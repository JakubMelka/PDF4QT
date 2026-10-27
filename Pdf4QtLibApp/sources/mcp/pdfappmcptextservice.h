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


#ifndef PDFAPPMCPTEXTSERVICE_H
#define PDFAPPMCPTEXTSERVICE_H

#include "pdfappglobal.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcpjob.h"

#include "pdfdocumenttextindex.h"
#include "pdftextlayout.h"

#include <QObject>
#include <QPointer>
#include <QRectF>

#include <memory>
#include <set>
#include <vector>

class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTextIndexer;
class PDFAppMcpHost;

/// Geometry of the text returned by text.get_text
enum class PDFAppMcpTextGeometry
{
    None,
    Lines,
    Words
};

/// What a text job does
enum class PDFAppMcpTextJobKind
{
    GetText,        ///< text.get_text and the text resources
    Search,         ///< text.search
    TextInArea      ///< text.get_text_in_area
};

/// Parameters of a text job (copied in the GUI thread)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTextRequest
{
public:
    PDFAppMcpTextJobKind kind = PDFAppMcpTextJobKind::GetText;
    QString actionId;                       ///< "text.get_text" (activity)
    PDFAppMcpDocumentHeader header;
    std::vector<PDFInteger> pageIndices;    ///< GetText: the pages; Search: the filter (empty = all); TextInArea: one page
    PDFAppMcpTextGeometry geometry = PDFAppMcpTextGeometry::None;
    qint64 maximumCharacters = 100000;
    QString query;
    PDFDocumentTextSearchOptions searchOptions;
    int maximumResults = 100;
    QRectF area;                            ///< TextInArea: the area in the page space
    bool isResource = false;                ///< The result is the text of a resource (plain text), not a tool result
    QString resourceUri;                    ///< URI of the resource
};

/// Text of the documents for the tools and the resources (plan of step 7, 2.3
/// PDFAppMcpTextService): the text index of the document (built by the indexer
/// of its tab, when it is missing - with the progress "Indexing text N %"), or
/// the text layout of a single page for at most 2 pages without an index. The
/// work runs in a worker on a snapshot of the document.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTextService
{
public:
    PDFAppMcpTextService() = delete;

    /// Pages read without the index of the whole document
    static constexpr int SINGLE_PAGE_LIMIT = 2;

    /// Most characters of a text resource of the whole document (4 MiB)
    static constexpr qint64 MAXIMUM_RESOURCE_CHARACTERS = 4 * 1024 * 1024;

    /// Starts the request: the text index is waited for or built, then a job
    /// finishes the call (also the activity row)
    static void start(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppDocument* document, PDFAppMcpTextRequest request);

    /// Text layout of one page (a worker; its own caches)
    static PDFTextLayout createPageLayout(const PDFDocument* document, PDFInteger pageIndex);

    /// Plain text of a text layout in the reading order of its blocks (the text of the index)
    static QString getLayoutText(const PDFTextLayout& layout, PDFInteger pageIndex);

    /// The words of a layout (separated by white space) with their rectangles in the page space
    static std::vector<std::pair<QString, QRectF>> getWords(const PDFTextLayout& layout);

    /// The lines of a layout with their rectangles in the page space
    static std::vector<std::pair<QString, QRectF>> getLines(const PDFTextLayout& layout);

    /// Text in an area of the page: the words, whose centers lie in the area, in
    /// the order of the lines (lines separated by newlines)
    static QString getAreaText(const PDFTextLayout& layout, const QRectF& area, std::vector<std::pair<QString, QRectF>>* words);

    /// The existing indexer of the tab of the document (nullptr = none)
    static PDFAppDocumentTextIndexer* findIndexer(PDFAppMcpHost* host, PDFAppDocument* document);
};

/// A text job (worker): the text, the search or the text of an area
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTextJob : public PDFAppMcpJob
{
public:
    PDFAppMcpTextJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpTextRequest request,
                     std::shared_ptr<const PDFDocumentTextIndex> index);

    virtual void execute() override;
    virtual void finish(PDFAppMcpHost* host) override;

private:
    PDFTextLayout getLayout(PDFInteger pageIndex) const;
    void executeGetText();
    void executeSearch();
    void executeTextInArea();

    PDFAppMcpTextRequest m_request;
    std::shared_ptr<const PDFDocumentTextIndex> m_index;
    std::shared_ptr<const PDFDocumentTextIndex> m_builtIndex;   ///< An index built by the job (given to the document)
    std::set<int> m_pages;      ///< Pages read (activity)
    bool m_isFailed = false;
};

/// Waits for the text index of a document (the indexer of its tab), reports
/// the progress of its build and starts the job, when it is ready. It ends with
/// the call (canceled), the document or the indexer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTextIndexWaiter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    PDFAppMcpTextIndexWaiter(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, PDFAppDocument* document, PDFAppDocumentTextIndexer* indexer, PDFAppMcpTextRequest request);
    virtual ~PDFAppMcpTextIndexWaiter() override;

    /// Requests the index (it may be ready at once)
    void start();

private:
    void onIndexReady();
    void onBuildingChanged();
    void onProgressChanged(int progress);
    void onCheckTimer();
    void startJob(std::shared_ptr<const PDFDocumentTextIndex> index);
    void fail(const QString& text);

    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFMcpCall> m_call;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentTextIndexer> m_indexer;
    QPointer<QTimer> m_checkTimer;
    PDFAppMcpTextRequest m_request;
    int m_lastProgress = -1;
    int m_requestCount = 0;
    bool m_isDone = false;
};

}   // namespace pdf

#endif // PDFAPPMCPTEXTSERVICE_H
