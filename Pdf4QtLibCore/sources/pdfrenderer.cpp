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

#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdfdocument.h"
#include "pdfexecutionpolicy.h"
#include "pdfprogress.h"
#include "pdfannotation.h"
#include "pdfblpainter.h"

#include "pdfthreadannotations.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QtMath>

#include <map>
#include <numeric>

#include <algorithm>
#include <cmath>
#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

PDFRenderer::PDFRenderer(const PDFDocument* document,
                         const PDFFontCache* fontCache,
                         const PDFICCProfileCache* iccProfileCache,
                         const PDFCMS* cms,
                         const PDFOptionalContentActivity* optionalContentActivity,
                         Features features,
                         const PDFMeshQualitySettings& meshQualitySettings) :
    m_document(document),
    m_fontCache(fontCache),
    m_iccProfileCache(iccProfileCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_operationControl(nullptr),
    m_features(features),
    m_meshQualitySettings(meshQualitySettings)
{
    Q_ASSERT(document);
}

QTransform PDFRenderer::createPagePointToDevicePointMatrix(const PDFPage* page,
                                                           const QRectF& rectangle,
                                                           PageRotation extraRotation)
{
    PageRotation pageRotation = getPageRotationCombined(page->getPageRotation(), extraRotation);
    QRectF mediaBox = page->getRotatedBox(page->getMediaBox(), pageRotation);

    return createMediaBoxToDevicePointMatrix(mediaBox, rectangle, pageRotation);
}

QTransform PDFRenderer::createMediaBoxToDevicePointMatrix(const QRectF& mediaBox,
                                                          const QRectF& rectangle,
                                                          PageRotation rotation)
{
    QTransform matrix;
    switch (rotation)
    {
        case PageRotation::None:
        {
            matrix.translate(rectangle.left(), rectangle.bottom());
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            matrix.translate(-mediaBox.left(), -mediaBox.top());
            break;
        }

        case PageRotation::Rotate90:
        {
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.rotate(90);
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            matrix.translate(-mediaBox.left(), -mediaBox.top());
            break;
        }

        case PageRotation::Rotate270:
        {
            matrix.translate(rectangle.right(), rectangle.top());
            matrix.rotate(-90);
            matrix.translate(-rectangle.height(), 0);
            matrix.scale(rectangle.width() / mediaBox.width(), -rectangle.height() / mediaBox.height());
            matrix.translate(-mediaBox.left(), -mediaBox.top());
            break;
        }

        case PageRotation::Rotate180:
        {
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.scale(rectangle.width() / mediaBox.width(), rectangle.height() / mediaBox.height());
            matrix.translate(mediaBox.width(), 0);
            matrix.scale(-1.0, 1.0);
            matrix.translate(-mediaBox.left(), -mediaBox.top());
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

void PDFRenderer::applyFeaturesToColorConvertor(const Features& features, PDFColorConvertor& convertor)
{
    convertor.setMode(PDFColorConvertor::Mode::Normal);

    if (features.testFlag(ColorAdjust_Invert))
    {
        convertor.setMode(PDFColorConvertor::Mode::InvertedColors);
    }

    if (features.testFlag(ColorAdjust_Grayscale))
    {
        convertor.setMode(PDFColorConvertor::Mode::Grayscale);
    }

    if (features.testFlag(ColorAdjust_HighContrast))
    {
        convertor.setMode(PDFColorConvertor::Mode::HighContrast);
    }

    if (features.testFlag(ColorAdjust_Bitonal))
    {
        convertor.setMode(PDFColorConvertor::Mode::Bitonal);
    }

    if (features.testFlag(ColorAdjust_CustomColors))
    {
        convertor.setMode(PDFColorConvertor::Mode::CustomColors);
    }
}

void PDFRenderer::applyFeaturesToAnnotationColorConvertor(const Features& features, PDFColorConvertor& convertor)
{
    if (!features.testFlag(ColorAdjust_Annotations))
    {
        // Jakub Melka: page content is adjusted always, when some color adjustment mode
        // is active, but annotations are an overlay above the page and the user may want
        // to keep their original colors. Deactivate the convertor - the remaining settings
        // of the convertor are kept, they are not used by an inactive convertor.
        convertor.setMode(PDFColorConvertor::Mode::Normal);
        return;
    }

    applyFeaturesToColorConvertor(features, convertor);
}

const PDFOperationControl* PDFRenderer::getOperationControl() const
{
    return m_operationControl;
}

void PDFRenderer::setOperationControl(const PDFOperationControl* newOperationControl)
{
    m_operationControl = newOperationControl;
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

    QTransform matrix = createPagePointToDevicePointMatrix(page, rectangle);

    PDFPainter processor(painter, m_features, matrix, page, m_document, m_fontCache, m_iccProfileCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    processor.setOperationControl(m_operationControl);
    return processor.processContents();
}

QList<PDFRenderError> PDFRenderer::render(QPainter* painter, const QTransform& matrix, size_t pageIndex) const
{
    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex >= catalog->getPageCount() || !catalog->getPage(pageIndex))
    {
        // Invalid page index
        return { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1)) };
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    Q_ASSERT(page);

    PDFPainter processor(painter, m_features, matrix, page, m_document, m_fontCache, m_iccProfileCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    processor.setOperationControl(m_operationControl);
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

    PDFPrecompiledPageGenerator generator(precompiledPage, m_features, page, m_document, m_fontCache, m_iccProfileCache, m_cms, m_optionalContentActivity, m_meshQualitySettings);
    generator.setOperationControl(m_operationControl);
    QList<PDFRenderError> errors = generator.processContents();

    PDFColorConvertor colorConvertor = m_cms->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(m_features, colorConvertor);
    precompiledPage->convertColors(colorConvertor);

    precompiledPage->optimize();
    precompiledPage->finalize(timer.nsecsElapsed(), qMove(errors));
    timer.invalidate();
}

PDFRasterizer::PDFRasterizer(QObject* parent) :
    BaseClass(parent),
    m_rendererEngine(RendererEngine::Blend2D_SingleThread)
{

}

PDFRasterizer::~PDFRasterizer()
{

}

void PDFRasterizer::reset(RendererEngine rendererEngine)
{
    m_rendererEngine = rendererEngine;
}

QImage PDFRasterizer::render(PDFInteger pageIndex,
                             const PDFPage* page,
                             const PDFPrecompiledPage* compiledPage,
                             QSize size,
                             PDFRenderer::Features features,
                             const PDFAnnotationManager* annotationManager,
                             const PDFCMS* cms,
                             PageRotation extraRotation)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        return QImage();
    }

    PDFColorConvertor convertor = cms->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(features, convertor);
    QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), size), extraRotation);

    if (m_rendererEngine == RendererEngine::Blend2D_MultiThread ||
        m_rendererEngine == RendererEngine::Blend2D_SingleThread)
    {
        PDFBLPaintDevice blPaintDevice(image, false);

        // Blend2D clears the buffer to transparent when painting begins, so the
        // paper is filled through the painter (the same as the QPainter engine).
        // Otherwise, formats without alpha channel (JPEG) get a black page.
        QPainter painter(&blPaintDevice);
        painter.fillRect(QRect(QPoint(0, 0), size), Qt::white);
        compiledPage->draw(&painter, page->getCropBox(), matrix, features, 1.0);

        if (annotationManager)
        {
            QList<PDFRenderError> errors;
            PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            annotationManager->drawPage(&painter, pageIndex, compiledPage, textLayoutGetter, matrix, convertor, errors);
        }
    }
    else
    {
        // Use standard software rasterizer.
        image.fill(Qt::white);

        QPainter painter(&image);
        compiledPage->draw(&painter, page->getCropBox(), matrix, features, 1.0);

        if (annotationManager)
        {
            QList<PDFRenderError> errors;
            PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            annotationManager->drawPage(&painter, pageIndex, compiledPage, textLayoutGetter, matrix, convertor, errors);
        }
    }

    // Calculate image DPI
    QSizeF rotatedSizeInMeters = page->getRotatedMediaBoxMM().size() / 1000.0;
    QSizeF rotatedSizeInPixels = image.size();
    auto dotsPerMeter = [](qreal pixels, qreal meters) -> int
    {
        if (!std::isfinite(meters) || meters <= 0.0)
        {
            return 0;
        }
        // A tiny MediaBox can imply a resolution outside the int range even
        // for a small image. Bound it before qCeil converts it to an integer.
        const qreal resolution = pixels / meters;
        if (!std::isfinite(resolution) || resolution >= std::numeric_limits<int>::max())
        {
            return std::numeric_limits<int>::max();
        }
        return qCeil(resolution);
    };
    image.setDotsPerMeterX(dotsPerMeter(rotatedSizeInPixels.width(), rotatedSizeInMeters.width()));
    image.setDotsPerMeterY(dotsPerMeter(rotatedSizeInPixels.height(), rotatedSizeInMeters.height()));

    return image;
}

