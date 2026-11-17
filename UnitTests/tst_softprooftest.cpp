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

// Soft Proofing of the core (step 7 of phase 2, plan 4.7, 8; package P3b): the settings without the proof
// (Q7.10), the context of Little CMS per instance (B200, R7-16), the list of the profiles of the proof with
// the reasons (FP-6, Q7.11), the tool (the share and the area out of the gamut).
//
// The CMYK profile of the tests is written by icctestprofile.h (ICC.1:2001-04, a version 2 output profile with the
// tags desc, wtpt, A2B0 and B2A0 as lut16Type): its gamut is the neutral axis and the chroma |a*|, |b*| <= 40
// (L* = 100 (1 - K), a* = 40 (2 M - 1), b* = 40 (2 Y - 1), cyan unused). So a neutral gray is in the gamut and
// the pure green of sRGB (a* about -79, b* about +81) is out of it - by the definition of the profile, not by a
// run of the code.

#include "icctestprofile.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_softproof.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfoperationcontrol.h"
#include "testlocaleguard.h"

#include <QElapsedTimer>
#include <QtTest>
#include <QJsonArray>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <map>
#include <thread>

/// The ICC profiles of the tests: the CMYK profile of UnitTests/icctestprofile.h (the one profile of the tests
/// of soft proofing, its gamut in the file comment) and profiles of other color spaces made of it
class SoftProofTestProfile
{
public:
    SoftProofTestProfile() = delete;

    /// A CMYK output profile (class prtr, PCS Lab) of the gamut of the file comment
    static QByteArray createCmykProfile(const QByteArray& description)
    {
        return pdftest::IccTestProfile::createCmykProfile(description);
    }

    /// A profile of the color space \p colorSpace ("RGB ", "GRAY") with the description \p description: enough
    /// for the classification by the data color space of the header (ICC.1:2001-04, 6.1). It is the CMYK profile
    /// with the header of a display profile - the device class (bytes 12-15) "mntr", the color space of the data
    /// (bytes 16-19) \p colorSpace, the connection space (bytes 20-23) "XYZ "; its tags are never used to convert
    static QByteArray createOtherProfile(const QByteArray& colorSpace, const QByteArray& description)
    {
        QByteArray profile = createCmykProfile(description);
        profile.replace(12, 4, QByteArray("mntr"));
        profile.replace(16, 4, colorSpace);
        profile.replace(20, 4, QByteArray("XYZ "));
        return profile;
    }
};

/// Operation control, which reports the operation as canceled from the beginning
class SoftProofTestCanceled : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return true; }
};

class SoftProofTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void settingsWithoutProofing();
    void alarmColorPerInstance();
    void alarmColorFromThreads();
    void outputIntentProfiles();
    void outputIntentCountLimited();
    void toolMeasuresGamut();
    void toolProfileOfFolder();
    void toolFailures();
    void toolStopped();
    void toolDistinctRenderingErrors();
    void toolDefaultAlarmColorAndFacts();

private:
    /// An output intent of the test documents
    struct OutputIntent
    {
        QByteArray conditionIdentifier;     ///< /OutputConditionIdentifier (empty = none)
        QByteArray profile;                 ///< Data of the stream /DestOutputProfile (empty = none)
        bool isReference = false;           ///< /DestOutputProfileRef instead of a profile (PDF 2.0)
    };

    /// A document of pages 100 x 100 pt with the contents and the output intents
    static pdf::PDFDocumentPointer createDocument(const std::vector<QByteArray>& contents, const std::vector<OutputIntent>& outputIntents);

    /// Settings of Little CMS with the profile folder of the test
    pdf::PDFCMSSettings createSettings() const;

    /// The identifier of the CMYK profile of the folder of the test
    QString getFolderProfileId() const;

    /// Runs the tool
    static pdf::PDFDocumentToolResult runTool(const pdf::PDFDocumentPointer& document, const QVariantMap& parameters, const std::optional<pdf::PDFCMSSettings>& settings,
                                              const pdf::PDFOperationControl* control = nullptr);

    /// The color of the pure green of sRGB by \p cms (the float path and the path of 8-bit buffers)
    static QRgb convertGreen(const pdf::PDFCMS* cms, bool isBuffer);

    QTemporaryDir m_profileDirectory;
    QByteArray m_cmykProfile;
};

