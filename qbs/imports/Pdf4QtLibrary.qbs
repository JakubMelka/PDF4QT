Pdf4QtProduct {
    Depends { name: "cpp" }
    property stringList libType: "dynamiclibrary"
    type: libType

    Group {
        fileTagsFilter: "dynamiclibrary"
        qbs.install: install
        qbs.installDir: targetInstallDir
        qbs.installSourceBase: buildDirectory
    }
    targetInstallDir: pdf4qtbuildconfig.libInstallDir
}

