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

#ifndef PDFAPPINSTANCE_H
#define PDFAPPINSTANCE_H

#include "pdfappglobal.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <vector>

class QLocalServer;
class QLocalSocket;
class QWidget;

namespace pdf
{

class PDFAppCommandLine;

/// Request of a new process to the running instance (decision 11 of the plan): open
/// the files as new tabs of the window of the instance, which holds the name of the
/// forwarding (the first started one), at the page (0 = none). A start without files
/// forwards nothing (it is a new window, PDFAppInstanceGuard::Mode::ListenIfFree).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInstanceRequest
{
public:
    /// Absolute paths (relative paths are resolved by the starting process)
    QStringList files;
    int page = 0;

    /// Window activation token of the starting process (Wayland XDG_ACTIVATION_TOKEN,
    /// X11 DESKTOP_STARTUP_ID), empty = none (PDFAppActivation)
    QString activationToken;

    /// Errors of the command line of the starting process (translated texts), the
    /// receiving window shows them; a missing file is not an error here, it is
    /// reported, when the file is opened
    QStringList errors;

    bool operator==(const PDFAppInstanceRequest&) const = default;
};

/// Messages between the instances. A frame is the magic "P4QI", the length of the
/// payload (32 bits, big endian) and the payload: JSON {"version": 1, "files": [...],
/// "page": n, "activationToken": "...", "errors": [...]}; the reply is
/// {"version": 1, "ok": true} or {"ok": false, "error": "..."}. Everything received is
/// untrusted: version, sizes, types, absolute paths (no device paths of Windows), the
/// range of the page, the token and the errors are checked.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInstanceProtocol
{
public:
    PDFAppInstanceProtocol() = delete;

    static constexpr int VERSION = 1;
    static constexpr qsizetype HEADER_SIZE = 8;
    static constexpr qsizetype MAXIMUM_MESSAGE_SIZE = 1024 * 1024;
    static constexpr qsizetype MAXIMUM_REPLY_SIZE = 4096;
    static constexpr qsizetype MAXIMUM_PATH_LENGTH = 32 * 1024;
    static constexpr qsizetype MAXIMUM_FILES = 500;
    static constexpr qsizetype MAXIMUM_TOKEN_LENGTH = 1024;
    static constexpr qsizetype MAXIMUM_ERRORS = 64;
    static constexpr qsizetype MAXIMUM_ERROR_LENGTH = 1024;

    /// Request of the command line of this process (files, page, errors within the
    /// limits of the protocol) with the token of PDFAppActivation::readStartupToken
    static PDFAppInstanceRequest createRequest(const PDFAppCommandLine& commandLine, const QString& activationToken);

    /// The token is printable ASCII without spaces and not longer than the limit
    static bool isValidToken(const QString& token);

    /// The path is absolute and it is not a device path of Windows (\\.\ or \\?\)
    static bool isAcceptedPath(const QString& path);

    /// The path is in a device namespace of Windows (\\.\ or \\?\, any separators)
    static bool isDevicePath(const QString& path);

    /// Result of the reading of a frame from a buffer
    enum class FrameResult
    {
        Incomplete,     ///< More data are needed
        Complete,       ///< The payload is available, the frame was removed from the buffer
        Invalid         ///< Wrong magic or size
    };

    /// Frame of the payload
    static QByteArray createFrame(const QByteArray& payload);

    /// Reads one frame from \p buffer
    static FrameResult readFrame(QByteArray& buffer, qsizetype maximumSize, QByteArray* payload);

    static QByteArray encodeRequest(const PDFAppInstanceRequest& request);
    static bool decodeRequest(const QByteArray& payload, PDFAppInstanceRequest* request, QString* error);

    static QByteArray encodeReply(bool ok, const QString& error);
    static bool decodeReply(const QByteArray& payload, bool* ok, QString* error);
};

class PDFAppInstanceConnection;

/// Server of the running instance (QLocalServer, accessible only by the user).
/// It answers each valid request with an acknowledgment and then emits
/// openRequested; the shell opens the files and raises the window. Invalid,
/// oversized and stalled messages are refused, nothing else happens. While the
/// application is quitting (suspend), valid requests are held without an answer:
/// a canceled quitting (resume) acknowledges and opens them, an accepted one
/// (quit) refuses them - the other process then starts by itself, so it never
/// thinks its files were delivered to a window, which is closing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInstanceServer : public QObject
{
    Q_OBJECT

public:
    /// At most this number of connections is served at once
    static constexpr int MAXIMUM_CONNECTIONS = 16;

    /// A connection, which does not send its message in this time, is closed
    static constexpr int DEFAULT_READ_TIMEOUT = 5000;

    /// Interval of the attempts to listen again after a canceled quitting, while
    /// another process holds the lock of the instances (PDFAppInstanceGuard)
    static constexpr int RETRY_INTERVAL = 200;

    /// Default time limit of these attempts: a starting process holds the lock for
    /// its time limit of the forwarding, a lock of a hung process is stale after
    /// PDFAppInstanceGuard::STALE_LOCK_TIME and it is removed by the next attempt
    static constexpr int DEFAULT_RETRY_LIMIT = 40000;

    explicit PDFAppInstanceServer(QObject* parent = nullptr);
    virtual ~PDFAppInstanceServer() override;

    /// Starts listening. \returns false, if the name is in use or the server cannot listen.
    bool listen(const QString& serverName);

    /// Removes a stale endpoint of the name and listens (only under the lock of
    /// PDFAppInstanceGuard, so a live server of another instance is not removed)
    bool listenRemovingStale(const QString& serverName);

    /// Stops listening and closes the connections
    void close();

    /// Start of the quitting of the application (newapplication.md 3): the server stops
    /// listening, before the questions about unsaved changes are asked. Connections,
    /// which are waiting or already accepted, are kept and their requests are held
    /// without an answer until resume or quit, so no request is lost; a process
    /// started later runs by itself. A server, which has not listened again after
    /// a canceled quitting yet, holds the requests again (and does not listen).
    void suspend();

    /// The user has canceled the quitting: the held requests are acknowledged and
    /// emitted (openRequested) and the server listens again under the name of the
    /// last listen, unless another instance has taken the name meanwhile (then the
    /// application runs without the forwarding). An open connection of this server
    /// holds the name too (an instance of the pipe on Windows): the server tries to
    /// listen again, when its last connection has closed. The call never waits for
    /// the lock of the instances (it runs in the GUI thread): while a starting process
    /// holds it, the server tries again from the event loop every RETRY_INTERVAL for
    /// at most \p retryLimit milliseconds (counted from this call, or from the closing
    /// of the last connection). \returns true, if the server listens now. The server
    /// may be deleted by a slot of openRequested.
    bool resume(int retryLimit, QString* error = nullptr);

    /// The quitting has been accepted: the server stops listening and refuses every
    /// connection, which has not been answered (held requests, messages still being
    /// read, messages arriving later) - the other processes start by themselves.
    /// The server does not listen again (resume returns false). The server may be
    /// deleted by a slot of requestRefused.
    void quit();

    /// The quitting has been accepted (quit)
    bool isQuitting() const { return m_isQuitting; }

    /// Number of the held requests (suspended server)
    int getHeldRequestCount() const;

    /// The server was suspended and it has not listened again
    bool isSuspended() const { return m_isSuspended; }

    /// Name of the last listen (also while suspended)
    const QString& getRequestedServerName() const { return m_serverName; }

    bool isListening() const;
    QString getServerName() const;
    QString getErrorString() const;

    /// Read timeout of a connection in milliseconds (tests)
    void setReadTimeout(int timeout);
    int getReadTimeout() const { return m_readTimeout; }

    /// Number of open connections
    int getConnectionCount() const;

Q_SIGNALS:
    /// A valid request has been acknowledged (open the files, raise the window)
    void openRequested(const pdf::PDFAppInstanceRequest& request);

    /// A message was refused (for the log and the tests)
    void requestRefused(const QString& reason);

private:
    friend class PDFAppInstanceConnection;

    void onNewConnection();
    void onRequestReady();
    void onRequestRefused(const QString& reason);
    void onConnectionDestroyed(QObject* object);

    void closeServer();

    /// Acknowledges the request of the connection and emits openRequested (the
    /// server may be deleted then)
    void deliver(PDFAppInstanceConnection* connection);

    /// Delivers all held requests (the server may be deleted then)
    void deliverHeldRequests();

    /// A suspended server, whose quitting was canceled, listens again, when its last
    /// connection has closed (resume could not listen while it was open) or when the
    /// lock of the instances is free (m_retryTimer)
    void retryResume();

    /// One attempt to listen again without waiting for the lock; schedules the next
    /// attempt, when the lock is held by another process and the time limit allows it
    bool listenAgain(QString* error);

    /// Open connections, which are still connections of the server
    QList<QPointer<PDFAppInstanceConnection>> getConnections() const;

    /// A connection of the server has its socket still open (Windows: it holds the
    /// name of the pipe)
    bool hasOpenConnection() const;

    QLocalServer* m_server = nullptr;
    QList<QObject*> m_connections;
    QString m_serverName;
    QTimer m_retryTimer;
    QDeadlineTimer m_retryDeadline;
    int m_retryLimit = DEFAULT_RETRY_LIMIT;
    int m_readTimeout = DEFAULT_READ_TIMEOUT;
    bool m_isSuspended = false;
    bool m_isHolding = false;
    bool m_isQuitting = false;
};

/// One connection of the server: reads one message, tells the server, that the
/// request is ready (the server acknowledges it at once, holds it while the
/// application is quitting, or refuses it), answers it and closes. A held request
/// is refused (the peer starts by itself) three quarters of the read timeout after
/// the connection was accepted, so before its client gives up waiting.
class PDFAppInstanceConnection : public QObject
{
    Q_OBJECT

public:
    PDFAppInstanceConnection(QLocalSocket* socket, int readTimeout, QObject* parent);
    virtual ~PDFAppInstanceConnection() override;

