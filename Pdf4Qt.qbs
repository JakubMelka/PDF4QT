import qbs
import qbs.Probes

Project {
    name: "Pdf4Qt"
    qbsSearchPaths: ["qbs"]

    Probes.ConanfileProbe {
        id: conan
        conanfilePath: project.sourceDirectory + "/conanfile.txt"
        generators: "qbs"
    }

    references: {
        var ref = ["Pdf4QtLib/Pdf4QtLib.qbs",
                   "Pdf4QtViewer/Pdf4QtViewer.qbs",
                   "Pdf4QtViewerLite/Pdf4QtViewerLite.qbs",
                   "CodeGenerator/CodeGenerator.qbs",
                   "JBIG2_Viewer/JBIG2_Viewer.qbs",
                   "Pdf4QtDocPageOrganizer/Pdf4QtDocPageOrganizer.qbs",
                   "Pdf4QtDocDiff/Pdf4QtDocDiff.qbs",
                   "Pdf4QtViewerPlugins/AudioBookPlugin/AudioBookPlugin.qbs",
                   "Pdf4QtViewerPlugins/DimensionsPlugin/DimensionsPlugin.qbs",
                   "Pdf4QtViewerPlugins/OutputPreviewPlugin/OutputPreviewPlugin.qbs",
                   "Pdf4QtViewerPlugins/RedactPlugin/RedactPlugin.qbs",
                   "Pdf4QtViewerPlugins/SoftProofingPlugin/SoftProofingPlugin.qbs",
                   "Pdf4QtViewerProfi/Pdf4QtViewerProfi.qbs",
                   "PdfExampleGenerator/PdfExampleGenerator.qbs",
                   "PdfTool/PdfTool.qbs",
                   "UnitTests/UnitTests.qbs"];
        ref.push(conan.generatedFilesPath + "/conanbuildinfo.qbs");
        return ref;
    }
}
