//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFANNOTATION_H
#define PDFANNOTATION_H

#include "pdfutils.h"
#include "pdfobject.h"
#include "pdfaction.h"
#include "pdffile.h"
#include "pdfmultimedia.h"
#include "pdfdocumentdrawinterface.h"

#include <QPainterPath>

#include <array>

namespace pdf
{
class PDFObjectStorage;
class PDFDrawWidgetProxy;

using TextAlignment = Qt::Alignment;

enum class AnnotationType
{
    Invalid,
    Text,
    Link,
    FreeText,
    Line,
    Square,
    Circle,
    Polygon,
    Polyline,
    Highlight,
    Underline,
    Squiggly,
    StrikeOut,
    Stamp,
    Caret,
    Ink,
    Popup,
    FileAttachment,
    Sound,
    Movie,
    Widget,
    Screen,
    PrinterMark,
    TrapNet,
    Watermark,
    _3D
};

enum class AnnotationLineEnding
{
    None,
    Square,
    Circle,
    Diamond,
    OpenArrow,
    ClosedArrow,
    Butt,
    ROpenArrow,
    RClosedArrow,
    Slash
};

/// Represents annotation's border. Two main definition exists, one is older,
/// called \p Simple, the other one is defined in BS dictionary of the annotation.
class PDFAnnotationBorder
{
public:
    explicit inline PDFAnnotationBorder() = default;

    enum class Definition
    {
        Invalid,
        Simple,
        BorderStyle
    };

    enum class Style
    {
        Solid,
        Dashed,
        Beveled,
        Inset,
        Underline
    };

    /// Parses the annotation border from the array. If object contains invalid annotation border,
    /// then default annotation border is returned. If object is empty, empty annotation border is returned.
    /// \param storage Object storage
    /// \param object Border object
    static PDFAnnotationBorder parseBorder(const PDFObjectStorage* storage, PDFObject object);

    /// Parses the annotation border from the BS dictionary. If object contains invalid annotation border,
    /// then default annotation border is returned. If object is empty, empty annotation border is returned.
    /// \param storage Object storage
    /// \param object Border object
    static PDFAnnotationBorder parseBS(const PDFObjectStorage* storage, PDFObject object);

    /// Returns true, if object is correctly defined
    bool isValid() const { return m_definition != Definition::Invalid; }

    Definition getDefinition() const { return m_definition; }
    Style getStyle() const { return m_style; }
    PDFReal getHorizontalCornerRadius() const { return m_hCornerRadius; }
    PDFReal getVerticalCornerRadius() const { return m_vCornerRadius; }
    PDFReal getWidth() const { return m_width; }
    const std::vector<PDFReal>& getDashPattern() const { return m_dashPattern; }

private:
    Definition m_definition = Definition::Invalid;
    Style m_style = Style::Solid;
    PDFReal m_hCornerRadius = 0.0;
    PDFReal m_vCornerRadius = 0.0;
    PDFReal m_width = 1.0;
    std::vector<PDFReal> m_dashPattern;
};

using AnnotationBorderStyle = PDFAnnotationBorder::Style;

/// Annotation border effect
class PDFAnnotationBorderEffect
{
public:
    explicit inline PDFAnnotationBorderEffect() = default;

    enum class Effect
    {
        None,
        Cloudy
    };

    /// Parses the annotation border effect from the object. If object contains invalid annotation border effect,
    /// then default annotation border effect is returned. If object is empty, also default annotation border effect is returned.
    /// \param storage Object storage
    /// \param object Border effect object
    static PDFAnnotationBorderEffect parse(const PDFObjectStorage* storage, PDFObject object);

private:
    Effect m_effect = Effect::None;
    PDFReal m_intensity = 0.0;
};

/// Storage which handles appearance streams of annotations. Appeareance streams are divided
/// to three main categories - normal, rollower and down. Each category can have different
/// states, for example, checkbox can have on/off state. This container can also resolve
/// queries to obtain appropriate appearance stream.
class PDFAppeareanceStreams
{
public:
    explicit inline PDFAppeareanceStreams() = default;

    enum class Appearance
    {
        Normal,
        Rollover,
        Down
    };

    using Key = std::pair<Appearance, QByteArray>;

    /// Parses annotation appearance streams from the object. If object is invalid, then
    /// empty appearance stream is constructed.
    /// \param storage Object storage
    /// \param object Appearance streams object
    static PDFAppeareanceStreams parse(const PDFObjectStorage* storage, PDFObject object);

    /// Tries to search for appearance stream for given appearance. If no appearance is found,
    /// then null object is returned.
    /// \param appearance Appearance type
    PDFObject getAppearance(Appearance appearance = Appearance::Normal) const { return getAppearance(appearance, QByteArray()); }

