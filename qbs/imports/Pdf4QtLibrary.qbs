Pdf4QtProduct {
    Depends { name: "cpp" }
    property stringList libType: "dynamiclibrary"
    type: libType

    Group {
        fileTagsFilter: "dynamiclibrary"
        //fileTagsFilter: libType.concat("dynamiclibrary_symlink")
        //    .concat(qbs.debugInformation ? ["debuginfo_dll"] : [])
        qbs.install: install
        qbs.installDir: targetInstallDir
        qbs.installSourceBase: buildDirectory
    }
    targetInstallDir: pdf4qtbuildconfig.libInstallDir
}

