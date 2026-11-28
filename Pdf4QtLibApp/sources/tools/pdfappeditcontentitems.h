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

#include <QBrush>
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

// Edit Content, stage E1, package P11 (canvas); fix package FX2. Plan: zadani_gui.md 4.14 ("scene
// items and lifetime"), 4.15 (table of the items, hit test), 4.24 (performance). This file: the
// base of the items and the items of stage E1 with their entries; an item of a later stage is a
// file of its package with its entry of the registry.

/// One clip of the chain of the clips in effect for an item (ISO 32000-2, 8.5.4; R6-3, R6-12): a
/// persistent list - a clip holds the chain of the clips before it in its scope, so a new clip
/// costs O(1) and every item of one clip scope shares one object. The scene keeps the object of
/// an unchanged clipping node over its walks, so the items under an unchanged clip keep their
/// pointer and are not painted again. The path is in the default space of the container of the
/// item (the coordinates of the parent of the item). GUI thread only (QPainterPath).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentClipNode
{
public:
    /// At most this many clip paths are intersected exactly by one paint or one hit of an item
    /// (the innermost ones first, the clips of the form uses around the item included); the clips
    /// beyond them only by the intersection of their bounding rectangles (hostile input: 100 000
    /// clips in one scope - Preview draws them exactly)
    static constexpr int MAXIMUM_EXACT_CLIPS = 16;

    /// \param parent The clips before this one in its scope (nullptr = none)
    /// \param path The clipping path with its fill rule
    explicit PDFAppEditContentClipNode(std::shared_ptr<const PDFAppEditContentClipNode> parent, QPainterPath path);

    /// Releases the parents held only by this node one by one (a long chain is not released recursively)
    ~PDFAppEditContentClipNode();

    PDFAppEditContentClipNode(const PDFAppEditContentClipNode&) = delete;
    PDFAppEditContentClipNode& operator=(const PDFAppEditContentClipNode&) = delete;

    const std::shared_ptr<const PDFAppEditContentClipNode>& getParent() const { return m_parent; }
    const QPainterPath& getPath() const { return m_path; }

    /// The intersection of the bounding rectangles of this clip and of all clips before it
    const QRectF& getBounds() const { return m_bounds; }

    /// Number of the clips of the chain (this one included)
    size_t getLength() const { return m_length; }

    /// Intersects the clip of the painter (its world transform maps the space of the chain) with
    /// the chain: at most \p budget paths exactly, the rest by their bounds; \p budget is lowered
    /// by the paths used
    void apply(QPainter* painter, int* budget) const;

    /// The point (the space of the chain) lies in the region of the chain (the same rule of the
    /// budget as apply)
    bool contains(const QPointF& point, int* budget) const;

private:
    mutable std::shared_ptr<const PDFAppEditContentClipNode> m_parent;     ///< mutable: moved out by the destructor of the child only
    QPainterPath m_path;
    QRectF m_bounds;
    size_t m_length = 1;
};

/// The chain of the clips in effect for an item: its innermost clip, nullptr = no clip
using PDFAppEditContentClipPathsPointer = std::shared_ptr<const PDFAppEditContentClipNode>;

/// The identity of a decoded picture of an image (R6-5, R6-14): one picture per image XObject
/// (all its uses share it), an inline image per its node; a stencil mask per the fill color it is
/// painted with. The size step is not part of the key: the scene keeps the largest picture
/// decoded for the key.
struct PDFAppEditContentPictureKey
{
    PDFObjectReference xobject;         ///< The image XObject; not valid for an inline image
    PDFContentContainerKey container;   ///< An inline image: its container (else not valid)
    PDFContentNodeId node;              ///< An inline image: its node (else 0)
    QRgb maskColor = 0;                 ///< The fill color of the display state (a stencil mask is painted in it)

    bool operator==(const PDFAppEditContentPictureKey& other) const;
    bool operator<(const PDFAppEditContentPictureKey& other) const;
};

/// The kinds of the work the scene gives to a worker for an item
enum class PDFAppEditContentDecodeKind
{
    Image,          ///< PDFContentDisplay::decodeImage (shared by the key of the picture)
    Shading,        ///< PDFContentDisplay::getShading of a sh node
    FillPattern,    ///< The pattern of the fill of a path: getTilingCell, else getShading
    StrokePattern,  ///< The same for the stroke
    Underlay        ///< The picture of the annotations and fields of the page
};

