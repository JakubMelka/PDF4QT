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

#include "pdfappcontext.h"
#include "pdfappmcpcontroller.h"
#include "pdfappannotationstylestorage.h"
#include "pdfapplocale.h"
#include "pdfapplog.h"
#include "pdfapppaths.h"
#include "pdfapptranslator.h"
#include "pdfdatabasenotifier.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"

#include "pdfannotationstyle.h"
#include "pdfcms.h"
#include "pdfthememanager.h"
#include "pdfutils.h"

#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QStyleHints>

namespace pdf
{

PDFAppContext::PDFAppContext(const PDFAppCommandLine& commandLine, QObject* parent) :
    QObject(parent),
    m_commandLine(commandLine)
{
    m_documentRegistry = new PDFAppDocumentRegistry(this);
    m_translator = new PDFAppTranslator(this);
    m_minuteTicker = new PDFAppMinuteTicker(this);

    // O17: the texts of the application are formatted in the language of the user
    // interface; the default locale of the process follows it ("%Ln", Qt widgets,
    // Pdf4QtLibFluent) - set before the windows and the worker threads exist
    PDFAppLocale::applyDefaultLocale();

    // Every change of a setting of the color management (a color of the color
    // modes, a slider) clears the lists of the manager of each open document: the
    // installed profiles of the system are listed once per process instead (more
    // than 100 ms on Windows per document and change, review RC5-BC; Settings ›
    // Color management lists them again - PDFAppColorProfiles::scan). The cache
    // is process-wide, so it is turned on once at the start, not by each view.
    PDFCMSManager::setExternalProfilesCacheEnabled(true);

    PDFSettingDefinitions::registerBuiltinDefinitions(&m_settingRegistry);
    PDFAppCommands::registerBuiltinCommands(&m_commandRegistry);

    if (QGuiApplication* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        connect(application->styleHints(), &QStyleHints::colorSchemeChanged, this, &PDFAppContext::onSystemColorSchemeChanged);
    }

    // The documents, which the system asks to open (macOS: QFileOpenEvent of the
    // Finder, also at the start before a window exists)
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        application->installEventFilter(this);
    }
}

PDFAppInstanceRequest PDFAppContext::createFileOpenRequest(const QUrl& url)
{
    if (!url.isValid() || !url.isLocalFile())
    {
        // Only local files are opened (as an argument file: of the command line)
        PDFAppCommandLineError error;
        error.kind = PDFAppCommandLineError::Kind::InvalidArgument;
        error.argument = url.toString(QUrl::FullyEncoded).left(PDFAppCommandLine::MAXIMUM_ARGUMENT_LENGTH);
        PDFAppInstanceRequest request;
        request.errors << error.getMessage().left(PDFAppInstanceProtocol::MAXIMUM_ERROR_LENGTH);
        return request;
    }

    // "--" ends the options: a file named like an option is a file
    const PDFAppCommandLine commandLine = PDFAppCommandLine::parse({ QCoreApplication::applicationFilePath(), QStringLiteral("--"), url.toLocalFile() }, QString());
    return PDFAppInstanceProtocol::createRequest(commandLine, QString());
}

bool PDFAppContext::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FileOpen && watched == QCoreApplication::instance())
    {
        const QFileOpenEvent* fileOpenEvent = static_cast<const QFileOpenEvent*>(event);
        requestOpen(createFileOpenRequest(fileOpenEvent->url()));
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void PDFAppContext::connectNotify(const QMetaMethod& signal)
{
    QObject::connectNotify(signal);

    // The window connects the signal in its constructor: the held requests are
    // delivered from the event loop, when the window is complete
    if (signal == QMetaMethod::fromSignal(&PDFAppContext::openRequested) && !m_pendingRequests.empty())
    {
        QMetaObject::invokeMethod(this, &PDFAppContext::deliverPendingRequests, Qt::QueuedConnection);
    }
}

void PDFAppContext::requestOpen(const PDFAppInstanceRequest& request)
{
    if (isSignalConnected(QMetaMethod::fromSignal(&PDFAppContext::openRequested)))
    {
        Q_EMIT openRequested(request);
        return;
    }

    if (m_pendingRequests.size() < size_t(MAXIMUM_PENDING_FILE_OPENS))
    {
        m_pendingRequests.push_back(request);
    }
}

void PDFAppContext::deliverPendingRequests()
{
    if (!isSignalConnected(QMetaMethod::fromSignal(&PDFAppContext::openRequested)))
    {
        return;
    }

    // A receiver may delete the context
    QPointer<PDFAppContext> guard(this);
    std::vector<PDFAppInstanceRequest> requests = std::move(m_pendingRequests);
    m_pendingRequests.clear();
    for (const PDFAppInstanceRequest& request : requests)
    {
        Q_EMIT openRequested(request);
        if (!guard)
        {
            return;
        }
    }
}

PDFAppMcpController* PDFAppContext::getMcpController() const
{
    return m_mcpController;
}

PDFAppContext::~PDFAppContext()
{
    // No QFileOpenEvent reaches the context, while its members are destroyed
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        application->removeEventFilter(this);
    }
    close();
}

