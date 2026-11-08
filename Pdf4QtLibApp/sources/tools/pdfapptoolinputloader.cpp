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

#include "pdfapptoolinputloader.h"
#include "pdfappdocumentloader.h"
#include "pdfappfiledrop.h"

#include "pdfcatalog.h"
#include "pdfdocumentmerger.h"
#include "pdfexecutionpolicy.h"
#include "pdfpage.h"
#include "pdfsecurityhandler.h"
#include "pdfthreadannotations.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QMutex>
#include <QThreadPool>

#include <cmath>
#include <limits>

namespace pdf
{

/// The way of a result of a task back to its loader: cut by the destructor of the loader
/// (a late result of a deleted loader is dropped in the worker); a task never holds the
/// loader itself
class PDFAppToolInputLoaderLink
{
public:
    explicit PDFAppToolInputLoaderLink(PDFAppToolInputLoader* loader) :
        m_loader(loader)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_loader = nullptr;
    }

    /// A task has finished (a thread of the pool): the loader takes the result in its own
    /// thread by a queued call
    void notify(const std::shared_ptr<PDFAppToolInputLoaderLink>& self, quint64 serial, PDFAppToolInputLoader::Result result)
    {
        QMutexLocker locker(&m_mutex);
        if (m_loader)
        {
            PDFThreadAnnotations::happensBefore(this);
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_loader, &PDFAppToolInputLoader::onTaskFinished, Qt::QueuedConnection, self, serial, std::move(result));
        }
    }

private:
    QMutex m_mutex;
    PDFAppToolInputLoader* m_loader;
};

/// The task of the pool: it holds the request (values) and the link, never the loader
class PDFAppToolInputLoaderTask
{
public:
    PDFAppToolInputLoaderTask(std::shared_ptr<PDFAppToolInputLoaderLink> link, quint64 serial, PDFAppToolInputLoader::Request request) :
        m_link(std::move(link)),
        m_serial(serial),
        m_request(std::move(request))
    {

    }

    void operator()()
    {
        PDFAppToolInputLoader::Result result = PDFAppToolInputLoader::read(std::move(m_request));
        m_link->notify(m_link, m_serial, std::move(result));
    }

private:
    std::shared_ptr<PDFAppToolInputLoaderLink> m_link;
    quint64 m_serial = 0;
    PDFAppToolInputLoader::Request m_request;
};

/// Helpers of the reading (a worker thread: only their arguments)
class PDFAppToolInputLoaderHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolInputLoader)

