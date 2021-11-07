import qbs

Pdf4QtPlugin {
    name: "AudioBookPlugin"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "AUDIOBOOKPLUGIN_LIBRARY"
    }
}
