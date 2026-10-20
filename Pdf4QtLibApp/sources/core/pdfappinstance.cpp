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

#include "pdfappinstance.h"
#include "pdfappcommandline.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QWidget>
#include <QWindow>
#include <QtEndian>

#include <vector>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <lmcons.h>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcPdf4QtInstance, "pdf4qt.instance")

namespace pdf
{

/// Magic of the frames of the protocol
static constexpr char PDF4QT_INSTANCE_MAGIC[4] = { 'P', '4', 'Q', 'I' };

/// Helper functions of the forwarding
class PDFAppInstanceHelper
{
public:
    PDFAppInstanceHelper() = delete;

    /// Checks, that the server of the connected \p socket is a process of the current
    /// user, and returns its process id (0, when the platform does not tell it)
    static bool checkServer(QLocalSocket& socket, qint64* processId, QString* error);

#if defined(Q_OS_WIN)
    /// Security identifier of the user of the token of the process (empty on an error)
    static QByteArray getProcessUser(HANDLE process);
#endif

    /// Probes the name: true, if a server accepts a connection on it
    static bool isNameHeld(const QString& serverName, int timeout);
};

bool PDFAppInstanceHelper::checkServer(QLocalSocket& socket, qint64* processId, QString* error)
{
    *processId = 0;

#if defined(Q_OS_WIN)
    HANDLE pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    ULONG serverProcessId = 0;
    if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE || !GetNamedPipeServerProcessId(pipe, &serverProcessId))
    {
        *error = QStringLiteral("The process of the running instance cannot be determined.");
        return false;
    }

    // A pipe of the same name may be created by another user first (the namespace
    // of the pipes is global): the files are sent only to an own process
    HANDLE serverProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverProcessId);
    const QByteArray serverUser = serverProcess ? getProcessUser(serverProcess) : QByteArray();
    if (serverProcess)
    {
        CloseHandle(serverProcess);
    }

    const QByteArray currentUser = getProcessUser(GetCurrentProcess());
    if (serverUser.isEmpty() || currentUser.isEmpty() || serverUser != currentUser)
    {
        *error = QStringLiteral("The endpoint of the running instance belongs to another user.");
        return false;
    }

    *processId = qint64(serverProcessId);

    // Windows lets only the foreground process give the foreground away: without
    // this the running instance could only flash its button on the taskbar
    AllowSetForegroundWindow(serverProcessId);
    return true;
#elif defined(Q_OS_LINUX)
    struct ucred credentials = { };
    socklen_t length = sizeof(credentials);
    if (getsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 || length != sizeof(credentials))
    {
        *error = QStringLiteral("The process of the running instance cannot be determined.");
        return false;
    }

    if (credentials.uid != getuid())
    {
        *error = QStringLiteral("The endpoint of the running instance belongs to another user.");
        return false;
    }

    *processId = qint64(credentials.pid);
    return true;
#elif defined(Q_OS_UNIX)
    uid_t user = 0;
    gid_t group = 0;
    if (getpeereid(int(socket.socketDescriptor()), &user, &group) != 0 || user != getuid())
    {
        *error = QStringLiteral("The endpoint of the running instance belongs to another user.");
        return false;
    }
    return true;
#else
    Q_UNUSED(socket);
    Q_UNUSED(error);
    return true;
#endif
}

#if defined(Q_OS_WIN)
QByteArray PDFAppInstanceHelper::getProcessUser(HANDLE process)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
    {
        return QByteArray();
    }

    QByteArray result;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size > 0 && size < 64 * 1024)
    {
        std::vector<char> buffer(size);
        if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
        {
            const TOKEN_USER* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            if (IsValidSid(user->User.Sid))
            {
                result = QByteArray(reinterpret_cast<const char*>(user->User.Sid), int(GetLengthSid(user->User.Sid)));
            }
        }
    }

    CloseHandle(token);
    return result;
}
#endif

bool PDFAppInstanceHelper::isNameHeld(const QString& serverName, int timeout)
{
    // Only a connection, no message: the other server closes it silently. Only "no
    // server" frees the name; any other failure (a busy pipe of a live server, a
    // timeout) keeps it held, so a second server never listens under the name.
    QLocalSocket socket;
    socket.connectToServer(serverName);
    const bool isConnected = socket.waitForConnected(qMax(1, timeout));
    const QLocalSocket::LocalSocketError socketError = socket.error();
    socket.abort();
    return isConnected || (socketError != QLocalSocket::ServerNotFoundError && socketError != QLocalSocket::ConnectionRefusedError);
}

