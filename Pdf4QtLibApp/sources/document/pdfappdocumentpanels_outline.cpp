// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfappdocumentpanels_outline.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappnotesoutlinecommands.h"
#include "pdfappoutlineeditor.h"
#include "pdfdatabasenotifier.h"
#include "pdfshortcutrepository.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

namespace pdf
{

/// Delegate of the outline tree: the inline editor (rename, new item) covers the
/// whole shape of the row, the field drawn by the style below it, so its text
/// (the frameless editor keeps the padding of a field) starts where the row draws
/// the title - outline-row 195:1503: the title at 26, padding 8 at the end
class PDFAppOutlineItemDelegate : public QStyledItemDelegate
{
public:
    explicit PDFAppOutlineItemDelegate(QObject* parent) :
        QStyledItemDelegate(parent)
    {
    }

    virtual void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!editor)
        {
            return;
        }

        QStyleOptionViewItem viewOption(option);
        initStyleOption(&viewOption, index);
        const QWidget* widget = option.widget;
        const QStyle* style = widget ? widget->style() : QApplication::style();
        const QRect shape = style->subElementRect(QStyle::SE_ItemViewItemFocusRect, &viewOption, widget);
        if (shape.isValid())
        {
            editor->setGeometry(shape);
        }
        else
        {
            QStyledItemDelegate::updateEditorGeometry(editor, option, index);
        }
    }
};

/// Helpers of the outline panel
class PDFAppOutlinePanelHelper
{
public:
    PDFAppOutlinePanelHelper() = delete;

    /// Longest title of an item shown
    static constexpr int MAXIMUM_TITLE_LENGTH = 1024;

    /// Property of the actions of the context menu: the id of the command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtCommand";

    /// Item of a target in the submenu Set Target (1236:165602; the names of the
    /// Target menu of Link to page 1170:27714 and of the link properties 1171:28262
    /// in the title case of the menu; Area… and Position… are picked on a page)
    static QString getTargetMenuText(DestinationType type)
    {
        switch (type)
        {
            case DestinationType::Fit:
                return PDFAppOutlinePanel::tr("Whole Page");
            case DestinationType::FitH:
                return PDFAppOutlinePanel::tr("Page Width");
            case DestinationType::FitV:
                return PDFAppOutlinePanel::tr("Page Height");
            case DestinationType::FitR:
                return PDFAppOutlinePanel::tr("Area…");
            case DestinationType::XYZ:
                return PDFAppOutlinePanel::tr("Position…");
            case DestinationType::FitB:
                return PDFAppOutlinePanel::tr("Visible Content");
            case DestinationType::FitBH:
                return PDFAppOutlinePanel::tr("Visible Content Width");
            case DestinationType::FitBV:
                return PDFAppOutlinePanel::tr("Visible Content Height");
            default:
                break;
        }
        return QString();
    }

    /// Title of an item: control characters replaced by spaces, shortened
    static QString sanitizeTitle(const QString& title)
    {
        QString result = title.left(MAXIMUM_TITLE_LENGTH);
        for (QChar& character : result)
        {
            if (character.category() == QChar::Other_Control || character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
            {
                character = QChar(u' ');
            }
        }
        return result.simplified();
    }

    /// Top of the destination of an outline action in the coordinates of its page
    /// (+infinity: the top of the page - no position, Fit, a damaged value)
    static PDFReal getActionTop(const PDFCatalog* catalog, const PDFAction* action)
    {
        constexpr PDFReal pageTop = std::numeric_limits<PDFReal>::infinity();
        if (!catalog || !action)
        {
            return pageTop;
        }

        for (const PDFAction* current : action->getActionList())
        {
            if (!current || current->getType() != ActionType::GoTo)
            {
                continue;
            }

            const PDFDestination* destination = &static_cast<const PDFActionGoTo*>(current)->getDestination();
            if (destination->getDestinationType() == DestinationType::Named)
            {
                destination = catalog->getNamedDestination(destination->getName());
            }
            if (!destination)
            {
                return pageTop;
            }

            switch (destination->getDestinationType())
            {
                case DestinationType::XYZ:
                case DestinationType::FitH:
                case DestinationType::FitBH:
                case DestinationType::FitR:
                    return qIsFinite(destination->getTop()) ? destination->getTop() : pageTop;

                default:
                    return pageTop;
            }
        }
        return pageTop;
    }

    /// Flags of an item (editable, draggable, a drop target) for the editing state
    static void setItemFlags(QStandardItem* item, bool isEditable)
    {
        item->setEditable(isEditable);
        item->setDragEnabled(isEditable);
        item->setDropEnabled(isEditable);
    }
};

PDFAppOutlineItemModel::PDFAppOutlineItemModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppOutlineItemModel::~PDFAppOutlineItemModel() = default;

PDFAppAnnotationOperations::OutlinePath PDFAppOutlineItemModel::getPath(const QModelIndex& index)
{
    PDFAppAnnotationOperations::OutlinePath path;
    for (QModelIndex current = index; current.isValid(); current = current.parent())
    {
        path.insert(path.begin(), size_t(current.row()));
    }
    return path;
}

QStringList PDFAppOutlineItemModel::mimeTypes() const
{
    return { getMimeType() };
}

QMimeData* PDFAppOutlineItemModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty() || m_documentKey.isEmpty())
    {
        return nullptr;
    }

    // "key" LF "row,row,row;row,row" (the paths of all dragged items - more items
    // of the extended selection)
    QStringList paths;
    for (const QModelIndex& index : indexes)
    {
        if (!index.isValid() || index.column() != 0)
        {
            continue;
        }

        QStringList rows;
        for (size_t row : getPath(index))
        {
            rows << QString::number(row);
        }
        paths << rows.join(QLatin1Char(','));
    }
    if (paths.isEmpty())
    {
        return nullptr;
    }

    QMimeData* data = new QMimeData();
    data->setData(getMimeType(), (m_documentKey + QLatin1Char('\n') + paths.join(QLatin1Char(';'))).toUtf8());
    return data;
}

