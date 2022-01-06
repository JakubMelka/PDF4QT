import qbs.FileInfo

Pdf4QtProduct {
    Depends { name: "cpp" }
    type: "application"
    cpp.rpaths: [FileInfo.joinPaths(cpp.rpathOrigin, "..", "lib"), conanLibDirectory]

    Depends { name: "Pdf4QtLib" }
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: targetInstallDir
        qbs.installSourceBase: buildDirectory
    }
    targetInstallDir: pdf4qtbuildconfig.appInstallDir
}
