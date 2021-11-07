import qbs

Pdf4QtPlugin {
    name: "RedactPlugin.qbs"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "REDACTPLUGIN_LIBRARY"
    }
}
