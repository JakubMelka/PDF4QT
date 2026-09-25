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
#include "pdfocrexport.h"
#include "pdfocrapplyprocessor.h"
#include "pdfocrdocumentrunner.h"
#include "pdfocrcompression.h"
#include "pdfimageoptimizer.h"
#include "pdfscanpreparation.h"
#include "pdfocrconfiguration.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfoutline.h"
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
#include <QThread>

#include <random>
#include <numbers>
#include <stdexcept>

using namespace pdf;

// A release build (PDF4QT_OCR_REQUIRED) must test the real engine and the built-in
// models: a missing prerequisite is a failure there, not a skip, so a green ctest
// is a release gate of the OCR package
#ifdef PDF4QT_OCR_TESTS_REQUIRED
#define PDF4QT_OCR_SKIP(message) QFAIL(message)
#else
#define PDF4QT_OCR_SKIP(message) QSKIP(message)
#endif

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
    // [tests: writer and preparer]
    void layerBindingAndFingerprint();
    void conformanceAndUserUnit();
    void contentBalanceAndBaseline();
    void documentObjectsPreserved();
    // [tests: session and project]
    void excludedRegionFlagsFollowGeometry();
    void reviewOnlyFlagRoundTrip();
    void originalRecognitionRetained();
    void lineOperations();
    void phraseFindAndReplace();
    void projectKeepsPageOutcomes();
    void projectLimits();
    // [tests: engine, controller and models]
    void scriptModelIdentifiers();
    void engineParameterSchema();
    void pageTimeout();
    void downloadVerificationThread();
    void memoryBudgetAndEngineLimits();
    void regionRotationOverride();
    void runtimeSetLease();
    void modelDependenciesAndCompatibility();
    // [tests: extensions of OCR_PLAN.md]
    void dictionaryInformation();
    void tesseractDictionaryInformation();
    void structuredExport();
    void applyProcessorPlanAndPermissions();
    void applyProcessorExecute();
    void compressionLossless();
    void compressionBitonalAndShared();
    void compressionStreaming();
    void compressionWithTextLayer();
    void optimizeImagesKeepsLayer();
    void tesseractAfterDeskew();
    void perspectiveCorrection();
    void documentRunner();
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

    /// Renders the page of the spec at 300 DPI into an image and creates a "scanned"
    /// document, whose only content is this image
    static PDFDocument createScanDocument(const PageSpec& spec);

    /// Recognizes the first page of the document by Tesseract with the built-in models
    /// of the profile Fast. Returns an empty result with the reason, if the engine or
    /// the models are not available (the caller skips the test), or with the error.
    static std::optional<PDFOCRPageResult> recognizeWithTesseract(const PDFDocument& document, PDFOCRConfiguration configuration, QString* skipReason, QString* errorMessage);

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

    PageSpec pureImagePage;
    pureImagePage.withImage = true;

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

    PDFDocument document = createDocument({ imagePage, textPage, invisiblePage, emptyPage, mixedPage, pureImagePage });
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);

    // A scan with a short digital text is a mixed page: it is never accepted automatically
    // as a page without text, the OCR could write over the page number (INPUT-02, INPUT-04)
    const PDFOCRPageAnalysis image = preparer.analyze(0, nullptr);
    QVERIFY2(image.contentClass == PDFOCRPageContentClass::Mixed,
             qPrintable(QStringLiteral("class %1, visible %2, invisible %3, unmapped %4, images %5, reasons: %6")
                        .arg(int(image.contentClass)).arg(image.visibleCharacterCount).arg(image.invisibleCharacterCount).arg(image.unmappedCharacterCount).arg(image.imageCount).arg(image.ambiguityReasons.join(QStringLiteral(" | ")))));
    QVERIFY(image.visibleCharacterCount > 0);
    QVERIFY(!image.hasVisibleText);
    QVERIFY(!image.notes.isEmpty());
    QCOMPARE(image.textRectangles.size(), size_t(1));
    QVERIFY2(image.textRectangles.front().contains(QPointF(283.0, 13.0)), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(image.textRectangles.front().left()).arg(image.textRectangles.front().top()).arg(image.textRectangles.front().width()).arg(image.textRectangles.front().height())));

    const PDFOCRPageAnalysis pureImage = preparer.analyze(5, nullptr);
    QCOMPARE(pureImage.contentClass, PDFOCRPageContentClass::Image);
    QVERIFY(pureImage.textRectangles.empty());

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
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(image, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::NeedsDecision);
    QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(pureImage, PDFOCRExistingTextPolicy::OnlyPagesWithoutText, false, &reason), Decision::Recognize);

    // An inclusive region over the digital page number is a collision (R05, chapter 6.2)
    {
        PDFOCRRegion overNumber;
        overNumber.id = 1;
        overNumber.type = PDFOCRRegionType::Recognize;
        overNumber.rect = QRectF(270, 5, 25, 15);
        PDFOCRRegion elsewhere;
        elsewhere.id = 2;
        elsewhere.type = PDFOCRRegionType::Recognize;
        elsewhere.rect = QRectF(10, 50, 100, 100);
        const std::vector<PDFOCRRegion> colliding = { overNumber, elsewhere };
        const std::vector<PDFOCRRegion> safe = { elsewhere };
        QCOMPARE(PDFOCRPagePreparer::getRegionsCollidingWithText(image, colliding).size(), size_t(1));
        QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(image, PDFOCRExistingTextPolicy::AddInRegions, true, &reason, &colliding), Decision::NeedsDecision);
        QVERIFY(reason.contains(QStringLiteral("overlap")));
        QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(image, PDFOCRExistingTextPolicy::AddInRegions, true, &reason, &safe), Decision::Recognize);
        QCOMPARE(PDFOCRPagePreparer::evaluateExistingTextPolicy(image, PDFOCRExistingTextPolicy::ReviewOnly, true, &reason, &colliding), Decision::Recognize);
    }
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

/// Directory with the built-in OCR data of the build tree (the language models are
/// extracted there from the archives of the repository by CMake)
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
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
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
    PDF4QT_OCR_SKIP("Tesseract engine is not compiled in.");
#else
    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
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
    PDF4QT_OCR_SKIP("Tesseract engine is not compiled in.");
#else
    if (qEnvironmentVariableIsEmpty("PDF4QT_OCR_BENCHMARK"))
    {
        QSKIP("Benchmark runs only when PDF4QT_OCR_BENCHMARK is set (it needs the native platform plugin with system fonts).");
    }

    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
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

// [test functions: writer and preparer]

// -------------------------------------------------------------------------
// R01, R02: binding of the layer metadata to the content, fingerprint of the
// page content (INPUT-05, PDF-09, PDF-11, EXPORT-04, PDF-13)
// -------------------------------------------------------------------------

void OCRTest::layerBindingAndFingerprint()
{
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

    auto replaceStreamContent = [](const PDFDocument& document, PDFObjectReference reference, const QByteArray& content)
    {
        PDFDocumentBuilder builder(&document);
        PDFDictionary dictionary = *document.getObjectByReference(reference).getStream()->getDictionary();
        dictionary.setEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject());
        dictionary.removeNullObjects();
        builder.setObject(reference, PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), QByteArray(content))));
        return builder.build();
    };

    PageSpec spec;
    spec.withImage = true;
    PDFDocument document = createDocument({ spec });

    PDFOCRTextLayerWriter::Options options;
    options.compress = false;
    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Bound"), QRectF(20, 100, 60, 12) }, { QStringLiteral("layer"), QRectF(90, 100, 50, 12) } });
    result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, 0);
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer applied = applyResults(document, { result }, options, &report);
    QVERIFY2(applied, qPrintable(report.error.message));

    const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(applied.data(), 0);
    QVERIFY(info.isPresent && info.isContentOwn && info.isDataOwn && info.isFontOwn && info.isIsolationOwn && info.fingerprintMatches);
    QVERIFY(PDFOCRTextLayerWriter::readLayer(applied.data(), 0).has_value());
    QVERIFY(PDFOCRTextLayerWriter::isInvisibleTextStream(applied->getDecodedStream(applied->getObjectByReference(info.contentReference).getStream())));

    // Another tool added a visible rectangle into the stream of the layer (R01): the
    // stream is not the own layer anymore, it is a part of the page fingerprint and
    // it must never be removed or replaced.
    {
        QByteArray content = applied->getDecodedStream(applied->getObjectByReference(info.contentReference).getStream());
        const int position = content.lastIndexOf("Q");
        QVERIFY(position > 0);
        content.insert(position, "0 1 0 rg 10 10 50 50 re f\n");
        PDFDocument edited = replaceStreamContent(*applied, info.contentReference, content);

        const PDFOCRTextLayerWriter::LayerInfo editedInfo = PDFOCRTextLayerWriter::readLayerInfo(&edited, 0);
        QVERIFY(editedInfo.isPresent);
        QVERIFY(!editedInfo.isContentOwn);
        QVERIFY(!editedInfo.fingerprintMatches);
        QVERIFY(!PDFOCRTextLayerWriter::readLayer(&edited, 0).has_value());
        QVERIFY(!PDFOCRTextLayerWriter::isInvisibleTextStream(content));
        QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&edited, 0) != PDFOCRPagePreparer::computePageFingerprint(applied.data(), 0));

        // The analysis sees a foreign layer, not the own one
        RenderingContext context(&edited);
        PDFOCRPagePreparer preparer = context.createPreparer(&edited);
        const PDFOCRPageAnalysis analysis = preparer.analyze(0, nullptr);
        QVERIFY(analysis.hasOwnOCRLayer);
        QVERIFY(!analysis.textRectangles.empty());

        PDFDocumentPointer removed = removeLayer(edited, 0);
        QVERIFY(removed);
        QVERIFY(removed->getObjectByReference(info.contentReference).isStream());
        const std::vector<PDFObjectReference> contents = PDFOCRTextLayerWriter::getPageContentReferences(removed.data(), 0);
        QVERIFY(std::find(contents.begin(), contents.end(), info.contentReference) != contents.end());
        QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.data(), 0).isPresent);

        // Writing over the edited layer keeps the edited stream. The words of the new
        // result must not collide with the text of the edited stream (it is a foreign
        // text now), so the new words lie elsewhere.
        PDFOCRPageResult other = createSampleResult(0, { { QStringLiteral("Elsewhere"), QRectF(20, 20, 60, 12) } });
        other.analysis = analysis;
        other.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&edited, 0);
        PDFDocumentPointer overwritten = applyResults(edited, { other }, options, &report);
        QVERIFY2(overwritten, qPrintable(report.error.message));
        QVERIFY(overwritten->getObjectByReference(info.contentReference).isStream());
        const std::vector<PDFObjectReference> overwrittenContents = PDFOCRTextLayerWriter::getPageContentReferences(overwritten.data(), 0);
        QVERIFY(std::find(overwrittenContents.begin(), overwrittenContents.end(), info.contentReference) != overwrittenContents.end());
        QVERIFY(extractText(*overwritten, 0).contains(QStringLiteral("Elsewhere")));

        // A collision with the foreign text is refused (chapter 6.2)
        PDFOCRPageResult colliding = createSampleResult(0, { { QStringLiteral("Over"), QRectF(20, 100, 60, 12) } });
        colliding.analysis = analysis;
        QVERIFY(!applyResults(edited, { colliding }, options, &report));
        QVERIFY(report.error.code == PDFOCRErrorCode::WriteFailed);
    }

    // Changed text of the layer (R01): the stored corrections are not current
    {
        QByteArray content = applied->getDecodedStream(applied->getObjectByReference(info.contentReference).getStream());
        const int position = content.indexOf("[<");
        QVERIFY(position > 0);
        content[position + 2] = content[position + 2] == '0' ? '1' : '0';
        PDFDocument edited = replaceStreamContent(*applied, info.contentReference, content);
        QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(&edited, 0).isContentOwn);
        QVERIFY(!PDFOCRTextLayerWriter::readLayer(&edited, 0).has_value());
    }

    // Changed data stream (R01): the data are not bound to the metadata anymore
    {
        QByteArray data = applied->getDecodedStream(applied->getObjectByReference(info.dataReference).getStream());
        data.replace("\"Bound\"", "\"Bound!\"");
        PDFDocument edited = replaceStreamContent(*applied, info.dataReference, data);
        QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(&edited, 0).isDataOwn);
        QVERIFY(!PDFOCRTextLayerWriter::readLayer(&edited, 0).has_value());
    }

    // Fingerprint (R02): an image of the same size differing in a single byte beyond the
    // first 4 KiB, an added redaction annotation and a changed configuration of the
    // optional content change the fingerprint; an own layer does not.
    {
        QImage first(128, 128, QImage::Format_RGB888);
        first.fill(Qt::white);
        QImage second = first;
        second.setPixelColor(60, 70, Qt::black);
        const PDFDocument firstDocument = createImageDocument(first, QSizeF(300, 300));
        const PDFDocument secondDocument = createImageDocument(second, QSizeF(300, 300));
        QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&firstDocument, 0) != PDFOCRPagePreparer::computePageFingerprint(&secondDocument, 0));
        const PDFDocument firstAgain = createImageDocument(first, QSizeF(300, 300));
        QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&firstDocument, 0), PDFOCRPagePreparer::computePageFingerprint(&firstAgain, 0));

        PDFDocumentBuilder builder(&firstDocument);
        builder.createAnnotationRedact(firstDocument.getCatalog()->getPage(0)->getPageReference(), QRectF(10, 10, 50, 20), Qt::black, Qt::red);
        const PDFDocument redacted = builder.build();
        QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&firstDocument, 0) != PDFOCRPagePreparer::computePageFingerprint(&redacted, 0));

        PDFDocumentBuilder ocBuilder(&firstDocument);
        PDFObjectFactory ocFactory;
        ocFactory.beginDictionary();
        ocFactory.beginDictionaryItem("OCProperties");
        ocFactory.beginDictionary();
        ocFactory.beginDictionaryItem("OCGs");
        ocFactory.beginArray();
        ocFactory.endArray();
        ocFactory.endDictionaryItem();
        ocFactory.beginDictionaryItem("D");
        ocFactory.beginDictionary();
        ocFactory.beginDictionaryItem("BaseState");
        ocFactory << WrapName("OFF");
        ocFactory.endDictionaryItem();
        ocFactory.endDictionary();
        ocFactory.endDictionaryItem();
        ocFactory.endDictionary();
        ocFactory.endDictionaryItem();
        ocFactory.endDictionary();
        ocBuilder.mergeTo(ocBuilder.getCatalogReference(), ocFactory.takeObject());
        const PDFDocument withOptionalContent = ocBuilder.build();
        QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&firstDocument, 0) != PDFOCRPagePreparer::computePageFingerprint(&withOptionalContent, 0));
    }

    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(applied.data(), 0), PDFOCRPagePreparer::computePageFingerprint(&document, 0));
}

// -------------------------------------------------------------------------
// R07, R08: conformance declaration with any prefix (PDF-15), user unit (IMAGE-01)
// -------------------------------------------------------------------------

void OCRTest::conformanceAndUserUnit()
{
    auto withMetadata = [](const PDFDocument& document, const QByteArray& xmp)
    {
        PDFDocumentBuilder builder(&document);
        builder.setCatalogMetadata(xmp);
        return builder.build();
    };

    auto removeConformance = [](const PDFDocument& document, bool* removed) -> PDFDocument
    {
        PDFDocumentBuilder builder(&document);
        *removed = PDFOCRTextLayerWriter::removeConformanceDeclaration(&builder, &document);
        return builder.build();
    };

    const QByteArray header = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">";
    const QByteArray footer = "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";

    PDFDocument document = createDocument({ PageSpec() });

    // Alternate prefix of the PDF/A namespace, attribute form
    {
        const PDFDocument declared = withMetadata(document, header + "<rdf:Description rdf:about=\"\" xmlns:a=\"http://www.aiim.org/pdfa/ns/id/\" a:part=\"2\" a:conformance=\"B\"/>" + footer);
        QStringList declarations;
        QVERIFY(PDFOCRPagePreparer::hasConformanceDeclaration(&declared, &declarations));
        QCOMPARE(declarations, QStringList{ QStringLiteral("PDF/A") });

        bool removed = false;
        const PDFDocument copy = removeConformance(declared, &removed);
        QVERIFY(removed);
        QVERIFY(!PDFOCRPagePreparer::hasConformanceDeclaration(&copy, nullptr));
        const QByteArray metadata = copy.getDecodedStream(copy.getObject(copy.getCatalog()->getMetadata()).getStream());
        QVERIFY(!metadata.contains("a:part"));
        QVERIFY(metadata.contains("rdf:Description"));
    }

    // Element form of PDF/UA with a nonstandard prefix, next to other metadata, which are preserved
    {
        const PDFDocument declared = withMetadata(document, header + "<rdf:Description rdf:about=\"\" xmlns:ua=\"http://www.aiim.org/pdfua/ns/id/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><ua:part>1</ua:part><dc:title>Kept title</dc:title></rdf:Description>" + footer);
        QStringList declarations;
        QVERIFY(PDFOCRPagePreparer::hasConformanceDeclaration(&declared, &declarations));
        QCOMPARE(declarations, QStringList{ QStringLiteral("PDF/UA") });

        bool removed = false;
        const PDFDocument copy = removeConformance(declared, &removed);
        QVERIFY(removed);
        QVERIFY(!PDFOCRPagePreparer::hasConformanceDeclaration(&copy, nullptr));
        const QByteArray metadata = copy.getDecodedStream(copy.getObject(copy.getCatalog()->getMetadata()).getStream());
        QVERIFY(metadata.contains("Kept title"));
        QVERIFY(!metadata.contains("<ua:part>"));
    }

    // A declared but unused namespace is not a conformance declaration
    {
        const PDFDocument undeclared = withMetadata(document, header + "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"/>" + footer);
        QVERIFY(!PDFOCRPagePreparer::hasConformanceDeclaration(&undeclared, nullptr));
    }

    // Unparseable metadata: detected, but the removal must fail (the copy is refused)
    {
        const PDFDocument broken = withMetadata(document, "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:Description xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" pdfaid:part=\"1\"");
        QVERIFY(PDFOCRPagePreparer::hasConformanceDeclaration(&broken, nullptr));
        bool removed = true;
        removeConformance(broken, &removed);
        QVERIFY(!removed);
    }

    // User unit (R08): a page of 72 x 72 units with /UserUnit 2 is 2 x 2 inches; at 300 DPI
    // the raster is 600 x 600 pixels, the canonical coordinates stay in the user space.
    {
        PageSpec spec;
        spec.size = QSizeF(72, 72);
        spec.userUnit = 2.0;
        spec.content = "0 0 0 rg 18 18 36 36 re f";
        const PDFDocument scaled = createDocument({ spec });
        const PDFPage* page = scaled.getCatalog()->getPage(0);
        QCOMPARE(PDFOCRPagePreparer::getRasterSize(page, 300.0), QSize(600, 600));
        QCOMPARE(PDFOCRPagePreparer::estimateRasterBytes(page, 300.0), qint64(600) * 600 * 4);
        QCOMPARE(PDFOCRPagePreparer::getLimitedDpi(page, 300.0, qint64(600) * 600), 300.0);
        QVERIFY(PDFOCRPagePreparer::getLimitedDpi(page, 300.0, qint64(300) * 300) <= 150.0);
        QVERIFY(PDFOCRPagePreparer::getLimitedDpi(page, 300.0, 0, 300) <= 150.0);

        RenderingContext context(&scaled);
        PDFOCRPagePreparer preparer = context.createPreparer(&scaled);
        const PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
        QVERIFY2(!raster.error, qPrintable(raster.error.message));
        QCOMPARE(raster.image.size(), QSize(600, 600));
        QCOMPARE(raster.geometry.dpi, 300.0);
        QCOMPARE(raster.geometry.userUnit, 2.0);

        const QRect dark = findDarkBoundingBox(raster.image);
        QVERIFY2(std::abs(dark.left() - 150) <= 2 && std::abs(dark.width() - 300) <= 3, qPrintable(QStringLiteral("%1 %2 %3 %4").arg(dark.left()).arg(dark.top()).arg(dark.width()).arg(dark.height())));

        const QRectF back = raster.geometry.getEngineToPage().mapRect(QRectF(dark));
        QVERIFY2(std::abs(back.left() - 18.0) < 0.5 && std::abs(back.width() - 36.0) < 0.6, qPrintable(QStringLiteral("%1 %2").arg(back.left()).arg(back.width())));

        const PDFOCRPagePreparer::RasterResult limited = preparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr, 200);
        QVERIFY2(!limited.error, qPrintable(limited.error.message));
        QVERIFY(limited.image.width() <= 200 && limited.image.height() <= 200);
        QVERIFY(limited.geometry.dpi < 300.0);
    }
}

// -------------------------------------------------------------------------
// AT-20, PDF-06: links, forms, attachments, bookmarks, boxes, rotation and metadata
// are preserved. Every object of the original document stays byte-identical, only
// the dictionary of the written page changes (contents, font resource, private data).
// -------------------------------------------------------------------------

void OCRTest::documentObjectsPreserved()
{
    PageSpec spec;
    spec.withImage = true;
    spec.withHelvetica = true;
    spec.size = QSizeF(300, 400);
    spec.cropBox = QRectF(10, 20, 280, 360);
    spec.rotation = PageRotation::Rotate90;
    spec.content = "BT /F1 1 Tf 0 0 Td ET";
    PDFDocument base = createDocument({ spec, PageSpec() });

    PDFDocumentBuilder builder(&base);
    const PDFObjectReference pageReference = base.getCatalog()->getPage(0)->getPageReference();

    // Link annotation
    builder.createAnnotationLink(pageReference, QRectF(20, 30, 50, 10), QStringLiteral("https://example.com/"), LinkHighlightMode::Invert);

    // Text form field with its widget
    PDFObjectFactory fieldFactory;
    fieldFactory.beginDictionary();
    fieldFactory.beginDictionaryItem("FT");
    fieldFactory << WrapName("Tx");
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("T");
    fieldFactory << QStringLiteral("Name");
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("V");
    fieldFactory << QStringLiteral("Value of the field");
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("Type");
    fieldFactory << WrapName("Annot");
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("Subtype");
    fieldFactory << WrapName("Widget");
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("Rect");
    fieldFactory << QRectF(20, 300, 100, 20);
    fieldFactory.endDictionaryItem();
    fieldFactory.beginDictionaryItem("P");
    fieldFactory << pageReference;
    fieldFactory.endDictionaryItem();
    fieldFactory.endDictionary();
    const PDFObjectReference fieldReference = builder.addObject(fieldFactory.takeObject());
    builder.appendTo(pageReference, [&]() { PDFObjectFactory factory; factory.beginDictionary(); factory.beginDictionaryItem("Annots"); factory.beginArray(); factory << fieldReference; factory.endArray(); factory.endDictionaryItem(); factory.endDictionary(); return factory.takeObject(); }());
    builder.createAcroForm({ fieldReference });

    // Embedded file, bookmark and document information
    QByteArray attachment("Attached data");
    PDFDictionary attachmentDictionary;
    attachmentDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("EmbeddedFile"));
    attachmentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(attachment.size()));
    const PDFObjectReference attachmentReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(attachmentDictionary), std::move(attachment))));

    PDFObjectFactory catalogFactory;
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("Names");
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("EmbeddedFiles");
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("Names");
    catalogFactory.beginArray();
    catalogFactory << QStringLiteral("data.txt");
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("Type");
    catalogFactory << WrapName("Filespec");
    catalogFactory.endDictionaryItem();
    catalogFactory.beginDictionaryItem("F");
    catalogFactory << QStringLiteral("data.txt");
    catalogFactory.endDictionaryItem();
    catalogFactory.beginDictionaryItem("EF");
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("F");
    catalogFactory << attachmentReference;
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    catalogFactory.endArray();
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), catalogFactory.takeObject());

    PDFOutlineItem root;
    QSharedPointer<PDFOutlineItem> bookmark = QSharedPointer<PDFOutlineItem>::create();
    bookmark->setTitle(QStringLiteral("Chapter"));
    root.addChild(bookmark);
    builder.setOutline(&root);
    builder.setDocumentTitle(QStringLiteral("Preserved title"));

    PDFDocument document = builder.build();
    const QString fieldValueBefore = extractText(document, 0);

    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Preserved"), QRectF(40, 150, 80, 14) } });
    result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, 0);
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer applied = applyResults(document, { result }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(applied, qPrintable(report.error.message));

    // Round trip through the file, as the user saves it
    PDFDocument saved = read(write(*applied));
    PDFDocument savedOriginal = read(write(document));

    // Every object of the original is unchanged, except the dictionary of the written page
    const PDFObjectStorage::PDFObjects& originalObjects = document.getStorage().getObjects();
    int changed = 0;
    for (size_t i = 0; i < originalObjects.size(); ++i)
    {
        const PDFObjectReference reference(PDFInteger(i), originalObjects[i].generation);
        if (originalObjects[i].object.isNull() || reference == pageReference)
        {
            continue;
        }
        if (!(applied->getObjectByReference(reference) == originalObjects[i].object))
        {
            ++changed;
            qWarning() << "changed object" << reference.objectNumber;
        }
    }
    QCOMPARE(changed, 0);

    // The written page keeps every entry except the ones of the text layer
    const PDFDictionary* before = document.getDictionaryFromObject(document.getObjectByReference(pageReference));
    const PDFDictionary* after = applied->getDictionaryFromObject(applied->getObjectByReference(pageReference));
    QVERIFY(before && after);
    for (size_t i = 0; i < before->getCount(); ++i)
    {
        const QByteArray key = before->getKey(i).getString();
        if (key == "Contents" || key == "Resources")
        {
            continue;
        }
        QVERIFY2(after->get(key) == before->getValue(i), key.constData());
    }

    // The saved documents keep the structures
    for (const PDFDocument* checked : { &savedOriginal, &saved })
    {
        const PDFCatalog* catalog = checked->getCatalog();
        const PDFPage* page = catalog->getPage(0);
        QCOMPARE(page->getCropBox(), QRectF(10, 20, 280, 360));
        QCOMPARE(page->getPageRotation(), PageRotation::Rotate90);
        QCOMPARE(page->getAnnotations().size(), size_t(2));
        QCOMPARE(catalog->getEmbeddedFiles().size(), size_t(1));
        QVERIFY(checked->getInfo()->title == QStringLiteral("Preserved title"));

        const PDFDictionary* catalogDictionary = checked->getDictionaryFromObject(checked->getTrailerDictionary()->get("Root"));
        QVERIFY(catalogDictionary && catalogDictionary->hasKey("Outlines") && catalogDictionary->hasKey("AcroForm"));
    }

    QVERIFY(extractText(saved, 0).contains(QStringLiteral("Preserved")));
    QCOMPARE(fieldValueBefore, extractText(savedOriginal, 0));
}

// -------------------------------------------------------------------------
// PDF-05, R14, R06: balance of the page content by the parser, baseline of the
// line, current masks at the time of the writing
// -------------------------------------------------------------------------

