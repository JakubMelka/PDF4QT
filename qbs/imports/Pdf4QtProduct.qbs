Product {
    Depends { name: "pdf4qtbuildconfig" }
    Depends { name: "cpp" }
    Depends { name: "Qt.core" }
    cpp.cxxLanguageVersion: "c++2a"
    property bool install: true
    property string targetInstallDir
}

