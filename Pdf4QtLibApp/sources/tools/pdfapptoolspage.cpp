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

#include "pdfapptoolspage.h"
#include "pdfapphomewidgets.h"
#include "pdfappinfowidgets.h"
#include "pdfapplocale.h"
#include "pdfappminuteticker.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcategorycard.h"
#include "pdfapptoolmenu.h"
#include "pdfapptoolpresentation.h"
#include "pdfdatabasenotifier.h"
#include "pdftoolcollectionrepository.h"
#include "pdftoolrepository.h"

#include "pdfdocumenttoolcatalog.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QHideEvent>
#include <QShowEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the Tools page
class PDFAppToolsPageHelper
{
public:
    PDFAppToolsPageHelper() = delete;

    /// Text of the search as the route keeps it (sanitized, the route valid)
    static QString getRouteText(const QString& category, const QString& text)
    {
        return PDFAppRoutes::tools(category, text).getQueryValue(QStringLiteral("q"));
    }

    /// The category is a value of the filter: All (empty), Pinned or a category of the catalog
    static bool isValidCategory(const QString& category)
    {
        return category.isEmpty() || category == PDFAppToolsPage::getPinnedFilter() || PDFDocumentToolCatalog::findCategory(category).has_value();
    }

    /// Count of the key in the counts of the chips (0 = none)
    static int getCount(const std::map<QString, int>& counts, const QString& key)
    {
        auto it = counts.find(key);
        return it != counts.end() ? it->second : 0;
    }

    /// Count of the matches of a category in the hint of the search ("Pages 2")
    static QString getCountItem(const QString& name, int count)
    {
        //: A category and the number of its tools matching the search ("Pages 2")
        return QCoreApplication::translate("pdf::PDFAppToolsPage", "%1 %2").arg(name, PDFAppLocale::formatNumber(count));
    }
};

PDFAppToolsPage::Pending PDFAppToolsPage::s_pending;

PDFAppClearRecentToolsDialog::PDFAppClearRecentToolsDialog(QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), CARD_WIDTH, parent)
{
    setObjectName(QStringLiteral("clearRecentToolsDialog"));

    // Clear List (Secondary) at the leading edge, Cancel (Primary, the default)
    m_clearButton = addActionButton(QString(), false, true);
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_cancelButton = addActionButton(QString(), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_clearButton.data(), &QPushButton::clicked, this, &PDFAppClearRecentToolsDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppClearRecentToolsDialog::reject);
    updateTexts();
}

PDFAppClearRecentToolsDialog::~PDFAppClearRecentToolsDialog() = default;

void PDFAppClearRecentToolsDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppClearRecentToolsDialog::updateTexts()
{
    setTitle(tr("Clear the list of recent tools?"));
    setText(tr("The list of the tools you used recently is removed from the Tools page, Home and the Tools panel of documents. "
               "Pinned tools, collections and the suggestions of the next tools are kept."));
    if (m_clearButton)
    {
        m_clearButton->setText(tr("Clear List"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
}

PDFAppToolsPage::PDFAppToolsPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_pending(s_pending)
{
    setObjectName(QStringLiteral("toolsPage"));

    // Only the first page of a navigation takes the pending focus
    s_pending = Pending();

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(SEARCH_DELAY);
    connect(&m_searchTimer, &QTimer::timeout, this, &PDFAppToolsPage::onSearchTimeout);

    // The notifications of a burst (a pin, another instance) read the database once
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &PDFAppToolsPage::onRefreshTimeout);

    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(STATUS_TIMEOUT);
    connect(&m_statusTimer, &QTimer::timeout, this, &PDFAppToolsPage::onStatusTimeout);

    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);
    layout->addStretch(1);
    buildContent();

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppToolsPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppToolsPage::onEverythingChanged);
    }

    // O25: the times of Recent tools follow the clock while the page is visible
    m_minuteSubscription = new PDFAppMinuteSubscription(context ? context->getMinuteTicker() : nullptr, this);
    connect(m_minuteSubscription, &PDFAppMinuteSubscription::minuteChanged, this, &PDFAppToolsPage::onMinuteChanged);

    refresh();
}

