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

// The tools Optimize Images and Convert Images to Grayscale (step 4 of phase 2, package P1; plan 8,
// row Optimize Images, Grayscale - the program of plan 8 "UnitTestsDocumentToolImages" is Images to
// PDF of step 3a already): the fixed parameters of plan 7, R4-20 and chapter 12; the syntax of the
// exceptions; an excluded image keeps its stream byte by byte, an override changes only its image;
// Grayscale converts RGB to DeviceGray and skips gray and bitonal images by their declaration (R4-5),
// Target DPI 0 keeps the pixels, a color image is converted also when its gray encoding is larger
// (Q4.15 a); the keys of ISO 32000-2 Table 87 stay, /Alternates goes; a soft mask shared with an
// excluded image is never replaced in place (B62); the images of a hidden layer are converted, an
// image only in an appearance is not (R4-4); a declaration of billions of pixels is "too large";
// the facts of 301:4019 and 308:7341.

#include "pdfdocumentbuilder.h"
#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool_grayscale.h"
#include "pdfdocumenttool_optimizeimages.h"
#include "pdfparser.h"

#include <QBuffer>
#include <QImageWriter>
#include <QJsonArray>
#include <QtTest>

#include <vector>

/// A document of images built from objects written as text
class OptimizeImagesDocument
{
public:
    static pdf::PDFObject parse(const QByteArray& text)
    {
        pdf::PDFParser parser(text, nullptr, pdf::PDFParser::None);
        return parser.getObject();
    }

    static QByteArray ref(pdf::PDFObjectReference reference)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R";
    }

    static QString key(pdf::PDFObjectReference reference)
    {
        return pdf::PDFDocumentImageList::Key::createReference(reference).toString();
    }

    pdf::PDFObjectReference addStream(const QByteArray& dictionaryText, QByteArray data)
    {
        const pdf::PDFObject dictionaryObject = parse(dictionaryText);
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
    }

    /// An uncompressed RGB image of 16 x 16 pixels in two colors (red and blue halves)
    pdf::PDFObjectReference addColorImage(const QByteArray& extra = QByteArray())
    {
        QByteArray data;
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                data += x < 8 ? QByteArray("\xFF\x00\x00", 3) : QByteArray("\x00\x00\xFF", 3);
            }
        }
        return addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceRGB /BitsPerComponent 8 " + extra + " >>", data);
    }

    /// An uncompressed gray image of 16 x 16 pixels
    pdf::PDFObjectReference addGrayImage()
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(256, '\x80'));
    }

    /// A bitonal image of 16 x 16 pixels (one bit, 2 bytes per row)
    pdf::PDFObjectReference addBitonalImage()
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 1 >>", QByteArray(32, '\x0F'));
    }

    /// A page drawing the named images, each in its own square of 50 pt
    pdf::PDFObjectReference addPage(const std::vector<pdf::PDFObjectReference>& images)
    {
        QByteArray xobjects;
        QByteArray content;
        for (size_t i = 0; i < images.size(); ++i)
        {
            xobjects += "/Im" + QByteArray::number(qint64(i)) + " " + ref(images[i]) + " ";
            content += "q 50 0 0 50 " + QByteArray::number(qint64(i) * 60) + " 0 cm /Im" + QByteArray::number(qint64(i)) + " Do Q\n";
        }
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
        const pdf::PDFObjectReference contents = addStream("<< >>", content);
        builder.mergeTo(page, parse("<< /Resources << /XObject << " + xobjects + " >> >> /Contents " + ref(contents) + " >>"));
        return page;
    }

    pdf::PDFDocument build() { return builder.build(); }

    pdf::PDFDocumentBuilder builder;
};

