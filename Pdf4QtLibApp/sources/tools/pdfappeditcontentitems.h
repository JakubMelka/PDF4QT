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

#ifndef PDFAPPEDITCONTENTITEMS_H
#define PDFAPPEDITCONTENTITEMS_H

#include "pdfappglobal.h"
#include "pdfcms.h"
#include "pdfcontentdisplay.h"
#include "pdfcontentnode.h"
#include "pdfdocument.h"

#include <QGraphicsItem>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentItemArguments;
class PDFAppEditContentRegistry;
class PDFAppEditContentScene;

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.14 ("scene items and
// lifetime"), 4.15 (table of the items, hit test), 4.24 (performance). This file: the base of
// the items and the items of stage E1 with their entries; an item of a later stage is a file of
// its package with its entry of the registry.

/// The clipping paths, under which an item is drawn: in the coordinates of the item (the default
/// space of its container), in the order of the content; the clip region is their intersection.
/// GUI thread only (QPainterPath); shared by the items of one clip scope.
using PDFAppEditContentClipPaths = std::vector<QPainterPath>;
using PDFAppEditContentClipPathsPointer = std::shared_ptr<const PDFAppEditContentClipPaths>;

/// The base of the scene items, which draw nodes of the content.
///
/// - Not a QObject (no QGraphicsObject, QGraphicsWidget, QGraphicsProxyWidget, QGraphicsTextItem
///   anywhere in the tool): the rules of the deferred deletion do not apply, the scene has its
///   own (PDFAppEditContentScene): an item is never deleted inside of event processing.
/// - An item accepts no mouse, key or hover events and has no flags of selection or movement:
///   the view gives the events to the tool controller, the selection is drawn by one item.
/// - An item copies or co-owns what it draws (the shared pointers of the node data: the path,
///   the text items with their fonts, the state); it never keeps a pointer to a node, to a
///   container or to the session. A QPainterPath built by the item is used only in the GUI
///   thread.
/// - Paint: the constant alpha and the blend modes of QPainter; no overprint, soft masks or
///   non-separable blend modes (Preview shows them). A hidden object and content of a layer
///   hidden in the editor: a gray dashed outline (updateEditorState).
///
/// Threads and ownership: GUI thread; owned by the scene (created by an entry of the registry,
/// PDFAppEditContentItemEntry::createItem); found by its node identifier
/// (PDFAppEditContentScene::findItem) at the moment of use, never kept.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentItem : public QGraphicsItem
{
public:
    /// The value of QGraphicsItem::type of every item of a node (qgraphicsitem_cast)
    enum { Type = UserType + 1 };

    /// \param nodeId The node the item draws
    /// \param kind The kind of the node
    /// \param parent The item of the parent node, nullptr for a child of the edited root
    explicit PDFAppEditContentItem(PDFContentNodeId nodeId, PDFContentNodeKind kind, PDFAppEditContentItem* parent);
    virtual ~PDFAppEditContentItem() override;

    virtual int type() const override { return Type; }

    PDFContentNodeId getNodeId() const { return m_nodeId; }
    PDFContentNodeKind getNodeKind() const { return m_nodeKind; }

    /// The node has a new version (a command, undo): the item takes the new data. \returns false,
    /// when the item cannot follow the change (another kind of drawing): the scene replaces it.
    /// The default returns false.
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container);

    /// The lock or the "hidden in the editor" state of the node changed (hidden: the gray dashed
    /// outline instead of the content)
    virtual void setEditorState(bool isLocked, bool isHidden);
    bool isLockedInEditor() const { return m_isLocked; }
    bool isHiddenInEditor() const { return m_isHidden; }

    /// The item is outside of the edited group (the dimmed rest of the page, -isolation)
    virtual void setDimmed(bool isDimmed);
    bool isDimmed() const { return m_isDimmed; }

    /// The theme or the device pixel ratio changed: cached paths and pictures are dropped
    /// (zadani_gui.md 4.15, 4.27). The default does nothing.
    virtual void clearCache();

    /// The point of the scene hits the shape of the item (zadani_gui.md 4.15 "hit": the text by
    /// the rectangles of its lines, a path by its fill or by a band around its outline at least
    /// \p tolerance wide - 6 px of the screen in the units of the scene). The default tests the
    /// bounding rectangle.
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const;

    // ---- P11 (the scene of the canvas; no other package calls these) ----

    /// The clipping paths in effect for the node (set by the scene at every walk of the content)
    void setClipPaths(PDFAppEditContentClipPathsPointer clipPaths);
    const PDFAppEditContentClipPathsPointer& getClipPaths() const { return m_clipPaths; }

    /// The transform of the item without a gesture (the matrix of a form use; identity else) and
    /// the move of a gesture of a tool in progress in the coordinates of the parent (the live drag of
    /// the tool Select); the item is drawn by base x gesture
    void setBaseTransform(const QTransform& transform);
    void setGestureTransform(const QTransform& transform);
    const QTransform& getBaseTransform() const { return m_baseTransform; }

    /// A number of the item, unique in the process (a late result of a worker finds its item by it)
    quint64 getSerial() const { return m_serial; }

protected:
    /// The scene of the canvas, nullptr outside of one
    PDFAppEditContentScene* getContentScene() const;

    /// Intersects the clip of the painter with the clipping paths (painter in item coordinates)
    void applyClipPaths(QPainter* painter) const;

    /// The point (item coordinates) lies in the region of the clipping paths
    bool isInsideClipPaths(const QPointF& itemPoint) const;

    /// The tolerance of a hit in the coordinates of the item (the scene tolerance through the
    /// scale of the item in the scene)
    qreal getItemTolerance(qreal sceneTolerance) const;

    /// Paints the gray dashed outline of a hidden object (zadani_gui.md 4.15) of the outline in item
    /// coordinates; the pen is cosmetic
    void paintHiddenOutline(QPainter* painter, const QPainterPath& outline) const;

private:
    void updateTransform();

    PDFContentNodeId m_nodeId;
    PDFContentNodeKind m_nodeKind = PDFContentNodeKind::Opaque;
    bool m_isLocked = false;
    bool m_isHidden = false;
    bool m_isDimmed = false;
    PDFAppEditContentClipPathsPointer m_clipPaths;
    QTransform m_baseTransform;
    QTransform m_gestureTransform;
    quint64 m_serial = 0;
};

