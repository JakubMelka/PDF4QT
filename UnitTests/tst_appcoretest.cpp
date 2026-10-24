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

#include "applocaleguard.h"
#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfutils.h"
#include "pdfappdocumentcommands.h"
#include "pdfappinstance.h"
#include "pdfapplocale.h"
#include "pdfapplog.h"
#include "pdfapppaths.h"
#include "pdfappmainwindow.h"
#include "pdfsettingdefinitions.h"

#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <memory>
#include <thread>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

/// Helper functions of the test
class AppCoreTestHelper
{
public:
    AppCoreTestHelper() = delete;

    /// Multiplier of the time limits: PDF4QT_TEST_TIMEOUT_FACTOR, or 20 under valgrind
    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        if (ok && factor > 0)
        {
            return factor;
        }
        if (qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")))
        {
            return 20;
        }
        return 1;
    }

    /// Endpoint name unique for the test run
    static QString createServerName(const QString& suffix)
    {
        return QStringLiteral("pdf4qt-test-%1-%2-%3").arg(QCoreApplication::applicationPid()).arg(++s_counter).arg(suffix);
    }

    /// Runs the event loop until the thread has finished (std::thread or std::jthread;
    /// a std::jthread is joined also by a failed check, which returns early)
    template<typename Thread>
    static bool waitForThread(Thread& thread, std::atomic<bool>& isFinished, int timeout)
    {
        QDeadlineTimer deadline(timeout);
        while (!isFinished && !deadline.hasExpired())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        const bool result = isFinished;
        thread.join();
        return result;
    }

    /// Reads the reply of the server of this thread on \p socket (the event loop runs).
    /// \returns false, if no complete reply has come in time.
    static bool readReply(QLocalSocket& socket, int timeout, bool* ok, QString* error)
    {
        QByteArray buffer;
        QByteArray payload;
        QDeadlineTimer deadline(timeout);
        while (!deadline.hasExpired())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            buffer += socket.readAll();
            if (pdf::PDFAppInstanceProtocol::readFrame(buffer, pdf::PDFAppInstanceProtocol::MAXIMUM_REPLY_SIZE, &payload) == pdf::PDFAppInstanceProtocol::FrameResult::Complete)
            {
                return pdf::PDFAppInstanceProtocol::decodeReply(payload, ok, error);
            }
        }
        return false;
    }

    /// Sends raw bytes and returns the reply (blocking, in a thread)
    static QByteArray sendRaw(const QString& serverName, const QByteArray& data, int timeout)
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(timeout))
        {
            return "no connection";
        }
        socket.write(data);
        socket.waitForBytesWritten(timeout);

        QByteArray reply;
        QDeadlineTimer deadline(timeout);
        while (!deadline.hasExpired() && socket.state() == QLocalSocket::ConnectedState)
        {
            if (socket.waitForReadyRead(50))
            {
                reply += socket.readAll();
            }
        }
        reply += socket.readAll();
        return reply;
    }

    static QString getExecutablePath()
    {
#if defined(Q_OS_WIN)
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("PDF4QT.exe"));
#else
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("PDF4QT"));
#endif
    }

    static int s_counter;
};

int AppCoreTestHelper::s_counter = 0;

/// Modes of the test executable started as a child process
class AppCoreChild
{
public:
    AppCoreChild() = delete;

    static int run(QCoreApplication& application)
    {
        const QStringList arguments = application.arguments();
        const QString mode = arguments.value(1);
        const QString serverName = arguments.value(2);
        const int runTime = arguments.value(3).toInt();

        if (mode == QLatin1String("--core-lock"))
        {
            return runLocker(serverName);
        }

        pdf::PDFAppInstanceServer server;
        int received = 0;
        QObject::connect(&server, &pdf::PDFAppInstanceServer::openRequested, &application, [&received](const pdf::PDFAppInstanceRequest& request)
        {
            ++received;
            fprintf(stdout, "request %s page %d\n", qPrintable(request.files.join(QLatin1Char('|'))), request.page);
            fflush(stdout);
        });

        pdf::PDFAppInstanceRequest request;
#if defined(Q_OS_WIN)
        request.files << QStringLiteral("C:/child/%1.pdf").arg(arguments.value(4));
#else
        request.files << QStringLiteral("/child/%1.pdf").arg(arguments.value(4));
#endif
        request.page = 3;

        QString error;
        const pdf::PDFAppInstanceGuard::Result result = pdf::PDFAppInstanceGuard::start(serverName, request, &server, 10000, &error);
        switch (result)
        {
            case pdf::PDFAppInstanceGuard::Result::Forwarded:
                fputs("forwarded\n", stdout);
                fflush(stdout);
                return 10;

            case pdf::PDFAppInstanceGuard::Result::Standalone:
                fprintf(stdout, "standalone %s\n", qPrintable(error));
                fflush(stdout);
                return 30;

            case pdf::PDFAppInstanceGuard::Result::Listening:
                break;
        }

        fputs("listening\n", stdout);
        fflush(stdout);

        if (mode == QLatin1String("--core-server"))
        {
            // Runs until it is killed
            return application.exec();
        }

        QTimer::singleShot(runTime, &application, &QCoreApplication::quit);
        application.exec();
        fprintf(stdout, "received=%d\n", received);
        fflush(stdout);
        return 20;
    }

    /// Another process deciding under the lock of the instances: the commands of the
    /// standard input "lock" and "unlock" take and release the lock, the end of the
    /// input quits
    static int runLocker(const QString& serverName)
    {
        QLockFile lockFile(pdf::PDFAppInstanceGuard::getLockFilePath(serverName));
        lockFile.setStaleLockTime(pdf::PDFAppInstanceGuard::STALE_LOCK_TIME);
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly))
        {
            return 40;
        }

        while (true)
        {
            const QByteArray line = input.readLine();
            const QByteArray command = line.trimmed();
            if (command == "lock")
            {
                fputs(lockFile.tryLock(10000 * AppCoreTestHelper::getTimeoutFactor()) ? "lock-acquired\n" : "lock-failed\n", stdout);
            }
            else if (command == "unlock")
            {
                lockFile.unlock();
                fputs("lock-released\n", stdout);
            }
            else if (line.isEmpty())
            {
                // The end of the input
                return 0;
            }
            fflush(stdout);
        }
    }
};

class AppCoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void commandLine_data();
    void commandLine();
    void commandLineSpecialCases();
    void commandLineRandom();
    void commandRegistry();
    void shortcutConflicts();
    void protocol();
    void protocolRandom();
    void instanceInProcess();
    void instanceMalformedMessages_data();
    void instanceMalformedMessages();
    void instanceStalledMessage();
    void instanceTooManyConnections();
    void instanceClientTimeout();
    void instanceNoServer();
    void instanceDeletedFromSlot();
    void guardCrossProcess();
    void guardStaleEndpoint();
    void guardConcurrentStart();
    void guardWithoutFiles();
    void suspendAndResume();
    void quitRefusesRequests();
    void suspendResumeDetails();
    void instanceBusyPipe();
    void resumeWhileLocked();
    void logs();
    void logRotationAndRetention();
    void logSizeLimit();
    void logPortable();
    void contextColorScheme();
    void authorSettingApplied();
    void reviewAuthorNameSanitizing();
    void reviewShortcutScopes();
    void uiLocale();
    void executableSmoke();
    void executablePortable();

private:
    QProcess* startChild(const QStringList& arguments);
    static QByteArray waitForOutput(QProcess* process, const QByteArray& text, int timeout);

    /// Sends a command to a child process of the mode --core-lock and waits for its answer
    static bool sendLockerCommand(QProcess* process, const QByteArray& command, const QByteArray& answer, int timeout);

    QTemporaryDir m_directory;
    QString m_existingFile;
};

QProcess* AppCoreTest::startChild(const QStringList& arguments)
{
    QProcess* process = new QProcess(this);
    process->start(QCoreApplication::applicationFilePath(), arguments);
    return process;
}

QByteArray AppCoreTest::waitForOutput(QProcess* process, const QByteArray& text, int timeout)
{
    QByteArray output;
    QDeadlineTimer deadline(timeout);
    while (!output.contains(text) && !deadline.hasExpired())
    {
        process->waitForReadyRead(50);
        output += process->readAllStandardOutput();
        if (process->state() == QProcess::NotRunning)
        {
            output += process->readAllStandardOutput();
            break;
        }
    }
    return output;
}

bool AppCoreTest::sendLockerCommand(QProcess* process, const QByteArray& command, const QByteArray& answer, int timeout)
{
    process->write(command + "\n");
    if (!process->waitForBytesWritten(timeout))
    {
        return false;
    }
    return waitForOutput(process, answer, timeout).contains(answer);
}

void AppCoreTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
    m_existingFile = m_directory.filePath(QStringLiteral("existing.pdf"));
    QFile file(m_existingFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%PDF-1.7\n");
    QDir(m_directory.path()).mkpath(QStringLiteral("folder"));
    m_existingFile = QFileInfo(m_existingFile).canonicalFilePath();
}

void AppCoreTest::commandLine_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<int>("fileCount");
    QTest::addColumn<int>("page");
    QTest::addColumn<bool>("portable");
    QTest::addColumn<bool>("newWindow");
    QTest::addColumn<QList<int>>("errors");

    using Kind = pdf::PDFAppCommandLineError::Kind;
    QTest::newRow("empty") << QStringList() << 0 << 0 << false << false << QList<int>();
    QTest::newRow("file") << QStringList({ "existing.pdf" }) << 1 << 0 << false << false << QList<int>();
    QTest::newRow("duplicate") << QStringList({ "existing.pdf", "./existing.pdf" }) << 1 << 0 << false << false << QList<int>();
    QTest::newRow("page") << QStringList({ "existing.pdf", "--page", "12" }) << 1 << 12 << false << false << QList<int>();
    QTest::newRow("page=") << QStringList({ "--page=7", "existing.pdf" }) << 1 << 7 << false << false << QList<int>();
    QTest::newRow("page zero") << QStringList({ "--page", "0" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidPage) });
    QTest::newRow("page negative") << QStringList({ "--page", "-3" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidPage) });
    QTest::newRow("page text") << QStringList({ "--page=abc" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidPage) });
    QTest::newRow("page huge") << QStringList({ "--page", "99999999999999999999" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidPage) });
    QTest::newRow("page missing") << QStringList({ "--page" }) << 0 << 0 << false << false << QList<int>({ int(Kind::MissingValue) });
    QTest::newRow("portable") << QStringList({ "--portable" }) << 0 << 0 << true << false << QList<int>();
    QTest::newRow("new window") << QStringList({ "--new-window", "existing.pdf" }) << 1 << 0 << false << true << QList<int>();
    QTest::newRow("unknown") << QStringList({ "--unknown", "-x" }) << 0 << 0 << false << false << QList<int>({ int(Kind::UnknownArgument), int(Kind::UnknownArgument) });
    QTest::newRow("missing file") << QStringList({ "missing.pdf" }) << 1 << 0 << false << false << QList<int>({ int(Kind::FileNotFound) });
    QTest::newRow("folder") << QStringList({ "folder" }) << 0 << 0 << false << false << QList<int>({ int(Kind::NotAFile) });
    QTest::newRow("options end") << QStringList({ "--", "--portable" }) << 1 << 0 << false << false << QList<int>({ int(Kind::FileNotFound) });
    QTest::newRow("empty argument") << QStringList({ "" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidArgument) });
    QTest::newRow("bad url") << QStringList({ "file:" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidArgument) });
#if defined(Q_OS_WIN)
    QTest::newRow("device path") << QStringList({ "//./PhysicalDrive0" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidArgument) });
    QTest::newRow("device namespace") << QStringList({ "\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\a.pdf" }) << 0 << 0 << false << false << QList<int>({ int(Kind::InvalidArgument) });
#endif
}

void AppCoreTest::commandLine()
{
    QFETCH(QStringList, arguments);
    QFETCH(int, fileCount);
    QFETCH(int, page);
    QFETCH(bool, portable);
    QFETCH(bool, newWindow);
    QFETCH(QList<int>, errors);

    arguments.prepend(QStringLiteral("PDF4QT"));
    const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse(arguments, m_directory.path());
    QCOMPARE(commandLine.getFiles().size(), fileCount);
    QCOMPARE(commandLine.getPage(), page);
    QCOMPARE(commandLine.isPortable(), portable);
    QCOMPARE(commandLine.isNewWindow(), newWindow);
    QCOMPARE(commandLine.isForwardingAllowed(), !portable && !newWindow && fileCount > 0);
    QCOMPARE(commandLine.isListeningAllowed(), !portable);

    QList<int> kinds;
    for (const pdf::PDFAppCommandLineError& error : commandLine.getErrors())
    {
        kinds << int(error.kind);
        QVERIFY(!error.getMessage().isEmpty());
    }
    QCOMPARE(kinds, errors);
    QCOMPARE(commandLine.getErrorMessages().size(), errors.size());
    QCOMPARE(commandLine.hasErrors(), !errors.isEmpty());

    for (const QString& file : commandLine.getFiles())
    {
        QVERIFY(QDir::isAbsolutePath(file));
    }
}

