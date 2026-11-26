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

#ifndef PDFCONTENTNODE_H
#define PDFCONTENTNODE_H

#include "pdfcontentoperation.h"
#include "pdfcontentresources.h"
#include "pdfcontentstate.h"
#include "pdfcontenttextlayout.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <compare>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

// Edit Content, stage E0, package P02 (tree and state). Plan: zadani_jadro.md 4.2 (model), 4.3
// (identifiers, what is opaque), zadani_overeni.md 6.2 (limits), 6.3 (threads). After stage E0
// this file is changed only by the coordinator.

// Threads and ownership of the plain types of this header (PDFContentNodeId,
// PDFContentContainerKey, PDFContentOperationSpan, PDFContentPathPaint, PDFContentIssue): values,
// which point to nothing; copied freely, also between threads. PDFContentNode::Data and
// PDFContentContainer::Data are values too: they co-own what they refer to (shared pointers to
// immutable states and nodes, the fonts of the text state, PDFObject values) and are changed only
// before a node or a container is created from them.

/// Identifier of a node inside of its container. The tree builder numbers the nodes by a depth
/// first walk from 1 (the root), so the same document gives the same numbers; a node created by
/// a command gets a number of the counter of the session (PDFContentEditSession::allocateNodeId),
/// which starts at PDFContentEditSession::FIRST_SESSION_NODE_ID (2^32): the numbers of the builder
/// are always below it (PDFContentContainer::MAXIMUM_OPERATION_COUNT bounds the nodes of a
/// container), whatever is loaded later. An identifier is valid only together with the
/// fingerprint of its container.
struct PDFContentNodeId
{
    quint64 value = 0;  ///< 0 = no node

    bool isValid() const { return value != 0; }
    auto operator<=>(const PDFContentNodeId&) const = default;
};

/// Key of a container: an independent sequence of operators
struct PDFContentContainerKey
{
    enum class Kind
    {
        Page,           ///< All streams of /Contents of a page
        Form,           ///< The stream of a form XObject (shared by all its uses)
        PatternCell     ///< The stream of a tiling pattern
    };

    Kind kind = Kind::Page;
    PDFInteger pageIndex = -1;      ///< Page: the index of the page; -1 otherwise
    PDFObjectReference reference;   ///< Page: the page object; Form: the form XObject; PatternCell: the pattern

    static PDFContentContainerKey createPage(PDFInteger pageIndex, PDFObjectReference pageReference) { return PDFContentContainerKey{ Kind::Page, pageIndex, pageReference }; }
    static PDFContentContainerKey createForm(PDFObjectReference formReference) { return PDFContentContainerKey{ Kind::Form, -1, formReference }; }
    static PDFContentContainerKey createPatternCell(PDFObjectReference patternReference) { return PDFContentContainerKey{ Kind::PatternCell, -1, patternReference }; }

    bool isValid() const { return reference.isValid() && (kind != Kind::Page || pageIndex >= 0); }
    std::strong_ordering operator<=>(const PDFContentContainerKey&) const = default;
};

/// Kind of a node (zadani_jadro.md 4.2)
enum class PDFContentNodeKind
{
    Root,           ///< The whole container
    Group,          ///< q ... Q; a chain q q q ... Q Q Q without operations between is one node (saveDepth)
    MarkedContent,  ///< BMC / BDC ... EMC
    MarkedPoint,    ///< MP, DP (a leaf)
    TextObject,     ///< BT ... ET; children: state runs, TextShow, nested MarkedContent
    TextShow,       ///< Tj, TJ, ', " (a leaf inside of a TextObject)
    Path,           ///< Path construction up to the painting operator, W / W* included (a leaf)
    Image,          ///< Do of an image XObject, or an inline image (a leaf)
    FormUse,        ///< Do of a form XObject (a leaf of this tree; the form is another container)
    Shading,        ///< sh (a leaf)
    StateRun,       ///< Consecutive state operators outside of objects (a leaf; never moved)
    Opaque          ///< What the builder does not understand or what exceeds a limit (a leaf; written unchanged, never edited, moved or deleted alone)
};

