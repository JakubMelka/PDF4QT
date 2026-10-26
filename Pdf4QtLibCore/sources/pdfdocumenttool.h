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

#ifndef PDFDOCUMENTTOOL_H
#define PDFDOCUMENTTOOL_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolparameter.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <vector>

namespace pdf
{

class PDFCertificateStore;
class PDFCMSManager;
class PDFDocumentToolCatalogEntry;
class PDFOperationControl;
class PDFProgress;

// Tools of the catalog without a user interface (newapp_tools.md 2, plan of
// step 6, chapter 3). PROVISIONAL: the interfaces are minimal and may change
// until the pilot tool Compress of phase 2 is implemented. The GUI of phase 2
// (PDFDocumentToolShell / PDFDocumentToolRunner), MCP (step 7, newapp_mcp.md 6)
// and later PdfTool call the same PDFDocumentTool.

/// Input document of a tool: a separate copy of the document of the tab (the
/// immutable data are shared; the tool builds its changes by its own
/// PDFDocumentBuilder - the document of the tab is never changed until the result
/// is accepted)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInputDocument
{
public:
    PDFDocumentPointer document;    ///< The document (never null in a valid context)
    QByteArray originalData;        ///< Data of the original file (incremental save, signatures); may be empty
    QString fileName;               ///< Name of the file (for the suggested names of the results); may be empty
};

/// Context of one run of a tool: the inputs and the environment. A value - the
/// caller fills it in the GUI thread and passes it to the worker. The pointers
/// of the environment may be null; the objects must outlive the run and their
/// functions used by the tool must be thread safe (const functions of
/// PDFCMSManager are).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolContext
{
public:
    /// Input documents: none (PDFDocumentToolInputKind::None), the document of the
    /// tab (One) or more documents (Multiple, the first is the document of the tab)
    std::vector<PDFDocumentToolInputDocument> documents;

    /// Pages selected in the thumbnails of the first document (indices from 0)
    std::vector<PDFInteger> selectedPages;

    /// Current page of the view of the first document (index from 0, -1 = none)
    PDFInteger currentPage = -1;

    const PDFCMSManager* cmsManager = nullptr;              ///< Color management of the application
    QString authorName;                                     ///< Author of the new content (annotations, signatures)
    const PDFCertificateStore* certificateStore = nullptr;  ///< Trusted certificates
    QString temporaryFolder;                                ///< Folder of the temporary files of the run

    /// The first input document, nullptr = none
    const PDFDocument* getDocument() const;
};

/// Status of a finished run
enum class PDFDocumentToolResultStatus
{
    Success,        ///< The result is ready (documents, files, facts)
    Stopped,        ///< The user (or MCP) canceled the run; nothing of it is used
    Failed,         ///< An error, the message says why
    NothingToDo     ///< Nothing to change ("File cannot be reduced", 295:2182); counts as a use of the tool
};

/// Document of the result: a changed input document or a new document
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOutputDocument
{
public:
    enum class Kind
    {
        Changed,    ///< A new version of the input document inputIndex (Keep in this tab / Save as a copy / Overwrite)
        New         ///< A new document (Open in a new tab / Save as a file)
    };

    Kind kind = Kind::Changed;
    int inputIndex = 0;                                                     ///< Index of the changed input document
    PDFDocumentPointer document;                                            ///< The document
    PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::Reset;  ///< What changed (hints of the update)
    QString suggestedFileName;                                              ///< Suggested name of the file (without a folder)
};

/// Fact of the result dialog and of the preview ("24.8 MB → 6.1 MB")
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFact
{
public:
    QString id;         ///< Stable identifier for MCP ("fileSize")
    QString title;      ///< Translated title ("File size")
    QString value;      ///< Translated value ("24.8 MB → 6.1 MB")
};

/// Row of the steps of the result (an icon, a text and a state - no text logs)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolStep
{
public:
    enum class State
    {
        Done,
        Warning,
        Failed,
        Skipped
    };

    QString iconName;   ///< Icon of Pdf4QtLibFluent/resources/icons (empty = by the state)
    QString text;       ///< Translated text
    State state = State::Done;
};

/// Result of a run (data only)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolResult
{
public:
    PDFDocumentToolResultStatus status = PDFDocumentToolResultStatus::Success;
    QString message;                                    ///< Reason of Failed / NothingToDo, a summary of Success (translated)
    std::vector<PDFDocumentToolOutputDocument> documents;   ///< Changed and new documents
    QStringList writtenFiles;                           ///< Files written by the tool (absolute paths)
    std::vector<PDFDocumentToolFact> facts;             ///< Facts of the result dialog
    std::vector<PDFDocumentToolStep> steps;             ///< Rows of the steps
    QJsonObject data;                                   ///< Structured data of the tool for MCP

    static PDFDocumentToolResult createFailed(const QString& message);
    static PDFDocumentToolResult createStopped();
    static PDFDocumentToolResult createNothingToDo(const QString& message);

    bool isSuccess() const { return status == PDFDocumentToolResultStatus::Success; }

    /// The run counts as a use of the tool (Success or NothingToDo, newapp_tools.md 3)
    bool isUse() const { return status == PDFDocumentToolResultStatus::Success || status == PDFDocumentToolResultStatus::NothingToDo; }

    /// Structured content of MCP: "status", "message", "documents" (kind, input
    /// index, page count, flags, suggested name), "files", "facts", "steps", "data"
    QJsonObject toJson() const;

    /// Name of the status in JSON ("success", "stopped", "failed", "nothing-to-do")
    static QString getStatusName(PDFDocumentToolResultStatus status);
};

/// A tool of the catalog (newapp_tools.md 2, Běh). One instance serves all runs:
/// every function is const and reentrant - it works only with its arguments, never
/// with the user interface or a mutable global state, checks the cancellation at
/// least after each page and may use PDFExecutionPolicy. The tool never writes
/// the resulting document itself (the result dialog, the parameter result of MCP
/// or PdfTool decide); exports of more files write them (PDFDocumentToolFileSink of
/// phase 2). PROVISIONAL until the pilot tool Compress.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTool
{
public:
    PDFDocumentTool() = default;
    virtual ~PDFDocumentTool();

    PDFDocumentTool(const PDFDocumentTool&) = delete;
    PDFDocumentTool& operator=(const PDFDocumentTool&) = delete;

    /// Identifier of the tool of PDFDocumentToolCatalog ("optimize.compress")
    virtual QString getId() const = 0;

    /// Entry of the catalog (texts, kinds, locked access, successors); nullptr for
    /// an id, which is not in the catalog (such a tool cannot be registered)
    const PDFDocumentToolCatalogEntry* getInfo() const;

    /// Definitions of the parameters (valid as a set, PDFDocumentToolParameters::isValid)
    virtual PDFDocumentToolParameterDefinitions getParameters() const = 0;

    /// Checks the context and the normalized parameters before a run. The default
    /// checks the input documents by the input kind of the catalog (None: any,
    /// One: exactly one, Multiple: at least one; never a null document).
    /// Returns the translated errors (empty = the tool can run).
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const;

    /// The tool computes a preview of its result (computePreview)
    virtual bool hasPreview() const { return false; }

    /// Preview of the result: facts only ("24.8 MB → 6.1 MB"), no documents. The
    /// default returns an empty successful result.
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control) const;

    /// Runs the tool with normalized and validated parameters. \p control and
    /// \p progress may be null; a tool, which sees the cancellation, returns Stopped.
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const = 0;

    /// Checked run of untrusted parameters (the runner of phase 2, MCP): the values
    /// are normalized by the definitions (defaults of the missing optional ones),
    /// validated with the context, then execute runs; an exception becomes Failed,
    /// a canceled operation Stopped. Errors of the validation give Failed with the
    /// messages joined and "errors" in data.
    PDFDocumentToolResult run(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const;

    /// Checked preview (as run, for computePreview)
    PDFDocumentToolResult runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control) const;

private:
    PDFDocumentToolResult prepare(const PDFDocumentToolContext& context, const QVariantMap& parameters, QVariantMap* normalizedParameters) const;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_H
