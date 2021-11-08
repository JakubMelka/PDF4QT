import qbs.Utilities

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
        cpp.defines: ["PDF4QTVIEWER_LIBRARY"]
    }
    cpp.defines: base.concat(["QT_INSTALL_DIRECTORY=" + Utilities.cStringQuote(Qt.core.binPath)])
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
