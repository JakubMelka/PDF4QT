//    Copyright (C) 2019-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

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
#include "pdfadvancedtools.h"
#include "pdfwidgetutils.h"

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
#include <QPluginLoader>
#include <QToolButton>
#include <QActionGroup>

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdfviewer
{

PDFViewerMainWindow::PDFViewerMainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::PDFViewerMainWindow),
    m_actionManager(new PDFActionManager(this)),
    m_programController(new PDFProgramController(this)),
    m_sidebarWidget(nullptr),
    m_sidebarDockWidget(nullptr),
    m_advancedFindWidget(nullptr),
    m_advancedFindDockWidget(nullptr),
    m_pageNumberSpinBox(nullptr),
    m_pageNumberLabel(nullptr),
    m_pageZoomSpinBox(nullptr),
    m_isLoadingUI(false),
    m_progress(new pdf::PDFProgress(this)),
    m_taskbarButton(new QWinTaskbarButton(this)),
    m_progressTaskbarIndicator(nullptr),
    m_progressDialog(nullptr),
    m_isChangingProgressStep(false)
{
    ui->setupUi(this);

    setAcceptDrops(true);

    // Initialize toolbar icon size
    adjustToolbar(ui->mainToolBar);

    // Initialize task bar progress
    m_progressTaskbarIndicator = m_taskbarButton->progress();

    // Initialize actions
    m_actionManager->setAction(PDFActionManager::Open, ui->actionOpen);
    m_actionManager->setAction(PDFActionManager::Close, ui->actionClose);
    m_actionManager->setAction(PDFActionManager::Quit, ui->actionQuit);
    m_actionManager->setAction(PDFActionManager::ZoomIn, ui->actionZoom_In);
    m_actionManager->setAction(PDFActionManager::ZoomOut, ui->actionZoom_Out);
    m_actionManager->setAction(PDFActionManager::Find, ui->actionFind);
    m_actionManager->setAction(PDFActionManager::FindPrevious, ui->actionFindPrevious);
    m_actionManager->setAction(PDFActionManager::FindNext, ui->actionFindNext);
    m_actionManager->setAction(PDFActionManager::SelectTextAll, ui->actionSelectTextAll);
    m_actionManager->setAction(PDFActionManager::DeselectText, ui->actionDeselectText);
    m_actionManager->setAction(PDFActionManager::CopyText, ui->actionCopyText);
    m_actionManager->setAction(PDFActionManager::RotateRight, ui->actionRotateRight);
    m_actionManager->setAction(PDFActionManager::RotateLeft, ui->actionRotateLeft);
    m_actionManager->setAction(PDFActionManager::Print, ui->actionPrint);
    m_actionManager->setAction(PDFActionManager::Undo, ui->actionUndo);
    m_actionManager->setAction(PDFActionManager::Redo, ui->actionRedo);
    m_actionManager->setAction(PDFActionManager::Save, ui->actionSave);
    m_actionManager->setAction(PDFActionManager::SaveAs, ui->actionSave_As);
    m_actionManager->setAction(PDFActionManager::GoToDocumentStart, ui->actionGoToDocumentStart);
    m_actionManager->setAction(PDFActionManager::GoToDocumentEnd, ui->actionGoToDocumentEnd);
    m_actionManager->setAction(PDFActionManager::GoToNextPage, ui->actionGoToNextPage);
    m_actionManager->setAction(PDFActionManager::GoToPreviousPage, ui->actionGoToPreviousPage);
    m_actionManager->setAction(PDFActionManager::GoToNextLine, ui->actionGoToNextLine);
    m_actionManager->setAction(PDFActionManager::GoToPreviousLine, ui->actionGoToPreviousLine);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteComment, ui->actionStickyNoteComment);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteHelp, ui->actionStickyNoteHelp);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteInsert, ui->actionStickyNoteInsert);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteKey, ui->actionStickyNoteKey);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNewParagraph, ui->actionStickyNoteNewParagraph);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNote, ui->actionStickyNoteNote);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteParagraph, ui->actionStickyNoteParagraph);
    m_actionManager->setAction(PDFActionManager::CreateTextHighlight, ui->actionCreateTextHighlight);
    m_actionManager->setAction(PDFActionManager::CreateTextUnderline, ui->actionCreateTextUnderline);
    m_actionManager->setAction(PDFActionManager::CreateTextStrikeout, ui->actionCreateTextStrikeout);
    m_actionManager->setAction(PDFActionManager::CreateTextSquiggly, ui->actionCreateTextSquiggly);
    m_actionManager->setAction(PDFActionManager::CreateHyperlink, ui->actionCreateHyperlink);
    m_actionManager->setAction(PDFActionManager::CreateInlineText, ui->actionInlineText);
    m_actionManager->setAction(PDFActionManager::CreateStraightLine, ui->actionCreateStraightLine);
    m_actionManager->setAction(PDFActionManager::CreatePolyline, ui->actionCreatePolyline);
    m_actionManager->setAction(PDFActionManager::CreatePolygon, ui->actionCreatePolygon);
    m_actionManager->setAction(PDFActionManager::CreateEllipse, ui->actionCreateEllipse);
    m_actionManager->setAction(PDFActionManager::CreateFreehandCurve, ui->actionCreateFreehandCurve);
    m_actionManager->setAction(PDFActionManager::RenderOptionAntialiasing, ui->actionRenderOptionAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionTextAntialiasing, ui->actionRenderOptionTextAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionSmoothPictures, ui->actionRenderOptionSmoothPictures);
    m_actionManager->setAction(PDFActionManager::RenderOptionIgnoreOptionalContentSettings, ui->actionRenderOptionIgnoreOptionalContentSettings);
    m_actionManager->setAction(PDFActionManager::RenderOptionDisplayAnnotations, ui->actionRenderOptionDisplayAnnotations);
    m_actionManager->setAction(PDFActionManager::RenderOptionInvertColors, ui->actionInvertColors);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextBlocks, ui->actionShow_Text_Blocks);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextLines, ui->actionShow_Text_Lines);
    m_actionManager->setAction(PDFActionManager::Properties, ui->actionProperties);
    m_actionManager->setAction(PDFActionManager::Options, ui->actionOptions);
    m_actionManager->setAction(PDFActionManager::GetSource, ui->actionGetSource);
    m_actionManager->setAction(PDFActionManager::About, ui->actionAbout);
    m_actionManager->setAction(PDFActionManager::SendByMail, ui->actionSend_by_E_Mail);
    m_actionManager->setAction(PDFActionManager::RenderToImages, ui->actionRender_to_Images);
    m_actionManager->setAction(PDFActionManager::Optimize, ui->actionOptimize);
    m_actionManager->setAction(PDFActionManager::Encryption, ui->actionEncryption);
    m_actionManager->setAction(PDFActionManager::FitPage, ui->actionFitPage);
    m_actionManager->setAction(PDFActionManager::FitWidth, ui->actionFitWidth);
    m_actionManager->setAction(PDFActionManager::FitHeight, ui->actionFitHeight);
    m_actionManager->setAction(PDFActionManager::ShowRenderingErrors, ui->actionRendering_Errors);
    m_actionManager->setAction(PDFActionManager::PageLayoutSinglePage, ui->actionPageLayoutSinglePage);
    m_actionManager->setAction(PDFActionManager::PageLayoutContinuous, ui->actionPageLayoutContinuous);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoPages, ui->actionPageLayoutTwoPages);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoColumns, ui->actionPageLayoutTwoColumns);
    m_actionManager->setAction(PDFActionManager::PageLayoutFirstPageOnRightSide, ui->actionFirstPageOnRightSide);
    m_actionManager->setAction(PDFActionManager::ToolSelectText, ui->actionSelectText);
    m_actionManager->setAction(PDFActionManager::ToolMagnifier, ui->actionMagnifier);
    m_actionManager->setAction(PDFActionManager::ToolScreenshot, ui->actionScreenshot);
    m_actionManager->setAction(PDFActionManager::ToolExtractImage, ui->actionExtractImage);
    m_actionManager->setAction(PDFActionManager::DeveloperCreateInstaller, ui->actionDeveloperCreateInstaller);
    m_actionManager->initActions(pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24)), true);

    for (QAction* action : m_programController->getRecentFileManager()->getActions())
    {
        ui->menuFile->insertAction(ui->actionQuit, action);
    }
    ui->menuFile->insertSeparator(ui->actionQuit);

    connect(ui->actionQuit, &QAction::triggered, this, &PDFViewerMainWindow::onActionQuitTriggered);

    m_pageNumberSpinBox = new QSpinBox(this);
    m_pageNumberLabel = new QLabel(this);
    m_pageNumberSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
    m_pageNumberSpinBox->setAlignment(Qt::AlignCenter);
    connect(m_pageNumberSpinBox, &QSpinBox::editingFinished, this, &PDFViewerMainWindow::onPageNumberSpinboxEditingFinished);

    for (QAction* action : m_actionManager->getActionGroup(PDFActionManager::CreateStampGroup)->actions())
    {
        ui->menuStamp->addAction(action);
    }

    // Page control
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionGoToDocumentStart);
    ui->mainToolBar->addAction(ui->actionGoToPreviousPage);
    ui->mainToolBar->addWidget(m_pageNumberSpinBox);
    ui->mainToolBar->addWidget(m_pageNumberLabel);
    ui->mainToolBar->addAction(ui->actionGoToNextPage);
    ui->mainToolBar->addAction(ui->actionGoToDocumentEnd);

    // Zoom
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionZoom_In);
    ui->mainToolBar->addAction(ui->actionZoom_Out);

    m_pageZoomSpinBox = new QDoubleSpinBox(this);
    m_pageZoomSpinBox->setMinimum(pdf::PDFDrawWidgetProxy::getMinZoom() * 100);
    m_pageZoomSpinBox->setMaximum(pdf::PDFDrawWidgetProxy::getMaxZoom() * 100);
    m_pageZoomSpinBox->setDecimals(2);
    m_pageZoomSpinBox->setSuffix(tr("%"));
    m_pageZoomSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
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
    ui->mainToolBar->addAction(ui->actionCreateTextHighlight);
    ui->mainToolBar->addAction(ui->actionCreateTextUnderline);
    ui->mainToolBar->addAction(ui->actionCreateTextStrikeout);
    ui->mainToolBar->addAction(ui->actionCreateTextSquiggly);
    ui->mainToolBar->addAction(ui->actionMagnifier);
    ui->mainToolBar->addAction(ui->actionScreenshot);
    ui->mainToolBar->addAction(ui->actionExtractImage);
    ui->mainToolBar->addSeparator();

    // Special tools
    QToolButton* insertStickyNoteButton = m_actionManager->createToolButtonForActionGroup(PDFActionManager::CreateStickyNoteGroup, ui->mainToolBar);
    ui->mainToolBar->addWidget(insertStickyNoteButton);
    ui->mainToolBar->addSeparator();

    m_programController->initialize(PDFProgramController::AllFeatures, this, this, m_actionManager, m_progress);
    setCentralWidget(m_programController->getPdfWidget());
    setFocusProxy(m_programController->getPdfWidget());

    m_sidebarWidget = new PDFSidebarWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), m_programController->getTextToSpeech(), m_programController->getCertificateStore(), m_programController->getSettings(), this);
    m_sidebarDockWidget = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDockWidget->setObjectName("SidebarDockWidget");
    m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDockWidget->setWidget(m_sidebarWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDockWidget);
    m_sidebarDockWidget->hide();
    connect(m_sidebarWidget, &PDFSidebarWidget::actionTriggered, m_programController, &PDFProgramController::onActionTriggered);

    m_advancedFindWidget = new PDFAdvancedFindWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), this);
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
    m_actionManager->addAdditionalAction(m_advancedFindDockWidget->toggleViewAction());

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");
    m_actionManager->addAdditionalAction(m_sidebarDockWidget->toggleViewAction());

    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFViewerMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFViewerMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFViewerMainWindow::onProgressFinished);

    m_programController->finishInitialization();
    updateDeveloperMenu();

    if (pdf::PDFToolManager* toolManager = m_programController->getToolManager())
    {
        connect(toolManager, &pdf::PDFToolManager::messageDisplayRequest, statusBar(), &QStatusBar::showMessage);
    }
}

