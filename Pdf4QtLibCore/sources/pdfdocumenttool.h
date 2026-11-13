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
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
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
    QString inputId;                ///< Id of the input of a tool of more inputs (PDFAppToolInputList, the parameter "inputs" of Merge); empty = the inputs are taken in the order of the context
    bool isOwnerPasswordVerified = false;   ///< The owner password of the original file was verified (PDFDocumentMerger::verifyOwnerPassword; a snapshot of an open tab with unsaved changes, owner answer Q15): the permission check of Merge (Q2) accepts the input as opened by its owner

    /// Input document of a snapshot of an open document: the file name is taken from
    /// its path
    /// \param document The document of the snapshot
    /// \param originalData Data of the opened file
    /// \param filePath Path of the file, may be empty
    /// \param isModified The document differs from the data of the file
    static PDFDocumentToolInputDocument create(PDFDocumentPointer document, QByteArray originalData, const QString& filePath, bool isModified);
};

/// Input image of a tool of more inputs (Merge, plan of step 2 of phase 2, 5.3): the
/// bytes of an image file (or of an image of the clipboard, saved as PNG). A value: the
/// bytes are implicitly shared and never changed. The tool decodes them itself in the
/// worker (PDFImagePageFactory), under the limits of hostile images (R15): a page per
/// frame (every frame of a multi-page TIFF, at most PDFImagePageFactory::MAXIMUM_FRAME_COUNT),
/// the page size by the resolution of the image.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInputImage
{
public:
    QByteArray data;            ///< Bytes of the image file (never empty in a valid context)
    QByteArray format;          ///< Format of QImageReader ("jpeg", "png", "tiff"); empty = by the content
    QString fileName;           ///< Name of the file (texts of the result, left out inputs); "Pasted image" of the clipboard
    QString inputId;            ///< Id of the input (as PDFDocumentToolInputDocument::inputId); empty = the order of the context
    double dotsPerInch = 0.0;   ///< Resolution given by the caller; 0 = the resolution of the file (a value outside 72 to 2400, zero, negative or NaN gives 300, R15)
};

/// Writer of the files of a tool, whose result is more files (Split, Extract Pages one file per
/// page, the exports of Convert; plan of step 3 of phase 2, 4.2, 9.3, R3, R14): the tool never
/// writes a file itself, it gives every file to the sink of its context.
///
/// Contract:
///  - Ownership: the caller of the run owns the sink (the runner of the application,
///    PDFAppToolFileSink, by a std::unique_ptr for the time of the run; a test or PdfTool its
///    own) and keeps it alive until the run has returned; PDFDocumentToolContext::fileSink is
///    only a pointer. The sink is no QObject and holds nothing of a view or a window: the
///    folder, the policy and the rules of the paths are plain values given to its constructor.
///  - Threads: the tool calls begin, write and isStopped from the one thread of its run, in the
///    order of the files (the parts may be computed in parallel, they are written one by one -
///    the order and Stop). getWrittenFiles and getNotWrittenFiles are read after the run has
///    returned (the caller hands the result to the GUI thread). The base class keeps its lists
///    without a lock.
///  - Existing files: the policy of the constructor. Ask (the GUI only, 356:11997, R3) - an
///    implementation stops in write at the first conflict, the GUI polls its state and answers
///    through a thread-safe object, the thread waits by PDFThreadAnnotations::wait and checks
///    the cancellation; MCP never asks (R17). A name equal to the path of a source or of a file
///    open in a tab is a conflict, which Overwrite does not overwrite (R14).
///  - A file is written whole or not at all (QSaveFile): Stop keeps the files written before.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFileSink
{
public:
    /// What happens with a file, which exists already
    enum class ExistingFiles
    {
        Ask,        ///< A question at the first conflict (the GUI, 356:11997)
        Overwrite,  ///< The file is replaced (never a source or a file open in a tab)
        Skip,       ///< The file is not written, it is in getNotWrittenFiles
        Rename      ///< The name gets " (2)", " (3)", ... (383:4447)
    };

    explicit PDFDocumentToolFileSink(ExistingFiles policy);
    virtual ~PDFDocumentToolFileSink();

    PDFDocumentToolFileSink(const PDFDocumentToolFileSink&) = delete;
    PDFDocumentToolFileSink& operator=(const PDFDocumentToolFileSink&) = delete;

    ExistingFiles getPolicy() const { return m_policy; }

    /// The run starts: the names of all files it will write (no folder, with the extension) in
    /// their order, so the conflicts are counted before the first file ("37 files", R3). The
    /// default accepts them. \returns false and the translated reason, when the run cannot start.
    virtual bool begin(const QStringList& fileNames, QString* errorText);

    /// Writes one file of the run: \p fileName without a folder (a name given to begin), the
    /// bytes of the whole file. \returns false and the translated reason, when it was not written
    /// (skipped, an error, stopped) - the name is then in getNotWrittenFiles.
    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) = 0;

    /// The sink has been stopped (Stop Export of the question): the tool writes no further file
    /// and ends as Stopped. The default is never stopped.
    virtual bool isStopped() const;

    /// Absolute paths of the files written in their order
    const QStringList& getWrittenFiles() const { return m_writtenFiles; }

    /// Names of the files, which were not written, in their order
    const QStringList& getNotWrittenFiles() const { return m_notWrittenFiles; }

