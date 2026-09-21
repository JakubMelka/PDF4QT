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

#include "pdfocrmodel.h"
#include "pdfocrengine.h"
#include "pdfocrsession.h"
#include "pdfocrproject.h"
#include "pdfocrconfiguration.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextflow.h"
#include "pdftextlayoutgenerator.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfstreamfilters.h"
#include "pdfexception.h"

#ifdef PDF4QT_OCR_TESSERACT
#include "pdftesseractocrengine.h"
#endif

#include <QtTest>
#include <QBuffer>
#include <QPainter>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#endif
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>

#include <random>
#include <stdexcept>

using namespace pdf;

namespace
{

/// Rendering environment for the tests (font cache, color management, optional content)
class RenderingContext
{
public:
    explicit RenderingContext(const PDFDocument* document) :
        m_optionalContentActivity(document, OCUsage::Export, nullptr),
        m_fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT)
    {
        PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &m_optionalContentActivity);
        m_fontCache.setDocument(modifiedDocument);
        m_fontCache.setCacheShrinkEnabled(nullptr, false);
    }

    ~RenderingContext()
    {
        m_fontCache.setCacheShrinkEnabled(nullptr, true);
    }

    PDFOCRPagePreparer createPreparer(const PDFDocument* document)
    {
        return PDFOCRPagePreparer(document, &m_fontCache, &m_cms, &m_optionalContentActivity, m_meshQualitySettings, RendererEngine::QPainter);
    }

    PDFOptionalContentActivity m_optionalContentActivity;
    PDFCMSGeneric m_cms;
    PDFFontCache m_fontCache;
    PDFMeshQualitySettings m_meshQualitySettings;
};

/// Simple HTTP server for the download tests (AT-04)
class TestHttpServer : public QObject
{
public:
    struct Response
    {
        int status = 200;
        QByteArray contentType = "application/octet-stream";
        QByteArray body;
        bool closeWithoutResponse = false;
    };

    explicit TestHttpServer(QObject* parent = nullptr) :
        QObject(parent)
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost, 0));
        connect(&m_server, &QTcpServer::newConnection, this, &TestHttpServer::onNewConnection);
    }

    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString& path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path); }
    void setResponse(const QString& path, Response response) { m_responses[path] = std::move(response); }
    int requestCount(const QString& path) const { return m_requestCounts.value(path, 0); }

private:
    void onNewConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
        {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]()
            {
                m_buffers[socket] += socket->readAll();
                const int headerEnd = m_buffers[socket].indexOf("\r\n\r\n");
                if (headerEnd == -1)
                {
                    return;
                }

                // The buffer belongs to this request only (the socket address can be reused)
                const QByteArray header = m_buffers.take(socket).left(headerEnd);
                const QList<QByteArray> parts = header.split(' ');
                const QString path = parts.size() > 1 ? QString::fromLatin1(parts[1]) : QString();
                ++m_requestCounts[path];

                const Response response = m_responses.value(path, Response{ 404, "text/html", "<html><body>Not found</body></html>", false });
                if (response.closeWithoutResponse)
                {
                    socket->abort();
                    socket->deleteLater();
                    return;
                }

                QByteArray data = "HTTP/1.1 " + QByteArray::number(response.status) + " OK\r\n";
                data += "Content-Type: " + response.contentType + "\r\n";
                data += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
                data += "Connection: close\r\n\r\n";
                data += response.body;
                socket->write(data);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket]()
            {
                m_buffers.remove(socket);
                socket->deleteLater();
            });
        }
    }

    QTcpServer m_server;
    QHash<QString, Response> m_responses;
    QHash<QString, int> m_requestCounts;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

} // anonymous namespace

class OCRTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void pageRangeParsing();
    void confidenceStatistics();
    void editingOperations();
    void lineTextEditing();
    void replaceAllAndCandidates();
    void textLayerRoundTrip();
    void renderPreservation();
    void textLayerRobustness();
    void preprocessingPipeline();
    void sessionRegressions();
    void geometryRoundTrip();
    void renderedGeometryRoundTrip();
    void idempotentApplyAndRemove();
    void pageAnalysisAndPolicy();
    void annotationsAndRedactions();
    void jobCancellation();
    void pageErrorAndRetry();
    void projectRoundTrip();
    void textExport();
    void genericAdapter();
    void invalidEngineOutput();
    void modelManagerBuiltIn();
    void modelDownload();
    void tesseractRecognition();
    void qualityAndPerformanceBenchmark();

private:
    struct PageSpec
    {
        QSizeF size = QSizeF(300, 200);
        QByteArray content;
        bool withHelvetica = false;
        bool withImage = false;
        PageRotation rotation = PageRotation::None;
        QRectF cropBox;
        double userUnit = 1.0;
        QRectF mediaBox;
    };

    static PDFDocument createDocument(const std::vector<PageSpec>& pages);
    static PDFDocument createImageDocument(const QImage& image, QSizeF pageSize);
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
    static QString extractText(const PDFDocument& document, PDFInteger pageIndex);
    static PDFTextLayout extractLayout(const PDFDocument& document, PDFInteger pageIndex);
    static PDFOCRPageResult createSampleResult(PDFInteger pageIndex, const std::vector<std::pair<QString, QRectF>>& words, PDFOCRConfidenceLevel level = PDFOCRConfidenceLevel::Word);
    static PDFOCRWord makeWord(PDFOCRPageResult& result, const QString& text, const QRectF& rect, std::optional<double> confidence);
    static QRect findDarkBoundingBox(const QImage& image);
    static PDFDocumentPointer applyResults(const PDFDocument& document, const std::vector<PDFOCRPageResult>& results, const PDFOCRTextLayerWriter::Options& options, PDFOCRTextLayerWriter::Report* report);

    std::shared_ptr<PDFOCRTestEngineFactory> m_testEngine;
};

void OCRTest::initTestCase()
{
    m_testEngine = std::make_shared<PDFOCRTestEngineFactory>();
    PDFOCREngineRegistry::getInstance()->registerFactory(m_testEngine);

#ifdef PDF4QT_OCR_TESSERACT
    PDFTesseractOCREngineFactory::registerEngine();
#endif
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

PDFDocument OCRTest::createDocument(const std::vector<PageSpec>& pages)
{
    PDFDocumentBuilder builder;

    auto dictionaryObject = [](std::initializer_list<std::pair<const char*, PDFObject>> entries)
    {
        PDFDictionary dictionary;
        for (const auto& [key, value] : entries)
        {
            dictionary.addEntry(PDFInplaceOrMemoryString(key), PDFObject(value));
        }
        return PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(dictionary)));
    };

    for (const PageSpec& spec : pages)
    {
        const QRectF mediaBox = spec.mediaBox.isValid() ? spec.mediaBox : QRectF(QPointF(0, 0), spec.size);
        const PDFObjectReference pageReference = builder.appendPage(mediaBox);

        QByteArray content = spec.content;
        PDFObjectReference imageReference;
        if (spec.withImage)
        {
            // Gray image covering the whole page, drawn before the rest of the content
            QByteArray imageData(32 * 32, '\xC0');
            PDFDictionary imageDictionary;
            imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
            imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
            imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(32));
            imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(32));
            imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
            imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
            imageDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(imageData.size()));
            imageReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(imageDictionary), std::move(imageData))));
            content = QStringLiteral("q %1 0 0 %2 %3 %4 cm /Im1 Do Q ").arg(mediaBox.width()).arg(mediaBox.height()).arg(mediaBox.left()).arg(mediaBox.top()).toLatin1() + content;
        }

        PDFDictionary contentDictionary;
        contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentDictionary), std::move(content))));

        PDFDictionary pageUpdate;
        pageUpdate.addEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentReference));

        PDFDictionary resources;
        if (spec.withHelvetica)
        {
            const PDFObject font = dictionaryObject({ { "Type", PDFObject::createName("Font") },
                                                      { "Subtype", PDFObject::createName("Type1") },
                                                      { "BaseFont", PDFObject::createName("Helvetica") },
                                                      { "Encoding", PDFObject::createName("WinAnsiEncoding") } });
            const PDFObjectReference fontReference = builder.addObject(font);
            resources.addEntry(PDFInplaceOrMemoryString("Font"), dictionaryObject({ { "F1", PDFObject::createReference(fontReference) } }));
        }
        if (spec.withImage)
        {
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), dictionaryObject({ { "Im1", PDFObject::createReference(imageReference) } }));
        }
        pageUpdate.addEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(resources))));

        builder.mergeTo(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageUpdate))));

        if (spec.rotation != PageRotation::None)
        {
            builder.setPageRotation(pageReference, spec.rotation);
        }
        if (spec.cropBox.isValid())
        {
            builder.setPageCropBox(pageReference, spec.cropBox);
        }
        if (!qFuzzyCompare(spec.userUnit, 1.0))
        {
            builder.setPageUserUnit(pageReference, spec.userUnit);
        }
    }

    return builder.build();
}

PDFDocument OCRTest::createImageDocument(const QImage& image, QSizeF pageSize)
{
    PDFDocumentBuilder builder;
    const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    QByteArray imageData;
    for (int y = 0; y < rgb.height(); ++y)
    {
        imageData.append(reinterpret_cast<const char*>(rgb.constScanLine(y)), rgb.width() * 3);
    }
    QByteArray compressed = PDFFlateDecodeFilter::compress(imageData);

    PDFDictionary imageDictionary;
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(rgb.width()));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(rgb.height()));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(compressed.size()));
    const PDFObjectReference imageReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(imageDictionary), std::move(compressed))));

    QByteArray content = QStringLiteral("q %1 0 0 %2 0 0 cm /Im1 Do Q").arg(pageSize.width()).arg(pageSize.height()).toLatin1();
    PDFDictionary contentDictionary;
    contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentDictionary), std::move(content))));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contentReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.beginDictionaryItem("XObject");
    factory.beginDictionary();
    factory.beginDictionaryItem("Im1");
    factory << imageReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(pageReference, factory.takeObject());
    return builder.build();
}

QByteArray OCRTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument OCRTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

QString OCRTest::extractText(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFDocumentTextFlowFactory factory;
    PDFDocumentTextFlow flow = factory.create(&document, { pageIndex }, PDFDocumentTextFlowFactory::Algorithm::Layout);
    return flow.getText();
}

PDFTextLayout OCRTest::extractLayout(const PDFDocument& document, PDFInteger pageIndex)
{
    RenderingContext context(&document);
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, &document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, QTransform(), context.m_meshQualitySettings);
    generator.processContents();
    return generator.createTextLayout();
}

PDFOCRWord OCRTest::makeWord(PDFOCRPageResult& result, const QString& text, const QRectF& rect, std::optional<double> confidence)
{
    PDFOCRWord word;
    word.id = result.allocateId();
    word.originalText = text;
    word.text = text;
    word.quad = PDFOCRQuad::fromRect(rect);
    if (confidence)
    {
        word.confidence = PDFOCRConfidence::fromRaw(*confidence, 0.0, 100.0, PDFOCRConfidenceLevel::Word);
    }
    return word;
}

PDFOCRPageResult OCRTest::createSampleResult(PDFInteger pageIndex, const std::vector<std::pair<QString, QRectF>>& words, PDFOCRConfidenceLevel level)
{
    PDFOCRPageResult result;
    result.pageIndex = pageIndex;
    result.state = PDFOCRPageState::Done;
    result.provenance.engineId = QStringLiteral("test");
    result.provenance.engineVersion = QStringLiteral("1.0");
    result.generation = 1;

    PDFOCRBlock block;
    block.id = result.allocateId();

    PDFOCRLine line;
    line.id = result.allocateId();

    for (const auto& item : words)
    {
        PDFOCRWord word = makeWord(result, item.first, item.second, level == PDFOCRConfidenceLevel::Word ? std::optional<double>(95.0) : std::nullopt);
        if (level == PDFOCRConfidenceLevel::Line)
        {
            word.confidence = PDFOCRConfidence::fromRaw(70.0, 0.0, 100.0, PDFOCRConfidenceLevel::Line);
        }
        line.words.push_back(std::move(word));
    }

    line.updateGeometryFromWords();
    block.lines.push_back(std::move(line));
    block.updateGeometryFromLines();
    result.blocks.push_back(std::move(block));
    return result;
}

QRect OCRTest::findDarkBoundingBox(const QImage& image)
{
    const QImage grayscale = image.convertToFormat(QImage::Format_Grayscale8);
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < grayscale.height(); ++y)
    {
        const uchar* line = grayscale.constScanLine(y);
        for (int x = 0; x < grayscale.width(); ++x)
        {
            if (line[x] < 128)
            {
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
                minY = qMin(minY, y);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX < 0)
    {
        return QRect();
    }

    // Exclusive right/bottom edge (pixel coverage)
    return QRect(QPoint(minX, minY), QPoint(maxX + 1, maxY + 1));
}

PDFDocumentPointer OCRTest::applyResults(const PDFDocument& document,
                                         const std::vector<PDFOCRPageResult>& results,
                                         const PDFOCRTextLayerWriter::Options& options,
                                         PDFOCRTextLayerWriter::Report* report)
{
    PDFDocumentModifier modifier(&document);
    std::vector<PDFOCRTextLayerWriter::PageRequest> requests;
    for (const PDFOCRPageResult& result : results)
    {
        PDFOCRTextLayerWriter::PageRequest request;
        request.pageIndex = result.pageIndex;
        request.result = result;
        request.layerId = QStringLiteral("layer-%1").arg(result.pageIndex);
        requests.push_back(std::move(request));
    }

    PDFOCRTextLayerWriter::Report localReport = PDFOCRTextLayerWriter::apply(modifier.getBuilder(), &document, requests, options);
    if (report)
    {
        *report = localReport;
    }

    if (localReport.error || !localReport.isModified())
    {
        return nullptr;
    }

    modifier.markPageContentsChanged();
    modifier.markReset();
    if (!modifier.finalize())
    {
        return nullptr;
    }
    return modifier.getDocument();
}

// -------------------------------------------------------------------------
// AT-01: page range parsing
// -------------------------------------------------------------------------

void OCRTest::pageRangeParsing()
{
    QString error;
    std::vector<PDFInteger> pages = PDFOCRPageSelection::parseRange(10, QStringLiteral("2-7"), PDFOCRPageSelection::Parity::Odd, &error);
    QCOMPARE(pages, (std::vector<PDFInteger>{ 2, 4, 6 }));   // physical pages 3, 5, 7 (0-based indices)
    QVERIFY(error.isEmpty());

    pages = PDFOCRPageSelection::parseRange(10, QStringLiteral(" 1, 3-5 , 9, 3 "), PDFOCRPageSelection::Parity::All, &error);
    QCOMPARE(pages, (std::vector<PDFInteger>{ 0, 2, 3, 4, 8 }));

    pages = PDFOCRPageSelection::parseRange(10, QStringLiteral("2-7"), PDFOCRPageSelection::Parity::Even, &error);
    QCOMPARE(pages, (std::vector<PDFInteger>{ 1, 3, 5 }));

    const QStringList invalid = { QString(), QStringLiteral("0"), QStringLiteral("-3"), QStringLiteral("3-"), QStringLiteral("abc"), QStringLiteral("7-2"), QStringLiteral("12"), QStringLiteral("1,,2") };
    for (const QString& text : invalid)
    {
        error.clear();
        pages = PDFOCRPageSelection::parseRange(10, text, PDFOCRPageSelection::Parity::All, &error);
        QVERIFY2(pages.empty(), qPrintable(text));
        QVERIFY2(!error.isEmpty(), qPrintable(text));
    }

    // Parity works on the physical numbers even when labels differ (PAGE-03): "1" has index 0
    QCOMPARE(PDFOCRPageSelection::filterParity({ 0, 1, 2, 3 }, PDFOCRPageSelection::Parity::Odd), (std::vector<PDFInteger>{ 0, 2 }));
    QCOMPARE(PDFOCRPageSelection::describe({ 0, 1, 2, 6 }), QStringLiteral("1-3, 7"));
}

// -------------------------------------------------------------------------
// AT-06: confidence
// -------------------------------------------------------------------------

void OCRTest::confidenceStatistics()
{
    PDFOCRPageResult result;
    result.pageIndex = 0;
    result.state = PDFOCRPageState::Done;

    PDFOCRBlock block;
    block.id = result.allocateId();
    PDFOCRLine line;
    line.id = result.allocateId();
    line.words.push_back(makeWord(result, QStringLiteral("zero"), QRectF(0, 0, 10, 10), 0.0));
    line.words.push_back(makeWord(result, QStringLiteral("almost"), QRectF(20, 0, 10, 10), 79.9));
    line.words.push_back(makeWord(result, QStringLiteral("threshold"), QRectF(40, 0, 10, 10), 80.0));
    line.words.push_back(makeWord(result, QStringLiteral("perfect"), QRectF(60, 0, 10, 10), 100.0));
    line.words.push_back(makeWord(result, QStringLiteral("unknown"), QRectF(80, 0, 10, 10), std::nullopt));
    line.words.push_back(makeWord(result, QStringLiteral("nontext"), QRectF(100, 0, 10, 10), -1.0));
    block.lines.push_back(line);
    result.blocks.push_back(block);

    // -1 is not a score (CONF-02)
    QVERIFY(!result.getWords()[5]->confidence.isAvailable());
    QVERIFY(result.getWords()[0]->confidence.isAvailable());
    QCOMPARE(result.getWords()[0]->confidence.normalized.value(), 0.0);

    const PDFOCRConfidenceStatistics statistics = PDFOCRConfidenceStatistics::compute(result, 80.0);
    QCOMPARE(statistics.wordCount, 6);
    QCOMPARE(statistics.scoredWordCount, 4);
    QCOMPARE(statistics.unknownWordCount, 2);
    QCOMPARE(statistics.belowThresholdCount, 2);   // 0 and 79.9
    QVERIFY(statistics.meanScore.has_value());
    QVERIFY(qAbs(statistics.meanScore.value() - (0.0 + 79.9 + 80.0 + 100.0) / 4.0) < 1e-9);
    QCOMPARE(statistics.reviewRequiredCount, 4);   // 0, 79.9 and two unknown
    QCOMPARE(statistics.level, PDFOCRConfidenceLevel::Word);

    // Line level confidence is preserved as line level, not presented as word confidence (CONF-01)
    PDFOCRRecognitionOutput output;
    output.confidenceLevel = PDFOCRConfidenceLevel::Line;
    PDFOCRRawBlock rawBlock;
    rawBlock.rect = QRectF(0, 0, 100, 20);
    PDFOCRRawLine rawLine;
    rawLine.rect = QRectF(0, 0, 100, 20);
    rawLine.rawConfidence = 55.0;
    PDFOCRRawWord rawWord;
    rawWord.text = QStringLiteral("word");
    rawWord.rect = QRectF(0, 0, 50, 20);
    rawWord.rawConfidence = 99.0;   // must be ignored, engine declares line granularity
    rawLine.words.push_back(rawWord);
    rawBlock.lines.push_back(rawLine);
    output.blocks.push_back(rawBlock);

    PDFOCRPageResult converted;
    converted.pageIndex = 0;
    PDFOCRPageGeometry geometry;
    geometry.pageToRaster = QTransform(1, 0, 0, -1, 0, 100);
    PDFOCRPagePreparer::appendOutput(converted, output, geometry, -1, { });
    QCOMPARE(converted.getWords().size(), size_t(1));
    QCOMPARE(converted.getWords()[0]->confidence.level, PDFOCRConfidenceLevel::Line);
    QCOMPARE(converted.getWords()[0]->confidence.normalized.value(), 55.0);
}

// -------------------------------------------------------------------------
// AT-07: editing
// -------------------------------------------------------------------------

void OCRTest::editingOperations()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) }, { QStringLiteral("world"), QRectF(70, 100, 50, 12) } }));

    const PDFOCRPageResult* page = session.getPage(0);
    QVERIFY(page);
    const int helloId = page->getWords()[0]->id;
    const int worldId = page->getWords()[1]->id;

    // High confidence word can be edited; the score is historical (CONF-05)
    QVERIFY(session.setWordText(0, helloId, QStringLiteral("Hallo")));
    const PDFOCRWord* hello = session.getPage(0)->findWord(helloId);
    QCOMPARE(hello->text, QStringLiteral("Hallo"));
    QCOMPARE(hello->originalText, QStringLiteral("Hello"));
    QCOMPARE(hello->reviewState, PDFOCRReviewState::Modified);
    QCOMPARE(hello->confidence.normalized.value(), 95.0);
    QVERIFY(session.isDirty());

    // Geometry change
    const PDFOCRQuad originalQuad = hello->quad;
    QVERIFY(session.setWordQuad(0, helloId, PDFOCRQuad::fromRect(QRectF(12, 100, 48, 12))));
    QCOMPARE(session.getPage(0)->findWord(helloId)->geometryOrigin, PDFOCRGeometryOrigin::Manual);

    // Listeners of the undo/redo signal see the final state of both stacks
    bool redoAvailableInSignal = false;
    connect(&session, &PDFOCRSession::undoRedoChanged, this, [&]() { redoAvailableInSignal = session.canRedo(); });

    // Undo restores both text and geometry
    session.undo();
    QVERIFY(redoAvailableInSignal);
    QCOMPARE(session.getPage(0)->findWord(helloId)->quad, originalQuad);
    session.undo();
    QCOMPARE(session.getPage(0)->findWord(helloId)->text, QStringLiteral("Hello"));
    QCOMPARE(session.getPage(0)->findWord(helloId)->reviewState, PDFOCRReviewState::Unreviewed);
    session.redo();
    QCOMPARE(session.getPage(0)->findWord(helloId)->text, QStringLiteral("Hallo"));

    // Confirmation does not change the score (CONF-05)
    QVERIFY(session.setWordReviewState(0, worldId, PDFOCRReviewState::Confirmed));
    QCOMPARE(session.getPage(0)->findWord(worldId)->confidence.normalized.value(), 95.0);
    QCOMPARE(session.getPage(0)->findWord(worldId)->reviewState, PDFOCRReviewState::Confirmed);

    // Merge
    int mergedId = 0;
    QVERIFY(session.mergeWords(0, helloId, worldId, &mergedId));
    const PDFOCRWord* merged = session.getPage(0)->findWord(mergedId);
    QVERIFY(merged);
    QCOMPARE(merged->text, QStringLiteral("Halloworld"));
    QCOMPARE(merged->predecessorIds, (std::vector<int>{ helloId, worldId }));
    QVERIFY(merged->quad.isValid());
    QVERIFY(qAbs(merged->quad.width() - 110.0) < 1e-6);
    QCOMPARE(session.getPage(0)->getWordCount(), 1);

    // Split
    int rightId = 0;
    QVERIFY(session.splitWord(0, mergedId, 5, &rightId));
    QCOMPARE(session.getPage(0)->getWordCount(), 2);
    const std::vector<const PDFOCRWord*> words = session.getPage(0)->getWords();
    QCOMPARE(words[0]->text, QStringLiteral("Hallo"));
    QCOMPARE(words[1]->text, QStringLiteral("world"));
    QVERIFY(qAbs(words[0]->quad.width() + words[1]->quad.width() - 110.0) < 1e-6);
    QCOMPARE(words[1]->geometryOrigin, PDFOCRGeometryOrigin::Estimated);
    QCOMPARE(words[1]->predecessorIds, (std::vector<int>{ mergedId }));

    // Insert a missing line on a page (EDIT-02, REGION-03)
    int lineId = 0;
    QVERIFY(session.insertLine(0, -1, QStringLiteral("added line here"), PDFOCRQuad::fromRect(QRectF(10, 50, 200, 12)), &lineId));
    const PDFOCRLine* line = session.getPage(0)->findLine(lineId);
    QVERIFY(line);
    QCOMPARE(line->words.size(), size_t(3));
    QCOMPARE(line->words[0].textOrigin, PDFOCRTextOrigin::Manual);
    QVERIFY(!line->words[0].confidence.isAvailable());

    // Discard (not text)
    QVERIFY(session.setWordReviewState(0, rightId, PDFOCRReviewState::Discarded));
    QCOMPARE(session.getPage(0)->getWordCount(), 4);

    // Every operation is one undo step
    QVERIFY(session.canUndo());
    const size_t wordCountBefore = session.getPage(0)->getWords().size();
    session.undo();
    QCOMPARE(session.getPage(0)->getWordCount(), 5);
    session.undo();
    QCOMPARE(session.getPage(0)->getWords().size(), wordCountBefore - 3);
    QVERIFY(session.canRedo());
}

