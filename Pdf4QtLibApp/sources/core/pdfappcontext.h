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

#ifndef PDFAPPCONTEXT_H
#define PDFAPPCONTEXT_H

#include "pdfappglobal.h"
#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappinstance.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingsrepository.h"
#include "pdfshortcutrepository.h"
#include "pdfrecentrepository.h"
#include "pdfworkspacerepository.h"
#include "pdftoolrepository.h"
#include "pdftoolcollectionrepository.h"
#include "pdfappthumbnailer.h"
#include "pdfappfolderinfo.h"
#include "pdfappdocumentregistry.h"
#include "pdfapptranslator.h"
#include "pdfappminuteticker.h"

#include <QObject>
#include <QPointer>

#include <memory>

namespace pdf
{

class PDFAppAnnotationStyleStorage;

/// Context of the running application: the storage, its notifier, the registries
/// of the settings and of the commands, the command line and the server of the
/// running instance. It is created by main and lives for the whole run; the shell
/// gets it from PDFAppWindowFactory::createMainWindow and passes it to the pages.
/// It holds no data of the user (everything is read from the database).
///
/// The context also applies the setting appearance.colorScheme to PDFThemeManager
/// (Auto follows the color scheme of the system) at the start and after every
/// change in any instance.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppContext : public QObject
{
    Q_OBJECT

public:
    explicit PDFAppContext(const PDFAppCommandLine& commandLine, QObject* parent = nullptr);
    virtual ~PDFAppContext() override;

    /// Opens the storage (GUI thread): the database of PDFAppPaths, or the memory
    /// database in the portable mode. \p options replaces the default options
    /// (tests); without a filePath and a directory the folder of the data
    /// (PDFAppPaths::getDataDirectory) with the file of the schema version.
    void open(PDFDatabaseOptions options = PDFDatabaseOptions());

    /// Closes the storage (also done by the destructor)
    void close();

    const PDFAppCommandLine& getCommandLine() const { return m_commandLine; }
    bool isPortable() const { return m_commandLine.isPortable(); }

    PDFDatabase* getDatabase() const { return m_database; }
    PDFDatabaseNotifier* getNotifier() const { return m_notifier; }
    const PDFSettingRegistry* getSettingRegistry() const { return &m_settingRegistry; }
    const PDFAppCommandRegistry* getCommandRegistry() const { return &m_commandRegistry; }

    /// Repositories (value objects, they read the database at each call)
    PDFSettingsRepository getSettings() const;
    PDFShortcutRepository getShortcuts() const;
    PDFRecentRepository getRecent() const;
    PDFWorkspaceRepository getWorkspaces() const;
    PDFToolRepository getTools() const;
    PDFToolCollectionRepository getToolCollections() const;

    /// Services of step 2, owned by the context (created by open, destroyed by
    /// close before the database); nullptr when the storage is not open.
    /// Thumbnails of the recent files (worker thread, results by the notifier)
    PDFAppThumbnailer* getThumbnailer() const { return m_thumbnailer; }

    /// Existence of paths and PDF counts of folders (worker threads, signal infoReady)
    PDFAppFolderInfoService* getFolderInfo() const { return m_folderInfo; }

    /// Open documents of the process (step 3): the document tabs register their
    /// documents, Recent and MCP list them. It lives as long as the context.
    PDFAppDocumentRegistry* getDocumentRegistry() const { return m_documentRegistry; }

    /// Storage of the styles of the annotation tools (step 4): installed into
    /// PDFAnnotationStyleSettings of the engine while the database is open
    /// (nullptr when it is closed)
    PDFAppAnnotationStyleStorage* getAnnotationStyleStorage() const { return m_annotationStyleStorage.get(); }

    /// Applies the author of new annotations (annotations.authorMode and
    /// annotations.customAuthorName) to PDFAuthorSettings of the engine; called at
    /// the opening and at every change of the two settings
    void applyAuthorSettings();

    /// Custom author name without control characters, bounded to 256 characters
    static QString sanitizeAuthorName(const QString& name);

    /// State of the storage for the banners of the Home tab
    PDFDatabaseState getStorageState() const;

    /// Takes the ownership of the server of the running instance (main creates it
    /// before the context, when it decides about the forwarding); its requests are
    /// re-emitted by openRequested. nullptr = no server (portable, --new-window, failure).
    void setInstanceServer(PDFAppInstanceServer* server);
    PDFAppInstanceServer* getInstanceServer() const { return m_instanceServer; }

    /// Folder and current file of the logs (About), empty in the portable mode
    QString getLogDirectory() const;
    QString getLogFile() const;

    /// Applies the color scheme of the settings (or of the system for Auto)
    void applyColorScheme();

    /// Language of the user interface (step 5): the translator of the application,
    /// which applies appearance.language at the opening of the storage and after
    /// every change of any instance. It lives as long as the context.
    PDFAppTranslator* getTranslator() const { return m_translator; }

    /// Shared minute timer of the relative times of recent activity (owner
    /// decision O25): the pages and panels, which show them, subscribe their
    /// widgets. It lives as long as the context.
    PDFAppMinuteTicker* getMinuteTicker() const { return m_minuteTicker; }

    /// Applies the setting appearance.language (the translator)
    void applyLanguage();

    /// Removes the unpinned recent files over recent.maxFiles (a lowered limit
    /// trims the history at once - step 5); the writes of another instance too
    void applyRecentLimit();

    /// Pages of the application (PDFAppPageRegistry) are registered by the navigator
    /// of a new window. false = the window is the shell only, every route ends on
    /// Page Not Found until a factory is set (tests of the shell). Default true.
    void setPagesEnabled(bool enabled) { m_isPagesEnabled = enabled; }
    bool isPagesEnabled() const { return m_isPagesEnabled; }

Q_SIGNALS:
    /// Another process asks to open the files (absolute paths) at the page, or only
    /// to raise the window (no files). The shell opens them as new tabs of the last
    /// active window, raises and activates it.
    void openRequested(const pdf::PDFAppInstanceRequest& request);

    /// The state of the storage has changed (banners)
    void storageStateChanged();

private:
    void onSettingChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onSystemColorSchemeChanged();
    void onOpenRequested(const pdf::PDFAppInstanceRequest& request);
    void onDatabaseStateChanged();

    PDFAppCommandLine m_commandLine;
    PDFSettingRegistry m_settingRegistry;
    PDFAppCommandRegistry m_commandRegistry;
    QPointer<PDFDatabase> m_database;
    QPointer<PDFDatabaseNotifier> m_notifier;
    QPointer<PDFAppInstanceServer> m_instanceServer;
    QPointer<PDFAppThumbnailer> m_thumbnailer;
    QPointer<PDFAppFolderInfoService> m_folderInfo;
    QPointer<PDFAppDocumentRegistry> m_documentRegistry;
    QPointer<PDFAppTranslator> m_translator;
    QPointer<PDFAppMinuteTicker> m_minuteTicker;
    std::unique_ptr<PDFAppAnnotationStyleStorage> m_annotationStyleStorage;
    bool m_isPagesEnabled = true;
};

}   // namespace pdf

#endif // PDFAPPCONTEXT_H
