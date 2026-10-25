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

#ifndef PDFRECENTREPOSITORY_H
#define PDFRECENTREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QMetaType>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>
#include <vector>

namespace pdf
{

class PDFSettingRegistry;
class PDFDatabaseStatement;

/// State of the thumbnail of a recent file (column thumbnail_state of recent_file).
/// Every state except Pending is recorded together with thumbnail_mtime = the time of
/// the modification of the file, from which it was made: the thumbnailer does not try
/// the file again, until the file changes.
enum class PDFRecentThumbnailState
{
    Pending = 0,            ///< Not made yet (or the file has changed)
    Ready = 1,              ///< The thumbnail of page 1 is stored
    PasswordProtected = 2,  ///< The document needs a password to open, it has no thumbnail
    Failed = 3              ///< Damaged, too large, no pages, too slow - no thumbnail
};

/// Reading state of a recent file (Figma Recent 80:910: "Page 12 of 24", "Finished", "Not started")
enum class PDFRecentReadingState
{
    NotStarted,     ///< Never further than page 1
    InProgress,     ///< "Page N of M"
    Finished        ///< The last page has been shown (reached_end)
};

/// Sort order of the recent files (route /recent?sort=last-opened|name|folder)
enum class PDFRecentSort
{
    LastOpened,     ///< Newest first
    Name,           ///< By the name of the file, then by the folder
    Folder          ///< By the folder, then by the name
};

/// Kind of a recent folder (Figma Open 61:1201, Recent Folders). The standard
/// folders of the system are listed first in the order Documents, Downloads,
/// Desktop; the page maps the kind to the hue of the folder card (Documents
/// Green, Downloads Blue, Desktop Violet, Other Neutral).
enum class PDFRecentFolderKind
{
    Other = 0,
    Documents = 1,
    Downloads = 2,
    Desktop = 3
};

/// Kind of a pinned location (meta text of the folder card: "This PC",
/// "Personal cloud", "Network" - getLocationKindText)
enum class PDFPinnedLocationKind
{
    Local = 0,
    Cloud = 1,
    Network = 2
};

/// One row of the recent files (table recent_file). Paths are stored with '/'
/// separators: show them by QDir::toNativeSeparators. Times are milliseconds
/// since the epoch (UTC), 0 = unknown.
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentFile
{
public:
    qint64 id = 0;
    QString path;           ///< Canonical absolute path of the file
    QString name;           ///< Name of the file ("Report.pdf")
    QString folder;         ///< Folder of the file
    qint64 lastOpened = 0;
    qint64 openCount = 0;
    bool pinned = false;
    qint64 pinOrder = 0;    ///< Position among the pinned files (0 = not pinned)
    int pageCount = -1;     ///< -1 = unknown (the thumbnailer fills it)
    qint64 fileSize = -1;   ///< Size in bytes at the last opening, -1 unknown
    qint64 fileModified = 0;///< Time of the modification of the file at the last opening
    int lastPage = 0;       ///< Last page shown (from 1), 0 = unknown
    bool reachedEnd = false;///< The last page has been shown (Finished)
    QString viewState;      ///< State of the panel View of the document (step 3), opaque text
    bool locked = false;    ///< The document lock applies at the next opening (step 3)
    bool hasThumbnail = false;
    qint64 thumbnailModified = 0;   ///< thumbnail_mtime (time of the file of the thumbnail), 0 = none
    PDFRecentThumbnailState thumbnailState = PDFRecentThumbnailState::Pending;

    PDFRecentReadingState getReadingState() const;

    /// Reading progress of 0 to 1 (Finished = 1, unknown page count = 0)
    qreal getProgress() const;
};

/// Thumbnail of a recent file
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentThumbnail
{
public:
    QByteArray png;         ///< PNG of page 1 (at most MAXIMUM_THUMBNAIL_DIMENSION pixels), empty = none
    qint64 fileModified = 0;///< thumbnail_mtime
    PDFRecentThumbnailState state = PDFRecentThumbnailState::Pending;

    /// Decodes the PNG (empty image for an invalid or oversized picture)
    QImage toImage() const;
};

/// Marker of a thumbnail being rendered (table thumbnail_rendering, no
/// triggers): PDFAppThumbnailer writes it before it reads and renders a
/// document and removes it afterwards. A marker, whose process has ended, was left
/// by a crash of the rendering - the file gets the state Failed until it changes,
/// so a document killing the process is not rendered again at every start.
class PDF4QTLIBAPPSHARED_EXPORT PDFThumbnailRenderingMarker
{
public:
    QString path;           ///< Path of the recent file (as stored)
    qint64 fileModified = 0;///< Time of the modification of the file, which is rendered
    QString instance;       ///< Id of the thumbnailer (random, one per thumbnailer object)
    qint64 processId = 0;   ///< Process of the thumbnailer
    qint64 started = 0;     ///< Start of the rendering (ms since the epoch, UTC)
};

/// Recent folder (table recent_folder, or a standard folder of the system)
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentFolder
{
public:
    QString path;
    QString name;           ///< Display name (standard folders: QStandardPaths::displayName)
    PDFRecentFolderKind kind = PDFRecentFolderKind::Other;
    qint64 lastUsed = 0;    ///< 0 = never used (a standard folder, which is not stored)

    bool isStandard() const { return kind != PDFRecentFolderKind::Other; }
};

/// Pinned location (table pinned_location)
class PDF4QTLIBAPPSHARED_EXPORT PDFPinnedLocation
{
public:
    qint64 id = 0;
    QString path;
    QString name;
    PDFPinnedLocationKind kind = PDFPinnedLocationKind::Local;
    qint64 position = 0;
};

/// Query of the recent files
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentQuery
{
public:
    bool pinnedOnly = false;
    PDFRecentSort sort = PDFRecentSort::LastOpened;

    /// Text, which the name or the path must contain (case insensitive); empty = all
    QString text;

    /// At most this number of files, -1 = all
    int limit = -1;
};

/// What Clear history removes (dialog 941:135742)
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentClearSummary
{
public:
    int removedFiles = 0;       ///< Recent files removed (without the pinned ones, unless they are included)
    int removedFolders = 0;     ///< Recent folders forgotten
    int removedWorkspaces = 0;  ///< Saved workspaces leaving Recent (they stay in Open › Workspaces)
    int pinnedFiles = 0;        ///< Pinned files ("Also remove the pinned items (3)")
    bool hasLastSession = false;///< Restore last session is cleared
    bool hasContinueReading = false;    ///< Continue where you left off is cleared

    /// "Removed: 11 entries"
    int getRemovedEntries() const { return removedFiles + removedFolders + removedWorkspaces; }
};

/// Recent files, recent folders and pinned locations (newapp_storage.md 4, tables
/// recent_file, recent_folder, pinned_location; Figma Home 28:4, Open 61:1201,
/// Recent 80:910, Clear history 941:135742). A value object: every read goes to the
/// database (no cache), every write is one short BEGIN IMMEDIATE transaction, whose
/// commit wakes PDFDatabaseNotifier, which notifies all instances (topics
/// getFileTopic, getFolderTopic, getPinnedLocationTopic - the names of the tables;
/// the key is the path of the file / folder or the id of the location). Clear
/// history also changes the table workspace (topic "workspace").
///
/// Usable in any thread (each thread has its own connection). Functions never
/// throw; errors of the database are returned (busy: another instance writes).
/// Rows are untrusted (another version, a damaged file): invalid rows are skipped.
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentRepository
{
public:
    /// Longest stored PNG of a thumbnail, which is read (a bigger blob is ignored)
    static constexpr int MAXIMUM_THUMBNAIL_BYTES = 2 * 1024 * 1024;

    /// Longest edge of a thumbnail in pixels (newapp_storage.md 4)
    static constexpr int MAXIMUM_THUMBNAIL_DIMENSION = 320;

    /// Longest stored view state (characters)
    static constexpr int MAXIMUM_VIEW_STATE_LENGTH = 65536;

    /// Recent folders, which are not standard folders of the system
    static constexpr int MAXIMUM_RECENT_FOLDERS = 20;

    /// Rows of pinned locations, which are read
    static constexpr int MAXIMUM_PINNED_LOCATIONS = 1000;

    /// Most rows of recent files, which a list reads (getFiles, getPinnedFiles,
    /// getContinueReading - LIMIT in their SQL): a database written by another
    /// program cannot make a page read an unbounded table
    static constexpr int MAXIMUM_FILE_ROWS = 10000;

    /// Largest page number, which is accepted
    static constexpr int MAXIMUM_PAGE = 100000000;

    static QString getFileTopic() { return QStringLiteral("recent_file"); }
    static QString getFolderTopic() { return QStringLiteral("recent_folder"); }
    static QString getPinnedLocationTopic() { return QStringLiteral("pinned_location"); }

    /// \param registry Registry of the settings (recent.maxFiles); nullptr = default 10
    PDFRecentRepository(PDFDatabase* database, const PDFSettingRegistry* registry);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    // ------------------------------------------------------------------ files

    /// Records an opened file: the canonical absolute path, name, folder, size and
    /// time of the modification (read from the file system), last_opened = now,
    /// open_count + 1. The folder of the file is recorded as a recent folder. The
    /// non-pinned files over the setting recent.maxFiles are deleted (the oldest
    /// first, pinned files never). The call of PDFAppMainWindow::openDocuments.
    /// \param storedPath Receives the path as it is stored (the key of the row,
    ///        of the notifications and of PDFAppThumbnailer::requestThumbnail)
    PDFDatabaseError recordOpened(const QString& filePath, QString* storedPath = nullptr);

    /// Removes the file from the list (Remove from List)
    PDFDatabaseError removeFile(const QString& path);

    /// Deletes the non-pinned files over the setting recent.maxFiles (the oldest
    /// first, pinned files never) - a lowered limit of Settings › General trims the
    /// history at once (step 5)
    PDFDatabaseError applyLimit();

    /// Pins / unpins a recorded file. A pinned file goes to the end of the pinned
    /// files; an unpinned one becomes subject to the limit of the recent files.
    PDFDatabaseError setPinned(const QString& path, bool pinned);

    /// Order of the pinned files: the listed paths first in this order, the other
    /// pinned files after them in their previous order (unknown paths are ignored)
    PDFDatabaseError setPinnedOrder(const QStringList& paths);

    /// Reading state (step 3): last page shown (from 1) and the page count;
    /// \p reachedEnd marks the document Finished (it stays Finished)
    PDFDatabaseError setReadingState(const QString& path, int lastPage, int pageCount, bool reachedEnd);

    /// State of the panel View of the document (step 3), at most MAXIMUM_VIEW_STATE_LENGTH characters
    PDFDatabaseError setViewState(const QString& path, const QString& viewState);

    /// Lock of the document at the next opening (step 3)
    PDFDatabaseError setLocked(const QString& path, bool locked);

    /// The recorded file of the path (path as stored, see PDFRepositoryHelper::normalizePath)
    std::optional<PDFRecentFile> getFile(const QString& path) const;

    /// Recent files by the query (Home, Open, Recent; the pinned ones included,
    /// unless pinnedOnly)
    std::vector<PDFRecentFile> getFiles(const PDFRecentQuery& query = PDFRecentQuery()) const;

    /// Pinned files in their order
    std::vector<PDFRecentFile> getPinnedFiles() const;

    /// Continue where you left off: files in progress (a page after the first,
    /// not finished), newest first. The page shows the first one, which exists.
    std::vector<PDFRecentFile> getContinueReading(int limit = 5) const;

    /// Number of the recent files / of the pinned ones (chips of Recent)
    int getFileCount() const;
    int getPinnedCount() const;

    // ------------------------------------------------------------- thumbnails

    /// Thumbnail of the file (an oversized blob is not read)
    PDFRecentThumbnail getThumbnail(const QString& path) const;

    /// Stores the result of the thumbnailer: the PNG (empty for a state without a
    /// picture), the time of the modification of the file, from which it was made,
    /// the state and the page count (-1 = unknown, not changed). Nothing is written,
    /// when the row does not exist or the stored values are the same.
    PDFDatabaseError setThumbnail(const QString& path, const QByteArray& png, qint64 fileModified, PDFRecentThumbnailState state, int pageCount);

    /// Longest instance id of a rendering marker
    static constexpr int MAXIMUM_INSTANCE_LENGTH = 64;

    /// Most rendering markers, which getRenderingMarkers reads
    static constexpr int MAXIMUM_RENDERING_MARKERS = 1000;

    /// Marker of the rendering of the file (std::nullopt = none or an invalid row)
    std::optional<PDFThumbnailRenderingMarker> getRenderingMarker(const QString& path) const;

    /// Markers of all renderings (invalid rows skipped), at most MAXIMUM_RENDERING_MARKERS
    std::vector<PDFThumbnailRenderingMarker> getRenderingMarkers() const;

    /// Writes the marker of a rendering, unless the file has one (another instance
    /// renders it, or a crashed rendering left it). \p written = the marker was
    /// written by this call (one statement, so of two instances only one wins).
    PDFDatabaseError beginRendering(const PDFThumbnailRenderingMarker& marker, bool* written);

    /// Removes the marker of the file written by the instance (nothing for another instance)
    PDFDatabaseError endRendering(const QString& path, const QString& instance);

    /// The rendering of the marker ended its process (a crash): the file gets the
    /// state Failed with thumbnail_mtime = the time of the file of the marker (no
    /// retry until the file changes), unless a result of the same version of the
    /// file is stored already; the marker is removed. One transaction; nothing is
    /// changed, when the marker has been replaced meanwhile. \p failed = the state
    /// of the file was changed to Failed.
    PDFDatabaseError failRendering(const PDFThumbnailRenderingMarker& marker, bool* failed = nullptr);

    // ----------------------------------------------------------------- folders

    /// Folders of Open: the standard folders of the system first (Documents,
    /// Downloads, Desktop - also never used ones), then at most
    /// MAXIMUM_RECENT_FOLDERS other folders, last used first
    std::vector<PDFRecentFolder> getFolders() const;

    /// Folders used recently (rows of recent_folder, also standard folders, which
    /// were used), last used first - Recent, filter Folders
    std::vector<PDFRecentFolder> getUsedFolders() const;

    /// Records a used folder (a file was opened from it, Browse files here…)
    PDFDatabaseError recordFolder(const QString& folderPath);

    /// Forgets a used folder (Remove from List). A standard folder stays in
    /// getFolders, only its time of use is forgotten.
    PDFDatabaseError removeFolder(const QString& folderPath);

    /// Standard folders of the system (QStandardPaths), without duplicates
    static std::vector<PDFRecentFolder> getStandardFolders();

    // -------------------------------------------------------- pinned locations

    /// Pinned locations in their order
    std::vector<PDFPinnedLocation> getPinnedLocations() const;

    /// Pins a location. \p name empty = the name of the folder. A location with
    /// the same path is refused (\p isDuplicate set to true).
    PDFDatabaseError addPinnedLocation(const QString& folderPath, const QString& name, qint64* id = nullptr, bool* isDuplicate = nullptr);

    /// Renames a location (the name is normalized, an empty name is refused; a
    /// location, which does not exist - removed by another instance - is an error)
    PDFDatabaseError renamePinnedLocation(qint64 id, const QString& name);

    /// Removes a location
    PDFDatabaseError removePinnedLocation(qint64 id);

    /// Order of the locations: the listed ids first, the others after them
    PDFDatabaseError setPinnedLocationOrder(const QList<qint64>& ids);

    /// A location with the path is pinned
    bool hasPinnedLocation(const QString& folderPath) const;

    /// Kind of a location from its path (no access to the file system): UNC and
    /// smb / network URLs are Network, folders of cloud clients (OneDrive, Dropbox,
    /// Google Drive, iCloud Drive, Nextcloud, ownCloud, pCloud, MEGA, Box) Cloud
    static PDFPinnedLocationKind detectLocationKind(const QString& folderPath);

    /// Translated meta text of a location ("This PC", "Personal cloud", "Network")
    static QString getLocationKindText(PDFPinnedLocationKind kind);

    // ----------------------------------------------------------- clear history

    /// What clearHistory would remove
    PDFRecentClearSummary getClearHistorySummary(bool includePinned) const;

    /// Clear history of Recent (941:135742, newapp_storage.md 4): removes the
    /// recent files (pinned ones only with \p includePinned), forgets the recent
    /// folders, the times of opening of the saved workspaces (they leave Recent,
    /// they stay in Open › Workspaces) and the workspace of the last session
    /// (Restore last session). Kept pinned files forget their reading position,
    /// so Continue where you left off is cleared too. Thumbnails of the removed
    /// files are removed with them.
    PDFDatabaseError clearHistory(bool includePinned);

    // ----------------------------------------------------------------- helpers

    static QString getSortId(PDFRecentSort sort);
    static std::optional<PDFRecentSort> parseSort(QStringView id);

private:
    qint64 getCurrentTime() const;
    int getMaximumFiles() const;
    PDFDatabaseConnection* getConnection() const;
    static bool readFile(const PDFDatabaseStatement& statement, PDFRecentFile& file);
    static bool readRenderingMarker(const PDFDatabaseStatement& statement, PDFThumbnailRenderingMarker& marker);
    std::vector<PDFRecentFile> readFiles(const char* sql) const;
    PDFDatabaseError pruneFiles(PDFDatabaseConnection* connection) const;
    PDFDatabaseError pruneFolders(PDFDatabaseConnection* connection) const;
    PDFDatabaseError recordFolderImpl(PDFDatabaseConnection* connection, const QString& folderPath) const;
    PDFDatabaseError updateFile(const char* sql, const QString& path, const QVariant& first, const QVariant& second = QVariant(), const QVariant& third = QVariant());

    QPointer<PDFDatabase> m_database;
    const PDFSettingRegistry* m_registry = nullptr;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFRecentThumbnailState)

#endif // PDFRECENTREPOSITORY_H
