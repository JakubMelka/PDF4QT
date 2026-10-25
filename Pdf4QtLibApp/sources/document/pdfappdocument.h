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

#ifndef PDFAPPDOCUMENT_H
#define PDFAPPDOCUMENT_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfsignaturehandler.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppUndoStack;
class PDFDocumentTextIndex;
class PDFOptionalContentActivity;

/// State of a document of a document tab
enum class PDFAppDocumentState
{
    Loading,            ///< The file is being read (the tab shows the progress)
    PasswordRequired,   ///< The file is encrypted, the tab asks for the password (228:8709)
    Failed,             ///< The file cannot be opened (getErrorMessage)
    Ready               ///< The document is shown
};

/// Who has made a change of the document (one undo step each)
enum class PDFAppChangeSource
{
    User,   ///< The user in the window
    Mcp     ///< An assistant through MCP (step 7)
};

/// Kind of a running operation of a document (the tab shows its progress, the
/// close-running dialog 955:145813 names it)
enum class PDFAppDocumentOperationKind
{
    Load,       ///< Reading and decryption of the file
    Save,       ///< Writing of the file
    Print,      ///< Printing
    Index,      ///< Text index for the search
    Signatures, ///< Verification of the signatures
    Fonts,      ///< Scan of the fonts (properties)
    Mcp,        ///< A change requested by an assistant
    Edit        ///< A change made by a worker of the window (step 4: Mark as annotations of many results)
};

/// Snapshot of a document for a worker thread: the immutable document of one
/// version, the original bytes of the file and its path. A worker may build a
/// modified copy by PDFDocumentModifier and commit it with the version of the
/// snapshot (PDFAppDocument::commit refuses it, when the document has changed).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSnapshot
{
public:
    PDFDocumentPointer document;
    quint64 version = 0;
    QByteArray originalData;
    QString filePath;

    bool isValid() const { return !document.isNull(); }
};

/// Data derived from one version of a document (the attachments, the notes):
/// computed once per version by the first user (a panel, the availability of a
/// tab of the sidebar) and shared by the others (PDFAppDocument::getDerivedData)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentDerivedData
{
public:
    virtual ~PDFAppDocumentDerivedData();
};

/// Long operation of a document (save, print, index, MCP change). Created by
/// PDFAppDocument::beginOperation, finished by finish(); the document deletes it
/// later. The progress and the cancellation request are thread safe to read.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentOperation : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocumentOperation(PDFAppDocumentOperationKind kind, QString text, bool blocksChanges, QObject* parent);
    virtual ~PDFAppDocumentOperation() override;

    PDFAppDocumentOperationKind getKind() const { return m_kind; }
    const QString& getText() const { return m_text; }

    /// The operation blocks changes of the document (commit is refused meanwhile)
    bool isBlockingChanges() const { return m_blocksChanges; }

    /// The user is asked before a tab with this operation is closed (close-running
    /// dialog 955:145813: a save, a print, a change of an assistant); the other
    /// operations (index, signatures, fonts) are stopped without a question
    bool isAskedBeforeClose() const;

    /// Progress 0 - 100, -1 = unknown
    int getProgress() const { return m_progress; }
    void setProgress(int progress);

    /// The user has asked to stop the operation (close-running dialog, Cancel)
    bool isCancelRequested() const { return m_isCancelRequested; }
    void requestCancel();

    bool isFinished() const { return m_isFinished; }

    /// The operation has ended (the document removes it and deletes it later)
    void finish();

Q_SIGNALS:
    void progressChanged(int progress);
    void cancelRequested();
    void finished();

private:
    PDFAppDocumentOperationKind m_kind;
    QString m_text;
    bool m_blocksChanges = false;
    int m_progress = -1;
    bool m_isCancelRequested = false;
    bool m_isFinished = false;
};

