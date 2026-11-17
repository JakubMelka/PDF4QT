---
title: Document Statistics
category: tools
order: 80
summary: See what takes up the space in the file.
keywords: statistics, object statistics, space usage, file size, object count, size breakdown, why so big
related: /tools/review.statistics, /tools/review.object-inspector, /tools/optimize.compress, /help/tools
---
Document Statistics shows which objects of the file take up its space: by their function (images, fonts, content streams, …) or by their object type. Nothing in the document changes; the tool has only Close.

### What you see

- **View** in the toolbar chooses the table. *By Function* assigns every object to what it is used for: its share is the part of the size of all objects in bytes. *By Object Type* counts every value of the document by its PDF type (dictionary, reference, integer, …): its share is the part of the number of all values.
- The bar and the table show each row with its share, its count and, by function, its size in object bytes. Click a row (or use the arrow keys) to see it in the right panel.
- **File** in the left panel has the size of the file, the pages, the PDF version and the number of objects. A document with unsaved changes has no file size yet: save it first.
- The object streams and the cross-reference streams of the file are the row *File Structure*.

The statistics describe the document as it is open now, including unsaved changes. After every change of the document they are computed again after a short pause.

### What "object bytes" means

The size of an object is the size of the object written into a file, without the structure around it. The sum of all objects is therefore close to, but not the same as, the size of the file.

### Export

**Export** writes the statistics that are shown – the tool never computes them again for the file:

| Format | File |
| --- | --- |
| CSV Table | The table shown, for spreadsheets (semicolon separated, UTF-8) |
| Text | Both tables as plain text |
| HTML | Both tables for a web browser |
| XML | Both tables as machine-readable values |

### Command line

PdfTool writes the same statistics: `pdftool statistics input.pdf`.