void AppCoreTest::commandLineSpecialCases()
{
    // Existing files are canonical, URLs are local files
    const QString url = QUrl::fromLocalFile(m_existingFile).toString();
    pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", url, m_existingFile }, QStringLiteral("/"));
    QCOMPARE(commandLine.getFiles(), QStringList({ m_existingFile }));
    QVERIFY(!commandLine.hasErrors());

    // Too many files: one error, at most MAXIMUM_FILES files
    QStringList arguments = { "PDF4QT" };
    for (int i = 0; i < pdf::PDFAppCommandLine::MAXIMUM_FILES + 20; ++i)
    {
        arguments << QStringLiteral("missing%1.pdf").arg(i);
    }
    commandLine = pdf::PDFAppCommandLine::parse(arguments, m_directory.path());
    QCOMPARE(commandLine.getFiles().size(), pdf::PDFAppCommandLine::MAXIMUM_FILES);
    int tooMany = 0;
    for (const pdf::PDFAppCommandLineError& error : commandLine.getErrors())
    {
        tooMany += error.kind == pdf::PDFAppCommandLineError::Kind::TooManyFiles ? 1 : 0;
    }
    QCOMPARE(tooMany, 1);
    QVERIFY(commandLine.getErrors().size() <= size_t(pdf::PDFAppCommandLine::MAXIMUM_FILES) + 1);

    // Huge argument
    commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", QString(100000, QLatin1Char('a')) }, m_directory.path());
    QCOMPARE(commandLine.getFiles().size(), 0);
    QCOMPARE(commandLine.getErrors().size(), size_t(1));
    QVERIFY(commandLine.getErrors().front().getMessage().size() < 1000);

    // Only the program
    commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT" }, QString());
    QVERIFY(!commandLine.hasErrors());
    commandLine = pdf::PDFAppCommandLine::parse({}, QString());
    QVERIFY(!commandLine.hasErrors());
    QVERIFY(!pdf::PDFAppCommandLine::getUsage().isEmpty());
}

void AppCoreTest::commandLineRandom()
{
    // Random command lines never crash and every file is absolute
    QRandomGenerator random(20261019);
    const QStringList pieces = { "--page", "--page=", "--portable", "--new-window", "--", "-", "file:", "file:///", "x.pdf", "..", "/", "\\", "C:",
                                 "%00", "existing.pdf", "folder", "-1", "0", "1", "2147483648", "\u0161", "\n", " ", "*", "?" };
    for (int i = 0; i < 3000; ++i)
    {
        QStringList arguments = { "PDF4QT" };
        const int count = random.bounded(8);
        for (int j = 0; j < count; ++j)
        {
            QString argument;
            const int pieceCount = 1 + random.bounded(3);
            for (int k = 0; k < pieceCount; ++k)
            {
                argument += pieces[random.bounded(int(pieces.size()))];
            }
            if (random.bounded(10) == 0)
            {
                argument += QChar(char16_t(random.bounded(0xD800)));
            }
            arguments << argument;
        }

        const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse(arguments, m_directory.path());
        QVERIFY(commandLine.getPage() >= 0 && commandLine.getPage() <= pdf::PDFAppCommandLine::MAXIMUM_PAGE);
        for (const QString& file : commandLine.getFiles())
        {
            QVERIFY2(QDir::isAbsolutePath(file), qPrintable(file));
        }
        for (const pdf::PDFAppCommandLineError& error : commandLine.getErrors())
        {
            QVERIFY(!error.getMessage().isEmpty());
        }
    }
}

void AppCoreTest::commandRegistry()
{
    pdf::PDFAppCommandRegistry registry;
    pdf::PDFAppCommands::registerBuiltinCommands(&registry);

    const QStringList ids = { "window.newTab", "window.closeTab", "window.nextTab", "window.previousTab", "window.quit", "file.open",
                              "navigation.back", "navigation.forward", "navigation.search", "navigation.help" };
    // Step 3 adds the commands of the document tab (scope Document and Canvas)
    QCOMPARE(registry.getCommands().size(), size_t(ids.size() + pdf::PDFAppDocumentCommandTable::getAllCommands().size()));
    for (const QString& id : ids)
    {
        const pdf::PDFAppCommand* command = registry.findCommand(id);
        QVERIFY2(command, qPrintable(id));
        QVERIFY(!command->getTitle().isEmpty());
        QVERIFY(!command->getGroupTitle().isEmpty());
        QVERIFY2(pdf::PDFThemeIcons::hasIcon(command->iconName), qPrintable(command->iconName));
        QVERIFY(!command->defaultShortcut.isEmpty());
    }

    QCOMPARE(registry.findCommand("window.newTab")->defaultShortcut, QKeySequence(Qt::CTRL | Qt::Key_T));
    QCOMPARE(registry.findCommand("window.closeTab")->defaultShortcut, QKeySequence(Qt::CTRL | Qt::Key_W));
    QCOMPARE(registry.findCommand("navigation.back")->defaultShortcut, QKeySequence(Qt::ALT | Qt::Key_Left));
    QCOMPARE(registry.findCommand("navigation.forward")->defaultShortcut, QKeySequence(Qt::ALT | Qt::Key_Right));
    QCOMPARE(registry.findCommand("navigation.search")->defaultShortcut, QKeySequence(Qt::CTRL | Qt::Key_K));
    QCOMPARE(registry.findCommand("file.open")->defaultShortcut, QKeySequence(Qt::CTRL | Qt::Key_O));
    QCOMPARE(registry.findCommand("file.open")->iconName, QStringLiteral("folder-open"));
    QVERIFY(!registry.findCommand("no.command"));

    // Icons of the zoom commands of Figma 1188:1537 - 1188:1556 (menus, search, shortcuts;
    // the buttons beside the zoom field of the toolbar keep plus / minus)
    QCOMPARE(registry.findCommand(pdf::PDFAppCommands::zoomOut())->iconName, QStringLiteral("zoom-out"));
    QCOMPARE(registry.findCommand(pdf::PDFAppCommands::actualSize())->iconName, QStringLiteral("actual-size"));
    QCOMPARE(registry.findCommand(pdf::PDFAppCommands::magnifier())->iconName, QStringLiteral("magnifier"));
    for (const pdf::PDFAppCommand* command : registry.getCommands())
    {
        QVERIFY2(command->iconName.isEmpty() || pdf::PDFThemeIcons::hasIcon(command->iconName), qPrintable(command->id + QStringLiteral(" ") + command->iconName));
    }

    // The built-in shortcuts do not conflict
    QVERIFY(registry.findConflicts({}).empty());

    // Registration of invalid and duplicate commands
    pdf::PDFAppCommand command;
    command.id = QStringLiteral("window.newTab");
    QVERIFY(!registry.registerCommand(command));
    command.id = QStringLiteral("nogroup");
    QVERIFY(!registry.registerCommand(command));
    command.id = QStringLiteral("Bad.id");
    QVERIFY(!registry.registerCommand(command));
    command.id = QStringLiteral("test.command");
    QVERIFY(registry.registerCommand(command));
    QVERIFY(command.getTitle().isEmpty());

    // Effective shortcuts with the shortcuts of the user
    pdf::PDFAppCommandRegistry::UserShortcuts user;
    user["window.newTab"] = QStringLiteral("Ctrl+Shift+N");
    user["window.closeTab"] = QString();
    user["navigation.back"] = QStringLiteral("Ctrl+NoSuchKey+");
    user["unknown.command"] = QStringLiteral("Ctrl+U");
    QCOMPARE(registry.getShortcut("window.newTab", user), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    QVERIFY(registry.getShortcut("window.closeTab", user).isEmpty());
    QCOMPARE(registry.getShortcut("navigation.back", user), QKeySequence(Qt::ALT | Qt::Key_Left));
    QVERIFY(registry.getShortcut("unknown.command", user).isEmpty());

    // Stored text
    bool ok = false;
    QCOMPARE(pdf::PDFAppCommandRegistry::fromStoredText(pdf::PDFAppCommandRegistry::toStoredText(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F5)), &ok), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F5));
    QVERIFY(ok);
    QVERIFY(pdf::PDFAppCommandRegistry::fromStoredText(QString(), &ok).isEmpty());
    QVERIFY(ok);
    pdf::PDFAppCommandRegistry::fromStoredText(QStringLiteral("Garbage+++Key"), &ok);
    QVERIFY(!ok);

    QRandomGenerator random(5);
    for (int i = 0; i < 2000; ++i)
    {
        QString text;
        const int length = random.bounded(12);
        for (int j = 0; j < length; ++j)
        {
            text += QChar(char16_t(32 + random.bounded(95)));
        }
        const QKeySequence shortcut = pdf::PDFAppCommandRegistry::fromStoredText(text, &ok);
        QVERIFY(ok || shortcut.isEmpty());
    }
}

void AppCoreTest::shortcutConflicts()
{
    pdf::PDFAppCommandRegistry registry;
    pdf::PDFAppCommands::registerBuiltinCommands(&registry);

    pdf::PDFAppCommandRegistry::UserShortcuts user;
    user["window.quit"] = QStringLiteral("Ctrl+T");
    std::vector<pdf::PDFAppShortcutConflict> conflicts = registry.findConflicts(user);
    QCOMPARE(conflicts.size(), size_t(1));
    QCOMPARE(conflicts[0].shortcut, QKeySequence(Qt::CTRL | Qt::Key_T));
    QCOMPARE(conflicts[0].commandIds, QStringList({ "window.newTab", "window.quit" }));

    // Removing the shortcut of one of them resolves the conflict
    user["window.newTab"] = QString();
    QVERIFY(registry.findConflicts(user).empty());

    // Checking a candidate before it is assigned
    QCOMPARE(registry.getConflictingCommands("window.newTab", QKeySequence(Qt::CTRL | Qt::Key_K), user), QStringList({ "navigation.search" }));
    QVERIFY(registry.getConflictingCommands("navigation.search", QKeySequence(Qt::CTRL | Qt::Key_K), user).isEmpty());
    QVERIFY(registry.getConflictingCommands("window.newTab", QKeySequence(), user).isEmpty());

    // A shortcut, which begins with another one, conflicts too
    QVERIFY(pdf::PDFAppCommandRegistry::isConflict(QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_D)));
    QVERIFY(pdf::PDFAppCommandRegistry::isConflict(QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_D), QKeySequence(Qt::CTRL | Qt::Key_K)));
    QVERIFY(!pdf::PDFAppCommandRegistry::isConflict(QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::Key_D)));
    QVERIFY(!pdf::PDFAppCommandRegistry::isConflict(QKeySequence(), QKeySequence()));
    user["window.quit"] = QStringLiteral("Ctrl+K, Ctrl+Q");
    conflicts = registry.findConflicts(user);
    QCOMPARE(conflicts.size(), size_t(1));
    QCOMPARE(conflicts[0].commandIds, QStringList({ "window.quit", "navigation.search" }));

    // Three commands with one shortcut = one conflict (F12 has no command)
    user["window.quit"] = QStringLiteral("F12");
    user["window.closeTab"] = QStringLiteral("F12");
    user["navigation.back"] = QStringLiteral("F12");
    conflicts = registry.findConflicts(user);
    QCOMPARE(conflicts.size(), size_t(1));
    QCOMPARE(conflicts[0].commandIds.size(), 3);
}

