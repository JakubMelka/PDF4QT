# Tool `annotations` – Edit annotations

The tool `annotations` adds, changes, deletes and replies to annotations (Edit documents). One call is one undo step and at most one confirmation of the user – put all items of a task into one call. The author of the annotations is the name of your client. Background, types, colors and icons: pdf4qt://docs/annotations.md.

| Action | Permission | Description |
| --- | --- | --- |
| `add` | Edit documents | Add annotations (one undo step) |
| `update` | Edit documents | Change annotations (one undo step) |
| `delete` | Edit documents | Delete annotations (one undo step) |
| `reply` | Edit documents | Reply to a comment |

Hints of the tool: changes PDF4QT or documents, destructive.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `annotations.add`

Add annotations (one undo step).

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "add", "add": {...}}`

### Parameters of `add`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `items` | array of 1–500 objects | yes | Annotations to add (all in one undo step) |
| `items[].type` | string, one of `highlight`, `underline`, `strikeout`, `squiggly`, `note`, `text_box`, `rectangle`, `ellipse`, `line`, `arrow`, `polygon`, `polyline`, `ink`, `stamp`, `link` | yes | Type of the annotation |
| `items[].page` | integer, ≥ 1 | yes | Physical page number from 1 |
| `items[].quads` | array of 1–1000 arrays of 8 numbers | no | Text markup (highlight, underline, strikeout, squiggly): quads from text.search |
| `items[].rect` | object {left, bottom, right, top} | no | text_box, rectangle, ellipse, stamp, link: the rectangle in PDF points |
| `items[].point` | object {x, y} | no | note: the position of the icon |
| `items[].points` | array of 2–1000 objects {x, y} | no | line / arrow: 2 points; polygon / polyline: 3 to 1000 points |
| `items[].strokes` | array of 1–50 arrays of 2–2000 objects {x, y} | no | ink: at most 50 strokes of at most 2000 points |
| `items[].contents` | string, at most 65536 characters | no | Text of the comment (the text of a text_box) |
| `items[].color` | string, at most 16 characters | no | Color: a palette name (yellow, orange, red, pink, purple, blue, green, black, white) or "#rrggbb" |
| `items[].fillColor` | string, at most 16 characters | no | Fill color of shapes: a palette name (yellow, orange, red, pink, purple, blue, green, black, white) or "#rrggbb" |
| `items[].width` | number, 0–100 | no | Border width in points |
| `items[].opacity` | number, 0–1 | no | Opacity |
| `items[].icon` | string, one of `comment`, `key`, `note`, `help`, `new_paragraph`, `paragraph`, `insert` | no | Icon of a note |
| `items[].stamp` | string, one of `approved`, `experimental`, `not_approved`, `as_is`, `expired`, `not_for_public_release`, `confidential`, `final`, `sold`, `departmental`, `for_comment`, `top_secret`, `draft`, `for_public_release` | no | Standard stamp |
| `items[].fontSize` | number, 4–144 | no | Font size of a text_box |
| `items[].url` | string, at most 2048 characters | no | link: an https address |
| `items[].targetPage` | integer, ≥ 1 | no | link: the target page from 1 (instead of url) |

### Result of `add`

`structuredContent` is `{"action": "add", "add": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `created` | array of objects {id, type, page} |  |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

Each type needs its geometry: `quads` for markups, `rect` for text_box, rectangle, ellipse, stamp and link, `point` for a note, `points` for lines and polygons, `strokes` for ink. A missing field gives an error naming the item.

### Examples of `add`

Highlight two matches of a search:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [{"type": "highlight", "page": 2, "quads": [[108.2, 641.5, 162.9, 641.5, 108.2, 629.4, 162.9, 629.4]], "color": "yellow"}, {"type": "highlight", "page": 7, "quads": [[72, 412.8, 126.7, 412.8, 72, 400.7, 126.7, 400.7]], "color": "yellow", "contents": "Same term as on page 2"}]}}}
```

A note, a text box and an arrow:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [{"type": "note", "page": 4, "point": {"x": 530, "y": 700}, "icon": "comment", "contents": "Source missing."}, {"type": "text_box", "page": 4, "rect": {"left": 380, "bottom": 600, "right": 560, "top": 640}, "contents": "Updated in 2026", "color": "red", "fontSize": 10}, {"type": "arrow", "page": 4, "points": [{"x": 380, "y": 620}, {"x": 300, "y": 580}], "color": "red", "width": 1.5}]}}}
```

