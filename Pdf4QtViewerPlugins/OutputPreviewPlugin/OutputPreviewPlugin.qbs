import qbs

Pdf4QtPlugin {
    name: "OutputPreviewPlugin.qbs"
    files: [
        "inkcoveragedialog.h",
        "inkcoveragedialog.cpp",
        "outputpreviewdialog.h",
        "outputpreviewdialog.cpp",
        "outputpreviewplugin.h",
        "outputpreviewplugin.cpp",
        "outputpreviewwidget.h",
        "outputpreviewwidget.cpp",
        "inkcoveragedialog.ui",
        "outputpreviewdialog.ui",
        "icons.qrc",
    ]
    cpp.includePaths: ["."]
}