bool PDFRenderedPageImage::hasSevereError() const
{
    return std::any_of(errors.cbegin(), errors.cend(), [](const PDFRenderError& error)
    {
        return error.type == RenderErrorType::Error || error.type == RenderErrorType::NotImplemented;
    });
}

QList<PDFRenderError> PDFRenderedPageImage::getReportedErrors() const
{
    QList<PDFRenderError> result;
    for (const PDFRenderError& error : errors)
    {
        if (error.type != RenderErrorType::Information)
        {
            result.push_back(error);
        }
    }
    return result;
}

PDFRasterizer* PDFRasterizerPool::acquire(const PDFOperationControl* operationControl)
{
    if (operationControl)
    {
        // The waiting is interruptible - a canceled task must not occupy a rasterizer,
        // which a newer task is waiting for, so the cancellation is polled while the
        // semaphore is being waited for.
        constexpr int POLL_INTERVAL_MS = 20;

        while (!m_semaphore.tryAcquire(1, POLL_INTERVAL_MS))
        {
            if (operationControl->isOperationCanceled())
            {
                return nullptr;
            }
        }
    }
    else
    {
        m_semaphore.acquire();
    }

    QMutexLocker guard(&m_mutex);
    Q_ASSERT(!m_rasterizers.empty());
    PDFRasterizer* rasterizer = m_rasterizers.back();
    m_rasterizers.pop_back();
    return rasterizer;
}

