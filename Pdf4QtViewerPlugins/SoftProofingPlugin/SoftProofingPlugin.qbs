import qbs

Pdf4QtPlugin {
    name: "SoftProofingPlugin.qbs"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
}
