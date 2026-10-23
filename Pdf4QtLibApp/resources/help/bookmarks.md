---
title: Bookmarks and the sidebar
category: open-and-view
order: 45
summary: Thumbnails, outline, layers, attachments, your own bookmarks and the notes of a document in the sidebar.
keywords: sidebar, thumbnails, pages, outline, contents, chapters, layers, optional content, attachments, embedded files, bookmarks, my bookmarks, notes, comments, rotate pages, export bookmarks, import bookmarks
related: /help/read-documents, /help/keyboard-shortcuts
---
The sidebar at the left of a document has a row of icons at its top. Each icon shows one panel. An icon is shown only when the document has something for its panel: a document without an outline has no **Outline** icon. F4 shows or hides the whole sidebar.

## Thumbnails

**Thumbnails** shows small pictures of the pages. Click a page to show it. Hold Ctrl or Shift to select more pages; the header then says how many pages are selected. The slider in the header changes the size of the pictures, and the button next to it keeps the current page in view while you read.

The pictures of large documents are drawn while you scroll, so a page shows a plain sheet for a moment before its picture appears.

Right-click the selected pages for more actions:

| Action | What it does |
| --- | --- |
| Rotate Left, Rotate Right | Rotates the pages of the document by 90 degrees. Undo (Ctrl+Z) returns it |
| Insert Pages…, Move to…, Extract to New Document, Remove Pages | Open the page tools |

A locked document cannot be rotated; unlock it or use **Edit a Copy** first.

## Outline

**Outline** is the table of contents of the document. Click an item to go to its page. The item of the page you read is highlighted. Type into *Search outline…* to find a chapter.

## Layers

Some documents, for example technical drawings, have layers. Switch a layer off to hide its content. The change applies only to the view; the document is not changed.

## Attachments

**Attachments** lists the files embedded in the document. The arrow at the right of a file saves it.

- A click on a PDF file opens it in a new tab of PDF4QT. The document is read directly from the attachment, no file is written to your disk. The tab works like a copy: it has no file yet, it is not added to your recent files, and **Save** asks where to save it.
- A click on any other file saves it. PDF4QT never opens other files from a document - a file from an unknown document can contain malware. Save it and check it first.

A saved attachment keeps the mark of a file from the internet (Windows), when the document has it, so your system warns you before you open it.

## Bookmarks

Your bookmarks mark the pages you want to return to. They are kept on your computer, not in the document, so you can bookmark any file, also one you are not allowed to change.

| Action | How |
| --- | --- |
| Add a bookmark of the current page | Ctrl+M, or the button with the bookmark in the header of **Bookmarks** |
| Go to the next or previous bookmark | Ctrl+. and Ctrl+, |
| Rename, move or remove a bookmark | Right-click it; F2 renames and Delete removes the selected bookmark |
| Export or import bookmarks | The button **⋯** in the header of **Bookmarks** |

**Generate automatically** in the menu **⋯** adds a bookmark of each main chapter of the outline, marked *Auto*. These bookmarks follow the outline and they are not saved. The option is off by default.

The bookmarks are exported to a JSON file, which you can import for the same document on another computer.

## Notes

**Notes** lists the comments and markups of the document by their pages, with their authors and dates. Click a note to go to its page. Type into *Search notes…* to find a note by its author or text. To add or change notes, see [Annotate a document](/help/annotations).
