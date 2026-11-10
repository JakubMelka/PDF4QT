---
title: Interleave Pages
category: tools
order: 15
summary: front and back sides of a scan explained
keywords: interleave, alternate, front and back, duplex scan, double sided, reversed back sides
related: /tools/pages.interleave, /tools/pages.merge, /help/tools
---
Interleave Pages mixes the front sides and the back sides of a scan into one document in the order of the paper.

### Step by step

1. Choose two PDF files above – the first one is the file with the front sides, the second one the file with the back sides. Open documents are added as they are at that moment.
2. Open the tool. The panel Sources shows the two scans in their places, **Front sides** and **Back sides**; **Swap** exchanges them, **Change…** replaces a file by another file or open document.
3. Check the first pages of the result in the preview: blue pages come from the front sides, green pages from the back sides, each with its page in its scan (“front 1”, “back 6”).
4. Press Create PDF. When it finishes, choose whether the new document opens in a new tab or is saved as a file.

### The order of the pages

The usual one-sided scan of a double-sided stack scans the front sides first, then the flipped stack with the back sides – from the last sheet to the first. **Back sides: Reversed** (the default) reads them that way: front 1, back 6, front 2, back 5, … for 6 sheets. **In order** pairs front 1 with back 1, front 2 with back 2, and so on.

| Back sides | 3 front and 3 back sides |
| --- | --- |
| Reversed | front 1, back 3, front 2, back 2, front 3, back 1 |
| In order | front 1, back 1, front 2, back 2, front 3, back 3 |

When the files have a different number of pages, some sides have no partner. **Extra pages: At the end** places them after the mixed pages, **Leave out** does not use them. The panel says how many sides are without a partner – check whether a sheet was skipped while scanning.

### What happens to the files

- Pages are copied with their content, annotations, form fields and links, as Merge copies them; nothing is re-rendered.
- The result has no bookmarks: bookmarks of the scans are not kept, and the result says so.
- Encrypted documents ask for their password. The result is not encrypted, so a document that does not allow printing, copying or changes needs its owner password.
- Only PDF files can be interleaved; an image is listed as not supported. The source files are never modified.

> [!TIP] More than two files
> Merge joins any number of files in any order; Manage Pages reorders and groups pages and saves the assembly.
