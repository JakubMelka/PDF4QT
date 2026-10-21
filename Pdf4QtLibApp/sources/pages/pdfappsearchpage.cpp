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

#include "pdfappsearchpage.h"
#include "pdfappcontext.h"
#include "pdfappinfowidgets.h"
#include "pdfapppagenotfound.h"
#include "pdfapptab.h"
#include "pdfdatabasenotifier.h"
#include "pdfrecentrepository.h"
#include "pdfshortcutrepository.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfcomponentbadges.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page of the results
class PDFAppSearchPageHelper
{
public:
    PDFAppSearchPageHelper() = delete;

    /// Key of the chip of a group (-1 = All)
    static int getChipKey(std::optional<PDFAppSearchGroup> group)
    {
        return group ? int(*group) : -1;
    }

    /// Text of a button: '&' of the data is not a mnemonic
    static QString toButtonText(const QString& text)
    {
        return QString(text).replace(QLatin1Char('&'), QLatin1String("&&"));
    }

    /// Longest part of the text of the search shown in the texts of the page
    static constexpr qsizetype MAXIMUM_SHOWN_TEXT = 64;

    /// Smallest space above and below the empty state in the card of the results
    static constexpr int EMPTY_STATE_MINIMUM_PADDING = 72;

    /// Space below the hints in the frame of the empty state (955:142542)
    static constexpr int EMPTY_STATE_BOTTOM_SPACE = 40;
};

PDFAppSearchPage::PDFAppSearchPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("searchPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    build();

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppSearchPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppSearchPage::onEverythingChanged);
    }
}

PDFAppSearchPage::~PDFAppSearchPage() = default;

void PDFAppSearchPage::build()
{
    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("searchContent"));
    layout()->addWidget(m_content);

    // search-grid: the main column (802) and the side column (340), gap 24

    QWidget* mainColumn = new QWidget(m_content);
    mainColumn->setObjectName(QStringLiteral("searchMainColumn"));
    QVBoxLayout* mainLayout = new QVBoxLayout(mainColumn);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(16);

    // row-filters: All and the groups with their counts, gap 8 (wrapped in a narrow window)
    QWidget* chips = new QWidget(mainColumn);
    chips->setObjectName(QStringLiteral("searchChips"));
    chips->setAccessibleName(tr("Groups"));
    PDFAppFlowLayout* chipsLayout = new PDFAppFlowLayout(chips, 8, 8);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    chipsLayout->addWidget(createChip(std::nullopt, chips));
    for (const PDFAppSearchGroup group : PDFAppSearchGroups::getGroups())
    {
        chipsLayout->addWidget(createChip(group, chips));
    }
    connect(m_chipGroup, &QButtonGroup::buttonClicked, this, &PDFAppSearchPage::onChipClicked);
    mainLayout->addWidget(chips);

    // results: a card bg/surface with the border 1, radius 12, the padding 6 above and below
    m_resultsCard = new PDFAppCard(QMargins(1, 1 + PDFAppSearchPainter::PANEL_PADDING_TOP, 1, 1 + PDFAppSearchPainter::PANEL_PADDING_TOP), 0, mainColumn);
    m_resultsCard->setObjectName(QStringLiteral("searchResults"));
    mainLayout->addWidget(m_resultsCard);
    mainLayout->addStretch(1);

    m_sideColumn = createSideColumn(m_content);
    PDFAppColumns* columns = PDFAppColumns::createInContent(m_content, mainColumn, m_sideColumn);

    // The main column has the height of the page, so the card of the empty state
    // reaches the footer (pdf4qt-search-no-results 955:142130: results 693 high in
    // the main column of 741); the side column keeps its own height
    if (QLayout* columnsLayout = columns ? columns->layout() : nullptr)
    {
        columnsLayout->setAlignment(mainColumn, Qt::Alignment());
    }
}

QPushButton* PDFAppSearchPage::createChip(std::optional<PDFAppSearchGroup> group, QWidget* parent)
{
    const QString text = group ? PDFAppSearchGroups::getTitle(*group) : tr("All");
    QPushButton* chip = new QPushButton(PDFAppSearchPageHelper::toButtonText(text), parent);
    chip->setObjectName(group ? QStringLiteral("chip-%1").arg(PDFAppSearchGroups::getId(*group)) : QStringLiteral("chip-all"));
    chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
    chip->setProperty(PDFWidgetStyleProperties::CHIP_COUNT, 0);
    chip->setCheckable(true);
    chip->setAutoDefault(false);
    chip->setAccessibleName(text);
    m_chipGroup->addButton(chip);
    m_chips[PDFAppSearchPageHelper::getChipKey(group)] = chip;
    return chip;
}

