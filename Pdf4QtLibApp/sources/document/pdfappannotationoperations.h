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

#ifndef PDFAPPANNOTATIONOPERATIONS_H
#define PDFAPPANNOTATIONOPERATIONS_H

#include "pdfappglobal.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfannotationproperties.h"
#include "pdfdocument.h"
#include "pdfdocumenttextindex.h"
#include "pdfoutline.h"
#include "pdftextlayout.h"

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <atomic>
#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;

/// Result of an operation: the changed document with its flags (one undo step,
/// committed by PDFAppAnnotationController::commit or by the MCP of step 7), or
/// nullopt, when nothing has been changed (invalid arguments, nothing to do)
using PDFAppAnnotationResult = std::optional<PDFModifiedDocument>;

/// Data of a new annotation (author, contents, colors), bounded by the operations
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNewAnnotation
{
public:
    QString author;             ///< Title (T) - PDFAuthorSettings::getAuthorName() of the caller
    QString contents;           ///< Contents (empty = none)
    QColor color;               ///< Color (C), the canonical light-theme value of PDFColorSwatchRow
    QColor fillColor;           ///< Interior color (IC), invalid = no fill
    PDFReal opacity = 1.0;      ///< Opacity (CA) 0 - 1
    PDFReal penWidth = 1.0;     ///< Border width 0 - 100
};

/// Part of a text markup on one page: the quadrilaterals of the marked text in
/// the page space (4 points per piece in the order of the PDF quad points: top
/// left, top right, bottom left, bottom right)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTextMarkupPart
{
public:
    PDFInteger pageIndex = -1;
    QPolygonF quadrilaterals;
};