void PDFRasterizerPool::release(pdf::PDFRasterizer* rasterizer)
{
    QMutexLocker guard(&m_mutex);
    Q_ASSERT(std::find(m_rasterizers.cbegin(), m_rasterizers.cend(), rasterizer) == m_rasterizers.cend());
    m_rasterizers.push_back(rasterizer);

    // Jakub Melka: we must release it at the end, to ensure rasterizer is in the array before
    // semaphore is released, to avoid race condition.
    m_semaphore.release();
}

void PDFRasterizerPool::render(const std::vector<PDFInteger>& pageIndices,
                               const PDFRasterizerPool::PageImageSizeGetter& imageSizeGetter,
                               const PDFRasterizerPool::ProcessImageMethod& processImage,
                               PDFProgressSink* progress,
                               const PDFOperationControl* operationControl)
{
    if (pageIndices.empty())
    {
        return;
    }

    Q_ASSERT(imageSizeGetter);
    Q_ASSERT(processImage);

    QElapsedTimer timer;
    timer.start();

    Q_EMIT renderError(PDFCatalog::INVALID_PAGE_INDEX, PDFRenderError(RenderErrorType::Information, PDFTranslationContext::tr("Start at %1...").arg(QTime::currentTime().toString(Qt::TextDate))));

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = true;
        info.text = PDFTranslationContext::tr("Rendering document into images.");
        progress->start(pageIndices.size(), qMove(info));
    }
    auto processPage = [this, progress, operationControl, &imageSizeGetter, &processImage](const PDFInteger pageIndex)
    {
        // Progress must be stepped even when the page is skipped, otherwise
        // the progress would never reach its end.
        auto progressGuard = qScopeGuard([progress]()
        {
            if (progress)
            {
                progress->step();
            }
        });

        renderPage(pageIndex, imageSizeGetter, processImage, operationControl, true);
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageIndices.cbegin(), pageIndices.cend(), processPage);

    if (progress)
    {
        progress->finish();
    }

    Q_EMIT renderError(PDFCatalog::INVALID_PAGE_INDEX, PDFRenderError(RenderErrorType::Information, PDFTranslationContext::tr("Finished at %1...").arg(QTime::currentTime().toString(Qt::TextDate))));
    Q_EMIT renderError(PDFCatalog::INVALID_PAGE_INDEX, PDFRenderError(RenderErrorType::Information, PDFTranslationContext::tr("%1 miliseconds elapsed to render %2 pages...").arg(timer.nsecsElapsed() / 1000000).arg(pageIndices.size())));
}

