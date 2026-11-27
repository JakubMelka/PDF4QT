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

#ifndef PDFAPPEDITCONTENTSELECTION_H
#define PDFAPPEDITCONTENTSELECTION_H

#include "pdfappglobal.h"
#include "pdfcontentcommands.h"

#include <QGraphicsItem>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>

#include <vector>

namespace pdf
{

class PDFAppEditContentContext;

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.14 ("the selection is the
// only source of truth for the tree, the canvas and the cards"), 4.15 (selection item: frames
// and handles). The plain state of PDFAppEditContentSelection and its getBoundingBox are real
// code of the skeleton (pe1_skeleton_notes.md decision 2, section 11); the rest of this file is
// P11's.

/// The selected nodes of the edited container and the key object. The tab Objects, the canvas
/// and the cards read it and change it; nobody keeps a second selection.
///
/// - The nodes are kept in the order they were selected in; the key object (the reference of
///   Align and Match size, the thicker frame of -multi-selected) is the node selected last, unless
///   setKeyNode names another selected node.
/// - Every node belongs to one container: a selection of another container replaces the
///   selection. The selection does not check that a node exists - who selects reads the model.
/// - A change emits changed() exactly once, last, and only when the selection really changed. A
///   slot may change the selection again or close the tool.
/// - The selection holds identifiers, never pointers to nodes or to scene items.
///
/// What P11 adds (not in the skeleton): the pruning after PDFContentEditSession::modelChanged
/// (removed nodes leave the selection) and the answer to selectionChangeRequested; in stage E2 the
/// selection of characters of the edited text (P22 asks the coordinator for its declarations).
///
/// Threads and ownership: a child of the context, GUI thread; it lives as long as the tool.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentSelection : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppEditContentSelection(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentSelection() override;

    /// The selected nodes in the order of their selection
    const std::vector<PDFContentNodeReference>& getNodes() const { return m_nodes; }

    bool isEmpty() const { return m_nodes.empty(); }
    int getCount() const { return int(m_nodes.size()); }
    bool contains(const PDFContentNodeReference& node) const;

    /// The key object; not valid for an empty selection
    PDFContentNodeReference getKeyNode() const { return m_keyNode; }

    /// Replaces the selection (duplicates and references, which are not valid, are dropped; the
    /// nodes of another container than the first one too). The key object is the last node.
    void setNodes(const std::vector<PDFContentNodeReference>& nodes);

    /// Selects only the node / nothing
    void setNode(const PDFContentNodeReference& node);
    void clear();

    /// Adds the node (Shift + click): it becomes the key object; a node of another container
    /// replaces the selection
    void addNode(const PDFContentNodeReference& node);

    /// Removes the node; the key object passes to the node selected last
    void removeNode(const PDFContentNodeReference& node);

    /// Adds the node, or removes it when it is selected (Ctrl + click)
    void toggleNode(const PDFContentNodeReference& node);

    /// Makes a selected node the key object (a click on an object of a multiple selection);
    /// nothing for a node, which is not selected
    void setKeyNode(const PDFContentNodeReference& node);

    /// The bounding box of the selected nodes in the default space of the page (the nodes of a
    /// form are mapped through the matrices of the isolation path): the union of
    /// PDFAppEditContentGeometryHelper::getPageBoundingBox of the nodes - the one policy of the
    /// box, which the frame of the selection (P11) and the cards (P13) both use. Empty (null) for
    /// an empty selection, while the container is not loaded and for nodes without a box. Real
    /// code of the skeleton (pdfappeditcontentcontext.h).
    QRectF getBoundingBox() const;

Q_SIGNALS:
    /// The nodes or the key object changed
    void changed();

private:
    /// The model changed: the removed nodes and the nodes, which do not exist any more, leave the selection
    void onModelChanged(const PDFContentChange& change);

    /// The session asks to select nodes (the copies of Duplicate, the nodes of an undo step)
    void onSelectionChangeRequested(const QList<PDFContentNodeReference>& nodes);

    /// The node of the card Locked object is forgotten with every change of the selection
    void onChanged();

    QPointer<PDFAppEditContentContext> m_context;
    std::vector<PDFContentNodeReference> m_nodes;
    PDFContentNodeReference m_keyNode;
};

/// The one permanent item of the scene, which paints the frames and the handles of the selection
/// (zadani_gui.md 4.15: frame 3 px outside, eight handles, only the corner ones below 24 px, the
/// round rotation handle, the common frame and the thicker frame of the key object; a constant
/// size on the screen) and the focus ring of the canvas (4.23). It accepts no events: the tool
/// Select asks it which handle lies under a point.
///
/// Threads and ownership: not a QObject. Created by the scene with itself (layer Selection) and
/// deleted with it; it holds the context by a QPointer and reads the selection when it paints.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentSelectionItem : public QGraphicsItem
{
public:
    /// A handle of the frame of the selection
    enum class Handle
    {
        None,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
        Rotation
    };

    explicit PDFAppEditContentSelectionItem(PDFAppEditContentContext* context);
    virtual ~PDFAppEditContentSelectionItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /// The handle under the point of the scene (the handles have a constant size on the screen:
    /// \p pixelSize is the size of a pixel of the view in the units of the scene)
    Handle getHandleAt(const QPointF& scenePoint, qreal pixelSize) const;

    /// The selection, the model or the zoom changed: the geometry is computed again
    void updateGeometry();

    // ---- P11 (the tool Select; no other package calls these) ----

    /// The size of a handle and the offset of the frame on the screen (pixels)
    static constexpr qreal HANDLE_SIZE = 8.0;
    static constexpr qreal FRAME_OFFSET = 3.0;
    static constexpr qreal ROTATION_HANDLE_DISTANCE = 20.0;
    static constexpr qreal SMALL_FRAME = 24.0;  ///< Below it only the corner handles

    /// The box the handles act on (the box of the one node, or the common box of more nodes) in the
    /// default space of the page, without the offset of the frame; empty without a selection
    const QRectF& getHandleBox() const { return m_handleBox; }

    /// The point of the box the handle stands on (Top = the edge of the larger y of the page space;
    /// the frame offset is not added); the center for Rotation and None
    static QPointF getHandlePoint(const QRectF& box, Handle handle);

    /// The handle on the opposite side (the fixed point of a resize)
    static Handle getOppositeHandle(Handle handle);

    /// The handle is shown for the box now (the small box: the corners only; Rotation always)
    bool isHandleShown(Handle handle) const;

    /// The position of the rotation handle in the scene
    QPointF getRotationHandlePoint() const;

    /// The direction "up" of the screen in the scene (a unit vector)
    const QPointF& getScreenUp() const { return m_screenUp; }

private:
    /// The frame of a node (its box, the key object, a locked node)
    struct Frame
    {
        QRectF box;
        bool isKey = false;
    };

    QPointer<PDFAppEditContentContext> m_context;
    std::vector<Frame> m_frames;
    QRectF m_handleBox;
    QRectF m_lockedBox;
    QString m_lockedLabel;      ///< "Locked · Path" (-locked-click)
    QRectF m_focusBox;
    QRectF m_boundingRect;
    QPointF m_screenUp = QPointF(0.0, 1.0);
    qreal m_pixelSize = 1.0;
    bool m_isMultiple = false;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTSELECTION_H
