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

#include "pdfocrconfiguration.h"
#include "pdfocrengine.h"
#include "pdfutils.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <set>
#include <algorithm>

#include <cmath>

namespace pdf
{

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static QJsonArray stringListToJson(const QStringList& list)
{
    QJsonArray array;
    for (const QString& item : list)
    {
        array.append(item);
    }
    return array;
}

static QJsonArray quadToJsonArray(const PDFOCRQuad& quad)
{
    QJsonArray array;
    for (const QPointF& point : quad.points)
    {
        array.append(point.x());
        array.append(point.y());
    }
    return array;
}

static std::optional<PDFOCRQuad> quadFromJsonArray(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 8)
    {
        return std::nullopt;
    }

    PDFOCRQuad quad;
    for (int i = 0; i < 4; ++i)
    {
        quad.points[size_t(i)] = QPointF(array.at(2 * i).toDouble(), array.at(2 * i + 1).toDouble());
    }
    return quad;
}

static QStringList stringListFromJson(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& item : value.toArray())
    {
        list << item.toString();
    }
    return list;
}

// -------------------------------------------------------------------------
// PDFOCRPreprocessing
// -------------------------------------------------------------------------

QJsonObject PDFOCRPreprocessing::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("rotation")] = rotation;
    object[QStringLiteral("autoOrientation")] = autoOrientation;
    object[QStringLiteral("deskew")] = deskew;
    object[QStringLiteral("grayscale")] = grayscale;
    object[QStringLiteral("binarization")] = static_cast<int>(binarization);
    object[QStringLiteral("denoise")] = denoise;
    object[QStringLiteral("invert")] = invert;
    if (perspective)
    {
        object[QStringLiteral("perspective")] = quadToJsonArray(*perspective);
    }
    return object;
}

PDFOCRPreprocessing PDFOCRPreprocessing::fromJson(const QJsonObject& object)
{
    PDFOCRPreprocessing result;
    result.rotation = object.value(QStringLiteral("rotation")).toInt(result.rotation);
    result.autoOrientation = object.value(QStringLiteral("autoOrientation")).toBool(result.autoOrientation);
    result.deskew = object.value(QStringLiteral("deskew")).toBool(result.deskew);
    result.grayscale = object.value(QStringLiteral("grayscale")).toBool(result.grayscale);
    const int binarization = object.value(QStringLiteral("binarization")).toInt(static_cast<int>(result.binarization));
    if (binarization >= static_cast<int>(PDFOCRBinarization::Automatic) && binarization <= static_cast<int>(PDFOCRBinarization::Sauvola))
    {
        result.binarization = static_cast<PDFOCRBinarization>(binarization);
    }
    result.denoise = object.value(QStringLiteral("denoise")).toBool(result.denoise);
    result.invert = object.value(QStringLiteral("invert")).toBool(result.invert);
    result.perspective = quadFromJsonArray(object.value(QStringLiteral("perspective")));
    return result;
}

// -------------------------------------------------------------------------
// PDFOCRConfiguration
// -------------------------------------------------------------------------

const std::vector<int>& PDFOCRConfiguration::getStandardResolutions()
{
    static const std::vector<int> resolutions = { 200, 300, 400, 600 };
    return resolutions;
}