    /// Tries to resolve appearance stream for given appearance and state. If no appearance is found,
    /// then null object is returned.
    /// \param appearance Appearance type
    /// \param state State name
    PDFObject getAppearance(Appearance appearance, const QByteArray& state) const;

private:
    std::map<Key, PDFObject> m_appearanceStreams;
};

/// Represents annotation's active region, it is used also to
/// determine underline lines.
class PDFAnnotationQuadrilaterals
{
public:
    using Quadrilateral = std::array<QPointF, 4>;
    using Quadrilaterals = std::vector<Quadrilateral>;

    inline explicit PDFAnnotationQuadrilaterals() = default;
    inline explicit PDFAnnotationQuadrilaterals(QPainterPath&& path, Quadrilaterals&& quadrilaterals) :
        m_path(qMove(path)),
        m_quadrilaterals(qMove(quadrilaterals))
    {

    }

    const QPainterPath& getPath() const { return m_path; }
    const Quadrilaterals& getQuadrilaterals() const { return m_quadrilaterals; }
    bool isEmpty() const { return m_path.isEmpty(); }

private:
    QPainterPath m_path;
    Quadrilaterals m_quadrilaterals;
};

/// Represents callout line (line from annotation to some point)
class PDFAnnotationCalloutLine
{
public:

    enum class Type
    {
        Invalid,
        StartEnd,
        StartKneeEnd
    };

    inline explicit PDFAnnotationCalloutLine() = default;
    inline explicit PDFAnnotationCalloutLine(QPointF start, QPointF end) :
        m_type(Type::StartEnd),
        m_points({start, end})
    {

    }

    inline explicit PDFAnnotationCalloutLine(QPointF start, QPointF knee, QPointF end) :
        m_type(Type::StartKneeEnd),
        m_points({start, knee, end})
    {

    }

    /// Parses annotation callout line from the object. If object is invalid, then
    /// invalid callout line is constructed.
    /// \param storage Object storage
    /// \param object Callout line object
    static PDFAnnotationCalloutLine parse(const PDFObjectStorage* storage, PDFObject object);

    bool isValid() const { return m_type != Type::Invalid; }
    Type getType() const { return m_type; }

    QPointF getPoint(int index) const { return m_points.at(index); }

private:
    Type m_type = Type::Invalid;
    std::array<QPointF, 3> m_points;
};

/// Information about annotation icon fitting (in the widget)
class PDFAnnotationIconFitInfo
{
public:
    inline explicit PDFAnnotationIconFitInfo() = default;

    enum class ScaleCondition
    {
        Always,
        ScaleBigger,
        ScaleSmaller,
        Never
    };

    enum class ScaleType
    {
        Anamorphic,  ///< Do not keep aspect ratio, fit whole annotation rectangle
        Proportional ///< Keep aspect ratio, annotation rectangle may not be filled fully with icon
    };

    /// Parses annotation appearance icon fit info from the object. If object is invalid, then
    /// default appearance icon fit info is constructed.
    /// \param storage Object storage
    /// \param object Appearance icon fit info object
    static PDFAnnotationIconFitInfo parse(const PDFObjectStorage* storage, PDFObject object);

private:
    ScaleCondition m_scaleCondition = ScaleCondition::Always;
    ScaleType m_scaleType = ScaleType::Proportional;
    QPointF m_relativeProportionalPosition = QPointF(0.5, 0.5);
    bool m_fullBox = false;
};

/// Additional appearance characteristics used for constructing of appearance
/// stream to display annotation on the screen (or just paint it).
class PDFAnnotationAppearanceCharacteristics
{
public:
    inline explicit PDFAnnotationAppearanceCharacteristics() = default;

    enum class PushButtonMode
    {
        NoIcon,
        NoCaption,
        IconWithCaptionBelow,
        IconWithCaptionAbove,
        IconWithCaptionRight,
        IconWithCaptionLeft,
        IconWithCaptionOverlaid
    };

    /// Number of degrees by which the widget annotation is rotated
    /// counterclockwise relative to the page.
    PDFInteger getRotation() const { return m_rotation; }
    const std::vector<PDFReal>& getBorderColor() const { return m_borderColor; }
    const std::vector<PDFReal>& getBackgroundColor() const { return m_backgroundColor; }
    const QString& getNormalCaption() const { return m_normalCaption; }
    const QString& getRolloverCaption() const { return m_rolloverCaption; }
    const QString& getDownCaption() const { return m_downCaption; }
    const PDFObject& getNormalIcon() const { return m_normalIcon; }
    const PDFObject& getRolloverIcon() const { return m_rolloverIcon; }
    const PDFObject& getDownIcon() const { return m_downIcon; }
    const PDFAnnotationIconFitInfo& getIconFit() const { return m_iconFit; }
    PushButtonMode getPushButtonMode() const { return m_pushButtonMode; }

