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
#include "pdfappannotationstylestorage.h"
#include "pdfapplocale.h"
#include "pdfapplog.h"
#include "pdfapppaths.h"
#include "pdfapptranslator.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfannotationstyle.h"
#include "pdfcms.h"
#include "pdfthememanager.h"
#include "pdfutils.h"

#include <QGuiApplication>
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
}

PDFAppContext::~PDFAppContext()
{
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
}

void PDFAppContext::close()
{
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
    constexpr qsizetype MAXIMUM_LENGTH = 256;

    QString result;
    result.reserve(qMin(name.size(), MAXIMUM_LENGTH));
    for (qsizetype i = 0; i < name.size(); ++i)
    {
        // Whole code points: a surrogate pair is one character, a lone surrogate
        // (invalid UTF-16) is dropped
        char32_t codePoint = name.at(i).unicode();
        qsizetype length = 1;
        if (QChar::isHighSurrogate(codePoint) && i + 1 < name.size() && name.at(i + 1).isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(name.at(i), name.at(i + 1));
            length = 2;
        }
        else if (QChar::isSurrogate(codePoint))
        {
            continue;
        }

        // Control characters, line and paragraph separators, bidirectional
        // controls and other format characters never get into the documents; the
        // joiners (ZWNJ, ZWJ) are a part of names written in some scripts
        const QChar::Category category = QChar::category(codePoint);
        const bool isJoiner = codePoint == 0x200C || codePoint == 0x200D;
        const bool isRemoved = category == QChar::Other_Control || category == QChar::Other_NotAssigned ||
                               category == QChar::Separator_Line || category == QChar::Separator_Paragraph ||
                               (category == QChar::Other_Format && !isJoiner);
        if (!isRemoved)
        {
            if (result.size() + length > MAXIMUM_LENGTH)
            {
                break;
            }
            result.append(name.mid(i, length));
        }
        i += length - 1;
    }

    return result.trimmed();
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
