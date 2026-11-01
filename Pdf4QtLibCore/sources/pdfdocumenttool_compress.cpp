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

#include "pdfdocumenttool_compress.h"

#include "pdfdocumentsizebreakdown.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"

#include <QElapsedTimer>
#include <QFileInfo>

namespace pdf
{

/// Helpers of the tool Compress
class PDFDocumentToolCompressHelper
{
public:
    PDFDocumentToolCompressHelper() = delete;

    /// Settings of the images of a preset: both profiles of the images, which are
    /// not bi-tonal, have the same resolution and quality (the bi-tonal ones keep
    /// their default - a downsampled scan of a text would lose the text)
    static PDFImageOptimizer::Settings createImageSettings(int targetDpi, int jpegQuality, PDFImageOptimizer::OptimizationGoal goal)
    {
        PDFImageOptimizer::Settings settings = PDFImageOptimizer::Settings::createDefault();
        settings.enabled = true;
        settings.goal = goal;

        // The quality the user sees is the quality used (owner answer Q2, chapter 12 of the plan)
        settings.exactJpegQuality = true;
        settings.colorProfile.targetDpi = targetDpi;
        settings.grayProfile.targetDpi = targetDpi;
        settings.colorProfile.jpegQuality = jpegQuality;
        settings.grayProfile.jpegQuality = jpegQuality;
        return settings;
    }

    /// Name of a parameter
    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Name of a signature impact in JSON
    static QString getSignatureImpactName(PDFDocumentToolSignatureImpact impact)
    {
        switch (impact)
        {
            case PDFDocumentToolSignatureImpact::None:
                return QStringLiteral("none");
            case PDFDocumentToolSignatureImpact::IncrementalSave:
                return QStringLiteral("incremental-save");
            case PDFDocumentToolSignatureImpact::LostCopyOnly:
                return QStringLiteral("lost-copy-only");
        }
        return QString();
    }
};

PDFDocumentToolCompress::Settings PDFDocumentToolCompress::createSettings(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolCompressHelper;
    using Goal = PDFImageOptimizer::OptimizationGoal;

    Settings settings;
    settings.structure = PDFOptimizer::All;

    const QString preset = parameters.value(Helper::name(PARAMETER_PRESET)).toString();
    if (preset == QLatin1String(PRESET_SMALLER_FILE))
    {
        settings.images = Helper::createImageSettings(110, 60, Goal::MinimumSize);
    }
    else if (preset == QLatin1String(PRESET_BEST_QUALITY))
    {
        settings.images = PDFImageOptimizer::Settings::createDefault();
        settings.images.enabled = false;
    }
    else if (preset == QLatin1String(PRESET_CUSTOM))
    {
        const bool isMinimumSize = parameters.value(Helper::name(PARAMETER_GOAL)).toString() == QLatin1String(GOAL_MINIMUM_SIZE);
        settings.images = Helper::createImageSettings(parameters.value(Helper::name(PARAMETER_TARGET_DPI)).toInt(),
                                                      parameters.value(Helper::name(PARAMETER_JPEG_QUALITY)).toInt(),
                                                      isMinimumSize ? Goal::MinimumSize : Goal::PreferQuality);
        settings.images.keepOriginalIfLarger = parameters.value(Helper::name(PARAMETER_KEEP_ORIGINAL)).toBool();
        settings.images.preserveTransparency = parameters.value(Helper::name(PARAMETER_PRESERVE_TRANSPARENCY)).toBool();
        settings.structure = parameters.value(Helper::name(PARAMETER_CLEAN_STRUCTURE)).toBool() ? PDFOptimizer::All : PDFOptimizer::None;
    }
    else
    {
        settings.images = Helper::createImageSettings(200, 80, Goal::PreferQuality);
    }

    return settings;
}

PDFDocument PDFDocumentToolCompress::compress(const PDFDocument* document, const Settings& settings, const PDFOperationControl* control, const Progress& progress, Statistics* statistics)
{
    Statistics localStatistics;
    PDFDocument output;

    if (settings.images.enabled)
    {
        std::vector<PDFImageOptimizer::ImageResult> imageResults;
        PDFImageOptimizer imageOptimizer;
        output = imageOptimizer.optimize(document, settings.images, PDFImageOptimizer::ImageOverrides(), progress.images, nullptr, &imageResults, control, progress.analysis);

        for (const PDFImageOptimizer::ImageResult& imageResult : imageResults)
        {
            localStatistics.imagesRecompressed += imageResult.keptOriginal ? 0 : 1;
        }
    }

    const PDFDocument* source = settings.images.enabled ? &output : document;
    PDFObjectStorage storage = source->getStorage();

    if (settings.structure && !PDFOperationControl::isOperationCancelled(control))
    {
        // A local optimizer without a parent: the optimizer has a state, one instance
        // of the tool serves more threads
        PDFOptimizer optimizer(settings.structure, nullptr);
        optimizer.setOperationControl(control);
        optimizer.setProgress(progress.structure);
        optimizer.setStorage(storage);
        optimizer.optimize();
        localStatistics.structure = optimizer.getStatistics();
        storage = optimizer.takeStorage();
    }

    if (statistics)
    {
        *statistics = localStatistics;
    }

    // The optimizers would report PDF 2.0: a file does not change its version by being compressed
    return PDFDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash());
}

