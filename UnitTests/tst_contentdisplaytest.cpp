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
#include "pdfcontentcommands.h"
#include "pdfcontentdisplay.h"
#include "pdfcontenteditsession.h"
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

#include <algorithm>
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

    // ---- stage E1, fix package FX1 (pe1_fixes.md) ----
    void meshShadings();
    void meshLimitAndCancel();
    void countsOfComplexPagePolicy();
    void imageCodestreamHeaders();
    void imageResourcesInheritedFromCaller();
    void patternOfMovedObject();
    void boundingBoxOfMiterAndUnpaintedPath();
    void boundingBoxOfLongStrokedPath();
    void clippedNodesOfTextInMarkedContent();
    void textVerticalCell();

private:
    static PDFContentContainerKey pageKey(const PDFDocument& document);
    static PDFContentContainerPointer build(const PDFDocument& document, const PDFContentContainerKey& key);

    /// The same with the resources of the content, which draws a form without /Resources of its
    /// own (PDFContentBuildContext::callerResources)
    static PDFContentContainerPointer build(const PDFDocument& document, const PDFContentContainerKey& key, const PDFObject& callerResources);

    /// The document of the mesh shadings (createMeshDocument in the source file): /S1 function based,
    /// /S4 free-form, /S5 lattice-form, /S6 Coons, /S7 tensor-product, and the large /L4, /L4E,
    /// /L5, /L6 for the limit
    static PDFDocument createMeshDocument(const QByteArray& content);

    /// A baseline JPEG of one 8 x 8 gray block of the sample \p sample (ISO/IEC 10918-1 Annex B;
    /// the tables of tst_imageoptimizertest.cpp: the quantization 8, so the sample is coded as its
    /// DC coefficient sample - 128); \p withEndMarker: EOI at the end; \p width, \p height: the size
    /// written into SOF (one block is coded whatever it says)
    static QByteArray createGrayJpeg(int sample, int width, int height, bool withEndMarker);

    /// A JPEG 2000 codestream of SOC and SIZ only (ISO/IEC 15444-1, A.5.1): one component of 8 bits
    static QByteArray createJpeg2000Codestream(quint32 width, quint32 height);

    /// The color of a pixel within \p tolerance of the expected one, opaque
    static bool isPixelNear(const QImage& image, int x, int y, int red, int green, int blue, int tolerance);

    /// The bytes of the values (each one modulo 256)
    static QByteArray bytes(std::initializer_list<int> values);

    /// An operation control, which is canceled from its \p cancelAt-th question on; it counts them
    class CountingControl : public PDFOperationControl
    {
    public:
        explicit CountingControl(int cancelAt) : m_cancelAt(cancelAt) { }
        virtual bool isOperationCanceled() const override { return ++m_count >= m_cancelAt; }
        int getCount() const { return m_count; }

    private:
        int m_cancelAt = 0;
        mutable std::atomic<int> m_count = 0;
    };

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
    return build(document, key, PDFObject());
}

PDFContentContainerPointer ContentDisplayTest::build(const PDFDocument& document, const PDFContentContainerKey& key, const PDFObject& callerResources)
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
    context.callerResources = callerResources;

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

    // The line space is the text space of A. With the Type 3 font /T3 (no outlines: the boxes are
    // the cells, review R5-10) the rectangle is exact: A (w0 0.6) has the cell x 0 ... 6, B (w0 0.8)
    // begins at 6 and ends at 14; y -2 ... 10 (-0.2 |Tfs| ... |Tfs|). The base line goes from (50 50)
    // to the end of B along the turned x axis: (50 64); the box on the page is x 40 ... 52, y 50 ... 64.
    PDFDocument type3Document = Documents::createFontsDocument("BT /T3 10 Tf 0 1 -1 0 50 50 Tm (AB) Tj ET");
    PDFContentContainerPointer type3 = build(type3Document, pageKey(type3Document));
    const PDFContentDisplayTextPointer type3Text = PDFContentDisplay::getText(*type3, getNodes(*type3, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&type3Document));
    QVERIFY(type3Text);
    QCOMPARE(type3Text->lines.size(), size_t(1));
    QVERIFY(isClose(type3Text->lines[0].rectangle, QRectF(0, -2, 14, 12), 1e-9));
    QVERIFY(isClose(type3Text->lines[0].baseline.p2(), QPointF(50, 64)));
    QVERIFY(isClose(type3Text->lines[0].boundingBox, QRectF(40, 50, 12, 14), 1e-9));

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

    // The text keeps exactly the realized fonts of its shows (review R5-10: the outlines of its
    // glyphs point into them), each once
    std::vector<PDFRealizedFontPointer> expectedFonts;
    for (const PDFContentNodePointer& show : getNodes(*changed, PDFContentNodeKind::TextShow))
    {
        const PDFRealizedFontPointer& font = show->getData().textState.realizedFont;
        if (font && std::find(expectedFonts.cbegin(), expectedFonts.cend(), font) == expectedFonts.cend())
        {
            expectedFonts.push_back(font);
        }
    }
    QVERIFY(text->realizedFonts == expectedFonts);

    // The glyph box moved by the wrap, exact for the Type 3 font /T3 (a cell, no outline): A at
    // (20 30) has the cell x 0 ... 6, y -2 ... 10, moved by [1 0 0 1 5 0]: x 25 ... 31, y 28 ... 40
    PDFDocument type3Document = Documents::createFontsDocument("BT /T3 10 Tf 20 30 Td (A) Tj ET");
    PDFContentContainerPointer type3 = build(type3Document, pageKey(type3Document));
    const PDFContentNodeId type3Id = getNodes(*type3, PDFContentNodeKind::TextObject)[0]->getId();
    PDFContentContainerPointer type3Changed = withOverride(type3, type3Id, override);
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*type3, type3Id, createContext(&type3Document)).box, QRectF(20, 28, 6, 12), 1e-9));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*type3Changed, type3Id, createContext(&type3Document)).box, QRectF(25, 28, 6, 12), 1e-9));
    const PDFContentDisplayTextPointer type3Text = PDFContentDisplay::getText(*type3Changed, type3Id, createContext(&type3Document));
    QVERIFY(type3Text && isClose(type3Text->boundingBox, QRectF(25, 28, 6, 12), 1e-9));

    // A show inside of a marked content of the text object counts to its box: B (w0 0.8) behind A
    // at x 26 ... 34
    PDFDocument markedDocument = Documents::createFontsDocument("BT /T3 10 Tf 20 30 Td (A) Tj /Span BMC (B) Tj EMC ET");
    PDFContentContainerPointer marked = build(markedDocument, pageKey(markedDocument));
    QVERIFY(isClose(PDFContentDisplay::getBoundingBox(*marked, getNodes(*marked, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&markedDocument)).box, QRectF(20, 28, 14, 12), 1e-9));
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

    // /Sh4: function based - a picture since the fix package FX1 (the expected colors of every
    // type of a picture: meshShadings); without /BBox and a clip its rectangle is the media box
    PDFContentDisplayImageOptions pictureOptions;
    pictureOptions.maximumPixels = 400;
    const PDFContentDisplayShading functionBased = PDFContentDisplay::getShading(&document, container, shadings[2]->getId(), false, pictureOptions, nullptr);
    QCOMPARE(functionBased.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(functionBased.kind, PDFContentDisplayShading::Kind::Picture);
    QVERIFY(isClose(functionBased.pictureRectangle, QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE), 1e-9));
    QCOMPARE(functionBased.picture.size(), QSize(20, 20));

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

