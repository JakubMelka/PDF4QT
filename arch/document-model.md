# Document model

- `PDFObject` ([pdfobject.h](../Pdf4QtLibCore/sources/pdfobject.h)) is an immutable, implicitly shared variant of the eight PDF object types in 16 bytes. `PDFDictionary` and `PDFArray` are immutable too (header and items in one allocation): build or change them by `PDFDictionaryBuilder` / `PDFArrayBuilder` (`PDFDictionaryBuilder builder(*dictionary); builder.setEntry(...); PDFObject::createDictionary(std::move(builder))`); `setFixedSize(n)` only when the number of items is known in advance.
- `PDFObjectStorage` holds all objects and is thread-safe for reading only. `PDFDocument` (storage + catalog) is immutable: every modification produces a new document.
- Changes flow as `PDFModifiedDocument`: the new document plus flags (`Reset`, `Annotation`, `FormField`, `PageContents`, ...). Consumers skip expensive rebuilds by the flags; when unsure, `Reset` is the conservative choice.
- Values are read out of dictionaries by `PDFDocumentDataLoaderDecorator` (defaulted and throwing accessors), not by hand.
- The whole document is parsed at open, so the file is not needed afterwards - no lazy loading, by design.
- The permissions of an encrypted document (ISO 32000-2, 7.6.4, Table 22) are respected everywhere in the new application, by one shared check (`PDFAppDocumentPermissions`); only the owner password lifts them, for the session of the tab (the security handler authorized as the owner, the encryption dictionary and `/P` of every write unchanged) - there is no setting to ignore them (owner 2026-11-08: conformance, not a deviation).
- A damaged file is restored by the permissive reading from the bytes (of more objects of one reference the last readable one wins). A restored file is told to the user, and a restored signed file is saved only as a copy written whole (owner 2026-11-05).

## Writing

- Writing goes through `PDFDocumentBuilder` ([pdfdocumentbuilder.h](../Pdf4QtLibCore/sources/pdfdocumentbuilder.h)). A large part of its API is generated from [generated_code_definition.xml](../generated_code_definition.xml) by the CodeGenerator application: regenerate, never hand-edit those methods.
- `PDFDocumentWriter`, `PDFOptimizer`, `PDFDocumentSanitizer`, `PDFDocumentManipulator` and `PDFRedact` work on builder-produced documents. The tools of the catalog and the old applications share one implementation of each operation in the core (page operations, link operations, the hidden data scan, the text exporter, the output formatter of PdfTool).

## Errors

- Errors are thrown as `PDFException`; rendering errors are collected non-fatally by `PDFRenderErrorReporter`.
- Out of memory is not handled (owner 2026-11-05): no `catch` of `std::bad_alloc`, no dialog - the application ends.
- Deliberate deviations from PDF 2.0 and parts not implemented are listed in [NOTES.txt](../NOTES.txt); security weaknesses found outside the current task in [SECURITY_TODO.md](../SECURITY_TODO.md).
