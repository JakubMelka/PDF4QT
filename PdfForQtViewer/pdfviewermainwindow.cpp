//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfviewermainwindow.h"
#include "ui_pdfviewermainwindow.h"

#include "pdfaboutdialog.h"
#include "pdfsidebarwidget.h"
#include "pdfadvancedfindwidget.h"
#include "pdfviewersettingsdialog.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfrendertoimagesdialog.h"
#include "pdfoptimizedocumentdialog.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"
#include "pdfstreamfilters.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfrenderingerrorswidget.h"
#include "pdffont.h"
#include "pdfitemmodels.h"
#include "pdfutils.h"
#include "pdfsendmail.h"
#include "pdfexecutionpolicy.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfsignaturehandler.h"

#include <QPainter>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QDesktopWidget>
#include <QStandardPaths>
#include <QDockWidget>
#include <QTreeView>
#include <QLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QSpinBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLockFile>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtConcurrent/QtConcurrent>

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdfviewer
{

PDFViewerMainWindow::PDFViewerMainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::PDFViewerMainWindow),
    m_CMSManager(new pdf::PDFCMSManager(this)),
    m_recentFileManager(new PDFRecentFileManager(this)),
    m_settings(new PDFViewerSettings(this)),
    m_pdfWidget(nullptr),
    m_sidebarWidget(nullptr),
    m_sidebarDockWidget(nullptr),
    m_advancedFindWidget(nullptr),
    m_advancedFindDockWidget(nullptr),
    m_optionalContentActivity(nullptr),
    m_pageNumberSpinBox(nullptr),
    m_pageNumberLabel(nullptr),
    m_pageZoomSpinBox(nullptr),
    m_isLoadingUI(false),
    m_progress(new pdf::PDFProgress(this)),
    m_taskbarButton(new QWinTaskbarButton(this)),
    m_progressTaskbarIndicator(nullptr),
    m_futureWatcher(nullptr),
    m_progressDialog(nullptr),
    m_isBusy(false),
    m_isChangingProgressStep(false),
    m_toolManager(nullptr),
    m_annotationManager(nullptr),
    m_formManager(nullptr),
    m_textToSpeech(new PDFTextToSpeech(this)),
    m_undoRedoManager(new PDFUndoRedoManager(this))
{
    ui->setupUi(this);

    // Initialize toolbar icon size
    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    ui->mainToolBar->setIconSize(iconSize);

    // Initialize task bar progress
    m_progressTaskbarIndicator = m_taskbarButton->progress();

    // Initialize shortcuts
    ui->actionOpen->setShortcut(QKeySequence::Open);
    ui->actionClose->setShortcut(QKeySequence::Close);
    ui->actionQuit->setShortcut(QKeySequence::Quit);
    ui->actionZoom_In->setShortcut(QKeySequence::ZoomIn);
    ui->actionZoom_Out->setShortcut(QKeySequence::ZoomOut);
    ui->actionFind->setShortcut(QKeySequence::Find);
    ui->actionFindPrevious->setShortcut(QKeySequence::FindPrevious);
    ui->actionFindNext->setShortcut(QKeySequence::FindNext);
    ui->actionSelectTextAll->setShortcut(QKeySequence::SelectAll);
    ui->actionDeselectText->setShortcut(QKeySequence::Deselect);
    ui->actionCopyText->setShortcut(QKeySequence::Copy);
    ui->actionRotateRight->setShortcut(QKeySequence("Ctrl+Shift++"));
    ui->actionRotateLeft->setShortcut(QKeySequence("Ctrl+Shift+-"));
    ui->actionPrint->setShortcut(QKeySequence::Print);
    ui->actionUndo->setShortcut(QKeySequence::Undo);
    ui->actionRedo->setShortcut(QKeySequence::Redo);
    ui->actionSave->setShortcut(QKeySequence::Save);
    ui->actionSave_As->setShortcut(QKeySequence::SaveAs);

    for (QAction* action : m_recentFileManager->getActions())
    {
        ui->menuFile->insertAction(ui->actionQuit, action);
    }
    ui->menuFile->insertSeparator(ui->actionQuit);

    connect(ui->actionOpen, &QAction::triggered, this, &PDFViewerMainWindow::onActionOpenTriggered);
    connect(ui->actionClose, &QAction::triggered, this, &PDFViewerMainWindow::onActionCloseTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &PDFViewerMainWindow::onActionQuitTriggered);
    connect(m_recentFileManager, &PDFRecentFileManager::fileOpenRequest, this, &PDFViewerMainWindow::openDocument);

    auto createGoToAction = [this](QMenu* menu, QString name, QString text, QKeySequence::StandardKey key, pdf::PDFDrawWidgetProxy::Operation operation, QString iconPath)
    {
        QIcon icon;
        icon.addFile(iconPath);
        QAction* action = new QAction(icon, text, this);
        action->setObjectName(name);
        action->setShortcut(key);
        menu->addAction(action);

        auto onTriggered = [this, operation]()
        {
            m_pdfWidget->getDrawWidgetProxy()->performOperation(operation);
        };
        connect(action, &QAction::triggered, this, onTriggered);
        return action;
    };

    QAction* actionGoToDocumentStart = createGoToAction(ui->menuGoTo, "actionGoToDocumentStart", tr("Go to document start"), QKeySequence::MoveToStartOfDocument, pdf::PDFDrawWidgetProxy::NavigateDocumentStart, ":/resources/previous-start.svg");
    QAction* actionGoToDocumentEnd = createGoToAction(ui->menuGoTo, "actionGoToDocumentEnd", tr("Go to document end"), QKeySequence::MoveToEndOfDocument, pdf::PDFDrawWidgetProxy::NavigateDocumentEnd, ":/resources/next-end.svg");
    QAction* actionGoToNextPage = createGoToAction(ui->menuGoTo, "actionGoToNextPage", tr("Go to next page"), QKeySequence::MoveToNextPage, pdf::PDFDrawWidgetProxy::NavigateNextPage, ":/resources/next-page.svg");
    QAction* actionGoToPreviousPage = createGoToAction(ui->menuGoTo, "actionGoToPreviousPage", tr("Go to previous page"), QKeySequence::MoveToPreviousPage, pdf::PDFDrawWidgetProxy::NavigatePreviousPage, ":/resources/previous-page.svg");
    createGoToAction(ui->menuGoTo, "actionGoToNextLine", tr("Go to next line"), QKeySequence::MoveToNextLine, pdf::PDFDrawWidgetProxy::NavigateNextStep, ":/resources/next.svg");
    createGoToAction(ui->menuGoTo, "actionGoToPreviousLine", tr("Go to previous line"), QKeySequence::MoveToPreviousLine, pdf::PDFDrawWidgetProxy::NavigatePreviousStep, ":/resources/previous.svg");

    m_pageNumberSpinBox = new QSpinBox(this);
    m_pageNumberLabel = new QLabel(this);
    m_pageNumberSpinBox->setFixedWidth(adjustDpiX(80));
    m_pageNumberSpinBox->setAlignment(Qt::AlignCenter);
    connect(m_pageNumberSpinBox, &QSpinBox::editingFinished, this, &PDFViewerMainWindow::onPageNumberSpinboxEditingFinished);

    // Page control
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(actionGoToDocumentStart);
    ui->mainToolBar->addAction(actionGoToPreviousPage);
    ui->mainToolBar->addWidget(m_pageNumberSpinBox);
    ui->mainToolBar->addWidget(m_pageNumberLabel);
    ui->mainToolBar->addAction(actionGoToNextPage);
    ui->mainToolBar->addAction(actionGoToDocumentEnd);

    // Zoom
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionZoom_In);
    ui->mainToolBar->addAction(ui->actionZoom_Out);

    m_pageZoomSpinBox = new QDoubleSpinBox(this);
    m_pageZoomSpinBox->setMinimum(pdf::PDFDrawWidgetProxy::getMinZoom() * 100);
    m_pageZoomSpinBox->setMaximum(pdf::PDFDrawWidgetProxy::getMaxZoom() * 100);
    m_pageZoomSpinBox->setDecimals(2);
    m_pageZoomSpinBox->setSuffix(tr("%"));
    m_pageZoomSpinBox->setFixedWidth(adjustDpiX(80));
    m_pageZoomSpinBox->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    connect(m_pageZoomSpinBox, &QDoubleSpinBox::editingFinished, this, &PDFViewerMainWindow::onPageZoomSpinboxEditingFinished);
    ui->mainToolBar->addWidget(m_pageZoomSpinBox);

    // Fit page, width, height
    ui->mainToolBar->addAction(ui->actionFitPage);
    ui->mainToolBar->addAction(ui->actionFitWidth);
    ui->mainToolBar->addAction(ui->actionFitHeight);
    ui->mainToolBar->addSeparator();

    // Tools
    ui->mainToolBar->addAction(ui->actionSelectText);
    ui->mainToolBar->addAction(ui->actionMagnifier);
    ui->mainToolBar->addAction(ui->actionScreenshot);
    ui->mainToolBar->addAction(ui->actionExtractImage);

    connect(ui->actionZoom_In, &QAction::triggered, this, [this] { m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomIn); });
    connect(ui->actionZoom_Out, &QAction::triggered, this, [this] { m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomOut); });

    readSettings();

    m_pdfWidget = new pdf::PDFWidget(m_CMSManager, m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1, this);
    setCentralWidget(m_pdfWidget);
    setFocusProxy(m_pdfWidget);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());
    m_pdfWidget->getDrawWidgetProxy()->setProgress(m_progress);
    m_textToSpeech->setProxy(m_pdfWidget->getDrawWidgetProxy());

    m_sidebarWidget = new PDFSidebarWidget(m_pdfWidget->getDrawWidgetProxy(), m_textToSpeech, &m_certificateStore, m_settings, this);
    m_sidebarDockWidget = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDockWidget->setObjectName("SidebarDockWidget");
    m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDockWidget->setWidget(m_sidebarWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDockWidget);
    m_sidebarDockWidget->hide();
    connect(m_sidebarWidget, &PDFSidebarWidget::actionTriggered, this, &PDFViewerMainWindow::onActionTriggered);
    m_textToSpeech->setSettings(m_settings);

    m_advancedFindWidget = new PDFAdvancedFindWidget(m_pdfWidget->getDrawWidgetProxy(), this);
    m_advancedFindDockWidget = new QDockWidget(tr("Advanced find"), this);
    m_advancedFindDockWidget->setObjectName("AdvancedFind");
    m_advancedFindDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_advancedFindDockWidget->setWidget(m_advancedFindWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_advancedFindDockWidget);
    m_advancedFindDockWidget->hide();
    QAction* toggleAdvancedFindAction = m_advancedFindDockWidget->toggleViewAction();
    toggleAdvancedFindAction->setObjectName("actionAdvancedFind");
    toggleAdvancedFindAction->setText(tr("Advanced Find"));
    toggleAdvancedFindAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    toggleAdvancedFindAction->setIcon(QIcon(":/resources/find-advanced.svg"));
    ui->menuEdit->insertAction(nullptr, toggleAdvancedFindAction);

    ui->actionRenderOptionAntialiasing->setData(pdf::PDFRenderer::Antialiasing);
    ui->actionRenderOptionTextAntialiasing->setData(pdf::PDFRenderer::TextAntialiasing);
    ui->actionRenderOptionSmoothPictures->setData(pdf::PDFRenderer::SmoothImages);
    ui->actionRenderOptionIgnoreOptionalContentSettings->setData(pdf::PDFRenderer::IgnoreOptionalContent);
    ui->actionRenderOptionDisplayAnnotations->setData(pdf::PDFRenderer::DisplayAnnotations);
    ui->actionInvertColors->setData(pdf::PDFRenderer::InvertColors);
    ui->actionShow_Text_Blocks->setData(pdf::PDFRenderer::DebugTextBlocks);
    ui->actionShow_Text_Lines->setData(pdf::PDFRenderer::DebugTextLines);

    for (QAction* action : getRenderingOptionActions())
    {
        connect(action, &QAction::triggered, this, &PDFViewerMainWindow::onRenderingOptionTriggered);
    }

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");

    // Initialize tools
    pdf::PDFToolManager::Actions actions;
    actions.findPrevAction = ui->actionFindPrevious;
    actions.findNextAction = ui->actionFindNext;
    actions.selectTextToolAction = ui->actionSelectText;
    actions.selectAllAction = ui->actionSelectTextAll;
    actions.deselectAction = ui->actionDeselectText;
    actions.copyTextAction = ui->actionCopyText;
    actions.magnifierAction = ui->actionMagnifier;
    actions.screenshotToolAction = ui->actionScreenshot;
    actions.extractImageAction = ui->actionExtractImage;
    m_toolManager = new pdf::PDFToolManager(m_pdfWidget->getDrawWidgetProxy(), actions, this, this);
    m_pdfWidget->setToolManager(m_toolManager);
    updateMagnifierToolSettings();
    connect(m_toolManager, &pdf::PDFToolManager::messageDisplayRequest, statusBar(), &QStatusBar::showMessage);

    m_annotationManager = new pdf::PDFWidgetAnnotationManager(m_pdfWidget->getDrawWidgetProxy(), this);
    connect(m_annotationManager, &pdf::PDFWidgetAnnotationManager::actionTriggered, this, &PDFViewerMainWindow::onActionTriggered);
    m_pdfWidget->setAnnotationManager(m_annotationManager);

    m_formManager = new pdf::PDFFormManager(m_pdfWidget->getDrawWidgetProxy(), this);
    m_formManager->setAnnotationManager(m_annotationManager);
    m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
    m_annotationManager->setFormManager(m_formManager);
    m_pdfWidget->setFormManager(m_formManager);
    connect(m_formManager, &pdf::PDFFormManager::actionTriggered, this, &PDFViewerMainWindow::onActionTriggered);
    connect(m_formManager, &pdf::PDFFormManager::documentModified, this, &PDFViewerMainWindow::onDocumentModified);

    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFViewerMainWindow::onDrawSpaceChanged);
    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFViewerMainWindow::onPageLayoutChanged);
    connect(m_pdfWidget, &pdf::PDFWidget::pageRenderingErrorsChanged, this, &PDFViewerMainWindow::onPageRenderingErrorsChanged, Qt::QueuedConnection);
    connect(m_settings, &PDFViewerSettings::settingsChanged, this, &PDFViewerMainWindow::onViewerSettingsChanged);
    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFViewerMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFViewerMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFViewerMainWindow::onProgressFinished);
    connect(this, &PDFViewerMainWindow::queryPasswordRequest, this, &PDFViewerMainWindow::onQueryPasswordRequest, Qt::BlockingQueuedConnection);
    connect(ui->actionFind, &QAction::triggered, this, [this] { m_toolManager->setActiveTool(m_toolManager->getFindTextTool()); });

    // Connect undo/redo manager
    connect(m_undoRedoManager, &PDFUndoRedoManager::undoRedoStateChanged, this, &PDFViewerMainWindow::updateUndoRedoActions);
    connect(m_undoRedoManager, &PDFUndoRedoManager::documentChangeRequest, this, &PDFViewerMainWindow::onDocumentUndoRedo);
    connect(ui->actionUndo, &QAction::triggered, m_undoRedoManager, &PDFUndoRedoManager::doUndo);
    connect(ui->actionRedo, &QAction::triggered, m_undoRedoManager, &PDFUndoRedoManager::doRedo);
    updateUndoRedoSettings();

    readActionSettings();
    updatePageLayoutActions();
    updateUI(true);
    onViewerSettingsChanged();
    updateActionsAvailability();
}

