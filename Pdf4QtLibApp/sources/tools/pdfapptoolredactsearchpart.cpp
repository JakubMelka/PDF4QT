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

#include "pdfapptoolredactsearchpart.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocumenttool_redact.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the search of Redact Content
class PDFAppToolRedactSearchPartHelper
{
public:
    PDFAppToolRedactSearchPartHelper() = delete;

    static constexpr const char* OPTION_PROPERTY = "pdf4qtRedactOption";

    /// Gap of the parts of the panel (sidebar-left 437:12140: 10) and of the chips (437:12255: 4)
    static constexpr int PANEL_GAP = 10;
    static constexpr int CHIP_GAP = 4;

    /// Height of the list of the results (437:12296: five rows of 288; the list scrolls)
    static constexpr int RESULTS_HEIGHT = 288;

    /// A count of a text, the singular right in English also without a translation
    static QString plural(int count, const char* singular, const char* pluralText)
    {
        return QCoreApplication::translate("pdf::PDFAppToolRedactSearchPart", count == 1 ? singular : pluralText, nullptr, count);
    }

    /// Option chip (Filter chip 78:836, an on / off option: 32 high, radius 16, the check mark of an
    /// option, which is on; its width follows its text)
    static QPushButton* createOptionButton(const QString& objectName, const QString& iconName, QWidget* parent)
    {
        QPushButton* button = new QPushButton(parent);
        button->setObjectName(objectName);
        button->setCheckable(true);
        button->setAutoDefault(false);
        button->setIcon(PDFThemeIcons::getIcon(iconName));
        button->setIconSize(QSize(14, 14));
        button->setFixedHeight(32);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::CHIP);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::OPTION);
        return button;
    }

    static QHBoxLayout* createRow(QVBoxLayout* layout)
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(CHIP_GAP);
        layout->addLayout(row);
        return row;
    }
};

PDFAppToolRedactSearchPart::PDFAppToolRedactSearchPart(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    m_model = new PDFAppDocumentSearchResultModel(this);
}

PDFAppToolRedactSearchPart::~PDFAppToolRedactSearchPart()
{
    // The job is canceled, nobody waits for it; its result goes nowhere
    stopSearch();
}

void PDFAppToolRedactSearchPart::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_document == document && m_view == view)
    {
        return;
    }

    if (m_indexer)
    {
        m_indexer->disconnect(this);
    }
    if (m_document)
    {
        disconnect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppToolRedactSearchPart::onDocumentChanged);
    }

    m_document = document;
    m_view = view;
    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppToolRedactSearchPart::onDocumentChanged);
    }

    // The text index of the tab (built lazily, shared with the search of the document)
    PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(view ? static_cast<QObject*>(view) : static_cast<QObject*>(document));
    m_indexer = handler ? handler->getIndexer() : nullptr;
    if (m_indexer)
    {
        connect(m_indexer.data(), &PDFAppDocumentTextIndexer::indexReady, this, &PDFAppToolRedactSearchPart::onIndexReady);
        connect(m_indexer.data(), &PDFAppDocumentTextIndexer::progressChanged, this, &PDFAppToolRedactSearchPart::onIndexProgressChanged);
    }
}

