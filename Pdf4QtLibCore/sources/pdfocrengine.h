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

#ifndef PDFOCRENGINE_H
#define PDFOCRENGINE_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrconfiguration.h"
#include "pdfoperationcontrol.h"

#include <QImage>
#include <QMutex>
#include <QVariant>

#include <memory>
#include <vector>
#include <limits>
#include <functional>

namespace pdf
{

/// Descriptor of one engine specific parameter (REC-03). The engine declares
/// a vetted schema of the parameters it accepts; a parameter, which is not
/// declared, is refused by the validation of the configuration.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCREngineParameterDescriptor
{
    enum class Type
    {
        Boolean,
        Integer,
        Double,
        String
    };

    /// Name of the parameter (as used in PDFOCRConfiguration::engineParameters)
    QString name;

    Type type = Type::String;

    /// Range of a numeric parameter (inclusive)
    double minimum = std::numeric_limits<double>::lowest();
    double maximum = std::numeric_limits<double>::max();

    QVariant defaultValue;

    /// Translated description for the user interface
    QString description;

    /// Parameter must be set before the initialization of the engine (it
    /// influences the loading of the models)
    bool beforeInitialization = false;
};

/// Capabilities of the engine (ARCH-02). Optional capabilities are really
/// optional, the engine must not pretend a capability it does not have.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCREngineCapabilities
{
    /// Granularity of the text confidence delivered by the engine
    PDFOCRConfidenceLevel confidenceLevel = PDFOCRConfidenceLevel::Unknown;

    /// Granularity of the geometry: true, if words have own geometry
    bool providesWordGeometry = false;

    /// Engine returns symbol level data
    bool providesSymbolGeometry = false;

    /// Engine returns arbitrary quadrilaterals/polygons (otherwise axis aligned rectangles)
    bool providesPolygonGeometry = false;

    /// Engine returns baselines of the lines
    bool providesBaselines = false;

    /// Supported layouts (page segmentation modes), empty = layout selection not supported
    std::vector<PDFOCRLayout> supportedLayouts;

    /// Supported engine modes, empty = not selectable
    std::vector<int> supportedEngineModes;

    bool supportsOrientationDetection = false;
    bool supportsAlternatives = false;
    bool supportsUserWords = false;
    bool supportsUserPatterns = false;
    bool supportsCharacterRestrictions = false;
    bool supportsCancellation = false;
    bool supportsProgress = false;
    bool supportsMultipleLanguages = false;
    bool supportsEngineBinarization = false;
    bool supportsGpu = false;

    /// Maximal image size (empty = unlimited)
    QSize maximumImageSize;

    /// Engine can be used only for export (text without exact geometry, ENGINE-01)
    bool isExportOnly = false;

    /// Orientation detection honours the cancellation and the deadline while it runs.
    /// If false, the call cannot be interrupted, the engine only bounds its cost
    /// (for example by downscaling the image) and checks the cancellation before it.
    bool orientationDetectionCancellable = false;

    /// Typed schema of the engine specific parameters (REC-03). Parameters not
    /// declared here are refused by the engine.
    std::vector<PDFOCREngineParameterDescriptor> parameters;
};

/// Resolved set of models for one recognition (LANG-05, LANG-06)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRResolvedModelSet
{
    /// Data path with all models of the set (for Tesseract, the "tessdata" directory)
    QString dataPath;

    /// Ordered languages
    QStringList languages;

    /// Model identifiers (e.g. "tesseract/fast/ces")
    QStringList modelIds;

    /// Hash of the set
    QString hash;

    PDFOCRModelProfile profile = PDFOCRModelProfile::Fast;

    /// Orientation and script detection data are available
    bool hasOrientationData = false;

    bool isValid() const { return !dataPath.isEmpty() && !languages.isEmpty(); }
};

/// Raw symbol returned by the engine (optional, ARCH-02)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRawSymbol
{
    QString text;
    QRectF rect;
    std::optional<double> rawConfidence;
};

/// Raw word returned by the engine. Geometry is in the coordinate space
/// of the input image (pixels, y grows downwards).
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRawWord
{
    QString text;

    /// Axis aligned rectangle in image space
    QRectF rect;

    /// Optional polygon (4 points: bottom-left, bottom-right, top-right, top-left
    /// in the writing direction, "bottom" being the baseline side). Empty, if
    /// the engine provides only rectangles.
    QPolygonF polygon;

    std::optional<double> rawConfidence;
    QString language;
    std::vector<PDFOCRRawSymbol> symbols;
};

/// Raw line returned by the engine
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRawLine
{
    QRectF rect;
    QPolygonF polygon;

    /// Baseline in image space (from start to end of the line), null if not available
    QLineF baseline;

    std::optional<double> rawConfidence;
    PDFOCRTextDirection direction = PDFOCRTextDirection::LeftToRight;

    /// Words. If empty, the line text is used as a single text segment.
    std::vector<PDFOCRRawWord> words;

    /// Text of the line (used, when the engine does not provide words)
    QString text;
};

/// Raw block returned by the engine
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRawBlock
{
    QRectF rect;
    PDFOCRBlockType type = PDFOCRBlockType::Text;
    std::vector<PDFOCRRawLine> lines;
};

/// Input of the recognition (ARCH-03)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRecognitionInput
{
    /// Input image (engine space)
    QImage image;

    /// Resolution of the image in DPI
    double dpi = 300.0;

    /// Region of the image to recognize (null = whole image)
    QRect region;

    /// Effective configuration
    PDFOCRConfiguration configuration;

    /// Resolved models
    PDFOCRResolvedModelSet models;

    /// Identifier of the coordinate space of the image (for diagnostics)
    QString coordinateSpace = QStringLiteral("engine");

    /// Time remaining until the deadline of the page in milliseconds (-1 = unlimited).
    /// The deadline is shared by all phases of the page (orientation detection,
    /// preprocessing, every recognized rectangle), so the engine must stop the
    /// recognition, when it is exceeded, and return a Timeout error (JOB-06).
    qint64 remainingMilliseconds = -1;
};

/// Output of the recognition (ARCH-04). Coordinates always belong to
/// the input image (engine space).
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRRecognitionOutput
{
    std::vector<PDFOCRRawBlock> blocks;

    /// Confidence granularity of the output
    PDFOCRConfidenceLevel confidenceLevel = PDFOCRConfidenceLevel::Unknown;

    /// Raw confidence range
    double rawConfidenceMinimum = 0.0;
    double rawConfidenceMaximum = 100.0;

    QString engineVersion;

    /// Size of the image, to which the coordinates belong
    QSize imageSize;

    /// Error (if any)
    PDFOCRError error;

    /// Recognition was cancelled
    bool cancelled = false;

    /// Orientation detected by the engine during the recognition (if any)
    std::optional<PDFOCROrientation> orientation;

    bool isSuccess() const { return !error.isError() && !cancelled; }
};

/// Progress callback (percent 0-100, -1 = indeterminate)
using PDFOCRProgressCallback = std::function<void(int)>;

/// Engine contract (ARCH-01). One engine instance must not be used by two
/// jobs at the same time.
class PDF4QTLIBCORESHARED_EXPORT PDFOCREngine
{
public:
    PDFOCREngine() = default;
    virtual ~PDFOCREngine() = default;

