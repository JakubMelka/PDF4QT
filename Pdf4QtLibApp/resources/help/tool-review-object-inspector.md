---
title: Object Inspector
category: tools
order: 81
summary: Browse the internal objects of the PDF file.
keywords: object inspector, objects, streams, dictionary, pdf syntax, developer, xref, internal structure, hex dump
related: /tools/review.object-inspector, /tools/review.statistics, /tools/review.report, /help/tools
---
Object Inspector shows the internal objects of the file. Nothing in the document changes; the tool has only Close.

### Views

The view button of the toolbar chooses what the tree shows:

- **Document** – everything, starting from the trailer of the file.
- **Pages**, **Content Streams**, **Graphic States**, **Color Spaces**, **Patterns**, **Shadings**, **Images**, **Forms**, **Fonts**, **Actions**, **Annotations** – the objects of one kind, with their number. A kind the document does not contain is not listed.
- **Object List** – every object of the file by its number. References are not followed there.

### Working with the tree

- The tree is read lazily: the entries of an object are read when you open it.
- A reference is opened only once in a view; at its other places it is shown, but not expanded. Selecting it shows the object on the right.
- An object stored in an object stream says so in the right panel.

### Contents

- A dictionary, an array or a value is shown in PDF syntax.
- A stream is decoded when you select it. Text is shown as text, binary data as a hex dump. Only the first 1 MB is shown; a stream that would decode to more than 256 MB is not decoded at all. When a filter fails, the canvas names it.
- An image is shown as a picture, with its size, bits per component, color space and filter.

### Pinned tabs

**Pin** keeps the current object in its own tab next to *Current Object*, so you can compare it with another one. A pinned tab is a snapshot and does not follow the tree; **Unpin** closes it.

### Copying

Copying the content of a stream is extraction of the content of the document. A document that does not allow copying asks for the owner password first.