/// A span of operations of the operation list of the container (PDFContentOperationList)
struct PDFContentOperationSpan
{
    size_t first = 0;
    size_t count = 0;

    bool isEmpty() const { return count == 0; }
    bool operator==(const PDFContentOperationSpan&) const = default;
};

/// A path as plain data (never a QPainterPath: a painter path must not be used by more threads).
/// Coordinates are of the space of the node (before its current transformation matrix).
///
/// Threads and ownership: a plain immutable value, shared between threads freely.
class PDF4QTLIBCORESHARED_EXPORT PDFContentPath
{
public:
    enum class SegmentType
    {
        MoveTo,     ///< One point
        LineTo,     ///< One point
        CurveTo,    ///< Three points: two control points and the end point
        Close       ///< No point
    };

    /// One segment of a path (a plain value)
    struct Segment
    {
        SegmentType type = SegmentType::MoveTo;
        QPointF points[3];
    };

    PDFContentPath() = default;

    const std::vector<Segment>& getSegments() const { return m_segments; }
    bool isEmpty() const { return m_segments.empty(); }

    void moveTo(QPointF point);
    void lineTo(QPointF point);
    void curveTo(QPointF control1, QPointF control2, QPointF point);
    void close();

    /// Appends a rectangle as the operator re does: m, three l, h (ISO 32000-2, 8.5.2.1). The
    /// rectangle is never normalized: a negative width or height is kept (the direction of the
    /// path matters for the nonzero winding number rule).
    void addRectangle(const QRectF& rectangle);

    /// The path is exactly one rectangle of addRectangle (it stays "re" when it is written);
    /// \p rectangle gets it as it was added, not normalized (a negative width or height is kept)
    bool isRectangle(QRectF* rectangle = nullptr) const;

    /// The box of all points of the path (control points included); empty for an empty path
    QRectF getBoundingBox() const;

    /// The path with every point mapped by a matrix
    PDFContentPath transformed(const QTransform& matrix) const;

private:
    std::vector<Segment> m_segments;

    /// The rectangle, when the path is exactly one rectangle of addRectangle (the operands of
    /// "re" as they were given: the width and the height are not computed back from the points)
    std::optional<QRectF> m_rectangle;
};

/// How a path is painted
struct PDFContentPathPaint
{
    enum class Clip
    {
        None,
        NonZero,    ///< W
        EvenOdd     ///< W*
    };

    bool stroke = false;
    bool fill = false;
    bool fillEvenOdd = false;   ///< f*, B*, b*
    bool closeBeforePaint = false;  ///< s, b, b*
    Clip clip = Clip::None;

    /// Only "n" with a clip: a clipping path, which paints nothing
    bool isClippingPath() const { return clip != Clip::None && !stroke && !fill; }
};

class PDFContentNode;
using PDFContentNodePointer = std::shared_ptr<const PDFContentNode>;

/// A node of the content tree: a view over operations of the container, or new content.
///
/// A node owns its opening operations (\p getPrefix: q of a Group, BDC of a MarkedContent, BT of a
/// TextObject; all operations of a leaf) and its closing operations (\p getSuffix: Q, EMC, ET;
/// empty for a leaf and for a node, whose end is missing in the content - an unclosed node,
/// PDFContentContainer::isUnclosed). Its children are written between them. The order of writing
/// is the order of the tree.
///
/// Zoom, selection, the lock and the "hidden in the editor" flag are not in the node: they are
/// the state of the session (PDFContentEditSession), outside of undo.
///
/// Threads and ownership: immutable; owned by shared pointers of its parent and of every container
/// version, which contains it (a command creates new nodes only on the path from the changed
/// node to the root). It can be read by any thread. It co-owns its states.
class PDF4QTLIBCORESHARED_EXPORT PDFContentNode
{
public:
    /// The data of a node; a plain value used to create a node (PDFContentContainerEditor) and
    /// returned by getData. Fields of other kinds than the kind of the node are ignored.
    struct Data
    {
        PDFContentNodeKind kind = PDFContentNodeKind::Opaque;
        PDFContentNodeId id;
        PDFContentNodeId parentId;                      ///< Not valid for the root
        PDFContentOperationSpan prefix;
        PDFContentOperationSpan suffix;
        QRectF boundingBox;                             ///< In the default space of the container; empty, when unknown or nothing is painted
        PDFContentResolvedStatePointer state;           ///< State at the beginning of the node
        PDFContentSymbolicStatePointer symbolicState;
        PDFContentStateLeak leak;
        std::optional<PDFContentStateOverride> override;
        bool isModified = false;                        ///< Changed by the editor: written canonically
        bool hasClipEffect = false;                     ///< A clipping path, a path with W, a text with a rendering mode 4 to 7: never wrapped (R-1)

