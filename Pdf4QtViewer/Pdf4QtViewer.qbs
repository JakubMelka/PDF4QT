DynamicLibrary {
    name: "Pdf4QtViewer"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    files: [
        "pdfaboutdialog.h",
        "pdfadvancedfindwidget.h",
        "pdfdocumentpropertiesdialog.h",
        "pdfencryptionsettingsdialog.h",
        "pdfencryptionstrengthhintwidget.h",
        "pdfoptimizedocumentdialog.h",
        "pdfprogramcontroller.h",
        "pdfrecentfilemanager.h",
        "pdfrendertoimagesdialog.h",
        "pdfsendmail.h",
        "pdfsidebarwidget.h",
        "pdftexttospeech.h",
        "pdfundoredomanager.h",
        "pdfviewerglobal.h",
        "pdfviewermainwindow.h",
        "pdfviewermainwindowlite.h",
        "pdfviewersettingsdialog.h",
        "pdfviewersettings.h",
        "pdfaboutdialog.cpp",
        "pdfadvancedfindwidget.cpp",
        "pdfdocumentpropertiesdialog.cpp",
        "pdfencryptionsettingsdialog.cpp",
        "pdfencryptionstrengthhintwidget.cpp",
        "pdfoptimizedocumentdialog.cpp",
        "pdfprogramcontroller.cpp",
        "pdfrecentfilemanager.cpp",
        "pdfrendertoimagesdialog.cpp",
        "pdfsendmail.cpp",
        "pdfsidebarwidget.cpp",
        "pdftexttospeech.cpp",
        "pdfundoredomanager.cpp",
        "pdfviewermainwindow.cpp",
        "pdfviewermainwindowlite.cpp",
        "pdfviewersettings.cpp",
        "pdfviewersettingsdialog.cpp",
        "pdfaboutdialog.ui",
        "pdfadvancedfindwidget.ui",
        "pdfdocumentpropertiesdialog.ui",
        "pdfencryptionsettingsdialog.ui",
        "pdfoptimizedocumentdialog.ui",
        "pdfrendertoimagesdialog.ui",
        "pdfsidebarwidget.ui",
        "pdfviewermainwindowlite.ui",
        "pdfviewermainwindow.ui",
        "pdfviewersettingsdialog.ui",
        "pdf4qtviewer.qrc",
    ]
    cpp.includePaths: ["."]
    cpp.defines: ['QT_INSTALL_DIRECTORY=""']
    Depends { name: "Qt"; submodules: ["widgets", "printsupport", "texttospeech", "network", "xml"] }
    Depends { name: "Pdf4QtLib" }
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: ["."]
        Depends { name: "Pdf4QtLib" }
    }
}