class DocumentToolOptimizeImagesTest : public QObject
{
    Q_OBJECT

private slots:
    void optimizeImagesParameters();
    void grayscaleParameters();
    void exceptionSyntax();
    void excludedImageKeepsStream();
    void overrideOnlyForItsImage();
    void overrideModeGoalAndOptions();
    void grayscaleConvertsColorImages();
    void grayscaleConvertsLargerGrayEncoding();
    void grayscaleKeepsDictionaryKeys();
    void sharedSoftMaskIsNotReplaced();
    void maskDrawnAsImageKeepsStream();
    void spotColorsAreSaid();
    void hiddenLayerAndAppearance();
    void tooLargeImageIsAProblem();
    void grayscaleWithoutColorImages();

private:
    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document)
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
    }

    static const pdf::PDFDocument* resultDocument(const pdf::PDFDocumentToolResult& result)
    {
        return result.documents.empty() ? nullptr : result.documents.front().document.data();
    }

    static const pdf::PDFDictionary* imageDictionary(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFObject& object = document->getObjectByReference(reference);
        return object.isStream() ? object.getStream()->getDictionary() : nullptr;
    }

    static QByteArray colorSpace(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFObject& object = document->getObject(imageDictionary(document, reference)->get("ColorSpace"));
        return object.isName() ? object.getString() : QByteArray();
    }

    static QByteArray streamData(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFObject& object = document->getObjectByReference(reference);
        return object.isStream() ? *object.getStream()->getContent() : QByteArray();
    }

    static QString fact(const pdf::PDFDocumentToolResult& result, const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

    static QString imageStatus(const pdf::PDFDocumentToolResult& result, pdf::PDFObjectReference reference)
    {
        for (const QJsonValue& value : result.data.value(QStringLiteral("images")).toArray())
        {
            if (value.toObject().value(QStringLiteral("id")).toString() == OptimizeImagesDocument::key(reference))
            {
                return value.toObject().value(QStringLiteral("status")).toString();
            }
        }
        return QString();
    }
};

void DocumentToolOptimizeImagesTest::optimizeImagesParameters()
{
    // Plan 7: mode, colorMode, goal, keepOriginalIfLarger, preserveTransparency, the three profiles
    // (flattened "<profile><Field>") and images; the preview is the result (Q4.9)
    pdf::PDFDocumentToolOptimizeImages tool;
    QCOMPARE(tool.getId(), QStringLiteral("optimize.images"));
    QVERIFY(tool.getInfo());
    QVERIFY(tool.isPreviewFinal());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    for (const char* name : { "mode", "colorMode", "goal", "keepOriginalIfLarger", "preserveTransparency", "images", "colorAlgorithm", "colorTargetDpi",
                              "colorJpegQuality", "grayResample", "bitonalAlgorithm", "bitonalThreshold" })
    {
        QVERIFY2(pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name)), name);
    }

    // A bitonal profile has no JPEG; its threshold is automatic by default
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("bitonalJpegQuality")));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("bitonalThreshold"))->getDefaultValue().toLongLong(), qint64(-1));
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("images"))->isStored());

    // 299:3362: the rows of the menus Mode and Goal have their icons; Auto and Preserve of Color
    // too (the GUI draws the swatches of the other three); the descriptions of the frame
    QStringList modeRows;
    for (const char* name : { "mode", "goal", "colorMode" })
    {
        for (const pdf::PDFDocumentToolParameterOption& option : pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name))->getOptions())
        {
            modeRows << QString::fromLatin1(name) + QLatin1Char(':') + option.id + QLatin1Char('=') + option.iconName + QLatin1Char('|') + option.getSourceDescription();
        }
    }
    QCOMPARE(modeRows, QStringList({ QStringLiteral("mode:auto=zap|Image analysis picks the color mode and compression"),
                                     QStringLiteral("mode:custom=sliders|Uses the selected color mode and profiles"),
                                     QStringLiteral("goal:prefer-quality=award|Keeps more detail"),
                                     QStringLiteral("goal:minimum-size=minimize-2|Prefers a smaller output"),
                                     QStringLiteral("colorMode:auto=zap|Let the optimizer decide"),
                                     QStringLiteral("colorMode:preserve=lock|Keep the original color characteristics"),
                                     QStringLiteral("colorMode:color=|Force color output"),
                                     QStringLiteral("colorMode:grayscale=|Force grayscale output"),
                                     QStringLiteral("colorMode:bitonal=|Force 1-bit output") }));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("keepOriginalIfLarger"))->getSourceDescription(), QStringLiteral("Leaves the image alone when re-encoding does not shrink it"));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("preserveTransparency"))->getSourceDescription(), QStringLiteral("Stores alpha as a soft mask when possible"));
}

