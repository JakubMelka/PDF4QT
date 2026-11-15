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

#ifndef PDFRENDERER_H
#define PDFRENDERER_H

#include "pdfpage.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"
#include "pdfmeshqualitysettings.h"
#include "pdfutils.h"
#include "pdfcolorconvertor.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QMutex>
#include <QSemaphore>
#include <QImageWriter>
#include <QImage>
#include <QString>
#include <QWaitCondition>

class QPainter;

namespace pdf
{
class PDFCMS;
class PDFFontCache;
class PDFICCProfileCache;
class PDFCMSManager;
class PDFPrecompiledPage;
class PDFAnnotationManager;
class PDFOptionalContentActivity;

/// Renders the PDF page on the painter, or onto an image.
class PDF4QTLIBCORESHARED_EXPORT PDFRenderer
{
public:

    enum Feature
    {
        None                        = 0x0000,
        Antialiasing                = 0x0001,   ///< Antialiasing for lines, shapes, etc.
        TextAntialiasing            = 0x0002,   ///< Antialiasing for drawing text
        SmoothImages                = 0x0004,   ///< Adjust images to the device space using smooth transformation (slower, but better image quality). Bitonal images drawn onto a raster device are adjusted always.
        IgnoreOptionalContent       = 0x0008,   ///< Ignore optional content (so all is drawn ignoring settings of optional content)
        ClipToCropBox               = 0x0010,   ///< Clip page content to crop box (items outside crop box will not be visible)
        DisplayTimes                = 0x0020,   ///< Display page compile/draw time
        DebugTextBlocks             = 0x0040,   ///< Debug text block layout algorithm
        DebugTextLines              = 0x0080,   ///< Debug text line layout algorithm
        DenyExtraGraphics           = 0x0100,   ///< Do not display additional graphics, for example from tools
        DisplayAnnotations          = 0x0200,   ///< Display annotations
        LogicalSizeZooming          = 0x0400,   ///< Use logical pixel resolution instead of physical one when zooming

        ColorAdjust_Invert          = 0x0800,   ///< Invert colors
        ColorAdjust_Grayscale       = 0x1000,   ///< Convert colors to grayscale
        ColorAdjust_HighContrast    = 0x2000,   ///< Convert colors to high constrast colors
        ColorAdjust_Bitonal         = 0x4000,   ///< Convert colors to bitonal (monochromatic)
        ColorAdjust_CustomColors    = 0x8000,   ///< Convert colors to custom color settings

        RealText                    = 0x10000,  ///< Draw text using QPainter::drawText instead of vector paths, where possible

        ColorAdjust_Annotations     = 0x20000,  ///< Apply active color adjustment also to annotations, not only to the page content
    };

    Q_DECLARE_FLAGS(Features, Feature)

    explicit PDFRenderer(const PDFDocument* document,
                         const PDFFontCache* fontCache,
                         const PDFICCProfileCache* iccProfileCache,
                         const PDFCMS* cms,
                         const PDFOptionalContentActivity* optionalContentActivity,
                         Features features,
                         const PDFMeshQualitySettings& meshQualitySettings);

    /// Paints desired page onto the painter. Page is painted in the rectangle using best-fit method.
    /// If the page doesn't exist, then error is returned. No exception is thrown. Rendering errors
    /// are reported and returned in the error list. If no error occured, empty list is returned.
    /// \param painter Painter
    /// \param rectangle Paint area for the page
    /// \param pageIndex Index of the page to be painted
    QList<PDFRenderError> render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const;

    /// Paints desired page onto the painter. Page is painted using \p matrix, which maps page coordinates
    /// to the device coordinates. If the page doesn't exist, then error is returned. No exception is thrown.
    /// Rendering errors are reported and returned in the error list. If no error occured, empty list is returned.
    QList<PDFRenderError> render(QPainter* painter, const QTransform& matrix, size_t pageIndex) const;

    /// Compiles page (i.e. prepares compiled page). \p page should be empty page, onto which
    /// are graphics commands written. No exception is thrown. Rendering errors are reported and written
    /// to the compiled page.
    /// \param precompiledPage Precompiled page pointer
    /// \param pageIndex Index of page to be compiled
    void compile(PDFPrecompiledPage* precompiledPage, size_t pageIndex) const;