void OCRTest::contentBalanceAndBaseline()
{
    using Balance = PDFOCRTextLayerWriter::ContentBalance;
    QVERIFY(PDFOCRTextLayerWriter::computeContentBalance("q 1 0 0 1 0 0 cm BT /F1 12 Tf (x) Tj ET Q").isBalanced());
    Balance balance = PDFOCRTextLayerWriter::computeContentBalance("q q 2 0 0 2 0 0 cm BT /F1 12 Tf (x) Tj /Span BMC");
    QCOMPARE(balance.graphicStateDepth, 2);
    QCOMPARE(balance.textObjectDepth, 1);
    QCOMPARE(balance.markedContentDepth, 1);
    QVERIFY(!balance.hasError);
    balance = PDFOCRTextLayerWriter::computeContentBalance("Q Q ET");
    QCOMPARE(balance.graphicStateDepth, -2);
    QVERIFY(balance.hasError);
    balance = PDFOCRTextLayerWriter::computeContentBalance("q BI /W 2 /H 1 /CS /G /BPC 8 ID \x01\xFF EI Q");
    QVERIFY2(balance.isBalanced(), qPrintable(QStringLiteral("%1 %2").arg(balance.graphicStateDepth).arg(balance.hasError)));

    QVERIFY(!PDFOCRTextLayerWriter::isInvisibleTextStream("q BT 0 Tr /F 12 Tf [<0041>] TJ ET Q"));
    QVERIFY(!PDFOCRTextLayerWriter::isInvisibleTextStream("q BT 3 Tr /F 12 Tf [<0041>] TJ ET 0 0 1 1 re f Q"));
    QVERIFY(PDFOCRTextLayerWriter::isInvisibleTextStream("q BT 3 Tr /F 12 Tf 1 0 0 1 10 10 Tm 100 Tz -2 Ts [<0041>] TJ ET Q"));

    // Unbalanced foreign content: two unclosed q, an unclosed text object; the layer is
    // written into a balanced page and the whole content validates by the parser.
    PageSpec spec;
    spec.withHelvetica = true;
    spec.content = "q q 1 0 0 1 5 5 cm 0.5 g 5 5 20 10 re f BT /F1 6 Tf 5 60 Td (12) Tj";
    PDFDocument document = createDocument({ spec, PageSpec() });

    QString error;
    QVERIFY(!PDFOCRTextLayerWriter::validatePageContent(&document, 0, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(PDFOCRTextLayerWriter::validatePageContent(&document, 1, &error));

    PDFOCRTextLayerWriter::Options options;
    options.compress = false;
    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Balanced"), QRectF(20, 100, 60, 12) } });
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer applied = applyResults(document, { result }, options, &report);
    QVERIFY2(applied, qPrintable(report.error.message));
    QVERIFY2(PDFOCRTextLayerWriter::validatePageContent(applied.data(), 0, &error), qPrintable(error));
    QVERIFY(extractText(*applied, 0).contains(QStringLiteral("Balanced")));

    const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(applied.data(), 0);
    QVERIFY(info.isIsolationOwn && info.isContentOwn);
    const QByteArray endContent = applied->getDecodedStream(applied->getObjectByReference(info.isolationEndReference).getStream());
    QCOMPARE(endContent.count("Q"), 3);
    QCOMPARE(endContent.count("ET"), 1);
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(applied.data(), 0), PDFOCRPagePreparer::computePageFingerprint(&document, 0));

    // Idempotent apply and removal with the page specific isolation
    QVERIFY(!applyResults(*applied, { result }, options, &report));
    QCOMPARE(report.unchangedPages, (std::vector<PDFInteger>{ 0 }));
    {
        PDFDocumentModifier modifier(applied.data());
        QVERIFY(PDFOCRTextLayerWriter::removeLayer(modifier.getBuilder(), applied.data(), 0));
        modifier.markPageContentsChanged();
        modifier.markReset();
        QVERIFY(modifier.finalize());
        PDFDocumentPointer removed = modifier.getDocument();
        QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.data(), 0).isPresent);
        QVERIFY(removed->getObjectByReference(info.isolationEndReference).isNull());
        QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(removed.data(), 0).size(), size_t(1));
        QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(removed.data(), 0), PDFOCRPagePreparer::computePageFingerprint(&document, 0));
    }

    // Baseline (R14, PDF-08): the origin of the text lies on the baseline and the glyph
    // boxes still cover the geometry of the word (text rise)
    {
        PDFOCRPageResult withBaseline = createSampleResult(1, { { QStringLiteral("Baseline"), QRectF(20, 100, 60, 12) } });
        PDFOCRLine& line = withBaseline.blocks.front().lines.front();
        line.baseline = QLineF(QPointF(20, 103), QPointF(80, 103));     // 3 units above the bottom edge (descender)
        int wordCount = 0;
        QStringList warnings;
        const QByteArray content = PDFOCRTextLayerWriter::createContentStream(withBaseline, "F", false, &wordCount, &warnings);
        QVERIFY(content.contains("-3 Ts"));
        QVERIFY(content.contains(" 20 103 Tm"));

        PDFDocumentPointer baselineApplied = applyResults(document, { withBaseline }, options, &report);
        QVERIFY2(baselineApplied, qPrintable(report.error.message));
        const PDFTextLayout layout = extractLayout(*baselineApplied, 1);
        bool found = false;
        for (const PDFTextBlock& block : layout.getTextBlocks())
        {
            for (const PDFTextLine& textLine : block.getLines())
            {
                for (const TextCharacter& character : textLine.getCharacters())
                {
                    if (character.character.isSpace())
                    {
                        continue;
                    }
                    found = true;
                    // The glyph origin stays at the bottom edge of the word (the rise shifts
                    // the glyphs back from the baseline), so the selection covers the word
                    const QRectF box = character.boundingBox.boundingRect();
                    QVERIFY2(box.isEmpty() || QRectF(19.5, 99.5, 61, 13).contains(box), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.width()).arg(box.height())));
                    QVERIFY2(std::abs(character.position.y() - 100.0) < 0.01, qPrintable(QString::number(character.position.y())));
                }
            }
        }
        QVERIFY(found);

        // Without a baseline (or with a baseline outside of the word) no rise is written
        line.baseline = QLineF(QPointF(20, 130), QPointF(80, 130));
        const QByteArray outside = PDFOCRTextLayerWriter::createContentStream(withBaseline, "F", false, &wordCount, &warnings);
        QVERIFY(!outside.contains("Ts"));
    }

    // Current masks at the time of the writing (R06): a stale flag does not matter, the
    // exclusion region of the result decides; a confirmed word is written.
    {
        PDFOCRPageResult masked = createSampleResult(1, { { QStringLiteral("Visible"), QRectF(20, 20, 60, 12) }, { QStringLiteral("Hidden"), QRectF(120, 20, 60, 12) } });
        PDFOCRRegion exclusion;
        exclusion.id = masked.allocateId();
        exclusion.type = PDFOCRRegionType::Exclude;
        exclusion.rect = QRectF(150, 15, 50, 20);
        masked.regions.push_back(exclusion);

        PDFDocumentPointer maskedApplied = applyResults(document, { masked }, options, &report);
        QVERIFY2(maskedApplied, qPrintable(report.error.message));
        QCOMPARE(report.writtenWords, 1);
        QVERIFY(extractText(*maskedApplied, 1).contains(QStringLiteral("Visible")));
        QVERIFY(!extractText(*maskedApplied, 1).contains(QStringLiteral("Hidden")));

        masked.getWords()[1]->reviewState = PDFOCRReviewState::Confirmed;
        PDFDocumentPointer confirmedApplied = applyResults(document, { masked }, options, &report);
        QVERIFY2(confirmedApplied, qPrintable(report.error.message));
        QCOMPARE(report.writtenWords, 2);

        // A redaction rectangle of the analysis masks as well
        masked.getWords()[1]->reviewState = PDFOCRReviewState::Unreviewed;
        masked.regions.clear();
        masked.analysis.redactionRectangles.push_back(QRectF(150, 15, 50, 20));
        PDFDocumentPointer redactedApplied = applyResults(document, { masked }, options, &report);
        QVERIFY2(redactedApplied, qPrintable(report.error.message));
        QCOMPARE(report.writtenWords, 1);
    }

    // A result recognized for the review only is never written (R03)
    {
        PDFOCRPageResult reviewOnly = createSampleResult(1, { { QStringLiteral("Review"), QRectF(20, 20, 60, 12) } });
        reviewOnly.reviewOnly = true;
        QVERIFY(!applyResults(document, { reviewOnly }, options, &report));
        QCOMPARE(report.error.code, PDFOCRErrorCode::WriteFailed);
    }
}

// [test functions: session and project]

// -------------------------------------------------------------------------
// R06: conflicts with the masks follow the geometry changes (REGION-05)
// -------------------------------------------------------------------------

void OCRTest::excludedRegionFlagsFollowGeometry()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) }, { QStringLiteral("world"), QRectF(70, 100, 50, 12) } }));

    const int helloId = session.getPage(0)->getWords()[0]->id;
    const int worldId = session.getPage(0)->getWords()[1]->id;
    auto overlaps = [&](const PDFOCRSession& target, int wordId)
    {
        const PDFOCRWord* word = target.getPage(0)->findWord(wordId);
        return word && word->overlapsExcludedRegion;
    };

    QVERIFY(!overlaps(session, helloId));
    QVERIFY(!overlaps(session, worldId));

    // Exclusion drawn over the first word marks it
    PDFOCRRegion region;
    region.type = PDFOCRRegionType::Exclude;
    region.rect = QRectF(0, 90, 65, 30);
    const int regionId = session.addRegion(0, region);
    QVERIFY(regionId > 0);
    QVERIFY(overlaps(session, helloId));
    QVERIFY(!overlaps(session, worldId));
    QVERIFY(PDFOCRReview::requiresReview(*session.getPage(0)->findWord(helloId), 0.0));

    // Region moved away from the word: the flag is cleared
    PDFOCRRegion moved = *session.getPage(0)->findRegion(regionId);
    moved.rect = QRectF(200, 90, 50, 30);
    QVERIFY(session.updateRegion(0, moved));
    QVERIFY(!overlaps(session, helloId));
    QVERIFY(!overlaps(session, worldId));

    // Word moved into the region: the flag is set (the geometry change alone does not clear it)
    QVERIFY(session.setWordQuad(0, worldId, PDFOCRQuad::fromRect(QRectF(210, 100, 30, 12))));
    QVERIFY(overlaps(session, worldId));

    // Undo/redo keep the flags consistent with the geometry
    session.undo();
    QVERIFY(!overlaps(session, worldId));
    session.redo();
    QVERIFY(overlaps(session, worldId));

    // Removed region clears the flag, its undo restores it
    QVERIFY(session.removeRegion(0, regionId));
    QVERIFY(!overlaps(session, worldId));
    session.undo();
    QVERIFY(overlaps(session, worldId));

    // Merged word inherits the conflict from the recomputation
    QVERIFY(session.setWordQuad(0, helloId, PDFOCRQuad::fromRect(QRectF(150, 100, 50, 12))));
    int mergedId = 0;
    QVERIFY(session.mergeWords(0, helloId, worldId, &mergedId));
    QVERIFY(overlaps(session, mergedId));
    session.undo();
    session.undo();

    // Project round trip: a stale flag in the project is recomputed from the regions
    PDFOCRProject project = session.createProject({ 0 });
    QVERIFY(project.pages.at(0).findWord(worldId)->overlapsExcludedRegion);
    project.pages.at(0).findWord(worldId)->overlapsExcludedRegion = false;
    project.pages.at(0).findWord(helloId)->overlapsExcludedRegion = true;

    QString error;
    PDFOCRProject reloaded;
    QVERIFY2(PDFOCRProjectSerializer::fromBytes(PDFOCRProjectSerializer::toBytes(project), reloaded, &error), qPrintable(error));

    PDFOCRSession loaded(nullptr);
    loaded.loadProject(reloaded, { 0 });
    QVERIFY(overlaps(loaded, worldId));
    QVERIFY(!overlaps(loaded, helloId));

    // Result set from the outside (job, project) is checked against the regions of the page
    PDFOCRPageResult fresh = createSampleResult(0, { { QStringLiteral("inside"), QRectF(205, 100, 30, 12) }, { QStringLiteral("outside"), QRectF(10, 100, 30, 12) } });
    for (PDFOCRWord* word : fresh.getWords())
    {
        word->overlapsExcludedRegion = word->text == QStringLiteral("outside");
    }
    loaded.setPageResult(fresh);
    QVERIFY(loaded.getPage(0)->getWords()[0]->overlapsExcludedRegion);
    QVERIFY(!loaded.getPage(0)->getWords()[1]->overlapsExcludedRegion);
}

// -------------------------------------------------------------------------
// R03: review-only is a property of the result (INPUT-04, EXPORT-03)
// -------------------------------------------------------------------------

void OCRTest::reviewOnlyFlagRoundTrip()
{
    PDFOCRSession session(nullptr);
    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } });
    result.reviewOnly = true;
    session.setPageResult(result);
    QVERIFY(session.getPage(0)->reviewOnly);

    int changedSignals = 0;
    connect(&session, &PDFOCRSession::pageChanged, this, [&](qint64 pageIndex) { changedSignals += pageIndex == 0 ? 1 : 0; });

    // Setting the flag is not an undo step, marks the session dirty and notifies
    session.setDirty(false);
    session.setPageReviewOnly(0, false);
    QVERIFY(!session.getPage(0)->reviewOnly);
    QVERIFY(session.isDirty());
    QCOMPARE(changedSignals, 1);
    QVERIFY(!session.canUndo());
    session.setPageReviewOnly(0, false);
    QCOMPARE(changedSignals, 1);
    session.setPageReviewOnly(0, true);
    QVERIFY(session.getPage(0)->reviewOnly);

    // Editing operations and undo/redo keep the flag
    const int wordId = session.getPage(0)->getWords()[0]->id;
    QVERIFY(session.setWordText(0, wordId, QStringLiteral("Hallo")));
    QVERIFY(session.getPage(0)->reviewOnly);
    session.undo();
    QVERIFY(session.getPage(0)->reviewOnly);
    session.redo();
    QVERIFY(session.getPage(0)->reviewOnly);

    // The flag is not part of the history: a later change survives the undo
    session.setPageReviewOnly(0, false);
    session.undo();
    QVERIFY(!session.getPage(0)->reviewOnly);
    session.redo();
    QVERIFY(!session.getPage(0)->reviewOnly);
    session.setPageReviewOnly(0, true);

    // Repeated recognition keeps the flag of the page
    PDFOCRPageResult candidate = createSampleResult(0, { { QStringLiteral("Candidate"), QRectF(10, 100, 50, 12) } });
    QVERIFY(session.applyCandidate(0, candidate, PDFOCRSession::CandidateMode::Replace, -1));
    QVERIFY(session.getPage(0)->reviewOnly);

    // Result set from the outside keeps its own flag
    PDFOCRPageResult writable = createSampleResult(1, { { QStringLiteral("Writable"), QRectF(10, 100, 50, 12) } });
    session.setPageResult(writable);
    QVERIFY(!session.getPage(1)->reviewOnly);
    session.setPageReviewOnly(1, true);
    QVERIFY(session.getPage(1)->reviewOnly);
    session.setPageResult(writable);
    QVERIFY(!session.getPage(1)->reviewOnly);

    // Project round trip
    PDFOCRProject project = session.createProject({ 0, 1 });
    QVERIFY(project.pages.at(0).reviewOnly);
    QVERIFY(!project.pages.at(1).reviewOnly);

    QString error;
    PDFOCRProject reloaded;
    QVERIFY2(PDFOCRProjectSerializer::fromBytes(PDFOCRProjectSerializer::toBytes(project), reloaded, &error), qPrintable(error));
    QVERIFY(reloaded.pages.at(0).reviewOnly);
    QVERIFY(!reloaded.pages.at(1).reviewOnly);

    PDFOCRSession loaded(nullptr);
    loaded.loadProject(reloaded, { 0, 1 });
    QVERIFY(loaded.getPage(0)->reviewOnly);
    QVERIFY(!loaded.getPage(1)->reviewOnly);
}

// -------------------------------------------------------------------------
// R09: raw recognition survives the structural corrections (DATA-02, CONF-05)
// -------------------------------------------------------------------------

void OCRTest::originalRecognitionRetained()
{
    PDFOCRPageResult result;
    result.pageIndex = 0;
    result.state = PDFOCRPageState::Done;
    PDFOCRBlock block;
    block.id = result.allocateId();
    PDFOCRLine line;
    line.id = result.allocateId();
    line.words.push_back(makeWord(result, QStringLiteral("quick"), QRectF(10, 100, 50, 12), 90.0));
    line.words.push_back(makeWord(result, QStringLiteral("brown"), QRectF(70, 100, 50, 12), 60.0));
    line.updateGeometryFromWords();
    block.lines.push_back(line);
    block.updateGeometryFromLines();
    result.blocks.push_back(block);

    const int lineId = line.id;
    const int quickId = line.words[0].id;
    const int brownId = line.words[1].id;

    PDFOCRSession session(nullptr);
    session.setPageResult(result);
    QCOMPARE(session.getOriginalWords(0).size(), size_t(2));
    QCOMPARE(session.getPage(0)->originalBlocks, session.getPage(0)->blocks);

    // Merge replaces the words, the raw recognition keeps them with their scores
    int mergedId = 0;
    QVERIFY(session.mergeWords(0, quickId, brownId, &mergedId));
    QVERIFY(!session.getPage(0)->findWord(quickId));
    const PDFOCRWord* originalQuick = session.findOriginalWord(0, quickId);
    QVERIFY(originalQuick);
    QCOMPARE(originalQuick->text, QStringLiteral("quick"));
    QCOMPARE(originalQuick->confidence.normalized.value(), 90.0);
    QCOMPARE(originalQuick->quad, PDFOCRQuad::fromRect(QRectF(10, 100, 50, 12)));
    QVERIFY(!session.findOriginalWord(0, mergedId));

    // Undo/redo do not change the raw recognition
    const std::vector<PDFOCRBlock> originalBlocks = session.getPage(0)->originalBlocks;
    session.undo();
    QCOMPARE(session.getPage(0)->originalBlocks, originalBlocks);
    session.redo();
    QCOMPARE(session.getPage(0)->originalBlocks, originalBlocks);

    // Neither do the other corrections
    QVERIFY(session.setWordText(0, mergedId, QStringLiteral("edited")));
    QVERIFY(session.setLineText(0, lineId, QStringLiteral("the quick brown fox")));
    QVERIFY(session.setWordQuad(0, session.getPage(0)->getWords()[0]->id, PDFOCRQuad::fromRect(QRectF(5, 100, 20, 12))));
    QVERIFY(session.removeWord(0, session.getPage(0)->getWords()[3]->id));
    QCOMPARE(session.getPage(0)->originalBlocks, originalBlocks);
    QVERIFY(session.confirmAllWords(0, nullptr));
    QCOMPARE(session.getPage(0)->originalBlocks, originalBlocks);
    session.undo();
    session.undo();
    session.undo();
    session.undo();
    session.undo();
    QCOMPARE(session.getPage(0)->findWord(mergedId)->text, QStringLiteral("quickbrown"));

    // Save and load: the original words with their scores are still retrievable
    PDFOCRProject project = session.createProject({ 0 });
    QString error;
    PDFOCRProject reloaded;
    QVERIFY2(PDFOCRProjectSerializer::fromBytes(PDFOCRProjectSerializer::toBytes(project), reloaded, &error), qPrintable(error));

    PDFOCRSession loaded(nullptr);
    loaded.loadProject(reloaded, { 0 });
    QCOMPARE(loaded.getPage(0)->getWordCount(), 1);
    QCOMPARE(loaded.getPage(0)->originalBlocks, originalBlocks);
    const PDFOCRWord* originalBrown = loaded.findOriginalWord(0, brownId);
    QVERIFY(originalBrown);
    QCOMPARE(originalBrown->text, QStringLiteral("brown"));
    QCOMPARE(originalBrown->confidence.normalized.value(), 60.0);
    QCOMPARE(loaded.getOriginalWords(0).size(), size_t(2));
    QVERIFY(PDFOCRValidator::validate(*loaded.getPage(0)).isEmpty());

    // Restoration of the merged word gives the concatenation of the original texts
    QVERIFY(loaded.restoreOriginalText(0, mergedId));
    QCOMPARE(loaded.getPage(0)->findWord(mergedId)->text, QStringLiteral("quick brown"));

    // A word from a line edit has no historical text of its own, its predecessors
    // in the raw recognition provide it
    PDFOCRSession lineSession(nullptr);
    lineSession.setPageResult(result);
    QVERIFY(lineSession.setLineText(0, lineId, QStringLiteral("quickbrown")));
    const PDFOCRWord* joined = lineSession.getPage(0)->getWords()[0];
    QVERIFY(joined->originalText.isEmpty());
    QCOMPARE(joined->predecessorIds, (std::vector<int>{ quickId, brownId }));
    QVERIFY(lineSession.restoreOriginalText(0, joined->id));
    QCOMPARE(lineSession.getPage(0)->getWords()[0]->text, QStringLiteral("quick brown"));

    // Manually inserted word has no original recognition
    int insertedId = 0;
    QVERIFY(lineSession.insertWord(0, lineId, 0, QStringLiteral("new"), PDFOCRQuad(), &insertedId));
    QVERIFY(!lineSession.restoreOriginalText(0, insertedId));

    // Repeated recognition of the page replaces the raw recognition
    PDFOCRPageResult candidate = createSampleResult(0, { { QStringLiteral("Candidate"), QRectF(10, 100, 50, 12) } });
    QVERIFY(session.applyCandidate(0, candidate, PDFOCRSession::CandidateMode::Replace, -1));
    QCOMPARE(session.getOriginalWords(0).size(), size_t(1));
    QCOMPARE(session.getOriginalWords(0)[0]->text, QStringLiteral("Candidate"));
    QCOMPARE(session.getOriginalWords(0)[0]->id, session.getPage(0)->getWords()[0]->id);
    session.undo();
    QCOMPARE(session.getPage(0)->originalBlocks, originalBlocks);

    // Repeated recognition of a region replaces only the raw recognition of the region
    PDFOCRPageResult regionResult = createSampleResult(0, { { QStringLiteral("whole"), QRectF(10, 150, 50, 12) } });
    PDFOCRRegion region;
    region.id = regionResult.allocateId();
    region.rect = QRectF(0, 90, 200, 30);
    regionResult.regions.push_back(region);
    PDFOCRBlock regionBlock = createSampleResult(0, { { QStringLiteral("old"), QRectF(10, 100, 50, 12) } }).blocks[0];
    regionBlock.id = regionResult.allocateId();
    regionBlock.lines[0].id = regionResult.allocateId();
    regionBlock.lines[0].words[0].id = regionResult.allocateId();
    regionBlock.regionId = region.id;
    regionResult.blocks.push_back(regionBlock);

    PDFOCRSession regionSession(nullptr);
    regionSession.setPageResult(regionResult);
    const int wholeId = regionSession.getPage(0)->getWords()[0]->id;
    QCOMPARE(regionSession.getOriginalWords(0).size(), size_t(2));

    PDFOCRPageResult regionCandidate = createSampleResult(0, { { QStringLiteral("new1"), QRectF(10, 100, 30, 12) }, { QStringLiteral("new2"), QRectF(50, 100, 30, 12) } });
    regionCandidate.blocks[0].regionId = region.id;
    QVERIFY(regionSession.applyCandidate(0, regionCandidate, PDFOCRSession::CandidateMode::ReplaceRegion, region.id));
    const std::vector<const PDFOCRWord*> originalWords = regionSession.getOriginalWords(0);
    QCOMPARE(originalWords.size(), size_t(3));
    QCOMPARE(originalWords[0]->id, wholeId);
    QCOMPARE(originalWords[1]->text, QStringLiteral("new1"));
    QCOMPARE(originalWords[2]->text, QStringLiteral("new2"));
    QCOMPARE(originalWords[1]->id, regionSession.getPage(0)->getWords()[1]->id);
    QVERIFY(PDFOCRValidator::validate(*regionSession.getPage(0)).isEmpty());
}

// -------------------------------------------------------------------------
// EDIT-02: line operations
// -------------------------------------------------------------------------

void OCRTest::lineOperations()
{
    PDFOCRPageResult result;
    result.pageIndex = 0;
    result.state = PDFOCRPageState::Done;

    auto createLine = [&](const std::vector<std::pair<QString, QRectF>>& words)
    {
        PDFOCRLine line;
        line.id = result.allocateId();
        for (const auto& item : words)
        {
            line.words.push_back(makeWord(result, item.first, item.second, 95.0));
        }
        line.updateGeometryFromWords();
        return line;
    };

    PDFOCRBlock block1;
    block1.id = result.allocateId();
    block1.lines.push_back(createLine({ { QStringLiteral("alpha"), QRectF(10, 120, 50, 12) }, { QStringLiteral("beta"), QRectF(70, 120, 40, 12) } }));
    block1.lines.push_back(createLine({ { QStringLiteral("gamma"), QRectF(10, 100, 50, 12) }, { QStringLiteral("delta"), QRectF(70, 100, 40, 12) } }));
    block1.updateGeometryFromLines();

    PDFOCRBlock block2;
    block2.id = result.allocateId();
    block2.lines.push_back(createLine({ { QStringLiteral("epsilon"), QRectF(10, 50, 60, 12) } }));
    block2.updateGeometryFromLines();

    result.blocks = { block1, block2 };

    const int block1Id = block1.id;
    const int block2Id = block2.id;
    const int lineAId = block1.lines[0].id;
    const int lineBId = block1.lines[1].id;
    const int lineCId = block2.lines[0].id;
    const int alphaId = block1.lines[0].words[0].id;
    const int betaId = block1.lines[0].words[1].id;
    const int gammaId = block1.lines[1].words[0].id;
    const int deltaId = block1.lines[1].words[1].id;
    const QLineF baselineA = block1.lines[0].baseline;
    const QLineF baselineB = block1.lines[1].baseline;

    PDFOCRSession session(nullptr);
    session.setPageResult(result);
    auto page = [&]() { return session.getPage(0); };
    auto isValid = [&]() { return PDFOCRValidator::validate(*page()).isEmpty(); };
    auto wordIds = [](const PDFOCRLine& line)
    {
        std::vector<int> ids;
        for (const PDFOCRWord& word : line.words)
        {
            ids.push_back(word.id);
        }
        return ids;
    };
    QVERIFY(isValid());

    // Merge: the second line is appended after the first, words keep identifiers
    int mergedId = 0;
    QVERIFY(session.mergeLines(0, lineAId, lineBId, &mergedId));
    QVERIFY(mergedId > 0);
    const PDFOCRLine* merged = page()->findLine(mergedId);
    QVERIFY(merged);
    QCOMPARE(wordIds(*merged), (std::vector<int>{ alphaId, betaId, gammaId, deltaId }));
    QCOMPARE(page()->findBlock(block1Id)->lines.size(), size_t(1));
    QVERIFY(!page()->findLine(lineAId));
    QVERIFY(!page()->findLine(lineBId));
    QVERIFY(merged->quad.isValid());
    QCOMPARE(merged->quad.boundingRect(), QRectF(10, 100, 100, 32));
    QCOMPARE(merged->baseline.p1(), baselineA.p1());
    QCOMPARE(merged->baseline.p2(), baselineB.p2());
    QVERIFY(isValid());
    QVERIFY(session.getUndoText() == PDFTranslationContext::tr("Merge lines"));

    // Lines of different blocks and the same line cannot be merged
    QVERIFY(!session.mergeLines(0, mergedId, lineCId, nullptr));
    QVERIFY(!session.mergeLines(0, mergedId, mergedId, nullptr));
    QVERIFY(!session.mergeLines(0, mergedId, 12345, nullptr));

    session.undo();
    QCOMPARE(page()->findBlock(block1Id)->lines.size(), size_t(2));
    QCOMPARE(wordIds(*page()->findLine(lineAId)), (std::vector<int>{ alphaId, betaId }));
    QCOMPARE(page()->findLine(lineBId)->baseline, baselineB);
    QVERIFY(!page()->findLine(mergedId));
    QVERIFY(isValid());
    session.redo();
    QVERIFY(page()->findLine(mergedId));

    // Split after the second word: the rest goes into a new line right after the original
    int splitId = 0;
    QVERIFY(session.splitLine(0, mergedId, betaId, &splitId));
    QVERIFY(splitId > 0);
    const PDFOCRBlock* splitBlock = page()->findBlock(block1Id);
    QCOMPARE(splitBlock->lines.size(), size_t(2));
    QCOMPARE(splitBlock->lines[0].id, mergedId);
    QCOMPARE(splitBlock->lines[1].id, splitId);
    QCOMPARE(wordIds(splitBlock->lines[0]), (std::vector<int>{ alphaId, betaId }));
    QCOMPARE(wordIds(splitBlock->lines[1]), (std::vector<int>{ gammaId, deltaId }));
    QCOMPARE(splitBlock->lines[0].quad.boundingRect(), QRectF(10, 120, 100, 12));
    QCOMPARE(splitBlock->lines[1].quad.boundingRect(), QRectF(10, 100, 100, 12));
    QCOMPARE(splitBlock->lines[1].baseline, baselineB);
    QVERIFY(isValid());

    // Split after the last word or after a foreign word is refused
    QVERIFY(!session.splitLine(0, splitId, deltaId, nullptr));
    QVERIFY(!session.splitLine(0, splitId, alphaId, nullptr));
    QVERIFY(!session.splitLine(0, 12345, alphaId, nullptr));

    session.undo();
    QCOMPARE(wordIds(*page()->findLine(mergedId)), (std::vector<int>{ alphaId, betaId, gammaId, deltaId }));
    QVERIFY(!page()->findLine(splitId));
    QVERIFY(isValid());
    session.redo();
    QVERIFY(page()->findLine(splitId));

    // Move the new line into the second block at the first position
    QVERIFY(session.moveLineToBlock(0, splitId, block2Id, 0));
    QCOMPARE(page()->findBlock(block1Id)->lines.size(), size_t(1));
    QCOMPARE(page()->findBlock(block2Id)->lines.size(), size_t(2));
    QCOMPARE(page()->findBlock(block2Id)->lines[0].id, splitId);
    QCOMPARE(page()->findBlock(block2Id)->lines[1].id, lineCId);
    QCOMPARE(page()->findBlock(block2Id)->quad.boundingRect(), QRectF(10, 50, 100, 62));
    QCOMPARE(page()->findBlock(block1Id)->quad.boundingRect(), QRectF(10, 120, 100, 12));
    QVERIFY(isValid());

    // Moving the last line of the block removes the block (index is clamped)
    QVERIFY(session.moveLineToBlock(0, mergedId, block2Id, 100));
    QCOMPARE(page()->blocks.size(), size_t(1));
    QCOMPARE(page()->blocks[0].id, block2Id);
    QCOMPARE(page()->blocks[0].lines.size(), size_t(3));
    QCOMPARE(page()->blocks[0].lines[2].id, mergedId);
    QCOMPARE(page()->blocks[0].quad.boundingRect(), QRectF(10, 50, 100, 82));
    QVERIFY(isValid());

    session.undo();
    QCOMPARE(page()->blocks.size(), size_t(2));
    QCOMPARE(page()->findBlock(block1Id)->lines[0].id, mergedId);
    QVERIFY(isValid());
    session.undo();
    QCOMPARE(page()->findBlock(block1Id)->lines.size(), size_t(2));
    QCOMPARE(page()->findBlock(block2Id)->lines.size(), size_t(1));
    QVERIFY(isValid());
    session.redo();
    session.redo();
    QCOMPARE(page()->blocks.size(), size_t(1));

    // Reorder inside the same block, unknown target
    QVERIFY(session.moveLineToBlock(0, mergedId, block2Id, 0));
    QCOMPARE(page()->blocks[0].lines[0].id, mergedId);
    QCOMPARE(page()->blocks[0].lines[1].id, splitId);
    QVERIFY(!session.moveLineToBlock(0, mergedId, block2Id, 0));
    QVERIFY(!session.moveLineToBlock(0, mergedId, 12345, 0));
    QVERIFY(isValid());

    // Right-to-left lines: words are in the logical order, quads are visual (EDIT-08)
    PDFOCRPageResult rtl;
    rtl.pageIndex = 1;
    rtl.state = PDFOCRPageState::Done;
    PDFOCRBlock rtlBlock;
    rtlBlock.id = rtl.allocateId();
    PDFOCRLine rtlLineA;
    rtlLineA.id = rtl.allocateId();
    rtlLineA.direction = PDFOCRTextDirection::RightToLeft;
    rtlLineA.words.push_back(makeWord(rtl, QStringLiteral("AB"), QRectF(100, 120, 40, 12), 95.0));
    rtlLineA.words.push_back(makeWord(rtl, QStringLiteral("CD"), QRectF(50, 120, 40, 12), 95.0));
    rtlLineA.updateGeometryFromWords();
    PDFOCRLine rtlLineB;
    rtlLineB.id = rtl.allocateId();
    rtlLineB.direction = PDFOCRTextDirection::RightToLeft;
    rtlLineB.words.push_back(makeWord(rtl, QStringLiteral("EF"), QRectF(100, 100, 40, 12), 95.0));
    rtlLineB.words.push_back(makeWord(rtl, QStringLiteral("GH"), QRectF(50, 100, 40, 12), 95.0));
    rtlLineB.updateGeometryFromWords();
    rtlBlock.lines = { rtlLineA, rtlLineB };
    rtlBlock.updateGeometryFromLines();
    rtl.blocks.push_back(rtlBlock);
    session.setPageResult(rtl);

    const int abId = rtlLineA.words[0].id;
    const int cdId = rtlLineA.words[1].id;
    const int efId = rtlLineB.words[0].id;
    const int ghId = rtlLineB.words[1].id;

    int rtlMergedId = 0;
    QVERIFY(session.mergeLines(1, rtlLineA.id, rtlLineB.id, &rtlMergedId));
    const PDFOCRLine* rtlMerged = session.getPage(1)->findLine(rtlMergedId);
    QVERIFY(rtlMerged);
    QCOMPARE(rtlMerged->direction, PDFOCRTextDirection::RightToLeft);
    QCOMPARE(wordIds(*rtlMerged), (std::vector<int>{ abId, cdId, efId, ghId }));
    QCOMPARE(rtlMerged->quad.boundingRect(), QRectF(50, 100, 90, 32));
    QCOMPARE(rtlMerged->baseline.p1(), rtlLineA.baseline.p1());
    QCOMPARE(rtlMerged->baseline.p2(), rtlLineB.baseline.p2());
    QVERIFY(PDFOCRValidator::validate(*session.getPage(1)).isEmpty());

    int rtlSplitId = 0;
    QVERIFY(session.splitLine(1, rtlMergedId, cdId, &rtlSplitId));
    const PDFOCRLine* rtlSplit = session.getPage(1)->findLine(rtlSplitId);
    QVERIFY(rtlSplit);
    QCOMPARE(rtlSplit->direction, PDFOCRTextDirection::RightToLeft);
    QCOMPARE(wordIds(*rtlSplit), (std::vector<int>{ efId, ghId }));
    QCOMPARE(rtlSplit->quad.boundingRect(), QRectF(50, 100, 90, 12));
    QCOMPARE(rtlSplit->baseline, rtlLineB.baseline);
    QCOMPARE(session.getPage(1)->findLine(rtlMergedId)->baseline, rtlLineA.baseline);
    QVERIFY(PDFOCRValidator::validate(*session.getPage(1)).isEmpty());

    // Lines of different directions are not merged
    int ltrLineId = 0;
    QVERIFY(session.insertLine(1, rtlBlock.id, QStringLiteral("ltr"), PDFOCRQuad::fromRect(QRectF(50, 80, 40, 12)), &ltrLineId));
    QVERIFY(!session.mergeLines(1, rtlSplitId, ltrLineId, nullptr));
}

