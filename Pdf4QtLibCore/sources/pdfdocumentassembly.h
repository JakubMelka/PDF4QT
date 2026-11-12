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

#ifndef PDFDOCUMENTASSEMBLY_H
#define PDFDOCUMENTASSEMBLY_H

#include "pdfglobal.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageplan.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QMarginsF>
#include <QMetaType>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pdf
{

/// The assembly of Manage Pages (plan of step 3b of phase 2, 4.1, 4.2; RB2, RB4, RB6, RB7, QB3,
/// QB5; package M1): the sources, the items (single pages and groups) in their order and the bin
/// of the removed items. The skeleton declares it, package M1 defines the operations.
///
/// Contract:
///  - A value without Qt objects, implicitly shared and immutable (RB7): a copy shares the data,
///    every operation returns a new value. It lives in the parameter "assembly" of
///    PDFDocumentToolManage (a QVariant, PDFDocumentToolParameterType::Value): an undo step of
///    the parameter model copies only the pointer, operator== compares the identity of the data
///    (two values made separately with equal content are different values - a new undo step).
///    Equal content is compared by isEqual (the tests, the file).
///  - Threads: an operation reads only its arguments (reentrant); the GUI calls them in its
///    thread, the run gets a copy of the value (the data are never changed after their creation,
///    so the copy is safe in any thread). The value holds no document: the run gets the
///    documents of the sources by its context (PDFDocumentPointer, shared and immutable).
///  - Operations are static functions of the class (no free functions): GUI, MCP and tests call
///    the same ones. An operation with a selection of less than two items over Sort and Regroup
///    works with all items (387:13288). New items go after the selection, without a selection
///    at the end (B19); new groups at the place of the first selected item (B50).
///  - Hostile values (a file, MCP): the limits below are checked by every operation that adds;
///    an operation never throws, a refused one returns the same value with the reason.
///  - Checkpoints are not in the value (RB7): they are a state of the GUI and of the file
///    (PDFDocumentAssemblyFile::Content); the bin is in the value (Undo returns it too).
///  - The sources of a run (package M1): a PDF source is the input document of the context, whose
///    PDFDocumentToolInputDocument::inputId is Source::id (the target - isTarget - is the first
///    document of the context); an image source carries its bytes (Source::imageData), the run
///    reads nothing else. addSource keeps a given unique id, so the GUI gives a source the id of
///    its input (PDFAppToolInputList), MCP the id of the file.
///  - Positions (QB5, KB2): the position of a page is its number in the result (1 = the first page
///    of the first item), as the tiles show it; Odd / Even select the items, whose every page is
///    at an odd / even position (a group of more pages has both, it is never selected by them);
///    Regroup counts the positions in the pages of the selection (K27).
///  - The original of a page of the target (RB4): an occurrence made by Clone carries isClone;
///    the model keeps exactly one occurrence of each page of the target, which is not a clone,
///    in the items (the first one, when an operation would make two; the first clone, when the
///    original went to the bin) - arrange keeps the page object with it.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentAssembly
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentAssembly)

    friend class PDFDocumentAssemblyHelper;