PDFAppToolsPage::~PDFAppToolsPage()
{
    // The dialog belongs to the window: it goes with the page, its answer is not
    // delivered to a dying page (hidden at once, deleted later)
    if (m_clearDialog)
    {
        PDFAppClearRecentToolsDialog* dialog = m_clearDialog;
        m_clearDialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

void PDFAppToolsPage::buildContent()
{
    ++m_rowBuildCount;

    // tools-grid 272:12862: main column 802 and side column 340, gap 24
    m_columns = PDFAppColumns::createMainAndSide(this, MINIMUM_MAIN_WIDTH);
    m_columns->setObjectName(QStringLiteral("toolsGrid"));
    if (QBoxLayout* layout = qobject_cast<QBoxLayout*>(this->layout()))
    {
        layout->insertWidget(0, m_columns);
    }

    m_tileGroup = new PDFAppToolFocusGroup(this, m_columns);
    m_rowGroup = new PDFAppToolFocusGroup(this, m_columns);
    m_collectionGroup = new PDFAppToolFocusGroup(this, m_columns);
    m_recentGroup = new PDFAppToolFocusGroup(this, m_columns);
    m_nearestGroup = new PDFAppToolFocusGroup(this, m_columns);

    // main-column 272:12863: toolbar, (the hint of the search), pinned, all tools, gap 24
    QBoxLayout* main = m_columns->getMainLayout();
    QWidget* mainColumn = m_columns->getMainColumn();
    main->addWidget(createToolbar(mainColumn));

    // A refused write (a busy or read-only database)
    m_statusLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextDanger, mainColumn);
    m_statusLabel->setObjectName(QStringLiteral("statusMessage"));
    m_statusLabel->hide();
    main->addWidget(m_statusLabel);

    main->addWidget(createSearchHint(mainColumn));
    main->addWidget(createPinnedSection(mainColumn));
    main->addWidget(createAllToolsSection(mainColumn));
    main->addStretch(1);

    // side-column 272:12942: collections and recent tools, gap 24
    QBoxLayout* side = m_columns->getSideLayout();
    QWidget* sideColumn = m_columns->getSideColumn();
    side->addWidget(createCollectionsSection(sideColumn));
    side->addWidget(createRecentSection(sideColumn));
    side->addStretch(1);

    m_tilesSignature.clear();
    m_collectionsSignature.clear();
    m_recentSignature.clear();
    m_collectionCardSignature.clear();
}

void PDFAppToolsPage::rebuild()
{
    const int scrollPosition = isVisible() ? getScrollPosition() : -1;
    const QString focusName = PDFAppPageRebuild::getFocusedName(this);
    const QString fieldText = m_searchField ? m_searchField->text() : m_searchText;
    const int cursorPosition = m_searchField ? m_searchField->cursorPosition() : int(fieldText.size());

    if (m_menu)
    {
        PDFAppToolMenu* menu = m_menu;
        m_menu = nullptr;
        menu->disconnect(this);
        menu->hide();
        menu->deleteLater();
    }

    m_chips.clear();
    m_tiles.clear();
    m_categoryCards.clear();
    m_toolRows.clear();
    m_collectionRows.clear();
    m_recentRows.clear();
    m_nearestRows.clear();
    m_nearestTools.clear();
    delete m_chipGroup.data();
    delete m_columns.data();
    buildContent();
    m_columns->show();

    if (m_searchField)
    {
        m_searchField->setText(fieldText);
        m_searchField->setCursorPosition(cursorPosition);
    }
    refresh();

    if (scrollPosition >= 0)
    {
        PDFAppPageRebuild::finishLayout();
        setScrollPosition(scrollPosition);
        requestDeferredScroll(scrollPosition, QString());
    }
    PDFAppPageRebuild::restoreFocus(this, focusName);
}

QWidget* PDFAppToolsPage::createToolbar(QWidget* parent)
{
    // toolbar 272:12864: the tool search and the chips, gap 12
    QWidget* toolbar = new QWidget(parent);
    toolbar->setObjectName(QStringLiteral("toolbar"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(toolbar, 12);

    m_searchField = new PDFLineEdit(toolbar);
    m_searchField->setObjectName(QStringLiteral("toolSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search tools by name, action or keyword…"));
    m_searchField->setAccessibleName(tr("Search tools"));
    m_searchField->setMaxLength(int(PDFSearchQuery::MAXIMUM_LENGTH));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->installEventFilter(this);
    connect(m_searchField.data(), &QLineEdit::textEdited, this, &PDFAppToolsPage::onSearchTextEdited);
    layout->addWidget(m_searchField);

    // row-filters 272:12867: All, Pinned and the six categories, gap 8
    QWidget* chips = new QWidget(toolbar);
    chips->setObjectName(QStringLiteral("chips"));
    chips->setAccessibleName(tr("Categories"));
    PDFAppFlowLayout* chipsLayout = new PDFAppFlowLayout(chips, 8, 8);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    chipsLayout->addWidget(createChip(QString(), tr("All"), chips));
    chipsLayout->addWidget(createChip(getPinnedFilter(), tr("Pinned"), chips));
    for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
    {
        chipsLayout->addWidget(createChip(category.getId(), category.getChipText(), chips));
    }
    connect(m_chipGroup.data(), &QButtonGroup::buttonClicked, this, &PDFAppToolsPage::onChipClicked);
    layout->addWidget(chips);
    return toolbar;
}

QPushButton* PDFAppToolsPage::createChip(const QString& category, const QString& text, QWidget* parent)
{
    QPushButton* chip = new QPushButton(PDFAppPageWidgets::escapeMnemonic(text), parent);
    chip->setObjectName(category.isEmpty() ? QStringLiteral("chip-all") : QStringLiteral("chip-%1").arg(category));
    chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
    chip->setProperty(PDFWidgetStyleProperties::CHIP_COUNT, 0);
    chip->setCheckable(true);
    chip->setAutoDefault(false);
    chip->setAccessibleName(text);
    m_chipGroup->addButton(chip);
    m_chips[category] = chip;
    return chip;
}

QWidget* PDFAppToolsPage::createSearchHint(QWidget* parent)
{
    // search-hint 530:3946: the icon info 14, the texts Regular 12 text/secondary and
    // the link show all N matches, gap 6
    m_searchHint = new QWidget(parent);
    m_searchHint->setObjectName(QStringLiteral("searchHint"));
    QHBoxLayout* layout = new QHBoxLayout(m_searchHint);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, m_searchHint);
    icon->setColors(std::nullopt, PDFThemeColors::IconMuted);
    layout->addWidget(icon, 0, Qt::AlignTop);

    m_searchHintText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextSecondary, m_searchHint);
    m_searchHintText->setObjectName(QStringLiteral("searchHintText"));
    layout->addWidget(m_searchHintText, 0, Qt::AlignTop);

    m_showAllLink = new PDFTextLink(QString(), m_searchHint);
    m_showAllLink->setObjectName(QStringLiteral("showAllMatches"));
    m_showAllLink->setLinkSize(PDFComponentPainter::TextLinkSize::Small);
    connect(m_showAllLink.data(), &PDFTextLink::clicked, this, &PDFAppToolsPage::onShowAllClicked);
    layout->addWidget(m_showAllLink, 0, Qt::AlignTop);
    layout->addStretch(1);

    m_searchHint->hide();
    return m_searchHint;
}

QWidget* PDFAppToolsPage::createPinnedSection(QWidget* parent)
{
    // pinned 272:12876: the header and the tiles 259 x 88 in rows of three, gaps 12
    m_pinnedSection = new QWidget(parent);
    m_pinnedSection->setObjectName(QStringLiteral("pinnedTools"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(m_pinnedSection, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Pinned tools"), PDFAppPageWidgets::createHint(tr("Pin or unpin any tool with its ★"), m_pinnedSection), m_pinnedSection));

    m_tileGrid = new PDFAppGrid(TILE_MINIMUM_WIDTH, 3, 12, 12, m_pinnedSection);
    m_tileGrid->setObjectName(QStringLiteral("pinnedTiles"));
    m_tileGrid->setAccessibleName(tr("Pinned tools"));
    layout->addWidget(m_tileGrid);

    // No pinned tool (a missing frame of Figma: the small empty state of Home)
    PDFAppCard* emptyCard = PDFAppPageWidgets::createCard(8, 0, m_pinnedSection);
    emptyCard->setObjectName(QStringLiteral("pinnedEmptyCard"));
    m_pinnedEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("star"), emptyCard);
    m_pinnedEmpty->setObjectName(QStringLiteral("pinnedEmpty"));
    m_pinnedEmpty->setTexts(tr("No pinned tools"), tr("Pin a tool with its ★ to keep it here and on Home."));
    emptyCard->getLayout()->addWidget(m_pinnedEmpty);
    emptyCard->hide();
    layout->addWidget(emptyCard);
    return m_pinnedSection;
}

QWidget* PDFAppToolsPage::createAllToolsSection(QWidget* parent)
{
    // all-tools 272:12888: the header and the category cards, gap 12
    m_allToolsSection = new QWidget(parent);
    m_allToolsSection->setObjectName(QStringLiteral("allTools"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(m_allToolsSection, 12);

    // The trailing part of the header: the hint ("46 tools in 6 categories") or
    // the link Edit of a collection
    QWidget* trailing = new QWidget(m_allToolsSection);
    QHBoxLayout* trailingLayout = new QHBoxLayout(trailing);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(0);
    m_allToolsHint = PDFAppPageWidgets::createHint(QString(), trailing);
    m_allToolsHint->setObjectName(QStringLiteral("allToolsHint"));
    trailingLayout->addWidget(m_allToolsHint);
    m_editCollectionLink = new PDFTextLink(tr("Edit"), trailing);
    m_editCollectionLink->setObjectName(QStringLiteral("editCollection"));
    connect(m_editCollectionLink.data(), &PDFTextLink::clicked, this, &PDFAppToolsPage::onCollectionClicked);
    m_editCollectionLink->hide();
    trailingLayout->addWidget(m_editCollectionLink);
    m_allToolsHeader = PDFAppPageWidgets::createSectionHeader(tr("All tools"), trailing, m_allToolsSection);
    m_allToolsHeader->setObjectName(QStringLiteral("allToolsHeader"));
    layout->addWidget(m_allToolsHeader);

    m_categoryGrid = new PDFAppToolCategoryGrid(m_allToolsSection);
    m_categoryGrid->setObjectName(QStringLiteral("categoryGrid"));
    m_categoryGrid->setAccessibleName(tr("All tools"));
    for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
    {
        PDFAppToolCategoryCard* card = new PDFAppToolCategoryCard(category.getTitle(), category.getDescription(),
                                                                  PDFAppToolPresentation::getToolCountText(category.getToolCount()),
                                                                  category.getIconName(), PDFAppToolPresentation::getHue(category.getHue()), m_categoryGrid);
        card->setObjectName(QStringLiteral("category-") + category.getId());

        QString subgroupId;
        bool isFirst = true;
        for (const PDFDocumentToolCatalogEntry* tool : PDFDocumentToolCatalog::getTools(category.getCategory()))
        {
            if (!tool->getSubgroupId().isEmpty() && (isFirst || tool->getSubgroupId() != subgroupId))
            {
                card->addCaption(tool->getSubgroupTitle());
            }
            subgroupId = tool->getSubgroupId();
            isFirst = false;

            PDFToolRow* row = PDFAppToolPresentation::createToolRow(*tool, false, card);
            prepareToolWidget(row);
            connect(row, &PDFToolRow::clicked, this, &PDFAppToolsPage::onToolClicked);
            connect(row, &PDFToolRow::pinnedChanged, this, &PDFAppToolsPage::onToolPinnedChanged);
            card->addRow(row);
            m_toolRows[tool->getId()] = row;
            m_rowGroup->addWidget(row);
        }
        m_categoryGrid->addCard(card);
        m_categoryCards.append(card);
    }
    layout->addWidget(m_categoryGrid);

    // The tools of a collection (Q1): created by updateCollectionCard
    layout->addWidget(createNoResults(m_allToolsSection));
    layout->addWidget(createNearestSection(m_allToolsSection));
    return m_allToolsSection;
}

QWidget* PDFAppToolsPage::createNoResults(QWidget* parent)
{
    // A search without a match (a missing frame of Figma: the look of 955:142130),
    // no pinned tool for the chip Pinned, a deleted collection
    PDFAppCard* card = PDFAppPageWidgets::createCard(40, 0, parent);
    card->setObjectName(QStringLiteral("noResults"));
    QVBoxLayout* layout = card->getLayout();

    m_noResultsIcon = new PDFIconBadge(QStringLiteral("search"), 72, 32, card);
    m_noResultsIcon->setObjectName(QStringLiteral("noResultsIcon"));
    m_noResultsIcon->setColors(PDFThemeColors::AccentNeutralBg, PDFThemeColors::IconMuted);
    layout->addWidget(m_noResultsIcon, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    m_noResultsTitle = PDFAppPageWidgets::createLabel(QString(), QFont::Bold, 22, PDFThemeColors::TextPrimary, card);
    m_noResultsTitle->setObjectName(QStringLiteral("noResultsTitle"));
    m_noResultsTitle->setAlignment(Qt::AlignCenter);
    m_noResultsTitle->setWordWrap(true);
    m_noResultsTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_noResultsTitle);
    layout->addSpacing(8);

    m_noResultsText = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 14), 21, PDFThemeColors::TextTertiary, card);
    m_noResultsText->setObjectName(QStringLiteral("noResultsText"));
    m_noResultsText->setAlignment(Qt::AlignHCenter);
    m_noResultsText->setMaximumWidth(460);
    layout->addWidget(m_noResultsText, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    QWidget* buttons = new QWidget(card);
    PDFAppFlowLayout* buttonsLayout = new PDFAppFlowLayout(buttons, 8, 8);
    m_clearSearchButton = PDFAppPageWidgets::createButton(tr("Clear Search"), QStringLiteral("x"), false, buttons);
    m_clearSearchButton->setObjectName(QStringLiteral("clearSearchButton"));
    connect(m_clearSearchButton.data(), &QPushButton::clicked, this, &PDFAppToolsPage::onClearSearchClicked);
    buttonsLayout->addWidget(m_clearSearchButton);
    m_searchEverywhereButton = PDFAppPageWidgets::createButton(tr("Search Everywhere"), QStringLiteral("search"), true, buttons);
    m_searchEverywhereButton->setObjectName(QStringLiteral("searchEverywhereButton"));
    connect(m_searchEverywhereButton.data(), &QPushButton::clicked, this, &PDFAppToolsPage::onSearchEverywhereClicked);
    buttonsLayout->addWidget(m_searchEverywhereButton);
    layout->addWidget(buttons, 0, Qt::AlignHCenter);

    card->hide();
    m_noResults = card;
    return card;
}

QWidget* PDFAppToolsPage::createNearestSection(QWidget* parent)
{
    // The closest tools of a search without a match (owner decision O25, tools.md
    // 9.1 point 4; a missing frame of Figma built from the page: the section header
    // and the list card of the collections 272:12943 - padding 8, gap 2 - with the
    // tool rows of the category cards). Filled by updateNearestTools.
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("nearestTools"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    PDFAppSectionHeader* header = PDFAppPageWidgets::createSectionHeader(tr("Closest tools"), nullptr, section);
    header->setObjectName(QStringLiteral("nearestToolsHeader"));
    layout->addWidget(header);

    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("nearestToolsList"));
    card->setAccessibleName(tr("Closest tools"));
    layout->addWidget(card);

    section->hide();
    m_nearestList = card;
    m_nearestSection = section;
    return section;
}

QWidget* PDFAppToolsPage::createCollectionsSection(QWidget* parent)
{
    // collections 272:12943: the header with Manage, the list card (padding 8, gap 2)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("collections"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);

    m_manageLink = new PDFTextLink(tr("Manage"), section);
    m_manageLink->setObjectName(QStringLiteral("manageCollections"));
    withLink(m_manageLink, PDFAppRoutes::toolCollections().toString());
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Collections"), m_manageLink, section));

    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("collectionsList"));
    card->setAccessibleName(tr("Collections"));
    m_collectionsCard = card;
    m_collectionsLayout = card->getLayout();

    // new-collection 272:12952
    m_newCollectionRow = new PDFCollectionRow(tr("New collection…"), tr("Group the tools you use for one task"), card);
    m_newCollectionRow->setObjectName(QStringLiteral("newCollectionRow"));
    m_newCollectionRow->setIconName(QStringLiteral("plus-circle"));
    m_newCollectionRow->setCreateVariant(true);
    connect(m_newCollectionRow.data(), &PDFCollectionRow::clicked, this, &PDFAppToolsPage::onNewCollectionClicked);
    m_collectionsLayout->addWidget(m_newCollectionRow);
    m_collectionGroup->addWidget(m_newCollectionRow);

    layout->addWidget(card);
    return section;
}

QWidget* PDFAppToolsPage::createRecentSection(QWidget* parent)
{
    // recent-tools 272:12958: the header with Clear, the list card (padding 8, gap 2)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("recentTools"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);

    m_clearRecentLink = new PDFTextLink(tr("Clear"), section);
    m_clearRecentLink->setObjectName(QStringLiteral("clearRecentTools"));
    connect(m_clearRecentLink.data(), &PDFTextLink::clicked, this, &PDFAppToolsPage::onClearRecentClicked);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Recent tools"), m_clearRecentLink, section));

    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("recentToolsList"));
    card->setAccessibleName(tr("Recent tools"));
    m_recentCard = card;
    m_recentLayout = card->getLayout();

    // The list is empty (all of phase 1, a missing frame of Figma): the small empty state of Home
    m_recentEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("tools"), card);
    m_recentEmpty->setObjectName(QStringLiteral("recentToolsEmpty"));
    m_recentEmpty->setTexts(tr("No recent tools"), tr("Tools you use appear here."));
    m_recentLayout->addWidget(m_recentEmpty);

    layout->addWidget(card);
    return section;
}

void PDFAppToolsPage::prepareToolWidget(QWidget* widget)
{
    // The context menu (also the menu key and Shift+F10, tools.md 9.7)
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this, &PDFAppToolsPage::onToolContextMenu);

    // The group is one stop of Tab: the stars are reached by the mouse and the
    // context menu (Pin / Unpin), not by Tab
    PDFStarButton* star = nullptr;
    if (PDFToolRow* row = qobject_cast<PDFToolRow*>(widget))
    {
        star = row->getStarButton();
    }
    else if (PDFQuickActionTile* tile = qobject_cast<PDFQuickActionTile*>(widget))
    {
        star = tile->getStarButton();
    }
    if (star)
    {
        star->setFocusPolicy(Qt::NoFocus);
    }
}

