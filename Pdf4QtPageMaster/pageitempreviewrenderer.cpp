// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pageitempreviewrenderer.h"
#include "pdfcompiler.h"
#include "pdffont.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QtConcurrent/QtConcurrent>
#include <limits>

namespace pdfpagemaster
{

PageItemPreviewRenderer::PageItemPreviewRenderer(PageItemModel* model, QObject* parent) :
    QObject(parent),
    m_model(model),
    m_pageImageCache(PREVIEW_CACHE_LIMIT_BYTES)
{
    m_viewportUpdateTimer.setSingleShot(true);
    m_viewportUpdateTimer.setInterval(VIEWPORT_UPDATE_TIMEOUT_MS);

    connect(&m_renderWatcher, &QFutureWatcher<RenderResult>::finished, this, &PageItemPreviewRenderer::onRenderFinished);
    connect(&m_viewportUpdateTimer, &QTimer::timeout, this, &PageItemPreviewRenderer::onViewportSettled);
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, &PageItemPreviewRenderer::onModelAboutToBeReset);
    connect(m_model, &QAbstractItemModel::modelReset, this, &PageItemPreviewRenderer::onModelReset);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &PageItemPreviewRenderer::onModelStructureChanged);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &PageItemPreviewRenderer::onModelStructureChanged);
}

PageItemPreviewRenderer::~PageItemPreviewRenderer()
{
    waitForCurrentRender();
}

void PageItemPreviewRenderer::setView(QListView* view)
{
    if (m_view == view)
    {
        return;
    }

    if (m_view)
    {
        if (m_view->verticalScrollBar())
        {
            disconnect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &PageItemPreviewRenderer::onViewportChanged);
        }
        if (m_view->horizontalScrollBar())
        {
            disconnect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &PageItemPreviewRenderer::onViewportChanged);
        }
        if (m_view->viewport())
        {
            m_view->viewport()->removeEventFilter(this);
        }
    }

    m_view = view;

    if (m_view)
    {
        if (m_view->verticalScrollBar())
        {
            connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &PageItemPreviewRenderer::onViewportChanged);
        }
        if (m_view->horizontalScrollBar())
        {
            connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &PageItemPreviewRenderer::onViewportChanged);
        }

        if (m_view->viewport())
        {
            m_view->viewport()->installEventFilter(this);
        }
    }

    onViewportChanged();
}

void PageItemPreviewRenderer::setPageImageSize(QSize pageImageSize)
{
    if (m_pageImageSize != pageImageSize)
    {
        m_pageImageSize = pageImageSize;
        clearPreviewCache();
    }
}

QPixmap PageItemPreviewRenderer::getPageImagePixmap(const PageGroupItem* item, const QRect& pageImageRect) const
{
    if (!item || item->groups.empty())
    {
        return QPixmap();
    }

    const QString key = getPageImageKey(item, pageImageRect.size());
    if (QImage* cachedImage = m_pageImageCache.object(key))
    {
        return QPixmap::fromImage(*cachedImage);
    }

    return QPixmap();
}

void PageItemPreviewRenderer::requestPreview(const PageGroupItem* item, const QRect& pageImageRect, int row, qreal devicePixelRatio)
{
    Q_ASSERT(item);

    if (item->groups.empty() || m_scrollInProgress || !isRowVisible(row))
    {
        return;
    }

    const PageGroupItem::GroupItem& groupItem = item->groups.front();
    if (groupItem.pageType == PT_Empty)
    {
        return;
    }

    const QString key = getPageImageKey(item, pageImageRect.size());
    if (m_pageImageCache.object(key) || m_pendingKeys.contains(key))
    {
        return;
    }

    RenderRequest request;
    request.key = key;
    request.row = row;
    request.pageType = groupItem.pageType;
    request.pageAdditionalRotation = groupItem.pageAdditionalRotation;
    request.logicalSize = pageImageRect.size();
    request.devicePixelRatio = devicePixelRatio;
    request.epoch = m_renderEpoch;

    switch (request.pageType)
    {
        case PT_DocumentPage:
        {
            request.documentIndex = groupItem.documentIndex;
            request.pageIndex = groupItem.pageIndex - 1;

            if (!ensureDocumentContext(request.documentIndex))
            {
                return;
            }
            break;
        }

        case PT_Image:
        {
            const auto& images = m_model->getImages();
            auto it = images.find(groupItem.imageIndex);
            if (it == images.cend())
            {
                return;
            }

            request.image = it->second.image;
            break;
        }

        case PT_Empty:
            return;

        default:
            Q_ASSERT(false);
            return;
    }

    m_pendingKeys.insert(key);
    m_requestQueue.push_back(std::move(request));
    startNextRequest();
}