void AppCoreTest::protocol()
{
    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    request.page = 5;

    QByteArray buffer = pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest(request));
    buffer += "trailing";
    QByteArray payload;
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(buffer, pdf::PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Complete);
    QCOMPARE(buffer, QByteArray("trailing"));

    pdf::PDFAppInstanceRequest decoded;
    QString error;
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeRequest(payload, &decoded, &error));
    QCOMPARE(decoded, request);

    // Frames: incomplete, wrong magic, oversized
    QByteArray partial = pdf::PDFAppInstanceProtocol::createFrame("12345").left(10);
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(partial, 100, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Incomplete);
    QByteArray wrongMagic = "XXXX";
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(wrongMagic, 100, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Invalid);
    QByteArray shortWrong = "P5";
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(shortWrong, 100, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Invalid);
    QByteArray oversized = pdf::PDFAppInstanceProtocol::createFrame(QByteArray(200, 'a'));
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(oversized, 100, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Invalid);
    QByteArray empty;
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(empty, 100, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Incomplete);

    // Requests: everything is checked
    const QByteArray invalidRequests[] =
    {
        "",
        "[]",
        "{}",
        "{\"version\": 2, \"files\": []}",
        "{\"version\": \"1\", \"files\": []}",
        "{\"version\": 1}",
        "{\"version\": 1, \"files\": \"/a.pdf\"}",
        "{\"version\": 1, \"files\": [1]}",
        "{\"version\": 1, \"files\": [\"relative.pdf\"]}",
        "{\"version\": 1, \"files\": [\"\"]}",
        "{\"version\": 1, \"files\": [], \"page\": -1}",
        "{\"version\": 1, \"files\": [], \"page\": 1.5}",
        "{\"version\": 1, \"files\": [], \"page\": \"2\"}",
        "{\"version\": 1, \"files\": [], \"page\": 1e30}",
    };
    for (const QByteArray& invalidRequest : invalidRequests)
    {
        QVERIFY2(!pdf::PDFAppInstanceProtocol::decodeRequest(invalidRequest, &decoded, &error), invalidRequest.constData());
        QVERIFY(!error.isEmpty());
    }

    pdf::PDFAppInstanceRequest tooMany;
    for (int i = 0; i < pdf::PDFAppInstanceProtocol::MAXIMUM_FILES + 1; ++i)
    {
        tooMany.files << m_existingFile;
    }
    QVERIFY(!pdf::PDFAppInstanceProtocol::decodeRequest(pdf::PDFAppInstanceProtocol::encodeRequest(tooMany), &decoded, &error));

    // Token and errors of the command line
    request.activationToken = QStringLiteral("startup-token_1.2:3");
    request.errors << QStringLiteral("Unknown argument --x.") << QStringLiteral("Second\nline");
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeRequest(pdf::PDFAppInstanceProtocol::encodeRequest(request), &decoded, &error));
    QCOMPARE(decoded, request);

    const QByteArray invalidExtensions[] =
    {
        "{\"version\": 1, \"files\": [], \"activationToken\": 5}",
        "{\"version\": 1, \"files\": [], \"activationToken\": \"with space\"}",
        "{\"version\": 1, \"files\": [], \"activationToken\": \"" + QByteArray(pdf::PDFAppInstanceProtocol::MAXIMUM_TOKEN_LENGTH + 1, 'a') + "\"}",
        "{\"version\": 1, \"files\": [], \"errors\": \"text\"}",
        "{\"version\": 1, \"files\": [], \"errors\": [1]}",
        "{\"version\": 1, \"files\": [], \"errors\": [\"" + QByteArray(pdf::PDFAppInstanceProtocol::MAXIMUM_ERROR_LENGTH + 1, 'e') + "\"]}",
        "{\"version\": 1, \"files\": [\"//./PhysicalDrive0\"]}",
        "{\"version\": 1, \"files\": [\"\\\\\\\\.\\\\pipe\\\\x\"]}",
        "{\"version\": 1, \"files\": [\"//?/GLOBALROOT/x.pdf\"]}",
    };
    for (const QByteArray& invalidRequest : invalidExtensions)
    {
        QVERIFY2(!pdf::PDFAppInstanceProtocol::decodeRequest(invalidRequest, &decoded, &error), invalidRequest.constData());
        QVERIFY(!error.isEmpty());
    }

    QStringList manyErrors;
    for (int i = 0; i <= pdf::PDFAppInstanceProtocol::MAXIMUM_ERRORS; ++i)
    {
        manyErrors << QStringLiteral("error %1").arg(i);
    }
    pdf::PDFAppInstanceRequest manyErrorsRequest;
    manyErrorsRequest.errors = manyErrors;
    QVERIFY(!pdf::PDFAppInstanceProtocol::decodeRequest(pdf::PDFAppInstanceProtocol::encodeRequest(manyErrorsRequest), &decoded, &error));
    QVERIFY(!pdf::PDFAppInstanceProtocol::isAcceptedPath(QStringLiteral("//./pipe/name")));
    QVERIFY(!pdf::PDFAppInstanceProtocol::isAcceptedPath(QStringLiteral("relative.pdf")));
    QVERIFY(pdf::PDFAppInstanceProtocol::isAcceptedPath(m_existingFile));
#if defined(Q_OS_WIN)
    QVERIFY(!pdf::PDFAppInstanceProtocol::isAcceptedPath(QStringLiteral("\\\\.\\PhysicalDrive0")));
    QVERIFY(pdf::PDFAppInstanceProtocol::isAcceptedPath(QStringLiteral("//server/share/a.pdf")));
#endif

    // The request of a command line: absolute files, the page, errors without the
    // missing files (the window opening them reports them), only a valid token
    const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", "existing.pdf", "missing.pdf", "--page", "3", "--bogus" }, m_directory.path());
    pdf::PDFAppInstanceRequest created = pdf::PDFAppInstanceProtocol::createRequest(commandLine, QStringLiteral("token"));
    QCOMPARE(created.files.size(), 2);
    QCOMPARE(created.files.front(), m_existingFile);
    QVERIFY(QDir::isAbsolutePath(created.files.back()));
    QCOMPARE(created.page, 3);
    QCOMPARE(created.activationToken, QStringLiteral("token"));
    pdf::PDFAppCommandLineError bogusError;
    bogusError.kind = pdf::PDFAppCommandLineError::Kind::UnknownArgument;
    bogusError.argument = QStringLiteral("--bogus");
    QCOMPARE(created.errors, QStringList({ bogusError.getMessage() }));
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeRequest(pdf::PDFAppInstanceProtocol::encodeRequest(created), &decoded, &error));
    created = pdf::PDFAppInstanceProtocol::createRequest(commandLine, QStringLiteral("bad token"));
    QVERIFY(created.activationToken.isEmpty());

    // Unknown keys are ignored (a newer client of the same version)
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeRequest("{\"version\": 1, \"files\": [], \"extra\": true}", &decoded, &error));
    QVERIFY(decoded.files.isEmpty());
    QCOMPARE(decoded.page, 0);

    // Replies
    bool ok = false;
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeReply(pdf::PDFAppInstanceProtocol::encodeReply(true, QString()), &ok, &error));
    QVERIFY(ok);
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeReply(pdf::PDFAppInstanceProtocol::encodeReply(false, QStringLiteral("reason")), &ok, &error));
    QVERIFY(!ok);
    QCOMPARE(error, QStringLiteral("reason"));
    QVERIFY(!pdf::PDFAppInstanceProtocol::decodeReply("{\"ok\": 1}", &ok, &error));
    QVERIFY(!pdf::PDFAppInstanceProtocol::decodeReply("garbage", &ok, &error));
}

void AppCoreTest::protocolRandom()
{
    QRandomGenerator random(77);
    const QByteArray valid = pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 2 }));
    for (int i = 0; i < 5000; ++i)
    {
        QByteArray data = valid;
        const int mutations = 1 + random.bounded(6);
        for (int j = 0; j < mutations; ++j)
        {
            const int position = random.bounded(int(data.size()));
            switch (random.bounded(3))
            {
                case 0: data[position] = char(random.bounded(256)); break;
                case 1: data.remove(position, 1 + random.bounded(4)); break;
                default: data.insert(position, char(random.bounded(256))); break;
            }
        }

        QByteArray payload;
        if (pdf::PDFAppInstanceProtocol::readFrame(data, pdf::PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE, &payload) == pdf::PDFAppInstanceProtocol::FrameResult::Complete)
        {
            pdf::PDFAppInstanceRequest request;
            QString error;
            if (pdf::PDFAppInstanceProtocol::decodeRequest(payload, &request, &error))
            {
                for (const QString& file : request.files)
                {
                    QVERIFY(QDir::isAbsolutePath(file));
                }
                QVERIFY(request.page >= 0);
            }
        }
    }
}

void AppCoreTest::instanceInProcess()
{
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("inprocess"));
    pdf::PDFAppInstanceServer server;
    QVERIFY(server.listen(serverName));
    QVERIFY(server.isListening());
    QVERIFY(!server.getServerName().isEmpty());
    QSignalSpy openSpy(&server, &pdf::PDFAppInstanceServer::openRequested);

    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile << QDir(m_directory.path()).filePath(QStringLiteral("missing.pdf"));
    request.page = 4;

    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Failed;
    qint64 serverProcessId = -1;
    std::thread thread([&]()
    {
        QString error;
        result = pdf::PDFAppInstanceClient::send(serverName, request, 10000, &error, &serverProcessId);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * AppCoreTestHelper::getTimeoutFactor()));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Delivered);
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy[0][0].value<pdf::PDFAppInstanceRequest>(), request);

    // The owner of the server was checked (Windows: the process of the pipe, Linux:
    // the credentials of the socket); on Windows it may take the foreground then
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    QCOMPARE(serverProcessId, QCoreApplication::applicationPid());
#endif

    // Empty request = raise the window. The first connection is closed first, so the
    // server has created its next instance of the pipe (on a loaded machine the event
    // loop of the server may be late, a busy pipe is waited for only PIPE_WAIT_LIMIT)
    QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 0, 10000 * AppCoreTestHelper::getTimeoutFactor());
    isFinished = false;
    QString raiseError;
    std::thread raiseThread([&]()
    {
        result = pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 10000, &raiseError);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(raiseThread, isFinished, 30000 * AppCoreTestHelper::getTimeoutFactor()));
    QVERIFY2(result == pdf::PDFAppInstanceClient::Result::Delivered, qPrintable(raiseError));
    QCOMPARE(openSpy.count(), 2);
    QVERIFY(openSpy[1][0].value<pdf::PDFAppInstanceRequest>().files.isEmpty());

    // The connections are closed after the reply
    QTRY_COMPARE(server.getConnectionCount(), 0);

    // A second server of the same name in the same process fails on Unix; the
    // guard serializes the decision on every platform (guardConcurrentStart)
    server.close();
    QVERIFY(!server.isListening());
}

void AppCoreTest::instanceMalformedMessages_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("garbage") << QByteArray("GET / HTTP/1.1\r\n\r\n");
    QTest::newRow("oversized header") << (QByteArray("P4QI") + QByteArray("\x7F\xFF\xFF\xFF", 4));
    QTest::newRow("oversized payload") << pdf::PDFAppInstanceProtocol::createFrame(QByteArray(pdf::PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE + 10, 'a'));
    QTest::newRow("not json") << pdf::PDFAppInstanceProtocol::createFrame("not json");
    QTest::newRow("wrong version") << pdf::PDFAppInstanceProtocol::createFrame("{\"version\": 99, \"files\": []}");
    QTest::newRow("relative path") << pdf::PDFAppInstanceProtocol::createFrame("{\"version\": 1, \"files\": [\"a.pdf\"]}");
}

void AppCoreTest::instanceMalformedMessages()
{
    QFETCH(QByteArray, data);

    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("malformed"));
    pdf::PDFAppInstanceServer server;
    QVERIFY(server.listen(serverName));
    QSignalSpy openSpy(&server, &pdf::PDFAppInstanceServer::openRequested);
    QSignalSpy refusedSpy(&server, &pdf::PDFAppInstanceServer::requestRefused);

    std::atomic<bool> isFinished = false;
    QByteArray reply;
    std::thread thread([&]()
    {
        reply = AppCoreTestHelper::sendRaw(serverName, data, 5000 * AppCoreTestHelper::getTimeoutFactor());
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 60000 * AppCoreTestHelper::getTimeoutFactor()));

    QCOMPARE(openSpy.count(), 0);
    QCOMPARE(refusedSpy.count(), 1);

    // The reply says the request was refused
    QByteArray payload;
    QCOMPARE(pdf::PDFAppInstanceProtocol::readFrame(reply, pdf::PDFAppInstanceProtocol::MAXIMUM_REPLY_SIZE, &payload), pdf::PDFAppInstanceProtocol::FrameResult::Complete);
    bool ok = true;
    QString error;
    QVERIFY(pdf::PDFAppInstanceProtocol::decodeReply(payload, &ok, &error));
    QVERIFY(!ok);
    QTRY_COMPARE(server.getConnectionCount(), 0);
}

void AppCoreTest::instanceStalledMessage()
{
    // A client, which sends half a message and waits, is closed after the timeout
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("stalled"));
    pdf::PDFAppInstanceServer server;
    server.setReadTimeout(300);
    QCOMPARE(server.getReadTimeout(), 300);
    QVERIFY(server.listen(serverName));
    QSignalSpy refusedSpy(&server, &pdf::PDFAppInstanceServer::requestRefused);

    std::atomic<bool> isFinished = false;
    std::thread thread([&]()
    {
        AppCoreTestHelper::sendRaw(serverName, pdf::PDFAppInstanceProtocol::createFrame("{\"version\": 1, \"files\": []}").left(12), 3000 * AppCoreTestHelper::getTimeoutFactor());
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 60000 * AppCoreTestHelper::getTimeoutFactor()));
    QCOMPARE(refusedSpy.count(), 1);
    QTRY_COMPARE(server.getConnectionCount(), 0);
}

void AppCoreTest::instanceTooManyConnections()
{
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("many"));
    pdf::PDFAppInstanceServer server;
    server.setReadTimeout(2000 * AppCoreTestHelper::getTimeoutFactor());
    QVERIFY(server.listen(serverName));

    std::vector<std::unique_ptr<QLocalSocket>> sockets;
    for (int i = 0; i < pdf::PDFAppInstanceServer::MAXIMUM_CONNECTIONS + 8; ++i)
    {
        sockets.push_back(std::make_unique<QLocalSocket>());
        sockets.back()->connectToServer(serverName);
        QCoreApplication::processEvents();
    }
    QTRY_VERIFY(server.getConnectionCount() > 0);
    for (int i = 0; i < 20; ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QVERIFY(server.getConnectionCount() <= pdf::PDFAppInstanceServer::MAXIMUM_CONNECTIONS);

    // Stalled connections time out, the server keeps working
    QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 0, 20000 * AppCoreTestHelper::getTimeoutFactor());
    sockets.clear();
    QVERIFY(server.isListening());
}