QStringList PDFOCRConfiguration::validate() const
{
    QStringList errors;

    if (engineId.isEmpty())
    {
        errors << PDFTranslationContext::tr("OCR engine is not selected.");
    }

    if (languages.isEmpty())
    {
        errors << PDFTranslationContext::tr("At least one language must be selected.");
    }

    for (const QString& language : languages)
    {
        if (!isValidLanguageIdentifier(language))
        {
            errors << PDFTranslationContext::tr("Invalid language identifier '%1'.").arg(language);
        }
    }

    if (!std::isfinite(dpi) || dpi < MinimumDpi || dpi > MaximumDpi)
    {
        errors << PDFTranslationContext::tr("Resolution must be in range %1-%2 DPI.").arg(MinimumDpi).arg(MaximumDpi);
    }

    const int layoutValue = static_cast<int>(layout);
    if (layoutValue < 0 || layoutValue > 13)
    {
        errors << PDFTranslationContext::tr("Invalid page layout type.");
    }

    if (engineMode < 0 || engineMode > 3)
    {
        errors << PDFTranslationContext::tr("Invalid engine mode.");
    }

    if (preprocessing.rotation != 0 && preprocessing.rotation != 90 && preprocessing.rotation != 180 && preprocessing.rotation != 270)
    {
        errors << PDFTranslationContext::tr("Rotation must be 0, 90, 180 or 270 degrees.");
    }

    if (!std::isfinite(reviewThreshold) || reviewThreshold < 0.0 || reviewThreshold > 100.0)
    {
        errors << PDFTranslationContext::tr("Review threshold must be in range 0-100.");
    }

    errors << compression.validate();

    if (workerCount < 1 || workerCount > 64)
    {
        errors << PDFTranslationContext::tr("Number of workers must be in range 1-64.");
    }

    if (memoryBudget < (qint64(64) << 20))
    {
        errors << PDFTranslationContext::tr("Memory budget must be at least 64 MiB.");
    }

    if (pageTimeoutSeconds < 0)
    {
        errors << PDFTranslationContext::tr("Page timeout must not be negative.");
    }

    return errors;
}

bool PDFOCRConfiguration::isValidLanguageIdentifier(const QString& language)
{
    // Language of the engine ("ces"), script model of the catalog ("script/Latin"),
    // optionally with the import suffix ("ces@3f2a1b"). Any other slash, a backslash
    // and ".." are refused, because the identifier becomes a part of a path (R11).
    static const QRegularExpression expression(QStringLiteral("^(script/)?[A-Za-z][A-Za-z0-9_]*(@[A-Za-z0-9_.-]+)?$"));
    return !language.isEmpty() &&
           !language.contains(QStringLiteral("..")) &&
           !language.contains(QChar('\\')) &&
           expression.match(language).hasMatch();
}

QStringList PDFOCRConfiguration::validateEngineParameters(const QVariantMap& parameters,
                                                          const std::vector<PDFOCREngineParameterDescriptor>& descriptors,
                                                          QStringList* errors)
{
    QStringList localErrors;
    QStringList accepted;

    for (auto it = parameters.begin(); it != parameters.end(); ++it)
    {
        const QString& name = it.key();
        const QVariant& value = it.value();

        auto descriptorIt = std::find_if(descriptors.begin(), descriptors.end(), [&name](const PDFOCREngineParameterDescriptor& descriptor) { return descriptor.name == name; });
        if (descriptorIt == descriptors.end())
        {
            localErrors << PDFTranslationContext::tr("Engine parameter '%1' is not supported.").arg(name);
            continue;
        }

        const PDFOCREngineParameterDescriptor& descriptor = *descriptorIt;
        bool convertible = false;
        double numericValue = 0.0;

        switch (descriptor.type)
        {
            case PDFOCREngineParameterDescriptor::Type::Boolean:
            {
                if (value.typeId() == QMetaType::Bool)
                {
                    convertible = true;
                }
                else if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::UInt || value.typeId() == QMetaType::ULongLong)
                {
                    const qlonglong integer = value.toLongLong();
                    convertible = integer == 0 || integer == 1;
                }
                else if (value.typeId() == QMetaType::Double)
                {
                    convertible = value.toDouble() == 0.0 || value.toDouble() == 1.0;
                }
                else if (value.typeId() == QMetaType::QString)
                {
                    const QString text = value.toString().trimmed().toLower();
                    convertible = text == QStringLiteral("true") || text == QStringLiteral("false") || text == QStringLiteral("1") || text == QStringLiteral("0");
                }
                break;
            }

            case PDFOCREngineParameterDescriptor::Type::Integer:
            {
                if (value.typeId() == QMetaType::Bool)
                {
                    convertible = false;
                }
                else if (value.typeId() == QMetaType::Double)
                {
                    const double doubleValue = value.toDouble();
                    convertible = std::isfinite(doubleValue) && doubleValue == std::floor(doubleValue);
                    numericValue = doubleValue;
                }
                else
                {
                    const qlonglong integer = value.toString().trimmed().toLongLong(&convertible);
                    numericValue = double(integer);
                }
                break;
            }

            case PDFOCREngineParameterDescriptor::Type::Double:
            {
                if (value.typeId() == QMetaType::Bool)
                {
                    convertible = false;
                }
                else
                {
                    numericValue = value.toString().trimmed().toDouble(&convertible);
                    convertible = convertible && std::isfinite(numericValue);
                }
                break;
            }

            case PDFOCREngineParameterDescriptor::Type::String:
            {
                convertible = value.canConvert<QString>() && !value.toString().contains(QChar('\n'));
                break;
            }
        }

        if (!convertible)
        {
            localErrors << PDFTranslationContext::tr("Value of the engine parameter '%1' has a wrong type.").arg(name);
            continue;
        }

        const bool isNumeric = descriptor.type == PDFOCREngineParameterDescriptor::Type::Integer || descriptor.type == PDFOCREngineParameterDescriptor::Type::Double;
        if (isNumeric && (numericValue < descriptor.minimum || numericValue > descriptor.maximum))
        {
            localErrors << PDFTranslationContext::tr("Value of the engine parameter '%1' must be in range %2-%3.").arg(name).arg(descriptor.minimum).arg(descriptor.maximum);
            continue;
        }

        accepted << name;
    }

    if (errors)
    {
        *errors = localErrors;
    }

    return accepted;
}

