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

#ifndef PDFAPPDOCUMENTSEARCHCONTROLLER_H
#define PDFAPPDOCUMENTSEARCHCONTROLLER_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfdocumenttextindex.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>

#include <memory>

class QTimer;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppDocument;
class PDFAppDocumentTextIndexer;
class PDFAppDocumentView;
class PDFDrawWidgetProxy;
class PDFModifiedDocument;

/// Results of one search of a document (shared by the controller, the model and
/// the highlights of the canvas; immutable)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchResults
{
public:
    PDFDocumentTextSearchResult result;     ///< Occurrences, truncation, error
    PDFDocumentPointer document;            ///< Document of the version, which was searched (page labels)
    QString query;                          ///< Searched text
    PDFDocumentTextSearchOptions options;   ///< Options of the search
};

using PDFAppDocumentSearchResultsPointer = std::shared_ptr<const PDFAppDocumentSearchResults>;

/// Model of the occurrences of the search for PDFSearchResultDelegate (component
/// Search result row 191:1073): Qt::DisplayRole = page ("Page 308", with the page
/// label "Page 12 (xii)"), PDFComponentRoles::TextRole = snippet around the
/// occurrence, MatchRangesRole = the occurrence in the snippet, Qt::ToolTipRole =
/// the whole context. The rows are read only when the view shows them (a list of
/// 100 000 results costs only the results themselves).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchResultModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// Maximal number of characters of the snippet before / after the occurrence
    static constexpr int SNIPPET_BEFORE = 60;
    static constexpr int SNIPPET_AFTER = 140;

    explicit PDFAppDocumentSearchResultModel(QObject* parent);
    virtual ~PDFAppDocumentSearchResultModel() override;

    void setResults(PDFAppDocumentSearchResultsPointer results);
    const PDFAppDocumentSearchResultsPointer& getResults() const { return m_results; }

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    /// Text of the page of an occurrence ("Page 308", "Page 12 (xii)")
    static QString getPageText(const PDFDocument* document, PDFInteger pageIndex);

    /// Snippet of the context around the occurrence: at most SNIPPET_BEFORE
    /// characters before and SNIPPET_AFTER after it (cut at a space, an ellipsis
    /// marks the cut), the white space is simplified
    /// \param context Context of the occurrence
    /// \param matchStart Position of the occurrence in the context (-1 = unknown)
    /// \param matchLength Length of the occurrence
    /// \param[out] snippetMatchStart Position of the occurrence in the snippet (-1 = unknown)
    static QString createSnippet(const QString& context, qsizetype matchStart, qsizetype matchLength, qsizetype* snippetMatchStart);

private:
    PDFAppDocumentSearchResultsPointer m_results;
};

/// Highlights of the occurrences on the canvas (Figma 198:4974 match-current,
/// 198:4975 match: search/match, the current one search/match-current with a
/// 2 px border search/match-border, radius 2, multiplied with the page). The
/// geometry comes from the search (quadrilaterals in the page space), so the
/// highlights need no text layout of the widgets.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchHighlighter : public IDocumentDrawInterface
{
public:
    PDFAppDocumentSearchHighlighter() = default;

    void setResults(PDFAppDocumentSearchResultsPointer results);
    void setCurrentIndex(int index) { m_currentIndex = index; }

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    /// Returns the path of the occurrence in the device space; \p inset shrinks
    /// the rectangles of the occurrence (the inside of the border of the current one)
    static QPainterPath getMatchPath(const PDFDocumentTextSearchMatch& match, const QTransform& matrix, qreal inset = 0.0);

    /// Comparator of std::lower_bound: the occurrence lies on a page before the page
    static bool isMatchBeforePage(const PDFDocumentTextSearchMatch& match, PDFInteger pageIndex);

private:
    PDFAppDocumentSearchResultsPointer m_results;
    int m_currentIndex = -1;
};

