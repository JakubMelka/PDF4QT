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

// Edit Content, stage E1, package P10: the display queries of the core (pdfcontentdisplay.h;
// GUI_overhaul/phase2_briefs/pe1_p10.md, pe1_skeleton_notes.md 8.4).
//
// The expected values are computed by hand from the content streams of the tests, the test
// documents (contenttestdocuments.h) and ISO 32000-2; the computation is written in the comments.

#include "contenttestdocuments.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfcontentdisplay.h"
#include "pdfcontentnode.h"
#include "pdfcontentstate.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfobject.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"

#include <QtTest>
#include <QByteArray>
#include <QRectF>
#include <QTransform>

#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

using namespace pdf;

using Documents = pdftest::ContentTestDocuments;

class ContentDisplayTest : public QObject
{
    Q_OBJECT

private slots:
    // item 1
    void displayStateOfBuilder();
    void displayStateOfExtGState();
    void displayStateOfOverride();
    void displayStateOfAncestorWrap();
    void displayStateOfPatterns();
    void containerMatrixOfForms();
    void containerMatrixCycleAndDepth();
    void containerMatrixOfPatternCell();

    // item 2
    void boundingBoxOfPaths();
    void boundingBoxOfLeaves();
    void boundingBoxOfGroupsAndUnknown();
    void boundingBoxOfShading();
    void boundingBoxThroughUses();

    // item 3
    void textGlyphs();
    void textLines();
    void textLineTolerance();
    void textLineDirection();
    void textVertical();
    void textFlags();
    void textOverride();
    void textThreads();

    // item 4
    void imageOfXObject();
    void imageDecodeAndMasks();
    void imageInlineAndForm();
    void imageLimits();
    void imageRefused();

    // item 5
    void clipChain();
    void clipChainMarkedContentAndText();
    void clippedNodes();

    // item 6
    void counts();

    // item 7
    void shadingGradients();
    void tilingCell();

private:
    static PDFContentContainerKey pageKey(const PDFDocument& document);
    static PDFContentContainerPointer build(const PDFDocument& document, const PDFContentContainerKey& key);

    /// The nodes of a kind in the order of the content
    static std::vector<PDFContentNodePointer> getNodes(const PDFContentContainer& container, PDFContentNodeKind kind);

    /// A copy of the container with an override of a node
    static PDFContentContainerPointer withOverride(const PDFContentContainerPointer& container, PDFContentNodeId id, const PDFContentStateOverride& override);

    static PDFContentStateValue makeReal(PDFReal value);
    static PDFContentStateValue makeInteger(PDFInteger value);
    static PDFContentStateValue makeColor(QByteArray deviceSpace, std::vector<PDFReal> components);

    static bool isClose(PDFReal left, PDFReal right) { return std::abs(left - right) <= 1e-9; }
    static bool isClose(const QTransform& left, const QTransform& right);
    static bool isClose(const QRectF& left, const QRectF& right, PDFReal tolerance);
    static bool isClose(const QPointF& left, const QPointF& right) { return isClose(left.x(), right.x()) && isClose(left.y(), right.y()); }
    static bool isRgb(const QColor& color, int red, int green, int blue);

    static PDFContentDisplayContext createContext(const PDFDocument* document);

    /// The document of the shadings: /Sh1 axial without /BBox, /Sh2 axial with /BBox [0 0 50 50] (both
    /// /Coords [0 0 100 0], DeviceRGB, the function of the type 2 from red to blue, N 1), /Sh3 radial,
    /// /Sh4 function based (type 1); the shading pattern /PS
    static PDFDocument createShadingDocument(const QByteArray& content);

    /// A hand-made form container of the key Form(reference) with /Matrix \p matrix and a FormUse
    /// (identifier 2, the identity as its state) of the form \p usedForm (none, when not valid)
    static PDFContentContainerPointer createHandMadeForm(PDFObjectReference reference, const QTransform& matrix, PDFObjectReference usedForm);

    /// A page with \p content and the image XObjects of \p images: /Im0, /Im1, ... - each the
    /// entries of the dictionary (/Type /XObject /Subtype /Image added) and the data
    using ImageDefinition = std::pair<Documents::DictionaryEntries, QByteArray>;
    static PDFDocument createImageDocument(const QByteArray& content, std::vector<ImageDefinition> images);

    /// Decodes the first image of the page
    static PDFContentDisplayImage decodeFirstImage(const PDFDocument& document, const PDFContentDisplayImageOptions& options, const PDFOperationControl* control);

    /// The color of a pixel (not premultiplied) as "r g b a"
    static QByteArray getPixel(const QImage& image, int x, int y);

    /// An operation control, which is canceled
    class CanceledControl : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return true; }
    };
};

PDFContentContainerKey ContentDisplayTest::pageKey(const PDFDocument& document)
{
    return PDFContentContainerKey::createPage(0, document.getCatalog()->getPage(0)->getPageReference());
}

PDFContentContainerPointer ContentDisplayTest::build(const PDFDocument& document, const PDFContentContainerKey& key)
{
    // The caches live only for the call: the container must not need them afterwards
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFContentBuildContext context;
    context.fontCache = &fontCache;
    context.iccProfileCache = &iccProfileCache;
    context.cms = &cms;
    context.pageIndex = 0;

    return PDFContentTreeBuilder::build(&document, key, nullptr, context, PDFContentBuildOptions(), nullptr, nullptr).container;
}

std::vector<PDFContentNodePointer> ContentDisplayTest::getNodes(const PDFContentContainer& container, PDFContentNodeKind kind)
{
    std::vector<PDFContentNodePointer> result;
    std::vector<PDFContentNodePointer> stack = { container.getRoot() };
    while (!stack.empty())
    {
        PDFContentNodePointer node = stack.back();
        stack.pop_back();
        if (node->getKind() == kind)
        {
            result.push_back(node);
        }
        const auto& children = node->getChildren();
        for (auto it = children.crbegin(); it != children.crend(); ++it)
        {
            stack.push_back(*it);
        }
    }
    return result;
}

PDFContentContainerPointer ContentDisplayTest::withOverride(const PDFContentContainerPointer& container, PDFContentNodeId id, const PDFContentStateOverride& override)
{
    PDFContentContainerEditor editor(container);
    PDFContentNode::Data data = *editor.getNodeData(id);
    data.override = override;
    editor.setNodeData(id, data);
    return editor.build();
}

PDFContentStateValue ContentDisplayTest::makeReal(PDFReal value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Real;
    result.real = value;
    return result;
}

PDFContentStateValue ContentDisplayTest::makeInteger(PDFInteger value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Integer;
    result.integer = value;
    return result;
}

PDFContentStateValue ContentDisplayTest::makeColor(QByteArray deviceSpace, std::vector<PDFReal> components)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Color;
    result.color.deviceSpace = std::move(deviceSpace);
    result.color.components = std::move(components);
    return result;
}

bool ContentDisplayTest::isClose(const QTransform& left, const QTransform& right)
{
    return isClose(left.m11(), right.m11()) && isClose(left.m12(), right.m12()) && isClose(left.m21(), right.m21()) &&
           isClose(left.m22(), right.m22()) && isClose(left.dx(), right.dx()) && isClose(left.dy(), right.dy());
}

bool ContentDisplayTest::isClose(const QRectF& left, const QRectF& right, PDFReal tolerance)
{
    const bool result = std::abs(left.left() - right.left()) <= tolerance && std::abs(left.top() - right.top()) <= tolerance &&
                        std::abs(left.width() - right.width()) <= tolerance && std::abs(left.height() - right.height()) <= tolerance;
    if (!result)
    {
        qWarning() << "rectangle" << left << "expected" << right;
    }
    return result;
}

bool ContentDisplayTest::isRgb(const QColor& color, int red, int green, int blue)
{
    // 8-bit channels of an opaque RGB color, one step of rounding allowed (0.5 -> 127 or 128)
    const bool result = color.isValid() && color.spec() == QColor::Rgb && std::abs(color.red() - red) <= 1 &&
                        std::abs(color.green() - green) <= 1 && std::abs(color.blue() - blue) <= 1 && color.alpha() == 255;
    if (!result)
    {
        qWarning() << "color" << color << "expected" << red << green << blue;
    }
    return result;
}

PDFContentDisplayContext ContentDisplayTest::createContext(const PDFDocument* document)
{
    PDFContentDisplayContext context;
    context.document = document;
    return context;
}

PDFDocument ContentDisplayTest::createShadingDocument(const QByteArray& content)
{
    return Documents::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        Q_UNUSED(builder);
        auto createShading = [](bool hasBox)
        {
            PDFObject function = Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) },
                                                                     { "Domain", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                     { "C0", Documents::createIntegerArrayObject({ 1, 0, 0 }) },
                                                                     { "C1", Documents::createIntegerArrayObject({ 0, 0, 1 }) },
                                                                     { "N", PDFObject::createInteger(1) } });
            Documents::DictionaryEntries entries = { { "ShadingType", PDFObject::createInteger(2) },
                                                     { "ColorSpace", PDFObject::createName("DeviceRGB") },
                                                     { "Coords", Documents::createIntegerArrayObject({ 0, 0, 100, 0 }) },
                                                     { "Function", std::move(function) } };
            if (hasBox)
            {
                entries.emplace_back("BBox", Documents::createIntegerArrayObject({ 0, 0, 50, 50 }));
            }
            return Documents::createDictionaryObject(std::move(entries));
        };

        // /Sh3: radial, the circles (10 10) r 0 and (10 10) r 50, /Extend [true false]
        PDFObject radial = Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(3) },
                                                               { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                               { "Coords", Documents::createIntegerArrayObject({ 10, 10, 0, 10, 10, 50 }) },
                                                               { "Extend", Documents::createArrayObject({ PDFObject::createBool(true), PDFObject::createBool(false) }) },
                                                               { "Function", Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) },
                                                                                                                 { "Domain", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                                                                 { "C0", Documents::createIntegerArrayObject({ 0 }) },
                                                                                                                 { "C1", Documents::createIntegerArrayObject({ 1 }) },
                                                                                                                 { "N", PDFObject::createInteger(1) } }) } });

        // /Sh4: function based (type 1), the PostScript function { pop } of two variables
        PDFObject function = Documents::addStreamObject(builder, { { "FunctionType", PDFObject::createInteger(4) },
                                                                   { "Domain", Documents::createIntegerArrayObject({ 0, 1, 0, 1 }) },
                                                                   { "Range", Documents::createIntegerArrayObject({ 0, 1 }) } }, QByteArray("{ pop }"));
        PDFObject functionShading = Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(1) },
                                                                        { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                                        { "Function", function } });

        // /PS: a shading pattern of the axial shading with /Matrix [1 0 0 1 5 5]
        PDFObject pattern = Documents::createDictionaryObject({ { "PatternType", PDFObject::createInteger(2) },
                                                                { "Shading", createShading(false) },
                                                                { "Matrix", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 5, 5 }) } });

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Shading"), Documents::createDictionaryObject({ { "Sh1", createShading(false) }, { "Sh2", createShading(true) },
                                                                                                    { "Sh3", radial }, { "Sh4", functionShading } }));
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "PS", pattern } }));
        return resources;
    });
}