PDFViewerMainWindow::~PDFViewerMainWindow()
{
    delete m_programController;
    m_programController = nullptr;

    delete m_actionManager;
    m_actionManager = nullptr;

    delete ui;
}

void PDFViewerMainWindow::onActionQuitTriggered()
{
    close();
}

void PDFViewerMainWindow::onPageNumberSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageNumberSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->goToPage(m_pageNumberSpinBox->value() - 1);
}

void PDFViewerMainWindow::onPageZoomSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageZoomSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->zoom(m_pageZoomSpinBox->value() / 100.0);
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

    m_programController->setIsBusy(true);
    m_programController->updateActionsAvailability();
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

    m_programController->setIsBusy(false);
    m_programController->updateActionsAvailability();
}

void PDFViewerMainWindow::updateDeveloperMenu()
{
    bool isDeveloperMode = m_programController->getSettings()->getSettings().m_allowDeveloperMode;
    ui->menuDeveloper->menuAction()->setVisible(isDeveloperMode);
}

void PDFViewerMainWindow::updateUI(bool fullUpdate)
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingUI, true);

    if (fullUpdate)
    {
        if (pdf::PDFDocument* document = m_programController->getDocument())
        {
            size_t pageCount = document->getCatalog()->getPageCount();
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
    }
    else
    {
        std::vector<pdf::PDFInteger> currentPages = m_programController->getPdfWidget()->getDrawWidget()->getCurrentPages();
        if (!currentPages.empty())
        {
            m_pageNumberSpinBox->setValue(currentPages.front() + 1);

            // Prefetch pages, if it is enabled
            if (m_programController->getSettings()->isPagePrefetchingEnabled())
            {
                m_programController->getPdfWidget()->getDrawWidgetProxy()->prefetchPages(currentPages.back());
            }
        }

        m_sidebarWidget->setCurrentPages(currentPages);
    }

    m_pageZoomSpinBox->setValue(m_programController->getPdfWidget()->getDrawWidgetProxy()->getZoom() * 100);
}

