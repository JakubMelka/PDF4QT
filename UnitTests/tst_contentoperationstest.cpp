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

// Edit Content, stage E0: the operations of a content, its resources, the writer of a content
// stream and the ExtGState dictionaries of the editor (package P01; GUI_overhaul/edit_tool/
// zadani_jadro.md 4.2, 4.4, 4.8, zadani_overeni.md chapter 8).
//
// The expected values are written by hand from the plan (rules R-1, R-4, R-6, R-7) and from
// ISO 32000-2 (7.2.3, 7.8.2, 7.8.3, 8.4.4, 8.4.5, 8.5, 8.6.8, 9.3, 11.6.5.1), never taken from
// a run. The containers of the writer tests are built by hand through the constructors of
// PDFContentNode and PDFContentContainer: the tests do not depend on the tree builder, except the
// last group (built*), which checks the whole way from a document to its bytes.

#include "contenttestdocuments.h"

#include "pdfcms.h"
#include "pdfcontentextgstate.h"
#include "pdfcontentnode.h"
#include "pdfcontentoperation.h"
#include "pdfcontentresources.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"

#include <QtTest>

#include <atomic>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <vector>

using namespace pdf;
using pdftest::ContentTestDocuments;

/// One operation of a hand-written content: its operator, its bytes (from the end of the
/// previous operation to the end of its operator) and the resources it names
struct ContentPiece
{
    PDFContentOperator contentOperator = PDFContentOperator::Invalid;
    QByteArray rawBytes;
    std::vector<PDFContentResourceName> resourceNames;
};

/// One row of the table of the properties written by an operator of their own
struct OperatorPropertyRow
{
    const char* title = nullptr;
    PDFContentStateProperty property = PDFContentStateProperty::LineWidth;
    PDFContentStateValue value;
    QByteArray expectedLines;   ///< Empty: the value cannot be written (an error of the node)
};

class ContentOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();

    // Operations and their list
    void operationCreation();
    void listOriginalContent_data();
    void listOriginalContent();
    void listOperationOverStreams();
    void listGeneratedAfterOriginal();
    void listCopiesShareOperations();
    void listPrefix();
    void listCopiesInThreads();

    // Resources (R-4)
    void resourcesRead();
    void resourcesAllocateName();
    void resourcesReservedNames();
    void resourcesOriginalEntriesStay();
    void resourcesWrite();
    void resourcesOfFormWithoutResources();
    void resourcesHostileInput();
    void resourcesManyNames();
    void resourcesCopiesInThreads();
    void resourcesLookupByObject();

    // ExtGState
    void extGStateKeys_data();
    void extGStateKeys();
    void extGStateDictionary();
    void extGStateRefusedValues();
    void extGStateSoftMaskDictionary();
    void extGStateEqual();
    void extGStateEqualityKey();

    // Canonical writing
    void writePath_data();
    void writePath();
    void writePathNotFinite();
    void writePathPaint_data();
    void writePathPaint();
    void writeMatrix();

    // Writer of a container
    void writeUnchanged_data();
    void writeUnchanged();
    void writeUnchangedTree();
    void writeJoints();
    void writeSuppressedOperation();
    void writeWrap();
    void writeWrapSharesExtGState();
    void writeWrapSharesExtGStateOfMany();
    void writeWrapProperties();
    void writeWrapExtGStateProperties();
    void writeWrapOverprint();
    void writeWrapResourceNames();
    void writeSoftMask();
    void writeResidualStateRun();
    void writeClippingPath();
    void writePaintingAndClippingPath();
    void writeNodesWithoutWrap();
    void writeModifiedPath();
    void writeErrors();
    void writeInThreads();

    // From a document to its bytes (needs the tree builder of package P02)
    void builtContainerIsWrittenByteForByte_data();
    void builtContainerIsWrittenByteForByte();
    void builtContainerResources();

private:
    using Category = PDFContentResourceCategory;
    using Property = PDFContentStateProperty;
    using Kind = PDFContentNodeKind;
    using Op = PDFContentOperator;

    static void addContentRows();

    static ContentPiece piece(Op contentOperator, const char* rawBytes, std::vector<PDFContentResourceName> resourceNames = {});
    static PDFContentOperationList createList(const std::vector<ContentPiece>& pieces, const QByteArray& tail);

    static PDFObject name(const char* text) { return PDFObject::createName(QByteArray(text)); }
    static PDFObject reference(PDFInteger objectNumber) { return PDFObject::createReference(PDFObjectReference(objectNumber, 0)); }
    static PDFObject dictionary(ContentTestDocuments::DictionaryEntries entries) { return ContentTestDocuments::createDictionaryObject(std::move(entries)); }

    /// The dictionary has exactly the given keys with the given values (in any order)
    static bool hasExactly(const PDFObject& object, const ContentTestDocuments::DictionaryEntries& entries);

    /// The names of the entries of a category, in their order
    static QList<QByteArray> entryNames(const PDFContentResources& resources, Category category);

    static PDFContentStateValue realValue(PDFReal value);
    static PDFContentStateValue integerValue(PDFInteger value);
    static PDFContentStateValue booleanValue(bool value);
    static PDFContentStateValue nameValue(const char* value);
    static PDFContentStateValue objectValue(PDFObject value);
    static PDFContentStateValue deviceColorValue(const char* deviceSpace, std::vector<PDFReal> components);
    static PDFContentStateValue colorValue(PDFObject colorSpace, std::vector<PDFReal> components, PDFObject pattern);
    static PDFContentStateValue softMaskValue(PDFObject softMask, const QTransform& matrix);

    static PDFContentResolvedStatePointer createState(const QTransform& matrix, bool overprintFilling = false);

    static PDFContentNode::Data nodeData(Kind kind, quint64 id, size_t first, size_t count);
    static PDFContentNodePointer createNode(PDFContentNode::Data data, std::vector<PDFContentNodePointer> children = {});
    static PDFContentNodePointer createLeaf(Kind kind, quint64 id, size_t first, size_t count);
    static PDFContentNodePointer createRoot(std::vector<PDFContentNodePointer> children);
    static PDFContentContainer createContainer(const PDFContentOperationList& operations, PDFContentNodePointer root, const PDFContentResources& resources = PDFContentResources());

    /// Writes the operations of the indices in the given order (each one a leaf of the root)
    static QByteArray writeInOrder(const PDFContentOperationList& operations, const std::vector<size_t>& order);

    /// The content "0 0 10 10 re f" with one path node, which has the override: the written bytes
    static PDFContentStreamWriteResult writePathWithOverride(const PDFContentStateOverride& override, const PDFContentResolvedStatePointer& state = nullptr, const PDFContentResources& resources = PDFContentResources());

    /// The container of writeWrap: an image inside of q ... Q, moved and made half transparent
    static PDFContentContainer createWrapContainer();
    static QByteArray wrapContainerBytes();

    static PDFContentBuildResult buildPage(const PDFDocument& document);
};

void ContentOperationsTest::skeletonPlainTypes()
{
    // A position is a stream of the content (an index from 0) and an offset in its decoded data;
    // the default one names no stream
    QVERIFY(!PDFContentStreamPosition().isValid());
    QVERIFY((PDFContentStreamPosition{ 0, 0 }).isValid());
    QVERIFY((PDFContentStreamPosition{ 3, 120 }).isValid());
    QVERIFY(!(PDFContentStreamPosition{ -1, 0 }).isValid());
    QVERIFY(!(PDFContentStreamPosition{ 0, -1 }).isValid());

    // The streams of /Contents follow each other (ISO 32000-2, 7.8.2): a position of a later stream
    // is behind every position of an earlier one, inside of a stream the offset decides
    QVERIFY((PDFContentStreamPosition{ 0, 500 }) < (PDFContentStreamPosition{ 1, 0 }));
    QVERIFY((PDFContentStreamPosition{ 1, 0 }) < (PDFContentStreamPosition{ 1, 1 }));
    QVERIFY((PDFContentStreamPosition{ 1, 1 }) > (PDFContentStreamPosition{ 0, 500 }));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) == (PDFContentStreamPosition{ 2, 7 }));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) != (PDFContentStreamPosition{ 2, 8 }));
    QVERIFY(!((PDFContentStreamPosition{ 2, 7 }) < (PDFContentStreamPosition{ 2, 7 })));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) <= (PDFContentStreamPosition{ 2, 7 }));

    // A range: from the end of the previous operation to the end of the operator. The operands
    // may begin in one stream and the operator may be in the next one (zadani_jadro.md 4.2), so a
    // range over the boundary of two streams is valid; an empty range too; a reversed one is not
    QVERIFY(!PDFContentByteRange().isValid());
    QVERIFY((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 90 }, { 1, 2 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 4 }, { 0, 4 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 1, 2 }, { 0, 90 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 0, 9 }, { 0, 3 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { -1, 0 }, { 0, 3 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 0, 3 }, { 0, -1 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }) == (PDFContentByteRange{ { 0, 3 }, { 0, 9 } }));
    QVERIFY(!((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }) == (PDFContentByteRange{ { 0, 3 }, { 1, 9 } })));

    // A default operation is nothing of a document and nothing of the editor: an unknown operator
    // without bytes, operands and flags - not modified (only Generated and Suppressed are)
    PDFContentOperation operation;
    QVERIFY(operation.getOperator() == PDFContentOperator::Invalid);
    QVERIFY(operation.getRawBytes().isEmpty());
    QVERIFY(!operation.getRange().isValid());
    QVERIFY(operation.getOperands().empty());
    QVERIFY(operation.getResourceNames().empty());
    QVERIFY(operation.getFlags() == PDFContentOperation::Flags(PDFContentOperation::None));
    QVERIFY(!operation.isModified());

    // The categories of the named resources of a content: the subdictionaries of a resource
    // dictionary (ISO 32000-2, 7.8.3, Table 34) without /ProcSet, which is an array of names and
    // names no resource of an operator - seven of them, in the order of zadani_jadro.md 4.2
    const std::vector<PDFContentResourceCategory> categories = { PDFContentResourceCategory::Font, PDFContentResourceCategory::XObject,
                                                                 PDFContentResourceCategory::ExtGState, PDFContentResourceCategory::ColorSpace,
                                                                 PDFContentResourceCategory::Pattern, PDFContentResourceCategory::Shading,
                                                                 PDFContentResourceCategory::Properties };
    QCOMPARE(categories.size(), size_t(7));
    for (size_t i = 0; i < categories.size(); ++i)
    {
        QCOMPARE(int(categories[i]), int(i));
    }

    // A resource name belongs to its category: the same name of two categories are two
    // resources; the names are ordered by the category first, by the bytes of the name then
    const PDFContentResourceName font{ PDFContentResourceCategory::Font, QByteArrayLiteral("R1") };
    const PDFContentResourceName xobject{ PDFContentResourceCategory::XObject, QByteArrayLiteral("R1") };
    QVERIFY(PDFContentResourceName().category == PDFContentResourceCategory::Font);
    QVERIFY(PDFContentResourceName().name.isEmpty());
    QVERIFY(font != xobject);
    QVERIFY(font == (PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("R1") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("Z") }) < (PDFContentResourceName{ PDFContentResourceCategory::XObject, QByteArrayLiteral("A") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("F1") }) < (PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("F2") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Properties, QByteArrayLiteral("MC1") }) > (PDFContentResourceName{ PDFContentResourceCategory::Shading, QByteArrayLiteral("Sh9") }));
}

// -------------------------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------------------------

ContentPiece ContentOperationsTest::piece(Op contentOperator, const char* rawBytes, std::vector<PDFContentResourceName> resourceNames)
{
    ContentPiece result;
    result.contentOperator = contentOperator;
    result.rawBytes = QByteArray(rawBytes);
    result.resourceNames = std::move(resourceNames);
    return result;
}

PDFContentOperationList ContentOperationsTest::createList(const std::vector<ContentPiece>& pieces, const QByteArray& tail)
{
    // One stream: the range of an operation are its bytes, the operations follow each other
    PDFContentOperationList list;
    PDFInteger offset = 0;
    for (const ContentPiece& contentPiece : pieces)
    {
        const PDFContentByteRange range{ { 0, offset }, { 0, offset + contentPiece.rawBytes.size() } };
        list.append(PDFContentOperation::createOriginal(contentPiece.contentOperator, contentPiece.rawBytes, range, PDFContentOperation::Operands(),
                                                        contentPiece.resourceNames, PDFContentOperation::None));
        offset += contentPiece.rawBytes.size();
    }
    list.finishOriginal(tail);
    return list;
}

bool ContentOperationsTest::hasExactly(const PDFObject& object, const ContentTestDocuments::DictionaryEntries& entries)
{
    if (!object.isDictionary() || object.getDictionary()->getCount() != entries.size())
    {
        return false;
    }

    for (const auto& entry : entries)
    {
        if (!object.getDictionary()->hasKey(entry.first) || object.getDictionary()->get(entry.first) != entry.second)
        {
            return false;
        }
    }

    return true;
}

QList<QByteArray> ContentOperationsTest::entryNames(const PDFContentResources& resources, Category category)
{
    QList<QByteArray> names;
    for (const PDFContentResources::Entry& entry : resources.getEntries(category))
    {
        names.push_back(entry.name);
    }
    return names;
}

PDFContentStateValue ContentOperationsTest::realValue(PDFReal value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Real;
    result.real = value;
    return result;
}

PDFContentStateValue ContentOperationsTest::integerValue(PDFInteger value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Integer;
    result.integer = value;
    return result;
}

PDFContentStateValue ContentOperationsTest::booleanValue(bool value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Boolean;
    result.boolean = value;
    return result;
}

PDFContentStateValue ContentOperationsTest::nameValue(const char* value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Name;
    result.name = QByteArray(value);
    return result;
}

PDFContentStateValue ContentOperationsTest::objectValue(PDFObject value)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Object;
    result.object = std::move(value);
    return result;
}

PDFContentStateValue ContentOperationsTest::deviceColorValue(const char* deviceSpace, std::vector<PDFReal> components)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Color;
    result.color.deviceSpace = QByteArray(deviceSpace);
    result.color.components = std::move(components);
    return result;
}

PDFContentStateValue ContentOperationsTest::colorValue(PDFObject colorSpace, std::vector<PDFReal> components, PDFObject pattern)
{
    PDFContentStateValue result;
    result.kind = PDFContentStateValue::Kind::Color;
    result.color.colorSpaceObject = std::move(colorSpace);
    result.color.components = std::move(components);
    result.color.patternObject = std::move(pattern);
    return result;
}

PDFContentStateValue ContentOperationsTest::softMaskValue(PDFObject softMask, const QTransform& matrix)
{
    PDFContentStateValue result = objectValue(std::move(softMask));
    result.matrix = matrix;
    return result;
}

PDFContentResolvedStatePointer ContentOperationsTest::createState(const QTransform& matrix, bool overprintFilling)
{
    PDFOverprintMode overprintMode;
    overprintMode.overprintFilling = overprintFilling;

    PDFPageContentProcessorState state;
    state.setCurrentTransformationMatrix(matrix);
    state.setOverprintMode(overprintMode);
    return std::make_shared<const PDFContentResolvedState>(state);
}

PDFContentNode::Data ContentOperationsTest::nodeData(Kind kind, quint64 id, size_t first, size_t count)
{
    PDFContentNode::Data data;
    data.kind = kind;
    data.id = PDFContentNodeId{ id };
    data.prefix = PDFContentOperationSpan{ first, count };
    return data;
}

PDFContentNodePointer ContentOperationsTest::createNode(PDFContentNode::Data data, std::vector<PDFContentNodePointer> children)
{
    return std::make_shared<const PDFContentNode>(std::move(data), std::move(children));
}

PDFContentNodePointer ContentOperationsTest::createLeaf(Kind kind, quint64 id, size_t first, size_t count)
{
    return createNode(nodeData(kind, id, first, count));
}

PDFContentNodePointer ContentOperationsTest::createRoot(std::vector<PDFContentNodePointer> children)
{
    return createNode(nodeData(Kind::Root, 1, 0, 0), std::move(children));
}

PDFContentContainer ContentOperationsTest::createContainer(const PDFContentOperationList& operations, PDFContentNodePointer root, const PDFContentResources& resources)
{
    PDFContentContainer::Data data;
    data.key = PDFContentContainerKey::createPage(0, PDFObjectReference(1, 0));
    data.operations = operations;
    data.root = std::move(root);
    data.resources = resources;
    return PDFContentContainer(std::move(data));
}

QByteArray ContentOperationsTest::writeInOrder(const PDFContentOperationList& operations, const std::vector<size_t>& order)
{
    std::vector<PDFContentNodePointer> leaves;
    quint64 id = 2;
    for (const size_t index : order)
    {
        leaves.push_back(createLeaf(Kind::Opaque, id++, index, 1));
    }

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(operations, createRoot(std::move(leaves))));
    return result.isSuccess() ? result.content : QByteArray("<error>");
}

PDFContentStreamWriteResult ContentOperationsTest::writePathWithOverride(const PDFContentStateOverride& override, const PDFContentResolvedStatePointer& state, const PDFContentResources& resources)
{
    const PDFContentOperationList list = createList({ piece(Op::Rectangle, "0 0 10 10 re"), piece(Op::PathFillWinding, " f") }, QByteArray());

    PDFContentNode::Data data = nodeData(Kind::Path, 2, 0, 2);
    data.override = override;
    data.state = state;

    return PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(std::move(data)) }), resources));
}

// -------------------------------------------------------------------------------------------
//  Operations and their list
// -------------------------------------------------------------------------------------------

void ContentOperationsTest::operationCreation()
{
    PDFContentOperation::Operands operands;
    operands.emplace_back(PDFLexicalAnalyzer::TokenType::Name, QVariant(QByteArray("Im1")));

    const PDFContentByteRange range{ { 0, 4 }, { 0, 12 } };
    const std::vector<PDFContentResourceName> names = { PDFContentResourceName{ Category::XObject, QByteArrayLiteral("Im1") } };
    const PDFContentOperation::Flags sourceFlags = PDFContentOperation::Flags(PDFContentOperation::Failed) | PDFContentOperation::InsideCompatibility | PDFContentOperation::Suppressed;

    // An operation read from a content keeps what it is given and is not modified by itself
    const PDFContentOperation original = PDFContentOperation::createOriginal(Op::PaintXObject, QByteArray(" /Im1 Do"), range, operands, names, PDFContentOperation::Failed);
    QVERIFY(original.getOperator() == Op::PaintXObject);
    QCOMPARE(original.getRawBytes(), QByteArray(" /Im1 Do"));
    QVERIFY(original.getRange() == range);
    QVERIFY(original.getOperands() == operands);
    QVERIFY(original.getResourceNames() == names);
    QVERIFY(original.getFlags() == PDFContentOperation::Flags(PDFContentOperation::Failed));
    QVERIFY(!original.isModified());

    // An operation of the editor: the flag Generated, no place in the content
    const PDFContentOperation generated = PDFContentOperation::createGenerated(Op::AdjustCurrentTransformationMatrix, QByteArray("1 0 0 1 10 20 cm"), PDFContentOperation::Operands(), {});
    QVERIFY(generated.getOperator() == Op::AdjustCurrentTransformationMatrix);
    QCOMPARE(generated.getRawBytes(), QByteArray("1 0 0 1 10 20 cm"));
    QVERIFY(!generated.getRange().isValid());
    QVERIFY(generated.getFlags() == PDFContentOperation::Flags(PDFContentOperation::Generated));
    QVERIFY(generated.isModified());

    // The same operation with other flags; a suppressed operation is modified
    const PDFContentOperation suppressed = original.withFlags(sourceFlags);
    QVERIFY(suppressed.getFlags() == sourceFlags);
    QCOMPARE(suppressed.getRawBytes(), original.getRawBytes());
    QVERIFY(suppressed.getRange() == range);
    QVERIFY(suppressed.getOperands() == operands);
    QVERIFY(suppressed.getResourceNames() == names);
    QVERIFY(suppressed.isModified());
    QVERIFY(original.getFlags() == PDFContentOperation::Flags(PDFContentOperation::Failed));

    // A copy for another place: the same operator, bytes, operands and names; no range, the flag
    // Generated set, Suppressed never, the other flags of the source kept
    const PDFContentOperation copy = PDFContentOperation::createCopy(suppressed);
    QVERIFY(copy.getOperator() == Op::PaintXObject);
    QCOMPARE(copy.getRawBytes(), QByteArray(" /Im1 Do"));
    QVERIFY(!copy.getRange().isValid());
    QVERIFY(copy.getOperands() == operands);
    QVERIFY(copy.getResourceNames() == names);
    QVERIFY(copy.getFlags() == (PDFContentOperation::Flags(PDFContentOperation::Failed) | PDFContentOperation::InsideCompatibility | PDFContentOperation::Generated));
    QVERIFY(copy.isModified());

    // An inline image and an unknown operator are copied as they are
    const PDFContentOperation image = PDFContentOperation::createOriginal(Op::InlineImage, QByteArray(" BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI"), range, {}, {}, PDFContentOperation::None);
    const PDFContentOperation imageCopy = PDFContentOperation::createCopy(image);
    QVERIFY(imageCopy.getOperator() == Op::InlineImage);
    QCOMPARE(imageCopy.getRawBytes(), image.getRawBytes());
    QVERIFY(imageCopy.getFlags() == PDFContentOperation::Flags(PDFContentOperation::Generated));

    // The source of a copy is not touched
    QVERIFY(suppressed.getFlags() == sourceFlags);
    QVERIFY(suppressed.getRange() == range);
}

