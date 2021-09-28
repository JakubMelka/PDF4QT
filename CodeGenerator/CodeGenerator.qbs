Pdf4QtApp {
    name: "CodeGenerator"
    files: [
        "main.cpp",
        "codegenerator.h",
        "codegenerator.cpp",
        "generatormainwindow.h",
        "generatormainwindow.cpp",
        "generatormainwindow.ui",
    ]
    Depends { name: "Qt"; submodules: ["xml"] }
}