A stamp, a rectangle and a link:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [{"type": "stamp", "page": 1, "rect": {"left": 400, "bottom": 700, "right": 560, "top": 760}, "stamp": "approved"}, {"type": "rectangle", "page": 3, "rect": {"left": 70, "bottom": 300, "right": 540, "top": 480}, "color": "blue", "fillColor": "#e8f0ff", "opacity": 0.5}, {"type": "link", "page": 3, "rect": {"left": 72, "bottom": 280, "right": 200, "top": 294}, "targetPage": 12}]}}}
```

A freehand mark:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [{"type": "ink", "page": 6, "strokes": [[{"x": 100, "y": 500}, {"x": 140, "y": 520}, {"x": 180, "y": 505}]], "color": "green", "width": 2}]}}}
```

## `annotations.update`

Change annotations (one undo step).

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "update", "update": {...}}`

### Parameters of `update`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `items` | array of 1–200 objects | yes | Changes (a Locked annotation changes only its text) |
| `items[].id` | string, at most 32 characters | yes | Annotation id "obj:gen" (such as "12:0") from document.get_annotations |
| `items[].contents` | string, at most 65536 characters | no | New text |
| `items[].color` | string, at most 16 characters | no | Color: a palette name (yellow, orange, red, pink, purple, blue, green, black, white) or "#rrggbb" |
| `items[].fillColor` | string, at most 16 characters | no | Fill color: a palette name (yellow, orange, red, pink, purple, blue, green, black, white) or "#rrggbb" |
| `items[].width` | number, 0–100 | no | Border width in points |
| `items[].opacity` | number, 0–1 | no | Opacity |
| `items[].rect` | object {left, bottom, right, top} | no | New rectangle in PDF points |
| `items[].hidden` | boolean | no | Hide or show the annotation |

### Result of `update`

`structuredContent` is `{"action": "update", "update": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `changed` | array of strings | Ids of the changed annotations |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

A Locked annotation changes only its text; a ReadOnly annotation cannot change.

### Examples of `update`

Change the text of a comment:

```json
{"name": "annotations", "arguments": {"action": "update", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "update": {"items": [{"id": "12:0", "contents": "Fixed in version 3."}]}}}
```

Recolor two highlights and hide a note:

```json
{"name": "annotations", "arguments": {"action": "update", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "update": {"items": [{"id": "41:0", "color": "green"}, {"id": "42:0", "color": "green"}, {"id": "43:0", "hidden": true}]}}}
```

## `annotations.delete`

Delete annotations (one undo step).

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "delete", "delete": {...}}`

### Parameters of `delete`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `ids` | array of 1–1000 strings (at most 32 characters) | yes | Ids of the annotations |

### Result of `delete`

`structuredContent` is `{"action": "delete", "delete": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `deleted` | array of strings |  |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

The confirmation lists the deleted annotations. Delete only what the user asked for.

### Examples of `delete`

Delete the annotations you added before:

```json
{"name": "annotations", "arguments": {"action": "delete", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "delete": {"ids": ["41:0", "42:0"]}}}
```

Delete one annotation:

```json
{"name": "annotations", "arguments": {"action": "delete", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "delete": {"ids": ["108:0"]}}}
```

## `annotations.reply`

Reply to a comment.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "reply", "reply": {...}}`

### Parameters of `reply`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string, at most 32 characters | yes | Id of the annotation to reply to |
| `contents` | string, at most 65536 characters | yes | Text of the reply |

### Result of `reply`

`structuredContent` is `{"action": "reply", "reply": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `id` | string | Id of the new reply |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

### Examples of `reply`

Reply to a comment:

```json
{"name": "annotations", "arguments": {"action": "reply", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "reply": {"id": "12:0", "contents": "Agreed, see the appendix."}}}
```

Reply in a thread of another document:

```json
{"name": "annotations", "arguments": {"action": "reply", "documentId": "M2XQ7RTB4KD5W3ZP6YH7ACNEFG", "reply": {"id": "7:0", "contents": "Done."}}}
```