void ContentOperationsTest::addContentRows()
{
    QTest::addColumn<QList<QByteArray>>("pieces");
    QTest::addColumn<QByteArray>("tail");
    QTest::addColumn<QByteArray>("content");

    // Each row: a content written by hand and the same content cut by hand at the end of every
    // operator (the range of an operation - pe0_skeleton_notes.md, decision 7); what follows the
    // last operator is the tail

    // White space of every kind (7.2.3) and comments, one of them with the text of an operator
    QTest::newRow("comments and white space")
            << QList<QByteArray>{ "% first comment\n q", "\t1 0 0 1 10 20 cm", " % a comment with an operator Q\r\n/Im1 Do", "\fQ" }
            << QByteArray(" \n")
            << QByteArray("% first comment\n q\t1 0 0 1 10 20 cm % a comment with an operator Q\r\n/Im1 Do\fQ \n");

    // Literal strings with escaped and balanced parentheses, hexadecimal strings with white
    // space and both cases of the digits, operators without white space before them
    QTest::newRow("literal and hexadecimal strings")
            << QList<QByteArray>{ "BT", "(a \\) unbalanced \\( and (balanced) \\\\)Tj", "<48 65 6C6c6F>Tj", "[(A)-120.5<42>]TJ", " ET" }
            << QByteArray()
            << QByteArray("BT(a \\) unbalanced \\( and (balanced) \\\\)Tj<48 65 6C6c6F>Tj[(A)-120.5<42>]TJ ET");

    // A name with #xx, numbers with a sign, without an integral part and without a fraction
    QTest::newRow("names and numbers")
            << QList<QByteArray>{ "/A#20B#23 gs", " +.5 1. -.0 00.50 re", " f*" }
            << QByteArray()
            << QByteArray("/A#20B#23 gs +.5 1. -.0 00.50 re f*");

    // An inline image is one operation, also when its data contain the bytes EI
    QTest::newRow("inline image with EI in the data")
            << QList<QByteArray>{ "q", " BI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI", " Q" }
            << QByteArray()
            << QByteArray("q BI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI Q");

    // An operation right after an inline image, without a white space between
    QTest::newRow("inline image followed by a delimiter")
            << QList<QByteArray>{ "BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI", "[]0 d" }
            << QByteArray()
            << QByteArray("BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI[]0 d");

    // A string, which does not end, cannot be an operand of anything: it is the tail
    QTest::newRow("tail, which cannot be read")
            << QList<QByteArray>{ "0 0 m", " 5 5 l" }
            << QByteArray(" S(a string, which does not end")
            << QByteArray("0 0 m 5 5 l S(a string, which does not end");

    QTest::newRow("only a comment")
            << QList<QByteArray>{ }
            << QByteArray("% nothing but a comment")
            << QByteArray("% nothing but a comment");

    QTest::newRow("empty content")
            << QList<QByteArray>{ }
            << QByteArray()
            << QByteArray();
}

void ContentOperationsTest::listOriginalContent_data()
{
    addContentRows();
}

void ContentOperationsTest::listOriginalContent()
{
    QFETCH(QList<QByteArray>, pieces);
    QFETCH(QByteArray, tail);
    QFETCH(QByteArray, content);

    // The row itself: the pieces and the tail are the content
    QCOMPARE(pieces.join() + tail, content);

    std::vector<ContentPiece> contentPieces;
    for (const QByteArray& rawBytes : pieces)
    {
        contentPieces.push_back(piece(Op::Invalid, rawBytes.constData()));
        contentPieces.back().rawBytes = rawBytes;
    }

    PDFContentOperationList list = createList(contentPieces, tail);

    QCOMPARE(list.getCount(), size_t(pieces.size()));
    QCOMPARE(list.getOriginalCount(), size_t(pieces.size()));
    QCOMPARE(list.isEmpty(), pieces.isEmpty());
    QCOMPARE(list.getTail(), tail);
    for (qsizetype i = 0; i < pieces.size(); ++i)
    {
        QCOMPARE(list.getOperation(size_t(i)).getRawBytes(), pieces[i]);
    }

    // zadani_jadro.md 4.2: the bytes of all operations and the rest after the last one give
    // the original content byte for byte
    QCOMPARE(list.getOriginalContent(), content);

    // An operation of the editor and a suppressed operation change nothing of the original content
    list.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray("q"), {}, {}));
    if (!pieces.isEmpty())
    {
        list.setFlags(0, PDFContentOperation::Suppressed);
        QVERIFY(list.getOperation(0).getFlags() == PDFContentOperation::Flags(PDFContentOperation::Suppressed));
    }
    QCOMPARE(list.getOriginalContent(), content);
    QCOMPARE(list.getOriginalCount(), size_t(pieces.size()));
    QCOMPARE(list.getCount(), size_t(pieces.size()) + 1);
}

void ContentOperationsTest::listOperationOverStreams()
{
    // Three streams of /Contents; the division is between two tokens (ISO 32000-2, 7.8.2), so
    // the operands of cm are in the first stream and its operator is in the second one
    const QByteArray stream0("q 1 0 0 1");
    const QByteArray stream1("5 5 cm /Im1 Do");
    const QByteArray stream2("Q");

    // 7.8.2: the streams are one content, as if they were joined with a white space between:
    // the editor joins them by one line feed (R-7)
    const QByteArray content("q 1 0 0 1\n5 5 cm /Im1 Do\nQ");
    QCOMPARE(stream0 + '\n' + stream1 + '\n' + stream2, content);

    // The operations by the rule of the ranges: from the end of the previous operation to the end
    // of the operator; the bytes of an operation over a boundary are joined by the line feed
    PDFContentOperationList list;
    list.append(PDFContentOperation::createOriginal(Op::SaveGraphicState, QByteArray("q"), PDFContentByteRange{ { 0, 0 }, { 0, 1 } }, {}, {}, PDFContentOperation::None));
    list.append(PDFContentOperation::createOriginal(Op::AdjustCurrentTransformationMatrix, QByteArray(" 1 0 0 1\n5 5 cm"), PDFContentByteRange{ { 0, 1 }, { 1, 6 } }, {}, {}, PDFContentOperation::None));
    list.append(PDFContentOperation::createOriginal(Op::PaintXObject, QByteArray(" /Im1 Do"), PDFContentByteRange{ { 1, 6 }, { 1, 14 } }, {}, {}, PDFContentOperation::None));
    list.append(PDFContentOperation::createOriginal(Op::RestoreGraphicState, QByteArray("\nQ"), PDFContentByteRange{ { 1, 14 }, { 2, 1 } }, {}, {}, PDFContentOperation::None));
    list.finishOriginal(QByteArray());

    QCOMPARE(list.getOriginalContent(), content);

    // The range of the operation over the boundary is kept as it was given
    const PDFContentByteRange& range = list.getOperation(1).getRange();
    QVERIFY(range.isValid());
    QCOMPARE(range.begin.stream, PDFInteger(0));
    QCOMPARE(range.begin.offset, PDFInteger(1));
    QCOMPARE(range.end.stream, PDFInteger(1));
    QCOMPARE(range.end.offset, PDFInteger(6));

    // The writer gives the same bytes for the unchanged tree q [cm, Do] Q
    PDFContentNode::Data group = nodeData(Kind::Group, 2, 0, 1);
    group.suffix = PDFContentOperationSpan{ 3, 1 };
    group.saveDepth = 1;
    const PDFContentNodePointer root = createRoot({ createNode(std::move(group), { createLeaf(Kind::StateRun, 3, 1, 1), createLeaf(Kind::Image, 4, 2, 1) }) });

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, root));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, content);
}

void ContentOperationsTest::listGeneratedAfterOriginal()
{
    PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::RestoreGraphicState, " Q") }, QByteArray("\n"));
    QCOMPARE(list.getCount(), size_t(2));
    QCOMPARE(list.getOriginalCount(), size_t(2));

    // The operations of the editor follow the original ones and get the next indices
    QCOMPARE(list.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray("q"), {}, {})), size_t(2));
    QCOMPARE(list.append(PDFContentOperation::createCopy(list.getOperation(1))), size_t(3));

    // An operation appended after the original content is at no place of the original content,
    // whatever its creator said: it is Generated (written with the joint rule, the container is changed)
    const PDFContentByteRange range{ { 0, 0 }, { 0, 1 } };
    QCOMPARE(list.append(PDFContentOperation::createOriginal(Op::PathFillWinding, QByteArray("f"), range, {}, {}, PDFContentOperation::Failed)), size_t(4));
    QVERIFY(list.getOperation(4).getFlags() == (PDFContentOperation::Flags(PDFContentOperation::Failed) | PDFContentOperation::Generated));
    QVERIFY(list.getOperation(4).isModified());

    QCOMPARE(list.getCount(), size_t(5));
    QCOMPARE(list.getOriginalCount(), size_t(2));
    QVERIFY(!list.getOperation(0).isModified());
    QVERIFY(!list.getOperation(1).isModified());
    QVERIFY(list.getOperation(2).isModified());
    QVERIFY(list.getOperation(3).isModified());
    QCOMPARE(list.getTail(), QByteArray("\n"));
    QCOMPARE(list.getOriginalContent(), QByteArray("q Q\n"));
}

void ContentOperationsTest::listCopiesShareOperations()
{
    // 2500 operations "0 w", "1 w", ...: more than two blocks of the storage
    const size_t count = 2500;
    PDFContentOperationList original;
    for (size_t i = 0; i < count; ++i)
    {
        const QByteArray rawBytes = (i == 0 ? QByteArray() : QByteArray(" ")) + QByteArray::number(qulonglong(i)) + " w";
        QCOMPARE(original.append(PDFContentOperation::createOriginal(Op::SetLineWidth, rawBytes, PDFContentByteRange(), {}, {}, PDFContentOperation::None)), i);
    }
    original.finishOriginal(QByteArray());

    // An index names its operation, also at the borders of the blocks
    for (const size_t index : { size_t(0), size_t(1), size_t(1023), size_t(1024), size_t(1025), size_t(2047), size_t(2048), size_t(2499) })
    {
        QCOMPARE(original.getOperation(index).getRawBytes(), (index == 0 ? QByteArray() : QByteArray(" ")) + QByteArray::number(qulonglong(index)) + " w");
    }

    // A copy shares the operations with the original: the very same objects
    PDFContentOperationList copy = original;
    QCOMPARE(copy.getCount(), count);
    QVERIFY(&copy.getOperation(10) == &original.getOperation(10));
    QVERIFY(&copy.getOperation(2400) == &original.getOperation(2400));

    // Appending to the copy: the original does not get the operation, and the operations before
    // stay shared with it (the header: "a copy, to which operations are appended, shares the
    // operations before with the original")
    QCOMPARE(copy.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray("q"), {}, {})), count);
    QCOMPARE(copy.getCount(), count + 1);
    QCOMPARE(original.getCount(), count);
    QCOMPARE(copy.getOperation(count).getRawBytes(), QByteArray("q"));
    QVERIFY(&copy.getOperation(10) == &original.getOperation(10));
    QVERIFY(&copy.getOperation(1500) == &original.getOperation(1500));
    QCOMPARE(copy.getOperation(2499).getRawBytes(), QByteArray(" 2499 w"));
    QCOMPARE(original.getOperation(2499).getRawBytes(), QByteArray(" 2499 w"));

    // New flags in the copy: the operation of the original keeps its flags, its neighbors in
    // the copy keep their bytes, the other operations stay shared
    copy.setFlags(1100, PDFContentOperation::Suppressed);
    QVERIFY(copy.getOperation(1100).getFlags() == PDFContentOperation::Flags(PDFContentOperation::Suppressed));
    QVERIFY(original.getOperation(1100).getFlags() == PDFContentOperation::Flags(PDFContentOperation::None));
    QCOMPARE(copy.getOperation(1100).getRawBytes(), QByteArray(" 1100 w"));
    QCOMPARE(copy.getOperation(1099).getRawBytes(), QByteArray(" 1099 w"));
    QCOMPARE(copy.getOperation(1101).getRawBytes(), QByteArray(" 1101 w"));
    QVERIFY(copy.getOperation(1101).getFlags() == PDFContentOperation::Flags(PDFContentOperation::None));
    QVERIFY(&copy.getOperation(10) == &original.getOperation(10));

    // The original appended to afterwards does not show in the copy (undo keeps old containers
    // valid, while a new one grows)
    QCOMPARE(original.append(PDFContentOperation::createGenerated(Op::RestoreGraphicState, QByteArray("Q"), {}, {})), count);
    QCOMPARE(original.getOperation(count).getRawBytes(), QByteArray("Q"));
    QCOMPARE(copy.getOperation(count).getRawBytes(), QByteArray("q"));
    QCOMPARE(original.getCount(), count + 1);
    QCOMPARE(copy.getCount(), count + 1);

    // A copy of a copy, changed, leaves both others alone
    PDFContentOperationList second = copy;
    second.setFlags(count, PDFContentOperation::Flags(PDFContentOperation::Generated) | PDFContentOperation::Suppressed);
    QVERIFY(copy.getOperation(count).getFlags() == PDFContentOperation::Flags(PDFContentOperation::Generated));
    QVERIFY(second.getOperation(count).getFlags() == (PDFContentOperation::Flags(PDFContentOperation::Generated) | PDFContentOperation::Suppressed));
}

void ContentOperationsTest::listPrefix()
{
    // The first operations of a list (PDFContentOperationList::getPrefix): the version of a
    // container before a change, made again by an undo, owns the operations it had - not the
    // ones the change appended behind them. 2000 original operations "0 w", "1 w", ... and 1000
    // generated ones "q": the generated ones begin inside of the second block of 1024.
    const size_t originalCount = 2000;
    const size_t count = 3000;
    PDFContentOperationList list;
    for (size_t i = 0; i < originalCount; ++i)
    {
        const QByteArray rawBytes = (i == 0 ? QByteArray() : QByteArray(" ")) + QByteArray::number(qulonglong(i)) + " w";
        list.append(PDFContentOperation::createOriginal(Op::SetLineWidth, rawBytes, PDFContentByteRange(), {}, {}, PDFContentOperation::None));
    }
    list.finishOriginal(QByteArray(" % end"));
    for (size_t i = originalCount; i < count; ++i)
    {
        list.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray("q"), {}, {}));
    }
    QCOMPARE(list.getCount(), count);

    // Cuts inside of a block, at its border and of the whole list; a cut keeps the original
    // operations, the tail and the original content, and shares the operations with the list
    for (const size_t prefixCount : { originalCount, size_t(2047), size_t(2048), size_t(2049), size_t(2500), count })
    {
        const PDFContentOperationList prefix = list.getPrefix(prefixCount);
        QCOMPARE(prefix.getCount(), prefixCount);
        QCOMPARE(prefix.getOriginalCount(), originalCount);
        QCOMPARE(prefix.getTail(), QByteArray(" % end"));
        QCOMPARE(prefix.getOriginalContent(), list.getOriginalContent());
        QCOMPARE(prefix.getOperation(0).getRawBytes(), QByteArray("0 w"));
        QCOMPARE(prefix.getOperation(1999).getRawBytes(), QByteArray(" 1999 w"));
        QCOMPARE(prefix.getOperation(prefixCount - 1).getRawBytes(), prefixCount > originalCount ? QByteArray("q") : QByteArray(" 1999 w"));
        // A full block is shared (the very same objects); the operations of a partly used last
        // block (1024 ... 2047 for a cut below 2048) are a copy of their own
        QVERIFY(&prefix.getOperation(10) == &list.getOperation(10));
        QCOMPARE(&prefix.getOperation(1500) == &list.getOperation(1500), prefixCount >= 2048);
    }

    // Appending to a cut: the next index is the count of the cut (not the count of the list),
    // and the list keeps its own operation at that index; again inside of a block and at
    // a border of blocks
    for (const size_t prefixCount : { size_t(2500), size_t(2048) })
    {
        PDFContentOperationList prefix = list.getPrefix(prefixCount);
        QCOMPARE(prefix.append(PDFContentOperation::createGenerated(Op::RestoreGraphicState, QByteArray("Q"), {}, {})), prefixCount);
        QCOMPARE(prefix.append(PDFContentOperation::createGenerated(Op::RestoreGraphicState, QByteArray("Q"), {}, {})), prefixCount + 1);
        QCOMPARE(prefix.getCount(), prefixCount + 2);
        QCOMPARE(prefix.getOperation(prefixCount).getRawBytes(), QByteArray("Q"));
        QCOMPARE(prefix.getOperation(prefixCount - 1).getRawBytes(), QByteArray("q"));
        QCOMPARE(list.getOperation(prefixCount).getRawBytes(), QByteArray("q"));
        QCOMPARE(list.getCount(), count);

        // New flags in the cut do not reach the list
        prefix.setFlags(prefixCount - 1, PDFContentOperation::Flags(PDFContentOperation::Generated) | PDFContentOperation::Suppressed);
        QVERIFY(list.getOperation(prefixCount - 1).getFlags() == PDFContentOperation::Flags(PDFContentOperation::Generated));
    }

    // A cut of a cut, which nobody else holds (its last block is its own), appended to: the
    // operations behind the cut are gone, the new one takes the first free index
    {
        PDFContentOperationList prefix = list.getPrefix(2600);
        prefix = prefix.getPrefix(2100);
        QCOMPARE(prefix.append(PDFContentOperation::createGenerated(Op::RestoreGraphicState, QByteArray("Q"), {}, {})), size_t(2100));
        QCOMPARE(prefix.getCount(), size_t(2101));
        QCOMPARE(prefix.getOperation(2100).getRawBytes(), QByteArray("Q"));
        QCOMPARE(prefix.getOperation(2099).getRawBytes(), QByteArray("q"));
    }

    // A count above the list gives the whole list
    QCOMPARE(list.getPrefix(count + 10).getCount(), count);
}