bool PDFAppToolsPage::setRoute(const PDFAppRoute& route)
{
    const QString collection = route.getQueryValue(QStringLiteral("collection"));
    const QString category = route.getQueryValue(QStringLiteral("category"));
    const QString searchText = route.getQueryValue(QStringLiteral("q"));

    // The route table validates the values; the page refuses what it cannot show:
    // a collection together with a filter, an unknown collection (Q1), a text,
    // which is not a text of the route
    if (!PDFAppToolsPageHelper::isValidCategory(category) || !route.getFragment().isEmpty() ||
        PDFAppToolsPageHelper::getRouteText(category, searchText) != searchText)
    {
        return false;
    }
    if (!collection.isEmpty() && (!category.isEmpty() || !searchText.isEmpty() || !m_state.findCollection(collection)))
    {
        return false;
    }

    m_collection = collection;
    m_category = category;
    m_searchText = searchText;

    if (m_searchField)
    {
        // The text of the field typed before the Push of the filter is kept (a
        // space at its end, which the route does not keep)
        QString fieldText = searchText;
        int cursorPosition = -1;
        if (m_pending.focus == PendingFocus::Search && PDFAppToolsPageHelper::getRouteText(category, m_pending.fieldText) == searchText)
        {
            fieldText = m_pending.fieldText;
            cursorPosition = m_pending.cursorPosition;
        }
        if (m_searchField->text() != fieldText)
        {
            m_searchField->setText(fieldText);
        }
        if (cursorPosition >= 0)
        {
            m_searchField->setCursorPosition(cursorPosition);
        }
    }

    applyFilter();
    return BaseClass::setRoute(route);
}

QString PDFAppToolsPage::getTitle() const
{
    return tr("Tools");
}

QString PDFAppToolsPage::getHeaderTitle() const
{
    return tr("Tools");
}

QString PDFAppToolsPage::getHeaderSubtitle() const
{
    return tr("Every PDF4QT tool in one place. Search, browse by category, and pin the ones you use most.");
}

