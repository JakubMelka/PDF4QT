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

#include "pdfocrengine.h"

#include <QThread>
#include <QElapsedTimer>

namespace pdf
{

// -------------------------------------------------------------------------
// PDFOCREngine
// -------------------------------------------------------------------------

std::optional<PDFOCROrientation> PDFOCREngine::detectOrientation(const QImage& image,
                                                                 double dpi,
                                                                 const PDFOperationControl* operationControl,
                                                                 PDFOCRError* error,
                                                                 qint64 remainingMilliseconds)
{
    Q_UNUSED(image);
    Q_UNUSED(dpi);
    Q_UNUSED(operationControl);
    Q_UNUSED(remainingMilliseconds);

    if (error)
    {
        *error = PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration,
                                     PDFTranslationContext::tr("Orientation detection is not supported by the engine '%1'.").arg(getName()),
                                     PDFTranslationContext::tr("Orientation detection"));
    }

    return std::nullopt;
}

// -------------------------------------------------------------------------
// PDFOCREngineFactory
// -------------------------------------------------------------------------

PDFOCRError PDFOCREngineFactory::validateModel(const QString& dataPath, const QString& language) const
{
    Q_UNUSED(dataPath);
    Q_UNUSED(language);
    return PDFOCRError::none();
}

// -------------------------------------------------------------------------
// PDFOCREngineRegistry
// -------------------------------------------------------------------------

PDFOCREngineRegistry* PDFOCREngineRegistry::getInstance()
{
    static PDFOCREngineRegistry registry;
    return &registry;
}

void PDFOCREngineRegistry::registerFactory(std::shared_ptr<PDFOCREngineFactory> factory)
{
    if (!factory)
    {
        return;
    }

    QMutexLocker lock(&m_mutex);

    const QString identifier = factory->getIdentifier();
    for (auto& existing : m_factories)
    {
        if (existing->getIdentifier() == identifier)
        {
            existing = std::move(factory);
            return;
        }
    }

    m_factories.push_back(std::move(factory));
}

void PDFOCREngineRegistry::unregisterFactory(const QString& identifier)
{
    QMutexLocker lock(&m_mutex);
    std::erase_if(m_factories, [&identifier](const auto& factory) { return factory->getIdentifier() == identifier; });
}

std::vector<std::shared_ptr<PDFOCREngineFactory>> PDFOCREngineRegistry::getFactories() const
{
    QMutexLocker lock(&m_mutex);
    return m_factories;
}

std::shared_ptr<PDFOCREngineFactory> PDFOCREngineRegistry::getFactory(const QString& identifier) const
{
    QMutexLocker lock(&m_mutex);
    for (const auto& factory : m_factories)
    {
        if (factory->getIdentifier() == identifier)
        {
            return factory;
        }
    }
    return nullptr;
}

std::unique_ptr<PDFOCREngine> PDFOCREngineRegistry::createEngine(const QString& identifier) const
{
    if (std::shared_ptr<PDFOCREngineFactory> factory = getFactory(identifier))
    {
        QString reason;
        if (factory->isAvailable(&reason))
        {
            return factory->createEngine();
        }
    }

    return nullptr;
}

bool PDFOCREngineRegistry::hasAvailableEngine() const
{
    for (const auto& factory : getFactories())
    {
        QString reason;
        if (factory->isAvailable(&reason))
        {
            return true;
        }
    }

    return false;
}

// -------------------------------------------------------------------------
// PDFOCRTestEngineFactory
// -------------------------------------------------------------------------

class PDFOCRTestEngine : public PDFOCREngine
{
public:
    explicit PDFOCRTestEngine(const PDFOCRTestEngineFactory* factory) :
        m_factory(factory)
    {

    }

    virtual QString getIdentifier() const override { return m_factory->getIdentifier(); }
    virtual QString getName() const override { return m_factory->getName(); }
    virtual QString getVersion() const override { return m_factory->getVersion(); }
    virtual PDFOCREngineCapabilities getCapabilities() const override { return m_factory->getCapabilities(); }

