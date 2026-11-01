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
#include "pdfdocumentsanitizer.h"
#include "pdfdocumentsizebreakdown.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfimage.h"
#include "pdfimagecompressor.h"
#include "pdfimageoptimizer.h"
#include "pdfoperationcontrol.h"
#include "pdfoptimizer.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdfstreamfilters.h"

#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QtTest>

#include <atomic>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

/// Cancellation of the tests
class CompressTestControl : public pdf::PDFOperationControl
{
public:
    void cancel() { m_isCanceled.store(true); }

    virtual bool isOperationCancelled() const override
    {
        m_checkCount.fetch_add(1);
        return m_isCanceled.load();
    }

    int getCheckCount() const { return m_checkCount.load(); }

private:
    std::atomic<bool> m_isCanceled = false;
    mutable std::atomic<int> m_checkCount = 0;
};

/// Records the progress of a run; cancels it when a step of the given text comes
class CompressTestListener : public pdf::PDFDocumentToolProgressListener
{
public:
    QList<int> percentages;
    QStringList texts;
    QString cancelOnText;
    CompressTestControl* control = nullptr;

    virtual void onToolProgress(int percentage, const QString& stepText) override
    {
        percentages << percentage;
        if (texts.isEmpty() || texts.back() != stepText)
        {
            texts << stepText;
        }

        if (control && !cancelOnText.isEmpty() && stepText.startsWith(cancelOnText))
        {
            control->cancel();
        }
    }
};

/// Documents of the tests, built by PDFDocumentBuilder
class CompressTestHelper
{
public:
    CompressTestHelper() = delete;

    /// Pixels of a photograph: a gradient with noise. Flate compresses it badly, so
    /// JPEG makes it smaller; the analysis of the optimizer classifies it as a photo.
    static QImage createPhoto(int size, quint32 seed)
    {
        QImage image(size, size, QImage::Format_RGB32);
        QRandomGenerator generator(seed);
        for (int y = 0; y < size; ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < size; ++x)
            {
                const int noise = generator.bounded(24);
                line[x] = qRgb((x * 200) / size + noise, (y * 200) / size + noise, ((x + y) * 100) / size + noise);
            }
        }
        return image;
    }

    /// Image, which takes little of the file and much of the memory when decoded
    static QImage createFlatImage(int size, quint32 seed)
    {
        QImage image(size, size, QImage::Format_RGB32);
        image.fill(qRgb(40 + seed % 200, 200 - seed % 150, 90));
        return image;
    }

    static pdf::PDFObject createStreamObject(QByteArray content, pdf::PDFDictionaryBuilder dictionary = pdf::PDFDictionaryBuilder())
    {
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
    }

    /// Form XObject drawing the XObjects of its names
    static pdf::PDFObject createFormObject(const QByteArray& content, const std::vector<std::pair<QByteArray, pdf::PDFObjectReference>>& xobjects)
    {
        pdf::PDFDictionaryBuilder xobjectDictionary;
        for (const auto& item : xobjects)
        {
            xobjectDictionary.addEntry(pdf::PDFInplaceOrMemoryString(item.first), pdf::PDFObject::createReference(item.second));
        }

        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xobjectDictionary)));

        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
        return createStreamObject(content, std::move(dictionary));
    }

    static pdf::PDFObjectReference addImage(pdf::PDFDocumentBuilder& builder, const QImage& image)
    {
        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::Flate;
        options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;
        options.enablePngPredictor = false;
        const pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(image, options);
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(stream)));
    }

    /// Sets the content of a page drawing XObjects by their names
    static void setPage(pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page, const QByteArray& content, const std::vector<std::pair<QByteArray, pdf::PDFObjectReference>>& xobjects)
    {
        const pdf::PDFObjectReference contentReference = builder.addObject(createStreamObject(content));

        pdf::PDFDictionaryBuilder xobjectDictionary;
        for (const auto& item : xobjects)
        {
            xobjectDictionary.addEntry(pdf::PDFInplaceOrMemoryString(item.first), pdf::PDFObject::createReference(item.second));
        }

        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xobjectDictionary)));

        pdf::PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    }

    /// Content drawing the image of the name Im at 288 dpi: size / 288 inches = size / 4 points
    /// (images of at most 256 x 256 pixels: Qt converts and scales larger ones in its own
    /// threads, which ThreadSanitizer does not see through - it would report them)
    static QByteArray createImageContent(int imageSize)
    {
        const QByteArray points = QByteArray::number(imageSize / 4.0);
        return "q " + points + " 0 0 " + points + " 0 0 cm /Im Do Q";
    }

    /// Pages of 200 x 200 points, each drawing its own image at 288 dpi
    static pdf::PDFDocument createPhotoDocument(int pageCount, int imageSize)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            setPage(builder, page, createImageContent(imageSize), { { "Im", addImage(builder, createPhoto(imageSize, quint32(i + 1))) } });
        }
        return builder.build();
    }

    /// The document with another version of its header
    static pdf::PDFDocument withVersion(const pdf::PDFDocument& document, pdf::PDFVersion version)
    {
        pdf::PDFObjectStorage storage = document.getStorage();
        return pdf::PDFDocument(std::move(storage), version, QByteArray());
    }

    static QByteArray write(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        const pdf::PDFOperationResult result = writer.write(&buffer, &document);
        return result ? data : QByteArray();
    }

    static pdf::PDFDocument read(const QByteArray& data, const QString& password = QString(), pdf::PDFDocumentReader::Result* readerResult = nullptr, bool authorizeOwnerOnly = false)
    {
        const auto getPassword = [&password](bool* ok)
        {
            *ok = !password.isEmpty();
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, true, authorizeOwnerOnly);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        if (readerResult)
        {
            *readerResult = reader.getReadingResult();
        }
        return document;
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, QByteArray originalData = QByteArray(), const QString& filePath = QStringLiteral("Project_Report.pdf"), bool isModified = false)
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), std::move(originalData), filePath, isModified));
    }

    static QVariantMap preset(const char* id)
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("preset"), QString::fromLatin1(id));
        return parameters;
    }

    /// Widths of all image XObjects of a document (the optimizer changes their dictionaries)
    static std::vector<int> getImageWidths(const pdf::PDFDocument& document)
    {
        std::vector<int> widths;
        pdf::PDFDocumentDataLoaderDecorator loader(&document);
        for (const pdf::PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
        {
            if (entry.object.isStream() && loader.readNameFromDictionary(entry.object.getStream()->getDictionary(), "Subtype") == "Image")
            {
                widths.push_back(int(loader.readIntegerFromDictionary(entry.object.getStream()->getDictionary(), "Width", 0)));
            }
        }
        return widths;
    }

    /// Stream of the image object of a document with the given reference
    static const pdf::PDFStream* getStream(const pdf::PDFDocument& document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFObject& object = document.getObjectByReference(reference);
        return object.isStream() ? object.getStream() : nullptr;
    }

    /// Result of drawing a page of a document: the errors of the content processor and the
    /// page of 100 x 100 points drawn into 100 x 100 pixels (the origin at the bottom left)
    struct DrawnPage
    {
        QList<pdf::PDFRenderError> errors;
        QImage image;

        /// Color of the point of the page in its coordinates
        QRgb at(int x, int y) const { return image.pixel(x, 99 - y) | 0xFF000000u; }

        /// Number of the errors of the text
        int count(const QString& message) const
        {
            int result = 0;
            for (const pdf::PDFRenderError& error : errors)
            {
                result += error.message == message ? 1 : 0;
            }
            return result;
        }
    };

    static DrawnPage draw(pdf::PDFDocument document, size_t pageIndex = 0)
    {
        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        fontCache.setDocument(pdf::PDFModifiedDocument(&document, nullptr));
        pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
        pdf::PDFCMSGeneric cms;

        DrawnPage page;
        page.image = QImage(100, 100, QImage::Format_ARGB32_Premultiplied);
        page.image.fill(Qt::white);
        QPainter painter(&page.image);
        pdf::PDFRenderer renderer(&document, &fontCache, nullptr, &cms, &optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());
        page.errors = renderer.render(&painter, QRectF(0, 0, 100, 100), pageIndex);
        painter.end();
        return page;
    }

    /// Image XObject of raw samples. Each key of \p extra is added to the dictionary.
    static pdf::PDFObjectReference addRawImage(pdf::PDFDocumentBuilder& builder, int width, int height, int bits, pdf::PDFObject colorSpace, const QByteArray& samples, std::vector<std::pair<QByteArray, pdf::PDFObject>> extra = { })
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(width));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(height));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(bits));
        if (!colorSpace.isNull())
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), std::move(colorSpace));
        }
        for (auto& item : extra)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(item.first), std::move(item.second));
        }
        return builder.addObject(createStreamObject(samples, std::move(dictionary)));
    }

    static pdf::PDFObject createNumberArray(const std::vector<double>& numbers)
    {
        pdf::PDFArrayBuilder array;
        for (const double number : numbers)
        {
            array.appendItem(pdf::PDFObject::createReal(number));
        }
        return pdf::PDFObject::createArray(std::move(array));
    }

    static pdf::PDFObject createIntegerArray(const std::vector<int>& numbers)
    {
        pdf::PDFArrayBuilder array;
        for (const int number : numbers)
        {
            array.appendItem(pdf::PDFObject::createInteger(number));
        }
        return pdf::PDFObject::createArray(std::move(array));
    }

    /// Samples of a smooth picture: the components are ramps over the image
    static QByteArray createRamp(int size, int components, int bytesPerComponent)
    {
        QByteArray samples;
        samples.reserve(size * size * components * bytesPerComponent);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                for (int component = 0; component < components; ++component)
                {
                    const int value = component % 2 == 0 ? (x * 255) / (size - 1) : (y * 255) / (size - 1);
                    for (int byte = 0; byte < bytesPerComponent; ++byte)
                    {
                        samples.append(char(value));
                    }
                }
            }
        }
        return samples;
    }

    /// The largest difference of the components of two images of one size composed over white
    static int getColorDifference(const QImage& first, const QImage& second)
    {
        const QImage a = first.convertToFormat(QImage::Format_ARGB32);
        const QImage b = second.convertToFormat(QImage::Format_ARGB32);
        int difference = 0;
        for (int y = 0; y < a.height(); ++y)
        {
            for (int x = 0; x < a.width(); ++x)
            {
                const QRgb pa = a.pixel(x, y);
                const QRgb pb = b.pixel(x, y);
                const auto over = [](int component, int alpha) { return (component * alpha + 255 * (255 - alpha)) / 255; };
                difference = qMax(difference, qAbs(over(qRed(pa), qAlpha(pa)) - over(qRed(pb), qAlpha(pb))));
                difference = qMax(difference, qAbs(over(qGreen(pa), qAlpha(pa)) - over(qGreen(pb), qAlpha(pb))));
                difference = qMax(difference, qAbs(over(qBlue(pa), qAlpha(pa)) - over(qBlue(pb), qAlpha(pb))));
            }
        }
        return difference;
    }

    /// The largest difference of the alpha of two images of one size
    static int getAlphaDifference(const QImage& first, const QImage& second)
    {
        const QImage a = first.convertToFormat(QImage::Format_ARGB32);
        const QImage b = second.convertToFormat(QImage::Format_ARGB32);
        int difference = 0;
        for (int y = 0; y < a.height(); ++y)
        {
            for (int x = 0; x < a.width(); ++x)
            {
                difference = qMax(difference, qAbs(qAlpha(a.pixel(x, y)) - qAlpha(b.pixel(x, y))));
            }
        }
        return difference;
    }

    /// Peak of the memory of the process in bytes (the commit charge on Windows, the
    /// resident set on Linux), 0 when it cannot be read
    static qint64 getPeakMemory()
    {
#if defined(Q_OS_WIN)
        PROCESS_MEMORY_COUNTERS_EX counters = { };
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        {
            return qint64(counters.PeakPagefileUsage);
        }
        return 0;
#else
        QFile file(QStringLiteral("/proc/self/status"));
        if (file.open(QIODevice::ReadOnly))
        {
            for (const QByteArray& line : file.readAll().split('\n'))
            {
                if (line.startsWith("VmHWM:"))
                {
                    return line.mid(6).trimmed().split(' ').front().toLongLong() * 1024;
                }
            }
        }
        return 0;
#endif
    }
};

/// The core of the tool Compress of phase 2 (plan of the first batch, wave A) and the
/// streaming and cancellation of the optimizers below it (chapter 11, F1)
class DocumentToolCompressTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // First: the peak of the memory of the process only grows
    void memoryBound();

    void presetSettings_data();
    void presetSettings();
    void presetsRun_data();
    void presetsRun();
    void presetDescriptions();
    void jpegQualityShownIsUsed_data();
    void jpegQualityShownIsUsed();
    void jpegQualityOfOldCallers();
    void previewIsTheResult();
    void sizeBeforeOfModifiedDocument();
    void notReduced();
    void breakdownOfObjects();
    void breakdownRows();
    void progressOverPhases();
    void canceledInEachPhase_data();
    void canceledInEachPhase();
    void canceledBeforeStart();
    void imageOptimizerCancellation();
    void optimizerCancellationAndCounters();
    void sanitizerCancellation();
    void textFlowCancellation();
    void visitImagesOneAtATime();
    void collectImagesSameAsPlacements();
    void undecodableImages();
    void recompressOfFilterChains();
    void colorTransformOfOldEncodingDropped();
    void recompressOfFilterChainsByTool();
    void imageDecodedOnAnotherPage();
    void imageWithoutPixels();
    void imageKindsKeepTheirLook_data();
    void imageKindsKeepTheirLook();
    void sparseTransparencyKept();
    void hostileDocuments_data();
    void hostileDocuments();
    void formDrawingItselfSkipped();
    void formsNestedTooDeeplySkipped();
    void formsWithinTheLimitDrawn();
    void formGuardLeavesNothingAfterAnError();
    void documentWithoutPages();
    void hostileParameters();
    void signedInput();
    void wholeWriteWithoutRevisions();
    void encryptedInput();
    void encryptedWithoutPermission();
    void eightThreadsOneInstance();
};

void DocumentToolCompressTest::initTestCase()
{
    // The texts of the frames: numbers and sizes of a locale with a decimal point
    QLocale::setDefault(QLocale::c());
}