std::vector<PDFAppAnnotationOperations::OutlinePath> PDFAppOutlineItemModel::getDraggedPaths(const QMimeData* data, const QString& documentKey)
{
    // The data may come from another process: bounded and checked
    std::vector<PDFAppAnnotationOperations::OutlinePath> paths;
    if (!data || documentKey.isEmpty() || !data->hasFormat(getMimeType()))
    {
        return paths;
    }

    const QByteArray bytes = data->data(getMimeType());
    if (bytes.size() > MAXIMUM_DRAG_DATA_SIZE)
    {
        return paths;
    }

    // Every move of a drag asks: the key of the document is compared first, and the
    // rows are read from the bytes - nothing of the data (up to 4 MB) is decoded
    const QByteArray prefix = documentKey.toUtf8() + '\n';
    if (!bytes.startsWith(prefix))
    {
        return paths;
    }

    const QByteArray text = bytes.sliced(prefix.size());
    if (size_t(text.count(';')) >= PDFAppAnnotationOperations::MAXIMUM_OUTLINE_ITEMS)
    {
        return paths;
    }

    const QList<QByteArray> items = text.split(';');
    for (const QByteArray& item : items)
    {
        const QList<QByteArray> rows = item.split(',');
        if (rows.isEmpty() || size_t(rows.size()) > PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH)
        {
            return {};
        }

        PDFAppAnnotationOperations::OutlinePath path;
        for (const QByteArray& row : rows)
        {
            bool ok = false;
            const uint value = row.trimmed().toUInt(&ok);
            if (!ok || value >= PDFAppAnnotationOperations::MAXIMUM_OUTLINE_ITEMS)
            {
                return {};
            }
            path.push_back(size_t(value));
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

std::optional<PDFAppAnnotationOperations::OutlinePath> PDFAppOutlineItemModel::getDraggedPath(const QMimeData* data, const QString& documentKey)
{
    // More dragged items: the first one
    std::vector<PDFAppAnnotationOperations::OutlinePath> paths = getDraggedPaths(data, documentKey);
    if (paths.empty())
    {
        return std::nullopt;
    }
    return paths.front();
}

Qt::DropActions PDFAppOutlineItemModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions PDFAppOutlineItemModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool PDFAppOutlineItemModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return getDraggedPath(data, m_documentKey).has_value();
}

bool PDFAppOutlineItemModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    // The panel performs the drop (one undo step of the document), the model
    // never inserts the dragged data itself
    Q_UNUSED(data);
    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return false;
}

int PDFAppOutlinePanel::findCurrentItemIndex(const std::vector<ItemPosition>& items, PDFInteger currentPage, PDFReal viewTop)
{
    // The page: the greatest page not after the current one
    PDFInteger bestPage = -1;
    for (const ItemPosition& item : items)
    {
        if (item.page >= 0 && item.page <= currentPage && item.page > bestPage)
        {
            bestPage = item.page;
        }
    }
    if (bestPage < 0)
    {
        return -1;
    }

    // An earlier page lies above the view whole: its lowest item (of equal ones
    // the later); the current page: the item closest above the top of the view
    const PDFReal limit = bestPage == currentPage ? viewTop : -std::numeric_limits<PDFReal>::infinity();
    int above = -1;
    int topmost = -1;
    for (int i = 0; i < int(items.size()); ++i)
    {
        const ItemPosition& item = items[size_t(i)];
        if (item.page != bestPage)
        {
            continue;
        }

        if (item.top >= limit && (above < 0 || item.top <= items[size_t(above)].top))
        {
            above = i;
        }
        if (topmost < 0 || item.top > items[size_t(topmost)].top)
        {
            topmost = i;
        }
    }
    return above >= 0 ? above : topmost;
}

PDFAppOutlineFilterModel::PDFAppOutlineFilterModel(QObject* parent) :
    BaseClass(parent)
{
    setRecursiveFilteringEnabled(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(Qt::DisplayRole);
}

PDFAppOutlineFilterModel::~PDFAppOutlineFilterModel() = default;

PDFAppOutlinePanel::PDFAppOutlinePanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outlinePanel"));

    // sidebar-content 195:1489: the search field at 2 / 12, the tree 8 below it;
    // PDFTreeView has the margins 6 around its rows
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, PDFAppPanelWidgets::CONTENT_PADDING_TOP, PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, PDFAppPanelWidgets::CONTENT_PADDING_BOTTOM - 6);
    layout->setSpacing(8 - 6);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(6, 0, 6, 0);
    m_searchField = PDFAppPanelWidgets::createSearchField(this, tr("Search outline…"), QStringLiteral("outlineSearch"));
    searchLayout->addWidget(m_searchField);
    layout->addLayout(searchLayout);

    m_tree = new PDFTreeView(this);
    m_tree->setObjectName(QStringLiteral("outlineTree"));
    m_tree->setRowStyle(PDFTreeView::RowStyle::Outline);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    // More items are selected by Ctrl / Shift (as the old Editor): Delete removes
    // and a drag moves all of them, the other commands act on the current item
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setAccessibleName(tr("Outline"));

    // The editing (step 4): the inline editor is opened only by the panel (F2, a
    // new item), the drop is performed by the panel
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    PDFAppPanelWidgets::prepareView(m_tree);
    m_tree->setItemDelegate(new PDFAppOutlineItemDelegate(m_tree));
    layout->addWidget(m_tree, 1);

    m_model = new PDFAppOutlineItemModel(this);
    m_filterModel = new PDFAppOutlineFilterModel(this);
    m_filterModel->setSourceModel(m_model);
    m_tree->setModel(m_filterModel);

    connect(m_searchField, &QLineEdit::textChanged, this, &PDFAppOutlinePanel::onSearchTextChanged);
    connect(m_tree, &QAbstractItemView::clicked, this, &PDFAppOutlinePanel::onItemClicked);
    connect(m_tree, &QAbstractItemView::activated, this, &PDFAppOutlinePanel::onItemClicked);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &PDFAppOutlinePanel::onContextMenuRequested);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppOutlinePanel::onCurrentIndexChanged);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppOutlinePanel::onSelectionChanged);
    if (QAbstractItemDelegate* delegate = m_tree->itemDelegate())
    {
        connect(delegate, &QAbstractItemDelegate::closeEditor, this, &PDFAppOutlinePanel::onEditorClosed);
    }
    m_tree->installEventFilter(this);
    m_tree->viewport()->installEventFilter(this);

    // F2 (outline.rename, scope Panel): the key works only in the panel
    m_renameAction = new QAction(this);
    m_renameAction->setObjectName(QStringLiteral("panel:") + PDFAppCommands::outlineRename());
    m_renameAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_renameAction.data(), &QAction::triggered, this, &PDFAppOutlinePanel::onRenameTriggered);
    addAction(m_renameAction);

    updateTexts();
    updateEditingState();
}

PDFAppOutlinePanel::~PDFAppOutlinePanel()
{
    // The children are deleted by the base class, when the members of the panel are
    // gone: an inline editor losing its focus there closes itself - its signals do
    // not reach the panel any more
    if (m_tree)
    {
        m_tree->disconnect(this);
        if (m_tree->selectionModel())
        {
            m_tree->selectionModel()->disconnect(this);
        }
        if (m_tree->itemDelegate())
        {
            m_tree->itemDelegate()->disconnect(this);
        }
        m_tree->removeEventFilter(this);
        m_tree->viewport()->removeEventFilter(this);
    }
    if (m_searchField)
    {
        m_searchField->disconnect(this);
    }
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView)
    {
        m_connectedView->disconnect(this);
    }
    if (m_connectedEditor)
    {
        m_connectedEditor->disconnect(this);

        // The editor forgets the item of the panel; a creation waiting for its
        // title ends (its temporary item goes with the panel). The creation ends
        // from the event loop: it may show another panel, which the sidebar being
        // destroyed (it deletes its panels) cannot do now.
        m_pendingItem = nullptr;
        m_connectedEditor->setCurrentItem({});
        if (m_connectedEditor && m_connectedEditor->isCreationPending())
        {
            QMetaObject::invokeMethod(m_connectedEditor.data(), &PDFAppOutlineEditor::cancelCreation, Qt::QueuedConnection);
        }
    }
}

QString PDFAppOutlinePanel::getId() const
{
    return PDFAppDocumentPanelIds::outline();
}

QString PDFAppOutlinePanel::getTitle() const
{
    return tr("Outline");
}

QString PDFAppOutlinePanel::getSubtitle() const
{
    // 195:1085 "86 items"
    const int count = int(m_items.size());
    return count == 0 ? QString() : (count == 1 ? tr("%Ln item", nullptr, count) : tr("%Ln items", nullptr, count));
}

