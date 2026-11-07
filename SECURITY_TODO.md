# Security backlog

Security weaknesses found during the work on the GUI overhaul (new application `PDF4QT`), which lie outside its scope - mostly in `Pdf4QtLibCore` and therefore also in the old applications (Editor, Viewer, PageMaster, Diff, PdfTool). They are collected here and solved together in a separate task, not in the redesign (owner decision, 2026-10-24).

Each entry: where, the attack, the proposed fix. An entry is never removed, also when it is fixed (owner 2026-11-06: a security check of documents will be built on them): a fixed entry gets a line `- **Status:** fixed ... (who, date, tests)`; the commit message names it.

## Open

### 1. Decompression bombs – no limit of the decoded size of a stream

- **Where:** `PDFFlateDecodeFilter::uncompress` and the other decoding filters (`pdfstreamfilters.cpp`), `PDFDocument::getDecodedStream` and every caller.
- **Attack:** a small hostile PDF contains a stream, which expands to tens of gigabytes (nested Flate filters, a long run of zeros). The application allocates until `std::bad_alloc` – in a worker thread it ends the process, in the GUI thread the application crashes. Reached by opening the document (page content, images, XMP metadata, fonts, embedded files, form appearances), also by the thumbnailer of the new application.
- **Today:** the new application catches `std::bad_alloc` around its workers and readers of documents - the reading and the automatic refresh, saving, printing, the text index and the search, the verification of the signatures, attachments and the jobs of MCP (`pdfappdocumentloader`, `pdfappautorefresh`, `pdfappdocumentsave`, `pdfappprint`, `pdfappdocumentsearchindex`, `pdfappdocumentsearchcontroller`, `pdfappdocumentsignatures`, `pdfappdocumentpanels_attachments`, `pdfappmcpjob`) and the thumbnailer catches every `std::exception` - the operation fails with an error instead of ending the process. A bomb still allocates up to the failure (memory pressure of the whole system), the XMP reader of the properties page catches only `PDFException`, and the old applications (Editor, Viewer, PageMaster, Diff, PdfTool) catch nothing. The item stays open for the limit in Core.
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

### 8. Outline parsing – recursion and quadratic copies of the visited set

- **Where:** `PDFOutlineItem::parseImpl` (`pdfoutline.cpp`), called when every document is opened (`PDFCatalog::parse`).
- **Attack:** a deep chain of `/First` items recurses once per level (a stack overflow of the reading thread for tens of thousands of levels); each level copies the whole set of the visited items (`currentVisitedOutlineItems`), so a wide outline with children costs O(N²) time and memory; shared subtrees under several items are parsed again for each of them.
- **Today:** the outline editing of the new application (step 4, package C) reads the outline itself without recursion, refuses outlines deeper than 256 levels or larger than 100 000 items and outlines with shared items; the parser of Core is unchanged.
- **Fix:** an iterative parser with one visited set and limits of the depth and of the number of items (the same as `PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH` / `MAXIMUM_OUTLINE_ITEMS`), by `PDFObjectWalk` (`pdfobjectutils.h`), as the outline walk of Remove Pages.

### 9. Annotations pasted or dropped from another process

- **Where:** `PDFAnnotationManipulator::deserializeAnnotations` / `insertAnnotations` (`pdfannotationmanipulator.cpp`), called by `PDFWidgetAnnotationManager::pasteAnnotations` and `handleAnnotationDrop` (Paste, Paste in Place and drag and drop of the new application, step 4, and of the old Editor).
- **Attack:** the clipboard / drag data of the MIME type `application/x-pdf4qt-annotation` is written by any process. It is read as a whole PDF in the GUI thread without a size limit (a file of hundreds of megabytes or a decompression bomb freezes or ends the application; also items 1, 2, 4), and `importAnnotations` copies every object the pasted annotations reference: link and widget actions (Launch, JavaScript, SubmitForm, GoToR), file attachments with embedded files, huge appearance streams. The user sees only "Paste".
- **Today:** the actions of a pasted link go through the confirmations of the new application when clicked (step 3, package F); nothing limits the size or the content of the pasted data.
- **Fix:** a size limit of the MIME data (e.g. 64 MB) before the reader, the reader with the limits of items 1 and 2, and an import filter dropping the actions other than URI / GoTo and the JavaScript of the pasted annotations (or a confirmation, which names them).

