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
#include "pdftransparencyrenderer.h"
#include "pdfutils.h"

#include <QtTest>
#include <QBuffer>
#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>

#include <array>
#include <functional>
#include <memory>
#include <vector>

/// Records the text matrices at the beginning of each text object and the
/// graphic state updates, which report a change of the text matrices after
/// the end of the first text object, outside of the text objects.
class TextMatrixRecordingProcessor : public pdf::PDFPageContentProcessor
{
public:
    using pdf::PDFPageContentProcessor::PDFPageContentProcessor;

    std::vector<QTransform> textMatricesAtTextBegin;
    std::vector<QTransform> textLineMatricesAtTextBegin;
    int textMatrixUpdatesOutsideTextObject = 0;

protected:
    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override
    {
        pdf::PDFPageContentProcessor::performInterceptInstruction(currentOperator, processOrder, operatorAsText);

        if (currentOperator == Operator::TextBegin && processOrder == ProcessOrder::BeforeOperation)
        {
            textMatricesAtTextBegin.push_back(getGraphicState()->getTextMatrix());
            textLineMatricesAtTextBegin.push_back(getGraphicState()->getTextLineMatrix());
        }

        if (currentOperator == Operator::TextEnd && processOrder == ProcessOrder::AfterOperation)
        {
            m_isTextObjectEnded = true;
        }
    }

    virtual void performUpdateGraphicsState(const pdf::PDFPageContentProcessorState& state) override
    {
        pdf::PDFPageContentProcessor::performUpdateGraphicsState(state);

        // The first update of the page reports the whole initial state as changed
        const pdf::PDFPageContentProcessorState::StateFlags flags = state.getStateFlags();
        const bool isTextMatrixChanged = flags.testFlag(pdf::PDFPageContentProcessorState::StateTextMatrix) ||
                                         flags.testFlag(pdf::PDFPageContentProcessorState::StateTextLineMatrix);
        if (m_isTextObjectEnded && !isTextProcessing() && isTextMatrixChanged)
        {
            ++textMatrixUpdatesOutsideTextObject;
        }
    }

private:
    bool m_isTextObjectEnded = false;
};

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
    void test_inserted_image_is_placed_into_the_rectangle();
    void test_inserted_image_does_not_inherit_transparency_state();
    void test_inserted_image_keeps_the_alpha_channel();
    void test_text_positions_are_preserved();
    void test_text_matrix_is_discarded_at_text_end();
    void test_minus_sign_in_the_middle_of_number_is_ignored();
    void test_invalid_token_keeps_previous_operands();
    void test_invalid_token_invalidates_operator();
    void test_form_xobject_fonts_are_preserved();
    void test_text_colors_are_preserved();
    void test_shading_is_preserved();
    void test_transparency_group_blend_mode_is_preserved();
    void test_transparency_group_is_composed_before_blending();
    void test_refreshed_text_element_uses_valid_fonts();
    void test_shading_color_space_from_form_resources_is_preserved();
    void test_transparency_group_bounding_box_contains_stroke_joins();
    void test_nested_isolated_transparency_group_is_preserved();
    void test_shading_composite_color_space_from_form_resources_is_preserved();
    void test_transparency_group_color_space_is_preserved();
    void test_shading_icc_alternate_color_space_from_form_resources_is_preserved();

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
        ComplexTilingPattern,
        TransparentState
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

    /// Creates the image, which is inserted into the page by the editor image
    /// tool. The left half is opaque, the right half is fully transparent.
    static QImage createInsertedTestImage();

    /// Creates a document with a single page, which contains a single image
    static pdf::PDFDocument createDocumentWithImage(Variant variant);

    /// Creates a document with a single page, which contains the page content
    /// \p pageContent and the standard font Helvetica as the resource /F1
    static pdf::PDFDocument createDocumentWithText(QByteArray pageContent);

    using DictionaryEntries = std::vector<std::pair<const char*, pdf::PDFObject>>;

    /// Creates a document with a single page 200 x 200, which contains the page content
    /// \p pageContent. Page resources are created by \p createResources, which can add
    /// objects (fonts, form XObjects, ...) into the document using the document builder.
    static pdf::PDFDocument createDocument(QByteArray pageContent, const std::function<pdf::PDFDictionary(pdf::PDFDocumentBuilder*)>& createResources);

    /// Creates a dictionary object with the given entries
    static pdf::PDFObject createDictionaryObject(DictionaryEntries entries);

    /// Creates an array object of the given numbers
    static pdf::PDFObject createNumberArrayObject(std::vector<pdf::PDFReal> numbers);

    /// Adds a stream object with the given dictionary entries and content into
    /// the document and returns a reference to it
    static pdf::PDFObject addStreamObject(pdf::PDFDocumentBuilder* builder, DictionaryEntries entries, QByteArray content);

    /// Adds a standard Type 1 font object into the document and returns a reference to it
    static pdf::PDFObject addStandardFontObject(pdf::PDFDocumentBuilder* builder, const char* baseFont);

    /// Returns the number of shading elements of the page content
    static size_t getShadingElementCount(const pdf::PDFEditedPageContent& content);

    /// Returns bounding boxes of all text elements of the page content
    static std::vector<QRectF> getTextBoundingBoxes(const pdf::PDFEditedPageContent& content);

    /// Processes the page content of the first page and returns the edited page content
    static pdf::PDFEditedPageContent processPageContent(const pdf::PDFDocument* document);

    /// Processes the page content of the first page, stores the edited page
    /// content into \p content and returns the errors of the processing
    static QList<pdf::PDFRenderError> processPageContent(const pdf::PDFDocument* document, pdf::PDFEditedPageContent* content);

    /// Returns the color of the page point in the image rendered by renderPage
    static QColor getPageColor(const QImage& image, QPointF point);

    /// Rewrites the content of the first page - the same way as the editor
    /// plugin does it, when the edited page content is written back
    /// to the document.
    /// If \p insertedImage is not null, it is written after all edited elements,
    /// the same way as the editor plugin writes an image inserted by the user.
    static pdf::PDFDocumentPointer rewritePageContent(const pdf::PDFDocument* document,
                                                      const pdf::PDFEditedPageContent& content,
                                                      bool clearImageObjects,
                                                      QByteArray* outputContent,
                                                      const QImage& insertedImage = QImage(),
                                                      const QRectF& insertedRectangle = QRectF());

    /// Returns the placement of all image elements of the page content
    static std::vector<ImagePlacement> getImagePlacements(const pdf::PDFEditedPageContent& content);

    /// Returns the placement of the first image element of the page content
    static ImagePlacement getImagePlacement(const pdf::PDFEditedPageContent& content);

    /// Returns the last image element of the page content, or nullptr
    static const pdf::PDFEditedPageContentElementImage* getLastImageElement(const pdf::PDFEditedPageContent& content);

    /// Performs the whole test - the page content is processed, written back
    /// and processed again. The placement of the image must be the same.
    static void testVariant(Variant variant, bool clearImageObjects);

    /// Renders the first page of the document into the image
    static QImage renderPage(const pdf::PDFDocument* document);

    /// Renders the first page of the document into the image using the transparency
    /// renderer, which composes transparency groups before they are blended with
    /// the backdrop (the renderer used by renderPage only approximates it).
    static QImage renderPageWithTransparency(const pdf::PDFDocument* document);

    /// Returns the number of pixels, whose color components differ by more than \p tolerance
    static int getDifferentPixelCount(const QImage& image1, const QImage& image2, int tolerance);
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