    PDFOCREngine(const PDFOCREngine&) = delete;
    PDFOCREngine& operator=(const PDFOCREngine&) = delete;

    /// Stable engine identifier
    virtual QString getIdentifier() const = 0;

    /// Displayed name
    virtual QString getName() const = 0;

    /// Version of the engine library
    virtual QString getVersion() const = 0;

    virtual PDFOCREngineCapabilities getCapabilities() const = 0;

    /// Validates the configuration (engine specific part). Returns
    /// no error, if configuration is valid.
    virtual PDFOCRError validateConfiguration(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) const = 0;

    /// Prepares the engine instance for recognition with given configuration
    /// and models (loads the models). Must be called before recognize.
    virtual PDFOCRError prepare(const PDFOCRConfiguration& configuration, const PDFOCRResolvedModelSet& models) = 0;

    /// Detects orientation of the image, if supported. Returns empty optional,
    /// if orientation detection is not supported or failed.
    /// \param image Image
    /// \param dpi Resolution of the image
    /// \param operationControl Operation control (cancellation)
    /// \param error Error (if any)
    /// \param remainingMilliseconds Time remaining until the deadline of the page (-1 = unlimited)
    virtual std::optional<PDFOCROrientation> detectOrientation(const QImage& image,
                                                               double dpi,
                                                               const PDFOperationControl* operationControl,
                                                               PDFOCRError* error,
                                                               qint64 remainingMilliseconds = -1);