QWidget* PDFAppToolRedactSearchPart::createContent(QWidget* parent)
{
    using H = PDFAppToolRedactSearchPartHelper;

    QWidget* content = new QWidget(parent);
    content->setObjectName(QStringLiteral("redactSearchContent"));
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(H::PANEL_GAP);

    // header 437:12248: Search and mark / Find text and mark every match
    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    m_titleLabel = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, content);
    m_titleLabel->setObjectName(QStringLiteral("redactSearchTitle"));
    m_subtitleLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, content);
    m_subtitleLabel->setObjectName(QStringLiteral("redactSearchSubtitle"));
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_subtitleLabel);
    layout->addLayout(titleLayout);

    // search-field 437:12252 (34 high) and the error of the field below it (as 435:7693)
    QVBoxLayout* fieldLayout = new QVBoxLayout();
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(4);
    m_field = new PDFLineEdit(content);
    m_field->setObjectName(QStringLiteral("redactSearchField"));
    m_field->setMaxLength(PDFDocumentToolRedact::MAXIMUM_QUERY_LENGTH + 1);
    m_field->setFixedHeight(34);
    m_field->setText(m_query);
    connect(m_field.data(), &QLineEdit::returnPressed, this, &PDFAppToolRedactSearchPart::onFieldReturnPressed);
    fieldLayout->addWidget(m_field);
    m_errorLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextDanger, content);
    m_errorLabel->setObjectName(QStringLiteral("redactSearchError"));
    m_errorLabel->setWordWrap(true);
    fieldLayout->addWidget(m_errorLabel);
    layout->addLayout(fieldLayout);

    // options 437:12255: Match case, Whole words / Regular expression / Across line breaks / Ignore
    // soft hyphens (checked, Disabled) with "Always on"
    QVBoxLayout* optionsLayout = new QVBoxLayout();
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(H::CHIP_GAP);
    const std::array<const char*, size_t(Option::Count)> objectNames = { "redactOptionMatchCase", "redactOptionWholeWords", "redactOptionRegex", "redactOptionAcrossLines", "redactOptionSoftHyphens" };
    const std::array<const char*, size_t(Option::Count)> iconNames = { "match-case", "whole-word", "regex", "corner-down-left", "text-lines" };
    for (size_t i = 0; i < m_optionButtons.size(); ++i)
    {
        QPushButton* button = H::createOptionButton(QString::fromLatin1(objectNames[i]), QString::fromLatin1(iconNames[i]), content);
        button->setProperty(H::OPTION_PROPERTY, int(i));
        button->setChecked(m_options[i]);
        m_optionButtons[i] = button;
    }
    QHBoxLayout* row = H::createRow(optionsLayout);
    row->addWidget(m_optionButtons[size_t(Option::MatchCase)]);
    row->addWidget(m_optionButtons[size_t(Option::WholeWords)]);
    row->addStretch(1);
    row = H::createRow(optionsLayout);
    row->addWidget(m_optionButtons[size_t(Option::RegularExpression)]);
    row->addStretch(1);
    row = H::createRow(optionsLayout);
    row->addWidget(m_optionButtons[size_t(Option::AcrossLineBreaks)]);
    row->addStretch(1);
    row = H::createRow(optionsLayout);
    row->addWidget(m_optionButtons[size_t(Option::IgnoreSoftHyphens)]);
    m_alwaysOnLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextMuted, content);
    m_alwaysOnLabel->setObjectName(QStringLiteral("redactOptionAlwaysOn"));
    row->addWidget(m_alwaysOnLabel);
    row->addStretch(1);
    layout->addLayout(optionsLayout);

    // K6.21: the flows of the text index always remove the soft hyphens
    m_optionButtons[size_t(Option::IgnoreSoftHyphens)]->setChecked(true);
    m_optionButtons[size_t(Option::IgnoreSoftHyphens)]->setEnabled(false);
    for (size_t i = 0; i < size_t(Option::IgnoreSoftHyphens); ++i)
    {
        connect(m_optionButtons[i].data(), &QPushButton::toggled, this, &PDFAppToolRedactSearchPart::onOptionToggled);
    }

    // results-head 437:12292: "7 matches on 3 pages", Select all
    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(8);
    m_resultsLabel = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 12, PDFThemeColors::TextPrimary, content);
    m_resultsLabel->setObjectName(QStringLiteral("redactSearchResults"));
    m_resultsLabel->setWordWrap(true);
    headLayout->addWidget(m_resultsLabel, 1);
    m_selectAllLink = new PDFTextLink(content);
    m_selectAllLink->setObjectName(QStringLiteral("redactSearchSelectAll"));
    connect(m_selectAllLink.data(), &PDFTextLink::clicked, this, &PDFAppToolRedactSearchPart::selectAll);
    headLayout->addWidget(m_selectAllLink, 0, Qt::AlignVCenter);
    layout->addLayout(headLayout);

    // results 437:12296: Search result rows 191:1073, 2 apart; the selected rows are marked
    PDFAppPanelListView* view = new PDFAppPanelListView(content);
    view->setObjectName(QStringLiteral("redactSearchView"));
    view->setContentPaddings(QMargins());
    PDFAppPanelWidgets::prepareView(view);
    view->setSpacing(1);
    view->setUniformItemSizes(false);
    view->setLayoutMode(QListView::Batched);
    view->setBatchSize(200);
    view->setSelectionMode(QAbstractItemView::MultiSelection);
    view->setItemDelegate(new PDFSearchResultDelegate(view));
    view->setModel(m_model);
    view->setFixedHeight(H::RESULTS_HEIGHT);
    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppToolRedactSearchPart::onSelectionChanged);
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppToolRedactSearchPart::onCurrentChanged);
    m_resultsView = view;
    layout->addWidget(view);

    m_noteLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, content);
    m_noteLabel->setObjectName(QStringLiteral("redactSearchNote"));
    layout->addWidget(m_noteLabel);

    // btn-mark-matches 437:12320 (Secondary, redact-text), btn-back-to-list 437:12325 (list)
    m_markButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("redact-text"), false, content);
    m_markButton->setObjectName(QStringLiteral("redactSearchMark"));
    m_markButton->setFixedHeight(36);
    connect(m_markButton.data(), &QPushButton::clicked, this, &PDFAppToolRedactSearchPart::onMarkClicked);
    layout->addWidget(m_markButton);
    m_backButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("list"), false, content);
    m_backButton->setObjectName(QStringLiteral("redactSearchBack"));
    m_backButton->setFixedHeight(36);
    connect(m_backButton.data(), &QPushButton::clicked, this, &PDFAppToolRedactSearchPart::onBackClicked);
    layout->addWidget(m_backButton);
    layout->addStretch(1);

    // The selection of the results is shown again (a new content after a change of the language)
    if (m_resultsView && m_resultsView->selectionModel())
    {
        const QSignalBlocker blocker(m_resultsView->selectionModel());
        for (size_t i = 0; i < m_matches.size(); ++i)
        {
            if (m_matches[i].isSelected)
            {
                m_resultsView->selectionModel()->select(m_model->index(int(i)), QItemSelectionModel::Select);
            }
        }
    }

    retranslate();
    updateWidgets();
    return content;
}