bool PDFAppToolsPage::hasSearchHeader() const
{
    return true;
}

bool PDFAppToolsPage::setFilter(const QString& category, const QString& searchText)
{
    Pending pending;
    pending.focus = PendingFocus::None;
    return changeFilter(category, searchText, pending);
}

bool PDFAppToolsPage::changeFilter(const QString& category, const QString& searchText, const Pending& pending)
{
    const QString validCategory = PDFAppToolsPageHelper::isValidCategory(category) ? category : QString();
    const QString text = PDFAppToolsPageHelper::getRouteText(validCategory, searchText);
    const PDFAppRoute route = PDFAppRoutes::tools(validCategory, text);

    if (!m_collection.isEmpty())
    {
        // A filter of a collection shows the catalog: a new entry of the history.
        // An emptied search field changes nothing, the chip All shows the whole
        // catalog (the exclusive group has checked it - it would stay checked over
        // the collection)
        if (validCategory.isEmpty() && text.isEmpty() && pending.focus != PendingFocus::Chip)
        {
            return true;
        }
        return navigateTo(route, PDFAppNavigationMode::Push, pending);
    }

    if (validCategory == m_category && text == m_searchText)
    {
        return true;
    }

    if (!isFiltered())
    {
        // The first filter: the filtered page is one entry of the history (Back
        // returns to the whole catalog)
        return navigateTo(route, PDFAppNavigationMode::Push, pending);
    }

    // A filter cleared to the route of the previous entry of the history (the whole
    // catalog before the search) goes Back to it - no two equal entries (O25); the
    // page of the entry takes the focus of the field
    if (isPreviousRoute(route))
    {
        s_pending = pending;
        const bool isAlive = requestBack();
        s_pending = Pending();
        return isAlive;
    }

    // Further changes replace the route of the shown page (the focus stays)
    m_category = validCategory;
    m_searchText = text;
    applyFilter();
    return replaceRoute(route);
}

void PDFAppToolsPage::refresh()
{
    m_refreshTimer.stop();

    PDFAppContext* context = getContext();
    m_state = PDFAppToolState::read(context ? context->getDatabase() : nullptr, RECENT_TOOLS_LIMIT);
    m_isStateCurrent = !m_isPageHidden;
    ++m_refreshCount;

    updateStars();
    updatePinnedTiles();
    updateCollections();
    updateRecentTools();
    applyFilter();
}

bool PDFAppToolsPage::setToolPinned(const QString& toolId, bool isPinned)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return false;
    }

    PDFAppContext* context = getContext();
    PDFDatabaseError error = PDFDatabaseError::fromMessage(tr("No database is open."));
    if (context && context->getDatabase())
    {
        error = context->getTools().setPinned(toolId, isPinned);
    }

    if (error.isError())
    {
        // The optimistic star goes back to the stored state
        setStarsOf(toolId, m_state.isPinned(toolId));
        const QString text = error.isBusy() ? tr("Another PDF4QT window is saving its settings. Try it again in a moment.")
                                            : tr("The pin could not be saved: %1").arg(error.getMessage());
        showStatusMessage(text);
        return false;
    }

    // The widgets of the tool follow at once, the tiles and the counts after the notification
    setStarsOf(toolId, isPinned);
    if (isPinned && !m_state.pinnedTools.contains(toolId))
    {
        m_state.pinnedTools.append(toolId);
    }
    else if (!isPinned)
    {
        m_state.pinnedTools.removeAll(toolId);
    }
    scheduleRefresh();
    return true;
}

PDFAppToolMenu* PDFAppToolsPage::showToolMenu(const QString& toolId, const QPoint& globalPosition)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return nullptr;
    }

    if (m_menu)
    {
        PDFAppToolMenu* previous = m_menu;
        m_menu = nullptr;
        previous->disconnect(this);
        previous->hide();
        previous->deleteLater();
    }

    // The menu belongs to the page (it goes with it) and deletes itself, when it is hidden
    PDFAppToolMenu* menu = new PDFAppToolMenu(toolId, m_state.isPinned(toolId), m_state.collections, this);
    connect(menu, &PDFAppToolMenu::openRequested, this, &PDFAppToolsPage::onMenuOpenRequested);
    connect(menu, &PDFAppToolMenu::pinRequested, this, &PDFAppToolsPage::onMenuPinRequested);
    connect(menu, &PDFAppToolMenu::collectionToggled, this, &PDFAppToolsPage::onMenuCollectionToggled);
    connect(menu, &PDFAppToolMenu::newCollectionRequested, this, &PDFAppToolsPage::onMenuNewCollectionRequested);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    m_menu = menu;
    menu->popup(globalPosition);
    return menu;
}

PDFAppClearRecentToolsDialog* PDFAppToolsPage::openClearRecentDialog()
{
    if (m_clearDialog)
    {
        m_clearDialog->raise();
        return m_clearDialog;
    }

    if (m_state.usage.empty())
    {
        return nullptr;
    }

    PDFAppClearRecentToolsDialog* dialog = new PDFAppClearRecentToolsDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppToolsPage::onClearDialogFinished);
    m_clearDialog = dialog;
    dialog->open();
    return dialog;
}

QPushButton* PDFAppToolsPage::getChip(const QString& category) const
{
    auto it = m_chips.find(category);
    return it != m_chips.end() ? it->second.data() : nullptr;
}

QList<QPushButton*> PDFAppToolsPage::getChips() const
{
    QList<QPushButton*> chips;
    QStringList order({ QString(), getPinnedFilter() });
    for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
    {
        order << category.getId();
    }
    for (const QString& category : std::as_const(order))
    {
        if (QPushButton* chip = getChip(category))
        {
            chips.append(chip);
        }
    }
    return chips;
}

PDFToolRow* PDFAppToolsPage::findToolRow(const QString& toolId) const
{
    // The tools of a shown collection have their own rows
    if (!m_collection.isEmpty() && m_collectionCard)
    {
        for (PDFToolRow* row : m_collectionCard->getRows())
        {
            if (PDFAppToolPresentation::getToolId(row) == toolId)
            {
                return row;
            }
        }
        return nullptr;
    }

    auto it = m_toolRows.find(toolId);
    return it != m_toolRows.end() ? it->second.data() : nullptr;
}

QList<PDFToolRow*> PDFAppToolsPage::getNearestRows() const
{
    QList<PDFToolRow*> rows;
    for (const QPointer<PDFToolRow>& row : m_nearestRows)
    {
        if (row)
        {
            rows.append(row.data());
        }
    }
    return rows;
}

QList<PDFToolRow*> PDFAppToolsPage::getVisibleToolRows() const
{
    QList<PDFToolRow*> rows;
    if (m_rowGroup)
    {
        for (QWidget* widget : m_rowGroup->getVisibleWidgets())
        {
            if (PDFToolRow* row = qobject_cast<PDFToolRow*>(widget))
            {
                rows.append(row);
            }
        }
    }
    return rows;
}

PDFQuickActionTile* PDFAppToolsPage::findTile(const QString& toolId) const
{
    for (PDFQuickActionTile* tile : getTiles())
    {
        if (PDFAppToolPresentation::getToolId(tile) == toolId)
        {
            return tile;
        }
    }
    return nullptr;
}

QList<PDFQuickActionTile*> PDFAppToolsPage::getTiles() const
{
    QList<PDFQuickActionTile*> tiles;
    for (const QPointer<PDFQuickActionTile>& tile : m_tiles)
    {
        if (tile)
        {
            tiles.append(tile.data());
        }
    }
    return tiles;
}

QList<PDFAppToolCategoryCard*> PDFAppToolsPage::getCategoryCards() const
{
    QList<PDFAppToolCategoryCard*> cards;
    for (const QPointer<PDFAppToolCategoryCard>& card : m_categoryCards)
    {
        if (card)
        {
            cards.append(card.data());
        }
    }
    return cards;
}

QList<PDFCollectionRow*> PDFAppToolsPage::getCollectionRows() const
{
    QList<PDFCollectionRow*> rows;
    for (const QPointer<PDFCollectionRow>& row : m_collectionRows)
    {
        if (row)
        {
            rows.append(row.data());
        }
    }
    return rows;
}

