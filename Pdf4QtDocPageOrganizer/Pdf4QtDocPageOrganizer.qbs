Pdf4QtApp {
    name: "Pdf4QtDocPageOrganizer"
    files: [
        "*.cpp",
        "*.h",
        "*.ui",
        "resources.qrc",
    ]
    cpp.includePaths: ["."]
    Depends { name: "Qt"; submodules: ["widgets"] }
}
