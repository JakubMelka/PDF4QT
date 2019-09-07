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

#include "pdfviewersettingsdialog.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"
#include "pdfstreamfilters.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfrenderingerrorswidget.h"
#include "pdffont.h"
#include "pdfitemmodels.h"

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

namespace pdfviewer
{

PDFViewerMainWindow::PDFViewerMainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::PDFViewerMainWindow),
    m_settings(new PDFViewerSettings(this)),
    m_pdfWidget(nullptr),
    m_optionalContentDockWidget(nullptr),
    m_optionalContentTreeView(nullptr),
    m_optionalContentTreeModel(nullptr),
    m_optionalContentActivity(nullptr)
{
    ui->setupUi(this);

    // Initialize shortcuts
    ui->actionOpen->setShortcut(QKeySequence::Open);
    ui->actionClose->setShortcut(QKeySequence::Close);
    ui->actionQuit->setShortcut(QKeySequence::Quit);

    connect(ui->actionOpen, &QAction::triggered, this, &PDFViewerMainWindow::onActionOpenTriggered);
    connect(ui->actionClose, &QAction::triggered, this, &PDFViewerMainWindow::onActionCloseTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &PDFViewerMainWindow::onActionQuitTriggered);

    auto createGoToAction = [this](QMenu* menu, QString text, QKeySequence::StandardKey key, pdf::PDFDrawWidgetProxy::Operation operation)
    {
        QAction* action = new QAction(text, this);
        action->setShortcut(key);
        menu->addAction(action);

        auto onTriggered = [this, operation]()
        {
            m_pdfWidget->getDrawWidgetProxy()->performOperation(operation);
        };
        connect(action, &QAction::triggered, this, onTriggered);
    };

    createGoToAction(ui->menuGoTo, tr("Go to document start"), QKeySequence::MoveToStartOfDocument, pdf::PDFDrawWidgetProxy::NavigateDocumentStart);
    createGoToAction(ui->menuGoTo, tr("Go to document end"), QKeySequence::MoveToEndOfDocument, pdf::PDFDrawWidgetProxy::NavigateDocumentEnd);
    createGoToAction(ui->menuGoTo, tr("Go to next page"), QKeySequence::MoveToNextPage, pdf::PDFDrawWidgetProxy::NavigateNextPage);
    createGoToAction(ui->menuGoTo, tr("Go to previous page"), QKeySequence::MoveToPreviousPage, pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
    createGoToAction(ui->menuGoTo, tr("Go to next line"), QKeySequence::MoveToNextLine, pdf::PDFDrawWidgetProxy::NavigateNextStep);
    createGoToAction(ui->menuGoTo, tr("Go to previous line"), QKeySequence::MoveToPreviousLine, pdf::PDFDrawWidgetProxy::NavigatePreviousStep);

    m_pdfWidget = new pdf::PDFWidget(this);
    setCentralWidget(m_pdfWidget);
    setFocusProxy(m_pdfWidget);

    m_optionalContentDockWidget = new QDockWidget(tr("Optional Content"), this);
    m_optionalContentDockWidget->setObjectName("OptionalContentDockWidget");
    m_optionalContentDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_optionalContentTreeView = new QTreeView(m_optionalContentDockWidget);
    m_optionalContentTreeView->header()->hide();
    m_optionalContentTreeModel = new pdf::PDFOptionalContentTreeItemModel(m_optionalContentTreeView);
    m_optionalContentTreeView->setModel(m_optionalContentTreeModel);
    m_optionalContentDockWidget->setWidget(m_optionalContentTreeView);
    addDockWidget(Qt::LeftDockWidgetArea, m_optionalContentDockWidget);
    m_optionalContentDockWidget->hide();

    ui->actionRenderOptionAntialiasing->setData(pdf::PDFRenderer::Antialiasing);
    ui->actionRenderOptionTextAntialiasing->setData(pdf::PDFRenderer::TextAntialiasing);
    ui->actionRenderOptionSmoothPictures->setData(pdf::PDFRenderer::SmoothImages);
    ui->actionRenderOptionIgnoreOptionalContentSettings->setData(pdf::PDFRenderer::IgnoreOptionalContent);

    for (QAction* action : getRenderingOptionActions())
    {
        connect(action, &QAction::triggered, this, &PDFViewerMainWindow::onRenderingOptionTriggered);
    }

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_optionalContentDockWidget->toggleViewAction());

    connect(m_pdfWidget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFViewerMainWindow::updatePageLayoutActions);
    connect(m_pdfWidget, &pdf::PDFWidget::pageRenderingErrorsChanged, this, &PDFViewerMainWindow::onPageRenderingErrorsChanged, Qt::QueuedConnection);
    connect(m_settings, &PDFViewerSettings::settingsChanged, this, &PDFViewerMainWindow::onViewerSettingsChanged);

    readSettings();
    updatePageLayoutActions();
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

    m_settings->readSettings(settings);
}

