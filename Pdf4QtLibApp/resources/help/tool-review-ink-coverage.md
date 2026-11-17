---
title: Ink Coverage
category: tools
order: 85
summary: How much of every ink the pages use.
keywords: ink coverage, ink usage, toner, tac, total area coverage, cmyk coverage, print cost, inks
related: /tools/review.ink-coverage, /tools/review.output-preview, /tools/review.soft-proof, /help/tools
---
Ink Coverage measures every ink of the selected pages: the four process inks and every spot color of the document. Nothing in the document changes.

### Step by step

1. Choose the pages on the left (all pages, the current page, the pages selected in the thumbnails or a range).
2. Press Calculate Coverage. The finished pages appear in the table while the others are still being measured.
3. Stop ends the calculation; the table keeps the pages finished so far. Calculate Again starts from the beginning.
4. Export writes the table as CSV, text, HTML or XML.

The result stays in the tool. When the document or the page selection changes, the tool says that the table is out of date and Calculate Again is available again.

### What the numbers mean

- mm² is the area of the ink weighted by its tint: 50 % of an ink on 10 mm² counts as 5 mm². It is measured over the whole page.
- % is the share of the ink inside of the trim box of the page (the crop box, when the page has no trim box).
- The Total row sums the areas of the measured pages, each page once; its % is the ink inside of all the boxes over their area.
- Peak coverage is the highest sum of all inks at one point and the page where it is.
- The pages are measured as they are printed: the annotations, which are printed, and the layers by their print state.

### Pages with problems

A page, which is rendered with errors, is measured and included in the totals, but it is marked with a warning: its values may differ from print. Only a page, which cannot be rendered at all, is left out of the totals.

### Command line

PdfTool `ink-coverage` writes the same table.
