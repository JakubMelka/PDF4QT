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

#ifndef PDFAPPTOOLSPAGE_H
#define PDFAPPTOOLSPAGE_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcategorycard.h"
#include "pdfapptoolmenu.h"
#include "pdfapptoolstate.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"

#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include <map>

class QAbstractButton;
class QButtonGroup;
class QHBoxLayout;
class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppEmptyState;
class PDFAppMinuteSubscription;
class PDFAppToolCategoryCard;
class PDFAppToolCategoryGrid;
class PDFAppToolFocusGroup;
class PDFAppToolMenu;
class PDFCollectionRow;
class PDFIconBadge;
class PDFQuickActionTile;
class PDFRecentToolRow;
class PDFTextLink;
class PDFToolRow;

/// Clear recent tools? (owner decision Q2 of step 6, O24; a missing frame of
/// Figma built from 941:136741): the warning badge, what is deleted and what is
/// kept, Clear List (Secondary, leading) and Cancel (Primary, the default). The
/// dialog only answers: the Tools page clears the list, when it is accepted.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppClearRecentToolsDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    static constexpr int CARD_WIDTH = 440;

    explicit PDFAppClearRecentToolsDialog(QWidget* parent);
    virtual ~PDFAppClearRecentToolsDialog() override;

    QPushButton* getClearButton() const { return m_clearButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();

    QPointer<QPushButton> m_clearButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Page Tools /tools (Figma pdf4qt-tools 272:12821, the search 530:2957; plan of
/// step 6 4.2, 4.3): the tool search and the filter chips with their counts, the
/// pinned tools (Quick action tiles in the order of the pins), the 46 tools in
/// the six category cards, the collections and the recent tools in the side
/// column.
///
/// The filter is the state of the route /tools?category=&q= (one category, the
/// chip Pinned or All; the text): the first change of an unfiltered page is a new
/// entry of the history (Push, the new page gets the focus and the text of the
/// field), further changes replace the route of the shown page. A collection
/// (/tools?collection=, owner decision Q1) shows its tools in its order in one card
/// with the link Edit. A tool found only by a keyword shows the reason ("matches
/// “combine”", Q3) instead of its description. A search without a match in any
/// category offers up to three closest tools below the empty state
/// (PDFDocumentToolSearch::searchNearest, owner decision O25); a filter cleared to
/// the route of the previous entry of the history goes Back to it (O25).
///
/// Everything is read from the database, when the page is shown and after a
/// notification of the tool tables (also of another instance), coalesced to one
/// refresh per turn of the event loop. The rows are built once, a refresh changes
/// only the stars, the counts, the visibility, the pinned tiles, the collections
/// and the recent tools. A pin is written at once (the star is optimistic and
/// reverted with a message, when the write is refused).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolsPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Value of the filter Pinned (/tools?category=pinned)
    static QString getPinnedFilter() { return QStringLiteral("pinned"); }

    /// Most recent tools of the side column (272:12958)
    static constexpr int RECENT_TOOLS_LIMIT = 5;

    /// Delay of the search after the last key (ms)
    static constexpr int SEARCH_DELAY = 200;

    /// Time, for which a refused write is shown (ms)
    static constexpr int STATUS_TIMEOUT = 8000;

    /// Narrowest main column beside the side column (main-column 802 + side 340)
    static constexpr int MINIMUM_MAIN_WIDTH = 480;

    /// Pinned tiles: three in a row of 802 (259), fewer in a narrow page
    static constexpr int TILE_MINIMUM_WIDTH = 220;

    explicit PDFAppToolsPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppToolsPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual bool hasSearchHeader() const override;

    /// Current filter: the category ("" = All, getPinnedFilter, a category id), the
    /// text of the search and the collection (route id, "" = none)
    const QString& getCategory() const { return m_category; }
    const QString& getSearchText() const { return m_searchText; }
    const QString& getCollection() const { return m_collection; }

    /// Changes the filter (Push of the first filter of an unfiltered page, then
    /// the route is replaced). \returns false, if the page has been deleted by the
    /// navigation (do not touch it then).
    bool setFilter(const QString& category, const QString& searchText);

    /// Reads the state from the database and updates the page now
    void refresh();

    /// State, which the page has read last
    const PDFAppToolState& getState() const { return m_state; }

    /// Writes the pin of the tool (the star, the context menu); the stars follow at
    /// once, a refused write reverts them and shows the message. \returns false,
    /// when the write was refused.
    bool setToolPinned(const QString& toolId, bool isPinned);

    /// Opens the context menu of the tool at the global position (tests: the menu)
    PDFAppToolMenu* showToolMenu(const QString& toolId, const QPoint& globalPosition);

    /// Opens the confirmation of Clear (recent tools); nullptr = nothing to clear
    PDFAppClearRecentToolsDialog* openClearRecentDialog();

    /// Parts of the page (tests)
    PDFLineEdit* getSearchField() const { return m_searchField; }
    QPushButton* getChip(const QString& category) const;
    QList<QPushButton*> getChips() const;
    PDFToolRow* findToolRow(const QString& toolId) const;
    QList<PDFToolRow*> getVisibleToolRows() const;
    PDFQuickActionTile* findTile(const QString& toolId) const;
    QList<PDFQuickActionTile*> getTiles() const;
    QList<PDFAppToolCategoryCard*> getCategoryCards() const;
    PDFAppToolCategoryCard* getCollectionCard() const { return m_collectionCard; }
    PDFAppToolCategoryGrid* getCategoryGrid() const { return m_categoryGrid; }
    QWidget* getPinnedSection() const { return m_pinnedSection; }
    PDFAppGrid* getTileGrid() const { return m_tileGrid; }
    PDFAppEmptyState* getPinnedEmpty() const { return m_pinnedEmpty; }
    PDFAppSectionHeader* getAllToolsHeader() const { return m_allToolsHeader; }
    PDFAppParagraph* getAllToolsHint() const { return m_allToolsHint; }
    QWidget* getSearchHint() const { return m_searchHint; }
    PDFAppParagraph* getSearchHintText() const { return m_searchHintText; }
    PDFTextLink* getShowAllLink() const { return m_showAllLink; }
    PDFTextLink* getEditCollectionLink() const { return m_editCollectionLink; }
    QWidget* getNoResults() const { return m_noResults; }
    QPushButton* getClearSearchButton() const { return m_clearSearchButton; }
    QPushButton* getSearchEverywhereButton() const { return m_searchEverywhereButton; }
    QList<PDFCollectionRow*> getCollectionRows() const;
    PDFCollectionRow* getNewCollectionRow() const { return m_newCollectionRow; }
    PDFTextLink* getManageLink() const { return m_manageLink; }
    QList<PDFRecentToolRow*> getRecentRows() const;
    PDFTextLink* getClearRecentLink() const { return m_clearRecentLink; }
    PDFAppEmptyState* getRecentEmpty() const { return m_recentEmpty; }
    PDFAppParagraph* getStatusLabel() const { return m_statusLabel; }
    PDFAppToolMenu* getToolMenu() const { return m_menu; }
    PDFAppClearRecentToolsDialog* getClearRecentDialog() const { return m_clearDialog; }
    PDFAppToolFocusGroup* getTileFocusGroup() const { return m_tileGroup; }
    PDFAppToolFocusGroup* getRowFocusGroup() const { return m_rowGroup; }
    PDFAppToolFocusGroup* getCollectionFocusGroup() const { return m_collectionGroup; }
    PDFAppToolFocusGroup* getRecentFocusGroup() const { return m_recentGroup; }

    /// The closest tools of a search without a match (O25, tools.md 9.1 point 4):
    /// the section below the empty state and its tool rows, the best first
    QWidget* getNearestSection() const { return m_nearestSection; }
    QList<PDFToolRow*> getNearestRows() const;
    PDFAppToolFocusGroup* getNearestFocusGroup() const { return m_nearestGroup; }

    /// Number of the rebuilds of the tool rows (tests: a pin never rebuilds them)
    int getRowBuildCount() const { return m_rowBuildCount; }

    /// Number of the reads of the state from the database (tests: a new page reads
    /// it once, not again at its first showing)
    int getRefreshCount() const { return m_refreshCount; }

    /// Subscription of the page to the minute ticker of the context: the times of
    /// Recent tools follow the clock while the page is visible (O25)
    PDFAppMinuteSubscription* getMinuteSubscription() const { return m_minuteSubscription.data(); }

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// What gets the focus on the page created by a Push of the filter
    enum class PendingFocus
    {
        None,
        Search,
        Chip
    };

    struct Pending
    {
        PendingFocus focus = PendingFocus::None;
        QString chip;           ///< Category of the chip
        QString fieldText;      ///< Text of the field, as the user has typed it
        int cursorPosition = -1;
    };

    void buildContent();
    void rebuild();

    QWidget* createToolbar(QWidget* parent);
    QWidget* createSearchHint(QWidget* parent);
    QWidget* createPinnedSection(QWidget* parent);
    QWidget* createAllToolsSection(QWidget* parent);
    QWidget* createNoResults(QWidget* parent);
    QWidget* createNearestSection(QWidget* parent);
    QWidget* createCollectionsSection(QWidget* parent);
    QWidget* createRecentSection(QWidget* parent);
    QPushButton* createChip(const QString& category, const QString& text, QWidget* parent);
    void prepareToolWidget(QWidget* widget);

    /// Changes the filter; the page created by a Push gets the \p pending focus.
    /// \returns false, if the page has been deleted by the navigation
    bool changeFilter(const QString& category, const QString& searchText, const Pending& pending);

    /// Applies the filter to the rows, chips, cards and texts
    void applyFilter();
    void applyCollection();
    void updateChips(const std::map<QString, int>& counts, bool isSearch);
    void updateTexts(int visibleCount, const std::map<QString, int>& counts, bool isSearch);

    void updateStars();
    void updatePinnedTiles();
    void rebuildPinnedTiles();
    void updateCollections();
    void updateRecentTools();
    void updateRecentTimes(qint64 now);
    void updateCollectionCard();
    void updateNearestTools(bool isShown);
    void updateFocusStops();

    void setStarsOf(const QString& toolId, bool isPinned);
    void showStatusMessage(const QString& text);
    bool navigateTo(const PDFAppRoute& route, PDFAppNavigationMode mode, const Pending& pending);
    void applyPendingFocus();
    void scheduleRefresh();

    QString getCategoryName(const QString& category) const;
    const PDFAppToolCollection* findShownCollection() const;
    bool isFiltered() const { return !m_category.isEmpty() || !m_searchText.isEmpty(); }

    void onSearchTextEdited(const QString& text);
    void onSearchTimeout();
    void onChipClicked(QAbstractButton* button);
    void onToolClicked();
    void onToolPinnedChanged(bool isPinned);
    void onToolContextMenu(const QPoint& position);
    void onCollectionClicked();
    void onNewCollectionClicked();
    void onShowAllClicked();
    void onClearSearchClicked();
    void onSearchEverywhereClicked();
    void onClearRecentClicked();
    void onClearDialogFinished(int result);
    void onMenuOpenRequested(const QString& toolId);
    void onMenuPinRequested(const QString& toolId, bool isPinned);
    void onMenuCollectionToggled(const QString& toolId, const QString& routeId, bool isIncluded);
    void onMenuNewCollectionRequested(const QString& toolId);
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onRefreshTimeout();
    void onMinuteChanged(qint64 now);
    void onStatusTimeout();

    static Pending s_pending;

    Pending m_pending;
    PDFAppToolState m_state;
    QString m_category;
    QString m_searchText;
    QString m_collection;
    bool m_isUpdatingStars = false;
    bool m_isStateCurrent = false;  ///< The state has been read, while the page was not hidden
    bool m_isPageHidden = false;    ///< The page has been hidden after it was shown
    int m_refreshCount = 0;
    int m_rowBuildCount = 0;
    QString m_tilesSignature;
    QString m_collectionsSignature;
    QString m_recentSignature;
    QString m_collectionCardSignature;

    QTimer m_searchTimer;
    QTimer m_refreshTimer;
    QTimer m_statusTimer;
    QPointer<PDFAppMinuteSubscription> m_minuteSubscription;

    QPointer<PDFAppColumns> m_columns;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<QButtonGroup> m_chipGroup;
    std::map<QString, QPointer<QPushButton>> m_chips;
    QPointer<PDFAppParagraph> m_statusLabel;
    QPointer<QWidget> m_searchHint;
    QPointer<PDFAppParagraph> m_searchHintText;
    QPointer<PDFTextLink> m_showAllLink;
    QPointer<QWidget> m_pinnedSection;
    QPointer<PDFAppGrid> m_tileGrid;
    QList<QPointer<PDFQuickActionTile>> m_tiles;
    QPointer<PDFAppEmptyState> m_pinnedEmpty;
    QPointer<QWidget> m_allToolsSection;
    QPointer<PDFAppSectionHeader> m_allToolsHeader;
    QPointer<PDFAppParagraph> m_allToolsHint;
    QPointer<PDFTextLink> m_editCollectionLink;
    QPointer<PDFAppToolCategoryGrid> m_categoryGrid;
    QList<QPointer<PDFAppToolCategoryCard>> m_categoryCards;
    std::map<QString, QPointer<PDFToolRow>> m_toolRows;
    QPointer<PDFAppToolCategoryCard> m_collectionCard;
    QPointer<QWidget> m_noResults;
    QPointer<PDFLabel> m_noResultsTitle;
    QPointer<PDFAppParagraph> m_noResultsText;
    QPointer<PDFIconBadge> m_noResultsIcon;
    QPointer<QPushButton> m_clearSearchButton;
    QPointer<QPushButton> m_searchEverywhereButton;
    QPointer<QWidget> m_collectionsCard;
    QPointer<QVBoxLayout> m_collectionsLayout;
    QList<QPointer<PDFCollectionRow>> m_collectionRows;
    QPointer<PDFCollectionRow> m_newCollectionRow;
    QPointer<PDFTextLink> m_manageLink;
    QPointer<QWidget> m_recentCard;
    QPointer<QVBoxLayout> m_recentLayout;
    QList<QPointer<PDFRecentToolRow>> m_recentRows;
    QPointer<PDFAppEmptyState> m_recentEmpty;
    QPointer<PDFTextLink> m_clearRecentLink;
    QPointer<PDFAppToolMenu> m_menu;
    QPointer<PDFAppClearRecentToolsDialog> m_clearDialog;
    QPointer<PDFAppToolFocusGroup> m_tileGroup;
    QPointer<PDFAppToolFocusGroup> m_rowGroup;
    QPointer<PDFAppToolFocusGroup> m_collectionGroup;
    QPointer<PDFAppToolFocusGroup> m_recentGroup;
    QPointer<PDFAppToolFocusGroup> m_nearestGroup;
    QPointer<QWidget> m_nearestSection;
    QPointer<QWidget> m_nearestList;
    QList<QPointer<PDFToolRow>> m_nearestRows;
    QStringList m_nearestTools;     ///< Tools of the shown rows of m_nearestRows
};

class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolsPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPTOOLSPAGE_H
