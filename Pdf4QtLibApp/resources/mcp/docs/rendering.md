# Rendering pages

The tool `render` draws pages as images for a model with vision (Read documents). Use it when the text alone is not enough: charts, figures, tables, scanned pages, forms, signatures, the layout of a page, or to check how your annotations look.

## One page

```json
{"name": "render", "arguments": {"action": "page", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "page": {"page": 5}}}
```

- `page` or `pageLabel` selects the page.
- `dpi` (18 to 600, default 150). 72 dpi is one pixel per 1/72 inch (one pixel per point, unless the page has a `userUnit`). Text is readable from about 100 dpi; 150 dpi is enough for most pages; use 200–300 dpi only for small print, and only with an `area`.
- `area` renders only a rectangle of the page in PDF points – zoom into a table or a figure instead of rendering the whole page at a high resolution:

```json
{"name": "render", "arguments": {"action": "page", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "page": {"page": 5, "dpi": 300, "area": {"left": 72, "bottom": 300, "right": 540, "top": 520}}}}
```

- `annotations`: `show` (the default) draws the annotations as the user sees them, `hide` draws the page without them.
- `format`: `png` (the default, sharp text) or `jpeg` with `jpegQuality` (30 to 95, default 85) – smaller for photos and scans.
- The image shows the layers (optional content), which the document shows by default, in the normal colors of the page: layers switched by the user and a color mode of the view (such as dark or high contrast) do not apply.

The result has the image as an image content block, a `resource_link` to pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}, and the data: `widthPx`, `heightPx`, `dpi`, the rendered `area`, and the matrices `pageToImage` and `imageToPage`.

## Limits

- At most 600 dpi and 50 megapixels per image.
- A PNG over 12 MiB is refused with an error: lower `dpi`, render an `area`, or use `jpeg`.
- Many clients limit the size of a result. A page at 150 dpi is usually below 1 MB as PNG; a full page at 600 dpi is not – render areas instead.

## From pixels to points

Never compute coordinates from pixels by hand: the image grows downward, it includes the rotation of the page and the area. Use `imageToPage` = `[a, b, c, d, e, f]`:

```text
x = a * px + c * py + e
y = b * px + d * py + f
```

Convert two opposite corners of a pixel rectangle and take the minima and maxima of x and y as `{left, bottom, right, top}`. Then, for example, read the text of the area by `text.get_text_in_area`, point at it by `view.show_area`, or mark it by `annotations.add` with a `rectangle`.

## Overview sheet

`overview` draws up to 24 pages as small cells of one image with their page numbers – the fastest way to find the page with a chart, a signature or a form:

```json
{"name": "render", "arguments": {"action": "overview", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "overview": {"pages": "1-24"}}}
```

`maxSize` (64 to 256, default 192) is the longest side of a cell in pixels. `cells` gives the position of every page in the image. Render the interesting page alone afterwards.