PDFRasterizerPool::PageStatus PDFRasterizerPool::renderPage(PDFInteger pageIndex,
                                                            const PageImageSizeGetter& imageSizeGetter,
                                                            const ProcessImageMethod& processImage,
                                                            const PDFOperationControl* operationControl,
                                                            bool isEmittingErrors)
{
    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        // Operation has been canceled, remaining pages are not rendered at all
        return PageStatus::Canceled;
    }

    // A page index outside of the document is a missing page (PDFCatalog::getPage would throw)
    const PDFCatalog* catalog = m_document->getCatalog();
    const PDFPage* page = pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(pageIndex)) : nullptr;

    if (!page)
    {
        if (isEmittingErrors)
        {
            Q_EMIT renderError(pageIndex, PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 not found.").arg(pageIndex)));
        }
        return PageStatus::Missing;
    }

    QElapsedTimer totalPageTimer;
    totalPageTimer.start();

    QElapsedTimer pageTimer;
    pageTimer.start();

    // Precompile the page
    PDFPrecompiledPage precompiledPage;
    PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
    PDFRenderer renderer(m_document, m_fontCache, m_iccProfileCache, cms.data(), m_optionalContentActivity, m_features, m_meshQualitySettings);
    renderer.setOperationControl(operationControl);
    renderer.compile(&precompiledPage, pageIndex);

    qint64 pageCompileTime = pageTimer.restart();

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        // The compilation has been interrupted, so the page is incomplete and there
        // is no point in rasterizing it - that is the most expensive part.
        return PageStatus::Canceled;
    }

    if (isEmittingErrors)
    {
        for (const PDFRenderError& error : precompiledPage.getErrors())
        {
            Q_EMIT renderError(pageIndex, error);
        }
    }

    // We can const-cast here, because we do not modify the document in annotation manager.
    // Annotations are just rendered to the target picture.
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(m_document), const_cast<PDFOptionalContentActivity*>(m_optionalContentActivity));

    // Annotation manager
    PDFAnnotationManager annotationManager(m_fontCache, m_iccProfileCache, m_cmsManager, m_optionalContentActivity, m_meshQualitySettings, m_features, PDFAnnotationManager::Target::Print, nullptr);
    annotationManager.setDocument(modifiedDocument);

    // The bytes of the image are taken from the budget before the image exists (Q4.7, R4-16)
    // and returned, when the image has been processed - also by an exception
    const QSize imageSize = imageSizeGetter(page);
    const qint64 imageBytes = getImageDataSize(imageSize);
    if (!acquireImageData(imageBytes, operationControl))
    {
        return PageStatus::Canceled;
    }
    auto imageDataGuard = qScopeGuard([this, imageBytes]() { releaseImageData(imageBytes); });

    // Render page to image
    pageTimer.restart();
    PDFRasterizer* rasterizer = acquire(operationControl);
    qint64 pageWaitTime = pageTimer.restart();

    if (!rasterizer)
    {
        // The operation has been canceled while a free rasterizer was being waited for
        return PageStatus::Canceled;
    }

    QImage image;
    qint64 pageRenderTime = 0;
    {
        // Return the rasterizer even if the rendering throws.
        // Otherwise a subsequent job can wait on the semaphore forever.
        auto rasterizerGuard = qScopeGuard([this, rasterizer]() { release(rasterizer); });
        image = rasterizer->render(pageIndex, page, &precompiledPage, imageSize, m_features, &annotationManager, cms.data(), PageRotation::None);
        pageRenderTime = pageTimer.elapsed();
    }

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        // Nobody is interested in the image anymore - it must not be processed.
        return PageStatus::Canceled;
    }

    // Now, process the image
    PDFRenderedPageImage renderedPageImage;
    renderedPageImage.pageIndex = pageIndex;
    renderedPageImage.pageImage = qMove(image);
    renderedPageImage.errors = precompiledPage.getErrors();
    renderedPageImage.pageCompileTime = pageCompileTime;
    renderedPageImage.pageWaitTime = pageWaitTime;
    renderedPageImage.pageRenderTime = pageRenderTime;
    renderedPageImage.pageTotalTime = totalPageTimer.elapsed();
    processImage(renderedPageImage);
    return PageStatus::Processed;
}

