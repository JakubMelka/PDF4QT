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
    PDFObject raw;              ///< The original value (kept for Foreign; for the others the original, until the item is changed)
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
using PDFContentStructureModelPointer = std::shared_ptr<const PDFContentStructureModel>;

/// The structure tree of a document as the editor needs it: every child of /K including the
/// integer MCIDs, the parent tree with its nulls and its foreign items. Read from the raw objects
/// by one PDFObjectWalk (one visited set for the whole walk, a depth limit, iterative): an element
/// shared by two parents appears once, a cycle ends.
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

    /// Something differs from the document the model was read from
    bool isModified() const;

private:
    std::vector<PDFContentStructureElement> m_elements;
    std::map<PDFInteger, PDFContentParentTreeItem> m_parentTree;
    PDFInteger m_nextParentTreeKey = 0;
    std::vector<QString> m_issues;
    PDFObjectReference m_structureTreeRoot;
    bool m_isModified = false;
};

}   // namespace pdf

#endif // PDFCONTENTSTRUCTURE_H
