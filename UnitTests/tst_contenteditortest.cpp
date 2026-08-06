// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdffont.h"
#include "pdfimage.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>

#include <array>
#include <memory>

class ContentEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void test_image_orientation_plain();
    void test_image_orientation_flipped_matrix();
    void test_image_orientation_form_xobject();
    void test_image_orientation_inline_image();
    void test_image_orientation_smask();
    void test_image_orientation_image_mask();
    void test_image_orientation_rotated_matrix();
    void test_image_orientation_replaced_image();
    void test_image_orientation_qt_generated_document();
    void test_image_orientation_tiling_pattern();
    void test_rendered_page_is_unchanged();
    void test_other_resources_are_preserved();
    void test_numbers_are_not_written_in_exponential_notation();
    void test_complex_tiling_pattern_is_not_processed();

private:
    enum class Variant
    {
        Plain,
        FlippedMatrix,
        FormXObject,
        InlineImage,
        SMask,
        ImageMask,
        RotatedMatrix,
        IndirectResources,
        TinyScale,
        TilingPattern,
        ComplexTilingPattern
    };

    /// Description of the image, as it is seen on the page. The color
    /// is the color of the image sample, which is displayed at the
    /// upper left corner of the image (in the page coordinate space).
    struct ImagePlacement
    {
        QPointF firstSamplePoint;
        QPointF lastSamplePoint;
        QColor firstSampleColor;
    };

    static QImage createTestImage();

    /// Creates a document with a single page, which contains a single image
    static pdf::PDFDocument createDocumentWithImage(Variant variant);

    /// Processes the page content of the first page and returns the edited page content
    static pdf::PDFEditedPageContent processPageContent(const pdf::PDFDocument* document);

    /// Rewrites the content of the first page - the same way as the editor
    /// plugin does it, when the edited page content is written back
    /// to the document.
    static pdf::PDFDocumentPointer rewritePageContent(const pdf::PDFDocument* document,
                                                      const pdf::PDFEditedPageContent& content,
                                                      bool clearImageObjects,
                                                      QByteArray* outputContent);

    /// Returns the placement of the first image element of the page content
    static ImagePlacement getImagePlacement(const pdf::PDFEditedPageContent& content);

    /// Performs the whole test - the page content is processed, written back
    /// and processed again. The placement of the image must be the same.
    static void testVariant(Variant variant, bool clearImageObjects);

    /// Renders the first page of the document into the image
    static QImage renderPage(const pdf::PDFDocument* document);
};

QImage ContentEditorTest::createTestImage()
{
    QImage image(4, 4, QImage::Format_RGB888);
    image.fill(Qt::red);
    for (int y = 2; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            image.setPixelColor(x, y, Qt::blue);
        }
    }
    return image;
}