bool PDFOCRConfiguration::isBasicLayout(PDFOCRLayout layout)
{
    switch (layout)
    {
        case PDFOCRLayout::Automatic:
        case PDFOCRLayout::SingleColumn:
        case PDFOCRLayout::SingleBlock:
        case PDFOCRLayout::SingleLine:
        case PDFOCRLayout::SparseText:
            return true;

        default:
            break;
    }

    return false;
}

QString PDFOCRConfiguration::getLayoutName(PDFOCRLayout layout)
{
    switch (layout)
    {
        case PDFOCRLayout::OrientationOnly:
            return PDFTranslationContext::tr("Orientation and script detection only");
        case PDFOCRLayout::AutomaticWithOrientation:
            return PDFTranslationContext::tr("Automatic with orientation detection");
        case PDFOCRLayout::SegmentationOnly:
            return PDFTranslationContext::tr("Segmentation only (no OCR)");
        case PDFOCRLayout::Automatic:
            return PDFTranslationContext::tr("Automatic");
        case PDFOCRLayout::SingleColumn:
            return PDFTranslationContext::tr("Single column");
        case PDFOCRLayout::VerticalBlock:
            return PDFTranslationContext::tr("Single block of vertical text");
        case PDFOCRLayout::SingleBlock:
            return PDFTranslationContext::tr("Single block of text");
        case PDFOCRLayout::SingleLine:
            return PDFTranslationContext::tr("Single line");
        case PDFOCRLayout::SingleWord:
            return PDFTranslationContext::tr("Single word");
        case PDFOCRLayout::CircleWord:
            return PDFTranslationContext::tr("Single word in a circle");
        case PDFOCRLayout::SingleCharacter:
            return PDFTranslationContext::tr("Single character");
        case PDFOCRLayout::SparseText:
            return PDFTranslationContext::tr("Sparse text");
        case PDFOCRLayout::SparseTextWithOrientation:
            return PDFTranslationContext::tr("Sparse text with orientation detection");
        case PDFOCRLayout::RawLine:
            return PDFTranslationContext::tr("Raw line");
    }

    return QString();
}