void DocumentToolCompressTest::memoryBound()
{
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    QSKIP("The allocators of the sanitizers keep the freed memory, the peak says nothing");
#endif

    // 24 images, which take little of the file: decoded each has 1500 x 1500 x 4 bytes
    constexpr int IMAGE_COUNT = 24;
    constexpr int IMAGE_SIZE = 1500;
    const qint64 decodedImageBytes = qint64(IMAGE_SIZE) * IMAGE_SIZE * 4;

    pdf::PDFDocument document;
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < IMAGE_COUNT; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            CompressTestHelper::setPage(builder, page, "q 200 0 0 200 0 0 cm /Im Do Q", { { "Im", CompressTestHelper::addImage(builder, CompressTestHelper::createFlatImage(IMAGE_SIZE, quint32(i))) } });
        }
        document = builder.build();
    }

    const qint64 baseline = CompressTestHelper::getPeakMemory();
    if (baseline <= 0)
    {
        QSKIP("The peak of the memory cannot be read on this system");
    }

    // The run of the tool: at most one decoded image with its raw samples at a time.
    // The limit is a few images, far below all of them at once.
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document), CompressTestHelper::preset(pdf::PDFDocumentToolCompress::PRESET_BALANCED), nullptr, nullptr);
    QVERIFY(result.status == pdf::PDFDocumentToolResultStatus::Success || result.status == pdf::PDFDocumentToolResultStatus::NothingToDo);
    const qint64 runPeak = CompressTestHelper::getPeakMemory();

    // The control: all images decoded at once - what the optimizer did before the streaming
    const std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(int(infos.size()), IMAGE_COUNT);
    const qint64 holdingPeak = CompressTestHelper::getPeakMemory();

    const qint64 runGrowth = runPeak - baseline;
    const qint64 holdingGrowth = holdingPeak - baseline;
    qInfo().noquote() << QStringLiteral("Peak of the memory above the document: the tool %1 MB, all images decoded at once %2 MB (%3 images of %4 MB)")
                             .arg(runGrowth / (1024 * 1024)).arg(holdingGrowth / (1024 * 1024)).arg(IMAGE_COUNT).arg(decodedImageBytes / (1024 * 1024));

    QVERIFY2(runGrowth < decodedImageBytes * 6, qPrintable(QStringLiteral("The tool holds %1 bytes at once").arg(runGrowth)));
    QVERIFY2(holdingGrowth > decodedImageBytes * IMAGE_COUNT / 2, "The detector does not see the images held at once");
}

void DocumentToolCompressTest::presetSettings_data()
{
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<bool>("imagesEnabled");
    QTest::addColumn<int>("targetDpi");
    QTest::addColumn<int>("jpegQuality");
    QTest::addColumn<int>("goal");
    QTest::addColumn<bool>("keepOriginal");
    QTest::addColumn<bool>("preserveTransparency");
    QTest::addColumn<bool>("structure");

    using Goal = pdf::PDFImageOptimizer::OptimizationGoal;

    // Owner answer Q2 (chapter 12 of the plan), Figma 294:1340 of Custom
    QTest::newRow("smaller-file") << CompressTestHelper::preset("smaller-file") << true << 110 << 60 << int(Goal::MinimumSize) << true << true << true;
    QTest::newRow("balanced") << CompressTestHelper::preset("balanced") << true << 200 << 80 << int(Goal::PreferQuality) << true << true << true;
    QTest::newRow("custom-defaults") << CompressTestHelper::preset("custom") << true << 150 << 75 << int(Goal::MinimumSize) << true << true << true;

    QVariantMap custom = CompressTestHelper::preset("custom");
    custom.insert(QStringLiteral("targetDpi"), 72);
    custom.insert(QStringLiteral("jpegQuality"), 30);
    custom.insert(QStringLiteral("goal"), QStringLiteral("prefer-quality"));
    custom.insert(QStringLiteral("keepOriginalIfLarger"), false);
    custom.insert(QStringLiteral("preserveTransparency"), false);
    custom.insert(QStringLiteral("cleanStructure"), false);
    QTest::newRow("custom-values") << custom << true << 72 << 30 << int(Goal::PreferQuality) << false << false << false;

    // The other parameters belong to Custom only
    QVariantMap balanced = custom;
    balanced.insert(QStringLiteral("preset"), QStringLiteral("balanced"));
    QTest::newRow("balanced-ignores-custom") << balanced << true << 200 << 80 << int(Goal::PreferQuality) << true << true << true;
}

void DocumentToolCompressTest::presetSettings()
{
    QFETCH(QVariantMap, parameters);
    QFETCH(bool, imagesEnabled);
    QFETCH(int, targetDpi);
    QFETCH(int, jpegQuality);
    QFETCH(int, goal);
    QFETCH(bool, keepOriginal);
    QFETCH(bool, preserveTransparency);
    QFETCH(bool, structure);

    pdf::PDFDocumentToolCompress tool;
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), parameters, &normalized).empty());

    const pdf::PDFDocumentToolCompress::Settings settings = pdf::PDFDocumentToolCompress::createSettings(normalized);
    QCOMPARE(settings.images.enabled, imagesEnabled);
    QCOMPARE(settings.images.colorProfile.targetDpi, targetDpi);
    QCOMPARE(settings.images.grayProfile.targetDpi, targetDpi);
    QCOMPARE(settings.images.colorProfile.jpegQuality, jpegQuality);
    QCOMPARE(settings.images.grayProfile.jpegQuality, jpegQuality);
    QCOMPARE(int(settings.images.goal), goal);
    QCOMPARE(settings.images.keepOriginalIfLarger, keepOriginal);
    QCOMPARE(settings.images.preserveTransparency, preserveTransparency);
    QCOMPARE(settings.structure == pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::All), structure);
    QCOMPARE(settings.structure == pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::None), !structure);
}

void DocumentToolCompressTest::presetsRun_data()
{
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<int>("expectedWidth");
    QTest::addColumn<int>("recompressed");

    // An image of 256 pixels drawn at 288 dpi. A downsampled image gets the resolution of
    // the preset: 256 * dpi / 288.
    QTest::newRow("smaller-file") << CompressTestHelper::preset("smaller-file") << 98 << 1;
    QTest::newRow("balanced") << CompressTestHelper::preset("balanced") << 178 << 1;
    QTest::newRow("best-quality") << CompressTestHelper::preset("best-quality") << 256 << 0;

    QVariantMap custom = CompressTestHelper::preset("custom");
    custom.insert(QStringLiteral("targetDpi"), 150);
    QTest::newRow("custom") << custom << 133 << 1;
}

void DocumentToolCompressTest::presetsRun()
{
    QFETCH(QVariantMap, parameters);
    QFETCH(int, expectedWidth);
    QFETCH(int, recompressed);

    pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(1, 256);

    // A file of an older version and with objects, which nobody uses (so also
    // Best quality has something to remove)
    {
        pdf::PDFDocumentBuilder builder(&input);
        builder.addObject(CompressTestHelper::createStreamObject(QByteArray(200000, 'x')));
        input = CompressTestHelper::withVersion(builder.build(), pdf::PDFVersion(1, 6));
    }

    const QByteArray file = CompressTestHelper::write(input);
    QVERIFY(file.startsWith("%PDF-1.6"));

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, file), parameters, nullptr, nullptr);

    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.documents.size(), size_t(1));

    const pdf::PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, pdf::PDFDocumentToolOutputDocument::Kind::Changed);
    QCOMPARE(output.inputIndex, 0);
    QCOMPARE(output.suggestedFileName, QStringLiteral("Project_Report_compressed.pdf")); // 312:7410
    QVERIFY(output.flags.testFlag(pdf::PDFModifiedDocument::Reset));

    // The images of the result have the resolution of the preset
    const std::vector<int> widths = CompressTestHelper::getImageWidths(*output.document);
    QCOMPARE(widths.size(), size_t(1));
    QCOMPARE(widths.front(), expectedWidth);

    // The input is not changed
    QCOMPARE(CompressTestHelper::getImageWidths(input), std::vector<int>{ 256 });

    // The output keeps the version of the input (never the 2.0 of the optimizers) and it is written so
    QCOMPARE(output.document->getInfo()->version.major, 1);
    QCOMPARE(output.document->getInfo()->version.minor, 6);
    const QByteArray written = CompressTestHelper::write(*output.document);
    QVERIFY(written.startsWith("%PDF-1.6"));

    // The sizes are those of the real files
    QCOMPARE(result.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));
    QCOMPARE(result.data.value(QStringLiteral("sizeAfter")).toInteger(), qint64(written.size()));
    QVERIFY(written.size() < file.size());
    QCOMPARE(result.data.value(QStringLiteral("savedBytes")).toInteger(), qint64(file.size() - written.size()));
    QCOMPARE(result.data.value(QStringLiteral("savedPercent")).toInt(), qRound(double(file.size() - written.size()) * 100.0 / double(file.size())));

    // The result can be read and has its page
    const pdf::PDFDocument reopened = CompressTestHelper::read(written);
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(1));

    // Facts of the dialog 296:2761 ("Size", "Images", "Time")
    QCOMPARE(result.facts.size(), size_t(3));
    QCOMPARE(result.facts[0].id, QStringLiteral("size"));
    QCOMPARE(result.facts[0].title, QStringLiteral("Size"));
    QVERIFY2(QRegularExpression(QStringLiteral("^[\\d.]+ [kKM]?i?B → [\\d.]+ [kKM]?i?B \\(\\d+ % smaller\\)$")).match(result.facts[0].value).hasMatch(), qPrintable(result.facts[0].value));
    QCOMPARE(result.facts[1].id, QStringLiteral("images"));
    QCOMPARE(result.facts[1].title, QStringLiteral("Images"));
    QCOMPARE(result.facts[1].value, QStringLiteral("%1 recompressed · %2 kept").arg(recompressed).arg(1 - recompressed));
    QCOMPARE(result.facts[2].id, QStringLiteral("time"));
    QCOMPARE(result.facts[2].title, QStringLiteral("Time"));
    QVERIFY2(QRegularExpression(QStringLiteral("^(\\d+ min )?\\d+ s$")).match(result.facts[2].value).hasMatch(), qPrintable(result.facts[2].value));

    // The line of the card Result of 291:994
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d+ % smaller · [\\d.]+ [kKM]?i?B saved$")).match(pdf::PDFDocumentToolCompress::getSavingText(result)).hasMatch(), qPrintable(pdf::PDFDocumentToolCompress::getSavingText(result)));
}

void DocumentToolCompressTest::presetDescriptions()
{
    // The texts of Figma 291:994, 295:2182 and 294:1340
    QCOMPARE(pdf::PDFDocumentToolCompress::getPresetDescription(CompressTestHelper::preset("balanced")),
             QStringLiteral("Balanced: images are reduced to 200 dpi where no difference is visible, unused objects are removed, identical objects merged and streams recompressed."));
    QCOMPARE(pdf::PDFDocumentToolCompress::getPresetDescription(CompressTestHelper::preset("best-quality")),
             QStringLiteral("Best quality: images are kept as they are; only the lossless structure clean-up runs."));

    pdf::PDFDocumentToolCompress tool;
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), CompressTestHelper::preset("custom"), &normalized).empty());
    QCOMPARE(pdf::PDFDocumentToolCompress::getPresetDescription(normalized), QStringLiteral("Custom: images at 150 dpi, JPEG quality 75, minimum size preferred. Structure clean-up is on."));

    normalized.insert(QStringLiteral("targetDpi"), 96);
    normalized.insert(QStringLiteral("jpegQuality"), 40);
    normalized.insert(QStringLiteral("goal"), QStringLiteral("prefer-quality"));
    normalized.insert(QStringLiteral("cleanStructure"), false);
    QCOMPARE(pdf::PDFDocumentToolCompress::getPresetDescription(normalized), QStringLiteral("Custom: images at 96 dpi, JPEG quality 40, quality preferred. Structure clean-up is off."));

    // The parameters: the default preset of 291:994, the offered resolutions cover the
    // default of Custom, Custom says its parameters
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QVERIFY(pdf::PDFDocumentToolParameters::isValid(definitions));
    QCOMPARE(definitions.front().getName(), QStringLiteral("preset"));
    QCOMPARE(definitions.front().getDefaultValue().toString(), QStringLiteral("balanced"));
    QStringList presetTitles;
    for (const pdf::PDFDocumentToolParameterOption& option : definitions.front().getOptions())
    {
        presetTitles << option.title;
    }
    QCOMPARE(presetTitles, QStringList({ QStringLiteral("Smaller file"), QStringLiteral("Balanced"), QStringLiteral("Best quality"), QStringLiteral("Custom…") }));
    QVERIFY(pdf::PDFDocumentToolCompress::getOfferedResolutions() == std::vector<int>({ 72, 96, 110, 150, 200, 300 }));
}

void DocumentToolCompressTest::jpegQualityShownIsUsed_data()
{
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<int>("quality");
    QTest::addColumn<int>("shiftedQuality");
    QTest::addColumn<int>("width");

    // An image of 256 pixels drawn at 288 dpi, downsampled to 256 * dpi / 288. The second
    // quality is what the goal of the optimizer would make of it (-10 / +5): it must differ.
    QTest::newRow("smaller-file") << CompressTestHelper::preset("smaller-file") << 60 << 50 << 98;
    QTest::newRow("balanced") << CompressTestHelper::preset("balanced") << 80 << 85 << 178;

    QVariantMap customDefault = CompressTestHelper::preset("custom");
    QTest::newRow("custom-default") << customDefault << 75 << 65 << 133;

    QVariantMap custom = CompressTestHelper::preset("custom");
    custom.insert(QStringLiteral("jpegQuality"), 42);
    custom.insert(QStringLiteral("goal"), QStringLiteral("prefer-quality"));
    custom.insert(QStringLiteral("targetDpi"), 150);
    QTest::newRow("custom-values") << custom << 42 << 47 << 133;
}

