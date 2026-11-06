# Plugins

Plugins of the Editor are `SHARED` libraries deriving from `PDFPlugin` ([pdfplugin.h](../Pdf4QtLibCore/sources/pdfplugin.h)), loaded at run time from `${PDF4QT_PLUGINS_DIR}` and described by a sibling `<Name>Plugin.json`. They receive the widget and an `IPluginDataExchange` back-channel to the host application, and contribute `QAction`s. Existing: AudioBook, Dimensions, Editor, ObjectInspector, OutputPreview, Redact, Scanner, Signature, SoftProofing.

A new plugin is a directory under [Pdf4QtEditorPlugins](../Pdf4QtEditorPlugins/), an `add_subdirectory` in its CMakeLists, the JSON descriptor and an install rule.
