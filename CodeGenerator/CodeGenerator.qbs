QtGuiApplication {
    name: "CodeGenerator"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    files: [
        "main.cpp",
        "codegenerator.h",
        "codegenerator.cpp",
        "generatormainwindow.h",
        "generatormainwindow.cpp",
        "generatormainwindow.ui",
    ]
    Depends { name: "Qt"; submodules: ["widgets", "xml"] }
    Depends { name: "Pdf4QtLib" }
}
