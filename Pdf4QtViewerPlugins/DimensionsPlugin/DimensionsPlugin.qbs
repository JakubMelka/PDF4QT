import qbs

Pdf4QtPlugin {
    name: "DimensionsPlugin"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "icons.qrc",
    ]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "DIMENTIONPLUGIN_LIBRARY"
    }
}
