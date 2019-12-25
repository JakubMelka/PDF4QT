//    Copyright (C) 2019 Jakub Melka
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
#include "pdfviewersettingsdialog.h"
#include "pdfdocumentpropertiesdialog.h"

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
    m_settings(new PDFViewerSettings(this)),
    m_pdfWidget(nullptr),
    m_sidebarDockWidget(nullptr),
    m_optionalContentActivity(nullptr),
    m_pageNumberSpinBox(nullptr),
    m_pageNumberLabel(nullptr),
    m_pageZoomSpinBox(nullptr),
    m_isLoadingUI(false),
    m_progress(new pdf::PDFProgress(this)),
    m_taskbarButton(new QWinTaskbarButton(this)),
    m_progressTaskbarIndicator(nullptr)
{
    ui->setupUi(this);

    // Initialize task bar progress
    m_progressTaskbarIndicator = m_taskbarButton->progress();

    // Initialize shortcuts
    ui->actionOpen->setShortcut(QKeySequence::Open);
    ui->actionClose->setShortcut(QKeySequence::Close);
    ui->actionQuit->setShortcut(QKeySequence::Quit);
    ui->actionZoom_In->setShortcut(QKeySequence::ZoomIn);
    ui->actionZoom_Out->setShortcut(QKeySequence::ZoomOut);

    connect(ui->actionOpen, &QAction::triggered, this, &PDFViewerMainWindow::onActionOpenTriggered);
    connect(ui->actionClose, &QAction::triggered, this, &PDFViewerMainWindow::onActionCloseTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &PDFViewerMainWindow::onActionQuitTriggered);

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

    connect(ui->actionZoom_In, &QAction::triggered, this, [this] { m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomIn); });
    connect(ui->actionZoom_Out, &QAction::triggered, this, [this] { m_pdfWidget->getDrawWidgetProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomOut); });

    readSettings();

    m_pdfWidget = new pdf::PDFWidget(m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1, this);
    setCentralWidget(m_pdfWidget);
    setFocusProxy(m_pdfWidget);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());

    m_sidebarWidget = new PDFSidebarWidget(m_pdfWidget->getDrawWidgetProxy(), this);
    m_sidebarDockWidget = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDockWidget->setObjectName("SidebarDockWidget");
    m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDockWidget->setWidget(m_sidebarWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDockWidget);
    m_sidebarDockWidget->hide();
    connect(m_sidebarWidget, &PDFSidebarWidget::actionTriggered, this, &PDFViewerMainWindow::onActionTriggered);

    ui->actionRenderOptionAntialiasing->setData(pdf::PDFRenderer::Antialiasing);
    ui->actionRenderOptionTextAntialiasing->setData(pdf::PDFRenderer::TextAntialiasing);
    ui->actionRenderOptionSmoothPictures->setData(pdf::PDFRenderer::SmoothImages);
    ui->actionRenderOptionIgnoreOptionalContentSettings->setData(pdf::PDFRenderer::IgnoreOptionalContent);

    for (QAction* action : getRenderingOptionActions())
    {
        connect(action, &QAction::triggered, this, &PDFViewerMainWindow::onRenderingOptionTriggered);
    }

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");

    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFViewerMainWindow::onDrawSpaceChanged);
    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFViewerMainWindow::onPageLayoutChanged);
    connect(m_pdfWidget, &pdf::PDFWidget::pageRenderingErrorsChanged, this, &PDFViewerMainWindow::onPageRenderingErrorsChanged, Qt::QueuedConnection);
    connect(m_settings, &PDFViewerSettings::settingsChanged, this, &PDFViewerMainWindow::onViewerSettingsChanged);
    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFViewerMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFViewerMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFViewerMainWindow::onProgressFinished);
    connect(&m_futureWatcher, &QFutureWatcher<AsyncReadingResult>::finished, this, &PDFViewerMainWindow::onDocumentReadingFinished);
    connect(this, &PDFViewerMainWindow::queryPasswordRequest, this, &PDFViewerMainWindow::onQueryPasswordRequest, Qt::BlockingQueuedConnection);

    readActionSettings();
    updatePageLayoutActions();
    updateUI(true);
    onViewerSettingsChanged();
    updateActionsAvailability();
}

PDFViewerMainWindow::~PDFViewerMainWindow()
{
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

            default:
                break;
        }
    }
}

void PDFViewerMainWindow::onProgressStarted()
{
    m_progressTaskbarIndicator->setRange(0, 100);
    m_progressTaskbarIndicator->reset();
    m_progressTaskbarIndicator->show();
}

void PDFViewerMainWindow::onProgressStep(int percentage)
{
    m_progressTaskbarIndicator->setValue(percentage);
}

void PDFViewerMainWindow::onProgressFinished()
{
    m_progressTaskbarIndicator->hide();
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
}

