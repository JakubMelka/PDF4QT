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

#ifndef PDFCONTENTDISPLAY_H
#define PDFCONTENTDISPLAY_H

#include "pdfblendfunction.h"
#include "pdfcontentnode.h"
#include "pdffont.h"
#include "pdfpagecontentprocessor.h"

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <memory>
#include <optional>
#include <vector>

namespace pdf
{
class PDFCMS;
class PDFDocument;
class PDFICCProfileCache;
class PDFOperationControl;

// Edit Content, stage E1, package P10: the display queries of the core - what the canvas of the tool
// draws from (pe1_skeleton_notes.md 8.4, items 1 - 7 and 9). Plan: zadani_gui.md 4.14 - 4.17,
// zadani_jadro.md 4.2, 4.7, zadani_overeni.md 6.2, 6.3.
//
// Threads and ownership of every type of this header: plain immutable values (numbers, QColor,
// QTransform with its type computed, PDFContentPath, PDFObject values, QImage) and shared pointers
// to const data, which co-own what they point to (the realized fonts, whose glyph outlines the text
// points to). A result is made in any thread and read by any number of threads; nothing of it needs
// the GUI thread, an event loop, a QPainterPath or a cache of the caller after the call. Every
// function of PDFContentDisplay is reentrant: it reads the immutable container and, where it takes
// one, the document, the color management and the ICC cache, which must outlive the call only.

/// What a display query needs besides the container. Raw pointers of the caller: they must outlive
/// the call only; nothing of the result points to them.
struct PDFContentDisplayContext
{
    /// The document of the container. nullptr: a color of an override in a space, which is no
    /// device space, stays unconverted (PDFContentDisplayColor::Kind::Unconverted), and the boxes
    /// of a form use and of a shading are the boxes the builder measured.
    const PDFDocument* document = nullptr;

    /// Color management of the conversion of the colors of an override; nullptr = the generic
    /// conversion (PDFCMSGeneric). The colors of the builder are converted already (with the color
    /// management of the session, when it built the container).
    const PDFCMS* cms = nullptr;

    /// Decoded ICC profiles of the document; may be nullptr
    const PDFICCProfileCache* iccProfileCache = nullptr;
};

/// One level of a path of uses: a content, which draws the next container of the path (zadani_gui.md
/// 4.15: the scene draws an isolated form in place, under the matrix of its use).
struct PDFContentDisplayUse
{
    PDFContentContainerPointer container;   ///< The content, which draws the next container (the page for the first level)
    PDFContentNodeId node;                  ///< The FormUse of the next container (a form), or the node painted with the pattern of the next container (a pattern cell)
};

/// A path of uses from the outer content (index 0, usually the page) down to the content, which draws
/// the container of a query. Empty = the default space of the container itself.
using PDFContentDisplayUsePath = std::vector<PDFContentDisplayUse>;

/// A fill or a stroke color as the canvas draws it
struct PDFContentDisplayColor
{
    enum class Kind
    {
        Color,          ///< \p color is the display color
        Pattern,        ///< A pattern (\p patternObject): \p color is the converted color of an uncolored tiling pattern (/PaintType 2), an invalid QColor for every other pattern
        Unconverted     ///< A color of an override, which this query cannot convert (\p value): a space other than DeviceGray, DeviceRGB, DeviceCMYK without a document in the context, a space the document cannot create, or a wrong number of components
    };

