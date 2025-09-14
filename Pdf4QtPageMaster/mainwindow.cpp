// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"
#include "assembleoutputsettingsdialog.h"
#include "selectoutlinetoregroupdialog.h"

#include "pdfaction.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QToolBar>
#include <QDesktopServices>
#include <QImageReader>
#include <QPixmapCache>
#include <QScreen>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSettings>
#include <QMimeData>

namespace pdfpagemaster
{

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_model(new PageItemModel(this)),
    m_delegate(new PageItemDelegate(m_model, this)),
    m_dropAction(Qt::IgnoreAction)
{
    ui->setupUi(this);

    m_delegate->setPageImageSize(getDefaultPageImageSize());

    ui->documentItemsView->setModel(m_model);
    ui->documentItemsView->setItemDelegate(m_delegate);
    connect(ui->documentItemsView, &QListView::customContextMenuRequested, this, &MainWindow::onWorkspaceCustomContextMenuRequested);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));

    ui->actionClear->setData(int(Operation::Clear));
    ui->actionCloneSelection->setData(int(Operation::CloneSelection));
    ui->actionRemoveSelection->setData(int(Operation::RemoveSelection));
    ui->actionReplaceSelection->setData(int(Operation::ReplaceSelection));
    ui->actionRestoreRemovedItems->setData(int(Operation::RestoreRemovedItems));
    ui->actionUndo->setData(int(Operation::Undo));
    ui->actionRedo->setData(int(Operation::Redo));
    ui->actionCut->setData(int(Operation::Cut));
    ui->actionCopy->setData(int(Operation::Copy));
    ui->actionPaste->setData(int(Operation::Paste));
    ui->actionRotate_Left->setData(int(Operation::RotateLeft));
    ui->actionRotate_Right->setData(int(Operation::RotateRight));
    ui->actionGroup->setData(int(Operation::Group));
    ui->actionUngroup->setData(int(Operation::Ungroup));
    ui->actionSelect_None->setData(int(Operation::SelectNone));
    ui->actionSelect_All->setData(int(Operation::SelectAll));
    ui->actionSelect_Even->setData(int(Operation::SelectEven));
    ui->actionSelect_Odd->setData(int(Operation::SelectOdd));
    ui->actionSelect_Portrait->setData(int(Operation::SelectPortrait));
    ui->actionSelect_Landscape->setData(int(Operation::SelectLandscape));
    ui->actionZoom_In->setData(int(Operation::ZoomIn));
    ui->actionZoom_Out->setData(int(Operation::ZoomOut));
    ui->actionUnited_Document->setData(int(Operation::Unite));
    ui->actionSeparate_to_Multiple_Documents->setData(int(Operation::Separate));
    ui->actionSeparate_to_Multiple_Documents_Grouped->setData(int(Operation::SeparateGrouped));
    ui->actionInsert_Image->setData(int(Operation::InsertImage));
    ui->actionInsert_Empty_Page->setData(int(Operation::InsertEmptyPage));
    ui->actionInsert_PDF->setData(int(Operation::InsertPDF));
    ui->actionGet_Source->setData(int(Operation::GetSource));
    ui->actionBecomeASponsor->setData(int(Operation::BecomeSponsor));
    ui->actionAbout->setData(int(Operation::About));
    ui->actionInvert_Selection->setData(int(Operation::InvertSelection));
    ui->actionRegroup_Even_Odd->setData(int(Operation::RegroupEvenOdd));
    ui->actionRegroup_Reverse->setData(int(Operation::RegroupReversed));
    ui->actionRegroup_by_Page_Pairs->setData(int(Operation::RegroupPaired));
    ui->actionRegroup_by_Outline->setData(int(Operation::RegroupOutline));
    ui->actionRegroup_by_Alternating_Pages->setData(int(Operation::RegroupAlternatingPages));
    ui->actionRegroup_by_Alternating_Pages_Reversed_Order->setData(int(Operation::RegroupAlternatingPagesReversed));
    ui->actionPrepare_Icon_Theme->setData(int(Operation::PrepareIconTheme));
    ui->actionShow_Document_Title_in_Items->setData(int(Operation::ShowDocumentTitle));

    m_iconTheme.registerAction(ui->actionAddDocuments, ":/pdfpagemaster/resources/open.svg");
    m_iconTheme.registerAction(ui->actionClose, ":/pdfpagemaster/resources/close.svg");
    m_iconTheme.registerAction(ui->actionCloneSelection, ":/pdfpagemaster/resources/clone-selection.svg");
    m_iconTheme.registerAction(ui->actionRemoveSelection, ":/pdfpagemaster/resources/remove-selection.svg");
    m_iconTheme.registerAction(ui->actionRestoreRemovedItems, ":/pdfpagemaster/resources/restore-removed-items.svg");
    m_iconTheme.registerAction(ui->actionInsert_PDF, ":/pdfpagemaster/resources/insert-page-from-pdf.svg");
    m_iconTheme.registerAction(ui->actionInsert_Image, ":/pdfpagemaster/resources/insert-image.svg");
    m_iconTheme.registerAction(ui->actionInsert_Empty_Page, ":/pdfpagemaster/resources/insert-empty-page.svg");
    m_iconTheme.registerAction(ui->actionCut, ":/pdfpagemaster/resources/cut.svg");
    m_iconTheme.registerAction(ui->actionCopy, ":/pdfpagemaster/resources/copy.svg");
    m_iconTheme.registerAction(ui->actionPaste, ":/pdfpagemaster/resources/paste.svg");
    m_iconTheme.registerAction(ui->actionReplaceSelection, ":/pdfpagemaster/resources/replace-selection.svg");
    m_iconTheme.registerAction(ui->actionSelect_None, ":/pdfpagemaster/resources/select-none.svg");
    m_iconTheme.registerAction(ui->actionSelect_All, ":/pdfpagemaster/resources/select-all.svg");
    m_iconTheme.registerAction(ui->actionSelect_Even, ":/pdfpagemaster/resources/select-even.svg");
    m_iconTheme.registerAction(ui->actionSelect_Odd, ":/pdfpagemaster/resources/select-odd.svg");
    m_iconTheme.registerAction(ui->actionSelect_Portrait, ":/pdfpagemaster/resources/select-portrait.svg");
    m_iconTheme.registerAction(ui->actionSelect_Landscape, ":/pdfpagemaster/resources/select-landscape.svg");
    m_iconTheme.registerAction(ui->actionRotate_Right, ":/pdfpagemaster/resources/rotate-right.svg");
    m_iconTheme.registerAction(ui->actionRotate_Left, ":/pdfpagemaster/resources/rotate-left.svg");
    m_iconTheme.registerAction(ui->actionZoom_In, ":/pdfpagemaster/resources/zoom-in.svg");
    m_iconTheme.registerAction(ui->actionZoom_Out, ":/pdfpagemaster/resources/zoom-out.svg");
    m_iconTheme.registerAction(ui->actionGet_Source, ":/pdfpagemaster/resources/get-source.svg");
    m_iconTheme.registerAction(ui->actionBecomeASponsor, ":/pdfpagemaster/resources/wallet.svg");
    m_iconTheme.registerAction(ui->actionAbout, ":/pdfpagemaster/resources/about.svg");
    m_iconTheme.registerAction(ui->actionUnited_Document, ":/pdfpagemaster/resources/make-united-document.svg");
    m_iconTheme.registerAction(ui->actionSeparate_to_Multiple_Documents, ":/pdfpagemaster/resources/make-separated-document.svg");
    m_iconTheme.registerAction(ui->actionSeparate_to_Multiple_Documents_Grouped, ":/pdfpagemaster/resources/make-separated-document-from-groups.svg");
    m_iconTheme.registerAction(ui->actionGroup, ":/pdfpagemaster/resources/group.svg");
    m_iconTheme.registerAction(ui->actionUngroup, ":/pdfpagemaster/resources/ungroup.svg");
    m_iconTheme.registerAction(ui->actionClear, ":/pdfpagemaster/resources/clear.svg");
    m_iconTheme.registerAction(ui->actionRegroup_Even_Odd, ":/pdfpagemaster/resources/regroup-even-odd.svg");
    m_iconTheme.registerAction(ui->actionRegroup_by_Page_Pairs, ":/pdfpagemaster/resources/regroup-pairs.svg");
    m_iconTheme.registerAction(ui->actionRegroup_by_Outline, ":/pdfpagemaster/resources/regroup-outline.svg");
    m_iconTheme.registerAction(ui->actionRegroup_by_Alternating_Pages, ":/pdfpagemaster/resources/regroup-alternating.svg");
    m_iconTheme.registerAction(ui->actionRegroup_by_Alternating_Pages_Reversed_Order, ":/pdfpagemaster/resources/regroup-alternating-reversed.svg");
    m_iconTheme.registerAction(ui->actionInvert_Selection, ":/pdfpagemaster/resources/invert-selection.svg");
    m_iconTheme.registerAction(ui->actionUndo, ":/pdfpagemaster/resources/undo.svg");
    m_iconTheme.registerAction(ui->actionRedo, ":/pdfpagemaster/resources/redo.svg");

    m_iconTheme.setDirectory("pdfpagemaster_theme");
    m_iconTheme.setPrefix(":/pdfpagemaster/resources/");
    m_iconTheme.loadTheme();

    QToolBar* mainToolbar = addToolBar(tr("&Main"));
    mainToolbar->setObjectName("main_toolbar");
    mainToolbar->addAction(ui->actionAddDocuments);
    mainToolbar->addSeparator();
    mainToolbar->addActions({ ui->actionCloneSelection, ui->actionRemoveSelection });
    mainToolbar->addSeparator();
    mainToolbar->addActions({ ui->actionUndo, ui->actionRedo });
    mainToolbar->addSeparator();
    mainToolbar->addActions({ ui->actionCut, ui->actionCopy, ui->actionPaste });
    mainToolbar->addSeparator();
    mainToolbar->addActions({ ui->actionGroup, ui->actionUngroup });
    QToolBar* insertToolbar = addToolBar(tr("&Insert"));
    insertToolbar->setObjectName("insert_toolbar");
    insertToolbar->addActions({ ui->actionInsert_PDF, ui->actionInsert_Image, ui->actionInsert_Empty_Page });
    QToolBar* selectToolbar = addToolBar(tr("&Select"));
    selectToolbar->setObjectName("select_toolbar");
    selectToolbar->addActions({ ui->actionSelect_None, ui->actionSelect_All, ui->actionSelect_Even, ui->actionSelect_Odd, ui->actionSelect_Portrait, ui->actionSelect_Landscape, ui->actionInvert_Selection });
    QToolBar* regroupToolbar = addToolBar(tr("&Regroup"));
    regroupToolbar->setObjectName("regroup_toolbar");
    regroupToolbar->addActions({ ui->actionRegroup_Even_Odd, ui->actionRegroup_by_Page_Pairs, ui->actionRegroup_by_Outline, ui->actionRegroup_by_Alternating_Pages, ui->actionRegroup_by_Alternating_Pages_Reversed_Order, ui->actionRegroup_Reverse });
    QToolBar* zoomToolbar = addToolBar(tr("&Zoom"));
    zoomToolbar->setObjectName("zoom_toolbar");
    zoomToolbar->addActions({ ui->actionZoom_In, ui->actionZoom_Out });
    QToolBar* makeToolbar = addToolBar(tr("Ma&ke"));
    makeToolbar->setObjectName("make_toolbar");
    makeToolbar->addActions({ ui->actionUnited_Document, ui->actionSeparate_to_Multiple_Documents, ui->actionSeparate_to_Multiple_Documents_Grouped });

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    auto toolbars = findChildren<QToolBar*>();
    for (QToolBar* toolbar : toolbars)
    {
        toolbar->setIconSize(iconSize);
        ui->menuToolbars->addAction(toolbar->toggleViewAction());
    }

    connect(&m_mapper, &QSignalMapper::mappedInt, this, &MainWindow::onMappedActionTriggered);
    connect(ui->documentItemsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);

    QList<QAction*> actions = findChildren<QAction*>();
    for (QAction* action : actions)
    {
        QVariant actionData = action->data();
        if (actionData.isValid())
        {
            connect(action, &QAction::triggered, &m_mapper, QOverload<>::of(&QSignalMapper::map));
            m_mapper.setMapping(action, actionData.toInt());
        }
    }

    if (pdf::PDFWidgetUtils::isDarkTheme())
    {
        pdf::PDFWidgetUtils::convertActionsForDarkTheme(actions, iconSize, qGuiApp->devicePixelRatio());
    }

    // Initialize pixmap cache size
    const int depth = 4; // 4 bytes (ARGB)
    const int reserveSize = 2; // Caching of two screens
    QSize size = QGuiApplication::primaryScreen()->availableVirtualSize();
    int bytes = size.width() * size.height() * depth * reserveSize;
    int kBytes = bytes / 1024;
    QPixmapCache::setCacheLimit(kBytes);

    loadSettings();
    updateActions();