void OCRTest::lineTextEditing()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("The"), QRectF(10, 100, 30, 12) },
                                                  { QStringLiteral("quick"), QRectF(45, 100, 50, 12) },
                                                  { QStringLiteral("fox"), QRectF(100, 100, 30, 12) } }));

    const int lineId = session.getPage(0)->blocks[0].lines[0].id;
    const int theId = session.getPage(0)->getWords()[0]->id;
    const int foxId = session.getPage(0)->getWords()[2]->id;

    // Middle word is replaced by two words: unchanged words keep identity and geometry (EDIT-03)
    QVERIFY(session.setLineText(0, lineId, QStringLiteral("The quick brown fox")));
    const std::vector<const PDFOCRWord*> words = session.getPage(0)->getWords();
    QCOMPARE(words.size(), size_t(4));
    QCOMPARE(words[0]->id, theId);
    QCOMPARE(words[3]->id, foxId);
    QCOMPARE(words[1]->text, QStringLiteral("quick"));
    QCOMPARE(words[2]->text, QStringLiteral("brown"));
    QCOMPARE(words[2]->geometryOrigin, PDFOCRGeometryOrigin::Estimated);
    QVERIFY(words[2]->quad.isValid());
    QVERIFY(words[1]->quad.points[0].x() >= 40.0);
    QVERIFY(words[2]->quad.points[1].x() <= 100.5);
    QCOMPARE(words[0]->reviewState, PDFOCRReviewState::Unreviewed);

    // Removing a word by editing the line
    QVERIFY(session.setLineText(0, lineId, QStringLiteral("The fox")));
    QCOMPARE(session.getPage(0)->getWordCount(), 2);

    session.undo();
    QCOMPARE(session.getPage(0)->getWordCount(), 4);
}

// -------------------------------------------------------------------------
// AT-08: replace all and candidates
// -------------------------------------------------------------------------

void OCRTest::replaceAllAndCandidates()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("Colour"), QRectF(10, 100, 50, 12) }, { QStringLiteral("colour"), QRectF(70, 100, 50, 12) } }));
    session.setPageResult(createSampleResult(1, { { QStringLiteral("colours"), QRectF(10, 100, 50, 12) } }));

    PDFOCRSession::FindOptions options;
    options.caseSensitive = false;
    const std::vector<PDFOCRSession::FindHit> hits = session.find({ 0, 1 }, QStringLiteral("colour"), options);
    QCOMPARE(hits.size(), size_t(3));

    const int replaced = session.replaceAll({ 0, 1 }, QStringLiteral("colour"), QStringLiteral("color"), options);
    QCOMPARE(replaced, 3);
    QCOMPARE(session.getPage(0)->getWords()[0]->text, QStringLiteral("color"));
    QCOMPARE(session.getPage(1)->getWords()[0]->text, QStringLiteral("colors"));

    // Single undo step restores both pages
    session.undo();
    QCOMPARE(session.getPage(0)->getWords()[0]->text, QStringLiteral("Colour"));
    QCOMPARE(session.getPage(1)->getWords()[0]->text, QStringLiteral("colours"));
    QVERIFY(!session.canUndo());

    // Whole words only
    options.wholeWords = true;
    QCOMPARE(session.find({ 0, 1 }, QStringLiteral("colour"), options).size(), size_t(2));

    // Manual correction and a repeated recognition candidate (EDIT-07)
    const int wordId = session.getPage(0)->getWords()[0]->id;
    QVERIFY(session.setWordText(0, wordId, QStringLiteral("Corrected")));
    PDFOCRPageResult candidate = createSampleResult(0, { { QStringLiteral("Candidate"), QRectF(10, 100, 50, 12) } });
    candidate.generation = 2;

    QVERIFY(session.hasManualCorrections(0));
    QVERIFY(!session.applyCandidate(0, candidate, PDFOCRSession::CandidateMode::Keep, -1));
    QCOMPARE(session.getPage(0)->findWord(wordId)->text, QStringLiteral("Corrected"));

    QVERIFY(session.applyCandidate(0, candidate, PDFOCRSession::CandidateMode::Replace, -1));
    QCOMPARE(session.getPage(0)->getWords()[0]->text, QStringLiteral("Candidate"));
    session.undo();
    QCOMPARE(session.getPage(0)->findWord(wordId)->text, QStringLiteral("Corrected"));
}

// -------------------------------------------------------------------------
// AT-09: invisible text layer
// -------------------------------------------------------------------------

void OCRTest::textLayerRoundTrip()
{
    PageSpec spec;
    spec.size = QSizeF(400, 300);
    spec.content = "0 0 0 rg 20 200 100 30 re f";
    PDFDocument document = createDocument({ spec });

    // Words with diacritics, a combining character and a character outside of BMP (EDIT-08, PDF-07)
    const QString word1 = QStringLiteral("Příliš");
    const QString word2 = QStringLiteral("žluťoučký");
    const QString word3 = QStringLiteral("kůň");
    const QString word4 = QString::fromUtf8("\xF0\x9D\x94\x98nicode");   // U+1D518 + "nicode"
    const QString word5 = QStringLiteral("été");              // combining acute

    PDFOCRPageResult result = createSampleResult(0, { { word1, QRectF(20, 200, 60, 30) },
                                                      { word2, QRectF(90, 200, 120, 30) },
                                                      { word3, QRectF(220, 200, 50, 30) },
                                                      { word4, QRectF(20, 150, 100, 30) },
                                                      { word5, QRectF(130, 150, 60, 30) } });

    PDFOCRTextLayerWriter::Options options;
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer modified = applyResults(document, { result }, options, &report);
    QVERIFY2(modified, qPrintable(report.error.message));
    QCOMPARE(report.writtenPages, (std::vector<PDFInteger>{ 0 }));
    QCOMPARE(report.writtenWords, 5);

    // Round trip through the writer and the reader
    const QByteArray data = write(*modified);
    PDFDocument reopened = read(data);
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(1));

    // Own layer is present and bound to the content (PDF-09)
    PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(&reopened, 0);
    QVERIFY(info.isPresent);
    QVERIFY(info.fingerprintMatches);
    QCOMPARE(info.wordCount, 5);
    QCOMPARE(info.layerId, QStringLiteral("layer-0"));

    // Content stream uses text rendering mode 3 (PDF-04)
    const PDFObject& contentObject = reopened.getObjectByReference(info.contentReference);
    QVERIFY(contentObject.isStream());
    const QByteArray content = reopened.getDecodedStream(contentObject.getStream());
    QVERIFY(content.contains("3 Tr"));
    QVERIFY(content.contains(" Tz"));
    QVERIFY(!content.contains("re\nf"));

    // Text extraction (PDF-07): exact unicode text, no NUL characters, no duplicated spaces
    const QString text = extractText(reopened, 0);
    QVERIFY2(text.contains(word1), qPrintable(text));
    QVERIFY2(text.contains(word2), qPrintable(text));
    QVERIFY2(text.contains(word3), qPrintable(text));
    QVERIFY2(text.contains(word4), qPrintable(text));
    QVERIFY2(text.contains(word5), qPrintable(text));
    QVERIFY(!text.contains(QChar(0)));
    QVERIFY(!text.contains(QStringLiteral("  ")));

    // Words are found at the correct place (PDF-08): the character positions of
    // the first word lie inside its rectangle
    PDFTextLayout layout = extractLayout(reopened, 0);
    QVERIFY(!layout.getTextBlocks().empty());
    bool found = false;
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            QString lineText;
            for (const TextCharacter& character : line.getCharacters())
            {
                lineText += character.character;
            }
            if (lineText.contains(word1))
            {
                const TextCharacter& first = line.getCharacters().front();
                QVERIFY(QRectF(18, 198, 64, 34).contains(first.position));
                found = true;
            }
        }
    }
    QVERIFY(found);

    // Reading the layer back (PDF-10) without review data: text and geometry, unknown confidence
    std::optional<PDFOCRPageResult> restored = PDFOCRTextLayerWriter::readLayer(&reopened, 0);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->getWordCount(), 5);
    QCOMPARE(restored->getWords()[0]->text, word1);
    QVERIFY(!restored->getWords()[0]->confidence.isAvailable());
    QCOMPARE(restored->getWords()[0]->textOrigin, PDFOCRTextOrigin::Imported);
    QVERIFY(qAbs(restored->getWords()[1]->quad.width() - 120.0) < 1e-3);

    // With review data (PDF-10 option), the original scores are restored
    options.keepReviewData = true;
    result.getWords()[0]->text = QStringLiteral("Corrected");
    result.getWords()[0]->reviewState = PDFOCRReviewState::Modified;
    PDFDocumentPointer modifiedWithReview = applyResults(document, { result }, options, &report);
    QVERIFY(modifiedWithReview);
    PDFDocument reopenedWithReview = read(write(*modifiedWithReview));
    restored = PDFOCRTextLayerWriter::readLayer(&reopenedWithReview, 0, &info);
    QVERIFY(restored.has_value());
    QVERIFY(info.hasReviewData);
    QCOMPARE(restored->getWords()[0]->text, QStringLiteral("Corrected"));
    QCOMPARE(restored->getWords()[0]->originalText, word1);
    QCOMPARE(restored->getWords()[0]->confidence.normalized.value(), 95.0);
    QCOMPARE(restored->getWords()[0]->reviewState, PDFOCRReviewState::Modified);

    // Corrected text is the only searchable copy (AT-18)
    const QString correctedText = extractText(reopenedWithReview, 0);
    QVERIFY(correctedText.contains(QStringLiteral("Corrected")));
    QVERIFY(!correctedText.contains(word1));
}

// -------------------------------------------------------------------------
// AT-10: original appearance is preserved
// -------------------------------------------------------------------------

