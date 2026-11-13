---
title: Manage Pages
category: tools
order: 18
summary: assemblies, groups and outputs explained
keywords: manage pages, arrange, reorder, rearrange, group, regroup, assembly, pagemaster, clone, sort, checkpoint, removed pages, pdf4qtassembly
related: /tools/pages.manage, /tools/pages.merge, /help/tools
---
Manage Pages arranges the pages of one or more documents: reorder, group, clone, rotate and remove pages, then create one PDF, a PDF per group or a PDF per page. Started over an open document, it rearranges the pages of that document. It replaces PageMaster.

### Step by step

1. Start **Manage Pages** from Tools without a document, or open a saved assembly. The tab **New assembly** offers **Add Files…** and the open documents, and the recent assemblies.
2. Add the sources with **Add**: **Files…** (Ctrl+I, PDF files and images), **Pages of a PDF…**, **Blank page** (Ctrl+Shift+I), **Paste image** (Ctrl+V) or an open document. The left panel lists the sources with the pages each one gives to the result ("16 of 31 pages used").
3. Arrange the pages on the canvas: drag them, use the menus **Select**, **Group** and **Sort**, or the commands of **More** and of the context menu. Each page shows its position in the result ("A · 6").
4. Choose the output in **Output**: **One PDF**, **One PDF per group or item** or **One PDF per page**, with the outline and the images.
5. Press **Create PDF** (Ctrl+Enter). The source files are never changed and the assembly stays open, so you can make another output.

### Groups and the order

- **Group** (Ctrl+G) makes a group of the selected pages at the place of the first one; **Ungroup** (Ctrl+Shift+G) keeps the pages where they are. **Rename…** (F2) names a group or a page.
- **Regroup the selection into groups**: odd and even pages, page pairs, **Every N pages…** (17 pages by 6 give groups of 6, 6 and 5 pages), **At page numbers…** (7, 13 give the groups 1–6, 7–12 and 13–17), **By bookmarks…** of one PDF, **Alternate two files** (front and back sides), **Alternate, second file reversed** and **Reverse order**. The positions count in the selection; the new groups stay at the place of the selection.
- **Sort** orders the selection by the file name (numbers in natural order, "file2" before "file10"), by the source, by the original page number or by the type, or reverses it. With less than two selected items Sort and Regroup work with all items.
- **Select** › **Odd pages** and **Even pages** use the position in the assembly, as the pages show it; **Page range…** selects by the position, or by the original pages of one source.
- **Clone** (Ctrl+L) adds another copy of the selected pages after them. **Replace selection…** (Ctrl+Alt+R) replaces the selected pages by pages of another file in one step.
- **Remove** (Del) moves the selection to **Removed pages**. **Restore** puts an item back after the item, which was before it; **Restore all** (Ctrl+Shift+R) restores everything. Removed pages are kept until the assembly is closed.
- **Save checkpoint** keeps the order under a name; **Restore** returns to it as one step of Undo. Two checkpoints may have the same name.
- **Cut**, **Copy** and **Paste** move and repeat pages inside the same assembly; pages of another assembly are not pasted.

Undo and Redo of the toolbar undo every change; the tooltip names the step ("Undo Group").

### The outputs

| Output | Result |
| --- | --- |
| One PDF | One new document; open it in a new tab or save it as a file |
| One PDF per group or item | A file for every group and for every page outside of a group, named by **File name** (`##_{group_name}` gives 01_Introduction.pdf, …) |
| One PDF per page | A file for every page |

| Outline | Result |
| --- | --- |
| Document parts | One bookmark per group (with its name) or run of pages of one file; a single blank page or image belongs to the part before it |
| Join outlines | The same bookmarks with the bookmarks of the sources nested under them |
| No outline | No bookmarks |

**Images** can be recompressed (Prefer quality, Minimum size). Forms, links and attachments are handled as in Merge: the result is a new document without the signatures of the sources, and a source, which restricts printing, copying or changes, needs its owner password.

