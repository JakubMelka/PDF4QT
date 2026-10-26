---
title: Where PDF4QT keeps your data
category: settings
order: 30
summary: One database of your settings and history, shared by all windows, and the logs.
keywords: database, data, storage, settings database, state, history, logs, folder, backup, export settings, import settings, older files, other database files, delete, damaged, read only, in memory, try again
related: /settings/storage, /about, /help/portable-mode, /help/settings
---
PDF4QT keeps your settings, the history of documents, pinned files, workspaces and the thumbnails of pages in one database file of your user account. All open windows of PDF4QT use the same file, so a change made in one window is seen in the others at once.

## Settings › Storage

[Settings › Storage](/settings/storage) shows where your data are and what you can do with them:

| Card | What it shows |
| --- | --- |
| Settings database | **Location** – the file of the database, **Show in Folder** opens its folder; **State** – how PDF4QT uses the file |
| Backup | **Export settings** and **Import settings** – a backup of everything in one file, see below |
| Other database files | The files of other versions of PDF4QT, see below |

The **State** is one of these:

| State | Meaning |
| --- | --- |
| Saved on this computer | The normal state: every window uses this file and sees each change at once |
| Portable – nothing is written | The [portable mode](/help/portable-mode): settings and history live in memory until PDF4QT closes |
| Read only – newer version | A newer PDF4QT saved the settings; this version reads them, but does not change them |
| In memory – kept until PDF4QT closes | The database could not be opened; changes are kept only until PDF4QT closes |

## When the database cannot be used

A banner at the top of the pages of the Home tab tells you, when something is wrong with the database:

| Banner | What happened | What you can do |
| --- | --- | --- |
| Settings could not be opened – changes are kept only until PDF4QT closes | The database is used by another program or it cannot be read. PDF4QT started with the default settings in memory | **Try Again**, or **Show in Folder** to look at the file |
| Settings are read only – they were saved by a newer PDF4QT | A newer version wrote the file. This version reads it, but does not change it, so the newer version keeps working; your changes last until PDF4QT closes | **Check for Updates** |
| The settings were damaged and have been reset | PDF4QT kept the damaged file under another name and created a new one with the default settings. Your documents are not affected | **Show in Folder**, **Dismiss** |

## Backup of the settings

[Settings › Storage](/settings/storage) saves everything PDF4QT keeps for you into one file: **Export…** asks for a file and writes the settings, the shortcuts, the Recent files and folders, the workspaces, the bookmarks, the trusted certificates, the pinned tools, the tool history and the styles of the annotations. The backup contains the paths of your documents, not the documents. It is a consistent copy also while other windows of PDF4QT work. After the export, a bar *Settings exported to “…”* below the card has **Show in Folder**. The next Export… or Import… starts in the folder you used last.

**Import…** replaces all current settings and history with a backup. A backup of an older PDF4QT is updated first; a backup of a newer PDF4QT, a damaged file or a file, which is not a backup of PDF4QT, is refused and nothing is changed. Other open windows of PDF4QT use the imported settings at once. An import cannot be undone.

Before the import, the confirmation *Import settings from the backup?* shows the file, the date and the version of the backup and lists every security setting, which the backup weakens (for example **Allow application launch** Off → On). Certificates of the backup, which PDF4QT does not trust yet, are listed with their SHA-256 fingerprints and trusted only when you check **Also trust the … certificates of the backup** – otherwise your current trusted certificates stay. **Import Settings** confirms the import. The activity log of the AI assistants is not a part of a backup, and a file larger than 128 MB is not imported.

## Files of other versions

Each version of the database has its own file. A new version of PDF4QT starts from a copy of the file of the older version, which stays untouched, so you can go back to the older version. *Other database files* in [Settings › Storage](/settings/storage) lists these files with the version of PDF4QT, which uses them, their size and date, for example *PDF4QT 2.0 · 2.4 MB · Oct 12, 2026*. **Delete…** asks *Delete the older database file?* and **Delete File** removes it; that version then starts with the default settings. A file, which a running PDF4QT uses, cannot be deleted. The file of a newer version has the tag *Newer version* and it cannot be deleted here: the newer PDF4QT keeps it up to date.

## Logs

PDF4QT writes a log file for every day into the folder of its data. Logs are kept for 7 days and they take at most 10 MB. The page [About](/about) shows the folders of the settings and of the logs. Access tokens, passwords and the content of documents are never written to the logs.

> [!NOTE] Settings of older versions
> The settings and the history of the older applications PDF4QT Viewer and Editor are not imported.
