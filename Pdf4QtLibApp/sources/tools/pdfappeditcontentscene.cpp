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

#include "pdfappeditcontentscene.h"

#include "pdfannotation.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentitems.h"
#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentview.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcolorconvertor.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpainterutils.h"
#include "pdfrenderer.h"
#include "pdftextlayout.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfthreadannotations.h"

#include <QGraphicsView>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QThreadPool>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace pdf
{

// Stage E1, package P11, fix package FX2: the scene of the canvas. Bounds (hostile input): the walk over the content is
// iterative and visits every node of the page container once (the container has at most
// PDFContentContainer::MAXIMUM_OPERATION_COUNT nodes); a clip costs O(1) (a persistent chain, kept over the walks for an
// unchanged clipping node); the forms are built breadth first, not deeper than PDFContentEditSession::
// MAXIMUM_CONTAINER_NESTING, with the keys of the path of the uses as the cycle check (a form drawing itself is drawn
// once), and all of them together under m_formItemBudget items (a form is built only when its node count still fits);
// a walk up to the child of the edited root is bounded by MAXIMUM_PARENT_STEPS; one decoding per image key and size
// step, at most MAXIMUM_RUNNING_JOBS jobs of the scene in the pool, the pictures within m_pictureBudget pixels; the
// editor state of any number of nodes is one walk per turn of the event loop.

/// The way of a decoded picture back to the scene: cut by the destructor and the retire of the scene, a job does not
/// hold the scene
class PDFAppEditContentDecodeLink
{
public:
    explicit PDFAppEditContentDecodeLink(PDFAppEditContentScene* scene) :
        m_scene(scene)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_scene = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job);

private:
    QMutex m_mutex;
    PDFAppEditContentScene* m_scene;
};

/// One job of a worker for the scene (an image, a shading, a pattern, the annotations): plain values written before
/// the start and read after the end (the hand-offs are annotated), except of the cancellation
class PDFAppEditContentDecodeJob : public PDFOperationControl
{
public:
    std::shared_ptr<PDFAppEditContentDecodeLink> link;
    PDFDocumentPointer document;            ///< Held by the job (the document must outlive the work)
    PDFContentContainerPointer container;   ///< Immutable
    PDFCMSPointer cms;                      ///< Held by the job; nullptr = the generic one
    PDFContentEditSnapshotPointer snapshot; ///< The new objects of the session (a pattern of a moved object, R5-3); may be nullptr
    PDFContentNodeId node;
    PDFInteger pageIndex = -1;
    quint64 serial = 0;                     ///< The item, which waits for the result (0: the underlay)
    PDFAppEditContentDecodeKind kind = PDFAppEditContentDecodeKind::Image;
    PDFAppEditContentPictureKey pictureKey; ///< Image: the key of the picture
    qint64 maximumPixels = 4000000;         ///< The budget of the picture (a size step)
    QRectF pageRectangle;                   ///< Underlay: the crop box
    QSize imageSize;                        ///< Underlay: the size of its picture
    bool isStarted = false;                 ///< GUI thread only: it was given to the pool (counted as running)

    PDFContentDisplayImage image;           ///< The result of an image
    PDFContentDisplayShading shading;       ///< The result of a shading (also of a shading pattern)
    PDFContentDisplayTilingCell cell;       ///< The result of a tiling pattern
    QImage picture;                         ///< The result of the underlay

    virtual bool isOperationCanceled() const override
    {
        return m_isCanceled.load(std::memory_order_relaxed);
    }

    void cancel()
    {
        m_isCanceled.store(true, std::memory_order_relaxed);
    }

    /// Runs in a worker
    void run()
    {
        PDFContentDisplayImageOptions options;
        options.pageIndex = pageIndex;
        options.cms = cms.data();
        options.maximumPixels = maximumPixels;
        options.newObjects = snapshot ? &snapshot->newObjects : nullptr;
        switch (kind)
        {
            case PDFAppEditContentDecodeKind::Image:
                image = PDFContentDisplay::decodeImage(document.data(), container, node, options, this);
                break;

            case PDFAppEditContentDecodeKind::Shading:
                shading = PDFContentDisplay::getShading(document.data(), container, node, false, options, this);
                break;

            case PDFAppEditContentDecodeKind::FillPattern:
            case PDFAppEditContentDecodeKind::StrokePattern:
            {
                // A tiling pattern, else a shading pattern (item 2 of the fix package)
                const bool isStroke = kind == PDFAppEditContentDecodeKind::StrokePattern;
                cell = PDFContentDisplay::getTilingCell(document.data(), container, node, isStroke, options, this);
                if (cell.status == PDFContentDisplayTilingCell::Status::NotTilingPattern && !isOperationCanceled())
                {
                    shading = PDFContentDisplay::getShading(document.data(), container, node, isStroke, options, this);
                }
                break;
            }

            case PDFAppEditContentDecodeKind::Underlay:
                renderUnderlay();
                break;
        }
    }

private:
    /// The annotations and the fields of the page (their appearances, as the view of the application draws them) into a
    /// transparent picture of the crop box; the objects of the drawing are created and destroyed in this thread
    void renderUnderlay()
    {
        const PDFDocument* pageDocument = document.data();
        const PDFCatalog* catalog = pageDocument ? pageDocument->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()) || imageSize.isEmpty() || pageRectangle.isEmpty())
        {
            return;
        }

        PDFOptionalContentActivity activity(pageDocument, OCUsage::View, nullptr);
        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(pageDocument);
        cmsManager.setSettings(cmsManager.getDefaultSettings());
        const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(pageDocument), &activity);
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        fontCache.setDocument(modifiedDocument);
        PDFFontCacheShrinkGuard shrinkGuard(&fontCache, this);
        PDFICCProfileCache iccProfileCache;
        iccProfileCache.setDocument(modifiedDocument);

        PDFAnnotationManager manager(&fontCache, &iccProfileCache, &cmsManager, &activity, PDFMeshQualitySettings(), PDFRenderer::getDefaultFeatures(), PDFAnnotationManager::Target::View, nullptr);
        manager.setDocument(modifiedDocument);

        QImage result(imageSize, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        const qreal scaleX = imageSize.width() / pageRectangle.width();
        const qreal scaleY = imageSize.height() / pageRectangle.height();
        const QTransform pageToImage(scaleX, 0.0, 0.0, -scaleY, -pageRectangle.left() * scaleX, pageRectangle.bottom() * scaleY);
        QList<PDFRenderError> errors;
        try
        {
            QPainter painter(&result);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            PDFTextLayoutGetter layoutGetter(nullptr, pageIndex);
            manager.drawPage(&painter, pageIndex, nullptr, layoutGetter, pageToImage, PDFColorConvertor(), errors);
        }
        catch (const PDFException&)
        {
            return;
        }
        catch (const PDFRendererException&)
        {
            return;
        }

        if (!isOperationCanceled())
        {
            picture = std::move(result);
        }
    }

    std::atomic_bool m_isCanceled = false;
};

void PDFAppEditContentDecodeLink::finished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job)
{
    QMutexLocker locker(&m_mutex);
    if (m_scene)
    {
        PDFThreadAnnotations::happensBefore(job.get());
        PDFThreadAnnotations::TransportScope transportScope;
        QMetaObject::invokeMethod(m_scene, &PDFAppEditContentScene::onDecodeFinished, Qt::QueuedConnection, job);
    }
}

/// The task of the pool: it holds the job, never the scene
class PDFAppEditContentDecodeTask
{
public:
    explicit PDFAppEditContentDecodeTask(std::shared_ptr<PDFAppEditContentDecodeJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        if (!m_job->isOperationCanceled())
        {
            m_job->run();
        }
        const std::shared_ptr<PDFAppEditContentDecodeLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppEditContentDecodeJob> m_job;
};

/// The paper: the crop box on the gray area (layer Paper)
class PDFAppEditContentPaperItem : public QGraphicsItem
{
public:
    explicit PDFAppEditContentPaperItem() = default;

    void setRectangle(const QRectF& rectangle)
    {
        prepareGeometryChange();
        m_rectangle = rectangle;
    }

    virtual QRectF boundingRect() const override
    {
        return m_rectangle.adjusted(-2.0, -2.0, 2.0, 2.0);
    }

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        if (m_rectangle.isEmpty())
        {
            return;
        }

        // The paper of the document is white (a value of the document, not of the theme); its edge a border token
        PDFPainterStateGuard guard(painter);
        painter->fillRect(m_rectangle, Qt::white);
        QPen pen(PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderDefault));
        pen.setCosmetic(true);
        pen.setWidthF(1.0);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rectangle);
    }

