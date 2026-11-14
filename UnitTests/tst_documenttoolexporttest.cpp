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

// The exports PDF to Images and Extract Images (step 4 of phase 2, package P1; plan 8, rows PDF to
// Images and Extract Images): the fixed parameters of plan 7, K4.11 and R4-20; the size of a page
// image from the crop box with /Rotate and /UserUnit (B57), the longer side in pixels, "@" required,
// the pages 0 and 1600 refused (B56), the limit of the pixels (R4-19), the written image is the crop
// box, a Stop keeps the files whole, the problems of a page (K4.11), Retry writes only its files, a
// file which cannot be written is a failure with its reason; Extract Images writes the same image
// once (B60), the alpha of a soft mask, a white background of JPEG, /Decode [1 0], the number of an
// image in the name with left out numbers skipped (K4.12), never a stencil.

#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_extractimages.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_split.h"
#include "pdfoperationcontrol.h"
#include "pdfparser.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

/// A document of pages and images built from objects written as text
class ExportDocument
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

    pdf::PDFObjectReference addStream(const QByteArray& dictionaryText, QByteArray data)
    {
        const pdf::PDFObject dictionaryObject = parse(dictionaryText);
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
    }

    /// A page of the media box with the resources and the content
    pdf::PDFObjectReference addPage(const QRectF& mediaBox, const QByteArray& content, const QByteArray& resources = "<< >>")
    {
        const pdf::PDFObjectReference page = builder.appendPage(mediaBox);
        const pdf::PDFObjectReference contents = addStream("<< >>", content);
        builder.mergeTo(page, parse("<< /Resources " + resources + " /Contents " + ref(contents) + " >>"));
        return page;
    }

    /// A page drawing the images in squares of 50 pt
    pdf::PDFObjectReference addImagePage(const std::vector<pdf::PDFObjectReference>& images)
    {
        QByteArray xobjects;
        QByteArray content;
        for (size_t i = 0; i < images.size(); ++i)
        {
            xobjects += "/Im" + QByteArray::number(qint64(i)) + " " + ref(images[i]) + " ";
            content += "q 50 0 0 50 " + QByteArray::number(qint64(i) * 60) + " 0 cm /Im" + QByteArray::number(qint64(i)) + " Do Q\n";
        }
        return addPage(QRectF(0, 0, 600, 200), content, "<< /XObject << " + xobjects + " >> >>");
    }

    /// An RGB image of the size of one color (no filter)
    pdf::PDFObjectReference addColorImage(int width, int height, QByteArray rgb, const QByteArray& extra = QByteArray())
    {
        QByteArray data;
        for (int i = 0; i < width * height; ++i)
        {
            data += rgb;
        }
        return addStream("<< /Type /XObject /Subtype /Image /Width " + QByteArray::number(width) + " /Height " + QByteArray::number(height) + " /ColorSpace /DeviceRGB /BitsPerComponent 8 " + extra + " >>", data);
    }

    pdf::PDFDocument build() { return builder.build(); }

    pdf::PDFDocumentBuilder builder;
};

/// A folder sink, which stops after the given number of written files (Stop Export of the question)
class StoppingFolderSink : public pdf::PDFDocumentToolFolderSink
{
public:
    explicit StoppingFolderSink(const QString& folder, int stopAfter) :
        pdf::PDFDocumentToolFolderSink(folder, ExistingFiles::Overwrite),
        m_stopAfter(stopAfter)
    {
    }

    virtual bool isStopped() const override { return getWrittenFiles().size() >= m_stopAfter; }

private:
    int m_stopAfter;
};

class DocumentToolExportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void pdfToImagesParameters();
    void extractImagesParameters();
    void pageGeometry();
    void pageSelectionAndName();
    void pageTooLarge();
    void writtenImageIsTheCropBox();
    void renderContextOfTheRun();
    void explicitFeaturesOfPdfTool();
    void stopKeepsWholeFiles();
    void renderingIssuesOfPages();
    void retryWritesOnlyItsFiles();
    void writeErrorIsAFailure();
    void identicalImagesOnce();
    void softMaskAndBackground();
    void decodeArrayOfOneBit();
    void imageNumbersWithLeftOut();
    void identicalInlineImagesOnce();
    void exportPlanCacheAndCancel();
    void exportPlanParameters();
    void retryOfAnEarlierDate();
    void damagedImageFailsWithItsReason();
    void tallImageOfPngAndTiff();
    void contentPlacedAfterKeepsResources();

private:
    /// A control, which says the operation is canceled from the start
    class CanceledControl : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCancelled() const override { return true; }
    };

    /// onlyOutputs is panel-only (K4.11, R4-20: MCP has no Retry): it is not in the schema of MCP
    static bool isInSchema(const pdf::PDFDocumentToolParameterDefinitions& definitions, const QString& name)
    {
        return pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject().contains(name);
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, pdf::PDFDocumentToolFileSink* sink)
    {
        pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("Report.pdf"), true));
        context.fileSink = sink;
        return context;
    }

    static QVariantMap pdfToImagesParameters(const QString& folder, int dpi = 72)
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("folder"), folder);
        parameters.insert(QStringLiteral("dpi"), dpi);
        parameters.insert(QStringLiteral("existingFiles"), QStringLiteral("overwrite"));
        return parameters;
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

    static QStringList fileNames(const QString& folder)
    {
        return QDir(folder).entryList(QDir::Files, QDir::Name);
    }
};

void DocumentToolExportTest::initTestCase()
{
    // The numbers of the texts are formatted by the default locale (O17): the texts of Figma
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void DocumentToolExportTest::pdfToImagesParameters()
{
    // Plan 7 and 4.4: the resolution 72 to 6000 dpi (300), the longer side 100 to 16 384 pixels,
    // the name "<name>_@" (339:3546 "Report_@"), the files of the folder sink, no gamma (K4.10)
    pdf::PDFDocumentToolPdfToImages tool;
    QCOMPARE(tool.getId(), QStringLiteral("convert.pdf-to-images"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    const pdf::PDFDocumentToolParameterDefinition* dpi = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("dpi"));
    QVERIFY(dpi);
    QCOMPARE(dpi->getIntegerMinimum(), qint64(72));
    QCOMPARE(dpi->getIntegerMaximum(), qint64(6000));
    QCOMPARE(dpi->getDefaultValue().toLongLong(), qint64(300));
    const pdf::PDFDocumentToolParameterDefinition* pixels = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("pixels"));
    QVERIFY(pixels);
    QCOMPARE(pixels->getIntegerMinimum(), qint64(100));
    QCOMPARE(pixels->getIntegerMaximum(), qint64(16384));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("fileName"))->getDefaultValue().toString(), QStringLiteral("{source_base}_@"));
    for (const char* name : { "pages", "format", "subtype", "compression", "quality", "optimizedWrite", "progressiveScanWrite", "sizeBy", "folder", "existingFiles" })
    {
        QVERIFY2(isInSchema(definitions, QString::fromLatin1(name)), name);
    }
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("gamma")));
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("onlyOutputs")));
    QVERIFY(!isInSchema(definitions, QStringLiteral("onlyOutputs")));
}