public:
    PDFAppToolInputLoaderHelper() = delete;

    static bool isCanceled(const PDFAppToolInputLoader::Request& request)
    {
        return request.isCanceled && request.isCanceled->load(std::memory_order_relaxed);
    }

    static PDFAppToolInputLoader::Result createFailure(PDFAppToolInputState state, const QString& reason)
    {
        PDFAppToolInputLoader::Result result;
        result.state = state;
        result.reason = reason;
        return result;
    }

    /// Reads the file whole (no lazy reading, owner)
    static bool readFile(PDFAppToolInputLoader::Request* request, PDFAppToolInputLoader::Result* failure)
    {
        const QFileInfo fileInfo(request->filePath);
        if (request->filePath.isEmpty() || !fileInfo.isFile())
        {
            *failure = createFailure(PDFAppToolInputState::Damaged, tr("The file does not exist."));
            return false;
        }
        if (fileInfo.size() > PDFAppToolInputLoader::MAXIMUM_FILE_SIZE)
        {
            *failure = createFailure(PDFAppToolInputState::Unsupported, tr("The file is too large."));
            return false;
        }

        QFile file(request->filePath);
        if (!file.open(QFile::ReadOnly))
        {
            *failure = createFailure(PDFAppToolInputState::Damaged, tr("The file cannot be read: %1").arg(file.errorString()));
            return false;
        }
        request->data = file.readAll();
        file.close();

        if (request->data.isEmpty())
        {
            *failure = createFailure(PDFAppToolInputState::Damaged, tr("The file is empty."));
            return false;
        }
        return true;
    }

    /// Resolution of a frame in dots per inch (0: the file has none)
    static double getDpi(int dotsPerMeter)
    {
        // QImage reports 2835 dots per meter (72 dpi) for a file without a resolution
        return dotsPerMeter > 0 ? double(dotsPerMeter) * 0.0254 : 0.0;
    }

    static PDFAppToolInputLoader::Result readImage(const PDFAppToolInputLoader::Request& request, PDFAppToolInputKind kind)
    {
        PDFAppToolInputLoader::Result result;
        result.kind = kind;
        result.fileSize = request.data.size();

        QByteArray data = request.data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);

        // The allocation limit of Qt is one value of the process (QImageReader::setAllocationLimit
        // is static): it is never written by a worker - the write would race with every reader
        // of it in the other workers (the core keeps the default, IMAGE_ALLOCATION_LIMIT_MB);
        // the pixel limit is checked below before the frame is decoded
        if (!reader.canRead())
        {
            return createFailure(PDFAppToolInputState::Unsupported, tr("The file is neither a PDF document nor an image, which can be read."));
        }

        result.imageFormat = reader.format();

        // A size larger than the limit is refused before the pixels are decoded (R15)
        const QSize headerSize = reader.size();
        if (headerSize.isValid() && qint64(headerSize.width()) * qint64(headerSize.height()) > PDFAppToolInputLoader::MAXIMUM_IMAGE_PIXELS)
        {
            return createFailure(PDFAppToolInputState::Unsupported, tr("The image is too large (%1 × %2 px).").arg(headerSize.width()).arg(headerSize.height()));
        }

        // An animation gives its first frame (plan 5.3), the pages of a TIFF all its frames (Q6),
        // at most MAXIMUM_IMAGE_FRAMES (R15): counted by the one rule of the core, which writes
        // them (PDFImagePageFactory::readInfo - a reader, which has decided its format from the
        // content, counts only the first directory of a TIFF)
        const PDFImagePageFactory::ImageInfo info = PDFImagePageFactory::readInfo(request.data, result.imageFormat);
        if (info.frameCount <= 0)
        {
            return createFailure(PDFAppToolInputState::Unsupported, info.error.isEmpty() ? tr("The image cannot be read.") : info.error);
        }
        const int frameCount = info.frameCount;

        if (isCanceled(request))
        {
            return createFailure(PDFAppToolInputState::Damaged, QString());
        }

        // The first frame is decoded whole: a damaged image is found now, not by the run
        const QImage image = reader.read();
        if (image.isNull())
        {
            const QString error = reader.errorString();
            return createFailure(PDFAppToolInputState::Damaged, error.isEmpty() ? tr("The image cannot be read.") : tr("The image cannot be read: %1").arg(error));
        }

        result.state = PDFAppToolInputState::Ready;
        result.pageCount = frameCount;
        result.imageSize = image.size();
        result.imageDpiX = getDpi(image.dotsPerMeterX());
        result.imageDpiY = getDpi(image.dotsPerMeterY());
        result.data = request.data;
        return result;
    }

    static PDFAppToolInputLoader::Result readPdf(PDFAppToolInputLoader::Request request)
    {
        PDFAppToolInputLoader::Result result;
        result.kind = PDFAppToolInputKind::File;
        result.fileSize = request.data.size();

        PDFAppDocumentReading::Request readingRequest;
        readingRequest.data = request.data;
        readingRequest.isFromMemory = true;
        readingRequest.hasPassword = request.hasPassword;
        readingRequest.isOwnerOnly = request.isOwnerPassword;
        readingRequest.password = request.password;
        readingRequest.progress = request.progress;
        request.password.clear();

        PDFAppDocumentReading::Result reading = PDFAppDocumentReading::read(std::move(readingRequest));
        result.isEncrypted = reading.isEncrypted;
        switch (reading.status)
        {
            case PDFAppDocumentReading::Status::Loaded:
                break;

            case PDFAppDocumentReading::Status::PasswordRequired:
            case PDFAppDocumentReading::Status::PasswordWrong:
            {
                // An owner password, which is wrong, leaves the input restricted; a password of
                // the user, which is wrong, leaves it protected
                result.data = request.data;
                result.state = request.isOwnerPassword ? PDFAppToolInputState::Restricted : PDFAppToolInputState::PasswordRequired;
                result.isPasswordWrong = reading.status == PDFAppDocumentReading::Status::PasswordWrong;
                result.reason = request.isOwnerPassword ? PDFDocumentMerger::getPermissionText(PDFDocumentMerger::Permission::OwnerPasswordRequired, request.name)
                                                        : tr("The pages cannot be read yet.");
                return result;
            }

            case PDFAppDocumentReading::Status::Failed:
                return createFailure(PDFAppToolInputState::Damaged, reading.errorMessage);
        }

        result.data = request.data;
        result.isRepaired = reading.restoration != PDFAppDocumentRestoration::None;

        // A dynamic XFA form is a form of another kind: its pages are a layout of the
        // application (plan 5.2, XFA), it cannot be merged
        if (reading.isPagedXFA)
        {
            result.state = PDFAppToolInputState::Unsupported;
            result.reason = tr("The file is a dynamic XFA form; its pages cannot be merged.");
            return result;
        }

        result.document = reading.document;
        PDFAppToolInputLoader::fillPdfResult(result.document.data(), &result, request.name);
        return result;
    }

    /// Q15: the owner password of a snapshot of an open tab (its unsaved changes are not in a
    /// file, which could be read again) is verified - by a copy of the security handler of the
    /// snapshot, else by the original file of the tab; the snapshot stays the document
    static PDFAppToolInputLoader::Result verifySnapshot(PDFAppToolInputLoader::Request request)
    {
        PDFAppToolInputLoader::Result result;
        result.kind = PDFAppToolInputKind::OpenTab;
        result.fileSize = request.data.size();
        result.data = request.data;
        result.isEncrypted = true;

        const PDFSecurityHandler* securityHandler = request.snapshotDocument->getStorage().getSecurityHandler();
        const bool isVerified = securityHandler ? PDFDocumentMerger::verifyOwnerPassword(securityHandler, request.password)
                                                : (!request.data.isEmpty() && PDFDocumentMerger::verifyOwnerPassword(request.data, request.password));
        request.password.clear();
        if (!isVerified)
        {
            result.state = PDFAppToolInputState::Restricted;
            result.isPasswordWrong = true;
            result.reason = PDFDocumentMerger::getPermissionText(PDFDocumentMerger::getPermission(request.snapshotDocument.data()), request.name);
            return result;
        }

        result.document = request.snapshotDocument;
        result.isOwnerPasswordVerified = true;
        PDFAppToolInputLoader::fillPdfResult(result.document.data(), &result, request.name);
        return result;
    }
};

