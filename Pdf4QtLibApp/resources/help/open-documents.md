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
> A missing file or an invalid argument never stops PDF4QT: the window, which opens the files, shows a message.