void DocumentToolOptimizeImagesTest::grayscaleParameters()
{
    // R4-20: "exclude"; 308:6911: one gray profile, Target DPI 0 (the resolution unchanged), JPEG 85
    pdf::PDFDocumentToolGrayscale tool;
    QCOMPARE(tool.getId(), QStringLiteral("optimize.grayscale"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("exclude")));
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("images")));
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("colorMode")));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("targetDpi"))->getDefaultValue().toLongLong(), qint64(0));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("jpegQuality"))->getDefaultValue().toLongLong(), qint64(85));

    // Q4.15 a (owner 14. 11.): Keep original if larger of Grayscale says its own meaning, Optimize
    // Images keeps the text of 299:3362
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("keepOriginalIfLarger"))->getDescription(),
             QStringLiteral("Uses the smallest gray encoding; color images are always converted"));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(pdf::PDFDocumentToolOptimizeImages().getParameters(), QStringLiteral("keepOriginalIfLarger"))->getDescription(),
             QStringLiteral("Leaves the image alone when re-encoding does not shrink it"));

    // The settings of Grayscale: gray, only the color images, the one profile in the gray profile
    QVariantMap parameters;
    parameters.insert(QStringLiteral("targetDpi"), 0);
    parameters.insert(QStringLiteral("jpegQuality"), 70);
    const pdf::PDFImageOptimizer::Settings settings = pdf::PDFDocumentToolGrayscale::createGrayscaleSettings(parameters);
    QCOMPARE(settings.colorMode, pdf::PDFImageOptimizer::ColorMode::Grayscale);
    QVERIFY(settings.onlyColorImages);
    QVERIFY(settings.exactJpegQuality);
    QCOMPARE(settings.grayProfile.jpegQuality, 70);
    QCOMPARE(settings.grayProfile.targetDpi, 0);
}

