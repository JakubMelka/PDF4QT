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

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextindex.h"
#include "pdfdocumentwriter.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfsignaturehandler.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>
#include <QtTest>

#include <atomic>

/// PDFDocumentTextIndex, PDFDocumentTextSearch and the page labels of PDFCatalog
/// (Pdf4QtLibCore, step 3): the build (parallel, the counter of the pages, the
/// cancellation), the page text, the options of the search (match case, whole
/// words, regular expressions, wildcards), the context of an occurrence with its
/// position and its geometry, the order of the results of the pages searched in
/// parallel, the limits (results across the chunks, the query), the guards of a
/// hostile query (empty matches, catastrophic backtracking, invalid expressions,
/// broken UTF-16), the cancellation of the search, the benchmark of 2000 pages;
/// the trust of the AATL list of PDFSignatureHandler::Parameters; the page
/// labels with hostile values.
class DocumentTextIndexTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void buildIndex();
    void buildProgress();
    void search();
    void searchWildcards();
    void searchWholeWordsPunctuation();
    void searchContext();
    void searchGeometry();
    void searchOrderAcrossChunks();
    void searchLimits();
    void searchLimitAcrossChunks();
    void searchEmptyMatches();
    void searchHostileQueries();
    void searchCatastrophicBacktracking();
    void cancel();
    void cancelDuringSearch();
    void createPattern();
    void flowFindEmptyMatch();
    void aatlParameter();
    void benchmark2000Pages();
    void pageLabels();
    void pageLabelsHostile();

private:
    static pdf::PDFDocument createDocument(int pageCount);
    static pdf::PDFDocument createDocument(const QStringList& pageTexts);
    static pdf::PDFDocument createLabeledDocument(const pdf::PDFObject& nums, int pageCount);
};

/// Cancellation of the test (canceled from the start)
class CanceledOperation : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Cancellation after a number of questions (the search asks before each page)
class CountdownOperation : public pdf::PDFOperationControl
{
public:
    explicit CountdownOperation(int count) : m_count(count) { }

    virtual bool isOperationCancelled() const override { return m_count.fetch_sub(1) <= 0; }

private:
    mutable std::atomic<int> m_count;
};

pdf::PDFDocument DocumentTextIndexTest::createDocument(int pageCount)
{
    QStringList texts;
    for (int i = 0; i < pageCount; ++i)
    {
        texts << QStringLiteral("Page %1 Alpha alphabet\nbeta gamma").arg(i + 1);
    }
    return createDocument(texts);
}

pdf::PDFDocument DocumentTextIndexTest::createDocument(const QStringList& pageTexts)
{
    pdf::PDFDocumentBuilder builder;
    for (const QString& pageText : pageTexts)
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        pdf::PDFPageContentStreamBuilder content(&builder);
        QPainter* painter = content.begin(page);
        if (!painter)
        {
            continue;
        }
        QFont font = painter->font();
        font.setPixelSize(12);
        painter->setFont(font);
        const QStringList lines = pageText.split(QLatin1Char('\n'));
        for (int line = 0; line < lines.size(); ++line)
        {
            painter->drawText(QPointF(30, 40 + 20 * line), lines[line]);
        }
        content.end(painter);
    }

    // The document is written and read back as the application reads a file
    const pdf::PDFDocument built = builder.build();
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    pdf::PDFDocumentWriter(nullptr).write(&buffer, &built);
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(buffer.data());
}

pdf::PDFDocument DocumentTextIndexTest::createLabeledDocument(const pdf::PDFObject& nums, int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, 100, 100));
    }

    pdf::PDFDictionaryBuilder labels;
    labels.setEntry(pdf::PDFInplaceOrMemoryString("Nums"), nums);
    pdf::PDFDictionaryBuilder catalog;
    catalog.setEntry(pdf::PDFInplaceOrMemoryString("PageLabels"), pdf::PDFObject::createDictionary(std::move(labels)));
    builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalog)));
    return builder.build();
}

