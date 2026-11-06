# OCR

The specification (OCR.md, Czech), the implementation report (OCR_REPORT.md) and the plan of the extensions (OCR_PLAN.md) are not in this working copy.

## Parts

- **Core** (`pdfocr*.h` in Pdf4QtLibCore, engine independent): the data model in the canonical unrotated PDF user space (word quads ordered bottom-left, bottom-right, top-right, top-left of the reading direction); engines return raw pixel-space output, which `PDFOCRPagePreparer` maps back by `pageToRaster * rasterToEngine`. The job controller runs a worker pool with one engine instance per worker, a memory budget and cancellation; the session holds results and corrections with undo / redo.
- **Engine**: the Tesseract adapter is the separate shared library [Pdf4QtOcrTesseract](../Pdf4QtOcrTesseract/) (option `PDF4QT_ENABLE_OCR`, define `PDF4QT_OCR_TESSERACT`). Nothing else includes Tesseract or Leptonica headers. PdfTool links it only with the option and registers the engine in `main`.
- **Dialogs** in Pdf4QtLibGui. Keep the object names of the two large `.ui` files stable: the dialog test looks the widgets up by name.
- **Command line**: `PdfTool ocr` / `ocr-models` use the synchronous `PDFOCRDocumentRunner` (it runs its own event loop - call it from a thread with an event dispatcher); a download never happens without `--accept-download`.

## Page analysis and fingerprint

- Existing digital or foreign invisible text is found first; a scan with any such text is `Mixed` and needs a decision of the user.
- Rasterization honors `/UserUnit` and the dimension limit of the engine.
- The page fingerprint `PDF4QT-OCR-PAGE-FINGERPRINT-3` covers the full raw streams of the resources, the annotations and the default optional content configuration, excluding only the verified streams of the own layer; real numbers by the exact text of the writer.

## Text layer

- Invisible text with an embedded glyphless font shared by the pages; the text origin lies on the line baseline and a negative `Ts` shifts the glyph boxes back onto the word geometry.
- Foreign content is enclosed between `q` / `Q` isolation streams - shared when the foreign content is balanced, page specific with extra `ET` / `EMC` / `Q` closers otherwise; the nesting is checked by the lexer.
- The own layer is identified by the page `/PieceInfo /PDF4QT_OCR`. Its references are untrusted input: content and data streams are bound by SHA-256 of the decoded streams and the content must pass `isInvisibleTextStream`. A stream changed by another tool is foreign content - part of the fingerprint, never removed, and `readLayer` returns nothing.
- `apply` refuses review-only results and words colliding with existing text, and recomputes the exclusion flags. `PDFPage::getContents()` is already dereferenced: use `getPageContentReferences`.
- The write transaction (`PDFOCRApplyProcessor`, used by the dialog and the command line): verify fingerprints → compress images → write the layer → strip the conformance of a copy → validate → write the copy. An own layer is rebound when only the image encoding changed (also by `PDFImageOptimizer`).
- Image compression: JBIG2 only as a generic region, never a symbol dictionary; lossy modes need the preview of the dialog and are not offered in a batch.

## Models

- Catalog with pinned checksums in [ocr/catalog](../ocr/catalog/); profiles `fast` / `standard` / `best` = the repositories tessdata_fast / tessdata / tessdata_best. Only `fast` is distributed, the larger profiles are downloaded by the user.
- The built-in models are committed as one xz archive per model; CMake extracts them into `<build>/<PDF4QT_INSTALL_OCR_DIR>`, which is installed and read by the tests.
- [ocr/tools/generate_catalog.py](../ocr/tools/generate_catalog.py) generates the catalog, manifests, licenses and archives (`--fetch-builtin` restores an archive without the 3 GB cache, `--<profile>-commit` moves a pinned commit, `--builtin-<profile>` changes a built-in set - then update [WixInstaller/Product.wxs.in](../WixInstaller/Product.wxs.in) by hand).
- Downloads over https; runtime sets are built in place under a lock and published by `complete.json` written last - never renamed.
- The OCR data root is shared by all applications (`getApplicationDataRoot` maps them to `PDF4QT Editor`), so PdfTool sees the models downloaded in the editor.
- Dictionary flags of the words come from the model (no extra dictionary library); words outside it need a review by default.
