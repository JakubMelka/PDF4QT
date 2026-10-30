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

// Step 9: several real instances of the new application on one data folder. The
// test starts itself as full application instances (child mode --multi-child:
// the instance guard of main.cpp, PDFAppContext with the database of
// PDF4QT_APP_DATA_DIRECTORY, the main window offscreen), each of them connected
// to the test by a local socket, by which the test drives it and reads what the
// user would see (the theme, the shortcuts of the actions, the pages, the AI
// page, the tabs, the geometry of the window). The executable PDF4QT itself is
// started for the forwarding of a file.

#include "appdocumentfixture.h"
#include "mcptestclient.h"

#include "pdfappaipage.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappinstance.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfappmcppermissions.h"
#include "pdfappnavigator.h"
#include "pdfappopenpage.h"
#include "pdfapppage.h"
#include "pdfapprecentpage.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolspage.h"
#include "pdfapptranslator.h"
#include "pdfappwindowfactory.h"
#include "pdfappwindowgeometry.h"
#include "pdfbookmarkrepository.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabaseschema.h"
#include "pdfdatabasestatement.h"
#include "pdfmcprepository.h"
#include "pdfrecentrepository.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfshortcutrepository.h"
#include "pdftoolrepository.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace pdftest
{

/// Helpers shared by the test and its child instances
class MultiInstanceHelper
{
public:
    MultiInstanceHelper() = delete;

    static int timeout(int milliseconds) { return milliseconds * AppDocumentFixture::getTimeoutFactor(); }

    static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

    static void print(const QByteArray& line)
    {
        fputs(line.constData(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }

    static QString getStorageModeName(pdf::PDFDatabaseStorageMode mode)
    {
        switch (mode)
        {
            case pdf::PDFDatabaseStorageMode::Disk:
                return QStringLiteral("disk");
            case pdf::PDFDatabaseStorageMode::Portable:
                return QStringLiteral("portable");
            case pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema:
                return QStringLiteral("readOnlyNewerSchema");
            case pdf::PDFDatabaseStorageMode::MemoryFallback:
                return QStringLiteral("memoryFallback");
        }
        return QString();
    }

    static QString getMcpStateName(pdf::PDFAppMcpServerState state)
    {
        switch (state)
        {
            case pdf::PDFAppMcpServerState::Stopped:
                return QStringLiteral("stopped");
            case pdf::PDFAppMcpServerState::Starting:
                return QStringLiteral("starting");
            case pdf::PDFAppMcpServerState::Running:
                return QStringLiteral("running");
            case pdf::PDFAppMcpServerState::OtherInstance:
                return QStringLiteral("otherInstance");
            case pdf::PDFAppMcpServerState::PortInUse:
                return QStringLiteral("portInUse");
            case pdf::PDFAppMcpServerState::Failed:
                return QStringLiteral("failed");
        }
        return QString();
    }

    static QJsonArray toArray(const QRect& rect)
    {
        return QJsonArray({ rect.x(), rect.y(), rect.width(), rect.height() });
    }

    static QRect toRect(const QJsonValue& value)
    {
        const QJsonArray array = value.toArray();
        return QRect(array.at(0).toInt(), array.at(1).toInt(), array.at(2).toInt(), array.at(3).toInt());
    }

    static QStringList toStringList(const QJsonValue& value)
    {
        QStringList result;
        for (const QJsonValue& item : value.toArray())
        {
            result << item.toString();
        }
        return result;
    }

    static QString getExecutablePath()
    {
#if defined(Q_OS_WIN)
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("PDF4QT.exe"));
#else
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("PDF4QT"));
#endif
    }

    static quint16 findFreePort()
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0))
        {
            return 0;
        }
        const quint16 port = server.serverPort();
        server.close();
        return port;
    }
};

/// The child instance: the commands of the test arrive as lines "<id>\t<command>\t
/// <argument>..." (percent-encoded parts), each one is answered by "<id>\t<JSON>"
class MultiChildAgent : public QObject
{
public:
    MultiChildAgent(pdf::PDFAppContext* context, pdf::PDFAppMainWindow* window, const QString& guard) :
        m_context(context),
        m_window(window),
        m_guard(guard)
    {
        connect(context, &pdf::PDFAppContext::openRequested, this, [this]() { ++m_openRequested; });
        if (pdf::PDFDatabaseNotifier* notifier = context->getNotifier())
        {
            connect(notifier, &pdf::PDFDatabaseNotifier::changed, this, [this](const QString& topic, const QString& key)
            {
                const qint64 time = MultiInstanceHelper::now();
                m_notified[topic + QLatin1Char('/') + key] = double(time);
                m_notified[topic] = double(time);
            });
            connect(notifier, &pdf::PDFDatabaseNotifier::everythingChanged, this, [this]()
            {
                ++m_everythingChanged;
                m_notified[QStringLiteral("*")] = double(MultiInstanceHelper::now());
            });
        }
        if (pdf::PDFAppMcpController* controller = context->getMcpController())
        {
            connect(controller, &pdf::PDFAppMcpController::showWindowRequested, this, [this]() { ++m_showWindowRequested; });
        }

        m_answerTimer.setInterval(10);
        connect(&m_answerTimer, &QTimer::timeout, this, &MultiChildAgent::onAnswerTimer);
    }

