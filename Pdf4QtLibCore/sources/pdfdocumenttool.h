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
#include "pdfcertificatestore.h"
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdfrenderer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFCertificateStore;
class PDFDocumentToolCatalogEntry;
class PDFOperationControl;
class PDFSpeechSynthesizerFactory;
class PDFTimestampClient;

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
    bool isWholeFileRewritten = false;      ///< The document of the tab was rewritten whole after \p originalData were read (Compress Keep, the encryption Keep; PDFAppDocument::isWholeFileRewritten): no incremental update of \p originalData is its file (plan of step 6 of phase 2, 4.4, R6-12). The application fills it, a caller without a tab leaves false.
    bool isRestored = false;                ///< \p originalData are a damaged file, which the reader restored (PDFAppDocument::getRestoration is not None): an incremental update of them would not be read by other readers (plan of step 6 of phase 2, 4.4, R6-12)

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

    /// The files of getNotWrittenFiles, which failed (an error, a name, which cannot be used, a
    /// protected file - not a skipped or a stopped one), each with its translated reason (Figma
    /// 339:7293 "3 of 1,592 files could not be written", step 4 of phase 2, K4.11): a pair of the
    /// name and the reason, in their order
    const std::vector<std::pair<QString, QString>>& getFailedFiles() const { return m_failedFiles; }

protected:
    /// An implementation records what has happened with a file
    void addWrittenFile(const QString& filePath);
    void addNotWrittenFile(const QString& fileName);

    /// A file, which failed: it is also a file, which was not written (addNotWrittenFile)
    void addFailedFile(const QString& fileName, const QString& reason);

private:
    ExistingFiles m_policy;
    QStringList m_writtenFiles;
    QStringList m_notWrittenFiles;
    std::vector<std::pair<QString, QString>> m_failedFiles;
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

