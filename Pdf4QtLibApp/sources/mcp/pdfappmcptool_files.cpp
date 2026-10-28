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

#include "pdfappmcptool_files.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcphost.h"
#include "pdfappmcpjob.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptoolset.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappundostack.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappsysteminfo.h"
#include "pdfapplog.h"
#include "pdfrepositoryhelper.h"

#include "pdfdatabase.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpjson.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QThread>
#include <QTimer>

namespace pdf
{

/// Helpers of the tool files
class PDFAppMcpFilesToolHelper
{
public:
    PDFAppMcpFilesToolHelper() = delete;

    static int s_passwordTimeout;

    static QString tr(const char* text, int count = -1)
    {
        return QCoreApplication::translate("pdf::PDFAppMcpFilesTool", text, nullptr, count);
    }

    static bool hasPdfHeader(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        return file.read(PDFAppFileDrop::HEADER_LENGTH).contains("%PDF-");
    }

    static QString getStateName(PDFAppDocumentState state)
    {
        switch (state)
        {
            case PDFAppDocumentState::Loading:
                return QStringLiteral("loading");
            case PDFAppDocumentState::PasswordRequired:
                return QStringLiteral("password_required");
            case PDFAppDocumentState::Failed:
                return QStringLiteral("failed");
            case PDFAppDocumentState::Ready:
                return QStringLiteral("ready");
        }
        return QStringLiteral("failed");
    }

    /// Result of files.open: the document, its state and the link to its information
    static PDFMcpCallResult createOpenResult(const PDFAppDocument* document)
    {
        PDFMcpJsonWriter writer;
        writer.beginObject();
        writer.writeMember(QLatin1StringView("action"), QLatin1StringView("open"));
        writer.key(QLatin1StringView("open"));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("documentId"), QStringView(document->getId()));
        writer.writeMember(QLatin1StringView("title"), QStringView(document->getTitle()));
        writer.writeMember(QLatin1StringView("pageCount"), qint64(document->isReady() ? document->getPageCount() : 0));
        writer.writeMember(QLatin1StringView("state"), QStringView(getStateName(document->getState())));
        writer.endObject();
        writer.endObject();
        const QByteArray structured = writer.take();

        PDFMcpContentBuilder builder;
        builder.addText(QString::fromUtf8(structured));
        builder.addResourceLink(QStringLiteral("pdf4qt://documents/%1/info").arg(document->getId()), document->getTitle(), QString(), QStringLiteral("application/json"));
        builder.setStructuredContent(structured);
        return builder.createCallResult(false);
    }

    /// files.open of a file open already: its tab is activated (and its page
    /// shown), the result names the document (plan 3.9; Read only)
    static void activateOpenDocument(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppDocument* document, int page, bool activate)
    {
        const QString actionId = QStringLiteral("files.open");
        if (!host || !call || call->isFinished())
        {
            return;
        }

        PDFAppMainWindow* window = host->getResolver().getWindow();
        PDFAppDocumentTab* tab = document ? host->getResolver().findTab(document) : nullptr;
        QPointer<PDFAppDocument> documentGuard(document);
        QPointer<PDFAppMcpHost> hostGuard(host);
        if (window && tab && activate)
        {
            window->activateTab(tab);
        }
        if (!hostGuard || call->isFinished())
        {
            return;
        }
        if (tab && documentGuard && documentGuard->isReady() && page >= 1 && page <= documentGuard->getPageCount())
        {
            if (PDFAppDocumentView* view = tab->getView())
            {
                view->goToPage(page - 1);
            }
        }
        if (!hostGuard || call->isFinished())
        {
            return;
        }
        if (!documentGuard)
        {
            host->finishWithError(call, actionId, nullptr, QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
            return;
        }
        host->recordActivity(call, actionId, documentGuard, QJsonObject(), PDFMcpActivityStatus::Ok);
        call->finish(createOpenResult(documentGuard));
    }
};

/// files.open of a file open already with Read documents = Ask: the tab is
/// activated after the read approval of the assistant [D2] - one approval per
/// assistant, shared with the other reads (owner decision 2026-10-27, item 26)
class PDFAppMcpOpenActivation : public PDFAppMcpDeferredRead
{
public:
    PDFAppMcpOpenActivation(PDFAppDocument* document, int page, bool activate) :
        m_document(document),
        m_page(page),
        m_activate(activate)
    {

    }