QImage ContentEditorTest::createInsertedTestImage()
{
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            image.setPixelColor(x, y, Qt::green);
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
    pdf::PDFDictionary graphicState;

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

        case Variant::TransparentState:
        {
            // The image is painted with a non-default transparency state, which
            // is a part of the state of the edited element. An element, which is
            // written after it, must not inherit that state.
            pdf::PDFObjectReference imageRef = createImageObject(false);
            xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

            pdf::PDFDictionary transparencyDictionary;
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("ca"), pdf::PDFObject::createReal(0.2));
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("CA"), pdf::PDFObject::createReal(0.2));
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BM"), pdf::PDFObject::createName("Multiply"));

            // The alpha source flag decides, whether the soft mask of an image
            // is interpreted as shape, or as opacity, so it must not leak into
            // the inserted image either.
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("AIS"), pdf::PDFObject::createBool(true));
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("OP"), pdf::PDFObject::createBool(true));
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("op"), pdf::PDFObject::createBool(true));
            transparencyDictionary.setEntry(pdf::PDFInplaceOrMemoryString("OPM"), pdf::PDFObject::createInteger(1));

            graphicState.addEntry(pdf::PDFInplaceOrMemoryString("GS0"),
                                  pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(transparencyDictionary))));

            pageContent = "q /GS0 gs /AbsoluteColorimetric ri 80 0 0 40 10 30 cm /Im1 Do Q";
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

    if (!graphicState.isEmpty())
    {
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"),
                           pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(graphicState))));
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
                                                              QByteArray* outputContent,
                                                              const QImage& insertedImage,
                                                              const QRectF& insertedRectangle)
{
    pdf::PDFDocumentModifier modifier(document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    const pdf::PDFPage* page = document->getCatalog()->getPage(0);

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(const_cast<pdf::PDFDocument*>(document));
    contentStreamBuilder.setFontDictionary(content.getFontDictionary());
    contentStreamBuilder.setXObjectDictionary(content.getXObjectDictionary());
    contentStreamBuilder.setGraphicStateDictionary(content.getGraphicStateDictionary());
    contentStreamBuilder.setShadingDictionary(content.getShadingDictionary());

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

    if (!insertedImage.isNull())
    {
        contentStreamBuilder.writeImage(insertedImage, insertedRectangle);
    }

    if (outputContent)
    {
        *outputContent = contentStreamBuilder.getOutputContent();
    }

    pdf::PDFDictionary fontDictionary = contentStreamBuilder.getFontDictionary();
    pdf::PDFDictionary xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
    pdf::PDFDictionary graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();
    pdf::PDFDictionary shadingDictionary = contentStreamBuilder.getShadingDictionary();

    builder->replaceObjectsByReferences(fontDictionary);
    builder->replaceObjectsByReferences(xobjectDictionary);
    builder->replaceObjectsByReferences(graphicStateDictionary);
    builder->replaceObjectsByReferences(shadingDictionary);

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
    setResources("Shading", shadingDictionary);

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

std::vector<ContentEditorTest::ImagePlacement> ContentEditorTest::getImagePlacements(const pdf::PDFEditedPageContent& content)
{
    std::vector<ImagePlacement> placements;

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
        ImagePlacement placement;
        placement.firstSamplePoint = transform.map(QPointF(0.0, 1.0));
        placement.lastSamplePoint = transform.map(QPointF(1.0, 0.0));
        placement.firstSampleColor = image.pixelColor(0, 0);
        placements.push_back(placement);
    }

    return placements;
}

ContentEditorTest::ImagePlacement ContentEditorTest::getImagePlacement(const pdf::PDFEditedPageContent& content)
{
    std::vector<ImagePlacement> placements = getImagePlacements(content);
    return !placements.empty() ? placements.front() : ImagePlacement();
}

const pdf::PDFEditedPageContentElementImage* ContentEditorTest::getLastImageElement(const pdf::PDFEditedPageContent& content)
{
    const pdf::PDFEditedPageContentElementImage* lastImageElement = nullptr;

    const size_t elementCount = content.getElementCount();
    for (size_t i = 0; i < elementCount; ++i)
    {
        const pdf::PDFEditedPageContentElement* element = const_cast<pdf::PDFEditedPageContent&>(content).getElement(i);

        if (const pdf::PDFEditedPageContentElementImage* imageElement = element->asImage())
        {
            lastImageElement = imageElement;
        }
    }

    return lastImageElement;
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

QImage ContentEditorTest::renderPageWithTransparency(const pdf::PDFDocument* document)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &activity));

    const QSize imageSize(400, 400);
    pdf::PDFInkMapper inkMapper(nullptr, document);
    pdf::PDFTransparencyRendererSettings settings;
    QTransform pagePointToDevicePointMatrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), imageSize));
    pdf::PDFTransparencyRenderer renderer(page, document, &fontCache, &cms, &activity, &inkMapper, settings, pagePointToDevicePointMatrix);

    renderer.beginPaint(imageSize);
    renderer.processContents();
    renderer.endPaint();

    return renderer.toImage(false, true, pdf::PDFRGB{ 1.0f, 1.0f, 1.0f }).convertToFormat(QImage::Format_RGB888);
}

int ContentEditorTest::getDifferentPixelCount(const QImage& image1, const QImage& image2, int tolerance)
{
    if (image1.size() != image2.size())
    {
        return image1.width() * image1.height();
    }

    int count = 0;
    for (int y = 0; y < image1.height(); ++y)
    {
        for (int x = 0; x < image1.width(); ++x)
        {
            const QRgb color1 = image1.pixel(x, y);
            const QRgb color2 = image2.pixel(x, y);

            if (qAbs(qRed(color1) - qRed(color2)) > tolerance ||
                qAbs(qGreen(color1) - qGreen(color2)) > tolerance ||
                qAbs(qBlue(color1) - qBlue(color2)) > tolerance)
            {
                ++count;
            }
        }
    }

    return count;
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

void ContentEditorTest::test_inserted_image_is_placed_into_the_rectangle()
{
    // Issue #413 - an image inserted by the editor image tool is written after
    // all edited elements of the page. It must not be transformed by the
    // transformation matrix of the last written element, which would move it
    // out of the page - and mirror it, if that matrix contains a flip. Both
    // variants place the image of the page by a non-identity matrix, the
    // flipped one by a matrix with a negative vertical scale.
    const std::array variants = { Variant::Plain, Variant::FlippedMatrix };

    for (Variant variant : variants)
    {
        pdf::PDFDocument document = createDocumentWithImage(variant);
        pdf::PDFEditedPageContent content = processPageContent(&document);

        QCOMPARE(getImagePlacements(content).size(), size_t(1));

        const QImage insertedImage = createInsertedTestImage();
        const QRectF insertedRectangle(20, 120, 60, 60);

        QByteArray outputContent;
        pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent, insertedImage, insertedRectangle);
        QVERIFY(modifiedDocument);

        std::vector<ImagePlacement> placements = getImagePlacements(processPageContent(modifiedDocument.data()));
        QCOMPARE(placements.size(), size_t(2));

        // The inserted image is square and the rectangle is square as well, so
        // the image fills the whole rectangle. The first sample of the image
        // data is displayed at the upper left corner of the rectangle - if the
        // image were mirrored, it would be at the lower left corner instead.
        const ImagePlacement& inserted = placements.back();

        if (inserted.firstSamplePoint != insertedRectangle.bottomLeft() ||
            inserted.lastSamplePoint != insertedRectangle.topRight())
        {
            qDebug() << "Variant" << int(variant) << "content stream:" << outputContent;
            qDebug() << "Inserted:" << inserted.firstSamplePoint << inserted.lastSamplePoint;
        }

        QCOMPARE(inserted.firstSamplePoint, insertedRectangle.bottomLeft());
        QCOMPARE(inserted.lastSamplePoint, insertedRectangle.topRight());
        QCOMPARE(inserted.firstSampleColor.rgb(), QColor(Qt::green).rgb());
    }
}

