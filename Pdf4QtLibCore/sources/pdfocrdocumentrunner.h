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

#ifndef PDFOCRDOCUMENTRUNNER_H
#define PDFOCRDOCUMENTRUNNER_H

#include "pdfocrapplyprocessor.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrproject.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{
class PDFCMS;
class PDFOCRModelManager;
class PDFOCRSession;
class PDFOperationControl;

/// Recognition of a whole document without a user interface (phase 7 of OCR_PLAN.md).
/// The runner performs the same steps as the OCR dialog of the editor, only the
/// decisions of the user are given in advance by the settings:
///   1. analysis of the selected pages and the policy of the existing text,
///   2. the results of a project are reused for the pages with the same fingerprint,
///   3. the language models are resolved (never downloaded),
///   4. the pages are recognized by PDFOCRJobController,
///   5. the results are collected in a session, which can be written by
///      PDFOCRApplyProcessor (writeCopy), exported or saved as a project.
///
/// The runner is synchronous. The job controller delivers its results by queued
/// signals, so run must be called from a thread with an event dispatcher (the main
/// thread of the application or a QThread); it runs its own event loop meanwhile.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRDocumentRunner
{
public:
    /// What happens with the pages, which need a decision of the user in the dialog
    /// (pages with both text and images, ambiguous pages, regions over the text)
    enum class DecisionPolicy
    {
        Skip,               ///< The page is skipped (default, nothing is decided silently)
        MaskExistingText,   ///< A scan with a small existing text is recognized with the text masked, other pages are skipped
        ReviewOnly          ///< The page is recognized for the review and the export only
    };

    struct Settings
    {
        /// Configuration of the recognition (the configuration of the project is not used)
        PDFOCRConfiguration configuration;

        /// Selected pages (0-based), empty = all pages
        std::vector<PDFInteger> pages;

        DecisionPolicy decisionPolicy = DecisionPolicy::Skip;

        /// Project with the results and the corrections of the previous work. The results
        /// of the pages with the same fingerprint are used without a new recognition, the
        /// page exceptions of the configuration and the regions of these pages are used too.
        std::optional<PDFOCRProject> project;

        /// Pages too large for the requested resolution are recognized with a reduced
        /// resolution. Without it such a page is an error (IMAGE-01, explicit consent).
        bool allowReducedResolution = false;

        /// Model manager used to resolve the language models of the engines with
        /// managed models (required for them, the runner never downloads a model)
        PDFOCRModelManager* modelManager = nullptr;

        /// Color management system (optional, a generic one is used, if not set)
        const PDFCMS* cms = nullptr;

        RendererEngine rendererEngine = RendererEngine::QPainter;

        /// File name of the document (identity of the document in the project)
        QString fileName;
    };

    /// Origin of the result of the page
    enum class PageSource
    {
        Recognized,     ///< The page was recognized now
        Project,        ///< The result was taken from the project
        Skipped,        ///< The page was not recognized (policy of the existing text, resolution)
    };

    struct PageRecord
    {
        PDFInteger pageIndex = -1;
        QString pageLabel;
        PageSource source = PageSource::Recognized;
        PDFOCRPageState state = PDFOCRPageState::Pending;

        /// The result is for the review and the export only
        bool reviewOnly = false;

        /// The existing text of the page was masked
        bool masked = false;

        /// Reason of the skip, message of the error (translated)
        QString message;

        PDFOCRConfidenceStatistics statistics;
        qint64 elapsedMilliseconds = 0;
    };

    struct PDF4QTLIBCORESHARED_EXPORT Result
    {
        /// Session with all results (never null after run)
        std::shared_ptr<PDFOCRSession> session;

        /// Selected pages
        std::vector<PDFInteger> pages;

        /// Records of the selected pages, ordered by the page index
        std::vector<PageRecord> records;

        /// Analysis and fingerprints of the processed pages of the document
        std::map<PDFInteger, PDFOCRPageAnalysis> analysis;
        std::map<PDFInteger, QByteArray> fingerprints;

        /// Summary of the job (empty, if no page was recognized)
        PDFOCRJobSummary summary;

        /// Warnings (translated): changed pages of the project, reduced resolution, ...
        QStringList warnings;

        /// Critical error (translated), no page was recognized then
        QString errorMessage;
        PDFOCRErrorCode errorCode = PDFOCRErrorCode::None;

        bool cancelled = false;

        bool isSuccess() const { return errorMessage.isEmpty() && !cancelled; }

        /// Returns the number of the pages in the state Error or Cancelled
        int getFailedPageCount() const;

        /// Returns the number of the pages with a result (recognized or taken from the project)
        int getResultPageCount() const;

        /// Returns the statistics of all pages with a result
        PDFOCRConfidenceStatistics getStatistics() const;
    };

    /// Progress of the recognition: number of the finished pages, total number of the
    /// recognized pages and the finished page (nullptr for the preparation steps)
    using ProgressCallback = std::function<void(int finished, int total, const PDFOCRPageResult* page)>;

    /// Runs the recognition, see the description of the class
    static Result run(const PDFDocument* document,
                      const Settings& settings,
                      const ProgressCallback& progress,
                      const PDFOperationControl* operationControl);

    struct WriteOptions
    {
        /// File name of the source document (a copy never overwrites it)
        QString sourceFileName;

        /// File name of the created copy
        QString outputFileName;

        PDFOCRTextLayerWriter::Options writerOptions;
        PDFOCRCompressionSettings compression;
        qint64 memoryBudget = qint64(1) << 30;
    };

    /// Writes the results of the pages into a copy of the document by PDFOCRApplyProcessor
    /// (permissions, certification, conformance declaration, compression, validation). The
    /// plan is returned, so the caller can report the pages, which were not written.
    /// A blocking reason (permissions, the copy overwriting the source) is returned as the
    /// error of the result.
    static PDFOCRApplyProcessor::Result writeCopy(const PDFDocument* document,
                                                  const Result& result,
                                                  const WriteOptions& options,
                                                  PDFOCRApplyProcessor::Plan* plan,
                                                  const PDFOperationControl* operationControl);

    /// Returns the results of the selected pages, which can be exported (the pages
    /// without a result are reported by the exporters as skipped pages)
    static std::vector<const PDFOCRPageResult*> getExportResults(const Result& result);

    /// Recognition of a single file, shared by the command line tool and the batch
    /// dialog of the editor: the document is read, its permissions are checked before
    /// the recognition, the pages are recognized, the text layer is written into
    /// a copy, the text is exported and the results are saved as a project.
    struct FileTask
    {
        QString inputFile;
        QString password;

        /// Output document; empty = the document is not written (export only)
        QString outputFile;

        // Exports (empty = not exported)
        QString exportText;
        QString exportHocr;
        QString exportAlto;
        QString exportTsv;

        /// Resolution of the coordinates of the structured exports (0 = of the recognition)
        double exportDpi = 0.0;

        /// Only the reviewed words are written and exported
        bool onlyReviewed = false;

        /// Review data are stored in the document (PDF-10)
        bool keepReviewData = false;

        /// Project, whose results are used for the unchanged pages (empty = none)
        QString projectFile;

        /// The results are saved into the project (empty = not saved)
        QString saveProjectFile;

        /// The outputs are written even if the recognition of some pages failed
        bool allowPageErrors = false;

        /// Creates the configuration of the file from the configuration of the project
        /// (nullptr, if no project is used). Returns an error (translated) or an empty string.
        std::function<QString(const PDFOCRProject* project, PDFOCRConfiguration* configuration)> configure;

        /// Selects the pages of the document (optional, all pages otherwise). Returns
        /// an error (translated) or an empty string.
        std::function<QString(PDFInteger pageCount, std::vector<PDFInteger>* pages)> selectPages;
    };

    struct PDF4QTLIBCORESHARED_EXPORT FileResult
    {
        enum class Status
        {
            Success,    ///< All requested outputs were written
            Skipped,    ///< Nothing was written (permissions, no recognized page, nothing to write)
            Failed      ///< The file failed, nothing was written
        };

        /// Stage of the failure or of the skip
        enum class Stage
        {
            None,
            Reading,        ///< The document cannot be read
            Permissions,    ///< The document cannot be written (permissions, certification)
            Project,        ///< The project cannot be read
            Configuration,  ///< The configuration or the page selection is invalid
            Recognition,    ///< Critical error of the recognition, or it was cancelled
            PageErrors,     ///< Some pages failed and allowPageErrors is not set
            NothingToWrite, ///< No page has a text, which can be written
            Writing,        ///< The document, an export or the project was not written
        };

        Status status = Status::Failed;
        Stage stage = Stage::None;

        /// Error, or the reason of the skip (translated)
        QString message;

        /// Reports of the written outputs (translated)
        QStringList messages;

        /// Warnings of the recognition (translated)
        QStringList warnings;

        int pageCount = 0;
        Result result;
        PDFOCRApplyProcessor::Plan plan;
        PDFOCRApplyProcessor::Result writeResult;
        qint64 elapsedMilliseconds = 0;
    };

    /// Processes the file, see FileTask. The settings give the policy of the decisions,
    /// the model manager and the rendering; the configuration, the pages, the project
    /// and the file name are set from the task. The same requirements as for run apply.
    static FileResult processFile(const FileTask& task,
                                  const Settings& settings,
                                  const ProgressCallback& progress,
                                  const PDFOperationControl* operationControl);

    /// Returns the translated name of the page state
    static QString getPageStateName(PDFOCRPageState state);
};

}   // namespace pdf

#endif // PDFOCRDOCUMENTRUNNER_H
