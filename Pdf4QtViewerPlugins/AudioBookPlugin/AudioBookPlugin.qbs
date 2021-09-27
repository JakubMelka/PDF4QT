import qbs

DynamicLibrary {
    name: "AudioBookPlugin"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    files: [
        "audiobookcreator.h",
        "audiobookcreator.cpp",
        "audiobookplugin.h",
        "audiobookplugin.cpp",
        "audiotextstreameditordockwidget.h",
        "audiotextstreameditordockwidget.cpp",
        "audiotextstreameditordockwidget.ui",
        "icons.qrc",
    ]
    Depends { name: "Qt"; submodules: ["core", "gui", "widgets"] }
    Depends { name: "Pdf4QtLib" }
}