pdf::PDFDocument ContentEditorTest::createDocumentWithImage(Variant variant)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 200, 200));

    QImage image = createTestImage();

    QByteArray pageContent;
    pdf::PDFDictionary xObject;
    pdf::PDFDictionary pattern;

    auto createImageObject = [&](bool addSoftMask)
    {
        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::Flate;
        options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;
        options.enablePngPredictor = false;
        options.alphaHandling = pdf::PDFImage::AlphaHandling::FlattenToWhite;

        pdf::PDFStream imageStream = pdf::PDFImage::createStreamFromImage(image, options);

        if (addSoftMask)
        {
            // Non-uniform mask - the upper half is opaque, the lower half
            // is transparent. A vertically mirrored mask would be detected.
            QImage maskImage(image.size(), QImage::Format_Grayscale8);
            maskImage.fill(255);
            for (int y = maskImage.height() / 2; y < maskImage.height(); ++y)
            {
                for (int x = 0; x < maskImage.width(); ++x)
                {
                    maskImage.setPixel(x, y, 0);
                }
            }

            pdf::PDFImage::ImageEncodeOptions maskOptions;
            maskOptions.compression = pdf::PDFImage::ImageCompression::Flate;
            maskOptions.colorMode = pdf::PDFImage::ImageColorMode::Grayscale;
            maskOptions.enablePngPredictor = false;
            maskOptions.alphaHandling = pdf::PDFImage::AlphaHandling::FlattenToWhite;

            pdf::PDFStream maskStream = pdf::PDFImage::createStreamFromImage(maskImage, maskOptions);
            pdf::PDFObjectReference maskRef = builder.addObject(
                pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(maskStream)));

            pdf::PDFDictionary dict = *imageStream.getDictionary();
            dict.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(maskRef));
            const QByteArray* content = imageStream.getContent();
            QByteArray contentDereferenced = content ? *content : QByteArray();
            imageStream = pdf::PDFStream(std::move(dict), std::move(contentDereferenced));
        }

        return builder.addObject(pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(imageStream)));
    };

    switch (variant)
    {
        case Variant::Plain:
        case Variant::SMask:
        {
            pdf::PDFObjectReference imageRef = createImageObject(variant == Variant::SMask);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));
            pageContent = "q 80 0 0 40 10 30 cm /Im1 Do Q";
            break;
        }

        case Variant::RotatedMatrix:
        {
            pdf::PDFObjectReference imageRef = createImageObject(false);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

            // Two rotations by 45 degrees - the resulting transformation matrix
            // contains values, which are very close to zero (but not exactly zero).
            pageContent = "q 100 0 0 100 0 0 cm "
                          "0.70710678118654746 0.70710678118654746 -0.70710678118654746 0.70710678118654746 0 0 cm "
                          "0.70710678118654746 0.70710678118654746 -0.70710678118654746 0.70710678118654746 0 0 cm "
                          "q 0.8 0 0 0.4 0.1 0.3 cm /Im1 Do Q Q";
            break;
        }

        case Variant::TinyScale:
        {
            // Very small scale factors - the transformation matrix values
            // cannot be written using the exponential notation.
            pdf::PDFObjectReference imageRef = createImageObject(false);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));
            pageContent = "q 0.00002 0 0 0.00001 10 30 cm /Im1 Do Q";
            break;
        }

        case Variant::IndirectResources:
        {
            pdf::PDFObjectReference imageRef = createImageObject(false);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));
            pageContent = "q 80 0 0 40 10 30 cm /Im1 Do Q";
            break;
        }

        case Variant::FlippedMatrix:
        {
            pdf::PDFObjectReference imageRef = createImageObject(false);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));
            pageContent = "q 80 0 0 -40 10 70 cm /Im1 Do Q";
            break;
        }

        case Variant::ImageMask:
        {
            // Stencil mask image - 4x4, 1 bit per component, one row per byte,
            // the upper half of the image is painted (sample value 0 paints).
            QByteArray maskData = QByteArray::fromHex("0000ffff");

            pdf::PDFDictionary maskDictionary;
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(4));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(4));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("ImageMask"), pdf::PDFObject::createBool(true));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(1));
            maskDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(maskData.size()));

            pdf::PDFObjectReference imageRef = builder.addObject(pdf::PDFObject::createStream(
                std::make_shared<pdf::PDFStream>(std::move(maskDictionary), std::move(maskData))));
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));
            pageContent = "q 0 0 0 rg 80 0 0 40 10 30 cm /Im1 Do Q";
            break;
        }

        case Variant::FormXObject:
        {
            pdf::PDFObjectReference imageRef = createImageObject(false);

            pdf::PDFDictionary formXObject;
            formXObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

            pdf::PDFDictionary formResources;
            formResources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                                   pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(formXObject))));

            QByteArray formContent = "q 80 0 0 40 0 0 cm /Im1 Do Q";

            pdf::PDFArray bbox;
            bbox.appendItem(pdf::PDFObject::createReal(0.0));
            bbox.appendItem(pdf::PDFObject::createReal(0.0));
            bbox.appendItem(pdf::PDFObject::createReal(200.0));
            bbox.appendItem(pdf::PDFObject::createReal(200.0));

            pdf::PDFArray matrix;
            matrix.appendItem(pdf::PDFObject::createReal(1.0));
            matrix.appendItem(pdf::PDFObject::createReal(0.0));
            matrix.appendItem(pdf::PDFObject::createReal(0.0));
            matrix.appendItem(pdf::PDFObject::createReal(1.0));
            matrix.appendItem(pdf::PDFObject::createReal(10.0));
            matrix.appendItem(pdf::PDFObject::createReal(30.0));

            pdf::PDFDictionary formDictionary;
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Form"));
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BBox"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(bbox))));
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Matrix"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(matrix))));
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                                    pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(formResources))));
            formDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(formContent.size()));

            pdf::PDFObjectReference formRef = builder.addObject(pdf::PDFObject::createStream(
                std::make_shared<pdf::PDFStream>(std::move(formDictionary), std::move(formContent))));

            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Fx1"), pdf::PDFObject::createReference(formRef));
            pageContent = "q /Fx1 Do Q";
            break;
        }

        case Variant::TilingPattern:
        case Variant::ComplexTilingPattern:
        {
            // The image is not painted directly - it is painted by a colored
            // tiling pattern, which fills a rectangle. This is, how the images
            // are painted for example by cairo/Inkscape generated documents.
            const bool isComplex = variant == Variant::ComplexTilingPattern;

            pdf::PDFObjectReference imageRef = createImageObject(false);

            pdf::PDFDictionary patternXObject;
            patternXObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

            pdf::PDFDictionary patternResources;
            patternResources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                                      pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(patternXObject))));

            QByteArray patternContent = "q 80 0 0 40 0 0 cm /Im1 Do Q";

            pdf::PDFArray bbox;
            bbox.appendItem(pdf::PDFObject::createReal(0.0));
            bbox.appendItem(pdf::PDFObject::createReal(0.0));
            bbox.appendItem(pdf::PDFObject::createReal(80.0));
            bbox.appendItem(pdf::PDFObject::createReal(40.0));

            pdf::PDFArray matrix;
            matrix.appendItem(pdf::PDFObject::createReal(1.0));
            matrix.appendItem(pdf::PDFObject::createReal(0.0));
            matrix.appendItem(pdf::PDFObject::createReal(0.0));
            matrix.appendItem(pdf::PDFObject::createReal(1.0));
            matrix.appendItem(pdf::PDFObject::createReal(10.0));
            matrix.appendItem(pdf::PDFObject::createReal(30.0));

            // The complex variant uses very small steps, so the filled area
            // is covered by tens of thousands of tiles.
            const pdf::PDFReal xStep = isComplex ? 1.0 : 80.0;
            const pdf::PDFReal yStep = isComplex ? 1.0 : 40.0;

            pdf::PDFDictionary patternDictionary;
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Pattern"));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("PatternType"), pdf::PDFObject::createInteger(1));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("PaintType"), pdf::PDFObject::createInteger(1));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("TilingType"), pdf::PDFObject::createInteger(1));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BBox"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(bbox))));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Matrix"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(matrix))));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("XStep"), pdf::PDFObject::createReal(xStep));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("YStep"), pdf::PDFObject::createReal(yStep));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(patternResources))));
            patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(patternContent.size()));

            pdf::PDFObjectReference patternRef = builder.addObject(pdf::PDFObject::createStream(
                std::make_shared<pdf::PDFStream>(std::move(patternDictionary), std::move(patternContent))));

            pattern.addEntry(pdf::PDFInplaceOrMemoryString("P1"), pdf::PDFObject::createReference(patternRef));
            pageContent = "q /Pattern cs /P1 scn 10 30 80 40 re f Q";
            break;
        }

        case Variant::InlineImage:
        {
            QByteArray imageData;
            for (int y = 0; y < image.height(); ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    QColor color = image.pixelColor(x, y);
                    imageData.append(char(color.red()));
                    imageData.append(char(color.green()));
                    imageData.append(char(color.blue()));
                }
            }

            pageContent = "q 80 0 0 40 10 30 cm BI /W 4 /H 4 /CS /RGB /BPC 8 ID ";
            pageContent.append(imageData);
            pageContent.append(" EI Q");
            break;
        }
    }

    pdf::PDFDictionary contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(pageContent.size()));
    pdf::PDFStream contentStream(std::move(contentDict), std::move(pageContent));
    pdf::PDFObjectReference contentRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(contentStream)));

    pdf::PDFDictionary resources;
    if (!xObject.isEmpty())
    {
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                           pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xObject))));
    }

    if (!pattern.isEmpty())
    {
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Pattern"),
                           pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pattern))));
    }

    if (variant == Variant::IndirectResources)
    {
        // Resource category, which is not regenerated by the content stream builder
        pdf::PDFDictionary colorSpaces;
        colorSpaces.addEntry(pdf::PDFInplaceOrMemoryString("CS0"), pdf::PDFObject::createName("DeviceRGB"));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"),
                           pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(colorSpaces))));
    }

    pdf::PDFObject resourcesObject = pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources)));

    if (variant == Variant::IndirectResources)
    {
        // Page resources are an indirect object (they can be shared between pages)
        resourcesObject = pdf::PDFObject::createReference(builder.addObject(std::move(resourcesObject)));
    }

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), std::move(resourcesObject));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));

    return builder.build();
}

