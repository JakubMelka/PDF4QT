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
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the outline panel
class PDFAppOutlinePanelHelper
{
public:
    PDFAppOutlinePanelHelper() = delete;

    /// Longest title of an item shown
    static constexpr int MAXIMUM_TITLE_LENGTH = 1024;

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
};

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

PDFReal PDFAppOutlinePanel::getViewTop(PDFInteger currentPage) const
{
    PDFAppDocumentView* view = getView();
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    PDFWidget* pdfWidget = view ? view->getPdfWidget() : nullptr;
    QWidget* drawWidget = pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr;
    if (!proxy || !drawWidget || currentPage < 0)
    {
        return std::numeric_limits<PDFReal>::infinity();
    }

    // The top edge of the view (the middle, where a page lies in every layout)
    QPointF pagePoint;
    if (proxy->getPageUnderPoint(QPoint(drawWidget->width() / 2, 0), &pagePoint) == currentPage && qIsFinite(pagePoint.y()))
    {
        return pagePoint.y();
    }
    return std::numeric_limits<PDFReal>::infinity();
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
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setAccessibleName(tr("Outline"));
    PDFAppPanelWidgets::prepareView(m_tree);
    layout->addWidget(m_tree, 1);

    m_model = new QStandardItemModel(this);
    m_filterModel = new PDFAppOutlineFilterModel(this);
    m_filterModel->setSourceModel(m_model);
    m_tree->setModel(m_filterModel);

    connect(m_searchField, &QLineEdit::textChanged, this, &PDFAppOutlinePanel::onSearchTextChanged);
    connect(m_tree, &QAbstractItemView::clicked, this, &PDFAppOutlinePanel::onItemClicked);
    connect(m_tree, &QAbstractItemView::activated, this, &PDFAppOutlinePanel::onItemClicked);
}

PDFAppOutlinePanel::~PDFAppOutlinePanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView)
    {
        m_connectedView->disconnect(this);
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

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppOutlinePanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppOutlinePanel::onDocumentDestroyed);
    }
    if (view && m_connectedView != view)
    {
        connect(view, &PDFAppDocumentView::currentPageChanged, this, &PDFAppOutlinePanel::onCurrentPageChanged);
    }
    m_connectedDocument = document;
    m_connectedView = view;
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
    Q_UNUSED(modified);
    rebuild();
}

void PDFAppOutlinePanel::onDocumentDestroyed()
{
    // The document is not whole any more: the items are cleared
    m_connectedDocument = nullptr;
    rebuild();
}

void PDFAppOutlinePanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    QSharedPointer<PDFOutlineItem> root = pointer ? pointer->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (pointer == m_document && root == m_outlineRoot && m_model && m_model->rowCount() > 0)
    {
        // The same outline (another kind of a change): only the current item
        selectCurrentItem();
        return;
    }

    m_items.clear();
    if (m_model)
    {
        m_model->clear();
    }
    m_document = pointer;
    m_outlineRoot = root;

    if (pointer && root && m_model)
    {
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
            item.item->setEditable(false);
            item.page = PDFAppPanelWidgets::getActionPage(catalog, entry.outlineItem->getAction());
            item.top = PDFAppOutlinePanelHelper::getActionTop(catalog, entry.outlineItem->getAction());
            item.item->setData(qlonglong(item.page), PageRole);
            item.item->setToolTip(item.item->text());
            entry.parent->appendRow(item.item);
            m_items.push_back(item);

            for (size_t i = entry.outlineItem->getChildCount(); i > 0; --i)
            {
                stack.push_back({ entry.outlineItem->getChild(i - 1), item.item });
            }
        }
    }

    if (m_searchField && !m_searchField->text().isEmpty())
    {
        setFilterText(m_searchField->text());
    }
    selectCurrentItem();
    Q_EMIT titleChanged();
}

void PDFAppOutlinePanel::setFilterText(const QString& text)
{
    if (!m_filterModel || !m_tree)
    {
        return;
    }

    m_filterModel->setFilterFixedString(text.trimmed());
    if (!text.trimmed().isEmpty())
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
    Q_UNUSED(pageIndex);
    selectCurrentItem();
}

void PDFAppOutlinePanel::selectCurrentItem()
{
    PDFAppDocumentView* view = getView();
    if (!m_tree || !m_filterModel || !m_model)
    {
        return;
    }

    const PDFInteger currentPage = view ? view->getCurrentPageIndex() : -1;
    std::vector<ItemPosition> positions;
    positions.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        positions.push_back(ItemPosition{ item.page, item.top });
    }
    const int bestIndex = findCurrentItemIndex(positions, currentPage, getViewTop(currentPage));
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
        m_tree->selectionModel()->clearSelection();
    }
    m_isSelecting = wasSelecting;
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
    if (!m_isSelecting)
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

    const PDFInteger page = index.data(PageRole).toLongLong();
    m_activatedItem = QPersistentModelIndex(m_filterModel->mapToSource(index));
    if (page >= 0)
    {
        view->goToPage(page);
    }
    else if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
    {
        statusBar->showMessage(tr("The item has no page in this document."), 5000);
    }
}

void PDFAppOutlinePanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
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
    return document && PDFAppOutlinePanel::hasOutline(document->getDocument().data());
}

PDFAppDocumentPanel* PDFAppOutlinePanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppOutlinePanel(parent);
}

}   // namespace pdf
