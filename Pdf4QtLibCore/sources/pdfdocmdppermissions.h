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

#ifndef PDFDOCMDPPERMISSIONS_H
#define PDFDOCMDPPERMISSIONS_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QCoreApplication>

namespace pdf
{

class PDFDocumentToolResult;
class PDFObjectWalk;

/// The permissions of the certification signature of a document (DocMDP, ISO 32000-2,
/// 12.8.2.2, Table 257: the value /P of the transform parameters of the signature reference
/// of /Perms /DocMDP; plan of step 3 of phase 2, Q12, R10) and of the locks of its signed
/// signature fields (/Lock /P, 12.7.5.5, PDF 2.0; plan of step 3b, RB5 point 3).
///
/// The one reading of the certification (PDFOCRApplyProcessor::getCertificationPermissions calls
/// it). Contract:
///  - No level of /P allows to insert, remove, rotate or crop pages (the page templates of
///    level 2 are no free insertion): a certified document is changed by Insert Pages, Add
///    Image as Page, Crop Pages, Resize Pages, Set Page Boxes, Rotate Pages, Remove Pages and
///    Manage Pages only as a copy (Save as a copy, written whole, without the values of the
///    signatures, /Perms /DocMDP and /DSS - Q4 of step 2, R10); an incremental update would mark
///    the certification as broken.
///  - The lock dictionary of a signature field (/Lock) has the permissions /P of the document
///    with the values of DocMDP since PDF 2.0; they apply, when the field is signed (a field
///    without its value has not applied its lock yet; without /P the lock changes no permission
///    of the document). getLevel is the strictest of DocMDP and of the locks of the signed fields.
///  - The tools call it in their validation, the shell and MCP in the choices of the result.
///  - Every function is reentrant, it reads only the document (a hostile /Perms, /Reference,
///    /TransformParams or /Lock of a wrong type is not certified or the default level 2; the
///    field tree is walked once with one visited set and a depth limit).
class PDF4QTLIBCORESHARED_EXPORT PDFDocMDPPermissions
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocMDPPermissions)

public:
    PDFDocMDPPermissions() = delete;

    /// The level of the certification; the values of the certified levels equal /P
    enum class Level
    {
        NotCertified = 0,               ///< No certification signature (/Perms without /DocMDP) and no signed field locked with /P
        NoChanges = 1,                  ///< /P 1: no changes
        FormFilling = 2,                ///< /P 2 (the default of DocMDP, also of a missing or unknown /P; an unknown /P of a lock): form filling, page templates and signing
        FormFillingAndAnnotations = 3   ///< /P 3: also annotations
    };

    /// The level of the document: the strictest (the smallest /P) of its certification and of the
    /// locks of its signed signature fields (NotCertified for nullptr)
    static Level getLevel(const PDFDocument* document);

    /// The level of the certification signature alone (/Perms /DocMDP, Table 257)
    static Level getCertificationLevel(const PDFDocument* document);

    /// The strictest level of the locks (/Lock /P) of the signed signature fields of /AcroForm
    /// /Fields (12.7.5.5); NotCertified, when no signed field has a lock with /P
    static Level getFieldLockLevel(const PDFDocument* document);

    /// The stricter of two levels (NotCertified is no restriction)
    static Level getStricterLevel(Level first, Level second);

    /// A change of the pages (insert, remove, rotate, crop, resize, boxes) keeps the certification
    /// of the level valid: only NotCertified (Table 257 allows no page change)
    static bool isPageChangeAllowed(Level level);

    /// A change of the pages keeps the certification of the document valid (getLevel)
    static bool isPageChangeAllowed(const PDFDocument* document);

    /// The document of a certified document, whose pages a tool has changed (Q12, R10): it can
    /// only be saved as a copy written whole, so it carries no broken certification - the values
    /// of all signatures go with their appearances (the fields stay empty for a new signature,
    /// Q4 of step 2), bit 2 (AppendOnly) of /SigFlags, /Perms (DocMDP, UR3) and /DSS. Everything
    /// else stays (the object numbers, /ID, the encryption). PDFDocumentMerger::removeSignatures.
    /// \param document The changed document
    /// \param signaturesRemoved Number of the signatures removed (optional)
    static PDFDocument removeCertification(const PDFDocument& document, PDFInteger* signaturesRemoved);

    /// Tells the result of a tool, which has changed the pages of a certified document and removed
    /// the certification (removeCertification), that it can only be saved as a copy: data
    /// "certificationRemoved" true, "certificationSignaturesRemoved", "signatureImpact"
    /// "lost-copy-only" (the value of a tool of PDFDocumentToolSignatureImpact::LostCopyOnly - the
    /// shell and MCP offer only Save as a copy written whole), and the fact "certification"
    /// unless the tool has written it already
    /// \param result The result of the tool
    /// \param signaturesRemoved Signatures removed by removeCertification
    static void addCertificationRemoved(PDFDocumentToolResult* result, PDFInteger signaturesRemoved);

    /// Depth of the field tree, which getFieldLockLevel walks
    static constexpr int MAXIMUM_FIELD_DEPTH = 256;

private:
    /// Walks a field and its kids (one walk of the whole tree): the lock levels of the signed
    /// signature fields go to \p level
    /// \param document The document
    /// \param field A field or a widget
    /// \param inheritedType The field type of its parent (/FT is inheritable, Table 226)
    /// \param isInheritedSigned The parent has a signature value (/V is inheritable)
    /// \param depth Depth of the field
    /// \param walk The walk of the whole tree
    /// \param level The strictest level found
    static void collectFieldLocks(const PDFDocument* document, const PDFObject& field, const QByteArray& inheritedType, bool isInheritedSigned,
                                  int depth, PDFObjectWalk* walk, Level* level);
};

}   // namespace pdf

#endif // PDFDOCMDPPERMISSIONS_H