/// The one place, where the canvas reads the display queries of the core (PDFContentDisplay,
/// package P10). Each query, which the core implements, is called directly; IS_*_READY says
/// which ones are real at the build of this file - a query still a stub is replaced by what the
/// data of the tree builder give (the outline of the box of the node). P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentDisplayAccess
{
public:
    PDFAppEditContentDisplayAccess() = delete;

    static constexpr bool IS_IMAGE_READY = true;        ///< PDFContentDisplay::decodeImage
    static constexpr bool IS_SHADING_READY = true;      ///< PDFContentDisplay::getShading (types 2 and 3; the others report NotImplemented: the placeholder)

    static std::optional<PDFContentDisplayState> getDisplayState(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context);
    static PDFContentDisplayTextPointer getText(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context);
    static PDFContentDisplayCounts getCounts(const PDFContentContainer& container);

    /// The QPainterPath of a path of the core (GUI thread only)
    static QPainterPath createPath(const PDFContentPath& path, Qt::FillRule fillRule);

    /// The pen of the stroke of the state (in the space of the node)
    static QPen createPen(const PDFContentDisplayState& state, const QColor& color);

    /// The brush of a display color (an unconverted color and a pattern without a color: a neutral
    /// gray, the canvas does not draw them in stage E1), with the alpha
    static QColor getColor(const PDFContentDisplayColor& color, PDFReal alpha);

    /// The composition mode of QPainter of a blend mode (the separable ones; the others Normal)
    static QPainter::CompositionMode getCompositionMode(BlendMode blendMode);

    /// The document of the session as a shared pointer, which a worker holds (the document of the
    /// GUI of the tool, when it is the document of the session); nullptr without a GUI. GUI thread.
    static PDFDocumentPointer getDocument(const PDFAppEditContentContext* context);

    /// The color management of the application (the environment of the host of the GUI); nullptr
    /// without a GUI (the generic one). GUI thread.
    static PDFCMSPointer getColorManagement(const PDFAppEditContentContext* context);
};

/// The item of a Path, which paints (fill, stroke, or both; also with a clip): the path in the
/// space of the node under the matrix of its display state. With View > Clipping paths a path,
/// which also clips, gets the blue dashed outline. P11 only (the class is public for the scene).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentPathItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentPathItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentPathItem() override;

    virtual QRectF boundingRect() const override;
    virtual QPainterPath shape() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;
    virtual void clearCache() override;

    /// The path in the coordinates of the item (the default space of the container)
    const QPainterPath& getContainerPath() const { return m_containerPath; }

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    QPainterPath m_path;                ///< In the space of the node
    QPainterPath m_containerPath;       ///< In the default space of the container
    QTransform m_matrix;                ///< The space of the node -> the default space of the container
    PDFContentPathPaint m_paint;
    std::optional<PDFContentDisplayState> m_state;
    QRectF m_boundingRect;
    mutable QPainterPath m_strokeShape; ///< Cache of the hit band (m_strokeShapeWidth)
    mutable qreal m_strokeShapeWidth = -1.0;
};

/// The item of a clipping path, which paints nothing ("n" with W / W*): the blue dashed outline,
/// only with View > Clipping paths (the only blue dashed outline of the tool). P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentClipPathItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentClipPathItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentClipPathItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    QPainterPath m_containerPath;
    QRectF m_boundingRect;
};