void ContentEditorTest::test_inserted_image_does_not_inherit_transparency_state()
{
    // The inserted image must not inherit the transparency state of the last
    // written element - a leaked alpha or blend mode changes the way the image
    // is composed onto the page (and can make it invisible).
    pdf::PDFDocument document = createDocumentWithImage(Variant::TransparentState);
    pdf::PDFEditedPageContent content = processPageContent(&document);

    const pdf::PDFEditedPageContentElementImage* originalElement = getLastImageElement(content);
    QVERIFY(originalElement);
    QCOMPARE(originalElement->getState().getAlphaFilling(), 0.2);
    QVERIFY(originalElement->getState().getBlendMode() == pdf::BlendMode::Multiply);
    QVERIFY(originalElement->getState().getAlphaIsShape());
    QVERIFY(originalElement->getState().getRenderingIntent() == pdf::RenderingIntent::AbsoluteColorimetric);
    QVERIFY(originalElement->getState().getOverprintMode().overprintFilling);

    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr,
                                                                 createInsertedTestImage(), QRectF(20, 120, 60, 60));
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent = processPageContent(modifiedDocument.data());
    QCOMPARE(getImagePlacements(modifiedContent).size(), size_t(2));

    const pdf::PDFEditedPageContentElementImage* insertedElement = getLastImageElement(modifiedContent);
    QVERIFY(insertedElement);
    QCOMPARE(insertedElement->getState().getAlphaFilling(), 1.0);
    QCOMPARE(insertedElement->getState().getAlphaStroking(), 1.0);
    QVERIFY(insertedElement->getState().getBlendMode() == pdf::BlendMode::Normal);

    // The alpha source flag decides, whether the soft mask of the inserted
    // image is interpreted as shape, or as opacity, and the rendering intent
    // and the overprint affect the colors of the image.
    QVERIFY(!insertedElement->getState().getAlphaIsShape());
    QVERIFY(insertedElement->getState().getRenderingIntent() == pdf::RenderingIntent::Perceptual);
    QVERIFY(insertedElement->getState().getOverprintMode() == pdf::PDFOverprintMode());
}

void ContentEditorTest::test_inserted_image_keeps_the_alpha_channel()
{
    // The alpha channel of the inserted image must be written as a soft mask.
    // Composing the image onto a white background would replace the transparent
    // part by a white rectangle, which covers the content below the image.
    pdf::PDFDocument document = createDocumentWithImage(Variant::Plain);
    pdf::PDFEditedPageContent content = processPageContent(&document);

    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr,
                                                                 createInsertedTestImage(), QRectF(20, 120, 60, 60));
    QVERIFY(modifiedDocument);

    const pdf::PDFPage* page = modifiedDocument->getCatalog()->getPage(0);
    const pdf::PDFDictionary* resources = modifiedDocument->getDictionaryFromObject(page->getResources());
    QVERIFY(resources);

    const pdf::PDFDictionary* xObjects = modifiedDocument->getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xObjects);

    // The inserted image is the only image, which has a soft mask
    const pdf::PDFStream* insertedImageStream = nullptr;
    for (size_t i = 0; i < xObjects->getCount(); ++i)
    {
        const pdf::PDFObject& object = modifiedDocument->getObject(xObjects->getValue(i));

        if (object.isStream() && object.getStream()->getDictionary()->hasKey("SMask"))
        {
            QVERIFY(!insertedImageStream);
            insertedImageStream = object.getStream();
        }
    }

    QVERIFY(insertedImageStream);

    const pdf::PDFObject& softMaskObject = modifiedDocument->getObject(insertedImageStream->getDictionary()->get("SMask"));
    QVERIFY(softMaskObject.isStream());

    const pdf::PDFStream* softMaskStream = softMaskObject.getStream();
    pdf::PDFDocumentDataLoaderDecorator loader(modifiedDocument.data());
    QCOMPARE(loader.readIntegerFromDictionary(softMaskStream->getDictionary(), "Width", 0), pdf::PDFInteger(4));
    QCOMPARE(loader.readIntegerFromDictionary(softMaskStream->getDictionary(), "Height", 0), pdf::PDFInteger(4));
    QCOMPARE(loader.readNameFromDictionary(softMaskStream->getDictionary(), "ColorSpace"), QByteArray("DeviceGray"));

    // The left half of the image is opaque, the right half is transparent
    QByteArray expectedSoftMaskData;
    for (int y = 0; y < 4; ++y)
    {
        expectedSoftMaskData.append(QByteArray::fromHex("ffff0000"));
    }

    QCOMPARE(modifiedDocument->getDecodedStream(softMaskStream), expectedSoftMaskData);

    // The color samples must not be premultiplied by the alpha channel
    QByteArray imageData = modifiedDocument->getDecodedStream(insertedImageStream);
    QCOMPARE(imageData.size(), qsizetype(4 * 4 * 3));
    QCOMPARE(imageData.left(6), QByteArray::fromHex("00ff0000ff00"));
}

pdf::PDFDocument ContentEditorTest::createDocumentWithText(QByteArray pageContent)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFDictionary contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(pageContent.size()));
    pdf::PDFStream contentStream(std::move(contentDict), std::move(pageContent));
    pdf::PDFObjectReference contentRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(contentStream)));

    pdf::PDFDictionary font;
    font.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Type1"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("BaseFont"), pdf::PDFObject::createName("Helvetica"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("Encoding"), pdf::PDFObject::createName("WinAnsiEncoding"));
    pdf::PDFObjectReference fontRef = builder.addObject(
        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(font))));

    pdf::PDFDictionary fonts;
    fonts.addEntry(pdf::PDFInplaceOrMemoryString("F1"), pdf::PDFObject::createReference(fontRef));

    pdf::PDFDictionary resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"),
                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(fonts))));

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));

    return builder.build();
}

std::vector<QRectF> ContentEditorTest::getTextBoundingBoxes(const pdf::PDFEditedPageContent& content)
{
    std::vector<QRectF> boundingBoxes;

    const size_t elementCount = content.getElementCount();
    for (size_t i = 0; i < elementCount; ++i)
    {
        const pdf::PDFEditedPageContentElement* element = const_cast<pdf::PDFEditedPageContent&>(content).getElement(i);

        if (element->asText())
        {
            boundingBoxes.push_back(element->getBoundingBox());
        }
    }

    return boundingBoxes;
}

void ContentEditorTest::test_text_positions_are_preserved()
{
    // Each text element is written into its own BT/ET block, in which the text
    // matrix starts as the identity. The text matrix left by the previous text
    // object must not be used as the initial state of the element - a text object,
    // whose text matrix is equal to that stale matrix, would be written without
    // the Tm operator and displayed at the origin, mirrored by the flipped matrix.
    // In real documents, the stale matrix is equal to the position of the next
    // text object, when that object continues where the previous one ended.
    QByteArray pageContent = "q 1 0 0 -1 0 200 cm "
                             // The text matrix is set after the text is shown, so the
                             // text object ends with the text matrix of the next object
                             "BT /F1 12 Tf 1 0 0 -1 20 30 Tm (A) Tj 1 0 0 -1 60 30 Tm ET "
                             "BT 1 0 0 -1 60 30 Tm (B) Tj ET "
                             // The font is selected before the text matrix
                             "BT /F1 12 Tf 1 0 0 -1 20 80 Tm (C) Tj ET "
                             "Q "
                             // Relative positioning, and text shown at the identity text matrix
                             "BT /F1 12 Tf 40 150 Td (D) Tj ET "
                             "q 1 0 0 1 100 100 cm BT (E) Tj ET Q";

    pdf::PDFDocument document = createDocumentWithText(pageContent);
    pdf::PDFEditedPageContent content = processPageContent(&document);

    std::vector<QRectF> originalBoundingBoxes = getTextBoundingBoxes(content);
    QCOMPARE(originalBoundingBoxes.size(), size_t(5));

    for (const QRectF& boundingBox : originalBoundingBoxes)
    {
        QVERIFY(boundingBox.isValid());
    }

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    // Each text object, which was positioned in the original content stream,
    // must be positioned in the rewritten content stream as well.
    QRegularExpression textObjectExpression("BT(.*?)ET", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator iterator = textObjectExpression.globalMatch(QString::fromLatin1(outputContent));
    int textObjectCount = 0;
    int positionedTextObjectCount = 0;
    while (iterator.hasNext())
    {
        QRegularExpressionMatch match = iterator.next();
        ++textObjectCount;

        if (match.captured(1).contains(" Tm"))
        {
            ++positionedTextObjectCount;
        }
    }

    QVERIFY2(textObjectCount == 5, outputContent.constData());
    QVERIFY2(positionedTextObjectCount == 4, outputContent.constData());

    // The reset of the text matrix by the BT operator must not be written as the text matrix
    QVERIFY2(!outputContent.contains("1 0 0 1 0 0 Tm"), outputContent.constData());

    std::vector<QRectF> modifiedBoundingBoxes = getTextBoundingBoxes(processPageContent(modifiedDocument.data()));
    QCOMPARE(modifiedBoundingBoxes.size(), originalBoundingBoxes.size());

    for (size_t i = 0; i < originalBoundingBoxes.size(); ++i)
    {
        const QRectF& original = originalBoundingBoxes[i];
        const QRectF& modified = modifiedBoundingBoxes[i];

        const bool isSame = qAbs(original.left() - modified.left()) < 0.001 &&
                            qAbs(original.top() - modified.top()) < 0.001 &&
                            qAbs(original.right() - modified.right()) < 0.001 &&
                            qAbs(original.bottom() - modified.bottom()) < 0.001;

        if (!isSame)
        {
            qDebug() << "Text element" << i << "original:" << original << "modified:" << modified;
            qDebug() << "Content stream:" << outputContent;
        }

        QVERIFY(isSame);
    }

    QImage originalImage = renderPage(&document);
    QImage modifiedImage = renderPage(modifiedDocument.data());
    QCOMPARE(modifiedImage, originalImage);
}

void ContentEditorTest::test_text_matrix_is_discarded_at_text_end()
{
    // The text matrix and the text line matrix exist only inside the text object.
    // The matrices of the previous text object must not be visible in the graphic
    // state at the beginning of the next one, and discarding them at the end of the
    // text object must not be reported as a change of the graphic state.
    QByteArray pageContent = "BT /F1 12 Tf 1 0 0 1 20 30 Tm (A) Tj 0 20 Td (B) Tj ET "
                             "1 0 0 1 5 5 cm 0 0 1 rg "
                             "BT (C) Tj ET";

    pdf::PDFDocument document = createDocumentWithText(pageContent);
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &activity));

    TextMatrixRecordingProcessor processor(page, &document, &fontCache, &cms, &activity,
                                           QTransform(), pdf::PDFMeshQualitySettings());
    QList<pdf::PDFRenderError> errors = processor.processContents();
    QVERIFY(errors.isEmpty());

    QCOMPARE(processor.textMatricesAtTextBegin.size(), size_t(2));
    QCOMPARE(processor.textLineMatricesAtTextBegin.size(), size_t(2));

    for (size_t i = 0; i < processor.textMatricesAtTextBegin.size(); ++i)
    {
        QVERIFY(processor.textMatricesAtTextBegin[i].isIdentity());
        QVERIFY(processor.textLineMatricesAtTextBegin[i].isIdentity());
    }

    QCOMPARE(processor.textMatrixUpdatesOutsideTextObject, 0);
}