/// The paint of a pattern of a path, ready for QPainter (made in the GUI thread from the plain
/// result of a worker; item 2 of the fix package): a brush in the default space of the container
struct PDFAppEditContentPatternPaint
{
    bool isDone = false;                ///< The worker has answered (\p brush may still be empty: a placeholder)
    bool isPainted = false;             ///< \p brush paints the pattern (else the neutral gray placeholder)
    QBrush brush;                       ///< Texture or gradient, its transform maps it into the default space of the container
    QColor backgroundColor;             ///< /Background of a shading pattern (invalid without one)
    qint64 pixels = 0;                  ///< The budget of pixels the picture was made for (the zoom step)
};

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
/// - Clips: the chain of an item is in the coordinates of its parent; the clips of the form uses
///   around an item (their chains in their parents) apply to it too (R6-7), under one budget of
///   exact paths (PDFAppEditContentClipNode::MAXIMUM_EXACT_CLIPS).
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
    /// bounding rectangle and the clips (R6-9). An object hidden in the editor is hit only by a
    /// band around its outline (R6-17: hiding exists to reach what lies below).
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const;

    // ---- P11 (the scene of the canvas; no other package calls these) ----

    /// The clipping paths in effect for the node (set by the scene at every walk of the content;
    /// an equal pointer changes nothing)
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

    /// The opacity of the content of the item (the constant alpha of an image or a form); the
    /// dimming multiplies it (R6-15)
    qreal getBaseOpacity() const { return m_baseOpacity; }

protected:
    /// The scene of the canvas, nullptr outside of one
    PDFAppEditContentScene* getContentScene() const;

    /// Intersects the clip of the painter with the clipping paths of the item and of the form uses
    /// around it (painter in item coordinates; its world transform is kept)
    void applyClipPaths(QPainter* painter) const;

    /// The point (item coordinates) lies in the region of the clipping paths of the item and of the
    /// form uses around it
    bool isInsideClipPaths(const QPointF& itemPoint) const;

    /// The tolerance of a hit in the coordinates of the item (the scene tolerance through the
    /// scale of the item in the scene)
    qreal getItemTolerance(qreal sceneTolerance) const;

    /// Paints the gray dashed outline of a hidden object (zadani_gui.md 4.15) of the outline in item
    /// coordinates; the pen is cosmetic
    void paintHiddenOutline(QPainter* painter, const QPainterPath& outline) const;

    /// The hit of a hidden object: a band of the tolerance around the outline (item coordinates)
    bool isHitByOutline(const QPainterPath& outline, const QPointF& itemPoint, qreal itemTolerance) const;

    /// The opacity of the content (1 = opaque); the item opacity is this times the dimming
    void setBaseOpacity(qreal opacity);

private:
    void updateTransform();
    void updateOpacity();

    PDFContentNodeId m_nodeId;
    PDFContentNodeKind m_nodeKind = PDFContentNodeKind::Opaque;
    bool m_isLocked = false;
    bool m_isHidden = false;
    bool m_isDimmed = false;
    qreal m_baseOpacity = 1.0;
    PDFAppEditContentClipPathsPointer m_clipPaths;
    QTransform m_baseTransform;
    QTransform m_gestureTransform;
    quint64 m_serial = 0;
};

/// The one place, where the canvas reads the display queries of the core (PDFContentDisplay,
/// package P10). P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentDisplayAccess
{
public:
    PDFAppEditContentDisplayAccess() = delete;

    static std::optional<PDFContentDisplayState> getDisplayState(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context);
    static PDFContentDisplayTextPointer getText(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context);
    static PDFContentDisplayCounts getCounts(const PDFContentContainer& container);

    /// The QPainterPath of a path of the core (GUI thread only)
    static QPainterPath createPath(const PDFContentPath& path, Qt::FillRule fillRule);

    /// The pen of the stroke of the state (in the space of the node; the dash pattern by
    /// PDFLineDashPattern::createForQPen of the core)
    static QPen createPen(const PDFContentDisplayState& state, const QColor& color);

    /// The brush of a display color (an unconverted color and a pattern without a color: a neutral
    /// gray placeholder - a pattern is drawn by its paint, PDFAppEditContentPatternPaint), with the
    /// alpha
    static QColor getColor(const PDFContentDisplayColor& color, PDFReal alpha);

    /// The composition mode of QPainter of a blend mode (PDFBlendModeInfo of the core: the
    /// separable ones; the others Normal)
    static QPainter::CompositionMode getCompositionMode(BlendMode blendMode);

    /// The document of the session as the shared pointer of the context, which a worker holds
    /// (PDFAppEditContentContext::getDocumentPointer; also without a GUI). GUI thread.
    static PDFDocumentPointer getDocument(const PDFAppEditContentContext* context);

    /// The color management of the application (the environment of the host of the GUI); nullptr
    /// without a GUI (the generic one). GUI thread; the scene asks once.
    static PDFCMSPointer getColorManagement(const PDFAppEditContentContext* context);

    /// The size step of a picture for \p pixels needed on the screen: the next power of 4, at
    /// least 4096 and at most PDFContentDisplayImageOptions::maximumPixels (4 megapixels)
    static qint64 getPixelStep(qreal pixels);

    /// The pixels of the device the rectangle of the item coordinates covers under \p transform
    /// (item -> device)
    static qreal getDevicePixels(const QRectF& rectangle, const QTransform& transform);

    /// The paint of the result of a worker for a pattern (a tiling cell repeated by its steps, a
    /// gradient, a picture of a shading); an empty brush when the result cannot be drawn
    static PDFAppEditContentPatternPaint createPatternPaint(const PDFContentDisplayTilingCell& cell, const PDFContentDisplayShading& shading, qint64 pixels);
};