bool PageItemPreviewRenderer::eventFilter(QObject* watched, QEvent* event)
{
    if (m_view && watched == m_view->viewport())
    {
        switch (event->type())
        {
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Wheel:
                onViewportChanged();
                break;

            default:
                break;
        }
    }

    return QObject::eventFilter(watched, event);
}

bool PageItemPreviewRenderer::isRowVisible(int row) const
{
    if (!m_view || !m_view->viewport() || row < 0)
    {
        return false;
    }

    const QModelIndex index = m_model->index(row, 0, QModelIndex());
    if (!index.isValid())
    {
        return false;
    }

    const QRect visibleRect = m_view->viewport()->rect();
    const QRect itemRect = m_view->visualRect(index);
    return itemRect.isValid() && itemRect.intersects(visibleRect);
}

QString PageItemPreviewRenderer::getPageImageKey(const PageGroupItem* item, const QSize& logicalSize) const
{
    Q_ASSERT(item);
    Q_ASSERT(!item->groups.empty());

    const PageGroupItem::GroupItem& groupItem = item->groups.front();
    return QString("%1#%2#%3#%4#%5@%6x%7")
            .arg(groupItem.documentIndex)
            .arg(groupItem.imageIndex)
            .arg(int(groupItem.pageAdditionalRotation))
            .arg(groupItem.pageIndex)
            .arg(groupItem.pageType)
            .arg(logicalSize.width())
            .arg(logicalSize.height());
}

bool PageItemPreviewRenderer::ensureDocumentContext(int documentIndex)
{
    QMutexLocker guard(&m_contextMutex);

    if (m_documentContexts.find(documentIndex) != m_documentContexts.cend())
    {
        return true;
    }

    const auto& documents = m_model->getDocuments();
    auto it = documents.find(documentIndex);
    if (it == documents.cend())
    {
        return false;
    }

    auto context = std::make_unique<DocumentRenderContext>();
    context->document = &it->second.document;
    context->fontCache = std::make_unique<pdf::PDFFontCache>(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    context->cmsManager = std::make_unique<pdf::PDFCMSManager>(nullptr);
    context->optionalContentActivity = std::make_unique<pdf::PDFOptionalContentActivity>(context->document, pdf::OCUsage::View, nullptr);
    context->fontCache->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(context->document), context->optionalContentActivity.get()));
    context->cmsManager->setDocument(context->document);

    const int rasterizerCount = pdf::PDFRasterizerPool::getCorrectedRasterizerCount(1);
    context->rasterizerPool = std::make_unique<pdf::PDFRasterizerPool>(context->document,
                                                                        context->fontCache.get(),
                                                                        context->cmsManager.get(),
                                                                        context->optionalContentActivity.get(),
                                                                        context->features,
                                                                        context->meshQualitySettings,
                                                                        rasterizerCount,
                                                                        pdf::RendererEngine::Blend2D_SingleThread,
                                                                        nullptr);
    m_documentContexts.emplace(documentIndex, std::move(context));
    return true;
}

void PageItemPreviewRenderer::clearPreviewCache()
{
    m_pageImageCache.clear();
    m_pendingKeys.clear();
    m_requestQueue.clear();
    ++m_renderEpoch;
}

void PageItemPreviewRenderer::pruneRequestQueue()
{
    QList<RenderRequest> filteredQueue;
    filteredQueue.reserve(m_requestQueue.size());

    for (RenderRequest& request : m_requestQueue)
    {
        if (isRowVisible(request.row))
        {
            request.epoch = m_renderEpoch;
            filteredQueue.push_back(std::move(request));
        }
        else
        {
            m_pendingKeys.remove(request.key);
        }
    }

    m_requestQueue.swap(filteredQueue);
}

void PageItemPreviewRenderer::startNextRequest()
{
    if (m_scrollInProgress || m_renderInProgress)
    {
        return;
    }

    while (!m_requestQueue.isEmpty())
    {
        const RenderRequest request = m_requestQueue.takeFirst();
        if (!isRowVisible(request.row))
        {
            m_pendingKeys.remove(request.key);
            continue;
        }

        m_renderInProgress = true;
        m_renderWatcher.setFuture(QtConcurrent::run([this, request]() { return renderPreviewAsync(request); }));
        return;
    }
}

void PageItemPreviewRenderer::waitForCurrentRender()
{
    if (m_renderWatcher.isRunning())
    {
        m_renderWatcher.waitForFinished();
    }

    m_renderInProgress = false;
}