QColor ContentEditorTest::getPageColor(const QImage& image, QPointF point)
{
    // The page 200 x 200 is rendered into the image 400 x 400 and
    // the vertical axis of the page coordinate space points up.
    return QColor(image.pixel(int(2.0 * point.x()), int(image.height() - 2.0 * point.y())));
}

QList<pdf::PDFRenderError> ContentEditorTest::processPageContent(const pdf::PDFDocument* document, pdf::PDFEditedPageContent* content)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &activity));

    pdf::PDFPageContentEditorProcessor processor(page, document, &fontCache, &cms, &activity,
                                                 QTransform(), pdf::PDFMeshQualitySettings());
    QList<pdf::PDFRenderError> errors = processor.processContents();
    *content = processor.takeEditedPageContent();
    return errors;
}

void ContentEditorTest::test_minus_sign_in_the_middle_of_number_is_ignored()
{
    // Issue #223 - the malformed number "0.00-90" (minus sign in the middle of
    // the number) must be read as the number 0.0090, as other PDF readers do.
    // If the number is rejected, the clipping path loses two of its points and
    // becomes a triangle, which hides a half of the clipped content.
    QByteArray pageContent = "q 0.00-90 0.00-90 m 100 0.00-90 l 100 100 l 0.00-90 100 l h W* n "
                             "0 0 1 rg 0 0 200 200 re f Q";

    pdf::PDFDocument document = createDocumentWithText(pageContent);

    pdf::PDFEditedPageContent content;
    QList<pdf::PDFRenderError> errors = processPageContent(&document, &content);
    QVERIFY(errors.isEmpty());
    QCOMPARE(content.getElementCount(), size_t(1));

    pdf::PDFEditedPageContentElement* element = content.getElement(0);
    QVERIFY(element->asPath());

    const QPainterPath& clipPath = element->getClipPath();
    QVERIFY(clipPath.contains(QPointF(90, 10)));
    QVERIFY(clipPath.contains(QPointF(10, 90)));
    QVERIFY(!clipPath.contains(QPointF(150, 150)));

    QImage originalImage = renderPage(&document);
    QCOMPARE(getPageColor(originalImage, QPointF(90, 10)), QColor(Qt::blue));
    QCOMPARE(getPageColor(originalImage, QPointF(10, 90)), QColor(Qt::blue));
    QCOMPARE(getPageColor(originalImage, QPointF(150, 150)), QColor(Qt::white));

    // The page content written by the editor must keep the whole clipping rectangle
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
    QVERIFY(modifiedDocument);
    QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
}

void ContentEditorTest::test_invalid_token_keeps_previous_operands()
{
    // The token "1.2.3" can't be read. Operands read before it must not be
    // discarded - the rectangle operator reads the first four operands and
    // the invalid token is an extra operand, which is not read at all.
    QByteArray pageContent = "0 0 1 rg 20 30 60 40 1.2.3 re f";

    pdf::PDFDocument document = createDocumentWithText(pageContent);

    pdf::PDFEditedPageContent content;
    QList<pdf::PDFRenderError> errors = processPageContent(&document, &content);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(content.getElementCount(), size_t(1));
    QCOMPARE(content.getElement(0)->getBoundingBox(), QRectF(20, 30, 60, 40));

    QImage image = renderPage(&document);
    QCOMPARE(getPageColor(image, QPointF(50, 50)), QColor(Qt::blue));
    QCOMPARE(getPageColor(image, QPointF(10, 10)), QColor(Qt::white));
}

void ContentEditorTest::test_invalid_token_invalidates_operator()
{
    // The operator, which reads the invalid token "1.2.3", must not be executed.
    // The rest of the invalid token (".3") must not be read as another operand,
    // otherwise the first rectangle is painted with wrong operands (0.3 30 60 40).
    // Operators, which don't read the invalid token, are executed - if the 'Q'
    // operator were not executed, the second rectangle would be translated.
    // The invalid token at the end of the content stream is harmless.
    QByteArray pageContent = "0 0 1 rg "
                             "20 1.2.3 30 60 40 re f "
                             "q 1 0 0 1 -100 0 cm 1.2.3 Q "
                             "120 120 40 40 re f "
                             "1.2.3";

    pdf::PDFDocument document = createDocumentWithText(pageContent);

    pdf::PDFEditedPageContent content;
    QList<pdf::PDFRenderError> errors = processPageContent(&document, &content);
    QCOMPARE(errors.size(), 4);
    QCOMPARE(content.getElementCount(), size_t(1));
    QCOMPARE(content.getElement(0)->getBoundingBox(), QRectF(120, 120, 40, 40));

    QImage image = renderPage(&document);
    QCOMPARE(getPageColor(image, QPointF(10, 50)), QColor(Qt::white));
    QCOMPARE(getPageColor(image, QPointF(40, 50)), QColor(Qt::white));
    QCOMPARE(getPageColor(image, QPointF(40, 140)), QColor(Qt::white));
    QCOMPARE(getPageColor(image, QPointF(140, 140)), QColor(Qt::blue));
}

pdf::PDFDocument ContentEditorTest::createDocument(QByteArray pageContent, const std::function<pdf::PDFDictionary(pdf::PDFDocumentBuilder*)>& createResources)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFObject contentObject = addStreamObject(&builder, DictionaryEntries(), std::move(pageContent));
    pdf::PDFDictionary resources = createResources(&builder);

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), std::move(contentObject));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));

    return builder.build();
}

pdf::PDFObject ContentEditorTest::createDictionaryObject(DictionaryEntries entries)
{
    pdf::PDFDictionary dictionary;
    for (auto& entry : entries)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
    }

    return pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(dictionary)));
}