/// The item of a Path (fill, stroke, both, neither; also with a clip, which is not a clipping path
/// only): the path in the space of the node under the matrix of its display state; a pattern of
/// the fill or the stroke by its paint, computed in a worker per zoom step (item 2). A path, whose
/// painting is switched off (n without a clip, item 8), draws nothing and is hit by nothing - it
/// is selected from the tree and shows its frame then. With View > Clipping paths a path, which
/// also clips, gets the blue dashed outline. P11 only (the class is public for the scene).
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

    const PDFContentContainerKey& getContainerKey() const { return m_containerKey; }

    /// The path paints something (its fill or its stroke is on)
    bool isPainted() const { return m_paint.fill || m_paint.stroke; }

    /// The fill / the stroke is painted with a pattern
    bool hasPattern(bool isStroke) const;

    /// The paint of the pattern of the fill / the stroke (the scene; a late result of an older zoom
    /// step or version never comes - the scene drops it)
    void setPatternPaint(bool isStroke, const PDFAppEditContentPatternPaint& paint);
    const PDFAppEditContentPatternPaint& getPatternPaint(bool isStroke) const { return isStroke ? m_strokePattern : m_fillPattern; }

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    /// Asks the scene for the paint of a pattern, when the zoom needs a larger picture than the one
    /// it has (\p pixels: the pixels of the pattern cell on the device)
    void requestPattern(bool isStroke, qreal pixels);

    PDFContentContainerKey m_containerKey;
    QPainterPath m_path;                ///< In the space of the node
    QPainterPath m_containerPath;       ///< In the default space of the container
    QTransform m_matrix;                ///< The space of the node -> the default space of the container
    PDFContentPathPaint m_paint;
    std::optional<PDFContentDisplayState> m_state;
    QRectF m_boundingRect;
    mutable QPainterPath m_strokeShape; ///< Cache of the hit band (m_strokeShapeWidth)
    mutable qreal m_strokeShapeWidth = -1.0;
    PDFAppEditContentPatternPaint m_fillPattern;
    PDFAppEditContentPatternPaint m_strokePattern;
    qint64 m_requestedPixels[2] = { 0, 0 }; ///< The step asked for (fill, stroke); 0 = none
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

    /// The clip the text sets at its end (ISO 32000-2, 9.3.6: the glyphs of the shows of the
    /// rendering modes 4 - 7, in the default space of the container); nullopt = it sets none
    std::optional<QPainterPath> getTextClip() const;

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

/// The item of an image: the decoded picture (scaled down to the size step of the zoom) under the
/// matrix of the image; the pictures are kept by the scene, one per image object and shared by its
/// uses (PDFAppEditContentScene::getPicture, R6-5), so a move or a resize keeps the picture
/// (R6-14). A light placeholder until the first picture comes. Hit by the unit square under its
/// matrix and the clips (R6-9). P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentImageItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentImageItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentImageItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;
    virtual void clearCache() override;

    const PDFContentContainerKey& getContainerKey() const { return m_containerKey; }
    const PDFAppEditContentPictureKey& getPictureKey() const { return m_pictureKey; }

    /// The scene has a picture of the image (of any size step)
    bool hasPicture() const;

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    PDFContentContainerKey m_containerKey;
    PDFAppEditContentPictureKey m_pictureKey;
    QTransform m_matrix;                ///< The unit square -> the default space of the container
    QRectF m_boundingRect;
    BlendMode m_blendMode = BlendMode::Normal;
};

