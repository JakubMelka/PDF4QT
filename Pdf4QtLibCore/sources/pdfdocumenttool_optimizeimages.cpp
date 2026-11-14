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

#include "pdfdocumenttool_optimizeimages.h"

#include "pdfoperationcontrol.h"

#include <QElapsedTimer>
#include <QJsonArray>

#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the tools Optimize Images and Convert Images to Grayscale
class PDFDocumentToolOptimizeImagesHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolOptimizeImages)

public:
    PDFDocumentToolOptimizeImagesHelper() = delete;

    using Tool = PDFDocumentToolOptimizeImages;

    /// The counts and the bytes of the facts
    struct Counts
    {
        qint64 recompressed = 0;
        qint64 kept = 0;            ///< Kept: the new encoding was not smaller, an inline image
        qint64 excluded = 0;
        qint64 skipped = 0;         ///< Grayscale: gray or bitonal by the declaration
        qint64 problems = 0;        ///< Too large, cannot be read
        qint64 bytesBefore = 0;     ///< Bytes of the counted images (Grayscale: the color images)
        qint64 bytesAfter = 0;

        void add(const QString& status, bool isCounted, qint64 originalBytes, qint64 newBytes)
        {
            if (status == QLatin1String(Tool::IMAGE_STATUS_RECOMPRESSED))
            {
                ++recompressed;
            }
            else if (status == QLatin1String(Tool::IMAGE_STATUS_EXCLUDED))
            {
                ++excluded;
            }
            else if (status == QLatin1String(Tool::IMAGE_STATUS_SKIPPED))
            {
                ++skipped;
            }
            else if (status == QLatin1String(Tool::IMAGE_STATUS_TOO_LARGE) || status == QLatin1String(Tool::IMAGE_STATUS_CANNOT_BE_READ))
            {
                ++problems;
            }
            else
            {
                ++kept;
            }

            if (isCounted)
            {
                bytesBefore += originalBytes;
                bytesAfter += newBytes;
            }
        }
    };

    /// The image declares a spot color (ISO 32000-2, 8.6.6.4 Separation, 8.6.6.5 DeviceN, also as
    /// the base of Indexed): a new encoding is written in a device space, the colorant is gone
    static bool isSpotColorImage(const PDFDocument* document, const PDFDocumentImageList::Item& item)
    {
        const auto isSpotFamily = [](const QString& family) { return family == QLatin1String("Separation") || family == QLatin1String("DeviceN"); };
        if (isSpotFamily(item.colorSpaceName))
        {
            return true;
        }
        if (item.colorSpaceName != QLatin1String("Indexed") || item.key.isInline())
        {
            return false;
        }

        // [/Indexed base hival lookup] (8.6.6.3): the family of the base
        const PDFObject& object = document->getObjectByReference(item.key.reference);
        const PDFObject& colorSpace = object.isStream() ? document->getObject(object.getStream()->getDictionary()->get("ColorSpace")) : PDFObject();
        if (!colorSpace.isArray() || colorSpace.getArray()->getCount() < 2)
        {
            return false;
        }
        const PDFObject& base = document->getObject(colorSpace.getArray()->getItem(1));
        const PDFObject& family = base.isArray() && base.getArray()->getCount() > 0 ? document->getObject(base.getArray()->getItem(0)) : base;
        return family.isName() && isSpotFamily(QString::fromLatin1(family.getString()));
    }

    /// The status of an image XObject by its result of the optimizer
    static QString getStatus(const PDFImageOptimizer::ImageResult* result)
    {
        if (!result)
        {
            return QString::fromLatin1(Tool::IMAGE_STATUS_KEPT);
        }

        switch (result->skipReason)
        {
            case PDFImageOptimizer::SkipReason::NotColorImage:
                return QString::fromLatin1(Tool::IMAGE_STATUS_SKIPPED);
            case PDFImageOptimizer::SkipReason::Excluded:
                return QString::fromLatin1(Tool::IMAGE_STATUS_EXCLUDED);
            case PDFImageOptimizer::SkipReason::TooLarge:
                return QString::fromLatin1(Tool::IMAGE_STATUS_TOO_LARGE);
            case PDFImageOptimizer::SkipReason::CannotBeRead:
                return QString::fromLatin1(Tool::IMAGE_STATUS_CANNOT_BE_READ);
            case PDFImageOptimizer::SkipReason::MaskOfAnotherImage:
                // B62: the soft mask of another image keeps its stream
                return QString::fromLatin1(Tool::IMAGE_STATUS_KEPT);
            case PDFImageOptimizer::SkipReason::InAppearance:
                // R4-4: an image of an appearance of an annotation keeps its stream
                return QString::fromLatin1(Tool::IMAGE_STATUS_KEPT);
            case PDFImageOptimizer::SkipReason::None:
                break;
        }
        return QString::fromLatin1(result->keptOriginal ? Tool::IMAGE_STATUS_KEPT : Tool::IMAGE_STATUS_RECOMPRESSED);
    }

    /// The text of a run, which changes no image
    static QString getNothingToDoText(Tool::Purpose purpose, const Counts& counts)
    {
        if (purpose == Tool::Purpose::Grayscale)
        {
            if (counts.excluded > 0)
            {
                return tr("No color image is converted: the others are excluded or already gray.");
            }
            return tr("The document has no color images to convert.");
        }
        if (counts.recompressed + counts.kept + counts.excluded + counts.skipped + counts.problems == 0)
        {
            return tr("The document has no images.");
        }
        return tr("Images cannot be reduced with these settings");
    }

    /// The options shared by both tools (Goal, Keep original if larger, Preserve transparency)
    static PDFImageOptimizer::Settings createOptionSettings(const QVariantMap& parameters)
    {
        PDFImageOptimizer::Settings settings = PDFImageOptimizer::Settings::createDefault();
        settings.enabled = true;

        // The quality the user sees is the quality used (as Compress): "JPEG 85" of AFTER is 85 in the file
        settings.exactJpegQuality = true;
        settings.goal = parameters.value(QString::fromLatin1(Tool::PARAMETER_GOAL)).toString() == QLatin1String(Tool::GOAL_MINIMUM_SIZE) ? PDFImageOptimizer::OptimizationGoal::MinimumSize
                                                                                                                                       : PDFImageOptimizer::OptimizationGoal::PreferQuality;
        settings.keepOriginalIfLarger = parameters.value(QString::fromLatin1(Tool::PARAMETER_KEEP_ORIGINAL), true).toBool();
        settings.preserveTransparency = parameters.value(QString::fromLatin1(Tool::PARAMETER_PRESERVE_TRANSPARENCY), true).toBool();
        return settings;
    }

    static PDFImageOptimizer::ColorMode getColorMode(const QString& id)
    {
        if (id == QLatin1String(Tool::COLOR_PRESERVE))
        {
            return PDFImageOptimizer::ColorMode::Preserve;
        }
        if (id == QLatin1String(Tool::COLOR_COLOR))
        {
            return PDFImageOptimizer::ColorMode::Color;
        }
        if (id == QLatin1String(Tool::COLOR_GRAYSCALE))
        {
            return PDFImageOptimizer::ColorMode::Grayscale;
        }
        if (id == QLatin1String(Tool::COLOR_BITONAL))
        {
            return PDFImageOptimizer::ColorMode::Bitonal;
        }
        return PDFImageOptimizer::ColorMode::Auto;
    }

    /// The algorithm of an id; std::nullopt for an unknown id
    static std::optional<PDFImageOptimizer::CompressionAlgorithm> getAlgorithm(const QString& id)
    {
        using Algorithm = PDFImageOptimizer::CompressionAlgorithm;
        static const std::map<QString, Algorithm> algorithms =
        {
            { QString::fromLatin1(Tool::ALGORITHM_AUTO), Algorithm::Auto },
            { QString::fromLatin1(Tool::ALGORITHM_FLATE), Algorithm::Flate },
            { QString::fromLatin1(Tool::ALGORITHM_JPEG), Algorithm::JPEG },
            { QString::fromLatin1(Tool::ALGORITHM_JPEG2000), Algorithm::JPEG2000 },
            { QString::fromLatin1(Tool::ALGORITHM_RUN_LENGTH), Algorithm::RunLength },
            { QString::fromLatin1(Tool::ALGORITHM_CCITT_G4), Algorithm::CCITTGroup4 },
            { QString::fromLatin1(Tool::ALGORITHM_JBIG2), Algorithm::JBIG2 },
        };
        auto it = algorithms.find(id);
        return it != algorithms.end() ? std::optional<Algorithm>(it->second) : std::nullopt;
    }

    /// The filter of an id; std::nullopt for an unknown id
    static std::optional<PDFImage::ResampleFilter> getResampleFilter(const QString& id)
    {
        if (id == QLatin1String(Tool::RESAMPLE_NEAREST))
        {
            return PDFImage::ResampleFilter::Nearest;
        }
        if (id == QLatin1String(Tool::RESAMPLE_BILINEAR))
        {
            return PDFImage::ResampleFilter::Bilinear;
        }
        if (id == QLatin1String(Tool::RESAMPLE_BICUBIC))
        {
            return PDFImage::ResampleFilter::Bicubic;
        }
        if (id == QLatin1String(Tool::RESAMPLE_LANCZOS))
        {
            return PDFImage::ResampleFilter::Lanczos;
        }
        return std::nullopt;
    }

    /// A field of a profile of an exception (the value is valid, readExceptionSettings has checked it)
    static void applyField(PDFImageOptimizer::CompressionProfile* profile, const QString& field, const QVariant& value)
    {
        if (field == QLatin1String(Tool::EXCEPTION_ALGORITHM))
        {
            profile->algorithm = getAlgorithm(value.toString()).value_or(PDFImageOptimizer::CompressionAlgorithm::Auto);
        }
        else if (field == QLatin1String(Tool::EXCEPTION_TARGET_DPI))
        {
            profile->targetDpi = value.toInt();
        }
        else if (field == QLatin1String(Tool::EXCEPTION_RESAMPLE))
        {
            profile->resampleFilter = getResampleFilter(value.toString()).value_or(PDFImage::ResampleFilter::Bicubic);
        }
        else if (field == QLatin1String(Tool::EXCEPTION_JPEG_QUALITY))
        {
            profile->jpegQuality = value.toInt();
        }
        else if (field == QLatin1String(Tool::EXCEPTION_JPEG2000_RATE))
        {
            profile->jpeg2000Rate = float(value.toDouble());
        }
        else if (field == QLatin1String(Tool::EXCEPTION_PNG_PREDICTOR))
        {
            profile->enablePngPredictor = value.toBool();
        }
        else if (field == QLatin1String(Tool::EXCEPTION_THRESHOLD))
        {
            profile->monochromeThreshold = value.toInt();
        }
    }

    /// One profile of the parameters (\p profile nullptr = the profile of Grayscale without a prefix)
    static PDFImageOptimizer::CompressionProfile readProfile(const QVariantMap& parameters, const char* profile, PDFImageOptimizer::CompressionProfile result)
    {
        const auto value = [&parameters, profile](const char* field) { return parameters.value(Tool::getProfileParameterName(profile, field)); };

        if (const QVariant algorithm = value(Tool::FIELD_ALGORITHM); algorithm.isValid())
        {
            result.algorithm = getAlgorithm(algorithm.toString()).value_or(PDFImageOptimizer::CompressionAlgorithm::Auto);
        }
        if (const QVariant dpi = value(Tool::FIELD_TARGET_DPI); dpi.isValid())
        {
            result.targetDpi = dpi.toInt();
        }
        if (const QVariant resample = value(Tool::FIELD_RESAMPLE); resample.isValid())
        {
            result.resampleFilter = getResampleFilter(resample.toString()).value_or(PDFImage::ResampleFilter::Bicubic);
        }
        if (const QVariant quality = value(Tool::FIELD_JPEG_QUALITY); quality.isValid())
        {
            result.jpegQuality = quality.toInt();
        }
        if (const QVariant rate = value(Tool::FIELD_JPEG2000_RATE); rate.isValid())
        {
            result.jpeg2000Rate = float(rate.toDouble());
        }
        if (const QVariant predictor = value(Tool::FIELD_PNG_PREDICTOR); predictor.isValid())
        {
            result.enablePngPredictor = predictor.toBool();
        }
        return result;
    }

    /// Reads an integer of an exception in its bounds
    static bool readInteger(const QString& name, const QString& text, int minimum, int maximum, QVariantMap* settings, QString* errorMessage)
    {
        bool isOk = false;
        const int value = text.toInt(&isOk);
        if (!isOk || value < minimum || value > maximum)
        {
            *errorMessage = tr("%1 must be a whole number from %2 to %3.").arg(name).arg(minimum).arg(maximum);
            return false;
        }
        settings->insert(name, value);
        return true;
    }

    /// Reads the settings "name=value,..." of an exception
    static bool readExceptionSettings(const QString& text, QVariantMap* settings, QString* errorMessage)
    {
        const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.isEmpty())
        {
            *errorMessage = tr("write \"off\" or the settings \"name=value,...\".");
            return false;
        }

        for (const QString& part : parts)
        {
            const qsizetype equals = part.indexOf(QLatin1Char('='));
            const QString name = part.left(equals).trimmed();
            const QString value = equals > 0 ? part.mid(equals + 1).trimmed() : QString();
            if (equals <= 0 || value.isEmpty())
            {
                *errorMessage = tr("\"%1\" is no setting \"name=value\".").arg(part.trimmed());
                return false;
            }

            if (name == QLatin1String(Tool::EXCEPTION_MODE))
            {
                if (value != QLatin1String(Tool::MODE_AUTO) && value != QLatin1String(Tool::MODE_CUSTOM))
                {
                    *errorMessage = tr("mode must be auto or custom.");
                    return false;
                }
                settings->insert(name, value);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_GOAL))
            {
                if (value != QLatin1String(Tool::GOAL_PREFER_QUALITY) && value != QLatin1String(Tool::GOAL_MINIMUM_SIZE))
                {
                    *errorMessage = tr("goal must be prefer-quality or minimum-size.");
                    return false;
                }
                settings->insert(name, value);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_KEEP_ORIGINAL) || name == QLatin1String(Tool::EXCEPTION_PRESERVE_TRANSPARENCY))
            {
                if (value != QLatin1String("true") && value != QLatin1String("false"))
                {
                    *errorMessage = tr("%1 must be true or false.").arg(name);
                    return false;
                }
                settings->insert(name, value == QLatin1String("true"));
            }
            else if (name == QLatin1String(Tool::EXCEPTION_COLOR_MODE))
            {
                const QStringList colorModes = { QString::fromLatin1(Tool::COLOR_AUTO), QString::fromLatin1(Tool::COLOR_PRESERVE), QString::fromLatin1(Tool::COLOR_COLOR),
                                                 QString::fromLatin1(Tool::COLOR_GRAYSCALE), QString::fromLatin1(Tool::COLOR_BITONAL) };
                if (!colorModes.contains(value))
                {
                    *errorMessage = tr("colorMode must be one of %1.").arg(colorModes.join(QStringLiteral(", ")));
                    return false;
                }
                settings->insert(name, value);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_ALGORITHM))
            {
                if (!getAlgorithm(value))
                {
                    *errorMessage = tr("algorithm must be one of auto, flate, jpeg, jpeg2000, runlength, ccitt-g4, jbig2.");
                    return false;
                }
                settings->insert(name, value);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_RESAMPLE))
            {
                if (!getResampleFilter(value))
                {
                    *errorMessage = tr("resample must be one of nearest, bilinear, bicubic, lanczos.");
                    return false;
                }
                settings->insert(name, value);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_TARGET_DPI))
            {
                if (!readInteger(name, value, 0, Tool::MAXIMUM_TARGET_DPI, settings, errorMessage))
                {
                    return false;
                }
            }
            else if (name == QLatin1String(Tool::EXCEPTION_JPEG_QUALITY))
            {
                if (!readInteger(name, value, 0, 100, settings, errorMessage))
                {
                    return false;
                }
            }
            else if (name == QLatin1String(Tool::EXCEPTION_JPEG2000_RATE))
            {
                bool isOk = false;
                const double rate = value.toDouble(&isOk);
                if (!isOk || !(rate >= 0.0 && rate <= 1000.0))
                {
                    *errorMessage = tr("jpeg2000Rate must be a number from 0 to 1000.");
                    return false;
                }
                settings->insert(name, rate);
            }
            else if (name == QLatin1String(Tool::EXCEPTION_PNG_PREDICTOR))
            {
                if (value != QLatin1String("true") && value != QLatin1String("false"))
                {
                    *errorMessage = tr("pngPredictor must be true or false.");
                    return false;
                }
                settings->insert(name, value == QLatin1String("true"));
            }
            else if (name == QLatin1String(Tool::EXCEPTION_THRESHOLD))
            {
                if (value == QLatin1String(Tool::THRESHOLD_AUTO))
                {
                    settings->insert(name, -1);
                }
                else if (!readInteger(name, value, 0, 255, settings, errorMessage))
                {
                    *errorMessage = tr("threshold must be a whole number from 0 to 255 or auto.");
                    return false;
                }
            }
            else
            {
                *errorMessage = tr("\"%1\" is no setting of an image (mode, colorMode, goal, keepOriginalIfLarger, preserveTransparency, algorithm, targetDpi, resample, jpegQuality, jpeg2000Rate, pngPredictor, threshold).").arg(name);
                return false;
            }
        }
        return true;
    }
};