    Kind kind = Kind::Color;
    QColor color;                   ///< An opaque RGB color (the alpha is PDFContentDisplayState::alphaFilling / alphaStroking)
    PDFContentColorValue value;     ///< The color as the override sets it; empty for a color of the builder
    PDFObject patternObject;        ///< Pattern: the pattern as the content names it (a reference; a provisional reference of a copy of R-5)
    bool isOverridden = false;      ///< The color comes from the effective override of the node
};

/// The state, with which the written content draws a node: the state of the builder at the beginning
/// of the node (PDFContentNode::getState) with the effective override applied
/// (PDFContentContainer::getEffectiveOverride: the own override of the node and the wraps of its
/// ancestors). Values of PDF (not of Qt) except the cap and the join.
struct PDFContentDisplayState
{
    /// The space of the node -> the target space: T x CTM (T = the transform of the effective
    /// override, CTM = the matrix of the state of the node), followed by the matrix of the container
    /// to the target space (getContainerMatrix) when a path of uses is given
    QTransform matrix;

    PDFReal lineWidth = 1.0;                        ///< In the space of the node; 0 = the thinnest line of the device
    PDFLineDashPattern dashPattern;
    Qt::PenCapStyle lineCap = Qt::FlatCap;
    Qt::PenJoinStyle lineJoin = Qt::MiterJoin;
    PDFReal miterLimit = 10.0;
    PDFReal alphaStroking = 1.0;
    PDFReal alphaFilling = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    TextRenderingMode textRenderingMode = TextRenderingMode::Fill;
    PDFContentDisplayColor fillColor;
    PDFContentDisplayColor strokeColor;
    bool hasSoftMask = false;                       ///< A soft mask is in effect (the canvas does not draw it, zadani_gui.md 4.15)
    PDFOverprintMode overprintMode;                 ///< Not drawn by the canvas (KE.11)

    /// The properties the effective override sets (the others are the values of the builder)
    PDFContentStatePropertySet overriddenProperties;
};

/// The box of a node
struct PDFContentDisplayBox
{
    QRectF box;                 ///< In the target space; empty, when nothing is painted or the extent is unknown
    bool isValid = false;       ///< The node exists and the matrices on the way can be used (else \p box is empty)
    bool isUnknown = false;     ///< The node paints something of an unknown extent: an Opaque node without a box of the builder, a Shading without /BBox outside of every clip, a form use without a box (also a node with such a node below it)
};

/// One glyph of a text for drawing
struct PDFContentDisplayGlyph
{
    /// The outline (in the glyph space), owned by a realized font of PDFContentDisplayText::realizedFonts
    /// (valid as long as the text lives); nullptr = no outline: a glyph of a Type 3 font, a glyph
    /// missing in the font program, a font, which was not realized - the canvas draws \p cell
    const PDFGlyphOutline* outline = nullptr;

    /// The advance cell in the glyph space: x from 0 to w0 x |Tfs| (w0 of the font dictionary; the
    /// move of the text matrix, when the dictionary has no width), y from -0.2 |Tfs| to |Tfs| (as the
    /// builder measures a glyph without an outline); normalized
    QRectF cell;

    /// Glyph space -> target space: [Th x s 0 0 s 0 Ts] x Tm x (the matrix of the display state of
    /// the text show). The glyph space is the space of the realized font (text space scaled by
    /// |Tfs|, s = the sign of Tfs, Th = Tz / 100, Ts = the rise), as PDFContentTextItem::outline.
    QTransform matrix;

    /// Text space of the glyph (Tm before the glyph, no font size, no scaling, no rise) -> target space
    QTransform textMatrix;