PDFViewerMainWindow::~PDFViewerMainWindow()
{
    delete m_formManager;
    m_formManager = nullptr;

    delete m_annotationManager;
    m_annotationManager = nullptr;

    delete ui;
}

void PDFViewerMainWindow::onActionOpenTriggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), m_settings->getDirectory(), tr("PDF document (*.pdf)"));
    if (!fileName.isEmpty())
    {
        openDocument(fileName);
    }
}

void PDFViewerMainWindow::onActionCloseTriggered()
{
    closeDocument();
}

void PDFViewerMainWindow::onActionQuitTriggered()
{
    close();
}

void PDFViewerMainWindow::onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount)
{
    if (errorsCount > 0)
    {
        statusBar()->showMessage(tr("Rendering of page %1: %2 errors occured.").arg(pageIndex + 1).arg(errorsCount), 4000);
    }
}

void PDFViewerMainWindow::onDrawSpaceChanged()
{
    updateUI(false);
}

void PDFViewerMainWindow::onPageLayoutChanged()
{
    updateUI(false);
    updatePageLayoutActions();
}

void PDFViewerMainWindow::onPageNumberSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageNumberSpinBox->hasFocus())
    {
        m_pdfWidget->setFocus();
    }

    m_pdfWidget->getDrawWidgetProxy()->goToPage(m_pageNumberSpinBox->value() - 1);
}

