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
#include "itempropertiesdialog.h"
#include "pdfpageimportdialog.h"
#include "selectoutlinetoregroupdialog.h"
#include "pageitempreviewrenderer.h"
#include "pdfpagegeometrydialog.h"

#include "pdfaction.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfimageoptimizer.h"
#include "pdfoutline.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
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
#include <QTableView>
#include <QHeaderView>
#include <QAbstractScrollArea>
#include <QVBoxLayout>
#include <QDate>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>

#include <tuple>

namespace pdfpagemaster
{

namespace
{

enum class SplitMode
{
    EveryPage,
    EveryNPages,
    PageNumbers,
    TopLevelBookmarks,
    ApproximateSize
};

QString sanitizeOutputFileName(QString fileName)
{
    static const QRegularExpression invalidCharacters(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]"));
    static const QRegularExpression trailingDotsAndSpaces(QStringLiteral("[. ]+$"));
    static const QRegularExpression windowsReservedName(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"), QRegularExpression::CaseInsensitiveOption);

    fileName.replace(invalidCharacters, QStringLiteral("_"));
    while (fileName.contains(QStringLiteral("..")))
    {
        fileName.replace(QStringLiteral(".."), QStringLiteral("."));
    }

    fileName = fileName.trimmed();
    fileName.remove(trailingDotsAndSpaces);

    QFileInfo fileInfo(fileName);
    QString baseName = fileInfo.completeBaseName();
    const QString suffix = fileInfo.suffix();
    if (baseName.isEmpty())
    {
        baseName = QStringLiteral("document");
    }
    if (windowsReservedName.match(baseName).hasMatch())
    {
        baseName.append(QLatin1Char('_'));
    }

    return suffix.isEmpty() ? baseName : QStringLiteral("%1.%2").arg(baseName, suffix);
}

QString normalizedOutputPathKey(const QString& fileName)
{
    return QDir::cleanPath(fileName).toCaseFolded();
}

void replaceInString(QString& templateString, QChar character, int number)
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
}

QString createOutputFileName(QString fileNameTemplate,
                             const QString& directory,
                             int outputIndex,
                             int groupIndex,
                             int sourceDocumentIndex,
                             int sourcePageIndex,
                             const QString& sourceName,
                             const QString& sourceBase,
                             const QString& sourceExtension)
{
    replaceInString(fileNameTemplate, '#', outputIndex);
    replaceInString(fileNameTemplate, '@', sourcePageIndex);
    replaceInString(fileNameTemplate, '%', sourceDocumentIndex);

    fileNameTemplate.replace("{source_name}", sourceName);
    fileNameTemplate.replace("{source_base}", sourceBase);
    fileNameTemplate.replace("{source_ext}", sourceExtension);
    fileNameTemplate.replace("{source_page}", QString::number(sourcePageIndex));
    fileNameTemplate.replace("{output_index}", QString::number(outputIndex));
    fileNameTemplate.replace("{group_index}", QString::number(groupIndex));
    fileNameTemplate.replace("{date}", QDate::currentDate().toString(Qt::ISODate));

    if (!fileNameTemplate.endsWith(".pdf", Qt::CaseInsensitive))
    {
        fileNameTemplate += ".pdf";
    }

    fileNameTemplate = sanitizeOutputFileName(fileNameTemplate);
    QDir outputDirectory(directory);
    return outputDirectory.filePath(fileNameTemplate);
}

bool resolveOutlinePageIndex(const pdf::PDFDocument* document, const pdf::PDFOutlineItem* item, pdf::PDFInteger* pageIndex)
{
    if (!document || !item || !pageIndex)
    {
        return false;
    }

    const pdf::PDFAction* action = item->getAction();
    const pdf::PDFActionGoTo* actionGoto = dynamic_cast<const pdf::PDFActionGoTo*>(action);
    if (!actionGoto)
    {
        return false;
    }

    pdf::PDFDestination destination = actionGoto->getDestination();
    if (destination.getDestinationType() == pdf::DestinationType::Named)
    {
        if (const pdf::PDFDestination* targetDestination = document->getCatalog()->getNamedDestination(destination.getName()))
        {
            destination = *targetDestination;
        }
        else
        {
            return false;
        }
    }

    if (!destination.isValid())
    {
        return false;
    }

    const size_t resolvedPageIndex = document->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
    if (resolvedPageIndex == pdf::PDFCatalog::INVALID_PAGE_INDEX)
    {
        return false;
    }

    *pageIndex = pdf::PDFInteger(resolvedPageIndex + 1);
    return true;
}

std::vector<pdf::PDFInteger> getTopLevelBookmarkPageIndices(const pdf::PDFDocument* document)
{
    std::vector<pdf::PDFInteger> pageIndices;
    if (!document)
    {
        return pageIndices;
    }

    QSharedPointer<pdf::PDFOutlineItem> outlineRoot = document->getCatalog()->getOutlineRootPtr();
    if (!outlineRoot)
    {
        return pageIndices;
    }

    for (size_t i = 0; i < outlineRoot->getChildCount(); ++i)
    {
        pdf::PDFInteger pageIndex = 0;
        if (resolveOutlinePageIndex(document, outlineRoot->getChild(i), &pageIndex))
        {
            pageIndices.push_back(pageIndex);
        }
    }

    std::sort(pageIndices.begin(), pageIndices.end());
    pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());
    return pageIndices;
}

std::vector<int> parseSplitPagePositions(const QString& text, bool* ok)
{
    std::vector<int> positions;
    if (ok)
    {
        *ok = false;
    }

    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool isNumber = false;
        const int position = part.toInt(&isNumber);
        if (!isNumber || position < 1)
        {
            return {};
        }

        positions.push_back(position);
    }

    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    if (ok)
    {
        *ok = !positions.empty();
    }
    return positions;
}

} // namespace

class WorkspaceFilterProxyModel : public QSortFilterProxyModel
{
public:
    explicit WorkspaceFilterProxyModel(QObject* parent) :
        QSortFilterProxyModel(parent)
    {
        setDynamicSortFilter(true);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setFilterText(const QString& text)
    {
        const QString simplifiedText = text.simplified().toCaseFolded();
        if (m_filterText != simplifiedText)
        {
            m_filterText = simplifiedText;
            invalidateRowsFilter();
        }
    }

    QString filterText() const { return m_filterText; }
    bool isFiltering() const { return !m_filterText.isEmpty(); }

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (m_filterText.isEmpty())
        {
            return true;
        }

        const PageItemModel* pageModel = qobject_cast<const PageItemModel*>(sourceModel());
        if (!pageModel)
        {
            return true;
        }

        const QModelIndex sourceIndex = pageModel->index(sourceRow, 0, sourceParent);
        const PageGroupItem* item = pageModel->getItem(sourceIndex);
        if (!item)
        {
            return false;
        }

        QStringList terms;
        for (int column = 0; column < PageItemModel::ColumnCount; ++column)
        {
            terms << pageModel->data(pageModel->index(sourceRow, column, sourceParent), Qt::DisplayRole).toString();
        }

        bool hasPortrait = false;
        bool hasLandscape = false;
        terms << (item->isGrouped() ? QObject::tr("Grouped") : QObject::tr("Ungrouped"));
        for (const PageGroupItem::GroupItem& groupItem : item->groups)
        {
            if (groupItem.rotatedPageDimensionsMM.width() <= groupItem.rotatedPageDimensionsMM.height())
            {
                hasPortrait = true;
                terms << QObject::tr("Portrait");
            }
            if (groupItem.rotatedPageDimensionsMM.width() >= groupItem.rotatedPageDimensionsMM.height())
            {
                hasLandscape = true;
                terms << QObject::tr("Landscape");
            }
        }

        const QString haystack = terms.join(QLatin1Char(' ')).toCaseFolded();
        const QStringList needles = m_filterText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& needle : needles)
        {
            if (needle == QStringLiteral("grouped") || needle == QObject::tr("Grouped").toCaseFolded())
            {
                if (!item->isGrouped())
                {
                    return false;
                }
                continue;
            }
            if (needle == QStringLiteral("ungrouped") || needle == QObject::tr("Ungrouped").toCaseFolded())
            {
                if (item->isGrouped())
                {
                    return false;
                }
                continue;
            }
            if (needle == QStringLiteral("portrait") || needle == QObject::tr("Portrait").toCaseFolded())
            {
                if (!hasPortrait)
                {
                    return false;
                }
                continue;
            }
            if (needle == QStringLiteral("landscape") || needle == QObject::tr("Landscape").toCaseFolded())
            {
                if (!hasLandscape)
                {
                    return false;
                }
                continue;
            }
            if (!haystack.contains(needle))
            {
                return false;
            }
        }