void AppCoreTest::instanceClientTimeout()
{
    // A server, which accepts but never answers
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("silent"));
    QLocalServer silentServer;
    QVERIFY(silentServer.listen(serverName));

    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Delivered;
    qint64 elapsed = 0;
    std::thread thread([&]()
    {
        QElapsedTimer timer;
        timer.start();
        QString error;
        result = pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 500, &error);
        elapsed = timer.elapsed();
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * AppCoreTestHelper::getTimeoutFactor()));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Failed);
    QVERIFY(elapsed >= 400);
    QVERIFY(elapsed < 10000 * AppCoreTestHelper::getTimeoutFactor());

    // The guard does not become a second server, when a server does not answer
    pdf::PDFAppInstanceServer server;
    isFinished = false;
    pdf::PDFAppInstanceGuard::Result guardResult = pdf::PDFAppInstanceGuard::Result::Listening;
    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    std::thread guardThread([&]()
    {
        guardResult = pdf::PDFAppInstanceGuard::start(serverName, request, &server, 500, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(guardThread, isFinished, 30000 * AppCoreTestHelper::getTimeoutFactor()));
    QCOMPARE(guardResult, pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!server.isListening());
}

void AppCoreTest::instanceNoServer()
{
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("none"));
    QString error;
    QCOMPARE(pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 1000, &error), pdf::PDFAppInstanceClient::Result::NoServer);

    // Invalid arguments of the guard
    pdf::PDFAppInstanceServer server;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(QString(), pdf::PDFAppInstanceRequest(), &server, 1000, &error), pdf::PDFAppInstanceGuard::Result::Standalone);
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), nullptr, 1000, &error), pdf::PDFAppInstanceGuard::Result::Standalone);

    // Default name: per user and session, stable, overridable
    qunsetenv("PDF4QT_APP_INSTANCE_NAME");
    const QString defaultName = pdf::PDFAppInstanceGuard::getServerName();
    const QString runtimeDirectory = pdf::PDFAppInstanceGuard::getRuntimeDirectory();
    QVERIFY(!runtimeDirectory.isEmpty());
    QVERIFY(QFileInfo(defaultName).fileName().startsWith(QStringLiteral("pdf4qt-")));
    QCOMPARE(QFileInfo(defaultName).fileName().size(), 23);
    QCOMPARE(pdf::PDFAppInstanceGuard::getServerName(), defaultName);
#if defined(Q_OS_WIN)
    QCOMPARE(defaultName.size(), 23);
#else
    // The socket is in the private runtime directory of the user, never in the shared
    // /tmp; the name depends on the screen of the session
    QVERIFY(QDir::isAbsolutePath(defaultName));
    QCOMPARE(QFileInfo(defaultName).absolutePath(), QDir::cleanPath(runtimeDirectory));
    QVERIFY(!(QFile::permissions(runtimeDirectory) & (QFileDevice::WriteGroup | QFileDevice::WriteOther)));
    QCOMPARE(QFileInfo(runtimeDirectory).ownerId(), uint(getuid()));
    const QByteArray display = qgetenv("DISPLAY");
    qputenv("DISPLAY", ":71");
    const QString firstDisplayName = pdf::PDFAppInstanceGuard::getServerName();
    qputenv("DISPLAY", ":72");
    QVERIFY(pdf::PDFAppInstanceGuard::getServerName() != firstDisplayName);
    if (display.isNull())
    {
        qunsetenv("DISPLAY");
    }
    else
    {
        qputenv("DISPLAY", display);
    }
#endif
    qputenv("PDF4QT_APP_INSTANCE_NAME", "custom-name");
    QCOMPARE(pdf::PDFAppInstanceGuard::getServerName(), QStringLiteral("custom-name"));
    qunsetenv("PDF4QT_APP_INSTANCE_NAME");

    // The lock is in the private directory of the user, not in the shared temp folder
    QCOMPARE(pdf::PDFAppInstanceGuard::getLockFilePath(QStringLiteral("a/b:c")), QDir(runtimeDirectory).filePath(QStringLiteral("a_b_c.lock")));
    QCOMPARE(pdf::PDFAppInstanceGuard::getLockFilePath(defaultName), QDir(runtimeDirectory).filePath(QFileInfo(defaultName).fileName() + QStringLiteral(".lock")));
    QVERIFY(pdf::PDFAppInstanceGuard::getLockFilePath(QString()).isEmpty());
}

void AppCoreTest::instanceDeletedFromSlot()
{
    // The server deleted by a slot of its own signal
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("deleted"));
    QPointer<pdf::PDFAppInstanceServer> server = new pdf::PDFAppInstanceServer();
    QVERIFY(server->listen(serverName));
    connect(server, &pdf::PDFAppInstanceServer::openRequested, server, [&server]() { delete server.data(); });

    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Failed;
    std::thread thread([&]()
    {
        result = pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 10000, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * AppCoreTestHelper::getTimeoutFactor()));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Delivered);
    QTRY_VERIFY(!server);
}

void AppCoreTest::guardCrossProcess()
{
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("cross"));
    QProcess* process = startChild({ QStringLiteral("--core-server"), serverName, QStringLiteral("0"), QStringLiteral("server") });
    QVERIFY2(waitForOutput(process, "listening", 30000 * factor).contains("listening"), "the child does not listen");

    // This process forwards to the child
    pdf::PDFAppInstanceServer server;
    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    request.page = 9;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, request, &server, 10000 * factor, &error), pdf::PDFAppInstanceGuard::Result::Forwarded);
    QVERIFY(!server.isListening());

    const QByteArray output = waitForOutput(process, "request", 10000 * factor);
    QVERIFY2(output.contains(QStringLiteral("request %1 page 9").arg(m_existingFile).toUtf8()), output.constData());

    process->kill();
    process->waitForFinished(10000 * factor);
}

void AppCoreTest::guardStaleEndpoint()
{
    // A killed instance leaves its endpoint (a socket file on Unix)
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("stale"));
    QProcess* process = startChild({ QStringLiteral("--core-server"), serverName, QStringLiteral("0"), QStringLiteral("server") });
    QVERIFY(waitForOutput(process, "listening", 30000 * factor).contains("listening"));
    process->kill();
    QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(10000 * factor));

    // The next instance becomes the server
    pdf::PDFAppInstanceServer server;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), &server, 5000 * factor, &error), pdf::PDFAppInstanceGuard::Result::Listening);
    QVERIFY(server.isListening());

    // And it is reachable
    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Failed;
    std::thread thread([&]()
    {
        result = pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 10000, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * factor));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Delivered);
}

void AppCoreTest::guardConcurrentStart()
{
    // Four processes start at once: exactly one listens, the others forward to it
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("concurrent"));
    const int processCount = 4;

    QList<QProcess*> processes;
    for (int i = 0; i < processCount; ++i)
    {
        processes << startChild({ QStringLiteral("--core-guard"), serverName, QString::number(6000 * factor), QString::number(i) });
    }

    int listening = 0;
    int forwarded = 0;
    int standalone = 0;
    int received = -1;
    for (QProcess* process : processes)
    {
        QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(120000 * factor));
        const QByteArray output = process->readAllStandardOutput();
        switch (process->exitCode())
        {
            case 10:
                ++forwarded;
                break;
            case 20:
            {
                ++listening;
                const qsizetype index = output.indexOf("received=");
                QVERIFY2(index >= 0, output.constData());
                received = output.mid(index + 9).trimmed().toInt();
                break;
            }
            case 30:
                ++standalone;
                break;
            default:
                QFAIL(output.constData());
        }
    }

    qInfo("listening %d, forwarded %d, standalone %d, received %d", listening, forwarded, standalone, received);
    QCOMPARE(listening, 1);
    QCOMPARE(forwarded + standalone, processCount - 1);
    QCOMPARE(received, forwarded);
    qDeleteAll(processes);
}

void AppCoreTest::guardWithoutFiles()
{
    // Owner decision (decision 11): a start without files and --new-window forward
    // nothing; they take the name only, when it is free
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("nofiles"));
    QProcess* process = startChild({ QStringLiteral("--core-server"), serverName, QStringLiteral("0"), QStringLiteral("server") });
    QVERIFY(waitForOutput(process, "listening", 30000 * factor).contains("listening"));

    pdf::PDFAppInstanceRequest withFiles;
    withFiles.files << m_existingFile;

    pdf::PDFAppInstanceServer server;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), &server, 5000 * factor, &error), pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!server.isListening());
    QVERIFY(!error.isEmpty());
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, withFiles, &server, 5000 * factor, &error, pdf::PDFAppInstanceGuard::Mode::ListenIfFree),
             pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!server.isListening());

    // Nothing has reached the running instance
    process->kill();
    QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(10000 * factor));
    QVERIFY(!process->readAllStandardOutput().contains("request"));

    // A free name (here a stale endpoint of the killed process) is taken
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, withFiles, &server, 5000 * factor, &error, pdf::PDFAppInstanceGuard::Mode::ListenIfFree),
             pdf::PDFAppInstanceGuard::Result::Listening);
    QVERIFY(server.isListening());
}

void AppCoreTest::suspendAndResume()
{
    // newapplication.md 3: the server stops listening at the start of the quitting and
    // listens again, when the user cancels it - unless a new instance took the name
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("suspend"));
    pdf::PDFAppInstanceServer first;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), &first, 5000 * factor, &error), pdf::PDFAppInstanceGuard::Result::Listening);
    QSignalSpy firstSpy(&first, &pdf::PDFAppInstanceServer::openRequested);

    // A connection accepted before the suspension is kept: its request is held
    // without an answer while the application is quitting (the window may close)
    QLocalSocket pending;
    pending.connectToServer(serverName);
    QVERIFY(pending.waitForConnected(5000 * factor));

    first.suspend();
    QVERIFY(first.isSuspended());
    QVERIFY(!first.isListening());
    QVERIFY(!first.isQuitting());
    QCOMPARE(first.getRequestedServerName(), serverName);

    pending.write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 2, QString(), QStringList() })));
    pending.flush();
    QTRY_COMPARE_WITH_TIMEOUT(first.getHeldRequestCount(), 1, 10000 * factor);
    QTest::qWait(100);
    QCOMPARE(firstSpy.count(), 0);
    QCOMPARE(pending.bytesAvailable(), 0);

    // The quitting is canceled: the held request is acknowledged and opened by its
    // window; the server listens again - at once, or when its last connection has
    // closed (on Windows the open instance of the pipe holds the name)
    first.resume(5000 * factor, &error);
    QCOMPARE(firstSpy.count(), 1);
    QCOMPARE(firstSpy.at(0).at(0).value<pdf::PDFAppInstanceRequest>().page, 2);
    QCOMPARE(first.getHeldRequestCount(), 0);
    bool ok = false;
    QString replyError;
    QVERIFY(AppCoreTestHelper::readReply(pending, 10000 * factor, &ok, &replyError));
    QVERIFY(ok);
    pending.abort();
    QTRY_COMPARE_WITH_TIMEOUT(first.getConnectionCount(), 0, 10000 * factor);
    QTRY_VERIFY_WITH_TIMEOUT(first.isListening(), 10000 * factor);
    QVERIFY(!first.isSuspended());

    // The quitting starts again: nobody listens now
    first.suspend();
    QVERIFY(first.isSuspended());
    QCOMPARE(pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 1000, &error), pdf::PDFAppInstanceClient::Result::NoServer);

    // A new instance takes the name while the first one is quitting
    pdf::PDFAppInstanceServer second;
    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, request, &second, 5000 * factor, &error), pdf::PDFAppInstanceGuard::Result::Listening);

    // The quitting is canceled: the name is held, the first one runs without it
    QVERIFY(!first.resume(5000 * factor, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!first.isListening());
    QVERIFY(first.isSuspended());

    // The second one quits; the first one listens again and receives the files
    second.close();
    QVERIFY(first.resume(5000 * factor, &error));
    QVERIFY(first.isListening());
    QVERIFY(!first.isSuspended());
    QVERIFY(first.resume(5000 * factor, &error));

    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Failed;
    std::thread thread([&]()
    {
        result = pdf::PDFAppInstanceClient::send(serverName, request, 10000 * factor, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * factor));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Delivered);
    QCOMPARE(firstSpy.count(), 2);

    // Defensive cases
    pdf::PDFAppInstanceServer neverListened;
    QVERIFY(!neverListened.resume(100, &error));
    neverListened.suspend();
    QVERIFY(!neverListened.isSuspended());
    QVERIFY(!pdf::PDFAppInstanceGuard::resume(nullptr, 100, &error));
}