QString PDFDocumentToolOptimizeImages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

QString PDFDocumentToolOptimizeImages::getProfileParameterName(const char* profile, const char* field)
{
    if (!profile)
    {
        // The one profile of Grayscale: "algorithm", "targetDpi", ...
        QString name = QString::fromLatin1(field);
        if (!name.isEmpty())
        {
            name[0] = name[0].toLower();
        }
        return name;
    }
    return QString::fromLatin1(profile) + QString::fromLatin1(field);
}

void PDFDocumentToolOptimizeImages::addOptionDefinitions(PDFDocumentToolParameterDefinitions* definitions, Purpose purpose) const
{
    using Definition = PDFDocumentToolParameterDefinition;

    definitions->push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_GOAL), QT_TR_NOOP("Goal"), QT_TR_NOOP("What matters more"),
                                                  {
                                                      // 299:3362 panel-goal: the texts and the icons
                                                      { QString::fromLatin1(GOAL_PREFER_QUALITY), QT_TR_NOOP("Prefer quality"), QT_TR_NOOP("Keeps more detail"), QStringLiteral("award") },
                                                      { QString::fromLatin1(GOAL_MINIMUM_SIZE), QT_TR_NOOP("Minimum size"), QT_TR_NOOP("Prefers a smaller output"), QStringLiteral("minimize-2") },
                                                  },
                                                  QString::fromLatin1(GOAL_PREFER_QUALITY)));
    // 299:3362 panel-options; Grayscale (Q4.15 a): the option chooses only the smallest gray
    // encoding, a color image is always converted (owner 14. 11.: the text of the code, 320:7812
    // keeps the text of Optimize Images - a recorded deviation)
    if (purpose == Purpose::Grayscale)
    {
        definitions->push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_ORIGINAL), QT_TR_NOOP("Keep original if larger"),
                                                      QT_TR_NOOP("Uses the smallest gray encoding; color images are always converted"), true));
    }
    else
    {
        definitions->push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_ORIGINAL), QT_TR_NOOP("Keep original if larger"),
                                                      QT_TR_NOOP("Leaves the image alone when re-encoding does not shrink it"), true));
    }
    definitions->push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PRESERVE_TRANSPARENCY), QT_TR_NOOP("Preserve transparency"),
                                                  QT_TR_NOOP("Stores alpha as a soft mask when possible"), true));
}

