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

#include "pdfappdocumentloader.h"
#include "pdfappdocument.h"
#include "pdfappcontext.h"
#include "pdfrecentrepository.h"

#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfform.h"
#include "pdfprogress.h"
#include "pdfsecurityhandler.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// State of the password callback of one reading (the reader calls the callback
/// in the worker thread, until it returns ok = false)
class PDFAppDocumentPasswordState
{
public:
    bool hasPassword = false;
    bool isRefused = false;     ///< The password of the request is not tried (too long)
    QString password;
    int calls = 0;
    bool isAsked = false;       ///< The reader has asked for a password
    bool isWrong = false;       ///< The password of the request was refused
};

/// Password callback of PDFDocumentReader: the password of the request is given
/// once; the next question means, that it was wrong. Nothing waits for the user.
class PDFAppDocumentPasswordCallback
{
public:
    explicit PDFAppDocumentPasswordCallback(PDFAppDocumentPasswordState* state) :
        m_state(state)
    {

    }

    QString operator()(bool* ok) const
    {
        ++m_state->calls;
        m_state->isAsked = true;

        if (m_state->hasPassword && !m_state->isRefused && m_state->calls == 1)
        {
            *ok = true;
            return m_state->password;
        }

        m_state->isWrong = m_state->hasPassword;
        *ok = false;
        return QString();
    }

private:
    PDFAppDocumentPasswordState* m_state;
};

PDFAppDocumentReading::Result PDFAppDocumentReading::read(Request request)
{
    // A worker thread: the decryption, the authentication by a certificate and the signatures may use
    // OpenSSL - its state of the thread goes with the job (PDFExecutionPolicy::releaseThreadResources,
    // ASan C), whichever way it ends
    const auto threadResourcesGuard = qScopeGuard(&PDFExecutionPolicy::releaseThreadResources);

    Result result;

    try
    {
        // A document from the memory has no file
        if (request.isFromMemory && request.data.isEmpty())
        {
            result.errorMessage = tr("The document is empty.");
            return result;
        }
        if (request.isFromMemory && request.data.size() > MAXIMUM_FILE_SIZE)
        {
            result.errorMessage = tr("The file is too large.");
            return result;
        }

        // The file is read once; a second reading with a password uses its bytes
        if (request.data.isEmpty())
        {
            const QFileInfo fileInfo(request.filePath);
            if (!fileInfo.isFile())
            {
                result.errorMessage = tr("The file does not exist.");
                return result;
            }
            if (fileInfo.size() > MAXIMUM_FILE_SIZE)
            {
                result.errorMessage = tr("The file is too large.");
                return result;
            }

            QFile file(request.filePath);
            if (!file.open(QFile::ReadOnly))
            {
                result.errorMessage = tr("The file cannot be read: %1").arg(file.errorString());
                return result;
            }
            request.data = file.readAll();
            file.close();

            if (request.data.isEmpty())
            {
                result.errorMessage = tr("The file is empty.");
                return result;
            }
        }
        result.data = request.data;

        // A password longer than the limit is not tried: it is wrong
        PDFAppDocumentPasswordState passwordState;
        passwordState.hasPassword = request.hasPassword;
        passwordState.isRefused = request.password.size() > MAXIMUM_PASSWORD_LENGTH;
        passwordState.password = passwordState.hasPassword && !passwordState.isRefused ? request.password : QString();
        request.password.clear();

        // The reader steps the counter, the GUI reads it by its timer
        PDFDocumentReader reader(request.progress.get(), PDFAppDocumentPasswordCallback(&passwordState), true, request.isOwnerOnly);
        reader.setOpenWithoutAttachments(request.isOpenWithoutAttachments);
        PDFDocument document = reader.readFromBuffer(request.data);
        passwordState.password.clear();
        result.isEmbeddedFilesOnlyEncryption = reader.isEmbeddedFilesOnlyEncryption();

        switch (reader.getReadingResult())
        {
            case PDFDocumentReader::Result::OK:
            {
                const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
                result.isEncrypted = securityHandler && securityHandler->getMode() != EncryptionMode::None;
                result.isAttachmentsLocked = securityHandler && securityHandler->isEmbeddedFilesLocked();
                result.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
                result.status = Status::Loaded;
                result.restoration = PDFAppDocument::getRestoration(reader);

                // A dynamic XFA form, whose layout has more pages or pages of other
                // sizes, is shown with the pages of its layout (step 8): the paged
                // document is a display version, the file keeps the original bytes
                if (PDFDocumentPointer pagedDocument = PDFXFAPaging::createPagedDocument(result.document.data()))
                {
                    result.document = std::move(pagedDocument);
                    result.isPagedXFA = true;
                }
                break;
            }

            case PDFDocumentReader::Result::Canceled:
                // The reader has asked for a password, which was not given or was wrong
                result.isEncrypted = true;
                result.status = passwordState.isWrong ? Status::PasswordWrong : Status::PasswordRequired;
                break;

            case PDFDocumentReader::Result::Failed:
                result.errorMessage = reader.getErrorMessage().isEmpty() ? tr("The file is not a valid PDF document.") : reader.getErrorMessage();
                break;
        }
    }
    catch (const PDFException& exception)
    {
        result.status = Status::Failed;
        result.document.reset();
        result.errorMessage = exception.getMessage();
    }

    if (request.progress)
    {
        request.progress->finishPhases();
    }
    return result;
}