PDFAppInstanceRequest PDFAppInstanceProtocol::createRequest(const PDFAppCommandLine& commandLine, const QString& activationToken)
{
    PDFAppInstanceRequest request;
    request.files = commandLine.getFiles().mid(0, MAXIMUM_FILES);
    request.page = commandLine.getPage();
    request.activationToken = isValidToken(activationToken) ? activationToken : QString();

    for (const PDFAppCommandLineError& error : commandLine.getErrors())
    {
        // A missing file is reported by the window, which opens it
        if (error.kind == PDFAppCommandLineError::Kind::FileNotFound || request.errors.size() >= MAXIMUM_ERRORS)
        {
            continue;
        }

        QString message = error.getMessage().left(MAXIMUM_ERROR_LENGTH);
        message.remove(QChar(0));
        request.errors << message;
    }

    return request;
}

bool PDFAppInstanceProtocol::isValidToken(const QString& token)
{
    if (token.size() > MAXIMUM_TOKEN_LENGTH)
    {
        return false;
    }

    for (const QChar character : token)
    {
        if (character.unicode() < 0x21 || character.unicode() > 0x7E)
        {
            return false;
        }
    }
    return true;
}

bool PDFAppInstanceProtocol::isAcceptedPath(const QString& path)
{
    if (path.isEmpty() || path.size() > MAXIMUM_PATH_LENGTH || path.contains(QChar(0)) || !QDir::isAbsolutePath(path))
    {
        return false;
    }

    return !isDevicePath(path);
}

bool PDFAppInstanceProtocol::isDevicePath(const QString& path)
{
    // Device namespaces of Windows (\\.\PhysicalDrive0, \\.\pipe\..., \\?\GLOBALROOT\...)
    // are not documents; UNC paths of network shares are
    const QString normalizedPath = path.size() >= 4 ? QString(path.left(4)).replace(QLatin1Char('\\'), QLatin1Char('/')) : QString();
    return normalizedPath == QLatin1String("//./") || normalizedPath == QLatin1String("//?/");
}

QByteArray PDFAppInstanceProtocol::createFrame(const QByteArray& payload)
{
    QByteArray frame;
    frame.reserve(HEADER_SIZE + payload.size());
    frame.append(PDF4QT_INSTANCE_MAGIC, sizeof(PDF4QT_INSTANCE_MAGIC));

    char size[4];
    qToBigEndian<quint32>(quint32(payload.size()), size);
    frame.append(size, sizeof(size));
    frame.append(payload);
    return frame;
}

PDFAppInstanceProtocol::FrameResult PDFAppInstanceProtocol::readFrame(QByteArray& buffer, qsizetype maximumSize, QByteArray* payload)
{
    if (buffer.size() < HEADER_SIZE)
    {
        // The magic is checked as soon as possible
        const qsizetype size = qMin(buffer.size(), qsizetype(sizeof(PDF4QT_INSTANCE_MAGIC)));
        if (memcmp(buffer.constData(), PDF4QT_INSTANCE_MAGIC, size) != 0)
        {
            return FrameResult::Invalid;
        }
        return FrameResult::Incomplete;
    }

    if (memcmp(buffer.constData(), PDF4QT_INSTANCE_MAGIC, sizeof(PDF4QT_INSTANCE_MAGIC)) != 0)
    {
        return FrameResult::Invalid;
    }

    const quint32 size = qFromBigEndian<quint32>(buffer.constData() + sizeof(PDF4QT_INSTANCE_MAGIC));
    if (qsizetype(size) > maximumSize)
    {
        return FrameResult::Invalid;
    }

    if (buffer.size() < HEADER_SIZE + qsizetype(size))
    {
        return FrameResult::Incomplete;
    }

    *payload = buffer.mid(HEADER_SIZE, size);
    buffer.remove(0, HEADER_SIZE + size);
    return FrameResult::Complete;
}

