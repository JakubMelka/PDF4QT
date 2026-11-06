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

// The tool Optimize Structure of the core (optimize.structure, package 2C of the first batch
// of phase 2; Figma 302:4072, 303:4560, 303:4897). The documents are built by the test: each
// step has an object, whose change only that step makes.

#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_structure.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdffont.h"
#include "pdfform.h"
#include "pdfimage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfocrpagepreparer.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdfstreamfilters.h"

#include "testprogressrecorder.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QtTest>

#include <atomic>
#include <thread>

/// Cancellation of the tests
class StructureTestControl : public pdf::PDFOperationControl
{
public:
    void cancel() { m_isCanceled.store(true); }
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(); }

private:
    std::atomic<bool> m_isCanceled = false;
};

/// Records the progress; cancels the run at a step of the text
class StructureTestListener : public pdftest::TestProgressRecorder
{
public:
    QString cancelOnText;
    StructureTestControl* control = nullptr;

    StructureTestListener()
    {
        onText = [this](const QString& stepText)
        {
            if (control && !cancelOnText.isEmpty() && stepText.startsWith(cancelOnText))
            {
                control->cancel();
            }
        };
    }
};

/// Documents of the tests
class StructureTestHelper
{
public:
    StructureTestHelper() = delete;

    static constexpr const char* UNUSED_MARKER = "UNUSED-OBJECT-MARKER";
    static constexpr const char* IDENTICAL_CONTENT = "IDENTICAL-STREAM-CONTENT";

    /// References of the objects, whose change one step makes
    struct Probes
    {
        pdf::PDFObjectReference gapReference;   ///< A free entry in the middle of the objects
        pdf::PDFObjectReference lastReference;  ///< The last object, which the shrink moves into the gap
        QByteArray flateContent;                ///< The decoded content of the stored Flate stream
        qsizetype storedFlateSize = 0;          ///< Its stored size (zlib level 0)
        size_t objectEntries = 0;               ///< Entries of the storage
    };

    static pdf::PDFObject createStream(QByteArray content, pdf::PDFDictionaryBuilder dictionary = pdf::PDFDictionaryBuilder())
    {
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
    }