#ifndef NDEBUG
    pdf::PDFWidgetUtils::checkMenuAccessibility(this);
#endif
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

QSize MainWindow::getMinPageImageSize() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(40, 40));
}

QSize MainWindow::getDefaultPageImageSize() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(100, 100));
}

QSize MainWindow::getMaxPageImageSize() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(250, 250));
}

void MainWindow::resizeEvent(QResizeEvent* resizeEvent)
{
    QMainWindow::resizeEvent(resizeEvent);

    ui->documentItemsView->doItemsLayout();
}

void MainWindow::on_actionClose_triggered()
{
    close();
}

void MainWindow::on_actionAddDocuments_triggered()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Select PDF document(s)"), m_settings.directory, tr("PDF document (*.pdf)"));
    if (!fileNames.isEmpty())
    {
        for (const QString& fileName : fileNames)
        {
            if (!insertDocument(fileName, QModelIndex()))
            {
                break;
            }
        }
    }
}

void MainWindow::onMappedActionTriggered(int actionId)
{
    performOperation(static_cast<Operation>(actionId));
}

void MainWindow::onWorkspaceCustomContextMenuRequested(const QPoint& point)
{
    QMenu* contextMenu = new QMenu(this);
    contextMenu->addAction(ui->actionCut);
    contextMenu->addAction(ui->actionCopy);
    contextMenu->addAction(ui->actionPaste);
    contextMenu->addSeparator();
    contextMenu->addAction(ui->actionRotate_Left);
    contextMenu->addAction(ui->actionRotate_Right);
    QMenu* selectMenu = contextMenu->addMenu(tr("Select"));
    selectMenu->addAction(ui->actionSelect_All);
    selectMenu->addAction(ui->actionSelect_Even);
    selectMenu->addAction(ui->actionSelect_Odd);
    selectMenu->addAction(ui->actionSelect_Landscape);
    selectMenu->addAction(ui->actionSelect_Portrait);
    selectMenu->addAction(ui->actionSelect_None);
    QMenu* regroupMenu = contextMenu->addMenu(tr("Regroup"));
    regroupMenu->addAction(ui->actionRegroup_Even_Odd);
    regroupMenu->addAction(ui->actionRegroup_by_Alternating_Pages);
    regroupMenu->addAction(ui->actionRegroup_by_Alternating_Pages_Reversed_Order);
    regroupMenu->addAction(ui->actionRegroup_by_Page_Pairs);
    regroupMenu->addAction(ui->actionRegroup_by_Outline);
    regroupMenu->addAction(ui->actionRegroup_Reverse);
    contextMenu->addSeparator();
    contextMenu->addAction(ui->actionGroup);
    contextMenu->addAction(ui->actionUngroup);

    contextMenu->exec(ui->documentItemsView->viewport()->mapToGlobal(point));
}

