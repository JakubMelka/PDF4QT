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

#include "pdfupdatechecker.h"
#include "pdfappcontext.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinitions.h"
#include "pdfwhatsnew.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pdf
{

QUrl PDFUpdateStatus::getDownloadUrl() const
{
    return PDFUpdateChecker::getReleaseUrl(latestVersion);
}

PDFUpdateStatus PDFUpdateStatus::create(bool isAutoCheck, bool isCheckingHere, qint64 lastCheck, qint64 lastSuccess, const QString& latestVersion,
                                        const QString& error, const QVersionNumber& runningVersion, qint64 now)
{
    PDFUpdateStatus status;
    status.isAutoCheck = isAutoCheck;
    status.runningVersion = runningVersion;
    status.latestVersion = PDFWhatsNewParser::parseVersion(latestVersion);
    status.lastCheck = qMax<qint64>(lastCheck, 0);
    status.lastSuccess = qMax<qint64>(lastSuccess, 0);
    status.error = error;

    // A check reserved by another instance a moment ago (it clears the error)
    const bool isPendingElsewhere = status.lastCheck > status.lastSuccess && error.isEmpty() && status.lastCheck <= now &&
                                    now - status.lastCheck < PDFUpdateChecker::PENDING_LIMIT;

    if (isCheckingHere || isPendingElsewhere)
    {
        status.state = State::Checking;
    }
    else if (!error.isEmpty())
    {
        status.state = State::Failed;
    }
    else if (!status.latestVersion.isNull() && QVersionNumber::compare(status.latestVersion.normalized(), runningVersion.normalized()) > 0)
    {
        status.state = State::UpdateAvailable;
    }
    else if (!isAutoCheck)
    {
        status.state = State::AutoCheckOff;
    }
    else if (status.lastSuccess > 0)
    {
        status.state = State::UpToDate;
    }
    else
    {
        status.state = State::NotChecked;
    }

    return status;
}

QString PDFUpdateStatus::formatTime(qint64 time, qint64 now)
{
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(time);
    const QDate today = QDateTime::fromMSecsSinceEpoch(now).date();
    const QString clock = locale.toString(dateTime.time(), QStringLiteral("HH:mm"));

    if (dateTime.date() == today)
    {
        return QCoreApplication::translate("pdf::PDFUpdateStatus", "today at %1").arg(clock);
    }
    if (dateTime.date() == today.addDays(-1))
    {
        return QCoreApplication::translate("pdf::PDFUpdateStatus", "yesterday at %1").arg(clock);
    }
    return QCoreApplication::translate("pdf::PDFUpdateStatus", "%1 at %2").arg(formatDate(dateTime.date()), clock);
}

QString PDFUpdateStatus::formatDate(const QDate& date)
{
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale.toString(date, QStringLiteral("MMM d, yyyy"));
}

PDFUpdateChecker::PDFUpdateChecker(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context),
    m_manager(new QNetworkAccessManager(this)),
    m_url(getDefaultUrl()),
    m_runningVersion(PDFWhatsNewCatalog::getRunningVersion())
{
    setObjectName(QStringLiteral("updateChecker"));

    // Replies are deleted by the checker; no cache, no cookies (attributes of the request)
    m_manager->setAutoDeleteReplies(false);
    m_manager->setRedirectPolicy(QNetworkRequest::UserVerifiedRedirectPolicy);

    m_timer.setInterval(TIMER_INTERVAL);
    connect(&m_timer, &QTimer::timeout, this, &PDFUpdateChecker::onTimer);
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, &PDFUpdateChecker::onTimer);
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, &PDFUpdateChecker::onDeadline);
}

PDFUpdateChecker::~PDFUpdateChecker()
{
    m_timer.stop();
    m_startTimer.stop();
    m_deadlineTimer.stop();

    // The reply must not call the checker, which is being destroyed
    if (QNetworkReply* reply = m_reply)
    {
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        delete reply;
    }
}

PDFUpdateChecker* PDFUpdateChecker::getInstance(PDFAppContext* context)
{
    if (!context)
    {
        return nullptr;
    }

    PDFUpdateChecker* checker = context->findChild<PDFUpdateChecker*>(QStringLiteral("updateChecker"), Qt::FindDirectChildrenOnly);
    if (!checker)
    {
        checker = new PDFUpdateChecker(context, context);
    }
    return checker;
}

QUrl PDFUpdateChecker::getDefaultUrl()
{
    return QUrl(QStringLiteral("https://api.github.com/repos/JakubMelka/PDF4QT/releases/latest"));
}

QUrl PDFUpdateChecker::getReleaseUrl(const QVersionNumber& version)
{
    if (version.isNull())
    {
        return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases"));
    }
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/releases/tag/v%1").arg(version.toString()));
}