void AppCoreTest::quitRefusesRequests()
{
    // The quitting is accepted: requests held during the questions, messages still
    // being read and messages arriving later are refused, so the other processes
    // start by themselves - an acknowledged request would be lost with the window
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("quit"));
    pdf::PDFAppInstanceServer server;
    QVERIFY(server.listen(serverName));
    QSignalSpy openSpy(&server, &pdf::PDFAppInstanceServer::openRequested);
    QSignalSpy refusedSpy(&server, &pdf::PDFAppInstanceServer::requestRefused);

    QLocalSocket held;
    held.connectToServer(serverName);
    QVERIFY(held.waitForConnected(5000 * factor));
    QLocalSocket silent;
    silent.connectToServer(serverName);
    QVERIFY(silent.waitForConnected(5000 * factor));
    QLocalSocket late;
    late.connectToServer(serverName);
    QVERIFY(late.waitForConnected(5000 * factor));

    server.suspend();
    QCOMPARE(server.getConnectionCount(), 3);
    held.write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 1, QString(), QStringList() })));
    held.flush();
    QTRY_COMPARE_WITH_TIMEOUT(server.getHeldRequestCount(), 1, 10000 * factor);

    // The held request and the silent connection are refused at once
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*quitting"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*quitting"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*quitting"));
    server.quit();
    QVERIFY(server.isQuitting());
    QVERIFY(!server.isListening());
    QVERIFY(!server.isSuspended());
    QCOMPARE(server.getHeldRequestCount(), 0);

    for (QLocalSocket* socket : { &held, &silent, &late })
    {
        bool ok = true;
        QString error;
        QVERIFY(AppCoreTestHelper::readReply(*socket, 10000 * factor, &ok, &error));
        QVERIFY(!ok);
        QCOMPARE(error, QStringLiteral("The running instance is quitting."));
    }

    QTest::qWait(100);
    QCOMPARE(openSpy.count(), 0);
    QCOMPARE(refusedSpy.count(), 3);

    // The server does not listen again
    QString error;
    QVERIFY(!server.resume(1000, &error));
    QVERIFY(!server.isListening());
    QCOMPARE(pdf::PDFAppInstanceClient::send(serverName, pdf::PDFAppInstanceRequest(), 1000, &error), pdf::PDFAppInstanceClient::Result::NoServer);

    // A request held longer than the read timeout is refused (its process has
    // started by itself meanwhile)
    const QString busyName = AppCoreTestHelper::createServerName(QStringLiteral("busy"));
    pdf::PDFAppInstanceServer busy;
    busy.setReadTimeout(200);
    QVERIFY(busy.listen(busyName));
    QSignalSpy busyOpenSpy(&busy, &pdf::PDFAppInstanceServer::openRequested);
    QLocalSocket waiting;
    waiting.connectToServer(busyName);
    QVERIFY(waiting.waitForConnected(5000 * factor));
    busy.suspend();
    waiting.write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 1, QString(), QStringList() })));
    waiting.flush();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*busy"));
    bool ok = true;
    QVERIFY(AppCoreTestHelper::readReply(waiting, 10000 * factor, &ok, &error));
    QVERIFY(!ok);
    QCOMPARE(error, QStringLiteral("The running instance is busy."));

    // A canceled quitting then has nothing to open
    QVERIFY(busy.resume(5000 * factor, &error));
    QCOMPARE(busyOpenSpy.count(), 0);
    waiting.abort();
    QTRY_COMPARE_WITH_TIMEOUT(busy.getConnectionCount(), 0, 10000 * factor);

    // A held request, whose process has given up (disconnected), is not opened
    busy.setReadTimeout(60000);
    QLocalSocket givingUp;
    givingUp.connectToServer(busyName);
    QVERIFY(givingUp.waitForConnected(5000 * factor));
    QTRY_COMPARE_WITH_TIMEOUT(busy.getConnectionCount(), 1, 10000 * factor);
    busy.suspend();
    givingUp.write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 1, QString(), QStringList() })));
    givingUp.flush();
    QTRY_COMPARE_WITH_TIMEOUT(busy.getHeldRequestCount(), 1, 10000 * factor);
    givingUp.abort();
    QTRY_COMPARE_WITH_TIMEOUT(busy.getHeldRequestCount(), 0, 10000 * factor);
    QVERIFY(busy.resume(5000 * factor, &error));
    QCOMPARE(busyOpenSpy.count(), 0);
}

void AppCoreTest::suspendResumeDetails()
{
    // Review of the suspend / resume / quit state machine
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QByteArray message = pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 1, QString(), QStringList() }));
    QString error;

    // A canceled quitting with a connection, whose message has not come yet: resume
    // does not block (Windows: probing the name of the own open pipe would wait
    // 5 s in QLocalSocket::connectToServer), the server listens again at once or
    // when the connection has closed
    {
        const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("open"));
        pdf::PDFAppInstanceServer server;
        QVERIFY(server.listen(serverName));
        QLocalSocket silent;
        silent.connectToServer(serverName);
        QVERIFY(silent.waitForConnected(5000 * factor));
        QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 1, 10000 * factor);
        server.suspend();

        QElapsedTimer timer;
        timer.start();
        const bool isListening = server.resume(5000 * factor, &error);
        QVERIFY2(timer.elapsed() < 2000 * factor, qPrintable(QStringLiteral("resume took %1 ms").arg(timer.elapsed())));
#if defined(Q_OS_WIN)
        QVERIFY(!isListening);
        QVERIFY(server.isSuspended());
#else
        QVERIFY(isListening);
#endif
        silent.abort();
        QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);
        QVERIFY(!server.isSuspended());
    }

    // The quitting starts again after a canceled one, which has not listened again
    // (another server holds the name, a connection is open): the requests are held
    // again and the server does not listen during the questions
    {
        const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("again"));
        pdf::PDFAppInstanceServer first;
        QVERIFY(first.listen(serverName));
        QSignalSpy openSpy(&first, &pdf::PDFAppInstanceServer::openRequested);
        QLocalSocket pending;
        pending.connectToServer(serverName);
        QVERIFY(pending.waitForConnected(5000 * factor));
        QTRY_COMPARE_WITH_TIMEOUT(first.getConnectionCount(), 1, 10000 * factor);
        first.suspend();

        pdf::PDFAppInstanceServer other;
        QVERIFY(other.listen(serverName));
        QVERIFY(!first.resume(5000 * factor, &error));
        QVERIFY(first.isSuspended());
        other.close();

        first.suspend();
        QVERIFY(first.isSuspended());
        pending.write(message);
        pending.flush();
        QTRY_COMPARE_WITH_TIMEOUT(first.getHeldRequestCount(), 1, 10000 * factor);
        QTest::qWait(100);
        QCOMPARE(openSpy.count(), 0);
        QVERIFY(!first.isListening());

        // Canceled again: the held request is opened, the server listens again
        first.resume(5000 * factor, &error);
        QCOMPARE(openSpy.count(), 1);
        bool ok = false;
        QVERIFY(AppCoreTestHelper::readReply(pending, 10000 * factor, &ok, &error));
        QVERIFY(ok);
        pending.abort();
        QTRY_VERIFY_WITH_TIMEOUT(first.isListening(), 10000 * factor);
    }

    // A slot of openRequested starts the quitting again (suspend): the other held
    // requests stay held and the server does not listen
    {
        const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("nested"));
        pdf::PDFAppInstanceServer server;
        QVERIFY(server.listen(serverName));
        QLocalSocket a;
        QLocalSocket b;
        a.connectToServer(serverName);
        b.connectToServer(serverName);
        QVERIFY(a.waitForConnected(5000 * factor));
        QVERIFY(b.waitForConnected(5000 * factor));
        QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 2, 10000 * factor);
        server.suspend();
        a.write(message);
        a.flush();
        b.write(message);
        b.flush();
        QTRY_COMPARE_WITH_TIMEOUT(server.getHeldRequestCount(), 2, 10000 * factor);

        int opened = 0;
        QMetaObject::Connection connection = connect(&server, &pdf::PDFAppInstanceServer::openRequested, &server, [&server, &opened]()
        {
            ++opened;
            server.suspend();
        });
        QVERIFY(!server.resume(5000 * factor, &error));
        QCOMPARE(opened, 1);
        QCOMPARE(server.getHeldRequestCount(), 1);
        QVERIFY(!server.isListening());
        QVERIFY(server.isSuspended());
        disconnect(connection);

        server.resume(5000 * factor, &error);
        QCOMPARE(opened, 1);
        QCOMPARE(server.getHeldRequestCount(), 0);
        a.abort();
        b.abort();
        QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);
    }

    // A held request is refused before its process gives up (the client waits the
    // read timeout from its connection): an acknowledgment after it would open the
    // files twice - in this window and in the process, which has started by itself
    {
        const int readTimeout = 2000 * factor;
        const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("margin"));
        pdf::PDFAppInstanceServer server;
        server.setReadTimeout(readTimeout);
        QVERIFY(server.listen(serverName));
        QLocalSocket client;
        QElapsedTimer sinceConnection;
        sinceConnection.start();
        client.connectToServer(serverName);
        QVERIFY(client.waitForConnected(5000 * factor));
        QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 1, 10000 * factor);
        server.suspend();
        QTest::qWait(readTimeout / 4);
        client.write(message);
        client.flush();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*busy"));
        bool ok = true;
        QVERIFY(AppCoreTestHelper::readReply(client, 10000 * factor, &ok, &error));
        QVERIFY(!ok);
        QCOMPARE(error, QStringLiteral("The running instance is busy."));
        QVERIFY2(sinceConnection.elapsed() < readTimeout, qPrintable(QStringLiteral("refused after %1 ms").arg(sinceConnection.elapsed())));
    }
}

void AppCoreTest::instanceBusyPipe()
{
    // A suspended server (questions of the quitting) with an open connection: on
    // Windows the connection holds the name of the pipe, but no instance of it is
    // free. QLocalSocket::connectToServer would wait there 5 s (WaitNamedPipe) and
    // ignore the time limit of the client, so a starting process would wait 5 s
    // before it starts by itself.
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("busypipe"));
    pdf::PDFAppInstanceServer server;
    QVERIFY(server.listen(serverName));
    QSignalSpy openSpy(&server, &pdf::PDFAppInstanceServer::openRequested);
    QLocalSocket open;
    open.connectToServer(serverName);
    QVERIFY(open.waitForConnected(5000 * factor));
    QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 1, 10000 * factor);
    server.suspend();

    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    const int timeout = 1000;
    QString error;

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::send(serverName, request, timeout, &error);
    qInfo("send to the busy pipe: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 1500 * factor, qPrintable(QStringLiteral("send took %1 ms").arg(timer.elapsed())));
#if defined(Q_OS_WIN)
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Failed);
    QVERIFY(!error.isEmpty());
#else
    // The suspension has removed the socket file
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::NoServer);
#endif

    // The guard of a starting process with files: the process starts by itself (on
    // Windows never as a second server of the name)
    pdf::PDFAppInstanceServer second;
    timer.restart();
    const pdf::PDFAppInstanceGuard::Result startResult = pdf::PDFAppInstanceGuard::start(serverName, request, &second, timeout, &error);
    qInfo("start with files at the busy pipe: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 1500 * factor, qPrintable(QStringLiteral("start took %1 ms").arg(timer.elapsed())));
#if defined(Q_OS_WIN)
    QCOMPARE(startResult, pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!second.isListening());
#else
    QCOMPARE(startResult, pdf::PDFAppInstanceGuard::Result::Listening);
    second.close();
#endif

    // The time limit of the application (main.cpp): a process started during the
    // questions of the quitting waits for the busy pipe at most PIPE_WAIT_LIMIT, not
    // the whole time limit of the exchange (it waited 5 s before it started by itself)
    const int defaultTimeout = pdf::PDFAppInstanceClient::DEFAULT_TIMEOUT;
    const int waitLimit = pdf::PDFAppInstanceClient::PIPE_WAIT_LIMIT;
    QVERIFY(waitLimit + 500 < defaultTimeout);
    timer.restart();
    const pdf::PDFAppInstanceClient::Result defaultSendResult = pdf::PDFAppInstanceClient::send(serverName, request, defaultTimeout, &error);
    qInfo("send with the default time limit to the busy pipe: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < (waitLimit + 500) * factor, qPrintable(QStringLiteral("send took %1 ms").arg(timer.elapsed())));
    timer.restart();
    const pdf::PDFAppInstanceGuard::Result defaultStartResult = pdf::PDFAppInstanceGuard::start(serverName, request, &second, defaultTimeout, &error);
    qInfo("start with files and the default time limit at the busy pipe: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < (waitLimit + 500) * factor, qPrintable(QStringLiteral("start took %1 ms").arg(timer.elapsed())));
#if defined(Q_OS_WIN)
    QCOMPARE(defaultSendResult, pdf::PDFAppInstanceClient::Result::Failed);
    QCOMPARE(defaultStartResult, pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!second.isListening());
#else
    QCOMPARE(defaultSendResult, pdf::PDFAppInstanceClient::Result::NoServer);
    QCOMPARE(defaultStartResult, pdf::PDFAppInstanceGuard::Result::Listening);
    second.close();
#endif

    // A start without files probes the name only
    timer.restart();
    const pdf::PDFAppInstanceGuard::Result probeResult = pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), &second, timeout, &error);
    qInfo("start without files at the busy pipe: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 1500 * factor, qPrintable(QStringLiteral("start took %1 ms").arg(timer.elapsed())));
#if defined(Q_OS_WIN)
    QCOMPARE(probeResult, pdf::PDFAppInstanceGuard::Result::Standalone);
    QVERIFY(!second.isListening());
#else
    QCOMPARE(probeResult, pdf::PDFAppInstanceGuard::Result::Listening);
    second.close();
#endif

    // The quitting is canceled and the connection closes: the server listens again
    // and receives the files
    open.abort();
    server.resume(5000 * factor, &error);
    QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);
    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result delivered = pdf::PDFAppInstanceClient::Result::Failed;
    std::jthread thread([&]()
    {
        delivered = pdf::PDFAppInstanceClient::send(serverName, request, 10000 * factor, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * factor));
    QCOMPARE(delivered, pdf::PDFAppInstanceClient::Result::Delivered);
    QCOMPARE(openSpy.count(), 1);

#if defined(Q_OS_WIN)
    // A live server, whose instances of the pipe are all taken, creates a new one
    // from its event loop: the client waits for it within its time limit
    const QString liveName = AppCoreTestHelper::createServerName(QStringLiteral("livebusy"));
    QLocalServer live;
    live.setListenBacklogSize(1);
    QVERIFY(live.listen(liveName));
    QLocalSocket first;
    first.connectToServer(liveName);
    QVERIFY(first.waitForConnected(5000 * factor));

    isFinished = false;
    pdf::PDFAppInstanceClient::Result waited = pdf::PDFAppInstanceClient::Result::Failed;
    std::jthread waitingThread([&]()
    {
        waited = pdf::PDFAppInstanceClient::send(liveName, request, 20000 * factor, nullptr);
        isFinished = true;
    });

    // No event of the server is processed: the only instance stays taken
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    QVERIFY(!isFinished);

    QList<QLocalSocket*> accepted;
    QHash<QLocalSocket*, QByteArray> buffers;
    bool isAnswered = false;
    QDeadlineTimer deadline(30000 * factor);
    while (!isFinished && !deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        while (live.hasPendingConnections())
        {
            accepted << live.nextPendingConnection();
        }

        for (QLocalSocket* socket : accepted)
        {
            QByteArray& buffer = buffers[socket];
            buffer += socket->readAll();
            QByteArray payload;
            if (!isAnswered && pdf::PDFAppInstanceProtocol::readFrame(buffer, pdf::PDFAppInstanceProtocol::MAXIMUM_MESSAGE_SIZE, &payload) == pdf::PDFAppInstanceProtocol::FrameResult::Complete)
            {
                isAnswered = true;
                socket->write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeReply(true, QString())));
                socket->flush();
            }
        }
    }
    waitingThread.join();
    QVERIFY(isAnswered);
    QCOMPARE(waited, pdf::PDFAppInstanceClient::Result::Delivered);

    // A name without a server: the wait ends at once
    timer.restart();
    QCOMPARE(pdf::PDFAppInstanceClient::send(AppCoreTestHelper::createServerName(QStringLiteral("nobody")), request, 5000 * factor, &error),
             pdf::PDFAppInstanceClient::Result::NoServer);
    QVERIFY(timer.elapsed() < 1000 * factor);
