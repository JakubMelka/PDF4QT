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

#ifndef PDFWIDGETANNOTATION_H
#define PDFWIDGETANNOTATION_H

#include "pdfwidgetsglobal.h"
#include "pdfannotation.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfsnapper.h"
#include "pdfannotationmanipulator.h"

#include <QPixmap>
#include <QUuid>

#include <map>
#include <array>
#include <optional>
#include <functional>

class QMimeData;

namespace pdf
{

struct PDFWidgetSnapshot;
class PDFDrawWidgetProxy;

/// Annotation manager for GUI rendering, it also manages annotations widgets
/// for parent widget. The manager also implements the selection of annotations
/// and the interactive editing of the selected annotations:
///
///  - a left click selects an annotation, Ctrl/Shift + click adds the annotation
///    to the selection (or removes it from the selection), Ctrl/Shift + drag on
///    an empty area selects all annotations inside the dragged rectangle. Thin
///    shapes (lines, outlines of polygons, ink) are selected by a click near
///    the shape, so they do not block the annotations inside their rectangle;
///    Alt + click selects the next annotation under the cursor,
///  - selected annotations can be dragged to a new position (also to another
///    page, or to another document; holding Ctrl while dropping copies them),
///    resized using the handles of the selection frame and rotated using the
///    rotation handle above the frame. Only the handles, which the selection
///    supports, are displayed (for example, a sticky note can only be moved). If
///    several annotations are selected, then the layout of the group is always
///    transformed - annotations, which do not support the transformation, are
///    moved to the transformed position. Handles have precedence over the
///    modifiers of the selection, so Shift can be held before the handle is pressed,
///  - if a single annotation defined by points is selected (line, polygon,
///    polyline, callout line of a free text), then each point has its own handle
///    and it can be dragged (Shift constrains the direction to multiples of 45
///    degrees, the point snaps to the geometry of the page and of the other
///    annotations, Ctrl disables the snapping); points of polygons and polylines
///    can be inserted and removed using the context menu. Points can be edited
///    also from the keyboard - Alt + Left/Right selects a point, arrows move it,
///    Insert adds a point behind it, Delete removes it,
///  - text markup annotations (highlight, ...) have handles at the ends of each
///    marked line, so a single line can be made longer or shorter; a single marked
///    line (a single stroke of an ink) can be deleted using the context menu,
///  - the frame of a free text annotation with a callout line is the frame of its
///    text box - the text box is moved and resized, the tip of the callout line
///    stays at its place. The whole annotation is moved by dragging its callout
///    line (or with Alt pressed), and by the arrows,
///  - Delete removes the selection, Ctrl+C / Ctrl+X / Ctrl+V copy, cut and paste
///    the selection through the clipboard (also between documents and between
///    running instances of the application), arrows nudge the selection,
///  - the context menu offers rotation by 90 degrees, mirroring and the
///    remaining operations.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetAnnotationManager : public PDFAnnotationManager, public IDrawWidgetInputInterface, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = PDFAnnotationManager;

public:
    explicit PDFWidgetAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFWidgetAnnotationManager() override;

    virtual void setDocument(const PDFModifiedDocument& document) override;
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;

    /// Draws the annotations of the page (both base classes declare this
    /// function, the override resolves the ambiguity)
    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    /// Draws the selection, the selection frame with the handles, the preview
    /// of the manipulated annotations and the rubber band. It is drawn after the
    /// pages are rendered, so it never gets into the page thumbnails.
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    /// Returns tooltip generated from annotation
    virtual QString getTooltip() const override { return m_tooltip; }

    /// Returns current cursor
    virtual const std::optional<QCursor>& getCursor() const override { return m_cursor; }

    virtual int getInputPriority() const override { return AnnotationPriority; }

    /// Shows the context menu for the annotation. If the annotation can be selected,
    /// then it is selected and the menu of the selection is displayed. Annotations, which
    /// cannot be selected on the page (hidden annotations, replies), get a menu with the
    /// operations, which do not need the geometry (properties, deleting) - so a hidden
    /// annotation can be made visible again from the list of the annotations.
    /// \param annotationReference Annotation
    /// \param pageReference Page of the annotation
    /// \param globalMenuPosition Position of the menu in global coordinates
    void showAnnotationMenu(pdf::PDFObjectReference annotationReference,
                            pdf::PDFObjectReference pageReference,
                            QPoint globalMenuPosition);