private:
    QRectF m_rectangle;
};

/// The parent of the items of the page (layer Content): paints nothing; hidden during Preview
class PDFAppEditContentContentRootItem : public QGraphicsItem
{
public:
    explicit PDFAppEditContentContentRootItem()
    {
        setFlag(QGraphicsItem::ItemHasNoContents, true);
    }

    virtual QRectF boundingRect() const override { return QRectF(); }
    virtual void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override { }
};

/// The picture of Preview over the paper (layer Content, above the content root, which is hidden then)
class PDFAppEditContentPreviewPictureItem : public QGraphicsItem
{
public:
    explicit PDFAppEditContentPreviewPictureItem(PDFAppEditContentContext* context) :
        m_context(context)
    {

    }

    void setRectangle(const QRectF& rectangle)
    {
        prepareGeometryChange();
        m_rectangle = rectangle;
    }

    virtual QRectF boundingRect() const override
    {
        return m_rectangle;
    }

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const PDFAppEditContentPreview* preview = m_context ? m_context->getPreview() : nullptr;
        const PDFDocument* document = m_context ? m_context->getSession()->getDocument() : nullptr;
        if (!preview || preview->getImage().isNull() || !document)
        {
            return;
        }

        // The picture covers the rectangle of its request (the page space); its pixels by the matrix of the request. A
        // picture of another page is not drawn on this paper (R6-18; the new picture comes)
        const PDFAppEditContentPreviewRequest& request = preview->getImageRequest();
        if (request.pageIndex != m_context->getPageIndex())
        {
            return;
        }
        const PDFCatalog* catalog = document->getCatalog();
        const PDFPage* page = request.pageIndex >= 0 && request.pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(request.pageIndex) : nullptr;
        if (!page)
        {
            return;
        }

        const QTransform pageToImage = PDFAppEditContentPreview::getPageToImageMatrix(request, page->getPageRotation());
        if (!pageToImage.isInvertible())
        {
            return;
        }

        PDFPainterStateGuard guard(painter);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->setWorldTransform(pageToImage.inverted(), true);
        painter->drawImage(QPointF(0.0, 0.0), preview->getImage());
    }

private:
    QPointer<PDFAppEditContentContext> m_context;
    QRectF m_rectangle;
};

/// The one item, which paints the preview of the active tool (layer ToolPreview); it covers the rectangle the tool
/// paints (R6-11), so an update of the tool repaints only that
class PDFAppEditContentToolPreviewItem : public QGraphicsItem
{
public:
    explicit PDFAppEditContentToolPreviewItem(PDFAppEditContentContext* context) :
        m_context(context)
    {

    }

    void setRectangle(const QRectF& rectangle)
    {
        if (rectangle == m_rectangle)
        {
            return;
        }
        prepareGeometryChange();
        m_rectangle = rectangle;
    }

    virtual QRectF boundingRect() const override
    {
        return m_rectangle;
    }

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const PDFAppEditContentToolController* controller = m_context ? m_context->getToolController() : nullptr;
        const PDFAppEditContentTool* tool = controller ? controller->getActiveTool() : nullptr;
        const PDFAppEditContentScene* scene = dynamic_cast<const PDFAppEditContentScene*>(this->scene());
        if (!tool || !scene)
        {
            return;
        }

        PDFAppEditContentToolPaintContext paintContext;
        paintContext.pixelSize = scene->getPixelSize();
        const PDFAppEditContentView* view = m_context->getView();
        paintContext.isFocused = view && view->hasFocus();

        PDFPainterStateGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        tool->paint(painter, paintContext);
    }

private:
    QPointer<PDFAppEditContentContext> m_context;
    QRectF m_rectangle;
};

/// Helpers of the scene (static functions only)
class PDFAppEditContentSceneHelper
{
public:
    PDFAppEditContentSceneHelper() = delete;

    /// The steps of a walk up to the child of the edited root (the depth of the tree with a reserve)
    static constexpr int MAXIMUM_PARENT_STEPS = PDFContentContainer::MAXIMUM_DEPTH + 16;

    /// Most pixels of the picture of the annotations of a page (4 points of the page per pixel at most)
    static constexpr qreal MAXIMUM_UNDERLAY_PIXELS = 4000000.0;
    static constexpr qreal MAXIMUM_UNDERLAY_SCALE = 4.0;

    /// The two pointers name one node (the owner comparison never reads an expired node)
    static bool isSameNode(const std::weak_ptr<const PDFContentNode>& known, const PDFContentNodePointer& node)
    {
        return !known.owner_before(node) && !node.owner_before(known) && !known.expired();
    }

    /// The kinds of the nodes, which have no item (drawn by their parent, or nothing to draw)
    static bool hasNoItem(PDFContentNodeKind kind)
    {
        return kind == PDFContentNodeKind::Root || kind == PDFContentNodeKind::StateRun || kind == PDFContentNodeKind::MarkedPoint || kind == PDFContentNodeKind::TextShow;
    }

    /// The kinds of the nodes, whose children are walked (a text object draws its shows itself)
    static bool hasChildItems(PDFContentNodeKind kind)
    {
        return kind == PDFContentNodeKind::Root || kind == PDFContentNodeKind::Group || kind == PDFContentNodeKind::MarkedContent;
    }

    /// The kinds of the nodes, which can be selected on the canvas (zadani_gui.md 4.15)
    static bool isSelectableKind(PDFContentNodeKind kind)
    {
        switch (kind)
        {
            case PDFContentNodeKind::Group:
            case PDFContentNodeKind::MarkedContent:
            case PDFContentNodeKind::TextObject:
            case PDFContentNodeKind::Path:
            case PDFContentNodeKind::Image:
            case PDFContentNodeKind::FormUse:
            case PDFContentNodeKind::Shading:
                return true;

            default:
                break;
        }
        return false;
    }

    /// The node sets a clip for the content behind it in its scope: a path with W / W*, a text of the rendering modes
    /// 4 - 7 (ISO 32000-2, 8.5.4, 9.3.6)
    static bool hasClip(const PDFContentNode& node)
    {
        switch (node.getKind())
        {
            case PDFContentNodeKind::Path:
                return node.getData().paint.clip != PDFContentPathPaint::Clip::None;

            case PDFContentNodeKind::TextObject:
                return node.getData().clipsByText;

            default:
                break;
        }
        return false;
    }

    /// The clip of a clipping node in the coordinates of its item (the default space of the container), nullopt =
    /// the node does not clip. A text takes the glyphs of its item (its clip takes effect at ET, item 3).
    static std::optional<QPainterPath> getClipPath(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context, const PDFAppEditContentItem* item)
    {
        if (node.getKind() == PDFContentNodeKind::TextObject)
        {
            const PDFAppEditContentTextItem* textItem = dynamic_cast<const PDFAppEditContentTextItem*>(item);
            return textItem ? textItem->getTextClip() : std::nullopt;
        }
        if (node.getKind() != PDFContentNodeKind::Path)
        {
            return std::nullopt;
        }

        const PDFContentPathPaint& paint = node.getData().paint;
        if (paint.clip == PDFContentPathPaint::Clip::None)
        {
            return std::nullopt;
        }

        const std::optional<PDFContentDisplayState> state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
        const Qt::FillRule fillRule = paint.clip == PDFContentPathPaint::Clip::EvenOdd ? Qt::OddEvenFill : Qt::WindingFill;
        QPainterPath path = PDFAppEditContentDisplayAccess::createPath(node.getData().path, fillRule);
        path.closeSubpath();
        if (state)
        {
            path = state->matrix.map(path);
        }
        path.setFillRule(fillRule);
        return path;
    }

    /// The chain with one more clip (O(1): the chain before it is shared)
    static PDFAppEditContentClipPathsPointer appendClip(const PDFAppEditContentClipPathsPointer& clips, QPainterPath path)
    {
        return std::make_shared<const PDFAppEditContentClipNode>(clips, std::move(path));
    }

    /// The number of the pixels of a picture
    static qint64 getPixels(const QImage& image)
    {
        return image.isNull() ? 0 : qint64(image.width()) * qint64(image.height());
    }
};

// ---------------------------------------------------------------- PDFAppEditContentScene

