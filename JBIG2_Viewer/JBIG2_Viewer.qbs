QtGuiApplication {
    name: "JBIG2_Viewer"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    files: [
        "main.cpp",
        "mainwindow.h",
        "mainwindow.cpp",
        "mainwindow.ui",
    ]
    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "Pdf4QtLib" }
}
