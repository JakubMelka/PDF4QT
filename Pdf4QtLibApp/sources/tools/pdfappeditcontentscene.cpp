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

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentitems.h"
#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentview.h"
#include "pdfcatalog.h"
#include "pdfexecutionpolicy.h"
#include "pdfoperationcontrol.h"
#include "pdfpainterutils.h"
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

#include <atomic>
#include <cmath>
#include <set>

namespace pdf
{

// Stage E1, package P11: the scene of the canvas. Bounds (hostile input): the walk over the content is iterative and
// visits every node of the page container once (the container has at most PDFContentContainer::MAXIMUM_OPERATION_COUNT
// nodes); the forms are built not deeper than PDFContentEditSession::MAXIMUM_CONTAINER_NESTING with the keys of the path
// as the visited set (a form drawing itself is drawn once); a walk up to the child of the edited root is bounded by
// MAXIMUM_PARENT_STEPS; at most one decoding job per image item and version runs.

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

/// One decoding of an image or a shading in the pool: plain values written before the start and read after the end
/// (the hand-offs are annotated), except of the cancellation
class PDFAppEditContentDecodeJob : public PDFOperationControl
{
public:
    std::shared_ptr<PDFAppEditContentDecodeLink> link;
    PDFDocumentPointer document;            ///< Held by the job (the document must outlive the decoding)
    PDFContentContainerPointer container;   ///< Immutable
    PDFCMSPointer cms;                      ///< Held by the job; nullptr = the generic one
    PDFContentNodeId node;
    PDFInteger pageIndex = -1;
    quint64 serial = 0;                     ///< The item, which waits for the result
    bool isShading = false;
    PDFContentDisplayImage image;           ///< The result of an image
    PDFContentDisplayShading shading;       ///< The result of a shading

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
        if (isShading)
        {
            shading = PDFContentDisplay::getShading(document.data(), container, node, false, options, this);
        }
        else
        {
            image = PDFContentDisplay::decodeImage(document.data(), container, node, options, this);
        }
    }

private:
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
        m_job->run();
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

        // The picture covers the rectangle of its request (the page space); its pixels by the matrix of the request
        const PDFAppEditContentPreviewRequest& request = preview->getImageRequest();
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

/// The one item, which paints the preview of the active tool (layer ToolPreview)
class PDFAppEditContentToolPreviewItem : public QGraphicsItem
{
public:
    explicit PDFAppEditContentToolPreviewItem(PDFAppEditContentContext* context) :
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
                return false;
        }
    }

    /// The clip of a clipping node in the coordinates of its item (the default space of the container), nullopt =
    /// the node does not clip
    static std::optional<QPainterPath> getClipPath(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
    {
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

    /// The clip paths with one more path (a new shared list)
    static PDFAppEditContentClipPathsPointer appendClip(const PDFAppEditContentClipPathsPointer& clips, QPainterPath path)
    {
        auto result = std::make_shared<PDFAppEditContentClipPaths>(clips ? *clips : PDFAppEditContentClipPaths());
        result->push_back(std::move(path));
        return result;
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
        }
    }

    m_cms = PDFAppEditContentDisplayAccess::getColorManagement(context);

    // The context, its session, its options, its selection and its controller live longer than the scene (a part)
    PDFContentEditSession* session = context->getSession();
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentScene::onPageChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::containerChanged, this, &PDFAppEditContentScene::onContainerChanged);
    PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentScene::onPreviewChanged);
    PDFDeferredDeletion::connect(session, &PDFContentEditSession::containerLoaded, this, &PDFAppEditContentScene::onContainerLoaded);
    PDFDeferredDeletion::connect(session, &PDFContentEditSession::modelChanged, this, &PDFAppEditContentScene::onModelChanged);
    PDFDeferredDeletion::connect(session, &PDFContentEditSession::editorStateChanged, this, &PDFAppEditContentScene::onEditorStateChanged);
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
    for (auto& job : m_decodingItems)
    {
        job.second->cancel();
    }
    m_decodingItems.clear();
    m_liveItems.clear();
    m_records.clear();
    m_retiredItems.clear();
    m_gestureItems.clear();
}