protected:
    /// An implementation records what has happened with a file
    void addWrittenFile(const QString& filePath);
    void addNotWrittenFile(const QString& fileName);

private:
    ExistingFiles m_policy;
    QStringList m_writtenFiles;
    QStringList m_notWrittenFiles;
};

/// A file sink, which writes the files into one folder (package A of step 3a of phase 2: the
/// application derives PDFAppToolFileSink from it, PdfTool separate and the tests use it): each
/// file by QSaveFile - whole or not at all -, the policies Overwrite, Skip and Rename, a file,
/// which must never be overwritten (isProtectedPath), the names written in this run counted, so
/// Rename never takes a name of a later file of the run. Ask is decided by resolveConflict: the
/// base cannot ask and skips the file (the application asks, 356:11997).
///
/// Threads: as its base - one thread of the run, the lists read after it.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFolderSink : public PDFDocumentToolFileSink
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolFolderSink)

public:
    /// \param folder The folder of the files (it must exist)
    /// \param policy What happens with a file, which exists already
    explicit PDFDocumentToolFolderSink(QString folder, ExistingFiles policy);
    virtual ~PDFDocumentToolFolderSink() override;

    /// The folder exists, every name is a plain file name (checkFileName), no two names are equal
    /// (case insensitive); the names of existing files are counted (getConflictFiles)
    virtual bool begin(const QStringList& fileNames, QString* errorText) override;

    /// Writes the file into the folder by the policy (see the class)
    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) override;

    /// The folder (an absolute clean path)
    const QString& getFolder() const { return m_folder; }

    /// Names given to begin, whose files existed then (the conflicts of the run, "37 files", R3)
    const QStringList& getConflictFiles() const { return m_conflictFiles; }

    /// Absolute paths of the written files, which existed before and were overwritten (Delete
    /// Written Files never deletes them, K46)
    const QStringList& getOverwrittenFiles() const { return m_overwrittenFiles; }

    /// Text of the last reason, why a file was not written (empty = none)
    const QString& getLastProblem() const { return m_lastProblem; }

protected:
    /// What happens with \p fileName, whose file exists (\p isProtected: it must never be
    /// overwritten); the default is the policy, Ask = Skip. A subclass may ask the user; it
    /// returns Skip after a stop (isStopped).
    virtual ExistingFiles resolveConflict(const QString& fileName, bool isProtected);

    /// The path is a file, which is never overwritten (a source of the run, a file open in a
    /// tab, R14); the default protects nothing
    virtual bool isProtectedPath(const QString& filePath) const;

    /// A plain file name of the folder; the default accepts a name, which
    /// PDFFileNameTemplate::sanitize keeps unchanged
    virtual bool checkFileName(const QString& fileName, QString* errorText) const;