void PDFViewerMainWindow::onPageZoomSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageZoomSpinBox->hasFocus())
    {
        m_pdfWidget->setFocus();
    }

    m_pdfWidget->getDrawWidgetProxy()->zoom(m_pageZoomSpinBox->value() / 100.0);
}

void PDFViewerMainWindow::onActionTriggered(const pdf::PDFAction* action)
{
    Q_ASSERT(action);

    for (const pdf::PDFAction* currentAction : action->getActionList())
    {
        switch (action->getType())
        {
            case pdf::ActionType::GoTo:
            {
                const pdf::PDFActionGoTo* typedAction = dynamic_cast<const pdf::PDFActionGoTo*>(currentAction);
                pdf::PDFDestination destination = typedAction->getDestination();
                if (destination.getDestinationType() == pdf::DestinationType::Named)
                {
                    if (const pdf::PDFDestination* targetDestination = m_pdfDocument->getCatalog()->getDestination(destination.getName()))
                    {
                        destination = *targetDestination;
                    }
                    else
                    {
                        destination = pdf::PDFDestination();
                        QMessageBox::critical(this, tr("Go to action"), tr("Failed to go to destination '%1'. Destination wasn't found.").arg(pdf::PDFEncoding::convertTextString(destination.getName())));
                    }
                }

                if (destination.getDestinationType() != pdf::DestinationType::Invalid &&
                    destination.getPageReference() != pdf::PDFObjectReference())
                {
                    const size_t pageIndex = m_pdfDocument->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                    if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
                    {
                        m_pdfWidget->getDrawWidgetProxy()->goToPage(pageIndex);

                        switch (destination.getDestinationType())
                        {
                            case pdf::DestinationType::Fit:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
                                break;

                            case pdf::DestinationType::FitH:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);
                                break;

                            case pdf::DestinationType::FitV:
                                m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);
                                break;

                            default:
                                break;
                        }
                    }
                }

                break;
            }

            case pdf::ActionType::Launch:
            {
                if (!m_settings->getSettings().m_allowLaunchApplications)
                {
                    // Launching of applications is disabled -> continue to next action
                    continue;
                }

                const pdf::PDFActionLaunch* typedAction = dynamic_cast<const pdf::PDFActionLaunch*>(currentAction);
#ifdef Q_OS_WIN
                const pdf::PDFActionLaunch::Win& winSpecification = typedAction->getWinSpecification();
                if (!winSpecification.file.isEmpty())
                {
                    QString message = tr("Would you like to launch application '%1' in working directory '%2' with parameters '%3'?").arg(QString::fromLatin1(winSpecification.file), QString::fromLatin1(winSpecification.directory), QString::fromLatin1(winSpecification.parameters));
                    if (QMessageBox::question(this, tr("Launch application"), message) == QMessageBox::Yes)
                    {
                        auto getStringOrNULL = [](const QByteArray& array) -> LPCSTR
                        {
                            if (!array.isEmpty())
                            {
                                return array.data();
                            }
                            return NULL;
                        };

                        const HINSTANCE result = ::ShellExecuteA(NULL, getStringOrNULL(winSpecification.operation), getStringOrNULL(winSpecification.file), getStringOrNULL(winSpecification.parameters), getStringOrNULL(winSpecification.directory), SW_SHOWNORMAL);
                        if (result <= HINSTANCE(32))
                        {
                            // Error occured
                            QMessageBox::warning(this, tr("Launch application"), tr("Executing application failed. Error code is %1.").arg(reinterpret_cast<intptr_t>(result)));
                        }
                    }

                    // Continue next action
                    continue;
                }

                const pdf::PDFFileSpecification& fileSpecification = typedAction->getFileSpecification();
                QString plaftormFileName = fileSpecification.getPlatformFileName();
                if (!plaftormFileName.isEmpty())
                {
                    QString message = tr("Would you like to launch application '%1'?").arg(plaftormFileName);
                    if (QMessageBox::question(this, tr("Launch application"), message) == QMessageBox::Yes)
                    {
                        const HINSTANCE result = ::ShellExecuteW(NULL, NULL, plaftormFileName.toStdWString().c_str(), NULL, NULL, SW_SHOWNORMAL);
                        if (result <= HINSTANCE(32))
                        {
                            // Error occured
                            QMessageBox::warning(this, tr("Launch application"), tr("Executing application failed. Error code is %1.").arg(reinterpret_cast<intptr_t>(result)));
                        }
                    }

                    // Continue next action
                    continue;
                }
#endif
                break;
            }

            case pdf::ActionType::URI:
            {
                if (!m_settings->getSettings().m_allowLaunchURI)
                {
                    // Launching of URI is disabled -> continue to next action
                    continue;
                }

                const pdf::PDFActionURI* typedAction = dynamic_cast<const pdf::PDFActionURI*>(currentAction);
                QByteArray URI = m_pdfDocument->getCatalog()->getBaseURI() + typedAction->getURI();
                QString urlString = QString::fromLatin1(URI);
                QString message = tr("Would you like to open URL '%1'?").arg(urlString);
                if (QMessageBox::question(this, tr("Open URL"), message) == QMessageBox::Yes)
                {
                    if (!QDesktopServices::openUrl(QUrl(urlString)))
                    {
                        // Error occured
                        QMessageBox::warning(this, tr("Open URL"), tr("Opening url '%1' failed.").arg(urlString));
                    }
                }

                break;
            }

            case pdf::ActionType::Named:
            {
                const pdf::PDFActionNamed* typedAction = dynamic_cast<const pdf::PDFActionNamed*>(currentAction);
                switch (typedAction->getNamedActionType())
                {
                    case pdf::PDFActionNamed::NamedActionType::NextPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateNextPage);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::PrevPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::FirstPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentStart);
                        break;

                    case pdf::PDFActionNamed::NamedActionType::LastPage:
                        m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::NavigateDocumentEnd);
                        break;

                    default:
                        break;
                }

                break;
            }

            case pdf::ActionType::SetOCGState:
            {
                const pdf::PDFActionSetOCGState* typedAction = dynamic_cast<const pdf::PDFActionSetOCGState*>(currentAction);
                const pdf::PDFActionSetOCGState::StateChangeItems& stateChanges = typedAction->getStateChangeItems();
                const bool isRadioButtonsPreserved = typedAction->isRadioButtonsPreserved();

                if (m_optionalContentActivity)
                {
                    for (const pdf::PDFActionSetOCGState::StateChangeItem& stateChange : stateChanges)
                    {
                        pdf::OCState newState = pdf::OCState::Unknown;
                        switch (stateChange.first)
                        {
                            case pdf::PDFActionSetOCGState::SwitchType::ON:
                                newState = pdf::OCState::ON;
                                break;

                            case pdf::PDFActionSetOCGState::SwitchType::OFF:
                                newState = pdf::OCState::OFF;
                                break;

                            case pdf::PDFActionSetOCGState::SwitchType::Toggle:
                            {
                                pdf::OCState oldState = m_optionalContentActivity->getState(stateChange.second);
                                switch (oldState)
                                {
                                    case pdf::OCState::ON:
                                        newState = pdf::OCState::OFF;
                                        break;

                                    case pdf::OCState::OFF:
                                        newState = pdf::OCState::ON;
                                        break;

                                    case pdf::OCState::Unknown:
                                        break;

                                    default:
                                        Q_ASSERT(false);
                                        break;
                                }

                                break;
                            }

                            default:
                                Q_ASSERT(false);
                        }

                        if (newState != pdf::OCState::Unknown)
                        {
                            m_optionalContentActivity->setState(stateChange.second, newState, isRadioButtonsPreserved);
                        }
                    }
                }

                break;
            }

            case pdf::ActionType::ResetForm:
            {
                m_formManager->performResetAction(dynamic_cast<const pdf::PDFActionResetForm*>(action));
                break;
            }

            default:
                break;
        }
    }
}