// -------------------------------------------------------------------------
// EDIT-05: phrase search across the word boundaries
// -------------------------------------------------------------------------

void OCRTest::phraseFindAndReplace()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("The"), QRectF(10, 100, 30, 12) },
                                                  { QStringLiteral("quick"), QRectF(45, 100, 50, 12) },
                                                  { QStringLiteral("brown"), QRectF(100, 100, 50, 12) },
                                                  { QStringLiteral("fox"), QRectF(155, 100, 30, 12) } }));
    session.setPageResult(createSampleResult(1, { { QStringLiteral("quick"), QRectF(10, 100, 50, 12) }, { QStringLiteral("brown"), QRectF(70, 100, 50, 12) } }));

    const int lineId = session.getPage(0)->blocks[0].lines[0].id;
    const int theId = session.getPage(0)->getWords()[0]->id;
    const int quickId = session.getPage(0)->getWords()[1]->id;
    const int brownId = session.getPage(0)->getWords()[2]->id;
    const int foxId = session.getPage(0)->getWords()[3]->id;

    PDFOCRSession::FindOptions options;

    // Phrase spanning two words
    std::vector<PDFOCRSession::FindHit> hits = session.find({ 0 }, QStringLiteral("quick brown"), options);
    QCOMPARE(hits.size(), size_t(1));
    QCOMPARE(hits[0].pageIndex, PDFInteger(0));
    QCOMPARE(hits[0].lineId, lineId);
    QCOMPARE(hits[0].wordId, quickId);
    QCOMPARE(hits[0].wordIds, (std::vector<int>{ quickId, brownId }));
    QVERIFY(!hits[0].singleWord);
    QCOMPARE(hits[0].position, 4);
    QCOMPARE(hits[0].length, 11);

    // Hit inside a single word is relative to the word
    hits = session.find({ 0 }, QStringLiteral("row"), options);
    QCOMPARE(hits.size(), size_t(1));
    QCOMPARE(hits[0].wordId, brownId);
    QCOMPARE(hits[0].wordIds, (std::vector<int>{ brownId }));
    QVERIFY(hits[0].singleWord);
    QCOMPARE(hits[0].position, 1);
    QCOMPARE(hits[0].length, 3);

    // Phrase over three words, case insensitive
    hits = session.find({ 0, 1 }, QStringLiteral("QUICK BROWN"), options);
    QCOMPARE(hits.size(), size_t(2));
    QCOMPARE(hits[1].pageIndex, PDFInteger(1));
    hits = session.find({ 0 }, QStringLiteral("k brown f"), options);
    QCOMPARE(hits.size(), size_t(1));
    QCOMPARE(hits[0].wordIds, (std::vector<int>{ quickId, brownId, foxId }));

    // Whole words apply to the phrase boundaries
    options.wholeWords = true;
    QCOMPARE(session.find({ 0 }, QStringLiteral("quick brown"), options).size(), size_t(1));
    QCOMPARE(session.find({ 0 }, QStringLiteral("uick brown"), options).size(), size_t(0));
    QCOMPARE(session.find({ 0 }, QStringLiteral("quick brow"), options).size(), size_t(0));
    options.wholeWords = false;
    options.caseSensitive = true;
    QCOMPARE(session.find({ 0 }, QStringLiteral("Quick brown"), options).size(), size_t(0));
    options.caseSensitive = false;

    // Discarded words are skipped in the line text
    QVERIFY(session.setWordReviewState(0, brownId, PDFOCRReviewState::Discarded));
    hits = session.find({ 0 }, QStringLiteral("quick fox"), options);
    QCOMPARE(hits.size(), size_t(1));
    QCOMPARE(hits[0].wordIds, (std::vector<int>{ quickId, foxId }));
    session.undo();
    QVERIFY(!session.canUndo());

    // Replacement of the phrase is a single undo step, unchanged words keep identity
    const int replaced = session.replaceAll({ 0, 1 }, QStringLiteral("quick brown"), QStringLiteral("slow red"), options);
    QCOMPARE(replaced, 2);
    std::vector<const PDFOCRWord*> words = session.getPage(0)->getWords();
    QCOMPARE(words.size(), size_t(4));
    QCOMPARE(words[0]->id, theId);
    QCOMPARE(words[1]->text, QStringLiteral("slow"));
    QCOMPARE(words[2]->text, QStringLiteral("red"));
    QCOMPARE(words[3]->id, foxId);
    QCOMPARE(words[1]->predecessorIds, (std::vector<int>{ quickId, brownId }));
    QCOMPARE(words[1]->reviewState, PDFOCRReviewState::Modified);
    QVERIFY(words[1]->quad.isValid());
    QVERIFY(words[1]->quad.points[0].x() >= 44.0);
    QVERIFY(words[2]->quad.points[1].x() <= 150.5);
    QCOMPARE(session.getPage(0)->findLine(lineId)->getText(), QStringLiteral("The slow red fox"));
    QCOMPARE(session.getPage(1)->getWordCount(), 2);
    QCOMPARE(session.getPage(1)->blocks[0].lines[0].getText(), QStringLiteral("slow red"));
    QVERIFY(PDFOCRValidator::validate(*session.getPage(0)).isEmpty());
    QVERIFY(PDFOCRValidator::validate(*session.getPage(1)).isEmpty());

    session.undo();
    QVERIFY(!session.canUndo());
    words = session.getPage(0)->getWords();
    QCOMPARE(words.size(), size_t(4));
    QCOMPARE(words[1]->id, quickId);
    QCOMPARE(words[2]->id, brownId);
    QCOMPARE(words[1]->text, QStringLiteral("quick"));
    QCOMPARE(words[2]->text, QStringLiteral("brown"));
    QCOMPARE(words[1]->reviewState, PDFOCRReviewState::Unreviewed);
    QCOMPARE(session.getPage(1)->blocks[0].lines[0].getText(), QStringLiteral("quick brown"));

    // Single word hits keep the current behaviour (text of the word is changed in place)
    QCOMPARE(session.replaceAll({ 0 }, QStringLiteral("own"), QStringLiteral("ight"), options), 1);
    QCOMPARE(session.getPage(0)->findWord(brownId)->text, QStringLiteral("bright"));
    session.undo();

    // Phrase replaced by a single word and by an empty text
    QCOMPARE(session.replaceAll({ 0 }, QStringLiteral("quick brown fox"), QStringLiteral("dog"), options), 1);
    QCOMPARE(session.getPage(0)->findLine(lineId)->getText(), QStringLiteral("The dog"));
    QCOMPARE(session.getPage(0)->getWords()[0]->id, theId);
    session.undo();
    QCOMPARE(session.replaceAll({ 0 }, QStringLiteral("The quick brown fox"), QString(), options), 1);
    QCOMPARE(session.getPage(0)->getWordCount(), 0);
    QCOMPARE(session.getPage(0)->getWords().size(), size_t(4));
    session.undo();
    QCOMPARE(session.getPage(0)->getWordCount(), 4);
    QVERIFY(!session.canUndo());
}

// -------------------------------------------------------------------------
// EXPORT-02/03: outcomes of the pages survive the project and appear in the report
// -------------------------------------------------------------------------

void OCRTest::projectKeepsPageOutcomes()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } }));

    PDFOCRPageResult skipped;
    skipped.pageIndex = 1;
    skipped.state = PDFOCRPageState::Skipped;
    skipped.skipReason = QStringLiteral("page has text");
    session.setPageResult(skipped);

    PDFOCRPageResult cancelled;
    cancelled.pageIndex = 2;
    cancelled.state = PDFOCRPageState::Cancelled;
    session.setPageResult(cancelled);

    PDFOCRPageResult failed;
    failed.pageIndex = 3;
    failed.state = PDFOCRPageState::Error;
    failed.error = PDFOCRError::create(PDFOCRErrorCode::Timeout, QStringLiteral("took too long"), QStringLiteral("Recognition"));
    session.setPageResult(failed);

    PDFOCRPageResult noText;
    noText.pageIndex = 4;
    noText.state = PDFOCRPageState::NoText;
    noText.skipReason = QStringLiteral("blank page");
    session.setPageResult(noText);

    // Pages without an outcome and without regions are not stored
    session.getOrCreatePage(5);
    session.setPageState(6, PDFOCRPageState::Recognizing);

    PDFOCRProject project = session.createProject({ 0, 1, 2, 3, 4, 5, 6 });
    QCOMPARE(project.pages.size(), size_t(5));
    QCOMPARE(project.pages.at(1).state, PDFOCRPageState::Skipped);
    QCOMPARE(project.pages.at(1).skipReason, QStringLiteral("page has text"));
    QCOMPARE(project.pages.at(2).state, PDFOCRPageState::Cancelled);
    QCOMPARE(project.pages.at(3).state, PDFOCRPageState::Error);
    QCOMPARE(project.pages.at(3).error.message, QStringLiteral("took too long"));
    QCOMPARE(project.pages.at(4).state, PDFOCRPageState::NoText);
    QVERIFY(!project.pages.count(5));
    QVERIFY(!project.pages.count(6));

    QString error;
    PDFOCRProject reloaded;
    QVERIFY2(PDFOCRProjectSerializer::fromBytes(PDFOCRProjectSerializer::toBytes(project), reloaded, &error), qPrintable(error));

    PDFOCRSession loaded(nullptr);
    loaded.loadProject(reloaded, { 0, 1, 2, 3, 4, 5, 6 });
    QCOMPARE(loaded.getPages(), (std::vector<PDFInteger>{ 0, 1, 2, 3, 4 }));
    QCOMPARE(loaded.getPage(1)->state, PDFOCRPageState::Skipped);
    QCOMPARE(loaded.getPage(1)->skipReason, QStringLiteral("page has text"));
    QCOMPARE(loaded.getPage(2)->state, PDFOCRPageState::Cancelled);
    QCOMPARE(loaded.getPage(3)->state, PDFOCRPageState::Error);
    QCOMPARE(loaded.getPage(3)->error.code, PDFOCRErrorCode::Timeout);
    QCOMPARE(loaded.getPage(3)->error.message, QStringLiteral("took too long"));
    QCOMPARE(loaded.getPage(4)->state, PDFOCRPageState::NoText);
    QCOMPARE(loaded.getPage(4)->skipReason, QStringLiteral("blank page"));

    // Export report lists the skipped pages with their reasons
    PDFOCRTextExporter::Options options;
    PDFOCRTextExporter::Report report;
    const QString text = PDFOCRTextExporter::exportText(loaded.getResults({ 0, 1, 2, 3, 4 }), options, &report);
    QVERIFY(text.contains(QStringLiteral("Hello")));
    QCOMPARE(report.exportedPages, (std::vector<PDFInteger>{ 0, 4 }));
    QCOMPARE(report.skippedPages, (std::vector<PDFInteger>{ 1, 2, 3 }));
    QCOMPARE(report.skippedDescriptions.size(), qsizetype(3));
    QVERIFY(report.skippedDescriptions[0].startsWith(PDFTranslationContext::tr("Page %1").arg(2)));
    QVERIFY(report.skippedDescriptions[0].contains(QStringLiteral("page has text")));
    QVERIFY(report.skippedDescriptions[1].startsWith(PDFTranslationContext::tr("Page %1").arg(3)));
    QVERIFY(report.skippedDescriptions[2].contains(QStringLiteral("took too long")));
    QCOMPARE(report.noTextDescriptions.size(), qsizetype(1));
    QVERIFY(report.noTextDescriptions[0].startsWith(PDFTranslationContext::tr("Page %1").arg(5)));
    QVERIFY(report.noTextDescriptions[0].contains(QStringLiteral("blank page")));
    QCOMPARE(PDFOCRTextExporter::getPageStateDescription(*loaded.getPage(2)), PDFTranslationContext::tr("recognition was cancelled"));
}

// -------------------------------------------------------------------------
// OPS-05 / DATA-03: limits of the project input
// -------------------------------------------------------------------------

void OCRTest::projectLimits()
{
    PDFOCRSession session(nullptr);
    session.setPageResult(createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } }));
    const QJsonObject valid = PDFOCRProjectSerializer::projectToJson(session.createProject({ 0 }));

    auto load = [](const QJsonObject& object, QString* error)
    {
        PDFOCRProject project;
        return PDFOCRProjectSerializer::fromBytes(QJsonDocument(object).toJson(QJsonDocument::Compact), project, error);
    };

    QString error;
    QVERIFY2(load(valid, &error), qPrintable(error));

    auto withPage = [&](const QJsonObject& page)
    {
        QJsonObject object = valid;
        QJsonArray pages;
        pages.append(page);
        object[QStringLiteral("pages")] = pages;
        return object;
    };
    const QJsonObject validPage = valid.value(QStringLiteral("pages")).toArray().at(0).toObject();

    // Too many regions
    {
        QJsonObject page = validPage;
        QJsonArray regions;
        PDFOCRRegion region;
        region.rect = QRectF(0, 0, 10, 10);
        for (int i = 0; i <= PDFOCRValidator::MaximumRegionsPerPage; ++i)
        {
            region.id = i + 100;
            regions.append(PDFOCRProjectSerializer::regionToJson(region));
        }
        page[QStringLiteral("regions")] = regions;
        QVERIFY(!load(withPage(page), &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumRegionsPerPage)), qPrintable(error));
    }

    // Too many words
    {
        QJsonObject page = validPage;
        QJsonArray blocks;
        QJsonObject block;
        QJsonArray lines;
        QJsonObject line;
        QJsonArray words;
        QJsonObject word;
        word[QStringLiteral("text")] = QStringLiteral("a");
        for (int i = 0; i <= PDFOCRValidator::MaximumWordsPerPage; ++i)
        {
            words.append(word);
        }
        line[QStringLiteral("words")] = words;
        lines.append(line);
        block[QStringLiteral("lines")] = lines;
        blocks.append(block);
        page[QStringLiteral("blocks")] = blocks;
        QVERIFY(!load(withPage(page), &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumWordsPerPage)), qPrintable(error));

        // The same limit applies to the raw recognition
        page = validPage;
        page[QStringLiteral("originalBlocks")] = blocks;
        QVERIFY(!load(withPage(page), &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumWordsPerPage)), qPrintable(error));
    }

    // Too long word text
    {
        QJsonObject page = validPage;
        QJsonArray blocks = page.value(QStringLiteral("blocks")).toArray();
        QJsonObject block = blocks.at(0).toObject();
        QJsonArray lines = block.value(QStringLiteral("lines")).toArray();
        QJsonObject line = lines.at(0).toObject();
        QJsonArray words = line.value(QStringLiteral("words")).toArray();
        QJsonObject word = words.at(0).toObject();
        word[QStringLiteral("text")] = QString(PDFOCRValidator::MaximumTextLength + 1, QChar('x'));
        words[0] = word;
        line[QStringLiteral("words")] = words;
        lines[0] = line;
        block[QStringLiteral("lines")] = lines;
        blocks[0] = block;
        page[QStringLiteral("blocks")] = blocks;
        QVERIFY(!load(withPage(page), &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumTextLength)), qPrintable(error));

        word[QStringLiteral("text")] = QString(PDFOCRValidator::MaximumTextLength, QChar('x'));
        words[0] = word;
        line[QStringLiteral("words")] = words;
        lines[0] = line;
        block[QStringLiteral("lines")] = lines;
        blocks[0] = block;
        page[QStringLiteral("blocks")] = blocks;
        QVERIFY2(load(withPage(page), &error), qPrintable(error));
    }

    // Too many pages
    {
        QJsonObject object = valid;
        QJsonArray pages;
        for (int i = 0; i <= PDFOCRValidator::MaximumPages; ++i)
        {
            pages.append(QJsonObject());
        }
        object[QStringLiteral("pages")] = pages;
        QVERIFY(!load(object, &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumPages)), qPrintable(error));
    }

    // Too large file is refused before it is read
    {
        QTemporaryDir directory;
        const QString fileName = directory.filePath(QStringLiteral("huge.pdf4qt-ocr"));
        QFile file(fileName);
        QVERIFY(file.open(QFile::WriteOnly));
        QVERIFY(file.resize(PDFOCRValidator::MaximumProjectFileSize + 1));
        file.close();
        PDFOCRProject project;
        QVERIFY(!PDFOCRProjectSerializer::load(fileName, project, &error));
        QVERIFY2(error.contains(QString::number(PDFOCRValidator::MaximumProjectFileSize / (1024 * 1024))), qPrintable(error));
    }

    // Validator reports the same limits
    {
        PDFOCRPageResult page = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } });
        QVERIFY(PDFOCRValidator::validate(page).isEmpty());

        page.blocks[0].lines[0].words[0].text = QString(PDFOCRValidator::MaximumTextLength + 1, QChar('x'));
        QStringList errors = PDFOCRValidator::validate(page);
        QCOMPARE(errors.size(), qsizetype(1));
        QVERIFY2(errors[0].contains(QString::number(PDFOCRValidator::MaximumTextLength)), qPrintable(errors[0]));

        page = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } });
        PDFOCRRegion region;
        region.rect = QRectF(0, 0, 10, 10);
        for (int i = 0; i <= PDFOCRValidator::MaximumRegionsPerPage; ++i)
        {
            region.id = page.allocateId();
            page.regions.push_back(region);
        }
        errors = PDFOCRValidator::validate(page);
        QCOMPARE(errors.size(), qsizetype(1));
        QVERIFY2(errors[0].contains(QString::number(PDFOCRValidator::MaximumRegionsPerPage)), qPrintable(errors[0]));

        PDFOCRValidator::Limits limits;
        limits.maximumWordsPerPage = 0;
        page = createSampleResult(0, { { QStringLiteral("Hello"), QRectF(10, 100, 50, 12) } });
        errors = PDFOCRValidator::validate(page, limits);
        QCOMPARE(errors.size(), qsizetype(1));
        QVERIFY(errors[0].contains(QStringLiteral("Too many words")));
    }
}


// [test functions: engine, controller and models]

// -------------------------------------------------------------------------
// R11: script models of the catalog ("script/Latin") are accepted by the
// validation and passed through to the engine
// -------------------------------------------------------------------------