void DocumentToolExportTest::extractImagesParameters()
{
    // Plan 7 and 4.5: exclude (the ids of the list), "<name>_img_#" (341:7516 "Report_img_#")
    pdf::PDFDocumentToolExtractImages tool;
    QCOMPARE(tool.getId(), QStringLiteral("convert.extract-images"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    for (const char* name : { "pages", "format", "compression", "quality", "exclude", "folder", "fileName", "existingFiles" })
    {
        QVERIFY2(isInSchema(definitions, QString::fromLatin1(name)), name);
    }
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("fileName"))->getDefaultValue().toString(), QStringLiteral("{source_base}_img_#"));
    QVERIFY(!isInSchema(definitions, QStringLiteral("onlyOutputs")));

    // The names of a Retry: one per line, empty = every file
    QCOMPARE(pdf::PDFDocumentToolFileOutput::getOnlyOutputs({ { QStringLiteral("onlyOutputs"), pdf::PDFDocumentToolFileOutput::createOnlyOutputs({ QStringLiteral("a.png"), QStringLiteral("b c.png") }) } }),
             QStringList({ QStringLiteral("a.png"), QStringLiteral("b c.png") }));
    QVERIFY(pdf::PDFDocumentToolFileOutput::getOnlyOutputs(QVariantMap()).isEmpty());
}

void DocumentToolExportTest::pageGeometry()
{
    // B57, plan 8: A4 (595.28 x 841.89 pt) at 300 dpi is 2480 x 3508 px; /Rotate 90 turns it; /UserUnit 2
    // doubles the unit (ISO 32000-2 Table 31); the size comes from the crop box; Pixels 2000 is the
    // longer side (1414 x 2000)
    ExportDocument builder;
    const QRectF a4(0, 0, 595.28, 841.89);
    builder.addPage(a4, QByteArray());
    const pdf::PDFObjectReference rotated = builder.addPage(a4, QByteArray());
    builder.builder.setPageRotation(rotated, pdf::PageRotation::Rotate90);
    const pdf::PDFObjectReference userUnit = builder.addPage(a4, QByteArray());
    builder.builder.setPageUserUnit(userUnit, 2.0);
    const pdf::PDFObjectReference cropped = builder.addPage(QRectF(0, 0, 400, 400), QByteArray());
    builder.builder.setPageCropBox(cropped, QRectF(100, 50, 144, 72));
    const pdf::PDFDocument document = builder.build();
    const pdf::PDFCatalog* catalog = document.getCatalog();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("dpi"), 300);
    using Tool = pdf::PDFDocumentToolPdfToImages;
    QCOMPARE(Tool::getPageGeometry(catalog->getPage(0), parameters).imageSize, QSize(2480, 3508));
    QCOMPARE(Tool::getPageGeometry(catalog->getPage(1), parameters).imageSize, QSize(3508, 2480));
    QCOMPARE(Tool::getPageGeometry(catalog->getPage(2), parameters).imageSize, QSize(4961, 7016));

    // The crop box of 2 x 1 inch: 600 x 300 px of the 1667 x 1667 px of the media box
    const Tool::PageGeometry crop = Tool::getPageGeometry(catalog->getPage(3), parameters);
    QCOMPARE(crop.imageSize, QSize(600, 300));
    QCOMPARE(crop.renderSize, QSize(1667, 1667));

    parameters.insert(QStringLiteral("sizeBy"), QStringLiteral("pixels"));
    parameters.insert(QStringLiteral("pixels"), 2000);
    QCOMPARE(Tool::getPageGeometry(catalog->getPage(0), parameters).imageSize, QSize(1414, 2000));
    QCOMPARE(Tool::getPageGeometry(catalog->getPage(1), parameters).imageSize, QSize(2000, 1414));
}

void DocumentToolExportTest::pageSelectionAndName()
{
    // B56: the pages 0 and 1600 do not exist; 339:5080: the name must contain @
    ExportDocument builder;
    builder.addPage(QRectF(0, 0, 100, 100), QByteArray());
    builder.addPage(QRectF(0, 0, 100, 100), QByteArray());
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    pdf::PDFDocumentToolPdfToImages tool;
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
    for (const QString& range : { QStringLiteral("0"), QStringLiteral("1600"), QStringLiteral("1-3") })
    {
        QVariantMap parameters = pdfToImagesParameters(folder.path());
        parameters.insert(QStringLiteral("pages"), range);
        const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(createContext(document, &sink), parameters);
        QVERIFY2(!errors.empty() && errors.front().parameter == QStringLiteral("pages"), qPrintable(range));
    }

    QVariantMap parameters = pdfToImagesParameters(folder.path());
    parameters.insert(QStringLiteral("fileName"), QStringLiteral("Report"));
    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(createContext(document, &sink), parameters);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("fileName"));
    QCOMPARE(errors.front().message, QStringLiteral("The name must contain @ for the page number"));
}

void DocumentToolExportTest::pageTooLarge()
{
    // R4-19, plan 6: a page of 5 x 5 m at 600 dpi (118 110 px per side) is refused before the run
    ExportDocument builder;
    builder.addPage(QRectF(0, 0, 14173.2, 14173.2), QByteArray());
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), pdfToImagesParameters(folder.path(), 600), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("lower the resolution")), qPrintable(result.message));
    QVERIFY(fileNames(folder.path()).isEmpty());

    // WebP holds 16 383 px per side, PNG 32 767: 20 000 px pass for PNG only
    pdf::PDFDocumentToolPdfToImages::PageGeometry geometry;
    geometry.imageSize = QSize(20000, 100);
    geometry.renderSize = geometry.imageSize;
    pdf::PDFDocumentToolImageFormat format;
    QVERIFY(pdf::PDFDocumentToolPdfToImages::checkPageGeometry(0, geometry, format).isEmpty());
    format.format = "webp";
    QCOMPARE(pdf::PDFDocumentToolPdfToImages::checkPageGeometry(0, geometry, format), QStringLiteral("Page 1 would be 20,000 px wide – lower the resolution"));
}

