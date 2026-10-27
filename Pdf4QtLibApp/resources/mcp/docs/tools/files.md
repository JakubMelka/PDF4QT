# Tool `files` – Open and save files

The tool `files` opens PDF files in PDF4QT and saves documents (Open and save files). Paths are absolute; network locations, devices and the folders of PDF4QT are refused. Saving over a file is always confirmed by the user, and a password is typed only in PDF4QT. Background and path rules: pdf4qt://docs/files.md.

| Action | Permission | Description |
| --- | --- | --- |
| `open` | Open and save files | Open a file (an open file is only activated) |
| `save` | Open and save files | Save the document over its own file (always confirmed) |
| `save_copy` | Open and save files | Save a copy of the document; the tab is unchanged |

Hints of the tool: changes PDF4QT or documents, destructive.

## Common parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `documentId` | string, at most 26 characters | no | Id of an open document (26 characters, from application.get_state). Omitted: the active document tab, else the only open document. |

## `files.open`

Open a file (an open file is only activated).

- Permission: Open and save files
- Common parameters: none
- Arguments: `{"action": "open", "open": {...}}`

### Parameters of `open`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `path` | string, at most 4096 characters | yes | Absolute path of a PDF file |
| `page` | integer, ≥ 1 | no | Page to show from 1 |
| `activate` | boolean, default `true` | no | Switch to the tab |

### Result of `open`

`structuredContent` is `{"action": "open", "open": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string |  |
| `title` | string |  |
| `pageCount` | integer |  |
| `state` | string, one of `ready`, `loading`, `password_required`, `failed` |  |

An open file is only activated (Read documents is enough). The call waits until the document is ready; a document with a password waits up to 2 minutes for the user to type it.

### Examples of `open`

Open a file on Windows:

```json
{"name": "files", "arguments": {"action": "open", "open": {"path": "C:\\Users\\Jane\\Documents\\Contract.pdf"}}}
```

Open a file at a page, in a background tab:

```json
{"name": "files", "arguments": {"action": "open", "open": {"path": "/home/jane/papers/survey.pdf", "page": 12, "activate": false}}}
```

## `files.save`

Save the document over its own file (always confirmed).

- Permission: Open and save files
- Common parameters: `documentId`
- Arguments: `{"action": "save", "save": {...}}`

### Parameters of `save`

No parameters.

### Result of `save`

`structuredContent` is `{"action": "save", "save": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `saved` | boolean |  |
| `path` | string |  |

Always confirmed. Signed documents are saved incrementally. A document without a file needs `files.save_copy`.

### Examples of `save`

Save the document over its file (the user confirms):

```json
{"name": "files", "arguments": {"action": "save", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

Save another document:

```json
{"name": "files", "arguments": {"action": "save", "documentId": "M2XQ7RTB4KD5W3ZP6YH7ACNEFG", "save": {}}}
```

## `files.save_copy`

Save a copy of the document; the tab is unchanged.

- Permission: Open and save files
- Common parameters: `documentId`
- Arguments: `{"action": "save_copy", "save_copy": {...}}`

### Parameters of `save_copy`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `path` | string, at most 4096 characters | no | Absolute path of the copy (.pdf); omitted = asked |
| `overwrite` | boolean, default `false` | no | Replace an existing file (always confirmed) |

### Result of `save_copy`

`structuredContent` is `{"action": "save_copy", "save_copy": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `documentId` | string | Id of the document - pass it to the next calls |
| `title` | string | Title of the document tab |
| `path` | string |  |

The tab keeps its document and file. Without `path`, PDF4QT asks the user (default `<name>_copy.pdf` next to the file).

### Examples of `save_copy`

Save a copy next to the original:

```json
{"name": "files", "arguments": {"action": "save_copy", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "save_copy": {"path": "C:\\Users\\Jane\\Documents\\Contract_reviewed.pdf"}}}
```

Overwrite an existing copy (the user confirms):

```json
{"name": "files", "arguments": {"action": "save_copy", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "save_copy": {"path": "/home/jane/papers/survey_notes.pdf", "overwrite": true}}}
```

Let PDF4QT ask the user for the path:

```json
{"name": "files", "arguments": {"action": "save_copy", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "save_copy": {}}}
```
