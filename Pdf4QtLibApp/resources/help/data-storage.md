---
title: Where PDF4QT keeps your data
category: settings
order: 30
summary: One database of your settings and history, shared by all windows, and the logs.
keywords: database, data, storage, history, logs, folder, backup, damaged, read only, try again
related: /about, /help/portable-mode, /help/settings
---
PDF4QT keeps your settings, the history of documents, pinned files, workspaces and the thumbnails of pages in one database file of your user account. All open windows of PDF4QT use the same file, so a change made in one window is seen in the others at once.

## When the database cannot be used

A banner at the top of the pages of the Home tab tells you, when something is wrong with the database:

| Banner | What happened | What you can do |
| --- | --- | --- |
| The database could not be opened | PDF4QT works with a database in memory; changes are lost when it is closed | **Try Again**, or **Show in Folder** to look at the file |
| A newer version of PDF4QT | The file was written by a newer version; PDF4QT reads a copy and changes are lost when it is closed | **Check for Updates** |
| The database was damaged | PDF4QT renamed the damaged file and created a new one | **Show in Folder**, **Dismiss** |

## Logs

PDF4QT writes a log file for every day into the folder of its data. Logs are kept for 7 days and they take at most 10 MB. The page [About](/about) shows the folders of the settings and of the logs. Access tokens, passwords and the content of documents are never written to the logs.

> [!NOTE] Settings of older versions
> The settings and the history of the older applications PDF4QT Viewer and Editor are not imported.