pdf::PDFEditedPageContent ContentEditorTest::processPageContent(const pdf::PDFDocument* document)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &activity));

    pdf::PDFPageContentEditorProcessor processor(page, document, &fontCache, &cms, &activity,
                                                 QTransform(), pdf::PDFMeshQualitySettings());
    processor.processContents();

    return processor.takeEditedPageContent();
}

pdf::PDFDocumentPointer ContentEditorTest::rewritePageContent(const pdf::PDFDocument* document,
                                                              const pdf::PDFEditedPageContent& content,
                                                              bool clearImageObjects,
                                                              QByteArray* outputContent)
{
    pdf::PDFDocumentModifier modifier(document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    const pdf::PDFPage* page = document->getCatalog()->getPage(0);

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(const_cast<pdf::PDFDocument*>(document));
    contentStreamBuilder.setFontDictionary(content.getFontDictionary());
    contentStreamBuilder.setXObjectDictionary(content.getXObjectDictionary());
    contentStreamBuilder.setGraphicStateDictionary(content.getGraphicStateDictionary());

    const size_t elementCount = content.getElementCount();
    for (size_t i = 0; i < elementCount; ++i)
    {
        pdf::PDFEditedPageContentElement* element = const_cast<pdf::PDFEditedPageContent&>(content).getElement(i);

        if (clearImageObjects)
        {
            if (pdf::PDFEditedPageContentElementImage* imageElement = element->asImage())
            {
                // Simulates the situation, when the user replaces the image
                // in the item settings dialog - the original image object
                // is dropped and the image is written from the raster data.
                imageElement->setImageObject(pdf::PDFObject());
            }
        }

        contentStreamBuilder.writeEditedElement(element);
    }

    if (outputContent)
    {
        *outputContent = contentStreamBuilder.getOutputContent();
    }

    pdf::PDFDictionary fontDictionary = contentStreamBuilder.getFontDictionary();
    pdf::PDFDictionary xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
    pdf::PDFDictionary graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();

    builder->replaceObjectsByReferences(fontDictionary);
    builder->replaceObjectsByReferences(xobjectDictionary);
    builder->replaceObjectsByReferences(graphicStateDictionary);

    pdf::PDFArray array;
    array.appendItem(pdf::PDFObject::createName("FlateDecode"));

    QByteArray compressedData = pdf::PDFFlateDecodeFilter::compress(contentStreamBuilder.getOutputContent());
    pdf::PDFDictionary contentDictionary;
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedData.size()));
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(qMove(array))));
    pdf::PDFObject contentObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(qMove(contentDictionary), qMove(compressedData)));

    pdf::PDFObject pageObject = builder->getObjectByReference(page->getPageReference());

    pdf::PDFDictionary resourcesDictionary;
    if (const pdf::PDFDictionary* currentResourcesDictionary = document->getDictionaryFromObject(page->getResources()))
    {
        resourcesDictionary = *currentResourcesDictionary;
    }

    auto setResources = [&resourcesDictionary](const char* key, const pdf::PDFDictionary& dictionary)
    {
        if (!dictionary.isEmpty())
        {
            resourcesDictionary.setEntry(pdf::PDFInplaceOrMemoryString(key),
                                         pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(dictionary)));
        }
    };

    setResources("Font", fontDictionary);
    setResources("XObject", xobjectDictionary);
    setResources("ExtGState", graphicStateDictionary);

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Resources");
    factory << resourcesDictionary;
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Contents");
    factory << builder->addObject(std::move(contentObject));
    factory.endDictionaryItem();

    factory.endDictionary();

    pageObject = pdf::PDFObjectManipulator::merge(pageObject, factory.takeObject(), pdf::PDFObjectManipulator::RemoveNullObjects);
    builder->setObject(page->getPageReference(), std::move(pageObject));

    modifier.markReset();
    modifier.finalize();

    return modifier.getDocument();
}

