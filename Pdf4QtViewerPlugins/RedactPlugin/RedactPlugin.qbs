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
}
