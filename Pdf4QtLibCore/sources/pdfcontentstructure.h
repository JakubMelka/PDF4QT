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

#ifndef PDFCONTENTSTRUCTURE_H
#define PDFCONTENTSTRUCTURE_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFOperationControl;

// Edit Content, stage E0, package P04 (structure: reading and consistency). Plan: zadani_jadro.md
// 4.12; zadani.md KE.17, KE.25, RE-12; zadani_overeni.md 6.1, 6.2. The existing PDFStructureTree is
// not used: it reads the integer children of /K and keeps the nulls of /ParentTree since 2026-11-25
// (SECURITY_TODO.md item 66), but it is a move-only tree for reading, without the raw entries, the
// kind of a parent tree entry and the way a marked content was written, which a writer needs.

// Threads and ownership of the plain types of this header (PDFContentStructureChild,
// PDFContentStructureElement, PDFContentParentTreeItem, PDFContentStructureChange): values owned
// by their model or by the caller; their objects are owned PDFObject values and references (never
// a pointer into the document). Copied freely, also between threads.

/// Identifier of a structure element in the model: the index of the element in the model (stable
/// for the life of the model and of the models made from it); -1 = none, 0 = the structure tree root
using PDFContentStructureElementId = PDFInteger;

/// A child of a structure element (ISO 32000-2, 14.7.2, Table 355, /K; 14.7.5)
struct PDFContentStructureChild
{
    enum class Kind
    {
        Element,                ///< A structure element: \p element
        MarkedContent,          ///< An integer MCID of the page /Pg of the element: \p mcid
        MarkedContentReference, ///< /MCR: \p mcid, \p page, \p stream, \p streamOwner (Table 357)
        ObjectReference         ///< /OBJR: \p object, \p page (Table 358)
    };

    Kind kind = Kind::Element;
    PDFContentStructureElementId element = -1;
    PDFInteger mcid = -1;
    PDFObjectReference page;        ///< /Pg of the child (not valid: the page of the element)
    PDFObjectReference stream;      ///< /Stm of an /MCR
    PDFObjectReference streamOwner; ///< /StmOwn of an /MCR
    PDFObjectReference object;      ///< /Obj of an /OBJR
};

/// A structure element
struct PDFContentStructureElement
{
    PDFContentStructureElementId id = -1;
    PDFContentStructureElementId parent = -1;   ///< -1 for the root
    PDFObjectReference reference;               ///< The object of the document; not valid for an element created by the editor
    QByteArray type;                            ///< /S as written
    QByteArray standardType;                    ///< /S mapped through /RoleMap (the same, when it is not mapped)
    std::optional<QString> title;               ///< /T
    std::optional<QString> alternateText;       ///< /Alt
    std::optional<QString> actualText;          ///< /ActualText
    std::optional<QString> expandedText;        ///< /E
    std::optional<QString> language;            ///< /Lang
    std::optional<QByteArray> elementId;        ///< /ID
    PDFObjectReference page;                    ///< /Pg
    PDFObject otherEntries;                     ///< A dictionary of every other key as it is (/A, /C, /R, /Ref, namespaces, unknown keys): written back unchanged
    std::vector<PDFContentStructureChild> children;
    bool isModified = false;                    ///< Changed by the editor: written
};

/// One item of the parent tree (ISO 32000-2, 14.7.5.4)
struct PDFContentParentTreeItem
{
    enum class Kind
    {
        MarkedContentArray, ///< A page or a content stream with /StructParents: \p elements indexed by MCID, -1 for null
        Element,            ///< An object with /StructParent: \p elements has one item
        Foreign             ///< Something else (not an array, not a reference to an element of the model): \p raw is written back unchanged
    };

    Kind kind = Kind::Foreign;
    std::vector<PDFContentStructureElementId> elements;
    PDFObject raw;              ///< The original value (kept for Foreign; for the others the original, kept also after the item is changed - isModified tells)
    bool isModified = false;
};