    QByteArray codeBytes;               ///< The bytes of the character code (1 to 4)
    unsigned int code = 0;              ///< The bytes as a big-endian number
    QString unicode;                    ///< Its text (PDFContentTextLayout::getUnicode); empty = no Unicode value
    size_t run = 0;                     ///< Index of its text show in PDFContentDisplayText::runs
    size_t itemIndex = 0;               ///< Index of its item in the text items of the show (PDFContentNode::Data::textItems)
};

/// One text show (Tj, TJ, ', ") of a text object with the state it is drawn with
struct PDFContentDisplayTextRun
{
    PDFContentNodeId node;              ///< The TextShow
    PDFContentDisplayState state;       ///< Its display state (colors, rendering mode, line width of a stroked text)
    size_t firstGlyph = 0;              ///< Its glyphs in PDFContentDisplayText::glyphs
    size_t glyphCount = 0;
    bool isType3 = false;               ///< A Type 3 font: the rendering mode does not apply (ISO 32000-2, 9.3.6; zadani_jadro.md 4.7)
    bool isVertical = false;            ///< A font of the vertical writing mode (/WMode 1)
    bool isInvisible = false;           ///< Rendering mode 3 (Invisible), not a Type 3 font
    bool isClipping = false;            ///< Rendering mode 4 - 7, not a Type 3 font
    bool isComplete = true;             ///< PDFContentNode::Data::isTextComplete: the positions are those of the standard
};

/// A line of a text: consecutive glyphs on one base line (definition: PDFContentDisplay::getText)
struct PDFContentDisplayTextLine
{
    size_t firstGlyph = 0;
    size_t glyphCount = 0;
    QTransform matrix;                  ///< The line space (the text space of its first glyph) -> target space
    QRectF rectangle;                   ///< In the line space: the union of the outline boxes and of the cells of its glyphs
    QRectF boundingBox;                 ///< In the target space: the union of the glyph boxes (outline box, else the cell)
    QLineF baseline;                    ///< In the target space: from the origin of the first glyph to the end of the cell of the last one (vertical: to the origin of the last one), at the rise 0
    bool isVertical = false;            ///< A column of a font of the vertical writing mode
    bool isInvisible = false;           ///< Every glyph of the line is of a run with isInvisible
    bool isClipping = false;            ///< A glyph of the line is of a run with isClipping
};

/// The text of a text object for drawing. Threads and ownership: immutable once made; it co-owns the
/// realized fonts its outlines belong to, so it is read by any thread as long as it lives.
struct PDFContentDisplayText
{
    PDFContentNodeId node;                              ///< The TextObject
    std::vector<PDFRealizedFontPointer> realizedFonts;  ///< Keeps the outlines of the glyphs alive
    std::vector<PDFContentDisplayTextRun> runs;         ///< The text shows in the order of the content
    std::vector<PDFContentDisplayGlyph> glyphs;         ///< The glyphs in the order of the content (spacers are not glyphs)
    std::vector<PDFContentDisplayTextLine> lines;       ///< The lines in the order of their first glyphs; every glyph is in exactly one
    QRectF boundingBox;                                 ///< In the target space: the union of the glyph boxes
    bool isInvisible = false;                           ///< Every run is invisible (and there is a glyph)
    bool isClipping = false;                            ///< A run clips
};

using PDFContentDisplayTextPointer = std::shared_ptr<const PDFContentDisplayText>;

/// Options of the decoding of an image for the display (item 4)
struct PDFContentDisplayImageOptions
{
    /// Most pixels of the result (zadani_overeni.md 6.2: 4 megapixels); a larger image is decoded and
    /// scaled down to fit, keeping its aspect ratio
    qint64 maximumPixels = 4000000;

    /// The page the image is shown on: the processor needs a page (the resources a form or a pattern
    /// cell without /Resources of its own inherits). Ignored for a page container (its own index).
    PDFInteger pageIndex = -1;

    const PDFCMS* cms = nullptr;                        ///< Color management; nullptr = the generic one
    const PDFICCProfileCache* iccProfileCache = nullptr; ///< May be nullptr
};

/// The decoded image of an Image node
struct PDFContentDisplayImage
{
    enum class Status
    {
        Decoded,        ///< \p image is the picture
        NotImage,       ///< The node is no Image node, or the container or the document is missing
        TooLarge,       ///< /Width x /Height of the image or of its mask above MAXIMUM_DECODED_PIXELS (or not positive): refused before any decoding
        Failed,         ///< The image cannot be decoded (\p errorText)
        Canceled        ///< The operation control canceled the work
    };