void MainWindow::updateActions()
{
    QList<QAction*> actions = findChildren<QAction*>();
    for (QAction* action : actions)
    {
        QVariant actionData = action->data();
        if (actionData.isValid())
        {
            action->setEnabled(canPerformOperation(static_cast<Operation>(actionData.toInt())));
        }
    }
}

void MainWindow::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        QRect availableGeometry = QApplication::primaryScreen()->availableGeometry();
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
    settings.endGroup();

    settings.beginGroup("Settings");
    m_settings.directory = settings.value("directory").toString();
    settings.endGroup();
}

void MainWindow::saveSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();

    settings.beginGroup("Settings");
    settings.setValue("directory", m_settings.directory);
    settings.endGroup();
}

bool MainWindow::insertDocument(const QString& fileName, const QModelIndex& insertIndex)
{
    bool isDocumentInserted = true;

    QFileInfo fileInfo(fileName);

    auto queryPassword = [this, &fileInfo](bool* ok)
    {
        *ok = false;
        return QInputDialog::getText(this, tr("Encrypted document"), tr("Enter password to access document '%1'").arg(fileInfo.fileName()), QLineEdit::Password, QString(), ok);
    };

    // Mark current directory as this
    m_settings.directory = fileInfo.dir().absolutePath();

    // Try to open a new document
    pdf::PDFDocumentReader reader(nullptr, qMove(queryPassword), true, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);

    QString errorMessage = reader.getErrorMessage();
    pdf::PDFDocumentReader::Result result = reader.getReadingResult();
    if (result == pdf::PDFDocumentReader::Result::OK)
    {
        const pdf::PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) ||
            securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
        {
            m_model->insertDocument(fileName, qMove(document), insertIndex);
        }
        else
        {
            isDocumentInserted = false;
            QMessageBox::critical(this, tr("Error"), tr("Document security doesn't permit to organize pages."));
        }
    }
    else if (result == pdf::PDFDocumentReader::Result::Failed)
    {
        isDocumentInserted = false;
        QMessageBox::critical(this, tr("Error"), errorMessage);
    }
    else if (result == pdf::PDFDocumentReader::Result::Cancelled)
    {
        isDocumentInserted = false;
    }

    updateActions();
    return isDocumentInserted;
}