// ---------------------------------------------------------------- fix package FX1 of stage E1

PDFDocument ContentDisplayTest::createMeshDocument(const QByteArray& content)
{
    return Documents::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        // The vertices and the patches in the units of the page: the coordinates are written with
        // /Decode [0 2^n - 1] (8 or 16 bits), so a value is its coordinate; the colors of 8 bits
        // with /Decode [0 1], DeviceRGB (ISO 32000-2, 8.7.4.5.5 - 8.7.4.5.8)
        auto meshEntries = [](int shadingType, int bitsPerCoordinate, bool hasFlag)
        {
            const int coordinateMaximum = (1 << bitsPerCoordinate) - 1;
            Documents::DictionaryEntries entries = { { "ShadingType", PDFObject::createInteger(shadingType) },
                                                     { "ColorSpace", PDFObject::createName("DeviceRGB") },
                                                     { "BitsPerCoordinate", PDFObject::createInteger(bitsPerCoordinate) },
                                                     { "BitsPerComponent", PDFObject::createInteger(8) },
                                                     { "Decode", Documents::createIntegerArrayObject({ 0, coordinateMaximum, 0, coordinateMaximum, 0, 1, 0, 1, 0, 1 }) } };
            if (hasFlag)
            {
                entries.emplace_back("BitsPerFlag", PDFObject::createInteger(8));
            }
            return entries;
        };

        auto byte = [](int value) { return char(value & 0xFF); };
        auto word = [&byte](int value) { return bytes({ byte(value >> 8), byte(value) }); };

        // /S1: type 1 over the domain [0 1 0 1] mapped by /Matrix [100 0 0 100 0 0] onto the square
        // 0 ... 100; the function { 0 } of the type 4 gives (x, y, 0): red = x / 100, green = y / 100
        PDFObject function = Documents::addStreamObject(builder, { { "FunctionType", PDFObject::createInteger(4) },
                                                                   { "Domain", Documents::createIntegerArrayObject({ 0, 1, 0, 1 }) },
                                                                   { "Range", Documents::createIntegerArrayObject({ 0, 1, 0, 1, 0, 1 }) } }, QByteArray("{ 0 }"));
        PDFObject functionShading = Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(1) },
                                                                        { "ColorSpace", PDFObject::createName("DeviceRGB") },
                                                                        { "Matrix", Documents::createIntegerArrayObject({ 100, 0, 0, 100, 0, 0 }) },
                                                                        { "Function", function } });

        // /S4: one free-form triangle (flag 0): (0 0) red, (100 0) green, (0 100) blue
        QByteArray freeForm;
        freeForm += bytes({ byte(0), byte(0), byte(0), byte(255), byte(0), byte(0) });
        freeForm += bytes({ byte(0), byte(100), byte(0), byte(0), byte(255), byte(0) });
        freeForm += bytes({ byte(0), byte(0), byte(100), byte(0), byte(0), byte(255) });
        PDFObject freeFormShading = Documents::addStreamObject(builder, meshEntries(4, 8, true), freeForm);

        // /S5: a lattice of 2 x 2 vertices: (0 0) red, (100 0) green; (0 100) blue, (100 100) white
        Documents::DictionaryEntries latticeEntries = meshEntries(5, 8, false);
        latticeEntries.emplace_back("VerticesPerRow", PDFObject::createInteger(2));
        QByteArray lattice;
        lattice += bytes({ byte(0), byte(0), byte(255), byte(0), byte(0) });
        lattice += bytes({ byte(100), byte(0), byte(0), byte(255), byte(0) });
        lattice += bytes({ byte(0), byte(100), byte(0), byte(0), byte(255) });
        lattice += bytes({ byte(100), byte(100), byte(255), byte(255), byte(255) });
        PDFObject latticeShading = Documents::addStreamObject(builder, latticeEntries, lattice);

        // /S6, /S7: one patch over the square 0 ... 99 with straight edges and the control points at
        // the thirds (so the patch maps (u, v) linearly: x = 99 u, y = 99 v); the colors of the
        // corners (u v) = (0 0) black, (0 1) green, (1 1) yellow, (1 0) red: the color (u, v, 0)
        auto point = [&word](int x, int y) { return word(x) + word(y); };
        QByteArray coons(1, byte(0));
        for (const auto& corner : { std::pair(0, 0), std::pair(0, 33), std::pair(0, 66), std::pair(0, 99), std::pair(33, 99), std::pair(66, 99),
                                    std::pair(99, 99), std::pair(99, 66), std::pair(99, 33), std::pair(99, 0), std::pair(66, 0), std::pair(33, 0) })
        {
            coons += point(corner.first, corner.second);
        }
        QByteArray tensor = coons;
        for (const auto& inner : { std::pair(33, 33), std::pair(33, 66), std::pair(66, 66), std::pair(66, 33) })
        {
            tensor += point(inner.first, inner.second);
        }
        const QByteArray colors = bytes({ byte(0), byte(0), byte(0), byte(0), byte(255), byte(0), byte(255), byte(255), byte(0), byte(255), byte(0), byte(0) });
        PDFObject coonsShading = Documents::addStreamObject(builder, meshEntries(6, 16, true), coons + colors);
        PDFObject tensorShading = Documents::addStreamObject(builder, meshEntries(7, 16, true), tensor + colors);

        // The limit of 200 000 triangles (PDFContentDisplay::MAXIMUM_MESH_TRIANGLES), counted from
        // the sizes of the data: /L4 200 003 vertices of 6 bytes (200 001 triangles), /L4E 200 002
        // (200 000 triangles: within the limit; the flag 0, then the flag 1 - every vertex makes a
        // triangle with the two before it, 8.7.4.5.5); /L5 a lattice of 2 x 100 002 vertices of 5
        // bytes (2 x 100 001 x 1 = 200 002 triangles); /L6 Coons of 8-bit coordinates, 2 300 023
        // bytes: at most 18 400 184 / 184 = 100 001 patches of the least size (a flag, 8 points, 2
        // colors: 8 + 128 + 48 bits), two triangles each
        PDFObject large4 = Documents::addStreamObject(builder, meshEntries(4, 8, true), QByteArray(200003 * 6, char(0)));
        QByteArray edgeData(200002 * 6, char(0));
        for (qsizetype i = 6; i < edgeData.size(); i += 6)
        {
            edgeData[i] = char(1);
        }
        PDFObject large4Edge = Documents::addStreamObject(builder, meshEntries(4, 8, true), edgeData);
        Documents::DictionaryEntries large5Entries = meshEntries(5, 8, false);
        large5Entries.emplace_back("VerticesPerRow", PDFObject::createInteger(2));
        PDFObject large5 = Documents::addStreamObject(builder, large5Entries, QByteArray(2 * 100002 * 5, char(0)));
        PDFObject large6 = Documents::addStreamObject(builder, meshEntries(6, 8, true), QByteArray(2300023, char(0)));

        // Over the limit by the subdivision (the data are within it): /L4S 30 triangles of the data,
        // each one a half of the square 0 ... 100 with the colors red, green, blue at its corners
        // (the flag 0, then the flag 1: each vertex a triangle with the two before it; the vertices
        // go round the four corners, the colors round the three), subdivided down to one pixel -
        // about 20 000 triangles each; /L6S eight patches of /S6, each subdivided until the colors of
        // the corners of a triangle differ by 1 % (the legs 99 / 2^7): 2 x 4^7 = 32 768 triangles
        // each, 262 144 together
        QByteArray subdivided;
        const std::pair<int, int> corners[] = { { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 } };
        const QByteArray rgb[] = { bytes({ 255, 0, 0 }), bytes({ 0, 255, 0 }), bytes({ 0, 0, 255 }) };
        for (int i = 0; i < 32; ++i)
        {
            const std::pair<int, int>& corner = corners[i % 4];
            subdivided += bytes({ i < 3 ? 0 : 1, corner.first, corner.second }) + rgb[i % 3];
        }
        PDFObject subdividedShading = Documents::addStreamObject(builder, meshEntries(4, 8, true), subdivided);
        PDFObject subdividedPatches = Documents::addStreamObject(builder, meshEntries(6, 16, true), (coons + colors).repeated(8));

        // /PM: a shading pattern of the triangle of /S4 with /Background [1 1 1] (8.7.4.3: the
        // background fills what the shading does not paint, when it is used as a pattern)
        Documents::DictionaryEntries backgroundEntries = meshEntries(4, 8, true);
        backgroundEntries.emplace_back("Background", Documents::createIntegerArrayObject({ 1, 1, 1 }));
        PDFObject backgroundShading = Documents::addStreamObject(builder, backgroundEntries, freeForm);
        PDFObject pattern = Documents::createDictionaryObject({ { "PatternType", PDFObject::createInteger(2) }, { "Shading", backgroundShading } });

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Shading"), Documents::createDictionaryObject({ { "S1", functionShading }, { "S4", freeFormShading }, { "S5", latticeShading },
                                                                                                    { "S6", coonsShading }, { "S7", tensorShading }, { "L4", large4 },
                                                                                                    { "L4E", large4Edge }, { "L5", large5 }, { "L6", large6 },
                                                                                                    { "L4S", subdividedShading }, { "L6S", subdividedPatches } }));
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "PM", pattern } }));
        return resources;
    });
}

