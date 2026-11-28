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

#ifndef PDFAPPEDITCONTENTSCENE_H
#define PDFAPPEDITCONTENTSCENE_H

#include "pdfappglobal.h"
#include "pdfappeditcontentitems.h"
#include "pdfappeditcontentregistry.h"
#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdisplay.h"
#include "pdfcontenteditsession.h"
#include "pdfdeferreddeletion.h"

#include <QGraphicsScene>
#include <QPointer>
#include <QTransform>

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentDecodeJob;
class PDFAppEditContentDecodeLink;
class PDFAppEditContentFormItem;
class PDFAppEditContentItem;
class PDFAppEditContentSelectionItem;

// Edit Content, stage E1, package P11 (canvas); fix package FX2. Plan: zadani_gui.md 4.14 ("scene
// items and lifetime"), 4.15 (scene, layers, hit test), 4.24 (performance); zadani.md 9.3.

/// The scene of the canvas: one page. The coordinates of the scene are the default user space of
/// the page (the origin at the bottom left, y up; the view flips and rotates). The paper is the
/// crop box; content outside of it is drawn on the gray area around (not clipped), so that it can
/// be selected.
///
/// - The items of the content are built from the edited page container by the item entries of
///   the registry, in the order of the content stream; a form use draws the container of its form
///   (when the session has it loaded) under its matrix - every use its own items, all forms of the
///   page together under one budget of items (FORM_ITEM_BUDGET, R6-1; a use beyond it keeps the
///   outline of its box), built breadth first, so the outer levels come first.
/// - **An item is never deleted inside of event processing.** A removed or replaced item is
///   hidden and moved to a list, which a queued slot of the scene empties (deleteRetiredItems);
///   the destructor of the scene deletes the rest.
/// - Tools, the selection and the cards hold node identifiers; findItem is the way to an item,
///   at the moment of use.
/// - The permanent items: the paper, the selection item, the item of the tool preview (it calls
///   PDFAppEditContentTool::paint of the active tool, it covers getPreviewRectangle of the tool),
///   the overlays of the registry (the annotation underlay, grid, guides, markers). They are
///   created with the scene and never replaced.
/// - Items accept no events; the scene does not use the selection or the focus of
///   QGraphicsScene (stage E2 adds the one text edit item with the keyboard focus).
/// - The scene follows the context and the session itself: pageChanged / containerChanged,
///   containerLoaded, modelChanged (only the nodes of the change are rebuilt), editorStateChanged
///   (one walk per turn of the event loop for any number of changes, R6-2), sessionReplaced (the
///   new session of PDFAppEditContentContext::reloadDocument), the selection, the options of the
///   display, the theme (PDFDeferredDeletion::connect; a change of the theme or of the device pixel
///   ratio drops the caches of the items).
/// - Work in workers (PDFExecutionPolicy::startTask on the global pool, at most
///   MAXIMUM_RUNNING_JOBS of the scene at once, the others wait in the scene): the pictures of the
///   images (one per image object and size step, shared by its uses, within a budget of pixels -
///   R6-5), the shadings, the patterns of the paths (per item and zoom step), the picture of the
///   annotations. A result of a canceled, replaced or retired request is dropped.
/// - The scene rectangle is the paper and the content (bounded to the paper grown by
///   CONTENT_REACH page sizes, R6-22), without a margin: the view adds the margins of the overlays.
///
/// Threads and ownership: a QObject, a child of the view (PDFAppEditContentView) - a part: it
/// dies with the canvas, possibly before the context. GUI thread. It owns its items. The context
/// is held by a QPointer and checked. The hook retire() stops the queued work and emits nothing;
/// the items are deleted by the destructor.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentScene : public QGraphicsScene, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QGraphicsScene;

