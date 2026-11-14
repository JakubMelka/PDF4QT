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

// The MCP actions of the exports of step 4 of phase 2 over HTTP - convert.pdf-to-images,
// convert.extract-images, convert.audiobook - and document.list_images (the integration; plan 7
// and 8 row MCP, R4-1, R4-8, R4-11, R4-21, Q4.2 of 14. 11.: WAV only): the schema from the
// definitions, result by the catalog, save_files into a missing folder refused, the files of the
// folder, the ids of list_images in exclude, the refusal without bit 5 (ISO 32000-2, Table 22).

#include "appmcpstep4.h"

#include "pdfappbuiltintools.h"
#include "pdfappcontext.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcppermissions.h"
#include "pdfspeechsynthesizer.h"

#include <QApplication>
#include <QImage>
#include <QtEndian>
#include <QtTest>

#include <thread>

using pdftest::AppMcpFixture;
using pdftest::AppMcpImageDocument;
using pdftest::AppMcpStep4;

/// The fake engine: 16 bit mono at 16 kHz, 20 bytes per character; no state (thread-safe). The
/// tests never use the voices of the system.
class McpFakeSynthesizer : public pdf::PDFSpeechSynthesizer
{
public:
    virtual bool setVoice(const QString&, QString*) override { return true; }
    virtual void setRate(double) override { }
    virtual void setVolume(double) override { }
    virtual bool synthesize(const QString& text, pdf::PDFAudioSink* sink, const pdf::PDFOperationControl*, QString*) override
    {
        const pdf::PDFAudioFormat format{ 16000, 1, pdf::PDFAudioFormat::SampleFormat::Int16 };
        return sink->writeAudio(format, QByteArray(int(text.size()) * 20, '\x10'));
    }
};

class McpFakeSynthesizerFactory : public pdf::PDFSpeechSynthesizerFactory
{
public:
    virtual bool isAvailable() const override { return true; }
    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override
    {
        return { { QStringLiteral("Fake Voice"), QStringLiteral("en-US"), pdf::PDFSpeechVoice::Gender::Female, pdf::PDFSpeechVoice::Age::Adult } };
    }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override { return std::make_unique<McpFakeSynthesizer>(); }
};

class AppMcpConvertTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void schemaFromDefinitions_data();
    void schemaFromDefinitions();
    void saveFilesIntoMissingFolderRefused();
    void pdfToImagesWritesEveryPage();
    void refusedWithoutCopy();
    void listImages();
    void imageListCache();
    void extractImagesExcludeById();
    void audioBookSaveFile();

private:
    /// Page 1: the color image A and the gray image B; page 2: A again and the color image C drawn
    /// only inside the layer "Hidden", which /D switches off (ISO 32000-2, 8.11.4.3)
    static QString writeLayeredImages(AppMcpFixture& fixture, pdf::PDFObjectReference* a, pdf::PDFObjectReference* b, pdf::PDFObjectReference* c, uint32_t permissions = 0);

    static bool start(AppMcpFixture& fixture);
};

void AppMcpConvertTest::initTestCase()
{
    pdf::PDFAppBuiltinTools::registerAll();
}

bool AppMcpConvertTest::start(AppMcpFixture& fixture)
{
    if (!fixture.setPermissions(pdf::PDFAppMcpPermission::Allow) || !fixture.startServer())
    {
        return false;
    }
    fixture.setTransport(pdftest::AppMcpTransport::Http);
    return true;
}

QString AppMcpConvertTest::writeLayeredImages(AppMcpFixture& fixture, pdf::PDFObjectReference* a, pdf::PDFObjectReference* b, pdf::PDFObjectReference* c, uint32_t permissions)
{
    AppMcpImageDocument document;
    const pdf::PDFObjectReference layer = document.builder.addObject(AppMcpImageDocument::parse("<< /Type /OCG /Name (Hidden) >>"));
    document.builder.mergeTo(document.builder.getCatalogReference(),
                             AppMcpImageDocument::parse("<< /OCProperties << /OCGs [" + AppMcpImageDocument::ref(layer) + "] /D << /OFF [" + AppMcpImageDocument::ref(layer) + "] >> >> >>"));
    *a = document.addColorImage(40, 20);
    *b = document.addGrayImage();
    *c = document.addColorImage(30, 30);

    // A is drawn 80 x 40 pt: 40 pixels / (80 / 72) in = 36 dpi on both axes (8.9.4)
    document.addImagePage({ { *a, "80 0 0 40 20 20" }, { *b, "64 0 0 64 20 200" } });
    document.addPage("<< /XObject << /ImA " + AppMcpImageDocument::ref(*a) + " /ImC " + AppMcpImageDocument::ref(*c) + " >> /Properties << /oc1 " + AppMcpImageDocument::ref(layer) + " >> >>",
                     "q 80 0 0 40 20 20 cm /ImA Do Q /OC /oc1 BDC q 60 0 0 60 100 100 cm /ImC Do Q EMC");
    if (permissions)
    {
        document.encrypt(permissions);
    }
    return document.write(fixture.path(QStringLiteral("Catalog.pdf")));
}

