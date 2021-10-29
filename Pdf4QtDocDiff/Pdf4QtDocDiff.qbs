Pdf4QtApp {
    name: "Pdf4QtDocDiff"
    files: [
        "*.cpp",
        "*.h",
        "*.ui",
        "resources.qrc",
    ]
    cpp.includePaths: ["."]
    Depends { name: "Qt"; submodules: ["widgets"] }
}