void DocumentTextIndexTest::initTestCase()
{
    // The offscreen platform has no fonts: the text of the samples would be drawn as
    // empty boxes (paths). The bundled Inter font of Pdf4QtLibFluent is used.
    const QDir fonts(QStringLiteral(PDF4QT_TEST_FONT_DIRECTORY));
    for (const QString& file : fonts.entryList({ QStringLiteral("*.ttf") }, QDir::Files))
    {
        QVERIFY(QFontDatabase::addApplicationFont(fonts.absoluteFilePath(file)) >= 0);
    }
    QGuiApplication::setFont(QFont(QStringLiteral("Inter")));
}

void DocumentTextIndexTest::buildIndex()
{
    const pdf::PDFDocument document = createDocument(3);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);
    QCOMPARE(index->getPageCount(), pdf::PDFInteger(3));
    QVERIFY2(index->getPageText(1).contains(QStringLiteral("Page 2")), qPrintable(index->getPageText(1)));
    QVERIFY(index->getPageText(-1).isEmpty());
    QVERIFY(index->getPageText(3).isEmpty());
    QVERIFY(index->getTextLayout(5).getTextBlocks().empty());
    QVERIFY(!index->getTextLayout(0).getTextBlocks().empty());
    QVERIFY(index->getMemoryConsumptionEstimate() > 0);

    QVERIFY(!pdf::PDFDocumentTextIndex::build(nullptr, nullptr, nullptr, nullptr));

    // A document without pages
    const pdf::PDFDocument empty = createDocument(QStringList());
    std::shared_ptr<const pdf::PDFDocumentTextIndex> emptyIndex = pdf::PDFDocumentTextIndex::build(&empty, nullptr, nullptr, nullptr);
    QVERIFY(emptyIndex);
    QCOMPARE(emptyIndex->getPageCount(), pdf::PDFInteger(0));
    QVERIFY(pdf::PDFDocumentTextSearch::find(*emptyIndex, QStringLiteral("a"), pdf::PDFDocumentTextSearchOptions(), 0, nullptr).results.empty());
}

void DocumentTextIndexTest::buildProgress()
{
    const pdf::PDFDocument document = createDocument(40);
    std::atomic<pdf::PDFInteger> processed = 0;
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr, &processed);
    QVERIFY(index);
    QCOMPARE(processed.load(), pdf::PDFInteger(40));

    // The progress of the core (started, a step per page, finished)
    pdf::PDFProgress progress(nullptr);
    QSignalSpy startedSpy(&progress, &pdf::PDFProgress::progressStarted);
    QSignalSpy finishedSpy(&progress, &pdf::PDFProgress::progressFinished);
    QVERIFY(pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, &progress));
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    // A canceled build counts the pages too (each page is skipped quickly)
    CanceledOperation canceled;
    processed = 0;
    QVERIFY(!pdf::PDFDocumentTextIndex::build(&document, nullptr, &canceled, nullptr, &processed));
    QCOMPARE(processed.load(), pdf::PDFInteger(40));
}

void DocumentTextIndexTest::search()
{
    const pdf::PDFDocument document = createDocument(4);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alpha"), options, 0, nullptr);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.results.size(), size_t(8));     // "Alpha" and "alphabet" on 4 pages
    QVERIFY(!result.results.front().context.isEmpty());
    QVERIFY(!result.results.front().textSelectionItems.empty());
    QVERIFY(!result.isTruncated);
    QVERIFY(!result.isCanceled);

    options.caseSensitive = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alpha"), options, 0, nullptr).results.size(), size_t(4));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("Alpha"), options, 0, nullptr).results.size(), size_t(4));

    options.caseSensitive = false;
    options.wholeWords = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alpha"), options, 0, nullptr).results.size(), size_t(4));

    options.wholeWords = false;
    options.regularExpression = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("Page [23]"), options, 0, nullptr).results.size(), size_t(2));

    result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("(unclosed"), options, 0, nullptr);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.results.empty());

    // Whole words and a regular expression together
    options.wholeWords = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alph\\w*"), options, 0, nullptr).results.size(), size_t(8));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alph"), options, 0, nullptr).results.size(), size_t(0));

    // A text, which is not there
    QVERIFY(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("omega"), pdf::PDFDocumentTextSearchOptions(), 0, nullptr).results.empty());
}