QWidget* PDFAppSearchPage::createKeyRow(const QStringList& keys, const QString& text, QWidget* parent)
{
    // key-row: py 8, the keys in a column of 84 (gap 4), gap 12, the text Regular 12 text/secondary
    QWidget* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("searchKeyRow"));
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(12);

    QWidget* keysWidget = new QWidget(row);
    keysWidget->setFixedWidth(84);
    QHBoxLayout* keysLayout = new QHBoxLayout(keysWidget);
    keysLayout->setContentsMargins(0, 0, 0, 0);
    keysLayout->setSpacing(PDFAppSearchPainter::KEY_GAP);
    for (const QString& key : keys)
    {
        keysLayout->addWidget(new PDFAppKeyCap(key, keysWidget));
    }
    keysLayout->addStretch(1);
    layout->addWidget(keysWidget);

    PDFLabel* label = PDFAppPageWidgets::createLabel(text, QFont::Normal, 12, PDFThemeColors::TextSecondary, row);
    label->setWordWrap(true);
    layout->addWidget(label, 1);
    return row;
}

QWidget* PDFAppSearchPage::createSideColumn(QWidget* parent)
{
    QWidget* side = new QWidget(parent);
    side->setObjectName(QStringLiteral("searchSideColumn"));
    QVBoxLayout* layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);

    // section-Keyboard: a card (paddings 16 / 8) of the rows of the keys
    QWidget* keyboard = new QWidget(side);
    keyboard->setObjectName(QStringLiteral("searchKeyboard"));
    QVBoxLayout* keyboardLayout = new QVBoxLayout(keyboard);
    keyboardLayout->setContentsMargins(0, 0, 0, 0);
    keyboardLayout->setSpacing(12);
    keyboardLayout->addWidget(new PDFAppSectionHeader(tr("Keyboard"), keyboard));
    PDFAppCard* keyboardCard = new PDFAppCard(QMargins(17, 9, 17, 9), 0, keyboard);
    keyboardCard->setObjectName(QStringLiteral("searchKeyboardCard"));
    keyboardCard->getLayout()->addWidget(createKeyRow(PDFAppKeyCap::getKeyNames(getSearchShortcut()), tr("Search everywhere"), keyboardCard));
    keyboardCard->getLayout()->addWidget(createKeyRow({ QString(QChar(0x2191)), QString(QChar(0x2193)) }, tr("Move between results"), keyboardCard));
    keyboardCard->getLayout()->addWidget(createKeyRow({ PDFAppSearchPainter::getEnterKeyText() }, tr("Open the selected result"), keyboardCard));
    keyboardCard->getLayout()->addWidget(createKeyRow({ tr("Esc") }, tr("Clear the search"), keyboardCard));
    keyboardLayout->addWidget(keyboardCard);
    layout->addWidget(keyboard);

    // section-What is searched: a card (paddings 16 / 14, gap 8), Regular 12 / 17
    QWidget* searched = new QWidget(side);
    searched->setObjectName(QStringLiteral("searchWhatIsSearched"));
    QVBoxLayout* searchedLayout = new QVBoxLayout(searched);
    searchedLayout->setContentsMargins(0, 0, 0, 0);
    searchedLayout->setSpacing(12);
    searchedLayout->addWidget(new PDFAppSectionHeader(tr("What is searched"), searched));
    PDFAppCard* searchedCard = new PDFAppCard(QMargins(17, 15, 17, 15), 8, searched);
    PDFAppParagraph* what = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, searchedCard);
    what->setObjectName(QStringLiteral("searchWhatText"));
    what->setText(tr("Tools, commands of the menus and toolbars, settings, the documents you opened or pinned, and the user manual and video tutorials."));
    PDFAppParagraph* inside = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextMuted, searchedCard);
    inside->setObjectName(QStringLiteral("searchInsideText"));
    inside->setText(tr("To find text inside a document, open it and press Ctrl+F."));
    searchedCard->getLayout()->addWidget(what);
    searchedCard->getLayout()->addWidget(inside);
    searchedLayout->addWidget(searchedCard);
    layout->addWidget(searched);
    layout->addStretch(1);
    return side;
}

