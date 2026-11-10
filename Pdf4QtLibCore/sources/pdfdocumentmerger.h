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

#ifndef PDFDOCUMENTMERGER_H
#define PDFDOCUMENTMERGER_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfdocumentpageoperations.h"
#include "pdfimagepagefactory.h"
#include "pdfpage.h"

#include <QCoreApplication>
#include <QImage>
#include <QMarginsF>
#include <QRectF>

#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFProgressSink;
class PDFSecurityHandler;

/// One implementation of the assembly of documents (plan of step 2 of phase 2, chapter 5,
/// R1-R10): Merge, Blank PDF, the old PageMaster (PDFDocumentManipulator is its adapter),
/// PdfTool unite and Diff call it.
///
/// Phase 1 (R1): a PDF input, of which not every page is taken, goes through
/// PDFDocumentPageOperations::removePages with the complement of its pages - the outline of
/// all levels, the destinations, /Names /Pages and /Templates, the links, the fields, the
/// layers, the page labels and the objects nothing refers to are cleaned there. No object of
/// a page, which is not taken, reaches the result.
///
/// Phase 2: the cleaned documents are joined whole. Each input is copied by a mapping of
/// its own object numbers (the same document given twice is two inputs, R20); its page tree
/// is never followed (the pages get the inherited attributes and the new root as /Parent).
/// The keys of the catalogs are joined by ISO 32000-2 (the table of chapter 5.2 of the
/// plan): the name trees with the collisions renamed ("name_2") and the references of the
/// input rewritten, the form fields renamed only where their fully qualified names collide
/// (Q3), signatures removed with their appearances (the field stays empty, Q4), the layers
/// with the default state of each input kept, the structure tree under a DocumentFragment
/// of each input (Q1, R9), the page labels, the articles, the attachments; the scripts of
/// the document, XFA, the collection, the alternate configurations of the layers, /Perms,
/// /DSS, /Legal are removed. The result is not encrypted and never declares PDF/A, PDF/UA,
/// PDF/X, PDF/E or PDF/VT; it has a new /ID (14.4), the version max(inputs, 1.7), 2.0 with
/// /DPartRoot (Q7).
///
/// One PDF input with all its pages (in any order, turned, cloned, with blank and image
/// pages added; R3) keeps its catalog: only the page tree, the page labels and the document
/// parts change; the declaration of PDF/A and PDF/UA is removed (R7). With an image among its
/// inputs (owner answer Q18) the outline mode applies as well: Document parts - a bookmark and a
/// part for every input, the outline of the PDF below its bookmark; Join as for more inputs; No
/// outline - none.
///
/// The input documents are never changed. Reentrant: the state of a merge is local, one
/// merge runs in one thread. The images of the result are optimized by the caller
/// (PDFImageOptimizer, Merge: Optimize, K24), as PageMaster did.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentMerger
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentMerger)