    bool connectControl(const QString& serverName, const QString& name)
    {
        m_socket = new QLocalSocket(this);
        m_socket->connectToServer(serverName);
        if (!m_socket->waitForConnected(MultiInstanceHelper::timeout(30000)))
        {
            return false;
        }
        connect(m_socket, &QLocalSocket::readyRead, this, &MultiChildAgent::onReadyRead);

        // A child never outlives its test (no orphan process)
        connect(m_socket, &QLocalSocket::disconnected, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
        write("hello\t" + name.toUtf8() + "\t" + QByteArray::number(QCoreApplication::applicationPid()) + "\t" + m_guard.toUtf8());
        return true;
    }

    void finish()
    {
        if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        {
            m_socket->waitForBytesWritten(1000);
        }
    }

private:
    void write(const QByteArray& line)
    {
        if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        {
            m_socket->write(line + '\n');
            m_socket->flush();
        }
    }

    void reply(const QByteArray& id, const QJsonObject& object)
    {
        write(id + '\t' + QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    void onReadyRead()
    {
        m_buffer += m_socket->readAll();
        qsizetype index = m_buffer.indexOf('\n');
        while (index >= 0)
        {
            const QByteArray line = m_buffer.left(index);
            m_buffer.remove(0, index + 1);

            // A command may run a nested event loop (a dialog), where the next
            // commands are handled
            handle(line);
            if (!m_socket)
            {
                return;
            }
            index = m_buffer.indexOf('\n');
        }
    }

    static QString errorText(const pdf::PDFDatabaseError& error)
    {
        return error.isError() ? error.toString() : QString();
    }

    pdf::PDFAppDocumentTab* findDocumentTab(const QString& filePath) const
    {
        const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
        for (pdf::PDFAppDocumentTab* tab : m_window ? m_window->getDocumentTabs() : QList<pdf::PDFAppDocumentTab*>())
        {
            if (tab->getDocument() && tab->getDocument()->getFilePath() == canonicalPath)
            {
                return tab;
            }
        }
        return nullptr;
    }

    QJsonObject getState() const
    {
        QJsonObject state;
        state[QStringLiteral("pid")] = double(QCoreApplication::applicationPid());
        state[QStringLiteral("guard")] = m_guard;

        pdf::PDFAppInstanceServer* server = m_context->getInstanceServer();
        state[QStringLiteral("listening")] = server && server->isListening();
        state[QStringLiteral("held")] = server ? server->getHeldRequestCount() : 0;
        state[QStringLiteral("openRequested")] = m_openRequested;
        state[QStringLiteral("everythingChanged")] = m_everythingChanged;
        state[QStringLiteral("portable")] = m_context->isPortable();

        const pdf::PDFDatabaseState storage = m_context->getStorageState();
        state[QStringLiteral("storageMode")] = MultiInstanceHelper::getStorageModeName(storage.mode);
        state[QStringLiteral("databaseFile")] = storage.filePath;
        state[QStringLiteral("damagedFile")] = storage.damagedFilePath;

        state[QStringLiteral("theme")] = pdf::PDFThemeManager::getInstance()->getTheme().getId();
        state[QStringLiteral("language")] = m_context->getTranslator() ? m_context->getTranslator()->getLanguage() : QString();
        state[QStringLiteral("notified")] = m_notified;

        if (m_window)
        {
            state[QStringLiteral("visible")] = m_window->isVisible();
            state[QStringLiteral("geometry")] = MultiInstanceHelper::toArray(m_window->geometry());
            state[QStringLiteral("maximized")] = m_window->isMaximized();
            state[QStringLiteral("available")] = MultiInstanceHelper::toArray(m_window->screen() ? m_window->screen()->availableGeometry() : QRect());

            QJsonArray tabs;
            for (pdf::PDFAppTab* tab : m_window->getTabs())
            {
                tabs.append(tab->getPage() ? QString::fromLatin1(tab->getPage()->metaObject()->className()) : QString());
            }
            state[QStringLiteral("tabs")] = tabs;
            state[QStringLiteral("currentTab")] = int(m_window->getTabs().indexOf(m_window->getCurrentTab()));

            QJsonArray documents;
            for (pdf::PDFAppDocumentTab* tab : m_window->getDocumentTabs())
            {
                if (const pdf::PDFAppDocument* document = tab->getDocument())
                {
                    QJsonObject item;
                    item[QStringLiteral("path")] = document->getFilePath();
                    item[QStringLiteral("ready")] = document->isReady();
                    item[QStringLiteral("modified")] = document->isModified();
                    documents.append(item);
                }
            }
            state[QStringLiteral("documents")] = documents;
        }

        QJsonObject mcp;
        if (pdf::PDFAppMcpController* controller = m_context->getMcpController())
        {
            mcp[QStringLiteral("state")] = MultiInstanceHelper::getMcpStateName(controller->getState());
            mcp[QStringLiteral("owner")] = controller->isOwner();
            mcp[QStringLiteral("running")] = controller->isRunning();
            mcp[QStringLiteral("instanceId")] = controller->getInstanceId();
            mcp[QStringLiteral("assistants")] = controller->getServer() ? int(controller->getServer()->getAssistants().size()) : 0;
            mcp[QStringLiteral("serverState")] = controller->getServer() ? int(controller->getServer()->getState()) : -1;
            if (pdf::PDFAppMcpHost* host = controller->getHost())
            {
                const pdf::PDFAppMcpPermissions& permissions = host->getPermissions();
                mcp[QStringLiteral("read")] = pdf::PDFAppMcpPermissions::getName(permissions.read);
                mcp[QStringLiteral("edit")] = pdf::PDFAppMcpPermissions::getName(permissions.edit);
                mcp[QStringLiteral("files")] = pdf::PDFAppMcpPermissions::getName(permissions.files);
            }
        }
        mcp[QStringLiteral("showWindowRequested")] = m_showWindowRequested;
        state[QStringLiteral("mcp")] = mcp;

        if (m_isAnswered)
        {
            state[QStringLiteral("answeredHeld")] = m_answeredHeld;
            state[QStringLiteral("answeredOpened")] = m_answeredOpened;
        }
        return state;
    }

    QJsonObject getPageInfo(pdf::PDFAppTab* tab) const
    {
        QJsonObject info;
        pdf::PDFAppPage* page = tab ? tab->getPage() : nullptr;
        if (!page)
        {
            return info;
        }

        info[QStringLiteral("class")] = QString::fromLatin1(page->metaObject()->className());
        info[QStringLiteral("id")] = QString::number(quintptr(page), 16);
        info[QStringLiteral("visible")] = page->isVisible();

        if (pdf::PDFAppRecentPage* recentPage = qobject_cast<pdf::PDFAppRecentPage*>(page))
        {
            QJsonArray files;
            for (const pdf::PDFAppRecentItem& item : recentPage->getModel()->getItems())
            {
                if (item.type == pdf::PDFAppRecentItem::Type::File)
                {
                    files.append(item.name);
                }
            }
            QJsonArray pinned;
            for (pdf::PDFPinnedCard* card : recentPage->getPinnedCards())
            {
                pinned.append(card->text());
            }
            info[QStringLiteral("files")] = files;
            info[QStringLiteral("pinned")] = pinned;
        }

        if (pdf::PDFAppOpenPage* openPage = qobject_cast<pdf::PDFAppOpenPage*>(page))
        {
            QJsonArray workspaces;
            for (pdf::PDFWorkspaceRow* row : openPage->getWorkspaceRows())
            {
                workspaces.append(row->getName());
            }
            QJsonArray files;
            for (pdf::PDFRecentFileRow* row : openPage->getRecentFileRows())
            {
                files.append(row->getFileName());
            }
            info[QStringLiteral("workspaces")] = workspaces;
            info[QStringLiteral("files")] = files;
        }

        if (pdf::PDFAppToolsPage* toolsPage = qobject_cast<pdf::PDFAppToolsPage*>(page))
        {
            QJsonArray tools;
            for (pdf::PDFQuickActionTile* tile : toolsPage->getTiles())
            {
                tools.append(pdf::PDFAppToolPresentation::getToolId(tile));
            }
            info[QStringLiteral("pinnedTools")] = tools;
        }

        if (pdf::PDFAppAiPage* aiPage = qobject_cast<pdf::PDFAppAiPage*>(page))
        {
            info[QStringLiteral("shownState")] = int(aiPage->getShownState());
            info[QStringLiteral("startEnabled")] = aiPage->getStartStopButton() && aiPage->getStartStopButton()->isEnabled();
            info[QStringLiteral("calloutVisible")] = aiPage->getCallout() && aiPage->getCallout()->isVisibleTo(aiPage);
            QJsonArray assistants;
            for (pdf::PDFAssistantRow* row : aiPage->getAssistantRows())
            {
                assistants.append(row->getRow().name);
            }
            info[QStringLiteral("assistants")] = assistants;
        }
        return info;
    }

    void onAnswerTimer()
    {
        pdf::PDFAppQuitUnsavedDialog* dialog = AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
        if (!dialog)
        {
            return;
        }

        pdf::PDFAppInstanceServer* server = m_context->getInstanceServer();
        const int held = server ? server->getHeldRequestCount() : 0;
        if (m_isWaitingForHeld && held < 1 && !m_answerDeadline.hasExpired())
        {
            return;
        }

        m_answerTimer.stop();
        m_isAnswered = true;
        m_answeredHeld = held;
        m_answeredOpened = m_openRequested;
        MultiInstanceHelper::print("answered held=" + QByteArray::number(held) + " opened=" + QByteArray::number(m_openRequested));
        if (QPushButton* button = dialog->getChoiceButton(m_answer))
        {
            button->click();
        }
    }

    void arm(const QString& answer, bool isWaitingForHeld)
    {
        m_answer = answer == QLatin1String("discard") ? pdf::PDFAppDocumentChoice::Discard : pdf::PDFAppDocumentChoice::Cancel;
        m_isWaitingForHeld = isWaitingForHeld;

        // The answer waits for the held request of a starting process, which may
        // connect late under load (the wait ends, when the request is held)
        m_answerDeadline = QDeadlineTimer(MultiInstanceHelper::timeout(30000));
        m_answerTimer.start();
    }

    void handle(const QByteArray& line)
    {
        const QList<QByteArray> parts = line.split('\t');
        const QByteArray id = parts.value(0);
        QStringList arguments;
        for (qsizetype i = 1; i < parts.size(); ++i)
        {
            arguments << QString::fromUtf8(QByteArray::fromPercentEncoding(parts[i]));
        }
        const QString command = arguments.value(0);
        QJsonObject result;

        if (command == QLatin1String("state"))
        {
            result = getState();
        }
        else if (command == QLatin1String("set"))
        {
            const QVariant value = QJsonDocument::fromJson("[" + arguments.value(2).toUtf8() + "]").array().at(0).toVariant();
            result[QStringLiteral("error")] = errorText(m_context->getSettings().setValue(arguments.value(1), value));
            result[QStringLiteral("time")] = double(MultiInstanceHelper::now());
        }
        else if (command == QLatin1String("get"))
        {
            result[QStringLiteral("value")] = QJsonValue::fromVariant(m_context->getSettings().getValue(arguments.value(1)));
        }
        else if (command == QLatin1String("restore"))
        {
            result[QStringLiteral("error")] = errorText(m_context->getSettings().restoreDefaults(arguments.value(1)));
            result[QStringLiteral("time")] = double(MultiInstanceHelper::now());
        }
        else if (command == QLatin1String("export"))
        {
            result[QStringLiteral("error")] = errorText(m_context->getDatabase()->exportBackup(arguments.value(1)));
        }
        else if (command == QLatin1String("import"))
        {
            const pdf::PDFDatabaseBackup backup = pdf::PDFDatabaseBackup::read(arguments.value(1));
            result[QStringLiteral("error")] = errorText(m_context->getDatabase()->importBackup(backup, pdf::PDFDatabaseImportCertificates::KeepCurrent));
            result[QStringLiteral("time")] = double(MultiInstanceHelper::now());
        }
        else if (command == QLatin1String("shortcut"))
        {
            result[QStringLiteral("error")] = errorText(m_context->getShortcuts().setShortcut(arguments.value(1), arguments.value(2)));
        }
        else if (command == QLatin1String("action"))
        {
            QAction* action = m_window ? m_window->getCommandAction(arguments.value(1)) : nullptr;
            result[QStringLiteral("shortcut")] = action ? action->shortcut().toString(QKeySequence::PortableText) : QString();
        }
        else if (command == QLatin1String("recent-open"))
        {
            QString storedPath;
            result[QStringLiteral("error")] = errorText(m_context->getRecent().recordOpened(arguments.value(1), &storedPath));
            result[QStringLiteral("path")] = storedPath;
        }
        else if (command == QLatin1String("recent-pin"))
        {
            result[QStringLiteral("error")] = errorText(m_context->getRecent().setPinned(arguments.value(1), arguments.value(2) == QLatin1String("1")));
        }
        else if (command == QLatin1String("recent-list"))
        {
            QJsonArray paths;
            for (const pdf::PDFRecentFile& file : m_context->getRecent().getFiles())
            {
                paths.append(file.path);
            }
            result[QStringLiteral("paths")] = paths;
        }
        else if (command == QLatin1String("workspace-save"))
        {
            pdf::PDFWorkspaceDocument document;
            document.path = arguments.value(2);
            document.page = 1;
            result[QStringLiteral("error")] = errorText(m_context->getWorkspaces().saveWorkspace(arguments.value(1), { document }, false));
        }
        else if (command == QLatin1String("tool-pin"))
        {
            result[QStringLiteral("error")] = errorText(m_context->getTools().setPinned(arguments.value(1), arguments.value(2) == QLatin1String("1")));
        }
        else if (command == QLatin1String("bookmark-add"))
        {
            result[QStringLiteral("error")] = errorText(pdf::PDFBookmarkRepository(m_context->getDatabase()).addBookmark(arguments.value(1), arguments.value(2).toLongLong(), arguments.value(3)));
        }
        else if (command == QLatin1String("bookmarks-add"))
        {
            // More bookmarks, each one its own transaction (the test waits for
            // "writing" on the standard output)
            MultiInstanceHelper::print("writing " + arguments.value(2).toUtf8());
            QElapsedTimer timer;
            timer.start();
            int errors = 0;
            const int count = arguments.value(3).toInt();
            pdf::PDFBookmarkRepository repository(m_context->getDatabase());
            for (int i = 0; i < count; ++i)
            {
                if (repository.addBookmark(arguments.value(1), i, QStringLiteral("%1-%2").arg(arguments.value(2)).arg(i)).isError())
                {
                    ++errors;
                }
            }
            result[QStringLiteral("errors")] = errors;
            result[QStringLiteral("elapsed")] = double(timer.elapsed());
        }
        else if (command == QLatin1String("bookmark-list"))
        {
            QJsonArray titles;
            for (const pdf::PDFBookmark& bookmark : pdf::PDFBookmarkRepository(m_context->getDatabase()).getBookmarks(arguments.value(1)))
            {
                titles.append(bookmark.title);
            }
            result[QStringLiteral("titles")] = titles;
        }
        else if (command == QLatin1String("hold-transaction"))
        {
            // A write transaction with uncommitted rows, which is never committed:
            // the test kills the process inside of it
            pdf::PDFDatabaseConnection* connection = m_context->getDatabase()->getConnection();
            pdf::PDFDatabaseTransaction* transaction = new pdf::PDFDatabaseTransaction(connection);
            pdf::PDFBookmarkRepository repository(m_context->getDatabase());
            int errors = 0;
            for (int i = 0; i < arguments.value(3).toInt(); ++i)
            {
                if (repository.addBookmark(arguments.value(1), i, QStringLiteral("%1-%2").arg(arguments.value(2)).arg(i)).isError())
                {
                    ++errors;
                }
            }
            result[QStringLiteral("active")] = transaction->isActive() && connection->isInTransaction();
            result[QStringLiteral("errors")] = errors;
            reply(id, result);
            m_socket->waitForBytesWritten(5000);

            const QDeadlineTimer deadline(MultiInstanceHelper::timeout(120000));
            while (!deadline.hasExpired())
            {
                QThread::msleep(20);
            }
            std::_Exit(70);
        }
        else if (command == QLatin1String("navigate"))
        {
            pdf::PDFAppTab* tab = m_window->getTabs().value(arguments.value(1).toInt());
            if (tab)
            {
                m_window->activateTab(tab);
                m_window->getNavigator()->navigate(pdf::PDFAppRoute::parse(arguments.value(2)), pdf::PDFAppNavigationMode::Push, tab);
            }
            result[QStringLiteral("ok")] = tab != nullptr;
        }
        else if (command == QLatin1String("activate-tab"))
        {
            pdf::PDFAppTab* tab = m_window->getTabs().value(arguments.value(1).toInt());
            if (tab)
            {
                m_window->activateTab(tab);
            }
            result[QStringLiteral("ok")] = tab != nullptr;
        }
        else if (command == QLatin1String("new-tab"))
        {
            result[QStringLiteral("ok")] = m_window->openNewTab() != nullptr;
        }
        else if (command == QLatin1String("open"))
        {
            m_window->openDocuments(QStringList({ arguments.value(1) }), 0);
            const bool isReady = QTest::qWaitFor([this, &arguments]()
            {
                pdf::PDFAppDocumentTab* tab = findDocumentTab(arguments.value(1));
                return tab && tab->getDocument()->isReady();
            }, MultiInstanceHelper::timeout(30000));
            result[QStringLiteral("ok")] = isReady;
        }
        else if (command == QLatin1String("make-unsaved"))
        {
            // Edit a Copy of an opened file: a document with unsaved changes
            m_window->openDocuments(QStringList({ arguments.value(1) }), 0);
            bool isReady = QTest::qWaitFor([this, &arguments]()
            {
                pdf::PDFAppDocumentTab* tab = findDocumentTab(arguments.value(1));
                return tab && tab->getDocument()->isReady();
            }, MultiInstanceHelper::timeout(30000));
            QPointer<pdf::PDFAppDocumentTab> copy = isReady ? m_window->openDocumentCopy(findDocumentTab(arguments.value(1))->getDocument()) : nullptr;
            isReady = copy && QTest::qWaitFor([&copy]()
            {
                return !copy || (copy->getDocument() && copy->getDocument()->isReady() && copy->getDocument()->isModified());
            }, MultiInstanceHelper::timeout(30000)) && copy;
            result[QStringLiteral("ok")] = isReady;
        }
        else if (command == QLatin1String("page"))
        {
            pdf::PDFAppTab* tab = arguments.size() > 1 ? m_window->getTabs().value(arguments.value(1).toInt()) : m_window->getCurrentTab();
            result = getPageInfo(tab);
        }
        else if (command == QLatin1String("bookmarks-panel"))
        {
            QJsonArray titles;
            if (pdf::PDFAppDocumentTab* tab = findDocumentTab(arguments.value(1)))
            {
                m_window->activateTab(tab);
                pdf::PDFAppDocumentSidebar* sidebar = tab->getView() ? tab->getView()->getSidebar() : nullptr;
                if (sidebar && sidebar->showPanel(QStringLiteral("bookmarks")))
                {
                    if (pdf::PDFAppBookmarksPanel* panel = qobject_cast<pdf::PDFAppBookmarksPanel*>(sidebar->findPanel(QStringLiteral("bookmarks"))))
                    {
                        for (const pdf::PDFAppBookmarkItem& item : panel->getModel()->getItems())
                        {
                            titles.append(QStringLiteral("%1@%2").arg(item.title).arg(item.page));
                        }
                    }
                }
            }
            result[QStringLiteral("titles")] = titles;
        }
        else if (command == QLatin1String("ai"))
        {
            pdf::PDFAppAiPage* page = m_window->getCurrentTab() ? qobject_cast<pdf::PDFAppAiPage*>(m_window->getCurrentTab()->getPage()) : nullptr;
            QAbstractButton* button = nullptr;
            if (page && arguments.value(1) == QLatin1String("start"))
            {
                button = page->getStartStopButton();
            }
            else if (page && arguments.value(1) == QLatin1String("callout"))
            {
                button = page->getCalloutButton();
            }
            else if (page && arguments.value(1) == QLatin1String("disconnect-first") && !page->getAssistantRows().isEmpty())
            {
                button = page->getAssistantRows().front()->getDisconnectButton();
            }
            result[QStringLiteral("clicked")] = button && button->isEnabled() && button->isVisibleTo(page);
            if (result[QStringLiteral("clicked")].toBool())
            {
                button->click();
            }
        }
        else if (command == QLatin1String("geometry"))
        {
            const QRect rect(arguments.value(1).toInt(), arguments.value(2).toInt(), arguments.value(3).toInt(), arguments.value(4).toInt());
            m_window->setGeometry(rect);
            result[QStringLiteral("placed")] = QTest::qWaitFor([this, rect]() { return !m_window || m_window->geometry() == rect; }, MultiInstanceHelper::timeout(5000));
            result[QStringLiteral("geometry")] = MultiInstanceHelper::toArray(m_window ? m_window->geometry() : QRect());
        }
        else if (command == QLatin1String("close"))
        {
            // The window closed by the user; the answer of the question about
            // unsaved changes (quit-unsaved) is given, when it is asked
            arm(arguments.value(1), false);
            QPointer<MultiChildAgent> guard(this);
            const bool isAccepted = m_window && m_window->close();
            if (!guard)
            {
                return;
            }
            m_answerTimer.stop();
            result[QStringLiteral("accepted")] = isAccepted;
        }
        else if (command == QLatin1String("freeze-close"))
        {
            // The event loop does not run until the marker file exists (a process
            // connects to the forwarding meanwhile), then the window is closed and
            // the question about unsaved changes is answered, when the request of
            // the connection is held
            result[QStringLiteral("frozen")] = true;
            reply(id, result);
            m_socket->waitForBytesWritten(5000);

            const QDeadlineTimer deadline(MultiInstanceHelper::timeout(60000));
            while (!QFile::exists(arguments.value(1)) && !deadline.hasExpired())
            {
                QThread::msleep(5);
            }
            arm(arguments.value(2), true);
            if (m_window)
            {
                m_window->close();
            }
            return;
        }
        else if (command == QLatin1String("freeze"))
        {
            // The process hangs (its event loop does not run) until the marker file exists
            result[QStringLiteral("frozen")] = true;
            reply(id, result);
            m_socket->waitForBytesWritten(5000);

            const QDeadlineTimer deadline(MultiInstanceHelper::timeout(120000));
            while (!QFile::exists(arguments.value(1)) && !deadline.hasExpired())
            {
                QThread::msleep(5);
            }
            return;
        }
        else if (command == QLatin1String("quit"))
        {
            result[QStringLiteral("ok")] = true;
            reply(id, result);
            m_socket->waitForBytesWritten(5000);
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
        else
        {
            result[QStringLiteral("unknownCommand")] = command;
        }

        reply(id, result);
    }

    QPointer<pdf::PDFAppContext> m_context;
    QPointer<pdf::PDFAppMainWindow> m_window;
    QString m_guard;
    QPointer<QLocalSocket> m_socket;
    QByteArray m_buffer;
    QJsonObject m_notified;
    QTimer m_answerTimer;
    QDeadlineTimer m_answerDeadline;
    pdf::PDFAppDocumentChoice m_answer = pdf::PDFAppDocumentChoice::Cancel;
    bool m_isWaitingForHeld = false;
    bool m_isAnswered = false;
    int m_answeredHeld = 0;
    int m_answeredOpened = 0;
    int m_openRequested = 0;
    int m_everythingChanged = 0;
    int m_showWindowRequested = 0;
};

/// The child mode: the start of main.cpp of PDF4QT (the instance guard, the
/// context, the window) with the control connection of the test
class MultiChild
{
public:
    MultiChild() = delete;

    static int run(int argc, char* argv[])
    {
        QApplication application(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("MelkaJ"));
        QCoreApplication::setApplicationName(QStringLiteral("PDF4QT"));

        const QStringList arguments = QCoreApplication::arguments();
        const QString controlName = arguments.value(2);
        const QString name = arguments.value(3);
        QStringList applicationArguments({ arguments.value(0) });
        applicationArguments << arguments.mid(4);
        const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse(applicationArguments, QDir::currentPath());

        // Decision 11 as in main.cpp
        QString guard = QStringLiteral("none");
        std::unique_ptr<pdf::PDFAppInstanceServer> instanceServer;
        if (commandLine.isListeningAllowed())
        {
            instanceServer = std::make_unique<pdf::PDFAppInstanceServer>();
            const pdf::PDFAppInstanceRequest request = pdf::PDFAppInstanceProtocol::createRequest(commandLine, QString());
            const pdf::PDFAppInstanceGuard::Mode mode = commandLine.isForwardingAllowed() ? pdf::PDFAppInstanceGuard::Mode::Forward
                                                                                           : pdf::PDFAppInstanceGuard::Mode::ListenIfFree;
            MultiInstanceHelper::print("guard-start");
            QString error;
            switch (pdf::PDFAppInstanceGuard::start(pdf::PDFAppInstanceGuard::getServerName(), request, instanceServer.get(),
                                                    pdf::PDFAppInstanceClient::DEFAULT_TIMEOUT, &error, mode))
            {
                case pdf::PDFAppInstanceGuard::Result::Forwarded:
                    MultiInstanceHelper::print("guard forwarded");
                    return 0;

                case pdf::PDFAppInstanceGuard::Result::Listening:
                    guard = QStringLiteral("listening");
                    break;

                case pdf::PDFAppInstanceGuard::Result::Standalone:
                    guard = QStringLiteral("standalone");
                    instanceServer.reset();
                    break;
            }
        }
        MultiInstanceHelper::print("guard " + guard.toUtf8());

        QApplication::setStyle(new pdf::PDFWidgetStyle());

        int exitCode = 0;
        {
            pdf::PDFAppContext context(commandLine);
            context.setInstanceServer(instanceServer.release());
            context.open();

            bool isIntervalValid = false;
            const int heartbeat = qEnvironmentVariableIntValue("PDF4QT_MULTI_HEARTBEAT", &isIntervalValid);
            if (isIntervalValid && context.getMcpController())
            {
                context.getMcpController()->setHeartbeatInterval(heartbeat);
            }

            std::unique_ptr<QWidget> window(pdf::PDFAppWindowFactory::createMainWindow(&context));
            window->show();

            MultiChildAgent agent(&context, qobject_cast<pdf::PDFAppMainWindow*>(window.get()), guard);
            if (!agent.connectControl(controlName, name))
            {
                MultiInstanceHelper::print("control-failed");
                return 50;
            }

            exitCode = application.exec();
            agent.finish();
            window.reset();
        }
        MultiInstanceHelper::print("exit");
        return exitCode;
    }
};

/// One child instance (or the executable PDF4QT) seen from the test
class MultiInstance
{
public:
    QString name;
    std::unique_ptr<QProcess> process;
    QLocalSocket* socket = nullptr;
    QByteArray buffer;
    QByteArray output;
    std::map<int, QJsonObject> replies;
    int nextId = 0;
    QString guard;
    qint64 pid = 0;

    bool isRunning() const { return process && process->state() != QProcess::NotRunning; }

    /// The standard output of the process so far
    const QByteArray& readOutput()
    {
        if (process)
        {
            output += process->readAllStandardOutput();
        }
        return output;
    }

    bool waitForOutput(const QByteArray& text, int timeout)
    {
        QDeadlineTimer deadline(timeout);
        while (!readOutput().contains(text) && !deadline.hasExpired())
        {
            if (!isRunning())
            {
                return readOutput().contains(text);
            }
            process->waitForReadyRead(20);
        }
        return readOutput().contains(text);
    }

    int send(const QStringList& command)
    {
        const int id = ++nextId;
        QByteArray line = QByteArray::number(id);
        for (const QString& part : command)
        {
            line += '\t' + part.toUtf8().toPercentEncoding();
        }
        line += '\n';
        if (socket)
        {
            socket->write(line);
            socket->flush();
            socket->waitForBytesWritten(1000);
        }
        return id;
    }

    QJsonObject wait(int id, int timeout)
    {
        QDeadlineTimer deadline(timeout);
        while (true)
        {
            auto it = replies.find(id);
            if (it != replies.end())
            {
                const QJsonObject reply = it->second;
                replies.erase(it);
                return reply;
            }
            if (!socket || socket->state() != QLocalSocket::ConnectedState || deadline.hasExpired())
            {
                QJsonObject failure;
                failure[QStringLiteral("failure")] = deadline.hasExpired() ? QStringLiteral("timeout") : QStringLiteral("disconnected");
                return failure;
            }
            socket->waitForReadyRead(20);
            buffer += socket->readAll();
            qsizetype index = buffer.indexOf('\n');
            while (index >= 0)
            {
                const QByteArray line = buffer.left(index);
                buffer.remove(0, index + 1);
                const qsizetype separator = line.indexOf('\t');
                replies[line.left(separator).toInt()] = QJsonDocument::fromJson(line.mid(separator + 1)).object();
                index = buffer.indexOf('\n');
            }
            readOutput();
        }
    }

    QJsonObject call(const QStringList& command, int timeout = -1)
    {
        return wait(send(command), timeout >= 0 ? timeout : MultiInstanceHelper::timeout(30000));
    }

    QJsonObject state() { return call({ QStringLiteral("state") }); }
    QJsonObject page(int tab = -1) { return tab >= 0 ? call({ QStringLiteral("page"), QString::number(tab) }) : call({ QStringLiteral("page") }); }
    QJsonObject mcp() { return state().value(QStringLiteral("mcp")).toObject(); }

    QStringList documents()
    {
        QStringList paths;
        for (const QJsonValue& value : state().value(QStringLiteral("documents")).toArray())
        {
            paths << value.toObject().value(QStringLiteral("path")).toString();
        }
        return paths;
    }

    QStringList recentPaths() { return MultiInstanceHelper::toStringList(call({ QStringLiteral("recent-list") }).value(QStringLiteral("paths"))); }

    /// Time of the last notification of the topic / key (0 = none)
    qint64 notifiedAt(const QString& key) { return qint64(state().value(QStringLiteral("notified")).toObject().value(key).toDouble()); }
};

/// The instances of one test: a data folder, a name of the forwarding, the
/// control server; every process is ended by end()
class MultiFixture
{
public:
    MultiFixture() :
        m_serverName(QStringLiteral("pdf4qt-multi-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter)),
        m_controlName(QStringLiteral("pdf4qt-multi-control-%1-%2").arg(QCoreApplication::applicationPid()).arg(s_counter))
    {
        dataDirectory = root.filePath(QStringLiteral("data"));
        QLocalServer::removeServer(m_controlName);
        m_control.listen(m_controlName);
        pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&m_registry);
    }

    ~MultiFixture()
    {
        end();
        if (database)
        {
            database->close();
        }
    }

    MultiFixture(const MultiFixture&) = delete;
    MultiFixture& operator=(const MultiFixture&) = delete;

    const QString& getServerName() const { return m_serverName; }
    bool isControlListening() const { return m_control.isListening(); }

    QProcessEnvironment environment() const
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PDF4QT_APP_DATA_DIRECTORY"), dataDirectory);
        environment.insert(QStringLiteral("PDF4QT_APP_INSTANCE_NAME"), m_serverName);
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        return environment;
    }

    /// Starts a child instance; connect() waits for its control connection
    MultiInstance* launch(const QString& name, const QStringList& arguments, const QStringList& extraEnvironment = QStringList())
    {
        auto instance = std::make_unique<MultiInstance>();
        instance->name = name;
        instance->process = std::make_unique<QProcess>();
        instance->process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QProcessEnvironment processEnvironment = environment();
        for (const QString& item : extraEnvironment)
        {
            processEnvironment.insert(item.section(QLatin1Char('='), 0, 0), item.section(QLatin1Char('='), 1));
        }
        instance->process->setProcessEnvironment(processEnvironment);
        instance->process->setWorkingDirectory(root.path());
        QStringList processArguments({ QStringLiteral("--multi-child"), m_controlName, name });
        processArguments << arguments;
        instance->process->start(QCoreApplication::applicationFilePath(), processArguments);
        instances.push_back(std::move(instance));
        return instances.back().get();
    }

    /// Starts the executable PDF4QT
    MultiInstance* launchExecutable(const QString& name, const QStringList& arguments)
    {
        auto instance = std::make_unique<MultiInstance>();
        instance->name = name;
        instance->process = std::make_unique<QProcess>();
        instance->process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QProcessEnvironment processEnvironment = environment();

        // A safety net: an executable, which does not forward, quits later
        processEnvironment.insert(QStringLiteral("PDF4QT_APP_QUIT_AFTER_MS"), QString::number(MultiInstanceHelper::timeout(120000)));
        instance->process->setProcessEnvironment(processEnvironment);
        instance->process->setWorkingDirectory(root.path());
        instance->process->start(MultiInstanceHelper::getExecutablePath(), arguments);
        instances.push_back(std::move(instance));
        return instances.back().get();
    }

    /// Waits for the hello of the control connection of the instance
    bool connect(MultiInstance* instance, int timeout)
    {
        QDeadlineTimer deadline(timeout);
        while (!deadline.hasExpired())
        {
            auto it = m_hello.find(instance->name);
            if (it != m_hello.end())
            {
                instance->socket = it->second.socket;
                instance->pid = it->second.pid;
                instance->guard = it->second.guard;
                m_hello.erase(it);
                return true;
            }

            if (!instance->isRunning())
            {
                instance->readOutput();
                qWarning("Instance %s ended: %s", qPrintable(instance->name), instance->output.constData());
                return false;
            }

            m_control.waitForNewConnection(20);
            while (QLocalSocket* socket = m_control.nextPendingConnection())
            {
                m_sockets.push_back(socket);
            }
            for (QLocalSocket* socket : m_sockets)
            {
                readHello(socket);
            }
        }
        return false;
    }

    MultiInstance* start(const QString& name, const QStringList& arguments = QStringList(), const QStringList& extraEnvironment = QStringList())
    {
        MultiInstance* instance = launch(name, arguments, extraEnvironment);
        return connect(instance, MultiInstanceHelper::timeout(60000)) ? instance : nullptr;
    }

    static bool waitForExit(MultiInstance* instance, int timeout)
    {
        const bool isFinished = !instance->isRunning() || instance->process->waitForFinished(timeout);
        instance->readOutput();
        return isFinished;
    }

    /// Kills every process, which still runs. \returns true, when no process runs then.
    bool end()
    {
        bool isEnded = true;
        for (const std::unique_ptr<MultiInstance>& instance : instances)
        {
            if (instance->isRunning())
            {
                instance->process->kill();
                instance->process->waitForFinished(MultiInstanceHelper::timeout(30000));
            }
            isEnded = isEnded && !instance->isRunning();
        }
        return isEnded;
    }

    /// Opens the database of the data folder in the test process (an observer of
    /// the shared file, as another instance)
    bool openDatabase()
    {
        database = std::make_unique<pdf::PDFDatabase>();
        pdf::PDFDatabaseOptions options;
        options.directory = dataDirectory;
        database->open(options);
        return database->getState().mode == pdf::PDFDatabaseStorageMode::Disk;
    }

    pdf::PDFSettingsRepository settings() const { return pdf::PDFSettingsRepository(database.get(), &m_registry); }
    pdf::PDFRecentRepository recent() const { return pdf::PDFRecentRepository(database.get(), &m_registry); }

    QString getDatabaseFileName() const { return pdf::PDFDatabaseFiles::getFileName(pdf::PDFDatabaseSchema::getCurrentVersion()); }

    /// A sample PDF of the root folder (canonical path)
    QString sample(const QString& name, int pageCount = 3)
    {
        const QString path = AppDocumentSamples::simple(root.path(), name, pageCount);
        return QFileInfo(path).canonicalFilePath();
    }

    QTemporaryDir root;
    QString dataDirectory;
    std::vector<std::unique_ptr<MultiInstance>> instances;
    std::unique_ptr<pdf::PDFDatabase> database;

private:
    struct Hello
    {
        QLocalSocket* socket = nullptr;
        qint64 pid = 0;
        QString guard;
    };

    void readHello(QLocalSocket* socket)
    {
        if (socket->property("hello").toBool())
        {
            return;
        }
        socket->waitForReadyRead(0);
        QByteArray data = socket->property("helloBuffer").toByteArray() + socket->readAll();
        const qsizetype index = data.indexOf('\n');
        if (index < 0)
        {
            socket->setProperty("helloBuffer", data);
            return;
        }
        const QList<QByteArray> parts = data.left(index).split('\t');
        socket->setProperty("hello", true);
        if (parts.value(0) == "hello")
        {
            Hello hello;
            hello.socket = socket;
            hello.pid = parts.value(2).toLongLong();
            hello.guard = QString::fromUtf8(parts.value(3));
            m_hello[QString::fromUtf8(parts.value(1))] = hello;
        }
    }

    QString m_serverName;
    QString m_controlName;
    QLocalServer m_control;
    std::vector<QLocalSocket*> m_sockets;
    std::map<QString, Hello> m_hello;
    pdf::PDFSettingRegistry m_registry;
    static int s_counter;
};

int MultiFixture::s_counter = 0;

}   // namespace pdftest

using pdftest::MultiFixture;
using pdftest::MultiInstance;
using pdftest::MultiInstanceHelper;

class AppMultiInstanceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void concurrentStart_data();
    void concurrentStart();
    void settingsFollowOtherInstance();
    void pagesFollowOtherInstance();
    void forwarding();
    void quitWithRequestInFlight_data();
    void quitWithRequestInFlight();
    void mcpServerInOneInstance();
    void mcpOwnerHangs_data();
    void mcpOwnerHangs();
    void windowGeometryLastClose();
    void portableInstance();
    void crashDuringWrite();

private:
    std::unique_ptr<MultiFixture> m_fixture;
};

void AppMultiInstanceTest::init()
{
    m_fixture = std::make_unique<MultiFixture>();
    QVERIFY(m_fixture->root.isValid());
    QVERIFY(m_fixture->isControlListening());
}

void AppMultiInstanceTest::cleanup()
{
    // Every process of the test has ended (no orphan process): the children are
    // killed, when the test failed before it ended them
    const bool isEnded = m_fixture->end();
    m_fixture.reset();
    QVERIFY(isEnded);
}

void AppMultiInstanceTest::concurrentStart_data()
{
    QTest::addColumn<int>("count");
    QTest::newRow("2") << 2;
    QTest::newRow("3") << 3;
    QTest::newRow("4") << 4;
}

void AppMultiInstanceTest::concurrentStart()
{
    // Instances started at the same time on an empty data folder (newapp_storage.md
    // 1, decision 11): one database file of the schema version, which all of them
    // use, exactly one of them holds the name of the forwarding, all of them work
    QFETCH(int, count);
    MultiFixture& fixture = *m_fixture;
    QVERIFY(!QDir(fixture.dataDirectory).exists());

    std::vector<MultiInstance*> instances;
    for (int i = 0; i < count; ++i)
    {
        instances.push_back(fixture.launch(QStringLiteral("instance-%1").arg(i), QStringList()));
    }
    for (MultiInstance* instance : instances)
    {
        QVERIFY2(fixture.connect(instance, MultiInstanceHelper::timeout(60000)), qPrintable(instance->name));
    }

    const QString databaseFile = QDir(fixture.dataDirectory).filePath(fixture.getDatabaseFileName());
    QCOMPARE(QDir(fixture.dataDirectory).entryList(QStringList({ QStringLiteral("*.db") }), QDir::Files), QStringList({ fixture.getDatabaseFileName() }));

    MultiInstance* owner = nullptr;
    int listeningCount = 0;
    for (MultiInstance* instance : instances)
    {
        const QJsonObject state = instance->state();
        QCOMPARE(state.value(QStringLiteral("storageMode")).toString(), QStringLiteral("disk"));
        QCOMPARE(QFileInfo(state.value(QStringLiteral("databaseFile")).toString()).canonicalFilePath(), QFileInfo(databaseFile).canonicalFilePath());
        QVERIFY(state.value(QStringLiteral("damagedFile")).toString().isEmpty());
        QVERIFY(state.value(QStringLiteral("visible")).toBool());
        if (instance->guard == QLatin1String("listening"))
        {
            ++listeningCount;
            owner = instance;
            QVERIFY(state.value(QStringLiteral("listening")).toBool());
        }
        else
        {
            QCOMPARE(instance->guard, QStringLiteral("standalone"));
            QVERIFY(!state.value(QStringLiteral("listening")).toBool());
        }
    }
    QCOMPARE(listeningCount, 1);

    // A file forwarded to the name opens in the owner only
    const QString forwardedFile = fixture.sample(QStringLiteral("forwarded.pdf"));
    pdf::PDFAppInstanceRequest request;
    request.files << forwardedFile;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceClient::send(fixture.getServerName(), request, MultiInstanceHelper::timeout(5000), &error), pdf::PDFAppInstanceClient::Result::Delivered);
    QTRY_VERIFY_WITH_TIMEOUT(owner->documents().contains(forwardedFile), MultiInstanceHelper::timeout(30000));
    for (MultiInstance* instance : instances)
    {
        QCOMPARE(instance->state().value(QStringLiteral("openRequested")).toInt(), instance == owner ? 1 : 0);
        if (instance != owner)
        {
            QVERIFY(instance->documents().isEmpty());
        }
    }

    // Every instance writes, every instance reads the writes of all of them
    QStringList storedPaths;
    for (int i = 0; i < count; ++i)
    {
        const QJsonObject reply = instances[i]->call({ QStringLiteral("recent-open"), fixture.root.filePath(QStringLiteral("written-%1.pdf").arg(i)) });
        QVERIFY2(reply.value(QStringLiteral("error")).toString().isEmpty(), qPrintable(reply.value(QStringLiteral("error")).toString()));
        storedPaths << reply.value(QStringLiteral("path")).toString();
    }
    for (MultiInstance* instance : instances)
    {
        const QStringList paths = instance->recentPaths();
        for (const QString& path : storedPaths)
        {
            QVERIFY2(paths.contains(path), qPrintable(instance->name + QStringLiteral(": ") + path));
        }
    }
}

void AppMultiInstanceTest::settingsFollowOtherInstance()
{
    // newapp_storage.md: nothing is cached - a setting changed in one instance is
    // applied in the others without a restart; the notification of another
    // process comes within a second (the poll of PRAGMA data_version)
    MultiFixture& fixture = *m_fixture;
    MultiInstance* writer = fixture.start(QStringLiteral("writer"));
    QVERIFY(writer);
    MultiInstance* reader = fixture.start(QStringLiteral("reader"));
    QVERIFY(reader);
    const int latencyLimit = MultiInstanceHelper::timeout(1000);

    const QJsonObject initialState = reader->state();
    const QString defaultTheme = initialState.value(QStringLiteral("theme")).toString();
    QCOMPARE(initialState.value(QStringLiteral("language")).toString(), pdf::PDFSettingValues::languageEnglish());
    const QString otherScheme = defaultTheme == QLatin1String("dark") ? QStringLiteral("light") : QStringLiteral("dark");

    // The color scheme
    QJsonObject written = writer->call({ QStringLiteral("set"), pdf::PDFSettingKeys::colorScheme(), QStringLiteral("\"%1\"").arg(otherScheme) });
    QVERIFY(written.value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("theme")).toString(), otherScheme, MultiInstanceHelper::timeout(10000));
    QCOMPARE(writer->state().value(QStringLiteral("theme")).toString(), otherScheme);
    const qint64 schemeLatency = reader->notifiedAt(QStringLiteral("setting/") + pdf::PDFSettingKeys::colorScheme()) - qint64(written.value(QStringLiteral("time")).toDouble());
    QVERIFY2(schemeLatency <= latencyLimit, qPrintable(QString::number(schemeLatency)));