/// What an edit of the content does to the structure (the consistency table of zadani_jadro.md
/// 4.12). Produced by the model, applied to the model and to the container by the command.
struct PDFContentStructureChange
{
    enum class Action
    {
        None,               ///< Nothing changes in the structure
        RemoveSequence,     ///< The last content of a sequence is gone: the wrap is removed, the child of the element removed, ParentTree[key][mcid] = null; the element stays, also without children
        Untag,              ///< A node leaves its sequence: it is not tagged any more
        SplitSequence,      ///< A node in the middle of a sequence gets another marking: the rest behind it gets a new MCID attached to the same element right after the original child
        KeepWithWrap,       ///< An order command moves the only content of a sequence with its wrap: the MCID stays
        Refused             ///< The edit is refused (\p text says why)
    };

    Action action = Action::None;
    PDFInteger structuralParentKey = -1;    ///< The key of the page (/StructParents)
    PDFInteger mcid = -1;                   ///< The MCID the action is about
    PDFInteger newMcid = -1;                ///< SplitSequence: the MCID of the rest
    PDFContentStructureElementId element = -1;
    QString text;                           ///< Refused: translated reason
};

class PDFContentStructureModel;
class PDFContentStructureDifference;
using PDFContentStructureModelPointer = std::shared_ptr<const PDFContentStructureModel>;

/// The structure tree of a document as the editor needs it: every child of /K including the
/// integer MCIDs, the parent tree with its nulls and its foreign items. Read from the raw objects
/// by two walks, of the elements and of the nodes of the parent tree (each with one visited set for
/// its whole walk, a depth limit, iterative): an element shared by two parents appears once, a
/// cycle ends.
///
/// Threads and ownership: immutable (a change gives a new model, which shares nothing mutable with
/// the old one); held by shared pointers of the session, of undo and of snapshots; read by any
/// thread. It owns its objects as PDFObject values and holds no pointer into the document.
class PDF4QTLIBCORESHARED_EXPORT PDFContentStructureModel
{
public:
    /// Limits (zadani_overeni.md 6.2)
    static constexpr int MAXIMUM_DEPTH = 256;                           ///< Of the elements
    static constexpr int MAXIMUM_NUMBER_TREE_DEPTH = 32;                ///< Of /ParentTree
    static constexpr PDFInteger MAXIMUM_MARKED_CONTENT_ID = 2147483647; ///< An MCID outside of 0 ... this is a reading issue

    PDFContentStructureModel() = default;

    /// Reads the structure of a document. A document without /StructTreeRoot gives an empty
    /// model (isEmpty), which is valid.
    /// \param document The document
    /// \param control Cancellation, may be nullptr (a canceled read returns nullptr)
    static PDFContentStructureModelPointer read(const PDFDocument* document, const PDFOperationControl* control);

    /// The document has no structure tree
    bool isEmpty() const;

    /// The elements; the element 0 is the structure tree root, when the model is not empty
    const std::vector<PDFContentStructureElement>& getElements() const;

    /// The element of an identifier, nullptr when there is none
    const PDFContentStructureElement* getElement(PDFContentStructureElementId id) const;

    /// The parent tree: key (/StructParents, /StructParent) -> item
    const std::map<PDFInteger, PDFContentParentTreeItem>& getParentTree() const;

    /// /ParentTreeNextKey, or one above the largest key, when it is missing or too small
    PDFInteger getNextParentTreeKey() const;

    /// The element of marked content: ParentTree[structuralParentKey][mcid]; -1, when the key or
    /// the MCID is not there or the item is null. The index is the MCID itself, nulls included.
    PDFContentStructureElementId getElementOfMarkedContent(PDFInteger structuralParentKey, PDFInteger mcid) const;

    /// The MCIDs of a page key in use: by the parent tree array (also its null items count as
    /// used indices up to its length) - a new MCID is above all of them and above \p usedInContent
    PDFInteger allocateMarkedContentId(PDFInteger structuralParentKey, PDFInteger largestUsedInContent) const;

