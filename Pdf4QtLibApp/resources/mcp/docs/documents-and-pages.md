# Documents, pages and coordinates

## Document ids

Every open document has a `documentId`: 26 characters of base32 (A–Z, 2–7), such as `K3FQW2M7B4XDZR5T6YH5ZCANPE`. `application.get_state` lists them; every result of a document action returns the `documentId` and the `title` of the tab again.

- The id belongs to the tab of the document. It stays the same while the tab is open – also after changes, undo and saving – and it is invalid after the tab is closed. Opening the same file again gives a new id.
- A copy made in PDF4QT (Edit a Copy) has its own id and no file until it is saved.
- An omitted `documentId` means the document of the active tab, else the only open document. When several documents are open and none is active, PDF4QT asks the user to choose (if your client supports elicitation) or answers with an error listing the ids to choose from.
- A document can be `loading`, `password_required` (the user must type the password in PDF4QT – you never receive passwords) or `failed`. Only `ready` documents can be read.

## Pages and labels

- `page` is the physical page number from 1 – the position of the page in the file.
- `label` is the page label printed in the document, such as `iv`, `A-2` or `12`. Many documents have labels, which differ from the physical numbers (front matter numbered with Roman numerals, appendices with letters).
- Ranges are strings: `1-3,7`, `5-` (to the end), `-3` (the first three pages). Only physical numbers are allowed in ranges.
- Actions that take one page accept `pageLabel` instead of `page`. A label used by several pages is ambiguous: PDF4QT asks the user or answers with the candidates.
- Results give both `page` and `label`. Cite pages to the user with the label the document prints (and the physical number when they differ).

## The coordinate space

Coordinates are PDF points (1/72 inch) in the **unrotated default user space of the page** – the space the page content uses:

```text
 y
 ^            MediaBox (the whole sheet)
 |   +-----------------------------------+  (right, top)
 |   |   CropBox (the visible part)      |
 |   |   +---------------------------+   |
 |   |   |                           |   |
 |   |   |   text, images, ...       |   |
 |   |   +---------------------------+   |
 |   +-----------------------------------+
 |  (left, bottom)
 +--------------------------------------------> x
 origin: usually the bottom-left corner of the MediaBox
```

- The origin is where the content stream puts it – usually the bottom-left corner of the MediaBox, but a MediaBox can start elsewhere (`document.get_pages` gives `mediaBox` and `cropBox` in this space).
- `rotation` (0, 90, 180 or 270) rotates the page **only when it is shown**. Coordinates never rotate: a point near the top of the unrotated page stays near the top of the space, even if the user sees it on the left side. `widthPt` and `heightPt` are the size as shown (after the rotation).
- `userUnit` scales points on some technical drawings (a point is `userUnit`/72 inch). Coordinates stay in points of the default space.

## Rectangles

A rectangle is an object `{left, bottom, right, top}` in points; `left` < `right` and `bottom` < `top` (PDF4QT normalizes swapped values).

```json
{"name": "text", "arguments": {"action": "get_text_in_area", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "get_text_in_area": {"page": 3, "rect": {"left": 72, "bottom": 400, "right": 540, "top": 720}}}}
```

## Quads

A quad is an array of 8 numbers: the four corners of a quadrilateral around a piece of text, in the QuadPoints order of PDF annotations – top-left x, y, top-right x, y, bottom-left x, y, bottom-right x, y. A rotated line of text has a rotated quad.

`text.search` and `document.get_selection` return quads in exactly this form (the geometry of `text.get_text` and `text.get_text_in_area` gives rectangles); `annotations.add` accepts them unchanged:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [{"type": "underline", "page": 3, "quads": [[72, 712.4, 180.6, 712.4, 72, 700.1, 180.6, 700.1]]}]}}}
```

## Images and points

`render.page` returns two matrices `[a, b, c, d, e, f]`:

- `pageToImage` maps a point (x, y) of the page to a pixel: `px = a·x + c·y + e`, `py = b·x + d·y + f`.
- `imageToPage` maps a pixel back to a point in the same way.

Image pixels grow downward and include the rotation of the page; points grow upward and never rotate – always convert through the matrix, never by hand. To mark something you see in an image, take the pixel rectangle, convert both corners by `imageToPage` and make a rectangle of the minima and maxima (pdf4qt://docs/rendering.md).

## Document resources

Next to the tools, the documents are resources (Read documents applies to them as to the tools):

| URI | Content |
| --- | --- |
| pdf4qt://application/state | The result of `application.get_state` (subscribe to learn about opened, closed and changed documents) |
| pdf4qt://documents/{documentId}/info | `document.get_info` |
| pdf4qt://documents/{documentId}/pages | `document.get_pages` (at most 2000 pages) |
| pdf4qt://documents/{documentId}/outline | `document.get_outline` (at most 500 items) |
| pdf4qt://documents/{documentId}/text | The whole text (at most 4194304 characters; use `text.get_text` for more) |
| pdf4qt://documents/{documentId}/pages/{page}/text | The text of one page |
| pdf4qt://documents/{documentId}/pages/{page}/image/{dpi} | A PNG image of one page (18 to 600 dpi) |
| pdf4qt://documents/{documentId}/annotations | `document.get_annotations` |
| pdf4qt://documents/{documentId}/pages/{page}/annotations | The annotations of one page |
| pdf4qt://documents/{documentId}/form | `document.get_form_fields` |
| pdf4qt://documents/{documentId}/attachments/{index} | The content of an attachment (at most 8 MiB) |

A subscribed document resource is updated after every change of the document (a commit, undo, reload or close).