    Status status = Status::NotImage;
    QImage image;               ///< Decoded: at most maximumPixels pixels; ARGB32 premultiplied; row 0 is the top of the image (v = 1 of the unit square)
    QSize sourceSize;           ///< /Width and /Height of the image
    QTransform matrix;          ///< The unit square of the image -> the default space of the container (the matrix of the display state of the node)
    bool isImageMask = false;   ///< A stencil mask: painted with the fill color of the display state of the node
    QString errorText;          ///< Failed: a translated reason
};

/// A clipping path in effect for a node (item 5)
struct PDFContentDisplayClip
{
    PDFContentNodeId node;                  ///< The node, which sets the clip: a Path with W / W*, or a TextObject, which clips by text
    PDFContentPath path;                    ///< The path in the default space of the container (empty for a text clip)
    Qt::FillRule fillRule = Qt::WindingFill;
    bool isText = false;                    ///< A clip by text: the glyphs of getText of \p node, whose runs clip
};

/// The clipping paths a node is drawn under inside of its container
struct PDFContentDisplayClipChain
{
    std::vector<PDFContentDisplayClip> clips;   ///< In the order of the content; the clip region is their intersection
    bool isValid = false;                       ///< The node exists
    bool isUnknown = false;                     ///< An Opaque node before the node may have changed the clip (its leak is unknown)
};

/// Counts of a container for the notice of a complex page (item 6)
struct PDFContentDisplayCounts
{
    size_t nodeCount = 0;           ///< Every node (PDFContentContainer::getNodeCount)
    size_t pathCount = 0;           ///< Path nodes
    size_t pathSegmentCount = 0;    ///< The segments of all Path nodes (PDFContentPath::getSegments; a rectangle is 5)
    size_t glyphCount = 0;          ///< Glyph items of all TextShow nodes
    size_t imageCount = 0;          ///< Image nodes
};

/// A shading for the display (item 7)
struct PDFContentDisplayShading
{
    enum class Status
    {
        Done,
        NotShading,         ///< Neither a Shading node nor a node painted with a shading pattern; or the container or the document is missing
        NotImplemented,     ///< A kind this package does not draw (listed in the report of P10)
        TooLarge,           ///< A mesh above MAXIMUM_MESH_TRIANGLES or a picture above the limit of pixels
        Failed,             ///< The shading cannot be read (\p errorText)
        Canceled
    };

    enum class Kind
    {
        Axial,              ///< A linear gradient: \p start, \p end, \p stops
        Radial,             ///< A radial gradient: \p start, \p startRadius, \p end, \p endRadius, \p stops
        Picture             ///< Another type (1, 4 - 7): \p picture over \p pictureRectangle - not done by P10 (Status::NotImplemented, the picture is empty)
    };

    struct Stop
    {
        PDFReal position = 0.0;     ///< 0 ... 1 along the domain
        QColor color;               ///< The display color
    };

    Status status = Status::NotShading;
    Kind kind = Kind::Axial;
    QTransform matrix;              ///< Shading space -> the default space of the container
    QPointF start;
    QPointF end;
    PDFReal startRadius = 0.0;
    PDFReal endRadius = 0.0;
    bool extendStart = false;
    bool extendEnd = false;
    std::vector<Stop> stops;        ///< At most MAXIMUM_GRADIENT_STOPS
    QColor backgroundColor;         ///< /Background of a shading pattern (invalid without one)
    QImage picture;                 ///< Picture: the rendered shading
    QRectF pictureRectangle;        ///< Picture: the rectangle of the picture in the default space of the container
    QString errorText;
};

/// The cell of a tiling pattern for the display (item 7)
struct PDFContentDisplayTilingCell
{
    enum class Status
    {
        Done,
        NotTilingPattern,   ///< The node is not painted with a tiling pattern; or the container or the document is missing
        NotImplemented,
        TooLarge,
        Failed,
        Canceled
    };