void PDFUpdateChecker::startAutomaticChecks(int delay)
{
    m_timer.start();
    m_startTimer.start(qMax(delay, 0));
}

qint64 PDFUpdateChecker::getCurrentTime() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

PDFUpdateStatus PDFUpdateChecker::getStatus() const
{
    const qint64 now = getCurrentTime();
    if (!m_context || !m_context->getDatabase())
    {
        return PDFUpdateStatus::create(true, isChecking(), 0, 0, QString(), QString(), m_runningVersion, now);
    }

    const PDFSettingsRepository settings = m_context->getSettings();
    return PDFUpdateStatus::create(settings.getBool(PDFSettingKeys::updateAutoCheck()), isChecking(),
                                   settings.getInteger(PDFSettingKeys::updateLastCheck()), settings.getInteger(PDFSettingKeys::updateLastSuccess()),
                                   settings.getString(PDFSettingKeys::updateLatestVersion()), settings.getString(PDFSettingKeys::updateLastError()),
                                   m_runningVersion, now);
}

bool PDFUpdateChecker::reserve(bool isForced)
{
    PDFDatabase* database = m_context ? m_context->getDatabase() : nullptr;
    PDFDatabaseConnection* connection = database ? database->getConnection() : nullptr;
    if (!connection)
    {
        return false;
    }

    // BEGIN IMMEDIATE: the read of the last check and its write are one step for all
    // instances, so only one of them sends the request
    PDFDatabaseTransaction transaction(connection, PDFDatabaseTransaction::Mode::Write);
    if (!transaction.isActive())
    {
        qWarning("The check for updates cannot be reserved: %s", qPrintable(transaction.getError().toString()));
        return false;
    }

    PDFSettingsRepository settings = m_context->getSettings();
    const qint64 now = getCurrentTime();
    if (!isForced)
    {
        if (!settings.getBool(PDFSettingKeys::updateAutoCheck()))
        {
            return false;
        }

        // A time in the future (the clock was set back) does not block the checks
        const qint64 lastCheck = settings.getInteger(PDFSettingKeys::updateLastCheck());
        if (lastCheck > 0 && lastCheck <= now && now - lastCheck < CHECK_INTERVAL)
        {
            return false;
        }
    }

    PDFDatabaseError error = settings.setValue(PDFSettingKeys::updateLastCheck(), qMax<qint64>(now, 1));
    if (!error.isError())
    {
        error = settings.resetValue(PDFSettingKeys::updateLastError());
    }

    if (error.isError() || !transaction.commit())
    {
        qWarning("The check for updates cannot be reserved: %s", qPrintable(error.isError() ? error.toString() : transaction.getError().toString()));
        return false;
    }

    return true;
}

bool PDFUpdateChecker::checkIfDue()
{
    if (isChecking() || !reserve(false))
    {
        return false;
    }

    startRequest();
    return true;
}

bool PDFUpdateChecker::checkNow()
{
    if (isChecking() || !reserve(true))
    {
        return false;
    }

    startRequest();
    return true;
}

void PDFUpdateChecker::startRequest()
{
    m_isRedirectRefused = false;
    m_isTooLarge = false;

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PDF4QT/%1").arg(m_runningVersion.toString()));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/vnd.github+json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(MAXIMUM_REDIRECTS);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(qMax(m_timeout, 1));

    ++m_requestCount;
    QNetworkReply* reply = m_manager->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, &PDFUpdateChecker::onReplyFinished);
    connect(reply, &QNetworkReply::redirected, this, &PDFUpdateChecker::onReplyRedirected);
    connect(reply, &QNetworkReply::downloadProgress, this, &PDFUpdateChecker::onDownloadProgress);
    m_deadlineTimer.start(qMax(m_timeout, 1));

    Q_EMIT statusChanged();
}

void PDFUpdateChecker::onReplyRedirected(const QUrl& url)
{
    QNetworkReply* reply = m_reply;
    if (!reply || sender() != reply)
    {
        return;
    }

    // Only https, or the same origin as the request (a loopback server of a test);
    // never a downgrade to http of another server
    const QUrl target = m_url.resolved(url);
    const bool isSameOrigin = target.scheme() == m_url.scheme() && target.host() == m_url.host() && target.port() == m_url.port();
    if (target.scheme() == QStringLiteral("https") || isSameOrigin)
    {
        Q_EMIT reply->redirectAllowed();
        return;
    }

    m_isRedirectRefused = true;
    reply->abort();
}

