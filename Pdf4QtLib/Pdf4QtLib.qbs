import qbs

Pdf4QtLibrary {
    name: "Pdf4QtLib"
    Depends { name: "Qt"; submodules: ["core", "gui", "widgets", "xml"] }
    Depends { name: "openssl" }
    Depends { name: "freetype" }
    Depends { name: "libjpeg" }
    Depends { name: "openjpeg" }
    Depends { name: "lcms" }
    Depends {
        condition: qbs.toolchain.contains("gcc")
        name: "tbb"
    }
    Depends {
        condition: qbs.hostOS.contains("linux")
        name: "fontconfig"
    }
    Properties {
        condition: qbs.hostOS.contains("windows")
        cpp.defines: "PDF4QTLIB_LIBRARY"
        cpp.cxxFlags: "/bigobj"
    }

    files: [
        "sources/*.cpp",
        "sources/*.h",
        "sources/*.ui",
        "cmaps.qrc",
    ]
    Export {
        Depends { name: "cpp" }
        Depends { name: "Qt"; submodules: ["core", "gui", "widgets", "xml"] }
        cpp.includePaths: ["sources"]
        Depends { name: "openssl" }
        Depends { name: "freetype" }
        Depends { name: "libjpeg" }
        Depends { name: "openjpeg" }
        Depends { name: "lcms" }
        Depends {
            condition: qbs.toolchain.contains("gcc")
            name: "tbb"
        }
        Depends {
            condition: qbs.hostOS.contains("linux")
            name: "fontconfig"
        }
    }
}