QWidget* PDFAppSearchPage::createEmptyState(QWidget* parent)
{
    // empty-state 955:142542: the circle 72 (accent/neutral/bg, icon/search 32),
    // 20, the title Bold 22, 8, the text Regular 14 / 21 (460), 20, Search the
    // User Manual, 16, the hints and 40 below them (the frame of the state is 299
    // high). The state is centered in the card, which fills the column (197 above
    // and below the frame in 955:142130); a low window keeps the padding 72 of the card
    QWidget* state = new QWidget(parent);
    state->setObjectName(QStringLiteral("searchEmptyState"));
    QVBoxLayout* layout = new QVBoxLayout(state);
    const int padding = PDFAppSearchPageHelper::EMPTY_STATE_MINIMUM_PADDING - 1 - PDFAppSearchPainter::PANEL_PADDING_TOP;
    layout->setContentsMargins(16, padding, 16, padding + PDFAppSearchPageHelper::EMPTY_STATE_BOTTOM_SPACE);
    layout->setSpacing(0);

    PDFIconBadge* circle = new PDFIconBadge(QStringLiteral("search"), 72, 32, state);
    circle->setObjectName(QStringLiteral("searchEmptyIcon"));
    circle->setColors(PDFThemeColors::AccentNeutralBg, PDFThemeColors::IconMuted);
    layout->addWidget(circle, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    m_emptyTitle = PDFAppPageWidgets::createLabel(QString(), QFont::Bold, 22, PDFThemeColors::TextPrimary, state);
    m_emptyTitle->setObjectName(QStringLiteral("searchEmptyTitle"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_emptyTitle);
    layout->addSpacing(8);

    m_emptyText = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 14), 21, PDFThemeColors::TextTertiary, state);
    m_emptyText->setObjectName(QStringLiteral("searchEmptyText"));
    m_emptyText->setAlignment(Qt::AlignHCenter);
    m_emptyText->setMaximumWidth(460);
    layout->addWidget(m_emptyText, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    m_searchManualButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("book-open")), tr("Search the User Manual"), state);
    m_searchManualButton->setObjectName(QStringLiteral("searchManualButton"));
    m_searchManualButton->setAutoDefault(false);
    connect(m_searchManualButton, &QPushButton::clicked, this, &PDFAppSearchPage::onSearchManualClicked);
    layout->addWidget(m_searchManualButton, 0, Qt::AlignHCenter);
    layout->addSpacing(16);

    // hint: Esc clear the search, 10, Ctrl K search everywhere (Regular 11 text/muted)
    QWidget* hints = new QWidget(state);
    hints->setObjectName(QStringLiteral("searchEmptyHints"));
    QHBoxLayout* hintsLayout = new QHBoxLayout(hints);
    hintsLayout->setContentsMargins(0, 0, 0, 0);
    hintsLayout->setSpacing(PDFAppSearchPainter::KEY_GAP);
    hintsLayout->addWidget(new PDFAppKeyCap(tr("Esc"), hints));
    hintsLayout->addWidget(PDFAppPageWidgets::createLabel(tr("clear the search"), QFont::Normal, 11, PDFThemeColors::TextMuted, hints));
    hintsLayout->addSpacing(10);
    for (const QString& key : PDFAppKeyCap::getKeyNames(getSearchShortcut()))
    {
        hintsLayout->addWidget(new PDFAppKeyCap(key, hints));
    }
    hintsLayout->addWidget(PDFAppPageWidgets::createLabel(tr("search everywhere"), QFont::Normal, 11, PDFThemeColors::TextMuted, hints));
    layout->addWidget(hints, 0, Qt::AlignHCenter);
    return state;
}

QKeySequence PDFAppSearchPage::getSearchShortcut() const
{
    PDFAppContext* context = getContext();
    if (!context || !context->getCommandRegistry())
    {
        return QKeySequence(Qt::CTRL | Qt::Key_K);
    }

    PDFAppCommandRegistry::UserShortcuts shortcuts;
    if (context->getDatabase())
    {
        shortcuts = context->getShortcuts().getShortcuts();
    }
    return context->getCommandRegistry()->getShortcut(PDFAppCommands::search(), shortcuts);
}

