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
#include "pdfobjectutils.h"
#include "pdfoptionalcontent.h"
#include "pdfparser.h"
#include "pdfprogress.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdfstreamfilters.h"

#include <QBuffer>
#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QtTest>

#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

Q_DECLARE_METATYPE(pdf::PDFObject)

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
/// Files with the structures of PDF 1.5, written by the test from the specification:
/// object streams (PDF Reference 1.7, 3.4.6), cross-reference streams (3.4.7) and the
/// layout of a linearized file (Appendix F). PDFDocumentWriter writes none of them.
class FileStructureTestHelper
{
public:
    FileStructureTestHelper() = delete;

    static constexpr int PAGE_COUNT = 2;
    static constexpr int PADDING_SIZE = 64 * 1024;

    static constexpr const char* HINT_MARKER = "HINT-TABLES-MARKER";
    static constexpr const char* OVERFLOW_MARKER = "OVERFLOW-TABLES-MARKER";

    struct FileOptions
    {
        int objectStreamCount = 1;  ///< More object streams: each one /Extends the previous one (Table 3.14)
        int paddingSize = 0;        ///< Bytes behind the objects of each object stream, no offset leads to them
        bool isFlate = false;       ///< The object streams are compressed
    };

    struct LinearizedOptions
    {
        bool hasOverflow = false;   ///< With the overflow hint stream
        bool isFlate = false;       ///< The primary hint stream is compressed
    };

    using Entries = std::initializer_list<std::pair<const char*, pdf::PDFObject>>;

    static pdf::PDFObject createDictionaryOf(Entries entries)
    {
        pdf::PDFDictionaryBuilder dictionary;
        for (const auto& entry : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), pdf::PDFObject(entry.second));
        }
        return pdf::PDFObject::createDictionary(std::move(dictionary));
    }

    static pdf::PDFObject createDictionary(const char* key, pdf::PDFObject value)
    {
        return createDictionaryOf({ { key, std::move(value) } });
    }

    /// A stream of one byte with the entries; /Length is the one of the entries, if there is one
    static pdf::PDFObject createStream(Entries entries)
    {
        pdf::PDFDictionaryBuilder dictionary;
        for (const auto& entry : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), pdf::PDFObject(entry.second));
        }
        if (dictionary.get("Length").isNull())
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(1));
        }
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray("x")));
    }

    static pdf::PDFObject createIntegers(const std::vector<int>& numbers)
    {
        return CompressTestHelper::createIntegerArray(numbers);
    }

    /// PAGE_COUNT pages of 200 x 200 points, each with a red rectangle moved by its page number
    static pdf::PDFDocument createDocument()
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < PAGE_COUNT; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            const QByteArray position = QByteArray::number(i * 20);
            CompressTestHelper::setPage(builder, page, "1 0 0 rg 10 " + position + " 50 50 re f", { });
        }
        return builder.build();
    }

    /// The document with a further entry of its catalog
    static pdf::PDFDocument withCatalogEntry(const pdf::PDFDocument& document, const char* key, pdf::PDFObject value)
    {
        pdf::PDFObjectStorage storage = document.getStorage();
        pdf::PDFObjectStorage::PDFObjects objects = storage.getObjects();

        pdf::PDFObjectStorage::Entry& entry = objects[size_t(document.getTrailerDictionary()->get("Root").getReference().objectNumber)];
        pdf::PDFDictionaryBuilder catalog(*entry.object.getDictionary());
        catalog.setEntry(pdf::PDFInplaceOrMemoryString(key), std::move(value));
        entry.object = pdf::PDFObject::createDictionary(std::move(catalog));

        storage.setObjects(std::move(objects));
        return pdf::PDFDocument(std::move(storage), document.getInfo()->version, QByteArray());
    }

    /// The document with a further object, no reference leads to it
    static pdf::PDFDocument withObject(const pdf::PDFDocument& document, pdf::PDFObject object, pdf::PDFObjectReference* reference)
    {
        pdf::PDFObjectStorage storage = document.getStorage();
        *reference = storage.addObject(std::move(object));
        return pdf::PDFDocument(std::move(storage), document.getInfo()->version, QByteArray());
    }

    static QVariantMap createCustomWithoutCleanUp()
    {
        QVariantMap parameters = CompressTestHelper::preset("custom");
        parameters.insert(QStringLiteral("targetDpi"), 150);
        parameters.insert(QStringLiteral("jpegQuality"), 75);
        parameters.insert(QStringLiteral("goal"), QStringLiteral("minimum-size"));
        parameters.insert(QStringLiteral("keepOriginalIfLarger"), true);
        parameters.insert(QStringLiteral("preserveTransparency"), true);
        parameters.insert(QStringLiteral("cleanStructure"), false);
        return parameters;
    }

    /// The reader, which repairs nothing
    static pdf::PDFDocument readStrict(const QByteArray& data, const QString& password = QString(), pdf::PDFDocumentReader::Result* readerResult = nullptr)
    {
        const auto getPassword = [&password](bool* ok)
        {
            *ok = !password.isEmpty();
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, false, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        if (readerResult)
        {
            *readerResult = reader.getReadingResult();
        }
        return document;
    }

    /// An indirect object, encrypted by the handler of the document (not the encryption
    /// dictionary, 3.5 "Encryption"; a cross-reference stream is written by the caller)
    static QByteArray serializeObject(const pdf::PDFDocument& document, pdf::PDFObjectReference reference, const pdf::PDFObject& object)
    {
        const pdf::PDFSecurityHandler* handler = document.getStorage().getSecurityHandler();
        const pdf::PDFObject& encryptObject = document.getTrailerDictionary()->get("Encrypt");
        const bool isEncryptDictionary = encryptObject.isReference() && encryptObject.getReference() == reference;

        pdf::PDFObject objectToWrite = object;
        if (handler && handler->getMode() != pdf::EncryptionMode::None && !isEncryptDictionary)
        {
            objectToWrite = handler->encryptObject(object, reference);
        }
        return serializePlainObject(reference, objectToWrite);
    }

    static QByteArray serializePlainObject(pdf::PDFObjectReference reference, const pdf::PDFObject& object)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " obj\r\n" + pdf::PDFDocumentWriter::getSerializedObject(object) + "\r\nendobj\r\n";
    }

    /// Entries of the trailer of the document in a dictionary (Table 3.13, Table 3.15)
    static void addTrailerEntries(const pdf::PDFDocument& document, pdf::PDFDictionaryBuilder& dictionary)
    {
        for (const char* key : { "Root", "Info", "ID", "Encrypt" })
        {
            pdf::PDFObject value = document.getTrailerDictionary()->get(key);
            if (!value.isNull())
            {
                dictionary.addEntry(pdf::PDFInplaceOrMemoryString(key), std::move(value));
            }
        }
    }

    /// File of the objects of the document: the streams and the encryption dictionary
    /// indirect, the other objects in object streams numbered after them, a cross-reference
    /// stream after those (generation 0 of the objects in streams: 3.4.6)
    static QByteArray createFile(const pdf::PDFDocument& document, const FileOptions& options)
    {
        const pdf::PDFObjectStorage::PDFObjects& objects = document.getStorage().getObjects();
        const pdf::PDFInteger firstObjectStream = pdf::PDFInteger(objects.size());
        const pdf::PDFInteger crossReferenceStream = firstObjectStream + options.objectStreamCount;
        const pdf::PDFObject& encryptObject = document.getTrailerDictionary()->get("Encrypt");

        // Table 3.16: the type, the offset or the number of the object stream, the generation or the index
        struct Entry
        {
            int type = 0;
            qint64 second = 0;
            int third = 0;
        };
        std::vector<Entry> entries(size_t(crossReferenceStream) + 1);
        entries[0].third = 65535;

        QByteArray file("%PDF-1.7\r\n%\xE2\xE3\xCF\xD3\r\n");

        std::vector<std::vector<size_t>> members(size_t(options.objectStreamCount));
        size_t memberCount = 0;
        for (size_t i = 1; i < objects.size(); ++i)
        {
            const pdf::PDFObject& object = objects[i].object;
            if (object.isNull())
            {
                continue;
            }

            const pdf::PDFObjectReference reference(pdf::PDFInteger(i), objects[i].generation);
            if (object.isStream() || (encryptObject.isReference() && encryptObject.getReference() == reference))
            {
                entries[i].type = 1;
                entries[i].second = file.size();
                entries[i].third = int(objects[i].generation);
                file.append(serializeObject(document, reference, object));
            }
            else
            {
                members[memberCount++ % members.size()].push_back(i);
            }
        }

        for (int streamIndex = 0; streamIndex < options.objectStreamCount; ++streamIndex)
        {
            const pdf::PDFInteger number = firstObjectStream + streamIndex;

            // Table 3.14: N pairs of integers, the objects from First on
            QByteArray pairs;
            QByteArray body;
            int memberIndex = 0;
            for (const size_t member : members[size_t(streamIndex)])
            {
                pairs += QByteArray::number(qulonglong(member)) + " " + QByteArray::number(body.size()) + " ";
                body += pdf::PDFDocumentWriter::getSerializedObject(objects[member].object) + "\r\n";

                entries[member].type = 2;
                entries[member].second = number;
                entries[member].third = memberIndex++;
            }

            QByteArray content = pairs + body + QByteArray(options.paddingSize, ' ');

            pdf::PDFDictionaryBuilder dictionary;
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("ObjStm"));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("N"), pdf::PDFObject::createInteger(memberIndex));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("First"), pdf::PDFObject::createInteger(pairs.size()));
            if (streamIndex > 0)
            {
                dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Extends"), pdf::PDFObject::createReference(pdf::PDFObjectReference(number - 1, 0)));
            }
            if (options.isFlate)
            {
                // Noise of the padding is not compressed away: the size of the dead container stays
                for (int i = 0; i < options.paddingSize; ++i)
                {
                    content[content.size() - 1 - i] = char(' ' + (i * 7919) % 89);
                }
                content = pdf::PDFFlateDecodeFilter::compress(content);
                dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
            }
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));

            entries[size_t(number)].type = 1;
            entries[size_t(number)].second = file.size();
            file.append(serializeObject(document, pdf::PDFObjectReference(number, 0), pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)))));
        }

        // Table 3.15; the stream is not encrypted (3.4.7) and has its own entry
        const qint64 crossReferenceOffset = file.size();
        entries[size_t(crossReferenceStream)].type = 1;
        entries[size_t(crossReferenceStream)].second = crossReferenceOffset;

        QByteArray table;
        for (const Entry& entry : entries)
        {
            table.append(char(entry.type));
            for (int shift = 24; shift >= 0; shift -= 8)
            {
                table.append(char((entry.second >> shift) & 0xFF));
            }
            table.append(char((entry.third >> 8) & 0xFF));
            table.append(char(entry.third & 0xFF));
        }

        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XRef"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Size"), pdf::PDFObject::createInteger(pdf::PDFInteger(entries.size())));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("W"), createIntegers({ 1, 4, 2 }));
        addTrailerEntries(document, dictionary);
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(table.size()));
        file.append(serializePlainObject(pdf::PDFObjectReference(crossReferenceStream, 0), pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(table)))));

        file.append("startxref\r\n" + QByteArray::number(crossReferenceOffset) + "\r\n%%EOF\r\n");
        return file;
    }

    /// Offsets of a linearized file (Table F.1), known after the file is laid out
    struct LinearizedLayout
    {
        qint64 fileLength = 0;
        qint64 primaryOffset = 0;
        qint64 primaryLength = 0;
        qint64 overflowOffset = 0;
        qint64 overflowLength = 0;
        qint64 firstPageEnd = 0;
        qint64 mainTableEntry = 0;
        qint64 mainTable = 0;
    };

    static QByteArray formatNumber(qint64 number)
    {
        return QByteArray::number(number).rightJustified(10, '0');
    }

    /// One entry of a table in 20 bytes (3.4.3)
    static QByteArray createTableEntry(qint64 offset, qint64 generation, char type)
    {
        return formatNumber(offset) + " " + QByteArray::number(generation).rightJustified(5, '0') + " " + type + "\r\n";
    }

    /// Appendix F, Example F.1 in the order of its parts: the header (1), the parameter
    /// dictionary (2), the first-page table (3), the primary hint stream (5), the objects of the
    /// document (6 - 9), the overflow hint stream (10) and the main table (11). The objects
    /// of the first-page table have the highest numbers (F.2.3), the hint streams last (F.2.5).
    static QByteArray createLinearizedFile(const pdf::PDFDocument& document, const LinearizedOptions& options)
    {
        // The offsets settle after the passes: only /Prev of the trailer has no fixed width
        LinearizedLayout layout;
        QByteArray file;
        for (int pass = 0; pass < 4; ++pass)
        {
            file = createLinearizedFilePass(document, options, layout);
        }
        return file;
    }

    static QByteArray createLinearizedFilePass(const pdf::PDFDocument& document, const LinearizedOptions& options, LinearizedLayout& layout)
    {
        const pdf::PDFObjectStorage::PDFObjects& objects = document.getStorage().getObjects();
        const pdf::PDFInteger linearizationNumber = pdf::PDFInteger(objects.size());
        const pdf::PDFInteger primaryNumber = linearizationNumber + 1;
        const pdf::PDFInteger overflowNumber = linearizationNumber + 2;
        const pdf::PDFInteger objectCount = primaryNumber + (options.hasOverflow ? 2 : 1);

        QByteArray file("%PDF-1.7\r\n%\xE2\xE3\xCF\xD3\r\n");

        // Part 2
        const qint64 linearizationOffset = file.size();
        QByteArray hints = "[ " + formatNumber(layout.primaryOffset) + " " + formatNumber(layout.primaryLength);
        if (options.hasOverflow)
        {
            hints += " " + formatNumber(layout.overflowOffset) + " " + formatNumber(layout.overflowLength);
        }
        hints += " ]";
        file.append(QByteArray::number(linearizationNumber) + " 0 obj\r\n<< /Linearized 1.0 /L " + formatNumber(layout.fileLength) + " /H " + hints +
                    " /O " + QByteArray::number(document.getCatalog()->getPage(0)->getPageReference().objectNumber) +
                    " /E " + formatNumber(layout.firstPageEnd) + " /N " + QByteArray::number(qulonglong(document.getCatalog()->getPageCount())) +
                    " /T " + formatNumber(layout.mainTableEntry) + " >>\r\nendobj\r\n");

        // Part 3
        const qint64 firstTable = file.size();
        const qint64 tableSize = 20 * (objectCount - linearizationNumber);
        QByteArray firstTableHead = "xref\r\n" + QByteArray::number(linearizationNumber) + " " + QByteArray::number(objectCount - linearizationNumber) + "\r\n";

        pdf::PDFDictionaryBuilder firstTrailer;
        firstTrailer.addEntry(pdf::PDFInplaceOrMemoryString("Size"), pdf::PDFObject::createInteger(objectCount));
        firstTrailer.addEntry(pdf::PDFInplaceOrMemoryString("Prev"), pdf::PDFObject::createInteger(layout.mainTable));
        addTrailerEntries(document, firstTrailer);
        const QByteArray firstTrailerText = "trailer\r\n" + pdf::PDFDocumentWriter::getSerializedObject(pdf::PDFObject::createDictionary(std::move(firstTrailer))) + "\r\nstartxref\r\n0\r\n%%EOF\r\n";

        // Part 5: the positions of the tables are positions in the decoded stream (F.2.5)
        const qint64 primaryOffset = firstTable + firstTableHead.size() + tableSize + firstTrailerText.size();
        QByteArray primaryData = QByteArray(HINT_MARKER).repeated(PADDING_SIZE / 16);
        pdf::PDFDictionaryBuilder primaryDictionary;
        primaryDictionary.addEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createInteger(16));
        primaryDictionary.addEntry(pdf::PDFInplaceOrMemoryString("O"), pdf::PDFObject::createInteger(32));
        if (options.isFlate)
        {
            for (qsizetype i = 0; i < primaryData.size(); ++i)
            {
                primaryData[i] = char(' ' + (i * 7919) % 89);
            }
            primaryData = pdf::PDFFlateDecodeFilter::compress(primaryData);
            primaryDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
        }
        primaryDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(primaryData.size()));
        const QByteArray primaryText = serializePlainObject(pdf::PDFObjectReference(primaryNumber, 0), pdf::PDFObject::createStream(pdf::PDFStream(std::move(primaryDictionary), std::move(primaryData))));

        // Parts 6 - 9
        QByteArray body;
        std::vector<qint64> offsets(objects.size(), -1);
        const qint64 bodyOffset = primaryOffset + primaryText.size();
        for (size_t i = 1; i < objects.size(); ++i)
        {
            if (!objects[i].object.isNull())
            {
                offsets[i] = bodyOffset + body.size();
                body.append(serializePlainObject(pdf::PDFObjectReference(pdf::PDFInteger(i), objects[i].generation), objects[i].object));
            }
        }

        // Part 10: a stream without the positions of the tables (F.2.5)
        const qint64 overflowOffset = bodyOffset + body.size();
        QByteArray overflowText;
        if (options.hasOverflow)
        {
            QByteArray overflowData(OVERFLOW_MARKER);
            pdf::PDFDictionaryBuilder overflowDictionary;
            overflowDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(overflowData.size()));
            overflowText = serializePlainObject(pdf::PDFObjectReference(overflowNumber, 0), pdf::PDFObject::createStream(pdf::PDFStream(std::move(overflowDictionary), std::move(overflowData))));
        }

        // Part 11
        const qint64 mainTable = overflowOffset + overflowText.size();
        QByteArray mainTableText = "xref\r\n0 " + QByteArray::number(linearizationNumber) + "\r\n";
        const qint64 mainTableEntry = mainTable + mainTableText.size() - 1;
        mainTableText.append(createTableEntry(0, 65535, 'f'));
        for (size_t i = 1; i < objects.size(); ++i)
        {
            mainTableText.append(offsets[i] >= 0 ? createTableEntry(offsets[i], objects[i].generation, 'n') : createTableEntry(0, objects[i].generation, 'f'));
        }
        mainTableText.append("trailer\r\n<< /Size " + QByteArray::number(linearizationNumber) + " >>\r\nstartxref\r\n" + formatNumber(firstTable) + "\r\n%%EOF\r\n");

        // The first-page table with the offsets known now
        file.append(firstTableHead);
        file.append(createTableEntry(linearizationOffset, 0, 'n'));
        file.append(createTableEntry(primaryOffset, 0, 'n'));
        if (options.hasOverflow)
        {
            file.append(createTableEntry(overflowOffset, 0, 'n'));
        }
        file.append(firstTrailerText);
        file.append(primaryText);
        file.append(body);
        file.append(overflowText);
        file.append(mainTableText);

        layout.fileLength = file.size();
        layout.primaryOffset = primaryOffset;
        layout.primaryLength = primaryText.size();
        layout.overflowOffset = overflowOffset;
        layout.overflowLength = overflowText.size();
        layout.firstPageEnd = bodyOffset + body.size() / 2;
        layout.mainTableEntry = mainTableEntry;
        layout.mainTable = mainTable;
        return file;
    }
};

class DocumentToolCompressTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // First: the peak of the memory of the process only grows
    void fontsHeldOnlyForThePage();
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
    void colorTransformOfTheFilter_data();
    void colorTransformOfTheFilter();
    void colorsOfChromeJpegKept();
    void recompressOfFilterChainsByTool();
    void imageDecodedOnAnotherPage();
    void imageWithoutPixels();
    void imageKindsKeepTheirLook_data();
    void imageKindsKeepTheirLook();
    void sparseTransparencyKept();
    void identicalLayersAndAnnotationsStayDistinct();
    void grayTonesKept_data();
    void grayTonesKept();
    void blackAndWhiteStaysBitonal_data();
    void blackAndWhiteStaysBitonal();
    void sparseColorsKept_data();
    void sparseColorsKept();
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
    void fileStructureDropped_data();
    void fileStructureDropped();
    void fileStructureUnusedCounter();
    void fileStructureReferenced_data();
    void fileStructureReferenced();
    void fileStructureReferencedByTrailer();
    void fileStructureLinearized_data();
    void fileStructureLinearized();
    void fileStructureLinearizedReferenced();
    void fileStructureLookalikes_data();
    void fileStructureLookalikes();
    void fileStructureEncrypted();
    void fileStructureIncrementalUpdate();
    void fileStructureDamagedRead();
    void fileStructureMissingObjects_data();
    void fileStructureMissingObjects();
    void fileStructureMissingObjectsExtends();
    void fileStructureHybrid();
    void fileStructureExtendsChain();
    void fileStructureOldCallers();
};

void DocumentToolCompressTest::initTestCase()
{
    // The texts of the frames: numbers and sizes of a locale with a decimal point
    QLocale::setDefault(QLocale::c());
}

