import qbs

Pdf4QtPlugin {
    name: "SoftProofingPlugin"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "SOFTPROOFINGPLUGIN_LIBRARY"
    }
}
