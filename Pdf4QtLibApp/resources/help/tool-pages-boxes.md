---
title: Set Page Boxes
category: tools
order: 26
summary: media, crop, bleed, trim and art box explained
keywords: page boxes, media box, crop box, bleed box, trim box, art box, bleed, prepress
related: /tools/pages.boxes, /tools/pages.crop, /help/tools
---
Set Page Boxes writes the media, crop, bleed, trim and art box of pages for print production. The content is not touched.

### The boxes

- **Media box** - the whole sheet.
- **Crop box** - what viewers show and print.
- **Bleed box** - the trim plus the bleed area, which a printer cuts off.
- **Trim box** - the finished page after cutting.
- **Art box** - the meaningful content.

A page, which has no bleed, trim or art box, uses its crop box for them; a box larger than the media box counts only inside of the media box.

### Step by step

1. Choose the pages in **Pages** and click a thumbnail of the left panel to see that page with its boxes. The toggle of the toolbar shows or hides the boxes in the preview.
2. **Based on** chooses the box, from which the new box is measured.
3. **Write to** chooses the boxes, which get the new box - more boxes at once. Every checked box gets the same rectangle.
4. Type the four margins of **New box = … +**: positive values make the new box larger than the box Based on, negative values smaller. The margins are as the page is shown.
5. **Boxes of page …** lists the boxes of the page of the preview, a box written has its new size.
6. Press **Set Boxes**. The result can stay in this tab, be saved as a copy or overwrite the original file.

A new box larger than the media box is written, but viewers and printers cut it at the media box: the warning says the largest margin, which fits. Add **Media box** to Write to to make the sheet larger too.

### What happens to the file

- Only the boxes of the selected pages change, each written into the page itself. The content is not changed.
- A document, which declares PDF/X, keeps its declaration: PDF/X needs a trim box or an art box on each page.
- A signed document stays signed: the change is saved after the signed part of the file. A certified document can be changed only as a copy.

### Assistants

The tool is also available to AI assistants through the action `pages.boxes` (see [AI tools](/help/ai-tools)).