void PDFDocumentToolOptimizeImages::addProfileDefinitions(PDFDocumentToolParameterDefinitions* definitions, const char* profile, int defaultDpi) const
{
    using Definition = PDFDocumentToolParameterDefinition;

    const bool isBitonal = profile && qstrcmp(profile, PROFILE_BITONAL) == 0;
    std::vector<PDFDocumentToolParameterOption> algorithms =
    {
        { QString::fromLatin1(ALGORITHM_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("The smallest suitable encoding") },
        { QString::fromLatin1(ALGORITHM_FLATE), QT_TR_NOOP("Flate"), QT_TR_NOOP("Lossless") },
    };
    if (isBitonal)
    {
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_RUN_LENGTH), QT_TR_NOOP("RunLength"), QT_TR_NOOP("Lossless, simple") });
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_CCITT_G4), QT_TR_NOOP("CCITT G4"), QT_TR_NOOP("Lossless, for black and white") });
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_JBIG2), QT_TR_NOOP("JBIG2"), QT_TR_NOOP("Lossless, the smallest for black and white") });
    }
    else
    {
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_JPEG), QT_TR_NOOP("JPEG"), QT_TR_NOOP("Lossy, for photographs") });
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_JPEG2000), QT_TR_NOOP("JPEG 2000"), QT_TR_NOOP("Lossy or lossless") });
        algorithms.push_back({ QString::fromLatin1(ALGORITHM_RUN_LENGTH), QT_TR_NOOP("RunLength"), QT_TR_NOOP("Lossless, simple") });
    }

    definitions->push_back(Definition::createEnum(tr, getProfileParameterName(profile, FIELD_ALGORITHM), QT_TR_NOOP("Algorithm"), QT_TR_NOOP("Encoding of the images"),
                                                  std::move(algorithms), QString::fromLatin1(ALGORITHM_AUTO)));
    definitions->push_back(Definition::createInteger(tr, getProfileParameterName(profile, FIELD_TARGET_DPI), QT_TR_NOOP("Target DPI"),
                                                     QT_TR_NOOP("Resolution, to which larger images are downsampled; 0 keeps the resolution"),
                                                     defaultDpi, 0, MAXIMUM_TARGET_DPI));
    definitions->push_back(Definition::createEnum(tr, getProfileParameterName(profile, FIELD_RESAMPLE), QT_TR_NOOP("Resample"), QT_TR_NOOP("Filter of the downsampling"),
                                                  {
                                                      { QString::fromLatin1(RESAMPLE_NEAREST), QT_TR_NOOP("Nearest"), nullptr },
                                                      { QString::fromLatin1(RESAMPLE_BILINEAR), QT_TR_NOOP("Bilinear"), nullptr },
                                                      { QString::fromLatin1(RESAMPLE_BICUBIC), QT_TR_NOOP("Bicubic"), nullptr },
                                                      { QString::fromLatin1(RESAMPLE_LANCZOS), QT_TR_NOOP("Lanczos"), nullptr },
                                                  },
                                                  QString::fromLatin1(RESAMPLE_BICUBIC)));
    if (!isBitonal)
    {
        definitions->push_back(Definition::createInteger(tr, getProfileParameterName(profile, FIELD_JPEG_QUALITY), QT_TR_NOOP("JPEG quality"),
                                                         QT_TR_NOOP("Quality of a JPEG image, 0 to 100 (the number written into the file)"), 85, 0, 100));
        definitions->push_back(Definition::createReal(tr, getProfileParameterName(profile, FIELD_JPEG2000_RATE), QT_TR_NOOP("JPEG 2000 rate"),
                                                      QT_TR_NOOP("Compression rate of a JPEG 2000 image; 0 = lossless"), 0.0, 0.0, 1000.0));
    }
    definitions->push_back(Definition::createBool(tr, getProfileParameterName(profile, FIELD_PNG_PREDICTOR), QT_TR_NOOP("PNG predictor"),
                                                  QT_TR_NOOP("The predictor of Flate makes most images smaller"), true));
    if (isBitonal)
    {
        definitions->push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_BITONAL_THRESHOLD), QT_TR_NOOP("Threshold"),
                                                         QT_TR_NOOP("Gray level, below which a pixel becomes black (0 to 255); -1 = automatic"), -1, -1, 255));
    }
}

