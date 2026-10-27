# Workflow: build an outline

## Goal

An outline (the bookmarks of the document) built from the real headings of a document open in PDF4QT, written in one step.

## Inputs

- `document` – the id or the title of an open document; omitted = the active document.
- `depth` – the number of levels, 1 to 4; omitted = 2.

## Steps

1. Resolve the document (`application.get_state`). A locked document cannot be changed.
2. Read the current outline:

   ```json
   {"name": "document", "arguments": {"action": "get_outline", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE"}}
   ```

   When it has items, show them to the user and ask whether to replace them – replacing loses the user's outline.
3. Find the headings. A table of contents page is the fastest source: search for "Contents" and read that page. Otherwise read the pages in chunks (`text.get_text` with `geometry` `lines` shows the larger, separate lines of headings) or search for the numbering pattern:

   ```json
   {"name": "text", "arguments": {"action": "search", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "search": {"query": "\\b[0-9]+(\\.[0-9]+)+ [A-Z][a-z]+", "regularExpression": true, "maxResults": 500}}}
   ```

4. For every heading, take the physical `page` where it really is (a match), not the number printed in a table of contents – they often differ.
5. Build the flat list in document order with levels up to `depth`; a level is at most one deeper than the previous item.
6. Write it in one call:

   ```json
   {"name": "outline", "arguments": {"action": "replace", "documentId": "K3FQW2M7B4XDZR5T6YH5ZCANPE", "replace": {"items": [
     {"title": "1 Introduction", "level": 1, "page": 3},
     {"title": "1.1 Background", "level": 2, "page": 3},
     {"title": "2 Method", "level": 1, "page": 7}
   ]}}}
   ```

7. Show the user the Outline panel by going to the first page of the document (`view.go_to`).

## Output format

"Created an outline with N items on L levels." followed by the top-level items with their pages, and that **Undo** in PDF4QT restores the previous outline.

## Pitfalls

- Titles are copied from the document exactly (without the dot leaders and page numbers of a table of contents).
- Never invent headings or pages.
- At most 2000 items and 32 levels; for `depth` 1 to 4 that is plenty.
- An outline, which PDF4QT cannot edit safely, is refused; report it.
