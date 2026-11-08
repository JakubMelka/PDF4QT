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

#ifndef PDFAPPTOOLINPUTLOADER_H
#define PDFAPPTOOLINPUTLOADER_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfimagepagefactory.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QObject>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <atomic>
#include <map>
#include <memory>

namespace pdf
{

class PDFAppToolInputLoaderLink;

/// Where an input of a tool with more inputs comes from (plan step 2, 4.4)
enum class PDFAppToolInputKind
{
    OpenTab,    ///< A snapshot of an open document tab, taken when it was added (R11)
    File,       ///< A PDF file read from the disk
    Image,      ///< An image file read from the disk (all frames of a TIFF are pages, Q6)
    Clipboard   ///< An image pasted from the clipboard (stored as PNG bytes, plan 5.3)
};

/// State of an input: the state of its row (378:2613, K39). Only Ready counts for a run.
enum class PDFAppToolInputState
{
    Loading,            ///< The loader reads the file (row Loading, K39)
    Ready,              ///< Usable: a PDF with pages or an image (isRepaired = Tag "Repaired")
    PasswordRequired,   ///< Encrypted, the password of the user is needed ("Protected by a password", 378:2613)
    Restricted,         ///< Opened without the owner password and its permissions do not allow the
                        ///< tool (Table 22 bit 11 Assemble denied, or another restriction, which the
                        ///< unencrypted result would lose, Q2 (c)) - "Restricted - enter the owner password"
    Damaged,            ///< The file cannot be read ("Damaged - the file cannot be read", 378:2613)
    Unsupported,        ///< Read, but not usable: a dynamic XFA form, an unknown or too large image, too many TIFF frames
    NoPages             ///< A PDF without a page (K39); it does not count to the minimum of a tool
};

/// Reads the inputs of PDFAppToolInputList (plan step 2, 4.4, 5.3, R15, Q2, Q6): a file is
/// read whole (no lazy reading, owner), it is a PDF by its signature (ISO 32000-2, 7.5.2,
/// "%PDF-" in its first 1024 bytes) or an image of QImageReader by its content. A PDF is read
/// by PDFAppDocumentReading (the permissive reader: a repaired file is Ready with isRepaired),
/// then checked: no pages (NoPages), a dynamic XFA form (Unsupported), the permissions of
/// Table 22 for the unencrypted result (Restricted, Q2 (c)). An image is checked by its header
/// and its first frame (the allocation limit, the pixel limit, at most MAXIMUM_IMAGE_FRAMES
/// frames of a TIFF) - a failure is Unsupported with the reason.
///
/// Threads and ownership (the pointer-ownership clause of the checklist):
/// - The reading runs in a task of the pool of PDFExecutionPolicy (PDFExecutionPolicy::startTask
///   on the global pool, as the reading of a document tab). The task gets a Request by value
///   (the path, the bytes, the password - plain values) and a shared pointer to the link;
///   it never gets a pointer to the list, the loader or a widget.
/// - The progress is a PDFProgressCounter shared by the task (it steps it) and the loader (the
///   GUI polls it, getProgress); no signal crosses the threads.
/// - The cancellation is a shared atomic flag of the request: cancel() sets it, the task checks
///   it between its stages (a reading of the PDF parser itself is not interruptible; its result
///   is dropped).
/// - The result comes back by a queued call to the loader in the GUI thread through the link,
///   which the destructor of the loader cuts under its mutex: a late result of a deleted loader
///   is dropped in the worker (PDFThreadAnnotations hand-off, TransportScope).
/// - The document of a result is a new immutable PDFDocumentPointer; the loader hands it to
///   the list in the GUI thread and keeps nothing of it.
///
/// The loader is a child of its list (owned by it); it emits readingFinished last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputLoader : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Largest file, which is read (as PDFAppDocumentReading::MAXIMUM_FILE_SIZE)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(2047) * 1024 * 1024;

    /// Most frames of a TIFF, which become pages (R15; the limit of the core, which writes them)
    static constexpr int MAXIMUM_IMAGE_FRAMES = PDFImagePageFactory::MAXIMUM_FRAME_COUNT;

    /// Allocation limit of one decoded frame in MB (SECURITY_TODO 2; the limit of the core):
    /// the default of the process of QImageReader::allocationLimit, which is never written
    /// here (a static value of Qt - a write from a worker would race with its readers)
    static constexpr int IMAGE_ALLOCATION_LIMIT_MB = PDFImagePageFactory::ALLOCATION_LIMIT_MB;

    /// Most pixels of one frame (the limit of the allocation in 32-bit pixels, R15)
    static constexpr qint64 MAXIMUM_IMAGE_PIXELS = PDFImagePageFactory::MAXIMUM_PIXEL_COUNT;