void DocumentToolCompressTest::jpegQualityShownIsUsed()
{
    QFETCH(QVariantMap, parameters);
    QFETCH(int, quality);
    QFETCH(int, shiftedQuality);
    QFETCH(int, width);

    // Owner answer Q2 and the decision JPEG of chapter 12: the stream of the image is the
    // same image encoded at exactly the quality the preset shows
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference image = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(256, 31));
    CompressTestHelper::setPage(builder, page, CompressTestHelper::createImageContent(256), { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const pdf::PDFStream* written = CompressTestHelper::getStream(*result.documents.front().document, image);
    QVERIFY(written);
    pdf::PDFDocumentDataLoaderDecorator loader(result.documents.front().document.data());
    QCOMPARE(loader.readNameFromDictionary(written->getDictionary(), "Filter"), QByteArray("DCTDecode"));
    QCOMPARE(int(loader.readIntegerFromDictionary(written->getDictionary(), "Width", 0)), width);

    const pdf::PDFImageCompressor::ImageStatisticsList decoded = pdf::PDFImageCompressor().collectImages(&input);
    QCOMPARE(decoded.size(), size_t(1));

    const auto encode = [&](int encodedQuality)
    {
        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::JPEG;
        options.colorMode = pdf::PDFImage::ImageColorMode::Color;
        options.targetSize = QSize(width, width);
        options.jpegQuality = encodedQuality;
        options.alphaHandling = pdf::PDFImage::AlphaHandling::FlattenToWhite;
        const pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(decoded.front().image, options);
        return *stream.getContent();
    };

    QCOMPARE(*written->getContent(), encode(quality));
    QVERIFY(*written->getContent() != encode(shiftedQuality));
}

void DocumentToolCompressTest::jpegQualityOfOldCallers()
{
    // The old applications and pdftool optimize: the goal still shifts the quality
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference image = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(256, 32));
    CompressTestHelper::setPage(builder, page, CompressTestHelper::createImageContent(256), { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    QVERIFY(!settings.exactJpegQuality);
    settings.enabled = true;
    settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize;
    settings.colorProfile.targetDpi = 150;
    settings.colorProfile.jpegQuality = 60;
    pdf::PDFImageOptimizer optimizer;
    const pdf::PDFDocument optimized = optimizer.optimize(&input, settings);

    const pdf::PDFImageCompressor::ImageStatisticsList decoded = pdf::PDFImageCompressor().collectImages(&input);
    QCOMPARE(decoded.size(), size_t(1));

    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = pdf::PDFImage::ImageCompression::JPEG;
    options.colorMode = pdf::PDFImage::ImageColorMode::Color;
    options.targetSize = QSize(133, 133);
    options.jpegQuality = 50;
    options.alphaHandling = pdf::PDFImage::AlphaHandling::FlattenToWhite;
    const pdf::PDFStream expected = pdf::PDFImage::createStreamFromImage(decoded.front().image, options);
    QCOMPARE(*CompressTestHelper::getStream(optimized, image)->getContent(), *expected.getContent());
}

void DocumentToolCompressTest::previewIsTheResult()
{
    // Measuring is running (plan chapter 3): the preview has the document of the result
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(1, 256);
    pdf::PDFDocumentToolCompress tool;
    QVERIFY(tool.hasPreview());
    QVERIFY(tool.isPreviewFinal());

    const pdf::PDFDocumentToolResult preview = tool.runPreview(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr);
    QCOMPARE(preview.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(preview.documents.size(), size_t(1));
    QCOMPARE(CompressTestHelper::getImageWidths(*preview.documents.front().document), std::vector<int>{ 178 });

    const pdf::PDFDocumentToolResult run = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(run.data.value(QStringLiteral("sizeAfter")).toInteger(), preview.data.value(QStringLiteral("sizeAfter")).toInteger());
}

void DocumentToolCompressTest::sizeBeforeOfModifiedDocument()
{
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(1, 256);
    const QByteArray file = CompressTestHelper::write(input);
    const QByteArray smallerFile(1000, 'x');

    // A document without its file, and one changed since the file was read, have no
    // file of their size: the size is what would be written
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult withoutFile = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(withoutFile.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));

    const pdf::PDFDocumentToolResult modified = tool.run(CompressTestHelper::createContext(input, smallerFile, QStringLiteral("a.pdf"), true), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(modified.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));

    // The bytes of an unchanged file are its size
    const pdf::PDFDocumentToolResult unmodified = tool.run(CompressTestHelper::createContext(input, smallerFile, QStringLiteral("a.pdf"), false), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(unmodified.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(smallerFile.size()));

    // A caller, which does not say that the bytes are of this document, gets the measured size
    pdf::PDFDocumentToolInputDocument unknown;
    unknown.document = pdf::PDFDocumentPointer(new pdf::PDFDocument(input));
    unknown.originalData = smallerFile;
    const pdf::PDFDocumentToolResult notSaid = tool.run(pdf::PDFDocumentToolContext::create(unknown), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(notSaid.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));
}

void DocumentToolCompressTest::notReduced()
{
    // A file compressed already: nothing more to save with Best quality (295:2182)
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult first = tool.run(CompressTestHelper::createContext(CompressTestHelper::createPhotoDocument(2, 256)), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(first.status, pdf::PDFDocumentToolResultStatus::Success);

    const QByteArray file = CompressTestHelper::write(*first.documents.front().document);
    const pdf::PDFDocument compressed = CompressTestHelper::read(file);
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(compressed, file), CompressTestHelper::preset("best-quality"), nullptr, nullptr);

    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QCOMPARE(result.message, QStringLiteral("File cannot be reduced with these settings"));
    QVERIFY(result.documents.empty());
    QVERIFY(result.isUse());

    // The numbers of the panel of 295:2182 are there, without a saving
    QCOMPARE(result.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));
    QCOMPARE(result.data.value(QStringLiteral("savedBytes")).toInteger(), qint64(0));
    QCOMPARE(result.data.value(QStringLiteral("savedPercent")).toInt(), 0);
    QCOMPARE(pdf::PDFDocumentToolCompress::getSavingText(result), QStringLiteral("No saving with this preset"));
    QCOMPARE(pdf::PDFDocumentToolCompress::getChangeRows(result).size(), size_t(4));
    QCOMPARE(result.facts.front().id, QStringLiteral("size"));
    QVERIFY(!result.facts.front().value.contains(QStringLiteral("smaller")));
}

void DocumentToolCompressTest::breakdownOfObjects()
{
    // Each row with objects of a known size: an image and its soft mask drawn from a
    // form with inherited pages, a font with its program, a page content
    constexpr qint64 FONT_PROGRAM_SIZE = 50000;
    constexpr qint64 PAGE_CONTENT_SIZE = 30000;
    constexpr qint64 FORM_CONTENT_SIZE = 20000;

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));

    QImage photo = CompressTestHelper::createPhoto(100, 7);
    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = pdf::PDFImage::ImageCompression::Flate;
    options.enablePngPredictor = false;
    const pdf::PDFStream maskStream = pdf::PDFImage::createStreamFromImage(photo.convertToFormat(QImage::Format_Grayscale8), options);
    const pdf::PDFObjectReference mask = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(maskStream)));

    const pdf::PDFStream imageStream = pdf::PDFImage::createStreamFromImage(photo, options);
    pdf::PDFDictionaryBuilder imageDictionary(*imageStream.getDictionary());
    imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(mask));
    const pdf::PDFObjectReference image = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(imageDictionary), QByteArray(*imageStream.getContent()))));
    const qint64 imageBytes = imageStream.getContent()->size() + maskStream.getContent()->size();

    // The form draws the image: it is not in the resources of the page
    pdf::PDFDictionaryBuilder formXObjects;
    formXObjects.addEntry(pdf::PDFInplaceOrMemoryString("Im"), pdf::PDFObject::createReference(image));
    pdf::PDFDictionaryBuilder formResources;
    formResources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(formXObjects)));
    pdf::PDFDictionaryBuilder formDictionary;
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(formResources)));
    QByteArray formContent("q 100 0 0 100 0 0 cm /Im Do Q\n");
    formContent.append(QByteArray(FORM_CONTENT_SIZE - formContent.size(), ' '));
    const pdf::PDFObjectReference form = builder.addObject(CompressTestHelper::createStreamObject(formContent, std::move(formDictionary)));

    // The font: dictionary -> descriptor -> program
    const pdf::PDFObjectReference program = builder.addObject(CompressTestHelper::createStreamObject(QByteArray(FONT_PROGRAM_SIZE, 'f')));
    pdf::PDFDictionaryBuilder descriptor;
    descriptor.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("FontDescriptor"));
    descriptor.addEntry(pdf::PDFInplaceOrMemoryString("FontFile2"), pdf::PDFObject::createReference(program));
    const pdf::PDFObjectReference descriptorReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(descriptor)));
    pdf::PDFDictionaryBuilder font;
    font.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("TrueType"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("FontDescriptor"), pdf::PDFObject::createReference(descriptorReference));
    const pdf::PDFObjectReference fontReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(font)));
    pdf::PDFDictionaryBuilder pageFont;
    pageFont.addEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createReference(fontReference));
    builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageFont)));

    QByteArray content("q 200 0 0 200 0 0 cm /Fm Do Q\n");
    content.append(QByteArray(PAGE_CONTENT_SIZE - content.size(), ' '));
    CompressTestHelper::setPage(builder, page, content, { { "Fm", form } });

    const pdf::PDFDocument document = builder.build();
    const qint64 fileSize = pdf::PDFDocumentWriter::getDocumentFileSize(&document);

    const pdf::PDFDocumentSizeBreakdown breakdown = pdf::PDFDocumentSizeBreakdown::create(&document, fileSize);

    // An object has its stream and a few hundred bytes of its dictionary
    constexpr qint64 OVERHEAD = 1024;
    QVERIFY2(breakdown.images >= imageBytes && breakdown.images < imageBytes + OVERHEAD, qPrintable(QString::number(breakdown.images)));
    QVERIFY2(breakdown.fonts >= FONT_PROGRAM_SIZE && breakdown.fonts < FONT_PROGRAM_SIZE + OVERHEAD, qPrintable(QString::number(breakdown.fonts)));
    QVERIFY2(breakdown.pageContent >= PAGE_CONTENT_SIZE + FORM_CONTENT_SIZE && breakdown.pageContent < PAGE_CONTENT_SIZE + FORM_CONTENT_SIZE + OVERHEAD, qPrintable(QString::number(breakdown.pageContent)));

    // One image, its mask is no image of the row (Images (412) = recompressed + kept)
    QCOMPARE(breakdown.imageCount, qint64(1));

    // The rows give the size of the file (plan chapter 3)
    QCOMPARE(breakdown.getTotal(), fileSize);
    QVERIFY(breakdown.structure > 0);

    // The file with old revisions is larger than its objects: the rest is Structure
    const pdf::PDFDocumentSizeBreakdown withRevisions = pdf::PDFDocumentSizeBreakdown::create(&document, fileSize + 100000);
    QCOMPARE(withRevisions.getTotal(), fileSize + 100000);
    QCOMPARE(withRevisions.structure, breakdown.structure + 100000);

    // A file smaller than the written objects (they are in compressed object streams):
    // no row is negative
    QCOMPARE(pdf::PDFDocumentSizeBreakdown::create(&document, 1).structure, qint64(0));

    // A file size unknown: Structure is the size of its objects
    const pdf::PDFDocumentSizeBreakdown withoutFile = pdf::PDFDocumentSizeBreakdown::create(&document, 0);
    QVERIFY(withoutFile.structure > 0 && withoutFile.structure <= breakdown.structure);

    // JSON round trip of the data of a result
    const pdf::PDFDocumentSizeBreakdown fromJson = pdf::PDFDocumentSizeBreakdown::fromJson(breakdown.toJson());
    QCOMPARE(fromJson.images, breakdown.images);
    QCOMPARE(fromJson.fonts, breakdown.fonts);
    QCOMPARE(fromJson.pageContent, breakdown.pageContent);
    QCOMPARE(fromJson.structure, breakdown.structure);
    QCOMPARE(fromJson.imageCount, breakdown.imageCount);

    QCOMPARE(pdf::PDFDocumentSizeBreakdown::create(nullptr, 100).getTotal(), qint64(0));
}

void DocumentToolCompressTest::breakdownRows()
{
    // The rows of What changes of 291:994
    pdf::PDFDocumentSizeBreakdown before;
    before.images = 22334668;
    before.fonts = 1992295;
    before.pageContent = 1258291;
    before.structure = 419430;
    before.imageCount = 412;

    pdf::PDFDocumentSizeBreakdown after;
    after.images = 3565158;
    after.fonts = 1992295;
    after.pageContent = 629145;
    after.structure = 209715;
    after.imageCount = 412;

    const std::vector<pdf::PDFDocumentToolFact> rows = pdf::PDFDocumentSizeBreakdown::createChangeRows(before, after);
    QCOMPARE(rows.size(), size_t(4));
    QCOMPARE(rows[0].id, QStringLiteral("images"));
    QCOMPARE(rows[0].title, QStringLiteral("Images (412)"));
    QCOMPARE(rows[0].value, pdf::PDFDocumentToolFormat::formatSizeChange(before.images, after.images));
    QCOMPARE(rows[1].title, QStringLiteral("Fonts"));
    QCOMPARE(rows[2].title, QStringLiteral("Page content"));
    QCOMPARE(rows[3].title, QStringLiteral("Structure"));

    // The values of the frame
    QCOMPARE(pdf::PDFDocumentToolFormat::formatSize(26004684), QStringLiteral("24.8 MB"));
    QCOMPARE(pdf::PDFDocumentToolFormat::formatSizeChange(26004684, 6396313), QStringLiteral("24.8 MB → 6.1 MB"));
    QCOMPARE(pdf::PDFDocumentToolFormat::formatDuration(72000), QStringLiteral("1 min 12 s"));
    QCOMPARE(pdf::PDFDocumentToolFormat::formatDuration(40000), QStringLiteral("40 s"));
    QCOMPARE(pdf::PDFDocumentToolFormat::formatDuration(10), QStringLiteral("1 s"));
    QCOMPARE(pdf::PDFDocumentToolFormat::getSavedPercent(26004684, 6396313), 75);
    QCOMPARE(pdf::PDFDocumentToolFormat::getSavedPercent(1000, 334), 67); // 66.6 %, whole percents are rounded
    QCOMPARE(pdf::PDFDocumentToolFormat::getSavedPercent(100, 100), 0);
    QCOMPARE(pdf::PDFDocumentToolFormat::getSavedPercent(100, 200), 0);
    QCOMPARE(pdf::PDFDocumentToolFormat::getSavedPercent(0, 0), 0);
}