/// The item of a text object: the glyph outlines of its lines (one path per line, built at the
/// first paint of the line; the positions of the layout of the core), hit by the rectangles of its
/// lines. Invisible text is not drawn; with View > Invisible text its base lines are gray dotted
/// lines. A glyph without an outline (Type 3, a missing glyph) is drawn as its advance cell. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentTextItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentTextItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentTextItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;
    virtual void clearCache() override;

    /// The text of the core (computed at the first use; nullptr when it cannot be)
    const PDFContentDisplayTextPointer& getText() const;

    /// The text is invisible (rendering mode 3 only)
    bool isInvisible() const;

private:
    /// The paths of a line (fill and stroke) by its runs
    struct LinePaths
    {
        bool isBuilt = false;
        std::vector<QPainterPath> runPaths;     ///< One per run of the line, in the coordinates of the item
        std::vector<size_t> runIndices;         ///< The run of each path
        QPainterPath cellPath;                  ///< The cells of the glyphs without an outline
    };

    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);
    void buildLine(size_t lineIndex) const;

    PDFContentContainerKey m_containerKey;
    QRectF m_builderBox;                                ///< The box of the builder (before the text is computed)
    mutable bool m_isTextComputed = false;
    mutable PDFContentDisplayTextPointer m_text;
    mutable std::vector<LinePaths> m_linePaths;
    QRectF m_boundingRect;
};

/// The item of an image: the decoded picture (scaled down) under the matrix of the image, decoded
/// in a worker at the first paint (PDFAppEditContentScene::requestDecoding); a light placeholder
/// until then. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentImageItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentImageItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentImageItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual void clearCache() override;

    const PDFContentContainerKey& getContainerKey() const { return m_containerKey; }

    /// The result of the decoding (the scene; a stencil mask is painted in \p maskColor)
    void setDecodedImage(const PDFContentDisplayImage& image);

    /// The decoding was asked for (the scene starts it once per item and version)
    bool isDecodingRequested() const { return m_isDecodingRequested; }
    bool hasPicture() const { return !m_image.isNull(); }

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    PDFContentContainerKey m_containerKey;
    QTransform m_matrix;                ///< The unit square -> the default space of the container
    QRectF m_boundingRect;
    QImage m_image;
    QColor m_maskColor;
    bool m_isDecodingRequested = false;
    bool m_isDecodingFailed = false;
};

/// The item of a shading (sh): an axial or a radial shading as a QGradient, another type as the
/// picture of the core; computed in a worker at the first paint. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentShadingItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentShadingItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentShadingItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual void clearCache() override;

    const PDFContentContainerKey& getContainerKey() const { return m_containerKey; }
    void setShading(const PDFContentDisplayShading& shading);
    bool isDecodingRequested() const { return m_isDecodingRequested; }

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    PDFContentContainerKey m_containerKey;
    QRectF m_boundingRect;
    std::optional<PDFContentDisplayShading> m_shading;
    bool m_isDecodingRequested = false;
};

/// The item of a use of a form XObject: the items of the container of the form are its children
/// (built by the scene, when the session has the form loaded), under the /Matrix of the form and the
/// matrix of the use, clipped to /BBox. Until then (and for a form, which cannot be loaded or is
/// nested too deep) the box of the builder is drawn as a light outline. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentFormItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentFormItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentFormItem() override;

    virtual QRectF boundingRect() const override;
    virtual QPainterPath shape() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;

    /// The key of the form (not valid for a form without an indirect object)
    const PDFContentContainerKey& getFormKey() const { return m_formKey; }

    /// The matrix of the use (the space of the use -> the default space of the container)
    const QTransform& getUseMatrix() const { return m_useMatrix; }

    /// The version of the form container, whose items are the children (0 = none)
    quint64 getFormVersion() const { return m_formVersion; }

    /// The scene has built the children of the form container \p form (its /BBox and /Matrix are
    /// taken; the children are clipped to /BBox)
    void setForm(const PDFContentContainer& form);

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    PDFContentContainerKey m_formKey;
    QTransform m_useMatrix;
    QRectF m_builderBox;        ///< In the default space of the container
    QRectF m_formBox;           ///< /BBox in the form space (with a form)
    quint64 m_formVersion = 0;
};

/// The item of a Group or a MarkedContent: it paints nothing (its children are items of their own
/// in the order of the content); it marks the node for findItem. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentGroupItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentGroupItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentGroupItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;
};

/// The item of an Opaque node: a hatched rectangle in the box of the builder (no box: nothing). P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentOpaqueItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentOpaqueItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentOpaqueItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;

private:
    QRectF m_box;
};

/// The entries of this file for the registry: the items of stage E1 (zadani_gui.md 4.15, the
/// table: path, clipping path, text line, image, shading as a picture, form use, group, opaque)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentItems
{
public:
    PDFAppEditContentItems() = delete;

    static void registerParts(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTITEMS_H