bool PDFAppSearchPage::setRoute(const PDFAppRoute& route)
{
    // group= is one of the groups (the route table refuses other values too)
    std::optional<PDFAppSearchGroup> group;
    if (route.hasQueryItem(QStringLiteral("group")))
    {
        group = PDFAppSearchGroups::findGroup(route.getQueryValue(QStringLiteral("group")));
        if (!group)
        {
            return false;
        }
    }

    BaseClass::setRoute(route);
    m_searchText = PDFSearchQuery::sanitize(route.getQueryValue(QStringLiteral("q")));
    m_group = group;
    refresh();
    return true;
}

QString PDFAppSearchPage::getTitle() const
{
    return tr("Search results");
}

QString PDFAppSearchPage::getHeaderTitle() const
{
    return tr("Search results");
}

QString PDFAppSearchPage::getHeaderSubtitle() const
{
    if (m_results.isEmptyQuery)
    {
        return tr("Type in the search field to find tools, commands, settings, documents and help.");
    }

    const QString text = PDFAppSearchEngine::getVisibleText(m_results.text, PDFAppSearchPageHelper::MAXIMUM_SHOWN_TEXT);
    const int count = m_group ? m_results.getCount(*m_group) : m_results.getTotalCount();
    if (m_group)
    {
        const QString name = PDFAppSearchGroups::getName(*m_group);
        if (count <= 0)
        {
            return tr("No results for “%1” in %2.").arg(text, name);
        }
        return count == 1 ? tr("1 result for “%1” in %2.").arg(text, name) : tr("%n results for “%1” in %2.", nullptr, count).arg(text, name);
    }

    if (count <= 0)
    {
        return tr("No results for “%1” in tools, commands, settings, documents and help.").arg(text);
    }
    return count == 1 ? tr("1 result for “%1” in tools, commands, settings, documents and help.").arg(text)
                      : tr("%n results for “%1” in tools, commands, settings, documents and help.", nullptr, count).arg(text);
}

void PDFAppSearchPage::setCurrentTime(const QDateTime& now)
{
    m_now = now;
    refresh();
}

void PDFAppSearchPage::refresh()
{
    // Nothing is kept: the database is read again (newapp_storage.md 1)
    PDFAppSearchData searchData = PDFAppSearchData::load(getContext());
    searchData.now = m_now;
    m_results = m_engine.search(m_searchText, searchData);

    rebuildResults();
    updateChips();

    // The subtitle of the header has changed
    Q_EMIT titleChanged();
}

