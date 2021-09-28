import qbs

Pdf4QtPlugin {
    name: "SoftProofingPlugin.qbs"
    files: [
        "settingsdialog.h",
        "settingsdialog.cpp",
        "softproofingplugin.h",
        "softproofingplugin.cpp",
        "settingsdialog.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
}