QString PDFOCRConfiguration::getLayoutDescription(PDFOCRLayout layout)
{
    switch (layout)
    {
        case PDFOCRLayout::OrientationOnly:
            return PDFTranslationContext::tr("Detects only the orientation and the script of the page, no text is recognized.");
        case PDFOCRLayout::AutomaticWithOrientation:
            return PDFTranslationContext::tr("Automatic page segmentation with orientation and script detection performed by the engine.");
        case PDFOCRLayout::SegmentationOnly:
            return PDFTranslationContext::tr("Automatic page segmentation without orientation detection and without text recognition.");
        case PDFOCRLayout::Automatic:
            return PDFTranslationContext::tr("Fully automatic page segmentation without orientation detection. Recommended for most documents.");
        case PDFOCRLayout::SingleColumn:
            return PDFTranslationContext::tr("Single column of text of variable sizes.");
        case PDFOCRLayout::VerticalBlock:
            return PDFTranslationContext::tr("Single uniform block of vertically aligned text.");
        case PDFOCRLayout::SingleBlock:
            return PDFTranslationContext::tr("Single uniform block of text, for example a cropped paragraph.");
        case PDFOCRLayout::SingleLine:
            return PDFTranslationContext::tr("Image contains a single text line.");
        case PDFOCRLayout::SingleWord:
            return PDFTranslationContext::tr("Image contains a single word.");
        case PDFOCRLayout::CircleWord:
            return PDFTranslationContext::tr("Image contains a single word placed in a circle.");
        case PDFOCRLayout::SingleCharacter:
            return PDFTranslationContext::tr("Image contains a single character.");
        case PDFOCRLayout::SparseText:
            return PDFTranslationContext::tr("Finds as much text as possible without a particular order (forms, posters).");
        case PDFOCRLayout::SparseTextWithOrientation:
            return PDFTranslationContext::tr("Sparse text with orientation and script detection.");
        case PDFOCRLayout::RawLine:
            return PDFTranslationContext::tr("Single text line, bypassing engine specific hacks.");
    }

    return QString();
}

const std::vector<PDFOCRLayout>& PDFOCRConfiguration::getLayouts()
{
    static const std::vector<PDFOCRLayout> layouts =
    {
        PDFOCRLayout::Automatic,
        PDFOCRLayout::SingleColumn,
        PDFOCRLayout::SingleBlock,
        PDFOCRLayout::SingleLine,
        PDFOCRLayout::SparseText,
        PDFOCRLayout::AutomaticWithOrientation,
        PDFOCRLayout::VerticalBlock,
        PDFOCRLayout::SingleWord,
        PDFOCRLayout::CircleWord,
        PDFOCRLayout::SingleCharacter,
        PDFOCRLayout::SparseTextWithOrientation,
        PDFOCRLayout::RawLine
    };
    return layouts;
}

const std::vector<PDFOCRModelProfile>& PDFOCRConfiguration::getProfiles()
{
    static const std::vector<PDFOCRModelProfile> profiles =
    {
        PDFOCRModelProfile::Fast,
        PDFOCRModelProfile::Standard,
        PDFOCRModelProfile::Best
    };
    return profiles;
}

QString PDFOCRConfiguration::getProfileName(PDFOCRModelProfile profile)
{
    switch (profile)
    {
        case PDFOCRModelProfile::Fast:
            return PDFTranslationContext::tr("Fast");
        case PDFOCRModelProfile::Standard:
            return PDFTranslationContext::tr("Standard");
        case PDFOCRModelProfile::Best:
            return PDFTranslationContext::tr("Quality");
    }

    return QString();
}

QString PDFOCRConfiguration::getProfileIdentifier(PDFOCRModelProfile profile)
{
    switch (profile)
    {
        case PDFOCRModelProfile::Fast:
            return QStringLiteral("fast");
        case PDFOCRModelProfile::Standard:
            return QStringLiteral("standard");
        case PDFOCRModelProfile::Best:
            return QStringLiteral("best");
    }

    return QString();
}

PDFOCRModelProfile PDFOCRConfiguration::parseProfileIdentifier(const QString& identifier)
{
    for (const PDFOCRModelProfile profile : getProfiles())
    {
        if (identifier == getProfileIdentifier(profile))
        {
            return profile;
        }
    }

    return PDFOCRModelProfile::Fast;
}