void PDFViewerMainWindow::onProgressStarted(pdf::ProgressStartupInfo info)
{
    Q_ASSERT(!m_progressDialog);
    if (info.showDialog)
    {
        m_progressDialog = new QProgressDialog(info.text, QString(), 0, 100, this);
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setCancelButton(nullptr);
    }

    m_progressTaskbarIndicator->setRange(0, 100);
    m_progressTaskbarIndicator->reset();
    m_progressTaskbarIndicator->show();
    m_isBusy = true;

    updateActionsAvailability();
}

void PDFViewerMainWindow::onProgressStep(int percentage)
{
    if (m_isChangingProgressStep)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isChangingProgressStep, true);

    if (m_progressDialog)
    {
        m_progressDialog->setValue(percentage);
    }

    m_progressTaskbarIndicator->setValue(percentage);
}

void PDFViewerMainWindow::onProgressFinished()
{
    if (m_progressDialog)
    {
        m_progressDialog->hide();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
    m_progressTaskbarIndicator->hide();
    m_isBusy = false;

    updateActionsAvailability();
}

void PDFViewerMainWindow::readSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
        QRect windowRect(0, 0, availableGeometry.width() / 2, availableGeometry.height() / 2);
        windowRect = windowRect.translated(availableGeometry.center() - windowRect.center());
        setGeometry(windowRect);
    }
    else
    {
        restoreGeometry(geometry);
    }

    QByteArray state = settings.value("windowState", QByteArray()).toByteArray();
    if (!state.isEmpty())
    {
        restoreState(state);
    }

    m_settings->readSettings(settings, m_CMSManager->getDefaultSettings());
    m_CMSManager->setSettings(m_settings->getColorManagementSystemSettings());
    m_textToSpeech->setSettings(m_settings);

    if (m_formManager)
    {
        m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
    }
}

