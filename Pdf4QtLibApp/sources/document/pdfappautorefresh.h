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

#ifndef PDFAPPAUTOREFRESH_H
#define PDFAPPAUTOREFRESH_H

#include "pdfappglobal.h"

#include "pdfdocument.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>

class QFileSystemWatcher;
class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;
class PDFMessageBox;

/// Reading of a changed file for the automatic refresh (worker thread, no widgets):
/// the file is read whole and parsed as at the opening (step3_large_documents.md),
/// unless its bytes are the bytes of the document (the own save, a touch).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAutoRefreshReader
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppAutoRefreshReader)

public:
    PDFAppAutoRefreshReader() = delete;

    /// Largest file read (the limit of a QByteArray)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(2047) * 1024 * 1024;

    enum class Outcome
    {
        Unchanged,          ///< The file has the bytes of the document
        Changed,            ///< A new valid document has been read
        PasswordRequired,   ///< The new file needs a password (the tab asks for it)
        Failed,             ///< The file cannot be read or parsed (it may be being written)
        Missing             ///< The file does not exist (it is being replaced)
    };

    class Result
    {
    public:
        Outcome outcome = Outcome::Failed;
        QString filePath;               ///< Path of the request
        QByteArray knownData;           ///< Bytes of the document, against which the file was compared
        PDFDocumentPointer document;    ///< Document of Changed
        QByteArray data;                ///< Bytes of the file (Changed, PasswordRequired)
        bool isPasswordProtected = false;
        bool isPagedXFA = false;        ///< \p document is the paged layout of the XFA form of the file (step 8)
        QString errorMessage;           ///< Error of Failed
    };

    /// Reads the file (thread safe); \p knownData are the bytes of the document
    static Result read(QString filePath, QByteArray knownData);
};

/// Automatic refresh of a document tab (package E, D3): a QFileSystemWatcher of
/// the file of the document; a change is checked after a short delay (several
/// writes of another program are joined), the file is read and parsed in a worker
/// thread like at the opening and the document is replaced keeping the view
/// (PDFAppDocument::reload: the undo history is cleared). A watched path,
/// which an atomic replace has removed from the watcher, is added again (also
/// later, while the file does not exist). Own saves are recognized by the bytes
/// of the file (ignoreOwnWrite and the original data of the document).
///
/// A document with unsaved changes is never replaced without a question: the
/// dialog refresh-unsaved (955:146242) offers Reload and Lose Changes / Keep My
/// Version; the kept file is not offered again until it changes once more. A new
/// file, which needs a password, is read by the loader of the tab (the password
/// state). The refresh is on by default (setting document.autoRefresh) and it has
/// a switch per document (the command file.autoRefresh).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAutoRefresh : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppAutoRefresh(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppAutoRefresh() override;

    /// Delay between the last change of the file and its check
    static constexpr int CHECK_DELAY = 500;

    /// Interval of the attempts to watch a file, which is missing (atomic replace)
    static constexpr int REWATCH_INTERVAL = 1000;

    /// Number of the attempts to watch a missing file
    static constexpr int REWATCH_ATTEMPTS = 30;

    /// The refresh is on for the document (the switch of the document)
    bool isEnabled() const { return m_isEnabled; }
    void setEnabled(bool enabled);

    /// Watched file (empty, when nothing is watched)
    QString getWatchedPath() const;

    /// A check of the file runs (the worker reads it)
    bool isChecking() const { return !m_watcher.isNull(); }

    /// The dialog refresh-unsaved, nullptr if it is not shown
    PDFMessageBox* getDialog() const;

    /// Checks the file at once (the delay of a change is skipped)
    void checkNow();

    /// The saver has written the file: the change notifications of this write are
    /// ignored (the file has the bytes of the document)
    void ignoreOwnWrite(const QString& filePath);

    /// Number of the reloads done (statistics for the tests)
    int getReloadCount() const { return m_reloadCount; }

Q_SIGNALS:
    /// The switch or the watched file has changed
    void stateChanged();

    /// A check of the file has ended (with or without a reload)
    void checked(pdf::PDFAppAutoRefreshReader::Outcome outcome);

    /// The document has been reloaded from the changed file
    void reloaded();

private:
    void updateWatchedPath();
    void scheduleCheck(int delay);
    void startCheck();
    void onFileChanged(const QString& path);
    void onRewatchTimeout();
    void onReaderFinished();
    void onDialogFinished(int result);
    void onDocumentStateChanged();
    void applyResult(const PDFAppAutoRefreshReader::Result& result);
    void showUnsavedDialog(const PDFAppAutoRefreshReader::Result& result);
    void closeDialog();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocument> m_document;
    QPointer<QFileSystemWatcher> m_fileWatcher;
    QPointer<QTimer> m_checkTimer;
    QPointer<QTimer> m_rewatchTimer;
    QPointer<QFutureWatcher<PDFAppAutoRefreshReader::Result>> m_watcher;
    QPointer<PDFMessageBox> m_dialog;
    QString m_watchedPath;
    QByteArray m_keptData;                          ///< Bytes of the file kept by Keep My Version
    PDFAppAutoRefreshReader::Result m_pendingResult;///< Result waiting for the dialog
    bool m_isEnabled = true;
    bool m_isCheckPending = false;                  ///< A change came while the worker was reading
    int m_rewatchAttempts = 0;
    int m_reloadCount = 0;
};

}   // namespace pdf

#endif // PDFAPPAUTOREFRESH_H
