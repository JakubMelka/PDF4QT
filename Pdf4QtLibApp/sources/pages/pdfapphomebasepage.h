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

#ifndef PDFAPPHOMEBASEPAGE_H
#define PDFAPPHOMEBASEPAGE_H

#include "pdfappglobal.h"
#include "pdfappfolderinfo.h"
#include "pdfappinfowidgets.h"
#include "pdfrecentrepository.h"

#include <QDialog>
#include <QHash>
#include <QPixmap>
#include <QPointer>

#include <optional>

class QAction;
class QMenu;
class QTimer;

namespace pdf
{

class PDFTextLink;

/// Kind of an item of Home, Open and Recent, which has a menu (941:137569)
enum class PDFAppHomeItemKind
{
    File,       ///< Recent or pinned file
    Folder,     ///< Recent folder (also a standard folder)
    Workspace,  ///< Saved workspace
    Location    ///< Pinned location
};

/// Action of a menu of an item (941:137569)
enum class PDFAppHomeAction
{
    Open,               ///< Open (a file, a workspace)
    Pin,                ///< Pin a file
    Unpin,              ///< Unpin a file
    ShowInFolder,       ///< Show in Folder (the file manager of the system)
    CopyPath,           ///< Copy Path
    RemoveFromList,     ///< Remove from List (the file or folder stays on the disk)
    BrowseHere,         ///< Browse files here… (the dialog of the system in the folder)
    PinLocation,        ///< Pin to Pinned Locations
    UnpinLocation,      ///< Unpin from Pinned Locations
    Rename,             ///< Rename… (a workspace, a location)
    Delete,             ///< Delete… (a workspace)
    RemoveLocation      ///< Remove from Pinned Locations
};

/// Item, whose menu is shown
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHomeItem
{
public:
    PDFAppHomeItemKind kind = PDFAppHomeItemKind::File;
    QString path;           ///< File or folder (as stored)
    qint64 id = 0;          ///< Workspace or location
    QString name;           ///< Name (dialogs)
    bool isPinned = false;  ///< A pinned file, a folder pinned as a location
    bool isRemovable = true;///< Remove from List is possible (a standard folder never used is not)
    int documentCount = 0;  ///< Documents of a workspace (Delete…)
};

/// Base of the pages Home, Open and Recent (Figma 28:4, 61:1201, 80:910): reading
/// of the database when the page is shown and after a notification (coalesced,
/// nothing is cached - newapp_storage.md 1), the existence of files and the PDF
/// counts of folders asked from PDFAppFolderInfoService, the thumbnails requested
/// from PDFAppThumbnailer and read from the database, the menus of the items
/// (941:137569) and their actions, the dialogs of workspaces, of pinned locations
/// and of the history, the opening of files (PDFAppMainWindow::openDocuments) and
/// the native file and folder dialogs. Ctrl+O browses files on these pages.
///
/// Lifetime: a menu, a dialog or a native dialog may outlive a navigation, which
/// deletes the page; every continuation is guarded (QPointer), an open dialog is
/// closed with the page, a pending notification or result of a worker reaches
/// nobody after the page is gone (connections with the page as the context).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHomeBasePage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    explicit PDFAppHomeBasePage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppHomeBasePage() override;

    /// Actions of the menu of the item in the order of the design (941:137569);
    /// a separator lies before RemoveFromList, Delete and RemoveLocation
    static QList<PDFAppHomeAction> getMenuActions(const PDFAppHomeItem& item);

    /// Text and icon of an action of the menu
    static QString getActionText(PDFAppHomeAction action);
    static QString getActionIconName(PDFAppHomeAction action);

    /// Creates the menu of the item (a child of \p parent, deleted with it)
    QMenu* createItemMenu(const PDFAppHomeItem& item, QWidget* parent);

    /// Performs the action of the item. \returns false, if the page has been deleted by it.
    bool performAction(const PDFAppHomeItem& item, PDFAppHomeAction action);