QList<PDFRecentToolRow*> PDFAppToolsPage::getRecentRows() const
{
    QList<PDFRecentToolRow*> rows;
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        if (row)
        {
            rows.append(row.data());
        }
    }
    return rows;
}

void PDFAppToolsPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    if (!event->spontaneous())
    {
        // The data are read, when the page is shown (nothing is cached); a new
        // page has read them in its constructor already (the notifications keep
        // them current, the times of the recent tools are read again after a hide)
        m_isPageHidden = false;
        if (!m_isStateCurrent)
        {
            refresh();
        }
        else
        {
            // The kept rows show the times of the current minute at once (O25)
            updateRecentTimes(PDFAppHomeFormat::getCurrentTime());
        }
        if (m_pending.focus != PendingFocus::None)
        {
            QMetaObject::invokeMethod(this, &PDFAppToolsPage::applyPendingFocus, Qt::QueuedConnection);
        }
    }
}

void PDFAppToolsPage::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    if (!event->spontaneous())
    {
        m_isPageHidden = true;
        m_isStateCurrent = false;
        m_refreshTimer.stop();
    }
}

void PDFAppToolsPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_columns)
    {
        rebuild();
    }
}

bool PDFAppToolsPage::eventFilter(QObject* watched, QEvent* event)
{
    // Esc in the search field clears the search (the second Esc goes on)
    if (watched == m_searchField.data() && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier && !m_searchField->text().isEmpty())
        {
            m_searchField->clear();
            m_searchTimer.stop();
            onSearchTimeout();
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolsPage::applyFilter()
{
    if (!m_categoryGrid)
    {
        return;
    }

    if (!m_collection.isEmpty())
    {
        applyCollection();
        return;
    }

    const PDFSearchQuery query(m_searchText);
    const bool isSearch = !query.isEmpty();
    std::map<QString, PDFDocumentToolSearchMatch> matches;
    if (isSearch)
    {
        for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::search(query))
        {
            if (match.tool)
            {
                matches[match.tool->getId()] = match;
            }
        }
    }

    const bool isPinnedFilter = m_category == getPinnedFilter();
    const std::optional<PDFDocumentToolCategory> category = PDFDocumentToolCatalog::findCategory(m_category);

    std::map<QString, int> counts;
    int visibleCount = 0;
    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        const QString toolId = tool.getId();
        auto match = matches.find(toolId);
        const bool isMatch = !isSearch || match != matches.end();
        const bool isPinned = m_state.isPinned(toolId);
        const QString categoryId = PDFDocumentToolCatalog::getCategoryId(tool.getCategory());
        if (isMatch)
        {
            ++counts[QString()];
            ++counts[categoryId];
            if (isPinned)
            {
                ++counts[getPinnedFilter()];
            }
        }

        const bool isInFilter = m_category.isEmpty() || (isPinnedFilter && isPinned) || (category && tool.getCategory() == *category);
        const bool isVisible = isMatch && isInFilter;
        visibleCount += isVisible ? 1 : 0;

        auto rowIt = m_toolRows.find(toolId);
        PDFToolRow* row = rowIt != m_toolRows.end() ? rowIt->second.data() : nullptr;
        if (!row)
        {
            continue;
        }

        row->setHidden(!isVisible);

        // Q3: a tool found only by a keyword says which keyword
        const bool isKeyword = isSearch && match != matches.end() && match->second.field == PDFDocumentToolSearchMatch::Field::Keyword && !match->second.keyword.isEmpty();
        const QString description = isKeyword ? PDFAppToolPresentation::getMatchReasonText(match->second.keyword) : tool.getDescription();
        if (row->getDescription() != description)
        {
            row->setDescription(description);
        }
    }

    for (const QPointer<PDFAppToolCategoryCard>& card : std::as_const(m_categoryCards))
    {
        if (card)
        {
            card->updateVisibility();
        }
    }
    m_categoryGrid->setHidden(visibleCount == 0);
    m_categoryGrid->updateCards();
    if (m_collectionCard)
    {
        m_collectionCard->hide();
    }
    if (m_pinnedSection)
    {
        m_pinnedSection->setHidden(isFiltered());
    }

    updateChips(counts, isSearch);
    updateTexts(visibleCount, counts, isSearch);
    updateNearestTools(isSearch && counts[QString()] == 0);
    updateFocusStops();
}

void PDFAppToolsPage::applyCollection()
{
    updateCollectionCard();

    const PDFAppToolCollection* collection = findShownCollection();
    m_categoryGrid->hide();
    if (m_pinnedSection)
    {
        m_pinnedSection->hide();
    }
    if (m_searchHint)
    {
        m_searchHint->hide();
    }
    if (m_collectionCard)
    {
        m_collectionCard->setHidden(!collection);
    }

    // The chips count the whole catalog, none is checked
    std::map<QString, int> counts;
    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        ++counts[QString()];
        ++counts[PDFDocumentToolCatalog::getCategoryId(tool.getCategory())];
        if (m_state.isPinned(tool.getId()))
        {
            ++counts[getPinnedFilter()];
        }
    }
    updateChips(counts, false);
    updateTexts(collection ? int(collection->toolIds.size()) : 0, counts, false);
    updateNearestTools(false);
    updateFocusStops();
}

void PDFAppToolsPage::updateChips(const std::map<QString, int>& counts, bool isSearch)
{
    const bool isCollection = !m_collection.isEmpty();
    for (const auto& [category, chip] : m_chips)
    {
        if (!chip)
        {
            continue;
        }

        auto it = counts.find(category);
        const int count = it != counts.end() ? it->second : 0;
        if (chip->property(PDFWidgetStyleProperties::CHIP_COUNT).toInt() != count)
        {
            PDFWidgetStyleProperties::set(chip, PDFWidgetStyleProperties::CHIP_COUNT, count);
            chip->updateGeometry();
        }
        chip->setAccessibleDescription(PDFAppToolPresentation::getToolCountText(count));

        // A chip without a match is disabled during a search (530:2957); the chip
        // of the filter and every chip outside of a search stay enabled
        const bool isCurrent = !isCollection && category == m_category;
        chip->setEnabled(!isSearch || count > 0 || isCurrent);
    }

    // The chip of the filter is checked (an exclusive group: one stop of the
    // focus); a collection checks none
    if (isCollection)
    {
        if (QAbstractButton* checked = m_chipGroup ? m_chipGroup->checkedButton() : nullptr)
        {
            m_chipGroup->setExclusive(false);
            checked->setChecked(false);
            m_chipGroup->setExclusive(true);
        }
    }
    else if (QPushButton* chip = getChip(m_category))
    {
        if (!chip->isChecked())
        {
            chip->setChecked(true);
        }
    }
}

