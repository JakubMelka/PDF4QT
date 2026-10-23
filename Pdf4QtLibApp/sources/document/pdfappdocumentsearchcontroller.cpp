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

#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"

#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfexception.h"
#include "pdfpainterutils.h"

#include "pdfcomponentdelegates.h"
#include "pdfthememanager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <new>

namespace pdf
{

PDFAppDocumentSearchResultModel::PDFAppDocumentSearchResultModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppDocumentSearchResultModel::~PDFAppDocumentSearchResultModel() = default;

void PDFAppDocumentSearchResultModel::setResults(PDFAppDocumentSearchResultsPointer results)
{
    beginResetModel();
    m_results = std::move(results);
    endResetModel();
}

int PDFAppDocumentSearchResultModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_results)
    {
        return 0;
    }
    return int(m_results->result.results.size());
}

QVariant PDFAppDocumentSearchResultModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_results || index.row() < 0 || size_t(index.row()) >= m_results->result.results.size())
    {
        return QVariant();
    }

    const PDFDocumentTextSearchMatch& match = m_results->result.results[size_t(index.row())];
    switch (role)
    {
        case Qt::DisplayRole:
            return getPageText(m_results->document.data(), match.pageIndex);

        case Qt::ToolTipRole:
            return match.context;

        case PDFComponentRoles::TextRole:
            return createSnippet(match.context, match.contextMatchStart, match.contextMatchLength, nullptr);

        case PDFComponentRoles::MatchRangesRole:
        {
            qsizetype start = -1;
            createSnippet(match.context, match.contextMatchStart, match.contextMatchLength, &start);
            QVariantList ranges;
            if (start >= 0 && match.contextMatchLength > 0)
            {
                ranges << QPoint(int(start), int(match.contextMatchLength));
            }
            return ranges;
        }

        default:
            break;
    }

    return QVariant();
}

QString PDFAppDocumentSearchResultModel::getPageText(const PDFDocument* document, PDFInteger pageIndex)
{
    const QString label = document ? document->getCatalog()->getPageLabel(pageIndex) : QString();
    const QString number = QString::number(pageIndex + 1);
    if (label.isEmpty() || label == number)
    {
        return tr("Page %1").arg(number);
    }
    return tr("Page %1 (%2)").arg(number, label);
}

QString PDFAppDocumentSearchResultModel::createSnippet(const QString& context, qsizetype matchStart, qsizetype matchLength, qsizetype* snippetMatchStart)
{
    if (snippetMatchStart)
    {
        *snippetMatchStart = -1;
    }

    if (matchStart < 0 || matchLength <= 0 || matchStart + matchLength > context.size())
    {
        // The occurrence is not known: the beginning of the context
        QString snippet = context.simplified();
        if (snippet.size() > SNIPPET_BEFORE + SNIPPET_AFTER)
        {
            snippet = snippet.left(SNIPPET_BEFORE + SNIPPET_AFTER) + QChar(0x2026);
        }
        return snippet;
    }

    // The part before the occurrence, cut at a space
    qsizetype begin = 0;
    bool isBeginCut = false;
    if (matchStart > SNIPPET_BEFORE)
    {
        begin = matchStart - SNIPPET_BEFORE;
        const qsizetype space = context.indexOf(QLatin1Char(' '), begin);
        if (space >= 0 && space < matchStart)
        {
            begin = space + 1;
        }
        isBeginCut = true;
    }

    // The part after the occurrence, cut at a space
    qsizetype end = context.size();
    bool isEndCut = false;
    const qsizetype matchEnd = matchStart + matchLength;
    if (end - matchEnd > SNIPPET_AFTER)
    {
        end = matchEnd + SNIPPET_AFTER;
        const qsizetype space = context.lastIndexOf(QLatin1Char(' '), end);
        if (space > matchEnd)
        {
            end = space;
        }
        isEndCut = true;
    }

    // White space is simplified in each part, so the position of the occurrence
    // is known exactly
    const QString before = context.mid(begin, matchStart - begin).simplified();
    const QString matched = context.mid(matchStart, matchLength).simplified();
    const QString after = context.mid(matchEnd, end - matchEnd).simplified();

    QString snippet;
    if (isBeginCut)
    {
        snippet += QChar(0x2026);
    }
    snippet += before;
    if (!before.isEmpty() && matchStart > 0 && context.at(matchStart - 1).isSpace())
    {
        snippet += QLatin1Char(' ');
    }

    if (snippetMatchStart)
    {
        *snippetMatchStart = snippet.size();
    }
    snippet += matched;

    if (!after.isEmpty() && matchEnd < context.size() && context.at(matchEnd).isSpace())
    {
        snippet += QLatin1Char(' ');
    }
    snippet += after;
    if (isEndCut)
    {
        snippet += QChar(0x2026);
    }

    // The length of the occurrence may have changed by the simplification
    if (snippetMatchStart && matched.size() != matchLength)
    {
        *snippetMatchStart = -1;
    }
    return snippet;
}

