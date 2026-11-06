# PDF4QT architecture

High-level documentation: the modules, the main flows of data and threads, and what the code does not show - why a mechanism exists, decisions of the owner, pitfalls of Qt. Classes, functions and test cases are in the code. The rules are in [CLAUDE.md](../CLAUDE.md).

| File | Content |
| --- | --- |
| [build.md](build.md) | Configuration, precompiled headers, moc, the trees of the development machine |
| [checks.md](checks.md) | Who runs which check; ASan / UBSan, valgrind, TSan / helgrind / drd, coverage |
| [tests.md](tests.md) | One row per test executable, facts needed to run them |
| [layering.md](layering.md) | Modules, data and threads, the new application, the MCP library |
| [document-model.md](document-model.md) | Immutable objects and documents, modifications, writing, errors |
| [rendering.md](rendering.md) | The content processor and its consumers, caches, the widget layer |
| [ocr.md](ocr.md) | OCR core, engine, text layer, models |
| [fluent-style.md](fluent-style.md) | Style and components of the GUI overhaul, Qt pitfalls |
| [plugins.md](plugins.md) | Plugins of the Editor |
| [conventions.md](conventions.md) | Details and reasons of the conventions |