void AppMcpConvertTest::schemaFromDefinitions_data()
{
    // The rows of the catalog: the result kind Files; a tool, which writes its files (folder and
    // fileName), has save_files only; Create Audio Book writes one WAV file (Q4.2 of 14. 11.), which
    // is no text - save_file only
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QStringList>("resultValues");
    QTest::addColumn<QString>("defaultResult");
    QTest::newRow("pdf-to-images") << QStringLiteral("convert.pdf-to-images") << QStringList({ QStringLiteral("save_files") }) << QStringLiteral("save_files");
    QTest::newRow("extract-images") << QStringLiteral("convert.extract-images") << QStringList({ QStringLiteral("save_files") }) << QStringLiteral("save_files");
    QTest::newRow("audiobook") << QStringLiteral("convert.audiobook") << QStringList({ QStringLiteral("save_file") }) << QStringLiteral("save_file");
}

void AppMcpConvertTest::schemaFromDefinitions()
{
    QFETCH(QString, toolId);
    QFETCH(QStringList, resultValues);
    QFETCH(QString, defaultResult);

    const QJsonObject action = AppMcpStep4::actionSchema(toolId);
    QVERIFY2(!action.isEmpty(), qPrintable(toolId));
    const QJsonObject properties = action.value(QStringLiteral("properties")).toObject();
    const QStringList parameters = AppMcpStep4::getAssistantParameters(toolId);
    QVERIFY(!parameters.isEmpty());
    for (const QString& parameter : parameters)
    {
        QVERIFY2(properties.contains(parameter), qPrintable(toolId + QStringLiteral(": ") + parameter));
    }
    QCOMPARE(properties.size(), parameters.size());

    const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    QVERIFY(tool);
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*tool->getInfo(), tool.get()), resultValues);
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*tool->getInfo()), defaultResult);

    if (properties.contains(QStringLiteral("existingFiles")))
    {
        // R17: an assistant is never asked - no value ask, also not in the description
        const QJsonObject existing = properties.value(QStringLiteral("existingFiles")).toObject();
        QVERIFY(!existing.value(QStringLiteral("enum")).toArray().contains(QStringLiteral("ask")));
        QVERIFY2(!existing.value(QStringLiteral("description")).toString().contains(QStringLiteral("- ask:")), qPrintable(existing.value(QStringLiteral("description")).toString()));
    }
    if (toolId == QLatin1String("convert.audiobook"))
    {
        // Q4.2 of 14. 11.: WAV only
        QCOMPARE(properties.value(QStringLiteral("format")).toObject().value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("wav") }));
    }
}

void AppMcpConvertTest::saveFilesIntoMissingFolderRefused()
{
    // R17: the folder of save_files must exist; nothing is written, the folder is not created
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab);
    const QString missing = QDir(fixture.folder()).filePath(QStringLiteral("missing/images"));
    for (const QString& action : { QStringLiteral("pdf-to-images"), QStringLiteral("extract-images") })
    {
        const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), action, QJsonObject({ { QStringLiteral("folder"), missing } }),
                                                                              QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
        QVERIFY(result);
        QVERIFY2(result->isError, qPrintable(action));
    }
    QVERIFY(!QFileInfo::exists(QDir(fixture.folder()).filePath(QStringLiteral("missing"))));
}

void AppMcpConvertTest::pdfToImagesWritesEveryPage()
{
    // Plan 4.4, B57: one file per page by "{source_base}_@" (@ = the page number), the size from the
    // crop box at the resolution: 300 x 400 pt at 144 dpi = 600 x 800 pixels
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 3));
    QVERIFY(tab);
    const QString folder = QDir(fixture.folder()).filePath(QStringLiteral("pages"));
    QVERIFY(QDir().mkpath(folder));
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), QStringLiteral("pdf-to-images"),
                                                                          QJsonObject({ { QStringLiteral("folder"), folder }, { QStringLiteral("dpi"), 144 } }),
                                                                          QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(QDir(folder).entryList(QDir::Files).size(), 3);
    for (int page = 1; page <= 3; ++page)
    {
        const QImage image(QDir(folder).filePath(QStringLiteral("Report_%1.png").arg(page)));
        QVERIFY2(!image.isNull(), qPrintable(QString::number(page)));
        QCOMPARE(image.size(), QSize(600, 800));
    }
}

