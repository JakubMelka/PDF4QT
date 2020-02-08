//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFRENDERER_H
#define PDFRENDERER_H

#include "pdfpage.h"
#include "pdfexception.h"
#include "pdfmeshqualitysettings.h"

#include <QImageWriter>
#include <QSurfaceFormat>

class QPainter;
class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFramebufferObject;

namespace pdf
{
class PDFCMS;
class PDFFontCache;
class PDFPrecompiledPage;
class PDFOptionalContentActivity;

/// Renders the PDF page on the painter, or onto an image.
class PDFFORQTLIBSHARED_EXPORT PDFRenderer
{
public:

    enum Feature
    {
        Antialiasing            = 0x0001,   ///< Antialiasing for lines, shapes, etc.
        TextAntialiasing        = 0x0002,   ///< Antialiasing for drawing text
        SmoothImages            = 0x0004,   ///< Adjust images to the device space using smooth transformation (slower, but better image quality)
        IgnoreOptionalContent   = 0x0008,   ///< Ignore optional content (so all is drawn ignoring settings of optional content)
        ClipToCropBox           = 0x0010,   ///< Clip page content to crop box (items outside crop box will not be visible)
        DisplayTimes            = 0x0020,   ///< Display page compile/draw time
        DebugTextBlocks         = 0x0040,   ///< Debug text block layout algorithm
        DebugTextLines          = 0x0080,   ///< Debug text line layout algorithm
        InvertColors            = 0x0100,   ///< Invert colors
    };

    Q_DECLARE_FLAGS(Features, Feature)

    explicit PDFRenderer(const PDFDocument* document,
                         const PDFFontCache* fontCache,
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
    QList<PDFRenderError> render(QPainter* painter, const QMatrix& matrix, size_t pageIndex) const;

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
    static QMatrix createPagePointToDevicePointMatrix(const PDFPage* page, const QRectF& rectangle);

    /// Returns default renderer features
    static constexpr Features getDefaultFeatures() { return Antialiasing | TextAntialiasing | ClipToCropBox; }

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    Features m_features;
    PDFMeshQualitySettings m_meshQualitySettings;
};

/// Renders PDF pages to bitmap images (QImage). It can use OpenGL for painting,
/// if it is enabled, if this is the case, offscreen rendering to framebuffer
/// is used.
class PDFRasterizer : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFRasterizer(QObject* parent);
    ~PDFRasterizer();

    /// Resets the renderer. This function must be called from main GUI thread,
    /// it cannot be called from deferred threads, because it can create hidden
    /// window (offscreen surface).
    /// \param useOpenGL Use OpenGL for rendering
    /// \param surfaceFormat Surface format to render
    void reset(bool useOpenGL, const QSurfaceFormat& surfaceFormat);

    enum Feature
    {
        UseOpenGL               = 0x0001,   ///< Use OpenGL for rendering
        ValidOpenGL             = 0x0002,   ///< OpenGL is initialized and valid
        FailedOpenGL            = 0x0004,   ///< OpenGL creation has failed
    };

    Q_DECLARE_FLAGS(Features, Feature)

    /// Renders page to the image of given size. If some error occurs, then
    /// empty image is returned. Warning: this function can modify this object,
    /// so it is not const and is not thread safe.
    /// \param page Page
    /// \param compiledPage Compiled page contents
    /// \param size Size of the target image
    /// \param features Renderer features
    QImage render(const PDFPage* page,
                  const PDFPrecompiledPage* compiledPage,
                  QSize size,
                  PDFRenderer::Features features);

private:
    void initializeOpenGL();
    void releaseOpenGL();

    Features m_features;
    QSurfaceFormat m_surfaceFormat;
    QOffscreenSurface* m_surface;
    QOpenGLContext* m_context;
    QOpenGLFramebufferObject* m_fbo;
};

/// Settings object for image writer
class PDFFORQTLIBSHARED_EXPORT PDFImageWriterSettings
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
class PDFFORQTLIBSHARED_EXPORT PDFPageImageExportSettings
{
public:
    explicit PDFPageImageExportSettings();

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

private:
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
