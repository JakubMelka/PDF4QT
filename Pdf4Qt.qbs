import qbs

Project {
    name: "Pdf4Qt"
    references: [
        "Pdf4QtLib/Pdf4QtLib.qbs",
        "Pdf4QtViewer/Pdf4QtViewer.qbs",
        "Pdf4QtViewerLite/Pdf4QtViewerLite.qbs",
        "CodeGenerator/CodeGenerator.qbs",
        "JBIG2_Viewer/JBIG2_Viewer.qbs",
        "Pdf4QtDocPageOrganizer/Pdf4QtDocPageOrganizer.qbs",
        "Pdf4QtViewerPlugins/AudioBookPlugin/AudioBookPlugin.qbs",
    ]
}
