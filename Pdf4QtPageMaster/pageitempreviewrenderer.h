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
    explicit PageItemPreviewRenderer(PageItemModel* model, QObject* parent);
    virtual ~PageItemPreviewRenderer() override;

    void setView(QListView* view);
    void setPageImageSize(QSize pageImageSize);

    QPixmap getPageImagePixmap(const PageGroupItem* item, const QRect& pageImageRect) const;
    void requestPreview(const PageGroupItem* item, const QRect& pageImageRect, int row, qreal devicePixelRatio);

signals:
    void previewUpdated();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
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

    struct RenderResult
    {
        QString key;
        int row = -1;
        QImage image;
        quint64 epoch = 0;
    };

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

    bool isRowVisible(int row) const;
    QString getPageImageKey(const PageGroupItem* item, const QSize& logicalSize) const;
    bool ensureDocumentContext(int documentIndex);
    void clearPreviewCache();
    void pruneRequestQueue();
    void startNextRequest();
    void waitForCurrentRender();
    RenderResult renderPreviewAsync(const RenderRequest& request) const;

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
    QFutureWatcher<RenderResult> m_renderWatcher;
    bool m_renderInProgress = false;
    quint64 m_renderEpoch = 0;
    bool m_scrollInProgress = false;
    QTimer m_viewportUpdateTimer;
    mutable QMutex m_contextMutex;
    std::map<int, std::unique_ptr<DocumentRenderContext>> m_documentContexts;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_PAGEITEMPREVIEWRENDERER_H