void OCRTest::scriptModelIdentifiers()
{
    PDFOCRConfiguration configuration;
    configuration.engineId = QStringLiteral("tesseract");

    // Accepted identifiers
    for (const char* language : { "eng", "script/Latin", "script/Cyrillic", "ces@3f2a1b9c", "script/Latin@import_1", "chi_sim" })
    {
        configuration.languages = { QLatin1String(language) };
        const QStringList errors = configuration.validate();
        QVERIFY2(errors.isEmpty(), qPrintable(QLatin1String(language) + QStringLiteral(": ") + errors.join(QStringLiteral("; "))));
        QVERIFY(PDFOCRConfiguration::isValidLanguageIdentifier(QLatin1String(language)));
    }

    // Refused identifiers (path traversal, other directories, separators)
    for (const char* language : { "../x", "a/b", "script/../x", "script/", "script//Latin", "a\\b", "..", "", "script/Latin/x", "/eng", "eng@", "eng@a/b", "1eng", "script/Latin@..", "eng ces" })
    {
        configuration.languages = { QLatin1String(language) };
        const QStringList errors = configuration.validate();
        QVERIFY2(!errors.isEmpty(), language);
        QVERIFY2(!PDFOCRConfiguration::isValidLanguageIdentifier(QLatin1String(language)), language);
    }

    // End-to-end with the test engine: a job with a script model identifier runs
    {
        PageSpec spec;
        spec.content = "0 0 0 rg 20 100 100 30 re f";
        PDFDocument document = createDocument({ spec });
        RenderingContext context(&document);

        std::atomic<bool> sawScriptLanguage = { false };
        m_testEngine->setRecognitionDelay(0);
        m_testEngine->setHandler([&sawScriptLanguage](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
        {
            sawScriptLanguage = input.configuration.languages == QStringList{ QStringLiteral("script/Latin") };
            PDFOCRRecognitionOutput output;
            output.imageSize = input.image.size();
            PDFOCRRawBlock block;
            block.rect = QRectF(0, 0, 100, 20);
            PDFOCRRawLine line;
            line.rect = QRectF(0, 0, 100, 20);
            line.text = QStringLiteral("latin");
            block.lines.push_back(line);
            output.blocks.push_back(block);
            return output;
        });

        PDFOCRJobController controller(nullptr);
        controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

        PDFOCRJobDescription description;
        description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
        description.configuration.languages = { QStringLiteral("script/Latin") };
        description.configuration.workerCount = 1;
        description.configuration.detectBlankPages = false;
        QVERIFY(description.configuration.validate().isEmpty());
        description.models.dataPath = QStringLiteral("/none");
        description.models.languages = { QStringLiteral("script/Latin") };
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
        QVERIFY(sawScriptLanguage);
    }

#ifdef PDF4QT_OCR_TESSERACT
    // The Tesseract adapter passes the identifier through: a model stored under
    // tessdata/script/<Name>.traineddata is loaded by the language string "script/<Name>"
    const QString builtInDirectory = getSourceOcrDirectory();
    const QString englishModel = builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata");
    if (builtInDirectory.isEmpty() || !QFile::exists(englishModel))
    {
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    QTemporaryDir setDirectory;
    QVERIFY(setDirectory.isValid());
    const QString tessdata = setDirectory.filePath(QStringLiteral("tessdata"));
    QVERIFY(QDir().mkpath(tessdata + QStringLiteral("/script")));
    QVERIFY(QFile::copy(englishModel, tessdata + QStringLiteral("/script/Latin.traineddata")));

    std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(QStringLiteral("tesseract"));
    QVERIFY(factory);
    const PDFOCRError validation = factory->validateModel(tessdata, QStringLiteral("script/Latin"));
    QVERIFY2(!validation, qPrintable(validation.message));
    QVERIFY(factory->validateModel(tessdata, QStringLiteral("script/Missing")).code == PDFOCRErrorCode::IncompatibleModel);

    // Recognition of a rendered text with the script model identifier only
    PageSpec textSpec;
    textSpec.size = QSizeF(400, 120);
    textSpec.withHelvetica = true;
    textSpec.content = "BT /F1 28 Tf 30 60 Td (Hello world) Tj ET";
    PDFDocument textDocument = createDocument({ textSpec });
    RenderingContext textContext(&textDocument);
    PDFOCRPagePreparer preparer = textContext.createPreparer(&textDocument);
    PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    QVERIFY(!raster.error);

    std::unique_ptr<PDFOCREngine> engine = factory->createEngine();
    QVERIFY(engine);

    PDFOCRResolvedModelSet models;
    models.dataPath = tessdata;
    models.languages = { QStringLiteral("script/Latin") };
    models.modelIds = { QStringLiteral("tesseract/fast/script/Latin") };

    PDFOCRConfiguration scriptConfiguration;
    scriptConfiguration.engineId = QStringLiteral("tesseract");
    scriptConfiguration.languages = { QStringLiteral("script/Latin") };
    QVERIFY(scriptConfiguration.validate().isEmpty());
    const PDFOCRError prepareError = engine->prepare(scriptConfiguration, models);
    QVERIFY2(!prepareError, qPrintable(prepareError.message));

    PDFOCRRecognitionInput input;
    input.image = raster.image;
    input.dpi = raster.geometry.dpi;
    input.configuration = scriptConfiguration;
    input.models = models;
    const PDFOCRRecognitionOutput output = engine->recognize(input, nullptr, nullptr);
    QVERIFY2(output.isSuccess(), qPrintable(output.error.message));

    QStringList words;
    for (const PDFOCRRawBlock& block : output.blocks)
    {
        for (const PDFOCRRawLine& line : block.lines)
        {
            for (const PDFOCRRawWord& word : line.words)
            {
                words << word.text;
            }
        }
    }
    QVERIFY2(words.join(QChar(' ')).contains(QStringLiteral("Hello")), qPrintable(words.join(QChar(' '))));
    engine->release();
#endif
}

// -------------------------------------------------------------------------
// REC-03: typed schema of the engine parameters
// -------------------------------------------------------------------------

void OCRTest::engineParameterSchema()
{
    // Engine independent helper
    std::vector<PDFOCREngineParameterDescriptor> descriptors;
    {
        PDFOCREngineParameterDescriptor boolean;
        boolean.name = QStringLiteral("flag");
        boolean.type = PDFOCREngineParameterDescriptor::Type::Boolean;
        descriptors.push_back(boolean);

        PDFOCREngineParameterDescriptor integer;
        integer.name = QStringLiteral("count");
        integer.type = PDFOCREngineParameterDescriptor::Type::Integer;
        integer.minimum = 1;
        integer.maximum = 10;
        descriptors.push_back(integer);

        PDFOCREngineParameterDescriptor real;
        real.name = QStringLiteral("ratio");
        real.type = PDFOCREngineParameterDescriptor::Type::Double;
        real.minimum = 0.5;
        real.maximum = 2.0;
        descriptors.push_back(real);

        PDFOCREngineParameterDescriptor text;
        text.name = QStringLiteral("name");
        text.type = PDFOCREngineParameterDescriptor::Type::String;
        descriptors.push_back(text);
    }

    QStringList errors;
    QVariantMap valid;
    valid[QStringLiteral("flag")] = true;
    valid[QStringLiteral("count")] = 5;
    valid[QStringLiteral("ratio")] = QStringLiteral("1.5");
    valid[QStringLiteral("name")] = QStringLiteral("x");
    QCOMPARE(PDFOCRConfiguration::validateEngineParameters(valid, descriptors, &errors).size(), 4);
    QVERIFY(errors.isEmpty());

    QVariantMap invalid;
    invalid[QStringLiteral("unknown")] = 1;
    invalid[QStringLiteral("flag")] = QStringLiteral("maybe");
    invalid[QStringLiteral("count")] = 11;
    invalid[QStringLiteral("ratio")] = QStringLiteral("abc");
    QVERIFY(PDFOCRConfiguration::validateEngineParameters(invalid, descriptors, &errors).isEmpty());
    QCOMPARE(errors.size(), 4);
    QVERIFY(errors.join(QChar('\n')).contains(QStringLiteral("unknown")));
    QVERIFY(errors.join(QChar('\n')).contains(QStringLiteral("count")));

#ifndef PDF4QT_OCR_TESSERACT
    PDF4QT_OCR_SKIP("Tesseract engine is not compiled in.");
#else
    const QString builtInDirectory = getSourceOcrDirectory();
    if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(QStringLiteral("tesseract"));
    QVERIFY(factory);
    const PDFOCREngineCapabilities capabilities = factory->getCapabilities();
    QVERIFY(!capabilities.parameters.empty());
    QVERIFY(!capabilities.orientationDetectionCancellable);

    PDFOCRResolvedModelSet models;
    models.dataPath = builtInDirectory + QStringLiteral("/tesseract/fast/tessdata");
    models.languages = { QStringLiteral("eng") };
    models.modelIds = { QStringLiteral("tesseract/fast/eng") };

    PDFOCRConfiguration configuration;
    configuration.engineId = QStringLiteral("tesseract");
    configuration.languages = { QStringLiteral("eng") };

    std::unique_ptr<PDFOCREngine> engine = factory->createEngine();
    QVERIFY(engine);

    // Unknown parameter is refused by name
    configuration.engineParameters.clear();
    configuration.engineParameters[QStringLiteral("tessedit_write_images")] = true;
    PDFOCRError error = engine->validateConfiguration(configuration, models);
    QCOMPARE(error.code, PDFOCRErrorCode::InvalidConfiguration);
    QVERIFY2(error.message.contains(QStringLiteral("tessedit_write_images")), qPrintable(error.message));
    QVERIFY(engine->prepare(configuration, models).code == PDFOCRErrorCode::InvalidConfiguration);

    // Out of range
    configuration.engineParameters.clear();
    configuration.engineParameters[QStringLiteral("user_defined_dpi")] = 10000;
    error = engine->validateConfiguration(configuration, models);
    QCOMPARE(error.code, PDFOCRErrorCode::InvalidConfiguration);
    QVERIFY2(error.message.contains(QStringLiteral("user_defined_dpi")), qPrintable(error.message));

    // Wrong type
    configuration.engineParameters.clear();
    configuration.engineParameters[QStringLiteral("lstm_choice_mode")] = QStringLiteral("two");
    error = engine->validateConfiguration(configuration, models);
    QCOMPARE(error.code, PDFOCRErrorCode::InvalidConfiguration);
    QVERIFY2(error.message.contains(QStringLiteral("lstm_choice_mode")), qPrintable(error.message));

    // Every parameter of the schema is accepted by the engine with its default value
    configuration.engineParameters.clear();
    for (const PDFOCREngineParameterDescriptor& descriptor : capabilities.parameters)
    {
        QVERIFY(!descriptor.name.isEmpty());
        QVERIFY(!descriptor.description.isEmpty());
        configuration.engineParameters[descriptor.name] = descriptor.defaultValue;
    }
    configuration.engineParameters[QStringLiteral("user_defined_dpi")] = 300;
    configuration.engineParameters[QStringLiteral("preserve_interword_spaces")] = QStringLiteral("true");
    configuration.engineParameters[QStringLiteral("textord_min_linesize")] = 2.5;
    error = engine->validateConfiguration(configuration, models);
    QVERIFY2(!error, qPrintable(error.message));
    error = engine->prepare(configuration, models);
    QVERIFY2(!error, qPrintable(error.message));
    engine->release();
#endif
}

// -------------------------------------------------------------------------
// R12: shared deadline of the page
// -------------------------------------------------------------------------

void OCRTest::pageTimeout()
{
    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec, spec });
    RenderingContext context(&document);

    std::atomic<qint64> remainingOfSecondPage = { -2 };
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([&remainingOfSecondPage](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        if (input.configuration.engineParameters.value(QStringLiteral("page")).toInt() == 0)
        {
            // Engine, which does not honour the deadline itself
            QThread::msleep(3000);
        }
        else
        {
            remainingOfSecondPage = input.remainingMilliseconds;
        }

        PDFOCRRawBlock block;
        block.rect = QRectF(0, 0, 100, 20);
        PDFOCRRawLine line;
        line.rect = QRectF(0, 0, 100, 20);
        line.text = QStringLiteral("late");
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
    description.configuration.pageTimeoutSeconds = 1;
    description.models.dataPath = QStringLiteral("/none");
    description.models.languages = { QStringLiteral("eng") };
    for (PDFInteger page : { 0, 1 })
    {
        PDFOCRPageTask task;
        task.pageIndex = page;
        task.configuration = description.configuration;
        task.configuration.engineParameters[QStringLiteral("page")] = int(page);
        task.generation = 1;
        description.pages.push_back(task);
    }

    std::map<PDFInteger, PDFOCRPageResult> results;
    std::optional<PDFOCRJobSummary> summary;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult result) { results[result.pageIndex] = std::move(result); });
    connect(&controller, &PDFOCRJobController::jobFinished, this, [&](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });

    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 20000);
    controller.waitForFinished();

    // The slow page ends with a timeout, the job continues with the next page
    QCOMPARE(results.size(), size_t(2));
    QCOMPARE(results[0].state, PDFOCRPageState::Error);
    QCOMPARE(results[0].error.code, PDFOCRErrorCode::Timeout);
    QVERIFY(!results[0].hasResult());
    QCOMPARE(results[1].state, PDFOCRPageState::Done);
    QCOMPARE(summary->errorPages, 1);
    QCOMPARE(summary->donePages, 1);
    QVERIFY(!summary->cancelled);
    QVERIFY(remainingOfSecondPage > 0 && remainingOfSecondPage <= 1000);

    // An engine honouring the deadline (simulated recognition) reports the timeout itself
    m_testEngine->setRecognitionDelay(3000);
    results.clear();
    summary.reset();
    description.pages.resize(1);
    description.pages[0].configuration.engineParameters[QStringLiteral("page")] = 1;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 20000);
    controller.waitForFinished();
    m_testEngine->setRecognitionDelay(0);
    QCOMPARE(results[0].error.code, PDFOCRErrorCode::Timeout);
    QVERIFY(timer.elapsed() < 2500);

    // Without a timeout the slow engine finishes normally
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        return output;
    });
    results.clear();
    summary.reset();
    description.configuration.pageTimeoutSeconds = 0;
    description.pages[0].configuration.pageTimeoutSeconds = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 20000);
    controller.waitForFinished();
    QCOMPARE(results[0].state, PDFOCRPageState::NoText);
}

// -------------------------------------------------------------------------
// R12: the verification and the installation of a download run off the caller thread
// -------------------------------------------------------------------------

void OCRTest::downloadVerificationThread()
{
    TestHttpServer server;
    const QByteArray goodModel = QByteArray("TESSDATA-TEST-MODEL-") + QByteArray(5000, 'x');
    const QByteArray goodHash = QCryptographicHash::hash(goodModel, QCryptographicHash::Sha256).toHex();
    server.setResponse(QStringLiteral("/thr.traineddata"), { 200, "application/octet-stream", goodModel, false });

    PDFOCRCatalogEntry catalogEntry;
    catalogEntry.id = QStringLiteral("tesseract/fast/thr");
    catalogEntry.engineId = QStringLiteral("tesseract");
    catalogEntry.language = QStringLiteral("thr");
    catalogEntry.name = QStringLiteral("thr");
    catalogEntry.profile = PDFOCRModelProfile::Fast;
    catalogEntry.family = QStringLiteral("language");
    catalogEntry.version = QStringLiteral("v1");
    catalogEntry.url = server.url(QStringLiteral("/thr.traineddata"));
    catalogEntry.fileName = QStringLiteral("thr.traineddata");
    catalogEntry.size = goodModel.size();
    catalogEntry.sha256 = QString::fromLatin1(goodHash);

    PDFOCRCatalog catalog;
    catalog.version = 1;
    catalog.engineId = QStringLiteral("tesseract");
    catalog.sourceCommits[QStringLiteral("fast")] = QStringLiteral("0123456789abcdef");
    catalog.entries.push_back(catalogEntry);

    QTemporaryDir userDirectory;
    QVERIFY(userDirectory.isValid());

    std::atomic<QThread*> validatorThread = { nullptr };
    std::vector<PDFOCRModelState> observedStates;

    PDFOCRModelManager manager(nullptr);
    manager.setAllowInsecureLoopback(true);
    manager.setUserDirectory(userDirectory.path());
    manager.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
    manager.setModelValidator([&validatorThread](const QString&, const QString&, const QString&)
    {
        validatorThread = QThread::currentThread();
        QThread::msleep(200);
        return PDFOCRError::none();
    });
    manager.setCatalog(catalog);

    connect(&manager, &PDFOCRModelManager::modelStateChanged, this, [&](const QString& id)
    {
        if (std::optional<PDFOCRModelInfo> model = manager.getModel(id))
        {
            observedStates.push_back(model->state);
        }
    });

    std::optional<std::pair<bool, QString>> finished;
    connect(&manager, &PDFOCRModelManager::downloadFinished, this, [&finished](const QString&, bool success, const QString& message) { finished = std::make_pair(success, message); });

    manager.download({ QStringLiteral("tesseract/fast/thr") });
    QVERIFY(manager.isDownloading());
    QTRY_VERIFY_WITH_TIMEOUT(finished.has_value(), 15000);
    QVERIFY2(finished->first, qPrintable(finished->second));
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDownloading(), 5000);

    // The validator ran in a worker, not in the thread of the manager
    QVERIFY(validatorThread.load() != nullptr);
    QVERIFY(validatorThread.load() != QThread::currentThread());

    // States: Downloading -> Verifying -> Installed
    QVERIFY(std::find(observedStates.begin(), observedStates.end(), PDFOCRModelState::Verifying) != observedStates.end());
    QVERIFY(std::find(observedStates.begin(), observedStates.end(), PDFOCRModelState::Downloading) != observedStates.end());
    std::optional<PDFOCRModelInfo> model = manager.getModel(QStringLiteral("tesseract/fast/thr"));
    QVERIFY(model.has_value());
    QCOMPARE(model->state, PDFOCRModelState::Installed);
    QVERIFY(QFile::exists(model->path));
    QVERIFY(QFile::exists(model->path + QStringLiteral(".sha256")));
    QVERIFY(QFile::exists(model->path + QStringLiteral(".meta.json")));
}

// -------------------------------------------------------------------------
// R13: memory budget and engine limits are hard limits
// -------------------------------------------------------------------------

void OCRTest::memoryBudgetAndEngineLimits()
{
    PageSpec largePage;
    largePage.size = QSizeF(595, 842);
    largePage.content = "0 0 0 rg 20 100 100 30 re f";
    PageSpec smallPage;
    smallPage.size = QSizeF(100, 100);
    smallPage.content = "0 0 0 rg 20 20 50 30 re f";
    PDFDocument document = createDocument({ largePage, smallPage });
    RenderingContext context(&document);

    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        PDFOCRRawBlock block;
        block.rect = QRectF(0, 0, 50, 20);
        PDFOCRRawLine line;
        line.rect = QRectF(0, 0, 50, 20);
        line.text = QStringLiteral("ok");
        block.lines.push_back(line);
        output.blocks.push_back(block);
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    std::map<PDFInteger, PDFOCRPageResult> results;
    std::optional<PDFOCRJobSummary> summary;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult result) { results[result.pageIndex] = std::move(result); });
    connect(&controller, &PDFOCRJobController::jobFinished, this, [&](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });

    auto createDescription = [](double dpi, qint64 memoryBudget, int workerCount)
    {
        PDFOCRJobDescription description;
        description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
        description.configuration.languages = { QStringLiteral("eng") };
        description.configuration.workerCount = workerCount;
        description.configuration.detectBlankPages = false;
        description.configuration.dpi = dpi;
        description.configuration.memoryBudget = memoryBudget;
        description.models.dataPath = QStringLiteral("/none");
        description.models.languages = { QStringLiteral("eng") };
        for (PDFInteger page : { 0, 1 })
        {
            PDFOCRPageTask task;
            task.pageIndex = page;
            task.configuration = description.configuration;
            task.generation = 1;
            description.pages.push_back(task);
        }
        return description;
    };

    auto run = [&](const PDFOCRJobDescription& description)
    {
        results.clear();
        summary.reset();
        int generation = 0;
        QVERIFY(controller.start(description, &generation));
        QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 60000);
        controller.waitForFinished();
    };

    // (a) A4 at 300 DPI needs about 3 x 35 MB of rasters, which exceeds the budget of
    // 64 MiB: the page fails with OutOfMemory (no waiting, no bypass), the small page is fine
    run(createDescription(300.0, qint64(64) << 20, 2));
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(results.size(), size_t(2));
    QCOMPARE(results[0].state, PDFOCRPageState::Error);
    QCOMPARE(results[0].error.code, PDFOCRErrorCode::OutOfMemory);
    QVERIFY2(results[0].error.message.contains(QStringLiteral("MB")), qPrintable(results[0].error.message));
    QCOMPARE(results[1].state, PDFOCRPageState::Done);
    QVERIFY(!summary->criticalError);
    QCOMPARE(summary->errorPages, 1);

    // (c) The dimension limit of the engine limits the raster of a page, which passes the pixel limit
    m_testEngine->setMaximumImageSize(QSize(600, 600));
    run(createDescription(300.0, qint64(1) << 30, 1));
    QVERIFY(!QTest::currentTestFailed());
    m_testEngine->setMaximumImageSize(QSize());
    QCOMPARE(results[0].state, PDFOCRPageState::Done);
    QVERIFY(results[0].geometry.rasterSize.width() <= 600);
    QVERIFY(results[0].geometry.rasterSize.height() <= 600);
    QVERIFY(results[0].geometry.dpi < 300.0);
    QVERIFY2(results[0].geometry.pipeline.join(QChar(' ')).contains(QStringLiteral("dpi-limited")), qPrintable(results[0].geometry.pipeline.join(QChar(' '))));
    QCOMPARE(results[1].geometry.dpi, 300.0);

    // (b) Model memory: 40 MB of models per worker do not fit twice into 64 MiB,
    // so a single worker is used; the raster memory left is still enough for the small page
    QTemporaryDir modelDirectory;
    QVERIFY(modelDirectory.isValid());
    QVERIFY(QDir().mkpath(modelDirectory.filePath(QStringLiteral("tessdata/script"))));
    {
        QFile modelFile(modelDirectory.filePath(QStringLiteral("tessdata/eng.traineddata")));
        QVERIFY(modelFile.open(QFile::WriteOnly));
        QVERIFY(modelFile.resize(qint64(30) << 20));
        QFile scriptFile(modelDirectory.filePath(QStringLiteral("tessdata/script/Latin.traineddata")));
        QVERIFY(scriptFile.open(QFile::WriteOnly));
        QVERIFY(scriptFile.resize(qint64(10) << 20));
    }
    QCOMPARE(PDFOCRJobController::estimateModelBytes(modelDirectory.filePath(QStringLiteral("tessdata"))), qint64(40) << 20);

    PDFOCRJobDescription modelDescription = createDescription(72.0, qint64(64) << 20, 2);
    modelDescription.models.dataPath = modelDirectory.filePath(QStringLiteral("tessdata"));
    run(modelDescription);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(summary->requestedWorkerCount, 2);
    QCOMPARE(summary->workerCount, 1);
    QCOMPARE(summary->modelMemoryBytes, qint64(40) << 20);
    QCOMPARE(results[0].state, PDFOCRPageState::Done);
    QCOMPARE(results[1].state, PDFOCRPageState::Done);
    QVERIFY2(results[1].geometry.pipeline.join(QChar(' ')).contains(QStringLiteral("workers-limited")), qPrintable(results[1].geometry.pipeline.join(QChar(' '))));

    // Models larger than the budget: the job fails with a critical OutOfMemory error
    {
        QFile modelFile(modelDirectory.filePath(QStringLiteral("tessdata/eng.traineddata")));
        QVERIFY(modelFile.open(QFile::WriteOnly));
        QVERIFY(modelFile.resize(qint64(70) << 20));
    }
    run(modelDescription);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(summary->criticalError.code, PDFOCRErrorCode::OutOfMemory);
    QCOMPARE(summary->errorPages, 2);
    QCOMPARE(results[0].state, PDFOCRPageState::Error);
    QCOMPARE(results[0].error.code, PDFOCRErrorCode::OutOfMemory);
    QCOMPARE(results[1].error.code, PDFOCRErrorCode::OutOfMemory);

    // (d) OPS-05: too many blocks are refused
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        PDFOCRRawBlock block;
        block.rect = QRectF(0, 0, 5, 5);
        PDFOCRRawLine line;
        line.rect = QRectF(0, 0, 5, 5);
        line.text = QStringLiteral("x");
        block.lines.push_back(line);
        output.blocks.assign(10001, block);
        return output;
    });
    PDFOCRJobDescription floodDescription = createDescription(72.0, qint64(1) << 30, 1);
    floodDescription.pages.resize(1);
    floodDescription.pages[0].pageIndex = 1;
    run(floodDescription);
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(results[1].state, PDFOCRPageState::Error);
    QCOMPARE(results[1].error.code, PDFOCRErrorCode::InvalidEngineOutput);
}

// -------------------------------------------------------------------------
// REGION-02: rotation override of a region
// -------------------------------------------------------------------------

void OCRTest::regionRotationOverride()
{
    PageSpec spec;
    spec.size = QSizeF(300, 200);
    spec.content = "0 0 0 rg 60 60 80 40 re f";
    PDFDocument document = createDocument({ spec });
    RenderingContext context(&document);

    PDFOCRRegion region;
    region.id = 1;
    region.type = PDFOCRRegionType::Recognize;
    region.rect = QRectF(50, 50, 100, 60);
    region.configuration.rotation = 90;

    // The handler returns one word at a known position of the rotated crop
    const QRectF wordInRotatedCrop(5, 10, 20, 8);
    std::atomic<int> cropWidth = { 0 };
    std::atomic<int> cropHeight = { 0 };
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([&](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        cropWidth = input.image.width();
        cropHeight = input.image.height();
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        PDFOCRRawBlock block;
        block.rect = wordInRotatedCrop;
        PDFOCRRawLine line;
        line.rect = wordInRotatedCrop;
        PDFOCRRawWord word;
        word.text = QStringLiteral("rotated");
        word.rect = wordInRotatedCrop;
        word.rawConfidence = 95.0;
        line.words.push_back(word);
        block.lines.push_back(line);
        output.blocks.push_back(block);
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    description.configuration.languages = { QStringLiteral("eng") };
    description.configuration.workerCount = 1;
    description.configuration.detectBlankPages = false;
    description.configuration.dpi = 72.0;
    description.models.dataPath = QStringLiteral("/none");
    description.models.languages = { QStringLiteral("eng") };
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = description.configuration;
    task.regions = { region };
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRPageResult> result;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = pageResult; });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000);
    controller.waitForFinished();

    QVERIFY2(result->state == PDFOCRPageState::Done, qPrintable(result->error.message));
    const QString pipeline = result->geometry.pipeline.join(QChar(' '));
    QVERIFY2(pipeline.contains(QStringLiteral("region(1,rotation=90,applied=90)")), qPrintable(pipeline));
    QVERIFY(!pipeline.contains(QStringLiteral("rotation-override-ignored")));

    // The recognized rectangle of the region in the engine space (as the controller computes it)
    const QTransform pageToEngine = result->geometry.getPageToEngine();
    const std::vector<std::pair<int, QRect>> rectangles = PDFOCRPagePreparer::getRecognitionRectangles(result->geometry.engineImageSize, pageToEngine, task.regions);
    QCOMPARE(rectangles.size(), size_t(1));
    const QRect cropRect = rectangles[0].second;

    // The crop was rotated by 90 degrees: width and height are swapped
    QCOMPARE(cropWidth.load(), cropRect.height());
    QCOMPARE(cropHeight.load(), cropRect.width());

    // Rotation by 90 degrees clockwise maps a crop point (x, y) to (h - y, x); the inverse
    // maps the rotated rectangle [X1, X2] x [Y1, Y2] to [Y1, Y2] x [h - X2, h - X1]
    const double h = cropRect.height();
    const QRectF expectedEngineRect(cropRect.left() + wordInRotatedCrop.top(),
                                    cropRect.top() + h - wordInRotatedCrop.right(),
                                    wordInRotatedCrop.height(),
                                    wordInRotatedCrop.width());
    const QRectF expectedPageRect = result->geometry.getEngineToPage().mapRect(expectedEngineRect);

    const std::vector<const PDFOCRWord*> words = std::as_const(*result).getWords();
    QCOMPARE(words.size(), size_t(1));
    QCOMPARE(words[0]->text, QStringLiteral("rotated"));
    const QRectF actualPageRect = words[0]->quad.boundingRect();
    QVERIFY2(std::abs(actualPageRect.left() - expectedPageRect.left()) < 0.5, qPrintable(QStringLiteral("%1 vs %2").arg(actualPageRect.left()).arg(expectedPageRect.left())));
    QVERIFY2(std::abs(actualPageRect.top() - expectedPageRect.top()) < 0.5, qPrintable(QStringLiteral("%1 vs %2").arg(actualPageRect.top()).arg(expectedPageRect.top())));
    QVERIFY2(std::abs(actualPageRect.width() - expectedPageRect.width()) < 0.5, qPrintable(QStringLiteral("%1 vs %2").arg(actualPageRect.width()).arg(expectedPageRect.width())));
    QVERIFY2(std::abs(actualPageRect.height() - expectedPageRect.height()) < 0.5, qPrintable(QStringLiteral("%1 vs %2").arg(actualPageRect.height()).arg(expectedPageRect.height())));

    // The word lies inside the region in the page space, at the expected place: 72 DPI
    // and no page rotation, so a crop pixel is a point; the origin of the rotated crop is
    // the top-right corner of the region, its x axis goes downwards and its y axis to the
    // left, so the word near the origin of the rotated crop lies at the left bottom of the region
    QVERIFY2(region.rect.contains(actualPageRect), qPrintable(QStringLiteral("%1,%2 %3x%4").arg(actualPageRect.left()).arg(actualPageRect.top()).arg(actualPageRect.width()).arg(actualPageRect.height())));
    QVERIFY(std::abs(actualPageRect.width() - wordInRotatedCrop.height()) < 0.5);
    QVERIFY(std::abs(actualPageRect.height() - wordInRotatedCrop.width()) < 0.5);
    QVERIFY(actualPageRect.left() > region.rect.left() + 5 && actualPageRect.left() < region.rect.left() + 12);
    QVERIFY(actualPageRect.top() > region.rect.top() && actualPageRect.top() < region.rect.top() + 8);
    QCOMPARE(result->originalBlocks.size(), result->blocks.size());

    // A region without an override is recognized in place (no crop)
    task.regions[0].configuration.rotation = -1;
    description.pages[0] = task;
    result.reset();
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000);
    controller.waitForFinished();
    QCOMPARE(cropWidth.load(), result->geometry.engineImageSize.width());
    QVERIFY(!result->geometry.pipeline.join(QChar(' ')).contains(QStringLiteral("region(1,rotation")));
}

// -------------------------------------------------------------------------
// LANG-07: lease of the runtime set
// -------------------------------------------------------------------------

