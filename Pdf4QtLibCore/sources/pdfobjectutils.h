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
    /// entries are not dereferenced. Kids are followed up to the maximal depth, which
    /// protects against cyclic trees of damaged documents.
    /// \param storage Storage
    /// \param root Root node of the number tree
    /// \param maximumDepth Maximal depth of the kids
    static std::vector<std::pair<PDFInteger, PDFObject>> readNumberTree(const PDFObjectStorage* storage, const PDFObject& root, int maximumDepth = 32);

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

    static void readNumberTreeNode(const PDFObjectStorage* storage, const PDFObject& node, int depth, int maximumDepth, std::vector<std::pair<PDFInteger, PDFObject>>& entries);
};

/// Storage, which can mark objects (for example, when we want to mark already visited objects
/// during parsing some complex structure, such as tree)
class PDFMarkedObjectsContext
{
public:
    inline explicit PDFMarkedObjectsContext() = default;

    inline bool isMarked(PDFObjectReference reference) const { return m_markedReferences.count(reference); }
    inline void mark(PDFObjectReference reference) { m_markedReferences.insert(reference); }
    inline void unmark(PDFObjectReference reference) { m_markedReferences.erase(reference); }

private:
    std::set<PDFObjectReference> m_markedReferences;
};

/// Class for marking/unmarking objects functioning as guard. If not already marked, then
/// during existence of this guard, object is marked and then it is unmarked. If it is
/// already marked, then nothing happens and locked flag is set to false.
class PDFMarkedObjectsLock
{
public:
    explicit inline PDFMarkedObjectsLock(PDFMarkedObjectsContext* context, PDFObjectReference reference) :
        m_context(context),
        m_reference(reference),
        m_locked(!reference.isValid() || !context->isMarked(reference))
    {
        if (m_locked && reference.isValid())
        {
            context->mark(reference);
        }
    }

    explicit inline PDFMarkedObjectsLock(PDFMarkedObjectsContext* context, const PDFObject& object) :
        PDFMarkedObjectsLock(context, object.isReference() ? object.getReference() : PDFObjectReference())
    {

    }

    inline ~PDFMarkedObjectsLock()
    {
        if (m_locked && m_reference.isValid())
        {
            m_context->unmark(m_reference);
        }
    }

    explicit operator bool() const { return m_locked; }

private:
    PDFMarkedObjectsContext* m_context;
    PDFObjectReference m_reference;
    bool m_locked;
};

/// Classifies objects according to their type. Some heuristic is used
/// when object type is missing or document is not well-formed.
class PDF4QTLIBCORESHARED_EXPORT PDFObjectClassifier
{
public:

    inline PDFObjectClassifier() = default;

    /// Performs object classification on a document. Old classification
    /// is being cleared.
    /// \param document Document
    void classify(const PDFDocument* document);

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
        Annotation      = 0x00000400
    };

    Q_DECLARE_FLAGS(Types, Type)

    /// Returns true, if object with given reference exists
    /// and was classified.
    /// \param reference Reference
    bool hasObject(PDFObjectReference reference) const;

    /// Returns true, if any object with given type is present in a document
    /// \param type Object type
    bool hasType(Type type) const { return m_allTypesUsed.testFlag(type); }

    /// Returns a list of objects with a given type
    /// \param type Type
    std::vector<PDFObjectReference> getObjectsByType(Type type) const;

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
        std::array<qint64, size_t(PDFObject::Type::LastType)> objectCountByType = { };
        std::map<Type, StatisticsItem> statistics;
    };

    /// Calculate document statistics. Document classification must be
    /// performed before this function is called, otherwise result is undefined.
    /// \param document Document
    /// \returns Calculated statistics of each object type
    Statistics calculateStatistics(const PDFDocument* document) const;

private:
    struct Classification
    {
        PDFObjectReference reference;
        Types types = None;
    };

    /// Marks object with a given type
    void mark(PDFObjectReference reference, Type type);

    /// Marks objects in dictionary with a given type
    void markDictionary(const PDFDocument* document, PDFObject object, Type type);

    std::vector<Classification> m_classification;
    Types m_allTypesUsed;
};

} // namespace pdf

#endif // PDFOBJECTUTILS_H