    virtual void proceed(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call) override
    {
        if (!m_document)
        {
            // Closed while the question waited
            if (host)
            {
                host->finishWithError(call, QStringLiteral("files.open"), nullptr, QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
            }
            return;
        }
        PDFAppMcpFilesToolHelper::activateOpenDocument(host, call, m_document, m_page, m_activate);
    }

    virtual void refuse(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(host);

        // The row of the question shows the decision already
        if (call && !call->isFinished())
        {
            call->finish(PDFMcpCallResult::createToolError(PDFAppMcpReadSupport::getReadRefusedText(decision)));
        }
    }

private:
    QPointer<PDFAppDocument> m_document;
    int m_page = 0;
    bool m_activate = true;
};

int PDFAppMcpFilesToolHelper::s_passwordTimeout = PDFAppMcpFilesTool::DEFAULT_PASSWORD_TIMEOUT;

void PDFAppMcpFilesTool::setPasswordTimeout(int timeout)
{
    PDFAppMcpFilesToolHelper::s_passwordTimeout = qMax(0, timeout);
}

int PDFAppMcpFilesTool::getPasswordTimeout()
{
    return PDFAppMcpFilesToolHelper::s_passwordTimeout;
}

QString PDFAppMcpFilesTool::checkFilePath(const QString& path, Access access, const PDFAppContext* context, QString* errorMessage)
{
    using Helper = PDFAppMcpFilesToolHelper;

    // The common rules of the paths (links resolved, the folders of PDF4QT)
    QString error;
    QString result = PDFAppMcpArguments::checkPath(path, access == Access::Write, context, &error);
    if (!result.isEmpty())
    {
        if (access == Access::Open && !QFileInfo(result).isFile())
        {
            error = QStringLiteral("the file does not exist");
        }
        else if (access == Access::Open && !Helper::hasPdfHeader(result))
        {
            error = QStringLiteral("the file is not a PDF file (no %PDF header) or it cannot be read");
        }
        result = error.isEmpty() ? result : QString();
    }

    if (errorMessage)
    {
        *errorMessage = error;
    }
    return error.isEmpty() ? result : QString();
}

class PDFAppMcpFileRequest;

/// Write of a save or a copy in a worker (the writer of step 3)
class PDFAppMcpFileWriteJob : public PDFAppMcpJob
{
public:
    PDFAppMcpFileWriteJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, std::shared_ptr<PDFAppMcpFileRequest> request,
                          QString filePath, bool isIncremental, QString markSourceFilePath, bool mayReplace) :
        PDFAppMcpJob(std::move(call), std::move(snapshot)),
        m_request(std::move(request)),
        m_filePath(std::move(filePath)),
        m_isIncremental(isIncremental),
        m_markSourceFilePath(std::move(markSourceFilePath)),
        m_mayReplace(mayReplace)
    {

    }

    virtual void execute() override
    {
        // The last check right before the write: a file or a link, which has
        // appeared since the confirmation, is never replaced by a copy (an
        // overwrite is always confirmed as such - plan 3.9, review RW7-0)
        const QFileInfo target(m_filePath);
        if (!m_mayReplace && (target.exists() || target.isSymLink()))
        {
            m_result = PDFAppDocumentFileWriter::Result();
            m_result.requestedFilePath = m_filePath;
            m_result.errorMessage = QStringLiteral("the file exists already; choose another name");
            return;
        }

        PDFAppDocumentFileWriter::Request request;
        request.document = getSnapshot().document;
        request.originalData = getSnapshot().originalData;
        request.filePath = m_filePath;
        request.isIncremental = m_isIncremental && !request.originalData.isEmpty();
        request.version = getSnapshot().version;
        request.markSourceFilePath = m_markSourceFilePath;
        m_result = PDFAppDocumentFileWriter::write(request);
    }

    virtual void finish(PDFAppMcpHost* host) override;

private:
    std::shared_ptr<PDFAppMcpFileRequest> m_request;
    QString m_filePath;
    bool m_isIncremental = false;
    QString m_markSourceFilePath;
    bool m_mayReplace = false;
    PDFAppDocumentFileWriter::Result m_result;
};

/// One request of the tool files: the confirmation, the write or the opening
class PDFAppMcpFileRequest : public PDFAppMcpConfirmationReceiver, public std::enable_shared_from_this<PDFAppMcpFileRequest>
{
public:
    enum class Mode
    {
        Open,
        Save,
        SaveCopy
    };

    PDFAppMcpFileRequest(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, Mode mode, PDFAppDocument* document, QString path) :
        m_host(host),
        m_call(std::move(call)),
        m_mode(mode),
        m_document(document),
        m_path(std::move(path))
    {

    }

    ~PDFAppMcpFileRequest()
    {
        // The write did not report back (the host has stopped meanwhile): the
        // operation Saving of the document ends anyway (the last reference is
        // released in the GUI thread then: the host has waited for its jobs)
        PDFAppDocumentOperation* operation = m_operation.data();
        if (operation && operation->thread() == QThread::currentThread())
        {
            m_operation = nullptr;
            operation->finish();
        }
    }

