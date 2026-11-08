---
title: Merge
category: tools
order: 12
summary: files, order and pages explained
keywords: merge, combine, join, unite, append, concatenate, several files, images, order, bookmarks, outline
related: /tools/pages.merge, /help/tools
---
Merge joins two or more PDF files and images into one document. Pick the files in any order, check the pages in the tool and press Create PDF. Then open the result in a new tab or save it as a file; the source files stay unchanged.

### Step by step

1. Choose the documents above – open tabs with one click, PDF files and images from disk, or drag and drop. The order of the list is the order in the result.
2. Open the tool with the button under the list. The tool lists the files on the left and shows every page of the result, grouped by file; drag a file in the list or a page in the preview to change the order.
3. Optionally rotate, clone or remove single pages, sort the files or reverse their order, and choose the outline of the result.
4. Press Create PDF. When it finishes, choose whether the new document opens in a new tab or is saved as a file.

### What happens to the files

- Pages are copied with their content, annotations, form fields and links; nothing is re-rendered, only Optimize encodes the images again.
- By default the result gets one bookmark per source file; Outline can also nest the bookmarks of each file under it, or leave the result without bookmarks.
- Encrypted documents ask for their password. The result is not encrypted unless you protect it afterwards, so a document that does not allow printing, copying or changes needs its owner password.
- The source files are never modified or deleted.

> [!WARNING] When a file cannot be opened
> A damaged or unsupported file is listed with the reason; remove it to continue.

> [!TIP] Interleave and Manage Pages
> Interleave Pages mixes the front and back sides of a scan; Manage Pages reorders, groups and saves an assembly for repeated use.

### The files and the pages

- **Add Files** adds PDF files and images (**Files…**, Ctrl+I), the picture of the clipboard (**Paste image**, Ctrl+V) and the open documents of the window. An open document is added as it is at that moment: its unsaved changes are used, later changes of the tab are not. A file, which is in the list already, is not added twice; use **Clone** to repeat a page.
- An image becomes a page of the size of its resolution (a scan of 300 dpi keeps its paper size); every page of a TIFF file is a page of the result.
- Drag a file in the list, or press Alt+Up and Alt+Down on it, to move it with its pages. **Delete** removes a file or the selected pages.
- In the preview, click a page to select it, Ctrl and Shift select more. Drag the selected pages to another place, or press Alt and an arrow. **Rotate Left**, **Rotate Right**, **Clone** and **Remove** work on the selected pages.
- **Sort** orders the files by their names (numbers in natural order), by their type (PDF documents first, then images), the pages by their original page numbers (the first pages of all files, then the second ones, …), or turns the current order around.
- **Details** shows the same pages as a table: the source, the type, the original page number, the size, the orientation and the rotation of each page.
- **Size** in the bar of the preview chooses the size of the pages: Small, Medium (the default) or Large. The choice is shared by every tool with a preview of pages.

Undo and Redo of the toolbar undo every change of the files, the pages and the options while the tool is open.

### The outline

| Outline | Result |
| --- | --- |
| Document parts | One bookmark per source file (a file, whose pages are in more places, gets a bookmark for each place) |
| Join outlines | The same bookmarks, with the bookmarks of each file nested under its bookmark |
| No outline | The result has no bookmarks |

With bookmarks, the document parts are also stored in the result as the document part structure of PDF 2.0.

### Forms, links and other parts

- Form fields are kept. A field, whose name is used by another file, is renamed (“Name_2”), so the fields of the files stay separate; the result says how many fields were renamed.
- Named destinations are kept the same way; links and bookmarks go to the pages of their own file.
- Signatures are not kept: the merged file is a new file, its signatures would not be valid. The value and the appearance of a signature are removed, the empty signature field stays for a new signature.
- Layers of all files are kept with their visibility.
- Tags (the structure for accessibility) are joined, when every PDF file is tagged and is taken whole; otherwise they are removed and the result says so.
- Scripts and actions run when the document opens are removed. Attachments are kept.
- A declaration of PDF/A or PDF/UA is not written: the result is not checked against the standard.
- **Optimize** recompresses the images of the result: Prefer quality uses the preset Balanced of Compress, Minimum size the preset Smaller file.

> [!NOTE] Locked documents
> A locked open document can be merged: Merge reads it and never changes it.

### Command line

The same operation is available in PdfTool for scripts and batch jobs:

```
pdftool unite Contract.pdf Appendix_A.pdf Appendix_B.pdf Contract_full.pdf
```

### Assistants

The tool is also available to AI assistants through the action `pages.merge` (see [AI tools](/help/ai-tools)): it merges open documents in the given order into a new tab or a new file.
