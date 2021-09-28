import qbs

Pdf4QtPlugin {
    name: "OutputPreviewPlugin.qbs"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
}