### 10. Folder of ICC profiles read without limits in the calling thread

- **Where:** `PDFCMSManager::getExternalColorProfiles` / `getExternalProfilesImpl` (`pdfcms.cpp`), reached by `getOutputProfiles` / `getGrayProfiles` / `getRGBProfiles` / `getCMYKProfiles` and `getDefaultSettings` after every `setSettings` (the cache is cleared) - in the GUI thread of a document view (new application) and of the settings dialog (old applications).
- **Attack:** the folder of `PDFCMSSettings::profileDirectory` (a setting, also of a database of another instance or a copied profile) is listed completely (`QDir::entryList`) and every `*.icc` file is read whole (`readAll`) without a limit of the count or of the size: a folder of thousands of files, of huge files or a network folder, whose server does not answer, freezes the user interface; damaged profiles go to `cmsOpenProfileFromMem`. On Windows `getInstalledWindowsColorProfiles` also opens every installed profile in the same thread.
- **Today:** the new application gives the manager only a folder, which passed `PDFAppColorProfiles::checkDirectory` (step 5, package B: a local folder of at most 1000 entries and 64 profiles of at most 8 MB, 64 MB together, no network path), and its Settings page reads the lists in a worker thread; the old applications pass any folder.
- **Fix:** limits of the count and of the size of the profiles in Core, the external lists read once in a worker (not after every change of the settings), a network folder never read implicitly.

### 11. Translation files of the old applications loaded by the name search of QTranslator

- **Where:** `PDFApplicationTranslator::installTranslator` (`pdfapplicationtranslator.cpp`, Core; Editor, Viewer, PageMaster, Diff, PdfTool).
- **Attack:** `QTranslator::load(QLocale, "PDF4QT", "_", path)` and `load(fileName, path)` try many names (`<name>.qm` before `<name>`, then shorter names cut at `_` / `.`), so another file of the folder than the expected one may be loaded, without a size limit. A `.qm` file with a block of dependencies (tag 0x96) makes Qt load every file it names - a relative name from the folder, an absolute one from anywhere, also a UNC path of another computer (credentials of the user sent to it). Needs write access to the folder of the translations (installation folder, a portable copy).
- **Today:** the new application (`PDFAppTranslator`, step 5) loads only the exact file of a fixed language id (`load(path, QString(), "", "")`), refuses files larger than 64 MB and files with a block of dependencies (review RS5).
- **Fix:** the same in the old translator; report the unbounded dependency loading of a crafted `.qm` to Qt.

### 12. Page ranges parsed without bounds

- **Where:** `PDFClosedIntervalSet::parse` (`pdfutils.cpp`), used by PdfTool (`--page-first` / page ranges of many commands), the old Editor / PageMaster dialogs and the range field of the old applications.
- **Attack:** the documentation says the numbers must lie between `first` and `last`, but the function never checks it: "0" and "9223372036854775807" pass; `getTotalLength` / `unfold` of such a set overflow `PDFInteger` (undefined behavior) or allocate billions of values; "-5-" is read as the whole range. A hostile command line or a range typed into a field (MCP of phase 2 passes page ranges of an assistant) reaches it.
- **Today:** the tools of the new application (`PDFDocumentToolPageSelection`, step 6 package D) check the characters, at most one '-' per part and 10 digits per number before they call it, and check the result against the pages of the document.
- **Also (review RS6):** `getTotalLength` sums by `std::accumulate(..., 0, ...)` - the accumulator is an `int`, so a set longer than 2^31 values has a wrong (truncated) length, and `unfold` allocates that length but writes every value (heap overflow: "1-4294967302" has the length 6, unfold writes 4294967302 values - reproduced as a crash). Callers comparing lengths (`pdfdocumentmanipulator.cpp`, `pdfsignaturehandler.cpp`) get wrong answers for such sets. Step 6 code compares the sets instead (`PDFDocumentToolParameterHelper::parsePages`).
- **Fix:** the bounds check of the documentation inside of `parse` (an error for a number outside of `first` .. `last`, at most one '-' per part), `PDFInteger(0)` as the initial value of `getTotalLength` with a saturated sum, `unfold` refusing a set longer than a limit, tests with the hostile ranges of `UnitTestsDocumentTool::pageSelection`.

