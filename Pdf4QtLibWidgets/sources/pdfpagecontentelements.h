//    Copyright (C) 2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTELEMENTS_H
#define PDFPAGECONTENTELEMENTS_H

#include "pdfwidgetsglobal.h"
#include "pdfdocumentdrawinterface.h"

#include <QPen>
#include <QFont>
#include <QBrush>
#include <QCursor>
#include <QPainterPath>
#include <QElapsedTimer>

#include <set>

class QSvgRenderer;

namespace pdf
{
class PDFWidget;
class PDFDocument;
class PDFPageContentScene;
class PDFEditedPageContentElement;

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElement
{ 
public:
    explicit PDFPageContentElement() = default;
    virtual ~PDFPageContentElement() = default;

    virtual PDFPageContentElement* clone() const = 0;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const = 0;

    /// Returns manipulation mode. If manipulation mode is zero, then element
    /// cannot be manipulated. If it is nonzero, then element can be manipulated
    /// in some way.
    /// \param point Point on page
    /// \param snapPointDistanceTreshold Snap point threshold
    virtual uint getManipulationMode(const QPointF& point, PDFReal snapPointDistanceThreshold) const = 0;

    /// Performs manipulation of given mode. Mode must be the one returned
    /// from function getManipulationMode. \sa getManipulationMode
    /// \param mode Mode
    /// \param offset Offset
    virtual void performManipulation(uint mode, const QPointF& offset) = 0;

    /// Returns bounding box of the element
    virtual QRectF getBoundingBox() const = 0;

    /// Sets size to the elements that supports it. Does
    /// nothing for elements, which does not support it.
    virtual void setSize(QSizeF size) = 0;

    /// Returns description of the element
    virtual QString getDescription() const = 0;

    PDFInteger getPageIndex() const;
    void setPageIndex(PDFInteger newPageIndex);

    PDFInteger getElementId() const;
    void setElementId(PDFInteger newElementId);

    /// Returns cursor shape for manipulation mode
    /// \param mode Manipulation mode
    static Qt::CursorShape getCursorShapeForManipulationMode(uint mode);

    enum ManipulationModes : uint
    {
        None = 0,
        Select,
        Translate,
        Top,
        Left,
        Right,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Pt1,
        Pt2
    };

protected:
    uint getRectangleManipulationMode(const QRectF& rectangle,
                                      const QPointF& point,
                                      PDFReal snapPointDistanceThreshold) const;

    void performRectangleManipulation(QRectF& rectangle,
                                      uint mode,
                                      const QPointF& offset);

    void performRectangleSetSize(QRectF& rectangle, QSizeF size);

    QString formatDescription(const QString& description) const;

    PDFInteger m_elementId = -1;
    PDFInteger m_pageIndex = -1;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentStyledElement : public PDFPageContentElement
{
public:
    explicit PDFPageContentStyledElement() = default;
    virtual ~PDFPageContentStyledElement() = default;

    const QPen& getPen() const;
    void setPen(const QPen& newPen);

    const QBrush& getBrush() const;
    void setBrush(const QBrush& newBrush);

protected:
    QPen m_pen;
    QBrush m_brush;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementRectangle : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementRectangle() = default;

    virtual PDFPageContentElementRectangle* clone() const override;

    bool isRounded() const;
    void setRounded(bool newRounded);

    const QRectF& getRectangle() const;
    void setRectangle(const QRectF& newRectangle);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size) override;
    virtual QString getDescription() const override;

private:
    bool m_rounded = false;
    QRectF m_rectangle;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementLine : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementLine() = default;

    virtual PDFPageContentElementLine* clone() const override;

    enum class LineGeometry
    {
        General,
        Horizontal,
        Vertical
    };

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size) override;
    virtual QString getDescription() const override;

    LineGeometry getGeometry() const;
    void setGeometry(LineGeometry newGeometry);

    const QLineF& getLine() const;
    void setLine(const QLineF& newLine);

private:
    LineGeometry m_geometry = LineGeometry::General;
    QLineF m_line;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementDot : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementDot() = default;

    virtual PDFPageContentElementDot* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size) override;
    virtual QString getDescription() const override;

    QPointF getPoint() const;
    void setPoint(QPointF newPoint);

private:
    QPointF m_point;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementFreehandCurve : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementFreehandCurve() = default;

    virtual PDFPageContentElementFreehandCurve* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size);
    virtual QString getDescription() const override;

    QPainterPath getCurve() const;
    void setCurve(QPainterPath newCurve);

    bool isEmpty() const { return m_curve.isEmpty(); }
    void addStartPoint(const QPointF& point);
    void addPoint(const QPointF& point);
    void clear();

private:
    QPainterPath m_curve;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentImageElement : public PDFPageContentElement
{
public:
    PDFPageContentImageElement();
    virtual ~PDFPageContentImageElement();

    virtual PDFPageContentImageElement* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size);
    virtual QString getDescription() const override;

    const QByteArray& getContent() const;
    void setContent(const QByteArray& newContent);

    const QRectF& getRectangle() const;
    void setRectangle(const QRectF& newRectangle);