PDFAppDocumentLoader::PDFAppDocumentLoader(PDFAppDocument* document) :
    BaseClass(document),
    m_document(document)
{

}

PDFAppDocumentLoader::~PDFAppDocumentLoader()
{
    // A running reading is dropped (the worker finishes alone, nothing waits)
    cancel();
    PDFSecureMemory::clear(m_password);
    PDFSecureMemory::clear(m_pendingPassword);
    m_password.clear();
    m_pendingPassword.clear();
}

void PDFAppDocumentLoader::start(const QString& filePath)
{
    if (!m_document || isRunning())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    m_filePath = canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath;
    m_data.clear();
    m_password.clear();
    m_hasPassword = false;

    PDFAppDocumentReading::Request request;
    request.filePath = m_filePath;

    // The registry finds the file at once (a second opening activates the tab)
    QPointer<PDFAppDocumentLoader> guard(this);
    m_document->setLoading(m_filePath);
    if (guard)
    {
        startReading(std::move(request), false);
    }
}

bool PDFAppDocumentLoader::startFromMemory(QByteArray data, const QString& title, const QString& originFilePath)
{
    if (!m_document || isRunning())
    {
        return false;
    }

    QPointer<PDFAppDocumentLoader> guard(this);
    if (!m_document->setLoadingFromMemory(title, originFilePath))
    {
        return false;
    }
    if (!guard || !m_document)
    {
        return false;
    }

    m_filePath.clear();
    m_data.clear();
    m_password.clear();
    m_hasPassword = false;

    PDFAppDocumentReading::Request request;
    request.data = std::move(data);
    request.isFromMemory = true;
    startReading(std::move(request), false);
    return true;
}

bool PDFAppDocumentLoader::setPassword(const QString& password)
{
    if (!m_document || isRunning() || m_document->getState() != PDFAppDocumentState::PasswordRequired || m_data.isEmpty())
    {
        return false;
    }

    PDFAppDocumentReading::Request request;
    request.filePath = m_filePath;
    request.data = m_data;
    request.hasPassword = true;
    request.password = password;

    // Kept for a reload of the file, if it is accepted
    m_password = password;
    m_hasPassword = true;

    QPointer<PDFAppDocumentLoader> guard(this);
    m_document->setLoading(m_filePath);
    if (guard)
    {
        startReading(std::move(request), false);
    }
    return true;
}

bool PDFAppDocumentLoader::reload()
{
    if (!m_document || isRunning() || !m_document->isReady() || m_document->getFilePath().isEmpty())
    {
        return false;
    }

    m_filePath = m_document->getFilePath();
    m_data.clear();

    PDFAppDocumentReading::Request request;
    request.filePath = m_filePath;
    request.hasPassword = m_hasPassword;
    request.password = m_password;
    request.isOpenWithoutAttachments = m_isAttachmentsLocked;
    startReading(std::move(request), true);
    return true;
}

bool PDFAppDocumentLoader::openWithoutAttachments()
{
    if (!m_document || isRunning() || m_document->getState() != PDFAppDocumentState::PasswordRequired || m_data.isEmpty() || !m_isEmbeddedFilesOnlyEncryption)
    {
        return false;
    }

    // No password: the attachments stay encrypted (Q5.9 c); nothing is kept for a reload
    PDFAppDocumentReading::Request request;
    request.filePath = m_filePath;
    request.data = m_data;
    request.isOpenWithoutAttachments = true;
    m_password.clear();
    m_hasPassword = false;

    QPointer<PDFAppDocumentLoader> guard(this);
    m_document->setLoading(m_filePath);
    if (guard)
    {
        startReading(std::move(request), false);
    }
    return true;
}

bool PDFAppDocumentLoader::unlockAttachments(const QString& password)
{
    if (!m_document || isRunning() || !m_document->isReady() || !m_isAttachmentsLocked || m_data.isEmpty())
    {
        return false;
    }

    // The same bytes with the password (the file is not read again); the result replaces the
    // document as the automatic refresh does (it was locked: there is nothing unsaved)
    PDFAppDocumentReading::Request request;
    request.filePath = m_filePath;
    request.data = m_data;
    request.hasPassword = true;
    request.password = password;
    m_pendingPassword = password;
    m_isAttachmentsPasswordWrong = false;
    m_isAttachmentsUnlock = true;
    startReading(std::move(request), true);
    return true;
}

bool PDFAppDocumentLoader::isLockedByUser(const PDFAppDocument* document)
{
    PDFAppContext* context = document ? document->getContext() : nullptr;
    if (!context || !context->getDatabase() || document->getFilePath().isEmpty())
    {
        return false;
    }
    const std::optional<PDFRecentFile> file = context->getRecent().getFile(document->getFilePath());
    return file && file->locked;
}

