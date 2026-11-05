---
title: Optimize Structure
category: tools
order: 30
summary: Make the file smaller without changing how its pages look.
keywords: optimize, structure, objects, lossless, clean up, remove unused objects, merge identical objects, recompress, flate, object streams
related: /tools/optimize.structure, /help/tools
---
Optimize Structure makes a PDF smaller by cleaning up its inner objects. Every step is lossless: text, images, forms, annotations and the pages look exactly the same. Images are not recompressed; for that use Compress or Optimize Images.

### Steps

Steps in the toolbar opens the list of the steps. Each one can be turned off; Select all and Clear turn on or off all of them, Restore defaults turns all six on again.

| Step | What it does |
| --- | --- |
| Embed simple objects | Small values (numbers, names, short strings) are written in place instead of being referenced |
| Remove null objects | Entries of dictionaries without a value are dropped |
| Remove unused objects | Objects no part of the document refers to, leftovers of earlier edits, are deleted |
| Merge identical objects | Objects with the same content are stored once |
| Shrink object storage | The objects are renumbered so no gaps remain |
| Recompress flate streams | Compressed streams are compressed again with the strongest setting; the slowest step, it often saves the most |

With Remove unused objects turned off, objects, which only an older version referred to, stay in the file.

### Step by step

1. Choose the document. The tool opens in the document’s tab with all six steps on; the right panel shows the size of the file and the number of its objects and streams.
2. Turn off the steps you do not want. The list Steps to run shows what will run.
3. Press Optimize Structure. The summary shows the size before and after, the steps that ran and the time, and lets you keep the result in this tab, save it as a copy or overwrite the original file. Copy report puts the whole report on the clipboard.

### What happens to the file

- The file is written whole: its old object streams, cross-reference streams and linearization data (Fast Web View) are not carried over, and old revisions of the file are gone.
- The PDF version of the file stays as it is.
- A declaration of PDF/A or PDF/UA is removed from the metadata of the result: it is not checked against the standard, and the result says so.
- A signed document can be saved only as a copy. The signatures of the copy are no longer valid.
- An encrypted document stays encrypted with the same password.

> [!WARNING] When the file cannot be made smaller
> If the chosen steps do not reduce the size, the tool says “File cannot be reduced with these steps” and writes nothing. Turn on more steps, or try Compress, which also reduces images.

### Command line

The same clean-up is available in PdfTool for scripts and batch jobs. The command overwrites its input file; `--opt-all` runs all the steps, each step has its own option:

```
pdftool optimize --opt-all input.pdf
pdftool optimize --opt-remove-unused --opt-merge-identical --opt-recompress-flate input.pdf
```