void DocumentToolCompressTest::fontsHeldOnlyForThePage()
{
    // An OCR layer has a font of its own size for almost every word: the walk over the pages
    // holds the realized fonts of a page only, within the limit of the cache
    pdf::PDFDocument document;
    {
        pdf::PDFDocumentBuilder builder;
        for (int pageIndex = 0; pageIndex < 8; ++pageIndex)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            QByteArray content("BT\n");
            for (int word = 0; word < 100; ++word)
            {
                content += QByteArray("/F1 ") + QByteArray::number(6.0 + pageIndex * 100 + word * 0.01, 'f', 2) + QByteArray(" Tf 10 10 Td (word) Tj\n");
            }
            content += "ET\n";
            const pdf::PDFObjectReference contentReference = builder.addObject(CompressTestHelper::createStreamObject(content));

            pdf::PDFDictionaryBuilder fontDictionary;
            fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
            fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Type1"));
            fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BaseFont"), pdf::PDFObject::createName("Helvetica"));
            const pdf::PDFObjectReference font = builder.addObject(pdf::PDFObject::createDictionary(std::move(fontDictionary)));

            pdf::PDFDictionaryBuilder fonts;
            fonts.addEntry(pdf::PDFInplaceOrMemoryString("F1"), pdf::PDFObject::createReference(font));
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::move(fonts)));
            pdf::PDFDictionaryBuilder pageUpdate;
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
        }
        document = builder.build();
    }

    pdf::PDFImageCompressor::Environment environment(&document);
    for (int pageIndex = 0; pageIndex < 8; ++pageIndex)
    {
        environment.processPagePlacements(pageIndex, [](pdf::PDFObjectReference, QPointF) { }, nullptr);
        QVERIFY2(environment.getFontCache()->getRealizedFontCount() <= pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT, qPrintable(QString::number(environment.getFontCache()->getRealizedFontCount())));
    }
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
        presetTitles << option.getTitle();
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
    // Skia of Chrome writes "/ColorTransform 0" next to the filter of an image, where it means
    // nothing (PDF Reference 1.7, Table 3.11: a parameter of the filter); DL is the decoded length
    // of the old samples. Neither describes the new JPEG (found by "Finite element method -
    // Wikipedia.pdf" of UnitTestsDocumentToolCorpus)
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
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceRGB"), samples, { { "ColorTransform", pdf::PDFObject::createInteger(0) }, { "DL", pdf::PDFObject::createInteger(samples.size()) } });
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
    QVERIFY(!imageObject.getStream()->getDictionary()->hasKey("DL"));

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

void DocumentToolCompressTest::colorTransformOfTheFilter_data()
{
    // PDF Reference 1.7, Table 3.11: ColorTransform is a parameter of DCTDecode (0 = the samples
    // are not transformed, the default of three components is 1), an Adobe marker overrides it.
    // The JPEG of these rows is in YCbCr, the encoder writes no Adobe marker: drawn without the
    // transformation, the red (220, 40, 40) is its Y, Cb, Cr (94, 98, 218).
    QTest::addColumn<int>("kind");
    QTest::addColumn<QRgb>("color");

    const QRgb red = qRgb(220, 40, 40);
    const QRgb notTransformed = qRgb(94, 98, 218);
    QTest::newRow("no parameter") << 0 << red;
    QTest::newRow("next to the filter, as Skia of Chrome writes it") << 1 << red;
    QTest::newRow("parameter 1") << 2 << red;
    QTest::newRow("parameter 0") << 3 << notTransformed;
    QTest::newRow("parameter 0 in an array") << 4 << notTransformed;
    QTest::newRow("parameter 0 of the last filter of a chain") << 5 << notTransformed;
    QTest::newRow("parameter 0 of another filter of a chain") << 6 << red;
    QTest::newRow("the Adobe marker overrides the parameter 0") << 7 << red;
}

void DocumentToolCompressTest::colorTransformOfTheFilter()
{
    QFETCH(int, kind);
    QFETCH(QRgb, color);

    constexpr int SIZE = 64;
    QImage image(SIZE, SIZE, QImage::Format_RGB888);
    image.fill(qRgb(220, 40, 40));

    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = pdf::PDFImage::ImageCompression::JPEG;
    options.jpegQuality = 95;
    QByteArray jpeg = *pdf::PDFImage::createStreamFromImage(image, options).getContent();
    QVERIFY(jpeg.startsWith("\xFF\xD8"));

    const auto createParameter = [](int value)
    {
        pdf::PDFDictionaryBuilder parameter;
        parameter.addEntry(pdf::PDFInplaceOrMemoryString("ColorTransform"), pdf::PDFObject::createInteger(value));
        return pdf::PDFObject::createDictionary(std::move(parameter));
    };
    const auto createArray = [](std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder array;
        for (const pdf::PDFObject& item : items)
        {
            array.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(array));
    };
    const pdf::PDFObject dctDecode = pdf::PDFObject::createName("DCTDecode");
    const pdf::PDFObject chain = createArray({ pdf::PDFObject::createName("FlateDecode"), dctDecode });

    std::vector<std::pair<QByteArray, pdf::PDFObject>> extra;
    switch (kind)
    {
        case 0:
            extra = { { "Filter", dctDecode } };
            break;
        case 1:
            extra = { { "Filter", dctDecode }, { "ColorTransform", pdf::PDFObject::createInteger(0) } };
            break;
        case 2:
            extra = { { "Filter", dctDecode }, { "DecodeParms", createParameter(1) } };
            break;
        case 3:
            extra = { { "Filter", dctDecode }, { "DecodeParms", createParameter(0) } };
            break;
        case 4:
            extra = { { "Filter", createArray({ dctDecode }) }, { "DecodeParms", createArray({ createParameter(0) }) } };
            break;
        case 5:
            extra = { { "Filter", chain }, { "DecodeParms", createArray({ pdf::PDFObject(), createParameter(0) }) } };
            jpeg = pdf::PDFFlateDecodeFilter::compress(jpeg);
            break;
        case 6:
            extra = { { "Filter", chain }, { "DecodeParms", createArray({ createParameter(0), pdf::PDFObject() }) } };
            jpeg = pdf::PDFFlateDecodeFilter::compress(jpeg);
            break;
        case 7:
        {
            // The segment APP14 of Adobe behind the marker SOI: the last byte is the transformation
            const QByteArray adobe = QByteArray::fromHex("FFEE000E41646F626500640000000001");
            jpeg.insert(2, adobe);
            extra = { { "Filter", dctDecode }, { "DecodeParms", createParameter(0) } };
            break;
        }
        default:
            QFAIL("Unknown row");
    }

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference imageReference = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceRGB"), jpeg, std::move(extra));
    CompressTestHelper::setPage(builder, page, "q 100 0 0 100 0 0 cm /Im Do Q", { { "Im", imageReference } });

    const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(builder.build());
    const QRgb pixel = drawn.at(50, 50);
    const int difference = qMax(qAbs(qRed(pixel) - qRed(color)), qMax(qAbs(qGreen(pixel) - qGreen(color)), qAbs(qBlue(pixel) - qBlue(color))));
    QVERIFY2(difference <= 6, qPrintable(QStringLiteral("%1 is not %2").arg(QColor(pixel).name(), QColor(color).name())));
}

void DocumentToolCompressTest::colorsOfChromeJpegKept()
{
    // A JPEG in YCbCr with "/ColorTransform 0" next to its filter, as in every PDF printed from
    // Chrome. Read as a parameter of the filter, its Y, Cb, Cr would be drawn as R, G, B and also
    // written so into the new JPEG: the file would stay in wrong colors for every other viewer.
    constexpr int SIZE = 256;
    QImage image(SIZE, SIZE, QImage::Format_RGB888);
    QRandomGenerator generator(11);
    for (int y = 0; y < SIZE; ++y)
    {
        for (int x = 0; x < SIZE; ++x)
        {
            const QRgb base = x < SIZE / 2 ? qRgb(220, 40, 40) : qRgb(40, 200, 60);
            const auto noise = [&generator](int value) { return qBound(0, value + generator.bounded(-8, 9), 255); };
            image.setPixel(x, y, qRgb(noise(qRed(base)), noise(qGreen(base)), noise(qBlue(base))));
        }
    }

    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = pdf::PDFImage::ImageCompression::JPEG;
    options.jpegQuality = 95;
    const QByteArray jpeg = *pdf::PDFImage::createStreamFromImage(image, options).getContent();

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference imageReference = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceRGB"), jpeg, { { "Filter", pdf::PDFObject::createName("DCTDecode") }, { "ColorTransform", pdf::PDFObject::createInteger(0) } });
    CompressTestHelper::setPage(builder, page, "q 100 0 0 100 0 0 cm /Im Do Q", { { "Im", imageReference } });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(document, CompressTestHelper::write(document)), CompressTestHelper::preset(pdf::PDFDocumentToolCompress::PRESET_SMALLER_FILE), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);

    const auto isNear = [](QRgb first, QRgb second)
    {
        return qMax(qAbs(qRed(first) - qRed(second)), qMax(qAbs(qGreen(first) - qGreen(second)), qAbs(qBlue(first) - qBlue(second)))) <= 16;
    };
    const pdf::PDFDocument* drawnDocuments[] = { &document, result.documents.front().document.data() };
    for (const pdf::PDFDocument* drawnDocument : drawnDocuments)
    {
        const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(*drawnDocument);
        QVERIFY2(isNear(drawn.at(25, 50), qRgb(220, 40, 40)), qPrintable(QColor(drawn.at(25, 50)).name()));
        QVERIFY2(isNear(drawn.at(75, 50), qRgb(40, 200, 60)), qPrintable(QColor(drawn.at(75, 50)).name()));
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

void DocumentToolCompressTest::identicalLayersAndAnnotationsStayDistinct()
{
    // The clean-up merges identical objects, but not these: the reference is their identity.
    // Two layers of one name, one of them off: one object would show the hidden layer.
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));

    const auto createLayer = []()
    {
        pdf::PDFDictionaryBuilder layer;
        layer.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("OCG"));
        layer.addEntry(pdf::PDFInplaceOrMemoryString("Name"), pdf::PDFObject::createString("Notes"));
        return pdf::PDFObject::createDictionary(std::move(layer));
    };
    const pdf::PDFObjectReference hiddenLayer = builder.addObject(createLayer());
    const pdf::PDFObjectReference shownLayer = builder.addObject(createLayer());

    pdf::PDFArrayBuilder layers;
    layers.appendItem(pdf::PDFObject::createReference(hiddenLayer));
    layers.appendItem(pdf::PDFObject::createReference(shownLayer));
    pdf::PDFArrayBuilder off;
    off.appendItem(pdf::PDFObject::createReference(hiddenLayer));
    pdf::PDFDictionaryBuilder configuration;
    configuration.addEntry(pdf::PDFInplaceOrMemoryString("OFF"), pdf::PDFObject::createArray(std::move(off)));
    pdf::PDFDictionaryBuilder properties;
    properties.addEntry(pdf::PDFInplaceOrMemoryString("OCGs"), pdf::PDFObject::createArray(std::move(layers)));
    properties.addEntry(pdf::PDFInplaceOrMemoryString("D"), pdf::PDFObject::createDictionary(std::move(configuration)));
    builder.setCatalogOptionalContentProperties(builder.addObject(pdf::PDFObject::createDictionary(std::move(properties))));

    const auto createAnnotation = []()
    {
        pdf::PDFDictionaryBuilder annotation;
        annotation.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Annot"));
        annotation.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Square"));
        annotation.addEntry(pdf::PDFInplaceOrMemoryString("Rect"), CompressTestHelper::createNumberArray({ 10, 10, 50, 50 }));
        return pdf::PDFObject::createDictionary(std::move(annotation));
    };
    const pdf::PDFObjectReference firstAnnotation = builder.addObject(createAnnotation());
    const pdf::PDFObjectReference secondAnnotation = builder.addObject(createAnnotation());
    pdf::PDFArrayBuilder annotations;
    annotations.appendItem(pdf::PDFObject::createReference(firstAnnotation));
    annotations.appendItem(pdf::PDFObject::createReference(secondAnnotation));
    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Annots"), pdf::PDFObject::createArray(std::move(annotations)));
    builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));

    // Two identical plain dictionaries are merged, so the clean-up did its work
    pdf::PDFDictionaryBuilder plain;
    plain.addEntry(pdf::PDFInplaceOrMemoryString("Sample"), pdf::PDFObject::createInteger(1));
    const pdf::PDFObject plainObject = pdf::PDFObject::createDictionary(std::move(plain));
    builder.addObject(plainObject);
    builder.addObject(plainObject);
    const pdf::PDFDocument input = builder.build();
    QVERIFY(input.getCatalog()->getOptionalContentProperties()->isValid());

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), CompressTestHelper::preset(pdf::PDFDocumentToolCompress::PRESET_BEST_QUALITY), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const pdf::PDFDocument output = CompressTestHelper::read(CompressTestHelper::write(*result.documents.front().document));
    const pdf::PDFOptionalContentProperties* outputProperties = output.getCatalog()->getOptionalContentProperties();
    QVERIFY(outputProperties->isValid());
    QCOMPARE(outputProperties->getAllOptionalContentGroups().size(), size_t(2));
    QVERIFY(outputProperties->getAllOptionalContentGroups().front() != outputProperties->getAllOptionalContentGroups().back());
    QCOMPARE(outputProperties->getDefaultConfiguration().getOffArray().size(), size_t(1));

    const std::vector<pdf::PDFObjectReference>& outputAnnotations = output.getCatalog()->getPage(0)->getAnnotations();
    QCOMPARE(outputAnnotations.size(), size_t(2));
    QVERIFY(outputAnnotations.front() != outputAnnotations.back());
}

