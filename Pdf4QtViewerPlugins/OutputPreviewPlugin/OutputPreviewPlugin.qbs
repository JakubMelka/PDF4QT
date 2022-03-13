import qbs

Pdf4QtPlugin {
    name: "OutputPreviewPlugin"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "OUTPUTPREVIEWPLUGIN_LIBRARY"
    }
}