private:
    /// A free name "<base> (2).<ext>", "(3)", ...: no file of the folder, no name of the run
    QString getRenamedFileName(const QString& fileName) const;

    /// The key of a name in the sets (case insensitive)
    static QString getKey(const QString& fileName);

    QString m_folder;
    QStringList m_conflictFiles;
    QStringList m_overwrittenFiles;
    QSet<QString> m_reservedNames;      ///< Names of the run (begin) and the names written, keys
    QString m_lastProblem;
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

    /// Input images of a tool of more inputs (Merge; the documents and the images together
    /// are its inputs, ordered by its parameter "inputs" through their input ids; without
    /// ids the documents come first, then the images)
    std::vector<PDFDocumentToolInputImage> images;

    /// Pages selected in the thumbnails of the first document (indices from 0)
    std::vector<PDFInteger> selectedPages;

    /// Current page of the view of the first document (index from 0, -1 = none)
    PDFInteger currentPage = -1;

    PDFCMSPointer cms;                                              ///< Color management of the application, null = the default settings
    QString authorName;                                             ///< Author of the new content (annotations, signatures)
    std::shared_ptr<const PDFCertificateStore> certificateStore;    ///< Trusted certificates, may be null
    QString temporaryFolder;                                        ///< Folder of the temporary files of the run

    /// Writer of the files of a tool, whose result is more files (PDFDocumentToolFileSink: owned
    /// by the caller of the run for its whole time, used only by the thread of the run);
    /// nullptr = none (a tool, which needs it, fails without it)
    PDFDocumentToolFileSink* fileSink = nullptr;

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

    /// The file of \p document, when the tool has written it already (a New document,
    /// whose size it measured by writing it, plan of step 2 of phase 2, 4.5): exactly the
    /// bytes PDFDocumentWriter writes for \p document, so the caller opens or saves these
    /// bytes and never writes the document a second time. Empty = not written (the caller
    /// writes \p document itself).
    QByteArray writtenData;
};