/// State of one renderOrdered: the pages encoded ahead of the next written one (guarded by
/// the mutex), the writer of the moment
class PDFRasterizerPool::OrderedState
{
public:
    QMutex mutex;
    QWaitCondition condition;
    size_t nextPosition = 0;                                ///< Position (in the page indices) of the next written page
    bool isWriting = false;                                 ///< A thread hands the ready pages to the writer
    bool isAborted = false;                                 ///< An exception has ended the rendering: nobody waits any more
    std::map<size_t, PDFRenderedPageOutput> readyPages;     ///< Encoded pages by their positions, not written yet
};

void PDFRasterizerPool::renderOrdered(const std::vector<PDFInteger>& pageIndices,
                                      const PageImageSizeGetter& imageSizeGetter,
                                      const EncodeImageMethod& encode,
                                      const WriteOutputMethod& write,
                                      PDFProgressSink* progress,
                                      const PDFOperationControl* operationControl)
{
    if (pageIndices.empty())
    {
        return;
    }

    Q_ASSERT(imageSizeGetter);
    Q_ASSERT(encode);
    Q_ASSERT(write);

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = true;
        info.text = PDFTranslationContext::tr("Rendering document into images.");
        progress->start(pageIndices.size(), qMove(info));
    }

    OrderedState state;
    const size_t windowSize = size_t(getReorderWindowSize());

    std::vector<size_t> positions(pageIndices.size());
    std::iota(positions.begin(), positions.end(), size_t(0));

    auto processPosition = [&](size_t position)
    {
        auto progressGuard = qScopeGuard([progress]()
        {
            if (progress)
            {
                progress->step();
            }
        });

        try
        {
            // The window: a page does not start, while it is too far after the next written page
            // (its encoded bytes would wait in the memory); the cancellation is polled
            {
                QMutexLocker locker(&state.mutex);
                while (!state.isAborted && position >= state.nextPosition + windowSize)
                {
                    if (PDFOperationControl::isOperationCanceled(operationControl))
                    {
                        return;
                    }
                    PDFThreadAnnotations::wait(state.condition, &state.mutex, QDeadlineTimer(WAIT_POLL_INTERVAL));
                }
                if (state.isAborted)
                {
                    return;
                }
            }

            PDFRenderedPageOutput output;
            output.pageIndex = pageIndices[position];

            const ProcessImageMethod encodeImage = [&output, &encode](PDFRenderedPageImage& image)
            {
                output.errors = image.errors;
                if (image.pageImage.isNull())
                {
                    output.errorMessage = PDFTranslationContext::tr("The image of the page cannot be created.");
                    return;
                }

                QString errorMessage;
                output.data = encode(image, &errorMessage);
                output.isRendered = errorMessage.isEmpty() && !output.data.isEmpty();
                if (!output.isRendered)
                {
                    output.data.clear();
                    output.errorMessage = !errorMessage.isEmpty() ? errorMessage : PDFTranslationContext::tr("The image of the page cannot be encoded.");
                }
            };

            switch (renderPage(output.pageIndex, imageSizeGetter, encodeImage, operationControl, false))
            {
                case PageStatus::Canceled:
                    // Nothing more is written, the threads waiting for the window see the cancellation
                    return;

                case PageStatus::Missing:
                    output.errorMessage = PDFTranslationContext::tr("Page %1 does not exist.").arg(output.pageIndex + 1);
                    break;

                case PageStatus::Processed:
                    break;
            }

            // The page is ready: the first thread, which finds the next page ready, hands the
            // ready pages to the writer one by one in their order, the others only leave theirs
            QMutexLocker locker(&state.mutex);
            state.readyPages.emplace(position, std::move(output));
            if (state.isWriting)
            {
                return;
            }

            state.isWriting = true;
            auto writingGuard = qScopeGuard([&state]() { state.isWriting = false; });
            while (!state.isAborted)
            {
                auto it = state.readyPages.find(state.nextPosition);
                if (it == state.readyPages.end())
                {
                    break;
                }

                PDFRenderedPageOutput current = std::move(it->second);
                state.readyPages.erase(it);

                // The writer runs without the lock; it is never called twice at once (isWriting)
                locker.unlock();
                {
                    auto relockGuard = qScopeGuard([&locker]() { locker.relock(); });
                    if (!PDFOperationControl::isOperationCanceled(operationControl))
                    {
                        write(current);
                    }
                }

                ++state.nextPosition;
                state.condition.wakeAll();
            }
        }
        catch (...)
        {
            // Nobody may wait for a page, which will never come: the waiting threads leave
            QMutexLocker locker(&state.mutex);
            state.isAborted = true;
            state.condition.wakeAll();
            throw;
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, positions.cbegin(), positions.cend(), processPosition);

    if (progress)
    {
        progress->finish();
    }
}