void PDFViewerMainWindow::readActionSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    // Load action shortcuts
    settings.beginGroup("Actions");
    for (QAction* action : getActions())
    {
        QString name = action->objectName();
        if (!name.isEmpty() && settings.contains(name))
        {
            QKeySequence sequence = QKeySequence::fromString(settings.value(name, action->shortcut().toString(QKeySequence::PortableText)).toString(), QKeySequence::PortableText);
            action->setShortcut(sequence);
        }
    }
    settings.endGroup();

    // Load recent files
    settings.beginGroup("RecentFiles");
    m_recentFileManager->setRecentFilesLimit(settings.value("MaximumRecentFilesCount", PDFRecentFileManager::getDefaultRecentFiles()).toInt());
    m_recentFileManager->setRecentFiles(settings.value("RecentFileList", QStringList()).toStringList());
    settings.endGroup();

    // Load trusted certificates
    QString trustedCertificateStoreFileName = getTrustedCertificateStoreFileName();
    QString trustedCertificateStoreLockFileName = trustedCertificateStoreFileName + ".lock";

    QLockFile lockFile(trustedCertificateStoreLockFileName);
    if (lockFile.lock())
    {
        QFile trustedCertificateStoreFile(trustedCertificateStoreFileName);
        if (trustedCertificateStoreFile.open(QFile::ReadOnly))
        {
            QDataStream stream(&trustedCertificateStoreFile);
            m_certificateStore.deserialize(stream);
            trustedCertificateStoreFile.close();
        }
        lockFile.unlock();
    }
}

void PDFViewerMainWindow::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    m_settings->writeSettings(settings);

    // Save action shortcuts
    settings.beginGroup("Actions");
    for (QAction* action : getActions())
    {
        QString name = action->objectName();
        if (!name.isEmpty())
        {
            QString accelerator = action->shortcut().toString(QKeySequence::PortableText);
            settings.setValue(name, accelerator);
        }
    }
    settings.endGroup();

    // Save recent files
    settings.beginGroup("RecentFiles");
    settings.setValue("MaximumRecentFilesCount", m_recentFileManager->getRecentFilesLimit());
    settings.setValue("RecentFileList", m_recentFileManager->getRecentFiles());
    settings.endGroup();

    // Save trusted certificates
    QString trustedCertificateStoreFileName = getTrustedCertificateStoreFileName();
    QString trustedCertificateStoreLockFileName = trustedCertificateStoreFileName + ".lock";

    QFileInfo fileInfo(trustedCertificateStoreFileName);
    QDir dir = fileInfo.dir();
    dir.mkpath(dir.path());

    QLockFile lockFile(trustedCertificateStoreLockFileName);
    if (lockFile.lock())
    {
        QFile trustedCertificateStoreFile(trustedCertificateStoreFileName);
        if (trustedCertificateStoreFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QDataStream stream(&trustedCertificateStoreFile);
            m_certificateStore.serialize(stream);
            trustedCertificateStoreFile.close();
        }
        lockFile.unlock();
    }
}

void PDFViewerMainWindow::updateTitle()
{
    if (m_pdfDocument)
    {
        QString title = m_pdfDocument->getInfo()->title;
        if (title.isEmpty())
        {
            title = m_fileInfo.fileName;
        }
        setWindowTitle(tr("%1 - PDF Viewer").arg(m_fileInfo.fileName));
    }
    else
    {
        setWindowTitle(tr("PDF Viewer"));
    }
}

void PDFViewerMainWindow::updatePageLayoutActions()
{
    for (QAction* action : { ui->actionPageLayoutContinuous, ui->actionPageLayoutSinglePage, ui->actionPageLayoutTwoColumns, ui->actionPageLayoutTwoPages })
    {
        action->setChecked(false);
    }

    const pdf::PageLayout pageLayout = m_pdfWidget->getDrawWidgetProxy()->getPageLayout();
    switch (pageLayout)
    {
        case pdf::PageLayout::SinglePage:
            ui->actionPageLayoutSinglePage->setChecked(true);
            break;

        case pdf::PageLayout::OneColumn:
            ui->actionPageLayoutContinuous->setChecked(true);
            break;

        case pdf::PageLayout::TwoColumnLeft:
        case pdf::PageLayout::TwoColumnRight:
            ui->actionPageLayoutTwoColumns->setChecked(true);
            ui->actionFirstPageOnRightSide->setChecked(pageLayout == pdf::PageLayout::TwoColumnRight);
            break;

        case pdf::PageLayout::TwoPagesLeft:
        case pdf::PageLayout::TwoPagesRight:
            ui->actionPageLayoutTwoPages->setChecked(true);
            ui->actionFirstPageOnRightSide->setChecked(pageLayout == pdf::PageLayout::TwoPagesRight);
            break;

        default:
            Q_ASSERT(false);
    }
}

void PDFViewerMainWindow::updateRenderingOptionActions()
{
    const pdf::PDFRenderer::Features features = m_settings->getFeatures();
    for (QAction* action : getRenderingOptionActions())
    {
        action->setChecked(features.testFlag(static_cast<pdf::PDFRenderer::Feature>(action->data().toInt())));
    }
}

void PDFViewerMainWindow::updateUI(bool fullUpdate)
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingUI, true);

    if (fullUpdate)
    {
        if (m_pdfDocument)
        {
            size_t pageCount = m_pdfDocument->getCatalog()->getPageCount();
            m_pageNumberSpinBox->setMinimum(1);
            m_pageNumberSpinBox->setMaximum(static_cast<int>(pageCount));
            m_pageNumberSpinBox->setEnabled(true);
            m_pageNumberLabel->setText(tr(" / %1").arg(pageCount));
        }
        else
        {
            m_pageNumberSpinBox->setEnabled(false);
            m_pageNumberLabel->setText(QString());
        }

        ui->actionProperties->setEnabled(m_pdfDocument);
        ui->actionSend_by_E_Mail->setEnabled(m_pdfDocument);
    }
    else
    {
        std::vector<pdf::PDFInteger> currentPages = m_pdfWidget->getDrawWidget()->getCurrentPages();
        if (!currentPages.empty())
        {
            m_pageNumberSpinBox->setValue(currentPages.front() + 1);

            // Prefetch pages, if it is enabled
            if (m_settings->isPagePrefetchingEnabled())
            {
                m_pdfWidget->getDrawWidgetProxy()->prefetchPages(currentPages.back());
            }
        }

        m_sidebarWidget->setCurrentPages(currentPages);
    }

    m_pageZoomSpinBox->setValue(m_pdfWidget->getDrawWidgetProxy()->getZoom() * 100);
}

void PDFViewerMainWindow::updateActionsAvailability()
{
    const bool isBusy = (m_futureWatcher && m_futureWatcher->isRunning()) || m_isBusy;
    const bool hasDocument = m_pdfDocument;
    const bool hasValidDocument = !isBusy && hasDocument;
    bool canPrint = false;
    if (m_pdfDocument)
    {
        const pdf::PDFObjectStorage& storage = m_pdfDocument->getStorage();
        const pdf::PDFSecurityHandler* securityHandler = storage.getSecurityHandler();
        canPrint = securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintLowResolution) ||
                   securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintHighResolution);
    }

    ui->actionOpen->setEnabled(!isBusy);
    ui->actionClose->setEnabled(hasValidDocument);
    ui->actionQuit->setEnabled(!isBusy);
    ui->actionOptions->setEnabled(!isBusy);
    ui->actionAbout->setEnabled(!isBusy);
    ui->actionFitPage->setEnabled(hasValidDocument);
    ui->actionFitWidth->setEnabled(hasValidDocument);
    ui->actionFitHeight->setEnabled(hasValidDocument);
    ui->actionRendering_Errors->setEnabled(hasValidDocument);
    ui->actionFind->setEnabled(hasValidDocument);
    ui->actionPrint->setEnabled(hasValidDocument && canPrint);
    ui->actionRender_to_Images->setEnabled(hasValidDocument && canPrint);
    ui->actionOptimize->setEnabled(hasValidDocument);
    ui->actionSave->setEnabled(hasValidDocument);
    ui->actionSave_As->setEnabled(hasValidDocument);
    setEnabled(!isBusy);
    updateUndoRedoActions();
}

