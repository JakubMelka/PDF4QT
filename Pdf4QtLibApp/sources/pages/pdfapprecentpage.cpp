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

#include "pdfapprecentpage.h"
#include "pdfapplocale.h"
#include "pdfappfiledrop.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfrepositoryhelper.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentrows.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QBoxLayout>
#include <QCollator>
#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>

namespace pdf
{

/// Helpers of the page Recent
class PDFAppRecentPageHelper
{
public:
    PDFAppRecentPageHelper() = delete;

    static constexpr const char* PATH_PROPERTY = "pdf4qtPath";
    static constexpr const char* ID_PROPERTY = "pdf4qtId";
    static constexpr const char* LIST_SCROLL = "listScroll";

    /// Lowest history card beside the side column (the card scrolls its rows)
    static constexpr int MINIMUM_HISTORY_HEIGHT = 240;

    /// The sort and Clear history… are beside the search from this width
    static constexpr int SEARCH_ROW_THRESHOLD = 560;

    /// Order of the items of the sorts Name and Folder (the collator of the locale)
    class Comparator
    {
    public:
        explicit Comparator(const QCollator* collator, bool byFolder) :
            m_collator(collator),
            m_byFolder(byFolder)
        {

        }

        bool operator()(const PDFAppRecentItem& first, const PDFAppRecentItem& second) const
        {
            if (m_byFolder)
            {
                const int folderOrder = m_collator->compare(first.sortFolder, second.sortFolder);
                if (folderOrder != 0)
                {
                    return folderOrder < 0;
                }
            }
            return m_collator->compare(first.name, second.name) < 0;
        }

    private:
        const QCollator* m_collator;
        bool m_byFolder;
    };

    template<typename T>
    static QList<T*> toList(const QList<QPointer<T>>& pointers)
    {
        QList<T*> result;
        for (const QPointer<T>& pointer : pointers)
        {
            if (pointer)
            {
                result << pointer.data();
            }
        }
        return result;
    }

    template<typename T>
    static void deleteAll(QList<QPointer<T>>& pointers)
    {
        for (const QPointer<T>& pointer : std::as_const(pointers))
        {
            if (pointer)
            {
                pointer->hide();
                pointer->deleteLater();
            }
        }
        pointers.clear();
    }

    static PDFComponentPainter::RecentItemType toItemType(PDFAppRecentItem::Type type)
    {
        switch (type)
        {
            case PDFAppRecentItem::Type::Folder:
                return PDFComponentPainter::RecentItemType::Folder;
            case PDFAppRecentItem::Type::Workspace:
                return PDFComponentPainter::RecentItemType::Workspace;
            case PDFAppRecentItem::Type::File:
            case PDFAppRecentItem::Type::Header:
                break;
        }
        return PDFComponentPainter::RecentItemType::File;
    }

    /// Items sorted by the time (newest first) in groups by the day with the headers
    static std::vector<PDFAppRecentItem> groupByDay(std::vector<PDFAppRecentItem> items, qint64 now)
    {
        std::stable_sort(items.begin(), items.end(), &PDFAppRecentPageHelper::isNewer);

        std::vector<PDFAppRecentItem> result;
        result.reserve(items.size() + 4);
        std::optional<PDFAppHomeFormat::DateGroup> currentGroup;
        size_t headerIndex = 0;
        for (PDFAppRecentItem& item : items)
        {
            const PDFAppHomeFormat::DateGroup group = PDFAppHomeFormat::getDateGroup(item.time, now);
            if (!currentGroup || *currentGroup != group)
            {
                PDFAppRecentItem header;
                header.type = PDFAppRecentItem::Type::Header;
                header.name = PDFAppHomeFormat::getDateGroupText(group);
                headerIndex = result.size();
                result.push_back(header);
                currentGroup = group;
            }
            ++result[headerIndex].count;
            result.push_back(std::move(item));
        }
        return result;
    }

    static bool isNewer(const PDFAppRecentItem& first, const PDFAppRecentItem& second)
    {
        return first.time > second.time;
    }
};

PDFAppRecentModel::PDFAppRecentModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppRecentModel::~PDFAppRecentModel() = default;

bool PDFAppRecentModel::isSameContent(const PDFAppRecentItem& first, const PDFAppRecentItem& second)
{
    // The times, not their texts (updateTimes formats them for the current minute);
    // the details of a workspace are its count and its time of the save
    const bool isSameDetails = first.type == PDFAppRecentItem::Type::Workspace ? first.savedTime == second.savedTime : first.details == second.details;
    return first.type == second.type && first.item.kind == second.item.kind && first.item.path == second.item.path && first.item.id == second.item.id &&
           first.name == second.name && isSameDetails && first.progressText == second.progressText && first.progress == second.progress &&
           first.time == second.time && first.isPinned == second.isPinned && first.count == second.count && first.isMissing == second.isMissing &&
           first.isLocked == second.isLocked && first.item.isRemovable == second.item.isRemovable && first.item.documentCount == second.item.documentCount && first.item.name == second.item.name;
}

void PDFAppRecentModel::setItems(std::vector<PDFAppRecentItem> items)
{
    bool isSame = items.size() == m_items.size();
    for (size_t i = 0; isSame && i < items.size(); ++i)
    {
        isSame = isSameContent(items[i], m_items[i]);
    }

    if (isSame)
    {
        // Only the pictures (a thumbnail arrived) and the texts of the times (a later
        // minute, O25) may differ
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].pixmap.cacheKey() != m_items[i].pixmap.cacheKey())
            {
                m_items[i].pixmap = items[i].pixmap;
                const QModelIndex changed = index(int(i), 0);
                Q_EMIT dataChanged(changed, changed, { PDFComponentRoles::PixmapRole });
            }
            if (items[i].opened != m_items[i].opened || items[i].details != m_items[i].details)
            {
                m_items[i].opened = items[i].opened;
                m_items[i].details = items[i].details;
                const QModelIndex changed = index(int(i), 0);
                Q_EMIT dataChanged(changed, changed, { PDFComponentRoles::DateRole, PDFComponentRoles::SubtitleRole, Qt::ToolTipRole, Qt::AccessibleDescriptionRole });
            }
        }
        return;
    }

    beginResetModel();
    m_items = std::move(items);
    endResetModel();
}