void PDFAppSearchPage::rebuildResults()
{
    if (!m_resultsCard)
    {
        return;
    }

    // The focused result is focused again (also after a notification)
    std::optional<std::pair<PDFAppSearchGroup, QString>> focusedKey;
    bool hadFocus = false;
    for (const QPointer<PDFAppSearchResultRow>& row : std::as_const(m_rows))
    {
        if (row && row->hasFocus())
        {
            focusedKey = std::make_pair(row->getResult().group, row->getResult().key);
            hadFocus = true;
        }
    }

    // The old content may hold the sender of the current signal (Show all)
    if (m_resultsContent)
    {
        m_resultsContent->hide();
        m_resultsContent->deleteLater();
        m_resultsContent = nullptr;
    }
    m_rows.clear();
    m_emptyState = nullptr;
    m_emptyTitle = nullptr;
    m_emptyText = nullptr;
    m_searchManualButton = nullptr;

    m_resultsContent = new QWidget(m_resultsCard);
    m_resultsContent->setObjectName(QStringLiteral("searchResultsContent"));
    QVBoxLayout* layout = new QVBoxLayout(m_resultsContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const int count = m_group ? m_results.getCount(*m_group) : m_results.getTotalCount();
    if (count <= 0)
    {
        m_emptyState = createEmptyState(m_resultsContent);
        layout->addStretch(1);
        layout->addWidget(m_emptyState);
        layout->addStretch(1);
        if (m_results.isEmptyQuery)
        {
            m_emptyTitle->setText(tr("Search everything"));
            m_emptyText->setText(tr("Type a word in the search field to find tools, commands, settings, documents and the user manual."));
            m_searchManualButton->hide();
        }
        else
        {
            const QString text = PDFAppSearchEngine::getVisibleText(m_results.text, PDFAppSearchPageHelper::MAXIMUM_SHOWN_TEXT);
            m_emptyTitle->setText(tr("No results for “%1”").arg(text));
            m_emptyText->setText(tr("Nothing matches this word. Check the spelling, try a shorter or more general word, or search the user manual."));
        }
    }
    else
    {
        for (const PDFAppSearchGroupResults& group : m_results.groups)
        {
            if ((m_group && group.group != *m_group) || group.totalCount <= 0)
            {
                continue;
            }

            // group-*: the title and the count of all results of the group
            PDFAppSearchGroupHeader* header = new PDFAppSearchGroupHeader(PDFAppSearchGroups::getTitle(group.group), QString::number(group.totalCount), m_resultsContent);
            header->setObjectName(QStringLiteral("group-%1").arg(PDFAppSearchGroups::getId(group.group)));
            layout->addWidget(header);

            // row-wrap: the rows 6 from the edges of the card
            QWidget* rows = new QWidget(m_resultsContent);
            rows->setObjectName(QStringLiteral("rows-%1").arg(PDFAppSearchGroups::getId(group.group)));
            QVBoxLayout* rowsLayout = new QVBoxLayout(rows);
            rowsLayout->setContentsMargins(PDFAppSearchPainter::ROW_WRAP_PADDING, 0, PDFAppSearchPainter::ROW_WRAP_PADDING, 0);
            rowsLayout->setSpacing(0);

            const size_t shown = m_group ? group.results.size() : std::min(group.results.size(), size_t(ALL_GROUP_ROWS));
            for (size_t i = 0; i < shown; ++i)
            {
                PDFAppSearchResultRow* row = new PDFAppSearchResultRow(group.results[i], rows);
                row->installEventFilter(this);
                connect(row, &QAbstractButton::clicked, this, &PDFAppSearchPage::onRowClicked);
                rowsLayout->addWidget(row);
                m_rows.append(row);
            }
            layout->addWidget(rows);

            if (!m_group && group.totalCount > int(shown))
            {
                PDFAppSearchShowAllButton* showAll = new PDFAppSearchShowAllButton(group.group, group.totalCount, m_resultsContent);
                connect(showAll, &QAbstractButton::clicked, this, &PDFAppSearchPage::onShowAllClicked);
                layout->addWidget(showAll);
            }
        }
    }

    m_resultsCard->getLayout()->addWidget(m_resultsContent);

    // The card of the empty state takes the rest of the main column, the results
    // keep their height (the stretch below the card takes the rest)
    QWidget* mainColumn = m_resultsCard->parentWidget();
    if (QBoxLayout* mainLayout = qobject_cast<QBoxLayout*>(mainColumn ? mainColumn->layout() : nullptr))
    {
        const bool isEmpty = count <= 0;
        const int cardIndex = mainLayout->indexOf(m_resultsCard);
        if (cardIndex >= 0)
        {
            mainLayout->setStretch(cardIndex, isEmpty ? 1 : 0);
            if (cardIndex + 1 < mainLayout->count() && mainLayout->itemAt(cardIndex + 1)->spacerItem())
            {
                mainLayout->setStretch(cardIndex + 1, isEmpty ? 0 : 1);
            }
        }
    }
    updateCurrentRow();

    if (hadFocus)
    {
        PDFAppSearchResultRow* focusRow = nullptr;
        for (const QPointer<PDFAppSearchResultRow>& row : std::as_const(m_rows))
        {
            if (row && focusedKey && row->getResult().group == focusedKey->first && row->getResult().key == focusedKey->second)
            {
                focusRow = row;
                break;
            }
        }
        if (!focusRow && !m_rows.isEmpty())
        {
            focusRow = m_rows.front();
        }
        if (focusRow)
        {
            focusRow->setFocus(Qt::OtherFocusReason);
        }
    }
}

void PDFAppSearchPage::updateChips()
{
    for (const auto& item : m_chips)
    {
        QPushButton* chip = item.second;
        if (!chip)
        {
            continue;
        }

        // Nothing was searched yet (Search everything): the chips have no counts
        // (-1 hides the count; "0" would say that nothing was found)
        const std::optional<PDFAppSearchGroup> group = item.first < 0 ? std::nullopt : std::make_optional(static_cast<PDFAppSearchGroup>(item.first));
        const int count = m_results.isEmptyQuery ? -1 : (group ? m_results.getCount(*group) : m_results.getTotalCount());
        if (chip->property(PDFWidgetStyleProperties::CHIP_COUNT).toInt() != count)
        {
            PDFWidgetStyleProperties::set(chip, PDFWidgetStyleProperties::CHIP_COUNT, count);
        }

        const bool isChecked = group == m_group;
        if (chip->isChecked() != isChecked)
        {
            chip->setChecked(isChecked);
        }
    }
}

PDFAppSearchResultRow* PDFAppSearchPage::getCurrentRow() const
{
    for (const QPointer<PDFAppSearchResultRow>& row : m_rows)
    {
        if (row && row->isCurrent())
        {
            return row;
        }
    }
    return nullptr;
}

void PDFAppSearchPage::updateCurrentRow()
{
    // The focused row, otherwise the first one (as the design shows it)
    PDFAppSearchResultRow* current = nullptr;
    for (const QPointer<PDFAppSearchResultRow>& row : std::as_const(m_rows))
    {
        if (row && row->hasFocus())
        {
            current = row;
            break;
        }
    }
    if (!current)
    {
        for (const QPointer<PDFAppSearchResultRow>& row : std::as_const(m_rows))
        {
            if (row)
            {
                current = row;
                break;
            }
        }
    }

    for (const QPointer<PDFAppSearchResultRow>& row : std::as_const(m_rows))
    {
        if (row)
        {
            row->setCurrent(row == current);
        }
    }
}

QList<PDFAppSearchResultRow*> PDFAppSearchPage::getRows() const
{
    QList<PDFAppSearchResultRow*> rows;
    for (const QPointer<PDFAppSearchResultRow>& row : m_rows)
    {
        if (row)
        {
            rows << row.data();
        }
    }
    return rows;
}

QList<PDFAppSearchShowAllButton*> PDFAppSearchPage::getShowAllButtons() const
{
    return m_resultsContent ? m_resultsContent->findChildren<PDFAppSearchShowAllButton*>() : QList<PDFAppSearchShowAllButton*>();
}

QList<PDFAppSearchGroupHeader*> PDFAppSearchPage::getGroupHeaders() const
{
    return m_resultsContent ? m_resultsContent->findChildren<PDFAppSearchGroupHeader*>() : QList<PDFAppSearchGroupHeader*>();
}

QPushButton* PDFAppSearchPage::getChip(std::optional<PDFAppSearchGroup> group) const
{
    auto it = m_chips.find(PDFAppSearchPageHelper::getChipKey(group));
    return it != m_chips.end() ? it->second.data() : nullptr;
}

void PDFAppSearchPage::setGroupFilter(std::optional<PDFAppSearchGroup> group)
{
    if (group == m_group)
    {
        updateChips();
        return;
    }

    m_group = group;
    rebuildResults();
    updateChips();
    if (!updateRouteInHistory())
    {
        // A slot of routeReplaced has deleted the page
        return;
    }
    Q_EMIT titleChanged();
}

bool PDFAppSearchPage::updateRouteInHistory()
{
    // The filter is the state of the route: the current entry of the history gets
    // the new route, the page stays (as the filters of Support)
    return replaceRoute(PDFAppRoutes::search(m_searchText, m_group ? PDFAppSearchGroups::getId(*m_group) : QString()));
}

void PDFAppSearchPage::updateSearchField()
{
    // The field of the header shows the text of the shown search
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget())
    {
        if (PDFAppTab* tab = qobject_cast<PDFAppTab*>(widget))
        {
            PDFLineEdit* field = tab->getSearchField();
            if (field && !field->hasFocus() && field->text() != m_searchText)
            {
                field->setText(m_searchText);
            }
            break;
        }
    }
}