### 13. MCP server: no TLS in the remote mode

- **Where:** `Pdf4QtLibMcp` (`PDFMcpListener`, `PDFMcpConnection`), the remote mode of the AI page (step 7, plan chapter 7).
- **Attack:** with "Allow connections from other computers" the requests, the results (document text, rendered pages) and the access token travel as plain HTTP; anyone on the network path reads them and can replay the token.
- **Today:** the AI page shows the warning row and the remote note; the token is always required in the remote mode.
- **Fix:** `QSslServer` with a certificate of the user (or a generated self-signed one with its fingerprint shown for pinning), the token only over TLS.

### 14. `PDFJsonHelper` used for untrusted input

- **Where:** `PDFJsonHelper` (`pdfjsonhelper.h`, Core) and its callers outside of the MCP server.
- **Problem:** the helper is tolerant by design (wrong types become defaults, numbers are read through doubles); the MCP server reads untrusted JSON only by the strict `PDFMcpJsonReader` (plan 2.2), but other places may parse files or messages of other processes by the tolerant helper.
- **Fix:** an audit of the callers; untrusted input by a strict reader (types, ranges, lengths, exact integers), the tolerant helper only for own outputs.

### 15. MCP Host allowlist without network change notifications

- **Where:** `PDFMcpDispatcher::refreshAllowedHosts` (remote mode).
- **Problem:** the allowed `Host` values of the remote mode are the addresses of the interfaces at the start; `QNetworkInformation` is unavailable on some Linux desktops, so a new address is learned only on a miss (refreshed at most once per second). A request to a new address arriving within that second is refused (403), and an address removed from the computer stays allowed until the next refresh.
- **Fix:** refresh on `QNetworkInformation` changes where the backend exists, and on a timer otherwise.

### 16. Clipboard history of copied MCP tokens

- **Where:** the AI page (*Copy* of the access token, *Copy Command* / *Copy Configuration* with the token).
- **Problem:** Windows clipboard history (Win+V), cloud clipboard sync and clipboard managers of Linux desktops keep the copied token after the user pasted it.
- **Fix:** mark the copied data as excluded from the history (`ExcludeClipboardContentFromMonitorProcessing` / `CanIncludeInClipboardHistory` = 0 on Windows, `x-kde-passwordManagerHint` on KDE), clear the clipboard after a while if it still holds the token.

### 17. Shell history of MCP setup commands

- **Where:** the setup cards of the AI page (`claude mcp add ... --header "Authorization: Bearer <token>"`, `gemini mcp add ...`).
- **Problem:** a command pasted into a shell with the token stays in the shell history file (bash, PowerShell PSReadLine) readable by other programs of the user.
- **Fix:** commands, which read the token from an environment variable or a file, or a note to prefix the command by a space (bash `HISTCONTROL=ignorespace`); the help topic already warns (plan 4.5).

### 18. Unbounded occurrences of the XFA layout engine

- **Where:** `PDFXFALayoutEngine` (`pdfxfaengine.cpp`, `getOccurenceCount` and the loops of `visit(XFA_subform)` / `visit(XFA_subformSet)`), run by `PDFFormManager::setDocument` (GUI thread of every application) and by `PDFXFAPaging` (worker of the reading of PDF4QT).
- **Problem:** the `occur` element of a subform of a hostile template gives any repetition count (`initial="1000000000"`), nested subforms multiply it; the layout repeats the content that many times (time and memory). The number of pages is bounded (`PDFXFAEngine::MAXIMUM_PAGE_COUNT` = 128, a layout behind the last page is dropped since step 8), the work of the repetitions is not.
- **Fix:** a budget of laid out items for the whole layout (an occurrence count bounded by the remaining budget), the layout stopped when it is spent.