    /// Creates page point to device point matrix for the given rectangle. It creates transformation
    /// from page's media box to the target rectangle.
    /// \param page Page, for which we want to create matrix
    /// \param rectangle Page rectangle, to which is page media box transformed
    /// \param extraRotation Extra rotation applied to the page rotation
    static QTransform createPagePointToDevicePointMatrix(const PDFPage* page,
                                                         const QRectF& rectangle,
                                                         PageRotation extraRotation = PageRotation::None);

    /// Creates media box to device point matrix for the given media box.
    /// \param mediaBox Media box
    /// \param rectangle Page rectangle, to which is page media box transformed
    /// \param rotation Rotation
    static QTransform createMediaBoxToDevicePointMatrix(const QRectF& mediaBox,
                                                        const QRectF& rectangle,
                                                        PageRotation rotation);

    /// Applies rendering flags to the color convertor
    static void applyFeaturesToColorConvertor(const Features& features, PDFColorConvertor& convertor);

    /// Applies rendering flags to the color convertor, which is used to draw annotations.
    /// Annotations are adjusted only when the feature ColorAdjust_Annotations is turned on,
    /// otherwise the convertor is deactivated and annotations keep their original colors.
    /// \param features Features
    /// \param convertor Color convertor
    static void applyFeaturesToAnnotationColorConvertor(const Features& features, PDFColorConvertor& convertor);

    /// Returns default renderer features
    static constexpr Features getDefaultFeatures() { return Features(Antialiasing | TextAntialiasing | ClipToCropBox | DisplayAnnotations | ColorAdjust_Annotations); }

    /// Returns color transformation features. These features are mutually exclusive - they
    /// select the color conversion mode. ColorAdjust_Annotations is deliberately not a part
    /// of this set, because it is a modifier of the selected mode, not a mode itself.
    static constexpr Features getColorFeatures() { return Features(ColorAdjust_Invert | ColorAdjust_Grayscale | ColorAdjust_HighContrast | ColorAdjust_Bitonal | ColorAdjust_CustomColors); }

    const PDFOperationControl* getOperationControl() const;
    void setOperationControl(const PDFOperationControl* newOperationControl);

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFOperationControl* m_operationControl;
    Features m_features;
    PDFMeshQualitySettings m_meshQualitySettings;
};

/// Renders PDF pages to bitmap images (QImage). It holds only the kind of its engine: it draws
/// into a QImage (Blend2D or the raster engine of QPainter), which works in any thread.
/// \note A rasterizer belongs to its PDFRasterizerPool (a child object of it, in the thread of the
///       pool) and is used by one thread at a time - the one, which has acquired it. (The note
///       "construct this object only in main GUI thread" of older versions did not hold: nothing of
///       it needs the GUI thread; step 4 of phase 2, R4-16.)
class PDF4QTLIBCORESHARED_EXPORT PDFRasterizer : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFRasterizer(QObject* parent);
    virtual ~PDFRasterizer() override;

    /// Resets the renderer.
    /// \param rendererEngine Renderer engine type
    void reset(RendererEngine rendererEngine);

    /// Renders page to the image of given size. If some error occurs, then
    /// empty image is returned. Warning: this function can modify this object,
    /// so it is not const and is not thread safe. We can also draw annotations,
    /// if \p annotationManager is not nullptr and annotations are enabled.
    /// \param pageIndex Page index
    /// \param page Page
    /// \param compiledPage Compiled page contents
    /// \param size Size of the target image
    /// \param features Renderer features
    /// \param annotationManager Annotation manager (can be nullptr)
    /// \param cms Color management system
    /// \param extraRotation Extra page rotation
    QImage render(PDFInteger pageIndex,
                  const PDFPage* page,
                  const PDFPrecompiledPage* compiledPage,
                  QSize size,
                  PDFRenderer::Features features,
                  const PDFAnnotationManager* annotationManager,
                  const PDFCMS* cms,
                  PageRotation extraRotation);

private:
    RendererEngine m_rendererEngine;
};