void PDFAppSearchPage::clearSearch()
{
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget())
    {
        if (PDFAppTab* tab = qobject_cast<PDFAppTab*>(widget))
        {
            if (PDFLineEdit* field = tab->getSearchField())
            {
                field->clear();
                field->setFocus(Qt::ShortcutFocusReason);
            }
            break;
        }
    }
}

void PDFAppSearchPage::scheduleRefresh()
{
    // Several notifications of one change are joined
    if (!m_isRefreshScheduled)
    {
        m_isRefreshScheduled = true;
        QMetaObject::invokeMethod(this, &PDFAppSearchPage::performScheduledRefresh, Qt::QueuedConnection);
    }
}

void PDFAppSearchPage::performScheduledRefresh()
{
    m_isRefreshScheduled = false;
    refresh();
}

void PDFAppSearchPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFRecentRepository::getFileTopic() || topic == PDFShortcutRepository::getTopic())
    {
        scheduleRefresh();
    }
}

void PDFAppSearchPage::onEverythingChanged()
{
    scheduleRefresh();
}

void PDFAppSearchPage::onChipClicked(QAbstractButton* button)
{
    for (const auto& item : m_chips)
    {
        if (item.second && item.second.data() == button)
        {
            setGroupFilter(item.first < 0 ? std::nullopt : std::make_optional(static_cast<PDFAppSearchGroup>(item.first)));
            return;
        }
    }
}