void DocumentToolCompressTest::grayTonesKept_data()
{
    QTest::addColumn<int>("firstTone");
    QTest::addColumn<int>("secondTone");
    QTest::addColumn<int>("tolerance");

    // A flat panel is one tone, a soft gradient lies inside of two steps of the analysis.
    // The decoded tones are written as the samples and decoded again: the default color
    // management moves a dark tone by more (30 is decoded as 24, then as 17)
    QTest::newRow("light-panel-on-white") << 230 << 255 << 2;
    QTest::newRow("solid-light-gray") << 200 << 200 << 2;
    QTest::newRow("solid-middle-gray") << 128 << 128 << 2;
    QTest::newRow("solid-dark-gray") << 30 << 30 << 8;
    QTest::newRow("two-tones") << 64 << 220 << 2;
    QTest::newRow("soft-gradient") << 203 << 212 << 2;
    QTest::newRow("just-below-white") << 247 << 247 << 2;
}

void DocumentToolCompressTest::grayTonesKept()
{
    QFETCH(int, firstTone);
    QFETCH(int, secondTone);
    QFETCH(int, tolerance);

    // An image of a few gray tones is not a text scan: the default preset must not make
    // it black and white, a light gray panel turned black or disappeared
    constexpr int SIZE = 64;
    QByteArray samples(SIZE * SIZE, char(firstTone));
    for (int i = 0; i < samples.size(); ++i)
    {
        if ((i / SIZE) % 8 == 0)
        {
            samples[i] = char(secondTone);
        }
    }

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceGray"), samples);
    CompressTestHelper::setPage(builder, page, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);

    const pdf::PDFDocument& output = *result.documents.front().document;
    pdf::PDFImageCompressor compressor;
    const pdf::PDFImageCompressor::ImageStatisticsList before = compressor.collectImages(&input);
    const pdf::PDFImageCompressor::ImageStatisticsList after = compressor.collectImages(&output);
    QCOMPARE(before.size(), size_t(1));
    QCOMPARE(after.size(), size_t(1));
    QCOMPARE(after.front().image.size(), QSize(SIZE, SIZE));

    const int difference = CompressTestHelper::getColorDifference(before.front().image, after.front().image);
    QVERIFY2(difference <= tolerance, qPrintable(QStringLiteral("The gray tones differ by %1: %2 -> %3").arg(difference).arg(qGray(before.front().image.pixel(5, 5))).arg(qGray(after.front().image.pixel(5, 5)))));

    const pdf::PDFDictionary* dictionary = output.getObjectByReference(after.front().reference).getStream()->getDictionary();
    QCOMPARE(pdf::PDFImageOptimizer::readFilterName(&output, dictionary), QStringLiteral("FlateDecode"));
}

void DocumentToolCompressTest::blackAndWhiteStaysBitonal_data()
{
    QTest::addColumn<int>("firstTone");
    QTest::addColumn<int>("secondTone");

    QTest::newRow("black-on-white") << 255 << 0;
    QTest::newRow("near-black-on-near-white") << 248 << 7;
    QTest::newRow("solid-white") << 255 << 255;
    QTest::newRow("solid-black") << 0 << 0;

    // Decoded as tones of the black: a threshold from their histogram made a part of them white
    QTest::newRow("black-and-almost-black") << 16 << 0;
    QTest::newRow("two-tones-of-the-black") << 16 << 12;
}

void DocumentToolCompressTest::blackAndWhiteStaysBitonal()
{
    QFETCH(int, firstTone);
    QFETCH(int, secondTone);

    // A real black and white page still gets the small coding by one bit, and every pixel
    // keeps its side: a solid image has no threshold between two tones
    constexpr int SIZE = 64;
    QByteArray samples(SIZE * SIZE, char(firstTone));
    for (int i = 0; i < samples.size(); ++i)
    {
        if ((i / SIZE) % 8 == 0)
        {
            samples[i] = char(secondTone);
        }
    }

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceGray"), samples);
    CompressTestHelper::setPage(builder, page, "q 72 0 0 72 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);

    const pdf::PDFDocument& output = *result.documents.front().document;
    const pdf::PDFImageCompressor::ImageStatisticsList before = pdf::PDFImageCompressor().collectImages(&input);
    const pdf::PDFImageCompressor::ImageStatisticsList after = pdf::PDFImageCompressor().collectImages(&output);
    QCOMPARE(before.size(), size_t(1));
    QCOMPARE(after.size(), size_t(1));

    const pdf::PDFDictionary* dictionary = output.getObjectByReference(after.front().reference).getStream()->getDictionary();
    QCOMPARE(pdf::PDFImageOptimizer::readFilterName(&output, dictionary), QStringLiteral("JBIG2Decode"));

    const QImage expected = before.front().image.convertToFormat(QImage::Format_RGB32);
    const QImage decoded = after.front().image.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < SIZE; ++y)
    {
        QVERIFY2(std::abs(qGray(decoded.pixel(5, y)) - qGray(expected.pixel(5, y))) <= 8, qPrintable(QStringLiteral("Row %1 is %2, expected %3").arg(y).arg(qGray(decoded.pixel(5, y))).arg(qGray(expected.pixel(5, y)))));
    }
}

void DocumentToolCompressTest::sparseColorsKept_data()
{
    QTest::addColumn<bool>("isBlackAndWhite");
    QTest::addColumn<int>("rowOffset");

    // Four neighboring rows: a sample of every fourth row misses at least three of them
    for (int rowOffset = 0; rowOffset < 4; ++rowOffset)
    {
        QTest::addRow("gray-photo-row-%d", rowOffset) << false << rowOffset;
        QTest::addRow("black-and-white-page-row-%d", rowOffset) << true << rowOffset;
    }
}

void DocumentToolCompressTest::sparseColorsKept()
{
    QFETCH(bool, isBlackAndWhite);
    QFETCH(int, rowOffset);

    // A red line of one pixel on a large image keeps its color, wherever it lies (the analysis
    // of the optimizer looked for the colors only at a sample of 256 pixels)
    constexpr int SIZE = 1024;
    const int redRow = 500 + rowOffset;
    QByteArray samples;
    samples.reserve(SIZE * SIZE * 3);
    QRandomGenerator generator(rowOffset + 1);
    for (int y = 0; y < SIZE; ++y)
    {
        for (int x = 0; x < SIZE; ++x)
        {
            const char tone = isBlackAndWhite ? ((y % 64 < 4) ? '\x00' : '\xFF') : char((x + y) * 200 / (2 * SIZE) + generator.bounded(40));
            samples.append(y == redRow ? '\xFF' : tone);
            samples.append(y == redRow ? '\x00' : tone);
            samples.append(y == redRow ? '\x00' : tone);
        }
    }

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 720, 720));
    const pdf::PDFObjectReference image = CompressTestHelper::addRawImage(builder, SIZE, SIZE, 8, pdf::PDFObject::createName("DeviceRGB"), samples);
    CompressTestHelper::setPage(builder, page, "q 720 0 0 720 0 0 cm /Im Do Q", { { "Im", image } });
    const pdf::PDFDocument input = builder.build();

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, QByteArray(10000000, 'x'), QStringLiteral("a.pdf"), false), CompressTestHelper::preset("balanced"), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("imagesRecompressed")).toInt(), 1);

    const pdf::PDFImageCompressor::ImageStatisticsList after = pdf::PDFImageCompressor().collectImages(result.documents.front().document.data());
    QCOMPARE(after.size(), size_t(1));
    QCOMPARE(after.front().image.size(), QSize(SIZE, SIZE));

    // The JPEG of a photograph softens a line of one pixel, its color must be seen
    const QRgb pixel = after.front().image.pixel(SIZE / 2, redRow);
    QVERIFY2(qRed(pixel) - qGreen(pixel) > 32, qPrintable(QStringLiteral("The line is rgb(%1, %2, %3)").arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel))));
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

void DocumentToolCompressTest::fileStructureDropped_data()
{
    using Helper = FileStructureTestHelper;

    QTest::addColumn<int>("objectStreamCount");
    QTest::addColumn<int>("paddingSize");
    QTest::addColumn<bool>("isFlate");

    // The padding is what no offset of the object stream leads to: it makes the dead
    // container larger than what the unpacked objects add, so only its removal saves bytes
    QTest::newRow("object-stream") << 1 << Helper::PADDING_SIZE << false;
    QTest::newRow("object-stream-flate") << 1 << Helper::PADDING_SIZE << true;

    // PDF Reference 1.7, Table 3.14: /Extends of one dead container to another one keeps nothing
    QTest::newRow("extends") << 2 << Helper::PADDING_SIZE << false;
}