QString PDFAppOutlinePanel::getIconName() const
{
    return QStringLiteral("list");
}

QWidget* PDFAppOutlinePanel::createHeaderWidget(QWidget* parent)
{
    // outline-new 195:1485 (Sidebar tab 32, icon plus) at the trailing side of the header
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("outlineHeader"));
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_newItemButton = PDFAppPanelWidgets::createHeaderButton(widget, QStringLiteral("plus"), tr("New outline item"), QStringLiteral("outlineNew"));
    layout->addWidget(m_newItemButton);
    connect(m_newItemButton.data(), &QToolButton::clicked, this, &PDFAppOutlinePanel::onNewItemClicked);
    updateTexts();
    updateEditingState();
    return widget;
}

void PDFAppOutlinePanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView && m_connectedView != view)
    {
        m_connectedView->disconnect(this);
    }

    PDFAppOutlineEditor* editor = view ? view->getOutlineEditor() : nullptr;
    if (m_connectedEditor && m_connectedEditor != editor)
    {
        m_connectedEditor->disconnect(this);
    }

    const bool isNewView = view && m_connectedView != view;
    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppOutlinePanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppOutlinePanel::onDocumentDestroyed);
    }
    if (isNewView)
    {
        connect(view, &PDFAppDocumentView::currentPageChanged, this, &PDFAppOutlinePanel::onCurrentPageChanged);
        PDFAppContext* context = view->getContext();
        if (context && context->getNotifier())
        {
            connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppOutlinePanel::onDatabaseChanged);
        }
    }
    if (editor && m_connectedEditor != editor)
    {
        connect(editor, &PDFAppOutlineEditor::stateChanged, this, &PDFAppOutlinePanel::onEditingStateChanged);
    }
    m_connectedDocument = document;
    m_connectedView = view;
    m_connectedEditor = editor;
    updateRenameShortcut();
    rebuild();
}

bool PDFAppOutlinePanel::hasOutline(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    const QSharedPointer<PDFOutlineItem> root = document->getCatalog()->getOutlineRootPtr();
    return root && root->getChildCount() > 0;
}

void PDFAppOutlinePanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // Only a change of the outline (or of the whole document) gives new items; the
    // other changes (annotations, forms) keep the items and their state
    PDFAppDocument* document = getDocument();
    const bool isOutlineChanged = modified.hasReset() || modified.hasFlag(PDFModifiedDocument::Outline) || modified.getFlags() == PDFModifiedDocument::None;
    if (!isOutlineChanged && document && document->isReady() && m_document && m_model && m_model->rowCount() > 0 && !m_pendingItem)
    {
        m_document = document->getDocument();
        m_outlineRoot = m_document ? m_document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
        if (m_model)
        {
            m_model->setDocumentKey(getDocumentKey());
        }
        updateEditingState();
        return;
    }
    rebuild();
}

void PDFAppOutlinePanel::onDocumentDestroyed()
{
    // The document is not whole any more: the items are cleared
    m_connectedDocument = nullptr;
    rebuild();
}

QString PDFAppOutlinePanel::getDocumentKey() const
{
    // The id of the document and the shown version (the panel holds it, so its
    // address is not used by another version meanwhile)
    const PDFAppDocument* document = getDocument();
    return document && m_document ? document->getId() + QLatin1Char(':') + QString::number(quintptr(m_document.data()), 16) : QString();
}

PDFAppOutlineEditor* PDFAppOutlinePanel::getEditor() const
{
    PDFAppDocumentView* view = getView();
    return view ? view->getOutlineEditor() : nullptr;
}

bool PDFAppOutlinePanel::isEditingAllowed() const
{
    const PDFAppOutlineEditor* editor = getEditor();
    return editor && editor->isEditingAllowed();
}

void PDFAppOutlinePanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    QSharedPointer<PDFOutlineItem> root = pointer ? pointer->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (pointer == m_document && root == m_outlineRoot && m_model && m_model->rowCount() > 0 && !m_pendingItem)
    {
        // The same outline (the document set again): the items and the selection stay
        updateEditingState();
        return;
    }

    // The state, which is kept: the expanded items and the item the user has
    // activated, known by their objects (by their order, when the outline cannot
    // be edited and the number of the items is the same)
    std::set<PDFObjectReference> expandedReferences;
    std::set<size_t> expandedIndices;
    PDFObjectReference activatedReference;
    size_t activatedIndex = std::numeric_limits<size_t>::max();
    PDFObjectReference selectedReference;
    size_t selectedIndex = std::numeric_limits<size_t>::max();
    const size_t oldCount = m_items.size();
    const QModelIndex selected = getCurrentItem();
    const QModelIndex selectedSource = selected.isValid() && m_filterModel && selected.model() == m_filterModel ? m_filterModel->mapToSource(selected) : QModelIndex();
    if (m_model && m_filterModel && m_tree)
    {
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            const QModelIndex sourceIndex = m_model->indexFromItem(m_items[i].item);
            const QModelIndex index = m_filterModel->mapFromSource(sourceIndex);
            if (index.isValid() && m_tree->isExpanded(index))
            {
                expandedReferences.insert(m_items[i].reference);
                expandedIndices.insert(i);
            }
            if (m_activatedItem.isValid() && QModelIndex(m_activatedItem) == sourceIndex)
            {
                activatedReference = m_items[i].reference;
                activatedIndex = i;
            }
            if (selectedSource.isValid() && selectedSource == sourceIndex)
            {
                selectedReference = m_items[i].reference;
                selectedIndex = i;
            }
        }
    }

    // A temporary item of a creation goes with the old items (the editor ends the
    // creation, when the document changes)
    m_pendingItem = nullptr;
    m_editedIndex = QPersistentModelIndex();
    m_isEditedPending = false;
    m_activatedItem = QPersistentModelIndex();

    m_items.clear();
    if (m_model)
    {
        m_model->clear();
    }
    m_document = pointer;
    m_outlineRoot = root;

    const bool isEditable = isEditingAllowed();
    if (pointer && root && m_model)
    {
        // The objects of the items, when the outline can be edited (the same order)
        const std::vector<PDFObjectReference> references = PDFAppAnnotationOperations::getOutlineItemReferences(pointer.data());

        // The tree is walked without recursion (an outline may be deep)
        const PDFCatalog* catalog = pointer->getCatalog();
        struct Entry
        {
            const PDFOutlineItem* outlineItem = nullptr;
            QStandardItem* parent = nullptr;
        };

        std::vector<Entry> stack;
        for (size_t i = root->getChildCount(); i > 0; --i)
        {
            stack.push_back({ root->getChild(i - 1), m_model->invisibleRootItem() });
        }

        while (!stack.empty() && int(m_items.size()) < MAXIMUM_ITEMS)
        {
            const Entry entry = stack.back();
            stack.pop_back();
            if (!entry.outlineItem)
            {
                continue;
            }

            Item item;
            item.item = new QStandardItem(PDFAppOutlinePanelHelper::sanitizeTitle(entry.outlineItem->getTitle()));
            PDFAppOutlinePanelHelper::setItemFlags(item.item, isEditable);
            item.page = PDFAppPanelWidgets::getActionPage(catalog, entry.outlineItem->getAction());
            item.top = PDFAppOutlinePanelHelper::getActionTop(catalog, entry.outlineItem->getAction());
            if (m_items.size() < references.size())
            {
                item.reference = references[m_items.size()];
            }
            item.item->setData(qlonglong(item.page), PageRole);
            item.item->setToolTip(item.item->text());
            if (entry.outlineItem->isFontBold() || entry.outlineItem->isFontItalics())
            {
                QFont font = m_tree ? m_tree->font() : QFont();
                font.setBold(entry.outlineItem->isFontBold());
                font.setItalic(entry.outlineItem->isFontItalics());
                item.item->setFont(font);
            }
            entry.parent->appendRow(item.item);
            m_items.push_back(item);

            for (size_t i = entry.outlineItem->getChildCount(); i > 0; --i)
            {
                stack.push_back({ entry.outlineItem->getChild(i - 1), item.item });
            }
        }
    }

    if (m_model)
    {
        m_model->setDocumentKey(getDocumentKey());
    }

    if (m_searchField && !m_searchField->text().isEmpty())
    {
        setFilterText(m_searchField->text());
    }

    // The kept state
    const bool hasReferences = !m_items.empty() && m_items.front().reference.isValid();
    QModelIndex selectedIndexAfter;
    if (m_model && m_filterModel && m_tree)
    {
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            const bool isKnown = hasReferences ? m_items[i].reference.isValid() : (oldCount == m_items.size());
            const bool wasExpanded = hasReferences ? expandedReferences.count(m_items[i].reference) > 0 : expandedIndices.count(i) > 0;
            const QModelIndex sourceIndex = m_model->indexFromItem(m_items[i].item);
            if (isKnown && wasExpanded)
            {
                m_tree->expand(m_filterModel->mapFromSource(sourceIndex));
            }
            const bool wasActivated = hasReferences ? (activatedReference.isValid() && m_items[i].reference == activatedReference) : (oldCount == m_items.size() && activatedIndex == i);
            if (wasActivated)
            {
                m_activatedItem = QPersistentModelIndex(sourceIndex);
            }
            const bool wasSelected = hasReferences ? (selectedReference.isValid() && m_items[i].reference == selectedReference) : (oldCount == m_items.size() && selectedIndex == i);
            if (wasSelected)
            {
                selectedIndexAfter = m_filterModel->mapFromSource(sourceIndex);
            }
        }
    }

    if (!m_itemToSelect.empty())
    {
        // The item changed by the editor
        const OutlinePath path = m_itemToSelect;
        m_itemToSelect.clear();
        selectItem(getItemIndex(path));
    }
    else if (selectedIndexAfter.isValid())
    {
        // The current item stays (undo, redo, a change of another view)
        selectItem(selectedIndexAfter);
    }
    else
    {
        selectCurrentItem();
    }

    // The new model has no current item, when none has been selected (the reset of
    // the model does not announce it): the editor forgets the item of the old one
    PDFAppOutlineEditor* editor = getEditor();
    if (editor && m_tree && !m_tree->currentIndex().isValid() && !editor->getCurrentItem().empty())
    {
        QPointer<PDFAppOutlinePanel> guard(this);
        editor->setCurrentItem({});
        if (!guard)
        {
            return;
        }
    }
    updateEditingState();
    Q_EMIT titleChanged();
}