PDFDocumentToolParameterDefinitions PDFDocumentToolOptimizeImages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_MODE), QT_TR_NOOP("Mode"), QT_TR_NOOP("How the encoding of each image is chosen"),
                                                 {
                                                     // 299:3362 panel-mode: the texts and the icons
                                                     { QString::fromLatin1(MODE_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Image analysis picks the color mode and compression"), QStringLiteral("zap") },
                                                     { QString::fromLatin1(MODE_CUSTOM), QT_TR_NOOP("Custom"), QT_TR_NOOP("Uses the selected color mode and profiles"), QStringLiteral("sliders") },
                                                 },
                                                 QString::fromLatin1(MODE_AUTO)));
    // 299:3362 panel-color-mode: the texts; the icons of Auto and Preserve (the GUI draws the
    // swatches of the other three, Optimize Images getColorIcon)
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_COLOR_MODE), QT_TR_NOOP("Color"), QT_TR_NOOP("Colors of the written images"),
                                                 {
                                                     { QString::fromLatin1(COLOR_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Let the optimizer decide"), QStringLiteral("zap") },
                                                     { QString::fromLatin1(COLOR_PRESERVE), QT_TR_NOOP("Preserve"), QT_TR_NOOP("Keep the original color characteristics"), QStringLiteral("lock") },
                                                     { QString::fromLatin1(COLOR_COLOR), QT_TR_NOOP("Color (RGB)"), QT_TR_NOOP("Force color output") },
                                                     { QString::fromLatin1(COLOR_GRAYSCALE), QT_TR_NOOP("Grayscale"), QT_TR_NOOP("Force grayscale output") },
                                                     { QString::fromLatin1(COLOR_BITONAL), QT_TR_NOOP("Bitonal"), QT_TR_NOOP("Force 1-bit output") },
                                                 },
                                                 QString::fromLatin1(COLOR_AUTO)));
    addOptionDefinitions(&definitions, Purpose::OptimizeImages);
    addProfileDefinitions(&definitions, PROFILE_COLOR, 150);
    addProfileDefinitions(&definitions, PROFILE_GRAY, 150);
    addProfileDefinitions(&definitions, PROFILE_BITONAL, 300);

    // The exceptions belong to the images of one document
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_IMAGES), QT_TR_NOOP("Exceptions"),
                                                   QT_TR_NOOP("Images with their own settings, separated by semicolons: \"<id>:off\" keeps the image, \"<id>:name=value,...\" gives it its own settings (mode, colorMode, goal, keepOriginalIfLarger, preserveTransparency, algorithm, targetDpi, resample, jpegQuality, jpeg2000Rate, pngPredictor, threshold; an unnamed setting is the global one); the id is the id of document.list_images (\"148 0\")"),
                                                   QString(), MAXIMUM_IMAGES_LENGTH).setStored(false));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolOptimizeImages::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    return execute(context, parameters, control, progress);
}