qreal PDFAppEditContentScene::getLayerZ(PDFAppEditContentSceneLayer layer)
{
    return qreal(int(layer)) * LAYER_STEP;
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
    for (const PDFContentContainerKey& key : forms)
    {
        onContainerLoaded(key);
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
    // edited root, which contains it
    const QRectF area(scenePoint.x() - tolerance, scenePoint.y() - tolerance, 2.0 * tolerance, 2.0 * tolerance);
    const QList<QGraphicsItem*> candidates = items(area, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder);
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
        const PDFContentNodePointer childNode = child.isValid() ? container->getNode(child.node) : PDFContentNodePointer();
        if (!childNode || !PDFAppEditContentSceneHelper::isSelectableKind(childNode->getKind()))
        {
            continue;
        }
        if (!isLockedIncluded && m_context->getSession()->isNodeLocked(child))
        {
            continue;
        }
        if (std::find(result.cbegin(), result.cend(), child) == result.cend())
        {
            result.push_back(child);
        }
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
    m_toolPreviewItem->update();
}

int PDFAppEditContentScene::getRetiredItemCount() const
{
    return int(m_retiredItems.size());
}

int PDFAppEditContentScene::getContentItemCount() const
{
    return int(m_liveItems.size());
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

void PDFAppEditContentScene::requestDecoding(PDFAppEditContentItem* item)
{
    if (!item || !m_context || PDFDeferredDeletion::isRetired(this) || m_liveItems.find(item->getSerial()) == m_liveItems.cend())
    {
        return;
    }

    PDFAppEditContentImageItem* imageItem = dynamic_cast<PDFAppEditContentImageItem*>(item);
    PDFAppEditContentShadingItem* shadingItem = dynamic_cast<PDFAppEditContentShadingItem*>(item);
    const PDFContentContainerKey key = imageItem ? imageItem->getContainerKey() : (shadingItem ? shadingItem->getContainerKey() : PDFContentContainerKey());
    PDFDocumentPointer document = PDFAppEditContentDisplayAccess::getDocument(m_context.data());
    PDFContentContainerPointer container = getContainer(key);
    if (!document || !container)
    {
        // No shared document (no GUI): nothing can hold the document in a worker - the placeholder stays
        if (imageItem)
        {
            PDFContentDisplayImage failed;
            failed.status = PDFContentDisplayImage::Status::Failed;
            imageItem->setDecodedImage(failed);
        }
        return;
    }

    // A job of the older version of the item is canceled (its result is dropped by the identity of the job)
    auto it = m_decodingItems.find(item->getSerial());
    if (it != m_decodingItems.end())
    {
        it->second->cancel();
        m_decodingItems.erase(it);
    }

    auto job = std::make_shared<PDFAppEditContentDecodeJob>();
    job->link = m_decodeLink;
    job->document = std::move(document);
    job->container = std::move(container);
    job->cms = PDFAppEditContentDisplayAccess::getColorManagement(m_context.data());
    job->node = item->getNodeId();
    job->pageIndex = m_context->getPageIndex();
    job->serial = item->getSerial();
    job->isShading = shadingItem != nullptr;
    m_decodingItems[item->getSerial()] = job;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppEditContentDecodeTask(std::move(job)));
}

void PDFAppEditContentScene::onDecodeFinished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A job, which is not the current one of its item (canceled, an older version, a retired item) is dropped
    const auto it = m_decodingItems.find(job->serial);
    if (it == m_decodingItems.end() || it->second != job || job->isOperationCanceled())
    {
        return;
    }
    m_decodingItems.erase(it);

    const auto itemIt = m_liveItems.find(job->serial);
    if (itemIt == m_liveItems.end())
    {
        return;
    }

    if (PDFAppEditContentImageItem* imageItem = dynamic_cast<PDFAppEditContentImageItem*>(itemIt->second))
    {
        imageItem->setDecodedImage(job->image);
    }
    else if (PDFAppEditContentShadingItem* shadingItem = dynamic_cast<PDFAppEditContentShadingItem*>(itemIt->second))
    {
        shadingItem->setShading(job->shading);
    }
}

