Pdf4QtApp {
    name: "CodeGenerator"
    files: [
        "*.cpp",
        "*.h",
        "*.ui",
    ]
    Depends { name: "Qt"; submodules: ["xml"] }
}
