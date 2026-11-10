---
title: Extract Pages
category: tools
order: 14
summary: one new file or one file per page explained
keywords: extract pages, export pages, selected pages, page range, one file per page, new document
related: /tools/pages.extract, /tools/pages.split, /help/tools
---
Extract Pages saves the selected pages as a new PDF, or as one file per page. The open document is not changed.

### Step by step

1. Choose the pages in **Pages to extract**: the pages selected in Thumbnails, a range such as `2-5, 9`, or all pages narrowed by Even, Odd, Portrait or Landscape. Click a page in the thumbnails below to add it or to leave it out; the selected thumbnails are the pages to extract.
2. The preview shows only the pages of the result, each with the page it comes from ("from page 8"). Delete on a page of the preview leaves it out.
3. Choose **Output** in the toolbar: **One file** or **One file per page**.
4. Press **Extract Pages**. One file opens as a new document in a new tab or is saved as a file; one file per page is written into the folder of **Output files**.

The pages keep the order of the document. To remove the pages from the document afterwards, use Remove Pages.

### One file per page

The files are named by the pattern of **File name**: `{source_base}_p@` gives `Report_p2.pdf`, `Report_p3.pdf`, …, where `@` is the page number. **Existing files** decides what happens with a file, which exists already, the same way as in Split.

### What the new files keep

- The content, annotations, form fields and links of the pages.
- Signatures are removed, the empty signature fields stay.
- A document protected by permissions must allow assembling it.