void SoftProofTest::initTestCase()
{
    QVERIFY(m_profileDirectory.isValid());
    m_cmykProfile = SoftProofTestProfile::createCmykProfile("Test Printing Gray");

    QFile file(m_profileDirectory.filePath(QStringLiteral("TestPrinting.icc")));
    QVERIFY(file.open(QFile::WriteOnly));
    QCOMPARE(file.write(m_cmykProfile), qint64(m_cmykProfile.size()));
    file.close();
}

pdf::PDFDocumentPointer SoftProofTest::createDocument(const std::vector<QByteArray>& contents, const std::vector<OutputIntent>& outputIntents)
{
    pdf::PDFDocumentBuilder builder;
    for (const QByteArray& content : contents)
    {
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 100, 100));
        pdf::PDFDictionaryBuilder contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(contentDictionary), QByteArray(content))));
        pdf::PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
        builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    }

    if (!outputIntents.empty())
    {
        pdf::PDFArrayBuilder intents;
        for (const OutputIntent& outputIntent : outputIntents)
        {
            // ISO 32000-2, 14.11.5, Table 401
            pdf::PDFDictionaryBuilder intent;
            intent.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("OutputIntent"));
            intent.addEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("GTS_PDFX"));
            if (!outputIntent.conditionIdentifier.isEmpty())
            {
                intent.addEntry(pdf::PDFInplaceOrMemoryString("OutputConditionIdentifier"), pdf::PDFObject::createString(outputIntent.conditionIdentifier));
            }
            if (!outputIntent.profile.isEmpty())
            {
                pdf::PDFDictionaryBuilder profileDictionary;
                profileDictionary.addEntry(pdf::PDFInplaceOrMemoryString("N"), pdf::PDFObject::createInteger(4));
                profileDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(outputIntent.profile.size()));
                const pdf::PDFObjectReference profileReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(profileDictionary), QByteArray(outputIntent.profile))));
                intent.addEntry(pdf::PDFInplaceOrMemoryString("DestOutputProfile"), pdf::PDFObject::createReference(profileReference));
            }
            if (outputIntent.isReference)
            {
                pdf::PDFDictionaryBuilder reference;
                reference.addEntry(pdf::PDFInplaceOrMemoryString("ProfileName"), pdf::PDFObject::createString("Coated FOGRA39"));
                intent.addEntry(pdf::PDFInplaceOrMemoryString("DestOutputProfileRef"), pdf::PDFObject::createDictionary(std::move(reference)));
            }
            intents.appendItem(pdf::PDFObject::createDictionary(std::move(intent)));
        }

        pdf::PDFDictionaryBuilder catalogUpdate;
        catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("OutputIntents"), pdf::PDFObject::createArray(std::move(intents)));
        builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalogUpdate)));
    }

    return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
}

pdf::PDFCMSSettings SoftProofTest::createSettings() const
{
    pdf::PDFCMSManager manager(nullptr);
    pdf::PDFCMSSettings settings = manager.getDefaultSettings();
    settings.system = pdf::PDFCMSSettings::System::LittleCMS2;
    settings.profileDirectory = m_profileDirectory.path();
    settings.intent = pdf::RenderingIntent::RelativeColorimetric;
    return settings;
}

QString SoftProofTest::getFolderProfileId() const
{
    pdf::PDFCMSManager manager(nullptr);
    manager.setSettings(createSettings());
    for (const pdf::PDFSoftProofingProfile& profile : manager.getSoftProofingProfiles())
    {
        if (profile.source == pdf::PDFSoftProofingProfile::Source::ProfileFolder && profile.fileName == QLatin1String("TestPrinting.icc"))
        {
            return profile.id;
        }
    }
    return QString();
}

pdf::PDFDocumentToolResult SoftProofTest::runTool(const pdf::PDFDocumentPointer& document, const QVariantMap& parameters, const std::optional<pdf::PDFCMSSettings>& settings, const pdf::PDFOperationControl* control)
{
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, QByteArray(), QStringLiteral("Poster.pdf"), true));
    context.rendering.cmsSettings = settings;
    pdf::PDFDocumentToolSoftProof tool;
    return tool.run(context, parameters, control, nullptr);
}