### 19. Content processor: forms and patterns repeated without a limit of the work

- **Where:** `PDFPageContentProcessor` (`Pdf4QtLibCore/sources/pdfpagecontentprocessor.cpp`), all renderers and the image optimizer.
- **Attack:** a chain of form XObjects, where each form draws the next one twice (no cycle), makes 2^32 executions within the depth limit from a small file: the page never ends. Cycles of tiling patterns and of Type 3 fonts were not checked, nor a soft mask of a graphic state whose group uses that soft mask again (`PDFTransparencyRenderer` draws it by a new processor with an empty stack of the forms).
- **Today:** a form drawing itself (also through other forms) and a nesting deeper than `MAXIMUM_FORM_NESTING` = 32 are skipped and reported once per page as a rendering error (the rest of the page is drawn) - found by the hostile test of the tool Compress, where they overflowed the stack, in every application. A run with a `PDFOperationControl` can be stopped.
- **Fix:** a limit of the executed forms of one page; the same guards for the patterns, the Type 3 fonts and the soft masks.

### 20. Image masks referencing themselves

- **Where:** `PDFImage::createImage` (`Pdf4QtLibCore/sources/pdfimage.cpp`), the entry `/Mask` of a stream; every renderer, the image optimizer and every other reader of the images.
- **Attack:** a stencil mask, whose `/Mask` refers to itself (or a longer cycle): `createImage` reads the mask by calling itself without a limit, the stack overflows and the application ends (reproduced under ASan by a page drawing such an image). `/SMask` is safe - a soft mask with `/Mask` or `/SMask` is refused.
- **Fix:** refuse `/Mask` and `/SMask` in the image read as a mask of another image (as the check of `isSoftMask` does).

### 21. A compiled page without a limit of its memory, std::bad_alloc out of the compilation

- **Where:** `PDFRenderer::compile` and `PDFPrecompiledPageGenerator` (`Pdf4QtLibCore/sources/pdfrenderer.cpp`, `pdfpainter.cpp`), every viewer of a page; found by `UnitTestsDocumentToolCorpus` on `testpdf/Bugs/runsforever-poppler.pdf` (9.7 MB, 1 page).
- **Problem:** nested forms repeat their content (`processForm` → `processContent` → `operatorPathStroke` → `PDFPrecompiledPageGenerator::performPathPainting`, entry 19 is the work, this is the memory): every path is one more instruction of the compiled page. The compilation of the page held 1.9 GB after 55 s; on Windows `std::bad_alloc` ended it after 409 s. `compile` says that no exception is thrown and catches nothing of this kind. The end of the application by the exception in the worker of `PDFAsynchronousPageCompiler` was reproduced by a limit of the memory of the process (review RP2FIX2, 2026-11-05). The owner does not want a `catch` of `std::bad_alloc` there (2026-11-05: when the allocator runs out, the application ends anywhere), so it stays.
- **Fix:** what stays: a limit of the instructions / memory of one compiled page, with the page reported as a rendering error.

### 22. Drawing of a compiled mesh without a limit and without the cancellation

- **Where:** `PDFPrecompiledPage::draw` → `PDFMesh::paint` (`Pdf4QtLibCore/sources/pdfpainter.cpp`, `pdfpattern.cpp`); found by `UnitTestsDocumentToolCorpus` on `testpdf/Bugs/SLOW_PERF_BTN_TRX_BigData_0513pdf(5).pdf` (5 MB, 5 pages).
- **Problem:** the triangles of the meshes of the shadings are drawn one by one by `QPainter` (`QRasterPaintEngine::fillPolygon`), `PDFPrecompiledPage::draw` has no `PDFOperationControl` and no limit of the triangles. Page 1 is not drawn into a picture of 600 px within 10 minutes (stack sampled at 75 s, Linux; the limit of the row ended it on Windows at 600 s, 3 of 3 runs). A viewer draws a compiled page in the thread of the user interface, so the window may not answer for that time (read from the code, not reproduced in the application).
- **Fix:** the operation control checked in `draw` and in `PDFMesh::paint`; a mesh with more triangles than pixels of its target drawn by a cheaper way (a picture of the mesh cached per zoom), a limit of the triangles reported as a rendering error.