void PDFAppToolRedactSearchPart::retranslate()
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(tr("Search and mark"));
    }
    if (m_subtitleLabel)
    {
        m_subtitleLabel->setText(tr("Find text and mark every match"));
    }
    if (m_field)
    {
        m_field->setPlaceholderText(tr("Text or regular expression to find"));
        m_field->setAccessibleName(tr("Text to find and mark"));
    }

    const std::array<QString, size_t(Option::Count)> texts = { tr("Match case"), tr("Whole words"), tr("Regular expression"), tr("Across line breaks"), tr("Ignore soft hyphens") };
    for (size_t i = 0; i < m_optionButtons.size(); ++i)
    {
        if (m_optionButtons[i])
        {
            m_optionButtons[i]->setText(texts[i]);
        }
    }
    if (m_optionButtons[size_t(Option::AcrossLineBreaks)])
    {
        m_optionButtons[size_t(Option::AcrossLineBreaks)]->setToolTip(tr("A phrase is found also when a line ends inside of it"));
    }
    if (m_optionButtons[size_t(Option::IgnoreSoftHyphens)])
    {
        m_optionButtons[size_t(Option::IgnoreSoftHyphens)]->setToolTip(tr("A word divided by a soft hyphen at the end of a line is always found as one word"));
    }
    if (m_alwaysOnLabel)
    {
        m_alwaysOnLabel->setText(tr("Always on"));
    }
    if (m_selectAllLink)
    {
        m_selectAllLink->setText(tr("Select all"));
    }
    if (m_backButton)
    {
        m_backButton->setText(tr("Back to Marked Areas"));
    }
    if (m_resultsView)
    {
        m_resultsView->setAccessibleName(tr("Matches"));
    }
}

std::vector<PDFAppToolRedactSearchPart::Match> PDFAppToolRedactSearchPart::getSelectedMatches() const
{
    std::vector<Match> matches;
    for (const Match& match : m_matches)
    {
        if (match.isSelected)
        {
            matches.push_back(match);
        }
    }
    return matches;
}