PDFDocumentToolResult PDFDocumentToolOptimizeImages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    QString errorMessage;
    const std::vector<ImageException> exceptions = parseImageExceptions(parameters.value(QString::fromLatin1(PARAMETER_IMAGES)).toString(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    return runOptimization(context, createSettings(parameters), exceptions, Purpose::OptimizeImages, control, progress);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolOptimizeImages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    const QString name = QString::fromLatin1(PARAMETER_IMAGES);
    QString errorMessage;
    const std::vector<ImageException> exceptions = parseImageExceptions(parameters.value(name).toString(), &errorMessage);
    if (errorMessage.isEmpty())
    {
        checkImageExceptions(context.getDocument(), exceptions, &errorMessage);
    }
    if (!errorMessage.isEmpty())
    {
        errors.push_back({ name, errorMessage });
    }
    return errors;
}

std::vector<PDFDocumentToolOptimizeImages::ImageException> PDFDocumentToolOptimizeImages::parseImageExceptions(const QString& text, QString* errorMessage)
{
    using Helper = PDFDocumentToolOptimizeImagesHelper;

    errorMessage->clear();
    std::vector<ImageException> exceptions;
    std::map<PDFDocumentImageList::Key, size_t> indices;

    const QStringList entries = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& entryText : entries)
    {
        const QString entry = entryText.trimmed();
        if (entry.isEmpty())
        {
            continue;
        }

        // The key of an inline image has colons too ("inline:12 0:3"): the settings follow the last one
        const qsizetype colon = entry.lastIndexOf(QLatin1Char(':'));
        const std::optional<PDFDocumentImageList::Key> key = colon > 0 ? PDFDocumentImageList::Key::fromString(QStringView(entry).left(colon)) : std::nullopt;
        if (!key)
        {
            *errorMessage = tr("\"%1\" is no exception of an image: write \"<id>:off\" or \"<id>:name=value,...\" with the id of document.list_images.").arg(entry);
            return {};
        }

        ImageException exception;
        exception.key = *key;
        const QString settingsText = entry.mid(colon + 1).trimmed();
        if (settingsText.compare(QLatin1String(EXCEPTION_OFF), Qt::CaseInsensitive) == 0)
        {
            exception.enabled = false;
        }
        else if (!Helper::readExceptionSettings(settingsText, &exception.settings, errorMessage))
        {
            *errorMessage = tr("Image %1: %2").arg(key->toString(), *errorMessage);
            return {};
        }

        if (key->isInline() && exception.enabled)
        {
            *errorMessage = tr("Image %1 is an inline image, which is always kept: only \"off\" can be written for it.").arg(key->toString());
            return {};
        }

        // The last exception of a key wins, in the place of the first
        auto it = indices.find(exception.key);
        if (it != indices.end())
        {
            exceptions[it->second] = std::move(exception);
        }
        else
        {
            indices[exception.key] = exceptions.size();
            exceptions.push_back(std::move(exception));
        }
    }
    return exceptions;
}