void DocumentToolOptimizeImagesTest::exceptionSyntax()
{
    // The syntax of "images" of the header: "<key>:off" and "<key>:name=value,..."; an inline key has colons
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    QString error;
    const std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(QStringLiteral("148 0:off; 150 0:colorMode=bitonal,algorithm=jbig2,targetDpi=300,threshold=auto; inline:12 0:3:off"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(exceptions.size(), size_t(3));
    QCOMPARE(exceptions[0].key.toString(), QStringLiteral("148 0"));
    QVERIFY(!exceptions[0].enabled);
    QCOMPARE(exceptions[1].settings.value(QStringLiteral("colorMode")).toString(), QStringLiteral("bitonal"));
    QCOMPARE(exceptions[1].settings.value(QStringLiteral("targetDpi")).toInt(), 300);
    QCOMPARE(exceptions[1].settings.value(QStringLiteral("threshold")).toInt(), -1);
    QVERIFY(exceptions[2].key.isInline());

    // The text written back reads the same
    const std::vector<Tool::ImageException> again = Tool::parseImageExceptions(Tool::writeImageExceptions(exceptions), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(again == exceptions);

    // The override of 301:3594 applied: bitonal, JBIG2 at 300 dpi in every profile of the image
    const pdf::PDFImageOptimizer::Settings imageSettings = Tool::createImageSettings(pdf::PDFImageOptimizer::Settings::createDefault(), exceptions[1]);
    QCOMPARE(imageSettings.colorMode, pdf::PDFImageOptimizer::ColorMode::Bitonal);
    QCOMPARE(imageSettings.bitonalProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2);
    QCOMPARE(imageSettings.bitonalProfile.targetDpi, 300);

    // Errors: no key, an unknown setting, a value out of its bounds, settings of an inline image
    for (const QString& invalid : { QStringLiteral("148:off"), QStringLiteral("148 0:size=3"), QStringLiteral("148 0:jpegQuality=101"), QStringLiteral("148 0:"),
                                    QStringLiteral("inline:12 0:3:targetDpi=100"), QStringLiteral("148 0:colorMode=sepia") })
    {
        Tool::parseImageExceptions(invalid, &error);
        QVERIFY2(!error.isEmpty(), qPrintable(invalid));
    }

    // Grayscale: ids separated by commas, R4-20
    const std::vector<Tool::ImageException> excluded = pdf::PDFDocumentToolGrayscale::parseExclude(QStringLiteral("148 0, inline:12 0:3"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(excluded.size(), size_t(2));
    QVERIFY(!excluded[0].enabled && !excluded[1].enabled);
    pdf::PDFDocumentToolGrayscale::parseExclude(QStringLiteral("148 0, x"), &error);
    QVERIFY(!error.isEmpty());
}

void DocumentToolOptimizeImagesTest::excludedImageKeepsStream()
{
    // Plan 8: an exception enabled = false leaves the stream byte by byte; the other image is written anew
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference excluded = builder.addColorImage();
    const pdf::PDFObjectReference converted = builder.addColorImage();
    builder.addPage({ excluded, converted });
    const pdf::PDFDocument document = builder.build();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("colorMode"), QStringLiteral("grayscale"));
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);
    parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(excluded) + QStringLiteral(":off"));

    pdf::PDFDocumentToolOptimizeImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = resultDocument(result);
    QVERIFY(output);

    QCOMPARE(streamData(output, excluded), streamData(&document, excluded));
    QCOMPARE(colorSpace(output, excluded), QByteArray("DeviceRGB"));
    QCOMPARE(colorSpace(output, converted), QByteArray("DeviceGray"));
    QCOMPARE(imageStatus(result, excluded), QStringLiteral("excluded"));
    QCOMPARE(imageStatus(result, converted), QStringLiteral("recompressed"));

    // 301:4019: "Recompressed 1 · kept original 0 · excluded 1", Problems 0
    QCOMPARE(fact(result, QStringLiteral("recompressed")), QStringLiteral("1 · kept original 0 · excluded 1"));
    QCOMPARE(fact(result, QStringLiteral("problems")), QStringLiteral("0"));
    QVERIFY(!fact(result, QStringLiteral("imagesSize")).isEmpty());
}

void DocumentToolOptimizeImagesTest::overrideOnlyForItsImage()
{
    // 301:3594: an override changes its image only - the other image keeps the global Preserve
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference overridden = builder.addColorImage();
    const pdf::PDFObjectReference other = builder.addColorImage();
    builder.addPage({ overridden, other });
    const pdf::PDFDocument document = builder.build();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("mode"), QStringLiteral("custom"));
    parameters.insert(QStringLiteral("colorMode"), QStringLiteral("preserve"));
    parameters.insert(QStringLiteral("colorAlgorithm"), QStringLiteral("flate"));
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);
    parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(overridden) + QStringLiteral(":colorMode=grayscale,algorithm=flate"));

    pdf::PDFDocumentToolOptimizeImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = resultDocument(result);
    QVERIFY(output);
    QCOMPARE(colorSpace(output, overridden), QByteArray("DeviceGray"));
    QCOMPARE(colorSpace(output, other), QByteArray("DeviceRGB"));

    // An id, which is no image of the document, is refused by the validation (MCP gets the reason)
    parameters.insert(QStringLiteral("images"), QStringLiteral("9999 0:off"));
    const pdf::PDFDocumentToolResult refused = tool.run(createContext(document), parameters, nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolOptimizeImagesTest::overrideModeGoalAndOptions()
{
    // Owner 14. 11.: an override has its own Mode ("Mode: Custom" of 301:3594), Goal, Keep original
    // if larger and Preserve transparency; a setting it does not name is the global one
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    using Settings = pdf::PDFImageOptimizer::Settings;
    QString error;
    const std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(QStringLiteral("150 0:mode=custom,goal=minimum-size,keepOriginalIfLarger=false,preserveTransparency=false; 151 0:colorMode=grayscale"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(exceptions.size(), size_t(2));
    const std::vector<Tool::ImageException> again = Tool::parseImageExceptions(Tool::writeImageExceptions(exceptions), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(again == exceptions);

    const Settings global = Settings::createDefault();
    QVERIFY(global.autoMode && global.keepOriginalIfLarger && global.preserveTransparency);
    QCOMPARE(global.goal, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    const Settings own = Tool::createImageSettings(global, exceptions[0]);
    QVERIFY(!own.autoMode);
    QCOMPARE(own.goal, pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize);
    QVERIFY(!own.keepOriginalIfLarger);
    QVERIFY(!own.preserveTransparency);
    const Settings colorOnly = Tool::createImageSettings(global, exceptions[1]);
    QVERIFY(colorOnly.autoMode && colorOnly.keepOriginalIfLarger && colorOnly.preserveTransparency);
    QCOMPARE(colorOnly.goal, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);

    for (const QString& invalid : { QStringLiteral("150 0:mode=manual"), QStringLiteral("150 0:goal=small"), QStringLiteral("150 0:keepOriginalIfLarger=yes"), QStringLiteral("150 0:preserveTransparency=1") })
    {
        Tool::parseImageExceptions(invalid, &error);
        QVERIFY2(!error.isEmpty(), qPrintable(invalid));
    }

    // Keep original if larger of the override: a raw image of 2 x 2 pixels (12 bytes) is smaller
    // than any JPEG - the global option keeps it, the override with false writes the JPEG
    {
        OptimizeImagesDocument builder;
        const QByteArray tiny = "<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>";
        const pdf::PDFObjectReference overridden = builder.addStream(tiny, QByteArray("\xFF\x00\x00\x00\x00\xFF\x00\xFF\x00\x10\x20\x30", 12));
        const pdf::PDFObjectReference other = builder.addStream(tiny, QByteArray("\xFF\x00\x00\x00\x00\xFF\x00\xFF\x00\x10\x20\x30", 12));
        builder.addPage({ overridden, other });
        const pdf::PDFDocument document = builder.build();

        QVariantMap parameters;
        parameters.insert(QStringLiteral("mode"), QStringLiteral("custom"));
        parameters.insert(QStringLiteral("colorMode"), QStringLiteral("preserve"));
        parameters.insert(QStringLiteral("colorAlgorithm"), QStringLiteral("jpeg"));
        parameters.insert(QStringLiteral("colorTargetDpi"), 0);
        parameters.insert(QStringLiteral("keepOriginalIfLarger"), true);
        parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(overridden) + QStringLiteral(":keepOriginalIfLarger=false"));

        pdf::PDFDocumentToolOptimizeImages tool;
        const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(imageStatus(result, overridden), QStringLiteral("recompressed"));
        QCOMPARE(imageStatus(result, other), QStringLiteral("kept"));
        const pdf::PDFDocument* output = resultDocument(result);
        QVERIFY(output);
        QCOMPARE(output->getObject(imageDictionary(output, overridden)->get("Filter")).getString(), QByteArray("DCTDecode"));
        QCOMPARE(streamData(output, other), QByteArray("\xFF\x00\x00\x00\x00\xFF\x00\xFF\x00\x10\x20\x30", 12));
    }

    // Mode of the override: the global Auto analyses a gray RGB image and writes it gray; the
    // override Custom with the color Auto keeps the color space of the image (Preserve)
    {
        OptimizeImagesDocument builder;
        const QByteArray grayRgb = "<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>";
        const pdf::PDFObjectReference overridden = builder.addStream(grayRgb, QByteArray(16 * 16 * 3, '\x80'));
        const pdf::PDFObjectReference other = builder.addStream(grayRgb, QByteArray(16 * 16 * 3, '\x80'));
        builder.addPage({ overridden, other });
        const pdf::PDFDocument document = builder.build();

        QVariantMap parameters;
        parameters.insert(QStringLiteral("mode"), QStringLiteral("auto"));
        parameters.insert(QStringLiteral("colorMode"), QStringLiteral("auto"));
        parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);
        parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(overridden) + QStringLiteral(":mode=custom,colorMode=auto,algorithm=flate"));

        pdf::PDFDocumentToolOptimizeImages tool;
        const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const pdf::PDFDocument* output = resultDocument(result);
        QVERIFY(output);
        QCOMPARE(colorSpace(output, overridden), QByteArray("DeviceRGB"));
        QCOMPARE(colorSpace(output, other), QByteArray("DeviceGray"));
    }
}

void DocumentToolOptimizeImagesTest::grayscaleConvertsColorImages()
{
    // 308:6911 / 308:7341, R4-5: the RGB image becomes DeviceGray with its pixels (Target DPI 0), the gray
    // and the bitonal image are skipped by their declaration - their streams stay byte by byte
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference color = builder.addColorImage();
    const pdf::PDFObjectReference gray = builder.addGrayImage();
    const pdf::PDFObjectReference bitonal = builder.addBitonalImage();
    builder.addPage({ color, gray, bitonal });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = resultDocument(result);
    QVERIFY(output);

    QCOMPARE(colorSpace(output, color), QByteArray("DeviceGray"));
    const pdf::PDFDictionary* converted = imageDictionary(output, color);
    QCOMPARE(output->getObject(converted->get("Width")).getInteger(), pdf::PDFInteger(16));
    QCOMPARE(output->getObject(converted->get("Height")).getInteger(), pdf::PDFInteger(16));
    QCOMPARE(streamData(output, gray), streamData(&document, gray));
    QCOMPARE(streamData(output, bitonal), streamData(&document, bitonal));
    QCOMPARE(imageStatus(result, gray), QStringLiteral("skipped"));
    QCOMPARE(imageStatus(result, bitonal), QStringLiteral("skipped"));

    // 308:7341: "Converted 1 · excluded 0 · skipped 2"; Images size counts the color image only
    QCOMPARE(fact(result, QStringLiteral("converted")), QStringLiteral("1 · excluded 0 · skipped 2"));
    QCOMPARE(result.data.value(QStringLiteral("imagesBefore")).toInteger(), qint64(16 * 16 * 3));
}

void DocumentToolOptimizeImagesTest::grayscaleConvertsLargerGrayEncoding()
{
    // Q4.15 a: Keep original if larger chooses the gray encoding only - a color image is converted also
    // when its gray stream is larger. Optimize Images with the same option keeps the original stream.
    QImage noise(128, 128, QImage::Format_RGB32);
    quint32 seed = 12345;
    for (int y = 0; y < noise.height(); ++y)
    {
        for (int x = 0; x < noise.width(); ++x)
        {
            seed = seed * 1103515245u + 12345u;
            noise.setPixel(x, y, qRgb(int(seed >> 24), int((seed >> 16) & 0xFF), int((seed >> 8) & 0xFF)));
        }
    }

    // A JPEG of the lowest quality: no gray encoding of quality 85 is smaller
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QImageWriter writer(&buffer, "jpg");
    writer.setQuality(1);
    QVERIFY(writer.write(noise));

    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference image = builder.addStream("<< /Type /XObject /Subtype /Image /Width 128 /Height 128 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode >>", jpeg);
    builder.addPage({ image });
    const pdf::PDFDocument document = builder.build();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), true);

    pdf::PDFDocumentToolGrayscale grayscale;
    const pdf::PDFDocumentToolResult converted = grayscale.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(converted.isSuccess(), qPrintable(converted.message));
    QCOMPARE(colorSpace(resultDocument(converted), image), QByteArray("DeviceGray"));

    parameters.insert(QStringLiteral("colorMode"), QStringLiteral("grayscale"));
    pdf::PDFDocumentToolOptimizeImages optimize;
    const pdf::PDFDocumentToolResult kept = optimize.run(createContext(document), parameters, nullptr, nullptr);
    QCOMPARE(kept.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QCOMPARE(imageStatus(kept, image), QStringLiteral("kept"));
}

void DocumentToolOptimizeImagesTest::grayscaleKeepsDictionaryKeys()
{
    // ISO 32000-2 Table 87: /OC, /StructParent, /Intent, /Metadata stay; /Alternates (8.9.5.4) and /OPI
    // (14.11.7) are other versions of the colored picture - dropped from a gray image
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference metadata = builder.addStream("<< /Type /Metadata /Subtype /XML >>", "<x/>");
    const pdf::PDFObjectReference alternate = builder.addColorImage();
    const pdf::PDFObjectReference layer = builder.builder.addObject(OptimizeImagesDocument::parse("<< /Type /OCG /Name (L) >>"));
    const pdf::PDFObjectReference image = builder.addColorImage("/OC " + OptimizeImagesDocument::ref(layer) + " /StructParent 7 /Intent /Saturation /Metadata " + OptimizeImagesDocument::ref(metadata) +
                                                                " /Alternates [ << /Image " + OptimizeImagesDocument::ref(alternate) + " >> ] /OPI << /2.0 << >> >>");
    builder.addPage({ image });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDictionary* dictionary = imageDictionary(resultDocument(result), image);
    QVERIFY(dictionary->hasKey("OC"));
    QVERIFY(dictionary->hasKey("StructParent"));
    QVERIFY(dictionary->hasKey("Intent"));
    QVERIFY(dictionary->hasKey("Metadata"));
    QVERIFY(!dictionary->hasKey("Alternates"));
    QVERIFY(!dictionary->hasKey("OPI"));
}

void DocumentToolOptimizeImagesTest::sharedSoftMaskIsNotReplaced()
{
    // B62: two images share one soft mask, one of them is excluded. The other image gets its new mask
    // as a new object; the shared mask stays byte by byte for the excluded image.
    OptimizeImagesDocument builder;
    QByteArray alpha(16 * 16, '\xFF');
    for (int i = 0; i < 16 * 8; ++i)
    {
        alpha[i] = '\x40';
    }
    const pdf::PDFObjectReference mask = builder.addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", alpha);
    const QByteArray softMask = "/SMask " + OptimizeImagesDocument::ref(mask);
    const pdf::PDFObjectReference excluded = builder.addColorImage(softMask);
    const pdf::PDFObjectReference converted = builder.addColorImage(softMask);
    builder.addPage({ excluded, converted });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    QVariantMap parameters;
    parameters.insert(QStringLiteral("exclude"), OptimizeImagesDocument::key(excluded));
    parameters.insert(QStringLiteral("algorithm"), QStringLiteral("flate"));
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = resultDocument(result);

    QCOMPARE(streamData(output, mask), streamData(&document, mask));
    const pdf::PDFObject& excludedMask = imageDictionary(output, excluded)->get("SMask");
    QVERIFY(excludedMask.isReference());
    QCOMPARE(excludedMask.getReference(), mask);

    const pdf::PDFObject& newMask = imageDictionary(output, converted)->get("SMask");
    QVERIFY(newMask.isReference());
    QVERIFY(newMask.getReference() != mask);
    QCOMPARE(colorSpace(output, converted), QByteArray("DeviceGray"));
}

void DocumentToolOptimizeImagesTest::maskDrawnAsImageKeepsStream()
{
    // B62 (ISO 32000-2, 11.6.5.2: the /SMask of an image is an image XObject): the gray image M is
    // drawn on the page and is the soft mask of the color image A, which is excluded. Optimize Images
    // (Flate, every image of the page) must not write M anew in place - A keeps its stream and would get
    // another transparency. M is "kept", its stream stays byte by byte.
    OptimizeImagesDocument builder;
    QByteArray alpha(16 * 16, '\xFF');
    for (int i = 0; i < 16 * 8; ++i)
    {
        alpha[i] = '\x40';
    }
    const pdf::PDFObjectReference mask = builder.addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", alpha);
    const pdf::PDFObjectReference excluded = builder.addColorImage("/SMask " + OptimizeImagesDocument::ref(mask));
    builder.addPage({ excluded, mask });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolOptimizeImages tool;
    QVariantMap parameters;
    parameters.insert(QStringLiteral("mode"), QStringLiteral("custom"));
    parameters.insert(QStringLiteral("colorMode"), QStringLiteral("grayscale"));
    parameters.insert(QStringLiteral("grayAlgorithm"), QStringLiteral("jpeg"));
    parameters.insert(QStringLiteral("grayJpegQuality"), 10);
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);
    parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(excluded) + QStringLiteral(":off"));
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);

    // Nothing else can be written: no document, the mask is kept
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QCOMPARE(imageStatus(result, mask), QStringLiteral("kept"));
    QCOMPARE(imageStatus(result, excluded), QStringLiteral("excluded"));

    // With a second, ordinary image the document is written: M and A stay byte by byte
    OptimizeImagesDocument second;
    const pdf::PDFObjectReference secondMask = second.addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", alpha);
    const pdf::PDFObjectReference secondExcluded = second.addColorImage("/SMask " + OptimizeImagesDocument::ref(secondMask));
    const pdf::PDFObjectReference ordinary = second.addColorImage();
    second.addPage({ secondExcluded, secondMask, ordinary });
    const pdf::PDFDocument secondDocument = second.build();
    parameters.insert(QStringLiteral("images"), OptimizeImagesDocument::key(secondExcluded) + QStringLiteral(":off"));
    const pdf::PDFDocumentToolResult written = tool.run(createContext(secondDocument), parameters, nullptr, nullptr);
    QVERIFY2(written.isSuccess(), qPrintable(written.message));
    const pdf::PDFDocument* output = resultDocument(written);
    QVERIFY(output);
    QCOMPARE(imageStatus(written, ordinary), QStringLiteral("recompressed"));
    QCOMPARE(streamData(output, secondMask), alpha);
    QCOMPARE(streamData(output, secondExcluded), streamData(&secondDocument, secondExcluded));
    QCOMPARE(imageDictionary(output, secondExcluded)->get("SMask").getReference(), secondMask);
}