    /// What is read (a value copied to the worker)
    class Request
    {
    public:
        QString id;                     ///< Id of the input (returned with the result)
        QString filePath;               ///< File (read, when \p data is empty)
        QByteArray data;                ///< Bytes read before (a second reading with a password, the clipboard)
        QString name;                   ///< Name of the input (the texts of the reasons)
        bool isImageOnly = false;       ///< \p data is an image (the clipboard), never a PDF
        bool hasPassword = false;       ///< \p password is tried
        bool isOwnerPassword = false;   ///< \p password is the owner password (Restricted): the reading authorizes the owner only
        PDFDocumentPointer snapshotDocument;    ///< A snapshot of an open tab (Q15): \p password is only verified as its owner password - by the
                                                ///< security handler of the snapshot, else by \p data (the original file of the tab); nothing is read again
        QString password;               ///< Never logged, never stored
        std::shared_ptr<PDFProgressCounter> progress;       ///< Stepped by the worker, may be null
        std::shared_ptr<std::atomic_bool> isCanceled;       ///< Set by the GUI thread, may be null
    };

    /// Result of a reading (a value moved back to the GUI thread)
    class Result
    {
    public:
        PDFAppToolInputKind kind;       ///< File or Image (by the content), Clipboard for an image of the clipboard
        PDFAppToolInputState state;
        QString reason;                 ///< Translated reason (a state other than Ready)
        bool isEncrypted = false;
        bool isPasswordWrong = false;
        bool isRepaired = false;
        bool isOwnerPasswordVerified = false;   ///< The owner password of a snapshot was verified (Q15): the document is permitted (PDFDocumentMerger::getPermission)
        PDFInteger pageCount = 0;
        QSizeF firstPageSize;
        qint64 fileSize = 0;
        PDFDocumentPointer document;
        QByteArray data;                ///< The bytes of the file (PDF: originalData, image: imageData)
        QByteArray imageFormat;
        QSize imageSize;
        double imageDpiX = 0.0;
        double imageDpiY = 0.0;

        Result();
    };

    explicit PDFAppToolInputLoader(QObject* parent);
    virtual ~PDFAppToolInputLoader() override;

    /// Reads the request (called by the worker; also directly by the tests). Thread safe:
    /// it works only with its argument.
    static Result read(Request request);

    /// Fills the state, the reason, the page count and the size of the first page of a PDF,
    /// which has been read (also a snapshot of an open tab): NoPages, Restricted or Ready;
    /// \p result->isOwnerPasswordVerified permits a restricted document (Q15)
    static void fillPdfResult(const PDFDocument* document, Result* result, const QString& name = QString());

    /// The document may be an input of a tool, whose result is not encrypted (Q2 (c), ISO
    /// 32000-2 Table 22, the one rule of the core: PDFDocumentMerger::getPermission): it is not
    /// encrypted, it was opened by the owner password, or it grants every permission (Assemble
    /// and all, which the result would lose). \p reason gets the translated reason of a
    /// refusal for the input \p name (may be null).
    static bool isPermitted(const PDFDocument* document, QString* reason, const QString& name = QString(), bool isOwnerPasswordVerified = false);

    /// Starts the reading of the request in the pool; a reading of the same id, which runs,
    /// is canceled (its result is dropped)
    void start(Request request);

    /// Cancels the reading of the input (no result comes)
    void cancel(const QString& id);

    /// Cancels every reading (the destructor does it too)
    void cancelAll();

    /// The input is being read
    bool isRunning(const QString& id) const;

    /// Number of the readings, which run
    int getRunningCount() const { return int(m_running.size()); }

    /// Percentage of the reading of the input (0-100), -1 when it is not known
    int getProgress(const QString& id) const;

Q_SIGNALS:
    /// The reading of the input has ended (GUI thread); a slot may delete the loader
    void readingFinished(const QString& id, const pdf::PDFAppToolInputLoader::Result& result);

private:
    friend class PDFAppToolInputLoaderLink;

    /// The task of the pool has finished (queued call of the link, GUI thread)
    void onTaskFinished(const std::shared_ptr<pdf::PDFAppToolInputLoaderLink>& link, quint64 serial, const pdf::PDFAppToolInputLoader::Result& result);

    struct Running
    {
        quint64 serial = 0;
        std::shared_ptr<PDFProgressCounter> progress;
        std::shared_ptr<std::atomic_bool> isCanceled;
    };

    std::shared_ptr<PDFAppToolInputLoaderLink> m_link;
    std::map<QString, Running> m_running;
    quint64 m_nextSerial = 1;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppToolInputLoader::Result)

#endif // PDFAPPTOOLINPUTLOADER_H