PDFContentContainerPointer ContentDisplayTest::createHandMadeForm(PDFObjectReference reference, const QTransform& matrix, PDFObjectReference usedForm)
{
    PDFContentContainerEditor editor(PDFContentContainerKey::createForm(reference));
    editor.getContainerData().matrix = matrix;
    if (usedForm.isValid())
    {
        PDFContentNode::Data data;
        data.kind = PDFContentNodeKind::FormUse;
        data.id = PDFContentNodeId{ 2 };
        data.state = std::make_shared<const PDFContentResolvedState>();
        data.symbolicState = std::make_shared<const PDFContentSymbolicState>();
        data.xobject = usedForm;
        editor.insertNode(std::move(data), PDFContentNodeId{ 1 }, -1);
    }
    return editor.build();
}

PDFDocument ContentDisplayTest::createImageDocument(const QByteArray& content, std::vector<ImageDefinition> images)
{
    return Documents::createDocument(content, [&images](PDFDocumentBuilder* builder)
    {
        Documents::DictionaryEntries xobjects;
        for (size_t i = 0; i < images.size(); ++i)
        {
            Documents::DictionaryEntries entries = { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") } };
            for (auto& entry : images[i].first)
            {
                entries.push_back(std::move(entry));
            }
            xobjects.emplace_back("Im" + QByteArray::number(qulonglong(i)), Documents::addStreamObject(builder, std::move(entries), images[i].second));
        }

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject(std::move(xobjects)));
        return resources;
    });
}

PDFContentDisplayImage ContentDisplayTest::decodeFirstImage(const PDFDocument& document, const PDFContentDisplayImageOptions& options, const PDFOperationControl* control)
{
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> images = getNodes(*container, PDFContentNodeKind::Image);
    if (images.empty())
    {
        return PDFContentDisplayImage();
    }
    return PDFContentDisplay::decodeImage(&document, container, images[0]->getId(), options, control);
}

QByteArray ContentDisplayTest::getPixel(const QImage& image, int x, int y)
{
    const QColor color = image.pixelColor(x, y);
    return QByteArray::number(color.red()) + " " + QByteArray::number(color.green()) + " " + QByteArray::number(color.blue()) + " " + QByteArray::number(color.alpha());
}

// ---------------------------------------------------------------- item 1

void ContentDisplayTest::displayStateOfBuilder()
{
    // The state of the path: 2 w, 1 J (round cap), 2 j (bevel join), 5 M, [3 1] 0 d, the red fill,
    // the blue stroke, the CTM [2 0 0 2 10 20] (ISO 32000-2, 8.4.3)
    PDFDocument document = Documents::createDocument("q 2 w 1 J 2 j 5 M [3 1] 0 d 1 0 0 rg 0 0 1 RG 2 0 0 2 10 20 cm 0 0 10 10 re B Q", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    QVERIFY(container);

    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(1));

    const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, paths[0]->getId(), createContext(&document));
    QVERIFY(state);
    QVERIFY(isClose(state->matrix, QTransform(2, 0, 0, 2, 10, 20)));
    QCOMPARE(state->lineWidth, 2.0);
    QCOMPARE(state->lineCap, Qt::RoundCap);
    QCOMPARE(state->lineJoin, Qt::BevelJoin);
    QCOMPARE(state->miterLimit, 5.0);
    QCOMPARE(state->dashPattern.getDashArray(), (std::vector<PDFReal>{ 3, 1 }));
    QCOMPARE(state->dashPattern.getDashOffset(), 0.0);
    QCOMPARE(state->fillColor.kind, PDFContentDisplayColor::Kind::Color);
    QVERIFY(isRgb(state->fillColor.color, 255, 0, 0));
    QVERIFY(!state->fillColor.isOverridden);
    QVERIFY(isRgb(state->strokeColor.color, 0, 0, 255));
    QCOMPARE(state->alphaFilling, 1.0);
    QCOMPARE(state->blendMode, BlendMode::Normal);
    QVERIFY(!state->hasSoftMask);
    QVERIFY(state->overriddenProperties.isEmpty());

    // The group itself has the state before q (the identity, the defaults: black, 1 w)
    const std::optional<PDFContentDisplayState> groupState = PDFContentDisplay::getDisplayState(*container, getNodes(*container, PDFContentNodeKind::Group)[0]->getId(), createContext(&document));
    QVERIFY(groupState);
    QVERIFY(groupState->matrix.isIdentity());
    QCOMPARE(groupState->lineWidth, 1.0);
    QVERIFY(isRgb(groupState->fillColor.color, 0, 0, 0));

    // An unknown node has no state
    QVERIFY(!PDFContentDisplay::getDisplayState(*container, PDFContentNodeId{ 999 }, createContext(&document)));
}

void ContentDisplayTest::displayStateOfExtGState()
{
    // /G1: /CA 0.5 /ca 0.25 /BM /Multiply (ISO 32000-2, Table 57)
    PDFDocument document = Documents::createDocument("/G1 gs 0 0 10 10 re f", [](PDFDocumentBuilder*)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "G1", Documents::createDictionaryObject({ { "CA", PDFObject::createReal(0.5) },
                                                                                                                                                 { "ca", PDFObject::createReal(0.25) },
                                                                                                                                                 { "BM", PDFObject::createName("Multiply") } }) } }));
        return resources;
    });
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, getNodes(*container, PDFContentNodeKind::Path)[0]->getId(), createContext(&document));
    QVERIFY(state);
    QCOMPARE(state->alphaStroking, 0.5);
    QCOMPARE(state->alphaFilling, 0.25);
    QCOMPARE(state->blendMode, BlendMode::Multiply);

    // The soft mask of /GS1 is in effect at the image
    PDFDocument maskDocument = Documents::createSoftMaskImageDocument();
    PDFContentContainerPointer maskContainer = build(maskDocument, pageKey(maskDocument));
    const std::optional<PDFContentDisplayState> maskState = PDFContentDisplay::getDisplayState(*maskContainer, getNodes(*maskContainer, PDFContentNodeKind::Image)[0]->getId(), createContext(&maskDocument));
    QVERIFY(maskState);
    QVERIFY(maskState->hasSoftMask);

    // 3 Tr: the text show is invisible
    PDFDocument textDocument = Documents::createFontsDocument("BT /F1 10 Tf 3 Tr (A) Tj ET");
    PDFContentContainerPointer textContainer = build(textDocument, pageKey(textDocument));
    const std::optional<PDFContentDisplayState> textState = PDFContentDisplay::getDisplayState(*textContainer, getNodes(*textContainer, PDFContentNodeKind::TextShow)[0]->getId(), createContext(&textDocument));
    QVERIFY(textState);
    QCOMPARE(textState->textRenderingMode, TextRenderingMode::Invisible);
}

void ContentDisplayTest::displayStateOfOverride()
{
    PDFDocument document = Documents::createDocument("q 2 0 0 2 10 20 cm 0 0 10 10 re B Q", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId pathId = getNodes(*container, PDFContentNodeKind::Path)[0]->getId();

    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 5, 7);
    override.properties[PDFContentStateProperty::LineWidth] = makeReal(4);
    override.properties[PDFContentStateProperty::LineCap] = makeInteger(2);
    override.properties[PDFContentStateProperty::LineJoin] = makeInteger(1);
    override.properties[PDFContentStateProperty::AlphaFilling] = makeReal(0.5);
    override.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceRGB", { 0, 1, 0 });
    override.properties[PDFContentStateProperty::StrokeColor] = makeColor("DeviceCMYK", { 0, 0, 0, 1 });
    PDFContentStateValue blend;
    blend.kind = PDFContentStateValue::Kind::Name;
    blend.name = "Screen";
    override.properties[PDFContentStateProperty::BlendMode] = blend;

    PDFContentContainerPointer changed = withOverride(container, pathId, override);
    const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*changed, pathId, createContext(&document));
    QVERIFY(state);

    // T x CTM: a point p goes to (p + (5, 7)) * 2 + (10, 20) = (2x + 20, 2y + 34)
    QVERIFY(isClose(state->matrix, QTransform(2, 0, 0, 2, 20, 34)));
    QCOMPARE(state->lineWidth, 4.0);
    QCOMPARE(state->lineCap, Qt::SquareCap);
    QCOMPARE(state->lineJoin, Qt::RoundJoin);
    QCOMPARE(state->alphaFilling, 0.5);
    QCOMPARE(state->alphaStroking, 1.0);
    QCOMPARE(state->blendMode, BlendMode::Screen);
    QCOMPARE(state->fillColor.kind, PDFContentDisplayColor::Kind::Color);
    QVERIFY(state->fillColor.isOverridden);
    QVERIFY(isRgb(state->fillColor.color, 0, 255, 0));
    QCOMPARE(state->fillColor.value.components, (std::vector<PDFReal>{ 0, 1, 0 }));

    // DeviceCMYK 0 0 0 1 is black (the generic conversion: R = (1 - C)(1 - K) ...)
    QVERIFY(isRgb(state->strokeColor.color, 0, 0, 0));
    QVERIFY(state->overriddenProperties.contains(PDFContentStateProperty::FillColor));
    QVERIFY(state->overriddenProperties.contains(PDFContentStateProperty::LineWidth));
    QVERIFY(!state->overriddenProperties.contains(PDFContentStateProperty::DashPattern));

    // DeviceGray 0.5 is the gray of the half intensity; a blend mode array takes its first known name
    PDFContentStateOverride grayOverride;
    grayOverride.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceGray", { 0.5 });
    PDFContentStateValue blendArray;
    blendArray.kind = PDFContentStateValue::Kind::Object;
    blendArray.object = Documents::createArrayObject({ PDFObject::createName("Unknown"), PDFObject::createName("Darken") });
    grayOverride.properties[PDFContentStateProperty::BlendMode] = blendArray;
    const std::optional<PDFContentDisplayState> grayState = PDFContentDisplay::getDisplayState(*withOverride(container, pathId, grayOverride), pathId, createContext(&document));
    QVERIFY(grayState);
    QVERIFY(isRgb(grayState->fillColor.color, 128, 128, 128));
    QCOMPARE(grayState->blendMode, BlendMode::Darken);
    QVERIFY(isClose(grayState->matrix, QTransform(2, 0, 0, 2, 10, 20)));

    // A wrong number of components: the color is not converted, the value is kept
    PDFContentStateOverride wrongOverride;
    wrongOverride.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceRGB", { 0.5, 0.5 });
    const std::optional<PDFContentDisplayState> wrongState = PDFContentDisplay::getDisplayState(*withOverride(container, pathId, wrongOverride), pathId, createContext(&document));
    QVERIFY(wrongState);
    QCOMPARE(wrongState->fillColor.kind, PDFContentDisplayColor::Kind::Unconverted);
    QCOMPARE(wrongState->fillColor.value.components.size(), size_t(2));

    // A space of the document: [/Indexed /DeviceRGB 1 <FF0000 00FF00>], the index 1 is green
    // (ISO 32000-2, 8.6.6.3); without a document it stays unconverted
    PDFContentStateOverride indexedOverride;
    PDFContentStateValue indexed = makeColor(QByteArray(), { 1 });
    indexed.color.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("Indexed"), PDFObject::createName("DeviceRGB"), PDFObject::createInteger(1),
                                                                    PDFObject::createString(QByteArray("\xFF\x00\x00\x00\xFF\x00", 6)) });
    indexedOverride.properties[PDFContentStateProperty::FillColor] = indexed;
    PDFContentContainerPointer indexedContainer = withOverride(container, pathId, indexedOverride);
    const std::optional<PDFContentDisplayState> indexedState = PDFContentDisplay::getDisplayState(*indexedContainer, pathId, createContext(&document));
    QVERIFY(indexedState);
    QCOMPARE(indexedState->fillColor.kind, PDFContentDisplayColor::Kind::Color);
    QVERIFY(isRgb(indexedState->fillColor.color, 0, 255, 0));
    const std::optional<PDFContentDisplayState> noDocumentState = PDFContentDisplay::getDisplayState(*indexedContainer, pathId, createContext(nullptr));
    QVERIFY(noDocumentState);
    QCOMPARE(noDocumentState->fillColor.kind, PDFContentDisplayColor::Kind::Unconverted);
    QCOMPARE(noDocumentState->fillColor.value.components, (std::vector<PDFReal>{ 1 }));
}