void OCRTest::renderPreservation()
{
    PageSpec spec;
    spec.size = QSizeF(200, 100);
    spec.content = "0 0 1 rg 10 10 80 40 re f 1 0 0 RG 4 w 100 20 m 180 80 l S";
    PDFDocument document = createDocument({ spec });

    // Image XObject data must not be recompressed: add an image to the page resources
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference pageReference = builder.getPages().front();
    QByteArray imageData(16 * 16 * 3, '\x40');
    PDFDictionary imageDictionary;
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(16));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(16));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    imageDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(imageData.size()));
    const PDFObjectReference imageReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(imageDictionary), QByteArray(imageData))));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.beginDictionaryItem("XObject");
    factory.beginDictionary();
    factory.beginDictionaryItem("Im1");
    factory << imageReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(pageReference, factory.takeObject());
    document = builder.build();

    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    PDFOCRPagePreparer::RasterResult before = preparer.rasterize(0, 150.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    QVERIFY(!before.error);

    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Blue"), QRectF(10, 10, 80, 40) } });
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer modified = applyResults(document, { result }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY(modified);

    RenderingContext modifiedContext(modified.data());
    PDFOCRPagePreparer modifiedPreparer = modifiedContext.createPreparer(modified.data());
    PDFOCRPagePreparer::RasterResult after = modifiedPreparer.rasterize(0, 150.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    QVERIFY(!after.error);

    QCOMPARE(after.image.size(), before.image.size());
    QVERIFY(after.image == before.image);

    // Image data are byte identical (PDF-06)
    const PDFObject& imageObject = modified->getObjectByReference(imageReference);
    QVERIFY(imageObject.isStream());
    QCOMPARE(*imageObject.getStream()->getContent(), imageData);

    // Original content stream is untouched
    const PDFPage* page = modified->getCatalog()->getPage(0);
    const PDFObject& contents = modified->getObject(page->getContents());
    QVERIFY(contents.isArray());
    QCOMPARE(contents.getArray()->getCount(), size_t(4));   // q, original content, Q, text layer
    const PDFObject& originalContent = modified->getObject(contents.getArray()->getItem(1));
    QCOMPARE(modified->getDecodedStream(originalContent.getStream()), spec.content);
}

// -------------------------------------------------------------------------
// PDF-05, PDF-09, PDF-11: robustness of the text layer writer
// -------------------------------------------------------------------------

void OCRTest::textLayerRobustness()
{
    auto dictionaryObject = [](std::initializer_list<std::pair<const char*, PDFObject>> entries)
    {
        PDFDictionary dictionary;
        for (const auto& [key, value] : entries)
        {
            dictionary.addEntry(PDFInplaceOrMemoryString(key), PDFObject(value));
        }
        return PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(dictionary)));
    };

    auto getFontKeys = [](const PDFDocument& document, PDFInteger pageIndex)
    {
        QStringList keys;
        const PDFPage* page = document.getCatalog()->getPage(pageIndex);
        if (const PDFDictionary* resources = document.getDictionaryFromObject(page->getResources()))
        {
            if (const PDFDictionary* fonts = document.getDictionaryFromObject(resources->get("Font")))
            {
                for (size_t i = 0; i < fonts->getCount(); ++i)
                {
                    keys << QString::fromLatin1(fonts->getKey(i).getString());
                }
            }
        }
        keys.sort();
        return keys;
    };

    auto removeLayer = [](const PDFDocument& document, PDFInteger pageIndex) -> PDFDocumentPointer
    {
        PDFDocumentModifier modifier(&document);
        if (!PDFOCRTextLayerWriter::removeLayer(modifier.getBuilder(), &document, pageIndex))
        {
            return nullptr;
        }
        modifier.markPageContentsChanged();
        modifier.markReset();
        return modifier.finalize() ? modifier.getDocument() : nullptr;
    };

    // Page of a scanner: the transformation matrix is changed without q/Q, the font
    // dictionary is an indirect object with a nested font descriptor and font file,
    // and the page has private data of another application.
    PageSpec spec;
    spec.withHelvetica = true;
    spec.content = "2 0 0 2 0 0 cm 0.5 g 5 5 20 10 re f BT /F1 6 Tf 5 60 Td (12) Tj ET";
    PDFDocument document = createDocument({ spec, spec });
    PDFObjectReference originalContentReference;
    {
        PDFDocumentBuilder builder(&document);
        for (size_t i = 0; i < 2; ++i)
        {
            const PDFObjectReference pageReference = document.getCatalog()->getPage(i)->getPageReference();
            const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
            const PDFDictionary* resources = builder.getDictionaryFromObject(pageDictionary->get("Resources"));
            const PDFDictionary* fonts = builder.getDictionaryFromObject(resources->get("Font"));
            const PDFObjectReference fontReference = fonts->get("F1").getReference();

            QByteArray fontFileData(64, 'x');
            PDFDictionary fontFileDictionary;
            fontFileDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(fontFileData.size()));
            const PDFObjectReference fontFileReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(fontFileDictionary), std::move(fontFileData))));
            const PDFObjectReference descriptorReference = builder.addObject(dictionaryObject({ { "Type", PDFObject::createName("FontDescriptor") },
                                                                                                { "FontName", PDFObject::createName("Helvetica") },
                                                                                                { "Flags", PDFObject::createInteger(32) },
                                                                                                { "FontFile3", PDFObject::createReference(fontFileReference) } }));
            builder.mergeTo(fontReference, dictionaryObject({ { "FontDescriptor", PDFObject::createReference(descriptorReference) } }));

            // Indirect font dictionary
            const PDFObjectReference fontDictionaryReference = builder.addObject(dictionaryObject({ { "F1", PDFObject::createReference(fontReference) } }));
            PDFDictionary newResources = *resources;
            newResources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createReference(fontDictionaryReference));

            PDFDictionary newPageDictionary = *pageDictionary;
            newPageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newResources))));
            newPageDictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"),
                                       dictionaryObject({ { "OtherApplication", dictionaryObject({ { "Private", dictionaryObject({ { "Value", PDFObject::createInteger(1) } }) } }) } }));
            if (i == 0)
            {
                originalContentReference = pageDictionary->get("Contents").getReference();
            }
            builder.setObject(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newPageDictionary))));
        }
        document = builder.build();
    }
    document = read(write(document));

    const QByteArray originalFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, 0);
    const QRectF wordRect(20, 100, 60, 12);

    PDFOCRPageResult result0 = createSampleResult(0, { { QStringLiteral("Robust"), wordRect }, { QStringLiteral("SECRETWORD"), QRectF(100, 100, 80, 12) } });
    result0.getWords()[1]->reviewState = PDFOCRReviewState::Discarded;
    PDFOCRPageResult result1 = createSampleResult(1, { { QStringLiteral("Second"), wordRect } });

    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer applied = applyResults(document, { result0, result1 }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY(applied);
    PDFDocument reopened = read(write(*applied));

    // Layer is bound to the page also when the font dictionary was indirect (the fingerprint
    // does not depend on the direct / indirect form of the resources), all objects are verified
    const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(&reopened, 0);
    QVERIFY(info.isPresent);
    QVERIFY(info.fingerprintMatches);
    QVERIFY(info.isContentOwn);
    QVERIFY(info.isDataOwn);
    QVERIFY(info.isFontOwn);
    QVERIFY(info.isIsolationOwn);
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&reopened, 0), originalFingerprint);

    // Font and the isolation streams are shared by the pages
    const PDFOCRTextLayerWriter::LayerInfo info1 = PDFOCRTextLayerWriter::readLayerInfo(&reopened, 1);
    QVERIFY(info1.isPresent && info1.fingerprintMatches);
    QCOMPARE(info1.fontReference, info.fontReference);
    QCOMPARE(info1.isolationBeginReference, info.isolationBeginReference);

    // Text layer is not deformed by the transformation matrix left by the foreign content
    {
        bool found = false;
        const PDFTextLayout layout = extractLayout(reopened, 0);
        for (const PDFTextBlock& block : layout.getTextBlocks())
        {
            for (const PDFTextLine& line : block.getLines())
            {
                QString lineText;
                for (const TextCharacter& character : line.getCharacters())
                {
                    lineText += character.character;
                }
                if (lineText.contains(QStringLiteral("Robust")))
                {
                    QVERIFY2(wordRect.adjusted(-2, -2, 2, 2).contains(line.getCharacters().front().position), "text layer is deformed by the foreign graphic state");
                    found = true;
                }
            }
        }
        QVERIFY(found);
    }

    // Discarded text is not stored anywhere in the document (content, private data)
    QVERIFY(!extractText(reopened, 0).contains(QStringLiteral("SECRETWORD")));
    {
        const PDFObject& dataObject = reopened.getObjectByReference(info.dataReference);
        QVERIFY(dataObject.isStream());
        const QByteArray data = reopened.getDecodedStream(dataObject.getStream());
        QVERIFY(data.contains("Robust"));
        QVERIFY(!data.contains("SECRETWORD"));
    }

    // Replacement of the layer does not create another font
    PDFOCRPageResult corrected = result0;
    corrected.getWords()[0]->text = QStringLiteral("Corrected");
    PDFDocumentPointer replaced = applyResults(reopened, { corrected }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY(replaced);
    QCOMPARE(PDFOCRTextLayerWriter::readLayerInfo(replaced.data(), 0).fontReference, info.fontReference);
    QCOMPARE(getFontKeys(*replaced, 0), QStringList({ QStringLiteral("F1"), QString::fromLatin1(PDFOCRTextLayerWriter::FONT_RESOURCE_PREFIX) }));

    // Removal: font key is removed, data of the other application are kept, content is restored
    PDFDocumentPointer removed = removeLayer(*replaced, 0);
    QVERIFY(removed);
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.data(), 0).isPresent);
    QCOMPARE(getFontKeys(*removed, 0), QStringList({ QStringLiteral("F1") }));
    {
        const PDFPage* page = removed->getCatalog()->getPage(0);
        const PDFDictionary* pieceInfo = removed->getDictionaryFromObject(page->getPieceDictionary(&removed->getStorage()));
        QVERIFY(pieceInfo);
        QVERIFY(pieceInfo->hasKey("OtherApplication"));
        QVERIFY(!pieceInfo->hasKey(PDFOCRTextLayerWriter::PIECE_INFO_KEY));
        QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(removed.data(), 0).size(), size_t(1));
        QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(removed.data(), 0), originalFingerprint);
        QVERIFY(!extractText(*removed, 0).contains(QStringLiteral("Corrected")));
    }
    // Second removal has nothing to do
    QVERIFY(!removeLayer(*removed, 0));

    // Page without any content: the layer is the only content stream, it is still identified
    {
        PDFDocument emptyDocument = createDocument({ PageSpec() });
        {
            PDFDocumentBuilder builder(&emptyDocument);
            const PDFObjectReference pageReference = emptyDocument.getCatalog()->getPage(0)->getPageReference();
            PDFDictionary pageDictionary = *builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
            pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject());
            pageDictionary.removeNullObjects();
            builder.setObject(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageDictionary))));
            emptyDocument = builder.build();
        }
        const QByteArray emptyFingerprint = PDFOCRPagePreparer::computePageFingerprint(&emptyDocument, 0);
        PDFDocumentPointer emptyApplied = applyResults(emptyDocument, { createSampleResult(0, { { QStringLiteral("Alone"), wordRect } }) }, PDFOCRTextLayerWriter::Options(), &report);
        QVERIFY(emptyApplied);
        QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(emptyApplied.data(), 0).size(), size_t(1));
        const PDFOCRTextLayerWriter::LayerInfo emptyInfo = PDFOCRTextLayerWriter::readLayerInfo(emptyApplied.data(), 0);
        QVERIFY(emptyInfo.isPresent && emptyInfo.isContentOwn && emptyInfo.fingerprintMatches);
        QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(emptyApplied.data(), 0), emptyFingerprint);
        QVERIFY(removeLayer(*emptyApplied, 0));
    }
    // Layer of the other page is untouched
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(removed.data(), 1).fingerprintMatches);

    // Page without any text left: obsolete layer is removed (AT-18)
    PDFOCRPageResult discarded = result1;
    discarded.getWords()[0]->reviewState = PDFOCRReviewState::Discarded;
    PDFDocumentPointer cleared = applyResults(reopened, { discarded }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY(cleared);
    QCOMPARE(report.removedPages, (std::vector<PDFInteger>{ 1 }));
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(cleared.data(), 1).isPresent);
    QVERIFY(!extractText(*cleared, 1).contains(QStringLiteral("Second")));

    // Review data: a change of the review state only is written (content stream is identical)
    PDFOCRTextLayerWriter::Options reviewOptions;
    reviewOptions.keepReviewData = true;
    PDFDocumentPointer withReview = applyResults(document, { result1 }, reviewOptions, &report);
    QVERIFY(withReview);
    QVERIFY(!applyResults(*withReview, { result1 }, reviewOptions, &report));
    QCOMPARE(report.unchangedPages, (std::vector<PDFInteger>{ 1 }));
    PDFOCRPageResult confirmed = result1;
    confirmed.getWords()[0]->reviewState = PDFOCRReviewState::Confirmed;
    QVERIFY(applyResults(*withReview, { confirmed }, reviewOptions, &report));
    QCOMPARE(report.writtenPages, (std::vector<PDFInteger>{ 1 }));

    // Forged metadata: references point to foreign objects (content of the page, font
    // of the page, catalog) and outside of the document. Nothing foreign may be removed.
    {
        PDFDocumentBuilder builder(&document);
        const PDFPage* page = document.getCatalog()->getPage(0);
        const PDFObjectReference pageReference = page->getPageReference();
        const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
        const PDFObjectReference contentReference = pageDictionary->get("Contents").getReference();
        const PDFObjectReference catalogReference = builder.getCatalogReference();

        const PDFObject privateData = dictionaryObject({ { "Version", PDFObject::createInteger(1) },
                                                         { "Contents", PDFObject::createReference(contentReference) },
                                                         { "IsolationBegin", PDFObject::createReference(contentReference) },
                                                         { "IsolationEnd", PDFObject::createReference(PDFObjectReference(9999999, 0)) },
                                                         { "Data", PDFObject::createReference(catalogReference) },
                                                         { "Font", PDFObject::createReference(PDFObjectReference(8888888, 0)) },
                                                         { "FontKey", PDFObject::createName("F1") } });
        PDFDictionary newPageDictionary = *pageDictionary;
        newPageDictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"),
                                   dictionaryObject({ { PDFOCRTextLayerWriter::PIECE_INFO_KEY, dictionaryObject({ { "Private", privateData } }) } }));
        builder.setObject(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newPageDictionary))));
        PDFDocument forged = builder.build();

        const PDFOCRTextLayerWriter::LayerInfo forgedInfo = PDFOCRTextLayerWriter::readLayerInfo(&forged, 0);
        QVERIFY(forgedInfo.isPresent);
        QVERIFY(!forgedInfo.isContentOwn);
        QVERIFY(!forgedInfo.isDataOwn);
        QVERIFY(!forgedInfo.isFontOwn);
        QVERIFY(!forgedInfo.isIsolationOwn);

        // Removal detaches the metadata only
        PDFDocumentPointer forgedRemoved = removeLayer(forged, 0);
        QVERIFY(forgedRemoved);
        QVERIFY(forgedRemoved->getObjectByReference(contentReference).isStream());
        QVERIFY(forgedRemoved->getObjectByReference(catalogReference).isDictionary());
        QCOMPARE(getFontKeys(*forgedRemoved, 0), QStringList({ QStringLiteral("F1") }));
        QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(forgedRemoved.data(), 0), (std::vector<PDFObjectReference>{ contentReference }));

        // Writing over the forged metadata keeps the foreign content and font as well
        PDFDocumentPointer forgedApplied = applyResults(forged, { result0 }, PDFOCRTextLayerWriter::Options(), &report);
        QVERIFY(forgedApplied);
        QVERIFY(forgedApplied->getObjectByReference(contentReference).isStream());
        QVERIFY(forgedApplied->getObjectByReference(catalogReference).isDictionary());
        QVERIFY(getFontKeys(*forgedApplied, 0).contains(QStringLiteral("F1")));
        const std::vector<PDFObjectReference> forgedContents = PDFOCRTextLayerWriter::getPageContentReferences(forgedApplied.data(), 0);
        QVERIFY(std::find(forgedContents.begin(), forgedContents.end(), contentReference) != forgedContents.end());
        QVERIFY(extractText(*forgedApplied, 0).contains(QStringLiteral("Robust")));
        QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(forgedApplied.data(), 0).isContentOwn);
    }

    Q_UNUSED(originalContentReference);
}

// -------------------------------------------------------------------------
// IMAGE-04, IMAGE-06: preprocessing pipeline (deskew direction, orientation)
// -------------------------------------------------------------------------

void OCRTest::preprocessingPipeline()
{
    auto createSkewedImage = [](double angle)
    {
        QImage image(900, 700, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(image.width() * 0.5, image.height() * 0.5);
        painter.rotate(angle);   // positive = clockwise in the image
        for (int i = -6; i <= 6; ++i)
        {
            painter.fillRect(QRectF(-320, i * 40 - 5, 640, 10), Qt::black);
        }
        painter.end();
        return image;
    };

    for (const double skew : { 3.0, -2.0 })
    {
        const QImage skewed = createSkewedImage(skew);

        double confidence = 0.0;
        const double estimated = PDFOCRPagePreparer::estimateSkewAngle(skewed, &confidence, nullptr);
        QVERIFY2(std::abs(estimated - skew) <= 0.5, qPrintable(QStringLiteral("estimated %1, expected %2").arg(estimated).arg(skew)));
        QVERIFY(confidence >= 30.0);

        PDFOCRPreprocessing preprocessing;
        preprocessing.deskew = true;
        preprocessing.grayscale = false;
        const PDFOCRPagePreparer::PreprocessResult result = PDFOCRPagePreparer::preprocess(skewed, PDFOCRPageGeometry(), preprocessing, std::nullopt, nullptr);
        QVERIFY(!result.error);
        QVERIFY(!result.image.isNull());
        QVERIFY(!result.geometry.rasterToEngine.isIdentity());
        QVERIFY(result.geometry.rasterToEngine.isInvertible());

        // The image is straightened (not skewed twice as much)
        double remainingConfidence = 0.0;
        const double remaining = PDFOCRPagePreparer::estimateSkewAngle(result.image, &remainingConfidence, nullptr);
        QVERIFY2(std::abs(remaining) <= 0.5, qPrintable(QStringLiteral("skew %1: remaining skew %2").arg(skew).arg(remaining)));

        // Recorded matrix maps the raster to the straightened image: the center of the bars stays dark
        const QPointF center = result.geometry.rasterToEngine.map(QPointF(skewed.width() * 0.5, skewed.height() * 0.5));
        QVERIFY(qGray(result.image.pixel(center.toPoint())) < 128);
        const QPointF barEnd = result.geometry.rasterToEngine.map(QTransform().translate(450, 350).rotate(skew).map(QPointF(300, 0)));
        QVERIFY(qGray(result.image.pixel(barEnd.toPoint())) < 128);
    }

    // Detected orientation is absolute: it replaces the manual rotation, it is not added to it
    {
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(Qt::white);

        PDFOCRPreprocessing preprocessing;
        preprocessing.rotation = 90;
        preprocessing.autoOrientation = true;
        preprocessing.grayscale = false;

        PDFOCROrientation orientation;
        orientation.rotation = 90;
        orientation.confidence = 80.0;
        PDFOCRPagePreparer::PreprocessResult result = PDFOCRPagePreparer::preprocess(image, PDFOCRPageGeometry(), preprocessing, orientation, nullptr);
        QVERIFY(!result.error);
        QCOMPARE(result.image.size(), QSize(100, 200));
        QVERIFY(result.geometry.pipeline.contains(QStringLiteral("rotate(90)")));

        // Uncertain detection is not applied, the manual rotation stays
        orientation.rotation = 180;
        orientation.confidence = PDFOCRPagePreparer::MinimumOrientationConfidence * 0.5;
        result = PDFOCRPagePreparer::preprocess(image, PDFOCRPageGeometry(), preprocessing, orientation, nullptr);
        QVERIFY(!result.error);
        QCOMPARE(result.image.size(), QSize(100, 200));
        QVERIFY(result.geometry.pipeline.contains(QStringLiteral("rotate(90)")));

        // Confident detection of the upright orientation overrides a wrong manual rotation
        orientation.rotation = 0;
        orientation.confidence = 80.0;
        result = PDFOCRPagePreparer::preprocess(image, PDFOCRPageGeometry(), preprocessing, orientation, nullptr);
        QCOMPARE(result.image.size(), QSize(200, 100));
    }

    // Limited resolution always fits into the pixel limit (raster size is rounded)
    {
        PageSpec spec;
        spec.size = QSizeF(595.3, 841.9);
        PDFDocument document = createDocument({ spec });
        const PDFPage* page = document.getCatalog()->getPage(0);
        for (qint64 maximumPixels = 1000000; maximumPixels < 1000000 + 40 * 7919; maximumPixels += 7919)
        {
            const double dpi = PDFOCRPagePreparer::getLimitedDpi(page, 1200.0, maximumPixels);
            const QSize size = PDFOCRPagePreparer::getRasterSize(page, dpi);
            QVERIFY2(qint64(size.width()) * qint64(size.height()) <= maximumPixels, qPrintable(QString::number(maximumPixels)));
        }
    }

    // Deduplication of the regions: a small word inside of a large wrong box is kept,
    // the same word recognized again in an overlapping region is not added twice
    {
        PDFOCRPageGeometry geometry;
        PDFOCRPageResult result;
        result.pageIndex = 0;

        auto createOutput = [](std::initializer_list<std::pair<QString, QRectF>> words)
        {
            PDFOCRRecognitionOutput output;
            PDFOCRRawBlock block;
            PDFOCRRawLine line;
            for (const auto& item : words)
            {
                PDFOCRRawWord word;
                word.text = item.first;
                word.rect = item.second;
                line.words.push_back(word);
                line.rect = line.rect.united(item.second);
            }
            block.rect = line.rect;
            block.lines.push_back(line);
            output.blocks.push_back(block);
            return output;
        };

        PDFOCRPagePreparer::appendOutput(result, createOutput({ { QStringLiteral("logo"), QRectF(0, 0, 400, 200) }, { QStringLiteral("small"), QRectF(20, 20, 60, 14) }, { QStringLiteral("word"), QRectF(90, 20, 50, 14) } }), geometry, -1, { });
        QCOMPARE(result.getWordCount(), 3);
        PDFOCRPagePreparer::appendOutput(result, createOutput({ { QStringLiteral("word"), QRectF(91, 20, 50, 14) }, { QStringLiteral("next"), QRectF(150, 20, 50, 14) } }), geometry, -1, { });
        QCOMPARE(result.getWordCount(), 4);
        QVERIFY(result.getText().contains(QStringLiteral("next")));
    }
}

// -------------------------------------------------------------------------
// EDIT-03, EDIT-07, EDIT-08, EDIT-09: regressions of the editing session
// -------------------------------------------------------------------------

void OCRTest::sessionRegressions()
{
    // Undo cannot throw a finished recognition away: steps recorded before the new
    // result of the page are removed from the history.
    {
        PDFOCRSession session(nullptr);
        PDFOCRRegion region;
        region.rect = QRectF(10, 10, 100, 50);
        QVERIFY(session.addRegion(0, region) > 0);
        QVERIFY(session.canUndo());

        session.setPageResult(createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) }, { QStringLiteral("world"), QRectF(70, 100, 50, 12) } }));
        QVERIFY(!session.canUndo());
        QVERIFY(!session.hasManualCorrections(0));

        const int wordId = session.getPage(0)->getWords()[0]->id;
        QVERIFY(session.setWordText(0, wordId, QStringLiteral("Hallo")));
        session.undo();
        QVERIFY(session.getPage(0)->hasResult());
        QCOMPARE(session.getPage(0)->getWords()[0]->text, QStringLiteral("Hello"));
        QVERIFY(!session.canUndo());
        session.undo();
        QVERIFY(session.getPage(0)->hasResult());

        // Structural change is a manual correction as well (EDIT-07)
        QVERIFY(session.removeWord(0, session.getPage(0)->getWords()[1]->id));
        QVERIFY(session.hasManualCorrections(0));

        // Failed edit of an unknown page leaves no page record
        QVERIFY(!session.setWordText(7, 1, QStringLiteral("x")));
        QVERIFY(!session.getPage(7));

        // Page being recognized cannot be changed by undo
        session.setPageState(0, PDFOCRPageState::Recognizing);
        session.undo();
        QCOMPARE(session.getPage(0)->getWordCount(), 1);
        session.setPageState(0, PDFOCRPageState::Done);
        session.undo();
        QCOMPARE(session.getPage(0)->getWordCount(), 2);
    }

    // Orientation of a rotated line is kept by the edits (page with /Rotate 90)
    {
        PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("alpha"), QRectF(0, 0, 50, 12) }, { QStringLiteral("beta"), QRectF(60, 0, 40, 12) } });
        QTransform rotation;
        rotation.translate(200, 100);
        rotation.rotate(90);
        for (PDFOCRBlock& block : result.blocks)
        {
            block.quad = block.quad.transformed(rotation);
            for (PDFOCRLine& line : block.lines)
            {
                line.quad = line.quad.transformed(rotation);
                for (PDFOCRWord& word : line.words)
                {
                    word.quad = word.quad.transformed(rotation);
                }
            }
        }

        PDFOCRSession session(nullptr);
        session.setPageResult(result);
        const QPointF direction = session.getPage(0)->blocks[0].lines[0].quad.direction();
        QVERIFY(std::abs(direction.x()) < 1e-6 && std::abs(std::abs(direction.y()) - 1.0) < 1e-6);

        // Geometry edit recomputes the line from the words
        const PDFOCRWord* first = session.getPage(0)->getWords()[0];
        QVERIFY(session.setWordQuad(0, first->id, first->quad.translated(direction * -2.0)));
        const PDFOCRLine& line = session.getPage(0)->blocks[0].lines[0];
        QVERIFY(QPointF::dotProduct(line.quad.direction(), direction) > 0.999);
        QVERIFY(qAbs(line.quad.height() - 12.0) < 1e-6);

        // Change of the token count lays the words out along the rotated line
        QVERIFY(session.setLineText(0, line.id, QStringLiteral("alpha gamma delta beta")));
        for (const PDFOCRWord* word : session.getPage(0)->getWords())
        {
            QVERIFY2(word->quad.isValid(), qPrintable(word->text));
            QVERIFY(QPointF::dotProduct(word->quad.direction(), direction) > 0.999);
            QVERIFY(word->quad.width() > 1.0);
        }
        QVERIFY(PDFOCRValidator::validate(*session.getPage(0)).isEmpty());
    }

    // Right-to-left line: quads are visual, words are in the logical order (first word is the rightmost)
    {
        PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("ABCD"), QRectF(200, 100, 80, 12) }, { QStringLiteral("EF"), QRectF(150, 100, 40, 12) }, { QStringLiteral("GH"), QRectF(100, 100, 40, 12) } });
        result.blocks[0].lines[0].direction = PDFOCRTextDirection::RightToLeft;

        PDFOCRSession session(nullptr);
        session.setPageResult(result);
        const int firstId = session.getPage(0)->getWords()[0]->id;
        const int secondId = session.getPage(0)->getWords()[1]->id;

        // Merge covers both words
        int mergedId = 0;
        QVERIFY(session.mergeWords(0, firstId, secondId, &mergedId));
        const QRectF mergedRect = session.getPage(0)->findWord(mergedId)->quad.boundingRect();
        QVERIFY(qAbs(mergedRect.left() - 150.0) < 1e-6 && qAbs(mergedRect.right() - 280.0) < 1e-6);
        QVERIFY(session.getPage(0)->findWord(mergedId)->quad.direction().x() > 0.999);
        session.undo();

        // Split: the first characters lie in the right part of the quad
        int rightId = 0;
        QVERIFY(session.splitWord(0, firstId, 1, &rightId));
        const std::vector<const PDFOCRWord*> words = std::as_const(*session.getPage(0)).getWords();
        QCOMPARE(words[0]->text, QStringLiteral("A"));
        QCOMPARE(words[1]->text, QStringLiteral("BCD"));
        QVERIFY(words[0]->quad.boundingRect().left() > words[1]->quad.boundingRect().left());
        QVERIFY(qAbs(words[0]->quad.boundingRect().right() - 280.0) < 1e-6);
        QVERIFY(qAbs(words[1]->quad.boundingRect().left() - 200.0) < 1e-6);
        // Restoring the original text of a half does not bring the whole word back
        QVERIFY(session.restoreOriginalText(0, words[0]->id) == false || session.getPage(0)->getWords()[0]->text == QStringLiteral("A"));
        session.undo();

        // Line edit: inserted words flow to the left
        const int lineId = session.getPage(0)->blocks[0].lines[0].id;
        QVERIFY(session.setLineText(0, lineId, QStringLiteral("ABCD XX YY EF GH")));
        double previousLeft = 1e9;
        for (const PDFOCRWord* word : std::as_const(*session.getPage(0)).getWords())
        {
            QVERIFY2(word->quad.isValid(), qPrintable(word->text));
            QVERIFY(word->quad.direction().x() > 0.999);
            QVERIFY2(word->quad.boundingRect().left() < previousLeft, qPrintable(word->text));
            previousLeft = word->quad.boundingRect().left();
        }
    }

    // A surrogate pair cannot be split
    {
        PDFOCRSession session(nullptr);
        session.setPageResult(createSampleResult(0, { { QString::fromUtf8("a\xF0\x9D\x94\x98" "b"), QRectF(10, 100, 50, 12) } }));
        const int wordId = session.getPage(0)->getWords()[0]->id;
        QVERIFY(!session.splitWord(0, wordId, 2, nullptr));
        QVERIFY(session.splitWord(0, wordId, 3, nullptr));
    }

    // Replacement of a region keeps the reading order and the identity of the other blocks
    {
        PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("first"), QRectF(10, 150, 50, 12) } });
        PDFOCRPageResult middle = createSampleResult(0, { { QStringLiteral("middle"), QRectF(10, 100, 50, 12) } });
        PDFOCRPageResult last = createSampleResult(0, { { QStringLiteral("last"), QRectF(10, 50, 50, 12) } });
        middle.blocks[0].regionId = 5;
        middle.blocks[0].id = 0;
        last.blocks[0].id = 0;
        result.blocks.push_back(middle.blocks[0]);
        result.blocks.push_back(last.blocks[0]);

        PDFOCRRegion region;
        region.id = 5;
        region.rect = QRectF(0, 90, 200, 30);
        result.regions.push_back(region);

        PDFOCRSession session(nullptr);
        session.setPageResult(result);
        const int lastBlockId = session.getPage(0)->blocks[2].id;

        PDFOCRPageResult candidate = createSampleResult(0, { { QStringLiteral("center"), QRectF(10, 100, 50, 12) } });
        candidate.blocks[0].regionId = 5;
        QVERIFY(session.applyCandidate(0, candidate, PDFOCRSession::CandidateMode::ReplaceRegion, 5));
        QCOMPARE(session.getPage(0)->blocks.size(), size_t(3));
        QCOMPARE(session.getPage(0)->blocks[1].getText(), QStringLiteral("center"));
        QCOMPARE(session.getPage(0)->blocks[2].id, lastBlockId);
        QVERIFY2(PDFOCRValidator::validate(*session.getPage(0)).isEmpty(), qPrintable(PDFOCRValidator::validate(*session.getPage(0)).join(QChar(10))));
    }

    // Loaded project replaces the results of the session
    {
        PDFOCRSession source(nullptr);
        source.setPageResult(createSampleResult(0, { { QStringLiteral("project"), QRectF(10, 100, 50, 12) } }));
        const PDFOCRProject project = source.createProject({ 0 });

        PDFOCRSession session(nullptr);
        session.setPageResult(createSampleResult(0, { { QStringLiteral("old"), QRectF(10, 100, 50, 12) } }));
        session.setPageResult(createSampleResult(3, { { QStringLiteral("stale"), QRectF(10, 100, 50, 12) } }));
        session.loadProject(project, { 0 });
        QCOMPARE(session.getPage(0)->getText(), QStringLiteral("project"));
        QVERIFY(!session.getPage(3));
        QVERIFY(!session.canUndo());
    }

    // Replace all: only the pages with hits are modified
    {
        PDFOCRSession session(nullptr);
        session.setPageResult(createSampleResult(0, { { QStringLiteral("teh"), QRectF(10, 100, 50, 12) } }));
        session.setPageResult(createSampleResult(1, { { QStringLiteral("other"), QRectF(10, 100, 50, 12) } }));
        QCOMPARE(session.replaceAll({ 0, 1 }, QStringLiteral("teh"), QStringLiteral("the"), PDFOCRSession::FindOptions()), 1);
        QVERIFY(session.hasManualCorrections(0));
        QVERIFY(!session.hasManualCorrections(1));
        QCOMPARE(session.replaceAll({ 0, 1 }, QStringLiteral("missing"), QStringLiteral("x"), PDFOCRSession::FindOptions()), 0);
        session.undo();
        QCOMPARE(session.getPage(0)->getText(), QStringLiteral("teh"));
    }
}

