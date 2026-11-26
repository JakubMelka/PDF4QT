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

#ifndef PDFOBJECTUTILS_H
#define PDFOBJECTUTILS_H

#include "pdfobject.h"

#include <set>
#include <vector>
#include <atomic>

namespace pdf
{
class PDFObjectStorage;
class PDFDocument;
class PDFObjectWalk;
class PDFOperationControl;

/// Utilities for manipulation with objects
class PDF4QTLIBCORESHARED_EXPORT PDFObjectUtils
{
public:
    /// Returns a list of references referenced by \p objects. So, all references, which are present
    /// in objects, appear in the result set, including objects, which are referenced by referenced
    /// objects (so, transitive closure above reference graph is returned).
    /// \param objects Objects
    /// \param storage Storage
    static std::set<PDFObjectReference> getReferences(const std::vector<PDFObject>& objects, const PDFObjectStorage& storage);

    /// Returns a list of references directly referenced from object. References itself are not followed.
    static std::set<PDFObjectReference> getDirectReferences(const PDFObject& object);

    static PDFObject replaceReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObjectReference>& referenceMapping);

    /// Returns name for object type
    /// \param type Type
    static QString getObjectTypeName(PDFObject::Type type);

    /// Returns a builder with a copy of the dictionary of the object (a reference is
    /// dereferenced), or an empty builder, if the object is not a dictionary.
    /// \param storage Storage
    /// \param object Object
    static PDFDictionaryBuilder copyDictionary(const PDFObjectStorage* storage, const PDFObject& object);

    /// Reads the entries of a number tree in the order of the tree. The values of the
    /// entries are not dereferenced. The kids are read by one PDFObjectWalk: each node once
    /// (a kid listed twice or a cycle of a damaged document adds nothing), up to the maximal depth.
    /// One budget of the walk (PDFObjectWalk::DEFAULT_MAXIMUM_NODES) bounds the whole reading:
    /// every entered node and every pair of /Nums read spend one item of it. A direct node is
    /// entered each time it is reached, and so is the /Nums array it names: many direct nodes,
    /// which name one indirect array, read it once each - the budget keeps that work and the
    /// number of the entries bounded.
    /// \param storage Storage
    /// \param root Root node of the number tree
    /// \param maximumDepth Maximal depth of the kids
    /// \param isLimitReached Optional. Set to true, when a node or a pair was not read because of
    ///        the maximal depth or of the budget of the walk (PDFObjectWalk::isLimitReached):
    ///        the result may be incomplete, and a caller, which writes the tree again from it,
    ///        would lose the rest. Set to false otherwise. A kid refused because it was read
    ///        already (a cycle, a kid listed twice) is no limit: its entries are in the result.
    ///        A node, which is not a dictionary, and a missing object are no limit either.
    static std::vector<std::pair<PDFInteger, PDFObject>> readNumberTree(const PDFObjectStorage* storage, const PDFObject& root, int maximumDepth = 32, bool* isLimitReached = nullptr);

    /// Returns true, if the object is an object stream of the file structure: a stream with
    /// /Type /ObjStm and the required entries /N and /First (PDF Reference 1.7, 3.4.6).
    /// \param object Object (a reference is not dereferenced)
    static bool isObjectStream(const PDFObject& object);

    /// Removes the objects of the storage, which are containers of the organization of a file
    /// and no part of the document, when the file is written whole: object streams (PDF
    /// Reference 1.7, 3.4.6 - the reader has unpacked them), cross-reference streams (3.4.7 -
    /// the writer writes its own table), the linearization parameter dictionary and the hint
    /// streams (Appendix F - "an operation that regenerates the document may remove the stream",
    /// F.2.5). A file written without them does not claim to be linearized (Table F.1).
    ///
    /// They are found by their dictionaries, a hint stream only in a linearized file. A container
    /// referenced by anything, which stays (the trailer, any other object, also an unreachable
    /// one, a container, which stays), is kept: nothing dangling is ever produced. A reference
    /// counts by its object number, whatever its generation is. Nothing else of the storage
    /// is changed (unreferenced objects of other kinds are the matter of PDFOptimizer); the
    /// encryption stays - the encryption dictionary is referenced by the trailer.
    ///
    /// An object stream is the only copy of its objects, when the reader has not unpacked them
    /// (PDFDocumentReader in the permissive reading of a damaged file does not): when a reference
    /// leads to an object the storage does not have, an object stream, which lists an object the
    /// storage does not have (or which cannot be read), stays too.
    ///
    /// Never for a document saved by an incremental update: the original bytes stay in that file
    /// and its cross-reference sections still lead into them.
    /// \param storage Storage
    /// \returns Number of the removed objects
    static qint64 removeFileStructureObjects(PDFObjectStorage& storage);

private:
    PDFObjectUtils() = delete;

    static void readNumberTreeNode(const PDFObjectStorage* storage, const PDFObject& node, int depth, PDFObjectWalk* walk, std::vector<std::pair<PDFInteger, PDFObject>>& entries);
};