void ContentDisplayTest::displayStateOfAncestorWrap()
{
    // The wrap of the text object (R-1) reaches its shows: the matrix M = [1 0 0 1 5 0] (the CTM
    // of the shows equals the one at BT, so T = M) and the red fill - except for the show B, whose
    // fill the operator rg inside of the object sets (its symbolic source differs)
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf (A) Tj 0 0 1 rg (B) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId textId = getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId();

    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 5, 0);
    override.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceRGB", { 1, 0, 0 });
    PDFContentContainerPointer changed = withOverride(container, textId, override);

    const std::vector<PDFContentNodePointer> shows = getNodes(*changed, PDFContentNodeKind::TextShow);
    QCOMPARE(shows.size(), size_t(2));
    const std::optional<PDFContentDisplayState> stateA = PDFContentDisplay::getDisplayState(*changed, shows[0]->getId(), createContext(&document));
    const std::optional<PDFContentDisplayState> stateB = PDFContentDisplay::getDisplayState(*changed, shows[1]->getId(), createContext(&document));
    QVERIFY(stateA && stateB);
    QVERIFY(isClose(stateA->matrix, QTransform(1, 0, 0, 1, 5, 0)));
    QVERIFY(isClose(stateB->matrix, QTransform(1, 0, 0, 1, 5, 0)));
    QVERIFY(isRgb(stateA->fillColor.color, 255, 0, 0));
    QVERIFY(stateA->fillColor.isOverridden);
    QVERIFY(isRgb(stateB->fillColor.color, 0, 0, 255));
    QVERIFY(!stateB->fillColor.isOverridden);
}

void ContentDisplayTest::displayStateOfPatterns()
{
    // createTwoTilingPatternsDocument: "/CsP cs 0 0.5 0 /PU scn ... f" (the uncolored /PU in the
    // color 0 0.5 0 of DeviceRGB), "/Pattern cs /PC scn ... f" (the colored /PC), "/CsP CS 1 0 0 /PU
    // SCN ... S" (the stroke /PU in red)
    PDFDocument document = Documents::createTwoTilingPatternsDocument();
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(3));

    const PDFDictionary* resources = document.getDictionaryFromObject(document.getCatalog()->getPage(0)->getResources());
    const PDFDictionary* patterns = document.getDictionaryFromObject(resources->get("Pattern"));
    const PDFObject uncolored = patterns->get("PU");
    const PDFObject colored = patterns->get("PC");
    QVERIFY(uncolored.isReference() && colored.isReference());

    const std::optional<PDFContentDisplayState> first = PDFContentDisplay::getDisplayState(*container, paths[0]->getId(), createContext(&document));
    QVERIFY(first);
    QCOMPARE(first->fillColor.kind, PDFContentDisplayColor::Kind::Pattern);
    QCOMPARE(first->fillColor.patternObject.getReference(), uncolored.getReference());
    QVERIFY(isRgb(first->fillColor.color, 0, 128, 0));

    const std::optional<PDFContentDisplayState> second = PDFContentDisplay::getDisplayState(*container, paths[1]->getId(), createContext(&document));
    QVERIFY(second);
    QCOMPARE(second->fillColor.kind, PDFContentDisplayColor::Kind::Pattern);
    QCOMPARE(second->fillColor.patternObject.getReference(), colored.getReference());
    QVERIFY(!second->fillColor.color.isValid());

    const std::optional<PDFContentDisplayState> third = PDFContentDisplay::getDisplayState(*container, paths[2]->getId(), createContext(&document));
    QVERIFY(third);
    QCOMPARE(third->strokeColor.kind, PDFContentDisplayColor::Kind::Pattern);
    QVERIFY(isRgb(third->strokeColor.color, 255, 0, 0));
    QCOMPARE(third->lineWidth, 8.0);
}

void ContentDisplayTest::containerMatrixOfForms()
{
    // createFormDocument: /Fm1 (/Matrix [2 0 0 2 10 20]) drawn by "q /Fm1 Do Q" and by "q 1 0 0 1 100 0
    // cm /Fm1 Do Q" - one form shared by two paths of uses -, /Fm2 by "/Fm2 Do"
    PDFDocument document = Documents::createFormDocument();
    PDFContentContainerPointer page = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> uses = getNodes(*page, PDFContentNodeKind::FormUse);
    QCOMPARE(uses.size(), size_t(3));

    PDFContentContainerPointer form = build(document, PDFContentContainerKey::createForm(uses[0]->getData().xobject));
    QVERIFY(form);

    // An empty path: the default space of the container itself
    QVERIFY(PDFContentDisplay::getContainerMatrix(*form, { })->isIdentity());

    const std::optional<QTransform> first = PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ page, uses[0]->getId() } });
    const std::optional<QTransform> second = PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ page, uses[1]->getId() } });
    QVERIFY(first && second);
    QVERIFY(isClose(*first, QTransform(2, 0, 0, 2, 10, 20)));
    QVERIFY(isClose(*second, QTransform(2, 0, 0, 2, 110, 20)));

    // The rectangle of the form [5 5 25 15] is at x 120 ... 160, y 30 ... 50 through the second use
    const PDFContentNodeId rectangleId = getNodes(*form, PDFContentNodeKind::Path)[0]->getId();
    const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*form, rectangleId, createContext(&document), { PDFContentDisplayUse{ page, uses[1]->getId() } });
    QVERIFY(state);
    QVERIFY(isClose(state->matrix, QTransform(2, 0, 0, 2, 110, 20)));
    QCOMPARE(state->lineWidth, 4.0);

    // The use of another form, a node which is no use, a missing node or container, a page inside
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ page, uses[2]->getId() } }));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ page, getNodes(*page, PDFContentNodeKind::Group)[0]->getId() } }));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ page, PDFContentNodeId{ 999 } } }));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ nullptr, uses[0]->getId() } }));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*page, { PDFContentDisplayUse{ form, rectangleId } }));
    QVERIFY(!PDFContentDisplay::getDisplayState(*form, rectangleId, createContext(&document), { PDFContentDisplayUse{ page, uses[2]->getId() } }));

    // An override of the use moves the form: T x CTM of the use = [1 0 0 1 0 5] x [1 0 0 1 100 0]
    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 0, 5);
    PDFContentContainerPointer movedPage = withOverride(page, uses[1]->getId(), override);
    const std::optional<QTransform> moved = PDFContentDisplay::getContainerMatrix(*form, { PDFContentDisplayUse{ movedPage, uses[1]->getId() } });
    QVERIFY(moved);
    QVERIFY(isClose(*moved, QTransform(2, 0, 0, 2, 110, 25)));
}

void ContentDisplayTest::containerMatrixCycleAndDepth()
{
    // A form, which draws itself: one visited set of the walk refuses the second visit
    const PDFObjectReference selfReference(500, 0);
    PDFContentContainerPointer self = createHandMadeForm(selfReference, QTransform(1, 0, 0, 1, 1, 0), selfReference);
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*self, { PDFContentDisplayUse{ self, PDFContentNodeId{ 2 } } }));

    // Two forms drawing each other: A <- B <- A
    const PDFObjectReference referenceA(501, 0);
    const PDFObjectReference referenceB(502, 0);
    PDFContentContainerPointer formA = createHandMadeForm(referenceA, QTransform(), referenceB);
    PDFContentContainerPointer formB = createHandMadeForm(referenceB, QTransform(), referenceA);
    QVERIFY(PDFContentDisplay::getContainerMatrix(*formA, { PDFContentDisplayUse{ formB, PDFContentNodeId{ 2 } } }));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*formA, { PDFContentDisplayUse{ formA, PDFContentNodeId{ 2 } }, PDFContentDisplayUse{ formB, PDFContentNodeId{ 2 } } }));

    // A chain of forms C0 <- C1 <- ... <- C33, each /Matrix [1 0 0 1 1 0]: a path of 32 levels gives
    // the sum of 32 translations of the inner containers C0 ... C31, a path of 33 levels is refused
    std::vector<PDFContentContainerPointer> chain;
    for (int i = 0; i <= 33; ++i)
    {
        chain.push_back(createHandMadeForm(PDFObjectReference(1000 + i, 0), QTransform(1, 0, 0, 1, 1, 0), i > 0 ? PDFObjectReference(1000 + i - 1, 0) : PDFObjectReference()));
    }

    auto createPath = [&chain](int levelCount)
    {
        PDFContentDisplayUsePath path;
        for (int i = levelCount; i >= 1; --i)
        {
            path.push_back(PDFContentDisplayUse{ chain[size_t(i)], PDFContentNodeId{ 2 } });
        }
        return path;
    };

    const std::optional<QTransform> deepest = PDFContentDisplay::getContainerMatrix(*chain[0], createPath(PDFContentDisplay::MAXIMUM_USE_DEPTH));
    QVERIFY(deepest);
    QVERIFY(isClose(*deepest, QTransform(1, 0, 0, 1, 32, 0)));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*chain[0], createPath(PDFContentDisplay::MAXIMUM_USE_DEPTH + 1)));
}

void ContentDisplayTest::containerMatrixOfPatternCell()
{
    // /PU has /Matrix [1 0 0 1 3 3]: the pattern space -> the page (ISO 32000-2, 8.7.3.1). The first
    // path fills with /PU, the second one with /PC, the third one strokes with /PU.
    PDFDocument document = Documents::createTwoTilingPatternsDocument();
    PDFContentContainerPointer page = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*page, PDFContentNodeKind::Path);

    const PDFDictionary* resources = document.getDictionaryFromObject(document.getCatalog()->getPage(0)->getResources());
    const PDFObject uncolored = document.getDictionaryFromObject(resources->get("Pattern"))->get("PU");
    PDFContentContainerPointer cell = build(document, PDFContentContainerKey::createPatternCell(uncolored.getReference()));
    QVERIFY(cell);

    const std::optional<QTransform> fill = PDFContentDisplay::getContainerMatrix(*cell, { PDFContentDisplayUse{ page, paths[0]->getId() } });
    const std::optional<QTransform> stroke = PDFContentDisplay::getContainerMatrix(*cell, { PDFContentDisplayUse{ page, paths[2]->getId() } });
    QVERIFY(fill && stroke);
    QVERIFY(isClose(*fill, QTransform(1, 0, 0, 1, 3, 3)));
    QVERIFY(isClose(*stroke, QTransform(1, 0, 0, 1, 3, 3)));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*cell, { PDFContentDisplayUse{ page, paths[1]->getId() } }));
}