void PDFAppSearchPage::onRowClicked()
{
    // The rows do not identify themselves in the signal
    PDFAppSearchResultRow* row = qobject_cast<PDFAppSearchResultRow*>(sender());
    if (!row)
    {
        return;
    }

    const PDFAppSearchResult result = row->getResult();
    activateResult(result);
}

void PDFAppSearchPage::onShowAllClicked()
{
    PDFAppSearchShowAllButton* button = qobject_cast<PDFAppSearchShowAllButton*>(sender());
    if (!button)
    {
        return;
    }

    // The button is deleted later with the old content; the focus goes to the first row
    const bool hadFocus = button->hasFocus();
    setGroupFilter(button->getGroup());
    if (hadFocus && !m_rows.isEmpty() && m_rows.front())
    {
        m_rows.front()->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppSearchPage::onSearchManualClicked()
{
    // The manual opens in the Help tab (owner decision O13)
    requestNavigation(PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()));
}

bool PDFAppSearchPage::activateResult(const PDFAppSearchResult& result)
{
    QPointer<PDFAppSearchPage> guard(this);
    switch (result.action)
    {
        case PDFAppSearchResult::Action::Navigate:
            return !result.route.isValid() || requestNavigation(result.route);

        case PDFAppSearchResult::Action::OpenUrl:
            Q_EMIT externalUrlRequested(result.url);
            break;

        case PDFAppSearchResult::Action::RunCommand:
            if (!PDFAppSearchActions::runCommand(this, result.commandId))
            {
                qWarning("The command %s of a search result cannot be run", qPrintable(result.commandId));
            }
            break;

        case PDFAppSearchResult::Action::OpenDocument:
            PDFAppSearchActions::openDocument(this, result.filePath);
            break;
    }
    return !guard.isNull();
}

void PDFAppSearchPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);
    updateSearchField();
}

void PDFAppSearchPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange && m_content)
    {
        // Every text of the page in the new language
        m_rows.clear();
        m_chips.clear();
        delete m_content.data();
        delete m_chipGroup.data();
        build();
        refresh();
    }
}

bool PDFAppSearchPage::eventFilter(QObject* watched, QEvent* event)
{
    PDFAppSearchResultRow* row = qobject_cast<PDFAppSearchResultRow*>(watched);
    const qsizetype index = row ? m_rows.indexOf(row) : -1;
    if (index < 0)
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            updateCurrentRow();
            break;

        case QEvent::KeyPress:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            if ((keyEvent->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
            {
                break;
            }

            qsizetype target = -1;
            switch (keyEvent->key())
            {
                case Qt::Key_Up:
                    target = index - 1;
                    break;
                case Qt::Key_Down:
                    target = index + 1;
                    break;
                case Qt::Key_Home:
                    target = 0;
                    break;
                case Qt::Key_End:
                    target = m_rows.size() - 1;
                    break;
                case Qt::Key_Escape:
                    clearSearch();
                    return true;
                default:
                    return BaseClass::eventFilter(watched, event);
            }

            // The first and the last row keep the focus
            if (target >= 0 && target < m_rows.size() && m_rows[target])
            {
                m_rows[target]->setFocus(Qt::TabFocusReason);
            }
            return true;
        }

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

PDFAppPage* PDFAppSearchPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppSearchPage(context, parent);
}

}   // namespace pdf
