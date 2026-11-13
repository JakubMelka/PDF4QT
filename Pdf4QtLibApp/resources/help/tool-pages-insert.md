---
title: Insert Pages
category: tools
order: 16
summary: sources, position and page ranges explained
keywords: insert pages, add pages, blank pages, replace pages, page range, position, insert pdf
related: /tools/pages.insert, /tools/pages.add-image, /help/tools
---
Insert Pages adds pages of other PDF files, images or blank pages to the open document, at the place you choose. The open document stays the same file: its pages, bookmarks, form fields, signatures and password protection are kept, the new pages are added to it.

### Step by step

1. Open the document and choose **Insert Pages** in Tools. Without a source the tool shows **Choose what to insert**.
2. Add the sources with **Add**: **PDF files…** (Ctrl+I), **Images…** (Ctrl+Alt+I), **Blank pages** (Ctrl+Shift+I), **Paste image** (Ctrl+V), or a document open in another tab. A tab with unsaved changes gives its unsaved state.
3. Choose the place in **Position**: before or after the current page, at the beginning, at the end, or **Replace selected pages** (available when pages are selected in Thumbnails). You can also click between two pages in the strip **Insert position**.
4. For each PDF source choose **All pages** or **Page range** in the right panel.
5. Press **Insert N Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

A source protected by a password shows **Enter Password…** and **Remove File** in its card; the tool runs when every source can be read. In the cards the pages of a PDF, which are inserted, are highlighted; the dimmed pages are not inserted. Click a page to add it to the range of its source or to take it away.

### Page ranges

A page range lists pages in the order they are inserted: `1-5, 7, 10-` inserts pages 1 to 5, page 7 and pages 10 to the last one. `-3` is the pages 1 to 3, `9-7` the pages 9, 8 and 7, `odd` and `even` the odd and the even pages. A page written twice is inserted twice.

### Blank pages and images

Blank pages have the size and the rotation of the page next to the place (**As page 12**, which starts in the orientation of that page), or a paper size; **Orientation** lays either of them in portrait or landscape (an A4 portrait page next to the place and Landscape give A4 landscape pages). An image becomes one page of the size of the page next to the place, the whole image fit inside 10 mm margins, in the middle, turned to the orientation of the image. Use **Add Image as Page** to choose another layout.

### What happens to the document

| Part | What happens |
| --- | --- |
| The open document | It keeps its bookmarks, labels, links, form fields, layers, attachments and settings. A signed document is saved after its signed part, so its signatures stay valid. |
| Password protection | The document stays protected with its password; the inserted pages are protected with it. |
| Bookmarks of the sources | Not added: Insert Pages adds pages, not bookmarks. Links inside an inserted source still work. |
| Page labels | Every page of the document keeps its label (“xiii” stays “xiii”); an inserted page gets its label from its source, else its page number. |
| Form fields | A field of a source with the name of a field of the document is renamed (“Name_2”); the fields of the document keep their names. A signature of a source is removed, its field stays empty. |
| Tags | When the document and every source are tagged and whole, the sources join the tags of the document. Otherwise the tags of the document stay and the result says “Inserted pages are not tagged”. |
| PDF/A, PDF/UA, PDF/X | The declaration of the document stays; the result says that the inserted pages were not checked. |
| Replaced pages | Removed with everything that leads to them, as by Remove Pages, in one step with the insertion. |

- A document certified by its author (a certification signature) allows no change of its pages: the result can only be saved as a copy, written whole and without the signatures. The right panel says so in a callout while you work in the tool, and the result says why again. The certified original stays as it is.
- A source, which restricts printing, copying or changes, needs its owner password: its pages would lose the restriction in the document.
- The pages come back with Undo until the document is closed.
