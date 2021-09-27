QtGuiApplication {
    name: "Pdf4QtViewerLite"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    files: [
        "main.cpp"
    ]
    Depends { name: "Qt.widgets" }
    Depends { name: "Pdf4QtViewer" }
}
