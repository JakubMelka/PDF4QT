---
title: Document Report
category: tools
order: 82
summary: A report of the information, scripts, destinations and structure of the document.
keywords: report, document info, javascript, scripts, named destinations, structure tree, xmp, metadata, xml export, fonts, inks, file hashes, page boxes
related: /tools/review.report, /tools/review.object-inspector, /tools/review.statistics, /help/document-properties, /help/tools
---
Document Report writes a report of the document into a file. Nothing in the document changes.

### Step by step

1. Check the sections in the left panel. A section the document has nothing of (no structure tree, no XMP metadata, …) is disabled and says why. Click a section to see it in the preview.
2. The page selection applies to JavaScript, named destinations and page boxes. The other sections describe the whole document.
3. Choose the format (Text, HTML or XML) and the dates (short, long, ISO or RFC 2822) in the toolbar.
4. Press **Create Report**. Choose where the file goes; the report can be opened right after it is saved.

After a report is created, Create Report stays disabled until the document or a setting changes.

### Sections

| Section | What it has |
| --- | --- |
| Basic information | Title, author, dates, version, Tagged (by `/MarkInfo`), Trapped, form, pages, encryption and permissions |
| File hashes | MD5, SHA1, SHA256, SHA384 and SHA512 of the saved file – save the document first |
| Metadata (XMP) | The embedded XML metadata as it is stored |
| JavaScript | Every script of the document, as a text |
| Named destinations | The named targets of links; a page that does not exist is "unresolved" |
| Page boxes | Media, crop, bleed, trim and art box in millimeters |
| Structure tree | The tags of a tagged document |
| Inks | The process and spot inks |
| Fonts | The fonts with their names exactly as stored, how they are embedded and their substitutes; optionally their character maps |

### Scripts are only shown

The JavaScript of the document is written into the report as a text. It is never run. Scripts of an XFA form may not be found.

### Structure as XML

**Structure as XML** in the toolbar writes every object of the file as XML: the trailer, then all objects with their dictionaries, arrays and values. Use it to inspect or compare documents with other tools. The streams are left out unless you export them as hexadecimal data or as text; with the streams the file can be several times bigger than the PDF.

### Permissions

The JavaScript, the XMP metadata and the streams of the XML are content of the document. A document that does not allow copying asks for the owner password before they are written.

### Command line

PdfTool writes the same sections: `pdftool info`, `info-metadata`, `info-javascript`, `info-dests`, `info-page-boxes`, `info-struct-tree`, `info-inks`, `info-fonts` and `xml`.
