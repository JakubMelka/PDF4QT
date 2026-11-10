---
title: Split
category: tools
order: 13
summary: rules, file names and existing files explained
keywords: split, separate, divide, every page, every n pages, bookmarks, file size, odd, even, file names
related: /tools/pages.split, /tools/pages.extract, /help/tools
---
Split separates a document into several PDF files: every page, every few pages, at chosen pages, at bookmarks or by file size. The open document is not changed.

### Step by step

1. Choose the rule in **Split** of the toolbar. **Files to create** lists the files with their pages and names, the preview shows a card of each file with its pages. Click a file in the list to show its card.
2. With **Every N pages**, type the number into the field **pages per file** of the toolbar; with **By file size**, the size into the field **MB**.
3. With **At selected pages**, click between two pages in the preview: a new file starts there, the dot below each page shows its file. Click the cut line to remove it, or edit the numbers in **Cuts**. **Clear Cuts** removes every cut.
4. With **At bookmarks**, tick the bookmarks in **Start a new file at**; **Top level** chooses the bookmarks of the top level.
5. Choose the folder, the file name and what happens with existing files in **Output files**. The line under the name shows the first and the last name.
6. Press **Split into Files**. The files are written one after another, the card **Writing the files** shows which one; the files already written are marked **Written**. **Stop** ends the export after the file, which is being written.

### The rules

| Split | New file starts |
| --- | --- |
| Every page | at every page |
| Every N pages | after the number of pages of **Pages per file** (10 by default); the last file can be shorter |
| At selected pages | at the pages of **New file starts at pages**, such as `10, 20, 35` |
| At bookmarks | at the page of each top-level bookmark, or of the bookmarks you tick; the pages before the first bookmark join the first file |
| By file size | when the file would be larger than **File size** (10 MB by default); a single page larger than that is a file of its own |
| Odd and even pages | two files: the odd pages and the even pages |

At most 10,000 files are created at once. **Optimize** can encode the images of the files again to make them smaller: **Prefer quality** reduces them to 200 dpi, **Minimum size** to 110 dpi with stronger JPEG compression.

The size of the page thumbnails is chosen in **Thumbnails**; Split starts with **Small**, a size you choose is kept for the next time.

### File names

The file name is a pattern; the extension `.pdf` is added.

| In the name | Is replaced by |
| --- | --- |
| `#` | the number of the file, `##` pads it with zeros (`01`, `02`, …) |
| `@` | the number of the first page of the file |
| `{source_base}` | the name of the document without its extension |
| `{group_name}` | the title of the bookmark (At bookmarks) |
| `{date}` | the date of the export |

Characters, which cannot be in a file name, are replaced by `_`. When two files would get the same name, the tool says so before anything is written: the files are marked **Duplicate name** and **Split into Files** stays disabled until the names differ.

### Existing files

| Existing files | What happens |
| --- | --- |
| Ask | At the first file, which exists, the tool asks: **Overwrite**, **Skip**, **Rename** or **Stop Export**; the answer can apply to all other files |
| Overwrite | The existing files are replaced |
| Skip | The existing files are kept, these files are not written |
| Rename | The new files get ` (2)`, ` (3)`, … in their names |

The open document and the other open documents are never overwritten.

### The result

- Every file is a new document: its pages keep their content, annotations, form fields, links, page labels and the bookmarks of its pages. Attachments of the document are only in the first file.
- Signatures are not kept, because a part of a signed document is not the signed document; the empty signature fields stay.
- When the export finishes, **Open Folder** shows the files. A stopped export lists what was written and offers **Delete Written Files**.
- A document protected by permissions must allow assembling it.
