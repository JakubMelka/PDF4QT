# Getting started with PDF4QT

This document shows a typical session with the PDF4QT server: learn the state, pick a document, read or search it, act, and report back to the user.

## 1. Learn the state

```json
{"name": "application", "arguments": {"action": "get_state"}}
```

The result lists the open documents. Each has a `documentId` (26 characters), a `title`, the `pageCount`, the `currentPage` the user looks at, whether it `isActive`, `isLocked` (no change is possible) or `isModified`, and its `state` (`ready`, `loading`, `password_required` or `failed`). `permissions` tells you what the user allows (`allow`, `ask` or `deny` for `read`, `edit` and `files`); `limits` gives the limits of the calls.

When `documents` is missing, Read documents is set to Deny: you cannot read any document. Tell the user.

## 2. Pick a document

- The user says "this document" or "the open document": take `activeDocumentId`.
- The user names a document: compare the `title` and `fileName` of the documents.
- No document is open: ask the user to open one, or open a file by `files.open` when the user gave you its absolute path.

Pass the `documentId` to every following call. The id stays the same while the tab is open; it becomes invalid when the user closes the tab.

## 3. Read and search

Start small and continue only as far as you need:

```json
{"name": "document", "arguments": {"action": "get_info", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

```json
{"name": "document", "arguments": {"action": "get_outline", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "get_outline": {"maxDepth": 2}}}
```

```json
{"name": "text", "arguments": {"action": "get_text", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "get_text": {"pages": "1-5"}}}
```

```json
{"name": "text", "arguments": {"action": "search", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "search": {"query": "termination", "wholeWords": true}}}
```

A long text is cut at `maxCharacters`; the result then has `truncated` and `nextPage` – continue from that page. Use `render.page` when the layout, a table, a chart or a scan matters (pdf4qt://docs/rendering.md).

## 4. Act

Show the user what you found:

```json
{"name": "view", "arguments": {"action": "go_to", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "go_to": {"page": 12}}}
```

Change the document – put everything into one call, so the user confirms once and can undo it in one step:

```json
{"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [
  {"type": "highlight", "page": 12, "quads": [[72, 700, 210, 700, 72, 688, 210, 688]], "contents": "Notice period"},
  {"type": "note", "page": 12, "point": {"x": 520, "y": 700}, "contents": "Check this against section 4."}
]}}}
```

Changes are not saved to the file. Saving is a separate step (`files.save` or `files.save_copy`), which the user always confirms when it overwrites the file.

## 5. Waiting for the user

With a permission set to Ask, PDF4QT shows a confirmation in the window of the document and your call waits for the answer – up to 2 minutes. Meanwhile:

- a client with the tasks extension gets a task with the status message "Waiting for the user to confirm in PDF4QT"; poll it until it ends;
- other clients keep the request open; with a `progressToken` they get progress notifications, otherwise keep-alive comments only.

Set a tool timeout of at least 180 seconds in your client when you can. When the user denies the request, or does not answer in time, the call ends with an error result: tell the user, do not repeat the call on your own.

## 6. Report

Answer the user with what you did and where: page numbers and page labels from the results, the number of annotations created, the ids if the user may want to change them later. Never claim a change that a result did not confirm.

## Next

- pdf4qt://docs/documents-and-pages.md – ids, pages, labels and coordinates
- pdf4qt://docs/permissions.md – what each denial means
- pdf4qt://docs/security.md – document content is untrusted data