void DocumentToolCompressTest::fileStructureDropped()
{
    using Helper = FileStructureTestHelper;

    QFETCH(int, objectStreamCount);
    QFETCH(int, paddingSize);
    QFETCH(bool, isFlate);

    Helper::FileOptions options;
    options.objectStreamCount = objectStreamCount;
    options.paddingSize = paddingSize;
    options.isFlate = isFlate;

    // An unreferenced object of another kind: with the clean-up off it stays
    pdf::PDFDocument source = Helper::createDocument();
    pdf::PDFObjectReference orphan;
    source = Helper::withObject(source, Helper::createDictionary("Orphan", pdf::PDFObject::createInteger(1)), &orphan);

    const QByteArray file = Helper::createFile(source, options);
    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument input = Helper::readStrict(file, QString(), &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);

    // What really stays after the reading: the containers are objects of the storage next
    // to the unpacked objects, numbered after the objects of the source
    const size_t firstContainer = source.getStorage().getObjects().size();
    for (size_t i = 0; i < size_t(objectStreamCount); ++i)
    {
        QVERIFY(pdf::PDFObjectUtils::isObjectStream(input.getStorage().getObjects().at(firstContainer + i).object));
    }
    QVERIFY(input.getStorage().getObjects().at(firstContainer + objectStreamCount).object.isStream());
    QVERIFY(input.getStorage().getObject(pdf::PDFObjectReference(orphan)).isDictionary());

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, file, QStringLiteral("input.pdf"), false), Helper::createCustomWithoutCleanUp(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.documents.size(), size_t(1));
    const pdf::PDFDocument& output = *result.documents.front().document;

    // The file: no container left, written whole
    const QByteArray written = CompressTestHelper::write(output);
    QCOMPARE(result.data.value(QStringLiteral("sizeBefore")).toInteger(), qint64(file.size()));
    QCOMPARE(result.data.value(QStringLiteral("sizeAfter")).toInteger(), qint64(written.size()));
    QVERIFY(written.size() < file.size());
    QVERIFY(!written.contains("/ObjStm"));
    QVERIFY(!written.contains("/XRef"));
    QVERIFY(!written.contains("/Extends"));
    QVERIFY(!written.contains("/W ["));
    QCOMPARE(written.count("%%EOF"), qsizetype(1));

    // Every other object is the same, also the unreferenced one; the strict reader takes the file
    pdf::PDFDocumentReader::Result writtenResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = Helper::readStrict(written, QString(), &writtenResult);
    QCOMPARE(writtenResult, pdf::PDFDocumentReader::Result::OK);

    const pdf::PDFObjectStorage::PDFObjects& sourceObjects = source.getStorage().getObjects();
    const pdf::PDFObjectStorage::PDFObjects& reopenedObjects = reopened.getStorage().getObjects();
    QCOMPARE(reopenedObjects.size(), input.getStorage().getObjects().size());
    for (size_t i = 1; i < reopenedObjects.size(); ++i)
    {
        if (i < sourceObjects.size())
        {
            QVERIFY2(reopenedObjects[i].object == sourceObjects[i].object, qPrintable(QStringLiteral("object %1").arg(i)));
        }
        else
        {
            QVERIFY2(reopenedObjects[i].object.isNull(), qPrintable(QStringLiteral("container %1").arg(i)));
        }
    }

    // Every page draws the same: the red rectangle at 10 ... 60 of the page of 200 points,
    // which is at 5 ... 30 of the picture of 100 pixels; the page number moves it
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(Helper::PAGE_COUNT));
    for (size_t page = 0; page < size_t(Helper::PAGE_COUNT); ++page)
    {
        const CompressTestHelper::DrawnPage drawnInput = CompressTestHelper::draw(input, page);
        const CompressTestHelper::DrawnPage drawn = CompressTestHelper::draw(reopened, page);
        QVERIFY(drawn.errors.isEmpty());
        QCOMPARE(drawn.image, drawnInput.image);
        QCOMPARE(drawn.at(10, 10 + int(page) * 10), qRgb(255, 0, 0));
        QCOMPARE(drawn.at(50, 50), qRgb(255, 255, 255));
    }
}

void DocumentToolCompressTest::fileStructureUnusedCounter()
{
    using Helper = FileStructureTestHelper;

    // With the clean-up on the optimizer still removes the containers and says so
    // (Figma 303:4897): its counter does not change by the step behind it
    Helper::FileOptions options;
    options.objectStreamCount = 2;

    const pdf::PDFDocument source = Helper::createDocument();
    const QByteArray file = Helper::createFile(source, options);
    const pdf::PDFDocument input = Helper::readStrict(file);
    QVERIFY(input.getStorage().getObjects().size() > 0);

    pdf::PDFDocumentToolCompress::Settings settings;
    settings.images = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.images.enabled = false;
    settings.structure = pdf::PDFOptimizer::RemoveUnusedObjects;

    pdf::PDFDocumentToolCompress::Statistics statistics;
    const pdf::PDFDocument output = pdf::PDFDocumentToolCompress::compress(&input, settings, nullptr, pdf::PDFDocumentToolCompress::Progress(), &statistics);

    // The two object streams and the cross-reference stream: nothing else of the source is unused
    QCOMPARE(statistics.structure.unusedObjectsRemoved, qint64(3));
    QVERIFY(!CompressTestHelper::write(output).contains("/ObjStm"));
}

void DocumentToolCompressTest::fileStructureReferenced_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<bool>("isFirstObjectStreamKept");
    QTest::addColumn<bool>("isSecondObjectStreamKept");
    QTest::addColumn<bool>("isCrossReferenceStreamKept");

    // A hostile document references a container: it stays, nothing dangling is produced;
    // the second object stream /Extends the first one, which does not hold the second one
    QTest::newRow("catalog-to-object-stream") << QStringLiteral("catalog") << true << false << false;
    QTest::newRow("unreachable-object-to-object-stream") << QStringLiteral("orphan") << true << false << false;
    QTest::newRow("dictionary-of-a-stream-to-object-stream") << QStringLiteral("stream") << true << false << false;
    QTest::newRow("other-generation-to-object-stream") << QStringLiteral("generation") << true << false << false;
    QTest::newRow("catalog-to-cross-reference-stream") << QStringLiteral("xref") << false << false << true;

    // The kept container references another one by /Extends: it stays with it
    QTest::newRow("catalog-to-extending-object-stream") << QStringLiteral("extends") << true << true << false;
}