    Status status = Status::NotTilingPattern;
    QImage picture;                 ///< One cell (/BBox of the pattern), at most maximumPixels
    QRectF cellRectangle;           ///< /BBox in the pattern space
    QTransform matrix;              ///< Pattern space -> the default space of the container (/Matrix of the pattern)
    PDFReal xStep = 0.0;
    PDFReal yStep = 0.0;
    QString errorText;
};

/// Display queries over an immutable container (static functions only). Plan: pe1_skeleton_notes.md
/// 8.4; the brief of package P10 (pe1_p10.md).
class PDF4QTLIBCORESHARED_EXPORT PDFContentDisplay
{
public:
    PDFContentDisplay() = delete;

    /// Most levels of a path of uses (PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
    static constexpr int MAXIMUM_USE_DEPTH = 32;

    /// Images with more pixels than this are not decoded (zadani_overeni.md 6.2: 2^31)
    static constexpr qint64 MAXIMUM_DECODED_PIXELS = qint64(1) << 31;

    /// Most stops of a gradient (item 7)
    static constexpr int MAXIMUM_GRADIENT_STOPS = 256;

    /// Most triangles of a mesh drawn for the display (zadani_overeni.md 6.2)
    static constexpr qint64 MAXIMUM_MESH_TRIANGLES = 200000;

    // ---- item 1: display state ----

    /// The display state of a node in the default space of its container.
    /// Thread: any. Cost: the depth of the node (getEffectiveOverride) and the conversion of the
    /// colors of an override (a color space of the document is created for a color, which is no
    /// device color); nothing is cached - one call per node and rebuilt item.
    /// \returns no value for an unknown node, a node without a state, and when a matrix on the way
    ///          cannot be inverted (getEffectiveOverride)
    static std::optional<PDFContentDisplayState> getDisplayState(const PDFContentContainer& container,
                                                                 PDFContentNodeId id,
                                                                 const PDFContentDisplayContext& context);

    /// The same in the target space of a path of uses (getContainerMatrix)
    /// \returns no value also when the path is not valid (getContainerMatrix)
    static std::optional<PDFContentDisplayState> getDisplayState(const PDFContentContainer& container,
                                                                 PDFContentNodeId id,
                                                                 const PDFContentDisplayContext& context,
                                                                 const PDFContentDisplayUsePath& uses);

    /// The matrix from the default space of \p container to the default space of the first content of
    /// the path (the page): for a form the /Matrix of the form x the matrix of the display state of its
    /// FormUse (T x CTM of the use), for a pattern cell the /Matrix of the pattern (ISO 32000-2, 8.7.3.1:
    /// pattern space -> the default space of the content, which uses it), level by level outward.
    /// Identity for an empty path. One visited set of the container keys of the path and of
    /// \p container: a key met twice (a cycle) gives no value.
    /// Thread: any. Cost: the length of the path times the depth of the uses.
    /// \returns no value, when the path is longer than MAXIMUM_USE_DEPTH, a level has no container or
    ///          no such node, the node of a level does not draw the next container (a FormUse of
    ///          another form, a node not painted with the pattern of a pattern cell, a page as an
    ///          inner container), a key is met twice, or a matrix cannot be inverted
    static std::optional<QTransform> getContainerMatrix(const PDFContentContainer& container, const PDFContentDisplayUsePath& uses);

    // ---- item 2: bounding boxes ----