    /// Opens the file (the window records it in Recent)
    void openFile(const QString& path, int page = 0);

    /// Browse files…: the native dialog in the folder (empty = the default
    /// folder of the system); the window opens the chosen files
    /// (PDFAppMainWindow::browseDocuments, the command Open… Ctrl+O)
    void browseFiles(const QString& folder = QString());

    /// Opens the documents of the workspace and marks it opened
    void openWorkspace(qint64 id);

    /// Restore last session: opens its documents and forgets the session
    void restoreLastSession();

    /// Pins / unpins a recent file
    void setFilePinned(const QString& path, bool isPinned);

    /// Pins a folder as a location / unpins it
    void setFolderPinned(const QString& path, bool isPinned);

    /// Add… of Pinned Locations: the native folder dialog
    void addPinnedLocation();

    /// Dialogs (shown without blocking; one at a time)
    void showSaveWorkspaceDialog();
    void showRenameWorkspaceDialog(qint64 id);
    void showDeleteWorkspaceDialog(qint64 id);
    void showRenameLocationDialog(qint64 id);
    void showClearHistoryDialog();

    /// Dialog shown by the page (nullptr = none)
    QDialog* getDialog() const { return m_dialog; }

    /// Message box of an error of the database shown by the page (nullptr = none)
    QDialog* getErrorBox() const { return m_errorBox; }

    /// Reads the database now (a refresh is otherwise coalesced after a notification)
    void refreshNow();

    /// A refresh is scheduled
    bool isRefreshPending() const;

    /// Number of the reads of the database (tests: notifications are coalesced)
    int getRefreshCount() const { return m_refreshCount; }

    /// Text of the effective shortcut of the command Open… of the window ("Ctrl+O")
    QString getBrowseShortcutText() const;

protected:
    /// Reads the database and shows it (called by refreshNow)
    virtual void refresh() = 0;

    /// The page shows data of the topic of the notifier
    virtual bool isTopicRelevant(const QString& topic) const;

    /// The information about a path has arrived (the page updates its widgets)
    virtual void onPathInfoChanged(const QString& path);

    /// Schedules a refresh (coalesced)
    void scheduleRefresh();

    /// Information about the path asked since the page was shown (std::nullopt = not known yet)
    std::optional<PDFAppFolderInfo> getPathInfo(const QString& path) const;

    /// Asks for the existence of a file / the PDF count of a folder (once per showing of the page)
    void requestPathInfo(const QString& path);
    void requestFolderInfo(const QString& path);

    /// Requests the thumbnail of the file (the thumbnailer skips an up-to-date one)
    void requestThumbnail(const PDFRecentFile& file);

    /// Picture of the first page of the file read from the database (decoded once
    /// per version of the thumbnail of the file); a null pixmap without a thumbnail
    QPixmap getThumbnail(const PDFRecentFile& file);

    /// Current time for the texts of the times
    qint64 getNow() const;

    /// Creates a text link with the route
    PDFTextLink* createLink(const QString& text, const PDFAppRoute& route, QWidget* parent);

    /// Shows the dialog without blocking (it replaces the previous one); the page
    /// handles its result in onDialogFinished
    void showDialog(QDialog* dialog);

    /// Shows the error of the database (nothing for no error)
    void showDatabaseError(const PDFDatabaseError& error);

    virtual void showEvent(QShowEvent* event) override;

private:
    void onMenuActionTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onInfoReady(const pdf::PDFAppFolderInfo& info);
    void onRefreshTimer();
    void onDialogFinished(int result);

    QPointer<QTimer> m_refreshTimer;
    QPointer<QDialog> m_dialog;
    QPointer<QDialog> m_errorBox;
    QHash<QString, PDFAppFolderInfo> m_pathInfos;
    QHash<QString, QPixmap> m_thumbnails;
    QHash<QString, qint64> m_thumbnailVersions;
    int m_refreshCount = 0;
    bool m_isBrowsing = false;
};

}   // namespace pdf

#endif // PDFAPPHOMEBASEPAGE_H
