---
title: Extract Images
category: tools
order: 63
summary: which images are exported, numbering and formats explained
keywords: extract images, export images, save pictures, photos, png, jpeg, fetch images, copy image, embedded images
related: /tools/convert.extract-images, /tools/convert.pdf-to-images, /help/tools
---
Extract Images writes the pictures embedded in the document into image files, each in its own pixels. The open document is not changed.

### Step by step

1. Choose the pages in **Pages**: all pages, the current page or a range. The list shows the images of these pages; a click on an image in the list or on the page selects it and shows it on its page.
2. Leave out the images you do not want: select them and press Space, or use **Include all** and **Exclude all**.
3. Choose the format in **Format** and its settings in **Options** (the quality and the compression of the format; the optimized and progressive writing of JPEG only); choose the folder, the file name and what happens with existing files in **Output files**.
4. Press **Export Images**. The images are written in the order of the list; **Stop** ends the export, the files already written stay whole.

**Copy Image** puts the selected image on the clipboard.

When the chosen pages contain only text and vector drawings, the list is empty and says so; **Search All Pages** chooses all pages.

### Which images are exported

- The images of the content of the pages and of their forms, in every layer, also small images written into the content of a page (inline images). An image, which is only in a layer hidden in this tab, is marked **hidden layer** and exported too, unless you leave it out.
- An image used on more pages is exported once. Two images with the same data and the same description are exported once too; images with the same bytes, but another size, are different images.
- Not exported: the letters of a font, which are drawn as pictures (Type 3 fonts), the appearances of annotations and stamps, the masks of other images, and stencil masks - they have no picture of their own, the color of the page paints through them.

### What a file contains

The image is decoded and its colors converted by the color management of **Settings**, as the page shows it. Its soft mask or mask becomes its transparency; a format without transparency (JPEG, BMP) gets a white background. The image keeps its pixel size. PNG and TIFF take an image of any size up to 2 GB of pixels; WebP holds at most 16,383 pixels per side. An image, which is too large to be decoded or for its format, or which cannot be read, is not exported and the result says why.

### File names

The file name is a pattern; the extension of the format is added.

| In the name | Is replaced by |
| --- | --- |
| `#` | the number of the image in the list ("Image 3" is `3`); the numbers of the images you leave out are skipped |
| `@` | the number of the first page of the image |
| `{source_base}` | the name of the document without its extension |
| `{date}` | the date of the export |

Existing files are handled as in PDF to Images: **Ask**, **Overwrite**, **Skip** or **Rename**.

### The result

- **Files**, **Left out**, **Total size** and **Time** say what was written. **Open Folder** shows the files.
- When files cannot be written, the result lists them with their reasons; **Retry** writes only these files again, **Choose Folder…** writes them into another folder.
- A document protected by permissions must allow copying its content.

### Command line

PdfTool exports the images by the same tool; its template uses `%` for the number of the image:

```
pdftool fetch-images input.pdf --image-output-dir out --image-template-fn "Image_%"
```