void DocumentToolCompressTest::progressOverPhases()
{
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(4, 200);

    CompressTestListener listener;
    pdf::PDFDocumentToolProgressAdapter adapter(&listener);
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr, &adapter);
    QVERIFY(result.status == pdf::PDFDocumentToolResultStatus::Success || result.status == pdf::PDFDocumentToolResultStatus::NothingToDo);

    // One growing bar up to 100 %
    QVERIFY(!listener.percentages.isEmpty());
    for (int i = 1; i < listener.percentages.size(); ++i)
    {
        QVERIFY2(listener.percentages[i] >= listener.percentages[i - 1], qPrintable(QStringLiteral("%1 after %2").arg(listener.percentages[i]).arg(listener.percentages[i - 1])));
    }
    QCOMPARE(listener.percentages.back(), 100);

    // The steps of 295:1711 in their order: "Recompressing images · 152 of 412"
    QVERIFY2(listener.texts.contains(QStringLiteral("Recompressing images · 2 of 4")), qPrintable(listener.texts.join(QStringLiteral(" | "))));
    QVERIFY(listener.texts.contains(QStringLiteral("Recompressing images · 4 of 4")));
    const qsizetype images = listener.texts.indexOf(QStringLiteral("Recompressing images · 2 of 4"));
    const qsizetype analysis = listener.texts.indexOf(QStringLiteral("Looking for images"));
    const qsizetype structure = listener.texts.indexOf(QStringLiteral("Cleaning up the structure"));
    const qsizetype measure = listener.texts.indexOf(QStringLiteral("Measuring the result"));
    QVERIFY(analysis >= 0 && analysis < images);
    QVERIFY(images < structure);
    QVERIFY(structure < measure);

    // Without images no phase of the images
    CompressTestListener bestListener;
    pdf::PDFDocumentToolProgressAdapter bestAdapter(&bestListener);
    tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("best-quality"), nullptr, &bestAdapter);
    QVERIFY(!bestListener.texts.contains(QStringLiteral("Looking for images")));
    QVERIFY(bestListener.texts.contains(QStringLiteral("Cleaning up the structure")));
    QCOMPARE(bestListener.percentages.back(), 100);
}

void DocumentToolCompressTest::canceledInEachPhase_data()
{
    QTest::addColumn<QString>("cancelOn");
    QTest::addColumn<QStringList>("notReached");

    QTest::newRow("analysis") << QStringLiteral("Looking for images") << QStringList({ QStringLiteral("Recompressing images · 1 of"), QStringLiteral("Cleaning up"), QStringLiteral("Measuring") });
    QTest::newRow("images") << QStringLiteral("Recompressing images · 1 of") << QStringList({ QStringLiteral("Recompressing images · 3 of"), QStringLiteral("Cleaning up"), QStringLiteral("Measuring") });
    QTest::newRow("structure") << QStringLiteral("Cleaning up") << QStringList({ QStringLiteral("Measuring") });
    QTest::newRow("measure") << QStringLiteral("Measuring") << QStringList();
}

void DocumentToolCompressTest::canceledInEachPhase()
{
    QFETCH(QString, cancelOn);
    QFETCH(QStringList, notReached);

    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(6, 200);
    const std::vector<int> widths = CompressTestHelper::getImageWidths(input);

    CompressTestControl control;
    CompressTestListener listener;
    listener.control = &control;
    listener.cancelOnText = cancelOn;
    pdf::PDFDocumentToolProgressAdapter adapter(&listener);
    QSignalSpy started(&adapter, &pdf::PDFProgress::progressStarted);
    QSignalSpy finished(&adapter, &pdf::PDFProgress::progressFinished);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), &control, &adapter);

    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);

    // A listener of the start gets its end also from a stopped run, the bar is not filled
    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 1);
    QVERIFY(listener.percentages.isEmpty() || listener.percentages.back() < 100);
    QVERIFY(result.documents.empty());
    QVERIFY(result.facts.empty());
    QCOMPARE(CompressTestHelper::getImageWidths(input), widths);

    // The later phases have not run
    for (const QString& text : listener.texts)
    {
        for (const QString& later : notReached)
        {
            QVERIFY2(!text.startsWith(later), qPrintable(QStringLiteral("%1 was reached: %2").arg(later, listener.texts.join(QStringLiteral(" | ")))));
        }
    }
}

void DocumentToolCompressTest::canceledBeforeStart()
{
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(1, 100);
    CompressTestControl control;
    control.cancel();

    pdf::PDFDocumentToolCompress tool;
    QCOMPARE(tool.run(CompressTestHelper::createContext(input), QVariantMap(), &control, nullptr).status, pdf::PDFDocumentToolResultStatus::Stopped);
    QCOMPARE(tool.runPreview(CompressTestHelper::createContext(input), QVariantMap(), &control).status, pdf::PDFDocumentToolResultStatus::Stopped);
}

void DocumentToolCompressTest::imageOptimizerCancellation()
{
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(6, 200);
    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.colorProfile.targetDpi = 72;
    settings.grayProfile.targetDpi = 72;

    // Canceled while the images are encoded: the next images are not decoded, the
    // returned document is the source
    CompressTestControl control;
    pdf::PDFProgress progress(nullptr);
    int steps = 0;
    QObject context;
    connect(&progress, &pdf::PDFProgress::progressStep, &context, [&]() { ++steps; control.cancel(); }, Qt::DirectConnection);

    pdf::PDFImageOptimizer optimizer;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    results.emplace_back();
    const pdf::PDFDocument canceled = optimizer.optimize(&input, settings, pdf::PDFImageOptimizer::ImageOverrides(), &progress, nullptr, &results, &control);
    QCOMPARE(steps, 1);
    QVERIFY(results.empty());
    QCOMPARE(CompressTestHelper::getImageWidths(canceled), CompressTestHelper::getImageWidths(input));

    // Canceled in the walk over the pages: not a single image encoded
    CompressTestControl analysisControl;
    pdf::PDFProgress analysis(nullptr);
    pdf::PDFProgress images(nullptr);
    int analysisSteps = 0;
    int imageSteps = 0;
    connect(&analysis, &pdf::PDFProgress::progressStep, &context, [&]() { ++analysisSteps; analysisControl.cancel(); }, Qt::DirectConnection);
    connect(&images, &pdf::PDFProgress::progressStep, &context, [&]() { ++imageSteps; }, Qt::DirectConnection);
    const pdf::PDFDocument canceledAnalysis = optimizer.optimize(&input, settings, pdf::PDFImageOptimizer::ImageOverrides(), &images, nullptr, &results, &analysisControl, &analysis);
    QCOMPARE(analysisSteps, 1);
    QCOMPARE(imageSteps, 0);
    QVERIFY(results.empty());
    QCOMPARE(CompressTestHelper::getImageWidths(canceledAnalysis), CompressTestHelper::getImageWidths(input));

    // Without the cancellation: all images, a step per image and per page, the same
    // result as the old call without the new arguments
    pdf::PDFProgress fullAnalysis(nullptr);
    pdf::PDFProgress fullImages(nullptr);
    int fullAnalysisSteps = 0;
    int fullImageSteps = 0;
    connect(&fullAnalysis, &pdf::PDFProgress::progressStep, &context, [&]() { ++fullAnalysisSteps; }, Qt::DirectConnection);
    connect(&fullImages, &pdf::PDFProgress::progressStep, &context, [&]() { ++fullImageSteps; }, Qt::DirectConnection);
    const pdf::PDFDocument full = optimizer.optimize(&input, settings, pdf::PDFImageOptimizer::ImageOverrides(), &fullImages, nullptr, &results, nullptr, &fullAnalysis);
    QCOMPARE(fullAnalysisSteps, 6);
    QCOMPARE(fullImageSteps, 6);
    QCOMPARE(results.size(), size_t(6));
    for (size_t i = 1; i < results.size(); ++i)
    {
        QVERIFY(results[i - 1].reference < results[i].reference);
    }
    const pdf::PDFDocument old = optimizer.optimize(&input, settings);
    QCOMPARE(CompressTestHelper::write(old), CompressTestHelper::write(full));
}

void DocumentToolCompressTest::optimizerCancellationAndCounters()
{
    // A document with one of each thing, which a step of 303:4897 handles
    pdf::PDFDocument base;
    pdf::PDFDocument rich;
    pdf::PDFObjectReference page;
    {
        pdf::PDFDocumentBuilder builder;
        page = builder.appendPage(QRectF(0, 0, 200, 200));
        base = builder.build();
    }
    {
        pdf::PDFDocumentBuilder builder(&base);

        // Unused objects
        builder.addObject(pdf::PDFObject::createString("unused 1"));
        builder.addObject(pdf::PDFObject::createString("unused 2"));
        builder.addObject(pdf::PDFObject::createDictionary(pdf::PDFDictionaryBuilder()));

        // A simple object behind a reference
        const pdf::PDFObjectReference simple = builder.addObject(pdf::PDFObject::createInteger(5));

        // A null entry (adding an object removes them, so it is set)
        pdf::PDFDictionaryBuilder withNull;
        withNull.addEntry(pdf::PDFInplaceOrMemoryString("Kept"), pdf::PDFObject::createInteger(1));
        withNull.addEntry(pdf::PDFInplaceOrMemoryString("Empty"), pdf::PDFObject());
        const pdf::PDFObjectReference nullHolder = builder.addObject(pdf::PDFObject::createInteger(0));
        builder.setObject(nullHolder, pdf::PDFObject::createDictionary(std::move(withNull)));

        // Identical dictionaries, big enough not to be the ones of the page tree
        const auto createDuplicate = []()
        {
            pdf::PDFDictionaryBuilder duplicate;
            duplicate.addEntry(pdf::PDFInplaceOrMemoryString("Name"), pdf::PDFObject::createString("the same dictionary"));
            return pdf::PDFObject::createDictionary(std::move(duplicate));
        };
        const pdf::PDFObjectReference duplicate1 = builder.addObject(createDuplicate());
        const pdf::PDFObjectReference duplicate2 = builder.addObject(createDuplicate());

        // A Flate stream without compression (zlib level 0)
        const QByteArray plain(20000, 'a');
        QByteArray stored = qCompress(plain, 0).mid(4);
        pdf::PDFDictionaryBuilder flate;
        flate.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
        const pdf::PDFObjectReference stream = builder.addObject(CompressTestHelper::createStreamObject(stored, std::move(flate)));

        pdf::PDFDictionaryBuilder update;
        update.addEntry(pdf::PDFInplaceOrMemoryString("TestSimple"), pdf::PDFObject::createReference(simple));
        update.addEntry(pdf::PDFInplaceOrMemoryString("TestNull"), pdf::PDFObject::createReference(nullHolder));
        update.addEntry(pdf::PDFInplaceOrMemoryString("TestDuplicate1"), pdf::PDFObject::createReference(duplicate1));
        update.addEntry(pdf::PDFInplaceOrMemoryString("TestDuplicate2"), pdf::PDFObject::createReference(duplicate2));
        update.addEntry(pdf::PDFInplaceOrMemoryString("TestStream"), pdf::PDFObject::createReference(stream));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        rich = builder.build();
    }

    pdf::PDFOptimizer baseOptimizer(pdf::PDFOptimizer::All, nullptr);
    baseOptimizer.setDocument(&base);
    baseOptimizer.optimize();
    const pdf::PDFOptimizer::Statistics baseStatistics = baseOptimizer.getStatistics();

    pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::All, nullptr);
    optimizer.setDocument(&rich);
    optimizer.optimize();
    QVERIFY(!optimizer.isCanceled());

    const pdf::PDFOptimizer::Statistics statistics = optimizer.getStatistics();
    QCOMPARE(statistics.simpleObjectsDereferenced - baseStatistics.simpleObjectsDereferenced, qint64(1));
    QCOMPARE(statistics.nullEntriesRemoved - baseStatistics.nullEntriesRemoved, qint64(1));
    QVERIFY2(statistics.unusedObjectsRemoved - baseStatistics.unusedObjectsRemoved >= 3, qPrintable(QString::number(statistics.unusedObjectsRemoved)));
    QCOMPARE(statistics.identicalObjectsMerged - baseStatistics.identicalObjectsMerged, qint64(1));
    QVERIFY(statistics.objectEntriesShrunk > 0);
    QCOMPARE(statistics.streamsRecompressed - baseStatistics.streamsRecompressed, qint64(1));
    QVERIFY(statistics.recompressedBytesSaved > 19000);

    // A step not selected counts nothing (303:4897 "Skipped – not selected")
    pdf::PDFOptimizer onlyUnused(pdf::PDFOptimizer::RemoveUnusedObjects, nullptr);
    onlyUnused.setDocument(&rich);
    onlyUnused.optimize();
    QVERIFY(onlyUnused.getStatistics().unusedObjectsRemoved >= 3);
    QCOMPARE(onlyUnused.getStatistics().simpleObjectsDereferenced, qint64(0));
    QCOMPARE(onlyUnused.getStatistics().streamsRecompressed, qint64(0));

    // The counters are of the last run
    onlyUnused.optimize();
    QCOMPARE(onlyUnused.getStatistics().unusedObjectsRemoved, qint64(0));

    // Progress: a step per stage with something to run
    pdf::PDFProgress progress(nullptr);
    QSignalSpy stepSpy(&progress, &pdf::PDFProgress::progressStep);
    QSignalSpy startSpy(&progress, &pdf::PDFProgress::progressStarted);
    QSignalSpy finishSpy(&progress, &pdf::PDFProgress::progressFinished);
    pdf::PDFOptimizer withProgress(pdf::PDFOptimizer::All, nullptr);
    withProgress.setProgress(&progress);
    withProgress.setDocument(&rich);
    withProgress.optimize();
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(finishSpy.count(), 1);
    QCOMPARE(stepSpy.count(), 5);
    QCOMPARE(stepSpy.last().first().toInt(), 100);

    // Canceled by the first progress text of the first stage: the later stages run
    // nothing, the signals stay balanced
    CompressTestControl control;
    pdf::PDFOptimizer canceled(pdf::PDFOptimizer::All, nullptr);
    canceled.setOperationControl(&control);
    canceled.setDocument(&rich);
    QSignalSpy finishedSpy(&canceled, &pdf::PDFOptimizer::optimizationFinished);
    QSignalSpy textSpy(&canceled, &pdf::PDFOptimizer::optimizationProgress);
    QObject context;
    connect(&canceled, &pdf::PDFOptimizer::optimizationProgress, &context, [&control]() { control.cancel(); }, Qt::DirectConnection);
    canceled.optimize();
    QVERIFY(canceled.isCanceled());
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(textSpy.count(), 1); // "Stage 1", nothing of its passes or of the later stages
    QCOMPARE(canceled.getStatistics().simpleObjectsDereferenced, qint64(0));
    QCOMPARE(canceled.getStatistics().unusedObjectsRemoved, qint64(0));
    QCOMPARE(canceled.getStatistics().streamsRecompressed, qint64(0));
    QCOMPARE(canceled.getStorage().getObjects().size(), rich.getStorage().getObjects().size());
}