void PDFAppDocumentSearchHighlighter::setResults(PDFAppDocumentSearchResultsPointer results)
{
    m_results = std::move(results);
    m_currentIndex = -1;
}

QPainterPath PDFAppDocumentSearchHighlighter::getMatchPath(const PDFDocumentTextSearchMatch& match, const QTransform& matrix, qreal inset)
{
    QPainterPath path;
    const QPolygonF& quadrilaterals = match.quadrilaterals;
    for (qsizetype i = 0; i + 3 < quadrilaterals.size(); i += 4)
    {
        // The order of the quad points: top left, top right, bottom left, bottom right
        QPolygonF polygon;
        polygon << quadrilaterals[i] << quadrilaterals[i + 1] << quadrilaterals[i + 3] << quadrilaterals[i + 2];
        polygon = matrix.map(polygon);

        QRectF rect = polygon.boundingRect();
        const bool isRectangle = qFuzzyCompare(polygon[0].y(), polygon[1].y()) && qFuzzyCompare(polygon[2].y(), polygon[3].y()) &&
                                 qFuzzyCompare(polygon[0].x(), polygon[3].x()) && qFuzzyCompare(polygon[1].x(), polygon[2].x());
        if (isRectangle)
        {
            const qreal radius = qMax(0.0, 2.0 - inset);
            rect = rect.adjusted(inset, inset, -inset, -inset);
            if (rect.isValid())
            {
                path.addRoundedRect(rect, radius, radius);
            }
        }
        else if (inset <= 0.0)
        {
            path.addPolygon(polygon);
            path.closeSubpath();
        }
    }
    return path;
}

void PDFAppDocumentSearchHighlighter::drawPage(QPainter* painter,
                                               PDFInteger pageIndex,
                                               const PDFPrecompiledPage* compiledPage,
                                               PDFTextLayoutGetter& layoutGetter,
                                               const QTransform& pagePointToDevicePointMatrix,
                                               const PDFColorConvertor& convertor,
                                               QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);

    if (!painter || !m_results || m_results->result.results.empty())
    {
        return;
    }

    // The occurrences are sorted by the pages
    const PDFDocumentTextSearchMatches& matches = m_results->result.results;
    auto first = std::lower_bound(matches.cbegin(), matches.cend(), pageIndex,
                                  &PDFAppDocumentSearchHighlighter::isMatchBeforePage);
    if (first == matches.cend() || first->pageIndex != pageIndex)
    {
        return;
    }

    QPainterPath matchPath;
    QPainterPath currentPath;
    QPainterPath currentInside;
    for (auto it = first; it != matches.cend() && it->pageIndex == pageIndex; ++it)
    {
        const int index = int(std::distance(matches.cbegin(), it));
        if (index == m_currentIndex)
        {
            currentPath = getMatchPath(*it, pagePointToDevicePointMatrix);
            currentInside = getMatchPath(*it, pagePointToDevicePointMatrix, 2.0);
        }
        else
        {
            matchPath.addPath(getMatchPath(*it, pagePointToDevicePointMatrix));
        }
    }

    // The colors of the theme (the highlights are a part of the user interface,
    // not of the page), multiplied with the page as in the design
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setCompositionMode(QPainter::CompositionMode_Multiply);
    painter->setPen(Qt::NoPen);
    painter->setBrush(manager->getColor(PDFThemeColors::SearchMatch));
    painter->drawPath(matchPath);

    // The current one: the border of 2 px inside of its shape and the fill
    // inside of the border, both multiplied with the page (a rotated occurrence
    // has no inside, it is the border color)
    if (!currentPath.isEmpty())
    {
        painter->setBrush(manager->getColor(PDFThemeColors::SearchMatchBorder));
        painter->drawPath(currentPath.subtracted(currentInside));
        painter->setBrush(manager->getColor(PDFThemeColors::SearchMatchCurrent));
        painter->drawPath(currentInside);
    }
}