qint64 PDFRasterizerPool::getDefaultImageDataBudget(qint64 physicalMemory)
{
    if (physicalMemory <= 0)
    {
        return UNKNOWN_MEMORY_IMAGE_DATA_BUDGET;
    }
    return qMax(MINIMUM_DEFAULT_IMAGE_DATA_BUDGET, physicalMemory / 4);
}

qint64 PDFRasterizerPool::getDefaultImageDataBudget()
{
    // The physical memory does not change while the application runs
    static const qint64 budget = getDefaultImageDataBudget(PDFSysUtils::getPhysicalMemorySize());
    return budget;
}

void PDFRasterizerPool::setImageDataBudget(qint64 budget)
{
    QMutexLocker locker(&m_imageDataMutex);
    m_imageDataBudget = qMax<qint64>(1, budget);
}

qint64 PDFRasterizerPool::getImageDataSize(QSize size)
{
    if (size.isEmpty())
    {
        return 0;
    }
    return qint64(size.width()) * qint64(size.height()) * BYTES_PER_PIXEL;
}

bool PDFRasterizerPool::acquireImageData(qint64 bytes, const PDFOperationControl* operationControl)
{
    QMutexLocker locker(&m_imageDataMutex);

    // A page larger than the whole budget runs alone: it waits, until no other image exists, and
    // the pages after it do not start meanwhile (they would keep the budget busy forever)
    const bool isLarge = bytes > m_imageDataBudget;
    if (isLarge)
    {
        ++m_largePagesWaiting;
    }
    auto largeGuard = qScopeGuard([this, isLarge]()
    {
        if (isLarge)
        {
            --m_largePagesWaiting;
        }
    });

    while (true)
    {
        const bool fits = isLarge ? m_imageDataUsed == 0 : (m_largePagesWaiting == 0 && (m_imageDataUsed == 0 || m_imageDataUsed + bytes <= m_imageDataBudget));
        if (fits)
        {
            m_imageDataUsed += bytes;
            return true;
        }

        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return false;
        }
        PDFThreadAnnotations::wait(m_imageDataCondition, &m_imageDataMutex, QDeadlineTimer(WAIT_POLL_INTERVAL));
    }
}