void OCRTest::runtimeSetLease()
{
    // Fake built-in model: the runtime set is built from it without any engine
    QTemporaryDir builtInDirectory;
    QTemporaryDir userDirectory;
    QVERIFY(builtInDirectory.isValid() && userDirectory.isValid());
    QVERIFY(QDir().mkpath(builtInDirectory.filePath(QStringLiteral("tesseract/fast/tessdata"))));
    {
        QFile modelFile(builtInDirectory.filePath(QStringLiteral("tesseract/fast/tessdata/xyz.traineddata")));
        QVERIFY(modelFile.open(QFile::WriteOnly));
        modelFile.write(QByteArray("TESSDATA-TEST-MODEL-") + QByteArray(1000, 'q'));
    }

    PDFOCRCatalog catalog;
    catalog.version = 1;
    catalog.engineId = QStringLiteral("tesseract");

    auto createManager = [&](PDFOCRModelManager& manager)
    {
        manager.setUserDirectory(userDirectory.path());
        manager.setBuiltInDirectory(builtInDirectory.path());
        manager.setCatalog(catalog);
    };

    PDFOCRModelManager manager(nullptr);
    createManager(manager);
    QVERIFY(manager.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("xyz"), PDFOCRModelProfile::Fast));

    PDFOCRError error;
    const PDFOCRResolvedModelSet set = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("xyz") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(set.isValid(), qPrintable(error.message));
    const QString setDirectory = QFileInfo(set.dataPath).absolutePath();
    QVERIFY(QFile::exists(setDirectory + QStringLiteral("/complete.json")));
    QVERIFY(!PDFOCRModelManager::isRuntimeSetInUse(setDirectory));

    // Data, which are not a runtime set, are not leased
    QVERIFY(!PDFOCRModelManager::acquireRuntimeSetLease(QStringLiteral("/none")));
    QVERIFY(!PDFOCRModelManager::acquireRuntimeSetLease(builtInDirectory.filePath(QStringLiteral("tesseract/fast/tessdata"))));

    // Lease held: the cleanup keeps the set
    std::unique_ptr<QLockFile> lease = PDFOCRModelManager::acquireRuntimeSetLease(set.dataPath);
    QVERIFY(lease);
    QVERIFY(PDFOCRModelManager::isRuntimeSetInUse(setDirectory));
    QVERIFY(!QDir(setDirectory).entryList(QStringList() << QStringLiteral("in-use.*.lock"), QDir::Files).isEmpty());
    PDFOCRError cleanError = manager.cleanRuntimeSets();
    QVERIFY2(!cleanError, qPrintable(cleanError.message));
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/xyz.traineddata")));

    // Lease released: the cleanup removes the set
    lease.reset();
    QVERIFY(!PDFOCRModelManager::isRuntimeSetInUse(setDirectory));
    cleanError = manager.cleanRuntimeSets();
    QVERIFY2(!cleanError, qPrintable(cleanError.message));
    QVERIFY(!QDir(setDirectory).exists());

    // A stale lease of a dead process is ignored and removed
    const PDFOCRResolvedModelSet rebuilt = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("xyz") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(rebuilt.isValid(), qPrintable(error.message));
    {
        QFile staleLease(setDirectory + QStringLiteral("/in-use.999999.1.lock"));
        QVERIFY(staleLease.open(QFile::WriteOnly));
        staleLease.write("999999\nUnitTestsOCR\n\n");
    }
    QVERIFY(!PDFOCRModelManager::isRuntimeSetInUse(setDirectory));
    QVERIFY(!QFile::exists(setDirectory + QStringLiteral("/in-use.999999.1.lock")));

    // Housekeeping of a new instance: an old set in use is kept, an unused one is removed
    auto makeOld = [&]()
    {
        QFile completeFile(setDirectory + QStringLiteral("/complete.json"));
        QVERIFY(completeFile.open(QFile::ReadWrite));
        QVERIFY(completeFile.setFileTime(QDateTime::currentDateTime().addDays(-40), QFileDevice::FileModificationTime));
    };
    makeOld();
    lease = PDFOCRModelManager::acquireRuntimeSetLease(rebuilt.dataPath);
    QVERIFY(lease);
    {
        PDFOCRModelManager housekeeper(nullptr);
        createManager(housekeeper);
        QVERIFY(QFile::exists(rebuilt.dataPath + QStringLiteral("/xyz.traineddata")));
    }
    lease.reset();
    {
        PDFOCRModelManager housekeeper(nullptr);
        createManager(housekeeper);
        QVERIFY(!QDir(setDirectory).exists());
    }

    // A running job holds the lease of its set until it finishes
    const PDFOCRResolvedModelSet jobSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("xyz") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(jobSet.isValid(), qPrintable(error.message));

    PageSpec spec;
    spec.content = "0 0 0 rg 20 100 100 30 re f";
    PDFDocument document = createDocument({ spec });
    RenderingContext context(&document);

    m_testEngine->setRecognitionDelay(1500);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        return output;
    });

    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    description.configuration.languages = { QStringLiteral("xyz") };
    description.configuration.workerCount = 1;
    description.configuration.detectBlankPages = false;
    description.models = jobSet;
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = description.configuration;
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRJobSummary> summary;
    connect(&controller, &PDFOCRJobController::jobFinished, this, [&](int, PDFOCRJobSummary jobSummary) { summary = jobSummary; });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTest::qWait(300);
    QVERIFY(PDFOCRModelManager::isRuntimeSetInUse(setDirectory));
    cleanError = manager.cleanRuntimeSets();
    QVERIFY2(!cleanError, qPrintable(cleanError.message));
    QVERIFY(QFile::exists(jobSet.dataPath + QStringLiteral("/xyz.traineddata")));

    QTRY_VERIFY_WITH_TIMEOUT(summary.has_value(), 10000);
    controller.waitForFinished();
    m_testEngine->setRecognitionDelay(0);
    QVERIFY(!PDFOCRModelManager::isRuntimeSetInUse(setDirectory));
    cleanError = manager.cleanRuntimeSets();
    QVERIFY2(!cleanError, qPrintable(cleanError.message));
    QVERIFY(!QDir(setDirectory).exists());
}

// -------------------------------------------------------------------------
// LANG-06/08/12: dependencies, incompatible models, engine upgrade
// -------------------------------------------------------------------------

void OCRTest::modelDependenciesAndCompatibility()
{
    TestHttpServer server;

    const QByteArray goodModel = QByteArray("TESSDATA-TEST-MODEL-") + QByteArray(5000, 'x');
    const QByteArray goodHash = QCryptographicHash::hash(goodModel, QCryptographicHash::Sha256).toHex();
    const QByteArray incompatibleModel = QByteArray("TESSDATA-OTHER-FORMAT-") + QByteArray(5000, 'z');
    const QByteArray incompatibleHash = QCryptographicHash::hash(incompatibleModel, QCryptographicHash::Sha256).toHex();

    server.setResponse(QStringLiteral("/dep.traineddata"), { 200, "application/octet-stream", goodModel, false });
    server.setResponse(QStringLiteral("/main.traineddata"), { 200, "application/octet-stream", goodModel, false });
    server.setResponse(QStringLiteral("/incompatible.traineddata"), { 200, "application/octet-stream", incompatibleModel, false });

    auto entry = [&server](const QString& language, const QString& path, const QByteArray& body, const QByteArray& hash)
    {
        PDFOCRCatalogEntry catalogEntry;
        catalogEntry.id = QStringLiteral("tesseract/fast/") + language;
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
        return catalogEntry;
    };

    PDFOCRCatalog catalog;
    catalog.version = 1;
    catalog.engineId = QStringLiteral("tesseract");
    catalog.sourceCommits[QStringLiteral("fast")] = QStringLiteral("0123456789abcdef");
    catalog.entries.push_back(entry(QStringLiteral("dep"), QStringLiteral("/dep.traineddata"), goodModel, goodHash));
    catalog.entries.push_back(entry(QStringLiteral("main"), QStringLiteral("/main.traineddata"), goodModel, goodHash));
    catalog.entries.back().dependencies = { QStringLiteral("tesseract/fast/dep") };
    catalog.entries.push_back(entry(QStringLiteral("incompatible"), QStringLiteral("/incompatible.traineddata"), incompatibleModel, incompatibleHash));

    auto testValidator = [](const QString&, const QString& dataPath, const QString& language)
    {
        QFile file(dataPath + QStringLiteral("/") + language + QStringLiteral(".traineddata"));
        if (!file.open(QFile::ReadOnly) || !file.read(20).startsWith("TESSDATA-TEST-MODEL"))
        {
            return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel, QStringLiteral("Model cannot be loaded by the engine."));
        }
        return PDFOCRError::none();
    };

    QTemporaryDir userDirectory;
    QVERIFY(userDirectory.isValid());
    PDFOCRModelManager manager(nullptr);
    manager.setAllowInsecureLoopback(true);
    manager.setMaximumParallelDownloads(1);
    manager.setUserDirectory(userDirectory.path());
    manager.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
    manager.setModelValidator(testValidator);
    manager.setCatalog(catalog);

    QStringList finishedOrder;
    std::map<QString, std::pair<bool, QString>> finished;
    connect(&manager, &PDFOCRModelManager::downloadFinished, this, [&](const QString& id, bool success, const QString& message)
    {
        finishedOrder << id;
        finished[id] = { success, message };
    });

    // The dependency is missing too
    const QStringList missing = manager.getMissingModels(QStringLiteral("tesseract"), { QStringLiteral("main") }, PDFOCRModelProfile::Fast);
    QVERIFY(missing.contains(QStringLiteral("tesseract/fast/main")));
    QVERIFY(missing.contains(QStringLiteral("tesseract/fast/dep")));

    // Download of the model enqueues the dependency first (LANG-06)
    manager.download({ QStringLiteral("tesseract/fast/main") });
    QCOMPARE(manager.getDownloadQueue(), QStringList({ QStringLiteral("tesseract/fast/dep"), QStringLiteral("tesseract/fast/main") }));
    QTRY_VERIFY_WITH_TIMEOUT(finished.count(QStringLiteral("tesseract/fast/main")) > 0, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDownloading(), 5000);
    QCOMPARE(finishedOrder, QStringList({ QStringLiteral("tesseract/fast/dep"), QStringLiteral("tesseract/fast/main") }));
    QVERIFY2(finished[QStringLiteral("tesseract/fast/dep")].first, qPrintable(finished[QStringLiteral("tesseract/fast/dep")].second));
    QVERIFY2(finished[QStringLiteral("tesseract/fast/main")].first, qPrintable(finished[QStringLiteral("tesseract/fast/main")].second));
    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/dep"))->state, PDFOCRModelState::Installed);
    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/main"))->state, PDFOCRModelState::Installed);
    QVERIFY(manager.getMissingModels(QStringLiteral("tesseract"), { QStringLiteral("main") }, PDFOCRModelProfile::Fast).isEmpty());

    // The runtime set of the model contains its dependency (LANG-06)
    PDFOCRError error;
    const PDFOCRResolvedModelSet set = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("main") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY2(set.isValid(), qPrintable(error.message));
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/main.traineddata")));
    QVERIFY(QFile::exists(set.dataPath + QStringLiteral("/dep.traineddata")));
    QVERIFY(set.modelIds.contains(QStringLiteral("tesseract/fast/dep")));
    QCOMPARE(set.languages, QStringList({ QStringLiteral("main"), QStringLiteral("dep") }));

    // A removed dependency is reported by name, the set is not silently built without it
    const std::optional<PDFOCRModelInfo> dependency = manager.getModel(QStringLiteral("tesseract/fast/dep"));
    QVERIFY(dependency.has_value());
    const PDFOCRError removeError = manager.removeUserModel(QStringLiteral("tesseract/fast/dep"));
    QVERIFY2(!removeError, qPrintable(removeError.message));
    const PDFOCRResolvedModelSet incompleteSet = manager.resolveModelSet(QStringLiteral("tesseract"), { QStringLiteral("main") }, PDFOCRModelProfile::Fast, &error);
    QVERIFY(!incompleteSet.isValid());
    QCOMPARE(error.code, PDFOCRErrorCode::MissingModel);
    QVERIFY2(error.message.contains(QStringLiteral("tesseract/fast/dep")), qPrintable(error.message));

    // A model, which the engine cannot load, ends in the state Incompatible and is not installed (LANG-08)
    finished.clear();
    manager.download({ QStringLiteral("tesseract/fast/incompatible") });
    QTRY_VERIFY_WITH_TIMEOUT(finished.count(QStringLiteral("tesseract/fast/incompatible")) > 0, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDownloading(), 5000);
    QVERIFY(!finished[QStringLiteral("tesseract/fast/incompatible")].first);
    QVERIFY2(finished[QStringLiteral("tesseract/fast/incompatible")].second.contains(QStringLiteral("cannot be loaded")), qPrintable(finished[QStringLiteral("tesseract/fast/incompatible")].second));
    std::optional<PDFOCRModelInfo> incompatible = manager.getModel(QStringLiteral("tesseract/fast/incompatible"));
    QVERIFY(incompatible.has_value());
    QCOMPARE(incompatible->state, PDFOCRModelState::Incompatible);
    QVERIFY(!incompatible->isUsable());
    QVERIFY(incompatible->path.isEmpty());
    QVERIFY(!incompatible->errorMessage.isEmpty());
    QVERIFY(!QFile::exists(QFileInfo(manager.getModel(QStringLiteral("tesseract/fast/main"))->path).absolutePath() + QStringLiteral("/incompatible.traineddata")));

    // The state survives a refresh
    manager.refresh();
    QCOMPARE(manager.getModel(QStringLiteral("tesseract/fast/incompatible"))->state, PDFOCRModelState::Incompatible);

    // Upgrade check (LANG-12): the engine version is recorded at the installation; a model
    // installed for a different major version of the engine is incompatible after a restart
    std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(QStringLiteral("tesseract"));
    const QString mainPath = manager.getModel(QStringLiteral("tesseract/fast/main"))->path;
    const QString metadataPath = mainPath + QStringLiteral(".meta.json");
    QVERIFY(QFile::exists(metadataPath));
    QJsonObject metadata;
    {
        QFile metadataFile(metadataPath);
        QVERIFY(metadataFile.open(QFile::ReadOnly));
        metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    }
    QCOMPARE(metadata.value(QStringLiteral("engineId")).toString(), QStringLiteral("tesseract"));
    QCOMPARE(metadata.value(QStringLiteral("engineVersion")).toString(), factory ? factory->getVersion() : QString());

    if (factory)
    {
        {
            PDFOCRModelManager restarted(nullptr);
            restarted.setUserDirectory(userDirectory.path());
            restarted.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
            restarted.setModelValidator(testValidator);
            restarted.setCatalog(catalog);
            QCOMPARE(restarted.getModel(QStringLiteral("tesseract/fast/main"))->state, PDFOCRModelState::Installed);
        }

        metadata[QStringLiteral("engineVersion")] = QStringLiteral("4.1.1");
        {
            QFile metadataFile(metadataPath);
            QVERIFY(metadataFile.open(QFile::WriteOnly | QFile::Truncate));
            metadataFile.write(QJsonDocument(metadata).toJson());
        }

        PDFOCRModelManager upgraded(nullptr);
        upgraded.setUserDirectory(userDirectory.path());
        upgraded.setBuiltInDirectory(userDirectory.filePath(QStringLiteral("no-builtin")));
        upgraded.setModelValidator(testValidator);
        upgraded.setCatalog(catalog);
        std::optional<PDFOCRModelInfo> upgradedMain = upgraded.getModel(QStringLiteral("tesseract/fast/main"));
        QVERIFY(upgradedMain.has_value());
        QCOMPARE(upgradedMain->state, PDFOCRModelState::Incompatible);
        QVERIFY(!upgradedMain->isUsable());
        QVERIFY2(upgradedMain->errorMessage.contains(QStringLiteral("4.1.1")), qPrintable(upgradedMain->errorMessage));
        QVERIFY(!upgraded.isLanguageUsable(QStringLiteral("tesseract"), QStringLiteral("main"), PDFOCRModelProfile::Fast));

        // A new download repairs the model
        finished.clear();
        upgraded.setAllowInsecureLoopback(true);
        connect(&upgraded, &PDFOCRModelManager::downloadFinished, this, [&](const QString& id, bool success, const QString& message) { finished[id] = { success, message }; });
        upgraded.download({ QStringLiteral("tesseract/fast/main") });
        QTRY_VERIFY_WITH_TIMEOUT(finished.count(QStringLiteral("tesseract/fast/main")) > 0, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!upgraded.isDownloading(), 5000);
        QVERIFY2(finished[QStringLiteral("tesseract/fast/main")].first, qPrintable(finished[QStringLiteral("tesseract/fast/main")].second));
        QCOMPARE(upgraded.getModel(QStringLiteral("tesseract/fast/main"))->state, PDFOCRModelState::Installed);
    }
}

// -------------------------------------------------------------------------
// Extensions of OCR_PLAN.md
// -------------------------------------------------------------------------

PDFDocument OCRTest::createScanDocument(const PageSpec& spec)
{
    PDFDocument textDocument = createDocument({ spec });
    RenderingContext textContext(&textDocument);
    PDFOCRPagePreparer textPreparer = textContext.createPreparer(&textDocument);
    PDFOCRPagePreparer::RasterResult raster = textPreparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    if (raster.error)
    {
        return PDFDocument();
    }
    return createImageDocument(raster.image.convertToFormat(QImage::Format_RGB888), spec.size);
}

std::optional<PDFOCRPageResult> OCRTest::recognizeWithTesseract(const PDFDocument& document, PDFOCRConfiguration configuration, QString* skipReason, QString* errorMessage)
{
#ifndef PDF4QT_OCR_TESSERACT
    Q_UNUSED(document);
    Q_UNUSED(configuration);
    Q_UNUSED(errorMessage);
    *skipReason = QStringLiteral("Tesseract engine is not compiled in.");
    return std::nullopt;
#else
    const QString builtInDirectory = getSourceOcrDirectory();
    for (const QString& language : configuration.languages)
    {
        if (builtInDirectory.isEmpty() || !QFile::exists(builtInDirectory + QStringLiteral("/tesseract/fast/tessdata/%1.traineddata").arg(language)))
        {
            *skipReason = QStringLiteral("Built-in OCR language model '%1' is not available (ocr/tesseract/fast/tessdata).").arg(language);
            return std::nullopt;
        }
    }

    QTemporaryDir userDirectory;
    PDFOCRModelManager manager(nullptr);
    manager.setBuiltInDirectory(builtInDirectory);
    manager.setUserDirectory(userDirectory.path());
    manager.loadBundledCatalog();

    PDFOCRError error;
    configuration.engineId = QStringLiteral("tesseract");
    PDFOCRResolvedModelSet models = manager.resolveModelSet(configuration.engineId, configuration.languages, PDFOCRModelProfile::Fast, &error);
    if (!models.isValid())
    {
        *errorMessage = error.message;
        return std::nullopt;
    }

    RenderingContext context(&document);
    PDFOCRJobController controller(nullptr);
    controller.setEnvironment(&document, &context.m_fontCache, &context.m_cms, &context.m_optionalContentActivity, &context.m_meshQualitySettings, RendererEngine::QPainter);

    PDFOCRJobDescription description;
    description.configuration = configuration;
    description.configuration.workerCount = 1;
    description.models = models;
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = description.configuration;
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRPageResult> result;
    QObject receiver;
    QObject::connect(&controller, &PDFOCRJobController::pageFinished, &receiver, [&](int, PDFOCRPageResult pageResult) { result = std::move(pageResult); });
    int generation = 0;
    if (!controller.start(description, &generation))
    {
        *errorMessage = QStringLiteral("Recognition job was not started.");
        return std::nullopt;
    }

    QElapsedTimer timer;
    timer.start();
    while (!result.has_value() && timer.elapsed() < 120000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    controller.waitForFinished();

    if (!result.has_value())
    {
        *errorMessage = QStringLiteral("Recognition did not finish in time.");
    }
    else if (result->state != PDFOCRPageState::Done)
    {
        *errorMessage = result->error.message;
    }
    return result;
#endif
}

void OCRTest::dictionaryInformation()
{
    // Engine output with the dictionary information: a dictionary word, a word
    // outside the dictionary, a number (unknown) and a word with punctuation
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;

        PDFOCRRawBlock block;
        block.rect = QRectF(10, 10, 400, 40);
        PDFOCRRawLine line;
        line.rect = block.rect;

        auto addWord = [&line](QString text, double x, std::optional<bool> dictionary)
        {
            PDFOCRRawWord word;
            word.text = std::move(text);
            word.rect = QRectF(x, 10, 60, 40);
            word.rawConfidence = 95.0;
            word.isDictionaryWord = dictionary;
            line.words.push_back(std::move(word));
        };

        addWord(QStringLiteral("document"), 10, true);
        addWord(QStringLiteral("qxzvbl"), 80, false);
        addWord(QStringLiteral("2026"), 150, std::nullopt);
        addWord(QStringLiteral("(Pdfforqt),"), 220, false);
        addWord(QStringLiteral("x"), 290, false);
        addWord(QStringLiteral("--"), 360, false);
        block.lines.push_back(std::move(line));
        output.blocks.push_back(std::move(block));
        return output;
    });

    PageSpec spec;
    spec.size = QSizeF(500, 100);
    spec.withImage = true;
    PDFDocument document = createDocument({ spec });
    RenderingContext context(&document);

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
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = std::move(pageResult); });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 30000);
    controller.waitForFinished();
    m_testEngine->setHandler(PDFOCRTestEngineFactory::Handler());
    QVERIFY2(result->state == PDFOCRPageState::Done, qPrintable(result->error.message));

    // The engine information is transferred into the model
    std::vector<PDFOCRWord*> words = result->getWords();
    QCOMPARE(words.size(), size_t(6));
    QCOMPARE(words[0]->inDictionary, std::optional<bool>(true));
    QCOMPARE(words[1]->inDictionary, std::optional<bool>(false));
    QVERIFY(!words[2]->inDictionary.has_value());
    QCOMPARE(words[3]->inDictionary, std::optional<bool>(false));

    // Only a real word outside the dictionary is a candidate: not a single character,
    // not a word without letters, not a number, not a dictionary word
    PDFOCRReviewCriteria criteria = PDFOCRReviewCriteria::create(50.0, true, QStringList());
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*words[0], criteria));
    QVERIFY(PDFOCRReview::isOutsideDictionary(*words[1], criteria));
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*words[2], criteria));
    QVERIFY(PDFOCRReview::isOutsideDictionary(*words[3], criteria));
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*words[4], criteria));
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*words[5], criteria));

    // The switch decides the review, the statistics count the words always
    QVERIFY(PDFOCRReview::requiresReview(*words[1], criteria));
    QVERIFY(!PDFOCRReview::requiresReview(*words[0], criteria));
    PDFOCRReviewCriteria scoreOnly = PDFOCRReviewCriteria::create(50.0, false, QStringList());
    QVERIFY(!PDFOCRReview::requiresReview(*words[1], scoreOnly));
    QVERIFY(!PDFOCRReview::requiresReview(*words[1], 50.0));

    PDFOCRConfidenceStatistics statistics = PDFOCRConfidenceStatistics::compute(*result, criteria);
    QCOMPARE(statistics.outsideDictionaryCount, 2);
    QCOMPARE(statistics.dictionaryCheckedCount, 5);
    QCOMPARE(statistics.reviewRequiredCount, 2);
    PDFOCRConfidenceStatistics scoreOnlyStatistics = PDFOCRConfidenceStatistics::compute(*result, scoreOnly);
    QCOMPARE(scoreOnlyStatistics.outsideDictionaryCount, 2);
    QCOMPARE(scoreOnlyStatistics.reviewRequiredCount, 0);

    // User words are accepted regardless of the case and of the surrounding punctuation
    PDFOCRReviewCriteria withUserWords = PDFOCRReviewCriteria::create(50.0, true, { QStringLiteral("PDFFORQT") });
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*words[3], withUserWords));
    QVERIFY(PDFOCRReview::isOutsideDictionary(*words[1], withUserWords));

    // The session takes the criteria from the configuration (the switch is on by default,
    // the user words are accepted words)
    PDFOCRSession session(nullptr);
    PDFOCRConfiguration configuration;
    QVERIFY(configuration.reviewOutsideDictionary);
    configuration.reviewThreshold = 50.0;
    session.setConfiguration(configuration);
    session.setPageResult(*result);
    QCOMPARE(session.getStatistics(0).reviewRequiredCount, 2);
    QCOMPARE(session.getReviewWords(0).size(), size_t(2));
    configuration.userWords = { QStringLiteral("pdfforqt") };
    session.setConfiguration(configuration);
    QCOMPARE(session.getStatistics(0).reviewRequiredCount, 1);
    configuration.reviewOutsideDictionary = false;
    session.setConfiguration(configuration);
    QCOMPARE(session.getStatistics(0).reviewRequiredCount, 0);
    configuration.reviewOutsideDictionary = true;
    configuration.userWords.clear();
    session.setConfiguration(configuration);

    // A decision of the user ends the dictionary review: a confirmed word and an
    // edited word are not candidates anymore
    const int unknownWordId = words[1]->id;
    QVERIFY(session.setWordReviewState(0, unknownWordId, PDFOCRReviewState::Confirmed));
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*session.getPage(0)->findWord(unknownWordId), criteria));
    session.undo();
    QVERIFY(PDFOCRReview::isOutsideDictionary(*session.getPage(0)->findWord(unknownWordId), criteria));
    const int punctuatedWordId = words[3]->id;
    QVERIFY(session.setWordText(0, punctuatedWordId, QStringLiteral("(PDF4QT),")));
    QVERIFY(!PDFOCRReview::isOutsideDictionary(*session.getPage(0)->findWord(punctuatedWordId), criteria));

    // The configuration keeps the switch, an old configuration without it has it on
    QVERIFY(!PDFOCRConfiguration::fromJson([]() { PDFOCRConfiguration off; off.reviewOutsideDictionary = false; return off.toJson(); }()).reviewOutsideDictionary);
    QJsonObject oldConfiguration = PDFOCRConfiguration().toJson();
    oldConfiguration.remove(QStringLiteral("reviewOutsideDictionary"));
    QVERIFY(PDFOCRConfiguration::fromJson(oldConfiguration).reviewOutsideDictionary);

    // The information survives the project (review data) and the text layer with the review data
    const PDFOCRPageResult restored = PDFOCRProjectSerializer::pageResultFromJson(PDFOCRProjectSerializer::pageResultToJson(*result, PDFOCRSerializationFlag::ReviewData));
    QCOMPARE(restored.getWords()[0]->inDictionary, std::optional<bool>(true));
    QCOMPARE(restored.getWords()[1]->inDictionary, std::optional<bool>(false));
    QVERIFY(!restored.getWords()[2]->inDictionary.has_value());

    const PDFOCRPageResult withoutReviewData = PDFOCRProjectSerializer::pageResultFromJson(PDFOCRProjectSerializer::pageResultToJson(*result, PDFOCRSerializationFlags()));
    QVERIFY(!withoutReviewData.getWords()[1]->inDictionary.has_value());

    PDFOCRTextLayerWriter::Options options;
    options.keepReviewData = true;
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer modified = applyResults(document, { *result }, options, &report);
    QVERIFY2(modified, qPrintable(report.error.message));
    PDFDocument reopened = read(write(*modified));
    std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(&reopened, 0);
    QVERIFY(layer.has_value());
    QCOMPARE(layer->getWords()[1]->inDictionary, std::optional<bool>(false));
    QCOMPARE(layer->getWords()[0]->inDictionary, std::optional<bool>(true));
}

void OCRTest::tesseractDictionaryInformation()
{
    // A "scanned" page with a dictionary word, a nonsense word and a number
    PageSpec spec;
    spec.size = QSizeF(520, 120);
    spec.withHelvetica = true;
    spec.content = "BT /F1 28 Tf 30 60 Td (Hello document qxzvbkw 2026) Tj ET";
    PDFDocument document = createScanDocument(spec);
    QVERIFY(document.getCatalog()->getPageCount() == 1);

    PDFOCRConfiguration configuration;
    configuration.languages = { QStringLiteral("eng") };
    configuration.dpi = 300.0;

    QString skipReason;
    QString errorMessage;
    std::optional<PDFOCRPageResult> result = recognizeWithTesseract(document, configuration, &skipReason, &errorMessage);
    if (!skipReason.isEmpty())
    {
        PDF4QT_OCR_SKIP(qPrintable(skipReason));
    }
    QVERIFY2(result.has_value() && errorMessage.isEmpty(), qPrintable(errorMessage));

    // The nonsense word itself may be recognized with an error (that is exactly the
    // case the dictionary information reveals), so the words are taken by their order
    const std::vector<PDFOCRWord*> words = result->getWords();
    QVERIFY2(words.size() == 4, qPrintable(result->getText()));
    const PDFOCRWord* dictionaryWord = words[1];
    const PDFOCRWord* unknownWord = words[2];
    const PDFOCRWord* number = words[3];
    QCOMPARE(dictionaryWord->text, QStringLiteral("document"));
    QCOMPARE(number->text, QStringLiteral("2026"));

    // The LSTM recognizer reports the dictionary match of the word (the probe of
    // chapter 4 of OCR_PLAN.md), numbers are not judged
    QCOMPARE(dictionaryWord->inDictionary, std::optional<bool>(true));
    QCOMPARE(unknownWord->inDictionary, std::optional<bool>(false));
    QVERIFY(!number->inDictionary.has_value());

    // Disabled word dictionary means unknown information
    configuration.engineParameters[QStringLiteral("load_system_dawg")] = false;
    std::optional<PDFOCRPageResult> withoutDictionary = recognizeWithTesseract(document, configuration, &skipReason, &errorMessage);
    QVERIFY2(withoutDictionary.has_value() && errorMessage.isEmpty(), qPrintable(errorMessage));
    for (const PDFOCRWord* word : withoutDictionary->getWords())
    {
        QVERIFY2(!word->inDictionary.has_value(), qPrintable(word->text));
    }
}