void DocumentTextIndexTest::searchWildcards()
{
    const pdf::PDFDocument document = createDocument(2);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    options.wildcards = true;

    // * = characters of a word: "alpha" and "alphabet"
    pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("alph*"), options, 0, nullptr);
    QCOMPARE(result.results.size(), size_t(4));
    QCOMPARE(result.results[1].matched, QStringLiteral("alphabet"));

    // ? = one character
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("g?mma"), options, 0, nullptr).results.size(), size_t(2));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("g?ma"), options, 0, nullptr).results.size(), size_t(0));

    // The other characters are literal (a regular expression is not interpreted)
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("[a-z]+"), options, 0, nullptr).results.size(), size_t(0));
    QVERIFY(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("(("), options, 0, nullptr).error.isEmpty());

    // * does not cross a word
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("beta*gamma"), options, 0, nullptr).results.size(), size_t(0));

    // Regular expression wins over wildcards
    options.regularExpression = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("g.mma"), options, 0, nullptr).results.size(), size_t(2));
}

void DocumentTextIndexTest::searchWholeWordsPunctuation()
{
    const pdf::PDFDocument document = createDocument(QStringList({ QStringLiteral("Total: (net) price, x-ray"), QStringLiteral("prices are xray") }));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    options.wholeWords = true;

    // A query, which starts and ends with a punctuation, is a whole "word" too
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("(net)"), options, 0, nullptr).results.size(), size_t(1));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("price"), options, 0, nullptr).results.size(), size_t(1));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("ray"), options, 0, nullptr).results.size(), size_t(1));
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("Total:"), options, 0, nullptr).results.size(), size_t(1));
}

void DocumentTextIndexTest::searchContext()
{
    const pdf::PDFDocument document = createDocument(QStringList({ QStringLiteral("  The revenue grew by 18 %\nbefore the revenue of regions") }));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("REVENUE"), pdf::PDFDocumentTextSearchOptions(), 0, nullptr);
    QCOMPARE(result.results.size(), size_t(2));
    for (const pdf::PDFDocumentTextSearchMatch& match : result.results)
    {
        QCOMPARE(match.pageIndex, pdf::PDFInteger(0));
        QCOMPARE(match.matched.toLower(), QStringLiteral("revenue"));
        QVERIFY2(match.contextMatchStart >= 0, qPrintable(match.context));
        QCOMPARE(match.contextMatchLength, qsizetype(7));
        QCOMPARE(match.context.mid(match.contextMatchStart, match.contextMatchLength), match.matched);
        QVERIFY(!match.context.startsWith(QLatin1Char(' ')));
    }
}

void DocumentTextIndexTest::searchGeometry()
{
    const pdf::PDFDocument document = createDocument(QStringList({ QStringLiteral("left word\nmiddle word"), QStringLiteral("other page word") }));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("word"), pdf::PDFDocumentTextSearchOptions(), 0, nullptr);
    QCOMPARE(result.results.size(), size_t(3));

    const QRectF page(0, 0, 300, 400);
    for (const pdf::PDFDocumentTextSearchMatch& match : result.results)
    {
        // One piece of four points, inside of the page, of a sane size
        QCOMPARE(match.quadrilaterals.size(), qsizetype(4));
        QVERIFY(match.boundingRect.isValid());
        QVERIFY(page.contains(match.boundingRect));
        QVERIFY(match.boundingRect.width() > 5 && match.boundingRect.width() < 100);
        QVERIFY(match.boundingRect.height() > 5 && match.boundingRect.height() < 40);
    }

    // The two occurrences of the first page are on different lines
    QVERIFY(qAbs(result.results[0].boundingRect.center().y() - result.results[1].boundingRect.center().y()) > 10);
    QCOMPARE(result.results[2].pageIndex, pdf::PDFInteger(1));
}

