---
title: Images to PDF
category: tools
order: 56
summary: page size, image layout and order explained
keywords: images to pdf, jpg to pdf, png to pdf, photos, scans, page size, margins, anchor, crop
related: /tools/convert.images-to-pdf, /tools/pages.add-image, /help/tools
---
Images to PDF turns photos and scans into a new PDF, one page per image, each placed on its page by the layout you choose.

### Step by step

1. Open the tool – it opens in its own tab “New document”.
2. Add the images: **Add Images** (**Image files…**, Ctrl+Alt+I, or **Paste from clipboard**, Ctrl+V), or drop the files anywhere in the window. The order of the list is the order of the pages; drag an image to move it.
3. Choose the page: **Size** in the toolbar, then Orientation, Margins, Image and Anchor in the panel **Page layout**. The preview shows the page of the selected image exactly as it will be written.
4. Press Create PDF. When it finishes, open the new document in this tab, or save it as a file and open it here.

### The page

| Setting | What it does |
| --- | --- |
| Size | A4, A3, A5, Letter, Legal, or Custom (Width and Height appear in the panel); **Fit to image** makes each page exactly as large as its image by its resolution |
| Orientation | **Auto** turns each page to match its image; Portrait and Landscape keep every page in one orientation |
| Margins | The space around the image on every side, in millimeters; less than half of the shorter side of the page |
| Image | **Fit, keep ratio** shows the whole image inside the margins; **Fill, keep ratio** covers the area, the edges may be cut; **Stretch** fills the area exactly, the proportions may change; **Original size** uses the resolution stored in the image |
| Anchor | Where the image lies inside the margins: one of the nine positions, the middle center by default |

The size of an image comes from its resolution (a scan of 300 dpi keeps its paper size); an image without a resolution is taken as 300 dpi.

### The images

- **Rotate Left** and **Rotate Right** turn the selected image; with Auto orientation its page turns with it. The image file is not changed.
- **Duplicate** adds one more page of the selected image, **Remove** takes it out of the list.
- **Sort** orders the images by their file names (numbers in natural order), by their folders and names, by their file types, or turns the current order around.
- Every page of a multi-page TIFF becomes a page. A file, which cannot be read, is listed with the reason; remove it to continue.
- **Optimize** keeps the images exactly as they are (Off), or recompresses large photos (Prefer quality, Minimum size). A JPEG is written with its own bytes unless it is optimized; other images are stored losslessly, transparency included.

Undo and Redo of the toolbar undo every change of the images and the options while the tool is open.

> [!TIP] Images into an existing document
> Add Image as Page inserts images as new pages of an open document.
