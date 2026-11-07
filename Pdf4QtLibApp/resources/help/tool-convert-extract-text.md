---
title: Extract Text
category: tools
order: 60
summary: save the text of a document as a file
keywords: extract, text, export, plain text, html, xml, txt, encoding, utf-8, latin-1, reading order, copy all text
related: /tools/convert.extract-text, /help/tools
---
Extract Text saves the text of the chosen pages as one file: plain text, HTML or XML. The document is not changed. Instead of a file, the text can also go to the clipboard.

### Step by step

1. Choose the pages in the left panel: all pages, the current page, the pages selected in the thumbnails or a range. The filters keep only even, odd, portrait or landscape pages. The tool opens with the pages selected in Thumbnails; a range with an empty field selects no page.
2. The canvas shows the page next to the text extracted from it. Click another thumbnail to see another page. Fit and 100 % change the size of the page.
3. Choose the reading order, what else is included and the format with its encoding in the toolbar.
4. The right panel estimates the number of words and the size of the file, and says how many pages have no text.
5. Press Extract Text. Choose where the file goes, or copy the whole text to the clipboard.

### Reading order

| Order | What it does |
| --- | --- |
| Auto | The structure, when the document is tagged; otherwise the layout |
| Layout | Blocks of text as they are placed on the page, columns one after another |
| Content stream | The order in which the text is written in the file; fast, but often not the order you read |
| Structure | Follows the tags of a tagged document; a document without tags refuses it |

### What is included

Besides the text of the pages, the file can have:

- **Page numbers** – a line “Page N” before the text of every page. The order Structure follows the tags, which do not end with a page, so it has no such lines.
- From the tags of a tagged document: **structure titles**, **languages**, **alternative descriptions** (the text that describes a picture, on by default), **expanded forms** of abbreviations, **actual text** that replaces content and **phonemes**. A tag belongs to the page it names; with only some pages chosen, the tags of the other pages are left out.

### Format and encoding

| Format | File |
| --- | --- |
| Plain text | A .txt file, one empty line after each paragraph |
| HTML | A .html file with headings and paragraphs, for a browser |
| XML | A .xml file, structured for other programs |

UTF-8 holds every language and is the right choice in almost every case. Latin-1 is for old Western European programs; System is the encoding of this computer. A character the encoding cannot hold is written as a question mark, and the summary says how many were replaced. HTML and XML files say their encoding, so a browser or a program reads them right.

### What is left out

- Pages without text, such as scanned pages, give nothing. Recognize the text first with OCR. When none of the chosen pages has text, the tool says so and writes no file.
- Text drawn as pictures or as shapes cannot be extracted.
- A document whose author does not allow copying its content refuses the tool. The document can still be read on screen.

> [!NOTE] The estimate
> The words, characters and size in the right panel are measured on a sample of the pages and scaled; the summary after the run has the exact numbers.

### Command line

PdfTool writes the same text to the standard output:

```
pdftool fetch-text --console-format text input.pdf
pdftool fetch-text --console-format html --text-analysis-alg layout --text-show-page-numbers input.pdf
```
