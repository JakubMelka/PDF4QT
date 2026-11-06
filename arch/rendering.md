# Rendering

`PDFPageContentProcessor` ([pdfpagecontentprocessor.h](../Pdf4QtLibCore/sources/pdfpagecontentprocessor.h)) is the single interpreter of page content streams: it decodes the operators, keeps the PDF graphic state and calls virtual `performXxx` hooks. Everything that consumes page content derives from it:

- `PDFPainter` - draws directly onto a `QPainter` (basic transparency only).
- `PDFPrecompiledPageGenerator` → `PDFPrecompiledPage` - a replayable instruction list; the viewer caches it and plays it for fast repaints.
- `PDFTransparencyRenderer` - full transparency groups, blend modes and spot colors, rasterized in software.
- `PDFBLPainter` - a Blend2D `QPaintEngine`, an alternative backend.
- Non-drawing consumers: text layout, the page content editor, the JavaScript scanner, image extraction.

A new operator or graphic feature goes into the processor first, then into each painter that needs it.

## Caches

Data derived from a document are neither held by `PDFDocument` nor global: they are separate objects created where the pages are processed and passed to the processors - `PDFFontCache` and `PDFICCProfileCache` (decoded ICC profiles; also passed to the creation of color spaces and patterns, may be `nullptr`). Each belongs to one version of the document: call `setDocument` for each new version.

A content processor does not own the realized fonts it used (a font removed from the font cache must be released): a font is held only while a text is drawn. The painter paths of glyphs are stored under the identifier of the outline (`PDFGlyphOutline::getId`, never reused), not under its address. Glyphs shared by threads are plain immutable `PDFGlyphOutline`; each processor creates its own `QPainterPath` from them, because the const functions of `QPainterPath` write into its data (and before Qt 6.10 copies share them).

## Widget layer

`PDFDrawSpaceController` lays the pages out in millimeters (zoom independent, block based), `PDFDrawWidgetProxy` maps that space to widget pixels, and `PDFAsynchronousPageCompiler` / `PDFAsynchronousTextLayoutCompiler` ([pdfcompiler.h](../Pdf4QtLibWidgets/sources/pdfcompiler.h)) compile pages on worker threads into a size-limited cache. Long operations honor `PDFOperationControl` for cancellation and report through `PDFProgress`. Thread fan-out is centralized in `PDFExecutionPolicy` ([pdfexecutionpolicy.h](../Pdf4QtLibCore/sources/pdfexecutionpolicy.h)), which leaves a thread to the canvas.
