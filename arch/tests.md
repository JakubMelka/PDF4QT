# Tests

QtTest executables, one per area, built into `<build>/usr/bin`. Run them by `ctest` from the build directory, a binary alone, or one function (`<exe> <function>`); capture the output with `-o result.txt,txt`. A new test is an `add_executable` + `add_test` block in [UnitTests/CMakeLists.txt](../UnitTests/CMakeLists.txt) with the precompiled header of its donor ([build.md](build.md)) and one row below (a `tst_apptool<part>test.cpp` is found by a `foreach` of that file). A test using `QRawFont` or any GUI type uses `QTEST_MAIN`, not `QTEST_APPLESS_MAIN`. What a test must check: [conventions.md](conventions.md#tests).

Facts needed to run them:

- Every test of a widget, a page or the application needs `QT_QPA_PLATFORM=offscreen` (ctest sets it).
- The App tests share the fixtures `appdocumentfixture.h` (memory database, real window, sample PDFs), `appsettingsfixture.h`, `apptoolsfixture.h`, `apptoolshellfixture.h`, `appmcpfixture.h`. A test of a computation records its progress by `testprogressrecorder.h` (a `PDFProgressCounter`, every call recorded under a lock).
- `PDF4QT_APP_SCREENSHOTS=<folder>` writes the screens of the App tests (both themes, 1x / 2x, 720 x 480, right-to-left) for a comparison with the Figma frames.
- Time limits scale by `PDF4QT_TEST_TIMEOUT_FACTOR` (20 under valgrind automatically).
- The lifetime sweeps (`*Lifetime`) record every signal and delete the tab, window or page from each emission; `PDF4QT_LIFETIME_TRACE=1` prints the sites, `PDF4QT_LIFETIME_SITE=<n>` replays one. They find bugs only under ASan / valgrind.
- `UnitTestsFluentLifetime`, `UnitTestsAppAnnotationLifetime`, `UnitTestsAppPagesLifetime` link `Qt6::CorePrivate`: built only with the private headers of QtCore.
- The big GUI executables are split by the runners into one process per `function:tag` ([checks.md](checks.md)).
- OCR tests read the models from `<build>/usr/bin/ocr` (`PDF4QT_OCR_DATA_DIRECTORY`, set by ctest); without Tesseract or the models they skip (`PDF4QT_OCR_SKIP`), which fails with `PDF4QT_OCR_REQUIRED=ON` (release gate).

| Target | File | Area |
| --- | --- | --- |
| `UnitTests` | [tst_lexicalanalyzertest.cpp](../UnitTests/tst_lexicalanalyzertest.cpp) | Lexer and parser; nesting deeper than `PDFParser::MAXIMUM_NESTING` is an error of the object, never a stack overflow. |
| `UnitTestsImageOptimizer` | [tst_imageoptimizertest.cpp](../UnitTests/tst_imageoptimizertest.cpp) | Image optimizer of the core. |
| `UnitTestsBitonalDocument` | [tst_bitonaldocumenttest.cpp](../UnitTests/tst_bitonaldocumenttest.cpp) | Bitonal conversion of documents. |
| `UnitTestsJBIG2` | [tst_jbig2test.cpp](../UnitTests/tst_jbig2test.cpp) | JBIG2 decoder. |
| `UnitTestsJBIG2Files` | [tst_jbig2filestest.cpp](../UnitTests/tst_jbig2filestest.cpp) | Every file of `jbig2test/` (not in the repository, skipped without it; `PDF4QT_JBIG2_TEST_DIRECTORY`) against its `.bmp`. |
| `UnitTestsCCITTFax` | [tst_ccittfaxtest.cpp](../UnitTests/tst_ccittfaxtest.cpp) | CCITT fax encoder / decoder. |
| `UnitTestsJBIG2Encoder` | [tst_jbig2encodertest.cpp](../UnitTests/tst_jbig2encodertest.cpp) | JBIG2 encoder, generic regions. |
| `UnitTestsJBIG2Segments` | [tst_jbig2segmentstest.cpp](../UnitTests/tst_jbig2segmentstest.cpp) | JBIG2 dictionaries, text, pattern and halftone regions built by test-side encoders. |
| `UnitTestsFormFieldFormat` | [tst_formfieldformattest.cpp](../UnitTests/tst_formfieldformattest.cpp) | Native Acrobat format / keystroke functions (AFNumber_*, AFDate_*, ...). |
| `UnitTestsFontEncoding` | [tst_fontencodingtest.cpp](../UnitTests/tst_fontencodingtest.cpp) | Font encodings, substitutes of ZapfDingbats / Symbol (a row skipped without its URW clone). |
| `UnitTestsAuthorSettings` | [tst_authorsettingstest.cpp](../UnitTests/tst_authorsettingstest.cpp) | Author settings. |
| `UnitTestsTextLayout` | [tst_textlayouttest.cpp](../UnitTests/tst_textlayouttest.cpp) | Text layout; a change of the algorithm is compared by `PDF4QT_TEXT_LAYOUT_BENCHMARK=<pdf>` + `..._DUMP=<file>` before / after, differences explained. |
| `UnitTestsContentEditor` | [tst_contenteditortest.cpp](../UnitTests/tst_contenteditortest.cpp) | Content editor, written numbers never `inf` / `nan` / an exponent. |
| `UnitTestsMeasure` | [tst_measuretest.cpp](../UnitTests/tst_measuretest.cpp) | Measurement. |
| `UnitTestsDimensions` | [tst_dimensionstest.cpp](../UnitTests/tst_dimensionstest.cpp) | Dimensions. |
| `UnitTestsSignatureBuilder` | [tst_signaturebuildertest.cpp](../UnitTests/tst_signaturebuildertest.cpp) | Document signer, incremental updates, signatures of restored (damaged) files. |
| `UnitTestsTimestamp` | [tst_timestamptest.cpp](../UnitTests/tst_timestamptest.cpp) | RFC 3161 timestamps against a loopback authority; a public one only with `PDF4QT_TIMESTAMP_TEST_URL`. |
| `UnitTestsPageContentEditorTools` | [tst_pagecontenteditortoolstest.cpp](../UnitTests/tst_pagecontenteditortoolstest.cpp) | Page content creation tools. |
| `UnitTestsAnnotationManipulator` | [tst_annotationmanipulatortest.cpp](../UnitTests/tst_annotationmanipulatortest.cpp) | Geometry editing of annotations; keeps its source at 100 % line / branch / MC/DC coverage. |
| `UnitTestsAnnotationSelection` | [tst_annotationselectiontest.cpp](../UnitTests/tst_annotationselectiontest.cpp) | Selection and editing in the annotation manager of the widgets. |
| `UnitTestsAnnotationIntegration` | [tst_annotationintegrationtest.cpp](../UnitTests/tst_annotationintegrationtest.cpp) | Annotation editing with the old application shell. |
| `UnitTestsAnnotationTools` | [tst_annotationtoolstest.cpp](../UnitTests/tst_annotationtoolstest.cpp) | Annotation tools and the opt-in engine switches of the new application; owner-deleted cases for ASan. |
| `UnitTestsFormFields` | [tst_formfieldstest.cpp](../UnitTests/tst_formfieldstest.cpp) | Form fields filled by the mouse, damaged field trees, tab order, XFA paging. |
| `UnitTestsOCR` | [tst_ocrtest.cpp](../UnitTests/tst_ocrtest.cpp) | OCR core; the benchmark only with `PDF4QT_OCR_BENCHMARK`. |
| `UnitTestsOCRDialog` | [tst_ocrdialogtest.cpp](../UnitTests/tst_ocrdialogtest.cpp) | OCR dialogs; user data redirected by `PDF4QT_OCR_USER_DIRECTORY`. |
| `UnitTestsScanPreparation` | [tst_scanpreparationtest.cpp](../UnitTests/tst_scanpreparationtest.cpp) | Crop / split / deskew of scans before OCR. |
| `PdfToolOcrSmoke` | [pdftoolocrsmoke.cmake](../UnitTests/pdftoolocrsmoke.cmake) | CMake script test of `PdfTool ocr` / `ocr-models`. |
| `PdfToolOptimizeSmoke` | [pdftooloptimizesmoke.cmake](../UnitTests/pdftooloptimizesmoke.cmake) | CMake script test of `PdfTool optimize` (= Compress of the core). |
| `UnitTestsDocumentWriter` | [tst_documentwritertest.cpp](../UnitTests/tst_documentwritertest.cpp) | Writer, permissive reading of damaged files; size benchmark only with `PDF4QT_WRITER_SIZE_FILES`. |
| `UnitTestsWidgetStyle` | [tst_widgetstyletest.cpp](../UnitTests/tst_widgetstyletest.cpp) | The Fluent style and its Qt workarounds; run `contextMenus` also on the native platform. |
| `UnitTestsFluentComponents` | [tst_fluentcomponentstest.cpp](../UnitTests/tst_fluentcomponentstest.cpp) | Components and delegates of Pdf4QtLibFluent against Figma. |
| `UnitTestsFluentLifetime` | [tst_fluentlifetimetest.cpp](../UnitTests/tst_fluentlifetimetest.cpp) | Every gallery widget destroyed in every order; one process per `function:tag` (~160), `-vs` refused. |
| `UnitTestsPDFObject` | [tst_pdfobjecttest.cpp](../UnitTests/tst_pdfobjecttest.cpp) | `PDFObject`, its strings and builders, sharing between threads. |
| `UnitTestsAppStorage` | [tst_appstoragetest.cpp](../UnitTests/tst_appstoragetest.cpp) | SQLite storage; the released schema is pinned by SHA-256 - never update the hash, add a migration. |
| `UnitTestsAppCore` | [tst_appcoretest.cpp](../UnitTests/tst_appcoretest.cpp) | Command line, commands, instance forwarding, log, locale, activation. |
| `UnitTestsAppNavigation` | [tst_appnavigationtest.cpp](../UnitTests/tst_appnavigationtest.cpp) | Window shell, routes, history, tabs; `nativeWindowsFrame` only with `-platform windows`. |
| `UnitTestsAppData` | [tst_appdatatest.cpp](../UnitTests/tst_appdatatest.cpp) | Repositories, thumbnailer, folder information, more processes. |
| `UnitTestsAppInfoPages` | [tst_appinfopagestest.cpp](../UnitTests/tst_appinfopagestest.cpp) | Pages About, Support, Sponsor. |
| `UnitTestsAppContent` | [tst_appcontenttest.cpp](../UnitTests/tst_appcontenttest.cpp) | Markdown, What's New against RELEASES.txt, update checker, user manual. |
| `UnitTestsAppSearch` | [tst_appsearchtest.cpp](../UnitTests/tst_appsearchtest.cpp) | Global search, its page and popup. |
| `UnitTestsAppHome` | [tst_apphometest.cpp](../UnitTests/tst_apphometest.cpp) | Pages Home, Open, Recent. |
| `UnitTestsAppDocument` | [tst_appdocumenttest.cpp](../UnitTests/tst_appdocumenttest.cpp) | Document tab: loading, password, undo, lock, close / quit questions. |
| `UnitTestsAppDocumentView` | [tst_appdocumentviewtest.cpp](../UnitTests/tst_appdocumentviewtest.cpp) | Document view, its toolbar and panels. |
| `UnitTestsAppDocumentPanels` | [tst_appdocumentpanelstest.cpp](../UnitTests/tst_appdocumentpanelstest.cpp) | Sidebar panels. |
| `UnitTestsDocumentTextIndex` | [tst_documenttextindextest.cpp](../UnitTests/tst_documenttextindextest.cpp) | Text index and search of a document. |
| `UnitTestsAppDocumentSearch` | [tst_appdocumentsearchtest.cpp](../UnitTests/tst_appdocumentsearchtest.cpp) | Find, signature verification, read aloud. |
| `UnitTestsAppDocumentFile` | [tst_appdocumentfiletest.cpp](../UnitTests/tst_appdocumentfiletest.cpp) | Save, automatic refresh, printing, e-mail. |
| `UnitTestsAppDocumentProperties` | [tst_appdocumentpropertiestest.cpp](../UnitTests/tst_appdocumentpropertiestest.cpp) | Document properties, link actions and their security rules. |
| `UnitTestsAppAnnotate` | [tst_appannotatetest.cpp](../UnitTests/tst_appannotatetest.cpp) | Creation of annotations. |
| `UnitTestsAppAnnotationEdit` | [tst_appannotationedittest.cpp](../UnitTests/tst_appannotationedittest.cpp) | Editing of annotations, the annotation controller. |
| `UnitTestsAppNotesOutline` | [tst_appnotesoutlinetest.cpp](../UnitTests/tst_appnotesoutlinetest.cpp) | Notes and outline editing. |
| `UnitTestsAppForms` | [tst_appformstest.cpp](../UnitTests/tst_appformstest.cpp) | Form filling. |
| `UnitTestsAppSettings` | [tst_appsettingstest.cpp](../UnitTests/tst_appsettingstest.cpp) | Settings page and General; the language switch needs the `.qm` of `release_translations`. |
| `UnitTestsAppSettingsRendering` | [tst_appsettingsrenderingtest.cpp](../UnitTests/tst_appsettingsrenderingtest.cpp) | Settings Viewing, Color modes, Color management, Advanced. |
| `UnitTestsAppSettingsEditing` | [tst_appsettingseditingtest.cpp](../UnitTests/tst_appsettingseditingtest.cpp) | Settings Editing, Security, Speech. |
| `UnitTestsAppSettingsShortcuts` | [tst_appsettingsshortcutstest.cpp](../UnitTests/tst_appsettingsshortcutstest.cpp) | Settings Shortcuts and Storage. |
| `UnitTestsAppSettingsLifetime` | [tst_appsettingslifetimetest.cpp](../UnitTests/tst_appsettingslifetimetest.cpp) | Lifetime sweep of Settings. |
| `UnitTestsAppTools` | [tst_apptoolstest.cpp](../UnitTests/tst_apptoolstest.cpp) | Tools catalog page and the routes of the tools. |
| `UnitTestsAppToolCollections` | [tst_apptoolcollectionstest.cpp](../UnitTests/tst_apptoolcollectionstest.cpp) | Collections editor. |
| `UnitTestsAppToolsPanel` | [tst_apptoolspaneltest.cpp](../UnitTests/tst_apptoolspaneltest.cpp) | Tools panel of the document toolbar. |
| `UnitTestsAppToolView` | [tst_apptoolviewtest.cpp](../UnitTests/tst_apptoolviewtest.cpp) | Tool mode of the document view. |
| `UnitTestsAppToolsLifetime` | [tst_apptoolslifetimetest.cpp](../UnitTests/tst_apptoolslifetimetest.cpp) | Lifetime sweep of the Tools pages and panel. |
| `UnitTestsDocumentTool` | [tst_documenttooltest.cpp](../UnitTests/tst_documenttooltest.cpp) | Core of the tools: catalog, parameters, schema, registry, suggestions, the test tool. |
| `UnitTestsDocumentToolCompress` | [tst_documenttoolcompresstest.cpp](../UnitTests/tst_documenttoolcompresstest.cpp) | Compress; `memoryBound` is skipped under the sanitizers. |
| `UnitTestsDocumentToolPages` | [tst_documenttoolpagestest.cpp](../UnitTests/tst_documenttoolpagestest.cpp) | Rotate Pages, Remove Pages. |
| `UnitTestsDocumentToolProtect` | [tst_documenttoolprotecttest.cpp](../UnitTests/tst_documenttoolprotecttest.cpp) | Remove Hidden Data, Remove External Links. |
| `UnitTestsDocumentToolStructure` | [tst_documenttoolstructuretest.cpp](../UnitTests/tst_documenttoolstructuretest.cpp) | Optimize Structure. |
| `UnitTestsDocumentToolExtractText` | [tst_documenttoolextracttexttest.cpp](../UnitTests/tst_documenttoolextracttexttest.cpp) | Extract Text and the exporter of PdfTool fetch-text. |
| `UnitTestsObjectWalk` | [tst_objectwalktest.cpp](../UnitTests/tst_objectwalktest.cpp) | `PDFObjectWalk` and the walks over the object graph: cycles and shared nodes. |
| `UnitTestsDocumentToolCorpus` | [tst_documenttoolcorpustest.cpp](../UnitTests/tst_documenttoolcorpustest.cpp) | Every tool over `testpdf` (never in the repository); only with `PDF4QT_TOOL_CORPUS_DIRECTORY` / `_RUN=1`, a filter - the whole corpus on the owner's request. |
| `UnitTestsAppAnnotationLifetime` | [tst_appannotationlifetimetest.cpp](../UnitTests/tst_appannotationlifetimetest.cpp) | Lifetime sweep of the annotation scenarios. |
| `UnitTestsMcp` | [tst_mcptest.cpp](../UnitTests/tst_mcptest.cpp) | Pdf4QtLibMcp: JSON, HTTP, both protocol eras, the conformance table. |
| `UnitTestsAppToolShell` | [tst_apptoolshelltest.cpp](../UnitTests/tst_apptoolshelltest.cpp) | Shared shell of the tools on the test tool. |
| `UnitTestsAppToolCompress` | [tst_apptoolcompresstest.cpp](../UnitTests/tst_apptoolcompresstest.cpp) | Compress in the application; benchmark only with `PDF4QT_COMPRESS_BENCHMARK`. |
| `UnitTestsAppToolPages` | [tst_apptoolpagestest.cpp](../UnitTests/tst_apptoolpagestest.cpp) | Rotate Pages, Remove Pages in the application. |
| `UnitTestsAppToolProtect` | [tst_apptoolprotecttest.cpp](../UnitTests/tst_apptoolprotecttest.cpp) | Remove Hidden Data, Remove External Links in the application. |
| `UnitTestsAppToolStructure` | [tst_apptoolstructuretest.cpp](../UnitTests/tst_apptoolstructuretest.cpp) | Optimize Structure in the application. |
| `UnitTestsAppToolExtractText` | [tst_apptoolextracttexttest.cpp](../UnitTests/tst_apptoolextracttexttest.cpp) | Extract Text in the application. |
| `UnitTestsAppToolPagesLifetime` | [tst_apptoolpageslifetimetest.cpp](../UnitTests/tst_apptoolpageslifetimetest.cpp) | Lifetime sweep of the tools in a document tab. |
| `UnitTestsAppMcp` | [tst_appmcptest.cpp](../UnitTests/tst_appmcptest.cpp) | MCP in the application: contract, permissions, read tools, catalog bridge. |
| `UnitTestsAppMcpEdit` | [tst_appmcpedittest.cpp](../UnitTests/tst_appmcpedittest.cpp) | MCP edit tools and their confirmations. |
| `UnitTestsAppAi` | [tst_appaitest.cpp](../UnitTests/tst_appaitest.cpp) | The page AI. |
| `UnitTestsAppMcpContent` | [tst_appmcpcontenttest.cpp](../UnitTests/tst_appmcpcontenttest.cpp) | MCP docs, skills, workflows against the contract; `PDF4QT_MCP_DOCS_DUMP=<folder>` writes the generated references. |
| `UnitTestsAppMcpLifetime` | [tst_appmcplifetimetest.cpp](../UnitTests/tst_appmcplifetimetest.cpp) | Lifetime sweep of the MCP scenarios. |
| `UnitTestsAppPagesLifetime` | [tst_apppageslifetimetest.cpp](../UnitTests/tst_apppageslifetimetest.cpp) | Lifetime of every route of the navigator. |
| `UnitTestsAppXfa` | [tst_appxfatest.cpp](../UnitTests/tst_appxfatest.cpp) | Dynamic XFA forms shown by their layout pages. |
| `UnitTestsAppMultiInstance` | [tst_appmultiinstancetest.cpp](../UnitTests/tst_appmultiinstancetest.cpp) | Real instances on one data folder; run with few jobs (`--jobs 4`). |
| `UnitTestsAnnotationProperties` | [tst_annotationpropertiestest.cpp](../UnitTests/tst_annotationpropertiestest.cpp) | Annotation property editor of the core. |