QByteArray ContentDisplayTest::createGrayJpeg(int sample, int width, int height, bool withEndMarker)
{
    auto word = [](int value) { return bytes({ char((value >> 8) & 0xFF), char(value & 0xFF) }); };

    QByteArray data("\xFF\xD8", 2);

    // DQT (B.2.4.1): table 0, all values 8
    data += QByteArray("\xFF\xDB", 2) + word(67) + QByteArray(1, char(0)) + QByteArray(64, char(8));

    // DHT (B.2.4.2): the luminance DC table of Annex K (Table K.3) and an AC table of the end of
    // block alone (the code 0)
    data += QByteArray("\xFF\xC4", 2) + word(2 + 17 + 12 + 17 + 1);
    data += QByteArray(1, char(0x00));
    for (const int count : { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 })
    {
        data += char(count);
    }
    for (int value = 0; value < 12; ++value)
    {
        data += char(value);
    }
    data += char(0x10);
    data += char(1);
    data += QByteArray(15, char(0));
    data += char(0x00);

    // SOF0 (B.2.2): 8 bits, Y, X, one component (H = V = 1, table 0)
    data += QByteArray("\xFF\xC0", 2) + word(11) + QByteArray(1, char(8)) + word(height) + word(width) + bytes({ char(1), char(1), char(0x11), char(0) });

    // SOS (B.2.3): the component, the tables 0 / 0, Ss 0, Se 63, Ah = Al = 0
    data += QByteArray("\xFF\xDA", 2) + word(8) + bytes({ char(1), char(1), char(0), char(0), char(0x3F), char(0) });

    // The block: the DC difference 192 - 128 = 64 is of the category 7 (the code 11110 of Table
    // K.3), its 7 additional bits 1000000, the end of block 0, padded by 1 bits (F.1.2.3):
    // 11110100 00000111 = F4 07. Other samples are not needed by the tests.
    Q_ASSERT(sample == 192);
    Q_UNUSED(sample);
    data += bytes({ 0xF4, 0x07 });

    if (withEndMarker)
    {
        data += QByteArray("\xFF\xD9", 2);
    }
    return data;
}

QByteArray ContentDisplayTest::createJpeg2000Codestream(quint32 width, quint32 height)
{
    auto number = [](quint64 value, int byteCount)
    {
        QByteArray result;
        for (int i = byteCount - 1; i >= 0; --i)
        {
            result += char((value >> (8 * i)) & 0xFF);
        }
        return result;
    };

    // SOC, SIZ: Lsiz 41, Rsiz 0, Xsiz, Ysiz, XOsiz 0, YOsiz 0, XTsiz, YTsiz, XTOsiz 0, YTOsiz 0,
    // Csiz 1, Ssiz 7 (8 bits), XRsiz 1, YRsiz 1 (Table A.9)
    return QByteArray("\xFF\x4F\xFF\x51", 4) + number(41, 2) + number(0, 2) + number(width, 4) + number(height, 4) + number(0, 4) + number(0, 4) +
           number(width, 4) + number(height, 4) + number(0, 4) + number(0, 4) + number(1, 2) + bytes({ char(7), char(1), char(1) });
}

QByteArray ContentDisplayTest::bytes(std::initializer_list<int> values)
{
    QByteArray result;
    for (const int value : values)
    {
        result.append(char(value & 0xFF));
    }
    return result;
}

bool ContentDisplayTest::isPixelNear(const QImage& image, int x, int y, int red, int green, int blue, int tolerance)
{
    const QColor color = image.pixelColor(x, y);
    const bool result = std::abs(color.red() - red) <= tolerance && std::abs(color.green() - green) <= tolerance && std::abs(color.blue() - blue) <= tolerance && color.alpha() == 255;
    if (!result)
    {
        qWarning() << "pixel" << x << y << color << "expected" << red << green << blue;
    }
    return result;
}