void DocumentToolCompressTest::fileStructureReferenced()
{
    using Helper = FileStructureTestHelper;

    QFETCH(QString, kind);
    QFETCH(bool, isFirstObjectStreamKept);
    QFETCH(bool, isSecondObjectStreamKept);
    QFETCH(bool, isCrossReferenceStreamKept);

    pdf::PDFDocument source = Helper::createDocument();

    // Numbers of the objects of the file: an optional object of the test, then the two
    // object streams (the second one /Extends the first one), then the cross-reference stream
    const bool hasOrphan = kind == QLatin1String("orphan") || kind == QLatin1String("stream");
    const pdf::PDFInteger firstObjectStream = pdf::PDFInteger(source.getStorage().getObjects().size()) + (hasOrphan ? 1 : 0);
    const pdf::PDFInteger secondObjectStream = firstObjectStream + 1;
    const pdf::PDFInteger crossReferenceStream = firstObjectStream + 2;

    if (kind == QLatin1String("catalog"))
    {
        source = Helper::withCatalogEntry(source, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(firstObjectStream, 0)));
    }
    else if (kind == QLatin1String("extends"))
    {
        source = Helper::withCatalogEntry(source, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(secondObjectStream, 0)));
    }
    else if (kind == QLatin1String("generation"))
    {
        source = Helper::withCatalogEntry(source, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(firstObjectStream, 5)));
    }
    else if (kind == QLatin1String("xref"))
    {
        source = Helper::withCatalogEntry(source, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(crossReferenceStream, 0)));
    }
    else if (kind == QLatin1String("stream"))
    {
        pdf::PDFObjectReference orphan;
        source = Helper::withObject(source, Helper::createStream({ { "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(firstObjectStream, 0)) } }), &orphan);
    }
    else
    {
        QCOMPARE(kind, QStringLiteral("orphan"));
        pdf::PDFObjectReference orphan;
        source = Helper::withObject(source, Helper::createDictionary("Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(firstObjectStream, 0))), &orphan);
    }

    Helper::FileOptions options;
    options.objectStreamCount = 2;
    const QByteArray file = Helper::createFile(source, options);
    const pdf::PDFDocument input = Helper::readStrict(file);
    QVERIFY(pdf::PDFObjectUtils::isObjectStream(input.getStorage().getObjects().at(size_t(secondObjectStream)).object));

    pdf::PDFDocumentToolCompress::Settings settings = pdf::PDFDocumentToolCompress::createSettings(Helper::createCustomWithoutCleanUp());
    const pdf::PDFDocument output = pdf::PDFDocumentToolCompress::compress(&input, settings, nullptr, pdf::PDFDocumentToolCompress::Progress(), nullptr);

    const QByteArray written = CompressTestHelper::write(output);
    pdf::PDFDocumentReader::Result writtenResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = Helper::readStrict(written, QString(), &writtenResult);
    QCOMPARE(writtenResult, pdf::PDFDocumentReader::Result::OK);

    const pdf::PDFObjectStorage::PDFObjects& inputObjects = input.getStorage().getObjects();
    const pdf::PDFObjectStorage::PDFObjects& reopenedObjects = reopened.getStorage().getObjects();
    QCOMPARE(reopenedObjects.at(size_t(firstObjectStream)).object == inputObjects.at(size_t(firstObjectStream)).object, isFirstObjectStreamKept);
    QCOMPARE(reopenedObjects.at(size_t(firstObjectStream)).object.isNull(), !isFirstObjectStreamKept);
    QCOMPARE(reopenedObjects.at(size_t(secondObjectStream)).object == inputObjects.at(size_t(secondObjectStream)).object, isSecondObjectStreamKept);
    QCOMPARE(reopenedObjects.at(size_t(secondObjectStream)).object.isNull(), !isSecondObjectStreamKept);
    QCOMPARE(reopenedObjects.at(size_t(crossReferenceStream)).object.isNull(), !isCrossReferenceStreamKept);
    QCOMPARE(written.contains("/ObjStm"), isFirstObjectStreamKept || isSecondObjectStreamKept);
    QCOMPARE(written.contains("/XRef"), isCrossReferenceStreamKept);

    // No reference, which led to an object, leads nowhere now
    std::set<pdf::PDFObjectReference> references = pdf::PDFObjectUtils::getReferences({ reopened.getStorage().getTrailerDictionary() }, reopened.getStorage());
    for (const pdf::PDFObjectStorage::Entry& entry : reopenedObjects)
    {
        references.merge(pdf::PDFObjectUtils::getDirectReferences(entry.object));
    }
    for (const pdf::PDFObjectReference& reference : references)
    {
        const bool isResolvedBefore = !input.getStorage().getObject(pdf::PDFObjectReference(reference)).isNull();
        const bool isResolved = !reopened.getStorage().getObject(pdf::PDFObjectReference(reference)).isNull();
        QVERIFY2(!isResolvedBefore || isResolved, qPrintable(QStringLiteral("reference %1 %2").arg(reference.objectNumber).arg(reference.generation)));
    }
}

void DocumentToolCompressTest::fileStructureReferencedByTrailer()
{
    using Helper = FileStructureTestHelper;

    // The trailer is the root of the references: a container named by any of its entries stays
    for (const char* key : { "Info", "Encrypt", "ID", "AnotherKey" })
    {
        pdf::PDFObjectStorage storage = Helper::createDocument().getStorage();
        const pdf::PDFObjectReference objectStream = storage.addObject(Helper::createStream({ { "Type", pdf::PDFObject::createName("ObjStm") }, { "N", pdf::PDFObject::createInteger(0) }, { "First", pdf::PDFObject::createInteger(0) } }));

        pdf::PDFDictionaryBuilder trailer(*storage.getTrailerDictionary().getDictionary());
        trailer.setEntry(pdf::PDFInplaceOrMemoryString(key), pdf::PDFObject::createReference(objectStream));
        storage.setTrailerDictionary(pdf::PDFObject::createDictionary(std::move(trailer)));

        QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(0));
        QVERIFY(storage.getObject(objectStream).isStream());
    }
}

void DocumentToolCompressTest::fileStructureLinearized_data()
{
    QTest::addColumn<bool>("hasOverflow");
    QTest::addColumn<bool>("isFlate");

    // PDF Reference 1.7, Table F.1: /H of two numbers, of four numbers with the overflow hint stream
    QTest::newRow("primary") << false << false;
    QTest::newRow("primary-flate") << false << true;
    QTest::newRow("overflow") << true << false;
}

void DocumentToolCompressTest::fileStructureLinearized()
{
    using Helper = FileStructureTestHelper;

    QFETCH(bool, hasOverflow);
    QFETCH(bool, isFlate);

    const pdf::PDFDocument source = Helper::createDocument();
    const size_t linearizationNumber = source.getStorage().getObjects().size();

    Helper::LinearizedOptions options;
    options.hasOverflow = hasOverflow;
    options.isFlate = isFlate;
    const QByteArray file = Helper::createLinearizedFile(source, options);

    // The input is a file of Appendix F: the dictionary at its beginning says the length of the file
    QVERIFY(file.left(256).contains("/Linearized"));
    QVERIFY(file.contains("/L " + QByteArray::number(file.size()).rightJustified(10, '0')));

    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument input = Helper::readStrict(file, QString(), &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);
    QVERIFY(input.getStorage().getObjects().at(linearizationNumber).object.isDictionary());
    QVERIFY(input.getStorage().getObjects().at(linearizationNumber + 1).object.isStream());
    QCOMPARE(input.getStorage().getObjects().size(), linearizationNumber + (hasOverflow ? 3 : 2));

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, file, QStringLiteral("input.pdf"), false), Helper::createCustomWithoutCleanUp(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const QByteArray written = CompressTestHelper::write(*result.documents.front().document);
    QVERIFY(!written.contains("/Linearized"));
    QVERIFY(!written.contains(Helper::HINT_MARKER));
    QVERIFY(!written.contains(Helper::OVERFLOW_MARKER));
    QVERIFY(!written.contains("/S 16"));

    // The hint tables are gone, the pages are not
    pdf::PDFDocumentReader::Result writtenResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = Helper::readStrict(written, QString(), &writtenResult);
    QCOMPARE(writtenResult, pdf::PDFDocumentReader::Result::OK);
    const pdf::PDFObjectStorage::PDFObjects& sourceObjects = source.getStorage().getObjects();
    for (size_t i = 1; i < sourceObjects.size(); ++i)
    {
        QVERIFY(reopened.getStorage().getObjects().at(i).object == sourceObjects[i].object);
    }
    QCOMPARE(CompressTestHelper::draw(reopened, 0).at(10, 10), qRgb(255, 0, 0));
}

void DocumentToolCompressTest::fileStructureLinearizedReferenced()
{
    using Helper = FileStructureTestHelper;

    // A hostile catalog references the hint stream: it stays, the dictionary goes
    {
        const pdf::PDFDocument plain = Helper::createDocument();
        const pdf::PDFInteger linearizationNumber = pdf::PDFInteger(plain.getStorage().getObjects().size());
        const pdf::PDFDocument source = Helper::withCatalogEntry(plain, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(linearizationNumber + 1, 0)));
        const pdf::PDFDocument input = Helper::readStrict(Helper::createLinearizedFile(source, Helper::LinearizedOptions()));

        pdf::PDFObjectStorage storage = input.getStorage();
        QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(1));
        QVERIFY(storage.getObjects().at(size_t(linearizationNumber)).object.isNull());
        QVERIFY(storage.getObjects().at(size_t(linearizationNumber + 1)).object.isStream());
    }

    // A hostile catalog references the dictionary: it stays with its old length of the file,
    // so the written file is not linearized for a reader (Table F.1, the entry L: "A mismatch
    // indicates that the file is not linearized"); the hint stream goes
    {
        const pdf::PDFDocument plain = Helper::createDocument();
        const pdf::PDFInteger linearizationNumber = pdf::PDFInteger(plain.getStorage().getObjects().size());
        const pdf::PDFDocument source = Helper::withCatalogEntry(plain, "Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(linearizationNumber, 0)));
        const QByteArray file = Helper::createLinearizedFile(source, Helper::LinearizedOptions());
        const pdf::PDFDocument input = Helper::readStrict(file);

        pdf::PDFObjectStorage storage = input.getStorage();
        QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(1));
        QVERIFY(storage.getObjects().at(size_t(linearizationNumber)).object.isDictionary());
        QVERIFY(storage.getObjects().at(size_t(linearizationNumber + 1)).object.isNull());

        const pdf::PDFDocument output = pdf::PDFDocumentTool::createWholeFileDocument(std::move(storage), &input);
        const QByteArray written = CompressTestHelper::write(output);
        QVERIFY(written.size() != file.size());
        QVERIFY(written.contains("/L " + QByteArray::number(file.size())));
    }

    // The overflow hint stream is told by the last number of /H: a stream of the plain
    // attributes of at most that length (F.2.5) goes, a longer one is another object
    {
        pdf::PDFObjectStorage storage = Helper::createDocument().getStorage();
        storage.addObject(Helper::createDictionaryOf({ { "Linearized", pdf::PDFObject::createReal(1.0) }, { "L", pdf::PDFObject::createInteger(1) }, { "H", Helper::createIntegers({ 1, 2, 3, 10 }) }, { "O", pdf::PDFObject::createInteger(1) }, { "E", pdf::PDFObject::createInteger(1) }, { "N", pdf::PDFObject::createInteger(1) }, { "T", pdf::PDFObject::createInteger(1) } }));

        const pdf::PDFObjectReference atTheLimit = storage.addObject(CompressTestHelper::createStreamObject(QByteArray(10, 'x')));
        const pdf::PDFObjectReference overTheLimit = storage.addObject(CompressTestHelper::createStreamObject(QByteArray(11, 'x')));

        QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(2));
        QVERIFY(storage.getObject(pdf::PDFObjectReference(atTheLimit)).isNull());
        QVERIFY(storage.getObject(pdf::PDFObjectReference(overTheLimit)).isStream());
    }
}

void DocumentToolCompressTest::fileStructureLookalikes_data()
{
    using Helper = FileStructureTestHelper;
    using Object = pdf::PDFObject;
    using Reference = pdf::PDFObjectReference;

    QTest::addColumn<pdf::PDFObject>("object");
    QTest::addColumn<bool>("isLinearized");
    QTest::addColumn<bool>("isRemoved");

    const Object integer = Object::createInteger(1);
    const Object name = Object::createName("FlateDecode");

    // Containers of the file structure identify themselves by the required entries of their tables
    QTest::newRow("object-stream") << Helper::createStream({ { "Type", Object::createName("ObjStm") }, { "N", integer }, { "First", integer } }) << false << true;
    QTest::newRow("object-stream-without-n") << Helper::createStream({ { "Type", Object::createName("ObjStm") }, { "First", integer } }) << false << false;
    QTest::newRow("object-stream-without-first") << Helper::createStream({ { "Type", Object::createName("ObjStm") }, { "N", integer } }) << false << false;
    QTest::newRow("object-stream-type-string") << Helper::createStream({ { "Type", Object::createString("ObjStm") }, { "N", integer }, { "First", integer } }) << false << false;
    QTest::newRow("object-stream-dictionary") << Helper::createDictionaryOf({ { "Type", Object::createName("ObjStm") }, { "N", integer }, { "First", integer } }) << false << false;
    QTest::newRow("xref-stream") << Helper::createStream({ { "Type", Object::createName("XRef") }, { "Size", integer }, { "W", Helper::createIntegers({ 1, 2, 1 }) } }) << false << true;
    QTest::newRow("xref-stream-without-size") << Helper::createStream({ { "Type", Object::createName("XRef") }, { "W", Helper::createIntegers({ 1, 2, 1 }) } }) << false << false;
    QTest::newRow("xref-stream-without-w") << Helper::createStream({ { "Type", Object::createName("XRef") }, { "Size", integer } }) << false << false;
    QTest::newRow("xref-dictionary") << Helper::createDictionaryOf({ { "Type", Object::createName("XRef") }, { "Size", integer }, { "W", Helper::createIntegers({ 1, 2, 1 }) } }) << false << false;

    // Linearization parameter dictionary: Table F.1
    const auto createLinearization = [integer](Object version, Object hints)
    {
        return Helper::createDictionaryOf({ { "Linearized", std::move(version) }, { "L", integer }, { "H", std::move(hints) }, { "O", integer }, { "E", integer }, { "N", integer }, { "T", integer } });
    };
    QTest::newRow("linearization") << createLinearization(Object::createReal(1.0), Helper::createIntegers({ 1, 2 })) << false << true;
    QTest::newRow("linearization-integer-version") << createLinearization(integer, Helper::createIntegers({ 1, 2, 3, 4 })) << false << true;
    QTest::newRow("linearization-name-version") << createLinearization(name, Helper::createIntegers({ 1, 2 })) << false << false;
    QTest::newRow("linearization-three-hints") << createLinearization(integer, Helper::createIntegers({ 1, 2, 3 })) << false << false;
    QTest::newRow("linearization-empty-hints") << createLinearization(integer, Helper::createIntegers({ })) << false << false;
    QTest::newRow("linearization-real-hints") << createLinearization(integer, CompressTestHelper::createNumberArray({ 1.0, 2.0 })) << false << false;
    QTest::newRow("linearization-without-length") << Helper::createDictionaryOf({ { "Linearized", integer }, { "H", Helper::createIntegers({ 1, 2 }) }, { "O", integer }, { "E", integer }, { "N", integer }, { "T", integer } }) << false << false;
    QTest::newRow("linearization-stream") << Helper::createStream({ { "Linearized", integer }, { "L", integer }, { "H", Helper::createIntegers({ 1, 2 }) }, { "O", integer }, { "E", integer }, { "N", integer }, { "T", integer } }) << false << false;

    // Hint streams: F.2.5, Table F.2 - only in a linearized file, a stream without a type,
    // direct entries, the required position S, positions of further tables
    const Object position = Object::createInteger(16);
    const Object reference = Object::createReference(Reference(1, 0));
    QTest::newRow("hint-of-linearized-file") << Helper::createStream({ { "S", position } }) << true << true;
    QTest::newRow("hint-with-further-tables") << Helper::createStream({ { "S", position }, { "O", position }, { "Future", position }, { "Filter", name } }) << true << true;
    QTest::newRow("hint-indirect-length") << Helper::createStream({ { "S", position }, { "Length", reference } }) << true << true;
    QTest::newRow("hint-of-other-file") << Helper::createStream({ { "S", position } }) << false << false;
    QTest::newRow("hint-with-type") << Helper::createStream({ { "Type", Object::createName("Hint") }, { "S", position } }) << true << false;
    QTest::newRow("hint-with-subtype") << Helper::createStream({ { "Subtype", Object::createName("Image") }, { "S", position } }) << true << false;
    QTest::newRow("hint-position-reference") << Helper::createStream({ { "S", reference } }) << true << false;
    QTest::newRow("hint-position-name") << Helper::createStream({ { "S", name } }) << true << false;
    QTest::newRow("hint-without-position") << Helper::createStream({ { "T", position } }) << true << false;
    QTest::newRow("hint-other-entry-name") << Helper::createStream({ { "S", position }, { "Note", name } }) << true << false;
    QTest::newRow("hint-filter-reference") << Helper::createStream({ { "S", position }, { "Filter", reference } }) << true << false;
    QTest::newRow("hint-length-name") << Helper::createStream({ { "S", position }, { "Length", name } }) << true << false;
    QTest::newRow("hint-dictionary") << Helper::createDictionaryOf({ { "S", position } }) << true << false;
    QTest::newRow("plain-stream-of-linearized-file") << Helper::createStream({ }) << true << false;
}

void DocumentToolCompressTest::fileStructureLookalikes()
{
    using Helper = FileStructureTestHelper;

    QFETCH(pdf::PDFObject, object);
    QFETCH(bool, isLinearized);
    QFETCH(bool, isRemoved);

    pdf::PDFObjectStorage storage = Helper::createDocument().getStorage();
    qint64 expectedCount = isRemoved ? 1 : 0;

    if (isLinearized)
    {
        // The dictionary of a linearized file goes as well
        storage.addObject(Helper::createDictionaryOf({ { "Linearized", pdf::PDFObject::createReal(1.0) }, { "L", pdf::PDFObject::createInteger(1) }, { "H", Helper::createIntegers({ 1, 2 }) }, { "O", pdf::PDFObject::createInteger(1) }, { "E", pdf::PDFObject::createInteger(1) }, { "N", pdf::PDFObject::createInteger(1) }, { "T", pdf::PDFObject::createInteger(1) } }));
        ++expectedCount;
    }

    const pdf::PDFObjectReference reference = storage.addObject(object);
    const pdf::PDFObjectStorage::PDFObjects before = storage.getObjects();

    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), expectedCount);
    QCOMPARE(storage.getObject(pdf::PDFObjectReference(reference)).isNull(), isRemoved);

    // Nothing else changes, the numbers of the objects stay
    QCOMPARE(storage.getObjects().size(), before.size());
    for (size_t i = 0; i < before.size() - (isLinearized ? 2 : 1); ++i)
    {
        QVERIFY(storage.getObjects().at(i).object == before.at(i).object);
    }
}