void DocumentTextIndexTest::searchOrderAcrossChunks()
{
    // More pages than one chunk: the results come in the order of the pages
    const int pageCount = qMax(16, QThread::idealThreadCount() * 4) * 2 + 3;
    const pdf::PDFDocument document = createDocument(pageCount);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("gamma"), pdf::PDFDocumentTextSearchOptions(), 0, nullptr);
    QCOMPARE(result.results.size(), size_t(pageCount));
    for (int i = 0; i < pageCount; ++i)
    {
        QCOMPARE(result.results[size_t(i)].pageIndex, pdf::PDFInteger(i));
    }
}

void DocumentTextIndexTest::searchLimits()
{
    const pdf::PDFDocument document = createDocument(5);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const pdf::PDFDocumentTextSearchOptions options;
    pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("gamma"), options, 3, nullptr);
    QCOMPARE(result.results.size(), size_t(3));
    QVERIFY(result.isTruncated);

    // Exactly the limit is not truncated
    result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("gamma"), options, 5, nullptr);
    QCOMPARE(result.results.size(), size_t(5));
    QVERIFY(!result.isTruncated);

    result = pdf::PDFDocumentTextSearch::find(*index, QString(pdf::PDFDocumentTextSearch::MAXIMUM_QUERY_LENGTH + 1, QLatin1Char('a')), options, 0, nullptr);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(pdf::PDFDocumentTextSearch::find(*index, QString(pdf::PDFDocumentTextSearch::MAXIMUM_QUERY_LENGTH, QLatin1Char('a')), options, 0, nullptr).error.isEmpty());

    QVERIFY(pdf::PDFDocumentTextSearch::find(*index, QString(), options, 0, nullptr).results.empty());
}

void DocumentTextIndexTest::searchLimitAcrossChunks()
{
    // Many occurrences on many pages: the limit is reached in a later chunk and
    // a page with more occurrences than the limit is cut too
    const int pageCount = qMax(16, QThread::idealThreadCount() * 4) + 5;
    const pdf::PDFDocument document = createDocument(pageCount);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;
    const pdf::PDFDocumentTextSearchResult all = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("a"), options, 1000000, nullptr);
    QVERIFY(!all.isTruncated);
    const size_t count = all.results.size();
    QVERIFY(count > size_t(pageCount) * 5);

    const size_t limit = count - 7;
    const pdf::PDFDocumentTextSearchResult limited = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("a"), options, limit, nullptr);
    QVERIFY(limited.isTruncated);
    QCOMPARE(limited.results.size(), limit);
    for (size_t i = 0; i < limit; ++i)
    {
        QCOMPARE(limited.results[i].pageIndex, all.results[i].pageIndex);
        QCOMPARE(limited.results[i].boundingRect, all.results[i].boundingRect);
    }

    const pdf::PDFDocumentTextSearchResult one = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("a"), options, 1, nullptr);
    QCOMPARE(one.results.size(), size_t(1));
    QVERIFY(one.isTruncated);
}

void DocumentTextIndexTest::searchEmptyMatches()
{
    const pdf::PDFDocument document = createDocument(2);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;

    // Expressions, which match the empty text, give no empty results (they used to
    // reach out of the text of the flow)
    for (const QString& pattern : { QStringLiteral("x*"), QStringLiteral("(?=a)"), QStringLiteral("^"), QStringLiteral("$"), QStringLiteral("\\b"), QStringLiteral("a?") })
    {
        const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, pattern, options, 0, nullptr);
        QVERIFY2(result.error.isEmpty(), qPrintable(pattern));
        for (const pdf::PDFDocumentTextSearchMatch& match : result.results)
        {
            QVERIFY2(!match.matched.isEmpty(), qPrintable(pattern));
            QVERIFY(!match.textSelectionItems.empty());
        }
    }

    // a? matches every "a" (the empty matches between them are skipped)
    QCOMPARE(pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("a?"), options, 0, nullptr).results.size(),
             pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("a"), options, 0, nullptr).results.size());
}