void ContentDisplayTest::meshShadings()
{
    // Every shading is drawn under the clip of the square 0 ... 100: the box of sh is the clip
    // (getBoundingBox), the picture of 10 000 pixels is 100 x 100, one pixel per unit; the pixel
    // (px, py) is the point (px + 0.5, 100 - py - 0.5) of the page. The expected colors are those
    // of the definitions, the meshing approximates them within its tolerance (1 % of a component)
    // and the flat triangles of its resolution: 4 of 255 are allowed.
    PDFDocument document = createMeshDocument("q 0 0 100 100 re W n /S1 sh Q q 0 0 100 100 re W n /S4 sh Q q 0 0 100 100 re W n /S5 sh Q "
                                              "q 0 0 100 100 re W n /S6 sh Q q 0 0 100 100 re W n /S7 sh Q");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> shadings = getNodes(*container, PDFContentNodeKind::Shading);
    QCOMPARE(shadings.size(), size_t(5));

    PDFContentDisplayImageOptions options;
    options.maximumPixels = 10000;
    std::vector<PDFContentDisplayShading> results;
    for (const PDFContentNodePointer& shading : shadings)
    {
        results.push_back(PDFContentDisplay::getShading(&document, container, shading->getId(), false, options, nullptr));
        const PDFContentDisplayShading& result = results.back();
        QCOMPARE(result.status, PDFContentDisplayShading::Status::Done);
        QCOMPARE(result.kind, PDFContentDisplayShading::Kind::Picture);
        QCOMPARE(result.picture.size(), QSize(100, 100));
        QVERIFY(isClose(result.pictureRectangle, QRectF(0, 0, 100, 100), 1e-9));
    }

    // Type 1 (8.7.4.5.2): the function at the point - (24.5 74.5): (0.245, 0.745, 0)
    QVERIFY(isPixelNear(results[0].picture, 24, 25, 62, 190, 0, 4));
    QVERIFY(isPixelNear(results[0].picture, 79, 89, 202, 27, 0, 4));

    // Type 4 (8.7.4.5.5): Gouraud by the barycentric coordinates - (19.5 20.5): 0.6 red, 0.195
    // green, 0.205 blue; outside of the triangle (80.5 80.5) nothing is painted
    QVERIFY(isPixelNear(results[1].picture, 19, 79, 153, 50, 52, 4));
    QCOMPARE(results[1].picture.pixelColor(80, 19).alpha(), 0);

    // Type 5 (8.7.4.5.6): the cells split into (v0 v1 v3) and (v3 v2 v0) - (74.5 25.5) in the first:
    // 0.255 red + 0.49 green + 0.255 white = (0.51, 0.745, 0.255); (24.5 75.5) in the second: 0.245
    // white + 0.51 blue + 0.245 red = (0.49, 0.245, 0.755)
    QVERIFY(isPixelNear(results[2].picture, 74, 74, 130, 190, 65, 4));
    QVERIFY(isPixelNear(results[2].picture, 24, 24, 125, 62, 193, 4));

    // Types 6, 7 (8.7.4.5.7, 8.7.4.5.8): the color bilinear in (u, v) - (24.5 25.5): u = 0.2475, v =
    // 0.2576; (74.5 75.5): u = 0.7525, v = 0.7626
    for (size_t i : { size_t(3), size_t(4) })
    {
        QVERIFY(isPixelNear(results[i].picture, 24, 74, 63, 66, 0, 4));
        QVERIFY(isPixelNear(results[i].picture, 74, 24, 192, 194, 0, 4));
    }

    // The rectangle of the picture is the box of the node within the box of the container: a clip
    // of -50 ... 350 on the page 0 ... 200 gives the page
    PDFDocument wideDocument = createMeshDocument("q -50 -50 400 400 re W n /S4 sh Q /Pattern cs /PM scn 0 0 100 100 re f");
    PDFContentContainerPointer wide = build(wideDocument, pageKey(wideDocument));
    const PDFContentDisplayShading wideShading = PDFContentDisplay::getShading(&wideDocument, wide, getNodes(*wide, PDFContentNodeKind::Shading)[0]->getId(), false, options, nullptr);
    QCOMPARE(wideShading.status, PDFContentDisplayShading::Status::Done);
    QVERIFY(isClose(wideShading.pictureRectangle, QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE), 1e-9));

    // A shading pattern with /Background fills the square of the path: the triangle as /S4, the rest
    // of the picture white (ISO 32000-2, 8.7.4.3); the color is given beside the picture too
    const PDFContentDisplayShading background = PDFContentDisplay::getShading(&wideDocument, wide, getNodes(*wide, PDFContentNodeKind::Path)[1]->getId(), false, options, nullptr);
    QCOMPARE(background.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(background.kind, PDFContentDisplayShading::Kind::Picture);
    QVERIFY(isClose(background.pictureRectangle, QRectF(0, 0, 100, 100), 1e-9));
    QVERIFY(isRgb(background.backgroundColor, 255, 255, 255));
    QVERIFY(isPixelNear(background.picture, 19, 79, 153, 50, 52, 4));
    QVERIFY(isPixelNear(background.picture, 80, 19, 255, 255, 255, 0));
}

void ContentDisplayTest::meshLimitAndCancel()
{
    PDFDocument document = createMeshDocument("q 0 0 100 100 re W n /L4 sh Q q 0 0 100 100 re W n /L4E sh Q q 0 0 100 100 re W n /L5 sh Q "
                                              "q 0 0 100 100 re W n /L6 sh Q q 0 0 100 100 re W n /S4 sh Q");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> shadings = getNodes(*container, PDFContentNodeKind::Shading);
    QCOMPARE(shadings.size(), size_t(5));

    PDFContentDisplayImageOptions options;
    options.maximumPixels = 10000;

    // Above MAXIMUM_MESH_TRIANGLES by the sizes of the data: refused before the mesh is made - the
    // operation control is asked twice only (after the pattern, after the meshing), never by the
    // meshing, which asks it every 1024 triangles of the data and for every triangle of a patch
    for (size_t i : { size_t(0), size_t(2), size_t(3) })
    {
        CountingControl never(1000000);
        const PDFContentDisplayShading refused = PDFContentDisplay::getShading(&document, container, shadings[i]->getId(), false, options, &never);
        QCOMPARE(refused.status, PDFContentDisplayShading::Status::TooLarge);
        QCOMPARE(refused.kind, PDFContentDisplayShading::Kind::Picture);
        QVERIFY(refused.picture.isNull());
        QCOMPARE(never.getCount(), 2);
    }

    // Within the limit by the data, over it by the subdivision: refused while it is meshed
    PDFDocument subdividedDocument = createMeshDocument("q 0 0 100 100 re W n /L4S sh Q q 0 0 100 100 re W n /L6S sh Q");
    PDFContentContainerPointer subdividedContainer = build(subdividedDocument, pageKey(subdividedDocument));
    for (const PDFContentNodePointer& shading : getNodes(*subdividedContainer, PDFContentNodeKind::Shading))
    {
        const PDFContentDisplayShading refused = PDFContentDisplay::getShading(&subdividedDocument, subdividedContainer, shading->getId(), false, options, nullptr);
        QVERIFY2(refused.status == PDFContentDisplayShading::Status::TooLarge, qPrintable(QStringLiteral("%1: %2 %3").arg(shading->getData().resourceName).arg(int(refused.status)).arg(refused.errorText)));
        QVERIFY(refused.picture.isNull());
    }

    // 200 000 triangles of the data are within the limit (all of them at one point, nothing is
    // painted)
    const PDFContentDisplayShading edge = PDFContentDisplay::getShading(&document, container, shadings[1]->getId(), false, options, nullptr);
    QCOMPARE(edge.status, PDFContentDisplayShading::Status::Done);
    QCOMPARE(edge.picture.size(), QSize(100, 100));
    QCOMPARE(edge.picture.pixelColor(50, 50).alpha(), 0);

    // Canceled: before the mesh; while its triangles are painted (a block of 4096 triangles: the
    // triangle of /S4 is subdivided into more than 8192 ones at one pixel per unit) - the questions
    // are the one after the pattern, the one after the meshing, the first block (canceled), the one
    // after the painting
    CanceledControl canceled;
    QCOMPARE(PDFContentDisplay::getShading(&document, container, shadings[4]->getId(), false, options, &canceled).status, PDFContentDisplayShading::Status::Canceled);

    CountingControl painting(3);
    const PDFContentDisplayShading canceledPainting = PDFContentDisplay::getShading(&document, container, shadings[4]->getId(), false, options, &painting);
    QCOMPARE(canceledPainting.status, PDFContentDisplayShading::Status::Canceled);
    QVERIFY(canceledPainting.picture.isNull());
    QCOMPARE(painting.getCount(), 4);

    CountingControl never(1000000);
    QCOMPARE(PDFContentDisplay::getShading(&document, container, shadings[4]->getId(), false, options, &never).status, PDFContentDisplayShading::Status::Done);
    QVERIFY(never.getCount() >= 5);

    // A budget of pixels, which no picture fits (zero pixels are one pixel): one pixel
    options.maximumPixels = 0;
    QCOMPARE(PDFContentDisplay::getShading(&document, container, shadings[4]->getId(), false, options, nullptr).picture.size(), QSize(1, 1));
}

