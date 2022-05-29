import qbs
import qbs.FileInfo
import qbs.Probes

Project {
    name: "Pdf4Qt"
    qbsSearchPaths: ["qbs"]
    property string conanLibDirectory: FileInfo.joinPaths(conan.generatedFilesPath, "lib")

    Probes.ConanfileProbe {
        id: conan
        conanfilePath: project.sourceDirectory + "/conanfile.txt"
        generators: "qbs"
    }

    references: {
        var ref = ["Pdf4QtLib/Pdf4QtLib.qbs",
                   "Pdf4QtViewer/Pdf4QtViewer.qbs",
                   "Pdf4QtViewerPlugins/AudioBookPlugin/AudioBookPlugin.qbs",
                   "Pdf4QtViewerPlugins/DimensionsPlugin/DimensionsPlugin.qbs",
                   "Pdf4QtViewerPlugins/OutputPreviewPlugin/OutputPreviewPlugin.qbs",
                   "Pdf4QtViewerPlugins/RedactPlugin/RedactPlugin.qbs",
                   "Pdf4QtViewerPlugins/SoftProofingPlugin/SoftProofingPlugin.qbs",
				   "Pdf4QtViewerPlugins/SignaturePlugin/SignaturePlugin.qbs",
                ];
        ref.push(conan.generatedFilesPath + "/conanbuildinfo.qbs");
        return ref;
    }
    SubProject {
        filePath: "Pdf4QtViewerProfi/Pdf4QtViewerProfi.qbs"
    }
    SubProject {
        filePath: "CodeGenerator/CodeGenerator.qbs"
    }
    SubProject {
        filePath: "JBIG2_Viewer/JBIG2_Viewer.qbs"
    }
    SubProject {
        filePath: "Pdf4QtDocPageOrganizer/Pdf4QtDocPageOrganizer.qbs"
    }
    SubProject {
        filePath: "Pdf4QtViewerLite/Pdf4QtViewerLite.qbs"
    }
    SubProject {
        filePath: "Pdf4QtDocDiff/Pdf4QtDocDiff.qbs"
    }
    SubProject {
        filePath: "PdfExampleGenerator/PdfExampleGenerator.qbs"
    }
    SubProject {
        filePath: "PdfTool/PdfTool.qbs"
    }
    SubProject {
        filePath: "UnitTests/UnitTests.qbs"
    }
}