QJsonObject PDFOCRConfiguration::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("engineId")] = engineId;
    object[QStringLiteral("languages")] = stringListToJson(languages);
    object[QStringLiteral("profile")] = getProfileIdentifier(profile);
    object[QStringLiteral("modelSetId")] = modelSetId;
    object[QStringLiteral("layout")] = static_cast<int>(layout);
    object[QStringLiteral("engineMode")] = engineMode;
    object[QStringLiteral("dpi")] = dpi;
    object[QStringLiteral("preprocessing")] = preprocessing.toJson();
    object[QStringLiteral("userWords")] = stringListToJson(userWords);
    object[QStringLiteral("userPatterns")] = stringListToJson(userPatterns);
    object[QStringLiteral("characterWhitelist")] = characterWhitelist;
    object[QStringLiteral("characterBlacklist")] = characterBlacklist;
    object[QStringLiteral("reviewThreshold")] = reviewThreshold;
    object[QStringLiteral("reviewOutsideDictionary")] = reviewOutsideDictionary;
    object[QStringLiteral("detectBlankPages")] = detectBlankPages;
    object[QStringLiteral("existingTextPolicy")] = static_cast<int>(existingTextPolicy);
    object[QStringLiteral("keepReviewDataInDocument")] = keepReviewDataInDocument;
    object[QStringLiteral("compression")] = compression.toJson();
    object[QStringLiteral("workerCount")] = workerCount;
    object[QStringLiteral("memoryBudget")] = QString::number(memoryBudget);
    object[QStringLiteral("pageTimeoutSeconds")] = pageTimeoutSeconds;
    object[QStringLiteral("engineParameters")] = QJsonObject::fromVariantMap(engineParameters);
    return object;
}

PDFOCRConfiguration PDFOCRConfiguration::fromJson(const QJsonObject& object)
{
    PDFOCRConfiguration result;
    result.engineId = object.value(QStringLiteral("engineId")).toString(result.engineId);
    result.languages = stringListFromJson(object.value(QStringLiteral("languages")));
    result.profile = parseProfileIdentifier(object.value(QStringLiteral("profile")).toString());
    result.modelSetId = object.value(QStringLiteral("modelSetId")).toString();

    const int layout = object.value(QStringLiteral("layout")).toInt(static_cast<int>(result.layout));
    if (layout >= 0 && layout <= 13)
    {
        result.layout = static_cast<PDFOCRLayout>(layout);
    }

    result.engineMode = object.value(QStringLiteral("engineMode")).toInt(result.engineMode);
    result.dpi = object.value(QStringLiteral("dpi")).toDouble(result.dpi);
    result.preprocessing = PDFOCRPreprocessing::fromJson(object.value(QStringLiteral("preprocessing")).toObject());
    result.userWords = stringListFromJson(object.value(QStringLiteral("userWords")));
    result.userPatterns = stringListFromJson(object.value(QStringLiteral("userPatterns")));
    result.characterWhitelist = object.value(QStringLiteral("characterWhitelist")).toString();
    result.characterBlacklist = object.value(QStringLiteral("characterBlacklist")).toString();
    result.reviewThreshold = object.value(QStringLiteral("reviewThreshold")).toDouble(result.reviewThreshold);
    result.reviewOutsideDictionary = object.value(QStringLiteral("reviewOutsideDictionary")).toBool(result.reviewOutsideDictionary);
    result.detectBlankPages = object.value(QStringLiteral("detectBlankPages")).toBool(result.detectBlankPages);

    const int policy = object.value(QStringLiteral("existingTextPolicy")).toInt(static_cast<int>(result.existingTextPolicy));
    if (policy >= static_cast<int>(PDFOCRExistingTextPolicy::OnlyPagesWithoutText) && policy <= static_cast<int>(PDFOCRExistingTextPolicy::ReviewOnly))
    {
        result.existingTextPolicy = static_cast<PDFOCRExistingTextPolicy>(policy);
    }

    result.keepReviewDataInDocument = object.value(QStringLiteral("keepReviewDataInDocument")).toBool(result.keepReviewDataInDocument);
    result.compression = PDFOCRCompressionSettings::fromJson(object.value(QStringLiteral("compression")).toObject());
    result.workerCount = object.value(QStringLiteral("workerCount")).toInt(result.workerCount);

    bool ok = false;
    const qint64 memoryBudget = object.value(QStringLiteral("memoryBudget")).toString().toLongLong(&ok);
    if (ok)
    {
        result.memoryBudget = memoryBudget;
    }

    result.pageTimeoutSeconds = object.value(QStringLiteral("pageTimeoutSeconds")).toInt(result.pageTimeoutSeconds);
    result.engineParameters = object.value(QStringLiteral("engineParameters")).toObject().toVariantMap();
    return result;
}

