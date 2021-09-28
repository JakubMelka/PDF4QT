Pdf4QtLibrary {
    Depends { name: "Pdf4QtLib" }

    Group {
        fileTagsFilter: "dynamiclibrary"
        //fileTagsFilter: libType.concat("dynamiclibrary_symlink")
        //    .concat(qbs.debugInformation ? ["debuginfo_dll"] : [])
        qbs.install: install
        qbs.installDir: "plugins"
        qbs.installSourceBase: buildDirectory
    }
}