    /// A page of 100 x 100 points: two rectangles and an image of 32 x 32 pixels
    static pdf::PDFDocument createBaseDocument()
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));

        QImage image(32, 32, QImage::Format_RGB32);
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                image.setPixel(x, y, qRgb(x * 8, y * 8, (x + y) * 4));
            }
        }
        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::Flate;
        options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;
        options.enablePngPredictor = false;
        const pdf::PDFObjectReference imageReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(pdf::PDFImage::createStreamFromImage(image, options))));

        const pdf::PDFObjectReference content = builder.addObject(createStream("1 0 0 rg 10 10 30 30 re f 0 0 1 rg 50 60 40 20 re f q 40 0 0 40 50 10 cm /Im Do Q"));
        pdf::PDFDictionaryBuilder xobjects;
        xobjects.addEntry(pdf::PDFInplaceOrMemoryString("Im"), pdf::PDFObject::createReference(imageReference));
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xobjects)));
        pdf::PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(content));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
        return builder.build();
    }

    /// The base document with an object for each step:
    ///   /PDF4QTValue - a reference to the integer 42 (Embed simple objects),
    ///   /PDF4QTDict << /Kept 1 /Empty null >> (Remove null objects),
    ///   an object with UNUSED_MARKER no reference leads to (Remove unused objects),
    ///   /PDF4QTCopyA and /PDF4QTCopyB - two identical streams (Merge identical objects),
    ///   a free entry before the object of /PDF4QTLast (Shrink object storage),
    ///   /PDF4QTFlate - a Flate stream stored by zlib level 0 (Recompress flate streams)
    static pdf::PDFDocument createDocument(Probes* probes)
    {
        const pdf::PDFDocument base = createBaseDocument();
        pdf::PDFObjectStorage storage = base.getStorage();

        const pdf::PDFObjectReference value = storage.addObject(pdf::PDFObject::createInteger(42));
        storage.addObject(createStream(QByteArray("% ") + UNUSED_MARKER));
        const pdf::PDFObjectReference copyA = storage.addObject(createStream(IDENTICAL_CONTENT));
        const pdf::PDFObjectReference copyB = storage.addObject(createStream(IDENTICAL_CONTENT));

        QByteArray text;
        for (int i = 0; i < 400; ++i)
        {
            text += "A line of the text, which repeats itself. ";
        }
        const QByteArray zlib = qCompress(text, 0).mid(4);  // qCompress puts the length before the zlib stream
        pdf::PDFDictionaryBuilder flateDictionary;
        flateDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
        const pdf::PDFObjectReference flate = storage.addObject(createStream(zlib, std::move(flateDictionary)));

        const pdf::PDFObjectReference gap = storage.addObject(pdf::PDFObject::createInteger(7));
        const pdf::PDFObjectReference last = storage.addObject(pdf::PDFObject::createArray(pdf::PDFArrayBuilder()));

        pdf::PDFObjectStorage::PDFObjects objects = storage.getObjects();
        objects[size_t(gap.objectNumber)].object = pdf::PDFObject();

        pdf::PDFDictionaryBuilder nullDictionary;
        nullDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Kept"), pdf::PDFObject::createInteger(1));
        nullDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Empty"), pdf::PDFObject());

        pdf::PDFObjectStorage::Entry& catalogEntry = objects[size_t(base.getTrailerDictionary()->get("Root").getReference().objectNumber)];
        pdf::PDFDictionaryBuilder catalog(*catalogEntry.object.getDictionary());
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTValue"), pdf::PDFObject::createReference(value));
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTDict"), pdf::PDFObject::createDictionary(std::move(nullDictionary)));
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTCopyA"), pdf::PDFObject::createReference(copyA));
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTCopyB"), pdf::PDFObject::createReference(copyB));
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTFlate"), pdf::PDFObject::createReference(flate));
        catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTLast"), pdf::PDFObject::createReference(last));
        catalogEntry.object = pdf::PDFObject::createDictionary(std::move(catalog));
        storage.setObjects(std::move(objects));

        if (probes)
        {
            probes->gapReference = gap;
            probes->lastReference = last;
            probes->flateContent = text;
            probes->storedFlateSize = zlib.size();
            probes->objectEntries = storage.getObjects().size();
        }
        return pdf::PDFDocument(std::move(storage), pdf::PDFVersion(1, 4), QByteArray());
    }

    static const pdf::PDFDictionary* getCatalog(const pdf::PDFDocument& document)
    {
        return document.getObject(document.getTrailerDictionary()->get("Root")).getDictionary();
    }

    static QByteArray write(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        return writer.write(&buffer, &document) ? data : QByteArray();
    }

    static pdf::PDFDocument read(const QByteArray& data, const QString& password = QString(), pdf::PDFDocumentReader::Result* readerResult = nullptr)
    {
        const auto getPassword = [&password](bool* ok)
        {
            *ok = !password.isEmpty();
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        if (readerResult)
        {
            *readerResult = reader.getReadingResult();
        }
        return document;
    }

    /// Context of a document, whose file is larger than any result (each run makes a document)
    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, QByteArray originalData = QByteArray(4000000, 'x'), bool isModified = false)
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), std::move(originalData), QStringLiteral("Project_Report.pdf"), isModified));
    }

    /// All the steps on or off, with exceptions
    static QVariantMap steps(bool isOn, std::initializer_list<const char*> others = { })
    {
        QVariantMap parameters;
        for (const pdf::PDFDocumentToolStructure::Step& step : pdf::PDFDocumentToolStructure::getSteps())
        {
            parameters.insert(QString::fromLatin1(step.parameter), isOn);
        }
        for (const char* other : others)
        {
            parameters.insert(QString::fromLatin1(other), !isOn);
        }
        return parameters;
    }

    static QString factOf(const std::vector<pdf::PDFDocumentToolFact>& facts, const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& fact : facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

    /// The page drawn into 100 x 100 pixels
    static QImage draw(pdf::PDFDocument document)
    {
        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        fontCache.setDocument(pdf::PDFModifiedDocument(&document, nullptr));
        pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
        pdf::PDFCMSGeneric cms;

        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        pdf::PDFRenderer renderer(&document, &fontCache, nullptr, &cms, &optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());
        renderer.render(&painter, QRectF(0, 0, 100, 100), 0);
        painter.end();
        return image;
    }
};

class DocumentToolStructureTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void parametersOfTheFrames();
    void atLeastOneStep();
    void eachStepAlone_data();
    void eachStepAlone();
    void pagesLookTheSame();
    void factsAndRowsOfTheFrames();
    void formatting();
    void wholeWriteKeepsVersion();
    void nothingToDo();
    void canceled();
    void progressOverPhases();
    void encryptedInput();
    void signedInput();
    void conformanceRemoved();
    void conformanceNotRemovable();
    void eightThreadsOneInstance();
    void hostileReferences();
};