ContentEditorTest::ImagePlacement ContentEditorTest::getImagePlacement(const pdf::PDFEditedPageContent& content)
{
    ImagePlacement placement;

    const size_t elementCount = content.getElementCount();
    for (size_t i = 0; i < elementCount; ++i)
    {
        const pdf::PDFEditedPageContentElement* element = const_cast<pdf::PDFEditedPageContent&>(content).getElement(i);
        const pdf::PDFEditedPageContentElementImage* imageElement = element->asImage();

        if (!imageElement)
        {
            continue;
        }

        QImage image = imageElement->getImage();
        QTransform transform = element->getTransform();

        // The image occupies the unit square in the element coordinate space
        // and the first sample of the image data is located at the corner (0, 1)
        // of that unit square.
        placement.firstSamplePoint = transform.map(QPointF(0.0, 1.0));
        placement.lastSamplePoint = transform.map(QPointF(1.0, 0.0));
        placement.firstSampleColor = image.pixelColor(0, 0);
        break;
    }

    return placement;
}

QImage ContentEditorTest::renderPage(const pdf::PDFDocument* document)
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &activity));

    pdf::PDFRenderer renderer(document, &fontCache, &cms, &activity,
                              pdf::PDFRenderer::Features(), pdf::PDFMeshQualitySettings());

    QImage image(400, 400, QImage::Format_RGB888);
    image.fill(Qt::white);

    QPainter painter(&image);
    renderer.render(&painter, QRectF(0, 0, image.width(), image.height()), 0);
    painter.end();

    return image;
}