    /// Problems of the reading (duplicate or invalid MCIDs, cycles cut, limits): texts for the GUI
    const std::vector<QString>& getIssues() const;

    /// The reading order of a page: its MCIDs in the order of the structure tree (depth first)
    std::vector<PDFInteger> getReadingOrder(PDFInteger structuralParentKey) const;

    // ---- consistency (zadani_jadro.md 4.12, table "Konzistence od E0") ----

    /// The content of a sequence is deleted.
    /// \param structuralParentKey The key of the page
    /// \param mcid The sequence
    /// \param isSequenceEmptyAfterwards No content stays in the sequence
    PDFContentStructureChange getChangeForDelete(PDFInteger structuralParentKey, PDFInteger mcid, bool isSequenceEmptyAfterwards) const;

    /// A node is moved out of its sequence by a drag in the tree (it becomes untagged); in the
    /// middle of the sequence the sequence is split
    PDFContentStructureChange getChangeForMoveOut(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, bool isInMiddle, PDFInteger largestUsedInContent) const;

    /// An order or alignment command moves a node of a sequence: the tag stays (the only content
    /// moves with its wrap, otherwise the sequence is split and the node gets a new MCID of the
    /// same element)
    PDFContentStructureChange getChangeForReorder(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, PDFInteger largestUsedInContent) const;

    /// The model after the changes, applied in their order (the original is not touched): one
    /// new model for all changes of a command (Clear Page changes every sequence of a page), so
    /// the model is copied once
    PDFContentStructureModelPointer applied(const std::vector<PDFContentStructureChange>& changes) const;

    /// The model after the changes, as applied(changes); \p difference (may be nullptr) gets
    /// what the changes touched, before and after, so that an undo step keeps the difference and
    /// not the two models (zadani_jadro.md 4.5).
    PDFContentStructureModelPointer applied(const std::vector<PDFContentStructureChange>& changes, PDFContentStructureDifference* difference) const;

    /// The model before the changes of \p difference, made from this model, which is the model
    /// after them (or a model equal to it in the parts the difference does not hold). The result
    /// is equal to the model before (isEqual), isModified and the foreign children included.
    /// \returns nullptr, when the difference cannot be of this model (another count of elements)
    PDFContentStructureModelPointer reverted(const PDFContentStructureDifference& difference) const;

    /// The model after the changes of \p difference, made from this model, which is the model
    /// before them (redo without the changes; the same as applied of the changes).
    /// \returns nullptr, when the difference cannot be of this model (another count of elements)
    PDFContentStructureModelPointer reapplied(const PDFContentStructureDifference& difference) const;

    /// The two models are equal in everything the queries, the consistency functions and the
    /// writer read (the elements, the parent tree, the foreign children and items, the irregular
    /// content, the identifiers in use, isModified, the issues and the source fingerprint)
    bool isEqual(const PDFContentStructureModel& other) const;

    /// This model without the changes of the tagged content of some keys (a page, which the
    /// writer leaves out): the difference of \p original and this model is taken apart. The items
    /// of the parent tree of the keys, and every element, whose changed children are all marked
    /// content of the keys, are as in \p original; every other change stays. An element with
    /// changed children of the keys and of other keys (a paragraph across a page break) is taken
    /// apart child by child: the removed children of the keys come back at their places, the
    /// parts of splits of the keys go, the changes of the other keys stay, in the order of /K.
    /// \param original The model read from the document now (the same source fingerprint)
    /// \param structuralParentKeys The keys (/StructParents) of the contents, which are not written
    /// \param[out] errorText Why the changes cannot be separated (may be nullptr)
    /// \returns nullptr, when they cannot: an element with changes of one of the keys has a
    ///          change, whose content is not known (a child without a known page, a changed entry
    ///          of the element), foreign children together with changes of another key, or a part
    ///          of a split of another key, whose place among the children of the keys is not known
    ///          (its original was removed, and children of the keys lie between the children it
    ///          can come from); or the two models have another count of elements
    PDFContentStructureModelPointer withoutChangesOfKeys(const PDFContentStructureModel& original, const std::set<PDFInteger>& structuralParentKeys, QString* errorText) const;

