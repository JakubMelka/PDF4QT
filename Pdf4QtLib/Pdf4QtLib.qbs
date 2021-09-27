import qbs

DynamicLibrary {
    name: "Pdf4QtLib"
    Depends { name: "cpp" }
    cpp.cxxLanguageVersion: "c++2a"
    Depends { name: "Qt"; submodules: ["core", "gui", "widgets"] }
    Depends { name: "openssl" }
    Depends { name: "freetype2" }
    Depends { name: "libjpeg" }
    Depends { name: "libopenjp2" }
    Depends { name: "lcms2" }
    Depends { name: "tbb" }
    files: [
        "sources/*.cpp",
        "sources/*.h",
        "sources/pdfrenderingerrorswidget.ui",
        "sources/pdfselectpagesdialog.ui",
        "cmaps.qrc",
    ]
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: ["sources"]
        Depends { name: "openssl" }
        Depends { name: "freetype2" }
        Depends { name: "libjpeg" }
        Depends { name: "libopenjp2" }
        Depends { name: "lcms2" }
        Depends { name: "tbb" }
    }
}
