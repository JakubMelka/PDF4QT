import qbs

Pdf4QtLibrary {
    name: "Pdf4QtLib"
    Depends { name: "Qt"; submodules: ["core", "gui", "widgets"] }
    Depends { name: "openssl" }
    Depends { name: "freetype2" }
    Depends { name: "libjpeg" }
    Depends { name: "libopenjp2" }
    Depends { name: "lcms2" }
    Depends {
        condition: qbs.toolchain.contains("gcc")
        name: "tbb"
    }
    files: [
        "sources/*.cpp",
        "sources/*.h",
        "sources/*.ui",
        "cmaps.qrc",
    ]
    Export {
        Depends { name: "cpp" }
        Depends { name: "Qt"; submodules: ["core", "gui", "widgets"] }
        cpp.includePaths: ["sources"]
        Depends { name: "openssl" }
        Depends { name: "freetype2" }
        Depends { name: "libjpeg" }
        Depends { name: "libopenjp2" }
        Depends { name: "lcms2" }
        Depends {
            condition: qbs.toolchain.contains("gcc")
            name: "tbb"
        }
    }
}