void PDFAppRecentModel::updateTimes(qint64 now)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        PDFAppRecentItem& item = m_items[i];
        if (item.type == PDFAppRecentItem::Type::Header)
        {
            continue;
        }

        const QString opened = PDFAppHomeFormat::formatTime(item.time, now);
        const QString details = item.type == PDFAppRecentItem::Type::Workspace
                                    ? PDFAppHomeFormat::formatTimeText(PDFAppHomeFormat::TimeText::WorkspaceDetails, item.savedTime, now, item.item.documentCount)
                                    : item.details;
        if (opened == item.opened && details == item.details)
        {
            continue;
        }

        item.opened = opened;
        item.details = details;
        const QModelIndex changed = index(int(i), 0);
        Q_EMIT dataChanged(changed, changed, { PDFComponentRoles::DateRole, PDFComponentRoles::SubtitleRole, Qt::ToolTipRole, Qt::AccessibleDescriptionRole });
    }
}

const PDFAppRecentItem* PDFAppRecentModel::getItem(int row) const
{
    return row >= 0 && row < int(m_items.size()) ? &m_items[size_t(row)] : nullptr;
}

int PDFAppRecentModel::findItem(PDFAppRecentItem::Type type, const QString& path, qint64 id) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const PDFAppRecentItem& item = m_items[i];
        if (item.type == type && item.type != PDFAppRecentItem::Type::Header && item.item.path == path && item.item.id == id)
        {
            return int(i);
        }
    }
    return -1;
}

void PDFAppRecentModel::setMissing(const QString& path, bool isMissing)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].type == PDFAppRecentItem::Type::File && m_items[i].item.path == path && m_items[i].isMissing != isMissing)
        {
            m_items[i].isMissing = isMissing;
            const QModelIndex changed = index(int(i), 0);
            Q_EMIT dataChanged(changed, changed);
        }
    }
}

void PDFAppRecentModel::setDetails(const QString& path, const QString& details)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].type == PDFAppRecentItem::Type::Folder && m_items[i].item.path == path && m_items[i].details != details)
        {
            m_items[i].details = details;
            const QModelIndex changed = index(int(i), 0);
            Q_EMIT dataChanged(changed, changed);
        }
    }
}

int PDFAppRecentModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant PDFAppRecentModel::data(const QModelIndex& index, int role) const
{
    const PDFAppRecentItem* item = index.isValid() && index.model() == this ? getItem(index.row()) : nullptr;
    if (!item)
    {
        return QVariant();
    }

    const bool isHeader = item->type == PDFAppRecentItem::Type::Header;
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return item->name;
        case Qt::ToolTipRole:
            return isHeader ? QVariant() : QVariant(item->details);
        case PDFComponentRoles::SubtitleRole:
        case Qt::AccessibleDescriptionRole:
            return item->details;
        case PDFComponentRoles::ProgressTextRole:
            if (item->isMissing)
            {
                return tr("No longer exists");
            }
            return item->progressText;
        case PDFComponentRoles::ProgressRole:
            return item->isMissing ? -1.0 : item->progress;
        case PDFComponentRoles::DateRole:
            return item->opened;
        case PDFComponentRoles::ItemTypeRole:
            return int(PDFAppRecentPageHelper::toItemType(item->type));
        case PDFComponentRoles::PixmapRole:
            return item->pixmap.isNull() ? QVariant() : QVariant(item->pixmap);
        case PDFComponentRoles::PinnedRole:
            return item->isPinned;
        case PDFComponentRoles::StarVisibleRole:
            return item->type != PDFAppRecentItem::Type::Workspace;
        case PDFComponentRoles::MissingRole:
            return item->isMissing;
        case PDFComponentRoles::LockedRole:
            return item->isLocked;
        case HeaderRole:
            return isHeader;
        case CountRole:
            return item->count;
        default:
            break;
    }
    return QVariant();
}

bool PDFAppRecentModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    const PDFAppRecentItem* item = index.isValid() && index.model() == this ? getItem(index.row()) : nullptr;
    if (!item || role != PDFComponentRoles::PinnedRole || item->type == PDFAppRecentItem::Type::Header || item->type == PDFAppRecentItem::Type::Workspace)
    {
        return false;
    }

    // The page writes the database, the model shows the change after the notification
    const PDFAppHomeItem homeItem = item->item;
    Q_EMIT pinRequested(homeItem, value.toBool());
    return false;
}

Qt::ItemFlags PDFAppRecentModel::flags(const QModelIndex& index) const
{
    const PDFAppRecentItem* item = index.isValid() && index.model() == this ? getItem(index.row()) : nullptr;
    if (!item)
    {
        return Qt::NoItemFlags;
    }
    if (item->type == PDFAppRecentItem::Type::Header)
    {
        return Qt::ItemIsEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

PDFAppRecentDelegate::PDFAppRecentDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppRecentDelegate::~PDFAppRecentDelegate() = default;

QList<PDFRowDelegate::Action> PDFAppRecentDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (getBool(index, PDFAppRecentModel::HeaderRole, false))
    {
        return QList<Action>();
    }

    // Workspaces are not pinned (newapp_storage.md 4 has no pinned workspaces):
    // the model gives them no star (StarVisibleRole), the library leaves out the action
    QList<Action> actions = BaseClass::getActions(option, index);
    for (Action& action : actions)
    {
        if (action.id == PinAction && getInt(index, PDFComponentRoles::ItemTypeRole, 0) == int(PDFComponentPainter::RecentItemType::Folder))
        {
            const bool isPinned = getBool(index, PDFComponentRoles::PinnedRole, false);
            action.text = isPinned ? tr("Unpin from Pinned Locations") : tr("Pin to Pinned Locations");
            action.toolTip = action.text;
        }
    }
    return actions;
}

void PDFAppRecentDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    if (!getBool(index, PDFAppRecentModel::HeaderRole, false))
    {
        BaseClass::paintItem(painter, option, index, state);
        return;
    }

    // group-header: paddings 12 / 0 / 6 / 10, the label and the count, gap 6
    PDFStylePainterGuard guard(painter);
    PDFComponentPainter::DirectionGuard directionGuard(painter, state);
    const QRectF rect = QRectF(option.rect).adjusted(10, 12, 0, -6);
    const QFont labelFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallSemiBold);
    const QFont countFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
    const QString label = getString(index, Qt::DisplayRole);
    const QString count = PDFAppLocale::formatNumber(getInt(index, PDFAppRecentModel::CountRole, 0));
    const qreal labelWidth = qMin(rect.width(), PDFStylePainter::textWidth(label, labelFont));
    const QRectF labelRect = state.visualRect(rect, QRectF(rect.left(), rect.top(), labelWidth, rect.height()));
    const QRectF countRect = state.visualRect(rect, QRectF(rect.left() + labelWidth + 6, rect.top(), qMax(0.0, rect.width() - labelWidth - 6), rect.height()));
    PDFComponentPainter::drawElidedText(painter, state, labelRect, label, labelFont, PDFThemeColors::TextSecondary);
    PDFComponentPainter::drawElidedText(painter, state, countRect, count, countFont, PDFThemeColors::TextMuted);
}