    bool canAcceptAnnotationDrag(const QMimeData* data) const;
    bool handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action, bool isSnappingEnabled = true);

    /// Updates the feedback of the drag and drop operation - the place, to which the dragged
    /// annotations snap, and the scope of the operation. It is displayed until it is cleared.
    /// \param data Dragged data
    /// \param widgetPos Position of the cursor
    /// \param isSnappingEnabled Snap the dragged annotations to the geometry of the page and of the other annotations
    void updateAnnotationDropFeedback(const QMimeData* data, const QPoint& widgetPos, bool isSnappingEnabled);

    /// Clears the feedback of the drag and drop operation
    void clearAnnotationDropFeedback();

    /// Returns the place, to which the dragged annotations snap (device coordinates),
    /// if the feedback of the drag and drop operation is displayed and they snap
    std::optional<QPointF> getAnnotationDropSnapPoint() const;

    /// Creates the data of the drag and drop operation, which has been prepared by the mouse
    /// press on the selection. Returns nullptr, if no operation is prepared. The caller
    /// is the owner of the data.
    QMimeData* createAnnotationDragData() const;

    /// Returns a text, which tells the user, that the interaction changes just a part of the selection
    /// (the selection can span several pages, handles and dragging work with a single page). Returns
    /// empty text, if the whole selection is changed.
    QString getInteractionScopeText(PDFInteger pageIndex) const;

    /// Returns the annotation, as it will look like, when the running interaction (dragging
    /// of a handle, or of a point) is finished. Returns nullptr, if there is no such preview.
    PDFAnnotationPtr getInteractionPreview(PDFObjectReference annotation) const;

    /// Returns whether a visible, editable annotation can be deleted by the user.
    /// Checks both document permissions and annotation flags.
    bool canDeleteAnnotation(const PageAnnotation& annotation) const;

    /// Returns true, if the annotation can be selected by the user - it must be
    /// of an editable type (links, widgets and popups are excluded), it must not
    /// be a reply to another annotation and it must be visible.
    bool isAnnotationSelectable(const PageAnnotation& annotation) const;

    /// Returns true, if the annotation can be moved, resized, rotated or mirrored
    /// by the user - it must be selectable, its geometry must be supported, it must
    /// not be locked or read only and the document must allow the modification.
    bool canTransformAnnotation(const PageAnnotation& annotation) const;

    /// Returns true, if the user can modify (edit, delete) the annotation - the
    /// document must allow it and the annotation must not be locked or read only.
    /// Unlike \ref canDeleteAnnotation, the annotation does not have to be visible,
    /// so it is used for the annotations managed from a list (hidden annotations, replies).
    bool canModifyAnnotation(const PageAnnotation& annotation) const;

    /// Returns the operations, which the user can do with the annotation
    PDFAnnotationManipulator::Capabilities getAnnotationCapabilities(const PageAnnotation& annotation) const;

    /// Returns the operations, which the user can do with the selection. If several
    /// annotations are selected on a page, then the layout of the group can be
    /// resized, rotated and mirrored regardless of the capabilities of the annotations.
    PDFAnnotationManipulator::Capabilities getSelectionCapabilities() const;

    /// Returns true, if the document permits modification of the annotations
    bool isModificationAllowed() const;

    /// Returns snap information generated from editable annotation geometry on a page.
    /// \param pageIndex Page index
    /// \param excludedAnnotation Annotation, whose geometry is skipped (the annotation being edited)
    /// \param excludeSelection Selected annotations are excluded too
    PDFSnapInfo getSnapInfo(PDFInteger pageIndex, PDFObjectReference excludedAnnotation = PDFObjectReference(), bool excludeSelection = false) const;

    /// Returns references of the selected annotations
    std::vector<PDFObjectReference> getSelectedAnnotations() const;

    /// Returns true, if the annotation is selected
    bool isAnnotationSelected(PDFObjectReference annotation) const;

    /// Returns true, if any annotation is selected
    bool hasSelection() const { return !m_selection.empty(); }

    /// Selects the annotations (annotations, which are not selectable, are ignored)
    /// \param annotations Annotations to be selected
    void setSelectedAnnotations(const std::vector<PDFObjectReference>& annotations);

    /// Adds the annotation to the selection, or replaces the selection with it
    /// \param annotation Annotation
    /// \param exclusive Replace the current selection
    void selectAnnotation(PDFObjectReference annotation, bool exclusive);

    /// Removes the annotation from the selection
    void deselectAnnotation(PDFObjectReference annotation);

    /// Clears the selection
    void clearSelection();

    /// Selects all selectable annotations on the pages, which are currently displayed
    void selectAllAnnotations();

    /// Copies the selected annotations to the clipboard
    void copySelectedAnnotations();

    /// Copies the selected annotations to the clipboard and deletes them
    void cutSelectedAnnotations();

    /// Returns true, if the clipboard contains annotations, which can be pasted
    bool canPasteAnnotations() const;

    /// Pastes annotations from the clipboard. If the widget position is over
    /// a page, then annotations are centered at it, otherwise they are pasted
    /// onto the first displayed page at their original position.
    /// \param widgetPosition Position in the widget (optional)
    /// \param keepPosition Paste the annotations at their original position (the widget position selects just the page)
    void pasteAnnotations(std::optional<QPoint> widgetPosition, bool keepPosition = false);

    /// Copies the selected annotations (with their popup windows and replies)
    /// onto the pages. An annotation is not copied onto its own page.
    /// \param pageIndices Indices of the target pages
    void copySelectedAnnotationsToPages(const std::vector<PDFInteger>& pageIndices);

    /// Deletes the selected annotations, which can be deleted
    void deleteSelectedAnnotations();

    /// Moves the selected annotations by an offset given in the page coordinates
    /// \param offset Offset
    void translateSelectedAnnotations(const QPointF& offset);

    /// Moves the selected annotations in a direction given on the screen. The
    /// direction is converted to the page coordinates for each page separately,
    /// because the pages can be rotated differently.
    /// \param deviceDirection Direction on the screen (y axis points downwards)
    /// \param distance Distance in the page units
    void nudgeSelectedAnnotations(const QPointF& deviceDirection, PDFReal distance);

    /// Rotates the selected annotations of each page around the center of
    /// their bounding rectangle. Positive angle rotates clockwise, as seen
    /// by the user on the screen.
    /// \param angleDegrees Angle in degrees
    void rotateSelectedAnnotations(qreal angleDegrees);

    /// Mirrors the selected annotations of each page around the center of
    /// their bounding rectangle. Horizontal orientation mirrors left and right
    /// side (as seen by the user on the screen), vertical top and bottom.
    /// \param orientation Orientation
    void flipSelectedAnnotations(Qt::Orientation orientation);

    /// Alignment of the selected annotations (as the user sees it on the screen)
    enum class Alignment
    {
        Left,
        HorizontalCenter,
        Right,
        Top,
        VerticalCenter,
        Bottom
    };

    /// Aligns the selected annotations of each page to the frame of the selection
    /// \param alignment Alignment
    void alignSelectedAnnotations(Alignment alignment);

    /// Distributes the selected annotations of each page, so the gaps between
    /// them are the same (at least three annotations are needed)
    /// \param orientation Orientation (on the screen)
    void distributeSelectedAnnotations(Qt::Orientation orientation);

    /// Sets the rectangle of the annotation (position and size). The geometry of
    /// the annotation (its points) is transformed together with the rectangle. If the
    /// annotation cannot be resized, then it is just moved to the center of the rectangle.
    /// \param annotation Annotation
    /// \param rectangle New rectangle (page coordinates)
    /// \returns true, if the annotation has been modified
    bool setAnnotationRectangle(PDFObjectReference annotation, const QRectF& rectangle);

    /// Sets the text box of a free text annotation, the tip of its callout line stays
    /// \returns true, if the annotation has been modified
    bool setAnnotationTextRectangle(PDFObjectReference annotation, const QRectF& textRectangle);

    /// Adds a callout line to a free text annotation. The line ends
    /// at the edge of the text box, which is the nearest one to the tip.
    /// \param annotation Annotation
    /// \param tip Place, to which the callout line points (page coordinates)
    /// \returns true, if the annotation has been modified
    bool addAnnotationCalloutLine(PDFObjectReference annotation, const QPointF& tip);

    /// Removes the callout line of a free text annotation
    /// \returns true, if the annotation has been modified
    bool removeAnnotationCalloutLine(PDFObjectReference annotation);

    /// Removes a part of the annotation (a marked region of a text markup, a stroke of an ink)
    /// \returns true, if the annotation has been modified
    bool removeAnnotationPart(PDFObjectReference annotation, size_t index);

    /// Adds parts to the annotation (marked areas of a text markup, strokes of an ink), see PDFAnnotationManipulator::addPart
    /// \returns true, if the annotation has been modified
    bool addAnnotationParts(PDFObjectReference annotation, const std::vector<QPolygonF>& shapes);

    /// Replaces the parts of the annotation, see PDFAnnotationManipulator::setParts
    /// \returns true, if the annotation has been modified
    bool setAnnotationParts(PDFObjectReference annotation, const std::vector<QPolygonF>& shapes);

    /// Erases the parts of the strokes of an ink, which are in the circle (page coordinates)
    /// \returns true, if the annotation has been modified
    bool eraseAnnotationInk(PDFObjectReference annotation, const QPointF& center, PDFReal radius);

    /// Adds a reply to the markup annotation (the author is the author from the settings). The
    /// reply is displayed in the popup window of the annotation, where the user can write it.
    /// \returns true, if the reply has been added
    bool addAnnotationReply(PDFObjectReference annotation, const QString& contents);

    /// Replaces the file attached by a file attachment annotation
    /// \returns true, if the annotation has been modified
    bool setAnnotationFileAttachment(PDFObjectReference annotation, const QString& fileName, const QByteArray& data);

    /// Edit of the parts of the selected annotation, which is done by the next dragging of the mouse
    enum class PartEdit
    {
        None,
        AddStroke,          ///< A new stroke of an ink is drawn
        AddMarkedAreas,     ///< Another text (or an area, if there is no text) is marked
        ReplaceMarkedAreas  ///< Another text (or an area) is marked instead of the text, which is marked now
    };

    /// Starts the edit of the parts of the selected annotation. The next dragging of the mouse
    /// on the page of the annotation does the edit (Escape cancels it).
    /// \returns true, if a single annotation, which supports the edit, is selected
    bool beginPartEdit(PartEdit partEdit);

    /// Returns the edit of the parts, which waits for the dragging of the mouse
    PartEdit getPartEdit() const { return m_partEdit; }

    /// Returns the areas marked by the dragging of the mouse from the start to the end (page
    /// coordinates) - the lines of the text between them, or the rectangle, if there is no text
    std::vector<QPolygonF> getMarkedShapes(PDFInteger pageIndex, const QPointF& start, const QPointF& end) const;

    /// Sets the color of the selected annotations
    void setSelectedAnnotationsColor(const QColor& color);

    /// Sets the opacity of the selected annotations
    /// \param opacity Opacity (from 0 to 1)
    void setSelectedAnnotationsOpacity(PDFReal opacity);

    /// Sets the width of the border (line) of the selected annotations, which have a border
    void setSelectedAnnotationsBorderWidth(PDFReal width);

    /// Returns the index of the point of the selected annotation,
    /// which is edited from the keyboard, or -1
    int getActivePoint() const { return m_activePoint; }

    /// Moves a single point of the annotation (end point of a line, vertex of
    /// a polygon or of a polyline, point of the callout line of a free text).
    /// \param annotation Annotation
    /// \param pointIndex Index of the point
    /// \param pagePoint New position of the point in the page coordinates
    /// \returns true, if the annotation has been modified
    bool moveAnnotationPoint(PDFObjectReference annotation, size_t pointIndex, const QPointF& pagePoint);

    /// Inserts a new point into a polygon or a polyline. The point is inserted
    /// into the segment, which starts at the point with the given index.
    /// \param annotation Annotation
    /// \param segmentIndex Index of the segment (of its first point)
    /// \param pagePoint Position of the new point in the page coordinates
    /// \returns true, if the annotation has been modified
    bool insertAnnotationPoint(PDFObjectReference annotation, size_t segmentIndex, const QPointF& pagePoint);

    /// Removes a point from a polygon or a polyline. The point is not removed,
    /// if the shape would degenerate (polygon needs three points, polyline two).
    /// \param annotation Annotation
    /// \param pointIndex Index of the point
    /// \returns true, if the annotation has been modified
    bool removeAnnotationPoint(PDFObjectReference annotation, size_t pointIndex);

