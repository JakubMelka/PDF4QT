Pdf4QtProduct {
    Depends { name: "cpp" }
    type: "application"
    Depends { name: "Pdf4QtLib" }
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: targetInstallDir
        qbs.installSourceBase: buildDirectory
    }
    targetInstallDir: pdf4qtbuildconfig.appInstallDir
}