/// Widget-free operations on the annotations and the outline (step 4 plan 3.1):
/// a document and arguments in, the changed document out. The user interface of
/// the packages A / B / C and the MCP of step 7 (change source Mcp) use the same
/// functions; nothing here touches a widget, the database or a global state other
/// than PDFAuthorSettings given by the caller in PDFAppNewAnnotation::author. Every
/// argument is untrusted (page indices, references, texts, numbers): an invalid
/// one gives nullopt, never an exception or a crash.
///
/// Wave 0 declares the functions; the bodies are written by the packages:
/// creation in pdfappannotationoperations_create.cpp (A), editing in
/// pdfappannotationoperations_edit.cpp (B), the outline in
/// pdfappoutlineoperations.cpp (C). A package may add functions of its part here
/// (a small edit of its own section). Until a package lands, its functions return
/// nullopt.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationOperations
{
public:
    PDFAppAnnotationOperations() = delete;

    /// Maximal length of a text written into an annotation (contents, reply, title)
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 64 * 1024;

    /// Maximal length of a web link
    static constexpr qsizetype MAXIMUM_URL_LENGTH = 2048;

    // ------------------------------------------------- creation (package A)

    /// Maximal number of the quadrilaterals of one text markup (pieces of the lines)
    static constexpr size_t MAXIMUM_MARKUP_QUADRILATERALS = 10000;

    /// Maximal number of the annotations created by one operation (Mark as annotations)
    static constexpr size_t MAXIMUM_CREATED_ANNOTATIONS = 200000;

    /// Opacity of a new highlight (the highlights are drawn with the blend mode Multiply)
    static constexpr PDFReal HIGHLIGHT_OPACITY = 1.0;

    /// Text markups (Highlight, Underline, Squiggly, StrikeOut) of a text selection,
    /// one annotation per part (the quadrilaterals of the selected text of one
    /// page, PDFAppAnnotationCreator::getMarkupParts); \p created gets their
    /// references (may be nullptr). Parts of pages, which do not exist, and empty
    /// parts are skipped; nothing left = nullopt. The contents of \p data (the
    /// comment of Highlight and comment) are written only into the first created
    /// markup - a selection over pages is one comment, not one per page.
    static PDFAppAnnotationResult createTextMarkups(const PDFDocumentPointer& document,
                                                    AnnotationType type,
                                                    const std::vector<PDFAppTextMarkupPart>& parts,
                                                    const PDFAppNewAnnotation& data,
                                                    std::vector<PDFObjectReference>* created);

    /// Sticky note (text annotation) with the icon at the point of the page
    static PDFAppAnnotationResult createStickyNote(const PDFDocumentPointer& document,
                                                   PDFInteger pageIndex,
                                                   const QPointF& position,
                                                   TextAnnotationIcon icon,
                                                   const PDFAppNewAnnotation& data,
                                                   PDFObjectReference* created);

    /// Text box (free text annotation) in the rectangle of the page; the border
    /// is PDFAppNewAnnotation::color (invalid = no border) of the width
    /// PDFAppNewAnnotation::penWidth (0 = no border)
    static PDFAppAnnotationResult createFreeText(const PDFDocumentPointer& document,
                                                 PDFInteger pageIndex,
                                                 const QRectF& rectangle,
                                                 const QString& text,
                                                 const PDFFreeTextStyle& style,
                                                 const PDFAppNewAnnotation& data,
                                                 PDFObjectReference* created);

    /// Web link in the rectangle of the page (refused for a link, which
    /// isWebLinkAllowed refuses)
    static PDFAppAnnotationResult createWebLink(const PDFDocumentPointer& document,
                                                PDFInteger pageIndex,
                                                const QRectF& rectangle,
                                                const QString& url,
                                                PDFObjectReference* created);

    /// Link to a page of the same document (Insert - Link to page): the link
    /// rectangle on the page \p pageIndex, the action GoTo to \p destination
    /// (its page index must exist; the page reference is taken from the document)
    static PDFAppAnnotationResult createPageLink(const PDFDocumentPointer& document,
                                                 PDFInteger pageIndex,
                                                 const QRectF& rectangle,
                                                 const PDFDestination& destination,
                                                 PDFObjectReference* created);

    /// Text markups of the search results (Mark as annotations, 737:9221), one
    /// annotation per occurrence, one undo step; a canceled \p control (may be
    /// nullptr) gives nullopt. \p processed (may be nullptr) counts the processed
    /// occurrences (the progress of a worker), \p createdCount (may be nullptr)
    /// gets the number of the created annotations. Thread safe (a worker builds
    /// the document of a snapshot).
    static PDFAppAnnotationResult markSearchResults(const PDFDocumentPointer& document,
                                                    const PDFDocumentTextSearchMatches& matches,
                                                    AnnotationType type,
                                                    const PDFAppNewAnnotation& data,
                                                    const PDFOperationControl* control,
                                                    std::atomic<size_t>* processed,
                                                    size_t* createdCount);

    /// The web link is http, https or mailto, at most MAXIMUM_URL_LENGTH long,
    /// without control characters
    static bool isWebLinkAllowed(const QString& url);

    /// The type is a text markup created by the operations (Highlight, Underline,
    /// Squiggly, StrikeOut)
    static bool isTextMarkupType(AnnotationType type);

    // ------------------------------------------------- editing (package B)

    /// Deletes the annotations (with their popups and replies)
    static PDFAppAnnotationResult deleteAnnotations(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations);

    /// Sets or clears the flag Hidden of the annotations
    static PDFAppAnnotationResult setAnnotationsHidden(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, bool hidden);

    /// Sets the contents of the annotation
    static PDFAppAnnotationResult setContents(const PDFDocumentPointer& document, PDFObjectReference annotation, const QString& contents);

    /// Adds a reply (a text annotation with IRT) to the annotation
    static PDFAppAnnotationResult addReply(const PDFDocumentPointer& document, PDFObjectReference annotation, const QString& contents, const QString& author, PDFObjectReference* created);

    /// Copies the annotations to the pages (the source page is skipped); one step.
    /// Refused (nullopt), when it would create more than MAXIMUM_COPIES annotations.
    static PDFAppAnnotationResult copyToPages(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, const std::vector<PDFInteger>& pageIndices);

    /// Number of the copies, which copyToPages makes of the arguments (the copies of
    /// the popups and of the replies are not counted)
    static size_t getCopyToPagesCount(const PDFDocumentPointer& document, const std::vector<PDFObjectReference>& annotations, const std::vector<PDFInteger>& pageIndices);

    /// Most copies of one copyToPages (owner, security review: annotations x pages)
    static constexpr size_t MAXIMUM_COPIES = 100000;

    /// Largest coordinate of a rectangle or a point written by the geometry operations
    static constexpr PDFReal MAXIMUM_COORDINATE = 1.0e6;

    /// The annotation exists on a page and it is neither Locked nor ReadOnly
    static bool canModifyAnnotation(const PDFDocumentPointer& document, PDFObjectReference annotation);

    /// The text of the annotation can be changed (not LockedContents / ReadOnly)
    static bool canModifyContents(const PDFDocumentPointer& document, PDFObjectReference annotation);

    /// Writes the properties of the fields \p fields (PDFAnnotationPropertyEditor,
    /// the properties dialog); the caller decides, which fields may be changed of
    /// a locked annotation (the dialog is read only for it)
    static PDFAppAnnotationResult setProperties(const PDFDocumentPointer& document,
                                                PDFObjectReference annotation,
                                                const PDFAnnotationProperties& properties,
                                                PDFAnnotationProperties::Fields fields);

    /// Moves / resizes the annotation to the rectangle (page coordinates)
    static PDFAppAnnotationResult setRectangle(const PDFDocumentPointer& document, PDFObjectReference annotation, const QRectF& rectangle);

    /// Transforms the annotation (rotation and flips of the geometry dialog)
    static PDFAppAnnotationResult transformAnnotation(const PDFDocumentPointer& document, PDFObjectReference annotation, const QTransform& transform);

    /// Sets the editable points of the annotation (the table of the points of the
    /// geometry dialog, PDFAnnotationManipulator::getEditablePoints)
    static PDFAppAnnotationResult setEditablePoints(const PDFDocumentPointer& document, PDFObjectReference annotation, const std::vector<QPointF>& points);

    // ------------------------------------------------- outline (package C)

    /// Path of an outline item: the indices of the children from the root
    using OutlinePath = std::vector<size_t>;

    /// Most items of an outline, which can be edited (the Outline panel shows as
    /// many); a larger outline is read only - an edit would lose the rest
    static constexpr size_t MAXIMUM_OUTLINE_ITEMS = 100000;

    /// Deepest outline, which can be edited
    static constexpr size_t MAXIMUM_OUTLINE_DEPTH = 256;

    /// Longest title of an outline item written by the operations
    static constexpr qsizetype MAXIMUM_OUTLINE_TITLE_LENGTH = 1024;

    // Every operation of the outline rewrites the item dictionaries of the outline
    // (/Parent, /Prev, /Next, /First, /Last, /Count) into their own objects and
    // keeps every other entry of an item (/A of any action, /SE, /C, unknown
    // keys); a new item gets a new object, a removed item becomes a null object.
    // The outline must be the one the catalog shows (a damaged outline - shared
    // items, cycles, more than MAXIMUM_OUTLINE_ITEMS items or deeper than
    // MAXIMUM_OUTLINE_DEPTH - is refused). The result has the flag Outline only.

    /// Replaces the outline of the document (flag Outline): the titles, the GoTo
    /// destinations, the colors and the styles of the items of \p root (an empty
    /// or a null root removes all items)
    static PDFAppAnnotationResult setOutline(const PDFDocumentPointer& document, const QSharedPointer<PDFOutlineItem>& root);

    /// Inserts a new item with the title and the destination as the child
    /// \p index of the item \p parent (index past the end = the last child)
    static PDFAppAnnotationResult insertOutlineItem(const PDFDocumentPointer& document, const OutlinePath& parent, size_t index, const QString& title, const PDFDestination& destination);

    /// Renames the item
    static PDFAppAnnotationResult renameOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item, const QString& title);

    /// Removes the item with its children
    static PDFAppAnnotationResult removeOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item);

    /// Moves the item with its children to be the child \p index of the item
    /// \p newParent; both are paths of the outline before the move (as
    /// QAbstractItemModel::moveRows; an index past the end = the last child). Refused
    /// for a parent inside of the moved item. \p newPath (may be nullptr) gets the
    /// path of the moved item after the move.
    static PDFAppAnnotationResult moveOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item, const OutlinePath& newParent, size_t index, OutlinePath* newPath);

    /// Sets the destination of the item (/Dest; the action /A is removed)
    static PDFAppAnnotationResult setOutlineItemDestination(const PDFDocumentPointer& document, const OutlinePath& item, const PDFDestination& destination);

    /// Sets the style of the title of the item (bits 1 and 2 of /F)
    static PDFAppAnnotationResult setOutlineItemStyle(const PDFDocumentPointer& document, const OutlinePath& item, bool bold, bool italic);

    /// Removes the items with their children as one change (an item inside of
    /// another removed item goes with it; duplicates are ignored)
    static PDFAppAnnotationResult removeOutlineItems(const PDFDocumentPointer& document, const std::vector<OutlinePath>& items);

    /// Moves the items with their children to be the children \p index, \p index + 1, ...
    /// of the item \p newParent in the order of the document (paths of the outline
    /// before the move; an item inside of another moved item goes with it). Refused
    /// for a parent inside of a moved item. \p newPaths (may be nullptr) gets the
    /// paths of the moved items after the move.
    static PDFAppAnnotationResult moveOutlineItems(const PDFDocumentPointer& document, const std::vector<OutlinePath>& items, const OutlinePath& newParent, size_t index, std::vector<OutlinePath>* newPaths);

    /// The target of the item is a position (a GoTo action or /Dest of the type
    /// XYZ of a page, not a named destination), which sets its own zoom (not null
    /// and not 0 - ISO 32000-2 12.3.2.2)
    static bool hasOutlineItemZoom(const PDFOutlineItem* item);

    /// The target of the item is a position (XYZ of a page), which keeps the zoom
    /// of the reader (the zoom is null or 0)
    static bool isOutlineItemKeepingZoom(const PDFOutlineItem* item);

    /// Items of the outline, whose target sets its own zoom (hasOutlineItemZoom),
    /// in the order of the document; empty when the outline cannot be edited
    static std::vector<OutlinePath> getOutlineItemsWithZoom(const PDFDocument* document);

    /// The positions (XYZ) of the targets of the items keep the zoom of the reader
    /// (the zoom becomes null) as one change; the other entries of the item and of
    /// its GoTo action are kept. Items without such a target are skipped; nullopt,
    /// when no item has been changed.
    static PDFAppAnnotationResult setOutlineItemsKeepZoom(const PDFDocumentPointer& document, const std::vector<OutlinePath>& items);

    /// References of the outline items in the order of the document (pre-order,
    /// the order of PDFAppOutlinePanel), empty when the outline cannot be edited.
    /// The references stay the same over the operations (a moved item keeps it).
    static std::vector<PDFObjectReference> getOutlineItemReferences(const PDFDocument* document);

    /// The outline of the document can be edited by the operations (no outline at
    /// all can be edited - the first item creates it)
    static bool isOutlineEditable(const PDFDocument* document);

    /// Title of an item typed by the user: control characters replaced by spaces,
    /// simplified and shortened to MAXIMUM_OUTLINE_TITLE_LENGTH
    static QString sanitizeOutlineTitle(const QString& title);
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONOPERATIONS_H
