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

#ifndef PDFAPPTOOLREDACTSEARCHPART_H
#define PDFAPPTOOLREDACTSEARCHPART_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfdocumenttextindex.h"
#include "pdfglobal.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QPolygonF>
#include <QString>

#include <array>
#include <memory>
#include <vector>

class QItemSelection;
class QPushButton;
class QWidget;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentSearchResultModel;
class PDFAppDocumentSearchResults;
class PDFAppDocumentTextIndexer;
class PDFAppDocumentView;
class PDFAppParagraph;
class PDFLabel;
class PDFLineEdit;
class PDFListView;
class PDFTextLink;

/// The search of Redact Content in the left panel (plan of step 6 of phase 2, 2.5, 4.5, K6.21, R6-19;
/// package P5; Figma 437:12113, 437:12452): the field of the search term with Match case, Whole words,
/// Regular expression, Across line breaks (K6.21) and Ignore soft hyphens (always on, Disabled "Always
/// on" - the flows of the text index remove the soft hyphens), the results with their snippets
/// (PDFSearchResultDelegate over PDFAppDocumentSearchResultModel), the selection of the results (the
/// selected rows are marked, Select all), Mark N Selected Matches, which makes a Redact annotation of
/// each selected result (the GUI of the tool commits them as one Undo step), and Back to Marked Areas.
///
/// The search runs over the text index of the document (PDFAppDocumentTextIndexer of the tab, built at
/// the first request; the panel shows the indexing meanwhile) by PDFDocumentTextSearch::find in one
/// asynchronous job (QtConcurrent::run; the job gets copies of plain values and a cancellation flag,
/// a new search cancels the running one, nobody waits for it). The limits of 7.2: a term of at most
/// 1,024 characters, at most 10,000 results; an empty or a wrong regular expression is an error of the
/// field (as 435:7693).
///
/// Across line breaks (K6.21): a line end of the text flow of the index is one space. Off: a match,
/// whose characters lie on more than one line, is dropped. On: each run of white space of a plain term
/// matches \s+ (any white space, also the line end); a regular expression of the user is used as it is
/// (\s matches the line end) - the rule, which the core of Redact uses for its parameter
/// searchAcrossLineBreaks too.
///
/// R6-19: the search term, the results and the selection live in memory only, in this part and its
/// widgets (no database, no last value, no parameter of the model, no Recent tools, no history of the
/// field); clear() forgets them, when the tool closes.
///
/// Ownership: a child object of the GUI; its widgets belong to their parents (the content of the left
/// panel, made again after a change of the language), the part holds them by QPointer; the model of the
/// results and the watcher of the job are its children. Q_EMIT is the last call of a function.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactSearchPart : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// A result of the search: the page and its quadrilaterals (the area of the match, Redact /QuadPoints)
    struct Match
    {
        PDFInteger pageIndex = -1;
        QPolygonF quadrilaterals;
        bool isSelected = true;
    };

    /// The options of the search (the chips of 437:12113)
    enum class Option
    {
        MatchCase,
        WholeWords,
        RegularExpression,
        AcrossLineBreaks,
        IgnoreSoftHyphens,
        Count
    };

    /// The state of the search
    enum class State
    {
        Idle,       ///< No search
        Indexing,   ///< The text index is being built (the search starts, when it is ready)
        Searching,  ///< The job runs
        Finished,   ///< The results are shown (also none)
        Failed      ///< The term is not valid (getError)
    };

    using SearchResultsPointer = std::shared_ptr<const PDFAppDocumentSearchResults>;
    using Watcher = QFutureWatcher<SearchResultsPointer>;

    explicit PDFAppToolRedactSearchPart(PDFAppContext* context, QObject* parent);
    virtual ~PDFAppToolRedactSearchPart() override;

    /// The document and the view of the tab (its text index); a search of another document is forgotten
    void setDocument(PDFAppDocument* document, PDFAppDocumentView* view);

    /// The content of the left panel in the search mode (437:12113), a new widget of the parent
    QWidget* createContent(QWidget* parent);

    /// The results of the last search (empty = none)
    const std::vector<Match>& getMatches() const { return m_matches; }

    /// The selected results
    std::vector<Match> getSelectedMatches() const;
    int getSelectedCount() const;

    /// The number of the pages with a result
    int getMatchPageCount() const;

    /// Forgets the term and the results (the close of the tool, a new document); the field is emptied
    /// by setText (no undo history of the term stays in the field)
    void clear();

    /// Searches the text of the field with the options (Enter of the field, a change of an option)
    void search();

    State getState() const { return m_state; }
    const QString& getError() const { return m_error; }

    /// The term of the last search (memory only, R6-19)
    const QString& getQuery() const { return m_query; }

    /// More results exist than the limit of 7.2 (the first ones are shown)
    bool isTruncated() const;

    bool isOptionChecked(Option option) const;
    void setOptionChecked(Option option, bool isChecked);

    /// Select all of the results (the link of 437:12113)
    void selectAll();

    /// The GUI has marked \p count matches (437:12452 "4 Matches Marked"); a new selection or search
    /// offers Mark N Selected Matches again
    void setMarkedCount(int count);
    int getMarkedCount() const { return m_markedCount; }

    /// The parts of the content (nullptr = none)
    PDFLineEdit* getField() const { return m_field.data(); }
    QPushButton* getOptionButton(Option option) const;
    PDFLabel* getResultsLabel() const { return m_resultsLabel.data(); }
    PDFLabel* getErrorLabel() const { return m_errorLabel.data(); }
    PDFTextLink* getSelectAllLink() const { return m_selectAllLink.data(); }
    PDFListView* getResultsView() const { return m_resultsView.data(); }
    QPushButton* getMarkButton() const { return m_markButton.data(); }
    QPushButton* getBackButton() const { return m_backButton.data(); }

    /// The function of the job (a worker thread): the search of the index by the rule of a run
    /// (PDFDocumentToolRedact::findMatches - one rule of Across line breaks, K6.21)
    static SearchResultsPointer runSearch(std::shared_ptr<const PDFDocumentTextIndex> index,
                                          PDFDocumentPointer document,
                                          QString query,
                                          PDFDocumentTextSearchOptions options,
                                          bool isAcrossLineBreaks,
                                          std::shared_ptr<PDFAppCancelFlag> cancelFlag);

