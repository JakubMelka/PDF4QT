# Modules

Strictly layered: each library is a shared library depending only on the ones above it.

| Module | Responsibility |
| --- | --- |
| [Pdf4QtLibCore](../Pdf4QtLibCore/) | The PDF engine, no Qt Widgets (Core, Gui, Svg, Xml, Network), buildable alone: parsing, object model, rendering, fonts, color management, encryption, signatures, forms, annotations, optimization, XFA, the OCR core and the document tools of the catalog. |
| [Pdf4QtLibWidgets](../Pdf4QtLibWidgets/) | Page draw widget, draw space, asynchronous page compilers, tool framework, annotation / form widget managers, page content editor tools. |
| [Pdf4QtLibGui](../Pdf4QtLibGui/) | Shell of the old Editor and Viewer: program controller, actions, settings, sidebar, dialogs, text to speech. |
| [Pdf4QtLibFluent](../Pdf4QtLibFluent/) | Style and components of the GUI overhaul ([fluent-style.md](fluent-style.md)). Only Qt (Core, Gui, Widgets, Svg), not Pdf4QtLibCore, so every application can link it. Not installed. |
| [Pdf4QtLibMcp](../Pdf4QtLibMcp/) | MCP server: transport and protocol, no document logic. QtCore, QtNetwork and Pdf4QtLibCore only for `PDFOperationControl`. Not installed in phase 1. |
| [Pdf4QtLibApp](../Pdf4QtLibApp/), [Pdf4QtApp](../Pdf4QtApp/) | The new unified application `PDF4QT` ([GUI_overhaul/newapplication.md](../GUI_overhaul/newapplication.md)). Not installed in phase 1. |
| Applications | Editor and Viewer (thin `main.cpp` over Pdf4QtLibGui, the Viewer read-only), PageMaster, Diff, LaunchPad, PdfTool (one self-registering `pdftool*.cpp` per command). |
| Plugins | [plugins.md](plugins.md). |
| Dev tools | Pdf4QtFluentGallery (everything the style draws; `--theme <id> --export <dir> --scale <n>`, `--screenshot <file>`), CodeGenerator (generates the builder API, [document-model.md](document-model.md)), JBIG2_Viewer, PdfExampleGenerator. |

Namespaces: `pdf`, `pdfviewer` (Gui layer), `pdfplugin`. Export macros: `PDF4QTLIBCORESHARED_EXPORT`, `PDF4QTLIBWIDGETSSHARED_EXPORT`, `PDF4QTLIBGUILIBSHARED_EXPORT`, `PDF4QTLIBFLUENTSHARED_EXPORT`, `PDF4QTLIBMCPSHARED_EXPORT`, `PDF4QTLIBAPPSHARED_EXPORT`.

The new application replaces the Editor, Viewer and the others: one application, whose features are gated by the lock of a document, not by the application. The old applications are not reused; they keep working and no old functionality is deleted until the end of phase 2. Logic used by both lives once in Pdf4QtLibCore, the old place calls it.

## Data and threads

- A document is immutable; a change produces a new document ([document-model.md](document-model.md)). In the new application `PDFAppDocument` commits it as a new version with one undo step.
- Work off the GUI thread: page compilation and text layout, reading a document, text index, tool runs and previews, MCP jobs, thumbnails, OCR. Fan-out only through `PDFExecutionPolicy`, cancellation by `PDFOperationControl`, progress by `PDFProgress`; results return to the GUI thread by queued signals / invocations.
- Widgets and everything of Pdf4QtLibFluent live in the GUI thread; a worker hands a picture over as a pixmap / image.

## The new application (Pdf4QtLibApp)