QVariantMap PDFOCRConfiguration::toParameterMap() const
{
    return toJson().toVariantMap();
}

// -------------------------------------------------------------------------
// PDFOCRPageOverride
// -------------------------------------------------------------------------

bool PDFOCRPageOverride::isEmpty() const
{
    return !languages && !layout && !rotation && !dpi && !autoOrientation && !deskew && !perspective;
}

PDFOCRConfiguration PDFOCRPageOverride::apply(PDFOCRConfiguration configuration) const
{
    if (languages)
    {
        configuration.languages = *languages;
    }
    if (layout)
    {
        configuration.layout = *layout;
    }
    if (rotation)
    {
        configuration.preprocessing.rotation = *rotation;
    }
    if (dpi)
    {
        configuration.dpi = *dpi;
    }
    if (autoOrientation)
    {
        configuration.preprocessing.autoOrientation = *autoOrientation;
    }
    if (deskew)
    {
        configuration.preprocessing.deskew = *deskew;
    }
    if (perspective)
    {
        configuration.preprocessing.perspective = *perspective;
    }
    return configuration;
}

QJsonObject PDFOCRPageOverride::toJson() const
{
    QJsonObject object;
    if (languages)
    {
        object[QStringLiteral("languages")] = stringListToJson(*languages);
    }
    if (layout)
    {
        object[QStringLiteral("layout")] = static_cast<int>(*layout);
    }
    if (rotation)
    {
        object[QStringLiteral("rotation")] = *rotation;
    }
    if (dpi)
    {
        object[QStringLiteral("dpi")] = *dpi;
    }
    if (autoOrientation)
    {
        object[QStringLiteral("autoOrientation")] = *autoOrientation;
    }
    if (deskew)
    {
        object[QStringLiteral("deskew")] = *deskew;
    }
    if (perspective)
    {
        object[QStringLiteral("perspective")] = quadToJsonArray(*perspective);
    }
    return object;
}

PDFOCRPageOverride PDFOCRPageOverride::fromJson(const QJsonObject& object)
{
    PDFOCRPageOverride result;
    if (object.contains(QStringLiteral("languages")))
    {
        result.languages = stringListFromJson(object.value(QStringLiteral("languages")));
    }
    if (object.contains(QStringLiteral("layout")))
    {
        const int layout = object.value(QStringLiteral("layout")).toInt();
        if (layout >= 0 && layout <= 13)
        {
            result.layout = static_cast<PDFOCRLayout>(layout);
        }
    }
    if (object.contains(QStringLiteral("rotation")))
    {
        result.rotation = object.value(QStringLiteral("rotation")).toInt();
    }
    if (object.contains(QStringLiteral("dpi")))
    {
        result.dpi = object.value(QStringLiteral("dpi")).toDouble();
    }
    if (object.contains(QStringLiteral("autoOrientation")))
    {
        result.autoOrientation = object.value(QStringLiteral("autoOrientation")).toBool();
    }
    if (object.contains(QStringLiteral("deskew")))
    {
        result.deskew = object.value(QStringLiteral("deskew")).toBool();
    }
    result.perspective = quadFromJsonArray(object.value(QStringLiteral("perspective")));
    return result;
}

// -------------------------------------------------------------------------
// PDFOCRProfile
// -------------------------------------------------------------------------

QJsonObject PDFOCRProfile::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("name")] = name;
    object[QStringLiteral("configuration")] = configuration.toJson();
    return object;
}

PDFOCRProfile PDFOCRProfile::fromJson(const QJsonObject& object)
{
    PDFOCRProfile profile;
    profile.name = object.value(QStringLiteral("name")).toString();
    profile.configuration = PDFOCRConfiguration::fromJson(object.value(QStringLiteral("configuration")).toObject());
    return profile;
}

// -------------------------------------------------------------------------
// PDFOCRConfigurationResolver
// -------------------------------------------------------------------------