void DocumentToolExportTest::writtenImageIsTheCropBox()
{
    // B57: the file is the crop box of the page - the left half of the crop box is red, the right
    // half white; the content outside of the crop box (blue) is not in the image
    ExportDocument builder;
    const pdf::PDFObjectReference page = builder.addPage(QRectF(0, 0, 200, 200), "0 0 1 rg 0 0 200 200 re f 1 1 1 rg 50 50 100 100 re f 1 0 0 rg 50 50 50 100 re f");
    builder.builder.setPageCropBox(page, QRectF(50, 50, 100, 100));
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), pdfToImagesParameters(folder.path()), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_1.png") }));

    const QImage image(QDir(folder.path()).filePath(QStringLiteral("Report_1.png")));
    QCOMPARE(image.size(), QSize(100, 100));
    QCOMPARE(QColor(image.pixel(10, 50)), QColor(Qt::red));
    QCOMPARE(QColor(image.pixel(90, 50)), QColor(Qt::white));
    QCOMPARE(QColor(image.pixel(1, 1)).blue(), 0);

    // 339:6505: Files "1 PNG · Report_1.png"
    QCOMPARE(fact(result, QStringLiteral("files")), QStringLiteral("1 PNG · Report_1.png"));
}

void DocumentToolExportTest::renderContextOfTheRun()
{
    // R4-10 b, owner answer Q4.6: the run renders by the context (PDFDocumentToolContext::rendering)
    // - the color management, the features of the renderer and the layer states of the tab reach
    // the pixels of the written image. A page of 100 x 100 pt at 72 dpi: the upper left quarter
    // DeviceCMYK cyan (1 0 0 0 k), the upper right quarter red in a layer, which /D shows; at the
    // bottom (y 0 to 20 pt) a Square annotation with /F 4 (Print) and a blue appearance.
    ExportDocument builder;
    const pdf::PDFObjectReference layer = builder.builder.addObject(ExportDocument::parse("<< /Type /OCG /Name (Red) >>"));
    builder.builder.mergeTo(builder.builder.getCatalogReference(), ExportDocument::parse("<< /OCProperties << /OCGs [" + ExportDocument::ref(layer) + "] /D << /ON [" + ExportDocument::ref(layer) + "] >> >> >>"));
    const pdf::PDFObjectReference appearance = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 100 20] >>", "0 0 1 rg 0 0 100 20 re f");
    const pdf::PDFObjectReference annotation = builder.builder.addObject(ExportDocument::parse("<< /Type /Annot /Subtype /Square /Rect [0 0 100 20] /F 4 /AP << /N " + ExportDocument::ref(appearance) + " >> >>"));
    const pdf::PDFObjectReference page = builder.addPage(QRectF(0, 0, 100, 100), "1 0 0 0 k 0 20 50 80 re f /OC /oc1 BDC 1 0 0 rg 50 20 50 80 re f EMC",
                                                         "<< /Properties << /oc1 " + ExportDocument::ref(layer) + " >> >>");
    builder.builder.mergeTo(page, ExportDocument::parse("<< /Annots [" + ExportDocument::ref(annotation) + "] >>"));
    const pdf::PDFDocument document = builder.build();

    struct Pixels
    {
        QColor cyan;        ///< (25, 40): the CMYK quarter
        QColor layer;       ///< (75, 40): the red quarter of the layer
        QColor annotation;  ///< (50, 90): the annotation
    };
    const auto render = [&document](const pdf::PDFDocumentToolRenderContext& rendering, Pixels* pixels)
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        pdf::PDFDocumentToolContext context = createContext(document, &sink);
        context.rendering = rendering;
        const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolPdfToImages().run(context, pdfToImagesParameters(folder.path()), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const QImage image(QDir(folder.path()).filePath(QStringLiteral("Report_1.png")));
        QCOMPARE(image.size(), QSize(100, 100));
        pixels->cyan = QColor(image.pixel(25, 40));
        pixels->layer = QColor(image.pixel(75, 40));
        pixels->annotation = QColor(image.pixel(50, 90));
    };

    // No application (PdfTool): the layer by /D with /Export (shown), the annotation by /F Print
    Pixels defaults;
    render(pdf::PDFDocumentToolRenderContext(), &defaults);
    QCOMPARE(defaults.layer, QColor(Qt::red));
    QCOMPARE(defaults.annotation, QColor(Qt::blue));

    // The tab hides the layer: the quarter is white (the paper)
    pdf::PDFDocumentToolRenderContext hidden;
    hidden.layerStates = std::map<pdf::PDFObjectReference, pdf::OCState>{ { layer, pdf::OCState::OFF } };
    Pixels hiddenPixels;
    render(hidden, &hiddenPixels);
    QCOMPARE(hiddenPixels.layer, QColor(Qt::white));
    QCOMPARE(hiddenPixels.annotation, QColor(Qt::blue));

    // The settings without Show annotations: the annotation is not drawn
    pdf::PDFDocumentToolRenderContext noAnnotations;
    noAnnotations.features = pdf::PDFRenderer::Features(pdf::PDFRenderer::getDefaultFeatures() & ~pdf::PDFRenderer::Features(pdf::PDFRenderer::DisplayAnnotations));
    Pixels noAnnotationPixels;
    render(noAnnotations, &noAnnotationPixels);
    QCOMPARE(noAnnotationPixels.annotation, QColor(Qt::white));
    QCOMPARE(noAnnotationPixels.layer, QColor(Qt::red));

    // The color management of the settings: the generic one converts DeviceCMYK by ISO 32000-2,
    // 10.4.2.4 (red = 1 - min(1, cyan + black), ...): exactly (0, 255, 255); the default color
    // management converts by its CMYK profile, never to this pure value
    pdf::PDFDocumentToolRenderContext generic;
    pdf::PDFCMSSettings genericSettings;
    genericSettings.system = pdf::PDFCMSSettings::System::Generic;
    generic.cmsSettings = genericSettings;
    Pixels genericPixels;
    render(generic, &genericPixels);
    QCOMPARE(genericPixels.cyan, QColor(0, 255, 255));
    QVERIFY2(defaults.cyan != QColor(0, 255, 255), qPrintable(defaults.cyan.name()));

    // A color mode of a view is no content (R4-10 b): an inverted view still writes red
    pdf::PDFDocumentToolRenderContext inverted;
    inverted.features = pdf::PDFRenderer::Features(pdf::PDFRenderer::getDefaultFeatures() | pdf::PDFRenderer::ColorAdjust_Invert);
    Pixels invertedPixels;
    render(inverted, &invertedPixels);
    QCOMPARE(invertedPixels.layer, QColor(Qt::red));
}

