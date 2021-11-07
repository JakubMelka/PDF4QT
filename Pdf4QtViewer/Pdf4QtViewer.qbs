Pdf4QtLibrary {
    name: "Pdf4QtViewer"
    files: [
        "*.h",
        "*.cpp",
        "*.ui",
        "pdf4qtviewer.qrc",
    ]
    cpp.includePaths: ["."]
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: ["PDF4QTVIEWER_LIBRARY", 'QT_INSTALL_DIRECTORY=""']
    }
    Properties {
        condition: qbs.hostOS.contains("linux")
        cpp.defines: ['QT_INSTALL_DIRECTORY=""']
    }
    Depends { name: "Qt"; submodules: ["printsupport", "texttospeech", "network", "xml"] }
    Depends { name: "Qt.winextras"; condition: qbs.hostOS.contains("windows") }
    Depends { name: "Pdf4QtLib" }
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: ["."]
        Depends { name: "Pdf4QtLib" }
        Depends { name: "Qt.winextras"; condition: qbs.hostOS.contains("windows") }
    }
}