void ContentOperationsTest::listCopiesInThreads()
{
    // A list inside of a container is read by any thread, while other threads change their own
    // copies of it (a command in the thread of the session, a writer in a worker)
    const size_t count = 3000;
    PDFContentOperationList original;
    for (size_t i = 0; i < count; ++i)
    {
        original.append(PDFContentOperation::createOriginal(Op::SetLineWidth, QByteArray::number(qulonglong(i)) + " w ", PDFContentByteRange(), {}, {}, PDFContentOperation::None));
    }
    original.finishOriginal(QByteArray());

    const int threadCount = 8;
    std::atomic<int> failures{ 0 };
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([&original, &failures, threadIndex, count]()
        {
            for (int round = 0; round < 200; ++round)
            {
                const size_t changedIndex = size_t(threadIndex) * 300 + size_t(round);

                PDFContentOperationList copy = original;
                copy.setFlags(changedIndex, PDFContentOperation::Suppressed);
                const size_t appendedIndex = copy.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray::number(threadIndex), {}, {}));

                const bool isCopyRight = appendedIndex == count &&
                                         copy.getCount() == count + 1 &&
                                         copy.getOperation(count).getRawBytes() == QByteArray::number(threadIndex) &&
                                         copy.getOperation(changedIndex).getFlags() == PDFContentOperation::Flags(PDFContentOperation::Suppressed) &&
                                         copy.getOperation(changedIndex).getRawBytes() == QByteArray::number(qulonglong(changedIndex)) + " w ";

                // The shared original, read at the same time
                const bool isOriginalRight = original.getCount() == count &&
                                             original.getOperation(changedIndex).getFlags() == PDFContentOperation::Flags(PDFContentOperation::None) &&
                                             original.getOperation(count - 1).getRawBytes() == QByteArray::number(qulonglong(count - 1)) + " w ";

                if (!isCopyRight || !isOriginalRight)
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
    QCOMPARE(original.getCount(), count);
    for (size_t i = 0; i < count; ++i)
    {
        QVERIFY(original.getOperation(i).getFlags() == PDFContentOperation::Flags(PDFContentOperation::None));
    }
}

// -------------------------------------------------------------------------------------------
//  Resources (R-4)
// -------------------------------------------------------------------------------------------

void ContentOperationsTest::resourcesRead()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));

    const PDFObject font1 = ContentTestDocuments::addObject(&builder, dictionary({ { "Type", name("Font") }, { "BaseFont", name("Helvetica") } }));
    const PDFObject font2 = ContentTestDocuments::addObject(&builder, dictionary({ { "Type", name("Font") }, { "BaseFont", name("Courier") } }));
    const PDFObject image = ContentTestDocuments::addObject(&builder, dictionary({ { "Subtype", name("Image") } }));
    const PDFObject colorSpace = ContentTestDocuments::addObject(&builder, ContentTestDocuments::createArrayObject({ name("CalGray"), dictionary({ { "WhitePoint", ContentTestDocuments::createNumberArrayObject({ 0.9505, 1.0, 1.089 }) } }) }));

    // The fonts are an indirect dictionary (as a dictionary shared by more pages is)
    const PDFObject fonts = ContentTestDocuments::addObject(&builder, dictionary({ { "F1", font1 }, { "F2", font2 } }));
    const PDFObject defaultRGB = ContentTestDocuments::createArrayObject({ name("CalRGB"), dictionary({ { "WhitePoint", ContentTestDocuments::createNumberArrayObject({ 0.9505, 1.0, 1.089 }) } }) });
    const PDFObject graphicState = dictionary({ { "Type", name("ExtGState") }, { "LW", PDFObject::createInteger(2) } });

    const PDFObject resources = dictionary({ { "ProcSet", ContentTestDocuments::createArrayObject({ name("PDF"), name("Text") }) },
                                             { "Font", fonts },
                                             { "XObject", dictionary({ { "Im1", image } }) },
                                             { "ColorSpace", dictionary({ { "DefaultRGB", defaultRGB }, { "Cs1", colorSpace } }) },
                                             { "ExtGState", dictionary({ { "GS1", graphicState } }) },
                                             { "Foo", PDFObject::createInteger(42) } });
    const PDFObject resourcesReference = ContentTestDocuments::addObject(&builder, resources);
    const PDFDocument document = builder.build();
    QVERIFY(document.getObject(fonts).isDictionary());

    // The operations name a defined font, an image, which is not defined, a color space by an
    // inline image (/CS /Cs1), an ExtGState, which is not defined, and a shading of a category
    // the dictionary does not have at all
    const PDFContentOperationList operations = createList({ piece(Op::TextSetFontAndFontSize, "/F1 12 Tf", { { Category::Font, "F1" } }),
                                                            piece(Op::PaintXObject, " /Im9 Do", { { Category::XObject, "Im9" } }),
                                                            piece(Op::InlineImage, " BI /W 1 /H 1 /BPC 8 /CS /Cs1 ID a\nEI", { { Category::ColorSpace, "Cs1" } }),
                                                            piece(Op::SetGraphicState, " /GS7 gs", { { Category::ExtGState, "GS7" } }),
                                                            piece(Op::ShadingPaintShape, " /Sh1 sh", { { Category::Shading, "Sh1" } }),
                                                            piece(Op::PaintXObject, " /Im9 Do", { { Category::XObject, "Im9" } }) }, QByteArray());

    for (const PDFObject& resourcesObject : { resources, resourcesReference })
    {
        const PDFContentResources contentResources = PDFContentResources::read(&document, resourcesObject, operations);

        // The entries in the order of their dictionary, all of them existed at load
        QCOMPARE(entryNames(contentResources, Category::Font), (QList<QByteArray>{ "F1", "F2" }));
        QCOMPARE(entryNames(contentResources, Category::XObject), (QList<QByteArray>{ "Im1" }));
        QCOMPARE(entryNames(contentResources, Category::ColorSpace), (QList<QByteArray>{ "DefaultRGB", "Cs1" }));
        QCOMPARE(entryNames(contentResources, Category::ExtGState), (QList<QByteArray>{ "GS1" }));
        QVERIFY(contentResources.getEntries(Category::Pattern).empty());
        QVERIFY(contentResources.getEntries(Category::Shading).empty());
        QVERIFY(contentResources.getEntries(Category::Properties).empty());
        for (const Category category : PDFContentResources::getCategories())
        {
            for (const PDFContentResources::Entry& entry : contentResources.getEntries(category))
            {
                QVERIFY(entry.isOriginal);
            }
        }

        // Name -> object: the value of the dictionary as it is (a reference stays a reference)
        QVERIFY(contentResources.getObject(Category::Font, "F1") == font1);
        QVERIFY(contentResources.getObject(Category::Font, "F2") == font2);
        QVERIFY(contentResources.getObject(Category::XObject, "Im1") == image);
        QVERIFY(contentResources.getObject(Category::ColorSpace, "DefaultRGB") == defaultRGB);
        QVERIFY(contentResources.getObject(Category::ExtGState, "GS1") == graphicState);
        QVERIFY(contentResources.getObject(Category::Font, "F9").isNull());
        QVERIFY(contentResources.getObject(Category::XObject, "F1").isNull());

        // Object -> name, inside of its category only; a direct object by its value
        QCOMPARE(contentResources.getName(Category::Font, font2), QByteArray("F2"));
        QCOMPARE(contentResources.getName(Category::XObject, font2), QByteArray());
        QCOMPARE(contentResources.getName(Category::ColorSpace, colorSpace), QByteArray("Cs1"));
        QCOMPARE(contentResources.getName(Category::ExtGState, dictionary({ { "Type", name("ExtGState") }, { "LW", PDFObject::createInteger(2) } })), QByteArray("GS1"));
        QCOMPARE(contentResources.getName(Category::ExtGState, dictionary({ { "Type", name("ExtGState") }, { "LW", PDFObject::createInteger(3) } })), QByteArray());
        QCOMPARE(contentResources.getName(Category::Font, PDFObject()), QByteArray());

        // The names used without a definition, each once
        QVERIFY(contentResources.getUndefinedNames(Category::Font).empty());
        QVERIFY(contentResources.getUndefinedNames(Category::ColorSpace).empty());
        QVERIFY(contentResources.getUndefinedNames(Category::XObject) == std::set<QByteArray>{ "Im9" });
        QVERIFY(contentResources.getUndefinedNames(Category::ExtGState) == std::set<QByteArray>{ "GS7" });
        QVERIFY(contentResources.getUndefinedNames(Category::Shading) == std::set<QByteArray>{ "Sh1" });

        QVERIFY(!contentResources.isChanged());
    }

    // The keys of the categories (ISO 32000-2, 7.8.3, Table 34), in the order of the enum
    const std::vector<Category> categories = PDFContentResources::getCategories();
    const QList<QByteArray> expectedKeys = { "Font", "XObject", "ExtGState", "ColorSpace", "Pattern", "Shading", "Properties" };
    QCOMPARE(categories.size(), size_t(7));
    for (size_t i = 0; i < categories.size(); ++i)
    {
        QCOMPARE(int(categories[i]), int(i));
        QCOMPARE(PDFContentResources::getCategoryKey(categories[i]), expectedKeys[qsizetype(i)]);
    }
}

void ContentOperationsTest::resourcesReservedNames()
{
    // PDFContentResources::getReservedNames: names of a category, which content without resources
    // drawn by this content uses (the tree builder sets them; A1L2-4). allocateName avoids them
    // in their category - also in a category, which has no dictionary - and they are no
    // undefined names.
    const PDFObject resourcesObject = dictionary({ { "ExtGState", dictionary({ { "GS1", dictionary({ { "LW", PDFObject::createInteger(1) } }) } }) } });
    PDFContentResources resources = PDFContentResources::read(nullptr, resourcesObject, createList({ }, QByteArray()));
    QVERIFY(resources.getReservedNames(Category::ExtGState).empty());
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS2"));
    QCOMPARE(resources.allocateName(Category::XObject, "Fm"), QByteArray("Fm1"));

    resources.setReservedNames(Category::ExtGState, { "GS2", "GS3" });
    resources.setReservedNames(Category::XObject, { "Fm1", "GS4" });
    QCOMPARE(resources.getReservedNames(Category::ExtGState), (std::set<QByteArray>{ "GS2", "GS3" }));
    QVERIFY(resources.getUndefinedNames(Category::ExtGState).empty());
    QVERIFY(!resources.isChanged());

    // Each category its own names: /GS4 is reserved for XObject only
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS4"));
    QCOMPARE(resources.allocateName(Category::XObject, "Fm"), QByteArray("Fm2"));
    QCOMPARE(resources.allocateName(Category::XObject, "GS"), QByteArray("GS1"));
    QCOMPARE(resources.allocateName(Category::Font, "GS"), QByteArray("GS1"));

    // addObject takes the same name; a copy (the resources of an editor) keeps the reserved names
    PDFContentResources copy = resources;
    QCOMPARE(copy.addObject(Category::ExtGState, "GS", dictionary({ { "CA", PDFObject::createReal(0.5) } })), QByteArray("GS4"));
    QCOMPARE(copy.addObject(Category::XObject, "Fm", reference(9)), QByteArray("Fm2"));
    QCOMPARE(copy.allocateName(Category::ExtGState, "GS"), QByteArray("GS5"));
    QCOMPARE(copy.getReservedNames(Category::XObject), resources.getReservedNames(Category::XObject));

    // No names: nothing is reserved
    resources.setReservedNames(Category::ExtGState, { });
    QVERIFY(resources.getReservedNames(Category::ExtGState).empty());
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS2"));
}

void ContentOperationsTest::resourcesAllocateName()
{
    const PDFObject state1 = dictionary({ { "LW", PDFObject::createInteger(1) } });
    const PDFObject state2 = dictionary({ { "LW", PDFObject::createInteger(2) } });
    const PDFObject resourcesObject = dictionary({ { "ExtGState", dictionary({ { "GS1", state1 }, { "GS2", state2 } }) },
                                                   { "Font", dictionary({ { "F1", reference(7) } }) } });

    // /GS3 is used by an operation (it may be an opaque one), but nothing defines it
    const PDFContentOperationList operations = createList({ piece(Op::SetGraphicState, "/GS3 gs", { { Category::ExtGState, "GS3" } }) }, QByteArray());
    PDFContentResources resources = PDFContentResources::read(nullptr, resourcesObject, operations);

    // R-4: the prefix and the smallest positive number, which gives a name neither defined (GS1,
    // GS2) nor used without a definition (GS3)
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS4"));
    QCOMPARE(resources.allocateName(Category::ExtGState, "X"), QByteArray("X1"));

    // Every category has its own dictionary (7.8.3): the names of another one do not collide
    QCOMPARE(resources.allocateName(Category::Font, "GS"), QByteArray("GS1"));
    QCOMPARE(resources.allocateName(Category::Font, "F"), QByteArray("F2"));
    QCOMPARE(resources.allocateName(Category::XObject, "F"), QByteArray("F1"));

    // allocateName reserves nothing
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS4"));
    QVERIFY(!resources.isChanged());

    const PDFObject stateA = dictionary({ { "CA", PDFObject::createReal(0.5) } });
    const PDFObject stateB = dictionary({ { "CA", PDFObject::createReal(0.25) } });
    const PDFObject stateC = dictionary({ { "CA", PDFObject::createReal(0.75) } });

    QCOMPARE(resources.addObject(Category::ExtGState, "GS", stateA), QByteArray("GS4"));
    QVERIFY(resources.isChanged());
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS5"));
    QVERIFY(resources.getObject(Category::ExtGState, "GS4") == stateA);
    QCOMPARE(resources.getName(Category::ExtGState, stateA), QByteArray("GS4"));

    // The same object is not added twice
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", stateA), QByteArray("GS4"));
    QCOMPARE(entryNames(resources, Category::ExtGState), (QList<QByteArray>{ "GS1", "GS2", "GS4" }));

    // An object, which is a resource since the load, keeps its name; nothing is added
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", dictionary({ { "LW", PDFObject::createInteger(2) } })), QByteArray("GS2"));
    QCOMPARE(resources.addObject(Category::Font, "F", reference(7)), QByteArray("F1"));
    QCOMPARE(entryNames(resources, Category::Font), (QList<QByteArray>{ "F1" }));

    QCOMPARE(resources.addObject(Category::ExtGState, "GS", stateB), QByteArray("GS5"));

    // A removed name is free again: the smallest number is 4 once more
    QVERIFY(resources.removeAdded(Category::ExtGState, "GS4"));
    QVERIFY(resources.getObject(Category::ExtGState, "GS4").isNull());
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS4"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", stateC), QByteArray("GS4"));

    // The added entries follow the original ones in the order of their adding
    QCOMPARE(entryNames(resources, Category::ExtGState), (QList<QByteArray>{ "GS1", "GS2", "GS5", "GS4" }));
    QVERIFY(resources.getObject(Category::ExtGState, "GS5") == stateB);
    QVERIFY(resources.getObject(Category::ExtGState, "GS4") == stateC);
    QVERIFY(resources.getEntries(Category::ExtGState)[1].isOriginal);
    QVERIFY(!resources.getEntries(Category::ExtGState)[2].isOriginal);
    QVERIFY(!resources.getEntries(Category::ExtGState)[3].isOriginal);

    // A null object defines nothing (7.3.7): it is not added
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", PDFObject()), QByteArray());
    QCOMPARE(resources.getEntries(Category::ExtGState).size(), size_t(4));

    // The undefined names are what the operations said, whatever was added
    QVERIFY(resources.getUndefinedNames(Category::ExtGState) == std::set<QByteArray>{ "GS3" });
}

void ContentOperationsTest::resourcesOriginalEntriesStay()
{
    // RE-6: an entry, which existed at load, is never removed and never changed: DefaultRGB is
    // named by no operator, /Cs1 is named by an inline image, /F1 may be used by a form, which
    // inherits the resources
    const PDFObject defaultRGB = ContentTestDocuments::createArrayObject({ name("CalRGB"), dictionary({ }) });
    const PDFObject resourcesObject = dictionary({ { "ColorSpace", dictionary({ { "DefaultRGB", defaultRGB }, { "Cs1", reference(8) } }) },
                                                   { "Font", dictionary({ { "F1", reference(7) } }) } });
    const PDFContentOperationList operations = createList({ piece(Op::PaintXObject, "/Im9 Do", { { Category::XObject, "Im9" } }) }, QByteArray());
    PDFContentResources resources = PDFContentResources::read(nullptr, resourcesObject, operations);

    QVERIFY(!resources.removeAdded(Category::ColorSpace, "DefaultRGB"));
    QVERIFY(!resources.removeAdded(Category::ColorSpace, "Cs1"));
    QVERIFY(!resources.removeAdded(Category::Font, "F1"));
    QVERIFY(!resources.setObject(Category::Font, "F1", reference(99)));
    QVERIFY(!resources.setObject(Category::ColorSpace, "DefaultRGB", name("DeviceRGB")));

    // A name, which is not defined (also one used by an operation), is no entry
    QVERIFY(!resources.removeAdded(Category::XObject, "Im9"));
    QVERIFY(!resources.removeAdded(Category::Font, "F2"));
    QVERIFY(!resources.setObject(Category::XObject, "Im9", reference(5)));
    QVERIFY(!resources.setObject(Category::Pattern, "P1", reference(5)));

    QCOMPARE(entryNames(resources, Category::ColorSpace), (QList<QByteArray>{ "DefaultRGB", "Cs1" }));
    QVERIFY(resources.getObject(Category::ColorSpace, "DefaultRGB") == defaultRGB);
    QVERIFY(resources.getObject(Category::Font, "F1") == reference(7));
    QVERIFY(!resources.isChanged());

    // An added entry: its object can be replaced (the provisional reference of a new object by
    // the real one), its name stays; it can be removed
    const PDFObject provisional = reference(1000);
    QCOMPARE(resources.addObject(Category::Pattern, "P", provisional), QByteArray("P1"));
    QVERIFY(resources.isChanged());
    QVERIFY(resources.setObject(Category::Pattern, "P1", reference(31)));
    QVERIFY(resources.getObject(Category::Pattern, "P1") == reference(31));
    QCOMPARE(resources.getName(Category::Pattern, reference(31)), QByteArray("P1"));
    QCOMPARE(resources.getName(Category::Pattern, provisional), QByteArray());
    QVERIFY(!resources.setObject(Category::Pattern, "P1", PDFObject()));
    QVERIFY(resources.getObject(Category::Pattern, "P1") == reference(31));

    QVERIFY(resources.removeAdded(Category::Pattern, "P1"));
    QVERIFY(!resources.removeAdded(Category::Pattern, "P1"));
    QVERIFY(resources.getEntries(Category::Pattern).empty());

    // Nothing differs from the load anymore
    QVERIFY(!resources.isChanged());
}

