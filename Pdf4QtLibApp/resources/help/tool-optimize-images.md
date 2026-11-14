---
title: Optimize Images
category: tools
order: 31
summary: Recompress the images of a document with your own settings, image by image.
keywords: optimize images, images, recompress, downsample, dpi, jpeg, jpeg 2000, jbig2, ccitt, bitonal, grayscale, override, exclude, image size
related: /tools/optimize.images, /help/tools
---
Optimize Images recompresses the images of a PDF. Compress does the same with a preset; Optimize Images shows every image of the document and lets you choose the encoding, the resolution and the colors – for all images at once or for each image on its own.

### The list of the images

The left panel lists every image the pages draw, in the order of the document: "Image 1" is the first image drawn. Each row shows the pixels, the lowest resolution at which the image is drawn, its color space, its compression and its size. After the measuring, the size shows the result, for example "1.2 MB → 310 KB".

- **Sort** orders the list by the document or by the size.
- **All · Overrides · Excluded** show all images, the images with their own settings or the excluded ones.
- Ctrl or Shift selects more images, Ctrl+A selects all.

A row can carry a label:

| Label | Meaning |
| --- | --- |
| override | The image has its own settings. |
| will keep original | A new encoding would not be smaller, the image keeps its stream (Keep original if larger). |
| excluded | The image is left as it is. |
| inline · kept | The image is a part of the content of the page (an inline image). It is never changed. |
| in an annotation · kept | An annotation appearance (a stamp, a signature field) uses the image too. It is never changed. |
| cannot be read, too large | The image cannot be decoded; it is left as it is and counted as a problem. |

### Settings

- **Mode** – *Auto* lets the analysis of each image pick its colors and compression, *Custom* uses the profiles.
- **Color** – *Auto*, *Preserve*, *Color (RGB)*, *Grayscale* or *Bitonal* (1 bit).
- **Goal** – *Prefer quality* or *Minimum size*.
- **Options** – *Keep original if larger* and *Preserve transparency* (the transparency is kept as a soft mask).
- The **Settings editor** holds three profiles, Color, Grayscale and Bitonal: the algorithm, the target resolution (0 keeps the resolution), the resampling filter, the JPEG quality (the number written into the file), the JPEG 2000 rate (0 is lossless), the PNG predictor and, for bitonal images, the threshold (Auto picks it from the image).

### Overrides and exclusions

Select images and press **Override**: the Settings editor then edits only their settings, and the menus Mode, Color, Goal and Options change their settings too. A new override starts as *Mode: Custom* with the global color; what it does not change follows the global settings. **Exclude** (or *Enable compression* in Selected image) leaves the selected images as they are. Undo and Redo work for every change.

### Step by step

1. The tool measures the result at once: the rows, the Summary and the canvas show the real encoding of the current settings. Every change measures again.
2. The canvas compares the selected image BEFORE and AFTER with Fit and 100 %; the line below says the saving and the change of the resolution.
3. Press Optimize Images. The result dialog shows the size of the images before and after and lets you keep the result in this tab, save it as a copy or overwrite the file.

### What happens to the file

- The images of all layers are optimized, also those in layers, which are switched off.
- Inline images, the glyphs of Type 3 fonts and the appearances of annotations (stamps, signature fields) are never changed.
- An image used on more pages is optimized once, with the lowest resolution of its uses.
- The other entries of an image (its layer, its structure, its rendering intent, its metadata) are kept.
- A declaration of PDF/A or PDF/UA is removed from the metadata, the result is not checked against the standard; the result says so.
- A signed document can be saved only as a copy written whole: an incremental update would keep the original images in the earlier revision. The signatures of the copy are no longer valid.

> [!NOTE] Nothing to optimize
> When no image gets smaller with the current settings, the Summary says so and nothing is written. Try a lower resolution, another algorithm or *Minimum size*.

### Command line

```
pdftool optimize --opt-images input.pdf
```