QRgb SoftProofTest::convertGreen(const pdf::PDFCMS* cms, bool isBuffer)
{
    pdf::PDFRenderErrorReporterDummy reporter;
    if (isBuffer)
    {
        unsigned char rgb[3] = { };
        if (!cms->fillRGBBufferFromDeviceRGB({ 0.0f, 1.0f, 0.0f }, pdf::RenderingIntent::RelativeColorimetric, rgb, &reporter))
        {
            return 0;
        }
        return qRgb(rgb[0], rgb[1], rgb[2]);
    }
    return cms->getColorFromDeviceRGB(pdf::PDFColor(0.0f, 1.0f, 0.0f), pdf::RenderingIntent::RelativeColorimetric, &reporter).rgb();
}

void SoftProofTest::settingsWithoutProofing()
{
    // Q7.10: a copy of the settings of a tab for the printing, the exports and the previews has no proof
    // and no gamut check; everything else of the color management stays
    pdf::PDFCMSSettings settings;
    settings.system = pdf::PDFCMSSettings::System::LittleCMS2;
    settings.isSoftProofing = true;
    settings.isGamutChecking = true;
    settings.softProofingProfile = QStringLiteral("FOGRA39");
    settings.proofingIntent = pdf::RenderingIntent::AbsoluteColorimetric;
    settings.outOfGamutColor = Qt::green;
    settings.deviceCMYK = QStringLiteral("cmyk");
    settings.intent = pdf::RenderingIntent::Perceptual;

    const pdf::PDFCMSSettings copy = settings.withoutProofing();
    QVERIFY(!copy.isSoftProofing);
    QVERIFY(!copy.isGamutChecking);
    QCOMPARE(copy.system, pdf::PDFCMSSettings::System::LittleCMS2);
    QCOMPARE(copy.deviceCMYK, QStringLiteral("cmyk"));
    QCOMPARE(copy.intent, pdf::RenderingIntent::Perceptual);

    // The values of the proof only get their defaults: the copy is the settings without any proof
    pdf::PDFCMSSettings expected = settings;
    expected.isSoftProofing = false;
    expected.isGamutChecking = false;
    expected.softProofingProfile = pdf::PDFCMSSettings().softProofingProfile;
    expected.proofingIntent = pdf::PDFCMSSettings().proofingIntent;
    expected.outOfGamutColor = pdf::PDFCMSSettings().outOfGamutColor;
    QVERIFY(copy == expected);

    // Two states of the proof of a tab give equal copies: the thumbnails and the printing keep their
    // color management, when the proof changes (PDFCMSManager::setSettings compares the settings)
    pdf::PDFCMSSettings otherProof = settings;
    otherProof.isSoftProofing = false;
    otherProof.softProofingProfile = QStringLiteral("Japan Color");
    otherProof.proofingIntent = pdf::RenderingIntent::Perceptual;
    otherProof.outOfGamutColor = Qt::magenta;
    QVERIFY(otherProof.withoutProofing() == copy);

    // The original is unchanged
    QVERIFY(settings.isSoftProofing);
    QVERIFY(settings.isGamutChecking);
    QCOMPARE(settings.softProofingProfile, QStringLiteral("FOGRA39"));
}

void SoftProofTest::alarmColorPerInstance()
{
    // B200: the alarm color is a value of the color management, not of the process. The second color
    // management is created after the first one - with the global alarm codes it would overwrite them.
    const QString profileId = getFolderProfileId();
    QVERIFY(!profileId.isEmpty());

    pdf::PDFCMSSettings settings = createSettings();
    settings.isGamutChecking = true;
    settings.softProofingProfile = profileId;

    pdf::PDFCMSSettings redSettings = settings;
    redSettings.outOfGamutColor = QColor(255, 0, 0);
    pdf::PDFCMSSettings blueSettings = settings;
    blueSettings.outOfGamutColor = QColor(0, 0, 255);

    pdf::PDFCMSManager redManager(nullptr);
    redManager.setSettings(redSettings);
    const pdf::PDFCMSPointer red = redManager.getCurrentCMS();
    pdf::PDFCMSManager blueManager(nullptr);
    blueManager.setSettings(blueSettings);
    const pdf::PDFCMSPointer blue = blueManager.getCurrentCMS();

    for (const bool isBuffer : { false, true })
    {
        // The pure green of sRGB is out of the gamut of the profile: each marks it by its own color
        QCOMPARE(convertGreen(red.data(), isBuffer), qRgb(255, 0, 0));
        QCOMPARE(convertGreen(blue.data(), isBuffer), qRgb(0, 0, 255));
        QCOMPARE(convertGreen(red.data(), isBuffer), qRgb(255, 0, 0));
    }

    // A neutral gray is in the gamut: it is not marked
    pdf::PDFRenderErrorReporterDummy reporter;
    const QColor gray = red->getColorFromDeviceGray(pdf::PDFColor(0.5f), pdf::RenderingIntent::RelativeColorimetric, &reporter);
    QVERIFY(gray.isValid());
    QCOMPARE(gray.red(), gray.green());
    QCOMPARE(gray.green(), gray.blue());

    // Without the gamut check the green is not marked at all
    pdf::PDFCMSManager plainManager(nullptr);
    plainManager.setSettings(redSettings.withoutProofing());
    const QRgb plainGreen = convertGreen(plainManager.getCurrentCMS().data(), false);
    QVERIFY(qGreen(plainGreen) > 200);
    QVERIFY(qRed(plainGreen) < 100);
}