int PDFAppToolRedactSearchPart::getSelectedCount() const
{
    int count = 0;
    for (const Match& match : m_matches)
    {
        count += match.isSelected ? 1 : 0;
    }
    return count;
}

int PDFAppToolRedactSearchPart::getMatchPageCount() const
{
    std::set<PDFInteger> pages;
    for (const Match& match : m_matches)
    {
        pages.insert(match.pageIndex);
    }
    return int(pages.size());
}

bool PDFAppToolRedactSearchPart::isTruncated() const
{
    return m_results && m_results->result.isTruncated;
}

bool PDFAppToolRedactSearchPart::isOptionChecked(Option option) const
{
    return option < Option::Count ? m_options[size_t(option)] : false;
}

void PDFAppToolRedactSearchPart::setOptionChecked(Option option, bool isChecked)
{
    if (option >= Option::IgnoreSoftHyphens)
    {
        // Always on (K6.21)
        return;
    }

    if (QPushButton* button = m_optionButtons[size_t(option)].data())
    {
        // The chip writes the option and searches again (onOptionToggled)
        button->setChecked(isChecked);
        return;
    }
    m_options[size_t(option)] = isChecked;
}

QPushButton* PDFAppToolRedactSearchPart::getOptionButton(Option option) const
{
    return option < Option::Count ? m_optionButtons[size_t(option)].data() : nullptr;
}

void PDFAppToolRedactSearchPart::clear()
{
    stopSearch();
    m_query.clear();
    m_error.clear();
    m_results.reset();
    m_matches.clear();
    m_markedCount = -1;
    m_state = State::Idle;
    if (m_model)
    {
        m_model->setResults(nullptr);
    }
    if (m_field)
    {
        // setText drops the undo history of the field (R6-19)
        m_field->setText(QString());
        m_field->setInvalid(false);
    }
    updateWidgets();
}

void PDFAppToolRedactSearchPart::setState(State state)
{
    m_state = state;
    updateWidgets();
}

void PDFAppToolRedactSearchPart::stopSearch()
{
    if (m_cancelFlag)
    {
        m_cancelFlag->cancel();
        m_cancelFlag.reset();
    }

    // The watcher does not wait for the worker, its result is not wanted
    if (Watcher* watcher = m_watcher.data())
    {
        m_watcher = nullptr;
        watcher->disconnect(this);
        watcher->deleteLater();
    }
}

void PDFAppToolRedactSearchPart::search()
{
    stopSearch();

    m_query = m_field ? m_field->text() : m_query;
    m_error.clear();
    m_markedCount = -1;
    m_results.reset();
    m_matches.clear();
    if (m_model)
    {
        m_model->setResults(nullptr);
    }

    if (m_query.isEmpty())
    {
        setState(State::Idle);
        Q_EMIT matchesChanged();
        return;
    }

    // The limits of 7.2 (the core refuses the same): 1,024 characters, a valid regular expression
    if (m_query.size() > PDFDocumentToolRedact::MAXIMUM_QUERY_LENGTH)
    {
        m_error = tr("The search text is too long – at most %1 characters.").arg(PDFAppLocale::formatNumber(PDFDocumentToolRedact::MAXIMUM_QUERY_LENGTH));
        setState(State::Failed);
        Q_EMIT matchesChanged();
        return;
    }
    if (isOptionChecked(Option::RegularExpression))
    {
        const QRegularExpression expression(m_query, QRegularExpression::UseUnicodePropertiesOption);
        if (!expression.isValid())
        {
            m_error = tr("The regular expression is not valid: %1.").arg(expression.errorString());
            setState(State::Failed);
            Q_EMIT matchesChanged();
            return;
        }
    }

    startSearch();
}