void DocumentToolCompressTest::sanitizerCancellation()
{
    pdf::PDFDocument input;
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 200, 200));
        builder.setDocumentTitle(QStringLiteral("Secret title"));

        // An object nobody uses: the final optimization removes it
        builder.addObject(pdf::PDFObject::createString("unused object"));

        pdf::PDFDictionaryBuilder outline;
        outline.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Outlines"));
        const pdf::PDFObjectReference outlineReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(outline)));
        pdf::PDFDictionaryBuilder catalog;
        catalog.addEntry(pdf::PDFInplaceOrMemoryString("Outlines"), pdf::PDFObject::createReference(outlineReference));
        builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalog)));
        input = builder.build();
    }

    const auto hasOutline = [](const pdf::PDFObjectStorage& storage)
    {
        pdf::PDFDocument document{ pdf::PDFObjectStorage(storage), pdf::PDFVersion(2, 0), QByteArray() };
        const pdf::PDFDictionary* catalog = document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"));
        return catalog && !document.getObject(catalog->get("Outlines")).isNull();
    };
    const auto hasUnusedObject = [](const pdf::PDFObjectStorage& storage)
    {
        for (const pdf::PDFObjectStorage::Entry& entry : storage.getObjects())
        {
            if (entry.object.isString() && entry.object.getString() == "unused object")
            {
                return true;
            }
        }
        return false;
    };
    QVERIFY(hasOutline(input.getStorage()));
    QVERIFY(hasUnusedObject(input.getStorage()));

    // Without the cancellation both steps and the final optimization run
    pdf::PDFDocumentSanitizer whole(pdf::PDFDocumentSanitizer::None, nullptr);
    whole.setFlags(pdf::PDFDocumentSanitizer::SanitizationFlags(pdf::PDFDocumentSanitizer::DocumentInfo) | pdf::PDFDocumentSanitizer::Outline);
    whole.setDocument(&input);
    whole.sanitize();
    QVERIFY(!hasOutline(whole.getStorage()));
    QVERIFY(!hasUnusedObject(whole.getStorage()));

    // Canceled after the document info: the outline and the optimization do not run
    CompressTestControl control;
    pdf::PDFDocumentSanitizer sanitizer(pdf::PDFDocumentSanitizer::None, nullptr);
    sanitizer.setFlags(pdf::PDFDocumentSanitizer::SanitizationFlags(pdf::PDFDocumentSanitizer::DocumentInfo) | pdf::PDFDocumentSanitizer::Outline);
    sanitizer.setOperationControl(&control);
    sanitizer.setDocument(&input);
    QSignalSpy finished(&sanitizer, &pdf::PDFDocumentSanitizer::sanitizationFinished);
    QObject context;
    connect(&sanitizer, &pdf::PDFDocumentSanitizer::sanitizationProgress, &context, [&control]() { control.cancel(); }, Qt::DirectConnection);
    sanitizer.sanitize();
    QVERIFY(sanitizer.isCanceled());
    QCOMPARE(finished.count(), 1);
    QVERIFY(hasOutline(sanitizer.getStorage()));
    QVERIFY(hasUnusedObject(sanitizer.getStorage()));
}

void DocumentToolCompressTest::textFlowCancellation()
{
    pdf::PDFDocument input;
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 200, 200));
        builder.appendPage(QRectF(0, 0, 200, 200));
        input = builder.build();
    }

    for (const pdf::PDFDocumentTextFlowFactory::Algorithm algorithm : { pdf::PDFDocumentTextFlowFactory::Algorithm::Layout, pdf::PDFDocumentTextFlowFactory::Algorithm::Content })
    {
        // The pages of the flow
        pdf::PDFDocumentTextFlowFactory factory;
        QVERIFY(!factory.create(&input, algorithm).isEmpty());

        // A canceled factory processes no page: the layout has no pages at all
        CompressTestControl control;
        control.cancel();
        pdf::PDFDocumentTextFlowFactory canceledFactory;
        canceledFactory.setOperationControl(&control);
        const pdf::PDFDocumentTextFlow flow = canceledFactory.create(&input, algorithm);
        if (algorithm == pdf::PDFDocumentTextFlowFactory::Algorithm::Layout)
        {
            QVERIFY(flow.isEmpty());
        }
        QVERIFY(control.getCheckCount() >= 2);
    }
}

void DocumentToolCompressTest::visitImagesOneAtATime()
{
    // Four pages: image A on the pages 1, 3 and 4 (largest on page 3), image B inside
    // of a form on page 2. Resolutions from the spec: pixels / size in inches.
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference imageA = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(240, 1));
    const pdf::PDFObjectReference imageB = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(144, 2));

    const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page1, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", imageA } });

    pdf::PDFDictionaryBuilder formXObjects;
    formXObjects.addEntry(pdf::PDFInplaceOrMemoryString("Im"), pdf::PDFObject::createReference(imageB));
    pdf::PDFDictionaryBuilder formResources;
    formResources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(formXObjects)));
    pdf::PDFDictionaryBuilder formDictionary;
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(formResources)));
    formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BBox"), pdf::PDFObject::createArray([]() { pdf::PDFArrayBuilder array; array.appendItem(pdf::PDFObject::createInteger(0)); array.appendItem(pdf::PDFObject::createInteger(0)); array.appendItem(pdf::PDFObject::createInteger(200)); array.appendItem(pdf::PDFObject::createInteger(200)); return array; }()));
    const pdf::PDFObjectReference form = builder.addObject(CompressTestHelper::createStreamObject("q 36 0 0 72 0 0 cm /Im Do Q", std::move(formDictionary)));

    const pdf::PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page2, "q 1 0 0 1 10 10 cm /Fm Do Q", { { "Fm", form } });

    const pdf::PDFObjectReference page3 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page3, "q 144 0 0 144 0 0 cm /Im Do Q", { { "Im", imageA } });

    const pdf::PDFObjectReference page4 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page4, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", imageA } });

    const pdf::PDFDocument document = builder.build();

    pdf::PDFImageCompressor compressor;
    pdf::PDFImageCompressor::Environment environment(&document);
    const pdf::PDFImageCompressor::ImagePlacements placements = compressor.collectImagePlacements(environment);

    // Sorted by the references, the first pages and the largest sizes
    QCOMPARE(placements.size(), size_t(2));
    QCOMPARE(placements[0].reference, imageA);
    QCOMPARE(placements[0].firstPageIndex, pdf::PDFInteger(0));
    QVERIFY(qFuzzyCompare(placements[0].maximumInches.x(), 2.0));
    QVERIFY(qFuzzyCompare(placements[0].maximumInches.y(), 2.0));
    QCOMPARE(placements[1].reference, imageB);
    QCOMPARE(placements[1].firstPageIndex, pdf::PDFInteger(1));
    QVERIFY(qFuzzyCompare(placements[1].maximumInches.x(), 0.5));
    QVERIFY(qFuzzyCompare(placements[1].maximumInches.y(), 1.0));

    // Each image once, in the order of the pages, nobody else holds its pixels
    std::vector<pdf::PDFImageCompressor::ImageStatistics> visited;
    bool allDetached = true;
    const pdf::PDFImageCompressor::DecodedImageCallback callback = [&](pdf::PDFImageCompressor::ImageStatistics statistics)
    {
        allDetached = allDetached && statistics.image.isDetached();
        visited.push_back(std::move(statistics));
    };
    compressor.visitImages(environment, placements, callback);

    QCOMPARE(visited.size(), size_t(2));
    QVERIFY(allDetached);
    QCOMPARE(visited[0].reference, imageA);
    QCOMPARE(visited[0].image.width(), 240);
    QVERIFY(qFuzzyCompare(visited[0].minimalDpi.x(), 120.0)); // 240 pixels / 2 inches
    QVERIFY(qFuzzyCompare(visited[0].minimalDpi.y(), 120.0));
    QCOMPARE(visited[1].reference, imageB);
    QVERIFY(qFuzzyCompare(visited[1].minimalDpi.x(), 288.0)); // 144 pixels / 0.5 inch
    QVERIFY(qFuzzyCompare(visited[1].minimalDpi.y(), 144.0)); // 144 pixels / 1 inch

    // The filter says which images are decoded: nothing else reaches the callback of a page
    int filteredCount = 0;
    const pdf::PDFImageCompressor::ImageCallback countImage = [&filteredCount](const pdf::PDFImage&, const pdf::PDFStream*, pdf::PDFObjectReference, QPointF) { ++filteredCount; };
    const pdf::PDFImageCompressor::ImageFilter refuseImage = [](pdf::PDFObjectReference) { return false; };
    const pdf::PDFImageCompressor::ImageFilter acceptImage = [imageA](pdf::PDFObjectReference reference) { return reference == imageA; };
    environment.processPage(0, countImage, nullptr, refuseImage);
    QCOMPARE(filteredCount, 0);
    environment.processPage(1, countImage, nullptr, acceptImage); // Draws the image B only
    QCOMPARE(filteredCount, 0);
    environment.processPage(0, countImage, nullptr, acceptImage);
    QCOMPARE(filteredCount, 1);
    environment.processPage(0, countImage, nullptr);
    QCOMPARE(filteredCount, 2);

    // A canceled visit stops before the next image: of the next page
    CompressTestControl control;
    int count = 0;
    const pdf::PDFImageCompressor::DecodedImageCallback cancelingCallback = [&](pdf::PDFImageCompressor::ImageStatistics)
    {
        ++count;
        control.cancel();
    };
    compressor.visitImages(environment, placements, cancelingCallback, &control);
    QCOMPARE(count, 1);

    // And of the same page
    pdf::PDFDocumentBuilder samePageBuilder;
    const pdf::PDFObjectReference samePage = samePageBuilder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(samePageBuilder, samePage, "q 72 0 0 72 0 0 cm /Im1 Do /Im2 Do Q",
                                { { "Im1", CompressTestHelper::addImage(samePageBuilder, CompressTestHelper::createPhoto(32, 3)) },
                                  { "Im2", CompressTestHelper::addImage(samePageBuilder, CompressTestHelper::createPhoto(32, 4)) } });
    const pdf::PDFDocument samePageDocument = samePageBuilder.build();
    pdf::PDFImageCompressor::Environment samePageEnvironment(&samePageDocument);
    const pdf::PDFImageCompressor::ImagePlacements samePagePlacements = compressor.collectImagePlacements(samePageEnvironment);
    QCOMPARE(samePagePlacements.size(), size_t(2));

    CompressTestControl samePageControl;
    int samePageCount = 0;
    const pdf::PDFImageCompressor::DecodedImageCallback samePageCallback = [&](pdf::PDFImageCompressor::ImageStatistics)
    {
        ++samePageCount;
        samePageControl.cancel();
    };
    compressor.visitImages(samePageEnvironment, samePagePlacements, samePageCallback, &samePageControl);
    QCOMPARE(samePageCount, 1);

    // A canceled walk finds nothing more
    CompressTestControl walkControl;
    walkControl.cancel();
    QVERIFY(compressor.collectImagePlacements(environment, &walkControl).empty());
}

void DocumentToolCompressTest::collectImagesSameAsPlacements()
{
    // The list of all images: the order of the references, the resolutions of the placements
    const pdf::PDFDocument document = CompressTestHelper::createPhotoDocument(3, 144);

    pdf::PDFImageCompressor compressor;
    pdf::PDFProgress progress(nullptr);
    QSignalSpy startSpy(&progress, &pdf::PDFProgress::progressStarted);
    const pdf::PDFImageCompressor::ImageStatisticsList images = compressor.collectImages(&document, nullptr, &progress);
    QCOMPARE(images.size(), size_t(3));
    QCOMPARE(startSpy.count(), 2); // The pages, then the images
    for (size_t i = 0; i < images.size(); ++i)
    {
        QCOMPARE(images[i].image.width(), 144);
        QVERIFY(qFuzzyCompare(images[i].minimalDpi.x(), 288.0));
        if (i > 0)
        {
            QVERIFY(images[i - 1].reference < images[i].reference);
        }
    }

    CompressTestControl control;
    control.cancel();
    QVERIFY(compressor.collectImages(&document, &control).empty());
    QVERIFY(compressor.collectImages(nullptr).empty());

    // The pages draw the images against the order of their references: the lists of the old
    // callers (the dialog of the images) and the results of the optimizer stay in the order
    // of the references, the new objects of the masks are numbered by them
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference first = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(64, 41));
    const pdf::PDFObjectReference second = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(64, 42));
    QVERIFY(first < second);
    const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page1, CompressTestHelper::createImageContent(64), { { "Im", second } });
    const pdf::PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page2, CompressTestHelper::createImageContent(64), { { "Im", first } });
    const pdf::PDFDocument reversed = builder.build();

    const pdf::PDFImageCompressor::ImageStatisticsList reversedImages = compressor.collectImages(&reversed);
    QCOMPARE(reversedImages.size(), size_t(2));
    QCOMPARE(reversedImages[0].reference, first);
    QCOMPARE(reversedImages[1].reference, second);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFImageOptimizer optimizer;
    optimizer.optimize(&reversed, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);
    QCOMPARE(results.size(), size_t(2));
    QCOMPARE(results[0].reference, first);
    QCOMPARE(results[1].reference, second);
}