        return true;
    }

private:
    QString m_filterText;
};

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_model(new PageItemModel(this)),
    m_previewRenderer(new PageItemPreviewRenderer(m_model, this)),
    m_delegate(new PageItemDelegate(m_model, m_previewRenderer, this)),
    m_filterModel(new WorkspaceFilterProxyModel(this)),
    m_detailsView(new QTableView(this)),
    m_sortByFileNameAction(nullptr),
    m_sortBySourceAction(nullptr),
    m_sortByPageNumberAction(nullptr),
    m_sortByTypeAction(nullptr),
    m_reverseOrderAction(nullptr),
    m_resetRotationAction(nullptr),
    m_renameGroupAction(nullptr),
    m_propertiesAction(nullptr),
    m_showDetailsViewAction(nullptr),
    m_insertPDFPagesAction(nullptr),
    m_splitAction(nullptr),
    m_selectPageRangeAction(nullptr),
    m_clearSearchAction(nullptr),
    m_selectVisibleAction(nullptr),
    m_searchEdit(new QLineEdit(this)),
    m_searchResultLabel(new QLabel(this)),
    m_dropAction(Qt::IgnoreAction)
{
    ui->setupUi(this);

    m_delegate->setPageImageSize(getDefaultPageImageSize());
    m_filterModel->setSourceModel(m_model);

    ui->documentItemsView->setModel(m_filterModel);
    ui->documentItemsView->setItemDelegate(m_delegate);
    m_previewRenderer->setView(ui->documentItemsView);
    connect(m_previewRenderer, &PageItemPreviewRenderer::previewUpdated, this, &MainWindow::onPreviewUpdated);
    connect(ui->documentItemsView, &QListView::customContextMenuRequested, this, &MainWindow::onWorkspaceCustomContextMenuRequested);

    m_detailsView->setModel(m_filterModel);
    m_detailsView->setSelectionModel(ui->documentItemsView->selectionModel());
    m_detailsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_detailsView->setDragEnabled(true);
    m_detailsView->setAcceptDrops(true);
    m_detailsView->setDragDropMode(QAbstractItemView::DragDrop);
    m_detailsView->setDragDropOverwriteMode(false);
    m_detailsView->setDefaultDropAction(Qt::MoveAction);
    m_detailsView->setSortingEnabled(false);
    m_detailsView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_detailsView->horizontalHeader()->setStretchLastSection(true);
    m_detailsView->verticalHeader()->hide();
    m_detailsView->hide();
    ui->verticalLayout->addWidget(m_detailsView);
    connect(m_detailsView, &QTableView::customContextMenuRequested, this, &MainWindow::onWorkspaceCustomContextMenuRequested);

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
    ui->actionPageGeometry->setData(int(Operation::ConfigurePageGeometry));
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

    m_sortByFileNameAction = new QAction(tr("Sort by File Name"), this);
    m_sortByFileNameAction->setData(int(Operation::SortByFileName));
    m_sortBySourceAction = new QAction(tr("Sort by Source"), this);
    m_sortBySourceAction->setData(int(Operation::SortBySource));
    m_sortByPageNumberAction = new QAction(tr("Sort by Page Number"), this);
    m_sortByPageNumberAction->setData(int(Operation::SortByPageNumber));
    m_sortByTypeAction = new QAction(tr("Sort by Type"), this);
    m_sortByTypeAction->setData(int(Operation::SortByType));
    m_reverseOrderAction = new QAction(tr("Reverse Order"), this);
    m_reverseOrderAction->setData(int(Operation::ReverseOrder));
    m_resetRotationAction = new QAction(tr("Reset Rotation"), this);
    m_resetRotationAction->setData(int(Operation::ResetRotation));
    m_renameGroupAction = new QAction(tr("Rename Item or Group..."), this);
    m_renameGroupAction->setData(int(Operation::RenameGroup));
    m_propertiesAction = new QAction(tr("Properties..."), this);
    m_propertiesAction->setData(int(Operation::Properties));
    m_showDetailsViewAction = new QAction(tr("Details View"), this);
    m_showDetailsViewAction->setCheckable(true);
    m_showDetailsViewAction->setData(int(Operation::ShowDetailsView));
    m_insertPDFPagesAction = new QAction(tr("Insert PDF Pages..."), this);
    m_insertPDFPagesAction->setData(int(Operation::InsertPDFPages));
    m_splitAction = new QAction(tr("Split..."), this);
    m_splitAction->setData(int(Operation::Split));
    m_selectPageRangeAction = new QAction(tr("Select Page Range..."), this);
    m_selectPageRangeAction->setData(int(Operation::SelectPageRange));
    m_clearSearchAction = new QAction(tr("Clear Search"), this);
    m_selectVisibleAction = new QAction(tr("Select Visible"), this);
    m_selectVisibleAction->setData(int(Operation::SelectVisible));

    QMenu* sortMenu = ui->menuEdit->addMenu(tr("Sort"));
    sortMenu->addActions({ m_sortByFileNameAction, m_sortBySourceAction, m_sortByPageNumberAction, m_sortByTypeAction });
    sortMenu->addSeparator();
    sortMenu->addAction(m_reverseOrderAction);
    ui->menuView->insertAction(ui->actionSelect_Even, m_selectPageRangeAction);
    ui->menuEdit->addAction(m_resetRotationAction);
    ui->menuEdit->addAction(m_renameGroupAction);
    ui->menuEdit->addAction(m_propertiesAction);
    ui->menuInsert->addAction(m_insertPDFPagesAction);
    ui->menuMake->addSeparator();
    ui->menuMake->addAction(m_splitAction);
    ui->menuView->addSeparator();
    ui->menuView->addAction(m_showDetailsViewAction);

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
    m_iconTheme.registerAction(m_selectPageRangeAction, ":/pdfpagemaster/resources/select-all.svg");
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
    m_iconTheme.registerAction(m_splitAction, ":/pdfpagemaster/resources/make-separated-document-from-groups.svg");
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
    mainToolbar->addSeparator();
    mainToolbar->addAction(ui->actionPageGeometry);
    mainToolbar->addAction(m_propertiesAction);
    QToolBar* insertToolbar = addToolBar(tr("&Insert"));
    insertToolbar->setObjectName("insert_toolbar");
    insertToolbar->addActions({ ui->actionInsert_PDF, m_insertPDFPagesAction, ui->actionInsert_Image, ui->actionInsert_Empty_Page });
    QToolBar* selectToolbar = addToolBar(tr("&Select"));
    selectToolbar->setObjectName("select_toolbar");
    selectToolbar->addActions({ ui->actionSelect_None, ui->actionSelect_All, m_selectPageRangeAction, ui->actionSelect_Even, ui->actionSelect_Odd, ui->actionSelect_Portrait, ui->actionSelect_Landscape, ui->actionInvert_Selection, m_selectVisibleAction });
    QToolBar* regroupToolbar = addToolBar(tr("&Regroup"));
    regroupToolbar->setObjectName("regroup_toolbar");
    regroupToolbar->addActions({ ui->actionRegroup_Even_Odd, ui->actionRegroup_by_Page_Pairs, ui->actionRegroup_by_Outline, ui->actionRegroup_by_Alternating_Pages, ui->actionRegroup_by_Alternating_Pages_Reversed_Order, ui->actionRegroup_Reverse });
    QToolBar* arrangeToolbar = addToolBar(tr("&Arrange"));
    arrangeToolbar->setObjectName("arrange_toolbar");
    arrangeToolbar->addActions({ m_sortByFileNameAction, m_sortBySourceAction, m_sortByPageNumberAction, m_reverseOrderAction, m_resetRotationAction });
    QToolBar* zoomToolbar = addToolBar(tr("&Zoom"));
    zoomToolbar->setObjectName("zoom_toolbar");
    zoomToolbar->addActions({ ui->actionZoom_In, ui->actionZoom_Out });
    QToolBar* makeToolbar = addToolBar(tr("Ma&ke"));
    makeToolbar->setObjectName("make_toolbar");
    makeToolbar->addActions({ ui->actionUnited_Document, ui->actionSeparate_to_Multiple_Documents, ui->actionSeparate_to_Multiple_Documents_Grouped, m_splitAction });
    QToolBar* searchToolbar = addToolBar(tr("&Search"));
    searchToolbar->setObjectName("search_toolbar");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(tr("Search workspace"));
    m_searchEdit->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 180));
    m_searchResultLabel->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 90));
    searchToolbar->addWidget(new QLabel(tr("Search"), this));
    searchToolbar->addWidget(m_searchEdit);
    searchToolbar->addAction(m_clearSearchAction);
    searchToolbar->addSeparator();
    searchToolbar->addWidget(m_searchResultLabel);

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    auto toolbars = findChildren<QToolBar*>();
    for (QToolBar* toolbar : toolbars)
    {
        toolbar->setIconSize(iconSize);
        ui->menuToolbars->addAction(toolbar->toggleViewAction());
    }

    connect(&m_mapper, &QSignalMapper::mappedInt, this, &MainWindow::onMappedActionTriggered);
    connect(ui->documentItemsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::updateSearchFilter);
    connect(m_clearSearchAction, &QAction::triggered, m_searchEdit, &QLineEdit::clear);
    connect(m_filterModel, &QAbstractItemModel::modelReset, this, &MainWindow::updateSearchResultLabel);
    connect(m_filterModel, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateSearchResultLabel);
    connect(m_filterModel, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateSearchResultLabel);
    connect(m_filterModel, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateSearchResultLabel);

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
    updateSearchFilter();
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