void PDFAppToolsPage::updateTexts(int visibleCount, const std::map<QString, int>& counts, bool isSearch)
{
    const QString text = m_searchText;
    const PDFAppToolCollection* collection = findShownCollection();
    const bool isPinnedFilter = m_category == getPinnedFilter();
    const QString categoryName = getCategoryName(m_category);

    // The header of all tools (272:12889, 530:3026)
    QString title = tr("All tools");
    QString hint;
    if (!m_collection.isEmpty())
    {
        title = collection ? tr("Tools in “%1”").arg(collection->name) : tr("Collection");
    }
    else if (isSearch)
    {
        if (m_category.isEmpty())
        {
            title = tr("Tools matching “%1”").arg(text);
        }
        else if (isPinnedFilter)
        {
            title = tr("Pinned tools matching “%1”").arg(text);
        }
        else
        {
            title = tr("Tools matching “%1” in %2").arg(text, categoryName);
        }
    }
    else if (isPinnedFilter)
    {
        title = tr("Pinned tools");
        hint = visibleCount > 0 ? PDFAppToolPresentation::getToolCountText(visibleCount) : QString();
    }
    else if (!m_category.isEmpty())
    {
        title = tr("Tools in %1").arg(categoryName);
    }
    else
    {
        hint = tr("%Ln tools in %1 categories", nullptr, PDFDocumentToolCatalog::getToolCount())
                   .arg(PDFAppLocale::formatNumber(int(PDFDocumentToolCatalog::getCategories().size())));
    }

    if (m_allToolsHeader && m_allToolsHeader->getTitleLabel())
    {
        m_allToolsHeader->getTitleLabel()->setText(title);
    }
    if (m_allToolsHint)
    {
        m_allToolsHint->setText(hint);
        m_allToolsHint->setHidden(hint.isEmpty());
    }
    if (m_editCollectionLink)
    {
        m_editCollectionLink->setVisible(collection != nullptr);
        if (collection)
        {
            m_editCollectionLink->setProperty(PDFAppToolPresentation::ROUTE_PROPERTY, PDFAppRoutes::toolCollection(collection->routeId).toString());
            m_editCollectionLink->setAccessibleName(tr("Edit collection %1").arg(collection->name));
        }
    }

    // The hint of the search in one category (530:3946): the matches of the other categories
    if (m_searchHint && m_collection.isEmpty())
    {
        const int total = PDFAppToolsPageHelper::getCount(counts, QString());
        const int current = PDFAppToolsPageHelper::getCount(counts, m_category);
        const int others = total - current;
        const bool isShown = isSearch && !m_category.isEmpty() && others > 0;
        m_searchHint->setVisible(isShown);
        if (isShown)
        {
            QString first;
            if (current == 0)
            {
                first = tr("No tools in %1 match “%2”.").arg(categoryName, text);
            }
            else if (current == 1)
            {
                first = tr("%Ln tool in %1 matches “%2”.", nullptr, current).arg(categoryName, text);
            }
            else
            {
                first = tr("%Ln tools in %1 match “%2”.", nullptr, current).arg(categoryName, text);
            }

            QString second;
            if (isPinnedFilter)
            {
                second = others == 1 ? tr("%Ln more tool matches among all tools –", nullptr, others) : tr("%Ln more match among all tools –", nullptr, others);
            }
            else
            {
                QStringList items;
                for (const PDFDocumentToolCategoryInfo& info : PDFDocumentToolCatalog::getCategories())
                {
                    const int count = PDFAppToolsPageHelper::getCount(counts, info.getId());
                    if (info.getId() != m_category && count > 0)
                    {
                        items << PDFAppToolsPageHelper::getCountItem(info.getChipText(), count);
                    }
                }
                const QString list = items.join(tr(", "));
                second = others == 1 ? tr("%Ln more matches in another category: %1 –", nullptr, others).arg(list)
                                     : tr("%Ln more match in other categories: %1 –", nullptr, others).arg(list);
            }

            m_searchHintText->setText(first + QStringLiteral("  ") + second);
            if (m_showAllLink)
            {
                m_showAllLink->setText(total == 1 ? tr("show %Ln match", nullptr, total) : tr("show all %Ln matches", nullptr, total));
            }
        }
    }

    // Nothing to show: no match, no pinned tool, a deleted collection
    if (m_noResults)
    {
        const bool isEmpty = !m_collection.isEmpty() ? collection == nullptr : visibleCount == 0;
        m_noResults->setVisible(isEmpty);
        if (isEmpty)
        {
            QString icon = QStringLiteral("search");
            QString emptyTitle;
            QString emptyText;
            bool isSearchState = false;
            if (!m_collection.isEmpty())
            {
                icon = QStringLiteral("layers");
                emptyTitle = tr("This collection does not exist any more");
                emptyText = tr("It has been deleted, maybe in another PDF4QT window. Choose another collection or browse all tools.");
            }
            else if (isSearch)
            {
                isSearchState = true;
                emptyTitle = tr("No tools match “%1”").arg(text);

                // Matches of other categories are offered by the hint above the card
                // (530:3946): the card never says that no category matches
                const int others = PDFAppToolsPageHelper::getCount(counts, QString()) - PDFAppToolsPageHelper::getCount(counts, m_category);
                emptyText = (m_category.isEmpty() || others > 0) ? tr("Check the spelling or try another word. The search looks in the names, descriptions and keywords of the tools.")
                                                 : tr("No tool matches in any category. Check the spelling or try another word.");
            }
            else
            {
                icon = QStringLiteral("star");
                emptyTitle = tr("No pinned tools");
                emptyText = tr("Pin a tool with its ★ to keep it here and on Home.");
            }

            m_noResultsIcon->setIconName(icon);
            m_noResultsTitle->setText(emptyTitle);
            m_noResultsText->setText(emptyText);
            m_clearSearchButton->setVisible(isSearchState);
            m_searchEverywhereButton->setVisible(isSearchState);
        }
    }
}

void PDFAppToolsPage::updateStars()
{
    m_isUpdatingStars = true;
    for (const auto& [toolId, row] : m_toolRows)
    {
        if (row)
        {
            row->setPinned(m_state.isPinned(toolId));
        }
    }
    if (m_collectionCard)
    {
        for (PDFToolRow* row : m_collectionCard->getRows())
        {
            row->setPinned(m_state.isPinned(PDFAppToolPresentation::getToolId(row)));
        }
    }
    for (PDFQuickActionTile* tile : getTiles())
    {
        tile->setPinned(m_state.isPinned(PDFAppToolPresentation::getToolId(tile)));
    }
    for (PDFToolRow* row : getNearestRows())
    {
        row->setPinned(m_state.isPinned(PDFAppToolPresentation::getToolId(row)));
    }
    m_isUpdatingStars = false;
}

void PDFAppToolsPage::setStarsOf(const QString& toolId, bool isPinned)
{
    m_isUpdatingStars = true;
    if (PDFToolRow* row = m_toolRows.count(toolId) ? m_toolRows[toolId].data() : nullptr)
    {
        row->setPinned(isPinned);
    }
    if (m_collectionCard)
    {
        for (PDFToolRow* row : m_collectionCard->getRows())
        {
            if (PDFAppToolPresentation::getToolId(row) == toolId)
            {
                row->setPinned(isPinned);
            }
        }
    }
    if (PDFQuickActionTile* tile = findTile(toolId))
    {
        tile->setPinned(isPinned);
    }
    for (PDFToolRow* row : getNearestRows())
    {
        if (PDFAppToolPresentation::getToolId(row) == toolId)
        {
            row->setPinned(isPinned);
        }
    }
    m_isUpdatingStars = false;
}

void PDFAppToolsPage::updatePinnedTiles()
{
    if (!m_tileGrid)
    {
        return;
    }

    const QString signature = m_state.pinnedTools.join(QChar(0x1F));
    if (signature != m_tilesSignature || m_tiles.size() != m_state.pinnedTools.size())
    {
        m_tilesSignature = signature;
        rebuildPinnedTiles();
    }

    // No pin: the empty state instead of the tiles
    const bool isEmpty = getTiles().isEmpty();
    m_tileGrid->setHidden(isEmpty);
    if (m_pinnedEmpty && m_pinnedEmpty->parentWidget())
    {
        m_pinnedEmpty->parentWidget()->setHidden(!isEmpty);
    }
}

void PDFAppToolsPage::rebuildPinnedTiles()
{

    for (const QPointer<PDFQuickActionTile>& tile : std::as_const(m_tiles))
    {
        if (tile)
        {
            // The tile may be the sender of the change (its star): deleted later
            tile->hide();
            tile->setParent(nullptr);
            tile->deleteLater();
        }
    }
    m_tiles.clear();

    for (const QString& toolId : std::as_const(m_state.pinnedTools))
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
        if (!tool)
        {
            continue;
        }

        PDFQuickActionTile* tile = PDFAppToolPresentation::createQuickActionTile(*tool, true, m_tileGrid);
        prepareToolWidget(tile);
        connect(tile, &PDFQuickActionTile::clicked, this, &PDFAppToolsPage::onToolClicked);
        connect(tile, &PDFQuickActionTile::pinnedChanged, this, &PDFAppToolsPage::onToolPinnedChanged);
        m_tileGrid->addWidget(tile);
        m_tiles.append(tile);
        m_tileGroup->addWidget(tile);
    }
    m_tileGrid->updateCells();
}