### The assembly file

**Save Assembly** (Ctrl+S) writes the sources, the order, the groups, the checkpoints and the output to a `.pdf4qtassembly` file; it is not a PDF. **Open Assembly…** (Ctrl+Shift+O), the list **Recent assemblies**, the page Open and a double click on the file open it again. Images are stored in the file; PDF files are referenced by their path, and by their path relative to the assembly, so a folder moved with its assembly still works. A PageMaster workspace (`.pagemaster`) is not opened.

When a source has changed or is missing, the assembly asks first:

- **Use Changed File** takes the new version; pages beyond its new page count go to Removed pages.
- **Locate…** chooses the file at its new place.
- A file on another computer (a network share) is never opened by itself: choose **Open From There** or **Locate…**.
- **Open Without Them** opens the assembly; the pages of the missing files are listed under Removed pages.

> [!WARNING] Files from others
> An assembly file only names its PDF sources. PDF4QT reads only the files it lists on this computer and never contacts another computer by itself, not even to check that a file is there.

### Rearranging an open document

Started over an open document (Tools, Thumbnails › **Move pages**, or **Then continue with**), Manage Pages works in the tab of that document. On the start page of the tool, a list with just one open document offers a link such as **Edit Invoice.pdf in its tab**, which opens the tool there; an assembly tab never changes its sources. The document is the source A ("this document"); each page shows its position and its page ("3 · p. 7"). **Rearrange Pages** (Ctrl+Enter) changes the document in one step, which Undo takes back; the result can stay in the tab, be saved as a copy or overwrite the file. **Open as an Assembly** opens a new assembly tab with a copy of the document, for groups and several files.

| Part | What happens |
| --- | --- |
| The open document | Its objects keep their numbers, so its bookmarks, links, labels and form fields stay with their pages. Every page keeps its label. |
| Signatures | A signed document is saved after its signed part: the signatures stay valid and viewers list the page changes made after signing. A cloned page gets no signature field. |
| Certified document | A certification allows no change of the pages: the result can only be saved as a copy, without the signatures. The right panel says so in a callout while you arrange the pages. |
| Password protection | The document stays protected; the change needs the permission to assemble the document. Without it the tool still opens and the pages can be arranged, but **Rearrange Pages** stays unavailable until you choose **Enter owner password…** in the right panel and unlock the document with its owner password. |
| Tags | The reading order of the tags is not changed. Cloned pages are not tagged. |
| Document parts | Removed, when the order of the pages changes. |

> [!NOTE] Locked documents
> A locked document cannot be rearranged; unlock it or open it as an assembly and create a new PDF.

### Limits

An assembly has at most 65,536 pages, the removed pages included; a file or a document with more pages cannot be added or rearranged here (Merge and Insert take up to 1,000,000 pages). An assembly has at most 100 checkpoints, and they keep at most 983,040 pages together, so that an assembly file with all its checkpoints (at most 1,048,576 pages) always opens again; **Save checkpoint** tells you when there is no room left.

### Keyboard

| Key | Action |
| --- | --- |
| Arrows, Space | Move the focus between the pages, select a page |
| Alt+arrows | Move the selection |
| F4, Shift+F4 | Rotate right, rotate left |
| Del | Remove |
| Ctrl+L | Clone |
| Alt+M | More |
| Ctrl+B | Show page boxes |
| F6, Shift+F6 | Switch between the toolbar, the panels and the pages (forward, back) |
| Ctrl+S | Save the assembly (in the tab of a document: save the document) |
| Ctrl+Enter | Create PDF (in the tab of a document: Rearrange Pages) |

### Assistants

The tool is also available to AI assistants through the action `pages.manage` (see [AI tools](/help/ai-tools)): it rearranges an open document by a list of all its pages ("3, 1-2, 4-"; a page named twice is cloned, a page cannot be left out), or creates new documents from a saved assembly, whose PDF files are open in PDF4QT.
