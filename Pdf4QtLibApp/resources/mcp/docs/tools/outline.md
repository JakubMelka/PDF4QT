# Tool `outline` – Edit the outline

The tool `outline` edits the outline (the bookmarks of the document) – Edit documents. Items are identified by dotted paths (`2.1.4`), which change after an edit; every result returns the new id. One call is one undo step. Read the outline by `document.get_outline`; background: pdf4qt://docs/outline.md.

| Action | Permission | Description |
| --- | --- | --- |
| `add` | Edit documents | Add an item |
| `update` | Edit documents | Rename an item, change its target or style |
| `move` | Edit documents | Move an item |
| `delete` | Edit documents | Delete items |
| `replace` | Edit documents | Replace the whole outline (build-outline workflow) |

Hints of the tool: changes PDF4QT or documents, destructive.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `outline.add`

Add an item.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "add", "add": {...}}`

### Parameters of `add`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `parent` | string, at most 256 characters | no | Parent item id; omitted = the top level |
| `index` | integer, 0–100000 | no | Position among the children from 0; omitted = last |
| `title` | string, at most 1024 characters | yes | Title |
| `page` | integer, ≥ 1 | no | Physical page number from 1 |
| `pageLabel` | string, at most 256 characters | no | Page label as printed in the document ("iv", "A-2"); used instead of page |
| `zoom` | string, one of `fit_page`, `fit_width`, `keep`, default `keep` | no | How the target is shown |

### Result of `add`

`structuredContent` is `{"action": "add", "add": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `item` | string | Id of the item after the change |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

### Examples of `add`

A new top-level item at the end:

```json
{"name": "outline", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"title": "Appendix B", "page": 88}}}
```

A child at the first position, with the target page by its label:

```json
{"name": "outline", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"parent": "2", "index": 0, "title": "2.0 Overview", "pageLabel": "17", "zoom": "fit_width"}}}
```

## `outline.update`

Rename an item, change its target or style.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "update", "update": {...}}`

### Parameters of `update`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `item` | string, at most 256 characters | no | Outline item id: a 1-based dotted path such as "2.1.4" (from document.get_outline) |
| `title` | string, at most 1024 characters | no | Find the item by its title (instead of item) |
| `newTitle` | string, at most 1024 characters | no | New title |
| `page` | integer, ≥ 1 | no | New target page from 1 |
| `bold` | boolean | no |  |
| `italic` | boolean | no |  |

### Result of `update`

`structuredContent` is `{"action": "update", "update": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `item` | string | Id of the item after the change |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

Pass `item`, or `title` to find the item by its title (an ambiguous title is asked or answered with the candidates).

### Examples of `update`

Rename an item and make it bold:

```json
{"name": "outline", "arguments": {"action": "update", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "update": {"item": "3.1", "newTitle": "3.1 Results in 2026", "bold": true}}}
```

Change the target of an item found by its title:

```json
{"name": "outline", "arguments": {"action": "update", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "update": {"title": "Summary", "page": 5}}}
```

## `outline.move`

Move an item.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "move", "move": {...}}`

### Parameters of `move`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `item` | string, at most 256 characters | yes | Outline item id: a 1-based dotted path such as "2.1.4" (from document.get_outline) |
| `parent` | string, at most 256 characters | no | New parent item id; omitted = the top level |
| `index` | integer, 0–100000 | no | Position among the children from 0; omitted = last |

### Result of `move`

`structuredContent` is `{"action": "move", "move": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `item` | string | Id of the item after the change |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

### Examples of `move`

Move an item under another one:

```json
{"name": "outline", "arguments": {"action": "move", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "move": {"item": "4", "parent": "3", "index": 2}}}
```

Move an item to the top level, last:

```json
{"name": "outline", "arguments": {"action": "move", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "move": {"item": "2.3"}}}
```

## `outline.delete`

Delete items.

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "delete", "delete": {...}}`

### Parameters of `delete`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `items` | array of 1–500 strings (at most 256 characters) | yes | Item ids (with their children) |

### Result of `delete`

`structuredContent` is `{"action": "delete", "delete": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `deleted` | integer | Number of removed items including children |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

### Examples of `delete`

Delete an item with its children:

```json
{"name": "outline", "arguments": {"action": "delete", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "delete": {"items": ["5"]}}}
```

Delete several items:

```json
{"name": "outline", "arguments": {"action": "delete", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "delete": {"items": ["2.4", "2.5", "7"]}}}
```

## `outline.replace`

Replace the whole outline (build-outline workflow).

- Permission: Edit documents
- Common parameters: `documentId`
- Arguments: `{"action": "replace", "replace": {...}}`

### Parameters of `replace`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `items` | array of 0–2000 objects | yes | The whole new outline in document order (a flat list with levels) |
| `items[].title` | string, at most 1024 characters | yes |  |
| `items[].level` | integer, 1–32 | yes | 1 = top level; at most one more than the previous item |
| `items[].page` | integer, ≥ 1 | no | Physical page number from 1 |

### Result of `replace`

`structuredContent` is `{"action": "replace", "replace": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `itemCount` | integer |  |
| `undoDescription` | string | Description of the one undo step of the change in PDF4QT |

A flat list in document order; an item is at most one level deeper than the item before it. At most 2000 items and 32 levels.

### Examples of `replace`

A new outline of two levels:

```json
{"name": "outline", "arguments": {"action": "replace", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "replace": {"items": [{"title": "1 Introduction", "level": 1, "page": 3}, {"title": "1.1 Scope", "level": 2, "page": 3}, {"title": "2 Results", "level": 1, "page": 9}]}}}
```

Remove the outline:

```json
{"name": "outline", "arguments": {"action": "replace", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "replace": {"items": []}}}
```