/// How a run renders pages (PDF to Images, Black & White, the size estimate of PDF to Images; step
/// 4 of phase 2, R4-10 b, owner answer Q4.6): the color management and the renderer of the
/// settings of the application and the states of the layers of the tab. A value: the caller fills
/// it in the GUI thread from the settings and the tab and copies it into the context; the worker
/// only reads its copy (no pointer to a manager, a view or a document). An empty value is the
/// rendering without the application (PdfTool, a test): the default color management with the
/// output intents of the document, PDFRenderer::getDefaultFeatures and the engine of the tool, the
/// layers by OCUsage::Export (ISO 32000-2, 8.11.4.4; also MCP and the own tab of a tool, which
/// have no tab of the document).
///
/// A tool takes from \p features only what is content: the color modes of a view (ColorAdjust_*),
/// the debugging flags, DisplayTimes and IgnoreOptionalContent are never used for an output
/// (PDFDocumentToolPageRenderSettings::getExportFeatures).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRenderContext
{
public:
    std::optional<PDFCMSSettings> cmsSettings;                          ///< Color management of the settings (empty = the default settings)
    std::optional<PDFRenderer::Features> features;                      ///< Features of the renderer of the settings (empty = PDFRenderer::getDefaultFeatures)
    std::optional<RendererEngine> engine;                               ///< Engine of the settings (empty = the default of the tool)
    std::optional<std::map<PDFObjectReference, OCState>> layerStates;   ///< States of the layers of the tab (empty = OCUsage::Export); a layer, which is missing, keeps its state of /D
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
    PDFDocumentToolRenderContext rendering;                         ///< How the pages are rendered (R4-10 b); empty = without the application
    QString authorName;                                             ///< Author of the new content (annotations, signatures)
    std::shared_ptr<const PDFCertificateStore> certificateStore;    ///< Trusted certificates, may be null

    /// Certificates of the user with their private keys for a run, which encrypts for a recipient
    /// (Encrypt with Certificate, plan of step 5 of phase 2, 4.5, 7.2): copies of the entries with
    /// their PKCS#12 (PDFCertificateEntry::pkcs12) and the certificate read from it (info) - the GUI
    /// or the confirmation of MCP gives the chosen one, the run never reads the disk for it. The tool
    /// finds the recipient by the SHA-256 fingerprint of its parameter "certificate"
    /// (PDFDocumentToolEncryption::getCertificateFingerprint of info.getCertificateData()). The PKCS#12 is
    /// encrypted by its password (the Secret certificatePassword of the run); the context is dropped
    /// with the run.
    std::vector<PDFCertificateEntry> userCertificates;

    /// Folder of the temporary files of the run (PDFDocumentToolTemporaryFile, step 4 of phase 2,
    /// R4-1): the application gives its own folder, which it cleans of the leftovers of an earlier
    /// process at its start (PDFAppToolTemporaryFolder); empty = the temporary folder of the system
    QString temporaryFolder;

    /// Who creates the speech synthesizers of a run (Create Audio Book, step 4 of phase 2, R4-15):
    /// a plain value shared by the context, no QObject; the runner of the application, the bridge
    /// of MCP and PdfTool fill it from the library Pdf4QtSpeech, a test gives a fake one. Null =
    /// no engine: the tool fails with "No text-to-speech engine".
    std::shared_ptr<PDFSpeechSynthesizerFactory> speechSynthesizerFactory;

    /// Writer of the files of a tool, whose result is more files (PDFDocumentToolFileSink: owned
    /// by the caller of the run for its whole time, used only by the thread of the run);
    /// nullptr = none (a tool, which needs it, fails without it)
    PDFDocumentToolFileSink* fileSink = nullptr;

    /// The client of the RFC 3161 timestamp authority of a run, which asks one (Sign with Certificate
    /// with a timestamp, Add Timestamp; plan of step 6 of phase 2, 6.6, 10.3): a plain value shared by
    /// the context, no QObject, used only by the thread of the run (PDFTimestampClient). The runner of
    /// the application, the bridge of MCP and PdfTool give the network client, a test a client over a
    /// local test authority (PDFTimestampTransport) - never the internet in a test. Null = no
    /// authority: a run, which needs one, fails with the reason, nothing goes to the network.
    std::shared_ptr<const PDFTimestampClient> timestampClient;

    /// The native handle of the window, to which a dialog of the system shown by the run belongs - the
    /// PIN dialog of a key of the certificate store of Windows (PDFSignatureFactory::unlock, plan of step
    /// 6 of phase 2, Q6.2): an opaque number (an HWND on Windows, QWidget::winId of the main window of the
    /// tool), never a pointer the run dereferences; the runner of the application fills it, 0 = none (MCP
    /// without a window, PdfTool, tests).
    quintptr parentWindow = 0;

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
    /// writes \p document itself). A Changed document has them, when the tool has verified
    /// its written file (the encryption of step 5 of phase 2 reads it back by the new
    /// password): a copy and an overwrite save them (PDFAppToolResultWriter::createCopyRequest).
    /// A tool, whose result is an incremental update of the input (Sign with Certificate, Add
    /// Timestamp; plan of step 6 of phase 2, 4.4, R6-27), gives the whole file it has verified -
    /// the exact bytes of PDFDocumentToolInputDocument::originalData followed by its update: they are
    /// the only valid form of its result, \p document is only their parsed copy (a new write of it
    /// would destroy every signature). Its result is only this file (PDFDocumentToolCatalogEntry::
    /// isFileOnlyResult): the result dialog and MCP save or open these bytes, never \p document.
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

/// A temporary file of a result (step 4 of phase 2, R4-1: an audio book of hundreds of MB is never
/// held in the memory). The result owns it by a std::shared_ptr: the file is deleted, when the
/// last copy of the result goes - Discard, the close of the tab, a result of MCP, which is dropped,
/// an error of the run. Saving it moves it (moveTo), then nothing is deleted.
///
/// Threads: created by the thread of the run, then read by one thread at a time (the worker of the
/// writer, PDFAppToolResultWriter::writeOutputFile); the destructor runs in the thread of the last
/// owner. moveTo and the destructor are serialized by a mutex.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolTemporaryFile
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolTemporaryFile)