void SoftProofTest::alarmColorFromThreads()
{
    // B200 from more threads at once (the coordinator runs it under TSan): each thread creates its own color
    // management with its own alarm color and converts with it, while the others create and convert theirs
    const QString profileId = getFolderProfileId();
    QVERIFY(!profileId.isEmpty());
    pdf::PDFCMSSettings settings = createSettings();
    settings.isGamutChecking = true;
    settings.softProofingProfile = profileId;

    constexpr int threadCount = 8;
    constexpr int conversionCount = 200;
    std::atomic<int> wrongColors = 0;
    std::atomic<int> done = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&, i]()
        {
            pdf::PDFCMSSettings threadSettings = settings;
            threadSettings.outOfGamutColor = QColor(i * 30, 255 - i * 30, 128);
            const QRgb expected = threadSettings.outOfGamutColor.rgb();

            for (int round = 0; round < 3; ++round)
            {
                pdf::PDFCMSManager manager(nullptr);
                manager.setSettings(threadSettings);
                const pdf::PDFCMSPointer cms = manager.getCurrentCMS();
                for (int j = 0; j < conversionCount; ++j)
                {
                    if (convertGreen(cms.data(), (j % 2) == 0) != (expected | 0xFF000000u))
                    {
                        ++wrongColors;
                    }
                }
            }
            ++done;
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    QCOMPARE(done.load(), threadCount);
    QCOMPARE(wrongColors.load(), 0);
}

