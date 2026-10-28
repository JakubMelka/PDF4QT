# Opening and saving files

The tool `files` opens PDF files in PDF4QT and saves documents (Open and save files). Reference: pdf4qt://docs/tools/files.md. Assistants work only with files whose path the user gives them; PDF4QT does not list the user's files or recent documents.

## Paths

- Paths are **absolute**: `C:\Users\Jane\Documents\Report.pdf`, `/home/jane/Report.pdf`. A relative path is refused – ask the user for the full path.
- In JSON, a backslash is written twice: `"C:\\Users\\Jane\\Report.pdf"`. Forward slashes work on Windows too.
- Refused paths: network locations (`\\server\share`, mapped network drives, remote mounts), device paths (`\\?\`, `\\.\`), reserved device names (`CON`, `NUL`, `COM1`, …), alternate data streams (`file.pdf:stream`), folders, folders that do not exist, and the folders of PDF4QT's own data. The error says why; do not try to work around it.

## Open

```json
{"name": "files", "arguments": {"action": "open", "open": {"path": "C:\\Users\\Jane\\Documents\\Q3_Forecast.pdf", "page": 4}}}
```

- With Open and save files set to Ask, PDF4QT asks the user to open the file ("Claude Code wants to open a document").
- A file, which is open already, is not opened again: you get its `documentId` and its tab is activated. That needs only Read documents (with Ask, the read approval of your client, as every read).
- The file must be a PDF (it must start with `%PDF`).
- The call waits until the document is ready and returns its `documentId`, `title`, `pageCount` and `state`. Pass the `documentId` to the next calls. The reading of the file waits at most 2 minutes; a file still being read then gives `state: loading` and an error – call `application.get_state` later.
- A document with a password: PDF4QT shows its password screen and waits up to 2 minutes (from the moment the screen appears) for the user to type the password. Never ask the user for the password yourself – you never receive it. Without the password, the result has `state: password_required` and an error.
- `activate: false` opens the file in a background tab; `page` shows a page.

## Save

```json
{"name": "files", "arguments": {"action": "save", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
```

- `save` writes the document over its own file. It is **always confirmed** by the user – also when Open and save files is Allow – because it replaces the file on the disk.
- A signed document is saved incrementally: the signed bytes stay unchanged, and the signatures stay valid as far as the signers allowed changes.
- A copy made in PDF4QT (Edit a Copy) has no file: use `save_copy`.
- A locked document is not saved; a document without changes needs no saving.

## Save a copy

```json
{"name": "files", "arguments": {"action": "save_copy", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "save_copy": {"path": "C:\\Users\\Jane\\Documents\\Report_reviewed.pdf"}}}
```

- `save_copy` writes a copy to `path` (an absolute path ending with `.pdf`); the document in its tab stays as it is, with its own file.
- Without `path`, PDF4QT asks the user for it, suggesting `<name>_copy.pdf` next to the file.
- An existing file is refused unless you pass `overwrite: true`; overwriting is always confirmed by the user.
- A locked document can be saved as a copy (the copy does not change the document).
- With Open and save files set to Ask, the user confirms the copy ("Claude Code wants to save a copy as Report_reviewed.pdf").

## Good practice

- Do not save on your own: tell the user what changed and save when the user asks. The user can also save in PDF4QT.
- Prefer `save_copy` when the user wants to keep the original.
- Report the path of the saved file from the result.