void PDFViewerMainWindow::onViewerSettingsChanged()
{
    m_pdfWidget->updateRenderer(m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());
    m_pdfWidget->getDrawWidgetProxy()->setFeatures(m_settings->getFeatures());
    m_pdfWidget->getDrawWidgetProxy()->setPreferredMeshResolutionRatio(m_settings->getPreferredMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setMinimalMeshResolutionRatio(m_settings->getMinimalMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setColorTolerance(m_settings->getColorTolerance());
    m_annotationManager->setFeatures(m_settings->getFeatures());
    m_annotationManager->setMeshQualitySettings(m_pdfWidget->getDrawWidgetProxy()->getMeshQualitySettings());
    pdf::PDFExecutionPolicy::setStrategy(m_settings->getMultithreadingStrategy());

    updateRenderingOptionActions();
}

void PDFViewerMainWindow::onRenderingOptionTriggered(bool checked)
{
    QAction* action = qobject_cast<QAction*>(sender());
    Q_ASSERT(action);

    pdf::PDFRenderer::Features features = m_settings->getFeatures();
    features.setFlag(static_cast<pdf::PDFRenderer::Feature>(action->data().toInt()), checked);
    m_settings->setFeatures(features);
}

void PDFViewerMainWindow::updateFileInfo(const QString& fileName)
{
    QFileInfo fileInfo(fileName);
    m_fileInfo.originalFileName = fileName;
    m_fileInfo.fileName = fileInfo.fileName();
    m_fileInfo.path = fileInfo.path();
    m_fileInfo.fileSize = fileInfo.size();
    m_fileInfo.writable = fileInfo.isWritable();
    m_fileInfo.creationTime = fileInfo.created();
    m_fileInfo.lastModifiedTime = fileInfo.lastModified();
    m_fileInfo.lastReadTime = fileInfo.lastRead();
}

void PDFViewerMainWindow::openDocument(const QString& fileName)
{
    // First close old document
    closeDocument();

    updateFileInfo(fileName);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto readDocument = [this, fileName]() -> AsyncReadingResult
    {
        AsyncReadingResult result;

        auto queryPassword = [this](bool* ok)
        {
            QString result;
            *ok = false;
            emit queryPasswordRequest(&result, ok);
            return result;
        };

        // Try to open a new document
        pdf::PDFDocumentReader reader(m_progress, qMove(queryPassword));
        pdf::PDFDocument document = reader.readFromFile(fileName);

        result.errorMessage = reader.getErrorMessage();
        result.result = reader.getReadingResult();
        if (result.result == pdf::PDFDocumentReader::Result::OK)
        {
            // Verify signatures
            pdf::PDFSignatureHandler::Parameters parameters;
            parameters.store = &m_certificateStore;
            parameters.dss = &document.getCatalog()->getDocumentSecurityStore();
            parameters.enableVerification = m_settings->getSettings().m_signatureVerificationEnabled;
            parameters.ignoreExpirationDate = m_settings->getSettings().m_signatureIgnoreCertificateValidityTime;
            parameters.useSystemCertificateStore = m_settings->getSettings().m_signatureUseSystemStore;

            pdf::PDFForm form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
            result.signatures = pdf::PDFSignatureHandler::verifySignatures(form, reader.getSource(), parameters);
            result.document.reset(new pdf::PDFDocument(qMove(document)));
        }

        return result;
    };
    m_future = QtConcurrent::run(readDocument);
    m_futureWatcher = new QFutureWatcher<AsyncReadingResult>();
    connect(m_futureWatcher, &QFutureWatcher<AsyncReadingResult>::finished, this, &PDFViewerMainWindow::onDocumentReadingFinished);
    m_futureWatcher->setFuture(m_future);
    updateActionsAvailability();
}

void PDFViewerMainWindow::onDocumentReadingFinished()
{
    QApplication::restoreOverrideCursor();

    AsyncReadingResult result = m_future.result();
    m_future = QFuture<AsyncReadingResult>();
    m_futureWatcher->deleteLater();
    m_futureWatcher = nullptr;

    switch (result.result)
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            // Mark current directory as this
            QFileInfo fileInfo(m_fileInfo.originalFileName);
            m_settings->setDirectory(fileInfo.dir().absolutePath());

            // We add file to recent files only, if we have successfully read the document
            m_recentFileManager->addRecentFile(m_fileInfo.originalFileName);

            m_pdfDocument = qMove(result.document);
            m_signatures = qMove(result.signatures);
            pdf::PDFModifiedDocument document(m_pdfDocument.data(), m_optionalContentActivity);
            setDocument(document);

            statusBar()->showMessage(tr("Document '%1' was successfully loaded!").arg(m_fileInfo.fileName), 4000);
            break;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            QMessageBox::critical(this, tr("PDF Viewer"), tr("Document read error: %1").arg(result.errorMessage));
            break;
        }

        case pdf::PDFDocumentReader::Result::Cancelled:
            break; // Do nothing, user cancelled the document reading
    }
    updateActionsAvailability();
}

void PDFViewerMainWindow::onDocumentModified(pdf::PDFModifiedDocument document)
{
    // We will create undo/redo step from old document, with flags from the new,
    // because new document is modification of old document with flags.

    Q_ASSERT(m_pdfDocument);
    m_undoRedoManager->createUndo(document, m_pdfDocument);

    m_pdfDocument = document;
    document.setOptionalContentActivity(m_optionalContentActivity);
    setDocument(document);
}

void PDFViewerMainWindow::onDocumentUndoRedo(pdf::PDFModifiedDocument document)
{
    m_pdfDocument = document;
    document.setOptionalContentActivity(m_optionalContentActivity);
    setDocument(document);
}

