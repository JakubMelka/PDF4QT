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

// Edit Content, stage E0: the tree of the nodes of a content, its states and the tree builder
// (package P02; GUI_overhaul/edit_tool/zadani_jadro.md 4.2, 4.3, zadani_overeni.md 6.2, chapter 8).
//
// The expected values are written by hand from the content streams of the tests, from the plan
// and from ISO 32000-2, never taken from a run. A tree is compared as a text:
//
//   Kind(first:count/first:count){child child ...}
//
// the first pair is the span of the opening operations of the node (all operations of a leaf),
// the second one the span of its closing operations; a pair is left out, when the span is empty.
// The operations of a content are numbered from 0 in their order, so "q 1 w Q" is
// Root{Group(0:1/2:1){StateRun(1:1)}}.

#include "contenttestdocuments.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfcontentcommands.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentnode.h"
#include "pdfcontentoperation.h"
#include "pdfcontentresources.h"
#include "pdfcontentstate.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfobject.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTransform>

#include <atomic>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

using namespace pdf;

using Documents = pdftest::ContentTestDocuments;
using IssueKinds = QList<int>;

Q_DECLARE_METATYPE(pdf::PDFContentNodeKind)

class ContentTreeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Plain types, path, node, symbolic state
    void plainTypes();
    void pathSegments();
    void pathRectangle();
    void nodeKinds_data();
    void nodeKinds();
    void symbolicState();
    void stateDifference();

    // Container and its editor
    void containerQueries();
    void editorCreatesRoot();
    void editorInsert();
    void editorSetNodeData();
    void editorRemove();
    void editorMove();
    void editorSharesUnchangedNodes();
    void editorDepthLimit();
    void editorBuildsOnce();
    void editorNodeCache();
    void editorManyQueries();
    void containerIsUnchanged();

    // Builder: structure
    void operatorNodes_data();
    void operatorNodes();
    void saveChain_data();
    void saveChain();
    void pathPainting_data();
    void pathPainting();
    void pathGeometry();
    void opaqueRules_data();
    void opaqueRules();
    void operationFlags();
    void failedOperator();
    void depthLimit();
    void saveChainDepthLimit();
    void operandLimit();
    void operationLimit();
    void contentTail_data();
    void contentTail();
    void appendIndex();
    void unclosedNodes_data();
    void unclosedNodes();
    void unclosedNodesKept();
    void unreadableFormMatrix();
    void dashLimit_data();
    void dashLimit();
    void dashLimitCost();
    void dashLimitOfInitialState();
    void dashLimitOfInitialStateBeforeAnyOperator_data();
    void dashLimitOfInitialStateBeforeAnyOperator();
    void dashLimitBehind_data();
    void dashLimitBehind();
    void stateObjects();
    void repeatedContentStream();
    void contentSizeLimit();
    void contentOverStreams();
    void readOnlyPage();
    void contentsOfOtherTypes();
    void issueLimit();
    void inlineImage();
    void inlineImageWithoutEnd();
    void type3Procedures();
    void identifiers();
    void markedContent();
    void boundingBoxes();

    // Builder: states
    void sharedStates();
    void leaks();
    void clipEffects();
    void symbolicOperators();
    void symbolicExtGState();
    void softMaskMatrix();

    // Builder: containers of forms and patterns, fingerprint, limits of the walk
    void formUsedManyTimes();
    void formContainer();
    void formResourcesInherited_data();
    void formResourcesInherited();
    void formInitialState();
    void formCycleAndSharedForm();
    void patternCell();
    void uncoloredPatternCell();
    void fingerprint();
    void invalidKeys();
    void cancelAndProgress();

    // Builder: text (PDFContentTextLayout of package P06)
    void textLayout();
    void textWithoutLayout();

    // Threads and ownership
    void containerOutlivesDocument();
    void buildInThreads();

private:
    /// The resources of the documents of this file (documented at its definition)
    static PDFDictionaryBuilder createResources(PDFDocumentBuilder* builder);
    static PDFDocument createDocument(const QByteArray& content) { return Documents::createDocument(content, &ContentTreeTest::createResources); }

    static PDFContentContainerKey pageKey(const PDFDocument& document);
    static PDFContentBuildResult build(const PDFDocument& document,
                                       const PDFContentContainerKey& key,
                                       bool layoutText = false,
                                       const PDFContentResolvedStatePointer& initialState = nullptr,
                                       PDFInteger pageIndex = -1,
                                       const PDFOperationControl* control = nullptr,
                                       PDFProgressSink* progress = nullptr);
    static PDFContentBuildResult buildPage(const PDFDocument& document, bool layoutText = false) { return build(document, pageKey(document), layoutText); }

    /// The value of a name in a category of the resources of the page ("XObject", "Fm1")
    static PDFObject pageResource(const PDFDocument& document, const char* category, const char* name);

    static QString describe(const PDFContentNodePointer& node);
    static QString describe(const PDFContentContainerPointer& container) { return container ? describe(container->getRoot()) : QStringLiteral("<no container>"); }
    static PDFContentNodePointer nodeAt(const PDFContentContainerPointer& container, std::initializer_list<size_t> path);
    static IssueKinds issueKinds(const std::vector<PDFContentIssue>& issues);
    static IssueKinds kinds(std::initializer_list<PDFContentIssue::Kind> list);
    static std::vector<quint64> identifiers(const PDFContentNodePointer& root);

    /// A node of a kind for the tests of the editor
    static PDFContentNode::Data nodeData(PDFContentNodeKind kind, quint64 id);

    /// Inserts a state run of one generated operation "3 w" (the identifier \p id) under the root
    /// at an index; false, when the editor refuses it
    static bool insertGeneratedRun(PDFContentContainerEditor& editor, quint64 id, PDFInteger index);

    /// The identifiers of the unclosed nodes (PDFContentContainer::isUnclosed) in the order of
    /// a depth first walk
    static QList<quint64> unclosedIdentifiers(const PDFContentContainerPointer& container);

    /// A time limit of a test in milliseconds, scaled by PDF4QT_TEST_TIMEOUT_FACTOR
    static qint64 timeLimit(qint64 milliseconds) { return milliseconds * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")); }

    /// The matrix is a move by (x, y) and nothing else (a sum of advances is not exact in
    /// floating point numbers: the limit is a millionth of a unit)
    static bool isTranslation(const QTransform& matrix, qreal x, qreal y);
};

// ---------------------------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------------------------

/// The resources of the documents made by createDocument:
///
///   /Font        /F1  the simple font with hand-written widths (Documents::addSimpleFontObject:
///                     32 -> 300, A -> 700, B -> 650 thousandths), /T3 the plain Type 3 font
///   /XObject     /Im1 an image 1 x 1 (DeviceGray, 8 bits), /Fm1 the form of
///                     Documents::addFormObject (/BBox [0 0 40 30], /Matrix [2 0 0 2 10 20]),
///                     /PS1 an XObject with /Subtype /PS (neither an image nor a form)
///   /ExtGState   /GS1 << /LW 3 >>
///                /GSA every key, which sets a parameter (ISO 32000-2, Table 57), except /Font and /op:
///                     /LW 2 /LC 1 /LJ 2 /ML 5 /D [[1 2] 0] /RI /Perceptual /OP true /OPM 1
///                     /BG (function) /BG2 /Default /UCR (function) /TR /Identity /HT /Default
///                     /FL 2 /SM 0.5 /SA true /BM /Multiply /SMask (a mask dictionary) /CA 0.25
///                     /ca 0.75 /AIS true /TK false /UseBlackPtComp /ON /HTO [1 2]
///                /GSO << /OP true /op false >>
///                /GSM << /SMask (the mask dictionary of /GSA) >>
///                /GSF << /Font [(the font object of /F1) 9] /LW 2.5 >>
///                /GSFN << /Font [/F1 7] >> (a name in place of the reference of the font)
///                /GSFX << /Font [5 9] /LW 4 >> (an array of two items, whose first one is no font)
///                /GSD << /D [[3 3 ... 3] 0] >> with 200 elements of the dash array
///                /GSW every key of /GSA and /op, /Font with a value of a type the key cannot have
///   /ColorSpace  /Cs1 [/CalRGB << /WhitePoint [0.9505 1 1.089] >>]
///   /Pattern     /P1  a colored tiling pattern (/BBox [0 0 10 10], steps 20, content "0 0 5 5 re f")
///   /Shading     /Sh1 an axial shading
///   /Properties  /MC1 << /MCID 7 >> (a direct dictionary), /MC2 << /Kind (x) >>,
///                /OC1 an optional content group (an indirect object)
PDFDictionaryBuilder ContentTreeTest::createResources(PDFDocumentBuilder* builder)
{
    PDFObject image = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                            { "Subtype", PDFObject::createName("Image") },
                                                            { "Width", PDFObject::createInteger(1) },
                                                            { "Height", PDFObject::createInteger(1) },
                                                            { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                            { "BitsPerComponent", PDFObject::createInteger(8) } }, QByteArray::fromHex("80"));
    PDFObject postScript = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                 { "Subtype", PDFObject::createName("PS") } }, QByteArray("showpage"));

    PDFObject function = Documents::addObject(builder, Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) },
                                                                                          { "Domain", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                                          { "N", PDFObject::createInteger(1) } }));

    PDFObject maskGroup = Documents::createDictionaryObject({ { "S", PDFObject::createName("Transparency") },
                                                              { "CS", PDFObject::createName("DeviceGray") } });
    PDFObject maskForm = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                               { "Subtype", PDFObject::createName("Form") },
                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                               { "Group", std::move(maskGroup) } }, QByteArray("0.5 g 0 0 100 200 re f"));
    PDFObject mask = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") },
                                                         { "S", PDFObject::createName("Luminosity") },
                                                         { "G", std::move(maskForm) } });

    PDFObject dash = Documents::createArrayObject({ Documents::createIntegerArrayObject({ 1, 2 }), PDFObject::createInteger(0) });

    PDFObject graphicStateAll = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") },
                                                                    { "LW", PDFObject::createInteger(2) },
                                                                    { "LC", PDFObject::createInteger(1) },
                                                                    { "LJ", PDFObject::createInteger(2) },
                                                                    { "ML", PDFObject::createInteger(5) },
                                                                    { "D", std::move(dash) },
                                                                    { "RI", PDFObject::createName("Perceptual") },
                                                                    { "OP", PDFObject::createBool(true) },
                                                                    { "OPM", PDFObject::createInteger(1) },
                                                                    { "BG", function },
                                                                    { "BG2", PDFObject::createName("Default") },
                                                                    { "UCR", function },
                                                                    { "TR", PDFObject::createName("Identity") },
                                                                    { "HT", PDFObject::createName("Default") },
                                                                    { "FL", PDFObject::createInteger(2) },
                                                                    { "SM", PDFObject::createReal(0.5) },
                                                                    { "SA", PDFObject::createBool(true) },
                                                                    { "BM", PDFObject::createName("Multiply") },
                                                                    { "SMask", mask },
                                                                    { "CA", PDFObject::createReal(0.25) },
                                                                    { "ca", PDFObject::createReal(0.75) },
                                                                    { "AIS", PDFObject::createBool(true) },
                                                                    { "TK", PDFObject::createBool(false) },
                                                                    { "UseBlackPtComp", PDFObject::createName("ON") },
                                                                    { "HTO", Documents::createIntegerArrayObject({ 1, 2 }) } });

    PDFObject calRGB = Documents::createArrayObject({ PDFObject::createName("CalRGB"),
                                                      Documents::createDictionaryObject({ { "WhitePoint", Documents::createNumberArrayObject({ 0.9505, 1.0, 1.089 }) } }) });

    PDFObject shading = Documents::addObject(builder, Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(2) },
                                                                                         { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                                                         { "Coords", Documents::createIntegerArrayObject({ 0, 0, 100, 0 }) },
                                                                                         { "Function", function } }));

    PDFObject layer = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("OCG") },
                                                                                       { "Name", PDFObject::createString("Layer") } }));

    PDFObject font = Documents::addSimpleFontObject(builder);

    // A value of a wrong type for every key (ISO 32000-2, Table 57: LW ML FL SM CA ca numbers, LC LJ
    // OPM integers, D HTO Font arrays, RI UseBlackPtComp names, OP op SA AIS TK booleans, BG UCR
    // functions, TR a function, an array or a name, HT a dictionary, a stream or a name, BM a name
    // or an array, SMask a dictionary or a name)
    PDFObject graphicStateWrong = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") },
                                                                      { "LW", PDFObject::createName("Wide") },
                                                                      { "LC", PDFObject::createString("x") },
                                                                      { "LJ", PDFObject::createName("J") },
                                                                      { "ML", Documents::createIntegerArrayObject({ 1 }) },
                                                                      { "D", PDFObject::createInteger(5) },
                                                                      { "RI", PDFObject::createInteger(1) },
                                                                      { "OP", PDFObject::createInteger(1) },
                                                                      { "op", PDFObject::createName("true") },
                                                                      { "OPM", PDFObject::createReal(1.5) },
                                                                      { "Font", Documents::createArrayObject({ font }) },
                                                                      { "BG", PDFObject::createName("Default") },
                                                                      { "UCR", PDFObject::createInteger(7) },
                                                                      { "TR", PDFObject::createInteger(5) },
                                                                      { "HT", PDFObject::createInteger(5) },
                                                                      { "FL", PDFObject::createName("F") },
                                                                      { "SM", PDFObject::createBool(true) },
                                                                      { "SA", PDFObject::createInteger(1) },
                                                                      { "BM", PDFObject::createInteger(5) },
                                                                      { "SMask", PDFObject::createInteger(5) },
                                                                      { "CA", PDFObject::createName("X") },
                                                                      { "ca", PDFObject::createString("y") },
                                                                      { "AIS", PDFObject::createInteger(0) },
                                                                      { "TK", PDFObject::createInteger(1) },
                                                                      { "UseBlackPtComp", PDFObject::createInteger(1) },
                                                                      { "HTO", PDFObject::createInteger(5) } });

    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", font },
                                                                                            { "T3", Documents::addType3FontObject(builder) } }));
    resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im1", std::move(image) },
                                                                                               { "Fm1", Documents::addFormObject(builder) },
                                                                                               { "PS1", std::move(postScript) } }));
    resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({
        { "GS1", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(3) } }) },
        { "GSA", std::move(graphicStateAll) },
        { "GSO", Documents::createDictionaryObject({ { "OP", PDFObject::createBool(true) }, { "op", PDFObject::createBool(false) } }) },
        { "GSM", Documents::createDictionaryObject({ { "SMask", mask } }) },
        { "GSF", Documents::createDictionaryObject({ { "Font", Documents::createArrayObject({ font, PDFObject::createInteger(9) }) }, { "LW", PDFObject::createReal(2.5) } }) },
        { "GSFN", Documents::createDictionaryObject({ { "Font", Documents::createArrayObject({ PDFObject::createName("F1"), PDFObject::createInteger(7) }) } }) },
        { "GSFX", Documents::createDictionaryObject({ { "Font", Documents::createArrayObject({ PDFObject::createInteger(5), PDFObject::createInteger(9) }) }, { "LW", PDFObject::createInteger(4) } }) },
        { "GSD", Documents::createDictionaryObject({ { "D", Documents::createArrayObject({ Documents::createIntegerArrayObject(std::vector<PDFInteger>(200, 3)), PDFObject::createInteger(0) }) } }) },
        { "GSW", std::move(graphicStateWrong) } }));
    resources.addEntry(PDFInplaceOrMemoryString("ColorSpace"), Documents::createDictionaryObject({ { "Cs1", std::move(calRGB) } }));
    resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P1", Documents::addTilingPatternObject(builder, 10, 20, 20, { }, QByteArray("0 0 5 5 re f")) } }));
    resources.addEntry(PDFInplaceOrMemoryString("Shading"), Documents::createDictionaryObject({ { "Sh1", std::move(shading) } }));
    resources.addEntry(PDFInplaceOrMemoryString("Properties"), Documents::createDictionaryObject({
        { "MC1", Documents::createDictionaryObject({ { "MCID", PDFObject::createInteger(7) } }) },
        { "MC2", Documents::createDictionaryObject({ { "Kind", PDFObject::createString("x") } }) },
        { "OC1", std::move(layer) } }));
    return resources;
}

PDFContentContainerKey ContentTreeTest::pageKey(const PDFDocument& document)
{
    return PDFContentContainerKey::createPage(0, document.getCatalog()->getPage(0)->getPageReference());
}

PDFContentBuildResult ContentTreeTest::build(const PDFDocument& document,
                                             const PDFContentContainerKey& key,
                                             bool layoutText,
                                             const PDFContentResolvedStatePointer& initialState,
                                             PDFInteger pageIndex,
                                             const PDFOperationControl* control,
                                             PDFProgressSink* progress)
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
    context.pageIndex = pageIndex;

    PDFContentBuildOptions options;
    options.layoutText = layoutText;

    return PDFContentTreeBuilder::build(&document, key, initialState, context, options, control, progress);
}

PDFObject ContentTreeTest::pageResource(const PDFDocument& document, const char* category, const char* name)
{
    const PDFDictionary* resources = document.getDictionaryFromObject(document.getCatalog()->getPage(0)->getResources());
    const PDFDictionary* dictionary = resources ? document.getDictionaryFromObject(resources->get(category)) : nullptr;
    return dictionary ? dictionary->get(name) : PDFObject();
}

QString ContentTreeTest::describe(const PDFContentNodePointer& node)
{
    if (!node)
    {
        return QStringLiteral("<null>");
    }

    static const char* const names[] = { "Root", "Group", "MarkedContent", "MarkedPoint", "TextObject", "TextShow",
                                         "Path", "Image", "FormUse", "Shading", "StateRun", "Opaque" };

    QString text = QString::fromLatin1(names[int(node->getKind())]);

    const PDFContentOperationSpan& prefix = node->getPrefix();
    const PDFContentOperationSpan& suffix = node->getSuffix();
    if (!prefix.isEmpty() || !suffix.isEmpty())
    {
        text += QLatin1Char('(');
        if (!prefix.isEmpty())
        {
            text += QStringLiteral("%1:%2").arg(prefix.first).arg(prefix.count);
        }
        if (!suffix.isEmpty())
        {
            text += QStringLiteral("/%1:%2").arg(suffix.first).arg(suffix.count);
        }
        text += QLatin1Char(')');
    }

    if (!node->getChildren().empty())
    {
        QStringList children;
        for (const PDFContentNodePointer& child : node->getChildren())
        {
            children << describe(child);
        }
        text += QLatin1Char('{') + children.join(QLatin1Char(' ')) + QLatin1Char('}');
    }

    return text;
}

PDFContentNodePointer ContentTreeTest::nodeAt(const PDFContentContainerPointer& container, std::initializer_list<size_t> path)
{
    PDFContentNodePointer node = container ? container->getRoot() : nullptr;
    for (const size_t index : path)
    {
        if (!node || index >= node->getChildren().size())
        {
            return nullptr;
        }
        node = node->getChildren()[index];
    }
    return node;
}

IssueKinds ContentTreeTest::issueKinds(const std::vector<PDFContentIssue>& issues)
{
    IssueKinds result;
    for (const PDFContentIssue& issue : issues)
    {
        result << int(issue.kind);
    }
    return result;
}

IssueKinds ContentTreeTest::kinds(std::initializer_list<PDFContentIssue::Kind> list)
{
    IssueKinds result;
    for (const PDFContentIssue::Kind kind : list)
    {
        result << int(kind);
    }
    return result;
}

std::vector<quint64> ContentTreeTest::identifiers(const PDFContentNodePointer& root)
{
    // The identifiers in the order of a depth first walk (a node before its children)
    std::vector<quint64> result;
    std::vector<PDFContentNodePointer> stack = { root };
    while (!stack.empty())
    {
        const PDFContentNodePointer node = stack.back();
        stack.pop_back();
        result.push_back(node->getId().value);
        for (size_t i = node->getChildren().size(); i > 0; --i)
        {
            stack.push_back(node->getChildren()[i - 1]);
        }
    }
    return result;
}

PDFContentNode::Data ContentTreeTest::nodeData(PDFContentNodeKind kind, quint64 id)
{
    PDFContentNode::Data data;
    data.kind = kind;
    data.id = PDFContentNodeId{ id };
    return data;
}

bool ContentTreeTest::insertGeneratedRun(PDFContentContainerEditor& editor, quint64 id, PDFInteger index)
{
    const size_t operationIndex = editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::SetLineWidth, "3 w", PDFContentOperation::Operands(), { }));
    PDFContentNode::Data data = nodeData(PDFContentNodeKind::StateRun, id);
    data.prefix = PDFContentOperationSpan{ operationIndex, 1 };
    return editor.insertNode(data, PDFContentNodeId{ 1 }, index);
}

QList<quint64> ContentTreeTest::unclosedIdentifiers(const PDFContentContainerPointer& container)
{
    QList<quint64> result;
    for (const quint64 id : identifiers(container->getRoot()))
    {
        if (container->isUnclosed(PDFContentNodeId{ id }))
        {
            result << id;
        }
    }
    return result;
}

/// A control of a build, which cancels it at a deadline: a build with a cost out of proportion
/// ends as canceled then, instead of running for minutes
class ContentTreeTestDeadline : public PDFOperationControl
{
public:
    explicit ContentTreeTestDeadline(qint64 milliseconds) : m_deadline(milliseconds) { }

    virtual bool isOperationCanceled() const override { return m_deadline.hasExpired(); }

private:
    QDeadlineTimer m_deadline;
};

bool ContentTreeTest::isTranslation(const QTransform& matrix, qreal x, qreal y)
{
    constexpr qreal EPSILON = 1e-6;
    return std::abs(matrix.m11() - 1.0) < EPSILON && std::abs(matrix.m12()) < EPSILON &&
           std::abs(matrix.m21()) < EPSILON && std::abs(matrix.m22() - 1.0) < EPSILON &&
           std::abs(matrix.dx() - x) < EPSILON && std::abs(matrix.dy() - y) < EPSILON;
}

// ---------------------------------------------------------------------------------------------
//  Plain types, path, node, symbolic state
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::plainTypes()
{
    using Key = PDFContentContainerKey;
    using Property = PDFContentStateProperty;

    // ---- The identifier of a node: 0 is no node, the builder numbers from 1 (the root) ----
    QVERIFY(!PDFContentNodeId().isValid());
    QVERIFY(!(PDFContentNodeId{ 0 }).isValid());
    QVERIFY((PDFContentNodeId{ 1 }).isValid());
    QVERIFY((PDFContentNodeId{ 1 }) < (PDFContentNodeId{ 2 }));
    QVERIFY((PDFContentNodeId{ 57 }) > (PDFContentNodeId{ 12 }));
    QVERIFY((PDFContentNodeId{ 57 }) == (PDFContentNodeId{ 57 }));
    QVERIFY((PDFContentNodeId{ 57 }) != (PDFContentNodeId{ 58 }));

    // ---- The key of a container: a page (index and reference), a form, a cell of a pattern ----
    const PDFObjectReference pageReference(12, 0);
    const Key page = Key::createPage(3, pageReference);
    QVERIFY(page.kind == Key::Kind::Page);
    QCOMPARE(page.pageIndex, PDFInteger(3));
    QVERIFY(page.reference == pageReference);
    QVERIFY(page.isValid());

    const PDFObjectReference formReference(40, 0);
    const Key form = Key::createForm(formReference);
    QVERIFY(form.kind == Key::Kind::Form);
    QCOMPARE(form.pageIndex, PDFInteger(-1));
    QVERIFY(form.reference == formReference);
    QVERIFY(form.isValid());

    const PDFObjectReference patternReference(41, 2);
    const Key patternCell = Key::createPatternCell(patternReference);
    QVERIFY(patternCell.kind == Key::Kind::PatternCell);
    QCOMPARE(patternCell.pageIndex, PDFInteger(-1));
    QVERIFY(patternCell.reference == patternReference);
    QVERIFY(patternCell.isValid());

    // A key without an object is no container; a page needs its index too
    QVERIFY(!Key().isValid());
    QVERIFY(!Key::createPage(-1, pageReference).isValid());
    QVERIFY(!Key::createPage(0, PDFObjectReference()).isValid());
    QVERIFY(Key::createPage(0, pageReference).isValid());
    QVERIFY(!Key::createForm(PDFObjectReference()).isValid());
    QVERIFY(!Key::createPatternCell(PDFObjectReference()).isValid());

    // The order: the kind first (Page, Form, PatternCell), the index of the page then, the
    // reference (object number, generation) last
    QVERIFY(Key::createPage(99, PDFObjectReference(99, 0)) < Key::createForm(PDFObjectReference(1, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(99, 0)) < Key::createPatternCell(PDFObjectReference(1, 0)));
    QVERIFY(Key::createPage(1, PDFObjectReference(50, 0)) < Key::createPage(2, PDFObjectReference(10, 0)));
    QVERIFY(Key::createPage(2, PDFObjectReference(10, 0)) < Key::createPage(2, PDFObjectReference(11, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(5, 0)) < Key::createForm(PDFObjectReference(6, 0)));
    QVERIFY(Key::createForm(PDFObjectReference(5, 0)) < Key::createForm(PDFObjectReference(5, 1)));
    QVERIFY(Key::createForm(PDFObjectReference(6, 0)) > Key::createForm(PDFObjectReference(5, 1)));
    QVERIFY(!(Key::createForm(formReference) < Key::createForm(formReference)));
    QVERIFY(Key::createForm(formReference) == form);
    QVERIFY(Key::createForm(formReference) != Key::createPatternCell(formReference));
    QVERIFY(Key::createPage(3, pageReference) == page);
    QVERIFY(Key::createPage(4, pageReference) != page);

    // ---- The limits (zadani_overeni.md 6.2) ----
    QCOMPARE(PDFContentContainer::MAXIMUM_DEPTH, 256);
    QCOMPARE(PDFContentContainer::MAXIMUM_OPERATION_COUNT, size_t(5000000));
    QCOMPARE(PDFContentContainer::MAXIMUM_OPERAND_COUNT, size_t(100000));
    QCOMPARE(PDFContentContainer::MAXIMUM_MARKED_CONTENT_ID, PDFInteger(2147483647));    // 2^31 - 1

    // ---- A span of operations ----
    QVERIFY(PDFContentOperationSpan().isEmpty());
    QVERIFY(!(PDFContentOperationSpan{ 4, 1 }).isEmpty());
    QVERIFY((PDFContentOperationSpan{ 4, 0 }).isEmpty());
    QVERIFY((PDFContentOperationSpan{ 4, 2 }) == (PDFContentOperationSpan{ 4, 2 }));
    QVERIFY(!((PDFContentOperationSpan{ 4, 2 }) == (PDFContentOperationSpan{ 5, 2 })));

    // ---- The set of properties ----
    PDFContentStatePropertySet set;
    QVERIFY(set.isEmpty());
    QVERIFY(set.getProperties().empty());
    QVERIFY(!set.contains(Property::LineWidth));

    // The properties come back in the order of the enum, whatever the order of the inserts
    set.insert(Property::SoftMask);
    set.insert(Property::LineWidth);
    set.insert(Property::Font);
    set.insert(Property::Font);
    QVERIFY(!set.isEmpty());
    QVERIFY(set.contains(Property::LineWidth));
    QVERIFY(set.contains(Property::Font));
    QVERIFY(set.contains(Property::SoftMask));
    QVERIFY(!set.contains(Property::FillColor));
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::Font, Property::SoftMask }));

    set.remove(Property::Font);
    set.remove(Property::FillColor);    // Not in the set: nothing changes
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::SoftMask }));

    PDFContentStatePropertySet other;
    other.insert(Property::FillColor);
    other.insert(Property::LineWidth);
    QVERIFY(!(set == other));
    set.unite(other);
    QVERIFY(set.getProperties() == std::vector<Property>({ Property::LineWidth, Property::FillColor, Property::SoftMask }));
    QVERIFY(other.getProperties() == std::vector<Property>({ Property::LineWidth, Property::FillColor }));

    PDFContentStatePropertySet same;
    same.insert(Property::SoftMask);
    same.insert(Property::FillColor);
    same.insert(Property::LineWidth);
    QVERIFY(set == same);

    // Every property is a member of its own: the last one does not fall onto the first one
    PDFContentStatePropertySet last;
    last.insert(Property::BlackPointCompensation);
    QVERIFY(last.contains(Property::BlackPointCompensation));
    QVERIFY(!last.contains(Property::LineWidth));
    QVERIFY(last.getProperties() == std::vector<Property>({ Property::BlackPointCompensation }));

    PDFContentStatePropertySet all;
    for (int i = 0; i < int(Property::Count); ++i)
    {
        all.insert(Property(i));
    }
    const std::vector<Property> allProperties = all.getProperties();
    QCOMPARE(allProperties.size(), size_t(Property::Count));
    for (size_t i = 0; i < allProperties.size(); ++i)
    {
        QCOMPARE(int(allProperties[i]), int(i));
    }

    // ---- An override (R-1): empty = no matrix and no property ----
    PDFContentStateOverride override;
    QVERIFY(override.isEmpty());
    override.transform = QTransform::fromTranslate(10.0, 20.0);
    QVERIFY(!override.isEmpty());
    override.transform = QTransform();
    QVERIFY(override.isEmpty());
    PDFContentStateValue lineWidth;
    lineWidth.kind = PDFContentStateValue::Kind::Real;
    lineWidth.real = 2.5;
    override.properties[Property::LineWidth] = lineWidth;
    QVERIFY(!override.isEmpty());
    QVERIFY(PDFContentStateValue().kind == PDFContentStateValue::Kind::None);

    // ---- A leak (R-3): empty = nothing of the state differs at the end of the node ----
    QVERIFY(PDFContentStateLeak().isEmpty());
    PDFContentStateLeak propertyLeak;
    propertyLeak.properties.insert(Property::FillColor);
    QVERIFY(!propertyLeak.isEmpty());
    PDFContentStateLeak transformLeak;
    transformLeak.isTransformChanged = true;
    QVERIFY(!transformLeak.isEmpty());
    PDFContentStateLeak clipLeak;
    clipLeak.isClipChanged = true;
    QVERIFY(!clipLeak.isEmpty());
    PDFContentStateLeak unknownLeak;
    unknownLeak.isUnknown = true;
    QVERIFY(!unknownLeak.isEmpty());

    // ---- A clipping path is only "n" with W or W* (zadani_jadro.md 4.2): a painting with W
    //      ("re W f") is a path, which paints and clips ----
    QVERIFY(!PDFContentPathPaint().isClippingPath());
    PDFContentPathPaint paint;
    paint.clip = PDFContentPathPaint::Clip::NonZero;
    QVERIFY(paint.isClippingPath());
    paint.clip = PDFContentPathPaint::Clip::EvenOdd;
    QVERIFY(paint.isClippingPath());
    paint.fill = true;
    QVERIFY(!paint.isClippingPath());
    paint.fill = false;
    paint.stroke = true;
    QVERIFY(!paint.isClippingPath());
    paint.clip = PDFContentPathPaint::Clip::None;
    QVERIFY(!paint.isClippingPath());

    // ---- A node stores its data and its children ----
    QVERIFY(PDFContentNode::Data{}.kind == PDFContentNodeKind::Opaque);
    QVERIFY(PDFContentContainer::Data{}.root == nullptr);
    QVERIFY(PDFContentStateValue{}.kind == PDFContentStateValue::Kind::None);
    QVERIFY(PDFContentPropertySource{}.extGStateKey.isEmpty());

    // A build context names no page: a form or a cell of a pattern needs one, a page ignores it
    QCOMPARE(PDFContentBuildContext{}.pageIndex, PDFInteger(-1));

    PDFContentNode::Data defaultData;
    QVERIFY(defaultData.kind == PDFContentNodeKind::Opaque);
    QVERIFY(!defaultData.id.isValid());
    QVERIFY(!defaultData.override.has_value());
    QVERIFY(!defaultData.isModified);
    QVERIFY(!defaultData.mcid.has_value());
    QVERIFY(defaultData.textItems.empty());
    QVERIFY(defaultData.isTextComplete);

    PDFContentNode::Data leafData;
    leafData.kind = PDFContentNodeKind::Path;
    leafData.id = PDFContentNodeId{ 3 };
    leafData.parentId = PDFContentNodeId{ 2 };
    leafData.prefix = PDFContentOperationSpan{ 5, 4 };
    leafData.isModified = true;
    leafData.hasClipEffect = true;
    leafData.override = PDFContentStateOverride();
    leafData.override->transform = QTransform::fromTranslate(7.0, 8.0);
    const PDFContentNodePointer leaf = std::make_shared<const PDFContentNode>(leafData, std::vector<PDFContentNodePointer>());
    QVERIFY(leaf->getKind() == PDFContentNodeKind::Path);
    QVERIFY(leaf->getId() == (PDFContentNodeId{ 3 }));
    QVERIFY(leaf->getParentId() == (PDFContentNodeId{ 2 }));
    QVERIFY(leaf->getPrefix() == (PDFContentOperationSpan{ 5, 4 }));
    QVERIFY(leaf->getSuffix().isEmpty());
    QVERIFY(leaf->getChildren().empty());
    QVERIFY(leaf->isModified());
    QVERIFY(leaf->hasClipEffect());
    QVERIFY(leaf->getOverride() != nullptr);
    QCOMPARE(leaf->getOverride()->transform, QTransform::fromTranslate(7.0, 8.0));

    PDFContentNode::Data groupData;
    groupData.kind = PDFContentNodeKind::Group;
    groupData.id = PDFContentNodeId{ 2 };
    groupData.parentId = PDFContentNodeId{ 1 };
    groupData.prefix = PDFContentOperationSpan{ 4, 1 };
    groupData.suffix = PDFContentOperationSpan{ 9, 1 };
    groupData.boundingBox = QRectF(10.0, 20.0, 30.0, 40.0);
    groupData.saveDepth = 3;
    const PDFContentNodePointer group = std::make_shared<const PDFContentNode>(groupData, std::vector<PDFContentNodePointer>({ leaf }));
    QVERIFY(group->getKind() == PDFContentNodeKind::Group);
    QVERIFY(group->getPrefix() == (PDFContentOperationSpan{ 4, 1 }));
    QVERIFY(group->getSuffix() == (PDFContentOperationSpan{ 9, 1 }));
    QCOMPARE(group->getBoundingBox(), QRectF(10.0, 20.0, 30.0, 40.0));
    QCOMPARE(group->getData().saveDepth, 3);
    QCOMPARE(group->getChildren().size(), size_t(1));
    QVERIFY(group->getChildren().front() == leaf);
    QVERIFY(group->getOverride() == nullptr);
    QVERIFY(group->getState() == nullptr);
    QVERIFY(group->getLeak().isEmpty());

    // ---- A container stores its data ----
    PDFContentNode::Data rootData;
    rootData.kind = PDFContentNodeKind::Root;
    rootData.id = PDFContentNodeId{ 1 };
    PDFContentContainer::Data containerData;
    containerData.key = page;
    containerData.root = std::make_shared<const PDFContentNode>(rootData, std::vector<PDFContentNodePointer>({ group }));
    containerData.boundingBox = QRectF(0.0, 0.0, 595.0, 842.0);
    containerData.structuralParentKey = 5;
    containerData.fingerprint = QByteArrayLiteral("fingerprint");
    containerData.issues.push_back(PDFContentIssue{ PDFContentIssue::Kind::UnknownOperator, PDFContentNodeId{ 3 }, 7, QStringLiteral("issue") });
    containerData.isReadOnly = true;
    const PDFContentContainer container(containerData);
    QVERIFY(container.getKey() == page);
    QVERIFY(container.getRoot() == containerData.root);
    QCOMPARE(container.getBoundingBox(), QRectF(0.0, 0.0, 595.0, 842.0));
    QVERIFY(container.getMatrix().isIdentity());
    QCOMPARE(container.getStructuralParentKey(), PDFInteger(5));
    QCOMPARE(container.getFingerprint(), QByteArrayLiteral("fingerprint"));
    QCOMPARE(container.getIssues().size(), size_t(1));
    QVERIFY(container.getIssues().front().kind == PDFContentIssue::Kind::UnknownOperator);
    QVERIFY(container.isReadOnly());
    QVERIFY(container.getInitialState() == nullptr);

    // ---- The resolved state keeps the state of the processor, but not its pointer to the soft
    //      mask dictionary of the document: it only says, that a mask is set ----
    QVERIFY(!PDFContentResolvedState().hasSoftMask());

    PDFPageContentProcessorState processorState;
    processorState.setCurrentTransformationMatrix(QTransform(2.0, 0.0, 0.0, 3.0, 10.0, 20.0));
    processorState.setLineWidth(4.0);
    const PDFContentResolvedState withoutMask(processorState);
    QVERIFY(!withoutMask.hasSoftMask());
    QVERIFY(withoutMask.getState().getSoftMask() == nullptr);
    QCOMPARE(withoutMask.getCurrentTransformationMatrix(), QTransform(2.0, 0.0, 0.0, 3.0, 10.0, 20.0));
    QCOMPARE(withoutMask.getState().getLineWidth(), 4.0);

    const PDFObject softMaskObject = PDFObject::createDictionary(PDFDictionaryBuilder());
    const PDFDictionary* softMask = softMaskObject.getDictionary();
    QVERIFY(softMask != nullptr);
    processorState.setSoftMask(softMask);
    const PDFContentResolvedState withMask(processorState);
    QVERIFY(withMask.hasSoftMask());
    QVERIFY(withMask.getState().getSoftMask() == nullptr);
    QCOMPARE(withMask.getState().getLineWidth(), 4.0);
    QVERIFY(processorState.getSoftMask() == softMask);      // The state of the processor is not touched
}

void ContentTreeTest::pathSegments()
{
    using Type = PDFContentPath::SegmentType;

    PDFContentPath path;
    QVERIFY(path.isEmpty());
    QVERIFY(path.getSegments().empty());
    QCOMPARE(path.getBoundingBox(), QRectF());
    QVERIFY(!path.isRectangle());

    path.moveTo(QPointF(10.0, 20.0));
    path.lineTo(QPointF(30.0, 20.0));
    path.curveTo(QPointF(40.0, 60.0), QPointF(-5.0, 70.0), QPointF(30.0, 40.0));
    path.close();

    QVERIFY(!path.isEmpty());
    const std::vector<PDFContentPath::Segment>& segments = path.getSegments();
    QCOMPARE(segments.size(), size_t(4));
    QVERIFY(segments[0].type == Type::MoveTo);
    QCOMPARE(segments[0].points[0], QPointF(10.0, 20.0));
    QVERIFY(segments[1].type == Type::LineTo);
    QCOMPARE(segments[1].points[0], QPointF(30.0, 20.0));
    QVERIFY(segments[2].type == Type::CurveTo);
    QCOMPARE(segments[2].points[0], QPointF(40.0, 60.0));
    QCOMPARE(segments[2].points[1], QPointF(-5.0, 70.0));
    QCOMPARE(segments[2].points[2], QPointF(30.0, 40.0));
    QVERIFY(segments[3].type == Type::Close);

    // The box of all points, the control points included: x from -5 (the second control point)
    // to 40 (the first one), y from 20 to 70
    QCOMPARE(path.getBoundingBox(), QRectF(-5.0, 20.0, 45.0, 50.0));
    QVERIFY(!path.isRectangle());

    // Every point is mapped: (x, y) -> (2 x + 100, 3 y + 200); the kinds of the segments stay
    const PDFContentPath mapped = path.transformed(QTransform(2.0, 0.0, 0.0, 3.0, 100.0, 200.0));
    QCOMPARE(mapped.getSegments().size(), size_t(4));
    QVERIFY(mapped.getSegments()[0].type == Type::MoveTo);
    QCOMPARE(mapped.getSegments()[0].points[0], QPointF(120.0, 260.0));
    QCOMPARE(mapped.getSegments()[1].points[0], QPointF(160.0, 260.0));
    QVERIFY(mapped.getSegments()[2].type == Type::CurveTo);
    QCOMPARE(mapped.getSegments()[2].points[0], QPointF(180.0, 380.0));
    QCOMPARE(mapped.getSegments()[2].points[1], QPointF(90.0, 410.0));
    QCOMPARE(mapped.getSegments()[2].points[2], QPointF(160.0, 320.0));
    QVERIFY(mapped.getSegments()[3].type == Type::Close);
    QCOMPARE(mapped.getBoundingBox(), QRectF(90.0, 260.0, 90.0, 150.0));

    // The original is a value of its own
    QCOMPARE(path.getSegments()[0].points[0], QPointF(10.0, 20.0));
}

void ContentTreeTest::pathRectangle()
{
    using Type = PDFContentPath::SegmentType;

    // ISO 32000-2, 8.5.2.1: "x y width height re" is "x y m, (x + width) y l, (x + width)
    // (y + height) l, x (y + height) l, h". A negative width is kept: 50 20 -30 40 re goes from
    // (50, 20) to the left.
    PDFContentPath path;
    path.addRectangle(QRectF(50.0, 20.0, -30.0, 40.0));

    const std::vector<PDFContentPath::Segment>& segments = path.getSegments();
    QCOMPARE(segments.size(), size_t(5));
    QVERIFY(segments[0].type == Type::MoveTo);
    QCOMPARE(segments[0].points[0], QPointF(50.0, 20.0));
    QVERIFY(segments[1].type == Type::LineTo);
    QCOMPARE(segments[1].points[0], QPointF(20.0, 20.0));
    QVERIFY(segments[2].type == Type::LineTo);
    QCOMPARE(segments[2].points[0], QPointF(20.0, 60.0));
    QVERIFY(segments[3].type == Type::LineTo);
    QCOMPARE(segments[3].points[0], QPointF(50.0, 60.0));
    QVERIFY(segments[4].type == Type::Close);

    QRectF rectangle;
    QVERIFY(path.isRectangle(&rectangle));
    QVERIFY(path.isRectangle());
    QCOMPARE(rectangle.x(), 50.0);
    QCOMPARE(rectangle.y(), 20.0);
    QCOMPARE(rectangle.width(), -30.0);     // Not normalized
    QCOMPARE(rectangle.height(), 40.0);
    QCOMPARE(path.getBoundingBox(), QRectF(20.0, 20.0, 30.0, 40.0));

    // A scale and a move keep the rectangle: (x, y) -> (2 x + 1, -3 y + 5); the origin (50, 20)
    // goes to (101, -55), the width -30 to -60, the height 40 to -120
    const PDFContentPath scaled = path.transformed(QTransform(2.0, 0.0, 0.0, -3.0, 1.0, 5.0));
    QRectF scaledRectangle;
    QVERIFY(scaled.isRectangle(&scaledRectangle));
    QCOMPARE(scaledRectangle.x(), 101.0);
    QCOMPARE(scaledRectangle.y(), -55.0);
    QCOMPARE(scaledRectangle.width(), -60.0);
    QCOMPARE(scaledRectangle.height(), -120.0);
    QCOMPARE(scaled.getSegments()[2].points[0], QPointF(41.0, -175.0));    // (20, 60)

    // A rotation by a right angle, (x, y) -> (-y, x): the same shape, but its first side is
    // vertical, which "re" cannot say
    const PDFContentPath rotated = path.transformed(QTransform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0));
    QVERIFY(!rotated.isRectangle());
    QCOMPARE(rotated.getSegments().size(), size_t(5));
    QCOMPARE(rotated.getSegments()[0].points[0], QPointF(-20.0, 50.0));
    QCOMPARE(rotated.getSegments()[1].points[0], QPointF(-20.0, 20.0));

    // Anything more than the one rectangle is not a rectangle
    PDFContentPath twoRectangles;
    twoRectangles.addRectangle(QRectF(0.0, 0.0, 10.0, 10.0));
    QVERIFY(twoRectangles.isRectangle());
    twoRectangles.addRectangle(QRectF(20.0, 0.0, 10.0, 10.0));
    QVERIFY(!twoRectangles.isRectangle());
    QCOMPARE(twoRectangles.getSegments().size(), size_t(10));

    PDFContentPath rectangleAndLine;
    rectangleAndLine.addRectangle(QRectF(0.0, 0.0, 10.0, 10.0));
    rectangleAndLine.lineTo(QPointF(5.0, 5.0));
    QVERIFY(!rectangleAndLine.isRectangle());

    PDFContentPath lineAndRectangle;
    lineAndRectangle.moveTo(QPointF(1.0, 1.0));
    lineAndRectangle.addRectangle(QRectF(0.0, 0.0, 10.0, 10.0));
    QVERIFY(!lineAndRectangle.isRectangle());

    // The out parameter is not touched, when the path is no rectangle
    QRectF untouched(1.0, 2.0, 3.0, 4.0);
    QVERIFY(!lineAndRectangle.isRectangle(&untouched));
    QCOMPARE(untouched, QRectF(1.0, 2.0, 3.0, 4.0));
}

void ContentTreeTest::nodeKinds_data()
{
    QTest::addColumn<PDFContentNodeKind>("kind");
    QTest::addColumn<bool>("isLeaf");
    QTest::addColumn<bool>("isWrappable");

    // zadani_jadro.md 4.2: nodes with children are Root, Group, MarkedContent and TextObject;
    // 4.4, R-1: a wrap of an override is for Path, Image, Shading, FormUse and TextObject
    QTest::newRow("Root") << PDFContentNodeKind::Root << false << false;
    QTest::newRow("Group") << PDFContentNodeKind::Group << false << false;
    QTest::newRow("MarkedContent") << PDFContentNodeKind::MarkedContent << false << false;
    QTest::newRow("MarkedPoint") << PDFContentNodeKind::MarkedPoint << true << false;
    QTest::newRow("TextObject") << PDFContentNodeKind::TextObject << false << true;
    QTest::newRow("TextShow") << PDFContentNodeKind::TextShow << true << false;
    QTest::newRow("Path") << PDFContentNodeKind::Path << true << true;
    QTest::newRow("Image") << PDFContentNodeKind::Image << true << true;
    QTest::newRow("FormUse") << PDFContentNodeKind::FormUse << true << true;
    QTest::newRow("Shading") << PDFContentNodeKind::Shading << true << true;
    QTest::newRow("StateRun") << PDFContentNodeKind::StateRun << true << false;
    QTest::newRow("Opaque") << PDFContentNodeKind::Opaque << true << false;
}

void ContentTreeTest::nodeKinds()
{
    QFETCH(PDFContentNodeKind, kind);
    QFETCH(bool, isLeaf);
    QFETCH(bool, isWrappable);

    QCOMPARE(PDFContentNode::isLeafKind(kind), isLeaf);
    QCOMPARE(PDFContentNode::isWrappableKind(kind), isWrappable);
}

void ContentTreeTest::symbolicState()
{
    using Property = PDFContentStateProperty;
    using Origin = PDFContentPropertySource::Origin;

    // Nothing is set: every property is inherited, the matrix of the soft mask is the identity
    const PDFContentSymbolicState empty;
    for (int i = 0; i < int(Property::Count); ++i)
    {
        QVERIFY(empty.isInherited(Property(i)));
        QVERIFY(empty.getSource(Property(i)).origin == Origin::Inherited);
        QVERIFY(empty.getSource(Property(i)).resourceName.isEmpty());
    }
    QVERIFY(empty.getSoftMaskMatrix().isIdentity());

    PDFContentPropertySource fontSource;
    fontSource.origin = Origin::Operator;
    fontSource.resourceName = "F1";
    fontSource.object = PDFObject::createReference(PDFObjectReference(12, 0));
    fontSource.operationIndex = 5;

    // A copy with another source: the original does not change
    const PDFContentSymbolicState withFont = empty.withSource(Property::Font, fontSource);
    QVERIFY(empty.isInherited(Property::Font));
    QVERIFY(!withFont.isInherited(Property::Font));
    QVERIFY(withFont.getSource(Property::Font).origin == Origin::Operator);
    QCOMPARE(withFont.getSource(Property::Font).resourceName, QByteArray("F1"));
    QVERIFY(withFont.getSource(Property::Font).object == PDFObject::createReference(PDFObjectReference(12, 0)));
    QCOMPARE(withFont.getSource(Property::Font).operationIndex, size_t(5));
    QVERIFY(withFont.isInherited(Property::LineWidth));
    QVERIFY(withFont.isInherited(Property::BlackPointCompensation));

    PDFContentPropertySource maskSource;
    maskSource.origin = Origin::ExtGState;
    maskSource.resourceName = "GS1";
    maskSource.extGStateKey = "SMask";
    maskSource.operationIndex = 9;

    const PDFContentSymbolicState withMask = withFont.withSource(Property::SoftMask, maskSource).withSoftMaskMatrix(QTransform(2.0, 0.0, 0.0, 2.0, 10.0, 20.0));
    QVERIFY(withMask.getSource(Property::SoftMask).origin == Origin::ExtGState);
    QCOMPARE(withMask.getSource(Property::SoftMask).extGStateKey, QByteArray("SMask"));
    QCOMPARE(withMask.getSoftMaskMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 10.0, 20.0));
    QCOMPARE(withMask.getSource(Property::Font).resourceName, QByteArray("F1"));    // Kept
    QVERIFY(withFont.isInherited(Property::SoftMask));                             // The source of the copy is not changed
    QVERIFY(withFont.getSoftMaskMatrix().isIdentity());

    // A source replaces the one before it
    PDFContentPropertySource otherFont = fontSource;
    otherFont.resourceName = "F2";
    otherFont.operationIndex = 11;
    const PDFContentSymbolicState replaced = withMask.withSource(Property::Font, otherFont);
    QCOMPARE(replaced.getSource(Property::Font).resourceName, QByteArray("F2"));
    QCOMPARE(replaced.getSource(Property::Font).operationIndex, size_t(11));
    QCOMPARE(withMask.getSource(Property::Font).resourceName, QByteArray("F1"));

    // An inherited source makes the property inherited again: nothing is known about it,
    // whatever else the source says
    PDFContentPropertySource inheritedSource;
    inheritedSource.resourceName = "F3";
    inheritedSource.operationIndex = 13;
    const PDFContentSymbolicState cleared = replaced.withSource(Property::Font, inheritedSource);
    QVERIFY(cleared.isInherited(Property::Font));
    QVERIFY(cleared.getSource(Property::Font).resourceName.isEmpty());
    QCOMPARE(cleared.getSource(Property::Font).operationIndex, size_t(0));
    QVERIFY(!cleared.isInherited(Property::SoftMask));
}

void ContentTreeTest::stateDifference()
{
    using Property = PDFContentStateProperty;
    using State = PDFPageContentProcessorState;
    using Properties = std::vector<Property>;

    // PDFContentResolvedState::getDifference: the properties, whose value differs. One change of
    // a state against the default state of the processor each - exactly its property differs.
    struct Row
    {
        const char* name;
        std::function<void(State&)> change;
        Properties properties;
    };

    const std::vector<Row> rows =
    {
        { "w", [](State& state) { state.setLineWidth(3.0); }, { Property::LineWidth } },
        { "J", [](State& state) { state.setLineCapStyle(Qt::RoundCap); }, { Property::LineCap } },
        { "j", [](State& state) { state.setLineJoinStyle(Qt::BevelJoin); }, { Property::LineJoin } },
        { "M", [](State& state) { state.setMitterLimit(4.0); }, { Property::MiterLimit } },
        { "d", [](State& state) { PDFLineDashPattern pattern; pattern.setDashArray({ 1.0, 2.0 }); state.setLineDashPattern(pattern); }, { Property::DashPattern } },
        { "d phase", [](State& state) { PDFLineDashPattern pattern; pattern.setDashOffset(1.0); state.setLineDashPattern(pattern); }, { Property::DashPattern } },
        { "ri name", [](State& state) { state.setRenderingIntentName("Saturation"); }, { Property::RenderingIntent } },
        { "ri value", [](State& state) { state.setRenderingIntent(RenderingIntent::Saturation); }, { Property::RenderingIntent } },
        { "i", [](State& state) { state.setFlatness(2.0); }, { Property::Flatness } },
        { "stroke color", [](State& state) { state.setStrokeColor(QColor(Qt::black), PDFColor(0.5f)); }, { Property::StrokeColor } },
        { "stroke color converted", [](State& state) { state.setStrokeColor(QColor(Qt::red), state.getStrokeColorOriginal()); }, { Property::StrokeColor } },
        { "fill color", [](State& state) { state.setFillColor(QColor(Qt::black), PDFColor(0.5f)); }, { Property::FillColor } },
        { "font size", [](State& state) { state.setTextFontSize(12.0); }, { Property::Font } },
        { "Tc", [](State& state) { state.setTextCharacterSpacing(1.0); }, { Property::CharacterSpacing } },
        { "Tw", [](State& state) { state.setTextWordSpacing(1.0); }, { Property::WordSpacing } },
        { "Tz", [](State& state) { state.setTextHorizontalScaling(90.0); }, { Property::HorizontalScaling } },
        { "TL", [](State& state) { state.setTextLeading(14.0); }, { Property::Leading } },
        { "Tr", [](State& state) { state.setTextRenderingMode(TextRenderingMode::Stroke); }, { Property::TextRenderingMode } },
        { "Ts", [](State& state) { state.setTextRise(2.0); }, { Property::TextRise } },
        { "TK", [](State& state) { state.setTextKnockout(false); }, { Property::TextKnockout } },
        { "CA", [](State& state) { state.setAlphaStroking(0.5); }, { Property::AlphaStroking } },
        { "ca", [](State& state) { state.setAlphaFilling(0.5); }, { Property::AlphaFilling } },
        { "BM", [](State& state) { state.setBlendMode(BlendMode::Multiply); }, { Property::BlendMode } },
        { "AIS", [](State& state) { state.setAlphaIsShape(true); }, { Property::AlphaIsShape } },
        { "SA", [](State& state) { state.setStrokeAdjustment(true); }, { Property::StrokeAdjustment } },
        { "OP", [](State& state) { PDFOverprintMode mode; mode.overprintStroking = true; state.setOverprintMode(mode); }, { Property::OverprintStroking } },
        { "op", [](State& state) { PDFOverprintMode mode; mode.overprintFilling = true; state.setOverprintMode(mode); }, { Property::OverprintFilling } },
        { "OPM", [](State& state) { PDFOverprintMode mode; mode.overprintMode = 1; state.setOverprintMode(mode); }, { Property::OverprintMode } },
        { "BG", [](State& state) { state.setBlackGenerationFunction(PDFObject::createName("Default")); }, { Property::BlackGeneration } },
        { "UCR", [](State& state) { state.setUndercolorRemovalFunction(PDFObject::createName("Default")); }, { Property::UndercolorRemoval } },
        { "TR", [](State& state) { state.setTransferFunction(PDFObject::createName("Identity")); }, { Property::Transfer } },
        { "HT", [](State& state) { state.setHalftone(PDFObject::createName("Default")); }, { Property::Halftone } },
        { "HTO", [](State& state) { state.setHalftoneOrigin(QPointF(1.0, 2.0)); }, { Property::HalftoneOrigin } },
        { "SM", [](State& state) { state.setSmoothness(0.5); }, { Property::Smoothness } },
        { "UseBlackPtComp", [](State& state) { state.setBlackPointCompensationMode(BlackPointCompensationMode::ON); }, { Property::BlackPointCompensation } },

        // More changes at once, in the order of the enum
        { "w and ca", [](State& state) { state.setAlphaFilling(0.5); state.setLineWidth(3.0); }, { Property::LineWidth, Property::AlphaFilling } },

        // What is no property of a resolved state: the matrix (compared by the caller) and the
        // text matrices
        { "cm", [](State& state) { state.setCurrentTransformationMatrix(QTransform(2.0, 0.0, 0.0, 2.0, 5.0, 6.0)); }, { } },
        { "Tm", [](State& state) { state.setTextMatrix(QTransform::fromTranslate(5.0, 6.0)); state.setTextLineMatrix(QTransform::fromTranslate(5.0, 6.0)); }, { } },
        { "nothing", [](State&) { }, { } }
    };

    // Both states have the same color spaces (a color space is compared as an object)
    const State base;
    const PDFContentResolvedState baseResolved(base);

    for (const Row& row : rows)
    {
        State changed = base;
        row.change(changed);
        const PDFContentResolvedState changedResolved(changed);

        QVERIFY2(PDFContentResolvedState::getDifference(baseResolved, changedResolved).getProperties() == row.properties, row.name);
        QVERIFY2(PDFContentResolvedState::getDifference(changedResolved, baseResolved).getProperties() == row.properties, row.name);
        QVERIFY2(PDFContentResolvedState::getDifference(base, changed).getProperties() == row.properties, row.name);
        QVERIFY2(PDFContentResolvedState::getDifference(changedResolved, changedResolved).isEmpty(), row.name);
    }

    // A color space is compared as an object, not by its kind: another default state has color
    // space objects of its own (DeviceGray again, the same black) - both colors differ, nothing else
    const State other;
    QVERIFY(other.getFillColorSpace() != base.getFillColorSpace());
    QVERIFY(PDFContentResolvedState::getDifference(baseResolved, PDFContentResolvedState(other)).getProperties() == Properties({ Property::StrokeColor, Property::FillColor }));

    // The soft mask: a resolved state knows only, whether one is set. A mask against no mask
    // differs; two different masks do not (their sources in the symbolic states tell them apart).
    // Two states of the processor are compared without the mask.
    const PDFObject firstMaskObject = PDFObject::createDictionary(PDFDictionaryBuilder());
    const PDFObject secondMaskObject = PDFObject::createDictionary(PDFDictionaryBuilder());
    State firstMask = base;
    firstMask.setSoftMask(firstMaskObject.getDictionary());
    State secondMask = base;
    secondMask.setSoftMask(secondMaskObject.getDictionary());

    const PDFContentResolvedState firstMaskResolved(firstMask);
    const PDFContentResolvedState secondMaskResolved(secondMask);
    QVERIFY(PDFContentResolvedState::getDifference(baseResolved, firstMaskResolved).getProperties() == Properties({ Property::SoftMask }));
    QVERIFY(PDFContentResolvedState::getDifference(firstMaskResolved, baseResolved).getProperties() == Properties({ Property::SoftMask }));
    QVERIFY(PDFContentResolvedState::getDifference(firstMaskResolved, secondMaskResolved).isEmpty());
    QVERIFY(PDFContentResolvedState::getDifference(base, firstMask).isEmpty());

    // The states of a built page: the font is compared as the object of the font cache (the
    // same font again is the same object), a color space of the resources and a device space
    // as the object the processor shares.
    // Tf0 rg1 re2 f3 Tf4 re5 f6 Tf7 rg8 re9 f10 cs11 sc12 re13 f14 cs15 sc16 re17 f18
    const PDFDocument document = createDocument("/F1 10 Tf 1 0 0 rg 0 0 5 5 re f /T3 10 Tf 0 0 5 5 re f /F1 10 Tf 1 0 0 rg 0 0 5 5 re f "
                                                "/Cs1 cs 0.1 0.2 0.3 sc 0 0 5 5 re f /Cs1 cs 0.1 0.2 0.3 sc 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:2) Path(2:2) StateRun(4:1) Path(5:2) StateRun(7:2) Path(9:2) "
                                                        "StateRun(11:2) Path(13:2) StateRun(15:2) Path(17:2)}"));

    const PDFContentResolvedState& first = *nodeAt(result.container, { 1 })->getState();
    const PDFContentResolvedState& otherFont = *nodeAt(result.container, { 3 })->getState();
    const PDFContentResolvedState& firstFontAgain = *nodeAt(result.container, { 5 })->getState();
    const PDFContentResolvedState& calibrated = *nodeAt(result.container, { 7 })->getState();
    const PDFContentResolvedState& calibratedAgain = *nodeAt(result.container, { 9 })->getState();

    QVERIFY(PDFContentResolvedState::getDifference(first, otherFont).getProperties() == Properties({ Property::Font }));     // The same size, another font
    QVERIFY(PDFContentResolvedState::getDifference(first, firstFontAgain).isEmpty());
    QVERIFY(PDFContentResolvedState::getDifference(firstFontAgain, calibrated).getProperties() == Properties({ Property::FillColor }));
    QVERIFY(PDFContentResolvedState::getDifference(calibrated, calibratedAgain).isEmpty());
    QVERIFY(PDFContentResolvedState::getDifference(*result.container->getInitialState(), first).getProperties() == Properties({ Property::FillColor, Property::Font }));

    // Another build has another font cache: its font objects are others, the fonts differ
    const PDFContentBuildResult secondResult = buildPage(document);
    QVERIFY(PDFContentResolvedState::getDifference(first, *nodeAt(secondResult.container, { 1 })->getState()).contains(Property::Font));
}

// ---------------------------------------------------------------------------------------------
//  Container and its editor
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::containerQueries()
{
    // Root 1 { Group 2 { Path 5, Image 9 }, StateRun 3 }
    const PDFContentNodePointer path = std::make_shared<const PDFContentNode>(nodeData(PDFContentNodeKind::Path, 5), std::vector<PDFContentNodePointer>());
    const PDFContentNodePointer image = std::make_shared<const PDFContentNode>(nodeData(PDFContentNodeKind::Image, 9), std::vector<PDFContentNodePointer>());
    const PDFContentNodePointer group = std::make_shared<const PDFContentNode>(nodeData(PDFContentNodeKind::Group, 2), std::vector<PDFContentNodePointer>({ path, image }));
    const PDFContentNodePointer run = std::make_shared<const PDFContentNode>(nodeData(PDFContentNodeKind::StateRun, 3), std::vector<PDFContentNodePointer>());
    const PDFContentNodePointer root = std::make_shared<const PDFContentNode>(nodeData(PDFContentNodeKind::Root, 1), std::vector<PDFContentNodePointer>({ group, run }));

    PDFContentContainer::Data data;
    data.root = root;
    const PDFContentContainer container(data);

    QVERIFY(container.getNode(PDFContentNodeId{ 1 }) == root);
    QVERIFY(container.getNode(PDFContentNodeId{ 2 }) == group);
    QVERIFY(container.getNode(PDFContentNodeId{ 3 }) == run);
    QVERIFY(container.getNode(PDFContentNodeId{ 5 }) == path);
    QVERIFY(container.getNode(PDFContentNodeId{ 9 }) == image);
    QVERIFY(container.getNode(PDFContentNodeId{ 4 }) == nullptr);
    QVERIFY(container.getNode(PDFContentNodeId{ 10 }) == nullptr);
    QVERIFY(container.getNode(PDFContentNodeId()) == nullptr);

    QVERIFY(container.getParent(PDFContentNodeId{ 1 }) == nullptr);
    QVERIFY(container.getParent(PDFContentNodeId{ 2 }) == root);
    QVERIFY(container.getParent(PDFContentNodeId{ 3 }) == root);
    QVERIFY(container.getParent(PDFContentNodeId{ 5 }) == group);
    QVERIFY(container.getParent(PDFContentNodeId{ 9 }) == group);
    QVERIFY(container.getParent(PDFContentNodeId{ 4 }) == nullptr);

    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 1 }), PDFInteger(-1));
    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 2 }), PDFInteger(0));
    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 3 }), PDFInteger(1));
    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 5 }), PDFInteger(0));
    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 9 }), PDFInteger(1));
    QCOMPARE(container.getIndexInParent(PDFContentNodeId{ 4 }), PDFInteger(-1));

    QVERIFY(container.getMaximumNodeId() == (PDFContentNodeId{ 9 }));
    QCOMPARE(container.getNodeCount(), size_t(5));

    // A container without a root has no node
    const PDFContentContainer empty{ PDFContentContainer::Data() };
    QVERIFY(empty.getNode(PDFContentNodeId{ 1 }) == nullptr);
    QVERIFY(!empty.getMaximumNodeId().isValid());
    QCOMPARE(empty.getNodeCount(), size_t(0));
}

void ContentTreeTest::editorCreatesRoot()
{
    // pe0_skeleton_notes.md, the header of the editor: an editor of a key creates the Root node
    // itself - the identifier 1, no operations, no children
    const PDFContentContainerKey key = PDFContentContainerKey::createPage(4, PDFObjectReference(20, 0));
    PDFContentContainerEditor editor(key);

    const PDFContentNodePointer rootBefore = editor.getNode(PDFContentNodeId{ 1 });
    QVERIFY(rootBefore != nullptr);
    QVERIFY(rootBefore->getKind() == PDFContentNodeKind::Root);

    const PDFContentContainerPointer container = editor.build();
    QVERIFY(container != nullptr);
    QVERIFY(container->getKey() == key);
    QVERIFY(container->getRoot() != nullptr);
    QVERIFY(container->getRoot()->getKind() == PDFContentNodeKind::Root);
    QVERIFY(container->getRoot()->getId() == (PDFContentNodeId{ 1 }));
    QVERIFY(!container->getRoot()->getParentId().isValid());
    QVERIFY(container->getRoot()->getChildren().empty());
    QVERIFY(container->getRoot()->getPrefix().isEmpty());
    QVERIFY(container->getRoot()->getSuffix().isEmpty());
    QCOMPARE(container->getNodeCount(), size_t(1));
    QVERIFY(!container->isReadOnly());
}

void ContentTreeTest::editorInsert()
{
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
    const PDFContentNodeId root{ 1 };

    // At the end (-1), at the beginning (0), in the middle
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 10), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 11), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 12), root, 0));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Shading, 13), root, 2));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::StateRun, 14), root, 4));     // The index equal to the count is the end
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::TextObject, 15), PDFContentNodeId{ 10 }, -1));

    // Refused: an identifier in use (also the one of the root), no identifier, a root, a parent,
    // which does not exist or is a leaf, an index out of range
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 11), root, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 1), root, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 0), root, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Root, 20), root, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 21), PDFContentNodeId{ 99 }, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 22), PDFContentNodeId{ 11 }, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 23), root, 6));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 24), root, -2));

    // The parent identifier of the data is set by the call, whatever the caller wrote there
    PDFContentNode::Data wrongParent = nodeData(PDFContentNodeKind::TextShow, 16);
    wrongParent.parentId = PDFContentNodeId{ 77 };
    QVERIFY(editor.insertNode(wrongParent, PDFContentNodeId{ 15 }, -1));

    const PDFContentContainerPointer container = editor.build();
    QVERIFY(container != nullptr);

    // Root { Image 12, Group 10 { TextObject 15 { TextShow 16 } }, Shading 13, Path 11, StateRun 14 }
    QCOMPARE(describe(container), QStringLiteral("Root{Image Group{TextObject{TextShow}} Shading Path StateRun}"));
    QVERIFY(identifiers(container->getRoot()) == std::vector<quint64>({ 1, 12, 10, 15, 16, 13, 11, 14 }));
    QCOMPARE(container->getNodeCount(), size_t(8));
    QVERIFY(container->getNode(PDFContentNodeId{ 16 })->getParentId() == (PDFContentNodeId{ 15 }));
    QVERIFY(container->getNode(PDFContentNodeId{ 15 })->getParentId() == (PDFContentNodeId{ 10 }));
    QVERIFY(container->getNode(PDFContentNodeId{ 12 })->getParentId() == root);
    QVERIFY(container->getNode(PDFContentNodeId{ 20 }) == nullptr);
    QVERIFY(container->getNode(PDFContentNodeId{ 22 }) == nullptr);
}

void ContentTreeTest::editorSetNodeData()
{
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
    const PDFContentNodeId root{ 1 };
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 2), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 3), PDFContentNodeId{ 2 }, -1));

    // The data are replaced; the identifier, the parent and the children stay, whatever the
    // new data say
    PDFContentNode::Data groupData = nodeData(PDFContentNodeKind::MarkedContent, 55);
    groupData.parentId = PDFContentNodeId{ 66 };
    groupData.tag = "Span";
    groupData.isModified = true;
    QVERIFY(editor.setNodeData(PDFContentNodeId{ 2 }, groupData));

    PDFContentNode::Data pathData = nodeData(PDFContentNodeKind::Path, 3);
    pathData.boundingBox = QRectF(1.0, 2.0, 3.0, 4.0);
    pathData.override = PDFContentStateOverride();
    pathData.override->transform = QTransform::fromTranslate(5.0, 6.0);
    QVERIFY(editor.setNodeData(PDFContentNodeId{ 3 }, pathData));

    PDFContentNode::Data rootData = nodeData(PDFContentNodeKind::Root, 1);
    rootData.boundingBox = QRectF(0.0, 0.0, 10.0, 10.0);
    QVERIFY(editor.setNodeData(root, rootData));

    // Refused: an unknown node, a leaf kind for a node with children, another kind for the
    // root, the root kind for another node
    QVERIFY(!editor.setNodeData(PDFContentNodeId{ 9 }, nodeData(PDFContentNodeKind::Path, 9)));
    QVERIFY(!editor.setNodeData(PDFContentNodeId{ 2 }, nodeData(PDFContentNodeKind::Path, 2)));
    QVERIFY(!editor.setNodeData(root, nodeData(PDFContentNodeKind::Group, 1)));
    QVERIFY(!editor.setNodeData(PDFContentNodeId{ 3 }, nodeData(PDFContentNodeKind::Root, 3)));

    const PDFContentContainerPointer container = editor.build();
    QCOMPARE(describe(container), QStringLiteral("Root{MarkedContent{Path}}"));

    const PDFContentNodePointer group = container->getNode(PDFContentNodeId{ 2 });
    QVERIFY(group != nullptr);
    QVERIFY(group->getId() == (PDFContentNodeId{ 2 }));
    QVERIFY(group->getParentId() == root);
    QCOMPARE(group->getData().tag, QByteArray("Span"));
    QVERIFY(group->isModified());
    QCOMPARE(group->getChildren().size(), size_t(1));
    QVERIFY(container->getNode(PDFContentNodeId{ 55 }) == nullptr);

    const PDFContentNodePointer path = container->getNode(PDFContentNodeId{ 3 });
    QCOMPARE(path->getBoundingBox(), QRectF(1.0, 2.0, 3.0, 4.0));
    QVERIFY(path->getOverride() != nullptr);
    QCOMPARE(path->getOverride()->transform, QTransform::fromTranslate(5.0, 6.0));
    QVERIFY(path->getParentId() == (PDFContentNodeId{ 2 }));

    QCOMPARE(container->getRoot()->getBoundingBox(), QRectF(0.0, 0.0, 10.0, 10.0));
}

void ContentTreeTest::editorRemove()
{
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
    const PDFContentNodeId root{ 1 };
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 2), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 3), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::MarkedContent, 4), PDFContentNodeId{ 3 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 5), PDFContentNodeId{ 4 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Shading, 6), root, -1));

    // The root and an unknown node cannot be removed
    QVERIFY(!editor.removeNode(root));
    QVERIFY(!editor.removeNode(PDFContentNodeId{ 9 }));

    // A node goes with everything below it
    QVERIFY(editor.removeNode(PDFContentNodeId{ 3 }));
    QVERIFY(editor.getNode(PDFContentNodeId{ 3 }) == nullptr);
    QVERIFY(editor.getNode(PDFContentNodeId{ 4 }) == nullptr);
    QVERIFY(editor.getNode(PDFContentNodeId{ 5 }) == nullptr);
    QVERIFY(!editor.removeNode(PDFContentNodeId{ 4 }));     // Gone with its parent

    // The current node of the editor has its current children
    QCOMPARE(describe(editor.getNode(root)), QStringLiteral("Root{Path Shading}"));

    // An identifier of a removed node is free again
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 5), root, 1));

    const PDFContentContainerPointer container = editor.build();
    QCOMPARE(describe(container), QStringLiteral("Root{Path Image Shading}"));
    QVERIFY(identifiers(container->getRoot()) == std::vector<quint64>({ 1, 2, 5, 6 }));
    QCOMPARE(container->getNodeCount(), size_t(4));
}

void ContentTreeTest::editorMove()
{
    // Root 1 { Path 2, Group 3 { Image 4, MarkedContent 5 { Shading 6 } }, Path 7 }
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
    const PDFContentNodeId root{ 1 };
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 2), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 3), root, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 4), PDFContentNodeId{ 3 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::MarkedContent, 5), PDFContentNodeId{ 3 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Shading, 6), PDFContentNodeId{ 5 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 7), root, -1));

    // Refused: the root, an unknown node or parent, under itself, under its own subtree, under
    // a leaf, an index out of range - and nothing changes
    QVERIFY(!editor.moveNode(root, PDFContentNodeId{ 3 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 9 }, root, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 2 }, PDFContentNodeId{ 9 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 3 }, PDFContentNodeId{ 3 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 3 }, PDFContentNodeId{ 5 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 2 }, PDFContentNodeId{ 7 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 2 }, root, 3));      // Two other children: the indices 0 ... 2
    QCOMPARE(describe(editor.getNode(root)), QStringLiteral("Root{Path Group{Image MarkedContent{Shading}} Path}"));
    QVERIFY(identifiers(editor.getNode(root)) == std::vector<quint64>({ 1, 2, 3, 4, 5, 6, 7 }));

    // Inside of one parent: the index counts the children without the moved node. The node 2
    // goes to the end (the index 2), then the node 7 to the front.
    QVERIFY(editor.moveNode(PDFContentNodeId{ 2 }, root, 2));
    QVERIFY(identifiers(editor.getNode(root)) == std::vector<quint64>({ 1, 3, 4, 5, 6, 7, 2 }));
    QVERIFY(editor.moveNode(PDFContentNodeId{ 7 }, root, 0));
    QVERIFY(identifiers(editor.getNode(root)) == std::vector<quint64>({ 1, 7, 3, 4, 5, 6, 2 }));

    // To another parent, with its subtree: the marked content 5 goes out of the group to the
    // root, between the nodes 7 and 3
    QVERIFY(editor.moveNode(PDFContentNodeId{ 5 }, root, 1));
    // A leaf goes into the moved node
    QVERIFY(editor.moveNode(PDFContentNodeId{ 2 }, PDFContentNodeId{ 5 }, 0));

    const PDFContentContainerPointer container = editor.build();
    QCOMPARE(describe(container), QStringLiteral("Root{Path MarkedContent{Path Shading} Group{Image}}"));
    QVERIFY(identifiers(container->getRoot()) == std::vector<quint64>({ 1, 7, 5, 2, 6, 3, 4 }));
    QVERIFY(container->getNode(PDFContentNodeId{ 5 })->getParentId() == root);
    QVERIFY(container->getNode(PDFContentNodeId{ 2 })->getParentId() == (PDFContentNodeId{ 5 }));
    QVERIFY(container->getNode(PDFContentNodeId{ 6 })->getParentId() == (PDFContentNodeId{ 5 }));
    QVERIFY(container->getParent(PDFContentNodeId{ 2 }) == container->getNode(PDFContentNodeId{ 5 }));
    QCOMPARE(container->getIndexInParent(PDFContentNodeId{ 3 }), PDFInteger(2));
}

void ContentTreeTest::editorSharesUnchangedNodes()
{
    // Root 1 { Group 2 { Path 3, Image 4 }, Group 5 { Shading 6 } }
    PDFContentContainerPointer original;
    {
        PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
        const PDFContentNodeId root{ 1 };
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 2), root, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 3), PDFContentNodeId{ 2 }, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 4), PDFContentNodeId{ 2 }, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 5), root, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Shading, 6), PDFContentNodeId{ 5 }, -1));
        editor.getContainerData().fingerprint = "fp";
        editor.getContainerData().structuralParentKey = 3;
        original = editor.build();
    }
    QCOMPARE(describe(original), QStringLiteral("Root{Group{Path Image} Group{Shading}}"));

    // A change of the path 3: zadani_jadro.md 4.2 / notes decision 2 - a command creates new
    // nodes only on the way from the changed node to the root, the rest is shared; the old
    // container stays as it was (undo keeps it)
    PDFContentContainerEditor editor(original);
    QVERIFY(editor.getNode(PDFContentNodeId{ 3 }) == original->getNode(PDFContentNodeId{ 3 }));     // Unchanged so far: the same object

    PDFContentNode::Data pathData = original->getNode(PDFContentNodeId{ 3 })->getData();
    pathData.isModified = true;
    QVERIFY(editor.setNodeData(PDFContentNodeId{ 3 }, pathData));
    const PDFContentContainerPointer changed = editor.build();

    QVERIFY(changed != original);
    QCOMPARE(describe(changed), QStringLiteral("Root{Group{Path Image} Group{Shading}}"));
    QVERIFY(changed->getNode(PDFContentNodeId{ 3 })->isModified());
    QVERIFY(changed->getNode(PDFContentNodeId{ 3 }) != original->getNode(PDFContentNodeId{ 3 }));
    QVERIFY(changed->getNode(PDFContentNodeId{ 2 }) != original->getNode(PDFContentNodeId{ 2 }));
    QVERIFY(changed->getRoot() != original->getRoot());
    QVERIFY(changed->getNode(PDFContentNodeId{ 4 }) == original->getNode(PDFContentNodeId{ 4 }));  // The sibling
    QVERIFY(changed->getNode(PDFContentNodeId{ 5 }) == original->getNode(PDFContentNodeId{ 5 }));  // The other branch
    QVERIFY(changed->getNode(PDFContentNodeId{ 6 }) == original->getNode(PDFContentNodeId{ 6 }));

    // The data of the container go on
    QVERIFY(changed->getKey() == original->getKey());
    QCOMPARE(changed->getFingerprint(), QByteArray("fp"));
    QCOMPARE(changed->getStructuralParentKey(), PDFInteger(3));

    // The old container is untouched
    QVERIFY(!original->getNode(PDFContentNodeId{ 3 })->isModified());
    QCOMPARE(original->getNodeCount(), size_t(6));

    // A removal and a move: only the parents concerned are new
    PDFContentContainerEditor secondEditor(original);
    QVERIFY(secondEditor.moveNode(PDFContentNodeId{ 4 }, PDFContentNodeId{ 5 }, 0));
    const PDFContentContainerPointer moved = secondEditor.build();
    QCOMPARE(describe(moved), QStringLiteral("Root{Group{Path} Group{Image Shading}}"));
    QVERIFY(moved->getNode(PDFContentNodeId{ 3 }) == original->getNode(PDFContentNodeId{ 3 }));
    QVERIFY(moved->getNode(PDFContentNodeId{ 6 }) == original->getNode(PDFContentNodeId{ 6 }));
    QVERIFY(moved->getNode(PDFContentNodeId{ 4 }) != original->getNode(PDFContentNodeId{ 4 }));    // Its parent identifier is another one
    QVERIFY(moved->getNode(PDFContentNodeId{ 4 })->getParentId() == (PDFContentNodeId{ 5 }));
    QCOMPARE(describe(original), QStringLiteral("Root{Group{Path Image} Group{Shading}}"));

    // An editor, which changes nothing, gives the same nodes
    PDFContentContainerEditor idleEditor(original);
    const PDFContentContainerPointer same = idleEditor.build();
    QVERIFY(same->getRoot() == original->getRoot());
}

void ContentTreeTest::editorDepthLimit()
{
    // zadani_overeni.md 6.2: the depth of the tree is 256 - 256 nodes with children below the
    // root and their leaves, so the deepest node is 257 levels below the root
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));

    quint64 parent = 1;
    for (int level = 1; level <= PDFContentContainer::MAXIMUM_DEPTH; ++level)
    {
        const quint64 id = quint64(level) + 1;
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, id), PDFContentNodeId{ parent }, -1));
        parent = id;
    }

    // The leaf of the level 257 is the last one allowed; nothing goes below a node of that level
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 1000), PDFContentNodeId{ parent }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 1001), PDFContentNodeId{ parent }, -1));
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 1002), PDFContentNodeId{ 1001 }, -1));

    // A move, which would put a node below the limit, is refused: the group 2000 with a child
    // is two levels high, under the node of the level 255 its child is at the level 257 (allowed),
    // under the node of the level 256 at the level 258
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 2000), PDFContentNodeId{ 1 }, -1));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 2001), PDFContentNodeId{ 2000 }, -1));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 2000 }, PDFContentNodeId{ 257 }, -1));
    QVERIFY(editor.moveNode(PDFContentNodeId{ 2000 }, PDFContentNodeId{ 256 }, -1));

    const PDFContentContainerPointer container = editor.build();
    QVERIFY(container->getNode(PDFContentNodeId{ 1000 }) != nullptr);
    QVERIFY(container->getNode(PDFContentNodeId{ 1002 }) == nullptr);
    QVERIFY(container->getNode(PDFContentNodeId{ 2001 })->getParentId() == (PDFContentNodeId{ 2000 }));
    QVERIFY(container->getNode(PDFContentNodeId{ 2000 })->getParentId() == (PDFContentNodeId{ 256 }));
    QCOMPARE(container->getNodeCount(), size_t(1 + 256 + 2 + 2));
}

void ContentTreeTest::editorBuildsOnce()
{
    PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 2), PDFContentNodeId{ 1 }, -1));

    const PDFContentContainerPointer container = editor.build();
    QVERIFY(container != nullptr);

    // The editor must not be used afterwards: it refuses everything
    QVERIFY(editor.build() == nullptr);
    QVERIFY(editor.getNode(PDFContentNodeId{ 1 }) == nullptr);
    QVERIFY(!editor.insertNode(nodeData(PDFContentNodeKind::Path, 3), PDFContentNodeId{ 1 }, -1));
    QVERIFY(!editor.setNodeData(PDFContentNodeId{ 2 }, nodeData(PDFContentNodeKind::Path, 2)));
    QVERIFY(!editor.removeNode(PDFContentNodeId{ 2 }));
    QVERIFY(!editor.moveNode(PDFContentNodeId{ 2 }, PDFContentNodeId{ 1 }, 0));
    QCOMPARE(describe(container), QStringLiteral("Root{Path}"));
}

void ContentTreeTest::editorNodeCache()
{
    // PDFContentContainerEditor::getNode: the node of a place, which has not changed since the
    // last call, is the same object again; a change of a node or of something below it gives
    // a new one, and only there (pdfcontentnode.h). A node returned earlier never changes.
    //
    // Root 1 { Group 2 { Path 3, Image 4 }, Group 5 { Shading 6 } }
    PDFContentContainerPointer original;
    {
        PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
        const PDFContentNodeId root{ 1 };
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 2), root, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 3), PDFContentNodeId{ 2 }, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Image, 4), PDFContentNodeId{ 2 }, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, 5), root, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Shading, 6), PDFContentNodeId{ 5 }, -1));
        original = editor.build();
    }

    const PDFContentNodeId rootId{ 1 };
    const PDFContentNodeId firstGroupId{ 2 };
    const PDFContentNodeId pathId{ 3 };
    const PDFContentNodeId imageId{ 4 };
    const PDFContentNodeId secondGroupId{ 5 };

    PDFContentContainerEditor editor(original);

    // The path is changed: its group and the root are new nodes, made once
    PDFContentNode::Data pathData = original->getNode(pathId)->getData();
    pathData.isModified = true;
    QVERIFY(editor.setNodeData(pathId, pathData));

    const PDFContentNodePointer firstGroup = editor.getNode(firstGroupId);
    QVERIFY(firstGroup != original->getNode(firstGroupId));
    QVERIFY(firstGroup == editor.getNode(firstGroupId));
    QVERIFY(firstGroup->getChildren()[0]->isModified());
    QVERIFY(firstGroup->getChildren()[0] == editor.getNode(pathId));
    QVERIFY(firstGroup->getChildren()[1] == original->getNode(imageId));

    const PDFContentNodePointer firstRoot = editor.getNode(rootId);
    QVERIFY(firstRoot == editor.getNode(rootId));
    QVERIFY(firstRoot->getChildren()[0] == firstGroup);                             // The node made for the group before
    QVERIFY(firstRoot->getChildren()[1] == original->getNode(secondGroupId));     // An unchanged branch

    // A change below the group: the group and the root are made again, the path is not
    PDFContentNode::Data imageData = original->getNode(imageId)->getData();
    imageData.isModified = true;
    QVERIFY(editor.setNodeData(imageId, imageData));

    const PDFContentNodePointer secondGroup = editor.getNode(firstGroupId);
    QVERIFY(secondGroup != firstGroup);
    QVERIFY(secondGroup->getChildren()[0] == firstGroup->getChildren()[0]);
    QVERIFY(secondGroup->getChildren()[1]->isModified());
    QVERIFY(editor.getNode(rootId) != firstRoot);
    QVERIFY(editor.getNode(rootId)->getChildren()[0] == secondGroup);

    // The nodes returned before are as they were
    QVERIFY(!firstGroup->getChildren()[1]->isModified());
    QVERIFY(firstRoot->getChildren()[0] == firstGroup);

    // A change of the other branch: the first group stays the same object, the root does not
    const PDFContentNodePointer secondRoot = editor.getNode(rootId);
    QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 7), secondGroupId, -1));
    QVERIFY(editor.getNode(firstGroupId) == secondGroup);
    QVERIFY(editor.getNode(rootId) != secondRoot);
    QCOMPARE(describe(editor.getNode(rootId)), QStringLiteral("Root{Group{Path Image} Group{Shading Path}}"));
    QCOMPARE(describe(secondRoot), QStringLiteral("Root{Group{Path Image} Group{Shading}}"));

    // A move: the moved node is a new object (its parent is a part of it), both parents and
    // the root are made again
    const PDFContentNodePointer thirdRoot = editor.getNode(rootId);
    const PDFContentNodePointer movedBefore = editor.getNode(pathId);
    QVERIFY(editor.moveNode(pathId, secondGroupId, 0));
    QVERIFY(editor.getNode(pathId) != movedBefore);
    QVERIFY(editor.getNode(pathId)->getParentId() == secondGroupId);
    QVERIFY(movedBefore->getParentId() == firstGroupId);
    QCOMPARE(describe(editor.getNode(rootId)), QStringLiteral("Root{Group{Image} Group{Path Shading Path}}"));
    QCOMPARE(describe(editor.getNode(firstGroupId)), QStringLiteral("Group{Image}"));
    QCOMPARE(describe(thirdRoot), QStringLiteral("Root{Group{Path Image} Group{Shading Path}}"));

    // A removal
    QVERIFY(editor.removeNode(imageId));
    QVERIFY(editor.getNode(imageId) == nullptr);
    QCOMPARE(describe(editor.getNode(firstGroupId)), QStringLiteral("Group"));
    QCOMPARE(describe(editor.getNode(rootId)), QStringLiteral("Root{Group Group{Path Shading Path}}"));

    // The container is the tree the editor showed last
    const PDFContentContainerPointer changed = editor.build();
    QCOMPARE(describe(changed), QStringLiteral("Root{Group Group{Path Shading Path}}"));
    QVERIFY(changed->getNode(pathId)->isModified());
    QVERIFY(changed->getNode(pathId)->getParentId() == secondGroupId);
    QCOMPARE(describe(original), QStringLiteral("Root{Group{Path Image} Group{Shading}}"));
}

void ContentTreeTest::editorManyQueries()
{
    // A query of the editor costs the node, not the whole changed tree (decision 5 of the fixes
    // of stage E0; the commands ask for a node and walk up to the root for every node they
    // handle). 200 groups of 100 paths: 20 201 nodes; every tenth path is changed (2 000 nodes,
    // ten in every group), then every node is asked for with its way up to the root.
    //
    // With a getNode, which makes the whole changed subtree again at every call, one way up
    // makes the root again - 200 groups and 2 000 paths with a copy of their data -, so the
    // 20 201 ways make about 4 x 10^7 nodes: far over the limit. Made once, they are 2 201.
    constexpr quint64 GROUP_COUNT = 200;
    constexpr quint64 LEAF_COUNT = 100;
    const PDFContentNodeId rootId{ 1 };

    auto groupId = [](quint64 group) { return PDFContentNodeId{ 2 + group * (LEAF_COUNT + 1) }; };
    auto leafId = [&groupId](quint64 group, quint64 leaf) { return PDFContentNodeId{ groupId(group).value + 1 + leaf }; };

    PDFContentContainerPointer original;
    {
        PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
        for (quint64 group = 0; group < GROUP_COUNT; ++group)
        {
            QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Group, groupId(group).value), rootId, -1));
            for (quint64 leaf = 0; leaf < LEAF_COUNT; ++leaf)
            {
                PDFContentNode::Data data = nodeData(PDFContentNodeKind::Path, leafId(group, leaf).value);
                data.path.addRectangle(QRectF(qreal(leaf), qreal(group), 1.0, 1.0));
                QVERIFY(editor.insertNode(std::move(data), groupId(group), -1));
            }
        }
        original = editor.build();
    }
    QCOMPARE(original->getNodeCount(), size_t(1 + GROUP_COUNT * (LEAF_COUNT + 1)));

    QElapsedTimer timer;
    timer.start();
    const qint64 limit = timeLimit(5000);

    PDFContentContainerEditor editor(original);
    size_t changedCount = 0;
    for (quint64 group = 0; group < GROUP_COUNT; ++group)
    {
        for (quint64 leaf = 0; leaf < LEAF_COUNT; leaf += 10)
        {
            PDFContentNode::Data data = original->getNode(leafId(group, leaf))->getData();
            data.isModified = true;
            QVERIFY(editor.setNodeData(leafId(group, leaf), std::move(data)));
            ++changedCount;
        }
    }
    QCOMPARE(changedCount, size_t(2000));

    // Every node with its way up to the root
    size_t queryCount = 0;
    size_t modifiedCount = 0;
    for (quint64 id = 1; id <= original->getMaximumNodeId().value; ++id)
    {
        PDFContentNodePointer node = editor.getNode(PDFContentNodeId{ id });
        QVERIFY(node != nullptr);
        modifiedCount += node->isModified() ? 1 : 0;

        int depth = 0;
        while (node->getParentId().isValid())
        {
            node = editor.getNode(node->getParentId());
            QVERIFY(node != nullptr);
            ++depth;
        }
        QVERIFY(node->getId() == rootId);
        QVERIFY(depth <= 2);

        if (++queryCount % 64 == 0 && timer.elapsed() > limit)
        {
            QFAIL(qPrintable(QStringLiteral("Only %1 of %2 nodes were asked for within %3 ms").arg(queryCount).arg(original->getNodeCount()).arg(limit)));
        }
    }
    QCOMPARE(queryCount, original->getNodeCount());
    QCOMPARE(modifiedCount, size_t(2000));

    // The same tree as the container gives: the changed paths at their places
    const PDFContentNodePointer root = editor.getNode(rootId);
    QCOMPARE(root->getChildren().size(), size_t(GROUP_COUNT));
    QVERIFY(root->getChildren()[7]->getChildren()[30]->isModified());
    QVERIFY(!root->getChildren()[7]->getChildren()[31]->isModified());
    QVERIFY(root->getChildren()[7]->getChildren()[31] == original->getNode(leafId(7, 31)));

    const PDFContentContainerPointer changed = editor.build();
    QVERIFY(changed->getNode(leafId(199, 90))->isModified());
    QVERIFY(changed->getNode(leafId(199, 91)) == original->getNode(leafId(199, 91)));
    QVERIFY2(timer.elapsed() <= limit, qPrintable(QString::number(timer.elapsed())));
}

void ContentTreeTest::containerIsUnchanged()
{
    // q0 w1 re2 f3 Q4 re5 f6: Root{Group(0:1/4:1){StateRun(1:1) Path(2:2)} Path(5:2)}
    const PDFDocument document = createDocument("q 2 w 0 0 5 5 re f Q 10 10 5 5 re f");
    const PDFContentContainerPointer original = buildPage(document).container;
    QVERIFY(original != nullptr);
    QCOMPARE(describe(original), QStringLiteral("Root{Group(0:1/4:1){StateRun(1:1) Path(2:2)} Path(5:2)}"));

    // The container of the builder is the content of the document: it is never written
    QVERIFY(original->isUnchanged());

    const PDFContentNodeId group = original->getRoot()->getChildren()[0]->getId();
    const PDFContentNodeId innerPath = original->getRoot()->getChildren()[0]->getChildren()[1]->getId();
    const PDFContentNodeId outerPath = original->getRoot()->getChildren()[1]->getId();

    // An editor, which changes nothing
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.build()->isUnchanged());
    }

    // A modified node
    {
        PDFContentContainerEditor editor(original);
        PDFContentNode::Data data = original->getNode(innerPath)->getData();
        data.isModified = true;
        QVERIFY(editor.setNodeData(innerPath, data));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // An override, also an empty one (R-2: a wrap without a change is still a wrap)
    {
        PDFContentContainerEditor editor(original);
        PDFContentNode::Data data = original->getNode(outerPath)->getData();
        data.override = PDFContentStateOverride();
        QVERIFY(editor.setNodeData(outerPath, data));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // A moved node: the same operations in another order
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.moveNode(outerPath, PDFContentNodeId{ 1 }, 0));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // A node moved to another parent
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.moveNode(outerPath, group, -1));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // A removed node: its operations are missing
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.removeNode(outerPath));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // A suppressed operation
    {
        PDFContentContainerEditor editor(original);
        PDFContentOperation::Flags flags = editor.getOperations().getOperation(1).getFlags();
        flags.setFlag(PDFContentOperation::Suppressed);
        editor.getOperations().setFlags(1, flags);
        QVERIFY(!editor.build()->isUnchanged());
    }

    // A node of a generated operation
    {
        PDFContentContainerEditor editor(original);
        const size_t index = editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::SetLineWidth, "3 w", PDFContentOperation::Operands(), { }));
        PDFContentNode::Data data = nodeData(PDFContentNodeKind::StateRun, 100);
        data.prefix = PDFContentOperationSpan{ index, 1 };
        QVERIFY(editor.insertNode(data, PDFContentNodeId{ 1 }, -1));
        QVERIFY(!editor.build()->isUnchanged());
    }

    // An added resource
    {
        PDFContentContainerEditor editor(original);
        const QByteArray name = editor.getResources().addObject(PDFContentResourceCategory::ExtGState, "GS", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(9) } }));
        QVERIFY(!name.isEmpty());
        QVERIFY(!editor.build()->isUnchanged());
    }

    QVERIFY(original->isUnchanged());

    // Two leaves of the root in the other order (no closing operation follows them): re0 f1 re2 f3
    {
        const PDFDocument leavesDocument = createDocument("0 0 5 5 re f 10 10 5 5 re f");
        const PDFContentContainerPointer leaves = buildPage(leavesDocument).container;
        QCOMPARE(describe(leaves), QStringLiteral("Root{Path(0:2) Path(2:2)}"));
        QVERIFY(leaves->isUnchanged());

        PDFContentContainerEditor editor(leaves);
        QVERIFY(editor.moveNode(leaves->getRoot()->getChildren()[1]->getId(), PDFContentNodeId{ 1 }, 0));
        const PDFContentContainerPointer swapped = editor.build();
        QCOMPARE(describe(swapped), QStringLiteral("Root{Path(2:2) Path(0:2)}"));
        QVERIFY(!swapped->isUnchanged());
    }
}

// ---------------------------------------------------------------------------------------------
//  Builder: structure
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::operatorNodes_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");

    // zadani_jadro.md 4.2, the table of the kinds; the operators by ISO 32000-2, Annex A

    // General and special graphics state: w J j M d ri i gs cm are one state run
    QTest::newRow("graphics state") << QByteArray("1 w 2 J 0 j 4 M [1 2] 0 d /Perceptual ri 1 i /GS1 gs 1 0 0 1 5 5 cm")
                                    << QStringLiteral("Root{StateRun(0:9)}");

    // Colors: cs sc CS SC g G rg RG k K cs scn CS SCN
    QTest::newRow("colors") << QByteArray("/Cs1 cs 0.1 0.2 0.3 sc /Cs1 CS 0.1 0.2 0.3 SC 0.5 g 0.5 G 1 0 0 rg 1 0 0 RG 0 0 0 1 k 0 0 0 1 K /Pattern cs /P1 scn /Pattern CS /P1 SCN")
                            << QStringLiteral("Root{StateRun(0:14)}");

    // The text state operators can stand outside of a text object (9.3.1)
    QTest::newRow("text state outside") << QByteArray("3 Tc 4 Tw 90 Tz 12 TL /F1 10 Tf 1 Tr 2 Ts")
                                        << QStringLiteral("Root{StateRun(0:7)}");

    // m0 l1 c2 v3 y4 h5 S6: one path up to its painting operator
    QTest::newRow("path") << QByteArray("0 0 m 10 10 l 20 0 30 10 40 0 c 5 5 6 6 v 7 7 8 8 y h S")
                          << QStringLiteral("Root{Path(0:7)}");

    // re0 W1 n2: the clipping operator belongs to the path
    QTest::newRow("clipping path") << QByteArray("0 0 10 10 re W n")
                                   << QStringLiteral("Root{Path(0:3)}");
    QTest::newRow("two paths") << QByteArray("0 0 10 10 re f 20 20 m 30 30 l S")
                               << QStringLiteral("Root{Path(0:2) Path(2:3)}");
    QTest::newRow("painting operator alone") << QByteArray("n f")
                                             << QStringLiteral("Root{Path(0:1) Path(1:1)}");

    // BT0 Tf1 Td2 TD3 Tm4 T*5 Tj6 TJ7 '8 "9 ET10: the state and the positioning operators are
    // state runs of the text object, each show operator is a leaf
    QTest::newRow("text") << QByteArray("BT /F1 12 Tf 10 20 Td 5 6 TD 1 0 0 1 7 8 Tm T* (A) Tj [(A) 10 (B)] TJ (A) ' 1 2 (B) \" ET")
                          << QStringLiteral("Root{TextObject(0:1/10:1){StateRun(1:5) TextShow(6:1) TextShow(7:1) TextShow(8:1) TextShow(9:1)}}");

    // BT0 Tf1 rg2 Tj3 w4 Tj5 ET6: colors and general state are allowed inside of a text object
    QTest::newRow("state inside text") << QByteArray("BT /F1 12 Tf 1 0 0 rg (A) Tj 2 w (B) Tj ET")
                                       << QStringLiteral("Root{TextObject(0:1/6:1){StateRun(1:2) TextShow(3:1) StateRun(4:1) TextShow(5:1)}}");

    // BT0 Tf1 BMC2 Tj3 EMC4 MP5 ET6: marked content nested in a text object (14.6.1)
    QTest::newRow("marked content in text") << QByteArray("BT /F1 10 Tf /T BMC (A) Tj EMC /T MP ET")
                                            << QStringLiteral("Root{TextObject(0:1/6:1){StateRun(1:1) MarkedContent(2:1/4:1){TextShow(3:1)} MarkedPoint(5:1)}}");

    // BMC0 BT1 Tf2 Tj3 ET4 EMC5: a text object nested in marked content
    QTest::newRow("text in marked content") << QByteArray("/T BMC BT /F1 10 Tf (A) Tj ET EMC")
                                            << QStringLiteral("Root{MarkedContent(0:1/5:1){TextObject(1:1/4:1){StateRun(2:1) TextShow(3:1)}}}");

    // Do of an image, of a form, sh
    QTest::newRow("xobjects and shading") << QByteArray("/Im1 Do /Fm1 Do /Sh1 sh")
                                          << QStringLiteral("Root{Image(0:1) FormUse(1:1) Shading(2:1)}");

    // MP0 DP1 BMC2 w3 EMC4 BDC5 EMC6
    QTest::newRow("marked content") << QByteArray("/Tag MP /Tag /MC2 DP /Tag BMC 1 w EMC /Tag /MC1 BDC EMC")
                                    << QStringLiteral("Root{MarkedPoint(0:1) MarkedPoint(1:1) MarkedContent(2:1/4:1){StateRun(3:1)} MarkedContent(5:1/6:1)}");

    // An inline image is one operation and one node
    QTest::newRow("inline image") << QByteArray("BI /W 1 /H 1 /BPC 8 /CS /G ID x\nEI")
                                  << QStringLiteral("Root{Image(0:1)}");

    // BX and EX stay where they are: a part of a state run
    QTest::newRow("compatibility") << QByteArray("BX 1 w EX")
                                   << QStringLiteral("Root{StateRun(0:3)}");

    // q0 w1 Q2
    QTest::newRow("group") << QByteArray("q 1 w Q")
                           << QStringLiteral("Root{Group(0:1/2:1){StateRun(1:1)}}");

    // A state run ends at every object and at every bracket: w0 re1 f2 w3 q4 w5 Q6 w7
    QTest::newRow("state runs") << QByteArray("1 w 0 0 5 5 re f 2 w q 3 w Q 4 w")
                                << QStringLiteral("Root{StateRun(0:1) Path(1:2) StateRun(3:1) Group(4:1/6:1){StateRun(5:1)} StateRun(7:1)}");

    // What the tree cannot express is opaque and stays, where it is: the operators d0 and d1 of
    // a glyph procedure in a page, an XObject, which is neither an image nor a form
    QTest::newRow("glyph operator in a page") << QByteArray("1 w 10 0 d0 2 w")
                                              << QStringLiteral("Root{StateRun(0:1) Opaque(1:1) StateRun(2:1)}");
    QTest::newRow("glyph operator with a box in a page") << QByteArray("10 0 0 0 5 5 d1")
                                                         << QStringLiteral("Root{Opaque(0:1)}");
    QTest::newRow("xobject of another type") << QByteArray("/PS1 Do")
                                             << QStringLiteral("Root{Opaque(0:1)}");

    QTest::newRow("empty") << QByteArray() << QStringLiteral("Root");
    QTest::newRow("white space") << QByteArray(" \n% nothing\n") << QStringLiteral("Root");
}

void ContentTreeTest::operatorNodes()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QVERIFY(!result.isCanceled);
    QCOMPARE(describe(result.container), tree);

    // The operations are the content, byte for byte
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(!result.container->isReadOnly());
    QVERIFY(result.container->getKey() == pageKey(document));
}

void ContentTreeTest::saveChain_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");
    QTest::addColumn<QList<int>>("saveDepths");     // Of the groups, in the order of a depth first walk
    QTest::addColumn<QList<bool>>("unclosed");      // PDFContentContainer::isUnclosed of the same groups

    // zadani_jadro.md 4.2: a chain q q q ... Q Q Q without operations between is one node

    // q0 q1 q2 w3 Q4 Q5 Q6
    QTest::newRow("chain") << QByteArray("q q q 1 w Q Q Q")
                           << QStringLiteral("Root{Group(0:3/4:3){StateRun(3:1)}}") << QList<int>({ 3 }) << QList<bool>({ false });
    QTest::newRow("empty chain") << QByteArray("q q Q Q")
                                 << QStringLiteral("Root{Group(0:2/2:2)}") << QList<int>({ 2 }) << QList<bool>({ false });

    // q0 q1 w2 Q3 w4 Q5: an operation between the two Q - the inner q is a group of its own
    QTest::newRow("operation between the ends") << QByteArray("q q 1 w Q 2 w Q")
                                                << QStringLiteral("Root{Group(0:1/5:1){Group(1:1/3:1){StateRun(2:1)} StateRun(4:1)}}") << QList<int>({ 1, 1 }) << QList<bool>({ false, false });

    // q0 w1 q2 w3 Q4 Q5: an operation between the two q
    QTest::newRow("operation between the beginnings") << QByteArray("q 1 w q 2 w Q Q")
                                                      << QStringLiteral("Root{Group(0:1/5:1){StateRun(1:1) Group(2:1/4:1){StateRun(3:1)}}}") << QList<int>({ 1, 1 }) << QList<bool>({ false, false });

    // q0 q1 q2 w3 Q4 Q5 w6 Q7: two of the three q end in a row - they are one inner group
    QTest::newRow("a part of the chain") << QByteArray("q q q 1 w Q Q 2 w Q")
                                         << QStringLiteral("Root{Group(0:1/7:1){Group(1:2/4:2){StateRun(3:1)} StateRun(6:1)}}") << QList<int>({ 1, 2 }) << QList<bool>({ false, false });

    // q0 q1 w2 Q3 q4 Q5 Q6: another group after the first Q
    QTest::newRow("group after a part") << QByteArray("q q 1 w Q q Q Q")
                                        << QStringLiteral("Root{Group(0:1/6:1){Group(1:1/3:1){StateRun(2:1)} Group(4:1/5:1)}}") << QList<int>({ 1, 1, 1 }) << QList<bool>({ false, false, false });

    // The end is missing in the content: no closing operations, the group is unclosed (4.4, R-7:
    // a missing Q is not added, until the node is changed - the test unclosedNodes shows, when
    // the model adds it)
    QTest::newRow("no end") << QByteArray("q 1 w")
                            << QStringLiteral("Root{Group(0:1){StateRun(1:1)}}") << QList<int>({ 1 }) << QList<bool>({ true });

    // q0 q1 w2 Q3 and the content ends: the inner q is closed, the outer one is not
    QTest::newRow("a part of the ends") << QByteArray("q q 1 w Q")
                                        << QStringLiteral("Root{Group(0:1){Group(1:1/3:1){StateRun(2:1)}}}") << QList<int>({ 1, 1 }) << QList<bool>({ true, false });

    // q0 q1 q2 w3 and the content ends: none of the three q of the chain is closed
    QTest::newRow("a chain without an end") << QByteArray("q q q 1 w")
                                            << QStringLiteral("Root{Group(0:3){StateRun(3:1)}}") << QList<int>({ 3 }) << QList<bool>({ true });
}

void ContentTreeTest::saveChain()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);
    QFETCH(QList<int>, saveDepths);
    QFETCH(QList<bool>, unclosed);

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), tree);

    QList<int> depths;
    QList<bool> unclosedGroups;
    for (const quint64 id : identifiers(result.container->getRoot()))
    {
        const PDFContentNodePointer node = result.container->getNode(PDFContentNodeId{ id });
        if (node->getKind() == PDFContentNodeKind::Group)
        {
            depths << node->getData().saveDepth;
            unclosedGroups << result.container->isUnclosed(node->getId());

            // Nothing leaks from a group: Q brings the state before q back (ISO 32000-2, 8.4.2).
            // It holds for a group without its Q too: "behind" such a group is behind its
            // missing Q, which the model writes, before anything is written there
            // (unclosedNodes), so the state behind the group is the state before its q.
            QVERIFY(node->getLeak().isEmpty());
        }
        else
        {
            // Only a node with children can be unclosed
            QVERIFY(!result.container->isUnclosed(node->getId()));
        }
    }
    QCOMPARE(depths, saveDepths);
    QCOMPARE(unclosedGroups, unclosed);
    QVERIFY(result.issues.empty());

    // The content is written back byte for byte, whatever it does not close
    QVERIFY(result.container->isUnchanged());
    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content, content);
}

void ContentTreeTest::pathPainting_data()
{
    QTest::addColumn<QByteArray>("operators");
    QTest::addColumn<bool>("stroke");
    QTest::addColumn<bool>("fill");
    QTest::addColumn<bool>("fillEvenOdd");
    QTest::addColumn<bool>("closeBeforePaint");
    QTest::addColumn<int>("clip");      // PDFContentPathPaint::Clip

    const int none = int(PDFContentPathPaint::Clip::None);
    const int nonZero = int(PDFContentPathPaint::Clip::NonZero);
    const int evenOdd = int(PDFContentPathPaint::Clip::EvenOdd);

    // ISO 32000-2, 8.5.3.1, Table 59 (path-painting operators) and 8.5.4, Table 60 (W, W*)
    QTest::newRow("S") << QByteArray("S") << true << false << false << false << none;
    QTest::newRow("s") << QByteArray("s") << true << false << false << true << none;
    QTest::newRow("f") << QByteArray("f") << false << true << false << false << none;
    QTest::newRow("F") << QByteArray("F") << false << true << false << false << none;
    QTest::newRow("f_star") << QByteArray("f*") << false << true << true << false << none;
    QTest::newRow("B") << QByteArray("B") << true << true << false << false << none;
    QTest::newRow("B_star") << QByteArray("B*") << true << true << true << false << none;
    QTest::newRow("b") << QByteArray("b") << true << true << false << true << none;
    QTest::newRow("b_star") << QByteArray("b*") << true << true << true << true << none;
    QTest::newRow("n") << QByteArray("n") << false << false << false << false << none;
    QTest::newRow("W n") << QByteArray("W n") << false << false << false << false << nonZero;
    QTest::newRow("W_star n") << QByteArray("W* n") << false << false << false << false << evenOdd;
    QTest::newRow("W f") << QByteArray("W f") << false << true << false << false << nonZero;
    QTest::newRow("W_star B") << QByteArray("W* B") << true << true << false << false << evenOdd;
    QTest::newRow("W S") << QByteArray("W S") << true << false << false << false << nonZero;
}

void ContentTreeTest::pathPainting()
{
    QFETCH(QByteArray, operators);
    QFETCH(bool, stroke);
    QFETCH(bool, fill);
    QFETCH(bool, fillEvenOdd);
    QFETCH(bool, closeBeforePaint);
    QFETCH(int, clip);

    // The path, then a second one: what the first one leaves, is the state of the second one
    const PDFDocument document = createDocument("10 20 30 40 re " + operators + " 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);

    const PDFContentNodePointer path = nodeAt(result.container, { 0 });
    QVERIFY(path != nullptr);
    QVERIFY(path->getKind() == PDFContentNodeKind::Path);
    QCOMPARE(result.container->getRoot()->getChildren().size(), size_t(2));

    const PDFContentPathPaint& paint = path->getData().paint;
    QCOMPARE(paint.stroke, stroke);
    QCOMPARE(paint.fill, fill);
    QCOMPARE(paint.fillEvenOdd, fillEvenOdd);
    QCOMPARE(paint.closeBeforePaint, closeBeforePaint);
    QCOMPARE(int(paint.clip), clip);

    // zadani_jadro.md 4.2: only "n" with a clip is a clipping path; a path with W has a clip
    // effect (it is never wrapped, R-1) and changes the clip for the nodes after it
    const bool hasClip = clip != int(PDFContentPathPaint::Clip::None);
    QCOMPARE(paint.isClippingPath(), hasClip && !stroke && !fill);
    QCOMPARE(path->hasClipEffect(), hasClip);
    QCOMPARE(path->getLeak().isClipChanged, hasClip);
    QVERIFY(path->getLeak().properties.isEmpty());
    QVERIFY(!path->getLeak().isTransformChanged);
    QVERIFY(!path->getLeak().isUnknown);
    QCOMPARE(path->getLeak().isEmpty(), !hasClip);

    QRectF rectangle;
    QVERIFY(path->getData().path.isRectangle(&rectangle));
    QCOMPARE(rectangle, QRectF(10.0, 20.0, 30.0, 40.0));
    QVERIFY(!path->isModified());
    QVERIFY(path->getOverride() == nullptr);
}

void ContentTreeTest::pathGeometry()
{
    using Type = PDFContentPath::SegmentType;

    // ISO 32000-2, 8.5.2.1, Table 58: v takes the current point as the first control point, y
    // the end point as the second one; h closes the subpath (the current point is its start)
    const PDFDocument document = createDocument("10 20 m 30 20 l 1 2 3 4 5 6 c 7 8 9 10 v 11 12 13 14 y h 50 60 -20 10 re S");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Path(0:8)}"));
    QVERIFY(result.issues.empty());

    const PDFContentNodePointer node = nodeAt(result.container, { 0 });
    const std::vector<PDFContentPath::Segment>& segments = node->getData().path.getSegments();
    QCOMPARE(segments.size(), size_t(11));

    QVERIFY(segments[0].type == Type::MoveTo);
    QCOMPARE(segments[0].points[0], QPointF(10.0, 20.0));
    QVERIFY(segments[1].type == Type::LineTo);
    QCOMPARE(segments[1].points[0], QPointF(30.0, 20.0));
    QVERIFY(segments[2].type == Type::CurveTo);         // c
    QCOMPARE(segments[2].points[0], QPointF(1.0, 2.0));
    QCOMPARE(segments[2].points[1], QPointF(3.0, 4.0));
    QCOMPARE(segments[2].points[2], QPointF(5.0, 6.0));
    QVERIFY(segments[3].type == Type::CurveTo);         // v: from (5, 6)
    QCOMPARE(segments[3].points[0], QPointF(5.0, 6.0));
    QCOMPARE(segments[3].points[1], QPointF(7.0, 8.0));
    QCOMPARE(segments[3].points[2], QPointF(9.0, 10.0));
    QVERIFY(segments[4].type == Type::CurveTo);         // y: to (13, 14)
    QCOMPARE(segments[4].points[0], QPointF(11.0, 12.0));
    QCOMPARE(segments[4].points[1], QPointF(13.0, 14.0));
    QCOMPARE(segments[4].points[2], QPointF(13.0, 14.0));
    QVERIFY(segments[5].type == Type::Close);           // h
    QVERIFY(segments[6].type == Type::MoveTo);          // re: 50 60 -20 10
    QCOMPARE(segments[6].points[0], QPointF(50.0, 60.0));
    QCOMPARE(segments[7].points[0], QPointF(30.0, 60.0));
    QCOMPARE(segments[8].points[0], QPointF(30.0, 70.0));
    QCOMPARE(segments[9].points[0], QPointF(50.0, 70.0));
    QVERIFY(segments[10].type == Type::Close);

    // A path of more than one rectangle is not written as "re"
    QVERIFY(!node->getData().path.isRectangle());
    QVERIFY(node->getData().paint.stroke);

    // The box in the space of the page (the matrix is the identity): x 1 ... 50, y 2 ... 70
    QCOMPARE(node->getBoundingBox(), QRectF(1.0, 2.0, 49.0, 68.0));
}

void ContentTreeTest::opaqueRules_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");
    QTest::addColumn<IssueKinds>("issues");

    using Kind = PDFContentIssue::Kind;

    // zadani_jadro.md 4.2, "what is opaque"

    // BMC0 q1 EMC2 Q3: the brackets cross each other - the smallest span, which covers both
    QTest::newRow("BDC q EMC Q") << QByteArray("/T BMC q EMC Q")
                                 << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::CrossedBrackets });
    QTest::newRow("q BDC Q EMC") << QByteArray("q /T BMC Q EMC")
                                 << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::CrossedBrackets });

    // The neighbors are not touched: w0 BMC1 q2 EMC3 Q4 w5
    QTest::newRow("neighbors of a crossing") << QByteArray("1 w /T BMC q EMC Q 2 w")
                                             << QStringLiteral("Root{StateRun(0:1) Opaque(1:4) StateRun(5:1)}") << kinds({ Kind::CrossedBrackets });

    // q0 w1 BMC2 q3 EMC4 Q5 Q6: the crossing is inside of a group, which stays a group
    QTest::newRow("crossing inside of a group") << QByteArray("q 1 w /T BMC q EMC Q Q")
                                                << QStringLiteral("Root{Group(0:1/6:1){StateRun(1:1) Opaque(2:4)}}") << kinds({ Kind::CrossedBrackets });

    // Marked content and a text object must nest (14.6.1): BT0 BMC1 ET2 EMC3 and BMC0 BT1 EMC2 ET3
    QTest::newRow("BT BMC ET EMC") << QByteArray("BT /T BMC ET EMC")
                                   << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::CrossedBrackets });
    QTest::newRow("BMC BT EMC ET") << QByteArray("/T BMC BT EMC ET")
                                   << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::CrossedBrackets });

    // BT inside of BT: the whole text object (the processor refuses the second BT)
    QTest::newRow("BT in BT") << QByteArray("BT BT ET ET")
                              << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::InvalidTextObject, Kind::OperatorFailed });

    // q, Q, cm, Do, sh and a path inside of BT ... ET (8.2, Figure 9): the whole text object
    QTest::newRow("q in BT") << QByteArray("BT q Q ET")
                             << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::InvalidTextObject });
    QTest::newRow("cm in BT") << QByteArray("1 w BT 1 0 0 1 0 0 cm ET 2 w")
                              << QStringLiteral("Root{StateRun(0:1) Opaque(1:3) StateRun(4:1)}") << kinds({ Kind::InvalidTextObject });
    QTest::newRow("Do in BT") << QByteArray("BT /Im1 Do ET")
                              << QStringLiteral("Root{Opaque(0:3)}") << kinds({ Kind::InvalidTextObject });
    QTest::newRow("sh in BT") << QByteArray("BT /Sh1 sh ET")
                              << QStringLiteral("Root{Opaque(0:3)}") << kinds({ Kind::InvalidTextObject });
    QTest::newRow("path in BT") << QByteArray("BT 0 0 5 5 re f ET")
                                << QStringLiteral("Root{Opaque(0:4)}") << kinds({ Kind::InvalidTextObject });

    // BT0 m1 ET2 l3 S4: the path begun inside of the text object ends after it - the span
    // covers the path too
    QTest::newRow("path over the end of BT") << QByteArray("BT 0 0 m ET 5 5 l S")
                                             << QStringLiteral("Root{Opaque(0:5)}") << kinds({ Kind::InvalidTextObject });

    // q0 BMC1 BT2 Q3 ET4 EMC5: Q inside of the text object closes the q before the marked
    // content - the span grows to that q and ends with the last open bracket
    QTest::newRow("three crossed brackets") << QByteArray("q /A BMC BT Q ET EMC 1 w")
                                            << QStringLiteral("Root{Opaque(0:6) StateRun(6:1)}") << kinds({ Kind::InvalidTextObject, Kind::CrossedBrackets });

    // m0 w1 l2 S3 w4: a foreign operator between the construction of a path and its painting
    // operator - the whole path
    QTest::newRow("foreign operator in a path") << QByteArray("0 0 m 1 w 5 5 l S 2 w")
                                                << QStringLiteral("Root{Opaque(0:4) StateRun(4:1)}") << kinds({ Kind::InvalidPath });
    QTest::newRow("bracket in a path") << QByteArray("0 0 m q 5 5 l Q S")
                                       << QStringLiteral("Root{Opaque(0:5)}") << kinds({ Kind::InvalidPath });

    // A path, which the operands do not give: l without a current point (8.5.2.1: only m and re
    // begin a path), an operator with a missing operand, no painting operator at the end
    QTest::newRow("line without a current point") << QByteArray("5 5 l S 1 w")
                                                  << QStringLiteral("Root{Opaque(0:2) StateRun(2:1)}") << kinds({ Kind::InvalidPath });
    QTest::newRow("missing operand in a path") << QByteArray("0 0 m 5 l S")
                                               << QStringLiteral("Root{Opaque(0:3)}") << kinds({ Kind::OperatorFailed, Kind::InvalidPath });
    QTest::newRow("path without a painting operator") << QByteArray("1 w 0 0 m 5 5 l")
                                                      << QStringLiteral("Root{StateRun(0:1) Opaque(1:2)}") << kinds({ Kind::InvalidPath });

    // An end without a beginning (the processor refuses each of them)
    QTest::newRow("EMC alone") << QByteArray("1 w EMC 2 w")
                               << QStringLiteral("Root{StateRun(0:1) Opaque(1:1) StateRun(2:1)}") << kinds({ Kind::UnbalancedEnd, Kind::OperatorFailed });
    QTest::newRow("Q alone") << QByteArray("Q")
                             << QStringLiteral("Root{Opaque(0:1)}") << kinds({ Kind::UnbalancedEnd, Kind::OperatorFailed });
    QTest::newRow("ET alone") << QByteArray("ET")
                              << QStringLiteral("Root{Opaque(0:1)}") << kinds({ Kind::UnbalancedEnd, Kind::OperatorFailed });

    // An unknown operator, also inside of a text object (its text object stays a text object)
    QTest::newRow("unknown operator") << QByteArray("1 w foo 2 w")
                                      << QStringLiteral("Root{StateRun(0:1) Opaque(1:1) StateRun(2:1)}") << kinds({ Kind::UnknownOperator });
    QTest::newRow("unknown operator in BT") << QByteArray("BT foo ET")
                                            << QStringLiteral("Root{TextObject(0:1/2:1){Opaque(1:1)}}") << kinds({ Kind::UnknownOperator });

    // A text operator outside of a text object
    QTest::newRow("Td outside of BT") << QByteArray("1 2 Td")
                                      << QStringLiteral("Root{Opaque(0:1)}") << kinds({ Kind::InvalidTextObject });

    // w0 Td1 w2: outside of a text object Td is no part of a state run - the run before it ends
    QTest::newRow("Td after a state run") << QByteArray("1 w 1 2 Td 2 w")
                                          << QStringLiteral("Root{StateRun(0:1) Opaque(1:1) StateRun(2:1)}") << kinds({ Kind::InvalidTextObject });

    // An XObject, which the resources do not define
    QTest::newRow("undefined xobject") << QByteArray("/Nope Do")
                                       << QStringLiteral("Root{Opaque(0:1)}") << kinds({ Kind::UndefinedResource, Kind::OperatorFailed });
}

void ContentTreeTest::opaqueRules()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);
    QFETCH(IssueKinds, issues);

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), tree);
    QCOMPARE(issueKinds(result.issues), issues);
    QCOMPARE(issueKinds(result.container->getIssues()), issues);

    // An opaque node is written unchanged: its operations are the bytes of the content; nothing
    // is known about what it does to the state
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->isUnchanged());
    QVERIFY(!result.container->isReadOnly());

    for (const quint64 id : identifiers(result.container->getRoot()))
    {
        const PDFContentNodePointer node = result.container->getNode(PDFContentNodeId{ id });
        if (node->getKind() == PDFContentNodeKind::Opaque)
        {
            QVERIFY(node->getLeak().isUnknown);
            QVERIFY(!node->getData().opaqueReason.isEmpty());
            QVERIFY(node->getChildren().empty());

            // The state after an opaque node is known (the processor has interpreted it): it
            // is the state of the node, which follows it
            QVERIFY(node->getLeak().endState != nullptr);
            QVERIFY(node->getLeak().endSymbolicState != nullptr);

            const PDFContentNodePointer parent = result.container->getParent(node->getId());
            const size_t next = size_t(result.container->getIndexInParent(node->getId())) + 1;
            if (next < parent->getChildren().size())
            {
                QVERIFY(parent->getChildren()[next]->getState() == node->getLeak().endState);
                QVERIFY(parent->getChildren()[next]->getSymbolicState() == node->getLeak().endSymbolicState);
            }
        }
    }

    // Every issue of an operation names the node, which holds the operation
    for (const PDFContentIssue& issue : result.issues)
    {
        const PDFContentNodePointer node = result.container->getNode(issue.nodeId);
        QVERIFY(node != nullptr);
        const PDFContentOperationSpan& prefix = node->getPrefix();
        const PDFContentOperationSpan& suffix = node->getSuffix();
        const bool isInPrefix = issue.operationIndex >= prefix.first && issue.operationIndex < prefix.first + prefix.count;
        const bool isInSuffix = issue.operationIndex >= suffix.first && issue.operationIndex < suffix.first + suffix.count;
        QVERIFY(isInPrefix || isInSuffix);
        QVERIFY(!issue.text.isEmpty());
    }
}

void ContentTreeTest::operationFlags()
{
    // BX0 foo1 EX2 bar3: between BX and EX an unknown operator is no error (ISO 32000-2, 7.8.2),
    // outside it is; both are opaque (zadani_jadro.md 4.2: "regardless of BX ... EX")
    const PDFDocument document = createDocument("BX foo EX bar");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Opaque(1:1) StateRun(2:1) Opaque(3:1)}"));

    const PDFContentOperationList& operations = result.container->getOperations();
    QCOMPARE(operations.getCount(), size_t(4));
    QCOMPARE(operations.getOriginalCount(), size_t(4));

    QVERIFY(operations.getOperation(0).getOperator() == PDFContentOperator::CompatibilityBegin);
    QVERIFY(!operations.getOperation(0).getFlags());
    QVERIFY(operations.getOperation(1).getOperator() == PDFContentOperator::Invalid);
    QVERIFY(operations.getOperation(1).getFlags().testFlag(PDFContentOperation::Unknown));
    QVERIFY(operations.getOperation(1).getFlags().testFlag(PDFContentOperation::InsideCompatibility));
    QVERIFY(operations.getOperation(2).getOperator() == PDFContentOperator::CompatibilityEnd);
    QVERIFY(!operations.getOperation(2).getFlags());
    QVERIFY(operations.getOperation(3).getFlags().testFlag(PDFContentOperation::Unknown));
    QVERIFY(!operations.getOperation(3).getFlags().testFlag(PDFContentOperation::InsideCompatibility));

    // The bytes of an operation: from the end of the operation before it to the end of its operator
    QCOMPARE(operations.getOperation(0).getRawBytes(), QByteArray("BX"));
    QCOMPARE(operations.getOperation(1).getRawBytes(), QByteArray(" foo"));
    QCOMPARE(operations.getOperation(3).getRawBytes(), QByteArray(" bar"));
    QVERIFY(operations.getOperation(1).getRange() == (PDFContentByteRange{ PDFContentStreamPosition{ 0, 2 }, PDFContentStreamPosition{ 0, 6 } }));

    // Only the operator outside of BX ... EX is an issue
    QCOMPARE(result.issues.size(), size_t(1));
    QVERIFY(result.issues.front().kind == PDFContentIssue::Kind::UnknownOperator);
    QCOMPARE(result.issues.front().operationIndex, size_t(3));
    QVERIFY(result.issues.front().nodeId == nodeAt(result.container, { 3 })->getId());

    // The operands are kept: w with one number, Tf with a name and a number; the resource names.
    // w0 Tf1 gs2 CS3 cs4 cs5 scn6 Do7 sh8 BDC9 EMC10
    const PDFDocument operandDocument = createDocument("2.5 w /F1 12 Tf /GS1 gs /Cs1 CS /DeviceRGB cs /Pattern cs /P1 scn /Im1 Do /Sh1 sh /T /MC1 BDC EMC");
    const PDFContentBuildResult operandResult = buildPage(operandDocument);
    QVERIFY(operandResult.container != nullptr);
    QVERIFY(operandResult.issues.empty());
    const PDFContentOperationList& operandOperations = operandResult.container->getOperations();
    QCOMPARE(operandOperations.getCount(), size_t(11));

    QCOMPARE(operandOperations.getOperation(0).getOperands().size(), size_t(1));
    QVERIFY(operandOperations.getOperation(0).getOperands()[0].type == PDFLexicalAnalyzer::TokenType::Real);
    QCOMPARE(operandOperations.getOperation(0).getOperands()[0].data.toDouble(), 2.5);
    QCOMPARE(operandOperations.getOperation(1).getOperands().size(), size_t(2));
    QVERIFY(operandOperations.getOperation(1).getOperands()[0].type == PDFLexicalAnalyzer::TokenType::Name);
    QCOMPARE(operandOperations.getOperation(1).getOperands()[0].data.toByteArray(), QByteArray("F1"));

    using Category = PDFContentResourceCategory;
    using Names = std::vector<PDFContentResourceName>;
    QVERIFY(operandOperations.getOperation(0).getResourceNames().empty());
    QVERIFY(operandOperations.getOperation(1).getResourceNames() == Names({ { Category::Font, "F1" } }));
    QVERIFY(operandOperations.getOperation(2).getResourceNames() == Names({ { Category::ExtGState, "GS1" } }));
    QVERIFY(operandOperations.getOperation(3).getResourceNames() == Names({ { Category::ColorSpace, "Cs1" } }));
    QVERIFY(operandOperations.getOperation(4).getResourceNames().empty());      // A device space is no resource (8.6.8)
    QVERIFY(operandOperations.getOperation(5).getResourceNames().empty());      // Nor is the pattern space
    QVERIFY(operandOperations.getOperation(6).getResourceNames() == Names({ { Category::Pattern, "P1" } }));
    QVERIFY(operandOperations.getOperation(7).getResourceNames() == Names({ { Category::XObject, "Im1" } }));
    QVERIFY(operandOperations.getOperation(8).getResourceNames() == Names({ { Category::Shading, "Sh1" } }));
    QVERIFY(operandOperations.getOperation(9).getResourceNames() == Names({ { Category::Properties, "MC1" } }));
    QVERIFY(operandOperations.getOperation(10).getResourceNames().empty());

    // The names of the leaves: StateRun(0:7) Image(7:1) Shading(8:1) MarkedContent(9:1/10:1)
    QCOMPARE(describe(operandResult.container), QStringLiteral("Root{StateRun(0:7) Image(7:1) Shading(8:1) MarkedContent(9:1/10:1)}"));
    QCOMPARE(nodeAt(operandResult.container, { 1 })->getData().resourceName, QByteArray("Im1"));
    QCOMPARE(nodeAt(operandResult.container, { 2 })->getData().resourceName, QByteArray("Sh1"));
    QVERIFY(nodeAt(operandResult.container, { 2 })->getKind() == PDFContentNodeKind::Shading);

    // The resources of the container are those of the page: every name is defined
    QVERIFY(operandResult.container->getResources().getObject(Category::Font, "F1").isReference());
    QVERIFY(operandResult.container->getResources().getObject(Category::Font, "F1") == pageResource(operandDocument, "Font", "F1"));
    QVERIFY(operandResult.container->getResources().getUndefinedNames(Category::Font).empty());
    QVERIFY(!operandResult.container->getResources().isChanged());
}

void ContentTreeTest::failedOperator()
{
    // rg0 gs1 w2 re3 f4: the graphics state /Missing is not defined - the operator fails, stays
    // in its state run and is flagged; the state is the one without it
    const PDFDocument document = createDocument("1 0 0 rg /Missing gs 2 w 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:3) Path(3:2)}"));

    const PDFContentOperationList& operations = result.container->getOperations();
    QVERIFY(!operations.getOperation(0).getFlags().testFlag(PDFContentOperation::Failed));
    QVERIFY(operations.getOperation(1).getFlags().testFlag(PDFContentOperation::Failed));
    QVERIFY(!operations.getOperation(2).getFlags().testFlag(PDFContentOperation::Failed));
    QVERIFY(!operations.getOperation(1).isModified());      // Failed is neither generated nor suppressed
    QVERIFY(result.container->isUnchanged());

    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UndefinedResource, PDFContentIssue::Kind::OperatorFailed }));
    const PDFContentNodeId runId = nodeAt(result.container, { 0 })->getId();
    for (const PDFContentIssue& issue : result.issues)
    {
        QCOMPARE(issue.operationIndex, size_t(1));
        QVERIFY(issue.nodeId == runId);
    }

    // The name used without a definition is known to the resources (allocateName avoids it)
    QVERIFY(result.container->getResources().getUndefinedNames(PDFContentResourceCategory::ExtGState).count(QByteArray("Missing")) == 1);

    // The state of the path: the color of rg and the width of w; the failed gs is the source of nothing
    using Property = PDFContentStateProperty;
    const PDFContentNodePointer path = nodeAt(result.container, { 1 });
    QCOMPARE(path->getState()->getState().getLineWidth(), 2.0);
    QCOMPARE(path->getSymbolicState()->getSource(Property::LineWidth).operationIndex, size_t(2));
    QCOMPARE(path->getSymbolicState()->getSource(Property::FillColor).operationIndex, size_t(0));
    QVERIFY(path->getSymbolicState()->isInherited(Property::AlphaFilling));
    QVERIFY(path->getSymbolicState()->isInherited(Property::SoftMask));

    // An operator with a wrong operand: the width stays 1 (the default of ISO 32000-2, Table 51),
    // the operator is not its source
    const PDFDocument operandDocument = createDocument("/Name w 0 0 5 5 re f");
    const PDFContentBuildResult operandResult = buildPage(operandDocument);
    QCOMPARE(describe(operandResult.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));
    QVERIFY(operandResult.container->getOperations().getOperation(0).getFlags().testFlag(PDFContentOperation::Failed));
    QCOMPARE(issueKinds(operandResult.issues), kinds({ PDFContentIssue::Kind::OperatorFailed }));
    const PDFContentNodePointer operandPath = nodeAt(operandResult.container, { 1 });
    QCOMPARE(operandPath->getState()->getState().getLineWidth(), 1.0);
    QVERIFY(operandPath->getSymbolicState()->isInherited(Property::LineWidth));
    QVERIFY(nodeAt(operandResult.container, { 0 })->getLeak().isEmpty());
}

void ContentTreeTest::depthLimit()
{
    // zadani_overeni.md 6.2: the depth of the tree is 256; deeper content is one opaque node up
    // to its matching end. 257 nested groups, each with an operation before the next q (so the
    // q are not a chain): the operations of the level i (from 1) are q 2(i-1), w 2(i-1)+1; the Q
    // follow from 514.
    const int depth = PDFContentContainer::MAXIMUM_DEPTH + 1;
    QByteArray content;
    for (int i = 0; i < depth; ++i)
    {
        content += "q 1 w ";
    }
    for (int i = 0; i < depth; ++i)
    {
        content += "Q ";
    }

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);

    // The groups of the levels 1 ... 256 are nodes, the 257th q with its w and its Q (the
    // operations 512, 513, 514) is one opaque node in the group of the level 256
    PDFContentNodePointer node = result.container->getRoot();
    for (int level = 1; level <= PDFContentContainer::MAXIMUM_DEPTH; ++level)
    {
        QCOMPARE(node->getChildren().size(), size_t(level == 1 ? 1 : 2));
        node = node->getChildren().back();
        QVERIFY(node->getKind() == PDFContentNodeKind::Group);
        QVERIFY(node->getPrefix() == (PDFContentOperationSpan{ size_t(2 * (level - 1)), 1 }));

        // The groups are closed from the inside: the level 256 by the Q 515, the level 1 by the Q 770
        QVERIFY(node->getSuffix() == (PDFContentOperationSpan{ size_t(515 + PDFContentContainer::MAXIMUM_DEPTH - level), 1 }));
        QVERIFY(node->getChildren().front()->getKind() == PDFContentNodeKind::StateRun);
    }

    QCOMPARE(node->getChildren().size(), size_t(2));
    const PDFContentNodePointer opaque = node->getChildren().back();
    QVERIFY(opaque->getKind() == PDFContentNodeKind::Opaque);
    QVERIFY(opaque->getPrefix() == (PDFContentOperationSpan{ 512, 3 }));
    QVERIFY(opaque->getLeak().isUnknown);

    // 1 root + 256 groups + 256 state runs + 1 opaque node; the opaque node is the last one of
    // the depth first walk
    QCOMPARE(result.container->getNodeCount(), size_t(514));
    QVERIFY(opaque->getId() == (PDFContentNodeId{ 514 }));

    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::DepthLimit }));
    QCOMPARE(result.issues.front().operationIndex, size_t(512));
    QVERIFY(result.issues.front().nodeId == opaque->getId());

    QCOMPARE(result.container->getOperations().getCount(), size_t(3 * depth));
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->isUnchanged());

    // 256 levels are not too deep: no opaque node, no issue
    QByteArray allowedContent;
    for (int i = 0; i < PDFContentContainer::MAXIMUM_DEPTH; ++i)
    {
        allowedContent += "q 1 w ";
    }
    for (int i = 0; i < PDFContentContainer::MAXIMUM_DEPTH; ++i)
    {
        allowedContent += "Q ";
    }

    const PDFDocument allowedDocument = createDocument(allowedContent);
    const PDFContentBuildResult allowedResult = buildPage(allowedDocument);
    QVERIFY(allowedResult.container != nullptr);
    QVERIFY(allowedResult.issues.empty());
    QCOMPARE(allowedResult.container->getNodeCount(), size_t(1 + 2 * PDFContentContainer::MAXIMUM_DEPTH));
    for (const quint64 id : identifiers(allowedResult.container->getRoot()))
    {
        QVERIFY(allowedResult.container->getNode(PDFContentNodeId{ id })->getKind() != PDFContentNodeKind::Opaque);
    }
}

void ContentTreeTest::saveChainDepthLimit()
{
    // A chain of q is one node, but it turns into a node inside of a node for every part of its
    // Q, which does not come in a row (saveChain) - so every q counts as a level. 300 q and
    // 300 Q: the first 256 q (the operations 0 ... 255) are the group, the q 256 ... 299 and the
    // 44 Q 300 ... 343, which close them, are one opaque node, the Q 344 ... 599 close the group.
    QByteArray content;
    for (int i = 0; i < 300; ++i)
    {
        content += "q ";
    }
    for (int i = 0; i < 300; ++i)
    {
        content += "Q ";
    }

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Group(0:256/344:256){Opaque(256:88)}}"));
    QCOMPARE(nodeAt(result.container, { 0 })->getData().saveDepth, 256);
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::DepthLimit }));
    QVERIFY(result.container->isUnchanged());
}

void ContentTreeTest::operandLimit()
{
    // zadani_overeni.md 6.2: more than 100 000 operands of one operation are not decoded, the
    // operation is opaque. The array of TJ with 100 001 numbers has 100 003 operands (with its
    // two brackets). BT0 Tf1 TJ2 Tj3 ET4: only the operation is opaque, not its text object.
    QByteArray numbers;
    for (int i = 0; i < 100001; ++i)
    {
        numbers += "0 ";
    }
    const QByteArray content = "BT /F1 10 Tf [" + numbers + "] TJ (A) Tj ET";

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:1) Opaque(2:1) TextShow(3:1)}}"));

    const PDFContentOperation& operation = result.container->getOperations().getOperation(2);
    QVERIFY(operation.getOperator() == PDFContentOperator::TextShowTextIndividualSpacing);
    QVERIFY(operation.getOperands().empty());
    QCOMPARE(operation.getRawBytes(), QByteArray(" [" + numbers + "] TJ"));     // The bytes are kept
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);

    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::OperandLimit }));
    QCOMPARE(result.issues.front().operationIndex, size_t(2));
    QVERIFY(result.issues.front().nodeId == nodeAt(result.container, { 0, 1 })->getId());

    // Exactly 100 000 operands (99 998 numbers and the two brackets) are decoded
    QByteArray allowedNumbers;
    for (int i = 0; i < 99998; ++i)
    {
        allowedNumbers += "0 ";
    }
    const PDFDocument allowedDocument = createDocument("BT /F1 10 Tf [" + allowedNumbers + "] TJ ET");
    const PDFContentBuildResult allowedResult = buildPage(allowedDocument);
    QCOMPARE(describe(allowedResult.container), QStringLiteral("Root{TextObject(0:1/3:1){StateRun(1:1) TextShow(2:1)}}"));
    QCOMPARE(allowedResult.container->getOperations().getOperation(2).getOperands().size(), size_t(100000));
    QVERIFY(allowedResult.issues.empty());
}

void ContentTreeTest::operationLimit()
{
    // zadani_overeni.md 6.2: above 5 000 000 operations the whole container is one opaque node.
    // 5 000 001 times "0 g".
    const size_t count = PDFContentContainer::MAXIMUM_OPERATION_COUNT + 1;
    QByteArray content;
    content.reserve(qsizetype(count * 4));
    for (size_t i = 0; i < count; ++i)
    {
        content += "0 g\n";
    }

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QVERIFY(!result.isCanceled);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Opaque(0:1)}"));

    // The content is kept whole as one operation, which is written back as it is
    const PDFContentOperationList& operations = result.container->getOperations();
    QCOMPARE(operations.getCount(), size_t(1));
    QCOMPARE(operations.getOriginalCount(), size_t(1));
    QVERIFY(operations.getOriginalContent() == content);
    QVERIFY(operations.getTail().isEmpty());
    QVERIFY(result.container->isUnchanged());
    QVERIFY(!result.container->isReadOnly());

    const PDFContentNodePointer opaque = nodeAt(result.container, { 0 });
    QVERIFY(opaque->getLeak().isUnknown);
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::OperationLimit }));
    QVERIFY(result.issues.front().nodeId == opaque->getId());
}

void ContentTreeTest::contentTail_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QByteArray>("tail");
    QTest::addColumn<QString>("tree");
    QTest::addColumn<bool>("isUnreadable");

    // zadani_jadro.md 4.2: the end of a content, which cannot be split into operations, is one
    // node at the end. White space and comments after the last operator are no such end.
    QTest::newRow("unfinished string") << QByteArray("1 w (abc") << QByteArray(" (abc")
                                       << QStringLiteral("Root{StateRun(0:1) Opaque}") << true;
    QTest::newRow("operand without an operator") << QByteArray("1 w 5") << QByteArray(" 5")
                                                 << QStringLiteral("Root{StateRun(0:1) Opaque}") << true;
    QTest::newRow("unfinished array") << QByteArray("q Q [1 2") << QByteArray(" [1 2")
                                      << QStringLiteral("Root{Group(0:1/1:1) Opaque}") << true;
    QTest::newRow("only operands") << QByteArray("1 2 3") << QByteArray("1 2 3")
                                   << QStringLiteral("Root{Opaque}") << true;
    QTest::newRow("comment") << QByteArray("1 w\n% the end") << QByteArray("\n% the end")
                             << QStringLiteral("Root{StateRun(0:1)}") << false;
    QTest::newRow("white space") << QByteArray("1 w \r\n\t ") << QByteArray(" \r\n\t ")
                                 << QStringLiteral("Root{StateRun(0:1)}") << false;
    QTest::newRow("nothing") << QByteArray("1 w") << QByteArray()
                             << QStringLiteral("Root{StateRun(0:1)}") << false;

    // A comment ends at the end of its line: the number after it is an operand without an operator
    QTest::newRow("operand after a comment") << QByteArray("1 w % c\n5") << QByteArray(" % c\n5")
                                             << QStringLiteral("Root{StateRun(0:1) Opaque}") << true;
}

void ContentTreeTest::contentTail()
{
    QFETCH(QByteArray, content);
    QFETCH(QByteArray, tail);
    QFETCH(QString, tree);
    QFETCH(bool, isUnreadable);

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), tree);
    QCOMPARE(result.container->getOperations().getTail(), tail);
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->isUnchanged());

    if (isUnreadable)
    {
        // The node of the tail is the last child of the root and has no operation of its own:
        // new content goes before it, the bytes are written after everything else
        const PDFContentNodePointer node = result.container->getRoot()->getChildren().back();
        QVERIFY(node->getKind() == PDFContentNodeKind::Opaque);
        QVERIFY(node->getPrefix().isEmpty());
        QVERIFY(node->getLeak().isUnknown);
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UnreadableTail }));
        QVERIFY(result.issues.front().nodeId == node->getId());
    }
    else
    {
        QVERIFY(result.issues.empty());
    }
}

void ContentTreeTest::appendIndex()
{
    // PDFContentContainer::getAppendIndex: where new content goes, so that it is the last
    // content, which is still read as content (zadani_jadro.md 4.2: new content is inserted
    // before the end of a content, which cannot be split into operations).
    const PDFContentNodeId root{ 1 };

    {
        // The tail node stays the last one: Root{StateRun(0:1) Opaque}
        const PDFDocument document = createDocument("1 w (abc");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{StateRun(0:1) Opaque}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));

        // A node inserted there is between the content and the tail
        PDFContentContainerEditor editor(container);
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 100), root, container->getAppendIndex(root)));
        const PDFContentContainerPointer appended = editor.build();
        QCOMPARE(describe(appended), QStringLiteral("Root{StateRun(0:1) Path Opaque}"));
        QCOMPARE(appended->getAppendIndex(root), PDFInteger(2));
    }

    {
        // Only a tail: the new content is the first child
        const PDFDocument document = createDocument("1 2 3");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{Opaque}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(0));
    }

    {
        // No tail: after the last child. q0 w1 Q2 BMC3 EMC4 foo5 - an opaque node, which is
        // an operation of its own, is content as any other.
        const PDFDocument document = createDocument("q 1 w Q /T BMC EMC foo");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{Group(0:1/2:1){StateRun(1:1)} MarkedContent(3:1/4:1) Opaque(5:1)}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(3));
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 0 })->getId()), PDFInteger(1));     // The group
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 1 })->getId()), PDFInteger(0));     // The empty marked content

        // A leaf has no children, an unknown node is no parent
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 0, 0 })->getId()), PDFInteger(-1));
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 2 })->getId()), PDFInteger(-1));
        QCOMPARE(container->getAppendIndex(PDFContentNodeId{ 99 }), PDFInteger(-1));
        QCOMPARE(container->getAppendIndex(PDFContentNodeId()), PDFInteger(-1));
    }

    {
        // An empty page
        const PDFDocument document = createDocument(QByteArray());
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(container->getAppendIndex(root), PDFInteger(0));
    }

    {
        // The rest of a content after an inline image without an end is image data, whatever
        // is written after it: new content goes before it. Root{StateRun(0:1) Opaque(1:...)}
        const PDFDocument document = createDocument("1 w BI /W 2 /H 2 ID abcd 2 w");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(container->getRoot()->getChildren().size(), size_t(2));
        QVERIFY(nodeAt(container, { 1 })->getKind() == PDFContentNodeKind::Opaque);
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));
    }

    {
        // The same inside of a group, which the content does not close: re0 f1 q2 w3 BI4 ... -
        // Root{Path(0:2) Group(2:1){StateRun(3:1) Opaque(4:...)}}. New content of the group goes
        // before the rest, new content of the root before the group, which holds the rest.
        const PDFDocument document = createDocument("0 0 5 5 re f q 1 w BI /W 2 /H 2 ID abcd Q");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(container->getRoot()->getChildren().size(), size_t(2));
        const PDFContentNodePointer group = nodeAt(container, { 1 });
        QVERIFY(group->getKind() == PDFContentNodeKind::Group);
        QVERIFY(group->getSuffix().isEmpty());
        QCOMPARE(group->getChildren().size(), size_t(2));
        QCOMPARE(container->getAppendIndex(group->getId()), PDFInteger(1));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));
        QVERIFY(container->isUnclosed(group->getId()));
    }

    {
        // The rest after an inline image without an end and an unreadable tail behind it (the
        // bytes of an image often end with something, which is no operation): both stay behind
        // everything else, new content goes before both. BI is the operation 1; the processor
        // reads "ID", "abcd" and "2 w" in the image data as the operations 2 to 4, "(x" is a
        // string without its end - the tail. Root{StateRun(0:1) Opaque(1:4) Opaque}
        const PDFDocument document = createDocument("1 w BI /W 2 /H 2 ID abcd 2 w (x");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{StateRun(0:1) Opaque(1:4) Opaque}"));
        QCOMPARE(container->getOperations().getTail(), QByteArray(" (x"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));

        // A node inserted there is before the image and before the tail
        PDFContentContainerEditor editor(container);
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Path, 100), root, container->getAppendIndex(root)));
        const PDFContentContainerPointer appended = editor.build();
        QCOMPARE(describe(appended), QStringLiteral("Root{StateRun(0:1) Path Opaque(1:4) Opaque}"));
        QCOMPARE(appended->getAppendIndex(root), PDFInteger(2));
    }

    {
        // The same with the rest inside of a group, which the content does not close: re0 f1 q2
        // w3 BI4 ID5 abcd6 Q7 and the tail "(x". New content of the root goes before the group
        // and the tail, new content of the group before the rest.
        const PDFDocument document = createDocument("0 0 5 5 re f q 1 w BI /W 2 /H 2 ID abcd Q (x");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1) Opaque(4:4)} Opaque}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 1 })->getId()), PDFInteger(1));
    }

    {
        // A group without its Q, but with nothing wrong after it: new content follows it. The
        // place is behind the missing Q of the group (the editor writes the Q, when something
        // is put there - the test unclosedNodes), not inside of the group.
        const PDFDocument document = createDocument("0 0 5 5 re f q 1 w");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1)}}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(2));
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 1 })->getId()), PDFInteger(1));
        QVERIFY(container->isUnclosed(nodeAt(container, { 1 })->getId()));
    }

    {
        // A closed group, whose last child is an opaque rest, cannot be made by the builder; a
        // closed node is never looked into: q0 foo1 Q2
        const PDFDocument document = createDocument("q foo Q");
        const PDFContentContainerPointer container = buildPage(document).container;
        QCOMPARE(describe(container), QStringLiteral("Root{Group(0:1/2:1){Opaque(1:1)}}"));
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));
        QCOMPARE(container->getAppendIndex(nodeAt(container, { 0 })->getId()), PDFInteger(1));
    }

    {
        // A tree made by hand: a node without operations at the end of a group, which has its
        // closing operation. The group is complete - content after it is content.
        PDFContentContainerEditor editor(PDFContentContainerKey::createPage(0, PDFObjectReference(20, 0)));
        PDFContentNode::Data groupData = nodeData(PDFContentNodeKind::Group, 2);
        groupData.prefix = PDFContentOperationSpan{ 0, 1 };
        groupData.suffix = PDFContentOperationSpan{ 1, 1 };
        QVERIFY(editor.insertNode(groupData, root, -1));
        QVERIFY(editor.insertNode(nodeData(PDFContentNodeKind::Opaque, 3), PDFContentNodeId{ 2 }, -1));
        const PDFContentContainerPointer container = editor.build();
        QCOMPARE(container->getAppendIndex(root), PDFInteger(1));
        QCOMPARE(container->getAppendIndex(PDFContentNodeId{ 2 }), PDFInteger(0));
    }
}

void ContentTreeTest::unclosedNodes_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");
    QTest::addColumn<int>("unclosedCount");
    QTest::addColumn<QString>("appendedTree");
    QTest::addColumn<QByteArray>("appendedContent");

    // A node, which the content does not close (the content ends inside of it), and content
    // appended to the page behind it. Whatever is written behind the last byte of the content is
    // read inside of the open node: in the graphics state of the q (ISO 32000-2, 8.4.2), in the
    // marked-content sequence (14.6), in the text object (8.2, Figure 9: no path or q inside of
    // BT ... ET). zadani_jadro.md 4.4, R-7: a missing Q, ET or EMC is not added, until the node
    // is changed - content appended behind it is that change: the model writes the missing
    // operators first.
    //
    // The appended node is a state run of one generated operation "3 w" at
    // getAppendIndex(root). The generated operations are numbered behind the original ones: "3 w"
    // first (the test appends it), then the closing operators - of the outer node before the
    // inner one. They are written one a line (canonical text).

    // re0 f1 q2 cm3 rg4; "3 w" is 5, Q is 6
    QTest::newRow("group") << QByteArray("0 0 5 5 re f q 2 0 0 2 0 0 cm 1 0 0 rg")
                           << QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:2)}}") << 1
                           << QStringLiteral("Root{Path(0:2) Group(2:1/6:1){StateRun(3:2)} StateRun(5:1)}")
                           << QByteArray("0 0 5 5 re f q 2 0 0 2 0 0 cm 1 0 0 rg\nQ\n3 w");

    // q0 q1 q2 w3; "3 w" is 4, the three Q are 5 to 7: one for each q of the chain
    QTest::newRow("chain of three q") << QByteArray("q q q 1 w")
                                      << QStringLiteral("Root{Group(0:3){StateRun(3:1)}}") << 1
                                      << QStringLiteral("Root{Group(0:3/5:3){StateRun(3:1)} StateRun(4:1)}")
                                      << QByteArray("q q q 1 w\nQ\nQ\nQ\n3 w");

    // q0 q1 w2 Q3; the inner group is closed, the outer one gets one Q
    QTest::newRow("a part of the ends") << QByteArray("q q 1 w Q")
                                        << QStringLiteral("Root{Group(0:1){Group(1:1/3:1){StateRun(2:1)}}}") << 1
                                        << QStringLiteral("Root{Group(0:1/5:1){Group(1:1/3:1){StateRun(2:1)}} StateRun(4:1)}")
                                        << QByteArray("q q 1 w Q\nQ\n3 w");

    // BDC0 re1 f2 (an optional content sequence: what is written inside of it, is hidden with
    // its layer); "3 w" is 3, EMC is 4
    QTest::newRow("marked content") << QByteArray("/OC /OC1 BDC 0 0 5 5 re f")
                                    << QStringLiteral("Root{MarkedContent(0:1){Path(1:2)}}") << 1
                                    << QStringLiteral("Root{MarkedContent(0:1/4:1){Path(1:2)} StateRun(3:1)}")
                                    << QByteArray("/OC /OC1 BDC 0 0 5 5 re f\nEMC\n3 w");

    // BT0 Tf1 Td2 Tj3; "3 w" is 4, ET is 5
    QTest::newRow("text object") << QByteArray("BT /F1 12 Tf 10 10 Td (A) Tj")
                                 << QStringLiteral("Root{TextObject(0:1){StateRun(1:2) TextShow(3:1)}}") << 1
                                 << QStringLiteral("Root{TextObject(0:1/5:1){StateRun(1:2) TextShow(3:1)} StateRun(4:1)}")
                                 << QByteArray("BT /F1 12 Tf 10 10 Td (A) Tj\nET\n3 w");

    // q0 BT1 Tf2 Tj3: two open nodes; "3 w" is 4, Q of the group is 5, ET of the text object
    // is 6 - written in the order of the tree, ET before Q
    QTest::newRow("text object in a group") << QByteArray("q BT /F1 12 Tf (A) Tj")
                                            << QStringLiteral("Root{Group(0:1){TextObject(1:1){StateRun(2:1) TextShow(3:1)}}}") << 2
                                            << QStringLiteral("Root{Group(0:1/5:1){TextObject(1:1/6:1){StateRun(2:1) TextShow(3:1)}} StateRun(4:1)}")
                                            << QByteArray("q BT /F1 12 Tf (A) Tj\nET\nQ\n3 w");

    // q0 w1 and a tail, which cannot be read: the new content goes before the tail node and
    // behind the group; "3 w" is 2, Q is 3; the tail is written after everything else
    QTest::newRow("group and a tail") << QByteArray("q 1 w (abc")
                                      << QStringLiteral("Root{Group(0:1){StateRun(1:1)} Opaque}") << 1
                                      << QStringLiteral("Root{Group(0:1/3:1){StateRun(1:1)} StateRun(2:1) Opaque}")
                                      << QByteArray("q 1 w\nQ\n3 w\n (abc");
}

void ContentTreeTest::unclosedNodes()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);
    QFETCH(int, unclosedCount);
    QFETCH(QString, appendedTree);
    QFETCH(QByteArray, appendedContent);

    const PDFContentNodeId root{ 1 };

    const PDFDocument document = createDocument(content);
    const PDFContentContainerPointer container = buildPage(document).container;
    QVERIFY(container != nullptr);
    QCOMPARE(describe(container), tree);
    QCOMPARE(int(unclosedIdentifiers(container).size()), unclosedCount);

    // Nothing is changed: nothing is added, the content is written byte for byte
    QVERIFY(container->isUnchanged());
    {
        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*container);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, content);
    }

    // New content at the end of the page
    PDFContentContainerEditor editor(container);
    QVERIFY(insertGeneratedRun(editor, 1000, container->getAppendIndex(root)));

    // The editor shows the closing operators only in the container it builds
    for (const quint64 id : unclosedIdentifiers(container))
    {
        QVERIFY(editor.getNode(PDFContentNodeId{ id })->getSuffix().isEmpty());
    }

    const PDFContentContainerPointer appended = editor.build();
    QCOMPARE(describe(appended), appendedTree);
    QVERIFY(unclosedIdentifiers(appended).isEmpty());
    QVERIFY(!appended->isUnchanged());

    // The closing operators are operations of the editor
    for (size_t i = appended->getOperations().getOriginalCount(); i < appended->getOperations().getCount(); ++i)
    {
        QVERIFY(appended->getOperations().getOperation(i).getFlags().testFlag(PDFContentOperation::Generated));
    }

    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*appended);
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content, appendedContent);

    // The container before the change is as it was (undo gives it back)
    QCOMPARE(describe(container), tree);
    QCOMPARE(int(unclosedIdentifiers(container).size()), unclosedCount);
    QVERIFY(container->isUnchanged());

    // The appended node is removed again by another change: the nodes stay closed (they were
    // changed), the closing operators are not written twice
    PDFContentContainerEditor secondEditor(appended);
    QVERIFY(secondEditor.removeNode(PDFContentNodeId{ 1000 }));
    QVERIFY(insertGeneratedRun(secondEditor, 1001, appended->getAppendIndex(root) - 1));
    const PDFContentContainerPointer second = secondEditor.build();
    QVERIFY(unclosedIdentifiers(second).isEmpty());
    QCOMPARE(second->getOperations().getCount(), appended->getOperations().getCount() + 1);

    const PDFContentStreamWriteResult secondWritten = PDFContentStreamWriter::write(*second);
    QVERIFY(secondWritten.isSuccess());
    QCOMPARE(secondWritten.content, appendedContent);
}

void ContentTreeTest::unclosedNodesKept()
{
    // What does not make the model close an unclosed node: nothing is written behind its end
    const PDFContentNodeId root{ 1 };

    // re0 f1 q2 w3: Root{Path(0:2) Group(2:1){StateRun(3:1)}}
    const QByteArray content("0 0 5 5 re f q 1 w");
    const PDFDocument document = createDocument(content);
    const PDFContentContainerPointer container = buildPage(document).container;
    QCOMPARE(describe(container), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1)}}"));
    const PDFContentNodeId group = nodeAt(container, { 1 })->getId();
    QVERIFY(container->isUnclosed(group));
    QVERIFY(!container->isUnclosed(root));
    QVERIFY(!container->isUnclosed(nodeAt(container, { 0 })->getId()));
    QVERIFY(!container->isUnclosed(nodeAt(container, { 1, 0 })->getId()));     // A node inside of an unclosed node is not unclosed by that
    QVERIFY(!container->isUnclosed(PDFContentNodeId{ 99 }));
    QVERIFY(!container->isUnclosed(PDFContentNodeId()));

    {
        // An editor, which changes nothing
        PDFContentContainerEditor editor(container);
        const PDFContentContainerPointer same = editor.build();
        QVERIFY(same->isUnclosed(group));
        QVERIFY(same->isUnchanged());
    }

    {
        // New content before the group: "3 w" is the operation 4
        PDFContentContainerEditor editor(container);
        QVERIFY(insertGeneratedRun(editor, 1000, 0));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{StateRun(4:1) Path(0:2) Group(2:1){StateRun(3:1)}}"));
        QVERIFY(changed->isUnclosed(group));
    }

    {
        // New content inside of the group, at its end: it is the last content of the page, the
        // group stays open behind it as it was
        PDFContentContainerEditor editor(container);
        const size_t operationIndex = editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::SetLineWidth, "3 w", PDFContentOperation::Operands(), { }));
        PDFContentNode::Data data = nodeData(PDFContentNodeKind::StateRun, 1000);
        data.prefix = PDFContentOperationSpan{ operationIndex, 1 };
        QVERIFY(editor.insertNode(data, group, container->getAppendIndex(group)));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1) StateRun(4:1)}}"));
        QVERIFY(changed->isUnclosed(group));

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray("0 0 5 5 re f q 1 w\n3 w"));
    }

    {
        // The group moved before the path: the path is written behind it, so the group gets its
        // Q (the operation 4). The first bytes are those of the operation q as it was read,
        // with the space before it.
        PDFContentContainerEditor editor(container);
        QVERIFY(editor.moveNode(group, root, 0));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{Group(2:1/4:1){StateRun(3:1)} Path(0:2)}"));
        QVERIFY(!changed->isUnclosed(group));

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray(" q 1 w\nQ\n0 0 5 5 re f"));
    }

    {
        // The unclosed group moved before a closed group: the closed group is written behind it,
        // so it gets its Q (the operation 6). q0 re1 f2 Q3 q4 w5.
        const PDFDocument groupsDocument = createDocument("q 0 0 5 5 re f Q q 1 w");
        const PDFContentContainerPointer groupsContainer = buildPage(groupsDocument).container;
        QCOMPARE(describe(groupsContainer), QStringLiteral("Root{Group(0:1/3:1){Path(1:2)} Group(4:1){StateRun(5:1)}}"));
        const PDFContentNodeId openGroup = nodeAt(groupsContainer, { 1 })->getId();

        PDFContentContainerEditor editor(groupsContainer);
        QVERIFY(editor.moveNode(openGroup, root, 0));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{Group(4:1/6:1){StateRun(5:1)} Group(0:1/3:1){Path(1:2)}}"));

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray(" q 1 w\nQ\nq 0 0 5 5 re f Q"));
    }

    {
        // A wrap of an unclosed text object (an override, also an empty one): the Q of the
        // wrap must not come inside of the text object (8.2, Figure 9), so the text object gets
        // its ET. BT0 Tf1 Td2 Tj3, ET is the operation 4.
        const QByteArray textContent("BT /F1 12 Tf 10 10 Td (A) Tj");
        const PDFDocument textDocument = createDocument(textContent);
        const PDFContentContainerPointer textContainer = buildPage(textDocument).container;
        QCOMPARE(describe(textContainer), QStringLiteral("Root{TextObject(0:1){StateRun(1:2) TextShow(3:1)}}"));
        const PDFContentNodeId textObject = nodeAt(textContainer, { 0 })->getId();
        QVERIFY(textContainer->isUnclosed(textObject));

        PDFContentContainerEditor editor(textContainer);
        PDFContentNode::Data data = textContainer->getNode(textObject)->getData();
        data.override = PDFContentStateOverride();
        QVERIFY(editor.setNodeData(textObject, data));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:2) TextShow(3:1)}}"));
        QVERIFY(!changed->isUnclosed(textObject));

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray("q\nBT /F1 12 Tf 10 10 Td (A) Tj\nET\nQ"));
    }

    {
        // The same with an unclosed marked-content sequence inside of the text object: the ET
        // of the text object is written behind the sequence, so the sequence gets its EMC too.
        // BT0 Tf1 BMC2 Tj3; ET is the operation 4, EMC the operation 5.
        const PDFDocument markedDocument = createDocument("BT /F1 12 Tf /A BMC (A) Tj");
        const PDFContentContainerPointer markedContainer = buildPage(markedDocument).container;
        QCOMPARE(describe(markedContainer), QStringLiteral("Root{TextObject(0:1){StateRun(1:1) MarkedContent(2:1){TextShow(3:1)}}}"));
        const PDFContentNodeId textObject = nodeAt(markedContainer, { 0 })->getId();
        QCOMPARE(int(unclosedIdentifiers(markedContainer).size()), 2);

        PDFContentContainerEditor editor(markedContainer);
        PDFContentNode::Data data = markedContainer->getNode(textObject)->getData();
        data.override = PDFContentStateOverride();
        QVERIFY(editor.setNodeData(textObject, data));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:1) MarkedContent(2:1/5:1){TextShow(3:1)}}}"));
        QVERIFY(unclosedIdentifiers(changed).isEmpty());

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray("q\nBT /F1 12 Tf /A BMC (A) Tj\nEMC\nET\nQ"));
    }

    {
        // An unclosed text object taken out of its unclosed group and put before it: the q of
        // the group is written behind the text object, which gets its ET (the operation 4); the
        // group is the last content now and stays open. q0 BT1 Tf2 Tj3.
        const PDFDocument nestedDocument = createDocument("q BT /F1 12 Tf (A) Tj");
        const PDFContentContainerPointer nestedContainer = buildPage(nestedDocument).container;
        QCOMPARE(describe(nestedContainer), QStringLiteral("Root{Group(0:1){TextObject(1:1){StateRun(2:1) TextShow(3:1)}}}"));
        const PDFContentNodeId nestedGroup = nodeAt(nestedContainer, { 0 })->getId();
        const PDFContentNodeId nestedText = nodeAt(nestedContainer, { 0, 0 })->getId();

        PDFContentContainerEditor editor(nestedContainer);
        QVERIFY(editor.moveNode(nestedText, root, 0));
        const PDFContentContainerPointer changed = editor.build();
        QCOMPARE(describe(changed), QStringLiteral("Root{TextObject(1:1/4:1){StateRun(2:1) TextShow(3:1)} Group(0:1)}"));
        QVERIFY(!changed->isUnclosed(nestedText));
        QVERIFY(changed->isUnclosed(nestedGroup));

        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*changed);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, QByteArray(" BT /F1 12 Tf (A) Tj\nET\nq"));
    }

    {
        // The rest after an inline image without an end inside of an unclosed group: whatever
        // is written behind it, is image data - a Q too. The group is never closed: new content
        // goes before it (getAppendIndex), and a node put behind it nevertheless changes nothing
        // of it. re0 f1 q2 w3 BI4 ID5 abcd6 Q7 (the last three are read in the image data).
        const PDFDocument imageDocument = createDocument("0 0 5 5 re f q 1 w BI /W 2 /H 2 ID abcd Q");
        const PDFContentContainerPointer imageContainer = buildPage(imageDocument).container;
        QCOMPARE(describe(imageContainer), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1) Opaque(4:4)}}"));
        const PDFContentNodeId imageGroup = nodeAt(imageContainer, { 1 })->getId();
        QVERIFY(imageContainer->isUnclosed(imageGroup));

        PDFContentContainerEditor editor(imageContainer);
        QVERIFY(insertGeneratedRun(editor, 1000, imageContainer->getAppendIndex(root)));
        const PDFContentContainerPointer before = editor.build();
        QCOMPARE(describe(before), QStringLiteral("Root{Path(0:2) StateRun(8:1) Group(2:1){StateRun(3:1) Opaque(4:4)}}"));
        QVERIFY(before->isUnclosed(imageGroup));

        PDFContentContainerEditor behindEditor(imageContainer);
        QVERIFY(insertGeneratedRun(behindEditor, 1000, -1));
        const PDFContentContainerPointer behind = behindEditor.build();
        QCOMPARE(describe(behind), QStringLiteral("Root{Path(0:2) Group(2:1){StateRun(3:1) Opaque(4:4)} StateRun(8:1)}"));
        QVERIFY(behind->isUnclosed(imageGroup));
    }
}

void ContentTreeTest::unreadableFormMatrix()
{
    // /Matrix of a form XObject and of a pattern is "an array of six numbers" (ISO 32000-2,
    // 8.10.2, Table 93; 8.7.3.1, Table 74). A value, which is none, is refused by the reader of
    // the project (the Do of such a form fails). Whatever fails, every byte of the content
    // stays in an operation: the page is written back as it was.
    //
    // q0 Do1 Q2 re3 f4
    const QByteArray content("q /Fm0 Do Q 0 0 5 5 re f");

    struct Row
    {
        const char* name;
        PDFObject matrix;
    };

    const std::vector<Row> rows =
    {
        { "five numbers", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 0 }) },
        { "seven numbers", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 0, 0, 0 }) },
        { "no number", Documents::createIntegerArrayObject({ }) },
        { "a name among the numbers", Documents::createArrayObject({ PDFObject::createInteger(1), PDFObject::createInteger(0), PDFObject::createInteger(0),
                                                                     PDFObject::createInteger(1), PDFObject::createInteger(0), PDFObject::createName("zero") }) },
        { "a name", PDFObject::createName("Identity") }
    };

    for (const Row& row : rows)
    {
        const PDFObject matrix = row.matrix;
        const PDFDocument document = Documents::createDocument(content, [matrix](PDFDocumentBuilder* builder)
        {
            PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                   { "Subtype", PDFObject::createName("Form") },
                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                   { "Matrix", matrix } }, QByteArray("0 0 5 5 re f"));
            PDFObject pattern = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                                      { "PatternType", PDFObject::createInteger(1) },
                                                                      { "PaintType", PDFObject::createInteger(1) },
                                                                      { "TilingType", PDFObject::createInteger(1) },
                                                                      { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                      { "XStep", PDFObject::createInteger(10) },
                                                                      { "YStep", PDFObject::createInteger(10) },
                                                                      { "Resources", Documents::createDictionaryObject({ }) },
                                                                      { "Matrix", matrix } }, QByteArray("0 0 5 5 re f"));

            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm0", std::move(form) } }));
            resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P0", std::move(pattern) } }));
            return resources;
        });

        const PDFContentBuildResult result = buildPage(document);
        QVERIFY2(result.container != nullptr, row.name);

        // The Do is an operation of the content, an opaque node (nothing is known about what
        // it paints) with the reason. The builder never enters a form, so its processor does
        // not refuse the Do: the operation is not flagged and there is no issue, as for an
        // XObject of an unknown type.
        QVERIFY2(describe(result.container) == QStringLiteral("Root{Group(0:1/2:1){Opaque(1:1)} Path(3:2)}"), qPrintable(QString::fromLatin1(row.name) + QStringLiteral(": ") + describe(result.container)));

        const PDFContentOperationList& operations = result.container->getOperations();
        QCOMPARE(operations.getOriginalCount(), size_t(5));
        QCOMPARE(operations.getOriginalContent(), content);
        QVERIFY(operations.getOperation(1).getOperator() == PDFContentOperator::PaintXObject);
        QCOMPARE(operations.getOperation(1).getRawBytes(), QByteArray(" /Fm0 Do"));
        QCOMPARE(operations.getOperation(2).getRawBytes(), QByteArray(" Q"));

        const PDFContentNodePointer opaque = nodeAt(result.container, { 0, 0 });
        QVERIFY(!opaque->getData().opaqueReason.isEmpty());
        QVERIFY(opaque->getLeak().isUnknown);
        QVERIFY(result.issues.empty());

        // The form cannot be read, so it is no container the page uses
        QVERIFY(result.usedForms.empty());

        QVERIFY(result.container->isUnchanged());
        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, content);

        // A change of the page keeps the Do: the path is removed, the rest is written
        PDFContentContainerEditor editor(result.container);
        QVERIFY(editor.removeNode(nodeAt(result.container, { 1 })->getId()));
        const PDFContentStreamWriteResult changedWritten = PDFContentStreamWriter::write(*editor.build());
        QVERIFY(changedWritten.isSuccess());
        QCOMPARE(changedWritten.content, QByteArray("q /Fm0 Do Q"));

        // The key of the form and of the pattern: no exception (the worker of a session has
        // nothing, which would catch one), no container, the issue says why
        const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(pageResource(document, "XObject", "Fm0").getReference());
        const PDFContentBuildResult formResult = build(document, formKey, false, nullptr, 0);
        QVERIFY2(formResult.container == nullptr, row.name);
        QVERIFY(!formResult.isCanceled);
        QCOMPARE(issueKinds(formResult.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
        QVERIFY(!formResult.issues.front().text.isEmpty());
        QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, formKey).isEmpty());

        const PDFContentContainerKey patternKey = PDFContentContainerKey::createPatternCell(pageResource(document, "Pattern", "P0").getReference());
        const PDFContentBuildResult patternResult = build(document, patternKey, false, nullptr, 0);
        QVERIFY2(patternResult.container == nullptr, row.name);
        QCOMPARE(issueKinds(patternResult.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
        QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, patternKey).isEmpty());
    }
}

void ContentTreeTest::dashLimit_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");
    QTest::addColumn<int>("issueOperation");     // The operation of the issue, -1 = no issue

    // The states of the nodes never hold a dash array of more than 128 elements (the cost of
    // comparing and copying the states must not grow with a size the content chooses): the
    // content, in which a longer array is in effect, is one opaque node - from the operation,
    // which sets it (or from the outermost open node around it), up to the operation, after which
    // the state is within the limit again and every bracket opened in between is closed.
    auto dash = [](int count)
    {
        QByteArray array("[");
        for (int i = 0; i < count; ++i)
        {
            array += "3 ";
        }
        return array + "]";
    };

    // w0 d1 re2 S3 d4 w5 re6 S7: the solid line of the operation 4 ends the opaque node
    QTest::newRow("operator d") << QByteArray("3 w ") + dash(130) + QByteArray(" 0 d 0 0 5 5 re S [] 0 d 2 w 0 0 5 5 re S")
                                << QStringLiteral("Root{StateRun(0:1) Opaque(1:4) StateRun(5:1) Path(6:2)}") << 1;

    // 128 elements are within the limit: d0 re1 S2
    QTest::newRow("at the limit") << dash(128) + QByteArray(" 0 d 0 0 5 5 re S")
                                  << QStringLiteral("Root{StateRun(0:1) Path(1:2)}") << -1;

    // q0 d1 re2 S3 Q4 re5 f6: the opaque node is the whole group - Q brings the dash pattern
    // before q back
    QTest::newRow("inside of a group") << QByteArray("q ") + dash(130) + QByteArray(" 0 d 0 0 5 5 re S Q 0 0 5 5 re f")
                                       << QStringLiteral("Root{Opaque(0:5) Path(5:2)}") << 1;

    // gs0 re1 S2: /GSD has 200 elements; nothing ends the pattern
    QTest::newRow("gs") << QByteArray("/GSD gs 0 0 5 5 re S")
                        << QStringLiteral("Root{Opaque(0:3)}") << 0;

    // BT0 d1 Tf2 Tj3 ET4 re5 f6: ET closes the text object, but the pattern stays
    QTest::newRow("inside of a text object") << QByteArray("BT ") + dash(130) + QByteArray(" 0 d /F1 12 Tf (A) Tj ET 0 0 5 5 re f")
                                             << QStringLiteral("Root{Opaque(0:7)}") << 1;

    // w0 J1 d2 j3 re4 S5: the state operators before d are a state run of their own
    QTest::newRow("in a state run") << QByteArray("1 w 2 J ") + dash(130) + QByteArray(" 0 d 1 j 0 0 5 5 re S")
                                    << QStringLiteral("Root{StateRun(0:2) Opaque(2:4)}") << 2;
}

void ContentTreeTest::dashLimit()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);
    QFETCH(int, issueOperation);

    using Property = PDFContentStateProperty;
    constexpr size_t MAXIMUM_DASH_LENGTH = 128;

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), tree);

    if (issueOperation >= 0)
    {
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::OperandLimit }));
        QCOMPARE(result.issues.front().operationIndex, size_t(issueOperation));
    }
    else
    {
        QVERIFY(result.issues.empty());
    }

    // No state of the container holds an array over the limit
    auto isBounded = [](const PDFContentResolvedStatePointer& state)
    {
        return !state || state->getState().getLineDashPattern().getDashArray().size() <= MAXIMUM_DASH_LENGTH;
    };

    QVERIFY(isBounded(result.container->getInitialState()));
    for (const quint64 id : identifiers(result.container->getRoot()))
    {
        const PDFContentNodePointer node = result.container->getNode(PDFContentNodeId{ id });
        QVERIFY(node->getState() != nullptr);
        QVERIFY(isBounded(node->getState()));
        QVERIFY(isBounded(node->getLeak().endState));

        if (node->getKind() == PDFContentNodeKind::Opaque)
        {
            QVERIFY(node->getLeak().isUnknown);
        }
    }

    // An opaque node, which lasts to the end of the content (in these rows the long pattern is
    // in effect there): the state behind it is the state of the end of the content, but its
    // dash pattern equals no pattern a node can have - neither the solid line nor a pattern
    // within the limit - so that content placed behind it sets its own pattern (R-2). The other
    // properties are those of the end of the content.
    const PDFContentNodePointer last = result.container->getRoot()->getChildren().back();
    if (last->getKind() == PDFContentNodeKind::Opaque)
    {
        const PDFContentResolvedStatePointer& endState = last->getLeak().endState;
        QVERIFY(endState != nullptr);
        QVERIFY(last->getLeak().endSymbolicState != nullptr);

        PDFPageContentProcessorState solid = endState->getState();
        solid.setLineDashPattern(PDFLineDashPattern());
        PDFPageContentProcessorState atLimit = endState->getState();
        PDFLineDashPattern atLimitPattern;
        atLimitPattern.setDashArray(std::vector<PDFReal>(MAXIMUM_DASH_LENGTH, 3.0));
        atLimit.setLineDashPattern(atLimitPattern);
        PDFPageContentProcessorState itself = endState->getState();

        const std::vector<Property> dashOnly = { Property::DashPattern };
        QVERIFY(PDFContentResolvedState::getDifference(solid, endState->getState()).getProperties() == dashOnly);
        QVERIFY(PDFContentResolvedState::getDifference(atLimit, endState->getState()).getProperties() == dashOnly);
        QVERIFY(PDFContentResolvedState::getDifference(itself, endState->getState()).getProperties() == dashOnly);
    }

    // The content is an operation each, written back byte for byte
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->isUnchanged());
    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content, content);

    const QByteArray tag(QTest::currentDataTag());
    if (tag == "operator d")
    {
        // The state run before the pattern says only what it does itself (the width 3 instead
        // of the initial 1); the opaque node begins in the state behind the run; the node after
        // the opaque one has the state of its place: the solid line and the width 2
        const std::vector<Property> leaked = { Property::LineWidth };
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == leaked);

        const PDFPageContentProcessorState& opaqueState = nodeAt(result.container, { 1 })->getState()->getState();
        QCOMPARE(opaqueState.getLineWidth(), 3.0);
        QVERIFY(opaqueState.getLineDashPattern().isSolid());

        const PDFPageContentProcessorState& pathState = nodeAt(result.container, { 3 })->getState()->getState();
        QCOMPARE(pathState.getLineWidth(), 2.0);
        QVERIFY(pathState.getLineDashPattern().isSolid());
    }
    else if (tag == "at the limit")
    {
        QCOMPARE(nodeAt(result.container, { 1 })->getState()->getState().getLineDashPattern().getDashArray().size(), MAXIMUM_DASH_LENGTH);
        const std::vector<Property> leaked = { Property::DashPattern };
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == leaked);
    }
    else if (tag == "inside of a group")
    {
        // Behind the group the state is the one before its q, also the state, which the opaque
        // node gives the place behind it (the pattern ended with Q, inside of the content)
        QVERIFY(nodeAt(result.container, { 1 })->getState()->getState().getLineDashPattern().isSolid());
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().endState->getState().getLineDashPattern().isSolid());
        QVERIFY(PDFContentResolvedState::getDifference(*nodeAt(result.container, { 0 })->getLeak().endState, *nodeAt(result.container, { 1 })->getState()).isEmpty());
    }
    else if (tag == "gs")
    {
        // /GSD sets the dash pattern only: the end differs from the beginning by it alone
        const std::vector<Property> dashOnly = { Property::DashPattern };
        QVERIFY(PDFContentResolvedState::getDifference(*last->getState(), *last->getLeak().endState).getProperties() == dashOnly);
    }
    else if (tag == "inside of a text object")
    {
        // Tf inside of the span: the end has the font of the content, not the one before the span
        const std::vector<Property> leaked = { Property::DashPattern, Property::Font };
        QVERIFY(PDFContentResolvedState::getDifference(*last->getState(), *last->getLeak().endState).getProperties() == leaked);
        QCOMPARE(last->getLeak().endState->getState().getTextFontSize(), 12.0);

        // A resolved state has no text matrices of its own (pdfcontentstate.h)
        QVERIFY(last->getLeak().endState->getState().getTextMatrix().isIdentity());
        QVERIFY(last->getLeak().endState->getState().getTextLineMatrix().isIdentity());
    }
}

void ContentTreeTest::dashLimitCost()
{
    // A dash array of 90 000 elements (below the limit of the operands of one operation, so d
    // is an ordinary state operator) and 500 000 operations after it. A builder, which compares
    // the whole state after every operation, compares 90 000 numbers 500 000 times: 4.5 x 10^10
    // comparisons from 1.2 MB of content. The build is canceled at the time limit, so such
    // a builder fails here instead of running on.
    constexpr int OPERATION_COUNT = 500000;

    QByteArray content("[");
    for (int i = 0; i < 90000; ++i)
    {
        content += "1 ";
    }
    content += "] 0 d";
    for (int i = 0; i < OPERATION_COUNT; ++i)
    {
        content += " n";
    }

    const PDFDocument document = createDocument(content);
    const ContentTreeTestDeadline deadline(timeLimit(15000));
    const PDFContentBuildResult result = build(document, pageKey(document), false, nullptr, -1, &deadline);
    QVERIFY2(!result.isCanceled, "The build did not end within the time limit");
    QVERIFY(result.container != nullptr);

    // d0 and the 500 000 n: one opaque node
    QCOMPARE(describe(result.container), QStringLiteral("Root{Opaque(0:500001)}"));
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->getInitialState()->getState().getLineDashPattern().isSolid());
    QVERIFY(nodeAt(result.container, { 0 })->getState()->getState().getLineDashPattern().isSolid());

    // Not one state was made for the 500 001 operations: the node begins in the initial state of
    // the content; the state behind it is made once, at the end of the content, and holds no long
    // array (its dash pattern is a marker of two elements, which equals no pattern)
    QVERIFY(nodeAt(result.container, { 0 })->getState() == result.container->getInitialState());
    const PDFContentResolvedStatePointer endState = nodeAt(result.container, { 0 })->getLeak().endState;
    QVERIFY(endState != nullptr);
    QVERIFY(endState->getState().getLineDashPattern().getDashArray().size() <= 2);
    QVERIFY(PDFContentResolvedState::getDifference(result.container->getInitialState()->getState(), endState->getState()).contains(PDFContentStateProperty::DashPattern));
}

void ContentTreeTest::dashLimitOfInitialState()
{
    // The initial state of a form is given by the caller: the state of the place of its first
    // use. A state with a dash array over the limit - no builder makes one, a caller can - is
    // not taken into the states of the form either: the initial state of the container has
    // a solid line, and the content of the form, in which the pattern stays in effect, is one
    // opaque node. The form /Fm1 is "/GF gs 0 1 0 rg 5 5 20 10 re f": gs0 rg1 re2 f3.
    const PDFDocument document = createDocument("/Fm1 Do");
    const PDFContentBuildResult pageResult = buildPage(document);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{FormUse(0:1)}"));
    const PDFContentNodePointer use = nodeAt(pageResult.container, { 0 });

    PDFPageContentProcessorState state = use->getState()->getState();
    PDFLineDashPattern pattern;
    pattern.setDashArray(std::vector<PDFReal>(200, 3.0));
    state.setLineDashPattern(pattern);
    const PDFContentResolvedStatePointer initialState = std::make_shared<const PDFContentResolvedState>(state);

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(use->getData().xobject);
    const PDFContentBuildResult result = build(document, formKey, false, initialState, 0);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Opaque(0:4)}"));
    QCOMPARE(result.container->getOperations().getOriginalContent(), Documents::formContent());
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::OperandLimit }));
    QCOMPARE(result.issues.front().operationIndex, size_t(0));

    QVERIFY(result.container->getInitialState()->getState().getLineDashPattern().isSolid());
    QVERIFY(result.container->getRoot()->getState()->getState().getLineDashPattern().isSolid());
    QVERIFY(nodeAt(result.container, { 0 })->getState()->getState().getLineDashPattern().isSolid());

    // The form does not set the pattern: it is still in effect at its end, so the state behind
    // the node has the marker, which equals no pattern (a solid line neither)
    const PDFContentResolvedStatePointer endState = nodeAt(result.container, { 0 })->getLeak().endState;
    QVERIFY(endState != nullptr);
    QVERIFY(endState->getState().getLineDashPattern().getDashArray().size() <= 2);
    QVERIFY(PDFContentResolvedState::getDifference(nodeAt(result.container, { 0 })->getState()->getState(), endState->getState()).contains(PDFContentStateProperty::DashPattern));

    // The same form in a state within the limit is read as ever
    const PDFContentBuildResult plainResult = build(document, formKey, false, use->getState(), 0);
    QCOMPARE(describe(plainResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(plainResult.issues.empty());
}

void ContentTreeTest::dashLimitOfInitialStateBeforeAnyOperator_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QString>("tree");

    // The initial state of the form has a dash array over the limit: the content is opaque from
    // its first operation, whatever operator it has (not only from a state operator), up to the
    // operation, after which the state is within the limit again and every bracket is closed
    QTest::newRow("path") << QByteArray("0 0 5 5 re S") << QStringLiteral("Root{Opaque(0:2)}");
    QTest::newRow("path, then a state operator") << QByteArray("0 0 5 5 re S 1 w 0 0 5 5 re S") << QStringLiteral("Root{Opaque(0:5)}");
    QTest::newRow("group") << QByteArray("q 0 0 5 5 re S Q") << QStringLiteral("Root{Opaque(0:4)}");
    QTest::newRow("text object") << QByteArray("BT 2 Tc ET") << QStringLiteral("Root{Opaque(0:3)}");

    // d0 re1 S2: the solid line of the first operation ends the opaque node at once
    QTest::newRow("solid line first") << QByteArray("[] 0 d 0 0 5 5 re S") << QStringLiteral("Root{Opaque(0:1) Path(1:2)}");
}

void ContentTreeTest::dashLimitOfInitialStateBeforeAnyOperator()
{
    QFETCH(QByteArray, content);
    QFETCH(QString, tree);

    const PDFDocument document = Documents::createDocument(QByteArray("/Fm0 Do"), [content](PDFDocumentBuilder* builder)
    {
        PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                               { "Subtype", PDFObject::createName("Form") },
                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, content);
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm0", std::move(form) } }));
        return resources;
    });

    const PDFContentBuildResult pageResult = buildPage(document);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{FormUse(0:1)}"));
    const PDFContentNodePointer use = nodeAt(pageResult.container, { 0 });

    PDFPageContentProcessorState state = use->getState()->getState();
    PDFLineDashPattern pattern;
    pattern.setDashArray(std::vector<PDFReal>(200, 3.0));
    state.setLineDashPattern(pattern);
    const PDFContentResolvedStatePointer initialState = std::make_shared<const PDFContentResolvedState>(state);

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(use->getData().xobject);
    const PDFContentBuildResult result = build(document, formKey, false, initialState, 0);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), tree);
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::OperandLimit }));
    QCOMPARE(result.issues.front().operationIndex, size_t(0));

    // No state of the container holds the long array; the content is written back byte for byte
    for (const quint64 id : identifiers(result.container->getRoot()))
    {
        const PDFContentNodePointer node = result.container->getNode(PDFContentNodeId{ id });
        QVERIFY(node->getState()->getState().getLineDashPattern().getDashArray().size() <= 128);
        QVERIFY(!node->getLeak().endState || node->getLeak().endState->getState().getLineDashPattern().getDashArray().size() <= 128);
    }

    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content, content);
}

void ContentTreeTest::dashLimitBehind_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QByteArray>("expected");   // White space collapsed

    auto dash = [](int count)
    {
        QByteArray array("[");
        for (int i = 0; i < count; ++i)
        {
            array += "3 ";
        }
        return array + "]";
    };

    // The first path of every content strokes a solid line of width 1 with butt caps (the
    // initial state, ISO 32000-2, Table 51). It is copied to the end of the content (the place
    // getAppendIndex of the root), behind content which ends with a dash pattern of more than
    // 128 elements in effect (an opaque node). A reader strokes the copy in the state there, so
    // the copy must set what its own state has and the place has not (R-2): the solid line
    // [] 0 d at least, in a wrap q ... Q (R-1), whose properties come in the order of
    // PDFContentStateProperty (w, J, d).
    const QByteArray path("0 0 5 5 re S");
    const QByteArray wrapped("q [] 0 d 0 0 5 5 re S Q");

    QTest::newRow("gs") << path + " /GSD gs " + path
                        << path + " /GSD gs " + path + " " + wrapped;

    const QByteArray operatorContent = path + " " + dash(130) + " 0 d " + path;
    QTest::newRow("operator d") << operatorContent
                                << operatorContent + " " + wrapped;

    // The width and the cap set behind the pattern are a part of the state at the end too
    QTest::newRow("other properties") << path + " /GSD gs 2 w 1 J " + path
                                      << path + " /GSD gs 2 w 1 J " + path + " q 1 w 0 J [] 0 d 0 0 5 5 re S Q";

    // A group without its Q: the processor restores the state before q at the end of the content,
    // but the copy is written behind the last operation, inside of the group (the opaque node
    // holds the open q), where the width 2 and the long pattern are in effect
    QTest::newRow("unclosed group") << path + " q /GSD gs 2 w " + path
                                    << path + " q /GSD gs 2 w " + path + " q 1 w [] 0 d 0 0 5 5 re S Q";

    const QByteArray textContent = path + " BT " + dash(130) + " 0 d /F1 12 Tf (A) Tj ET";
    QTest::newRow("inside of a text object") << textContent
                                             << textContent + " " + wrapped;

    // The solid line again: the state behind the opaque node is the one of the copy, nothing
    // is set (no wrap)
    const QByteArray endedContent = path + " " + dash(130) + " 0 d " + path + " [] 0 d";
    QTest::newRow("pattern ended") << endedContent
                                   << endedContent + " " + path;

    // The same inside of a group, which the content does not close: the opaque node lasts to the
    // end (it holds the open q), but the pattern there is the solid line again
    const QByteArray openContent = path + " " + dash(130) + " 0 d q [] 0 d";
    QTest::newRow("pattern ended inside of an open group") << openContent
                                                           << openContent + " " + path;
}

void ContentTreeTest::dashLimitBehind()
{
    QFETCH(QByteArray, content);
    QFETCH(QByteArray, expected);

    // A copy by the command of the core (the bake of R-2 compares the state of the copy with
    // the state, which the model gives the place behind the last node)
    PDFDocumentPointer document(new PDFDocument(createDocument(content)));
    PDFContentEditSession session(document, PDFCMSPointer(), nullptr);
    const PDFContentContainerKey key = session.getPageKey(0);
    QString errorText;
    QVERIFY2(session.loadContainer(key, nullptr, &errorText), qPrintable(errorText));

    const PDFContentContainerPointer before = session.getContainer(key);
    QVERIFY(before->getRoot()->getChildren().size() >= 2);
    const PDFContentNodePointer first = before->getRoot()->getChildren().front();
    QVERIFY(first->getKind() == PDFContentNodeKind::Path);

    const PDFContentCommandPointer command = PDFContentCommandFactory::createCopy({ PDFContentNodeReference{ key, first->getId() } },
                                                                                   PDFContentNodeReference{ key, before->getRoot()->getId() },
                                                                                   -1, QTransform());
    QVERIFY2(session.execute(command, &errorText), qPrintable(errorText));

    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*session.getContainer(key));
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content.simplified(), expected);
}

void ContentTreeTest::stateObjects()
{
    using Property = PDFContentStateProperty;
    using State = PDFPageContentProcessorState;
    using Properties = std::vector<Property>;

    // PDFContentResolvedState::getDifference, the objects of a state (the functions of black
    // generation, undercolor removal and transfer, the halftone): the same object of the
    // document is equal without being read; two different objects are compared by value only as
    // far as a small budget goes (pdfcontentstate.h) and count as different beyond it.
    const State base;

    auto difference = [&base](const PDFObject& left, const PDFObject& right)
    {
        State leftState = base;
        State rightState = base;
        leftState.setTransferFunction(left);
        rightState.setTransferFunction(right);
        return PDFContentResolvedState::getDifference(leftState, rightState).getProperties();
    };

    const Properties none;
    const Properties transfer = { Property::Transfer };

    // Names
    QVERIFY(difference(PDFObject::createName("Identity"), PDFObject::createName("Identity")) == none);
    QVERIFY(difference(PDFObject::createName("Identity"), PDFObject::createName("Default")) == transfer);
    QVERIFY(difference(PDFObject::createName("Identity"), PDFObject()) == transfer);

    // A name longer than a name stored in the object itself, in two objects; two different
    // objects with the same name of 1 000 bytes are beyond what is compared
    const QByteArray longName(100, 'n');
    QVERIFY(difference(PDFObject::createName(longName), PDFObject::createName(longName)) == none);
    QVERIFY(difference(PDFObject::createName(longName), PDFObject::createName(QByteArray(100, 'm'))) == transfer);

    const PDFObject veryLongName = PDFObject::createName(QByteArray(1000, 'n'));
    QVERIFY(difference(veryLongName, veryLongName) == none);
    QVERIFY(difference(veryLongName, PDFObject::createName(QByteArray(1000, 'n'))) == transfer);

    // Two small arrays with the same values (an array of four functions given by references)
    auto references = [](PDFInteger last)
    {
        return Documents::createArrayObject({ PDFObject::createReference(PDFObjectReference(5, 0)), PDFObject::createReference(PDFObjectReference(6, 0)),
                                              PDFObject::createReference(PDFObjectReference(7, 0)), PDFObject::createReference(PDFObjectReference(last, 0)) });
    };
    QVERIFY(difference(references(8), references(8)) == none);
    QVERIFY(difference(references(8), references(9)) == transfer);
    QVERIFY(difference(references(8), Documents::createArrayObject({ PDFObject::createReference(PDFObjectReference(5, 0)) })) == transfer);

    // Two small dictionaries (a halftone dictionary of type 1), compared entry by entry
    auto halftone = [](PDFInteger frequency)
    {
        return Documents::createDictionaryObject({ { "HalftoneType", PDFObject::createInteger(1) },
                                                   { "Frequency", PDFObject::createInteger(frequency) },
                                                   { "Angle", PDFObject::createInteger(45) },
                                                   { "SpotFunction", PDFObject::createName("Round") } });
    };
    QVERIFY(difference(halftone(60), halftone(60)) == none);
    QVERIFY(difference(halftone(60), halftone(75)) == transfer);
    QVERIFY(difference(Documents::createDictionaryObject({ { "Frequency", PDFObject::createInteger(60) } }),
                       Documents::createDictionaryObject({ { "Angle", PDFObject::createInteger(60) } })) == transfer);    // Another key, the same value

    // A large array: the same object is equal, another object with the same 1 000 values is
    // beyond the budget and counts as different
    const PDFObject largeArray = Documents::createIntegerArrayObject(std::vector<PDFInteger>(1000, 7));
    QVERIFY(difference(largeArray, largeArray) == none);
    QVERIFY(difference(largeArray, Documents::createIntegerArrayObject(std::vector<PDFInteger>(1000, 7))) == transfer);

    // A large dictionary in the same way: 100 entries /K0 ... /K99
    auto largeDictionary = []
    {
        Documents::DictionaryEntries entries;
        for (int i = 0; i < 100; ++i)
        {
            entries.emplace_back(QByteArray("K") + QByteArray::number(i), PDFObject::createInteger(i));
        }
        return Documents::createDictionaryObject(std::move(entries));
    };
    const PDFObject dictionary = largeDictionary();
    QVERIFY(difference(dictionary, dictionary) == none);
    QVERIFY(difference(dictionary, largeDictionary()) == transfer);

    // A stream: the same object is equal; the data of two streams are never compared
    auto stream = [] { return PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), QByteArray("{ }"))); };
    const PDFObject function = stream();
    QVERIFY(difference(function, function) == none);
    QVERIFY(difference(function, stream()) == transfer);

    // The four properties are compared so, each one its own
    {
        State left = base;
        State right = base;
        left.setBlackGenerationFunction(largeArray);
        right.setBlackGenerationFunction(largeArray);
        left.setUndercolorRemovalFunction(largeArray);
        right.setUndercolorRemovalFunction(function);
        left.setHalftone(halftone(60));
        right.setHalftone(halftone(60));
        QVERIFY(PDFContentResolvedState::getDifference(left, right).getProperties() == Properties({ Property::UndercolorRemoval }));
    }

    // The cost: a state and its copy share an array of 1 000 000 numbers; 20 000 comparisons
    // (one after each operation of a content) must not read it - a comparison by value reads
    // 2 x 10^10 numbers
    {
        State left = base;
        left.setTransferFunction(Documents::createIntegerArrayObject(std::vector<PDFInteger>(1000000, 7)));
        const State right = left;

        QElapsedTimer timer;
        timer.start();
        const qint64 limit = timeLimit(5000);

        for (int i = 0; i < 20000; ++i)
        {
            QVERIFY(PDFContentResolvedState::getDifference(left, right).isEmpty());
            if (i % 16 == 15 && timer.elapsed() > limit)
            {
                QFAIL(qPrintable(QStringLiteral("Only %1 of 20000 comparisons were made within %2 ms").arg(i + 1).arg(limit)));
            }
        }
    }

    // The name of the rendering intent, whose length the content chooses, is shared by a state
    // and its copy in the same way
    {
        State left = base;
        left.setRenderingIntentName(QByteArray(8000000, 'r'));
        State right = left;

        QElapsedTimer timer;
        timer.start();
        const qint64 limit = timeLimit(5000);

        for (int i = 0; i < 50000; ++i)
        {
            QVERIFY(PDFContentResolvedState::getDifference(left, right).isEmpty());
            if (i % 16 == 15 && timer.elapsed() > limit)
            {
                QFAIL(qPrintable(QStringLiteral("Only %1 of 50000 comparisons were made within %2 ms").arg(i + 1).arg(limit)));
            }
        }

        // Another name of the same length differs
        right.setRenderingIntentName(QByteArray(8000000, 's'));
        QVERIFY(PDFContentResolvedState::getDifference(left, right).getProperties() == Properties({ Property::RenderingIntent }));
        right.setRenderingIntentName(QByteArray(8000000, 'r'));
        QVERIFY(PDFContentResolvedState::getDifference(left, right).isEmpty());
    }
}

void ContentTreeTest::repeatedContentStream()
{
    // /Contents can name a stream more than once: the content has its bytes at each of the
    // places (ISO 32000-2, 7.8.2: the streams are concatenated, a line feed between them here).
    // re0 f1 | re2 f3 | re4 f5
    const QByteArray streamContent("0 0 5 5 re f");
    const PDFDocument document = Documents::createDocumentFromObjects([&streamContent](PDFDocumentBuilder* builder)
    {
        const PDFObject stream = Documents::addStreamObject(builder, { }, streamContent);
        return Documents::createArrayObject({ stream, stream, stream });
    }, &ContentTreeTest::createResources);

    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QVERIFY(!result.container->isReadOnly());
    QVERIFY(result.issues.empty());
    QCOMPARE(describe(result.container), QStringLiteral("Root{Path(0:2) Path(2:2) Path(4:2)}"));

    const QByteArray content("0 0 5 5 re f\n0 0 5 5 re f\n0 0 5 5 re f");
    const PDFContentOperationList& operations = result.container->getOperations();
    QCOMPARE(operations.getOriginalContent(), content);

    // Each item has its own place: the operation 2 begins at the end of the first stream and
    // ends in the second one, the operation 4 the same one stream further
    QCOMPARE(operations.getOperation(2).getRawBytes(), QByteArray("\n0 0 5 5 re"));
    QVERIFY(operations.getOperation(2).getRange().begin == (PDFContentStreamPosition{ 0, 12 }));
    QVERIFY(operations.getOperation(2).getRange().end == (PDFContentStreamPosition{ 1, 10 }));
    QVERIFY(operations.getOperation(4).getRange().begin == (PDFContentStreamPosition{ 1, 12 }));
    QVERIFY(operations.getOperation(4).getRange().end == (PDFContentStreamPosition{ 2, 10 }));
    QVERIFY(operations.getOperation(5).getRange().end == (PDFContentStreamPosition{ 2, 12 }));

    QVERIFY(result.container->isUnchanged());
    const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
    QVERIFY(written.isSuccess());
    QCOMPARE(written.content, content);

    // Another number of the items is another content (another fingerprint)
    const PDFDocument twiceDocument = Documents::createDocumentFromObjects([&streamContent](PDFDocumentBuilder* builder)
    {
        const PDFObject stream = Documents::addStreamObject(builder, { }, streamContent);
        return Documents::createArrayObject({ stream, stream });
    }, &ContentTreeTest::createResources);
    const PDFContentBuildResult twiceResult = buildPage(twiceDocument);
    QCOMPARE(describe(twiceResult.container), QStringLiteral("Root{Path(0:2) Path(2:2)}"));
    QVERIFY(twiceResult.container->getFingerprint() != result.container->getFingerprint());
}

void ContentTreeTest::contentSizeLimit()
{
    // /Contents names one stream of one megabyte 600 times: a document of about a megabyte (a few
    // kilobytes with a filter) with a content of 600 MB. A container keeps every byte of its
    // content, so the size of a content is limited (512 MB of decoded bytes, each item of
    // /Contents counted): above it the content is not read - the container is empty and
    // read-only, as for a stream, which cannot be decoded; the page is still shown.
    constexpr int ITEM_COUNT = 600;
    const PDFDocument document = Documents::createDocumentFromObjects([](PDFDocumentBuilder* builder)
    {
        QByteArray streamContent(1024 * 1024, ' ');
        streamContent.replace(0, 12, "0 0 5 5 re f");
        const PDFObject stream = Documents::addStreamObject(builder, { }, std::move(streamContent));
        return Documents::createArrayObject(std::vector<PDFObject>(ITEM_COUNT, stream));
    }, &ContentTreeTest::createResources);

    const ContentTreeTestDeadline deadline(timeLimit(30000));
    const PDFContentBuildResult result = build(document, pageKey(document), false, nullptr, -1, &deadline);
    QVERIFY2(!result.isCanceled, "The build did not end within the time limit");
    QVERIFY(result.container != nullptr);

    QVERIFY(result.container->isReadOnly());
    QCOMPARE(describe(result.container), QStringLiteral("Root"));
    QCOMPARE(result.container->getOperations().getCount(), size_t(0));
    QVERIFY(result.container->getOperations().getTail().isEmpty());
    QVERIFY(result.container->getInitialState() != nullptr);
    // The issue has the nearest kind of the header (a content, which is read-only, because its
    // streams are not read) and says the reason (the source text; the tests load no translation)
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UndecodableStream }));
    QVERIFY(!result.issues.front().nodeId.isValid());
    QVERIFY(result.issues.front().text.contains(QStringLiteral("too large")));
    QVERIFY(!result.container->getFingerprint().isEmpty());

    // A read-only container is never written
    QVERIFY(!PDFContentStreamWriter::write(*result.container).isSuccess());

    // Below the limit the same stream is a content as any other: 8 items, 8 paths (the streams
    // are joined by a line feed each)
    const PDFDocument smallDocument = Documents::createDocumentFromObjects([](PDFDocumentBuilder* builder)
    {
        QByteArray streamContent(1024 * 1024, ' ');
        streamContent.replace(0, 12, "0 0 5 5 re f");
        const PDFObject stream = Documents::addStreamObject(builder, { }, std::move(streamContent));
        return Documents::createArrayObject(std::vector<PDFObject>(8, stream));
    }, &ContentTreeTest::createResources);

    const PDFContentBuildResult smallResult = buildPage(smallDocument);
    QVERIFY(smallResult.container != nullptr);
    QVERIFY(!smallResult.container->isReadOnly());
    QVERIFY(smallResult.issues.empty());
    QCOMPARE(smallResult.container->getRoot()->getChildren().size(), size_t(8));
    QCOMPARE(smallResult.container->getOperations().getOriginalCount(), size_t(16));
    QCOMPARE(smallResult.container->getOperations().getOriginalContent().size(), qsizetype(8) * 1024 * 1024 + 7);
}

void ContentTreeTest::contentOverStreams()
{
    // ISO 32000-2, 7.8.2: the streams of /Contents are one content, divided between two tokens.
    // The operands of rg are in the first stream, its operator in the second one: the bytes of
    // the operation are the bytes of both, joined by one line feed.
    const PDFDocument document = Documents::createDocument(QList<QByteArray>({ "1 0 0", "rg 2 w", " 0 0 5 5 re", "f" }), &ContentTreeTest::createResources);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);

    // rg0 w1 re2 f3
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(result.issues.empty());

    const PDFContentOperationList& operations = result.container->getOperations();
    QCOMPARE(operations.getCount(), size_t(4));
    QCOMPARE(operations.getOperation(0).getRawBytes(), QByteArray("1 0 0\nrg"));
    QVERIFY(operations.getOperation(0).getRange() == (PDFContentByteRange{ PDFContentStreamPosition{ 0, 0 }, PDFContentStreamPosition{ 1, 2 } }));
    QCOMPARE(operations.getOperation(1).getRawBytes(), QByteArray(" 2 w"));
    QCOMPARE(operations.getOperation(2).getRawBytes(), QByteArray("\n 0 0 5 5 re"));
    QVERIFY(operations.getOperation(2).getRange() == (PDFContentByteRange{ PDFContentStreamPosition{ 1, 6 }, PDFContentStreamPosition{ 2, 11 } }));
    QCOMPARE(operations.getOperation(3).getRawBytes(), QByteArray("\nf"));
    QCOMPARE(operations.getOriginalContent(), QByteArray("1 0 0\nrg 2 w\n 0 0 5 5 re\nf"));
    QVERIFY(operations.getTail().isEmpty());

    // The operation over the boundary did its work: the path is filled red (DeviceRGB 1 0 0)
    const PDFContentNodePointer path = nodeAt(result.container, { 1 });
    const PDFColor& color = path->getState()->getState().getFillColorOriginal();
    QCOMPARE(color.size(), size_t(3));
    QCOMPARE(double(color[0]), 1.0);
    QCOMPARE(double(color[1]), 0.0);
    QCOMPARE(double(color[2]), 0.0);

    // The last stream has only white space: it is the tail, after the line feed of the join
    const PDFDocument tailDocument = Documents::createDocument(QList<QByteArray>({ "1 w", "  " }), &ContentTreeTest::createResources);
    const PDFContentBuildResult tailResult = buildPage(tailDocument);
    QCOMPARE(describe(tailResult.container), QStringLiteral("Root{StateRun(0:1)}"));
    QCOMPARE(tailResult.container->getOperations().getTail(), QByteArray("\n  "));
    QCOMPARE(tailResult.container->getOperations().getOriginalContent(), QByteArray("1 w\n  "));
}

void ContentTreeTest::readOnlyPage()
{
    // zadani_jadro.md 4.3: a page with a stream of /Contents, which cannot be decoded, is read
    // only - it is shown, never changed, never written (a joined rewrite would drop the stream).
    // The four items: "1 w", a dictionary, a stream, which is no flate data, "0 0 5 5 re f".
    const PDFDocument document = Documents::createBrokenContentsDocument("1 w", "0 0 5 5 re f", &ContentTreeTest::createResources);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QVERIFY(result.container->isReadOnly());

    // The streams, which can be read, are in the tree: w0 re1 f2
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));
    QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UndecodableStream }));
    QVERIFY(!result.issues.front().nodeId.isValid());

    // The range of the operation re: from the end of w in the stream 0 to its operator in the stream 3
    QVERIFY(result.container->getOperations().getOperation(1).getRange() == (PDFContentByteRange{ PDFContentStreamPosition{ 0, 3 }, PDFContentStreamPosition{ 3, 10 } }));

    // A page of streams, which can all be read, is not read only
    const PDFDocument goodDocument = Documents::createDocument(QList<QByteArray>({ "1 w", "0 0 5 5 re f" }), &ContentTreeTest::createResources);
    const PDFContentBuildResult goodResult = buildPage(goodDocument);
    QVERIFY(!goodResult.container->isReadOnly());
    QVERIFY(goodResult.issues.empty());

    // The two pages are not the same content
    QVERIFY(result.container->getFingerprint() != goodResult.container->getFingerprint());
}

void ContentTreeTest::contentsOfOtherTypes()
{
    // A page without /Contents is an empty page (ISO 32000-2, 7.7.3.3, Table 31: "If this entry
    // is absent, the page shall be empty"): an empty tree, which can get new content
    {
        const PDFDocument document = Documents::createDocumentFromObjects([](PDFDocumentBuilder*) { return PDFObject(); }, &ContentTreeTest::createResources);
        const PDFContentBuildResult result = buildPage(document);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root"));
        QVERIFY(!result.container->isReadOnly());
        QVERIFY(result.issues.empty());
        QCOMPARE(result.container->getOperations().getCount(), size_t(0));
        QVERIFY(result.container->getOperations().getTail().isEmpty());
        QVERIFY(result.container->isUnchanged());
        QVERIFY(result.container->getRoot()->getState() != nullptr);
        QVERIFY(result.container->getRoot()->getState() == result.container->getInitialState());
    }

    // /Contents, which is neither a stream nor an array of streams, is no content the editor
    // could replace: read only
    {
        const PDFDocument document = Documents::createDocumentFromObjects([](PDFDocumentBuilder*) { return PDFObject::createInteger(5); }, &ContentTreeTest::createResources);
        const PDFContentBuildResult result = buildPage(document);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root"));
        QVERIFY(result.container->isReadOnly());
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UndecodableStream }));
    }

    // A form, whose stream cannot be decoded: its container is read only and empty
    {
        PDFObjectReference form;
        const PDFDocument document = Documents::createDocument(QByteArray("/Fm Do"), [&form](PDFDocumentBuilder* builder)
        {
            const PDFObject formObject = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                               { "Subtype", PDFObject::createName("Form") },
                                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                               { "Filter", PDFObject::createName("FlateDecode") } }, Documents::brokenContentsUndecodableData());
            form = formObject.getReference();

            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm", formObject } }));
            return resources;
        });

        // The page, which uses the form, is not concerned
        const PDFContentBuildResult pageResult = buildPage(document);
        QCOMPARE(describe(pageResult.container), QStringLiteral("Root{FormUse(0:1)}"));
        QVERIFY(!pageResult.container->isReadOnly());

        const PDFContentBuildResult result = build(document, PDFContentContainerKey::createForm(form), false, nullptr, 0);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root"));
        QVERIFY(result.container->isReadOnly());
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::UndecodableStream }));
        QCOMPARE(result.container->getBoundingBox(), QRectF(0.0, 0.0, 10.0, 10.0));
        QVERIFY(!result.container->getFingerprint().isEmpty());
    }
}

void ContentTreeTest::issueLimit()
{
    // zadani_overeni.md 6.2: a limit never refuses the content. A hostile content has an issue for
    // each of its operations: the list of the issues is bounded, the tree is not shortened.
    constexpr int COUNT = 5000;
    QByteArray content;
    for (int i = 0; i < COUNT; ++i)
    {
        content += "foo ";
    }

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(result.container->getRoot()->getChildren().size(), size_t(COUNT));
    QCOMPARE(result.container->getOperations().getCount(), size_t(COUNT));
    for (const PDFContentNodePointer& child : result.container->getRoot()->getChildren())
    {
        QVERIFY(child->getKind() == PDFContentNodeKind::Opaque);
    }

    QVERIFY(!result.issues.empty());
    QVERIFY(result.issues.size() < size_t(COUNT));
    QCOMPARE(result.container->getIssues().size(), result.issues.size());

    // The issues, which are kept, are the first ones, each with its node
    for (size_t i = 0; i < result.issues.size(); ++i)
    {
        QVERIFY(result.issues[i].kind == PDFContentIssue::Kind::UnknownOperator);
        QCOMPARE(result.issues[i].operationIndex, i);
        QVERIFY(result.issues[i].nodeId == result.container->getRoot()->getChildren()[i]->getId());
    }
}

void ContentTreeTest::inlineImage()
{
    using Variant = Documents::InlineImageVariant;

    // The page of createInlineImageDocument: "q 20 0 0 20 10 10 cm\nBI <entries>ID <data>\nEI\nQ\n
    // 0 0 5 5 re f\n" - q0 cm1 BI2 Q3 re4 f5. KE.18: the builder takes the range of the image
    // from the processor (the length of the data) and keeps the bytes.
    struct Row
    {
        Variant variant;
        QByteArray bytes;
        std::vector<PDFContentResourceName> names;
    };

    const std::vector<Row> rows =
    {
        { Variant::Plain, "\nBI /W 2 /H 2 /BPC 8 /CS /G ID abcd\nEI", { } },

        // The data contain the bytes EI: the image ends after its four bytes of data, not there
        { Variant::EndMarkerInData, "\nBI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI", { } },

        // The color space is a name of the resources (ISO 32000-2, 8.9.7): the operation uses it
        { Variant::NamedColorSpace, "\nBI /W 1 /H 2 /BPC 8 /CS /Cs1 ID abEIcd\nEI", { { PDFContentResourceCategory::ColorSpace, "Cs1" } } }
    };

    for (const Row& row : rows)
    {
        const PDFDocument document = Documents::createInlineImageDocument(row.variant);
        const PDFContentBuildResult result = buildPage(document);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{Group(0:1/3:1){StateRun(1:1) Image(2:1)} Path(4:2)}"));
        QVERIFY(result.issues.empty());

        const PDFContentOperationList& operations = result.container->getOperations();
        QCOMPARE(operations.getCount(), size_t(6));
        QVERIFY(operations.getOperation(2).getOperator() == PDFContentOperator::InlineImage);
        QCOMPARE(operations.getOperation(2).getRawBytes(), row.bytes);
        QVERIFY(operations.getOperation(2).getResourceNames() == row.names);
        QCOMPARE(operations.getOriginalContent(), Documents::inlineImagePageContent(row.variant));
        QCOMPARE(operations.getTail(), QByteArray("\n"));

        // The image: the unit square under "20 0 0 20 10 10 cm" covers x 10 ... 30, y 10 ... 30
        const PDFContentNodePointer image = nodeAt(result.container, { 0, 1 });
        QVERIFY(image->getData().isInlineImage);
        QVERIFY(!image->getData().xobject.isValid());
        QCOMPARE(image->getBoundingBox(), QRectF(10.0, 10.0, 20.0, 20.0));
        QVERIFY(image->getLeak().isEmpty());
        QCOMPARE(image->getState()->getCurrentTransformationMatrix(), QTransform(20.0, 0.0, 0.0, 20.0, 10.0, 10.0));
    }

    // An indexed color space over a color space of the resources (8.6.6.3: [/Indexed base hival
    // lookup]; /I is its abbreviation in an inline image, Table 92): the base is the resource
    // the operation uses. One sample of one component (the index), the data "x".
    {
        const PDFDocument document = createDocument("BI /W 1 /H 1 /BPC 8 /CS [/I /Cs1 1 <000000FFFFFF>] ID x\nEI");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{Image(0:1)}"));
        const std::vector<PDFContentResourceName> names = { { PDFContentResourceCategory::ColorSpace, "Cs1" } };
        QVERIFY(result.container->getOperations().getOperation(0).getResourceNames() == names);

        // A device space as the base is no resource
        const PDFDocument deviceDocument = createDocument("BI /W 1 /H 1 /BPC 8 /CS [/I /RGB 1 <000000FFFFFF>] ID x\nEI");
        const PDFContentBuildResult deviceResult = buildPage(deviceDocument);
        QCOMPARE(describe(deviceResult.container), QStringLiteral("Root{Image(0:1)}"));
        QVERIFY(deviceResult.container->getOperations().getOperation(0).getResourceNames().empty());
    }
}

void ContentTreeTest::inlineImageWithoutEnd()
{
    // zadani_overeni.md 6.2: an inline image without a length, which can be found (no EI), makes
    // the rest of the content opaque - the bytes after BI are image data, not operations.
    // w0 BI1 and whatever the bytes after BI look like.
    const QByteArray content("1 w BI /W 2 /H 2 ID abcd 2 w q");
    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);

    const std::vector<PDFContentNodePointer>& children = result.container->getRoot()->getChildren();
    QCOMPARE(children.size(), size_t(2));
    QVERIFY(children[0]->getKind() == PDFContentNodeKind::StateRun);
    QVERIFY(children[0]->getPrefix() == (PDFContentOperationSpan{ 0, 1 }));

    // One opaque leaf from BI to the last operation: the "q" in the image data opens nothing
    const PDFContentNodePointer opaque = children[1];
    QVERIFY(opaque->getKind() == PDFContentNodeKind::Opaque);
    QCOMPARE(opaque->getPrefix().first, size_t(1));
    QCOMPARE(opaque->getPrefix().first + opaque->getPrefix().count, result.container->getOperations().getCount());
    QVERIFY(opaque->getChildren().empty());
    QVERIFY(opaque->getLeak().isUnknown);

    const PDFContentOperation& operation = result.container->getOperations().getOperation(1);
    QVERIFY(operation.getOperator() == PDFContentOperator::InlineImage);
    QVERIFY(operation.getFlags().testFlag(PDFContentOperation::Failed));
    QCOMPARE(operation.getRawBytes(), QByteArray(" BI"));

    QVERIFY(issueKinds(result.issues).contains(int(PDFContentIssue::Kind::OperatorFailed)));
    QCOMPARE(result.container->getOperations().getOriginalContent(), content);
    QVERIFY(result.container->isUnchanged());

    // Inside of a group: the group stays open (its Q, if any, is a part of the opaque rest)
    const PDFDocument groupDocument = createDocument("q 1 w BI /W 2 /H 2 ID abcd Q");
    const PDFContentBuildResult groupResult = buildPage(groupDocument);
    const PDFContentNodePointer group = nodeAt(groupResult.container, { 0 });
    QVERIFY(group->getKind() == PDFContentNodeKind::Group);
    QVERIFY(group->getSuffix().isEmpty());
    QCOMPARE(group->getChildren().size(), size_t(2));
    QVERIFY(group->getChildren()[1]->getKind() == PDFContentNodeKind::Opaque);
    QCOMPARE(groupResult.container->getRoot()->getChildren().size(), size_t(1));
}

void ContentTreeTest::type3Procedures()
{
    // zadani_jadro.md 4.3: the glyph procedures of a Type 3 font run in the same processor; their
    // operations are not operations of the page. The glyph of the code 65 of the variant
    // InnerText is "60 0 d0 0 0 50 50 re f BT /T3 1 Tf 30 40 Td (B) Tj ET" - a text object of
    // its own inside of the text object of the page. BT0 Tf1 Tj2 ET3.
    const PDFDocument document = Documents::createType3Document("BT /T3 10 Tf (AB) Tj ET", Documents::Type3Variant::InnerText);
    const PDFContentBuildResult result = buildPage(document, true);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/3:1){StateRun(1:1) TextShow(2:1)}}"));
    QCOMPARE(result.container->getOperations().getCount(), size_t(4));
    QVERIFY(result.issues.empty());

    // The font of the glyph procedure (the size 1) is not the font of the page: after the text
    // object the size is still 10
    const PDFContentNodePointer textObject = nodeAt(result.container, { 0 });
    QVERIFY(textObject->getLeak().properties.contains(PDFContentStateProperty::Font));
    QCOMPARE(textObject->getLeak().endState->getState().getTextFontSize(), 10.0);
    QVERIFY(textObject->getLeak().endSymbolicState->getSource(PDFContentStateProperty::Font).operationIndex == 1);

    // The two glyphs of the page are laid out (the widths 60 and 80 of the glyph space, the
    // font matrix 0.01: 0.6 and 0.8 of the text space; the size 10 -> the advances 6 and 8)
    const PDFContentNodePointer show = nodeAt(result.container, { 0, 1 });
    QCOMPARE(show->getData().textItems.size(), size_t(2));
    QCOMPARE(show->getData().textItems[0].advance, 6.0);
    QCOMPARE(show->getData().textItems[1].advance, 8.0);
    QVERIFY(isTranslation(show->getData().textItems[0].textMatrix, 0.0, 0.0));
    QVERIFY(isTranslation(show->getData().textItems[1].textMatrix, 6.0, 0.0));
}

void ContentTreeTest::identifiers()
{
    // zadani_jadro.md 4.2: the builder numbers the nodes by a depth first walk, the root is 1, so
    // the same document gives the same numbers.
    // q0 w1 re2 f3 Q4 BT5 ET6 BMC7 EMC8:
    //   Root 1 { Group 2 { StateRun 3, Path 4 }, TextObject 5, MarkedContent 6 }
    const QByteArray content("q 1 w 0 0 5 5 re f Q BT ET /T BMC EMC");
    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Group(0:1/4:1){StateRun(1:1) Path(2:2)} TextObject(5:1/6:1) MarkedContent(7:1/8:1)}"));
    QVERIFY(identifiers(result.container->getRoot()) == std::vector<quint64>({ 1, 2, 3, 4, 5, 6 }));

    QVERIFY(!result.container->getRoot()->getParentId().isValid());
    QVERIFY(result.container->getNode(PDFContentNodeId{ 2 })->getKind() == PDFContentNodeKind::Group);
    QVERIFY(result.container->getNode(PDFContentNodeId{ 2 })->getParentId() == (PDFContentNodeId{ 1 }));
    QVERIFY(result.container->getNode(PDFContentNodeId{ 3 })->getParentId() == (PDFContentNodeId{ 2 }));
    QVERIFY(result.container->getNode(PDFContentNodeId{ 4 })->getKind() == PDFContentNodeKind::Path);
    QVERIFY(result.container->getNode(PDFContentNodeId{ 4 })->getParentId() == (PDFContentNodeId{ 2 }));
    QVERIFY(result.container->getNode(PDFContentNodeId{ 5 })->getKind() == PDFContentNodeKind::TextObject);
    QVERIFY(result.container->getNode(PDFContentNodeId{ 5 })->getParentId() == (PDFContentNodeId{ 1 }));
    QVERIFY(result.container->getNode(PDFContentNodeId{ 6 })->getParentId() == (PDFContentNodeId{ 1 }));
    QVERIFY(result.container->getMaximumNodeId() == (PDFContentNodeId{ 6 }));
    QCOMPARE(result.container->getNodeCount(), size_t(6));

    // The same content again (another document object): the same tree, the same numbers, the
    // same fingerprint - the identifiers of one build are valid for the other
    const PDFDocument secondDocument = createDocument(content);
    const PDFContentBuildResult secondResult = buildPage(secondDocument);
    QCOMPARE(describe(secondResult.container), describe(result.container));
    QVERIFY(identifiers(secondResult.container->getRoot()) == identifiers(result.container->getRoot()));
    QCOMPARE(secondResult.container->getFingerprint(), result.container->getFingerprint());

    // An opaque span takes the numbers of no node it has swallowed: w0 BMC1 q2 EMC3 Q4 w5 is
    // Root 1 { StateRun 2, Opaque 3, StateRun 4 }
    const PDFDocument opaqueDocument = createDocument("1 w /T BMC q EMC Q 2 w");
    const PDFContentBuildResult opaqueResult = buildPage(opaqueDocument);
    QVERIFY(identifiers(opaqueResult.container->getRoot()) == std::vector<quint64>({ 1, 2, 3, 4 }));
}

void ContentTreeTest::markedContent()
{
    // BDC0 EMC1 BDC2 EMC3 BDC4 EMC5 BMC6 EMC7 BDC8 EMC9 BDC10 EMC11 BDC12 EMC13 BDC14 EMC15 DP16
    const PDFDocument document = createDocument("/P << /MCID 3 /ActualText (Hi) >> BDC EMC\n"
                                                "/Span /MC1 BDC EMC\n"
                                                "/OC /OC1 BDC EMC\n"
                                                "/Artifact BMC EMC\n"
                                                "/Artifact << /Type /Pagination >> BDC EMC\n"
                                                "/P << /MCID 3 >> BDC EMC\n"
                                                "/P << /MCID -1 >> BDC EMC\n"
                                                "/P << /MCID 2147483648 >> BDC EMC\n"
                                                "/Tag /MC2 DP");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    const std::vector<PDFContentNodePointer>& children = result.container->getRoot()->getChildren();
    QCOMPARE(children.size(), size_t(9));

    // An inline property list (ISO 32000-2, 14.6.2) with an identifier (14.7.5.2) and /ActualText (14.9.4)
    const PDFContentNode::Data& paragraph = children[0]->getData();
    QVERIFY(paragraph.kind == PDFContentNodeKind::MarkedContent);
    QCOMPARE(paragraph.tag, QByteArray("P"));
    QVERIFY(paragraph.properties.isDictionary());
    QVERIFY(paragraph.propertiesName.isEmpty());
    QVERIFY(paragraph.mcid.has_value());
    QCOMPARE(*paragraph.mcid, PDFInteger(3));
    QVERIFY(paragraph.actualText.has_value());
    QCOMPARE(*paragraph.actualText, QStringLiteral("Hi"));
    QVERIFY(!paragraph.isArtifact);
    QVERIFY(!paragraph.optionalContent.isValid());

    // A property list named in /Properties of the resources: /MC1 is << /MCID 7 >>
    const PDFContentNode::Data& span = children[1]->getData();
    QCOMPARE(span.tag, QByteArray("Span"));
    QCOMPARE(span.propertiesName, QByteArray("MC1"));
    QVERIFY(span.properties == pageResource(document, "Properties", "MC1"));
    QVERIFY(span.mcid.has_value());
    QCOMPARE(*span.mcid, PDFInteger(7));
    QVERIFY(!span.actualText.has_value());

    // Optional content (8.11.3.2): /OC with the name of a group
    const PDFContentNode::Data& layer = children[2]->getData();
    QCOMPARE(layer.tag, QByteArray("OC"));
    QCOMPARE(layer.propertiesName, QByteArray("OC1"));
    QVERIFY(layer.properties.isReference());
    QVERIFY(layer.optionalContent.isValid());
    QVERIFY(layer.optionalContent == pageResource(document, "Properties", "OC1").getReference());
    QVERIFY(!layer.mcid.has_value());
    QVERIFY(!layer.isArtifact);

    // Artifacts (14.8.2.2), without and with a property list
    QCOMPARE(children[3]->getData().tag, QByteArray("Artifact"));
    QVERIFY(children[3]->getData().isArtifact);
    QVERIFY(children[3]->getData().properties.isNull());
    QVERIFY(!children[3]->getData().mcid.has_value());
    QVERIFY(children[4]->getData().isArtifact);
    QVERIFY(children[4]->getData().properties.isDictionary());

    // zadani_overeni.md 6.2: an identifier used twice and values out of 0 ... 2^31 - 1 are issues
    // of the reading
    QVERIFY(children[5]->getData().mcid.has_value());
    QCOMPARE(*children[5]->getData().mcid, PDFInteger(3));
    QVERIFY(!children[6]->getData().mcid.has_value());
    QVERIFY(!children[7]->getData().mcid.has_value());

    using Kind = PDFContentIssue::Kind;
    QCOMPARE(issueKinds(result.issues), kinds({ Kind::InvalidMarkedContentId, Kind::InvalidMarkedContentId, Kind::InvalidMarkedContentId }));
    QCOMPARE(result.issues[0].operationIndex, size_t(10));
    QVERIFY(result.issues[0].nodeId == children[5]->getId());
    QCOMPARE(result.issues[1].operationIndex, size_t(12));
    QVERIFY(result.issues[1].nodeId == children[6]->getId());
    QCOMPARE(result.issues[2].operationIndex, size_t(14));
    QVERIFY(result.issues[2].nodeId == children[7]->getId());

    // A marked-content point with a property list
    const PDFContentNode::Data& point = children[8]->getData();
    QVERIFY(point.kind == PDFContentNodeKind::MarkedPoint);
    QCOMPARE(point.tag, QByteArray("Tag"));
    QCOMPARE(point.propertiesName, QByteArray("MC2"));
    QVERIFY(point.properties == pageResource(document, "Properties", "MC2"));
    QVERIFY(!point.mcid.has_value());

    // /StructParents of the page (14.7.5.4): none here, 0 on the tagged page of the shared documents
    QCOMPARE(result.container->getStructuralParentKey(), PDFInteger(-1));

    // An identifier, which is not an integer, is no identifier
    {
        const PDFDocument wrongDocument = createDocument("/P << /MCID /Three >> BDC EMC /P << /MCID 4 >> BDC EMC");
        const PDFContentBuildResult wrongResult = buildPage(wrongDocument);
        QCOMPARE(describe(wrongResult.container), QStringLiteral("Root{MarkedContent(0:1/1:1) MarkedContent(2:1/3:1)}"));
        QVERIFY(!nodeAt(wrongResult.container, { 0 })->getData().mcid.has_value());
        QCOMPARE(*nodeAt(wrongResult.container, { 1 })->getData().mcid, PDFInteger(4));
        QCOMPARE(issueKinds(wrongResult.issues), kinds({ Kind::InvalidMarkedContentId }));
        QCOMPARE(wrongResult.issues.front().operationIndex, size_t(0));
    }

    const PDFDocument taggedDocument = Documents::createTaggedDocument();
    const PDFContentBuildResult taggedResult = buildPage(taggedDocument);
    QVERIFY(taggedResult.container != nullptr);
    QCOMPARE(taggedResult.container->getStructuralParentKey(), PDFInteger(0));
    QVERIFY(taggedResult.issues.empty());

    // The identifiers of the tagged page in the order of its content: 0 (/P), 1 (/Span), none
    // (/Artifact), 3 (/Figure)
    const std::vector<PDFContentNodePointer>& taggedChildren = taggedResult.container->getRoot()->getChildren();
    QCOMPARE(taggedChildren.size(), size_t(4));
    QCOMPARE(*taggedChildren[0]->getData().mcid, PDFInteger(0));
    QCOMPARE(*taggedChildren[1]->getData().mcid, PDFInteger(1));
    QVERIFY(!taggedChildren[2]->getData().mcid.has_value());
    QVERIFY(taggedChildren[2]->getData().isArtifact);
    QCOMPARE(*taggedChildren[3]->getData().mcid, PDFInteger(3));
    QCOMPARE(taggedChildren[3]->getData().tag, QByteArray("Figure"));
}

void ContentTreeTest::boundingBoxes()
{
    // q0 cm1 re2 f3 Do4 Do5 Q6 w7. Under "2 0 0 3 10 20 cm" the point (x, y) is (2 x + 10, 3 y + 20)
    // of the page:
    //   the rectangle [0 0 5 5]                         -> x 10 ... 20, y 20 ... 35
    //   the image, the unit square (8.9.5)              -> x 10 ... 12, y 20 ... 23
    //   the form /Fm1, /BBox [0 0 40 30] under /Matrix [2 0 0 2 10 20] (8.10.2): x 10 ... 90,
    //   y 20 ... 80 of the space of Do                   -> x 30 ... 190, y 80 ... 260
    const PDFDocument document = createDocument("q 2 0 0 3 10 20 cm 0 0 5 5 re f /Im1 Do /Fm1 Do Q 1 w");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Group(0:1/6:1){StateRun(1:1) Path(2:2) Image(4:1) FormUse(5:1)} StateRun(7:1)}"));

    QCOMPARE(nodeAt(result.container, { 0, 1 })->getBoundingBox(), QRectF(10.0, 20.0, 10.0, 15.0));
    QCOMPARE(nodeAt(result.container, { 0, 2 })->getBoundingBox(), QRectF(10.0, 20.0, 2.0, 3.0));
    QCOMPARE(nodeAt(result.container, { 0, 3 })->getBoundingBox(), QRectF(30.0, 80.0, 160.0, 180.0));

    // A state run paints nothing; a node with children has the box of its children
    QVERIFY(nodeAt(result.container, { 0, 0 })->getBoundingBox().isNull());
    QVERIFY(nodeAt(result.container, { 1 })->getBoundingBox().isNull());
    QCOMPARE(nodeAt(result.container, { 0 })->getBoundingBox(), QRectF(10.0, 20.0, 180.0, 240.0));
    QCOMPARE(result.container->getRoot()->getBoundingBox(), QRectF(10.0, 20.0, 180.0, 240.0));

    // The box of the container of a page is its media box (the shared documents: [0 0 200 200])
    QCOMPARE(result.container->getBoundingBox(), QRectF(0.0, 0.0, 200.0, 200.0));
    QVERIFY(result.container->getMatrix().isIdentity());

    // The image and the form of Do
    QVERIFY(!nodeAt(result.container, { 0, 2 })->getData().isInlineImage);
    QVERIFY(nodeAt(result.container, { 0, 2 })->getData().xobject == pageResource(document, "XObject", "Im1").getReference());
    QCOMPARE(nodeAt(result.container, { 0, 2 })->getData().resourceName, QByteArray("Im1"));
    QVERIFY(nodeAt(result.container, { 0, 3 })->getData().xobject == pageResource(document, "XObject", "Fm1").getReference());
    QCOMPARE(nodeAt(result.container, { 0, 3 })->getData().resourceName, QByteArray("Fm1"));
}

// ---------------------------------------------------------------------------------------------
//  Builder: states
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::sharedStates()
{
    // zadani_jadro.md 4.3: the states are shared, a new one is made only on a change.
    // re0 f1 re2 f3 q4 rg5 re6 f7 Q8 re9 f10 w11 re12 f13
    const PDFDocument document = createDocument("0 0 5 5 re f 10 0 5 5 re f q 1 0 0 rg 20 0 5 5 re f Q 30 0 5 5 re f 3 w 40 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{Path(0:2) Path(2:2) Group(4:1/8:1){StateRun(5:1) Path(6:2)} Path(9:2) StateRun(11:1) Path(12:2)}"));

    const PDFContentNodePointer first = nodeAt(result.container, { 0 });
    const PDFContentNodePointer second = nodeAt(result.container, { 1 });
    const PDFContentNodePointer group = nodeAt(result.container, { 2 });
    const PDFContentNodePointer inner = nodeAt(result.container, { 2, 1 });
    const PDFContentNodePointer afterGroup = nodeAt(result.container, { 3 });
    const PDFContentNodePointer last = nodeAt(result.container, { 5 });

    // Nothing has changed between the first two paths, the group and its state run
    QVERIFY(first->getState() != nullptr);
    QVERIFY(first->getState() == result.container->getInitialState());
    QVERIFY(first->getState() == result.container->getRoot()->getState());
    QVERIFY(second->getState() == first->getState());
    QVERIFY(group->getState() == first->getState());
    QVERIFY(nodeAt(result.container, { 2, 0 })->getState() == first->getState());
    QVERIFY(second->getSymbolicState() == first->getSymbolicState());
    QVERIFY(first->getSymbolicState() == result.container->getInitialSymbolicState());

    // The color has changed inside of the group
    QVERIFY(inner->getState() != first->getState());
    QVERIFY(inner->getSymbolicState() != first->getSymbolicState());
    QCOMPARE(double(inner->getState()->getState().getFillColorOriginal()[0]), 1.0);

    // Q gives the state before q back: the same shared objects
    QVERIFY(afterGroup->getState() == first->getState());
    QVERIFY(afterGroup->getSymbolicState() == first->getSymbolicState());

    // The line width has changed
    QVERIFY(last->getState() != first->getState());
    QCOMPARE(last->getState()->getState().getLineWidth(), 3.0);
    QCOMPARE(first->getState()->getState().getLineWidth(), 1.0);    // Table 51: the initial line width

    // The initial state of a page: the identity as the matrix, DeviceGray black (Table 51)
    const PDFPageContentProcessorState& initial = result.container->getInitialState()->getState();
    QVERIFY(initial.getCurrentTransformationMatrix().isIdentity());
    QCOMPARE(initial.getFillColorOriginal().size(), size_t(1));
    QCOMPARE(double(initial.getFillColorOriginal()[0]), 0.0);
    QVERIFY(initial.getSoftMask() == nullptr);
    QVERIFY(!result.container->getInitialState()->hasSoftMask());
}

void ContentTreeTest::leaks()
{
    using Property = PDFContentStateProperty;
    using Properties = std::vector<Property>;

    // zadani_jadro.md 4.3 (RE-3): every node has its leak - the difference of the state between
    // its beginning and its end.
    {
        // BMC0 rg1 EMC2 re3 f4: the marked content has the leak of its children
        const PDFDocument document = createDocument("/T BMC 1 0 0 rg EMC 0 0 5 5 re f");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{MarkedContent(0:1/2:1){StateRun(1:1)} Path(3:2)}"));

        const PDFContentNodePointer marked = nodeAt(result.container, { 0 });
        const PDFContentNodePointer run = nodeAt(result.container, { 0, 0 });
        const PDFContentNodePointer path = nodeAt(result.container, { 1 });

        QVERIFY(marked->getLeak().properties.getProperties() == Properties({ Property::FillColor }));
        QVERIFY(!marked->getLeak().isTransformChanged);
        QVERIFY(!marked->getLeak().isClipChanged);
        QVERIFY(!marked->getLeak().isUnknown);
        QVERIFY(run->getLeak().properties.getProperties() == Properties({ Property::FillColor }));

        // The state at the end of the node is the state the following node sees
        QVERIFY(marked->getLeak().endState == path->getState());
        QVERIFY(marked->getLeak().endSymbolicState == path->getSymbolicState());
        QVERIFY(run->getLeak().endState == path->getState());
        QCOMPARE(double(marked->getLeak().endState->getState().getFillColorOriginal()[0]), 1.0);
        QCOMPARE(marked->getLeak().endState->getState().getFillColorOriginal().size(), size_t(3));

        // The path changes nothing
        QVERIFY(path->getLeak().isEmpty());
        QVERIFY(path->getLeak().endState == nullptr);
        QVERIFY(path->getLeak().endSymbolicState == nullptr);
    }

    {
        // Do0 Do1 sh2 MP3 w4: an image, a form, a shading and a marked-content point change
        // nothing for the nodes after them (a form is painted between an implicit q and Q,
        // ISO 32000-2, 8.10.1)
        const PDFDocument document = createDocument("/Im1 Do /Fm1 Do /Sh1 sh /T MP 2 w");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{Image(0:1) FormUse(1:1) Shading(2:1) MarkedPoint(3:1) StateRun(4:1)}"));
        for (size_t i = 0; i < 4; ++i)
        {
            const PDFContentNodePointer node = nodeAt(result.container, { i });
            QVERIFY(node->getLeak().isEmpty());
            QVERIFY(node->getLeak().endState == nullptr);
            QVERIFY(!node->hasClipEffect());
            QVERIFY(node->getState() == result.container->getInitialState());
        }
        QVERIFY(!nodeAt(result.container, { 4 })->getLeak().isEmpty());
    }

    {
        // q0 rg1 cm2 Q3 w4 cm5: a group is balanced; a state run with w and cm
        const PDFDocument document = createDocument("q 1 0 0 rg 2 0 0 2 0 0 cm Q 3 w 1 0 0 1 5 6 cm");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{Group(0:1/3:1){StateRun(1:2)} StateRun(4:2)}"));

        QVERIFY(nodeAt(result.container, { 0 })->getLeak().isEmpty());

        const PDFContentStateLeak& inner = nodeAt(result.container, { 0, 0 })->getLeak();
        QVERIFY(inner.properties.getProperties() == Properties({ Property::FillColor }));
        QVERIFY(inner.isTransformChanged);
        QCOMPARE(inner.endState->getCurrentTransformationMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 0.0, 0.0));

        const PDFContentStateLeak& outer = nodeAt(result.container, { 1 })->getLeak();
        QVERIFY(outer.properties.getProperties() == Properties({ Property::LineWidth }));
        QVERIFY(outer.isTransformChanged);
        QCOMPARE(outer.endState->getCurrentTransformationMatrix(), QTransform(1.0, 0.0, 0.0, 1.0, 5.0, 6.0));
        QCOMPARE(outer.endState->getState().getLineWidth(), 3.0);
    }

    {
        // BT0 Tf1 Tc2 Tj3 ET4: the text state set inside of a text object stays after it (9.3.1)
        const PDFDocument document = createDocument("BT /F1 12 Tf 3 Tc (A) Tj ET");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:2) TextShow(3:1)}}"));

        const PDFContentStateLeak& textLeak = nodeAt(result.container, { 0 })->getLeak();
        QVERIFY(textLeak.properties.getProperties() == Properties({ Property::Font, Property::CharacterSpacing }));
        QVERIFY(!textLeak.isTransformChanged);
        QVERIFY(!textLeak.isClipChanged);
        QCOMPARE(textLeak.endState->getState().getTextFontSize(), 12.0);
        QCOMPARE(textLeak.endState->getState().getTextCharacterSpacing(), 3.0);

        QVERIFY(nodeAt(result.container, { 0, 0 })->getLeak().properties.getProperties() == Properties({ Property::Font, Property::CharacterSpacing }));
        QVERIFY(nodeAt(result.container, { 0, 1 })->getLeak().isEmpty());       // Tj sets nothing
    }

    {
        // BT0 Tf1 "2 ET3: the operator " sets the word spacing and the character spacing (Table 107)
        const PDFDocument document = createDocument("BT /F1 12 Tf 2 3 (A) \" ET");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/3:1){StateRun(1:1) TextShow(2:1)}}"));

        const PDFContentStateLeak& showLeak = nodeAt(result.container, { 0, 1 })->getLeak();
        QVERIFY(showLeak.properties.getProperties() == Properties({ Property::CharacterSpacing, Property::WordSpacing }));
        QCOMPARE(showLeak.endState->getState().getTextWordSpacing(), 2.0);
        QCOMPARE(showLeak.endState->getState().getTextCharacterSpacing(), 3.0);
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == Properties({ Property::Font, Property::CharacterSpacing, Property::WordSpacing }));
    }

    {
        // BT0 Tf1 TL2 TD3 ET4: TL and TD set the leading (TD: the negative of its second operand)
        const PDFDocument document = createDocument("BT /F1 12 Tf 14 TL 5 -7 TD ET");
        const PDFContentBuildResult result = buildPage(document);
        const PDFContentStateLeak& textLeak = nodeAt(result.container, { 0 })->getLeak();
        QVERIFY(textLeak.properties.getProperties() == Properties({ Property::Font, Property::Leading }));
        QCOMPARE(textLeak.endState->getState().getTextLeading(), 7.0);
        QCOMPARE(textLeak.endSymbolicState->getSource(Property::Leading).operationIndex, size_t(3));
    }

    {
        // w0 re1 f2 w3 re4 f5: the value at the end is the value at the beginning - no leak of
        // the second state run of a page, though it sets the property
        const PDFDocument document = createDocument("4 w 0 0 5 5 re f 4 w 0 0 5 5 re f");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Path(1:2) StateRun(3:1) Path(4:2)}"));
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == Properties({ Property::LineWidth }));
        QVERIFY(nodeAt(result.container, { 2 })->getLeak().isEmpty());

        // The source is the second operator nevertheless
        QCOMPARE(nodeAt(result.container, { 3 })->getSymbolicState()->getSource(Property::LineWidth).operationIndex, size_t(3));
    }

    {
        // gs0 re1 f2: every parameter of the graphics state dictionary /GSA, whose value differs
        // from the initial one (ISO 32000-2, 8.4.1, Table 51: line width 1, cap 0, join 0, miter
        // limit 10, solid line, RelativeColorimetric, no overprint, overprint mode 0, flatness 1,
        // no stroke adjustment, Normal, no mask, alphas 1, alpha source false, text knockout
        // true). The initial values of the smoothness, of the functions and of the halftone
        // depend on the device: they are not asked here.
        const PDFDocument document = createDocument("/GSA gs 0 0 5 5 re f");
        const PDFContentBuildResult result = buildPage(document);
        const PDFContentStateLeak& leak = nodeAt(result.container, { 0 })->getLeak();
        const std::vector<Property> expected = { Property::LineWidth, Property::LineCap, Property::LineJoin, Property::MiterLimit, Property::DashPattern,
                                                 Property::RenderingIntent, Property::Flatness, Property::TextKnockout, Property::AlphaStroking,
                                                 Property::AlphaFilling, Property::BlendMode, Property::SoftMask, Property::AlphaIsShape,
                                                 Property::StrokeAdjustment, Property::OverprintStroking, Property::OverprintFilling, Property::OverprintMode };
        for (const Property property : expected)
        {
            QVERIFY2(leak.properties.contains(property), qPrintable(QString::number(int(property))));
        }

        // What the dictionary does not set, does not leak
        for (const Property property : { Property::FillColor, Property::StrokeColor, Property::Font, Property::CharacterSpacing, Property::WordSpacing,
                                         Property::HorizontalScaling, Property::Leading, Property::TextRenderingMode, Property::TextRise })
        {
            QVERIFY2(!leak.properties.contains(property), qPrintable(QString::number(int(property))));
        }
        QVERIFY(!leak.isTransformChanged);
        QVERIFY(leak.endState->hasSoftMask());
    }
}

void ContentTreeTest::clipEffects()
{
    {
        // BT0 Tf1 Tr2 Tj3 ET4 re5 f6: a text of the rendering mode 7 adds to the clipping path,
        // the clip applies from ET (ISO 32000-2, 9.3.6) - R-1: such a text is never wrapped
        const PDFDocument document = createDocument("BT /F1 10 Tf 7 Tr (A) Tj ET 0 0 5 5 re f");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:2) TextShow(3:1)} Path(5:2)}"));

        const PDFContentNodePointer textObject = nodeAt(result.container, { 0 });
        QVERIFY(textObject->getData().clipsByText);
        QVERIFY(textObject->hasClipEffect());
        QVERIFY(textObject->getLeak().isClipChanged);
        QVERIFY(nodeAt(result.container, { 0, 1 })->hasClipEffect());
        QVERIFY(nodeAt(result.container, { 0, 1 })->getLeak().isClipChanged);
        QVERIFY(nodeAt(result.container, { 0, 1 })->getData().textState.renderingMode == TextRenderingMode::Clip);
        QVERIFY(!nodeAt(result.container, { 1 })->hasClipEffect());
    }

    // The modes 4 ... 7 clip, the modes 0 ... 3 do not (Table 104)
    for (int mode = 0; mode <= 7; ++mode)
    {
        const PDFDocument document = createDocument("BT /F1 10 Tf " + QByteArray::number(mode) + " Tr (A) Tj ET");
        const PDFContentBuildResult result = buildPage(document);
        const PDFContentNodePointer textObject = nodeAt(result.container, { 0 });
        QCOMPARE(textObject->getData().clipsByText, mode >= 4);
        QCOMPARE(textObject->hasClipEffect(), mode >= 4);
        QCOMPARE(textObject->getLeak().isClipChanged, mode >= 4);
    }

    {
        // BT0 Tf1 Tr2 Tj3 Tr4 Tj5 ET6: one part of the text clips, the other one does not
        const PDFDocument document = createDocument("BT /F1 10 Tf 4 Tr (A) Tj 0 Tr (B) Tj ET");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/6:1){StateRun(1:2) TextShow(3:1) StateRun(4:1) TextShow(5:1)}}"));
        QVERIFY(nodeAt(result.container, { 0 })->getData().clipsByText);
        QVERIFY(nodeAt(result.container, { 0, 1 })->hasClipEffect());
        QVERIFY(!nodeAt(result.container, { 0, 3 })->hasClipEffect());

        // The rendering mode at the end is the one at the beginning (0): it does not leak
        QVERIFY(!nodeAt(result.container, { 0 })->getLeak().properties.contains(PDFContentStateProperty::TextRenderingMode));
    }

    {
        // 9.3.6: the rendering mode does not apply to a Type 3 font - its text never clips
        const PDFDocument document = createDocument("BT /T3 10 Tf 7 Tr (A) Tj ET");
        const PDFContentBuildResult result = buildPage(document);
        QVERIFY(!nodeAt(result.container, { 0 })->getData().clipsByText);
        QVERIFY(!nodeAt(result.container, { 0 })->hasClipEffect());
        QVERIFY(!nodeAt(result.container, { 0, 1 })->hasClipEffect());
        QVERIFY(!nodeAt(result.container, { 0 })->getLeak().isClipChanged);
    }

    {
        // BMC0 re1 W2 n3 EMC4 q5 re6 W7 n8 Q9: a clip set inside of marked content reaches the
        // nodes after it; a group keeps it inside
        const PDFDocument document = createDocument("/T BMC 0 0 5 5 re W n EMC q 0 0 5 5 re W n Q");
        const PDFContentBuildResult result = buildPage(document);
        QCOMPARE(describe(result.container), QStringLiteral("Root{MarkedContent(0:1/4:1){Path(1:3)} Group(5:1/9:1){Path(6:3)}}"));
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().isClipChanged);
        QVERIFY(!nodeAt(result.container, { 0 })->hasClipEffect());     // Only a leaf and a text object can have it
        QVERIFY(nodeAt(result.container, { 0, 0 })->hasClipEffect());
        QVERIFY(nodeAt(result.container, { 1 })->getLeak().isEmpty());
        QVERIFY(nodeAt(result.container, { 1, 0 })->getLeak().isClipChanged);
    }
}

void ContentTreeTest::symbolicOperators()
{
    using Property = PDFContentStateProperty;
    using Origin = PDFContentPropertySource::Origin;

    // zadani_jadro.md 4.3: the symbolic state says for each property, where it came from.
    // Tf0 Tc1 Tw2 Tz3 TL4 Tr5 Ts6 w7 J8 j9 M10 d11 ri12 i13 rg14 CS15 SC16 cs17 scn18 re19 f20
    const PDFDocument document = createDocument("/F1 9 Tf 3 Tc 4 Tw 90 Tz 12 TL 1 Tr 2 Ts 5 w 1 J 2 j 4 M [1] 0 d /Saturation ri 2 i "
                                                "1 0 0 rg /Cs1 CS 0.1 0.2 0.3 SC /Pattern cs /P1 scn 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:19) Path(19:2)}"));
    QVERIFY(result.issues.empty());

    const PDFContentSymbolicStatePointer symbolic = nodeAt(result.container, { 1 })->getSymbolicState();
    QVERIFY(symbolic != nullptr);

    // The operators of one property each
    const std::vector<std::pair<Property, size_t>> operators =
    {
        { Property::CharacterSpacing, 1 }, { Property::WordSpacing, 2 }, { Property::HorizontalScaling, 3 }, { Property::Leading, 4 },
        { Property::TextRenderingMode, 5 }, { Property::TextRise, 6 }, { Property::LineWidth, 7 }, { Property::LineCap, 8 },
        { Property::LineJoin, 9 }, { Property::MiterLimit, 10 }, { Property::DashPattern, 11 }, { Property::RenderingIntent, 12 },
        { Property::Flatness, 13 }
    };
    for (const auto& [property, index] : operators)
    {
        const PDFContentPropertySource& source = symbolic->getSource(property);
        QVERIFY2(source.origin == Origin::Operator, qPrintable(QString::number(int(property))));
        QCOMPARE(source.operationIndex, index);
        QVERIFY(source.resourceName.isEmpty());
        QVERIFY(source.extGStateKey.isEmpty());
    }

    // Tf: the name and the object of the font
    const PDFContentPropertySource& font = symbolic->getSource(Property::Font);
    QVERIFY(font.origin == Origin::Operator);
    QCOMPARE(font.operationIndex, size_t(0));
    QCOMPARE(font.resourceName, QByteArray("F1"));
    QVERIFY(font.object.isReference());
    QVERIFY(font.object == pageResource(document, "Font", "F1"));

    // CS + SC: the color space of the resources, as it is there; no pattern
    const PDFContentPropertySource& stroke = symbolic->getSource(Property::StrokeColor);
    QVERIFY(stroke.origin == Origin::Operator);
    QCOMPARE(stroke.operationIndex, size_t(16));
    QCOMPARE(stroke.resourceName, QByteArray("Cs1"));
    QVERIFY(stroke.object.isArray());
    QVERIFY(stroke.object == pageResource(document, "ColorSpace", "Cs1"));
    QVERIFY(stroke.patternName.isEmpty());
    QVERIFY(stroke.patternObject.isNull());

    // rg, then /Pattern cs and /P1 scn: the last operator counts; the pattern space is named
    // by the operator itself, the pattern is the name of the resources and its object
    const PDFContentPropertySource& fill = symbolic->getSource(Property::FillColor);
    QVERIFY(fill.origin == Origin::Operator);
    QCOMPARE(fill.operationIndex, size_t(18));
    QVERIFY(fill.resourceName.isEmpty());
    QVERIFY(fill.object == PDFObject::createName("Pattern"));
    QCOMPARE(fill.patternName, QByteArray("P1"));
    QVERIFY(fill.patternObject.isReference());
    QVERIFY(fill.patternObject == pageResource(document, "Pattern", "P1"));

    // Nothing of an ExtGState
    for (const Property property : { Property::AlphaStroking, Property::AlphaFilling, Property::BlendMode, Property::SoftMask, Property::Halftone, Property::TextKnockout })
    {
        QVERIFY(symbolic->isInherited(property));
    }

    // The resolved values of the same node
    const PDFPageContentProcessorState& state = nodeAt(result.container, { 1 })->getState()->getState();
    QCOMPARE(state.getTextFontSize(), 9.0);
    QCOMPARE(state.getTextCharacterSpacing(), 3.0);
    QCOMPARE(state.getTextWordSpacing(), 4.0);
    QCOMPARE(state.getTextHorizontalScaling(), 90.0);
    QCOMPARE(state.getTextLeading(), 12.0);
    QVERIFY(state.getTextRenderingMode() == TextRenderingMode::Stroke);
    QCOMPARE(state.getTextRise(), 2.0);
    QCOMPARE(state.getLineWidth(), 5.0);
    QCOMPARE(state.getMitterLimit(), 4.0);
    QCOMPARE(state.getFlatness(), 2.0);
    QCOMPARE(state.getRenderingIntentName(), QByteArray("Saturation"));

    // The device color operators name their space
    {
        // g0 RG1 k2 K3 re4 f5: K after RG - the last one is the source of the stroking color
        const PDFDocument deviceDocument = createDocument("0.5 g 1 0 0 RG 0 0 0 1 k 0 0 0 1 K 0 0 5 5 re f");
        const PDFContentBuildResult deviceResult = buildPage(deviceDocument);
        const PDFContentSymbolicStatePointer deviceSymbolic = nodeAt(deviceResult.container, { 1 })->getSymbolicState();
        QVERIFY(deviceSymbolic->getSource(Property::FillColor).object == PDFObject::createName("DeviceCMYK"));
        QCOMPARE(deviceSymbolic->getSource(Property::FillColor).operationIndex, size_t(2));
        QVERIFY(deviceSymbolic->getSource(Property::StrokeColor).object == PDFObject::createName("DeviceCMYK"));
        QCOMPARE(deviceSymbolic->getSource(Property::StrokeColor).operationIndex, size_t(3));
    }
    {
        const PDFDocument deviceDocument = createDocument("0.5 g 1 0 0 RG 0 0 5 5 re f");
        const PDFContentBuildResult deviceResult = buildPage(deviceDocument);
        const PDFContentSymbolicStatePointer deviceSymbolic = nodeAt(deviceResult.container, { 1 })->getSymbolicState();
        QVERIFY(deviceSymbolic->getSource(Property::FillColor).object == PDFObject::createName("DeviceGray"));
        QVERIFY(deviceSymbolic->getSource(Property::StrokeColor).object == PDFObject::createName("DeviceRGB"));
        QVERIFY(deviceSymbolic->getSource(Property::FillColor).resourceName.isEmpty());
        QVERIFY(deviceSymbolic->getSource(Property::FillColor).patternName.isEmpty());
    }

    // q ... Q: the sources inside of the group are gone after it
    {
        // q0 w1 rg2 re3 f4 Q5 re6 f7
        const PDFDocument groupDocument = createDocument("q 5 w 1 0 0 rg 0 0 5 5 re f Q 0 0 5 5 re f");
        const PDFContentBuildResult groupResult = buildPage(groupDocument);
        const PDFContentSymbolicStatePointer inside = nodeAt(groupResult.container, { 0, 1 })->getSymbolicState();
        const PDFContentSymbolicStatePointer after = nodeAt(groupResult.container, { 1 })->getSymbolicState();
        QVERIFY(!inside->isInherited(Property::LineWidth));
        QVERIFY(!inside->isInherited(Property::FillColor));
        QVERIFY(after->isInherited(Property::LineWidth));
        QVERIFY(after->isInherited(Property::FillColor));
    }
}

void ContentTreeTest::symbolicExtGState()
{
    using Property = PDFContentStateProperty;
    using Origin = PDFContentPropertySource::Origin;

    // zadani_jadro.md 4.3: every key of an ExtGState on its own. ISO 32000-2, 8.4.5, Table 57:
    // gs sets only the parameters, whose keys are in the dictionary. gs0 re1 f2.
    const PDFDocument document = createDocument("/GSA gs 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));
    QVERIFY(result.issues.empty());
    QVERIFY(!result.container->getOperations().getOperation(0).getFlags().testFlag(PDFContentOperation::Failed));

    const PDFDictionary* dictionary = document.getDictionaryFromObject(pageResource(document, "ExtGState", "GSA"));
    QVERIFY(dictionary != nullptr);

    const PDFContentSymbolicStatePointer symbolic = nodeAt(result.container, { 1 })->getSymbolicState();

    // The parameter of each key of Table 57. /BG2 takes precedence over /BG ("if both BG and BG2
    // are present in the same graphics state parameter dictionary, BG2 shall take precedence");
    // /OP without /op sets both overprint parameters.
    const std::vector<std::pair<Property, QByteArray>> keys =
    {
        { Property::LineWidth, "LW" }, { Property::LineCap, "LC" }, { Property::LineJoin, "LJ" }, { Property::MiterLimit, "ML" },
        { Property::DashPattern, "D" }, { Property::RenderingIntent, "RI" }, { Property::OverprintStroking, "OP" },
        { Property::OverprintFilling, "OP" }, { Property::OverprintMode, "OPM" }, { Property::BlackGeneration, "BG2" },
        { Property::UndercolorRemoval, "UCR" }, { Property::Transfer, "TR" }, { Property::Halftone, "HT" }, { Property::Flatness, "FL" },
        { Property::Smoothness, "SM" }, { Property::StrokeAdjustment, "SA" }, { Property::BlendMode, "BM" }, { Property::SoftMask, "SMask" },
        { Property::AlphaStroking, "CA" }, { Property::AlphaFilling, "ca" }, { Property::AlphaIsShape, "AIS" }, { Property::TextKnockout, "TK" },
        { Property::BlackPointCompensation, "UseBlackPtComp" }, { Property::HalftoneOrigin, "HTO" }
    };

    PDFContentStatePropertySet setProperties;
    for (const auto& [property, key] : keys)
    {
        const PDFContentPropertySource& source = symbolic->getSource(property);
        QVERIFY2(source.origin == Origin::ExtGState, key.constData());
        QCOMPARE(source.resourceName, QByteArray("GSA"));
        QCOMPARE(source.extGStateKey, key);
        QCOMPARE(source.operationIndex, size_t(0));

        // The value of the key as it is in the dictionary: kept by reference, never copied
        QVERIFY2(!dictionary->get(key).isNull(), key.constData());
        QVERIFY2(source.object == dictionary->get(key), key.constData());
        setProperties.insert(property);
    }
    QVERIFY(symbolic->getSource(Property::UndercolorRemoval).object.isReference());
    QVERIFY(symbolic->getSource(Property::BlackGeneration).object == PDFObject::createName("Default"));
    QVERIFY(symbolic->getSource(Property::SoftMask).object.isDictionary());

    // Everything else is still inherited: the dictionary has neither /Font nor a color
    for (int i = 0; i < int(Property::Count); ++i)
    {
        if (!setProperties.contains(Property(i)))
        {
            QVERIFY2(symbolic->isInherited(Property(i)), qPrintable(QString::number(i)));
        }
    }

    // The values of the dictionary are in the resolved state
    const PDFPageContentProcessorState& state = nodeAt(result.container, { 1 })->getState()->getState();
    QCOMPARE(state.getLineWidth(), 2.0);
    QCOMPARE(state.getMitterLimit(), 5.0);
    QCOMPARE(state.getAlphaStroking(), 0.25);
    QCOMPARE(state.getAlphaFilling(), 0.75);
    QVERIFY(state.getBlendMode() == BlendMode::Multiply);
    QVERIFY(state.getOverprintMode().overprintStroking);
    QVERIFY(state.getOverprintMode().overprintFilling);
    QCOMPARE(state.getOverprintMode().overprintMode, 1);
    QVERIFY(state.getAlphaIsShape());
    QVERIFY(state.getStrokeAdjustment());
    QCOMPARE(state.getSmoothness(), 0.5);
    QVERIFY(nodeAt(result.container, { 1 })->getState()->hasSoftMask());

    // A later gs replaces only the sources of its own keys: /GSO is << /OP true /op false >> -
    // with /op in the dictionary, /OP sets the stroking parameter only. gs0 gs1 w2 re3 f4.
    const PDFDocument secondDocument = createDocument("/GSA gs /GSO gs 7 w 0 0 5 5 re f");
    const PDFContentBuildResult secondResult = buildPage(secondDocument);
    const PDFContentSymbolicStatePointer second = nodeAt(secondResult.container, { 1 })->getSymbolicState();

    QCOMPARE(second->getSource(Property::OverprintStroking).resourceName, QByteArray("GSO"));
    QCOMPARE(second->getSource(Property::OverprintStroking).extGStateKey, QByteArray("OP"));
    QCOMPARE(second->getSource(Property::OverprintStroking).operationIndex, size_t(1));
    QCOMPARE(second->getSource(Property::OverprintFilling).resourceName, QByteArray("GSO"));
    QCOMPARE(second->getSource(Property::OverprintFilling).extGStateKey, QByteArray("op"));
    QCOMPARE(second->getSource(Property::OverprintMode).resourceName, QByteArray("GSA"));   // Not a key of /GSO
    QCOMPARE(second->getSource(Property::AlphaFilling).resourceName, QByteArray("GSA"));
    QCOMPARE(second->getSource(Property::AlphaFilling).operationIndex, size_t(0));

    // An operator after gs is the source of its property
    QVERIFY(second->getSource(Property::LineWidth).origin == Origin::Operator);
    QCOMPARE(second->getSource(Property::LineWidth).operationIndex, size_t(2));
    QVERIFY(second->getSource(Property::LineWidth).extGStateKey.isEmpty());

    const PDFPageContentProcessorState& secondState = nodeAt(secondResult.container, { 1 })->getState()->getState();
    QVERIFY(secondState.getOverprintMode().overprintStroking);
    QVERIFY(!secondState.getOverprintMode().overprintFilling);
    QCOMPARE(secondState.getLineWidth(), 7.0);

    // /Font (Table 57): an array [font size], where the font is an indirect reference to a font
    // dictionary - the operands of Tf without a resource name. /GSF is << /Font [(the font of
    // /F1) 9] /LW 2.5 >>. gs0 re1 f2.
    {
        const PDFDocument fontDocument = createDocument("/GSF gs 0 0 5 5 re f");
        const PDFContentBuildResult fontResult = buildPage(fontDocument);
        QCOMPARE(describe(fontResult.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));
        QVERIFY(fontResult.issues.empty());

        const PDFContentNodePointer fontPath = nodeAt(fontResult.container, { 1 });
        const PDFContentPropertySource& fontSource = fontPath->getSymbolicState()->getSource(Property::Font);
        QVERIFY(fontSource.origin == Origin::ExtGState);
        QCOMPARE(fontSource.resourceName, QByteArray("GSF"));
        QCOMPARE(fontSource.extGStateKey, QByteArray("Font"));
        QCOMPARE(fontSource.operationIndex, size_t(0));

        // The object of the property is the font - the indirect reference, which is the first
        // item of the array -, never the array [font size]: the font is written back as
        // "/name 9 Tf" (ISO 32000-2, 9.3.1, Table 103: the operand of Tf is the name of a font
        // resource), and a resource name of the array would name an array as a font
        QVERIFY(fontSource.object.isReference());
        QVERIFY(fontSource.object == pageResource(fontDocument, "Font", "F1"));
        QVERIFY(fontDocument.getObject(fontSource.object).isDictionary());

        QVERIFY(fontPath->getState()->getState().getTextFont() != nullptr);
        QCOMPARE(fontPath->getState()->getState().getTextFontSize(), 9.0);
        QCOMPARE(fontPath->getState()->getState().getLineWidth(), 2.5);

        const std::vector<Property> leaked = { Property::LineWidth, Property::Font };
        QVERIFY(nodeAt(fontResult.container, { 0 })->getLeak().properties.getProperties() == leaked);
    }

    // A name in place of the reference (/GSFN is << /Font [/F1 7] >>) is taken by the processor
    // and looked up in the font resources, as Tf does (NOTES.txt): the object of the property is
    // the value of that name there - the same reference of the font -, the size is 7. gs0 re1 f2.
    {
        const PDFDocument nameDocument = createDocument("/GSFN gs 0 0 5 5 re f");
        const PDFContentBuildResult nameResult = buildPage(nameDocument);
        const PDFContentNodePointer namePath = nodeAt(nameResult.container, { 1 });
        const PDFContentPropertySource& nameSource = namePath->getSymbolicState()->getSource(Property::Font);
        QVERIFY(nameSource.origin == Origin::ExtGState);
        QCOMPARE(nameSource.resourceName, QByteArray("GSFN"));
        QVERIFY(nameSource.object.isReference());
        QVERIFY(nameSource.object == pageResource(nameDocument, "Font", "F1"));
        QCOMPARE(namePath->getState()->getState().getTextFontSize(), 7.0);
    }

    // An array of two items, whose first one is neither a reference nor a name (/GSFX is
    // << /Font [5 9] /LW 4 >>): "font shall be an indirect reference to a font dictionary"
    // (Table 57) - the entry sets no font, the other entry of the dictionary is applied
    {
        const PDFDocument wrongFontDocument = createDocument("/GSFX gs 0 0 5 5 re f");
        const PDFContentBuildResult wrongFontResult = buildPage(wrongFontDocument);
        const PDFContentNodePointer wrongFontPath = nodeAt(wrongFontResult.container, { 1 });
        QVERIFY(wrongFontPath->getSymbolicState()->isInherited(Property::Font));
        QVERIFY(wrongFontPath->getSymbolicState()->getSource(Property::LineWidth).origin == Origin::ExtGState);
        QVERIFY(wrongFontPath->getState()->getState().getTextFont() == nullptr);
        QCOMPARE(wrongFontPath->getState()->getState().getLineWidth(), 4.0);

        const std::vector<Property> leaked = { Property::LineWidth };
        QVERIFY(nodeAt(wrongFontResult.container, { 0 })->getLeak().properties.getProperties() == leaked);
    }

    // A value of a type the key cannot have (Table 57) sets nothing, as an entry, which is absent:
    // /GSW has such a value for every key - nothing has its source there, the state is the initial one
    {
        const PDFDocument wrongDocument = createDocument("/GSW gs 0 0 5 5 re f");
        const PDFContentBuildResult wrongResult = buildPage(wrongDocument);
        QCOMPARE(describe(wrongResult.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));

        const PDFContentNodePointer wrongPath = nodeAt(wrongResult.container, { 1 });
        for (int i = 0; i < int(Property::Count); ++i)
        {
            QVERIFY2(wrongPath->getSymbolicState()->isInherited(Property(i)), qPrintable(QString::number(i)));
        }
        QVERIFY(wrongPath->getSymbolicState()->getSoftMaskMatrix().isIdentity());

        const PDFPageContentProcessorState& wrongState = wrongPath->getState()->getState();
        QCOMPARE(wrongState.getLineWidth(), 1.0);
        QCOMPARE(wrongState.getMitterLimit(), 10.0);
        QCOMPARE(wrongState.getAlphaStroking(), 1.0);
        QCOMPARE(wrongState.getAlphaFilling(), 1.0);
        QVERIFY(wrongState.getBlendMode() == BlendMode::Normal);
        QVERIFY(wrongState.getTextFont() == nullptr);
    }
}

void ContentTreeTest::softMaskMatrix()
{
    using Property = PDFContentStateProperty;

    // zadani_jadro.md 4.3 / R-6: the space of a soft mask is the current transformation matrix of
    // the moment the mask is set by gs (ISO 32000-2, 11.6.5.1) - the symbolic state keeps it.
    // cm0 gs1 cm2 re3 f4 gs5 re6 f7: the mask of /GSM is set under [2 0 0 2 10 20]; the second
    // cm gives [1 0 0 1 5 5] x [2 0 0 2 10 20] = [2 0 0 2 20 30], under which it is set again.
    const PDFDocument document = createDocument("2 0 0 2 10 20 cm /GSM gs 1 0 0 1 5 5 cm 0 0 5 5 re f /GSM gs 0 0 5 5 re f");
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:3) Path(3:2) StateRun(5:1) Path(6:2)}"));

    const PDFContentNodePointer firstPath = nodeAt(result.container, { 1 });
    QVERIFY(firstPath->getState()->hasSoftMask());
    QCOMPARE(firstPath->getState()->getCurrentTransformationMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 20.0, 30.0));
    QCOMPARE(firstPath->getSymbolicState()->getSoftMaskMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 10.0, 20.0));
    QCOMPARE(firstPath->getSymbolicState()->getSource(Property::SoftMask).resourceName, QByteArray("GSM"));
    QCOMPARE(firstPath->getSymbolicState()->getSource(Property::SoftMask).operationIndex, size_t(1));

    // The resolved state does not point to the dictionary of the document; the object of the
    // mask is the one of the symbolic state
    QVERIFY(firstPath->getState()->getState().getSoftMask() == nullptr);
    QVERIFY(firstPath->getSymbolicState()->getSource(Property::SoftMask).object.isDictionary());

    const PDFContentNodePointer secondPath = nodeAt(result.container, { 3 });
    QCOMPARE(secondPath->getSymbolicState()->getSoftMaskMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 20.0, 30.0));
    QCOMPARE(secondPath->getSymbolicState()->getSource(Property::SoftMask).operationIndex, size_t(5));

    // The same mask dictionary set again in another space is another mask for the nodes after
    // it: the state run of the second gs leaks the soft mask
    QVERIFY(nodeAt(result.container, { 2 })->getLeak().properties.contains(Property::SoftMask));
    QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.contains(Property::SoftMask));
    QVERIFY(nodeAt(result.container, { 0 })->getLeak().isTransformChanged);

    // Before the first gs there is no mask and the matrix is the identity
    QVERIFY(!result.container->getInitialState()->hasSoftMask());
    QVERIFY(result.container->getInitialSymbolicState()->getSoftMaskMatrix().isIdentity());

    // q ... Q: the mask and its matrix end with the group. q0 cm1 gs2 Q3 re4 f5
    const PDFDocument groupDocument = createDocument("q 3 0 0 3 0 0 cm /GSM gs Q 0 0 5 5 re f");
    const PDFContentBuildResult groupResult = buildPage(groupDocument);
    const PDFContentNodePointer after = nodeAt(groupResult.container, { 1 });
    QVERIFY(!after->getState()->hasSoftMask());
    QVERIFY(after->getSymbolicState()->isInherited(Property::SoftMask));
    QVERIFY(after->getSymbolicState()->getSoftMaskMatrix().isIdentity());
}

// ---------------------------------------------------------------------------------------------
//  Builder: containers of forms and patterns, fingerprint, limits of the walk
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::formUsedManyTimes()
{
    // zadani_jadro.md 4.3: one pass without a descent - the builder never enters a form; a form
    // used a thousand times is one key of the result (the session builds it once).
    QByteArray content;
    for (int i = 0; i < 1000; ++i)
    {
        content += "/Fm1 Do\n";
    }

    const PDFDocument document = createDocument(content);
    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);

    const PDFObjectReference form = pageResource(document, "XObject", "Fm1").getReference();
    QCOMPARE(result.usedForms.size(), size_t(1));
    QVERIFY(result.usedForms.front() == PDFContentContainerKey::createForm(form));
    QVERIFY(result.usedPatternCells.empty());

    // A thousand leaves of the page, a thousand operations: the four operations of the form
    // ("/GF gs 0 1 0 rg 5 5 20 10 re f") are not among them
    QCOMPARE(result.container->getRoot()->getChildren().size(), size_t(1000));
    QCOMPARE(result.container->getOperations().getCount(), size_t(1000));
    QCOMPARE(result.container->getNodeCount(), size_t(1001));
    for (const PDFContentNodePointer& child : result.container->getRoot()->getChildren())
    {
        QVERIFY(child->getKind() == PDFContentNodeKind::FormUse);
        QVERIFY(child->getData().xobject == form);
        QVERIFY(child->getChildren().empty());
    }
    QVERIFY(result.issues.empty());
}

void ContentTreeTest::formContainer()
{
    using Property = PDFContentStateProperty;

    // The page of createFormDocument: "q /Fm1 Do Q\nq 1 0 0 1 100 0 cm /Fm1 Do Q\n/Fm2 Do\n" -
    // q0 Do1 Q2 q3 cm4 Do5 Q6 Do7
    const PDFDocument document = Documents::createFormDocument();
    const PDFObjectReference form1 = pageResource(document, "XObject", "Fm1").getReference();
    const PDFObjectReference form2 = pageResource(document, "XObject", "Fm2").getReference();

    const PDFContentBuildResult pageResult = buildPage(document);
    QVERIFY(pageResult.container != nullptr);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{Group(0:1/2:1){FormUse(1:1)} Group(3:1/6:1){StateRun(4:1) FormUse(5:1)} FormUse(7:1)}"));

    // The forms in the order of their first use, each once
    QCOMPARE(pageResult.usedForms.size(), size_t(2));
    QVERIFY(pageResult.usedForms[0] == PDFContentContainerKey::createForm(form1));
    QVERIFY(pageResult.usedForms[1] == PDFContentContainerKey::createForm(form2));

    // The box of a use: /BBox [0 0 40 30] under /Matrix [2 0 0 2 10 20] is x 10 ... 90, y 20 ... 80;
    // the second use is moved by 100
    QCOMPARE(nodeAt(pageResult.container, { 0, 0 })->getBoundingBox(), QRectF(10.0, 20.0, 80.0, 60.0));
    QCOMPARE(nodeAt(pageResult.container, { 1, 1 })->getBoundingBox(), QRectF(110.0, 20.0, 80.0, 60.0));
    QCOMPARE(nodeAt(pageResult.container, { 2 })->getBoundingBox(), QRectF(0.0, 0.0, 10.0, 10.0));    // /Fm2: /BBox [0 0 10 10]

    // The container of the form /Fm1: "/GF gs 0 1 0 rg 5 5 20 10 re f" - gs0 rg1 re2 f3
    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(form1);
    const PDFContentBuildResult formResult = build(document, formKey, false, nullptr, 0);
    QVERIFY(formResult.container != nullptr);
    QVERIFY(formResult.container->getKey() == formKey);
    QCOMPARE(describe(formResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(formResult.issues.empty());
    QCOMPARE(formResult.container->getOperations().getOriginalContent(), Documents::formContent());

    // /BBox and /Matrix of the form; the nodes are in the space of the form
    QCOMPARE(formResult.container->getBoundingBox(), QRectF(0.0, 0.0, 40.0, 30.0));
    QCOMPARE(formResult.container->getMatrix(), QTransform(2.0, 0.0, 0.0, 2.0, 10.0, 20.0));
    QCOMPARE(formResult.container->getStructuralParentKey(), PDFInteger(-1));
    QVERIFY(formResult.container->getInitialState()->getCurrentTransformationMatrix().isIdentity());
    QVERIFY(nodeAt(formResult.container, { 1 })->getState()->getCurrentTransformationMatrix().isIdentity());
    QCOMPARE(nodeAt(formResult.container, { 1 })->getBoundingBox(), QRectF(5.0, 5.0, 20.0, 10.0));

    // The resources of the form: /GF << /LW 4 >>
    QCOMPARE(nodeAt(formResult.container, { 1 })->getState()->getState().getLineWidth(), 4.0);
    QCOMPARE(nodeAt(formResult.container, { 1 })->getSymbolicState()->getSource(Property::LineWidth).resourceName, QByteArray("GF"));
    QVERIFY(formResult.container->getResources().getObject(PDFContentResourceCategory::ExtGState, "GF").isDictionary());

    // The form /Fm2 has no /Resources: the names of its content are those of the page, which
    // draws it (ISO 32000-2, 7.8.3) - /GP << /LW 6 >> of the page
    const PDFContentBuildResult inheritedResult = build(document, PDFContentContainerKey::createForm(form2), false, nullptr, 0);
    QVERIFY(inheritedResult.container != nullptr);
    QCOMPARE(describe(inheritedResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(!issueKinds(inheritedResult.issues).contains(int(PDFContentIssue::Kind::UndefinedResource)));
    QVERIFY(!issueKinds(inheritedResult.issues).contains(int(PDFContentIssue::Kind::OperatorFailed)));
    QCOMPARE(nodeAt(inheritedResult.container, { 1 })->getState()->getState().getLineWidth(), 6.0);
    QCOMPARE(nodeAt(inheritedResult.container, { 1 })->getSymbolicState()->getSource(Property::LineWidth).resourceName, QByteArray("GP"));
    QCOMPARE(inheritedResult.container->getBoundingBox(), QRectF(0.0, 0.0, 10.0, 10.0));
    QVERIFY(inheritedResult.container->getMatrix().isIdentity());

    // The container records the page, whose resources it was built with; a page and a form with
    // /Resources of its own have none
    QCOMPARE(inheritedResult.container->getInheritedResourcesPageIndex(), PDFInteger(0));
    QCOMPARE(formResult.container->getInheritedResourcesPageIndex(), PDFInteger(-1));
    QCOMPARE(pageResult.container->getInheritedResourcesPageIndex(), PDFInteger(-1));

    // The same form without /Resources drawn by two pages ("/Fm2 Do" each, the page resources
    // /XObject << /Fm2 >> /ExtGState << /GP << /Type /ExtGState /LW 6 >> >>): built from the
    // page index 1, it records the page 1, and so does a new version made by the editor (a
    // change: its path is marked as modified). The second page itself records nothing.
    PDFDocumentBuilder twoPagesBuilder;
    const PDFObject sharedForm = Documents::addFormWithoutResourcesObject(&twoPagesBuilder);
    for (int i = 0; i < 2; ++i)
    {
        const PDFObject graphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "LW", PDFObject::createInteger(6) } });
        const PDFObject pageResources = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", sharedForm } }) },
                                                                            { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicState } }) } });
        const PDFObjectReference page = twoPagesBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        twoPagesBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", pageResources },
                                                                          { "Contents", Documents::addStreamObject(&twoPagesBuilder, Documents::DictionaryEntries(), "/Fm2 Do") } }));
    }
    const PDFDocument twoPagesDocument = twoPagesBuilder.build();
    const PDFContentContainerKey sharedFormKey = PDFContentContainerKey::createForm(sharedForm.getReference());

    const PDFContentBuildResult secondPageFormResult = build(twoPagesDocument, sharedFormKey, false, nullptr, 1);
    QVERIFY(secondPageFormResult.container != nullptr);
    QCOMPARE(describe(secondPageFormResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QCOMPARE(secondPageFormResult.container->getInheritedResourcesPageIndex(), PDFInteger(1));
    QCOMPARE(build(twoPagesDocument, sharedFormKey, false, nullptr, 0).container->getInheritedResourcesPageIndex(), PDFInteger(0));

    {
        PDFContentContainerEditor editor(secondPageFormResult.container);
        const PDFContentNodeId pathId = nodeAt(secondPageFormResult.container, { 1 })->getId();
        PDFContentNode::Data data = editor.getNode(pathId)->getData();
        data.isModified = true;
        QVERIFY(editor.setNodeData(pathId, std::move(data)));
        const PDFContentContainerPointer edited = editor.build();
        QVERIFY(!edited->isUnchanged());
        QCOMPARE(edited->getInheritedResourcesPageIndex(), PDFInteger(1));
    }

    const PDFContentContainerKey secondPageKey = PDFContentContainerKey::createPage(1, twoPagesDocument.getCatalog()->getPage(1)->getPageReference());
    const PDFContentBuildResult secondPageResult = build(twoPagesDocument, secondPageKey, false, nullptr, 1);
    QVERIFY(secondPageResult.container != nullptr);
    QCOMPARE(secondPageResult.container->getInheritedResourcesPageIndex(), PDFInteger(-1));

    // An empty container of a new key records nothing
    QCOMPARE(PDFContentContainerEditor(sharedFormKey).build()->getInheritedResourcesPageIndex(), PDFInteger(-1));

    // A form needs the page, which uses it (the processor and the inherited resources): without
    // it there is no container and an issue says why
    const PDFContentBuildResult noPageResult = build(document, formKey, false, nullptr, -1);
    QVERIFY(noPageResult.container == nullptr);
    QVERIFY(!noPageResult.isCanceled);
    QCOMPARE(issueKinds(noPageResult.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
    QVERIFY(!noPageResult.issues.front().text.isEmpty());

    const PDFContentBuildResult wrongPageResult = build(document, formKey, false, nullptr, 5);
    QVERIFY(wrongPageResult.container == nullptr);
    QCOMPARE(issueKinds(wrongPageResult.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
}

void ContentTreeTest::formResourcesInherited_data()
{
    QTest::addColumn<int>("variant");
    QTest::addColumn<qint64>("inheritedResourcesPageIndex");
    QTest::addColumn<double>("lineWidth");

    // ISO 32000-2, 7.3.7: an entry with the value null is the same as no entry; 7.3.10: an
    // indirect reference to an object, which does not exist, is the null object. A form without
    // a dictionary of resources of its own uses the resources of the page (7.8.3): it records
    // the page 0 and its /GP is the one of the page (/LW 6). A form with a dictionary (even an
    // empty one) has resources of its own: it records no page; /GP of its own has /LW 3, in the
    // empty dictionary /GP is undefined and the line width keeps its initial value 1.0
    // (Table 51).
    QTest::newRow("no entry") << 0 << qint64(0) << 6.0;
    QTest::newRow("null") << 1 << qint64(0) << 6.0;
    QTest::newRow("a reference to a missing object") << 2 << qint64(0) << 6.0;
    QTest::newRow("a reference to a null object") << 3 << qint64(0) << 6.0;
    QTest::newRow("a dictionary") << 4 << qint64(-1) << 3.0;
    QTest::newRow("a reference to a dictionary") << 5 << qint64(-1) << 3.0;
    QTest::newRow("an empty dictionary") << 6 << qint64(-1) << 1.0;
}

void ContentTreeTest::formResourcesInherited()
{
    QFETCH(int, variant);
    QFETCH(qint64, inheritedResourcesPageIndex);
    QFETCH(double, lineWidth);

    // page: "/Fm1 Do", /XObject << /Fm1 >> /ExtGState << /GP << /LW 6 >> >>
    // /Fm1: "/GP gs 1 0 0 rg 0 0 10 10 re f" with the /Resources of the row
    PDFDocumentBuilder builder;
    const PDFObjectReference formReference = builder.addObject(PDFObject());
    const PDFObject ownResources = Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GP", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(3) } }) } }) } });
    const PDFObjectReference missingReference(900, 0);
    PDFObject resources;
    switch (variant)
    {
        case 1:
            resources = PDFObject::createNull();
            break;
        case 2:
            resources = PDFObject::createReference(missingReference);
            break;
        case 3:
            resources = Documents::addObject(&builder, PDFObject());
            break;
        case 4:
            resources = ownResources;
            break;
        case 5:
            resources = Documents::addObject(&builder, ownResources);
            break;
        case 6:
            resources = PDFObject::createDictionary(PDFDictionaryBuilder());
            break;
        default:
            break;
    }

    {
        // setObject keeps an entry with the null value (addObject would drop it)
        const QByteArray content = Documents::formWithoutResourcesContent();
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 10, 10 }));
        dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        if (variant != 0)
        {
            dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), resources);
        }
        builder.setObject(formReference, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(content))));
    }

    const PDFObject pageResources = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", PDFObject::createReference(formReference) } }) },
                                                                        { "ExtGState", Documents::createDictionaryObject({ { "GP", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(6) } }) } }) } });
    const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", pageResources },
                                                                       { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    const PDFDocument document = builder.build();

    // The document really has the entry of the row (the test would prove nothing otherwise)
    const PDFDictionary* formDictionary = document.getObjectByReference(formReference).getStream()->getDictionary();
    QCOMPARE(formDictionary->hasKey("Resources"), variant != 0);
    QVERIFY(formDictionary->get("Resources") == resources);
    QVERIFY(PDFInteger(document.getStorage().getObjects().size()) <= missingReference.objectNumber);
    if (variant == 3)
    {
        QVERIFY(resources.getReference().objectNumber < PDFInteger(document.getStorage().getObjects().size()));
        QVERIFY(document.getObjectByReference(resources.getReference()).isNull());
    }

    const PDFContentBuildResult result = build(document, PDFContentContainerKey::createForm(formReference), false, nullptr, 0);
    QVERIFY(result.container != nullptr);
    QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QCOMPARE(result.container->getInheritedResourcesPageIndex(), PDFInteger(inheritedResourcesPageIndex));
    QCOMPARE(nodeAt(result.container, { 1 })->getState()->getState().getLineWidth(), lineWidth);
    QCOMPARE(issueKinds(result.issues).contains(int(PDFContentIssue::Kind::UndefinedResource)), variant == 6);
}

void ContentTreeTest::formInitialState()
{
    using Property = PDFContentStateProperty;
    using Properties = std::vector<Property>;

    // zadani_jadro.md 4.10: the state at the beginning of a form is the state of the place of its
    // first use; the properties the form does not set are inherited, and their value is the
    // value of that one use only. The page sets the line width 4 and the fill color green
    // before /Fm1 - exactly what the form sets itself ("/GF gs 0 1 0 rg", /GF is << /LW 4 >>).
    // cm0 w1 rg2 J3 Do4
    const PDFDocument document = createDocument("3 0 0 3 7 8 cm 4 w 0 1 0 rg 1 J /Fm1 Do");
    const PDFContentBuildResult pageResult = buildPage(document);
    QVERIFY(pageResult.container != nullptr);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{StateRun(0:4) FormUse(4:1)}"));

    const PDFContentNodePointer use = nodeAt(pageResult.container, { 1 });
    const PDFContentResolvedStatePointer useState = use->getState();
    QCOMPARE(useState->getState().getLineWidth(), 4.0);
    QCOMPARE(useState->getCurrentTransformationMatrix(), QTransform(3.0, 0.0, 0.0, 3.0, 7.0, 8.0));

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(use->getData().xobject);
    const PDFContentBuildResult formResult = build(document, formKey, false, useState, 0);
    QVERIFY(formResult.container != nullptr);
    QCOMPARE(describe(formResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));

    // The initial state: the values of the use, in the space of the form (the matrix of the
    // page is not a part of it)
    const PDFContentResolvedStatePointer initial = formResult.container->getInitialState();
    QVERIFY(initial != nullptr);
    QCOMPARE(initial->getState().getLineWidth(), 4.0);
    QVERIFY(initial->getState().getLineCapStyle() == Qt::RoundCap);     // 1 J
    QCOMPARE(initial->getState().getFillColorOriginal().size(), size_t(3));
    QCOMPARE(double(initial->getState().getFillColorOriginal()[1]), 1.0);
    QVERIFY(initial->getCurrentTransformationMatrix().isIdentity());
    QVERIFY(formResult.container->getRoot()->getState() == initial);

    // Everything is inherited at the beginning
    const PDFContentSymbolicStatePointer initialSymbolic = formResult.container->getInitialSymbolicState();
    QVERIFY(initialSymbolic->isInherited(Property::LineWidth));
    QVERIFY(initialSymbolic->isInherited(Property::FillColor));
    QVERIFY(initialSymbolic->isInherited(Property::LineCap));

    // The state run sets the line width and the fill color to the values they have in this
    // use. In a form they leak nevertheless: another use can come with other values (R-2:
    // an inherited property counts as unknown).
    const PDFContentNodePointer run = nodeAt(formResult.container, { 0 });
    QVERIFY(run->getLeak().properties.getProperties() == Properties({ Property::LineWidth, Property::FillColor }));
    QVERIFY(run->getLeak().endSymbolicState->getSource(Property::LineWidth).origin == PDFContentPropertySource::Origin::ExtGState);
    QVERIFY(run->getLeak().endSymbolicState->isInherited(Property::LineCap));

    // The same operators in a page, where the values are known: the second state run sets
    // what is set already - no leak. w0 rg1 re2 f3 w4 rg5 re6 f7
    const PDFDocument pageDocument = createDocument("4 w 0 1 0 rg 0 0 5 5 re f 4 w 0 1 0 rg 0 0 5 5 re f");
    const PDFContentBuildResult pageOnlyResult = buildPage(pageDocument);
    QCOMPARE(describe(pageOnlyResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2) StateRun(4:2) Path(6:2)}"));
    QVERIFY(nodeAt(pageOnlyResult.container, { 0 })->getLeak().properties.getProperties() == Properties({ Property::LineWidth, Property::FillColor }));
    QVERIFY(nodeAt(pageOnlyResult.container, { 2 })->getLeak().isEmpty());

    // Without an initial state a form starts with the default state
    const PDFContentBuildResult defaultResult = build(document, formKey, false, nullptr, 0);
    QCOMPARE(defaultResult.container->getInitialState()->getState().getLineWidth(), 1.0);
    QVERIFY(defaultResult.container->getInitialState()->getState().getLineCapStyle() == Qt::FlatCap);

    // A soft mask of the place of the use is known inside of the form (the flag, not the object)
    const PDFDocument maskDocument = createDocument("/GSM gs /Fm1 Do");
    const PDFContentBuildResult maskPageResult = buildPage(maskDocument);
    const PDFContentNodePointer maskUse = nodeAt(maskPageResult.container, { 1 });
    QVERIFY(maskUse->getState()->hasSoftMask());
    const PDFContentBuildResult maskFormResult = build(maskDocument, PDFContentContainerKey::createForm(maskUse->getData().xobject), false, maskUse->getState(), 0);
    QVERIFY(maskFormResult.container->getInitialState()->hasSoftMask());
    QVERIFY(maskFormResult.container->getInitialState()->getState().getSoftMask() == nullptr);
    QVERIFY(nodeAt(maskFormResult.container, { 1 })->getState()->hasSoftMask());
    QVERIFY(maskFormResult.container->getInitialSymbolicState()->isInherited(Property::SoftMask));
}

void ContentTreeTest::formCycleAndSharedForm()
{
    // zadani_overeni.md 6.1: the walk over the forms is the session's (one map of visited keys);
    // the builder gives it the keys and never descends, so a form, which draws itself, and a form
    // shared by two forms are no danger for it:
    //
    //   page  "/A Do /C Do /A Do"      resources /A, /C
    //   A     "/A Do /B Do /B Do"      resources /A (itself), /B
    //   C     "/B Do"                  resources /B
    //   B     "0 0 1 1 re f"
    PDFObjectReference referenceA;
    PDFObjectReference referenceB;
    PDFObjectReference referenceC;

    const PDFDocument document = Documents::createDocument(QByteArray("/A Do /C Do /A Do"), [&](PDFDocumentBuilder* builder)
    {
        auto createForm = [](PDFObject xobjects, const QByteArray& content)
        {
            PDFDictionaryBuilder dictionary;
            dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
            dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
            dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 10, 10 }));
            dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), Documents::createDictionaryObject({ { "XObject", std::move(xobjects) } }));
            dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
            return PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(content)));
        };

        referenceA = builder->addObject(PDFObject());
        referenceB = builder->addObject(createForm(Documents::createDictionaryObject({ }), "0 0 1 1 re f"));
        referenceC = builder->addObject(createForm(Documents::createDictionaryObject({ { "B", PDFObject::createReference(referenceB) } }), "/B Do"));
        builder->setObject(referenceA, createForm(Documents::createDictionaryObject({ { "A", PDFObject::createReference(referenceA) },
                                                                                      { "B", PDFObject::createReference(referenceB) } }), "/A Do /B Do /B Do"));

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "A", PDFObject::createReference(referenceA) },
                                                                                                   { "C", PDFObject::createReference(referenceC) } }));
        return resources;
    });

    using Keys = std::vector<PDFContentContainerKey>;
    const PDFContentContainerKey keyA = PDFContentContainerKey::createForm(referenceA);
    const PDFContentContainerKey keyB = PDFContentContainerKey::createForm(referenceB);
    const PDFContentContainerKey keyC = PDFContentContainerKey::createForm(referenceC);

    // The page: three leaves; A once, in the order of the first uses; B is not a form of the page
    const PDFContentBuildResult pageResult = buildPage(document);
    QVERIFY(pageResult.container != nullptr);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{FormUse(0:1) FormUse(1:1) FormUse(2:1)}"));
    QVERIFY(pageResult.usedForms == Keys({ keyA, keyC }));
    QCOMPARE(pageResult.container->getOperations().getCount(), size_t(3));

    // A: the build ends, though the form draws itself; itself and B, each once
    const PDFContentBuildResult resultA = build(document, keyA, false, nullptr, 0);
    QVERIFY(resultA.container != nullptr);
    QCOMPARE(describe(resultA.container), QStringLiteral("Root{FormUse(0:1) FormUse(1:1) FormUse(2:1)}"));
    QVERIFY(resultA.usedForms == Keys({ keyA, keyB }));
    QCOMPARE(resultA.container->getOperations().getCount(), size_t(3));
    QVERIFY(nodeAt(resultA.container, { 0 })->getData().xobject == referenceA);
    QVERIFY(resultA.issues.empty());

    // C reaches the shared form B too
    const PDFContentBuildResult resultC = build(document, keyC, false, nullptr, 0);
    QVERIFY(resultC.usedForms == Keys({ keyB }));

    // B uses nothing
    const PDFContentBuildResult resultB = build(document, keyB, false, nullptr, 0);
    QCOMPARE(describe(resultB.container), QStringLiteral("Root{Path(0:2)}"));
    QVERIFY(resultB.usedForms.empty());
}

void ContentTreeTest::patternCell()
{
    using Property = PDFContentStateProperty;

    // The page of createTilingPatternDocument: "/Pattern cs /P1 scn 25 25 40 40 re f\n" -
    // cs0 scn1 re2 f3. The tiling pattern is a container of its own (the session builds it,
    // when it is entered), the builder does not unfold its cells.
    const PDFDocument document = Documents::createTilingPatternDocument();
    const PDFObjectReference pattern = pageResource(document, "Pattern", "P1").getReference();

    const PDFContentBuildResult pageResult = buildPage(document);
    QVERIFY(pageResult.container != nullptr);
    QCOMPARE(describe(pageResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QCOMPARE(pageResult.usedPatternCells.size(), size_t(1));
    QVERIFY(pageResult.usedPatternCells.front() == PDFContentContainerKey::createPatternCell(pattern));
    QVERIFY(pageResult.usedForms.empty());
    QCOMPARE(pageResult.container->getOperations().getCount(), size_t(4));     // Not the four operations of the cell too

    const PDFContentPropertySource& fill = nodeAt(pageResult.container, { 1 })->getSymbolicState()->getSource(Property::FillColor);
    QCOMPARE(fill.patternName, QByteArray("P1"));
    QVERIFY(fill.patternObject == PDFObject::createReference(pattern));

    // The cell of /P1: "/GT gs 1 0 0 rg 0 0 10 10 re f" with the resources of the pattern
    // (/GT << /LW 2 >>), /BBox [0 0 10 10], /Matrix [1 0 0 1 5 5]
    const PDFContentContainerKey cellKey = PDFContentContainerKey::createPatternCell(pattern);
    const PDFContentBuildResult cellResult = build(document, cellKey, false, nullptr, 0);
    QVERIFY(cellResult.container != nullptr);
    QVERIFY(cellResult.container->getKey() == cellKey);
    QCOMPARE(describe(cellResult.container), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(cellResult.issues.empty());
    QCOMPARE(cellResult.container->getOperations().getOriginalContent(), Documents::tilingPatternContent());
    QCOMPARE(cellResult.container->getBoundingBox(), QRectF(0.0, 0.0, 10.0, 10.0));
    QCOMPARE(cellResult.container->getMatrix(), QTransform(1.0, 0.0, 0.0, 1.0, 5.0, 5.0));
    QCOMPARE(nodeAt(cellResult.container, { 1 })->getState()->getState().getLineWidth(), 2.0);
    QVERIFY(nodeAt(cellResult.container, { 1 })->getState()->getCurrentTransformationMatrix().isIdentity());

    // A pattern of a form is found by the build of the form, not of the page: "/FmP Do" - the
    // form /FmP fills with its pattern /P2
    const PDFDocument formDocument = Documents::createTilingPatternDocument("/FmP Do");
    const PDFContentBuildResult formPageResult = buildPage(formDocument);
    QCOMPARE(formPageResult.usedForms.size(), size_t(1));
    QVERIFY(formPageResult.usedPatternCells.empty());

    const PDFContentBuildResult formResult = build(formDocument, formPageResult.usedForms.front(), false, nullptr, 0);
    QVERIFY(formResult.container != nullptr);
    QCOMPARE(formResult.usedPatternCells.size(), size_t(1));
    QVERIFY(formResult.usedPatternCells.front() == PDFContentContainerKey::createPatternCell(pageResource(formDocument, "Pattern", "P2").getReference()));

    // Every pattern once, in the order of the first uses, for filling and for stroking:
    // /P1 (fill), /P2 (fill), /P1 again, /P2 (stroke), /P3 (stroke)
    const PDFDocument manyDocument = Documents::createTilingPatternDocument("/Pattern cs /P1 scn 0 0 5 5 re f /P2 scn 0 0 5 5 re f /P1 scn 0 0 5 5 re f "
                                                                            "/Pattern CS /P2 SCN 0 0 5 5 re S /P3 SCN 0 0 5 5 re S");
    const PDFContentBuildResult manyResult = buildPage(manyDocument);
    QVERIFY(manyResult.container != nullptr);
    const std::vector<PDFContentContainerKey> expectedCells = { PDFContentContainerKey::createPatternCell(pageResource(manyDocument, "Pattern", "P1").getReference()),
                                                                PDFContentContainerKey::createPatternCell(pageResource(manyDocument, "Pattern", "P2").getReference()),
                                                                PDFContentContainerKey::createPatternCell(pageResource(manyDocument, "Pattern", "P3").getReference()) };
    QVERIFY(manyResult.usedPatternCells == expectedCells);

    // A key of a pattern cell, which names a form, is no container (and the other way round)
    QVERIFY(build(formDocument, PDFContentContainerKey::createPatternCell(formPageResult.usedForms.front().reference), false, nullptr, 0).container == nullptr);
    QVERIFY(build(document, PDFContentContainerKey::createForm(pattern), false, nullptr, 0).container == nullptr);
}

void ContentTreeTest::uncoloredPatternCell()
{
    using Property = PDFContentStateProperty;
    using Properties = std::vector<Property>;
    using Origin = PDFContentPropertySource::Origin;
    using Keys = std::vector<PDFContentContainerKey>;

    // ISO 32000-2, Table 74, /PaintType: 1 is a colored tiling pattern, whose content stream
    // specifies the colors of the cell; 2 is an uncolored one - "The content stream shall not
    // invoke operators that specify colours or other colour-related parameters in the graphics
    // state; otherwise, the operator is ignored and processing of the stream continues without
    // error", the cell "is painted with a separately specified colour each time the pattern is
    // used". The initial color of a content is DeviceGray, black (Table 51).
    //
    // The patterns of the page (/BBox [0 0 10 10], steps 20):
    //   /PC   /PaintType 1, the content cellContent
    //   /PU   /PaintType 2, the same content
    //   /PUI  /PaintType (a reference to the integer 2), the same content
    //   /PCM  /PaintType 1, the content mixedContent, /Resources << /Pattern << /PC >> >>
    //   /PUM  /PaintType 2, the same content and resources
    //   /W0 (no /PaintType), /W3 (3), /WZ (0), /WR (the real 2.0), /WN (the name /Uncolored):
    //         patterns with a wrong paint type
    //   /WS   a stream with /PatternType 2 and /PaintType 1: a paint type does not make a stream
    //         a tiling pattern
    const QByteArray cellContent("1 0 0 rg 0 0 10 10 re f");                            // rg0 re1 f2
    const QByteArray mixedContent("/Pattern cs /PC scn 0 0 1 RG 2 w 0 0 10 10 re B");   // cs0 scn1 RG2 w3 re4 B5

    // The page uses /PC and two of the wrong patterns: cs0 scn1 re2 f3 scn4 re5 f6 scn7 re8 f9
    const QByteArray pageContent("/Pattern cs /PC scn 0 0 5 5 re f /W3 scn 0 0 5 5 re f /WN scn 0 0 5 5 re f");

    const PDFDocument document = Documents::createDocument(pageContent, [&](PDFDocumentBuilder* builder)
    {
        auto addPattern = [builder](PDFObject paintType, PDFObject resources, const QByteArray& content)
        {
            Documents::DictionaryEntries entries = { { "Type", PDFObject::createName("Pattern") },
                                                     { "PatternType", PDFObject::createInteger(1) },
                                                     { "TilingType", PDFObject::createInteger(1) },
                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                     { "XStep", PDFObject::createInteger(20) },
                                                     { "YStep", PDFObject::createInteger(20) },
                                                     { "Resources", std::move(resources) } };
            if (!paintType.isNull())
            {
                entries.emplace_back("PaintType", std::move(paintType));
            }
            return Documents::addStreamObject(builder, std::move(entries), content);
        };

        auto noResources = []() { return Documents::createDictionaryObject(Documents::DictionaryEntries()); };

        PDFObject colored = addPattern(PDFObject::createInteger(1), noResources(), cellContent);
        auto mixedResources = [&colored]() { return Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "PC", colored } }) } }); };

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({
            { "PC", colored },
            { "PU", addPattern(PDFObject::createInteger(2), noResources(), cellContent) },
            { "PUI", addPattern(Documents::addObject(builder, PDFObject::createInteger(2)), noResources(), cellContent) },
            { "PCM", addPattern(PDFObject::createInteger(1), mixedResources(), mixedContent) },
            { "PUM", addPattern(PDFObject::createInteger(2), mixedResources(), mixedContent) },
            { "W0", addPattern(PDFObject(), noResources(), cellContent) },
            { "W3", addPattern(PDFObject::createInteger(3), noResources(), cellContent) },
            { "WZ", addPattern(PDFObject::createInteger(0), noResources(), cellContent) },
            { "WR", addPattern(PDFObject::createReal(2.0), noResources(), cellContent) },
            { "WN", addPattern(PDFObject::createName("Uncolored"), noResources(), cellContent) },
            { "WS", Documents::addStreamObject(builder, { { "PatternType", PDFObject::createInteger(2) }, { "PaintType", PDFObject::createInteger(1) } }, cellContent) } }));
        return resources;
    });

    auto cellKey = [&document](const char* name)
    {
        return PDFContentContainerKey::createPatternCell(pageResource(document, "Pattern", name).getReference());
    };

    auto hasFailedOperation = [](const PDFContentContainerPointer& container)
    {
        const PDFContentOperationList& operations = container->getOperations();
        for (size_t i = 0; i < operations.getCount(); ++i)
        {
            if (operations.getOperation(i).getFlags().testFlag(PDFContentOperation::Failed))
            {
                return true;
            }
        }
        return false;
    };

    // A new version of a container made by the editor (a change: its last node is marked as
    // modified); it keeps the facts of the container it was made from
    auto editedVersion = [](const PDFContentContainerPointer& container)
    {
        PDFContentContainerEditor editor(container);
        const PDFContentNodeId id = container->getRoot()->getChildren().back()->getId();
        PDFContentNode::Data data = container->getNode(id)->getData();
        data.isModified = true;
        return editor.setNodeData(id, std::move(data)) ? editor.build() : PDFContentContainerPointer();
    };

    // The colored cell: rg sets the fill color, the rectangle is filled red
    {
        const PDFContentBuildResult result = build(document, cellKey("PC"), false, nullptr, 0);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));
        QVERIFY(result.issues.empty());

        // Its color operators have an effect; the cell has /Resources of its own
        QVERIFY(!result.container->isColorIgnored());
        QCOMPARE(result.container->getInheritedResourcesPageIndex(), PDFInteger(-1));
        const PDFContentContainerPointer edited = editedVersion(result.container);
        QVERIFY(edited != nullptr);
        QVERIFY(!edited->isUnchanged());
        QVERIFY(!edited->isColorIgnored());

        const PDFContentNodePointer path = nodeAt(result.container, { 1 });
        const PDFPageContentProcessorState& state = path->getState()->getState();
        QVERIFY(state.getFillColorSpace()->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceRGB);
        QCOMPARE(state.getFillColorOriginal().size(), size_t(3));
        QCOMPARE(double(state.getFillColorOriginal()[0]), 1.0);
        QCOMPARE(double(state.getFillColorOriginal()[1]), 0.0);
        QCOMPARE(double(state.getFillColorOriginal()[2]), 0.0);
        QCOMPARE(state.getFillColor().red(), 255);

        const PDFContentPropertySource& fill = path->getSymbolicState()->getSource(Property::FillColor);
        QVERIFY(fill.origin == Origin::Operator);
        QCOMPARE(fill.operationIndex, size_t(0));
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == Properties({ Property::FillColor }));

        QVERIFY(result.container->isUnchanged());
        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, cellContent);
    }

    // The same content in the uncolored cell (the paint type direct, then by a reference): rg
    // is ignored. The tree is the same - the operation stays an operation of the content - but
    // the rectangle is not red: its colors are those of the beginning of the cell, the initial
    // DeviceGray black of the builder, and their source is inherited (the color of the use,
    // which paints with the pattern; a cell does not know its uses). Nothing leaks.
    for (const char* name : { "PU", "PUI" })
    {
        const PDFContentBuildResult result = build(document, cellKey(name), false, nullptr, 0);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:1) Path(1:2)}"));

        const PDFContentNodePointer run = nodeAt(result.container, { 0 });
        const PDFContentNodePointer path = nodeAt(result.container, { 1 });
        const PDFPageContentProcessorState& state = path->getState()->getState();
        QVERIFY(state.getFillColorSpace()->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray);
        QCOMPARE(state.getFillColorOriginal().size(), size_t(1));
        QCOMPARE(double(state.getFillColorOriginal()[0]), 0.0);
        QCOMPARE(state.getFillColor().red(), 0);
        QCOMPARE(state.getFillColor().green(), 0);
        QCOMPARE(state.getFillColor().blue(), 0);

        // No state of the cell differs from its initial one: the same shared object
        QVERIFY(path->getState() == result.container->getInitialState());
        QVERIFY(run->getState() == result.container->getInitialState());

        QVERIFY(path->getSymbolicState()->isInherited(Property::FillColor));
        QVERIFY(path->getSymbolicState()->getSource(Property::FillColor).origin == Origin::Inherited);
        QVERIFY(path->getSymbolicState()->isInherited(Property::StrokeColor));
        QVERIFY(run->getLeak().isEmpty());

        // The container says it once, at the operation: an issue, not a failed operation ("is
        // ignored and processing of the stream continues without error")
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::IgnoredColorOperator }));
        QCOMPARE(result.issues.front().operationIndex, size_t(0));
        QVERIFY(result.issues.front().nodeId == run->getId());
        QVERIFY(!result.issues.front().text.isEmpty());
        QVERIFY(!hasFailedOperation(result.container));

        // The ignored operator is written back as it is: an unchanged cell byte for byte
        QCOMPARE(result.container->getOperations().getCount(), size_t(3));
        QCOMPARE(result.container->getOperations().getOriginalContent(), cellContent);
        QVERIFY(result.container->isUnchanged());
        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, cellContent);
        QVERIFY(!written.resources.isChanged());

        // The container says, that color operators written into it have no effect; a new
        // version made by the editor still says so
        QVERIFY(result.container->isColorIgnored());
        const PDFContentContainerPointer edited = editedVersion(result.container);
        QVERIFY(edited != nullptr);
        QVERIFY(!edited->isUnchanged());
        QVERIFY(edited->isColorIgnored());
    }

    // Every kind of the color operators (a color space, a pattern, a device color; filling
    // and stroking) next to an operator, which is none (2 w). Colored: all of them are
    // sources, the pattern of scn is used.
    {
        const PDFContentBuildResult result = build(document, cellKey("PCM"), false, nullptr, 0);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:4) Path(4:2)}"));
        QVERIFY(result.issues.empty());
        QVERIFY(result.usedPatternCells == Keys({ cellKey("PC") }));

        const PDFContentNodePointer path = nodeAt(result.container, { 1 });
        QVERIFY(nodeAt(result.container, { 0 })->getLeak().properties.getProperties() == Properties({ Property::LineWidth, Property::StrokeColor, Property::FillColor }));
        QCOMPARE(path->getSymbolicState()->getSource(Property::FillColor).patternName, QByteArray("PC"));
        QCOMPARE(path->getSymbolicState()->getSource(Property::FillColor).operationIndex, size_t(1));
        QCOMPARE(path->getSymbolicState()->getSource(Property::StrokeColor).operationIndex, size_t(2));
        QCOMPARE(path->getState()->getState().getStrokeColorOriginal().size(), size_t(3));
        QCOMPARE(double(path->getState()->getState().getStrokeColorOriginal()[2]), 1.0);
    }

    // Uncolored: none of them sets anything and no pattern is used; the line width is set as
    // everywhere. One issue for the whole cell, at the first color operator.
    {
        const PDFContentBuildResult result = build(document, cellKey("PUM"), false, nullptr, 0);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{StateRun(0:4) Path(4:2)}"));
        QVERIFY(result.usedPatternCells.empty());

        const PDFContentNodePointer run = nodeAt(result.container, { 0 });
        const PDFContentNodePointer path = nodeAt(result.container, { 1 });
        QVERIFY(run->getLeak().properties.getProperties() == Properties({ Property::LineWidth }));
        QVERIFY(path->getSymbolicState()->isInherited(Property::FillColor));
        QVERIFY(path->getSymbolicState()->isInherited(Property::StrokeColor));
        QVERIFY(path->getSymbolicState()->getSource(Property::FillColor).patternName.isEmpty());
        QVERIFY(path->getSymbolicState()->getSource(Property::LineWidth).origin == Origin::Operator);

        const PDFPageContentProcessorState& state = path->getState()->getState();
        QCOMPARE(state.getLineWidth(), 2.0);
        QVERIFY(state.getFillColorSpace()->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray);
        QVERIFY(state.getStrokeColorSpace()->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray);
        QCOMPARE(state.getStrokeColorOriginal().size(), size_t(1));
        QCOMPARE(double(state.getStrokeColorOriginal()[0]), 0.0);

        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::IgnoredColorOperator }));
        QCOMPARE(result.issues.front().operationIndex, size_t(0));
        QVERIFY(!hasFailedOperation(result.container));
        QVERIFY(result.container->isColorIgnored());

        QVERIFY(result.container->isUnchanged());
        const PDFContentStreamWriteResult written = PDFContentStreamWriter::write(*result.container);
        QVERIFY(written.isSuccess());
        QCOMPARE(written.content, mixedContent);
    }

    // A paint type, which is neither the integer 1 nor 2 (Table 74: required, 1 or 2): the
    // pattern reading of the processor refuses such a pattern, it is never painted - its cell
    // is no container. Neither is a stream, which is not a tiling pattern, whatever its paint type.
    for (const char* name : { "W0", "W3", "WZ", "WR", "WN", "WS" })
    {
        const PDFContentBuildResult result = build(document, cellKey(name), false, nullptr, 0);
        QVERIFY2(result.container == nullptr, name);
        QVERIFY(!result.isCanceled);
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
        QVERIFY(!result.issues.front().text.isEmpty());
        QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, cellKey(name)).isEmpty());
    }

    // ... and the page, which names it by scn, does not list it: the operator fails there
    const PDFContentBuildResult pageResult = buildPage(document);
    QVERIFY(pageResult.container != nullptr);
    QVERIFY(pageResult.usedPatternCells == Keys({ cellKey("PC") }));
    const PDFContentOperationList& pageOperations = pageResult.container->getOperations();
    QCOMPARE(pageOperations.getCount(), size_t(10));
    QVERIFY(!pageOperations.getOperation(1).getFlags().testFlag(PDFContentOperation::Failed));
    QVERIFY(pageOperations.getOperation(4).getFlags().testFlag(PDFContentOperation::Failed));
    QVERIFY(pageOperations.getOperation(7).getFlags().testFlag(PDFContentOperation::Failed));

    // Color operators of a page and of a form have an effect, also in a new version; an empty
    // container of a new key (the key of an uncolored cell) does not know it either
    QVERIFY(!pageResult.container->isColorIgnored());
    QVERIFY(!editedVersion(pageResult.container)->isColorIgnored());

    const PDFDocument formDocument = Documents::createFormDocument();
    const PDFContentBuildResult formResult = build(formDocument, PDFContentContainerKey::createForm(pageResource(formDocument, "XObject", "Fm1").getReference()), false, nullptr, 0);
    QVERIFY(formResult.container != nullptr);
    QVERIFY(!formResult.container->isColorIgnored());
    QVERIFY(!editedVersion(formResult.container)->isColorIgnored());

    QVERIFY(!PDFContentContainerEditor(cellKey("PU")).build()->isColorIgnored());
}

void ContentTreeTest::fingerprint()
{
    // zadani_jadro.md 4.2, 4.6 (RE-15): the fingerprint is a hash of the decoded bytes of the
    // streams and of the references of the resource dictionaries; equal fingerprints mean the
    // identifiers of the nodes are valid for the document.
    const QByteArray content("q 1 w 0 0 5 5 re f Q");
    const PDFDocument document = createDocument(content);
    const PDFContentContainerKey key = pageKey(document);

    const PDFContentBuildResult result = buildPage(document);
    QVERIFY(result.container != nullptr);
    const QByteArray fingerprint = result.container->getFingerprint();
    QVERIFY(!fingerprint.isEmpty());

    // The fingerprint without a build is the one of the container
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&document, key), fingerprint);

    // The same content and resources in another document object
    const PDFDocument sameDocument = createDocument(content);
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&sameDocument, pageKey(sameDocument)), fingerprint);

    // One more byte of the content (a white space: the operations are the same)
    const PDFDocument spaceDocument = createDocument("q 1 w  0 0 5 5 re f Q");
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&spaceDocument, pageKey(spaceDocument)) != fingerprint);

    // One byte of the content differs, the length is the same
    const PDFDocument byteDocument = createDocument("q 2 w 0 0 5 5 re f Q");
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&byteDocument, pageKey(byteDocument)) != fingerprint);

    // The same bytes in two streams instead of one (the positions of the operations are others):
    // one stream with a line feed in it against two streams, which are joined by one (7.8.2).
    // The pages have no resources, so nothing but the streams differs.
    const PDFDocument joinedDocument = Documents::createDocument(QByteArray("q 1 w\n0 0 5 5 re f Q"), &Documents::createNoResources);
    const PDFDocument splitDocument = Documents::createDocument(QList<QByteArray>({ "q 1 w", "0 0 5 5 re f Q" }), &Documents::createNoResources);
    const PDFContentBuildResult joinedResult = buildPage(joinedDocument);
    const PDFContentBuildResult splitResult = buildPage(splitDocument);
    QCOMPARE(joinedResult.container->getOperations().getOriginalContent(), splitResult.container->getOperations().getOriginalContent());
    QVERIFY(joinedResult.container->getFingerprint() != splitResult.container->getFingerprint());
    QVERIFY(!joinedResult.container->getFingerprint().isEmpty());

    // The same content with other resources: a name means another object. The resources of
    // createResources with /GS1 pointing to another dictionary.
    auto createChangedResources = [](PDFDocumentBuilder* builder, const char* name, PDFObject value, const char* category)
    {
        PDFDictionaryBuilder resources = createResources(builder);
        const PDFDictionary* original = resources.getDictionary();

        PDFDictionaryBuilder changed;
        for (size_t i = 0; i < original->getCount(); ++i)
        {
            if (original->getKey(i).getString() == category)
            {
                const PDFDictionary* categoryDictionary = original->getValue(i).getDictionary();
                Documents::DictionaryEntries entries;
                for (size_t j = 0; j < categoryDictionary->getCount(); ++j)
                {
                    const QByteArray entryName = categoryDictionary->getKey(j).getString();
                    entries.emplace_back(entryName, entryName == name ? value : categoryDictionary->getValue(j));
                }
                changed.addEntry(PDFInplaceOrMemoryString(original->getKey(i).getString()), Documents::createDictionaryObject(std::move(entries)));
            }
            else
            {
                changed.addEntry(PDFInplaceOrMemoryString(original->getKey(i).getString()), PDFObject(original->getValue(i)));
            }
        }
        return changed;
    };

    const PDFDocument graphicStateDocument = Documents::createDocument(content, [&](PDFDocumentBuilder* builder)
    {
        return createChangedResources(builder, "GS1", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(8) } }), "ExtGState");
    });
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&graphicStateDocument, pageKey(graphicStateDocument)) != fingerprint);

    // A reference of the resources points to another object (the entry /F1 is the reference of
    // another font object here)
    const PDFDocument fontDocument = Documents::createDocument(content, [&](PDFDocumentBuilder* builder)
    {
        return createChangedResources(builder, "F1", Documents::addStandardFontObject(builder, "Courier"), "Font");
    });
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&fontDocument, pageKey(fontDocument)) != fingerprint);

    // The test of the helper itself: unchanged resources give the same fingerprint
    const PDFDocument unchangedDocument = Documents::createDocument(content, [&](PDFDocumentBuilder* builder)
    {
        return createChangedResources(builder, "NoSuchName", PDFObject(), "ExtGState");
    });
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&unchangedDocument, pageKey(unchangedDocument)), fingerprint);

    // A key, which names nothing of the document
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, PDFContentContainerKey()).isEmpty());
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, PDFContentContainerKey::createPage(1, key.reference)).isEmpty());
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, PDFContentContainerKey::createPage(0, PDFObjectReference(9999, 0))).isEmpty());
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, PDFContentContainerKey::createForm(key.reference)).isEmpty());
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(nullptr, key).isEmpty());

    // A form: its own stream and its own /Resources only, so no page is needed - and the
    // fingerprint of a form without /Resources does not change with the resources of the page,
    // which it inherits
    const PDFDocument formDocument = Documents::createFormDocument();
    const PDFContentContainerKey form1 = PDFContentContainerKey::createForm(pageResource(formDocument, "XObject", "Fm1").getReference());
    const PDFContentContainerKey form2 = PDFContentContainerKey::createForm(pageResource(formDocument, "XObject", "Fm2").getReference());
    const QByteArray fingerprint1 = PDFContentTreeBuilder::computeFingerprint(&formDocument, form1);
    const QByteArray fingerprint2 = PDFContentTreeBuilder::computeFingerprint(&formDocument, form2);
    QVERIFY(!fingerprint1.isEmpty());
    QVERIFY(!fingerprint2.isEmpty());
    QVERIFY(fingerprint1 != fingerprint2);
    QCOMPARE(build(formDocument, form1, false, nullptr, 0).container->getFingerprint(), fingerprint1);
    QCOMPARE(build(formDocument, form2, false, nullptr, 0).container->getFingerprint(), fingerprint2);
    QVERIFY(fingerprint1 != PDFContentTreeBuilder::computeFingerprint(&formDocument, pageKey(formDocument)));

    // The same form without /Resources under a page with other resources
    PDFObjectReference inheritingForm;
    const PDFDocument otherPageDocument = Documents::createDocument(QByteArray("/Fm2 Do"), [&](PDFDocumentBuilder* builder)
    {
        const PDFObject form = Documents::addFormWithoutResourcesObject(builder);
        inheritingForm = form.getReference();

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm2", form } }));
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GP", Documents::createDictionaryObject({ { "LW", PDFObject::createInteger(9) } }) } }));
        return resources;
    });
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&otherPageDocument, PDFContentContainerKey::createForm(inheritingForm)), fingerprint2);
}

void ContentTreeTest::invalidKeys()
{
    const PDFDocument document = createDocument("1 w");
    const PDFContentContainerKey key = pageKey(document);
    const PDFObjectReference image = pageResource(document, "XObject", "Im1").getReference();
    const PDFObjectReference font = pageResource(document, "Font", "F1").getReference();

    // A key, which names nothing of the document: no container, not canceled, one issue with
    // the reason
    const std::vector<PDFContentContainerKey> keys =
    {
        PDFContentContainerKey(),
        PDFContentContainerKey::createPage(1, key.reference),                   // The document has one page
        PDFContentContainerKey::createPage(-1, key.reference),
        PDFContentContainerKey::createPage(0, PDFObjectReference(9999, 0)),     // Not the object of the page 0
        PDFContentContainerKey::createForm(image),                              // An image is no form
        PDFContentContainerKey::createForm(font),                               // Not a stream
        PDFContentContainerKey::createForm(PDFObjectReference(9999, 0)),
        PDFContentContainerKey::createPatternCell(image)
    };

    for (const PDFContentContainerKey& invalidKey : keys)
    {
        const PDFContentBuildResult result = build(document, invalidKey, false, nullptr, 0);
        QVERIFY(result.container == nullptr);
        QVERIFY(!result.isCanceled);
        QCOMPARE(issueKinds(result.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));
        QVERIFY(!result.issues.front().text.isEmpty());
        QVERIFY(!result.issues.front().nodeId.isValid());
        QVERIFY(result.usedForms.empty());
        QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, invalidKey).isEmpty());
    }

    // No document
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFContentBuildContext context;
    context.fontCache = &fontCache;
    context.cms = &cms;
    const PDFContentBuildResult noDocument = PDFContentTreeBuilder::build(nullptr, key, nullptr, context, PDFContentBuildOptions(), nullptr, nullptr);
    QVERIFY(noDocument.container == nullptr);
    QCOMPARE(issueKinds(noDocument.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));

    // No caches: the builder cannot read the fonts and the colors
    const PDFContentBuildResult noCaches = PDFContentTreeBuilder::build(&document, key, nullptr, PDFContentBuildContext(), PDFContentBuildOptions(), nullptr, nullptr);
    QVERIFY(noCaches.container == nullptr);
    QCOMPARE(issueKinds(noCaches.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));

    // One of the two is not enough
    PDFContentBuildContext fontCacheOnly;
    fontCacheOnly.fontCache = &fontCache;
    const PDFContentBuildResult noColorManagement = PDFContentTreeBuilder::build(&document, key, nullptr, fontCacheOnly, PDFContentBuildOptions(), nullptr, nullptr);
    QVERIFY(noColorManagement.container == nullptr);
    QCOMPARE(issueKinds(noColorManagement.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));

    PDFContentBuildContext colorManagementOnly;
    colorManagementOnly.cms = &cms;
    const PDFContentBuildResult noFontCache = PDFContentTreeBuilder::build(&document, key, nullptr, colorManagementOnly, PDFContentBuildOptions(), nullptr, nullptr);
    QVERIFY(noFontCache.container == nullptr);
    QCOMPARE(issueKinds(noFontCache.issues), kinds({ PDFContentIssue::Kind::InvalidContainer }));

    // The valid key builds (the cache of ICC profiles may be missing)
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    const PDFContentBuildResult valid = PDFContentTreeBuilder::build(&document, key, nullptr, context, PDFContentBuildOptions(), nullptr, nullptr);
    QVERIFY(valid.container != nullptr);
    QCOMPARE(describe(valid.container), QStringLiteral("Root{StateRun(0:1)}"));
}

/// A control, which is canceled from the beginning, or after a number of questions
class TestControl : public PDFOperationControl
{
public:
    explicit TestControl(int allowedQuestions) : m_remaining(allowedQuestions) { }

    virtual bool isOperationCanceled() const override
    {
        return m_remaining.fetch_sub(1) <= 0;
    }

private:
    mutable std::atomic<int> m_remaining;
};

/// A progress, which counts its calls
class TestProgress : public PDFProgressSink
{
public:
    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override { Q_UNUSED(startupInfo); ++starts; steps = stepCount; }
    virtual void step() override { ++done; }
    virtual void finish() override { ++finishes; }

    int starts = 0;
    int finishes = 0;
    size_t steps = 0;
    size_t done = 0;
};

void ContentTreeTest::cancelAndProgress()
{
    QByteArray content;
    for (int i = 0; i < 2000; ++i)
    {
        content += "0 0 5 5 re f\n";
    }
    const PDFDocument document = createDocument(content);

    // Canceled before the start
    {
        const TestControl control(0);
        const PDFContentBuildResult result = build(document, pageKey(document), false, nullptr, -1, &control);
        QVERIFY(result.isCanceled);
        QVERIFY(result.container == nullptr);
        QVERIFY(result.issues.empty());
    }

    // Canceled in the middle of the content: no half-made container
    {
        const TestControl control(500);
        const PDFContentBuildResult result = build(document, pageKey(document), false, nullptr, -1, &control);
        QVERIFY(result.isCanceled);
        QVERIFY(result.container == nullptr);
    }

    // Not canceled: the whole content, and the progress is started, stepped to its end and finished
    {
        const TestControl control(std::numeric_limits<int>::max());
        TestProgress progress;
        const PDFContentBuildResult result = build(document, pageKey(document), false, nullptr, -1, &control, &progress);
        QVERIFY(!result.isCanceled);
        QVERIFY(result.container != nullptr);
        QCOMPARE(result.container->getRoot()->getChildren().size(), size_t(2000));
        QCOMPARE(progress.starts, 1);
        QCOMPARE(progress.finishes, 1);
        QVERIFY(progress.steps > 0);
        QCOMPARE(progress.done, progress.steps);
    }
}

// ---------------------------------------------------------------------------------------------
//  Builder: text (PDFContentTextLayout of package P06)
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::textLayout()
{
    // zadani_jadro.md 4.3: the builder has its own machine of the text matrices and lays the
    // glyphs out by ISO 32000-2, 9.4.4 with the widths of the font dictionary. The font /F1 of
    // the shared documents: A (65) 700, B (66) 650, the space (32) 300 thousandths.
    //   tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th
    {
        // BT0 Tf1 Td2 Tj3 TJ4 ET5, Tfs = 10, Th = 1
        //   Td: Tm = Tlm = translate(100, 200)
        //   Tj (AB): A at 100, tx = 0.7 * 10 = 7; B at 107, tx = 6.5; Tm at 113.5
        //   TJ [(A) -500 (B)]: A at 113.5, tx = 7; the number -500: tx = 500 / 1000 * 10 = 5;
        //   B at 113.5 + 7 + 5 = 125.5, tx = 6.5
        const PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 100 200 Td (AB) Tj [(A) -500 (B)] TJ ET");
        const PDFContentBuildResult result = buildPage(document, true);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/5:1){StateRun(1:2) TextShow(3:1) TextShow(4:1)}}"));
        QVERIFY(result.issues.empty());

        const PDFContentNode::Data& first = nodeAt(result.container, { 0, 1 })->getData();
        QCOMPARE(first.textItems.size(), size_t(2));
        QVERIFY(first.textItems[0].kind == PDFContentTextItem::Kind::Glyph);
        QCOMPARE(first.textItems[0].codeBytes, QByteArray("A"));
        QCOMPARE(first.textItems[0].code, 65u);
        QVERIFY(isTranslation(first.textItems[0].textMatrix, 100.0, 200.0));
        QCOMPARE(first.textItems[0].advance, 7.0);
        QCOMPARE(first.textItems[1].codeBytes, QByteArray("B"));
        QVERIFY(isTranslation(first.textItems[1].textMatrix, 107.0, 200.0));
        QCOMPARE(first.textItems[1].advance, 6.5);
        QVERIFY(first.isTextComplete);

        // The text state of the operator
        QVERIFY(first.textState.font != nullptr);
        QCOMPARE(first.textState.fontSize, 10.0);
        QCOMPARE(first.textState.horizontalScaling, 100.0);
        QCOMPARE(first.textState.characterSpacing, 0.0);
        QVERIFY(first.textState.renderingMode == TextRenderingMode::Fill);

        const PDFContentNode::Data& second = nodeAt(result.container, { 0, 2 })->getData();
        QCOMPARE(second.textItems.size(), size_t(3));
        QVERIFY(second.textItems[0].kind == PDFContentTextItem::Kind::Glyph);
        QVERIFY(isTranslation(second.textItems[0].textMatrix, 113.5, 200.0));
        QVERIFY(second.textItems[1].kind == PDFContentTextItem::Kind::Spacer);
        QCOMPARE(second.textItems[1].adjustment, -500.0);
        QVERIFY(isTranslation(second.textItems[1].textMatrix, 120.5, 200.0));
        QCOMPARE(second.textItems[1].advance, 5.0);
        QVERIFY(second.textItems[2].kind == PDFContentTextItem::Kind::Glyph);
        QVERIFY(isTranslation(second.textItems[2].textMatrix, 125.5, 200.0));
        QVERIFY(second.isTextComplete);

        // The box of the text is somewhere around its glyphs: it holds the origin of the first
        // glyph (the outlines are those of a font of the system, a test cannot know them)
        const QRectF box = nodeAt(result.container, { 0 })->getBoundingBox();
        QVERIFY(!box.isNull());
        QVERIFY(box.left() < 110.0 && box.right() > 110.0);
        QVERIFY(box.top() < 205.0 && box.bottom() > 195.0);
    }

    {
        // BT0 Tf1 TL2 Td3 '4 "5 TD6 T*7 Tj8 Tm9 Tj10 ET11, Tfs = 10, Table 106 and Table 107:
        //   TL 12; Td: Tlm = translate(100, 200)
        //   (A) ': T* first - Tlm = translate(100, 188); A at (100, 188), tx = 7
        //   2 3 (B ) ": Tw = 2, Tc = 3; T* - (100, 176); B at (100, 176), tx = 6.5 + 3 = 9.5;
        //     the space (the single-byte code 32) at (109.5, 176), tx = 3 + 3 + 2 = 8
        //   5 -7 TD: the leading is 7; Tlm = translate(105, 169)
        //   T*: (105, 162)
        //   (A) Tj: A at (105, 162), tx = 7 + 3 = 10 (Tc is still 3)
        //   1 0 0 1 50 60 Tm: Tm = Tlm = translate(50, 60)
        //   (B) Tj: B at (50, 60)
        const PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 12 TL 100 200 Td (A) ' 2 3 (B ) \" 5 -7 TD T* (A) Tj 1 0 0 1 50 60 Tm (B) Tj ET");
        const PDFContentBuildResult result = buildPage(document, true);
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/11:1){StateRun(1:3) TextShow(4:1) TextShow(5:1) StateRun(6:2) TextShow(8:1) StateRun(9:1) TextShow(10:1)}}"));

        const PDFContentNode::Data& nextLine = nodeAt(result.container, { 0, 1 })->getData();
        QCOMPARE(nextLine.textItems.size(), size_t(1));
        QVERIFY(isTranslation(nextLine.textItems[0].textMatrix, 100.0, 188.0));
        QCOMPARE(nextLine.textItems[0].advance, 7.0);

        const PDFContentNode::Data& spacing = nodeAt(result.container, { 0, 2 })->getData();
        QCOMPARE(spacing.textItems.size(), size_t(2));
        QVERIFY(isTranslation(spacing.textItems[0].textMatrix, 100.0, 176.0));
        QCOMPARE(spacing.textItems[0].advance, 9.5);
        QVERIFY(!spacing.textItems[0].isWordSpace);
        QVERIFY(isTranslation(spacing.textItems[1].textMatrix, 109.5, 176.0));
        QCOMPARE(spacing.textItems[1].advance, 8.0);
        QVERIFY(spacing.textItems[1].isWordSpace);
        QCOMPARE(spacing.textState.wordSpacing, 2.0);
        QCOMPARE(spacing.textState.characterSpacing, 3.0);
        QCOMPARE(spacing.textState.leading, 12.0);

        const PDFContentNode::Data& afterMove = nodeAt(result.container, { 0, 4 })->getData();
        QCOMPARE(afterMove.textItems.size(), size_t(1));
        QVERIFY(isTranslation(afterMove.textItems[0].textMatrix, 105.0, 162.0));
        QCOMPARE(afterMove.textItems[0].advance, 10.0);
        QCOMPARE(afterMove.textState.leading, 7.0);

        const PDFContentNode::Data& afterMatrix = nodeAt(result.container, { 0, 6 })->getData();
        QCOMPARE(afterMatrix.textItems.size(), size_t(1));
        QVERIFY(isTranslation(afterMatrix.textItems[0].textMatrix, 50.0, 60.0));
    }

    {
        // Two text objects: BT sets the matrices to the identity again (9.4.1). The string of
        // the second object starts at the origin, not after the first one.
        const PDFDocument document = Documents::createFontsDocument("BT /F1 10 Tf 30 40 Td (A) Tj ET BT (B) Tj ET");
        const PDFContentBuildResult result = buildPage(document, true);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/4:1){StateRun(1:2) TextShow(3:1)} TextObject(5:1/7:1){TextShow(6:1)}}"));
        QVERIFY(isTranslation(nodeAt(result.container, { 0, 1 })->getData().textItems[0].textMatrix, 30.0, 40.0));
        QVERIFY(isTranslation(nodeAt(result.container, { 1, 0 })->getData().textItems[0].textMatrix, 0.0, 0.0));
        QCOMPARE(nodeAt(result.container, { 1, 0 })->getData().textState.fontSize, 10.0);   // The font stays (9.3.1)
    }

    {
        // A font without widths in its dictionary (a standard font without /Widths): the glyphs
        // are there, their positions are not those of the standard - the text is movable as
        // a whole only (zadani_jadro.md 4.7)
        const PDFDocument document = Documents::createType3Document("BT /F1 10 Tf (AB) Tj ET", Documents::Type3Variant::Plain);
        const PDFContentBuildResult result = buildPage(document, true);
        const PDFContentNode::Data& show = nodeAt(result.container, { 0, 1 })->getData();
        QCOMPARE(show.textItems.size(), size_t(2));
        QVERIFY(!show.textItems[0].width.has_value());
        QVERIFY(!show.isTextComplete);
    }

    {
        // A show operator without a font: the processor refuses it; no glyphs, not complete
        const PDFDocument document = Documents::createFontsDocument("BT (A) Tj ET");
        const PDFContentBuildResult result = buildPage(document, true);
        QCOMPARE(describe(result.container), QStringLiteral("Root{TextObject(0:1/2:1){TextShow(1:1)}}"));
        const PDFContentNode::Data& show = nodeAt(result.container, { 0, 0 })->getData();
        QVERIFY(show.textItems.empty());
        QVERIFY(!show.isTextComplete);
        QVERIFY(show.textState.font == nullptr);
        QVERIFY(result.container->getOperations().getOperation(1).getFlags().testFlag(PDFContentOperation::Failed));
    }
}

void ContentTreeTest::textWithoutLayout()
{
    // PDFContentBuildOptions::layoutText: a caller, which needs only the structure, gets no
    // glyphs; the tree and the states are the same
    const QByteArray content("BT /F1 10 Tf 100 200 Td (AB) Tj ET");
    const PDFDocument document = Documents::createFontsDocument(content);

    const PDFContentBuildResult withoutLayout = buildPage(document, false);
    const PDFContentBuildResult withLayout = buildPage(document, true);
    QVERIFY(withoutLayout.container != nullptr);
    QCOMPARE(describe(withoutLayout.container), describe(withLayout.container));
    QVERIFY(identifiers(withoutLayout.container->getRoot()) == identifiers(withLayout.container->getRoot()));
    QCOMPARE(withoutLayout.container->getFingerprint(), withLayout.container->getFingerprint());

    const PDFContentNode::Data& plain = nodeAt(withoutLayout.container, { 0, 1 })->getData();
    QVERIFY(plain.textItems.empty());
    QVERIFY(!plain.isTextComplete);
    QCOMPARE(plain.textState.fontSize, 10.0);
    QVERIFY(plain.textState.font != nullptr);

    QCOMPARE(nodeAt(withLayout.container, { 0, 1 })->getData().textItems.size(), size_t(2));
    QVERIFY(nodeAt(withoutLayout.container, { 0 })->getLeak().properties == nodeAt(withLayout.container, { 0 })->getLeak().properties);
    QVERIFY(nodeAt(withoutLayout.container, { 0 })->getLeak().properties.contains(PDFContentStateProperty::Font));
}

// ---------------------------------------------------------------------------------------------
//  Threads and ownership
// ---------------------------------------------------------------------------------------------

void ContentTreeTest::containerOutlivesDocument()
{
    // zadani_jadro.md 4.2 / zadani_overeni.md 6.3: the container co-owns everything it refers to
    // (fonts, realized fonts with their outlines, objects) and holds no pointer into the
    // document, a cache or the builder. The document and the caches of the build are gone,
    // before the container is read (a memory checker sees a read of freed memory).
    PDFContentContainerPointer container;
    PDFContentContainerPointer formContainer;
    PDFObject fontReference;
    {
        const PDFDocument document = createDocument("q 2 0 0 2 10 20 cm /GSA gs 1 0 0 rg /Cs1 CS /Pattern cs /P1 scn 0 0 5 5 re f /Im1 Do /Fm1 Do Q\n"
                                                    "/Span /MC1 BDC BT /F1 10 Tf 30 40 Td (AB) Tj /T3 5 Tf (A) Tj ET EMC /Sh1 sh (tail");
        const PDFContentBuildResult result = buildPage(document, true);
        QVERIFY(result.container != nullptr);
        container = result.container;
        fontReference = pageResource(document, "Font", "F1");

        const PDFContentBuildResult formResult = build(document, result.usedForms.front(), true, nodeAt(container, { 0, 3 })->getState(), 0);
        QVERIFY(formResult.container != nullptr);
        formContainer = formResult.container;
    }

    // q0 cm1 gs2 rg3 CS4 cs5 scn6 re7 f8 Do9 Do10 Q11 BDC12 BT13 Tf14 Td15 Tj16 Tf17 Tj18 ET19 EMC20 sh21
    QCOMPARE(describe(container), QStringLiteral("Root{Group(0:1/11:1){StateRun(1:6) Path(7:2) Image(9:1) FormUse(10:1)} "
                                                 "MarkedContent(12:1/20:1){TextObject(13:1/19:1){StateRun(14:2) TextShow(16:1) StateRun(17:1) TextShow(18:1)}} "
                                                 "Shading(21:1) Opaque}"));

    using Property = PDFContentStateProperty;
    size_t glyphCount = 0;
    for (const quint64 id : identifiers(container->getRoot()))
    {
        const PDFContentNodePointer node = container->getNode(PDFContentNodeId{ id });
        QVERIFY(node != nullptr);

        // The states: values, color spaces, the font
        const PDFPageContentProcessorState& state = node->getState()->getState();
        QVERIFY(state.getFillColorSpace() != nullptr);
        QVERIFY(state.getFillColorSpace()->getColorComponentCount() <= 4);
        QVERIFY(state.getStrokeColorSpace() != nullptr);
        QVERIFY(state.getSoftMask() == nullptr);
        if (state.getTextFont())
        {
            QVERIFY(state.getTextFont()->getFontType() != FontType::Invalid);
        }

        // The objects of the symbolic state
        for (int i = 0; i < int(Property::Count); ++i)
        {
            const PDFContentPropertySource& source = node->getSymbolicState()->getSource(Property(i));
            if (source.object.isDictionary())
            {
                QVERIFY(source.object.getDictionary()->getCount() > 0);
            }
        }

        // The glyphs and their outlines (owned by the realized font of the text state)
        for (const PDFContentTextItem& item : node->getData().textItems)
        {
            if (item.kind == PDFContentTextItem::Kind::Glyph)
            {
                ++glyphCount;
                if (item.outline)
                {
                    QVERIFY(node->getData().textState.realizedFont != nullptr);
                    QVERIFY(item.outline->getBoundingRect().width() >= 0.0);
                }
            }
        }

        if (node->getData().properties.isDictionary())
        {
            QCOMPARE(node->getData().properties.getDictionary()->getCount(), size_t(1));
        }
    }
    QCOMPARE(glyphCount, size_t(3));    // A, B and the A of the Type 3 font

    // The operations and the resources
    QCOMPARE(container->getOperations().getCount(), size_t(22));
    QCOMPARE(container->getOperations().getOperation(16).getRawBytes(), QByteArray(" (AB) Tj"));
    QCOMPARE(container->getOperations().getTail(), QByteArray(" (tail"));
    QVERIFY(container->getResources().getObject(PDFContentResourceCategory::Font, "F1") == fontReference);
    QVERIFY(container->getResources().getObject(PDFContentResourceCategory::ColorSpace, "Cs1").isArray());

    const PDFContentPropertySource& mask = nodeAt(container, { 0, 1 })->getSymbolicState()->getSource(Property::SoftMask);
    QVERIFY(mask.object.isDictionary());
    QVERIFY(mask.object.getDictionary()->get("S") == PDFObject::createName("Luminosity"));

    // The container of the form, built with the state of the use as its initial state
    QCOMPARE(describe(formContainer), QStringLiteral("Root{StateRun(0:2) Path(2:2)}"));
    QVERIFY(formContainer->getInitialState()->hasSoftMask());
    QCOMPARE(formContainer->getInitialState()->getState().getAlphaFilling(), 0.75);     // /ca of /GSA
}

void ContentTreeTest::buildInThreads()
{
    // zadani_overeni.md 6.3: the builder is reentrant (one task per container in a worker
    // thread, each with its own caches); the container is immutable and read by any thread.
    // Nothing here needs an event loop, a GUI thread or a canvas (zadani.md 12.7).
    const QByteArray content("q 2 0 0 2 10 20 cm /GSA gs 1 0 0 rg 0 0 5 5 re f /Fm1 Do Q\n"
                             "BT /F1 10 Tf 30 40 Td (AB) Tj ET /T BMC 1 w EMC\n");
    const PDFDocument document = createDocument(content);
    const QString expected = QStringLiteral("Root{Group(0:1/7:1){StateRun(1:3) Path(4:2) FormUse(6:1)} "
                                            "TextObject(8:1/12:1){StateRun(9:2) TextShow(11:1)} MarkedContent(13:1/15:1){StateRun(14:1)}}");

    constexpr int THREAD_COUNT = 8;
    std::vector<PDFContentBuildResult> results(THREAD_COUNT);
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_COUNT; ++i)
        {
            threads.emplace_back([&document, &results, i]() { results[size_t(i)] = buildPage(document, true); });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }
    }

    for (const PDFContentBuildResult& result : results)
    {
        QVERIFY(result.container != nullptr);
        QCOMPARE(describe(result.container), expected);
        QCOMPARE(result.container->getFingerprint(), results.front().container->getFingerprint());
        QVERIFY(identifiers(result.container->getRoot()) == identifiers(results.front().container->getRoot()));
        QCOMPARE(result.usedForms.size(), size_t(1));
    }

    // One container read by more threads at once: every thread walks the tree and maps points
    // by the matrices of the states (a matrix must be plain data, when it is shared)
    const PDFContentContainerPointer container = results.front().container;
    std::atomic<int> failures = 0;
    std::atomic<qint64> sum = 0;
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_COUNT; ++i)
        {
            threads.emplace_back([&container, &failures, &sum]()
            {
                qreal total = 0.0;
                for (int round = 0; round < 200; ++round)
                {
                    for (quint64 id = 1; id <= container->getMaximumNodeId().value; ++id)
                    {
                        const PDFContentNodePointer node = container->getNode(PDFContentNodeId{ id });
                        if (!node || !node->getState() || !node->getSymbolicState())
                        {
                            ++failures;
                            continue;
                        }

                        const QPointF point = node->getState()->getCurrentTransformationMatrix().map(QPointF(1.0, 1.0));
                        total += point.x() + node->getSymbolicState()->getSoftMaskMatrix().map(QPointF(1.0, 0.0)).x();
                        for (const PDFContentTextItem& item : node->getData().textItems)
                        {
                            total += item.textMatrix.map(QPointF(0.0, 0.0)).x();
                        }
                        if (container->getParent(node->getId()) == nullptr && id != 1)
                        {
                            ++failures;
                        }
                    }
                }
                sum += qint64(total);
            });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }
    }

    QCOMPARE(failures.load(), 0);

    // Every thread has computed the same sum
    QCOMPARE(sum.load() % THREAD_COUNT, qint64(0));
    QVERIFY(sum.load() > 0);
}

QTEST_MAIN(ContentTreeTest)

#include "tst_contenttreetest.moc"
