---
title: Remove Hidden Data
category: tools
order: 50
summary: Remove the data a reader does not see, before you share a document.
keywords: hidden data, metadata, sanitize, privacy, remove author, document info, comments, attachments, ocr text
related: /tools/protect.sanitize, /help/tools
---
Remove Hidden Data removes what a reader does not see on the pages: the author and the dates of the document, its metadata, comments, attached files and other kinds of data you choose. The text and the images of the pages stay as they are.

### What can be removed

The tool looks at the document first. A kind, which is not in the document, has the tag Not found and cannot be ticked.

| Kind | What goes |
| --- | --- |
| Document info | Author, title, subject, keywords, producer and dates – also their copies in the XMP metadata of the document; its other metadata stay |
| All metadata | The XMP metadata streams of the document and of its pages, images and fonts |
| Comments and markup | Comments, highlights and the other markup annotations with their replies and pop-up windows |
| File attachments | The files embedded in the document and in its annotations |
| Outline | The bookmarks of the document |
| Page labels | A custom numbering of the pages such as i–xii, 1–1,580 |
| Embedded search index | A search index stored in the file |
| Page thumbnails | The small pictures of the pages stored in the file |
| Invisible text (OCR layer) | The text layer of scanned pages. Search and copy stop working on these pages |
| Signature fields | Shown only for a signed document: the signature fields with the names, reasons and certificates of the signatures |

### Presets

A preset only ticks the list, nothing runs. You can change any row afterwards; the label then says Custom.

| Preset | Ticks |
| --- | --- |
| Standard | Everything except the invisible text |
| Before sending | Info, metadata, comments, index and thumbnails |
| Before publishing | The same and the attachments; the outline and the page labels stay |
| Everything | Every kind, including the OCR text layer and the signature fields |
| Clear all | Nothing: choose by hand |

### Step by step

1. Choose the document. The tool scans it and shows what it has found.
2. Tick the kinds you want to remove, or pick a preset.
3. Press Remove Hidden Data. The summary shows each removed kind and the size before and after.
4. Save the result as a copy (recommended), keep it in this tab or overwrite the original file.

### What happens to the file

- The file is written whole: old revisions of the file, which an incremental save would keep, are not carried over, so what was removed is really gone from the file.
- A signed document can be saved only as a copy. Its signatures are no longer valid in the copy; tick Signature fields to remove them altogether.
- A declaration of PDF/A or PDF/UA is removed: the result is not checked against the standard.
- A document protected by permissions must allow changes. Comments, the attachments of comments and the signature fields also need the permission to change annotations and form fields; without it Remove Hidden Data stays disabled while they are ticked, and the status bar says which kinds to untick.
- Visible content of the pages is not removed. Use Redact Content for it.

> [!WARNING] The invisible text of scanned pages
> When you remove the invisible text, the scanned pages become pictures only: you cannot search them, select their text or have them read aloud any more.