void OCRTest::structuredExport()
{
    // Page 1: ordinary page, page 2: rotated by 90 degrees, page 3: crop box with an
    // offset and user unit 2, page 4: not recognized (skipped and reported)
    PageSpec plainSpec;
    plainSpec.size = QSizeF(300, 200);
    PageSpec rotatedSpec;
    rotatedSpec.size = QSizeF(300, 200);
    rotatedSpec.rotation = PageRotation::Rotate90;
    PageSpec unitSpec;
    unitSpec.size = QSizeF(300, 200);
    unitSpec.cropBox = QRectF(50, 20, 200, 150);
    unitSpec.userUnit = 2.0;
    PDFDocument document = createDocument({ plainSpec, rotatedSpec, unitSpec, plainSpec });

    // Page 1: two lines of a block, the first ends with a word hyphenated at the line end,
    // a discarded word, a word with unknown confidence and a Czech word
    PDFOCRPageResult page1;
    page1.pageIndex = 0;
    page1.state = PDFOCRPageState::Done;
    page1.provenance.engineId = QStringLiteral("tesseract");
    page1.provenance.engineVersion = QStringLiteral("5.5.2");
    page1.geometry.dpi = 72.0;
    {
        PDFOCRBlock block;
        block.id = page1.allocateId();

        PDFOCRLine first;
        first.id = page1.allocateId();
        first.words.push_back(makeWord(page1, QStringLiteral("Hello"), QRectF(10, 150, 50, 20), 91.0));
        first.words.back().language = QStringLiteral("eng");
        PDFOCRWord discarded = makeWord(page1, QStringLiteral("noise"), QRectF(70, 150, 30, 20), 20.0);
        discarded.reviewState = PDFOCRReviewState::Discarded;
        first.words.push_back(discarded);
        first.words.push_back(makeWord(page1, QStringLiteral("docu-"), QRectF(110, 150, 60, 20), 88.0));
        first.words.back().language = QStringLiteral("ces");
        first.updateGeometryFromWords();

        PDFOCRLine second;
        second.id = page1.allocateId();
        second.words.push_back(makeWord(page1, QStringLiteral("ment"), QRectF(10, 120, 40, 20), 77.0));
        second.words.push_back(makeWord(page1, QStringLiteral("<a&b>"), QRectF(60, 120, 40, 20), std::nullopt));
        second.updateGeometryFromWords();

        block.lines.push_back(first);
        block.lines.push_back(second);
        block.updateGeometryFromLines();
        page1.blocks.push_back(block);
    }

    // Page 2: horizontal text of the page space, which is vertical on the rotated page
    PDFOCRPageResult page2 = createSampleResult(1, { { QStringLiteral("Rotated"), QRectF(100, 100, 80, 20) } });
    page2.geometry.dpi = 72.0;

    // Page 3: a right-to-left line
    PDFOCRPageResult page3 = createSampleResult(2, { { QStringLiteral("abc"), QRectF(60, 100, 40, 10) } });
    page3.geometry.dpi = 72.0;
    page3.blocks.front().lines.front().direction = PDFOCRTextDirection::RightToLeft;
    page3.blocks.front().lines.front().baseline = QLineF(QPointF(100, 100), QPointF(60, 100));
    page3.blocks.front().lines.front().words.front().language = QStringLiteral("script/Latin");

    PDFOCRPageResult page4;
    page4.pageIndex = 3;
    page4.state = PDFOCRPageState::Pending;

    const std::vector<const PDFOCRPageResult*> pages = { &page1, &page2, &page3, &page4 };

    // Transformation of the pages (the same as the rasterization of the recognition)
    QTransform transform;
    QSize size;
    QVERIFY(PDFOCRStructuredExporter::getPageTransform(&document, 0, 72.0, &transform, &size));
    QCOMPARE(size, QSize(300, 200));
    QVERIFY(PDFOCRStructuredExporter::getPageTransform(&document, 1, 72.0, &transform, &size));
    QCOMPARE(size, QSize(200, 300));
    QVERIFY(PDFOCRStructuredExporter::getPageTransform(&document, 2, 72.0, &transform, &size));
    QCOMPARE(size, QSize(400, 300));
    QVERIFY(!PDFOCRStructuredExporter::getPageTransform(&document, 7, 72.0, &transform, &size));

    // Resolution: explicit, of the recognition, or the default
    PDFOCRStructuredExporter::Options dpiOptions;
    QCOMPARE(PDFOCRStructuredExporter::getExportDpi(page1, dpiOptions), 72.0);
    QCOMPARE(PDFOCRStructuredExporter::getExportDpi(page4, dpiOptions), PDFOCRStructuredExporter::DefaultDpi);
    dpiOptions.dpi = 150.0;
    QCOMPARE(PDFOCRStructuredExporter::getExportDpi(page1, dpiOptions), 150.0);

    // Language tags
    QCOMPARE(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("ces")), QStringLiteral("cs"));
    QCOMPARE(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("chi_sim")), QStringLiteral("zh-Hans"));
    QCOMPARE(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("eng@ab12cd34")), QStringLiteral("en"));
    QCOMPARE(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("xyz")), QStringLiteral("xyz"));
    QVERIFY(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("script/Latin")).isEmpty());
    QVERIFY(PDFOCRStructuredExporter::toLanguageTag(QStringLiteral("abc_def")).isEmpty());

    // hOCR ---------------------------------------------------------------
    PDFOCRStructuredExporter::Options options;
    options.format = PDFOCRStructuredExporter::Format::Hocr;
    options.imageFileNameTemplate = QStringLiteral("page-%1.png");
    options.title = QStringLiteral("Test & document");
    PDFOCRTextExporter::Report report;
    const QByteArray hocr = PDFOCRStructuredExporter::exportPages(&document, pages, options, &report);

    QCOMPARE(report.exportedPages, (std::vector<PDFInteger>{ 0, 1, 2 }));
    QCOMPARE(report.skippedPages, std::vector<PDFInteger>{ 3 });
    QCOMPARE(report.skippedDescriptions.size(), 1);
    QCOMPARE(report.wordCount, 6);

    struct HocrElement
    {
        QString className;
        QString title;
        QString text;
        QString language;
        QString direction;
    };
    std::vector<HocrElement> elements;
    QStringList metaNames;
    {
        QXmlStreamReader reader(hocr);
        std::vector<size_t> open;
        while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isStartElement())
            {
                if (reader.name() == QLatin1String("meta"))
                {
                    metaNames << reader.attributes().value(QStringLiteral("name")).toString();
                }

                const QString className = reader.attributes().value(QStringLiteral("class")).toString();
                if (!className.isEmpty())
                {
                    HocrElement element;
                    element.className = className;
                    element.title = reader.attributes().value(QStringLiteral("title")).toString();
                    element.language = reader.attributes().value(QStringLiteral("lang")).toString();
                    element.direction = reader.attributes().value(QStringLiteral("dir")).toString();
                    elements.push_back(element);
                    open.push_back(elements.size() - 1);
                }
                else
                {
                    open.push_back(std::numeric_limits<size_t>::max());
                }
            }
            else if (reader.isCharacters() && !open.empty() && open.back() != std::numeric_limits<size_t>::max())
            {
                elements[open.back()].text += reader.text().toString();
            }
            else if (reader.isEndElement() && !open.empty())
            {
                open.pop_back();
            }
        }
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    }

    auto findElements = [&elements](const QString& className)
    {
        std::vector<HocrElement> result;
        std::copy_if(elements.begin(), elements.end(), std::back_inserter(result), [&className](const HocrElement& element) { return element.className == className; });
        return result;
    };

    QVERIFY(metaNames.contains(QStringLiteral("ocr-system")));
    QVERIFY(metaNames.contains(QStringLiteral("ocr-capabilities")));
    QVERIFY(metaNames.contains(QStringLiteral("ocr-number-of-pages")));
    QVERIFY(metaNames.contains(QStringLiteral("ocr-langs")));

    const std::vector<HocrElement> hocrPages = findElements(QStringLiteral("ocr_page"));
    QCOMPARE(hocrPages.size(), size_t(3));
    QCOMPARE(hocrPages[0].title, QStringLiteral("image \"page-1.png\"; bbox 0 0 300 200; ppageno 0; scan_res 72 72"));
    QCOMPARE(hocrPages[1].title, QStringLiteral("image \"page-2.png\"; bbox 0 0 200 300; ppageno 1; scan_res 72 72"));

    const std::vector<HocrElement> hocrWords = findElements(QStringLiteral("ocrx_word"));
    QCOMPARE(hocrWords.size(), size_t(6));
    QCOMPARE(hocrWords[0].text, QStringLiteral("Hello"));
    QCOMPARE(hocrWords[0].title, QStringLiteral("bbox 10 30 60 50; x_wconf 91"));
    QCOMPARE(hocrWords[0].language, QStringLiteral("en"));
    QCOMPARE(hocrWords[1].text, QStringLiteral("docu-"));
    QCOMPARE(hocrWords[1].language, QStringLiteral("cs"));
    QCOMPARE(hocrWords[3].text, QStringLiteral("<a&b>"));
    QCOMPARE(hocrWords[3].title, QStringLiteral("bbox 60 60 100 80"));

    // Page with user unit 2 and the crop box starting at [50, 20]: [60, 100] - [100, 110]
    // is [20, 120] - [100, 140] in the pixels of the visible page
    QCOMPARE(hocrWords[5].text, QStringLiteral("abc"));
    QVERIFY2(hocrWords[5].title.startsWith(QStringLiteral("bbox 20 120 100 140")), qPrintable(hocrWords[5].title));

    const std::vector<HocrElement> hocrLines = findElements(QStringLiteral("ocr_line"));
    QCOMPARE(hocrLines.size(), size_t(4));
    QVERIFY2(hocrLines[0].title.startsWith(QStringLiteral("bbox 10 30 170 50; baseline 0.000 0")), qPrintable(hocrLines[0].title));
    QVERIFY2(hocrLines[2].title.contains(QStringLiteral("textangle 270")), qPrintable(hocrLines[2].title));
    QCOMPARE(hocrLines[3].direction, QStringLiteral("rtl"));
    QVERIFY2(!hocrLines[3].title.contains(QStringLiteral("textangle")), qPrintable(hocrLines[3].title));

    // Rotated page: the word is mapped by the same transformation as the rasterization
    QVERIFY(PDFOCRStructuredExporter::getPageTransform(&document, 1, 72.0, &transform, &size));
    const QRect rotatedBox = transform.map(PDFOCRQuad::fromRect(QRectF(100, 100, 80, 20)).toPolygon()).boundingRect().toAlignedRect();
    QVERIFY2(hocrWords[4].title.startsWith(QStringLiteral("bbox %1 %2 %3 %4").arg(rotatedBox.left()).arg(rotatedBox.top()).arg(rotatedBox.right() + 1).arg(rotatedBox.bottom() + 1)), qPrintable(hocrWords[4].title));

    // Without the scores, no confidence is exported
    options.includeConfidence = false;
    QVERIFY(!PDFOCRStructuredExporter::exportPages(&document, pages, options, nullptr).contains("x_wconf"));
    options.includeConfidence = true;

    // Only reviewed words
    options.onlyReviewed = true;
    PDFOCRPageResult reviewedPage = page1;
    reviewedPage.getWords()[0]->reviewState = PDFOCRReviewState::Confirmed;
    const QByteArray reviewedHocr = PDFOCRStructuredExporter::exportPages(&document, { &reviewedPage }, options, nullptr);
    QVERIFY(reviewedHocr.contains(">Hello<"));
    QVERIFY(!reviewedHocr.contains(">ment<"));
    options.onlyReviewed = false;

    // ALTO ---------------------------------------------------------------
    options.format = PDFOCRStructuredExporter::Format::Alto;
    const QByteArray alto = PDFOCRStructuredExporter::exportPages(&document, pages, options, nullptr);

    struct AltoString
    {
        QXmlStreamAttributes attributes;
        bool followedByHyphen = false;
        QString polygon;
    };
    std::vector<AltoString> strings;
    std::vector<QXmlStreamAttributes> altoPages;
    std::vector<QXmlStreamAttributes> altoLines;
    QString measurementUnit;
    {
        QXmlStreamReader reader(alto);
        while (!reader.atEnd())
        {
            reader.readNext();
            if (!reader.isStartElement())
            {
                continue;
            }

            if (reader.name() == QLatin1String("alto"))
            {
                QCOMPARE(reader.namespaceUri().toString(), QStringLiteral("http://www.loc.gov/standards/alto/ns-v4#"));
            }
            else if (reader.name() == QLatin1String("MeasurementUnit"))
            {
                measurementUnit = reader.readElementText();
            }
            else if (reader.name() == QLatin1String("Page"))
            {
                altoPages.push_back(reader.attributes());
            }
            else if (reader.name() == QLatin1String("TextLine"))
            {
                altoLines.push_back(reader.attributes());
            }
            else if (reader.name() == QLatin1String("String"))
            {
                strings.push_back(AltoString{ reader.attributes(), false, QString() });
            }
            else if (reader.name() == QLatin1String("HYP"))
            {
                QVERIFY(!strings.empty());
                strings.back().followedByHyphen = true;
                QCOMPARE(reader.attributes().value(QStringLiteral("CONTENT")).toString(), QStringLiteral("-"));
            }
            else if (reader.name() == QLatin1String("Polygon"))
            {
                QVERIFY(!strings.empty());
                strings.back().polygon = reader.attributes().value(QStringLiteral("POINTS")).toString();
            }
        }
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    }

    QCOMPARE(measurementUnit, QStringLiteral("pixel"));
    QCOMPARE(altoPages.size(), size_t(3));
    QCOMPARE(altoPages[0].value(QStringLiteral("WIDTH")).toString(), QStringLiteral("300"));
    QCOMPARE(altoPages[1].value(QStringLiteral("WIDTH")).toString(), QStringLiteral("200"));
    QCOMPARE(altoPages[2].value(QStringLiteral("HEIGHT")).toString(), QStringLiteral("300"));
    QCOMPARE(altoPages[2].value(QStringLiteral("PHYSICAL_IMG_NR")).toString(), QStringLiteral("3"));
    QCOMPARE(altoLines[0].value(QStringLiteral("BASELINE")).toString(), QStringLiteral("10,50 170,50"));

    QCOMPARE(strings.size(), size_t(6));
    const QXmlStreamAttributes& hello = strings[0].attributes;
    QCOMPARE(hello.value(QStringLiteral("CONTENT")).toString(), QStringLiteral("Hello"));
    QCOMPARE(hello.value(QStringLiteral("HPOS")).toString(), QStringLiteral("10"));
    QCOMPARE(hello.value(QStringLiteral("VPOS")).toString(), QStringLiteral("30"));
    QCOMPARE(hello.value(QStringLiteral("WIDTH")).toString(), QStringLiteral("50"));
    QCOMPARE(hello.value(QStringLiteral("HEIGHT")).toString(), QStringLiteral("20"));
    QCOMPARE(hello.value(QStringLiteral("WC")).toString(), QStringLiteral("0.910"));
    QCOMPARE(hello.value(QStringLiteral("LANG")).toString(), QStringLiteral("en"));

    // Word hyphenated at the line end: the first part without the hyphen, HYP, the second part
    QCOMPARE(strings[1].attributes.value(QStringLiteral("CONTENT")).toString(), QStringLiteral("docu"));
    QCOMPARE(strings[1].attributes.value(QStringLiteral("SUBS_TYPE")).toString(), QStringLiteral("HypPart1"));
    QCOMPARE(strings[1].attributes.value(QStringLiteral("SUBS_CONTENT")).toString(), QStringLiteral("document"));
    QVERIFY(strings[1].followedByHyphen);
    QCOMPARE(strings[2].attributes.value(QStringLiteral("SUBS_TYPE")).toString(), QStringLiteral("HypPart2"));
    QCOMPARE(strings[2].attributes.value(QStringLiteral("SUBS_CONTENT")).toString(), QStringLiteral("document"));

    // Unknown confidence has no WC, the special characters are escaped by the writer
    QCOMPARE(strings[3].attributes.value(QStringLiteral("CONTENT")).toString(), QStringLiteral("<a&b>"));
    QVERIFY(!strings[3].attributes.hasAttribute(QStringLiteral("WC")));

    // Vertical word of the rotated page has its exact polygon, the script model is not a language
    QVERIFY(!strings[4].polygon.isEmpty());
    QVERIFY(!strings[5].attributes.hasAttribute(QStringLiteral("LANG")));

    // TSV ----------------------------------------------------------------
    options.format = PDFOCRStructuredExporter::Format::Tsv;
    const QStringList rows = QString::fromUtf8(PDFOCRStructuredExporter::exportPages(&document, pages, options, nullptr)).split(QChar('\n'), Qt::SkipEmptyParts);
    QCOMPARE(rows.front(), QStringLiteral("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext"));

    // 3 pages, 3 blocks (2 rows each), 4 lines and 6 words
    QCOMPARE(rows.size(), 1 + 3 + 3 * 2 + 4 + 6);
    for (const QString& row : rows)
    {
        QCOMPARE(row.split(QChar('\t')).size(), 12);
    }
    QCOMPARE(rows[1], QStringLiteral("1\t1\t0\t0\t0\t0\t0\t0\t300\t200\t-1\t"));
    QVERIFY(rows.contains(QStringLiteral("5\t1\t1\t1\t1\t1\t10\t30\t50\t20\t91.000000\tHello")));
    QVERIFY(rows.contains(QStringLiteral("5\t1\t1\t1\t2\t2\t60\t60\t40\t20\t-1\t<a&b>")));
}

namespace
{

/// Image of the image documents of the compression tests
struct TestImage
{
    QImage image;               ///< Source pixels
    int bitsPerComponent = 8;   ///< 1 (black and white) or 8
    bool gray = true;           ///< DeviceGray or DeviceRGB
    bool compress = false;      ///< FlateDecode, otherwise uncompressed
    bool jpeg = false;          ///< DCTDecode (lossy source)
};

/// Encodes the samples of the image of the PDF image stream
QByteArray getImageSamples(const TestImage& testImage)
{
    QByteArray data;
    const QImage& image = testImage.image;
    if (testImage.bitsPerComponent == 1)
    {
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        const int rowBytes = (gray.width() + 7) / 8;
        for (int y = 0; y < gray.height(); ++y)
        {
            QByteArray row(rowBytes, '\0');
            const uchar* line = gray.constScanLine(y);
            for (int x = 0; x < gray.width(); ++x)
            {
                // Sample 1 is white in DeviceGray with the default decode array
                if (line[x] >= 128)
                {
                    row[x / 8] = char(uchar(row[x / 8]) | uchar(0x80 >> (x % 8)));
                }
            }
            data.append(row);
        }
    }
    else if (testImage.gray)
    {
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        for (int y = 0; y < gray.height(); ++y)
        {
            data.append(reinterpret_cast<const char*>(gray.constScanLine(y)), gray.width());
        }
    }
    else
    {
        const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
        for (int y = 0; y < rgb.height(); ++y)
        {
            data.append(reinterpret_cast<const char*>(rgb.constScanLine(y)), rgb.width() * 3);
        }
    }
    return data;
}

/// Creates a document, whose pages draw the images over the whole page
/// (pageImages[i] is the index of the image drawn on the page i; several
/// pages can share one image object)
PDFDocument createImagePagesDocument(const std::vector<TestImage>& images, const std::vector<int>& pageImages, QSizeF pageSize, PDFVersion version = PDFVersion(1, 7))
{
    PDFDocumentBuilder builder;

    std::vector<PDFObjectReference> imageReferences;
    for (const TestImage& testImage : images)
    {
        QByteArray data;
        QByteArray filter;
        if (testImage.jpeg)
        {
            QBuffer buffer(&data);
            buffer.open(QBuffer::WriteOnly);
            testImage.image.convertToFormat(testImage.gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888).save(&buffer, "JPG", 90);
            filter = "DCTDecode";
        }
        else
        {
            data = getImageSamples(testImage);
            if (testImage.compress)
            {
                data = PDFFlateDecodeFilter::compress(data);
                filter = "FlateDecode";
            }
        }

        PDFDictionary dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(testImage.image.width()));
        dictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(testImage.image.height()));
        dictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName(testImage.gray ? "DeviceGray" : "DeviceRGB"));
        dictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(testImage.bitsPerComponent));
        dictionary.addEntry(PDFInplaceOrMemoryString("Interpolate"), PDFObject::createBool(false));
        if (!filter.isEmpty())
        {
            dictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName(filter));
        }
        dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(data.size()));
        imageReferences.push_back(builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(data)))));
    }

    for (int imageIndex : pageImages)
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));
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
        factory << imageReferences[size_t(imageIndex)];
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(pageReference, factory.takeObject());
    }

    PDFDocument document = builder.build();
    return PDFDocument(PDFObjectStorage(document.getStorage()), version, QByteArray());
}

/// Black and white image with some text-like structure
QImage createBitonalTextImage(QSize size, int seed)
{
    QImage image(size, QImage::Format_Grayscale8);
    image.fill(255);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    QFont font = painter.font();
    font.setPixelSize(size.height() / 20);
    painter.setFont(font);
    painter.setPen(Qt::black);
    for (int line = 0; line < 12; ++line)
    {
        painter.drawText(QPoint(size.width() / 12, size.height() / 14 * (line + 1)), QStringLiteral("Line %1 of the scanned text %2 ABCDEFGH").arg(line + 1).arg(seed));
    }
    painter.end();

    // Hard black and white (no antialiasing remains)
    for (int y = 0; y < image.height(); ++y)
    {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
        {
            row[x] = row[x] < 128 ? 0 : 255;
        }
    }
    return image;
}

/// Gray scan of a text: paper, ink and antialiased edges
QImage createGrayTextScan(QSize size)
{
    QImage image(size, QImage::Format_Grayscale8);
    image.fill(236);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(size.height() / 22);
    painter.setFont(font);
    painter.setPen(QColor(30, 30, 30));
    for (int line = 0; line < 14; ++line)
    {
        painter.drawText(QPoint(size.width() / 12, size.height() / 16 * (line + 1)), QStringLiteral("Scanned gray text line %1 with some words").arg(line + 1));
    }
    return image;
}

/// Photo-like gray image (smooth gradients, a lot of mid tones)
QImage createPhotoImage(QSize size, bool gray)
{
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
    {
        QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
        {
            const int r = 60 + (x * 140) / size.width() + (y * 40) / size.height();
            const int g = 80 + (y * 120) / size.height();
            const int b = 120 + ((x + y) * 60) / (size.width() + size.height());
            row[x] = gray ? qRgb(qGray(r, g, b), qGray(r, g, b), qGray(r, g, b)) : qRgb(r, g, b);
        }
    }
    return image;
}

QImage renderPage(const PDFDocument& document, PDFInteger pageIndex, double dpi)
{
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    return preparer.rasterize(pageIndex, dpi, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr).image;
}

QByteArray getImageFilter(const PDFDocument& document, PDFInteger pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    const PDFDictionary* xobjects = document.getDictionaryFromObject(resources->get("XObject"));
    const PDFObject& image = document.getObject(xobjects->get("Im1"));
    const PDFObject& filter = document.getObject(image.getStream()->getDictionary()->get("Filter"));
    return filter.isName() ? filter.getString() : QByteArray();
}

} // namespace

void OCRTest::applyProcessorPlanAndPermissions()
{
    PageSpec spec;
    spec.size = QSizeF(300, 200);
    spec.withImage = true;
    PDFDocument document = createDocument({ spec, spec, spec, spec, spec });

    // Context of an ordinary document
    PDFOCRApplyProcessor::Context context = PDFOCRApplyProcessor::createContext(&document, QString());
    QVERIFY(context.canModify);
    QVERIFY(context.canCopyContent);
    QCOMPARE(context.certificationPermissions, 0);
    QVERIFY(!context.isTagged);
    QVERIFY(!context.hasSignatures);
    QVERIFY(!context.hasConformanceDeclaration());
    QVERIFY(PDFOCRApplyProcessor::checkPermissions(context, PDFOCRApplyProcessor::OutputMode::ModifyDocument).isEmpty());

    // Permissions (PDF-12) and the conformance declaration (PDF-15)
    using Mode = PDFOCRApplyProcessor::OutputMode;
    PDFOCRApplyProcessor::Context locked = context;
    locked.canModify = false;
    QVERIFY(!PDFOCRApplyProcessor::checkPermissions(locked, Mode::ModifyDocument).isEmpty());
    QVERIFY(!PDFOCRApplyProcessor::checkPermissions(locked, Mode::CreateCopy).isEmpty());
    PDFOCRApplyProcessor::Context certified = context;
    certified.certificationPermissions = 1;
    QVERIFY(!PDFOCRApplyProcessor::checkPermissions(certified, Mode::ModifyDocument).isEmpty());
    QVERIFY(!PDFOCRApplyProcessor::checkPermissions(certified, Mode::CreateCopy).isEmpty());
    certified.certificationPermissions = 2;
    QVERIFY(!PDFOCRApplyProcessor::checkPermissions(certified, Mode::ModifyDocument).isEmpty());
    QVERIFY(PDFOCRApplyProcessor::checkPermissions(certified, Mode::CreateCopy).isEmpty());
    certified.certificationPermissions = 3;
    QVERIFY(PDFOCRApplyProcessor::checkPermissions(certified, Mode::CreateCopy).isEmpty());
    PDFOCRApplyProcessor::Context conforming = context;
    conforming.conformanceDeclarations = { QStringLiteral("PDF/A-2b") };
    QVERIFY2(PDFOCRApplyProcessor::checkPermissions(conforming, Mode::ModifyDocument).contains(QStringLiteral("PDF/A-2b")), qPrintable(PDFOCRApplyProcessor::checkPermissions(conforming, Mode::ModifyDocument)));
    QVERIFY(PDFOCRApplyProcessor::checkPermissions(conforming, Mode::CreateCopy).isEmpty());

    // Results: 0 = valid, 1 = review only, 2 = changed page, 3 = invalid geometry,
    // 4 = no result, 9 = not a page of the document
    auto createResult = [&](PDFInteger pageIndex)
    {
        PDFOCRPageResult result = createSampleResult(pageIndex, { { QStringLiteral("Hello"), QRectF(20, 100, 60, 20) }, { QStringLiteral("world"), QRectF(90, 100, 60, 20) } });
        result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, qMin<PDFInteger>(pageIndex, 4));
        return result;
    };

    PDFOCRApplyProcessor::Request request;
    request.results.push_back(createResult(0));
    request.results.push_back(createResult(1));
    request.results.back().reviewOnly = true;
    request.results.push_back(createResult(2));
    request.results.back().pageFingerprint = QByteArray("changed");
    request.results.push_back(createResult(3));
    request.results.back().getWords()[0]->quad = PDFOCRQuad::fromRect(QRectF(2e7, 2e7, 10, 10));
    PDFOCRPageResult pending;
    pending.pageIndex = 4;
    request.results.push_back(pending);
    request.results.push_back(createResult(9));
    for (PDFInteger page = 0; page < 5; ++page)
    {
        request.fingerprints[page] = PDFOCRPagePreparer::computePageFingerprint(&document, page);
    }
    PDFOCRPageAnalysis analysis;
    analysis.hasOwnOCRLayer = true;
    request.analysis[0] = analysis;
    request.reviewCriteria = PDFOCRReviewCriteria::create(99.0, true, QStringList());

    PDFOCRApplyProcessor::Plan plan = PDFOCRApplyProcessor::createPlan(context, request);
    QCOMPARE(plan.getPages(), std::vector<PDFInteger>{ 0 });
    QCOMPARE(plan.excludedPages, (std::vector<PDFInteger>{ 1, 2, 3, 4, 9 }));
    QCOMPARE(plan.excluded.size(), 5);
    QVERIFY2(plan.excluded[0].contains(QStringLiteral("review/export only")), qPrintable(plan.excluded[0]));
    QVERIFY2(plan.excluded[1].contains(QStringLiteral("differs")), qPrintable(plan.excluded[1]));
    QVERIFY2(plan.excluded[2].contains(QStringLiteral("geometry")), qPrintable(plan.excluded[2]));
    QCOMPARE(plan.replacedLayers, 1);
    QCOMPARE(plan.unreviewedWords, 2);
    QCOMPARE(plan.uncertainWords, 2);
    QVERIFY(plan.knownFingerprints.count(0));
    QVERIFY(!plan.writerOptions.markAsArtifact);
    QVERIFY(plan.requests.front().result.analysis.hasOwnOCRLayer);

    // Tagged document writes an artifact, a copy of a conforming document removes the declaration
    PDFOCRApplyProcessor::Context tagged = context;
    tagged.isTagged = true;
    QVERIFY(PDFOCRApplyProcessor::createPlan(tagged, request).writerOptions.markAsArtifact);
    request.outputMode = Mode::CreateCopy;
    QVERIFY(PDFOCRApplyProcessor::createPlan(conforming, request).removeConformance);
    QVERIFY(!PDFOCRApplyProcessor::createPlan(context, request).removeConformance);
    request.outputMode = Mode::ModifyDocument;

    // The copy never overwrites the document
    QTemporaryDir directory;
    const QString sourceFile = directory.filePath(QStringLiteral("source.pdf"));
    QFile file(sourceFile);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write("%PDF-1.7");
    file.close();
    PDFOCRApplyProcessor::Context fileContext = context;
    fileContext.fileName = sourceFile;
    PDFOCRApplyProcessor::Plan copyPlan = PDFOCRApplyProcessor::createPlan(fileContext, request);
    QVERIFY(!PDFOCRApplyProcessor::setCopyFileName(copyPlan, fileContext, sourceFile).isEmpty());
    QVERIFY(copyPlan.copyFileName.isEmpty());
    QVERIFY(PDFOCRApplyProcessor::setCopyFileName(copyPlan, fileContext, directory.filePath(QStringLiteral("copy.pdf"))).isEmpty());
    QCOMPARE(copyPlan.copyFileName, directory.filePath(QStringLiteral("copy.pdf")));

    // Summary and warnings
    const QString summary = PDFOCRApplyProcessor::getSummary(plan).join(QChar('\n'));
    QVERIFY2(summary.contains(QStringLiteral("Pages: 1")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("Own OCR layers to be replaced: 1")), qPrintable(summary));
    plan.writerOptions.onlyReviewed = true;
    plan.compression.mode = PDFOCRCompressionMode::BitonalTextScans;
    const QString warnings = PDFOCRApplyProcessor::getWarnings(plan, tagged).join(QChar('\n'));
    QVERIFY2(warnings.contains(QStringLiteral("INCOMPLETE")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("tagged")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("LOSSY")), qPrintable(warnings));
    QVERIFY2(PDFOCRApplyProcessor::getSummary(plan).join(QChar('\n')).contains(QStringLiteral("Black and white text scans")), qPrintable(PDFOCRApplyProcessor::getSummary(plan).join(QChar('\n'))));
}