void SoftProofTest::outputIntentProfiles()
{
    // FP-6: every output intent is a row of the menu of the proof, in the order of the document; only a CMYK
    // profile can be the profile of a proof (14.11.5), the others are there with their reasons
    std::vector<OutputIntent> intents;
    intents.push_back({ "FOGRA39", QByteArray(), false });                                                       // the name only
    intents.push_back({ "sRGB IEC61966-2.1", SoftProofTestProfile::createOtherProfile("RGB ", "Test RGB"), false });   // RGB (PDF/A)
    intents.push_back({ "Test CMYK", m_cmykProfile, false });                                                   // usable
    intents.push_back({ "Gray", SoftProofTestProfile::createOtherProfile("GRAY", "Test Gray"), false });        // gray
    intents.push_back({ "Damaged", QByteArray("this is no ICC profile"), false });                              // damaged
    intents.push_back({ QByteArray(), QByteArray(), true });                                                    // PDF 2.0 reference
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray("0.5 g 0 0 100 100 re f") }, intents);

    pdf::PDFCMSManager manager(nullptr);
    manager.setDocument(document.data());
    manager.setSettings(createSettings());
    const pdf::PDFSoftProofingProfiles profiles = manager.getSoftProofingProfiles();
    QVERIFY(profiles.size() >= intents.size() + 1);

    using Source = pdf::PDFSoftProofingProfile::Source;
    for (size_t i = 0; i < intents.size(); ++i)
    {
        QCOMPARE(profiles[i].source, Source::OutputIntent);
        QVERIFY(profiles[i].fileName.isEmpty());
        QCOMPARE(profiles[i].isAvailable, i == 2);
        QCOMPARE(profiles[i].id.isEmpty(), i != 2);
        QCOMPARE(profiles[i].reason.isEmpty(), i == 2);
    }

    // The names: the description of the profile, else the printing condition
    QCOMPARE(profiles[0].name, QStringLiteral("FOGRA39"));
    QCOMPARE(profiles[1].name, QStringLiteral("Test RGB"));
    QCOMPARE(profiles[2].name, QStringLiteral("Test Printing Gray"));
    QCOMPARE(profiles[2].outputCondition, QStringLiteral("Test CMYK"));
    QCOMPARE(profiles[4].name, QStringLiteral("Damaged"));
    QCOMPARE(profiles[5].name, QStringLiteral("Output intent 6"));

    // The reasons (the kinds differ)
    QVERIFY(profiles[0].reason.contains(QStringLiteral("printing condition only")));
    QVERIFY(profiles[1].reason.contains(QStringLiteral("RGB")));
    QVERIFY(profiles[3].reason.contains(QStringLiteral("gray")));
    QVERIFY(profiles[4].reason.contains(QStringLiteral("damaged")));
    QVERIFY(profiles[5].reason.contains(QStringLiteral("outside of the document")));

    // An empty profile of the settings is the first available output intent, not the first one
    pdf::PDFCMSSettings settings = createSettings();
    QCOMPARE(manager.getEffectiveSoftProofingProfile(settings), profiles[2].id);
    settings.softProofingProfile = QStringLiteral("other");
    QCOMPARE(manager.getEffectiveSoftProofingProfile(settings), QStringLiteral("other"));

    // The profile of the folder of the settings follows the output intents, with its description and file
    auto folderProfile = std::find_if(profiles.cbegin(), profiles.cend(), [](const pdf::PDFSoftProofingProfile& profile) { return profile.fileName == QLatin1String("TestPrinting.icc"); });
    QVERIFY(folderProfile != profiles.cend());
    QCOMPARE(folderProfile->source, Source::ProfileFolder);
    QCOMPARE(folderProfile->name, QStringLiteral("Test Printing Gray"));
    QVERIFY(folderProfile->isAvailable);
    QVERIFY(!folderProfile->id.isEmpty());

    // Only CMYK profiles after the output intents (Q7.11: no built-in profiles)
    for (size_t i = intents.size(); i < profiles.size(); ++i)
    {
        QVERIFY(profiles[i].source != Source::OutputIntent);
        QVERIFY(profiles[i].isAvailable);
    }

    // A document without output intents: none of them
    pdf::PDFCMSManager emptyManager(nullptr);
    emptyManager.setDocument(createDocument({ QByteArray() }, {}).data());
    emptyManager.setSettings(createSettings());
    for (const pdf::PDFSoftProofingProfile& profile : emptyManager.getSoftProofingProfiles())
    {
        QVERIFY(profile.source != Source::OutputIntent);
    }
    QVERIFY(emptyManager.getEffectiveSoftProofingProfile(createSettings()).isEmpty());
}

void SoftProofTest::outputIntentCountLimited()
{
    // Hostile input: a document with more output intents than any standard needs - at most
    // MAXIMUM_OUTPUT_INTENTS are read (each would decode its profile)
    std::vector<OutputIntent> intents(pdf::PDFCMSManager::MAXIMUM_OUTPUT_INTENTS + 8, OutputIntent{ "Test CMYK", m_cmykProfile, false });
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray() }, intents);

    pdf::PDFCMSManager manager(nullptr);
    manager.setDocument(document.data());
    manager.setSettings(createSettings());
    size_t outputIntentCount = 0;
    for (const pdf::PDFSoftProofingProfile& profile : manager.getSoftProofingProfiles())
    {
        if (profile.source == pdf::PDFSoftProofingProfile::Source::OutputIntent)
        {
            ++outputIntentCount;
        }
    }
    QCOMPARE(outputIntentCount, pdf::PDFCMSManager::MAXIMUM_OUTPUT_INTENTS);
}