void ContentDisplayTest::countsOfComplexPagePolicy()
{
    // The two numbers of the one complex-page policy of the canvas and the tool GUI (pe1_fixes.md
    // FX3 item 4): every node of the tree and the segments of the paths. The root, the square
    // (5 segments), the state run "1 0 0 rg", the open path m l (2 segments), the empty group, the
    // text object with its state run "/F1 10 Tf" and its show: 8 nodes, 7 segments.
    PDFDocument document = Documents::createFontsDocument("0 0 10 10 re f 1 0 0 rg 0 0 m 10 10 l S q Q BT /F1 10 Tf (A) Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayCounts counts = PDFContentDisplay::getCounts(*container);
    QCOMPARE(counts.nodeCount, size_t(8));
    QCOMPARE(counts.nodeCount, container->getNodeCount());
    QCOMPARE(counts.pathCount, size_t(2));
    QCOMPARE(counts.pathSegmentCount, size_t(7));
    QCOMPARE(counts.glyphCount, size_t(1));

    // A page of many objects and of few long paths gives the same two numbers to both parts
    QByteArray longPath("0 0 m");
    for (int i = 1; i <= 1000; ++i)
    {
        longPath += ' ' + QByteArray::number(i) + " 0 l";
    }
    longPath += " S";
    PDFDocument longDocument = Documents::createDocument(longPath, &Documents::createNoResources);
    PDFContentContainerPointer longContainer = build(longDocument, pageKey(longDocument));
    const PDFContentDisplayCounts longCounts = PDFContentDisplay::getCounts(*longContainer);
    QCOMPARE(longCounts.nodeCount, size_t(2));
    QCOMPARE(longCounts.pathSegmentCount, size_t(1001));
}

void ContentDisplayTest::imageCodestreamHeaders()
{
    auto image = [](const QByteArray& filter, int width, int height, const QByteArray& data) -> ImageDefinition
    {
        return ImageDefinition({ { "Width", PDFObject::createInteger(width) }, { "Height", PDFObject::createInteger(height) },
                                 { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) },
                                 { "Filter", PDFObject::createName(filter) } }, data);
    };

    const QString differentSize = QStringLiteral("The size of the image in its data differs from /Width and /Height.");
    const QString truncated = QStringLiteral("The data of the image end before the image does.");

    // A JPEG of 8 x 8 with the size of its frame header: decoded, the sample 192 everywhere
    const PDFContentDisplayImage decoded = decodeFirstImage(createImageDocument("/Im0 Do", { image("DCTDecode", 8, 8, createGrayJpeg(192, 8, 8, true)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(decoded.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(decoded.image.size(), QSize(8, 8));
    QCOMPARE(getPixel(decoded.image, 3, 5), QByteArray("192 192 192 255"));

    // The same data under /Width 1 /Height 1: the decoder would allocate by the frame header, so the
    // dictionary is no bound - refused (SECURITY_TODO item 77)
    const PDFContentDisplayImage smaller = decodeFirstImage(createImageDocument("/Im0 Do", { image("DCTDecode", 1, 1, createGrayJpeg(192, 8, 8, true)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(smaller.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(smaller.errorText, differentSize);

    // A frame header of 40000 x 40000 with three components (written by hand, B.2.2): 1.6 x 10^9
    // pixels are within 2^31, but 4.8 x 10^9 samples exceed the 32-bit buffer of the decoder
    QByteArray large("\xFF\xD8\xFF\xC0\x00\x11\x08\x9C\x40\x9C\x40\x03\x01\x11\x00\x02\x11\x00\x03\x11\x00", 21);
    const PDFContentDisplayImage tooLarge = decodeFirstImage(createImageDocument("/Im0 Do", { image("DCTDecode", 40000, 40000, large) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(tooLarge.status, PDFContentDisplayImage::Status::TooLarge);
    QCOMPARE(tooLarge.sourceSize, QSize(40000, 40000));

    // The data end inside of the scan (no EOI): the decoder would not end on them
    const PDFContentDisplayImage cut = decodeFirstImage(createImageDocument("/Im0 Do", { image("DCTDecode", 8, 8, createGrayJpeg(192, 8, 8, false)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(cut.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(cut.errorText, truncated);

    // No frame header at all
    const PDFContentDisplayImage noHeader = decodeFirstImage(createImageDocument("/Im0 Do", { image("DCTDecode", 8, 8, QByteArray("\xFF\xD8\xFF\xD9", 4)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(noHeader.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(noHeader.errorText, QStringLiteral("The size of the image cannot be read from its data."));

    // JPEG 2000: the codestream of SIZ 100000 x 100000 (10^10 pixels) under /Width 1 /Height 1; a
    // codestream of 4 x 4 under /Width 2 /Height 2 and inside of the box jp2c of a JP2 file; the same
    // under /Width 4 /Height 4 passes the header and is refused by the decoder (no tiles)
    const PDFContentDisplayImage jpxLarge = decodeFirstImage(createImageDocument("/Im0 Do", { image("JPXDecode", 1, 1, createJpeg2000Codestream(100000, 100000)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(jpxLarge.status, PDFContentDisplayImage::Status::TooLarge);

    const PDFContentDisplayImage jpxSmaller = decodeFirstImage(createImageDocument("/Im0 Do", { image("JPXDecode", 2, 2, createJpeg2000Codestream(4, 4)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(jpxSmaller.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(jpxSmaller.errorText, differentSize);

    // A JP2 file (I.5): the signature box, ftyp, jp2h with ihdr (2 x 2: not read, the codestream
    // decides), jp2c with the codestream
    auto box = [](const char* type, const QByteArray& content)
    {
        const quint32 length = quint32(8 + content.size());
        return bytes({ char(length >> 24), char((length >> 16) & 0xFF), char((length >> 8) & 0xFF), char(length & 0xFF) }) + QByteArray(type, 4) + content;
    };
    const QByteArray imageHeader = QByteArray(3, char(0)) + char(2) + QByteArray(3, char(0)) + char(2) + bytes({ char(0), char(1), char(7), char(7), char(0), char(0) });
    const QByteArray jp2 = box("jP  ", QByteArray("\x0D\x0A\x87\x0A", 4)) + box("ftyp", QByteArray("jp2 \0\0\0\0jp2 ", 12)) + box("jp2h", box("ihdr", imageHeader)) +
                           box("jp2c", createJpeg2000Codestream(4, 4));
    const PDFContentDisplayImage jp2Smaller = decodeFirstImage(createImageDocument("/Im0 Do", { image("JPXDecode", 2, 2, jp2) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(jp2Smaller.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(jp2Smaller.errorText, differentSize);

    const PDFContentDisplayImage jpxSame = decodeFirstImage(createImageDocument("/Im0 Do", { image("JPXDecode", 4, 4, createJpeg2000Codestream(4, 4)) }), PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(jpxSame.status, PDFContentDisplayImage::Status::Failed);
    QVERIFY(jpxSame.errorText != differentSize);

    // The soft mask is checked as the image: a JPEG mask of a frame header 8 x 8 under /Width 1
    PDFDocument maskedDocument = Documents::createDocument("/Im0 Do", [](PDFDocumentBuilder* builder)
    {
        PDFObject mask = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                               { "Width", PDFObject::createInteger(1) }, { "Height", PDFObject::createInteger(1) },
                                                               { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) },
                                                               { "Filter", PDFObject::createName("DCTDecode") } }, createGrayJpeg(192, 8, 8, true));
        PDFObject masked = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                                 { "Width", PDFObject::createInteger(1) }, { "Height", PDFObject::createInteger(1) },
                                                                 { "ColorSpace", PDFObject::createName("DeviceGray") }, { "BitsPerComponent", PDFObject::createInteger(8) },
                                                                 { "SMask", mask } }, QByteArray(1, char(0)));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im0", masked } }));
        return resources;
    });
    const PDFContentDisplayImage maskRefused = decodeFirstImage(maskedDocument, PDFContentDisplayImageOptions(), nullptr);
    QCOMPARE(maskRefused.status, PDFContentDisplayImage::Status::Failed);
    QCOMPARE(maskRefused.errorText, differentSize);
}

void ContentDisplayTest::imageResourcesInheritedFromCaller()
{
    // The page draws /FmA and defines /Im1 blue; /FmA has its own resources with /Im1 red and /FmB;
    // /FmB has no resources and draws /Im1: the renderer draws the red one of its caller (NOTES.txt
    // 7.8.3, the caller first, then the page), so the display does
    PDFObject callerResources;
    PDFDocument document = Documents::createDocument("/FmA Do", [&callerResources](PDFDocumentBuilder* builder)
    {
        auto addImage = [builder](const QByteArray& rgb)
        {
            return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Image") },
                                                         { "Width", PDFObject::createInteger(1) }, { "Height", PDFObject::createInteger(1) },
                                                         { "ColorSpace", PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", PDFObject::createInteger(8) } }, rgb);
        };
        PDFObject red = addImage(QByteArray("\xFF\x00\x00", 3));
        PDFObject blue = addImage(QByteArray("\x00\x00\xFF", 3));
        PDFObject formB = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) } }, QByteArray("q 10 0 0 10 0 0 cm /Im1 Do Q"));
        callerResources = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Im1", red }, { "FmB", formB } }) } });
        PDFObject formA = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) }, { "Resources", callerResources } }, QByteArray("/FmB Do"));

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "FmA", formA }, { "Im1", blue } }));
        return resources;
    });

    // The key of /FmB from the use in /FmA
    PDFContentContainerPointer page = build(document, pageKey(document));
    const PDFObjectReference formAReference = getNodes(*page, PDFContentNodeKind::FormUse)[0]->getData().xobject;
    PDFContentContainerPointer formA = build(document, PDFContentContainerKey::createForm(formAReference));
    QVERIFY(formA);
    const PDFObjectReference formBReference = getNodes(*formA, PDFContentNodeKind::FormUse)[0]->getData().xobject;
    PDFContentContainerPointer formB = build(document, PDFContentContainerKey::createForm(formBReference), callerResources);
    QVERIFY(formB);
    QVERIFY(formB->getInheritedResources().isDictionary());

    PDFContentDisplayImageOptions options;
    options.pageIndex = 0;
    const PDFContentDisplayImage image = PDFContentDisplay::decodeImage(&document, formB, getNodes(*formB, PDFContentNodeKind::Image)[0]->getId(), options, nullptr);
    QCOMPARE(image.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(getPixel(image.image, 0, 0), QByteArray("255 0 0 255"));

    // Drawn by the page alone (no caller known): the blue one of the page
    PDFContentContainerPointer formOfPage = build(document, PDFContentContainerKey::createForm(formBReference));
    const PDFContentDisplayImage pageImage = PDFContentDisplay::decodeImage(&document, formOfPage, getNodes(*formOfPage, PDFContentNodeKind::Image)[0]->getId(), options, nullptr);
    QCOMPARE(pageImage.status, PDFContentDisplayImage::Status::Decoded);
    QCOMPARE(getPixel(pageImage.image, 0, 0), QByteArray("0 0 255 255"));
}

void ContentDisplayTest::patternOfMovedObject()
{
    // R-5: a moved object paints with a copy of its pattern with /Matrix x D, a new object of the
    // session. The second path of createTwoTilingPatternsDocument fills with /PC (/Matrix the
    // identity), moved by D = [1 0 0 1 10 0]: the copy has /Matrix [1 0 0 1 10 0]. The shading
    // pattern /PS of createShadingDocument (/Matrix [1 0 0 1 5 5]) moved by [1 0 0 1 0 20]:
    // [1 0 0 1 5 25] (ISO 32000-2, 8.7.3.1).
    PDFDocumentPointer document(new PDFDocument(Documents::createTwoTilingPatternsDocument()));
    PDFContentEditSession session(document, PDFCMSPointer(), nullptr);
    const PDFContentContainerKey key = session.getPageKey(0);
    QString errorText;
    QVERIFY2(session.loadContainer(key, nullptr, &errorText), qPrintable(errorText));
    const PDFContentNodeId pathId = getNodes(*session.getContainer(key), PDFContentNodeKind::Path)[1]->getId();
    const PDFObject sourcePattern = PDFContentDisplay::getDisplayState(*session.getContainer(key), pathId, createContext(document.get()))->fillColor.patternObject;
    QVERIFY(sourcePattern.isReference());

    QVERIFY2(session.execute(PDFContentCommandFactory::createTransform({ PDFContentNodeReference{ key, pathId } }, QTransform(1, 0, 0, 1, 10, 0)), &errorText), qPrintable(errorText));
    const PDFContentContainerPointer moved = session.getContainer(key);
    const PDFContentEditSnapshotPointer snapshot = session.createSnapshot();
    const PDFObject copy = PDFContentDisplay::getDisplayState(*moved, pathId, createContext(document.get()))->fillColor.patternObject;
    QVERIFY(copy.isReference() && !(copy == sourcePattern) && snapshot->newObjects.count(copy.getReference()));

    PDFContentDisplayImageOptions options;
    options.maximumPixels = 100;
    options.newObjects = &snapshot->newObjects;
    const PDFContentDisplayTilingCell cell = PDFContentDisplay::getTilingCell(document.get(), moved, pathId, false, options, nullptr);
    QCOMPARE(cell.status, PDFContentDisplayTilingCell::Status::Done);
    QVERIFY(isClose(cell.matrix, QTransform(1, 0, 0, 1, 10, 0)));
    QCOMPARE(getPixel(cell.picture, 2, 7), QByteArray("255 0 0 255"));

    // Without the new objects the pattern is not found
    options.newObjects = nullptr;
    QVERIFY(PDFContentDisplay::getTilingCell(document.get(), moved, pathId, false, options, nullptr).status != PDFContentDisplayTilingCell::Status::Done);

    // The cell of the pattern under the use of the moved path: the matrix of the copy
    const PDFContentContainerPointer cellContainer = build(*document, PDFContentContainerKey::createPatternCell(sourcePattern.getReference()));
    QVERIFY(cellContainer);
    const PDFContentDisplayUsePath uses = { PDFContentDisplayUse{ moved, pathId } };
    PDFContentDisplayContext context = createContext(document.get());
    context.newObjects = &snapshot->newObjects;
    const std::optional<QTransform> cellMatrix = PDFContentDisplay::getContainerMatrix(*cellContainer, uses, context);
    QVERIFY(cellMatrix.has_value());
    QVERIFY(isClose(*cellMatrix, QTransform(1, 0, 0, 1, 10, 0)));
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*cellContainer, uses).has_value());

    // Another pattern is no copy of the cell: the first path fills with /PU
    const PDFContentNodeId otherId = getNodes(*moved, PDFContentNodeKind::Path)[0]->getId();
    QVERIFY(!PDFContentDisplay::getContainerMatrix(*cellContainer, { PDFContentDisplayUse{ moved, otherId } }, context).has_value());

    // New objects written by hand: the stream of /PC with /Matrix [1 0 0 1 7 0] is a copy; with
    // one byte more of the data, or with another /XStep, it is none
    const PDFStream* cellStream = document->getObjectByReference(sourcePattern.getReference()).getStream();
    QVERIFY(cellStream);
    auto makeCopy = [cellStream](bool isContentChanged, bool isDictionaryChanged)
    {
        PDFDictionaryBuilder dictionary(*cellStream->getDictionary());
        dictionary.setEntry(PDFInplaceOrMemoryString("Matrix"), Documents::createIntegerArrayObject({ 1, 0, 0, 1, 7, 0 }));
        if (isDictionaryChanged)
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("XStep"), PDFObject::createInteger(11));
        }
        QByteArray content = *cellStream->getContent();
        if (isContentChanged)
        {
            content += ' ';
        }
        return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
    };
    const std::map<PDFObjectReference, PDFObject> handMade = { { PDFObjectReference(1, PDFContentEditSession::NEW_OBJECT_GENERATION), makeCopy(false, false) },
                                                               { PDFObjectReference(2, PDFContentEditSession::NEW_OBJECT_GENERATION), makeCopy(true, false) },
                                                               { PDFObjectReference(3, PDFContentEditSession::NEW_OBJECT_GENERATION), makeCopy(false, true) } };
    PDFContentDisplayContext handMadeContext = createContext(document.get());
    handMadeContext.newObjects = &handMade;
    const PDFContentContainerPointer source = build(*document, key);
    for (const auto& [reference, expectedCopy] : std::vector<std::pair<PDFInteger, bool>>{ { 1, true }, { 2, false }, { 3, false } })
    {
        PDFContentStateOverride override;
        override.properties[PDFContentStateProperty::FillColor] = makeColor(QByteArray(), { });
        override.properties[PDFContentStateProperty::FillColor].color.patternObject = PDFObject::createReference(PDFObjectReference(reference, PDFContentEditSession::NEW_OBJECT_GENERATION));
        const PDFContentContainerPointer withCopy = withOverride(source, pathId, override);
        const std::optional<QTransform> handMadeMatrix = PDFContentDisplay::getContainerMatrix(*cellContainer, { PDFContentDisplayUse{ withCopy, pathId } }, handMadeContext);
        QCOMPARE(handMadeMatrix.has_value(), expectedCopy);
        if (expectedCopy)
        {
            QVERIFY(isClose(*handMadeMatrix, QTransform(1, 0, 0, 1, 7, 0)));
        }
    }

    // A shading pattern of a moved path
    PDFDocumentPointer shadingDocument(new PDFDocument(createShadingDocument("/Pattern cs /PS scn 0 0 10 10 re f")));
    PDFContentEditSession shadingSession(shadingDocument, PDFCMSPointer(), nullptr);
    const PDFContentContainerKey shadingKey = shadingSession.getPageKey(0);
    QVERIFY2(shadingSession.loadContainer(shadingKey, nullptr, &errorText), qPrintable(errorText));
    const PDFContentNodeId shadedId = getNodes(*shadingSession.getContainer(shadingKey), PDFContentNodeKind::Path)[0]->getId();
    QVERIFY2(shadingSession.execute(PDFContentCommandFactory::createTransform({ PDFContentNodeReference{ shadingKey, shadedId } }, QTransform(1, 0, 0, 1, 0, 20)), &errorText), qPrintable(errorText));
    const PDFContentEditSnapshotPointer shadingSnapshot = shadingSession.createSnapshot();
    PDFContentDisplayImageOptions shadingOptions;
    shadingOptions.newObjects = &shadingSnapshot->newObjects;
    const PDFContentDisplayShading shading = PDFContentDisplay::getShading(shadingDocument.get(), shadingSession.getContainer(shadingKey), shadedId, false, shadingOptions, nullptr);
    QCOMPARE(shading.status, PDFContentDisplayShading::Status::Done);
    QVERIFY(isClose(shading.matrix, QTransform(1, 0, 0, 1, 5, 25)));
}

void ContentDisplayTest::boundingBoxOfMiterAndUnpaintedPath()
{
    // 0 0 m 100 10 l 0 20 l S, 2 w, a miter join: at (100 10) the segments make the angle
    // 2 atan(0.1) = 11.42 degrees, the miter length ratio 1 / sin(5.71 degrees) = 10.0499 (ISO
    // 32000-2, 8.4.3.5). Under the miter limit 10 the join is beveled: the outer corners of the
    // two strokes are (100 + 0.0995, 10 -+ 0.995), the right edge 100.0995. Under 11 the miter: its
    // tip lies 1 x 10.0499 from the corner along the bisector, at x 110.0499.
    PDFDocument document = Documents::createDocument("2 w 0 j 10 M 0 0 m 100 10 l 0 20 l S 11 M 0 0 m 100 10 l 0 20 l S", &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(2));
    const PDFContentDisplayBox beveled = PDFContentDisplay::getBoundingBox(*container, paths[0]->getId(), createContext(&document));
    const PDFContentDisplayBox mitered = PDFContentDisplay::getBoundingBox(*container, paths[1]->getId(), createContext(&document));
    QVERIFY2(std::abs(beveled.box.right() - 100.0995) <= 0.005, qPrintable(QString::number(beveled.box.right())));
    QVERIFY2(std::abs(mitered.box.right() - 110.0499) <= 0.005, qPrintable(QString::number(mitered.box.right())));

    // A path, which paints nothing (n without a clip, the painting switched off): the box of its
    // points, no stroke
    PDFDocument unpaintedDocument = Documents::createDocument("4 w 10 20 30 40 re n", &Documents::createNoResources);
    PDFContentContainerPointer unpainted = build(unpaintedDocument, pageKey(unpaintedDocument));
    const PDFContentDisplayBox unpaintedBox = PDFContentDisplay::getBoundingBox(*unpainted, getNodes(*unpainted, PDFContentNodeKind::Path)[0]->getId(), createContext(&unpaintedDocument));
    QVERIFY(unpaintedBox.isValid && !unpaintedBox.isUnknown);
    QVERIFY(isClose(unpaintedBox.box, QRectF(10, 20, 30, 40), 1e-9));
}

void ContentDisplayTest::boundingBoxOfLongStrokedPath()
{
    // 10 001 segments (m and 10 000 l) over x 0 ... 100, y 0 ... 1, stroked with 2 w, a miter join
    // and the miter limit 10: more than 10 000 segments, so the box is the box of the points
    // enlarged by the reach w / 2 x max(sqrt(2), 10) = 10 - (-10 -10) to (110 11)
    QByteArray content("2 w 0 j 10 M 0 0 m");
    for (int i = 1; i <= 10000; ++i)
    {
        content += ' ' + QByteArray::number(PDFReal(i) / 100.0) + ' ' + QByteArray::number(i % 2) + " l";
    }
    content += " S";
    PDFDocument document = Documents::createDocument(content, &Documents::createNoResources);
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentNodePointer path = getNodes(*container, PDFContentNodeKind::Path)[0];
    QCOMPARE(path->getData().path.getSegments().size(), size_t(10001));

    const PDFContentDisplayBox box = PDFContentDisplay::getBoundingBox(*container, path->getId(), createContext(&document));
    QVERIFY(isClose(box.box, QRectF(-10, -10, 120, 21), 1e-9));

    // A round join reaches sqrt(2) x w / 2 (the projecting square cap would): the same path with 1 j
    PDFDocument roundDocument = Documents::createDocument(content.replace("0 j", "1 j"), &Documents::createNoResources);
    PDFContentContainerPointer roundContainer = build(roundDocument, pageKey(roundDocument));
    const PDFContentDisplayBox roundBox = PDFContentDisplay::getBoundingBox(*roundContainer, getNodes(*roundContainer, PDFContentNodeKind::Path)[0]->getId(), createContext(&roundDocument));
    const PDFReal reach = std::sqrt(2.0);
    QVERIFY(isClose(roundBox.box, QRectF(-reach, -reach, 100 + 2 * reach, 1 + 2 * reach), 1e-9));
}

void ContentDisplayTest::clippedNodesOfTextInMarkedContent()
{
    // The clip of the show A (7 Tr) inside of a marked content of the text object becomes real at
    // ET (ISO 32000-2, 9.3.6): it applies to the rectangle behind the text, not to the show B
    PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf /Span BMC 7 Tr (A) Tj EMC (B) Tj ET 0 0 10 10 re f");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const std::vector<PDFContentNodePointer> shows = getNodes(*container, PDFContentNodeKind::TextShow);
    const std::vector<PDFContentNodePointer> paths = getNodes(*container, PDFContentNodeKind::Path);
    QCOMPARE(shows.size(), size_t(2));
    QCOMPARE(paths.size(), size_t(1));
    QCOMPARE(PDFContentDisplay::getClippedNodes(*container, shows[0]->getId()), (std::vector<PDFContentNodeId>{ paths[0]->getId() }));
}

void ContentDisplayTest::textVerticalCell()
{
    // /FV: /Identity-V, w1 of the CID 65 is -0.7, of the other CIDs -0.9 (/DW2): at 10 Tf the cell of
    // A goes from 0 down to -7 in y and from -5 to 5 in x (ISO 32000-2, 9.7.4.3), with 50 Tz from -10
    // to 10 in the glyph space (x_text = 0.5 x x_glyph); the glyph B (CID 66) down to -9. The cell is
    // the box of a glyph without an outline: a vertical text without a font program has a box.
    PDFDocument document = Documents::createFontsDocument("BT /FV 10 Tf 50 100 Td <00410042> Tj 50 Tz <0041> Tj ET");
    PDFContentContainerPointer container = build(document, pageKey(document));
    const PDFContentDisplayTextPointer text = PDFContentDisplay::getText(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(text);
    QCOMPARE(text->glyphs.size(), size_t(3));
    QVERIFY(isClose(text->glyphs[0].cell, QRectF(-5, -7, 10, 7), 1e-9));
    QVERIFY(isClose(text->glyphs[1].cell, QRectF(-5, -9, 10, 9), 1e-9));
    QVERIFY(isClose(text->glyphs[2].cell, QRectF(-10, -7, 20, 7), 1e-9));

    // The box of the text object: the cells, or the outlines where the font is realized - never empty
    const PDFContentDisplayBox box = PDFContentDisplay::getBoundingBox(*container, getNodes(*container, PDFContentNodeKind::TextObject)[0]->getId(), createContext(&document));
    QVERIFY(!box.box.isEmpty());
    for (const PDFContentDisplayGlyph& glyph : text->glyphs)
    {
        if (!glyph.outline || glyph.outline->isEmpty())
        {
            QVERIFY(box.box.contains(glyph.matrix.mapRect(glyph.cell)));
        }
    }
}

QTEST_MAIN(ContentDisplayTest)

#include "tst_contentdisplaytest.moc"
