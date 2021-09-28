import qbs

Pdf4QtPlugin {
    name: "RedactPlugin.qbs"
    files: [
        "createredacteddocumentdialog.h",
        "createredacteddocumentdialog.cpp",
        "redactplugin.h",
        "redactplugin.cpp",
        "createredacteddocumentdialog.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
}
