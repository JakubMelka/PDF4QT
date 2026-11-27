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
#include "pdfappeditcontentregistry.h"
#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdisplay.h"
#include "pdfdeferreddeletion.h"

#include <QGraphicsScene>
#include <QPointer>
#include <QTransform>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentDecodeJob;
class PDFAppEditContentDecodeLink;
class PDFAppEditContentFormItem;
class PDFAppEditContentItem;
class PDFAppEditContentSelectionItem;

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.14 ("scene items and
// lifetime"), 4.15 (scene, layers, hit test), 4.24 (performance); zadani.md 9.3.

/// The scene of the canvas: one page. The coordinates of the scene are the default user space of
/// the page (the origin at the bottom left, y up; the view flips and rotates). The paper is the
/// crop box; content outside of it is drawn on the gray area around (not clipped), so that it can
/// be selected.
///
/// - The items of the content are built from the edited page container by the item entries of
///   the registry, in the order of the content stream; a form use draws the container of its form
///   (when the session has it loaded) under its matrix.
/// - **An item is never deleted inside of event processing.** A removed or replaced item is
///   hidden and moved to a list, which a queued slot of the scene empties (deleteRetiredItems);
///   the destructor of the scene deletes the rest.
/// - Tools, the selection and the cards hold node identifiers; findItem is the way to an item,
///   at the moment of use.
/// - The permanent items: the paper, the selection item, the item of the tool preview (it calls
///   PDFAppEditContentTool::paint of the active tool), the overlays of the registry (grid, guides,
///   markers). They are created with the scene and never replaced.
/// - Items accept no events; the scene does not use the selection or the focus of
///   QGraphicsScene (stage E2 adds the one text edit item with the keyboard focus).
/// - The scene follows the context and the session itself: pageChanged / containerChanged,
///   containerLoaded, modelChanged (only the nodes of the change are rebuilt), editorStateChanged,
///   the selection, the options of the display, the theme (PDFDeferredDeletion::connect; a change
///   of the theme or of the device pixel ratio drops the caches of the items).
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
    /// child of the edited root, which contains it.
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

    /// The preview of the active tool is painted again
    void updateToolPreview();

    /// Items retired and not deleted yet (tests: "an item is not deleted inside of an event")
    int getRetiredItemCount() const;

    // ---- P11 (the items, the tools and the view of the canvas; no other package calls these) ----

    /// Limits of the complex page (zadani.md 12.1, QE.10): above them a drag shows the common
    /// outline instead of moving the items
    static constexpr size_t COMPLEX_PAGE_OBJECTS = 20000;
    static constexpr size_t COMPLEX_PAGE_PATH_SEGMENTS = 500000;

    /// A drag of more items than this shows the common outline (zadani_gui.md 4.24)
    static constexpr int GESTURE_ITEM_LIMIT = 500;

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

    /// An image or a shading item asks for its picture (its first paint): a job in a worker, the
    /// result comes back to the item by its serial (a retired item is forgotten)
    void requestDecoding(PDFAppEditContentItem* item);

    /// Number of the decodings in progress (tests)
    int getDecodingCount() const { return int(m_decodingItems.size()); }

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
        bool hasOverride = false;
        bool isVisited = false;
    };

    /// Deletes the retired items (a queued slot; never called from an event of an item)
    void deleteRetiredItems();

    /// Reconciles the items of the page container with its version (rebuild: everything new)
    void synchronize(bool isFullRebuild);

    /// Builds the items of a form container as the children of the form item (fresh)
    void buildForm(PDFAppEditContentFormItem* formItem, int nestingDepth, std::vector<PDFContentContainerKey> path);

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
    void requestMissingForms();

    void onPageChanged();
    void onContainerChanged();
    void onContainerLoaded(const PDFContentContainerKey& key);
    void onModelChanged(const PDFContentChange& change);
    void onEditorStateChanged(const PDFContentNodeReference& node);
    void onOptionChanged(const QString& name);
    void onSelectionChanged();
    void onThemeChanged();
    void onPreviewChanged();
    void onDecodeFinished(const std::shared_ptr<PDFAppEditContentDecodeJob>& job);

    QPointer<PDFAppEditContentContext> m_context;
    PDFCMSPointer m_cms;                            ///< The color management of the application (held: the display context points to it)
    QGraphicsItem* m_paperItem = nullptr;
    QGraphicsItem* m_contentRoot = nullptr;
    QGraphicsItem* m_previewItem = nullptr;
    PDFAppEditContentSelectionItem* m_selectionItem = nullptr;
    QGraphicsItem* m_toolPreviewItem = nullptr;
    std::vector<QGraphicsItem*> m_overlays;
    PDFContentContainerKey m_containerKey;          ///< The page container of the items
    quint64 m_containerVersion = 0;                 ///< Its version the items show (0 = none)
    std::map<PDFContentNodeId, Record> m_records;
    std::vector<QGraphicsItem*> m_retiredItems;
    std::map<quint64, PDFAppEditContentItem*> m_liveItems;      ///< Serial -> item (every item of the content, forms included)
    std::map<quint64, std::shared_ptr<PDFAppEditContentDecodeJob>> m_decodingItems;     ///< Serial -> its job
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
    bool m_isMissingItemReported = false;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTSCENE_H
