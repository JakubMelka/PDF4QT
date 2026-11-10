---
title: Rotate Pages
category: tools
order: 10
summary: Turn the pages of a document in the file.
keywords: pages, rotate, turn, landscape, portrait, upside down, sideways, reset rotation
related: /tools/pages.rotate, /help/tools
---
Rotate Pages turns pages of the document in steps of 90°, and the rotation is saved in the file: everyone who opens it sees the pages turned. To turn only what you see, use **View › Rotate**, which is not saved.

### Step by step

1. Choose the pages in **Pages to rotate**: all, the current page, the pages selected in Thumbnails or a range such as `1-3, 8`, narrowed by Even, Odd, Portrait or Landscape. The tool opens with the pages selected in Thumbnails, else with the current page. A range with an empty field selects no page.
2. Click a page in the preview to add it to the selection or to take it away. Shift adds the pages between the page you clicked before and this one.
3. Press **Rotate Left**, **Rotate Right** or **Rotate 180°**. Every click turns the selected pages by another step. **Reset Rotation** turns them back to the rotation 0.
4. The preview shows the pages as they will be saved, **Pending rotation** lists them. **Undo** of a row leaves that page as it is.
5. Press **Rotate Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

Undo and Redo of the toolbar undo the steps of the tool while it is open; after the result is kept in the tab, Undo of the document brings the original pages back.

### The size of the thumbnails

**Thumbnails** in the toolbar chooses the size of the pages in the preview: **Small** shows the most pages on the screen at once, **Medium** (the default) shows them one and a half times larger, **Large** twice as large, with more detail of each page. The choice is remembered for the next time and for every tool with a preview of pages.

When an AI assistant removes or moves pages while the tool is open, the pending rotations stay with their pages: a moved page keeps its turn under its new number, a removed page loses it. A document protected by permissions must allow assembling it (inserting, rotating and deleting pages).

### What happens to the file

- Only the rotation of each turned page changes. The content, the annotations and the form fields of the page turn with it.
- A page with a text layer of Recognize Text (OCR) keeps its layer: search and copy keep working after the rotation.
- A signed document stays signed: the rotation is saved after the signed part of the file. The signature then shows that the document was changed after signing.
- A document certified by its author (a certification signature) allows no change of its pages: the result can only be saved as a copy, written whole and without the signatures, and the tool says why. The certified original stays as it is.

> [!TIP] Scans on their side
> Choose **All** and **Landscape**: the selection is every page that lies on its side, and one click on Rotate Left or Rotate Right fixes the whole scan.

### Assistants

The tool is also available to AI assistants through the action `pages.rotate` (see [AI tools](/help/ai-tools)).
