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

// The rasterizer pool of step 4 of phase 2 (plan R4-16, owner answer Q4.7): the budget of the
// bytes of the page images (a page over the budget runs alone), the ordered rendering with its
// reorder window of 2 x the rasterizers, the cancellation, the errors of a page from its image
// and never from the signal renderError. The expectations come from the rules of the plan.

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfparser.h"
#include "pdfrenderer.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

/// Everything the pool needs; the pool keeps a copy of the mesh settings
class PoolContext
{
public:
    explicit PoolContext(pdf::PDFDocument* document, int rasterizerCount) :
        m_optionalContentActivity(document, pdf::OCUsage::Export, nullptr),
        m_cmsManager(nullptr),
        m_fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT)
    {
        m_cmsManager.setDocument(document);
        m_cmsManager.setSettings(pdf::PDFCMSSettings());

        pdf::PDFModifiedDocument modifiedDocument(document, &m_optionalContentActivity);
        m_fontCache.setDocument(modifiedDocument);
        m_iccProfileCache.setDocument(modifiedDocument);
        m_fontCache.setCacheShrinkEnabled(nullptr, false);

        pdf::PDFMeshQualitySettings meshQualitySettings;
        m_pool = std::make_unique<pdf::PDFRasterizerPool>(document, &m_fontCache, &m_iccProfileCache, &m_cmsManager, &m_optionalContentActivity,
                                                          pdf::PDFRenderer::getDefaultFeatures(), meshQualitySettings, rasterizerCount,
                                                          pdf::RendererEngine::QPainter, nullptr);
    }

    ~PoolContext()
    {
        m_pool.reset();
        m_fontCache.setCacheShrinkEnabled(nullptr, true);
    }

    pdf::PDFRasterizerPool* pool() const { return m_pool.get(); }

private:
    pdf::PDFOptionalContentActivity m_optionalContentActivity;
    pdf::PDFCMSManager m_cmsManager;
    pdf::PDFFontCache m_fontCache;
    pdf::PDFICCProfileCache m_iccProfileCache;
    std::unique_ptr<pdf::PDFRasterizerPool> m_pool;
};

/// An operation control, which the test cancels
class TestOperationControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCancelled.load(); }
    void cancel() { m_isCancelled.store(true); }

private:
    std::atomic<bool> m_isCancelled = false;
};

class RasterizerPoolTest : public QObject
{
    Q_OBJECT

private slots:
    void budgetBoundsTheImagesAtOnce();
    void pageOverBudgetRunsAlone();
    void defaultBudgetIsAQuarterOfTheMemory();
    void orderedWritesInOrderWithinTheWindow();
    void cancellationEndsTheWriting();
    void errorsComeFromThePageNotFromTheSignal();
    void missingPageKeepsTheOrder();

private:
    /// A document of plain pages, the content of each page is \p content
    static pdf::PDFDocument createDocument(int pageCount, const QByteArray& content = QByteArray("0 0 1 rg 10 10 50 50 re f"));

    /// The pages 0 .. count - 1
    static std::vector<pdf::PDFInteger> allPages(int count);

    /// Time limits scale by PDF4QT_TEST_TIMEOUT_FACTOR
    static int scaled(int milliseconds);
};

pdf::PDFDocument RasterizerPoolTest::createDocument(int pageCount, const QByteArray& content)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));

        pdf::PDFDictionaryBuilder contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(contentDictionary), QByteArray(content))));

        pdf::PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    }
    return builder.build();
}

std::vector<pdf::PDFInteger> RasterizerPoolTest::allPages(int count)
{
    std::vector<pdf::PDFInteger> pages(size_t(count), 0);
    for (int i = 0; i < count; ++i)
    {
        pages[size_t(i)] = i;
    }
    return pages;
}