pdf::PDFObject ContentEditorTest::createNumberArrayObject(std::vector<pdf::PDFReal> numbers)
{
    pdf::PDFArray array;
    for (pdf::PDFReal number : numbers)
    {
        array.appendItem(pdf::PDFObject::createReal(number));
    }

    return pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(array)));
}

pdf::PDFObject ContentEditorTest::addStreamObject(pdf::PDFDocumentBuilder* builder, DictionaryEntries entries, QByteArray content)
{
    pdf::PDFDictionary dictionary;
    for (auto& entry : entries)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
    }
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));

    pdf::PDFObject streamObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(content)));
    return pdf::PDFObject::createReference(builder->addObject(std::move(streamObject)));
}

pdf::PDFObject ContentEditorTest::addStandardFontObject(pdf::PDFDocumentBuilder* builder, const char* baseFont)
{
    pdf::PDFObject fontObject = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Font") },
                                                         { "Subtype", pdf::PDFObject::createName("Type1") },
                                                         { "BaseFont", pdf::PDFObject::createName(baseFont) },
                                                         { "Encoding", pdf::PDFObject::createName("WinAnsiEncoding") } });
    return pdf::PDFObject::createReference(builder->addObject(std::move(fontObject)));
}

size_t ContentEditorTest::getShadingElementCount(const pdf::PDFEditedPageContent& content)
{
    size_t count = 0;
    for (size_t i = 0; i < content.getElementCount(); ++i)
    {
        if (content.getElement(i)->asShading())
        {
            ++count;
        }
    }
    return count;
}

void ContentEditorTest::test_form_xobject_fonts_are_preserved()
{
    // Issue #337 - text painted by a form XObject uses the fonts of the form resources.
    // The font name /F1 denotes a different font in the page resources and the font
    // name /F2 is missing there at all. The rewritten page must use the right fonts.
    QByteArray pageContent = "BT /F1 16 Tf 20 160 Td (Page) Tj ET q /Fm1 Do Q";
    QByteArray formContent = "BT /F1 16 Tf 20 110 Td (Form) Tj /F2 16 Tf 0 -40 Td (Bold) Tj ET";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject formFonts = createDictionaryObject({ { "F1", addStandardFontObject(builder, "Courier") },
                                                            { "F2", addStandardFontObject(builder, "Times-Bold") } });
        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Resources", createDictionaryObject({ { "Font", formFonts } }) } }, formContent);

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), createDictionaryObject({ { "F1", addStandardFontObject(builder, "Helvetica") } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(getTextBoundingBoxes(content).size(), size_t(2));

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent;
    QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message));
    QCOMPARE(getTextBoundingBoxes(modifiedContent).size(), size_t(2));

    // Each font used by the rewritten content stream must denote the original font
    const pdf::PDFPage* page = modifiedDocument->getCatalog()->getPage(0);
    const pdf::PDFDictionary* resources = modifiedDocument->getDictionaryFromObject(page->getResources());
    QVERIFY(resources);
    const pdf::PDFDictionary* fonts = modifiedDocument->getDictionaryFromObject(resources->get("Font"));
    QVERIFY(fonts);

    QSet<QByteArray> baseFonts;
    QRegularExpression fontExpression("/(\\S+) [0-9.]+ Tf");
    QRegularExpressionMatchIterator iterator = fontExpression.globalMatch(QString::fromLatin1(outputContent));
    while (iterator.hasNext())
    {
        const QByteArray key = iterator.next().captured(1).toLatin1();
        const pdf::PDFDictionary* font = modifiedDocument->getDictionaryFromObject(fonts->get(key));
        QVERIFY2(font, key.constData());
        baseFonts.insert(modifiedDocument->getObject(font->get("BaseFont")).getString());
    }

    QVERIFY2(baseFonts == QSet<QByteArray>({ "Helvetica", "Courier", "Times-Bold" }), outputContent.constData());
    QCOMPARE(renderPage(modifiedDocument.data()), renderPage(&document));
}

void ContentEditorTest::test_text_colors_are_preserved()
{
    // Issue #337 - colors changed inside the text object must be preserved
    QByteArray pageContent = "BT /F1 24 Tf 20 170 Td "
                             "1 0 0 rg (Red) Tj "
                             "0 -35 Td 0 0 1 rg (Blue) Tj "
                             "0 -35 Td 0.5 g (Gray) Tj "
                             "0 -35 Td 1 0 1 0 k (Cmyk) Tj "
                             "0 -35 Td 2 Tr 0 1 0 RG (Stroke) Tj ET "
                             "150 20 30 30 re f";

    pdf::PDFDocument document = createDocumentWithText(pageContent);
    pdf::PDFEditedPageContent content = processPageContent(&document);
    QCOMPARE(getTextBoundingBoxes(content).size(), size_t(1));

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    QVERIFY2(outputContent.contains("\n0 0 1 rg\n"), outputContent.constData());
    QVERIFY2(outputContent.contains("\n1 0 1 0 k\n"), outputContent.constData());
    QVERIFY2(outputContent.contains("\n0 1 0 RG\n"), outputContent.constData());

    QImage originalImage = renderPage(&document);
    QCOMPARE(getPageColor(originalImage, QPointF(165, 35)), QColor(0, 255, 0));
    QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
}

void ContentEditorTest::test_shading_is_preserved()
{
    // Issue #337 - shadings painted by the 'sh' operator must be preserved, including the
    // shading painted by a form XObject, whose shading name denotes a different shading
    // in the page resources.
    QByteArray pageContent = "q 20 20 160 70 re W n /Sh0 sh Q q /Fm1 Do Q";
    QByteArray formContent = "q 20 110 160 70 re W n /Sh0 sh Q";

    auto createAxialShading = [](std::vector<pdf::PDFReal> coords, std::vector<pdf::PDFReal> c0, std::vector<pdf::PDFReal> c1)
    {
        pdf::PDFArray extend;
        extend.appendItem(pdf::PDFObject::createBool(true));
        extend.appendItem(pdf::PDFObject::createBool(true));

        pdf::PDFObject function = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                           { "Domain", createNumberArrayObject({ 0, 1 }) },
                                                           { "C0", createNumberArrayObject(std::move(c0)) },
                                                           { "C1", createNumberArrayObject(std::move(c1)) },
                                                           { "N", pdf::PDFObject::createInteger(1) } });

        return createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                        { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                        { "Coords", createNumberArrayObject(std::move(coords)) },
                                        { "Function", function },
                                        { "Extend", pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(extend))) } });
    };

    pdf::PDFDocument document = createDocument(pageContent, [&](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject formShading = createAxialShading({ 0, 110, 0, 180 }, { 0, 1, 0 }, { 1, 1, 0 });
        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Resources", createDictionaryObject({ { "Shading", createDictionaryObject({ { "Sh0", formShading } }) } }) } }, formContent);

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), createDictionaryObject({ { "Sh0", createAxialShading({ 20, 0, 180, 0 }, { 1, 0, 0 }, { 0, 0, 1 }) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(content.getElementCount(), size_t(2));
    QCOMPARE(getShadingElementCount(content), size_t(2));
    QVERIFY(QRectF(19, 19, 162, 72).contains(content.getElement(0)->getBoundingBox()));
    QVERIFY(QRectF(19, 109, 162, 72).contains(content.getElement(1)->getBoundingBox()));

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);
    QCOMPARE(outputContent.count(" sh\n"), 2);

    pdf::PDFEditedPageContent modifiedContent;
    QVERIFY(processPageContent(modifiedDocument.data(), &modifiedContent).isEmpty());
    QCOMPARE(getShadingElementCount(modifiedContent), size_t(2));

    // The page shading goes from red to blue, the form shading from green to yellow
    QImage originalImage = renderPage(&document);
    QColor leftColor = getPageColor(originalImage, QPointF(25, 50));
    QColor rightColor = getPageColor(originalImage, QPointF(175, 50));
    QColor formColor = getPageColor(originalImage, QPointF(100, 115));
    QVERIFY(leftColor.red() > 200 && leftColor.blue() < 55);
    QVERIFY(rightColor.blue() > 200 && rightColor.red() < 55);
    QVERIFY(formColor.green() > 200 && formColor.blue() < 55);
    QCOMPARE(getPageColor(originalImage, QPointF(100, 100)), QColor(Qt::white));

    QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
}

void ContentEditorTest::test_transparency_group_blend_mode_is_preserved()
{
    // Issue #337 - the content of a transparency group is written directly into the page,
    // so the blend mode, with which the group is composed onto the page, must be applied
    // to the content of the group. Otherwise the white rectangle of the group covers
    // the gray rectangle below it, instead of being multiplied with it.
    QByteArray pageContent = "0.5 g 20 20 160 160 re f q /GS1 gs /Fm1 Do Q";
    QByteArray formContent = "1 g 40 40 120 120 re f 1 0 0 rg 80 80 40 40 re f";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject group = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Group") },
                                                        { "S", pdf::PDFObject::createName("Transparency") } });
        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Group", group } }, formContent);

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GS1", createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                                                                                                  { "BM", pdf::PDFObject::createName("Multiply") } }) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(content.getElementCount(), size_t(3));
    // The elements of the group are painted with the normal blend mode. The blend mode
    // is used to compose the whole group, which is written back as a form XObject.
    QVERIFY(!content.getElement(0)->getTransparencyGroup());
    for (size_t i = 1; i < 3; ++i)
    {
        const pdf::PDFEditedPageContentTransparencyGroupPointer& group = content.getElement(i)->getTransparencyGroup();
        QVERIFY(group);
        QVERIFY(group == content.getElement(1)->getTransparencyGroup());
        QVERIFY(group->blendMode == pdf::BlendMode::Multiply);
        QVERIFY(content.getElement(i)->getState().getBlendMode() == pdf::BlendMode::Normal);
    }

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);
    QCOMPARE(outputContent.count(" Do\n"), 1);

    pdf::PDFEditedPageContent modifiedContent;
    QVERIFY(processPageContent(modifiedDocument.data(), &modifiedContent).isEmpty());
    QCOMPARE(modifiedContent.getElementCount(), size_t(3));
    QVERIFY(modifiedContent.getElement(2)->getTransparencyGroup());
    QVERIFY(modifiedContent.getElement(2)->getTransparencyGroup()->blendMode == pdf::BlendMode::Multiply);

    QCOMPARE(renderPage(modifiedDocument.data()), renderPage(&document));
    QCOMPARE(getDifferentPixelCount(renderPageWithTransparency(modifiedDocument.data()), renderPageWithTransparency(&document), 2), 0);
}