void DocumentToolOptimizeImagesTest::spotColorsAreSaid()
{
    // Plan 5 (ISO 32000-2, 8.6.6.4 Separation, 8.6.6.5 DeviceN, 8.6.6.3 Indexed): a recompressed image
    // is written in a device space, its colorant is gone - the result says "N images with spot colors
    // were converted"; an image of DeviceRGB is no spot color
    OptimizeImagesDocument builder;
    const QByteArray separation = "[/Separation /Gold /DeviceCMYK << /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 0.2 1 0] /N 1 >>]";
    QByteArray tints;
    for (int i = 0; i < 16 * 16; ++i)
    {
        tints += char(i % 16 * 16);
    }
    const pdf::PDFObjectReference spot = builder.addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace " + separation + " /BitsPerComponent 8 >>", tints);
    const pdf::PDFObjectReference indexedSpot = builder.addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace [/Indexed " + separation + " 1 <00FF>] /BitsPerComponent 8 >>",
                                                                  QByteArray(128, '\x00') + QByteArray(128, '\x01'));
    const pdf::PDFObjectReference rgb = builder.addColorImage();
    builder.addPage({ spot, indexedSpot, rgb });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    QVariantMap parameters;
    parameters.insert(QStringLiteral("algorithm"), QStringLiteral("flate"));
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(imageStatus(result, spot), QStringLiteral("recompressed"));
    QCOMPARE(imageStatus(result, indexedSpot), QStringLiteral("recompressed"));
    QCOMPARE(imageStatus(result, rgb), QStringLiteral("recompressed"));
    QCOMPARE(fact(result, QStringLiteral("spotColors")), QStringLiteral("2 images with spot colors were converted"));
    QCOMPARE(result.data.value(QStringLiteral("imagesSpotColors")).toInteger(), qint64(2));

    // Excluded, the spot images keep their colorants: nothing is said
    parameters.insert(QStringLiteral("exclude"), OptimizeImagesDocument::key(spot) + QStringLiteral(", ") + OptimizeImagesDocument::key(indexedSpot));
    const pdf::PDFDocumentToolResult excluded = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(excluded.isSuccess(), qPrintable(excluded.message));
    QVERIFY(fact(excluded, QStringLiteral("spotColors")).isEmpty());
    QCOMPARE(excluded.data.value(QStringLiteral("imagesSpotColors")).toInteger(), qint64(0));
}