public:
    /// Deletes the file, unless it has been moved
    ~PDFDocumentToolTemporaryFile();

    PDFDocumentToolTemporaryFile(const PDFDocumentToolTemporaryFile&) = delete;
    PDFDocumentToolTemporaryFile& operator=(const PDFDocumentToolTemporaryFile&) = delete;

    /// Creates a new empty file with a unique name ("pdf4qt-XXXXXX<suffix>") in \p folder
    /// \param folder Folder of the file (PDFDocumentToolContext::temporaryFolder); empty = the temporary folder of the system
    /// \param suffix Suffix of the name with its dot (".wav"), may be empty
    /// \param errorMessage Translated reason of a failure
    /// \returns The file, nullptr on a failure
    static std::shared_ptr<PDFDocumentToolTemporaryFile> create(const QString& folder, const QString& suffix, QString* errorMessage);

    /// Absolute path of the file (the tool writes into it; a moved file is no longer there)
    const QString& getFilePath() const { return m_filePath; }

    /// Size of the file in bytes, -1 when it does not exist (moved, deleted)
    qint64 getSize() const;

    /// Renames the file to \p targetPath, which must not exist (QFile::rename on one volume;
    /// the caller copies the file otherwise). On a success the object forgets the file: the
    /// destructor does not delete the target.
    /// \returns false and the reason, when it was not moved (the file stays a temporary file)
    bool moveTo(const QString& targetPath, QString* errorMessage);

    /// The file has been moved by moveTo
    bool isMoved() const;

    /// Copies the file to \p targetPath by QSaveFile (in blocks): an existing target is replaced
    /// only when the copy is complete, a failed copy leaves it as it was (PdfTool --overwrite).
    /// The temporary file stays (its owner deletes it).
    /// \returns false and the reason, when the target was not written
    bool copyTo(const QString& targetPath, QString* errorMessage) const;

private:
    explicit PDFDocumentToolTemporaryFile(QString filePath);

    QString m_filePath;
    mutable QMutex m_mutex;
    bool m_isMoved = false;
};

/// File of the result, which the tool does not write itself: the result dialog or MCP
/// asks where it goes (one file of an export, tools.md 9.3 point 19, Extract Text).
/// Files written by the tool itself are in PDFDocumentToolResult::writtenFiles.
///
/// The content is either \p data in the memory or a temporary file (\p temporaryFile, step 4
/// of phase 2, R4-1: the audio book; \p data is then empty). Every place, which writes or reads
/// an output, goes through PDFAppToolResultWriter::writeOutputFile (the application) or readContent
/// (a text of MCP), never through \p data alone.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOutputFile
{
public:
    QString suggestedFileName;  ///< Suggested name of the file (without a folder)
    QByteArray data;            ///< Content of the file (empty, when it is in \p temporaryFile)
    QString mimeType;           ///< Type of the content ("text/plain")

    /// The temporary file of the content (R4-1), nullptr = the content is \p data. Copies of the
    /// output share it; the last one deletes it.
    std::shared_ptr<PDFDocumentToolTemporaryFile> temporaryFile;

    /// An output, whose content is a temporary file
    static PDFDocumentToolOutputFile createTemporary(QString suggestedFileName, QString mimeType, std::shared_ptr<PDFDocumentToolTemporaryFile> file);

    /// The content is in a temporary file
    bool isTemporaryFile() const { return temporaryFile != nullptr; }

    /// Path of the temporary file of the content (R4-1: PDFDocumentToolOutputFile::filePath), empty = \p data
    QString getFilePath() const;

    /// Size of the content in bytes (of the temporary file or of \p data), -1 when the temporary file is gone
    qint64 getSize() const;

    /// Reads at most \p maximumSize bytes of the content (a text of MCP, return_text)
    /// \param maximumSize Most bytes read
    /// \param isTruncated The content is longer than \p maximumSize (optional)
    /// \param errorMessage Translated reason, when the temporary file cannot be read (optional)
    QByteArray readContent(qint64 maximumSize, bool* isTruncated, QString* errorMessage) const;
};

/// A page of an export, whose picture may be incomplete (Rendering issues of 339:6505, step 4 of
/// phase 2, K4.11, R4-16): the errors of the page of PDFRenderedPageImage::errors, without the
/// level Information
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolPageProblem
{
public:
    PDFInteger pageIndex = -1;  ///< Page (index from 0)
    QString message;            ///< Translated text of the problem
};