void ContentOperationsTest::resourcesWrite()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));

    // Two category dictionaries are indirect objects, as dictionaries shared by pages are
    const PDFObject sharedFonts = ContentTestDocuments::addObject(&builder, dictionary({ { "F1", reference(101) } }));
    const PDFObject sharedPatterns = ContentTestDocuments::addObject(&builder, dictionary({ { "P1", reference(102) } }));
    const PDFObject procSet = ContentTestDocuments::createArrayObject({ name("PDF") });
    const PDFObject colorSpaces = dictionary({ { "DefaultRGB", ContentTestDocuments::createArrayObject({ name("CalRGB"), dictionary({ }) }) } });

    const PDFObject resourcesObject = dictionary({ { "ProcSet", procSet },
                                                   { "Font", sharedFonts },
                                                   { "Pattern", sharedPatterns },
                                                   { "ColorSpace", colorSpaces },
                                                   { "XObject", PDFObject::createInteger(17) },
                                                   { "Foo", PDFObject::createString(QByteArray("bar")) } });
    const PDFDocument document = builder.build();

    PDFContentResources resources = PDFContentResources::read(&document, resourcesObject, PDFContentOperationList());

    // Nothing added: the dictionary as it was, the references of the categories included
    PDFDocumentBuilder writeBuilder(&document);
    QVERIFY(resources.write(&writeBuilder) == resourcesObject);

    const PDFObject graphicState = dictionary({ { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });
    QCOMPARE(resources.addObject(Category::Font, "F", reference(103)), QByteArray("F2"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", graphicState), QByteArray("GS1"));
    QCOMPARE(resources.addObject(Category::XObject, "Im", reference(104)), QByteArray("Im1"));

    const PDFObject written = resources.write(&writeBuilder);

    // R-4, key by key. Every other key of the original dictionary is kept; a category without
    // a change keeps its original value: the pattern dictionary stays the same reference
    QVERIFY(written.isDictionary());
    const PDFDictionary* writtenDictionary = written.getDictionary();
    QCOMPARE(writtenDictionary->getCount(), size_t(7));
    QVERIFY(writtenDictionary->get("ProcSet") == procSet);
    QVERIFY(writtenDictionary->get("Foo") == PDFObject::createString(QByteArray("bar")));
    QVERIFY(writtenDictionary->get("Pattern") == sharedPatterns);
    QVERIFY(writtenDictionary->get("Pattern").isReference());
    QVERIFY(writtenDictionary->get("ColorSpace") == colorSpaces);

    // A category with an added entry is a new dictionary of this container: a direct copy of
    // the shared one with its entries, and the new entry
    QVERIFY(hasExactly(writtenDictionary->get("Font"), { { "F1", reference(101) }, { "F2", reference(103) } }));

    // The shared dictionary of the document is not changed in place: neither in the document,
    // nor in the builder
    QVERIFY(hasExactly(document.getObject(sharedFonts), { { "F1", reference(101) } }));
    QVERIFY(hasExactly(writeBuilder.getObject(sharedFonts), { { "F1", reference(101) } }));

    // A category the dictionary did not have; a category, whose value was no dictionary
    QVERIFY(hasExactly(writtenDictionary->get("ExtGState"), { { "GS1", graphicState } }));
    QVERIFY(hasExactly(writtenDictionary->get("XObject"), { { "Im1", reference(104) } }));

    // A content without resources: only what was added
    PDFContentResources emptyResources = PDFContentResources::read(&document, PDFObject(), PDFContentOperationList());
    QVERIFY(hasExactly(emptyResources.write(&writeBuilder), { }));
    QCOMPARE(emptyResources.addObject(Category::ExtGState, "GS", graphicState), QByteArray("GS1"));
    QVERIFY(hasExactly(emptyResources.write(&writeBuilder), { { "ExtGState", dictionary({ { "GS1", graphicState } }) } }));

    // The resources read before are a value of their own: they did not get the entry
    QVERIFY(hasExactly(resources.write(&writeBuilder).getDictionary()->get("ExtGState"), { { "GS1", graphicState } }));
    QVERIFY(PDFContentResources::read(&document, resourcesObject, PDFContentOperationList()).write(&writeBuilder) == resourcesObject);
}

void ContentOperationsTest::resourcesOfFormWithoutResources()
{
    // The form /Fm2 of the test document has no /Resources: its content "/GP gs 1 0 0 rg
    // 0 0 10 10 re f" uses the ExtGState /GP of the page (ISO 32000-2, 7.8.3). R-4: when such
    // a form is changed, it gets its own copy of the inherited dictionary - with /GP in it.
    const PDFDocument document = ContentTestDocuments::createFormDocument();
    const PDFPage* page = document.getCatalog()->getPage(0);
    QVERIFY(page);

    QCOMPARE(ContentTestDocuments::formWithoutResourcesContent(), QByteArray("/GP gs 1 0 0 rg 0 0 10 10 re f"));
    const PDFContentOperationList operations = createList({ piece(Op::SetGraphicState, "/GP gs", { { Category::ExtGState, "GP" } }),
                                                            piece(Op::ColorSetDeviceRGBFilling, " 1 0 0 rg"),
                                                            piece(Op::Rectangle, " 0 0 10 10 re"),
                                                            piece(Op::PathFillWinding, " f") }, QByteArray());

    PDFContentResources resources = PDFContentResources::read(&document, page->getResources(), operations);
    QCOMPARE(entryNames(resources, Category::ExtGState), (QList<QByteArray>{ "GP" }));
    QCOMPARE(entryNames(resources, Category::XObject), (QList<QByteArray>{ "Fm1", "Fm2" }));
    QVERIFY(resources.getUndefinedNames(Category::ExtGState).empty());

    const PDFObject graphicState = dictionary({ { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", graphicState), QByteArray("GS1"));

    const PDFObject written = resources.write(nullptr);
    QVERIFY(written.isDictionary());

    // /GP is still defined, by the same value as on the page; the new entry is next to it
    const PDFObject pageGraphicState = dictionary({ { "Type", name("ExtGState") }, { "LW", PDFObject::createInteger(6) } });
    QVERIFY(hasExactly(written.getDictionary()->get("ExtGState"), { { "GP", pageGraphicState }, { "GS1", graphicState } }));

    // The forms of the page stay too (the form may draw another form by a name of the page)
    const PDFObject& pageResources = document.getObject(page->getResources());
    QVERIFY(pageResources.isDictionary());
    QVERIFY(written.getDictionary()->get("XObject") == pageResources.getDictionary()->get("XObject"));
    QCOMPARE(written.getDictionary()->getCount(), size_t(2));
}

void ContentOperationsTest::resourcesHostileInput()
{
    const PDFObject graphicState = dictionary({ { "ca", PDFObject::createReal(0.5) } });
    const PDFContentOperationList operations = createList({ piece(Op::TextSetFontAndFontSize, "/F1 12 Tf", { { Category::Font, "F1" } }) }, QByteArray());

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    const PDFObject integerObject = ContentTestDocuments::addObject(&builder, PDFObject::createInteger(5));

    // A category shared by two keys (a node shared by two paths)
    const PDFObject sharedCategory = ContentTestDocuments::addObject(&builder, dictionary({ { "A", reference(201) } }));
    const PDFObject sharedResources = dictionary({ { "Font", sharedCategory }, { "XObject", sharedCategory } });

    // A cycle: the font dictionary of the resources refers to the resources themselves
    const PDFObjectReference cycleResourcesReference = builder.addObject(PDFObject());
    const PDFObject cycleFonts = ContentTestDocuments::addObject(&builder, dictionary({ { "F1", PDFObject::createReference(cycleResourcesReference) } }));
    builder.setObject(cycleResourcesReference, dictionary({ { "Font", cycleFonts }, { "Resources", PDFObject::createReference(cycleResourcesReference) } }));

    const PDFDocument document = builder.build();

    // /Resources of a wrong type, a reference to nothing, a reference to a wrong type: no
    // entries, the name of the operation is not defined, a new name can still be added
    for (const PDFObject& resourcesObject : { PDFObject::createInteger(5), reference(99999), integerObject, PDFObject::createString(QByteArray("x")) })
    {
        PDFContentResources resources = PDFContentResources::read(&document, resourcesObject, operations);
        for (const Category category : PDFContentResources::getCategories())
        {
            QVERIFY(resources.getEntries(category).empty());
        }
        QVERIFY(resources.getUndefinedNames(Category::Font) == std::set<QByteArray>{ "F1" });
        QCOMPARE(resources.allocateName(Category::Font, "F"), QByteArray("F2"));
        QCOMPARE(resources.addObject(Category::ExtGState, "GS", graphicState), QByteArray("GS1"));
        QVERIFY(hasExactly(resources.write(nullptr), { { "ExtGState", dictionary({ { "GS1", graphicState } }) } }));
    }

    // A category of a wrong type defines nothing; a category with a null entry: the name is
    // not defined (7.3.7), but it is never given to a new object (the copy of the dictionary
    // would have the key twice)
    {
        const PDFObject resourcesObject = dictionary({ { "XObject", ContentTestDocuments::createArrayObject({ reference(5) }) },
                                                       { "Font", dictionary({ { "F1", PDFObject() }, { "F2", reference(7) } }) } });
        PDFContentResources resources = PDFContentResources::read(&document, resourcesObject, operations);
        QVERIFY(resources.getEntries(Category::XObject).empty());
        QVERIFY(resources.getObject(Category::Font, "F1").isNull());
        QVERIFY(resources.getObject(Category::Font, "F2") == reference(7));
        QVERIFY(resources.getUndefinedNames(Category::Font) == std::set<QByteArray>{ "F1" });
        QCOMPARE(resources.allocateName(Category::Font, "F"), QByteArray("F3"));
        QVERIFY(!resources.removeAdded(Category::Font, "F1"));
    }

    // A key written twice: the first one counts, as for every reader of the dictionary
    {
        PDFDictionaryBuilder fonts;
        fonts.addEntry(PDFInplaceOrMemoryString("F1"), reference(7));
        fonts.addEntry(PDFInplaceOrMemoryString("F1"), reference(8));
        PDFDictionaryBuilder resourcesDictionary;
        resourcesDictionary.addEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));

        const PDFContentResources resources = PDFContentResources::read(&document, PDFObject::createDictionary(std::move(resourcesDictionary)), operations);
        QCOMPARE(entryNames(resources, Category::Font), (QList<QByteArray>{ "F1" }));
        QVERIFY(resources.getObject(Category::Font, "F1") == reference(7));
        QVERIFY(resources.getUndefinedNames(Category::Font).empty());
    }

    // The shared category: both keys read it; a change of one of them leaves the other one
    // (and the shared object) alone
    {
        PDFContentResources resources = PDFContentResources::read(&document, sharedResources, PDFContentOperationList());
        QCOMPARE(entryNames(resources, Category::Font), (QList<QByteArray>{ "A" }));
        QCOMPARE(entryNames(resources, Category::XObject), (QList<QByteArray>{ "A" }));
        QCOMPARE(resources.addObject(Category::Font, "F", reference(202)), QByteArray("F1"));

        const PDFObject written = resources.write(nullptr);
        QVERIFY(hasExactly(written.getDictionary()->get("Font"), { { "A", reference(201) }, { "F1", reference(202) } }));
        QVERIFY(written.getDictionary()->get("XObject") == sharedCategory);
        QVERIFY(hasExactly(document.getObject(sharedCategory), { { "A", reference(201) } }));
        QCOMPARE(entryNames(resources, Category::XObject), (QList<QByteArray>{ "A" }));
    }

    // The cycle: the reading ends (it never follows the values of the entries)
    {
        const PDFContentResources resources = PDFContentResources::read(&document, PDFObject::createReference(cycleResourcesReference), operations);
        QCOMPARE(entryNames(resources, Category::Font), (QList<QByteArray>{ "F1" }));
        QVERIFY(resources.getObject(Category::Font, "F1") == PDFObject::createReference(cycleResourcesReference));
        QVERIFY(resources.getUndefinedNames(Category::Font).empty());
    }
}

void ContentOperationsTest::resourcesManyNames()
{
    // A dictionary with the names GS1 ... GS20000 and operations, which use GS20001 ... GS20050
    // without a definition: the smallest free number is 20051
    const int definedCount = 20000;
    const int undefinedCount = 50;

    PDFDictionaryBuilder graphicStates;
    for (int i = 1; i <= definedCount; ++i)
    {
        graphicStates.addEntry(PDFInplaceOrMemoryString("GS" + QByteArray::number(i)), reference(1000 + i));
    }
    PDFDictionaryBuilder resourcesDictionary;
    resourcesDictionary.addEntry(PDFInplaceOrMemoryString("ExtGState"), PDFObject::createDictionary(std::move(graphicStates)));

    PDFContentOperationList operations;
    for (int i = 1; i <= undefinedCount; ++i)
    {
        const QByteArray resourceName = "GS" + QByteArray::number(definedCount + i);
        operations.append(PDFContentOperation::createOriginal(Op::SetGraphicState, " /" + resourceName + " gs", PDFContentByteRange(), {},
                                                              { PDFContentResourceName{ Category::ExtGState, resourceName } }, PDFContentOperation::None));
    }
    operations.finishOriginal(QByteArray());

    PDFContentResources resources = PDFContentResources::read(nullptr, PDFObject::createDictionary(std::move(resourcesDictionary)), operations);
    QCOMPARE(resources.getEntries(Category::ExtGState).size(), size_t(definedCount));
    QCOMPARE(resources.getUndefinedNames(Category::ExtGState).size(), size_t(undefinedCount));
    QCOMPARE(resources.allocateName(Category::ExtGState, "GS"), QByteArray("GS20051"));

    // 300 new objects get the next 300 numbers
    for (int i = 0; i < 300; ++i)
    {
        QCOMPARE(resources.addObject(Category::ExtGState, "GS", dictionary({ { "CA", PDFObject::createReal(i / 1000.0) } })), "GS" + QByteArray::number(definedCount + undefinedCount + 1 + i));
    }
    QCOMPARE(resources.getEntries(Category::ExtGState).size(), size_t(definedCount + 300));
    QVERIFY(resources.getObject(Category::ExtGState, "GS1") == reference(1001));
    QVERIFY(resources.getObject(Category::ExtGState, "GS20000") == reference(21000));
}

void ContentOperationsTest::resourcesCopiesInThreads()
{
    // The resources inside of a container are read by any thread, while other threads change
    // their own copies (a command, a writer of the worker)
    const PDFObject resourcesObject = dictionary({ { "ExtGState", dictionary({ { "GS1", dictionary({ { "LW", PDFObject::createInteger(1) } }) } }) } });
    const PDFContentResources original = PDFContentResources::read(nullptr, resourcesObject, PDFContentOperationList());

    const int threadCount = 8;
    std::atomic<int> failures{ 0 };
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([&original, &failures, threadIndex]()
        {
            for (int round = 0; round < 500; ++round)
            {
                const PDFObject object = PDFObject::createReference(PDFObjectReference(1000 * (threadIndex + 1) + round, 0));

                PDFContentResources copy = original;
                const QByteArray addedName = copy.addObject(PDFContentResourceCategory::ExtGState, "GS", object);
                const QByteArray addedFontName = copy.addObject(PDFContentResourceCategory::Font, "F", object);

                const bool isCopyRight = addedName == "GS2" && addedFontName == "F1" &&
                                         copy.getObject(PDFContentResourceCategory::ExtGState, "GS2") == object &&
                                         copy.getEntries(PDFContentResourceCategory::ExtGState).size() == 2 &&
                                         copy.isChanged();
                const bool isOriginalRight = original.getEntries(PDFContentResourceCategory::ExtGState).size() == 1 &&
                                             original.getObject(PDFContentResourceCategory::ExtGState, "GS2").isNull() &&
                                             original.allocateName(PDFContentResourceCategory::ExtGState, "GS") == "GS2" &&
                                             original.getEntries(PDFContentResourceCategory::Font).empty() &&
                                             !original.isChanged();

                if (!isCopyRight || !isOriginalRight)
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
    QCOMPARE(entryNames(original, Category::ExtGState), (QList<QByteArray>{ "GS1" }));
    QVERIFY(!original.isChanged());
}

void ContentOperationsTest::resourcesLookupByObject()
{
    // getName and addObject find an object by PDFObject equality: the same reference, or a direct
    // object of the same type and value (a dictionary with the same entries in the same order).
    // Of more entries with an equal object, the first one in the order of the entries is found.
    const PDFObject integerOne = dictionary({ { "x", PDFObject::createInteger(1) } });
    const PDFObject realOne = dictionary({ { "x", PDFObject::createReal(1.0) } });
    const PDFObject zero = dictionary({ { "y", PDFObject::createReal(0.0) } });
    const PDFObject negativeZero = dictionary({ { "y", PDFObject::createReal(-0.0) } });
    const PDFObject notANumber = dictionary({ { "z", PDFObject::createReal(std::numeric_limits<PDFReal>::quiet_NaN()) } });
    const PDFObject twoKeys = dictionary({ { "a", PDFObject::createInteger(1) }, { "b", PDFObject::createInteger(2) } });
    const PDFObject twoKeysSwapped = dictionary({ { "b", PDFObject::createInteger(2) }, { "a", PDFObject::createInteger(1) } });

    const PDFObject resourcesObject = dictionary({ { "ExtGState", dictionary({ { "A", reference(5) },
                                                                               { "B", reference(5) },
                                                                               { "C", integerOne },
                                                                               { "D", integerOne },
                                                                               { "E", realOne },
                                                                               { "F", negativeZero },
                                                                               { "G", notANumber },
                                                                               { "H", twoKeys } }) } });
    PDFContentResources resources = PDFContentResources::read(nullptr, resourcesObject, PDFContentOperationList());

    QCOMPARE(resources.getName(Category::ExtGState, reference(5)), QByteArray("A"));
    QCOMPARE(resources.getName(Category::ExtGState, PDFObject::createReference(PDFObjectReference(5, 1))), QByteArray());
    QCOMPARE(resources.getName(Category::ExtGState, integerOne), QByteArray("C"));
    QCOMPARE(resources.getName(Category::ExtGState, realOne), QByteArray("E"));

    // 0.0 is equal to -0.0; NaN is equal to nothing, also to itself
    QCOMPARE(resources.getName(Category::ExtGState, zero), QByteArray("F"));
    QCOMPARE(resources.getName(Category::ExtGState, notANumber), QByteArray());

    // The order of the keys of a dictionary plays a role here (unlike the ExtGState writer)
    QCOMPARE(resources.getName(Category::ExtGState, twoKeys), QByteArray("H"));
    QCOMPARE(resources.getName(Category::ExtGState, twoKeysSwapped), QByteArray());

    // Another category has no such entries; a null object is never a resource
    QCOMPARE(resources.getName(Category::Font, reference(5)), QByteArray());
    QCOMPARE(resources.getName(Category::ExtGState, PDFObject()), QByteArray());

    // addObject of an equal object gives the first name; of another one a new name (also for
    // the NaN dictionary, every time)
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", reference(5)), QByteArray("A"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", integerOne), QByteArray("C"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", zero), QByteArray("F"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", twoKeysSwapped), QByteArray("GS1"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", notANumber), QByteArray("GS2"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", notANumber), QByteArray("GS3"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", reference(6)), QByteArray("GS4"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", twoKeysSwapped), QByteArray("GS1"));
    QCOMPARE(resources.getName(Category::ExtGState, reference(6)), QByteArray("GS4"));

    // An added entry with a new object: found by the new one, no longer by the old one
    QVERIFY(resources.setObject(Category::ExtGState, "GS4", reference(7)));
    QCOMPARE(resources.getName(Category::ExtGState, reference(6)), QByteArray());
    QCOMPARE(resources.getName(Category::ExtGState, reference(7)), QByteArray("GS4"));

    // An added entry with the object of an earlier one: the earlier one is found first
    QVERIFY(resources.setObject(Category::ExtGState, "GS4", reference(5)));
    QCOMPARE(resources.getName(Category::ExtGState, reference(5)), QByteArray("A"));
    QCOMPARE(resources.getName(Category::ExtGState, reference(7)), QByteArray());

    // A removed entry is not found; the entries after it still are
    QVERIFY(resources.setObject(Category::ExtGState, "GS4", reference(8)));
    QVERIFY(resources.removeAdded(Category::ExtGState, "GS1"));
    QCOMPARE(resources.getName(Category::ExtGState, twoKeysSwapped), QByteArray());
    QCOMPARE(resources.getName(Category::ExtGState, reference(8)), QByteArray("GS4"));
    QCOMPARE(resources.addObject(Category::ExtGState, "GS", reference(8)), QByteArray("GS4"));
    QCOMPARE(resources.getName(Category::ExtGState, integerOne), QByteArray("C"));
    QCOMPARE(entryNames(resources, Category::ExtGState), (QList<QByteArray>{ "A", "B", "C", "D", "E", "F", "G", "H", "GS2", "GS3", "GS4" }));

    // A copy changed later: the original keeps its own answers
    PDFContentResources copy = resources;
    QCOMPARE(copy.addObject(Category::ExtGState, "GS", reference(9)), QByteArray("GS1"));
    QCOMPARE(copy.getName(Category::ExtGState, reference(9)), QByteArray("GS1"));
    QCOMPARE(resources.getName(Category::ExtGState, reference(9)), QByteArray());
}

// -------------------------------------------------------------------------------------------
//  ExtGState
// -------------------------------------------------------------------------------------------

void ContentOperationsTest::extGStateKeys_data()
{
    QTest::addColumn<int>("property");
    QTest::addColumn<QByteArray>("key");

    // ISO 32000-2, 8.4.5, Table 57: the key of every parameter, which has no operator of its
    // own. An empty key: the parameter is set by its operator (8.4.4, 8.6.8, 9.3), not by a
    // dictionary of the editor.
    QTest::newRow("LineWidth") << int(Property::LineWidth) << QByteArray();
    QTest::newRow("LineCap") << int(Property::LineCap) << QByteArray();
    QTest::newRow("LineJoin") << int(Property::LineJoin) << QByteArray();
    QTest::newRow("MiterLimit") << int(Property::MiterLimit) << QByteArray();
    QTest::newRow("DashPattern") << int(Property::DashPattern) << QByteArray();
    QTest::newRow("RenderingIntent") << int(Property::RenderingIntent) << QByteArray();
    QTest::newRow("Flatness") << int(Property::Flatness) << QByteArray();
    QTest::newRow("StrokeColor") << int(Property::StrokeColor) << QByteArray();
    QTest::newRow("FillColor") << int(Property::FillColor) << QByteArray();
    QTest::newRow("Font") << int(Property::Font) << QByteArray();
    QTest::newRow("CharacterSpacing") << int(Property::CharacterSpacing) << QByteArray();
    QTest::newRow("WordSpacing") << int(Property::WordSpacing) << QByteArray();
    QTest::newRow("HorizontalScaling") << int(Property::HorizontalScaling) << QByteArray();
    QTest::newRow("Leading") << int(Property::Leading) << QByteArray();
    QTest::newRow("TextRenderingMode") << int(Property::TextRenderingMode) << QByteArray();
    QTest::newRow("TextRise") << int(Property::TextRise) << QByteArray();
    QTest::newRow("TextKnockout") << int(Property::TextKnockout) << QByteArray("TK");
    QTest::newRow("AlphaStroking") << int(Property::AlphaStroking) << QByteArray("CA");
    QTest::newRow("AlphaFilling") << int(Property::AlphaFilling) << QByteArray("ca");
    QTest::newRow("BlendMode") << int(Property::BlendMode) << QByteArray("BM");
    QTest::newRow("SoftMask") << int(Property::SoftMask) << QByteArray("SMask");
    QTest::newRow("AlphaIsShape") << int(Property::AlphaIsShape) << QByteArray("AIS");
    QTest::newRow("StrokeAdjustment") << int(Property::StrokeAdjustment) << QByteArray("SA");
    QTest::newRow("OverprintStroking") << int(Property::OverprintStroking) << QByteArray("OP");
    QTest::newRow("OverprintFilling") << int(Property::OverprintFilling) << QByteArray("op");
    QTest::newRow("OverprintMode") << int(Property::OverprintMode) << QByteArray("OPM");
    QTest::newRow("BlackGeneration") << int(Property::BlackGeneration) << QByteArray("BG");
    QTest::newRow("UndercolorRemoval") << int(Property::UndercolorRemoval) << QByteArray("UCR");
    QTest::newRow("Transfer") << int(Property::Transfer) << QByteArray("TR");
    QTest::newRow("Halftone") << int(Property::Halftone) << QByteArray("HT");
    QTest::newRow("HalftoneOrigin") << int(Property::HalftoneOrigin) << QByteArray("HTO");
    QTest::newRow("Smoothness") << int(Property::Smoothness) << QByteArray("SM");
    QTest::newRow("BlackPointCompensation") << int(Property::BlackPointCompensation) << QByteArray("UseBlackPtComp");
}

void ContentOperationsTest::extGStateKeys()
{
    QFETCH(int, property);
    QFETCH(QByteArray, key);

    QCOMPARE(PDFContentExtGStateWriter::getKey(Property(property)), key);
    QCOMPARE(PDFContentExtGStateWriter::isExtGStateProperty(Property(property)), !key.isEmpty());
}

void ContentOperationsTest::extGStateDictionary()
{
    // The table above has every property once
    QCOMPARE(int(Property::Count), 33);

    // One changed parameter: /Type and exactly its key. gs sets only the keys, which are
    // present (8.4.5), so nothing else of the state changes - no /CA, and never /SMask (R-6)
    const PDFObject alpha = PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, realValue(0.5) } });
    QVERIFY(hasExactly(alpha, { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    QVERIFY(!alpha.getDictionary()->hasKey("SMask"));
    QVERIFY(!alpha.getDictionary()->hasKey("CA"));

    // Every kind of a value
    const PDFObject all = PDFContentExtGStateWriter::createDictionary({ { Property::TextKnockout, booleanValue(false) },
                                                                        { Property::AlphaStroking, realValue(0.25) },
                                                                        { Property::AlphaFilling, realValue(1.0) },
                                                                        { Property::BlendMode, nameValue("Multiply") },
                                                                        { Property::AlphaIsShape, booleanValue(true) },
                                                                        { Property::StrokeAdjustment, booleanValue(true) },
                                                                        { Property::OverprintStroking, booleanValue(true) },
                                                                        { Property::OverprintFilling, booleanValue(false) },
                                                                        { Property::OverprintMode, integerValue(1) },
                                                                        { Property::BlackGeneration, objectValue(reference(11)) },
                                                                        { Property::UndercolorRemoval, objectValue(reference(12)) },
                                                                        { Property::Transfer, objectValue(reference(13)) },
                                                                        { Property::Halftone, objectValue(reference(14)) },
                                                                        { Property::HalftoneOrigin, objectValue(ContentTestDocuments::createIntegerArrayObject({ 3, 4 })) },
                                                                        { Property::Smoothness, realValue(0.02) },
                                                                        { Property::BlackPointCompensation, nameValue("ON") } });
    QVERIFY(hasExactly(all, { { "Type", name("ExtGState") },
                              { "TK", PDFObject::createBool(false) },
                              { "CA", PDFObject::createReal(0.25) },
                              { "ca", PDFObject::createReal(1.0) },
                              { "BM", name("Multiply") },
                              { "AIS", PDFObject::createBool(true) },
                              { "SA", PDFObject::createBool(true) },
                              { "OP", PDFObject::createBool(true) },
                              { "op", PDFObject::createBool(false) },
                              { "OPM", PDFObject::createInteger(1) },
                              { "BG", reference(11) },
                              { "UCR", reference(12) },
                              { "TR", reference(13) },
                              { "HT", reference(14) },
                              { "HTO", ContentTestDocuments::createIntegerArrayObject({ 3, 4 }) },
                              { "SM", PDFObject::createReal(0.02) },
                              { "UseBlackPtComp", name("ON") } }));

    // Table 57: BG and UCR are functions; the name Default is a value of BG2 and UCR2 only. TR
    // takes a function or the name Identity, TR2 the name Default too: a name goes to the
    // second key, a function to the plain one.
    const PDFObject defaults = PDFContentExtGStateWriter::createDictionary({ { Property::BlackGeneration, nameValue("Default") },
                                                                             { Property::UndercolorRemoval, nameValue("Default") },
                                                                             { Property::Transfer, nameValue("Default") },
                                                                             { Property::Halftone, nameValue("Default") } });
    QVERIFY(hasExactly(defaults, { { "Type", name("ExtGState") },
                                   { "BG2", name("Default") },
                                   { "UCR2", name("Default") },
                                   { "TR2", name("Default") },
                                   { "HT", name("Default") } }));

    // The same for a name given as an object (the value of the symbolic state of the source)
    const PDFObject objectDefaults = PDFContentExtGStateWriter::createDictionary({ { Property::BlackGeneration, objectValue(name("Default")) },
                                                                                   { Property::Transfer, objectValue(name("Identity")) } });
    QVERIFY(hasExactly(objectDefaults, { { "Type", name("ExtGState") }, { "BG2", name("Default") }, { "TR2", name("Identity") } }));

    // A blend mode may be an array of names of the document (kept as it is)
    const PDFObject blendModes = ContentTestDocuments::createArrayObject({ name("Hue"), name("Normal") });
    QVERIFY(hasExactly(PDFContentExtGStateWriter::createDictionary({ { Property::BlendMode, objectValue(blendModes) } }),
                       { { "Type", name("ExtGState") }, { "BM", blendModes } }));

    // The overprint of filling alone sets only itself
    QVERIFY(hasExactly(PDFContentExtGStateWriter::createDictionary({ { Property::OverprintFilling, booleanValue(true) } }),
                       { { "Type", name("ExtGState") }, { "op", PDFObject::createBool(true) } }));

    // No property: a dictionary, which sets nothing
    QVERIFY(hasExactly(PDFContentExtGStateWriter::createDictionary({ }), { { "Type", name("ExtGState") } }));
}

void ContentOperationsTest::extGStateRefusedValues()
{
    // R-6: a soft mask is never a key among others, and never a key of this function
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::SoftMask, objectValue(reference(9)) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, realValue(0.5) }, { Property::SoftMask, objectValue(reference(9)) } }).isNull());

    // A property with an operator of its own is not written by a dictionary of the editor
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::LineWidth, realValue(2.0) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, realValue(0.5) }, { Property::Font, objectValue(reference(9)) } }).isNull());

    // Table 57, /OP: "Specifying an OP entry sets both parameters unless there is also an op
    // entry in the same graphics state parameter dictionary". /OP alone would change the
    // overprint of filling, which was not asked for: both, or none
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::OverprintStroking, booleanValue(true) } }).isNull());
    QVERIFY(!PDFContentExtGStateWriter::createDictionary({ { Property::OverprintStroking, booleanValue(true) }, { Property::OverprintFilling, booleanValue(true) } }).isNull());

    // A value of a wrong kind, a number, which is not finite, an object, which is null
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, booleanValue(true) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, realValue(qQNaN()) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaStroking, realValue(qInf()) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::TextKnockout, integerValue(1) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::OverprintMode, realValue(1.0) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::BlendMode, realValue(1.0) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::BlendMode, nameValue("") } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::Halftone, objectValue(PDFObject()) } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::HalftoneOrigin, nameValue("Default") } }).isNull());
    QVERIFY(PDFContentExtGStateWriter::createDictionary({ { Property::AlphaFilling, PDFContentStateValue() } }).isNull());
}

