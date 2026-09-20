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
        const QByteArray dataPath = QDir::toNativeSeparators(models.dataPath).toUtf8();

        if (m_api->Init(dataPath.constData(), languageString.constData(), toEngineMode(configuration.engineMode)) != 0)
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

        // User words and patterns are written into the managed working storage (ARCH-03)
        if (!configuration.userWords.isEmpty() || !configuration.userPatterns.isEmpty())
        {
            const QString workingDirectory = QDir(models.dataPath).absolutePath() + QStringLiteral("/../user");
            QDir().mkpath(workingDirectory);

            if (!configuration.userWords.isEmpty())
            {
                m_userWordsFile = QDir(workingDirectory).absoluteFilePath(QStringLiteral("words-%1.txt").arg(QUuid::createUuid().toString(QUuid::Id128).left(8)));
                QFile file(m_userWordsFile);
                if (file.open(QFile::WriteOnly | QFile::Truncate))
                {
                    file.write(configuration.userWords.join(QChar('\n')).toUtf8());
                    file.close();
                    setVariable("user_words_file", QDir::toNativeSeparators(m_userWordsFile).toUtf8());
                }
            }

            if (!configuration.userPatterns.isEmpty())
            {
                m_userPatternsFile = QDir(workingDirectory).absoluteFilePath(QStringLiteral("patterns-%1.txt").arg(QUuid::createUuid().toString(QUuid::Id128).left(8)));
                QFile file(m_userPatternsFile);
                if (file.open(QFile::WriteOnly | QFile::Truncate))
                {
                    file.write(configuration.userPatterns.join(QChar('\n')).toUtf8());
                    file.close();
                    setVariable("user_patterns_file", QDir::toNativeSeparators(m_userPatternsFile).toUtf8());
                }
            }
        }

        // Engine specific typed parameters (validated names only)
        for (auto it = configuration.engineParameters.begin(); it != configuration.engineParameters.end(); ++it)
        {
            const QByteArray name = it.key().toLatin1();
            if (name.isEmpty() || name.contains(' ') || name.startsWith("debug"))
            {
                continue;
            }
            setVariable(name.constData(), it.value().toString().toUtf8());
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

    virtual std::optional<PDFOCROrientation> detectOrientation(const QImage& image,
                                                               double dpi,
                                                               const PDFOperationControl* operationControl,
                                                               PDFOCRError* error) override
    {
        if (!m_models.hasOrientationData)
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("Orientation data (osd) are not available."), PDFTranslationContext::tr("Orientation detection"));
            }
            return std::nullopt;
        }

        if (!m_orientationApi)
        {
            m_orientationApi = std::make_unique<tesseract::TessBaseAPI>();
            const QByteArray dataPath = QDir::toNativeSeparators(m_models.dataPath).toUtf8();
            if (m_orientationApi->Init(dataPath.constData(), "osd", tesseract::OEM_TESSERACT_ONLY) != 0)
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
        if (input.configuration.pageTimeoutSeconds > 0)
        {
            monitor.set_deadline_msecs(input.configuration.pageTimeoutSeconds * 1000);
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

        if (monitor.deadline_exceeded())
        {
            m_api->Clear();
            output.error = PDFOCRError::create(PDFOCRErrorCode::Timeout, PDFTranslationContext::tr("Recognition exceeded the time limit of %1 s.").arg(input.configuration.pageTimeoutSeconds), PDFTranslationContext::tr("Recognition"));
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

        do
        {
            if (iterator->IsAtBeginningOf(tesseract::RIL_BLOCK))
            {
                const tesseract::PolyBlockType blockType = iterator->BlockType();
                if (!PTIsTextType(blockType))
                {
                    currentBlock = nullptr;
                    currentLine = nullptr;
                    if (!iterator->Next(tesseract::RIL_BLOCK))
                    {
                        break;
                    }
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
                if (!iterator->Next(tesseract::RIL_WORD))
                {
                    break;
                }
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

                // Right-to-left words are delivered in the visual order by the iterator;
                // the logical order is the reading order, so RTL lines are reversed
                // after the line is complete (EDIT-08).
                if (!word.text.isEmpty() && word.rect.isValid())
                {
                    currentLine->words.push_back(std::move(word));
                }
            }
        } while (iterator->Next(tesseract::RIL_WORD));

        // Remove empty lines and blocks
        for (PDFOCRRawBlock& block : output.blocks)
        {
            std::erase_if(block.lines, [](const PDFOCRRawLine& line) { return line.words.empty(); });
        }
        std::erase_if(output.blocks, [](const PDFOCRRawBlock& block) { return block.lines.empty(); });
    }

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
    return capabilities;
}

std::unique_ptr<PDFOCREngine> PDFTesseractOCREngineFactory::createEngine() const
{
    return std::make_unique<PDFTesseractOCREngine>(this);
}

PDFOCRError PDFTesseractOCREngineFactory::validateModel(const QString& dataPath, const QString& language) const
{
    tesseract::TessBaseAPI api;
    const QByteArray nativeDataPath = QDir::toNativeSeparators(dataPath).toUtf8();
    const QByteArray languageCode = language.toUtf8();

    // Orientation data are legacy only, language models are LSTM
    const tesseract::OcrEngineMode mode = language == QStringLiteral("osd") ? tesseract::OEM_TESSERACT_ONLY : tesseract::OEM_LSTM_ONLY;
    const int result = api.Init(nativeDataPath.constData(), languageCode.constData(), mode);
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
