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

#include "pdftesseractocrengine.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QElapsedTimer>

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/resultiterator.h>
#include <tesseract/publictypes.h>
#include <leptonica/allheaders.h>

#include <memory>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <optional>

namespace pdf
{

namespace
{

/// Monitor data passed to the Tesseract cancel/progress callbacks
struct MonitorContext
{
    const PDFOperationControl* operationControl = nullptr;
    const PDFOCRProgressCallback* progressCallback = nullptr;
    int lastProgress = -1;
};

bool cancelFunction(void* data, int words)
{
    Q_UNUSED(words);
    MonitorContext* context = reinterpret_cast<MonitorContext*>(data);
    return PDFOperationControl::isOperationCancelled(context->operationControl);
}

bool progressFunction(tesseract::ETEXT_DESC* monitor, int left, int right, int top, int bottom)
{
    Q_UNUSED(left);
    Q_UNUSED(right);
    Q_UNUSED(top);
    Q_UNUSED(bottom);

    MonitorContext* context = reinterpret_cast<MonitorContext*>(monitor->cancel_this);
    if (context && context->progressCallback && *context->progressCallback && monitor->progress != context->lastProgress)
    {
        context->lastProgress = monitor->progress;
        (*context->progressCallback)(monitor->progress);
    }

    return !PDFOperationControl::isOperationCancelled(context ? context->operationControl : nullptr);
}

tesseract::PageSegMode toPageSegMode(PDFOCRLayout layout)
{
    const int value = static_cast<int>(layout);
    if (value >= 0 && value < tesseract::PSM_COUNT)
    {
        return static_cast<tesseract::PageSegMode>(value);
    }
    return tesseract::PSM_AUTO;
}

tesseract::OcrEngineMode toEngineMode(int engineMode)
{
    switch (engineMode)
    {
        case 0:
            return tesseract::OEM_TESSERACT_ONLY;
        case 1:
            return tesseract::OEM_LSTM_ONLY;
        case 2:
            return tesseract::OEM_TESSERACT_LSTM_COMBINED;
        default:
            return tesseract::OEM_DEFAULT;
    }
}

QRectF boundingBox(const tesseract::PageIterator* iterator, tesseract::PageIteratorLevel level)
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    if (iterator->BoundingBox(level, &left, &top, &right, &bottom))
    {
        return QRectF(QPointF(left, top), QPointF(right, bottom));
    }
    return QRectF();
}

/// Reads the files for Tesseract. Tesseract opens the files by the narrow character
/// functions, which use the ANSI code page on Windows, so a path with characters
/// outside of the code page (user name with diacritics) would not work. Reading
/// through Qt makes the model loading independent of the code page (AT-22).
bool readFileForEngine(const char* fileName, std::vector<char>* data)
{
    if (!fileName || !data)
    {
        return false;
    }

    QFile file(QString::fromUtf8(fileName));
    if (!file.open(QFile::ReadOnly))
    {
        return false;
    }

    constexpr qint64 MAXIMUM_MODEL_SIZE = qint64(1) << 30;
    const qint64 size = file.size();
    if (size < 0 || size > MAXIMUM_MODEL_SIZE)
    {
        return false;
    }

    data->resize(size_t(size));
    return size == 0 || file.read(data->data(), size) == size;
}

/// Initializes the engine with the data path in UTF-8 and the file reader
int initializeApi(tesseract::TessBaseAPI& api,
                  const QString& dataPath,
                  const QByteArray& languages,
                  tesseract::OcrEngineMode mode,
                  const std::vector<std::string>& variableNames,
                  const std::vector<std::string>& variableValues)
{
    const QByteArray encodedDataPath = QDir::toNativeSeparators(dataPath).toUtf8();
    return api.Init(encodedDataPath.constData(), 0, languages.constData(), mode, nullptr, 0, &variableNames, &variableValues, false, &readFileForEngine);
}

/// Encodes the name of a file, which is opened by the engine itself (user words and
/// patterns). Returns no value, if the name cannot be represented.
std::optional<QByteArray> encodeFileNameForEngine(const QString& fileName)
{
    const QString nativeFileName = QDir::toNativeSeparators(fileName);

#ifdef Q_OS_WIN
    const QByteArray encoded = nativeFileName.toLocal8Bit();
    if (QString::fromLocal8Bit(encoded) != nativeFileName)
    {
        return std::nullopt;
    }
    return encoded;
#else
    return nativeFileName.toUtf8();
#endif
}

QString takeText(char* text)
{
    QString result;
    if (text)
    {
        result = QString::fromUtf8(text);
        delete[] text;
    }
    return result;
}

/// Formats the validated value of a typed parameter for the engine (REC-03).
/// Numbers are always formatted with the '.' decimal separator.
QByteArray formatParameterValue(const PDFOCREngineParameterDescriptor& descriptor, const QVariant& value)
{
    switch (descriptor.type)
    {
        case PDFOCREngineParameterDescriptor::Type::Boolean:
        {
            bool boolean = false;
            if (value.typeId() == QMetaType::QString)
            {
                const QString text = value.toString().trimmed().toLower();
                boolean = text == QStringLiteral("true") || text == QStringLiteral("1");
            }
            else
            {
                boolean = value.toBool();
            }
            return boolean ? QByteArrayLiteral("1") : QByteArrayLiteral("0");
        }

        case PDFOCREngineParameterDescriptor::Type::Integer:
            return QByteArray::number(qlonglong(value.toString().trimmed().toDouble()));

        case PDFOCREngineParameterDescriptor::Type::Double:
            return QByteArray::number(value.toString().trimmed().toDouble(), 'g', 15);

        case PDFOCREngineParameterDescriptor::Type::String:
            break;
    }

    return value.toString().toUtf8();
}

/// Vetted schema of the Tesseract parameters offered to the user (REC-03). Only
/// these variables are passed to the engine, everything else is refused by the
/// validation of the configuration. Names are the names of the Tesseract 5 variables.
const std::vector<PDFOCREngineParameterDescriptor>& getParameterSchema()
{
    static const std::vector<PDFOCREngineParameterDescriptor> schema = []()
    {
        using Type = PDFOCREngineParameterDescriptor::Type;
        std::vector<PDFOCREngineParameterDescriptor> result;

        auto add = [&result](const char* name, Type type, QVariant defaultValue, double minimum, double maximum, QString description, bool beforeInitialization)
        {
            PDFOCREngineParameterDescriptor descriptor;
            descriptor.name = QString::fromLatin1(name);
            descriptor.type = type;
            descriptor.defaultValue = std::move(defaultValue);
            descriptor.minimum = minimum;
            descriptor.maximum = maximum;
            descriptor.description = std::move(description);
            descriptor.beforeInitialization = beforeInitialization;
            result.push_back(std::move(descriptor));
        };

        constexpr double lowest = std::numeric_limits<double>::lowest();
        constexpr double highest = std::numeric_limits<double>::max();

        add("preserve_interword_spaces", Type::Boolean, false, lowest, highest, PDFTranslationContext::tr("Preserve multiple spaces between the words in the text output."), false);
        add("user_defined_dpi", Type::Integer, 0, 70, 2400, PDFTranslationContext::tr("Resolution assumed by the engine, when the image does not declare it (DPI)."), false);
        add("textord_min_linesize", Type::Double, 1.25, 0.5, 10.0, PDFTranslationContext::tr("Minimal line size relative to the median text size used by the line finder."), false);
        add("tessedit_do_invert", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Try also the inverted image (light text on a dark background)."), false);
        add("lstm_choice_mode", Type::Integer, 0, 0, 2, PDFTranslationContext::tr("Alternative symbol choices of the LSTM recognizer (0 = none, 1 = per symbol, 2 = per timestep)."), false);
        add("classify_bln_numeric_mode", Type::Boolean, false, lowest, highest, PDFTranslationContext::tr("Numeric only mode of the classifier."), false);
        add("textord_tabfind_find_tables", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Detect tables during the layout analysis."), false);
        add("textord_heavy_nr", Type::Boolean, false, lowest, highest, PDFTranslationContext::tr("Heavy noise removal during the layout analysis."), false);
        add("min_characters_to_try", Type::Integer, 50, 1, 10000, PDFTranslationContext::tr("Minimal number of characters required by the orientation and script detection."), false);
        add("load_system_dawg", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Load the system dictionary of the language."), true);
        add("load_freq_dawg", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Load the dictionary of the frequent words of the language."), true);
        add("load_punc_dawg", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Load the punctuation patterns of the language."), true);
        add("load_number_dawg", Type::Boolean, true, lowest, highest, PDFTranslationContext::tr("Load the number patterns of the language."), true);

        return result;
    }();

    return schema;
}

} // anonymous namespace

/// Tesseract engine instance. One instance must not be used by two jobs
/// at the same time (JOB-09).
class PDFTesseractOCREngine : public PDFOCREngine
{
public:
    explicit PDFTesseractOCREngine(const PDFTesseractOCREngineFactory* factory) :
        m_factory(factory)
    {

    }

    virtual ~PDFTesseractOCREngine() override
    {
        release();
    }

    virtual QString getIdentifier() const override { return m_factory->getIdentifier(); }
    virtual QString getName() const override { return m_factory->getName(); }
    virtual QString getVersion() const override { return m_factory->getVersion(); }
    virtual PDFOCREngineCapabilities getCapabilities() const override { return m_factory->getCapabilities(); }

    virtual PDFOCRError validateConfiguration(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) const override
    {
        if (configuration.engineId != getIdentifier())
        {
            return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Configuration belongs to a different engine."), PDFTranslationContext::tr("Configuration"));
        }

        if (!models.isValid())
        {
            return PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("No language model is available."), PDFTranslationContext::tr("Configuration"));
        }

        for (const QString& language : configuration.languages)
        {
            if (!models.languages.contains(language.section(QChar('@'), 0, 0)))
            {
                return PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Language model '%1' is not part of the resolved model set.").arg(language), PDFTranslationContext::tr("Configuration"));
            }
        }

        // Legacy and combined engine modes are not supported by the fast/best LSTM models (chapter 3.3)
        if (configuration.engineMode == 0 || configuration.engineMode == 2)
        {
            return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel,
                                       PDFTranslationContext::tr("Engine mode %1 (legacy) is not supported by the LSTM models of the profile '%2'.").arg(configuration.engineMode).arg(PDFOCRConfiguration::getProfileName(models.profile)),
                                       PDFTranslationContext::tr("Configuration"));
        }

        if (configuration.layout == PDFOCRLayout::SegmentationOnly)
        {
            return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Layout 'segmentation only' does not recognize any text."), PDFTranslationContext::tr("Configuration"));
        }

        // Engine parameters must match the declared schema (REC-03): unknown names,
        // wrong types and values out of range are refused by name.
        QStringList parameterErrors;
        PDFOCRConfiguration::validateEngineParameters(configuration.engineParameters, getParameterSchema(), &parameterErrors);
        if (!parameterErrors.isEmpty())
        {
            return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, parameterErrors.front(), PDFTranslationContext::tr("Configuration"), parameterErrors.join(QChar('\n')));
        }

        return PDFOCRError::none();
    }

    virtual PDFOCRError prepare(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) override
    {
        if (PDFOCRError error = validateConfiguration(configuration, models))
        {
            return error;
        }

        release();

        m_api = std::make_unique<tesseract::TessBaseAPI>();

        QStringList languages;
        for (const QString& language : configuration.languages)
        {
            languages << language.section(QChar('@'), 0, 0);
        }
        const QByteArray languageString = languages.join(QChar('+')).toUtf8();

        // User words and patterns are written into the managed working storage (ARCH-03).
        // Tesseract loads them together with the dictionaries, so they must be passed
        // to the initialization; setting them later has no effect (REC-03).
        std::vector<std::string> initNames;
        std::vector<std::string> initValues;

        auto writeUserFile = [&](const QStringList& lines, const QString& prefix, const char* variable, QString& fileName) -> PDFOCRError
        {
            if (lines.isEmpty())
            {
                return PDFOCRError::none();
            }

            const QString workingDirectory = QDir(models.dataPath).absolutePath() + QStringLiteral("/../user");
            QDir().mkpath(workingDirectory);
            fileName = QDir::cleanPath(QDir(workingDirectory).absoluteFilePath(QStringLiteral("%1-%2.txt").arg(prefix, QUuid::createUuid().toString(QUuid::Id128).left(8))));

            QFile file(fileName);
            const QByteArray data = lines.join(QChar('\n')).toUtf8() + "\n";
            if (!file.open(QFile::WriteOnly | QFile::Truncate) || file.write(data) != data.size())
            {
                fileName.clear();
                return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration,
                                           PDFTranslationContext::tr("User words cannot be written into the working directory '%1'.").arg(workingDirectory),
                                           PDFTranslationContext::tr("Initialization"));
            }
            file.close();

            const std::optional<QByteArray> encodedFileName = encodeFileNameForEngine(fileName);
            if (!encodedFileName)
            {
                return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration,
                                           PDFTranslationContext::tr("User words cannot be used, the path '%1' cannot be passed to the engine.").arg(fileName),
                                           PDFTranslationContext::tr("Initialization"));
            }

            initNames.emplace_back(variable);
            initValues.emplace_back(encodedFileName->constData());
            return PDFOCRError::none();
        };

        if (PDFOCRError error = writeUserFile(configuration.userWords, QStringLiteral("words"), "user_words_file", m_userWordsFile))
        {
            release();
            return error;
        }
        if (PDFOCRError error = writeUserFile(configuration.userPatterns, QStringLiteral("patterns"), "user_patterns_file", m_userPatternsFile))
        {
            release();
            return error;
        }

        // Typed parameters of the schema (REC-03), validated by validateConfiguration above.
        // Parameters influencing the loading of the models are passed to the initialization.
        std::vector<std::pair<QByteArray, QByteArray>> runtimeParameters;
        for (const PDFOCREngineParameterDescriptor& descriptor : getParameterSchema())
        {
            auto it = configuration.engineParameters.find(descriptor.name);
            if (it == configuration.engineParameters.end())
            {
                continue;
            }

            const QByteArray name = descriptor.name.toLatin1();
            const QByteArray value = formatParameterValue(descriptor, it.value());
            if (descriptor.beforeInitialization)
            {
                initNames.emplace_back(name.constData());
                initValues.emplace_back(value.constData());
            }
            else
            {
                runtimeParameters.emplace_back(name, value);
            }
        }

        if (initializeApi(*m_api, models.dataPath, languageString, toEngineMode(configuration.engineMode), initNames, initValues) != 0)
        {
            m_api.reset();
            return PDFOCRError::create(PDFOCRErrorCode::InitializationFailed,
                                       PDFTranslationContext::tr("Tesseract cannot be initialized with the languages '%1' from '%2'.").arg(QString::fromUtf8(languageString), models.dataPath),
                                       PDFTranslationContext::tr("Initialization"));
        }

        m_api->SetPageSegMode(toPageSegMode(configuration.layout));

        // Parameters after the initialization (REC-03)
        QStringList failedParameters;
        auto setVariable = [&](const char* name, const QByteArray& value)
        {
            if (!m_api->SetVariable(name, value.constData()))
            {
                failedParameters << QString::fromLatin1(name);
            }
        };

        switch (configuration.preprocessing.binarization)
        {
            case PDFOCRBinarization::AdaptiveOtsu:
                setVariable("thresholding_method", "1");
                break;
            case PDFOCRBinarization::Sauvola:
                setVariable("thresholding_method", "2");
                break;
            default:
                break;
        }

        if (!configuration.characterWhitelist.isEmpty())
        {
            setVariable("tessedit_char_whitelist", configuration.characterWhitelist.toUtf8());
        }
        if (!configuration.characterBlacklist.isEmpty())
        {
            setVariable("tessedit_char_blacklist", configuration.characterBlacklist.toUtf8());
        }

        // Engine specific typed parameters of the schema (REC-03)
        for (const auto& parameter : runtimeParameters)
        {
            setVariable(parameter.first.constData(), parameter.second);
        }

        if (!failedParameters.isEmpty())
        {
            return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration,
                                       PDFTranslationContext::tr("Parameters '%1' were not accepted by the engine.").arg(failedParameters.join(QStringLiteral(", "))),
                                       PDFTranslationContext::tr("Initialization"));
        }