void DocumentToolCompressTest::fileStructureEncrypted()
{
    using Helper = FileStructureTestHelper;

    const QString password = QStringLiteral("user-secret");
    const QByteArray secret("TEXT-OF-THE-TITLE");

    // A reference leading nowhere makes the tool read the object stream, which is
    // encrypted: all of its objects are unpacked, so it goes
    pdf::PDFDocument plain = Helper::withCatalogEntry(Helper::createDocument(), "Note", pdf::PDFObject::createString(secret));
    plain = Helper::withCatalogEntry(plain, "Dangling", pdf::PDFObject::createReference(pdf::PDFObjectReference(5000, 0)));
    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = password;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu;
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocument built = builder.build();

    Helper::FileOptions options;
    options.paddingSize = Helper::PADDING_SIZE;
    const QByteArray file = Helper::createFile(built, options);
    QVERIFY(!file.contains(secret));

    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument input = Helper::readStrict(file, password, &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(input.getCatalog()->getPageCount(), size_t(Helper::PAGE_COUNT));

    pdf::PDFDocumentToolCompress tool;
    const pdf::PDFDocumentToolResult result = tool.run(CompressTestHelper::createContext(input, file, QStringLiteral("input.pdf"), false), Helper::createCustomWithoutCleanUp(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    // The file has the encryption dictionary, no container and no plain text
    const QByteArray written = CompressTestHelper::write(*result.documents.front().document);
    QVERIFY(written.contains("/Encrypt"));
    QVERIFY(!written.contains("/ObjStm"));
    QVERIFY(!written.contains("/XRef"));
    QVERIFY(!written.contains(secret));

    // It asks for the password and opens with it
    pdf::PDFDocumentReader::Result withoutPassword = pdf::PDFDocumentReader::Result::OK;
    Helper::readStrict(written, QString(), &withoutPassword);
    QVERIFY(withoutPassword != pdf::PDFDocumentReader::Result::OK);

    pdf::PDFDocumentReader::Result withPassword = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = Helper::readStrict(written, password, &withPassword);
    QCOMPARE(withPassword, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(reopened.getStorage().getSecurityHandler()->getMode(), input.getStorage().getSecurityHandler()->getMode());

    const pdf::PDFObject rootObject = reopened.getTrailerDictionary()->get("Root");
    QCOMPARE(reopened.getObject(rootObject).getDictionary()->get("Note").getString(), secret);
    QCOMPARE(CompressTestHelper::draw(reopened, 0).at(10, 10), qRgb(255, 0, 0));
}

void DocumentToolCompressTest::fileStructureIncrementalUpdate()
{
    using Helper = FileStructureTestHelper;

    // PDF Reference 1.7, 3.4.7: the sections of the original still lead into its object
    // streams. A document without its containers, updated incrementally, drops nothing.
    const pdf::PDFDocument source = Helper::createDocument();
    const QByteArray file = Helper::createFile(source, Helper::FileOptions());
    const pdf::PDFDocument input = Helper::readStrict(file);

    pdf::PDFObjectStorage storage = input.getStorage();
    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(2));
    const pdf::PDFDocument withoutContainers = pdf::PDFDocumentTool::createWholeFileDocument(std::move(storage), &input);

    QByteArray updated;
    {
        QBuffer buffer(&updated);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.writeIncrementalUpdate(&buffer, file, &withoutContainers)));
    }
    QVERIFY(updated.startsWith(file));

    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = Helper::readStrict(updated, QString(), &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);

    // Every object of the source is still in the file and the object stream too
    const pdf::PDFObjectStorage::PDFObjects& sourceObjects = source.getStorage().getObjects();
    for (size_t i = 1; i < sourceObjects.size(); ++i)
    {
        QVERIFY(reopened.getStorage().getObjects().at(i).object == sourceObjects[i].object);
    }
    QVERIFY(pdf::PDFObjectUtils::isObjectStream(reopened.getStorage().getObjects().at(sourceObjects.size()).object));

    // Another object under the number of the object stream would take the objects in
    // it away from the file: the update is refused, nothing is written
    pdf::PDFObjectStorage renumberedStorage = input.getStorage();
    pdf::PDFObjectStorage::PDFObjects renumberedObjects = renumberedStorage.getObjects();
    renumberedObjects[sourceObjects.size()].object = Helper::createDictionary("Moved", pdf::PDFObject::createInteger(1));
    renumberedStorage.setObjects(std::move(renumberedObjects));
    const pdf::PDFDocument renumbered(std::move(renumberedStorage), input.getInfo()->version, QByteArray());

    QByteArray refused;
    {
        QBuffer buffer(&refused);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(!bool(writer.writeIncrementalUpdate(&buffer, file, &renumbered)));
    }
    QVERIFY(refused.isEmpty());
}

void DocumentToolCompressTest::fileStructureDamagedRead()
{
    using Helper = FileStructureTestHelper;

    // A damaged file (the offset of startxref leads nowhere) with a classic trailer: the
    // permissive reader rebuilds the objects by their "obj" marks and does not unpack the object
    // stream. The annotation of the page is only in it (PDF Reference 1.7, 3.4.6), so the stream
    // is the only copy of it and the tool must not remove it from the file.
    const QByteArray annotation("<< /Type /Annot /Subtype /Text /Rect [ 0 0 10 10 ] /Contents (ONLY-COPY-OF-THE-NOTE) >>");
    const QByteArray pairs("5 0 ");
    const QByteArray content = pairs + annotation;

    QByteArray file("%PDF-1.5\r\n");
    file += "1 0 obj\r\n<< /Type /Catalog /Pages 2 0 R >>\r\nendobj\r\n";
    file += "2 0 obj\r\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\r\nendobj\r\n";
    file += "3 0 obj\r\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 200 200 ] /Annots [ 5 0 R ] >>\r\nendobj\r\n";
    file += "4 0 obj\r\n<< /Type /ObjStm /N 1 /First " + QByteArray::number(pairs.size()) + " /Length " + QByteArray::number(content.size()) + " >>\r\nstream\r\n" + content + "\r\nendstream\r\nendobj\r\n";
    file += "6 0 obj\r\n<< /Unrelated 1 >>\r\nendobj\r\n";
    file += "trailer\r\n<< /Size 7 /Root 1 0 R >>\r\nstartxref\r\n999999\r\n%%EOF\r\n";

    pdf::PDFDocumentReader strict(nullptr, nullptr, false, false);
    strict.readFromBuffer(file);
    QVERIFY(strict.getReadingResult() != pdf::PDFDocumentReader::Result::OK);

    pdf::PDFDocumentReader permissive(nullptr, nullptr, true, false);
    const pdf::PDFDocument input = permissive.readFromBuffer(file);
    QCOMPARE(permissive.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(input.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(pdf::PDFObjectUtils::isObjectStream(input.getStorage().getObject(pdf::PDFObjectReference(4, 0))));
    QVERIFY(input.getStorage().getObject(pdf::PDFObjectReference(5, 0)).isNull());
    QCOMPARE(input.getStorage().getObjects().size(), size_t(7));

    pdf::PDFDocumentToolCompress::Settings settings = pdf::PDFDocumentToolCompress::createSettings(Helper::createCustomWithoutCleanUp());
    const pdf::PDFDocument output = pdf::PDFDocumentToolCompress::compress(&input, settings, nullptr, pdf::PDFDocumentToolCompress::Progress(), nullptr);
    const QByteArray written = CompressTestHelper::write(output);
    QVERIFY(written.contains("/ObjStm"));
    QVERIFY(written.contains("ONLY-COPY-OF-THE-NOTE"));

    // A file with cross-reference streams only has no trailer keyword: the permissive reader
    // finds no catalog, so its object streams are never the only copy in an opened document
    const QByteArray withoutTrailer = Helper::createFile(Helper::createDocument(), Helper::FileOptions()).replace("startxref\r\n", "startxref\r\n9");
    pdf::PDFDocumentReader permissiveWithoutTrailer(nullptr, nullptr, true, false);
    permissiveWithoutTrailer.readFromBuffer(withoutTrailer);
    QCOMPARE(permissiveWithoutTrailer.getReadingResult(), pdf::PDFDocumentReader::Result::Failed);
}

void DocumentToolCompressTest::fileStructureMissingObjects_data()
{
    QTest::addColumn<QByteArray>("stream");
    QTest::addColumn<bool>("hasMissingReference");
    QTest::addColumn<bool>("isKept");

    // Table 3.14: N pairs "object number, offset"; object 1 is the catalog of the storage
    const QByteArray present = "<< /Type /ObjStm /N 1 /First 4 /Length 8 >>|1 0 null";
    const QByteArray missing = "<< /Type /ObjStm /N 1 /First 7 /Length 11 >>|900 0  null";

    // An object the storage has: the object stream was unpacked, also with a reference
    // of the document leading nowhere (real files have them)
    QTest::newRow("object-present") << present << false << false;
    QTest::newRow("object-present-dangling-reference") << present << true << false;

    // The object is not in the storage: only with a reference leading to a missing object
    // it may be needed, a file of unreferenced missing objects has no use for them
    QTest::newRow("object-missing-not-referenced") << missing << false << false;
    QTest::newRow("object-missing-referenced") << missing << true << true;

    // Something, which the tool cannot read, may hold anything
    QTest::newRow("too-many-objects") << QByteArray("<< /Type /ObjStm /N 100 /First 4 /Length 8 >>|1 0 null") << true << true;
    QTest::newRow("first-beyond-the-stream") << QByteArray("<< /Type /ObjStm /N 1 /First 400 /Length 8 >>|1 0 null") << true << true;
    QTest::newRow("header-of-other-values") << QByteArray("<< /Type /ObjStm /N 1 /First 4 /Length 8 >>|/A /B null") << true << true;
    QTest::newRow("undecodable") << QByteArray("<< /Type /ObjStm /N 1 /First 4 /Filter /FlateDecode /DecodeParms << /Predictor 99 >> /Length 8 >>|1 0 null") << true << true;
}

void DocumentToolCompressTest::fileStructureMissingObjects()
{
    using Helper = FileStructureTestHelper;

    QFETCH(QByteArray, stream);
    QFETCH(bool, hasMissingReference);
    QFETCH(bool, isKept);

    const QList<QByteArray> parts = stream.split('|');
    QCOMPARE(parts.size(), qsizetype(2));

    pdf::PDFParsingContext context([](pdf::PDFParsingContext*, pdf::PDFObjectReference) { return pdf::PDFObject(); });
    pdf::PDFParser parser(parts[0], &context, pdf::PDFParser::None);
    pdf::PDFDictionaryBuilder dictionary(*parser.getObject().getDictionary());

    pdf::PDFObjectStorage storage = Helper::createDocument().getStorage();
    QVERIFY(!storage.getObject(pdf::PDFObjectReference(1, 0)).isNull());
    const pdf::PDFObjectReference container = storage.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(parts[1]))));
    if (hasMissingReference)
    {
        storage.addObject(Helper::createDictionary("Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(1000, 0))));
    }

    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(isKept ? 0 : 1));
    QCOMPARE(storage.getObject(container).isStream(), isKept);
}

void DocumentToolCompressTest::fileStructureMissingObjectsExtends()
{
    using Helper = FileStructureTestHelper;

    // A kept object stream counts as an object, which stays: its /Extends keeps the other one,
    // whose objects are all present
    pdf::PDFObjectStorage storage = Helper::createDocument().getStorage();
    const pdf::PDFObjectReference extended = storage.addObject(pdf::PDFObject::createStream(pdf::PDFStream(
        pdf::PDFDictionaryBuilder(*Helper::createDictionaryOf({ { "Type", pdf::PDFObject::createName("ObjStm") }, { "N", pdf::PDFObject::createInteger(1) }, { "First", pdf::PDFObject::createInteger(4) }, { "Length", pdf::PDFObject::createInteger(8) } }).getDictionary()),
        QByteArray("1 0 null"))));
    const pdf::PDFObjectReference kept = storage.addObject(pdf::PDFObject::createStream(pdf::PDFStream(
        pdf::PDFDictionaryBuilder(*Helper::createDictionaryOf({ { "Type", pdf::PDFObject::createName("ObjStm") }, { "N", pdf::PDFObject::createInteger(1) }, { "First", pdf::PDFObject::createInteger(7) }, { "Length", pdf::PDFObject::createInteger(11) }, { "Extends", pdf::PDFObject::createReference(extended) } }).getDictionary()),
        QByteArray("900 0  null"))));
    storage.addObject(Helper::createDictionary("Hostile", pdf::PDFObject::createReference(pdf::PDFObjectReference(900, 0))));

    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(storage), qint64(0));
    QVERIFY(storage.getObject(kept).isStream());
    QVERIFY(storage.getObject(extended).isStream());
}