void PDFViewerMainWindow::setDocument(pdf::PDFModifiedDocument document)
{
    if (document.hasReset())
    {
        if (m_optionalContentActivity)
        {
            // We use deleteLater, because we want to avoid consistency problem with model
            // (we set document to the model before activity).
            m_optionalContentActivity->deleteLater();
            m_optionalContentActivity = nullptr;
        }

        if (document)
        {
            m_optionalContentActivity = new pdf::PDFOptionalContentActivity(document, pdf::OCUsage::View, this);
        }

        m_undoRedoManager->clear();
    }
    else if (m_optionalContentActivity)
    {
        Q_ASSERT(document);
        m_optionalContentActivity->setDocument(document);
    }

    document.setOptionalContentActivity(m_optionalContentActivity);
    m_annotationManager->setDocument(document);
    m_formManager->setDocument(document);
    m_toolManager->setDocument(document);
    m_textToSpeech->setDocument(document);
    m_pdfWidget->setDocument(document);
    m_sidebarWidget->setDocument(document, m_signatures);
    m_advancedFindWidget->setDocument(document);

    if (m_sidebarWidget->isEmpty())
    {
        m_sidebarDockWidget->hide();
    }
    else
    {
        m_sidebarDockWidget->show();
    }

    if (!document)
    {
        m_advancedFindDockWidget->hide();
    }

    updateTitle();
    updateUI(true);

    if (m_pdfDocument && document.hasReset())
    {
        const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
        setPageLayout(catalog->getPageLayout());
        updatePageLayoutActions();

        if (const pdf::PDFAction* action = catalog->getOpenAction())
        {
            onActionTriggered(action);
        }
    }

    updateActionsAvailability();
}

void PDFViewerMainWindow::closeDocument()
{
    m_signatures.clear();
    setDocument(pdf::PDFModifiedDocument());
    m_pdfDocument.reset();
    updateActionsAvailability();
}

void PDFViewerMainWindow::setPageLayout(pdf::PageLayout pageLayout)
{
    m_pdfWidget->getDrawWidgetProxy()->setPageLayout(pageLayout);
}

std::vector<QAction*> PDFViewerMainWindow::getRenderingOptionActions() const
{
    return { ui->actionRenderOptionAntialiasing,
             ui->actionRenderOptionTextAntialiasing,
             ui->actionRenderOptionSmoothPictures,
             ui->actionRenderOptionIgnoreOptionalContentSettings,
             ui->actionRenderOptionDisplayAnnotations,
             ui->actionShow_Text_Blocks,
             ui->actionShow_Text_Lines,
             ui->actionInvertColors };
}

QList<QAction*> PDFViewerMainWindow::getActions() const
{
    return findChildren<QAction*>(QString(), Qt::FindChildrenRecursively);
}

QString PDFViewerMainWindow::getTrustedCertificateStoreFileName() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/TrustedCertStorage.bin";
}

int PDFViewerMainWindow::adjustDpiX(int value)
{
    const int physicalDpiX = this->physicalDpiX();
    const int adjustedValue = (value * physicalDpiX) / 96;
    return adjustedValue;
}

void PDFViewerMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_futureWatcher && m_futureWatcher->isRunning())
    {
        // Jakub Melka: Do not allow to close the application, if document
        // reading is running.
        event->ignore();
    }
    else
    {
        writeSettings();
        closeDocument();
        event->accept();
    }
}

void PDFViewerMainWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    m_taskbarButton->setWindow(windowHandle());
}

void PDFViewerMainWindow::onQueryPasswordRequest(QString* password, bool* ok)
{
    *password = QInputDialog::getText(this, tr("Encrypted document"), tr("Enter password to access document content"), QLineEdit::Password, QString(), ok);
}

void PDFViewerMainWindow::on_actionPageLayoutSinglePage_triggered()
{
    setPageLayout(pdf::PageLayout::SinglePage);
}

void PDFViewerMainWindow::on_actionPageLayoutContinuous_triggered()
{
    setPageLayout(pdf::PageLayout::OneColumn);
}

void PDFViewerMainWindow::on_actionPageLayoutTwoPages_triggered()
{
    setPageLayout(ui->actionFirstPageOnRightSide->isChecked() ? pdf::PageLayout::TwoPagesRight : pdf::PageLayout::TwoPagesLeft);
}

void PDFViewerMainWindow::on_actionPageLayoutTwoColumns_triggered()
{
    setPageLayout(ui->actionFirstPageOnRightSide->isChecked() ? pdf::PageLayout::TwoColumnRight : pdf::PageLayout::TwoColumnLeft);
}

void PDFViewerMainWindow::on_actionFirstPageOnRightSide_triggered()
{
    switch (m_pdfWidget->getDrawWidgetProxy()->getPageLayout())
    {
        case pdf::PageLayout::SinglePage:
        case pdf::PageLayout::OneColumn:
            break;

        case pdf::PageLayout::TwoColumnLeft:
        case pdf::PageLayout::TwoColumnRight:
            on_actionPageLayoutTwoColumns_triggered();
            break;

        case pdf::PageLayout::TwoPagesLeft:
        case pdf::PageLayout::TwoPagesRight:
            on_actionPageLayoutTwoPages_triggered();
            break;

        default:
            Q_ASSERT(false);
    }
}

void PDFViewerMainWindow::on_actionFitPage_triggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
}

void PDFViewerMainWindow::on_actionFitWidth_triggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);
}

void PDFViewerMainWindow::on_actionFitHeight_triggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);
}

void PDFViewerMainWindow::on_actionRendering_Errors_triggered()
{
    pdf::PDFRenderingErrorsWidget renderingErrorsDialog(this, m_pdfWidget);
    renderingErrorsDialog.exec();
}

void PDFViewerMainWindow::updateMagnifierToolSettings()
{
    pdf::PDFMagnifierTool* magnifierTool = m_toolManager->getMagnifierTool();
    magnifierTool->setMagnifierSize(pdf::PDFWidgetUtils::scaleDPI_x(this, m_settings->getSettings().m_magnifierSize));
    magnifierTool->setMagnifierZoom(m_settings->getSettings().m_magnifierZoom);
}

void PDFViewerMainWindow::updateUndoRedoSettings()
{
    const PDFViewerSettings::Settings& settings = m_settings->getSettings();
    m_undoRedoManager->setMaximumSteps(settings.m_maximumUndoSteps, settings.m_maximumRedoSteps);
}

void PDFViewerMainWindow::updateUndoRedoActions()
{
    const bool isBusy = (m_futureWatcher && m_futureWatcher->isRunning()) || m_isBusy;
    const bool canUndo = !isBusy && m_undoRedoManager->canUndo();
    const bool canRedo = !isBusy && m_undoRedoManager->canRedo();

    ui->actionUndo->setEnabled(canUndo);
    ui->actionRedo->setEnabled(canRedo);
}

void PDFViewerMainWindow::on_actionOptions_triggered()
{
    PDFViewerSettingsDialog::OtherSettings otherSettings;
    otherSettings.maximumRecentFileCount = m_recentFileManager->getRecentFilesLimit();

    PDFViewerSettingsDialog dialog(m_settings->getSettings(), m_settings->getColorManagementSystemSettings(), otherSettings, m_certificateStore, getActions(), m_CMSManager, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_settings->setSettings(dialog.getSettings());
        m_settings->setColorManagementSystemSettings(dialog.getCMSSettings());
        m_CMSManager->setSettings(m_settings->getColorManagementSystemSettings());
        m_recentFileManager->setRecentFilesLimit(dialog.getOtherSettings().maximumRecentFileCount);
        m_textToSpeech->setSettings(m_settings);
        m_formManager->setAppearanceFlags(m_settings->getSettings().m_formAppearanceFlags);
        m_certificateStore = dialog.getCertificateStore();
        updateMagnifierToolSettings();
        updateUndoRedoSettings();
    }
}