void AppMcpConvertTest::refusedWithoutCopy()
{
    // Q4.5, ISO 32000-2, Table 22: the three exports need bit 5 (copy and extract); without it
    // nothing is written
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    fixture.context->setSpeechSynthesizerFactory(std::make_shared<McpFakeSynthesizerFactory>());
    pdf::PDFObjectReference a;
    pdf::PDFObjectReference b;
    pdf::PDFObjectReference c;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeLayeredImages(fixture, &a, &b, &c, AppMcpStep4::allowedWithout(5)));
    QVERIFY(tab);
    const QString folder = QDir(fixture.folder()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(folder));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() } });
    for (const QString& action : { QStringLiteral("pdf-to-images"), QStringLiteral("extract-images") })
    {
        const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), action, QJsonObject({ { QStringLiteral("folder"), folder } }), common);
        QVERIFY(result);
        QVERIFY2(result->isError, qPrintable(action));
    }
    QJsonObject audio = common;
    audio.insert(QStringLiteral("result"), QStringLiteral("save_file"));
    audio.insert(QStringLiteral("path"), QDir(folder).filePath(QStringLiteral("Catalog.wav")));
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), QStringLiteral("audiobook"), QJsonObject(), audio);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());
}

void AppMcpConvertTest::listImages()
{
    // Plan 7, R4-8, R4-11: the ids of the images ("<object> <generation>"), the number N of the
    // whole list (the order of the first drawing), the pages from 1, the declared color, the
    // resolution (8.9.4), hiddenLayer for an image drawn only in a layer off by /D (8.11.4.3)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference a;
    pdf::PDFObjectReference b;
    pdf::PDFObjectReference c;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeLayeredImages(fixture, &a, &b, &c));
    QVERIFY(tab);

    QJsonObject list = AppMcpStep4::listImages(fixture, tab);
    QCOMPARE(list.value(QStringLiteral("totalImages")).toInt(), 3);
    QVERIFY(!list.contains(QStringLiteral("nextCursor")));
    QCOMPARE(list.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    QJsonArray images = list.value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 3);
    const QJsonObject imageA = images.at(0).toObject();
    QCOMPARE(imageA.value(QStringLiteral("id")).toString(), AppMcpImageDocument::id(a));
    QCOMPARE(imageA.value(QStringLiteral("number")).toInt(), 1);
    QCOMPARE(imageA.value(QStringLiteral("kind")).toString(), QStringLiteral("xobject"));
    QCOMPARE(imageA.value(QStringLiteral("pages")).toArray(), QJsonArray({ 1, 2 }));
    QCOMPARE(imageA.value(QStringLiteral("pageCount")).toInt(), 2);
    QCOMPARE(imageA.value(QStringLiteral("width")).toInt(), 40);
    QCOMPARE(imageA.value(QStringLiteral("height")).toInt(), 20);
    QCOMPARE(imageA.value(QStringLiteral("dpi")).toDouble(), 36.0);
    QCOMPARE(imageA.value(QStringLiteral("colorSpace")).toString(), QStringLiteral("DeviceRGB"));
    QCOMPARE(imageA.value(QStringLiteral("color")).toString(), QStringLiteral("color"));
    QCOMPARE(imageA.value(QStringLiteral("bytes")).toInt(), 40 * 20 * 3);
    QVERIFY(!imageA.value(QStringLiteral("hiddenLayer")).toBool());
    QVERIFY(!imageA.value(QStringLiteral("usedAsMask")).toBool());
    const QJsonObject imageB = images.at(1).toObject();
    QCOMPARE(imageB.value(QStringLiteral("id")).toString(), AppMcpImageDocument::id(b));
    QCOMPARE(imageB.value(QStringLiteral("color")).toString(), QStringLiteral("gray"));
    QCOMPARE(imageB.value(QStringLiteral("pages")).toArray(), QJsonArray({ 1 }));
    const QJsonObject imageC = images.at(2).toObject();
    QCOMPARE(imageC.value(QStringLiteral("id")).toString(), AppMcpImageDocument::id(c));
    QCOMPARE(imageC.value(QStringLiteral("number")).toInt(), 3);
    QVERIFY(imageC.value(QStringLiteral("hiddenLayer")).toBool());

    // Page 2 only: A and C with their numbers of the whole list (1 and 3, the # of the file names)
    list = AppMcpStep4::listImages(fixture, tab, QJsonObject({ { QStringLiteral("pages"), QStringLiteral("2") } }));
    QCOMPARE(list.value(QStringLiteral("totalImages")).toInt(), 2);
    images = list.value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 2);
    QCOMPARE(images.at(0).toObject().value(QStringLiteral("number")).toInt(), 1);
    QCOMPARE(images.at(1).toObject().value(QStringLiteral("number")).toInt(), 3);

    // Paging: one image per call, the cursor of the previous result continues
    list = AppMcpStep4::listImages(fixture, tab, QJsonObject({ { QStringLiteral("maxResults"), 1 } }));
    QCOMPARE(list.value(QStringLiteral("images")).toArray().size(), 1);
    QCOMPARE(list.value(QStringLiteral("totalImages")).toInt(), 3);
    const QString cursor = list.value(QStringLiteral("nextCursor")).toString();
    QVERIFY(!cursor.isEmpty());
    list = AppMcpStep4::listImages(fixture, tab, QJsonObject({ { QStringLiteral("maxResults"), 1 }, { QStringLiteral("cursor"), cursor } }));
    QCOMPARE(list.value(QStringLiteral("images")).toArray().size(), 1);
    QCOMPARE(list.value(QStringLiteral("images")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(), AppMcpImageDocument::id(b));

    // A page outside of the document and a cursor, which is not one, are refused
    const std::optional<pdf::PDFMcpCallResult> outside = AppMcpStep4::callListImages(fixture, tab, QJsonObject({ { QStringLiteral("pages"), QStringLiteral("3") } }));
    QVERIFY(outside && outside->isError);
    QVERIFY2(outside->getFirstText().contains(QStringLiteral("pages")), qPrintable(outside->getFirstText()));
    const std::optional<pdf::PDFMcpCallResult> badCursor = AppMcpStep4::callListImages(fixture, tab, QJsonObject({ { QStringLiteral("cursor"), QStringLiteral("nonsense") } }));
    QVERIFY(badCursor && badCursor->isError);
    QVERIFY2(badCursor->getFirstText().contains(QStringLiteral("cursor")), qPrintable(badCursor->getFirstText()));
}