void PDFAppOutlinePanel::setFilterText(const QString& text)
{
    if (!m_filterModel || !m_tree)
    {
        return;
    }

    // A text with * or ? is a wildcard pattern (as the old Editor), else a part
    // of a title; the pattern is bounded (the wildcard conversion is linear)
    const QString filter = text.trimmed().left(PDFAppAnnotationOperations::MAXIMUM_OUTLINE_TITLE_LENGTH);
    if (filter.contains(QLatin1Char('*')) || filter.contains(QLatin1Char('?')))
    {
        // A title is no path: * and ? match also / and \ (the default wildcard
        // conversion of Qt stops at the separators of the paths)
        m_filterModel->setFilterRegularExpression(QRegularExpression::fromWildcard(filter, Qt::CaseInsensitive,
                                                                                   QRegularExpression::UnanchoredWildcardConversion | QRegularExpression::NonPathWildcardConversion));
    }
    else
    {
        m_filterModel->setFilterFixedString(filter);
    }
    if (!filter.isEmpty())
    {
        // The found items are shown with their parents
        m_tree->expandAll();
    }
    selectCurrentItem();
}

void PDFAppOutlinePanel::onSearchTextChanged(const QString& text)
{
    setFilterText(text);
}

void PDFAppOutlinePanel::onCurrentPageChanged(PDFInteger pageIndex)
{
    // Only a new page moves the selection (the view reports its page again, while
    // it takes a new version of the document - the selection of the user stays)
    if (pageIndex < 0 || pageIndex == m_lastPageIndex)
    {
        return;
    }
    selectCurrentItem();
}

void PDFAppOutlinePanel::selectCurrentItem()
{
    PDFAppDocumentView* view = getView();
    if (!m_tree || !m_filterModel || !m_model || m_pendingItem || m_editedIndex.isValid())
    {
        return;
    }

    const PDFInteger currentPage = view ? view->getCurrentPageIndex() : -1;
    m_lastPageIndex = currentPage;
    std::vector<ItemPosition> positions;
    positions.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        positions.push_back(ItemPosition{ item.page, item.top });
    }
    const int bestIndex = findCurrentItemIndex(positions, currentPage, PDFAppOutlineEditor::getViewTop(getView(), currentPage).value_or(std::numeric_limits<PDFReal>::infinity()));
    const QStandardItem* best = bestIndex >= 0 ? m_items[size_t(bestIndex)].item : nullptr;
    const PDFInteger bestPage = bestIndex >= 0 ? m_items[size_t(bestIndex)].page : -1;

    QModelIndex index = best ? m_filterModel->mapFromSource(m_model->indexFromItem(best)) : QModelIndex();

    // Several items of the same page: the item the user has activated stays
    // selected, while the current page is its page - a click on an item is not
    // replaced by another item of its page, when the view reaches it. An item
    // selected by the view itself follows the view (the next item of the page).
    const QModelIndex selected = getCurrentItem();
    const bool isActivatedSelected = m_activatedItem.isValid() && selected.isValid() && selected.model() == m_filterModel &&
                                     m_filterModel->mapToSource(selected) == QModelIndex(m_activatedItem);
    if (best && isActivatedSelected && selected.data(PageRole).toLongLong() == bestPage)
    {
        index = selected;
    }
    else if (!best || m_activatedItem.data(PageRole).toLongLong() != bestPage)
    {
        // The view has left the page of the activated item
        m_activatedItem = QPersistentModelIndex();
    }

    const bool wasSelecting = m_isSelecting;
    m_isSelecting = true;
    if (index.isValid())
    {
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        {
            m_tree->expand(parent);
        }
        m_tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
    else
    {
        // No item is highlighted: no item is current either (the commands and Del
        // act on the current item, which the user must see)
        m_tree->selectionModel()->clearSelection();
        m_tree->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }
    m_isSelecting = wasSelecting;
}