/// One walk over a graph of the objects of a document: the outline (/First, /Next), the page
/// tree, the name and number trees, the structure tree (/K), the fields (/Kids, /Parent), the
/// actions (/Next), the forms of the resources, the closure of the references, ...
///
/// Why (review RCORE, 2026-11-06): the walk of the outline of Remove Pages had its set of the
/// visited items for one chain of siblings. Two items, whose /First lead back to the first of
/// them, made each path a new walk of the same two items - about 2^256 steps within its depth
/// limit, the tool never ended. A set per path or per chain (a reference forgotten, when its
/// subtree is done) stops a cycle, but not a node shared by more paths: the work doubles with
/// each level. So every walk has
///  - one set of the visited references for the whole walk: an indirect object is entered once,
///    whatever path leads to it; a cycle and a shared node end at the second visit,
///  - a depth limit: the recursion of the caller and its stack stay bounded,
///  - a budget of the entered nodes: the work stays linear in the number of objects, also for
///    the direct objects, which have no reference to remember.
/// A direct object is a tree inside of its indirect object (it has no cycle): it is walked by
/// the depth and the budget only, so it is entered each time its container is read.
///
/// A node refused because it was visited is a part of the graph, which the walk has read already
/// (the result is the one of the reachable graph, each node counted once). A node refused by the
/// depth or the budget is not read at all: isLimitReached() says so, and a caller, which must
/// not change a document from an incomplete walk, fails. Not thread-safe: one walk, one thread.
class PDF4QTLIBCORESHARED_EXPORT PDFObjectWalk
{
public:
    /// Depth of a walk, whose caller has no own limit
    static constexpr int DEFAULT_MAXIMUM_DEPTH = 256;

    /// Twice the limit of the number of the indirect objects of ISO 32000-2, Annex C
    /// (8 388 607): a walk, which enters each object once, never spends it on a conforming file
    static constexpr qint64 DEFAULT_MAXIMUM_NODES = qint64(16) * 1024 * 1024;

    /// \param maximumDepth Maximal depth of an entered node (the root has the depth 0)
    /// \param maximumNodes Maximal number of the entered nodes
    explicit PDFObjectWalk(int maximumDepth = DEFAULT_MAXIMUM_DEPTH, qint64 maximumNodes = DEFAULT_MAXIMUM_NODES);

    /// Enters a node of the walk: true = the caller reads it. A reference is entered once in the
    /// whole walk, a direct object (or an invalid reference) each time it is reached.
    /// \param node Node (a reference or a direct object)
    /// \param depth Depth of the node
    bool enter(const PDFObject& node, int depth);

    /// Enters an indirect object (see enter)
    /// \param reference Reference of the object
    /// \param depth Depth of the object
    bool enterReference(PDFObjectReference reference, int depth);

    /// True, if the object was entered by this walk
    bool isVisited(PDFObjectReference reference) const { return m_visited.count(reference) > 0; }

    /// True, if a node was refused by the depth or by the budget: the walk has not read the
    /// whole graph (a node refused as visited does not count)
    bool isLimitReached() const { return m_isLimitReached; }

    /// Number of the entered nodes
    qint64 getNodeCount() const { return m_nodeCount; }

    int getMaximumDepth() const { return m_maximumDepth; }

    /// The entered indirect objects
    const std::set<PDFObjectReference>& getVisitedReferences() const { return m_visited; }

private:
    /// Checks the depth and spends the budget of one node
    bool enterNode(int depth);

    int m_maximumDepth;
    qint64 m_maximumNodes;
    qint64 m_nodeCount = 0;
    bool m_isLimitReached = false;
    std::set<PDFObjectReference> m_visited;
};

/// Classifies the objects of a document by their function (plan of step 7 of phase 2, 4.2; Document
/// Statistics, the views of Object Inspector, the old ObjectInspectorPlugin and PdfTool `statistics`).
///
/// Two results:
///  - the roots of a class (getObjectsByType, hasType): the objects, which are used directly as such - the
///    pages, their content streams (/Contents, a stream or an array of streams, ISO 32000-2 Table 31), the
///    annotations of the pages, the resources of every owner of resources (PDFResourceWalk: the pages with
///    their inherited /Resources, the forms, the appearance streams, the tiling patterns, the Type 3 fonts,
///    the groups of the soft masks; 7.8.3, Table 34) by their category (/ExtGState, /ColorSpace, /Pattern,
///    /Shading, /Font, /XObject by /Subtype), the forms of these owners, the actions (/OpenAction and /AA of
///    the catalog, /AA of the pages, /A and /AA of the annotations, /A of the outline items, /AA of the
///    fields, the JavaScript name tree, the chains of /Next of 12.6.2, a dictionary of /Type /Action - B205:
///    /Type /Action is optional, it is no condition) and the containers of the file structure (an object
///    stream /Type /ObjStm, 7.5.7, a cross-reference stream /Type /XRef, 7.5.8 - B206). An object may be a
///    root of more classes. These are the lists of the views of the inspector (482:15054).
///  - the class of each object (getClass) for the statistics: every object is assigned to exactly one class
///    - the class of the root it is used for (480:14219 "Every object is assigned to what it is used for"):
///    a root takes its class (by the priority of getClasses when it is a root of more of them), the other
///    objects take the class of the nearest root, from which they are reachable (a font program is a Font,
///    a soft mask of an image an Image, the resource dictionary of a page a Page). The back links /Parent
///    and /P are not followed (a widget would take its field, an annotation its page). What no root reaches
///    is None ("Other").
///
/// Every walk has one visited set, a depth limit and a budget of the nodes (PDFObjectWalk) and is iterative:
/// a cycle, a node shared by more paths, a reference to a missing object or a wrong type (B190: /Contents
/// 99999 0 R) changes nothing; isComplete() says, whether a limit stopped a walk (the statistics are then a
/// part of the document). Not thread-safe: one classifier, one thread; the document is only read.
class PDF4QTLIBCORESHARED_EXPORT PDFObjectClassifier
{
public:

