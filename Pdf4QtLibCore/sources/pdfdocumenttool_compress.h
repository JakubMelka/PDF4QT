// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFDOCUMENTTOOL_COMPRESS_H
#define PDFDOCUMENTTOOL_COMPRESS_H

#include "pdfdocumenttool.h"
#include "pdfimageoptimizer.h"
#include "pdfoptimizer.h"

#include <functional>

namespace pdf
{

/// The tool Compress (optimize.compress, Figma 291:994 ...; plan of the first batch,
/// chapter 3 and owner answer Q2): the image optimizer and the structure optimizer
/// in one step, with the size really measured. The preview is the result
/// (isPreviewFinal): the run is the measurement, confirming it computes nothing again.
///
/// Result:
///   Success - the changed document; facts "size", "images", "time" of the result
///   dialog 296:2761; data of the panel: "preset", "sizeBefore", "sizeAfter",
///   "savedBytes", "savedPercent", "imagesRecompressed", "imagesKept", "breakdownBefore",
///   "breakdownAfter" (PDFDocumentSizeBreakdown), "durationMilliseconds",
///   "signatureCount", "signatureImpact"; "conformanceRemoved" and the fact "conformance",
///   when the document declared PDF/A or PDF/UA (the declaration is removed from the result);
///   NothingToDo - the file is not smaller (295:2182): the same facts and data, no document;
///   Stopped - canceled in any phase, no document.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCompress : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolCompress)

public:
    static constexpr const char* TOOL_ID = "optimize.compress";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PRESET = "preset";
    static constexpr const char* PARAMETER_TARGET_DPI = "targetDpi";
    static constexpr const char* PARAMETER_JPEG_QUALITY = "jpegQuality";
    static constexpr const char* PARAMETER_GOAL = "goal";
    static constexpr const char* PARAMETER_KEEP_ORIGINAL = "keepOriginalIfLarger";
    static constexpr const char* PARAMETER_PRESERVE_TRANSPARENCY = "preserveTransparency";
    static constexpr const char* PARAMETER_CLEAN_STRUCTURE = "cleanStructure";

    /// Identifiers of the presets
    static constexpr const char* PRESET_SMALLER_FILE = "smaller-file";
    static constexpr const char* PRESET_BALANCED = "balanced";
    static constexpr const char* PRESET_BEST_QUALITY = "best-quality";
    static constexpr const char* PRESET_CUSTOM = "custom";

    /// Identifiers of the goals of the custom preset
    static constexpr const char* GOAL_PREFER_QUALITY = "prefer-quality";
    static constexpr const char* GOAL_MINIMUM_SIZE = "minimum-size";

    /// Resolutions offered by the custom panel (294:1340, "Downsample to")
    static std::vector<int> getOfferedResolutions() { return { 72, 96, 110, 150, 200, 300 }; }

    /// What the tool runs
    struct Settings
    {
        PDFImageOptimizer::Settings images;             ///< Images (enabled = false keeps them as they are)
        PDFOptimizer::OptimizationFlags structure;      ///< Steps of the structure optimizer
    };

    /// Numbers of a run of compress
    struct Statistics
    {
        qint64 imagesRecompressed = 0;              ///< Images replaced by a new encoding
        PDFOptimizer::Statistics structure;         ///< Counters of the structure optimizer
        ConformanceRemoval conformance;             ///< The declaration of PDF/A and PDF/UA of the result
    };

    /// Progress of the phases of compress (each may be null)
    struct Progress
    {
        PDFProgressSink* analysis = nullptr;    ///< Walk over the pages, a step per page
        PDFProgressSink* images = nullptr;      ///< Images, a step per image
        PDFProgressSink* structure = nullptr;   ///< Structure, a step per stage and the texts of the stages (PDFProgressSink::addMessage)
    };

    /// What a preset runs: Smaller file 110 dpi, JPEG 60, minimum size; Balanced 200 dpi,
    /// JPEG 80, prefer quality; Best quality keeps the images (owner answer Q2); Custom
    /// the parameters (294:1340). Every preset cleans the structure, Custom as it says.
    /// \param parameters Normalized parameters of the tool
    static Settings createSettings(const QVariantMap& parameters);

    /// Compresses the document: first the images, then the structure, and the declaration of
    /// PDF/A or PDF/UA is removed from the metadata of the result. The source document
    /// is not changed, the result keeps its PDF version (also its source hash).
    /// \param document Source document
    /// \param settings What runs
    /// \param control Cancellation (optional); check it, a canceled run returns an unusable document
    /// \param progress Progress of the phases
    /// \param statistics Numbers of the run (optional)
    static PDFDocument compress(const PDFDocument* document, const Settings& settings, const PDFOperationControl* control, const Progress& progress, Statistics* statistics);

    /// Description of the preset below What changes ("Balanced: images are reduced to
    /// 200 dpi ..."); the one of Custom says its values (294:1340)
    /// \param parameters Normalized parameters of the tool
    static QString getPresetDescription(const QVariantMap& parameters);

    /// Line below the sizes of the card Result of a finished run: "75 % smaller · 18.7 MB
    /// saved" (291:994), "No saving with this preset" (295:2182)
    static QString getSavingText(const PDFDocumentToolResult& result);

    /// Rows of What changes of a finished run ("Images (412)", "21.3 MB → 3.4 MB")
    static std::vector<PDFDocumentToolFact> getChangeRows(const PDFDocumentToolResult& result);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual bool hasPreview() const override { return true; }
    virtual bool isPreviewFinal() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_COMPRESS_H