void PDFViewerMainWindow::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    m_settings->writeSettings(settings);
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

void PDFViewerMainWindow::onViewerSettingsChanged()
{
    m_pdfWidget->getDrawWidgetProxy()->setFeatures(m_settings->getFeatures());
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

    // Password callback
    auto getPasswordCallback = [this](bool* ok) -> QString
    {
        return QInputDialog::getText(this, tr("Encrypted document"), tr("Enter password to acces document content"), QLineEdit::Password, QString(), ok);
    };

    // Try to open a new document
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pdf::PDFDocumentReader reader(qMove(getPasswordCallback));
    pdf::PDFDocument document = reader.readFromFile(fileName);
    QApplication::restoreOverrideCursor();

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            // Mark current directory as this
            QFileInfo fileInfo(fileName);
            m_settings->setDirectory(fileInfo.dir().absolutePath());
            m_currentFile = fileInfo.fileName();

            m_pdfDocument.reset(new pdf::PDFDocument(std::move(document)));
            setDocument(m_pdfDocument.data());

            statusBar()->showMessage(tr("Document '%1' was successfully loaded!").arg(fileName), 4000);
            break;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            QMessageBox::critical(this, tr("PDF Viewer"), tr("Document read error: %1").arg(reader.getErrorMessage()));
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
    m_optionalContentTreeModel->setDocument(document);
    m_optionalContentTreeModel->setActivity(m_optionalContentActivity);
    m_optionalContentTreeView->expandAll();

    if (m_optionalContentTreeModel->isEmpty())
    {
        m_optionalContentDockWidget->hide();
    }
    else
    {
        m_optionalContentDockWidget->show();
    }

    updateTitle();
}

void PDFViewerMainWindow::closeDocument()
{
    setDocument(nullptr);
    m_pdfDocument.reset();
}

void PDFViewerMainWindow::setPageLayout(pdf::PageLayout pageLayout)
{
    m_pdfWidget->getDrawWidgetProxy()->setPageLayout(pageLayout);
}

std::vector<QAction*> PDFViewerMainWindow::getRenderingOptionActions() const
{
    return { ui->actionRenderOptionAntialiasing, ui->actionRenderOptionTextAntialiasing, ui->actionRenderOptionSmoothPictures, ui->actionRenderOptionIgnoreOptionalContentSettings };
}

void PDFViewerMainWindow::closeEvent(QCloseEvent* event)
{
    writeSettings();
    closeDocument();
    event->accept();
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

void PDFViewerMainWindow::on_actionRendering_Errors_triggered()
{
    pdf::PDFRenderingErrorsWidget renderingErrorsDialog(this, m_pdfWidget);
    renderingErrorsDialog.exec();
}

void PDFViewerMainWindow::on_actionOptions_triggered()
{
    PDFViewerSettingsDialog dialog(m_settings->getSettings(), this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_settings->setSettings(dialog.getSettings());
    }
}

}   // namespace pdfviewer