void DocumentToolExportTest::explicitFeaturesOfPdfTool()
{
    // Plan 4.8 (old functionality kept): PdfTool render renders by the tool with the features of its
    // command line, --render-invert-colors and --render-ignore-opt-content are explicit requests, not
    // the color mode of a view (R4-10 b holds for the settings of the application, create). A page of
    // 100 x 100 pt: red, over it a blue square in a layer hidden by /D (/OFF).
    ExportDocument builder;
    const pdf::PDFObjectReference layer = builder.builder.addObject(ExportDocument::parse("<< /Type /OCG /Name (Blue) >>"));
    builder.builder.mergeTo(builder.builder.getCatalogReference(), ExportDocument::parse("<< /OCProperties << /OCGs [" + ExportDocument::ref(layer) + "] /D << /OFF [" + ExportDocument::ref(layer) + "] >> >> >>"));
    builder.addPage(QRectF(0, 0, 100, 100), "1 0 0 rg 0 0 100 100 re f /OC /oc1 BDC 0 0 1 rg 50 0 50 100 re f EMC",
                    "<< /Properties << /oc1 " + ExportDocument::ref(layer) + " >> >>");
    const pdf::PDFDocument document = builder.build();

    // Pixel (25, 50) is red, (75, 50) red or the blue of the layer
    const auto render = [&document](const pdf::PDFDocumentToolPageRenderSettings& renderSettings, QColor* left, QColor* right)
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        pdf::PDFDocumentToolPdfToImages tool;
        QVariantMap parameters;
        QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), pdfToImagesParameters(folder.path()), &parameters).empty());
        const pdf::PDFDocumentToolResult result = tool.exportPages(createContext(document, &sink), parameters, renderSettings, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const QImage image(QDir(folder.path()).filePath(QStringLiteral("Report_1.png")));
        QCOMPARE(image.size(), QSize(100, 100));
        *left = QColor(image.pixel(25, 50));
        *right = QColor(image.pixel(75, 50));
    };

    // The command line: the inverted red is cyan (InvertRgb), the layer of /D /OFF is drawn
    pdf::PDFDocumentToolPageRenderSettings commandLine;
    commandLine.features = pdf::PDFRenderer::Features(pdf::PDFRenderer::getDefaultFeatures() | pdf::PDFRenderer::ColorAdjust_Invert | pdf::PDFRenderer::IgnoreOptionalContent);
    QColor left;
    QColor right;
    render(commandLine, &left, &right);
    QCOMPARE(left, QColor(0, 255, 255));
    QCOMPARE(right, QColor(255, 255, 0));

    // The same features as the settings of a view of the application: no color mode, the layers by /D
    pdf::PDFDocumentToolRenderContext viewSettings;
    viewSettings.features = commandLine.features;
    render(pdf::PDFDocumentToolPageRenderSettings::create(viewSettings), &left, &right);
    QCOMPARE(left, QColor(Qt::red));
    QCOMPARE(right, QColor(Qt::red));
}

void DocumentToolExportTest::stopKeepsWholeFiles()
{
    // 356:12504: Stop after the first file - it is whole, the other pages are "Not exported"
    ExportDocument builder;
    for (int i = 0; i < 3; ++i)
    {
        builder.addPage(QRectF(0, 0, 100, 100), "1 0 0 rg 0 0 100 100 re f");
    }
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    StoppingFolderSink sink(folder.path(), 1);

    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), pdfToImagesParameters(folder.path()), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QCOMPARE(result.writtenFiles.size(), qsizetype(1));
    QCOMPARE(result.notWrittenFiles, QStringList({ QStringLiteral("Report_2.png"), QStringLiteral("Report_3.png") }));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_1.png") }));
    QCOMPARE(QImage(result.writtenFiles.front()).size(), QSize(100, 100));
    QCOMPARE(fact(result, QStringLiteral("notExported")), QStringLiteral("Pages 2 to 3"));
    QVERIFY(fact(result, QStringLiteral("written")).startsWith(QStringLiteral("1 PNG · ")));
}

void DocumentToolExportTest::renderingIssuesOfPages()
{
    // K4.11, 339:6505: a page, whose content names a missing image, has a problem; the other not
    ExportDocument builder;
    builder.addPage(QRectF(0, 0, 100, 100), "1 0 0 rg 0 0 100 100 re f");
    builder.addPage(QRectF(0, 0, 100, 100), "/Missing Do");
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), pdfToImagesParameters(folder.path()), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.writtenFiles.size(), qsizetype(2));
    QVERIFY(!result.renderingIssues.empty());
    for (const pdf::PDFDocumentToolPageProblem& problem : result.renderingIssues)
    {
        QCOMPARE(problem.pageIndex, pdf::PDFInteger(1));
        QVERIFY(!problem.message.isEmpty());
    }
    // The count of the pages (the plural form is the one of the translation)
    QVERIFY(fact(result, QStringLiteral("renderingIssues")).startsWith(QStringLiteral("1 ")));
}

void DocumentToolExportTest::retryWritesOnlyItsFiles()
{
    // K4.11, R4-20: Retry N Files runs the same parameters with onlyOutputs - only those files
    ExportDocument builder;
    for (int i = 0; i < 3; ++i)
    {
        builder.addPage(QRectF(0, 0, 100, 100), QByteArray());
    }
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    QVariantMap parameters = pdfToImagesParameters(folder.path());
    parameters.insert(QStringLiteral("onlyOutputs"), pdf::PDFDocumentToolFileOutput::createOnlyOutputs({ QStringLiteral("Report_2.png") }));
    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_2.png") }));
}