/// Model of one document of a document tab (step 3 plan 1.1): no widgets. It holds
/// the current immutable document, its version (+1 on every change, also undo and
/// redo), the saved version, the original bytes of the file (incremental save,
/// signatures), the optional content activity, the lock, the undo stack, the
/// running operations and the text index. The document is owned by its tab and
/// registered in PDFAppDocumentRegistry of the context (id = MCP documentId).
///
/// Lifecycle: the tab starts PDFAppDocumentLoader, which calls setLoading (the
/// canonical path is known at once, so an open file is found by the registry
/// while it is read), then setLoaded, setPasswordRequired or setFailed. A copy
/// (Edit a Copy) is created by setCopyOf: it has no file and it is modified.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocument : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocument(PDFAppContext* context, QObject* parent);
    virtual ~PDFAppDocument() override;

    /// New random id: 128 bits of QRandomGenerator::system() in base32 (26
    /// characters, accepted by PDFAppRouteValues::documentId)
    static QString createId();

    PDFAppContext* getContext() const;

    /// Id of the document (route /document/{id}, MCP documentId)
    const QString& getId() const { return m_id; }

    PDFAppDocumentState getState() const { return m_state; }
    bool isReady() const { return m_state == PDFAppDocumentState::Ready; }

    /// Error of the state Failed (translated text)
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Canonical path of the file (empty for Edit a Copy, which has no file yet)
    const QString& getFilePath() const { return m_filePath; }

    /// Title of the tab: the name of the file, "Copy of <name>" of a copy without
    /// a file, "Untitled" without both
    QString getTitle() const;

    /// The document is a copy of another document (Edit a Copy), which has not
    /// been saved to a file yet
    bool isCopy() const { return m_isCopy; }

    /// Current document (nullptr until the state is Ready)
    PDFDocumentPointer getDocument() const { return m_document; }

    /// Number of pages (0 until the state is Ready)
    PDFInteger getPageCount() const;

    /// Version of the document (+1 on every change, also on undo / redo)
    quint64 getVersion() const { return m_version; }

    /// Version of the last change, which may have changed the outline: every change
    /// except a change of annotations or of form fields (the objects of the outline
    /// are kept by them); data read from the outline are valid while it is the same
    quint64 getOutlineVersion() const { return m_outlineVersion; }

    /// Version, which is saved in the file
    quint64 getSavedVersion() const { return m_savedVersion; }

    /// The document has unsaved changes
    bool isModified() const { return m_version != m_savedVersion; }

    /// Number of changes since the saved version (steps of the undo stack): 0 for
    /// an unmodified document, -1 when it is not known (the saved version is not
    /// reachable by undo / redo, a copy, which has never been saved)
    int getUnsavedChangeCount() const;

    /// The current version is saved (after a successful save of package E)
    void setSaved();

    /// The version \p version has been saved to the file \p filePath (Save, Save as,
    /// a copy saved the first time - package E writes a snapshot in a worker, so the
    /// version may be older than the current one): the path, the title and the
    /// original bytes follow the file, the version becomes the saved version.
    /// Ignored for a version newer than the current one or an empty path.
    void setSavedAs(quint64 version, const QString& filePath, QByteArray data);

    /// Bytes of the opened (or last saved) file
    const QByteArray& getOriginalData() const { return m_originalData; }

    /// Activity of the optional content of the current document (nullptr until Ready)
    PDFOptionalContentActivity* getOptionalContentActivity() const;

    /// The document is locked (editing commands are off, recent_file.locked)
    bool isLocked() const { return m_isLocked; }
    void setLocked(bool locked);

    /// The user interface has edits, which are not committed yet (step 4, package
    /// D: an open editor of a text field or of a list of choices). Save and Undo
    /// are enabled by them; the owner of the edits sets the state.
    bool hasPendingEdits() const { return m_hasPendingEdits; }
    void setPendingEdits(bool hasPendingEdits) { m_hasPendingEdits = hasPendingEdits; }

    /// Asks the user interface to commit its pending edits (pendingEditsCommitRequested):
    /// called before undo, redo, save, lock and closing, so an open editor of a form
    /// field becomes an undo step first. A slot may delete the document.
    void commitPendingEdits();

    /// The file is protected by a password (the tab shows the password icon)
    bool isPasswordProtected() const { return m_isPasswordProtected; }

    /// State PasswordRequired after a wrong password (228:8965)
    bool isPasswordWrong() const { return m_isPasswordWrong; }

    /// Progress of the reading of the file 0 - 100, -1 = unknown (state Loading)
    int getLoadingProgress() const { return m_loadingProgress; }
    void setLoadingProgress(int progress);

    /// Undo stack of the document
    PDFAppUndoStack* getUndoStack() const { return m_undoStack.get(); }

    /// Snapshot of the current version for a worker thread
    PDFAppDocumentSnapshot getSnapshot() const;

    /// Commits a change: \p modified carries the new document (PDFDocumentPointer)
    /// and its flags. Refused (false), when the version is not \p expectedVersion,
    /// the document is locked or not ready, a blocking operation runs or the new
    /// document is null. Every accepted change is one undo step and increments the
    /// version. The limits of the undo stack are read at each commit (the setting
    /// editing.undoLimit, the memory limit of the stack).
    bool commit(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion, PDFAppChangeSource source);

    /// Commits a change of a merge session (step 4, owner decision O20 - a live
    /// change of the color or of the width of the selected annotations): when
    /// \p mergeKey is not empty, equals the key of the top undo step and nothing
    /// has happened since that step (no other commit, undo, redo, save), the change
    /// replaces the current document (the version is incremented) and the top step
    /// keeps the document before the first change of the session. Otherwise it is
    /// a new undo step with the key. Refused as \p commit.
    bool commit(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion, PDFAppChangeSource source, const QString& mergeKey);

    /// Ends the merge session of the top undo step (a selection or a tool has
    /// changed, a dialog has been closed): the next commit is a new undo step
    void closeUndoMerge();

    /// Sets the document of an undo / redo step (PDFAppUndoStack): the version is
    /// incremented, no undo step is recorded; back at the saved position of the
    /// undo stack the document is not modified
    void restore(PDFDocumentPointer document, PDFModifiedDocument::ModificationFlags flags);

    /// Starts an operation (the document owns it until it finishes)
    PDFAppDocumentOperation* beginOperation(PDFAppDocumentOperationKind kind, const QString& text, bool blocksChanges);

    /// Running operations
    std::vector<PDFAppDocumentOperation*> getOperations() const;

    /// A running operation blocks changes
    bool isBlockedByOperation() const;

    /// Text index of the document (nullptr = not built yet or not valid for the
    /// current version); kept over changes without PageContents / Reset
    std::shared_ptr<const PDFDocumentTextIndex> getTextIndex() const { return m_textIndex; }

    /// Sets the index built from the version \p version (ignored, when the
    /// document has changed its page contents since then)
    void setTextIndex(std::shared_ptr<const PDFDocumentTextIndex> index, quint64 version);

    /// Data derived from the current version under \p key (nullptr = not computed
    /// yet); every change of the document drops all of them. The GUI thread only.
    std::shared_ptr<const PDFAppDocumentDerivedData> getDerivedData(const QString& key) const;

    /// Stores the data derived from the current version (a cache: the document
    /// itself does not change)
    void setDerivedData(const QString& key, std::shared_ptr<const PDFAppDocumentDerivedData> data) const;

    /// Results of the verification of the signatures of the current version
    const std::vector<PDFSignatureVerificationResult>& getSignatureResults() const { return m_signatureResults; }

    /// Sets the results of the version \p version (ignored for an older version)
    void setSignatureResults(std::vector<PDFSignatureVerificationResult> results, quint64 version);

    /// The loader starts reading the file: state Loading, the canonical path is set
    /// (a document, which is shown, keeps its state - a reload)
    void setLoading(const QString& filePath);

    /// The loader starts reading a document from the memory (a PDF attachment,
    /// owner decision O18): state Loading, no file, the title \p title. The document
    /// behaves as a copy (Edit a Copy): it is modified, it is saved only by Save As
    /// and it is never recorded in Recent. \p originFilePath is the file, from which
    /// the document comes (getOriginFilePath). Refused (false), when the document
    /// is not new.
    bool setLoadingFromMemory(const QString& title, const QString& originFilePath);

    /// File, from which the document comes: its own file, else the file of the
    /// document of an attachment or of a copy (the Mark of the Web of a saved
    /// attachment is taken from it); empty when not known
    QString getOriginFilePath() const;

    /// Result of a loader: the document is shown (state Ready, a new saved version,
    /// the undo stack is cleared). A document read from the memory
    /// (setLoadingFromMemory, an empty \p filePath) stays a modified copy.
    void setLoaded(PDFDocumentPointer document, QByteArray originalData, const QString& filePath, bool isPasswordProtected);

    /// Result of a reload of the file (automatic refresh of package E): the document
    /// is replaced (flags Reset and PreserveView, the state stays Ready), the undo
    /// stack is cleared and the new version is saved. Ignored, when the document is
    /// not shown.
    void reload(PDFDocumentPointer document, QByteArray data, bool isPasswordProtected);

    /// Result of a loader: the file needs a password (\p isWrong after a wrong
    /// password, 228:8965)
    void setPasswordRequired(const QString& filePath, bool isWrong = false);

    /// Result of a loader: the file cannot be opened
    void setFailed(const QString& filePath, const QString& errorMessage);

    /// Makes the document a copy of \p source (Edit a Copy): the current document
    /// of the source, its original bytes, no file, unlocked, modified. Refused
    /// (false), when the source is not ready or this document is not new.
    bool setCopyOf(const PDFAppDocument* source);