    inline PDFObjectClassifier() = default;

    /// Performs object classification on a document. Old classification
    /// is being cleared.
    /// \param document Document
    void classify(const PDFDocument* document);

    /// Classifies the objects of a document (the old classification is cleared)
    /// \param document Document (nullptr = nothing)
    /// \param control Cancellation (optional), checked by every walk
    /// \returns false, when the classification was canceled (it is incomplete then)
    bool classify(const PDFDocument* document, const PDFOperationControl* control);

    enum Type : uint32_t
    {
        None            = 0x00000000,
        Page            = 0x00000001,
        ContentStream   = 0x00000002,
        GraphicState    = 0x00000004,
        ColorSpace      = 0x00000008,
        Pattern         = 0x00000010,
        Shading         = 0x00000020,
        Image           = 0x00000040,
        Form            = 0x00000080,
        Font            = 0x00000100,
        Action          = 0x00000200,
        Annotation      = 0x00000400,
        FileStructure   = 0x00000800     ///< Object streams and cross-reference streams (B206)
    };

    Q_DECLARE_FLAGS(Types, Type)

    /// Returns true, if object with given reference exists
    /// and was classified.
    /// \param reference Reference
    bool hasObject(PDFObjectReference reference) const;

    /// Returns true, if any object with given type is present in a document
    /// \param type Object type
    bool hasType(Type type) const { return m_allTypesUsed.testFlag(type); }

    /// Returns a list of objects with a given type (the roots of the class)
    /// \param type Type
    std::vector<PDFObjectReference> getObjectsByType(Type type) const;

    /// The number of the roots of a class (getObjectsByType without the list)
    /// \param type Type
    qint64 getObjectCountByType(Type type) const;

    /// The class of the object for the statistics (exactly one; None = no root uses it, "Other")
    /// \param reference Reference
    Type getClass(PDFObjectReference reference) const;

    /// False, when a limit of a walk (depth, budget) or a cancellation stopped the classification
    bool isComplete() const { return m_isComplete; }

    /// The classes in their priority (a root of more classes is assigned to the first one), None last
    static std::vector<Type> getClasses();

    struct StatisticsItem
    {
        inline StatisticsItem() :
            count(0),
            bytes(0)
        {

        }

        std::atomic<qint64> count;
        std::atomic<qint64> bytes;
    };

    struct Statistics
    {
        /// The values of the indirect objects by their type: an object, an entry of a dictionary or a stream,
        /// an item of an array, each counted once (a stream counts as a Stream, its dictionary is not counted
        /// twice); the free entries of the storage are no values
        std::array<qint64, size_t(PDFObject::Type::LastType)> objectCountByType = { };

        /// The objects of each class (getClass) and their serialized size ("object bytes", B186)
        std::map<Type, StatisticsItem> statistics;

        /// The computation was canceled (the numbers are incomplete)
        bool isCanceled = false;
    };

    /// Calculate document statistics. Document classification must be
    /// performed before this function is called, otherwise result is undefined.
    /// \param document Document
    /// \returns Calculated statistics of each object type
    Statistics calculateStatistics(const PDFDocument* document) const;

    /// Calculates the statistics of the classified document (PDFExecutionPolicy over the objects)
    /// \param document Document (the one classified)
    /// \param control Cancellation (optional)
    Statistics calculateStatistics(const PDFDocument* document, const PDFOperationControl* control) const;

private:
    friend class PDFObjectClassifierBuilder;

    struct Classification
    {
        PDFObjectReference reference;
        Types types = None;     ///< The classes, of which the object is a root
        Type assigned = None;   ///< The class of the statistics
    };

    /// Marks object with a given type (a root of the class)
    void mark(PDFObjectReference reference, Type type);

    std::vector<Classification> m_classification;
    Types m_allTypesUsed;
    bool m_isComplete = true;
};

} // namespace pdf

#endif // PDFOBJECTUTILS_H
