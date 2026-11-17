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

## Print production (step 7b of phase 2)

`PDFTransparencyRenderer` is the renderer of Output Preview and Ink Coverage (`PDFOutputPreviewAnalyzer`, [pdfoutputpreviewanalyzer.h](../Pdf4QtLibCore/sources/pdfoutputpreviewanalyzer.h)); it measures the page as it is printed:

- **Overprint** (ISO 32000-2, 8.6.7, 11.7.4.3, 11.7.4.4, Table 146): `OP` / `op` apply to the elementary objects (fills, strokes, text, images, shadings), never to the compositing of a transparency group. The nonzero overprint mode (`OPM 1`) applies only when the source color space is DeviceCMYK and the blending space of the group is DeviceCMYK too, and never to images or shadings (`isNonZeroOverprintAllowed`). A page group in RGB is measured in the process inks of the output, not as RGB "inks". The colorant `/None` paints nothing, `/All` marks every ink, spot inks too. The spot inks are found by `PDFResourceWalk` (pages, forms, appearance streams, patterns, Type 3 fonts, soft masks), not only in the resources of the page.
- **Printed annotations** (Q7.8 a): after `processContents`, `processPrintedAnnotations` paints the normal appearance of every annotation with the flag *Print*, not *Hidden*, and not a reply (12.5.3, Table 167), its optional content by the activity of the renderer, placed by 12.5.5 with the initial graphics state of the page. An annotation without an appearance stream is not painted (a warning; `NOTES.txt`), NoZoom is ignored as for printing, NoRotate keeps it upright. The errors of the appearance streams are problems of the page: those of `reportRenderError` and those, which the parser adds itself (`getRenderErrors`).
- **Ratios** (Q7.6, R7-8): the ink inside of the TrimBox (else the CropBox, clipped by the MediaBox, 14.11.2) over the area of that box; the areas in mm² are of the whole page.

## Soft proof per tab

The soft proof is a state of one document tab (`PDFAppDocumentSoftProof`, Q7.10): it is applied to the color management of the view of that tab only, after its settings are read. Every other consumer takes the color management without it (`PDFCMSSettings::withoutProofing`, `PDFAppDocumentSoftProof::createCMSWithoutProofing`): printing, exports, thumbnails, the runs of tools (`PDFAppToolRunEnvironment::cms`), the page pictures of the tools (page stage, before / after overlay) and the pictures of Object Inspector. The canvas of the tab shows the proof also while another tool is open over it. Each `PDFLittleCMS` has its own LittleCMS context and creates its profiles and transforms by the `…THR` functions, so the alarm color of the gamut check of one tab never reaches another one (B200, R7-16).

## Widget layer

`PDFDrawSpaceController` lays the pages out in millimeters (zoom independent, block based), `PDFDrawWidgetProxy` maps that space to widget pixels, and `PDFAsynchronousPageCompiler` / `PDFAsynchronousTextLayoutCompiler` ([pdfcompiler.h](../Pdf4QtLibWidgets/sources/pdfcompiler.h)) compile pages on worker threads into a size-limited cache. Long operations honor `PDFOperationControl` for cancellation and report through `PDFProgress`. Thread fan-out is centralized in `PDFExecutionPolicy` ([pdfexecutionpolicy.h](../Pdf4QtLibCore/sources/pdfexecutionpolicy.h)), which leaves a thread to the canvas.