void DocumentToolCompressTest::colorTransformOfOldEncodingDropped()
{
    // Skia of Chrome writes "/ColorTransform 0" next to the filter of an image, PDFImage honors
    // it there. It says how the old samples are stored: kept for a new JPEG, which is in YCbCr,
    // its Y, Cb, Cr are drawn as R, G, B - a green image became purple (found by
    // "Finite element method - Wikipedia.pdf" of UnitTestsDocumentToolCorpus)
    constexpr int SIZE = 256;
    const QImage photo = CompressTestHelper::createPhoto(SIZE, 7).convertToFormat(QImage::Format_RGB888);
    QByteArray samples;
    samples.reserve(SIZE * SIZE * 3);
    for (int y = 0; y < SIZE; ++y)
    {
        samples.append(reinterpret_cast<const char*>(photo.constScanLine(y)), SIZE * 3);
    }

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceRGB"), samples, { { "ColorTransform", pdf::PDFObject::createInteger(0) } });
    CompressTestHelper::setPage(builder, page, CompressTestHelper::createImageContent(SIZE), { { "Im", image } });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document, CompressTestHelper::write(document)), CompressTestHelper::preset(pdf::PDFDocumentToolCompress::PRESET_SMALLER_FILE), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    const pdf::PDFDocument* output = result.documents.front().document.data();

    const pdf::PDFObject& imageObject = output->getObjectByReference(image);
    QVERIFY(imageObject.isStream());
    QVERIFY2(pdf::PDFImageOptimizer::readFilterName(output, imageObject.getStream()->getDictionary()) == QStringLiteral("DCTDecode"), "The image is encoded as a new JPEG");
    QVERIFY(!imageObject.getStream()->getDictionary()->hasKey("ColorTransform"));

    // The image lies over 64 x 64 points of the page: the same colors within the JPEG artifacts
    const CompressTestHelper::DrawnPage before = CompressTestHelper::draw(document);
    const CompressTestHelper::DrawnPage after = CompressTestHelper::draw(*output);
    for (const QPoint& point : { QPoint(10, 10), QPoint(30, 10), QPoint(50, 30), QPoint(10, 50), QPoint(55, 55) })
    {
        const QRgb first = before.at(point.x(), point.y());
        const QRgb second = after.at(point.x(), point.y());
        const int difference = qMax(qAbs(qRed(first) - qRed(second)), qMax(qAbs(qGreen(first) - qGreen(second)), qAbs(qBlue(first) - qBlue(second))));
        QVERIFY2(difference <= 32, qPrintable(QStringLiteral("At %1, %2: %3 became %4").arg(point.x()).arg(point.y()).arg(QColor(first).name(), QColor(second).name())));
    }
}

void DocumentToolCompressTest::recompressOfFilterChains()
{
    // ISO 32000-2, 7.4.1: the filters of the array decode the stored data in the order of the
    // array, so only a first FlateDecode stores deflated data. A second one lies below another
    // filter: its stream is not touched (found by BIBLE21.pdf of the corpus: the whole file failed)
    const QByteArray text = QByteArray("BT /F1 12 Tf (recompress) Tj ET\n").repeated(200);
    const QByteArray deflated = pdf::PDFFlateDecodeFilter::compress(text);

    const auto createFilters = [](const QList<QByteArray>& filters)
    {
        pdf::PDFArrayBuilder array;
        for (const QByteArray& filter : filters)
        {
            array.appendItem(pdf::PDFObject::createName(filter));
        }
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(std::move(array)));
        return dictionary;
    };

    // qCompress of the level 0 stores the bytes: a zlib stream, which can get much smaller
    const QByteArray storedHex = qCompress(text.toHex(), 0).mid(4);
    const QByteArray garbage("this is not a deflated stream, not even its header");

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference flateBelowHex = builder.addObject(CompressTestHelper::createStreamObject(deflated.toHex(), createFilters({ "ASCIIHexDecode", "FlateDecode" })));
    const pdf::PDFObjectReference flateAboveHex = builder.addObject(CompressTestHelper::createStreamObject(storedHex, createFilters({ "FlateDecode", "ASCIIHexDecode" })));
    const pdf::PDFObjectReference damaged = builder.addObject(CompressTestHelper::createStreamObject(garbage, createFilters({ "FlateDecode" })));
    const pdf::PDFDocument document = builder.build();

    pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RecompressFlateStreams, nullptr);
    optimizer.setStorage(document.getStorage());
    optimizer.optimize();
    const pdf::PDFObjectStorage storage = optimizer.takeStorage();

    const pdf::PDFStream* belowHex = storage.getObjectByReference(flateBelowHex).getStream();
    QCOMPARE(*belowHex->getContent(), deflated.toHex());
    QCOMPARE(storage.getDecodedStream(belowHex), text);

    const pdf::PDFStream* aboveHex = storage.getObjectByReference(flateAboveHex).getStream();
    QVERIFY2(aboveHex->getContent()->size() < storedHex.size() / 2, "The first FlateDecode of a chain is recompressed");
    QCOMPARE(storage.getDecodedStream(aboveHex), text);

    QCOMPARE(*storage.getObjectByReference(damaged).getStream()->getContent(), garbage);
    QCOMPARE(optimizer.getStatistics().streamsRecompressed, qint64(1));
}

void DocumentToolCompressTest::recompressOfFilterChainsByTool()
{
    // A page drawn by a stream of [/ASCIIHexDecode /FlateDecode]: the tool does not fail on
    // it and the page keeps its content
    const QByteArray content = QByteArray("q 1 0 0 rg 10 10 80 80 re f Q\n").repeated(100);
    pdf::PDFArrayBuilder filters;
    filters.appendItem(pdf::PDFObject::createName("ASCIIHexDecode"));
    filters.appendItem(pdf::PDFObject::createName("FlateDecode"));
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(std::move(filters)));

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference contentReference = builder.addObject(CompressTestHelper::createStreamObject(pdf::PDFFlateDecodeFilter::compress(content).toHex(), std::move(dictionary)));
    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
    builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    const pdf::PDFDocument document = builder.build();
    const QByteArray written = CompressTestHelper::write(document);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document, written), CompressTestHelper::preset(pdf::PDFDocumentToolCompress::PRESET_BEST_QUALITY), nullptr, nullptr);
    QVERIFY2(result.status != pdf::PDFDocumentToolResultStatus::Failed, qPrintable(result.message));

    if (!result.documents.empty())
    {
        const pdf::PDFDocument* output = result.documents.front().document.data();
        QCOMPARE(CompressTestHelper::draw(*output).at(50, 50), qRgb(255, 0, 0));
    }
}

void DocumentToolCompressTest::undecodableImages()
{
    // A garbage image on the first page and a good one on the second: the good one is
    // recompressed, the garbage stays as it was
    const QByteArray garbage("this is not a JPEG image at all, not at all, nothing like it");

    pdf::PDFDocumentBuilder builder;
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(100));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(100));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceRGB"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("DCTDecode"));
    const pdf::PDFObjectReference garbageImage = builder.addObject(CompressTestHelper::createStreamObject(garbage, std::move(dictionary)));

    const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page1, "q 200 0 0 200 0 0 cm /Im Do Q", { { "Im", garbageImage } });
    const pdf::PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference goodImage = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(256, 3));
    CompressTestHelper::setPage(builder, page2, CompressTestHelper::createImageContent(256), { { "Im", goodImage } });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.colorProfile.targetDpi = 100;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFImageOptimizer optimizer;
    const pdf::PDFDocument optimized = optimizer.optimize(&document, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);

    const pdf::PDFObject garbageObject = optimized.getObjectByReference(garbageImage);
    QVERIFY(garbageObject.isStream());
    QCOMPARE(*garbageObject.getStream()->getContent(), garbage);

    bool goodRecompressed = false;
    for (const pdf::PDFImageOptimizer::ImageResult& result : results)
    {
        if (result.reference == goodImage)
        {
            goodRecompressed = !result.keptOriginal;
        }
        else
        {
            QVERIFY(result.keptOriginal);
        }
    }
    QVERIFY(goodRecompressed);

    // The tool ends normally with such a document
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document), CompressTestHelper::preset("smaller-file"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);
    QCOMPARE(result.data.value(QStringLiteral("imagesKept")).toInt(), 1);
}

void DocumentToolCompressTest::imageDecodedOnAnotherPage()
{
    // The color space of the image has a name of the resources (ISO 32000-2 8.9.5.1 allows
    // a name defined in the ColorSpace subdictionary), which only the second page defines:
    // the image cannot be read on its first page and gets its pixels on the second
    pdf::PDFDocumentBuilder builder;

    const QImage gray = CompressTestHelper::createPhoto(64, 21).convertToFormat(QImage::Format_Grayscale8);
    QByteArray samples;
    for (int y = 0; y < gray.height(); ++y)
    {
        samples.append(reinterpret_cast<const char*>(gray.constScanLine(y)), gray.width());
    }

    pdf::PDFDictionaryBuilder imageDictionary;
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(64));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(64));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("CS0"));
    const pdf::PDFObjectReference image = builder.addObject(CompressTestHelper::createStreamObject(samples, std::move(imageDictionary)));

    const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page1, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });

    // A page without the image between them is skipped, the last one draws it
    builder.appendPage(QRectF(0, 0, 200, 200));

    const pdf::PDFObjectReference page3 = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference contents = builder.addObject(CompressTestHelper::createStreamObject("q 36 0 0 36 0 0 cm /Im Do Q"));
    pdf::PDFDictionaryBuilder xobjects;
    xobjects.addEntry(pdf::PDFInplaceOrMemoryString("Im"), pdf::PDFObject::createReference(image));
    pdf::PDFDictionaryBuilder colorSpaces;
    colorSpaces.addEntry(pdf::PDFInplaceOrMemoryString("CS0"), pdf::PDFObject::createName("DeviceGray"));
    pdf::PDFDictionaryBuilder resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xobjects)));
    resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createDictionary(std::move(colorSpaces)));
    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contents));
    builder.mergeTo(page3, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    const pdf::PDFDocument document = builder.build();

    pdf::PDFImageCompressor compressor;
    pdf::PDFImageCompressor::Environment environment(&document);
    const pdf::PDFImageCompressor::ImagePlacements placements = compressor.collectImagePlacements(environment);
    QCOMPARE(placements.size(), size_t(1));
    QCOMPARE(placements.front().firstPageIndex, pdf::PDFInteger(0));

    std::vector<pdf::PDFImageCompressor::ImageStatistics> visited;
    const pdf::PDFImageCompressor::DecodedImageCallback callback = [&visited](pdf::PDFImageCompressor::ImageStatistics statistics)
    {
        visited.push_back(std::move(statistics));
    };
    compressor.visitImages(environment, placements, callback);

    QCOMPARE(visited.size(), size_t(1));
    QCOMPARE(visited.front().reference, image);
    QCOMPARE(visited.front().image.width(), 64);

    // Its largest placement is the one of the first page: 64 pixels / 1 inch
    QVERIFY(qFuzzyCompare(visited.front().minimalDpi.x(), 64.0));
}

void DocumentToolCompressTest::imageWithoutPixels()
{
    // The samples can be read, the conversion cannot (ISO 32000-2 Table 87: Decode has two
    // numbers per component): the image is reported without pixels and it is kept
    pdf::PDFDocumentBuilder builder;
    pdf::PDFArrayBuilder decode;
    decode.appendItem(pdf::PDFObject::createInteger(0));
    decode.appendItem(pdf::PDFObject::createInteger(1));
    pdf::PDFDictionaryBuilder imageDictionary;
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(8));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(8));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceRGB"));
    imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Decode"), pdf::PDFObject::createArray(std::move(decode)));
    const QByteArray samples(8 * 8 * 3, 'a');
    const pdf::PDFObjectReference image = builder.addObject(CompressTestHelper::createStreamObject(samples, std::move(imageDictionary)));

    const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page1, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page2, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFImageCompressor compressor;
    pdf::PDFImageCompressor::Environment environment(&document);
    std::vector<pdf::PDFImageCompressor::ImageStatistics> visited;
    const pdf::PDFImageCompressor::DecodedImageCallback callback = [&visited](pdf::PDFImageCompressor::ImageStatistics statistics)
    {
        visited.push_back(std::move(statistics));
    };
    compressor.visitImages(environment, compressor.collectImagePlacements(environment), callback);

    QCOMPARE(visited.size(), size_t(1));
    QCOMPARE(visited.front().reference, image);
    QVERIFY(visited.front().image.isNull());

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFImageOptimizer optimizer;
    const pdf::PDFDocument optimized = optimizer.optimize(&document, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(results.front().keptOriginal);
    QCOMPARE(*optimized.getObjectByReference(image).getStream()->getContent(), samples);
}

void DocumentToolCompressTest::imageKindsKeepTheirLook_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<bool>("isRecompressed");
    QTest::addColumn<int>("colorTolerance");

    // The JPEG of the highest quality differs by the rounding only; its subsampled colors
    // bleed over the hard edge of a stencil mask
    QTest::newRow("decode-inverted") << QStringLiteral("decode-inverted") << true << 16;
    QTest::newRow("soft-mask") << QStringLiteral("soft-mask") << true << 16;
    QTest::newRow("soft-mask-decode") << QStringLiteral("soft-mask-decode") << true << 16;
    QTest::newRow("soft-mask-matte") << QStringLiteral("soft-mask-matte") << true << 16;
    QTest::newRow("sixteen-bit") << QStringLiteral("sixteen-bit") << true << 16;
    QTest::newRow("cmyk") << QStringLiteral("cmyk") << true << 16;
    QTest::newRow("lab") << QStringLiteral("lab") << true << 16;
    QTest::newRow("indexed") << QStringLiteral("indexed") << true << 16;
    QTest::newRow("color-key") << QStringLiteral("color-key") << true << 32;
    QTest::newRow("stencil-mask") << QStringLiteral("stencil-mask") << true << 32;
    QTest::newRow("image-mask") << QStringLiteral("image-mask") << false << 0;
}