void PDFUpdateChecker::onDownloadProgress(qint64 received, qint64 total)
{
    Q_UNUSED(total);

    QNetworkReply* reply = m_reply;
    if (reply && sender() == reply && received > MAXIMUM_REPLY_SIZE)
    {
        m_isTooLarge = true;
        reply->abort();
    }
}

void PDFUpdateChecker::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply || sender() != reply)
    {
        return;
    }

    m_reply = nullptr;
    m_deadlineTimer.stop();
    reply->disconnect(this);
    reply->deleteLater();

    QVersionNumber version;
    QString error;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (m_isTooLarge)
    {
        error = tr("The answer of GitHub is too large.");
    }
    else if (m_isRedirectRefused)
    {
        error = tr("GitHub redirected the request to an address that is not secure.");
    }
    else if (reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError)
    {
        error = tr("GitHub did not answer in time. Check your connection.");
    }
    else if (reply->error() != QNetworkReply::NoError && httpStatus >= 400)
    {
        error = tr("GitHub answered with the error %1.").arg(httpStatus);
    }
    else if (reply->error() != QNetworkReply::NoError)
    {
        error = tr("GitHub could not be reached. Check your connection.");
    }
    else if (httpStatus != 200)
    {
        error = tr("GitHub answered with the error %1.").arg(httpStatus);
    }
    else
    {
        const QByteArray data = reply->read(MAXIMUM_REPLY_SIZE + 1);
        if (data.size() > MAXIMUM_REPLY_SIZE)
        {
            error = tr("The answer of GitHub is too large.");
        }
        else
        {
            version = parseReply(data, &error);
        }
    }

    finish(version, error);
}

void PDFUpdateChecker::finish(const QVersionNumber& version, const QString& error)
{
    QPointer<PDFUpdateChecker> guard(this);
    PDFDatabase* database = m_context ? m_context->getDatabase() : nullptr;
    PDFDatabaseConnection* connection = database ? database->getConnection() : nullptr;
    if (connection)
    {
        PDFSettingsRepository settings = m_context->getSettings();
        PDFDatabaseTransaction transaction(connection, PDFDatabaseTransaction::Mode::Write);
        PDFDatabaseError databaseError = transaction.getError();
        if (transaction.isActive())
        {
            if (!error.isEmpty())
            {
                databaseError = settings.setValue(PDFSettingKeys::updateLastError(), error.left(1024));
            }
            else
            {
                databaseError = settings.setValue(PDFSettingKeys::updateLatestVersion(), version.toString());
                if (!databaseError.isError())
                {
                    databaseError = settings.resetValue(PDFSettingKeys::updateLastError());
                }
                if (!databaseError.isError())
                {
                    databaseError = settings.setValue(PDFSettingKeys::updateLastSuccess(), qMax<qint64>(getCurrentTime(), 1));
                }
            }

            if (!databaseError.isError() && !transaction.commit())
            {
                databaseError = transaction.getError();
            }
        }

        if (databaseError.isError())
        {
            qWarning("The result of the check for updates cannot be stored: %s", qPrintable(databaseError.toString()));
        }
    }

    if (!error.isEmpty())
    {
        qInfo("Check for updates failed: %s", qPrintable(error));
    }

    if (guard)
    {
        Q_EMIT statusChanged();
    }
}

void PDFUpdateChecker::onTimer()
{
    checkIfDue();
}

void PDFUpdateChecker::onDeadline()
{
    // The whole request took too long (the transfer timeout of the request is
    // restarted by every received byte); the abort finishes it as a timeout
    if (QNetworkReply* reply = m_reply)
    {
        reply->abort();
    }
}

QVersionNumber PDFUpdateChecker::parseReply(const QByteArray& data, QString* error)
{
    QString message;
    QVersionNumber version;

    QJsonParseError parseError;
    const QJsonDocument document = data.size() <= MAXIMUM_REPLY_SIZE ? QJsonDocument::fromJson(data, &parseError) : QJsonDocument();
    if (data.size() > MAXIMUM_REPLY_SIZE)
    {
        message = tr("The answer of GitHub is too large.");
    }
    else if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        message = tr("The answer of GitHub is not valid.");
    }
    else
    {
        const QJsonValue tag = document.object().value(QStringLiteral("tag_name"));
        QString text = tag.isString() ? tag.toString() : QString();
        if (text.startsWith(QChar(u'v')) || text.startsWith(QChar(u'V')))
        {
            text.remove(0, 1);
        }

        version = text.size() <= 32 ? PDFWhatsNewParser::parseVersion(text) : QVersionNumber();
        if (version.isNull())
        {
            message = tr("The answer of GitHub has no valid version.");
        }
    }

    if (error)
    {
        *error = message;
    }
    return version;
}

}   // namespace pdf