void DocumentTextIndexTest::searchHostileQueries()
{
    const pdf::PDFDocument document = createDocument(2);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    // Broken UTF-16, control characters and an invalid expression never crash
    QString brokenText;
    brokenText += QChar(0xD800);
    brokenText += QStringLiteral("alpha");
    brokenText += QChar(0xDC00);
    brokenText += QChar(0x0000);
    brokenText += QChar(0x001B);

    for (bool regularExpression : { false, true })
    {
        for (bool wildcards : { false, true })
        {
            for (bool wholeWords : { false, true })
            {
                pdf::PDFDocumentTextSearchOptions options;
                options.regularExpression = regularExpression;
                options.wildcards = wildcards;
                options.wholeWords = wholeWords;

                for (const QString& query : { brokenText, QStringLiteral("(?<=a+)b"), QStringLiteral("\\"), QStringLiteral("[z-a]"), QStringLiteral("(?R)"), QStringLiteral("*?+") })
                {
                    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, query, options, 0, nullptr);
                    QVERIFY(result.results.size() < 100);
                }
            }
        }
    }

    // Random texts
    QRandomGenerator random(20261022);
    for (int i = 0; i < 200; ++i)
    {
        QString query;
        const int length = random.bounded(1, 20);
        for (int j = 0; j < length; ++j)
        {
            query += QChar(char16_t(random.bounded(1, 0x3000)));
        }

        pdf::PDFDocumentTextSearchOptions options;
        options.regularExpression = random.bounded(2) == 1;
        options.wildcards = random.bounded(2) == 1;
        options.wholeWords = random.bounded(2) == 1;
        options.caseSensitive = random.bounded(2) == 1;
        const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, query, options, 0, nullptr);
        Q_UNUSED(result);
    }
}

void DocumentTextIndexTest::searchCatastrophicBacktracking()
{
    // An expression with nested quantifiers over a long run of one character: the
    // match limit of PCRE2 ends every attempt, the search finishes
    const QString run = QString(60, QLatin1Char('a')) + QLatin1Char('!');
    const pdf::PDFDocument document = createDocument(QStringList({ run, run }));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("(a+)+$"), options, 0, nullptr);
    QVERIFY(result.error.isEmpty());
    QVERIFY2(timer.elapsed() < 60000, qPrintable(QString::number(timer.elapsed())));
    qInfo("Catastrophic expression: %lld ms, %zu results", timer.elapsed(), result.results.size());
}

void DocumentTextIndexTest::cancel()
{
    const pdf::PDFDocument document = createDocument(3);
    CanceledOperation canceled;
    QVERIFY(!pdf::PDFDocumentTextIndex::build(&document, nullptr, &canceled, nullptr));

    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);
    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("beta"), pdf::PDFDocumentTextSearchOptions(), 0, &canceled);
    QVERIFY(result.isCanceled);
    QVERIFY(result.results.empty());
}

void DocumentTextIndexTest::cancelDuringSearch()
{
    const int pageCount = qMax(16, QThread::idealThreadCount() * 4) * 3;
    const pdf::PDFDocument document = createDocument(pageCount);
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    // Canceled after a few pages: no partial results are given
    CountdownOperation countdown(5);
    const pdf::PDFDocumentTextSearchResult result = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("beta"), pdf::PDFDocumentTextSearchOptions(), 0, &countdown);
    QVERIFY(result.isCanceled);
    QVERIFY(!result.isTruncated);
    QVERIFY(result.results.empty());
}