void PDFAppContext::open(PDFDatabaseOptions options)
{
    close();

    if (options.filePath.isEmpty() && options.directory.isEmpty())
    {
        // O19: the file of the schema version in the folder of the data
        options.directory = PDFAppPaths::getDataDirectory();
    }
    options.portable = options.portable || m_commandLine.isPortable();

    m_database = new PDFDatabase(this);
    connect(m_database, &PDFDatabase::stateChanged, this, &PDFAppContext::onDatabaseStateChanged);
    m_database->open(options);

    const PDFDatabaseState state = m_database->getState();
    if (state.mode == PDFDatabaseStorageMode::Portable)
    {
        qInfo("%s", qPrintable(state.getDescription()));
    }
    else if (state.mode != PDFDatabaseStorageMode::Disk || !state.damagedFilePath.isEmpty())
    {
        qWarning("%s", qPrintable(state.getDescription()));
    }

    m_notifier = new PDFDatabaseNotifier(m_database, this);
    connect(m_notifier, &PDFDatabaseNotifier::changed, this, &PDFAppContext::onSettingChanged);
    connect(m_notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppContext::onEverythingChanged);

    m_thumbnailer = new PDFAppThumbnailer(m_database, this);
    m_folderInfo = new PDFAppFolderInfoService(this);

    // The engine reads and writes the styles of the annotation tools in the
    // database (step 4, E1) and writes the author chosen in the settings
    m_annotationStyleStorage = std::make_unique<PDFAppAnnotationStyleStorage>(m_database);
    PDFAnnotationStyleSettings::setStorage(m_annotationStyleStorage.get());
    PDFAnnotationStyleManager::setStyleWindowEnabled(false);
    applyAuthorSettings();

    applyColorScheme();

    // The language before the windows exist (no LanguageChange to rebuild them)
    applyLanguage();

    // The MCP server (step 7): started here when mcp.startWithApp is on
    m_mcpController = new PDFAppMcpController(this, this);
    m_mcpController->initialize();
}

void PDFAppContext::close()
{
    // The MCP server first (plan of step 7, 2.6): the server stops, the pending
    // confirmations are denied, the jobs end, the host is destroyed and the row
    // of the server is released - while the database is still open
    if (m_mcpController)
    {
        PDFAppMcpController* controller = m_mcpController;
        m_mcpController = nullptr;
        controller->shutdown();
        delete controller;
    }

    // The storage of the annotation styles is removed before the database closes
    // (the engine falls back to its default storage)
    if (m_annotationStyleStorage)
    {
        // Only the context, whose storage the engine uses, gives the engine its
        // defaults back (another context of the process keeps its own)
        if (PDFAnnotationStyleSettings::getStorage() == m_annotationStyleStorage.get())
        {
            PDFAnnotationStyleSettings::setStorage(nullptr);
            PDFAnnotationStyleManager::setStyleWindowEnabled(true);

            // The author of the closed database is not written into documents
            // of the process any more
            PDFAuthorSettings::setAuthorName(PDFAuthorSettings::AuthorNameMode::Anonymous, QString());
        }
        m_annotationStyleStorage.reset();
    }

    // The worker threads use the database: they stop before it is closed
    if (m_thumbnailer)
    {
        delete m_thumbnailer;
        m_thumbnailer = nullptr;
    }

    if (m_folderInfo)
    {
        delete m_folderInfo;
        m_folderInfo = nullptr;
    }

    if (m_notifier)
    {
        delete m_notifier;
        m_notifier = nullptr;
    }

    if (m_database)
    {
        PDFDatabase* database = m_database;
        m_database = nullptr;
        database->disconnect(this);
        database->close();
        delete database;
    }
}

PDFSettingsRepository PDFAppContext::getSettings() const
{
    return PDFSettingsRepository(m_database, &m_settingRegistry);
}

PDFShortcutRepository PDFAppContext::getShortcuts() const
{
    return PDFShortcutRepository(m_database);
}

PDFRecentRepository PDFAppContext::getRecent() const
{
    return PDFRecentRepository(m_database, &m_settingRegistry);
}

PDFWorkspaceRepository PDFAppContext::getWorkspaces() const
{
    return PDFWorkspaceRepository(m_database);
}

PDFToolRepository PDFAppContext::getTools() const
{
    return PDFToolRepository(m_database);
}

PDFToolCollectionRepository PDFAppContext::getToolCollections() const
{
    return PDFToolCollectionRepository(m_database);
}

PDFDatabaseState PDFAppContext::getStorageState() const
{
    return m_database ? m_database->getState() : PDFDatabaseState();
}