Q_SIGNALS:
    /// The document is being destroyed (emitted first by its destructor, while the
    /// PDF document is still alive): a view stops using the PDF document (its page
    /// compilers read it in worker threads). A slot must not delete the document.
    void aboutToBeDestroyed();

    /// The state has changed (getState)
    void stateChanged();

    /// The document has been changed (open, commit, undo / redo, reload); the
    /// flags of \p modified say what has changed
    void documentChanged(const pdf::PDFModifiedDocument& modified);

    /// The saved state has changed (isModified, getUnsavedChangeCount)
    void modifiedChanged();

    /// The file of the document has changed (Save as): the path and the title
    void fileChanged();

    /// The lock has changed (isLocked)
    void lockChanged();

    /// An operation has started or finished
    void operationsChanged();

    /// The progress of a running operation has changed (the progress of the tab;
    /// the commands do not change with it)
    void operationProgressChanged();

    /// The signature results have changed
    void signatureResultsChanged();

    /// The progress of the reading has changed (getLoadingProgress)
    void loadingProgressChanged();

    /// The pending edits of the user interface are to be committed now (commitPendingEdits)
    void pendingEditsCommitRequested();

private:
    void setDocument(PDFDocumentPointer document, PDFModifiedDocument::ModificationFlags flags, bool isSaved);
    void setState(PDFAppDocumentState state);
    void onOperationFinished();

    QPointer<PDFAppContext> m_context;
    QString m_id;
    PDFAppDocumentState m_state = PDFAppDocumentState::Loading;
    QString m_errorMessage;
    QString m_filePath;
    QString m_copyTitle;
    QString m_originFilePath;   ///< File of the source of a copy or of an attachment
    bool m_isCopy = false;
    PDFDocumentPointer m_document;
    quint64 m_version = 0;
    quint64 m_savedVersion = 0;
    QByteArray m_originalData;
    QPointer<PDFOptionalContentActivity> m_optionalContentActivity;
    bool m_isLocked = false;
    bool m_hasPendingEdits = false;
    bool m_isPasswordProtected = false;
    bool m_isPasswordWrong = false;
    int m_loadingProgress = -1;
    std::unique_ptr<PDFAppUndoStack> m_undoStack;
    std::vector<QPointer<PDFAppDocumentOperation>> m_operations;
    std::shared_ptr<const PDFDocumentTextIndex> m_textIndex;
    quint64 m_pageContentsVersion = 0;   ///< Version of the last change of the page contents (Reset, PageContents)
    quint64 m_outlineVersion = 0;        ///< Version of the last change, which may have changed the outline
    std::vector<PDFSignatureVerificationResult> m_signatureResults;
    mutable std::map<QString, std::shared_ptr<const PDFAppDocumentDerivedData>> m_derivedData;   ///< Of the current version
};

}   // namespace pdf

#endif // PDFAPPDOCUMENT_H