PDFAppEditContentScene::PDFAppEditContentScene(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    m_decodeLink = std::make_shared<PDFAppEditContentDecodeLink>(this);

    // The permanent items (never replaced): the paper, the content root, the picture of Preview, the selection, the
    // preview of the tool, the overlays of the registry
    PDFAppEditContentPaperItem* paper = new PDFAppEditContentPaperItem();
    paper->setZValue(getLayerZ(PDFAppEditContentSceneLayer::Paper));
    addItem(paper);
    m_paperItem = paper;

    m_contentRoot = new PDFAppEditContentContentRootItem();
    m_contentRoot->setZValue(getLayerZ(PDFAppEditContentSceneLayer::Content));
    addItem(m_contentRoot);

    PDFAppEditContentPreviewPictureItem* previewItem = new PDFAppEditContentPreviewPictureItem(context);
    previewItem->setZValue(getLayerZ(PDFAppEditContentSceneLayer::Content) + 1.0);
    previewItem->setVisible(false);
    addItem(previewItem);
    m_previewItem = previewItem;

    m_selectionItem = new PDFAppEditContentSelectionItem(context);
    m_selectionItem->setZValue(getLayerZ(PDFAppEditContentSceneLayer::Selection));
    addItem(m_selectionItem);

    PDFAppEditContentToolPreviewItem* toolPreview = new PDFAppEditContentToolPreviewItem(context);
    toolPreview->setZValue(getLayerZ(PDFAppEditContentSceneLayer::ToolPreview));
    addItem(toolPreview);
    m_toolPreviewItem = toolPreview;

    if (!context)
    {
        return;
    }

    for (const PDFAppEditContentOverlayEntry* entry : context->getRegistry()->getOverlays())
    {
        if (QGraphicsItem* overlay = entry->createOverlay(context, this))
        {
            if (overlay->scene() != this)
            {
                addItem(overlay);
            }
            overlay->setZValue(getLayerZ(entry->getLayer()));
            m_overlays.push_back(overlay);
            if (!m_underlayItem)
            {
                m_underlayItem = dynamic_cast<PDFAppEditContentUnderlayItem*>(overlay);
            }
        }
    }

    // One environment of the host for the scene (R6-20: not one per decoding)
    m_cms = PDFAppEditContentDisplayAccess::getColorManagement(context);

    // The context, its session, its options, its selection and its controller live longer than the scene (a part); a
    // new session (reloadDocument) is connected by sessionReplaced
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentScene::onPageChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppEditContentScene::onContainerChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentScene::onPreviewChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentScene::onSessionReplaced);
    connectSession();
    PDFDeferredDeletion::connect(context->getOptions(), &PDFAppEditContentOptions::changed, this, &PDFAppEditContentScene::onOptionChanged);
    PDFDeferredDeletion::connect(context->getSelection(), &PDFAppEditContentSelection::changed, this, &PDFAppEditContentScene::onSelectionChanged);
    PDFDeferredDeletion::connect(context->getToolController(), &PDFAppEditContentToolController::stateChanged, this, &PDFAppEditContentScene::updateToolPreview);
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppEditContentScene::onThemeChanged);

    updateOptions();
    rebuild();
    onPreviewChanged();
}

PDFAppEditContentScene::~PDFAppEditContentScene()
{
    // No late result reaches the scene; the jobs end by themselves (they hold their data). The items (also the
    // retired ones, which are still in the scene) are deleted by QGraphicsScene.
    m_decodeLink->detach();
    cancelJobs();
    m_liveItems.clear();
    m_formItems.clear();
    m_records.clear();
    m_retiredItems.clear();
    m_gestureItems.clear();
}

qreal PDFAppEditContentScene::getLayerZ(PDFAppEditContentSceneLayer layer)
{
    return qreal(int(layer)) * LAYER_STEP;
}

void PDFAppEditContentScene::connectSession()
{
    // The signals of the session of the context (the session is a child of the context; it is replaced by
    // reloadDocument - the old one is disconnected here before it is deleted)
    PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (session == m_session.data())
    {
        return;
    }
    if (m_session)
    {
        disconnect(m_session.data(), nullptr, this, nullptr);
    }

    m_session = session;
    if (session)
    {
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::containerLoaded, this, &PDFAppEditContentScene::onContainerLoaded);
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::modelChanged, this, &PDFAppEditContentScene::onModelChanged);
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::editorStateChanged, this, &PDFAppEditContentScene::onEditorStateChanged);
    }
}

void PDFAppEditContentScene::rebuild()
{
    synchronize(true);
}