    /// Something differs from the document the model was read from
    bool isModified() const;

    /// The number of this version of the model: every model made by the default constructor,
    /// read, applied (also of changes, which change nothing) or withoutChangesOfKeys (when it
    /// leaves something out) gets a new number (unique in the process, never 0); reverted and
    /// reapplied give the model they make the number of the version it reproduces
    /// (PDFContentStructureDifference::getVersionBefore / getVersionAfter), a copy keeps the
    /// number. Two models with the same number have the same content, also when they are two
    /// objects: a command tells by it, whether the model is the one it was applied to (as by
    /// PDFContentContainer::getVersion). isEqual does not compare it.
    quint64 getVersion() const { return m_version; }

    /// A hash of everything read() took from the document (the value of /StructTreeRoot of the
    /// catalog, the objects of the elements and of the items of /K, the nodes and values of
    /// /ParentTree, /StructParents of the pages and streams named by the children). It is kept
    /// unchanged by applied, reverted and reapplied: the writer compares it with
    /// computeSourceFingerprint of the document it writes into, so a model read from another
    /// state of the document is refused, also in its modified parts.
    const QByteArray& getSourceFingerprint() const;

    /// The fingerprint read() would give the model of \p document now. A bounded walk with one
    /// visited set (the depth limits of the reading, PDFObjectWalk::DEFAULT_MAXIMUM_NODES objects).
    /// \param document The document (nullptr: the fingerprint of a model without a document)
    /// \param control Cancellation, may be nullptr (a canceled computation returns an empty array)
    static QByteArray computeSourceFingerprint(const PDFDocument* document, const PDFOperationControl* control);

private:
    friend class PDFContentStructureModelHelper;    ///< Reading and the changes (pdfcontentstructure.cpp)
    friend class PDFStructureTreeWriterHelper;      ///< Writing (pdfstructuretreewriter.cpp)
    friend class PDFContentStructureDifference;     ///< Its states hold the private parts of the model

    /// An item of /K the model does not read as a child (null, a wrong type, a direct structure
    /// element dictionary, a marked content or object reference with other keys than those of
    /// ISO 32000-2 Tables 357 and 358, a reference the walk refused: an element of another
    /// parent, a cycle, the depth limit). It is written back unchanged at its place, unless it
    /// names exactly one marked content of a page (a marked-content reference with other keys or
    /// a direct structure element dictionary, whose /K is one marked content): when the last
    /// content of that sequence is deleted, the reference is removed and the direct element loses
    /// its /K (as an element of the model loses its child).
    struct ForeignChild
    {
        size_t position = 0;        ///< Number of the children of the element in front of it
        PDFObject object;           ///< The item as it is
        PDFInteger key = -1;        ///< The key of the marked content the item names exactly, -1 for none
        PDFInteger mcid = -1;       ///< Its marked content identifier
        bool isElement = false;     ///< A direct structure element dictionary (not a marked-content reference)
    };

    /// A page or a content stream and a marked content identifier
    using ContentKey = std::pair<PDFInteger, PDFInteger>;

    /// A new number of a version (one counter of the process: models are read by workers too)
    static quint64 allocateVersion();

    quint64 m_version = allocateVersion();
    std::vector<PDFContentStructureElement> m_elements;
    std::map<PDFInteger, PDFContentParentTreeItem> m_parentTree;
    PDFInteger m_nextParentTreeKey = 0;
    std::vector<QString> m_issues;
    PDFObjectReference m_structureTreeRoot;
    bool m_isModified = false;

    /// No limit stopped the reading of the elements or of the parent tree. An incomplete
    /// model refuses the changes of the structure: what it writes would lose the unread part.
    bool m_isComplete = true;