void PDFAppOutlinePanel::selectItem(const QModelIndex& index)
{
    if (!m_tree || !m_filterModel || !index.isValid() || index.model() != m_filterModel)
    {
        return;
    }

    const bool wasSelecting = m_isSelecting;
    m_isSelecting = true;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
    {
        m_tree->expand(parent);
    }
    m_tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
    m_isSelecting = wasSelecting;

    // The item stays selected, while the view shows its page
    m_activatedItem = QPersistentModelIndex(m_filterModel->mapToSource(index));
}

QModelIndex PDFAppOutlinePanel::getCurrentItem() const
{
    if (!m_tree || !m_tree->selectionModel())
    {
        return QModelIndex();
    }

    const QModelIndexList selected = m_tree->selectionModel()->selectedIndexes();
    return selected.isEmpty() ? QModelIndex() : selected.front();
}

QAbstractItemModel* PDFAppOutlinePanel::getViewModel() const
{
    return m_filterModel.data();
}

void PDFAppOutlinePanel::onItemClicked(const QModelIndex& index)
{
    // Ctrl / Shift + click changes the selection of more items: the view stays (the
    // page of the item would select one item of the page again - selectCurrentItem)
    const Qt::KeyboardModifiers selectionModifiers = QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!m_isSelecting && selectionModifiers == Qt::NoModifier)
    {
        activateItem(index);
    }
}

void PDFAppOutlinePanel::activateItem(const QModelIndex& index)
{
    PDFAppDocumentView* view = getView();
    if (!index.isValid() || !view || index.model() != m_filterModel)
    {
        return;
    }

    // The temporary item of a creation has no destination yet
    const QModelIndex sourceIndex = m_filterModel->mapToSource(index);
    if (m_pendingItem && m_model && m_model->itemFromIndex(sourceIndex) == m_pendingItem)
    {
        return;
    }

    const PDFInteger page = index.data(PageRole).toLongLong();
    m_activatedItem = QPersistentModelIndex(sourceIndex);
    if (page >= 0)
    {
        view->goToPage(page);
    }
    else if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
    {
        statusBar->showMessage(tr("The item has no page in this document."), 5000);
    }
}

// ---------------------------------------------------------------------- editing

QModelIndex PDFAppOutlinePanel::getSourceIndex(const OutlinePath& path) const
{
    if (!m_model || path.empty())
    {
        return QModelIndex();
    }

    QModelIndex index;
    for (size_t row : path)
    {
        if (row >= size_t(m_model->rowCount(index)))
        {
            return QModelIndex();
        }
        index = m_model->index(int(row), 0, index);
    }
    return index;
}

QModelIndex PDFAppOutlinePanel::getItemIndex(const OutlinePath& path) const
{
    const QModelIndex sourceIndex = getSourceIndex(path);
    return sourceIndex.isValid() && m_filterModel ? m_filterModel->mapFromSource(sourceIndex) : QModelIndex();
}

PDFAppOutlinePanel::OutlinePath PDFAppOutlinePanel::getItemPath(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return {};
    }
    if (m_filterModel && index.model() == m_filterModel)
    {
        return PDFAppOutlineItemModel::getPath(m_filterModel->mapToSource(index));
    }
    if (m_model && index.model() == m_model)
    {
        return PDFAppOutlineItemModel::getPath(index);
    }
    return {};
}

void PDFAppOutlinePanel::onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);

    // The temporary item of a creation is not an item of the document
    PDFAppOutlineEditor* editor = getEditor();
    if (!editor || !m_filterModel || !m_model)
    {
        return;
    }
    const QModelIndex sourceIndex = current.isValid() ? m_filterModel->mapToSource(current) : QModelIndex();
    if (m_pendingItem && m_model->itemFromIndex(sourceIndex) == m_pendingItem)
    {
        return;
    }
    editor->setCurrentItem(PDFAppOutlineItemModel::getPath(sourceIndex));
}

void PDFAppOutlinePanel::onSelectionChanged()
{
    PDFAppOutlineEditor* editor = getEditor();
    if (!editor || !m_tree || !m_filterModel || !m_model)
    {
        return;
    }

    // The temporary item of a creation is not an item of the document
    std::vector<OutlinePath> items;
    for (const QModelIndex& index : m_tree->selectionModel()->selectedRows())
    {
        const QModelIndex sourceIndex = m_filterModel->mapToSource(index);
        if (sourceIndex.isValid() && !(m_pendingItem && m_model->itemFromIndex(sourceIndex) == m_pendingItem))
        {
            items.push_back(PDFAppOutlineItemModel::getPath(sourceIndex));
        }
    }
    editor->setSelectedItems(items);
}

void PDFAppOutlinePanel::beginPendingItem()
{
    PDFAppOutlineEditor* editor = getEditor();
    if (!editor || !editor->isCreationPending() || !m_model || !m_tree || !m_filterModel)
    {
        return;
    }

    // A temporary item of an earlier creation goes away (availabilityChanged - a
    // slot may delete the tab and this panel)
    QPointer<PDFAppOutlinePanel> endGuard(this);
    endPendingItem();
    if (!endGuard)
    {
        return;
    }

    // The filter would hide the new item
    if (m_searchField && !m_searchField->text().isEmpty())
    {
        m_searchField->clear();
    }

    const OutlinePath parentPath = editor->getPendingParent();
    QStandardItem* parent = parentPath.empty() ? m_model->invisibleRootItem() : m_model->itemFromIndex(getSourceIndex(parentPath));
    if (!parent)
    {
        editor->cancelCreation();
        return;
    }

    m_pendingItem = new QStandardItem(editor->getPendingTitle().isEmpty() ? PDFAppOutlineEditor::getDefaultTitle() : editor->getPendingTitle());
    m_pendingItem->setEditable(true);
    m_pendingItem->setDragEnabled(false);
    m_pendingItem->setDropEnabled(false);
    m_pendingItem->setData(qlonglong(-1), PageRole);
    parent->insertRow(int(qMin(editor->getPendingIndex(), size_t(parent->rowCount()))), m_pendingItem);

    const QModelIndex index = m_filterModel->mapFromSource(m_pendingItem->index());
    const bool wasSelecting = m_isSelecting;
    m_isSelecting = true;
    for (QModelIndex current = index.parent(); current.isValid(); current = current.parent())
    {
        m_tree->expand(current);
    }
    m_tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
    m_isSelecting = wasSelecting;

    // The tab of the panel is shown also for a document without an outline (a
    // slot may delete the tab and this panel)
    QPointer<PDFAppOutlinePanel> guard(this);
    Q_EMIT availabilityChanged();
    if (!guard)
    {
        return;
    }

    // The editor opens from the event loop (a popup, which is closing now - the
    // Insert panel -, would take its focus)
    QMetaObject::invokeMethod(this, &PDFAppOutlinePanel::openPendingEditor, Qt::QueuedConnection);
}

void PDFAppOutlinePanel::openPendingEditor()
{
    if (!m_pendingItem || !m_tree || !m_filterModel)
    {
        return;
    }

    const QModelIndex index = m_filterModel->mapFromSource(m_pendingItem->index());
    if (!index.isValid())
    {
        return;
    }

    m_tree->setFocus(Qt::OtherFocusReason);
    m_editedIndex = QPersistentModelIndex(m_pendingItem->index());
    m_isEditedPending = true;
    m_tree->edit(index);
    if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(m_tree->indexWidget(index)))
    {
        lineEdit->selectAll();
    }
}

