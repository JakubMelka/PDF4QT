---
title: Remove External Links
category: tools
order: 40
summary: Remove links leading out of the document.
keywords: links, web, e-mail, remove, external, url, hyperlinks, mailto
related: /tools/protect.remove-links, /help/tools
---
Remove External Links removes the links, which lead out of the document: links to web pages, e-mail addresses and other addresses. Only the link is removed; the text or the image under it stays on the page, it just stops being clickable. Links, which jump inside the document, keep working.

### Step by step

1. Choose the document. The left panel lists every external link, grouped by its page.
2. Every link is ticked. Untick a link to keep it. Click a row to show the link on the page.
3. Show filters the list: all links, web addresses (http and https) or e-mail addresses. The filter changes the list only, not what is removed. Select All and Select None act on the links shown.
4. Press Remove Links. Keep the result in this tab, save it as a copy or overwrite the original file.

### What happens to the file

- The page tree and the other objects of the file stay, so a signed document can still be saved incrementally: its signed revision stays as it was.
- A link of another kind than a web or an e-mail address (an ftp address, for example) is listed under All links only.

> [!NOTE] Links inside the document
> Links to a page or a bookmark of the same document are internal links. They are counted in the summary and never touched.

### Command line

PdfTool removes every external link of a document:

```
pdftool remove-external-links input.pdf
```

The command overwrites its input file.