bool PDFAppDocumentSearchHighlighter::isMatchBeforePage(const PDFDocumentTextSearchMatch& match, PDFInteger pageIndex)
{
    return match.pageIndex < pageIndex;
}

PDFAppDocumentSearchController::PDFAppDocumentSearchController(PDFAppDocument* document, PDFAppDocumentView* view, PDFAppDocumentTextIndexer* indexer, QObject* parent) :
    BaseClass(parent),
    m_document(document),
    m_view(view),
    m_indexer(indexer),
    m_highlighter(std::make_unique<PDFAppDocumentSearchHighlighter>())
{
    m_model = new PDFAppDocumentSearchResultModel(this);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);
    connect(m_refreshTimer, &QTimer::timeout, this, &PDFAppDocumentSearchController::onRefreshTimer);

    if (indexer)
    {
        connect(indexer, &PDFAppDocumentTextIndexer::indexReady, this, &PDFAppDocumentSearchController::onIndexReady);
        connect(indexer, &PDFAppDocumentTextIndexer::buildingChanged, this, &PDFAppDocumentSearchController::onIndexBuildingChanged);
        connect(indexer, &PDFAppDocumentTextIndexer::progressChanged, this, &PDFAppDocumentSearchController::onIndexProgressChanged);
    }

    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentSearchController::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentSearchController::onDocumentStateChanged);
    }

    m_proxy = view ? view->getProxy() : nullptr;
    if (m_proxy)
    {
        m_proxy->registerDrawInterface(m_highlighter.get());
    }
}

PDFAppDocumentSearchController::~PDFAppDocumentSearchController()
{
    stopSearch();
    if (m_proxy)
    {
        m_proxy->unregisterDrawInterface(m_highlighter.get());
        m_proxy->repaintNeeded();
    }
}

PDFDrawWidgetProxy* PDFAppDocumentSearchController::getProxy() const
{
    return m_proxy.data();
}

void PDFAppDocumentSearchController::search(const QString& query)
{
    if (query.isEmpty())
    {
        clear();
        return;
    }

    // The results of another text are not shown (nor walked by F3 / Enter) while
    // the new text is searched
    QPointer<PDFAppDocumentSearchController> guard(this);
    if (query != m_query)
    {
        dropResults();
        if (!guard)
        {
            return;
        }
    }

    m_query = query;
    searchAgain();
}

