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

#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdfprogress.h"
#include "pdfsecurityhandler.h"

#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <new>

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

/// Receiver of the progress of the reader in the worker thread (direct
/// connection): it only stores the percentage, the GUI reads it by a timer
class PDFAppDocumentProgressReceiver
{
public:
    explicit PDFAppDocumentProgressReceiver(std::shared_ptr<std::atomic<int>> progress) :
        m_progress(std::move(progress))
    {

    }

    void operator()(int percentage) const
    {
        if (m_progress)
        {
            m_progress->store(qBound(0, percentage, 100));
        }
    }

private:
    std::shared_ptr<std::atomic<int>> m_progress;
};

PDFAppDocumentReading::Result PDFAppDocumentReading::read(Request request)
{
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

        PDFProgress progress(nullptr);
        QObject::connect(&progress, &PDFProgress::progressStep, &progress, PDFAppDocumentProgressReceiver(request.progress), Qt::DirectConnection);

        PDFDocumentReader reader(&progress, PDFAppDocumentPasswordCallback(&passwordState), true, false);
        PDFDocument document = reader.readFromBuffer(request.data);
        passwordState.password.clear();

        switch (reader.getReadingResult())
        {
            case PDFDocumentReader::Result::OK:
            {
                const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
                result.isEncrypted = securityHandler && securityHandler->getMode() != EncryptionMode::None;
                result.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
                result.status = Status::Loaded;
                break;
            }

            case PDFDocumentReader::Result::Cancelled:
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
    catch (const std::bad_alloc&)
    {
        result.status = Status::Failed;
        result.document.reset();
        result.errorMessage = tr("There is not enough memory to open the file.");
    }

    if (request.progress)
    {
        request.progress->store(100);
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
    m_password.clear();
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
    startReading(std::move(request), true);
    return true;
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
    m_progress = std::make_shared<std::atomic<int>>(-1);
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
        m_document->setLoadingProgress(m_progress->load());
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
    switch (result.status)
    {
        case PDFAppDocumentReading::Status::Loaded:
            if (m_isReload)
            {
                m_document->reload(result.document, std::move(result.data), result.isEncrypted);
            }
            else
            {
                m_document->setLoaded(result.document, std::move(result.data), m_filePath, result.isEncrypted);
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