/// Simple structure for storing rendered page images
struct PDF4QTLIBCORESHARED_EXPORT PDFRenderedPageImage
{
    qint64 pageCompileTime = 0;
    qint64 pageWaitTime = 0;
    qint64 pageRenderTime = 0;
    qint64 pageTotalTime = 0;
    PDFInteger pageIndex;
    QImage pageImage;

    /// Errors reported while the page has been compiled. The renderer does not
    /// stop at an error - the content, which cannot be processed, is skipped and
    /// the rest of the page is rendered - so the image can be incomplete even
    /// when it is not null. \sa hasSevereError
    QList<PDFRenderError> errors;

    /// Returns true, if the errors indicate, that a part of the content of the page
    /// is missing in the image - an error, or a feature, which is not implemented.
    /// Warnings and unsupported features, which are rendered approximately, do not
    /// count.
    bool hasSevereError() const;

    /// The errors a tool reports as the problems of the page (Rendering issues of the result
    /// dialog, step 4 of phase 2, R4-16): every error except the level Information
    QList<PDFRenderError> getReportedErrors() const;
};

/// What the encoder of an ordered rendering made of one page (PDFRasterizerPool::renderOrdered):
/// the bytes of the page (an encoded image file) and the errors of the page. A value.
struct PDFRenderedPageOutput
{
    PDFInteger pageIndex = -1;      ///< Page (index from 0)
    bool isRendered = false;        ///< The page was rendered and encoded (false: the page does not exist, its image could not be allocated or the encoder failed)
    QByteArray data;                ///< The bytes of the encoder (empty, when the page was not rendered)
    QString errorMessage;           ///< Translated reason of a page, which was not rendered (empty otherwise)
    QList<PDFRenderError> errors;   ///< Errors of the compilation of the page (PDFRenderedPageImage::errors)
};

