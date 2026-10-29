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

#ifndef PDFAPPDOCUMENTSAVE_H
#define PDFAPPDOCUMENTSAVE_H

#include "pdfappdocumentcommands.h"

#include "pdfdocument.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppAutoRefresh;
class PDFAppDocumentOperation;
class PDFAppDocumentTab;
class PDFAppPrintController;
class PDFAppSendMail;
class PDFMessageBox;

/// Writing of the file of a document (package E): runs in a worker thread, it
/// uses no widgets and nothing of the document tab. The document is written into
/// memory first (the bytes become the original data of the document), then into
/// the file by QSaveFile (a temporary file renamed over the target, so a failure
/// never leaves a damaged file).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentFileWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentFileWriter)

public:
    PDFAppDocumentFileWriter() = delete;

    /// Largest file written (the limit of a QByteArray)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(2047) * 1024 * 1024;

    /// What is written
    class Request
    {
    public:
        PDFDocumentPointer document;    ///< Document of one version (immutable)
        QByteArray originalData;        ///< Bytes of the opened file (incremental update)
        QString filePath;               ///< Target file
        bool isIncremental = false;     ///< Append an incremental update to the original data (signed documents)
        bool isOriginalData = false;    ///< Write \p originalData unchanged - \p document is a display version (the paged layout of an XFA form, step 8)
        quint64 version = 0;            ///< Version of the document (given back in the result)
        QString markSourceFilePath;     ///< File, whose Mark of the Web the written file takes over (the file of the document or of its origin, O18)
    };

    /// Result of the writing
    class Result
    {
    public:
        bool isSaved = false;           ///< The file has been written
        QString filePath;               ///< Canonical path of the written file (empty on an error)
        QString requestedFilePath;      ///< Path of the request (also on an error - the file of Save As)
        QByteArray data;                ///< Written bytes
        quint64 version = 0;            ///< Version of the request
        bool isIncremental = false;     ///< The file is an incremental update
        QString errorMessage;           ///< Error of the system or of the writer (translated)
    };

    /// Writes the document (thread safe, called by a worker)
    static Result write(Request request);

    /// Number of the signed signature fields of the document (fields of the type
    /// Signature with a value); 0 for nullptr
    static int getSignatureCount(const PDFDocument* document);

    /// The file can be written: an existing file, which is writable, or a new file
    /// in an existing folder
    static bool isWritable(const QString& filePath);

    /// Attempts of the replacement of the file and the delay between them (ms)
    static constexpr int WRITE_ATTEMPTS = 5;
    static constexpr int WRITE_RETRY_DELAY = 200;

private:
    /// Writes the bytes by QSaveFile (retried, when the replacement fails)
    static bool writeFile(const QString& filePath, const QByteArray& data, QString* errorMessage);
};

/// Save and Save as of a document tab (package E, Figma 955:146677 save-signed,
/// 955:147112 save-error). The flow is asynchronous: the dialogs of the design
/// are opened without a nested event loop (QDialog::open) and the file is written
/// by PDFAppDocumentFileWriter in a worker thread from a snapshot of the document,
/// so the document stays usable. Each call of \p save or \p saveAs, which returns
/// true, ends by exactly one signal \p finished (also, when the user cancels a
/// dialog); a call during a running save joins it.
///
/// - A document without a file (Edit a Copy) is saved by Save as.
/// - A signed document is written as an incremental update (the signed revisions
///   stay in the file); the first Save of the tab asks by the dialog save-signed
///   (Save as Copy… / Cancel / Save Incrementally), later saves of the same tab do
///   not ask again until the document is opened again or reloaded from the disk.
/// - A file, which cannot be written (read-only), is not touched: the dialog
///   save-error offers Save As…; it also reports a failed writing (a full disk).
/// - The document stays usable while it is written (the operation Save does not
///   block changes); a change made meanwhile keeps the document modified.
/// - After a successful save the written version is the saved one with the file and
///   its bytes (PDFAppDocument::setSavedAs), the file is recorded in Recent and its
///   thumbnail is requested.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSaver : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocumentSaver(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppDocumentSaver() override;

    /// Save. \returns false, when nothing can be saved (no document shown); then
    /// no signal \p finished comes. \p finished may be emitted before the call
    /// returns (a canceled file dialog), connect to it first.
    bool save();

    /// Save as (the native file dialog). As \p save.
    bool saveAs();

    /// A save runs (a dialog is open or the file is being written)
    bool isRunning() const { return m_step != Step::Idle; }

    /// The file is being written by the worker
    bool isWriting() const { return m_step == Step::Writing; }

    /// Save can be used now (state of the command file.save): a shown document,
    /// which has changes or has no file yet, is not locked, its file is writable
    /// and no save runs
    bool canSave() const;

    /// Save as can be used now: a shown document and no save runs
    bool canSaveAs() const;

    /// The user has confirmed the incremental save of the signed document in this tab
    bool isSignedSaveConfirmed() const { return m_isSignedSaveConfirmed; }

    /// The shown dialog of the flow (save-signed or save-error), nullptr if none
    PDFMessageBox* getDialog() const;

    /// Folder and name proposed by Save as for the document
    QString getProposedFilePath() const;

