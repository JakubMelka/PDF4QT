---
title: Where PDF4QT keeps your data
category: settings
order: 30
summary: One database of your settings and history, shared by all windows, and the logs.
keywords: database, data, storage, history, logs, folder, backup, export, import, older files, damaged, read only, try again
related: /settings/storage, /about, /help/portable-mode, /help/settings
---
PDF4QT keeps your settings, the history of documents, pinned files, workspaces and the thumbnails of pages in one database file of your user account. All open windows of PDF4QT use the same file, so a change made in one window is seen in the others at once.

## When the database cannot be used

A banner at the top of the pages of the Home tab tells you, when something is wrong with the database:

| Banner | What happened | What you can do |
| --- | --- | --- |
| The database could not be opened | PDF4QT works with a database in memory; changes are lost when it is closed | **Try Again**, or **Show in Folder** to look at the file |
| A newer version of PDF4QT | The file was written by a newer version; PDF4QT reads a copy and changes are lost when it is closed | **Check for Updates** |
| The database was damaged | PDF4QT renamed the damaged file and created a new one | **Show in Folder**, **Dismiss** |

## Backup of the settings

[Settings › Storage](/settings/storage) saves everything PDF4QT keeps for you into one file: **Export…** writes the settings, the shortcuts, the Recent files and folders, the workspaces, the bookmarks, the trusted certificates, the pinned tools, the tool history and the styles of the annotations. The backup contains the paths of your documents, not the documents. It is a consistent copy also while other windows of PDF4QT work.

**Import…** replaces all current settings and history with a backup. A backup of an older PDF4QT is updated first; a backup of a newer PDF4QT, a damaged file or a file, which is not a backup of PDF4QT, is refused and nothing is changed. Other open windows of PDF4QT use the imported settings at once. An import cannot be undone.

Before the import, the confirmation lists every security setting, which the backup weakens (for example **Allow application launch** Off → On). Certificates of the backup, which PDF4QT does not trust yet, are trusted only when you check **Also trust the certificates of the backup** - otherwise your current trusted certificates stay. The activity log of the AI assistants is not a part of a backup, and a file larger than 128 MB is not imported.

## Files of other versions

Each version of the database has its own file. A new version of PDF4QT starts from a copy of the file of the older version, which stays untouched, so you can go back to the older version. [Settings › Storage](/settings/storage) lists these files with the version of PDF4QT, which uses them. **Delete…** removes the file of an older version - only when no running PDF4QT uses it. The file of a newer version is kept: the newer PDF4QT keeps it up to date.

## Logs

PDF4QT writes a log file for every day into the folder of its data. Logs are kept for 7 days and they take at most 10 MB. The page [About](/about) shows the folders of the settings and of the logs. Access tokens, passwords and the content of documents are never written to the logs.

> [!NOTE] Settings of older versions
> The settings and the history of the older applications PDF4QT Viewer and Editor are not imported.