QString PDFDocumentToolCompress::getPresetDescription(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolCompressHelper;

    const QString preset = parameters.value(Helper::name(PARAMETER_PRESET)).toString();
    if (preset == QLatin1String(PRESET_SMALLER_FILE))
    {
        return tr("Smaller file: images are reduced to screen resolution with stronger JPEG compression, unused objects are removed, identical objects merged and streams recompressed.");
    }
    if (preset == QLatin1String(PRESET_BEST_QUALITY))
    {
        return tr("Best quality: images are kept as they are; only the lossless structure clean-up runs.");
    }
    if (preset == QLatin1String(PRESET_CUSTOM))
    {
        const bool isMinimumSize = parameters.value(Helper::name(PARAMETER_GOAL)).toString() == QLatin1String(GOAL_MINIMUM_SIZE);
        const bool isStructure = parameters.value(Helper::name(PARAMETER_CLEAN_STRUCTURE)).toBool();
        const QString dpi = QString::number(parameters.value(Helper::name(PARAMETER_TARGET_DPI)).toInt());
        const QString quality = QString::number(parameters.value(Helper::name(PARAMETER_JPEG_QUALITY)).toInt());
        if (isMinimumSize)
        {
            return isStructure ? tr("Custom: images at %1 dpi, JPEG quality %2, minimum size preferred. Structure clean-up is on.").arg(dpi, quality)
                               : tr("Custom: images at %1 dpi, JPEG quality %2, minimum size preferred. Structure clean-up is off.").arg(dpi, quality);
        }
        return isStructure ? tr("Custom: images at %1 dpi, JPEG quality %2, quality preferred. Structure clean-up is on.").arg(dpi, quality)
                           : tr("Custom: images at %1 dpi, JPEG quality %2, quality preferred. Structure clean-up is off.").arg(dpi, quality);
    }
    return tr("Balanced: images are reduced to 200 dpi where no difference is visible, unused objects are removed, identical objects merged and streams recompressed.");
}

QString PDFDocumentToolCompress::getSavingText(const PDFDocumentToolResult& result)
{
    const qint64 savedBytes = result.data.value(QStringLiteral("savedBytes")).toInteger();
    if (result.status != PDFDocumentToolResultStatus::Success || savedBytes <= 0)
    {
        return tr("No saving with this preset");
    }
    return tr("%1 % smaller · %2 saved").arg(result.data.value(QStringLiteral("savedPercent")).toInt()).arg(PDFDocumentToolFormat::formatSize(savedBytes));
}