PDFAppToolInputLoader::Result::Result() :
    kind(PDFAppToolInputKind::File),
    state(PDFAppToolInputState::Damaged)
{

}

void PDFAppToolInputLoader::fillPdfResult(const PDFDocument* document, Result* result, const QString& name)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const size_t pageCount = catalog ? catalog->getPageCount() : 0;
    result->pageCount = PDFInteger(qMin<size_t>(pageCount, size_t(std::numeric_limits<PDFInteger>::max())));
    if (const PDFPage* page = pageCount > 0 ? catalog->getPage(0) : nullptr)
    {
        result->firstPageSize = page->getRotatedCropBox().size();
    }

    if (pageCount == 0)
    {
        result->state = PDFAppToolInputState::NoPages;
        result->reason = PDFAppToolInputLoaderHelper::tr("The document has no pages.");
        return;
    }

    QString reason;
    if (!isPermitted(document, &reason, name, result->isOwnerPasswordVerified))
    {
        result->state = PDFAppToolInputState::Restricted;
        result->reason = reason;
        return;
    }

    result->state = PDFAppToolInputState::Ready;
    result->reason.clear();
}

bool PDFAppToolInputLoader::isPermitted(const PDFDocument* document, QString* reason, const QString& name, bool isOwnerPasswordVerified)
{
    // Q2 (c), ISO 32000-2 Table 22: the result is not encrypted, it keeps none of the
    // restrictions of an input - the rule of the core, which checks it again for every caller;
    // a verified owner password permits a snapshot (Q15)
    const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(document, isOwnerPasswordVerified);
    if (permission == PDFDocumentMerger::Permission::Permitted)
    {
        return true;
    }

    if (reason)
    {
        *reason = PDFDocumentMerger::getPermissionText(permission, name);
    }
    return false;
}