void ContentOperationsTest::extGStateSoftMaskDictionary()
{
    // R-6: the single key /SMask with the mask of the document by its reference
    QVERIFY(hasExactly(PDFContentExtGStateWriter::createSoftMaskDictionary(reference(9)), { { "SMask", reference(9) } }));

    // ISO 32000-2, 11.6.5.1: the name None says there is no mask
    QVERIFY(hasExactly(PDFContentExtGStateWriter::createSoftMaskDictionary(PDFObject()), { { "SMask", name("None") } }));
}

void ContentOperationsTest::extGStateEqual()
{
    const PDFObject left = dictionary({ { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.5) }, { "BM", name("Multiply") } });

    // The same keys with the same values: the order of the keys and /Type (which sets nothing)
    // play no role
    QVERIFY(PDFContentExtGStateWriter::isEqual(left, left));
    QVERIFY(PDFContentExtGStateWriter::isEqual(left, dictionary({ { "BM", name("Multiply") }, { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.5) } })));
    QVERIFY(PDFContentExtGStateWriter::isEqual(left, dictionary({ { "BM", name("Multiply") }, { "CA", PDFObject::createReal(0.5) } })));

    // Another value, a missing key, one more key
    QVERIFY(!PDFContentExtGStateWriter::isEqual(left, dictionary({ { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.25) }, { "BM", name("Multiply") } })));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(left, dictionary({ { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.5) } })));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(left, dictionary({ { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.5) }, { "BM", name("Multiply") }, { "ca", PDFObject::createReal(0.5) } })));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(left, dictionary({ { "ca", PDFObject::createReal(0.5) }, { "BM", name("Multiply") } })));

    // A soft mask dictionary differs from the one, which resets the mask
    QVERIFY(PDFContentExtGStateWriter::isEqual(PDFContentExtGStateWriter::createSoftMaskDictionary(reference(9)), PDFContentExtGStateWriter::createSoftMaskDictionary(reference(9))));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(PDFContentExtGStateWriter::createSoftMaskDictionary(reference(9)), PDFContentExtGStateWriter::createSoftMaskDictionary(PDFObject())));

    // Only dictionaries are compared (a reference is not followed: there is no document here)
    QVERIFY(!PDFContentExtGStateWriter::isEqual(reference(9), reference(9)));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(left, PDFObject()));
    QVERIFY(!PDFContentExtGStateWriter::isEqual(PDFObject(), PDFObject()));
}

void ContentOperationsTest::extGStateEqualityKey()
{
    // The keys of the writer's maps (A1L1-3): two objects have the same key of equality exactly
    // when isEqual calls them equal, and the same object key exactly when PDFObject::operator==
    // does; an object equal to nothing has no key. Every pair of the objects below is checked.
    const PDFReal notANumber = std::numeric_limits<PDFReal>::quiet_NaN();

    PDFDictionaryBuilder twice;
    twice.addEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createReal(0.5));
    twice.addEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createReal(0.25));

    PDFDictionaryBuilder twiceOtherFirst;
    twiceOtherFirst.addEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createReal(0.25));
    twiceOtherFirst.addEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createReal(0.5));

    const std::vector<PDFObject> objects = {
        dictionary({ { "Type", name("ExtGState") }, { "CA", PDFObject::createReal(0.5) }, { "BM", name("Multiply") } }),
        dictionary({ { "BM", name("Multiply") }, { "CA", PDFObject::createReal(0.5) } }),
        dictionary({ { "CA", PDFObject::createReal(0.5) }, { "BM", name("Multiply") }, { "SMask", PDFObject() } }),
        dictionary({ { "CA", PDFObject::createReal(0.5) } }),
        dictionary({ { "CA", PDFObject::createReal(-0.0) } }),
        dictionary({ { "CA", PDFObject::createReal(0.0) } }),
        dictionary({ { "CA", PDFObject::createInteger(0) } }),
        dictionary({ { "CA", PDFObject::createReal(notANumber) } }),
        dictionary({ { "Type", PDFObject::createReal(notANumber) }, { "CA", PDFObject::createReal(0.5) } }),
        PDFObject::createDictionary(std::move(twice)),
        PDFObject::createDictionary(std::move(twiceOtherFirst)),
        dictionary({ { "BM", name("Multiply") } }),
        dictionary({ { "BM", PDFObject::createString(QByteArray("Multiply")) } }),
        dictionary({ { "BM", ContentTestDocuments::createArrayObject({ name("Multiply"), name("Normal") }) } }),
        dictionary({ { "BM", ContentTestDocuments::createArrayObject({ name("Normal"), name("Multiply") }) } }),
        dictionary({ { "BM", ContentTestDocuments::createArrayObject({ name("MultiplyNormal") }) } }),
        dictionary({ { "SMask", dictionary({ { "S", name("Alpha") }, { "G", reference(9) } }) } }),
        dictionary({ { "SMask", dictionary({ { "G", reference(9) }, { "S", name("Alpha") } }) } }),
        dictionary({ { "SMask", reference(9) } }),
        dictionary({ { "SMask", PDFObject::createReference(PDFObjectReference(9, 1)) } }),
        dictionary({ { "OP", PDFObject::createBool(true) } }),
        dictionary({ { "OP", PDFObject::createBool(false) } }),
        dictionary({ { "Type", name("ExtGState") } }),
        dictionary({ }),
        reference(9),
        PDFObject(),
        PDFObject::createReal(0.5),
        ContentTestDocuments::createArrayObject({ PDFObject::createReal(notANumber) })
    };

    for (size_t i = 0; i < objects.size(); ++i)
    {
        for (size_t j = 0; j < objects.size(); ++j)
        {
            const QByteArray leftKey = PDFContentExtGStateWriter::getEqualityKey(objects[i]);
            const QByteArray rightKey = PDFContentExtGStateWriter::getEqualityKey(objects[j]);
            const bool isSameKey = !leftKey.isEmpty() && leftKey == rightKey;
            QVERIFY2(isSameKey == PDFContentExtGStateWriter::isEqual(objects[i], objects[j]), qPrintable(QStringLiteral("equality key of %1 and %2").arg(i).arg(j)));

            const QByteArray leftObjectKey = PDFContentExtGStateWriter::getObjectKey(objects[i]);
            const QByteArray rightObjectKey = PDFContentExtGStateWriter::getObjectKey(objects[j]);
            const bool isSameObjectKey = !leftObjectKey.isEmpty() && leftObjectKey == rightObjectKey;
            QVERIFY2(isSameObjectKey == (objects[i] == objects[j]), qPrintable(QStringLiteral("object key of %1 and %2").arg(i).arg(j)));
        }
    }

    // By hand: /Type and a null value set nothing, the order of the keys plays no role, -0 is 0;
    // a real 0 is not the integer 0; a NaN is equal to nothing (only a key, which is no entry -
    // /Type - may hold it); the first one of a key written twice counts
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[0]) == PDFContentExtGStateWriter::getEqualityKey(objects[1]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[1]) == PDFContentExtGStateWriter::getEqualityKey(objects[2]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[4]) == PDFContentExtGStateWriter::getEqualityKey(objects[5]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[5]) != PDFContentExtGStateWriter::getEqualityKey(objects[6]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[7]).isEmpty());
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[8]) == PDFContentExtGStateWriter::getEqualityKey(objects[3]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[9]) == PDFContentExtGStateWriter::getEqualityKey(objects[3]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[10]) != PDFContentExtGStateWriter::getEqualityKey(objects[3]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(objects[22]) == PDFContentExtGStateWriter::getEqualityKey(objects[23]));
    QVERIFY(PDFContentExtGStateWriter::getEqualityKey(reference(9)).isEmpty());
    QVERIFY(!PDFContentExtGStateWriter::getObjectKey(PDFObject()).isEmpty());
    QVERIFY(PDFContentExtGStateWriter::getObjectKey(objects.back()).isEmpty());
}

// -------------------------------------------------------------------------------------------
//  Canonical writing
// -------------------------------------------------------------------------------------------

void ContentOperationsTest::writePath_data()
{
    QTest::addColumn<int>("shape");
    QTest::addColumn<QByteArray>("expected");

    // zadani_jadro.md 4.4, canonical writing: re for a rectangle, else m l c h, one operator
    // a line; numbers without an exponent, without ".0", "-0" is "0"
    QTest::newRow("rectangle") << 0 << QByteArray("10 20 30 40 re");
    QTest::newRow("rectangle with a negative size") << 1 << QByteArray("10.5 20 -30 40.25 re");
    QTest::newRow("triangle") << 2 << QByteArray("0 0 m\n10 0 l\n5 8 l\nh");
    QTest::newRow("curve") << 3 << QByteArray("0 0 m\n1 2 3 4 5 6 c");
    QTest::newRow("two rectangles") << 4 << QByteArray("0 0 m\n1 0 l\n1 2 l\n0 2 l\nh\n5 5 m\n8 5 l\n8 9 l\n5 9 l\nh");
    QTest::newRow("numbers") << 5 << QByteArray("0.5 -0.25 m\n0 0.00001 l\n1234.5 -1000000 l");
    QTest::newRow("empty") << 6 << QByteArray();
}

void ContentOperationsTest::writePath()
{
    QFETCH(int, shape);
    QFETCH(QByteArray, expected);

    PDFContentPath path;
    switch (shape)
    {
        case 0:
            path.addRectangle(QRectF(10, 20, 30, 40));
            break;

        case 1:
            // The rectangle of "re" is never normalized: the direction of the path matters
            path.addRectangle(QRectF(10.5, 20, -30, 40.25));
            break;

        case 2:
            path.moveTo(QPointF(0, 0));
            path.lineTo(QPointF(10, 0));
            path.lineTo(QPointF(5, 8));
            path.close();
            break;

        case 3:
            path.moveTo(QPointF(0, 0));
            path.curveTo(QPointF(1, 2), QPointF(3, 4), QPointF(5, 6));
            break;

        case 4:
            // Two rectangles are not "exactly one rectangle": the segments of re (8.5.2.1: m,
            // three l, h)
            path.addRectangle(QRectF(0, 0, 1, 2));
            path.addRectangle(QRectF(5, 5, 3, 4));
            break;

        case 5:
            // -0.0 is "0"; 0.00001 has no exponent; an integral value has no fraction
            path.moveTo(QPointF(0.5, -0.25));
            path.lineTo(QPointF(-0.0, 0.00001));
            path.lineTo(QPointF(1234.5, -1000000.0));
            break;

        default:
            break;
    }

    QString errorText = QStringLiteral("old text");
    QCOMPARE(PDFContentStreamWriter::writePath(path, &errorText), expected);
    QVERIFY(errorText.isEmpty());

    // The text of the error is optional
    QCOMPARE(PDFContentStreamWriter::writePath(path, nullptr), expected);
}

void ContentOperationsTest::writePathNotFinite()
{
    // zadani_jadro.md 4.4: NaN and infinity are an error, never a number of the content
    for (const qreal value : { qQNaN(), qInf(), -qInf() })
    {
        PDFContentPath path;
        path.moveTo(QPointF(0, 0));
        path.lineTo(QPointF(10, value));

        QString errorText;
        QCOMPARE(PDFContentStreamWriter::writePath(path, &errorText), QByteArray());
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(PDFContentStreamWriter::writePath(path, nullptr), QByteArray());

        PDFContentPath rectangle;
        rectangle.addRectangle(QRectF(0, 0, value, 5));
        errorText.clear();
        QCOMPARE(PDFContentStreamWriter::writePath(rectangle, &errorText), QByteArray());
        QVERIFY(!errorText.isEmpty());

        errorText.clear();
        QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform(1, 0, 0, 1, value, 0), &errorText), QByteArray());
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform(value, 0, 0, 1, 0, 0), nullptr), QByteArray());
    }
}

void ContentOperationsTest::writePathPaint_data()
{
    QTest::addColumn<bool>("stroke");
    QTest::addColumn<bool>("fill");
    QTest::addColumn<bool>("fillEvenOdd");
    QTest::addColumn<bool>("closeBeforePaint");
    QTest::addColumn<int>("clip");
    QTest::addColumn<QByteArray>("expected");

    const int none = int(PDFContentPathPaint::Clip::None);
    const int nonZero = int(PDFContentPathPaint::Clip::NonZero);
    const int evenOdd = int(PDFContentPathPaint::Clip::EvenOdd);

    // ISO 32000-2, 8.5.3.1: the path-painting operators (F is the obsolete twin of f)
    QTest::newRow("S") << true << false << false << false << none << QByteArray("S");
    QTest::newRow("s") << true << false << false << true << none << QByteArray("s");
    QTest::newRow("f") << false << true << false << false << none << QByteArray("f");
    QTest::newRow("f*") << false << true << true << false << none << QByteArray("f*");
    QTest::newRow("B") << true << true << false << false << none << QByteArray("B");
    QTest::newRow("B*") << true << true << true << false << none << QByteArray("B*");
    QTest::newRow("b") << true << true << false << true << none << QByteArray("b");
    QTest::newRow("b*") << true << true << true << true << none << QByteArray("b*");
    QTest::newRow("n") << false << false << false << false << none << QByteArray("n");

    // 8.5.4: W or W* stands right before the painting operator
    QTest::newRow("W n") << false << false << false << false << nonZero << QByteArray("W n");
    QTest::newRow("W* n") << false << false << false << false << evenOdd << QByteArray("W* n");
    QTest::newRow("W f") << false << true << false << false << nonZero << QByteArray("W f");
    QTest::newRow("W* f") << false << true << false << false << evenOdd << QByteArray("W* f");
    QTest::newRow("W f*") << false << true << true << false << nonZero << QByteArray("W f*");
    QTest::newRow("W* b*") << true << true << true << true << evenOdd << QByteArray("W* b*");
    QTest::newRow("W S") << true << false << false << false << nonZero << QByteArray("W S");
}

void ContentOperationsTest::writePathPaint()
{
    QFETCH(bool, stroke);
    QFETCH(bool, fill);
    QFETCH(bool, fillEvenOdd);
    QFETCH(bool, closeBeforePaint);
    QFETCH(int, clip);
    QFETCH(QByteArray, expected);

    PDFContentPathPaint paint;
    paint.stroke = stroke;
    paint.fill = fill;
    paint.fillEvenOdd = fillEvenOdd;
    paint.closeBeforePaint = closeBeforePaint;
    paint.clip = PDFContentPathPaint::Clip(clip);

    QCOMPARE(PDFContentStreamWriter::writePathPaint(paint), expected);
}

void ContentOperationsTest::writeMatrix()
{
    // ISO 32000-2, 8.3.4: the operands of cm are a b c d e f of the matrix [a b 0; c d 0; e f 1],
    // which is QTransform(m11, m12, m21, m22, dx, dy)
    QString errorText = QStringLiteral("old text");
    QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform(1, 2, 3, 4, 5, 6), &errorText), QByteArray("1 2 3 4 5 6 cm"));
    QVERIFY(errorText.isEmpty());

    QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform(), nullptr), QByteArray("1 0 0 1 0 0 cm"));

    // A rotation by a right angle counterclockwise (8.3.3: cos sin -sin cos 0 0) and a move
    QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform(0, 1, -1, 0, 100, 50.5), nullptr), QByteArray("0 1 -1 0 100 50.5 cm"));

    // A translation of QTransform is e f
    QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform::fromTranslate(10, -20), nullptr), QByteArray("1 0 0 1 10 -20 cm"));
    QCOMPARE(PDFContentStreamWriter::writeMatrix(QTransform::fromScale(0.5, 0.25), nullptr), QByteArray("0.5 0 0 0.25 0 0 cm"));

    // A matrix, which is not affine, is no matrix of a content
    QTransform projective(1, 0, 0.5, 0, 1, 0, 0, 0, 1);
    QCOMPARE(PDFContentStreamWriter::writeMatrix(projective, &errorText), QByteArray());
    QVERIFY(!errorText.isEmpty());
}

// -------------------------------------------------------------------------------------------
//  Writer of a container
// -------------------------------------------------------------------------------------------

void ContentOperationsTest::writeUnchanged_data()
{
    addContentRows();
}

void ContentOperationsTest::writeUnchanged()
{
    QFETCH(QList<QByteArray>, pieces);
    QFETCH(QByteArray, tail);
    QFETCH(QByteArray, content);

    std::vector<ContentPiece> contentPieces;
    std::vector<size_t> order;
    for (const QByteArray& rawBytes : pieces)
    {
        // The inline images of the rows are marked as such: the writer must not treat them
        // differently between their original neighbors
        ContentPiece contentPiece;
        contentPiece.contentOperator = rawBytes.contains("ID ") ? Op::InlineImage : Op::Invalid;
        contentPiece.rawBytes = rawBytes;
        order.push_back(contentPieces.size());
        contentPieces.push_back(std::move(contentPiece));
    }

    const PDFContentOperationList list = createList(contentPieces, tail);

    // Every operation is a node of its own: all joints are joints of original neighbors, which
    // are written as they were (R-7) - the content byte for byte, the tail included (KE.21: the
    // operations nobody touched stay byte identical)
    std::vector<PDFContentNodePointer> leaves;
    quint64 id = 2;
    for (const size_t index : order)
    {
        leaves.push_back(createLeaf(Kind::Opaque, id++, index, 1));
    }

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot(std::move(leaves))));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, content);
    QVERIFY(!result.resources.isChanged());

    // The same with one node for all operations
    const PDFContentStreamWriteResult single = PDFContentStreamWriter::write(createContainer(list, createRoot({ createLeaf(Kind::Opaque, 2, 0, list.getCount()) })));
    QVERIFY(single.isSuccess());
    QCOMPARE(single.content, content);
}