private:
    QRectF m_rectangle;
    QByteArray m_content;
    QImage m_image;
    std::unique_ptr<QSvgRenderer> m_renderer;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementTextBox : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementTextBox() = default;

    virtual PDFPageContentElementTextBox* clone() const override;

    const QRectF& getRectangle() const { return m_rectangle; }
    void setRectangle(const QRectF& newRectangle) { m_rectangle = newRectangle; }

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual uint getManipulationMode(const QPointF& point,
                                     PDFReal snapPointDistanceThreshold) const override;

    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size) override;
    virtual QString getDescription() const override;

    const QString& getText() const;
    void setText(const QString& newText);

    const QFont& getFont() const;
    void setFont(const QFont& newFont);

    PDFReal getAngle() const;
    void setAngle(PDFReal newAngle);

    const Qt::Alignment& getAlignment() const;
    void setAlignment(const Qt::Alignment& newAlignment);

private:
    QString m_text;
    QRectF m_rectangle;
    QFont m_font;
    PDFReal m_angle = 0.0;
    Qt::Alignment m_alignment = Qt::AlignCenter;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementEdited : public PDFPageContentElement
{
public:
    PDFPageContentElementEdited(const PDFEditedPageContentElement* element);
    virtual ~PDFPageContentElementEdited();

    virtual PDFPageContentElementEdited* clone() const override;
    virtual void drawPage(QPainter* painter, PDFInteger pageIndex, const PDFPrecompiledPage* compiledPage, PDFTextLayoutGetter& layoutGetter, const QTransform& pagePointToDevicePointMatrix, QList<PDFRenderError>& errors) const override;
    virtual uint getManipulationMode(const QPointF& point, PDFReal snapPointDistanceThreshold) const override;
    virtual void performManipulation(uint mode, const QPointF& offset) override;
    virtual QRectF getBoundingBox() const override;
    virtual void setSize(QSizeF size) override;
    virtual QString getDescription() const override;

private:
    std::unique_ptr<PDFEditedPageContentElement> m_element;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentElementManipulator : public QObject
{
    Q_OBJECT

public:
    PDFPageContentElementManipulator(PDFPageContentScene* scene, QObject* parent);

    enum class Operation
    {
        AlignTop,
        AlignCenterVertically,
        AlignBottom,
        AlignLeft,
        AlignCenterHorizontally,
        AlignRight,
        SetSameHeight,
        SetSameWidth,
        SetSameSize,
        CenterHorizontally,
        CenterVertically,
        CenterHorAndVert,
        LayoutVertically,
        LayoutHorizontally,
        LayoutForm,
        LayoutGrid
    };

    enum SelectionMode
    {
        NoUpdate = 0x0000,
        Clear    = 0x0001,  ///< Clears current selection
        Select   = 0x0002,  ///< Selects item
        Deselect = 0x0004,  ///< Deselects item
        Toggle   = 0x0008,  ///< Toggles selection of the item
    };
    Q_DECLARE_FLAGS(SelectionModes, SelectionMode)

    /// Returns true, if element with given id is selected
    /// \param id Element id
    bool isSelected(PDFInteger id) const;

    /// Returns true, if all elements are selected
    /// \param ids Element ids
    bool isAllSelected(const std::set<PDFInteger>& elementIds) const;

    /// Returns true, if selection is empty
    bool isSelectionEmpty() const { return m_selection.empty(); }

    /// Clear all selection, stop manipulation
    void reset();

    void update(PDFInteger id, SelectionModes modes);
    void update(const std::set<PDFInteger>& ids, SelectionModes modes);
    void select(PDFInteger id);
    void select(const std::set<PDFInteger>& ids);
    void selectNew(PDFInteger id);
    void selectNew(const std::set<PDFInteger>& ids);
    void deselect(PDFInteger id);
    void deselect(const std::set<PDFInteger>& ids);
    void selectAll();
    void deselectAll();

    bool isManipulationAllowed(PDFInteger pageIndex) const;
    bool isManipulationInProgress() const { return m_isManipulationInProgress; }

    void performOperation(Operation operation);
    void performDeleteSelection();

    void startManipulation(PDFInteger pageIndex,
                           const QPointF& startPoint,
                           const QPointF& currentPoint,
                           PDFReal snapPointDistanceThreshold);

    void updateManipulation(PDFInteger pageIndex,
                            const QPointF& startPoint,
                            const QPointF& currentPoint);

    void finishManipulation(PDFInteger pageIndex,
                            const QPointF& startPoint,
                            const QPointF& currentPoint,
                            bool createCopy);

    void cancelManipulation();

    void drawPage(QPainter* painter,
                  PDFInteger pageIndex,
                  const PDFPrecompiledPage* compiledPage,
                  PDFTextLayoutGetter& layoutGetter,
                  const QTransform& pagePointToDevicePointMatrix,
                  QList<PDFRenderError>& errors) const;

    /// Returns bounding box of whole selection
    QRectF getSelectionBoundingRect() const;

    /// Returns page rectangle for the page
    QRectF getPageMediaBox(PDFInteger pageIndex) const;

signals:
    void selectionChanged();
    void stateChanged();

private:
    void eraseSelectedElementById(PDFInteger id);

    PDFPageContentScene* m_scene;
    std::vector<PDFInteger> m_selection;
    bool m_isManipulationInProgress;
    std::vector<std::unique_ptr<PDFPageContentElement>> m_manipulatedElements;
    std::map<PDFInteger, uint> m_manipulationModes;
    QPointF m_lastUpdatedPoint;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentScene : public QObject,
                                                   public IDocumentDrawInterface,
                                                   public IDrawWidgetInputInterface
{
    Q_OBJECT

public:
    explicit PDFPageContentScene(QObject* parent);
    virtual ~PDFPageContentScene();

    static constexpr PDFInteger INVALID_ELEMENT_ID = 0;

    /// Add new element to page content scene, scene
    /// takes ownership over the element.
    /// \param element Element
    void addElement(PDFPageContentElement* element);

    /// Replaces element in the page content scene, scene
    /// takes ownership over the element.
    /// \param element Element
    void replaceElement(PDFPageContentElement* element);

    /// Returns element by its id (identifier number)
    /// \param id Element id
    PDFPageContentElement* getElementById(PDFInteger id) const;

    /// Clear whole scene - remove all page content elements
    void clear();

    /// Returns true, if scene is empty
    bool isEmpty() const { return m_elements.empty(); }

    bool isActive() const;
    void setActive(bool newIsActive);

    /// Returns set of all element ids
    std::set<PDFInteger> getElementIds() const;

    /// Returns set of selected element ids
    std::set<PDFInteger> getSelectedElementIds() const;

    /// Returns set of involved pages
    std::set<PDFInteger> getPageIndices() const;

    /// Returns bounding box of elements on page
    QRectF getBoundingBox(PDFInteger pageIndex) const;

    /// Set selected items
    void setSelectedElementIds(const std::set<PDFInteger>& selectedElementIds);

    /// Removes elements specified in selection
    /// \param selection Items to be removed
    void removeElementsById(const std::vector<PDFInteger>& selection);

    /// Performs manipulation of selected elements with manipulator
    void performOperation(int operation);

    /// Returns document (or nullptr)
    const PDFDocument* getDocument() const;

    // IDrawWidgetInputInterface interface
public:
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;
    virtual int getInputPriority() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    PDFWidget* widget() const;
    void setWidget(PDFWidget* newWidget);

    void drawElements(QPainter* painter,
                      PDFInteger pageIndex,
                      PDFTextLayoutGetter& layoutGetter,
                      const QTransform& pagePointToDevicePointMatrix,
                      const PDFPrecompiledPage* compiledPage,
                      QList<PDFRenderError>& errors) const;

signals:
    /// This signal is emitted when scene has changed (including graphics)
    void sceneChanged(bool graphicsOnly);

    void selectionChanged();

    /// Request to edit the elements
    void editElementRequest(const std::set<PDFInteger>& elements);

private:

    struct MouseEventInfo
    {
        std::set<PDFInteger> hoveredElementIds;
        QPoint widgetMouseStartPos;
        QPoint widgetMouseCurrentPos;
        QElapsedTimer timer;
        PDFInteger pageIndex = -1;
        QPointF pagePos;

        bool isValid() const { return !hoveredElementIds.empty(); }
    };

    MouseEventInfo getMouseEventInfo(QWidget* widget, QPoint point);

    struct MouseGrabInfo
    {
        MouseEventInfo info;
        int mouseGrabNesting = 0;

        bool isMouseGrabbed() const { return mouseGrabNesting > 0; }
    };

    PDFReal getSnapPointDistanceThreshold() const;

    bool isMouseGrabbed() const { return m_mouseGrabInfo.isMouseGrabbed(); }

    /// Grabs mouse input, if mouse is already grabbed, or if event
    /// is accepted.
    /// \param info Mouse event info
    /// \param event Mouse event
    void grabMouse(const MouseEventInfo& info, QMouseEvent* event);

    /// Release mouse input
    /// \param info Mouse event info
    /// \param event Mouse event
    void ungrabMouse(const MouseEventInfo& info, QMouseEvent* event);

    /// Updates mouse cursor
    /// \param info Mouse event info
    /// \param snapPointDistanceTreshold Snap point threshold
    void updateMouseCursor(const MouseEventInfo& info, PDFReal snapPointDistanceThreshold);

    /// Reaction on selection changed
    void onSelectionChanged();

    PDFInteger m_firstFreeId;
    bool m_isActive;
    PDFWidget* m_widget;
    std::vector<std::unique_ptr<PDFPageContentElement>> m_elements;
    std::optional<QCursor> m_cursor;
    PDFPageContentElementManipulator m_manipulator;
    MouseGrabInfo m_mouseGrabInfo;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFPageContentElementManipulator::SelectionModes)

#endif // PDFPAGECONTENTELEMENTS_H