public:
    /// The z value of a layer is its number times this step
    static constexpr qreal LAYER_STEP = 1000000.0;

    explicit PDFAppEditContentScene(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentScene() override;

    /// The z value of the bottom of a layer
    static qreal getLayerZ(PDFAppEditContentSceneLayer layer);

    /// Builds the items of the edited page of the context again (the page, the container or the
    /// isolation path changed); the old items are retired
    void rebuild();

    /// Rebuilds only the items of the nodes of the change (zadani.md 9.3 updateNodes)
    void updateNodes(const PDFContentChange& change);

    /// The item of the node, nullptr = none (the node is not on the page, is not built yet, or
    /// its item was retired). Never keep the pointer over a return to the event loop.
    PDFAppEditContentItem* findItem(const PDFContentNodeReference& node) const;

    /// The node of the edited root under the point, the topmost first (zadani_gui.md 4.15 "hit":
    /// by shape; \p tolerance is 6 px of the screen in the units of the scene). Locked nodes,
    /// opaque nodes and nodes outside of the edited root are not returned, unless \p isLockedIncluded
    /// (the tool Select asks so, to show the card Locked object). A child of a group gives the
    /// child of the edited root, which contains it. Cost: the items under the point (the index of
    /// the scene) times the walk up to the child of the edited root; the result is deduplicated by
    /// a set (R6-4).
    std::vector<PDFContentNodeReference> getNodesAt(const QPointF& scenePoint, qreal tolerance, bool isLockedIncluded) const;

    /// The nodes of the edited root, whose bounding boxes lie inside of the rectangle (the
    /// selection by a rectangle; locked and opaque nodes are left out)
    std::vector<PDFContentNodeReference> getNodesIn(const QRectF& sceneRectangle) const;

    /// The rectangle of the paper (the crop box of the page) and of the whole content in the
    /// coordinates of the scene; empty before a page is shown
    QRectF getPaperRectangle() const;
    QRectF getContentRectangle() const;

    /// The item of the selection (never nullptr while the scene lives)
    PDFAppEditContentSelectionItem* getSelectionItem() const;

    /// The preview of the active tool is painted again: only the rectangle it covered and the one
    /// it covers now (PDFAppEditContentTool::getPreviewRectangle, R6-11)
    void updateToolPreview();

    /// Items retired and not deleted yet (tests: "an item is not deleted inside of an event")
    int getRetiredItemCount() const;

    // ---- P11 (the items, the tools and the view of the canvas; no other package calls these) ----

    /// Limits of the complex page (zadani.md 12.1, QE.10): above them a drag shows the common
    /// outline instead of moving the items (PDFAppEditContentContext::isComplexPage reads them)
    static constexpr size_t COMPLEX_PAGE_OBJECTS = 20000;
    static constexpr size_t COMPLEX_PAGE_PATH_SEGMENTS = 500000;

    /// A drag of more items than this shows the common outline (zadani_gui.md 4.24)
    static constexpr int GESTURE_ITEM_LIMIT = 500;

    /// Most items of the contents of all form uses of the page together (R6-1)
    static constexpr size_t FORM_ITEM_BUDGET = COMPLEX_PAGE_OBJECTS;

    /// Most pixels of the decoded pictures of the images of the scene together (R6-5: 256 MB);
    /// a picture over it is not kept (the placeholder, or the smaller picture it had) - nothing
    /// is evicted, so visible pictures are never decoded again and again; another page or
    /// session drops them all
    static constexpr qint64 MAXIMUM_PICTURE_PIXELS = 64000000;

    /// Most jobs of the scene running in the pool at once (the others wait in the scene, so the
    /// pool is not filled by one page: R6-5)
    static constexpr int MAXIMUM_RUNNING_JOBS = 2;

    /// The content is bounded to the paper grown by this many sizes of the page on every side
    /// (R6-22: a coordinate of 10^12 does not make the scroll bars useless)
    static constexpr qreal CONTENT_REACH = 2.0;

    PDFAppEditContentContext* getContext() const { return m_context.data(); }

    /// What the items give to the display queries of the core (the document, the color management)
    PDFContentDisplayContext getDisplayContext() const;

    /// The current version of a container of the session, nullptr = not loaded
    PDFContentContainerPointer getContainer(const PDFContentContainerKey& key) const;

    /// The options of the display (cached from PDFAppEditContentOptions)
    bool isClippingPathShown() const { return m_isClippingPathShown; }
    bool isInvisibleTextShown() const { return m_isInvisibleTextShown; }

    /// The edited page has more objects or path segments than the limits of the complex page
    bool isComplexPage() const { return m_isComplexPage; }

    /// Number of the items of the content (tests)
    int getContentItemCount() const;

    /// An image, a shading or a path with a pattern asks for its result (\p pixels: the budget of
    /// the picture, a size step of PDFAppEditContentDisplayAccess::getPixelStep): a job in a
    /// worker, the result comes back to the item by its serial and kind (a retired item is
    /// forgotten; a new request of the same item and kind replaces the old one). The images ask by
    /// getPicture instead.
    void requestDecoding(PDFAppEditContentItem* item, PDFAppEditContentDecodeKind kind, qint64 pixels);

    /// The picture of the image of \p item for \p pixels of the device (R6-5, R6-14): the largest
    /// picture the scene has for its key (any size step; nullptr = none yet). A larger size step
    /// than the one it has is decoded in a worker (one job per key), unless the picture is the
    /// whole image already or over the budget. \p isFailed: the image cannot be decoded.
    const QImage* getPicture(const PDFAppEditContentImageItem* item, qreal pixels, bool* isFailed);

    /// The scene has a picture of the key
    bool hasPicture(const PDFAppEditContentPictureKey& key) const;

    /// Number of the jobs waiting or running (tests)
    int getDecodingCount() const;

    /// Number of the jobs started since the scene was created (tests: one decoding per image object)
    int getStartedJobCount() const { return m_startedJobCount; }

    /// The pixels of the pictures kept, the budget (tests may lower it)
    qint64 getPicturePixels() const { return m_picturePixels; }
    void setPictureBudget(qint64 pixels);

    /// The items of the contents of the forms kept now, the budget (tests may lower it; the forms
    /// are built again)
    size_t getFormItemCount() const { return m_formItemCount; }
    void setFormItemBudget(size_t budget);

    /// Number of the walks over the page container (tests: R6-2)
    int getWalkCount() const { return m_walkCount; }

    /// The overlay of the annotations (nullptr without its entry in the registry)
    PDFAppEditContentUnderlayItem* getUnderlayItem() const { return m_underlayItem; }

    /// The children of the edited root, which can be selected, in the order of the content (Tab)
    std::vector<PDFContentNodeReference> getSelectableNodes() const;

    /// The child of the edited root, which contains the node (itself for a child); not valid for a
    /// node outside of the edited root
    PDFContentNodeReference getEditedRootChild(const PDFContentNodeReference& node) const;

    /// The live move of a gesture of a tool (a drag of Select): the items of the nodes (with their
    /// subtrees) are drawn moved by \p transform (in the default space of the page); for more than
    /// GESTURE_ITEM_LIMIT items or a complex page only the selection item shows the moved outline
    /// (isGestureOutlineOnly). clearGestureTransform ends it.
    void setGestureTransform(const std::vector<PDFContentNodeReference>& nodes, const QTransform& transform);
    void clearGestureTransform();
    const QTransform& getGestureTransform() const { return m_gestureTransform; }
    bool isGestureActive() const { return m_isGestureActive; }
    bool isGestureOutlineOnly() const { return m_isGestureOutlineOnly; }

    /// The size of a pixel of the view in the units of the scene (1 without a view)
    qreal getPixelSize() const;

protected:
    virtual void retire() override;

private:
    friend class PDFAppEditContentDecodeLink;

    /// The record of a node of the edited page container
    struct Record
    {
        std::weak_ptr<const PDFContentNode> node;   ///< Identity only (owner comparison), never locked to read
        PDFAppEditContentItem* item = nullptr;      ///< nullptr: the node has no item (a state run, a text show, a marked point)
        PDFContentNodeId parentId;
        PDFAppEditContentClipPathsPointer clipBefore;   ///< A clipping node: the chain it was given at the last walk
        PDFAppEditContentClipPathsPointer clipAfter;    ///< ... and the chain it made (kept while both stay, R6-12)
        bool hasOverride = false;
        bool isVisited = false;
    };

    /// A decoded picture of an image (R6-5)
    struct Picture
    {
        QImage image;                       ///< The largest picture decoded (null = none yet)
        qint64 step = 0;                    ///< Its size step
        std::set<quint64> serials;          ///< The items, which painted it (repainted at a new picture)
        bool isFailed = false;              ///< The image cannot be decoded
        bool isComplete = false;            ///< The picture is the whole image: no larger one exists
        bool isOverBudget = false;          ///< A larger picture did not fit the budget (asked no more)
    };

    /// A form item waiting for its content (the breadth-first build of R6-1)
    struct FormBuild
    {
        int depth = 0;
        quint64 serial = 0;

        /// The order of the build: the outer levels first
        bool operator<(const FormBuild& other) const { return depth < other.depth; }
    };

    /// Deletes the retired items (a queued slot; never called from an event of an item)
    void deleteRetiredItems();

    /// Reconciles the items of the page container with its version (rebuild: everything new)
    void synchronize(bool isFullRebuild);

    /// Builds the items of a form container as the children of the form item (fresh); the form
    /// items of its content are added to \p queue (the next level)
    void buildForm(PDFAppEditContentFormItem* formItem, int nestingDepth, std::vector<FormBuild>* queue);

    /// Creates the item of a node by the registry (nullptr = no entry draws it)
    PDFAppEditContentItem* createItem(const PDFContentContainer& container, const PDFContentNode& node, QGraphicsItem* parentItem);

    /// Retires an item: hidden, forgotten (with its descendants), deleted from the event loop
    void retireItem(PDFAppEditContentItem* item);
    void forgetItemTree(QGraphicsItem* item);

    /// The editor state (lock, hidden) and the dimming of the items of the page container
    void updateEditorStates();

    void updateOptions();
    void updatePaper();
    void updateComplexity();
    void updateSceneRectangle();
    void updateUnderlay();
    void requestMissingForms();

    /// The jobs: one more is started, or it waits (MAXIMUM_RUNNING_JOBS)
    void startJob(const std::shared_ptr<PDFAppEditContentDecodeJob>& job);
    void startWaitingJobs();
    void cancelJobs();

    /// The snapshot of the session for the new objects of a job (made once per version of the model)
    PDFContentEditSnapshotPointer getSnapshot();

    /// The pictures of the images are dropped (another page, another session)
    void clearPictures();

    /// The connections to the session of the context (the constructor, a new session)
    void connectSession();

    void onPageChanged();
    void onContainerChanged();
    void onContainerLoaded(const PDFContentContainerKey& key);
    void onModelChanged(const PDFContentChange& change);
    void onEditorStateChanged(const PDFContentNodeReference& node);
    void onEditorStateWalk();
    void onOptionChanged(const QString& name);
    void onSelectionChanged();
    void onThemeChanged();
    void onPreviewChanged();
    void onSessionReplaced();
    void onDecodeFinished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job);

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<PDFContentEditSession> m_session;      ///< The session the scene is connected to
    PDFCMSPointer m_cms;                            ///< The color management of the application (held: the display context points to it)
    QGraphicsItem* m_paperItem = nullptr;
    QGraphicsItem* m_contentRoot = nullptr;
    QGraphicsItem* m_previewItem = nullptr;
    PDFAppEditContentSelectionItem* m_selectionItem = nullptr;
    QGraphicsItem* m_toolPreviewItem = nullptr;
    PDFAppEditContentUnderlayItem* m_underlayItem = nullptr;
    std::vector<QGraphicsItem*> m_overlays;
    PDFContentContainerKey m_containerKey;          ///< The page container of the items
    quint64 m_containerVersion = 0;                 ///< Its version the items show (0 = none)
    std::map<PDFContentNodeId, Record> m_records;
    std::vector<QGraphicsItem*> m_retiredItems;
    std::map<quint64, PDFAppEditContentItem*> m_liveItems;      ///< Serial -> item (every item of the content, forms included)
    std::set<quint64> m_formItems;                              ///< The serials of the live form items
    size_t m_formItemCount = 0;                                 ///< The live items inside of forms
    size_t m_formItemBudget = FORM_ITEM_BUDGET;
    std::map<std::pair<quint64, int>, std::shared_ptr<PDFAppEditContentDecodeJob>> m_itemJobs;       ///< (serial, kind) -> its job (shadings, patterns, the underlay with serial 0)
    std::map<PDFAppEditContentPictureKey, std::shared_ptr<PDFAppEditContentDecodeJob>> m_pictureJobs; ///< The decoding of a picture of an image
    std::deque<std::shared_ptr<PDFAppEditContentDecodeJob>> m_waitingJobs;
    std::map<PDFAppEditContentPictureKey, Picture> m_pictures;
    qint64 m_picturePixels = 0;
    qint64 m_pictureBudget = MAXIMUM_PICTURE_PIXELS;
    int m_runningJobCount = 0;
    int m_startedJobCount = 0;
    int m_walkCount = 0;
    PDFInteger m_underlayPage = -1;                 ///< The page the picture of the annotations was asked for
    PDFContentEditSnapshotPointer m_snapshot;
    std::shared_ptr<PDFAppEditContentDecodeLink> m_decodeLink;
    QRectF m_paperRectangle;
    std::vector<PDFContentNodeReference> m_gestureNodes;
    QTransform m_gestureTransform;
    std::vector<PDFAppEditContentItem*> m_gestureItems;
    bool m_isGestureActive = false;
    bool m_isGestureOutlineOnly = false;
    bool m_isClippingPathShown = false;
    bool m_isInvisibleTextShown = false;
    bool m_isComplexPage = false;
    bool m_isDeleteQueued = false;
    bool m_isEditorStateWalkQueued = false;
    bool m_isMissingItemReported = false;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTSCENE_H