#endif
}

void AppCoreTest::resumeWhileLocked()
{
    // The user cancels the quitting, while a starting process decides under the lock
    // of the instances: the window never waits for the lock, the server listens again
    // from the event loop, when the lock is free
    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("locked"));
    const QByteArray message = pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest({ { m_existingFile }, 4, QString(), QStringList() }));
    const int retryInterval = pdf::PDFAppInstanceServer::RETRY_INTERVAL;
    const int retryLimit = 5000 * factor;
    QProcess* locker = startChild({ QStringLiteral("--core-lock"), serverName });
    QVERIFY(locker->waitForStarted(30000 * factor));
    QString error;

    pdf::PDFAppInstanceServer server;
    QVERIFY(server.listen(serverName));
    QSignalSpy openSpy(&server, &pdf::PDFAppInstanceServer::openRequested);

    // Resume returns at once and the server listens later
    server.suspend();
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!server.resume(retryLimit, &error));
    qInfo("resume with the lock held: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 500 * factor, qPrintable(QStringLiteral("resume took %1 ms").arg(timer.elapsed())));
    QVERIFY(!error.isEmpty());
    QVERIFY(server.isSuspended());
    QTest::qWait(3 * retryInterval);
    QVERIFY(!server.isListening());
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);
    QVERIFY(!server.isSuspended());

    // A request held during the questions is opened at once, although the lock is
    // held; the server listens again, when the lock is free (on Windows also only
    // after its connection has closed)
    QLocalSocket pending;
    pending.connectToServer(serverName);
    QVERIFY(pending.waitForConnected(5000 * factor));
    QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 1, 10000 * factor);
    server.suspend();
    pending.write(message);
    pending.flush();
    QTRY_COMPARE_WITH_TIMEOUT(server.getHeldRequestCount(), 1, 10000 * factor);
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    timer.restart();
    QVERIFY(!server.resume(retryLimit, &error));
    qInfo("resume with the lock held and a held request: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 500 * factor, qPrintable(QStringLiteral("resume took %1 ms").arg(timer.elapsed())));
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.at(0).at(0).value<pdf::PDFAppInstanceRequest>().page, 4);
    bool ok = false;
    QVERIFY(AppCoreTestHelper::readReply(pending, 10000 * factor, &ok, &error));
    QVERIFY(ok);
    pending.abort();
    QTRY_COMPARE_WITH_TIMEOUT(server.getConnectionCount(), 0, 10000 * factor);
    QTest::qWait(3 * retryInterval);
    QVERIFY(!server.isListening());
    QVERIFY(server.isSuspended());
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);
    QVERIFY(!server.isSuspended());

    // The quitting starts again during the attempts: the server does not listen
    // during the questions, the next canceling tries again
    server.suspend();
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QVERIFY(!server.resume(retryLimit, &error));
    server.suspend();
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTest::qWait(3 * retryInterval);
    QVERIFY(!server.isListening());
    QVERIFY(server.isSuspended());
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QVERIFY(!server.resume(retryLimit, &error));
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTRY_VERIFY_WITH_TIMEOUT(server.isListening(), 10000 * factor);

    // A request of another process reaches the server again
    std::atomic<bool> isFinished = false;
    pdf::PDFAppInstanceClient::Result result = pdf::PDFAppInstanceClient::Result::Failed;
    pdf::PDFAppInstanceRequest request;
    request.files << m_existingFile;
    std::jthread thread([&]()
    {
        result = pdf::PDFAppInstanceClient::send(serverName, request, 10000 * factor, nullptr);
        isFinished = true;
    });
    QVERIFY(AppCoreTestHelper::waitForThread(thread, isFinished, 30000 * factor));
    QCOMPARE(result, pdf::PDFAppInstanceClient::Result::Delivered);
    QCOMPARE(openSpy.count(), 2);

    // The quitting is accepted during the attempts: the server never listens again
    server.suspend();
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QVERIFY(!server.resume(retryLimit, &error));
    server.quit();
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTest::qWait(3 * retryInterval);
    QVERIFY(!server.isListening());
    QVERIFY(server.isQuitting());

    // The attempts end after their time limit
    pdf::PDFAppInstanceServer limited;
    QVERIFY(limited.listen(serverName));
    limited.suspend();
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QVERIFY(!limited.resume(2 * retryInterval, &error));
    QTest::qWait(5 * retryInterval);
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTest::qWait(3 * retryInterval);
    QVERIFY(!limited.isListening());
    QVERIFY(limited.isSuspended());
    QVERIFY(limited.resume(retryLimit, &error));

    // Another instance takes the name during the attempts: they end, the server
    // runs without the forwarding
    limited.suspend();
    QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
    QVERIFY(!limited.resume(retryLimit, &error));
    pdf::PDFAppInstanceServer other;
    QVERIFY(other.listen(serverName));
    QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
    QTest::qWait(3 * retryInterval);
    QVERIFY(!limited.isListening());
    QVERIFY(limited.isSuspended());
    other.close();

    // The server is deleted during the attempts
    {
        pdf::PDFAppInstanceServer* deleted = new pdf::PDFAppInstanceServer();
        QVERIFY(deleted->listen(serverName));
        deleted->suspend();
        QVERIFY(sendLockerCommand(locker, "lock", "lock-acquired", 30000 * factor));
        QVERIFY(!deleted->resume(retryLimit, &error));
        delete deleted;
        QVERIFY(sendLockerCommand(locker, "unlock", "lock-released", 30000 * factor));
        QTest::qWait(3 * retryInterval);
    }

    locker->closeWriteChannel();
    QVERIFY(locker->waitForFinished(30000 * factor));
    QCOMPARE(locker->exitCode(), 0);
    delete locker;
}

void AppCoreTest::logs()
{
    const QString directory = m_directory.filePath(QStringLiteral("logs"));
    pdf::PDFAppLogOptions options;
    options.directory = directory;

    const QDateTime now(QDate(2026, 10, 19), QTime(12, 0));
    pdf::PDFAppLog::setCurrentDateTimeForTesting(now);
    QVERIFY(pdf::PDFAppLog::install(options));
    QVERIFY(pdf::PDFAppLog::isInstalled());
    QCOMPARE(pdf::PDFAppLog::getLogDirectory(), directory);
    const QString fileName = QDir(directory).filePath(QStringLiteral("pdf4qt-2026-10-19.log"));
    QCOMPARE(pdf::PDFAppLog::getCurrentLogFile(), fileName);

    // Messages of more threads
    qInfo("log message from the main thread");
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back([i]()
        {
            for (int j = 0; j < 50; ++j)
            {
                qDebug("thread %d message %d", i, j);
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    qWarning("warning\nwith two lines");

    // Untrusted texts cannot forge a line of the log (next lines are indented,
    // control characters replaced)
    qWarning("forging\n2026-10-19T12:00:00.000 [info] forged one\r2026-10-19T12:00:00.000 [info] forged two\x01" "end");
    pdf::PDFAppLog::flush();

    QFile file(fileName);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("[info] log message from the main thread")));
    QVERIFY(content.contains(QStringLiteral("[warning] warning")));
    QVERIFY(content.contains(QStringLiteral("2026-10-19T12:00:00.000")));
    for (int i = 0; i < 4; ++i)
    {
        QVERIFY(content.contains(QStringLiteral("thread %1 message 49").arg(i)));
    }
    QCOMPARE(content.count(QStringLiteral("[debug] thread")), 200);
    for (const QString& line : content.split(QLatin1Char('\n')))
    {
        QVERIFY2(line.isEmpty() || line.startsWith(QStringLiteral("2026-10-19T12:00:00.000 [")) || line.startsWith(QStringLiteral("    ")), qPrintable(line));
        QVERIFY2(!line.startsWith(QStringLiteral("2026-10-19T12:00:00.000 [info] forged")), qPrintable(line));
        QVERIFY(!line.contains(QChar(1)) && !line.contains(QLatin1Char('\r')));
        QVERIFY(!line.contains(QStringLiteral("end")) || line.contains(QStringLiteral("forged two") + QChar(0xFFFD) + QStringLiteral("end")));
    }
    QVERIFY(content.contains(QStringLiteral("\n    2026-10-19T12:00:00.000 [info] forged two")));

#if defined(Q_OS_UNIX)
    // The folder created for the logs is accessible only by the user
    QVERIFY(!(QFile::permissions(directory) & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::ExeGroup | QFileDevice::ExeOther)));
#endif

    // Folder of the logs (owner decision): Linux the state directory of XDG
    {
        const QByteArray dataDirectory = qgetenv("PDF4QT_APP_DATA_DIRECTORY");
        qunsetenv("PDF4QT_APP_DATA_DIRECTORY");
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
        QCOMPARE(pdf::PDFAppPaths::getLogDirectory(), QDir(QStandardPaths::writableLocation(QStandardPaths::StateLocation)).filePath(QStringLiteral("logs")));
#else
        QCOMPARE(pdf::PDFAppPaths::getLogDirectory(), QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("logs")));
#endif
        qputenv("PDF4QT_APP_DATA_DIRECTORY", m_directory.path().toUtf8());
        QCOMPARE(pdf::PDFAppPaths::getLogDirectory(), QDir(m_directory.path()).filePath(QStringLiteral("logs")));
        QCOMPARE(pdf::PDFAppPaths::getDatabaseFilePath(), QDir(m_directory.path()).filePath(QStringLiteral("pdf4qt-%1.db").arg(pdf::PDFDatabaseSchema::getCurrentVersion())));
        if (dataDirectory.isNull())
        {
            qunsetenv("PDF4QT_APP_DATA_DIRECTORY");
        }
        else
        {
            qputenv("PDF4QT_APP_DATA_DIRECTORY", dataDirectory);
        }
    }

    pdf::PDFAppLog::uninstall();
    QVERIFY(!pdf::PDFAppLog::isInstalled());

    // A removed log has no folder and no file (About shows no folder of it)
    QVERIFY(pdf::PDFAppLog::getLogDirectory().isEmpty());
    QVERIFY(pdf::PDFAppLog::getCurrentLogFile().isEmpty());
    pdf::PDFAppLog::uninstall();
    QVERIFY(pdf::PDFAppLog::getLogDirectory().isEmpty());

    // Installed again: the folder of the new installation
    QVERIFY(pdf::PDFAppLog::install(options));
    QCOMPARE(pdf::PDFAppLog::getLogDirectory(), directory);
    pdf::PDFAppLog::uninstall();
    QVERIFY(pdf::PDFAppLog::getLogDirectory().isEmpty());
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime());

    QCOMPARE(pdf::PDFAppLog::getFileDate(QStringLiteral("pdf4qt-2026-01-02.log")), QDate(2026, 1, 2));
    QVERIFY(!pdf::PDFAppLog::getFileDate(QStringLiteral("pdf4qt-2026-13-02.log")).isValid());
    QVERIFY(!pdf::PDFAppLog::getFileDate(QStringLiteral("other.log")).isValid());
}

