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
///    an empty area selects all annotations inside the dragged rectangle,
///  - selected annotations can be dragged to a new position (also to another
///    page, or to another document; holding Ctrl while dropping copies them),
///    resized using the handles of the selection frame and rotated using the
///    rotation handle above the frame,
///  - if a single annotation defined by points is selected (line, polygon,
///    polyline, callout line of a free text), then each point has its own handle
///    and it can be dragged (Shift constrains the direction to multiples of 45
///    degrees); points of polygons and polylines can be inserted and removed
///    using the context menu,
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

    /// Selects the annotation and shows the context menu for it
    /// \param annotationReference Annotation
    /// \param pageReference Page of the annotation
    /// \param globalMenuPosition Position of the menu in global coordinates
    void showAnnotationMenu(pdf::PDFObjectReference annotationReference,
                            pdf::PDFObjectReference pageReference,
                            QPoint globalMenuPosition);

    bool canAcceptAnnotationDrag(const QMimeData* data) const;
    bool handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action);

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

    /// Returns true, if the document permits modification of the annotations
    bool isModificationAllowed() const;

    /// Returns snap information generated from editable annotation geometry on a page.
    /// \param pageIndex Page index
    PDFSnapInfo getSnapInfo(PDFInteger pageIndex) const;

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
    void pasteAnnotations(std::optional<QPoint> widgetPosition);

    /// Deletes the selected annotations, which can be deleted
    void deleteSelectedAnnotations();

    /// Moves the selected annotations by an offset given in the page coordinates
    /// \param offset Offset
    void translateSelectedAnnotations(const QPointF& offset);

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
        Point           ///< Dragging of a single point of the selected annotation
    };

    struct InteractionState
    {
        Interaction type = Interaction::None;
        PDFInteger pageIndex = -1;
        QTransform pageToDevice;
        QTransform deviceToPage;
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
    };

    void updateFromMouseEvent(QMouseEvent* event);
    bool beginAnnotationDrag(QMouseEvent* event, PDFInteger pageIndex);
    void startAnnotationDrag(QMouseEvent* event);
    QPixmap createAnnotationDragPixmap(const QTransform& pagePointToDevicePointMatrix, QPoint& hotSpot) const;
    const PDFAction* getLinkActionAtPosition(QPoint widgetPos) const;

    /// Returns page annotation under the widget position, which can be selected.
    /// If several annotations are under the position, then the topmost is returned.
    const PageAnnotation* findSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const;

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

    /// Computes the layout of the selection frame
    /// \param boundingRectangle Bounding rectangle of the selection (page coordinates)
    /// \param pageToDevice Page to device matrix
    HandleLayout computeHandleLayout(const QRectF& boundingRectangle, const QTransform& pageToDevice) const;

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
    QTransform computeHandleTransform(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers, qreal* angle) const;

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
    /// of all pages. The transformation is computed for each page separately
    /// from the bounding rectangle of the selection on the page.
    /// \param transformFactory Function creating the transformation from the bounding rectangle and the page
    void transformSelectedAnnotations(const std::function<QTransform(const QRectF&, const PDFPage*)>& transformFactory);

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

    std::vector<SelectedAnnotation> m_selection;
    mutable std::optional<QCursor> m_rotationCursor;
    HoveredAnnotation m_hoveredAnnotation;
    Handle m_hoveredHandle = Handle::None;
    int m_hoveredPoint = -1;
    InteractionState m_interaction;
    DragState m_dragState;

    /// Identifier of this manager, it is stored in the drag data, so the drop
    /// can distinguish, if the annotations are dragged inside the document
    /// (then they are moved), or from another document (then they are copied).
    QUuid m_dragSourceId;
};

}   // namespace pdf

#endif // PDFWIDGETANNOTATION_H
