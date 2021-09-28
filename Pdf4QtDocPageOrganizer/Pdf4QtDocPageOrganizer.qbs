Pdf4QtApp {
    name: "Pdf4QtDocPageOrganizer"
    files: [
        "aboutdialog.cpp",
        "assembleoutputsettingsdialog.cpp",
        "main.cpp",
        "mainwindow.cpp",
        "pageitemdelegate.cpp",
        "pageitemmodel.cpp",
        "selectbookmarkstoregroupdialog.cpp",
        "aboutdialog.h",
        "assembleoutputsettingsdialog.h",
        "mainwindow.h",
        "pageitemdelegate.h",
        "pageitemmodel.h",
        "selectbookmarkstoregroupdialog.h",
        "aboutdialog.ui",
        "assembleoutputsettingsdialog.ui",
        "mainwindow.ui",
        "selectbookmarkstoregroupdialog.ui",
        "resources.qrc",
    ]
    cpp.includePaths: ["."]
    Depends { name: "Qt"; submodules: ["widgets"] }
}