void MainWindow::onPreviewUpdated()
{
    if (ui->documentItemsView && ui->documentItemsView->viewport())
    {
        ui->documentItemsView->viewport()->update();
    }
}

void MainWindow::onWorkspaceCustomContextMenuRequested(const QPoint& point)
{
    QWidget* sourceWidget = qobject_cast<QWidget*>(sender());
    if (QAbstractScrollArea* scrollArea = qobject_cast<QAbstractScrollArea*>(sourceWidget))
    {
        sourceWidget = scrollArea->viewport();
    }
    QWidget* viewport = sourceWidget ? sourceWidget : ui->documentItemsView->viewport();

    QMenu* contextMenu = new QMenu(this);
    contextMenu->addAction(ui->actionCut);
    contextMenu->addAction(ui->actionCopy);
    contextMenu->addAction(ui->actionPaste);
    contextMenu->addSeparator();
    contextMenu->addAction(ui->actionRotate_Left);
    contextMenu->addAction(ui->actionRotate_Right);
    contextMenu->addAction(m_resetRotationAction);
    QMenu* selectMenu = contextMenu->addMenu(tr("Select"));
    selectMenu->addAction(ui->actionSelect_All);
    selectMenu->addAction(m_selectPageRangeAction);
    selectMenu->addAction(ui->actionSelect_Even);
    selectMenu->addAction(ui->actionSelect_Odd);
    selectMenu->addAction(ui->actionSelect_Landscape);
    selectMenu->addAction(ui->actionSelect_Portrait);
    selectMenu->addAction(m_selectVisibleAction);
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
    contextMenu->addAction(m_renameGroupAction);
    contextMenu->addSeparator();
    contextMenu->addAction(m_sortByFileNameAction);
    contextMenu->addAction(m_reverseOrderAction);
    contextMenu->addSeparator();
    contextMenu->addAction(m_splitAction);
    contextMenu->addSeparator();
    contextMenu->addAction(m_propertiesAction);

    contextMenu->exec(viewport->mapToGlobal(point));
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

QModelIndexList MainWindow::getSelectedRows() const
{
    QModelIndexList selection = ui->documentItemsView->selectionModel()->selectedRows();
    if (selection.isEmpty())
    {
        selection = ui->documentItemsView->selectionModel()->selection().indexes();
    }

    std::sort(selection.begin(), selection.end(), [](const QModelIndex& left, const QModelIndex& right)
    {
        return left.row() < right.row();
    });

    QModelIndexList rows;
    int previousRow = -1;
    for (const QModelIndex& index : selection)
    {
        const QModelIndex sourceIndex = m_filterModel->mapToSource(index);
        if (sourceIndex.isValid() && sourceIndex.row() != previousRow)
        {
            rows << m_model->index(sourceIndex.row(), 0, QModelIndex());
            previousRow = sourceIndex.row();
        }
    }
    return rows;
}

QModelIndexList MainWindow::getSelectedRowsOrAll() const
{
    QModelIndexList rows = getSelectedRows();
    if (!rows.isEmpty())
    {
        return rows;
    }

    rows.reserve(m_model->rowCount(QModelIndex()));
    for (int row = 0; row < m_model->rowCount(QModelIndex()); ++row)
    {
        rows << m_model->index(row, 0, QModelIndex());
    }
    return rows;
}

QModelIndexList MainWindow::getSelectedRowsForOrdering() const
{
    QModelIndexList selection = getSelectedRows();
    if (selection.size() < 2)
    {
        selection.clear();
    }
    return selection;
}

QModelIndexList MainWindow::getVisibleRows() const
{
    QModelIndexList rows;
    rows.reserve(m_filterModel->rowCount(QModelIndex()));
    for (int row = 0; row < m_filterModel->rowCount(QModelIndex()); ++row)
    {
        const QModelIndex sourceIndex = m_filterModel->mapToSource(m_filterModel->index(row, 0, QModelIndex()));
        if (sourceIndex.isValid())
        {
            rows << m_model->index(sourceIndex.row(), 0, QModelIndex());
        }
    }
    return rows;
}

void MainWindow::selectSourceSelection(const QItemSelection& selection, bool addToExisting)
{
    QItemSelection proxySelection;
    std::vector<int> rows;
    const QModelIndexList indexes = selection.indexes();
    rows.reserve(indexes.size());
    for (const QModelIndex& index : indexes)
    {
        if (index.isValid())
        {
            rows.push_back(index.row());
        }
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.empty())
    {
        if (!addToExisting)
        {
            ui->documentItemsView->clearSelection();
        }
        return;
    }

    for (int row : rows)
    {
        const QModelIndex proxyIndex = m_filterModel->mapFromSource(m_model->index(row, 0, QModelIndex()));
        if (proxyIndex.isValid())
        {
            proxySelection.select(proxyIndex, proxyIndex);
        }
    }

    const QItemSelectionModel::SelectionFlags flags = (addToExisting ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect) | QItemSelectionModel::Rows;
    ui->documentItemsView->selectionModel()->select(proxySelection, flags);
}

void MainWindow::setDetailsViewVisible(bool visible)
{
    ui->documentItemsView->setVisible(!visible);
    m_detailsView->setVisible(visible);
    if (visible)
    {
        m_detailsView->resizeColumnsToContents();
    }
    else
    {
        m_previewRenderer->setView(ui->documentItemsView);
        ui->documentItemsView->doItemsLayout();
    }
}

void MainWindow::showItemProperties()
{
    const QModelIndexList selection = getSelectedRows();
    if (selection.isEmpty())
    {
        return;
    }

    const PageGroupItem* item = m_model->getItem(selection.front());
    if (!item)
    {
        return;
    }

    ItemPropertiesDialog dialog(m_model, selection.front(), this);
    if (dialog.exec() == QDialog::Accepted && dialog.isImageDisplayNameEditable())
    {
        m_model->setImageDisplayName(dialog.getEditableImageIndex(), dialog.getImageDisplayName());
    }
}

void MainWindow::updateSearchFilter()
{
    m_filterModel->setFilterText(m_searchEdit->text());
    updateSearchResultLabel();
    updateActions();
}

void MainWindow::updateSearchResultLabel()
{
    const int visibleCount = m_filterModel->rowCount(QModelIndex());
    const int totalCount = m_model->rowCount(QModelIndex());
    if (m_filterModel->isFiltering())
    {
        m_searchResultLabel->setText(tr("%1 of %2").arg(visibleCount).arg(totalCount));
    }
    else
    {
        m_searchResultLabel->setText(tr("%1 items").arg(totalCount));
    }
    m_clearSearchAction->setEnabled(m_filterModel->isFiltering());
}

bool MainWindow::insertDocument(const QString& fileName, const QModelIndex& insertIndex)
{
    return insertDocument(fileName, insertIndex, {});
}

bool MainWindow::insertDocument(const QString& fileName, const QModelIndex& insertIndex, const std::vector<pdf::PDFInteger>& pages)
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
            if (m_model->insertDocument(fileName, qMove(document), insertIndex, pages) == -1)
            {
                isDocumentInserted = false;
                QMessageBox::critical(this, tr("Error"), tr("Document '%1' is already in the workspace.").arg(fileInfo.fileName()));
            }
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
    QModelIndexList selection = getSelectedRows();
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
        case Operation::ResetRotation:
            return isSelected;

        case Operation::Group:
            return isMultiSelected;
        case Operation::Ungroup:
            return m_model->isGrouped(selection);
        case Operation::RenameGroup:
        case Operation::Properties:
            return isSelected;

        case Operation::SelectNone:
            return isSelected;

        case Operation::SelectAll:
        case Operation::SelectEven:
        case Operation::SelectOdd:
        case Operation::SelectPortrait:
        case Operation::SelectLandscape:
        case Operation::SelectPageRange:
            return !isModelEmpty;

        case Operation::ZoomIn:
            return m_delegate->getPageImageSize() != getMaxPageImageSize();

        case Operation::ZoomOut:
            return m_delegate->getPageImageSize() != getMinPageImageSize();

        case Operation::Unite:
        case Operation::Separate:
        case Operation::SeparateGrouped:
        case Operation::Split:
            return !isModelEmpty;

        case Operation::InsertImage:
        case Operation::InsertEmptyPage:
        case Operation::InsertPDF:
        case Operation::InsertPDFPages:
        case Operation::ConfigurePageGeometry:
        case Operation::GetSource:
        case Operation::BecomeSponsor:
        case Operation::About:
        case Operation::PrepareIconTheme:
        case Operation::ShowDocumentTitle:
        case Operation::ShowDetailsView:
            return true;

        case Operation::SortByFileName:
        case Operation::SortBySource:
        case Operation::SortByPageNumber:
        case Operation::SortByType:
        case Operation::ReverseOrder:
            return !isModelEmpty;

        case Operation::SelectVisible:
            return m_filterModel->rowCount(QModelIndex()) > 0;

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

void MainWindow::exportAssembledDocuments(std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledDocuments, const QString& assembleModeText)
{
    if (assembledDocuments.empty())
    {
        QMessageBox::critical(this, tr("Error"), tr("No documents to assemble."));
        return;
    }

    auto sourceInfo = [this](const pdf::PDFDocumentManipulator::AssembledPage& page)
    {
        QFileInfo info;
        QString name;
        QString extension;
        if (page.documentIndex != -1)
        {
            auto it = m_model->getDocuments().find(page.documentIndex);
            if (it != m_model->getDocuments().cend())
            {
                info = QFileInfo(it->second.fileName);
                name = info.fileName();
                extension = info.suffix();
            }
        }
        else if (page.imageIndex != -1)
        {
            auto it = m_model->getImages().find(page.imageIndex);
            if (it != m_model->getImages().cend())
            {
                const ImageItem& image = it->second;
                info = QFileInfo(!image.fileName.isEmpty() ? image.fileName : image.displayName);
                name = !image.displayName.isEmpty() ? image.displayName : info.fileName();
                extension = !info.suffix().isEmpty() ? info.suffix() : image.format;
            }
        }

        return std::make_tuple(name, info.completeBaseName(), extension);
    };

    AssembleOutputSettingsDialog dialog(m_settings.directory, this);
    dialog.setOutputPreviewFactory([&]()
    {
        std::vector<AssembleOutputSettingsDialog::OutputPreviewItem> previewItems;
        previewItems.reserve(assembledDocuments.size());
        QSet<QString> generatedNames;
        int outputIndex = 1;
        const int documentCount = int(m_model->getDocuments().size());
        if (!QDir(dialog.getDirectory()).exists())
        {
            previewItems.push_back({ dialog.getDirectory(), QString(), QString(), assembleModeText, tr("Output directory does not exist"), true });
            return previewItems;
        }

        for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
        {
            const pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
            const int sourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
            const int sourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);
            auto [sourceName, sourceBase, sourceExtension] = sourceInfo(samplePage);
            const QString fileName = createOutputFileName(dialog.getFileName(), dialog.getDirectory(), outputIndex, outputIndex, sourceDocumentIndex, sourcePageIndex, sourceName, sourceBase, sourceExtension);

            QString status = tr("Ready");
            bool isBlocking = false;
            if (QFile::exists(fileName))
            {
                status = dialog.isOverwriteFiles() ? tr("Will overwrite") : tr("Already exists");
                isBlocking = !dialog.isOverwriteFiles();
            }
            const QString fileNameKey = normalizedOutputPathKey(fileName);
            if (generatedNames.contains(fileNameKey))
            {
                status = tr("Duplicate name");
                isBlocking = true;
            }
            generatedNames.insert(fileNameKey);

            previewItems.push_back({ fileName, QString::number(assembledPages.size()), sourceName, assembleModeText, status, isBlocking });
            ++outputIndex;
        }
        return previewItems;
    });
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    pdf::PDFDocumentManipulator manipulator;

    for (const auto& documentItem : m_model->getDocuments())
    {
        manipulator.addDocument(documentItem.first, &documentItem.second.document);
    }
    for (const auto& imageItem : m_model->getImages())
    {
        manipulator.addImage(imageItem.first, imageItem.second.image);
    }

    pdf::PDFOperationResult result(true);
    std::vector<std::pair<QString, pdf::PDFDocument>> assembledDocumentStorage;

    int assembledDocumentIndex = 1;
    const int documentCount = int(m_model->getDocuments().size());

    QString directory = dialog.getDirectory();
    QString fileNameTemplate = dialog.getFileName();
    const bool isOverwriteEnabled = dialog.isOverwriteFiles();
    pdf::PDFDocumentManipulator::OutlineMode outlineMode = dialog.getOutlineMode();
    const bool isImageOptimizationEnabled = dialog.isImageOptimizationEnabled();
    const pdf::PDFImageOptimizer::Settings imageOptimizationSettings = dialog.getImageOptimizationSettings();
    manipulator.setOutlineMode(outlineMode);

    if (!directory.endsWith('/'))
    {
        directory += "/";
    }

    if (!QDir(directory).exists())
    {
        QMessageBox::critical(this, tr("Error"), tr("Output directory does not exist."));
        return;
    }

    QSet<QString> generatedFileNames;
    int validationOutputIndex = 1;
    for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
    {
        const pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
        const int validationSourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
        const int validationSourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);
        auto [validationSourceName, validationSourceBase, validationSourceExtension] = sourceInfo(samplePage);
        const QString validationFileName = createOutputFileName(fileNameTemplate, directory, validationOutputIndex, validationOutputIndex, validationSourceDocumentIndex, validationSourcePageIndex, validationSourceName, validationSourceBase, validationSourceExtension);

        const QString validationFileNameKey = normalizedOutputPathKey(validationFileName);
        if (generatedFileNames.contains(validationFileNameKey))
        {
            QMessageBox::critical(this, tr("Error"), tr("Output file name '%1' is generated more than once. Please change the file template.").arg(validationFileName));
            return;
        }
        generatedFileNames.insert(validationFileNameKey);

        if (!isOverwriteEnabled && QFile::exists(validationFileName))
        {
            QMessageBox::critical(this, tr("Error"), tr("Document with filename '%1' already exists.").arg(validationFileName));
            return;
        }

        ++validationOutputIndex;
    }

    for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
    {
        pdf::PDFOperationResult currentResult = manipulator.assemble(assembledPages);
        if (!currentResult && result)
        {
            result = currentResult;
            break;
        }

        pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
        const int sourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
        const int sourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);

        auto [sourceName, sourceBase, sourceExtension] = sourceInfo(samplePage);
        QString fileName = createOutputFileName(fileNameTemplate, directory, assembledDocumentIndex, assembledDocumentIndex, sourceDocumentIndex, sourcePageIndex, sourceName, sourceBase, sourceExtension);

        pdf::PDFDocument assembledDocument = manipulator.takeAssembledDocument();
        if (m_hasPageGeometrySettings)
        {
            const pdf::PDFOperationResult geometryResult = pdf::PDFPageGeometry::apply(&assembledDocument, m_pageGeometrySettings);
            if (!geometryResult)
            {
                QMessageBox::critical(this, tr("Error"), geometryResult.getErrorMessage());
                return;
            }
        }

        if (isImageOptimizationEnabled)
        {
            pdf::PDFImageOptimizer imageOptimizer;
            assembledDocument = imageOptimizer.optimize(&assembledDocument, imageOptimizationSettings);
        }

        assembledDocumentStorage.emplace_back(std::make_pair(std::move(fileName), std::move(assembledDocument)));
        ++assembledDocumentIndex;
    }

    if (!result)
    {
        QMessageBox::critical(this, tr("Error"), result.getErrorMessage());
        return;
    }

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