void DocumentToolExportTest::writeErrorIsAFailure()
{
    // 339:7293: a file, which cannot be written (its name is a folder), is Failed with its reason;
    // the other files are written
    ExportDocument builder;
    for (int i = 0; i < 3; ++i)
    {
        builder.addPage(QRectF(0, 0, 100, 100), QByteArray());
    }
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("Report_2.png")));
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    pdf::PDFDocumentToolPdfToImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), pdfToImagesParameters(folder.path()), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.writtenFiles.size(), qsizetype(2));
    QCOMPARE(result.failedFiles.size(), size_t(1));
    QCOMPARE(result.failedFiles.front().fileName, QStringLiteral("Report_2.png"));
    QVERIFY(!result.failedFiles.front().reason.isEmpty());
    QVERIFY(fact(result, QStringLiteral("failed")).startsWith(QStringLiteral("1 ")));
    QVERIFY(fact(result, QStringLiteral("written")).startsWith(QStringLiteral("2 PNG · ")));
}

void DocumentToolExportTest::identicalImagesOnce()
{
    // B60, Q4.8: an image drawn twice (one reference) and a second object of the same stream are one
    // file; the same bytes of other dimensions (2 x 8 and 4 x 4 pixels) are two images
    ExportDocument builder;
    const pdf::PDFObjectReference first = builder.addColorImage(4, 4, QByteArray("\xFF\x00\x00", 3));
    const pdf::PDFObjectReference copy = builder.addColorImage(4, 4, QByteArray("\xFF\x00\x00", 3));
    const pdf::PDFObjectReference otherSize = builder.addColorImage(2, 8, QByteArray("\xFF\x00\x00", 3));
    builder.addImagePage({ first, first, copy, otherSize });
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    QVariantMap parameters;
    parameters.insert(QStringLiteral("folder"), folder.path());
    pdf::PDFDocumentToolExtractImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_img_1.png"), QStringLiteral("Report_img_3.png") }));
    QCOMPARE(QImage(QDir(folder.path()).filePath(QStringLiteral("Report_img_3.png"))).size(), QSize(2, 8));
    QCOMPARE(result.data.value(QStringLiteral("duplicateCount")).toInteger(), qint64(1));
}

void DocumentToolExportTest::softMaskAndBackground()
{
    // 4.5: the soft mask is the alpha of a PNG (8.9.6.3: 0 = transparent); JPEG has no alpha - the
    // image is over a white background
    ExportDocument builder;
    QByteArray alpha(16, '\xFF');
    for (int i = 0; i < 8; ++i)
    {
        alpha[i] = '\x00';
    }
    const pdf::PDFObjectReference mask = builder.addStream("<< /Type /XObject /Subtype /Image /Width 4 /Height 4 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", alpha);
    const pdf::PDFObjectReference image = builder.addColorImage(4, 4, QByteArray("\x00\x00\x00", 3), "/SMask " + ExportDocument::ref(mask));
    builder.addImagePage({ image });
    const pdf::PDFDocument document = builder.build();

    for (const QString& format : { QStringLiteral("png"), QStringLiteral("jpeg") })
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters;
        parameters.insert(QStringLiteral("folder"), folder.path());
        parameters.insert(QStringLiteral("format"), format);
        parameters.insert(QStringLiteral("quality"), 100);
        pdf::PDFDocumentToolExtractImages tool;
        const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.writtenFiles.size(), qsizetype(1));

        const QImage written(result.writtenFiles.front());
        QCOMPARE(written.size(), QSize(4, 4));
        if (format == QLatin1String("png"))
        {
            QVERIFY(written.hasAlphaChannel());
            QCOMPARE(qAlpha(written.pixel(1, 0)), 0);
            QCOMPARE(qAlpha(written.pixel(1, 3)), 255);
        }
        else
        {
            QVERIFY(qGray(written.pixel(1, 0)) > 240);
            QVERIFY(qGray(written.pixel(1, 3)) < 15);
        }
    }
}

void DocumentToolExportTest::decodeArrayOfOneBit()
{
    // 8.9.5.2: /Decode [1 0] of a gray image of one bit maps the sample 0 to white
    ExportDocument builder;
    const pdf::PDFObjectReference image = builder.addStream("<< /Type /XObject /Subtype /Image /Width 8 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0] >>", QByteArray(2, '\x00'));
    builder.addImagePage({ image });
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    QVariantMap parameters;
    parameters.insert(QStringLiteral("folder"), folder.path());
    pdf::PDFDocumentToolExtractImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QImage written(result.writtenFiles.front());
    QCOMPARE(written.size(), QSize(8, 2));
    QCOMPARE(qGray(written.pixel(3, 1)), 255);
}