void SoftProofTest::toolMeasuresGamut()
{
    // Page 1: a neutral gray page (in the gamut) with a pure green square of 50 x 50 pt (out of the gamut) -
    // a quarter of the crop box, (50 x 25.4 / 72)² = 311.13 mm²; page 2: the gray only. The output intent of
    // the document is the profile (an empty parameter "profile").
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray("0.5 g 0 0 100 100 re f 0 1 0 rg 0 0 50 50 re f"), QByteArray("0.5 g 0 0 100 100 re f") },
                                                            { { "Test CMYK", m_cmykProfile, false } });

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_RESOLUTION), 100);
    const pdf::PDFDocumentToolResult result = runTool(document, parameters, createSettings());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QJsonArray pages = result.data.value(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::DATA_PAGES)).toArray();
    QCOMPARE(pages.size(), 2);
    const QJsonObject first = pages.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("page")).toInt(), 1);
    QVERIFY2(std::abs(first.value(QStringLiteral("share")).toDouble() - 25.0) < 0.01, qPrintable(QString::number(first.value(QStringLiteral("share")).toDouble())));
    const double expectedArea = std::pow(50.0 * 25.4 / 72.0, 2.0);
    QVERIFY2(std::abs(first.value(QStringLiteral("area")).toDouble() - expectedArea) < 0.05, qPrintable(QString::number(first.value(QStringLiteral("area")).toDouble())));

    const QJsonObject second = pages.at(1).toObject();
    QCOMPARE(second.value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(second.value(QStringLiteral("share")).toDouble(), 0.0);
    QCOMPARE(second.value(QStringLiteral("area")).toDouble(), 0.0);

    QCOMPARE(result.data.value(QStringLiteral("pagesOutOfGamut")).toInt(), 1);
    QCOMPARE(result.data.value(QStringLiteral("profile")).toString(), QStringLiteral("Test Printing Gray"));
    QCOMPARE(result.data.value(QStringLiteral("intent")).toString(), QStringLiteral("relative-colorimetric"));
    QVERIFY(result.renderingIssues.empty());

    // Only page 2 selected: nothing out of the gamut
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PAGES), QStringLiteral("2"));
    const pdf::PDFDocumentToolResult secondOnly = runTool(document, parameters, createSettings());
    QVERIFY2(secondOnly.isSuccess(), qPrintable(secondOnly.message));
    QCOMPARE(secondOnly.data.value(QStringLiteral("pages")).toArray().size(), 1);
    QCOMPARE(secondOnly.data.value(QStringLiteral("pagesOutOfGamut")).toInt(), 0);
}

void SoftProofTest::toolProfileOfFolder()
{
    // The profile of the profile folder (by its identifier) on a document without output intents; MCP and
    // PdfTool without settings of the application use the default settings - here the folder is given
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray("1 g 0 0 100 100 re f 0 1 0 rg 0 0 100 50 re f") }, {});
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PROFILE), getFolderProfileId());
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_RESOLUTION), 100);
    const pdf::PDFDocumentToolResult result = runTool(document, parameters, createSettings());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    // The green half of the page (the white paper and the white fill are in the gamut)
    const QJsonObject page = result.data.value(QStringLiteral("pages")).toArray().at(0).toObject();
    QVERIFY2(std::abs(page.value(QStringLiteral("share")).toDouble() - 50.0) < 0.01, qPrintable(QString::number(page.value(QStringLiteral("share")).toDouble())));
}

void SoftProofTest::toolFailures()
{
    const pdf::PDFDocumentPointer noIntent = createDocument({ QByteArray("0 1 0 rg 0 0 50 50 re f") }, {});

    // No output intent and no profile given: Failed, the text points to the settings
    pdf::PDFDocumentToolResult result = runTool(noIntent, QVariantMap(), createSettings());
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("Color management")));

    // An RGB output intent only: Failed with its reason (FP-6)
    const pdf::PDFDocumentPointer rgbIntent = createDocument({ QByteArray() }, { { "sRGB", SoftProofTestProfile::createOtherProfile("RGB ", "Test RGB"), false } });
    result = runTool(rgbIntent, QVariantMap(), createSettings());
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("RGB")));

    // An unknown profile
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PROFILE), QStringLiteral("no-such-profile"));
    result = runTool(noIntent, parameters, createSettings());
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("no-such-profile")));

    // The generic color management cannot check the gamut
    pdf::PDFCMSSettings generic = createSettings();
    generic.system = pdf::PDFCMSSettings::System::Generic;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PROFILE), getFolderProfileId());
    result = runTool(noIntent, parameters, generic);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("Little CMS")));

    // More than 50 pages (plan 7.1): an error of the parameter pages; 50 pass the validation
    std::vector<QByteArray> contents(pdf::PDFDocumentToolSoftProof::MAXIMUM_PAGES + 1, QByteArray());
    const pdf::PDFDocumentPointer large = createDocument(contents, {});
    pdf::PDFDocumentToolSoftProof tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(large, QByteArray(), QStringLiteral("Large.pdf"), true));
    QVariantMap all;
    std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(context, all);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PAGES));
    all.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_PAGES), QStringLiteral("2-51"));
    QVERIFY(tool.validate(context, all).empty());
}