public:
    PDFDocumentMerger() = delete;

    /// The outline of a result of more inputs (378:1879, menu Outline)
    enum class OutlineMode
    {
        NoOutline,      ///< No outline, no document parts
        Join,           ///< An item per run of pages of an input, the outline of the input below it (closed)
        DocumentParts   ///< An item per run of pages of an input
    };

    /// An input of a merge
    struct PDF4QTLIBCORESHARED_EXPORT Source
    {
        enum class Kind
        {
            Document,   ///< Pages of a PDF document
            Image,      ///< Frames of an image (bytes of a file, or a decoded image)
            Blank       ///< Blank pages
        };

        Kind kind = Kind::Blank;
        PDFDocumentPointer document;    ///< Kind::Document: the document (shared, immutable)
        QByteArray imageData;           ///< Kind::Image: bytes of the image file
        QByteArray imageFormat;         ///< Kind::Image: format of QImageReader, empty = by the content
        QImage image;                   ///< Kind::Image: a decoded image (used when \p imageData is empty)
        double dotsPerInch = 0.0;       ///< Kind::Image: resolution given by the caller, 0 = of the file
        QString name;                   ///< Name of the file ("Contract.pdf"): the captions of the parts, the left out inputs
        bool isOwnerPasswordVerified = false;   ///< Kind::Document: the owner password of its original file was verified (Q15), the permission check accepts it (getPermission)

        static Source createDocument(PDFDocumentPointer document, const QString& name);
        static Source createImage(const QByteArray& data, const QByteArray& format, double dotsPerInch, const QString& name);
        static Source createImage(const QImage& image, double dotsPerInch, const QString& name);
        static Source createBlank();
    };

    /// A page of the result
    struct Item
    {
        size_t source = 0;                      ///< Index of the source
        PDFInteger pageIndex = 0;               ///< Page of the document, frame of the image (from 0); not used for a blank page
        PageRotation turn = PageRotation::None; ///< Turn clockwise relative to the rotation of the page
        QSizeF pageSize;                        ///< Blank page: its size in points (required); image page: the size of the page, empty = by the resolution
        QMarginsF cropMargins;                  ///< Document page: margins in points cut from its crop box (left, top, right, bottom as the page is shown with its own rotation; B14: from the origin of the crop box)
        QRectF imageClip;                       ///< Image page: the part of the shown image, which fills the page, in the coordinates 0 to 1 from its top left corner; empty = all

        /// Image page: the layout of the image on its page (Add Image as Page, Images to PDF, the
        /// images of Insert Pages; plan of step 3 of phase 2, R1 a); std::nullopt = \p imageClip
        /// fills the page of \p pageSize (Merge, PageMaster - PDFImagePageLayout::createStretched)
        std::optional<PDFImagePageLayout> imageLayout;
    };

    /// What a merge does
    struct Settings
    {
        OutlineMode outline = OutlineMode::DocumentParts;
        bool checkPermissions = true;   ///< The inputs must permit the merge (R2); false only for Diff, whose result is no document of the user
        bool isNewFile = false;         ///< One PDF input keeps its catalog (R3 of step 2), but the result is a new file of its pages (Extract, plan 5.6 of step 3): new dates of the creation in /Info and XMP, as a part of Split (PDFNewDocumentFinisher::Settings::isNewFile)
    };

    /// What a merge has changed besides the pages
    struct Statistics
    {
        qint64 pageCount = 0;                   ///< Pages of the result
        qint64 inputCount = 0;                  ///< Inputs with a page in the result
        qint64 partCount = 0;                   ///< Runs of pages of one input (the document parts, K14)
        qint64 fieldsRenamed = 0;               ///< Form fields renamed for a collision of their names (Q3)
        qint64 destinationsRenamed = 0;         ///< Named destinations renamed for a collision
        qint64 namesRenamed = 0;                ///< Entries of the other name trees renamed (attachments, ...)
        qint64 signaturesRemoved = 0;           ///< Signatures removed from their fields (Q4)
        qint64 resourcesRenamed = 0;            ///< Default resources of forms renamed (/DR)
        bool scriptsRemoved = false;            ///< Scripts of a document, an action at its opening or document actions removed
        bool xfaRemoved = false;                ///< XFA forms removed (the AcroForm stays)
        bool collectionRemoved = false;         ///< A portable collection removed
        bool layerConfigurationsRemoved = false; ///< Alternate configurations of the layers removed
        bool outputIntentsRemoved = false;      ///< The output intents of the inputs differ, the result has none
        bool structureTreeRemoved = false;      ///< Tags removed: not every PDF input is tagged and taken whole (Q1)
        bool structureTreeMerged = false;       ///< The structure trees are joined
        bool isSingleDocument = false;          ///< One PDF input with all its pages: its catalog is kept (R3)
        QStringList conformanceRemoved;         ///< Standards no longer declared ("PDF/A", "PDF/UA")

        // The insertion mode (insert)
        qint64 insertedPageCount = 0;           ///< Pages inserted into the target
        qint64 replacedPageCount = 0;           ///< Pages of the target replaced by them
        bool insertedPagesUntagged = false;     ///< The target is tagged, the inserted pages are outside of its structure (a source is not tagged or not taken whole)
        bool isPageTreeFlattened = false;       ///< The page tree of the target was flattened (a replacement, a damaged tree): its inherited values went into its pages
        PDFDocumentPageOperations::RemovalStatistics replacement;  ///< What the removal of the replaced pages has changed (Replace selected pages)
    };

    /// An input, which is not in the result (its copy failed, owner answer Q5)
    struct LeftOut
    {
        size_t source = 0;          ///< Index of the source
        PDFInteger pageCount = 0;   ///< Pages of the result, which it would have had
        QString reason;             ///< Translated reason
    };

    /// Result of a merge
    struct Result
    {
        bool isSuccess = false;             ///< The document is valid
        bool isCanceled = false;            ///< Canceled, the document is empty
        QString errorMessage;               ///< Translated reason of a failure
        PDFDocument document;               ///< The result
        Statistics statistics;
        std::vector<LeftOut> leftOut;       ///< Inputs left out of a successful result
    };

    /// Joins the items of the sources into a new document
    /// \param sources The inputs
    /// \param items The pages of the result in their order; a page used more than once
    ///        becomes a clone after its first occurrence (a copy of the page object and of its
    ///        annotations, the content is shared; B4)
    /// \param settings What the merge does
    /// \param control Cancellation (optional), checked after each page, image and input
    /// \param progress Progress (optional): a step per page of the result
    static Result merge(const std::vector<Source>& sources, const std::vector<Item>& items, const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress);

    /// Inserts the items of the sources into a document, which stays itself (the insertion mode
    /// of Insert Pages and Add Image as Page; plan of step 3 of phase 2, 5.7, Q3 a, R8, Q18).
    ///
    /// Contract (the skeleton of step 3a declares it, package C defines it):
    ///  - The result is a new version of \p target for an incremental update (the rule of a tool
    ///    with IncrementalSave, batch 1, chapter 12): the object numbers, the /ID, the encryption,
    ///    the signatures, /Perms, the output intents (/OutputIntents always stay) and the
    ///    declaration of PDF/A, PDF/UA, PDF/X of the target stay (Q18: the result tells, that the
    ///    inserted pages were not checked); only new objects are added and the changed ones
    ///    rewritten.
    ///  - The page tree of the target is kept: the new pages are inserted into it and /Count of
    ///    their ancestors is updated, so every inherited value of the pages of the target
    ///    (/Resources, /MediaBox, /CropBox, /Rotate, Table 31) stays (R8).
    ///  - The sources are joined by the rules of merge (the name trees, the fields renamed on a
    ///    collision, the signatures of a source removed, the structure, the layers), the target
    ///    being the first input; every source permits it (getPermission, owner answer Q2 of step
    ///    2). A certified target is refused by the tool before (PDFDocMDPPermissions, Q12).
    ///  - \p position: the index of the page of the target, before which the items go (0 = the
    ///    beginning, the page count = the end); \p replacedPages: pages of the target (indices
    ///    from 0), which the items replace (Replace selected pages, 388:17167), their place is
    ///    \p position.
    ///  - Reentrant as merge; \p control after each page, \p progress a step per inserted page.
    /// \param target The document, into which the pages go
    /// \param position Index of the page of the target, before which the items go
    /// \param replacedPages Pages of the target removed by the insertion (indices from 0)
    /// \param sources The inputs of the inserted pages
    /// \param items The inserted pages in their order
    /// \param settings What the insertion does (the outline of the inserted pages)
    /// \param control Cancellation (optional)
    /// \param progress Progress (optional)
    ///
    /// Implementation notes (package C): \p position counts the pages of the target before the
    /// replaced ones are removed; the replaced pages go by PDFDocumentPageOperations::removePages
    /// first (the numbers stay; the tree is flattened there with its inherited values, R8), a
    /// failure of the insertion changes nothing (B20). Every replaced page is an error (no page of
    /// the target would stay to anchor the insertion - the tool asks for another position).
    /// The objects of the sources, which the result does not reach (their catalogs, page trees,
    /// outlines, encryption dictionaries), are not added; the new objects are numbered from the
    /// end of the target without gaps. The outline of the target stays, the outlines of the
    /// sources are not added (Insert adds pages, not bookmarks); the page labels are written only
    /// when the target has them (its pages keep their labels, 12.4.2); the structure of the
    /// target stays, a tagged source taken whole joins it as a DocumentFragment, else the inserted
    /// pages are outside of it (Statistics::insertedPagesUntagged); the document parts of the
    /// target (14.12) take the inserted pages into the part, inside of which they are inserted.
    static Result insert(const PDFDocument& target, PDFInteger position, std::vector<PDFInteger> replacedPages, const std::vector<Source>& sources,
                         const std::vector<Item>& items, const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress);

    /// The values of all signatures of a document removed (owner answer Q4 of step 2, the rule
    /// of the merge): the value /V of a signature field, the appearance of its widgets (the field
    /// stays empty for a new signature), bit 2 (AppendOnly) of /SigFlags; with a removed signature,
    /// and always with /Perms /DocMDP (also one, whose signature is in no field), /Perms (DocMDP,
    /// UR3) and /DSS go too (they belong to the signatures). The object numbers,
    /// /ID, the encryption, the version and the hash of the source data stay
    /// (PDFDocMDPPermissions::removeCertification: a certified document after a change of its pages).
    /// \param document The document
    /// \param signaturesRemoved Number of the signatures removed (optional)
    static PDFDocument removeSignatures(const PDFDocument& document, PDFInteger* signaturesRemoved);

    /// Whether a document may be merged (owner answer Q2, ISO 32000-2, 7.6.4.2, Table 22)
    enum class Permission
    {
        Permitted,              ///< Not encrypted, opened by the owner password, or nothing restricted
        AssembleDenied,         ///< Bit 11 (assemble the document) is clear: the owner password is needed
        OwnerPasswordRequired   ///< Assemble is allowed, but the result would lose another restriction (print, copy, changes): the owner password is needed
    };

    /// The permissions of Table 22, which an input of a merge must allow: bit 11 (assemble) and
    /// every restriction, which the result - not encrypted - would lose (bits 3, 4, 5, 6, 9, 12;
    /// not the deprecated bit 10). Bit 11 is the first. One list for the core (getPermission) and
    /// for the shared check of the new application (PDFAppDocumentPermissions).
    static std::vector<PDFSecurityHandler::Permission> getRequiredPermissions();

    /// The permission of a document for a merge: the result is not encrypted, so a document,
    /// which restricts anything besides the deprecated bit 10, needs its owner password
    static Permission getPermission(const PDFDocument* document);

    /// The permission of an input (owner answer Q15): an input, whose owner password was verified
    /// against its original file (verifyOwnerPassword), is Permitted - a snapshot of an open tab
    /// with unsaved changes was opened by the user password, its own handler cannot say it
    /// \param document The document of the input
    /// \param isOwnerPasswordVerified The owner password of the original file was verified
    static Permission getPermission(const PDFDocument* document, bool isOwnerPasswordVerified);

    /// Verifies the owner password against a security handler (of the original file of an open
    /// tab: its encryption dictionary, ISO 32000-2, 7.6.4.4, Algorithms 7 and 12 / 2.A). Nothing is
    /// decrypted; the handler is not changed (a copy of it authenticates). A document, which is
    /// not encrypted, has no owner password to verify: true. A damaged encryption dictionary
    /// (/Perms not confirmed by the key of the owner) is false; nothing is thrown.
    /// \param securityHandler The handler of the original file (PDFObjectStorage::getSecurityHandler), may be null
    /// \param ownerPassword The password
    static bool verifyOwnerPassword(const PDFSecurityHandler* securityHandler, const QString& ownerPassword);

    /// Verifies the owner password against the bytes of the original file: its encryption
    /// dictionary is read by PDFDocumentReader (the file is read, the snapshot is never touched).
    /// False for a file, which cannot be read.
    /// \param originalData The bytes of the original file
    /// \param ownerPassword The password
    static bool verifyOwnerPassword(const QByteArray& originalData, const QString& ownerPassword);

    /// Translated reason of a permission, which is not Permitted ("Contract.pdf does not allow ...")
    /// and the way out: the owner of the document unlocks it with its owner password (owner
    /// 2026-11-08: the new application asks for it, PdfTool takes it by --pswd; an assistant
    /// never asks for it - MCP has the English text of PDFAppDocumentPermissions)
    static QString getPermissionText(Permission permission, const QString& name);

    /// Most indirect objects of a result (PDF Reference 1.7, Table C.1)
    static constexpr qint64 MAXIMUM_OBJECT_COUNT = 8388607;

    /// Most pages of a result
    static constexpr qint64 MAXIMUM_PAGE_COUNT = 1000000;
};

}   // namespace pdf

#endif // PDFDOCUMENTMERGER_H