void DocumentToolExportTest::imageNumbersWithLeftOut()
{
    // K4.12, 341:7516: "#" is the number of the image in the list - the left out image 2 has no file
    // and its number is skipped; a stencil (ImageMask) is never exported
    ExportDocument builder;
    const pdf::PDFObjectReference first = builder.addColorImage(2, 2, QByteArray("\xFF\x00\x00", 3));
    const pdf::PDFObjectReference second = builder.addColorImage(2, 2, QByteArray("\x00\xFF\x00", 3));
    const pdf::PDFObjectReference third = builder.addColorImage(2, 2, QByteArray("\x00\x00\xFF", 3));
    const pdf::PDFObjectReference stencil = builder.addStream("<< /Type /XObject /Subtype /Image /Width 8 /Height 1 /ImageMask true >>", QByteArray(1, '\x0F'));
    builder.addImagePage({ first, second, third, stencil });
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    QVariantMap parameters;
    parameters.insert(QStringLiteral("folder"), folder.path());
    parameters.insert(QStringLiteral("exclude"), pdf::PDFDocumentImageList::Key::createReference(second).toString());
    pdf::PDFDocumentToolExtractImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_img_1.png"), QStringLiteral("Report_img_3.png") }));
    QCOMPARE(QColor(QImage(QDir(folder.path()).filePath(QStringLiteral("Report_img_3.png"))).pixel(0, 0)), QColor(Qt::blue));

    // 341:8462: Files "2 PNG · numbered like the images"; Left out says the choice and the stencil
    QCOMPARE(fact(result, QStringLiteral("files")), QStringLiteral("2 PNG · numbered like the images"));
    QVERIFY(fact(result, QStringLiteral("leftOut")).startsWith(QStringLiteral("1 ")));
    QVERIFY(fact(result, QStringLiteral("leftOut")).contains(QStringLiteral("by your choice")));
    QCOMPARE(result.data.value(QStringLiteral("excludedCount")).toInteger(), qint64(1));

    // An id, which is no image of the document, is an error
    parameters.insert(QStringLiteral("exclude"), QStringLiteral("9999 0"));
    QCOMPARE(tool.run(createContext(document, &sink), parameters, nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolExportTest::identicalInlineImagesOnce()
{
    // B60 for inline images (8.9.7, R4-8: each BI is an item of its content): two inline images of
    // the same dictionary and data are one file, the third of other data is another file. The
    // streams of the inline images are copies made by the walk of the page (ASan P2S4: the copy
    // used to die before its hash was read).
    const QByteArray red = "BI /W 2 /H 2 /CS /RGB /BPC 8 ID " + QByteArray("\xFF\x00\x00\xFF\x00\x00\xFF\x00\x00\xFF\x00\x00", 12) + " EI";
    const QByteArray blue = "BI /W 2 /H 2 /CS /RGB /BPC 8 ID " + QByteArray("\x00\x00\xFF\x00\x00\xFF\x00\x00\xFF\x00\x00\xFF", 12) + " EI";
    ExportDocument builder;
    builder.addPage(QRectF(0, 0, 200, 100), "q 20 0 0 20 0 0 cm " + red + " Q q 20 0 0 20 40 0 cm " + red + " Q q 20 0 0 20 80 0 cm " + blue + " Q");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = pdf::PDFDocumentToolExtractImages::createImageList(&document, std::nullopt, nullptr, nullptr);
    QCOMPARE(list.getItems().size(), size_t(3));

    QVariantMap parameters;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(pdf::PDFDocumentToolExtractImages().getParameters(), parameters, &parameters).empty());
    QString errorMessage;
    const std::optional<pdf::PDFDocumentToolExtractImages::ExportPlan> plan = pdf::PDFDocumentToolExtractImages::createExportPlan(&document, list, QStringLiteral("Report.pdf"), parameters, QDate(2026, 11, 14),
                                                                                                                                  nullptr, nullptr, &errorMessage);
    QVERIFY2(plan, qPrintable(errorMessage));
    QCOMPARE(plan->images.size(), size_t(2));
    QCOMPARE(plan->images[0].itemIndex, size_t(0));
    QCOMPARE(plan->images[0].duplicates, std::vector<size_t>({ 1 }));
    QCOMPARE(plan->images[1].itemIndex, size_t(2));
    QCOMPARE(plan->duplicates, std::vector<size_t>({ 1 }));
    QCOMPARE(plan->getFileName(0), QStringLiteral("Report_img_1.png"));
    QCOMPARE(plan->getFileName(2), QStringLiteral("Report_img_3.png"));

    // The run writes the red one and the blue one
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
    parameters.insert(QStringLiteral("folder"), folder.path());
    const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolExtractImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_img_1.png"), QStringLiteral("Report_img_3.png") }));
    QCOMPARE(QColor(QImage(QDir(folder.path()).filePath(QStringLiteral("Report_img_1.png"))).pixel(0, 0)), QColor(Qt::red));
    QCOMPARE(QColor(QImage(QDir(folder.path()).filePath(QStringLiteral("Report_img_3.png"))).pixel(0, 0)), QColor(Qt::blue));
}

void DocumentToolExportTest::exportPlanCacheAndCancel()
{
    // RS4APP: the GUI makes the plan again at each change of the names or the pages; the hashes of
    // the images of the list are read once into the cache of the list (one entry per exported
    // candidate: the image XObjects and the inline image), the next plan of another name reads
    // them from it and gives the same images. A canceled plan has no result and no reason.
    ExportDocument builder;
    const pdf::PDFObjectReference first = builder.addColorImage(2, 2, QByteArray("\xFF\x00\x00", 3));
    const pdf::PDFObjectReference copy = builder.addColorImage(2, 2, QByteArray("\xFF\x00\x00", 3));
    const pdf::PDFObjectReference other = builder.addColorImage(2, 2, QByteArray("\x00\xFF\x00", 3));
    builder.addImagePage({ first, copy, other });
    builder.addPage(QRectF(0, 0, 100, 100), "q 20 0 0 20 0 0 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x80 EI Q");
    const pdf::PDFDocument document = builder.build();
    const pdf::PDFDocumentImageList list = pdf::PDFDocumentToolExtractImages::createImageList(&document, std::nullopt, nullptr, nullptr);
    QCOMPARE(list.getItems().size(), size_t(4));

    QVariantMap parameters;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(pdf::PDFDocumentToolExtractImages().getParameters(), parameters, &parameters).empty());
    pdf::PDFDocumentToolExtractImages::ExportPlanCache cache;
    QString errorMessage;
    const std::optional<pdf::PDFDocumentToolExtractImages::ExportPlan> plan = pdf::PDFDocumentToolExtractImages::createExportPlan(&document, list, QStringLiteral("Report.pdf"), parameters, QDate(2026, 11, 14),
                                                                                                                                  nullptr, &cache, &errorMessage);
    QVERIFY2(plan, qPrintable(errorMessage));
    QCOMPARE(cache.getCount(), size_t(4));
    QCOMPARE(plan->images.size(), size_t(3));
    QCOMPARE(plan->duplicates, std::vector<size_t>({ 1 }));

    parameters.insert(QStringLiteral("fileName"), QStringLiteral("Photo_#"));
    const std::optional<pdf::PDFDocumentToolExtractImages::ExportPlan> renamed = pdf::PDFDocumentToolExtractImages::createExportPlan(&document, list, QStringLiteral("Report.pdf"), parameters, QDate(2026, 11, 14),
                                                                                                                                     nullptr, &cache, &errorMessage);
    QVERIFY2(renamed, qPrintable(errorMessage));
    QCOMPARE(cache.getCount(), size_t(4));
    QCOMPARE(renamed->duplicates, std::vector<size_t>({ 1 }));
    QCOMPARE(renamed->getFileName(0), QStringLiteral("Photo_1.png"));
    QCOMPARE(renamed->getFileName(1), QString());
    QCOMPARE(renamed->getFileName(2), QStringLiteral("Photo_3.png"));
    QCOMPARE(renamed->getFileName(3), QStringLiteral("Photo_4.png"));

    // Canceled: no plan, no reason (the GUI drops it, the run is Stopped)
    CanceledControl canceled;
    errorMessage.clear();
    pdf::PDFDocumentToolExtractImages::ExportPlanCache emptyCache;
    QVERIFY(!pdf::PDFDocumentToolExtractImages::createExportPlan(&document, list, QStringLiteral("Report.pdf"), parameters, QDate(2026, 11, 14), &canceled, &emptyCache, &errorMessage));
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(emptyCache.getCount(), size_t(0));
}

void DocumentToolExportTest::retryOfAnEarlierDate()
{
    // RS4CORE, K4.11: Retry N Files writes the failed files of the earlier run - with {date} in the
    // name they carry the date of that run (ISO 8601), not today; both exports find them
    ExportDocument builder;
    const pdf::PDFObjectReference image = builder.addColorImage(2, 2, QByteArray("\xFF\x00\x00", 3));
    builder.addImagePage({ image });
    builder.addPage(QRectF(0, 0, 100, 100), QByteArray());
    const pdf::PDFDocument document = builder.build();

    // PDF to Images
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters = pdfToImagesParameters(folder.path());
        parameters.insert(QStringLiteral("fileName"), QStringLiteral("Report_{date}_@"));
        parameters.insert(QStringLiteral("onlyOutputs"), pdf::PDFDocumentToolFileOutput::createOnlyOutputs({ QStringLiteral("Report_2020-01-02_2.png") }));
        const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolPdfToImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_2020-01-02_2.png") }));
    }

    // Extract Images
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters;
        parameters.insert(QStringLiteral("folder"), folder.path());
        parameters.insert(QStringLiteral("fileName"), QStringLiteral("{source_base}_{date}_img_#"));
        parameters.insert(QStringLiteral("onlyOutputs"), pdf::PDFDocumentToolFileOutput::createOnlyOutputs({ QStringLiteral("Report_2019-12-31_img_1.png") }));
        const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolExtractImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_2019-12-31_img_1.png") }));
    }

    // A name of no date of the run (another template) is still not a file of this export
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters = pdfToImagesParameters(folder.path());
        parameters.insert(QStringLiteral("fileName"), QStringLiteral("Report_{date}_@"));
        parameters.insert(QStringLiteral("onlyOutputs"), pdf::PDFDocumentToolFileOutput::createOnlyOutputs({ QStringLiteral("Other_2020-01-02_2.png") }));
        QCOMPARE(pdf::PDFDocumentToolPdfToImages().run(createContext(document, &sink), parameters, nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
        QVERIFY(fileNames(folder.path()).isEmpty());
    }
}