    QString getActionId() const
    {
        switch (m_mode)
        {
            case Mode::Open:
                return QStringLiteral("files.open");
            case Mode::Save:
                return QStringLiteral("files.save");
            case Mode::SaveCopy:
                return QStringLiteral("files.save_copy");
        }
        return QString();
    }

    void setOverwrite(bool isOverwrite) { m_isOverwrite = isOverwrite; }
    void setOpenOptions(int page, bool activate) { m_page = page; m_activate = activate; }

    /// The permission and the confirmation (GUI thread)
    void start()
    {
        PDFAppMcpHost* host = m_host;
        if (!host || !m_call)
        {
            return;
        }

        const QString assistantKey = m_call->getRequest().client.assistantKey;
        const PDFAppMcpPermissionPolicy& policy = host->getPolicy();
        const PDFAppMcpPolicyResult policyResult = policy.evaluate(PDFAppMcpPermissionKind::Files, assistantKey, true);
        if (policyResult == PDFAppMcpPolicyResult::Denied)
        {
            fail(QStringLiteral("The user does not allow assistants to open and save files (Open and save files is set to Deny in PDF4QT)."), PDFMcpActivityStatus::DeniedUser);
            return;
        }

        // Overwriting the original file is always confirmed (958:28030), a request,
        // which came while a modal dialog was open, too
        const bool isAlwaysConfirmed = m_mode == Mode::Save || (m_mode == Mode::SaveCopy && m_isOverwrite);
        const bool isAfterDialog = QApplication::activeModalWidget() != nullptr;
        if (policyResult == PDFAppMcpPolicyResult::Allowed && !isAlwaysConfirmed && !isAfterDialog)
        {
            proceed(PDFMcpActivityStatus::Ok);
            return;
        }

        // At most 16 requests wait in one tab (plan 2.5)
        if (host->isConfirmationQueueFull(m_document))
        {
            fail(PDFAppMcpHost::getConfirmationQueueFullText(), PDFMcpActivityStatus::Failed);
            return;
        }

        std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::make_shared<PDFAppMcpConfirmation>();
        confirmation->kind = m_mode == Mode::Open ? PDFAppMcpConfirmationKind::Open : (m_mode == Mode::Save ? PDFAppMcpConfirmationKind::Save : PDFAppMcpConfirmationKind::SaveCopy);
        confirmation->permissions = policy.getKindsToConfirm(PDFAppMcpPermissionKind::Files, assistantKey, true);
        confirmation->call = m_call;
        confirmation->document = m_document;
        confirmation->actionId = getActionId();
        confirmation->isAlwaysConfirmed = isAlwaysConfirmed;
        confirmation->isAfterDialog = isAfterDialog;
        confirmation->details.insert(QStringLiteral("path"), QDir::toNativeSeparators(m_path));
        confirmation->details.insert(QStringLiteral("overwrite"), m_isOverwrite);
        if (m_document)
        {
            const int unsaved = m_document->getUnsavedChangeCount();
            QString unsavedText;
            if (unsaved == 1 && m_document->getUndoStack())
            {
                unsavedText = m_document->getUndoStack()->getUndoDescription();
            }
            else if (unsaved != 0)
            {
                unsavedText = unsaved > 0 ? (unsaved == 1 ? PDFAppMcpFilesToolHelper::tr("%n change", unsaved) : PDFAppMcpFilesToolHelper::tr("%n changes", unsaved)) : PDFAppMcpFilesToolHelper::tr("Changes");
            }
            confirmation->details.insert(QStringLiteral("unsaved"), unsavedText);
            confirmation->details.insert(QStringLiteral("content"), unsavedText.isEmpty() ? m_document->getTitle()
                                                                                          : PDFAppMcpFilesToolHelper::tr("%1 with %2").arg(m_document->getTitle(), unsavedText));
            if (const int signatureCount = PDFAppDocumentFileWriter::getSignatureCount(m_document->getDocument().data()))
            {
                QJsonArray facts;
                QJsonObject fact;
                fact.insert(QStringLiteral("key"), PDFAppMcpFilesToolHelper::tr("Signatures"));
                fact.insert(QStringLiteral("value"), PDFAppMcpFilesToolHelper::tr("%n kept - the changes are appended", signatureCount));
                facts.append(fact);
                confirmation->details.insert(QStringLiteral("facts"), facts);
            }
        }
        confirmation->receiver = std::shared_ptr<PDFAppMcpConfirmationReceiver>(shared_from_this(), static_cast<PDFAppMcpConfirmationReceiver*>(this));
        m_holder = shared_from_this();

        // The decision may come at once (the host deleted or shut down from a slot
        // of its signal): it releases the holder, this request stays alive here
        const auto keepAlive = shared_from_this();
        host->requestConfirmation(confirmation);
        m_activityId = confirmation->activityId;
    }

    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(confirmationId);