        // Group
        int saveDepth = 0;                              ///< Number of q of the chain (>= 1)

        // MarkedContent, MarkedPoint
        QByteArray tag;
        PDFObject properties;                           ///< The property list: a dictionary, or the object of the name of /Properties; null for BMC / MP
        QByteArray propertiesName;                      ///< The name of /Properties, when the operand is a name
        PDFObjectReference optionalContent;             ///< The OCG or OCMD of a tag OC
        std::optional<PDFInteger> mcid;                 ///< /MCID of the properties
        bool isArtifact = false;
        std::optional<QString> actualText;              ///< /ActualText of the properties

        // TextObject
        bool clipsByText = false;                       ///< Some part has a rendering mode 4 to 7

        // TextShow
        PDFContentTextState textState;                  ///< The text state of the show operator; it co-owns the font and the realized font, whose outlines the items point to
        std::vector<PDFContentTextItem> textItems;      ///< Glyphs and spacers in the order of the string; empty, when the builder was asked not to lay out text (PDFContentBuildOptions::layoutText)
        bool isTextComplete = true;                     ///< false: a glyph without a width in the font dictionary - the text is movable as a whole only

        // Path
        PDFContentPath path;
        PDFContentPathPaint paint;

        // Image
        bool isInlineImage = false;
        PDFObjectReference xobject;                     ///< Image, FormUse: the XObject (not valid for an inline image or a direct object)
        QByteArray resourceName;                        ///< Image, FormUse, Shading: the name of the operand

        // Opaque
        QString opaqueReason;                           ///< Why the node is opaque (shown by the GUI)
    };

    explicit PDFContentNode(Data data, std::vector<PDFContentNodePointer> children);

    PDFContentNodeKind getKind() const { return m_data.kind; }
    PDFContentNodeId getId() const { return m_data.id; }
    PDFContentNodeId getParentId() const { return m_data.parentId; }
    const std::vector<PDFContentNodePointer>& getChildren() const { return m_children; }
    const PDFContentOperationSpan& getPrefix() const { return m_data.prefix; }
    const PDFContentOperationSpan& getSuffix() const { return m_data.suffix; }
    const QRectF& getBoundingBox() const { return m_data.boundingBox; }

    /// The state of the place, where the node is written (before its first operation, without
    /// its own override). The stream writer relies on it: the matrices of a soft mask (R-6) and
    /// the value of /op written beside /OP are computed from this state. The builder sets it
    /// for every node. The editor does not: insertNode, setNodeData and moveNode keep the states
    /// of the data as they are given, so a command, which moves a node or inserts one, gives it
    /// the state and the symbolic state of its new place (setNodeData) - and the same to the
    /// nodes, whose state its change reaches (the nodes after a removed or inserted state run).
    const PDFContentResolvedStatePointer& getState() const { return m_data.state; }

    /// The symbolic state of the same place as getState
    const PDFContentSymbolicStatePointer& getSymbolicState() const { return m_data.symbolicState; }
    const PDFContentStateLeak& getLeak() const { return m_data.leak; }
    const PDFContentStateOverride* getOverride() const { return m_data.override ? &*m_data.override : nullptr; }
    bool isModified() const { return m_data.isModified; }
    bool hasClipEffect() const { return m_data.hasClipEffect; }
    const Data& getData() const { return m_data; }