void AppCoreTest::logRotationAndRetention()
{
    const QString directory = m_directory.filePath(QStringLiteral("rotation"));
    QDir().mkpath(directory);

    // Old files and a foreign file
    for (int day = 1; day <= 12; ++day)
    {
        QFile file(QDir(directory).filePath(pdf::PDFAppLog::getFileName(QDate(2026, 10, day))));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("old\n");
    }
    QFile foreign(QDir(directory).filePath(QStringLiteral("notes.txt")));
    QVERIFY(foreign.open(QIODevice::WriteOnly));
    foreign.close();

    pdf::PDFAppLogOptions options;
    options.directory = directory;
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime(QDate(2026, 10, 12), QTime(23, 59)));
    QVERIFY(pdf::PDFAppLog::install(options));

    // 7 days are kept: 6. 10. - 12. 10.
    QStringList files = QDir(directory).entryList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files, QDir::Name);
    QCOMPARE(files.size(), 7);
    QCOMPARE(files.first(), QStringLiteral("pdf4qt-2026-10-06.log"));
    QVERIFY(QFile::exists(foreign.fileName()));

    // Midnight: a new file, the oldest one is deleted
    qInfo("before midnight");
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime(QDate(2026, 10, 13), QTime(0, 1)));
    qInfo("after midnight");
    QCOMPARE(pdf::PDFAppLog::getCurrentLogFile(), QDir(directory).filePath(QStringLiteral("pdf4qt-2026-10-13.log")));
    files = QDir(directory).entryList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files, QDir::Name);
    QCOMPARE(files.size(), 7);
    QCOMPARE(files.first(), QStringLiteral("pdf4qt-2026-10-07.log"));
    pdf::PDFAppLog::uninstall();
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime());

    QFile today(QDir(directory).filePath(QStringLiteral("pdf4qt-2026-10-13.log")));
    QVERIFY(today.open(QIODevice::ReadOnly));
    const QByteArray content = today.readAll();
    QVERIFY(content.contains("after midnight"));
    QVERIFY(!content.contains("before midnight"));

    // Cleanup by the size: the oldest files go first, never the file of the day
    const QString sizeDirectory = m_directory.filePath(QStringLiteral("size"));
    QDir().mkpath(sizeDirectory);
    for (int day = 1; day <= 5; ++day)
    {
        QFile file(QDir(sizeDirectory).filePath(pdf::PDFAppLog::getFileName(QDate(2026, 10, day))));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1000, 'x'));
    }
    const QStringList deleted = pdf::PDFAppLog::cleanup(sizeDirectory, QDate(2026, 10, 5), 7, 2500);
    QCOMPARE(deleted.size(), 3);
    files = QDir(sizeDirectory).entryList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files, QDir::Name);
    QCOMPARE(files, QStringList({ "pdf4qt-2026-10-04.log", "pdf4qt-2026-10-05.log" }));

    // The file of the day alone over the limit is deleted
    QVERIFY(pdf::PDFAppLog::cleanup(sizeDirectory, QDate(2026, 10, 5), 7, 500).size() == 2);
    QVERIFY(pdf::PDFAppLog::cleanup(QString(), QDate(2026, 10, 5), 7, 500).isEmpty());
    QVERIFY(pdf::PDFAppLog::cleanup(sizeDirectory, QDate(), 7, 500).isEmpty());
}

void AppCoreTest::logSizeLimit()
{
    const QString directory = m_directory.filePath(QStringLiteral("limit"));
    QDir().mkpath(directory);
    {
        QFile file(QDir(directory).filePath(pdf::PDFAppLog::getFileName(QDate(2026, 10, 18))));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(6000, 'x'));
    }

    pdf::PDFAppLogOptions options;
    options.directory = directory;
    options.maximumTotalSize = 10000;
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime(QDate(2026, 10, 19), QTime(8, 0)));
    QVERIFY(pdf::PDFAppLog::install(options));
    for (int i = 0; i < 500; ++i)
    {
        qInfo("message number %d of the size test", i);
    }
    pdf::PDFAppLog::uninstall();
    pdf::PDFAppLog::setCurrentDateTimeForTesting(QDateTime());

    qint64 totalSize = 0;
    for (const QFileInfo& info : QDir(directory).entryInfoList(QDir::Files))
    {
        totalSize += info.size();
    }
    QVERIFY2(totalSize <= 10000 + 500, qPrintable(QString::number(totalSize)));

    QFile file(QDir(directory).filePath(QStringLiteral("pdf4qt-2026-10-19.log")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.contains("message number 0 "));
    QVERIFY(!content.contains("message number 499 "));
    QVERIFY(content.contains("size limit"));
}

void AppCoreTest::logPortable()
{
    const QString directory = m_directory.filePath(QStringLiteral("portablelogs"));
    pdf::PDFAppLogOptions options;
    options.directory = directory;
    options.portable = true;
    QVERIFY(!pdf::PDFAppLog::install(options));
    QVERIFY(pdf::PDFAppLog::isInstalled());
    qInfo("portable message");
    QVERIFY(pdf::PDFAppLog::getCurrentLogFile().isEmpty());
    QVERIFY(pdf::PDFAppLog::getLogDirectory().isEmpty());
    pdf::PDFAppLog::uninstall();
    QVERIFY(!QDir(directory).exists());
}

void AppCoreTest::uiLocale()
{
    // O17: an English user interface on a Czech system - the default locale of the
    // process is the locale of the system, until the application sets it
    pdftest::AppSystemLocaleGuard guard(pdftest::AppSystemLocaleGuard::czech());

    QCOMPARE(pdf::PDFAppLocale::getDefaultUiLocale().language(), QLocale::English);
    QCOMPARE(pdf::PDFAppLocale::getDefaultUiLocale().territory(), QLocale::UnitedStates);
    QCOMPARE(pdf::PDFAppLocale::getUiLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::formatDate(QDate(2026, 10, 23)), QStringLiteral("Oct 23, 2026"));
    QCOMPARE(pdf::PDFAppLocale::formatShortDate(QDate(2026, 10, 23)), QStringLiteral("Oct 23"));
    QCOMPARE(pdf::PDFAppLocale::formatClock(QTime(14, 5)), QStringLiteral("14:05"));
    QCOMPARE(pdf::PDFAppLocale::formatDateTime(QDateTime(QDate(2026, 10, 23), QTime(14, 5))), QStringLiteral("Oct 23, 2026, 14:05"));
    QVERIFY(pdf::PDFAppLocale::formatDateTime(QDateTime()).isEmpty());
    QCOMPARE(pdf::PDFAppLocale::formatNumber(1234567), QStringLiteral("1,234,567"));
    QCOMPARE(pdf::PDFAppLocale::formatDataSize(1300, 1, QLocale::DataSizeTraditionalFormat), QStringLiteral("1.3 kB"));
    QCOMPARE(pdf::PDFAppLocale::formatDataSize(1234567, 1, QLocale::DataSizeSIFormat), QStringLiteral("1.2 MB"));
    QCOMPARE(pdf::PDFAppLocale::formatDataSize(-5, 0, QLocale::DataSizeSIFormat), pdf::PDFAppLocale::formatDataSize(0, 0, QLocale::DataSizeSIFormat));

    // The paper and the units of the printer are regional settings of the system
    QCOMPARE(pdf::PDFAppLocale::getRegionalLocale(), QLocale::system());

    // The languages of the setting appearance.language (O12: English by default)
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral("en")), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral(" EN ")), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QString()), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral("auto")), QLocale::system());
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral("cs")).language(), QLocale::Czech);
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral("pt_BR")).territory(), QLocale::Brazil);
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QStringLiteral("not a language")), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::getLocaleForLanguage(QString(100000, QChar('x'))), pdf::PDFAppLocale::getDefaultUiLocale());

    // The context makes it the default locale of the process: "%Ln" of tr(), the
    // widgets of Qt and of Pdf4QtLibFluent follow the language of the interface
    {
        const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", "--portable" }, QString());
        pdf::PDFAppContext context(commandLine);
        QCOMPARE(QLocale(), pdf::PDFAppLocale::getUiLocale());
        QCOMPARE(QCoreApplication::translate("AppCoreTest", "%Ln pages", nullptr, 1592), QStringLiteral("1,592 pages"));
        QWidget widget;
        QCOMPARE(widget.locale(), pdf::PDFAppLocale::getUiLocale());
    }

    // A change of the language (the translator of step 5); the formatting also from
    // a worker thread
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::German, QLocale::Germany));
    QCOMPARE(QLocale().language(), QLocale::German);
    QCOMPARE(pdf::PDFAppLocale::formatNumber(1234), QStringLiteral("1.234"));
    QString threadText;
    std::thread worker([&threadText]() { threadText = pdf::PDFAppLocale::formatNumber(5678); });
    worker.join();
    QCOMPARE(threadText, QStringLiteral("5.678"));
}

void AppCoreTest::authorSettingApplied()
{
    const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", "--portable" }, QString());
    pdf::PDFAppContext context(commandLine);
    pdf::PDFDatabaseOptions options;
    options.memoryName = QStringLiteral("/pdf4qt-core-author-%1").arg(QCoreApplication::applicationPid());
    context.open(options);
    QVERIFY(context.getDatabase());

    // Q1: Anonymous by default (the name is stored in the documents)
    QCOMPARE(context.getSettings().getString(pdf::PDFSettingKeys::annotationsAuthorMode()), pdf::PDFSettingValues::authorModeAnonymous());
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorNameMode(), pdf::PDFAuthorSettings::AuthorNameMode::Anonymous);
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorName(), pdf::PDFAuthorSettings::getAnonymousAuthorName());
    QVERIFY(context.getSettings().getBool(pdf::PDFSettingKeys::annotationsShowMarkupBar()));

    // A custom name is applied at every change (notification of the database)
    QVERIFY(!context.getSettings().setValue(pdf::PDFSettingKeys::annotationsCustomAuthorName(), QStringLiteral("Jane Doe")).isError());
    QVERIFY(!context.getSettings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()).isError());
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorName(), QStringLiteral("Jane Doe"));
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorNameMode(), pdf::PDFAuthorSettings::AuthorNameMode::CustomName);

    // Control characters and bidirectional overrides never get into a document
    QVERIFY(!context.getSettings().setValue(pdf::PDFSettingKeys::annotationsCustomAuthorName(), QString::fromUtf8("Ja\nne\x01 \xE2\x80\xAE""Doe")).isError());
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorName(), QStringLiteral("Jane Doe"));
    QCOMPARE(pdf::PDFAppContext::sanitizeAuthorName(QString(1000, QLatin1Char('x'))).size(), 256);
    QCOMPARE(pdf::PDFAppContext::sanitizeAuthorName(QStringLiteral("  Name  ")), QStringLiteral("Name"));
    QString surrogates;
    for (int i = 0; i < 200; ++i)
    {
        surrogates += QString::fromUtf8("\xF0\x9F\x98\x80");
    }
    const QString cut = pdf::PDFAppContext::sanitizeAuthorName(QLatin1Char('a') + surrogates);
    QVERIFY(cut.size() <= 256);
    QVERIFY(!cut.back().isHighSurrogate());

    // A text longer than the limit of the setting is refused by the repository
    QVERIFY(context.getSettings().setValue(pdf::PDFSettingKeys::annotationsCustomAuthorName(), QString(300, QLatin1Char('y'))).isError());

    // System user name
    QVERIFY(!context.getSettings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeSystemUserName()).isError());
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorNameMode(), pdf::PDFAuthorSettings::AuthorNameMode::SystemUserName);

    // Restore defaults of the section Editing: Anonymous again
    QVERIFY(!context.getSettings().restoreDefaults(pdf::PDFSettingDefinitions::sectionEditing()).isError());
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorNameMode(), pdf::PDFAuthorSettings::AuthorNameMode::Anonymous);
}

void AppCoreTest::reviewAuthorNameSanitizing()
{
    using C = pdf::PDFAppContext;

    // Joiners of names in Persian and Indic scripts and emoji sequences are kept
    const QString persian = QString::fromUtf8("\xD9\x85\xDB\x8C\xE2\x80\x8C\xD8\xB1\xD9\x88\xD9\x85");
    QCOMPARE(C::sanitizeAuthorName(persian), persian);
    const QString family = QString::fromUtf8("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB");
    QCOMPARE(C::sanitizeAuthorName(family), family);

    // Bidirectional controls, isolates, line / paragraph separators, a BOM and the
    // format characters of the supplementary planes (tags) are removed
    QCOMPARE(C::sanitizeAuthorName(QString::fromUtf8("a\xE2\x80\xAE" "b\xE2\x81\xA6" "c\xE2\x80\xA8" "d\xE2\x80\xA9" "e\xEF\xBB\xBF" "f\xF3\xA0\x80\x81" "g")), QStringLiteral("abcdefg"));

    // Lone surrogates (invalid UTF-16) never get into a document, wherever they are
    QString lone = QStringLiteral("x");
    lone += QChar(0xDC00);
    lone += QStringLiteral("y");
    lone += QChar(0xD800);
    lone += QStringLiteral("z");
    lone += QChar(0xD800);
    QCOMPARE(C::sanitizeAuthorName(lone), QStringLiteral("xyz"));

    // The limit never cuts a surrogate pair
    QString emoji;
    for (int i = 0; i < 200; ++i)
    {
        emoji += QString::fromUtf8("\xF0\x9F\x98\x80");
    }
    const QString cut = C::sanitizeAuthorName(QLatin1Char('a') + emoji);
    QCOMPARE(cut.size(), 255);
    QVERIFY(!cut.back().isHighSurrogate());
    QVERIFY(cut.isValidUtf16());
    QCOMPARE(C::sanitizeAuthorName(QString(300, QLatin1Char('n'))).size(), 256);
    QVERIFY(C::sanitizeAuthorName(QString()).isEmpty());
}