QString PDFDocumentToolOptimizeImages::writeImageExceptions(const std::vector<ImageException>& exceptions)
{
    QStringList entries;
    for (const ImageException& exception : exceptions)
    {
        if (!exception.enabled)
        {
            entries << exception.key.toString() + QLatin1Char(':') + QLatin1String(EXCEPTION_OFF);
            continue;
        }

        QStringList settings;
        for (auto it = exception.settings.cbegin(); it != exception.settings.cend(); ++it)
        {
            QString value;
            if (it.key() == QLatin1String(EXCEPTION_THRESHOLD) && it.value().toInt() < 0)
            {
                value = QString::fromLatin1(THRESHOLD_AUTO);
            }
            else if (it.value().typeId() == QMetaType::Bool)
            {
                value = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
            }
            else if (it.value().typeId() == QMetaType::Double || it.value().typeId() == QMetaType::Float)
            {
                value = QString::number(it.value().toDouble(), 'g', 10);
            }
            else
            {
                value = it.value().toString();
            }
            settings << it.key() + QLatin1Char('=') + value;
        }
        entries << exception.key.toString() + QLatin1Char(':') + settings.join(QLatin1Char(','));
    }
    return entries.join(QStringLiteral("; "));
}

PDFImageOptimizer::Settings PDFDocumentToolOptimizeImages::createSettings(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolOptimizeImagesHelper;

    PDFImageOptimizer::Settings settings = Helper::createOptionSettings(parameters);
    settings.autoMode = parameters.value(QString::fromLatin1(PARAMETER_MODE)).toString() != QLatin1String(MODE_CUSTOM);
    settings.colorMode = Helper::getColorMode(parameters.value(QString::fromLatin1(PARAMETER_COLOR_MODE)).toString());
    settings.colorProfile = Helper::readProfile(parameters, PROFILE_COLOR, settings.colorProfile);
    settings.grayProfile = Helper::readProfile(parameters, PROFILE_GRAY, settings.grayProfile);
    settings.bitonalProfile = Helper::readProfile(parameters, PROFILE_BITONAL, settings.bitonalProfile);
    if (parameters.contains(QString::fromLatin1(PARAMETER_BITONAL_THRESHOLD)))
    {
        settings.bitonalProfile.monochromeThreshold = parameters.value(QString::fromLatin1(PARAMETER_BITONAL_THRESHOLD)).toInt();
    }
    return settings;
}