    // A shortcut of a command: the action of the window of the other instance
    const QString command = QStringLiteral("window.newTab");
    const QString keys = QStringLiteral("Ctrl+Alt+F9");
    QVERIFY(reader->call({ QStringLiteral("action"), command }).value(QStringLiteral("shortcut")).toString() != keys);
    QVERIFY(writer->call({ QStringLiteral("shortcut"), command, keys }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->call({ QStringLiteral("action"), command }).value(QStringLiteral("shortcut")).toString(), keys, MultiInstanceHelper::timeout(10000));

    // The language of the user interface (switched at once, Q2 of step 5)
    written = writer->call({ QStringLiteral("set"), pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("\"de\"") });
    QVERIFY(written.value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("language")).toString(), QStringLiteral("de"), MultiInstanceHelper::timeout(10000));
    const qint64 languageLatency = reader->notifiedAt(QStringLiteral("setting/") + pdf::PDFSettingKeys::appearanceLanguage()) - qint64(written.value(QStringLiteral("time")).toDouble());
    QVERIFY2(languageLatency <= latencyLimit, qPrintable(QString::number(languageLatency)));

    // A permission of MCP: the host of every instance gets it (a new catalog)
    QCOMPARE(reader->mcp().value(QStringLiteral("edit")).toString(), pdf::PDFSettingValues::permissionAsk());
    written = writer->call({ QStringLiteral("set"), pdf::PDFSettingKeys::mcpPermissionEdit(), QStringLiteral("\"%1\"").arg(pdf::PDFSettingValues::permissionDeny()) });
    QVERIFY(written.value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->mcp().value(QStringLiteral("edit")).toString(), pdf::PDFSettingValues::permissionDeny(), MultiInstanceHelper::timeout(10000));