    /// The box of a node in the default space of its container after the effective override:
    ///  - Path: its points (control points included) under the matrix of the display state; a stroked
    ///    path also its solid stroke (the line width, the cap, the join and the miter limit of the
    ///    display state in the space of the node; a dash pattern only takes parts away); the line
    ///    width 0 adds nothing;
    ///  - Image: the unit square under the matrix;
    ///  - FormUse: /BBox of the form under its /Matrix and the matrix (the document of the context;
    ///    without it the box of the builder moved by the override);
    ///  - TextObject, TextShow: the union of the glyph boxes of getText;
    ///  - Shading: /BBox of the shading (the document of the context) intersected with the boxes of the
    ///    clips in effect (getClipChain); without both the extent is unknown (isUnknown);
    ///  - Root, Group, MarkedContent: the union of the children (not deeper than
    ///    PDFContentContainer::MAXIMUM_DEPTH + 2 levels: below them the box of the builder);
    ///  - Opaque: the box of the builder moved by the override; none known: empty and isUnknown;
    ///  - StateRun, MarkedPoint: empty.
    /// Thread: any. Cost: the size of the subtree (a stroked path: its segments).
    static PDFContentDisplayBox getBoundingBox(const PDFContentContainer& container,
                                               PDFContentNodeId id,
                                               const PDFContentDisplayContext& context);

    /// The same in the target space of a path of uses: every leaf is measured under the whole matrix
    /// (not the box of the box)
    static PDFContentDisplayBox getBoundingBox(const PDFContentContainer& container,
                                               PDFContentNodeId id,
                                               const PDFContentDisplayContext& context,
                                               const PDFContentDisplayUsePath& uses);

    // ---- item 3: text for drawing ----

    /// The glyphs of a text object as the written content draws them, and its split into lines.
    /// Positions: the layout of the builder by the standard (PDFContentNode::Data::textItems of its
    /// TextShow nodes, also inside of nested marked content); an override never changes them (the
    /// text properties of an override are the values the node was laid out with, R-2), it moves
    /// the glyphs (matrix) and gives the colors and the rendering mode.
    ///
    /// A line: the glyphs are taken in the order of the content; the next glyph g2 stays in the line of
    /// the previous glyph g1, when both are of the same writing mode and, with the text space of g1
    /// (its Tm, no rise) as the frame: horizontal - the origin of g2 is at |y| <= 0.001 x |Tfs of g1|
    /// and the x axis of g2 points along +x (its y component at most 0.001 of its x component);
    /// vertical - the origin of g2 at |x| <= 0.001 x |Tfs of g1| and the y axis of g2 along +y. A
    /// glyph of a matrix, which cannot be inverted, begins a line. So a jump along the base line (Td on
    /// the same line, a TJ number) keeps the line, and the text rise (a superscript) is not a new
    /// line; a return to an earlier base line later in the content is a new line.
    /// A font, which cannot be realized, gives glyphs without an outline, never an error.
    /// Thread: any. Cost: the glyphs of the text object.
    /// \returns nullptr for an unknown node and for a node, which is no TextObject (or whose state
    ///          cannot be computed)
    static PDFContentDisplayTextPointer getText(const PDFContentContainer& container,
                                                PDFContentNodeId textObjectId,
                                                const PDFContentDisplayContext& context);

    /// The same in the target space of a path of uses
    static PDFContentDisplayTextPointer getText(const PDFContentContainer& container,
                                                PDFContentNodeId textObjectId,
                                                const PDFContentDisplayContext& context,
                                                const PDFContentDisplayUsePath& uses);

    // ---- item 4: image for display ----

    /// Decodes the image of an Image node (an image XObject or an inline image, /Decode, /SMask and
    /// /Mask as the alpha, /ImageMask in the fill color of the display state, through the color
    /// management) by the image decoding of the processor. /Width x /Height of the image and of its mask
    /// above MAXIMUM_DECODED_PIXELS is refused before anything is decoded. An image of a new object of
    /// the session (a provisional reference, stage E3) is not available here (Failed).
    /// Thread: a worker (also any other thread); it creates and destroys its own processor and
    /// caches. Cost: the decoding of the whole image and a scaling. Cancelable by \p control.
    /// \param document The document (must outlive the call; the worker holds it)
    /// \param container The container of the node (held by the caller)
    static PDFContentDisplayImage decodeImage(const PDFDocument* document,
                                              const PDFContentContainerPointer& container,
                                              PDFContentNodeId id,
                                              const PDFContentDisplayImageOptions& options,
                                              const PDFOperationControl* control);