void PDFAppOutlinePanel::endPendingItem()
{
    if (!m_pendingItem || !m_model)
    {
        m_pendingItem = nullptr;
        return;
    }

    QStandardItem* item = m_pendingItem;
    m_pendingItem = nullptr;
    if (m_isEditedPending)
    {
        m_isEditedPending = false;
        m_editedIndex = QPersistentModelIndex();
    }

    QStandardItem* parent = item->parent() ? item->parent() : m_model->invisibleRootItem();
    parent->removeRow(item->row());
    Q_EMIT availabilityChanged();
}

bool PDFAppOutlinePanel::editItem(const OutlinePath& path)
{
    if (!m_tree || !m_filterModel || !m_model || !isEditingAllowed() || m_pendingItem)
    {
        return false;
    }

    // An item hidden by the filter is shown
    QModelIndex index = getItemIndex(path);
    if (!index.isValid() && getSourceIndex(path).isValid() && m_searchField && !m_searchField->text().isEmpty())
    {
        m_searchField->clear();
        index = getItemIndex(path);
    }
    if (!index.isValid() || !index.flags().testFlag(Qt::ItemIsEditable))
    {
        return false;
    }

    selectItem(index);
    m_tree->setFocus(Qt::OtherFocusReason);
    m_editedIndex = QPersistentModelIndex(m_filterModel->mapToSource(index));
    m_isEditedPending = false;
    m_tree->edit(index);

    QWidget* editor = m_tree->indexWidget(index);
    if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor))
    {
        lineEdit->selectAll();
    }
    if (!editor)
    {
        m_editedIndex = QPersistentModelIndex();
        return false;
    }
    return true;
}

void PDFAppOutlinePanel::finishInlineEditing()
{
    if (!m_tree || !m_filterModel || !m_editedIndex.isValid() || m_isEditedPending)
    {
        return;
    }

    // The view closes an editor without the signals of the delegate, when its
    // current item is changed by the program (the temporary item of a creation):
    // the title would be lost. The delegate commits and closes it as by Enter
    // (onEditorClosed takes the result), the title is written now.
    const QModelIndex index = m_filterModel->mapFromSource(m_editedIndex);
    QWidget* editor = index.isValid() ? m_tree->indexWidget(index) : nullptr;
    QAbstractItemDelegate* delegate = index.isValid() ? m_tree->itemDelegateForIndex(index) : nullptr;
    if (!editor || !delegate)
    {
        return;
    }

    Q_EMIT delegate->commitData(editor);
    Q_EMIT delegate->closeEditor(editor, QAbstractItemDelegate::SubmitModelCache);
    if (m_editResult.isValid)
    {
        onEditFinishedQueued();
    }
}