void AppMcpConvertTest::imageListCache()
{
    // RS4APP: the cursors of one listing walk the pages once - the complete list of a version of a
    // document is kept (a new version misses it), an incomplete (canceled) list never; the oldest
    // lists go; jobs of several workers read and fill it at once
    using Cache = pdf::PDFAppMcpImageListCache;
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFDocument emptyDocument = builder.build();
    const std::shared_ptr<const pdf::PDFDocumentImageList> list = pdf::PDFAppMcpDocumentReader::collectImages(&emptyDocument, nullptr);
    QVERIFY(list && list->isComplete());

    Cache cache;
    QVERIFY(!cache.find(QStringLiteral("doc"), 1));
    cache.insert(QStringLiteral("doc"), 1, list);
    QCOMPARE(cache.find(QStringLiteral("doc"), 1), list);
    QVERIFY(!cache.find(QStringLiteral("doc"), 2));
    QVERIFY(!cache.find(QStringLiteral("other"), 1));

    pdf::PDFAppCancelFlag canceled;
    canceled.cancel();
    const std::shared_ptr<const pdf::PDFDocumentImageList> incomplete = pdf::PDFAppMcpDocumentReader::collectImages(&emptyDocument, &canceled);
    if (!incomplete->isComplete())
    {
        cache.insert(QStringLiteral("doc"), 3, incomplete);
        QVERIFY(!cache.find(QStringLiteral("doc"), 3));
    }

    for (quint64 version = 10; version < 10 + Cache::MAXIMUM_LISTS; ++version)
    {
        cache.insert(QStringLiteral("doc"), version, list);
    }
    QVERIFY(!cache.find(QStringLiteral("doc"), 1));
    QCOMPARE(cache.find(QStringLiteral("doc"), 10), list);

    // Several threads at once (a data race would be found by TSan of the coordinator)
    Cache shared;
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&shared, &list, t]()
        {
            for (int i = 0; i < 500; ++i)
            {
                shared.insert(QStringLiteral("doc%1").arg(t), quint64(i), list);
                shared.find(QStringLiteral("doc%1").arg((t + 1) % 8), quint64(i));
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    // After the threads the cache holds at most its lists and works as before
    size_t kept = 0;
    for (int t = 0; t < 8; ++t)
    {
        for (int i = 0; i < 500; ++i)
        {
            kept += shared.find(QStringLiteral("doc%1").arg(t), quint64(i)) ? 1 : 0;
        }
    }
    QVERIFY(kept >= 1 && kept <= Cache::MAXIMUM_LISTS);
    shared.insert(QStringLiteral("last"), 1, list);
    QCOMPARE(shared.find(QStringLiteral("last"), 1), list);
}

void AppMcpConvertTest::extractImagesExcludeById()
{
    // R4-11, K4.12: Extract Images writes the images of every layer (also C of the hidden layer);
    // an id of list_images in exclude is left out and its number is skipped in "{source_base}_img_#"
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference a;
    pdf::PDFObjectReference b;
    pdf::PDFObjectReference c;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeLayeredImages(fixture, &a, &b, &c));
    QVERIFY(tab);
    const QJsonArray images = AppMcpStep4::listImages(fixture, tab).value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 3);
    const QString excludedId = images.at(1).toObject().value(QStringLiteral("id")).toString();

    const QString folder = QDir(fixture.folder()).filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(folder));
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), QStringLiteral("extract-images"),
                                                                          QJsonObject({ { QStringLiteral("folder"), folder }, { QStringLiteral("exclude"), excludedId } }),
                                                                          QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QStringList files = QDir(folder).entryList(QDir::Files);
    files.sort();
    QCOMPARE(files, QStringList({ QStringLiteral("Catalog_img_1.png"), QStringLiteral("Catalog_img_3.png") }));
    QCOMPARE(QImage(QDir(folder).filePath(QStringLiteral("Catalog_img_1.png"))).size(), QSize(40, 20));
    QCOMPARE(QImage(QDir(folder).filePath(QStringLiteral("Catalog_img_3.png"))).size(), QSize(30, 30));
}