PDFOCRConfiguration PDFOCRConfigurationResolver::resolve(const PDFOCRConfiguration& jobConfiguration,
                                                         const PDFOCRPageOverride* pageOverride,
                                                         const PDFOCRRegionOverride* regionOverride)
{
    PDFOCRConfiguration configuration = jobConfiguration;

    if (pageOverride)
    {
        configuration = pageOverride->apply(configuration);
    }

    if (regionOverride)
    {
        if (!regionOverride->languages.isEmpty())
        {
            configuration.languages = regionOverride->languages;
        }
        if (regionOverride->segmentation >= 0 && regionOverride->segmentation <= 13)
        {
            configuration.layout = static_cast<PDFOCRLayout>(regionOverride->segmentation);
        }
        if (regionOverride->rotation >= 0)
        {
            configuration.preprocessing.rotation = regionOverride->rotation;
        }
    }

    return configuration;
}

// -------------------------------------------------------------------------
// PDFOCRPageSelection
// -------------------------------------------------------------------------

std::vector<PDFInteger> PDFOCRPageSelection::parseRange(PDFInteger pageCount, const QString& text, Parity parity, QString* errorMessage)
{
    std::vector<PDFInteger> result;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Page range is empty. Enter page numbers from 1 to %1, for example '1, 3-5, 9'.").arg(pageCount);
        }
        return result;
    }

    // Only the documented syntax is accepted: numbers and closed intervals
    // separated by commas (PAGE-02).
    static const QRegularExpression tokenExpression(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$"));
    std::set<PDFInteger> pages;

    for (const QString& token : trimmed.split(QChar(','), Qt::KeepEmptyParts))
    {
        const QRegularExpressionMatch match = tokenExpression.match(token);
        if (!match.hasMatch())
        {
            if (errorMessage)
            {
                *errorMessage = PDFTranslationContext::tr("Invalid page range '%1'. Enter page numbers from 1 to %2, for example '1, 3-5, 9'.").arg(token.trimmed()).arg(pageCount);
            }
            return result;
        }

        bool ok = false;
        const PDFInteger first = match.captured(1).toLongLong(&ok);
        PDFInteger last = first;
        if (ok && match.capturedLength(2) > 0)
        {
            last = match.captured(2).toLongLong(&ok);
        }

        if (!ok || first < 1 || last < 1)
        {
            if (errorMessage)
            {
                *errorMessage = PDFTranslationContext::tr("Invalid page range '%1'. Page numbers start from 1.").arg(token.trimmed());
            }
            return result;
        }

        if (first > pageCount || last > pageCount)
        {
            if (errorMessage)
            {
                *errorMessage = PDFTranslationContext::tr("Page range '%1' exceeds the page count %2.").arg(token.trimmed()).arg(pageCount);
            }
            return result;
        }

        if (last < first)
        {
            if (errorMessage)
            {
                *errorMessage = PDFTranslationContext::tr("Page range '%1' is descending.").arg(token.trimmed());
            }
            return result;
        }

        for (PDFInteger page = first; page <= last; ++page)
        {
            pages.insert(page - 1);
        }
    }

    result.assign(pages.begin(), pages.end());
    result = filterParity(std::move(result), parity);

    if (result.empty() && errorMessage)
    {
        *errorMessage = PDFTranslationContext::tr("Selected page range is empty.");
    }

    return result;
}

std::vector<PDFInteger> PDFOCRPageSelection::filterParity(std::vector<PDFInteger> indices, Parity parity)
{
    if (parity == Parity::All)
    {
        return indices;
    }

    // Parity of the physical page number (1-based), not of the index
    const PDFInteger remainder = parity == Parity::Odd ? 1 : 0;
    std::erase_if(indices, [remainder](PDFInteger index) { return (index + 1) % 2 != remainder; });
    return indices;
}

QString PDFOCRPageSelection::describe(const std::vector<PDFInteger>& indices)
{
    PDFClosedIntervalSet set;
    for (PDFInteger index : indices)
    {
        set.addValue(index + 1);
    }
    return set.toText(true);
}

}   // namespace pdf