    virtual PDFOCRError validateConfiguration(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) const override
    {
        Q_UNUSED(models);

        if (configuration.engineId != getIdentifier())
        {
            return PDFOCRError::create(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Configuration belongs to a different engine."));
        }

        return PDFOCRError::none();
    }

    virtual PDFOCRError prepare(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) override
    {
        m_prepared = true;
        return validateConfiguration(configuration, models);
    }

    virtual PDFOCRRecognitionOutput recognize(const PDFOCRRecognitionInput& input,
                                              const PDFOperationControl* operationControl,
                                              const PDFOCRProgressCallback& progressCallback) override
    {
        PDFOCRRecognitionOutput output;
        output.engineVersion = getVersion();
        output.imageSize = input.image.size();

        if (!m_prepared)
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("Engine is not prepared."));
            return output;
        }

        if (input.remainingMilliseconds == 0)
        {
            output.error = PDFOCRError::create(PDFOCRErrorCode::Timeout, PDFTranslationContext::tr("Recognition exceeded the time limit of the page."), PDFTranslationContext::tr("Recognition"));
            return output;
        }

        const int delay = m_factory->getRecognitionDelay();
        if (delay > 0)
        {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < delay)
            {
                if (PDFOperationControl::isOperationCancelled(operationControl))
                {
                    output.cancelled = true;
                    output.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was cancelled."));
                    return output;
                }

                // The deadline of the page is honoured by the simulated recognition (JOB-06)
                if (input.remainingMilliseconds > 0 && timer.elapsed() >= input.remainingMilliseconds)
                {
                    output.error = PDFOCRError::create(PDFOCRErrorCode::Timeout, PDFTranslationContext::tr("Recognition exceeded the time limit of the page."), PDFTranslationContext::tr("Recognition"));
                    return output;
                }

                if (progressCallback)
                {
                    progressCallback(int(100 * timer.elapsed() / delay));
                }

                QThread::msleep(5);
            }
        }

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            output.cancelled = true;
            output.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was cancelled."));
            return output;
        }

        PDFOCRTestEngineFactory::Handler handler = m_factory->getHandler();
        if (handler)
        {
            output = handler(input, operationControl);
            if (output.engineVersion.isEmpty())
            {
                output.engineVersion = getVersion();
            }
            if (output.imageSize.isEmpty())
            {
                output.imageSize = input.image.size();
            }
        }

        if (progressCallback)
        {
            progressCallback(100);
        }

        return output;
    }

    virtual void release() override
    {
        m_prepared = false;
    }

private:
    const PDFOCRTestEngineFactory* m_factory;
    bool m_prepared = false;
};

bool PDFOCRTestEngineFactory::isAvailable(QString* reason) const
{
    Q_UNUSED(reason);
    return true;
}

PDFOCREngineCapabilities PDFOCRTestEngineFactory::getCapabilities() const
{
    PDFOCREngineCapabilities capabilities;
    capabilities.confidenceLevel = PDFOCRConfidenceLevel::Line;
    capabilities.providesWordGeometry = false;
    capabilities.providesPolygonGeometry = true;
    capabilities.supportsCancellation = true;
    capabilities.supportsProgress = true;
    capabilities.supportsMultipleLanguages = true;

    // No parameter schema: the test engine accepts any engine parameter, the tests
    // pass their data (page index, flags) through the parameters to the handler.

    QMutexLocker lock(&m_mutex);
    capabilities.maximumImageSize = m_maximumImageSize;
    return capabilities;
}

std::unique_ptr<PDFOCREngine> PDFOCRTestEngineFactory::createEngine() const
{
    return std::make_unique<PDFOCRTestEngine>(this);
}

PDFOCRError PDFOCRTestEngineFactory::validateModel(const QString& dataPath, const QString& language) const
{
    Q_UNUSED(dataPath);
    Q_UNUSED(language);
    return PDFOCRError::none();
}

void PDFOCRTestEngineFactory::setHandler(Handler handler)
{
    QMutexLocker lock(&m_mutex);
    m_handler = std::move(handler);
}

PDFOCRTestEngineFactory::Handler PDFOCRTestEngineFactory::getHandler() const
{
    QMutexLocker lock(&m_mutex);
    return m_handler;
}

void PDFOCRTestEngineFactory::setRecognitionDelay(int milliseconds)
{
    QMutexLocker lock(&m_mutex);
    m_recognitionDelay = milliseconds;
}

int PDFOCRTestEngineFactory::getRecognitionDelay() const
{
    QMutexLocker lock(&m_mutex);
    return m_recognitionDelay;
}

void PDFOCRTestEngineFactory::setMaximumImageSize(QSize size)
{
    QMutexLocker lock(&m_mutex);
    m_maximumImageSize = size;
}

}   // namespace pdf