void PDFAppToolsPage::updateCollections()
{
    if (!m_collectionsLayout)
    {
        return;
    }

    QStringList signatureParts;
    for (const PDFAppToolCollection& collection : m_state.collections)
    {
        signatureParts << QStringLiteral("%1|%2|%3|%4").arg(collection.routeId, collection.name, collection.iconName, collection.toolIds.join(QLatin1Char(',')));
    }
    const QString signature = signatureParts.join(QChar(0x1F));
    if (signature == m_collectionsSignature && m_collectionRows.size() == qsizetype(m_state.collections.size()))
    {
        return;
    }
    m_collectionsSignature = signature;

    for (const QPointer<PDFCollectionRow>& row : std::as_const(m_collectionRows))
    {
        if (row)
        {
            row->hide();
            row->setParent(nullptr);
            row->deleteLater();
        }
    }
    m_collectionRows.clear();

    int index = 0;
    for (const PDFAppToolCollection& collection : m_state.collections)
    {
        PDFCollectionRow* row = PDFAppToolPresentation::createCollectionRow(collection, m_collectionsCard);
        connect(row, &PDFCollectionRow::clicked, this, &PDFAppToolsPage::onCollectionClicked);
        m_collectionsLayout->insertWidget(index++, row);
        m_collectionRows.append(row);
        m_collectionGroup->removeWidget(m_newCollectionRow);
        m_collectionGroup->addWidget(row);
        m_collectionGroup->addWidget(m_newCollectionRow);
    }
}

void PDFAppToolsPage::updateRecentTools()
{
    if (!m_recentLayout)
    {
        return;
    }

    // The times, not their texts: the minute ticker updates the texts (O25)
    const qint64 now = PDFAppHomeFormat::getCurrentTime();
    QStringList signatureParts;
    for (const PDFToolUsage& usage : m_state.usage)
    {
        signatureParts << QStringLiteral("%1|%2").arg(usage.toolId).arg(usage.lastUsed);
    }
    const QString signature = signatureParts.join(QChar(0x1F));
    if (signature != m_recentSignature || m_recentRows.size() != qsizetype(m_state.usage.size()))
    {
        m_recentSignature = signature;
        for (const QPointer<PDFRecentToolRow>& row : std::as_const(m_recentRows))
        {
            if (row)
            {
                row->hide();
                row->setParent(nullptr);
                row->deleteLater();
            }
        }
        m_recentRows.clear();

        int index = 0;
        for (const PDFToolUsage& usage : m_state.usage)
        {
            // Unknown ids are skipped by the repository; the catalog is checked again
            const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(usage.toolId);
            if (!tool || index >= RECENT_TOOLS_LIMIT)
            {
                continue;
            }

            PDFRecentToolRow* row = PDFAppToolPresentation::createRecentToolRow(*tool, usage.lastUsed, now, m_recentCard);
            prepareToolWidget(row);
            connect(row, &PDFRecentToolRow::clicked, this, &PDFAppToolsPage::onToolClicked);
            m_recentLayout->insertWidget(index++, row);
            m_recentRows.append(row);
            m_recentGroup->addWidget(row);
        }
    }

    updateRecentTimes(now);

    // Q2: Clear is hidden, while the list is empty
    const bool isEmpty = getRecentRows().isEmpty();
    if (m_recentEmpty)
    {
        m_recentEmpty->setVisible(isEmpty);
    }
    if (m_clearRecentLink)
    {
        m_clearRecentLink->setHidden(isEmpty);
    }
}

void PDFAppToolsPage::updateCollectionCard()
{
    const PDFAppToolCollection* collection = findShownCollection();
    QString signature;
    if (collection)
    {
        signature = QStringLiteral("%1|%2|%3|%4").arg(collection->routeId, collection->name, collection->iconName, collection->toolIds.join(QLatin1Char(',')));
    }
    if (signature == m_collectionCardSignature && (m_collectionCard || !collection))
    {
        return;
    }
    m_collectionCardSignature = signature;

    QPointer<PDFAppToolCategoryCard> previous = m_collectionCard;
    m_collectionCard = nullptr;
    if (previous)
    {
        // A row of the card may be the sender (its star): deleted later
        previous->hide();
        previous->setParent(nullptr);
        previous->deleteLater();
    }

    if (!collection || !m_allToolsSection)
    {
        return;
    }

    // One card 802 with the tools of the collection in its order (Q1): the icon,
    // the name and the number of the tools of the collection
    const QString description = collection->isBuiltin ? tr("Built-in collection") : tr("Your collection");
    PDFAppToolCategoryCard* card = new PDFAppToolCategoryCard(collection->name, description, PDFAppToolPresentation::getToolCountText(int(collection->toolIds.size())),
                                                              collection->iconName, PDFAccentHue::Blue, m_allToolsSection);
    card->setObjectName(QStringLiteral("collectionCard"));
    if (PDFToolCategoryHeader* header = card->getHeader())
    {
        // The name of the collection is data (its own direction, review RS6)
        header->setTitleData(true);
    }
    for (const QString& toolId : collection->toolIds)
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
        if (!tool)
        {
            continue;
        }

        PDFToolRow* row = PDFAppToolPresentation::createToolRow(*tool, m_state.isPinned(toolId), card);
        prepareToolWidget(row);
        connect(row, &PDFToolRow::clicked, this, &PDFAppToolsPage::onToolClicked);
        connect(row, &PDFToolRow::pinnedChanged, this, &PDFAppToolsPage::onToolPinnedChanged);
        card->addRow(row);
        m_rowGroup->addWidget(row);
    }
    card->updateVisibility();

    if (QBoxLayout* layout = qobject_cast<QBoxLayout*>(m_allToolsSection->layout()))
    {
        layout->insertWidget(layout->indexOf(m_categoryGrid) + 1, card);
    }
    m_collectionCard = card;
}

void PDFAppToolsPage::updateNearestTools(bool isShown)
{
    if (!m_nearestSection || !m_nearestList)
    {
        return;
    }

    // Up to three nearest tools (every tool of the catalog can be opened from the
    // page); none, when no tool is close enough
    QStringList toolIds;
    if (isShown)
    {
        for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::searchNearest(PDFSearchQuery(m_searchText)))
        {
            if (match.tool)
            {
                toolIds << match.tool->getId();
            }
        }
    }

    // The rows are built again only for other tools (the focus stays in a row)
    if (toolIds != m_nearestTools)
    {
        for (const QPointer<PDFToolRow>& row : std::as_const(m_nearestRows))
        {
            if (row)
            {
                // A row may be the sender (its star): deleted later
                row->hide();
                row->setParent(nullptr);
                row->deleteLater();
            }
        }
        m_nearestRows.clear();
        m_nearestTools = toolIds;

        QBoxLayout* layout = qobject_cast<QBoxLayout*>(m_nearestList->layout());
        for (const QString& toolId : std::as_const(toolIds))
        {
            const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
            if (!tool || !layout)
            {
                continue;
            }

            PDFToolRow* row = PDFAppToolPresentation::createToolRow(*tool, m_state.isPinned(toolId), m_nearestList);
            row->setObjectName(QStringLiteral("nearest-") + toolId);
            prepareToolWidget(row);
            connect(row, &PDFToolRow::clicked, this, &PDFAppToolsPage::onToolClicked);
            connect(row, &PDFToolRow::pinnedChanged, this, &PDFAppToolsPage::onToolPinnedChanged);
            layout->addWidget(row);
            row->show();
            m_nearestRows.append(row);
            if (m_nearestGroup)
            {
                m_nearestGroup->addWidget(row);
            }
        }
    }

    m_nearestSection->setVisible(!m_nearestRows.isEmpty());
}

void PDFAppToolsPage::updateFocusStops()
{
    for (PDFAppToolFocusGroup* group : { m_tileGroup.data(), m_rowGroup.data(), m_collectionGroup.data(), m_recentGroup.data(), m_nearestGroup.data() })
    {
        if (group)
        {
            group->updateStops();
        }
    }
}

void PDFAppToolsPage::showStatusMessage(const QString& text)
{
    qWarning("Tools page: %s", qPrintable(text.left(512)));
    if (!m_statusLabel)
    {
        return;
    }

    m_statusLabel->setText(text);
    m_statusLabel->setHidden(text.isEmpty());
    m_statusTimer.start();
}

bool PDFAppToolsPage::navigateTo(const PDFAppRoute& route, PDFAppNavigationMode mode, const Pending& pending)
{
    // The next page of the navigation takes the focus and the text of the field
    s_pending = pending;
    const bool isAlive = requestNavigation(route, mode);
    s_pending = Pending();
    return isAlive;
}

void PDFAppToolsPage::applyPendingFocus()
{
    const Pending pending = m_pending;
    m_pending = Pending();

    if (pending.focus == PendingFocus::Search && m_searchField)
    {
        m_searchField->setFocus(Qt::OtherFocusReason);
        if (pending.cursorPosition >= 0)
        {
            m_searchField->setCursorPosition(pending.cursorPosition);
        }
    }
    else if (pending.focus == PendingFocus::Chip)
    {
        if (QPushButton* chip = getChip(pending.chip))
        {
            chip->setFocus(Qt::TabFocusReason);
        }
    }
}