void PDFAppContext::setInstanceServer(PDFAppInstanceServer* server)
{
    if (m_instanceServer == server)
    {
        return;
    }

    if (m_instanceServer)
    {
        m_instanceServer->disconnect(this);
    }

    m_instanceServer = server;
    if (server)
    {
        server->setParent(this);
        connect(server, &PDFAppInstanceServer::openRequested, this, &PDFAppContext::onOpenRequested);
    }
}

QString PDFAppContext::getLogDirectory() const
{
    return PDFAppLog::getLogDirectory();
}

QString PDFAppContext::getLogFile() const
{
    return PDFAppLog::getCurrentLogFile();
}

void PDFAppContext::applyColorScheme()
{
    const QString colorScheme = getSettings().getString(PDFSettingKeys::colorScheme());
    const bool isLight = colorScheme == PDFSettingValues::colorSchemeLight();
    const bool isDark = colorScheme == PDFSettingValues::colorSchemeDark();

    // Auto (also the default, when the database cannot be read) leaves the color scheme
    // of the application to the system: the style removes its request of the scheme of
    // the theme (Light / Dark), otherwise QStyleHints::colorScheme would return the
    // requested scheme and the changes of the system would not be signaled. The removal
    // can signal a change of the scheme at once, this function is then entered again
    // (it converges: the second call sees the same setting and the same scheme).
    QPointer<PDFAppContext> self(this);
    PDFThemeManager* themeManager = PDFThemeManager::getInstance();
    themeManager->setFollowingSystemColorScheme(!isLight && !isDark);
    if (!self)
    {
        return;
    }

    PDFThemeColors::Scheme scheme = isDark ? PDFThemeColors::Scheme::Dark : PDFThemeColors::Scheme::Light;
    if (!isLight && !isDark)
    {
        // Unknown (no scheme of the system, high contrast) is Light
        QGuiApplication* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
        if (application && application->styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        {
            scheme = PDFThemeColors::Scheme::Dark;
        }
    }

    if (themeManager->getTheme().getScheme() != scheme)
    {
        themeManager->setScheme(scheme);
    }
}

void PDFAppContext::applyLanguage()
{
    if (m_translator)
    {
        m_translator->setLanguage(getSettings().getString(PDFSettingKeys::appearanceLanguage()));
    }
}

void PDFAppContext::applyRecentLimit()
{
    if (!m_database)
    {
        return;
    }

    const PDFDatabaseError error = getRecent().applyLimit();
    if (error.isError())
    {
        qWarning("The history of the recent files could not be trimmed: %s", qPrintable(error.toString()));
    }
}

void PDFAppContext::applyAuthorSettings()
{
    const PDFSettingsRepository settings = getSettings();
    const QString mode = settings.getString(PDFSettingKeys::annotationsAuthorMode());
    const QString customName = sanitizeAuthorName(settings.getString(PDFSettingKeys::annotationsCustomAuthorName()));

    PDFAuthorSettings::AuthorNameMode authorMode = PDFAuthorSettings::AuthorNameMode::Anonymous;
    if (mode == PDFSettingValues::authorModeSystemUserName())
    {
        authorMode = PDFAuthorSettings::AuthorNameMode::SystemUserName;
    }
    else if (mode == PDFSettingValues::authorModeCustomName())
    {
        authorMode = PDFAuthorSettings::AuthorNameMode::CustomName;
    }
    PDFAuthorSettings::setAuthorName(authorMode, customName);
}

QString PDFAppContext::sanitizeAuthorName(const QString& name)
{
    // Control characters, line and paragraph separators, bidirectional
    // controls and other format characters never get into the documents; the
    // joiners (ZWNJ, ZWJ) are a part of names written in some scripts
    constexpr qsizetype MAXIMUM_LENGTH = 256;
    return PDFRepositoryHelper::removeInvisibleCharacters(name, true, MAXIMUM_LENGTH).trimmed();
}

void PDFAppContext::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::colorScheme())
    {
        applyColorScheme();
    }
    else if (topic == PDFSettingsRepository::getTopic() && (key == PDFSettingKeys::annotationsAuthorMode() || key == PDFSettingKeys::annotationsCustomAuthorName()))
    {
        applyAuthorSettings();
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::appearanceLanguage())
    {
        applyLanguage();
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::recentMaxFiles())
    {
        applyRecentLimit();
    }
}

void PDFAppContext::onEverythingChanged()
{
    applyAuthorSettings();
    applyColorScheme();
    applyLanguage();

    // No applyRecentLimit: a write here would make the notifier poll at once (the
    // limit is applied, when the setting changes, and at every recording)
}

void PDFAppContext::onSystemColorSchemeChanged()
{
    applyColorScheme();
}

void PDFAppContext::onOpenRequested(const PDFAppInstanceRequest& request)
{
    const PDFAppInstanceRequest requestCopy = request;
    Q_EMIT openRequested(requestCopy);
}

void PDFAppContext::onDatabaseStateChanged()
{
    Q_EMIT storageStateChanged();
}

}   // namespace pdf
