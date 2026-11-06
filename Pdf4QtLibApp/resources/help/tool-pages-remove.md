---
title: Remove Pages
category: tools
order: 20
summary: Remove pages from a document, also what refers to them.
keywords: pages, remove, delete, discard, drop pages, trash
related: /tools/pages.remove, /help/tools
---
Remove Pages marks pages and removes them from the document. Together with the pages, everything that leads to them is removed, so no link, bookmark or field points at a page that no longer exists.

### Step by step

1. Choose the pages in **Pages to remove**: the current page, the pages selected in Thumbnails, a range such as `4-6, 20`, or all pages narrowed by Even, Odd, Portrait or Landscape. The tool opens with the pages selected in Thumbnails, else with the current page.
2. Click a page in the preview to mark it or to keep it. Shift marks the pages between the page you clicked before and this one; Delete marks the page with the keyboard focus.
3. **Marked for removal** lists the marked pages. **Keep** of a row keeps that page, **Keep All Pages** keeps all of them.
4. Press **Remove Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

A document must keep at least one page: when every page is marked, the tool says so and removes nothing.

When an AI assistant removes or moves pages while the tool is open, the marks stay on their pages: a moved page keeps its mark under its new number, a removed page loses its mark. A document protected by permissions must allow assembling it (inserting, rotating and deleting pages).

### What else is removed

| Part of the document | What happens |
| --- | --- |
| Bookmarks (outline) | An item that leads to a removed page is removed. An item with items below it that stay keeps them, without its own target. |
| Named destinations | The names of the removed pages are removed. |
| Links | A link to a removed page is removed; a button keeps itself without its action. |
| Form fields | A field whose widgets were all on removed pages is removed, also a signature field. |
| Layers | A layer used only by the removed pages is removed. |
| Page labels | Every page that stays keeps its label (“iii” stays “iii”). |
| Tags, articles, opening action | Removed from the whole document, as in PageMaster. A declaration of PDF/UA or PDF/A is removed with the tags and the result says so. |

- A page with a text layer of Recognize Text (OCR) that stays keeps its layer.
- Removed pages come back with Undo until the document is closed. To keep them in another file first, use Extract Pages.
- A document with a signature on a page that stays is saved after its signed part, so the signature stays: the removed pages are still inside the signed part of the file. When the last signed page is removed, the file is written whole and the removed pages are gone from it.