    /// A leaf never has children (every kind except Root, Group, MarkedContent, TextObject)
    static bool isLeafKind(PDFContentNodeKind kind);

    /// A node of this kind can get a wrap of an override (R-1: Path, Image, Shading, FormUse,
    /// TextObject), unless it has a clipping effect
    static bool isWrappableKind(PDFContentNodeKind kind);

private:
    Data m_data;
    std::vector<PDFContentNodePointer> m_children;
};

/// A problem the builder met; the content is still loaded (an opaque node, a read-only page)
struct PDFContentIssue
{
    enum class Kind
    {
        OperatorFailed,         ///< The processor refused an operation (it stays in the tree, flagged Failed)
        UnknownOperator,
        CrossedBrackets,        ///< BDC q EMC Q and alike: the covering span is opaque
        InvalidTextObject,      ///< BT inside of BT, a forbidden operator inside of BT ... ET
        InvalidPath,            ///< A foreign operator between path construction and painting
        UnbalancedEnd,          ///< EMC, Q, ET without a beginning
        DepthLimit,             ///< Deeper than PDFContentContainer::MAXIMUM_DEPTH
        OperationLimit,         ///< More operations than MAXIMUM_OPERATION_COUNT: the whole container is opaque
        OperandLimit,           ///< More operands than MAXIMUM_OPERAND_COUNT
        UnreadableTail,         ///< The end of the content cannot be split into operations
        UndecodableStream,      ///< A stream of /Contents cannot be decoded: the container is read-only
        InvalidMarkedContentId, ///< MCID out of range or used twice
        UndefinedResource,      ///< An operation names a resource, which the dictionary does not define
        InvalidContainer,       ///< The key names nothing of the document, or the build cannot start (a form or a pattern without its page, no font cache or color management): there is no container
        IgnoredColorOperator    ///< A color operator in the cell of an uncolored tiling pattern (/PaintType 2), ignored by the standard (ISO 32000-2, 8.7.3.3): one issue per container, at the first such operator; the operation is not flagged Failed (it stays in the tree, sets no color and is the source of none)
    };

    Kind kind = Kind::OperatorFailed;
    PDFContentNodeId nodeId;        ///< The node, when the issue belongs to one
    size_t operationIndex = 0;      ///< The operation, when it belongs to one
    QString text;                   ///< Translated description
};

class PDFContentContainer;
using PDFContentContainerPointer = std::shared_ptr<const PDFContentContainer>;

/// A container: the operations of one content, the tree of nodes over them and its resources.
///
/// Threads and ownership: immutable; held by shared pointers of the session, of the undo stack,
/// of snapshots and of the worker, which built it - it lives as long as any of them. It
/// co-owns everything it refers to (operations, states with their fonts and color spaces,
/// resource objects), so it depends neither on the session nor on a cache; it does not keep the
/// document alive and holds no pointer into it. It can be read by any thread.
class PDF4QTLIBCORESHARED_EXPORT PDFContentContainer
{
public:
    /// Limits (zadani_overeni.md 6.2): exceeding one never refuses the page
    static constexpr int MAXIMUM_DEPTH = 256;                       ///< Deeper content is one opaque node up to its end
    static constexpr size_t MAXIMUM_OPERATION_COUNT = 5000000;      ///< Above it the whole container is one opaque node
    static constexpr size_t MAXIMUM_OPERAND_COUNT = 100000;         ///< Operands of one operation; above it the operation is opaque
    static constexpr PDFInteger MAXIMUM_MARKED_CONTENT_ID = 2147483647;