void DocumentToolCompressTest::fileStructureHybrid()
{
    using Helper = FileStructureTestHelper;

    // PDF Reference 1.7, 3.4.7 "Compatibility with Applications That Do Not Support PDF 1.5":
    // the classic table of a hybrid-reference file has the object in the object stream free, only
    // the cross-reference stream of /XRefStm leads to it. The reader of a valid file unpacks it
    // (a reference of the page leads to it), so the container is dead here too.
    const QByteArray annotation("<< /Type /Annot /Subtype /Text /Rect [ 0 0 10 10 ] /Contents (HIDDEN-OBJECT-OF-THE-FILE) >>");
    const QByteArray pairs("6 0 ");
    const QByteArray content = pairs + annotation;

    QByteArray file("%PDF-1.5\r\n");
    std::array<qint64, 8> offsets = { };
    const auto addObject = [&](int number, const QByteArray& body)
    {
        offsets[size_t(number)] = file.size();
        file += QByteArray::number(number) + " 0 obj\r\n" + body + "\r\nendobj\r\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 200 200 ] /Annots [ 6 0 R ] >>");
    addObject(5, "<< /Type /ObjStm /N 1 /First " + QByteArray::number(pairs.size()) + " /Length " + QByteArray::number(content.size()) + " >>\r\nstream\r\n" + content + "\r\nendstream");

    // Table 3.16: the object 6 is the first one in the object stream 5
    QByteArray entries;
    entries.append(char(2));
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        entries.append(char((5 >> shift) & 0xFF));
    }
    entries.append(char(0));
    entries.append(char(0));
    const qint64 xrefStream = file.size();
    addObject(7, "<< /Type /XRef /Size 8 /Index [ 6 1 ] /W [ 1 4 2 ] /Length " + QByteArray::number(entries.size()) + " >>\r\nstream\r\n" + entries + "\r\nendstream");

    const qint64 table = file.size();
    file += "xref\r\n0 8\r\n";
    file += Helper::createTableEntry(0, 65535, 'f');
    for (int number = 1; number < 8; ++number)
    {
        const bool isFree = number == 4 || number == 6;
        file += Helper::createTableEntry(isFree ? 0 : offsets[size_t(number)], 0, isFree ? 'f' : 'n');
    }
    file += "trailer\r\n<< /Size 8 /Root 1 0 R /XRefStm " + QByteArray::number(xrefStream) + " >>\r\nstartxref\r\n" + QByteArray::number(table) + "\r\n%%EOF\r\n";

    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument input = Helper::readStrict(file, QString(), &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);
    QVERIFY(input.getStorage().getObject(pdf::PDFObjectReference(6, 0)).isDictionary());

    pdf::PDFDocumentToolCompress::Settings settings = pdf::PDFDocumentToolCompress::createSettings(Helper::createCustomWithoutCleanUp());
    const pdf::PDFDocument output = pdf::PDFDocumentToolCompress::compress(&input, settings, nullptr, pdf::PDFDocumentToolCompress::Progress(), nullptr);
    const QByteArray written = CompressTestHelper::write(output);
    QVERIFY(!written.contains("/ObjStm"));
    QVERIFY(!written.contains("/XRef"));
    QVERIFY(written.contains("HIDDEN-OBJECT-OF-THE-FILE"));

    const pdf::PDFDocument reopened = Helper::readStrict(written, QString(), &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);
    QVERIFY(reopened.getStorage().getObject(pdf::PDFObjectReference(6, 0)) == input.getStorage().getObject(pdf::PDFObjectReference(6, 0)));
}

void DocumentToolCompressTest::fileStructureExtendsChain()
{
    using Helper = FileStructureTestHelper;

    // PDF Reference 1.7, Table 3.14: a chain of /Extends of a hostile size; a reference to the last
    // one keeps all of them, without it none. The work is linear (the limit is far above a
    // linear pass, a quadratic walk over the chain would need minutes).
    constexpr int CHAIN_LENGTH = 100000;
    pdf::PDFObjectStorage base = Helper::createDocument().getStorage();
    pdf::PDFObjectReference previous;
    for (int i = 0; i < CHAIN_LENGTH; ++i)
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("ObjStm"));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("N"), pdf::PDFObject::createInteger(0));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("First"), pdf::PDFObject::createInteger(0));
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(0));
        if (previous.isValid())
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Extends"), pdf::PDFObject::createReference(previous));
        }
        previous = base.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray())));
    }

    pdf::PDFObjectStorage withoutReference = base;
    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(withoutReference), qint64(CHAIN_LENGTH));

    pdf::PDFObjectStorage withReference = base;
    withReference.addObject(Helper::createDictionary("Hostile", pdf::PDFObject::createReference(previous)));

    QElapsedTimer timer;
    timer.start();
    QCOMPARE(pdf::PDFObjectUtils::removeFileStructureObjects(withReference), qint64(0));
    const int factor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(timer.elapsed() < 20000 * factor, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(withReference.getObject(previous).isStream());
}

void DocumentToolCompressTest::fileStructureOldCallers()
{
    using Helper = FileStructureTestHelper;

    // The old applications write what they have read: the writer and the optimizer
    // without its steps drop nothing by themselves
    Helper::FileOptions options;
    options.paddingSize = Helper::PADDING_SIZE;
    const QByteArray file = Helper::createFile(Helper::createDocument(), options);
    const pdf::PDFDocument input = Helper::readStrict(file);

    const QByteArray written = CompressTestHelper::write(input);
    QVERIFY(written.contains("/ObjStm"));
    QVERIFY(written.contains("/XRef"));
    QVERIFY(written.contains(QByteArray(Helper::PADDING_SIZE, ' ')));

    pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::MergeIdenticalObjects | pdf::PDFOptimizer::RemoveNullObjects), nullptr);
    optimizer.setDocument(&input);
    optimizer.optimize();
    const pdf::PDFDocument optimized = optimizer.takeOptimizedDocument();
    QVERIFY(CompressTestHelper::write(optimized).contains("/ObjStm"));
}

QTEST_MAIN(DocumentToolCompressTest)

#include "tst_documenttoolcompresstest.moc"
