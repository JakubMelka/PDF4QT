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
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace pdf
{

class PDFCertificateStore;
class PDFDocumentToolCatalogEntry;
class PDFOperationControl;

// Tools of the catalog without a user interface (newapp_tools.md 2, plan of
// step 6, chapter 3, plan of the first batch of phase 2, chapter 2.2). The GUI
// of phase 2 (PDFDocumentToolShell / PDFDocumentToolRunner), MCP (step 7,
// newapp_mcp.md 6) and later PdfTool call the same PDFDocumentTool.

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
    bool isModified = true;         ///< The document may differ from \p originalData (unsaved changes). The safe default: only a caller, which knows that the bytes are of this very document, says false (PDFAppDocumentSnapshot::isModified); a size is then measured by writing it.

    /// Input document of a snapshot of an open document: the file name is taken from
    /// its path
    /// \param document The document of the snapshot
    /// \param originalData Data of the opened file
    /// \param filePath Path of the file, may be empty
    /// \param isModified The document differs from the data of the file
    static PDFDocumentToolInputDocument create(PDFDocumentPointer document, QByteArray originalData, const QString& filePath, bool isModified);
};

/// Context of one run of a tool: the inputs and the environment. A value - the
/// caller fills it in the GUI thread and passes it to the worker. It never holds a
/// pointer to an object of a view or of a window (they may be gone before the
/// worker ends): what it refers to is shared by the context and immutable.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolContext
{
public:
    /// Context of one input document (the usual case of a tool in a tab)
    static PDFDocumentToolContext create(PDFDocumentToolInputDocument input);

    /// Input documents: none (PDFDocumentToolInputKind::None), the document of the
    /// tab (One) or more documents (Multiple, the first is the document of the tab)
    std::vector<PDFDocumentToolInputDocument> documents;

    /// Pages selected in the thumbnails of the first document (indices from 0)
    std::vector<PDFInteger> selectedPages;

    /// Current page of the view of the first document (index from 0, -1 = none)
    PDFInteger currentPage = -1;

    PDFCMSPointer cms;                                              ///< Color management of the application, null = the default settings
    QString authorName;                                             ///< Author of the new content (annotations, signatures)
    std::shared_ptr<const PDFCertificateStore> certificateStore;    ///< Trusted certificates, may be null
    QString temporaryFolder;                                        ///< Folder of the temporary files of the run

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

/// File of the result, which the tool does not write itself: the result dialog or MCP
/// asks where it goes (one file of an export, tools.md 9.3 point 19, Extract Text).
/// Files written by the tool itself are in PDFDocumentToolResult::writtenFiles.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOutputFile
{
public:
    QString suggestedFileName;  ///< Suggested name of the file (without a folder)
    QByteArray data;            ///< Content of the file
    QString mimeType;           ///< Type of the content ("text/plain")
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
    std::vector<PDFDocumentToolOutputFile> outputs;     ///< Files, which the caller writes
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
    /// index, page count, flags, suggested name), "files", "facts", "steps", "data";
    /// "outputs" (suggested name, type, size - never the content) when there are any
    QJsonObject toJson() const;

    /// Name of the status in JSON ("success", "stopped", "failed", "nothing-to-do")
    static QString getStatusName(PDFDocumentToolResultStatus status);
};

/// Texts of the values of the tools, in the language of the user interface (the
/// default locale of the process, O17): "24.8 MB", "24.8 MB → 6.1 MB", "1 min 12 s"
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFormat
{
public:
    PDFDocumentToolFormat() = delete;

    /// Size of a file ("24.8 MB")
    static QString formatSize(qint64 bytes);

    /// Change of a size ("24.8 MB → 6.1 MB")
    static QString formatSizeChange(qint64 before, qint64 after);

    /// Time of a run ("1 min 12 s", "12 s"; at least 1 s)
    static QString formatDuration(qint64 milliseconds);

    /// How much smaller is the result, in whole percents (0 when it is not smaller)
    static int getSavedPercent(qint64 before, qint64 after);
};

/// Receiver of the progress of a run of a tool (the bar of the shell, the
/// notifications of MCP). It is called in the thread of the run, so it must hand the
/// values over by itself.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolProgressListener
{
public:
    virtual ~PDFDocumentToolProgressListener();

    /// \param percentage Progress of the run, 0 to 100
    /// \param stepText Text of the current step ("Recompressing images · 152 of 412"), may be empty
    virtual void onToolProgress(int percentage, const QString& stepText) = 0;
};

/// PDFProgress of a run, which tells its listener the percentage and the step: one
/// adapter of the GUI and of MCP. It lives in the thread of the run, on the stack of the run.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolProgressAdapter : public PDFProgress
{
public:
    /// \param listener Receiver, must outlive the adapter
    explicit PDFDocumentToolProgressAdapter(PDFDocumentToolProgressListener* listener);

private:
    void onStep(int percentage);
    void onStepText(QString text);

    PDFDocumentToolProgressListener* m_listener;
    int m_percentage = 0;
    QString m_stepText;
};

/// A tool of the catalog (newapp_tools.md 2, Běh). One instance serves all runs:
/// every function is const and reentrant - it works only with its arguments, never
/// with the user interface or a mutable global state, checks the cancellation at
/// least after each page and may use PDFExecutionPolicy. The tool never writes
/// the resulting document itself (the result dialog, the parameter result of MCP
/// or PdfTool decide); exports of more files write them (PDFDocumentToolFileSink of
/// phase 2).
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
    /// One: exactly one, Multiple: at least one; never a null document) and the
    /// permissions of an encrypted document (PDFDocumentToolCatalogEntry::isPermitted).
    /// A locked document is checked by the caller, the core does not know the lock.
    /// Returns the translated errors (empty = the tool can run).
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const;

    /// The tool computes a preview of its result (computePreview)
    virtual bool hasPreview() const { return false; }

    /// The preview is the result itself: it has the documents of the result, nothing
    /// is computed again when the user confirms it (Compress measures by running)
    virtual bool isPreviewFinal() const { return false; }

    /// Preview of the result: facts only ("24.8 MB → 6.1 MB"), no documents, unless
    /// isPreviewFinal. \p control and \p progress may be null. The default returns an
    /// empty successful result.
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const;

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
    PDFDocumentToolResult runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress = nullptr) const;

    /// Document of a tool, which rewrites the whole file (PDFDocumentToolSignatureImpact::LostCopyOnly:
    /// Compress, Optimize Structure, Remove Hidden Data; owner 2026-11-02). The containers of the
    /// organization of the file, which the written file would carry as dead objects next to
    /// the unpacked copies of their contents, are dropped also with the structure clean-up
    /// off (PDFObjectUtils::removeFileStructureObjects). The version is the version of the
    /// input (the optimizers would report PDF 2.0: a file does not change its version by
    /// being rewritten).
    /// \param storage Objects of the result (the last step of the tool)
    /// \param input Input document of the tool
    static PDFDocument createWholeFileDocument(PDFObjectStorage storage, const PDFDocument* input);

private:
    PDFDocumentToolResult prepare(const PDFDocumentToolContext& context, const QVariantMap& parameters, QVariantMap* normalizedParameters) const;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_H