void PDFAppToolRedactSearchPart::startSearch()
{
    std::shared_ptr<const PDFDocumentTextIndex> index;
    if (m_indexer)
    {
        index = m_indexer->getIndex();
    }
    if (!index && m_document && !m_indexer)
    {
        index = m_document->getTextIndex();
    }

    if (!index)
    {
        if (!m_indexer || !m_document)
        {
            m_error = tr("The text of the document is not available.");
            setState(State::Failed);
            Q_EMIT matchesChanged();
            return;
        }

        // The index is built first (its request may run foreign code: the part is guarded)
        QPointer<PDFAppToolRedactSearchPart> guard(this);
        m_indexProgress = m_indexer->getProgress();
        m_state = State::Indexing;
        const bool isReady = m_indexer->requestIndex();
        if (!guard)
        {
            return;
        }
        if (!isReady || !m_indexer || !m_indexer->getIndex())
        {
            setState(State::Indexing);
            Q_EMIT matchesChanged();
            return;
        }
        index = m_indexer->getIndex();
    }

    // The rule of Across line breaks (K6.21) is the one of the core (the parameter search of a run)
    PDFDocumentTextSearchOptions options;
    options.caseSensitive = isOptionChecked(Option::MatchCase);
    options.wholeWords = isOptionChecked(Option::WholeWords);
    options.regularExpression = isOptionChecked(Option::RegularExpression);
    const bool isAcrossLineBreaks = isOptionChecked(Option::AcrossLineBreaks);

    // One job: copies of plain values and the flag only (the job knows no QObject)
    m_cancelFlag = std::make_shared<PDFAppCancelFlag>();
    Watcher* watcher = new Watcher(this);
    m_watcher = watcher;
    connect(watcher, &Watcher::finished, this, &PDFAppToolRedactSearchPart::onSearchFinished);
    watcher->setFuture(QtConcurrent::run(&PDFAppToolRedactSearchPart::runSearch, index, m_document ? m_document->getDocument() : PDFDocumentPointer(), m_query, options, isAcrossLineBreaks, m_cancelFlag));
    setState(State::Searching);
    Q_EMIT matchesChanged();
}

PDFAppToolRedactSearchPart::SearchResultsPointer PDFAppToolRedactSearchPart::runSearch(std::shared_ptr<const PDFDocumentTextIndex> index,
                                                                                    PDFDocumentPointer document,
                                                                                    QString query,
                                                                                    PDFDocumentTextSearchOptions options,
                                                                                    bool isAcrossLineBreaks,
                                                                                    std::shared_ptr<PDFAppCancelFlag> cancelFlag)
{
    std::shared_ptr<PDFAppDocumentSearchResults> results = std::make_shared<PDFAppDocumentSearchResults>();
    results->document = std::move(document);
    results->options = options;
    if (!index)
    {
        return results;
    }

    // An exception must not leave the worker: the watcher would throw it again from the slot of the
    // GUI thread
    try
    {
        results->result = PDFDocumentToolRedact::findMatches(*index, query, options, isAcrossLineBreaks, cancelFlag.get());
    }
    catch (const PDFException& exception)
    {
        results->result = PDFDocumentTextSearchResult();
        results->result.error = exception.getMessage();
    }
    return results;
}

void PDFAppToolRedactSearchPart::onSearchFinished()
{
    SearchResultsPointer results;
    if (Watcher* watcher = m_watcher.data())
    {
        results = watcher->result();
        m_watcher = nullptr;
        watcher->deleteLater();
    }
    m_cancelFlag.reset();

    if (!results || results->result.isCanceled)
    {
        setState(State::Idle);
        Q_EMIT matchesChanged();
        return;
    }

    m_results = results;
    m_matches.clear();
    if (!results->result.error.isEmpty())
    {
        m_error = results->result.error;
        if (m_model)
        {
            m_model->setResults(nullptr);
        }
        setState(State::Failed);
        Q_EMIT matchesChanged();
        return;
    }

    // Every result is selected at first (Find text and mark every match)
    m_matches.reserve(results->result.results.size());
    for (const PDFDocumentTextSearchMatch& found : results->result.results)
    {
        Match match;
        match.pageIndex = found.pageIndex;
        match.quadrilaterals = found.quadrilaterals;
        match.isSelected = true;
        m_matches.push_back(std::move(match));
    }

    if (m_model)
    {
        m_model->setResults(results);
    }
    if (m_resultsView && m_resultsView->selectionModel() && m_model && m_model->rowCount() > 0)
    {
        const QSignalBlocker blocker(m_resultsView->selectionModel());
        m_resultsView->selectionModel()->select(QItemSelection(m_model->index(0), m_model->index(m_model->rowCount() - 1)), QItemSelectionModel::Select);
        m_resultsView->viewport()->update();
    }
    setState(State::Finished);
    Q_EMIT matchesChanged();
}

