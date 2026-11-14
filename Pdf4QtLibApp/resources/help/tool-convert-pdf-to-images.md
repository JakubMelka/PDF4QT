---
title: PDF to Images
category: tools
order: 62
summary: formats, resolution, file names and problems of pages explained
keywords: pdf to images, render, export pages, png, jpeg, tiff, webp, bmp, dpi, pixels, image files, picture of a page
related: /tools/convert.pdf-to-images, /tools/convert.extract-images, /help/tools
---
PDF to Images writes a picture of each selected page into an image file: one file per page. The open document is not changed.

### Step by step

1. Choose the pages in **Pages**: all pages, the current page, the pages selected in the thumbnails or a range such as `1-12, 17`; **Only** keeps the even, odd, portrait or landscape pages. Click a thumbnail to see its page in the preview; the line above the page says the name of its file and the size of its image.
2. Choose the format in **Format**: PNG, JPEG, TIFF, WebP and BMP are listed first, **More formats** lists every format the application can write.
3. Choose the resolution: 72, 150, 300 or 600 dpi in the toolbar, any other value in **Resolution**. With **Size by: Pixels** the longer side of every image has the same number of pixels.
4. Choose the folder, the file name and what happens with existing files in **Output files**.
5. Press **Export Images**. The files are written in the order of the pages; **Stop** ends the export after the page, which is being written. The files already written stay whole.

### The size of the images

The image is the visible area of the page: its crop box, turned by the rotation of the page. A page, which uses a larger unit (/UserUnit), gets a larger image. At 300 dpi an A4 page is 2480 × 3508 pixels.

An image may have at most 32,767 pixels per side (WebP 16,383) and at most 2 GB of pixels. A page, which would be larger, is said before the export starts - lower the resolution. The status bar estimates the size of all files from the page of the preview, it says "about".

### Format and options

| Setting | What it does |
| --- | --- |
| Subtype | A variant of the format, when it has any |
| Compression | The compression level of a lossless format (PNG, TIFF) |
| Quality | The quality of a lossy format (JPEG, WebP), 0 to 100 |
| Optimized write, Progressive scan write | Options of JPEG; other formats do not have them |

A setting, which the chosen format does not support, is disabled. A format without transparency (JPEG, BMP) gets a white background.

### How the pages are rendered

The pages are rendered with the settings of the rendering and of the color management of **Settings**, and with the layers as they are shown in this tab. The color modes of the view (inverted colors, high contrast, grayscale) change only the display, they are not used. Annotations and form fields are drawn as they are printed.

### File names

The file name is a pattern; the extension of the format is added. The name must contain `@`, so every page gets its own file.

| In the name | Is replaced by |
| --- | --- |
| `@` | the number of the page |
| `#` | the number of the file in the export, `##` pads it with zeros |
| `{source_base}` | the name of the document without its extension |
| `{date}` | the date of the export |

### Existing files

| Existing files | What happens |
| --- | --- |
| Ask | At the first file, which exists, the tool asks: **Overwrite**, **Skip**, **Rename** or **Stop Export** |
| Overwrite | The existing files are replaced |
| Skip | The existing files are kept, their pages are not exported |
| Rename | The new files get ` (2)`, ` (3)`, … in their names |

The open documents are never overwritten.

### The result

- **Files**, **Total size** and **Time** say what was written. **Open Folder** shows the files.
- **Rendering issues** lists the pages, whose pictures may be incomplete - a font, which is not embedded, a part of the page, which cannot be drawn exactly.
- When files cannot be written (the disk is full, a file is open in another program), the result lists them with their reasons. **Retry** writes only these files again with the same settings, **Choose Folder…** writes them into another folder.
- A stopped export lists what was written and offers **Delete Written Files**.
- A document protected by permissions must allow copying its content.

### Command line

PdfTool renders the pages by the same tool; its template uses `%` for the page number:

```
pdftool render input.pdf --image-output-dir out --image-template-fn "Page_%" --image-res-dpi 300
```