bool MainWindow::canPerformOperation(Operation operation) const
{
    QModelIndexList selection = ui->documentItemsView->selectionModel()->selection().indexes();
    const bool isSelected = !selection.isEmpty();
    const bool isMultiSelected = selection.size() > 1;
    const bool isModelEmpty = m_model->rowCount(QModelIndex()) == 0;

    switch (operation)
    {
        case Operation::Clear:
            return true;

        case Operation::CloneSelection:
        case Operation::RemoveSelection:
        case Operation::ReplaceSelection:
            return isSelected;

        case Operation::RestoreRemovedItems:
            return !m_model->isTrashBinEmpty();

        case Operation::Undo:
            return m_model->canUndo();

        case Operation::Redo:
            return m_model->canRedo();

        case Operation::Cut:
        case Operation::Copy:
            return isSelected;

        case Operation::Paste:
        {
            const QMimeData* mimeData = QApplication::clipboard()->mimeData();
            return mimeData && mimeData->hasFormat(PageItemModel::getMimeDataType());
        }

        case Operation::RotateLeft:
        case Operation::RotateRight:
            return isSelected;

        case Operation::Group:
            return isMultiSelected;
        case Operation::Ungroup:
            return m_model->isGrouped(selection);

        case Operation::SelectNone:
            return isSelected;

        case Operation::SelectAll:
        case Operation::SelectEven:
        case Operation::SelectOdd:
        case Operation::SelectPortrait:
        case Operation::SelectLandscape:
            return !isModelEmpty;

        case Operation::ZoomIn:
            return m_delegate->getPageImageSize() != getMaxPageImageSize();

        case Operation::ZoomOut:
            return m_delegate->getPageImageSize() != getMinPageImageSize();

        case Operation::Unite:
        case Operation::Separate:
        case Operation::SeparateGrouped:
            return !isModelEmpty;

        case Operation::InsertImage:
        case Operation::InsertEmptyPage:
        case Operation::InsertPDF:
        case Operation::GetSource:
        case Operation::BecomeSponsor:
        case Operation::About:
        case Operation::PrepareIconTheme:
        case Operation::ShowDocumentTitle:
            return true;

        case Operation::InvertSelection:
            return !isModelEmpty;

        case Operation::RegroupEvenOdd:
        {
            PageItemModel::SelectionInfo info = m_model->getSelectionInfo(selection);
            return info.isDocumentOnly();
        }

        case Operation::RegroupReversed:
            return !isModelEmpty && !selection.isEmpty();

        case Operation::RegroupPaired:
            return !isModelEmpty && !selection.isEmpty();

        case Operation::RegroupOutline:
        {
            PageItemModel::SelectionInfo info = m_model->getSelectionInfo(selection);
            return info.isSingleDocument();
        }

        case Operation::RegroupAlternatingPages:
        case Operation::RegroupAlternatingPagesReversed:
        {
            PageItemModel::SelectionInfo info = m_model->getSelectionInfo(selection);
            return info.isTwoDocuments();
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return false;
}

void MainWindow::performOperation(Operation operation)
{
    switch (operation)
    {
        case Operation::Clear:
        {
            m_model->clear();
            QPixmapCache::clear();
            break;
        }
        case Operation::CloneSelection:
        {
            m_model->cloneSelection(ui->documentItemsView->selectionModel()->selection().indexes());
            break;
        }

        case Operation::RemoveSelection:
        {
            m_model->removeSelection(ui->documentItemsView->selectionModel()->selection().indexes());
            break;
        }

        case Operation::RestoreRemovedItems:
        {
            QModelIndexList restoredItemIndices = m_model->restoreRemovedItems();
            QItemSelection itemSelection;
            for (const QModelIndex& index : restoredItemIndices)
            {
                itemSelection.select(index, index);
            }
            ui->documentItemsView->selectionModel()->select(itemSelection, QItemSelectionModel::ClearAndSelect);
            break;
        }

        case Operation::Undo:
        {
            m_model->undo();
            break;
        }

        case Operation::Redo:
        {
            m_model->redo();
            break;
        }

        case Operation::Cut:
        case Operation::Copy:
        {
            QModelIndexList indices = ui->documentItemsView->selectionModel()->selection().indexes();

            if (indices.isEmpty())
            {
                return;
            }

            if (QMimeData* mimeData = m_model->mimeData(indices))
            {
                QApplication::clipboard()->setMimeData(mimeData);
            }

            ui->documentItemsView->clearSelection();
            m_dropAction = (operation == Operation::Cut) ? Qt::MoveAction : Qt::CopyAction;
            break;
        }

        case Operation::Paste:
        {
            QModelIndexList indices = ui->documentItemsView->selectionModel()->selection().indexes();

            int insertRow = m_model->rowCount(QModelIndex()) - 1;
            if (!indices.isEmpty())
            {
                insertRow = indices.back().row();
            }

            QModelIndex insertIndex = m_model->index(insertRow, 0, QModelIndex());
            const QMimeData* mimeData = QApplication::clipboard()->mimeData();
            if (m_model->canDropMimeData(mimeData, m_dropAction, -1, -1, insertIndex))
            {
                m_model->dropMimeData(mimeData, m_dropAction, -1, -1, insertIndex);
            }
            break;
        }

        case Operation::Group:
            m_model->group(ui->documentItemsView->selectionModel()->selection().indexes());
            break;

        case Operation::Ungroup:
            m_model->ungroup(ui->documentItemsView->selectionModel()->selection().indexes());
            break;

        case Operation::SelectNone:
            ui->documentItemsView->clearSelection();
            break;

        case Operation::SelectAll:
            ui->documentItemsView->selectAll();
            break;

        case Operation::SelectEven:
            ui->documentItemsView->selectionModel()->select(m_model->getSelectionEven(), QItemSelectionModel::ClearAndSelect);
            break;

        case Operation::SelectOdd:
            ui->documentItemsView->selectionModel()->select(m_model->getSelectionOdd(), QItemSelectionModel::ClearAndSelect);
            break;

        case Operation::SelectPortrait:
            ui->documentItemsView->selectionModel()->select(m_model->getSelectionPortrait(), QItemSelectionModel::ClearAndSelect);
            break;

        case Operation::SelectLandscape:
            ui->documentItemsView->selectionModel()->select(m_model->getSelectionLandscape(), QItemSelectionModel::ClearAndSelect);
            break;

        case Operation::ZoomIn:
        {
            QSize pageImageSize = m_delegate->getPageImageSize();
            pageImageSize *= 1.2;
            pageImageSize = pageImageSize.boundedTo(getMaxPageImageSize());

            if (pageImageSize != m_delegate->getPageImageSize())
            {
                m_delegate->setPageImageSize(pageImageSize);
            }
            break;
        }

        case Operation::ZoomOut:
        {
            QSize pageImageSize = m_delegate->getPageImageSize();
            pageImageSize /= 1.2;
            pageImageSize = pageImageSize.expandedTo(getMinPageImageSize());

            if (pageImageSize != m_delegate->getPageImageSize())
            {
                m_delegate->setPageImageSize(pageImageSize);
            }
            break;
        }

        case Operation::RotateLeft:
            m_model->rotateLeft(ui->documentItemsView->selectionModel()->selection().indexes());
            break;

        case Operation::RotateRight:
            m_model->rotateRight(ui->documentItemsView->selectionModel()->selection().indexes());
            break;

        case Operation::GetSource:
            QDesktopServices::openUrl(QUrl("https://github.com/JakubMelka/PDF4QT"));
            break;

        case Operation::BecomeSponsor:
            QDesktopServices::openUrl(QUrl("https://github.com/sponsors/JakubMelka"));
            break;

        case Operation::InsertEmptyPage:
            m_model->insertEmptyPage(ui->documentItemsView->selectionModel()->selection().indexes());
            break;

        case Operation::About:
        {
            PDFAboutDialog aboutDialog(this);
            aboutDialog.exec();
            break;
        }

        case Operation::PrepareIconTheme:
            m_iconTheme.prepareTheme();
            break;

        case Operation::ShowDocumentTitle:
            m_model->setShowTitleInDescription(ui->actionShow_Document_Title_in_Items->isChecked());
            break;

        case Operation::Unite:
        case Operation::Separate:
        case Operation::SeparateGrouped:
        {
            PageItemModel::AssembleMode assembleMode = PageItemModel::AssembleMode::Unite;

            switch (operation)
            {
                case Operation::Unite:
                    assembleMode = PageItemModel::AssembleMode::Unite;
                    break;

                case Operation::Separate:
                    assembleMode = PageItemModel::AssembleMode::Separate;
                    break;

                case Operation::SeparateGrouped:
                    assembleMode = PageItemModel::AssembleMode::SeparateGrouped;
                    break;

                default:
                    Q_ASSERT(false);
            }

            std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledDocuments = m_model->getAssembledPages(assembleMode);

            // Check we have something to process
            if (assembledDocuments.empty())
            {
                QMessageBox::critical(this, tr("Error"), tr("No documents to assemble."));
                break;
            }

            AssembleOutputSettingsDialog dialog(m_settings.directory, this);
            if (dialog.exec() == QDialog::Accepted)
            {
                pdf::PDFDocumentManipulator manipulator;

                // Add documents and images
                for (const auto& documentItem : m_model->getDocuments())
                {
                    manipulator.addDocument(documentItem.first, &documentItem.second.document);
                }
                for (const auto& imageItem : m_model->getImages())
                {
                    manipulator.addImage(imageItem.first, imageItem.second.image);
                }

                // Jakub Melka: create assembled documents
                pdf::PDFOperationResult result(true);
                std::vector<std::pair<QString, pdf::PDFDocument>> assembledDocumentStorage;

                int sourceDocumentIndex = 1;
                int assembledDocumentIndex = 1;
                int sourcePageIndex = 1;
                int documentCount = int(m_model->getDocuments().size());

                QString directory = dialog.getDirectory();
                QString fileNameTemplate = dialog.getFileName();
                const bool isOverwriteEnabled = dialog.isOverwriteFiles();
                pdf::PDFDocumentManipulator::OutlineMode outlineMode = dialog.getOutlineMode();
                manipulator.setOutlineMode(outlineMode);

                if (!directory.endsWith('/'))
                {
                    directory += "/";
                }

                auto replaceInString = [](QString& templateString, QChar character, int number)
                {
                    int index = templateString.indexOf(character, 0, Qt::CaseSensitive);
                    if (index != -1)
                    {
                        int lastIndex = templateString.lastIndexOf(character, -1, Qt::CaseSensitive);
                        int count = lastIndex - index + 1;

                        QString textNumber = QString::number(number);
                        textNumber = textNumber.rightJustified(count, '0', false);

                        templateString.remove(index, count);
                        templateString.insert(index, textNumber);
                    }
                };

                for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
                {
                    pdf::PDFOperationResult currentResult = manipulator.assemble(assembledPages);
                    if (!currentResult && result)
                    {
                        result = currentResult;
                        break;
                    }

                    pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
                    sourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
                    sourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);

                    QString fileName = fileNameTemplate;

                    replaceInString(fileName, '#', assembledDocumentIndex);
                    replaceInString(fileName, '@', sourcePageIndex);
                    replaceInString(fileName, '%', sourceDocumentIndex);

                    if (!fileName.endsWith(".pdf"))
                    {
                        fileName += ".pdf";
                    }
                    fileName.prepend(directory);

                    assembledDocumentStorage.emplace_back(std::make_pair(std::move(fileName), manipulator.takeAssembledDocument()));
                    ++assembledDocumentIndex;
                }

                if (!result)
                {
                    QMessageBox::critical(this, tr("Error"), result.getErrorMessage());
                    return;
                }

                // Now, try to save files
                for (const auto& assembledDocumentItem : assembledDocumentStorage)
                {
                    QString filename = assembledDocumentItem.first;
                    const pdf::PDFDocument* document = &assembledDocumentItem.second;

                    const bool isDocumentFileAlreadyExisting = QFile::exists(filename);
                    if (!isOverwriteEnabled && isDocumentFileAlreadyExisting)
                    {
                        QMessageBox::critical(this, tr("Error"), tr("Document with given filename already exists."));
                        return;
                    }

                    pdf::PDFDocumentWriter writer(nullptr);
                    pdf::PDFOperationResult writeResult = writer.write(filename, document, isDocumentFileAlreadyExisting);

                    if (!writeResult)
                    {
                        QMessageBox::critical(this, tr("Error"), writeResult.getErrorMessage());
                        return;
                    }
                }
            }

            break;
        }

        case Operation::InsertImage:
        {
            QStringList filters;
            for (const QByteArray& imageFormat : QImageReader::supportedImageFormats())
            {
                filters << QString::fromLatin1(imageFormat).toLower();
            }
            QString filter = tr("Images (*.%1)").arg(filters.join(" *."));
            QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Select Image(s)"), m_settings.directory, filter, nullptr);

            if (!fileNames.isEmpty())
            {
                QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
                QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();

                for (const QString& fileName : fileNames)
                {
                    m_model->insertImage(fileName, insertIndex);
                    insertIndex = insertIndex.sibling(insertIndex.row() + 1, insertIndex.column());
                }
            }
            break;
        }

        case Operation::InsertPDF:
        {
            QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Select PDF document(s)"), m_settings.directory, tr("PDF document (*.pdf)"));

            if (!fileNames.isEmpty())
            {
                QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
                QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();

                for (const QString& fileName : fileNames)
                {
                    if (!insertDocument(fileName, insertIndex))
                    {
                        break;
                    }
                    insertIndex = insertIndex.sibling(insertIndex.row() + 1, insertIndex.column());
                }
            }
            break;
        }

        case Operation::ReplaceSelection:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();

            if (indexes.isEmpty())
            {
                // Jakub Melka: we have nothing to do, selection is empty
                return;
            }

            QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), m_settings.directory, tr("PDF document (*.pdf)"));
            if (!fileName.isEmpty())
            {
                insertDocument(fileName, indexes.back());
                m_model->removeSelection(indexes);
            }
            break;
        }

        case Operation::InvertSelection:
        {
            QItemSelection selection = ui->documentItemsView->selectionModel()->selection();
            ui->documentItemsView->selectAll();
            ui->documentItemsView->selectionModel()->select(selection, QItemSelectionModel::Deselect);
            break;
        }

        case Operation::RegroupReversed:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            m_model->regroupReversed(indexes);
            break;
        }

        case Operation::RegroupEvenOdd:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            m_model->regroupEvenOdd(indexes);
            break;
        }

        case Operation::RegroupPaired:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            m_model->regroupPaired(indexes);
            break;
        }

        case Operation::RegroupOutline:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();

            if (!indexes.isEmpty())
            {
                PageItemModel::SelectionInfo selectionInfo = m_model->getSelectionInfo(indexes);
                const std::map<int, DocumentItem>& documents = m_model->getDocuments();

                auto it = documents.find(selectionInfo.firstDocumentIndex);
                if (it != documents.end())
                {
                    const pdf::PDFDocument* document = &it->second.document;
                    SelectOutlineToRegroupDialog dialog(document, this);

                    if (dialog.exec() == SelectOutlineToRegroupDialog::Accepted)
                    {
                        std::vector<pdf::PDFInteger> breakPageIndices;
                        std::vector<const pdf::PDFOutlineItem*> outlineItems = dialog.getSelectedOutlineItems();

                        // Jakub Melka: Resolve outline items. Try to find an index
                        // of page of each outline item.
                        for (const pdf::PDFOutlineItem* item : outlineItems)
                        {
                            const pdf::PDFAction* action = item->getAction();
                            const pdf::PDFActionGoTo* actionGoto = dynamic_cast<const pdf::PDFActionGoTo*>(action);
                            if (actionGoto)
                            {
                                pdf::PDFDestination destination = actionGoto->getDestination();

                                if (destination.getDestinationType() == pdf::DestinationType::Named)
                                {
                                    if (const pdf::PDFDestination* targetDestination = document->getCatalog()->getNamedDestination(destination.getName()))
                                    {
                                        destination = *targetDestination;
                                    }
                                    else
                                    {
                                        destination = pdf::PDFDestination();
                                    }
                                }

                                if (destination.isValid())
                                {
                                    const size_t pageIndex = document->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                                    if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
                                    {
                                        breakPageIndices.push_back(pageIndex + 1);
                                    }
                                }
                            }
                        }

                        std::sort(breakPageIndices.begin(), breakPageIndices.end());
                        breakPageIndices.erase(std::unique(breakPageIndices.begin(), breakPageIndices.end()), breakPageIndices.end());

                        m_model->regroupOutline(indexes, breakPageIndices);
                    }
                }
            }

            break;
        }

        case Operation::RegroupAlternatingPages:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            m_model->regroupAlternatingPages(indexes, false);
            break;
        }

        case Operation::RegroupAlternatingPagesReversed:
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            m_model->regroupAlternatingPages(indexes, true);
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    updateActions();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasImage() || mimeData->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    event->ignore();

    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasImage())
    {
        QImage image = mimeData->imageData().value<QImage>();
        if (!image.isNull())
        {
            QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
            QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();
            m_model->insertImage(image, insertIndex);
            event->accept();
        }
    }
    else if (mimeData->hasUrls())
    {
        QModelIndexList indexes = ui->documentItemsView->selectionModel()->selection().indexes();
        QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();

        QList<QUrl> urls = mimeData->urls();
        std::reverse(urls.begin(), urls.end());
        QList<QByteArray> supportedImageFormats = QImageReader::supportedImageFormats();

        for (QUrl url : urls)
        {
            event->accept();

            if (url.isLocalFile())
            {
                QString fileName = url.toLocalFile();
                QFileInfo fileInfo(fileName);
                QString suffix = fileInfo.suffix().toLower();

                if (supportedImageFormats.contains(suffix.toUtf8()))
                {
                    if (m_model->insertImage(fileName, insertIndex) == -1)
                    {
                        // Exit the loop if the image was not inserted.
                        event->ignore();
                        break;
                    }
                }
                else if (suffix == "pdf")
                {
                    if (!insertDocument(url.toLocalFile(), insertIndex))
                    {
                        // Exit the loop if the document was not inserted. This could be because
                        // the document requires a password and the user might have provided an incorrect one,
                        // or the file couldn't be opened. We don't want to proceed with the next file.
                        event->ignore();
                        break;
                    }
                }
                else
                {
                    // We ignore other file extensions.
                }
            }
        }
    }
}

}   // namespace pdfpagemaster