void DocumentToolExportTest::damagedImageFailsWithItsReason()
{
    // Corpus NOT_RENDERED_AM_SCHED: /Mask of an image is a stencil mask (8.9.6.3: /ImageMask true,
    // 1 bit) - an 8 bit gray image there is damaged. Its file fails with the reason of the decoder,
    // the other image is written; never "The tool failed with an unexpected error".
    ExportDocument builder;
    const pdf::PDFObjectReference wrongMask = builder.addStream("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(4, '\x80'));
    const pdf::PDFObjectReference damaged = builder.addColorImage(2, 2, QByteArray("\xFF\x00\x00", 3), "/Mask " + ExportDocument::ref(wrongMask));
    const pdf::PDFObjectReference valid = builder.addColorImage(2, 2, QByteArray("\x00\x00\xFF", 3));
    builder.addImagePage({ damaged, valid });
    const pdf::PDFDocument document = builder.build();
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);

    QVariantMap parameters;
    parameters.insert(QStringLiteral("folder"), folder.path());
    const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolExtractImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(fileNames(folder.path()), QStringList({ QStringLiteral("Report_img_2.png") }));
    QCOMPARE(result.failedFiles.size(), size_t(1));
    QCOMPARE(result.failedFiles.front().fileName, QStringLiteral("Report_img_1.png"));
    QVERIFY2(result.failedFiles.front().reason.startsWith(QStringLiteral("The image cannot be read: ")), qPrintable(result.failedFiles.front().reason));
}

void DocumentToolExportTest::tallImageOfPngAndTiff()
{
    // Owner 2026-11-14 (corpus CopenhagenD-Thursday, 422 x 32 768 px): an extracted PNG or TIFF
    // image is written without painting, so the side limit of the raster engine (32 767 px) does
    // not apply to it - only the 2 GiB of the decoded image. WebP keeps its own limit (16 383 px)
    // and BMP (without alpha, it may be painted over white) keeps 32 767 px.
    ExportDocument builder;
    const int height = 32768;
    const pdf::PDFObjectReference tall = builder.addStream("<< /Type /XObject /Subtype /Image /Width 1 /Height " + QByteArray::number(height) + " /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(height, '\x40'));
    builder.addImagePage({ tall });
    const pdf::PDFDocument document = builder.build();

    // A format, which Qt of the platform cannot write (the Linux Qt has no TIFF and WebP writers),
    // is refused by the tool before any image: it is not checked there
    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    for (const QString& format : { QStringLiteral("png"), QStringLiteral("tiff") })
    {
        if (!writable.contains(format.toLatin1()))
        {
            qInfo("%s cannot be written on this platform: not checked", qPrintable(format));
            continue;
        }
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters;
        parameters.insert(QStringLiteral("folder"), folder.path());
        parameters.insert(QStringLiteral("format"), format);
        const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolExtractImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(format + QStringLiteral(": ") + result.message));
        QVERIFY2(result.failedFiles.empty(), qPrintable(format + QStringLiteral(": ") + (result.failedFiles.empty() ? QString() : result.failedFiles.front().reason)));
        const QStringList written = fileNames(folder.path());
        QCOMPARE(written.size(), 1);
        const QImage image(folder.filePath(written.front()));
        QCOMPARE(image.size(), QSize(1, height));
    }

    for (const auto& [format, side] : { std::pair<QString, QString>(QStringLiteral("webp"), QStringLiteral("16")), std::pair<QString, QString>(QStringLiteral("bmp"), QStringLiteral("32")) })
    {
        if (!writable.contains(format.toLatin1()))
        {
            qInfo("%s cannot be written on this platform: not checked", qPrintable(format));
            continue;
        }
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVariantMap parameters;
        parameters.insert(QStringLiteral("folder"), folder.path());
        parameters.insert(QStringLiteral("format"), format);
        const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolExtractImages().run(createContext(document, &sink), parameters, nullptr, nullptr);
        QCOMPARE(result.failedFiles.size(), size_t(1));
        const QString& reason = result.failedFiles.front().reason;
        QVERIFY2(reason.contains(QStringLiteral("at most")) && reason.contains(side), qPrintable(format + QStringLiteral(": ") + reason));
    }
}