void PDFAppDocumentLoader::cancel()
{
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        delete m_watcher.data();
    }
    if (m_progressTimer)
    {
        m_progressTimer->stop();
    }
}

void PDFAppDocumentLoader::startReading(PDFAppDocumentReading::Request request, bool isReload)
{
    m_isReload = isReload;
    m_progress = std::make_shared<PDFProgressCounter>();
    request.progress = m_progress;

    if (!m_progressTimer)
    {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(PROGRESS_INTERVAL);
        connect(m_progressTimer.data(), &QTimer::timeout, this, &PDFAppDocumentLoader::onProgressTimeout);
    }

    // The worker gets its own copy of the request (the static function, no lambda)
    m_watcher = new QFutureWatcher<PDFAppDocumentReading::Result>(this);
    connect(m_watcher.data(), &QFutureWatcher<PDFAppDocumentReading::Result>::finished, this, &PDFAppDocumentLoader::onReadingFinished);
    m_watcher->setFuture(QtConcurrent::run(&PDFAppDocumentReading::read, std::move(request)));
    m_progressTimer->start();
}

void PDFAppDocumentLoader::onProgressTimeout()
{
    if (m_document && m_progress && !m_isReload)
    {
        // -1 until the reader has started (no bar of an unknown length)
        const PDFProgressCounter::Snapshot snapshot = m_progress->snapshot();
        m_document->setLoadingProgress(snapshot.count > 0 || snapshot.percentage > 0 ? snapshot.percentage : -1);
    }
}

void PDFAppDocumentLoader::onReadingFinished()
{
    // The watcher is the sender: it is deleted later
    QFutureWatcher<PDFAppDocumentReading::Result>* watcher = m_watcher.data();
    if (!watcher)
    {
        return;
    }
    m_watcher = nullptr;
    watcher->disconnect(this);
    watcher->deleteLater();
    if (m_progressTimer)
    {
        m_progressTimer->stop();
    }

    PDFAppDocumentReading::Result result = watcher->result();
    QPointer<PDFAppDocumentLoader> guard(this);
    if (!m_document)
    {
        return;
    }

    m_data = result.data;
    if (m_isAttachmentsUnlock)
    {
        // unlockAttachments (Q5.9 c): the document is replaced only by the right password; the
        // lock is lifted after the replacement (both emit - the state is set first)
        m_isAttachmentsUnlock = false;
        QString password = m_pendingPassword;
        PDFSecureMemory::clear(m_pendingPassword);
        m_pendingPassword.clear();
        if (result.status == PDFAppDocumentReading::Status::Loaded && !result.isAttachmentsLocked)
        {
            // Only the lock of the attachments goes: a lock of the user stays (owner 2026-11-15)
            const bool isUserLocked = isLockedByUser(m_document);
            m_password = password;
            m_hasPassword = true;
            m_isAttachmentsLocked = false;
            m_document->reload(result.document, std::move(result.data), result.isEncrypted, result.isPagedXFA, result.restoration);
            if (guard && m_document && !isUserLocked)
            {
                m_document->setLocked(false);
            }
        }
        else
        {
            m_isAttachmentsPasswordWrong = result.status != PDFAppDocumentReading::Status::Failed;
        }
        PDFSecureMemory::clear(password);
        if (guard)
        {
            Q_EMIT finished();
        }
        return;
    }

    m_isEmbeddedFilesOnlyEncryption = result.isEmbeddedFilesOnlyEncryption;
    switch (result.status)
    {
        case PDFAppDocumentReading::Status::Loaded:
            m_isAttachmentsLocked = result.isAttachmentsLocked;
            if (m_isReload)
            {
                m_document->reload(result.document, std::move(result.data), result.isEncrypted, result.isPagedXFA, result.restoration);
            }
            else
            {
                m_document->setLoaded(result.document, std::move(result.data), m_filePath, result.isEncrypted, result.isPagedXFA, result.restoration);
            }

            // Opened without its attachments: nothing is saved (tools.md 4.7), the encrypted
            // attachments are never written as if they were plain
            if (guard && m_document && m_isAttachmentsLocked)
            {
                m_document->setLocked(true);
            }
            break;

        case PDFAppDocumentReading::Status::PasswordRequired:
        case PDFAppDocumentReading::Status::PasswordWrong:
        {
            // The password is not kept, when it does not open the file
            const bool isWrong = result.status == PDFAppDocumentReading::Status::PasswordWrong && !m_isReload;
            m_password.clear();
            m_hasPassword = false;
            m_document->setPasswordRequired(m_filePath, isWrong);
            break;
        }

        case PDFAppDocumentReading::Status::Failed:
            if (!m_isReload)
            {
                m_data.clear();
                m_document->setFailed(m_filePath, result.errorMessage);
            }
            break;
    }

    if (guard)
    {
        Q_EMIT finished();
    }
}

}   // namespace pdf

#include "moc_pdfappdocumentloader.cpp"