    /// Parses annotation appearance characteristics from the object. If object is invalid, then
    /// default appearance characteristics is constructed.
    /// \param storage Object storage
    /// \param object Appearance characteristics object
    static PDFAnnotationAppearanceCharacteristics parse(const PDFObjectStorage* storage, PDFObject object);

private:
    PDFInteger m_rotation = 0;
    std::vector<PDFReal> m_borderColor;
    std::vector<PDFReal> m_backgroundColor;
    QString m_normalCaption;
    QString m_rolloverCaption;
    QString m_downCaption;
    PDFObject m_normalIcon;
    PDFObject m_rolloverIcon;
    PDFObject m_downIcon;
    PDFAnnotationIconFitInfo m_iconFit;
    PushButtonMode m_pushButtonMode = PushButtonMode::NoIcon;
};

/// Storage for annotation additional actions
class PDFAnnotationAdditionalActions
{
public:

    enum Action
    {
        CursorEnter,
        CursorLeave,
        MousePressed,
        MouseReleased,
        FocusIn,
        FocusOut,
        PageOpened,
        PageClosed,
        PageShow,
        PageHide,
        End
    };

    inline explicit PDFAnnotationAdditionalActions() = default;

    /// Returns action for given type. If action is invalid,
    /// or not present, nullptr is returned.
    /// \param action Action type
    const PDFAction* getAction(Action action) const { return m_actions.at(action).get(); }

    /// Parses annotation additional actions from the object. If object is invalid, then
    /// empty additional actions is constructed.
    /// \param storage Object storage
    /// \param object Additional actions object
    static PDFAnnotationAdditionalActions parse(const PDFObjectStorage* storage, PDFObject object);

private:
    std::array<PDFActionPtr, End> m_actions;
};

/// Annotation default appearance
class PDFAnnotationDefaultAppearance
{
public:
    explicit inline PDFAnnotationDefaultAppearance() = default;

    /// Parses appearance string. If error occurs, then default appearance
    /// string is constructed.
    static PDFAnnotationDefaultAppearance parse(const QByteArray& string);

    const QByteArray& getFontName() const { return m_fontName; }
    PDFReal getFontSize() const { return m_fontSize; }
    QColor getFontColor() const { return m_fontColor; }

private:
    QByteArray m_fontName;
    PDFReal m_fontSize = 8.0;
    QColor m_fontColor = Qt::black;
};

class PDFAnnotation;
class PDFMarkupAnnotation;
class PDFTextAnnotation;

using PDFAnnotationPtr = QSharedPointer<PDFAnnotation>;

struct AnnotationDrawParameters
{
    /// Painter, onto which is annotation graphics drawn
    QPainter* painter = nullptr;

    /// Output parameter. Marks annotation's graphics bounding
    /// rectangle (it can be different/adjusted from original
    /// annotation bounding rectangle, in that case, it must be adjusted).
    /// If this rectangle is invalid, then appearance content stream
    /// is assumed to be empty.
    QRectF boundingRectangle;

    /// Appeareance mode (normal/rollover/down, and appearance state)
    PDFAppeareanceStreams::Key key;
};

/// Base class for all annotation types. Represents PDF annotation object.
/// Annotations are various enhancements to pages graphical representation,
/// such as graphics, text, highlight or multimedia content, such as sounds,
/// videos and 3D annotations.
class PDFAnnotation
{
public:
    explicit PDFAnnotation();
    virtual ~PDFAnnotation() = default;

    enum Flag : uint
    {
        None            = 0x0000,
        Invisible       = 0x0001, ///< If set, do not display unknown annotations using their AP dictionary
        Hidden          = 0x0002, ///< If set, do not display annotation and do not show popup windows (completely hidden)
        Print           = 0x0004, ///< If set, print annotation
        NoZoom          = 0x0008, ///< Do not apply page zoom while displaying annotation rectangle
        NoRotate        = 0x0010, ///< Do not rotate annotation's appearance to match orientation of the page
        NoView          = 0x0020, ///< Do not display annotation on the screen (it still can be printed)
        ReadOnly        = 0x0040, ///< Do not allow interacting with the user (and disallow also mouse interaction)
        Locked          = 0x0080, ///< Do not allow to delete/modify annotation by user
        ToggleNoView    = 0x0100, ///< If set, invert the interpretation of NoView flag
        LockedContents  = 0x0200, ///< Do not allow to modify contents of the annotation
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    virtual AnnotationType getType() const = 0;

    virtual PDFMarkupAnnotation* asMarkupAnnotation() { return nullptr; }
    virtual const PDFMarkupAnnotation* asMarkupAnnotation() const { return nullptr; }

