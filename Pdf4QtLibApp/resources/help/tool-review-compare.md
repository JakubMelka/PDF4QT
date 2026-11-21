---
title: Compare
category: tools
order: 83
summary: Find the differences between two PDF documents.
keywords: compare, compare pdfs, diff, differences, changes, versions, revision, side by side, overlay, what changed
related: /tools/review.compare, /tools/review.report, /help/tools
---
Compare finds the differences between an original and a revised document: changed text, vector graphics, images and shadings, and pages that were added, removed or moved. It opens in its own tab, and you can have more comparisons open at once. The compared documents never change.

### Step by step

1. Choose the Original (the older version) and the Revised document (the newer one): drop a PDF onto a card, press Choose File…, or pick a document from Open tabs. An open tab with unsaved changes is compared as it is when you pick it (its working version).
2. In Documents, change the pages of each document if you need to (for example 1-10, 15). Swap exchanges the two documents.
3. Press Compare (F5). The differences appear in the list on the left, grouped by their pages, and they are marked on the pages.
4. Go through them in the list or with F6 (next) and Shift+F6 (previous).
5. Export saves the report as a PDF with the differences marked as annotations, or the differences as XML.

Closing the tool after you have chosen other documents (Choose File…, Change…) asks first, also when you close the tab or the window or open another page in the tab; the documents the tab opened with and **Swap** ask nothing.

You can also start a comparison from the command line: `PDF4QT --compare original.pdf revised.pdf`.

### What you see

- View: Side by Side, Original Only, Revised Only or Overlay (the paired pages on top of each other, blended).
- Filter: the kinds of differences shown, with their counts. A kind without any difference is unavailable. The export uses the differences shown.
- Show: the colored highlights, the markers at the edge of the view, only the pages with differences, and whether the view follows the selected difference.
- Colors: the color of each kind - removed, added, replaced and moved. When you give a kind the color of another kind, the two kinds swap their colors.
- Overlay: the blend of the two pages, the page sizes (original sizes, the smaller page fitted to the larger one, or a manual scale for each document) and the shift of the revised page in millimeters. Reset returns to the defaults. The overlay changes only the view; the results and the reports use the original documents.

### When the result is out of date

The result stays in the tab. Compare Again is unavailable until something changes: after you change Options, the pages or a document, the tool says that the shown differences come from the previous settings and Compare Again is available again. The options (texts as vector graphics, characters instead of words) take effect after Compare Again.

### Protected documents

A document with a password asks for it when you choose it. Comparing on the screen is reading the documents, so a document with restrictions can be compared. Exporting is taking content out of the documents: the XML needs the permission to copy content, and the report, which is a new unencrypted document of their pages, needs the same permission as Merge. For a restricted document the tool asks for its owner password.

### Limits

Very large text ranges are compared within a memory budget: when it is exceeded, the tool reports that the text of those pages differs as a whole instead of word by word. Stop ends the comparison and shows no differences.