// -------------------------------------------------------------------------
// AT-11: geometry (GEOM-05)
// -------------------------------------------------------------------------

void OCRTest::geometryRoundTrip()
{
    // Synthetic transformations: crop box with offset, rotation, user unit, deskew and padding
    PDFOCRPageGeometry geometry;
    geometry.cropBox = QRectF(30, 50, 400, 600);
    geometry.mediaBox = QRectF(0, 0, 500, 700);
    geometry.rotation = 90;
    geometry.userUnit = 2.5;
    geometry.dpi = 300.0;
    geometry.rasterSize = QSize(2500, 1667);
    geometry.pageToRaster = PDFRenderer::createMediaBoxToDevicePointMatrix(PDFPage::getRotatedBox(geometry.cropBox, PageRotation::Rotate90), QRectF(0, 0, 2500, 1667), PageRotation::Rotate90);

    QTransform deskew;
    deskew.rotate(1.7);
    QTransform padding;
    padding.translate(12, 8);
    geometry.rasterToEngine = QImage::trueMatrix(deskew, 2500, 1667) * padding;
    QVERIFY(geometry.isInvertible());

    const QTransform pageToEngine = geometry.getPageToEngine();
    const QTransform engineToPage = geometry.getEngineToPage();

    // Tolerance is 0.25 points in physical points after user unit (GEOM-05)
    const double tolerance = 0.25 / geometry.userUnit;

    for (int i = 0; i < 200; ++i)
    {
        const QPointF pagePoint(30.0 + (i * 37) % 400, 50.0 + (i * 53) % 600);
        const QPointF enginePoint = pageToEngine.map(pagePoint);
        const QPointF back = engineToPage.map(enginePoint);
        QVERIFY2((back - pagePoint).manhattanLength() < tolerance, qPrintable(QStringLiteral("%1,%2 -> %3,%4").arg(pagePoint.x()).arg(pagePoint.y()).arg(back.x()).arg(back.y())));
    }

    // Quads and baselines are transformed as whole (all vertices)
    PDFOCRQuad quad = PDFOCRQuad::fromRect(QRectF(100, 200, 50, 12));
    const PDFOCRQuad transformed = quad.transformed(pageToEngine).transformed(engineToPage);
    for (size_t i = 0; i < 4; ++i)
    {
        QVERIFY((transformed.points[i] - quad.points[i]).manhattanLength() < tolerance);
    }
}

void OCRTest::renderedGeometryRoundTrip()
{
    // A rectangle drawn at known page coordinates is located in the raster and
    // mapped back; every rotation, a crop box with offset and a user unit are covered.
    struct Case
    {
        PageRotation rotation;
        QRectF cropBox;
        double userUnit;
        QRectF mediaBox;
    };

    const std::vector<Case> cases =
    {
        { PageRotation::None, QRectF(), 1.0, QRectF() },
        { PageRotation::Rotate90, QRectF(), 1.0, QRectF() },
        { PageRotation::Rotate180, QRectF(), 1.0, QRectF() },
        { PageRotation::Rotate270, QRectF(), 1.0, QRectF() },
        { PageRotation::None, QRectF(20, 30, 250, 150), 1.0, QRectF() },
        { PageRotation::Rotate90, QRectF(20, 30, 250, 150), 2.0, QRectF() },
        { PageRotation::None, QRectF(), 1.0, QRectF(-50, 100, 300, 200) },
    };

    for (const Case& testCase : cases)
    {
        PageSpec spec;
        spec.size = QSizeF(300, 200);
        spec.mediaBox = testCase.mediaBox;
        const QRectF mediaBox = testCase.mediaBox.isValid() ? testCase.mediaBox : QRectF(QPointF(0, 0), spec.size);
        const QRectF rectangle(mediaBox.left() + 60, mediaBox.top() + 80, 90, 40);
        spec.content = QStringLiteral("0 0 0 rg %1 %2 %3 %4 re f").arg(rectangle.left()).arg(rectangle.top()).arg(rectangle.width()).arg(rectangle.height()).toLatin1();
        spec.rotation = testCase.rotation;
        spec.cropBox = testCase.cropBox;
        spec.userUnit = testCase.userUnit;
        PDFDocument document = createDocument({ spec });

        RenderingContext context(&document);
        PDFOCRPagePreparer preparer = context.createPreparer(&document);
        PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 600.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
        QVERIFY2(!raster.error, qPrintable(raster.error.message));
        QCOMPARE(raster.geometry.rotation, int(testCase.rotation) * 90);

        const QRect darkBox = findDarkBoundingBox(raster.image);
        QVERIFY(!darkBox.isNull());

        // Deskew of zero degrees and padding are part of the pipeline in the engine space
        PDFOCRPreprocessing preprocessing;
        preprocessing.grayscale = true;
        PDFOCRPagePreparer::PreprocessResult preprocessed = PDFOCRPagePreparer::preprocess(raster.image, raster.geometry, preprocessing, std::nullopt, nullptr);
        QVERIFY(!preprocessed.error);

        const QRect engineBox = findDarkBoundingBox(preprocessed.image);
        PDFOCRRecognitionOutput output;
        PDFOCRRawBlock block;
        block.rect = QRectF(engineBox);
        PDFOCRRawLine line;
        line.rect = QRectF(engineBox);
        PDFOCRRawWord word;
        word.text = QStringLiteral("box");
        word.rect = QRectF(engineBox);
        line.words.push_back(word);
        block.lines.push_back(line);
        output.blocks.push_back(block);

        PDFOCRPageResult result;
        result.pageIndex = 0;
        PDFOCRPagePreparer::appendOutput(result, output, preprocessed.geometry, -1, { });
        QCOMPARE(result.getWordCount(), 1);

        // Mapped rectangle equals the drawn rectangle within 0.25 points (one pixel at 600 DPI is 0.12 pt)
        const QRectF mapped = result.getWords()[0]->quad.boundingRect();
        const double tolerance = 0.25 / testCase.userUnit + 0.13;
        QVERIFY2(qAbs(mapped.left() - rectangle.left()) < tolerance, qPrintable(QStringLiteral("rotation %1: left %2 vs %3").arg(int(testCase.rotation)).arg(mapped.left()).arg(rectangle.left())));
        QVERIFY2(qAbs(mapped.top() - rectangle.top()) < tolerance, qPrintable(QStringLiteral("rotation %1: top %2 vs %3").arg(int(testCase.rotation)).arg(mapped.top()).arg(rectangle.top())));
        QVERIFY2(qAbs(mapped.width() - rectangle.width()) < 2 * tolerance, qPrintable(QStringLiteral("rotation %1: width %2 vs %3").arg(int(testCase.rotation)).arg(mapped.width()).arg(rectangle.width())));
        QVERIFY2(qAbs(mapped.height() - rectangle.height()) < 2 * tolerance, qPrintable(QStringLiteral("rotation %1: height %2 vs %3").arg(int(testCase.rotation)).arg(mapped.height()).arg(rectangle.height())));

        // The writing direction of the quad follows the displayed (rotated) page:
        // the baseline runs along the visual horizontal edge of the rectangle.
        const PDFOCRQuad& quad = result.getWords()[0]->quad;
        QVERIFY(quad.isValid());
        const bool transposed = testCase.rotation == PageRotation::Rotate90 || testCase.rotation == PageRotation::Rotate270;
        QVERIFY2(qAbs(quad.width() - (transposed ? rectangle.height() : rectangle.width())) < 2 * tolerance, qPrintable(QString::number(quad.width())));
        QVERIFY2(qAbs(quad.height() - (transposed ? rectangle.width() : rectangle.height())) < 2 * tolerance, qPrintable(QString::number(quad.height())));
        switch (testCase.rotation)
        {
            case PageRotation::None:
                QVERIFY(quad.points[1].x() > quad.points[0].x() && quad.points[3].y() > quad.points[0].y());
                break;
            case PageRotation::Rotate90:
                // Page displayed rotated clockwise: the visual right is the +y direction of the page
                QVERIFY(quad.points[1].y() > quad.points[0].y() && quad.points[3].x() < quad.points[0].x());
                break;
            case PageRotation::Rotate180:
                QVERIFY(quad.points[1].x() < quad.points[0].x() && quad.points[3].y() < quad.points[0].y());
                break;
            case PageRotation::Rotate270:
                QVERIFY(quad.points[1].y() < quad.points[0].y() && quad.points[3].x() > quad.points[0].x());
                break;
        }
    }
}

// -------------------------------------------------------------------------
// AT-12: repeated application, replacement and removal of the own layer
// -------------------------------------------------------------------------

void OCRTest::idempotentApplyAndRemove()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec, spec });

    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("first"), QRectF(20, 100, 100, 30) } });
    PDFOCRTextLayerWriter::Options options;
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer first = applyResults(document, { result }, options, &report);
    QVERIFY(first);

    const QByteArray fingerprintBefore = PDFOCRPagePreparer::computePageFingerprint(&document, 0);
    const QByteArray fingerprintAfter = PDFOCRPagePreparer::computePageFingerprint(first.data(), 0);
    QCOMPARE(fingerprintBefore, fingerprintAfter);
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&document, 1), PDFOCRPagePreparer::computePageFingerprint(first.data(), 1));

    // Second application of the same result: nothing changes (PDF-11)
    PDFDocumentPointer second = applyResults(*first, { result }, options, &report);
    QVERIFY(!second);
    QCOMPARE(report.unchangedPages, (std::vector<PDFInteger>{ 0 }));
    QVERIFY(report.writtenPages.empty());

    // Replacement by a corrected result: still a single layer
    result.getWords()[0]->text = QStringLiteral("second");
    result.generation = 2;
    PDFDocumentPointer third = applyResults(*first, { result }, options, &report);
    QVERIFY(third);
    const PDFPage* page = third->getCatalog()->getPage(0);
    const PDFObject& contents = third->getObject(page->getContents());
    QVERIFY(contents.isArray());
    QCOMPARE(contents.getArray()->getCount(), size_t(4));   // q, original content, Q, single text layer
    QVERIFY(extractText(*third, 0).contains(QStringLiteral("second")));
    QVERIFY(!extractText(*third, 0).contains(QStringLiteral("first")));

    // Font resources contain exactly one own font
    const PDFDictionary* resources = third->getDictionaryFromObject(page->getResources());
    QVERIFY(resources);
    const PDFDictionary* fonts = third->getDictionaryFromObject(resources->get("Font"));
    QVERIFY(fonts);
    QCOMPARE(fonts->getCount(), size_t(1));

    // Removal restores the original content and removes the private data (PDF-11)
    PDFDocumentModifier modifier(third.data());
    QVERIFY(PDFOCRTextLayerWriter::removeLayer(modifier.getBuilder(), third.data(), 0));
    QVERIFY(!PDFOCRTextLayerWriter::removeLayer(modifier.getBuilder(), third.data(), 1));
    modifier.markPageContentsChanged();
    modifier.markReset();
    QVERIFY(modifier.finalize());
    PDFDocumentPointer removed = modifier.getDocument();
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.data(), 0).isPresent);
    const PDFPage* removedPage = removed->getCatalog()->getPage(0);
    const PDFObject& removedContents = removed->getObject(removedPage->getContents());
    QVERIFY(removedContents.isStream() || (removedContents.isArray() && removedContents.getArray()->getCount() == 1));
    QVERIFY(!extractText(*removed, 0).contains(QStringLiteral("second")));
    QVERIFY(extractLayout(*removed, 0).getTextBlocks().empty());
    QVERIFY(removed->getDictionaryFromObject(removedPage->getPieceDictionary(&removed->getStorage())) == nullptr ||
            !removed->getDictionaryFromObject(removedPage->getPieceDictionary(&removed->getStorage()))->hasKey(PDFOCRTextLayerWriter::PIECE_INFO_KEY));

    // Foreign invisible text is not removed
    PageSpec foreign;
    foreign.withHelvetica = true;
    foreign.content = "BT 3 Tr /F1 12 Tf 20 100 Td (foreign text of another tool) Tj ET";
    PDFDocument foreignDocument = createDocument({ foreign });
    PDFDocumentModifier foreignModifier(&foreignDocument);
    QVERIFY(!PDFOCRTextLayerWriter::removeLayer(foreignModifier.getBuilder(), &foreignDocument, 0));
    QVERIFY(!foreignModifier.finalize());
}