    /// Draws the annotation using parameters. Annotation is drawn onto painter,
    /// but actual graphics can be drawn outside of annotation's rectangle.
    /// In that case, adjusted annotation's rectangle is passed to the parameters.
    /// Painter must use annotation's coordinate system (for example, line points
    /// must match in both in painter and this annotation).
    /// \param parameters Graphics parameters
    virtual void draw(AnnotationDrawParameters& parameters) const;

    /// Returns a list of appearance states, which must be created for this annotation
    virtual std::vector<PDFAppeareanceStreams::Key> getDrawKeys() const;

    const QRectF& getRectangle() const { return m_rectangle; }
    const QString& getContents() const { return m_contents; }
    PDFObjectReference getPageReference() const { return m_pageReference; }
    const QString& getName() const { return m_name; }
    const QDateTime& getLastModifiedDateTime() const { return m_lastModified; }
    const QString& getLastModifiedString() const { return m_lastModifiedString; }
    Flags getFlags() const { return m_flags; }
    const PDFAppeareanceStreams& getAppearanceStreams() const { return m_appearanceStreams; }
    const QByteArray& getAppearanceState() const { return m_appearanceState; }
    const PDFAnnotationBorder& getBorder() const { return m_annotationBorder; }
    const std::vector<PDFReal>& getColor() const { return m_color; }
    PDFInteger getStructuralParent() const { return m_structParent; }
    PDFObjectReference getOptionalContent() const { return m_optionalContentReference; }

    /// Parses annotation from the object. If error occurs, then nullptr is returned.
    /// \param storage Object storage
    /// \param object Annotation object
    static PDFAnnotationPtr parse(const PDFObjectStorage* storage, PDFObject object);

    /// Parses quadrilaterals and fills them in the painter path. If no quadrilaterals are defined,
    /// then annotation rectangle is used. If annotation rectangle is also invalid,
    /// then empty painter path is used.
    /// \param storage Object storage
    /// \param quadrilateralsObject Object with quadrilaterals definition
    /// \param annotationRect Annotation rectangle
    static PDFAnnotationQuadrilaterals parseQuadrilaterals(const PDFObjectStorage* storage, PDFObject quadrilateralsObject, const QRectF annotationRect);

    /// Converts name to line ending. If appropriate line ending for name is not found,
    /// then None line ending is returned.
    /// \param name Name of the line ending
    static AnnotationLineEnding convertNameToLineEnding(const QByteArray& name);

    /// Returns draw color from defined annotation color
    static QColor getDrawColorFromAnnotationColor(const std::vector<PDFReal>& color);

protected:
    virtual QColor getStrokeColor() const;
    virtual QColor getFillColor() const;

    struct LineGeometryInfo
    {
        /// Original line
        QLineF originalLine;

        /// Transformed line
        QLineF transformedLine;

        /// Matrix LCStoGCS is local coordinate system of line originalLine. It transforms
        /// points on the line to the global coordinate system. So, point (0, 0) will
        /// map onto p1 and point (originalLine.length(), 0) will map onto p2.
        QMatrix LCStoGCS;

        /// Inverted matrix of LCStoGCS. It maps global coordinate system to local
        /// coordinate system of the original line.
        QMatrix GCStoLCS;

        static LineGeometryInfo create(QLineF line);
    };

    /// Returns pen from border settings and annotation color
    QPen getPen() const;

    /// Returns brush from interior color. If annotation doesn't have
    /// a brush, then empty brush is returned.
    QBrush getBrush() const;

    /// Draw line using given parameters and painter. Line is specified
    /// by its geometry information. Painter must be set to global coordinates.
    /// Bounding path is also updated, it is specified in global coordinates,
    /// not in line local coordinate system. We consider p1 as start point of
    /// the line, and p2 as the end point. The painter must have proper QPen
    /// and QBrush setted, this function uses current pen/brush to paint the line.
    /// \param info Line geometry info
    /// \param painter Painter
    /// \param lineEndingSize Line ending size
    /// \param p1Ending Line endpoint graphics at p1
    /// \param p2Ending Line endpoint graphics at p2
    /// \param boundingPath Bounding path in global coordinate system
    /// \param textOffset Additional text offset
    /// \param text Text, which should be printed along the line
    /// \param textIsAboveLine Text should be printed above line
    void drawLine(const LineGeometryInfo& info,
                  QPainter& painter,
                  PDFReal lineEndingSize,
                  AnnotationLineEnding p1Ending,
                  AnnotationLineEnding p2Ending,
                  QPainterPath& boundingPath,
                  QPointF textOffset,
                  const QString& text,
                  bool textIsAboveLine) const;

private:

    QRectF m_rectangle; ///< Annotation rectangle, in page coordinates, "Rect" entry
    QString m_contents; ///< Text to be displayed to the user (or alternate text), "Content" entry
    PDFObjectReference m_pageReference; ///< Reference to annotation's page, "P" entry
    QString m_name; ///< Unique name (in page context) for the annotation, "NM" entry
    QDateTime m_lastModified; ///< Date and time, when annotation was last modified, "M" entry
    QString m_lastModifiedString; ///< Date and time, in text format
    Flags m_flags; ///< Annotation flags
    PDFAppeareanceStreams m_appearanceStreams; ///< Appearance streams, "AP" entry
    QByteArray m_appearanceState; ///< Appearance state, "AS" entry
    PDFAnnotationBorder m_annotationBorder; ///< Annotation border, "Border" entry
    std::vector<PDFReal> m_color; ///< Color (for example, title bar of popup window), "C" entry
    PDFInteger m_structParent; ///< Structural parent identifier, "StructParent" entry
    PDFObjectReference m_optionalContentReference; ///< Reference to optional content, "OC" entry
};

/// Markup annotation object, used to mark up contents of PDF documents. Markup annotations
/// can have various types, as free text (just text displayed on page), annotations with popup
/// windows, and special annotations, such as multimedia annotations.
class PDFMarkupAnnotation : public PDFAnnotation
{
public:
    explicit inline PDFMarkupAnnotation() = default;

    virtual PDFMarkupAnnotation* asMarkupAnnotation() override { return this; }
    virtual const PDFMarkupAnnotation* asMarkupAnnotation() const override { return this; }

    enum class ReplyType
    {
        Reply,
        Group
    };

    const QString& getWindowTitle() const { return m_windowTitle; }
    PDFObjectReference getPopupAnnotation() const { return m_popupAnnotation; }
    PDFReal getOpacity() const { return m_opacity; }
    const QString& getRichTextString() const { return m_richTextString; }
    const QDateTime& getCreationDate() const { return m_creationDate; }
    PDFObjectReference getInReplyTo() const { return m_inReplyTo; }
    const QString& getSubject() const { return m_subject; }
    ReplyType getReplyType() const { return m_replyType; }
    const QByteArray& getIntent() const { return m_intent; }
    const PDFObject& getExternalData() const { return m_externalData; }

protected:
    virtual QColor getStrokeColor() const override;
    virtual QColor getFillColor() const override;

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QString m_windowTitle;
    PDFObjectReference m_popupAnnotation;
    PDFReal m_opacity = 1.0;
    QString m_richTextString;
    QDateTime m_creationDate;
    PDFObjectReference m_inReplyTo;
    QString m_subject;
    ReplyType m_replyType = ReplyType::Reply;
    QByteArray m_intent;
    PDFObject m_externalData;
};

enum class TextAnnotationIcon
{
    Comment,
    Help,
    Insert,
    Key,
    NewParagraph,
    Note,
    Paragraph
};

/// Text annotation represents note attached to a specific point in the PDF
/// document. It appears as icon, and it is not zoomed, or rotated (behaves
/// as if flag NoZoom and NoRotate were set). When this annotation is opened,
/// it displays popup window containing the text of the note, font and size
/// is implementation dependent by viewer application.
class PDFTextAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFTextAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Text; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    bool isOpen() const { return m_open; }
    const QByteArray& getIconName() const { return m_iconName; }
    const QString& getState() const { return m_state; }
    const QString& getStateModel() const { return m_stateModel; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    bool m_open = false;
    QByteArray m_iconName;
    QString m_state;
    QString m_stateModel;
};

enum class LinkHighlightMode
{
    None,
    Invert,
    Outline,
    Push
};

/// Link annotation represents hypertext link to a destination to elsewhere
/// in the document, or action to be performed.
class PDFLinkAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFLinkAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Link; }
    virtual std::vector<PDFAppeareanceStreams::Key> getDrawKeys() const;
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    const PDFAction* getAction() const { return m_action.data(); }
    LinkHighlightMode getHighlightMode() const { return m_highlightMode; }
    const PDFAction* getURIAction() const { return m_previousAction.data(); }
    const PDFAnnotationQuadrilaterals& getActivationRegion() const { return m_activationRegion; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    PDFActionPtr m_action;
    LinkHighlightMode m_highlightMode = LinkHighlightMode::Invert;
    PDFActionPtr m_previousAction;
    PDFAnnotationQuadrilaterals m_activationRegion;
};

/// Free text annotation displays text directly on the page. Free text doesn't have
/// open/close state, text is always visible.
class PDFFreeTextAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFFreeTextAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::FreeText; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    enum class Justification
    {
        Left,
        Centered,
        Right
    };

