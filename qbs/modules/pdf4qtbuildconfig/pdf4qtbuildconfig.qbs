Module {
    property string libDirName: "lib"
    property string appInstallDir: "bin"
    property string libInstallDir: qbs.targetOS.contains("windows") ? "bin" : libDirName
}