void PDFAppOutlinePanel::onEditorClosed(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
{
    Q_UNUSED(editor);
    if (!m_editedIndex.isValid())
    {
        return;
    }

    // The data of the editor are in the model already (commitData comes first);
    // the change of the document runs from the event loop - not inside of the
    // editing of the view
    EditResult result;
    result.isValid = true;
    result.isPending = m_isEditedPending;
    result.isAccepted = hint != QAbstractItemDelegate::RevertModelCache;
    result.title = m_editedIndex.data(Qt::DisplayRole).toString();
    result.path = PDFAppOutlineItemModel::getPath(m_editedIndex);
    m_editedIndex = QPersistentModelIndex();
    m_isEditedPending = false;
    m_editResult = result;
    QMetaObject::invokeMethod(this, &PDFAppOutlinePanel::onEditFinishedQueued, Qt::QueuedConnection);
}

void PDFAppOutlinePanel::onEditFinishedQueued()
{
    const EditResult result = m_editResult;
    m_editResult = EditResult();
    PDFAppOutlineEditor* editor = getEditor();
    if (!result.isValid || !editor)
    {
        return;
    }

    // Nothing of the panel is used after a change of the document (its slots may
    // delete the tab)
    if (result.isPending)
    {
        if (editor->isCreationPending())
        {
            if (result.isAccepted)
            {
                editor->finishCreation(result.title);
            }
            else
            {
                editor->cancelCreation();
            }
        }
        return;
    }

    QPointer<PDFAppOutlinePanel> guard(this);
    const bool isRenamed = result.isAccepted && editor->renameItem(result.path, result.title);
    if (!guard || isRenamed)
    {
        return;
    }

    // The title of the document again (the typed text has not been written)
    if (QStandardItem* item = m_model ? m_model->itemFromIndex(getSourceIndex(result.path)) : nullptr)
    {
        const PDFOutlineItem* outlineItem = m_outlineRoot.data();
        for (size_t row : result.path)
        {
            outlineItem = outlineItem && row < outlineItem->getChildCount() ? outlineItem->getChild(row) : nullptr;
        }
        if (outlineItem)
        {
            item->setText(PDFAppOutlinePanelHelper::sanitizeTitle(outlineItem->getTitle()));
        }
    }
}

PDFAppOutlinePanel::DropPosition PDFAppOutlinePanel::getDropPosition(const QRect& rect, const QPoint& point)
{
    if (!rect.isValid() || !rect.contains(point))
    {
        return DropPosition::OnViewport;
    }

    const int quarter = qMax(1, rect.height() / 4);
    if (point.y() < rect.top() + quarter)
    {
        return DropPosition::Above;
    }
    if (point.y() > rect.bottom() - quarter)
    {
        return DropPosition::Below;
    }
    return DropPosition::OnItem;
}

PDFAppOutlinePanel::DropRequest PDFAppOutlinePanel::getDropRequest(const OutlinePath& item, const QModelIndex& target, DropPosition position) const
{
    DropRequest request;
    if (item.empty() || !m_model)
    {
        return request;
    }

    request.isValid = true;
    request.item = item;
    request.index = size_t(m_model->rowCount());
    const OutlinePath targetPath = getItemPath(target);
    if (!targetPath.empty())
    {
        switch (position)
        {
            case DropPosition::Above:
            case DropPosition::Below:
                request.parent = targetPath;
                request.parent.pop_back();
                request.index = targetPath.back() + (position == DropPosition::Below ? 1 : 0);
                break;

            case DropPosition::OnItem:
                request.parent = targetPath;
                request.index = size_t(m_model->rowCount(getSourceIndex(targetPath)));
                break;

            case DropPosition::OnViewport:
                break;
        }
    }
    return request;
}

bool PDFAppOutlinePanel::dropItem(const QModelIndex& source, const QModelIndex& target, DropPosition position)
{
    PDFAppOutlineEditor* editor = getEditor();
    const DropRequest request = getDropRequest(getItemPath(source), target, position);
    return editor && request.isValid && editor->moveItem(request.item, request.parent, request.index);
}

void PDFAppOutlinePanel::onDropQueued()
{
    const DropRequest request = m_dropRequest;
    m_dropRequest = DropRequest();
    PDFAppOutlineEditor* editor = getEditor();
    if (request.isValid && editor)
    {
        if (request.items.size() > 1)
        {
            editor->moveItems(request.items, request.parent, request.index);
        }
        else
        {
            editor->moveItem(request.item, request.parent, request.index);
        }
    }
}

// ---------------------------------------------------------------------- menus and keys

void PDFAppOutlinePanel::requestCommand(const QString& commandId)
{
    if (PDFAppDocumentView* view = getView())
    {
        view->requestCommand(commandId);
    }
}

void PDFAppOutlinePanel::onNewItemClicked()
{
    requestCommand(PDFAppCommands::outlineNewItem());
}

void PDFAppOutlinePanel::onRenameTriggered()
{
    if (PDFAppOutlineEditor* editor = getEditor())
    {
        editor->renameCurrentItem();
    }
}

QMenu* PDFAppOutlinePanel::createContextMenu(const QModelIndex& index)
{
    if (!m_tree || (index.isValid() && index.model() != m_filterModel))
    {
        return nullptr;
    }

    // The commands act on the current item: the item of the menu becomes current.
    // An item of more selected items keeps them (Delete of the menu removes all of
    // them, as the key Delete)
    QItemSelectionModel* selectionModel = m_tree->selectionModel();
    if (index.isValid() && selectionModel && selectionModel->isSelected(index) && selectionModel->selectedRows().size() > 1)
    {
        const bool wasSelecting = m_isSelecting;
        m_isSelecting = true;
        selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        m_isSelecting = wasSelecting;
    }
    else if (index.isValid())
    {
        selectItem(index);
    }

    QMenu* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("outlineContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    PDFAppOutlineEditor* editor = getEditor();
    const bool hasItem = index.isValid() && !(m_pendingItem && m_model->itemFromIndex(m_filterModel->mapToSource(index)) == m_pendingItem);
    if (hasItem)
    {
        QAction* goTo = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("corner-down-right")), tr("Go to Page"));
        goTo->setObjectName(QStringLiteral("outlineGoTo"));
        goTo->setEnabled(index.data(PageRole).toLongLong() >= 0);
        goTo->setProperty(PDFAppOutlinePanelHelper::COMMAND_PROPERTY, QString());
        connect(goTo, &QAction::triggered, this, &PDFAppOutlinePanel::onMenuActionTriggered);
    }

    if (editor && isEditingAllowed())
    {
        const PDFAppCommandRegistry* registry = getView() && getView()->getContext() ? getView()->getContext()->getCommandRegistry() : nullptr;
        struct MenuCommand
        {
            QString commandId;
            QString text;
            bool isEnabled = true;
            bool isCheckable = false;
            bool isChecked = false;
            bool isSeparatorBefore = false;
            bool isDestructive = false;
        };

        const std::vector<MenuCommand> commands =
        {
            { PDFAppCommands::outlineNewItem(), tr("New Item"), true, false, false, hasItem, false },
            { PDFAppCommands::outlineRename(), tr("Rename"), hasItem, false, false, false, false },
            { PDFAppCommands::outlineSetTarget(), tr("Set Target"), hasItem, false, false, false, false },
            { PDFAppCommands::outlineBold(), tr("Bold"), hasItem, true, hasItem && editor->isCurrentItemBold(), true, false },
            { PDFAppCommands::outlineItalic(), tr("Italic"), hasItem, true, hasItem && editor->isCurrentItemItalic(), false, false },
            { PDFAppCommands::outlineMoveUp(), tr("Move Up"), hasItem && editor->canMoveUp(), false, false, true, false },
            { PDFAppCommands::outlineMoveDown(), tr("Move Down"), hasItem && editor->canMoveDown(), false, false, false, false },
            { PDFAppCommands::outlineIndent(), tr("Indent"), hasItem && editor->canIndent(), false, false, false, false },
            { PDFAppCommands::outlineOutdent(), tr("Outdent"), hasItem && editor->canOutdent(), false, false, false, false },
            { PDFAppCommands::outlineDelete(), tr("Delete"), hasItem, false, false, true, true },
        };

        for (const MenuCommand& command : commands)
        {
            if (!hasItem && command.commandId != PDFAppCommands::outlineNewItem())
            {
                continue;
            }
            if (command.isSeparatorBefore)
            {
                menu->addSeparator();
            }

            if (command.commandId == PDFAppCommands::outlineSetTarget())
            {
                addSetTargetMenu(menu, registry);
                continue;
            }

            const PDFAppCommand* registered = registry ? registry->findCommand(command.commandId) : nullptr;
            QAction* action = menu->addAction(command.text);
            action->setObjectName(QStringLiteral("outlineMenu:") + command.commandId);
            if (registered && !registered->iconName.isEmpty() && !command.isCheckable)
            {
                action->setIcon(PDFThemeIcons::getIcon(registered->iconName));
            }
            action->setEnabled(command.isEnabled);
            action->setCheckable(command.isCheckable);
            action->setChecked(command.isChecked);
            action->setProperty(PDFAppOutlinePanelHelper::COMMAND_PROPERTY, command.commandId);
            if (command.isDestructive)
            {
                action->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
            }
            if (command.commandId == PDFAppCommands::outlineRename() && m_renameAction)
            {
                action->setShortcut(m_renameAction->shortcut());
                action->setShortcutContext(Qt::WidgetShortcut);
                action->setShortcutVisibleInContextMenu(true);
            }
            if (command.commandId == PDFAppCommands::outlineDelete())
            {
                // The key of the panel (1236:30150: "Del")
                action->setShortcut(QKeySequence(Qt::Key_Delete));
                action->setShortcutContext(Qt::WidgetShortcut);
                action->setShortcutVisibleInContextMenu(true);
            }
            connect(action, &QAction::triggered, this, &PDFAppOutlinePanel::onMenuActionTriggered);
        }
    }

    if (menu->actions().isEmpty())
    {
        delete menu;
        return nullptr;
    }
    return menu;
}

void PDFAppOutlinePanel::addSetTargetMenu(QMenu* menu, const PDFAppCommandRegistry* registry)
{
    PDFAppOutlineEditor* editor = getEditor();
    if (!menu || !editor)
    {
        return;
    }

    // Set Target (pdf4qt-document-outline-menu-target 1236:165602; the old Editor:
    // Named Destination, Fit Page, ..., XYZ, Inherit Zoom, Inherit Zoom for All
    // Chapters): the current view, the views of the current page, Area… and
    // Position… picked on a page as in the step 2 of Link to page, a named
    // destination of the document and the zoom of the reader; no icons
    QMenu* submenu = menu->addMenu(tr("Set Target"));
    submenu->setObjectName(QStringLiteral("outlineSetTargetMenu"));
    submenu->menuAction()->setObjectName(QStringLiteral("outlineSetTargetMenu"));

    struct TargetCommand
    {
        QString commandId;
        QString text;
        bool isEnabled = true;
        bool isCheckable = false;
        bool isChecked = false;
        bool isSeparatorBefore = false;
    };

    std::vector<TargetCommand> commands;
    commands.push_back({ PDFAppCommands::outlineSetTarget(), tr("Current View"), true, false, false, false });
    bool isFirstPicked = true;
    for (DestinationType type : PDFAppNotesOutlineCommandHandler::getTargets())
    {
        commands.push_back({ PDFAppNotesOutlineCommandHandler::getTargetCommandId(type), PDFAppOutlinePanelHelper::getTargetMenuText(type), true, false, false, isFirstPicked });
        isFirstPicked = false;
    }
    commands.push_back({ PDFAppCommands::outlineSetTargetNamed(), tr("Named Destination…"), true, false, false, true });
    commands.push_back({ PDFAppCommands::outlineKeepZoom(), tr("Keep the Reader's Zoom"), editor->isCurrentItemTargetPosition(), true, editor->isCurrentItemKeepingZoom(), true });
    commands.push_back({ PDFAppCommands::outlineKeepZoomAll(), tr("Keep the Reader's Zoom for All Items…"), editor->getItemsWithZoomCount() > 0, false, false, false });

    for (const TargetCommand& command : commands)
    {
        if (command.isSeparatorBefore)
        {
            submenu->addSeparator();
        }

        const PDFAppCommand* registered = registry ? registry->findCommand(command.commandId) : nullptr;
        QAction* action = submenu->addAction(command.text);
        action->setObjectName(QStringLiteral("outlineMenu:") + command.commandId);
        if (registered)
        {
            action->setToolTip(registered->getTitle());
        }
        action->setEnabled(command.isEnabled);
        action->setCheckable(command.isCheckable);
        action->setChecked(command.isChecked);
        action->setProperty(PDFAppOutlinePanelHelper::COMMAND_PROPERTY, command.commandId);
        connect(action, &QAction::triggered, this, &PDFAppOutlinePanel::onMenuActionTriggered);
    }
}

