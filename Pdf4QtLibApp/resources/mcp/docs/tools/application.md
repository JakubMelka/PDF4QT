# Tool `application` – PDF4QT application

The tool `application` returns the state of PDF4QT: the open documents with their ids, the active document, the permissions the user has set for assistants and the limits of the calls. Call it first in every session and whenever you need the current list of documents (the user may open and close tabs at any time).

- `application.get_state` needs no permission for the state itself; the list `documents` needs Read documents. With Read documents set to Ask, the first call while documents are open asks the user once (pdf4qt://docs/permissions.md); with Deny, `documents` is absent and `note` says why.
- `path` of a document is given only when Open and save files is not Deny.
- The result is also the resource pdf4qt://application/state, which you can subscribe to.

| Action | Permission | Description |
| --- | --- | --- |
| `get_state` | none (always available) | State of PDF4QT and the list of the open documents |

Hints of the tool: read only, idempotent.

## Common parameters

No parameters.

## `application.get_state`

State of PDF4QT and the list of the open documents.

- Permission: none (always available)
- Common parameters: none
- Arguments: `{"action": "get_state", "get_state": {...}}`

### Parameters of `get_state`

No parameters.

### Result of `get_state`

`structuredContent` is `{"action": "get_state", "get_state": {...}}` with these fields:

| Field | Type | Description |
| --- | --- | --- |
| `version` | string | Version of PDF4QT |
| `era` | string | Protocol version of this request |
| `permissions` | object | Permissions set by the user in PDF4QT |
| `permissions.read` | string, one of `allow`, `ask`, `deny` | Read documents |
| `permissions.edit` | string, one of `allow`, `ask`, `deny` | Edit documents |
| `permissions.files` | string, one of `allow`, `ask`, `deny` | Open and save files |
| `readApproved` | boolean | The user has approved reading of the documents for this assistant |
| `activeDocumentId` | string | Id of the document of the active tab (absent when the active tab is not a document) |
| `activeRoute` | string | Route of the active tab, such as /home or /document/{id} |
| `documents` | array of objects | Open documents (absent when Read documents is Deny) |
| `documents[].documentId` | string | Id of the document |
| `documents[].title` | string | Title of the document tab |
| `documents[].fileName` | string | Name of the file (empty for a copy without a file) |
| `documents[].path` | string | Absolute path of the file (only when Open and save files is not Deny) |
| `documents[].pageCount` | integer |  |
| `documents[].currentPage` | integer | Page shown in the tab, from 1 |
| `documents[].isActive` | boolean | The tab of the document is the active tab |
| `documents[].isLocked` | boolean | Locked in PDF4QT: no change is possible |
| `documents[].isModified` | boolean | Has unsaved changes |
| `documents[].isSigned` | boolean |  |
| `documents[].hasForm` | boolean |  |
| `documents[].isEncrypted` | boolean |  |
| `documents[].state` | string, one of `ready`, `loading`, `password_required`, `failed` |  |
| `limits` | object {maxRenderDpi, maxRenderMegapixels, maxTextPages, maxTextCharacters, maxSearchResults, maxAnnotationItems, maxAttachmentBytes, confirmationTimeoutSeconds} | Limits of the calls |
| `docs` | string | URI of the documentation index |
| `note` | string | A note for the assistant (such as why the documents are missing) |

### Examples of `get_state`

The state at the start of a session:

```json
{"name": "application", "arguments": {"action": "get_state"}}
```

The same with an explicit empty parameter object:

```json
{"name": "application", "arguments": {"action": "get_state", "get_state": {}}}
```
