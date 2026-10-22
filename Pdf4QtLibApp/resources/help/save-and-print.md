---
title: Save, print and send
category: open-and-view
order: 50
summary: Save and Save as, signed documents, the automatic refresh, printing and sending a document by e-mail.
keywords: save, save as, signed, signature, incremental, read-only, refresh, reload, changed on the disk, print, printer, copies, paper, e-mail, mail, send
related: /help/read-documents, /help/keyboard-shortcuts
---
The commands of this topic are in the **File** panel of the toolbar of a document.

## Save and Save as

**Save** (Ctrl+S) writes the changes into the file of the document. The file is written safely: PDF4QT writes a new file next to it first and replaces the old file only when everything has been written, so a full disk or a crash never leaves a damaged file. You can keep reading and changing the document while it is being saved; a change made meanwhile stays unsaved.

**Save as** (Ctrl+Shift+S) writes the document into another file, and the tab continues with the new file. A copy made by **Edit a Copy** has no file yet, so its first **Save** asks for a file name.

A file, which cannot be written (it is read-only, or it is open in another tab), is not touched. PDF4QT tells you why and offers **Save As…** to keep your changes in another file.

## Signed documents

A document with digital signatures is saved as an *incremental update*: the signed versions stay in the file byte by byte and the new changes are appended after them, so the signatures can still be verified. The first **Save** of a signed document asks whether you want to save it so; **Save as Copy…** writes the signed document with your changes into another file and leaves the original untouched.

## Automatic refresh

When another program changes the file of an open document, PDF4QT reads the new version and shows it at the same place. The history of **Undo** starts again with the new version.

If you have unsaved changes, nothing is replaced without a question: **Reload and Lose Changes** shows the new version, **Keep My Version** leaves the file on the disk as it is - your next **Save** replaces it.

The automatic refresh is on for every newly opened document. **Automatic refresh** in the **File** panel turns it off and on for the current document.

## Print

**Print…** (Ctrl+P) opens the print dialog:

| Setting | What it does |
| --- | --- |
| Printer | The printer and its state; **Printer properties…** opens the settings of the driver of the printer (Windows) |
| Pages | All pages, the current page, the pages selected in the thumbnails or a range such as `1-3, 8`; even, odd, portrait or landscape pages only |
| Copies | The number of copies; **Collate** prints the copies one after another |
| Orientation | **Auto** prints each page on the paper of its own orientation |
| Paper size | The paper of the printer |
| Scaling | **Fit to page**, **Actual size** or a percentage of the actual size |
| Color | Color or grayscale |

The preview shows the selected pages on the paper with the current settings. When the thumbnails have a selection of pages, the dialog starts with these pages.

The document is printed in the background, so you can keep working with it. The bar at the top of the pages shows the progress; **Stop** stops the printing after the page being printed. When the printer stops responding, **Print the Rest…** opens the dialog again with the pages, which were not printed.

A document, whose author does not allow printing, cannot be printed; a document allowing only a low resolution is printed so, and the dialog says it.

## Send by e-mail

**Send by e-mail…** opens a new message of your e-mail program with the file of the document attached. The message takes the file from the disk, so a document with unsaved changes is saved first. On Linux the message is created by `xdg-email` of your desktop; on macOS the command is not available.