        std::shared_ptr<PDFAppMcpFileRequest> holder = std::move(m_holder);
        if (!m_host || !m_call || m_call->isFinished())
        {
            return;
        }

        if (decision != PDFAppMcpDecision::Allowed)
        {
            m_call->finish(PDFMcpCallResult::createToolError(PDFAppMcpChangeJob::getDecisionText(decision)));
            return;
        }

        proceed(PDFMcpActivityStatus::Allowed);
    }

    /// The write has ended (GUI thread)
    void onWritten(const PDFAppDocumentFileWriter::Result& result)
    {
        if (QPointer<PDFAppDocumentOperation> operation = m_operation)
        {
            m_operation = nullptr;
            operation->finish();
        }

        if (!m_host || !m_call || m_call->isFinished())
        {
            return;
        }

        if (!result.isSaved && m_call->isCanceled())
        {
            // Canceled before the job ran: nothing has been written
            fail(QStringLiteral("The request was canceled."), PDFMcpActivityStatus::Canceled);
            return;
        }

        if (!result.isSaved)
        {
            fail(QStringLiteral("The file could not be written: %1").arg(result.errorMessage.isEmpty() ? QStringLiteral("an error of the system") : result.errorMessage),
                 PDFMcpActivityStatus::Failed);
            return;
        }

        PDFAppDocument* document = m_document;
        if (m_mode == Mode::Save && document && document->isReady())
        {
            // The written version is saved (a document changed meanwhile stays modified)
            document->setSavedAs(result.version, result.filePath, result.data);

            // The state of the document notifies the application at once: a slot may
            // close the tab or the window (UnitTestsAppMcpLifetime)
            document = m_document;
        }

        // Status message "Project_Report.pdf saved" (955:144508)
        if (document && m_host)
        {
            if (PDFAppDocumentTab* tab = m_host->getResolver().findTab(document))
            {
                if (PDFAppDocumentView* view = tab->getView())
                {
                    if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
                    {
                        statusBar->showMessage(PDFAppMcpFilesToolHelper::tr("%1 saved").arg(QFileInfo(result.filePath).fileName()), 10000);
                    }
                }
            }
        }

        PDFAppMcpOutputMembers output;
        if (m_mode == Mode::Save)
        {
            output.emplace_back(QStringLiteral("saved"), true);
        }
        output.emplace_back(QStringLiteral("path"), QDir::toNativeSeparators(result.filePath));
        succeed(PDFAppMcpChangeJob::createResult(m_mode == Mode::Save ? QStringLiteral("save") : QStringLiteral("save_copy"), document, output));
    }

    /// The document of open is ready, needs its password, failed or was closed
    void onOpenFinished(PDFAppDocument* document, bool isTimeout)
    {
        if (!m_call || m_call->isFinished())
        {
            return;
        }

        if (!document)
        {
            fail(QStringLiteral("PDF4QT could not open the file (the tab was closed or the file cannot be read)."), PDFMcpActivityStatus::Failed);
            return;
        }

        switch (document->getState())
        {
            case PDFAppDocumentState::Ready:
                succeed(PDFAppMcpFilesToolHelper::createOpenResult(document));
                return;
            case PDFAppDocumentState::PasswordRequired:
                fail(isTimeout ? QStringLiteral("The document needs a password, which the user did not type in PDF4QT within 2 minutes (state password_required). The password is typed only in PDF4QT.")
                               : QStringLiteral("The document needs a password (state password_required). Ask the user to type it in PDF4QT."),
                     PDFMcpActivityStatus::Failed);
                return;
            case PDFAppDocumentState::Failed:
                fail(QStringLiteral("PDF4QT could not open the file (state failed): %1").arg(PDFMcpJson::sanitizeString(document->getErrorMessage(), PDFMcpJson::SanitizeFlag::RemoveBidi, 200)),
                     PDFMcpActivityStatus::Failed);
                return;
            case PDFAppDocumentState::Loading:
                fail(QStringLiteral("The document is still loading in PDF4QT (state loading). Call application.get_state later."), PDFMcpActivityStatus::Failed);
                return;
        }
    }

private:
    void proceed(PDFMcpActivityStatus status);