public:
    /// Limits of an assembly (plan 4.1, 6 with RB8)
    static constexpr int MAXIMUM_SOURCE_COUNT = 1000;           ///< PDFAppToolInputList::MAXIMUM_INPUTS
    /// Pages of an assembly, the bin included (owner 2026-11-12, plan of step 3b chapter 13 point
    /// 3): the own tab and the tab of a document; a source or a document with more pages is
    /// refused. Merge, Insert and the other tools keep PDFDocumentMerger::MAXIMUM_PAGE_COUNT.
    static constexpr qint64 MAXIMUM_PAGE_COUNT = 65536;
    static constexpr int MAXIMUM_ITEM_COUNT = 100000;
    static constexpr int MAXIMUM_CHECKPOINT_COUNT = 100;
    static constexpr int MAXIMUM_NAME_LENGTH = 1024;

    /// Input of a blank page in PDFDocumentPagePlan::Item::input (a blank page has no source)
    static constexpr int BLANK_INPUT = -1;

    /// Kind of a source
    enum class SourceKind
    {
        Pdf,        ///< Pages of a PDF document
        Image       ///< Frames of an image (its bytes are in the assembly, Q11)
    };

    /// A source of the assembly (plan 4.1, 4.3); the index in getSources is the input of the
    /// pages of the plan (sources are only added; a removed one is parked, R12 of step 2)
    class Source
    {
    public:
        QString id;                     ///< Stable id ("s1", "s2", ...), never reused in one assembly
        SourceKind kind = SourceKind::Pdf;
        QString name;                   ///< Name of the file ("Contract.pdf"), of an image of the clipboard "Pasted image"
        QString path;                   ///< Absolute path of the file; empty = no file (an unsaved document, the clipboard)
        QString relativePath;           ///< Path relative to the assembly file (only the file reads it)
        qint64 size = -1;               ///< Size of the file in bytes, -1 = unknown
        QDateTime modified;             ///< Time of the last change of the file (UTC), invalid = unknown
        QByteArray sha256;              ///< SHA-256 of the file (the check of a changed source, 387:16980), empty = unknown
        PDFInteger pageCount = 0;       ///< Pages of a PDF, frames of an image
        QByteArray imageData;           ///< Image: the bytes of the file
        QByteArray imageFormat;         ///< Image: the format of QImageReader, empty = by the content
        bool isParked = false;          ///< Removed from the list of the sources, kept for the bin and the undo steps
        bool isTarget = false;          ///< The document of the tab of the document host ("this document", input 0); never removed
        std::vector<QSizeF> pageSizes;  ///< The shown size of each page in points (PDF: the effective crop box with /Rotate and /UserUnit, an image: its frames by their resolution) - Portrait / Landscape (B31), the size of a blank page; empty = unknown. Not in the file: the application fills it by updateSource, when it has opened the source (createDocumentSource, createImageSource)

        bool operator==(const Source&) const = default;
    };

    /// A page of an item
    class Page
    {
    public:
        PDFDocumentPagePlan::Item plan;     ///< input = index of the source (BLANK_INPUT for a blank page), page, turn, isClone (set by Clone only, RB4), pageSize of a blank page, imageLayout, cropMargins
        PDFObjectReference targetPage;      ///< A page of the target (the document host): its page object (RB2), else invalid
        quint64 targetVersion = 0;          ///< A page of the target: the version of the document, whose page it is (RB2), else 0

        bool operator==(const Page&) const = default;
    };

    /// An item of the canvas: a single page or a group
    class Item
    {
    public:
        QString id;                     ///< Stable id, unique in the assembly (the selection, the clipboard)
        QString name;                   ///< Own name (Rename, the name of a group); empty = the derived name
        std::vector<Page> pages;        ///< The pages in their order (at least one)
        bool isCreatedGroup = false;    ///< Made by Group or Regroup (QB3: a group is a part of the outline also with one page)

        /// The item is a group (QB3): more pages, or made by Group; a renamed single page is no group
        bool isGroup() const { return pages.size() > 1 || isCreatedGroup; }

        bool operator==(const Item&) const = default;
    };

    /// An item in the bin (B21): it goes back after its neighbor, when it still exists
    class RemovedItem
    {
    public:
        Item item;
        QString previousItemId;         ///< The item before it at its removal, empty = it was the first
        int index = 0;                  ///< Its index at the removal (the place, when the neighbor is gone)

        bool operator==(const RemovedItem&) const = default;
    };

    /// A checkpoint (B22, B23): not part of the value (RB7), held by the GUI and the file
    class Checkpoint
    {
    public:
        QString id;                     ///< Stable id (B23: names may repeat)
        QString name;
        QDateTime time;
        std::vector<Item> items;        ///< The items at the time of the checkpoint (the sources are only added, so they are known)

        bool operator==(const Checkpoint&) const = default;
    };

    /// The ids of the selected items in the order of the assembly
    using Selection = std::vector<QString>;

    /// What an operation tells besides the new value (\p edit of the operations, may be null)
    class Edit
    {
    public:
        Selection selection;            ///< The items selected after the operation
        QString errorMessage;           ///< Translated reason of a refusal (the returned value is the given one)
    };

    /// Mode of Regroup (387:13288, 387:15926; KB2 / QB5: positions in the selection)
    enum class RegroupMode
    {
        OddAndEven,             ///< Two groups: the odd and the even positions of the selection
        PagePairs,              ///< Groups of two pages
        EveryPages,             ///< Groups of N pages (RegroupSettings::pageCount)
        AtPageNumbers,          ///< A new group at each position (RegroupSettings::positions)
        ByBookmarks,            ///< A group per bookmark (RegroupSettings::breakPages, one PDF source)
        AlternateTwoFiles,      ///< The pages of two sources alternated (PDFDocumentPagePlan::interleave)
        AlternateSecondReversed,///< The same, the second source from its last page
        ReverseOrder            ///< The pages in the reverse order
    };

    /// Values of a Regroup
    class RegroupSettings
    {
    public:
        int pageCount = 2;                      ///< EveryPages: N (1 to the pages of the selection)
        std::vector<int> positions;             ///< AtPageNumbers: the positions in the selection from 1, where a group begins
        std::vector<PDFInteger> breakPages;     ///< ByBookmarks: the pages of the source (from 0), where a group begins (PDFOutlineBreakPoints)
        QStringList groupNames;                 ///< ByBookmarks: the names of the groups (the titles of the bookmarks)
    };

    /// Key of Sort (387:13288)
    enum class SortKey
    {
        FileName,       ///< The name of the source, numbers naturally ("file2" before "file10")
        Source,         ///< The order of the sources
        PageNumber,     ///< The page of its source
        Type,           ///< PDF page, image, blank page
        Reverse         ///< The reverse order
    };

    /// Rule of Select (387:13288; KB2 / QB5: Odd and Even by the position in the assembly)
    enum class SelectRule
    {
        All,
        None,
        Invert,
        PageRange,      ///< SelectSettings::pageRange of positions, or of the original pages of SelectSettings::sourceId
        Odd,
        Even,
        Portrait,       ///< After the turn and the crop (B31); a square is both
        Landscape,
        SearchResults   ///< The items matching SelectSettings::searchText (grouped, ungrouped, portrait, landscape, image, blank, a name)
    };

    /// Values of a Select
    class SelectSettings
    {
    public:
        QString pageRange;      ///< PageRange: the text of PDFPageRangeList
        QString sourceId;       ///< PageRange: the pages of this source by their original numbers; empty = positions in the assembly
        bool isAdded = false;   ///< PageRange: add to the selection (Add), else replace it
        QString searchText;     ///< SearchResults
    };

    /// The summary of the panels (387:12569: "Sources · 3 files · 16 of 31 pages used", Removed
    /// pages, Result), plan 4.1, KB5
    class Summary
    {
    public:
        int sourceCount = 0;            ///< Sources, which are not parked
        qint64 sourcePageCount = 0;     ///< Pages of the sources ("31")
        qint64 usedPageCount = 0;       ///< Pages of the sources used at least once ("16")
        qint64 removedPageCount = 0;    ///< Pages in the bin ("15")
        qint64 resultPageCount = 0;     ///< Pages of the result ("17")
        int itemCount = 0;              ///< Items of the canvas ("10 items")
        int partCount = 0;              ///< Document parts of the result (QB3, "5 document parts")
        qint64 estimatedSize = -1;      ///< About the size of the result in bytes (KB5: a PDF page = file size / pages, an image = its bytes, a blank page = 1 KB), -1 = unknown
    };

    /// The plan of the merger of the assembly (toPlan)
    class Plan
    {
    public:
        std::vector<PDFDocumentPagePlan::Item> items;       ///< The pages in their order; input = index of the source in getSources (BLANK_INPUT: a blank page)
        std::vector<PDFDocumentMerger::Part> parts;         ///< The document parts (QB3): a group, a run of one source outside of groups; a lone blank page and image go to the part before them
        std::vector<int> itemOfPage;                        ///< For each page of \p items the index of its item (the files per group)
    };

    /// An empty assembly (no source, no item)
    PDFDocumentAssembly();

    /// An assembly of the parts (the file, the tests, MCP); the values are checked by the limits
    /// and the ids (unique, every input of a page valid): an invalid content gives an empty
    /// assembly and \p errorMessage
    static PDFDocumentAssembly create(std::vector<Source> sources, std::vector<Item> items, std::vector<RemovedItem> removedItems, QString* errorMessage);

    const std::vector<Source>& getSources() const;
    const std::vector<Item>& getItems() const;
    const std::vector<RemovedItem>& getRemovedItems() const;

    /// Id of the instance of the assembly (the clipboard, B18: pages are pasted only into the
    /// assembly they come from); the operations keep it
    const QString& getInstanceId() const;

    /// No item
    bool isEmpty() const { return getItems().empty(); }

    /// The identity of the data (RB7): a copy is equal, an operation gives another value
    bool operator==(const PDFDocumentAssembly& other) const { return m_data == other.m_data; }

    /// The content is equal (sources, items, bin; not the instance id)
    bool isEqual(const PDFDocumentAssembly& other) const;

    // ---------------------------------------------------------------- operations (package M1)

    /// Adds a source (Add › Files…, Pages of a PDF…, Paste image, Open documents); the same file
    /// is the same source (B52). \p sourceId gets its id.
    static PDFDocumentAssembly addSource(const PDFDocumentAssembly& assembly, Source source, QString* sourceId, QString* errorMessage);

    /// Group (Ctrl+G) at the place of the first selected item; Ungroup keeps the pages in place
    static PDFDocumentAssembly group(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);
    static PDFDocumentAssembly ungroup(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);

    /// Rename… (F2): the own name of an item (at most MAXIMUM_NAME_LENGTH characters)
    static PDFDocumentAssembly rename(const PDFDocumentAssembly& assembly, const QString& itemId, const QString& name, Edit* edit);

    /// Regroup the selection (all items with less than two selected) at its place (B50)
    static PDFDocumentAssembly regroup(const PDFDocumentAssembly& assembly, const Selection& selection, RegroupMode mode, const RegroupSettings& settings, Edit* edit);

    /// Sort the selection (all items with less than two selected) in its positions, stable
    static PDFDocumentAssembly sort(const PDFDocumentAssembly& assembly, const Selection& selection, SortKey key, Edit* edit);

    /// Clone (Ctrl+L): the clones after the selection (RB4: the clones carry isClone)
    static PDFDocumentAssembly clone(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);

    /// Move to… and the drag: the selection before the item \p beforeItemId (empty = the end)
    static PDFDocumentAssembly move(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& beforeItemId, Edit* edit);

    /// Rotate left / right (Shift+F4 / F4), Reset rotation
    static PDFDocumentAssembly rotate(const PDFDocumentAssembly& assembly, const Selection& selection, PageRotation turn, Edit* edit);
    static PDFDocumentAssembly resetRotation(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);

    /// Crop… (Q17): margins of the visible area; \p isSameSource = all pages of the sources of the selection
    static PDFDocumentAssembly crop(const PDFDocumentAssembly& assembly, const Selection& selection, const QMarginsF& margins, bool isSameSource, Edit* edit);

    /// Blank page (Ctrl+Shift+I) after the selection: the size and the orientation of the page before it, A4 in an empty assembly
    static PDFDocumentAssembly insertBlank(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);

    /// Pages of a source after the selection (Add › Files…, Pages of a PDF…; PDFPageRangeList order, duplicates = further occurrences)
    static PDFDocumentAssembly insertPages(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& sourceId, const std::vector<PDFInteger>& pages, Edit* edit);

    /// Replace selection… (Ctrl+Alt+R): one operation (B20, B53) - a refused one changes nothing
    static PDFDocumentAssembly replace(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& sourceId, const std::vector<PDFInteger>& pages, Edit* edit);

    /// Remove (Del) into the bin; Restore / Restore all back to the place after the neighbor (B21)
    static PDFDocumentAssembly remove(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit);
    static PDFDocumentAssembly restore(const PDFDocumentAssembly& assembly, const Selection& removedItemIds, Edit* edit);
    static PDFDocumentAssembly restoreAll(const PDFDocumentAssembly& assembly, Edit* edit);

    /// Restores a checkpoint (B22: an undo step of the caller; the bin stays); its sources are
    /// known (they are only parked, B51). The pages of the target (the document host) follow their
    /// page objects (RB2): the checkpoint of an earlier version gets the indices of this one, a page
    /// whose object is gone leaves it, and a page of the target in neither the restored items nor
    /// the bin goes to the bin (getTargetPages knows every page)
    static PDFDocumentAssembly restoreCheckpoint(const PDFDocumentAssembly& assembly, const Checkpoint& checkpoint, Edit* edit);

    /// The items selected by a rule (Select menu, 387:15926 Select page range)
    static Selection select(const PDFDocumentAssembly& assembly, const Selection& selection, SelectRule rule, const SelectSettings& settings);

    /// The plan of the merger (the outputs of PDFDocumentToolManage, plan 4.4)
    static Plan toPlan(const PDFDocumentAssembly& assembly);

    /// The summary of the panels
    static Summary getSummary(const PDFDocumentAssembly& assembly);

    /// The assembly of a document of the document host: one target source (isTarget, input 0)
    /// and an item per page with its page object and the version (RB2)
    static PDFDocumentAssembly createForDocument(const PDFDocument* document, quint64 documentVersion, const QString& name);

    /// The assembly made for a new version of the target (RB2): the pages follow their page
    /// objects, new pages of the document go to the end (\p addedPageCount), the pages, which are
    /// gone, leave the items and the bin. \returns the new value.
    static PDFDocumentAssembly mapToDocument(const PDFDocumentAssembly& assembly, const PDFDocument* document, quint64 documentVersion, PDFInteger* addedPageCount);

    // ------------------------------------------------- helpers of package M1 (next to the fixed ones)

    /// A PDF source of a document (Add › Files…, Open documents): the name, the path, the pages
    /// and their shown sizes; the size, the time and the hash of the file are the application's
    static Source createDocumentSource(const PDFDocument* document, const QString& name, const QString& path);

    /// An image source of the bytes of an image file (Add › Files…, Paste image): its frames and
    /// their sizes (the frames of a multi-page TIFF take the size of the first one). A source of
    /// no page and \p errorMessage for bytes, which cannot be read (PDFImagePageFactory, R15).
    static Source createImageSource(const QByteArray& data, const QByteArray& format, const QString& name, const QString& path, QString* errorMessage);

    /// The source of the same id gets the values of \p source (the application has opened or
    /// located it, Use Changed File of 387:16980): the pages over its new page count go to the bin;
    /// the kind and the target flag stay
    static PDFDocumentAssembly updateSource(const PDFDocumentAssembly& assembly, Source source, Edit* edit);

    /// A source removed from the list (it is parked, R12 of step 2: the undo steps and the
    /// checkpoints still know it): its pages go to the bin (Open Without Them of 387:16980). The
    /// target is never removed.
    static PDFDocumentAssembly removeSource(const PDFDocumentAssembly& assembly, const QString& sourceId, Edit* edit);

    /// Paste (4.6, B18, B19): copies of items of this assembly (the clipboard holds them by value)
    /// after the selection with new ids; a page of an unknown source is refused
    static PDFDocumentAssembly insertItems(const PDFDocumentAssembly& assembly, const Selection& selection, std::vector<Item> items, Edit* edit);

    /// A checkpoint of the items (Save checkpoint, B23: a new id, names may repeat; the time now);
    /// PDFDocumentAssemblyFile::canCreateCheckpoint says, whether the file has room for it
    static Checkpoint createCheckpoint(const PDFDocumentAssembly& assembly, const QString& name);

    /// The limits of an assembly (sources, items, MAXIMUM_PAGE_COUNT pages with the bin), which
    /// the operations keep: false and the reason for a value over them - the assembly of the tab
    /// of a document, which has grown by another tool (mapToDocument adds its pages); the run
    /// refuses it (PDFDocumentToolManage::validate)
    static bool checkLimits(const PDFDocumentAssembly& assembly, QString* errorMessage);

    /// A document of \p pageCount pages fits an assembly (MAXIMUM_PAGE_COUNT): the tab of a
    /// document, MCP pages; false and the reason
    static bool checkDocumentPageCount(PDFInteger pageCount, QString* errorMessage);

    /// The name of an item: its own name, else derived ("Contract.pdf, p. 1–5", "signature_page.png",
    /// "Blank page") - the canvas, {group_name} of the files per group, the titles of the parts
    static QString getItemName(const PDFDocumentAssembly& assembly, const Item& item);

    /// The shown size of a page in points: the size of its source page (Source::pageSizes) or of
    /// the blank page, less the crop, turned by its turn; empty = unknown
    static QSizeF getPageSize(const PDFDocumentAssembly& assembly, const Page& page);

    /// The page objects of the target in the order of the version, for which the assembly was made
    /// (PDFDocumentMerger::Settings::targetPages, RB2): every page of the target is in the items or
    /// in the bin; an invalid reference for a page, which is in none of them
    static std::vector<PDFObjectReference> getTargetPages(const PDFDocumentAssembly& assembly);

private:
    struct Data
    {
        QString instanceId;
        std::vector<Source> sources;
        std::vector<Item> items;
        std::vector<RemovedItem> removedItems;
        quint64 nextId = 1;             ///< The number of the next id of an item ("p<n>") and of a source ("s<n>")
    };

    explicit PDFDocumentAssembly(std::shared_ptr<const Data> data);

    std::shared_ptr<const Data> m_data;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFDocumentAssembly)

#endif // PDFDOCUMENTASSEMBLY_H
