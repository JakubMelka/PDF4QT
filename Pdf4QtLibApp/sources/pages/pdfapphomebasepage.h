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

#include <memory>
#include <optional>

class QAction;
class QMenu;
class QTimer;

namespace pdf
{

class PDFAppNetworkPaths;
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
/// Network paths (PDFAppNetworkPaths: UNC, URLs, GVFS, mapped network drives,
/// network mounts) are never contacted by the page itself: no existence check, no
/// PDF count, no thumbnail request - their rows show the stored data without the
/// state "No longer exists" (the stored thumbnail of the last opening, if any). The
/// check and the thumbnail come with the opening of the item (an explicit action).
///
/// A request refused by the busy folder information service is asked again later
/// (it never stays pending). After a refresh, which has rebuilt the rows, the
/// keyboard focus returns to the same item (a row, a card or its nested button).
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
    /// Most decoded thumbnails kept by a page (the least recently used one goes first)
    static constexpr int MAXIMUM_THUMBNAILS = 512;

    /// Delay of a new request of the paths refused by the busy folder information service (ms)
    static constexpr int RETRY_INTERVAL = 250;

    /// Name of the dynamic property of a row or a card, which holds the path of its
    /// item (the focus returns to the item of the same path after a refresh)
    static constexpr const char* PATH_PROPERTY = "pdf4qtPath";

    /// Name of the dynamic property of a row or a card, which holds the id of its item
    static constexpr const char* ID_PROPERTY = "pdf4qtId";

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

    /// The path lies on a network file system (classified at the last refresh
    /// without touching it): the page does not check it, count it or render it
    bool isNetworkPath(const QString& path) const;

    /// Number of the decoded thumbnails kept by the page (tests)
    int getThumbnailCacheSize() const { return int(m_thumbnails.size()); }

    /// The decoded thumbnail of the path is kept (tests)
    bool hasCachedThumbnail(const QString& path) const { return m_thumbnails.contains(path); }

    /// Number of the paths refused by the folder information service, which are
    /// asked again (tests)
    int getRefusedRequestCount() const { return int(m_refusedRequests.size()); }

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
    /// Decoded thumbnail of a file
    struct ThumbnailEntry
    {
        QPixmap pixmap;
        qint64 version = 0;     ///< thumbnail_mtime of the decoded picture
        quint64 lastUse = 0;    ///< Order of the use (least recently used goes first)
    };

    /// Item, which had the keyboard focus before a refresh
    struct FocusState
    {
        QPointer<QWidget> item;         ///< Row or card of the item
        QPointer<QWidget> container;    ///< Parent of the row or card
        QPointer<QWidget> focused;      ///< Focused widget (the item or its nested widget)
        const char* property = nullptr; ///< PATH_PROPERTY or ID_PROPERTY
        QVariant value;                 ///< Path or id of the item
        QString itemName;               ///< Object name of the row or card
        QString focusedName;            ///< Object name of the focused nested widget
        QByteArray focusedClass;        ///< Class of the focused nested widget
        Qt::FocusReason reason = Qt::OtherFocusReason;
    };

    void onMenuActionTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onInfoReady(const pdf::PDFAppFolderInfo& info);
    void onRefreshTimer();
    void onRetryTimer();
    void onDialogFinished(int result);

    /// Asks the service for the information (marks the request, remembers a refusal)
    void requestInfo(const QString& path, bool countPdfFiles);

    FocusState captureFocus() const;
    void restoreFocus(const FocusState& state);

    QPointer<QTimer> m_refreshTimer;
    QPointer<QTimer> m_retryTimer;
    QPointer<QDialog> m_dialog;
    QPointer<QDialog> m_errorBox;
    QHash<QString, PDFAppFolderInfo> m_pathInfos;
    QHash<QString, bool> m_refusedRequests;     ///< Path -> count the PDF files
    QHash<QString, ThumbnailEntry> m_thumbnails;
    std::unique_ptr<PDFAppNetworkPaths> m_networkPaths;
    quint64 m_thumbnailUse = 0;
    int m_refreshCount = 0;
    bool m_isBrowsing = false;
};

}   // namespace pdf

#endif // PDFAPPHOMEBASEPAGE_H