void PDFAppToolRedactSearchPart::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // "4 Matches Marked" says what the last mark did; another version (an Undo of the marks) ends it
    // (the marks of the GUI set it again after their commit)
    m_markedCount = -1;

    // The marks themselves change only annotations: the text and its places stay. Another change of the
    // pages makes the results (their pages and quadrilaterals) the places of the old version.
    const PDFModifiedDocument::ModificationFlags textKeepingFlags(PDFModifiedDocument::Annotation | PDFModifiedDocument::FormField |
                                                                      PDFModifiedDocument::Authorization | PDFModifiedDocument::PreserveUndoRedo |
                                                                      PDFModifiedDocument::PreserveView | PDFModifiedDocument::Outline);
    const bool isTextChanged = modified.getFlags() == PDFModifiedDocument::None || (modified.getFlags() & ~textKeepingFlags) != PDFModifiedDocument::None;
    if (isTextChanged && (m_state == State::Indexing || m_state == State::Searching || m_state == State::Finished))
    {
        stopSearch();
        m_results.reset();
        m_matches.clear();
        if (m_model)
        {
            m_model->setResults(nullptr);
        }
        setState(State::Idle);
        Q_EMIT matchesChanged();
        return;
    }
    updateWidgets();
}

void PDFAppToolRedactSearchPart::onIndexReady()
{
    if (m_state == State::Indexing)
    {
        startSearch();
    }
}

void PDFAppToolRedactSearchPart::onIndexProgressChanged(int progress)
{
    m_indexProgress = progress;
    if (m_state == State::Indexing)
    {
        updateWidgets();
    }
}

void PDFAppToolRedactSearchPart::updateWidgets()
{
    using H = PDFAppToolRedactSearchPartHelper;

    const bool hasResults = m_state == State::Finished && !m_matches.empty();
    if (m_field)
    {
        m_field->setInvalid(m_state == State::Failed, m_state == State::Failed ? m_error : QString());
    }
    if (m_errorLabel)
    {
        m_errorLabel->setText(m_state == State::Failed ? m_error : QString());
        m_errorLabel->setVisible(m_state == State::Failed);
    }

    if (m_resultsLabel)
    {
        QString text;
        switch (m_state)
        {
            case State::Indexing:
                text = tr("Indexing the text… %1 %").arg(m_indexProgress);
                break;
            case State::Searching:
                text = tr("Searching…");
                break;
            case State::Finished:
                text = m_matches.empty() ? tr("No matches")
                                         : tr("%1 on %2").arg(H::plural(int(m_matches.size()), QT_TR_NOOP("%Ln match"), QT_TR_NOOP("%Ln matches")),
                                                              H::plural(getMatchPageCount(), QT_TR_NOOP("%Ln page"), QT_TR_NOOP("%Ln pages")));
                break;
            default:
                break;
        }
        m_resultsLabel->setText(text);
        m_resultsLabel->setVisible(!text.isEmpty());
    }
    if (m_selectAllLink)
    {
        m_selectAllLink->setVisible(hasResults);
        m_selectAllLink->setEnabled(hasResults && getSelectedCount() < int(m_matches.size()));
    }
    if (m_resultsView)
    {
        m_resultsView->setVisible(hasResults);
    }
    if (m_noteLabel)
    {
        const bool isTruncatedNote = hasResults && isTruncated();
        m_noteLabel->setText(isTruncatedNote ? tr("Only the first %1 matches are shown. Search for a longer text to find the others.").arg(PDFAppLocale::formatNumber(PDFDocumentToolRedact::MAXIMUM_SEARCH_RESULT_COUNT))
                                             : QString());
        m_noteLabel->setVisible(isTruncatedNote);
    }
    if (m_markButton)
    {
        // 437:12113 "Mark 4 Selected Matches", 437:12452 "4 Matches Marked" (Disabled)
        const int selected = getSelectedCount();
        if (m_markedCount >= 0)
        {
            m_markButton->setText(H::plural(m_markedCount, QT_TR_NOOP("%Ln Match Marked"), QT_TR_NOOP("%Ln Matches Marked")));
            m_markButton->setEnabled(false);
        }
        else
        {
            m_markButton->setText(selected > 0 ? H::plural(selected, QT_TR_NOOP("Mark %Ln Selected Match"), QT_TR_NOOP("Mark %Ln Selected Matches")) : tr("Mark Selected Matches"));
            m_markButton->setEnabled(hasResults && selected > 0);
        }
        m_markButton->setVisible(hasResults);
    }
}