void SoftProofTest::toolStopped()
{
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray("0 1 0 rg 0 0 50 50 re f") }, { { "Test CMYK", m_cmykProfile, false } });
    SoftProofTestCanceled canceled;
    const pdf::PDFDocumentToolResult result = runTool(document, QVariantMap(), createSettings(), &canceled);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.data.isEmpty());
}

void SoftProofTest::toolDistinctRenderingErrors()
{
    // Hostile input (RS7COLOR): a page of 100 000 operators, which are not known (ISO 32000-2, 7.8.2), each of
    // another name of the same length, and a green square out of the gamut. Each error is one issue of the page,
    // the gamut is still measured (a quarter of the page), and the run ends in a time proportional to the
    // errors - one search in the list of the issues for each of them was quadratic (minutes)
    constexpr int ERROR_COUNT = 100000;
    QByteArray content = "0.5 g 0 0 100 100 re f 0 1 0 rg 0 0 50 50 re f\n";
    content.reserve(content.size() + ERROR_COUNT * 9);
    for (int i = 0; i < ERROR_COUNT; ++i)
    {
        content += 'u' + QByteArray::number(i).rightJustified(7, '0') + ' ';
    }
    const pdf::PDFDocumentPointer document = createDocument({ content }, { { "Test CMYK", m_cmykProfile, false } });

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_RESOLUTION), 100);
    QElapsedTimer timer;
    timer.start();
    const pdf::PDFDocumentToolResult result = runTool(document, parameters, createSettings());
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QString unknownOperator = pdf::PDFTranslationContext::tr("Unknown operator '%1'.");
    qsizetype unknownOperators = 0;
    bool hasLast = false;
    for (const pdf::PDFDocumentToolPageProblem& problem : result.renderingIssues)
    {
        QCOMPARE(problem.pageIndex, pdf::PDFInteger(0));
        unknownOperators += problem.message.startsWith(unknownOperator.left(unknownOperator.indexOf(QLatin1Char('\'')))) ? 1 : 0;
        hasLast = hasLast || problem.message == unknownOperator.arg(QStringLiteral("u0099999"));
    }
    QCOMPARE(unknownOperators, qsizetype(ERROR_COUNT));
    QVERIFY(hasLast);

    const QJsonObject page = result.data.value(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::DATA_PAGES)).toArray().at(0).toObject();
    QVERIFY2(std::abs(page.value(QStringLiteral("share")).toDouble() - 25.0) < 0.01, qPrintable(QString::number(page.value(QStringLiteral("share")).toDouble())));

    const qint64 limit = 20000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void SoftProofTest::toolDefaultAlarmColorAndFacts()
{
    // RS7FIGMA M11: the default alarm color is the swatch "Red" of the color row (479:11760, 479:15922) - the
    // canonical annotation red #FF3B30 of Output Preview, not #FF0000
    const pdf::PDFDocumentToolSoftProof tool;
    const QVariantMap defaults = pdf::PDFDocumentToolParameters::getDefaultValues(tool.getParameters());
    QCOMPARE(defaults.value(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_ALARM_COLOR)).value<QColor>().name(QColor::HexRgb), QStringLiteral("#ff3b30"));

    // The fix wave of step 7: the facts are machine-readable (MCP, PdfTool) - the C locale, whatever the default
    // locale is. The document of toolMeasuresGamut: page 1 has a quarter out of the gamut, page 2 nothing.
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());
    const pdf::PDFDocumentPointer document = createDocument({ QByteArray("0.5 g 0 0 100 100 re f 0 1 0 rg 0 0 50 50 re f"), QByteArray("0.5 g 0 0 100 100 re f") },
                                                            { { "Test CMYK", m_cmykProfile, false } });
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolSoftProof::PARAMETER_RESOLUTION), 100);
    const pdf::PDFDocumentToolResult result = runTool(document, parameters, createSettings());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    std::map<QString, QString> facts;
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts["pagesOutOfGamut"], QStringLiteral("1 of 2"));
    QCOMPARE(facts["largestShare"], QStringLiteral("25.0 % on page 1"));
}

QTEST_MAIN(SoftProofTest)

#include "tst_softprooftest.moc"
