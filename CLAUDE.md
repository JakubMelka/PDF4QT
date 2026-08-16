# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

PDF4QT is a C++20 / Qt 6 PDF library plus a family of desktop applications (editor, viewer, page master, diff, launch pad) and a command line tool. Single author project (Jakub Melka), MIT licensed since April 2025.

## Working on GitHub issues

When a GitHub issue is fixed or an enhancement implemented, **the issue must be recorded in [RELEASES.txt](RELEASES.txt)** under the `CURRENT:` section at the top of the file, in the form:

```
 - Issue #NNN: <issue title as it appears on GitHub>
```

Newest issues go first inside `CURRENT:`; the section is renamed to a version line (`V: 1.6.0.0 14.6.2026`) at release time. Commit messages follow the same convention: `Issue #NNN: <issue title>`.

## Build and test

Do not run builds unless the user explicitly asks for a build in the current conversation (see [AGENTS.md](AGENTS.md)).

Configure (vcpkg toolchain is required; Qt 6.9+):

```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Useful options: `PDF4QT_BUILD_ONLY_CORE_LIBRARY` (skips every GUI target and the apps), `PDF4QT_BUILD_TESTS` (ON by default), `PDF4QT_QT_ROOT` (needed when installing Qt dependencies), `PDF4QT_INSTALL_TO_USR`. On Linux set `VCPKG_OVERLAY_PORTS` to `vcpkg/overlays/linux:vcpkg/overlays/general` to avoid the incompatible libpng crash.

On this machine there is an existing Ninja + MSVC debug tree at `build/msvc2022_debug` (Qt 6.9.0 at `E:/Programming/Qt/6.9.0/msvc2022_64`). Build it by writing a `.bat` to the scratchpad (nested quotes get mangled otherwise) that calls `vcvars64.bat`, `cd /d` into the build dir and runs `ninja <target>`.

Tests are QtTest executables, one per area, all built into `<build>/usr/bin`:

| Target | File |
| --- | --- |
| `UnitTests` | [tst_lexicalanalyzertest.cpp](UnitTests/tst_lexicalanalyzertest.cpp) |
| `UnitTestsImageOptimizer` | [tst_imageoptimizertest.cpp](UnitTests/tst_imageoptimizertest.cpp) |
| `UnitTestsFontEncoding` | [tst_fontencodingtest.cpp](UnitTests/tst_fontencodingtest.cpp) |
| `UnitTestsAuthorSettings` | [tst_authorsettingstest.cpp](UnitTests/tst_authorsettingstest.cpp) |
| `UnitTestsContentEditor` | [tst_contenteditortest.cpp](UnitTests/tst_contenteditortest.cpp) |

Run all of them with `ctest` from the build dir, a single binary directly (`./UnitTestsFontEncoding`), or a single test function with `./UnitTests <testFunctionName>`. The executables need Qt's `bin` on `PATH`; QtTest stdout is swallowed in some shells here, so capture with `-o result.txt,txt` and read the file. A new test needs its own `add_executable` + `add_test` block in [UnitTests/CMakeLists.txt](UnitTests/CMakeLists.txt). Tests touching `QRawFont` or any GUI type must use `QTEST_MAIN` (QGuiApplication), not `QTEST_APPLESS_MAIN`.

## Architecture

### Module layering

Strictly layered; each layer is a shared library that only depends on the ones above it.

- **[Pdf4QtLibCore/](Pdf4QtLibCore/)** — the PDF engine. No Qt Widgets dependency (Core, Gui, Svg, Xml only), so it can be built stand-alone via `PDF4QT_BUILD_ONLY_CORE_LIBRARY`. Parsing, object model, rendering, fonts, color management, encryption, signatures, forms, annotations, optimization, XFA.
- **[Pdf4QtLibWidgets/](Pdf4QtLibWidgets/)** — widget layer: the page draw widget, draw space controller, asynchronous compilers, tool framework, annotation/form widget managers, page content editor tools.
- **[Pdf4QtLibGui/](Pdf4QtLibGui/)** — the application shell shared by Editor and Viewer: main windows, `PDFProgramController`, `PDFActionManager`, settings, sidebar, dialogs, text-to-speech.
- **Applications** — [Pdf4QtEditor/](Pdf4QtEditor/), [Pdf4QtViewer/](Pdf4QtViewer/) (both are thin `main.cpp` shells over Pdf4QtLibGui, Editor with editing features, Viewer read-only), [Pdf4QtPageMaster/](Pdf4QtPageMaster/), [Pdf4QtDiff/](Pdf4QtDiff/), [Pdf4QtLaunchPad/](Pdf4QtLaunchPad/) (launcher for the others), [PdfTool/](PdfTool/) (CLI; one `pdftool*.cpp` per subcommand, all deriving from `PDFToolAbstractApplication` and self-registering).
- **[Pdf4QtEditorPlugins/](Pdf4QtEditorPlugins/)** — runtime-loaded plugins, see below.
- **Dev tools** — [CodeGenerator/](CodeGenerator/) (GUI editor for [generated_code_definition.xml](generated_code_definition.xml), which generates the `PDFDocumentBuilder` API), [JBIG2_Viewer/](JBIG2_Viewer/), [PdfExampleGenerator/](PdfExampleGenerator/).

Everything lives in `namespace pdf` (GUI-layer classes in `namespace pdfviewer`, plugins in `namespace pdfplugin`). Export macros: `PDF4QTLIBCORESHARED_EXPORT`, `PDF4QTLIBWIDGETSSHARED_EXPORT`, `PDF4QTLIBGUILIBSHARED_EXPORT`.

### Document model

`PDFObject` ([pdfobject.h](Pdf4QtLibCore/sources/pdfobject.h)) is an immutable, implicitly-shared variant of the eight PDF object types. `PDFObjectStorage` holds all objects and is thread-safe for reading only. `PDFDocument` ([pdfdocument.h](Pdf4QtLibCore/sources/pdfdocument.h)) wraps the storage plus catalog and is **immutable** — every modification produces a new document.

Because of that, changes flow through `PDFModifiedDocument`, which carries the new document plus modification flags (`Reset`, `Annotation`, `FormField`, `PageContents`, …). Consumers use the flags to skip expensive rebuilds; when unsure, `Reset` is the conservative choice. Read values out of dictionaries with `PDFDocumentDataLoaderDecorator` rather than by hand — it has both defaulted and throwing accessors.

Writing/editing goes through `PDFDocumentBuilder` ([pdfdocumentbuilder.h](Pdf4QtLibCore/sources/pdfdocumentbuilder.h)). A large part of its API is generated from `generated_code_definition.xml` by the `CodeGenerator` app — regenerate rather than hand-editing those methods. `PDFDocumentWriter`, `PDFOptimizer`, `PDFDocumentSanitizer`, `PDFDocumentManipulator` and `PDFRedact` operate on the same builder-produced documents.

Errors are reported by throwing `PDFException` ([pdfexception.h](Pdf4QtLibCore/sources/pdfexception.h)); rendering errors are collected non-fatally through `PDFRenderErrorReporter`.

### Rendering pipeline

`PDFPageContentProcessor` ([pdfpagecontentprocessor.h](Pdf4QtLibCore/sources/pdfpagecontentprocessor.h)) is the single interpreter of page content streams — it decodes operators, maintains `PDFPageContentProcessorState` (the PDF graphic state) and calls virtual `performXxx` hooks. Everything that consumes page content derives from it:

- `PDFPainter` — draws directly onto a `QPainter` (basic transparency only).
- `PDFPrecompiledPageGenerator` → `PDFPrecompiledPage` — bakes the content into a replayable instruction list; this is what the viewer caches and "plays" for fast repaints.
- `PDFTransparencyRenderer` — full transparency groups, blend modes and spot colors, software rasterized.
- `PDFBLPainter` ([pdfblpainter.h](Pdf4QtLibCore/sources/pdfblpainter.h)) — Blend2D-backed `QPaintEngine` used as an alternative backend.
- `PDFTextLayoutGenerator`, `PDFPageContentEditorProcessor`, `PDFJavaScriptScanner`, image extraction — non-drawing consumers.

Add support for a new operator or graphic feature in the processor first, then in each derived painter that needs it.

In the widget layer, `PDFDrawSpaceController` lays pages out in millimeters (zoom-independent, block based), `PDFDrawWidgetProxy` maps that device space to widget pixels, and `PDFAsynchronousPageCompiler` / `PDFAsynchronousTextLayoutCompiler` ([pdfcompiler.h](Pdf4QtLibWidgets/sources/pdfcompiler.h)) compile pages on worker threads into a size-limited cache. Long operations honour `PDFOperationControl` for cancellation and report through `PDFProgress`. Thread fan-out is centralized in `PDFExecutionPolicy` ([pdfexecutionpolicy.h](Pdf4QtLibCore/sources/pdfexecutionpolicy.h)) — use it instead of spawning threads ad hoc.

### Plugins

Plugins are `SHARED` libraries deriving from `PDFPlugin` ([pdfplugin.h](Pdf4QtLibCore/sources/pdfplugin.h)), built into `${PDF4QT_PLUGINS_DIR}` and described by a sibling `<Name>Plugin.json` metadata file. They receive the widget and a `IPluginDataExchange` back-channel to the host application, and contribute `QAction`s. Existing ones: AudioBook, Dimensions, Editor, ObjectInspector, OutputPreview, Redact, Scanner, Signature, SoftProofing. A new plugin means a directory under [Pdf4QtEditorPlugins/](Pdf4QtEditorPlugins/), an `add_subdirectory` in its CMakeLists, the JSON descriptor, and an install rule.

## Conventions

- **CRLF line endings** for all source and text files (`.gitattributes` enforces `eol=crlf`); `.desktop` files stay LF.
- Every file starts with the MIT license header block (`Copyright (c) 2018-2025 Jakub Melka and Contributors`).
- `QT_NO_EMIT` is defined project-wide — signals are emitted with `Q_EMIT`, never `emit`.
- MSVC builds with `/W4`; keep new code warning-free (`/wd5054`, `/wd4127`, `/wd4702` are the only suppressions).
- User-visible strings must be translatable (`tr()` / `QApplication::translate`). `.ts` files under [translations/](translations/) are regenerated by the `PDF4QT_lupdate` target and normalized to CRLF by [cmake/NormalizeTsLineEndings.cmake](cmake/NormalizeTsLineEndings.cmake); do not hand-edit them.
- [NOTES.txt](NOTES.txt) tracks, section by section, where the implementation deliberately deviates from or does not yet implement the PDF 2.0 specification — check it before assuming a gap is a bug.