PDFImageOptimizer::Settings PDFDocumentToolOptimizeImages::createImageSettings(const PDFImageOptimizer::Settings& settings, const ImageException& exception)
{
    using Helper = PDFDocumentToolOptimizeImagesHelper;

    PDFImageOptimizer::Settings imageSettings = settings;
    for (auto it = exception.settings.cbegin(); it != exception.settings.cend(); ++it)
    {
        if (it.key() == QLatin1String(EXCEPTION_COLOR_MODE))
        {
            imageSettings.colorMode = Helper::getColorMode(it.value().toString());
            continue;
        }
        if (it.key() == QLatin1String(EXCEPTION_MODE))
        {
            imageSettings.autoMode = it.value().toString() != QLatin1String(MODE_CUSTOM);
            continue;
        }
        if (it.key() == QLatin1String(EXCEPTION_GOAL))
        {
            imageSettings.goal = it.value().toString() == QLatin1String(GOAL_MINIMUM_SIZE) ? PDFImageOptimizer::OptimizationGoal::MinimumSize : PDFImageOptimizer::OptimizationGoal::PreferQuality;
            continue;
        }
        if (it.key() == QLatin1String(EXCEPTION_KEEP_ORIGINAL))
        {
            imageSettings.keepOriginalIfLarger = it.value().toBool();
            continue;
        }
        if (it.key() == QLatin1String(EXCEPTION_PRESERVE_TRANSPARENCY))
        {
            imageSettings.preserveTransparency = it.value().toBool();
            continue;
        }

        // The fields of an override are the profile of the image, whichever color it resolves to
        for (PDFImageOptimizer::CompressionProfile* profile : { &imageSettings.colorProfile, &imageSettings.grayProfile, &imageSettings.bitonalProfile })
        {
            Helper::applyField(profile, it.key(), it.value());
        }
    }
    return imageSettings;
}

PDFImageOptimizer::ImageOverrides PDFDocumentToolOptimizeImages::createOverrides(const PDFImageOptimizer::Settings& settings, const std::vector<ImageException>& exceptions)
{
    PDFImageOptimizer::ImageOverrides overrides;
    for (const ImageException& exception : exceptions)
    {
        if (exception.key.isInline())
        {
            continue;
        }

        PDFImageOptimizer::ImageOverride imageOverride;
        imageOverride.enabled = exception.enabled;
        imageOverride.useCustomSettings = exception.enabled && !exception.settings.isEmpty();
        imageOverride.settings = imageOverride.useCustomSettings ? createImageSettings(settings, exception) : settings;
        overrides[exception.key.reference] = std::move(imageOverride);
    }
    return overrides;
}

bool PDFDocumentToolOptimizeImages::checkImageExceptions(const PDFDocument* document, const std::vector<ImageException>& exceptions, QString* errorMessage)
{
    if (!document)
    {
        return true;
    }

    for (const ImageException& exception : exceptions)
    {
        if (exception.key.isInline())
        {
            // The content of an inline image is a page or a form (R4-8), a stream or a page dictionary
            const PDFObject& content = document->getObjectByReference(exception.key.inlineContentStream);
            if (!content.isStream() && !content.isDictionary())
            {
                *errorMessage = tr("Image %1 is not an image of the document. Use the ids of document.list_images.").arg(exception.key.toString());
                return false;
            }
            continue;
        }

        const PDFObject& object = document->getObjectByReference(exception.key.reference);
        const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : nullptr;
        const PDFObject& subtype = dictionary ? document->getObject(dictionary->get("Subtype")) : PDFObject();
        if (!subtype.isName() || subtype.getString() != "Image")
        {
            *errorMessage = tr("Image %1 is not an image of the document. Use the ids of document.list_images.").arg(exception.key.toString());
            return false;
        }
    }
    return true;
}