void PDFAppEditContentScene::updateNodes(const PDFContentChange& change)
{
    if (!m_context || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    bool isPageChanged = false;
    std::set<PDFContentContainerKey> forms;
    for (const PDFContentContainerKey& key : change.containers)
    {
        if (key == m_containerKey)
        {
            isPageChanged = true;
        }
        else if (key.kind == PDFContentContainerKey::Kind::Form)
        {
            forms.insert(key);
        }
    }

    // The page: one walk, only the changed nodes get new items (the unchanged nodes are shared by the versions)
    if (isPageChanged)
    {
        synchronize(false);
    }

    // A changed form: the items of its uses are built again
    if (!forms.empty())
    {
        requestMissingForms();
    }
}

PDFAppEditContentItem* PDFAppEditContentScene::findItem(const PDFContentNodeReference& node) const
{
    if (node.container != m_containerKey)
    {
        return nullptr;
    }

    const auto it = m_records.find(node.node);
    return it != m_records.cend() ? it->second.item : nullptr;
}

std::vector<PDFContentNodeReference> PDFAppEditContentScene::getNodesAt(const QPointF& scenePoint, qreal tolerance, bool isLockedIncluded) const
{
    std::vector<PDFContentNodeReference> result;
    if (!m_context || !m_contentRoot->isVisible())
    {
        return result;
    }

    const PDFContentContainerPointer container = getContainer(m_containerKey);
    if (!container)
    {
        return result;
    }

    // The items under the point from the top (the stacking order of the content), each mapped to the child of the
    // edited root, which contains it; the children already returned by a set (R6-4: no search over the result)
    const QRectF area(scenePoint.x() - tolerance, scenePoint.y() - tolerance, 2.0 * tolerance, 2.0 * tolerance);
    const QList<QGraphicsItem*> candidates = items(area, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder);
    std::set<PDFContentNodeReference> returned;
    for (QGraphicsItem* candidate : candidates)
    {
        PDFAppEditContentItem* item = qgraphicsitem_cast<PDFAppEditContentItem*>(candidate);
        if (!item || !item->isVisible())
        {
            continue;
        }

        // The item of the page, which contains it (an item of a form: its form item); bounded by the nesting of forms
        QGraphicsItem* top = item;
        int steps = 0;
        while (top->parentItem() && top->parentItem() != m_contentRoot && steps++ < PDFContentEditSession::MAXIMUM_CONTAINER_NESTING + 1)
        {
            top = top->parentItem();
        }
        PDFAppEditContentItem* pageItem = top->parentItem() == m_contentRoot ? qgraphicsitem_cast<PDFAppEditContentItem*>(top) : nullptr;
        if (!pageItem || !item->isHit(scenePoint, tolerance))
        {
            continue;
        }

        const PDFContentNodeReference child = getEditedRootChild(PDFContentNodeReference{ m_containerKey, pageItem->getNodeId() });
        if (!child.isValid() || returned.count(child))
        {
            continue;
        }
        const PDFContentNodePointer childNode = container->getNode(child.node);
        if (!childNode || !PDFAppEditContentSceneHelper::isSelectableKind(childNode->getKind()))
        {
            continue;
        }
        if (!isLockedIncluded && m_context->getSession()->isNodeLocked(child))
        {
            continue;
        }
        returned.insert(child);
        result.push_back(child);
    }

    return result;
}

std::vector<PDFContentNodeReference> PDFAppEditContentScene::getNodesIn(const QRectF& sceneRectangle) const
{
    // The boxes of the one policy of the box (PDFAppEditContentGeometryHelper), the same as the frames
    std::vector<PDFContentNodeReference> result;
    const QRectF rectangle = sceneRectangle.normalized();
    for (const PDFContentNodeReference& node : getSelectableNodes())
    {
        // Inside by the edges (a box of a line has no height; QRectF::contains refuses such a rectangle)
        const QRectF box = PDFAppEditContentGeometryHelper::getPageBoundingBox(m_context.data(), node);
        const bool isInside = box.left() >= rectangle.left() && box.right() <= rectangle.right() && box.top() >= rectangle.top() && box.bottom() <= rectangle.bottom();
        if (!box.isNull() && isInside)
        {
            result.push_back(node);
        }
    }
    return result;
}

QRectF PDFAppEditContentScene::getPaperRectangle() const
{
    return m_paperRectangle;
}

QRectF PDFAppEditContentScene::getContentRectangle() const
{
    return m_contentRoot->childrenBoundingRect();
}

PDFAppEditContentSelectionItem* PDFAppEditContentScene::getSelectionItem() const
{
    return m_selectionItem;
}

void PDFAppEditContentScene::updateToolPreview()
{
    // R6-11: the item covers what the active tool paints; prepareGeometryChange repaints the old rectangle, update the
    // new one - not the whole scene at every move of a drag
    const PDFAppEditContentToolController* controller = m_context ? m_context->getToolController() : nullptr;
    const PDFAppEditContentTool* tool = controller ? controller->getActiveTool() : nullptr;
    const QRectF rectangle = tool ? tool->getPreviewRectangle(getPixelSize()) : QRectF();
    PDFAppEditContentToolPreviewItem* item = static_cast<PDFAppEditContentToolPreviewItem*>(m_toolPreviewItem);
    item->setRectangle(rectangle);
    item->update();
}

int PDFAppEditContentScene::getRetiredItemCount() const
{
    return int(m_retiredItems.size());
}

int PDFAppEditContentScene::getContentItemCount() const
{
    return int(m_liveItems.size());
}

int PDFAppEditContentScene::getDecodingCount() const
{
    return int(m_itemJobs.size() + m_pictureJobs.size());
}

PDFContentDisplayContext PDFAppEditContentScene::getDisplayContext() const
{
    // The document converts the colors of the overrides in its spaces; the color management of the application, as the
    // builder converts the colors of the content (without a GUI the generic one)
    PDFContentDisplayContext context;
    if (m_context)
    {
        context.document = m_context->getSession()->getDocument();
        context.cms = m_cms.data();
    }
    return context;
}

PDFContentContainerPointer PDFAppEditContentScene::getContainer(const PDFContentContainerKey& key) const
{
    return m_context && key.isValid() ? m_context->getSession()->getContainer(key) : PDFContentContainerPointer();
}

std::vector<PDFContentNodeReference> PDFAppEditContentScene::getSelectableNodes() const
{
    std::vector<PDFContentNodeReference> result;
    if (!m_context)
    {
        return result;
    }

    const PDFContentNodeReference editedRoot = m_context->getEditedRoot();
    const PDFContentContainerPointer container = getContainer(editedRoot.container);
    const PDFContentNodePointer root = container ? container->getNode(editedRoot.node) : PDFContentNodePointer();
    if (!root)
    {
        return result;
    }

    // The children of the edited root (no walk deeper), without the locked ones
    const PDFContentEditSession* session = m_context->getSession();
    for (const PDFContentNodePointer& child : root->getChildren())
    {
        const PDFContentNodeReference reference{ editedRoot.container, child->getId() };
        if (PDFAppEditContentSceneHelper::isSelectableKind(child->getKind()) && !session->isNodeLocked(reference))
        {
            result.push_back(reference);
        }
    }
    return result;
}

PDFContentNodeReference PDFAppEditContentScene::getEditedRootChild(const PDFContentNodeReference& node) const
{
    if (!m_context || node.container != m_containerKey)
    {
        return PDFContentNodeReference();
    }

    const PDFContentNodeReference editedRoot = m_context->getEditedRoot();
    if (editedRoot.container != m_containerKey || node.node == editedRoot.node)
    {
        return PDFContentNodeReference();
    }

    // Up by the parents of the records (bounded by the depth of the tree)
    PDFContentNodeId current = node.node;
    for (int step = 0; step < PDFAppEditContentSceneHelper::MAXIMUM_PARENT_STEPS; ++step)
    {
        const auto it = m_records.find(current);
        if (it == m_records.cend())
        {
            return PDFContentNodeReference();
        }
        if (it->second.parentId == editedRoot.node)
        {
            return PDFContentNodeReference{ m_containerKey, current };
        }
        if (!it->second.parentId.isValid())
        {
            return PDFContentNodeReference();
        }
        current = it->second.parentId;
    }
    return PDFContentNodeReference();
}

void PDFAppEditContentScene::setGestureTransform(const std::vector<PDFContentNodeReference>& nodes, const QTransform& transform)
{
    if (!m_isGestureActive || nodes != m_gestureNodes)
    {
        clearGestureTransform();
        m_gestureNodes = nodes;
        m_isGestureActive = true;

        // The items of the nodes with their subtrees; more than the limit (or a complex page): only the outline
        const PDFContentContainerPointer container = getContainer(m_containerKey);
        std::vector<const PDFContentNode*> stack;
        for (const PDFContentNodeReference& node : nodes)
        {
            const PDFContentNodePointer start = container && node.container == m_containerKey ? container->getNode(node.node) : PDFContentNodePointer();
            if (start)
            {
                stack.push_back(start.get());
            }
        }

        m_isGestureOutlineOnly = m_isComplexPage;
        while (!stack.empty() && !m_isGestureOutlineOnly)
        {
            const PDFContentNode* current = stack.back();
            stack.pop_back();
            if (PDFAppEditContentItem* item = findItem(PDFContentNodeReference{ m_containerKey, current->getId() }))
            {
                m_gestureItems.push_back(item);
            }
            if (PDFAppEditContentSceneHelper::hasChildItems(current->getKind()))
            {
                for (const PDFContentNodePointer& child : current->getChildren())
                {
                    stack.push_back(child.get());
                }
            }
            if (int(m_gestureItems.size()) > GESTURE_ITEM_LIMIT)
            {
                m_isGestureOutlineOnly = true;
            }
        }
        if (m_isGestureOutlineOnly)
        {
            m_gestureItems.clear();
        }
    }

    m_gestureTransform = transform;
    for (PDFAppEditContentItem* item : m_gestureItems)
    {
        item->setGestureTransform(transform);
    }
    m_selectionItem->updateGeometry();
}

void PDFAppEditContentScene::clearGestureTransform()
{
    for (PDFAppEditContentItem* item : m_gestureItems)
    {
        item->setGestureTransform(QTransform());
    }
    m_gestureItems.clear();
    m_gestureNodes.clear();
    m_gestureTransform = QTransform();
    const bool wasActive = m_isGestureActive;
    m_isGestureActive = false;
    m_isGestureOutlineOnly = false;
    if (wasActive)
    {
        m_selectionItem->updateGeometry();
    }
}

qreal PDFAppEditContentScene::getPixelSize() const
{
    const QList<QGraphicsView*> sceneViews = views();
    if (sceneViews.isEmpty())
    {
        return 1.0;
    }

    const QTransform& transform = sceneViews.front()->transform();
    const qreal scale = std::sqrt(std::abs(transform.determinant()));
    return scale > 0.0 ? 1.0 / scale : 1.0;
}

void PDFAppEditContentScene::startJob(const std::shared_ptr<PDFAppEditContentDecodeJob>& job)
{
    // At most MAXIMUM_RUNNING_JOBS of the scene in the global pool (the other work of the application is not starved by
    // one page of 10 000 images, R6-5); the others wait here in the order of their requests
    if (m_runningJobCount >= MAXIMUM_RUNNING_JOBS)
    {
        m_waitingJobs.push_back(job);
        return;
    }

    ++m_runningJobCount;
    ++m_startedJobCount;
    job->isStarted = true;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppEditContentDecodeTask(job));
}

void PDFAppEditContentScene::startWaitingJobs()
{
    while (m_runningJobCount < MAXIMUM_RUNNING_JOBS && !m_waitingJobs.empty())
    {
        std::shared_ptr<PDFAppEditContentDecodeJob> job = std::move(m_waitingJobs.front());
        m_waitingJobs.pop_front();
        if (!job->isOperationCanceled())
        {
            startJob(job);
        }
    }
}

void PDFAppEditContentScene::cancelJobs()
{
    // The running jobs end by themselves (their results are dropped by the identity of the job); the waiting ones never
    // start
    for (auto& job : m_itemJobs)
    {
        job.second->cancel();
    }
    for (auto& job : m_pictureJobs)
    {
        job.second->cancel();
    }
    for (const std::shared_ptr<PDFAppEditContentDecodeJob>& job : m_waitingJobs)
    {
        job->cancel();
    }
    m_itemJobs.clear();
    m_pictureJobs.clear();
    m_waitingJobs.clear();
}