int RasterizerPoolTest::scaled(int milliseconds)
{
    const int factor = qBound(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"), 1000);
    return milliseconds * factor;
}

void RasterizerPoolTest::budgetBoundsTheImagesAtOnce()
{
    // Q4.7: the images existing at once never exceed the budget (here two pages of 100 x 100 pixels
    // of 4 bytes); four rasterizers and slow encoders would otherwise hold four images
    pdf::PDFDocument document = createDocument(12);
    PoolContext context(&document, 4);
    const qint64 pageBytes = pdf::PDFRasterizerPool::getImageDataSize(QSize(100, 100));
    QCOMPARE(pageBytes, qint64(100 * 100 * 4));
    context.pool()->setImageDataBudget(2 * pageBytes);

    std::atomic<qint64> current = 0;
    std::atomic<qint64> maximum = 0;
    std::vector<pdf::PDFInteger> written;
    const auto encode = [&](const pdf::PDFRenderedPageImage& image, QString*) -> QByteArray
    {
        const qint64 now = current.fetch_add(pageBytes) + pageBytes;
        qint64 previous = maximum.load();
        while (now > previous && !maximum.compare_exchange_weak(previous, now))
        {
        }
        QThread::msleep(30);
        current.fetch_sub(pageBytes);
        return QByteArray::number(image.pageIndex);
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output) { written.push_back(output.pageIndex); };

    context.pool()->renderOrdered(allPages(12), [](const pdf::PDFPage*) { return QSize(100, 100); }, encode, write, nullptr, nullptr);

    QVERIFY2(maximum.load() <= 2 * pageBytes, qPrintable(QString::number(maximum.load())));
    QCOMPARE(written, allPages(12));
}

void RasterizerPoolTest::defaultBudgetIsAQuarterOfTheMemory()
{
    // Owner 14. 11.: the default budget is a quarter of the physical memory, at least 2 GiB;
    // an unknown size of the memory gives 4 GiB (Q4.7 of step 4)
    constexpr qint64 GiB = qint64(1024) * 1024 * 1024;
    using Pool = pdf::PDFRasterizerPool;
    QCOMPARE(Pool::getDefaultImageDataBudget(0), 4 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(-1), 4 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(4 * GiB), 2 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(8 * GiB), 2 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(16 * GiB), 4 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(32 * GiB), 8 * GiB);
    QCOMPARE(Pool::getDefaultImageDataBudget(64 * GiB), 16 * GiB);

    // This computer: the memory is known on the supported systems
    const qint64 memory = pdf::PDFSysUtils::getPhysicalMemorySize();
    QVERIFY(memory > 0);
    QCOMPARE(Pool::getDefaultImageDataBudget(), Pool::getDefaultImageDataBudget(memory));
}

void RasterizerPoolTest::pageOverBudgetRunsAlone()
{
    // One page larger than the whole budget runs alone: it waits, until no other image exists,
    // and no other page starts while it exists
    pdf::PDFDocument document = createDocument(8);
    PoolContext context(&document, 4);
    context.pool()->setImageDataBudget(1000);

    std::atomic<int> current = 0;
    std::atomic<int> maximum = 0;
    int writtenCount = 0;
    const auto encode = [&](const pdf::PDFRenderedPageImage&, QString*) -> QByteArray
    {
        const int now = ++current;
        int previous = maximum.load();
        while (now > previous && !maximum.compare_exchange_weak(previous, now))
        {
        }
        QThread::msleep(20);
        --current;
        return QByteArray("page");
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output)
    {
        QVERIFY(output.isRendered);
        ++writtenCount;
    };

    context.pool()->renderOrdered(allPages(8), [](const pdf::PDFPage*) { return QSize(100, 100); }, encode, write, nullptr, nullptr);

    QCOMPARE(maximum.load(), 1);
    QCOMPARE(writtenCount, 8);
}

void RasterizerPoolTest::orderedWritesInOrderWithinTheWindow()
{
    // R4-16: the writer gets the pages in the order of the call; while the first page is slow, a
    // page at least 2 x the rasterizers (the window) after it does not start
    constexpr int pageCount = 16;
    pdf::PDFDocument document = createDocument(pageCount);
    PoolContext context(&document, 2);
    const int window = context.pool()->getReorderWindowSize();
    QCOMPARE(window, 4);

    // The pages in a mixed order of the call: the writer follows the call, not the threads
    std::vector<pdf::PDFInteger> pages = allPages(pageCount);
    std::reverse(pages.begin(), pages.end());

    QMutex mutex;
    std::vector<pdf::PDFInteger> written;
    std::vector<pdf::PDFInteger> startedBeforeFirstWrite;
    bool isFirstWritten = false;
    const auto sizeGetter = [&](const pdf::PDFPage* page) -> QSize
    {
        QMutexLocker locker(&mutex);
        if (!isFirstWritten)
        {
            const pdf::PDFInteger pageIndex = pdf::PDFInteger(document.getCatalog()->getPageIndexFromPageReference(page->getPageReference()));
            startedBeforeFirstWrite.push_back(pageIndex);
        }
        return QSize(20, 20);
    };
    const auto encode = [&](const pdf::PDFRenderedPageImage& image, QString*) -> QByteArray
    {
        if (image.pageIndex == pages.front())
        {
            QThread::msleep(scaled(200));
        }
        return QByteArray::number(image.pageIndex);
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output)
    {
        QMutexLocker locker(&mutex);
        QCOMPARE(output.data, QByteArray::number(output.pageIndex));
        written.push_back(output.pageIndex);
        isFirstWritten = true;
    };

    context.pool()->renderOrdered(pages, sizeGetter, encode, write, nullptr, nullptr);

    QCOMPARE(written, pages);

    // Only the pages of the window (the first page and the three after it in the order of the
    // call) could start before the first page was written
    for (const pdf::PDFInteger pageIndex : startedBeforeFirstWrite)
    {
        const auto position = std::distance(pages.cbegin(), std::find(pages.cbegin(), pages.cend(), pageIndex));
        QVERIFY2(position < window, qPrintable(QString("page %1 at the position %2 started before the first page was written").arg(pageIndex).arg(position)));
    }
}

void RasterizerPoolTest::cancellationEndsTheWriting()
{
    // A cancellation: the remaining pages are not written, the threads waiting for the window
    // leave, the call returns
    constexpr int pageCount = 40;
    pdf::PDFDocument document = createDocument(pageCount);
    PoolContext context(&document, 2);
    TestOperationControl control;

    std::vector<pdf::PDFInteger> written;
    bool isWrittenAfterCancel = false;
    const auto encode = [&](const pdf::PDFRenderedPageImage& image, QString*) -> QByteArray
    {
        QThread::msleep(5);
        return QByteArray::number(image.pageIndex);
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output)
    {
        if (control.isOperationCancelled())
        {
            isWrittenAfterCancel = true;
        }
        written.push_back(output.pageIndex);
        if (written.size() == 3)
        {
            control.cancel();
        }
    };

    QElapsedTimer timer;
    timer.start();
    context.pool()->renderOrdered(allPages(pageCount), [](const pdf::PDFPage*) { return QSize(20, 20); }, encode, write, nullptr, &control);

    QVERIFY(timer.elapsed() < scaled(10000));
    QVERIFY(!isWrittenAfterCancel);
    QCOMPARE(written, allPages(3));
}

void RasterizerPoolTest::errorsComeFromThePageNotFromTheSignal()
{
    // R4-16: the problems of a page come with the page (its errors), the signal renderError of the
    // threads is not emitted by an ordered rendering; Information is not a problem to report
    pdf::PDFDocument document = createDocument(2, QByteArray("q /Missing Do Q"));
    PoolContext context(&document, 2);
    QSignalSpy errorSpy(context.pool(), &pdf::PDFRasterizerPool::renderError);

    std::vector<QList<pdf::PDFRenderError>> errors;
    std::vector<bool> isRendered;
    const auto encode = [&](const pdf::PDFRenderedPageImage& image, QString*) -> QByteArray
    {
        return QByteArray::number(image.pageIndex);
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output)
    {
        errors.push_back(output.errors);
        isRendered.push_back(output.isRendered);
    };

    context.pool()->renderOrdered(allPages(2), [](const pdf::PDFPage*) { return QSize(20, 20); }, encode, write, nullptr, nullptr);

    QCOMPARE(errors.size(), size_t(2));
    for (size_t i = 0; i < errors.size(); ++i)
    {
        // The page is rendered without the missing XObject, and says so (8.8: Do of a name, which
        // is not in the resources)
        QVERIFY(isRendered[i]);
        QVERIFY(std::any_of(errors[i].cbegin(), errors[i].cend(), [](const pdf::PDFRenderError& error) { return error.type == pdf::RenderErrorType::Error; }));
    }
    QCOMPARE(errorSpy.count(), 0);

    pdf::PDFRenderedPageImage image;
    image.errors = { pdf::PDFRenderError(pdf::RenderErrorType::Information, QStringLiteral("info")), pdf::PDFRenderError(pdf::RenderErrorType::Warning, QStringLiteral("warning")) };
    const QList<pdf::PDFRenderError> reported = image.getReportedErrors();
    QCOMPARE(reported.size(), 1);
    QCOMPARE(reported.front().type, pdf::RenderErrorType::Warning);
}

void RasterizerPoolTest::missingPageKeepsTheOrder()
{
    // A page, which does not exist, comes to the writer as not rendered with its reason; the pages
    // after it are written in their order
    pdf::PDFDocument document = createDocument(3);
    PoolContext context(&document, 2);

    std::vector<pdf::PDFInteger> written;
    std::vector<bool> isRendered;
    QString missingReason;
    const auto encode = [&](const pdf::PDFRenderedPageImage& image, QString*) -> QByteArray
    {
        return QByteArray::number(image.pageIndex);
    };
    const auto write = [&](pdf::PDFRenderedPageOutput& output)
    {
        written.push_back(output.pageIndex);
        isRendered.push_back(output.isRendered);
        if (!output.isRendered)
        {
            missingReason = output.errorMessage;
            QVERIFY(output.data.isEmpty());
        }
    };

    context.pool()->renderOrdered({ 0, 7, 2 }, [](const pdf::PDFPage*) { return QSize(20, 20); }, encode, write, nullptr, nullptr);

    QCOMPARE(written, std::vector<pdf::PDFInteger>({ 0, 7, 2 }));
    QCOMPARE(isRendered, std::vector<bool>({ true, false, true }));
    QVERIFY(!missingReason.isEmpty());
}

QTEST_MAIN(RasterizerPoolTest)

#include "tst_rasterizerpooltest.moc"