QSize PDFAppRecentDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    if (getBool(index, PDFAppRecentModel::HeaderRole, false))
    {
        return QSize(width > 0 ? width : 100, HEADER_HEIGHT);
    }
    return BaseClass::getItemSize(option, index, width);
}

PDFAppRecentListView::PDFAppRecentListView(QWidget* parent) :
    BaseClass(parent)
{
    setViewportMargins(8, 4, 8, 8);
}

PDFAppRecentListView::~PDFAppRecentListView() = default;

PDFAppRecentPage::PDFAppRecentPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("recentPage"));

    // recent-grid: main column (toolbar, pinned, history) and the side column 340
    // (Continue where you left off, Workspaces), gap 24
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);
    m_columns = new PDFAppColumns(this);
    m_columns->setObjectName(QStringLiteral("recentGrid"));
    m_columns->addColumn(0, 1, 400);
    m_columns->addColumn(340, 0, 0);
    layout->addWidget(m_columns, 1);

    QBoxLayout* main = m_columns->getColumnLayout(0);
    main->addWidget(createToolbar(m_columns->getColumn(0)));
    main->addWidget(createPinned(m_columns->getColumn(0)));
    main->addWidget(createHistory(m_columns->getColumn(0)), 1);

    QBoxLayout* side = m_columns->getColumnLayout(1);
    side->addWidget(createContinue(m_columns->getColumn(1)));
    side->addWidget(createWorkspaces(m_columns->getColumn(1)));
    side->addStretch(1);

    updateControls();
}

PDFAppRecentPage::~PDFAppRecentPage()
{
    // The view does not use the delegate and the model any more, while they go
    if (m_listView)
    {
        m_listView->disconnect(this);
    }
}

QWidget* PDFAppRecentPage::createToolbar(QWidget* parent)
{
    // toolbar: the search, the sort and Clear history… (gap 12), the filters below (gap 12)
    QWidget* toolbar = new QWidget(parent);
    toolbar->setObjectName(QStringLiteral("toolbar"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(toolbar, 12);

    m_searchField = new PDFLineEdit(toolbar);
    m_searchField->setObjectName(QStringLiteral("recentSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search by name or path…"));
    m_searchField->setAccessibleName(tr("Search in Recent"));
    m_searchField->setMaxLength(MAXIMUM_SEARCH_LENGTH);
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_searchField, &QLineEdit::textEdited, this, &PDFAppRecentPage::onSearchTextEdited);

    QWidget* actions = new QWidget(toolbar);
    actions->setObjectName(QStringLiteral("toolbarActions"));
    QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(12);

    m_sortBox = new PDFComboBox(actions);
    m_sortBox->setObjectName(QStringLiteral("sortBox"));
    m_sortBox->setSelectPrefix(tr("Sort:"));
    m_sortBox->setAccessibleName(tr("Sort"));
    m_sortBox->addItem(tr("Last opened"), PDFRecentRepository::getSortId(PDFRecentSort::LastOpened));
    m_sortBox->addItem(tr("Name"), PDFRecentRepository::getSortId(PDFRecentSort::Name));
    m_sortBox->addItem(tr("Folder"), PDFRecentRepository::getSortId(PDFRecentSort::Folder));
    m_sortBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_sortBox, &QComboBox::activated, this, &PDFAppRecentPage::onSortActivated);
    actionsLayout->addWidget(m_sortBox, 0, Qt::AlignVCenter);

    m_clearButton = PDFAppPageWidgets::createButton(tr("Clear history…"), QStringLiteral("trash"), false, actions);
    m_clearButton->setObjectName(QStringLiteral("clearHistoryButton"));
    m_clearButton->setFixedHeight(32);
    connect(m_clearButton, &QPushButton::clicked, this, &PDFAppRecentPage::onClearHistoryClicked);
    actionsLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);

    // row-search: the search takes the rest of the row; in a narrow page the sort
    // and Clear history… are below it
    PDFAppResponsiveRow* searchRow = new PDFAppResponsiveRow(m_searchField, actions, PDFAppRecentPageHelper::SEARCH_ROW_THRESHOLD, 12, toolbar);
    searchRow->setObjectName(QStringLiteral("searchRow"));
    layout->addWidget(searchRow);

    // row-filters: chips with their counts, gap 6
    QWidget* filters = new QWidget(toolbar);
    filters->setObjectName(QStringLiteral("filters"));
    PDFAppFlowLayout* filtersLayout = new PDFAppFlowLayout(filters, 6, 6);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);

    const std::pair<Filter, QString> chips[] = {
        { Filter::All, tr("All") },
        { Filter::Files, tr("Files") },
        { Filter::Folders, tr("Folders") },
        { Filter::Workspaces, tr("Workspaces") },
        { Filter::Pinned, tr("Pinned") }
    };

    for (const auto& chipInfo : chips)
    {
        QPushButton* chip = new QPushButton(PDFAppPageWidgets::escapeMnemonic(chipInfo.second), filters);
        const QString id = getFilterId(chipInfo.first);
        chip->setObjectName(id.isEmpty() ? QStringLiteral("chip-all") : QStringLiteral("chip-%1").arg(id));
        chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
        chip->setProperty(PDFWidgetStyleProperties::CHIP_COUNT, 0);
        chip->setCheckable(true);
        chip->setAutoDefault(false);
        chip->setAccessibleName(chipInfo.second);
        m_chipGroup->addButton(chip, int(chipInfo.first));
        filtersLayout->addWidget(chip);
        m_chips[int(chipInfo.first)] = chip;
    }
    connect(m_chipGroup, &QButtonGroup::buttonClicked, this, &PDFAppRecentPage::onChipClicked);
    layout->addWidget(filters);
    return toolbar;
}