void PDFAppDocumentSearchController::searchAgain()
{
    stopSearch();
    m_refreshTimer->stop();
    m_error.clear();

    if (m_query.isEmpty())
    {
        clear();
        return;
    }

    // The listeners learn the new query also when the state stays (a new text
    // while another one is searched or indexed)
    const State oldState = m_state;
    QPointer<PDFAppDocumentSearchController> guard(this);
    if (!m_document || !m_document->isReady() || !m_indexer)
    {
        // The search starts, when the document is shown (onDocumentStateChanged)
        setState(State::Indexing);
    }
    else
    {
        // The indexer announces a started build (buildingChanged): a slot may
        // delete the tab and this controller with it
        const bool isIndexReady = m_indexer->requestIndex();
        if (!guard)
        {
            return;
        }

        if (isIndexReady)
        {
            startSearch();
        }
        else if (m_indexer && m_indexer->isBuilding())
        {
            setState(State::Indexing);
        }
        else
        {
            setState(State::Idle);
        }
    }

    if (guard && m_state == oldState)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSearchController::dropResults()
{
    if (!m_results && m_currentIndex < 0)
    {
        return;
    }

    m_results.reset();
    m_currentIndex = -1;
    m_model->setResults(nullptr);
    updateHighlights();
    Q_EMIT currentIndexChanged(-1);
}

void PDFAppDocumentSearchController::clear()
{
    stopSearch();
    m_refreshTimer->stop();

    const bool hadResults = m_results != nullptr;
    m_query.clear();
    m_error.clear();
    m_results.reset();
    m_currentIndex = -1;
    m_model->setResults(nullptr);
    updateHighlights();

    m_state = State::Idle;

    QPointer<PDFAppDocumentSearchController> guard(this);
    if (hadResults)
    {
        Q_EMIT currentIndexChanged(-1);
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSearchController::setOptions(const PDFDocumentTextSearchOptions& options)
{
    if (m_options == options)
    {
        return;
    }

    m_options = options;
    if (!m_query.isEmpty())
    {
        // The results of the old options are not shown during the new search
        QPointer<PDFAppDocumentSearchController> guard(this);
        dropResults();
        if (guard)
        {
            searchAgain();
        }
    }
    else
    {
        Q_EMIT stateChanged();
    }
}

int PDFAppDocumentSearchController::getIndexProgress() const
{
    return m_indexer ? m_indexer->getProgress() : 0;
}

int PDFAppDocumentSearchController::getResultCount() const
{
    return m_results ? int(m_results->result.results.size()) : 0;
}

bool PDFAppDocumentSearchController::isTruncated() const
{
    return m_results && m_results->result.isTruncated;
}

void PDFAppDocumentSearchController::setState(State state)
{
    if (m_state == state)
    {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
}

void PDFAppDocumentSearchController::startSearch()
{
    std::shared_ptr<const PDFDocumentTextIndex> index = m_indexer ? m_indexer->getIndex() : nullptr;
    if (!index || !m_document)
    {
        setState(State::Idle);
        return;
    }

    m_cancelFlag = std::make_shared<PDFAppCancelFlag>();
    m_watcher = new Watcher(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &PDFAppDocumentSearchController::onSearchFinished);
    m_watcher->setFuture(QtConcurrent::run(&PDFAppDocumentSearchController::runSearch, index, m_document->getDocument(), m_query, m_options, m_cancelFlag));
    setState(State::Searching);
}

void PDFAppDocumentSearchController::stopSearch()
{
    if (m_cancelFlag)
    {
        m_cancelFlag->cancel();
        m_cancelFlag.reset();
    }

    // The watcher does not wait for the worker, its result is not wanted
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
}

PDFAppDocumentSearchResultsPointer PDFAppDocumentSearchController::runSearch(std::shared_ptr<const PDFDocumentTextIndex> index,
                                                                            PDFDocumentPointer document,
                                                                            QString query,
                                                                            PDFDocumentTextSearchOptions options,
                                                                            std::shared_ptr<PDFAppCancelFlag> cancelFlag)
{
    std::shared_ptr<PDFAppDocumentSearchResults> results = std::make_shared<PDFAppDocumentSearchResults>();
    results->document = std::move(document);
    results->query = query;
    results->options = options;
    if (index)
    {
        // An exception must not leave the worker: the watcher would throw it again
        // from the slot of the GUI thread
        try
        {
            results->result = PDFDocumentTextSearch::find(*index, query, options, MAXIMUM_RESULTS, cancelFlag.get());
        }
        catch (const PDFException& exception)
        {
            results->result = PDFDocumentTextSearchResult();
            results->result.error = exception.getMessage();
        }
        catch (const std::bad_alloc&)
        {
            results->result = PDFDocumentTextSearchResult();
            results->result.error = tr("There is not enough memory to search the document.");
        }
    }
    return results;
}

void PDFAppDocumentSearchController::onSearchFinished()
{
    PDFAppDocumentSearchResultsPointer results;
    if (m_watcher)
    {
        results = m_watcher->result();
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    m_cancelFlag.reset();

    if (!results || results->result.isCanceled)
    {
        setState(State::Idle);
        return;
    }

    m_results = results;
    m_error = results->result.error;
    m_currentIndex = -1;
    m_model->setResults(m_results);

    // The first occurrence on the current page or after it
    int currentIndex = -1;
    const PDFDocumentTextSearchMatches& matches = m_results->result.results;
    if (!matches.empty())
    {
        const PDFInteger currentPage = m_view ? qMax(PDFInteger(0), m_view->getCurrentPageIndex()) : 0;
        auto it = std::lower_bound(matches.cbegin(), matches.cend(), currentPage, &PDFAppDocumentSearchHighlighter::isMatchBeforePage);
        currentIndex = it != matches.cend() ? int(std::distance(matches.cbegin(), it)) : 0;
    }

    QPointer<PDFAppDocumentSearchController> guard(this);
    m_state = m_error.isEmpty() ? State::Finished : State::Failed;
    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }

    updateHighlights();
    if (currentIndex >= 0)
    {
        setCurrentIndex(currentIndex);
    }
    else if (m_view && m_view->getStatusBar() && m_error.isEmpty())
    {
        m_view->getStatusBar()->showMessage(tr("No results"), 5000);
    }
}

void PDFAppDocumentSearchController::setCurrentIndex(int index)
{
    if (index < 0 || index >= getResultCount())
    {
        return;
    }

    m_currentIndex = index;
    m_highlighter->setCurrentIndex(index);
    showCurrentMatch();
    Q_EMIT currentIndexChanged(index);
}

void PDFAppDocumentSearchController::goToNext()
{
    const int count = getResultCount();
    if (count > 0)
    {
        setCurrentIndex(m_currentIndex < 0 ? 0 : (m_currentIndex + 1) % count);
    }
}

void PDFAppDocumentSearchController::goToPrevious()
{
    const int count = getResultCount();
    if (count > 0)
    {
        setCurrentIndex(m_currentIndex <= 0 ? count - 1 : m_currentIndex - 1);
    }
}

void PDFAppDocumentSearchController::showCurrentMatch()
{
    if (!m_results || m_currentIndex < 0 || m_currentIndex >= getResultCount())
    {
        return;
    }

    const PDFDocumentTextSearchMatch& match = m_results->result.results[size_t(m_currentIndex)];
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        if (m_document && match.pageIndex >= 0 && match.pageIndex < m_document->getPageCount())
        {
            proxy->goToPageAndEnsureVisible(match.pageIndex, match.boundingRect);
        }
        proxy->repaintNeeded();
    }

    if (m_view && m_view->getStatusBar())
    {
        m_view->getStatusBar()->showMessage(tr("Result %1 of %2").arg(QLocale().toString(m_currentIndex + 1), QLocale().toString(getResultCount())), 5000);
    }
}

void PDFAppDocumentSearchController::updateHighlights()
{
    m_highlighter->setResults(m_results);
    m_highlighter->setCurrentIndex(m_currentIndex);
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        proxy->repaintNeeded();
    }
}

void PDFAppDocumentSearchController::onIndexReady()
{
    if (m_state == State::Indexing && !m_query.isEmpty())
    {
        startSearch();
    }
}

void PDFAppDocumentSearchController::onIndexBuildingChanged()
{
    // The build has been stopped (Stop of the operation): the search ends
    if (m_state == State::Indexing && m_indexer && !m_indexer->isBuilding() && !m_indexer->getIndex())
    {
        setState(State::Idle);
    }
}

void PDFAppDocumentSearchController::onIndexProgressChanged()
{
    if (m_state == State::Indexing)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSearchController::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // The occurrences and their geometry belong to the old page contents
    if ((modified.hasReset() || modified.hasPageContentsChanged()) && !m_query.isEmpty())
    {
        m_refreshTimer->start();
    }
}

void PDFAppDocumentSearchController::onDocumentStateChanged()
{
    if (m_document && m_document->isReady() && m_state == State::Indexing && !m_query.isEmpty() && !m_watcher && (!m_indexer || !m_indexer->isBuilding()))
    {
        searchAgain();
    }
}

void PDFAppDocumentSearchController::onRefreshTimer()
{
    if (!m_query.isEmpty())
    {
        searchAgain();
    }
}

}   // namespace pdf
