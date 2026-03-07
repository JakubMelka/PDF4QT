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

#ifndef PDFPAGEMASTER_PAGEITEMPREVIEWRENDERER_H
#define PDFPAGEMASTER_PAGEITEMPREVIEWRENDERER_H

#include "pageitemmodel.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfrenderer.h"

#include <QCache>
#include <QEvent>
#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <map>
#include <memory>
#include <vector>

class QListView;
class QPixmap;

namespace pdfpagemaster
{

struct PageGroupItem;

class PageItemPreviewRenderer : public QObject
{
    Q_OBJECT

private:
    static constexpr int PREVIEW_CACHE_LIMIT_BYTES = 256 * 1024 * 1024;
    static constexpr int VIEWPORT_UPDATE_TIMEOUT_MS = 120;

public:
    /**
     * @brief Asynchronous thumbnail renderer for page items displayed in a list view.
     *
     * The renderer accepts preview requests from the delegate, keeps an in-memory image
     * cache, and renders missing previews in background worker threads. Requests are
     * visibility-aware, so off-screen rows are deprioritized or dropped when scrolling.
     *
     * The instance itself lives in the GUI thread. Heavy rendering is executed
     * asynchronously and completed previews are delivered via @ref previewUpdated.
     *
     * Internally, requests are tagged by an epoch. The epoch is increased on
     * viewport/model changes so stale results can be ignored safely.
     *
     * @param model Source model with document/image storage and row metadata.
     * @param parent QObject parent.
     */
    explicit PageItemPreviewRenderer(PageItemModel* model, QObject* parent);
    virtual ~PageItemPreviewRenderer() override;

    /**
     * @brief Attaches a view used for visibility checks and viewport-change tracking.
     *
     * The renderer observes scrollbar/viewport events to defer rendering while scrolling
     * and to keep only relevant requests in the queue.
     *
     * Passing @c nullptr detaches the current view.
     */
    void setView(QListView* view);

    /**
     * @brief Sets the logical preview image size used by cache keys and rendering.
     *
     * Changing the size invalidates all cached thumbnails and schedules a repaint.
     */
    void setPageImageSize(QSize pageImageSize);

    /**
     * @brief Returns a cached preview pixmap for an item, or null pixmap if unavailable.
     * @param item Item whose first group page/image is used as preview source.
     * @param pageImageRect Target rectangle in logical coordinates.
     * @return Cached pixmap if present; otherwise null pixmap.
     */
    QPixmap getPageImagePixmap(const PageGroupItem* item, const QRect& pageImageRect) const;

    /**
     * @brief Queues asynchronous preview generation for the given item.
     * @param item Item whose first group page/image is rendered.
     * @param pageImageRect Target preview rectangle in logical coordinates.
     * @param row Model row used for visibility prioritization.
     * @param devicePixelRatio DPR of the paint device used to render crisp thumbnails.
     *
     * @note The request is ignored if a preview is already cached, already queued,
     *       or currently irrelevant (for example row is not visible).
     */
    void requestPreview(const PageGroupItem* item, const QRect& pageImageRect, int row, qreal devicePixelRatio);

signals:
    /// Emitted when one or more previews become available or cache/viewport state changes.
    void previewUpdated();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Immutable data required to render one preview.
    struct RenderRequest
    {
        QString key;
        int row = -1;
        PageType pageType = PT_Empty;
        int documentIndex = -1;
        pdf::PDFInteger pageIndex = -1;
        QImage image;
        pdf::PageRotation pageAdditionalRotation = pdf::PageRotation::None;
        QSize logicalSize;
        qreal devicePixelRatio = 1.0;
        quint64 epoch = 0;
    };

    /// Result of one finished render request.
    struct RenderResult
    {
        QString key;
        int row = -1;
        QImage image;
        quint64 epoch = 0;
    };
    using RenderBatchResult = std::vector<RenderResult>;

    /// Reusable document-bound rendering state shared between requests.
    struct DocumentRenderContext
    {
        const pdf::PDFDocument* document = nullptr;
        std::unique_ptr<pdf::PDFFontCache> fontCache;
        std::unique_ptr<pdf::PDFCMSManager> cmsManager;
        std::unique_ptr<pdf::PDFOptionalContentActivity> optionalContentActivity;
        pdf::PDFMeshQualitySettings meshQualitySettings;
        pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures();
        std::unique_ptr<pdf::PDFRasterizerPool> rasterizerPool;
    };

    /// Returns true if the model row currently intersects the attached viewport.
    bool isRowVisible(int row) const;

    /// Builds a stable cache key from source item identity and logical preview size.
    QString getPageImageKey(const PageGroupItem* item, const QSize& logicalSize) const;

    /// Creates/reuses per-document rendering context required for PDF page previews.
    bool ensureDocumentContext(int documentIndex);

    /// Clears cache and queued requests and advances epoch to invalidate stale results.
    void clearPreviewCache();

    /// Removes queued requests for rows that are no longer visible.
    void pruneRequestQueue();

    /// Starts next asynchronous render batch if renderer is idle.
    void startNextRequest();

    /// Waits for currently running render batch to complete.
    void waitForCurrentRender();

    /// Renders a single request (executed in worker context).
    RenderResult renderPreviewAsync(const RenderRequest& request) const;

    /// Renders a batch of requests using PDFExecutionPolicy page-scope scheduling.
    RenderBatchResult renderPreviewBatchAsync(QList<RenderRequest> requests) const;

private slots:
    void onRenderFinished();
    void onViewportChanged();
    void onViewportSettled();
    void onModelAboutToBeReset();
    void onModelReset();
    void onModelStructureChanged();

private:
    PageItemModel* m_model;
    QSize m_pageImageSize;
    QPointer<QListView> m_view;
    QCache<QString, QImage> m_pageImageCache;
    QSet<QString> m_pendingKeys;
    QList<RenderRequest> m_requestQueue;
    QFutureWatcher<RenderBatchResult> m_renderWatcher;
    bool m_renderInProgress = false;
    quint64 m_renderEpoch = 0;
    bool m_scrollInProgress = false;
    QTimer m_viewportUpdateTimer;
    mutable QMutex m_contextMutex;
    std::map<int, std::unique_ptr<DocumentRenderContext>> m_documentContexts;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_PAGEITEMPREVIEWRENDERER_H