/// Input of a tool of more inputs, which is not in the result (a partial result, Figma
/// 378:4376, owner answer Q5: only when the copy of the input fails during the run)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolLeftOutInput
{
public:
    QString inputId;            ///< Id of the input (PDFDocumentToolInputDocument::inputId), may be empty
    int inputIndex = -1;        ///< Index of the input in the order of the tool (the letter A = 0)
    QString name;               ///< Name of the input ("Annex_A.pdf")
    PDFInteger pageCount = 0;   ///< Pages of the input, which the result should have had
    QString reason;             ///< Translated reason ("The file cannot be read: ...")
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
    QStringList notWrittenFiles;                        ///< Files of the run, which were not written (names; skipped, failed or after Stop - R3 stopped, PDFDocumentToolFileSink)
    std::vector<PDFDocumentToolFact> facts;             ///< Facts of the result dialog
    std::vector<PDFDocumentToolStep> steps;             ///< Rows of the steps
    std::vector<PDFDocumentToolLeftOutInput> leftOutInputs;    ///< Inputs, which are not in the result (Success = a partial result; empty = complete)
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

/// What the output of a run of a tool with its parameters is (plan of step 3b of phase 2, RB3 a):
/// the values of the catalog, which a tool with more outputs decides by its parameters - Manage
/// Pages This document changes the open document (Keep, an incremental update of a signed file),
/// One PDF is a new document of its inputs, One PDF per group or per page are files. The rules of
/// the result dialog, of the writer of a copy, of the permissions and of MCP read this one value
/// instead of the catalog entry (PDFDocumentTool::getOutputTraits). A plain value: copied freely,
/// also into a thread; \p entry points into the static catalog (never deleted).
///
/// The constructor of a catalog entry is implicit on purpose: a caller without parameters (the
/// start page, a test, PdfTool) passes the entry and gets the values of its row - the same values
/// a tool without its own rule gives for every parameter.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOutputTraits
{
public:
    PDFDocumentToolOutputTraits() = default;

    /// The values of the row of the catalog (nullptr = no entry: ChangeDocument, no preselected
    /// choice, no signature impact, no new document - as the tool of an unknown id)
    PDFDocumentToolOutputTraits(const PDFDocumentToolCatalogEntry* catalogEntry);

    const PDFDocumentToolCatalogEntry* entry = nullptr;                             ///< The entry of the catalog (titles, locked access), may be null
    PDFDocumentToolResultKind resultKind = PDFDocumentToolResultKind::ChangeDocument;
    PDFDocumentToolPreselectedResult preselectedResult = PDFDocumentToolPreselectedResult::None;
    PDFDocumentToolSignatureImpact signatureImpact = PDFDocumentToolSignatureImpact::None;
    bool isNewDocumentOfInputs = false;     ///< PDFDocumentToolCatalogEntry::isNewDocumentOfInputs of the output
    bool isPageChange = false;              ///< PDFDocumentToolCatalogEntry::isPageChange of the output (PDFDocMDPPermissions::isCertificationRemovedBy)
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

    /// Kind of the result of a run with the normalized parameters (plan of step 3 of phase 2,
    /// 4.2: Extract Pages one file = NewDocument, one file per page = Files); the shell chooses
    /// the text of the Primary button and the dialog by it, MCP the value of result. The default
    /// is the kind of the catalog entry (ChangeDocument without an entry).
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const;

    /// Every kind, which getResultKind can give (the values of the parameter result of the
    /// schema of MCP); the default is PDFDocumentToolCatalogEntry::getPossibleResultKinds
    virtual std::vector<PDFDocumentToolResultKind> getPossibleResultKinds() const;

    /// How many inputs (documents and images of the context) the tool takes - the bounds of
    /// documentIds and imagePaths of MCP (K41); the default is
    /// PDFDocumentToolCatalogEntry::getInputCountRange (0 to 0 without an entry)
    virtual PDFDocumentToolInputCountRange getInputCountRange() const;

    /// The choice of the result dialog preselected for a run with the normalized parameters, and
    /// the default of the parameter result of MCP (plan of step 3b of phase 2, RB3 a: Manage Pages
    /// This document Keep, One PDF Open in a new tab). The default is the value of the catalog entry
    /// (None without an entry).
    virtual PDFDocumentToolPreselectedResult getPreselectedResult(const QVariantMap& parameters) const;

    /// What the result of a run with the normalized parameters does with the signatures of a
    /// signed document (RB3 a: an incremental update of the copy is written only for
    /// IncrementalSave). The default is the value of the catalog entry (None without an entry).
    virtual PDFDocumentToolSignatureImpact getSignatureImpact(const QVariantMap& parameters) const;

    /// The result of a run with the normalized parameters is one or more new documents of the pages
    /// of the inputs, not encrypted: every restriction of an input would be lost, the run needs the
    /// permissions of PDFDocumentMerger::getRequiredPermissions (owner answer Q2 of step 2, RB3 a).
    /// The default is PDFDocumentToolCatalogEntry::isNewDocumentOfInputs (false without an entry).
    virtual bool isNewDocumentOfInputs(const QVariantMap& parameters) const;

    /// A run with the normalized parameters changes the pages of the open document, which no level
    /// of a certification allows (ISO 32000-2, 12.8.2.2, Table 257): on a certified document the
    /// run removes the certification and its result is only a copy (PDFDocMDPPermissions::
    /// isCertificationRemovedBy - the one rule of the run, of the callout of the panel and of the
    /// result; owner 13. 11. 2026). The default is PDFDocumentToolCatalogEntry::isPageChange
    /// (false without an entry).
    virtual bool isPageChange(const QVariantMap& parameters) const;

    /// The values above and the entry together (the one value of the rules of the
    /// application, PDFDocumentToolOutputTraits); not virtual
    PDFDocumentToolOutputTraits getOutputTraits(const QVariantMap& parameters) const;

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
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const;

    /// Runs the tool with normalized and validated parameters. \p control and
    /// \p progress may be null; a tool, which sees the cancellation, returns Stopped.
    /// The progress is called by the threads of the run (PDFProgressSink), a reader polls it.
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const = 0;

    /// Checked run of untrusted parameters (the runner of phase 2, MCP): the values
    /// are normalized by the definitions (defaults of the missing optional ones),
    /// validated with the context, then execute runs; an exception becomes Failed,
    /// a canceled operation Stopped. Errors of the validation give Failed with the
    /// messages joined and "errors" in data. A run, which is a use of the tool (Success,
    /// NothingToDo), ends with a full bar (PDFProgressSink::finishPhases).
    PDFDocumentToolResult run(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const;

    /// Checked preview (as run, for computePreview)
    PDFDocumentToolResult runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress = nullptr) const;

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

    /// Largest file the application writes (PDFAppDocumentFileWriter::MAXIMUM_FILE_SIZE)
    static constexpr qint64 MAXIMUM_RESULT_SIZE = qint64(2047) * 1024 * 1024;

    /// Size of the input of a tool, which rewrites the whole file: the size of the file the user
    /// sees; a document with unsaved changes has no such file, its size is measured as the size
    /// of a result is. Negative: it cannot be measured.
    /// \param input The input document
    static qint64 getWholeFileInputSize(const PDFDocumentToolInputDocument& input);

    /// Size of the written file of a result, and the failure of a result, which cannot
    /// be written or is too large for the application
    struct WholeFileSize
    {
        qint64 size = -1;   ///< Bytes of the written file, negative when \p error is set
        QString error;      ///< Translated reason of a failure
    };

    /// Measures the file of a result by writing it
    /// \param output The result
    static WholeFileSize getWholeFileOutputSize(const PDFDocument* output);

    /// The bytes of the whole file of a document (PDFDocumentWriter): a new document of a tool
    /// (the parts of Split, PDFDocumentToolOutputDocument::writtenData). Empty and the translated
    /// reason, when it cannot be written or is larger than MAXIMUM_RESULT_SIZE. Reentrant.
    /// \param document The document
    /// \param errorMessage Reason of a failure
    static QByteArray writeDocumentData(const PDFDocument* document, QString* errorMessage);

    /// When a result of a tool, which rewrites the whole file, gets its document
    enum class WholeFileDocument
    {
        WhenSmaller,    ///< The size is the purpose (Compress, Optimize Structure): a file, which is not smaller, gets none, the caller has returned NothingToDo
        Always          ///< The removal is the purpose (Remove Hidden Data): a rewritten file can be larger, what was removed still has to go
    };

    /// Finishes the result of a tool, which rewrites the whole file: "sizeBefore", "sizeAfter",
    /// "savedBytes", "savedPercent", "signatureCount", "signatureImpact" in data, and - by
    /// the rule - the changed document with its suggested name "<name><suffix>.pdf".
    /// \param result The result of the tool
    /// \param input The input document
    /// \param output The document of the result
    /// \param sizeBefore Size of the input (getWholeFileInputSize)
    /// \param sizeAfter Size of the output (getWholeFileOutputSize)
    /// \param fileNameSuffix Suffix of the suggested name ("_compressed")
    /// \param documentRule When the result gets its document
    void finishWholeFileResult(PDFDocumentToolResult* result, const PDFDocumentToolInputDocument& input, PDFDocument output, qint64 sizeBefore, qint64 sizeAfter, const QString& fileNameSuffix, WholeFileDocument documentRule = WholeFileDocument::WhenSmaller) const;

    /// What was done with the declaration of conformance of a rewritten document
    struct ConformanceRemoval
    {
        QStringList removed;    ///< Standards no longer declared ("PDF/A", "PDF/UA")
        QStringList kept;       ///< Standards declared by metadata, which cannot be changed
    };

    /// Removes the declaration of PDF/A and PDF/UA of a document, whose content a tool has
    /// rewritten: the result is not checked against the standard, so it must not say it
    /// conforms (the rule of the copies of OCR, PDF-15). The removal is the one of
    /// PDFOCRTextLayerWriter::removeConformanceDeclaration.
    /// \param storage Objects of the result, the metadata are replaced in place
    /// \param document The document of \p storage
    static ConformanceRemoval removeConformanceDeclaration(PDFObjectStorage* storage, const PDFDocument* document);

    /// The result said in plain words, the fact of the result dialog and of What changes
    /// \param removed The standards, which are no longer declared (ConformanceRemoval::removed)
    static PDFDocumentToolFact createConformanceFact(const QStringList& removed);

    /// The standards, which the document declares ("PDF/A", "PDF/UA", "PDF/X"): PDF/A and PDF/UA
    /// by the XMP metadata (PDFOCRPagePreparer::hasConformanceDeclaration), PDF/X by an output
    /// intent of the subtype GTS_PDFX (ISO 32000-2, 14.11.5). A tool, which adds pages without
    /// checking them, keeps the declarations and says so (Q18: Insert Pages, Manage Pages).
    /// \param document The document (nullptr = none)
    static QStringList getConformanceDeclarations(const PDFDocument* document);

    /// Document of a tool, which rewrites the whole file, without the declaration of PDF/A and
    /// PDF/UA (the last step of Compress and Optimize Structure): createWholeFileDocument and
    /// removeConformanceDeclaration in their order.
    /// \param storage Objects of the result
    /// \param input Input document of the tool
    /// \param conformance What was done with the declaration; nullptr = nothing is removed (a canceled run)
    static PDFDocument createWholeFileDocument(PDFObjectStorage storage, const PDFDocument* input, ConformanceRemoval* conformance);

    /// A result, which would keep a declaration that is not true: the metadata of the document
    /// cannot be changed, so the tool has made nothing
    /// \param kept The standards (ConformanceRemoval::kept)
    static PDFDocumentToolResult createConformanceFailure(const QStringList& kept);

    /// Tells the result, that it no longer declares the standards: the fact "conformance" and
    /// "conformanceRemoved" in data; nothing for an empty list
    /// \param result The result of the tool
    /// \param removed The standards (ConformanceRemoval::removed)
    static void addConformanceFact(PDFDocumentToolResult* result, const QStringList& removed);

    /// Checks the parameters a caller has written explicitly (an assistant by MCP), before they
    /// are normalized: a value, which would be silently ignored, is an error, which names what
    /// to set. The panels always hold all values, they do not call it. The default finds nothing.
    /// \param given The parameters as they were given
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const;

    /// The values, whose default depends on another given value, added to the parameters as they
    /// were given, before they are normalized (run, runPreview): a caller, who gives only some
    /// values (an assistant by MCP), gets the default of the combination it gave (Blank PDF: a
    /// 16:9 page without an orientation is landscape, owner answer Q22; Insert: blank pages
    /// "As page N" without an orientation take the orientation of that page, owner D6). The
    /// panels hold every value, nothing is added for them. The default adds nothing.
    /// \param context The documents of the run (a default may depend on a page of them)
    /// \param given The parameters as they were given
    virtual QVariantMap completeGivenParameters(const PDFDocumentToolContext& context, const QVariantMap& given) const;

    /// The parameters given for the document \p from, made for \p to, another version of it
    /// (MCP runs a tool again on a new version; an open tool keeps its values, when an
    /// assistant changes the pages below it): a page named by its number follows its page
    /// object (PDFDocumentToolPageSelection::mapPages), a page, which is gone, is left out.
    /// The default changes nothing (a tool, whose parameters name no page).
    /// \param parameters The parameters (normalized or as given)
    /// \param from The version, whose pages the parameters name
    /// \param to The other version
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const;

private:
    PDFDocumentToolResult prepare(const PDFDocumentToolContext& context, const QVariantMap& parameters, QVariantMap* normalizedParameters) const;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_H