void DocumentToolCompressTest::imageKindsKeepTheirLook()
{
    QFETCH(QString, kind);
    QFETCH(bool, isRecompressed);
    QFETCH(int, colorTolerance);

    // ISO 32000-2 8.9.5, 8.9.6, 11.6.5.3: whatever the original image has in its dictionary
    // (Decode, SMask with its Decode and Matte, Mask of both kinds, more bits, other color
    // spaces), the image of the result looks the same: the same colors over white, the same
    // transparency. The image is written again always, at a high quality, without a downsampling.
    constexpr int SIZE = 64;
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));

    const pdf::PDFObject deviceRGB = pdf::PDFObject::createName("DeviceRGB");
    const pdf::PDFObject deviceGray = pdf::PDFObject::createName("DeviceGray");
    const QByteArray rgb = CompressTestHelper::createRamp(SIZE, 3, 1);
    const QByteArray gray = CompressTestHelper::createRamp(SIZE, 1, 1);

    pdf::PDFObjectReference image;
    if (kind == QLatin1String("decode-inverted"))
    {
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, deviceGray, gray, { { "Decode", CompressTestHelper::createNumberArray({ 1, 0 }) } });
    }
    else if (kind == QLatin1String("soft-mask") || kind == QLatin1String("soft-mask-decode") || kind == QLatin1String("soft-mask-matte"))
    {
        std::vector<std::pair<QByteArray, pdf::PDFObject>> maskEntries;
        if (kind == QLatin1String("soft-mask-decode"))
        {
            maskEntries.emplace_back("Decode", CompressTestHelper::createNumberArray({ 1, 0 }));
        }
        if (kind == QLatin1String("soft-mask-matte"))
        {
            maskEntries.emplace_back("Matte", CompressTestHelper::createNumberArray({ 0.5, 0.5, 0.5 }));
        }
        const pdf::PDFObjectReference mask = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, deviceGray, gray, std::move(maskEntries));
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, deviceRGB, rgb, { { "SMask", pdf::PDFObject::createReference(mask) } });
    }
    else if (kind == QLatin1String("sixteen-bit"))
    {
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 16, deviceRGB, CompressTestHelper::createRamp(SIZE, 3, 2));
    }
    else if (kind == QLatin1String("cmyk"))
    {
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceCMYK"), CompressTestHelper::createRamp(SIZE, 4, 1));
    }
    else if (kind == QLatin1String("lab"))
    {
        pdf::PDFDictionaryBuilder lab;
        lab.addEntry(pdf::PDFInplaceOrMemoryString("WhitePoint"), CompressTestHelper::createNumberArray({ 0.9642, 1.0, 0.8249 }));
        lab.addEntry(pdf::PDFInplaceOrMemoryString("Range"), CompressTestHelper::createNumberArray({ -100, 100, -100, 100 }));
        pdf::PDFArrayBuilder colorSpace;
        colorSpace.appendItem(pdf::PDFObject::createName("Lab"));
        colorSpace.appendItem(pdf::PDFObject::createDictionary(std::move(lab)));
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createArray(std::move(colorSpace)), rgb);
    }
    else if (kind == QLatin1String("indexed"))
    {
        QByteArray palette;
        for (int i = 0; i < 256; ++i)
        {
            palette.append(char(i));
            palette.append(char(255 - i));
            palette.append('\x80');
        }
        pdf::PDFArrayBuilder colorSpace;
        colorSpace.appendItem(pdf::PDFObject::createName("Indexed"));
        colorSpace.appendItem(pdf::PDFObject::createName("DeviceRGB"));
        colorSpace.appendItem(pdf::PDFObject::createInteger(255));
        colorSpace.appendItem(pdf::PDFObject::createString(palette));
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createArray(std::move(colorSpace)), gray);
    }
    else if (kind == QLatin1String("color-key"))
    {
        image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, deviceRGB, rgb, { { "Mask", CompressTestHelper::createIntegerArray({ 0, 127, 0, 127, 0, 255 }) } });
    }
    else if (kind == QLatin1String("stencil-mask") || kind == QLatin1String("image-mask"))
    {
        QByteArray bits;
        for (int y = 0; y < SIZE; ++y)
        {
            for (int x = 0; x < SIZE / 8; ++x)
            {
                bits.append(y < SIZE / 2 ? '\x00' : '\xFF');
            }
        }
        const std::vector<std::pair<QByteArray, pdf::PDFObject>> maskEntries = { { "ImageMask", pdf::PDFObject::createBool(true) } };
        if (kind == QLatin1String("image-mask"))
        {
            image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 1, pdf::PDFObject(), bits, maskEntries);
        }
        else
        {
            const pdf::PDFObjectReference mask = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 1, pdf::PDFObject(), bits, maskEntries);
            image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, deviceRGB, rgb, { { "Mask", pdf::PDFObject::createReference(mask) } });
        }
    }
    CompressTestHelper::setPage(builder, page, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    QVariantMap parameters = CompressTestHelper::preset("custom");
    parameters.insert(QStringLiteral("targetDpi"), 300);
    parameters.insert(QStringLiteral("jpegQuality"), 100);
    parameters.insert(QStringLiteral("goal"), QStringLiteral("prefer-quality"));
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    const pdf::PDFDocument& output = *result.documents.front().document;
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), isRecompressed ? 1 : 0);

    pdf::PDFImageCompressor compressor;
    const pdf::PDFImageCompressor::ImageStatisticsList before = compressor.collectImages(&input);
    const pdf::PDFImageCompressor::ImageStatisticsList after = compressor.collectImages(&output);
    QCOMPARE(before.size(), size_t(1));
    QCOMPARE(after.size(), size_t(1));
    QVERIFY(!before.front().image.isNull());
    QVERIFY(!after.front().image.isNull());
    QCOMPARE(after.front().image.size(), QSize(SIZE, SIZE));

    // The masks are written without a loss: the transparency is the same to the last step
    const int colorDifference = CompressTestHelper::getColorDifference(before.front().image, after.front().image);
    const int alphaDifference = CompressTestHelper::getAlphaDifference(before.front().image, after.front().image);
    QVERIFY2(colorDifference <= colorTolerance, qPrintable(QStringLiteral("The colors differ by %1").arg(colorDifference)));
    QVERIFY2(alphaDifference <= 1, qPrintable(QStringLiteral("The transparency differs by %1").arg(alphaDifference)));
}

void DocumentToolCompressTest::sparseTransparencyKept()
{
    // A large image with a soft mask, which hides only two single pixels: they are transparent
    // also in the result (the analysis of the optimizer looks only at a sample of 256 pixels)
    constexpr int SIZE = 1024;
    QByteArray mask(SIZE * SIZE, '\xFF');
    mask[100 * SIZE + 100] = 0;
    mask[103 * SIZE + 103] = 0;

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 720, 720));
    const pdf::PDFObjectReference maskReference = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceGray"), mask);
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceGray"), CompressTestHelper::createRamp(SIZE, 1, 1),
                                                                          { { "SMask", pdf::PDFObject::createReference(maskReference) } });
    CompressTestHelper::setPage(builder, page, "q 720 0 0 720 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    QVariantMap parameters = CompressTestHelper::preset("custom");
    parameters.insert(QStringLiteral("targetDpi"), 300);
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);

    const pdf::PDFImageCompressor::ImageStatisticsList after = pdf::PDFImageCompressor().collectImages(result.documents.front().document.data());
    QCOMPARE(after.size(), size_t(1));
    QCOMPARE(after.front().image.size(), QSize(SIZE, SIZE));
    QCOMPARE(qAlpha(after.front().image.pixel(100, 100)), 0);
    QCOMPARE(qAlpha(after.front().image.pixel(103, 103)), 0);
    QCOMPARE(qAlpha(after.front().image.pixel(500, 500)), 255);
}

void DocumentToolCompressTest::hostileDocuments_data()
{
    QTest::addColumn<int>("kind");

    QTest::newRow("form-drawing-itself") << 0;
    QTest::newRow("image-with-too-few-bytes") << 1;
    QTest::newRow("image-of-a-negative-size") << 2;
    QTest::newRow("senseless-matrices") << 3;
    QTest::newRow("missing-xobject") << 4;
    QTest::newRow("form-drawing-itself-many-times") << 5;
    QTest::newRow("two-forms-drawing-each-other") << 6;
    QTest::newRow("thousand-nested-forms") << 7;
}

void DocumentToolCompressTest::hostileDocuments()
{
    QFETCH(int, kind);

    // The run ends, the result can be read again or the tool reports nothing to do
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));

    switch (kind)
    {
        case 0:
        {
            const pdf::PDFObjectReference form = builder.addObject(pdf::PDFObject::createInteger(0));
            pdf::PDFDictionaryBuilder formXObjects;
            formXObjects.addEntry(pdf::PDFInplaceOrMemoryString("Fm"), pdf::PDFObject::createReference(form));
            pdf::PDFDictionaryBuilder formResources;
            formResources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(formXObjects)));
            pdf::PDFDictionaryBuilder formDictionary;
            formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
            formDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(formResources)));
            builder.setObject(form, CompressTestHelper::createStreamObject("/Fm Do", std::move(formDictionary)));
            CompressTestHelper::setPage(builder, page, "q 1 0 0 1 0 0 cm /Fm Do Q", { { "Fm", form } });
            break;
        }

        case 1:
        {
            pdf::PDFDictionaryBuilder image;
            image.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
            image.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(4000));
            image.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(4000));
            image.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
            image.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceGray"));
            const pdf::PDFObjectReference reference = builder.addObject(CompressTestHelper::createStreamObject("too few bytes", std::move(image)));
            CompressTestHelper::setPage(builder, page, "q 100 0 0 100 0 0 cm /Im Do Q", { { "Im", reference } });
            break;
        }

        case 2:
        {
            pdf::PDFDictionaryBuilder image;
            image.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
            image.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(-5));
            image.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(0));
            const pdf::PDFObjectReference reference = builder.addObject(CompressTestHelper::createStreamObject("x", std::move(image)));
            CompressTestHelper::setPage(builder, page, "q 100 0 0 100 0 0 cm /Im Do Q", { { "Im", reference } });
            break;
        }

        case 3:
        {
            const pdf::PDFObjectReference reference = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(64, 11));
            CompressTestHelper::setPage(builder, page, "q 1e30 0 0 1e30 0 0 cm /Im Do Q q 0 0 0 0 0 0 cm /Im Do Q", { { "Im", reference } });
            break;
        }

        case 4:
            CompressTestHelper::setPage(builder, page, "/Missing Do", { });
            break;

        case 5:
        {
            // A cycle is refused at once: 16 calls per level would never end by a limit of the depth only
            const pdf::PDFObjectReference form = builder.addObject(pdf::PDFObject::createInteger(0));
            QByteArray content;
            for (int i = 0; i < 16; ++i)
            {
                content.append("/Fm Do\n");
            }
            builder.setObject(form, CompressTestHelper::createFormObject(content, { { "Fm", form } }));
            CompressTestHelper::setPage(builder, page, content, { { "Fm", form } });
            break;
        }

        case 6:
        {
            const pdf::PDFObjectReference first = builder.addObject(pdf::PDFObject::createInteger(0));
            const pdf::PDFObjectReference second = builder.addObject(pdf::PDFObject::createInteger(0));
            builder.setObject(first, CompressTestHelper::createFormObject("/Fm Do", { { "Fm", second } }));
            builder.setObject(second, CompressTestHelper::createFormObject("/Fm Do", { { "Fm", first } }));
            CompressTestHelper::setPage(builder, page, "/Fm Do", { { "Fm", first } });
            break;
        }

        case 7:
        {
            // No cycle, a chain: the image at its end is too deep to be reached, the
            // limit stops the recursion before the stack ends
            pdf::PDFObjectReference next = CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(32, 13));
            for (int i = 0; i < 1000; ++i)
            {
                next = builder.addObject(CompressTestHelper::createFormObject("/Fm Do", { { "Fm", next } }));
            }
            CompressTestHelper::setPage(builder, page, "/Fm Do", { { "Fm", next } });
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    const pdf::PDFDocument document = builder.build();

    for (const char* preset : { "smaller-file", "balanced", "best-quality", "custom" })
    {
        pdf::PDFDocumentToolCompress tool;
        const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document), CompressTestHelper::preset(preset), nullptr, nullptr);
        QVERIFY2(result.status == pdf::PDFDocumentToolResultStatus::Success || result.status == pdf::PDFDocumentToolResultStatus::NothingToDo, qPrintable(result.message));

        if (result.status == pdf::PDFDocumentToolResultStatus::Success)
        {
            pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
            const pdf::PDFDocument reopened = CompressTestHelper::read(CompressTestHelper::write(*result.documents.front().document), QString(), &readerResult);
            QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);
            QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(1));
        }
    }
}

void DocumentToolCompressTest::formDrawingItselfSkipped()
{
    // A page and a form with content after the refused call: only the refused form is skipped.
    // The form draws itself three times, also through another form: one report of the type Error
    const QString message = QStringLiteral("Form XObject draws itself, it was not painted again.");

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference form = builder.addObject(pdf::PDFObject::createInteger(0));
    const pdf::PDFObjectReference other = builder.addObject(pdf::PDFObject::createInteger(0));
    builder.setObject(other, CompressTestHelper::createFormObject("/Fm Do 1 1 0 rg 0 50 25 50 re f", { { "Fm", form } }));
    builder.setObject(form, CompressTestHelper::createFormObject("q 0 1 0 rg 50 50 50 50 re f Q /Fm Do /Fm Do /Fm Do /Other Do 0 0 1 rg 50 0 50 50 re f",
                                                                 { { "Fm", form }, { "Other", other } }));
    CompressTestHelper::setPage(builder, page, "/Fm Do 1 0 0 rg 0 0 50 50 re f", { { "Fm", form } });
    const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(builder.build());

    QCOMPARE(drawn.errors.size(), 1);
    QCOMPARE(drawn.errors.front().type, pdf::RenderErrorType::Error);
    QCOMPARE(drawn.errors.front().message, message);

    QCOMPARE(drawn.at(75, 75), qRgb(0, 255, 0));    // Before the refused calls
    QCOMPARE(drawn.at(75, 25), qRgb(0, 0, 255));    // In the form after them
    QCOMPARE(drawn.at(12, 75), qRgb(255, 255, 0));  // In the other form after its refused call
    QCOMPARE(drawn.at(25, 25), qRgb(255, 0, 0));    // On the page after the form, with the colors and the matrix of the page
}

void DocumentToolCompressTest::formsNestedTooDeeplySkipped()
{
    // 33 forms inside of each other, the limit is 32: the form 33 is refused. The form 32 calls
    // it three times and the page calls the chain twice: one report of the type Error.
    const QString message = QStringLiteral("Form XObjects are nested too deeply, the innermost form was not painted.");

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFObjectReference next = builder.addObject(CompressTestHelper::createFormObject("1 1 0 rg 0 0 100 100 re f", { }));
    next = builder.addObject(CompressTestHelper::createFormObject("/Fm Do /Fm Do /Fm Do 0 1 0 rg 50 50 50 50 re f", { { "Fm", next } }));
    for (int level = 31; level >= 1; --level)
    {
        next = builder.addObject(CompressTestHelper::createFormObject("/Fm Do", { { "Fm", next } }));
    }
    CompressTestHelper::setPage(builder, page, "/Fm Do /Fm Do 1 0 0 rg 0 0 50 50 re f", { { "Fm", next } });
    const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(builder.build());

    QCOMPARE(drawn.errors.size(), 1);
    QCOMPARE(drawn.errors.front().type, pdf::RenderErrorType::Error);
    QCOMPARE(drawn.errors.front().message, message);

    QCOMPARE(drawn.at(10, 90), qRgb(255, 255, 255));    // The form 33 is not painted
    QCOMPARE(drawn.at(75, 75), qRgb(0, 255, 0));        // The form 32 goes on after the refused calls
    QCOMPARE(drawn.at(25, 25), qRgb(255, 0, 0));        // The page goes on after the chain

    // Both refused cases on one page have their own reports
    pdf::PDFDocumentBuilder bothBuilder;
    const pdf::PDFObjectReference bothPage = bothBuilder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference self = bothBuilder.addObject(pdf::PDFObject::createInteger(0));
    bothBuilder.setObject(self, CompressTestHelper::createFormObject("/Fm Do", { { "Fm", self } }));
    pdf::PDFObjectReference chain = bothBuilder.addObject(CompressTestHelper::createFormObject("0 0 m", { }));
    for (int level = 32; level >= 1; --level)
    {
        chain = bothBuilder.addObject(CompressTestHelper::createFormObject("/Fm Do", { { "Fm", chain } }));
    }
    CompressTestHelper::setPage(bothBuilder, bothPage, "/Self Do /Chain Do", { { "Self", self }, { "Chain", chain } });
    const CompressTestHelper::DrawnPage both = CompressTestHelper::draw(bothBuilder.build());
    QCOMPARE(both.errors.size(), 2);
    QCOMPARE(both.count(message), 1);
    QCOMPARE(both.count(QStringLiteral("Form XObject draws itself, it was not painted again.")), 1);
}

