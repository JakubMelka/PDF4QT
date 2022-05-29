import qbs

Pdf4QtPlugin {
    name: "SignaturePlugin"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "SIGNATUREPLUGIN_LIBRARY"
    }
}
