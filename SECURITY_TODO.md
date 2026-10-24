# Security backlog

Security weaknesses found during the work on the GUI overhaul (new application `PDF4QT`), which lie outside its scope - mostly in `Pdf4QtLibCore` and therefore also in the old applications (Editor, Viewer, PageMaster, Diff, PdfTool). They are collected here and solved together in a separate task, not in the redesign (owner decision, 2026-10-24).

Each entry: where, the attack, the proposed fix. Remove an entry when it is fixed (with its test); the commit message names it.

## Open

### 1. Decompression bombs – no limit of the decoded size of a stream

- **Where:** `PDFFlateDecodeFilter::uncompress` and the other decoding filters (`pdfstreamfilters.cpp`), `PDFDocument::getDecodedStream` and every caller.
- **Attack:** a small hostile PDF contains a stream, which expands to tens of gigabytes (nested Flate filters, a long run of zeros). The application allocates until `std::bad_alloc` – in a worker thread it ends the process, in the GUI thread the application crashes. Reached by opening the document (page content, images, XMP metadata, fonts, embedded files, form appearances), also by the thumbnailer of the new application.
- **Today:** the new application catches `std::bad_alloc` only when an attachment is written (`PDFAppAttachments::write`); the XMP reader of the properties page catches only `PDFException`.
- **Fix:** a limit of the decoded size per stream in the filters (configurable, a sane default, e.g. a multiple of the file size bounded by an absolute maximum), reported as a `PDFException` / a rendering error; tests with nested Flate bombs for each consumer.

### 2. Image decoding limits (inline images included)

- **Where:** image decoding in Core (`PDFImage`, JPEG / JPEG 2000 / JBIG2 / CCITT decoders, inline images of the content stream).
- **Attack:** a declared image size of billions of pixels or a decoder input, which allocates far more than the stream is long.
- **Today:** the thumbnailer of the new application limits the largest image of page 1 to 64 MP (`thumbnailerImageLimit`), the viewers have no limit.
- **Fix:** a pixel / memory limit of a decoded image in Core, reported as a rendering error.

### 3. Form field tree – shared subtrees multiply the fields

- **Where:** `PDFFormField::parse` / `parseImpl` (`pdfform.cpp`).
- **Attack:** N root fields of `/AcroForm /Fields` share one subtree of M kids. The guard added in the redesign (cycle detection and `MAX_FIELD_TREE_DEPTH` = 128) works per root field, so the parser builds N × M fields – a memory blow-up from a small file.
- **Fix:** a limit of the total number of fields of a form (e.g. 100 000) – no change for valid files. (Sharing the set of used references across the roots would change, to which parent a field listed twice belongs, also in the old applications.)

### 4. Document reading cannot be canceled

- **Where:** `PDFDocumentReader`.
- **Attack / problem:** a huge or hostile file keeps the reader busy, nothing can stop it (the thumbnailer of the new application reads in parts and checks a budget between them, the readers of the documents do not).
- **Fix:** a cancellation token / time and memory budget in the reader.

### 5. OpenSSL thread state in the old applications

- **Where:** signature verification of `Pdf4QtLibGui` (`PDFProgramController`, a worker at the opening of a document).
- **Problem:** OpenSSL keeps an error state per thread; a pool thread alive at the exit of the process leaks it (seen by valgrind in the new application, fixed there by `PDFSignatureHandler::releaseThreadResources()` called at the end of the worker).
- **Fix:** call `PDFSignatureHandler::releaseThreadResources()` at the end of every worker function, which runs OpenSSL, also in the old applications.

### 6. Print permission of low resolution

- **Where:** printing of the new application (`pdfappprint.cpp`) and of the old applications.
- **Problem:** a document, which allows printing only in low resolution (permission bit 12 clear), is printed with `QPrinter::ScreenResolution` only – vector content is still printed sharp. The old Editor ignores the bit completely.
- **Fix:** rasterize the pages at a low resolution (e.g. 150 dpi) for such documents.

### 7. Mark of the Web on Linux

- **Where:** attachments saved by the new application (`PDFAppAttachments`).
- **Problem:** on Windows a saved attachment takes the `Zone.Identifier` of its source PDF (O18); on Linux there is no equivalent yet (the `user.xdg.origin.url` extended attribute / GIO metadata used by browsers).
- **Fix:** copy the origin attributes of the source PDF where the file system supports them.
