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
#include "pdfoperationcontrol.h"

#include <QElapsedTimer>
#include <QJsonArray>

namespace pdf
{

/// Helpers of the tool Compress
class PDFDocumentToolCompressHelper
{
public:
    PDFDocumentToolCompressHelper() = delete;

    // The descriptions of the presets: below What changes of the panel translated
    // (getPresetDescription), in the schema of the assistant in English
    static constexpr const char* DESCRIPTION_SMALLER_FILE = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCompress", "Smaller file: images are reduced to screen resolution with stronger JPEG compression, unused objects are removed, identical objects merged and streams recompressed.");
    static constexpr const char* DESCRIPTION_BALANCED = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCompress", "Balanced: images are reduced to 200 dpi where no difference is visible, unused objects are removed, identical objects merged and streams recompressed.");
    static constexpr const char* DESCRIPTION_BEST_QUALITY = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCompress", "Best quality: images are kept as they are; only the lossless structure clean-up runs.");

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
        if (progress.structureMessage)
        {
            QObject::connect(&optimizer, &PDFOptimizer::optimizationProgress, &optimizer, progress.structureMessage, Qt::DirectConnection);
        }
        optimizer.setStorage(storage);
        optimizer.optimize();
        localStatistics.structure = optimizer.getStatistics();
        storage = optimizer.takeStorage();
    }

    // After the optimizer: its counters of the unused objects stay the same with the clean-up on
    // The content has been rewritten and it is not checked against a standard the document
    // declares (a recoded image can be against it, the rule of the copies of OCR)
    PDFDocument result = createWholeFileDocument(std::move(storage), document, PDFOperationControl::isOperationCancelled(control) ? nullptr : &localStatistics.conformance);

    if (statistics)
    {
        *statistics = localStatistics;
    }
    return result;
}

QString PDFDocumentToolCompress::getPresetDescription(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolCompressHelper;

    const QString preset = parameters.value(Helper::name(PARAMETER_PRESET)).toString();
    if (preset == QLatin1String(PRESET_SMALLER_FILE))
    {
        return tr(Helper::DESCRIPTION_SMALLER_FILE);
    }
    if (preset == QLatin1String(PRESET_BEST_QUALITY))
    {
        return tr(Helper::DESCRIPTION_BEST_QUALITY);
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
    return tr(Helper::DESCRIPTION_BALANCED);
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
    std::vector<PDFDocumentToolFact> rows = PDFDocumentSizeBreakdown::createChangeRows(PDFDocumentSizeBreakdown::fromJson(result.data.value(QStringLiteral("breakdownBefore")).toObject()),
                                                                                       PDFDocumentSizeBreakdown::fromJson(result.data.value(QStringLiteral("breakdownAfter")).toObject()));

    // The file of the result does not say it conforms any more: the user reads it also before the decision
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("conformance"))
        {
            rows.push_back(fact);
        }
    }
    return rows;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolCompress::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolCompressHelper;

    // A value of Custom with another preset would be ignored, the assistant would report a result it did not get
    std::vector<PDFDocumentToolParameterError> errors;
    if (given.value(Helper::name(PARAMETER_PRESET)).toString() == QLatin1String(PRESET_CUSTOM))
    {
        return errors;
    }

    for (const char* parameter : { PARAMETER_TARGET_DPI, PARAMETER_JPEG_QUALITY, PARAMETER_GOAL, PARAMETER_KEEP_ORIGINAL, PARAMETER_PRESERVE_TRANSPARENCY, PARAMETER_CLEAN_STRUCTURE })
    {
        const QString name = Helper::name(parameter);
        if (given.contains(name))
        {
            errors.push_back({ name, tr("%1 is used only with the preset custom: set preset to custom, or leave %1 out.").arg(name) });
        }
    }
    return errors;
}