void PDFAppToolRedactSearchPart::selectAll()
{
    if (!m_resultsView || !m_resultsView->selectionModel() || !m_model || m_model->rowCount() == 0)
    {
        return;
    }

    // The selection writes the matches (onSelectionChanged emits)
    m_resultsView->selectionModel()->select(QItemSelection(m_model->index(0), m_model->index(m_model->rowCount() - 1)), QItemSelectionModel::Select);
}

void PDFAppToolRedactSearchPart::setMarkedCount(int count)
{
    m_markedCount = count;
    updateWidgets();
}

void PDFAppToolRedactSearchPart::updateSelectionFromView()
{
    if (!m_resultsView || !m_resultsView->selectionModel())
    {
        return;
    }

    const QItemSelectionModel* selection = m_resultsView->selectionModel();
    for (size_t i = 0; i < m_matches.size(); ++i)
    {
        m_matches[i].isSelected = selection->isRowSelected(int(i), QModelIndex());
    }
}

void PDFAppToolRedactSearchPart::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);

    if (m_isUpdatingSelection)
    {
        return;
    }

    // The state and the parts follow at once; Qt reads the selection model after its signal and the slots of
    // matchesChanged may delete the tab with the list: the signal follows from the event loop
    updateSelectionFromView();
    m_markedCount = -1;
    updateWidgets();
    if (!m_isSelectionChangeQueued)
    {
        m_isSelectionChangeQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolRedactSearchPart::onQueuedSelectionChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolRedactSearchPart::onQueuedSelectionChanged()
{
    m_isSelectionChangeQueued = false;
    Q_EMIT matchesChanged();
}

void PDFAppToolRedactSearchPart::onCurrentChanged()
{
    // The page of the view runs code of the application (its signals may delete the tab with the list), while Qt
    // reads the selection model after its signal: the match is shown from the event loop
    if (!m_isCurrentChangeQueued)
    {
        m_isCurrentChangeQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolRedactSearchPart::onQueuedCurrentChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolRedactSearchPart::onQueuedCurrentChanged()
{
    // The row shows its match on its page (the current row of now)
    m_isCurrentChangeQueued = false;
    const QModelIndex current = m_resultsView && m_resultsView->selectionModel() ? m_resultsView->selectionModel()->currentIndex() : QModelIndex();
    if (!current.isValid() || current.row() >= int(m_matches.size()) || !m_view)
    {
        return;
    }
    m_view->goToPage(m_matches[size_t(current.row())].pageIndex);
}

void PDFAppToolRedactSearchPart::onFieldReturnPressed()
{
    search();
}

void PDFAppToolRedactSearchPart::onOptionToggled(bool isChecked)
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button)
    {
        return;
    }

    const int option = button->property(PDFAppToolRedactSearchPartHelper::OPTION_PROPERTY).toInt();
    if (option < 0 || option >= int(Option::IgnoreSoftHyphens))
    {
        return;
    }

    m_options[size_t(option)] = isChecked;
    if (!m_query.isEmpty() || (m_field && !m_field->text().isEmpty()))
    {
        // A shown search is searched again with the new options
        search();
    }
}

void PDFAppToolRedactSearchPart::onMarkClicked()
{
    if (getSelectedCount() > 0 && m_markedCount < 0)
    {
        Q_EMIT markRequested();
    }
}

void PDFAppToolRedactSearchPart::onBackClicked()
{
    Q_EMIT backRequested();
}

}   // namespace pdf

#include "moc_pdfapptoolredactsearchpart.cpp"