void ContentOperationsTest::writeUnchangedTree()
{
    // A tree with every kind of a bracket: Root [ Group q [ StateRun cm, MarkedContent BDC
    // [ TextObject BT [ StateRun Tf, TextShow Tj ] ET ] EMC ] Q, Path re f ], then the tail
    const QByteArray content("q 1 0 0 1 5 5 cm\n/Span <</MCID 0>> BDC BT /F1 12 Tf (Hi) Tj ET EMC Q\n0 0 10 10 re\nf % end\n");
    const PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"),
                                                      piece(Op::AdjustCurrentTransformationMatrix, " 1 0 0 1 5 5 cm"),
                                                      piece(Op::MarkedContentBeginWithProperties, "\n/Span <</MCID 0>> BDC"),
                                                      piece(Op::TextBegin, " BT"),
                                                      piece(Op::TextSetFontAndFontSize, " /F1 12 Tf", { { Category::Font, "F1" } }),
                                                      piece(Op::TextShowTextString, " (Hi) Tj"),
                                                      piece(Op::TextEnd, " ET"),
                                                      piece(Op::MarkedContentEnd, " EMC"),
                                                      piece(Op::RestoreGraphicState, " Q"),
                                                      piece(Op::Rectangle, "\n0 0 10 10 re"),
                                                      piece(Op::PathFillWinding, "\nf") }, QByteArray(" % end\n"));
    QCOMPARE(list.getOriginalContent(), content);

    PDFContentNode::Data text = nodeData(Kind::TextObject, 5, 3, 1);
    text.suffix = PDFContentOperationSpan{ 6, 1 };
    PDFContentNode::Data marked = nodeData(Kind::MarkedContent, 4, 2, 1);
    marked.suffix = PDFContentOperationSpan{ 7, 1 };
    PDFContentNode::Data group = nodeData(Kind::Group, 2, 0, 1);
    group.suffix = PDFContentOperationSpan{ 8, 1 };

    const PDFContentNodePointer textNode = createNode(text, { createLeaf(Kind::StateRun, 6, 4, 1), createLeaf(Kind::TextShow, 7, 5, 1) });
    const PDFContentNodePointer markedNode = createNode(marked, { textNode });
    const PDFContentNodePointer groupNode = createNode(group, { createLeaf(Kind::StateRun, 3, 1, 1), markedNode });
    const PDFContentNodePointer pathNode = createLeaf(Kind::Path, 8, 9, 2);

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ groupNode, pathNode })));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, content);

    // R-7: a missing Q, ET or EMC is not added - a group without its suffix ends without Q
    PDFContentNode::Data openGroup = nodeData(Kind::Group, 2, 0, 1);
    const PDFContentNodePointer openGroupNode = createNode(openGroup, { createLeaf(Kind::StateRun, 3, 1, 1) });
    const PDFContentOperationList openList = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::AdjustCurrentTransformationMatrix, " 1 0 0 1 5 5 cm") }, QByteArray("\n"));
    const PDFContentStreamWriteResult openResult = PDFContentStreamWriter::write(createContainer(openList, createRoot({ openGroupNode })));
    QVERIFY(openResult.isSuccess());
    QCOMPARE(openResult.content, QByteArray("q 1 0 0 1 5 5 cm\n"));
}

void ContentOperationsTest::writeJoints()
{
    // R-7: between two written operations, which were not neighbors in the original content,
    // one line feed is written, whenever two regular characters would meet (7.2.3); original
    // neighbors are joined unchanged

    // The path moved before the group: "f" would meet "q" and read as the unknown operator "fq"
    {
        const PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::AdjustCurrentTransformationMatrix, " 1 0 0 1 5 5 cm"),
                                                          piece(Op::PaintXObject, " /Im1 Do"), piece(Op::RestoreGraphicState, " Q"),
                                                          piece(Op::Rectangle, "\n0 0 10 10 re"), piece(Op::PathFillWinding, "\nf") }, QByteArray());
        QCOMPARE(writeInOrder(list, { 4, 5, 0, 1, 2, 3 }), QByteArray("\n0 0 10 10 re\nf\nq 1 0 0 1 5 5 cm /Im1 Do Q"));

        // The other way round nothing meets: "Q" is followed by the line feed of the path
        QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4, 5 }), QByteArray("q 1 0 0 1 5 5 cm /Im1 Do Q\n0 0 10 10 re\nf"));
    }

    // A delimiter separates by itself (7.2.3): "Do" and "/Im1" need nothing between them
    {
        const PDFContentOperationList list = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::PaintXObject, " /Im2 Do") }, QByteArray());
        QCOMPARE(writeInOrder(list, { 1, 0 }), QByteArray(" /Im2 Do/Im1 Do"));
    }

    // A deleted operation: what follows an operator in a content begins with a white space or
    // with a delimiter, so nothing is added
    {
        const PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::PaintXObject, " /Im1 Do"), piece(Op::TextShowTextString, "(a)Tj"),
                                                          piece(Op::RestoreGraphicState, " Q") }, QByteArray());
        QCOMPARE(writeInOrder(list, { 0, 1, 2, 3 }), QByteArray("q /Im1 Do(a)Tj Q"));
        QCOMPARE(writeInOrder(list, { 0, 2, 3 }), QByteArray("q(a)Tj Q"));
        QCOMPARE(writeInOrder(list, { 0, 3 }), QByteArray("q Q"));
        QCOMPARE(writeInOrder(list, { 3 }), QByteArray(" Q"));
    }

    // An inline image: readers look for the end of its data as EI followed by a white space (a
    // delimiter after EI is correct by 7.2.3, but the data may contain "EI" too), so a new
    // neighbor, which begins with a delimiter, gets a line feed - a rule of the writer beyond
    // R-7. Its original neighbor is joined as it was.
    {
        const PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::InlineImage, " BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI"),
                                                          piece(Op::RestoreGraphicState, " Q"), piece(Op::SetLineDashPattern, "[]0 d") }, QByteArray("(a string, which does not end"));
        QCOMPARE(writeInOrder(list, { 0, 1, 2, 3 }), QByteArray("q BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI Q[]0 d(a string, which does not end"));
        QCOMPARE(writeInOrder(list, { 0, 1, 3, 2 }), QByteArray("q BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI\n[]0 d Q(a string, which does not end"));

        // The same for the tail, which follows the image after the operations between were deleted
        QCOMPARE(writeInOrder(list, { 0, 1 }), QByteArray("q BI /W 1 /H 1 /BPC 8 /CS /G ID a\nEI\n(a string, which does not end"));
    }

    // The tail is written after everything else, joined as it was to the last original operation
    // and by the same rule to any other one
    {
        const PDFContentOperationList numbers = createList({ piece(Op::PathFillWinding, "f"), piece(Op::SaveGraphicState, "\nq") }, QByteArray(" 1 2"));
        QCOMPARE(writeInOrder(numbers, { 0, 1 }), QByteArray("f\nq 1 2"));
        QCOMPARE(writeInOrder(numbers, { 1, 0 }), QByteArray("\nq\nf 1 2"));
        QCOMPARE(writeInOrder(numbers, { }), QByteArray(" 1 2"));

        const PDFContentOperationList comment = createList({ piece(Op::PathFillWinding, "f"), piece(Op::SaveGraphicState, "\nq") }, QByteArray("% c"));
        QCOMPARE(writeInOrder(comment, { 1, 0 }), QByteArray("\nq\nf% c"));

        // A content without any operation (operands without an operator) is the tail alone; the
        // new content of the editor goes before it, on lines of its own
        PDFContentOperationList operands = createList({ }, QByteArray("1 2 3"));
        const size_t save = operands.append(PDFContentOperation::createGenerated(Op::SaveGraphicState, QByteArray("q"), {}, {}));
        const size_t restore = operands.append(PDFContentOperation::createGenerated(Op::RestoreGraphicState, QByteArray("Q"), {}, {}));
        QCOMPARE(writeInOrder(operands, { }), QByteArray("1 2 3"));
        QCOMPARE(writeInOrder(operands, { save, restore }), QByteArray("q\nQ\n1 2 3"));
    }

    // Operations of the editor (canonical text: one operator a line) and copies between
    // original operations
    {
        PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::PaintXObject, " /Im1 Do"), piece(Op::RestoreGraphicState, " Q") }, QByteArray());
        const size_t matrix = list.append(PDFContentOperation::createGenerated(Op::AdjustCurrentTransformationMatrix, QByteArray("1 0 0 1 10 20 cm"), {}, {}));
        const size_t copy = list.append(PDFContentOperation::createCopy(list.getOperation(1)));
        QCOMPARE(matrix, size_t(3));
        QCOMPARE(copy, size_t(4));

        QCOMPARE(writeInOrder(list, { 0, matrix, 1, 2 }), QByteArray("q\n1 0 0 1 10 20 cm\n /Im1 Do Q"));
        QCOMPARE(writeInOrder(list, { 0, 1, copy, 2 }), QByteArray("q /Im1 Do\n /Im1 Do\n Q"));
        QCOMPARE(writeInOrder(list, { matrix, matrix }), QByteArray("1 0 0 1 10 20 cm\n1 0 0 1 10 20 cm"));
    }

    // No second line feed, where the original bytes bring one
    {
        PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::PaintXObject, "\n/Im1 Do"), piece(Op::RestoreGraphicState, "\r\nQ") }, QByteArray("\n"));
        const size_t matrix = list.append(PDFContentOperation::createGenerated(Op::AdjustCurrentTransformationMatrix, QByteArray("1 0 0 1 10 20 cm"), {}, {}));
        QCOMPARE(writeInOrder(list, { 0, matrix, 1, matrix, 2, matrix }), QByteArray("q\n1 0 0 1 10 20 cm\n/Im1 Do\n1 0 0 1 10 20 cm\r\nQ\n1 0 0 1 10 20 cm\n"));
    }
}

void ContentOperationsTest::writeSuppressedOperation()
{
    // R-1: the operations of a text, which set a property the wrap overrides, are suppressed
    // and not written; their neighbors become new neighbors (R-7)
    PDFContentOperationList list = createList({ piece(Op::TextBegin, "BT"), piece(Op::ColorSetDeviceRGBFilling, " 1 0 0 rg"), piece(Op::TextShowTextString, "(a)Tj"),
                                                piece(Op::ColorSetDeviceGrayFilling, " 0 g"), piece(Op::TextEnd, " ET") }, QByteArray());
    QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4 }), QByteArray("BT 1 0 0 rg(a)Tj 0 g ET"));

    list.setFlags(1, PDFContentOperation::Suppressed);
    QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4 }), QByteArray("BT(a)Tj 0 g ET"));

    list.setFlags(3, PDFContentOperation::Suppressed);
    QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4 }), QByteArray("BT(a)Tj ET"));

    // The first operation of the content suppressed: the second one begins the content
    list.setFlags(0, PDFContentOperation::Suppressed);
    QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4 }), QByteArray("(a)Tj ET"));

    // Restored again
    list.setFlags(0, PDFContentOperation::None);
    list.setFlags(1, PDFContentOperation::None);
    list.setFlags(3, PDFContentOperation::None);
    QCOMPARE(writeInOrder(list, { 0, 1, 2, 3, 4 }), QByteArray("BT 1 0 0 rg(a)Tj 0 g ET"));

    // A suppressed operation of the editor is not written either
    const size_t generated = list.append(PDFContentOperation::createGenerated(Op::ColorSetDeviceGrayFilling, QByteArray("0.5 g"), {}, {}));
    QCOMPARE(writeInOrder(list, { 0, generated, 2, 4 }), QByteArray("BT\n0.5 g\n(a)Tj ET"));
    list.setFlags(generated, PDFContentOperation::Flags(PDFContentOperation::Generated) | PDFContentOperation::Suppressed);
    QCOMPARE(writeInOrder(list, { 0, generated, 2, 4 }), QByteArray("BT(a)Tj ET"));
}

PDFContentContainer ContentOperationsTest::createWrapContainer()
{
    // "q 200 0 0 100 50 60 cm /Im1 Do Q": Root [ Group q [ StateRun cm, Image Do ] Q ]
    const PDFContentOperationList list = createList({ piece(Op::SaveGraphicState, "q"), piece(Op::AdjustCurrentTransformationMatrix, " 200 0 0 100 50 60 cm"),
                                                      piece(Op::PaintXObject, " /Im1 Do", { { Category::XObject, "Im1" } }), piece(Op::RestoreGraphicState, " Q") }, QByteArray());

    // The image is moved by (10, 20) of its own space, half transparent, and - to have every
    // group of the operators of a wrap once - with a red fill and the line width 2
    PDFContentStateOverride override;
    override.transform = QTransform(1, 0, 0, 1, 10, 20);
    override.properties[Property::AlphaFilling] = realValue(0.5);
    override.properties[Property::FillColor] = deviceColorValue("DeviceRGB", { 1.0, 0.0, 0.0 });
    override.properties[Property::LineWidth] = realValue(2.0);

    PDFContentNode::Data image = nodeData(Kind::Image, 4, 2, 1);
    image.override = override;
    image.state = createState(QTransform(200, 0, 0, 100, 50, 60));

    PDFContentNode::Data group = nodeData(Kind::Group, 2, 0, 1);
    group.suffix = PDFContentOperationSpan{ 3, 1 };

    const PDFContentResources resources = PDFContentResources::read(nullptr, dictionary({ { "XObject", dictionary({ { "Im1", reference(5) } }) } }), list);
    return createContainer(list, createRoot({ createNode(group, { createLeaf(Kind::StateRun, 3, 1, 1), createNode(image) }) }), resources);
}

QByteArray ContentOperationsTest::wrapContainerBytes()
{
    // R-1: q, the matrix, /GSn gs, the colors, the line parameters, the operations of the node,
    // Q - one operator a line; the bytes of the original operations as they were
    return QByteArray("q 200 0 0 100 50 60 cm\n"
                      "q\n"
                      "1 0 0 1 10 20 cm\n"
                      "/GS1 gs\n"
                      "1 0 0 rg\n"
                      "2 w\n"
                      " /Im1 Do\n"
                      "Q\n"
                      " Q");
}

void ContentOperationsTest::writeWrap()
{
    const PDFContentContainer container = createWrapContainer();
    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(container);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, wrapContainerBytes());

    // The ExtGState of the wrap: a direct dictionary with exactly the changed key - no /CA and
    // no /SMask (RE-2: a mask in effect must not be set again by it)
    QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "GS1" }));
    QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    QVERIFY(!result.resources.getEntries(Category::ExtGState)[0].isOriginal);
    QVERIFY(result.resources.isChanged());

    // What the container had stays (R-4), and the container itself is not changed by its writing
    QVERIFY(result.resources.getObject(Category::XObject, "Im1") == reference(5));
    QVERIFY(!container.getResources().isChanged());
    QVERIFY(container.getResources().getEntries(Category::ExtGState).empty());

    // An override, which is empty, still isolates the node (R-2: a node with a leak is always
    // wrapped at its new place)
    {
        PDFContentNode::Data image = nodeData(Kind::Image, 4, 2, 1);
        image.override = PDFContentStateOverride();
        PDFContentNode::Data group = nodeData(Kind::Group, 2, 0, 1);
        group.suffix = PDFContentOperationSpan{ 3, 1 };

        const PDFContentStreamWriteResult emptyResult = PDFContentStreamWriter::write(createContainer(container.getOperations(), createRoot({ createNode(group, { createLeaf(Kind::StateRun, 3, 1, 1), createNode(image) }) })));
        QVERIFY(emptyResult.isSuccess());
        QCOMPARE(emptyResult.content, QByteArray("q 200 0 0 100 50 60 cm\nq\n /Im1 Do\nQ\n Q"));
        QVERIFY(!emptyResult.resources.isChanged());
    }

    // Every wrappable kind gets the wrap (R-1: Path, Image, Shading, FormUse, TextObject); a text
    // object with its children inside
    {
        const PDFContentOperationList list = createList({ piece(Op::TextBegin, "BT"), piece(Op::TextSetFontAndFontSize, " /F1 12 Tf"), piece(Op::TextShowTextString, " (a) Tj"), piece(Op::TextEnd, " ET"),
                                                          piece(Op::ShadingPaintShape, " /Sh1 sh"), piece(Op::PaintXObject, " /Fm1 Do") }, QByteArray("\n"));
        PDFContentStateOverride override;
        override.properties[Property::FillColor] = deviceColorValue("DeviceGray", { 0.5 });

        PDFContentNode::Data text = nodeData(Kind::TextObject, 2, 0, 1);
        text.suffix = PDFContentOperationSpan{ 3, 1 };
        text.override = override;
        PDFContentNode::Data shading = nodeData(Kind::Shading, 5, 4, 1);
        shading.override = override;
        PDFContentNode::Data form = nodeData(Kind::FormUse, 6, 5, 1);
        form.override = override;

        const PDFContentNodePointer root = createRoot({ createNode(text, { createLeaf(Kind::StateRun, 3, 1, 1), createLeaf(Kind::TextShow, 4, 2, 1) }), createNode(shading), createNode(form) });
        const PDFContentStreamWriteResult kindsResult = PDFContentStreamWriter::write(createContainer(list, root));
        QVERIFY(kindsResult.isSuccess());
        QCOMPARE(kindsResult.content, QByteArray("q\n0.5 g\nBT /F1 12 Tf (a) Tj ET\nQ\n"
                                                 "q\n0.5 g\n /Sh1 sh\nQ\n"
                                                 "q\n0.5 g\n /Fm1 Do\nQ\n"));
    }
}

void ContentOperationsTest::writeWrapSharesExtGState()
{
    // Three images: two with the alpha 0.5, one with 0.25. zadani_jadro.md 4.8: an equal
    // dictionary is shared inside of the container.
    const PDFContentOperationList list = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::PaintXObject, " /Im2 Do"), piece(Op::PaintXObject, " /Im3 Do") }, QByteArray());

    std::vector<PDFContentNodePointer> images;
    const PDFReal alphas[] = { 0.5, 0.25, 0.5 };
    for (size_t i = 0; i < 3; ++i)
    {
        PDFContentNode::Data image = nodeData(Kind::Image, 2 + i, i, 1);
        image.override = PDFContentStateOverride();
        image.override->properties[Property::AlphaFilling] = realValue(alphas[i]);
        images.push_back(createNode(image));
    }
    const PDFContentNodePointer root = createRoot(images);

    const QByteArray expected("q\n/GS1 gs\n/Im1 Do\nQ\n"
                              "q\n/GS2 gs\n /Im2 Do\nQ\n"
                              "q\n/GS1 gs\n /Im3 Do\nQ");

    {
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, root));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, expected);
        QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "GS1", "GS2" }));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS2"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.25) } }));
    }

    // The names GS1 (defined) and GS2 (used by an operation without a definition) are taken:
    // the new dictionaries get GS3 and GS4, the original entry stays as it is
    {
        const PDFObject lineWidth = dictionary({ { "LW", PDFObject::createInteger(3) } });
        const PDFContentOperationList usedList = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::PaintXObject, " /Im2 Do"), piece(Op::PaintXObject, " /Im3 Do", { { Category::ExtGState, "GS2" } }) }, QByteArray());
        const PDFContentResources resources = PDFContentResources::read(nullptr, dictionary({ { "ExtGState", dictionary({ { "GS1", lineWidth } }) } }), usedList);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(usedList, root, resources));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/GS3 gs\n/Im1 Do\nQ\n"
                                            "q\n/GS4 gs\n /Im2 Do\nQ\n"
                                            "q\n/GS3 gs\n /Im3 Do\nQ"));
        QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "GS1", "GS3", "GS4" }));
        QVERIFY(result.resources.getObject(Category::ExtGState, "GS1") == lineWidth);
    }

    // A dictionary of the document, which sets the same key to the same value (its keys in
    // another order, without /Type), is used instead of a new one
    {
        const PDFObject sameAlpha = dictionary({ { "ca", PDFObject::createReal(0.25) } });
        const PDFContentResources resources = PDFContentResources::read(nullptr, dictionary({ { "ExtGState", dictionary({ { "Old", sameAlpha } }) } }), list);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, root, resources));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/GS1 gs\n/Im1 Do\nQ\n"
                                            "q\n/Old gs\n /Im2 Do\nQ\n"
                                            "q\n/GS1 gs\n /Im3 Do\nQ"));
        QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "Old", "GS1" }));
    }
}