- **Storage** (`sources/storage`): one SQLite database in WAL per user instead of QSettings; one connection per thread; damaged, newer or unopenable files are renamed or copied into memory (*Try Again*); portable mode = an in-memory database. One file per schema version `<data>/pdf4qt-<schema>.db` (owner O19): a missing file is created from a copy of the newest older file by the backup API, migrated in a temporary file under a lock and renamed; the older file is never written or deleted automatically, so the older version keeps working. Migration rules ([newapp_storage.md](../GUI_overhaul/newapp_storage.md) 5): a released migration is never changed, columns are never renamed or dropped, a new column has a default, unknown values read as defaults, SQL always names its columns, a breaking change is a new schema version. Other connections and processes learn about changes by `PDFDatabaseNotifier` (change log triggers + `PRAGMA data_version`). Only this library links SQLite (C API).
- **Core** (`sources/core`): setting definitions, the command registry, command line, forwarding of files to a running instance (`PDFAppInstanceGuard`, `QLocalServer`), log, `PDFAppContext`. Owner O17: dates, times, sizes and numbers are formatted by `PDFAppLocale` in the language of the user interface, never by `QLocale()` / `QLocale::system()`; the context makes it the default locale of the process, so `%Ln`, Qt widgets and Fluent follow it; regional values (paper, printer units, voice) come from `getRegionalLocale()`. Relative times of O25 are refreshed by the shared minute ticker of the context.
- **Shell** (`sources/shell`): window, router, history and pages. A page changes its own route by `PDFAppPage::replaceRoute`, never through the history of its tab.
- **Document tab** (`sources/document`): the model `PDFAppDocument` (version, commit, undo), the registry, the tab with its persistent view, one command handler per package. `PDFAppAnnotationController` (one per view) is the only place where the engine of the page (tool, annotation and form managers in their delegated mode) is committed to the model: one undo step per engine change, merge sessions of live changes, requests re-emitted from the event loop, lock and permissions.
- **Settings** (`sources/settings`): sections in one registry; one generic row per setting definition, its control chosen by the type and the presentation hints.
- **Pages** (`sources/pages`): every page derives from `PDFAppContentPage` and is laid out by the helpers of [pdfapppagelayout.h](../Pdf4QtLibApp/sources/pages/pdfapppagelayout.h); a page adds no own layout classes.
- **Tools** (`sources/tools`, catalog and tools in the core, [GUI_overhaul/newapp_tools.md](../GUI_overhaul/newapp_tools.md)): a thin tool is declarative (parameters, descriptions, one core function); the shell, panels, result dialog, runner, measuring and page selection are shared. Every tool has an MCP action and a help topic; F1 opens the topic of the current page. Compress, Optimize Structure and Remove Hidden Data never save incrementally - a signed document gets only a copy written whole (O31). `edit.content`, `edit.page-numbers` and `convert.ocr` are not started without the explicit permission of the owner (O32).
- **MCP** (`sources/mcp`): the controller of the context (settings `mcp.*`, start / stop, token, port probe; one server per user - the row `mcp_server` with its heartbeat, requests of other instances by `mcp_request`), the host in the GUI thread (permissions Allow / Ask / Deny, a read approval per assistant, the worker pool, commits as one undo step with the source Mcp, the activity log), the frozen tool contract, the confirmations in the document tab and the page AI. A local server requires no token by default (owner 2026-10-28); the remote mode forces it. The English docs, skills and workflows served as resources are in `resources/mcp`.

## The MCP library (Pdf4QtLibMcp)

Design: [GUI_overhaul/newapp_mcp.md](../GUI_overhaul/newapp_mcp.md). Streamable HTTP on one endpoint `/mcp` with its own strict HTTP/1.1 parser. Both protocol eras at once (owner O26): the stateless revision 2026-07-28 (`server/discover`, `subscriptions/listen`, `input_required` with a sealed request state, the tasks extension) and the legacy 2025-11-25 / 2025-06-18 (`initialize`, `Mcp-Session-Id`).

- Threads: the sockets live in a fixed pool of 4 I/O threads; the listeners and the dispatcher (token, Host / Origin, era, sessions, assistants, streams, tasks, rate limits, list methods from an immutable catalog snapshot) in one control thread; the abstract host is called only in its own GUI thread by queued invocations. Nobody blocks: no lock across threads, a result for a dead host or connection is dropped. `PDFMcpServer::stop()` is synchronous and runs before the host is destroyed.
- JSON: one pass of `PDFMcpJsonScanner` (size, depth, members, duplicate keys, UTF-8, the exact top-level id) before `QJsonDocument`, differentially fuzzed against Qt; a strict reader, an ordered writer for deterministic schemas (no `oneOf` / `$ref`). Every limit is in `PDFMcpLimits`, every error code in `PDFMcpErrorCodes`; prefix `PDFMcp`.
