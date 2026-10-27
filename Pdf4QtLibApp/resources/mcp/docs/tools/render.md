# Tool `render` – Render pages

The tool `render` draws pages as images for vision (Read documents). The image is an image content block of the result; `structuredContent` describes it, including the matrices between image pixels and PDF points. Background and size guidance: pdf4qt://docs/rendering.md.

| Action | Permission | Description |
| --- | --- | --- |
| `page` | Read documents | Image of one page or of an area of it |
| `overview` | Read documents | Contact sheet of up to 24 pages with page numbers (find where a chart is) |

Hints of the tool: read only.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `render.page`

Image of one page or of an area of it.

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "page", "page": {...}}`

### Parameters of `page`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `page` | integer, ≥ 1 | no | Physical page number from 1 |
| `pageLabel` | string, at most 256 characters | no | Page label as printed in the document ("iv", "A-2"); used instead of page |
| `dpi` | integer, 18–600, default `150` | no | Resolution |
| `area` | object {left, bottom, right, top} | no | Only this area of the page, PDF points |
| `annotations` | string, one of `show`, `hide`, default `show` | no | Draw the annotations |
| `format` | string, one of `png`, `jpeg`, default `png` | no | Image format |
| `jpegQuality` | integer, 30–95, default `85` | no | Quality of JPEG |

### Result of `page`

`structuredContent` is `{"action": "page", "page": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `page` | integer | Physical page number from 1 |
| `label` | string | Page label |
| `widthPx` | integer |  |
| `heightPx` | integer |  |
| `dpi` | integer |  |
| `area` | object {left, bottom, right, top} |  |
| `pageToImage` | array of numbers | Matrix [a, b, c, d, e, f] from PDF points to image pixels |
| `imageToPage` | array of numbers | Matrix [a, b, c, d, e, f] from image pixels to PDF points |
| `format` | string |  |
| `bytes` | integer |  |

At most 600 dpi and 50 megapixels; a PNG over 12 MiB is refused (lower `dpi`, use `area`, or `jpeg`). Convert pixels to points only by `imageToPage`.

### Examples of `page`

A page at the default 150 dpi:

```json
{"name": "render", "arguments": {"action": "page", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "page": {"page": 3}}}
```

A zoomed area at 300 dpi, without annotations:

```json
{"name": "render", "arguments": {"action": "page", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "page": {"page": 3, "dpi": 300, "area": {"left": 72, "bottom": 350, "right": 300, "top": 500}, "annotations": "hide"}}}
```

A scanned page by its label as JPEG:

```json
{"name": "render", "arguments": {"action": "page", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "page": {"pageLabel": "A-4", "dpi": 120, "format": "jpeg", "jpegQuality": 75}}}
```

## `render.overview`

Contact sheet of up to 24 pages with page numbers (find where a chart is).

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "overview", "overview": {...}}`

### Parameters of `overview`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `pages` | string, at most 4096 characters | yes | Physical pages, at most 24, such as "1-12" |
| `maxSize` | integer, 64–256, default `192` | no | Longest side of a cell in pixels |

### Result of `overview`

`structuredContent` is `{"action": "overview", "overview": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `widthPx` | integer |  |
| `heightPx` | integer |  |
| `cells` | array of objects | Cells of the pages in the image |
| `cells[].page` | integer | Physical page number from 1 |
| `cells[].label` | string | Page label |
| `cells[].x` | integer |  |
| `cells[].y` | integer |  |
| `cells[].width` | integer |  |
| `cells[].height` | integer |  |

At most 24 pages. `cells` gives the position of every page in the image.

### Examples of `overview`

A contact sheet of the first 24 pages:

```json
{"name": "render", "arguments": {"action": "overview", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "overview": {"pages": "1-24"}}}
```

Small cells of selected pages:

```json
{"name": "render", "arguments": {"action": "overview", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "overview": {"pages": "30-35,60,72-75", "maxSize": 96}}}
```
