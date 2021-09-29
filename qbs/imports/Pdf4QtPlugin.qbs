Pdf4QtLibrary {
    Depends { name: "Pdf4QtLib" }

    Group {
        fileTagsFilter: "dynamiclibrary"
        qbs.install: install
        qbs.installDir: "pdfplugins"
        qbs.installSourceBase: buildDirectory
    }
}

