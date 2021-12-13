Product {
    Depends { name: "pdf4qtbuildconfig" }
    Depends { name: "cpp" }
    Depends { name: "Qt.core" }
    cpp.cxxLanguageVersion: "c++20"
    cpp.minimumWindowsVersion: "6.1" // Require at least Windows 7, older versions will be ignored
    property bool install: true
    property string targetInstallDir
}