PDFAppToolInputLoader::Result PDFAppToolInputLoader::read(Request request)
{
    using Helper = PDFAppToolInputLoaderHelper;

    Result result;
    if (request.data.isEmpty() && !request.isImageOnly)
    {
        Result failure;
        if (!Helper::readFile(&request, &failure))
        {
            failure.isPasswordWrong = false;
            if (request.progress)
            {
                request.progress->finishPhases();
            }
            return failure;
        }
    }

    if (Helper::isCanceled(request))
    {
        return Helper::createFailure(PDFAppToolInputState::Damaged, QString());
    }

    if (request.snapshotDocument)
    {
        result = Helper::verifySnapshot(request);
    }
    else if (request.data.isEmpty())
    {
        result = Helper::createFailure(PDFAppToolInputState::Damaged, Helper::tr("The image is empty."));
    }
    else if (!request.isImageOnly && PDFAppFileDrop::isPdfData(request.data))
    {
        result = Helper::readPdf(request);
    }
    else
    {
        result = Helper::readImage(request, request.isImageOnly ? PDFAppToolInputKind::Clipboard : PDFAppToolInputKind::Image);
    }

    request.password.clear();
    if (request.progress)
    {
        request.progress->finishPhases();
    }
    return result;
}

PDFAppToolInputLoader::PDFAppToolInputLoader(QObject* parent) :
    BaseClass(parent),
    m_link(std::make_shared<PDFAppToolInputLoaderLink>(this))
{

}

PDFAppToolInputLoader::~PDFAppToolInputLoader()
{
    // Nobody waits for the workers: they finish alone, their results are dropped
    m_link->detach();
    cancelAll();
}

void PDFAppToolInputLoader::start(Request request)
{
    cancel(request.id);

    Running running;
    running.serial = m_nextSerial++;
    running.progress = std::make_shared<PDFProgressCounter>();
    running.isCanceled = std::make_shared<std::atomic_bool>(false);
    request.progress = running.progress;
    request.isCanceled = running.isCanceled;
    m_running[request.id] = running;

    // The global pool of Qt (as the reading of a document tab); the task gets values only
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppToolInputLoaderTask(m_link, running.serial, std::move(request)));
}

void PDFAppToolInputLoader::cancel(const QString& id)
{
    auto it = m_running.find(id);
    if (it != m_running.end())
    {
        it->second.isCanceled->store(true, std::memory_order_relaxed);
        m_running.erase(it);
    }
}

void PDFAppToolInputLoader::cancelAll()
{
    for (auto& item : m_running)
    {
        item.second.isCanceled->store(true, std::memory_order_relaxed);
    }
    m_running.clear();
}

bool PDFAppToolInputLoader::isRunning(const QString& id) const
{
    return m_running.count(id) > 0;
}

int PDFAppToolInputLoader::getProgress(const QString& id) const
{
    auto it = m_running.find(id);
    if (it == m_running.end() || !it->second.progress)
    {
        return -1;
    }

    const PDFProgressCounter::Snapshot snapshot = it->second.progress->snapshot();
    return snapshot.count > 0 || snapshot.percentage > 0 ? snapshot.percentage : -1;
}

void PDFAppToolInputLoader::onTaskFinished(const std::shared_ptr<PDFAppToolInputLoaderLink>& link, quint64 serial, const Result& result)
{
    PDFThreadAnnotations::happensAfter(link.get());

    // A canceled reading or a reading replaced by a newer one of the same input
    QString id;
    for (auto it = m_running.begin(); it != m_running.end(); ++it)
    {
        if (it->second.serial == serial)
        {
            id = it->first;
            m_running.erase(it);
            break;
        }
    }
    if (id.isEmpty())
    {
        return;
    }

    Q_EMIT readingFinished(id, result);
}

}   // namespace pdf

#include "moc_pdfapptoolinputloader.cpp"