/// Pool of page image renderers. It can use predefined number of renderers to
/// render page images asynchronously. You can use this object in two ways -
/// first one is as standard object pool, second one is to directly render
/// page images asynchronously.
///
/// Threads: the pool may be created in any thread (the GUI, the run of a tool, PdfTool); it and
/// its rasterizers belong to that thread and are deleted there. render and renderOrdered run in
/// the calling thread and in the threads of PDFExecutionPolicy (Scope::Page). The signal
/// renderError is emitted from those threads: a tool never connects to it, it reads the errors
/// of each page (PDFRenderedPageImage::errors, PDFRenderedPageOutput::errors; R4-16).
///
/// Memory (owner answer Q4.7 of step 4 of phase 2, R4-16): besides the semaphore of the
/// rasterizers, the pool has a budget of the bytes of the page images, which exist at once
/// (getImageDataBudget, getDefaultImageDataBudget = a quarter of the physical memory, at least
/// 2 GiB, owner 14. 11.): a page waits, until
/// its image fits into the budget; a page larger than the whole budget runs alone (it waits,
/// until no other image exists). The bytes of a page are taken before its rasterization and
/// returned after its processImage (render) or its encoder (renderOrdered).
class PDF4QTLIBCORESHARED_EXPORT PDFRasterizerPool : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Smallest default budget of the page images existing at once (owner 14. 11.: a quarter of
    /// the physical memory, at least 2 GiB; 8 GB → 2 GiB, 16 GB → 4 GiB, 64 GB → 16 GiB)
    static constexpr qint64 MINIMUM_DEFAULT_IMAGE_DATA_BUDGET = qint64(2) * 1024 * 1024 * 1024;

    /// Default budget, when the size of the physical memory is not known (Q4.7 of step 4)
    static constexpr qint64 UNKNOWN_MEMORY_IMAGE_DATA_BUDGET = qint64(4) * 1024 * 1024 * 1024;

    /// Default budget of the page images existing at once: a quarter of the physical memory,
    /// at least MINIMUM_DEFAULT_IMAGE_DATA_BUDGET; UNKNOWN_MEMORY_IMAGE_DATA_BUDGET when the
    /// physical memory is not known (\p physicalMemory 0)
    static qint64 getDefaultImageDataBudget(qint64 physicalMemory);

    /// Default budget of the page images of this computer (PDFSysUtils::getPhysicalMemorySize)
    static qint64 getDefaultImageDataBudget();

    /// Bytes of a pixel of a rendered page (QImage::Format_ARGB32_Premultiplied)
    static constexpr qint64 BYTES_PER_PIXEL = 4;

    /// Interval of the checks of the cancellation of a waiting thread (ms)
    static constexpr int WAIT_POLL_INTERVAL = 20;

    using PageImageSizeGetter = std::function<QSize(const PDFPage*)>;
    using ProcessImageMethod = std::function<void(PDFRenderedPageImage&)>;

    /// Encoder of an ordered rendering: called in the threads of the pool, more pages at once; it
    /// returns the bytes of the page (an image file) and may set \p errorMessage (a page, which
    /// cannot be encoded: empty bytes). It must be thread-safe and must not keep the image.
    using EncodeImageMethod = std::function<QByteArray(const PDFRenderedPageImage& image, QString* errorMessage)>;

    /// Writer of an ordered rendering: called for every page, which was not skipped by the
    /// cancellation, in the order of the page indices of the call, one call at a time (each call
    /// happens after the previous one under the mutex of the pool, in any thread of the pool).
    using WriteOutputMethod = std::function<void(PDFRenderedPageOutput& output)>;

    /// Creates new rasterizer pool
    /// \param document Document
    /// \param fontCache Font cache
    /// \param iccProfileCache Cache of the decoded ICC profiles of the document (can be nullptr)
    /// \param cmsManager Color management system manager
    /// \param optionalContentActivity Optional content activity
    /// \param features Renderer features
    /// \param meshQualitySettings Mesh quality settings
    /// \param rasterizerCount Number of rasterizers
    /// \param rendererEngine Renderer engine
    /// \param parent Parent object
    explicit PDFRasterizerPool(const PDFDocument* document,
                               PDFFontCache* fontCache,
                               const PDFICCProfileCache* iccProfileCache,
                               const PDFCMSManager* cmsManager,
                               const PDFOptionalContentActivity* optionalContentActivity,
                               PDFRenderer::Features features,
                               const PDFMeshQualitySettings& meshQualitySettings,
                               int rasterizerCount,
                               RendererEngine rendererEngine,
                               QObject* parent);

    /// Acquire rasterizer. This function is thread safe. The function blocks
    /// until a rasterizer is free, unless the operation is canceled meanwhile -
    /// then nullptr is returned and nothing has to be released.
    /// \param operationControl Operation control (can be nullptr)
    PDFRasterizer* acquire(const PDFOperationControl* operationControl = nullptr);

    /// Return back (release) rasterizer into rasterizer pool
    /// This function is thread safe.
    /// \param rasterizer Rasterizer
    void release(PDFRasterizer* rasterizer);

    /// Renders pages asynchronously to images, using given page indices,
    /// function which returns rendered size and process image function,
    /// which processes rendered images. Rendering can be interrupted using
    /// the operation control - pages, which have not been rendered yet, are
    /// skipped and the process image method is not called for them. The
    /// cancellation is checked before and after the compilation of the page,
    /// while a free rasterizer is being waited for, and after the rasterization.
    /// Errors of the compilation are passed to the process image method, so it
    /// can find out, whether the image is complete.
    /// \param pageIndices Page indices for rendered pages
    /// \param imageSizeGetter Getter, which computes image size from page index
    /// \param processImage Method, which processes rendered page images
    /// \param progress Progress indicator
    /// \param operationControl Operation control (can be nullptr)
    void render(const std::vector<PDFInteger>& pageIndices,
                const PageImageSizeGetter& imageSizeGetter,
                const ProcessImageMethod& processImage,
                PDFProgressSink* progress,
                const PDFOperationControl* operationControl = nullptr);

    /// Renders the pages in the threads of the pool and hands their encoded bytes to the writer in
    /// the order of \p pageIndices (step 4 of phase 2, R4-16: PDF to Images writes its files in the
    /// order of the pages, Black & White Pages its images). The encoded pages, which wait for an
    /// earlier page, are bounded by the reorder window (getReorderWindowSize, 2 x the rasterizers):
    /// a page does not start, while it is that far after the next page to be written - its thread
    /// waits (PDFThreadAnnotations::wait) and checks the cancellation. A page, which does not
    /// exist, whose image cannot be allocated or which the encoder refuses, comes to the writer
    /// with isRendered false and its reason, so the order goes on. The cancellation skips the
    /// remaining pages: the writer is not called for them. renderError is not emitted. The
    /// progress gets a step per page.
    /// \param pageIndices Pages in the order of the writing (each page once)
    /// \param imageSizeGetter Size of the image of a page (called before its rasterization)
    /// \param encode Encoder (see EncodeImageMethod)
    /// \param write Writer (see WriteOutputMethod)
    /// \param progress Progress (optional)
    /// \param operationControl Cancellation (optional)
    void renderOrdered(const std::vector<PDFInteger>& pageIndices,
                       const PageImageSizeGetter& imageSizeGetter,
                       const EncodeImageMethod& encode,
                       const WriteOutputMethod& write,
                       PDFProgressSink* progress,
                       const PDFOperationControl* operationControl = nullptr);

    /// The budget of the bytes of the page images existing at once (see the class); a value
    /// smaller than 1 is 1. Set it before a rendering.
    void setImageDataBudget(qint64 budget);
    qint64 getImageDataBudget() const { return m_imageDataBudget; }

    /// The number of rasterizers of the pool
    int getRasterizerCount() const { return m_rasterizerCount; }

    /// Most encoded pages of renderOrdered, which exist at once ahead of the next written page:
    /// 2 x the rasterizers
    int getReorderWindowSize() const { return 2 * m_rasterizerCount; }

    /// Bytes of the image of a page of the size (BYTES_PER_PIXEL), 0 for an empty size
    static qint64 getImageDataSize(QSize size);

    /// Returns default rasterizer count
    static int getDefaultRasterizerCount();

    /// Returns corrected rasterizer count (so, if user
    /// select too high or too low rasterizer count, this function
    /// corrects it to acceptable number.
    /// \param rasterizerCount Requested number of rasterizers
    /// \returns Corrected number of rasterizers
    static int getCorrectedRasterizerCount(int rasterizerCount);

signals:
    void renderError(PDFInteger pageIndex, PDFRenderError error);

private:
    class OrderedState;

    /// Takes \p bytes of the budget: waits, until they fit (or until no image exists, when they
    /// are more than the whole budget). \returns false (nothing taken), when the operation has
    /// been canceled meanwhile.
    bool acquireImageData(qint64 bytes, const PDFOperationControl* operationControl);

    /// Returns bytes taken by acquireImageData
    void releaseImageData(qint64 bytes);

    /// What has happened with a page of renderPage
    enum class PageStatus
    {
        Processed,  ///< processImage got the image (a null image, when it could not be allocated)
        Missing,    ///< The page does not exist
        Canceled    ///< The cancellation skipped the page
    };

    /// Compiles and rasterizes one page with its share of the budget held: \p processImage gets
    /// the image in the thread of the call, the bytes of the budget are returned after it.
    /// \param isEmittingErrors The signal renderError tells the errors (render only)
    PageStatus renderPage(PDFInteger pageIndex, const PageImageSizeGetter& imageSizeGetter, const ProcessImageMethod& processImage,
                          const PDFOperationControl* operationControl, bool isEmittingErrors);

    const PDFDocument* m_document;
    PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMSManager* m_cmsManager;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    PDFRenderer::Features m_features;
    PDFMeshQualitySettings m_meshQualitySettings;   ///< A copy: the pool never refers to the settings of its creator

    QSemaphore m_semaphore;
    QMutex m_mutex;
    std::vector<PDFRasterizer*> m_rasterizers;
    int m_rasterizerCount = 1;

    QMutex m_imageDataMutex;
    QWaitCondition m_imageDataCondition;
    qint64 m_imageDataBudget = getDefaultImageDataBudget();
    qint64 m_imageDataUsed = 0;     ///< Bytes of the page images, which exist now (guarded by m_imageDataMutex)
    int m_largePagesWaiting = 0;    ///< Pages larger than the budget, which wait to run alone (guarded by m_imageDataMutex)
};