// -------------------------------------------------------------------------
// AT-13: analysis and existing text policy
// -------------------------------------------------------------------------

void OCRTest::pageAnalysisAndPolicy()
{
    PageSpec imagePage;
    imagePage.withHelvetica = true;
    imagePage.withImage = true;
    imagePage.content = "BT /F1 10 Tf 280 10 Td (12) Tj ET";    // scan with a digital page number

    PageSpec textPage;
    textPage.withHelvetica = true;
    textPage.content = "BT /F1 12 Tf 20 150 Td (This is a page with a usable amount of visible digital text, ) Tj 0 -14 Td (which does not need any recognition at all.) Tj ET";

    PageSpec invisiblePage;
    invisiblePage.withHelvetica = true;
    invisiblePage.withImage = true;
    invisiblePage.content = "BT 3 Tr /F1 12 Tf 20 150 Td (This invisible text was created by another OCR tool, it is foreign.) Tj ET";

    PageSpec emptyPage;

    PageSpec mixedPage;
    mixedPage.withHelvetica = true;
    mixedPage.withImage = true;
    mixedPage.content = "BT /F1 12 Tf 20 180 Td (This is a page with a usable amount of visible digital text and an image.) Tj ET";

    PDFDocument document = createDocument({ imagePage, textPage, invisiblePage, emptyPage, mixedPage });
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);

    const PDFOCRPageAnalysis image = preparer.analyze(0, nullptr);
    QVERIFY2(image.contentClass == PDFOCRPageContentClass::Image,
             qPrintable(QStringLiteral("class %1, visible %2, invisible %3, unmapped %4, images %5, reasons: %6")
                        .arg(int(image.contentClass)).arg(image.visibleCharacterCount).arg(image.invisibleCharacterCount).arg(image.unmappedCharacterCount).arg(image.imageCount).arg(image.ambiguityReasons.join(QStringLiteral(" | ")))));
    QVERIFY(image.visibleCharacterCount > 0);
    QVERIFY(!image.hasVisibleText);
    QVERIFY(!image.notes.isEmpty());

    const PDFOCRPageAnalysis text = preparer.analyze(1, nullptr);
    QCOMPARE(text.contentClass, PDFOCRPageContentClass::VisibleText);
    QVERIFY(text.hasVisibleText);

    const PDFOCRPageAnalysis invisible = preparer.analyze(2, nullptr);
    QCOMPARE(invisible.contentClass, PDFOCRPageContentClass::InvisibleText);
    QVERIFY(invisible.hasInvisibleText);
    QVERIFY(!invisible.hasOwnOCRLayer);

    const PDFOCRPageAnalysis empty = preparer.analyze(3, nullptr);
    QCOMPARE(empty.contentClass, PDFOCRPageContentClass::Empty);

    const PDFOCRPageAnalysis mixed = preparer.analyze(4, nullptr);
    QCOMPARE(mixed.contentClass, PDFOCRPageContentClass::Mixed);

    QString reason;
    using Decision = PDFOCRPagePreparer::PolicyDecision;
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(image, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::Recognize);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(text, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::Skip);
    QVERIFY(!reason.isEmpty());
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(invisible, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::Skip);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(mixed, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::NeedsDecision);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(text, PDFOCRExistingTextPolicy::ReviewOnly, false, &reason), Decision::Recognize);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(text, PDFOCRExistingTextPolicy::AddInRegions, true, &reason), Decision::Recognize);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(text, PDFOCRExistingTextPolicy::AddInRegions, false, &reason), Decision::NeedsDecision);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(invisible, PDFOCRExistingTextPolicy::ReplaceOwnLayer, false, &reason), Decision::Skip);

    // Own layer is recognized as own and replaced by the replace mode
    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("scan"), QRectF(10, 50, 200, 100) } });
    PDFDocumentPointer withLayer = applyResults(document, { result }, PDFOCRTextLayerWriter::Options(), nullptr);
    QVERIFY(withLayer);
    RenderingContext layerContext(withLayer.data());
    PDFOCRPagePreparer layerPreparer = layerContext.createPreparer(withLayer.data());
    const PDFOCRPageAnalysis own = layerPreparer.analyze(0, nullptr);
    QVERIFY(own.hasOwnOCRLayer);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(own, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::Skip);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(own, PDFOCRExistingTextPolicy::ReplaceOwnLayer, false, &reason), Decision::Recognize);

    // Fingerprint of the document is stable when the own layer is added (EXPORT-04)
    QCOMPARE(PDFOCRPagePreparer::computeDocumentFingerprint(&document), PDFOCRPagePreparer::computeDocumentFingerprint(withLayer.data()));

    // Page labels
    QCOMPARE(PDFOCRPagePreparer::getPageLabel(&document, 0), QString());
}

// -------------------------------------------------------------------------
// AT-19: annotations and redactions
// -------------------------------------------------------------------------

void OCRTest::annotationsAndRedactions()
{
    PageSpec spec;
    spec.size = QSizeF(300, 200);
    spec.content = "0 0 0 rg 20 120 100 30 re f 0 0 0 rg 160 120 100 30 re f";
    PDFDocument baseDocument = createDocument({ spec });

    // Unapplied redaction annotation over the second rectangle
    PDFDocumentBuilder builder(&baseDocument);
    builder.createAnnotationRedact(builder.getPages().front(), QRectF(150, 110, 120, 50), Qt::black, Qt::black);
    PDFDocument document = builder.build();

    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    const PDFOCRPageAnalysis analysis = preparer.analyze(0, nullptr);
    QVERIFY(analysis.hasAnnotations);
    QVERIFY(analysis.hasUnappliedRedactions);
    QCOMPARE(analysis.redactionRectangles.size(), size_t(1));
    QVERIFY(!analysis.notes.isEmpty());

    // The area of the redaction is masked in the working raster, so the hidden content is not read (PDF-13)
    std::vector<QRectF> masks = analysis.annotationRectangles;
    masks.insert(masks.end(), analysis.redactionRectangles.begin(), analysis.redactionRectangles.end());
    PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 150.0, masks, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    QVERIFY(!raster.error);
    const QRect darkBox = findDarkBoundingBox(raster.image);
    const QRectF darkPageBox = raster.geometry.pageToRaster.inverted().mapRect(QRectF(darkBox));
    QVERIFY2(darkPageBox.right() < 125.0, qPrintable(QString::number(darkPageBox.right())));
    QVERIFY(darkPageBox.left() > 15.0);

    // Annotations are never part of the working raster (IMAGE-03)
    PDFOCRPagePreparer::RasterResult unmasked = preparer.rasterize(0, 150.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    const QRectF unmaskedBox = unmasked.geometry.pageToRaster.inverted().mapRect(QRectF(findDarkBoundingBox(unmasked.image)));
    QVERIFY(unmaskedBox.right() > 255.0 && unmaskedBox.right() < 265.0);

    // A result reaching into the excluded area is flagged and not written automatically (REGION-05)
    PDFOCRRecognitionOutput output;
    PDFOCRRawBlock block;
    PDFOCRRawLine line;
    PDFOCRRawWord inside;
    inside.text = QStringLiteral("visible");
    inside.rect = raster.geometry.pageToRaster.mapRect(QRectF(20, 120, 100, 30));
    PDFOCRRawWord overlapping;
    overlapping.text = QStringLiteral("secret");
    overlapping.rect = raster.geometry.pageToRaster.mapRect(QRectF(140, 120, 100, 30));
    line.rect = inside.rect.united(overlapping.rect);
    line.words = { inside, overlapping };
    block.rect = line.rect;
    block.lines.push_back(line);
    output.blocks.push_back(block);

    PDFOCRPageResult result;
    result.pageIndex = 0;
    result.state = PDFOCRPageState::Done;
    PDFOCRPagePreparer::appendOutput(result, output, raster.geometry, -1, masks);
    QCOMPARE(result.getWordCount(), 2);
    QVERIFY(!result.getWords()[0]->overlapsExcludedRegion);
    QVERIFY(result.getWords()[1]->overlapsExcludedRegion);
    QVERIFY(PDFOCRReview::requiresReview(*result.getWords()[1], 0.0));

    int writtenWords = 0;
    QStringList warnings;
    const QByteArray content = PDFOCRTextLayerWriter::createContentStream(result, "F", false, &writtenWords, &warnings);
    QCOMPARE(writtenWords, 1);
    QCOMPARE(warnings.size(), 1);
    QVERIFY(!content.contains(PDFOCRTextLayerWriter::encodeText(QStringLiteral("secret"), nullptr)));

    // Tagged documents get the layer as an artifact (PDF-14)
    const QByteArray artifactContent = PDFOCRTextLayerWriter::createContentStream(result, "F", false, &writtenWords, nullptr, true);
    QVERIFY(artifactContent.contains("/Artifact BMC"));
    QVERIFY(artifactContent.contains("EMC"));

    // Regions created from the detected blocks (REGION-02)
    PDFOCRSession session(nullptr);
    session.setPageResult(result);
    QCOMPARE(session.createRegionsFromBlocks(0), 1);
    QCOMPARE(session.getPage(0)->regions.size(), size_t(1));
    QVERIFY(session.getPage(0)->regions[0].proposedByAnalysis);
    QCOMPARE(session.getPage(0)->blocks[0].regionId, session.getPage(0)->regions[0].id);
    session.undo();
    QVERIFY(session.getPage(0)->regions.empty());
}

// -------------------------------------------------------------------------
// AT-14: cancellation
// -------------------------------------------------------------------------

void OCRTest::jobCancellation()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec, spec, spec, spec });
    RenderingContext context(&document);

    m_testEngine->setRecognitionDelay(3000);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        PDFOCRRawBlock block;
        block.rect = QRectF(0, 0, 100, 20);
        PDFOCRRawLine line;
        line.rect = QRectF(0, 0, 100, 20);
        line.text = QStringLiteral("late result");
        block.lines.push_back(line);
        output.blocks.push_back(block);
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    description.configuration.languages = { QStringLiteral("eng") };
    description.configuration.workerCount = 1;
    description.configuration.detectBlankPages = false;
    description.models.dataPath = QStringLiteral("/none");
    description.models.languages = { QStringLiteral("eng") };
    for (PDFInteger i = 0; i < 4; ++i)
    {
        PDFOCRPageTask task;
        task.pageIndex = i;
        task.configuration = description.configuration;
        task.generation = 1;
        description.pages.push_back(task);
    }

    std::vector<PDFOCRPageResult> results;
    std::optional<PDFOCRJobSummary> summary;
    int currentGeneration = 0;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int generation, PDFOCRPageResult result)
    {
        if (generation != currentGeneration)
        {
            // Late callback of a different run is ignored (JOB-02)
            return;
        }
        results.push_back(std::move(result));
    });
    connect(&controller, &PDFOCRJobController::jobFinished, this, [&](int generation, PDFOCRJobSummary jobSummary)
    {
        if (generation == currentGeneration)
        {
            summary = jobSummary;
        }
    });

    QVERIFY(controller.start(description, &currentGeneration));
    QVERIFY(controller.isRunning());
    QTest::qWait(300);

    // Stop returns immediately (JOB-06)
    QElapsedTimer timer;
    timer.start();
    controller.stop();
    QVERIFY(timer.elapsed() < 200);
    QVERIFY(controller.isStopping());

    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 10000);
    QVERIFY(timer.elapsed() < 5000);
    QVERIFY(summary->cancelled);
    QCOMPARE(int(results.size()), 4);
    for (const PDFOCRPageResult& result : results)
    {
        QCOMPARE(result.state, PDFOCRPageState::Cancelled);
        QVERIFY(!result.hasResult());
    }
    QVERIFY(!controller.isRunning());

    // A run which finishes normally delivers valid results
    m_testEngine->setRecognitionDelay(0);
    results.clear();
    summary.reset();
    description.pages.resize(1);
    QVERIFY(controller.start(description, &currentGeneration));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 10000);
    QCOMPARE(results.size(), size_t(1));
    QCOMPARE(results[0].state, PDFOCRPageState::Done);
    QCOMPARE(results[0].getWords()[0]->text, QStringLiteral("late result"));
    QVERIFY(!results[0].getWords()[0]->confidence.isAvailable());
    QCOMPARE(results[0].provenance.engineId, QStringLiteral("test"));
    controller.waitForFinished();
}

// -------------------------------------------------------------------------
// AT-15: page error and retry
// -------------------------------------------------------------------------

void OCRTest::pageErrorAndRetry()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec, spec, spec });
    RenderingContext context(&document);

    std::atomic<bool> failSecondPage = { true };
    std::atomic<int> callCount = { 0 };
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([&](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        const int call = ++callCount;
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        if (failSecondPage && input.configuration.engineParameters.value(QStringLiteral("page")).toInt() == 1)
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::WorkerCrashed, QStringLiteral("Simulated failure %1").arg(call), QStringLiteral("Recognition"));
            return output;
        }

        PDFOCRRawBlock block;
        block.rect = QRectF(0, 0, 100, 20);
        PDFOCRRawLine line;
        line.rect = QRectF(0, 0, 100, 20);
        PDFOCRRawWord word;
        word.text = QStringLiteral("ok");
        word.rect = QRectF(0, 0, 100, 20);
        word.rawConfidence = 90.0;
        line.words.push_back(word);
        block.lines.push_back(line);
        output.blocks.push_back(block);
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRSession session(nullptr);
    session.setDocument(&document, PDFOCRDocumentIdentity());
    connect(&controller, &PDFOCRJobController::pageFinished, &session, [&session](int, PDFOCRPageResult result) { session.setPageResult(std::move(result)); });

    std::optional<PDFOCRJobSummary> summary;
    connect(&controller, &PDFOCRJobController::jobFinished, this, [&](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });

    auto createDescription = [](const std::vector<PDFInteger>& pages)
    {
        PDFOCRJobDescription description;
        description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
        description.configuration.languages = { QStringLiteral("eng") };
        description.configuration.workerCount = 2;
        description.configuration.detectBlankPages = false;
        description.models.dataPath = QStringLiteral("/none");
        description.models.languages = { QStringLiteral("eng") };
        for (PDFInteger page : pages)
        {
            PDFOCRPageTask task;
            task.pageIndex = page;
            task.configuration = description.configuration;
            task.configuration.engineParameters[QStringLiteral("page")] = int(page);
            task.generation = 1;
            description.pages.push_back(task);
        }
        return description;
    };

    int generation = 0;
    QVERIFY(controller.start(createDescription({ 0, 1, 2 }), &generation));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 10000);
    controller.waitForFinished();

    QCOMPARE(summary->donePages, 2);
    QCOMPARE(summary->errorPages, 1);
    QVERIFY(summary->isPartial());
    QCOMPARE(session.getPage(0)->state, PDFOCRPageState::Done);
    QCOMPARE(session.getPage(1)->state, PDFOCRPageState::Error);
    QCOMPARE(session.getPage(1)->error.code, PDFOCRErrorCode::WorkerCrashed);
    QCOMPARE(session.getPage(2)->state, PDFOCRPageState::Done);

    // Retry of the failed page only (JOB-07)
    failSecondPage = false;
    summary.reset();
    const int callsBefore = callCount;
    QVERIFY(controller.start(createDescription({ 1 }), &generation));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 10000);
    controller.waitForFinished();
    QCOMPARE(callCount - callsBefore, 1);
    QCOMPARE(session.getPage(1)->state, PDFOCRPageState::Done);
    QCOMPARE(session.getPage(0)->state, PDFOCRPageState::Done);
    QVERIFY(!summary->isPartial());
}

// -------------------------------------------------------------------------
// AT-17: project
// -------------------------------------------------------------------------

void OCRTest::projectRoundTrip()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec, spec });

    PDFOCRDocumentIdentity identity;
    identity.fileName = QStringLiteral("test.pdf");
    identity.pageCount = 2;
    identity.fingerprint = PDFOCRPagePreparer::computeDocumentFingerprint(&document);

    PDFOCRSession session(nullptr);
    session.setDocument(&document, identity);
    PDFOCRConfiguration configuration;
    configuration.languages = { QStringLiteral("ces"), QStringLiteral("eng") };
    configuration.dpi = 400.0;
    configuration.userWords = { QStringLiteral("PDF4QT") };
    session.setConfiguration(configuration);

    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(20, 100, 50, 30) }, { QStringLiteral("world"), QRectF(80, 100, 40, 30) } });
    result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, 0);
    result.provenance.modelIds = { QStringLiteral("tesseract/fast/ces") };
    result.provenance.modelSetHash = QStringLiteral("abc");
    result.geometry.dpi = 400.0;
    result.geometry.pageToRaster = QTransform(5.5, 0, 0, -5.5, 0, 1100);
    result.orientation = PDFOCROrientation{ 90, 12.5, 0.0, std::nullopt, QStringLiteral("Latin") };
    PDFOCRRegion region;
    region.rect = QRectF(0, 0, 100, 100);
    region.type = PDFOCRRegionType::Exclude;
    region.name = QStringLiteral("logo");
    result.regions.push_back(region);
    result.assignIdentifiers();
    session.setPageResult(result);

    const int wordId = session.getPage(0)->getWords()[0]->id;
    QVERIFY(session.setWordText(0, wordId, QStringLiteral("Hallo")));
    QVERIFY(session.setWordReviewState(0, session.getPage(0)->getWords()[1]->id, PDFOCRReviewState::Confirmed));

    PDFOCRPageOverride pageOverride;
    pageOverride.dpi = 600.0;
    session.setPageOverride(1, pageOverride);

    PDFOCRProject project = session.createProject({ 0, 1 });
    const QByteArray bytes = PDFOCRProjectSerializer::toBytes(project);
    QVERIFY(!bytes.contains("\"password\""));

    PDFOCRProject loaded;
    QString error;
    QVERIFY2(PDFOCRProjectSerializer::fromBytes(bytes, loaded, &error), qPrintable(error));
    QCOMPARE(loaded.configuration, configuration);
    QCOMPARE(loaded.document, identity);
    QCOMPARE(loaded.pages.size(), size_t(1));
    QCOMPARE(loaded.pageOverrides.at(1).dpi.value(), 600.0);
    QCOMPARE(loaded.selectedPages, (std::vector<PDFInteger>{ 0, 1 }));

    const PDFOCRPageResult& loadedPage = loaded.pages.at(0);
    const PDFOCRPageResult* originalPage = session.getPage(0);
    QCOMPARE(loadedPage.getWords()[0]->text, QStringLiteral("Hallo"));
    QCOMPARE(loadedPage.getWords()[0]->originalText, QStringLiteral("Hello"));
    QCOMPARE(loadedPage.getWords()[0]->reviewState, PDFOCRReviewState::Modified);
    QCOMPARE(loadedPage.getWords()[1]->reviewState, PDFOCRReviewState::Confirmed);
    QCOMPARE(loadedPage.getWords()[0]->confidence, originalPage->getWords()[0]->confidence);
    QCOMPARE(loadedPage.getWords()[0]->quad, originalPage->getWords()[0]->quad);
    QCOMPARE(loadedPage.regions.size(), size_t(1));
    QCOMPARE(loadedPage.regions[0].name, QStringLiteral("logo"));
    QCOMPARE(loadedPage.provenance.modelIds, originalPage->provenance.modelIds);
    QCOMPARE(loadedPage.geometry.pageToRaster, originalPage->geometry.pageToRaster);
    QCOMPARE(loadedPage.orientation->rotation, 90);
    QCOMPARE(loadedPage.pageFingerprint, originalPage->pageFingerprint);
    QCOMPARE(loadedPage.state, PDFOCRPageState::Done);

    // Save and load through a file
    QTemporaryDir directory;
    const QString fileName = directory.filePath(QStringLiteral("test.pdf4qt-ocr"));
    QVERIFY(PDFOCRProjectSerializer::save(project, fileName, &error));
    PDFOCRProject loadedFromFile;
    QVERIFY(PDFOCRProjectSerializer::load(fileName, loadedFromFile, &error));
    QCOMPARE(PDFOCRProjectSerializer::toBytes(loadedFromFile), bytes);

    // Match with the unchanged document
    PDFOCRProjectSerializer::MatchResult match = PDFOCRProjectSerializer::match(loaded, identity, [&document](PDFInteger page) { return PDFOCRPagePreparer::computePageFingerprint(&document, page); });
    QVERIFY(match.documentMatches);
    QCOMPARE(match.matchingPages, (std::vector<PDFInteger>{ 0 }));

    // Changed document: page fingerprint differs, automatic application is blocked (EXPORT-04)
    PageSpec changed;
    changed.content = "0 0 0 rg 25 100 100 30 re f";
    PDFDocument changedDocument = createDocument({ changed, spec });
    PDFOCRDocumentIdentity changedIdentity = identity;
    changedIdentity.fingerprint = PDFOCRPagePreparer::computeDocumentFingerprint(&changedDocument);
    match = PDFOCRProjectSerializer::match(loaded, changedIdentity, [&changedDocument](PDFInteger page) { return PDFOCRPagePreparer::computePageFingerprint(&changedDocument, page); });
    QVERIFY(!match.documentMatches);
    QCOMPARE(match.changedPages, (std::vector<PDFInteger>{ 0 }));

    // Invalid inputs
    QVERIFY(!PDFOCRProjectSerializer::fromBytes("{\"format\":\"other\"}", loaded, &error));
    QVERIFY(!PDFOCRProjectSerializer::fromBytes("not json", loaded, &error));
}