QWidget* PDFAppRecentPage::createPinned(QWidget* parent)
{
    // pinned: the header with Show all pinned (N), the cards 3 in a row (gap 12)
    m_pinnedSection = new QWidget(parent);
    m_pinnedSection->setObjectName(QStringLiteral("pinned"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(m_pinnedSection, 12);
    m_showAllPinned = new PDFTextLink(tr("Show all pinned"), m_pinnedSection);
    m_showAllPinned->setObjectName(QStringLiteral("showAllPinned"));
    withLink(m_showAllPinned, createRoute(Filter::Pinned, PDFRecentSort::LastOpened, QString()).toString());
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Pinned"), m_showAllPinned, m_pinnedSection));

    PDFAppGrid* grid = new PDFAppGrid(220, MAXIMUM_PINNED_CARDS, 12, 12, m_pinnedSection);
    grid->setObjectName(QStringLiteral("pinnedCards"));
    m_pinnedGrid = grid;
    layout->addWidget(grid);
    m_pinnedSection->hide();
    return m_pinnedSection;
}

QWidget* PDFAppRecentPage::createHistory(QWidget* parent)
{
    // history: bg/surface, border, radius 12, paddings 4 / 18 / 8 / 8 (the scroll
    // bar lies in the trailing padding), the rows without gaps
    m_historyCard = PDFAppPageWidgets::createCard(0, 0, parent);
    m_historyCard->setObjectName(QStringLiteral("history"));
    m_historyCard->setMinimumHeight(PDFAppRecentPageHelper::MINIMUM_HISTORY_HEIGHT);
    m_historyCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(m_historyCard->layout());
    cardLayout->setContentsMargins(1, 1, 1, 1);

    m_historyStack = new QStackedWidget(m_historyCard);
    cardLayout->addWidget(m_historyStack);

    m_listView = new PDFAppRecentListView(m_historyStack);
    m_listView->setObjectName(QStringLiteral("historyList"));
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setSpacing(0);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setUniformItemSizes(false);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setAccessibleName(tr("History"));

    m_model = new PDFAppRecentModel(m_listView);
    m_delegate = new PDFAppRecentDelegate(m_listView);
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);
    m_listView->installEventFilter(this);
    connect(m_listView, &QAbstractItemView::clicked, this, &PDFAppRecentPage::onListClicked);
    connect(m_delegate, &PDFRowDelegate::actionTriggered, this, &PDFAppRecentPage::onDelegateAction);
    connect(m_model, &PDFAppRecentModel::pinRequested, this, &PDFAppRecentPage::onPinRequested);
    m_historyStack->addWidget(m_listView);

    m_emptyState = new PDFAppEmptyState(m_historyStack);
    connect(m_emptyState, &PDFAppEmptyState::browseRequested, this, &PDFAppRecentPage::onBrowseRequested);
    m_historyStack->addWidget(m_emptyState);
    return m_historyCard;
}

QWidget* PDFAppRecentPage::createContinue(QWidget* parent)
{
    // continue-reading: the header and the card
    m_continueSection = new QWidget(parent);
    m_continueSection->setObjectName(QStringLiteral("continueReading"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(m_continueSection, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Continue where you left off"), nullptr, m_continueSection));
    m_continueCard = new PDFAppContinueCard(m_continueSection);
    connect(m_continueCard, &PDFAppContinueCard::continueRequested, this, &PDFAppRecentPage::onContinueRequested);
    layout->addWidget(m_continueCard);
    m_continueSection->hide();
    return m_continueSection;
}

QWidget* PDFAppRecentPage::createWorkspaces(QWidget* parent)
{
    // workspaces: the header with Manage, the list card (padding 8, gap 2)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("workspaces"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    PDFTextLink* manage = new PDFTextLink(tr("Manage"), section);
    manage->setObjectName(QStringLiteral("manageWorkspaces"));
    withLink(manage, createRoute(Filter::Workspaces, PDFRecentSort::LastOpened, QString()).toString());
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Workspaces"), manage, section));

    m_workspacesCard = PDFAppPageWidgets::createCard(8, 2, section);
    m_workspacesCard->setObjectName(QStringLiteral("workspacesList"));
    m_workspacesLayout = qobject_cast<QVBoxLayout*>(m_workspacesCard->layout());
    m_sessionRow = new PDFAppSessionRow(false, m_workspacesCard);
    m_sessionRow->hide();
    connect(m_sessionRow, &PDFAppSessionRow::restoreRequested, this, &PDFAppRecentPage::onSessionRestoreRequested);
    m_workspacesLayout->addWidget(m_sessionRow);
    layout->addWidget(m_workspacesCard);
    return section;
}

bool PDFAppRecentPage::setRoute(const PDFAppRoute& route)
{
    const std::optional<Filter> filter = parseFilter(route.getQueryValue(QStringLiteral("filter")));
    const QString sortId = route.getQueryValue(QStringLiteral("sort"));
    const std::optional<PDFRecentSort> sort = sortId.isEmpty() ? std::optional<PDFRecentSort>(PDFRecentSort::LastOpened) : PDFRecentRepository::parseSort(sortId);
    const QString searchText = route.getQueryValue(QStringLiteral("q"));

    // The route table validates the values too; the page never shows an unknown one
    if (!filter || !sort || searchText.size() > MAXIMUM_SEARCH_LENGTH || !route.getFragment().isEmpty())
    {
        return false;
    }

    m_filter = *filter;
    m_sort = *sort;
    m_searchText = PDFAppHomeFormat::toSearchText(searchText, MAXIMUM_SEARCH_LENGTH);
    updateControls();
    if (isVisible())
    {
        refreshNow();
    }
    return BaseClass::setRoute(route);
}

QString PDFAppRecentPage::getTitle() const
{
    return tr("Recent");
}

QString PDFAppRecentPage::getHeaderTitle() const
{
    return tr("Recent");
}

QBoxLayout* PDFAppRecentPage::getBannerLayout() const
{
    return m_columns ? m_columns->getMainLayout() : nullptr;
}

QString PDFAppRecentPage::getHeaderSubtitle() const
{
    return tr("Documents, folders and workspaces you opened recently, with the page where you stopped reading.");
}

QVariantMap PDFAppRecentPage::saveViewState() const
{
    QVariantMap state = BaseClass::saveViewState();
    if (m_listView)
    {
        state.insert(QString::fromLatin1(PDFAppRecentPageHelper::LIST_SCROLL), m_listView->verticalScrollBar()->value());
    }
    return state;
}

void PDFAppRecentPage::restoreViewState(const QVariantMap& state)
{
    BaseClass::restoreViewState(state);

    bool isOk = false;
    const int value = state.value(QString::fromLatin1(PDFAppRecentPageHelper::LIST_SCROLL)).toInt(&isOk);
    if (isOk && value >= 0)
    {
        // The rows are laid out after the page is shown
        m_pendingListScroll = value;
        QMetaObject::invokeMethod(this, &PDFAppRecentPage::applyPendingListScroll, Qt::QueuedConnection);
    }
}

void PDFAppRecentPage::applyPendingListScroll()
{
    if (m_listView && m_pendingListScroll)
    {
        m_listView->doItemsLayout();
        m_listView->verticalScrollBar()->setValue(*m_pendingListScroll);
    }
    m_pendingListScroll.reset();
}

PDFAppRoute PDFAppRecentPage::createRoute(Filter filter, PDFRecentSort sort, const QString& searchText)
{
    // The default sort is not a part of the route
    const QString sortId = sort != PDFRecentSort::LastOpened ? PDFRecentRepository::getSortId(sort) : QString();
    return PDFAppRoutes::recent(getFilterId(filter), sortId, searchText.left(MAXIMUM_SEARCH_LENGTH));
}

QString PDFAppRecentPage::getFilterId(Filter filter)
{
    switch (filter)
    {
        case Filter::Files:
            return QStringLiteral("files");
        case Filter::Folders:
            return QStringLiteral("folders");
        case Filter::Workspaces:
            return QStringLiteral("workspaces");
        case Filter::Pinned:
            return QStringLiteral("pinned");
        case Filter::All:
            break;
    }
    return QString();
}

std::optional<PDFAppRecentPage::Filter> PDFAppRecentPage::parseFilter(const QString& id)
{
    for (Filter filter : { Filter::All, Filter::Files, Filter::Folders, Filter::Workspaces, Filter::Pinned })
    {
        if (getFilterId(filter) == id)
        {
            return filter;
        }
    }
    return std::nullopt;
}

void PDFAppRecentPage::setFilter(Filter filter, PDFRecentSort sort, const QString& searchText)
{
    QString text = PDFAppHomeFormat::toSearchText(searchText, MAXIMUM_SEARCH_LENGTH);

    // The encoded route has a limit of its length (a search of many non-Latin characters)
    while (!text.isEmpty() && !createRoute(filter, sort, text).isValid())
    {
        text.chop(text.size() >= 2 && text.back().isLowSurrogate() ? 2 : 1);
    }

    if (filter == m_filter && sort == m_sort && text == m_searchText)
    {
        return;
    }

    m_filter = filter;
    m_sort = sort;
    m_searchText = text;
    updateControls();
    refreshNow();
    updateRouteInHistory();
}

void PDFAppRecentPage::updateRouteInHistory()
{
    // The filter is the state of the route: the current entry of the history of
    // the tab gets the new route, the page stays (the focus stays in the search
    // field or on the chip)
    replaceRoute(createRoute(m_filter, m_sort, m_searchText));
}

void PDFAppRecentPage::updateControls()
{
    m_isUpdatingControls = true;
    if (m_searchField && m_searchField->text() != m_searchText)
    {
        m_searchField->setText(m_searchText);
    }
    if (m_sortBox)
    {
        m_sortBox->setCurrentIndex(qMax(0, m_sortBox->findData(PDFRecentRepository::getSortId(m_sort))));
    }
    if (QPushButton* chip = getChip(m_filter))
    {
        chip->setChecked(true);
    }
    m_isUpdatingControls = false;
}

QPushButton* PDFAppRecentPage::getChip(Filter filter) const
{
    auto it = m_chips.find(int(filter));
    return it != m_chips.end() ? it->second.data() : nullptr;
}

QList<PDFPinnedCard*> PDFAppRecentPage::getPinnedCards() const
{
    return PDFAppRecentPageHelper::toList(m_pinnedCards);
}

QList<PDFWorkspaceRow*> PDFAppRecentPage::getWorkspaceRows() const
{
    return PDFAppRecentPageHelper::toList(m_workspaceRows);
}

bool PDFAppRecentPage::isEmptyStateShown() const
{
    return m_historyStack && m_historyStack->currentWidget() == m_emptyState;
}

void PDFAppRecentPage::refresh()
{
    refreshList();
    refreshContinue();
    refreshWorkspaces();
}

void PDFAppRecentPage::refreshList()
{
    PDFAppContext* context = getContext();
    std::vector<PDFRecentFile> files;
    std::vector<PDFRecentFile> pinned;
    std::vector<PDFRecentFolder> folders;
    std::vector<PDFWorkspace> workspaces;
    std::vector<PDFPinnedLocation> locations;
    int fileCount = 0;
    if (context && context->getDatabase())
    {
        PDFRecentRepository recent = context->getRecent();
        PDFRecentQuery query;
        query.sort = m_sort;
        files = recent.getFiles(query);
        fileCount = int(files.size());
        pinned = recent.getPinnedFiles();
        folders = recent.getUsedFolders();
        workspaces = context->getWorkspaces().getRecentWorkspaces();
        locations = recent.getPinnedLocations();
    }

    // Counts of the chips are the counts of the whole history (the search filters the rows)
    updateChips(fileCount + int(folders.size()) + int(workspaces.size()), fileCount, int(folders.size()), int(workspaces.size()), int(pinned.size()));

    const qint64 now = getNow();
    const bool hasSearch = !m_searchText.isEmpty();
    std::vector<PDFAppRecentItem> items;

    const bool showFiles = m_filter == Filter::All || m_filter == Filter::Files || m_filter == Filter::Pinned;
    const bool showFolders = m_filter == Filter::All || m_filter == Filter::Folders;
    const bool showWorkspaces = m_filter == Filter::All || m_filter == Filter::Workspaces;

    if (showFiles)
    {
        for (const PDFRecentFile& file : (m_filter == Filter::Pinned ? pinned : files))
        {
            if (hasSearch && !PDFRepositoryHelper::containsText(file.name, m_searchText) && !PDFRepositoryHelper::containsText(file.path, m_searchText))
            {
                continue;
            }

            PDFAppRecentItem item;
            item.type = PDFAppRecentItem::Type::File;
            item.item.kind = PDFAppHomeItemKind::File;
            item.item.path = file.path;
            item.item.name = file.name;
            item.item.isPinned = file.pinned;
            item.name = PDFAppHomeFormat::toDisplayText(file.name);
            item.details = PDFAppHomeFormat::formatFileDetails(file);
            item.progressText = PDFAppHomeFormat::formatReading(file);
            item.progress = PDFAppHomeFormat::getBarProgress(file);
            item.opened = PDFAppHomeFormat::formatTime(file.lastOpened, now);
            item.time = file.lastOpened;
            item.sortFolder = file.folder;
            item.isPinned = file.pinned;
            requestPathInfo(file.path);
            const std::optional<PDFAppFolderInfo> info = getPathInfo(file.path);
            item.isMissing = info && info->pdfCount != -2 && !info->isFile;
            item.isLocked = file.thumbnailState == PDFRecentThumbnailState::PasswordProtected;
            requestThumbnail(file);
            item.pixmap = getThumbnail(file);
            items.push_back(std::move(item));
        }
    }

    if (showFolders)
    {
        for (const PDFRecentFolder& folder : folders)
        {
            if (hasSearch && !PDFRepositoryHelper::containsText(folder.name, m_searchText) && !PDFRepositoryHelper::containsText(folder.path, m_searchText))
            {
                continue;
            }

            bool isPinned = false;
            for (const PDFPinnedLocation& location : locations)
            {
                isPinned = isPinned || PDFRepositoryHelper::isSamePath(location.path, folder.path);
            }

            PDFAppRecentItem item;
            item.type = PDFAppRecentItem::Type::Folder;
            item.item.kind = PDFAppHomeItemKind::Folder;
            item.item.path = folder.path;
            item.item.name = folder.name;
            item.item.isPinned = isPinned;
            item.item.isRemovable = true;
            item.name = PDFAppHomeFormat::toDisplayText(folder.name);
            item.details = PDFAppHomeFormat::toDisplayPath(folder.path);
            requestFolderInfo(folder.path);
            const std::optional<PDFAppFolderInfo> info = getPathInfo(folder.path);
            if (info && info->pdfCount >= 0)
            {
                item.details = tr("%1 · %2").arg(item.details, PDFAppHomeFormat::formatPdfCount(info->pdfCount, info->isCountLimited));
            }
            item.progressText = QStringLiteral("—");
            item.opened = PDFAppHomeFormat::formatTime(folder.lastUsed, now);
            item.time = folder.lastUsed;
            item.sortFolder = folder.path;
            item.isPinned = isPinned;
            items.push_back(std::move(item));
        }
    }

    if (showWorkspaces)
    {
        for (const PDFWorkspace& workspace : workspaces)
        {
            if (hasSearch && !PDFRepositoryHelper::containsText(workspace.name, m_searchText))
            {
                continue;
            }

            PDFAppRecentItem item;
            item.type = PDFAppRecentItem::Type::Workspace;
            item.item.kind = PDFAppHomeItemKind::Workspace;
            item.item.id = workspace.id;
            item.item.name = workspace.name;
            item.item.documentCount = workspace.documentCount;
            item.name = PDFAppHomeFormat::toDisplayText(workspace.name);
            item.details = PDFAppHomeFormat::formatWorkspaceDetails(workspace, now);
            item.progressText = QStringLiteral("—");
            item.opened = PDFAppHomeFormat::formatTime(workspace.opened, now);
            item.time = workspace.opened;
            item.savedTime = workspace.saved;
            items.push_back(std::move(item));
        }
    }

    // Last opened: groups by the day (Today, Yesterday, This week, Earlier); Name and
    // Folder: one list without groups (the files are sorted by the repository, the
    // other items join them by the same key)
    if (m_sort == PDFRecentSort::LastOpened)
    {
        items = PDFAppRecentPageHelper::groupByDay(std::move(items), now);
        m_groupsDate = QDateTime::fromMSecsSinceEpoch(now, QTimeZone::LocalTime).date();
    }
    else
    {
        m_groupsDate = QDate();
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::stable_sort(items.begin(), items.end(), PDFAppRecentPageHelper::Comparator(&collator, m_sort == PDFRecentSort::Folder));
    }

    // The current item of the list (the keyboard) survives a reset of the model - a
    // pin by the keyboard changes the item and the model is reset
    const PDFAppRecentItem* current = m_model && m_listView ? m_model->getItem(m_listView->currentIndex().row()) : nullptr;
    const std::optional<PDFAppRecentItem> currentItem = current ? std::optional<PDFAppRecentItem>(*current) : std::nullopt;

    if (m_model)
    {
        m_model->setItems(std::move(items));
    }

    if (currentItem && m_model && m_listView && !m_listView->currentIndex().isValid())
    {
        const int row = m_model->findItem(currentItem->type, currentItem->item.path, currentItem->item.id);
        if (row >= 0)
        {
            m_listView->setCurrentIndex(m_model->index(row, 0));
        }
    }

    // The pinned cards are shown in All without a search (Pinned shows them as the list)
    refreshPinned(m_filter == Filter::All && !hasSearch ? pinned : std::vector<PDFRecentFile>());
    updateEmptyState();
}

void PDFAppRecentPage::updateChips(int all, int files, int folders, int workspaces, int pinned)
{
    const std::pair<Filter, int> counts[] = { { Filter::All, all }, { Filter::Files, files }, { Filter::Folders, folders }, { Filter::Workspaces, workspaces }, { Filter::Pinned, pinned } };
    for (const auto& count : counts)
    {
        QPushButton* chip = getChip(count.first);
        if (chip && chip->property(PDFWidgetStyleProperties::CHIP_COUNT).toInt() != count.second)
        {
            PDFWidgetStyleProperties::set(chip, PDFWidgetStyleProperties::CHIP_COUNT, count.second);
            chip->updateGeometry();
        }
    }
}

void PDFAppRecentPage::refreshPinned(const std::vector<PDFRecentFile>& pinned)
{
    if (m_showAllPinned)
    {
        m_showAllPinned->setText(tr("Show all pinned (%1)").arg(pinned.size()));
    }
    if (m_pinnedSection)
    {
        m_pinnedSection->setVisible(!pinned.empty());
    }

    std::vector<PDFRecentFile> shown(pinned.begin(), pinned.begin() + qMin(pinned.size(), size_t(MAXIMUM_PINNED_CARDS)));
    QStringList signature;
    for (const PDFRecentFile& file : shown)
    {
        signature << QStringLiteral("%1|%2|%3|%4").arg(file.path, file.name, PDFAppHomeFormat::formatPinnedProgress(file)).arg(file.getProgress());
    }
    const QString text = signature.join(QChar(0x1F));

    if (text != m_pinnedSignature || m_pinnedCards.size() != qsizetype(shown.size()))
    {
        m_pinnedSignature = text;
        PDFAppRecentPageHelper::deleteAll(m_pinnedCards);
        PDFAppGrid* grid = qobject_cast<PDFAppGrid*>(m_pinnedGrid.data());
        for (const PDFRecentFile& file : shown)
        {
            if (!grid)
            {
                break;
            }

            PDFPinnedCard* card = new PDFPinnedCard(PDFAppHomeFormat::toDisplayText(file.name), PDFAppHomeFormat::toDisplayPath(file.folder),
                                                    PDFAppHomeFormat::formatPinnedProgress(file), file.getProgress(), grid);
            card->setObjectName(QStringLiteral("pinnedCard"));
            card->setProperty(PDFAppRecentPageHelper::PATH_PROPERTY, file.path);

            PDFAppHomeItem item;
            item.kind = PDFAppHomeItemKind::File;
            item.path = file.path;
            item.name = file.name;
            item.isPinned = true;
            card->setMenu(createItemMenu(item, card));
            connect(card, &PDFPinnedCard::clicked, this, &PDFAppRecentPage::onPinnedCardClicked);
            connect(card, &PDFPinnedCard::pinnedChanged, this, &PDFAppRecentPage::onPinnedCardPinnedChanged);
            grid->addWidget(card);
            m_pinnedCards << card;
        }
        if (grid)
        {
            grid->updateCells();
        }
    }

    // Pictures of the first pages (also a thumbnail, which has just arrived), the
    // lock of a document protected by a password and the state of a missing file
    for (const PDFRecentFile& file : shown)
    {
        requestThumbnail(file);
        requestPathInfo(file.path);
        const QPixmap pixmap = getThumbnail(file);
        const std::optional<PDFAppFolderInfo> info = getPathInfo(file.path);
        const bool isMissing = info && info->pdfCount != -2 && !info->isFile;
        for (const QPointer<PDFPinnedCard>& card : std::as_const(m_pinnedCards))
        {
            if (card && card->property(PDFAppRecentPageHelper::PATH_PROPERTY).toString() == file.path)
            {
                if (card->getPixmap().cacheKey() != pixmap.cacheKey())
                {
                    card->setPixmap(pixmap);
                }
                card->setLocked(file.thumbnailState == PDFRecentThumbnailState::PasswordProtected);
                card->setStatus(isMissing ? PDFPinnedCard::Status::Missing : PDFPinnedCard::Status::Available);
            }
        }
    }
}

void PDFAppRecentPage::updateEmptyState()
{
    if (!m_historyStack || !m_model || !m_emptyState)
    {
        return;
    }

    // The history is empty, when it has no item, or (All without a search) when it
    // has only the pinned files, which the cards above show (955:142865)
    bool hasHistory = false;
    for (const PDFAppRecentItem& item : m_model->getItems())
    {
        if (item.type == PDFAppRecentItem::Type::Header)
        {
            continue;
        }
        if (m_filter != Filter::All || !m_searchText.isEmpty() || item.type != PDFAppRecentItem::Type::File || !item.isPinned)
        {
            hasHistory = true;
            break;
        }
    }

    if (!m_searchText.isEmpty())
    {
        m_emptyState->setTexts(tr("No results"), tr("Nothing in Recent matches “%1”.").arg(PDFAppHomeFormat::toDisplayText(m_searchText, 64)));
    }
    else
    {
        switch (m_filter)
        {
            case Filter::All:
                m_emptyState->setTexts(tr("The history is empty"),
                                       tr("Documents, folders and workspaces you open from now on appear here. Your pinned items and saved workspaces were kept."));
                break;
            case Filter::Files:
                m_emptyState->setTexts(tr("No recent files"), tr("The documents you open appear here."));
                break;
            case Filter::Folders:
                m_emptyState->setTexts(tr("No recent folders"), tr("The folders, from which you open documents, appear here."));
                break;
            case Filter::Workspaces:
                m_emptyState->setTexts(tr("No recent workspaces"), tr("The workspaces you open appear here. Save a workspace in Open › Workspaces."));
                break;
            case Filter::Pinned:
                m_emptyState->setTexts(tr("No pinned files"), tr("Pin a document by its star to keep it at the top of Recent."));
                break;
        }
    }

    m_historyStack->setCurrentWidget(hasHistory ? static_cast<QWidget*>(m_listView.data()) : static_cast<QWidget*>(m_emptyState.data()));
}

void PDFAppRecentPage::refreshContinue()
{
    PDFAppContext* context = getContext();
    m_continueCandidates.clear();
    if (context && context->getDatabase())
    {
        m_continueCandidates = context->getRecent().getContinueReading(MAXIMUM_CONTINUE_CANDIDATES);
    }

    // The first file in progress, which exists (asked by the folder information
    // service); unknown files before it are waited for. A file on the network is
    // not checked by the page: it is shown (opening it checks it)
    const PDFRecentFile* shown = nullptr;
    for (const PDFRecentFile& file : m_continueCandidates)
    {
        if (isNetworkPath(file.path))
        {
            shown = &file;
            break;
        }

        requestPathInfo(file.path);
        const std::optional<PDFAppFolderInfo> info = getPathInfo(file.path);
        if (!info || info->pdfCount == -2)
        {
            break;
        }
        if (info->isFile)
        {
            shown = &file;
            break;
        }
    }

    if (m_continueSection)
    {
        m_continueSection->setVisible(shown != nullptr);
    }
    if (shown && m_continueCard)
    {
        m_continueCard->setFile(*shown, getNow());
        requestThumbnail(*shown);
        const QPixmap pixmap = getThumbnail(*shown);
        if (m_continueCard->getPixmap().cacheKey() != pixmap.cacheKey())
        {
            m_continueCard->setPixmap(pixmap);
        }
    }
}

void PDFAppRecentPage::refreshWorkspaces()
{
    PDFAppContext* context = getContext();
    std::vector<PDFWorkspace> workspaces;
    std::optional<PDFWorkspace> session;
    if (context && context->getDatabase())
    {
        workspaces = context->getWorkspaces().getWorkspaces();
        session = context->getWorkspaces().getLastSession();
    }

    const qint64 now = getNow();
    if (m_sessionRow)
    {
        m_sessionRow->setVisible(session.has_value());
        if (session)
        {
            PDFAppHomeFormat::setTimeText(m_sessionRow, PDFAppHomeFormat::TimeText::SessionMeta, session->saved, now, session->documentCount);
        }
    }
    if (m_workspacesCard)
    {
        m_workspacesCard->setVisible(session.has_value() || !workspaces.empty());
    }

    QStringList signature;
    for (const PDFWorkspace& workspace : workspaces)
    {
        // The times, not their texts: the minute ticker updates the texts (O25)
        signature << QStringLiteral("%1|%2|%3|%4").arg(workspace.id).arg(workspace.name).arg(workspace.documentCount).arg(workspace.opened);
    }
    const QString text = signature.join(QChar(0x1F));
    if (text == m_workspacesSignature && m_workspaceRows.size() == qsizetype(workspaces.size()))
    {
        return;
    }
    m_workspacesSignature = text;

    PDFAppRecentPageHelper::deleteAll(m_workspaceRows);
    for (const PDFWorkspace& workspace : workspaces)
    {
        PDFWorkspaceRow* row = new PDFWorkspaceRow(PDFAppHomeFormat::toDisplayText(workspace.name), QString(), m_workspacesCard);
        PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::WorkspaceMeta, workspace.opened, now, workspace.documentCount);
        row->setObjectName(QStringLiteral("workspaceRow"));
        row->setProperty(PDFAppRecentPageHelper::ID_PROPERTY, workspace.id);

        PDFAppHomeItem item;
        item.kind = PDFAppHomeItemKind::Workspace;
        item.id = workspace.id;
        item.name = workspace.name;
        item.documentCount = workspace.documentCount;
        row->setMenu(createItemMenu(item, row));
        connect(row, &PDFWorkspaceRow::clicked, this, &PDFAppRecentPage::onWorkspaceClicked);
        m_workspacesLayout->addWidget(row);
        m_workspaceRows << row;
    }
}

void PDFAppRecentPage::updateTimeTexts(qint64 now)
{
    // The rows of the page (Continue where you left off, the workspaces) and the
    // items of the list; the groups (Today, Yesterday, ...) are made again by a
    // refresh after midnight
    BaseClass::updateTimeTexts(now);
    if (m_model)
    {
        m_model->updateTimes(now);
    }

    if (m_groupsDate.isValid() && m_groupsDate != QDateTime::fromMSecsSinceEpoch(now, QTimeZone::LocalTime).date())
    {
        m_groupsDate = QDate();
        scheduleRefresh();
    }
}

void PDFAppRecentPage::onPathInfoChanged(const QString& path)
{
    const std::optional<PDFAppFolderInfo> info = getPathInfo(path);
    if (!info || !m_model)
    {
        return;
    }

    if (info->pdfCount >= 0 || info->isDirectory)
    {
        for (const PDFAppRecentItem& item : m_model->getItems())
        {
            if (item.type == PDFAppRecentItem::Type::Folder && item.item.path == path && info->pdfCount >= 0)
            {
                m_model->setDetails(path, tr("%1 · %2").arg(PDFAppHomeFormat::toDisplayPath(path), PDFAppHomeFormat::formatPdfCount(info->pdfCount, info->isCountLimited)));
                break;
            }
        }
    }
    else
    {
        m_model->setMissing(path, !info->isFile);
        for (const QPointer<PDFPinnedCard>& card : std::as_const(m_pinnedCards))
        {
            if (card && card->property(PDFAppRecentPageHelper::PATH_PROPERTY).toString() == path)
            {
                card->setStatus(info->isFile ? PDFPinnedCard::Status::Available : PDFPinnedCard::Status::Missing);
            }
        }
    }

    // Continue where you left off waits for the files before the first one, which exists
    for (const PDFRecentFile& file : m_continueCandidates)
    {
        if (file.path == path)
        {
            refreshContinue();
            break;
        }
    }
    updateEmptyState();
}

void PDFAppRecentPage::showListMenu(int row)
{
    const PDFAppRecentItem* item = m_model ? m_model->getItem(row) : nullptr;
    if (!item || item->type == PDFAppRecentItem::Type::Header || !m_listView || !m_delegate)
    {
        return;
    }

    if (QMenu* previous = m_listMenu.data())
    {
        previous->close();
    }

    // The menu is deleted, when it is closed; it is placed below the menu button
    QMenu* menu = createItemMenu(item->item, this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    m_listMenu = menu;
    const QModelIndex index = m_model->index(row, 0);
    const QRect rect = m_delegate->getActionRect(m_listView, index, PDFRecentItemDelegate::MenuAction);
    const QRect anchor = rect.isValid() ? rect : m_listView->visualRect(index);
    const QPoint position = m_listView->viewport()->mapToGlobal(layoutDirection() == Qt::RightToLeft ? anchor.bottomRight() : anchor.bottomLeft());
    menu->popup(position);
}

void PDFAppRecentPage::openItem(const QModelIndex& index)
{
    const PDFAppRecentItem* item = m_model && index.isValid() ? m_model->getItem(index.row()) : nullptr;
    if (!item || item->type == PDFAppRecentItem::Type::Header)
    {
        return;
    }

    const PDFAppHomeItem homeItem = item->item;
    performAction(homeItem, homeItem.kind == PDFAppHomeItemKind::Folder ? PDFAppHomeAction::BrowseHere : PDFAppHomeAction::Open);
}

bool PDFAppRecentPage::eventFilter(QObject* watched, QEvent* event)
{
    // Enter on the current item opens it (QAbstractItemView::activated also comes
    // with a double click, which follows the click, which has opened it already)
    if (watched == m_listView && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && keyEvent->modifiers() == Qt::NoModifier)
        {
            openItem(m_listView->currentIndex());
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppRecentPage::onSearchTextEdited(const QString& text)
{
    if (!m_isUpdatingControls)
    {
        setFilter(m_filter, m_sort, text);
    }
}

void PDFAppRecentPage::onSortActivated(int index)
{
    if (!m_sortBox || m_isUpdatingControls)
    {
        return;
    }

    const std::optional<PDFRecentSort> sort = PDFRecentRepository::parseSort(m_sortBox->itemData(index).toString());
    if (sort)
    {
        setFilter(m_filter, *sort, m_searchText);
    }
}

void PDFAppRecentPage::onChipClicked(QAbstractButton* button)
{
    if (!m_chipGroup || !button || m_isUpdatingControls)
    {
        return;
    }

    const int id = m_chipGroup->id(button);
    if (id >= int(Filter::All) && id <= int(Filter::Pinned))
    {
        setFilter(static_cast<Filter>(id), m_sort, m_searchText);
    }
}

void PDFAppRecentPage::onClearHistoryClicked()
{
    showClearHistoryDialog();
}

void PDFAppRecentPage::onListClicked(const QModelIndex& index)
{
    openItem(index);
}

void PDFAppRecentPage::onDelegateAction(const QModelIndex& index, int action)
{
    if (action == PDFRecentItemDelegate::MenuAction && index.isValid())
    {
        showListMenu(index.row());
    }
}

void PDFAppRecentPage::onPinRequested(const PDFAppHomeItem& item, bool isPinned)
{
    if (item.kind == PDFAppHomeItemKind::Folder)
    {
        setFolderPinned(item.path, isPinned);
    }
    else if (item.kind == PDFAppHomeItemKind::File)
    {
        setFilePinned(item.path, isPinned);
    }
}

void PDFAppRecentPage::onPinnedCardClicked()
{
    // The cards do not identify themselves in the signal: the path is their property
    const QObject* card = sender();
    if (card)
    {
        openFile(card->property(PDFAppRecentPageHelper::PATH_PROPERTY).toString());
    }
}

void PDFAppRecentPage::onPinnedCardPinnedChanged(bool isPinned)
{
    const QObject* card = sender();
    if (card)
    {
        setFilePinned(card->property(PDFAppRecentPageHelper::PATH_PROPERTY).toString(), isPinned);
    }
}

void PDFAppRecentPage::onContinueRequested()
{
    if (!m_continueCard)
    {
        return;
    }

    // Continue reading opens the file at its last page
    for (const PDFRecentFile& file : m_continueCandidates)
    {
        if (file.path == m_continueCard->getPath())
        {
            openFile(file.path, qMax(0, file.lastPage));
            return;
        }
    }
}

void PDFAppRecentPage::onWorkspaceClicked()
{
    const QObject* row = sender();
    if (row)
    {
        openWorkspace(row->property(PDFAppRecentPageHelper::ID_PROPERTY).toLongLong());
    }
}

void PDFAppRecentPage::onSessionRestoreRequested()
{
    restoreLastSession();
}

void PDFAppRecentPage::onBrowseRequested()
{
    browseFiles();
}

PDFAppPage* PDFAppRecentPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppRecentPage(context, parent);
}

}   // namespace pdf