void ContentEditorTest::testVariant(Variant variant, bool clearImageObjects)
{
    pdf::PDFDocument document = createDocumentWithImage(variant);
    pdf::PDFEditedPageContent content = processPageContent(&document);
    ImagePlacement original = getImagePlacement(content);

    QVERIFY(original.firstSampleColor.isValid());

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, clearImageObjects, &outputContent);
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent = processPageContent(modifiedDocument.data());
    ImagePlacement modified = getImagePlacement(modifiedContent);

    if (modified.firstSamplePoint != original.firstSamplePoint ||
        modified.lastSamplePoint != original.lastSamplePoint ||
        modified.firstSampleColor != original.firstSampleColor)
    {
        qDebug() << "Content stream:" << outputContent;
        qDebug() << "Original:" << original.firstSamplePoint << original.lastSamplePoint << original.firstSampleColor;
        qDebug() << "Modified:" << modified.firstSamplePoint << modified.lastSamplePoint << modified.firstSampleColor;
    }

    QCOMPARE(modified.firstSampleColor, original.firstSampleColor);
    QCOMPARE(modified.firstSamplePoint, original.firstSamplePoint);
    QCOMPARE(modified.lastSamplePoint, original.lastSamplePoint);
}

void ContentEditorTest::test_image_orientation_plain()
{
    testVariant(Variant::Plain, false);
}

void ContentEditorTest::test_image_orientation_flipped_matrix()
{
    testVariant(Variant::FlippedMatrix, false);
}

void ContentEditorTest::test_image_orientation_form_xobject()
{
    testVariant(Variant::FormXObject, false);
}

void ContentEditorTest::test_image_orientation_inline_image()
{
    testVariant(Variant::InlineImage, false);
}