QByteArray PDFAppInstanceProtocol::encodeRequest(const PDFAppInstanceRequest& request)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), VERSION);
    object.insert(QStringLiteral("files"), QJsonArray::fromStringList(request.files));
    object.insert(QStringLiteral("page"), request.page);
    if (!request.activationToken.isEmpty())
    {
        object.insert(QStringLiteral("activationToken"), request.activationToken);
    }
    if (!request.errors.isEmpty())
    {
        object.insert(QStringLiteral("errors"), QJsonArray::fromStringList(request.errors));
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool PDFAppInstanceProtocol::decodeRequest(const QByteArray& payload, PDFAppInstanceRequest* request, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *error = QStringLiteral("The message is not a JSON object.");
        return false;
    }

    const QJsonObject object = document.object();
    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != double(VERSION))
    {
        *error = QStringLiteral("Unsupported version of the message.");
        return false;
    }

    const QJsonValue files = object.value(QStringLiteral("files"));
    if (!files.isArray())
    {
        *error = QStringLiteral("The list of the files is missing.");
        return false;
    }

    const QJsonArray fileArray = files.toArray();
    if (fileArray.size() > MAXIMUM_FILES)
    {
        *error = QStringLiteral("Too many files.");
        return false;
    }

    PDFAppInstanceRequest result;
    for (const QJsonValue& value : fileArray)
    {
        if (!value.isString())
        {
            *error = QStringLiteral("A file is not a string.");
            return false;
        }

        const QString path = value.toString();
        if (!isAcceptedPath(path))
        {
            *error = QStringLiteral("A file is not an absolute path.");
            return false;
        }

        result.files << QDir::cleanPath(path);
    }

    const QJsonValue token = object.value(QStringLiteral("activationToken"));
    if (!token.isUndefined())
    {
        if (!token.isString() || !isValidToken(token.toString()))
        {
            *error = QStringLiteral("Invalid activation token.");
            return false;
        }
        result.activationToken = token.toString();
    }

    const QJsonValue errors = object.value(QStringLiteral("errors"));
    if (!errors.isUndefined())
    {
        const QJsonArray errorArray = errors.toArray();
        if (!errors.isArray() || errorArray.size() > MAXIMUM_ERRORS)
        {
            *error = QStringLiteral("Invalid errors of the command line.");
            return false;
        }

        for (const QJsonValue& value : errorArray)
        {
            const QString text = value.toString();
            if (!value.isString() || text.size() > MAXIMUM_ERROR_LENGTH || text.contains(QChar(0)))
            {
                *error = QStringLiteral("Invalid errors of the command line.");
                return false;
            }
            result.errors << text;
        }
    }

    const QJsonValue page = object.value(QStringLiteral("page"));
    if (!page.isUndefined())
    {
        const double pageNumber = page.toDouble(-1.0);
        if (!page.isDouble() || pageNumber < 0.0 || pageNumber > 10000000.0 || pageNumber != double(int(pageNumber)))
        {
            *error = QStringLiteral("Invalid page.");
            return false;
        }
        result.page = int(pageNumber);
    }

    *request = std::move(result);
    error->clear();
    return true;
}