void AppMcpConvertTest::audioBookSaveFile()
{
    // R4-1, Q4.2 of 14. 11.: the WAV file is made in a temporary file and moved to the path of
    // save_file (RIFF / WAVE header, PCM 16 bit mono of the engine); return_text is no result of it;
    // nothing stays in the temporary folder of the application
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    fixture.context->setSpeechSynthesizerFactory(std::make_shared<McpFakeSynthesizerFactory>());
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Story.pdf"), 2));
    QVERIFY(tab);
    const QString path = QDir(fixture.folder()).filePath(QStringLiteral("Story.wav"));

    QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("return_text") } });
    std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("convert"), QStringLiteral("audiobook"), QJsonObject(), common);
    QVERIFY(result);
    QVERIFY(result->isError);

    common.insert(QStringLiteral("result"), QStringLiteral("save_file"));
    common.insert(QStringLiteral("path"), path);
    result = AppMcpStep4::call(fixture, QStringLiteral("convert"), QStringLiteral("audiobook"), QJsonObject({ { QStringLiteral("format"), QStringLiteral("wav") } }), common);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    // RIFF: "RIFF", the size of the rest, "WAVE", "fmt " with PCM (1), 1 channel, 16 bits
    const QByteArray data = AppMcpStep4::readFile(path);
    QVERIFY(data.size() > 44);
    QCOMPARE(data.left(4), QByteArray("RIFF"));
    QCOMPARE(qFromLittleEndian<quint32>(data.constData() + 4), quint32(data.size() - 8));
    QCOMPARE(data.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(data.mid(12, 4), QByteArray("fmt "));
    QCOMPARE(qFromLittleEndian<quint16>(data.constData() + 20), quint16(1));
    QCOMPARE(qFromLittleEndian<quint16>(data.constData() + 22), quint16(1));
    QCOMPARE(qFromLittleEndian<quint16>(data.constData() + 34), quint16(16));

    const QString temporaryFolder = fixture.context->getToolTemporaryFolder();
    if (!temporaryFolder.isEmpty())
    {
        QVERIFY(QDir(temporaryFolder).entryList(QStringList({ QStringLiteral("pdf4qt-*") }), QDir::Files).isEmpty());
    }
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of two instances (AppMcpFixture::secondInstance)
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppMcpConvertTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcpconverttest.moc"