void ContentEditorTest::test_transparency_group_is_composed_before_blending()
{
    // Issue #337 - the blend mode and the constant alpha of a transparency group can't
    // be applied to each object of the group. The blue rectangle covers the red one inside
    // the group, then the composed group is multiplied with the white page and painted
    // with the half opacity. So the overlap must be light blue, not dark violet.
    QByteArray pageContent = "q /GS1 gs /Fm1 Do Q";
    QByteArray formContent = "1 0 0 rg 20 20 120 120 re f 0 0 1 rg 60 60 120 120 re f";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject group = createDictionaryObject({ { "Type", pdf::PDFObject::createName("Group") },
                                                        { "S", pdf::PDFObject::createName("Transparency") },
                                                        { "I", pdf::PDFObject::createBool(true) } });
        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Group", group } }, formContent);

        pdf::PDFObject graphicState = createDictionaryObject({ { "Type", pdf::PDFObject::createName("ExtGState") },
                                                               { "BM", pdf::PDFObject::createName("Multiply") },
                                                               { "ca", pdf::PDFObject::createReal(0.5) } });

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GS1", graphicState } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(content.getElementCount(), size_t(2));

    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent;
    QVERIFY(processPageContent(modifiedDocument.data(), &modifiedContent).isEmpty());
    QCOMPARE(modifiedContent.getElementCount(), size_t(2));

    auto isColor = [](const QColor& color, int red, int green, int blue)
    {
        return qAbs(color.red() - red) <= 4 && qAbs(color.green() - green) <= 4 && qAbs(color.blue() - blue) <= 4;
    };

    const QImage originalImage = renderPageWithTransparency(&document);
    const QImage modifiedImage = renderPageWithTransparency(modifiedDocument.data());

    for (const QImage& image : { originalImage, modifiedImage })
    {
        const QColor redColor = getPageColor(image, QPointF(30, 30));
        const QColor overlapColor = getPageColor(image, QPointF(100, 100));
        const QColor blueColor = getPageColor(image, QPointF(170, 170));
        QVERIFY2(isColor(redColor, 255, 128, 128), qPrintable(redColor.name()));
        QVERIFY2(isColor(overlapColor, 128, 128, 255), qPrintable(overlapColor.name()));
        QVERIFY2(isColor(blueColor, 128, 128, 255), qPrintable(blueColor.name()));
    }

    QCOMPARE(getDifferentPixelCount(modifiedImage, originalImage, 2), 0);
}

void ContentEditorTest::test_refreshed_text_element_uses_valid_fonts()
{
    // The editor refreshes the edited text element from a temporary document, into which
    // the element was written. Font objects created only in the temporary document (here
    // the fallback font replacing an unknown font) must not be used in the edited document.
    // Font resources are filtered by the editor, the content stream builder must cope
    // with the unfiltered font resources too.
    for (const bool isFontResourcesFiltered : { false, true })
    {
        pdf::PDFDocument document = createDocumentWithText("BT /F1 24 Tf 20 100 Td (Hello) Tj ET");
        pdf::PDFEditedPageContent content = processPageContent(&document);
        QCOMPARE(content.getElementCount(), size_t(1));

        pdf::PDFEditedPageContentElementText* targetElement = content.getElement(0)->asText();
        QVERIFY(targetElement);
        targetElement->setItemsAsText(targetElement->getItemsAsText().replace("F1", "MissingFont"));

        pdf::PDFDocumentPointer temporaryDocument = rewritePageContent(&document, content, false, nullptr);
        QVERIFY(temporaryDocument);

        pdf::PDFEditedPageContent temporaryContent = processPageContent(temporaryDocument.data());
        QCOMPARE(temporaryContent.getElementCount(), size_t(1));
        const pdf::PDFEditedPageContentElementText* sourceElement = temporaryContent.getElement(0)->asText();
        QVERIFY(sourceElement);

        // The same refresh, as EditorPlugin::updateTextElement performs
        targetElement->setState(sourceElement->getState());
        targetElement->setTextPath(sourceElement->getTextPath());
        targetElement->setItems(sourceElement->getItems());
        targetElement->setTransform(sourceElement->getTransform());
        targetElement->setClipPath(sourceElement->getClipPath());
        targetElement->setFontResources(isFontResourcesFiltered ? pdf::PDFEditedPageContentElementText::getFontResourcesValidInDocument(sourceElement->getFontResources(), temporaryDocument.data(), &document)
                                                                : sourceElement->getFontResources());
        targetElement->setItemsAsText(sourceElement->getItemsAsText());

        if (isFontResourcesFiltered)
        {
            QVERIFY(!targetElement->getFontResources().empty());
            for (const pdf::PDFEditedPageContentElementText::FontResource& fontResource : targetElement->getFontResources())
            {
                QVERIFY(fontResource.fontObject.isNull());
            }
        }

        QByteArray outputContent;
        pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
        QVERIFY(modifiedDocument);

        pdf::PDFEditedPageContent modifiedContent;
        QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message + "\n" + QString::fromLatin1(outputContent)));
        QCOMPARE(getTextBoundingBoxes(modifiedContent).size(), size_t(1));
        QCOMPARE(renderPage(modifiedDocument.data()), renderPage(temporaryDocument.data()));
    }
}

