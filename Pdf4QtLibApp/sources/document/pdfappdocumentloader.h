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

#ifndef PDFAPPDOCUMENTLOADER_H
#define PDFAPPDOCUMENTLOADER_H

#include "pdfappglobal.h"

#include "pdfdocument.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>

namespace pdf
{

class PDFAppDocument;

/// Reading of a PDF file in a worker thread (package A, step 3 plan 1.2): no
/// widgets, nothing of the document tab, thread safe. The file is read whole and
/// parsed (no lazy loading, as the Editor); a password is never asked across the
/// threads - the request carries it, and a missing or wrong password is the
/// result of the reading (the tab asks the user and reads again).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentReading
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentReading)

public:
    PDFAppDocumentReading() = delete;

    /// Largest file, which is read (the limit of a QByteArray)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(2047) * 1024 * 1024;

    /// Longest password, which is tried (a longer one is wrong)
    static constexpr int MAXIMUM_PASSWORD_LENGTH = 1024;

    /// What is read
    class Request
    {
    public:
        QString filePath;           ///< File (read, when \p data is empty)
        QByteArray data;            ///< Bytes of the file read before (a second reading with a password)
        bool isFromMemory = false;  ///< \p data is the document (an attachment), no file is read
        bool hasPassword = false;   ///< \p password is tried (an empty password, too)
        QString password;           ///< Password of the user (never logged, never stored)

        /// Progress of the reading 0 - 100 (written by the worker, -1 = not started)
        std::shared_ptr<std::atomic<int>> progress;
    };

    /// Result of the reading
    enum class Status
    {
        Loaded,             ///< The document has been read
        PasswordRequired,   ///< The file is encrypted, a password is needed
        PasswordWrong,      ///< The password of the request is not the password of the file
        Failed              ///< The file cannot be read (errorMessage)
    };

    class Result
    {
    public:
        Status status = Status::Failed;
        PDFDocumentPointer document;    ///< The document (Loaded)
        QByteArray data;                ///< Bytes of the file (kept for a second reading)
        bool isEncrypted = false;       ///< The file is encrypted (the tab shows the password icon)
        bool isPagedXFA = false;        ///< \p document is the paged layout of the XFA form of the file (PDFXFAPaging, step 8)
        QString errorMessage;           ///< Translated error (Failed)
    };

    /// Reads the document (called by a worker thread)
    static Result read(Request request);
};

/// Reads and decrypts the file of a document (step 3 plan 1.2). The reading runs
/// in a pool thread (PDFAppDocumentReading); its progress is shown by the loading
/// state of the tab, it is not cancellable (owner decision D11), but a tab closed
/// meanwhile drops the result (the worker is never waited for). The first reading
/// without a password records, that the file needs one: the document gets the
/// state PasswordRequired (228:8709) and setPassword reads the bytes again with
/// the password; a wrong password gives the state again with the error (228:8965).
/// No blocking call crosses the threads.
///
/// The loader keeps the last accepted password in memory for the lifetime of the
/// tab (reload of the automatic refresh): it is never logged or stored.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentLoader : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Interval of the update of the progress
    static constexpr int PROGRESS_INTERVAL = 100;

    /// \p document is the document, which gets the result (also the parent)
    explicit PDFAppDocumentLoader(PDFAppDocument* document);
    virtual ~PDFAppDocumentLoader() override;

    /// Starts the reading of the file (the document gets the state Loading)
    void start(const QString& filePath);

    /// Starts the reading of a document from the memory (a PDF attachment, O18):
    /// no temporary file, the document has no file and the title \p title
    /// (PDFAppDocument::setLoadingFromMemory); an encrypted document asks for its
    /// password as a file does. \returns false, when the document is not new or a
    /// reading runs.
    bool startFromMemory(QByteArray data, const QString& title, const QString& originFilePath);

    /// Reads the file again with the password (state PasswordRequired). \returns
    /// false, when the document does not need a password or a reading runs.
    bool setPassword(const QString& password);

    /// Reads the file of a shown document again (automatic refresh): the result
    /// replaces the document (PDFAppDocument::reload). The last accepted password
    /// is tried first; when it does not open the file any more, the document asks
    /// for the password again. \returns false, when the document has no file or a
    /// reading runs.
    bool reload();

    /// Drops a running reading (the document stays in its state, no signal comes)
    void cancel();

    /// The reading runs
    bool isRunning() const { return m_watcher != nullptr; }

Q_SIGNALS:
    /// The reading has ended (the state of the document says the result)
    void finished();

private:
    void startReading(PDFAppDocumentReading::Request request, bool isReload);
    void onReadingFinished();
    void onProgressTimeout();

    QPointer<PDFAppDocument> m_document;
    QString m_filePath;
    QByteArray m_data;
    QString m_password;
    bool m_hasPassword = false;
    bool m_isReload = false;
    std::shared_ptr<std::atomic<int>> m_progress;
    QPointer<QFutureWatcher<PDFAppDocumentReading::Result>> m_watcher;
    QPointer<QTimer> m_progressTimer;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTLOADER_H