PDFContentEditSnapshotPointer PDFAppEditContentScene::getSnapshot()
{
    if (!m_snapshot && m_session)
    {
        m_snapshot = m_session->createSnapshot();
    }
    return m_snapshot;
}

void PDFAppEditContentScene::requestDecoding(PDFAppEditContentItem* item, PDFAppEditContentDecodeKind kind, qint64 pixels)
{
    if (!item || !m_context || PDFDeferredDeletion::isRetired(this) || m_liveItems.find(item->getSerial()) == m_liveItems.cend())
    {
        return;
    }

    PDFAppEditContentShadingItem* shadingItem = dynamic_cast<PDFAppEditContentShadingItem*>(item);
    PDFAppEditContentPathItem* pathItem = dynamic_cast<PDFAppEditContentPathItem*>(item);
    const bool isPattern = kind == PDFAppEditContentDecodeKind::FillPattern || kind == PDFAppEditContentDecodeKind::StrokePattern;
    if (!(kind == PDFAppEditContentDecodeKind::Shading && shadingItem) && !(isPattern && pathItem))
    {
        return;
    }

    const PDFContentContainerKey key = shadingItem ? shadingItem->getContainerKey() : pathItem->getContainerKey();
    PDFDocumentPointer document = PDFAppEditContentDisplayAccess::getDocument(m_context.data());
    PDFContentContainerPointer container = getContainer(key);
    if (!document || !container)
    {
        // Nothing can be computed: the placeholder stays
        if (shadingItem)
        {
            PDFContentDisplayShading failed;
            failed.status = PDFContentDisplayShading::Status::Failed;
            shadingItem->setShading(failed);
        }
        else
        {
            PDFAppEditContentPatternPaint paint;
            paint.isDone = true;
            pathItem->setPatternPaint(kind == PDFAppEditContentDecodeKind::StrokePattern, paint);
        }
        return;
    }

    // A job of the older version or zoom step of the item is canceled (its result is dropped by the identity of the job)
    const std::pair<quint64, int> jobKey(item->getSerial(), int(kind));
    auto it = m_itemJobs.find(jobKey);
    if (it != m_itemJobs.end())
    {
        it->second->cancel();
        m_itemJobs.erase(it);
    }

    auto job = std::make_shared<PDFAppEditContentDecodeJob>();
    job->link = m_decodeLink;
    job->document = std::move(document);
    job->container = std::move(container);
    job->cms = m_cms;
    job->snapshot = getSnapshot();
    job->node = item->getNodeId();
    job->pageIndex = m_context->getPageIndex();
    job->serial = item->getSerial();
    job->kind = kind;
    job->maximumPixels = pixels;
    m_itemJobs[jobKey] = job;
    startJob(job);
}

const QImage* PDFAppEditContentScene::getPicture(const PDFAppEditContentImageItem* item, qreal pixels, bool* isFailed)
{
    *isFailed = false;
    if (!item || !m_context || PDFDeferredDeletion::isRetired(this))
    {
        return nullptr;
    }

    const PDFAppEditContentPictureKey& key = item->getPictureKey();
    Picture& entry = m_pictures[key];
    entry.serials.insert(item->getSerial());
    *isFailed = entry.isFailed && entry.image.isNull();

    // A larger size step than the picture has: one decoding of the key (the uses of the image share it)
    const qint64 step = PDFAppEditContentDisplayAccess::getPixelStep(pixels);
    if (!entry.isFailed && !entry.isComplete && !entry.isOverBudget && entry.step < step)
    {
        auto it = m_pictureJobs.find(key);
        if (it == m_pictureJobs.end() || it->second->maximumPixels < step)
        {
            if (it != m_pictureJobs.end())
            {
                it->second->cancel();
                m_pictureJobs.erase(it);
            }

            PDFDocumentPointer document = PDFAppEditContentDisplayAccess::getDocument(m_context.data());
            PDFContentContainerPointer container = getContainer(item->getContainerKey());
            if (!document || !container)
            {
                entry.isFailed = true;
                *isFailed = entry.image.isNull();
            }
            else
            {
                auto job = std::make_shared<PDFAppEditContentDecodeJob>();
                job->link = m_decodeLink;
                job->document = std::move(document);
                job->container = std::move(container);
                job->cms = m_cms;
                job->node = item->getNodeId();
                job->pageIndex = m_context->getPageIndex();
                job->serial = item->getSerial();
                job->kind = PDFAppEditContentDecodeKind::Image;
                job->pictureKey = key;
                job->maximumPixels = step;
                m_pictureJobs[key] = job;
                startJob(job);
            }
        }
    }

    return entry.image.isNull() ? nullptr : &entry.image;
}

bool PDFAppEditContentScene::hasPicture(const PDFAppEditContentPictureKey& key) const
{
    const auto it = m_pictures.find(key);
    return it != m_pictures.cend() && !it->second.image.isNull();
}

void PDFAppEditContentScene::setPictureBudget(qint64 pixels)
{
    m_pictureBudget = qMax<qint64>(0, pixels);
}

void PDFAppEditContentScene::setFormItemBudget(size_t budget)
{
    m_formItemBudget = budget;
    rebuild();
}

void PDFAppEditContentScene::clearPictures()
{
    for (auto& job : m_pictureJobs)
    {
        job.second->cancel();
    }
    m_pictureJobs.clear();
    m_pictures.clear();
    m_picturePixels = 0;
}

void PDFAppEditContentScene::onDecodeFinished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    if (job->isStarted)
    {
        job->isStarted = false;
        --m_runningJobCount;
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    startWaitingJobs();
    if (job->isOperationCanceled())
    {
        return;
    }

    // A job, which is not the current one of its request (canceled, an older version or zoom step, a retired item, an old
    // session) is dropped
    switch (job->kind)
    {
        case PDFAppEditContentDecodeKind::Image:
        {
            const auto it = m_pictureJobs.find(job->pictureKey);
            if (it == m_pictureJobs.end() || it->second != job)
            {
                return;
            }
            m_pictureJobs.erase(it);

            Picture& entry = m_pictures[job->pictureKey];
            const PDFContentDisplayImage& image = job->image;
            if (image.status == PDFContentDisplayImage::Status::Decoded && !image.image.isNull())
            {
                // R6-5: within the budget of the scene; over it the placeholder (or the smaller picture the key has) -
                // nothing is evicted, so the visible pictures are never decoded again and again
                const qint64 pixels = PDFAppEditContentSceneHelper::getPixels(image.image);
                const qint64 oldPixels = PDFAppEditContentSceneHelper::getPixels(entry.image);
                if (m_picturePixels + pixels - oldPixels <= m_pictureBudget)
                {
                    m_picturePixels += pixels - oldPixels;
                    entry.image = image.image;
                    entry.step = job->maximumPixels;
                    const qint64 sourcePixels = qint64(image.sourceSize.width()) * qint64(image.sourceSize.height());
                    entry.isComplete = sourcePixels <= pixels || job->maximumPixels >= PDFContentDisplayImageOptions().maximumPixels;
                }
                else
                {
                    entry.isOverBudget = true;
                }
            }
            else if (entry.image.isNull())
            {
                entry.isFailed = true;
            }
            else
            {
                // A larger picture cannot be decoded: the smaller one stays
                entry.isComplete = true;
            }

            // The items, which painted the picture, paint the new one (the retired ones are forgotten)
            for (auto serialIt = entry.serials.begin(); serialIt != entry.serials.end();)
            {
                const auto itemIt = m_liveItems.find(*serialIt);
                if (itemIt == m_liveItems.end())
                {
                    serialIt = entry.serials.erase(serialIt);
                    continue;
                }
                itemIt->second->update();
                ++serialIt;
            }
            return;
        }

        case PDFAppEditContentDecodeKind::Underlay:
        {
            const auto it = m_itemJobs.find(std::make_pair(quint64(0), int(job->kind)));
            if (it == m_itemJobs.end() || it->second != job)
            {
                return;
            }
            m_itemJobs.erase(it);
            if (m_underlayItem && m_context && job->pageIndex == m_context->getPageIndex())
            {
                m_underlayItem->setPicture(job->picture, job->pageRectangle, job->pageIndex);
            }
            return;
        }

        default:
            break;
    }

    const auto it = m_itemJobs.find(std::make_pair(job->serial, int(job->kind)));
    if (it == m_itemJobs.end() || it->second != job)
    {
        return;
    }
    m_itemJobs.erase(it);

    const auto itemIt = m_liveItems.find(job->serial);
    if (itemIt == m_liveItems.end())
    {
        return;
    }

    if (job->kind == PDFAppEditContentDecodeKind::Shading)
    {
        if (PDFAppEditContentShadingItem* shadingItem = dynamic_cast<PDFAppEditContentShadingItem*>(itemIt->second))
        {
            shadingItem->setShading(job->shading);
        }
    }
    else if (PDFAppEditContentPathItem* pathItem = dynamic_cast<PDFAppEditContentPathItem*>(itemIt->second))
    {
        pathItem->setPatternPaint(job->kind == PDFAppEditContentDecodeKind::StrokePattern, PDFAppEditContentDisplayAccess::createPatternPaint(job->cell, job->shading, job->maximumPixels));
    }
}