void PDFRasterizerPool::releaseImageData(qint64 bytes)
{
    QMutexLocker locker(&m_imageDataMutex);
    m_imageDataUsed -= bytes;
    Q_ASSERT(m_imageDataUsed >= 0);
    m_imageDataCondition.wakeAll();
}

int PDFRasterizerPool::getDefaultRasterizerCount()
{
    int hint = QThread::idealThreadCount() / 2;
    return getCorrectedRasterizerCount(hint);
}

int PDFRasterizerPool::getCorrectedRasterizerCount(int rasterizerCount)
{
    return qBound(1, rasterizerCount, 256);
}

PDFImageWriterSettings::PDFImageWriterSettings()
{
    m_formats = QImageWriter::supportedImageFormats();

    constexpr const char* DEFAULT_FORMAT = "png";
    if (m_formats.count(DEFAULT_FORMAT))
    {
        selectFormat(DEFAULT_FORMAT);
    }
    else
    {
        selectFormat(m_formats.front());
    }
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

PDFPageImageExportSettings::PDFPageImageExportSettings(const PDFDocument* document) :
    m_document(document)
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

bool PDFPageImageExportSettings::validate(QString* errorMessagePtr, bool validatePageSelection, bool validateFileSettings, bool validateResolution) const
{
    QString dummy;
    QString& errorMessage = errorMessagePtr ? *errorMessagePtr : dummy;

    if (validateFileSettings)
    {
        if (m_directory.isEmpty())
        {
            errorMessage = PDFTranslationContext::tr("Target directory is empty.");
            return false;
        }

        // Check, if target directory exists
        QDir directory(m_directory);
        if (!directory.exists())
        {
            errorMessage = PDFTranslationContext::tr("Target directory '%1' doesn't exist.").arg(m_directory);
            return false;
        }

        if (m_fileTemplate.isEmpty())
        {
            errorMessage = PDFTranslationContext::tr("File template is empty.");
            return false;
        }

        if (!m_fileTemplate.contains("%"))
        {
            errorMessage = PDFTranslationContext::tr("File template must contain character '%' for page number.");
            return false;
        }
    }

    // Check page selection
    if (validatePageSelection)
    {
        if (m_pageSelectionMode == PageSelectionMode::Selection)
        {
            std::vector<PDFInteger> pages = getPages();
            if (pages.empty())
            {
                errorMessage = PDFTranslationContext::tr("Page list is invalid. It should have form such as '1-12,17,24,27-29'.");
                return false;
            }

            if (pages.back() >= PDFInteger(m_document->getCatalog()->getPageCount()))
            {
                errorMessage = PDFTranslationContext::tr("Page list contains page, which is not in the document (%1).").arg(pages.back());
                return false;
            }
        }
    }

    if (validateResolution)
    {
        if (m_resolutionMode == ResolutionMode::DPI && (m_dpiResolution < getMinDPIResolution() || m_dpiResolution > getMaxDPIResolution()))
        {
            errorMessage = PDFTranslationContext::tr("DPI resolution should be in range %1 to %2.").arg(getMinDPIResolution()).arg(getMaxDPIResolution());
            return false;
        }

        if (m_resolutionMode == ResolutionMode::Pixels && (m_pixelResolution < getMinPixelResolution() || m_pixelResolution > getMaxPixelResolution()))
        {
            errorMessage = PDFTranslationContext::tr("Pixel resolution should be in range %1 to %2.").arg(getMinPixelResolution()).arg(getMaxPixelResolution());
            return false;
        }
    }

    return true;
}

std::vector<PDFInteger> PDFPageImageExportSettings::getPages() const
{
    std::vector<PDFInteger> result;

    switch (m_pageSelectionMode)
    {
        case PageSelectionMode::All:
        {
            result.resize(m_document->getCatalog()->getPageCount(), 0);
            std::iota(result.begin(), result.end(), 0);
            break;
        }

        case PageSelectionMode::Selection:
        {
            bool ok = false;
            QStringList parts = m_pageSelection.split(QChar(','), Qt::SkipEmptyParts, Qt::CaseSensitive);
            for (const QString& part : parts)
            {
                QStringList numbers = part.split(QChar('-'), Qt::KeepEmptyParts, Qt::CaseSensitive);
                switch (numbers.size())
                {
                    case 1:
                    {
                        const QString& numberString = numbers.front();
                        result.push_back(numberString.toLongLong(&ok) - 1);
                        break;
                    }

                    case 2:
                    {
                        bool ok1 = false;
                        bool ok2 = false;
                        const QString& lowString = numbers.front();
                        const QString& highString = numbers.back();
                        const PDFInteger low = lowString.toLongLong(&ok1) - 1;
                        const PDFInteger high = highString.toLongLong(&ok2) - 1;
                        ok = ok1 && ok2 && low <= high && low >= 0;
                        if (ok)
                        {
                            const PDFInteger count = high - low + 1;
                            result.resize(result.size() + count, 0);
                            std::iota(std::prev(result.end(), count), result.end(), low);
                        }
                        break;
                    }

                    default:
                    {
                        ok = true;
                        break;
                    }
                }

                // If error is detected, do not continue in parsing
                if (!ok)
                {
                    break;
                }

                // We must remove duplicate pages
                std::sort(result.begin(), result.end());
                result.erase(std::unique(result.begin(), result.end()), result.end());
            }

            if (!ok)
            {
                result.clear();
            }

            break;
        }

        default:
            break;
    }

    return result;
}

QString PDFPageImageExportSettings::getOutputFileName(PDFInteger pageIndex, const QByteArray& outputFormat) const
{
    QString fileName = m_fileTemplate;
    fileName.replace('%', QString::number(pageIndex + 1));

    QFileInfo fileInfo(fileName);
    if (fileInfo.suffix() != outputFormat)
    {
        fileName = QString("%1.%2").arg(fileName, QString::fromLatin1(outputFormat));
    }

    // Add directory
    QString fileNameWithDirectory = QString("%1/%2").arg(m_directory, fileName);
    return QDir::toNativeSeparators(fileNameWithDirectory);
}

PDFRasterizerPool::PDFRasterizerPool(const PDFDocument* document,
                                     PDFFontCache* fontCache,
                                     const PDFICCProfileCache* iccProfileCache,
                                     const PDFCMSManager* cmsManager,
                                     const PDFOptionalContentActivity* optionalContentActivity,
                                     PDFRenderer::Features features,
                                     const PDFMeshQualitySettings& meshQualitySettings,
                                     int rasterizerCount,
                                     RendererEngine rendererEngine,
                                     QObject* parent) :
    BaseClass(parent),
    m_document(document),
    m_fontCache(fontCache),
    m_iccProfileCache(iccProfileCache),
    m_cmsManager(cmsManager),
    m_optionalContentActivity(optionalContentActivity),
    m_features(features),
    m_meshQualitySettings(meshQualitySettings),
    m_semaphore(rasterizerCount)
{
    m_rasterizerCount = qMax(1, rasterizerCount);
    m_rasterizers.reserve(rasterizerCount);
    for (int i = 0; i < rasterizerCount; ++i)
    {
        m_rasterizers.push_back(new PDFRasterizer(this));
        m_rasterizers.back()->reset(rendererEngine);
    }
}

}   // namespace pdf

#include "moc_pdfrenderer.cpp"
