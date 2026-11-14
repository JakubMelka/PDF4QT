---
title: Convert to Black & White
category: tools
order: 40
summary: whole pages or images to 1 bit, modes, blank pages
keywords: black and white, bitonal, monochrome, 1-bit, b&w, scan cleanup, threshold, otsu, dithering, jbig2, ccitt, fax, blank pages, create bitonal document
related: /tools/optimize.bitonal, /help/tools
---
Convert to Black & White turns a scanned document into a small black and white (1-bit) one. A scan in color or gray takes many times more space than the same page in black and white, and a fax machine, an archive or a court filing often wants exactly that.

### Source: whole pages or images

| Source | What is converted | Use it for |
| --- | --- | --- |
| Whole pages | Every page is drawn into one image and converted. The page then is a single image over its media box. | Scans, also scans stored as several layers (a background and a masked text layer), which cannot be converted one by one |
| Images | Each image drawn on the pages is converted separately. Text and vector graphics stay as they are and remain selectable. | Documents with real text and colored pictures |

The resolution of whole pages is estimated from the images of the document (at least 300 dpi); you can set 24 to 600 dpi. A page, whose image would be larger than 32,767 pixels on a side or 2 GB, is refused with a message: lower the resolution.

### Methods

- **Automatic (Otsu's 1D method)** computes the threshold for every item from its gray levels. The line below the preview shows the threshold it found.
- **User-defined threshold** uses one threshold (0 to 255) for all items: a darker pixel becomes black.
- **Adaptive thresholding** computes a threshold of each region; it handles uneven lighting and shadows.
- **Dithering (Floyd-Steinberg)** keeps the shades of photographs as patterns of dots.

### Modes of the items

Select pages or images in the left panel (Ctrl and Shift select more) and choose a mode in **Mode** or by right-click. With nothing selected, a mode applies to the whole document.

| Mode | Result |
| --- | --- |
| Convert using the selected method | The item becomes black and white |
| Convert, inverted | The same, then black and white are swapped (a negative scan) |
| Leave unchanged | The item is not touched at all |
| Fill with black / Fill with white | The item is replaced by a black or a white area; a transparent image keeps its shape |

### Blank pages

**Blank pages** examines the pages at 200 dpi and finds the scans of a blank sheet: the dust, the texture of the paper and the shadow of the edge are not content, a page number or a single printed line is. The found pages are never changed silently - you are asked, whether they get a white fill. **Stop** ends the detection at once.

### What happens to the document

- The result is a copy written whole (Save as a copy is recommended): only then is the file really smaller. A signed document is saved only as a copy, and its signatures are no longer valid.
- The images are encoded by Flate, RunLength, CCITT Group 4 or JBIG2; Automatic tries all of them and keeps the smallest. All of them are lossless.
- Whole pages: the text of a converted page can no longer be selected or searched, its tags are removed from the structure tree, its layers are merged as for an export, and its own OCR text layer is gone. Annotations and form fields stay. Run [Recognize Text (OCR)](/tools/convert.ocr) afterwards to get the text back.
- Images: only the images drawn by the pages are converted - not an image, which is only listed in the resources. Inline images, stencil masks and the masks of other images are kept. An image, which an annotation appearance uses too, is kept and shown as *in an annotation · kept*. The images of every layer are converted. The OCR text layer of PDF4QT stays bound to its pages.
- A declaration of PDF/A or PDF/UA is removed, the result is not checked against the standard.
- An item, which cannot be read, drawn completely or encoded, stays as it was; the result lists it ("Not converted: pages 17, 43 (rendering errors)"), so you can discard it.

### Command line

The same conversion is available in PdfTool:

```
pdftool bitonal input.pdf output.pdf
pdftool bitonal --bitonal-source images --bitonal-method manual --bitonal-threshold 140 input.pdf output.pdf
pdftool bitonal --bitonal-detect-blank --bitonal-compression jbig2 input.pdf output.pdf
```