    void fail(const QString& text, PDFMcpActivityStatus status)
    {
        if (!m_call || m_call->isFinished())
        {
            return;
        }

        if (m_host && m_activityId > 0 && m_host->getActivityLog())
        {
            QJsonObject detail;
            if (status == PDFMcpActivityStatus::Failed)
            {
                detail.insert(QStringLiteral("reason"), PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi, 200));
            }
            m_host->getActivityLog()->update(m_activityId, status, detail);
            m_call->finish(PDFMcpCallResult::createToolError(text));
            return;
        }

        if (m_host)
        {
            m_host->finishWithError(m_call, getActionId(), m_document, text, status);
        }
        else
        {
            m_call->finish(PDFMcpCallResult::createToolError(text));
        }
    }

    void succeed(const PDFMcpCallResult& result)
    {
        if (!m_call || m_call->isFinished())
        {
            return;
        }

        if (m_host)
        {
            if (m_activityId > 0 && m_host->getActivityLog())
            {
                m_host->getActivityLog()->update(m_activityId, PDFMcpActivityStatus::Allowed);
            }
            else
            {
                m_host->recordActivity(m_call, getActionId(), m_document, QJsonObject(), m_status);
            }
        }
        m_call->finish(result);
    }

    friend class PDFAppMcpOpenWaiter;

    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFMcpCall> m_call;
    Mode m_mode = Mode::Open;
    QPointer<PDFAppDocument> m_document;
    QString m_path;
    bool m_isOverwrite = false;
    int m_page = 0;
    bool m_activate = true;
    qint64 m_activityId = 0;
    PDFMcpActivityStatus m_status = PDFMcpActivityStatus::Ok;
    QPointer<PDFAppDocumentOperation> m_operation;
    std::shared_ptr<PDFAppMcpFileRequest> m_holder;
};

/// Waits for the document of files.open: ready, the password typed (2 min from
/// the password screen of the tab, not from the request - the reading of a large
/// file does not shorten the time of the user), failed or closed; the reading
/// itself is bounded by DEFAULT_LOADING_TIMEOUT; a child of the host
class PDFAppMcpOpenWaiter : public QObject
{
public:
    PDFAppMcpOpenWaiter(PDFAppMcpHost* host, std::shared_ptr<PDFAppMcpFileRequest> request, QString canonicalPath) :
        QObject(host),
        m_request(std::move(request)),
        m_canonicalPath(std::move(canonicalPath))
    {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &PDFAppMcpOpenWaiter::onTimeout);
        m_timer->start(PDFAppMcpFilesTool::DEFAULT_LOADING_TIMEOUT);

        // A call canceled while the file is read or the password screen waits ends
        // at once (nobody else answers it; every cancellation has its activity row)
        m_cancelTimer = new QTimer(this);
        m_cancelTimer->setInterval(CANCEL_CHECK_INTERVAL);
        connect(m_cancelTimer, &QTimer::timeout, this, &PDFAppMcpOpenWaiter::onCancelCheck);
        m_cancelTimer->start();

        PDFAppContext* context = host ? host->getContext() : nullptr;
        m_registry = context ? context->getDocumentRegistry() : nullptr;
        if (m_registry)
        {
            connect(m_registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppMcpOpenWaiter::onDocumentAdded);
            attach(m_registry->findByCanonicalPath(m_canonicalPath));
        }

        // The window closed before its document was announced: the tab is gone, the
        // call ends at once instead of after the password timeout (UnitTestsAppMcpLifetime)
        if (PDFAppMainWindow* window = host ? host->getResolver().getWindow() : nullptr)
        {
            connect(window, &QObject::destroyed, this, &PDFAppMcpOpenWaiter::onWindowDestroyed);
        }
    }

    void attach(PDFAppDocument* document)
    {
        if (!document || m_document)
        {
            return;
        }

        m_document = document;
        m_hasDocument = true;
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppMcpOpenWaiter::check);
        connect(document, &QObject::destroyed, this, &PDFAppMcpOpenWaiter::onDocumentDestroyed);
        check();
    }