    enum class Intent
    {
        None,
        Callout,
        TypeWriter
    };

    const QByteArray& getDefaultAppearance() const { return m_defaultAppearance; }
    Justification getJustification() const { return m_justification; }
    const QString& getDefaultStyle() const { return m_defaultStyleString; }
    const PDFAnnotationCalloutLine& getCalloutLine() const { return m_calloutLine; }
    Intent getIntent() const { return m_intent; }
    const QRectF& getTextRectangle() const { return m_textRectangle; }
    const PDFAnnotationBorderEffect& getBorderEffect() const { return m_effect; }
    AnnotationLineEnding getStartLineEnding() const { return m_startLineEnding; }
    AnnotationLineEnding getEndLineEnding() const { return m_endLineEnding; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QByteArray m_defaultAppearance;
    Justification m_justification = Justification::Left;
    QString m_defaultStyleString;
    PDFAnnotationCalloutLine m_calloutLine;
    Intent m_intent = Intent::None;
    QRectF m_textRectangle;
    PDFAnnotationBorderEffect m_effect;
    AnnotationLineEnding m_startLineEnding = AnnotationLineEnding::None;
    AnnotationLineEnding m_endLineEnding = AnnotationLineEnding::None;
};

/// Line annotation, draws straight line on the page (in most simple form), or
/// it can display, for example, dimensions with perpendicular lines at the line
/// endings. Caption text can also be displayed.
class PDFLineAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFLineAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Line; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    enum class Intent
    {
        Arrow,
        Dimension
    };

    enum class CaptionPosition
    {
        Inline,
        Top
    };

    const QLineF& getLine() const { return m_line; }
    AnnotationLineEnding getStartLineEnding() const { return m_startLineEnding; }
    AnnotationLineEnding getEndLineEnding() const { return m_endLineEnding; }
    const std::vector<PDFReal>& getInteriorColor() const { return m_interiorColor; }
    PDFReal getLeaderLineLength() const { return m_leaderLineLength; }
    PDFReal getLeaderLineExtension() const { return m_leaderLineExtension; }
    PDFReal getLeaderLineOffset() const { return m_leaderLineOffset; }
    bool isCaptionRendered() const { return m_captionRendered; }
    Intent getIntent() const { return m_intent; }
    CaptionPosition getCaptionPosition() const { return m_captionPosition; }
    const PDFObject& getMeasureDictionary() const { return m_measureDictionary; }
    const QPointF& getCaptionOffset() const { return m_captionOffset; }

protected:
    virtual QColor getFillColor() const override;

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QLineF m_line;
    AnnotationLineEnding m_startLineEnding = AnnotationLineEnding::None;
    AnnotationLineEnding m_endLineEnding = AnnotationLineEnding::None;
    std::vector<PDFReal> m_interiorColor;
    PDFReal m_leaderLineLength = 0.0;
    PDFReal m_leaderLineExtension = 0.0;
    PDFReal m_leaderLineOffset = 0.0;
    bool m_captionRendered = false;
    Intent m_intent = Intent::Arrow;
    CaptionPosition m_captionPosition = CaptionPosition::Inline;
    PDFObject m_measureDictionary;
    QPointF m_captionOffset;
};

/// Simple geometry annotation.
/// Square and circle annotations displays rectangle or ellipse on the page.
/// Name is a bit strange (because rectangle may not be a square or circle is not ellipse),
/// but it is defined in PDF specification, so we will use these terms.
class PDFSimpleGeometryAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFSimpleGeometryAnnotation(AnnotationType type) :
        m_type(type)
    {

    }

    virtual AnnotationType getType() const override { return m_type; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    const std::vector<PDFReal>& getInteriorColor() const { return m_interiorColor; }
    const PDFAnnotationBorderEffect& getBorderEffect() const { return m_effect; }
    const QRectF& getGeometryRectangle() const { return m_geometryRectangle; }

protected:
    virtual QColor getFillColor() const override;

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    AnnotationType m_type;
    std::vector<PDFReal> m_interiorColor;
    PDFAnnotationBorderEffect m_effect;
    QRectF m_geometryRectangle;
};

/// Polygonal geometry, consists of polygon or polyline geometry. Polygon annotation
/// displays closed polygon (potentially filled), polyline annotation displays
/// polyline, which is not closed.
class PDFPolygonalGeometryAnnotation : public PDFMarkupAnnotation
{
public:
    enum class Intent
    {
        None,
        Cloud,
        Dimension
    };

    inline explicit PDFPolygonalGeometryAnnotation(AnnotationType type) :
        m_type(type),
        m_intent(Intent::None)
    {

    }