// -------------------------------------------------------------------------
// AT-18: text export
// -------------------------------------------------------------------------

void OCRTest::textExport()
{
    PDFOCRSession session(nullptr);
    PDFOCRPageResult page0 = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) }, { QStringLiteral("world"), QRectF(70, 100, 50, 12) } });
    page0.pageLabel = QStringLiteral("iv");

    // Second block: two lines, the first ends with a hyphen
    PDFOCRBlock block;
    block.id = page0.allocateId();
    PDFOCRLine line1;
    line1.id = page0.allocateId();
    line1.words.push_back(makeWord(page0, QStringLiteral("recog-"), QRectF(10, 80, 50, 12), 90.0));
    PDFOCRLine line2;
    line2.id = page0.allocateId();
    line2.words.push_back(makeWord(page0, QStringLiteral("nition"), QRectF(10, 60, 50, 12), 90.0));
    line2.words.push_back(makeWord(page0, QStringLiteral("done"), QRectF(70, 60, 50, 12), 90.0));
    block.lines.push_back(line1);
    block.lines.push_back(line2);
    page0.blocks.push_back(block);
    session.setPageResult(page0);

    PDFOCRPageResult page2 = createSampleResult(2, { { QStringLiteral("Third"), QRectF(10, 100, 50, 12) } });
    session.setPageResult(page2);
    session.getOrCreatePage(1);   // page without result

    const int wordId = session.getPage(0)->getWords()[0]->id;
    QVERIFY(session.setWordText(0, wordId, QStringLiteral("Corrected")));
    QVERIFY(session.setWordReviewState(0, session.getPage(0)->getWords()[1]->id, PDFOCRReviewState::Discarded));

    PDFOCRTextExporter::Options options;
    PDFOCRTextExporter::Report report;
    const QString text = PDFOCRTextExporter::exportText(session.getResults({ 0, 1, 2 }), options, &report);

    QVERIFY(text.contains(QStringLiteral("Corrected")));
    QVERIFY(!text.contains(QStringLiteral("Hello")));
    QVERIFY(!text.contains(QStringLiteral("world")));
    QVERIFY(text.contains(QStringLiteral("recog-\nnition done")));
    QVERIFY(text.indexOf(QStringLiteral("Corrected")) < text.indexOf(QStringLiteral("Third")));
    QVERIFY(text.contains(QStringLiteral("Page 1 (iv)")));
    QCOMPARE(report.exportedPages, (std::vector<PDFInteger>{ 0, 2 }));
    QCOMPARE(report.skippedPages, (std::vector<PDFInteger>{ 1 }));

    // Optional joining of hyphenated words
    options.joinHyphenatedWords = true;
    options.pageSeparator = PDFOCRTextExporter::PageSeparator::FormFeed;
    const QString joined = PDFOCRTextExporter::exportText(session.getResults({ 0, 2 }), options, nullptr);
    QVERIFY2(joined.contains(QStringLiteral("recognition\ndone")), qPrintable(joined));
    QVERIFY(joined.contains(QChar(0x0C)));

    // Block order change is reflected in the export (EDIT-09)
    QVERIFY(session.moveBlock(0, block.id, 0));
    const QString reordered = PDFOCRTextExporter::exportText(session.getResults({ 0 }), PDFOCRTextExporter::Options(), nullptr);
    QVERIFY(reordered.indexOf(QStringLiteral("recog-")) < reordered.indexOf(QStringLiteral("Corrected")));

    QTemporaryDir directory;
    QString error;
    QVERIFY(PDFOCRTextExporter::writeTextFile(directory.filePath(QStringLiteral("out.txt")), text, &error));
    QFile file(directory.filePath(QStringLiteral("out.txt")));
    QVERIFY(file.open(QFile::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), text);
}

// -------------------------------------------------------------------------
// AT-21: generic adapter (line text, polygons, unknown confidence)
// -------------------------------------------------------------------------

void OCRTest::genericAdapter()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 200 30 re f";
    PDFDocument document = createDocument({ spec });
    RenderingContext context(&document);

    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Unknown;

        // Line with a polygon (slightly rotated), no words, no confidence
        const double scale = input.dpi / 72.0;
        PDFOCRRawBlock block;
        PDFOCRRawLine line;
        const QPointF bottomLeft(20 * scale, (200 - 100) * scale);
        const QPointF bottomRight(220 * scale, (200 - 102) * scale);
        const QPointF topRight(220 * scale, (200 - 132) * scale);
        const QPointF topLeft(20 * scale, (200 - 130) * scale);
        line.polygon << bottomLeft << bottomRight << topRight << topLeft;
        line.rect = line.polygon.boundingRect();
        line.text = QStringLiteral("generic line of text");
        block.lines.push_back(line);
        block.rect = line.rect;
        output.blocks.push_back(block);
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    description.configuration.languages = { QStringLiteral("eng") };
    description.configuration.workerCount = 1;
    description.configuration.detectBlankPages = false;
    description.models.dataPath = QStringLiteral("/none");
    description.models.languages = { QStringLiteral("eng") };
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = description.configuration;
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRPageResult> result;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = pageResult; });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000);
    controller.waitForFinished();

    QCOMPARE(result->state, PDFOCRPageState::Done);
    QCOMPARE(result->getWordCount(), 1);
    const PDFOCRWord* segment = result->getWords()[0];
    QCOMPARE(segment->text, QStringLiteral("generic line of text"));
    QVERIFY(!segment->confidence.isAvailable());
    QVERIFY(segment->quad.isValid());
    QVERIFY(qAbs(segment->quad.points[0].x() - 20.0) < 0.5);
    QVERIFY(qAbs(segment->quad.points[0].y() - 100.0) < 0.5);
    QVERIFY(qAbs(segment->quad.points[1].y() - 102.0) < 0.5);

    // Corrections and the writer work without engine specific data
    PDFOCRSession session(nullptr);
    session.setPageResult(*result);
    QVERIFY(session.setWordText(0, segment->id, QStringLiteral("generic corrected text")));
    const PDFOCRConfidenceStatistics statistics = session.getStatistics(0);
    QCOMPARE(statistics.unknownWordCount, 1);
    QVERIFY(!statistics.meanScore.has_value());
    QCOMPARE(statistics.reviewRequiredCount, 1);

    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer modified = applyResults(document, { *session.getPage(0) }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(modified, qPrintable(report.error.message));
    QVERIFY(extractText(*modified, 0).contains(QStringLiteral("generic corrected text")));
}

// -------------------------------------------------------------------------
// AT-24: invalid engine output
// -------------------------------------------------------------------------

void OCRTest::invalidEngineOutput()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 200 30 re f";
    PDFDocument document = createDocument({ spec });
    RenderingContext context(&document);

    enum class Failure { NaN, Huge, LongText, WrongImage, Crash, ThrowStandard, ThrowPdf, None };
    std::atomic<int> failure = { 0 };

    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([&failure](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        PDFOCRRawBlock block;
        PDFOCRRawLine line;
        PDFOCRRawWord word;
        word.text = QStringLiteral("word");
        word.rect = QRectF(10, 10, 100, 20);

        switch (static_cast<Failure>(failure.load()))
        {
            case Failure::NaN:
                word.rect = QRectF(std::numeric_limits<double>::quiet_NaN(), 10, 100, 20);
                break;
            case Failure::Huge:
                word.rect = QRectF(1e12, 10, 100, 20);
                break;
            case Failure::LongText:
                word.text = QString(10000, QChar('a'));
                break;
            case Failure::WrongImage:
                output.imageSize = QSize(1, 1);
                break;
            case Failure::Crash:
                output.error = PDFOCRError::create(PDFOCRErrorCode::WorkerCrashed, QStringLiteral("worker crashed"));
                return output;
            case Failure::ThrowStandard:
                throw std::runtime_error("engine failure");
            case Failure::ThrowPdf:
                throw PDFException(QStringLiteral("engine failure"));
            case Failure::None:
                break;
        }

        line.rect = word.rect;
        line.words.push_back(word);
        block.lines.push_back(line);
        block.rect = line.rect;
        output.blocks.push_back(block);
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    // Exception thrown by the engine must end as an error of the page, and the
    // worker must stay usable for the following pages (the last run succeeds).
    for (int f = 0; f <= int(Failure::None); ++f)
    {
        failure = f;

        PDFOCRJobDescription description;
        description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
        description.configuration.languages = { QStringLiteral("eng") };
        description.configuration.workerCount = 1;
        description.configuration.detectBlankPages = false;
        description.models.dataPath = QStringLiteral("/none");
        description.models.languages = { QStringLiteral("eng") };
        PDFOCRPageTask task;
        task.pageIndex = 0;
        task.configuration = description.configuration;
        description.pages.push_back(task);

        std::optional<PDFOCRPageResult> result;
        QMetaObject::Connection connection = connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = pageResult; });
        int generation = 0;
        QVERIFY(controller.start(description, &generation));
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000);
        controller.waitForFinished();
        disconnect(connection);

        QVERIFY(!controller.isRunning());

        if (static_cast<Failure>(f) == Failure::None)
        {
            QCOMPARE(result->state, PDFOCRPageState::Done);
            QVERIFY(result->hasResult());
            continue;
        }

        const bool isCrash = f >= int(Failure::Crash);
        QCOMPARE(result->state, PDFOCRPageState::Error);
        QVERIFY2(result->error.code == (isCrash ? PDFOCRErrorCode::WorkerCrashed : PDFOCRErrorCode::InvalidEngineOutput), qPrintable(result->error.message));
        QVERIFY(!result->hasResult());
    }

    // Invalid results are rejected by the writer, the document is not modified (DATA-03)
    PDFOCRPageResult invalid = createSampleResult(0, { { QStringLiteral("word"), QRectF(10, 10, 100, 20) } });
    invalid.getWords()[0]->quad.points[0] = QPointF(std::numeric_limits<double>::infinity(), 0);
    PDFOCRTextLayerWriter::Report report;
    QVERIFY(!applyResults(document, { invalid }, PDFOCRTextLayerWriter::Options(), &report));
    QVERIFY(report.error);
    QCOMPARE(report.error.code, PDFOCRErrorCode::WriteFailed);

    // Text without geometry cannot be written (EDIT-03)
    PDFOCRPageResult noGeometry = createSampleResult(0, { { QStringLiteral("word"), QRectF(10, 10, 100, 20) } });
    noGeometry.getWords()[0]->quad = PDFOCRQuad();
    QVERIFY(!applyResults(document, { noGeometry }, PDFOCRTextLayerWriter::Options(), &report));
}

// -------------------------------------------------------------------------
// AT-02 / AT-22: built-in models
// -------------------------------------------------------------------------

static QString getSourceOcrDirectory()
{
    const QByteArray environmentDirectory = qgetenv("PDF4QT_OCR_DATA_DIRECTORY");
    if (!environmentDirectory.isEmpty())
    {
        return QString::fromLocal8Bit(environmentDirectory);
    }

#ifdef PDF4QT_OCR_SOURCE_DATA_DIRECTORY
    return QStringLiteral(PDF4QT_OCR_SOURCE_DATA_DIRECTORY);
#else
    return QString();
#endif
}

void OCRTest::modelManagerBuiltIn()
{
    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        QSKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    QTemporaryDir userDirectory;
    PDFOCRModelManager manager(nullptr);
    manager.setModelValidator([](const QString&, const QString&, const QString&) { return PDFOCRError::none(); });
    manager.setBuiltInDirectory(builtInDirectory);
    manager.setUserDirectory(userDirectory.path());
    manager.loadBundledCatalog();

    QVERIFY(manager.getCatalog().isValid());
    QVERIFY(manager.getCatalog().entries.size() > 100);

    // AT-02: the built-in languages are physically available (LANG-01, LANG-02)
    for (const char* language : { "ces", "eng", "slk", "deu", "spa", "rus", "chi_sim", "chi_tra" })
    {
        QVERIFY2(manager.isLanguageUsable(QStringLiteral("tesseract"), QLatin1String(language), PDFOCRModelProfile::Fast), language);
        std::optional<PDFOCRModelInfo> model = manager.getModel(QStringLiteral("tesseract/fast/") + QLatin1String(language));
        QVERIFY(model.has_value());
        QCOMPARE(model->origin, PDFOCRModelOrigin::BuiltIn);
        QCOMPARE(model->state, PDFOCRModelState::BuiltIn);
        QVERIFY(model->catalogVerified);
        QVERIFY(QFile::exists(model->path));
    }
    QVERIFY(manager.isOrientationDataUsable(QStringLiteral("tesseract"), PDFOCRModelProfile::Fast));
    QVERIFY(!manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("fra"), PDFOCRModelProfile::Fast));
    QVERIFY(!manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("ces"), PDFOCRModelProfile::Best));
    QVERIFY(!manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("ces"), PDFOCRModelProfile::Standard));

    // Only the profile Fast has built-in models in the distribution. The other profiles are checked,
    // when a developer has placed their English model (generate_catalog.py --builtin-best eng --update-builtin).
    QStringList englishChecksums;
    for (const PDFOCRModelProfile profile : PDFOCRConfiguration::getProfiles())
    {
        const QString profileId = PDFOCRConfiguration::getProfileIdentifier(profile);
        if (!QFile::exists(builtInDirectory + QStringLiteral("/tesseract/%1/tessdata/eng.traineddata").arg(profileId)))
        {
            QVERIFY(profile != PDFOCRModelProfile::Fast);
            QVERIFY(!manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("eng"), profile));
            QCOMPARE(manager.getMissingModels(QStringLiteral("tesseract"), { QStringLiteral("eng") }, profile), QStringList{ QStringLiteral("tesseract/%1/eng").arg(profileId) });
            continue;
        }

        QVERIFY2(manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("eng"), profile), qPrintable(profileId));
        QVERIFY(manager.isOrientationDataUsable(QStringLiteral("tesseract"), profile));
        std::optional<PDFOCRModelInfo> english = manager.getModel(QStringLiteral("tesseract/%1/eng").arg(profileId));
        QVERIFY(english.has_value());
        QCOMPARE(english->origin, PDFOCRModelOrigin::BuiltIn);
        QCOMPARE(english->profile, profile);
        QVERIFY2(english->catalogVerified, qPrintable(profileId));

        PDFOCRError resolveError;
        const PDFOCRResolvedModelSet englishSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("eng") }, profile, &resolveError);
        QVERIFY2(englishSet.isValid(), qPrintable(resolveError.message));
        QCOMPARE(englishSet.profile, profile);
        QVERIFY(!englishChecksums.contains(english->sha256));
        englishChecksums << english->sha256;
    }

    QCOMPARE(PDFOCRConfiguration::parseProfileIdentifier(QStringLiteral("standard")), PDFOCRModelProfile::Standard);
    QCOMPARE(PDFOCRConfiguration::parseProfileIdentifier(QStringLiteral("unknown")), PDFOCRModelProfile::Fast);
    QCOMPARE(manager.getCatalog().getSetId(PDFOCRModelProfile::Standard).left(18), QStringLiteral("tessdata_standard-"));
    QCOMPARE(manager.getMissingModels(QStringLiteral("tesseract"), { QStringLiteral("ces"), QStringLiteral("fra") }, PDFOCRModelProfile::Fast), QStringList{ QStringLiteral("tesseract/fast/fra") });

    // Display text (LANG-03)
    std::optional<PDFOCRModelInfo> czech = manager.getModel(QStringLiteral("tesseract/fast/ces"));
    QVERIFY(PDFOCRModelManager::getModelDisplayText(*czech).startsWith(QStringLiteral("Czech (ces)")));

    // Orientation data are not offered as a document language
    for (const PDFOCRModelInfo& model : manager.getUsableLanguageModels(QStringLiteral("tesseract"), PDFOCRModelProfile::Fast, true))
    {
        QVERIFY(model.language != QStringLiteral("osd"));
    }

    // Unified runtime set (LANG-06): the set contains all languages and osd
    PDFOCRError error;
    PDFOCRResolvedModelSet set = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("ces"), QStringLiteral("eng") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(!error, qPrintable(error.message));
    QVERIFY(set.isValid());
    QCOMPARE(set.languages, (QStringList{ QStringLiteral("ces"), QStringLiteral("eng") }));
    QVERIFY(set.hasOrientationData);
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/ces.traineddata")));
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/eng.traineddata")));
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/osd.traineddata")));
    QVERIFY(set.dataPath.startsWith(QDir(userDirectory.path()).absolutePath()));
    QVERIFY(!set.hash.isEmpty());

    // Repeated resolution reuses the set
    const PDFOCRResolvedModelSet again = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("ces"), QStringLiteral("eng") }, PDFOCRModelProfile::Fast, &error);
    QCOMPARE(again.dataPath, set.dataPath);
    QCOMPARE(again.hash, set.hash);

    // Missing model is never replaced silently (LANG-13)
    const PDFOCRResolvedModelSet missing = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("ces"), QStringLiteral("fra") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY(!missing.isValid());
    QCOMPARE(error.code, PDFOCRErrorCode::MissingModel);

    // Built-in models cannot be removed, only hidden (LANG-12)
    QVERIFY(manager.removeUserModel(QStringLiteral("tesseract/fast/ces")));
    manager.setModelHidden(QStringLiteral("tesseract/fast/slk"), true);
    QVERIFY(manager.getModel(QStringLiteral("tesseract/fast/slk"))->isHidden);
    bool slovakOffered = false;
    for (const PDFOCRModelInfo& model : manager.getUsableLanguageModels(QStringLiteral("tesseract"), PDFOCRModelProfile::Fast, false))
    {
        slovakOffered = slovakOffered || model.language == QStringLiteral("slk");
    }
    QVERIFY(!slovakOffered);
    manager.setModelHidden(QStringLiteral("tesseract/fast/slk"), false);

    // Import of a local model file (LANG-13): origin is not verified by the catalog
    const QString importSource = builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/deu.traineddata");
    QString importedId;
    error = manager.importModel(importSource, QStringLiteral("tesseract"), PDFOCRModelProfile::Best, &importedId);
    QVERIFY2(!error, qPrintable(error.message));
    std::optional<PDFOCRModelInfo> imported = manager.getModel(importedId);
    QVERIFY(imported.has_value());
    QCOMPARE(imported->origin, PDFOCRModelOrigin::Imported);
    QVERIFY(!imported->catalogVerified);
    QCOMPARE(imported->profile, PDFOCRModelProfile::Best);
    QVERIFY(imported->language.startsWith(QStringLiteral("deu@")));

    // The import does not override the built-in model by the name (LANG-05)
    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/deu"))->origin, PDFOCRModelOrigin::BuiltIn);
    const PDFOCRResolvedModelSet importedSet = manager.resolveModelSet(QStringLiteral("tesseract"), { imported->language }, PDFOCRModelProfile::Best, &error);
    QVERIFY2(importedSet.isValid(), qPrintable(error.message));
    QCOMPARE(importedSet.languages, QStringList{ QStringLiteral("deu") });

    // Removal of the user model
    QVERIFY(!manager.removeUserModel(importedId));
    QVERIFY(!manager.getModel(importedId).has_value());

    // Cache cleanup keeps the models (OPS-06)
    QVERIFY(!manager.cleanRuntimeSets());
    QVERIFY(manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("ces"), PDFOCRModelProfile::Fast));
}