    /// The data of a container; a plain value used to create one
    struct Data
    {
        PDFContentContainerKey key;
        PDFContentOperationList operations;
        PDFContentNodePointer root;
        PDFContentResources resources;
        PDFContentResolvedStatePointer initialState;    ///< State at the beginning of the content (a form: the state of the place of its first use)
        PDFContentSymbolicStatePointer initialSymbolicState;
        QRectF boundingBox;                             ///< Page: the media box; Form, PatternCell: /BBox
        QTransform matrix;                              ///< Form, PatternCell: /Matrix; identity for a page
        PDFInteger structuralParentKey = -1;            ///< /StructParents of the page or of the form; -1 = none
        QByteArray fingerprint;                         ///< Hash of the decoded bytes of the streams and of the references of the resource dictionaries (zadani_jadro.md 4.6); of a form or a pattern: its own stream and its own /Resources only, never the resources it inherits from a page
        std::vector<PDFContentIssue> issues;
        bool isReadOnly = false;                        ///< A stream could not be decoded: shown, never changed, never written
        bool isColorIgnored = false;                    ///< The cell of an uncolored tiling pattern (/PaintType 2): color operators written into it have no effect
        PDFInteger inheritedResourcesPageIndex = -1;    ///< A form or a pattern cell without /Resources of its own: the index of the page, whose resources it was built with; -1 for a page and for a container with its own /Resources
    };

    explicit PDFContentContainer(Data data);

    const PDFContentContainerKey& getKey() const { return m_data.key; }
    const PDFContentOperationList& getOperations() const { return m_data.operations; }
    const PDFContentNodePointer& getRoot() const { return m_data.root; }
    const PDFContentResources& getResources() const { return m_data.resources; }
    const PDFContentResolvedStatePointer& getInitialState() const { return m_data.initialState; }
    const PDFContentSymbolicStatePointer& getInitialSymbolicState() const { return m_data.initialSymbolicState; }
    const QRectF& getBoundingBox() const { return m_data.boundingBox; }
    const QTransform& getMatrix() const { return m_data.matrix; }
    PDFInteger getStructuralParentKey() const { return m_data.structuralParentKey; }
    const QByteArray& getFingerprint() const { return m_data.fingerprint; }
    const std::vector<PDFContentIssue>& getIssues() const { return m_data.issues; }
    bool isReadOnly() const { return m_data.isReadOnly; }

    /// The container of the cell of an uncolored tiling pattern (/PaintType 2; ISO 32000-2,
    /// 8.7.3.3): color operators written into it have no effect - a command or a panel, which
    /// sets a color, uses it to refuse the change or to explain it. False for a page, a form and
    /// the cell of a colored tiling pattern.
    bool isColorIgnored() const { return m_data.isColorIgnored; }

    /// The index of the page, whose resources the container was built with, when the container
    /// (a form or a pattern cell) has no /Resources of its own (ISO 32000-2, 7.8.3: it uses the
    /// resources of the page it is drawn on); -1 for a page and for a container with its own
    /// /Resources. The document writer materializes exactly these resources, when a name was
    /// added to such a container (R-4).
    PDFInteger getInheritedResourcesPageIndex() const { return m_data.inheritedResourcesPageIndex; }

    const Data& getData() const { return m_data; }

    /// The node of an identifier, nullptr when there is none
    PDFContentNodePointer getNode(PDFContentNodeId id) const;

    /// The parent of a node, nullptr for the root and for an unknown identifier
    PDFContentNodePointer getParent(PDFContentNodeId id) const;

    /// Index of a node among the children of its parent, -1 for the root and for an unknown one
    PDFInteger getIndexInParent(PDFContentNodeId id) const;

    /// The largest identifier of a node of the container (the session counter starts above it)
    PDFContentNodeId getMaximumNodeId() const;

    /// Number of the nodes
    size_t getNodeCount() const;

