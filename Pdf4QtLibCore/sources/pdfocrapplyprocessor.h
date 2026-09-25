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

#ifndef PDFOCRAPPLYPROCESSOR_H
#define PDFOCRAPPLYPROCESSOR_H

#include "pdfocrmodel.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfocrcompression.h"
#include "pdfdocument.h"

#include <map>
#include <vector>

namespace pdf
{
class PDFOperationControl;

/// Writes the OCR results into the document as one transaction (PDF-02 to PDF-15):
/// the checks of the permissions and of the certification, the selection of the
/// pages, which can be written, the summary for the confirmation, the compression
/// of the scanned images, the writing of the text layer, the removal of the
/// unverified conformance declaration of a copy and the validation of the result.
///
/// The class does not depend on any user interface; the dialog of the editor and
/// the command line tool use it the same way:
///   1. checkPermissions - a blocking reason, why the document cannot be written,
///   2. createPlan - pages to write, excluded pages with the reason, statistics,
///   3. setCopyFileName (only when a copy is created),
///   4. getSummary / getWarnings - confirmation,
///   5. execute - in a worker thread, the source document is never modified.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRApplyProcessor
{
public:
    enum class OutputMode
    {
        ModifyDocument, ///< The current document is modified
        CreateCopy      ///< A copy of the document is written into a file
    };

    /// Properties of the document, which decide, whether and how it can be written
    struct Context
    {
        const PDFDocument* document = nullptr;

        /// File name of the document (to refuse a copy overwriting the document)
        QString fileName;

        bool canModify = true;
        bool canCopyContent = true;
        bool hasSignatures = false;
        bool isEncrypted = false;
        bool isTagged = false;

        /// Permissions of the certification signature (DocMDP, value /P of its transform
        /// parameters): 0 = the document is not certified, 1 = no changes allowed, 2 = form
        /// filling and signing, 3 = additionally annotations (PDF-12)
        int certificationPermissions = 0;

        /// Human readable names of the PDF/A and PDF/UA declarations of the document (PDF-15)
        QStringList conformanceDeclarations;

        bool hasConformanceDeclaration() const { return !conformanceDeclarations.isEmpty(); }
    };

    /// Creates the context from the document: permissions of the security handler,
    /// certification, signatures, structure tree and conformance declarations
    static Context createContext(const PDFDocument* document, const QString& fileName);

    /// Returns the permissions of the certification signature of the document
    /// (catalog /Perms /DocMDP, the first signature reference, /TransformParams /P).
    /// Returns 0, if the document is not certified, 2 if /P is missing (PDF-12).
    static int getCertificationPermissions(const PDFDocument* document);

    /// Returns true, if the document contains a signed signature field
    static bool hasSignatureFields(const PDFDocument* document);

    /// Returns the reason, why the document cannot be written in the output mode
    /// (translated), or an empty string, if it can be written (PDF-12, PDF-15)
    static QString checkPermissions(const Context& context, OutputMode outputMode);

    struct Request
    {
        OutputMode outputMode = OutputMode::ModifyDocument;

        /// Results of the candidate pages (only results with a valid recognition are considered)
        std::vector<PDFOCRPageResult> results;

        /// Cached analysis of the pages of the current document (optional). The writer
        /// checks the collisions with the existing text of the current document, not of
        /// the document of the recognition (PDF-02).
        std::map<PDFInteger, PDFOCRPageAnalysis> analysis;

        /// Cached fingerprints of the pages of the current document (optional);
        /// the missing ones are computed by execute (JOB-02, EXPORT-04)
        std::map<PDFInteger, QByteArray> fingerprints;

        /// Criteria of the review for the statistics of the summary
        PDFOCRReviewCriteria reviewCriteria;

        /// Options of the writer (markAsArtifact is decided by the context)
        PDFOCRTextLayerWriter::Options writerOptions;

        /// Compression of the scanned images of the written pages (phase 3 of OCR_PLAN.md)
        PDFOCRCompressionSettings compression;

        /// Budget of the decoded images of the compression in bytes
        qint64 memoryBudget = qint64(1) << 30;

        /// The results are all results, because no selected page has a result (summary only)
        bool usedAllResults = false;
    };

    struct PDF4QTLIBCORESHARED_EXPORT Plan
    {
        OutputMode outputMode = OutputMode::ModifyDocument;
        QString copyFileName;
        std::vector<PDFOCRTextLayerWriter::PageRequest> requests;
        std::map<PDFInteger, QByteArray> knownFingerprints;
        PDFOCRTextLayerWriter::Options writerOptions;
        PDFOCRCompressionSettings compression;
        qint64 memoryBudget = qint64(1) << 30;
        bool removeConformance = false;
        bool usedAllResults = false;

        /// Pages, which are not written, and the human readable reasons
        std::vector<PDFInteger> excludedPages;
        QStringList excluded;

        int unreviewedWords = 0;
        int uncertainWords = 0;
        int outsideDictionaryWords = 0;
        int replacedLayers = 0;

        /// Returns the pages to write
        std::vector<PDFInteger> getPages() const;

        bool hasRequests() const { return !requests.empty(); }
    };

    /// Creates the plan: selects the pages, which can be written (not review only,
    /// not of an export-only engine, with text or with an own layer to remove,
    /// unchanged page, valid geometry), and computes the statistics.
    static Plan createPlan(const Context& context, const Request& request);

    /// Sets the file name of the copy. Returns an error (translated), if the copy
    /// would overwrite the document; the plan is not changed then.
    static QString setCopyFileName(Plan& plan, const Context& context, const QString& copyFileName);

    /// Returns the lines of the summary before the application (PDF-02)
    static QStringList getSummary(const Plan& plan);

    /// Returns the warnings of the confirmation (incomplete layer, signatures,
    /// certification, tagged document, removed conformance declaration, lossy compression)
    static QStringList getWarnings(const Plan& plan, const Context& context);

    struct Result
    {
        /// Modified document, empty, if nothing was changed (identical layer) or on error
        PDFDocumentPointer document;

        PDFOCRTextLayerWriter::Report report;
        PDFOCRCompressionReport compressionReport;

        /// Error (translated); the document was not changed at all then (PDF-03)
        QString errorMessage;

        QString copyFileName;
        bool conformanceRemoved = false;

        /// Fingerprints of the written pages in the result document. They differ from
        /// the fingerprints of the recognition, when the images were compressed, so the
        /// session can be rebound to the new revision of the document.
        std::map<PDFInteger, QByteArray> fingerprints;

        bool isSuccess() const { return errorMessage.isEmpty(); }
    };

    /// Executes the plan above the immutable source document of the context. The
    /// changed pages are verified again, the images are compressed, the text layer
    /// is written, a copy is stripped of the unverified conformance declaration,
    /// every written layer is validated and a copy is written into its file. Can
    /// be executed in a worker thread. Exceptions are converted into the error.
    static Result execute(const Context& context, const Plan& plan, const PDFOperationControl* operationControl);

    /// Removes the own OCR layers of the pages (PDF-11). Removal of a layer changes
    /// the content, so a certified document or a document without the permission to
    /// modify it is refused.
    static Result removeLayers(const Context& context, const std::vector<PDFInteger>& pages, const PDFOperationControl* operationControl);
};

}   // namespace pdf

#endif // PDFOCRAPPLYPROCESSOR_H
