---
title: Add Image as Page
category: tools
order: 17
summary: page size, margins and image layout explained
keywords: add image, image as page, picture page, photo page, page size, margins, anchor, fit
related: /tools/pages.add-image, /tools/pages.insert, /help/tools
---
Add Image as Page inserts pictures as new pages of the open document, each placed on its page by the layout you choose. Each image becomes one page; every page of a multi-page TIFF becomes a page.

### Step by step

1. Open the document and choose **Add Image as Page** in Tools. Without an image the tool shows **Add the images** with **Choose Images…**.
2. Add the images with **Add Images**: **Image files…** or **Paste from clipboard** (Ctrl+V). The list **Images** shows each one with its pixels, its size and the page it becomes.
3. Turn an image with the buttons turn left and turn right; the page follows the turned image.
4. Choose the place in **Position**: before or after the current page, at the beginning or at the end, or click between two pages in the strip below the images.
5. Choose the size of the new pages in **Page**: **Same as page N** (the page next to the place), **Fit to image** (the size of the image by its resolution), A4, A3, Letter or **Custom**.
6. Set the **Page layout** in the right panel and press **Add N Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

### Page layout

| Setting | Meaning |
| --- | --- |
| Orientation | Follows each image: a landscape photo gets a landscape page. |
| Margins | The space around the image, at most half of the shorter side of the page. |
| Image | **Fit, keep ratio** shows the whole image; **Fill, keep ratio** fills the page inside the margins and cuts the rest; **Stretch** fills it without keeping the ratio; **Original size** uses the size by the resolution of the image. |
| Anchor | Where the image lies inside the margins (Middle center by default). The anchor belongs to the page: it is the same in every language of the user interface. |

- The image file is not changed: the page shows a copy of it. A JPEG photo is kept as it is, without a new compression.
- The open document keeps everything else, as with Insert Pages: a signed document is saved after its signed part, a password stays, a declaration of PDF/A stays and the result says that the new pages were not checked.
- A document certified by its author (a certification signature) allows no change of its pages: the result can only be saved as a copy, written whole and without the signatures, and the tool says why. The certified original stays as it is.
- To make a new document of images, use **Images to PDF**; to change the content of a page, use **Edit Content**.