    /// The index (of PDFContentContainerEditor::insertNode) at which new content is appended to
    /// the children of a node, so that it is the last content, which is still read as content:
    /// the number of the children without the children at the end, which must stay behind
    /// everything else. Such a child is
    ///  - the tail node: an Opaque node without operations, which the builder puts at the end
    ///    of the root, when the bytes after the last operation are more than white space and
    ///    comments (an unfinished string, operands without an operator; the bytes themselves
    ///    are PDFContentOperationList::getTail and are written after everything else);
    ///  - the rest of a content after an inline image without an end (an Opaque node, which
    ///    holds the refused operation BI): whatever is written after it, is image data;
    ///  - an unclosed node (isUnclosed), whose own last child is such a rest or a node of this
    ///    kind again (a group, a marked content or a text object, which the content does not
    ///    close before the image).
    /// A content can have both a rest of an inline image and a tail node behind it: new content
    /// goes before both (the index is the number of the children less two then).
    ///
    /// An unclosed node with nothing wrong inside is no such child: new content follows it. "At
    /// the end" means behind its missing closing operators then - the state of that place is the
    /// state behind the node (of a Group: the state before its q), and
    /// PDFContentContainerEditor::build gives the node its closing operators, as soon as
    /// something is written behind it.
    ///
    /// "Insert at the end" of the root is this index, never -1.
    /// \returns -1 for an unknown node and for a leaf
    PDFInteger getAppendIndex(PDFContentNodeId parentId) const;

    /// The content does not close the node: a Group without its Q (of a chain of q: without any
    /// of them), a MarkedContent without its EMC, a TextObject without its ET - the content ends
    /// inside of the node, or an inline image without an end swallows the rest of it. Such
    /// a node has opening operations and no closing ones (PDFContentNode::getSuffix is empty).
    /// The answer is about the node itself: the nodes inside of an unclosed node are not unclosed
    /// by that, the caller walks up, when it needs it.
    ///
    /// What the model does with it (zadani_jadro.md 4.4, R-7: a missing Q, ET or EMC is not added
    /// until the node is changed):
    ///  - a container, which is not changed, is written byte for byte: nothing is added;
    ///  - PDFContentContainerEditor::build gives an unclosed node its closing operators as
    ///    generated operations of its suffix (ET, EMC, one Q for each q of the chain), when the
    ///    new container writes an operation behind the end of the node - a node inserted or moved
    ///    behind it in the same parent or in a node above - or when the node has an override (its
    ///    wrap must not end inside of it). The node is closed in that container: isUnclosed is
    ///    false there. An unclosed node, whose last child is the rest of an inline image without
    ///    an end (or an unclosed node with such a rest, and so on), is never closed so: whatever is
    ///    written behind the rest is image data, new content goes before the node (getAppendIndex);
    ///  - the editor does not refuse anything because of an unclosed node: a command, which would
    ///    move, transform or reorder such a node, asks this function and refuses itself.
    /// \returns false for the root, a leaf, a closed node, a node without opening operations
    ///          (nothing is open) and an unknown identifier
    bool isUnclosed(PDFContentNodeId id) const;

    /// Nothing of the container differs from the content it was loaded from: no node is modified,
    /// has an override or has moved, no operation is generated or suppressed, no resource added.
    /// Such a container is never written.
    bool isUnchanged() const;

private:
    class Index;

    Data m_data;

    /// The nodes by their identifiers. It is built by the constructor and never changed (so
    /// every thread reads it without a lock); it points to nodes, which m_data.root owns.
    std::shared_ptr<const Index> m_index;
};

/// Changes a container into a new one (the old one stays as it is - undo keeps it). Used by the
/// tree builder to build the first container and by commands for every change.
///
/// Threads and ownership: a local object of one thread; build() gives an immutable container,
/// which shares the unchanged nodes and operations with the original.
class PDF4QTLIBCORESHARED_EXPORT PDFContentContainerEditor
{
public:
    /// An empty container of a key (the tree builder). The editor creates the Root node itself:
    /// the identifier 1, no operations, no children; its data are set by setNodeData (insertNode
    /// never inserts a root).
    explicit PDFContentContainerEditor(const PDFContentContainerKey& key);

    /// Starts from an existing container (a command)
    explicit PDFContentContainerEditor(const PDFContentContainerPointer& container);

