Pdf4QtLibrary {
    name: "Pdf4QtViewer"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "pdf4qtviewer.qrc",
    ]
    cpp.includePaths: ["."]
    cpp.defines: ['QT_INSTALL_DIRECTORY=""']
    Depends { name: "Qt"; submodules: ["printsupport", "texttospeech", "network", "xml"] }
    Depends { name: "Pdf4QtLib" }
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: ["."]
        Depends { name: "Pdf4QtLib" }
    }
}