PageItemPreviewRenderer::RenderResult PageItemPreviewRenderer::renderPreviewAsync(const RenderRequest& request) const
{
    RenderResult result;
    result.key = request.key;
    result.row = request.row;
    result.epoch = request.epoch;

    const QSize pixelSize = (QSizeF(request.logicalSize) * request.devicePixelRatio).toSize();
    if (!pixelSize.isValid())
    {
        return result;
    }

    switch (request.pageType)
    {
        case PT_DocumentPage:
        {
            DocumentRenderContext* context = nullptr;
            {
                QMutexLocker guard(&m_contextMutex);
                auto it = m_documentContexts.find(request.documentIndex);
                if (it != m_documentContexts.cend())
                {
                    context = it->second.get();
                }
            }

            if (!context || !context->document || request.pageIndex < 0)
            {
                return result;
            }

            const pdf::PDFCatalog* catalog = context->document->getCatalog();
            if (!catalog || request.pageIndex >= pdf::PDFInteger(catalog->getPageCount()))
            {
                return result;
            }

            const pdf::PDFPage* page = catalog->getPage(request.pageIndex);
            if (!page)
            {
                return result;
            }

            pdf::PDFCMSPointer cms = context->cmsManager->getCurrentCMS();
            if (cms.isNull())
            {
                return result;
            }

            pdf::PDFPrecompiledPage compiledPage;
            pdf::PDFRenderer renderer(context->document,
                                      context->fontCache.get(),
                                      cms.data(),
                                      context->optionalContentActivity.get(),
                                      context->features,
                                      context->meshQualitySettings);
            renderer.compile(&compiledPage, request.pageIndex);
            if (!compiledPage.isValid())
            {
                return result;
            }

            pdf::PDFRasterizer* rasterizer = context->rasterizerPool->acquire();
            QImage image = rasterizer->render(request.pageIndex,
                                              page,
                                              &compiledPage,
                                              pixelSize,
                                              context->features,
                                              nullptr,
                                              cms.data(),
                                              request.pageAdditionalRotation);
            context->rasterizerPool->release(rasterizer);

            if (!image.isNull())
            {
                image.setDevicePixelRatio(request.devicePixelRatio);
                result.image = std::move(image);
            }
            break;
        }

        case PT_Image:
        {
            if (request.image.isNull())
            {
                return result;
            }

            QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(request.devicePixelRatio);
            image.fill(Qt::white);

            QPainter painter(&image);
            const QRectF drawRect(QPointF(0.0, 0.0), QSizeF(request.logicalSize));
            const QRectF mediaBox(QPointF(0.0, 0.0), QSizeF(request.image.size()));
            const QRectF rotatedMediaBox = pdf::PDFPage::getRotatedBox(mediaBox, request.pageAdditionalRotation);
            const QTransform matrix = pdf::PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedMediaBox, drawRect, request.pageAdditionalRotation);

            painter.setWorldTransform(matrix);
            painter.translate(0, request.image.height());
            painter.scale(1.0, -1.0);
            painter.drawImage(0, 0, request.image);

            result.image = std::move(image);
            break;
        }

        case PT_Empty:
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return result;
}

void PageItemPreviewRenderer::onRenderFinished()
{
    const RenderResult result = m_renderWatcher.result();
    m_renderInProgress = false;
    m_pendingKeys.remove(result.key);

    if (!result.image.isNull())
    {
        const bool isCurrentResult = result.epoch == m_renderEpoch || isRowVisible(result.row);
        if (isCurrentResult)
        {
            const int cost = qMax(1, int(qMin<qint64>(result.image.sizeInBytes(), std::numeric_limits<int>::max())));
            m_pageImageCache.insert(result.key, new QImage(result.image), cost);
        }
    }

    Q_EMIT previewUpdated();
    startNextRequest();
}

void PageItemPreviewRenderer::onViewportChanged()
{
    m_scrollInProgress = true;
    ++m_renderEpoch;
    pruneRequestQueue();
    m_viewportUpdateTimer.start();
}

void PageItemPreviewRenderer::onViewportSettled()
{
    m_scrollInProgress = false;
    ++m_renderEpoch;
    pruneRequestQueue();
    Q_EMIT previewUpdated();
    startNextRequest();
}

void PageItemPreviewRenderer::onModelAboutToBeReset()
{
    clearPreviewCache();
    waitForCurrentRender();

    QMutexLocker guard(&m_contextMutex);
    m_documentContexts.clear();
}

void PageItemPreviewRenderer::onModelReset()
{
    clearPreviewCache();
    onViewportChanged();
    Q_EMIT previewUpdated();
}

void PageItemPreviewRenderer::onModelStructureChanged()
{
    clearPreviewCache();
    onViewportChanged();
    Q_EMIT previewUpdated();
}

}   // namespace pdfpagemaster