// -------------------------------------------------------------------------
// AT-03 / AT-04: download
// -------------------------------------------------------------------------

void OCRTest::modelDownload()
{
    TestHttpServer server;

    const QByteArray goodModel = QByteArray("TESSDATA-TEST-MODEL-") + QByteArray(5000, 'x');
    const QByteArray goodHash = QCryptographicHash::hash(goodModel, QCryptographicHash::Sha256).toHex();
    const QByteArray updatedModel = QByteArray("TESSDATA-TEST-MODEL-V2-") + QByteArray(6000, 'y');
    const QByteArray updatedHash = QCryptographicHash::hash(updatedModel, QCryptographicHash::Sha256).toHex();

    server.setResponse(QStringLiteral("/fra.traineddata"), { 200, "application/octet-stream", goodModel, false });
    server.setResponse(QStringLiteral("/bad.traineddata"), { 200, "application/octet-stream", QByteArray("corrupted"), false });
    server.setResponse(QStringLiteral("/html.traineddata"), { 200, "text/html", "<html><body>Login page</body></html>", false });
    server.setResponse(QStringLiteral("/drop.traineddata"), { 200, "application/octet-stream", QByteArray(), true });
    server.setResponse(QStringLiteral("/missing.traineddata"), { 404, "text/plain", "not here", false });
    server.setResponse(QStringLiteral("/Arabic.traineddata"), { 200, "application/octet-stream", goodModel, false });

    auto entry = [&server](const QString& id, const QString& language, const QString& path, const QByteArray& body, const QByteArray& hash)
    {
        PDFOCRCatalogEntry catalogEntry;
        catalogEntry.id = id;
        catalogEntry.engineId = QStringLiteral("tesseract");
        catalogEntry.language = language;
        catalogEntry.name = language;
        catalogEntry.profile = PDFOCRModelProfile::Fast;
        catalogEntry.family = QStringLiteral("language");
        catalogEntry.version = QStringLiteral("v1");
        catalogEntry.url = server.url(path);
        catalogEntry.fileName = language + QStringLiteral(".traineddata");
        catalogEntry.size = body.size();
        catalogEntry.sha256 = QString::fromLatin1(hash);
        catalogEntry.license = QStringLiteral("Apache-2.0");
        return catalogEntry;
    };

    PDFOCRCatalog catalog;
    catalog.version = 1;
    catalog.engineId = QStringLiteral("tesseract");
    catalog.sourceCommits[QStringLiteral("fast")] = QStringLiteral("0123456789abcdef");
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/fra"), QStringLiteral("fra"), QStringLiteral("/fra.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/bad"), QStringLiteral("bad"), QStringLiteral("/bad.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/html"), QStringLiteral("html"), QStringLiteral("/html.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/drop"), QStringLiteral("drop"), QStringLiteral("/drop.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/missing"), QStringLiteral("missing"), QStringLiteral("/missing.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/insecure"), QStringLiteral("insecure"), QStringLiteral("/fra.traineddata"), goodModel, goodHash));
    catalog.entries.back().url = QStringLiteral("http://example.invalid/fra.traineddata");
    catalog.entries.push_back(entry(QStringLiteral("tesseract/fast/script/Arabic"), QStringLiteral("script/Arabic"), QStringLiteral("/Arabic.traineddata"), goodModel, goodHash));
    catalog.entries.back().family = QStringLiteral("script");

    auto testValidator = [](const QString&, const QString& dataPath, const QString& language)
    {
        QFile file(dataPath + QStringLiteral("/") + language + QStringLiteral(".traineddata"));
        if (!file.open(QFile::ReadOnly) || !file.read(20).startsWith("TESSDATA-TEST-MODEL"))
        {
            return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel, QStringLiteral("Model cannot be loaded."));
        }
        return PDFOCRError::none();
    };

    // User directory with diacritics and a space (AT-22)
    QTemporaryDir userDirectory(QDir::tempPath() + QStringLiteral("/ocr modely příliš žluťoučké XXXXXX"));
    QVERIFY(userDirectory.isValid());
    PDFOCRModelManager manager(nullptr);
    manager.setAllowInsecureLoopback(true);
    manager.setUserDirectory(userDirectory.path());
    manager.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
    // The validator is the engine: here it accepts only the test model format
    manager.setModelValidator(testValidator);
    manager.setCatalog(catalog);

    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/fra"))->state, PDFOCRModelState::Available);

    std::map<QString, std::pair<bool, QString>> finished;
    connect(&manager, &PDFOCRModelManager::downloadFinished, this, [&finished](const QString& id, bool success, const QString& message) { finished[id] = { success, message }; });

    auto waitFor = [&finished](const QString& id)
    {
        QTRY_VERIFY_WITH_TIMEOUT(finished.count(id) > 0, 15000);
    };

    // AT-03: successful download into the user directory next to the certificates layout
    manager.download({ QStringLiteral("tesseract/fast/fra") });
    QVERIFY(manager.isDownloading());
    waitFor(QStringLiteral("tesseract/fast/fra"));
    QVERIFY2(finished[QStringLiteral("tesseract/fast/fra")].first, qPrintable(finished[QStringLiteral("tesseract/fast/fra")].second));
    std::optional<PDFOCRModelInfo> french = manager.getModel(QStringLiteral("tesseract/fast/fra"));
    QCOMPARE(french->state, PDFOCRModelState::Installed);
    QCOMPARE(french->origin, PDFOCRModelOrigin::Downloaded);
    QVERIFY(french->catalogVerified);
    QVERIFY(french->path.startsWith(QDir(userDirectory.path()).absolutePath() + QStringLiteral("/tesseract/fast/")));
    QVERIFY(QFile::exists(french->path));
    QVERIFY(manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("fra"), PDFOCRModelProfile::Fast));
    QVERIFY(!QDir(userDirectory.filePath(QStringLiteral("downloads"))).exists() || QDir(userDirectory.filePath(QStringLiteral("downloads"))).entryList(QDir::Files).isEmpty());

    // Script models live in a subdirectory, also in the runtime set (LANG-06)
    {
        manager.download({ QStringLiteral("tesseract/fast/script/Arabic") });
        waitFor(QStringLiteral("tesseract/fast/script/Arabic"));
        QVERIFY2(finished[QStringLiteral("tesseract/fast/script/Arabic")].first, qPrintable(finished[QStringLiteral("tesseract/fast/script/Arabic")].second));

        PDFOCRError scriptError;
        const PDFOCRResolvedModelSet scriptSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("fra"), QStringLiteral("script/Arabic") }, PDFOCRModelProfile::Fast, &scriptError);
        QVERIFY2(scriptSet.isValid(), qPrintable(scriptError.message));
        QVERIFY(QFile::exists(scriptSet.dataPath + QStringLiteral("/script/Arabic.traineddata")));
        QVERIFY(QFile::exists(scriptSet.dataPath + QStringLiteral("/fra.traineddata")));
    }

    // Restart / upgrade of the application keeps the downloaded models (AT-22, LANG-12)
    {
        PDFOCRModelManager restarted(nullptr);
        restarted.setUserDirectory(userDirectory.path());
        restarted.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
        restarted.setModelValidator(testValidator);
        restarted.setCatalog(catalog);
        std::optional<PDFOCRModelInfo> restartedFrench = restarted.getModel(QStringLiteral("tesseract/fast/fra"));
        QVERIFY(restartedFrench.has_value());
        QCOMPARE(restartedFrench->state, PDFOCRModelState::Installed);
        QVERIFY(restartedFrench->catalogVerified);
        QCOMPARE(restartedFrench->path, french->path);
    }

    // Crash in the middle of the activation of an update: the last working version is
    // returned back, leftovers of the installation are removed (LANG-07)
    {
        QVERIFY(QFile::rename(french->path, french->path + QStringLiteral(".old")));
        {
            QFile leftover(french->path + QStringLiteral(".new"));
            QVERIFY(leftover.open(QFile::WriteOnly));
            leftover.write("incomplete");
        }

        PDFOCRModelManager recovered(nullptr);
        recovered.setUserDirectory(userDirectory.path());
        recovered.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
        recovered.setModelValidator(testValidator);
        recovered.setCatalog(catalog);
        QVERIFY(QFile::exists(french->path));
        QVERIFY(!QFile::exists(french->path + QStringLiteral(".old")));
        QVERIFY(!QFile::exists(french->path + QStringLiteral(".new")));
        QCOMPARE(recovered.getModel(QStringLiteral("tesseract/fast/fra"))->state, PDFOCRModelState::Installed);
    }

    // Damaged model file is reported by name, when a new runtime set is built (LANG-02)
    {
        QFile::setPermissions(french->path, QFile::ReadOwner | QFile::WriteOwner);
        QFile modelFile(french->path);
        QVERIFY(modelFile.open(QFile::WriteOnly | QFile::Truncate));
        modelFile.write(QByteArray("TESSDATA-TEST-MODEL-") + QByteArray(5000, 'z'));
        modelFile.close();

        PDFOCRError damagedError;
        const PDFOCRResolvedModelSet damagedSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("fra") }, PDFOCRModelProfile::Fast, &damagedError);
        QVERIFY(!damagedSet.isValid());
        QCOMPARE(damagedError.code, PDFOCRErrorCode::VerificationFailed);
        QVERIFY(damagedError.message.contains(QStringLiteral("fra")));

        QVERIFY(modelFile.open(QFile::WriteOnly | QFile::Truncate));
        modelFile.write(goodModel);
        modelFile.close();
    }

    // AT-04: wrong hash, HTML page, dropped connection, missing file, insecure URL
    manager.download({ QStringLiteral("tesseract/fast/bad"), QStringLiteral("tesseract/fast/html"), QStringLiteral("tesseract/fast/drop"), QStringLiteral("tesseract/fast/missing"), QStringLiteral("tesseract/fast/insecure") });
    for (const char* id : { "tesseract/fast/bad", "tesseract/fast/html", "tesseract/fast/drop", "tesseract/fast/missing", "tesseract/fast/insecure" })
    {
        waitFor(QLatin1String(id));
        QVERIFY2(!finished[QLatin1String(id)].first, id);
        QVERIFY2(!finished[QLatin1String(id)].second.isEmpty(), id);
        std::optional<PDFOCRModelInfo> model = manager.getModel(QLatin1String(id));
        QVERIFY(model.has_value());
        QCOMPARE(model->state, PDFOCRModelState::Error);
        QVERIFY(!model->isUsable());
        QVERIFY(model->path.isEmpty());
    }
    QVERIFY(finished[QStringLiteral("tesseract/fast/bad")].second.contains(QStringLiteral("Checksum")) || finished[QStringLiteral("tesseract/fast/bad")].second.contains(QStringLiteral("size")));
    QVERIFY(finished[QStringLiteral("tesseract/fast/html")].second.contains(QStringLiteral("HTML")));
    QVERIFY(finished[QStringLiteral("tesseract/fast/insecure")].second.contains(QStringLiteral("insecure")));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDownloading(), 5000);

    // The old version keeps working after a failed update and the retry succeeds
    PDFOCRCatalog updatedCatalog = catalog;
    updatedCatalog.entries[0] = entry(QStringLiteral("tesseract/fast/fra"), QStringLiteral("fra"), QStringLiteral("/fra-v2.traineddata"), updatedModel, updatedHash);
    updatedCatalog.entries[0].version = QStringLiteral("v2");
    updatedCatalog.sourceCommits[QStringLiteral("fast")] = QStringLiteral("fedcba9876543210");
    server.setResponse(QStringLiteral("/fra-v2.traineddata"), { 200, "application/octet-stream", QByteArray("corrupted update"), false });
    manager.setCatalog(updatedCatalog);
    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/fra"))->state, PDFOCRModelState::UpdateAvailable);

    finished.clear();
    manager.download({ QStringLiteral("tesseract/fast/fra") });
    waitFor(QStringLiteral("tesseract/fast/fra"));
    QVERIFY(!finished[QStringLiteral("tesseract/fast/fra")].first);
    QVERIFY(QFile::exists(french->path));
    PDFOCRError error;
    PDFOCRResolvedModelSet set = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("fra") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(set.isValid(), qPrintable(error.message));

    server.setResponse(QStringLiteral("/fra-v2.traineddata"), { 200, "application/octet-stream", updatedModel, false });
    finished.clear();
    manager.download({ QStringLiteral("tesseract/fast/fra") });
    waitFor(QStringLiteral("tesseract/fast/fra"));
    QVERIFY2(finished[QStringLiteral("tesseract/fast/fra")].first, qPrintable(finished[QStringLiteral("tesseract/fast/fra")].second));
    french = manager.getModel(QStringLiteral("tesseract/fast/fra"));
    QCOMPARE(french->state, PDFOCRModelState::Installed);
    QCOMPARE(french->installedVersion, QStringLiteral("v2"));

    // The new version is used by a new run, while the old set keeps existing (LANG-07)
    PDFOCRResolvedModelSet newSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("fra") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY(newSet.isValid());
    QVERIFY(newSet.hash != set.hash);
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/fra.traineddata")));

    // Cancellation of a queued download
    manager.setMaximumParallelDownloads(1);
    server.setResponse(QStringLiteral("/slow.traineddata"), { 200, "application/octet-stream", goodModel, false });
    PDFOCRCatalog cancelCatalog = updatedCatalog;
    cancelCatalog.entries.push_back(entry(QStringLiteral("tesseract/fast/slow"), QStringLiteral("slow"), QStringLiteral("/slow.traineddata"), goodModel, goodHash));
    cancelCatalog.entries.push_back(entry(QStringLiteral("tesseract/fast/queued"), QStringLiteral("queued"), QStringLiteral("/slow.traineddata"), goodModel, goodHash));
    manager.setCatalog(cancelCatalog);
    finished.clear();
    manager.download({ QStringLiteral("tesseract/fast/slow"), QStringLiteral("tesseract/fast/queued") });
    manager.cancelDownload(QStringLiteral("tesseract/fast/queued"));
    QVERIFY(finished.count(QStringLiteral("tesseract/fast/queued")) > 0);
    QVERIFY(!finished[QStringLiteral("tesseract/fast/queued")].first);
    waitFor(QStringLiteral("tesseract/fast/slow"));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDownloading(), 5000);
    QVERIFY(!manager.getModel(QStringLiteral("tesseract/fast/queued"))->isUsable());
}

// -------------------------------------------------------------------------
// AT-02: recognition with the real engine and the built-in models
// -------------------------------------------------------------------------

void OCRTest::tesseractRecognition()
{
#ifndef PDF4QT_OCR_TESSERACT
    QSKIP("Tesseract engine is not compiled in.");
#else
    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        QSKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    // User directory with diacritics and a space (AT-22): the runtime model set lives
    // in it, so the engine must be able to load the models and the user words from it.
    QTemporaryDir userDirectory(QDir::tempPath() + QStringLiteral("/ocr žluťoučký kůň XXXXXX"));
    QVERIFY(userDirectory.isValid());
    PDFOCRModelManager manager(nullptr);
    manager.setBuiltInDirectory(builtInDirectory);
    manager.setUserDirectory(userDirectory.path());
    manager.loadBundledCatalog();

    // Models are loadable by the pinned engine (LANG-02), including the orientation data
    std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(QStringLiteral("tesseract"));
    QVERIFY(factory);
    QVERIFY(!factory->getVersion().isEmpty());
    const QString tessdata = builtInDirectory + QStringLiteral("/tesseract/fast/tessdata");
    for (const char* language : { "ces", "eng", "slk", "deu", "osd" })
    {
        const PDFOCRError validation = factory->validateModel(tessdata, QLatin1String(language));
        QVERIFY2(!validation, qPrintable(validation.message));
    }

    // A "scanned" page: the text is rendered into an image, which is the only content of the page
    PageSpec textSpec;
    textSpec.size = QSizeF(400, 120);
    textSpec.withHelvetica = true;
    textSpec.content = "BT /F1 28 Tf 30 60 Td (Hello world 2026) Tj ET";
    PDFDocument textDocument = createDocument({ textSpec });
    QImage scan;
    {
        RenderingContext textContext(&textDocument);
        PDFOCRPagePreparer textPreparer = textContext.createPreparer(&textDocument);
        PDFOCRPagePreparer::RasterResult raster = textPreparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
        QVERIFY(!raster.error);
        scan = raster.image.convertToFormat(QImage::Format_RGB888);
    }

    PDFDocument document = createImageDocument(scan, textSpec.size);
    RenderingContext context(&document);

    PDFOCRError error;
    PDFOCRResolvedModelSet models = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("ces"), QStringLiteral("eng") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(models.isValid(), qPrintable(error.message));

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration.engineId = QStringLiteral("tesseract");
    description.configuration.languages = { QStringLiteral("ces"), QStringLiteral("eng") };
    description.configuration.workerCount = 1;
    description.configuration.dpi = 300.0;
    description.configuration.preprocessing.autoOrientation = true;
    description.configuration.userWords = { QStringLiteral("Hello"), QStringLiteral("Pdfforqt") };
    description.models = models;
    QVERIFY2(models.dataPath.contains(QStringLiteral("žluťoučký")), qPrintable(models.dataPath));
    {
        const PDFOCRError validation = factory->validateModel(models.dataPath, QStringLiteral("ces"));
        QVERIFY2(!validation, qPrintable(validation.message));
    }
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = description.configuration;
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRPageResult> result;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = pageResult; });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 120000);
    controller.waitForFinished();

    QVERIFY2(result->state == PDFOCRPageState::Done, qPrintable(result->error.message));
    const QString text = result->getText();
    QVERIFY2(text.contains(QStringLiteral("Hello")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("world")), qPrintable(text));
    QVERIFY(result->getWords()[0]->confidence.isAvailable());
    QCOMPARE(result->getWords()[0]->confidence.level, PDFOCRConfidenceLevel::Word);
    QCOMPARE(result->provenance.engineId, QStringLiteral("tesseract"));
    QVERIFY(result->provenance.modelIds.contains(QStringLiteral("tesseract/fast/ces")));

    // The geometry of the first word lies at the drawn text
    const QRectF helloRect = result->getWords()[0]->quad.boundingRect();
    QVERIFY2(helloRect.left() > 20 && helloRect.left() < 45, qPrintable(QString::number(helloRect.left())));
    QVERIFY2(helloRect.bottom() > 50 && helloRect.top() < 95, qPrintable(QString::number(helloRect.top())));

    // The layer is written and the corrected text is searchable in the reopened document
    PDFOCRSession session(nullptr);
    session.setPageResult(*result);
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer modified = applyResults(document, { *session.getPage(0) }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(modified, qPrintable(report.error.message));
    PDFDocument reopened = read(write(*modified));
    QVERIFY(extractText(reopened, 0).contains(QStringLiteral("Hello")));

    // English of the profiles Standard and Quality is recognized by the LSTM engine, the orientation
    // data are taken from the built-in set of the profile Fast. These models are not distributed,
    // the check runs when a developer has placed them (generate_catalog.py --builtin-best eng --update-builtin).
    for (const PDFOCRModelProfile profile : { PDFOCRModelProfile::Standard, PDFOCRModelProfile::Best })
    {
        const QString profileId = PDFOCRConfiguration::getProfileIdentifier(profile);
        const QString profileTessdata = builtInDirectory + QStringLiteral("/tesseract/%1/tessdata").arg(profileId);
        if (!QFile::exists(profileTessdata + QStringLiteral("/eng.traineddata")))
        {
            continue;
        }

        const PDFOCRError validation = factory->validateModel(profileTessdata, QStringLiteral("eng"));
        QVERIFY2(!validation, qPrintable(validation.message));

        PDFOCRJobDescription profileDescription = description;
        profileDescription.configuration.profile = profile;
        profileDescription.configuration.languages = { QStringLiteral("eng") };
        profileDescription.models = manager.resolveModelSet(QStringLiteral("tesseract"), profileDescription.configuration.languages, profile, &error);
        QVERIFY2(profileDescription.models.isValid(), qPrintable(error.message));
        QVERIFY(profileDescription.models.hasOrientationData);
        profileDescription.pages[0].configuration = profileDescription.configuration;

        result.reset();
        QVERIFY(controller.start(profileDescription, &generation));
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 120000);
        controller.waitForFinished();

        QVERIFY2(result->state == PDFOCRPageState::Done, qPrintable(result->error.message));
        QVERIFY2(result->getText().contains(QStringLiteral("Hello world 2026")), qPrintable(profileId + QChar(':') + result->getText()));
        QVERIFY(result->provenance.modelIds.contains(QStringLiteral("tesseract/%1/eng").arg(profileId)));
    }