void DocumentToolExportTest::contentPlacedAfterKeepsResources()
{
    // P4 open point of step 4: PDFPageContentStreamBuilder PlaceAfter on a page without content and
    // resources lost the resources of the new content (a merge with a null object) - its fonts and
    // images were not drawn. The new content draws a red image over the right half of each page and
    // a text at the bottom of the left half (a font and an XObject of its own resources). Page 1 is
    // empty (no content, no resources at all); page 2 has a content, which draws the image of the
    // resources inherited from the page tree (Table 31, 7.7.3.4) on its left half.
    ExportDocument builder;
    const pdf::PDFObjectReference empty = builder.builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference blue = builder.addColorImage(2, 2, QByteArray("\x00\x00\xFF", 3));
    const pdf::PDFObjectReference inherited = builder.builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference oldContent = builder.addStream("<< >>", "q 50 0 0 100 0 0 cm /Blue Do Q");
    builder.builder.mergeTo(inherited, ExportDocument::parse("<< /Contents " + ExportDocument::ref(oldContent) + " >>"));

    QImage red(4, 4, QImage::Format_RGB32);
    red.fill(Qt::red);
    const auto placeAfter = [&builder, &red](pdf::PDFObjectReference page)
    {
        pdf::PDFPageContentStreamBuilder content(&builder.builder, pdf::PDFContentStreamBuilder::CoordinateSystem::Qt, pdf::PDFPageContentStreamBuilder::Mode::PlaceAfter);
        QPainter* painter = content.begin(page);
        QVERIFY(painter);
        painter->drawImage(QRectF(50, 0, 50, 100), red);
        QFont font = painter->font();
        font.setPixelSize(20);
        painter->setFont(font);
        painter->drawText(QPointF(2, 95), QStringLiteral("XXXX"));
        content.end(painter);

        // The resources of the new content are on the page: the font and the image
        const pdf::PDFDictionary* pageDictionary = builder.builder.getDictionaryFromObject(builder.builder.getObjectByReference(page));
        const pdf::PDFObject& resources = builder.builder.getObject(pageDictionary->get("Resources"));
        QVERIFY(resources.isDictionary());
        QVERIFY(builder.builder.getObject(resources.getDictionary()->get("Font")).isDictionary());
        QVERIFY(builder.builder.getObject(resources.getDictionary()->get("XObject")).isDictionary());
    };

    // The empty page first, while the page tree has no resources to inherit
    placeAfter(empty);
    const pdf::PDFObject parent = builder.builder.getDictionaryFromObject(builder.builder.getObjectByReference(inherited))->get("Parent");
    QVERIFY(parent.isReference());
    builder.builder.mergeTo(parent.getReference(), ExportDocument::parse("<< /Resources << /XObject << /Blue " + ExportDocument::ref(blue) + " >> >> >>"));
    placeAfter(inherited);

    // A hostile chain of /Parent without resources, which leads back to the page (a cycle): the
    // search of the inherited resources ends at the second visit
    const pdf::PDFObjectReference cyclic = builder.builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFObjectReference loopNode = builder.builder.addObject(ExportDocument::parse("<< /Type /Pages /Parent " + ExportDocument::ref(cyclic) + " >>"));
    builder.builder.mergeTo(cyclic, ExportDocument::parse("<< /Parent " + ExportDocument::ref(loopNode) + " >>"));
    placeAfter(cyclic);
    builder.builder.mergeTo(cyclic, ExportDocument::parse("<< /Parent " + ExportDocument::ref(parent.getReference()) + " >>"));
    const pdf::PDFDocument document = builder.build();

    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    pdf::PDFDocumentToolFolderSink sink(folder.path(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
    const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolPdfToImages().run(createContext(document, &sink), pdfToImagesParameters(folder.path()), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QImage first(QDir(folder.path()).filePath(QStringLiteral("Report_1.png")));
    const QImage second(QDir(folder.path()).filePath(QStringLiteral("Report_2.png")));
    QCOMPARE(first.size(), QSize(100, 100));

    // The image of QPdfWriter may be stored lossy: red within a small tolerance
    const auto isRed = [](QRgb pixel) { return qRed(pixel) > 230 && qGreen(pixel) < 25 && qBlue(pixel) < 25; };
    QVERIFY2(isRed(first.pixel(75, 50)), qPrintable(QColor(first.pixel(75, 50)).name()));
    QCOMPARE(QColor(first.pixel(25, 50)), QColor(Qt::white));
    QVERIFY2(isRed(second.pixel(75, 50)), qPrintable(QColor(second.pixel(75, 50)).name()));
    QCOMPARE(QColor(second.pixel(25, 50)), QColor(Qt::blue));

    // The text of the empty page is drawn: dark glyphs at the bottom of its left half
    int dark = 0;
    for (int y = 75; y < 100; ++y)
    {
        for (int x = 0; x < 50; ++x)
        {
            dark += qGray(first.pixel(x, y)) < 128 ? 1 : 0;
        }
    }
    QVERIFY2(dark > 20, qPrintable(QString::number(dark)));
}

void DocumentToolExportTest::exportPlanParameters()
{
    // RS4APP: the plan is the names (fileName and the extension of format), the pages and exclude;
    // the encoding, the folder and Existing files do not change it
    using Tool = pdf::PDFDocumentToolExtractImages;
    for (const char* name : { "pages", "exclude", "format", "fileName" })
    {
        QVERIFY2(Tool::isExportPlanParameter(QString::fromLatin1(name)), name);
    }
    for (const char* name : { "compression", "quality", "optimizedWrite", "progressiveScanWrite", "folder", "existingFiles", "onlyOutputs", "" })
    {
        QVERIFY2(!Tool::isExportPlanParameter(QString::fromLatin1(name)), name);
    }
}

// The renderer of the pages needs the font database of a QGuiApplication
QTEST_MAIN(DocumentToolExportTest)

#include "tst_documenttoolexporttest.moc"