QByteArray PDFAppInstanceProtocol::encodeReply(bool ok, const QString& error)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), VERSION);
    object.insert(QStringLiteral("ok"), ok);
    if (!ok)
    {
        object.insert(QStringLiteral("error"), error.left(512));
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool PDFAppInstanceProtocol::decodeReply(const QByteArray& payload, bool* ok, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    const QJsonObject object = document.object();
    const QJsonValue okValue = object.value(QStringLiteral("ok"));
    if (!okValue.isBool())
    {
        return false;
    }

    *ok = okValue.toBool();
    *error = object.value(QStringLiteral("error")).toString().left(512);
    return true;
}

PDFAppInstanceServer::PDFAppInstanceServer(QObject* parent) :
    QObject(parent)
{
    qRegisterMetaType<pdf::PDFAppInstanceRequest>();
}

PDFAppInstanceServer::~PDFAppInstanceServer()
{
    close();
}

bool PDFAppInstanceServer::listen(const QString& serverName)
{
    // Accepted connections of a previous listen are still answered (resume)
    closeServer();
    m_serverName = serverName;
    m_isSuspended = false;
    m_isHolding = false;
    m_isQuitting = false;

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->setMaxPendingConnections(MAXIMUM_CONNECTIONS);
    connect(m_server, &QLocalServer::newConnection, this, &PDFAppInstanceServer::onNewConnection);

    if (!m_server->listen(serverName))
    {
        qCWarning(lcPdf4QtInstance) << "Instance server cannot listen:" << m_server->errorString();
        return false;
    }

    return true;
}

bool PDFAppInstanceServer::listenRemovingStale(const QString& serverName)
{
    if (listen(serverName))
    {
        return true;
    }

    if (m_server && m_server->serverError() == QAbstractSocket::AddressInUseError)
    {
        // An endpoint of a crashed instance (Unix socket file). Nobody answers on it,
        // the caller has checked it under the lock.
        QLocalServer::removeServer(serverName);
        return listen(serverName);
    }

    return false;
}

void PDFAppInstanceServer::close()
{
    // No retry of the listening is scheduled by the deleted connections
    m_isSuspended = false;
    m_isHolding = false;
    const QList<QObject*> connections = m_connections;
    m_connections.clear();
    qDeleteAll(connections);

    closeServer();
    m_isSuspended = false;
    m_isHolding = false;
}

void PDFAppInstanceServer::closeServer()
{
    if (m_server)
    {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

void PDFAppInstanceServer::suspend()
{
    if (!isListening())
    {
        return;
    }

    // Requests are held from now on (resume or quit answers them). Connections
    // waiting in the backlog of the system are accepted first (the signal
    // newConnection calls onNewConnection), so they are answered.
    m_isHolding = true;
    for (int i = 0; i <= MAXIMUM_CONNECTIONS && m_server && m_server->isListening(); ++i)
    {
        bool isTimedOut = false;
        m_server->waitForNewConnection(0, &isTimedOut);
        onNewConnection();
        if (isTimedOut)
        {
            break;
        }
    }
    closeServer();
    m_isSuspended = true;
}

bool PDFAppInstanceServer::resume(int timeout, QString* error)
{
    if (isListening())
    {
        return true;
    }

    if (!m_isSuspended)
    {
        // Only a suspended server resumes (not a closed one, nor one, which never listened)
        return false;
    }

    // The quitting is canceled: the window opens the held requests first (also when
    // the server cannot listen again), their connections close then; this server
    // may be deleted by a slot
    m_isHolding = false;
    QPointer<PDFAppInstanceServer> guard(this);
    deliverHeldRequests();
    if (!guard || m_isQuitting)
    {
        return false;
    }

    const bool isListening = PDFAppInstanceGuard::resume(this, timeout, error);
    m_isSuspended = !isListening;
    return isListening;
}

void PDFAppInstanceServer::retryResume()
{
    if (!m_isSuspended || m_isHolding || m_isQuitting || isListening() || !m_connections.isEmpty())
    {
        return;
    }

    QString error;
    if (PDFAppInstanceGuard::resume(this, RETRY_TIMEOUT, &error))
    {
        m_isSuspended = false;
    }
    else
    {
        qCInfo(lcPdf4QtInstance) << "The running instance does not listen again:" << error;
    }
}

void PDFAppInstanceServer::quit()
{
    m_isQuitting = true;
    m_isHolding = false;
    m_isSuspended = false;
    closeServer();

    // Every connection, which has not been answered, is refused (also the ones,
    // whose message has not arrived yet); a slot of requestRefused may delete this
    QPointer<PDFAppInstanceServer> guard(this);
    for (const QPointer<PDFAppInstanceConnection>& connection : getConnections())
    {
        if (!guard)
        {
            return;
        }

        if (connection && !connection->isFinished())
        {
            connection->refuse(QStringLiteral("The running instance is quitting."));
        }
    }
}

int PDFAppInstanceServer::getHeldRequestCount() const
{
    int count = 0;
    for (const QPointer<PDFAppInstanceConnection>& connection : getConnections())
    {
        if (connection && connection->isHeld())
        {
            ++count;
        }
    }
    return count;
}

QList<QPointer<PDFAppInstanceConnection>> PDFAppInstanceServer::getConnections() const
{
    QList<QPointer<PDFAppInstanceConnection>> connections;
    for (QObject* object : m_connections)
    {
        if (PDFAppInstanceConnection* connection = qobject_cast<PDFAppInstanceConnection*>(object))
        {
            connections.append(connection);
        }
    }
    return connections;
}

void PDFAppInstanceServer::deliver(PDFAppInstanceConnection* connection)
{
    if (!connection || !connection->isHeld())
    {
        return;
    }

    // The request is copied: the connection, which owns it, may be deleted by a slot
    const PDFAppInstanceRequest request = connection->getRequest();
    connection->acknowledge();
    Q_EMIT openRequested(request);
}

void PDFAppInstanceServer::deliverHeldRequests()
{
    QPointer<PDFAppInstanceServer> guard(this);
    for (const QPointer<PDFAppInstanceConnection>& connection : getConnections())
    {
        if (!guard)
        {
            return;
        }

        if (connection && connection->isHeld())
        {
            deliver(connection);
        }
    }
}

bool PDFAppInstanceServer::isListening() const
{
    return m_server && m_server->isListening();
}

QString PDFAppInstanceServer::getServerName() const
{
    return m_server ? m_server->fullServerName() : QString();
}

QString PDFAppInstanceServer::getErrorString() const
{
    return m_server ? m_server->errorString() : QString();
}

void PDFAppInstanceServer::setReadTimeout(int timeout)
{
    m_readTimeout = qBound(1, timeout, 600000);
}

int PDFAppInstanceServer::getConnectionCount() const
{
    return int(m_connections.size());
}

void PDFAppInstanceServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (!socket)
        {
            continue;
        }

        if (m_connections.size() >= MAXIMUM_CONNECTIONS)
        {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        PDFAppInstanceConnection* connection = new PDFAppInstanceConnection(socket, m_readTimeout, this);
        m_connections.append(connection);
        connect(connection, &QObject::destroyed, this, &PDFAppInstanceServer::onConnectionDestroyed);
        connect(connection, &PDFAppInstanceConnection::requestReady, this, &PDFAppInstanceServer::onRequestReady);
        connect(connection, &PDFAppInstanceConnection::requestRefused, this, &PDFAppInstanceServer::onRequestRefused);
    }
}

void PDFAppInstanceServer::onConnectionDestroyed(QObject* object)
{
    // Only the pointer is forgotten (the connection is being destroyed)
    m_connections.removeAll(object);

    // The quitting was canceled, but an open connection held the name
    if (m_connections.isEmpty() && m_isSuspended && !m_isHolding && !m_isQuitting)
    {
        QMetaObject::invokeMethod(this, &PDFAppInstanceServer::retryResume, Qt::QueuedConnection);
    }
}

void PDFAppInstanceServer::onRequestReady()
{
    // The connections do not identify themselves in the signal
    PDFAppInstanceConnection* connection = qobject_cast<PDFAppInstanceConnection*>(sender());
    if (!connection || !m_connections.contains(connection))
    {
        return;
    }

    if (m_isQuitting)
    {
        // The window is closing: the other process starts by itself
        connection->refuse(QStringLiteral("The running instance is quitting."));
        return;
    }

    if (m_isHolding)
    {
        // Answered by resume or quit
        return;
    }

    deliver(connection);
}

void PDFAppInstanceServer::onRequestRefused(const QString& reason)
{
    const QString reasonCopy = reason;
    qCWarning(lcPdf4QtInstance) << "Request of another instance refused:" << reasonCopy;
    Q_EMIT requestRefused(reasonCopy);
}

PDFAppInstanceConnection::PDFAppInstanceConnection(QLocalSocket* socket, int readTimeout, QObject* parent) :
    QObject(parent),
    m_socket(socket),
    m_readTimeout(readTimeout)
{
    socket->setParent(this);
    socket->setReadBufferSize(PDFAppInstanceProtocol::HEADER_SIZE + PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE + 1);
    connect(socket, &QLocalSocket::readyRead, this, &PDFAppInstanceConnection::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, this, &PDFAppInstanceConnection::onDisconnected);

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PDFAppInstanceConnection::onTimeout);
    m_timer.start(readTimeout);

    // Data may have arrived before the connection was created
    if (socket->bytesAvailable() > 0)
    {
        QMetaObject::invokeMethod(this, &PDFAppInstanceConnection::onReadyRead, Qt::QueuedConnection);
    }
}

PDFAppInstanceConnection::~PDFAppInstanceConnection()
{
    m_timer.stop();
    if (m_socket)
    {
        m_socket->abort();
    }
}

void PDFAppInstanceConnection::onReadyRead()
{
    if (m_isFinished || m_isHeld || !m_socket)
    {
        if (m_socket)
        {
            m_socket->readAll();
        }
        return;
    }

    const qsizetype limit = PDFAppInstanceProtocol::HEADER_SIZE + PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE;
    m_buffer.append(m_socket->read(qMax(qsizetype(0), limit + 1 - m_buffer.size())));

    // The message is processed outside of the signal of the socket: a slot of the
    // application may delete the server (and this socket) synchronously
    if (!m_isProcessingScheduled)
    {
        m_isProcessingScheduled = true;
        QMetaObject::invokeMethod(this, &PDFAppInstanceConnection::processBuffer, Qt::QueuedConnection);
    }
}

void PDFAppInstanceConnection::processBuffer()
{
    m_isProcessingScheduled = false;
    if (m_isFinished || m_isHeld || !m_socket)
    {
        return;
    }

    const qsizetype limit = PDFAppInstanceProtocol::HEADER_SIZE + PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE;
    QByteArray payload;
    switch (PDFAppInstanceProtocol::readFrame(m_buffer, PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE, &payload))
    {
        case PDFAppInstanceProtocol::FrameResult::Incomplete:
            if (m_buffer.size() > limit)
            {
                refuse(QStringLiteral("The message is too long."));
            }
            return;

        case PDFAppInstanceProtocol::FrameResult::Invalid:
            refuse(QStringLiteral("Invalid frame of the message."));
            return;

        case PDFAppInstanceProtocol::FrameResult::Complete:
            break;
    }

    PDFAppInstanceRequest request;
    QString error;
    if (!PDFAppInstanceProtocol::decodeRequest(payload, &request, &error))
    {
        refuse(error);
        return;
    }

    // The server answers the request (at once, or later while the application is
    // quitting); a held request waits at most the read timeout
    m_request = std::move(request);
    m_isHeld = true;
    m_buffer.clear();
    m_timer.start(m_readTimeout);
    Q_EMIT requestReady();
}

void PDFAppInstanceConnection::acknowledge()
{
    if (m_isFinished || !m_isHeld)
    {
        return;
    }

    // Acknowledgment first, then the application opens the files
    m_isHeld = false;
    m_isFinished = true;
    m_timer.stop();
    if (m_socket)
    {
        m_socket->write(PDFAppInstanceProtocol::createFrame(PDFAppInstanceProtocol::encodeReply(true, QString())));
        m_socket->flush();
    }
    finish();
}

void PDFAppInstanceConnection::refuse(const QString& reason)
{
    if (m_isFinished)
    {
        return;
    }

    m_isHeld = false;
    m_isFinished = true;
    m_timer.stop();
    if (m_socket)
    {
        m_socket->write(PDFAppInstanceProtocol::createFrame(PDFAppInstanceProtocol::encodeReply(false, reason)));
        m_socket->flush();
    }

    QPointer<PDFAppInstanceConnection> guard(this);
    Q_EMIT requestRefused(reason);
    if (!guard)
    {
        return;
    }

    finish();
}

void PDFAppInstanceConnection::finish()
{
    // The reply is written before the socket closes; the connection is deleted,
    // when the socket is disconnected (or by the timer)
    if (m_socket && m_socket->state() != QLocalSocket::UnconnectedState)
    {
        m_socket->disconnectFromServer();
    }

    if (!m_socket || m_socket->state() == QLocalSocket::UnconnectedState)
    {
        deleteLater();
        return;
    }

    m_timer.start(PDFAppInstanceServer::DEFAULT_READ_TIMEOUT);
}

void PDFAppInstanceConnection::onTimeout()
{
    if (!m_isFinished)
    {
        refuse(m_isHeld ? QStringLiteral("The running instance is busy.") : QStringLiteral("Timeout of the message."));
        return;
    }

    // The peer has not read the reply in time
    if (m_socket)
    {
        m_socket->abort();
    }
    deleteLater();
}

void PDFAppInstanceConnection::onDisconnected()
{
    // A message read before is still processed (the queued call runs before the
    // deferred deletion); a held request is dropped (its peer has given up and
    // starts by itself)
    if (m_isHeld)
    {
        m_isHeld = false;
        m_isFinished = true;
    }
    m_timer.stop();
    deleteLater();
}

PDFAppInstanceClient::Result PDFAppInstanceClient::send(const QString& serverName, const PDFAppInstanceRequest& request, int timeout, QString* error,
                                                       qint64* serverProcessId)
{
    QString errorText;
    const QDeadlineTimer deadline(qMax(1, timeout));
    if (serverProcessId)
    {
        *serverProcessId = 0;
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(int(qMax<qint64>(1, deadline.remainingTime()))))
    {
        const QLocalSocket::LocalSocketError socketError = socket.error();
        if (error)
        {
            *error = socket.errorString();
        }

        if (socketError == QLocalSocket::ServerNotFoundError || socketError == QLocalSocket::ConnectionRefusedError)
        {
            return Result::NoServer;
        }
        return Result::Failed;
    }

    qint64 processId = 0;
    if (!PDFAppInstanceHelper::checkServer(socket, &processId, &errorText))
    {
        // Nothing is sent to a server of another user; the process starts by itself
        qCWarning(lcPdf4QtInstance) << "Instance server refused:" << errorText;
        if (error)
        {
            *error = errorText;
        }
        socket.abort();
        return Result::Failed;
    }

    if (serverProcessId)
    {
        *serverProcessId = processId;
    }

    socket.write(PDFAppInstanceProtocol::createFrame(PDFAppInstanceProtocol::encodeRequest(request)));
    while (socket.bytesToWrite() > 0)
    {
        if (deadline.hasExpired() || !socket.waitForBytesWritten(int(qMax<qint64>(1, deadline.remainingTime()))))
        {
            if (error)
            {
                *error = QStringLiteral("The request cannot be sent: %1").arg(socket.errorString());
            }
            socket.abort();
            return Result::Failed;
        }
    }

    QByteArray buffer;
    QByteArray payload;
    while (true)
    {
        const PDFAppInstanceProtocol::FrameResult frameResult = PDFAppInstanceProtocol::readFrame(buffer, PDFAppInstanceProtocol::MAXIMUM_REPLY_SIZE, &payload);
        if (frameResult == PDFAppInstanceProtocol::FrameResult::Complete)
        {
            break;
        }

        if (frameResult == PDFAppInstanceProtocol::FrameResult::Invalid)
        {
            if (error)
            {
                *error = QStringLiteral("Invalid reply of the running instance.");
            }
            socket.abort();
            return Result::Failed;
        }

        if (socket.bytesAvailable() > 0)
        {
            buffer.append(socket.read(PDFAppInstanceProtocol::HEADER_SIZE + PDFAppInstanceProtocol::MAXIMUM_REPLY_SIZE));
            continue;
        }

        if (deadline.hasExpired() || socket.state() != QLocalSocket::ConnectedState ||
            !socket.waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime()))))
        {
            if (socket.bytesAvailable() > 0)
            {
                continue;
            }

            if (error)
            {
                *error = QStringLiteral("The running instance has not answered in time.");
            }
            socket.abort();
            return Result::Failed;
        }
    }

    bool ok = false;
    if (!PDFAppInstanceProtocol::decodeReply(payload, &ok, &errorText) || !ok)
    {
        if (error)
        {
            *error = errorText.isEmpty() ? QStringLiteral("The running instance has refused the request.") : errorText;
        }
        socket.abort();
        return Result::Failed;
    }

    socket.disconnectFromServer();
    return Result::Delivered;
}


