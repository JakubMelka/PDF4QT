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

#ifndef PDFAPPDOCUMENTTOOLSPANEL_H
#define PDFAPPDOCUMENTTOOLSPANEL_H

#include "pdfappglobal.h"
#include "pdfappdocumentmenus.h"
#include "pdfapproute.h"
#include "pdfappsearchengine.h"
#include "pdftoolrepository.h"

#include <QPointer>
#include <QStringList>

#include <vector>

class QGridLayout;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocument;
class PDFAppMinuteSubscription;
class PDFAppSearchResultRow;
class PDFLabel;
class PDFLineEdit;
class PDFLinkRow;
class PDFRecentToolRow;
class PDFToolShortcut;

/// Tools panel of the document toolbar (Figma panel-tools 196:3650, 400 px wide,
/// package C of step 6): the search field "Search tools and actions…", the first
/// six pinned tools as Tool shortcuts in two columns, the three recently used
/// tools, a divider and the link row All tools (the catalog /tools in the Home
/// tab). A text in the search field replaces the sections by the matching tools
/// and commands of PDFAppSearchEngine (a command runs on the view of the panel, a
/// tool opens as a shortcut does); Down goes from the field into the results,
/// Enter opens the first one, Esc clears the text and then closes the panel. A
/// search without a result offers up to three closest available tools
/// (PDFDocumentToolSearch::searchNearest, owner decision O25) below its text.
///
/// A tool opens on the document of the view (/document/{id}/tool/{toolId}); a tool
/// without a document (Images to PDF, Blank PDF, Manage Certificates) opens its
/// page /tools/{toolId} in the Home tab. A locked document disables the tools,
/// which would change it (tools.md 4.7), with the reason in the tooltip.
///
/// The panel reads the pins and the use of the tools from the database, when it
/// opens and, while it is open, after a notification of another connection
/// (coalesced to one refresh per turn of the event loop). It keeps nothing, when
/// it is closed. The toolbar opens it as a panel of the menu bar
/// (PDFAppDocumentToolbar::Panel::Tools).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentToolsPanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    /// Width of the panel with its border (196:3650)
    static constexpr int PANEL_WIDTH = 400;

    /// Pinned tools shown (the first ones in the order of the pins), recent tools
    static constexpr int MAXIMUM_PINNED = 6;
    static constexpr int MAXIMUM_RECENT = 3;

    /// Most results of one group (Tools, Actions) of a search
    static constexpr int MAXIMUM_GROUP_RESULTS = 5;

    /// Gap of the caption and of its content, gap of the tool shortcuts (196:3655)
    static constexpr int SECTION_CONTENT_GAP = 8;
    static constexpr int SHORTCUT_GAP = 8;

    /// Longest text of the search field
    static constexpr int MAXIMUM_SEARCH_LENGTH = 256;

    explicit PDFAppDocumentToolsPanel(PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppDocumentToolsPanel() override;

    /// Reads the states (the lock of the document); a closed panel (the opening)
    /// reads the pins and the use of the tools from the database and clears the search
    virtual void updateState() override;

    /// Parts of the panel
    PDFLineEdit* getSearchField() const;
    PDFLinkRow* getAllToolsRow() const;
    std::vector<PDFToolShortcut*> getPinnedShortcuts() const;
    std::vector<PDFRecentToolRow*> getRecentRows() const;
    std::vector<PDFAppSearchResultRow*> getResultRows() const;

    /// The closest tools of a search without a result (O25, tools.md 9.1 point 4):
    /// up to three available tools below the text of no results, the best first
    std::vector<PDFAppSearchResultRow*> getNearestRows() const;
    PDFLabel* getNearestCaption() const { return m_nearestCaption.data(); }

    /// Texts of the empty states (nullptr, when the list has items)
    PDFLabel* getPinnedEmptyLabel() const;
    PDFLabel* getRecentEmptyLabel() const;
    PDFLabel* getNoResultsLabel() const;

    /// The search field has a text: the results replace the sections
    bool isSearching() const;

    /// Results of the last search (empty without a text)
    const PDFAppSearchResults& getSearchResults() const { return m_results; }

    /// The tool can be opened from the panel (a locked document disables the tools,
    /// which change it)
    bool isToolAvailable(const QString& toolId) const;

    /// Route of the tool opened from the panel: /document/{id}/tool/{toolId}, the page
    /// /tools/{toolId} of a tool without a document (an invalid route for an unknown tool)
    PDFAppRoute getToolRoute(const QString& toolId) const;

    /// Opens the tool (closes the panel and navigates from the event loop).
    /// \returns false for an unknown or unavailable tool.
    bool activateTool(const QString& toolId);

    /// Tooltip of a tool disabled by the lock of the document
    static QString getLockedToolTip();

    /// Subscription of the panel to the minute ticker of the context: the times of
    /// the recent tools follow the clock while the panel is open (O25)
    PDFAppMinuteSubscription* getMinuteSubscription() const { return m_minuteSubscription.data(); }

    /// Number of the reads of the pins and of the use from the database (tests)
    int getReadCount() const { return m_readCount; }

protected:
    virtual void retranslate() override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PDFAppDocument* getDocument() const;
    bool isDocumentLocked() const;

    void readData();
    void rebuildPinned(bool force);
    void rebuildRecent(bool force);
    void updateRecentTimes(qint64 now);
    void rebuildResults();
    void rebuildNearest();
    void updateTexts();
    void updateAvailability();
    void updateMode();
    void updateTabOrder();
    std::vector<QWidget*> getFocusChain() const;
    void moveFocus(bool isNext);
    void updatePanelHeight();
    void watchChildren();
    void setCurrentResultRow(PDFAppSearchResultRow* row);
    void scheduleRefresh();
    void navigateQueued(const PDFAppRoute& route);

    void onSearchTextChanged();
    void onSearchReturnPressed();
    void onShortcutClicked();
    void onRecentClicked();
    void onResultClicked();
    void onAllToolsClicked();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onDatabaseEverythingChanged();
    void onRefreshQueued();
    void onMinuteChanged(qint64 now);
    void activateResult(PDFAppSearchResultRow* row);

    QPointer<PDFLineEdit> m_searchField;
    QPointer<QWidget> m_sections;
    QPointer<QWidget> m_pinnedSection;
    QPointer<QWidget> m_pinnedGrid;
    QPointer<QGridLayout> m_pinnedLayout;
    QPointer<PDFLabel> m_pinnedEmpty;
    QPointer<QWidget> m_recentSection;
    QPointer<QWidget> m_recentList;
    QPointer<QVBoxLayout> m_recentLayout;
    QPointer<PDFLabel> m_recentEmpty;
    QPointer<QWidget> m_resultsWidget;
    QPointer<PDFLabel> m_toolsCaption;
    QPointer<QWidget> m_toolResults;
    QPointer<QVBoxLayout> m_toolResultsLayout;
    QPointer<PDFLabel> m_commandsCaption;
    QPointer<QWidget> m_commandResults;
    QPointer<QVBoxLayout> m_commandResultsLayout;
    QPointer<PDFLabel> m_noResults;
    QPointer<PDFLabel> m_nearestCaption;
    QPointer<QWidget> m_nearestResults;
    QPointer<QVBoxLayout> m_nearestResultsLayout;
    QPointer<QWidget> m_divider;
    QPointer<PDFLinkRow> m_allToolsRow;
    QPointer<PDFAppMinuteSubscription> m_minuteSubscription;

    std::vector<QPointer<PDFToolShortcut>> m_shortcuts;
    std::vector<QPointer<PDFRecentToolRow>> m_recentRows;
    std::vector<QPointer<PDFAppSearchResultRow>> m_resultRows;
    std::vector<QPointer<PDFAppSearchResultRow>> m_nearestRows;

    /// Data of the database read at the opening (and after a notification)
    QStringList m_pinnedTools;
    std::vector<PDFToolUsage> m_usage;

    /// Shown tools of the sections (a refresh, which changes nothing, keeps the widgets)
    QStringList m_shownPinned;
    QStringList m_shownRecent;

    PDFAppSearchEngine m_engine;
    PDFAppSearchData m_searchData;
    PDFAppSearchResults m_results;
    bool m_isRefreshPending = false;
    int m_readCount = 0;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTTOOLSPANEL_H