std::vector<PDFDocumentToolFact> PDFDocumentToolCompress::getChangeRows(const PDFDocumentToolResult& result)
{
    return PDFDocumentSizeBreakdown::createChangeRows(PDFDocumentSizeBreakdown::fromJson(result.data.value(QStringLiteral("breakdownBefore")).toObject()),
                                                      PDFDocumentSizeBreakdown::fromJson(result.data.value(QStringLiteral("breakdownAfter")).toObject()));
}

QString PDFDocumentToolCompress::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolCompress::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolCompressHelper;

    QVariantMap presetParameters;

    const auto getDescription = [&presetParameters](const char* preset)
    {
        presetParameters.insert(Helper::name(PARAMETER_PRESET), QString::fromLatin1(preset));
        return getPresetDescription(presetParameters);
    };

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createEnum(Helper::name(PARAMETER_PRESET), tr("Preset"), tr("What the tool does with the images and the structure of the file"),
                                                 {
                                                     { QString::fromLatin1(PRESET_SMALLER_FILE), tr("Smaller file"), getDescription(PRESET_SMALLER_FILE), QString() },
                                                     { QString::fromLatin1(PRESET_BALANCED), tr("Balanced"), getDescription(PRESET_BALANCED), QString() },
                                                     { QString::fromLatin1(PRESET_BEST_QUALITY), tr("Best quality"), getDescription(PRESET_BEST_QUALITY), QString() },
                                                     { QString::fromLatin1(PRESET_CUSTOM), tr("Custom…"), tr("The values of the other parameters"), QString() },
                                                 },
                                                 QString::fromLatin1(PRESET_BALANCED)));
    definitions.push_back(Definition::createInteger(Helper::name(PARAMETER_TARGET_DPI), tr("Downsample to"), tr("Resolution of the images in dpi (the preset Custom only)"), 150, 36, 1200));
    definitions.push_back(Definition::createInteger(Helper::name(PARAMETER_JPEG_QUALITY), tr("JPEG quality"), tr("Quality of the JPEG images, 1 to 100 (the preset Custom only)"), 75, 1, 100));
    definitions.push_back(Definition::createEnum(Helper::name(PARAMETER_GOAL), tr("Goal"), tr("What the optimizer prefers (the preset Custom only)"),
                                                 {
                                                     { QString::fromLatin1(GOAL_PREFER_QUALITY), tr("Prefer quality"), QString(), QString() },
                                                     { QString::fromLatin1(GOAL_MINIMUM_SIZE), tr("Minimum size"), QString(), QString() },
                                                 },
                                                 QString::fromLatin1(GOAL_MINIMUM_SIZE)));
    definitions.push_back(Definition::createBool(Helper::name(PARAMETER_KEEP_ORIGINAL), tr("Keep original image if the new one is larger"), tr("The preset Custom only"), true));
    definitions.push_back(Definition::createBool(Helper::name(PARAMETER_PRESERVE_TRANSPARENCY), tr("Preserve transparency"), tr("The preset Custom only"), true));
    definitions.push_back(Definition::createBool(Helper::name(PARAMETER_CLEAN_STRUCTURE), tr("Clean up and recompress the file structure"), tr("Lossless – the pages look exactly the same. The preset Custom only"), true));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolCompress::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const
{
    return execute(context, parameters, control, progress);
}

