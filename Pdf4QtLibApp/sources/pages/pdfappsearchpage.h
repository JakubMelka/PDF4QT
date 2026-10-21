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

#ifndef PDFAPPSEARCHPAGE_H
#define PDFAPPSEARCHPAGE_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"
#include "pdfcomponentbuttons.h"
#include "pdfappsearchengine.h"
#include "pdfappsearchwidgets.h"
#include "pdfappworkspace.h"

#include <QDateTime>
#include <QPointer>
#include <QPushButton>

#include <map>
#include <optional>

class QAbstractButton;
class QButtonGroup;
class QVBoxLayout;

namespace pdf
{

class PDFAppCard;

/// Page of all results of the global search /search?q=&group= (pdf4qt-search-all
/// 941:7081, no results 955:142130): the chips of the groups with their counts
/// (the filter group= is the state of the route: a chip replaces the route of the
/// current entry of the history), the card of the results (each group at most
/// ALL_GROUP_ROWS rows with Show all (n), a filtered group all its results), the
/// side column Keyboard and What is searched. The page reads the database (Recent,
/// shortcuts) when its route is set and after every change notification; the
/// results never become stale. Keyboard: the arrows move between the results,
/// Enter / Space opens the result, Esc clears the search field of the header.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Rows of a group in the view of all groups (Show all (n) shows the rest)
    static constexpr int ALL_GROUP_ROWS = 5;

    explicit PDFAppSearchPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppSearchPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// Text of the search and the filtered group (std::nullopt = all groups)
    const QString& getSearchText() const { return m_searchText; }
    std::optional<PDFAppSearchGroup> getGroup() const { return m_group; }

    /// Shown results
    const PDFAppSearchResults& getResults() const { return m_results; }

    /// Sets the time of "Opened yesterday" (tests; an invalid time = the clock)
    void setCurrentTime(const QDateTime& now);

    /// Reads the database and searches again (also after a notification)
    void refresh();

    /// Activates the result as a click on its row does. \returns false, if the page
    /// has been deleted by it (a navigation, a command closing the tab)
    bool activateResult(const PDFAppSearchResult& result);

    /// Changes the filter of the groups (the route of the current entry of the history)
    void setGroupFilter(std::optional<PDFAppSearchGroup> group);

    /// Parts of the page (tests)
    QList<PDFAppSearchResultRow*> getRows() const;
    QList<PDFAppSearchShowAllButton*> getShowAllButtons() const;
    QList<PDFAppSearchGroupHeader*> getGroupHeaders() const;
    QPushButton* getChip(std::optional<PDFAppSearchGroup> group) const;
    PDFAppCard* getResultsCard() const { return m_resultsCard; }
    QWidget* getEmptyState() const { return m_emptyState; }
    QPushButton* getSearchManualButton() const { return m_searchManualButton; }
    QWidget* getSideColumn() const { return m_sideColumn; }

    /// The current row (the focused one, or the first one)
    PDFAppSearchResultRow* getCurrentRow() const;

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build();
    QWidget* createSideColumn(QWidget* parent);
    QWidget* createKeyRow(const QStringList& keys, const QString& text, QWidget* parent);
    QWidget* createEmptyState(QWidget* parent);
    QPushButton* createChip(std::optional<PDFAppSearchGroup> group, QWidget* parent);

    /// Builds the content of the card of the results
    void rebuildResults();
    void updateChips();
    void updateCurrentRow();
    /// Returns false, if the page has been deleted by a slot of routeReplaced
    bool updateRouteInHistory();
    void updateSearchField();
    void scheduleRefresh();
    void performScheduledRefresh();
    void clearSearch();

    void onChipClicked(QAbstractButton* button);
    void onRowClicked();
    void onShowAllClicked();
    void onSearchManualClicked();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();

    QKeySequence getSearchShortcut() const;

    PDFAppSearchEngine m_engine;
    PDFAppSearchResults m_results;
    QString m_searchText;
    std::optional<PDFAppSearchGroup> m_group;
    QDateTime m_now;
    bool m_isRefreshScheduled = false;

    QPointer<QWidget> m_content;
    QPointer<QWidget> m_sideColumn;
    QPointer<QButtonGroup> m_chipGroup;
    std::map<int, QPointer<QPushButton>> m_chips;
    QPointer<PDFAppCard> m_resultsCard;
    QPointer<QWidget> m_resultsContent;
    QPointer<QWidget> m_emptyState;
    QPointer<PDFLabel> m_emptyTitle;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<QPushButton> m_searchManualButton;
    QList<QPointer<PDFAppSearchResultRow>> m_rows;
};

class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSEARCHPAGE_H