PDFDocumentToolResult PDFDocumentToolOptimizeImages::runOptimization(const PDFDocumentToolContext& context, const PDFImageOptimizer::Settings& settings, const std::vector<ImageException>& exceptions,
                                                                     Purpose purpose, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolOptimizeImagesHelper;

    QElapsedTimer timer;
    timer.start();

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    const qint64 sizeBefore = getWholeFileInputSize(input);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    PDFProgressPhases phases(progress);
    const int listPhase = phases.addPhase(5, tr("Looking for images"), false);
    const int analysisPhase = phases.addPhase(5, tr("Looking for images"), false);
    const int imagesPhase = phases.addPhase(80, purpose == Purpose::Grayscale ? tr("Converting images") : tr("Recompressing images"), true);
    const int measurePhase = phases.addPhase(10, tr("Measuring the result"), false);
    phases.start();

    // The one list of the images (R4-4: all layers), the same as the list of the panel
    const PDFDocumentImageList list = PDFDocumentImageList::create(document, PDFDocumentImageList::Options::createForChange(), control, phases.getPhase(listPhase));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    PDFImageOptimizer::Settings runSettings = settings;
    runSettings.enabled = true;
    const PDFImageOptimizer::ImageOverrides overrides = createOverrides(runSettings, exceptions);

    std::vector<PDFImageOptimizer::ImageResult> imageResults;
    PDFImageOptimizer optimizer;
    PDFDocument optimized = optimizer.optimize(document, runSettings, overrides, phases.getPhase(imagesPhase), nullptr, &imageResults, control, phases.getPhase(analysisPhase));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    phases.beginPhase(measurePhase);

    std::set<PDFDocumentImageList::Key> excludedKeys;
    for (const ImageException& exception : exceptions)
    {
        if (!exception.enabled)
        {
            excludedKeys.insert(exception.key);
        }
    }

    std::map<PDFObjectReference, const PDFImageOptimizer::ImageResult*> resultOfImage;
    for (const PDFImageOptimizer::ImageResult& imageResult : imageResults)
    {
        resultOfImage[imageResult.reference] = &imageResult;
    }

    // The rows of the list and the counts of the facts, in the order of the list
    Helper::Counts counts;
    qint64 spotColorImages = 0;     // Recompressed images, which have lost a spot color (plan 5, 8.6)
    QJsonArray images;
    for (const PDFDocumentImageList::Item& item : list.getItems())
    {
        const bool isCounted = purpose == Purpose::OptimizeImages || item.isColorImage();
        const qint64 originalBytes = item.streamBytes + item.maskBytes;
        qint64 newBytes = originalBytes;
        QString status;

        if (item.kind == PDFDocumentImageList::Kind::Inline)
        {
            status = excludedKeys.count(item.key) ? QString::fromLatin1(IMAGE_STATUS_EXCLUDED) : QString::fromLatin1(IMAGE_STATUS_INLINE);
        }
        else
        {
            auto it = resultOfImage.find(item.key.reference);
            const PDFImageOptimizer::ImageResult* imageResult = it != resultOfImage.end() ? it->second : nullptr;
            status = Helper::getStatus(imageResult);
            if (imageResult && !imageResult->keptOriginal)
            {
                // The change of the bytes of the optimizer (the image and its soft mask)
                newBytes = qMax(qint64(0), originalBytes - qint64(imageResult->originalBytes) + qint64(imageResult->newBytes));
            }
        }

        counts.add(status, isCounted, originalBytes, newBytes);
        if (status == QLatin1String(IMAGE_STATUS_RECOMPRESSED) && Helper::isSpotColorImage(document, item))
        {
            ++spotColorImages;
        }

        QJsonObject image;
        image.insert(QStringLiteral("id"), item.key.toString());
        image.insert(QStringLiteral("status"), status);
        image.insert(QStringLiteral("originalBytes"), originalBytes);
        image.insert(QStringLiteral("newBytes"), newBytes);
        images.append(image);
    }

    PDFDocumentToolResult result;
    PDFDocument output;
    ConformanceRemoval conformance;
    if (counts.recompressed > 0)
    {
        output = createWholeFileDocument(optimized.getStorage(), document, &conformance);

        // Never a result, which still claims a standard it was not checked against
        if (!conformance.kept.isEmpty())
        {
            return createConformanceFailure(conformance.kept);
        }
    }

    qint64 sizeAfter = sizeBefore;
    if (counts.recompressed > 0)
    {
        const WholeFileSize outputSize = getWholeFileOutputSize(&output);
        if (!outputSize.error.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(outputSize.error);
        }
        sizeAfter = outputSize.size;
    }
    else
    {
        result = PDFDocumentToolResult::createNothingToDo(Helper::getNothingToDoText(purpose, counts));
    }

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    phases.finish();

    const qint64 duration = timer.elapsed();
    const int savedPercent = PDFDocumentToolFormat::getSavedPercent(counts.bytesBefore, counts.bytesAfter);
    const QString sizeValue = counts.bytesAfter < counts.bytesBefore ? tr("%1 (%2 % smaller)").arg(PDFDocumentToolFormat::formatSizeChange(counts.bytesBefore, counts.bytesAfter)).arg(savedPercent)
                                                                     : PDFDocumentToolFormat::formatSizeChange(counts.bytesBefore, counts.bytesAfter);
    result.facts.push_back({ QStringLiteral("imagesSize"), tr("Images size"), sizeValue });
    if (purpose == Purpose::Grayscale)
    {
        // 308:7341: "285 · excluded 1 · skipped 126"
        result.facts.push_back({ QStringLiteral("converted"), tr("Converted"), tr("%1 · %2 · %3").arg(QString::number(counts.recompressed), tr("excluded %Ln", nullptr, int(counts.excluded)), tr("skipped %Ln", nullptr, int(counts.skipped + counts.kept))) });
        if (counts.problems > 0)
        {
            result.facts.push_back({ QStringLiteral("problems"), tr("Problems"), tr("%Ln images cannot be read", nullptr, int(counts.problems)) });
        }
    }
    else
    {
        // 301:4019: "398 · kept original 12 · excluded 2", Problems "0"
        result.facts.push_back({ QStringLiteral("recompressed"), tr("Recompressed"), tr("%1 · %2 · %3").arg(QString::number(counts.recompressed), tr("kept original %Ln", nullptr, int(counts.kept + counts.skipped)), tr("excluded %Ln", nullptr, int(counts.excluded))) });
        result.facts.push_back({ QStringLiteral("problems"), tr("Problems"), QString::number(counts.problems) });
    }

    if (spotColorImages > 0)
    {
        // Plan 5 (ISO 32000-2, 8.6.6.4, 8.6.6.5): the new encoding has device colors, the colorant
        // (a plate of the print) is gone - said, never silently
        result.facts.push_back({ QStringLiteral("spotColors"), tr("Spot colors"), tr("%Ln images with spot colors were converted", nullptr, int(spotColorImages)) });
    }

    if (counts.recompressed > 0)
    {
        addConformanceFact(&result, conformance.removed);
    }

    result.data.insert(QStringLiteral("imagesSpotColors"), spotColorImages);
    result.data.insert(QStringLiteral("imagesBefore"), counts.bytesBefore);
    result.data.insert(QStringLiteral("imagesAfter"), counts.bytesAfter);
    result.data.insert(QStringLiteral("imagesRecompressed"), counts.recompressed);
    result.data.insert(QStringLiteral("imagesKept"), counts.kept);
    result.data.insert(QStringLiteral("imagesExcluded"), counts.excluded);
    result.data.insert(QStringLiteral("imagesSkipped"), counts.skipped);
    result.data.insert(QStringLiteral("imagesProblems"), counts.problems);
    result.data.insert(QStringLiteral("images"), images);
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);

    finishWholeFileResult(&result, input, std::move(output), sizeBefore, sizeAfter, purpose == Purpose::Grayscale ? QStringLiteral("_grayscale") : QStringLiteral("_images"),
                          counts.recompressed > 0 ? WholeFileDocument::Always : WholeFileDocument::WhenSmaller);
    return result;
}

}   // namespace pdf