        m_configuration = configuration;
        m_models = models;
        return PDFOCRError::none();
    }

    /// Orientation detection. Tesseract's DetectOrientationScript has no monitor, so
    /// it can be neither cancelled nor bounded by a deadline while it runs (the
    /// capability orientationDetectionCancellable is false). Its cost is bounded
    /// instead: the cancellation and the deadline are checked before the call and
    /// the image is downscaled, so that its longer side has at most
    /// MaximumOrientationDimension pixels - the orientation does not need the full
    /// resolution. The returned rotation is independent of the scale.
    virtual std::optional<PDFOCROrientation> detectOrientation(const QImage& sourceImage,
                                                               double sourceDpi,
                                                               const PDFOperationControl* operationControl,
                                                               PDFOCRError* error,
                                                               qint64 remainingMilliseconds) override
    {
        if (!m_models.hasOrientationData)
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Orientation data (osd) are not available."), PDFTranslationContext::tr("Orientation detection"));
            }
            return std::nullopt;
        }

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was cancelled."), PDFTranslationContext::tr("Orientation detection"));
            }
            return std::nullopt;
        }

        if (remainingMilliseconds == 0)
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::Timeout, PDFTranslationContext::tr("Recognition exceeded the time limit of the page."), PDFTranslationContext::tr("Orientation detection"));
            }
            return std::nullopt;
        }

        // Bounded cost: the detection runs on a downscaled image (see above)
        QImage image = sourceImage;
        double dpi = sourceDpi;
        const int longerSide = qMax(image.width(), image.height());
        if (longerSide > MaximumOrientationDimension)
        {
            const double scale = double(MaximumOrientationDimension) / double(longerSide);
            image = image.scaled(qMax(1, qRound(image.width() * scale)), qMax(1, qRound(image.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            dpi = qMax(70.0, sourceDpi * scale);
        }

        if (!m_orientationApi)
        {
            m_orientationApi = std::make_unique<tesseract::TessBaseAPI>();
            if (initializeApi(*m_orientationApi, m_models.dataPath, QByteArray("osd"), tesseract::OEM_TESSERACT_ONLY, { }, { }) != 0)
            {
                m_orientationApi.reset();
                if (error)
                {
                    *error = PDFOCRError::create(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("Orientation detection cannot be initialized."), PDFTranslationContext::tr("Orientation detection"));
                }
                return std::nullopt;
            }
            m_orientationApi->SetPageSegMode(tesseract::PSM_OSD_ONLY);
        }

        const QImage grayscale = image.format() == QImage::Format_Grayscale8 ? image : image.convertToFormat(QImage::Format_Grayscale8);
        if (grayscale.isNull())
        {
            return std::nullopt;
        }

        // The orientation detector reads the resolution from the Leptonica image,
        // so the image is passed as a Pix with the resolution set.
        Pix* pix = pixCreate(grayscale.width(), grayscale.height(), 8);
        if (!pix)
        {
            return std::nullopt;
        }

        const int wordsPerLine = pixGetWpl(pix);
        l_uint32* data = pixGetData(pix);
        for (int y = 0; y < grayscale.height(); ++y)
        {
            std::memcpy(data + qint64(y) * wordsPerLine, grayscale.constScanLine(y), size_t(grayscale.width()));
        }
        pixEndianByteSwap(pix);
        pixSetResolution(pix, qRound(dpi), qRound(dpi));

        m_orientationApi->SetImage(pix);
        m_orientationApi->SetSourceResolution(qRound(dpi));
        pixDestroy(&pix);

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            m_orientationApi->Clear();
            return std::nullopt;
        }

        int orientationDegrees = 0;
        float orientationConfidence = 0.0f;
        const char* scriptName = nullptr;
        float scriptConfidence = 0.0f;
        const bool detected = m_orientationApi->DetectOrientationScript(&orientationDegrees, &orientationConfidence, &scriptName, &scriptConfidence);

        PDFOCROrientation orientation;
        if (detected)
        {
            // orient_deg is the detected orientation of the text; the image must
            // be rotated by (360 - orient_deg) degrees clockwise to be upright.
            orientation.rotation = ((360 - orientationDegrees) % 360 + 360) % 360;

            // Tesseract confidence is a positive score without an upper bound (typically
            // 0-30); it is scaled to 0-100 as an indicator, not a probability.
            orientation.confidence = qBound(0.0, double(orientationConfidence) * 5.0, 100.0);
            orientation.script = scriptName ? QString::fromUtf8(scriptName) : QString();
        }

        m_orientationApi->Clear();

        if (!detected)
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::None, PDFTranslationContext::tr("Orientation was not detected (not enough text)."), PDFTranslationContext::tr("Orientation detection"));
            }
            return std::nullopt;
        }

        return orientation;
    }

    virtual PDFOCRRecognitionOutput recognize(const PDFOCRRecognitionInput& input,
                                              const PDFOperationControl* operationControl,
                                              const PDFOCRProgressCallback& progressCallback) override
    {
        PDFOCRRecognitionOutput output;
        output.engineVersion = getVersion();
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        output.rawConfidenceMinimum = 0.0;
        output.rawConfidenceMaximum = 100.0;

        if (!m_api)
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("Engine is not prepared."), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        if (input.image.isNull())
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Input image is empty."), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        // Shared deadline of the page (JOB-06): nothing is started, when no time remains
        qint64 deadlineMilliseconds = input.remainingMilliseconds;
        if (deadlineMilliseconds < 0 && input.configuration.pageTimeoutSeconds > 0)
        {
            deadlineMilliseconds = qint64(input.configuration.pageTimeoutSeconds) * 1000;
        }

        auto createTimeoutError = [&input]()
        {
            return PDFOCRError::create(PDFOCRErrorCode::Timeout,
                                       input.configuration.pageTimeoutSeconds > 0 ? PDFTranslationContext::tr("Recognition exceeded the time limit of %1 s.").arg(input.configuration.pageTimeoutSeconds) : PDFTranslationContext::tr("Recognition exceeded the time limit of the page."),
                                       PDFTranslationContext::tr("Recognition"));
        };

        if (deadlineMilliseconds == 0)
        {
            output.error = createTimeoutError();
            return output;
        }

        // Image data are passed in memory (chapter 3): grayscale or RGB
        QImage image = input.image;
        int bytesPerPixel = 1;
        if (image.format() != QImage::Format_Grayscale8)
        {
            image = image.convertToFormat(QImage::Format_RGB888);
            bytesPerPixel = 3;
        }

        if (image.isNull())
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory, PDFTranslationContext::tr("Cannot convert the input image."), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        // Reset the state of the previous page (ARCH-07)
        m_api->Clear();
        m_api->ClearAdaptiveClassifier();

        m_api->SetImage(image.constBits(), image.width(), image.height(), bytesPerPixel, int(image.bytesPerLine()));
        m_api->SetSourceResolution(qRound(input.dpi));

        if (!input.region.isNull())
        {
            const QRect region = input.region.intersected(image.rect());
            if (region.isEmpty())
            {
                m_api->Clear();
                return output;
            }
            m_api->SetRectangle(region.left(), region.top(), region.width(), region.height());
        }

        MonitorContext context;
        context.operationControl = operationControl;
        context.progressCallback = &progressCallback;

        tesseract::ETEXT_DESC monitor;
        monitor.cancel = &cancelFunction;
        monitor.cancel_this = &context;
        monitor.progress_callback2 = &progressFunction;
        if (deadlineMilliseconds > 0)
        {
            monitor.set_deadline_msecs(int(qMin<qint64>(deadlineMilliseconds, std::numeric_limits<int>::max())));
        }

        QElapsedTimer timer;
        timer.start();

        const int recognitionResult = m_api->Recognize(&monitor);

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            m_api->Clear();
            output.cancelled = true;
            output.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was cancelled."), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        if (monitor.deadline_exceeded() || (deadlineMilliseconds > 0 && timer.elapsed() >= deadlineMilliseconds))
        {
            m_api->Clear();
            output.error = createTimeoutError();
            return output;
        }

        if (recognitionResult != 0)
        {
            m_api->Clear();
            output.error = PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, PDFTranslationContext::tr("Tesseract recognition failed (code %1).").arg(recognitionResult), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        // Results are copied into the independent model before the instance is reused (ARCH-07)
        std::unique_ptr<tesseract::ResultIterator> iterator(m_api->GetIterator());
        if (iterator)
        {
            readResults(iterator.get(), output);
        }

        // Iterator must not outlive the results it points to
        iterator.reset();
        m_api->Clear();

        if (progressCallback)
        {
            progressCallback(100);
        }

        return output;
    }

    virtual void release() override
    {
        if (m_api)
        {
            m_api->End();
            m_api.reset();
        }

        if (m_orientationApi)
        {
            m_orientationApi->End();
            m_orientationApi.reset();
        }

        if (!m_userWordsFile.isEmpty())
        {
            QFile::remove(m_userWordsFile);
            m_userWordsFile.clear();
        }

        if (!m_userPatternsFile.isEmpty())
        {
            QFile::remove(m_userPatternsFile);
            m_userPatternsFile.clear();
        }
    }

private:
    void readResults(tesseract::ResultIterator* iterator, PDFOCRRecognitionOutput& output) const
    {
        iterator->Begin();

        if (iterator->Empty(tesseract::RIL_BLOCK))
        {
            return;
        }

        PDFOCRRawBlock* currentBlock = nullptr;
        PDFOCRRawLine* currentLine = nullptr;

        // The iterator is advanced explicitly at the end of the loop body. A "continue"
        // in a do-while loop would evaluate the condition and advance the iterator once
        // more, so the first word after a non-text block (image, ruling line) would be lost.
        bool hasElement = true;
        while (hasElement)
        {
            if (iterator->IsAtBeginningOf(tesseract::RIL_BLOCK))
            {
                const tesseract::PolyBlockType blockType = iterator->BlockType();
                if (!PTIsTextType(blockType))
                {
                    currentBlock = nullptr;
                    currentLine = nullptr;
                    hasElement = iterator->Next(tesseract::RIL_BLOCK);
                    continue;
                }

                PDFOCRRawBlock block;
                block.rect = boundingBox(iterator, tesseract::RIL_BLOCK);
                block.type = blockType == tesseract::PT_TABLE ? PDFOCRBlockType::Table : PDFOCRBlockType::Text;
                output.blocks.push_back(std::move(block));
                currentBlock = &output.blocks.back();
                currentLine = nullptr;
            }

            if (!currentBlock)
            {
                hasElement = iterator->Next(tesseract::RIL_WORD);
                continue;
            }

            if (iterator->IsAtBeginningOf(tesseract::RIL_TEXTLINE))
            {
                PDFOCRRawLine line;
                line.rect = boundingBox(iterator, tesseract::RIL_TEXTLINE);

                int x1 = 0;
                int y1 = 0;
                int x2 = 0;
                int y2 = 0;
                if (iterator->Baseline(tesseract::RIL_TEXTLINE, &x1, &y1, &x2, &y2))
                {
                    line.baseline = QLineF(x1, y1, x2, y2);
                }

                tesseract::Orientation orientation = tesseract::ORIENTATION_PAGE_UP;
                tesseract::WritingDirection writingDirection = tesseract::WRITING_DIRECTION_LEFT_TO_RIGHT;
                tesseract::TextlineOrder textlineOrder = tesseract::TEXTLINE_ORDER_TOP_TO_BOTTOM;
                float deskewAngle = 0.0f;
                iterator->Orientation(&orientation, &writingDirection, &textlineOrder, &deskewAngle);

                switch (writingDirection)
                {
                    case tesseract::WRITING_DIRECTION_RIGHT_TO_LEFT:
                        line.direction = PDFOCRTextDirection::RightToLeft;
                        break;
                    case tesseract::WRITING_DIRECTION_TOP_TO_BOTTOM:
                        line.direction = PDFOCRTextDirection::TopToBottom;
                        break;
                    default:
                        line.direction = PDFOCRTextDirection::LeftToRight;
                        break;
                }

                if (!iterator->Empty(tesseract::RIL_TEXTLINE))
                {
                    line.rawConfidence = double(iterator->Confidence(tesseract::RIL_TEXTLINE));
                }

                currentBlock->lines.push_back(std::move(line));
                currentLine = &currentBlock->lines.back();
            }

            if (currentLine && !iterator->Empty(tesseract::RIL_WORD))
            {
                PDFOCRRawWord word;
                word.text = takeText(iterator->GetUTF8Text(tesseract::RIL_WORD)).trimmed();
                word.rect = boundingBox(iterator, tesseract::RIL_WORD);
                word.rawConfidence = double(iterator->Confidence(tesseract::RIL_WORD));

                if (const char* language = iterator->WordRecognitionLanguage())
                {
                    word.language = QString::fromUtf8(language);
                }

                // Symbols (optional, ARCH-02)
                tesseract::ResultIterator symbolIterator(*iterator);
                do
                {
                    PDFOCRRawSymbol symbol;
                    symbol.text = takeText(symbolIterator.GetUTF8Text(tesseract::RIL_SYMBOL));
                    symbol.rect = boundingBox(&symbolIterator, tesseract::RIL_SYMBOL);
                    symbol.rawConfidence = double(symbolIterator.Confidence(tesseract::RIL_SYMBOL));
                    if (!symbol.text.isEmpty())
                    {
                        word.symbols.push_back(std::move(symbol));
                    }

                    if (symbolIterator.IsAtFinalElement(tesseract::RIL_WORD, tesseract::RIL_SYMBOL))
                    {
                        break;
                    }
                } while (symbolIterator.Next(tesseract::RIL_SYMBOL));

                // The result iterator delivers the words in the reading (logical) order,
                // also for the right-to-left lines (EDIT-08). Boxes are visual.
                if (!word.text.isEmpty() && word.rect.isValid())
                {
                    currentLine->words.push_back(std::move(word));
                }
            }

            hasElement = iterator->Next(tesseract::RIL_WORD);
        }

        // Remove empty lines and blocks
        for (PDFOCRRawBlock& block : output.blocks)
        {
            std::erase_if(block.lines, [](const PDFOCRRawLine& line) { return line.words.empty(); });
        }
        std::erase_if(output.blocks, [](const PDFOCRRawBlock& block) { return block.lines.empty(); });
    }

    /// Maximal longer side of the image passed to the orientation detection
    static constexpr int MaximumOrientationDimension = 2000;

    const PDFTesseractOCREngineFactory* m_factory;
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
    std::unique_ptr<tesseract::TessBaseAPI> m_orientationApi;
    PDFOCRConfiguration m_configuration;
    PDFOCRResolvedModelSet m_models;
    QString m_userWordsFile;
    QString m_userPatternsFile;
};