void PDFAppEditContentScene::retire()
{
    // No late result, no queued deletion runs any more (the items are deleted by the destructor); emits nothing
    m_decodeLink->detach();
    for (auto& job : m_decodingItems)
    {
        job.second->cancel();
    }
    m_decodingItems.clear();
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
    return item;
}

void PDFAppEditContentScene::forgetItemTree(QGraphicsItem* root)
{
    // The item and its descendants (the items of a form, bounded by the nesting of the forms)
    std::vector<QGraphicsItem*> stack{ root };
    while (!stack.empty())
    {
        QGraphicsItem* current = stack.back();
        stack.pop_back();
        if (PDFAppEditContentItem* item = qgraphicsitem_cast<PDFAppEditContentItem*>(current))
        {
            m_liveItems.erase(item->getSerial());
            auto job = m_decodingItems.find(item->getSerial());
            if (job != m_decodingItems.end())
            {
                job->second->cancel();
                m_decodingItems.erase(job);
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

    PDFContentEditSession* session = m_context->getSession();
    const PDFContentContainerKey pageKey = m_context->getPageKey();
    const PDFContentContainerPointer container = getContainer(pageKey);

    // Another page (or a rebuild): every item goes
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
            clip = entry.savedClip;
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
            if (item && (!isSame || entry.isRefreshed) && !item->updateNode(node, *container))
            {
                retireItem(item);
                item = nullptr;
            }
            if (!item)
            {
                item = createItem(*container, node, m_contentRoot);
            }
            record.item = item;
            if (item)
            {
                item->setZValue(order);
                item->setClipPaths(clip);
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

        // The clip of a path applies after its painting, to the content behind it in its scope (W, W*)
        if (std::optional<QPainterPath> clipPath = Helper::getClipPath(node, *container, displayContext))
        {
            clip = Helper::appendClip(clip, std::move(*clipPath));
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
    // forms of a page with it)
    std::vector<PDFAppEditContentFormItem*> formItems;
    for (const auto& item : m_liveItems)
    {
        if (PDFAppEditContentFormItem* formItem = dynamic_cast<PDFAppEditContentFormItem*>(item.second))
        {
            formItems.push_back(formItem);
        }
    }

    for (PDFAppEditContentFormItem* formItem : formItems)
    {
        if (m_liveItems.find(formItem->getSerial()) == m_liveItems.end())
        {
            // Retired meanwhile (a form of a form built again)
            continue;
        }

        const PDFContentContainerPointer form = getContainer(formItem->getFormKey());
        if (!form || form->getVersion() == formItem->getFormVersion())
        {
            continue;
        }

        // The keys of the forms around the item (the visited set of the path) and its depth
        std::vector<PDFContentContainerKey> path{ m_containerKey };
        int depth = 1;
        for (QGraphicsItem* parent = formItem->parentItem(); parent && parent != m_contentRoot && depth <= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING + 1; parent = parent->parentItem())
        {
            if (const PDFAppEditContentFormItem* outer = dynamic_cast<const PDFAppEditContentFormItem*>(parent))
            {
                path.push_back(outer->getFormKey());
                ++depth;
            }
        }
        buildForm(formItem, depth, std::move(path));
    }
}

void PDFAppEditContentScene::buildForm(PDFAppEditContentFormItem* formItem, int nestingDepth, std::vector<PDFContentContainerKey> path)
{
    using Helper = PDFAppEditContentSceneHelper;

    const PDFContentContainerKey formKey = formItem->getFormKey();
    const PDFContentContainerPointer form = getContainer(formKey);
    if (!form || !form->getRoot())
    {
        return;
    }

    // A form nested too deep or drawing itself (a cycle through the path) keeps the outline of its box
    if (nestingDepth > PDFContentEditSession::MAXIMUM_CONTAINER_NESTING || std::find(path.cbegin(), path.cend(), formKey) != path.cend())
    {
        return;
    }
    path.push_back(formKey);

    // The old children (another version of the form) go
    const QList<QGraphicsItem*> oldChildren = formItem->childItems();
    for (QGraphicsItem* child : oldChildren)
    {
        if (PDFAppEditContentItem* item = qgraphicsitem_cast<PDFAppEditContentItem*>(child))
        {
            retireItem(item);
        }
    }
    formItem->setForm(*form);

    // The content of the form in its order with its clip scopes (as the page; no records: the items of a form are
    // not selected in stage E1)
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

    std::vector<PDFAppEditContentFormItem*> nestedForms;
    PDFAppEditContentClipPathsPointer clip;
    qreal order = 0.0;
    while (!stack.empty())
    {
        Entry entry = std::move(stack.back());
        stack.pop_back();
        if (entry.isExit)
        {
            clip = entry.savedClip;
            continue;
        }

        const PDFContentNode& node = *entry.node;
        if (!Helper::hasNoItem(node.getKind()))
        {
            if (PDFAppEditContentItem* item = createItem(*form, node, formItem))
            {
                item->setZValue(order);
                item->setClipPaths(clip);
                if (PDFAppEditContentFormItem* nested = dynamic_cast<PDFAppEditContentFormItem*>(item))
                {
                    nestedForms.push_back(nested);
                }
            }
            order += 1.0;
        }

        if (std::optional<QPainterPath> clipPath = Helper::getClipPath(node, *form, displayContext))
        {
            clip = Helper::appendClip(clip, std::move(*clipPath));
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

    for (PDFAppEditContentFormItem* nested : nestedForms)
    {
        buildForm(nested, nestingDepth + 1, path);
    }
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
    const PDFContentContainerPointer container = getContainer(m_containerKey);
    if (!container)
    {
        m_isComplexPage = false;
        return;
    }

    const PDFContentDisplayCounts counts = PDFAppEditContentDisplayAccess::getCounts(*container);
    m_isComplexPage = counts.nodeCount > COMPLEX_PAGE_OBJECTS || counts.pathSegmentCount > COMPLEX_PAGE_PATH_SEGMENTS;
}

void PDFAppEditContentScene::updateSceneRectangle()
{
    // The paper and the content outside of it, with a margin of the half of the larger side (so the page can be moved
    // to the middle of the view at any zoom and the overlays do not cover its edge)
    QRectF rectangle = m_paperRectangle;
    const QRectF content = getContentRectangle();
    if (!content.isNull())
    {
        rectangle = rectangle.isNull() ? content : rectangle.united(content);
    }
    if (rectangle.isNull())
    {
        rectangle = QRectF(0.0, 0.0, 612.0, 792.0);
    }

    const qreal margin = qMax(rectangle.width(), rectangle.height()) * 0.5 + 36.0;
    const QRectF sceneRectangle = rectangle.adjusted(-margin, -margin, margin, margin);
    setSceneRect(sceneRectangle);
    static_cast<PDFAppEditContentToolPreviewItem*>(m_toolPreviewItem)->setRectangle(sceneRectangle);
}

void PDFAppEditContentScene::onPageChanged()
{
    rebuild();
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
    updateNodes(change);
}

void PDFAppEditContentScene::onEditorStateChanged(const PDFContentNodeReference& node)
{
    // The lock or "hidden" of a node reaches its subtree: one walk (no new items)
    if (node.container == m_containerKey)
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
    m_previewItem->update();
}

void PDFAppEditContentScene::updateEditorStates()
{
    synchronize(false);
}

}   // namespace pdf

#include "moc_pdfappeditcontentscene.cpp"