void ContentEditorTest::test_image_orientation_smask()
{
    testVariant(Variant::SMask, false);
}

void ContentEditorTest::test_image_orientation_image_mask()
{
    testVariant(Variant::ImageMask, false);
}

void ContentEditorTest::test_image_orientation_rotated_matrix()
{
    testVariant(Variant::RotatedMatrix, false);
}

void ContentEditorTest::test_image_orientation_replaced_image()
{
    testVariant(Variant::Plain, true);
}

void ContentEditorTest::test_image_orientation_qt_generated_document()
{
    // Creates a document using the Qt pdf engine (which is a different
    // producer than the pdf4qt document builder) and checks, that the image
    // survives the content stream rewrite without being mirrored.
    QByteArray documentData;

    {
        QBuffer buffer(&documentData);
        QVERIFY(buffer.open(QIODevice::WriteOnly));

        QPdfWriter writer(&buffer);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);

        QPainter painter(&writer);
        painter.setPen(Qt::black);
        painter.drawText(QPointF(50, 50), "Test text");
        painter.drawRect(QRectF(50, 100, 200, 50));
        painter.drawImage(QRectF(50, 200, 160, 80), createTestImage());
        painter.end();
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    pdf::PDFDocument document = reader.readFromBuffer(documentData);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    pdf::PDFEditedPageContent content = processPageContent(&document);
    ImagePlacement original = getImagePlacement(content);
    QVERIFY(original.firstSampleColor.isValid());

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent = processPageContent(modifiedDocument.data());
    ImagePlacement modified = getImagePlacement(modifiedContent);

    if (modified.firstSamplePoint != original.firstSamplePoint ||
        modified.lastSamplePoint != original.lastSamplePoint ||
        modified.firstSampleColor != original.firstSampleColor)
    {
        qDebug() << "Content stream:" << outputContent;
        qDebug() << "Original:" << original.firstSamplePoint << original.lastSamplePoint << original.firstSampleColor;
        qDebug() << "Modified:" << modified.firstSamplePoint << modified.lastSamplePoint << modified.firstSampleColor;
    }

    QCOMPARE(modified.firstSampleColor, original.firstSampleColor);
    QCOMPARE(modified.firstSamplePoint, original.firstSamplePoint);
    QCOMPARE(modified.lastSamplePoint, original.lastSamplePoint);
}

void ContentEditorTest::test_image_orientation_tiling_pattern()
{
    // Issue #238 - the image, which is painted by a tiling pattern, must not
    // disappear, when the page content is edited and written back. The content
    // of the pattern must be decomposed into the edited content elements and
    // the image must be placed exactly as in the plain variant, which paints
    // the same image directly.
    pdf::PDFDocument plainDocument = createDocumentWithImage(Variant::Plain);
    ImagePlacement plain = getImagePlacement(processPageContent(&plainDocument));
    QVERIFY(plain.firstSampleColor.isValid());

    pdf::PDFDocument document = createDocumentWithImage(Variant::TilingPattern);
    pdf::PDFEditedPageContent content = processPageContent(&document);
    ImagePlacement original = getImagePlacement(content);

    QVERIFY(original.firstSampleColor.isValid());
    QCOMPARE(original.firstSamplePoint, plain.firstSamplePoint);
    QCOMPARE(original.lastSamplePoint, plain.lastSamplePoint);
    QCOMPARE(original.firstSampleColor, plain.firstSampleColor);

    testVariant(Variant::TilingPattern, false);
}

void ContentEditorTest::test_complex_tiling_pattern_is_not_processed()
{
    // A tiling pattern with a huge number of tiles would produce an unusable
    // amount of the edited content elements, so it is not processed at all.
    // The processing must not hang and an error must be reported.
    pdf::PDFDocument document = createDocumentWithImage(Variant::ComplexTilingPattern);

    const pdf::PDFPage* page = document.getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &activity));

    pdf::PDFPageContentEditorProcessor processor(page, &document, &fontCache, &cms, &activity,
                                                 QTransform(), pdf::PDFMeshQualitySettings());
    QList<pdf::PDFRenderError> errors = processor.processContents();
    pdf::PDFEditedPageContent content = processor.takeEditedPageContent();

    QCOMPARE(content.getElementCount(), size_t(0));
    QVERIFY(!errors.isEmpty());
}