/// A file of an export, which could not be written (Failed of 339:7293, K4.11)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFileProblem
{
public:
    QString fileName;   ///< Name of the file (without a folder)
    QString reason;     ///< Translated reason ("Access denied")
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
    std::vector<PDFDocumentToolPageProblem> renderingIssues;    ///< Pages of an export, whose pictures may be incomplete (339:6505 Rendering issues; step 4, K4.11)
    std::vector<PDFDocumentToolFileProblem> failedFiles;        ///< Files of \p notWrittenFiles, which failed, with their reasons (339:7293 Failed; PDFDocumentToolFileSink::getFailedFiles)
    QJsonObject data;                                   ///< Structured data of the tool for MCP

    static PDFDocumentToolResult createFailed(const QString& message);
    static PDFDocumentToolResult createStopped();
    static PDFDocumentToolResult createNothingToDo(const QString& message);

    bool isSuccess() const { return status == PDFDocumentToolResultStatus::Success; }

    /// The run counts as a use of the tool (Success or NothingToDo, newapp_tools.md 3)
    bool isUse() const { return status == PDFDocumentToolResultStatus::Success || status == PDFDocumentToolResultStatus::NothingToDo; }

    /// Structured content of MCP: "status", "message", "documents" (kind, input
    /// index, page count, flags, suggested name), "files", "facts", "steps", "data";
    /// "outputs" (suggested name, type, size - never the content) when there are any;
    /// "renderingIssues" (page from 1, message) and "failedFiles" (fileName, reason) when
    /// there are any
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

    /// Size of a file in the numbers of \p locale (the machine-readable facts of the review tools: the C locale)
    static QString formatSize(qint64 bytes, const QLocale& locale);

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

    /// Permissions of an encrypted document (ISO 32000-2, 7.6.4.2, Table 22), which a run with the
    /// normalized parameters needs: every one of them must be granted (plan of step 6 of phase 2, R6-8 -
    /// Sign with Certificate needs bits 4 and 6 for a new signature field, bit 9 or 6 for an existing
    /// empty field). The default is the mask of the catalog entry
    /// (PDFDocumentToolCatalogEntry::getRequiredPermissions; none without an entry). The validation of
    /// the core (validate) and the application (PDFAppDocumentPermissions, a request with the parameters
    /// of the run) ask it; a tool, whose output makes new documents of its inputs, needs the permissions
    /// of PDFDocumentMerger::getRequiredPermissions besides (isNewDocumentOfInputs). Reentrant.
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const;

    /// Permissions of an encrypted document (Table 22), of which any one is enough for a run with the
    /// normalized parameters, besides every one of getRequiredPermissions; empty = no such choice (the
    /// default). Sign with Certificate into an existing empty field: bit 9 or bit 6 ("fill in existing
    /// interactive form fields (including signature fields)" / "fill in interactive form fields", plan of
    /// step 6 of phase 2, R6-8, RS6APP M10). The validation of the core (validate, isPermittedRun), the
    /// signer and the application (PDFAppDocumentPermissions) ask it with getRequiredPermissions. Reentrant.
    virtual std::vector<PDFSecurityHandler::Permission> getAnyOfPermissions(const QVariantMap& parameters) const;

    /// The security handler of the document grants a run with the normalized parameters: every one of
    /// getRequiredPermissions and, when there are any, at least one of getAnyOfPermissions
    bool isPermittedRun(const PDFDocument* document, const QVariantMap& parameters) const;

    /// The output file of a tool of the result kind Files, which the caller writes
    /// (PDFDocumentToolResult::outputs), is a text: MCP may return it as the text of its result
    /// (return_text). The default is true; a binary output (the WAV file of Create Audio Book) is
    /// only written to a file (save_file).
    virtual bool isTextOutput() const { return true; }

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

    /// Removes the declaration of the given standards only (plan of step 5 of phase 2, R5-6: the
    /// encryption removes PDF/A, which forbids it, and keeps PDF/UA, which it does not break -
    /// bit 10 is always set, ISO 14289-1, 7.16); the other declarations are neither removed nor
    /// listed in the result. One implementation with removeConformanceDeclaration above.
    /// \param storage Objects of the result, the metadata are replaced in place
    /// \param document The document of \p storage
    /// \param standards The standards to remove ("PDF/A", "PDF/UA")
    static ConformanceRemoval removeConformanceDeclaration(PDFObjectStorage* storage, const PDFDocument* document, const QStringList& standards);

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