/// Settings object for image writer
class PDF4QTLIBCORESHARED_EXPORT PDFImageWriterSettings
{
public:
    explicit PDFImageWriterSettings();

    /// Returns true, if image option is supported
    bool isOptionSupported(QImageIOHandler::ImageOption option) const { return m_supportedOptions.count(option); }

    /// Returns a list of available image formats
    const QList<QByteArray>& getFormats() const { return m_formats; }

    /// Returns a list of available subtypes
    const QList<QByteArray>& getSubtypes() const { return m_subtypes; }

    /// Selects image format (and initializes default values)
    void selectFormat(const QByteArray& format);

    int getCompression() const;
    void setCompression(int compression);

    int getQuality() const;
    void setQuality(int quality);

    float getGamma() const;
    void setGamma(float gamma);

    bool hasOptimizedWrite() const;
    void setOptimizedWrite(bool optimizedWrite);

    bool hasProgressiveScanWrite() const;
    void setProgressiveScanWrite(bool progressiveScanWrite);

    QByteArray getCurrentFormat() const;

    QByteArray getCurrentSubtype() const;
    void setCurrentSubtype(const QByteArray& currentSubtype);

private:
    int m_compression = 9;
    int m_quality = 100;
    float m_gamma = 1.0;
    bool m_optimizedWrite = false;
    bool m_progressiveScanWrite = false;
    QByteArray m_currentFormat;
    QByteArray m_currentSubtype;
    std::set<QImageIOHandler::ImageOption> m_supportedOptions;