void MainWindow::splitDocuments()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Split PDF"));

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* scopeLabel = new QLabel(getSelectedRows().isEmpty() ? tr("Split the whole workspace.") : tr("Split the selected workspace items."), &dialog);
    layout->addWidget(scopeLabel);

    QFormLayout* formLayout = new QFormLayout();
    QComboBox* modeComboBox = new QComboBox(&dialog);
    modeComboBox->addItem(tr("Every page"), static_cast<int>(SplitMode::EveryPage));
    modeComboBox->addItem(tr("Every N pages"), static_cast<int>(SplitMode::EveryNPages));
    modeComboBox->addItem(tr("At selected page numbers"), static_cast<int>(SplitMode::PageNumbers));
    modeComboBox->addItem(tr("At top-level bookmarks"), static_cast<int>(SplitMode::TopLevelBookmarks));
    modeComboBox->addItem(tr("By approximate output file size"), static_cast<int>(SplitMode::ApproximateSize));
    formLayout->addRow(tr("Split"), modeComboBox);

    QLabel* pagesPerDocumentLabel = new QLabel(tr("Pages per document"), &dialog);
    QSpinBox* pagesPerDocumentSpinBox = new QSpinBox(&dialog);
    pagesPerDocumentSpinBox->setRange(1, 1000000);
    pagesPerDocumentSpinBox->setValue(10);
    formLayout->addRow(pagesPerDocumentLabel, pagesPerDocumentSpinBox);

    QLabel* pageNumbersLabel = new QLabel(tr("Start new document at pages"), &dialog);
    QLineEdit* pageNumbersLineEdit = new QLineEdit(&dialog);
    pageNumbersLineEdit->setPlaceholderText(tr("10, 20, 35"));
    formLayout->addRow(pageNumbersLabel, pageNumbersLineEdit);

    QLabel* approximateSizeLabel = new QLabel(tr("Approximate size"), &dialog);
    QDoubleSpinBox* approximateSizeSpinBox = new QDoubleSpinBox(&dialog);
    approximateSizeSpinBox->setRange(0.1, 102400.0);
    approximateSizeSpinBox->setDecimals(1);
    approximateSizeSpinBox->setValue(10.0);
    approximateSizeSpinBox->setSuffix(tr(" MB"));
    formLayout->addRow(approximateSizeLabel, approximateSizeSpinBox);
    layout->addLayout(formLayout);

    QLabel* approximationNoteLabel = new QLabel(tr("Approximate size split is estimated from source file sizes; actual PDF output size can vary."), &dialog);
    approximationNoteLabel->setWordWrap(true);
    layout->addWidget(approximationNoteLabel);

    QLabel* resultLabel = new QLabel(&dialog);
    resultLabel->setWordWrap(true);
    layout->addWidget(resultLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    auto createSplitDocuments = [&]() -> std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>>
    {
        const QModelIndexList indexes = getSelectedRowsOrAll();
        const SplitMode mode = SplitMode(modeComboBox->currentData().toInt());

        switch (mode)
        {
            case SplitMode::EveryPage:
                return m_model->getSplitAssembledPagesEveryN(indexes, 1);

            case SplitMode::EveryNPages:
                return m_model->getSplitAssembledPagesEveryN(indexes, pagesPerDocumentSpinBox->value());

            case SplitMode::PageNumbers:
            {
                bool ok = false;
                const std::vector<int> pagePositions = parseSplitPagePositions(pageNumbersLineEdit->text(), &ok);
                if (!ok)
                {
                    return {};
                }
                return m_model->getSplitAssembledPagesAtPagePositions(indexes, pagePositions);
            }

            case SplitMode::TopLevelBookmarks:
            {
                const PageItemModel::SelectionInfo info = m_model->getSelectionInfo(indexes);
                if (!info.isSingleDocument())
                {
                    return {};
                }

                const std::map<int, DocumentItem>& documents = m_model->getDocuments();
                auto it = documents.find(info.firstDocumentIndex);
                if (it == documents.cend())
                {
                    return {};
                }

                const std::vector<pdf::PDFInteger> pageIndices = getTopLevelBookmarkPageIndices(&it->second.document);
                if (pageIndices.empty())
                {
                    return {};
                }

                return m_model->getSplitAssembledPagesAtDocumentPages(indexes, pageIndices);
            }

            case SplitMode::ApproximateSize:
            {
                const qint64 maximumSizeBytes = qint64(approximateSizeSpinBox->value() * 1024.0 * 1024.0);
                return m_model->getSplitAssembledPagesByApproximateSize(indexes, maximumSizeBytes);
            }

            default:
                Q_ASSERT(false);
                break;
        }

        return {};
    };

    auto updateDialog = [&]()
    {
        const SplitMode mode = SplitMode(modeComboBox->currentData().toInt());
        const bool isEveryNPages = mode == SplitMode::EveryNPages;
        const bool isPageNumbers = mode == SplitMode::PageNumbers;
        const bool isApproximateSize = mode == SplitMode::ApproximateSize;

        pagesPerDocumentLabel->setVisible(isEveryNPages);
        pagesPerDocumentSpinBox->setVisible(isEveryNPages);
        pageNumbersLabel->setVisible(isPageNumbers);
        pageNumbersLineEdit->setVisible(isPageNumbers);
        approximateSizeLabel->setVisible(isApproximateSize);
        approximateSizeSpinBox->setVisible(isApproximateSize);
        approximationNoteLabel->setVisible(isApproximateSize);

        QString message;
        bool isValid = true;
        if (mode == SplitMode::PageNumbers)
        {
            bool ok = false;
            parseSplitPagePositions(pageNumbersLineEdit->text(), &ok);
            if (!ok)
            {
                isValid = false;
                message = tr("Enter one or more page numbers, for example 10, 20, 35.");
            }
        }
        else if (mode == SplitMode::TopLevelBookmarks)
        {
            const PageItemModel::SelectionInfo info = m_model->getSelectionInfo(getSelectedRowsOrAll());
            if (!info.isSingleDocument())
            {
                isValid = false;
                message = tr("Top-level bookmark split requires exactly one source PDF.");
            }
        }

        const std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> documents = isValid ? createSplitDocuments() : std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>>();
        if (isValid && documents.empty())
        {
            isValid = false;
            message = mode == SplitMode::TopLevelBookmarks ? tr("No top-level bookmarks with page destinations were found.") : tr("No output documents would be created.");
        }
        else if (isValid)
        {
            message = tr("%1 output document(s) will be created before writing files.").arg(documents.size());
        }

        resultLabel->setText(message);
        buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isValid);
    };

    connect(modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateDialog);
    connect(pagesPerDocumentSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, updateDialog);
    connect(pageNumbersLineEdit, &QLineEdit::textChanged, &dialog, updateDialog);
    connect(approximateSizeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog, updateDialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, &dialog, [&]()
    {
        std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledDocuments = createSplitDocuments();
        if (assembledDocuments.empty())
        {
            QMessageBox::warning(&dialog, tr("Split PDF"), tr("No output documents would be created."));
            return;
        }

        dialog.accept();
        const SplitMode mode = SplitMode(modeComboBox->currentData().toInt());
        exportAssembledDocuments(qMove(assembledDocuments), mode == SplitMode::ApproximateSize ? tr("Split documents (approximate size)") : tr("Split documents"));
    });

    updateDialog();
    dialog.exec();
}

