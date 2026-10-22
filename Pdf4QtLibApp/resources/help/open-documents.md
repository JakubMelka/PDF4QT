---
title: Open documents
category: open-and-view
order: 10
summary: The page Open, drag and drop, the file manager and the command line.
keywords: open, browse, drag, drop, file, command line, page, arguments, pdf
related: /open, /help/recent-files, /help/workspaces
---
Every document opens in its own tab. A document, which is already open in the window, is not opened twice: PDF4QT switches to its tab.

## The page Open

[Open](/open) in the sidebar collects everything you need to open a document:

- **Drop PDF files here** – drop one or more files on the area, or click **Browse Files…** to choose them in the dialog of your system.
- **Recent Folders** – the standard folders of your system first, then the folders you opened files from recently.
- **Pinned Files** and **Recent Files** – the files you pinned by the star and the files you opened last.
- **Workspaces** and **Pinned Locations** at the side, see [Workspaces and sessions](/help/workspaces).

## Drag and drop

You can drop PDF files from the file manager on the page Open, and each of them opens in its own tab.

## From the file manager

When you open a PDF file by PDF4QT from the file manager of your system and PDF4QT is already running, the file opens as a new tab of the running window, which comes to the front.

## From the command line

```
PDF4QT [file…] [--page <n>] [--portable] [--new-window]
```

| Option | Meaning |
| --- | --- |
| `file…` | Files to open; relative paths are relative to the current folder |
| `--page <n>` | Open the files at the physical page `n` (the first page is 1); a page out of the range is ignored |
| `--portable` | Write nothing to the disk, see [Portable mode](/help/portable-mode) |
| `--new-window` | Start a separate window, which does not receive the files of other starts |

> [!WARNING] A file, which cannot be opened
> A missing file or an invalid argument never stops PDF4QT: the window, which opens the files, lists them in one message.

## While a document opens

The tab of a document appears at once and the file is read in the background: a large file shows its progress in the tab and in a line at the bottom of the tab. The opening cannot be stopped, but you can close the tab meanwhile. While a dialog of PDF4QT is open, the files wait and open when you close it.

## The page, where a document opens

A document opens at the page you give it (`--page` of the command line, the page saved in a workspace). Without a page it opens at the page, where you left it the last time (the list of [Recent files](/help/recent-files) shows it as *Page 12 of 24*); a page, which the document does not have, is ignored.

## Documents protected by a password

A document, which needs a password, shows the screen **This document is protected** instead of its pages. Type the password and press **Enter** or **Open document**; the eye button shows the typed password. A wrong password is cleared and the screen says *Incorrect password*. **Close tab** closes the tab. The password is kept only in the memory of the tab (an automatic refresh of the file uses it again) and never written anywhere.

A document, which is encrypted with an owner password only, opens without a question; its tab shows the icon of a password.

## Files, which cannot be opened

Files, which do not exist, folders, files, which are not PDF documents, and damaged files are listed in one message *N files could not be opened* with the reason of each of them, together with the options of the command line, which were not used. **Copy Details** copies the list as a text.

## Locked documents

A locked document cannot be changed by accident: its tab shows a lock and the commands, which change the document, are turned off. Lock a document by **Lock document** of the panel **File**; click the lock in the tab to **Unlock** it or to **Edit a Copy** – a new tab with a copy of the document, which is not saved to a file yet, while the original stays locked. The lock is remembered with the file, so the document is locked again, when you open it next time.