/// Search in the document of a tab (Figma 198:4520): the query and its options,
/// the lazy text index (PDFAppDocumentTextIndexer), the search in a worker thread
/// (cancellable, a new query cancels the running one, closing the tab never waits),
/// the results with a safety limit of MAXIMUM_RESULTS (the panel shows a note), the
/// current occurrence (next / previous, F3 / Shift+F3) and the highlights of the
/// canvas. A change of the page contents searches again.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Safety limit of the number of the results (step3_large_documents.md)
    static constexpr size_t MAXIMUM_RESULTS = 100000;

    enum class State
    {
        Idle,       ///< No search (or it was stopped)
        Indexing,   ///< The text index is being built (getIndexProgress)
        Searching,  ///< The search runs
        Finished,   ///< The results are shown (also no result)
        Failed      ///< The query is not valid (getError)
    };

    using Watcher = QFutureWatcher<PDFAppDocumentSearchResultsPointer>;

    explicit PDFAppDocumentSearchController(PDFAppDocument* document, PDFAppDocumentView* view, PDFAppDocumentTextIndexer* indexer, QObject* parent);
    virtual ~PDFAppDocumentSearchController() override;

    /// Searches the text with the current options (an empty text clears the search)
    void search(const QString& query);

    /// Searches the current text again (after a change of the options)
    void searchAgain();

    /// Stops the search and removes the results and the highlights
    void clear();

    const QString& getQuery() const { return m_query; }

    const PDFDocumentTextSearchOptions& getOptions() const { return m_options; }

    /// Sets the options; a shown search is searched again
    void setOptions(const PDFDocumentTextSearchOptions& options);

    State getState() const { return m_state; }

    /// Progress of the text index 0 - 100 (state Indexing)
    int getIndexProgress() const;

    /// Error of the query (state Failed)
    const QString& getError() const { return m_error; }

    const PDFAppDocumentSearchResultsPointer& getResults() const { return m_results; }
    int getResultCount() const;

    /// More results exist than MAXIMUM_RESULTS
    bool isTruncated() const;

    /// Current occurrence (-1 = none)
    int getCurrentIndex() const { return m_currentIndex; }

    /// Makes the occurrence current and shows it (an index out of range is ignored)
    void setCurrentIndex(int index);

    /// The next / previous occurrence (wraps around)
    void goToNext();
    void goToPrevious();

    PDFAppDocumentSearchResultModel* getModel() const { return m_model; }

    /// Searches the index (the function of the worker thread)
    static PDFAppDocumentSearchResultsPointer runSearch(std::shared_ptr<const PDFDocumentTextIndex> index,
                                                       PDFDocumentPointer document,
                                                       QString query,
                                                       PDFDocumentTextSearchOptions options,
                                                       std::shared_ptr<PDFAppCancelFlag> cancelFlag);

Q_SIGNALS:
    /// The state, the results or the query have changed
    void stateChanged();

    /// The current occurrence has changed
    void currentIndexChanged(int index);

private:
    void setState(State state);
    void startSearch();
    void stopSearch();
    void onSearchFinished();
    void onIndexReady();
    void onIndexBuildingChanged();
    void onIndexProgressChanged();
    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onDocumentStateChanged();
    void onRefreshTimer();
    void updateHighlights();
    void showCurrentMatch();
    PDFDrawWidgetProxy* getProxy() const;

    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocumentTextIndexer> m_indexer;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<PDFAppDocumentSearchResultModel> m_model;
    QPointer<Watcher> m_watcher;
    QPointer<QTimer> m_refreshTimer;
    std::shared_ptr<PDFAppCancelFlag> m_cancelFlag;
    std::unique_ptr<PDFAppDocumentSearchHighlighter> m_highlighter;
    QString m_query;
    PDFDocumentTextSearchOptions m_options;
    State m_state = State::Idle;
    QString m_error;
    PDFAppDocumentSearchResultsPointer m_results;
    int m_currentIndex = -1;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSEARCHCONTROLLER_H