void ContentOperationsTest::writeWrapSharesExtGStateOfMany()
{
    // A1L1-3. A container of 5 000 direct ExtGState dictionaries and 5 000 images, each with an
    // override of the alpha of filling (zadani_jadro.md 4.8: an equal dictionary is shared, the
    // order of the keys and /Type play no role):
    //   E1 ... E4998   << /CA k / 10000 >> (the alpha of stroking: equal to no wanted dictionary)
    //   E4999          << /Type /ExtGState /ca 0.5 >>
    //   E5000          << /ca 0.5 >> (equal to E4999)
    // The image 0 wants /ca 0.5: E4999, the first equal dictionary. The image i (1 ... 4998) wants
    // /ca i / 10000: a new dictionary GSi. The image 4999 wants /ca 1 / 10000 again: GS1. The
    // writing takes a time of about (images + entries) x log, not images x entries.
    constexpr int COUNT = 5000;

    std::vector<ContentPiece> pieces;
    std::vector<PDFContentNodePointer> images;
    for (int i = 0; i < COUNT; ++i)
    {
        pieces.push_back(piece(Op::PaintXObject, i == 0 ? "/Im1 Do" : " /Im1 Do", { { Category::XObject, "Im1" } }));

        PDFReal alpha = 0.5;
        if (i == COUNT - 1)
        {
            alpha = 1 / 10000.0;
        }
        else if (i > 0)
        {
            alpha = i / 10000.0;
        }

        PDFContentNode::Data image = nodeData(Kind::Image, quint64(2 + i), size_t(i), 1);
        image.override = PDFContentStateOverride();
        image.override->properties[Property::AlphaFilling] = realValue(alpha);
        images.push_back(createNode(image));
    }
    const PDFContentOperationList list = createList(pieces, QByteArray());

    PDFDictionaryBuilder graphicStates;
    for (int k = 1; k <= COUNT - 2; ++k)
    {
        graphicStates.addEntry(PDFInplaceOrMemoryString("E" + QByteArray::number(k)), dictionary({ { "CA", PDFObject::createReal(k / 10000.0) } }));
    }
    graphicStates.addEntry(PDFInplaceOrMemoryString("E4999"), dictionary({ { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    graphicStates.addEntry(PDFInplaceOrMemoryString("E5000"), dictionary({ { "ca", PDFObject::createReal(0.5) } }));
    PDFDictionaryBuilder resourcesDictionary;
    resourcesDictionary.addEntry(PDFInplaceOrMemoryString("ExtGState"), PDFObject::createDictionary(std::move(graphicStates)));
    const PDFContentResources resources = PDFContentResources::read(nullptr, PDFObject::createDictionary(std::move(resourcesDictionary)), list);

    QByteArray expected = "q\n/E4999 gs\n/Im1 Do\nQ";
    for (int i = 1; i < COUNT - 1; ++i)
    {
        expected += "\nq\n/GS" + QByteArray::number(i) + " gs\n /Im1 Do\nQ";
    }
    expected += "\nq\n/GS1 gs\n /Im1 Do\nQ";

    const PDFContentContainer container = createContainer(list, createRoot(images), resources);

    QElapsedTimer timer;
    timer.start();
    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(container);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(result.isSuccess());
    QVERIFY(result.content == expected);
    QVERIFY(result.content.startsWith("q\n/E4999 gs\n/Im1 Do\nQ\nq\n/GS1 gs\n /Im1 Do\nQ\nq\n/GS2 gs\n"));
    QVERIFY(result.content.endsWith("\nq\n/GS4998 gs\n /Im1 Do\nQ\nq\n/GS1 gs\n /Im1 Do\nQ"));

    QCOMPARE(result.resources.getEntries(Category::ExtGState).size(), size_t(COUNT + COUNT - 2));
    QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(1 / 10000.0) } }));
    QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS4998"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(4998 / 10000.0) } }));
    QVERIFY(result.resources.getObject(Category::ExtGState, "GS4999").isNull());

    const qint64 limit = qint64(2000) * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void ContentOperationsTest::writeWrapProperties()
{
    PDFLineDashPattern dash;
    dash.setDashArray({ 3.0, 1.5 });
    dash.setDashOffset(2.0);
    PDFContentStateValue dashValue;
    dashValue.kind = PDFContentStateValue::Kind::Dash;
    dashValue.dash = dash;

    PDFContentStateValue solidValue;
    solidValue.kind = PDFContentStateValue::Kind::Dash;

    PDFContentStateValue badDashValue = dashValue;
    badDashValue.dash.setDashOffset(qQNaN());

    // ISO 32000-2, 8.4.4 (Table 56: w J j M d ri i), 8.6.8 (Table 73: G g RG rg K k), 9.3.1
    // (Table 103: Tc Tw Tz TL Tr Ts): the operator of every property, which has one
    const std::vector<OperatorPropertyRow> rows =
    {
        { "line width", Property::LineWidth, realValue(2.5), "2.5 w" },
        { "line cap", Property::LineCap, integerValue(1), "1 J" },
        { "line join", Property::LineJoin, integerValue(2), "2 j" },
        { "miter limit", Property::MiterLimit, realValue(4.0), "4 M" },
        { "dash", Property::DashPattern, dashValue, "[3 1.5] 2 d" },
        { "solid line", Property::DashPattern, solidValue, "[] 0 d" },
        { "rendering intent", Property::RenderingIntent, nameValue("Perceptual"), "/Perceptual ri" },
        { "flatness", Property::Flatness, realValue(1.0), "1 i" },
        { "stroking gray", Property::StrokeColor, deviceColorValue("DeviceGray", { 0.5 }), "0.5 G" },
        { "filling gray", Property::FillColor, deviceColorValue("DeviceGray", { 0.5 }), "0.5 g" },
        { "stroking RGB", Property::StrokeColor, deviceColorValue("DeviceRGB", { 1.0, 0.5, 0.0 }), "1 0.5 0 RG" },
        { "filling RGB", Property::FillColor, deviceColorValue("DeviceRGB", { 1.0, 0.5, 0.0 }), "1 0.5 0 rg" },
        { "stroking CMYK", Property::StrokeColor, deviceColorValue("DeviceCMYK", { 0.0, 0.25, 0.5, 1.0 }), "0 0.25 0.5 1 K" },
        { "filling CMYK", Property::FillColor, deviceColorValue("DeviceCMYK", { 0.0, 0.25, 0.5, 1.0 }), "0 0.25 0.5 1 k" },
        { "character spacing", Property::CharacterSpacing, realValue(0.5), "0.5 Tc" },
        { "word spacing", Property::WordSpacing, realValue(-1.25), "-1.25 Tw" },
        { "horizontal scaling", Property::HorizontalScaling, realValue(90.0), "90 Tz" },
        { "leading", Property::Leading, realValue(14.0), "14 TL" },
        { "rendering mode", Property::TextRenderingMode, integerValue(2), "2 Tr" },
        { "rise", Property::TextRise, realValue(-3.0), "-3 Ts" },

        // What cannot be written is an error of the node (zadani_jadro.md 4.4: NaN and infinity)
        { "line width, not finite", Property::LineWidth, realValue(qQNaN()), "" },
        { "line width of a wrong kind", Property::LineWidth, integerValue(2), "" },
        { "line cap of a wrong kind", Property::LineCap, realValue(1.0), "" },
        { "dash, not finite", Property::DashPattern, badDashValue, "" },
        { "rendering intent without a name", Property::RenderingIntent, nameValue(""), "" },
        { "gray with three components", Property::FillColor, deviceColorValue("DeviceGray", { 1.0, 0.0, 0.0 }), "" },
        { "RGB with four components", Property::StrokeColor, deviceColorValue("DeviceRGB", { 1.0, 0.0, 0.0, 0.0 }), "" },
        { "CMYK with three components", Property::FillColor, deviceColorValue("DeviceCMYK", { 1.0, 0.0, 0.0 }), "" },
        { "unknown device space", Property::FillColor, deviceColorValue("DeviceN", { 1.0 }), "" },
        { "color, not finite", Property::FillColor, deviceColorValue("DeviceGray", { qInf() }), "" },
        { "color of a wrong kind", Property::FillColor, realValue(1.0), "" },
        { "color without a space", Property::FillColor, colorValue(PDFObject(), { 1.0 }, PDFObject()), "" },
        { "font without an object", Property::Font, realValue(12.0), "" }
    };

    for (const OperatorPropertyRow& row : rows)
    {
        PDFContentStateOverride override;
        override.properties[row.property] = row.value;

        const PDFContentStreamWriteResult result = writePathWithOverride(override);
        if (row.expectedLines.isEmpty())
        {
            // The container is not written at all; the error names the node
            QVERIFY2(!result.isSuccess(), row.title);
            QVERIFY2(result.content.isEmpty(), row.title);
            QVERIFY2(result.errors.size() == 1, row.title);
            QVERIFY2(result.errors[0].nodeId == PDFContentNodeId{ 2 }, row.title);
            QVERIFY2(!result.errors[0].text.isEmpty(), row.title);
            QVERIFY2(!result.errors[0].container.isValid(), row.title);
        }
        else
        {
            QVERIFY2(result.isSuccess(), row.title);
            const QByteArray expected = "q\n" + row.expectedLines + "\n0 0 10 10 re f\nQ";
            if (result.content != expected)
            {
                qWarning("%s", row.title);
            }
            QCOMPARE(result.content, expected);
            QVERIFY2(!result.resources.isChanged(), row.title);
        }
    }

    // More properties: the colors first (stroking, filling), then the line parameters and the
    // text state in the order of their operators w J j M d ri i, Tf Tc Tw Tz TL Tr Ts
    PDFContentStateOverride override;
    override.properties[Property::TextRise] = realValue(1.0);
    override.properties[Property::CharacterSpacing] = realValue(2.0);
    override.properties[Property::Flatness] = realValue(3.0);
    override.properties[Property::LineWidth] = realValue(4.0);
    override.properties[Property::FillColor] = deviceColorValue("DeviceGray", { 0.0 });
    override.properties[Property::StrokeColor] = deviceColorValue("DeviceGray", { 1.0 });
    override.properties[Property::LineJoin] = integerValue(1);

    const PDFContentStreamWriteResult result = writePathWithOverride(override);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, QByteArray("q\n1 G\n0 g\n4 w\n1 j\n3 i\n2 Tc\n1 Ts\n0 0 10 10 re f\nQ"));
}

void ContentOperationsTest::writeWrapExtGStateProperties()
{
    // The properties without an operator go into one ExtGState with exactly their keys, written
    // before the operators of the other properties
    PDFContentStateOverride override;
    override.properties[Property::AlphaStroking] = realValue(0.25);
    override.properties[Property::BlendMode] = nameValue("Multiply");
    override.properties[Property::TextKnockout] = booleanValue(false);
    override.properties[Property::Transfer] = objectValue(reference(13));
    override.properties[Property::BlackGeneration] = nameValue("Default");
    override.properties[Property::LineWidth] = realValue(2.0);

    const PDFContentStreamWriteResult result = writePathWithOverride(override);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, QByteArray("q\n/GS1 gs\n2 w\n0 0 10 10 re f\nQ"));
    QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "GS1" }));
    QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") },
                                                                                 { "TK", PDFObject::createBool(false) },
                                                                                 { "CA", PDFObject::createReal(0.25) },
                                                                                 { "BM", name("Multiply") },
                                                                                 { "BG2", name("Default") },
                                                                                 { "TR", reference(13) } }));

    // A value, which the dictionary cannot hold, is an error of the node
    PDFContentStateOverride badOverride;
    badOverride.properties[Property::AlphaFilling] = realValue(qQNaN());
    const PDFContentStreamWriteResult badResult = writePathWithOverride(badOverride);
    QVERIFY(!badResult.isSuccess());
    QVERIFY(badResult.content.isEmpty());
    QCOMPARE(badResult.errors.size(), size_t(1));
    QVERIFY(badResult.errors[0].nodeId == PDFContentNodeId{ 2 });
    QVERIFY(!badResult.resources.isChanged());
}

void ContentOperationsTest::writeWrapOverprint()
{
    // ISO 32000-2, Table 57, /OP: "Specifying an OP entry sets both parameters unless there is
    // also an op entry in the same graphics state parameter dictionary". An override of the
    // overprint of stroking alone must not change the one of filling: /op is written with the
    // value the state of the node has.
    for (const bool overprintFilling : { false, true })
    {
        PDFContentStateOverride override;
        override.properties[Property::OverprintStroking] = booleanValue(true);

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(QTransform(), overprintFilling));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/GS1 gs\n0 0 10 10 re f\nQ"));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") },
                                                                                     { "OP", PDFObject::createBool(true) },
                                                                                     { "op", PDFObject::createBool(overprintFilling) } }));
    }

    // Both given: as they are given, whatever the state says
    {
        PDFContentStateOverride override;
        override.properties[Property::OverprintStroking] = booleanValue(false);
        override.properties[Property::OverprintFilling] = booleanValue(true);

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(QTransform(), false));
        QVERIFY(result.isSuccess());
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") },
                                                                                     { "OP", PDFObject::createBool(false) },
                                                                                     { "op", PDFObject::createBool(true) } }));
    }

    // The overprint of filling alone needs nothing else
    {
        PDFContentStateOverride override;
        override.properties[Property::OverprintFilling] = booleanValue(true);

        const PDFContentStreamWriteResult result = writePathWithOverride(override);
        QVERIFY(result.isSuccess());
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "op", PDFObject::createBool(true) } }));
    }

    // Without the state of the node the other value is not known: an error, never a guess
    {
        PDFContentStateOverride override;
        override.properties[Property::OverprintStroking] = booleanValue(true);

        const PDFContentStreamWriteResult result = writePathWithOverride(override);
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });
    }
}

void ContentOperationsTest::writeWrapResourceNames()
{
    // The resources of the container: a font, a color space and a pattern of the document
    const PDFContentResources resources = PDFContentResources::read(nullptr, dictionary({ { "Font", dictionary({ { "F7", reference(21) } }) },
                                                                                          { "ColorSpace", dictionary({ { "CS1", reference(22) } }) },
                                                                                          { "Pattern", dictionary({ { "Old", reference(23) } }) } }), PDFContentOperationList());

    // 8.6.8: a color space with parameters is named by a resource; scn / SCN take every space
    // (sc / SC do not take ICCBased, Separation, DeviceN and Pattern). zadani_jadro.md 4.8: a
    // space of the document is used by its reference under a new name of the resources.
    {
        PDFContentStateOverride override;
        override.properties[Property::FillColor] = colorValue(reference(30), { 0.2, 0.4 }, PDFObject());
        override.properties[Property::StrokeColor] = colorValue(reference(22), { 1.0 }, PDFObject());
        override.properties[Property::Font] = objectValue(reference(31));
        override.properties[Property::Font].kind = PDFContentStateValue::Kind::Font;
        override.properties[Property::Font].real = 12.0;

        const PDFContentStreamWriteResult result = writePathWithOverride(override, nullptr, resources);
        QVERIFY(result.isSuccess());

        // The stroking space is a resource already (CS1); the filling one gets the next free
        // name CS2; the font gets F1 (F7 is another font)
        QCOMPARE(result.content, QByteArray("q\n/CS1 CS\n1 SCN\n/CS2 cs\n0.2 0.4 scn\n/F1 12 Tf\n0 0 10 10 re f\nQ"));
        QCOMPARE(entryNames(result.resources, Category::ColorSpace), (QList<QByteArray>{ "CS1", "CS2" }));
        QVERIFY(result.resources.getObject(Category::ColorSpace, "CS2") == reference(30));
        QCOMPARE(entryNames(result.resources, Category::Font), (QList<QByteArray>{ "F7", "F1" }));
        QVERIFY(result.resources.getObject(Category::Font, "F1") == reference(31));
    }

    // A font, which is a resource of the container already, keeps its name
    {
        PDFContentStateOverride override;
        override.properties[Property::Font] = objectValue(reference(21));
        override.properties[Property::Font].kind = PDFContentStateValue::Kind::Font;
        override.properties[Property::Font].real = 9.5;

        const PDFContentStreamWriteResult result = writePathWithOverride(override, nullptr, resources);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/F7 9.5 Tf\n0 0 10 10 re f\nQ"));
        QVERIFY(!result.resources.isChanged());
    }

    // Patterns (8.7.3.2, 8.7.3.3): a colored pattern in the space Pattern, an uncolored one in
    // [/Pattern base] with the components of its tint before the name. The pattern arrives as a
    // ready object (R-5: the copy with the new /Matrix is made by the command).
    {
        PDFContentStateOverride override;
        override.properties[Property::FillColor] = colorValue(PDFObject(), { }, reference(40));
        override.properties[Property::StrokeColor] = colorValue(reference(41), { 1.0, 0.0, 0.5 }, reference(23));

        const PDFContentStreamWriteResult result = writePathWithOverride(override, nullptr, resources);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/CS2 CS\n1 0 0.5 /Old SCN\n/Pattern cs\n/P1 scn\n0 0 10 10 re f\nQ"));
        QCOMPARE(entryNames(result.resources, Category::Pattern), (QList<QByteArray>{ "Old", "P1" }));
        QVERIFY(result.resources.getObject(Category::Pattern, "P1") == reference(40));
        QVERIFY(result.resources.getObject(Category::ColorSpace, "CS2") == reference(41));
    }

    // The four names of the spaces without parameters are operands themselves and never name
    // a resource (8.6.8, Table 73)
    {
        PDFContentStateOverride override;
        override.properties[Property::FillColor] = colorValue(name("Pattern"), { }, reference(23));
        override.properties[Property::StrokeColor] = colorValue(name("DeviceCMYK"), { 0.0, 0.0, 0.0, 1.0 }, PDFObject());

        const PDFContentStreamWriteResult result = writePathWithOverride(override, nullptr, resources);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/DeviceCMYK CS\n0 0 0 1 SCN\n/Pattern cs\n/Old scn\n0 0 10 10 re f\nQ"));
        QVERIFY(!result.resources.isChanged());
    }

    // A name of a resource is written as a name object (7.3.5): the ExtGState "A B" of the
    // document, which equals the dictionary of the override, is written with #20
    {
        const PDFContentResources spaceResources = PDFContentResources::read(nullptr, dictionary({ { "ExtGState", dictionary({ { "A B", dictionary({ { "ca", PDFObject::createReal(0.5) } }) } }) } }), PDFContentOperationList());

        PDFContentStateOverride override;
        override.properties[Property::AlphaFilling] = realValue(0.5);

        const PDFContentStreamWriteResult result = writePathWithOverride(override, nullptr, spaceResources);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/A#20B gs\n0 0 10 10 re f\nQ"));
    }
}

void ContentOperationsTest::writeSoftMask()
{
    // The example of the plan (zadani_overeni.md 8, R-6): "/GS1 gs q 200 0 0 100 50 60 cm /Im1 Do Q",
    // /GS1 sets a soft mask at the level of the page, where the matrix is the identity: E = I.
    // ISO 32000-2, 11.6.5.1: the space of the mask is the matrix of its group x the matrix of
    // the moment gs set it.
    const QTransform nodeMatrix(200, 0, 0, 100, 50, 60);
    const QTransform move(1, 0, 0, 1, 10, 20);

    // The image is moved by T = "1 0 0 1 10 20 cm" inside of its space: a point p goes to
    // (p + (10, 20)) x C = p x C + (2000, 2000), so in the space of the page it moves by
    // D = [1 0 0 1 2000 2000]. The mask has to move with it: its matrix must be E x D = D.
    // Inside of the wrap, after T cm, the matrix is T x C. X cm must give X x T x C = D:
    //   X = D x C^-1 x T^-1 = C^-1 (zadani_jadro.md 4.4: X = CTM_e x CTM^-1)
    //   C^-1 = [1/200 0 0 1/100 -50/200 -60/100] = [0.005 0 0 0.01 -0.25 -0.6], X^-1 = C
    {
        PDFContentStateOverride override;
        override.transform = move;
        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform(1, 0, 0, 1, 2000, 2000));

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(nodeMatrix));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n"
                                            "1 0 0 1 10 20 cm\n"
                                            "0.005 0 0 0.01 -0.25 -0.6 cm\n"
                                            "/GS1 gs\n"
                                            "200 0 0 100 50 60 cm\n"
                                            "0 0 10 10 re f\n"
                                            "Q"));

        // The mask is set by a dictionary with the single key /SMask, the mask by its reference
        QCOMPARE(entryNames(result.resources, Category::ExtGState), (QList<QByteArray>{ "GS1" }));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "SMask", reference(9) } }));
    }

    // Matrices, which do not commute (a wrong order of the multiplication shows): the node is
    // drawn with C = [0 1 -1 0 100 50] (a quarter turn and a move), the mask was set under
    // E = [2 0 0 2 10 20] and has to stay there (a moved node, whose mask is set again - R-2).
    //   C^-1: x = y' - 50, y = 100 - x', so C^-1 = [0 -1 1 0 -50 100]
    //   X = E x C^-1: (x, y) -> (2x + 10, 2y + 20) -> ((2y + 20) - 50, 100 - (2x + 10))
    //     = (2y - 30, -2x + 90), so X = [0 -2 2 0 -30 90]
    //   X^-1: x = (90 - y'') / 2, y = (x'' + 30) / 2, so X^-1 = [0 0.5 -0.5 0 45 15]
    // Other properties go into a dictionary of their own, after the mask.
    {
        PDFContentStateOverride override;
        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform(2, 0, 0, 2, 10, 20));
        override.properties[Property::AlphaFilling] = realValue(0.5);

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(QTransform(0, 1, -1, 0, 100, 50)));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n"
                                            "0 -2 2 0 -30 90 cm\n"
                                            "/GS1 gs\n"
                                            "0 0.5 -0.5 0 45 15 cm\n"
                                            "/GS2 gs\n"
                                            "0 0 10 10 re f\n"
                                            "Q"));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "SMask", reference(9) } }));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS2"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    }

    // The matrix of the mask is the matrix of this place of the wrap (T x C): X is the identity
    // and only gs is written. T x C = [200 0 0 100 50 + 10 * 200, 60 + 20 * 100] = [200 0 0 100 2050 2060]
    {
        PDFContentStateOverride override;
        override.transform = move;
        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform(200, 0, 0, 100, 2050, 2060));

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(nodeMatrix));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n1 0 0 1 10 20 cm\n/GS1 gs\n0 0 10 10 re f\nQ"));
    }

    // A change of the alpha under a mask: the dictionary has only /ca, the mask is not set again
    // and stays where it is
    {
        PDFContentStateOverride override;
        override.properties[Property::AlphaFilling] = realValue(0.5);

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(nodeMatrix));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n/GS1 gs\n0 0 10 10 re f\nQ"));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    }

    // No mask (the target of a move has a mask, the source has none): /SMask /None, which has
    // no space - no matrices around it, whatever the matrices are
    for (const PDFContentStateValue& value : { softMaskValue(PDFObject(), QTransform(5, 0, 0, 5, 1, 1)), softMaskValue(name("None"), QTransform()), nameValue("None") })
    {
        PDFContentStateOverride override;
        override.transform = move;
        override.properties[Property::SoftMask] = value;

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(nodeMatrix));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n1 0 0 1 10 20 cm\n/GS1 gs\n0 0 10 10 re f\nQ"));
        QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "SMask", name("None") } }));
    }

    // A matrix, which cannot be inverted (zadani_overeni.md 6.2: an error of the node): the
    // matrix of the node, the matrix of the mask
    {
        PDFContentStateOverride override;
        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform(2, 0, 0, 2, 10, 20));
        QVERIFY(!writePathWithOverride(override, createState(QTransform(1, 2, 2, 4, 0, 0))).isSuccess());

        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform(0, 0, 0, 0, 10, 20));
        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(nodeMatrix));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });
        QVERIFY(!result.resources.isChanged());

        override.properties[Property::SoftMask] = realValue(1.0);
        QVERIFY(!writePathWithOverride(override, createState(nodeMatrix)).isSuccess());
    }

    // A small matrix is still a matrix: two scales of 0.000001 (the determinant 1e-12) are
    // inverted. C = [0.000001 0 0 0.000001 0 0], E = I: X = C^-1 = [1000000 0 0 1000000 0 0]
    {
        PDFContentStateOverride override;
        override.properties[Property::SoftMask] = softMaskValue(reference(9), QTransform());

        const PDFContentStreamWriteResult result = writePathWithOverride(override, createState(QTransform(0.000001, 0, 0, 0.000001, 0, 0)));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n1000000 0 0 1000000 0 0 cm\n/GS1 gs\n0.000001 0 0 0.000001 0 0 cm\n0 0 10 10 re f\nQ"));
    }
}