/// The item of a shading (sh): an axial or a radial shading as a QGradient, another type as the
/// picture of the core (the meshes too); computed in a worker at the first paint; the constant
/// alpha and the blend mode of its state (R6-16). A placeholder only for TooLarge / Failed. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentShadingItem : public PDFAppEditContentItem
{
public:
    explicit PDFAppEditContentShadingItem(const PDFAppEditContentItemArguments& arguments);
    virtual ~PDFAppEditContentShadingItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    virtual bool updateNode(const PDFContentNode& node, const PDFContentContainer& container) override;
    virtual bool isHit(const QPointF& scenePoint, qreal tolerance) const override;
    virtual void clearCache() override;

    const PDFContentContainerKey& getContainerKey() const { return m_containerKey; }
    void setShading(const PDFContentDisplayShading& shading);
    const std::optional<PDFContentDisplayShading>& getShading() const { return m_shading; }
    bool isDecodingRequested() const { return m_isDecodingRequested; }

private:
    void setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context);

    PDFContentContainerKey m_containerKey;
    QRectF m_boundingRect;
    std::optional<PDFContentDisplayShading> m_shading;
    PDFReal m_alpha = 1.0;
    BlendMode m_blendMode = BlendMode::Normal;
    bool m_isDecodingRequested = false;
};

/// The item of a use of a form XObject: the items of the container of the form are its children
/// (built by the scene, when the session has the form loaded and the budget of the items of forms
/// of the page allows, R6-1), under the /Matrix of the form and the matrix of the use, clipped to
/// /BBox and by the clips of the content around the use (R6-7). Until then (and for a form, which
/// cannot be loaded, is nested too deep, draws itself, or is beyond the budget) the box of the
/// builder is drawn as a light outline. A hidden use hides its content (R6-8). P11 only.
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
    virtual void setEditorState(bool isLocked, bool isHidden) override;

    /// The key of the form (not valid for a form without an indirect object)
    const PDFContentContainerKey& getFormKey() const { return m_formKey; }

    /// The matrix of the use (the space of the use -> the default space of the container)
    const QTransform& getUseMatrix() const { return m_useMatrix; }

    /// The version of the form container, whose items are the children (0 = none)
    quint64 getFormVersion() const { return m_formVersion; }

    /// The scene has built the children of the form container \p form (its /BBox and /Matrix are
    /// taken; the children are clipped to /BBox)
    void setForm(const PDFContentContainer& form);

    /// The children are gone without new ones (the clips around the use changed, the budget): the
    /// outline is drawn until the scene builds them again
    void resetForm();

    /// The children follow the hidden state of the use (after a build too)
    void updateChildrenVisibility();

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

/// The overlay of the annotations and form fields of the page (zadani_gui.md 4.15, frames
/// -annotations-underlay / -annotations-hidden; item 1 of the fix package): their appearances
/// drawn dimmed under the content, never selectable (it is no item of a node and accepts no
/// events), shown while the option showAnnotations is on and Preview is off. The picture is
/// rendered by the scene in a worker (the annotations of the document, which the tool does not
/// edit), a late result for another page or request is dropped. P11 only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentUnderlayItem : public QGraphicsItem
{
public:
    /// The opacity of the dimmed annotations
    static constexpr qreal DIMMED_OPACITY = 0.35;

    explicit PDFAppEditContentUnderlayItem();
    virtual ~PDFAppEditContentUnderlayItem() override;

    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /// The picture of the annotations covers \p pageRectangle (the default space of the page; row 0
    /// is its top); a null picture: nothing (no annotation, another page)
    void setPicture(const QImage& picture, const QRectF& pageRectangle, PDFInteger pageIndex);
    const QImage& getPicture() const { return m_picture; }
    PDFInteger getPageIndex() const { return m_pageIndex; }

private:
    QImage m_picture;
    QRectF m_rectangle;
    PDFInteger m_pageIndex = -1;
};

/// The entries of this file for the registry: the items of stage E1 (zadani_gui.md 4.15, the
/// table: path, clipping path, text line, image, shading as a picture, form use, group, opaque)
/// and the overlay of the annotations
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentItems
{
public:
    PDFAppEditContentItems() = delete;

    static void registerParts(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTITEMS_H