void PDFAppOutlinePanel::onMenuActionTriggered()
{
    // The actions do not identify themselves in the signal
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action)
    {
        return;
    }

    const QString commandId = action->property(PDFAppOutlinePanelHelper::COMMAND_PROPERTY).toString();
    if (commandId.isEmpty())
    {
        // Go to Page: the current item (the item of the menu)
        activateItem(getCurrentItem());
        return;
    }
    requestCommand(commandId);
}

void PDFAppOutlinePanel::onContextMenuRequested(const QPoint& position)
{
    if (!m_tree)
    {
        return;
    }

    QModelIndex index = m_tree->indexAt(position);
    QPoint globalPosition = m_tree->viewport()->mapToGlobal(position);
    if (!index.isValid() && !m_tree->viewport()->rect().contains(position))
    {
        // The keyboard: the menu of the current item
        index = m_tree->currentIndex();
        const QRect rect = index.isValid() ? m_tree->visualRect(index) : QRect();
        if (!rect.isValid())
        {
            return;
        }
        globalPosition = m_tree->viewport()->mapToGlobal(rect.center());
    }

    if (QMenu* menu = createContextMenu(index))
    {
        menu->popup(globalPosition);
    }
}

bool PDFAppOutlinePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (m_tree && watched == m_tree && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->modifiers() == Qt::NoModifier && isEditingAllowed())
        {
            if (keyEvent->key() == Qt::Key_Delete && m_tree->currentIndex().isValid())
            {
                requestCommand(PDFAppCommands::outlineDelete());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Insert)
            {
                requestCommand(PDFAppCommands::outlineNewItem());
                return true;
            }
        }
    }

    if (m_tree && watched == m_tree->viewport())
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                // Only an item of this version of the document is accepted
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (!isEditingAllowed() || !PDFAppOutlineItemModel::getDraggedPath(dropEvent->mimeData(), getDocumentKey()))
                {
                    dropEvent->ignore();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                const std::optional<OutlinePath> item = PDFAppOutlineItemModel::getDraggedPath(dropEvent->mimeData(), getDocumentKey());
                if (!item || !isEditingAllowed() || !m_model)
                {
                    dropEvent->ignore();
                    return true;
                }

                const QPoint position = dropEvent->position().toPoint();
                const QModelIndex target = m_tree->indexAt(position);
                const DropPosition place = target.isValid() ? getDropPosition(m_tree->visualRect(target), position) : DropPosition::OnViewport;

                m_dropRequest = getDropRequest(*item, target, place);
                m_dropRequest.items = PDFAppOutlineItemModel::getDraggedPaths(dropEvent->mimeData(), getDocumentKey());

                // The view does not remove the dragged rows (no action for it - the
                // items are owned by the model of the panel, which is built again from
                // the document); the document is changed from the event loop
                dropEvent->setDropAction(Qt::IgnoreAction);
                dropEvent->accept();
                QMetaObject::invokeMethod(this, &PDFAppOutlinePanel::onDropQueued, Qt::QueuedConnection);
                return true;
            }

            default:
                break;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppOutlinePanel::onEditingStateChanged()
{
    updateEditingState();
}

void PDFAppOutlinePanel::updateEditingState()
{
    // A locked document hides the editing (O20)
    const bool isEditable = isEditingAllowed();
    if (m_newItemButton)
    {
        m_newItemButton->setVisible(isEditable);
    }
    if (m_renameAction)
    {
        m_renameAction->setEnabled(isEditable);
    }
    if (m_tree)
    {
        const QAbstractItemView::DragDropMode mode = isEditable ? QAbstractItemView::DragDrop : QAbstractItemView::NoDragDrop;
        if (m_tree->dragDropMode() != mode)
        {
            m_tree->setDragDropMode(mode);
        }
    }
    for (const Item& item : m_items)
    {
        if (item.item && item.item->isEditable() != isEditable)
        {
            PDFAppOutlinePanelHelper::setItemFlags(item.item, isEditable);
        }
    }
}

void PDFAppOutlinePanel::updateRenameShortcut()
{
    PDFAppDocumentView* view = getView();
    PDFAppContext* context = view ? view->getContext() : nullptr;
    const PDFAppCommandRegistry* registry = context ? context->getCommandRegistry() : nullptr;
    if (!m_renameAction || !registry)
    {
        return;
    }

    // The shortcut of the user (an unreadable database leaves the default)
    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    if (context->getDatabase())
    {
        PDFDatabaseError error;
        userShortcuts = context->getShortcuts().getShortcuts(&error);
        if (error.isError())
        {
            userShortcuts.clear();
        }
    }
    m_renameAction->setShortcut(registry->getShortcut(PDFAppCommands::outlineRename(), userShortcuts));
}

void PDFAppOutlinePanel::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);
    if (topic == PDFShortcutRepository::getTopic())
    {
        updateRenameShortcut();
    }
}

void PDFAppOutlinePanel::updateTexts()
{
    if (m_searchField)
    {
        m_searchField->setPlaceholderText(tr("Search outline…"));
        m_searchField->setAccessibleName(tr("Search outline…"));
    }
    if (m_tree)
    {
        m_tree->setAccessibleName(tr("Outline"));
    }
    if (m_newItemButton)
    {
        m_newItemButton->setToolTip(tr("New outline item"));
        m_newItemButton->setAccessibleName(tr("New outline item"));
    }
    if (m_renameAction)
    {
        m_renameAction->setText(tr("Rename outline item"));
    }
}

void PDFAppOutlinePanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
        Q_EMIT titleChanged();
    }
}

QString PDFAppOutlinePanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::outline();
}

int PDFAppOutlinePanelFactory::getOrder() const
{
    return 10;
}

QString PDFAppOutlinePanelFactory::getTitle() const
{
    return PDFAppOutlinePanel::tr("Outline");
}

QString PDFAppOutlinePanelFactory::getIconName() const
{
    return QStringLiteral("list");
}

bool PDFAppOutlinePanelFactory::isAvailable(const PDFAppDocument* document) const
{
    // A new item of a document without an outline waits for its title in the panel
    return document && (PDFAppOutlinePanel::hasOutline(document->getDocument().data()) || PDFAppOutlineEditor::isCreationPending(document));
}

PDFAppDocumentPanel* PDFAppOutlinePanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppOutlinePanel(parent);
}

}   // namespace pdf
