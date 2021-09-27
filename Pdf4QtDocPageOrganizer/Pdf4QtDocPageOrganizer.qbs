QtGuiApplication {
    name: "Pdf4QtDocPageOrganizer"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
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
    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "Pdf4QtLib" }
}