QMenu* PDFViewerMainWindow::addToolMenu(QString name)
{
    return ui->menuTools->addMenu(name);
}

void PDFViewerMainWindow::setStatusBarMessage(QString message, int time)
{
    statusBar()->showMessage(message, time);
}

void PDFViewerMainWindow::setDocument(const pdf::PDFModifiedDocument& document)
{
    if (m_sidebarWidget)
    {
        m_sidebarWidget->setDocument(document, *m_programController->getSignatures());
    }

    if (m_advancedFindWidget)
    {
        m_advancedFindWidget->setDocument(document);
    }

    if (m_sidebarWidget)
    {
        if (m_sidebarWidget->isEmpty())
        {
            m_sidebarDockWidget->hide();
        }
        else
        {
            m_sidebarDockWidget->show();
        }
    }

    if (!document && m_advancedFindDockWidget)
    {
        m_advancedFindDockWidget->hide();
    }
}

void PDFViewerMainWindow::adjustToolbar(QToolBar* toolbar)
{
    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    toolbar->setIconSize(iconSize);
}

pdf::PDFTextSelection PDFViewerMainWindow::getSelectedText() const
{
    if (!m_advancedFindWidget)
    {
        return pdf::PDFTextSelection();
    }

    return m_advancedFindWidget->getSelectedText();
}

void PDFViewerMainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_programController->canClose())
    {
        // Jakub Melka: Do not allow to close the application, if document
        // reading is running.
        event->ignore();
    }
    else
    {
        m_programController->writeSettings();
        m_programController->closeDocument();
        event->accept();
    }
}

void PDFViewerMainWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    m_taskbarButton->setWindow(windowHandle());
}

void PDFViewerMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFViewerMainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFViewerMainWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1)
        {
            m_programController->openDocument(urls.front().toLocalFile());
            event->acceptProposedAction();
        }
    }
}

}   // namespace pdfviewer