void DocumentToolOptimizeImagesTest::hiddenLayerAndAppearance()
{
    // R4-4: an image drawn only in a layer, which /D turns off, is converted too; an image only in the
    // appearance of an annotation (a stamp) is never changed
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference hidden = builder.addColorImage();
    const pdf::PDFObjectReference stamp = builder.addColorImage();
    const pdf::PDFObjectReference layer = builder.builder.addObject(OptimizeImagesDocument::parse("<< /Type /OCG /Name (Hidden) >>"));
    const pdf::PDFObjectReference appearance = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 50 50] /Resources << /XObject << /S " + OptimizeImagesDocument::ref(stamp) + " >> >> >>",
                                                                 "q 50 0 0 50 0 0 cm /S Do Q");
    const pdf::PDFObjectReference annotation = builder.builder.addObject(OptimizeImagesDocument::parse("<< /Type /Annot /Subtype /Stamp /Rect [0 0 50 50] /AP << /N " + OptimizeImagesDocument::ref(appearance) + " >> >>"));
    const pdf::PDFObjectReference page = builder.builder.appendPage(QRectF(0, 0, 400, 400));
    const pdf::PDFObjectReference contents = builder.addStream("<< >>", "/OC /L1 BDC q 50 0 0 50 0 0 cm /H Do Q EMC");
    builder.builder.mergeTo(page, OptimizeImagesDocument::parse("<< /Resources << /Properties << /L1 " + OptimizeImagesDocument::ref(layer) + " >> /XObject << /H " + OptimizeImagesDocument::ref(hidden) +
                                                                " >> >> /Contents " + OptimizeImagesDocument::ref(contents) + " /Annots [ " + OptimizeImagesDocument::ref(annotation) + " ] >>"));
    builder.builder.setCatalogOptionalContentProperties(builder.builder.addObject(OptimizeImagesDocument::parse("<< /OCGs [ " + OptimizeImagesDocument::ref(layer) + " ] /D << /OFF [ " + OptimizeImagesDocument::ref(layer) + " ] >> >>")));
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = resultDocument(result);
    QCOMPARE(colorSpace(output, hidden), QByteArray("DeviceGray"));
    QCOMPARE(streamData(output, stamp), streamData(&document, stamp));
}