void DocumentToolStructureTest::initTestCase()
{
    // The texts of the frames are in English with the separators of en-US (the application sets
    // the locale of its user interface the same way, O17)
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void DocumentToolStructureTest::parametersOfTheFrames()
{
    // panel-steps 303:4560: six Bool parameters with the texts of the panel, all on (302:4072 "6 of 6");
    // the right panel 319:7288 has the short texts and the icons of the tiles
    const pdf::PDFDocumentToolStructure tool;
    QCOMPARE(tool.getId(), QStringLiteral("optimize.structure"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QVERIFY(pdf::PDFDocumentToolParameters::isValid(definitions));
    const QStringList names = { QStringLiteral("embedSimpleObjects"), QStringLiteral("removeNullObjects"), QStringLiteral("removeUnusedObjects"),
                                QStringLiteral("mergeIdenticalObjects"), QStringLiteral("shrinkObjectStorage"), QStringLiteral("recompressFlateStreams") };
    const QStringList titles = { QStringLiteral("Embed (dereference) simple objects, such as int, bool, real"), QStringLiteral("Remove null objects from dictionary entries"),
                                 QStringLiteral("Remove unused objects (unreachable from document root)"), QStringLiteral("Merge identical objects"),
                                 QStringLiteral("Shrink object storage (squeeze free entries)"), QStringLiteral("Recompress flate streams by maximal compression") };
    const QStringList descriptions = { QStringLiteral("Small values are written in place instead of being referenced"), QStringLiteral("Entries without a value are dropped"),
                                       QStringLiteral("Leftovers of earlier edits are deleted"), QStringLiteral("Duplicates are stored once"),
                                       QStringLiteral("Objects are renumbered so no gaps remain"), QStringLiteral("Takes the longest, saves the most") };
    QCOMPARE(definitions.size(), size_t(6));
    for (int i = 0; i < 6; ++i)
    {
        QCOMPARE(definitions[size_t(i)].getName(), names[i]);
        QCOMPARE(definitions[size_t(i)].getType(), pdf::PDFDocumentToolParameterType::Bool);
        QCOMPARE(definitions[size_t(i)].getTitle(), titles[i]);
        QCOMPARE(definitions[size_t(i)].getDescription(), descriptions[i]);
        QCOMPARE(definitions[size_t(i)].getDefaultValue().toBool(), true);
    }

    const QStringList shortTitles = { QStringLiteral("Embed simple objects"), QStringLiteral("Remove null objects"), QStringLiteral("Remove unused objects"),
                                      QStringLiteral("Merge identical objects"), QStringLiteral("Shrink object storage"), QStringLiteral("Recompress flate streams") };
    const QStringList shortDescriptions = { QStringLiteral("Writes small values in place"), QStringLiteral("Drops entries without a value"), QStringLiteral("Deletes leftovers of earlier edits"),
                                            QStringLiteral("Stores duplicates once"), QStringLiteral("Renumbers objects without gaps"), QStringLiteral("Slowest step, saves the most") };
    const QStringList icons = { QStringLiteral("embed"), QStringLiteral("eraser"), QStringLiteral("trash"), QStringLiteral("copy"), QStringLiteral("hash"), QStringLiteral("minimize-2") };
    const std::vector<pdf::PDFDocumentToolStructure::Step>& steps = pdf::PDFDocumentToolStructure::getSteps();
    QCOMPARE(steps.size(), size_t(6));
    for (int i = 0; i < 6; ++i)
    {
        QCOMPARE(QString::fromLatin1(steps[size_t(i)].parameter), names[i]);
        QCOMPARE(pdf::PDFDocumentToolStructure::getStepTitle(steps[size_t(i)]), shortTitles[i]);
        QCOMPARE(pdf::PDFDocumentToolStructure::getStepDescription(steps[size_t(i)]), shortDescriptions[i]);
        QCOMPARE(QString::fromLatin1(steps[size_t(i)].iconName), icons[i]);
    }

    // The catalog: Keep preselected, the copy only of a signed document, the permission Modify (ISO 32000-2 Table 22)
    const pdf::PDFDocumentToolCatalogEntry* entry = tool.getInfo();
    QCOMPARE(entry->getPreselectedResult(), pdf::PDFDocumentToolPreselectedResult::Keep);
    QCOMPARE(entry->getSignatureImpact(), pdf::PDFDocumentToolSignatureImpact::LostCopyOnly);
    QCOMPARE(entry->getRequiredPermissions(), std::vector<pdf::PDFSecurityHandler::Permission>({ pdf::PDFSecurityHandler::Permission::Modify }));
    QVERIFY(!tool.hasPreview());
}

void DocumentToolStructureTest::atLeastOneStep()
{
    // Plan chapter 6: at least one step; nothing is run without one
    using H = StructureTestHelper;
    const pdf::PDFDocument document = H::createBaseDocument();
    const pdf::PDFDocumentToolStructure tool;

    const pdf::PDFDocumentToolResult refused = tool.run(H::createContext(document), H::steps(false), nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(refused.message.contains(QStringLiteral("Choose at least one step")), qPrintable(refused.message));
    QVERIFY(refused.documents.empty());

    // Omitted parameters are on: an assistant, which turns one off, runs the other five
    QVariantMap oneOff;
    oneOff.insert(QStringLiteral("shrinkObjectStorage"), false);
    const pdf::PDFDocumentToolResult run = tool.run(H::createContext(document), oneOff, nullptr, nullptr);
    QCOMPARE(run.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(run.data.value(QStringLiteral("stepsRun")).toInt(), 5);
}

void DocumentToolStructureTest::eachStepAlone_data()
{
    QTest::addColumn<QString>("parameter");
    for (const pdf::PDFDocumentToolStructure::Step& step : pdf::PDFDocumentToolStructure::getSteps())
    {
        QTest::newRow(step.parameter) << QString::fromLatin1(step.parameter);
    }
}

void DocumentToolStructureTest::eachStepAlone()
{
    // tools.md 4 optimize.structure: each step does its own change and only it; the rows of
    // Steps performed (303:5370) say which steps ran
    using H = StructureTestHelper;
    QFETCH(QString, parameter);

    H::Probes probes;
    const pdf::PDFDocument input = H::createDocument(&probes);
    QVariantMap parameters = H::steps(false);
    parameters.insert(parameter, true);

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.documents.size(), size_t(1));
    const pdf::PDFDocument& output = *result.documents.front().document;
    const pdf::PDFDictionary* catalog = H::getCatalog(output);
    QVERIFY(catalog);
    const QByteArray written = H::write(output);
    QVERIFY(!written.isEmpty());

    const bool isEmbed = parameter == QLatin1String("embedSimpleObjects");
    const bool isNull = parameter == QLatin1String("removeNullObjects");
    const bool isUnused = parameter == QLatin1String("removeUnusedObjects");
    const bool isMerge = parameter == QLatin1String("mergeIdenticalObjects");
    const bool isShrink = parameter == QLatin1String("shrinkObjectStorage");
    const bool isRecompress = parameter == QLatin1String("recompressFlateStreams");

    // Embed: the reference to the integer is the integer itself
    QCOMPARE(catalog->get("PDF4QTValue").isInt(), isEmbed);
    QCOMPARE(output.getObject(catalog->get("PDF4QTValue")).getInteger(), qint64(42));

    // Null: the entry without a value is gone, the other entry stays
    const pdf::PDFDictionary* dictionary = catalog->get("PDF4QTDict").getDictionary();
    QCOMPARE(dictionary->hasKey("Empty"), !isNull);
    QCOMPARE(dictionary->get("Kept").getInteger(), qint64(1));

    // Unused: the object no reference leads to is not in the written file
    QCOMPARE(written.contains(H::UNUSED_MARKER), !isUnused);

    // Merge: both entries refer to one stream
    QCOMPARE(catalog->get("PDF4QTCopyA").getReference() == catalog->get("PDF4QTCopyB").getReference(), isMerge);

    // Shrink: the last object fills the free entry, the list of the objects is shorter
    QCOMPARE(catalog->get("PDF4QTLast").getReference().objectNumber == probes.gapReference.objectNumber, isShrink);
    QCOMPARE(output.getStorage().getObjects().size() < probes.objectEntries, isShrink);
    QVERIFY(output.getObject(catalog->get("PDF4QTLast")).isArray());

    // Recompress: the stored stream is smaller, its decoded content the same
    const pdf::PDFStream* flate = output.getObject(catalog->get("PDF4QTFlate")).getStream();
    QVERIFY(flate);
    QCOMPARE(flate->getContent()->size() < probes.storedFlateSize, isRecompress);
    QCOMPARE(output.getDecodedStream(flate), probes.flateContent);

    // The rows of the result: only the chosen step has run
    const std::vector<pdf::PDFDocumentToolStructure::StepRow> rows = pdf::PDFDocumentToolStructure::getStepRows(result);
    QCOMPARE(rows.size(), size_t(6));
    for (const pdf::PDFDocumentToolStructure::StepRow& row : rows)
    {
        QCOMPARE(row.isRun, row.parameter == parameter);
        if (row.isRun)
        {
            QVERIFY(row.text != QStringLiteral("Skipped – not selected"));
            QVERIFY(result.data.value(QStringLiteral("steps")).toObject().value(parameter).toObject().value(QStringLiteral("count")).toInteger() >= 1);
        }
        else
        {
            QCOMPARE(row.text, QStringLiteral("Skipped – not selected"));
        }
    }
    for (const pdf::PDFDocumentToolStructure::Step& step : pdf::PDFDocumentToolStructure::getSteps())
    {
        if (QString::fromLatin1(step.parameter) == parameter)
        {
            QCOMPARE(pdf::PDFDocumentToolStructure::getFlags(parameters), pdf::PDFOptimizer::OptimizationFlags(step.flag));
        }
    }
}

void DocumentToolStructureTest::pagesLookTheSame()
{
    // Plan chapter 7: lossless, the page drawn after all the steps has the same pixels
    using H = StructureTestHelper;
    const pdf::PDFDocument input = H::createDocument(nullptr);
    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const QImage before = H::draw(input);
    const QImage after = H::draw(H::read(H::write(*result.documents.front().document)));
    QCOMPARE(after.size(), before.size());
    QVERIFY(before.pixel(20, 80) != qRgb(255, 255, 255));   // The red rectangle is drawn at all
    QCOMPARE(after, before);
}

void DocumentToolStructureTest::factsAndRowsOfTheFrames()
{
    // 303:4897: the facts Size, Steps, Time of the dialog; the rows Size before, Size after,
    // Saved, Objects, Time of the panel; the report of Copy report
    using H = StructureTestHelper;
    const pdf::PDFDocument input = H::createDocument(nullptr);
    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true, { "shrinkObjectStorage" }), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const qint64 before = result.data.value(QStringLiteral("sizeBefore")).toInteger();
    const qint64 after = result.data.value(QStringLiteral("sizeAfter")).toInteger();
    QCOMPARE(before, qint64(4000000));
    QCOMPARE(after, qint64(H::write(*result.documents.front().document).size()));

    QCOMPARE(result.facts.size(), size_t(3));
    QCOMPARE(result.facts[0].id, QStringLiteral("size"));
    QCOMPARE(result.facts[0].title, QStringLiteral("Size"));
    QCOMPARE(result.facts[0].value, QStringLiteral("%1 (%2 smaller)").arg(pdf::PDFDocumentToolFormat::formatSizeChange(before, after), pdf::PDFDocumentToolStructure::formatSavedPercent(before, after)));
    QCOMPARE(result.facts[1].id, QStringLiteral("steps"));
    QCOMPARE(result.facts[1].value, QStringLiteral("5 of 6 run · 1 skipped"));
    QCOMPARE(result.facts[2].id, QStringLiteral("time"));
    QVERIFY(result.facts[2].value.endsWith(QStringLiteral(" s")));

    const std::vector<pdf::PDFDocumentToolFact> rows = pdf::PDFDocumentToolStructure::getResultRows(result);
    QCOMPARE(rows.size(), size_t(5));
    QCOMPARE(rows[0].title, QStringLiteral("Size before"));
    QCOMPARE(rows[0].value, pdf::PDFDocumentToolFormat::formatSize(before));
    QCOMPARE(rows[1].title, QStringLiteral("Size after"));
    QCOMPARE(rows[2].title, QStringLiteral("Saved"));
    QCOMPARE(rows[2].value, QStringLiteral("%1 · %2").arg(pdf::PDFDocumentToolFormat::formatSize(before - after), pdf::PDFDocumentToolStructure::formatSavedPercent(before, after)));
    QCOMPARE(rows[3].title, QStringLiteral("Objects"));
    const pdf::PDFDocumentToolStructure::Counts countsBefore = pdf::PDFDocumentToolStructure::getCounts(&input);
    const pdf::PDFDocumentToolStructure::Counts countsAfter = pdf::PDFDocumentToolStructure::getCounts(result.documents.front().document.data());
    QVERIFY(countsAfter.objects < countsBefore.objects);
    QCOMPARE(rows[3].value, QStringLiteral("%1 → %2").arg(countsBefore.objects).arg(countsAfter.objects));
    QCOMPARE(rows[4].title, QStringLiteral("Time"));

    // The steps of MCP: a text with the title, the state
    QCOMPARE(result.steps.size(), size_t(6));
    QCOMPARE(result.steps[4].state, pdf::PDFDocumentToolStep::State::Skipped);
    QCOMPARE(result.steps[4].text, QStringLiteral("Shrink object storage: Skipped – not selected"));
    QCOMPARE(result.steps[0].state, pdf::PDFDocumentToolStep::State::Done);
    QVERIFY(result.steps[0].text.startsWith(QStringLiteral("Embed simple objects: ")));
    QVERIFY2(QRegularExpression(QStringLiteral("^Remove unused objects: [0-9,]+ objects? removed$")).match(result.steps[2].text).hasMatch(), qPrintable(result.steps[2].text));

    const QStringList report = pdf::PDFDocumentToolStructure::createReport(result).split(QLatin1Char('\n'));
    QCOMPARE(report.front(), QStringLiteral("Optimize Structure"));
    QVERIFY(report.contains(QStringLiteral("Size before: ") + rows[0].value));
    QVERIFY(report.contains(QStringLiteral("Objects: ") + rows[3].value));
    QVERIFY(report.contains(QStringLiteral("Steps performed")));
    QVERIFY(report.contains(QStringLiteral("Shrink object storage: Skipped – not selected")));
    QCOMPARE(report.size(), 1 + 5 + 2 + 6);

    // All six: "6 of 6 run"; the suggested name of the copy
    const pdf::PDFDocumentToolResult all = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(H::factOf(all.facts, QStringLiteral("steps")), QStringLiteral("6 of 6 run"));
    QCOMPARE(all.documents.front().suggestedFileName, QStringLiteral("Project_Report_optimized.pdf"));
}

void DocumentToolStructureTest::formatting()
{
    // 303:5001: "4.2 s", "12.6 %"; a minute and more as the other tools
    QCOMPARE(pdf::PDFDocumentToolStructure::formatDuration(4200), QStringLiteral("4.2 s"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatDuration(4249), QStringLiteral("4.2 s"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatDuration(3), QStringLiteral("0.1 s"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatDuration(72000), QStringLiteral("1 min 12 s"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatSavedPercent(1000, 874), QStringLiteral("12.6 %"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatSavedPercent(1000, 1000), QStringLiteral("0.0 %"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatSavedPercent(1000, 1200), QStringLiteral("0.0 %"));
    QCOMPARE(pdf::PDFDocumentToolStructure::formatSavedPercent(0, 0), QStringLiteral("0.0 %"));
}

void DocumentToolStructureTest::wholeWriteKeepsVersion()
{
    // Plan 2.7: a local optimizer and the version of the input (the optimizer reports 2.0); the
    // file is written whole, one revision
    using H = StructureTestHelper;
    const pdf::PDFDocument input = H::createDocument(nullptr);
    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    const pdf::PDFDocument& output = *result.documents.front().document;
    QCOMPARE(output.getInfo()->version.major, uint16_t(1));
    QCOMPARE(output.getInfo()->version.minor, uint16_t(4));
    const QByteArray written = H::write(output);
    QVERIFY(written.startsWith("%PDF-1.4"));
    QCOMPARE(written.count("%%EOF"), qsizetype(1));
    QVERIFY(!written.contains("/Prev"));

    // The input is not changed
    QVERIFY(H::getCatalog(input)->get("PDF4QTValue").isReference());
}

void DocumentToolStructureTest::nothingToDo()
{
    // A file, which the tool has optimized already, does not get smaller: nothing to do, no document
    using H = StructureTestHelper;
    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult first = tool.run(H::createContext(H::createDocument(nullptr)), H::steps(true), nullptr, nullptr);
    QCOMPARE(first.status, pdf::PDFDocumentToolResultStatus::Success);

    const QByteArray file = H::write(*first.documents.front().document);
    const pdf::PDFDocument reopened = H::read(file);
    const pdf::PDFDocumentToolResult second = tool.run(H::createContext(reopened, file), H::steps(true), nullptr, nullptr);
    QCOMPARE(second.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QCOMPARE(second.message, QStringLiteral("File cannot be reduced with these steps"));
    QVERIFY(second.documents.empty());
    QVERIFY(second.isUse());
    QCOMPARE(H::factOf(second.facts, QStringLiteral("steps")), QStringLiteral("6 of 6 run"));
    QCOMPARE(second.data.value(QStringLiteral("savedBytes")).toInteger(), qint64(0));
}

void DocumentToolStructureTest::canceled()
{
    using H = StructureTestHelper;
    const pdf::PDFDocument input = H::createDocument(nullptr);
    const pdf::PDFDocumentToolStructure tool;

    StructureTestControl control;
    control.cancel();
    const pdf::PDFDocumentToolResult before = tool.run(H::createContext(input), H::steps(true), &control, nullptr);
    QCOMPARE(before.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(before.documents.empty());

    // Canceled while the structure is cleaned up: no later phase, no document
    StructureTestControl runningControl;
    StructureTestListener listener;
    listener.control = &runningControl;
    listener.cancelOnText = QStringLiteral("Cleaning up the structure");
    const pdf::PDFDocumentToolResult during = tool.run(H::createContext(input), H::steps(true), &runningControl, &listener);
    QCOMPARE(during.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(during.documents.empty());
    for (const QString& text : listener.texts)
    {
        QVERIFY2(!text.startsWith(QStringLiteral("Measuring the result")), qPrintable(text));
    }
}

void DocumentToolStructureTest::progressOverPhases()
{
    // One bar over the stages of the optimizer and the measuring of the written file
    using H = StructureTestHelper;
    StructureTestListener listener;
    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(H::createDocument(nullptr)), H::steps(true), nullptr, &listener);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);

    QVERIFY(!listener.percentages.isEmpty());
    QCOMPARE(listener.percentages.back(), 100);
    for (int i = 1; i < listener.percentages.size(); ++i)
    {
        QVERIFY(listener.percentages[i] >= listener.percentages[i - 1]);
    }
    bool hasStructure = false;
    bool hasMeasure = false;
    for (const QString& text : listener.texts)
    {
        hasStructure = hasStructure || text.startsWith(QStringLiteral("Cleaning up the structure"));
        hasMeasure = hasMeasure || text.startsWith(QStringLiteral("Measuring the result"));
    }
    QVERIFY2(hasStructure && hasMeasure, qPrintable(listener.texts.join(QStringLiteral(" | "))));
}

void DocumentToolStructureTest::encryptedInput()
{
    // The memory holds the decrypted document; the written result is encrypted again (plan 7)
    using H = StructureTestHelper;
    const QString password = QStringLiteral("user-secret");
    pdf::PDFDocument plain = H::createDocument(nullptr);
    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = password;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu;
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const QByteArray file = H::write(builder.build());

    pdf::PDFDocumentReader::Result readerResult = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument opened = H::read(file, password, &readerResult);
    QCOMPARE(readerResult, pdf::PDFDocumentReader::Result::OK);

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(opened), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    const QByteArray written = H::write(*result.documents.front().document);

    pdf::PDFDocumentReader::Result withoutPassword = pdf::PDFDocumentReader::Result::OK;
    H::read(written, QString(), &withoutPassword);
    QVERIFY(withoutPassword != pdf::PDFDocumentReader::Result::OK);
    pdf::PDFDocumentReader::Result withPassword = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument reopened = H::read(written, password, &withPassword);
    QCOMPARE(withPassword, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(H::draw(reopened), H::draw(opened));

    // Without the permission Modify the tool refuses (ISO 32000-2 Table 22)
    pdf::PDFDocumentBuilder restrictedBuilder(&plain);
    settings.userPassword = QString();
    settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Modify);
    restrictedBuilder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocument restricted = H::read(H::write(restrictedBuilder.build()));
    const pdf::PDFDocumentToolResult refused = tool.run(H::createContext(restricted), H::steps(true), nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.documents.empty());
}

void DocumentToolStructureTest::signedInput()
{
    // O31: the tool works on a signed document, the impact LostCopyOnly is reported, the fields stay
    using H = StructureTestHelper;
    pdf::PDFDocument base = H::createDocument(nullptr);
    pdf::PDFDocumentBuilder builder(&base);
    pdf::PDFDictionaryBuilder signature;
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Sig"));
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("Adobe.PPKLite"));
    const pdf::PDFObjectReference signatureValue = builder.addObject(pdf::PDFObject::createDictionary(std::move(signature)));
    builder.createSignatureField(QStringLiteral("Signature1"), signatureValue, base.getCatalog()->getPage(0)->getPageReference());
    const pdf::PDFDocument document = builder.build();
    QCOMPARE(pdf::PDFForm::getSignatureCount(&document), 1);

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.data.value(QStringLiteral("signatureCount")).toInt(), 1);
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QCOMPARE(pdf::PDFForm::getSignatureCount(result.documents.front().document.data()), 1);
}

void DocumentToolStructureTest::conformanceRemoved()
{
    // The file is rewritten and not checked against the standard: the declaration goes, the fact says it
    using H = StructureTestHelper;
    const QByteArray xmp("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
                         "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                         "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                         "<pdfaid:part>1</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance><dc:creator>Kept Author</dc:creator>"
                         "</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>");
    pdf::PDFDocument base = H::createDocument(nullptr);
    pdf::PDFDocumentBuilder builder(&base);
    builder.setCatalogMetadata(xmp);
    const pdf::PDFDocument input = builder.build();
    QVERIFY(pdf::PDFOCRPagePreparer::hasConformanceDeclaration(&input, nullptr));

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    const pdf::PDFDocument output = H::read(H::write(*result.documents.front().document));
    QVERIFY(!pdf::PDFOCRPagePreparer::hasConformanceDeclaration(&output, nullptr));
    QVERIFY(H::factOf(result.facts, QStringLiteral("conformance")).contains(QStringLiteral("no longer declares PDF/A")));
    QCOMPARE(result.data.value(QStringLiteral("conformanceRemoved")).toArray().first().toString(), QStringLiteral("PDF/A"));
    QVERIFY(pdf::PDFDocumentToolStructure::createReport(result).contains(QStringLiteral("no longer declares PDF/A")));

    // A document without a declaration has no such fact
    const pdf::PDFDocumentToolResult plain = tool.run(H::createContext(base), H::steps(true), nullptr, nullptr);
    QVERIFY(H::factOf(plain.facts, QStringLiteral("conformance")).isEmpty());
}

void DocumentToolStructureTest::conformanceNotRemovable()
{
    // Metadata, which cannot be changed, would keep a declaration, which is not true: nothing is made
    using H = StructureTestHelper;
    pdf::PDFDocument base = H::createDocument(nullptr);
    pdf::PDFDocumentBuilder builder(&base);
    builder.setCatalogMetadata("<broken http://www.aiim.org/pdfa/ns/id/ no closing tag");
    const pdf::PDFDocument input = builder.build();
    QVERIFY(pdf::PDFOCRPagePreparer::hasConformanceDeclaration(&input, nullptr));

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());
    QVERIFY2(result.message.contains(QStringLiteral("metadata cannot be changed")), qPrintable(result.message));
}

void DocumentToolStructureTest::eightThreadsOneInstance()
{
    // One instance serves all runs at once (the runner, MCP): a local optimizer per run
    using H = StructureTestHelper;
    const pdf::PDFDocument input = H::createDocument(nullptr);
    const pdf::PDFDocumentToolStructure tool;
    const qint64 expected = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr).data.value(QStringLiteral("sizeAfter")).toInteger();
    QVERIFY(expected > 0);

    std::atomic<int> same = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&tool, &input, &same, expected]()
        {
            const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
            same += result.isSuccess() && result.data.value(QStringLiteral("sizeAfter")).toInteger() == expected ? 1 : 0;
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    QCOMPARE(same.load(), 8);
}

void DocumentToolStructureTest::hostileReferences()
{
    // Plan 7: references to objects, which do not exist, and an array, which contains itself
    using H = StructureTestHelper;
    const pdf::PDFDocument base = H::createBaseDocument();
    pdf::PDFObjectStorage storage = base.getStorage();
    const pdf::PDFObjectReference cycle = storage.addObject(pdf::PDFObject());
    pdf::PDFObjectStorage::PDFObjects objects = storage.getObjects();
    pdf::PDFArrayBuilder array;
    array.appendItem(pdf::PDFObject::createReference(cycle));
    array.appendItem(pdf::PDFObject::createReference(pdf::PDFObjectReference(99999, 0)));
    objects[size_t(cycle.objectNumber)].object = pdf::PDFObject::createArray(std::move(array));
    pdf::PDFObjectStorage::Entry& catalogEntry = objects[size_t(base.getTrailerDictionary()->get("Root").getReference().objectNumber)];
    pdf::PDFDictionaryBuilder catalog(*catalogEntry.object.getDictionary());
    catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTCycle"), pdf::PDFObject::createReference(cycle));
    catalog.setEntry(pdf::PDFInplaceOrMemoryString("PDF4QTMissing"), pdf::PDFObject::createReference(pdf::PDFObjectReference(88888, 0)));
    catalogEntry.object = pdf::PDFObject::createDictionary(std::move(catalog));
    storage.setObjects(std::move(objects));
    const pdf::PDFDocument input(std::move(storage), pdf::PDFVersion(1, 7), QByteArray());

    const pdf::PDFDocumentToolStructure tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(input), H::steps(true), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    const pdf::PDFDocument output = H::read(H::write(*result.documents.front().document));
    QCOMPARE(output.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(H::draw(output), H::draw(input));
}

QTEST_MAIN(DocumentToolStructureTest)

#include "tst_documenttoolstructuretest.moc"
