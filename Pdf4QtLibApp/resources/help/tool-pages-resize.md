---
title: Resize Pages
category: tools
order: 24
summary: paper size, margins, anchor and scaling explained
keywords: resize, page size, paper size, scale, margins, anchor, a4, letter, orientation
related: /tools/pages.resize, /tools/pages.crop, /help/tools
---
Resize Pages changes the paper size of pages: the content is scaled to fit or keeps its size, placed by the margins and the anchor.

### Step by step

1. Choose the pages in **Pages to resize** and click a thumbnail of the left panel to see that page: **Now** and **After** show it before and after the change.
2. **Size** chooses the new paper: A5, A4, A3, Letter, Legal, **Custom** (the width and the height appear in the right panel) or **Keep page size** (only the margins and the placement change).
3. **Orientation** of a named size: **Auto** gives a landscape sheet to a page, which is shown landscape, **Portrait** and **Landscape** give every page the same orientation.
4. **Content: Scale to fit** enlarges or reduces the content to fit inside of the **Margins**; **Keep size** only places it - the content is not rewritten, the page shows the whole new size.
5. **Anchor** places the content inside of the margins; **Offset X** moves it to the right, **Offset Y** down. **Preserve aspect ratio** keeps the proportions of the content, **Scale comments and form fields** scales them with it (otherwise they only move with it).
6. Press **Resize Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

### What happens to the file

- The visible area of each page (its crop box) is placed on the new sheet. What the crop box hid stays hidden.
- With Scale to fit, everything, which points to a place on the page, moves with the content: links and bookmarks to the page, comments and form fields, measurements and their scale, article threads and the layout of the tags. A measurement shows the same real distance after the change.
- The content is not wrapped into a new object: the tags of an accessible document keep their content on the page.
- Margins, which leave no room for the content, are refused with the size, which fits.
- A page turned by 90° is measured as it is shown: its new sheet lies as it is seen.
- Moving or scaling comments and form fields needs the permission to change them, when the document is protected by permissions.
- A signed document stays signed: the change is saved after the signed part of the file. A certified document can be changed only as a copy, without the certification and the signatures; the right panel says so in a callout while you work in the tool.

### Assistants

The tool is also available to AI assistants through the action `pages.resize` (see [AI tools](/help/ai-tools)).
