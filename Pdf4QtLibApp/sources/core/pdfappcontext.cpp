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
#include "pdfapplog.h"
#include "pdfapppaths.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfthememanager.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStyleHints>

namespace pdf
{

PDFAppContext::PDFAppContext(const PDFAppCommandLine& commandLine, QObject* parent) :
    QObject(parent),
    m_commandLine(commandLine)
{
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

    if (options.filePath.isEmpty())
    {
        options.filePath = PDFAppPaths::getDatabaseFilePath();
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

    applyColorScheme();
}

void PDFAppContext::close()
{
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

    PDFThemeColors::Scheme scheme = PDFThemeColors::Scheme::Light;
    if (colorScheme == PDFSettingValues::colorSchemeDark())
    {
        scheme = PDFThemeColors::Scheme::Dark;
    }
    else if (colorScheme != PDFSettingValues::colorSchemeLight())
    {
        // Auto (also the default, when the database cannot be read)
        QGuiApplication* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
        if (application && application->styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        {
            scheme = PDFThemeColors::Scheme::Dark;
        }
    }

    PDFThemeManager* themeManager = PDFThemeManager::getInstance();
    if (themeManager->getTheme().getScheme() != scheme)
    {
        themeManager->setScheme(scheme);
    }
}

void PDFAppContext::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::colorScheme())
    {
        applyColorScheme();
    }
}

void PDFAppContext::onEverythingChanged()
{
    applyColorScheme();
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
