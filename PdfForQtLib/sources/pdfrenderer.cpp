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

#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdfdocument.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLPaintDevice>
#include <QOpenGLFramebufferObject>

namespace pdf
{

PDFRenderer::PDFRenderer(const PDFDocument* document,
                         const PDFFontCache* fontCache,
                         const PDFCMS* cms,
                         const PDFOptionalContentActivity* optionalContentActivity,
                         Features features,
                         const PDFMeshQualitySettings& meshQualitySettings) :
    m_document(document),
    m_fontCache(fontCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_features(features),
    m_meshQualitySettings(meshQualitySettings)
{
    Q_ASSERT(document);
}

QMatrix PDFRenderer::createPagePointToDevicePointMatrix(const PDFPage* page, const QRectF& rectangle)
{
    QRectF mediaBox = page->getRotatedMediaBox();

    QMatrix matrix;
    switch (page->getPageRotation())
    {
        case PageRotation::None:
        {
            matrix.translate(rectangle.left(), rectangle.bottom());
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            break;
        }

        case PageRotation::Rotate90:
        {
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.rotate(90);
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            break;
        }

        case PageRotation::Rotate270:
        {
            matrix.translate(rectangle.right(), rectangle.top());
            matrix.rotate(-90);
            matrix.translate(-rectangle.height(), 0);
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            break;
        }

        case PageRotation::Rotate180:
        {
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.scale(rectangle.width() / mediaBox.width(), rectangle.height() / mediaBox.height());
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return matrix;
}

QList<PDFRenderError> PDFRenderer::render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const
{
    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex >= catalog->getPageCount() || !catalog->getPage(pageIndex))
    {
        // Invalid page index
        return { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1)) };
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    Q_ASSERT(page);

    QMatrix matrix = createPagePointToDevicePointMatrix(page, rectangle);

    PDFPainter processor(painter, m_features, matrix, page, m_document, m_fontCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    return processor.processContents();
}

QList<PDFRenderError> PDFRenderer::render(QPainter* painter, const QMatrix& matrix, size_t pageIndex) const
{
    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex >= catalog->getPageCount() || !catalog->getPage(pageIndex))
    {
        // Invalid page index
        return { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1)) };
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    Q_ASSERT(page);

    PDFPainter processor(painter, m_features, matrix, page, m_document, m_fontCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    return processor.processContents();
}

void PDFRenderer::compile(PDFPrecompiledPage* precompiledPage, size_t pageIndex) const
{
    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex >= catalog->getPageCount() || !catalog->getPage(pageIndex))
    {
        // Invalid page index
        precompiledPage->finalize(0, { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1)) });
        return;
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    Q_ASSERT(page);

    QElapsedTimer timer;
    timer.start();