    /// Recognizes the image. Function must return promptly after the operation
    /// is cancelled (with cancelled flag set in the output).
    virtual PDFOCRRecognitionOutput recognize(const PDFOCRRecognitionInput& input,
                                              const PDFOperationControl* operationControl,
                                              const PDFOCRProgressCallback& progressCallback) = 0;

    /// Releases the resources of the engine instance
    virtual void release() = 0;
};

/// Factory of the engine (registered in the registry)
class PDF4QTLIBCORESHARED_EXPORT PDFOCREngineFactory
{
public:
    PDFOCREngineFactory() = default;
    virtual ~PDFOCREngineFactory() = default;

    /// Stable engine identifier
    virtual QString getIdentifier() const = 0;

    /// Displayed name of the engine
    virtual QString getName() const = 0;

    /// Version of the engine library
    virtual QString getVersion() const = 0;

    /// License of the engine (short text)
    virtual QString getLicense() const { return QString(); }

    /// Returns true, if engine is available. If not, reason is filled.
    virtual bool isAvailable(QString* reason) const = 0;

    virtual PDFOCREngineCapabilities getCapabilities() const = 0;

    /// Creates the engine instance
    virtual std::unique_ptr<PDFOCREngine> createEngine() const = 0;

    /// Validates the model file for the engine (loadability). Data path
    /// is the directory containing the models, language is the language code.
    virtual PDFOCRError validateModel(const QString& dataPath, const QString& language) const;

    /// Returns true, if the engine uses language models managed by the model manager
    virtual bool usesManagedModels() const { return true; }
};

/// Registry of the engines (ARCH-01). Thread safe.
class PDF4QTLIBCORESHARED_EXPORT PDFOCREngineRegistry
{
public:
    static PDFOCREngineRegistry* getInstance();

    /// Registers the factory. Factory with the same identifier is replaced.
    void registerFactory(std::shared_ptr<PDFOCREngineFactory> factory);

    /// Unregisters the factory
    void unregisterFactory(const QString& identifier);

    /// Returns registered factories
    std::vector<std::shared_ptr<PDFOCREngineFactory>> getFactories() const;

    /// Returns factory by identifier, or nullptr
    std::shared_ptr<PDFOCREngineFactory> getFactory(const QString& identifier) const;

    /// Creates engine by identifier, or returns nullptr
    std::unique_ptr<PDFOCREngine> createEngine(const QString& identifier) const;

    /// Returns true, if at least one engine is available
    bool hasAvailableEngine() const;

private:
    PDFOCREngineRegistry() = default;

    mutable QMutex m_mutex;
    std::vector<std::shared_ptr<PDFOCREngineFactory>> m_factories;
};

/// Deterministic test engine (ARCH-05, AT-21, AT-24). It returns results
/// provided by a handler function. It provides only line text, polygons
/// and unknown (or weak) confidence.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRTestEngineFactory : public PDFOCREngineFactory
{
public:
    using Handler = std::function<PDFOCRRecognitionOutput(const PDFOCRRecognitionInput&, const PDFOperationControl*)>;

    static constexpr const char* IDENTIFIER = "test";

    virtual QString getIdentifier() const override { return QLatin1String(IDENTIFIER); }
    virtual QString getName() const override { return QStringLiteral("Test engine"); }
    virtual QString getVersion() const override { return QStringLiteral("1.0"); }
    virtual bool isAvailable(QString* reason) const override;
    virtual PDFOCREngineCapabilities getCapabilities() const override;
    virtual std::unique_ptr<PDFOCREngine> createEngine() const override;
    virtual PDFOCRError validateModel(const QString& dataPath, const QString& language) const override;
    virtual bool usesManagedModels() const override { return false; }

    /// Sets the handler (thread safe)
    void setHandler(Handler handler);
    Handler getHandler() const;

    /// Sets the delay of the recognition in milliseconds (for cancellation tests)
    void setRecognitionDelay(int milliseconds);
    int getRecognitionDelay() const;

    /// Sets the maximal image size declared in the capabilities (empty = unlimited)
    void setMaximumImageSize(QSize size);

private:
    mutable QMutex m_mutex;
    Handler m_handler;
    int m_recognitionDelay = 0;
    QSize m_maximumImageSize;
};

}   // namespace pdf

#endif // PDFOCRENGINE_H