void OCRTest::applyProcessorExecute()
{
    PageSpec spec;
    spec.size = QSizeF(300, 200);
    spec.withImage = true;
    PDFDocument document = createDocument({ spec, spec });
    const PDFOCRApplyProcessor::Context context = PDFOCRApplyProcessor::createContext(&document, QString());

    PDFOCRApplyProcessor::Request request;
    for (PDFInteger page = 0; page < 2; ++page)
    {
        PDFOCRPageResult result = createSampleResult(page, { { QStringLiteral("Page"), QRectF(20, 100, 60, 20) }, { QString::number(page + 1), QRectF(90, 100, 30, 20) } });
        result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, page);
        request.results.push_back(result);
    }

    // Modification of the document
    PDFOCRApplyProcessor::Plan plan = PDFOCRApplyProcessor::createPlan(context, request);
    QCOMPARE(plan.getPages(), (std::vector<PDFInteger>{ 0, 1 }));
    PDFOCRApplyProcessor::Result result = PDFOCRApplyProcessor::execute(context, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QVERIFY(result.document);
    QCOMPARE(result.report.writtenPages, (std::vector<PDFInteger>{ 0, 1 }));
    for (PDFInteger page = 0; page < 2; ++page)
    {
        const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(result.document.data(), page);
        QVERIFY(info.isPresent && info.fingerprintMatches);
        QCOMPARE(result.fingerprints[page], PDFOCRPagePreparer::computePageFingerprint(result.document.data(), page));
    }
    QVERIFY(extractText(*result.document, 1).contains(QStringLiteral("Page 2")));

    // The same layer again: nothing is changed (PDF-11)
    const PDFOCRApplyProcessor::Context writtenContext = PDFOCRApplyProcessor::createContext(result.document.data(), QString());
    PDFOCRApplyProcessor::Result repeated = PDFOCRApplyProcessor::execute(writtenContext, PDFOCRApplyProcessor::createPlan(writtenContext, request), nullptr);
    QVERIFY2(repeated.isSuccess(), qPrintable(repeated.errorMessage));
    QVERIFY(!repeated.document);
    QCOMPARE(repeated.report.unchangedPages, (std::vector<PDFInteger>{ 0, 1 }));

    // A page changed since the recognition is not written, the other pages are
    PDFOCRApplyProcessor::Plan changedPlan = plan;
    changedPlan.knownFingerprints[0] = QByteArray("changed");
    PDFOCRApplyProcessor::Result changedResult = PDFOCRApplyProcessor::execute(context, changedPlan, nullptr);
    QVERIFY2(changedResult.isSuccess(), qPrintable(changedResult.errorMessage));
    QCOMPARE(changedResult.report.writtenPages, std::vector<PDFInteger>{ 1 });
    QVERIFY(changedResult.report.messages.join(QChar('\n')).contains(QStringLiteral("Page 1")));
    changedPlan.knownFingerprints[1] = QByteArray("changed");
    changedResult = PDFOCRApplyProcessor::execute(context, changedPlan, nullptr);
    QVERIFY(!changedResult.isSuccess());
    QVERIFY(!changedResult.document);

    // Cancelled operation does not produce a document
    PDFOCRCancelToken token;
    token.cancel();
    PDFOCRApplyProcessor::Result cancelled = PDFOCRApplyProcessor::execute(context, plan, &token);
    QVERIFY(!cancelled.isSuccess());
    QVERIFY(!cancelled.document);

    // Permissions are enforced also by the execution
    PDFOCRApplyProcessor::Context locked = context;
    locked.canModify = false;
    QVERIFY(!PDFOCRApplyProcessor::execute(locked, plan, nullptr).isSuccess());

    // A copy is written into its file
    QTemporaryDir directory;
    PDFOCRApplyProcessor::Plan copyPlan = plan;
    copyPlan.outputMode = PDFOCRApplyProcessor::OutputMode::CreateCopy;
    QVERIFY(PDFOCRApplyProcessor::setCopyFileName(copyPlan, context, directory.filePath(QStringLiteral("copy.pdf"))).isEmpty());
    PDFOCRApplyProcessor::Result copyResult = PDFOCRApplyProcessor::execute(context, copyPlan, nullptr);
    QVERIFY2(copyResult.isSuccess(), qPrintable(copyResult.errorMessage));
    QFile copyFile(directory.filePath(QStringLiteral("copy.pdf")));
    QVERIFY(copyFile.open(QFile::ReadOnly));
    PDFDocument copy = read(copyFile.readAll());
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&copy, 0).fingerprintMatches);

    // Removal of the layers
    PDFOCRApplyProcessor::Result removed = PDFOCRApplyProcessor::removeLayers(writtenContext, { 0, 1 }, nullptr);
    QVERIFY2(removed.isSuccess(), qPrintable(removed.errorMessage));
    QVERIFY(removed.document);
    QCOMPARE(removed.report.writtenPages, (std::vector<PDFInteger>{ 0, 1 }));
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.document.data(), 0).isPresent);
    PDFOCRApplyProcessor::Context certifiedContext = writtenContext;
    certifiedContext.certificationPermissions = 2;
    QVERIFY(!PDFOCRApplyProcessor::removeLayers(certifiedContext, { 0 }, nullptr).isSuccess());
}

void OCRTest::compressionLossless()
{
    // 1: black and white image, 2: gray gradient, 3: lossy JPEG source, 4: color photo
    std::vector<TestImage> images(4);
    images[0].image = createBitonalTextImage(QSize(600, 400), 1);
    images[0].bitsPerComponent = 1;
    images[1].image = createPhotoImage(QSize(300, 200), true);
    images[2].image = createPhotoImage(QSize(300, 200), true);
    images[2].jpeg = true;
    images[3].image = createPhotoImage(QSize(150, 100), false);
    images[3].gray = false;
    const PDFDocument document = createImagePagesDocument(images, { 0, 1, 2, 3 }, QSizeF(300, 200), PDFVersion(1, 3));

    const std::map<PDFObjectReference, std::vector<PDFInteger>> usage = PDFOCRImageCompressor::getImageUsage(&document);
    QCOMPARE(usage.size(), size_t(4));

    std::vector<QImage> before;
    for (PDFInteger page = 0; page < 4; ++page)
    {
        before.push_back(renderPage(document, page, 100.0));
        QVERIFY(!before.back().isNull());
    }

    PDFOCRCompressionSettings settings;
    settings.mode = PDFOCRCompressionMode::Lossless;
    settings.bitonalEncoding = PDFOCRBitonalEncoding::JBIG2;
    PDFOCRCompressionReport report;
    const PDFDocument compressed = PDFOCRImageCompressor::compress(&document, { 0, 1, 2, 3 }, settings, qint64(1) << 30, nullptr, &report);

    using Action = PDFOCRCompressionImageResult::Action;
    QCOMPARE(report.images.size(), size_t(4));
    auto findResult = [&report](PDFInteger page) -> const PDFOCRCompressionImageResult*
    {
        for (const PDFOCRCompressionImageResult& image : report.images)
        {
            if (image.pages == std::vector<PDFInteger>{ page })
            {
                return &image;
            }
        }
        return nullptr;
    };

    // The black and white image is encoded by JBIG2 (generic region), the version is raised
    const PDFOCRCompressionImageResult* bitonal = findResult(0);
    QVERIFY(bitonal);
    QCOMPARE(bitonal->action, Action::Compressed);
    QCOMPARE(bitonal->imageClass, PDFOCRCompressionImageResult::ImageClass::Bitonal);
    QCOMPARE(bitonal->encoding, QStringLiteral("JBIG2"));
    QVERIFY(bitonal->newBytes < bitonal->originalBytes);
    QCOMPARE(getImageFilter(compressed, 0), QByteArray("JBIG2Decode"));
    QVERIFY(report.versionRaised);
    QCOMPARE(compressed.getInfo()->version.minor, uint16_t(4));

    // The uncompressed gray image is encoded by Flate, the JPEG source is kept
    QCOMPARE(findResult(1)->action, Action::Compressed);
    QCOMPARE(getImageFilter(compressed, 1), QByteArray("FlateDecode"));
    QCOMPARE(findResult(2)->action, Action::SkippedUnsupported);
    QCOMPARE(getImageFilter(compressed, 2), QByteArray("DCTDecode"));
    QCOMPARE(findResult(3)->action, Action::Compressed);
    QVERIFY2(report.getSummary().contains(QStringLiteral("3 compressed")), qPrintable(report.getSummary()));

    // Lossless: the pages look exactly the same
    for (PDFInteger page = 0; page < 4; ++page)
    {
        QVERIFY2(renderPage(compressed, page, 100.0) == before[size_t(page)], qPrintable(QStringLiteral("Page %1 differs").arg(page + 1)));
    }

    // Other entries of the image dictionary are kept
    const PDFDictionary* resources = compressed.getDictionaryFromObject(compressed.getCatalog()->getPage(0)->getResources());
    const PDFObject& image = compressed.getObject(compressed.getDictionaryFromObject(resources->get("XObject"))->get("Im1"));
    QVERIFY(image.getStream()->getDictionary()->hasKey("Interpolate"));

    // The smallest encoding of the black and white image, CCITT and Flate
    for (PDFOCRBitonalEncoding encoding : { PDFOCRBitonalEncoding::Smallest, PDFOCRBitonalEncoding::CCITTGroup4, PDFOCRBitonalEncoding::Flate })
    {
        settings.bitonalEncoding = encoding;
        PDFOCRCompressionReport encodingReport;
        const PDFDocument encoded = PDFOCRImageCompressor::compress(&document, { 0 }, settings, qint64(1) << 30, nullptr, &encodingReport);
        QCOMPARE(encodingReport.images.front().action, Action::Compressed);
        QVERIFY2(renderPage(encoded, 0, 100.0) == before[0], qPrintable(encodingReport.images.front().encoding));
        if (encoding == PDFOCRBitonalEncoding::CCITTGroup4)
        {
            QCOMPARE(getImageFilter(encoded, 0), QByteArray("CCITTFaxDecode"));
        }
    }

    // Off: nothing is changed
    settings.mode = PDFOCRCompressionMode::Off;
    PDFOCRCompressionReport offReport;
    const PDFDocument unchanged = PDFOCRImageCompressor::compress(&document, { 0, 1, 2, 3 }, settings, qint64(1) << 30, nullptr, &offReport);
    QVERIFY(offReport.images.empty());
    QCOMPARE(getImageFilter(unchanged, 0), QByteArray());

    // Cancelled: nothing is changed
    settings.mode = PDFOCRCompressionMode::Lossless;
    PDFOCRCancelToken token;
    token.cancel();
    PDFOCRCompressionReport cancelledReport;
    const PDFDocument cancelled = PDFOCRImageCompressor::compress(&document, { 0 }, settings, qint64(1) << 30, &token, &cancelledReport);
    QVERIFY(!cancelledReport.isChanged());
    QCOMPARE(getImageFilter(cancelled, 0), QByteArray());
}

void OCRTest::compressionBitonalAndShared()
{
    // Classification of the content
    const QImage grayScan = createGrayTextScan(QSize(800, 600));
    QVERIFY(PDFOCRImageCompressor::isTextScan(grayScan));
    QVERIFY(!PDFOCRImageCompressor::isTextScan(createPhotoImage(QSize(400, 300), true)));
    QVERIFY(!PDFOCRImageCompressor::isTextScan(createPhotoImage(QSize(400, 300), false)));

    // Conversion to black and white by the method of the settings
    PDFOCRCompressionSettings settings;
    settings.mode = PDFOCRCompressionMode::BitonalTextScans;
    for (PDFOCRThresholdMethod method : { PDFOCRThresholdMethod::Automatic, PDFOCRThresholdMethod::Adaptive, PDFOCRThresholdMethod::Manual })
    {
        settings.thresholdMethod = method;
        const QImage bitonal = PDFOCRImageCompressor::toBitonal(grayScan, settings, nullptr);
        QCOMPARE(bitonal.size(), grayScan.size());
        int black = 0;
        int other = 0;
        for (int y = 0; y < bitonal.height(); ++y)
        {
            for (int x = 0; x < bitonal.width(); ++x)
            {
                const int value = qGray(bitonal.pixel(x, y));
                black += value == 0 ? 1 : 0;
                other += (value != 0 && value != 255) ? 1 : 0;
            }
        }
        QCOMPARE(other, 0);
        QVERIFY(black > 0);
        QVERIFY(black < bitonal.width() * bitonal.height() / 3);
    }
    settings.thresholdMethod = PDFOCRThresholdMethod::Manual;
    settings.manualThreshold = 0;
    const QImage white = PDFOCRImageCompressor::toBitonal(grayScan, settings, nullptr);
    QCOMPARE(qGray(white.pixel(white.width() / 2, white.height() / 2)), 255);
    settings.thresholdMethod = PDFOCRThresholdMethod::Automatic;

    // 0: gray scan of a text, 1: gray photo, 2: black and white image shared by the pages 3 and 4
    std::vector<TestImage> images(3);
    images[0].image = grayScan;
    images[1].image = createPhotoImage(QSize(400, 300), true);
    images[2].image = createBitonalTextImage(QSize(400, 300), 2);
    images[2].bitsPerComponent = 1;
    const PDFDocument document = createImagePagesDocument(images, { 0, 1, 2, 2 }, QSizeF(400, 300));

    using Action = PDFOCRCompressionImageResult::Action;
    using ImageClass = PDFOCRCompressionImageResult::ImageClass;

    // Lossy mode: the scan becomes black and white, the photo is compressed losslessly
    PDFOCRCompressionReport report;
    const PDFDocument converted = PDFOCRImageCompressor::compress(&document, { 0, 1 }, settings, qint64(1) << 30, nullptr, &report);
    QCOMPARE(report.images.size(), size_t(2));
    for (const PDFOCRCompressionImageResult& image : report.images)
    {
        QCOMPARE(image.action, Action::Compressed);
        if (image.pages.front() == 0)
        {
            QCOMPARE(image.imageClass, ImageClass::TextScan);
            QVERIFY(image.encoding == QStringLiteral("JBIG2") || image.encoding == QStringLiteral("CCITT G4") || image.encoding == QStringLiteral("Flate"));
        }
        else
        {
            QCOMPARE(image.imageClass, ImageClass::Picture);
            QCOMPARE(image.encoding, QStringLiteral("Flate"));
        }
    }

    // The converted scan is a 1-bit image, the photo keeps its 8-bit samples and looks the same
    auto getImageDictionary = [&converted](PDFInteger pageIndex)
    {
        const PDFDictionary* resources = converted.getDictionaryFromObject(converted.getCatalog()->getPage(pageIndex)->getResources());
        return converted.getObject(converted.getDictionaryFromObject(resources->get("XObject"))->get("Im1")).getStream()->getDictionary();
    };
    QCOMPARE(getImageDictionary(0)->get("BitsPerComponent").getInteger(), PDFInteger(1));
    QCOMPARE(getImageDictionary(1)->get("BitsPerComponent").getInteger(), PDFInteger(8));
    QVERIFY(renderPage(converted, 1, 72.0) == renderPage(document, 1, 72.0));

    // Lossless mode does not convert the scan
    settings.mode = PDFOCRCompressionMode::Lossless;
    PDFOCRCompressionReport losslessReport;
    const PDFDocument lossless = PDFOCRImageCompressor::compress(&document, { 0 }, settings, qint64(1) << 30, nullptr, &losslessReport);
    QCOMPARE(losslessReport.images.front().encoding, QStringLiteral("Flate"));
    QVERIFY(renderPage(lossless, 0, 72.0) == renderPage(document, 0, 72.0));

    // The image shared with a page, which is not written, is skipped by default
    PDFOCRCompressionReport sharedReport;
    PDFOCRImageCompressor::compress(&document, { 2 }, settings, qint64(1) << 30, nullptr, &sharedReport);
    QCOMPARE(sharedReport.images.front().action, Action::SkippedShared);
    QVERIFY2(sharedReport.images.front().message.contains(QStringLiteral("4")), qPrintable(sharedReport.images.front().message));
    QVERIFY(sharedReport.getSummary().contains(QStringLiteral("1 skipped (shared)")));
    PDFOCRCompressionReport bothPagesReport;
    PDFOCRImageCompressor::compress(&document, { 2, 3 }, settings, qint64(1) << 30, nullptr, &bothPagesReport);
    QCOMPARE(bothPagesReport.images.front().action, Action::Compressed);
    QCOMPARE(bothPagesReport.images.front().pages, (std::vector<PDFInteger>{ 2, 3 }));
    settings.compressSharedImages = true;
    PDFOCRCompressionReport forcedReport;
    PDFOCRImageCompressor::compress(&document, { 2 }, settings, qint64(1) << 30, nullptr, &forcedReport);
    QCOMPARE(forcedReport.images.front().action, Action::Compressed);

    // Excluded image
    settings.excludedImages = { report.images.front().reference, report.images.back().reference };
    PDFOCRCompressionReport excludedReport;
    PDFOCRImageCompressor::compress(&document, { 0, 1 }, settings, qint64(1) << 30, nullptr, &excludedReport);
    QCOMPARE(excludedReport.getCount(Action::SkippedExcluded), 2);
    settings.excludedImages.clear();

    // Preview of a page: original, result and sizes
    settings.mode = PDFOCRCompressionMode::BitonalTextScans;
    const std::vector<PDFOCRImageCompressor::Preview> previews = PDFOCRImageCompressor::createPreview(&document, 0, { 0, 1 }, settings, nullptr);
    QCOMPARE(previews.size(), size_t(1));
    QCOMPARE(previews.front().original.size(), grayScan.size());
    QCOMPARE(previews.front().compressed.size(), grayScan.size());
    QVERIFY(previews.front().result.newBytes > 0);

    // Settings: JSON (the excluded images are not stored) and validation
    PDFOCRCompressionSettings stored = settings;
    stored.thresholdMethod = PDFOCRThresholdMethod::Manual;
    stored.manualThreshold = 97;
    stored.downsample = true;
    stored.downsampleDpi = 200;
    stored.jpegQuality = 70;
    stored.excludedImages = { PDFObjectReference(5, 0) };
    PDFOCRCompressionSettings restored = PDFOCRCompressionSettings::fromJson(stored.toJson());
    QVERIFY(restored.excludedImages.empty());
    restored.excludedImages = stored.excludedImages;
    QVERIFY(restored == stored);
    QVERIFY(PDFOCRCompressionSettings().validate().isEmpty());
    PDFOCRCompressionSettings invalid;
    invalid.manualThreshold = 300;
    invalid.jpegQuality = 0;
    QCOMPARE(invalid.validate().size(), 2);
    QVERIFY(!PDFOCRCompressionSettings().isLossy());
    stored.mode = PDFOCRCompressionMode::Custom;
    QVERIFY(stored.isLossy());

    // The compression is a part of the configuration and of the profiles
    PDFOCRConfiguration configuration;
    configuration.compression = stored;
    configuration.compression.excludedImages.clear();
    QVERIFY(PDFOCRConfiguration::fromJson(configuration.toJson()).compression == configuration.compression);
}

void OCRTest::compressionStreaming()
{
    // Many pages, each with its own black and white image
    constexpr int PageCount = 24;
    std::vector<TestImage> images;
    std::vector<int> pageImages;
    for (int i = 0; i < PageCount; ++i)
    {
        TestImage image;
        image.image = createBitonalTextImage(QSize(400, 300), i);
        image.bitsPerComponent = 1;
        images.push_back(image);
        pageImages.push_back(i);
    }
    const PDFDocument document = createImagePagesDocument(images, pageImages, QSizeF(400, 300));

    std::vector<PDFInteger> pages;
    for (int i = 0; i < PageCount; ++i)
    {
        pages.push_back(i);
    }

    PDFOCRCompressionSettings settings;
    settings.mode = PDFOCRCompressionMode::Lossless;

    // A small budget processes the pages one by one: a single decoded image at a time
    PDFOCRCompressionReport smallBudget;
    PDFOCRImageCompressor::compress(&document, pages, settings, 1, nullptr, &smallBudget);
    QCOMPARE(smallBudget.getCount(PDFOCRCompressionImageResult::Action::Compressed), PageCount);
    QCOMPARE(smallBudget.maximumConcurrentImages, 1);

    // A large budget is bounded by the number of the threads, never by the number of the pages
    PDFOCRCompressionReport largeBudget;
    PDFOCRImageCompressor::compress(&document, pages, settings, qint64(64) << 30, nullptr, &largeBudget);
    QCOMPARE(largeBudget.getCount(PDFOCRCompressionImageResult::Action::Compressed), PageCount);
    QVERIFY(largeBudget.maximumConcurrentImages >= 1);
    QVERIFY(largeBudget.maximumConcurrentImages <= qMax(1, QThread::idealThreadCount()));
}

void OCRTest::compressionWithTextLayer()
{
    std::vector<TestImage> images(2);
    for (int i = 0; i < 2; ++i)
    {
        images[size_t(i)].image = createBitonalTextImage(QSize(600, 400), i);
        images[size_t(i)].bitsPerComponent = 1;
    }
    const PDFDocument document = createImagePagesDocument(images, { 0, 1 }, QSizeF(300, 200));
    const PDFOCRApplyProcessor::Context context = PDFOCRApplyProcessor::createContext(&document, QString());

    PDFOCRSession session(nullptr);
    PDFOCRApplyProcessor::Request request;
    for (PDFInteger page = 0; page < 2; ++page)
    {
        PDFOCRPageResult result = createSampleResult(page, { { QStringLiteral("Scanned"), QRectF(20, 100, 80, 20) } });
        result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(&document, page);
        request.results.push_back(result);
        session.setPageResult(result);
    }
    request.compression.mode = PDFOCRCompressionMode::Lossless;
    session.setDirty(false);

    PDFOCRApplyProcessor::Plan plan = PDFOCRApplyProcessor::createPlan(context, request);
    QVERIFY(PDFOCRApplyProcessor::getSummary(plan).join(QChar('\n')).contains(QStringLiteral("Compression of the scanned images: Lossless")));
    QVERIFY(PDFOCRApplyProcessor::getWarnings(plan, context).join(QChar('\n')).contains(QStringLiteral("save the project again")));
    QVERIFY(!PDFOCRApplyProcessor::getWarnings(plan, context).join(QChar('\n')).contains(QStringLiteral("LOSSY")));

    PDFOCRApplyProcessor::Result result = PDFOCRApplyProcessor::execute(context, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QVERIFY(result.document);
    QCOMPARE(result.compressionReport.getCount(PDFOCRCompressionImageResult::Action::Compressed), 2);
    QCOMPARE(result.report.writtenPages, (std::vector<PDFInteger>{ 0, 1 }));

    // The layer is bound to the compressed page, so it can be opened again for the corrections
    for (PDFInteger page = 0; page < 2; ++page)
    {
        QVERIFY(getImageFilter(*result.document, page) != QByteArray());
        PDFOCRTextLayerWriter::LayerInfo info;
        std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(result.document.data(), page, &info);
        QVERIFY(layer.has_value());
        QVERIFY(info.fingerprintMatches);
        QVERIFY(result.fingerprints[page] != request.results[size_t(page)].pageFingerprint);
    }

    // The session is bound to the new revision of the pages
    session.rebindPageFingerprints(result.fingerprints);
    QVERIFY(session.isDirty());
    QCOMPARE(session.getPage(0)->pageFingerprint, result.fingerprints[0]);
    QCOMPARE(session.getPage(1)->pageFingerprint, PDFOCRPagePreparer::computePageFingerprint(result.document.data(), 1));

    // Written copy is valid after the reading
    PDFDocument reopened = read(write(*result.document));
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&reopened, 0).fingerprintMatches);
    QVERIFY(extractText(reopened, 0).contains(QStringLiteral("Scanned")));
}

void OCRTest::optimizeImagesKeepsLayer()
{
    std::vector<TestImage> images(1);
    images[0].image = createPhotoImage(QSize(300, 200), true);
    const PDFDocument document = createImagePagesDocument(images, { 0 }, QSizeF(300, 200));

    PDFOCRPageResult result = createSampleResult(0, { { QStringLiteral("Photo"), QRectF(20, 100, 80, 20) } });
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer withLayer = applyResults(document, { result }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(withLayer, qPrintable(report.error.message));
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(withLayer.data(), 0).fingerprintMatches);

    // Optimization of the images (only the encoding of the image changes): the layer is rebound
    PDFImageOptimizer::Settings settings = PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = PDFImageOptimizer::ColorMode::Preserve;
    settings.grayProfile.algorithm = PDFImageOptimizer::CompressionAlgorithm::Flate;
    settings.grayProfile.targetDpi = 0;
    settings.colorProfile.targetDpi = 0;
    std::vector<PDFImageOptimizer::ImageResult> imageResults;
    PDFImageOptimizer optimizer;
    const PDFDocument optimized = optimizer.optimize(withLayer.data(), settings, PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &imageResults);
    QCOMPARE(imageResults.size(), size_t(1));
    QVERIFY(!imageResults.front().keptOriginal);
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&optimized, 0) != PDFOCRPagePreparer::computePageFingerprint(withLayer.data(), 0));
    PDFOCRTextLayerWriter::LayerInfo optimizedInfo;
    QVERIFY(PDFOCRTextLayerWriter::readLayer(&optimized, 0, &optimizedInfo).has_value());
    QVERIFY(optimizedInfo.fingerprintMatches);

    // Any other change of the page keeps the layer unbound (the change is not hidden)
    const PDFObjectReference imageReference = imageResults.front().reference;
    PDFDocumentBuilder builder(&optimized);
    const PDFObjectReference pageReference = optimized.getCatalog()->getPage(0)->getPageReference();
    builder.setPageMediaBox(pageReference, QRectF(0, 0, 310, 200));
    const PDFDocument changed = builder.build();
    std::vector<PDFInteger> rebound;
    const PDFDocument rebindAttempt = PDFOCRTextLayerWriter::rebindOptimizedDocument(withLayer.data(), changed, { imageReference }, &rebound);
    QVERIFY(rebound.empty());
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(&rebindAttempt, 0).fingerprintMatches);

    // A layer, which is not bound in the original, stays unbound
    std::vector<PDFInteger> reboundAgain;
    PDFOCRTextLayerWriter::rebindOptimizedDocument(&changed, optimized, { imageReference }, &reboundAgain);
    QVERIFY(reboundAgain.empty());
}