// ---------------------------------------------------------------- item 2

void ContentDisplayTest::boundingBoxOfPaths()
{
    // 1: 4 w, the rectangle x 10 ... 40, y 20 ... 60 stroked with miter joins: half of the width
    //    around it, x 8 ... 42, y 18 ... 62
    // 2: 2 w 2 J (square cap), a line (10 10) - (30 10): x 9 ... 31, y 9 ... 11
    // 3: 0 w, a line (50 50) - (70 50): the thinnest line adds nothing, x 50 ... 70, y 50
    // 4: the rectangle filled: x 10 ... 40, y 20 ... 60
    // 5: 2 w 1 J (round cap), a line (10 100) - (30 100): x 9 ... 31, y 99 ... 101 (the arcs of Qt
    //    are curves: 1e-3)
    // 6: the CTM [2 0 0 1 0 0], 2 w 0 J, a line (10 150) - (30 150): the stroke of the node space,
    //    x 10 ... 30, y 149 ... 151, scaled: x 20 ... 60, y 149 ... 151
    PDFDocument document = Documents::createDocument("4 w 10 20 30 40 re S "
                                                     "2 w 2 J 10 10 m 30 10 l S "
                                                     "0 w 50 50 m 70 50 l S "
                                                     "10 20 30 40 re f "
                                                     "2 w 1 J 10 100 m 30 100 l S "
                                                     "q 2 0 0 1 0 0 cm 2 w 0 J 10 150 m 30 150 l S Q", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(6));

    const PDFContentDisplayContext context = createContext(&document);
    auto box = [&](size_t index) { return PDFContentDisplay::getBoundingBox(*container, paths[index]->getId(), context); };

    QVERIFY(box(0).isValid);
    QVERIFY(!box(0).isUnknown);
    QVERIFY(isClose(box(0).box, QRectF(8, 18, 34, 44), 1e-9));
    QVERIFY(isClose(box(1).box, QRectF(9, 9, 22, 2), 1e-9));
    QVERIFY(isClose(box(2).box, QRectF(50, 50, 20, 0), 1e-9));
    QVERIFY(isClose(box(3).box, QRectF(10, 20, 30, 40), 1e-9));
    QVERIFY(isClose(box(4).box, QRectF(9, 99, 22, 2), 1e-3));
    QVERIFY(isClose(box(5).box, QRectF(20, 149, 40, 2), 1e-9));

    // An override moves the stroked rectangle by 5 to the right and makes its line 6 wide
    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 5, 0);
    override.properties[PDFContentStateProperty::LineWidth] = makeReal(6);
    PDFContentContainerPointer changed = withOverride(container, paths[0]->getId(), override);
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*changed, paths[0]->getId(), context).box, QRectF(12, 17, 36, 46), 1e-9));

    // The wrap [1 0 0 1 0 10] of the group around the sixth path reaches it (getEffectiveOverride: T =
    // R C R^-1 with R = [2 0 0 1 0 0], C = M = [1 0 0 1 0 10], so T = [1 0 0 1 0 10] in the space of the
    // path): x 20 ... 60, y 159 ... 161 - measured from the group and from the path itself
    const PDFContentNodeId groupId = getNodes(*container, PDFContentNodeKind::Group)[0]->getId();
    PDFContentStateOverride groupOverride;
    groupOverride.transform = QTransform(1, 0, 0, 1, 0, 10);
    PDFContentContainerPointer wrapped = withOverride(container, groupId, groupOverride);
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*wrapped, groupId, context).box, QRectF(20, 159, 40, 2), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*wrapped, paths[5]->getId(), context).box, QRectF(20, 159, 40, 2), 1e-9));

    // An unknown node
    QVERIFY(!PDFContentDisplay::getBoundingBox(*container, PDFContentNodeId{ 999 }, context).isValid);
}

void ContentDisplayTest::boundingBoxOfLeaves()
{
    // The image under [100 0 0 50 50 60]: the unit square x 50 ... 150, y 60 ... 110
    PDFDocument imageDocument = Documents::createSoftMaskImageDocument();
    PDFContentContainerPointer imageContainer = build(imageDocument, pageKey(imageDocument));
    const PDFContentNodeId imageId = getNodes(*imageContainer, PDFContentNodeKind::Image)[0]->getId();
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*imageContainer, imageId, createContext(&imageDocument)).box, QRectF(50, 60, 100, 50), 1e-9));

    // /Fm1: /BBox [0 0 40 30] under /Matrix [2 0 0 2 10 20]: x 10 ... 90, y 20 ... 80; the second use
    // is moved by 100; /Fm2: /BBox [0 0 10 10]
    PDFDocument document = Documents::createFormDocument();
    PDFContentContainerPointer page = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> uses = getNodes(*page, PDFContentNodeKind::FormUse);
    const PDFContentDisplayContext context = createContext(&document);
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*page, uses[0]->getId(), context).box, QRectF(10, 20, 80, 60), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*page, uses[1]->getId(), context).box, QRectF(110, 20, 80, 60), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*page, uses[2]->getId(), context).box, QRectF(0, 0, 10, 10), 1e-9));

    // A rotation by 90 degrees of the use: the box of the form is turned exactly (x' = -y, y' = x):
    // [10 90] x [20 80] -> x -80 ... -20, y 10 ... 90 (not the box of the box)
    PDFContentStateOverride rotation;
    rotation.transform = QTransform(0, 1, -1, 0, 0, 0);
    PDFContentContainerPointer rotated = withOverride(page, uses[0]->getId(), rotation);
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*rotated, uses[0]->getId(), context).box, QRectF(-80, 10, 60, 80), 1e-9));

    // Without the document the box of the builder is moved by the override D = CTM^-1 x T x CTM (the
    // CTM is the identity): the same here
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*rotated, uses[0]->getId(), createContext(nullptr)).box, QRectF(-80, 10, 60, 80), 1e-9));

    // A Type 3 text (no outlines: the cells): /T3 10 Tf at (20 30); A is 0.6 x 10 = 6 wide, B 8 wide,
    // both from 0.2 x 10 below the base line to 10 above it: x 20 ... 34, y 28 ... 40
    PDFDocument textDocument = Documents::createFontsDocument("BT /T3 10 Tf 20 30 Td (AB) Tj ET");
    PDFContentContainerPointer textContainer = build(textDocument, pageKey(textDocument));
    const PDFContentNodeId textId = getNodes(*textContainer, PDFContentNodeKind::TextObject)[0]->getId();
    const PDFContentNodeId showId = getNodes(*textContainer, PDFContentNodeKind::TextShow)[0]->getId();
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*textContainer, textId, createContext(&textDocument)).box, QRectF(20, 28, 14, 12), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*textContainer, showId, createContext(&textDocument)).box, QRectF(20, 28, 14, 12), 1e-9));
}

void ContentDisplayTest::boundingBoxOfGroupsAndUnknown()
{
    // The page of createFormDocument: the union of the three uses, x 0 ... 190, y 0 ... 80
    PDFDocument document = Documents::createFormDocument();
    PDFContentContainerPointer page = build(document, pageKey(document));
    const PDFContentDisplayBox rootBox = PDFContentDisplay::getBoundingBox(*page, page->getRoot()->getId(), createContext(&document));
    QVERIFY(rootBox.isValid);
    QVERIFY(!rootBox.isUnknown);
    QVERIFY(isClose(rootBox.box, QRectF(0, 0, 190, 80), 1e-9));

    // An unknown operator: an Opaque node without a box - the extent is unknown, also for the root;
    // the box of the root is the rectangle; a state run has no box
    PDFDocument opaqueDocument = Documents::createDocument("0 0 10 10 re f 1 2 xyz 1 0 0 rg", &Documents::createNoResources);
    PDFContentContainerPointer opaque = build(opaqueDocument, pageKey(opaqueDocument));
    const std::vector<PDFContentNodePointer> opaqueNodes = getNodes(*opaque, PDFContentNodeKind::Opaque);
    QCOMPARE(opaqueNodes.size(), size_t(1));
    const PDFContentDisplayBox opaqueBox = PDFContentDisplay::getBoundingBox(*opaque, opaqueNodes[0]->getId(), createContext(&opaqueDocument));
    QVERIFY(opaqueBox.isValid);
    QVERIFY(opaqueBox.isUnknown);
    QVERIFY(opaqueBox.box.isNull());
    const PDFContentDisplayBox opaqueRoot = PDFContentDisplay::getBoundingBox(*opaque, opaque->getRoot()->getId(), createContext(&opaqueDocument));
    QVERIFY(opaqueRoot.isUnknown);
    QVERIFY(isClose(opaqueRoot.box, QRectF(0, 0, 10, 10), 1e-9));
    const PDFContentDisplayBox stateBox = PDFContentDisplay::getBoundingBox(*opaque, getNodes(*opaque, PDFContentNodeKind::StateRun)[0]->getId(), createContext(&opaqueDocument));
    QVERIFY(stateBox.isValid && !stateBox.isUnknown && stateBox.box.isNull());
}

void ContentDisplayTest::boundingBoxOfShading()
{
    // sh paints the clip region (ISO 32000-2, 8.7.4.2): /Sh1 has no /BBox, /Sh2 has [0 0 50 50]
    //  1: /Sh1 under the clip [0 0 30 40]: x 0 ... 30, y 0 ... 40
    //  2: /Sh1 without a clip: unknown
    //  3: /Sh2 without a clip: its box x 0 ... 50, y 0 ... 50
    //  4: /Sh2 under the clip [20 20 100 10]: the intersection x 20 ... 50, y 20 ... 30
    PDFDocument document = createShadingDocument("q 0 0 30 40 re W n /Sh1 sh Q /Sh1 sh /Sh2 sh q 20 20 100 10 re W n /Sh2 sh Q");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> shadings = getNodes(*container, PDFContentNodeKind::Shading);
    QCOMPARE(shadings.size(), size_t(4));

    const PDFContentDisplayContext context = createContext(&document);
    const PDFContentDisplayBox first = PDFContentDisplay::getBoundingBox(*container, shadings[0]->getId(), context);
    QVERIFY(!first.isUnknown);
    QVERIFY(isClose(first.box, QRectF(0, 0, 30, 40), 1e-9));
    const PDFContentDisplayBox second = PDFContentDisplay::getBoundingBox(*container, shadings[1]->getId(), context);
    QVERIFY(second.isUnknown);
    QVERIFY(second.box.isNull());
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*container, shadings[2]->getId(), context).box, QRectF(0, 0, 50, 50), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*container, shadings[3]->getId(), context).box, QRectF(20, 20, 30, 10), 1e-9));

    // Without the document only the clip is known
    QVERIFY(PDFContentDisplay::getBoundingBox(*container, shadings[2]->getId(), createContext(nullptr)).isUnknown);
}