signals:
    void actionTriggered(const PDFAction* action);
    void documentModified(PDFModifiedDocument document);
    void selectionChanged();

private:
    /// Selected annotation
    struct SelectedAnnotation
    {
        PDFObjectReference annotation;
        PDFInteger pageIndex = -1;
    };

    /// Handles of the selection frame. Corner and edge handles are named by
    /// their position on the screen (not in the page coordinate system).
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
        Rotate
    };

    /// Layout of the selection frame in the device coordinates
    struct HandleLayout
    {
        bool isValid = false;
        QRectF frame;                       ///< Selection frame (bounding rectangle of the selection)
        std::array<QPointF, 8> handles;     ///< Resize handles, in the order of the enum Handle (TopLeft ... Left)
        bool isTextBox = false;             ///< Frame of the text box of a free text annotation with a callout line
        bool hasResizeHandles = false;      ///< Selection can be resized
        bool hasRotationHandle = false;     ///< Selection can be rotated
        bool isRotationArbitrary = false;   ///< Selection can be rotated by any angle (otherwise in steps of 90 degrees)
        QPointF rotationHandle;             ///< Center of the rotation handle
        QPointF rotationHandleBase;         ///< Point on the frame, from which the line to the rotation handle is drawn
        qreal handleSize = 0.0;             ///< Size of the resize handle
        qreal rotationHandleRadius = 0.0;   ///< Radius of the rotation handle
    };

    /// Interaction, which is currently performed by the mouse
    enum class Interaction
    {
        None,
        RubberBand,     ///< Selection of annotations by a dragged rectangle
        Handle,         ///< Resizing or rotation using a handle of the selection frame
        Point,          ///< Dragging of a single point of the selected annotation
        Part            ///< Drawing of a new part of the selected annotation (see PartEdit)
    };

    struct InteractionState
    {
        Interaction type = Interaction::None;
        PDFInteger pageIndex = -1;
        QTransform pageToDevice;        ///< Matrix, by which the manipulated geometry is displayed (see getSelectionToDeviceMatrix)
        QTransform deviceToPage;
        QTransform pageToDeviceBase;    ///< Page to device matrix of the page
        QPoint startDevicePosition;
        QPoint currentDevicePosition;
        Handle handle = Handle::None;
        HandleLayout layout;
        QTransform previewTransform;    ///< Transformation of the manipulated annotations (page coordinates)
        qreal previewAngle = 0.0;       ///< Rotation angle of the preview (degrees, clockwise on screen)
        bool isAdditive = false;        ///< Rubber band adds annotations to the selection
        int pointIndex = -1;            ///< Index of the dragged point
        PDFObjectReference pointAnnotation; ///< Annotation, whose point is dragged
        std::vector<QPointF> previewPoints; ///< Points of the annotation with the dragged point (page coordinates)
        bool isPreviewClosed = false;   ///< Preview points form a closed shape
        bool isPreviewQuadEnds = false; ///< Preview points are the ends of the marked regions
        std::vector<size_t> previewStrokeSizes; ///< Preview points are the points of the strokes of an ink
        std::vector<QPointF> partPoints;    ///< Points of the drawn part (page coordinates) - the stroke, or the start and the end of the marked text
        std::vector<QPolygonF> partShapes;  ///< Shapes of the drawn parts (page coordinates)
        bool isSnapped = false;         ///< Dragged point (handle) is snapped
        QPointF snappedDevicePoint;     ///< Place, to which the dragged handle is snapped (device coordinates)
        QRectF textRectangle;           ///< Text box, which is manipulated by the handles (page coordinates)
        std::map<PDFObjectReference, PDFAnnotationPtr> previewAnnotations; ///< Annotations, as they will look like, when the interaction is finished
    };

    /// Feedback of the drag and drop operation
    struct DropFeedback
    {
        bool isActive = false;
        PDFInteger pageIndex = -1;
        QPoint devicePosition;
        bool isSnapped = false;
        QPointF snappedDevicePoint;
    };

    /// Text box of the selected free text annotation with a callout line
    struct TextBoxInfo
    {
        PDFInteger pageIndex = -1;
        PDFObjectReference annotation;
        QRectF textRectangle;

        bool isValid() const { return pageIndex != -1; }
    };

    /// Points of the selected annotation, which can be edited one by one
    struct PointEditInfo
    {
        PDFInteger pageIndex = -1;
        PDFObjectReference annotation;
        PDFAnnotationManipulator::EditablePoints points;

        bool isValid() const { return pageIndex != -1 && points.isValid(); }
    };

    /// Point or segment of the selected annotation, for which the context menu is shown
    struct PointMenuContext
    {
        PDFObjectReference annotation;
        int removedPoint = -1;      ///< Point, which can be removed
        int insertSegment = -1;     ///< Segment, into which a point can be inserted
        QPointF insertPosition;     ///< Position of the inserted point (page coordinates)
    };

    /// Annotation, which is under the mouse cursor and which can be selected
    struct HoveredAnnotation
    {
        PDFInteger pageIndex = -1;
        PDFObjectReference annotation;
    };

    struct DragState
    {
        bool isActive = false;
        bool isDragging = false;
        bool isCopy = false;
        QPoint startDevicePos;
        PDFInteger pageIndex = -1;
        QTransform pageToDevice;
        QTransform deviceToPage;
        std::vector<PDFObjectReference> annotations;
        std::vector<QRectF> rectangles;
        PDFObjectReference pageReference;
        QRectF boundingRectangle;
        QPointF cursorOffset;
        QPixmap dragPixmap;
        QPoint dragHotSpot;
        bool isTextBoxOnly = false;     ///< Only the text box of a free text annotation is dragged (the tip of its callout line stays)
    };

    void updateFromMouseEvent(QMouseEvent* event);
    bool beginAnnotationDrag(QMouseEvent* event, PDFInteger pageIndex);
    void startAnnotationDrag(QMouseEvent* event);
    QPixmap createAnnotationDragPixmap(const QTransform& pagePointToDevicePointMatrix, QPoint& hotSpot) const;
    const PDFAction* getLinkActionAtPosition(QPoint widgetPos) const;

    /// Returns page annotation under the widget position, which can be selected.
    /// If several annotations are under the position, then the topmost is returned.
    const PageAnnotation* findSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const;

    /// Returns all page annotations under the widget position, which can be selected.
    /// Annotations, whose shape is under the position, go first (from the topmost one),
    /// they are followed by the annotations, which have just their rectangle there.
    std::vector<const PageAnnotation*> findSelectableAnnotations(QPoint widgetPos, PDFInteger* pageIndex) const;

    /// Returns the annotation, which should be selected by the next step of cycling
    /// through the annotations under the position (the one below the selected one)
    const PageAnnotation* findNextSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const;

    /// Returns true, if the shape of the annotation (not just its rectangle) is at the
    /// device position. Annotations without a thin shape are tested by the rectangle.
    bool isAnnotationShapeAtPosition(const PageAnnotation& annotation, const QTransform& annotationToDevice, const QPointF& devicePosition) const;

    /// Returns the matrix, which maps the geometry of the annotation (in page coordinates)
    /// to the device. It is the page to device matrix, unless the annotation has flags,
    /// which change the way it is displayed (NoRotate, NoZoom).
    QTransform getAnnotationToDeviceMatrix(const PageAnnotation& annotation, PDFInteger pageIndex, const QTransform& pageToDevice) const;

    /// Returns the matrix, which maps the geometry of the transformable selection on the
    /// page to the device. If a single annotation is selected, then it is its matrix (see
    /// \ref getAnnotationToDeviceMatrix), so the handles and the preview match the displayed
    /// annotation. A group of annotations is always manipulated in the space of the page.
    QTransform getSelectionToDeviceMatrix(PDFInteger pageIndex, const QTransform& pageToDevice) const;

    /// Returns the page to device matrix of the page. If the page is not displayed,
    /// then a matrix with the same orientation is returned (it can be used to convert
    /// directions and orientations between the screen and the page).
    QTransform getPageToDeviceMatrix(PDFInteger pageIndex) const;

    /// Keeps the displayed position of an annotation, which is not displayed by the matrix
    /// of the page (flags NoRotate, NoZoom), after its geometry has been edited in the builder.
    /// Such an annotation is anchored at the corner of its rectangle, so an edit, which changes
    /// the rectangle, would move the whole displayed annotation. The function moves the edited
    /// annotation, so it is displayed at the place, where the user edited it.
    void keepDisplayedPosition(PDFDocumentBuilder* builder, PDFObjectReference annotation) const;

    /// Returns the operations, which the user can do with the selection on the page
    PDFAnnotationManipulator::Capabilities getSelectionCapabilities(PDFInteger pageIndex) const;

    /// Returns true, if some annotation on the displayed pages can be selected
    bool hasSelectableAnnotation() const;

    /// Copies the annotations to the clipboard, returns true on success
    bool copyAnnotationsToClipboard(const std::vector<PDFObjectReference>& annotations);

    /// Returns the selected annotations, which can be deleted
    std::vector<PDFObjectReference> getDeletableSelectedAnnotations() const;

    /// Returns the text box of the selection, which is manipulated instead of the annotation
    /// rectangle - a single free text annotation with a callout line must be selected
    TextBoxInfo getTextBoxInfo() const;

    /// Returns the index of the part of the selected annotation (see
    /// PDFAnnotationManipulator::getParts), which can be removed and which is
    /// at the device position, or -1
    int hitTestPart(const QPoint& devicePosition, PDFObjectReference* annotation) const;

    /// Applies the function to the selected annotations, which can be modified,
    /// in a single modification of the document. The function returns true, if
    /// it has modified the annotation.
    void modifySelectedAnnotations(const std::function<bool(PDFDocumentBuilder*, const PageAnnotation&)>& function);

    /// Applies a translation to each selected annotation of the pages. The function returns
    /// the translation (in device coordinates) for the annotation from its displayed
    /// rectangle, the frame of the selection on the page and the index of the annotation.
    void translateSelectedAnnotationsOnScreen(const std::function<QPointF(const QRectF&, const QRectF&, const std::vector<QRectF>&, size_t)>& function);

    /// Handles the keys, which edit the points of the selected annotation
    bool handlePointKeys(QKeyEvent* event);

    /// Draws a text with the numeric feedback next to the mouse cursor
    void drawInfoText(QPainter* painter, const QString& text, const PDFColorConvertor& convertor) const;

    /// Draws the annotation, as it will look like after the transformation
    /// \param isDrawnDirectly The annotation is not drawn by its appearance stream (it is an annotation of a preview, its appearance stream is not in the document)
    void drawAnnotationPreview(QPainter* painter, const PageAnnotation& annotation, PDFInteger pageIndex, const QTransform& annotationToDevice, bool isDrawnDirectly = false) const;

    /// Draws the annotation of the preview of the interaction (see updateInteractionPreview). Returns false, if there is none.
    bool drawInteractionPreview(QPainter* painter, PDFObjectReference annotation, PDFInteger pageIndex, const QTransform& annotationToDevice) const;

    /// Creates the annotations of the preview - the operation, which the interaction does, when it is finished,
    /// is done in a temporary document, so the preview displays the real result (new measured value, new layout of the text)
    void updateInteractionPreview();

    /// Prepares the snapper for an interaction on the page
    void prepareSnapper(const PDFWidgetSnapshot& snapshot, PDFInteger pageIndex, PDFObjectReference excludedAnnotation, bool excludeSelection);

    /// Snaps the rectangle (page coordinates), which is moved on the page, by one of its corners, or by its center.
    /// Returns the correction of the position (page coordinates).
    QPointF snapMovedRectangle(PDFInteger pageIndex, const QRectF& rectangle, bool* isSnapped, QPointF* snappedDevicePoint);

    /// Returns the explanation of the limited capabilities of the annotation for the user
    QString getCapabilitiesHint(PDFAnnotationManipulator::Capabilities capabilities) const;

    /// Returns the page annotation of the annotation on the given page, or nullptr
    const PageAnnotation* findPageAnnotation(PDFInteger pageIndex, PDFObjectReference annotation) const;

    /// Returns the index of the page, on which the annotation is. The page is
    /// determined from the annotation itself (entry P), if the entry is missing,
    /// then the pages currently displayed are searched. Returns -1, if the page
    /// cannot be determined.
    PDFInteger findAnnotationPage(PDFObjectReference annotation) const;

    /// Returns the page annotations of the selected annotations on the page
    std::vector<const PageAnnotation*> getSelectedAnnotations(PDFInteger pageIndex, bool transformableOnly) const;

    /// Returns the bounding rectangle of the selected annotations on the page (page coordinates)
    QRectF getSelectionBoundingRectangle(PDFInteger pageIndex, bool transformableOnly) const;

    /// Returns the page reference of the annotation
    PDFObjectReference getPageReference(PDFInteger pageIndex, const PageAnnotation& annotation) const;

    /// Returns true, if a tool is active in the widget (the annotation manager
    /// does not react to the keyboard shortcuts in that case)
    bool isToolActive() const;

    /// Removes annotations, which do not exist any more, from the selection
    void updateSelectionAfterDocumentChange();

    /// Computes the layout of the selection frame of the page. The frame is the
    /// bounding rectangle of the selected annotations, as they are displayed.
    /// \param pageIndex Page index
    /// \param pageToDevice Page to device matrix
    HandleLayout computeHandleLayout(PDFInteger pageIndex, const QTransform& pageToDevice) const;

    /// Returns the handle at the device position
    Handle hitTestHandle(const HandleLayout& layout, const QPointF& devicePosition) const;

    /// Returns the cursor shape for the handle
    static Qt::CursorShape getCursorShapeForHandle(Handle handle);

    /// Returns the custom cursor for the rotation handle
    const QCursor& getRotationCursor() const;

    /// Starts an interaction with the handle
    bool beginHandleInteraction(const QPoint& devicePosition);

    /// Updates the preview transformation of the handle interaction
    void updateHandleInteraction(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers);

    /// Computes the transformation (in page coordinates) of the handle interaction
    QTransform computeHandleTransform(const QPointF& devicePosition, Qt::KeyboardModifiers modifiers, qreal* angle) const;

    /// Finishes the interaction (applies the transformation, or selects the annotations)
    void finishInteraction();

    /// Cancels the interaction
    void cancelInteraction();

    /// Returns the points of the selection, which can be edited one by one. They
    /// are available only if a single annotation, which can be transformed, is selected.
    PointEditInfo getPointEditInfo() const;

    /// Returns the editable points of the annotation, if the user can modify it
    PDFAnnotationManipulator::EditablePoints getModifiablePoints(PDFObjectReference annotation) const;

    /// Returns the index of the point handle at the device position, or -1
    int hitTestPoint(const PointEditInfo& info, const QTransform& pageToDevice, const QPointF& devicePosition) const;

    /// Returns the index of the segment at the device position, or -1
    /// \param info Points
    /// \param pageToDevice Page to device matrix
    /// \param devicePosition Device position
    /// \param[out] pagePosition Nearest position on the segment in the page coordinates
    int hitTestSegment(const PointEditInfo& info, const QTransform& pageToDevice, const QPointF& devicePosition, QPointF* pagePosition) const;

    /// Starts dragging of the point handle at the device position
    bool beginPointInteraction(const QPoint& devicePosition);

    /// Starts drawing of a new part of the selected annotation (see beginPartEdit)
    bool beginPartInteraction(const QPoint& devicePosition);

    /// Updates the drawn part
    void updatePartInteraction(const QPoint& devicePosition);

    /// Updates the preview of the dragged point
    void updatePointInteraction(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers);

    /// Returns the point (segment) at the device position for the context menu
    PointMenuContext getPointMenuContext(const QPoint& devicePosition) const;

    /// Sets the editable points of the annotation and emits the modified document
    bool setAnnotationPoints(PDFObjectReference annotation, const std::vector<QPointF>& points);

    /// Returns the rubber band rectangle in the page coordinates
    QRectF getRubberBandRectangle() const;

    /// Applies the transformation (in page coordinates) to the selected
    /// annotations of the page, which can be transformed.
    /// \param pageIndex Page index
    /// \param transform Transformation
    void transformSelectedAnnotations(PDFInteger pageIndex, const QTransform& transform);

    /// Applies the transformation (in page coordinates) to the selected annotations
    /// of all pages in a single modification of the document. The transformation
    /// is computed for each page separately from the bounding rectangle of the
    /// selection on the page.
    /// \param transformFactory Function creating the transformation from the bounding rectangle and the page index
    void transformSelectedAnnotations(const std::function<QTransform(const QRectF&, PDFInteger)>& transformFactory);

    /// Shows the context menu for an annotation, which cannot be selected
    void showUnselectableAnnotationMenu(const PageAnnotation& annotation, PDFObjectReference pageReference, QPoint globalPosition);

    /// Shows the context menu for the current selection
    /// \param globalPosition Position of the menu
    /// \param widgetPosition Position of the mouse in the widget (for pasting)
    /// \param pointContext Point or segment of the selected annotation under the mouse
    void showSelectionMenu(QPoint globalPosition, std::optional<QPoint> widgetPosition, const PointMenuContext& pointContext);

    /// Draws the selection, the selection frame with handles, the preview of
    /// the manipulated annotations and the rubber band.
    void drawSelection(QPainter* painter,
                       PDFInteger pageIndex,
                       const QTransform& pagePointToDevicePointMatrix,
                       const PDFColorConvertor& convertor) const;

    /// Requests repaint of the widget
    void requestRepaint();

    void onShowPopupAnnotation();
    void onCopyAnnotation();
    void onEditAnnotation();
    void onEditGeometry();
    void onDeleteAnnotation();

    /// Creates dialog for markup annotations. This function is used only for markup annotations,
    /// do not use them for other annotations (function can crash).
    /// \param widget Dialog's parent widget
    /// \param pageAnnotation Markup annotation
    /// \param pageAnnotations Page annotations
    QDialog* createDialogForMarkupAnnotations(PDFWidget* widget,
                                              const PageAnnotation& pageAnnotation,
                                              const PageAnnotations& pageAnnotations);

    /// Creates widgets for markup annotation main popup widget. Also sets
    /// default size of parent widget.
    /// \param parentWidget Parent widget, where widgets are created
    /// \param pageAnnotation Markup annotation
    /// \param pageAnnotations Page annotations
    void createWidgetsForMarkupAnnotations(QWidget* parentWidget,
                                           const PageAnnotation& pageAnnotation,
                                           const PageAnnotations& pageAnnotations);

    PDFDrawWidgetProxy* m_proxy;
    QString m_tooltip;
    std::optional<QCursor> m_cursor;
    QPoint m_editableAnnotationGlobalPosition; ///< Position, where action on annotation was executed
    PDFObjectReference m_editableAnnotation;    ///< Annotation to be edited or deleted
    PDFObjectReference m_editableAnnotationPage;    ///< Page of annotation above
    bool m_suppressLinkActivationOnRelease = false;
    bool m_isLinkPressed = false;               ///< The mouse button was pressed over a link (so its release can activate it)
    PDFObjectReference m_pendingDeselection;    ///< Selected annotation clicked with a modifier, it is deselected on release (if it is not dragged)

    std::vector<SelectedAnnotation> m_selection;
    int m_activePoint = -1;                 ///< Point of the selected annotation, which is edited from the keyboard
    PDFSnapper m_snapper;                   ///< Snapping of the dragged point
    mutable std::optional<QCursor> m_rotationCursor;
    HoveredAnnotation m_hoveredAnnotation;
    Handle m_hoveredHandle = Handle::None;
    int m_hoveredPoint = -1;
    QPoint m_lastMousePosition;
    PartEdit m_partEdit = PartEdit::None;
    InteractionState m_interaction;
    DropFeedback m_dropFeedback;
    DragState m_dragState;

    /// Identifier of this manager, it is stored in the drag data, so the drop
    /// can distinguish, if the annotations are dragged inside the document
    /// (then they are moved), or from another document (then they are copied).
    QUuid m_dragSourceId;
};

}   // namespace pdf

#endif // PDFWIDGETANNOTATION_H