void PDFAppEditContentScene::retire()
{
    // No late result, no queued deletion runs any more (the items are deleted by the destructor); emits nothing
    m_decodeLink->detach();
    cancelJobs();
}

void PDFAppEditContentScene::deleteRetiredItems()
{
    m_isDeleteQueued = false;

    // From the event loop only: no event of an item is being processed (zadani_gui.md 4.14)
    std::vector<QGraphicsItem*> items;
    items.swap(m_retiredItems);
    for (QGraphicsItem* item : items)
    {
        delete item;
    }
}

PDFAppEditContentItem* PDFAppEditContentScene::createItem(const PDFContentContainer& container, const PDFContentNode& node, QGraphicsItem* parentItem)
{
    const PDFAppEditContentItemEntry* entry = m_context ? m_context->getRegistry()->findItem(node, container) : nullptr;
    if (!entry)
    {
        // A missing entry is a bug of the registry: reported once (pdfappeditcontentregistry.h)
        if (!m_isMissingItemReported)
        {
            m_isMissingItemReported = true;
            qWarning("PDFAppEditContentScene: no item entry draws a node of the kind %d", int(node.getKind()));
        }
        return nullptr;
    }

    PDFAppEditContentItemArguments arguments;
    arguments.container = &container;
    arguments.node = &node;
    arguments.scene = this;
    arguments.parent = qgraphicsitem_cast<PDFAppEditContentItem*>(parentItem);
    PDFAppEditContentItem* item = entry->createItem(arguments);
    if (!item)
    {
        return nullptr;
    }

    if (item->parentItem() != parentItem)
    {
        item->setParentItem(parentItem);
    }
    if (item->scene() != this)
    {
        addItem(item);
    }
    m_liveItems[item->getSerial()] = item;
    if (parentItem != m_contentRoot)
    {
        ++m_formItemCount;
    }
    if (dynamic_cast<PDFAppEditContentFormItem*>(item))
    {
        m_formItems.insert(item->getSerial());
    }
    return item;
}

void PDFAppEditContentScene::forgetItemTree(QGraphicsItem* root)
{
    // The item and its descendants (the items of a form, bounded by the nesting of the forms); the parents are still
    // set here, so an item of a form is known by its parent
    std::vector<QGraphicsItem*> stack{ root };
    while (!stack.empty())
    {
        QGraphicsItem* current = stack.back();
        stack.pop_back();
        if (PDFAppEditContentItem* item = qgraphicsitem_cast<PDFAppEditContentItem*>(current))
        {
            if (m_liveItems.erase(item->getSerial()) > 0 && item->parentItem() && item->parentItem() != m_contentRoot && m_formItemCount > 0)
            {
                --m_formItemCount;
            }
            m_formItems.erase(item->getSerial());
            for (int kind = int(PDFAppEditContentDecodeKind::Image); kind <= int(PDFAppEditContentDecodeKind::StrokePattern); ++kind)
            {
                auto job = m_itemJobs.find(std::make_pair(item->getSerial(), kind));
                if (job != m_itemJobs.end())
                {
                    job->second->cancel();
                    m_itemJobs.erase(job);
                }
            }
            m_gestureItems.erase(std::remove(m_gestureItems.begin(), m_gestureItems.end(), item), m_gestureItems.end());
        }
        for (QGraphicsItem* child : current->childItems())
        {
            stack.push_back(child);
        }
    }
}

void PDFAppEditContentScene::retireItem(PDFAppEditContentItem* item)
{
    if (!item)
    {
        return;
    }

    // Hidden now, deleted from the event loop: an item is never deleted inside of an event (zadani_gui.md 4.14). It
    // leaves its parent (a form item may be deleted before it).
    forgetItemTree(item);
    item->hide();
    if (item->parentItem())
    {
        item->setParentItem(nullptr);
    }
    m_retiredItems.push_back(item);
    if (!m_isDeleteQueued)
    {
        m_isDeleteQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentScene::deleteRetiredItems, Qt::QueuedConnection);
    }
}