void DocumentTextIndexTest::createPattern()
{
    pdf::PDFDocumentTextSearchOptions options;
    QVERIFY(pdf::PDFDocumentTextSearch::createPattern(QStringLiteral("a.b"), options).isEmpty());

    options.wholeWords = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::createPattern(QStringLiteral("a.b"), options), QStringLiteral("(?<!\\w)(?:a\\.b)(?!\\w)"));

    options.wholeWords = false;
    options.wildcards = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::createPattern(QStringLiteral("a*b?."), options), QStringLiteral("a\\w*b\\w\\."));

    options.regularExpression = true;
    QCOMPARE(pdf::PDFDocumentTextSearch::createPattern(QStringLiteral("a*b?."), options), QStringLiteral("a*b?."));
}

void DocumentTextIndexTest::flowFindEmptyMatch()
{
    // PDFTextFlow::find of an empty match used to read out of the characters of the
    // flow (the old applications share it)
    const pdf::PDFDocument document = createDocument(QStringList({ QStringLiteral("abc") }));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const pdf::PDFTextFlows flows = pdf::PDFTextFlow::createTextFlows(index->getTextLayout(0), pdf::PDFTextFlow::SeparateBlocks, 0);
    QVERIFY(!flows.empty());
    QVERIFY(flows.front().find(QRegularExpression(QStringLiteral("x*"))).empty());
    QVERIFY(flows.front().find(QRegularExpression(QStringLiteral("$"))).empty());
    QCOMPARE(flows.front().find(QRegularExpression(QStringLiteral("b"))).size(), size_t(1));

    const pdf::PDFFindResults results = flows.front().find(QStringLiteral("bc"), Qt::CaseSensitive);
    QCOMPARE(results.size(), size_t(1));
    QCOMPARE(results.front().context.mid(results.front().contextMatchStart, results.front().contextMatchLength), QStringLiteral("bc"));
}

void DocumentTextIndexTest::aatlParameter()
{
    // The default keeps the behavior of the old applications (AATL with the
    // system store); the new application trusts the AATL list always
    const pdf::PDFSignatureHandler::Parameters parameters;
    QCOMPARE(parameters.aatlCertificates, pdf::PDFSignatureHandler::AATLCertificates::WithSystemStore);
    QVERIFY(parameters.useSystemCertificateStore);
}

void DocumentTextIndexTest::benchmark2000Pages()
{
    // The large document of D11: the build in parallel and searches; the numbers are
    // reported, the limits are generous (valgrind, slow machines)
    QElapsedTimer timer;
    timer.start();
    const pdf::PDFDocument document = createDocument(2000);
    const qint64 createTime = timer.restart();

    std::atomic<pdf::PDFInteger> processed = 0;
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = pdf::PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr, &processed);
    const qint64 buildTime = timer.restart();
    QVERIFY(index);
    QCOMPARE(index->getPageCount(), pdf::PDFInteger(2000));
    QCOMPARE(processed.load(), pdf::PDFInteger(2000));

    const pdf::PDFDocumentTextSearchResult text = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("gamma"), pdf::PDFDocumentTextSearchOptions(), 100000, nullptr);
    const qint64 textTime = timer.restart();
    QCOMPARE(text.results.size(), size_t(2000));

    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;
    const pdf::PDFDocumentTextSearchResult expression = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("Page 1\\d\\d\\b"), options, 100000, nullptr);
    const qint64 expressionTime = timer.restart();
    QCOMPARE(expression.results.size(), size_t(100));

    // Every character (about 60 000), cut at 50 000 results
    const pdf::PDFDocumentTextSearchResult many = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("."), options, 50000, nullptr);
    const qint64 manyTime = timer.restart();
    QVERIFY(many.isTruncated);
    QCOMPARE(many.results.size(), size_t(50000));

    qInfo("2000 pages: document %lld ms, index %lld ms (%lld kB), text %lld ms, expression %lld ms, 50 000 results %lld ms",
          createTime, buildTime, index->getMemoryConsumptionEstimate() / 1024, textTime, expressionTime, manyTime);
    QVERIFY(buildTime < 600000);
}