void ContentEditorTest::test_rendered_page_is_unchanged()
{
    // The rewritten page must render exactly the same way as the original one.
    // This detects any mirroring or displacement of the page content, including
    // the content, which is masked by a soft mask.
    const std::array variants = { Variant::Plain, Variant::FlippedMatrix, Variant::FormXObject,
                                  Variant::SMask, Variant::ImageMask, Variant::TilingPattern };

    for (Variant variant : variants)
    {
        pdf::PDFDocument document = createDocumentWithImage(variant);
        pdf::PDFEditedPageContent content = processPageContent(&document);

        pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
        QVERIFY(modifiedDocument);

        QImage originalImage = renderPage(&document);
        QImage modifiedImage = renderPage(modifiedDocument.data());

        QCOMPARE(originalImage.size(), modifiedImage.size());

        int differentPixelCount = 0;
        for (int y = 0; y < originalImage.height(); ++y)
        {
            for (int x = 0; x < originalImage.width(); ++x)
            {
                if (originalImage.pixel(x, y) != modifiedImage.pixel(x, y))
                {
                    ++differentPixelCount;
                }
            }
        }

        if (differentPixelCount > 0)
        {
            qDebug() << "Variant" << int(variant) << "differs in" << differentPixelCount << "pixels";
            originalImage.save(QString("original_%1.png").arg(int(variant)));
            modifiedImage.save(QString("modified_%1.png").arg(int(variant)));
        }

        QCOMPARE(differentPixelCount, 0);
    }
}

void ContentEditorTest::test_other_resources_are_preserved()
{
    // Resources, which are not regenerated by the content stream builder, must
    // survive the rewrite of the page content. This is important especially for
    // pages, whose resource dictionary is an indirect object.
    pdf::PDFDocument document = createDocumentWithImage(Variant::IndirectResources);
    pdf::PDFEditedPageContent content = processPageContent(&document);

    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
    QVERIFY(modifiedDocument);

    const pdf::PDFPage* page = modifiedDocument->getCatalog()->getPage(0);
    const pdf::PDFDictionary* resources = modifiedDocument->getDictionaryFromObject(page->getResources());
    QVERIFY(resources);

    const pdf::PDFDictionary* colorSpaces = modifiedDocument->getDictionaryFromObject(resources->get("ColorSpace"));
    QVERIFY(colorSpaces);
    QVERIFY(colorSpaces->hasKey("CS0"));

    const pdf::PDFDictionary* xObjects = modifiedDocument->getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xObjects);
    QVERIFY(xObjects->getCount() > 0);
}

void ContentEditorTest::test_numbers_are_not_written_in_exponential_notation()
{
    // Numbers in the content stream must not use the exponential notation
    // (see PDF 32000-1, chapter 7.3.3) - such a number is rejected by the
    // parser and the operator, in which it appears, is not executed.
    pdf::PDFDocument document = createDocumentWithImage(Variant::TinyScale);
    pdf::PDFEditedPageContent content = processPageContent(&document);
    ImagePlacement original = getImagePlacement(content);
    QVERIFY(original.firstSampleColor.isValid());

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    QRegularExpression exponentialNotation("[0-9][eE][+-]?[0-9]");
    QVERIFY2(!exponentialNotation.match(QString::fromLatin1(outputContent)).hasMatch(), outputContent.constData());

    pdf::PDFEditedPageContent modifiedContent = processPageContent(modifiedDocument.data());
    ImagePlacement modified = getImagePlacement(modifiedContent);

    QCOMPARE(modified.firstSampleColor, original.firstSampleColor);
    QCOMPARE(modified.firstSamplePoint, original.firstSamplePoint);
    QCOMPARE(modified.lastSamplePoint, original.lastSamplePoint);
}

QTEST_MAIN(ContentEditorTest)

#include "tst_contenteditortest.moc"