void ContentOperationsTest::writeResidualStateRun()
{
    // R-3: when the leak of a node stops reaching the nodes after it (the text object between the
    // two images was deleted), a state run at the place of its end sets what leaked - here the
    // fill color and the font. It is written in place, without q / Q, so it stays in effect.
    const PDFContentOperationList list = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::PaintXObject, " /Im2 Do") }, QByteArray("\n"));

    PDFContentNode::Data run = nodeData(Kind::StateRun, 3, 0, 0);
    run.isModified = true;
    run.override = PDFContentStateOverride();
    run.override->properties[Property::FillColor] = deviceColorValue("DeviceRGB", { 1.0, 0.0, 0.0 });
    run.override->properties[Property::Font] = objectValue(reference(31));
    run.override->properties[Property::Font].kind = PDFContentStateValue::Kind::Font;
    run.override->properties[Property::Font].real = 12.0;
    run.override->properties[Property::AlphaFilling] = realValue(0.5);

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createLeaf(Kind::Image, 2, 0, 1), createNode(run), createLeaf(Kind::Image, 4, 1, 1) })));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, QByteArray("/Im1 Do\n/GS1 gs\n1 0 0 rg\n/F1 12 Tf\n /Im2 Do\n"));
    QVERIFY(hasExactly(result.resources.getObject(Category::ExtGState, "GS1"), { { "Type", name("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }));
    QVERIFY(result.resources.getObject(Category::Font, "F1") == reference(31));

    // A state run with operations of its own and an override: the operators of the override
    // follow its operations
    const PDFContentOperationList runList = createList({ piece(Op::SetLineWidth, "2 w"), piece(Op::PaintXObject, " /Im2 Do") }, QByteArray());
    PDFContentNode::Data ownRun = nodeData(Kind::StateRun, 2, 0, 1);
    ownRun.override = PDFContentStateOverride();
    ownRun.override->properties[Property::LineCap] = integerValue(1);

    const PDFContentStreamWriteResult ownResult = PDFContentStreamWriter::write(createContainer(runList, createRoot({ createNode(ownRun), createLeaf(Kind::Image, 3, 1, 1) })));
    QVERIFY(ownResult.isSuccess());
    QCOMPARE(ownResult.content, QByteArray("2 w\n1 J\n /Im2 Do"));
}

void ContentOperationsTest::writeClippingPath()
{
    // RE-4: a clipping path "re W n" is never wrapped (Q would end its clip for the image after
    // it); a move rewrites its geometry canonically in place
    const PDFContentOperationList list = createList({ piece(Op::Rectangle, "10 20 30 40 re"), piece(Op::ClipWinding, " W"), piece(Op::PathClear, " n"),
                                                      piece(Op::PaintXObject, " /Im1 Do") }, QByteArray());

    PDFContentNode::Data clip = nodeData(Kind::Path, 2, 0, 3);
    clip.hasClipEffect = true;
    clip.paint.clip = PDFContentPathPaint::Clip::NonZero;
    clip.path.addRectangle(QRectF(10, 20, 30, 40));
    const PDFContentNodePointer image = createLeaf(Kind::Image, 3, 3, 1);

    // The point p of the wrap "M cm" is the point p x M: a move by (5, 7) gives the rectangle
    // at (15, 27) with the same size; the properties of an override have no effect on "W n"
    {
        PDFContentNode::Data data = clip;
        data.override = PDFContentStateOverride();
        data.override->transform = QTransform(1, 0, 0, 1, 5, 7);
        data.override->properties[Property::FillColor] = deviceColorValue("DeviceGray", { 0.5 });

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data), image })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("15 27 30 40 re\nW n\n /Im1 Do"));
        QVERIFY(!result.resources.isChanged());
    }

    // A quarter turn M = [0 1 -1 0 0 0]: (x, y) -> (-y, x). The corners (10, 20), (40, 20),
    // (40, 60), (10, 60) go to (-20, 10), (-20, 40), (-60, 40), (-60, 10); the first side is not
    // horizontal anymore, which "re" cannot say. The even-odd rule stays "W*".
    {
        PDFContentNode::Data data = clip;
        data.paint.clip = PDFContentPathPaint::Clip::EvenOdd;
        data.override = PDFContentStateOverride();
        data.override->transform = QTransform(0, 1, -1, 0, 0, 0);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data), image })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("-20 10 m\n-20 40 l\n-60 40 l\n-60 10 l\nh\nW* n\n /Im1 Do"));
    }

    // An override, which is empty (R-2: the wrap of a node with a leak - "the exception are the
    // nodes with a clipping effect"): the node is written as it is
    {
        PDFContentNode::Data data = clip;
        data.override = PDFContentStateOverride();

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data), image })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("10 20 30 40 re W n /Im1 Do"));
    }

    // A matrix, which cannot be inverted, is an error of the node
    {
        PDFContentNode::Data data = clip;
        data.override = PDFContentStateOverride();
        data.override->transform = QTransform(0, 0, 0, 0, 5, 7);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data), image })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });
    }
}

void ContentOperationsTest::writePaintingAndClippingPath()
{
    // zadani_jadro.md 4.4, R-1: a path, which paints and clips ("re W f"), is split into
    // "q <override> path painting Q" and the path with "W n" behind it, with its geometry
    // recomputed in place - the image after it stays clipped
    const PDFContentOperationList list = createList({ piece(Op::Rectangle, "10 20 30 40 re"), piece(Op::ClipWinding, " W"), piece(Op::PathFillWinding, " f"),
                                                      piece(Op::PaintXObject, " /Im1 Do") }, QByteArray());

    PDFContentNode::Data path = nodeData(Kind::Path, 2, 0, 3);
    path.hasClipEffect = true;
    path.paint.fill = true;
    path.paint.clip = PDFContentPathPaint::Clip::NonZero;
    path.path.addRectangle(QRectF(10, 20, 30, 40));
    path.override = PDFContentStateOverride();
    path.override->transform = QTransform(1, 0, 0, 1, 5, 7);
    path.override->properties[Property::FillColor] = deviceColorValue("DeviceRGB", { 1.0, 0.0, 0.0 });

    const PDFContentNodePointer image = createLeaf(Kind::Image, 3, 3, 1);

    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(path), image })));
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, QByteArray("q\n"
                                        "1 0 0 1 5 7 cm\n"
                                        "1 0 0 rg\n"
                                        "10 20 30 40 re\n"
                                        "f\n"
                                        "Q\n"
                                        "15 27 30 40 re\n"
                                        "W n\n"
                                        " /Im1 Do"));

    // Only a new color: the clip keeps its geometry. Stroked, filled by the even-odd rule and
    // clipped by it: "B*" inside, "W* n" behind
    path.override->transform = QTransform();
    path.paint.stroke = true;
    path.paint.fillEvenOdd = true;
    path.paint.clip = PDFContentPathPaint::Clip::EvenOdd;

    const PDFContentStreamWriteResult colorResult = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(path), image })));
    QVERIFY(colorResult.isSuccess());
    QCOMPARE(colorResult.content, QByteArray("q\n1 0 0 rg\n10 20 30 40 re\nB*\nQ\n10 20 30 40 re\nW* n\n /Im1 Do"));
}

void ContentOperationsTest::writeNodesWithoutWrap()
{
    // A text with a rendering mode 4 to 7 adds to the clipping path at ET (ISO 32000-2, 9.3.6):
    // it is never wrapped. Its canonical rewrite is stage E2: until then an override, which is
    // not empty, is an error of the node (zadani_jadro.md 4.4); an empty one changes nothing.
    const PDFContentOperationList list = createList({ piece(Op::TextBegin, "BT"), piece(Op::TextSetRenderMode, " 7 Tr"), piece(Op::TextShowTextString, " (a) Tj"),
                                                      piece(Op::TextEnd, " ET"), piece(Op::PaintXObject, " /Im1 Do") }, QByteArray());

    PDFContentNode::Data text = nodeData(Kind::TextObject, 2, 0, 1);
    text.suffix = PDFContentOperationSpan{ 3, 1 };
    text.hasClipEffect = true;
    text.clipsByText = true;
    const std::vector<PDFContentNodePointer> textChildren = { createLeaf(Kind::StateRun, 3, 1, 1), createLeaf(Kind::TextShow, 4, 2, 1) };
    const PDFContentNodePointer image = createLeaf(Kind::Image, 5, 4, 1);

    {
        PDFContentNode::Data data = text;
        data.override = PDFContentStateOverride();
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data, textChildren), image })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("BT 7 Tr (a) Tj ET /Im1 Do"));
    }

    {
        PDFContentNode::Data data = text;
        data.override = PDFContentStateOverride();
        data.override->properties[Property::FillColor] = deviceColorValue("DeviceGray", { 0.5 });
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data, textChildren), image })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });
    }

    // The same text without the clipping effect is wrapped
    {
        PDFContentNode::Data data = text;
        data.hasClipEffect = false;
        data.clipsByText = false;
        data.override = PDFContentStateOverride();
        data.override->properties[Property::FillColor] = deviceColorValue("DeviceGray", { 0.5 });
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data, textChildren), image })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("q\n0.5 g\nBT 7 Tr (a) Tj ET\nQ\n /Im1 Do"));
    }

    // R-1: containers and the other kinds have no wrap - an override there is an error of the
    // node (a command gives the change to the leaves), also an empty one
    for (const Kind kind : { Kind::Group, Kind::MarkedContent, Kind::MarkedPoint, Kind::TextShow, Kind::Opaque, Kind::Root })
    {
        PDFContentNode::Data data = nodeData(kind, 7, 4, 1);
        data.override = PDFContentStateOverride();

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(data) })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 7 });
    }
}

void ContentOperationsTest::writeModifiedPath()
{
    // A path changed in its geometry (or a new one) is written canonically from its data: its
    // old operations "0 0 10 10 re f" are not written
    const PDFContentOperationList list = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::Rectangle, " 0 0 10 10 re"), piece(Op::PathFillWinding, " f"),
                                                      piece(Op::PaintXObject, " /Im2 Do") }, QByteArray());

    PDFContentNode::Data path = nodeData(Kind::Path, 3, 1, 2);
    path.isModified = true;
    path.path.moveTo(QPointF(0, 0));
    path.path.lineTo(QPointF(10, 0));
    path.path.lineTo(QPointF(5, 8));
    path.path.close();
    path.paint.stroke = true;
    path.paint.fill = true;
    path.paint.fillEvenOdd = true;

    const PDFContentNodePointer first = createLeaf(Kind::Image, 2, 0, 1);
    const PDFContentNodePointer last = createLeaf(Kind::Image, 4, 3, 1);

    {
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ first, createNode(path), last })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("/Im1 Do\n0 0 m\n10 0 l\n5 8 l\nh\nB*\n /Im2 Do"));
    }

    // A new path has no operations at all; with an override it is wrapped as every path
    {
        PDFContentNode::Data newPath = path;
        newPath.prefix = PDFContentOperationSpan();
        newPath.override = PDFContentStateOverride();
        newPath.override->properties[Property::LineWidth] = realValue(3.0);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ first, createNode(newPath), last })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("/Im1 Do\nq\n3 w\n0 0 m\n10 0 l\n5 8 l\nh\nB*\nQ\n /Im2 Do"));
    }

    // Without the flag the operations of the node are its bytes, whatever its data say
    {
        PDFContentNode::Data unchanged = path;
        unchanged.isModified = false;

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ first, createNode(unchanged), last })));
        QVERIFY(result.isSuccess());
        QCOMPARE(result.content, QByteArray("/Im1 Do 0 0 10 10 re f /Im2 Do"));
    }

    // A coordinate, which is not finite: an error of the node, nothing is written
    {
        PDFContentNode::Data broken = path;
        broken.path.lineTo(QPointF(qInf(), 0));

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ first, createNode(broken), last })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 3 });
        QVERIFY(!result.errors[0].text.isEmpty());
    }
}

void ContentOperationsTest::writeErrors()
{
    const PDFContentOperationList list = createList({ piece(Op::PaintXObject, "/Im1 Do"), piece(Op::PaintXObject, " /Im2 Do") }, QByteArray());

    // A container with a stream, which cannot be decoded, is never written (zadani_jadro.md 4.3:
    // the joined rewrite would drop that stream)
    {
        PDFContentContainer::Data data;
        data.key = PDFContentContainerKey::createPage(0, PDFObjectReference(1, 0));
        data.operations = list;
        data.root = createRoot({ createLeaf(Kind::Image, 2, 0, 1) });
        data.isReadOnly = true;

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(PDFContentContainer(std::move(data)));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(!result.errors[0].nodeId.isValid());
        QVERIFY(!result.errors[0].text.isEmpty());
    }

    // A container without a tree has nothing to write: an error, never an empty content, which
    // would replace the page
    {
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, PDFContentNodePointer()));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
    }

    // A node, which names operations the list does not have (also by a count, which overflows)
    for (const PDFContentOperationSpan& span : { PDFContentOperationSpan{ 1, 2 }, PDFContentOperationSpan{ 3, 1 }, PDFContentOperationSpan{ std::numeric_limits<size_t>::max(), 2 }, PDFContentOperationSpan{ 1, std::numeric_limits<size_t>::max() } })
    {
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createLeaf(Kind::Opaque, 2, span.first, span.count) })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });

        // The same span as a suffix
        PDFContentNode::Data group = nodeData(Kind::Group, 3, 0, 1);
        group.suffix = span;
        QVERIFY(!PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(group) }))).isSuccess());
    }

    // An empty span at the end of the list names nothing and is fine
    QVERIFY(PDFContentStreamWriter::write(createContainer(list, createRoot({ createLeaf(Kind::Opaque, 2, 2, 0) }))).isSuccess());

    // Every node with an error is reported (the GUI selects them); the container is not written
    // at all - no bytes and none of the names, which the nodes without an error would have got
    {
        PDFContentNode::Data good = nodeData(Kind::Image, 2, 0, 1);
        good.override = PDFContentStateOverride();
        good.override->properties[Property::AlphaFilling] = realValue(0.5);

        PDFContentNode::Data bad = nodeData(Kind::Image, 3, 1, 1);
        bad.override = PDFContentStateOverride();
        bad.override->properties[Property::LineWidth] = realValue(qQNaN());

        PDFContentNode::Data worse = nodeData(Kind::Image, 4, 1, 1);
        worse.override = PDFContentStateOverride();
        worse.override->transform = QTransform(1, 0, 0, 1, qInf(), 0);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(good), createNode(bad), createNode(worse) })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(2));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 3 });
        QVERIFY(result.errors[1].nodeId == PDFContentNodeId{ 4 });
        QVERIFY(!result.resources.isChanged());
        QVERIFY(result.resources.getEntries(Category::ExtGState).empty());

        // Without the two the first one is written
        const PDFContentStreamWriteResult goodResult = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(good) })));
        QVERIFY(goodResult.isSuccess());
        QCOMPARE(goodResult.content, QByteArray("q\n/GS1 gs\n/Im1 Do\nQ"));
    }

    // A matrix of a wrap, which cannot be inverted (zadani_overeni.md 6.2)
    {
        PDFContentNode::Data singular = nodeData(Kind::Image, 2, 0, 1);
        singular.override = PDFContentStateOverride();
        singular.override->transform = QTransform(1, 1, 1, 1, 0, 0);

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(createContainer(list, createRoot({ createNode(singular) })));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.content.isEmpty());
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(result.errors[0].nodeId == PDFContentNodeId{ 2 });
    }
}

void ContentOperationsTest::writeInThreads()
{
    // The writer is static and reentrant and reads an immutable container: more workers write
    // the same container at once (a preview and the run of the tool)
    const PDFContentContainer container = createWrapContainer();
    const QByteArray expected = wrapContainerBytes();

    const int threadCount = 8;
    std::atomic<int> failures{ 0 };
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([&container, &expected, &failures]()
        {
            for (int round = 0; round < 200; ++round)
            {
                const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(container);
                if (!result.isSuccess() || result.content != expected ||
                    result.resources.getEntries(PDFContentResourceCategory::ExtGState).size() != 1 ||
                    result.resources.getEntries(PDFContentResourceCategory::ExtGState)[0].name != "GS1")
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
    QVERIFY(!container.getResources().isChanged());
}

// -------------------------------------------------------------------------------------------
//  From a document to its bytes
// -------------------------------------------------------------------------------------------

PDFContentBuildResult ContentOperationsTest::buildPage(const PDFDocument& document)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFContentBuildContext context;
    context.fontCache = &fontCache;
    context.cms = &cms;

    const PDFPage* page = document.getCatalog()->getPage(0);
    const PDFContentContainerKey key = PDFContentContainerKey::createPage(0, page->getPageReference());
    return PDFContentTreeBuilder::build(&document, key, nullptr, context, PDFContentBuildOptions(), nullptr, nullptr);
}

void ContentOperationsTest::builtContainerIsWrittenByteForByte_data()
{
    QTest::addColumn<QList<QByteArray>>("streams");

    // zadani_overeni.md 8: comments, white space, hexadecimal and literal strings, #xx, the
    // numbers +.5 and 1.
    QTest::newRow("comments, strings, names, numbers")
            << QList<QByteArray>{ "% first comment\n q\t1 0 0 1 10 20 cm % a comment with an operator Q\r\n+.5 1. -.0 00.50 re f*\n"
                                  "BT /F#31 12 Tf (a \\) unbalanced \\( and (balanced) \\\\)Tj<48 65 6C6c6F>Tj[(A)-120.5<42>]TJ ET\fQ \n" };

    // An operation over the boundary of two streams (7.8.2)
    QTest::newRow("operation over two streams")
            << QList<QByteArray>{ "q 1 0 0 1", "5 5 cm 0 0 10 10 re f", "Q" };

    // An inline image with EI in its data (ContentTestDocuments::inlineImagePageContent)
    QTest::newRow("inline image with EI in the data")
            << QList<QByteArray>{ "q 20 0 0 20 10 10 cm\nBI /W 2 /H 2 /BPC 8 /CS /G ID aEIb\nEI\nQ\n0 0 5 5 re f\n" };

    // What the builder does not understand is written as it is: an unknown operator, crossed
    // brackets, a string, which does not end
    QTest::newRow("opaque content")
            << QList<QByteArray>{ "1 2 unknown q /Tag BMC Q EMC 0 0 5 5 re f (a string, which does not end" };

    QTest::newRow("empty content")
            << QList<QByteArray>{ "" };
}

void ContentOperationsTest::builtContainerIsWrittenByteForByte()
{
    QFETCH(QList<QByteArray>, streams);

    const PDFDocument document = ContentTestDocuments::createDocument(streams, [](PDFDocumentBuilder* builder)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), ContentTestDocuments::createDictionaryObject({ { "F1", ContentTestDocuments::addSimpleFontObject(builder) } }));
        return resources;
    });

    const PDFContentBuildResult buildResult = buildPage(document);
    QVERIFY(buildResult.container);
    QVERIFY(!buildResult.container->isReadOnly());

    // ISO 32000-2, 7.8.2: the streams of /Contents are one content; the editor joins them by
    // one line feed (R-7)
    const QByteArray content = streams.join('\n');
    QCOMPARE(buildResult.container->getOperations().getOriginalContent(), content);

    // KE.21: a container nobody touched gives its original bytes
    const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(*buildResult.container);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.content, content);
    QVERIFY(!result.resources.isChanged());
}

void ContentOperationsTest::builtContainerResources()
{
    // The inline image of the document names its color space /Cs1 of the resources (/CS /Cs1):
    // the name is a resource name of the operation, and the entry is defined
    const PDFDocument document = ContentTestDocuments::createInlineImageDocument(ContentTestDocuments::InlineImageVariant::NamedColorSpace);
    const PDFContentBuildResult buildResult = buildPage(document);
    QVERIFY(buildResult.container);

    const PDFContentResources& resources = buildResult.container->getResources();
    QCOMPARE(entryNames(resources, Category::ColorSpace), (QList<QByteArray>{ "Cs1" }));
    QVERIFY(resources.getEntries(Category::ColorSpace)[0].isOriginal);
    QVERIFY(resources.getUndefinedNames(Category::ColorSpace).empty());

    bool isNamedByInlineImage = false;
    const PDFContentOperationList& operations = buildResult.container->getOperations();
    for (size_t i = 0; i < operations.getCount(); ++i)
    {
        const PDFContentOperation& operation = operations.getOperation(i);
        if (operation.getOperator() == Op::InlineImage)
        {
            isNamedByInlineImage = operation.getResourceNames() == std::vector<PDFContentResourceName>{ PDFContentResourceName{ Category::ColorSpace, QByteArrayLiteral("Cs1") } };
        }
    }
    QVERIFY(isNamedByInlineImage);

    // R-4: the entry cannot be removed, and a new color space gets another name
    PDFContentResources changed = resources;
    QVERIFY(!changed.removeAdded(Category::ColorSpace, "Cs1"));
    QCOMPARE(changed.allocateName(Category::ColorSpace, "Cs"), QByteArray("Cs2"));
}

QTEST_MAIN(ContentOperationsTest)

#include "tst_contentoperationstest.moc"