private:
    void onDocumentAdded(PDFAppDocument* document)
    {
        if (document && !m_document && QFileInfo(document->getFilePath()).canonicalFilePath() == m_canonicalPath)
        {
            attach(document);
        }
    }

    void check()
    {
        PDFAppDocument* document = m_document;
        if (!document || !m_request)
        {
            return;
        }

        // A password: the tab shows its password screen, the call waits
        const PDFAppDocumentState state = document->getState();
        if (state == PDFAppDocumentState::Ready || state == PDFAppDocumentState::Failed)
        {
            end(document, false);
        }
        else if (state == PDFAppDocumentState::PasswordRequired && m_request->m_call)
        {
            // The time of the user starts with the password screen (once: a wrong
            // password typed keeps the state and the time)
            if (!m_isWaitingForPassword && m_timer)
            {
                m_isWaitingForPassword = true;
                m_timer->start(qMax(1, PDFAppMcpFilesTool::getPasswordTimeout()));
            }
            m_request->m_call->reportProgress(50.0, 100.0, QStringLiteral("Waiting for the password in PDF4QT"));
        }
    }

    void onDocumentDestroyed()
    {
        // The slot of destroyed only forgets the document
        end(nullptr, false);
    }

    void onWindowDestroyed()
    {
        // A document attached already ends by its own destroyed (QWidget emits
        // destroyed before it deletes its children); the slot reads no member of the window
        if (!m_document)
        {
            end(nullptr, false);
        }
    }

    void onTimeout()
    {
        end(m_document.data(), true);
    }

    void onCancelCheck()
    {
        const std::shared_ptr<PDFMcpCall> call = m_request ? m_request->m_call : nullptr;
        if (!call || call->isFinished())
        {
            // Answered elsewhere: nothing waits any more
            end(nullptr, false);
            return;
        }

        if (call->isCanceled())
        {
            std::shared_ptr<PDFAppMcpFileRequest> request = std::move(m_request);
            end(nullptr, false);
            request->fail(QStringLiteral("The request was canceled."), PDFMcpActivityStatus::Canceled);
        }
    }

    void end(PDFAppDocument* document, bool isTimeout)
    {
        std::shared_ptr<PDFAppMcpFileRequest> request = std::move(m_request);
        if (m_timer)
        {
            m_timer->stop();
        }
        if (m_cancelTimer)
        {
            m_cancelTimer->stop();
        }
        if (request)
        {
            request->onOpenFinished(document, isTimeout || !m_hasDocument);
        }
        deleteLater();
    }

    std::shared_ptr<PDFAppMcpFileRequest> m_request;
    QString m_canonicalPath;
    QPointer<PDFAppDocumentRegistry> m_registry;
    QPointer<PDFAppDocument> m_document;
    /// Interval of the check of a canceled call (as PDFAppMcpTextIndexWaiter)
    static constexpr int CANCEL_CHECK_INTERVAL = 250;

    QPointer<QTimer> m_timer;
    QPointer<QTimer> m_cancelTimer;
    bool m_hasDocument = false;
    bool m_isWaitingForPassword = false;
};

void PDFAppMcpFileWriteJob::finish(PDFAppMcpHost* host)
{
    Q_UNUSED(host);

    if (m_request)
    {
        m_request->onWritten(m_result);
    }
}

