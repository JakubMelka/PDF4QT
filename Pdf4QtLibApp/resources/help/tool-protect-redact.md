---
title: Redact Content
category: tools
order: 45
summary: Permanently remove text, images and areas from a copy of the document.
keywords: redact, redaction, black out, remove text, censor, hide information, permanently remove
related: /tools/protect.redact, /tools/protect.sanitize, /help/tools
---
Redact Content removes the marked areas from a new copy of the document. Every page of the copy is rebuilt, so the removed content is gone from the file, not only covered. The open document is not changed: it keeps its content and its marks.

### Step by step

1. Mark what should disappear. Area: drag a rectangle over the page. Text: drag over the text. Pages…: mark whole pages. Search…: find a text, choose the matches and press Mark Selected Matches.
2. The marks are listed by pages in the left panel. Click a mark in the list or on the page to select it, press its × or Delete to remove it. Clear All removes every mark.
3. Change an area mark on the page as an annotation: drag it to move it on its page, Ctrl + drag copies it, drag its handles to resize it, the arrows nudge it. A text mark and a whole page mark keep their shape; they can only be removed. A locked mark cannot be changed.
4. Choose the Fill of the removed areas (black, white, any color or no fill) and what else the copy Keeps: the title, the metadata, the outline.
5. Marks | Preview result at the top of the right panel shows the pages as the copy will look.
6. Press Create Redacted Copy and save the copy. Its name ends with _REDACTED.

Esc ends a drag or the mode Area, Text or Search…; without them it clears the selected mark; when none of them is active, Esc closes the tool.

### What is removed

- Everything under a marked area: text, images (their pixels, not only a cover), drawings and shadings. A marked page becomes a blank page of the same size.
- Content outside the visible area of a page (its crop box).
- Comments, form fields, links, attachments, bookmarks, the structure and the other hidden data: the copy contains the pages only. The title, the metadata and the outline are copied only when they are checked in Keep; the title is not copied when it is unchecked, also with the metadata.
- Signatures: the copy is a new document, it is not signed.

> [!WARNING] Text becomes shapes
> The text of the copy is drawn as shapes. It looks the same, but it cannot be searched, selected or copied, and it is not read aloud.

### The check after the write

The copy is checked after it is written: it is read back, the marked areas are drawn again and the removed words are searched in it. When anything under a marked area is still there, the copy is not created. A kept title, metadata or outline, which still contains a removed word, is said in the result.

### Marks are annotations

The marks are redaction annotations of the document. They are saved with the document, so you can finish the redaction later, and other programs show them as redaction marks. Each new or removed mark is one step of Undo of the document. When you close the tool with marks, it asks whether to keep them in the document or to remove them.

The tool draws only the marked areas filled by its Fill. An overlay text or an overlay appearance of a mark made by another program (/OverlayText, /RO) is not drawn into the copy.

### Search

- Match case, Whole words and Regular expression work as in the search of the document.
- Across line breaks finds a phrase also when a line ends inside of it: every space of the text matches any white space, the end of a line too. A regular expression is used as it is: \s matches the end of a line.
- A word divided by a soft hyphen at the end of a line is always found as one word.
- At most 10,000 matches are listed, a search text has at most 1,024 characters.

Nothing of the search is remembered: the text you searched for, the matches and the marked areas are never saved in the settings, the recently used values or the history of the tools.

### Command line

PdfTool `redact` creates the redacted copy from the redaction annotations of a document.