    /// Element -> its foreign children, in the order of /K
    std::map<PDFContentStructureElementId, std::vector<ForeignChild>> m_foreignChildren;

    /// The elements, whose /K was one object and not an array (written back the same way,
    /// while they have one child)
    std::set<PDFContentStructureElementId> m_singleChildElements;

    /// (key, index) of the items of the arrays of the parent tree, which are neither null nor
    /// an element of the model: -1 in PDFContentParentTreeItem::elements, written back from raw
    std::set<ContentKey> m_foreignParentTreeItems;

    /// A page or a content stream named by a child -> its /StructParents
    std::map<PDFObjectReference, PDFInteger> m_structuralParentKeys;

    /// (key, MCID), whose items in /K do not agree with the parent tree (a child of more
    /// elements, of another element than the parent tree says, or twice in one element; an item
    /// of the parent tree, whose element does not list it; a foreign child names it). Its
    /// deletion removes it from every element; it cannot be split (it cannot be tagged anew).
    std::set<ContentKey> m_irregularContent;

    /// Key -> the largest MCID a child of an element names for it
    std::map<PDFInteger, PDFInteger> m_largestChildContentId;

    /// computeSourceFingerprint of the document at the reading
    QByteArray m_sourceFingerprint;
};

/// What PDFContentStructureModel::applied changed: the parts of the model the changes touched,
/// as they were before and as they are after them (an element with its children and foreign
/// children, an item of the parent tree, the membership of a marked content in the private sets,
/// the largest identifier of a key, the flag isModified). Its size is proportional to the touched
/// elements and items of the parent tree, not to the model; a change, which changes nothing,
/// gives an empty difference.
///
/// Threads and ownership: a plain value owned by the caller (a command of the undo stack); copied
/// freely, also between threads. It holds no pointer into a model or a document.
class PDF4QTLIBCORESHARED_EXPORT PDFContentStructureDifference
{
public:
    /// The changes changed nothing in the model
    bool isEmpty() const;

    /// Number of the elements the changes touched (with their foreign children)
    size_t getElementCount() const;

    /// Number of the items of the parent tree the changes touched
    size_t getParentTreeItemCount() const;

    /// The numbers of the versions of the models before and after the changes
    /// (PDFContentStructureModel::getVersion; 0 for a difference, which applied did not make)
    quint64 getVersionBefore() const { return m_versionBefore; }
    quint64 getVersionAfter() const { return m_versionAfter; }

private:
    friend class PDFContentStructureModel;
    friend class PDFContentStructureModelHelper;

    using ContentKey = PDFContentStructureModel::ContentKey;
    using ForeignChildren = std::vector<PDFContentStructureModel::ForeignChild>;

    /// An element and its foreign children (std::nullopt: the element does not exist, it has no
    /// foreign children)
    struct ElementState
    {
        std::optional<PDFContentStructureElement> element;
        std::optional<ForeignChildren> foreignChildren;
    };

    /// The touched parts of one model
    struct State
    {
        size_t elementCount = 0;
        std::map<PDFContentStructureElementId, ElementState> elements;
        std::map<PDFInteger, std::optional<PDFContentParentTreeItem>> parentTree;   ///< std::nullopt: no item
        std::set<ContentKey> foreignParentTreeItems;    ///< Those of m_touchedContents, which are in the set of the model
        std::set<ContentKey> irregularContent;          ///< Those of m_touchedContents, which are in the set of the model
        std::map<PDFInteger, std::optional<PDFInteger>> largestChildContentId;      ///< std::nullopt: no entry
        PDFInteger nextParentTreeKey = 0;
        bool isModified = false;
    };

    State m_before;
    State m_after;
    std::set<ContentKey> m_touchedContents;     ///< (key, MCID), whose membership in a private set differs
    quint64 m_versionBefore = 0;
    quint64 m_versionAfter = 0;
};

}   // namespace pdf

#endif // PDFCONTENTSTRUCTURE_H