void DocumentTextIndexTest::pageLabels()
{
    pdf::PDFArrayBuilder nums;
    nums.appendItem(pdf::PDFObject::createInteger(0));
    pdf::PDFDictionaryBuilder roman;
    roman.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName(QByteArray("r")));
    nums.appendItem(pdf::PDFObject::createDictionary(std::move(roman)));
    nums.appendItem(pdf::PDFObject::createInteger(3));
    pdf::PDFDictionaryBuilder decimal;
    decimal.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName(QByteArray("D")));
    decimal.setEntry(pdf::PDFInplaceOrMemoryString("P"), pdf::PDFObject::createString(QByteArray("A-")));
    decimal.setEntry(pdf::PDFInplaceOrMemoryString("St"), pdf::PDFObject::createInteger(5));
    nums.appendItem(pdf::PDFObject::createDictionary(std::move(decimal)));

    const pdf::PDFDocument document = createLabeledDocument(pdf::PDFObject::createArray(std::move(nums)), 6);
    const pdf::PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageLabels().size(), size_t(2));
    QCOMPARE(catalog->getPageLabel(0), QStringLiteral("i"));
    QCOMPARE(catalog->getPageLabel(2), QStringLiteral("iii"));
    QCOMPARE(catalog->getPageLabel(3), QStringLiteral("A-5"));
    QCOMPARE(catalog->getPageLabel(5), QStringLiteral("A-7"));
    QVERIFY(catalog->getPageLabel(6).isEmpty());
    QVERIFY(catalog->getPageLabel(-1).isEmpty());
    QCOMPARE(catalog->findPageIndexByLabel(QStringLiteral("A-6")), std::optional<pdf::PDFInteger>(4));
    QVERIFY(!catalog->findPageIndexByLabel(QStringLiteral("A-8")));
    QVERIFY(!catalog->findPageIndexByLabel(QString()));

    // A document without labels
    const pdf::PDFDocument plain = createDocument(1);
    QVERIFY(plain.getCatalog()->getPageLabel(0).isEmpty());
    QVERIFY(!plain.getCatalog()->findPageIndexByLabel(QStringLiteral("1")));
}

void DocumentTextIndexTest::pageLabelsHostile()
{
    // Huge start numbers (roman numerals and letters fall back to decimal numbers),
    // an overflowing start number and a very long prefix
    pdf::PDFArrayBuilder nums;
    auto addLabel = [&nums](int pageIndex, const char* style, qint64 start, const QByteArray& prefix)
    {
        nums.appendItem(pdf::PDFObject::createInteger(pageIndex));
        pdf::PDFDictionaryBuilder label;
        label.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName(QByteArray(style)));
        label.setEntry(pdf::PDFInplaceOrMemoryString("St"), pdf::PDFObject::createInteger(start));
        if (!prefix.isEmpty())
        {
            label.setEntry(pdf::PDFInplaceOrMemoryString("P"), pdf::PDFObject::createString(prefix));
        }
        nums.appendItem(pdf::PDFObject::createDictionary(std::move(label)));
    };
    addLabel(0, "R", 1000000000000LL, QByteArray());
    addLabel(1, "a", 1000000000000LL, QByteArray());
    addLabel(2, "D", std::numeric_limits<qint64>::max(), QByteArray());
    addLabel(4, "r", -5, QByteArray(10000, 'x'));

    const pdf::PDFDocument document = createLabeledDocument(pdf::PDFObject::createArray(std::move(nums)), 6);
    const pdf::PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageLabel(0), QStringLiteral("1000000000000"));
    QCOMPARE(catalog->getPageLabel(1), QStringLiteral("1000000000000"));
    QCOMPARE(catalog->getPageLabel(3), QString::number(std::numeric_limits<qint64>::max()));
    QVERIFY(catalog->getPageLabel(4).size() <= pdf::PDFCatalog::MAXIMUM_PAGE_LABEL_PREFIX_LENGTH + 32);
}

QTEST_MAIN(DocumentTextIndexTest)
#include "tst_documenttextindextest.moc"