QString PDFAppActivation::readStartupToken()
{
    QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (token.isEmpty())
    {
        token = qEnvironmentVariable("DESKTOP_STARTUP_ID");
    }
    return PDFAppInstanceProtocol::isValidToken(token) ? token : QString();
}

void PDFAppActivation::activateWindow(QWidget* window, const QString& activationToken)
{
    if (!window)
    {
        return;
    }

    if (window->isMinimized())
    {
        // A maximized window stays maximized
        window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    }
    if (!window->isVisible())
    {
        window->show();
    }
    window->raise();

    const QString token = PDFAppInstanceProtocol::isValidToken(activationToken) ? activationToken : QString();
    if (!token.isEmpty() && QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
    {
        // The xdg-shell integration of Qt activates the window by this token and
        // clears the variable (QWaylandXdgSurface::requestActivate)
        qputenv("XDG_ACTIVATION_TOKEN", token.toLatin1());
    }
    window->activateWindow();

#if !defined(Q_OS_WIN)
    // Windows: the starting process has allowed the foreground (AllowSetForegroundWindow)
    if (token.isEmpty())
    {
        QApplication::alert(window);
    }
#endif
}

QString PDFAppInstanceGuard::getRuntimeDirectory()
{
#if defined(Q_OS_WIN)
    // %TEMP% of the user: the profile of the user is accessible only by the user
    return QDir::tempPath();
#else
    // Qt checks $XDG_RUNTIME_DIR and falls back to a private directory of the user;
    // the directory is checked again: owned by the user, not writable by others
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty())
    {
        return QString();
    }

    struct stat status = { };
    const QByteArray encodedDirectory = QFile::encodeName(directory);
    if (lstat(encodedDirectory.constData(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != getuid() ||
        (status.st_mode & (S_IWGRP | S_IWOTH)) != 0)
    {
        qCWarning(lcPdf4QtInstance) << "The runtime directory is not private, files are not forwarded:" << directory;
        return QString();
    }
    return directory;
#endif
}

QString PDFAppInstanceGuard::getServerName()
{
    const QString overriddenName = qEnvironmentVariable("PDF4QT_APP_INSTANCE_NAME");
    if (!overriddenName.isEmpty())
    {
        return overriddenName;
    }

    // One endpoint per user and session, so the files are never opened on another
    // screen (decision 11)
    QByteArray identity;
#if defined(Q_OS_WIN)
    wchar_t userName[UNLEN + 1] = { };
    DWORD userNameSize = UNLEN + 1;
    if (GetUserNameW(userName, &userNameSize))
    {
        identity = QString::fromWCharArray(userName).toUtf8();
    }
    DWORD sessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
    {
        identity += "/session-" + QByteArray::number(quint64(sessionId));
    }
#else
    identity = "uid-" + QByteArray::number(quint64(getuid())) + "/wayland-" + qgetenv("WAYLAND_DISPLAY") + "/x11-" + qgetenv("DISPLAY");
#endif

    const QByteArray hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16);
    const QString name = QStringLiteral("pdf4qt-%1").arg(QString::fromLatin1(hash));

#if defined(Q_OS_WIN)
    // Named pipe of the name with the access of the user only (UserAccessOption)
    return name;
#else
    // A bare name would be a socket in the shared /tmp, where another user could
    // create it first: the full path in the private runtime directory is used
    const QString directory = getRuntimeDirectory();
    return directory.isEmpty() ? QString() : QDir(directory).filePath(name);
#endif
}

QString PDFAppInstanceGuard::getLockFilePath(const QString& serverName)
{
    const QString directory = getRuntimeDirectory();
    if (directory.isEmpty() || serverName.isEmpty())
    {
        return QString();
    }

    QString fileName = QDir::isAbsolutePath(serverName) ? QFileInfo(serverName).fileName() : serverName;
    for (QChar& character : fileName)
    {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('_'))
        {
            character = QLatin1Char('_');
        }
    }
    return QDir(directory).filePath(fileName + QStringLiteral(".lock"));
}

