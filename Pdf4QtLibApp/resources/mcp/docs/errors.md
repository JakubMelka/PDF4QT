# Errors and what to do

PDF4QT reports two kinds of errors:

- **Execution errors** are tool results with `isError: true`. Their text is written for you: what happened, why, and what to pass or to ask the user. Read it and act on it.
- **Protocol errors** are JSON-RPC errors: the request itself was wrong or could not be served.

## Execution errors

| Error family (start of the text) | Meaning | What to do |
| --- | --- | --- |
| "Invalid arguments: …" | The arguments do not match the schema; the text names the JSON path, such as `search.query` | Fix the named argument. The reference of the tool lists every parameter: pdf4qt://docs/tools/{tool}.md |
| "items[2]: highlight needs quads …" | One item of a batch is incomplete | Fix the item; nothing of the call was done |
| "No open document has the id …" | The tab was closed, or the id is wrong | Call `application.get_state` for the open documents |
| "No document is open in PDF4QT …" | Nothing is open | Ask the user to open the document, or `files.open` a path the user gave you |
| "Several documents are open … Pass documentId, one of: …" | The document is not clear and your client cannot ask the user | Pick the right id from the list, or ask the user |
| "The document is still loading …" | The document is not ready | Try again in a moment |
| "The document needs a password …" | The user must type it in PDF4QT | Ask the user to type it in PDF4QT; never ask for the password itself |
| "The user denied …" | The user said no in PDF4QT | Tell the user; do not repeat the call |
| "The user did not answer … within 2 minutes" | The confirmation expired | Ask the user whether to try again |
| "The user does not allow …" | The permission is Deny | Tell the user; the permission can be changed on the AI page |
| "PDF4QT cannot ask the user …" | No confirmation can be shown right now | Ask the user to allow the action on the AI page, or try later |
| "The document is locked in PDF4QT …" | The user locked the document | Ask the user to unlock it or to use Edit a Copy |
| "PDF4QT is busy with another operation …" | A save, a print or a tool runs on the document | Try again when it has finished |
| "PDF4QT shows a dialog …" | A dialog of PDF4QT is open; changes are refused meanwhile | Try again after the user closes it |
| "Too many requests are waiting for the user …" | 16 requests wait in the tab | Wait for the answers; send fewer calls with more items each |
| "The document changed in the meantime …" | An annotation or an outline item you referred to is gone | Read the document again and call with the current ids |
| "The tab of the document was closed" | The user closed the document while the call ran | Tell the user |
| "… has unsaved changes …" (`view.close`) | Closing would lose the user's changes | Ask the user to save or to close the tab |
| "… use save_copy" | The document has no file of its own | Use `files.save_copy` with a path |
| "The image would have … megapixels …" / "The image has … MiB, more than the limit of 12 MiB …" | The image is too large | Render with a lower `dpi`, an `area`, or `jpeg` |
| "This tool is not available …" | A tool of the catalog was removed | Call `tools/list` again |
| "The run was canceled" / "The request was canceled" / "… stopped" | Your client canceled the call, or the server stopped | Nothing; call again if the user still wants it |
| "The user declined …" / "The user did not choose … in the client …" | The user declined a question of PDF4QT in your client | Ask the user in the conversation |
| "The choice of the user expired or is not valid …" | An answer to a question of PDF4QT came too late (5 minutes) | Call the tool again |

## Protocol errors

| Code / HTTP status | Meaning | What to do |
| --- | --- | --- |
| -32602 "Unknown tool" / "Unknown action …" | The tool or the action does not exist, or the user denies it | Use the names of `tools/list` |
| -32602 / -32002 "Resource not found" | The URI does not exist (a closed document) | List the resources again |
| -32601 / HTTP 404 | The method is not implemented by PDF4QT | Do not use it |
| -31001 / HTTP 429 | Too many calls; `data.retryAfterMs` says how long to wait | Wait, then send fewer calls |
| -31002 / HTTP 503 | The server is stopping | Wait until the user starts it again |
| HTTP 401 | The access token is missing or wrong | The user must set up the client again with the current token (AI page of PDF4QT) |
| HTTP 403 | The request came from a browser page or an unknown host | Connect directly, not through a browser |
| HTTP 404 with a session id | The session ended (30 minutes idle, disconnected, a new token, a restart) | Initialize a new session |

## Limits

| Limit | Value |
| --- | --- |
| Pages of `text.get_text` per call | 50 (and 1000000 characters) |
| Results of `text.search` | 500 |
| Items of `annotations.add` | 500 |
| Pages of `render.overview` | 24 |
| Resolution of `render.page` | 600 dpi, 50 megapixels |
| Attachment content | 8 MiB |
| Text resource of a document | 4194304 characters |
| Waiting confirmations per tab | 16 |
| Confirmation timeout | 2 minutes |
| Calls running at once | 16 per assistant |
| Rate | 20 calls per second (short bursts up to 50) per assistant |

`application.get_state` returns the limits, which apply to you, in `limits`.