Q_SIGNALS:
    /// Mark N Selected Matches was clicked: the GUI makes the marks of getSelectedMatches (the last call)
    void markRequested();

    /// The results, their selection or the state of the search have changed
    void matchesChanged();

    /// Back to Marked Areas was clicked
    void backRequested();

private:
    void setState(State state);
    void stopSearch();
    void startSearch();
    void updateWidgets();
    void updateSelectionFromView();
    void retranslate();

    void onFieldReturnPressed();
    void onOptionToggled(bool isChecked);
    void onSearchFinished();
    void onIndexReady();
    void onIndexProgressChanged(int progress);

    /// A new version of the document (review RS6APP): "N matches marked" ends; results of pages, whose
    /// content has changed (an assistant, Undo of a page change, a reload), are dropped - their places
    /// would mark other content
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onCurrentChanged();
    void onMarkClicked();
    void onBackClicked();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocumentTextIndexer> m_indexer;
    QPointer<PDFAppDocumentSearchResultModel> m_model;  ///< Child of the part
    QPointer<Watcher> m_watcher;                        ///< Child of the part
    std::shared_ptr<PDFAppCancelFlag> m_cancelFlag;
    SearchResultsPointer m_results;
    std::vector<Match> m_matches;
    std::array<bool, size_t(Option::Count)> m_options = { false, false, false, false, true };
    QString m_query;
    QString m_error;
    State m_state = State::Idle;
    int m_indexProgress = 0;
    int m_markedCount = -1;
    bool m_isUpdatingSelection = false;

    QPointer<PDFLineEdit> m_field;
    QPointer<PDFLabel> m_errorLabel;
    std::array<QPointer<QPushButton>, size_t(Option::Count)> m_optionButtons;
    QPointer<PDFLabel> m_alwaysOnLabel;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_subtitleLabel;
    QPointer<PDFLabel> m_resultsLabel;
    QPointer<PDFTextLink> m_selectAllLink;
    QPointer<PDFListView> m_resultsView;
    QPointer<PDFAppParagraph> m_noteLabel;
    QPointer<QPushButton> m_markButton;
    QPointer<QPushButton> m_backButton;
};

}   // namespace pdf

#endif // PDFAPPTOOLREDACTSEARCHPART_H
