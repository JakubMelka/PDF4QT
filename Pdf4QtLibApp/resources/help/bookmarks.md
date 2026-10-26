---
title: Bookmarks and the sidebar
category: open-and-view
order: 45
summary: Thumbnails, outline, layers, attachments, your own bookmarks, the notes and the other panels of a document in the sidebar.
keywords: sidebar, thumbnails, pages, outline, contents, chapters, layers, optional content, attachments, embedded files, bookmarks, my bookmarks, notes, comments, rotate pages, export bookmarks, import bookmarks, follow the current page, thumbnail size, search results, speech, signatures, sidebar icon size
related: /help/read-documents, /help/notes-and-outline, /help/keyboard-shortcuts, /settings/general#sidebar-icon-size
---
The sidebar at the left of a document has a row of icons at its top. Each icon shows one panel. An icon is shown only when the document has something for its panel: a document without an outline has no **Outline** icon. F4 shows or hides the whole sidebar.

[Settings › General](/settings/general#show-sidebar-when-opening-documents) decides whether a new document opens with the sidebar (**Show sidebar when opening documents**) and how large its icons are (**Sidebar icon size**).

## Thumbnails

**Thumbnails** shows small pictures of the pages. Click a page to show it. Hold Ctrl or Shift to select more pages; the header then says how many pages are selected. The slider in the header (**Thumbnail size**) changes the size of the pictures, and the button next to it, **Follow the current page**, keeps the current page in view while you read.

The pictures of large documents are drawn while you scroll, so a page shows a plain sheet for a moment before its picture appears.

Right-click the selected pages for more actions:

| Action | What it does |
| --- | --- |
| Rotate Left, Rotate Right | Rotates the pages of the document by 90 degrees. Undo (Ctrl+Z) returns it |
| Insert Pages…, Move to…, Extract to New Document, Remove Pages | Open the page tools |

A locked document cannot be rotated; unlock it or use **Edit a Copy** first. The selected pages are also the pages, which the [print dialog](/help/save-and-print#print) offers first.

## Outline

**Outline** is the table of contents of the document. Click an item to go to its page. The item of the page you read is highlighted. Type into *Search outline…* to find a chapter. To add, rename, move or delete items, see [Edit notes and the outline](/help/notes-and-outline).

## Layers

Some documents, for example technical drawings, have layers. Switch a layer off to hide its content. The change applies only to the view; the document is not changed. **Ignore optional content** in [Settings › Viewing](/settings/viewing#ignore-optional-content) shows the content of all layers regardless of their state.

## Attachments

**Attachments** lists the files embedded in the document. The arrow at the right of a file saves it.

- A click on a PDF file opens it in a new tab of PDF4QT. The document is read directly from the attachment, no file is written to your disk. The tab works like a copy: it has no file yet, it is not added to your recent files, and **Save** asks where to save it.
- A click on any other file saves it. PDF4QT never opens other files from a document - a file from an unknown document can contain malware. Save it and check it first.

A saved attachment keeps the mark of a file from the internet (Windows), when the document has it, so your system warns you before you open it.

## Bookmarks

Your bookmarks mark the pages you want to return to. They are kept on your computer, not in the document, so you can bookmark any file, also one you are not allowed to change. A document, which has not been saved to a file yet, has no bookmarks.

| Action | How |
| --- | --- |
| Add a bookmark of the current page | Ctrl+M, or the button with the bookmark in the header of **Bookmarks** |
| Go to the next or previous bookmark | Ctrl+. and Ctrl+, |
| Go to a bookmark | Click it, or select it and press Enter |
| Rename, move or remove a bookmark | Right-click it: **Go to Page**, **Rename…**, **Move Up**, **Move Down**, **Remove**. F2 renames and Delete removes the selected bookmark |
| Export or import bookmarks | **Export bookmarks…** and **Import bookmarks…** in the button **⋯** in the header of **Bookmarks** |

**Generate automatically** in the menu **⋯** adds a bookmark of each main chapter of the outline, marked *Auto*. These bookmarks follow the outline and they are not saved. The option is off by default.

The bookmarks are exported to a JSON file, which you can import, for example for the same document on another computer. The panel **Go to** of the toolbar has the bookmarks too: **Add**, **Previous**, **Next**, the export, the import and **Generate automatically**.

## Notes

**Notes** lists the comments and markups of the document by their pages, with their authors and dates. Click a note to go to its page. Type into *Search notes…* to find a note by its author or text. To add notes, see [Annotate a document](/help/annotations); to reply to them, hide and delete them in the panel, see [Edit notes and the outline](/help/notes-and-outline).

## Other panels

| Panel | What it shows |
| --- | --- |
| Search results | The occurrences of a search in the document, see [Search in a document](/help/search#search-in-a-document) |
| Speech | Reading the document aloud, see [Read aloud](/help/read-aloud) |
| Signatures | The digital signatures and timestamps of a signed document, see [Digital signatures](/help/signatures) |