### 23. The fingerprint of an OCR page walks shared objects again for each path

- **Where:** `PDFOCRPagePreparerHelper::digestObject` (`Pdf4QtLibCore/sources/pdfocrpagepreparer.cpp`); seen on `testpdf/Bugs/SLOW_LOADING_OMAP4460_ES1.x_PUBLIC_TRM_vN.pdf` (27 MB: the first 60 pages take more than 150 s, also `error6.pdf`, `CUDA_C_Programming_Guide.pdf`, `ISO_DIS_32000-2-DIS4.pdf`).
- **Problem:** `visited` holds only the path from the root (a reference is erased after its digest) and no digest is remembered, so an object shared by more paths is digested once per path, its streams hashed again. Read from the code, not run: 64 levels (`MaximumDepth`) of arrays with two references to the next level are about 2^64 visits, a hang of the OCR analysis of a page.
- **Fix:** a digest remembered per reference for one fingerprint (the same document, the same skip rules), at most one hash of a stream per fingerprint; a cycle stays `R` by the path, so the remembered digest of an object inside of a cycle needs care.

### 24. Reading of a damaged file depends on the order of the threads

- **Where:** `PDFDocumentReader::restoreObjects` of the permissive reading (`Pdf4QtLibCore/sources/pdfdocumentreader.cpp`, the parallel `processOffsetEntry`); seen on `testpdf/Signature/invalid/isa.pdf`, `isa-1.pdf`, `isa-2.pdf`, `siwa-2.pdf` (attacks on signatures by incremental saving), found when two builds of the reader were compared.
- **Problem:** the first thread to finish an object of a number wins, and a file with more objects of one number gives other objects from run to run in the same build; `isa.pdf` read by 8 processes at once opened with another content or failed in 4 of 18 runs, `isa-1.pdf` / `isa-2.pdf` gave 2 and 3 variants in 30 reads of one process. It is not only a file made to confuse the reader: every incremental update has more objects of one number. A hand-made file of two revisions of 200 strings with a damaged `startxref` read 100 times shows the old revision 99 times and a mix of 171 old and 29 new values once, so the last saved changes are silently not shown. The reader of the applications is permissive, so the user meets it by opening such a file: other annotations and values at each opening, the automatic refresh, the thumbnail and an MCP read may each see another version, a tab with a signature shows the verdict over the bytes of the file next to values of another revision.
- **Fix:** the object chosen by its offset (the last object of a number in the file wins, the order of the updates), independent of the threads.
- **Status:** fixed in phase 2 by P2READER (2026-11-04, commit 41b424c8): the object of a number is chosen by its offset, independent of the threads. The item stays for the security check of documents.

### 25. The prefix `\??\` and a cleaned path pass the other checks of network paths