Q_SIGNALS:
    /// The save has ended: \p isSaved = the version of the start is in the file;
    /// false = canceled by the user or failed (the error has been shown)
    void finished(bool isSaved);

    /// The saver has started or ended a step (the state of the commands may change)
    void stateChanged();

    /// A file has been written by the saver (the automatic refresh ignores it)
    void fileWritten(const QString& filePath);

private:
    enum class Step
    {
        Idle,           ///< Nothing runs
        SignedDialog,   ///< The dialog save-signed is open
        FileDialog,     ///< The native file dialog runs
        Writing,        ///< The worker writes the file
        ErrorDialog     ///< The dialog save-error is open
    };

    /// Starts the flow (Save or Save as)
    void start(bool isSaveAs);

    /// Asks for a file by the native dialog and writes it. \p isIncremental is
    /// used for a signed document.
    void chooseFileAndWrite(bool isIncremental);

    /// Writes the file (or opens save-error for a file, which cannot be written)
    void writeFile(const QString& filePath, bool isIncremental);

    void showSignedDialog(int signatureCount);
    void showErrorDialog(const QString& filePath, const QString& text, const QString& details);
    void closeDialog();

    void onSignedDialogFinished(int result);
    void onSignedDialogSaveAsCopy();
    void onErrorDialogSaveAs();
    void onErrorDialogRejected();
    void onWriterFinished();
    void onDocumentStateChanged();

    /// Ends the flow and emits \p finished
    void finish(bool isSaved);

    /// The file is open in another document tab of the process
    bool isOpenInAnotherDocument(const QString& filePath) const;

    QWidget* getDialogParent() const;
    PDFAppDocument* getDocument() const;

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocument> m_document;
    Step m_step = Step::Idle;
    bool m_isSignedSaveConfirmed = false;
    QPointer<PDFMessageBox> m_dialog;
    QPointer<QFutureWatcher<PDFAppDocumentFileWriter::Result>> m_watcher;
    QPointer<PDFAppDocumentOperation> m_operation;
};

/// Commands of package E (file operations): Save, Save as, Print, Send by e-mail,
/// Automatic refresh. The handler owns the saver, the automatic refresh, the
/// printing and the e-mail of its tab.
///
/// Package A (dialogs close-unsaved and quit-unsaved) saves a document by
/// \p save and waits for \p saveFinished.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentFileCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentFileCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentFileCommandHandler() override;

    /// The handler of package E of the tab (nullptr, if the tab has none)
    static PDFAppDocumentFileCommandHandler* find(const PDFAppDocumentTab* tab);

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

    /// Saves the document (as the command Save, PDFAppDocumentSaver::save): the
    /// result comes by \p saveFinished. \returns false, when nothing can be saved
    /// (then no signal comes).
    bool save();

    /// Save as (PDFAppDocumentSaver::saveAs)
    bool saveAs();

    PDFAppDocumentSaver* getSaver() const;
    PDFAppAutoRefresh* getAutoRefresh() const;
    PDFAppPrintController* getPrintController() const;
    PDFAppSendMail* getSendMail() const;

Q_SIGNALS:
    /// A save has ended (PDFAppDocumentSaver::finished)
    void saveFinished(bool isSaved);

private:
    void onPartStateChanged();
    void onSaverFinished(bool isSaved);

    QPointer<PDFAppDocumentSaver> m_saver;
    QPointer<PDFAppAutoRefresh> m_autoRefresh;
    QPointer<PDFAppPrintController> m_printController;
    QPointer<PDFAppSendMail> m_sendMail;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSAVE_H