PDFAppInstanceGuard::Result PDFAppInstanceGuard::start(const QString& serverName, const PDFAppInstanceRequest& request,
                                                       PDFAppInstanceServer* server, int timeout, QString* error, Mode mode)
{
    const QString lockFilePath = getLockFilePath(serverName);
    if (serverName.isEmpty() || !server || lockFilePath.isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral("There is no private endpoint of the user.");
        }
        return Result::Standalone;
    }

    // Two processes started at the same time decide one after the other
    QLockFile lockFile(lockFilePath);
    lockFile.setStaleLockTime(30000);
    if (!lockFile.tryLock(qMax(1, timeout)))
    {
        if (error)
        {
            *error = QStringLiteral("The lock of the instances cannot be acquired.");
        }
        return Result::Standalone;
    }

    if (mode == Mode::ListenIfFree || request.files.isEmpty())
    {
        // A start without files (a new window) and --new-window forward nothing; the
        // name is taken only, when no instance holds it
        if (PDFAppInstanceHelper::isNameHeld(serverName, qMax(1, timeout)))
        {
            if (error)
            {
                *error = QStringLiteral("Another instance receives the forwarded files.");
            }
            return Result::Standalone;
        }
    }
    else
    {
        // Under the lock only one process connects at a time, so a refused connection
        // is a stale endpoint, never a full backlog of a live server
        QString sendError;
        const PDFAppInstanceClient::Result sendResult = PDFAppInstanceClient::send(serverName, request, timeout, &sendError);

        switch (sendResult)
        {
            case PDFAppInstanceClient::Result::Delivered:
                return Result::Forwarded;

            case PDFAppInstanceClient::Result::Failed:
                // A server exists, but does not answer: start normally, never a second server
                if (error)
                {
                    *error = sendError;
                }
                return Result::Standalone;

            case PDFAppInstanceClient::Result::NoServer:
                break;
        }
    }

    if (server->listenRemovingStale(serverName))
    {
        return Result::Listening;
    }

    if (error)
    {
        *error = server->getErrorString();
    }
    return Result::Standalone;
}

bool PDFAppInstanceGuard::resume(PDFAppInstanceServer* server, int timeout, QString* error)
{
    if (!server)
    {
        return false;
    }

    if (server->isListening())
    {
        return true;
    }

    const QString serverName = server->getRequestedServerName();
    const QString lockFilePath = getLockFilePath(serverName);
    if (lockFilePath.isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral("There is no private endpoint of the user.");
        }
        return false;
    }

    QLockFile lockFile(lockFilePath);
    lockFile.setStaleLockTime(30000);
    if (!lockFile.tryLock(qMax(1, timeout)))
    {
        if (error)
        {
            *error = QStringLiteral("The lock of the instances cannot be acquired.");
        }
        return false;
    }

    // A new instance may have taken the name while this one was quitting
    if (PDFAppInstanceHelper::isNameHeld(serverName, qMax(1, timeout)))
    {
        if (error)
        {
            *error = QStringLiteral("Another instance receives the forwarded files.");
        }
        return false;
    }

    if (server->listenRemovingStale(serverName))
    {
        return true;
    }

    if (error)
    {
        *error = server->getErrorString();
    }
    return false;
}

}   // namespace pdf