#endif
}

// -------------------------------------------------------------------------
// QA-01..QA-05: quality and performance benchmark (run on demand)
// -------------------------------------------------------------------------

#ifdef PDF4QT_OCR_TESSERACT
namespace
{

size_t getLevenshteinDistance(const QStringList& first, const QStringList& second)
{
    std::vector<size_t> previous(size_t(second.size()) + 1);
    std::vector<size_t> current(size_t(second.size()) + 1);
    std::iota(previous.begin(), previous.end(), size_t(0));

    for (int i = 0; i < first.size(); ++i)
    {
        current[0] = size_t(i) + 1;
        for (int j = 0; j < second.size(); ++j)
        {
            const size_t substitution = previous[size_t(j)] + (first[i] == second[j] ? 0 : 1);
            current[size_t(j) + 1] = std::min({ previous[size_t(j) + 1] + 1, current[size_t(j)] + 1, substitution });
        }
        std::swap(previous, current);
    }
    return previous[size_t(second.size())];
}

QStringList toCharacters(const QString& text)
{
    QStringList result;
    for (const QChar& character : text)
    {
        result << QString(character);
    }
    return result;
}

qint64 getPeakMemory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters = { };
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        return qint64(counters.PeakWorkingSetSize);
    }
#endif
    return -1;
}

} // anonymous namespace
#endif

void OCRTest::qualityAndPerformanceBenchmark()
{
#ifndef PDF4QT_OCR_TESSERACT
    QSKIP("Tesseract engine is not compiled in.");
#else
    if (qEnvironmentVariableIsEmpty("PDF4QT_OCR_BENCHMARK"))
    {
        QSKIP("Benchmark runs only when PDF4QT_OCR_BENCHMARK is set (it needs the native platform plugin with system fonts).");
    }

    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        QSKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    QTemporaryDir userDirectory;
    PDFOCRModelManager manager(nullptr);
    manager.setBuiltInDirectory(builtInDirectory);
    manager.setUserDirectory(userDirectory.path());
    manager.loadBundledCatalog();

    // Profile of the models: fast (default), standard or best
    const PDFOCRModelProfile profile = PDFOCRConfiguration::parseProfileIdentifier(qEnvironmentVariable("PDF4QT_OCR_BENCHMARK_PROFILE"));
    QVERIFY2(manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("eng"), profile), "English model of the profile is not available.");

    // Degraded scan: the page is resampled to the given resolution and back (blur), and a noise
    // is added. The targets of the clean print (QA-02) are not evaluated for it.
    const int degradedDpi = qEnvironmentVariableIntValue("PDF4QT_OCR_BENCHMARK_DEGRADED_DPI");
    const bool isDegraded = degradedDpi > 0 && degradedDpi < 300;
    const double degradedNoise = qEnvironmentVariableIsSet("PDF4QT_OCR_BENCHMARK_DEGRADED_NOISE") ? qBound(0, qEnvironmentVariableIntValue("PDF4QT_OCR_BENCHMARK_DEGRADED_NOISE"), 100) : 8.0;

    struct Corpus
    {
        QString name;
        QStringList languages;
        QStringList lines;
    };

    // Clean print, 11 pt, 300 DPI: prose, diacritics, punctuation, date, currency, e-mail, URL, identifiers
    const std::vector<Corpus> corpora =
    {
        { QStringLiteral("Czech (ces)"), { QStringLiteral("ces") },
          { QStringLiteral("P\u0159\u00edli\u0161 \u017elu\u0165ou\u010dk\u00fd k\u016f\u0148 \u00fap\u011bl \u010f\u00e1belsk\u00e9 \u00f3dy."),
            QStringLiteral("Smlouva byla podeps\u00e1na dne 14. 6. 2026 v Brn\u011b a nab\u00fdv\u00e1 \u00fa\u010dinnosti"),
            QStringLiteral("prvn\u00edm dnem n\u00e1sleduj\u00edc\u00edho m\u011bs\u00edce. Celkov\u00e1 cena \u010din\u00ed 12 345,50 K\u010d"),
            QStringLiteral("v\u010detn\u011b dan\u011b z p\u0159idan\u00e9 hodnoty. Objednatel uhrad\u00ed fakturu"),
            QStringLiteral("\u010d. 2026-00417 do t\u0159iceti dn\u016f od jej\u00edho doru\u010den\u00ed."),
            QStringLiteral("Dotazy pos\u00edlejte na adresu podpora@example.cz nebo na"),
            QStringLiteral("https://www.example.cz/kontakt, p\u0159\u00edpadn\u011b volejte 541 234 567."),
            QStringLiteral("Zhotovitel odpov\u00edd\u00e1 za \u0161kodu zp\u016fsobenou poru\u0161en\u00edm povinnost\u00ed"),
            QStringLiteral("podle t\u00e9to smlouvy; v\u00fd\u0161e n\u00e1hrady je omezena \u010d\u00e1stkou 500 000 K\u010d."),
            QStringLiteral("Ob\u011b strany prohla\u0161uj\u00ed, \u017ee si text p\u0159e\u010detly a souhlas\u00ed s n\u00edm.") } },
        { QStringLiteral("English (eng)"), { QStringLiteral("eng") },
          { QStringLiteral("The quick brown fox jumps over the lazy dog near the river bank."),
            QStringLiteral("This agreement was signed on 14 June 2026 in London and becomes"),
            QStringLiteral("effective on the first day of the following month. The total price is"),
            QStringLiteral("$12,345.50 including value added tax. The customer shall pay the"),
            QStringLiteral("invoice No. 2026-00417 within thirty days of its delivery."),
            QStringLiteral("Send your questions to support@example.com or visit the page"),
            QStringLiteral("https://www.example.com/contact, or call +44 20 7946 0958."),
            QStringLiteral("The contractor is liable for any damage caused by a breach of the"),
            QStringLiteral("obligations under this agreement; the compensation is limited to"),
            QStringLiteral("the amount of 500,000 USD. Both parties declare that they agree.") } },
        { QStringLiteral("Mixed (ces+eng)"), { QStringLiteral("ces"), QStringLiteral("eng") },
          { QStringLiteral("Tento dokument obsahuje \u010desk\u00fd i anglick\u00fd text na jedn\u00e9 str\u00e1nce."),
            QStringLiteral("This document contains both Czech and English text on one page."),
            QStringLiteral("Rozpozn\u00e1v\u00e1n\u00ed prob\u00edh\u00e1 lok\u00e1ln\u011b, bez p\u0159ipojen\u00ed k internetu."),
            QStringLiteral("The recognition works locally, without an internet connection."),
            QStringLiteral("V\u00fdsledky je mo\u017en\u00e9 zkontrolovat a ru\u010dn\u011b opravit."),
            QStringLiteral("The results can be reviewed and corrected manually.") } },
    };

    const QSizeF pageSize(595.0, 842.0);
    const QSize imageSize(2480, 3508);
    QStringList report;
    report << QStringLiteral("OCR BENCHMARK");
    report << QStringLiteral("Engine: Tesseract %1, profile %2, 300 DPI, clean print 11 pt (Times New Roman and Arial), A4")
              .arg(PDFOCREngineRegistry::getInstance()->getFactory(QStringLiteral("tesseract"))->getVersion(), PDFOCRConfiguration::getProfileIdentifier(profile));
    if (isDegraded)
    {
        report << QStringLiteral("Degraded scan: resampled to %1 DPI and back, gray noise with the standard deviation %2, reduced contrast").arg(degradedDpi).arg(degradedNoise);
    }
    report << QStringLiteral("Machine: %1, %2, %3 logical processors; Qt %4, %5 build")
              .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()).arg(QThread::idealThreadCount())
              .arg(QString::fromLatin1(qVersion()),
#ifdef QT_NO_DEBUG
                   QStringLiteral("release"));
#else
                   QStringLiteral("debug"));
#endif
    report << QStringLiteral("Normalization: whitespace collapsed to single spaces, no other normalization; omitted text counts as errors.");

    auto createPageImage = [&](const QStringList& lines, const QString& family)
    {
        QImage image(imageSize, QImage::Format_RGB888);
        image.fill(Qt::white);
        image.setDotsPerMeterX(qRound(300.0 / 0.0254));
        image.setDotsPerMeterY(qRound(300.0 / 0.0254));
        QPainter painter(&image);
        QFont font(family);
        font.setPointSizeF(11.0);
        painter.setFont(font);
        painter.setPen(Qt::black);
        int y = 400;
        for (const QString& line : lines)
        {
            painter.drawText(QPoint(300, y), line);
            y += 75;
        }
        painter.end();

        if (isDegraded)
        {
            const QSize degradedSize = imageSize * (degradedDpi / 300.0);
            image = image.scaled(degradedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .scaled(imageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB888);
            image.setDotsPerMeterX(qRound(300.0 / 0.0254));
            image.setDotsPerMeterY(qRound(300.0 / 0.0254));

            std::mt19937 generator(20260921);
            std::normal_distribution<double> noise(0.0, qMax(degradedNoise, 0.001));
            for (int row = 0; row < image.height(); ++row)
            {
                uchar* line = image.scanLine(row);
                for (int column = 0; column < image.width(); ++column)
                {
                    const int value = qBound(0, int(line[3 * column] * 0.85 + 20.0 + noise(generator)), 255);
                    line[3 * column] = line[3 * column + 1] = line[3 * column + 2] = uchar(value);
                }
            }
        }

        return image;
    };

    auto recognize = [&](PDFDocument& document, const QStringList& languages, int workerCount, std::vector<PDFOCRPageResult>& results, qint64& elapsed)
    {
        RenderingContext context(&document);
        PDFOCRError error;
        PDFOCRResolvedModelSet models = manager.resolveModelSet(QStringLiteral("tesseract"), languages, profile, &error);
        QVERIFY2(models.isValid(), qPrintable(error.message));

        PDFOCRJobController controller(nullptr);
        controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

        PDFOCRJobDescription description;
        description.configuration.engineId = QStringLiteral("tesseract");
        description.configuration.profile = profile;
        description.configuration.languages = languages;
        description.configuration.workerCount = workerCount;
        description.models = models;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            PDFOCRPageTask task;
            task.pageIndex = PDFInteger(i);
            task.configuration = description.configuration;
            task.generation = 1;
            description.pages.push_back(task);
        }

        std::optional<PDFOCRJobSummary> summary;
        QMetaObject::Connection pageConnection = connect(&controller, &PDFOCRJobController::pageFinished, this, [&results](int, PDFOCRPageResult result) { results.push_back(std::move(result)); });
        QMetaObject::Connection jobConnection = connect(&controller, &PDFOCRJobController::jobFinished, this, [&summary](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });
        int generation = 0;
        QVERIFY(controller.start(description, &generation));
        QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 3 * 3600 * 1000);
        controller.waitForFinished();
        disconnect(pageConnection);
        disconnect(jobConnection);
        elapsed = summary->elapsedMilliseconds;
        QCOMPARE(summary->errorPages, 0);
    };

    // Quality (QA-01, QA-02)
    for (const Corpus& corpus : corpora)
    {
        if (!manager.getMissingModels(QStringLiteral("tesseract"), corpus.languages, profile).isEmpty())
        {
            // Only the models of the built-in directory are measured, nothing is downloaded
            report << QStringLiteral("%1: skipped, the models are not built-in in the profile").arg(corpus.name);
            continue;
        }

        size_t characterErrors = 0;
        size_t characterCount = 0;
        size_t wordErrors = 0;
        size_t wordCount = 0;
        qint64 totalElapsed = 0;

        for (const QString& family : { QStringLiteral("Times New Roman"), QStringLiteral("Arial") })
        {
            PDFDocument document = createImageDocument(createPageImage(corpus.lines, family), pageSize);
            std::vector<PDFOCRPageResult> results;
            qint64 elapsed = 0;
            recognize(document, corpus.languages, 1, results, elapsed);
            if (QTest::currentTestFailed())
            {
                return;
            }
            totalElapsed += elapsed;
            QCOMPARE(results.size(), size_t(1));

            const QString reference = corpus.lines.join(QChar(' ')).simplified();
            const QString recognized = results.front().getText().simplified();
            characterErrors += getLevenshteinDistance(toCharacters(reference), toCharacters(recognized));
            characterCount += size_t(reference.size());
            const QStringList referenceWords = reference.split(QChar(' '), Qt::SkipEmptyParts);
            wordErrors += getLevenshteinDistance(referenceWords, recognized.split(QChar(' '), Qt::SkipEmptyParts));
            wordCount += size_t(referenceWords.size());
        }

        report << QStringLiteral("%1: CER %2 %, WER %3 % (%4 characters, %5 words, 2 fonts), %6 ms per page including the cold initialization")
                  .arg(corpus.name).arg(100.0 * characterErrors / characterCount, 0, 'f', 2).arg(100.0 * wordErrors / wordCount, 0, 'f', 2)
                  .arg(characterCount).arg(wordCount).arg(totalElapsed / 2);

        // QA-02: target of the clean print corpus, evaluated for each language set
        const double cer = 100.0 * characterErrors / characterCount;
        const double wer = 100.0 * wordErrors / wordCount;
        if (isDegraded)
        {
            continue;
        }
        QVERIFY2(cer <= 2.0, qPrintable(QStringLiteral("%1: CER %2 % is above the target 2 %").arg(corpus.name).arg(cer, 0, 'f', 2)));
        QVERIFY2(wer <= 5.0, qPrintable(QStringLiteral("%1: WER %2 % is above the target 5 %").arg(corpus.name).arg(wer, 0, 'f', 2)));
    }

    // Performance and memory (QA-04, QA-05): many pages share one image, so the memory growth of the pipeline is visible
    const int pageCount = qMax(1, qEnvironmentVariableIntValue("PDF4QT_OCR_BENCHMARK_PAGES"));
    {
        PDFDocumentBuilder builder;
        const QImage image = createPageImage(corpora[1].lines + corpora[1].lines + corpora[1].lines, QStringLiteral("Times New Roman"));
        PDFDocument single = createImageDocument(image, pageSize);
        builder.setDocument(&single);
        const PDFObjectReference firstPage = builder.getPages().front();
        const PDFObject pageObject = builder.getObjectByReference(firstPage);
        std::vector<PDFObjectReference> pages = { firstPage };
        for (int i = 1; i < pageCount; ++i)
        {
            pages.push_back(builder.addObject(pageObject));
        }
        builder.setPages(pages);
        PDFDocument document = builder.build();
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(pageCount));

        const qint64 memoryBefore = getPeakMemory();
        std::vector<PDFOCRPageResult> results;
        qint64 elapsed = 0;
        recognize(document, { QStringLiteral("eng") }, 2, results, elapsed);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QCOMPARE(int(results.size()), pageCount);

        std::vector<qint64> times;
        int words = 0;
        for (const PDFOCRPageResult& result : results)
        {
            QCOMPARE(result.state, PDFOCRPageState::Done);
            times.push_back(result.elapsedMilliseconds);
            words += result.getWordCount();
        }
        std::sort(times.begin(), times.end());

        report << QStringLiteral("Throughput: %1 pages A4/300 DPI, 2 workers, total %2 s, %3 pages per minute, page time p50 %4 ms, p95 %5 ms, %6 words")
                  .arg(pageCount).arg(elapsed / 1000.0, 0, 'f', 1).arg(60000.0 * pageCount / qMax<qint64>(1, elapsed), 0, 'f', 1)
                  .arg(times[times.size() / 2]).arg(times[size_t(double(times.size() - 1) * 0.95)]).arg(words);
        report << QStringLiteral("Peak working set: %1 MB before, %2 MB after the run (raster budget 1024 MB)").arg(memoryBefore / (1024 * 1024)).arg(getPeakMemory() / (1024 * 1024));

        // Size increment of the output: text, font and metadata, not a new copy of the images
        std::vector<PDFOCRPageResult> writtenResults(results.begin(), results.begin() + qMin<size_t>(results.size(), 10));
        for (PDFOCRPageResult& result : writtenResults)
        {
            result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, result.pageIndex);
        }
        PDFOCRTextLayerWriter::Report writeReport;
        PDFDocumentPointer modified = applyResults(document, writtenResults, PDFOCRTextLayerWriter::Options(), &writeReport);
        QVERIFY2(modified, qPrintable(writeReport.error.message));
        const qint64 sizeBefore = write(document).size();
        const qint64 sizeAfter = write(*modified).size();
        report << QStringLiteral("Output increment: %1 bytes for %2 pages with %3 words (%4 bytes per page); original file %5 bytes")
                  .arg(sizeAfter - sizeBefore).arg(writtenResults.size()).arg(writeReport.writtenWords).arg((sizeAfter - sizeBefore) / qint64(writtenResults.size())).arg(sizeBefore);
    }

    // Cancellation latency (JOB-06)
    {
        PDFDocument document = createImageDocument(createPageImage(corpora[1].lines + corpora[1].lines + corpora[1].lines, QStringLiteral("Arial")), pageSize);
        RenderingContext context(&document);
        PDFOCRError error;
        PDFOCRResolvedModelSet models = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("eng") }, profile, &error);
        PDFOCRJobController controller(nullptr);
        controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);
        PDFOCRJobDescription description;
        description.configuration.engineId = QStringLiteral("tesseract");
        description.configuration.languages = { QStringLiteral("eng") };
        description.configuration.workerCount = 1;
        description.models = models;
        PDFOCRPageTask task;
        task.pageIndex = 0;
        task.configuration = description.configuration;
        description.pages.push_back(task);

        std::optional<PDFOCRJobSummary> summary;
        bool recognizing = false;
        connect(&controller, &PDFOCRJobController::pageStateChanged, this, [&recognizing](int, qint64, int state, QString) { recognizing = recognizing || state == int(PDFOCRPageState::Recognizing); });
        connect(&controller, &PDFOCRJobController::jobFinished, this, [&summary](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });
        int generation = 0;
        QVERIFY(controller.start(description, &generation));
        QTRY_VERIFY_WITH_TIMEOUT(recognizing, 60000);
        QTest::qWait(150);
        QElapsedTimer timer;
        timer.start();
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 60000);
        report << QStringLiteral("Cancellation latency during the recognition: %1 ms (cancelled: %2)").arg(timer.elapsed()).arg(summary->cancelled ? QStringLiteral("yes") : QStringLiteral("finished before the stop"));
        controller.waitForFinished();
    }

    const QString reportText = report.join(QChar('\n'));
    qInfo().noquote() << reportText;

    const QByteArray reportFile = qgetenv("PDF4QT_OCR_BENCHMARK_REPORT");
    if (!reportFile.isEmpty())
    {
        QFile file(QString::fromLocal8Bit(reportFile));
        QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
        file.write(reportText.toUtf8());
    }
#endif
}

QTEST_MAIN(OCRTest)

#include "tst_ocrtest.moc"