    virtual AnnotationType getType() const override { return m_type; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    const std::vector<QPointF>& getVertices() const { return m_vertices; }
    AnnotationLineEnding getStartLineEnding() const { return m_startLineEnding; }
    AnnotationLineEnding getEndLineEnding() const { return m_endLineEnding; }
    const std::vector<PDFReal>& getInteriorColor() const { return m_interiorColor; }
    const PDFAnnotationBorderEffect& getBorderEffect() const { return m_effect; }
    Intent getIntent() const { return m_intent; }
    const PDFObject& getMeasure() const { return m_measure; }

protected:
    virtual QColor getFillColor() const override;

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    AnnotationType m_type;
    std::vector<QPointF> m_vertices;
    AnnotationLineEnding m_startLineEnding = AnnotationLineEnding::None;
    AnnotationLineEnding m_endLineEnding = AnnotationLineEnding::None;
    std::vector<PDFReal> m_interiorColor;
    PDFAnnotationBorderEffect m_effect;
    Intent m_intent;
    PDFObject m_measure;
};

/// Annotation for text highlighting. Can highlight, underline, strikeout,
/// or squiggly underline the text.
class PDFHighlightAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFHighlightAnnotation(AnnotationType type) :
        m_type(type)
    {

    }

    virtual AnnotationType getType() const override { return m_type; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    const PDFAnnotationQuadrilaterals& getHiglightArea() const { return m_highlightArea; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    AnnotationType m_type;
    PDFAnnotationQuadrilaterals m_highlightArea;
};

/// Annotation for visual symbol that indicates presence of text edits.
class PDFCaretAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFCaretAnnotation() = default;

    enum class Symbol
    {
        None,
        Paragraph
    };

    virtual AnnotationType getType() const override { return AnnotationType::Caret; }

    const QRectF& getCaretRectangle() const { return m_caretRectangle; }
    Symbol getSymbol() const { return m_symbol; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QRectF m_caretRectangle;
    Symbol m_symbol = Symbol::None;
};

/// Annotation for stamps. Displays text or graphics intended to look
/// as if they were stamped on the paper.
class PDFStampAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFStampAnnotation() = default;

    enum class Stamp
    {
        Approved,
        AsIs,
        Confidential,
        Departmental,
        Draft,
        Experimental,
        Expired,
        Final,
        ForComment,
        ForPublicRelease,
        NotApproved,
        NotForPublicRelease,
        Sold,
        TopSecret
    };

    virtual AnnotationType getType() const override { return AnnotationType::Stamp; }

    Stamp getStamp() const { return m_stamp; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    Stamp m_stamp = Stamp::Draft;
};

/// Ink annotation. Represents a path composed of disjoint polygons.
class PDFInkAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFInkAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Ink; }

    const QPainterPath& getInkPath() const { return m_inkPath; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QPainterPath m_inkPath;
};

/// Popup annotation. Displays text in popup window for markup annotations.
/// This annotation contains field to associated annotation, for which
/// is window displayed, and window state (open/closed).
class PDFPopupAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFPopupAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Popup; }

    bool isOpened() const { return m_opened; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    bool m_opened = false;
};

/// File attachment annotation contains reference to (embedded or external) file.
/// So it is a link to specified file. Activating annotation enables user to view
/// or store attached file in the filesystem.
class PDFFileAttachmentAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFFileAttachmentAnnotation() = default;

    enum class Icon
    {
        Graph,
        Paperclip,
        PushPin,
        Tag
    };

    virtual AnnotationType getType() const override { return AnnotationType::FileAttachment; }

    const PDFFileSpecification& getFileSpecification() const { return m_fileSpecification; }
    Icon getIcon() const { return m_icon; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    PDFFileSpecification m_fileSpecification;
    Icon m_icon = Icon::PushPin;
};

/// Sound annotation contains sound, which is played, when
/// annotation is activated.
class PDFSoundAnnotation : public PDFMarkupAnnotation
{
public:
    inline explicit PDFSoundAnnotation() = default;

    enum class Icon
    {
        Speaker,
        Microphone
    };

    virtual AnnotationType getType() const override { return AnnotationType::Sound; }

    const PDFSound& getSound() const { return m_sound; }
    Icon getIcon() const { return m_icon; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    PDFSound m_sound;
    Icon m_icon = Icon::Speaker;
};

/// Movie annotation contains movie or sound, which is played, when
/// annotation is activated.
class PDFMovieAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFMovieAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Movie; }

    const QString& getMovieTitle() const { return m_movieTitle; }
    bool isMovieToBePlayed() const { return m_playMovie; }
    const PDFMovie& getMovie() const { return m_movie; }
    const PDFMovieActivation& getMovieActivation() const { return m_movieActivation; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QString m_movieTitle;
    bool m_playMovie = true;
    PDFMovie m_movie;
    PDFMovieActivation m_movieActivation;
};

