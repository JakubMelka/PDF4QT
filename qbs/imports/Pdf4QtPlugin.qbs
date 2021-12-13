Pdf4QtLibrary {
    Depends { name: "Pdf4QtLib" }

    Group {
        fileTagsFilter: "dynamiclibrary"
        qbs.install: install
        qbs.installDir: "bin/pdfplugins"
        qbs.installSourceBase: buildDirectory
    }
}

