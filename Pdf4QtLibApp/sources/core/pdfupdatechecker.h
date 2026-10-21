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

#ifndef PDFUPDATECHECKER_H
#define PDFUPDATECHECKER_H

#include "pdfappglobal.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace pdf
{

class PDFAppContext;

/// State of the update check shown by the card Update status of What's New
/// (Figma pdf4qt-whatsnew-update-states 989:14257). It is derived from the
/// settings update.* of the database (shared by all instances) and from the
/// request running in this instance.
class PDF4QTLIBAPPSHARED_EXPORT PDFUpdateStatus
{
public:
    enum class State
    {
        NotChecked,         ///< No check has finished yet (the automatic check is on)
        Checking,           ///< A check runs (in this instance, or one reserved by another instance a moment ago)
        UpToDate,           ///< The running version is the latest one
        UpdateAvailable,    ///< GitHub has a newer version
        Failed,             ///< The last check failed (reason in getError)
        AutoCheckOff        ///< The automatic check is off (Settings › General › Updates)
    };

    State state = State::NotChecked;
    bool isAutoCheck = true;
    QVersionNumber runningVersion;
    QVersionNumber latestVersion;

    /// Time of the last start of a check and of the last successful check (ms since the epoch, 0 = never)
    qint64 lastCheck = 0;
    qint64 lastSuccess = 0;

    /// Reason of the failure of the last check
    QString error;

    /// Release page of the latest version on GitHub (Download from GitHub)
    QUrl getDownloadUrl() const;

    /// Derives the state (a pure function of the stored values)
    static PDFUpdateStatus create(bool isAutoCheck, bool isCheckingHere, qint64 lastCheck, qint64 lastSuccess, const QString& latestVersion,
                                  const QString& error, const QVersionNumber& runningVersion, qint64 now);

    /// Time of a check for the card: "today at 09:12", "yesterday at 08:40",
    /// "Sep 30, 2026 at 10:00" (local time, American English)
    static QString formatTime(qint64 time, qint64 now);

    /// Date of a release "Sep 14, 2026"
    static QString formatDate(const QDate& date);
};

/// Check for updates (newapp_whatsnew.md 5): asks GitHub for the latest release
/// (QNetworkAccessManager, User-Agent PDF4QT/<version>, no cookies, no other
/// identification), at most once a day automatically and only shows the result -
/// nothing is downloaded or installed.
///
/// The automatic check runs START_DELAY after the start and then every hour, when
/// update.autoCheck is on and update.lastCheck is older than CHECK_INTERVAL. Writing
/// update.lastCheck in a BEGIN IMMEDIATE transaction before the request reserves the
/// check: of more instances only one asks GitHub, the others read the result from the
/// database (update.latestVersion, update.lastError, update.lastSuccess) through the
/// notifier. The reply is untrusted: its size, its JSON, the types of its values, the
/// version and every redirect (only https, or the same origin) are checked, it has a
/// time limit.
///
/// One checker belongs to a context (getInstance); pages read its status and connect
/// to statusChanged and to the notifier of the database.
class PDF4QTLIBAPPSHARED_EXPORT PDFUpdateChecker : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// The automatic check runs at most once in this interval (24 hours)
    static constexpr qint64 CHECK_INTERVAL = qint64(24) * 3600 * 1000;

    /// Interval of the timer of the automatic check (one hour)
    static constexpr int TIMER_INTERVAL = 3600 * 1000;

    /// Delay of the first automatic check after the start
    static constexpr int START_DELAY = 15 * 1000;

    /// A check reserved by another instance is shown as Checking for this time
    static constexpr qint64 PENDING_LIMIT = 2 * 60 * 1000;

    /// Largest reply, which is read
    static constexpr qint64 MAXIMUM_REPLY_SIZE = 1024 * 1024;

    /// Time limit of the request
    static constexpr int DEFAULT_TIMEOUT = 30 * 1000;

    /// Most redirects of the request
    static constexpr int MAXIMUM_REDIRECTS = 5;

    explicit PDFUpdateChecker(PDFAppContext* context, QObject* parent = nullptr);
    virtual ~PDFUpdateChecker() override;

    /// Checker of the context (created as a child of the context at the first call)
    static PDFUpdateChecker* getInstance(PDFAppContext* context);

    /// Address of the latest release of PDF4QT on GitHub (API)
    static QUrl getDefaultUrl();

    /// Release page of the version on GitHub
    static QUrl getReleaseUrl(const QVersionNumber& version);

    /// Starts the automatic checks: the first one after \p delay, then every hour
    void startAutomaticChecks(int delay = START_DELAY);

    /// Starts a check, when the automatic check is on and the last check (of any
    /// instance) is older than CHECK_INTERVAL. \returns true, if a request was sent.
    bool checkIfDue();

    /// Check for updates / Try Again: starts a check now (also with the automatic
    /// check off). \returns true, if a request was sent (false: a check runs
    /// already, the database cannot be written).
    bool checkNow();

    /// A request of this instance runs
    bool isChecking() const { return !m_reply.isNull(); }

    /// Current status (reads the database)
    PDFUpdateStatus getStatus() const;

    /// Address of the request (tests: a loopback server)
    QUrl getUrl() const { return m_url; }
    void setUrl(const QUrl& url) { m_url = url; }

    /// Clock in milliseconds since the epoch (tests); an empty function = the real clock
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    qint64 getCurrentTime() const;

    /// Time limit of the request in milliseconds: no data for this time, or the
    /// whole request longer than this time (a server sending a byte now and then)
    void setTimeout(int timeout) { m_timeout = timeout; }

    /// Running version (tests; PDF4QT_PROJECT_VERSION by default)
    void setRunningVersion(const QVersionNumber& version) { m_runningVersion = version; }
    const QVersionNumber& getRunningVersion() const { return m_runningVersion; }

    /// Number of the requests sent by this checker
    int getRequestCount() const { return m_requestCount; }

    /// Version of the latest release of a reply of GitHub ({"tag_name": "v1.6.0.0"}).
    /// A null version and the reason for a reply, which is not valid.
    static QVersionNumber parseReply(const QByteArray& data, QString* error);

Q_SIGNALS:
    /// A check of this instance has started or finished (the result is in the database)
    void statusChanged();

private:
    bool reserve(bool isForced);
    void startRequest();
    void finish(const QVersionNumber& version, const QString& error);
    void onReplyFinished();
    void onReplyRedirected(const QUrl& url);
    void onDownloadProgress(qint64 received, qint64 total);
    void onTimer();
    void onDeadline();

    QPointer<PDFAppContext> m_context;
    QNetworkAccessManager* m_manager = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QTimer m_startTimer;
    QTimer m_deadlineTimer;     ///< Whole time of the request (the transfer timeout restarts at each received byte)
    QUrl m_url;
    std::function<qint64()> m_clock;
    int m_timeout = DEFAULT_TIMEOUT;
    QVersionNumber m_runningVersion;
    int m_requestCount = 0;
    bool m_isRedirectRefused = false;
    bool m_isTooLarge = false;
};

}   // namespace pdf

#endif // PDFUPDATECHECKER_H