void PDFViewerMainWindow::updateTitle()
{
    if (m_pdfDocument)
    {
        QString title = m_pdfDocument->getInfo()->title;
        if (title.isEmpty())
        {
            title = m_currentFile;
        }
        setWindowTitle(tr("%1 - PDF Viewer").arg(m_currentFile));
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
    const bool isReading = m_futureWatcher.isRunning();
    const bool hasDocument = m_pdfDocument;
    const bool hasValidDocument = !isReading && hasDocument;

    ui->actionOpen->setEnabled(!isReading);
    ui->actionClose->setEnabled(hasValidDocument);
    ui->actionQuit->setEnabled(!isReading);
    ui->actionOptions->setEnabled(!isReading);
    ui->actionAbout->setEnabled(!isReading);
    ui->actionFitPage->setEnabled(hasValidDocument);
    ui->actionFitWidth->setEnabled(hasValidDocument);
    ui->actionFitHeight->setEnabled(hasValidDocument);
    ui->actionRendering_Errors->setEnabled(hasValidDocument);
}

void PDFViewerMainWindow::onViewerSettingsChanged()
{
    m_pdfWidget->updateRenderer(m_settings->getRendererEngine(), m_settings->isMultisampleAntialiasingEnabled() ? m_settings->getRendererSamples() : -1);
    m_pdfWidget->updateCacheLimits(m_settings->getCompiledPageCacheLimit() * 1024, m_settings->getThumbnailsCacheLimit(), m_settings->getFontCacheLimit(), m_settings->getInstancedFontCacheLimit());
    m_pdfWidget->getDrawWidgetProxy()->setFeatures(m_settings->getFeatures());
    m_pdfWidget->getDrawWidgetProxy()->setPreferredMeshResolutionRatio(m_settings->getPreferredMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setMinimalMeshResolutionRatio(m_settings->getMinimalMeshResolutionRatio());
    m_pdfWidget->getDrawWidgetProxy()->setColorTolerance(m_settings->getColorTolerance());

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

void PDFViewerMainWindow::openDocument(const QString& fileName)
{
    // First close old document
    closeDocument();

    QFileInfo fileInfo(fileName);
    m_fileInfo.originalFileName = fileName;
    m_fileInfo.fileName = fileInfo.fileName();
    m_fileInfo.path = fileInfo.path();
    m_fileInfo.fileSize = fileInfo.size();
    m_fileInfo.writable = fileInfo.isWritable();
    m_fileInfo.creationTime = fileInfo.created();
    m_fileInfo.lastModifiedTime = fileInfo.lastModified();
    m_fileInfo.lastReadTime = fileInfo.lastRead();

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
            result.document.reset(new pdf::PDFDocument(qMove(document)));
        }

        return result;
    };
    m_future = QtConcurrent::run(readDocument);
    m_futureWatcher.setFuture(m_future);
    updateActionsAvailability();
}

void PDFViewerMainWindow::onDocumentReadingFinished()
{
    QApplication::restoreOverrideCursor();

    AsyncReadingResult result = m_future.result();
    switch (result.result)
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            // Mark current directory as this
            QFileInfo fileInfo(m_fileInfo.originalFileName);
            m_settings->setDirectory(fileInfo.dir().absolutePath());
            m_currentFile = fileInfo.fileName();

            m_pdfDocument = result.document;
            setDocument(m_pdfDocument.data());

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
}

void PDFViewerMainWindow::setDocument(const pdf::PDFDocument* document)
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

    m_pdfWidget->setDocument(document, m_optionalContentActivity);
    m_sidebarWidget->setDocument(document, m_optionalContentActivity);

    if (m_sidebarWidget->isEmpty())
    {
        m_sidebarDockWidget->hide();
    }
    else
    {
        m_sidebarDockWidget->show();
    }

    updateTitle();
    updateUI(true);

    if (m_pdfDocument)
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
    setDocument(nullptr);
    m_pdfDocument.reset();
    updateActionsAvailability();
}

void PDFViewerMainWindow::setPageLayout(pdf::PageLayout pageLayout)
{
    m_pdfWidget->getDrawWidgetProxy()->setPageLayout(pageLayout);
}

std::vector<QAction*> PDFViewerMainWindow::getRenderingOptionActions() const
{
    return { ui->actionRenderOptionAntialiasing, ui->actionRenderOptionTextAntialiasing, ui->actionRenderOptionSmoothPictures, ui->actionRenderOptionIgnoreOptionalContentSettings };
}

QList<QAction*> PDFViewerMainWindow::getActions() const
{
    return findChildren<QAction*>(QString(), Qt::FindChildrenRecursively);
}

int PDFViewerMainWindow::adjustDpiX(int value)
{
    const int physicalDpiX = this->physicalDpiX();
    const int adjustedValue = (value * physicalDpiX) / 96;
    return adjustedValue;
}

void PDFViewerMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_futureWatcher.isRunning())
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

void PDFViewerMainWindow::on_actionOptions_triggered()
{
    PDFViewerSettingsDialog dialog(m_settings->getSettings(), m_settings->getColorManagementSystemSettings(), getActions(), m_CMSManager, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_settings->setSettings(dialog.getSettings());
        m_settings->setColorManagementSystemSettings(dialog.getCMSSettings());
        m_CMSManager->setSettings(m_settings->getColorManagementSystemSettings());
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

}   // namespace pdfviewer