    /// A valid request has been read and it waits for the answer
    bool isHeld() const { return m_isHeld; }

    /// The connection has answered
    bool isFinished() const { return m_isFinished; }

    /// The socket is not closed yet
    bool isOpen() const;

    /// Request of a held connection
    const PDFAppInstanceRequest& getRequest() const { return m_request; }

    /// Answers the held request with the acknowledgment and closes
    void acknowledge();

    /// Answers with the refusal (also before the message was read) and closes;
    /// emits requestRefused (the connection may be deleted then)
    void refuse(const QString& reason);

Q_SIGNALS:
    /// A valid request has been read (isHeld), the server answers it
    void requestReady();
    void requestRefused(const QString& reason);

private:
    void onReadyRead();
    void processBuffer();
    void onTimeout();
    void onDisconnected();
    void finish();

    QPointer<QLocalSocket> m_socket;
    QTimer m_timer;
    QElapsedTimer m_age;
    QByteArray m_buffer;
    PDFAppInstanceRequest m_request;
    int m_readTimeout = PDFAppInstanceServer::DEFAULT_READ_TIMEOUT;
    bool m_isFinished = false;
    bool m_isHeld = false;
    bool m_isProcessingScheduled = false;
};

/// Client of a new process: sends the request and waits for the acknowledgment
/// (blocking, called in main before the event loop runs)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInstanceClient
{
public:
    PDFAppInstanceClient() = delete;

    enum class Result
    {
        Delivered,  ///< The running instance has acknowledged the request
        NoServer,   ///< No instance listens (or a stale endpoint of a crashed one)
        Failed      ///< A server exists, but it has not acknowledged in time or refused
    };

    /// Default time to wait for the acknowledgment
    static constexpr int DEFAULT_TIMEOUT = 5000;

    /// Windows: the longest wait for a free instance of the pipe. A live server keeps
    /// free instances (the backlog of QLocalServer) and creates a new one from its
    /// event loop within milliseconds; a pipe busy for longer is a server, which is
    /// quitting with an open connection (no instance comes until the user answers
    /// its questions), so the starting process runs by itself without waiting the
    /// whole timeout.
    static constexpr int PIPE_WAIT_LIMIT = 1000;

    /// Sends the request. The server must be a process of the same user (Windows: the
    /// owner of the process of the pipe, Linux: the credentials of the socket), else
    /// nothing is sent (Failed). On Windows the server process is then allowed to bring
    /// its window to the foreground (AllowSetForegroundWindow) before the request is
    /// sent. \p serverProcessId receives the process id of the server, when the
    /// platform tells it (0 otherwise). \p timeout limits the whole exchange including
    /// the connection: on Windows a pipe without a free instance (a server, which is
    /// busy or quitting with an open connection) is waited for at most PIPE_WAIT_LIMIT
    /// within it (QLocalSocket::connectToServer would wait a fixed 5 s), then Failed.
    static Result send(const QString& serverName, const PDFAppInstanceRequest& request, int timeout, QString* error,
                       qint64* serverProcessId = nullptr);
};

/// Messages of the startup notification of X11 (freedesktop.org Startup Notification
/// Protocol 0.1): the launcher shows a busy cursor or a launch feedback until the
/// started application sends "remove: ID=<DESKTOP_STARTUP_ID>". A process, which
/// forwards its files to the running instance, never shows a window, so the running
/// instance ends the notification of its token (newapplication.md 3).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStartupNotification
{
public:
    PDFAppStartupNotification() = delete;

    /// Bytes of a message in one client message (format 8) of the protocol
    static constexpr int CHUNK_SIZE = 20;

    /// The value of a key of a message: a space, '"' and '\' are preceded by '\'
    static QByteArray escapeValue(const QByteArray& value);

    /// The message, which ends the startup notification of \p startupId
    static QByteArray createRemoveMessage(const QString& startupId);

    /// The message with its terminating NUL in chunks of CHUNK_SIZE bytes (the last
    /// one padded by NUL bytes): the first one is sent as _NET_STARTUP_INFO_BEGIN,
    /// the others as _NET_STARTUP_INFO
    static std::vector<QByteArray> splitMessage(const QByteArray& message);

    /// The time of the user action of the launch: the number of the suffix
    /// "_TIME<n>" of the id (an X server time of 32 bits). \returns false, when the
    /// id has no such suffix.
    static bool getTimestamp(const QString& startupId, quint32* timestamp);
};

/// Messages of the activation of a window on X11, which Qt does not send for a
/// shown window (the tests replace them by PDFAppActivation::setX11Messenger)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppX11Messenger
{
public:
    virtual ~PDFAppX11Messenger();

    /// Sends the chunks of a message of the startup notification
    /// (PDFAppStartupNotification::splitMessage) to the root window of \p window.
    /// \returns false, when they cannot be sent.
    virtual bool sendStartupMessage(QWidget* window, const std::vector<QByteArray>& chunks) = 0;

    /// Asks the window manager to activate \p window (_NET_ACTIVE_WINDOW of an
    /// application) by the time of the user action of the launch. \returns false,
    /// when the request cannot be sent.
    virtual bool requestActivation(QWidget* window, quint32 timestamp) = 0;
};

/// Activation of the window of the running instance (newapplication.md 3): the token
/// of the starting process is read before QApplication is created (the platform
/// plugin consumes it), it travels with the request and the receiving window uses it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppActivation
{
public:
    PDFAppActivation() = delete;

    /// Window system of the activation
    enum class Platform
    {
        Automatic,  ///< The platform of the process (setPlatformOverride: none)
        Windows,
        Wayland,
        X11,
        Other       ///< macOS, offscreen, ...
    };

    /// How the window has been activated
    enum class Result
    {
        None,               ///< No window
        Activated,          ///< Activated by Qt (Windows: the starting process has allowed it)
        ActivatedByToken,   ///< Wayland: by the XDG activation token
        ActivatedByStartupId,///< X11: the startup notification ended, activated by its time
        Alerted             ///< The token cannot be used: activated by Qt and the user alerted
    };

    /// XDG_ACTIVATION_TOKEN (Wayland) or DESKTOP_STARTUP_ID (X11) of this process,
    /// empty when there is none or it is not valid. Call it before QApplication exists.
    static QString readStartupToken();

    /// Restores a minimized window (a maximized one stays maximized), raises and
    /// activates it. Wayland: by the token of the starting process. X11: the startup
    /// notification of the token is ended and the window is activated by the time
    /// of the token. Windows: the starting process has allowed it. A token, which
    /// cannot be used, and no token alert the user (except on Windows).
    static Result activateWindow(QWidget* window, const QString& activationToken);

    /// The platform of the activation (the override, else the platform of the process)
    static Platform getPlatform();

    /// Tests: the platform of the activation; Automatic restores the real one
    static void setPlatformOverride(Platform platform);

    /// Tests: the X11 messages; nullptr restores the messages to the X server
    /// (the messenger is not owned)
    static void setX11Messenger(PDFAppX11Messenger* messenger);

private:
    static Platform& getPlatformOverride();
    static PDFAppX11Messenger*& getX11MessengerOverride();
    static PDFAppX11Messenger* getX11Messenger();
};

/// Decides, whether this process forwards its files to a running instance or
/// becomes the instance, which listens (decision 11). The whole decision is done
/// under a lock file of the user, so two processes started at the same time never
/// both listen: the second one waits for the lock and then forwards to the first.
/// Every failure ends in Standalone - the process starts normally without a server.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInstanceGuard
{
public:
    PDFAppInstanceGuard() = delete;

    /// A lock file older than this (or of a process, which does not run) is stale
    static constexpr int STALE_LOCK_TIME = 30000;

    /// Time limit of the probe of the name by resume (a busy name is held anyway)
    static constexpr int PROBE_TIMEOUT = 100;

    enum class Result
    {
        Forwarded,  ///< The request was delivered, the process quits
        Listening,  ///< The process is the running instance now (the server listens)
        Standalone  ///< The process starts normally without the server
    };

    enum class Mode
    {
        Forward,        ///< Files are forwarded to the instance holding the name, else the process listens
        ListenIfFree    ///< Nothing is forwarded (a start without files, --new-window), the process listens, if the name is free
    };

    /// Name of the endpoint of the user and of the session (Windows: the session id,
    /// Unix: $WAYLAND_DISPLAY and $DISPLAY), so files are never opened on another
    /// screen. Windows: a named pipe accessible only by the user. Unix: the full path
    /// of the socket in the runtime directory of the user (never the shared /tmp, where
    /// another user could take the name). Empty, when there is no safe runtime
    /// directory (no forwarding then). PDF4QT_APP_INSTANCE_NAME overrides it (tests).
    static QString getServerName();

    /// Directory of the lock file (and on Unix of the socket), accessible only by the
    /// user: Windows %TEMP% of the user, Unix the runtime directory ($XDG_RUNTIME_DIR,
    /// Qt falls back to a private directory of the user), owned by the user and not
    /// writable by others. Empty, when there is none.
    static QString getRuntimeDirectory();

    /// Path of the lock file of the decision (in the runtime directory), empty when
    /// there is no runtime directory
    static QString getLockFilePath(const QString& serverName);

    /// Forwards the request to a running instance, or starts \p server listening
    /// (Mode::Forward with an empty list of files is Mode::ListenIfFree)
    static Result start(const QString& serverName, const PDFAppInstanceRequest& request,
                        PDFAppInstanceServer* server, int timeout, QString* error, Mode mode = Mode::Forward);

    /// PDFAppInstanceServer::resume: under the lock, the server listens again under
    /// its name, unless another instance answers on it. \p timeout is the wait for
    /// the lock (0 = a single attempt, which never waits - the GUI thread), the probe
    /// of the name waits at most PROBE_TIMEOUT. \p isLockBusy is set, when the lock is
    /// held by another process (the caller may try again later).
    static bool resume(PDFAppInstanceServer* server, int timeout, QString* error, bool* isLockBusy = nullptr);
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppInstanceRequest)

#endif // PDFAPPINSTANCE_H