void ContentEditorTest::test_shading_color_space_from_form_resources_is_preserved()
{
    // Issue #337 - the color space of the shading painted by a form XObject can be a name
    // of the color space resource of the form. The same name denotes another color space
    // in the page resources.
    QByteArray pageContent = "q /Fm1 Do Q";
    QByteArray formContent = "q 20 20 160 160 re W n /Sh0 sh Q";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject function = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                           { "Domain", createNumberArrayObject({ 0, 1 }) },
                                                           { "C0", createNumberArrayObject({ 1, 0, 0 }) },
                                                           { "C1", createNumberArrayObject({ 0, 0, 1 }) },
                                                           { "N", pdf::PDFObject::createInteger(1) } });

        pdf::PDFObject shading = createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                                          { "ColorSpace", pdf::PDFObject::createName("LocalRGB") },
                                                          { "Coords", createNumberArrayObject({ 20, 0, 180, 0 }) },
                                                          { "Function", function } });

        pdf::PDFObject formResources = createDictionaryObject({ { "Shading", createDictionaryObject({ { "Sh0", shading } }) },
                                                                { "ColorSpace", createDictionaryObject({ { "LocalRGB", pdf::PDFObject::createName("DeviceRGB") } }) } });

        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Resources", formResources } }, formContent);

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), createDictionaryObject({ { "LocalRGB", pdf::PDFObject::createName("DeviceGray") } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(getShadingElementCount(content), size_t(1));

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);
    QCOMPARE(outputContent.count(" sh\n"), 1);

    pdf::PDFEditedPageContent modifiedContent;
    QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message));
    QCOMPARE(getShadingElementCount(modifiedContent), size_t(1));

    // The shading goes from red to blue
    QImage originalImage = renderPage(&document);
    QColor leftColor = getPageColor(originalImage, QPointF(25, 100));
    QColor rightColor = getPageColor(originalImage, QPointF(175, 100));
    QVERIFY2(leftColor.red() > 200 && leftColor.green() < 55 && leftColor.blue() < 55, qPrintable(leftColor.name()));
    QVERIFY2(rightColor.blue() > 200 && rightColor.green() < 55 && rightColor.red() < 55, qPrintable(rightColor.name()));

    QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
}

void ContentEditorTest::test_transparency_group_bounding_box_contains_stroke_joins()
{
    // Issue #337 - the bounding box of the form XObject of a transparency group must
    // contain the whole stroke of the paths. The miter join of the sharp angle exceeds
    // the path far more than by the line width.
    QByteArray pageContent = "q /GS1 gs /Fm1 Do Q";
    QByteArray formContent = "0 0 1 RG 10 w 0 j 10 M 80 20 m 100 160 l 120 20 l S";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject group = createDictionaryObject({ { "S", pdf::PDFObject::createName("Transparency") },
                                                        { "I", pdf::PDFObject::createBool(true) } });
        pdf::PDFObject formObject = addStreamObject(builder, { { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Group", group } }, formContent);

        pdf::PDFObject graphicState = createDictionaryObject({ { "ca", pdf::PDFObject::createReal(0.5) },
                                                               { "CA", pdf::PDFObject::createReal(0.5) } });

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), createDictionaryObject({ { "GS1", graphicState } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(content.getElementCount(), size_t(1));

    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
    QVERIFY(modifiedDocument);

    const QImage originalImage = renderPageWithTransparency(&document);
    const QImage modifiedImage = renderPageWithTransparency(modifiedDocument.data());

    // The tip of the miter join is above the path (the path ends at y = 160)
    const QColor tipColor = getPageColor(originalImage, QPointF(100, 175));
    QVERIFY2(tipColor != QColor(Qt::white), qPrintable(tipColor.name()));
    QCOMPARE(getPageColor(modifiedImage, QPointF(100, 175)), tipColor);
    QCOMPARE(getDifferentPixelCount(modifiedImage, originalImage, 2), 0);
}

void ContentEditorTest::test_nested_isolated_transparency_group_is_preserved()
{
    // Issue #337 - the isolated transparency group must be preserved, even if it is painted
    // with the normal blend mode and without constant alpha. The blue rectangle is multiplied
    // with the transparent backdrop of the inner isolated group (so it stays blue) and then
    // it covers the red rectangle. If the inner group was flattened, the blue rectangle would
    // be multiplied with the red rectangle.
    QByteArray pageContent = "q /Outer gs /Fm1 Do Q";
    QByteArray outerFormContent = "1 0 0 rg 20 20 120 120 re f /Fm2 Do";
    QByteArray innerFormContent = "q /Multiply gs 0 0 1 rg 60 60 120 120 re f Q";

    pdf::PDFDocument document = createDocument(pageContent, [&](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject group = createDictionaryObject({ { "S", pdf::PDFObject::createName("Transparency") },
                                                        { "I", pdf::PDFObject::createBool(true) } });
        pdf::PDFObject innerFormObject = addStreamObject(builder, { { "Subtype", pdf::PDFObject::createName("Form") },
                                                                    { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                                    { "Group", group } }, innerFormContent);
        pdf::PDFObject outerFormObject = addStreamObject(builder, { { "Subtype", pdf::PDFObject::createName("Form") },
                                                                    { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                                    { "Group", group } }, outerFormContent);

        pdf::PDFObject graphicStates = createDictionaryObject({ { "Outer", createDictionaryObject({ { "ca", pdf::PDFObject::createReal(0.5) } }) },
                                                                { "Multiply", createDictionaryObject({ { "BM", pdf::PDFObject::createName("Multiply") } }) } });

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", outerFormObject }, { "Fm2", innerFormObject } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), std::move(graphicStates));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(content.getElementCount(), size_t(2));

    // The blue rectangle is in the inner group, which is nested in the outer group
    const pdf::PDFEditedPageContentTransparencyGroupPointer& outerGroup = content.getElement(0)->getTransparencyGroup();
    const pdf::PDFEditedPageContentTransparencyGroupPointer& innerGroup = content.getElement(1)->getTransparencyGroup();
    QVERIFY(outerGroup);
    QVERIFY(innerGroup);
    QVERIFY(innerGroup != outerGroup);
    QVERIFY(innerGroup->parent == outerGroup);
    QVERIFY(innerGroup->isolated);

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);

    pdf::PDFEditedPageContent modifiedContent;
    QVERIFY(processPageContent(modifiedDocument.data(), &modifiedContent).isEmpty());
    QCOMPARE(modifiedContent.getElementCount(), size_t(2));

    const QImage originalImage = renderPageWithTransparency(&document);
    const QImage modifiedImage = renderPageWithTransparency(modifiedDocument.data());

    const QColor overlapColor = getPageColor(originalImage, QPointF(100, 100));
    QVERIFY2(qAbs(overlapColor.red() - 128) <= 4 && qAbs(overlapColor.green() - 128) <= 4 && qAbs(overlapColor.blue() - 255) <= 4, qPrintable(overlapColor.name()));
    QCOMPARE(getDifferentPixelCount(modifiedImage, originalImage, 2), 0);
}

void ContentEditorTest::test_shading_composite_color_space_from_form_resources_is_preserved()
{
    // Issue #337 - the color space of the shading painted by a form XObject can depend on
    // a color space resource of the form (here the alternate color space of the separation
    // and of the DeviceN color space). The same name denotes another color space in the page
    // resources.
    for (const bool isDeviceN : { false, true })
    {
        QByteArray pageContent = "q /Fm1 Do Q";
        QByteArray formContent = "q 20 20 160 160 re W n /Sh0 sh Q";

        pdf::PDFDocument document = createDocument(pageContent, [&formContent, isDeviceN](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject function = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                               { "Domain", createNumberArrayObject({ 0, 1 }) },
                                                               { "C0", createNumberArrayObject({ 0 }) },
                                                               { "C1", createNumberArrayObject({ 1 }) },
                                                               { "N", pdf::PDFObject::createInteger(1) } });

            // The tint transformation maps the tint 0 to red and the tint 1 to blue
            pdf::PDFObject tintTransform = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                                    { "Domain", createNumberArrayObject({ 0, 1 }) },
                                                                    { "C0", createNumberArrayObject({ 1, 0, 0 }) },
                                                                    { "C1", createNumberArrayObject({ 0, 0, 1 }) },
                                                                    { "N", pdf::PDFObject::createInteger(1) } });

            pdf::PDFArray colorSpace;
            if (isDeviceN)
            {
                pdf::PDFArray colorantNames;
                colorantNames.appendItem(pdf::PDFObject::createName("Spot"));

                colorSpace.appendItem(pdf::PDFObject::createName("DeviceN"));
                colorSpace.appendItem(pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(colorantNames))));
            }
            else
            {
                colorSpace.appendItem(pdf::PDFObject::createName("Separation"));
                colorSpace.appendItem(pdf::PDFObject::createName("Spot"));
            }
            colorSpace.appendItem(pdf::PDFObject::createName("LocalRGB"));
            colorSpace.appendItem(tintTransform);

            pdf::PDFObject shading = createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                                              { "ColorSpace", pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(colorSpace))) },
                                                              { "Coords", createNumberArrayObject({ 20, 0, 180, 0 }) },
                                                              { "Function", function } });

            pdf::PDFObject formResources = createDictionaryObject({ { "Shading", createDictionaryObject({ { "Sh0", shading } }) },
                                                                    { "ColorSpace", createDictionaryObject({ { "LocalRGB", pdf::PDFObject::createName("DeviceRGB") } }) } });

            pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                                   { "Subtype", pdf::PDFObject::createName("Form") },
                                                                   { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                                   { "Resources", formResources } }, formContent);

            pdf::PDFDictionary resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), createDictionaryObject({ { "LocalRGB", pdf::PDFObject::createName("DeviceGray") } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
            return resources;
        });

        pdf::PDFEditedPageContent content;
        QVERIFY(processPageContent(&document, &content).isEmpty());
        QCOMPARE(getShadingElementCount(content), size_t(1));

        QByteArray outputContent;
        pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
        QVERIFY(modifiedDocument);
        QCOMPARE(outputContent.count(" sh\n"), 1);

        pdf::PDFEditedPageContent modifiedContent;
        QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message));
        QCOMPARE(getShadingElementCount(modifiedContent), size_t(1));

        // The shading goes from red to blue
        QImage originalImage = renderPage(&document);
        QColor leftColor = getPageColor(originalImage, QPointF(25, 100));
        QColor rightColor = getPageColor(originalImage, QPointF(175, 100));
        QVERIFY2(leftColor.red() > 200 && leftColor.green() < 55 && leftColor.blue() < 55, qPrintable(leftColor.name()));
        QVERIFY2(rightColor.blue() > 200 && rightColor.green() < 55 && rightColor.red() < 55, qPrintable(rightColor.name()));

        QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
    }
}