    // ---- item 5: clip in effect ----

    /// The clipping paths a node is drawn under inside of its container: the W / W* paths and the
    /// clipping text objects before it inside of the enclosing q ... Q (also those inside of a marked
    /// content before it, which leaks its clip), in the order of the content. The clip of the node
    /// itself is not one (W applies after the painting of its path). Clips of the uses around the
    /// container are not included (getContainerMatrix, /BBox of a form).
    /// Thread: any. Cost: the nodes before the node inside of its ancestors.
    static PDFContentDisplayClipChain getClipChain(const PDFContentContainer& container, PDFContentNodeId id);

    /// The nodes the clip of a node applies to: the roots of the subtrees after it up to the end of the
    /// enclosing q ... Q (through the end of a marked content or of a text object around it), in the
    /// order of the content. A TextShow, which clips, clips at the end of its text object.
    /// \returns empty for a node without a clipping effect (PDFContentNode::hasClipEffect) and for an
    ///          unknown node
    static std::vector<PDFContentNodeId> getClippedNodes(const PDFContentContainer& container, PDFContentNodeId clipNodeId);

    // ---- item 6: counts ----

    /// Counts of the container for the complex-page notice (QE.10). Computed once per version
    /// (PDFContentContainer::getVersion; a small process-wide cache of the last versions, guarded by
    /// a mutex). Thread: any. Cost: the nodes of the container at the first call of a version.
    static PDFContentDisplayCounts getCounts(const PDFContentContainer& container);

    // ---- item 7: shadings and patterns ----

    /// The shading of a Shading node (sh) or of a node painted with a shading pattern (\p isStroke:
    /// the stroke color, else the fill color): an axial or a radial shading (types 2, 3) as values of
    /// a gradient - the functions sampled at MAXIMUM_GRADIENT_STOPS points of the domain into display
    /// colors, \p matrix the shading space of the node (sh) or the /Matrix of the pattern. The other
    /// types (1, 4 - 7) are not done by package P10: Status::NotImplemented with the kind Picture
    /// (\p picture stays empty). /Background: \p backgroundColor of a pattern (sh ignores it).
    /// \p options: the color management; maximumPixels and pageIndex are not used.
    /// Thread: a worker (also any other thread). Cost: the creation of the shading and 256
    /// evaluations of its function. Cancelable (between the steps).
    static PDFContentDisplayShading getShading(const PDFDocument* document,
                                               const PDFContentContainerPointer& container,
                                               PDFContentNodeId id,
                                               bool isStroke,
                                               const PDFContentDisplayImageOptions& options,
                                               const PDFOperationControl* control);

    /// The cell of the tiling pattern a node is painted with (\p isStroke as getShading): /BBox of the
    /// pattern painted by the painter of the renderer into a picture, which fills the budget
    /// options.maximumPixels with the aspect ratio of /BBox (the caller gives the budget of its zoom;
    /// the size is checked before the picture is allocated); transparent where the cell paints
    /// nothing. An uncolored pattern (/PaintType 2) is painted in the color of the node (ISO 32000-2,
    /// 8.7.3.3). \p matrix is /Matrix (pattern space -> the default space of the container), the steps
    /// /XStep and /YStep; the canvas repeats the picture itself.
    /// Thread: a worker (QPainter on a QImage of its own; it creates its own font cache). Cost: the
    /// painting of the cell. Cancelable.
    static PDFContentDisplayTilingCell getTilingCell(const PDFDocument* document,
                                                     const PDFContentContainerPointer& container,
                                                     PDFContentNodeId id,
                                                     bool isStroke,
                                                     const PDFContentDisplayImageOptions& options,
                                                     const PDFOperationControl* control);
};

}   // namespace pdf

#endif // PDFCONTENTDISPLAY_H