void PDFAppToolsPage::scheduleRefresh()
{
    // A hidden page (another tab) reads the state, when it is shown again: its
    // state is not current since the hide (showEvent)
    if (m_isPageHidden)
    {
        m_isStateCurrent = false;
        m_refreshTimer.stop();
        return;
    }

    if (!m_refreshTimer.isActive())
    {
        m_refreshTimer.start();
    }
}

QString PDFAppToolsPage::getCategoryName(const QString& category) const
{
    if (category == getPinnedFilter())
    {
        return tr("Pinned");
    }
    if (const std::optional<PDFDocumentToolCategory> value = PDFDocumentToolCatalog::findCategory(category))
    {
        if (const PDFDocumentToolCategoryInfo* info = PDFDocumentToolCatalog::getCategoryInfo(*value))
        {
            return info->getChipText();
        }
    }
    return QString();
}

const PDFAppToolCollection* PDFAppToolsPage::findShownCollection() const
{
    return m_collection.isEmpty() ? nullptr : m_state.findCollection(m_collection);
}

void PDFAppToolsPage::onSearchTextEdited(const QString& text)
{
    Q_UNUSED(text);
    m_searchTimer.start();
}

void PDFAppToolsPage::onSearchTimeout()
{
    if (!m_searchField)
    {
        return;
    }

    Pending pending;
    pending.focus = PendingFocus::Search;
    pending.fieldText = m_searchField->text();
    pending.cursorPosition = m_searchField->cursorPosition();
    changeFilter(m_category, m_searchField->text(), pending);
}

void PDFAppToolsPage::onChipClicked(QAbstractButton* button)
{
    for (const auto& [category, chip] : m_chips)
    {
        if (chip && chip.data() == button)
        {
            // The text being typed belongs to the filter too
            m_searchTimer.stop();
            Pending pending;
            pending.focus = PendingFocus::Chip;
            pending.chip = category;
            const QString text = m_searchField ? m_searchField->text() : m_searchText;
            const QString categoryCopy = category;
            changeFilter(categoryCopy, text, pending);
            return;
        }
    }
}

void PDFAppToolsPage::onToolClicked()
{
    // The clicked signal does not identify the widget: its route is its property
    const PDFAppRoute route = PDFAppToolPresentation::getRoute(sender());
    if (route.isValid())
    {
        // The page may be deleted by the navigation
        requestNavigation(route);
    }
}

void PDFAppToolsPage::onToolPinnedChanged(bool isPinned)
{
    if (m_isUpdatingStars)
    {
        return;
    }

    const QString toolId = PDFAppToolPresentation::getToolId(sender());
    if (!toolId.isEmpty())
    {
        setToolPinned(toolId, isPinned);
    }
}

void PDFAppToolsPage::onToolContextMenu(const QPoint& position)
{
    QWidget* widget = qobject_cast<QWidget*>(sender());
    if (!widget)
    {
        return;
    }

    const QString toolId = PDFAppToolPresentation::getToolId(widget);
    showToolMenu(toolId, widget->mapToGlobal(position));
}

void PDFAppToolsPage::onCollectionClicked()
{
    // The clicked signal does not identify the row: its route is its property
    const PDFAppRoute route = PDFAppToolPresentation::getRoute(sender());
    if (route.isValid())
    {
        requestNavigation(route);
    }
}

void PDFAppToolsPage::onNewCollectionClicked()
{
    requestNavigation(PDFAppRoutes::toolCollectionNew());
}

void PDFAppToolsPage::onShowAllClicked()
{
    Pending pending;
    pending.focus = PendingFocus::Search;
    pending.fieldText = m_searchField ? m_searchField->text() : m_searchText;
    pending.cursorPosition = m_searchField ? m_searchField->cursorPosition() : -1;

    QPointer<PDFAppToolsPage> guard(this);
    const QString text = m_searchText;
    if (changeFilter(QString(), text, pending) && guard && m_searchField && !isHidden())
    {
        // The link is hidden now: the focus goes to the search
        m_searchField->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppToolsPage::onClearSearchClicked()
{
    m_searchTimer.stop();
    Pending pending;
    pending.focus = PendingFocus::Search;

    QPointer<PDFAppToolsPage> guard(this);
    if (m_searchField)
    {
        m_searchField->clear();
    }
    // A filter cleared to the previous entry of the history goes Back: this page is
    // hidden and deleted later, the page of the entry takes the focus (O25)
    const QString category = m_category;
    if (changeFilter(category, QString(), pending) && guard && m_searchField && !isHidden())
    {
        m_searchField->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppToolsPage::onSearchEverywhereClicked()
{
    requestNavigation(PDFAppRoutes::search(m_searchText));
}

void PDFAppToolsPage::onClearRecentClicked()
{
    openClearRecentDialog();
}

void PDFAppToolsPage::onClearDialogFinished(int result)
{
    m_clearDialog = nullptr;

    PDFAppContext* context = getContext();
    if (result != QDialog::Accepted || !context || !context->getDatabase())
    {
        return;
    }

    // Q2: only the list of the recent tools (tool_usage); the pins, the collections
    // and the pairs of Then continue with stay. The page follows the notification.
    const PDFDatabaseError error = context->getTools().clearUsage();
    if (error.isError())
    {
        showStatusMessage(tr("The list of recent tools could not be cleared: %1").arg(error.getMessage()));
    }
    scheduleRefresh();
}

void PDFAppToolsPage::onMenuOpenRequested(const QString& toolId)
{
    requestNavigation(PDFAppToolPresentation::getToolRoute(toolId));
}

void PDFAppToolsPage::onMenuPinRequested(const QString& toolId, bool isPinned)
{
    setToolPinned(toolId, isPinned);
}

void PDFAppToolsPage::onMenuCollectionToggled(const QString& toolId, const QString& routeId, bool isIncluded)
{
    PDFAppContext* context = getContext();
    const PDFAppToolCollection* collection = m_state.findCollection(routeId);
    if (!context || !context->getDatabase() || !collection || !PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return;
    }

    const bool isLastTool = !isIncluded && collection->toolIds.size() <= 1 && collection->toolIds.contains(toolId);
    const QString name = collection->name;
    const PDFDatabaseError error = context->getToolCollections().setToolIncluded(collection->isBuiltin ? 0 : collection->rowId, collection->builtinId, toolId, isIncluded);
    if (error.isError())
    {
        QString text;
        if (isLastTool)
        {
            text = tr("The last tool of the collection “%1” cannot be removed. Delete the collection in Manage collections.").arg(name);
        }
        else if (error.isBusy())
        {
            text = tr("Another PDF4QT window is saving its settings. Try it again in a moment.");
        }
        else
        {
            text = tr("The collection “%1” could not be changed: %2").arg(name, error.getMessage());
        }
        showStatusMessage(text);
    }
    scheduleRefresh();
}

void PDFAppToolsPage::onMenuNewCollectionRequested(const QString& toolId)
{
    requestNavigation(PDFAppRoutes::toolCollectionNew(toolId));
}

void PDFAppToolsPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    if (PDFAppToolState::isRelevantTopic(topic, key))
    {
        scheduleRefresh();
    }
}

void PDFAppToolsPage::onEverythingChanged()
{
    scheduleRefresh();
}

void PDFAppToolsPage::onRefreshTimeout()
{
    refresh();
}

void PDFAppToolsPage::onMinuteChanged(qint64 now)
{
    // Only the texts: no read of the database, no rebuild of the rows
    updateRecentTimes(now);
}

void PDFAppToolsPage::updateRecentTimes(qint64 now)
{
    for (const QPointer<PDFRecentToolRow>& row : std::as_const(m_recentRows))
    {
        PDFAppHomeFormat::updateTimeTexts(row, now);
    }
    if (m_minuteSubscription)
    {
        m_minuteSubscription->setUpdated(now);
    }
}

void PDFAppToolsPage::onStatusTimeout()
{
    if (m_statusLabel)
    {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }
}

PDFAppPage* PDFAppToolsPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppToolsPage(context, parent);
}

}   // namespace pdf
