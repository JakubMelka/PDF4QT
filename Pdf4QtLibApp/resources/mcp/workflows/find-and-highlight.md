# Workflow: find and highlight

## Goal

Every occurrence of a text in a document open in PDF4QT highlighted in one step.

## Inputs

- `query` (required) – the text to find.
- `document` – the id or the title of an open document; omitted = the active document.
- `color` – the color of the highlights: `yellow`, `orange`, `red`, `pink`, `purple`, `blue`, `green`, `black` or `white`; omitted = `yellow`.

## Steps

1. Resolve the document (`application.get_state`); a locked document cannot be highlighted.
2. Search. Use `wholeWords` for a word, which is also part of other words; `matchCase` for names and abbreviations:

   ```json
   {"name": "text", "arguments": {"action": "search", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "search": {"query": "force majeure", "maxResults": 500}}}
   ```

3. No match: try a variant (singular / plural, without hyphens) once, then report that nothing was found.
4. More than 500 matches (`truncated`): tell the user and ask whether to highlight the first 500 or to narrow the query.
5. Add one `highlight` per match with its `page` and `quads`, all in one call:

   ```json
   {"name": "annotations", "arguments": {"action": "add", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "add": {"items": [
     {"type": "highlight", "page": 2, "quads": [[201.3, 512.6, 279.9, 512.6, 201.3, 500.5, 279.9, 500.5]], "color": "yellow"},
     {"type": "highlight", "page": 9, "quads": [[90.4, 300.2, 169, 300.2, 90.4, 288.1, 169, 288.1]], "color": "yellow"}
   ]}}}
   ```

6. Optionally show the first match: `view.go_to` its page.

## Output format

"Highlighted N occurrences of "query" on pages …" with the pages (labels when they differ), and that **Undo** in PDF4QT removes them at once.

## Pitfalls

- Never one call per match.
- Keep the quads of a match together in one item (a match across a line break has two quads).
- Text in scanned pages without a text layer cannot be found.
- Do not add `contents` unless the user asked for comments.