    PDFPrecompiledPageGenerator generator(precompiledPage, m_features, page, m_document, m_fontCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    QList<PDFRenderError> errors = generator.processContents();

    if (m_features.testFlag(InvertColors))
    {
        precompiledPage->invertColors();
    }

    precompiledPage->optimize();
    precompiledPage->finalize(timer.nsecsElapsed(), qMove(errors));
    timer.invalidate();
}

PDFRasterizer::PDFRasterizer(QObject* parent) :
    BaseClass(parent),
    m_features(),
    m_surfaceFormat(),
    m_surface(nullptr),
    m_context(nullptr),
    m_fbo(nullptr)
{

}

PDFRasterizer::~PDFRasterizer()
{
    releaseOpenGL();
}

void PDFRasterizer::reset(bool useOpenGL, const QSurfaceFormat& surfaceFormat)
{
    if (useOpenGL != m_features.testFlag(UseOpenGL) || surfaceFormat != m_surfaceFormat)
    {
        // In either case, we must reset OpenGL
        releaseOpenGL();

        m_features.setFlag(UseOpenGL, useOpenGL);
        m_surfaceFormat = surfaceFormat;

        // We create new OpenGL renderer, but only if it hasn't failed (we do not try
        // again to create new OpenGL renderer.
        if (m_features.testFlag(UseOpenGL) && !m_features.testFlag(FailedOpenGL))
        {
            initializeOpenGL();
        }
    }
}

QImage PDFRasterizer::render(const PDFPage* page, const PDFPrecompiledPage* compiledPage, QSize size, PDFRenderer::Features features)
{
    QImage image;

    QMatrix matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), size));
    if (m_features.testFlag(UseOpenGL) && m_features.testFlag(ValidOpenGL))
    {
        // We have valid OpenGL context, try to select it and possibly create framebuffer object
        // for target image (to paint it using paint device).
        Q_ASSERT(m_surface && m_context);
        if (m_context->makeCurrent(m_surface))
        {
            if (!m_fbo || m_fbo->width() != size.width() || m_fbo->height() != size.height())
            {
                // Delete old framebuffer object
                delete m_fbo;

                // Create a new framebuffer object
                QOpenGLFramebufferObjectFormat format;
                format.setSamples(m_surfaceFormat.samples());
                m_fbo = new QOpenGLFramebufferObject(size.width(), size.height(), format);
            }

            Q_ASSERT(m_fbo);
            if (m_fbo->isValid() && m_fbo->bind())
            {
                // Now, we have bind the buffer.
                {
                    QOpenGLPaintDevice device(size);
                    QPainter painter(&device);
                    painter.fillRect(QRect(QPoint(0, 0), size), compiledPage->getPaperColor());
                    compiledPage->draw(&painter, page->getCropBox(), matrix, features);
                }

                m_fbo->release();

                image = m_fbo->toImage();
            }
            else
            {
                m_features.setFlag(FailedOpenGL, true);
                m_features.setFlag(ValidOpenGL, false);
            }

            m_context->doneCurrent();
        }
    }

    if (image.isNull())
    {
        // If we can't use OpenGL, or user doesn't want to use OpenGL, then fallback
        // to standard software rasterizer.
        image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);

        QPainter painter(&image);
        compiledPage->draw(&painter, page->getCropBox(), matrix, features);
    }

    // Jakub Melka: Convert the image into format Format_ARGB32_Premultiplied for fast drawing.
    // If this format is used, then no image conversion is performed while drawing.
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
    {
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
    }

    return image;
}

void PDFRasterizer::initializeOpenGL()
{
    Q_ASSERT(!m_surface);
    Q_ASSERT(!m_context);
    Q_ASSERT(!m_fbo);

    m_features.setFlag(ValidOpenGL, false);
    m_features.setFlag(FailedOpenGL, false);

    // Create context
    m_context = new QOpenGLContext(this);
    m_context->setFormat(m_surfaceFormat);
    if (!m_context->create())
    {
        m_features.setFlag(FailedOpenGL, true);

        delete m_context;
        m_context = nullptr;
    }

    // Create surface
    m_surface = new QOffscreenSurface(nullptr, this);
    m_surface->setFormat(m_surfaceFormat);
    m_surface->create();
    if (!m_surface->isValid())
    {
        m_features.setFlag(FailedOpenGL, true);

        delete m_context;
        delete m_surface;

        m_context = nullptr;
        m_surface = nullptr;
    }

    // Check, if we can make it current
    if (m_context->makeCurrent(m_surface))
    {
        m_features.setFlag(ValidOpenGL, true);
        m_context->doneCurrent();
    }
    else
    {
        m_features.setFlag(FailedOpenGL, true);
        releaseOpenGL();
    }
}

void PDFRasterizer::releaseOpenGL()
{
    if (m_surface)
    {
        Q_ASSERT(m_context);

        // Delete framebuffer
        if (m_fbo)
        {
            m_context->makeCurrent(m_surface);
            delete m_fbo;
            m_fbo = nullptr;
            m_context->doneCurrent();
        }

        // Delete OpenGL context
        delete m_context;
        m_context = nullptr;

        // Delete surface
        m_surface->destroy();
        delete m_surface;
        m_surface = nullptr;

        // Set flag, that we do not have valid OpenGL
        m_features.setFlag(ValidOpenGL, false);
    }
}

PDFImageWriterSettings::PDFImageWriterSettings()
{
    m_formats = QImageWriter::supportedImageFormats();
    selectFormat(m_formats.front());
}