void OCRTest::tesseractAfterDeskew()
{
    // A scan skewed by 4 degrees clockwise is straightened permanently by the preparation
    // of the scan, then recognized: the words lie horizontally on the straightened text
    PageSpec spec;
    spec.size = QSizeF(520, 200);
    spec.withHelvetica = true;
    const QPointF center(260, 100);
    const QTransform skew = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-4.0) * QTransform::fromTranslate(center.x(), center.y());
    spec.content = QStringLiteral("q %1 %2 %3 %4 %5 %6 cm BT /F1 30 Tf 40 110 Td (Straightened scan) Tj ET BT /F1 30 Tf 40 60 Td (of the page) Tj ET Q")
                       .arg(skew.m11()).arg(skew.m12()).arg(skew.m21()).arg(skew.m22()).arg(skew.dx()).arg(skew.dy()).toLatin1();
    const PDFDocument scan = createScanDocument(spec);
    QCOMPARE(scan.getCatalog()->getPageCount(), size_t(1));

    RenderingContext context(&scan);
    PDFOCRPagePreparer preparer = context.createPreparer(&scan);
    const PDFScanPreparation::PageAnalysis analysis = PDFScanPreparation::analyzePage(preparer, &scan, 0, nullptr);
    QVERIFY(analysis.isScan);
    QVERIFY2(std::abs(analysis.skewAngle - 4.0) < 0.5, qPrintable(QString::number(analysis.skewAngle)));

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&scan);
    plan.pages.front().deskewAngle = analysis.skewAngle;
    const PDFScanPreparation::Result prepared = PDFScanPreparation::apply(&scan, plan, nullptr);
    QVERIFY2(prepared.isSuccess() && prepared.document, qPrintable(prepared.errorMessage));

    PDFOCRConfiguration configuration;
    configuration.languages = { QStringLiteral("eng") };
    configuration.dpi = 300.0;
    QString skipReason;
    QString errorMessage;
    std::optional<PDFOCRPageResult> result = recognizeWithTesseract(*prepared.document, configuration, &skipReason, &errorMessage);
    if (!skipReason.isEmpty())
    {
        PDF4QT_OCR_SKIP(qPrintable(skipReason));
    }
    QVERIFY2(result.has_value() && errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(result->getText().contains(QStringLiteral("Straightened")), qPrintable(result->getText()));

    // The words are horizontal on the visible page and lie on the straightened text
    const PDFPage* page = prepared.document->getCatalog()->getPage(0);
    const QTransform pageToVisible = PDFScanPreparation::getPageToVisible(page);
    for (const PDFOCRWord* word : result->getWords())
    {
        const QPointF bottomLeft = pageToVisible.map(word->quad.points[0]);
        const QPointF bottomRight = pageToVisible.map(word->quad.points[1]);
        const double angle = std::atan2(bottomRight.y() - bottomLeft.y(), bottomRight.x() - bottomLeft.x()) * 180.0 / std::numbers::pi;
        QVERIFY2(std::abs(angle) < 1.0, qPrintable(QStringLiteral("%1: %2").arg(word->text).arg(angle)));
    }

    // The text layer written onto the straightened page is found at the text
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer withLayer = applyResults(*prepared.document, { *result }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(withLayer, qPrintable(report.error.message));
    QVERIFY(extractText(*withLayer, 0).contains(QStringLiteral("Straightened")));
}

void OCRTest::perspectiveCorrection()
{
    // A "photographed" page: the rendered text is warped by a known projective
    // transformation into a trapezoid and the result is the image of the page
    PageSpec spec;
    spec.size = QSizeF(400, 300);
    spec.withHelvetica = true;
    spec.content = "BT /F1 30 Tf 40 190 Td (Perspective text) Tj ET BT /F1 30 Tf 40 120 Td (of a photo) Tj ET";
    PDFDocument textDocument = createDocument({ spec });
    QImage original;
    {
        RenderingContext textContext(&textDocument);
        PDFOCRPagePreparer textPreparer = textContext.createPreparer(&textDocument);
        original = textPreparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr).image.convertToFormat(QImage::Format_RGB32);
    }
    QVERIFY(!original.isNull());
    const QSizeF imageSize = original.size();

    // Corners of the document on the photo (image coordinates): TL, TR, BR, BL
    const QPolygonF photoCorners = QPolygonF() << QPointF(0.09 * imageSize.width(), 0.08 * imageSize.height())
                                               << QPointF(0.91 * imageSize.width(), 0.13 * imageSize.height())
                                               << QPointF(0.96 * imageSize.width(), 0.92 * imageSize.height())
                                               << QPointF(0.05 * imageSize.width(), 0.96 * imageSize.height());
    QTransform warp;
    QVERIFY(QTransform::quadToQuad(QPolygonF(QRectF(QPointF(0, 0), imageSize)), photoCorners, warp));
    QImage photo(original.size(), QImage::Format_RGB32);
    photo.fill(Qt::white);
    {
        QPainter painter(&photo);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setTransform(warp);
        painter.drawImage(QPointF(0, 0), original);
    }
    PDFDocument document = createImageDocument(photo, spec.size);

    // Image of the page -> canonical page space
    const QTransform imageToPage = QTransform::fromScale(spec.size.width() / imageSize.width(), -spec.size.height() / imageSize.height()) * QTransform::fromTranslate(0, spec.size.height());
    PDFOCRQuad corners;
    corners.points[0] = imageToPage.map(photoCorners[3]);   // bottom-left
    corners.points[1] = imageToPage.map(photoCorners[2]);   // bottom-right
    corners.points[2] = imageToPage.map(photoCorners[1]);   // top-right
    corners.points[3] = imageToPage.map(photoCorners[0]);   // top-left

    // Validation of the corners
    const QRectF pageRect(QPointF(0, 0), spec.size);
    QVERIFY2(PDFOCRPagePreparer::validatePerspective(corners, pageRect).isEmpty(), qPrintable(PDFOCRPagePreparer::validatePerspective(corners, pageRect)));
    PDFOCRQuad crossed = corners;
    std::swap(crossed.points[1], crossed.points[2]);
    QVERIFY(!PDFOCRPagePreparer::validatePerspective(crossed, pageRect).isEmpty());
    PDFOCRQuad tinyQuad = PDFOCRQuad::fromRect(QRectF(10, 10, 50, 40));
    QVERIFY(!PDFOCRPagePreparer::validatePerspective(tinyQuad, pageRect).isEmpty());
    PDFOCRQuad sharp = PDFOCRQuad::fromRect(QRectF(0, 0, 400, 300));
    sharp.points[2] = QPointF(10, 295);
    QVERIFY(!PDFOCRPagePreparer::validatePerspective(sharp, pageRect).isEmpty());

    // Preprocessing: the corrected raster is the rectangle of the document and the
    // corners of the raster map back onto the corners of the document on the photo
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    const PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    QVERIFY(!raster.error);
    PDFOCRPreprocessing preprocessing;
    preprocessing.perspective = corners;
    preprocessing.deskew = true;
    const PDFOCRPagePreparer::PreprocessResult corrected = PDFOCRPagePreparer::preprocess(raster.image, raster.geometry, preprocessing, std::nullopt, nullptr);
    QVERIFY2(!corrected.error, qPrintable(corrected.error.message));
    QVERIFY(corrected.geometry.pipeline.join(QChar(' ')).contains(QStringLiteral("perspective(")));
    QVERIFY(corrected.geometry.pipeline.contains(QStringLiteral("deskew(skipped,perspective)")));
    const QTransform engineToPage = corrected.geometry.getEngineToPage();
    const QSizeF engineSize = corrected.image.size();
    QVERIFY(QLineF(engineToPage.map(QPointF(0, 0)), corners.points[3]).length() < 1.0);
    QVERIFY(QLineF(engineToPage.map(QPointF(engineSize.width(), engineSize.height())), corners.points[1]).length() < 1.0);

    // Invalid corners are an error of the page
    PDFOCRPreprocessing invalid;
    invalid.perspective = crossed;
    QVERIFY(PDFOCRPagePreparer::preprocess(raster.image, raster.geometry, invalid, std::nullopt, nullptr).error);

    // Recognition by the test engine: the word of the corrected image lies on the text of the photo
    const QRectF engineWord(0.2 * engineSize.width(), 0.3 * engineSize.height(), 0.4 * engineSize.width(), 0.1 * engineSize.height());
    m_testEngine->setHandler([engineWord](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        PDFOCRRawBlock block;
        block.rect = engineWord;
        PDFOCRRawLine line;
        line.rect = engineWord;
        PDFOCRRawWord word;
        word.text = QStringLiteral("Perspective");
        word.rect = engineWord;
        word.rawConfidence = 90.0;
        line.words.push_back(word);
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
    description.configuration.dpi = 300.0;
    description.models.dataPath = QStringLiteral("/none");
    description.models.languages = { QStringLiteral("eng") };
    PDFOCRPageOverride pageOverride;
    pageOverride.perspective = corners;
    PDFOCRPageTask task;
    task.pageIndex = 0;
    task.configuration = pageOverride.apply(description.configuration);
    task.generation = 1;
    description.pages.push_back(task);

    std::optional<PDFOCRPageResult> result;
    connect(&controller, &PDFOCRJobController::pageFinished, this, [&](int, PDFOCRPageResult pageResult) { result = std::move(pageResult); });
    int generation = 0;
    QVERIFY(controller.start(description, &generation));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 30000);
    controller.waitForFinished();
    m_testEngine->setHandler(PDFOCRTestEngineFactory::Handler());
    QVERIFY2(result->state == PDFOCRPageState::Done, qPrintable(result->error.message));

    // Expected position: the corrected image is the original image scaled to its size,
    // the original image was warped onto the photo
    auto expectedPagePoint = [&](QPointF enginePoint)
    {
        const QPointF originalPoint(enginePoint.x() * imageSize.width() / engineSize.width(), enginePoint.y() * imageSize.height() / engineSize.height());
        return imageToPage.map(warp.map(originalPoint));
    };
    const PDFOCRWord* word = result->getWords().front();
    QVERIFY2(QLineF(word->quad.points[0], expectedPagePoint(engineWord.bottomLeft())).length() < 2.0, qPrintable(QStringLiteral("%1 %2").arg(word->quad.points[0].x()).arg(word->quad.points[0].y())));
    QVERIFY2(QLineF(word->quad.points[1], expectedPagePoint(engineWord.bottomRight())).length() < 2.0, qPrintable(QStringLiteral("%1 %2").arg(word->quad.points[1].x()).arg(word->quad.points[1].y())));

    // The word is a parallelogram (the text layer can place it), the layer is written
    const QPointF bottom = word->quad.points[1] - word->quad.points[0];
    const QPointF top = word->quad.points[2] - word->quad.points[3];
    QVERIFY(QLineF(QPointF(), bottom - top).length() < 1e-6);
    PDFOCRTextLayerWriter::Report report;
    PDFDocumentPointer withLayer = applyResults(document, { *result }, PDFOCRTextLayerWriter::Options(), &report);
    QVERIFY2(withLayer, qPrintable(report.error.message));
    QVERIFY(extractText(*withLayer, 0).contains(QStringLiteral("Perspective")));

    // Serialization: the page override, the configuration and the project
    const PDFOCRPageOverride restoredOverride = PDFOCRPageOverride::fromJson(pageOverride.toJson());
    QVERIFY(restoredOverride.perspective.has_value());
    QVERIFY(restoredOverride == pageOverride);
    QVERIFY(!restoredOverride.isEmpty());
    PDFOCRConfiguration configuration = pageOverride.apply(PDFOCRConfiguration());
    QVERIFY(PDFOCRConfiguration::fromJson(configuration.toJson()) == configuration);
    QVERIFY(!PDFOCRPreprocessing::fromJson(PDFOCRPreprocessing().toJson()).perspective.has_value());

    // Tesseract reads the corrected photo
    PDFOCRConfiguration tesseractConfiguration;
    tesseractConfiguration.languages = { QStringLiteral("eng") };
    tesseractConfiguration.dpi = 300.0;
    tesseractConfiguration.preprocessing.perspective = corners;
    QString skipReason;
    QString errorMessage;
    std::optional<PDFOCRPageResult> tesseractResult = recognizeWithTesseract(document, tesseractConfiguration, &skipReason, &errorMessage);
    if (!skipReason.isEmpty())
    {
        PDF4QT_OCR_SKIP(qPrintable(skipReason));
    }
    QVERIFY2(tesseractResult.has_value() && errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(tesseractResult->getText().contains(QStringLiteral("Perspective")), qPrintable(tesseractResult->getText()));
    QVERIFY2(tesseractResult->getText().contains(QStringLiteral("photo")), qPrintable(tesseractResult->getText()));
}

// -------------------------------------------------------------------------
// Phase 7 of OCR_PLAN.md: recognition of a document without a user interface
// -------------------------------------------------------------------------

void OCRTest::documentRunner()
{
    PageSpec scanPage;
    scanPage.withImage = true;

    PageSpec numberedScanPage;
    numberedScanPage.withHelvetica = true;
    numberedScanPage.withImage = true;
    numberedScanPage.content = "BT /F1 10 Tf 280 10 Td (12) Tj ET";

    PageSpec textPage;
    textPage.withHelvetica = true;
    textPage.content = "BT /F1 12 Tf 20 150 Td (This is a page with a usable amount of visible digital text, ) Tj 0 -14 Td (which does not need any recognition at all.) Tj ET";

    PageSpec wideScanPage;
    wideScanPage.withImage = true;
    wideScanPage.size = QSizeF(320, 200);

    PageSpec mixedPage;
    mixedPage.withHelvetica = true;
    mixedPage.withImage = true;
    mixedPage.content = "BT /F1 12 Tf 20 180 Td (This is a page with a usable amount of visible digital text and an image.) Tj ET";

    // 0 scan, 1 scan with a digital page number, 2 digital text, 3 wide scan, 4 text and image
    const PDFDocument document = createDocument({ scanPage, numberedScanPage, textPage, wideScanPage, mixedPage });

    // The wide page fails on request, the other pages have a single word in the middle
    std::atomic<bool> failWidePage = { true };
    std::atomic<int> callCount = { 0 };
    const int wideImageWidth = qRound(320.0 / 72.0 * 300.0);
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([&](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        ++callCount;
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        if (failWidePage && qAbs(input.image.width() - wideImageWidth) <= 2)
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::WorkerCrashed, QStringLiteral("Simulated failure"), QStringLiteral("Recognition"));
            return output;
        }

        const QRectF rect(input.image.width() * 0.2, input.image.height() * 0.4, input.image.width() * 0.4, input.image.height() * 0.1);
        PDFOCRRawBlock block;
        block.rect = rect;
        PDFOCRRawLine line;
        line.rect = rect;
        PDFOCRRawWord word;
        word.text = QStringLiteral("word");
        word.rect = rect;
        word.rawConfidence = 90.0;
        line.words.push_back(word);
        block.lines.push_back(line);
        output.blocks.push_back(block);
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        return output;
    });

    PDFOCRDocumentRunner::Settings settings;
    settings.configuration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    settings.configuration.languages = { QStringLiteral("eng") };
    settings.configuration.workerCount = 2;
    settings.configuration.detectBlankPages = false;

    auto findRecord = [](const PDFOCRDocumentRunner::Result& result, PDFInteger page) -> const PDFOCRDocumentRunner::PageRecord&
    {
        static const PDFOCRDocumentRunner::PageRecord none;
        auto it = std::find_if(result.records.cbegin(), result.records.cend(), [page](const auto& record) { return record.pageIndex == page; });
        return it != result.records.cend() ? *it : none;
    };

    // Pages needing a decision are skipped by default, a failed page is reported
    std::vector<int> progressCalls;
    PDFOCRDocumentRunner::Result result = PDFOCRDocumentRunner::run(&document, settings, [&](int finished, int total, const PDFOCRPageResult* page)
    {
        if (page)
        {
            progressCalls.push_back(finished);
            QCOMPARE(total, 2);
        }
    }, nullptr);
    QVERIFY2(result.errorMessage.isEmpty(), qPrintable(result.errorMessage));
    QVERIFY(!result.cancelled);
    QCOMPARE(result.records.size(), size_t(5));
    QCOMPARE(progressCalls, std::vector<int>({ 1, 2 }));
    QCOMPARE(findRecord(result, 0).state, PDFOCRPageState::Done);
    QCOMPARE(findRecord(result, 0).source, PDFOCRDocumentRunner::PageSource::Recognized);
    QCOMPARE(findRecord(result, 0).statistics.wordCount, 1);
    QCOMPARE(findRecord(result, 1).state, PDFOCRPageState::Skipped);
    QVERIFY(findRecord(result, 1).message.contains(QStringLiteral("decision")));
    QCOMPARE(findRecord(result, 2).state, PDFOCRPageState::Skipped);
    QCOMPARE(findRecord(result, 2).source, PDFOCRDocumentRunner::PageSource::Skipped);
    QCOMPARE(findRecord(result, 3).state, PDFOCRPageState::Error);
    QVERIFY(findRecord(result, 3).message.contains(QStringLiteral("Simulated failure")));
    QCOMPARE(findRecord(result, 4).state, PDFOCRPageState::Skipped);
    QCOMPARE(result.getFailedPageCount(), 1);
    QCOMPARE(result.getResultPageCount(), 1);
    QCOMPARE(result.summary.errorPages, 1);
    QCOMPARE(result.getStatistics().wordCount, 1);

    // Masking: the scan with the digital page number is recognized with the number
    // masked; the page with a visible text cannot be masked
    failWidePage = false;
    settings.decisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;
    result = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QCOMPARE(result.getFailedPageCount(), 0);
    QCOMPARE(findRecord(result, 1).state, PDFOCRPageState::Done);
    QVERIFY(findRecord(result, 1).masked);
    QVERIFY(!findRecord(result, 1).reviewOnly);
    QCOMPARE(findRecord(result, 3).state, PDFOCRPageState::Done);
    QCOMPARE(findRecord(result, 4).state, PDFOCRPageState::Skipped);
    QVERIFY(findRecord(result, 4).message.contains(QStringLiteral("cannot be masked")));

    // The copy with the text layer
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceFileName = directory.filePath(QStringLiteral("source.pdf"));
    const QString copyFileName = directory.filePath(QStringLiteral("copy.pdf"));
    {
        QFile sourceFile(sourceFileName);
        QVERIFY(sourceFile.open(QFile::WriteOnly));
        sourceFile.write(write(document));
    }

    PDFOCRDocumentRunner::WriteOptions writeOptions;
    writeOptions.sourceFileName = sourceFileName;
    writeOptions.outputFileName = sourceFileName;
    PDFOCRApplyProcessor::Plan plan;
    PDFOCRApplyProcessor::Result writeResult = PDFOCRDocumentRunner::writeCopy(&document, result, writeOptions, &plan, nullptr);
    QVERIFY2(!writeResult.isSuccess(), "The copy must never overwrite the source document");

    writeOptions.outputFileName = copyFileName;
    writeResult = PDFOCRDocumentRunner::writeCopy(&document, result, writeOptions, &plan, nullptr);
    QVERIFY2(writeResult.isSuccess(), qPrintable(writeResult.errorMessage));
    QCOMPARE(plan.getPages(), std::vector<PDFInteger>({ 0, 1, 3 }));
    QVERIFY(QFileInfo::exists(copyFileName));
    {
        QFile copyFile(copyFileName);
        QVERIFY(copyFile.open(QFile::ReadOnly));
        const PDFDocument copy = read(copyFile.readAll());
        QCOMPARE(copy.getCatalog()->getPageCount(), size_t(5));
        for (PDFInteger page : { 0, 1, 3 })
        {
            const std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(&copy, page);
            QVERIFY2(layer.has_value(), qPrintable(QStringLiteral("page %1").arg(page)));
            QCOMPARE(layer->getText().trimmed(), QStringLiteral("word"));
        }
        QVERIFY(!PDFOCRTextLayerWriter::readLayer(&copy, 2).has_value());
        QVERIFY(!PDFOCRTextLayerWriter::readLayer(&copy, 4).has_value());
    }

    // Exports of the results of the selected pages
    const std::vector<const PDFOCRPageResult*> exportResults = PDFOCRDocumentRunner::getExportResults(result);
    QCOMPARE(exportResults.size(), size_t(5));
    PDFOCRTextExporter::Report textReport;
    const QString text = PDFOCRTextExporter::exportText(exportResults, PDFOCRTextExporter::Options(), &textReport);
    QCOMPARE(text.count(QStringLiteral("word")), 3);
    QCOMPARE(textReport.exportedPages, std::vector<PDFInteger>({ 0, 1, 3 }));

    // Review only: the pages needing a decision are recognized, but never written
    settings.decisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::ReviewOnly;
    PDFOCRDocumentRunner::Result reviewResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QVERIFY(reviewResult.isSuccess());
    QVERIFY(findRecord(reviewResult, 1).reviewOnly);
    QVERIFY(findRecord(reviewResult, 4).reviewOnly);
    QCOMPARE(findRecord(reviewResult, 4).state, PDFOCRPageState::Done);
    QVERIFY(!findRecord(reviewResult, 0).reviewOnly);
    writeOptions.outputFileName = directory.filePath(QStringLiteral("review.pdf"));
    writeResult = PDFOCRDocumentRunner::writeCopy(&document, reviewResult, writeOptions, &plan, nullptr);
    QVERIFY2(writeResult.isSuccess(), qPrintable(writeResult.errorMessage));
    QCOMPARE(plan.getPages(), std::vector<PDFInteger>({ 0, 3 }));

    // Page selection
    settings.pages = { 3 };
    PDFOCRDocumentRunner::Result selectedResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QVERIFY(selectedResult.isSuccess());
    QCOMPARE(selectedResult.records.size(), size_t(1));
    QCOMPARE(selectedResult.records.front().pageIndex, PDFInteger(3));
    settings.pages = { 7 };
    QVERIFY(!PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr).errorMessage.isEmpty());
    settings.pages.clear();

    // Project: the corrected results of the unchanged pages are used without a new
    // recognition, a changed page is recognized again
    const PDFOCRPageResult* firstPage = result.session->getPage(0);
    QVERIFY(firstPage && !firstPage->getWords().empty());
    QVERIFY(result.session->setWordText(0, firstPage->getWords().front()->id, QStringLiteral("Corrected")));
    PDFOCRProject project = result.session->createProject(result.pages);
    QVERIFY(project.pages.count(3));
    project.pages[3].pageFingerprint = QByteArray("changed page");

    settings.decisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;
    settings.project = project;
    const int callsBefore = callCount;
    PDFOCRDocumentRunner::Result projectResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QVERIFY2(projectResult.isSuccess(), qPrintable(projectResult.errorMessage));
    QCOMPARE(callCount - callsBefore, 1);
    QCOMPARE(findRecord(projectResult, 0).source, PDFOCRDocumentRunner::PageSource::Project);
    QCOMPARE(findRecord(projectResult, 1).source, PDFOCRDocumentRunner::PageSource::Project);
    QCOMPARE(findRecord(projectResult, 3).source, PDFOCRDocumentRunner::PageSource::Recognized);
    QCOMPARE(projectResult.session->getPage(0)->getText().trimmed(), QStringLiteral("Corrected"));
    QCOMPARE(projectResult.warnings.size(), qsizetype(1));
    QVERIFY(projectResult.warnings.front().contains(QStringLiteral("different content")));
    settings.project.reset();

    // A page too large for the engine is an error without the consent to a reduced resolution
    m_testEngine->setMaximumImageSize(QSize(800, 800));
    settings.pages = { 0 };
    PDFOCRDocumentRunner::Result largeResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QVERIFY(largeResult.errorMessage.isEmpty());
    QCOMPARE(findRecord(largeResult, 0).state, PDFOCRPageState::Error);
    QCOMPARE(largeResult.session->getPage(0)->error.code, PDFOCRErrorCode::ImageTooLarge);
    settings.allowReducedResolution = true;
    largeResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, nullptr);
    QCOMPARE(findRecord(largeResult, 0).state, PDFOCRPageState::Done);
    QCOMPARE(largeResult.warnings.size(), qsizetype(1));
    m_testEngine->setMaximumImageSize(QSize());
    settings.allowReducedResolution = false;
    settings.pages.clear();

    // Invalid configuration and an engine with managed models without the model manager
    PDFOCRDocumentRunner::Settings invalidSettings = settings;
    invalidSettings.configuration.languages.clear();
    QVERIFY(!PDFOCRDocumentRunner::run(&document, invalidSettings, nullptr, nullptr).errorMessage.isEmpty());
    invalidSettings = settings;
    invalidSettings.configuration.engineId = QStringLiteral("unknown-engine");
    QCOMPARE(PDFOCRDocumentRunner::run(&document, invalidSettings, nullptr, nullptr).errorCode, PDFOCRErrorCode::InvalidConfiguration);

    // Processing of a file: the copy, the exports and the project are written
    {
        PDFOCRDocumentRunner::FileTask task;
        task.inputFile = sourceFileName;
        task.outputFile = directory.filePath(QStringLiteral("file.pdf"));
        task.exportText = directory.filePath(QStringLiteral("file.txt"));
        task.exportAlto = directory.filePath(QStringLiteral("file.xml"));
        task.saveProjectFile = directory.filePath(QStringLiteral("file.pdf4qt-ocr"));
        PDFOCRConfiguration taskConfiguration = settings.configuration;
        task.configure = [&taskConfiguration](const PDFOCRProject* project, PDFOCRConfiguration* configuration)
        {
            *configuration = project ? project->configuration : taskConfiguration;
            return QString();
        };

        PDFOCRDocumentRunner::Settings fileSettings;
        fileSettings.decisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;

        failWidePage = true;
        PDFOCRDocumentRunner::FileResult fileResult = PDFOCRDocumentRunner::processFile(task, fileSettings, nullptr, nullptr);
        QCOMPARE(fileResult.status, PDFOCRDocumentRunner::FileResult::Status::Failed);
        QCOMPARE(fileResult.stage, PDFOCRDocumentRunner::FileResult::Stage::PageErrors);
        QVERIFY(!QFileInfo::exists(task.outputFile));
        QVERIFY(!QFileInfo::exists(task.exportText));
        QVERIFY(!QFileInfo::exists(task.saveProjectFile));

        task.allowPageErrors = true;
        fileResult = PDFOCRDocumentRunner::processFile(task, fileSettings, nullptr, nullptr);
        QCOMPARE(fileResult.status, PDFOCRDocumentRunner::FileResult::Status::Success);
        QVERIFY(fileResult.message.contains(QStringLiteral("failed")));
        QCOMPARE(fileResult.pageCount, 5);
        QCOMPARE(fileResult.plan.getPages(), std::vector<PDFInteger>({ 0, 1 }));
        QVERIFY(QFileInfo::exists(task.outputFile));
        QVERIFY(QFileInfo::exists(task.exportAlto));
        QVERIFY(QFileInfo::exists(task.saveProjectFile));
        QVERIFY(!fileResult.messages.isEmpty());
        {
            QFile textFile(task.exportText);
            QVERIFY(textFile.open(QFile::ReadOnly));
            QCOMPARE(QString::fromUtf8(textFile.readAll()).count(QStringLiteral("word")), 2);
        }

        // The saved project: its pages are used without a new recognition
        failWidePage = false;
        PDFOCRDocumentRunner::FileTask projectTask = task;
        projectTask.outputFile.clear();
        projectTask.saveProjectFile.clear();
        projectTask.exportAlto.clear();
        projectTask.exportText = directory.filePath(QStringLiteral("project.txt"));
        projectTask.projectFile = task.saveProjectFile;
        projectTask.allowPageErrors = false;
        const int callsBeforeProject = callCount;
        fileResult = PDFOCRDocumentRunner::processFile(projectTask, fileSettings, nullptr, nullptr);
        QVERIFY2(fileResult.status == PDFOCRDocumentRunner::FileResult::Status::Success, qPrintable(fileResult.message));
        QCOMPARE(callCount - callsBeforeProject, 1);    // only the failed page
        QCOMPARE(findRecord(fileResult.result, 0).source, PDFOCRDocumentRunner::PageSource::Project);
        QCOMPARE(findRecord(fileResult.result, 3).source, PDFOCRDocumentRunner::PageSource::Recognized);

        // A page selection without a recognizable page writes nothing
        PDFOCRDocumentRunner::FileTask textTask = task;
        textTask.outputFile = directory.filePath(QStringLiteral("text.pdf"));
        textTask.exportText = directory.filePath(QStringLiteral("text.txt"));
        textTask.exportAlto.clear();
        textTask.saveProjectFile.clear();
        textTask.selectPages = [](PDFInteger, std::vector<PDFInteger>* pages) { *pages = { 2 }; return QString(); };
        fileResult = PDFOCRDocumentRunner::processFile(textTask, fileSettings, nullptr, nullptr);
        QCOMPARE(fileResult.status, PDFOCRDocumentRunner::FileResult::Status::Skipped);
        QCOMPARE(fileResult.stage, PDFOCRDocumentRunner::FileResult::Stage::NothingToWrite);
        QVERIFY(!QFileInfo::exists(textTask.outputFile));
        QVERIFY(!QFileInfo::exists(textTask.exportText));

        // Errors of the configuration and of the reading
        textTask.selectPages = nullptr;
        textTask.configure = [](const PDFOCRProject*, PDFOCRConfiguration*) { return QStringLiteral("No language"); };
        fileResult = PDFOCRDocumentRunner::processFile(textTask, fileSettings, nullptr, nullptr);
        QCOMPARE(fileResult.stage, PDFOCRDocumentRunner::FileResult::Stage::Configuration);
        QCOMPARE(fileResult.message, QStringLiteral("No language"));

        textTask.inputFile = directory.filePath(QStringLiteral("missing.pdf"));
        fileResult = PDFOCRDocumentRunner::processFile(textTask, fileSettings, nullptr, nullptr);
        QCOMPARE(fileResult.status, PDFOCRDocumentRunner::FileResult::Status::Failed);
        QCOMPARE(fileResult.stage, PDFOCRDocumentRunner::FileResult::Stage::Reading);
    }

    // Cancellation
    PDFOCRCancelToken cancelToken;
    cancelToken.cancel();
    const int callsBeforeCancel = callCount;
    PDFOCRDocumentRunner::Result cancelledResult = PDFOCRDocumentRunner::run(&document, settings, nullptr, &cancelToken);
    QVERIFY(cancelledResult.cancelled);
    QVERIFY(!cancelledResult.isSuccess());
    QCOMPARE(callCount - callsBeforeCancel, 0);

    m_testEngine->setHandler(PDFOCRTestEngineFactory::Handler());
}

QTEST_MAIN(OCRTest)

#include "tst_ocrtest.moc"