    QList<QByteArray> m_formats;
    QList<QByteArray> m_subtypes;
};

/// This class is for setup of page image exporter
class PDF4QTLIBCORESHARED_EXPORT PDFPageImageExportSettings
{
public:
    explicit PDFPageImageExportSettings() : PDFPageImageExportSettings(nullptr) { }
    explicit PDFPageImageExportSettings(const PDFDocument* document);

    enum class PageSelectionMode
    {
        All,
        Selection
    };

    enum class ResolutionMode
    {
        DPI,
        Pixels
    };

    ResolutionMode getResolutionMode() const;
    void setResolutionMode(ResolutionMode resolution);

    PageSelectionMode getPageSelectionMode() const;
    void setPageSelectionMode(PageSelectionMode pageSelectionMode);

    QString getDirectory() const;
    void setDirectory(const QString& directory);

    QString getFileTemplate() const;
    void setFileTemplate(const QString& fileTemplate);

    QString getPageSelection() const;
    void setPageSelection(const QString& pageSelection);

    int getDpiResolution() const;
    void setDpiResolution(int dpiResolution);

    int getPixelResolution() const;
    void setPixelResolution(int pixelResolution);

    /// Validates the settings, if they can be used for image generation
    bool validate(QString* errorMessagePtr, bool validatePageSelection = true, bool validateFileSettings = true, bool validateResolution = true) const;

    /// Returns list of selected pages
    std::vector<PDFInteger> getPages() const;

    /// Returns output file name for given page
    QString getOutputFileName(PDFInteger pageIndex, const QByteArray& outputFormat) const;

    static constexpr int getMinDPIResolution() { return 72; }
    static constexpr int getMaxDPIResolution() { return 6000; }

    static constexpr int getMinPixelResolution() { return 100; }
    static constexpr int getMaxPixelResolution() { return 16384; }

private:
    const PDFDocument* m_document;
    ResolutionMode m_resolutionMode = ResolutionMode::DPI;
    PageSelectionMode m_pageSelectionMode = PageSelectionMode::All;
    QString m_directory;
    QString m_fileTemplate;
    QString m_pageSelection;
    int m_dpiResolution = 300;
    int m_pixelResolution = 100;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRenderer::Features)

#endif // PDFRENDERER_H