// -------------------------------------------------------------------------
// PDFTesseractOCREngineFactory
// -------------------------------------------------------------------------

void PDFTesseractOCREngineFactory::registerEngine()
{
    PDFOCREngineRegistry::getInstance()->registerFactory(std::make_shared<PDFTesseractOCREngineFactory>());
}

QString PDFTesseractOCREngineFactory::getName() const
{
    return QStringLiteral("Tesseract OCR");
}

QString PDFTesseractOCREngineFactory::getVersion() const
{
    return QString::fromLatin1(tesseract::TessBaseAPI::Version());
}

QString PDFTesseractOCREngineFactory::getLicense() const
{
    return QStringLiteral("Apache-2.0");
}

QString PDFTesseractOCREngineFactory::getLeptonicaVersion()
{
    char* version = ::getLeptonicaVersion();
    QString result = version ? QString::fromLatin1(version) : QString();
    if (version)
    {
        lept_free(version);
    }
    return result;
}

bool PDFTesseractOCREngineFactory::isAvailable(QString* reason) const
{
    Q_UNUSED(reason);
    return true;
}

PDFOCREngineCapabilities PDFTesseractOCREngineFactory::getCapabilities() const
{
    PDFOCREngineCapabilities capabilities;
    capabilities.confidenceLevel = PDFOCRConfidenceLevel::Word;
    capabilities.providesWordGeometry = true;
    capabilities.providesSymbolGeometry = true;
    capabilities.providesPolygonGeometry = false;
    capabilities.providesBaselines = true;
    capabilities.supportedLayouts = PDFOCRConfiguration::getLayouts();
    capabilities.supportedEngineModes = { 1, 3 };
    capabilities.supportsOrientationDetection = true;
    capabilities.supportsAlternatives = false;
    capabilities.supportsUserWords = true;
    capabilities.supportsUserPatterns = true;
    capabilities.supportsCharacterRestrictions = true;
    capabilities.supportsCancellation = true;
    capabilities.supportsProgress = true;
    capabilities.supportsMultipleLanguages = true;
    capabilities.supportsEngineBinarization = true;
    capabilities.supportsGpu = false;
    capabilities.maximumImageSize = QSize(32767, 32767);

    // DetectOrientationScript has no monitor, see PDFTesseractOCREngine::detectOrientation
    capabilities.orientationDetectionCancellable = false;
    capabilities.parameters = getParameterSchema();
    return capabilities;
}

std::unique_ptr<PDFOCREngine> PDFTesseractOCREngineFactory::createEngine() const
{
    return std::make_unique<PDFTesseractOCREngine>(this);
}

PDFOCRError PDFTesseractOCREngineFactory::validateModel(const QString& dataPath, const QString& language) const
{
    tesseract::TessBaseAPI api;
    const QByteArray languageCode = language.toUtf8();

    // Orientation data are legacy only, language models are LSTM
    const tesseract::OcrEngineMode mode = language == QStringLiteral("osd") ? tesseract::OEM_TESSERACT_ONLY : tesseract::OEM_LSTM_ONLY;
    const int result = initializeApi(api, dataPath, languageCode, mode, { }, { });
    api.End();

    if (result != 0)
    {
        return PDFOCRError::create(PDFOCRErrorCode::IncompatibleModel,
                                   PDFTranslationContext::tr("Model '%1' cannot be loaded by Tesseract %2.").arg(language, getVersion()),
                                   PDFTranslationContext::tr("Model verification"));
    }

    return PDFOCRError::none();
}

}   // namespace pdf