void PDFAppMcpFileRequest::proceed(PDFMcpActivityStatus status)
{
    m_status = status;
    PDFAppMcpHost* host = m_host;
    if (!host || !m_call || m_call->isFinished())
    {
        return;
    }

    if (m_call->isCanceled())
    {
        fail(QStringLiteral("The request was canceled."), PDFMcpActivityStatus::Canceled);
        return;
    }

    if (m_mode == Mode::Open)
    {
        PDFAppMainWindow* window = host->getResolver().getWindow();
        if (!window)
        {
            fail(QStringLiteral("PDF4QT has no window to open the file in."), PDFMcpActivityStatus::Failed);
            return;
        }

        // A file read by the window as the user opens it (Recent records it)
        QPointer<PDFAppTab> previousTab = window->getCurrentTab();
        QPointer<PDFAppMainWindow> windowGuard(window);
        PDFAppOpenRequest request;
        request.filePath = m_path;
        request.page = qMax(0, m_page);
        std::shared_ptr<PDFAppMcpFileRequest> self = shared_from_this();
        PDFAppMcpOpenWaiter* waiter = new PDFAppMcpOpenWaiter(host, self, m_path);
        QPointer<PDFAppMcpOpenWaiter> waiterGuard(waiter);
        window->openDocuments(std::vector<PDFAppOpenRequest>({ request }));
        if (windowGuard && !m_activate && previousTab)
        {
            windowGuard->activateTab(previousTab);
        }
        if (waiterGuard && host->getContext() && host->getContext()->getDocumentRegistry())
        {
            waiterGuard->attach(host->getContext()->getDocumentRegistry()->findByCanonicalPath(m_path));
        }
        return;
    }

    // An open editor of a form field is saved too, as by Save of the user (step 4,
    // package D); the commit runs the code of the application
    if (m_document && m_document->isReady() && m_document->hasPendingEdits())
    {
        std::shared_ptr<PDFAppMcpFileRequest> self = shared_from_this();
        m_document->commitPendingEdits();
        if (!m_host || !m_call || m_call->isFinished())
        {
            return;
        }
        host = m_host;
    }

    PDFAppDocument* document = m_document;
    if (!document || !document->isReady())
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }
    if (m_mode == Mode::Save && document->getFilePath() != m_path)
    {
        // Saved as another file by the user while the confirmation waited: the
        // confirmed file is not the file of the document any more
        fail(QStringLiteral("The document was saved as another file in PDF4QT meanwhile; call save again."), PDFMcpActivityStatus::Failed);
        return;
    }
    if (m_mode == Mode::Save && document->isLocked())
    {
        fail(PDFAppMcpChangeJob::getLockedText(), PDFMcpActivityStatus::DeniedLocked);
        return;
    }
    if (document->isBlockedByOperation())
    {
        fail(QStringLiteral("PDF4QT is busy with another operation on the document (saving, printing or a tool). Try again when it ends."), PDFMcpActivityStatus::DeniedBusy);
        return;
    }

    // The target checked again after the confirmation (up to 2 minutes): the
    // rules of the paths and a file, which has appeared meanwhile
    if (m_mode == Mode::SaveCopy)
    {
        QString error;
        const QString path = PDFAppMcpFilesTool::checkFilePath(m_path, PDFAppMcpFilesTool::Access::Write, host->getContext(), &error);
        if (path.isEmpty() || path != m_path)
        {
            fail(QStringLiteral("path: %1").arg(path.isEmpty() ? error : QStringLiteral("the path has changed since the confirmation; try again")), PDFMcpActivityStatus::Failed);
            return;
        }

        const QFileInfo target(m_path);
        if (!m_isOverwrite && (target.exists() || target.isSymLink()))
        {
            fail(QStringLiteral("path: the file exists already; choose another name, or pass overwrite: true (the user is always asked)."), PDFMcpActivityStatus::Failed);
            return;
        }
    }

    // A signed document keeps its signatures: the changes are appended
    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    const bool isIncremental = PDFAppDocumentFileWriter::getSignatureCount(snapshot.document.data()) > 0;
    const QString originFilePath = document->getOriginFilePath();

    // The operation is shown by the tab (its title, the commands): a slot may close
    // the tab or the window, or delete the host (UnitTestsAppMcpLifetime)
    const auto keepAlive = shared_from_this();
    PDFAppDocumentOperation* operation = document->beginOperation(PDFAppDocumentOperationKind::Save, PDFAppMcpFilesToolHelper::tr("Saving"), false);
    m_operation = operation;
    if (!m_host || !m_call || m_call->isFinished())
    {
        return;
    }
    if (!m_document)
    {
        fail(QStringLiteral("The tab of the document was closed in PDF4QT."), PDFMcpActivityStatus::DeniedClosed);
        return;
    }
    m_host->startJob(std::make_shared<PDFAppMcpFileWriteJob>(m_call, snapshot, shared_from_this(), m_path, isIncremental, originFilePath, m_mode == Mode::Save || m_isOverwrite));
}

void PDFAppMcpFilesTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    PDFAppMcpHost* host = invocation.host;
    const std::shared_ptr<PDFMcpCall>& call = invocation.call;
    const QString actionId = invocation.getActionId();
    const QJsonObject& arguments = invocation.actionArguments;
    PDFAppContext* context = host->getContext();

    if (invocation.actionName == QLatin1String("open"))
    {
        QString error;
        const QString path = checkFilePath(arguments.value(QStringLiteral("path")).toString(), Access::Open, context, &error);
        if (path.isEmpty())
        {
            host->finishWithError(call, actionId, nullptr, QStringLiteral("path: %1. See pdf4qt://docs/files.md").arg(error), PDFMcpActivityStatus::Failed);
            return;
        }

        const int page = arguments.value(QStringLiteral("page")).toInt(0);
        const bool activate = arguments.value(QStringLiteral("activate")).toBool(true);

        // A file open already: Read only (plan 3.9) - with Read documents = Ask the
        // read approval of the assistant [D2] first, as every read (item 26)
        PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
        if (PDFAppDocument* document = registry ? registry->findByCanonicalPath(path) : nullptr)
        {
            std::shared_ptr<PDFAppMcpOpenActivation> continuation = std::make_shared<PDFAppMcpOpenActivation>(document, page, activate);
            switch (PDFAppMcpReadSupport::checkRead(host, call, actionId, document, continuation))
            {
                case PDFAppMcpReadAccess::Denied:
                    host->finishWithError(call, actionId, document, QStringLiteral("The user does not allow assistants to read documents (Read documents is set to Deny in PDF4QT)."),
                                          PDFMcpActivityStatus::DeniedUser);
                    return;
                case PDFAppMcpReadAccess::Asked:
                    return;
                case PDFAppMcpReadAccess::Allowed:
                    break;
            }

            PDFAppMcpFilesToolHelper::activateOpenDocument(host, call, document, page, activate);
            return;
        }

        std::shared_ptr<PDFAppMcpFileRequest> request = std::make_shared<PDFAppMcpFileRequest>(host, call, PDFAppMcpFileRequest::Mode::Open, nullptr, path);
        request->setOpenOptions(page, activate);
        request->start();
        return;
    }

    // save, save_copy: the document of the tab (a locked document may save a copy)
    const bool isCopy = invocation.actionName == QLatin1String("save_copy");
    PDFAppDocument* document = PDFAppMcpChangeJob::prepareDocument(invocation, isCopy);
    if (!document)
    {
        return;
    }

    QString path;
    if (!isCopy)
    {
        path = document->getFilePath();
        if (path.isEmpty())
        {
            host->finishWithError(call, actionId, document, QStringLiteral("The document has no file yet (a copy or an attachment); use save_copy with a path."), PDFMcpActivityStatus::Failed);
            return;
        }
        if (!PDFAppDocumentFileWriter::isWritable(path))
        {
            host->finishWithError(call, actionId, document, QStringLiteral("The file of the document cannot be written (read only or its folder is missing); use save_copy."), PDFMcpActivityStatus::Failed);
            return;
        }
    }
    else
    {
        // The path of the copy: given, chosen by the client (elicitation), else an error
        QString requestedPath = arguments.value(QStringLiteral("path")).toString();
        if (requestedPath.isEmpty())
        {
            bool isDeclined = false;
            const std::optional<QJsonObject> answer = PDFAppMcpChoice::getAnswer(call->getRequest(), &isDeclined);
            if (isDeclined)
            {
                host->finishWithError(call, actionId, document, QStringLiteral("The user declined to choose the path of the copy."), PDFMcpActivityStatus::Failed);
                return;
            }
            requestedPath = answer ? answer->value(QStringLiteral("path")).toString() : QString();
        }

        if (requestedPath.isEmpty())
        {
            const QFileInfo info(document->getFilePath().isEmpty() ? document->getTitle() : document->getFilePath());
            const QString folder = document->getFilePath().isEmpty() ? QDir::homePath() : info.absolutePath();
            PDFAppMcpChoice::Property property;
            property.name = QStringLiteral("path");
            property.title = QStringLiteral("Absolute path of the copy (.pdf)");
            property.defaultValue = QDir::toNativeSeparators(folder + QLatin1Char('/') + info.completeBaseName() + QStringLiteral("_copy.pdf"));
            PDFAppMcpChoice::ask(host, call, actionId, document, QStringLiteral("Where should PDF4QT save the copy of %1?").arg(document->getTitle()), { property },
                                 QStringLiteral("save_copy needs path: the absolute path of the copy (.pdf)."));
            return;
        }

        QString error;
        path = checkFilePath(requestedPath, Access::Write, context, &error);
        if (path.isEmpty())
        {
            host->finishWithError(call, actionId, document, QStringLiteral("path: %1. See pdf4qt://docs/files.md").arg(error), PDFMcpActivityStatus::Failed);
            return;
        }

        const bool isOverwrite = arguments.value(QStringLiteral("overwrite")).toBool(false);
        if (QFileInfo::exists(path) && !isOverwrite)
        {
            host->finishWithError(call, actionId, document, QStringLiteral("path: the file exists already; choose another name, or pass overwrite: true (the user is always asked)."), PDFMcpActivityStatus::Failed);
            return;
        }

        const QString ownPath = QFileInfo(document->getFilePath()).canonicalFilePath();
        PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
        if ((!ownPath.isEmpty() && ownPath == path) || (registry && registry->findByCanonicalPath(path)))
        {
            host->finishWithError(call, actionId, document, QStringLiteral("path: the file is open in PDF4QT; use save for the document itself, or another name."), PDFMcpActivityStatus::Failed);
            return;
        }

        std::shared_ptr<PDFAppMcpFileRequest> request = std::make_shared<PDFAppMcpFileRequest>(host, call, PDFAppMcpFileRequest::Mode::SaveCopy, document, path);
        request->setOverwrite(QFileInfo::exists(path));
        request->start();
        return;
    }

    std::shared_ptr<PDFAppMcpFileRequest> request = std::make_shared<PDFAppMcpFileRequest>(host, call, PDFAppMcpFileRequest::Mode::Save, document, path);
    request->start();
}

}   // namespace pdf
