---
title: Portable mode
category: settings
order: 20
summary: Run PDF4QT without writing settings or history to the computer.
keywords: portable, usb, stick, no installation, nothing saved, memory, private
related: /help/data-storage, /help/open-documents
---
In the portable mode PDF4QT writes nothing to the disk of the computer: no settings, no history and no thumbnails. Everything is kept in memory and it is gone, when PDF4QT is closed.

## Start the portable mode

Start PDF4QT with the option `--portable`:

```
PDF4QT --portable
```

## What is different

- Every page of the Home tab shows a banner of the portable mode, and the footer has the tag **Portable** next to the version.
- PDF4QT starts with the default settings, and your changes last until it is closed.
- The window does not receive files of other starts of PDF4QT, and it does not pass its own files to another window.
- The access token of the AI server is new at every start.
- No log files are written; the messages go only to the standard error output.

> [!TIP] Your documents are not affected
> The portable mode concerns only the data of PDF4QT. Documents you save are saved as usual.
