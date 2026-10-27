# Tool `view` – Show in PDF4QT

The tool `view` shows things to the user in the PDF4QT window: it switches tabs, goes to a page, points at an area, opens the search panel, navigates to a page of the application, or closes a tab. It needs Read documents and never changes a document. Use it to show the user what you are talking about – but do not jump around while the user reads; `activate: false` changes a background tab without switching to it.

| Action | Permission | Description |
| --- | --- | --- |
| `activate` | Read documents | Switch to the tab of the document (documentId required) |
| `go_to` | Read documents | Show a page, an outline item or a named destination (one of them) |
| `show_area` | Read documents | Scroll to an area and outline it for 3 seconds ("look here") |
| `show_search` | Read documents | Open the search panel with the query; the results are highlighted |
| `navigate` | Read documents | Navigate the Home or the document tab like a link |
| `close` | Read documents | Close the tab of the document (documentId required; refused with unsaved changes) |

Hints of the tool: changes PDF4QT or documents, idempotent.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `view.activate`

Switch to the tab of the document (documentId required).

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "activate", "activate": {...}}`

### Parameters of `activate`

No parameters.

### Result of `activate`

`structuredContent` is `{"action": "activate", "activate": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `activeDocumentId` | string |  |

### Examples of `activate`

Switch to the tab of a document:

```json
{"name": "view", "arguments": {"action": "activate", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

Switch to another document:

```json
{"name": "view", "arguments": {"action": "activate", "documentId": "M2XQ7RTB4KD5W3ZP6YH7ACNEFG", "activate": {}}}
```

## `view.go_to`

Show a page, an outline item or a named destination (one of them).

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "go_to", "go_to": {...}}`

### Parameters of `go_to`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `page` | integer, ≥ 1 | no | Physical page number from 1 |
| `pageLabel` | string, at most 256 characters | no | Page label as printed in the document ("iv", "A-2"); used instead of page |
| `outlineItem` | string, at most 256 characters | no | Outline item id: a 1-based dotted path such as "2.1.4" (from document.get_outline) |
| `namedDestination` | string, at most 1024 characters | no | Named destination of the document |
| `fit` | string, one of `page`, `width` | no | Fit the page or its width (instead of zoom) |
| `zoom` | number, 10–6400 | no | Zoom in percent |
| `activate` | boolean, default `true` | no | Switch to the tab |

### Result of `go_to`

`structuredContent` is `{"action": "go_to", "go_to": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `page` | integer | Physical page number from 1 |
| `label` | string | Page label |

Pass one of `page`, `pageLabel`, `outlineItem` or `namedDestination`; `fit` or `zoom` set the zoom.

### Examples of `go_to`

Go to a page:

```json
{"name": "view", "arguments": {"action": "go_to", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "go_to": {"page": 14}}}
```

Go to a page by its label and fit its width:

```json
{"name": "view", "arguments": {"action": "go_to", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "go_to": {"pageLabel": "iv", "fit": "width"}}}
```

Go to an outline item at 150 %, without switching the tab:

```json
{"name": "view", "arguments": {"action": "go_to", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "go_to": {"outlineItem": "3.2", "zoom": 150, "activate": false}}}
```

Go to a named destination:

```json
{"name": "view", "arguments": {"action": "go_to", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "go_to": {"namedDestination": "chapter.4"}}}
```

## `view.show_area`

Scroll to an area and outline it for 3 seconds ("look here").

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "show_area", "show_area": {...}}`

### Parameters of `show_area`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `page` | integer, ≥ 1 | yes | Physical page number from 1 |
| `rect` | object {left, bottom, right, top} | yes | Area in PDF points of the unrotated page |
| `activate` | boolean, default `true` | no | Switch to the tab |

### Result of `show_area`

`structuredContent` is `{"action": "show_area", "show_area": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `page` | integer | Physical page number from 1 |
| `label` | string | Page label |

The area is outlined for 3 seconds.

### Examples of `show_area`

Point at a paragraph:

```json
{"name": "view", "arguments": {"action": "show_area", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "show_area": {"page": 5, "rect": {"left": 72, "bottom": 430, "right": 540, "top": 520}}}}
```

Point at a figure in a background tab:

```json
{"name": "view", "arguments": {"action": "show_area", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "show_area": {"page": 9, "rect": {"left": 100, "bottom": 200, "right": 500, "top": 450}, "activate": false}}}
```

## `view.show_search`

Open the search panel with the query; the results are highlighted.

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "show_search", "show_search": {...}}`

### Parameters of `show_search`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `query` | string, at most 1024 characters | yes | Text to find (1 to 1024 characters) |
| `matchCase` | boolean, default `false` | no | Match the case of the letters |
| `wholeWords` | boolean, default `false` | no | Match whole words only |
| `regularExpression` | boolean, default `false` | no | The query is a regular expression (PCRE2); not with wildcards |
| `wildcards` | boolean, default `false` | no | The query has the wildcards * and ?; not with regularExpression |

### Result of `show_search`

`structuredContent` is `{"action": "show_search", "show_search": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `query` | string |  |

### Examples of `show_search`

Open the search panel with a query:

```json
{"name": "view", "arguments": {"action": "show_search", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "show_search": {"query": "warranty"}}}
```

A regular expression search for the user:

```json
{"name": "view", "arguments": {"action": "show_search", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "show_search": {"query": "\\bArticle [0-9]+\\b", "regularExpression": true, "matchCase": true}}}
```

## `view.navigate`

Navigate the Home or the document tab like a link.

- Permission: Read documents
- Common parameters: none
- Arguments: `{"action": "navigate", "navigate": {...}}`

### Parameters of `navigate`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `route` | string, at most 2048 characters | yes | Route of the application, such as /help/annotations or /tools |

### Result of `navigate`

`structuredContent` is `{"action": "navigate", "navigate": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `route` | string |  |

A route of the application, such as `/help/ai-permissions`, `/tools` or `/home`. Routes, which PDF4QT does not know, are refused.

### Examples of `navigate`

Open a topic of the user manual:

```json
{"name": "view", "arguments": {"action": "navigate", "navigate": {"route": "/help/annotations"}}}
```

Open the Tools page:

```json
{"name": "view", "arguments": {"action": "navigate", "navigate": {"route": "/tools"}}}
```

## `view.close`

Close the tab of the document (documentId required; refused with unsaved changes).

- Permission: Read documents
- Common parameters: `documentId`
- Arguments: `{"action": "close", "close": {...}}`

### Parameters of `close`

No parameters.

### Result of `close`

`structuredContent` is `{"action": "close", "close": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `closed` | boolean |  |

Refused when the document has unsaved changes, when a save, a print or a tool runs on it, or when another assistant waits for a confirmation in it. Ask the user instead.

### Examples of `close`

Close a tab, which has no unsaved changes:

```json
{"name": "view", "arguments": {"action": "close", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

Close another document:

```json
{"name": "view", "arguments": {"action": "close", "documentId": "M2XQ7RTB4KD5W3ZP6YH7ACNEFG", "close": {}}}
```