void PDFImageWriterSettings::selectFormat(const QByteArray& format)
{
    if (m_currentFormat != format)
    {
        m_currentFormat = format;

        QImageWriter writer;
        writer.setFormat(format);

        m_compression = 0;
        m_quality = 0;
        m_gamma = 0;
        m_optimizedWrite = false;
        m_progressiveScanWrite = false;
        m_subtypes = writer.supportedSubTypes();
        m_currentSubtype = !m_subtypes.isEmpty() ? m_subtypes.front() : QByteArray();

        // Jakub Melka: init default values based on image handler. Unfortunately,
        // image writer doesn't give us access to these values, so they are hardcoded.
        if (format == "jpeg" || format == "jpg")
        {
            m_quality = 75;
            m_optimizedWrite = false;
            m_progressiveScanWrite = false;
        }
        else if (format == "png")
        {
            m_compression = 50;
            m_quality = 50;
            m_gamma = 0;
        }
        else if (format == "tif" || format == "tiff")
        {
            m_compression = 1;
        }
        else if (format == "webp")
        {
            m_quality = 75;
        }

        m_supportedOptions.clear();
        for (QImageIOHandler::ImageOption imageOption : { QImageIOHandler::CompressionRatio, QImageIOHandler::Quality,
                                                          QImageIOHandler::Gamma, QImageIOHandler::OptimizedWrite,
                                                          QImageIOHandler::ProgressiveScanWrite, QImageIOHandler::SupportedSubTypes })
        {
            if (writer.supportsOption(imageOption))
            {
                m_supportedOptions.insert(imageOption);
            }
        }
    }
}

int PDFImageWriterSettings::getCompression() const
{
    return m_compression;
}

void PDFImageWriterSettings::setCompression(int compression)
{
    m_compression = compression;
}

int PDFImageWriterSettings::getQuality() const
{
    return m_quality;
}

void PDFImageWriterSettings::setQuality(int quality)
{
    m_quality = quality;
}

float PDFImageWriterSettings::getGamma() const
{
    return m_gamma;
}

void PDFImageWriterSettings::setGamma(float gamma)
{
    m_gamma = gamma;
}

bool PDFImageWriterSettings::hasOptimizedWrite() const
{
    return m_optimizedWrite;
}

void PDFImageWriterSettings::setOptimizedWrite(bool optimizedWrite)
{
    m_optimizedWrite = optimizedWrite;
}

bool PDFImageWriterSettings::hasProgressiveScanWrite() const
{
    return m_progressiveScanWrite;
}

void PDFImageWriterSettings::setProgressiveScanWrite(bool progressiveScanWrite)
{
    m_progressiveScanWrite = progressiveScanWrite;
}

QByteArray PDFImageWriterSettings::getCurrentFormat() const
{
    return m_currentFormat;
}

QByteArray PDFImageWriterSettings::getCurrentSubtype() const
{
    return m_currentSubtype;
}

void PDFImageWriterSettings::setCurrentSubtype(const QByteArray& currentSubtype)
{
    m_currentSubtype = currentSubtype;
}

PDFPageImageExportSettings::PDFPageImageExportSettings()
{
    m_fileTemplate = PDFTranslationContext::tr("Image_%");
}

PDFPageImageExportSettings::ResolutionMode PDFPageImageExportSettings::getResolutionMode() const
{
    return m_resolutionMode;
}

void PDFPageImageExportSettings::setResolutionMode(ResolutionMode resolution)
{
    m_resolutionMode = resolution;
}

PDFPageImageExportSettings::PageSelectionMode PDFPageImageExportSettings::getPageSelectionMode() const
{
    return m_pageSelectionMode;
}

void PDFPageImageExportSettings::setPageSelectionMode(PageSelectionMode pageSelectionMode)
{
    m_pageSelectionMode = pageSelectionMode;
}

QString PDFPageImageExportSettings::getDirectory() const
{
    return m_directory;
}

void PDFPageImageExportSettings::setDirectory(const QString& directory)
{
    m_directory = directory;
}

QString PDFPageImageExportSettings::getFileTemplate() const
{
    return m_fileTemplate;
}

void PDFPageImageExportSettings::setFileTemplate(const QString& fileTemplate)
{
    m_fileTemplate = fileTemplate;
}

QString PDFPageImageExportSettings::getPageSelection() const
{
    return m_pageSelection;
}

void PDFPageImageExportSettings::setPageSelection(const QString& pageSelection)
{
    m_pageSelection = pageSelection;
}

int PDFPageImageExportSettings::getDpiResolution() const
{
    return m_dpiResolution;
}

void PDFPageImageExportSettings::setDpiResolution(int dpiResolution)
{
    m_dpiResolution = dpiResolution;
}

int PDFPageImageExportSettings::getPixelResolution() const
{
    return m_pixelResolution;
}

void PDFPageImageExportSettings::setPixelResolution(int pixelResolution)
{
    m_pixelResolution = pixelResolution;
}

}   // namespace pdf