void ContentDisplayTest::boundingBoxThroughUses()
{
    // The rectangle [5 5 25 15] of /Fm1 through the second use [2 0 0 2 110 20]: x 120 ... 160, y 30 ... 50;
    // the root of the form (its rectangle) too
    PDFDocument document = Documents::createFormDocument();
    PDFContentContainerPointer page = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> uses = getNodes(*page, PDFContentNodeKind::FormUse);
    PDFContentContainerPointer form = build(document, PDFContentContainerKey::createForm(uses[0]->getData().xobject));
    const PDFContentNodeId rectangleId = getNodes(*form, PDFContentNodeKind::Path)[0]->getId();
    const PDFContentDisplayUsePath path = { PDFContentDisplayUse{ page, uses[1]->getId() } };

    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*form, rectangleId, createContext(&document), path).box, QRectF(120, 30, 40, 20), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*form, form->getRoot()->getId(), createContext(&document), path).box, QRectF(120, 30, 40, 20), 1e-9));

    // A path, which does not lead to the form: no box
    QVERIFY(!PDFContentDisplay::getBoundingBox(*form, rectangleId, createContext(&document), { PDFContentDisplayUse{ page, uses[2]->getId() } }).isValid);
}

// ---------------------------------------------------------------- item 3

void ContentDisplayTest::textGlyphs()
{
    // /F1 10 Tf, 50 Tz (Th = 0.5), 3 Ts, Td (20 30). The widths of /F1: A 700, B 650 (Documents).
    //  A: Tm = [1 0 0 1 20 30]; glyph space -> page [Th 0 0 1 0 Ts] x Tm = [0.5 0 0 1 20 33];
    //     the cell x 0 ... 0.7 x 10 = 7, y -2 ... 10
    //  B: Tm moves by tx = (0.7 x 10) x 0.5 = 3.5 (9.4.4): [1 0 0 1 23.5 30], glyph [0.5 0 0 1 23.5 33];
    //     the cell x 0 ... 6.5
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 50 Tz 3 Ts 20 30 Td (AB) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId textId = getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId();
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, textId, createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->node, textId);
    QCOMPARE(text->runs.size(), size_t(1));
    QCOMPARE(text->glyphs.size(), size_t(2));
    QCOMPARE(text->runs[0].firstGlyph, size_t(0));
    QCOMPARE(text->runs[0].glyphCount, size_t(2));
    QVERIFY(!text->runs[0].isType3);
    QVERIFY(!text->runs[0].isVertical);

    const PDFContentDisplayGlyph& a = text->glyphs[0];
    const PDFContentDisplayGlyph& b = text->glyphs[1];
    QVERIFY(isClose(a.textMatrix, QTransform(1, 0, 0, 1, 20, 30)));
    QVERIFY(isClose(a.matrix, QTransform(0.5, 0, 0, 1, 20, 33)));
    QVERIFY(isClose(a.cell, QRectF(0, -2, 7, 12), 1e-9));
    QCOMPARE(a.unicode, QString("A"));
    QCOMPARE(a.code, 65u);
    QCOMPARE(a.codeBytes, QByteArray("A"));
    QCOMPARE(a.itemIndex, size_t(0));
    QVERIFY(isClose(b.textMatrix, QTransform(1, 0, 0, 1, 23.5, 30)));
    QVERIFY(isClose(b.matrix, QTransform(0.5, 0, 0, 1, 23.5, 33)));
    QVERIFY(isClose(b.cell, QRectF(0, -2, 6.5, 12), 1e-9));
    QCOMPARE(b.unicode, QString("B"));

    // One line; the base line from the origin of A to the end of the cell of B (6.5 x 0.5 = 3.25)
    QCOMPARE(text->lines.size(), size_t(1));
    QCOMPARE(text->lines[0].glyphCount, size_t(2));
    QVERIFY(isClose(text->lines[0].baseline.p1(), QPointF(20, 30)));
    QVERIFY(isClose(text->lines[0].baseline.p2(), QPointF(26.75, 30)));
    QVERIFY(isClose(text->lines[0].matrix, QTransform(1, 0, 0, 1, 20, 30)));

    // A negative font size mirrors the glyph space: [Th s 0 0 s 0 Ts] with s = -1; the cell keeps |Tfs|
    PDFDocument negativeDocument = Documents::createFontsDocument("BT /F1 -10 Tf (A) Tj ET");
    PDFContentContainerPointer negative = build(negativeDocument, pageKey(negativeDocument));
    const PDFContentDisplayTextPointer negativeText = PDFContentDisplay::getText(*negative, getNodes(*negative, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&negativeDocument));
    QVERIFY(negativeText && negativeText->glyphs.size() == 1);
    QVERIFY(isClose(negativeText->glyphs[0].matrix, QTransform(-1, 0, 0, -1, 0, 0)));
    QVERIFY(isClose(negativeText->glyphs[0].cell, QRectF(0, -2, 7, 12), 1e-9));

    // A node, which is no text object
    QVERIFY(!PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextShow)[0]->getId(), createContext(&document)));
    QVERIFY(!PDFContentDisplay::getText(*container, PDFContentNodeId{ 999 }, createContext(&document)));
}

void ContentDisplayTest::textLines()
{
    // A B at (20 100); Td (0 -12): C at (20 88) begins a line; Td (5 0): D at (25 88) on the same base
    // line; 2 Ts: E raised (the rise is not a new line); a TJ with a spacer keeps the line
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 20 100 Td (AB) Tj 0 -12 Td (A) Tj 5 0 Td (B) Tj 2 Ts (A) Tj [(B) -500 (A)] TJ ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->glyphs.size(), size_t(7));
    QCOMPARE(text->lines.size(), size_t(2));
    QCOMPARE(text->lines[0].firstGlyph, size_t(0));
    QCOMPARE(text->lines[0].glyphCount, size_t(2));
    QCOMPARE(text->lines[1].firstGlyph, size_t(2));
    QCOMPARE(text->lines[1].glyphCount, size_t(5));
    QVERIFY(isClose(text->glyphs[2].textMatrix, QTransform(1, 0, 0, 1, 20, 88)));
    QVERIFY(isClose(text->glyphs[3].textMatrix, QTransform(1, 0, 0, 1, 25, 88)));

    // The TJ: B, the spacer (item 1), A (item 2)
    QCOMPARE(text->glyphs[6].itemIndex, size_t(2));
    QCOMPARE(text->runs.size(), size_t(5));
}

void ContentDisplayTest::textLineTolerance()
{
    // The tolerance of a base line is 0.1 % of the font size: 0.01 for 10 Tf (zadani_jadro.md 4.7).
    // A at y 100; B at 100.009 (0.009 from A: the same line); C at 100.011 (0.002 from B: the same
    // line); D at 100.022 (0.011 from C: a new line)
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 20 100 Td (A) Tj 0 0.009 Td (B) Tj 0 0.002 Td (A) Tj 0 0.011 Td (B) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->lines.size(), size_t(2));
    QCOMPARE(text->lines[0].glyphCount, size_t(3));
    QCOMPARE(text->lines[1].glyphCount, size_t(1));
}

void ContentDisplayTest::textLineDirection()
{
    // Turned by 90 degrees: Tm = [0 1 -1 0 50 50], the x axis of the text goes up. A at (50 50), B at
    // (50 57) (the advance 7 along the axis), the end of the cell of B at (50 63.5): one line
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 0 1 -1 0 50 50 Tm (AB) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->lines.size(), size_t(1));
    QVERIFY(isClose(text->glyphs[1].textMatrix, QTransform(0, 1, -1, 0, 50, 57)));
    QVERIFY(isClose(text->lines[0].baseline.p1(), QPointF(50, 50)));
    QVERIFY(isClose(text->lines[0].baseline.p2(), QPointF(50, 63.5)));

    // The line space is the text space of A: the cell of B lies at x 7 ... 13.5, y -2 ... 10 of it;
    // the rectangle of the line includes the cells of A and B (x 0 ... 13.5)
    QVERIFY(text->lines[0].rectangle.left() <= 1e-9);
    QVERIFY(text->lines[0].rectangle.right() >= 13.5 - 1e-9);

    // A mirrored text matrix reverses the x axis: B begins a new line, though on the same base line
    PDFDocument mirroredDocument = Documents::createFontsDocument("BT /F1 10 Tf 1 0 0 1 0 50 Tm (A) Tj -1 0 0 1 100 50 Tm (B) Tj ET");
    PDFContentContainerPointer mirrored = build(mirroredDocument, pageKey(mirroredDocument));
    const PDFContentDisplayTextPointer mirroredText = PDFContentDisplay::getText(*mirrored, getNodes(*mirrored, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&mirroredDocument));
    QVERIFY(mirroredText);
    QCOMPARE(mirroredText->lines.size(), size_t(2));

    // A rotation by a small angle (tan = 0.002 > 0.001): a new line
    PDFDocument slantedDocument = Documents::createFontsDocument("BT /F1 10 Tf 1 0 0 1 0 50 Tm (A) Tj 1 0.002 0 1 7 50 Tm (B) Tj ET");
    PDFContentContainerPointer slanted = build(slantedDocument, pageKey(slantedDocument));
    const PDFContentDisplayTextPointer slantedText = PDFContentDisplay::getText(*slanted, getNodes(*slanted, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&slantedDocument));
    QVERIFY(slantedText);
    QCOMPARE(slantedText->lines.size(), size_t(2));
}