void PDFViewerMainWindow::on_actionProperties_triggered()
{
    Q_ASSERT(m_pdfDocument);

    PDFDocumentPropertiesDialog documentPropertiesDialog(m_pdfDocument.data(), &m_fileInfo, this);
    documentPropertiesDialog.exec();
}

void PDFViewerMainWindow::on_actionAbout_triggered()
{
    PDFAboutDialog dialog(this);
    dialog.exec();
}

void PDFViewerMainWindow::on_actionSend_by_E_Mail_triggered()
{
    Q_ASSERT(m_pdfDocument);

    QString subject = m_pdfDocument->getInfo()->title;
    if (subject.isEmpty())
    {
        subject = m_fileInfo.fileName;
    }

    if (!PDFSendMail::sendMail(this, subject, m_fileInfo.originalFileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("Error while starting email client occured!"));
    }
}

void PDFViewerMainWindow::on_actionRotateRight_triggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::RotateRight);
}

void PDFViewerMainWindow::on_actionRotateLeft_triggered()
{
    m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::RotateLeft);
}

void PDFViewerMainWindow::on_actionPrint_triggered()
{
    // Are we allowed to print in high resolution? If yes, then print in high resolution,
    // otherwise print in low resolution. If this action is triggered, then print operation
    // should be allowed (at least print in low resolution).
    const pdf::PDFObjectStorage& storage = m_pdfDocument->getStorage();
    const pdf::PDFSecurityHandler* securityHandler = storage.getSecurityHandler();
    QPrinter::PrinterMode printerMode = QPrinter::HighResolution;
    if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintHighResolution))
    {
        printerMode = QPrinter::ScreenResolution;
    }

    // Run print dialog
    QPrinter printer(printerMode);
    QPrintDialog printDialog(&printer, this);
    printDialog.setOptions(QPrintDialog::PrintPageRange | QPrintDialog::PrintShowPageSize | QPrintDialog::PrintCollateCopies | QPrintDialog::PrintSelection);
    printDialog.setOption(QPrintDialog::PrintCurrentPage, m_pdfWidget->getDrawWidget()->getCurrentPages().size() == 1);
    printDialog.setMinMax(1, int(m_pdfDocument->getCatalog()->getPageCount()));
    if (printDialog.exec() == QPrintDialog::Accepted)
    {
        std::vector<pdf::PDFInteger> pageIndices;
        switch (printDialog.printRange())
        {
            case QAbstractPrintDialog::AllPages:
            {
                pageIndices.resize(m_pdfDocument->getCatalog()->getPageCount(), 0);
                std::iota(pageIndices.begin(), pageIndices.end(), 0);
                break;
            }

            case QAbstractPrintDialog::Selection:
            case QAbstractPrintDialog::CurrentPage:
            {
                pageIndices = m_pdfWidget->getDrawWidget()->getCurrentPages();
                break;
            }

            case QAbstractPrintDialog::PageRange:
            {
                const pdf::PDFInteger fromPage = printDialog.fromPage();
                const pdf::PDFInteger toPage = printDialog.toPage();
                const pdf::PDFInteger pageCount = toPage - fromPage + 1;
                if (pageCount > 0)
                {
                    pageIndices.resize(pageCount, 0);
                    std::iota(pageIndices.begin(), pageIndices.end(), fromPage - 1);
                }
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }

        if (pageIndices.empty())
        {
            // Nothing to be printed
            return;
        }

        pdf::ProgressStartupInfo info;
        info.showDialog = true;
        info.text = tr("Printing document");
        m_progress->start(pageIndices.size(), qMove(info));
        printer.setFullPage(true);
        QPainter painter(&printer);

        const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
        pdf::PDFDrawWidgetProxy* proxy = m_pdfWidget->getDrawWidgetProxy();
        pdf::PDFOptionalContentActivity optionalContentActivity(m_pdfDocument.data(), pdf::OCUsage::Print, nullptr);
        pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
        pdf::PDFRenderer renderer(m_pdfDocument.get(), proxy->getFontCache(), cms.data(), &optionalContentActivity, proxy->getFeatures(), proxy->getMeshQualitySettings());

        const pdf::PDFInteger lastPage = pageIndices.back();
        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            const pdf::PDFPage* page = catalog->getPage(pageIndex);
            Q_ASSERT(page);

            QRectF mediaBox = page->getRotatedMediaBox();
            QRectF paperRect = printer.paperRect();
            QSizeF scaledSize = mediaBox.size().scaled(paperRect.size(), Qt::KeepAspectRatio);
            mediaBox.setSize(scaledSize);
            mediaBox.moveCenter(paperRect.center());

            renderer.render(&painter, mediaBox, pageIndex);
            m_progress->step();

            if (pageIndex != lastPage)
            {
                if (!printer.newPage())
                {
                    break;
                }
            }
        }

        painter.end();
        m_progress->finish();
    }
}

void PDFViewerMainWindow::on_actionRender_to_Images_triggered()
{
    PDFRenderToImagesDialog dialog(m_pdfDocument.data(), m_pdfWidget->getDrawWidgetProxy(), m_progress, this);
    dialog.exec();
}

void PDFViewerMainWindow::on_actionOptimize_triggered()
{
    PDFOptimizeDocumentDialog dialog(m_pdfDocument.data(), this);

    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(dialog.takeOptimizedDocument()));
        pdf::PDFModifiedDocument document(qMove(pointer), m_optionalContentActivity, pdf::PDFModifiedDocument::Reset);
        onDocumentModified(qMove(document));
    }
}

void PDFViewerMainWindow::on_actionSave_As_triggered()
{

    QFileInfo fileInfo(m_fileInfo.originalFileName);
    QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save As"), fileInfo.dir().absoluteFilePath(m_fileInfo.originalFileName), tr("Portable Document (*.pdf);;All files (*.*)"));
    if (!saveFileName.isEmpty())
    {
        saveDocument(saveFileName);
    }
}

void PDFViewerMainWindow::on_actionSave_triggered()
{
    saveDocument(m_fileInfo.originalFileName);
}

void PDFViewerMainWindow::saveDocument(const QString& fileName)
{
    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(fileName, m_pdfDocument.data(), true);
    if (result)
    {
        updateFileInfo(fileName);
        updateTitle();
        m_recentFileManager->addRecentFile(fileName);
    }
    else
    {
        QMessageBox::critical(this, tr("Error"), result.getErrorMessage());
    }
}

}   // namespace pdfviewer