void DocumentToolOptimizeImagesTest::tooLargeImageIsAProblem()
{
    // Plan 6: a declaration of 100 000 x 100 000 pixels with a short stream is not decoded - "too large",
    // a problem of the result; the image keeps its stream
    OptimizeImagesDocument builder;
    const pdf::PDFObjectReference huge = builder.addStream("<< /Type /XObject /Subtype /Image /Width 100000 /Height 100000 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", QByteArray(12, 'x'));
    const pdf::PDFObjectReference color = builder.addColorImage();
    builder.addPage({ huge, color });
    const pdf::PDFDocument document = builder.build();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("colorMode"), QStringLiteral("grayscale"));
    parameters.insert(QStringLiteral("keepOriginalIfLarger"), false);
    pdf::PDFDocumentToolOptimizeImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(imageStatus(result, huge), QStringLiteral("too-large"));
    QCOMPARE(fact(result, QStringLiteral("problems")), QStringLiteral("1"));
    QCOMPARE(streamData(resultDocument(result), huge), QByteArray(12, 'x'));
}

void DocumentToolOptimizeImagesTest::grayscaleWithoutColorImages()
{
    // 2.7 of the plan: nothing to convert is NothingToDo with the text of the tool, no document
    OptimizeImagesDocument builder;
    builder.addPage({ builder.addGrayImage() });
    const pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentToolGrayscale tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document), QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());
    QCOMPARE(result.message, QStringLiteral("The document has no color images to convert."));
}

QTEST_GUILESS_MAIN(DocumentToolOptimizeImagesTest)

#include "tst_documenttooloptimizeimagestest.moc"