void MainWindow::selectPageRange()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Select Page Range"));

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* descriptionLabel = new QLabel(tr("Use workspace order for visible items, or original PDF page numbers for the current source document."), &dialog);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);

    QFormLayout* formLayout = new QFormLayout();
    QLineEdit* rangeEdit = new QLineEdit(&dialog);
    rangeEdit->setPlaceholderText(tr("1-3, 8, 10-12"));
    formLayout->addRow(tr("Range"), rangeEdit);

    QComboBox* scopeComboBox = new QComboBox(&dialog);
    scopeComboBox->addItem(tr("Select in whole workspace (visible order)"), 0);
    scopeComboBox->addItem(tr("Select within current source document (original page numbers)"), 1);
    formLayout->addRow(tr("Scope"), scopeComboBox);

    QComboBox* selectionModeComboBox = new QComboBox(&dialog);
    selectionModeComboBox->addItem(tr("Replace current selection"), 0);
    selectionModeComboBox->addItem(tr("Add to current selection"), 1);
    formLayout->addRow(tr("Selection"), selectionModeComboBox);
    layout->addLayout(formLayout);

    QLabel* resultLabel = new QLabel(&dialog);
    resultLabel->setWordWrap(true);
    layout->addWidget(resultLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    auto getCurrentSourceDocumentIndex = [&]() -> int
    {
        QModelIndexList selection = getSelectedRows();
        if (selection.isEmpty())
        {
            const QModelIndex currentProxyIndex = ui->documentItemsView->currentIndex();
            const QModelIndex currentSourceIndex = m_filterModel->mapToSource(currentProxyIndex);
            if (currentSourceIndex.isValid())
            {
                selection << m_model->index(currentSourceIndex.row(), 0, QModelIndex());
            }
        }

        for (const QModelIndex& index : selection)
        {
            const PageGroupItem* item = m_model->getItem(index);
            if (!item)
            {
                continue;
            }

            for (const PageGroupItem::GroupItem& groupItem : item->groups)
            {
                if (groupItem.pageType == PT_DocumentPage && groupItem.documentIndex != -1)
                {
                    return groupItem.documentIndex;
                }
            }
        }

        return -1;
    };

    auto createSelection = [&](QString* errorMessage) -> QItemSelection
    {
        QItemSelection selection;
        const bool useCurrentSource = scopeComboBox->currentData().toInt() == 1;
        if (rangeEdit->text().trimmed().isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = tr("Enter a page range, for example 1-3, 8, 10-12.");
            }
            return selection;
        }

        if (useCurrentSource)
        {
            const int documentIndex = getCurrentSourceDocumentIndex();
            auto documentIt = m_model->getDocuments().find(documentIndex);
            if (documentIt == m_model->getDocuments().cend())
            {
                if (errorMessage)
                {
                    *errorMessage = tr("Select a workspace item from a PDF source first.");
                }
                return selection;
            }

            QString parseError;
            const std::vector<pdf::PDFInteger> pages = PDFPageImportDialog::parsePageRangeText(rangeEdit->text(), documentIt->second.document.getCatalog()->getPageCount(), &parseError);
            if (!parseError.isEmpty())
            {
                if (errorMessage)
                {
                    *errorMessage = parseError;
                }
                return selection;
            }

            QSet<pdf::PDFInteger> pageSet;
            for (pdf::PDFInteger page : pages)
            {
                pageSet.insert(page);
            }

            for (const QModelIndex& rowIndex : getVisibleRows())
            {
                const PageGroupItem* item = m_model->getItem(rowIndex);
                if (!item)
                {
                    continue;
                }

                const bool matches = std::any_of(item->groups.cbegin(), item->groups.cend(), [&](const PageGroupItem::GroupItem& groupItem)
                {
                    return groupItem.pageType == PT_DocumentPage && groupItem.documentIndex == documentIndex && pageSet.contains(groupItem.pageIndex);
                });

                if (matches)
                {
                    selection.select(rowIndex, rowIndex);
                }
            }
        }
        else
        {
            const int visibleRowCount = m_filterModel->rowCount(QModelIndex());
            QString parseError;
            const std::vector<pdf::PDFInteger> positions = PDFPageImportDialog::parsePageRangeText(rangeEdit->text(), visibleRowCount, &parseError);
            if (!parseError.isEmpty())
            {
                if (errorMessage)
                {
                    *errorMessage = parseError;
                }
                return selection;
            }

            for (pdf::PDFInteger position : positions)
            {
                const QModelIndex proxyIndex = m_filterModel->index(int(position - 1), 0, QModelIndex());
                const QModelIndex sourceIndex = m_filterModel->mapToSource(proxyIndex);
                if (sourceIndex.isValid())
                {
                    selection.select(m_model->index(sourceIndex.row(), 0, QModelIndex()), m_model->index(sourceIndex.row(), 0, QModelIndex()));
                }
            }
        }

        if (selection.isEmpty() && errorMessage)
        {
            *errorMessage = tr("No visible workspace items match this page range.");
        }

        return selection;
    };

    auto updateDialog = [&]()
    {
        QString errorMessage;
        const QItemSelection selection = createSelection(&errorMessage);
        const bool isValid = !selection.isEmpty();
        resultLabel->setText(isValid ? tr("%1 visible workspace item(s) will be selected.").arg(selection.indexes().size()) : errorMessage);
        buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isValid);
    };

    connect(rangeEdit, &QLineEdit::textChanged, &dialog, updateDialog);
    connect(scopeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateDialog);
    connect(selectionModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateDialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, &dialog, [&]()
    {
        QString errorMessage;
        const QItemSelection selection = createSelection(&errorMessage);
        if (selection.isEmpty())
        {
            QMessageBox::warning(&dialog, tr("Select Page Range"), errorMessage);
            return;
        }

        const bool addToExisting = selectionModeComboBox->currentData().toInt() == 1;
        selectSourceSelection(selection, addToExisting);
        dialog.accept();
    });

    updateDialog();
    dialog.exec();
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
            m_model->cloneSelection(getSelectedRows());
            break;
        }

        case Operation::RemoveSelection:
        {
            m_model->removeSelection(getSelectedRows());
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
            selectSourceSelection(itemSelection);
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
            QModelIndexList indices = getSelectedRows();

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
            QModelIndexList indices = getSelectedRows();

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
            m_model->group(getSelectedRows());
            break;

        case Operation::Ungroup:
            m_model->ungroup(getSelectedRows());
            break;

        case Operation::SelectNone:
            ui->documentItemsView->clearSelection();
            break;

        case Operation::SelectAll:
            ui->documentItemsView->selectAll();
            break;

        case Operation::SelectEven:
            selectSourceSelection(m_model->getSelectionEven());
            break;

        case Operation::SelectOdd:
            selectSourceSelection(m_model->getSelectionOdd());
            break;

        case Operation::SelectPortrait:
            selectSourceSelection(m_model->getSelectionPortrait());
            break;

        case Operation::SelectLandscape:
            selectSourceSelection(m_model->getSelectionLandscape());
            break;

        case Operation::SelectVisible:
        {
            QItemSelection itemSelection;
            for (const QModelIndex& index : getVisibleRows())
            {
                itemSelection.select(index, index);
            }
            selectSourceSelection(itemSelection);
            break;
        }

        case Operation::SelectPageRange:
            selectPageRange();
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
            m_model->rotateLeft(getSelectedRows());
            break;

        case Operation::RotateRight:
            m_model->rotateRight(getSelectedRows());
            break;

        case Operation::ResetRotation:
            m_model->resetRotation(getSelectedRows());
            break;

        case Operation::GetSource:
            QDesktopServices::openUrl(QUrl("https://github.com/JakubMelka/PDF4QT"));
            break;

        case Operation::BecomeSponsor:
            QDesktopServices::openUrl(QUrl("https://github.com/sponsors/JakubMelka"));
            break;

        case Operation::InsertEmptyPage:
            m_model->insertEmptyPage(getSelectedRows());
            break;

        case Operation::ConfigurePageGeometry:
        {
            pdf::PDFPageGeometryDialog dialog(this);

            std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledPages = m_model->getAssembledPages(PageItemModel::AssembleMode::Unite);
            const pdf::PDFInteger pageCount = assembledPages.empty() ? 0 : pdf::PDFInteger(assembledPages.front().size());
            dialog.setPageCount(pageCount);

            if (m_hasPageGeometrySettings)
            {
                dialog.setSettings(m_pageGeometrySettings);
            }

            if (dialog.exec() == QDialog::Accepted)
            {
                m_pageGeometrySettings = dialog.getSettings();
                m_hasPageGeometrySettings = true;
            }
            break;
        }

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

        case Operation::ShowDetailsView:
            setDetailsViewVisible(m_showDetailsViewAction->isChecked());
            break;

        case Operation::SortByFileName:
            m_model->sortItems(getSelectedRowsForOrdering(), PageItemModel::SortMode::FileName, Qt::AscendingOrder);
            break;

        case Operation::SortBySource:
            m_model->sortItems(getSelectedRowsForOrdering(), PageItemModel::SortMode::Source, Qt::AscendingOrder);
            break;

        case Operation::SortByPageNumber:
            m_model->sortItems(getSelectedRowsForOrdering(), PageItemModel::SortMode::PageNumber, Qt::AscendingOrder);
            break;

        case Operation::SortByType:
            m_model->sortItems(getSelectedRowsForOrdering(), PageItemModel::SortMode::Type, Qt::AscendingOrder);
            break;

        case Operation::ReverseOrder:
            m_model->reverseItems(getSelectedRowsForOrdering());
            break;

        case Operation::RenameGroup:
        {
            const QModelIndexList selection = getSelectedRows();
            if (selection.isEmpty())
            {
                break;
            }

            const PageGroupItem* item = m_model->getItem(selection.front());
            const QString currentName = item ? m_model->getItemDisplayText(item) : QString();
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Rename Item or Group"), tr("Display name"), QLineEdit::Normal, currentName, &ok);
            if (ok)
            {
                m_model->renameItems(selection, name.trimmed());
            }
            break;
        }

        case Operation::Properties:
            showItemProperties();
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

            QString assembleModeText;
            switch (assembleMode)
            {
                case PageItemModel::AssembleMode::Unite:
                    assembleModeText = tr("United document");
                    break;
                case PageItemModel::AssembleMode::Separate:
                    assembleModeText = tr("Separate documents");
                    break;
                case PageItemModel::AssembleMode::SeparateGrouped:
                    assembleModeText = tr("Grouped documents");
                    break;
                default:
                    Q_ASSERT(false);
                    break;
            }

            std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> assembledDocuments = m_model->getAssembledPages(assembleMode);

            // Check we have something to process
            if (assembledDocuments.empty())
            {
                QMessageBox::critical(this, tr("Error"), tr("No documents to assemble."));
                break;
            }

            auto sourceInfo = [this](const pdf::PDFDocumentManipulator::AssembledPage& page)
            {
                QFileInfo info;
                QString name;
                QString extension;
                if (page.documentIndex != -1)
                {
                    auto it = m_model->getDocuments().find(page.documentIndex);
                    if (it != m_model->getDocuments().cend())
                    {
                        info = QFileInfo(it->second.fileName);
                        name = info.fileName();
                        extension = info.suffix();
                    }
                }
                else if (page.imageIndex != -1)
                {
                    auto it = m_model->getImages().find(page.imageIndex);
                    if (it != m_model->getImages().cend())
                    {
                        const ImageItem& image = it->second;
                        info = QFileInfo(!image.fileName.isEmpty() ? image.fileName : image.displayName);
                        name = !image.displayName.isEmpty() ? image.displayName : info.fileName();
                        extension = !info.suffix().isEmpty() ? info.suffix() : image.format;
                    }
                }

                return std::make_tuple(name, info.completeBaseName(), extension);
            };

            AssembleOutputSettingsDialog dialog(m_settings.directory, this);
            dialog.setOutputPreviewFactory([&]()
            {
                std::vector<AssembleOutputSettingsDialog::OutputPreviewItem> previewItems;
                previewItems.reserve(assembledDocuments.size());
                QSet<QString> generatedNames;
                int outputIndex = 1;
                const int documentCount = int(m_model->getDocuments().size());
                if (!QDir(dialog.getDirectory()).exists())
                {
                    previewItems.push_back({ dialog.getDirectory(), QString(), QString(), assembleModeText, tr("Output directory does not exist"), true });
                    return previewItems;
                }

                for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
                {
                    const pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
                    const int sourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
                    const int sourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);
                    auto [sourceName, sourceBase, sourceExtension] = sourceInfo(samplePage);
                    const QString fileName = createOutputFileName(dialog.getFileName(), dialog.getDirectory(), outputIndex, outputIndex, sourceDocumentIndex, sourcePageIndex, sourceName, sourceBase, sourceExtension);

                    QString status = tr("Ready");
                    bool isBlocking = false;
                    if (QFile::exists(fileName))
                    {
                        status = dialog.isOverwriteFiles() ? tr("Will overwrite") : tr("Already exists");
                        isBlocking = !dialog.isOverwriteFiles();
                    }
                    const QString fileNameKey = normalizedOutputPathKey(fileName);
                    if (generatedNames.contains(fileNameKey))
                    {
                        status = tr("Duplicate name");
                        isBlocking = true;
                    }
                    generatedNames.insert(fileNameKey);

                    previewItems.push_back({ fileName, QString::number(assembledPages.size()), sourceName, assembleModeText, status, isBlocking });
                    ++outputIndex;
                }
                return previewItems;
            });
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
                const bool isImageOptimizationEnabled = dialog.isImageOptimizationEnabled();
                const pdf::PDFImageOptimizer::Settings imageOptimizationSettings = dialog.getImageOptimizationSettings();
                manipulator.setOutlineMode(outlineMode);

                if (!directory.endsWith('/'))
                {
                    directory += "/";
                }

                if (!QDir(directory).exists())
                {
                    QMessageBox::critical(this, tr("Error"), tr("Output directory does not exist."));
                    return;
                }

                QSet<QString> generatedFileNames;
                int validationOutputIndex = 1;
                for (const std::vector<pdf::PDFDocumentManipulator::AssembledPage>& assembledPages : assembledDocuments)
                {
                    const pdf::PDFDocumentManipulator::AssembledPage samplePage = assembledPages.front();
                    const int validationSourceDocumentIndex = samplePage.documentIndex == -1 ? documentCount + samplePage.imageIndex : samplePage.documentIndex;
                    const int validationSourcePageIndex = qMax(int(samplePage.pageIndex + 1), 1);
                    auto [validationSourceName, validationSourceBase, validationSourceExtension] = sourceInfo(samplePage);
                    const QString validationFileName = createOutputFileName(fileNameTemplate, directory, validationOutputIndex, validationOutputIndex, validationSourceDocumentIndex, validationSourcePageIndex, validationSourceName, validationSourceBase, validationSourceExtension);

                    const QString validationFileNameKey = normalizedOutputPathKey(validationFileName);
                    if (generatedFileNames.contains(validationFileNameKey))
                    {
                        QMessageBox::critical(this, tr("Error"), tr("Output file name '%1' is generated more than once. Please change the file template.").arg(validationFileName));
                        return;
                    }
                    generatedFileNames.insert(validationFileNameKey);

                    if (!isOverwriteEnabled && QFile::exists(validationFileName))
                    {
                        QMessageBox::critical(this, tr("Error"), tr("Document with filename '%1' already exists.").arg(validationFileName));
                        return;
                    }

                    ++validationOutputIndex;
                }

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

                    auto [sourceName, sourceBase, sourceExtension] = sourceInfo(samplePage);
                    QString fileName = createOutputFileName(fileNameTemplate, directory, assembledDocumentIndex, assembledDocumentIndex, sourceDocumentIndex, sourcePageIndex, sourceName, sourceBase, sourceExtension);

                    pdf::PDFDocument assembledDocument = manipulator.takeAssembledDocument();
                    if (m_hasPageGeometrySettings)
                    {
                        const pdf::PDFOperationResult geometryResult = pdf::PDFPageGeometry::apply(&assembledDocument, m_pageGeometrySettings);
                        if (!geometryResult)
                        {
                            QMessageBox::critical(this, tr("Error"), geometryResult.getErrorMessage());
                            return;
                        }
                    }

                    if (isImageOptimizationEnabled)
                    {
                        pdf::PDFImageOptimizer imageOptimizer;
                        assembledDocument = imageOptimizer.optimize(&assembledDocument, imageOptimizationSettings);
                    }

                    assembledDocumentStorage.emplace_back(std::make_pair(std::move(fileName), std::move(assembledDocument)));
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

        case Operation::Split:
            splitDocuments();
            break;

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
                QModelIndexList indexes = getSelectedRows();
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
                QModelIndexList indexes = getSelectedRows();
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

        case Operation::InsertPDFPages:
        {
            QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), m_settings.directory, tr("PDF document (*.pdf)"));
            if (fileName.isEmpty())
            {
                break;
            }

            QFileInfo fileInfo(fileName);
            m_settings.directory = fileInfo.dir().absolutePath();

            auto queryPassword = [this, &fileInfo](bool* ok)
            {
                *ok = false;
                return QInputDialog::getText(this, tr("Encrypted document"), tr("Enter password to access document '%1'").arg(fileInfo.fileName()), QLineEdit::Password, QString(), ok);
            };

            pdf::PDFDocumentReader reader(nullptr, qMove(queryPassword), true, false);
            pdf::PDFDocument document = reader.readFromFile(fileName);

            if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
            {
                if (reader.getReadingResult() == pdf::PDFDocumentReader::Result::Failed)
                {
                    QMessageBox::critical(this, tr("Error"), reader.getErrorMessage());
                }
                break;
            }

            const pdf::PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
            if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) &&
                !securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
            {
                QMessageBox::critical(this, tr("Error"), tr("Document security doesn't permit to organize pages."));
                break;
            }

            const pdf::PDFInteger pageCount = document.getCatalog()->getPageCount();
            PDFPageImportDialog dialog(fileName, pageCount, this);
            if (dialog.exec() != QDialog::Accepted)
            {
                break;
            }
            const std::vector<pdf::PDFInteger> pages = dialog.getPages();

            QModelIndexList indexes = getSelectedRows();
            QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();
            if (m_model->insertDocument(fileName, qMove(document), insertIndex, pages) == -1)
            {
                QMessageBox::critical(this, tr("Error"), tr("Document '%1' is already in the workspace.").arg(fileInfo.fileName()));
            }
            break;
        }

        case Operation::ReplaceSelection:
        {
            QModelIndexList indexes = getSelectedRows();

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
            QModelIndexList indexes = getSelectedRows();
            m_model->regroupReversed(indexes);
            break;
        }

        case Operation::RegroupEvenOdd:
        {
            QModelIndexList indexes = getSelectedRows();
            m_model->regroupEvenOdd(indexes);
            break;
        }

        case Operation::RegroupPaired:
        {
            QModelIndexList indexes = getSelectedRows();
            m_model->regroupPaired(indexes);
            break;
        }

        case Operation::RegroupOutline:
        {
            QModelIndexList indexes = getSelectedRows();

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

                        for (const pdf::PDFOutlineItem* item : outlineItems)
                        {
                            pdf::PDFInteger pageIndex = 0;
                            if (resolveOutlinePageIndex(document, item, &pageIndex))
                            {
                                breakPageIndices.push_back(pageIndex);
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
            QModelIndexList indexes = getSelectedRows();
            m_model->regroupAlternatingPages(indexes, false);
            break;
        }

        case Operation::RegroupAlternatingPagesReversed:
        {
            QModelIndexList indexes = getSelectedRows();
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
            QModelIndexList indexes = getSelectedRows();
            QModelIndex insertIndex = !indexes.isEmpty() ? indexes.front() : QModelIndex();
            m_model->insertImage(image, insertIndex);
            event->accept();
        }
    }
    else if (mimeData->hasUrls())
    {
        QModelIndexList indexes = getSelectedRows();
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