/// Screen action represents area of page in which is media played.
/// See also Rendition actions and their relationship to this annotation.
class PDFScreenAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFScreenAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Screen; }

    const QString& getScreenTitle() const { return m_screenTitle; }
    const PDFAnnotationAppearanceCharacteristics& getAppearanceCharacteristics() const { return m_appearanceCharacteristics; }
    const PDFAction* getAction() const { return m_action.get(); }
    const PDFAnnotationAdditionalActions& getAdditionalActions() const { return m_additionalActions; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QString m_screenTitle;
    PDFAnnotationAppearanceCharacteristics m_appearanceCharacteristics;
    PDFActionPtr m_action;
    PDFAnnotationAdditionalActions m_additionalActions;
};

/// Widget annotation represents form fileds for interactive forms.
/// Annotation's dictionary is merged with form field dictionary,
/// it can be done, because dictionaries doesn't overlap.
class PDFWidgetAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFWidgetAnnotation() = default;

    enum class HighlightMode
    {
        None,
        Invert,
        Outline,
        Push,
        Toggle
    };

    virtual AnnotationType getType() const override { return AnnotationType::Widget; }

    HighlightMode getHighlightMode() const { return m_highlightMode; }
    const PDFAnnotationAppearanceCharacteristics& getAppearanceCharacteristics() const { return m_appearanceCharacteristics; }
    const PDFAction* getAction() const { return m_action.get(); }
    const PDFAnnotationAdditionalActions& getAdditionalActions() const { return m_additionalActions; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    HighlightMode m_highlightMode = HighlightMode::Invert;
    PDFAnnotationAppearanceCharacteristics m_appearanceCharacteristics;
    PDFActionPtr m_action;
    PDFAnnotationAdditionalActions m_additionalActions;
};

/// Printer mark annotation represents graphics symbol, mark, or other
/// graphic feature to assist printing production.
class PDFPrinterMarkAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFPrinterMarkAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::PrinterMark; }
};

/// Trapping characteristics for the page
class PDFTrapNetworkAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFTrapNetworkAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::TrapNet; }
};

/// Watermark annotation represents watermark displayed on the page,
/// for example, if it is printed. Watermarks are displayed at fixed
/// position and size on the page.
class PDFWatermarkAnnotation : public PDFAnnotation
{
public:
    inline explicit PDFWatermarkAnnotation() = default;

    virtual AnnotationType getType() const override { return AnnotationType::Watermark; }

    const QMatrix& getMatrix() const { return m_matrix; }
    PDFReal getRelativeHorizontalOffset() const { return m_relativeHorizontalOffset; }
    PDFReal getRelativeVerticalOffset() const { return m_relativeVerticalOffset; }

private:
    friend static PDFAnnotationPtr PDFAnnotation::parse(const PDFObjectStorage* storage, PDFObject object);

    QMatrix m_matrix;
    PDFReal m_relativeHorizontalOffset = 0.0;
    PDFReal m_relativeVerticalOffset = 0.0;
};

/// Annotation manager manages annotations for document's pages. Each page
/// can have multiple annotations, and this object caches them. Also,
/// this object builds annotation's appearance streams, if necessary.
/// This object is not thread safe, functions can't be called from another
/// thread.
class PDFFORQTLIBSHARED_EXPORT PDFAnnotationManager : public QObject, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:

    enum class Target
    {
        View,
        Print
    };

    explicit PDFAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFAnnotationManager() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix) const override;

    void setDocument(const PDFDocument* document);

    Target getTarget() const;
    void setTarget(Target target);

private:
    struct PageAnnotation
    {
        PDFAppeareanceStreams::Appearance appearance = PDFAppeareanceStreams::Appearance::Normal;
        PDFCachedItem<PDFObject> appearanceStream;
        PDFAnnotationPtr annotation;
    };

    struct PageAnnotations
    {
        bool isEmpty() const { return annotations.empty(); }

        std::vector<PageAnnotation> annotations;
    };

    /// Returns current appearance stream for given page annotation
    /// \param pageAnnotation Page annotation
    PDFObject getAppearanceStream(PageAnnotation& pageAnnotation) const;

    /// Returns reference to page annotation for given page index.
    /// This function requires, that pointer to m_document is valid.
    /// \param pageIndex Page index (must point to valid page)
    PageAnnotations& getPageAnnotations(PDFInteger pageIndex) const;

    const PDFDocument* m_document;
    PDFDrawWidgetProxy* m_proxy;
    mutable std::map<PDFInteger, PageAnnotations> m_pageAnnotations;
    Target m_target = Target::View;
};

}   // namespace pdf

#endif // PDFANNOTATION_H