void ContentDisplayTest::textVertical()
{
    // /FV: /Identity-V, the vertical displacement w1 of the CID 65 is -0.7 (Documents): A at (50 100),
    // the next A at (50 93) - one column; Td (20 0) from the line start (50 100): A at (70 100), a new
    // column. The base line of a column goes from its first to its last origin.
    PDFDocument document = Documents::createFontsDocument("BT /FV 10 Tf 50 100 Td <00410041> Tj 20 0 Td <0041> Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->glyphs.size(), size_t(3));
    QVERIFY(text->runs[0].isVertical);
    QVERIFY(isClose(text->glyphs[1].textMatrix, QTransform(1, 0, 0, 1, 50, 93)));
    QCOMPARE(text->lines.size(), size_t(2));
    QVERIFY(text->lines[0].isVertical);
    QCOMPARE(text->lines[0].glyphCount, size_t(2));
    QVERIFY(isClose(text->lines[0].baseline.p1(), QPointF(50, 100)));
    QVERIFY(isClose(text->lines[0].baseline.p2(), QPointF(50, 93)));
}

void ContentDisplayTest::textFlags()
{
    // 3 Tr: invisible; 7 Tr: clipping; a Type 3 font is neither (ISO 32000-2, 9.3.6). The text state
    // outlives ET (9.3.1): the last object sets 0 Tr itself.
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 3 Tr (A) Tj ET BT /F1 10 Tf 7 Tr (A) Tj ET BT /T3 10 Tf 3 Tr (A) Tj ET BT /F1 10 Tf 0 Tr (A) Tj 3 Tr (B) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> texts = getNodes(*container, PDFContentNodeKind::TextObject);
    QCOMPARE(texts.size(), size_t(4));
    const PDFContentDisplayContext context = createContext(&document);

    const PDFContentDisplayTextPointer invisible = PDFContentDisplay::getText(*container, texts[0]->getId(), context);
    QVERIFY(invisible->isInvisible);
    QVERIFY(!invisible->isClipping);
    QVERIFY(invisible->lines[0].isInvisible);

    const PDFContentDisplayTextPointer clipping = PDFContentDisplay::getText(*container, texts[1]->getId(), context);
    QVERIFY(!clipping->isInvisible);
    QVERIFY(clipping->isClipping);
    QVERIFY(clipping->lines[0].isClipping);

    const PDFContentDisplayTextPointer type3 = PDFContentDisplay::getText(*container, texts[2]->getId(), context);
    QVERIFY(type3->runs[0].isType3);
    QVERIFY(!type3->isInvisible);
    QVERIFY(type3->glyphs[0].outline == nullptr);
    QVERIFY(isClose(type3->glyphs[0].cell, QRectF(0, -2, 6, 12), 1e-9));

    // A visible A and an invisible B on one line: the line is not invisible
    const PDFContentDisplayTextPointer mixed = PDFContentDisplay::getText(*container, texts[3]->getId(), context);
    QCOMPARE(mixed->lines.size(), size_t(1));
    QVERIFY(!mixed->lines[0].isInvisible);
    QVERIFY(!mixed->isInvisible);
    QVERIFY(mixed->runs[1].isInvisible);
}

void ContentDisplayTest::textOverride()
{
    // The wrap of the text object [1 0 0 1 5 0] moves its glyphs: A at (25 30)
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 20 30 Td (A) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId textId = getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId();
    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 5, 0);
    override.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceRGB", { 0, 0, 1 });
    PDFContentContainerPointer changed = withOverride(container, textId, override);

    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*changed, textId, createContext(&document));
    QVERIFY(text);
    QVERIFY(isClose(text->glyphs[0].textMatrix, QTransform(1, 0, 0, 1, 25, 30)));
    QVERIFY(isRgb(text->runs[0].state.fillColor.color, 0, 0, 255));

    // The realized font of the glyphs is kept by the text
    QVERIFY(text->glyphs[0].outline == nullptr || !text->realizedFonts.empty());
}

void ContentDisplayTest::textThreads()
{
    // Eight threads read one container at once (the canvas and the workers): every result is the
    // one of this thread
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 20 100 Td (AB) Tj 0 -12 Td (AB) Tj ET 0 0 10 10 re S");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId textId = getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId();
    const PDFContentDisplayTextPointer expected = PDFContentDisplay::getText(*container, textId, createContext(&document));
    const QRectF expectedBox = PDFContentDisplay::getBoundingBox(*container, container->getRoot()->getId(), createContext(&document)).box;

    std::atomic<int> failures = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]()
        {
            for (int j = 0; j < 50; ++j)
            {
                const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, textId, createContext(&document));
                const QRectF box = PDFContentDisplay::getBoundingBox(*container, container->getRoot()->getId(), createContext(&document)).box;
                if (!text || text->lines.size() != expected->lines.size() || text->glyphs.size() != expected->glyphs.size() || box != expectedBox ||
                    text->glyphs[3].matrix != expected->glyphs[3].matrix)
                {
                    ++failures;
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    QCOMPARE(failures.load(), 0);
    QCOMPARE(expected->lines.size(), size_t(2));
}

// ---------------------------------------------------------------- item 4

void ContentDisplayTest::imageOfXObject()
{
    // The image of createSoftMaskImageDocument: 2 x 2 DeviceRGB, the samples FF0000 00FF00 / 0000FF
    // FFFF00 - the first row is the top of the image (ISO 32000-2, 8.9.3): red, green / blue, yellow;
    // drawn under [100 0 0 50 50 60]
    PDFDocument document = Documents::createSoftMaskImageDocument();
    const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(image.image.size(), QSize(2, 2));
    QCOMPARE(image.sourceSize, QSize(2, 2));
    QVERIFY(!image.isImageMask);
    QCOMPARE(getPixel(image.image, 0, 0), QByteArray("255 0 0 255"));
    QCOMPARE(getPixel(image.image, 1, 0), QByteArray("0 255 0 255"));
    QCOMPARE(getPixel(image.image, 0, 1), QByteArray("0 0 255 255"));
    QCOMPARE(getPixel(image.image, 1, 1), QByteArray("255 255 0 255"));
    QVERIFY(isClose(image.matrix, QTransform(100, 0, 0, 50, 50, 60)));
}

void ContentDisplayTest::imageDecodeAndMasks()
{
    const Documents::DictionaryEntries rgb = { { "Width", PDFObject::createInteger(2) }, { "Height", PDFObject::createInteger(1) },
                                               { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) } };

    // /Decode [1 0 1 0 1 0] inverts the samples (8.9.5.2): FF0000 -> cyan, 000000 -> white
    {
        Documents::DictionaryEntries entries = rgb;
        entries.emplace_back("Decode", Documents::createIntegerArrayObject({ 1, 0, 1, 0, 1, 0 }));
        PDFDocument document = createImageDocument("q 10 0 0 10 0 0 cm /Im0 Do Q", { { entries, QByteArray("\xFF\x00\x00\x00\x00\x00", 6) } });
        const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
        QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
        QCOMPARE(getPixel(image.image, 0, 0), QByteArray("0 255 255 255"));
        QCOMPARE(getPixel(image.image, 1, 0), QByteArray("255 255 255 255"));
    }

    // /SMask 2 x 1 DeviceGray FF 00: the first pixel opaque, the second one transparent (11.6.5.3)
    {
        PDFDocument document = Documents::createDocument("q 10 0 0 10 0 0 cm /Im0 Do Q", [&rgb](PDFDocumentBuilder* builder)
        {
            PDFObject mask = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                   { "Width", PDFObject::createInteger(2) }, { "Height", PDFObject::createInteger(1) },
                                                                   { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) } },
                                                        QByteArray("\xFF\x00", 2));
            Documents::DictionaryEntries entries = { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") } };
            for (const auto& entry : rgb)
            {
                entries.push_back(entry);
            }
            entries.emplace_back("SMask", mask);
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im0", Documents::addStreamObject(builder, std::move(entries), QByteArray("\xFF\x00\x00\xFF\x00\x00", 6)) } }));
            return resources;
        });
        const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
        QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
        QCOMPARE(getPixel(image.image, 0, 0), QByteArray("255 0 0 255"));
        QCOMPARE(image.image.pixelColor(1, 0).alpha(), 0);
    }

    // /Mask [0 0 255 255 0 0]: the color key masks the green pixel (8.9.6.4)
    {
        Documents::DictionaryEntries entries = rgb;
        entries.emplace_back("Mask", Documents::createIntegerArrayObject({ 0, 0, 255, 255, 0, 0 }));
        PDFDocument document = createImageDocument("q 10 0 0 10 0 0 cm /Im0 Do Q", { { entries, QByteArray("\xFF\x00\x00\x00\xFF\x00", 6) } });
        const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
        QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
        QCOMPARE(getPixel(image.image, 0, 0), QByteArray("255 0 0 255"));
        QCOMPARE(image.image.pixelColor(1, 0).alpha(), 0);
    }

    // /ImageMask: the samples 0 1 (the byte 0x40); with /Decode [0 1] the sample 0 paints (8.9.6.2) -
    // in the fill color of the node, here blue from "0 0 1 rg"
    {
        PDFDocument document = createImageDocument("0 0 1 rg q 10 0 0 10 0 0 cm /Im0 Do Q", { { { { "Width", PDFObject::createInteger(2) }, { "Height", PDFObject::createInteger(1) },
                                                                                                 { "ImageMask", PDFObject::createBool(true) } }, QByteArray("\x40", 1) } });
        const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
        QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
        QVERIFY(image.isImageMask);
        QCOMPARE(getPixel(image.image, 0, 0), QByteArray("0 0 255 255"));
        QCOMPARE(image.image.pixelColor(1, 0).alpha(), 0);

        // The fill color of an override paints the mask: red
        PDFContentContainerPointer container = build(document, pageKey(document));
        const PDFContentNodeId imageId = getNodes(*container, PDFContentNodeKind::Image)[0]->getId();
        PDFContentStateOverride override;
        override.properties[PDFContentStateProperty::FillColor] = makeColor("DeviceRGB", { 1, 0, 0 });
        const PDFContentDisplayImage recolored = PDFContentDisplay::decodeImage(&document, withOverride(container, imageId, override), imageId, PDFContentDisplayImageOptions(), nullptr);
        QCOMPARE(recolored.status, PDFContentDisplayImage::Status::Decoded);
        QCOMPARE(getPixel(recolored.image, 0, 0), QByteArray("255 0 0 255"));
    }
}