void PDFAppEditContentScene::synchronize(bool isFullRebuild)
{
    using Helper = PDFAppEditContentSceneHelper;

    if (!m_context || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    // Any walk makes a queued walk of the editor state unnecessary (it reads the state too)
    m_isEditorStateWalkQueued = false;
    ++m_walkCount;

    PDFContentEditSession* session = m_context->getSession();
    const PDFContentContainerKey pageKey = m_context->getPageKey();
    const PDFContentContainerPointer container = getContainer(pageKey);

    // Another page (or a rebuild): every item goes; another page: the pictures of its images too
    if (pageKey != m_containerKey)
    {
        clearPictures();
    }
    if (isFullRebuild || pageKey != m_containerKey || !container)
    {
        clearGestureTransform();
        for (auto& record : m_records)
        {
            if (record.second.item)
            {
                retireItem(record.second.item);
            }
        }
        m_records.clear();
        m_containerVersion = 0;
    }
    m_containerKey = pageKey;
    updatePaper();
    if (!container || !container->getRoot())
    {
        updateComplexity();
        updateSceneRectangle();
        m_selectionItem->updateGeometry();
        return;
    }

    for (auto& record : m_records)
    {
        record.second.isVisited = false;
    }

    const PDFContentDisplayContext displayContext = getDisplayContext();
    const PDFContentNodeReference editedRoot = m_context->getEditedRoot();
    const bool isEditedRootOnPage = editedRoot.container == pageKey;

    // An iterative walk in the order of the content; an exit entry ends the clip scope of a group (q ... Q)
    struct Entry
    {
        PDFContentNodePointer node;
        PDFContentNodeId parentId;
        PDFAppEditContentClipPathsPointer savedClip;
        bool isExit = false;
        bool isRefreshed = false;   ///< An ancestor with an override changed: the display state of the subtree changed
        bool isLocked = false;
        bool isHidden = false;
        bool isInside = false;      ///< Inside of the edited root
    };

    std::vector<Entry> stack;
    PDFAppEditContentClipPathsPointer clip;
    qreal order = 0.0;
    {
        const PDFContentNodePointer& root = container->getRoot();
        const std::vector<PDFContentNodePointer>& children = root->getChildren();
        const bool isInside = !isEditedRootOnPage || editedRoot.node == root->getId();
        for (auto it = children.crbegin(); it != children.crend(); ++it)
        {
            stack.push_back(Entry{ *it, root->getId(), nullptr, false, false, false, false, isInside });
        }
        Record& rootRecord = m_records[root->getId()];
        rootRecord.node = root;
        rootRecord.isVisited = true;
    }

    while (!stack.empty())
    {
        Entry entry = std::move(stack.back());
        stack.pop_back();
        if (entry.isExit)
        {
            clip = std::move(entry.savedClip);
            continue;
        }

        const PDFContentNode& node = *entry.node;
        const PDFContentNodeKind kind = node.getKind();
        const PDFContentNodeReference reference{ pageKey, node.getId() };
        Record& record = m_records[node.getId()];
        const bool isSame = Helper::isSameNode(record.node, entry.node);
        const bool hadOverride = record.hasOverride;
        record.isVisited = true;
        record.parentId = entry.parentId;
        record.node = entry.node;
        record.hasOverride = node.getOverride() != nullptr;

        const bool isLocked = entry.isLocked || session->isNodeLocked(reference);
        const bool isHidden = entry.isHidden || session->isNodeHidden(reference);
        const bool isInside = entry.isInside || (isEditedRootOnPage && editedRoot.node == node.getId());

        if (!Helper::hasNoItem(kind))
        {
            PDFAppEditContentItem* item = record.item;
            bool isRebuilt = false;
            if (item && (!isSame || entry.isRefreshed))
            {
                isRebuilt = true;
                if (!item->updateNode(node, *container))
                {
                    retireItem(item);
                    item = nullptr;
                }
            }
            if (!item)
            {
                isRebuilt = true;
                item = createItem(*container, node, m_contentRoot);
            }
            record.item = item;
            if (item)
            {
                item->setZValue(order);
                if (item->getClipPaths() != clip)
                {
                    item->setClipPaths(clip);

                    // R6-19: the box of a shading is cut by the clips in effect - a new chain gives it again
                    if (!isRebuilt && kind == PDFContentNodeKind::Shading)
                    {
                        item->updateNode(node, *container);
                    }
                }
                item->setEditorState(isLocked, isHidden);
                item->setDimmed(isEditedRootOnPage && !isInside);
            }
            order += 1.0;
        }
        else if (record.item)
        {
            retireItem(record.item);
            record.item = nullptr;
        }

        // The clip of a path applies after its painting, the clip of a text at its end (W, W*, ISO 32000-2, 8.5.4; the
        // rendering modes 4 - 7, 9.3.6) - to the content behind it in its scope. An unchanged clipping node, which gets the
        // same chain as at the last walk, gives the same chain object again (R6-12: the items behind it are not
        // repainted, the path is not built again).
        if (Helper::hasClip(node))
        {
            if (isSame && !entry.isRefreshed && record.clipAfter && record.clipBefore == clip)
            {
                clip = record.clipAfter;
            }
            else
            {
                record.clipBefore = clip;
                if (std::optional<QPainterPath> clipPath = Helper::getClipPath(node, *container, displayContext, record.item))
                {
                    clip = Helper::appendClip(clip, std::move(*clipPath));
                }
                record.clipAfter = clip;
            }
        }
        else if (record.clipBefore || record.clipAfter)
        {
            record.clipBefore.reset();
            record.clipAfter.reset();
        }

        if (Helper::hasChildItems(kind))
        {
            const bool isRefreshed = entry.isRefreshed || (!isSame && (hadOverride || record.hasOverride));
            if (kind == PDFContentNodeKind::Group)
            {
                // q ... Q: the clips set inside end with the group
                stack.push_back(Entry{ nullptr, PDFContentNodeId(), clip, true });
            }
            const std::vector<PDFContentNodePointer>& children = node.getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                stack.push_back(Entry{ *it, node.getId(), nullptr, false, isRefreshed, isLocked, isHidden, isInside });
            }
        }
    }

    // The nodes, which are gone
    for (auto it = m_records.begin(); it != m_records.end();)
    {
        if (!it->second.isVisited)
        {
            if (it->second.item)
            {
                retireItem(it->second.item);
            }
            it = m_records.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_containerVersion = container->getVersion();
    requestMissingForms();
    updateComplexity();
    updateSceneRectangle();
    m_selectionItem->updateGeometry();
}

void PDFAppEditContentScene::requestMissingForms()
{
    // The forms of the page, whose containers are loaded and not built into their items yet (the session loads the
    // forms of a page with it), breadth first: the uses of the page first, then their nested uses (R6-1: the budget of
    // the items goes to the outer levels first). The nested uses built here join the queue.
    std::vector<FormBuild> queue;
    for (const quint64 serial : m_formItems)
    {
        const auto it = m_liveItems.find(serial);
        PDFAppEditContentFormItem* formItem = it != m_liveItems.end() ? dynamic_cast<PDFAppEditContentFormItem*>(it->second) : nullptr;
        const PDFContentContainerPointer form = formItem ? getContainer(formItem->getFormKey()) : PDFContentContainerPointer();
        if (!form || form->getVersion() == formItem->getFormVersion())
        {
            continue;
        }

        // The depth by the form items around it (bounded by the nesting of the forms)
        int depth = 1;
        for (QGraphicsItem* parent = formItem->parentItem(); parent && parent != m_contentRoot && depth <= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING + 1; parent = parent->parentItem())
        {
            if (dynamic_cast<const PDFAppEditContentFormItem*>(parent))
            {
                ++depth;
            }
        }
        queue.push_back(FormBuild{ depth, serial });
    }
    std::stable_sort(queue.begin(), queue.end());

    for (size_t index = 0; index < queue.size(); ++index)
    {
        const FormBuild build = queue[index];
        const auto it = m_liveItems.find(build.serial);
        if (it == m_liveItems.end())
        {
            // Retired meanwhile (a form of a form built again)
            continue;
        }
        if (PDFAppEditContentFormItem* formItem = dynamic_cast<PDFAppEditContentFormItem*>(it->second))
        {
            buildForm(formItem, build.depth, &queue);
        }
    }
}

void PDFAppEditContentScene::buildForm(PDFAppEditContentFormItem* formItem, int nestingDepth, std::vector<FormBuild>* queue)
{
    using Helper = PDFAppEditContentSceneHelper;

    const PDFContentContainerKey formKey = formItem->getFormKey();
    const PDFContentContainerPointer form = getContainer(formKey);
    if (!form || !form->getRoot() || form->getVersion() == formItem->getFormVersion())
    {
        return;
    }

    // A form nested too deep or drawing itself (a cycle through the keys of the forms around the use - a form shared by
    // two uses is drawn by both) keeps the outline of its box
    if (nestingDepth > PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
    {
        return;
    }
    int steps = 0;
    for (QGraphicsItem* parent = formItem->parentItem(); parent && parent != m_contentRoot && steps <= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING + 1; parent = parent->parentItem(), ++steps)
    {
        const PDFAppEditContentFormItem* outer = dynamic_cast<const PDFAppEditContentFormItem*>(parent);
        if (outer && outer->getFormKey() == formKey)
        {
            return;
        }
    }

    // The old children (another version of the form) go
    const QList<QGraphicsItem*> oldChildren = formItem->childItems();
    for (QGraphicsItem* child : oldChildren)
    {
        if (PDFAppEditContentItem* item = qgraphicsitem_cast<PDFAppEditContentItem*>(child))
        {
            retireItem(item);
        }
    }

    // R6-1: the items of all forms of the page under one budget - a form, whose nodes do not fit any more, keeps the
    // outline of its box (the count of the nodes is known per version, PDFContentDisplay::getCounts)
    const size_t cost = PDFAppEditContentDisplayAccess::getCounts(*form).nodeCount;
    if (m_formItemCount + cost > m_formItemBudget)
    {
        if (formItem->getFormVersion() != 0)
        {
            formItem->resetForm();
        }
        return;
    }
    formItem->setForm(*form);

    // The content of the form in its order with its clip scopes (as the page; no records: the items of a form are
    // not selected in stage E1). The clips around the use apply to it through the form item (R6-7).
    const PDFContentDisplayContext displayContext = getDisplayContext();
    struct Entry
    {
        PDFContentNodePointer node;
        PDFAppEditContentClipPathsPointer savedClip;
        bool isExit = false;
    };

    std::vector<Entry> stack;
    for (auto it = form->getRoot()->getChildren().crbegin(); it != form->getRoot()->getChildren().crend(); ++it)
    {
        stack.push_back(Entry{ *it, nullptr, false });
    }

    PDFAppEditContentClipPathsPointer clip;
    qreal order = 0.0;
    while (!stack.empty())
    {
        Entry entry = std::move(stack.back());
        stack.pop_back();
        if (entry.isExit)
        {
            clip = std::move(entry.savedClip);
            continue;
        }

        const PDFContentNode& node = *entry.node;
        PDFAppEditContentItem* item = nullptr;
        if (!Helper::hasNoItem(node.getKind()))
        {
            item = createItem(*form, node, formItem);
            if (item)
            {
                item->setZValue(order);
                item->setClipPaths(clip);
                if (dynamic_cast<PDFAppEditContentFormItem*>(item))
                {
                    queue->push_back(FormBuild{ nestingDepth + 1, item->getSerial() });
                }
            }
            order += 1.0;
        }

        if (Helper::hasClip(node))
        {
            if (std::optional<QPainterPath> clipPath = Helper::getClipPath(node, *form, displayContext, item))
            {
                clip = Helper::appendClip(clip, std::move(*clipPath));
            }
        }

        if (Helper::hasChildItems(node.getKind()))
        {
            if (node.getKind() == PDFContentNodeKind::Group)
            {
                stack.push_back(Entry{ nullptr, clip, true });
            }
            for (auto it = node.getChildren().crbegin(); it != node.getChildren().crend(); ++it)
            {
                stack.push_back(Entry{ *it, nullptr, false });
            }
        }
    }

    // A hidden use hides its new content too (R6-8)
    formItem->updateChildrenVisibility();
}

void PDFAppEditContentScene::updateOptions()
{
    const PDFAppEditContentOptions* options = m_context ? m_context->getOptions() : nullptr;
    m_isClippingPathShown = options && options->getBool(QString::fromLatin1(PDFAppEditContentOptions::SHOW_CLIPPING_PATHS));
    m_isInvisibleTextShown = options && options->getBool(QString::fromLatin1(PDFAppEditContentOptions::SHOW_INVISIBLE_TEXT));
}

void PDFAppEditContentScene::updatePaper()
{
    QRectF paper;
    const PDFDocument* document = m_context ? m_context->getSession()->getDocument() : nullptr;
    const PDFInteger pageIndex = m_context ? m_context->getPageIndex() : -1;
    if (document && pageIndex >= 0 && pageIndex < PDFInteger(document->getCatalog()->getPageCount()))
    {
        if (const PDFPage* page = document->getCatalog()->getPage(pageIndex))
        {
            paper = page->getCropBox().normalized();
        }
    }

    m_paperRectangle = paper;
    static_cast<PDFAppEditContentPaperItem*>(m_paperItem)->setRectangle(paper);
    static_cast<PDFAppEditContentPreviewPictureItem*>(m_previewItem)->setRectangle(paper);
}

void PDFAppEditContentScene::updateComplexity()
{
    // The one policy of the complex page of the context (integration row 27: the notice of the tool GUI asks the same)
    const PDFContentContainerPointer container = getContainer(m_containerKey);
    m_isComplexPage = container && PDFAppEditContentContext::isComplexPage(PDFAppEditContentDisplayAccess::getCounts(*container));
}

void PDFAppEditContentScene::updateSceneRectangle()
{
    // The paper and the content outside of it - the content bounded to the paper grown by CONTENT_REACH page sizes
    // (R6-22); no margin: the view adds the margins, in which the page can be centered and the overlays do not cover it
    QRectF rectangle = m_paperRectangle;
    QRectF content = getContentRectangle();
    if (!rectangle.isNull() && !content.isNull())
    {
        const qreal reach = qMax(rectangle.width(), rectangle.height()) * CONTENT_REACH;
        content = content.intersected(rectangle.adjusted(-reach, -reach, reach, reach));
    }
    if (!content.isNull())
    {
        rectangle = rectangle.isNull() ? content : rectangle.united(content);
    }
    if (rectangle.isNull())
    {
        rectangle = QRectF(0.0, 0.0, 612.0, 792.0);
    }
    setSceneRect(rectangle);
}

void PDFAppEditContentScene::updateUnderlay()
{
    // zadani_gui.md 4.15: the annotations and fields of the page, dimmed under the content, while the option is on and
    // Preview is off; their picture is rendered in a worker once per page (the tool does not change them)
    if (!m_underlayItem || !m_context || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    const PDFAppEditContentOptions* options = m_context->getOptions();
    const bool isShown = options && options->getBool(QString::fromLatin1(PDFAppEditContentOptions::SHOW_ANNOTATIONS)) && !m_context->isPreviewActive();
    m_underlayItem->setVisible(isShown);
    const PDFInteger pageIndex = m_context->getPageIndex();
    if (!isShown || pageIndex < 0 || m_underlayPage == pageIndex)
    {
        return;
    }

    // Another page: the job of the old one is dropped, its picture too
    m_underlayPage = pageIndex;
    const std::pair<quint64, int> jobKey(0, int(PDFAppEditContentDecodeKind::Underlay));
    auto it = m_itemJobs.find(jobKey);
    if (it != m_itemJobs.end())
    {
        it->second->cancel();
        m_itemJobs.erase(it);
    }
    m_underlayItem->setPicture(QImage(), QRectF(), pageIndex);

    PDFDocumentPointer document = PDFAppEditContentDisplayAccess::getDocument(m_context.data());
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(pageIndex) : nullptr;
    if (!page || page->getAnnotations().empty())
    {
        return;
    }

    const QRectF cropBox = page->getCropBox().normalized();
    const qreal area = cropBox.width() * cropBox.height();
    if (!(area > 0.0) || !std::isfinite(area))
    {
        return;
    }
    const qreal scale = qMin(PDFAppEditContentSceneHelper::MAXIMUM_UNDERLAY_SCALE, std::sqrt(PDFAppEditContentSceneHelper::MAXIMUM_UNDERLAY_PIXELS / area));
    const QSize size(qMax(1, int(std::floor(cropBox.width() * scale))), qMax(1, int(std::floor(cropBox.height() * scale))));

    auto job = std::make_shared<PDFAppEditContentDecodeJob>();
    job->link = m_decodeLink;
    job->document = std::move(document);
    job->cms = m_cms;
    job->pageIndex = pageIndex;
    job->kind = PDFAppEditContentDecodeKind::Underlay;
    job->pageRectangle = cropBox;
    job->imageSize = size;
    m_itemJobs[jobKey] = job;
    startJob(job);
}

void PDFAppEditContentScene::onPageChanged()
{
    rebuild();
    updateUnderlay();
}

void PDFAppEditContentScene::onContainerChanged()
{
    // The container of the page was loaded, or the edited root changed (the dimming)
    synchronize(false);
}

void PDFAppEditContentScene::onContainerLoaded(const PDFContentContainerKey& key)
{
    if (key == m_containerKey)
    {
        // The page itself: the context tells it by containerChanged
        return;
    }

    if (key.kind == PDFContentContainerKey::Kind::Form)
    {
        requestMissingForms();
    }
}

void PDFAppEditContentScene::onModelChanged(const PDFContentChange& change)
{
    // A new version of the model: a new snapshot for the new objects of the next job
    m_snapshot.reset();
    updateNodes(change);
}

void PDFAppEditContentScene::onEditorStateChanged(const PDFContentNodeReference& node)
{
    // The lock or "hidden" of a node reaches its subtree: one walk for any number of changes in one turn of the event
    // loop (R6-2: Lock of 100 000 selected objects is 100 000 emissions, not 100 000 walks)
    if (node.container != m_containerKey || m_isEditorStateWalkQueued)
    {
        return;
    }
    m_isEditorStateWalkQueued = true;
    QMetaObject::invokeMethod(this, &PDFAppEditContentScene::onEditorStateWalk, Qt::QueuedConnection);
}

void PDFAppEditContentScene::onEditorStateWalk()
{
    // Another walk ran meanwhile (it read the state): nothing
    if (m_isEditorStateWalkQueued)
    {
        synchronize(false);
    }
}

void PDFAppEditContentScene::onOptionChanged(const QString& name)
{
    if (name == QLatin1String(PDFAppEditContentOptions::SHOW_CLIPPING_PATHS) || name == QLatin1String(PDFAppEditContentOptions::SHOW_INVISIBLE_TEXT))
    {
        updateOptions();
        update();
    }
    else if (name == QLatin1String(PDFAppEditContentOptions::SHOW_ANNOTATIONS))
    {
        updateUnderlay();
    }
}

void PDFAppEditContentScene::onSelectionChanged()
{
    m_selectionItem->updateGeometry();
}

void PDFAppEditContentScene::onThemeChanged()
{
    // A change of the theme: the cached paths and pictures of the items go, everything is painted again
    for (const auto& item : m_liveItems)
    {
        item.second->clearCache();
    }
    update();
}

void PDFAppEditContentScene::onPreviewChanged()
{
    // Preview shows the picture of the renderer instead of the items (zadani_gui.md 4.15)
    const bool isPreview = m_context && m_context->isPreviewActive();
    m_contentRoot->setVisible(!isPreview);
    m_previewItem->setVisible(isPreview);
    m_selectionItem->setVisible(!isPreview);
    m_toolPreviewItem->setVisible(!isPreview);
    for (QGraphicsItem* overlay : m_overlays)
    {
        overlay->setVisible(!isPreview);
    }
    updateUnderlay();
    m_previewItem->update();
}

void PDFAppEditContentScene::onSessionReplaced()
{
    // PDFAppEditContentContext::reloadDocument: the jobs of the old session are dropped (its container keys and node
    // identifiers may be equal in the new one), its pictures and snapshot go, the new session is connected; pageChanged
    // follows and builds the items again
    cancelJobs();
    clearPictures();
    m_snapshot.reset();
    m_underlayPage = -1;
    connectSession();
}

void PDFAppEditContentScene::updateEditorStates()
{
    synchronize(false);
}

}   // namespace pdf

#include "moc_pdfappeditcontentscene.cpp"