    // Restore defaults of General: the scheme and the language of the other
    // instance are the defaults again, the settings of other sections stay
    QVERIFY(writer->call({ QStringLiteral("restore"), pdf::PDFSettingDefinitions::sectionGeneral() }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("theme")).toString(), defaultTheme, MultiInstanceHelper::timeout(10000));
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("language")).toString(), pdf::PDFSettingValues::languageEnglish(), MultiInstanceHelper::timeout(10000));
    QCOMPARE(reader->mcp().value(QStringLiteral("edit")).toString(), pdf::PDFSettingValues::permissionDeny());
    QCOMPARE(reader->call({ QStringLiteral("action"), command }).value(QStringLiteral("shortcut")).toString(), keys);

    // An imported backup is everythingChanged in the other instance: the backup
    // has the other scheme, the current state the default one
    QVERIFY(writer->call({ QStringLiteral("set"), pdf::PDFSettingKeys::colorScheme(), QStringLiteral("\"%1\"").arg(otherScheme) }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("theme")).toString(), otherScheme, MultiInstanceHelper::timeout(10000));
    const QString backupPath = fixture.root.filePath(QStringLiteral("state.pdf4qt-backup"));
    QVERIFY(writer->call({ QStringLiteral("export"), backupPath }).value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(writer->call({ QStringLiteral("restore"), pdf::PDFSettingDefinitions::sectionGeneral() }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("theme")).toString(), defaultTheme, MultiInstanceHelper::timeout(10000));

    const int everythingChanged = reader->state().value(QStringLiteral("everythingChanged")).toInt();
    written = writer->call({ QStringLiteral("import"), backupPath });
    QVERIFY2(written.value(QStringLiteral("error")).toString().isEmpty(), qPrintable(written.value(QStringLiteral("error")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(reader->state().value(QStringLiteral("everythingChanged")).toInt() > everythingChanged, MultiInstanceHelper::timeout(10000));
    QTRY_COMPARE_WITH_TIMEOUT(reader->state().value(QStringLiteral("theme")).toString(), otherScheme, MultiInstanceHelper::timeout(10000));
    const qint64 importLatency = reader->notifiedAt(QStringLiteral("*")) - qint64(written.value(QStringLiteral("time")).toDouble());
    QVERIFY2(importLatency <= latencyLimit, qPrintable(QString::number(importLatency)));
}

void AppMultiInstanceTest::pagesFollowOtherInstance()
{
    // Recent, pins, workspaces, tool pins and bookmarks written by one instance
    // appear in the pages of another one: a shown page at once, a hidden page,
    // when it is shown again (the same page object)
    MultiFixture& fixture = *m_fixture;
    const QString first = fixture.sample(QStringLiteral("First.pdf"));
    const QString second = fixture.sample(QStringLiteral("Second.pdf"));
    MultiInstance* writer = fixture.start(QStringLiteral("writer"));
    QVERIFY(writer);
    MultiInstance* reader = fixture.start(QStringLiteral("reader"));
    QVERIFY(reader);
    const int limit = MultiInstanceHelper::timeout(10000);
    const QString key = QStringLiteral("class");

    // Recent shown in the home tab
    QVERIFY(reader->call({ QStringLiteral("navigate"), QStringLiteral("0"), QStringLiteral("/recent") }).value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(reader->page().value(key).toString(), QStringLiteral("pdf::PDFAppRecentPage"), MultiInstanceHelper::timeout(10000));
    QVERIFY(!reader->page().value(QStringLiteral("files")).toArray().contains(QStringLiteral("First.pdf")));

    const QJsonObject recorded = writer->call({ QStringLiteral("recent-open"), first });
    QVERIFY(recorded.value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(reader->page().value(QStringLiteral("files")).toArray().contains(QStringLiteral("First.pdf")), limit);
    QVERIFY(reader->page().value(QStringLiteral("pinned")).toArray().isEmpty());
    QVERIFY(writer->call({ QStringLiteral("recent-pin"), recorded.value(QStringLiteral("path")).toString(), QStringLiteral("1") }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(reader->page().value(QStringLiteral("pinned")).toArray().contains(QStringLiteral("First.pdf")), limit);
    const QString recentPageId = reader->page().value(QStringLiteral("id")).toString();

    // New tab: Open is shown, Recent of the home tab is hidden
    QVERIFY(reader->call({ QStringLiteral("new-tab") }).value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(reader->page().value(key).toString(), QStringLiteral("pdf::PDFAppOpenPage"), MultiInstanceHelper::timeout(10000));
    QVERIFY(!reader->page(0).value(QStringLiteral("visible")).toBool());

    QVERIFY(writer->call({ QStringLiteral("recent-open"), second }).value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(writer->call({ QStringLiteral("workspace-save"), QStringLiteral("Team review"), first }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(MultiInstanceHelper::toStringList(reader->page().value(QStringLiteral("workspaces"))).contains(QStringLiteral("Team review")), limit);
    QTRY_VERIFY_WITH_TIMEOUT(MultiInstanceHelper::toStringList(reader->page().value(QStringLiteral("files"))).contains(QStringLiteral("Second.pdf")), limit);

    // The hidden Recent page shows the file, when its tab is shown again
    QVERIFY(reader->call({ QStringLiteral("activate-tab"), QStringLiteral("0") }).value(QStringLiteral("ok")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(reader->page().value(QStringLiteral("files")).toArray().contains(QStringLiteral("Second.pdf")), limit);
    QCOMPARE(reader->page().value(QStringLiteral("id")).toString(), recentPageId);
    QVERIFY(reader->page().value(QStringLiteral("pinned")).toArray().contains(QStringLiteral("First.pdf")));

    // Pinned tools of the Tools page
    const QString toolId = QStringLiteral("pages.merge");
    QVERIFY(reader->call({ QStringLiteral("navigate"), QStringLiteral("0"), QStringLiteral("/tools") }).value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(reader->page().value(key).toString(), QStringLiteral("pdf::PDFAppToolsPage"), MultiInstanceHelper::timeout(10000));
    QVERIFY(!MultiInstanceHelper::toStringList(reader->page().value(QStringLiteral("pinnedTools"))).contains(toolId));
    QVERIFY(writer->call({ QStringLiteral("tool-pin"), toolId, QStringLiteral("1") }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(MultiInstanceHelper::toStringList(reader->page().value(QStringLiteral("pinnedTools"))).contains(toolId), limit);
    QVERIFY(writer->call({ QStringLiteral("tool-pin"), toolId, QStringLiteral("0") }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!MultiInstanceHelper::toStringList(reader->page().value(QStringLiteral("pinnedTools"))).contains(toolId), limit);

    // Personal bookmarks of a file open in the other instance
    QVERIFY(reader->call({ QStringLiteral("open"), first }).value(QStringLiteral("ok")).toBool());
    const QString bookmarkKey = QStringLiteral("Chapter mark@1");
    QVERIFY(!MultiInstanceHelper::toStringList(reader->call({ QStringLiteral("bookmarks-panel"), first }).value(QStringLiteral("titles"))).contains(bookmarkKey));
    QVERIFY(writer->call({ QStringLiteral("bookmark-add"), first, QStringLiteral("1"), QStringLiteral("Chapter mark") }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(MultiInstanceHelper::toStringList(reader->call({ QStringLiteral("bookmarks-panel"), first }).value(QStringLiteral("titles"))).contains(bookmarkKey), limit);
}

void AppMultiInstanceTest::forwarding()
{
    // Decision 11: a start with a file forwards it to the instance holding the
    // name (the file opens there, the starting process quits); a start without
    // files and --new-window run their own window and forward nothing
    MultiFixture& fixture = *m_fixture;
    const QString plainFile = fixture.sample(QStringLiteral("Window.pdf"));
    const QString forwardedFile = fixture.sample(QStringLiteral("Forwarded.pdf"));
    MultiInstance* owner = fixture.start(QStringLiteral("owner"));
    QVERIFY(owner);
    QCOMPARE(owner->guard, QStringLiteral("listening"));

    MultiInstance* plain = fixture.start(QStringLiteral("plain"));
    QVERIFY(plain);
    QCOMPARE(plain->guard, QStringLiteral("standalone"));
    QVERIFY(plain->state().value(QStringLiteral("visible")).toBool());

    MultiInstance* newWindow = fixture.start(QStringLiteral("new-window"), { QStringLiteral("--new-window"), plainFile });
    QVERIFY(newWindow);
    QCOMPARE(newWindow->guard, QStringLiteral("standalone"));
    QTRY_COMPARE_WITH_TIMEOUT(newWindow->documents(), QStringList({ plainFile }), MultiInstanceHelper::timeout(30000));
    QCOMPARE(owner->state().value(QStringLiteral("openRequested")).toInt(), 0);
    QVERIFY(owner->documents().isEmpty());
    QVERIFY(owner->state().value(QStringLiteral("listening")).toBool());

    // The executable PDF4QT with a file
    QVERIFY(QFile::exists(MultiInstanceHelper::getExecutablePath()));
    MultiInstance* executable = fixture.launchExecutable(QStringLiteral("executable"), { forwardedFile });
    QVERIFY(MultiFixture::waitForExit(executable, MultiInstanceHelper::timeout(30000)));
    QCOMPARE(executable->process->exitStatus(), QProcess::NormalExit);
    QCOMPARE(executable->process->exitCode(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(owner->documents(), QStringList({ forwardedFile }), MultiInstanceHelper::timeout(30000));
    QCOMPARE(owner->state().value(QStringLiteral("openRequested")).toInt(), 1);
    QCOMPARE(plain->state().value(QStringLiteral("openRequested")).toInt(), 0);
    QVERIFY(plain->documents().isEmpty());
    QCOMPARE(newWindow->documents(), QStringList({ plainFile }));

    // A child starting with a file forwards it too and quits
    MultiInstance* forwarder = fixture.launch(QStringLiteral("forwarder"), { plainFile });
    QVERIFY(MultiFixture::waitForExit(forwarder, MultiInstanceHelper::timeout(30000)));
    QVERIFY(forwarder->output.contains("guard forwarded"));
    QCOMPARE(forwarder->process->exitCode(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(owner->documents().size(), 2, MultiInstanceHelper::timeout(30000));
    QVERIFY(owner->documents().contains(plainFile));
}

void AppMultiInstanceTest::quitWithRequestInFlight_data()
{
    QTest::addColumn<bool>("isAccepted");
    QTest::newRow("quit") << true;
    QTest::newRow("cancel") << false;
}

void AppMultiInstanceTest::quitWithRequestInFlight()
{
    // newapplication.md 3: the owner starts quitting, while the request of a
    // starting process is in flight (connected, not answered). It is held during
    // the question about unsaved changes; an accepted quit refuses it - the
    // process starts by itself with its file; a canceled one opens it - the file
    // is never lost
    QFETCH(bool, isAccepted);
    MultiFixture& fixture = *m_fixture;
    const QString unsavedFile = fixture.sample(QStringLiteral("Unsaved.pdf"));
    const QString forwardedFile = fixture.sample(QStringLiteral("InFlight.pdf"));
    const QString marker = fixture.root.filePath(QStringLiteral("unfreeze.marker"));

    MultiInstance* owner = fixture.start(QStringLiteral("owner"));
    QVERIFY(owner);
    QCOMPARE(owner->guard, QStringLiteral("listening"));
    QVERIFY(owner->call({ QStringLiteral("make-unsaved"), unsavedFile }).value(QStringLiteral("ok")).toBool());

    // The event loop of the owner stops, the starting process connects meanwhile
    QVERIFY(owner->call({ QStringLiteral("freeze-close"), marker, isAccepted ? QStringLiteral("discard") : QStringLiteral("cancel") }).value(QStringLiteral("frozen")).toBool());
    MultiInstance* starter = fixture.launch(QStringLiteral("starter"), { forwardedFile });
    QVERIFY(starter->waitForOutput("guard-start", MultiInstanceHelper::timeout(30000)));

    // No observable moment of the connection inside of the guard: it connects and
    // writes its request at once after it has taken the free lock
    QTest::qWait(MultiInstanceHelper::timeout(300));
    QVERIFY(!starter->readOutput().contains("guard "));
    {
        QFile file(marker);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    if (isAccepted)
    {
        // The owner quits, the starter runs by itself with its file
        QVERIFY(MultiFixture::waitForExit(owner, MultiInstanceHelper::timeout(30000)));
        QCOMPARE(owner->process->exitCode(), 0);
        QVERIFY2(owner->output.contains("answered held=1 opened=0"), owner->output.constData());

        QVERIFY(fixture.connect(starter, MultiInstanceHelper::timeout(60000)));
        QCOMPARE(starter->guard, QStringLiteral("standalone"));
        QTRY_COMPARE_WITH_TIMEOUT(starter->documents(), QStringList({ forwardedFile }), MultiInstanceHelper::timeout(30000));
    }
    else
    {
        // The owner opens the held request and listens again, the starter quits
        QVERIFY(MultiFixture::waitForExit(starter, MultiInstanceHelper::timeout(30000)));
        QVERIFY2(starter->output.contains("guard forwarded"), starter->output.constData());
        QCOMPARE(starter->process->exitCode(), 0);

        const QJsonObject state = owner->state();
        QCOMPARE(state.value(QStringLiteral("answeredHeld")).toInt(), 1);
        QCOMPARE(state.value(QStringLiteral("answeredOpened")).toInt(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(owner->documents().contains(forwardedFile), MultiInstanceHelper::timeout(30000));
        QCOMPARE(owner->state().value(QStringLiteral("openRequested")).toInt(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(owner->state().value(QStringLiteral("listening")).toBool(), MultiInstanceHelper::timeout(30000));
    }
}

void AppMultiInstanceTest::mcpServerInOneInstance()
{
    // newapp_mcp.md 4: the server runs in the instance, which has the port
    // (Start with PDF4QT of another instance does not take it); the AI page of the
    // others shows the other window, Start is disabled; Show Other Window and
    // Disconnect go to the owner by mcp_request; the owner quits - the row is
    // free at once; the owner is killed - its heartbeat older than 10 s enables
    // Start and the others delete its assistants
    MultiFixture& fixture = *m_fixture;
    QVERIFY(fixture.openDatabase());
    const quint16 port = MultiInstanceHelper::findFreePort();
    QVERIFY(port > 0);
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::mcpPort(), int(port)).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::mcpStartWithApp(), true).isError());

    const QStringList environment({ QStringLiteral("PDF4QT_MULTI_HEARTBEAT=100") });
    std::vector<MultiInstance*> instances;
    for (int i = 0; i < 3; ++i)
    {
        instances.push_back(fixture.launch(QStringLiteral("mcp-%1").arg(i), QStringList(), environment));
    }
    for (MultiInstance* instance : instances)
    {
        QVERIFY(fixture.connect(instance, MultiInstanceHelper::timeout(60000)));
    }

    // Exactly one owner
    MultiInstance* owner = nullptr;
    auto findOwner = [&instances, &owner]()
    {
        int owners = 0;
        int others = 0;
        for (MultiInstance* instance : instances)
        {
            const QJsonObject mcp = instance->mcp();
            if (mcp.value(QStringLiteral("owner")).toBool() && mcp.value(QStringLiteral("running")).toBool())
            {
                ++owners;
                owner = instance;
            }
            else if (mcp.value(QStringLiteral("state")).toString() == QLatin1String("otherInstance"))
            {
                ++others;
            }
        }
        return owners == 1 && others == int(instances.size()) - 1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findOwner(), MultiInstanceHelper::timeout(30000));
    pdf::PDFMcpServerRow row = pdf::PDFMcpRepository(fixture.database.get()).getServer();
    QCOMPARE(row.instance, owner->mcp().value(QStringLiteral("instanceId")).toString());
    QCOMPARE(row.pid, owner->pid);
    QCOMPARE(row.port, int(port));

    std::vector<MultiInstance*> others;
    for (MultiInstance* instance : instances)
    {
        if (instance != owner)
        {
            others.push_back(instance);
        }
    }
    MultiInstance* second = others[0];
    MultiInstance* third = others[1];
    const int otherState = int(pdf::PDFAppAiPage::ShownState::OtherInstance);
    const int stoppedState = int(pdf::PDFAppAiPage::ShownState::Stopped);
    const int runningState = int(pdf::PDFAppAiPage::ShownState::Running);

    // The AI page of the other instances
    for (MultiInstance* other : others)
    {
        QVERIFY(other->call({ QStringLiteral("navigate"), QStringLiteral("0"), QStringLiteral("/ai") }).value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("class")).toString(), QStringLiteral("pdf::PDFAppAiPage"), MultiInstanceHelper::timeout(10000));
        QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), otherState, MultiInstanceHelper::timeout(10000));
        QVERIFY(!other->page().value(QStringLiteral("startEnabled")).toBool());
        QVERIFY(other->page().value(QStringLiteral("calloutVisible")).toBool());
    }

    // Show Other Window reaches the owner only
    QVERIFY(second->call({ QStringLiteral("ai"), QStringLiteral("callout") }).value(QStringLiteral("clicked")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(owner->mcp().value(QStringLiteral("showWindowRequested")).toInt(), 1, MultiInstanceHelper::timeout(10000));
    QCOMPARE(third->mcp().value(QStringLiteral("showWindowRequested")).toInt(), 0);
    QCOMPARE(second->mcp().value(QStringLiteral("showWindowRequested")).toInt(), 0);

    // An assistant of the owner, listed and disconnected in another instance
    {
        pdftest::McpTestClient client(port);
        client.setClientInfo(QStringLiteral("claude-code"));
        QVERIFY(!client.listTools().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(owner->mcp().value(QStringLiteral("assistants")).toInt(), 1, MultiInstanceHelper::timeout(10000));
        QTRY_COMPARE_WITH_TIMEOUT(MultiInstanceHelper::toStringList(second->page().value(QStringLiteral("assistants"))), QStringList({ QStringLiteral("Claude Code") }),
                                  MultiInstanceHelper::timeout(10000));
        QVERIFY(second->call({ QStringLiteral("ai"), QStringLiteral("disconnect-first") }).value(QStringLiteral("clicked")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(owner->mcp().value(QStringLiteral("assistants")).toInt(), 0, MultiInstanceHelper::timeout(10000));
        QTRY_VERIFY_WITH_TIMEOUT(second->page().value(QStringLiteral("assistants")).toArray().isEmpty(), MultiInstanceHelper::timeout(10000));
    }

    // The owner quits: its row is free, Start is enabled at once (a notification)
    QVERIFY(owner->call({ QStringLiteral("quit") }).value(QStringLiteral("ok")).toBool());
    QVERIFY(MultiFixture::waitForExit(owner, MultiInstanceHelper::timeout(30000)));
    QCOMPARE(owner->process->exitCode(), 0);
    for (MultiInstance* other : others)
    {
        QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), stoppedState, MultiInstanceHelper::timeout(5000));
        QVERIFY(other->page().value(QStringLiteral("startEnabled")).toBool());
    }

    // Another instance takes the server over
    QVERIFY(second->call({ QStringLiteral("ai"), QStringLiteral("start") }).value(QStringLiteral("clicked")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(second->page().value(QStringLiteral("shownState")).toInt(), runningState, MultiInstanceHelper::timeout(10000));
    QVERIFY(second->mcp().value(QStringLiteral("owner")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(third->page().value(QStringLiteral("shownState")).toInt(), otherState, MultiInstanceHelper::timeout(10000));
    QVERIFY(!third->page().value(QStringLiteral("startEnabled")).toBool());
    row = pdf::PDFMcpRepository(fixture.database.get()).getServer();
    QCOMPARE(row.pid, second->pid);

    // The new owner is killed with a connected assistant: after the stale
    // heartbeat the last instance shows the stopped server without assistants
    pdftest::McpTestClient client(port);
    client.setClientInfo(QStringLiteral("claude-code"));
    QVERIFY(!client.listTools().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(MultiInstanceHelper::toStringList(third->page().value(QStringLiteral("assistants"))), QStringList({ QStringLiteral("Claude Code") }),
                              MultiInstanceHelper::timeout(10000));
    QCOMPARE(third->page().value(QStringLiteral("shownState")).toInt(), otherState);

    second->process->kill();
    QVERIFY(MultiFixture::waitForExit(second, MultiInstanceHelper::timeout(30000)));
    const int staleLimit = MultiInstanceHelper::timeout(int(pdf::PDFMcpRepository::STALE_TIME + 3 * pdf::PDFMcpRepository::HEARTBEAT_INTERVAL + 5000));
    QTRY_COMPARE_WITH_TIMEOUT(third->page().value(QStringLiteral("shownState")).toInt(), stoppedState, staleLimit);
    QVERIFY(third->page().value(QStringLiteral("startEnabled")).toBool());
    QVERIFY(third->page().value(QStringLiteral("assistants")).toArray().isEmpty());

    QVERIFY(third->call({ QStringLiteral("ai"), QStringLiteral("start") }).value(QStringLiteral("clicked")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(third->page().value(QStringLiteral("shownState")).toInt(), runningState, MultiInstanceHelper::timeout(10000));
    QCOMPARE(pdf::PDFMcpRepository(fixture.database.get()).getServer().pid, third->pid);
    QTRY_VERIFY_WITH_TIMEOUT(pdf::PDFMcpRepository(fixture.database.get()).getClients().empty(), MultiInstanceHelper::timeout(10000));
}

void AppMultiInstanceTest::mcpOwnerHangs_data()
{
    QTest::addColumn<bool>("isKilled");
    QTest::newRow("returns-and-quits") << false;
    QTest::newRow("killed-while-hung") << true;
}

void AppMultiInstanceTest::mcpOwnerHangs()
{
    // The owner of the server hangs (its process runs, its heartbeat stops):
    // after 10 s another instance enables Start; its Start finds the port of the
    // hung PDF4QT and shows the other window; the owner comes back and quits, or
    // it is killed while it hangs - Start is enabled again (newapp_mcp.md 4)
    QFETCH(bool, isKilled);
    MultiFixture& fixture = *m_fixture;
    QVERIFY(fixture.openDatabase());
    const quint16 port = MultiInstanceHelper::findFreePort();
    QVERIFY(port > 0);
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::mcpPort(), int(port)).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::mcpStartWithApp(), true).isError());
    const QStringList environment({ QStringLiteral("PDF4QT_MULTI_HEARTBEAT=100") });
    const int otherState = int(pdf::PDFAppAiPage::ShownState::OtherInstance);
    const int stoppedState = int(pdf::PDFAppAiPage::ShownState::Stopped);
    const int runningState = int(pdf::PDFAppAiPage::ShownState::Running);

    MultiInstance* owner = fixture.start(QStringLiteral("owner"), QStringList(), environment);
    QVERIFY(owner);
    QTRY_VERIFY_WITH_TIMEOUT(owner->mcp().value(QStringLiteral("owner")).toBool(), MultiInstanceHelper::timeout(30000));
    MultiInstance* other = fixture.start(QStringLiteral("other"), QStringList(), environment);
    QVERIFY(other);
    QVERIFY(other->call({ QStringLiteral("navigate"), QStringLiteral("0"), QStringLiteral("/ai") }).value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("class")).toString(), QStringLiteral("pdf::PDFAppAiPage"), MultiInstanceHelper::timeout(10000));
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), otherState, MultiInstanceHelper::timeout(10000));
    QVERIFY(!other->page().value(QStringLiteral("startEnabled")).toBool());

    // The owner hangs: its heartbeat older than 10 s is the end of the server
    const QString marker = fixture.root.filePath(QStringLiteral("unfreeze.marker"));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(owner->call({ QStringLiteral("freeze"), marker }).value(QStringLiteral("frozen")).toBool());
    const int staleLimit = MultiInstanceHelper::timeout(int(pdf::PDFMcpRepository::STALE_TIME + 3 * pdf::PDFMcpRepository::HEARTBEAT_INTERVAL + 5000));
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), stoppedState, staleLimit);
    QVERIFY2(timer.elapsed() >= pdf::PDFMcpRepository::STALE_TIME - 1000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(other->page().value(QStringLiteral("startEnabled")).toBool());

    // Start: the hung PDF4QT still has the port - the other window is shown
    QVERIFY(other->call({ QStringLiteral("ai"), QStringLiteral("start") }).value(QStringLiteral("clicked")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), otherState, MultiInstanceHelper::timeout(10000));
    QVERIFY(!other->page().value(QStringLiteral("startEnabled")).toBool());
    QVERIFY(!other->mcp().value(QStringLiteral("owner")).toBool());
    QCOMPARE(pdf::PDFMcpRepository(fixture.database.get()).getServer().pid, owner->pid);

    if (isKilled)
    {
        // The hung owner is killed (End task): its port is free, Start is enabled
        // although its row (stale already) is not written (review RMULTI)
        owner->process->kill();
        QVERIFY(MultiFixture::waitForExit(owner, MultiInstanceHelper::timeout(30000)));
    }
    else
    {
        // The owner comes back (its heartbeat goes on) and quits: Start is enabled
        {
            QFile file(marker);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QTRY_VERIFY_WITH_TIMEOUT(MultiInstanceHelper::now() - pdf::PDFMcpRepository(fixture.database.get()).getServer().heartbeat < 1000, MultiInstanceHelper::timeout(10000));
        QVERIFY(owner->call({ QStringLiteral("quit") }).value(QStringLiteral("ok")).toBool());
        QVERIFY(MultiFixture::waitForExit(owner, MultiInstanceHelper::timeout(30000)));
        QCOMPARE(owner->process->exitCode(), 0);
    }
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), stoppedState, MultiInstanceHelper::timeout(5000));
    QVERIFY(other->page().value(QStringLiteral("startEnabled")).toBool());

    QVERIFY(other->call({ QStringLiteral("ai"), QStringLiteral("start") }).value(QStringLiteral("clicked")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(other->page().value(QStringLiteral("shownState")).toInt(), runningState, MultiInstanceHelper::timeout(10000));
    QVERIFY(other->mcp().value(QStringLiteral("owner")).toBool());
    QCOMPARE(pdf::PDFMcpRepository(fixture.database.get()).getServer().pid, other->pid);
}

void AppMultiInstanceTest::windowGeometryLastClose()
{
    // O29: the geometry of the window is written by an accepted close; the next
    // start restores the last accepted close of any instance (a canceled close
    // writes nothing)
    MultiFixture& fixture = *m_fixture;
    const QString unsavedFile = fixture.sample(QStringLiteral("Unsaved.pdf"));
    MultiInstance* first = fixture.start(QStringLiteral("first"));
    QVERIFY(first);
    MultiInstance* second = fixture.start(QStringLiteral("second"));
    QVERIFY(second);

    const QRect available = MultiInstanceHelper::toRect(first->state().value(QStringLiteral("available")));
    const QSize firstSize = QSize(724, 484).boundedTo(available.size());
    const QSize secondSize = QSize(736, 492).boundedTo(available.size());
    const QRect firstGeometry(available.topLeft() + QPoint(qMin(11, available.width() - firstSize.width()), qMin(13, available.height() - firstSize.height())), firstSize);
    const QRect secondGeometry(available.topLeft() + QPoint(qMin(37, available.width() - secondSize.width()), qMin(41, available.height() - secondSize.height())), secondSize);
    QVERIFY(firstGeometry != secondGeometry);
    auto setGeometry = [](MultiInstance* instance, const QRect& rect)
    {
        return MultiInstanceHelper::toRect(instance->call({ QStringLiteral("geometry"), QString::number(rect.x()), QString::number(rect.y()),
                                                            QString::number(rect.width()), QString::number(rect.height()) }).value(QStringLiteral("geometry")));
    };
    QCOMPARE(setGeometry(first, firstGeometry), firstGeometry);
    QCOMPARE(setGeometry(second, secondGeometry), secondGeometry);

    // The first window is closed: its geometry is the next start
    QJsonObject closed = first->call({ QStringLiteral("close"), QStringLiteral("cancel") });
    QVERIFY(closed.value(QStringLiteral("accepted")).toBool());
    QVERIFY(MultiFixture::waitForExit(first, MultiInstanceHelper::timeout(30000)));
    QCOMPARE(first->process->exitCode(), 0);

    // The second window asks about its unsaved document later, the close is
    // canceled: nothing is stored
    QVERIFY(second->call({ QStringLiteral("make-unsaved"), unsavedFile }).value(QStringLiteral("ok")).toBool());
    closed = second->call({ QStringLiteral("close"), QStringLiteral("cancel") });
    QVERIFY(closed.contains(QStringLiteral("accepted")));
    QVERIFY(!closed.value(QStringLiteral("accepted")).toBool());
    QVERIFY(second->state().value(QStringLiteral("visible")).toBool());

    MultiInstance* third = fixture.start(QStringLiteral("third"));
    QVERIFY(third);
    QJsonObject state = third->state();
    QCOMPARE(MultiInstanceHelper::toRect(state.value(QStringLiteral("geometry"))), firstGeometry);
    QVERIFY(!state.value(QStringLiteral("maximized")).toBool());
    QVERIFY(third->call({ QStringLiteral("quit") }).value(QStringLiteral("ok")).toBool());
    QVERIFY(MultiFixture::waitForExit(third, MultiInstanceHelper::timeout(30000)));

    // The second window closed later (Quit Without Saving): the last accepted close
    closed = second->call({ QStringLiteral("close"), QStringLiteral("discard") });
    QVERIFY(closed.value(QStringLiteral("accepted")).toBool());
    QVERIFY(MultiFixture::waitForExit(second, MultiInstanceHelper::timeout(30000)));
    QCOMPARE(second->process->exitCode(), 0);

    MultiInstance* fourth = fixture.start(QStringLiteral("fourth"));
    QVERIFY(fourth);
    state = fourth->state();
    QCOMPARE(MultiInstanceHelper::toRect(state.value(QStringLiteral("geometry"))), secondGeometry);
    QVERIFY(!state.value(QStringLiteral("maximized")).toBool());

    QVERIFY(fixture.openDatabase());
    QCOMPARE(pdf::PDFAppWindowGeometry::fromText(fixture.settings().getString(pdf::PDFSettingKeys::windowGeometry())).getNormalGeometry(), secondGeometry);
}

void AppMultiInstanceTest::portableInstance()
{
    // newapplication.md 3: --portable keeps everything in memory - it writes
    // nothing into the shared database, sees nothing of it and does not take the
    // name of the forwarding
    MultiFixture& fixture = *m_fixture;
    MultiInstance* portable = fixture.start(QStringLiteral("portable"), { QStringLiteral("--portable") });
    QVERIFY(portable);
    QCOMPARE(portable->guard, QStringLiteral("none"));
    QJsonObject state = portable->state();
    QCOMPARE(state.value(QStringLiteral("storageMode")).toString(), QStringLiteral("portable"));
    QVERIFY(state.value(QStringLiteral("portable")).toBool());
    QVERIFY(!QDir(fixture.dataDirectory).exists());
    const QString portableTheme = state.value(QStringLiteral("theme")).toString();
    const QString otherScheme = portableTheme == QLatin1String("dark") ? QStringLiteral("light") : QStringLiteral("dark");

    MultiInstance* first = fixture.start(QStringLiteral("first"));
    QVERIFY(first);
    QCOMPARE(first->guard, QStringLiteral("listening"));
    MultiInstance* second = fixture.start(QStringLiteral("second"));
    QVERIFY(second);
    QCOMPARE(second->guard, QStringLiteral("standalone"));

    // A write of a normal instance: the other normal instance sees it, the
    // portable one does not
    const QString shared = first->call({ QStringLiteral("recent-open"), fixture.root.filePath(QStringLiteral("shared.pdf")) }).value(QStringLiteral("path")).toString();
    QVERIFY(!shared.isEmpty());
    QVERIFY(first->call({ QStringLiteral("set"), pdf::PDFSettingKeys::colorScheme(), QStringLiteral("\"%1\"").arg(otherScheme) }).value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(second->recentPaths().contains(shared), MultiInstanceHelper::timeout(10000));
    QTRY_COMPARE_WITH_TIMEOUT(second->state().value(QStringLiteral("theme")).toString(), otherScheme, MultiInstanceHelper::timeout(10000));
    QVERIFY(!portable->recentPaths().contains(shared));
    QCOMPARE(portable->state().value(QStringLiteral("theme")).toString(), portableTheme);

    // A write of the portable instance reaches nobody; a later write of a normal
    // instance does (the order of the notifications)
    const QString hidden = portable->call({ QStringLiteral("recent-open"), fixture.root.filePath(QStringLiteral("portable-only.pdf")) }).value(QStringLiteral("path")).toString();
    QVERIFY(!hidden.isEmpty());
    QVERIFY(portable->recentPaths().contains(hidden));
    QVERIFY(portable->call({ QStringLiteral("set"), pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("\"de\"") }).value(QStringLiteral("error")).toString().isEmpty());
    const QString later = first->call({ QStringLiteral("recent-open"), fixture.root.filePath(QStringLiteral("later.pdf")) }).value(QStringLiteral("path")).toString();
    QTRY_VERIFY_WITH_TIMEOUT(second->recentPaths().contains(later), MultiInstanceHelper::timeout(10000));
    QVERIFY(!second->recentPaths().contains(hidden));
    QCOMPARE(second->state().value(QStringLiteral("language")).toString(), pdf::PDFSettingValues::languageEnglish());
    QVERIFY(fixture.openDatabase());
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::appearanceLanguage()), pdf::PDFSettingValues::languageEnglish());
    for (const pdf::PDFRecentFile& file : fixture.recent().getFiles())
    {
        QVERIFY(file.path != hidden);
    }

    // A forwarded file opens in the normal instance holding the name
    const QString forwardedFile = fixture.sample(QStringLiteral("Forwarded.pdf"));
    pdf::PDFAppInstanceRequest request;
    request.files << forwardedFile;
    QString error;
    QCOMPARE(pdf::PDFAppInstanceClient::send(fixture.getServerName(), request, MultiInstanceHelper::timeout(5000), &error), pdf::PDFAppInstanceClient::Result::Delivered);
    QTRY_VERIFY_WITH_TIMEOUT(first->documents().contains(forwardedFile), MultiInstanceHelper::timeout(30000));
    QCOMPARE(portable->state().value(QStringLiteral("openRequested")).toInt(), 0);
    QVERIFY(portable->documents().isEmpty());

    // The portable instance quits without a file of its own in the data folder
    const QStringList files = QDir(fixture.dataDirectory).entryList(QDir::Files);
    QVERIFY(portable->call({ QStringLiteral("quit") }).value(QStringLiteral("ok")).toBool());
    QVERIFY(MultiFixture::waitForExit(portable, MultiInstanceHelper::timeout(30000)));
    QCOMPARE(portable->process->exitCode(), 0);
    QCOMPARE(QDir(fixture.dataDirectory).entryList(QDir::Files), files);
}

void AppMultiInstanceTest::crashDuringWrite()
{
    // An instance killed inside of a write transaction (newapp_storage.md: one
    // SQLite file in WAL): the other instances go on writing, nothing they have
    // committed is lost, the uncommitted rows of the killed one are gone and the
    // file is consistent for the next start
    MultiFixture& fixture = *m_fixture;
    const QString path = fixture.root.filePath(QStringLiteral("bookmarked.pdf"));
    const int count = 20;
    MultiInstance* victim = fixture.start(QStringLiteral("victim"));
    QVERIFY(victim);
    MultiInstance* survivor = fixture.start(QStringLiteral("survivor"));
    QVERIFY(survivor);

    QCOMPARE(survivor->call({ QStringLiteral("bookmarks-add"), path, QStringLiteral("before"), QString::number(count) }).value(QStringLiteral("errors")).toInt(), 0);

    const QJsonObject held = victim->call({ QStringLiteral("hold-transaction"), path, QStringLiteral("uncommitted"), QString::number(count) });
    QVERIFY(held.value(QStringLiteral("active")).toBool());
    QCOMPARE(held.value(QStringLiteral("errors")).toInt(), 0);

    // The survivor writes, while the victim holds the lock of the writer: it
    // waits in the busy handler of SQLite (no observable moment - the wait is
    // proven by the time of its writes); the victim is killed then
    const int holdTime = 500;
    const int during = survivor->send({ QStringLiteral("bookmarks-add"), path, QStringLiteral("during"), QString::number(count) });
    QVERIFY(survivor->waitForOutput("writing during", MultiInstanceHelper::timeout(30000)));
    QTest::qWait(holdTime);
    victim->process->kill();
    QVERIFY(MultiFixture::waitForExit(victim, MultiInstanceHelper::timeout(30000)));
    const QJsonObject duringReply = survivor->wait(during, MultiInstanceHelper::timeout(60000));
    QVERIFY(duringReply.contains(QStringLiteral("errors")));
    QCOMPARE(duringReply.value(QStringLiteral("errors")).toInt(), 0);
    QVERIFY2(duringReply.value(QStringLiteral("elapsed")).toInt() >= holdTime, qPrintable(QString::number(duringReply.value(QStringLiteral("elapsed")).toInt())));
    QCOMPARE(survivor->call({ QStringLiteral("bookmarks-add"), path, QStringLiteral("after"), QString::number(count) }).value(QStringLiteral("errors")).toInt(), 0);

    // The next start: the file on the disk, not damaged, every committed row
    MultiInstance* next = fixture.start(QStringLiteral("next"));
    QVERIFY(next);
    const QJsonObject state = next->state();
    QCOMPARE(state.value(QStringLiteral("storageMode")).toString(), QStringLiteral("disk"));
    QVERIFY(state.value(QStringLiteral("damagedFile")).toString().isEmpty());
    const QStringList titles = MultiInstanceHelper::toStringList(next->call({ QStringLiteral("bookmark-list"), path }).value(QStringLiteral("titles")));
    QCOMPARE(titles.size(), 3 * count);
    for (const QString& prefix : { QStringLiteral("before"), QStringLiteral("during"), QStringLiteral("after") })
    {
        for (int i = 0; i < count; ++i)
        {
            QVERIFY2(titles.contains(QStringLiteral("%1-%2").arg(prefix).arg(i)), qPrintable(prefix));
        }
    }
    for (const QString& title : titles)
    {
        QVERIFY(!title.startsWith(QStringLiteral("uncommitted")));
    }

    QVERIFY(fixture.openDatabase());
    pdf::PDFDatabaseStatement check(fixture.database->getConnection(), "PRAGMA integrity_check");
    QVERIFY(check.next());
    QCOMPARE(check.getText(0), QStringLiteral("ok"));
}

int main(int argc, char* argv[])
{
    // The application instances started by the tests
    if (argc > 1 && QByteArray(argv[1]) == "--multi-child")
    {
        return pdftest::MultiChild::run(argc, argv);
    }

    QApplication application(argc, argv);
    AppMultiInstanceTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmultiinstancetest.moc"