void ContentDisplayTest::imageInlineAndForm()
{
    // An inline image 2 x 1 (abbreviated keys, 8.9.7): red, green
    QByteArray content = "q 10 0 0 10 20 30 cm BI /W 2 /H 1 /CS /RGB /BPC 8 ID ";
    content += QByteArray("\xFF\x00\x00\x00\xFF\x00", 6);
    content += " EI Q";
    PDFDocument document = Documents::createDocument(content, &Documents::createNoResources);
    const PDFContentDisplayImage image = decodeFirstImage(document, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(image.image.size(), QSize(2, 1));
    QCOMPARE(getPixel(image.image, 0, 0), QByteArray("255 0 0 255"));
    QCOMPARE(getPixel(image.image, 1, 0), QByteArray("0 255 0 255"));
    QVERIFY(isClose(image.matrix, QTransform(10, 0, 0, 10, 20, 30)));

    // An image of a form with resources of its own: the names of the form, not of the page (the
    // page has no /ImF)
    PDFDocument formDocument = Documents::createDocument("/Fm Do", [](PDFDocumentBuilder* builder)
    {
        PDFObject image = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                { "Width", PDFObject::createInteger(1) }, { "Height", PDFObject::createInteger(1) },
                                                                { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) } },
                                                     QByteArray("\x00", 1));
        PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                               { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "ImF", image } }) } }) } },
                                                    QByteArray("q 10 0 0 10 0 0 cm /ImF Do Q"));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm", form } }));
        return resources;
    });
    PDFContentContainerPointer page = build(formDocument, pageKey(formDocument));
    PDFContentContainerPointer form = build(formDocument, PDFContentContainerKey::createForm(getNodes(*page, PDFContentNodeKind::FormUse)[0]->getData().xobject));
    QVERIFY(form);
    const PDFContentDisplayImage formImage = PDFContentDisplay::decodeImage(&formDocument, form, getNodes(*form, PDFContentNodeKind::Image)[0]->getId(), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(formImage.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(getPixel(formImage.image, 0, 0), QByteArray("0 0 0 255"));
}

void ContentDisplayTest::imageLimits()
{
    // 4 x 2 pixels: at most 2 pixels gives 2 x 1 (the scale sqrt(2 / 8) = 0.5), at most 3 pixels too
    // (floor(4 x 0.61) = 2, floor(2 x 0.61) = 1); a thin image 100 x 1 with at most 10 pixels: 10 x 1
    const QByteArray samples(4 * 2 * 3, char(0x40));
    PDFDocument document = createImageDocument("q 10 0 0 10 0 0 cm /Im0 Do Q /Im1 Do",
                                               { { { { "Width", PDFObject::createInteger(4) }, { "Height", PDFObject::createInteger(2) },
                                                     { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) } }, samples },
                                                 { { { "Width", PDFObject::createInteger(100) }, { "Height", PDFObject::createInteger(1) },
                                                     { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) } }, QByteArray(100, char(0)) } });
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> images = getNodes(*container, PDFContentNodeKind::Image);
    QCOMPARE(images.size(), size_t(2));

    PDFContentDisplayImageOptions options;
    options.maximumPixels = 2;
    PDFContentDisplayImage image = PDFContentDisplay::decodeImage(&document, container, images[0]->getId(), options, nullptr);
    QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(image.image.size(), QSize(2, 1));
    QCOMPARE(image.sourceSize, QSize(4, 2));
    options.maximumPixels = 3;
    QCOMPARE(PDFContentDisplay::decodeImage(&document, container, images[0]->getId(), options, nullptr).image.size(), QSize(2, 1));
    options.maximumPixels = 8;
    QCOMPARE(PDFContentDisplay::decodeImage(&document, container, images[0]->getId(), options, nullptr).image.size(), QSize(4, 2));
    options.maximumPixels = 10;
    QCOMPARE(PDFContentDisplay::decodeImage(&document, container, images[1]->getId(), options, nullptr).image.size(), QSize(10, 1));

    // /Width x /Height above 2^31 (65536 x 65536 = 2^32), and a soft mask of that size: refused
    // before any decoding (the data are 3 bytes)
    PDFDocument largeDocument = Documents::createDocument("/Im0 Do /Im1 Do", [](PDFDocumentBuilder* builder)
    {
        PDFObject largeMask = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                    { "Width", PDFObject::createInteger(65536) }, { "Height", PDFObject::createInteger(65536) },
                                                                    { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) } },
                                                         QByteArray("\x00", 1));
        PDFObject large = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                { "Width", PDFObject::createInteger(65536) }, { "Height", PDFObject::createInteger(65536) },
                                                                { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) } },
                                                     QByteArray("\x00\x00\x00", 3));
        PDFObject masked = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                 { "Width", PDFObject::createInteger(1) }, { "Height", PDFObject::createInteger(1) },
                                                                 { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) },
                                                                 { "SMask", largeMask } },
                                                      QByteArray("\x00\x00\x00", 3));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im0", large }, { "Im1", masked } }));
        return resources;
    });
    PDFContentContainerPointer largeContainer = build(largeDocument, pageKey(largeDocument));
    const std::vector<PDFContentNodePointer> largeImages = getNodes(*largeContainer, PDFContentNodeKind::Image);
    QCOMPARE(largeImages.size(), size_t(2));
    const PDFContentDisplayImage large = PDFContentDisplay::decodeImage(&largeDocument, largeContainer, largeImages[0]->getId(), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(large.status, PDFContentDisplayImage::Status::TooLarge);
    QVERIFY(large.image.isNull());
    QCOMPARE(large.sourceSize, QSize(65536, 65536));
    QCOMPARE(PDFContentDisplay::decodeImage(&largeDocument, largeContainer, largeImages[1]->getId(), PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayImage::Status::TooLarge);
}

void ContentDisplayTest::imageRefused()
{
    PDFDocument document = Documents::createSoftMaskImageDocument();
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodeId imageId = getNodes(*container, PDFContentNodeKind::Image)[0]->getId();

    // No image node, no document, no container
    QCOMPARE(PDFContentDisplay::decodeImage(&document, container, container->getRoot()->getId(), PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayImage::Status::NotImage);
    QCOMPARE(PDFContentDisplay::decodeImage(nullptr, container, imageId, PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayImage::Status::NotImage);
    QCOMPARE(PDFContentDisplay::decodeImage(&document, nullptr, imageId, PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayImage::Status::NotImage);

    // Canceled
    CanceledControl canceled;
    const PDFContentDisplayImage canceledImage = PDFContentDisplay::decodeImage(&document, container, imageId, PDFContentDisplayImageOptions(), &canceled);
    QCOMPARE(canceledImage.status, PDFContentDisplayImage::Status::Canceled);
    QVERIFY(canceledImage.image.isNull());

    // Data, which /FlateDecode cannot read: failed with a reason
    PDFDocument brokenDocument = createImageDocument("/Im0 Do", { { { { "Width", PDFObject::createInteger(2) }, { "Height", PDFObject::createInteger(2) },
                                                                      { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) },
                                                                      { "Filter", PDFObject::createName("FlateDecode") } }, QByteArray("not a flate stream") } });
    const PDFContentDisplayImage broken = decodeFirstImage(brokenDocument, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(broken.status, PDFContentDisplayImage::Status::Failed);
    QVERIFY(!broken.errorText.isEmpty());
    QVERIFY(broken.image.isNull());
}

// ---------------------------------------------------------------- item 5

void ContentDisplayTest::clipChain()
{
    // q [clip 0 0 100 100 W n] [10 10 20 20 re W* f: paints and clips] [rg] [5 5 5 5 re f] Q [the line]
    //  - the red rectangle is drawn under both clips (nonzero, then even-odd);
    //  - the path, which paints and clips, under the first one only (W applies after its painting);
    //  - the line after Q under none (Q restores the clip)
    PDFDocument document = Documents::createDocument("q 0 0 100 100 re W n 10 10 20 20 re W* f 1 0 0 rg 5 5 5 5 re f Q 50 50 m 60 60 l S", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(4));

    const PDFContentDisplayClipChain red = PDFContentDisplay::getClipChain(*container, paths[2]->getId());
    QVERIFY(red.isValid);
    QVERIFY(!red.isUnknown);
    QCOMPARE(red.clips.size(), size_t(2));
    QCOMPARE(red.clips[0].node, paths[0]->getId());
    QCOMPARE(red.clips[0].fillRule, Qt::WindingFill);
    QVERIFY(isClose(red.clips[0].path.getBoundingBox(), QRectF(0, 0, 100, 100), 1e-9));
    QCOMPARE(red.clips[1].node, paths[1]->getId());
    QCOMPARE(red.clips[1].fillRule, Qt::OddEvenFill);
    QVERIFY(!red.clips[1].isText);

    const PDFContentDisplayClipChain paintAndClip = PDFContentDisplay::getClipChain(*container, paths[1]->getId());
    QCOMPARE(paintAndClip.clips.size(), size_t(1));
    QCOMPARE(paintAndClip.clips[0].node, paths[0]->getId());

    QVERIFY(PDFContentDisplay::getClipChain(*container, paths[3]->getId()).clips.empty());
    QVERIFY(!PDFContentDisplay::getClipChain(*container, PDFContentNodeId{ 999 }).isValid);

    // An override of the clip moves its path into the default space: [1 0 0 1 5 0]
    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 5, 0);
    PDFContentContainerPointer changed = withOverride(container, paths[0]->getId(), override);
    QVERIFY(isClose(PDFContentDisplay::getClipChain(*changed, paths[2]->getId()).clips[0].path.getBoundingBox(), QRectF(5, 0, 100, 100), 1e-9));

    // An unknown operator before the node may have changed the clip
    PDFDocument opaqueDocument = Documents::createDocument("1 2 xyz 0 0 10 10 re f", &Documents::createNoResources);
    PDFContentContainerPointer opaque = build(opaqueDocument, pageKey(opaqueDocument));
    QVERIFY(PDFContentDisplay::getClipChain(*opaque, getNodes(*opaque, PDFContentNodeKind::Path)[0]->getId()).isUnknown);
}

void ContentDisplayTest::clipChainMarkedContentAndText()
{
    // A marked content does not restore the state: the clip inside of it reaches the line after EMC;
    // a clipping text object clips the rectangle after its ET
    PDFDocument document = Documents::createFontsDocument("/P BMC 0 0 50 50 re W n EMC 1 1 m 2 2 l S BT /F1 10 Tf 7 Tr (A) Tj ET 0 0 10 10 re f");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(3));
    const PDFContentNodeId textId = getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId();

    const PDFContentDisplayClipChain line = PDFContentDisplay::getClipChain(*container, paths[1]->getId());
    QCOMPARE(line.clips.size(), size_t(1));
    QCOMPARE(line.clips[0].node, paths[0]->getId());

    const PDFContentDisplayClipChain rectangle = PDFContentDisplay::getClipChain(*container, paths[2]->getId());
    QCOMPARE(rectangle.clips.size(), size_t(2));
    QCOMPARE(rectangle.clips[1].node, textId);
    QVERIFY(rectangle.clips[1].isText);
    QVERIFY(rectangle.clips[1].path.isEmpty());

    // Inside of a text object a clip by text of an earlier show is not in effect yet (it is at ET)
    PDFDocument textDocument = Documents::createFontsDocument("BT /F1 10 Tf 7 Tr (A) Tj 0 Tr (B) Tj ET");
    PDFContentContainerPointer textContainer = build(textDocument, pageKey(textDocument));
    QVERIFY(PDFContentDisplay::getClipChain(*textContainer, getNodes(*textContainer, PDFContentNodeKind::TextShow)[1]->getId()).clips.empty());
}

void ContentDisplayTest::clippedNodes()
{
    PDFDocument document = Documents::createDocument("q 0 0 100 100 re W n 10 10 20 20 re W* f 1 0 0 rg 5 5 5 5 re f Q 50 50 m 60 60 l S", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    const PDFContentNodePointer group = getNodes(*container, PDFContentNodeKind::Group)[0];

    // The siblings after the clip up to Q: the path, the state run, the red rectangle
    const std::vector<PDFContentNodeId> clipped = PDFContentDisplay::getClippedNodes(*container, paths[0]->getId());
    std::vector<PDFContentNodeId> expected;
    for (size_t i = 1; i < group->getChildren().size(); ++i)
    {
        expected.push_back(group->getChildren()[i]->getId());
    }
    QCOMPARE(expected.size(), size_t(3));
    QCOMPARE(clipped, expected);

    // A node without a clip effect, an unknown node
    QVERIFY(PDFContentDisplay::getClippedNodes(*container, paths[2]->getId()).empty());
    QVERIFY(PDFContentDisplay::getClippedNodes(*container, PDFContentNodeId{ 999 }).empty());

    // Through the end of a marked content up to the end of the content; a clipping text show clips
    // after the end of its text object
    PDFDocument markedDocument = Documents::createFontsDocument("/P BMC 0 0 50 50 re W n EMC 1 1 m 2 2 l S BT /F1 10 Tf 7 Tr (A) Tj ET 0 0 10 10 re f");
    PDFContentContainerPointer marked = build(markedDocument, pageKey(markedDocument));
    const std::vector<PDFContentNodePointer> markedPaths = getNodes(*marked, PDFContentNodeKind::Path);
    const PDFContentNodeId textId = getNodes(*marked, PDFContentNodeKind::TextObject)[0]->getId();
    const std::vector<PDFContentNodeId> markedClipped = PDFContentDisplay::getClippedNodes(*marked, markedPaths[0]->getId());
    QCOMPARE(markedClipped, (std::vector<PDFContentNodeId>{ markedPaths[1]->getId(), textId, markedPaths[2]->getId() }));
    const std::vector<PDFContentNodeId> textClipped = PDFContentDisplay::getClippedNodes(*marked, getNodes(*marked, PDFContentNodeKind::TextShow)[0]->getId());
    QCOMPARE(textClipped, (std::vector<PDFContentNodeId>{ markedPaths[2]->getId() }));
}

// ---------------------------------------------------------------- item 6

void ContentDisplayTest::counts()
{
    // Two paths: "re" is m, three l, h (5 segments, PDFContentPath::addRectangle), "m l c" 3 segments;
    // two glyphs; one image
    PDFDocument document = Documents::createFontsDocument("0 0 10 10 re f 0 0 m 10 10 l 20 0 30 0 40 10 c S BT /F1 10 Tf (AB) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayCounts counts = PDFContentDisplay::getCounts(*container);
    QCOMPARE(counts.pathCount, size_t(2));
    QCOMPARE(counts.pathSegmentCount, size_t(8));
    QCOMPARE(counts.glyphCount, size_t(2));
    QCOMPARE(counts.imageCount, size_t(0));
    QCOMPARE(counts.nodeCount, container->getNodeCount());

    // The same version again; another version has its own counts
    QCOMPARE(PDFContentDisplay::getCounts(*container).pathSegmentCount, size_t(8));
    PDFContentContainerEditor editor(container);
    QVERIFY(editor.removeNode(getNodes(*container, PDFContentNodeKind::Path)[0]->getId()));
    PDFContentContainerPointer changed = editor.build();
    QVERIFY(changed->getVersion() != container->getVersion());
    QCOMPARE(PDFContentDisplay::getCounts(*changed).pathSegmentCount, size_t(3));
    QCOMPARE(PDFContentDisplay::getCounts(*changed).pathCount, size_t(1));
    QCOMPARE(PDFContentDisplay::getCounts(*container).pathSegmentCount, size_t(8));

    PDFDocument imageDocument = Documents::createSoftMaskImageDocument();
    PDFContentContainerPointer imageContainer = build(imageDocument, pageKey(imageDocument));
    QCOMPARE(PDFContentDisplay::getCounts(*imageContainer).imageCount, size_t(1));
}

// ---------------------------------------------------------------- item 7

void ContentDisplayTest::shadingGradients()
{
    PDFDocument document = createShadingDocument("/Sh2 sh /Sh3 sh /Sh4 sh /Pattern cs /PS scn 0 0 10 10 re f");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> shadings = getNodes(*container, PDFContentNodeKind::Shading);
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(shadings.size(), size_t(3));
    QCOMPARE(paths.size(), size_t(1));

    // /Sh2: axial (0 0) - (100 0), no extension; the function C0 + t (C1 - C0) (ISO 32000-2, 7.10.3,
    // N = 1) sampled at 256 points: the stop 51 is at t = 51 / 255 = 0.2, the color 0.8 0 0.2
    const PDFContentDisplayShading axial = PDFContentDisplay::getShading(&document, container, shadings[0]->getId(), false, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(axial.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(axial.kind, PDFContentDisplayShading::Kind::Axial);
    QVERIFY(isClose(axial.start, QPointF(0, 0)));
    QVERIFY(isClose(axial.end, QPointF(100, 0)));
    QVERIFY(!axial.extendStart && !axial.extendEnd);
    QVERIFY(axial.matrix.isIdentity());
    QCOMPARE(axial.stops.size(), size_t(PDFContentDisplay::MAXIMUM_GRADIENT_STOPS));
    QCOMPARE(axial.stops[0].position, 0.0);
    QCOMPARE(axial.stops[255].position, 1.0);
    QVERIFY(isRgb(axial.stops[0].color, 255, 0, 0));
    QVERIFY(isRgb(axial.stops[255].color, 0, 0, 255));
    QVERIFY(std::abs(axial.stops[51].position - 0.2) <= 1e-12);
    QVERIFY(isRgb(axial.stops[51].color, 204, 0, 51));
    QVERIFY(!axial.backgroundColor.isValid());

    // /Sh3: radial, the circles (10 10) r 0 and r 50, extended at the start; DeviceGray 0 ... 1
    const PDFContentDisplayShading radial = PDFContentDisplay::getShading(&document, container, shadings[1]->getId(), false, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(radial.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(radial.kind, PDFContentDisplayShading::Kind::Radial);
    QVERIFY(isClose(radial.start, QPointF(10, 10)));
    QVERIFY(isClose(radial.end, QPointF(10, 10)));
    QCOMPARE(radial.startRadius, 0.0);
    QCOMPARE(radial.endRadius, 50.0);
    QVERIFY(radial.extendStart && !radial.extendEnd);
    QVERIFY(isRgb(radial.stops[0].color, 0, 0, 0));
    QVERIFY(isRgb(radial.stops[255].color, 255, 255, 255));

    // /Sh4: function based - not done (declared)
    const PDFContentDisplayShading functionBased = PDFContentDisplay::getShading(&document, container, shadings[2]->getId(), false, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(functionBased.status, PDFContentDisplayShading::Status::NotImplemented);
    QCOMPARE(functionBased.kind, PDFContentDisplayShading::Kind::Picture);

    // The shading pattern /PS fills the rectangle: the pattern space under /Matrix [1 0 0 1 5 5]
    const PDFContentDisplayShading pattern = PDFContentDisplay::getShading(&document, container, paths[0]->getId(), false, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(pattern.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(pattern.kind, PDFContentDisplayShading::Kind::Axial);
    QVERIFY(isClose(pattern.matrix, QTransform(1, 0, 0, 1, 5, 5)));

    // The stroke is no pattern; no document; canceled
    QCOMPARE(PDFContentDisplay::getShading(&document, container, paths[0]->getId(), true, PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayShading::Status::NotShading);
    QCOMPARE(PDFContentDisplay::getShading(nullptr, container, shadings[0]->getId(), false, PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayShading::Status::NotShading);
    CanceledControl canceled;
    QCOMPARE(PDFContentDisplay::getShading(&document, container, shadings[0]->getId(), false, PDFContentDisplayImageOptions(), &canceled).status, PDFContentDisplayShading::Status::Canceled);

    // A shading node is no tiling pattern
    QCOMPARE(PDFContentDisplay::getTilingCell(&document, container, paths[0]->getId(), false, PDFContentDisplayImageOptions(), nullptr).status, PDFContentDisplayTilingCell::Status::NotTilingPattern);
}

void ContentDisplayTest::tilingCell()
{
    // createTwoTilingPatternsDocument: the first path fills with /PU (uncolored, the cell "0 0 6 6 re
    // f", /Matrix [1 0 0 1 3 3]) in 0 0.5 0; the second one fills with /PC (colored: red [0 0 5 5],
    // blue [5 5 5 5]); the third one strokes with /PU in red. /BBox [0 0 10 10], the steps 10.
    PDFDocument document = Documents::createTwoTilingPatternsDocument();
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);

    // 100 pixels: 10 x 10, one pixel per unit; the pattern y goes up, the rows of the picture down:
    // red covers the columns 0 ... 4 of the rows 5 ... 9, blue the columns 5 ... 9 of the rows 0 ... 4
    PDFContentDisplayImageOptions options;
    options.maximumPixels = 100;
    const PDFContentDisplayTilingCell colored = PDFContentDisplay::getTilingCell(&document, container, paths[1]->getId(), false, options, nullptr);
    QCOMPARE(colored.status, PDFContentDisplayTilingCell::Status::Done);
    QCOMPARE(colored.picture.size(), QSize(10, 10));
    QVERIFY(isClose(colored.cellRectangle, QRectF(0, 0, 10, 10), 1e-9));
    QCOMPARE(colored.xStep, 10.0);
    QCOMPARE(colored.yStep, 10.0);
    QVERIFY(colored.matrix.isIdentity());
    QCOMPARE(getPixel(colored.picture, 2, 7), QByteArray("255 0 0 255"));
    QCOMPARE(getPixel(colored.picture, 7, 2), QByteArray("0 0 255 255"));
    QCOMPARE(colored.picture.pixelColor(7, 7).alpha(), 0);
    QCOMPARE(colored.picture.pixelColor(2, 2).alpha(), 0);

    // 400 pixels: 20 x 20; the point (2.5 2.5) of the pattern is the pixel (5 15)
    options.maximumPixels = 400;
    const PDFContentDisplayTilingCell larger = PDFContentDisplay::getTilingCell(&document, container, paths[1]->getId(), false, options, nullptr);
    QCOMPARE(larger.picture.size(), QSize(20, 20));
    QCOMPARE(getPixel(larger.picture, 5, 15), QByteArray("255 0 0 255"));

    // The uncolored /PU in the color of the use: 0 0.5 0 for the fill, red for the stroke; the square
    // [0 0 6 6] covers the columns 0 ... 5 of the rows 4 ... 9
    options.maximumPixels = 100;
    const PDFContentDisplayTilingCell uncolored = PDFContentDisplay::getTilingCell(&document, container, paths[0]->getId(), false, options, nullptr);
    QCOMPARE(uncolored.status, PDFContentDisplayTilingCell::Status::Done);
    QVERIFY(isClose(uncolored.matrix, QTransform(1, 0, 0, 1, 3, 3)));
    QVERIFY(isRgb(uncolored.picture.pixelColor(2, 7), 0, 128, 0));
    QCOMPARE(uncolored.picture.pixelColor(8, 2).alpha(), 0);
    const PDFContentDisplayTilingCell stroke = PDFContentDisplay::getTilingCell(&document, container, paths[2]->getId(), true, options, nullptr);
    QCOMPARE(stroke.status, PDFContentDisplayTilingCell::Status::Done);
    QCOMPARE(getPixel(stroke.picture, 2, 7), QByteArray("255 0 0 255"));

    // A tiling pattern is no shading; canceled
    QCOMPARE(PDFContentDisplay::getShading(&document, container, paths[1]->getId(), false, options, nullptr).status, PDFContentDisplayShading::Status::NotShading);
    CanceledControl canceled;
    const PDFContentDisplayTilingCell canceledCell = PDFContentDisplay::getTilingCell(&document, container, paths[1]->getId(), false, options, &canceled);
    QCOMPARE(canceledCell.status, PDFContentDisplayTilingCell::Status::Canceled);
    QVERIFY(canceledCell.picture.isNull());
}

QTEST_MAIN(ContentDisplayTest)

#include "tst_contentdisplaytest.moc"