QString PDFDocumentToolCompress::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolCompress::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolCompressHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_PRESET), QT_TR_NOOP("Preset"), QT_TR_NOOP("What the tool does with the images and the structure of the file"),
                                                 {
                                                     { QString::fromLatin1(PRESET_SMALLER_FILE), QT_TR_NOOP("Smaller file"), Helper::DESCRIPTION_SMALLER_FILE },
                                                     { QString::fromLatin1(PRESET_BALANCED), QT_TR_NOOP("Balanced"), Helper::DESCRIPTION_BALANCED },
                                                     { QString::fromLatin1(PRESET_BEST_QUALITY), QT_TR_NOOP("Best quality"), Helper::DESCRIPTION_BEST_QUALITY },
                                                     { QString::fromLatin1(PRESET_CUSTOM), QT_TR_NOOP("Custom…"), QT_TR_NOOP("The values of the other parameters") },
                                                 },
                                                 QString::fromLatin1(PRESET_BALANCED)));
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_TARGET_DPI), QT_TR_NOOP("Downsample to"), QT_TR_NOOP("Resolution of the images in dpi (the preset Custom only)"), 150, 36, 1200));
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_JPEG_QUALITY), QT_TR_NOOP("JPEG quality"), QT_TR_NOOP("Quality of the JPEG images, 1 to 100 (the preset Custom only)"), 75, 1, 100));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_GOAL), QT_TR_NOOP("Goal"), QT_TR_NOOP("What the optimizer prefers (the preset Custom only)"),
                                                 {
                                                     { QString::fromLatin1(GOAL_PREFER_QUALITY), QT_TR_NOOP("Prefer quality") },
                                                     { QString::fromLatin1(GOAL_MINIMUM_SIZE), QT_TR_NOOP("Minimum size") },
                                                 },
                                                 QString::fromLatin1(GOAL_MINIMUM_SIZE)));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_KEEP_ORIGINAL), QT_TR_NOOP("Keep original image if the new one is larger"), QT_TR_NOOP("The preset Custom only"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_PRESERVE_TRANSPARENCY), QT_TR_NOOP("Preserve transparency"), QT_TR_NOOP("The preset Custom only"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_CLEAN_STRUCTURE), QT_TR_NOOP("Clean up and recompress the file structure"), QT_TR_NOOP("Lossless – the pages look exactly the same. The preset Custom only"), true));
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

    const qint64 sizeBefore = getWholeFileInputSize(input);
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

    // Never a result, which still claims a standard it was not checked against
    if (!statistics.conformance.kept.isEmpty())
    {
        return createConformanceFailure(statistics.conformance.kept);
    }

    phases.beginPhase(measurePhase);

    const WholeFileSize outputSize = getWholeFileOutputSize(&output);
    if (!outputSize.error.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(outputSize.error);
    }
    const qint64 sizeAfter = outputSize.size;

    const PDFDocumentSizeBreakdown breakdownBefore = PDFDocumentSizeBreakdown::create(document, sizeBefore, control);
    const PDFDocumentSizeBreakdown breakdownAfter = PDFDocumentSizeBreakdown::create(&output, sizeAfter, control);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    phases.finish();

    const qint64 duration = timer.elapsed();
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

    // Only a result, which exists, has lost its declaration
    if (sizeAfter < sizeBefore)
    {
        addConformanceFact(&result, statistics.conformance.removed);
    }

    result.data.insert(QStringLiteral("preset"), parameters.value(Helper::name(PARAMETER_PRESET)).toString());
    result.data.insert(QStringLiteral("imagesRecompressed"), statistics.imagesRecompressed);
    result.data.insert(QStringLiteral("imagesKept"), imagesKept);
    result.data.insert(QStringLiteral("breakdownBefore"), breakdownBefore.toJson());
    result.data.insert(QStringLiteral("breakdownAfter"), breakdownAfter.toJson());
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);

    finishWholeFileResult(&result, input, std::move(output), sizeBefore, sizeAfter, QStringLiteral("_compressed"));
    return result;
}

}   // namespace pdf