void AppCoreTest::reviewShortcutScopes()
{
    pdf::PDFAppCommandRegistry registry;
    pdf::PDFAppCommands::registerBuiltinCommands(&registry);

    // F2 opens the properties of an annotation on the canvas (Figma 1019:18817),
    // Alt+Return is its second key; F2 renames an outline item in the panel
    const pdf::PDFAppCommand* properties = registry.findCommand(pdf::PDFAppCommands::annotationProperties());
    const pdf::PDFAppCommand* rename = registry.findCommand(pdf::PDFAppCommands::outlineRename());
    QVERIFY(properties);
    QVERIFY(rename);
    QCOMPARE(properties->scope, pdf::PDFAppCommandScope::Canvas);
    QCOMPARE(rename->scope, pdf::PDFAppCommandScope::Panel);
    QCOMPARE(properties->defaultShortcut, QKeySequence(Qt::Key_F2));
    QCOMPARE(rename->defaultShortcut, QKeySequence(Qt::Key_F2));
    QCOMPARE(registry.getShortcuts(properties->id, {}), QList<QKeySequence>({ QKeySequence(Qt::Key_F2), QKeySequence(Qt::ALT | Qt::Key_Return) }));
    QVERIFY(registry.findConflicts({}).empty());

    // The canvas and a panel never conflict, every other pair of scopes does
    using S = pdf::PDFAppCommandScope;
    QVERIFY(!pdf::PDFAppCommandRegistry::isScopeConflict(S::Canvas, S::Panel));
    QVERIFY(!pdf::PDFAppCommandRegistry::isScopeConflict(S::Panel, S::Canvas));
    QVERIFY(pdf::PDFAppCommandRegistry::isScopeConflict(S::Canvas, S::Canvas));
    QVERIFY(pdf::PDFAppCommandRegistry::isScopeConflict(S::Panel, S::Panel));
    QVERIFY(pdf::PDFAppCommandRegistry::isScopeConflict(S::Document, S::Canvas));
    QVERIFY(pdf::PDFAppCommandRegistry::isScopeConflict(S::Window, S::Panel));

    // F2 for a command of the document tab conflicts with both of them
    QStringList conflicting = registry.getConflictingCommands(pdf::PDFAppCommands::newTab(), QKeySequence(Qt::Key_F2), {});
    conflicting.sort();
    QCOMPARE(conflicting, QStringList({ pdf::PDFAppCommands::annotationProperties(), pdf::PDFAppCommands::outlineRename() }));
    QCOMPARE(registry.getConflictingCommands(pdf::PDFAppCommands::outlineRename(), QKeySequence(Qt::Key_F2), {}), QStringList());

    // The second key is a key of the command: a conflict with it is reported
    QCOMPARE(registry.getConflictingCommands(pdf::PDFAppCommands::cut(), QKeySequence(Qt::ALT | Qt::Key_Return), {}), QStringList({ pdf::PDFAppCommands::annotationProperties() }));
    pdf::PDFAppCommandRegistry::UserShortcuts user;
    user[pdf::PDFAppCommands::cut()] = QStringLiteral("Alt+Return");
    std::vector<pdf::PDFAppShortcutConflict> conflicts = registry.findConflicts(user);
    QCOMPARE(conflicts.size(), size_t(1));
    QCOMPARE(conflicts.front().commandIds.size(), 2);

    // A shortcut of the user replaces all keys of the command (also an empty one)
    user.clear();
    user[pdf::PDFAppCommands::annotationProperties()] = QStringLiteral("Ctrl+F2");
    QCOMPARE(registry.getShortcuts(properties->id, user), QList<QKeySequence>({ QKeySequence(Qt::CTRL | Qt::Key_F2) }));
    user[pdf::PDFAppCommands::annotationProperties()] = QString();
    QVERIFY(registry.getShortcuts(properties->id, user).isEmpty());

    // An invalid stored text keeps the defaults
    user[pdf::PDFAppCommands::annotationProperties()] = QStringLiteral("Ctrl+Shift+Alt+Meta+Nonsense+Key");
    QCOMPARE(registry.getShortcuts(properties->id, user).size(), 2);
    QVERIFY(registry.getShortcuts(QStringLiteral("no.command"), {}).isEmpty());
}

void AppCoreTest::contextColorScheme()
{
    const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse({ "PDF4QT", "--portable" }, QString());
    pdf::PDFAppContext context(commandLine);
    QVERIFY(context.isPortable());
    QVERIFY(context.getSettingRegistry()->findDefinition(QStringLiteral("appearance.colorScheme")));
    QVERIFY(context.getCommandRegistry()->findCommand(QStringLiteral("window.newTab")));
    QVERIFY(!context.getDatabase());

    pdf::PDFDatabaseOptions options;
    options.memoryName = QStringLiteral("/pdf4qt-core-%1").arg(QCoreApplication::applicationPid());
    context.open(options);
    QVERIFY(context.getDatabase());
    QVERIFY(context.getNotifier());
    QCOMPARE(context.getStorageState().mode, pdf::PDFDatabaseStorageMode::Portable);

    // The color scheme of the settings is applied to the theme manager
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    // Auto (default) follows the color scheme of the system, Light / Dark do not
    QVERIFY(themeManager->isFollowingSystemColorScheme());
    QVERIFY(!context.getSettings().setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("dark")).isError());
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QVERIFY(!themeManager->isFollowingSystemColorScheme());
    QVERIFY(!context.getSettings().setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("auto")).isError());
    QTRY_VERIFY(themeManager->isFollowingSystemColorScheme());
    QTRY_COMPARE(themeManager->getTheme().getScheme(), QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    QVERIFY(!context.getSettings().setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("light")).isError());
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    QTRY_VERIFY(!themeManager->isFollowingSystemColorScheme());

    // Requests of the server are re-emitted by the context
    QSignalSpy openSpy(&context, &pdf::PDFAppContext::openRequested);
    pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
    context.setInstanceServer(server);
    QCOMPARE(context.getInstanceServer(), server);
    QCOMPARE(server->parent(), &context);
    // The window shows the errors of the command line of the other process
    std::unique_ptr<pdf::PDFAppMainWindow> window = std::make_unique<pdf::PDFAppMainWindow>(&context);
    pdf::PDFAppInstanceRequest forwarded;
    forwarded.files << m_existingFile;
    forwarded.page = 1;
    forwarded.errors << QStringLiteral("Unknown argument --forwarded.");
    Q_EMIT server->openRequested(forwarded);
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(window->getRequestedFiles(), QStringList({ m_existingFile }));
    QCOMPARE(window->getForwardedErrors(), QStringList({ QStringLiteral("Unknown argument --forwarded.") }));
    // The dialog open-failed lists the errors of a request (step 3: shown from the event loop)
    const auto isErrorShown = [&window]()
    {
        for (const QLabel* label : window->findChildren<QLabel*>())
        {
            if (label->text().contains(QStringLiteral("Unknown argument --forwarded.")))
            {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY(isErrorShown());
    window.reset();
    context.setInstanceServer(nullptr);

    // Shortcuts of the user through the context
    QVERIFY(!context.getShortcuts().setShortcut(QStringLiteral("window.newTab"), QStringLiteral("Ctrl+N")).isError());
    QCOMPARE(context.getCommandRegistry()->getShortcut(QStringLiteral("window.newTab"), context.getShortcuts().getShortcuts()), QKeySequence(Qt::CTRL | Qt::Key_N));

    context.close();
    QVERIFY(!context.getDatabase());
    context.close();
}

void AppCoreTest::executableSmoke()
{
    const QString executable = AppCoreTestHelper::getExecutablePath();
    if (!QFile::exists(executable))
    {
        QSKIP("PDF4QT executable is not built");
    }

    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString dataDirectory = m_directory.filePath(QStringLiteral("appdata"));
    const QString serverName = AppCoreTestHelper::createServerName(QStringLiteral("app"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PDF4QT_APP_DATA_DIRECTORY"), dataDirectory);
    environment.insert(QStringLiteral("PDF4QT_APP_INSTANCE_NAME"), serverName);
    environment.insert(QStringLiteral("PDF4QT_APP_QUIT_AFTER_MS"), QString::number(6000 * factor));
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    QProcess first;
    first.setProcessEnvironment(environment);
    first.start(executable, { QStringLiteral("--unknown-argument") });
    QVERIFY(first.waitForStarted(30000 * factor));

    // Wait, until the first instance listens
    QDeadlineTimer deadline(30000 * factor);
    bool isListening = false;
    while (!isListening && !deadline.hasExpired())
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        isListening = socket.waitForConnected(100);
        socket.abort();
        QThread::msleep(50);
    }
    QVERIFY(isListening);

    // The second start forwards its file (a relative path of its working directory),
    // its errors of the command line and its activation token, and quits
    QProcess second;
    QProcessEnvironment secondEnvironment = environment;
    secondEnvironment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), QStringLiteral("test-activation-token"));
    second.setProcessEnvironment(secondEnvironment);
    second.setWorkingDirectory(m_directory.path());
    second.start(executable, { QStringLiteral("existing.pdf"), QStringLiteral("--page"), QStringLiteral("2"), QStringLiteral("--bogus") });
    QVERIFY(second.state() == QProcess::NotRunning || second.waitForFinished(30000 * factor));
    QCOMPARE(second.exitStatus(), QProcess::NormalExit);
    QCOMPARE(second.exitCode(), 0);

    // A start without files forwards nothing: it is a new window of its own
    QProcess third;
    QProcessEnvironment thirdEnvironment = environment;
    thirdEnvironment.insert(QStringLiteral("PDF4QT_APP_QUIT_AFTER_MS"), QString::number(500 * factor));
    third.setProcessEnvironment(thirdEnvironment);
    third.start(executable, QStringList());
    QVERIFY(third.state() == QProcess::NotRunning || third.waitForFinished(60000 * factor));
    QCOMPARE(third.exitStatus(), QProcess::NormalExit);
    QCOMPARE(third.exitCode(), 0);

    // The first one quits cleanly
    QVERIFY(first.state() == QProcess::NotRunning || first.waitForFinished(60000 * factor));
    QCOMPARE(first.exitStatus(), QProcess::NormalExit);
    QCOMPARE(first.exitCode(), 0);

    // Database (the file of the schema version, O19) and log of the first instance
    QVERIFY(QFile::exists(QDir(dataDirectory).filePath(QStringLiteral("pdf4qt-%1.db").arg(pdf::PDFDatabaseSchema::getCurrentVersion()))));
    QVERIFY(!QFile::exists(QDir(dataDirectory).filePath(QStringLiteral("pdf4qt.db"))));
    const QStringList logs = QDir(QDir(dataDirectory).filePath(QStringLiteral("logs"))).entryList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files);
    QVERIFY(!logs.isEmpty());
    QByteArray content;
    for (const QString& log : logs)
    {
        QFile file(QDir(QDir(dataDirectory).filePath(QStringLiteral("logs"))).filePath(log));
        QVERIFY(file.open(QIODevice::ReadOnly));
        content += file.readAll();
    }
    QVERIFY2(content.contains("Unknown argument --unknown-argument"), content.constData());
    QVERIFY2(content.contains("Open requested by another process: 1 file(s), page 2, with an activation token"), content.constData());
    QCOMPARE(content.count("Open requested by another process"), 1);
    QVERIFY2(content.contains("Command line of another process: Unknown argument --bogus."), content.constData());
    QVERIFY2(content.contains("Files were passed to the running instance"), content.constData());
    QVERIFY2(content.contains("Another instance receives the forwarded files"), content.constData());
}

void AppCoreTest::executablePortable()
{
    const QString executable = AppCoreTestHelper::getExecutablePath();
    if (!QFile::exists(executable))
    {
        QSKIP("PDF4QT executable is not built");
    }

    const int factor = AppCoreTestHelper::getTimeoutFactor();
    const QString dataDirectory = m_directory.filePath(QStringLiteral("portabledata"));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PDF4QT_APP_DATA_DIRECTORY"), dataDirectory);
    environment.insert(QStringLiteral("PDF4QT_APP_INSTANCE_NAME"), AppCoreTestHelper::createServerName(QStringLiteral("portable")));
    environment.insert(QStringLiteral("PDF4QT_APP_QUIT_AFTER_MS"), QString::number(1500 * factor));
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(executable, { QStringLiteral("--portable"), m_existingFile });
    QVERIFY(process.state() == QProcess::NotRunning || process.waitForFinished(60000 * factor));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    // Nothing on the disk
    QVERIFY(!QDir(dataDirectory).exists());
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of more processes
    if (argc > 1 && QByteArray(argv[1]).startsWith("--core-"))
    {
        QCoreApplication application(argc, argv);
        return AppCoreChild::run(application);
    }

    QApplication application(argc, argv);
    AppCoreTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appcoretest.moc"