void ContentEditorTest::test_transparency_group_color_space_is_preserved()
{
    // Issue #337 - the colors of the transparency group are converted into the blending
    // color space of the group, so the red rectangle of the group with the gray color space
    // is gray. The color space can be given directly, or as a name of the color space
    // resource of the form XObject, which paints the group.
    for (const bool isColorSpaceResource : { false, true })
    {
        pdf::PDFDocument document = createDocument("q /Fm1 Do Q", [isColorSpaceResource](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFObject group = createDictionaryObject({ { "S", pdf::PDFObject::createName("Transparency") },
                                                            { "I", pdf::PDFObject::createBool(true) },
                                                            { "CS", pdf::PDFObject::createName(isColorSpaceResource ? "LocalGray" : "DeviceGray") } });
            pdf::PDFObject groupFormObject = addStreamObject(builder, { { "Subtype", pdf::PDFObject::createName("Form") },
                                                                        { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                                        { "Group", group } }, "1 0 0 rg 20 20 160 160 re f");

            pdf::PDFDictionary resources;
            if (isColorSpaceResource)
            {
                pdf::PDFObject formResources = createDictionaryObject({ { "ColorSpace", createDictionaryObject({ { "LocalGray", pdf::PDFObject::createName("DeviceGray") } }) },
                                                                        { "XObject", createDictionaryObject({ { "Fm2", groupFormObject } }) } });
                pdf::PDFObject formObject = addStreamObject(builder, { { "Subtype", pdf::PDFObject::createName("Form") },
                                                                       { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                                       { "Resources", formResources } }, "/Fm2 Do");
                resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
            }
            else
            {
                resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", groupFormObject } }));
            }
            return resources;
        });

        pdf::PDFEditedPageContent content;
        QVERIFY(processPageContent(&document, &content).isEmpty());
        QCOMPARE(content.getElementCount(), size_t(1));

        const pdf::PDFEditedPageContentTransparencyGroupPointer& group = content.getElement(0)->getTransparencyGroup();
        QVERIFY(group);
        QVERIFY(group->colorSpaceObject == pdf::PDFObject::createName("DeviceGray"));

        pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, nullptr);
        QVERIFY(modifiedDocument);

        pdf::PDFEditedPageContent modifiedContent;
        QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message));
        QCOMPARE(modifiedContent.getElementCount(), size_t(1));

        const QImage originalImage = renderPageWithTransparency(&document);
        const QImage modifiedImage = renderPageWithTransparency(modifiedDocument.data());

        const QColor color = getPageColor(originalImage, QPointF(100, 100));
        QVERIFY2(color.red() == color.green() && color.green() == color.blue() && color.red() < 128, qPrintable(color.name()));
        QCOMPARE(getDifferentPixelCount(modifiedImage, originalImage, 2), 0);
    }
}

void ContentEditorTest::test_shading_icc_alternate_color_space_from_form_resources_is_preserved()
{
    // Issue #337 - the alternate color space of the ICC based color space of the shading
    // painted by a form XObject can be a name of the color space resource of the form.
    const QByteArray iccProfile = QColorSpace(QColorSpace::SRgb).iccProfile();
    QVERIFY(!iccProfile.isEmpty());

    QByteArray pageContent = "q /Fm1 Do Q";
    QByteArray formContent = "q 20 20 160 160 re W n /Sh0 sh Q";

    pdf::PDFDocument document = createDocument(pageContent, [&formContent, &iccProfile](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFObject function = createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                           { "Domain", createNumberArrayObject({ 0, 1 }) },
                                                           { "C0", createNumberArrayObject({ 1, 0, 0 }) },
                                                           { "C1", createNumberArrayObject({ 0, 0, 1 }) },
                                                           { "N", pdf::PDFObject::createInteger(1) } });

        pdf::PDFObject profileObject = addStreamObject(builder, { { "N", pdf::PDFObject::createInteger(3) },
                                                                  { "Alternate", pdf::PDFObject::createName("LocalRGB") } }, iccProfile);

        pdf::PDFArray colorSpace;
        colorSpace.appendItem(pdf::PDFObject::createName("ICCBased"));
        colorSpace.appendItem(profileObject);

        pdf::PDFObject shading = createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                                          { "ColorSpace", pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(colorSpace))) },
                                                          { "Coords", createNumberArrayObject({ 20, 0, 180, 0 }) },
                                                          { "Function", function } });

        pdf::PDFObject formResources = createDictionaryObject({ { "Shading", createDictionaryObject({ { "Sh0", shading } }) },
                                                                { "ColorSpace", createDictionaryObject({ { "LocalRGB", pdf::PDFObject::createName("DeviceRGB") } }) } });

        pdf::PDFObject formObject = addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") },
                                                               { "Subtype", pdf::PDFObject::createName("Form") },
                                                               { "BBox", createNumberArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Resources", formResources } }, formContent);

        pdf::PDFDictionary resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), createDictionaryObject({ { "Fm1", formObject } }));
        return resources;
    });

    pdf::PDFEditedPageContent content;
    QVERIFY(processPageContent(&document, &content).isEmpty());
    QCOMPARE(getShadingElementCount(content), size_t(1));

    QByteArray outputContent;
    pdf::PDFDocumentPointer modifiedDocument = rewritePageContent(&document, content, false, &outputContent);
    QVERIFY(modifiedDocument);
    QCOMPARE(outputContent.count(" sh\n"), 1);

    pdf::PDFEditedPageContent modifiedContent;
    QList<pdf::PDFRenderError> errors = processPageContent(modifiedDocument.data(), &modifiedContent);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.front().message));
    QCOMPARE(getShadingElementCount(modifiedContent), size_t(1));

    QImage originalImage = renderPage(&document);
    QVERIFY(getPageColor(originalImage, QPointF(100, 100)) != QColor(Qt::white));
    QCOMPARE(renderPage(modifiedDocument.data()), originalImage);
}

QTEST_MAIN(ContentEditorTest)

#include "tst_contenteditortest.moc"