- **Where:** `PDFAppNetworkPaths::isNetworkPathByName` and `PDFRepositoryHelper::isDevicePath`; used by `pdfappcolorprofiles.cpp:118`, `pdfappsettingcontrol.cpp:1023`, `pdfappsettingssection_storage.cpp:586`, `pdfapphomebasepage.cpp:687` and the links `pdfappdocumentactions.cpp:1019` (a PDF link with a `..` in front of `\??\UNC\`). Read from the code, not run; only `PDFAppToolPathRules::isDeviceOrNetworkPath` of the tools and of MCP knows them (fixed 2026-11-03).
- **Problem:** Qt removes the prefix when it opens a path, and `QDir::cleanPath` makes `/x/..//host/share` a UNC path. A stored folder (an imported backup, a hostile row) or a link `\??\UNC\host\share` reads as local, then `QFileInfo::exists` contacts that computer with the credentials of the user.
- **Fix:** the rule of `isDeviceOrNetworkPath` in one common function (a raw `\??\`, then the cleaned path), used by every place above.

### 26. Object number of a damaged file allocates the storage of all numbers below it

- **Where:** `PDFDocumentReader::createRestoredObjects` (`Pdf4QtLibCore/sources/pdfdocumentreader.cpp`, `objects.resize(...)`; the same line was in `readDamagedDocumentFromBuffer` before item 24 was fixed); found 2026-11-04.
- **Problem:** the permissive reading takes the object number from the bytes without a limit. A file of 300 bytes with `999999999999 0 obj 1 endobj` and a wrong `startxref` asks for terabytes: run under ASan it ends by out-of-memory in this line (`build/work/P2READER`); from the code, in a normal build `std::bad_alloc` leaves the reader, which catches only `PDFException` (not run). A number of 100 million takes gigabytes without any failure.
- **Fix:** an object number above the limit of ISO 32000-2 Annex C or above the number of bytes of the file skipped by `restoreObjects`, as a damaged object; the same check for the table of the strict reader.

### 28. A file over 2 GB ends PdfTool and the old applications

- **Where:** `PDFDocumentReader` (`Pdf4QtLibCore/sources/pdfdocumentreader.cpp`): `findFromEnd` (`const int size = byteArray.size()`), the offsets of `PDFDamagedFileScanner` and `findObjectByteOffsets` are `int`; found 2026-11-05.
- **Problem:** a buffer of 2200 MB gives a negative size, the search starts in front of the buffer: SEGV under ASan in `readFromBuffer` (`build/work/RP2READER`). The new application is safe by its limit of 2047 MB (`PDFAppDocumentLoader::MAXIMUM_FILE_SIZE`, also the automatic refresh), `readFromFile` of PdfTool and of the old applications has no limit.
- **Fix:** `readFromBuffer` refuses a buffer larger than `INT_MAX` with an error, as the loader does.

### 27. The text layout of the whole document ends the application without memory

- **Where:** `PDFAsynchronousTextLayoutCompiler::makeTextLayout` and `onTextLayoutCreated` (`Pdf4QtLibWidgets/sources/pdfcompiler.cpp`, `m_textLayoutCompileFuture.result()`), the old applications and the text selection of the new one; found by the review RP2FIX2, read from the code, not reproduced.
- **Problem:** `std::bad_alloc` in the worker of the layout is kept by the future as `QUnhandledException`, `result()` throws it in the slot of the user interface thread, nothing catches it: `std::terminate`. A heavy tool holding the memory while the user selects a text is enough. The same worker sets `result.setTextLayout` for no page left without the memory, `m_isRunning` would stay set.
- **Fix:** none planned: the owner does not want a `catch` of `std::bad_alloc` (2026-11-05: when the allocator runs out, the application ends anywhere). Kept as a record of the behavior.

### 29. A name tree, which is its own kid, ends the reading of the document

- **Where:** `PDFNameTreeLoader::parseImpl` (`pdfnametreeloader.h`), every name tree of the catalog (`/Names /Dests`, `/AP`, `/JavaScript`, `/Pages`, `/Templates`, `/IDS`, `/URLS`, `/EmbeddedFiles`); found by the test of Remove Pages of package 2A (`removeHostile`), reproduced: the process ends with an access violation (stack overflow) while the document is read.
- **Attack:** a node of a name tree lists itself (or an ancestor) in `/Kids`: the recursion never ends. A small hostile file ends every application, which opens it, also the thumbnailer of the new application.
- **Fix:** the set of the visited nodes and a depth limit as in `PDFObjectUtils::readNumberTree` (the walk of `PDFDocumentPageRemoval::readNameTree` of Remove Pages is bounded already).
- **Status:** fixed in phase 2 by P2WALK (2026-11-06): `PDFNameTreeLoader` and `PDFNumberTreeLoader` walk by `PDFObjectWalk` (one visited set, a depth limit); tests `UnitTestsObjectWalk` (`nameTree`, `pageLabelsTree`). The item stays for the security check of documents.

### 30. The removal of an annotation follows /Popup to any object

- **Where:** `PDFDocumentBuilder::removeAnnotation` (`pdfdocumentbuilder.cpp`): the delete of annotations of the old Editor and of the annotation operations of the new application, and the old commands of Remove External Links (`PDFDocumentLinkOperations::removeExternalLinkAnnotations`, PdfTool `remove-external-links`, the Editor); found by package 2B of phase 2, read from the code.
- **Attack:** an annotation of a hostile file has `/Popup` pointing at the catalog or at a page. The removal of the annotation replaces the object of `/Popup` by a null object without looking at it: the document loses its catalog or a page, the next reading or writing fails. The same function reads `/Annots` by `readReferenceArray`, which gives nothing for an array with one direct item: the array is not rewritten, the removed annotation stays listed as a null reference.
- **Fix:** follow `/Popup` only to a dictionary of `/Subtype /Popup`, never to a page; rewrite `/Annots` item by item (the tools of phase 2 do it: `PDFDocumentLinkOperations::removeLinks`, `PDFDocumentSanitizer::removeAnnotations`).

### 31. The permissions of a revision 2 security handler are read by the table of revision 3

- **Where:** `PDFStandardSecurityHandler::isAllowed` (`pdfsecurityhandler.cpp`): the new tools (catalog bits of ISO 32000-2 Table 22) and the old applications; found by P2DECIDE of phase 2, read from the code.
- **Attack:** bits 9 to 12 of `/P` exist only from revision 3; with revision 2 the assembly of pages, filling of forms and high-quality print fall under bits 4, 6 and 3. `isAllowed` ignores the revision, and a revision 2 file usually has the higher bits set to 1: a document, which forbids changes by bit 4, still lets Rotate and Remove Pages run (bit 11).
- **Fix:** with `/R 2` answer bits 9 to 12 by bits 3, 4 and 6 as the table of revision 2 says (the owner decided 2026-11-06: phase 3, it changes the old applications too).

### 32. The page tree is read by a recursion without a depth limit

- **Where:** `PDFPage::parseImpl` (`pdfpage.cpp`), called when every document is read (`PDFCatalog::parse`) and by `PDFDocumentBuilder::flattenPageTree` (Remove Pages, Remove Hidden Data, the old Remove External Links); found by P2WALK of phase 2, read from the code.
- **Attack:** a chain of tens of thousands of `/Pages` nodes, each the only kid of the previous one, recurses once per level: the stack of the reading thread overflows and the application ends. The visited set is one for the whole tree (no cycle, no repeated subtree), but a page or a node listed by two parents makes the whole document unreadable ("Detected cycles in page tree").
- **Fix:** the walk by `PDFObjectWalk` (`pdfobjectutils.h`) with a depth limit far above any real tree (a balanced tree of 8 million pages has a depth below 30), a deeper tree reported as damaged; a node listed twice skipped instead of refusing the document.

### 33. The visibility expression of optional content is read by a recursion without a depth limit

- **Where:** the parser of `/VE` of an optional content membership dictionary (`pdfoptionalcontent.cpp`, the recursive `parseNode`), when the page content refers to the dictionary (every renderer); found by P2WALK of phase 2, read from the code.
- **Attack:** a direct array nested tens of thousands of times (`[/Not [/Not [/Not ...]]]`) or a chain of indirect arrays recurses once per level: the stack overflows. An operand array shared by two operators is refused as a cycle (one set for the whole expression), so a valid expression, which repeats a sub-expression, does not work.
- **Fix:** a depth limit (ISO 32000-2, 8.11.2.2 sets none; 64 levels suffice) and a node of a shared operand array parsed once and reused (a cycle stays refused) - never a set per path, which repeats a shared part for each path (the reason of `PDFObjectWalk`).