    ~PDFContentContainerEditor();

    PDFContentContainerEditor(const PDFContentContainerEditor&) = delete;
    PDFContentContainerEditor& operator=(const PDFContentContainerEditor&) = delete;

    /// The operations and the resources being changed (appending only; an entry of the
    /// resources, which existed at load, is never removed)
    PDFContentOperationList& getOperations();
    PDFContentResources& getResources();

    /// The data of the container except its root, operations and resources (key, states, boxes,
    /// fingerprint, issues, read-only flag, color-ignored flag, page of the inherited resources;
    /// a new version keeps them as they were in the container it was made from, an empty
    /// container of a key has the defaults). The editor owns the root, the operations
    /// (getOperations) and the resources (getResources): the fields root, operations and
    /// resources of the returned value are ignored by build(), whatever the caller writes there.
    PDFContentContainer::Data& getContainerData();

    /// The current node of an identifier (nullptr, when there is none): an immutable node with
    /// its current children. The node of a place, which has not changed since the last call, is
    /// the same object again (the editor keeps the nodes it has made and forgets a node, when it
    /// or something below it changes), so a call costs the node itself and the changed nodes
    /// below it, which were not asked for since their change - not the whole changed subtree
    /// every time. A node returned earlier stays valid and unchanged, whatever the editor does.
    PDFContentNodePointer getNode(PDFContentNodeId id) const;

    /// Inserts a new node under a parent at an index of its children: -1 (at the end), or
    /// 0 ... the number of the children (the node gets this index). The identifier of \p data
    /// must be valid and unused, its parentId is set by the call; its states are kept as they
    /// are given (PDFContentNode::getState). New content at the end of a parent goes to
    /// PDFContentContainer::getAppendIndex, which is before the tail node of the root.
    /// \returns false, when the parent does not exist or is a leaf, the identifier is not valid
    ///          or used, the data are of the kind Root, the index is out of range, or the node
    ///          would be deeper than PDFContentContainer::MAXIMUM_DEPTH + 1 below the root (as
    ///          deep as the builder goes: MAXIMUM_DEPTH nested nodes with children and their leaves)
    bool insertNode(PDFContentNode::Data data, PDFContentNodeId parentId, PDFInteger index);

    /// Replaces the data of a node (its identifier, parent and children stay)
    /// \returns false for an unknown node, for a leaf kind given to a node with children, for
    ///          another kind than Root given to the root and for the kind Root given to another node
    bool setNodeData(PDFContentNodeId id, PDFContentNode::Data data);

    /// Removes a node with its subtree
    bool removeNode(PDFContentNodeId id);

    /// Moves a node with its subtree under another parent (or to another index of the same one).
    /// The index is -1 (at the end) or 0 ... the number of the children of the new parent
    /// **without the moved node**: inside of one parent the node is taken out first, then put
    /// at the index of the children, which are left. The data of the node stay, except its
    /// parent identifier: the caller gives the node the state of its new place
    /// (PDFContentNode::getState).
    /// \returns false for a move under itself or its own subtree, under a leaf, of the root, of
    ///          or under an unknown node, for an index out of range, and when a node of the
    ///          subtree would be deeper than PDFContentContainer::MAXIMUM_DEPTH + 1 below the
    ///          root; nothing changes then
    bool moveNode(PDFContentNodeId id, PDFContentNodeId newParentId, PDFInteger index);

    /// The new container. The editor must not be used afterwards.
    ///
    /// An unclosed node (PDFContentContainer::isUnclosed), behind whose end the new container
    /// writes an operation, or which has an override, gets its closing operators here: generated
    /// operations (ET, EMC, one Q for each q) appended to the operations and named by the suffix
    /// of the node. getNode of the editor does not show them before. A container, in which
    /// nothing is written behind its unclosed nodes, keeps them unclosed (the container of the
    /// builder and every unchanged one: they are written byte for byte).
    PDFContentContainerPointer build();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}   // namespace pdf

#endif // PDFCONTENTNODE_H