PDFDocumentToolResult PDFDocumentToolCompress::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const
{
    using Helper = PDFDocumentToolCompressHelper;

    QElapsedTimer timer;
    timer.start();

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const Settings settings = createSettings(parameters);

    // The size of the file the user sees; a document with unsaved changes has no such
    // file, so its size is measured as the size of the result is
    const qint64 sizeBefore = !input.originalData.isEmpty() && !input.isModified ? qint64(input.originalData.size()) : PDFDocumentWriter::getDocumentFileSize(document);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    // One bar over the phases; the weights are the usual shares of the time
    PDFProgressPhases phases(progress, tr("Compressing…"));
    const int analysisPhase = settings.images.enabled ? phases.addPhase(5, tr("Looking for images"), false) : -1;
    const int imagesPhase = settings.images.enabled ? phases.addPhase(65, tr("Recompressing images"), true) : -1;
    const int structurePhase = settings.structure ? phases.addPhase(20, tr("Cleaning up the structure"), false) : -1;
    const int measurePhase = phases.addPhase(10, tr("Measuring the result"), false);
    phases.start();

    Progress compressProgress;
    compressProgress.analysis = phases.getPhase(analysisPhase);
    compressProgress.images = phases.getPhase(imagesPhase);
    compressProgress.structure = phases.getPhase(structurePhase);

    Statistics statistics;
    PDFDocument output = compress(document, settings, control, compressProgress, &statistics);

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    phases.beginPhase(measurePhase);

    const qint64 sizeAfter = PDFDocumentWriter::getDocumentFileSize(&output);
    if (sizeAfter < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The result cannot be written."));
    }
    if (sizeAfter > MAXIMUM_RESULT_SIZE)
    {
        return PDFDocumentToolResult::createFailed(tr("The result is too large to be saved."));
    }

    const PDFDocumentSizeBreakdown breakdownBefore = PDFDocumentSizeBreakdown::create(document, sizeBefore, control);
    const PDFDocumentSizeBreakdown breakdownAfter = PDFDocumentSizeBreakdown::create(&output, sizeAfter, control);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    phases.finish();

    const qint64 duration = timer.elapsed();
    const qint64 savedBytes = qMax(qint64(0), sizeBefore - sizeAfter);
    const int savedPercent = PDFDocumentToolFormat::getSavedPercent(sizeBefore, sizeAfter);
    const qint64 imagesKept = qMax(qint64(0), breakdownBefore.imageCount - statistics.imagesRecompressed);

    PDFDocumentToolResult result;
    if (sizeAfter >= sizeBefore)
    {
        result = PDFDocumentToolResult::createNothingToDo(tr("File cannot be reduced with these settings"));
    }

    const QString sizeValue = sizeAfter < sizeBefore ? tr("%1 (%2 % smaller)").arg(PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter)).arg(savedPercent)
                                                     : PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter);
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), sizeValue });
    result.facts.push_back({ QStringLiteral("images"), tr("Images"), tr("%1 · %2").arg(tr("%Ln recompressed", nullptr, int(statistics.imagesRecompressed)), tr("%Ln kept", nullptr, int(imagesKept))) });
    result.facts.push_back({ QStringLiteral("time"), tr("Time"), PDFDocumentToolFormat::formatDuration(duration) });

    result.data.insert(QStringLiteral("preset"), parameters.value(Helper::name(PARAMETER_PRESET)).toString());
    result.data.insert(QStringLiteral("sizeBefore"), sizeBefore);
    result.data.insert(QStringLiteral("sizeAfter"), sizeAfter);
    result.data.insert(QStringLiteral("savedBytes"), savedBytes);
    result.data.insert(QStringLiteral("savedPercent"), savedPercent);
    result.data.insert(QStringLiteral("imagesRecompressed"), statistics.imagesRecompressed);
    result.data.insert(QStringLiteral("imagesKept"), imagesKept);
    result.data.insert(QStringLiteral("breakdownBefore"), breakdownBefore.toJson());
    result.data.insert(QStringLiteral("breakdownAfter"), breakdownAfter.toJson());
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);

    const PDFDocumentToolCatalogEntry* info = getInfo();
    result.data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(document));
    result.data.insert(QStringLiteral("signatureImpact"), Helper::getSignatureImpactName(info ? info->getSignatureImpact() : PDFDocumentToolSignatureImpact::None));

    if (sizeAfter < sizeBefore)
    {
        PDFDocumentToolOutputDocument outputDocument;
        outputDocument.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        outputDocument.inputIndex = 0;
        outputDocument.document = PDFDocumentPointer(new PDFDocument(std::move(output)));
        outputDocument.flags = PDFModifiedDocument::Reset;

        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        outputDocument.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_compressed.pdf");
        result.documents.push_back(std::move(outputDocument));
    }

    return result;
}

}   // namespace pdf