void DocumentToolCompressTest::formsWithinTheLimitDrawn()
{
    // A form drawn twice in a row, in another form again, and by two pages: no report
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference first = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference second = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference form = builder.addObject(CompressTestHelper::createFormObject("0 1 0 rg 0 0 10 10 re f", { }));
    const pdf::PDFObjectReference outer = builder.addObject(CompressTestHelper::createFormObject("q 1 0 0 1 80 80 cm /Fm Do Q", { { "Fm", form } }));
    CompressTestHelper::setPage(builder, first, "/Fm Do q 1 0 0 1 40 40 cm /Fm Do Q /Outer Do", { { "Fm", form }, { "Outer", outer } });
    CompressTestHelper::setPage(builder, second, "q 1 0 0 1 20 20 cm /Fm Do Q", { { "Fm", form } });
    const pdf::PDFDocument document = builder.build();

    const CompressTestHelper::DrawnPage page1 = CompressTestHelper::draw(document, 0);
    QVERIFY(page1.errors.isEmpty());
    QCOMPARE(page1.at(5, 5), qRgb(0, 255, 0));
    QCOMPARE(page1.at(45, 45), qRgb(0, 255, 0));
    QCOMPARE(page1.at(85, 85), qRgb(0, 255, 0));

    const CompressTestHelper::DrawnPage page2 = CompressTestHelper::draw(document, 1);
    QVERIFY(page2.errors.isEmpty());
    QCOMPARE(page2.at(25, 25), qRgb(0, 255, 0));

    // 32 forms inside of each other: the innermost is painted
    pdf::PDFDocumentBuilder deepBuilder;
    const pdf::PDFObjectReference deepPage = deepBuilder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFObjectReference next = deepBuilder.addObject(CompressTestHelper::createFormObject("1 1 0 rg 0 0 100 100 re f", { }));
    for (int level = 31; level >= 1; --level)
    {
        next = deepBuilder.addObject(CompressTestHelper::createFormObject("/Fm Do", { { "Fm", next } }));
    }
    CompressTestHelper::setPage(deepBuilder, deepPage, "/Fm Do", { { "Fm", next } });
    const CompressTestHelper::DrawnPage deep = CompressTestHelper::draw(deepBuilder.build());
    QVERIFY(deep.errors.isEmpty());
    QCOMPARE(deep.at(50, 50), qRgb(255, 255, 0));
}

void DocumentToolCompressTest::formGuardLeavesNothingAfterAnError()
{
    // An error inside of a form leaves it: the same form can be drawn again afterwards, it is
    // not taken for a form drawing itself (the stack of the forms is restored)
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));

    // Group with a color space, which cannot be created: an exception of the form below its Do
    pdf::PDFDictionaryBuilder group;
    group.addEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("Transparency"));
    group.addEntry(pdf::PDFInplaceOrMemoryString("CS"), pdf::PDFObject::createName("NoSuchColorSpace"));
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Group"), pdf::PDFObject::createDictionary(std::move(group)));
    const pdf::PDFObjectReference broken = builder.addObject(CompressTestHelper::createStreamObject("0 0 1 rg 0 50 50 50 re f", std::move(dictionary)));

    CompressTestHelper::setPage(builder, page, "/Broken Do /Broken Do 1 0 0 rg 0 0 50 50 re f", { { "Broken", broken } });
    const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(builder.build());

    QCOMPARE(drawn.at(25, 25), qRgb(255, 0, 0));
    QCOMPARE(drawn.count(QStringLiteral("Form XObject draws itself, it was not painted again.")), 0);
    QCOMPARE(drawn.count(QStringLiteral("Form XObjects are nested too deeply, the innermost form was not painted.")), 0);
    QVERIFY2(drawn.errors.size() >= 2, "The form is expected to fail at both calls");
}

void DocumentToolCompressTest::documentWithoutPages()
{
    // A document without pages: no image, the structure only
    pdf::PDFDocumentBuilder emptyBuilder;
    const pdf::PDFDocument empty = emptyBuilder.build();
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult emptyResult = tool.run(CompressTestHelper::createContext(empty), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QVERIFY2(emptyResult.status == pdf::PDFDocumentToolResultStatus::Success || emptyResult.status == pdf::PDFDocumentToolResultStatus::NothingToDo, qPrintable(emptyResult.message));
    QCOMPARE(emptyResult.data.value(QStringLiteral("imagesRecompressed")).toInt(), 0);
}

void DocumentToolCompressTest::hostileParameters()
{
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(1, 100);
    pdf::PDFDocumentToolCompress tool;

    const auto expectFailure = [&](const QVariantMap& parameters, const char* parameter)
    {
        const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input), parameters, nullptr, nullptr);
        QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
        QVERIFY(result.documents.empty());
        const QJsonArray errors = result.data.value(QStringLiteral("errors")).toArray();
        QVERIFY(!errors.isEmpty());
        QCOMPARE(errors.first().toObject().value(QStringLiteral("parameter")).toString(), QString::fromLatin1(parameter));
    };

    QVariantMap parameters = CompressTestHelper::preset("not-a-preset");
    expectFailure(parameters, "preset");

    for (const QVariant& value : { QVariant(0), QVariant(35), QVariant(1201), QVariant(QStringLiteral("many")), QVariant(qQNaN()), QVariant(1e300) })
    {
        parameters = CompressTestHelper::preset("custom");
        parameters.insert(QStringLiteral("targetDpi"), value);
        expectFailure(parameters, "targetDpi");
    }

    for (const QVariant& value : { QVariant(0), QVariant(101), QVariant(-1) })
    {
        parameters = CompressTestHelper::preset("custom");
        parameters.insert(QStringLiteral("jpegQuality"), value);
        expectFailure(parameters, "jpegQuality");
    }

    parameters = CompressTestHelper::preset("custom");
    parameters.insert(QStringLiteral("goal"), QStringLiteral("smallest"));
    expectFailure(parameters, "goal");

    // No document, a null document, two documents
    QCOMPARE(tool.run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
    pdf::PDFDocumentToolContext nullContext;
    nullContext.documents.emplace_back();
    QCOMPARE(tool.run(nullContext, QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
    pdf::PDFDocumentToolContext twoDocuments = CompressTestHelper::createContext(input);
    twoDocuments.documents.push_back(twoDocuments.documents.front());
    QCOMPARE(tool.run(twoDocuments, QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolCompressTest::signedInput()
{
    // A signed document: the tool works, the impact is reported (O31, plan chapter 4)
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    CompressTestHelper::setPage(builder, page, CompressTestHelper::createImageContent(256), { { "Im", CompressTestHelper::addImage(builder, CompressTestHelper::createPhoto(256, 9)) } });

    pdf::PDFDictionaryBuilder signature;
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Sig"));
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("Adobe.PPKLite"));
    signature.addEntry(pdf::PDFInplaceOrMemoryString("SubFilter"), pdf::PDFObject::createName("adbe.pkcs7.detached"));
    const pdf::PDFObjectReference signatureValue = builder.addObject(pdf::PDFObject::createDictionary(std::move(signature)));
    builder.createSignatureField(QStringLiteral("Signature1"), signatureValue, page);
    const pdf::PDFDocument document = builder.build();

    QCOMPARE(pdf::PDFForm::getSignatureCount(&document), 1);
    QCOMPARE(pdf::PDFForm::getSignatureCount(nullptr), 0);

    const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(u"optimize.compress");
    QVERIFY(entry);
    QCOMPARE(entry->getSignatureImpact(), pdf::PDFDocumentToolSignatureImpact::LostCopyOnly);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("signatureCount")).toInt(), 1);
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));

    // The fields stay in the copy (plan chapter 4)
    QCOMPARE(pdf::PDFForm::getSignatureCount(result.documents.front().document.data()), 1);

    // An unsigned document reports none
    const pdf::PDFDocumentToolResult unsignedResult = tool.run(CompressTestHelper::createContext(CompressTestHelper::createPhotoDocument(1, 200)), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(unsignedResult.data.value(QStringLiteral("signatureCount")).toInt(), 0);
}

void DocumentToolCompressTest::wholeWriteWithoutRevisions()
{
    // An incrementally updated file: the old revision has a text, which is gone in the
    // current one. The written result has no old revision (O31: hidden data must not stay).
    const QByteArray marker("OLD-REVISION-MARKER");

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    QByteArray oldContent = "% " + marker + "\n";
    oldContent.append(QByteArray(1000, ' '));
    CompressTestHelper::setPage(builder, page, oldContent, { });
    const pdf::PDFDocument first = builder.build();
    const QByteArray firstFile = CompressTestHelper::write(first);
    QVERIFY(firstFile.contains(marker));

    pdf::PDFDocumentBuilder secondBuilder(&first);
    CompressTestHelper::setPage(secondBuilder, page, "% the new content\n", { });
    const pdf::PDFDocument second = secondBuilder.build();

    QByteArray file;
    {
        QBuffer buffer(&file);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.writeIncrementalUpdate(&buffer, firstFile, &second)));
    }
    QVERIFY(file.contains(marker));
    QCOMPARE(file.count("%%EOF"), qsizetype(2));

    const pdf::PDFDocument reopened = CompressTestHelper::read(file);
    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(reopened, file), CompressTestHelper::preset("best-quality"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const QByteArray written = CompressTestHelper::write(*result.documents.front().document);
    QVERIFY(!written.contains(marker));
    QCOMPARE(written.count("%%EOF"), qsizetype(1));
    QVERIFY(!written.contains("/Prev"));
}

void DocumentToolCompressTest::encryptedInput()
{
    // The memory holds the decrypted document, the written result is encrypted again
    const QString password = QStringLiteral("user-secret");

    pdf::PDFDocument plain = CompressTestHelper::createPhotoDocument(1, 256);
    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = password;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu;
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocument built = builder.build();

    const QByteArray file = CompressTestHelper::write(built);
    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument opened = CompressTestHelper::read(file, password, &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(opened, file), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(CompressTestHelper::getImageWidths(*result.documents.front().document), std::vector<int>{ 178 });

    // The size is measured with the encryption (encrypted strings have random bytes,
    // their escapes make every writing a little different)
    const QByteArray written = CompressTestHelper::write(*result.documents.front().document);
    QVERIFY(qAbs(qint64(written.size()) - result.data.value(QStringLiteral("sizeAfter")).toInteger()) < 1024);

    // The copy asks for the password and opens with it, its image can be decoded
    pdf::PDFDocumentReader::Result withoutPassword = pdf::PDFDocumentReader::Result::OK;
    CompressTestHelper::read(written, QString(), &withoutPassword);
    QVERIFY(withoutPassword != pdf::PDFDocumentReader::Result::OK);

    pdf::PDFDocumentReader::Result withPassword = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = CompressTestHelper::read(written, password, &withPassword);
    QCOMPARE(withPassword, pdf::PDFDocumentReader::Result::OK);
    const std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&reopened);
    QCOMPARE(infos.size(), size_t(1));
    QCOMPARE(infos.front().image.width(), 178);
}

void DocumentToolCompressTest::encryptedWithoutPermission()
{
    // ISO 32000-2 Table 22, bit 4: the tool needs the permission to modify the document.
    // The owner password only: the file opens without a password with the limited permissions.
    pdf::PDFDocument plain = CompressTestHelper::createPhotoDocument(1, 200);
    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = QString();
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Modify);
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocument built = builder.build();

    const QByteArray file = CompressTestHelper::write(built);
    const pdf::PDFDocument restricted = CompressTestHelper::read(file);
    QVERIFY(restricted.getStorage().getSecurityHandler());
    QVERIFY(!restricted.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::Modify));

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult refused = tool.run(CompressTestHelper::createContext(restricted, file), QVariantMap(), nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(refused.message, QStringLiteral("The permissions of the document do not allow this tool."));
    QVERIFY(refused.documents.empty());

    // The preview too
    QCOMPARE(tool.runPreview(CompressTestHelper::createContext(restricted, file), QVariantMap(), nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);

    // The owner may everything
    const pdf::PDFDocument owner = CompressTestHelper::read(file, QStringLiteral("owner-secret"), nullptr, true);
    QVERIFY(owner.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::Modify));
    QVERIFY(tool.run(CompressTestHelper::createContext(owner, file), QVariantMap(), nullptr, nullptr).status != pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolCompressTest::eightThreadsOneInstance()
{
    // One instance of the tool serves all runs at once (the runner, MCP): the results
    // are the same as of one thread
    const pdf::PDFDocument input = CompressTestHelper::createPhotoDocument(3, 256);
    const pdf::PDFDocumentToolCompress tool;

    const pdf::PDFDocumentToolResult single = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(single.status, pdf::PDFDocumentToolResultStatus::Success);
    const qint64 expectedSize = single.data.value(QStringLiteral("sizeAfter")).toInteger();

    std::atomic<int> successes = 0;
    std::atomic<int> differentSizes = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&tool, &input, &successes, &differentSizes, expectedSize]()
        {
            const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input), CompressTestHelper::preset("balanced"), nullptr, nullptr);
            if (result.status == pdf::PDFDocumentToolResultStatus::Success && !result.documents.empty())
            {
                ++successes;
            }
            if (result.data.value(QStringLiteral("sizeAfter")).toInteger() != expectedSize)
            {
                ++differentSizes;
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    QCOMPARE(successes.load(), 8);
    QCOMPARE(differentSizes.load(), 0);
}

QTEST_MAIN(DocumentToolCompressTest)

#include "tst_documenttoolcompresstest.moc"
