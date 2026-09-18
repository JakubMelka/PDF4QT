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

#ifndef PDFANNOTATIONGEOMETRYDIALOG_H
#define PDFANNOTATIONGEOMETRYDIALOG_H

#include "pdfwidgetsglobal.h"
#include "pdfannotationmanipulator.h"

#include <QDialog>

class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QGroupBox;
class QTableWidget;
class QDoubleSpinBox;

namespace pdf
{

/// Dialog for exact (numerical) editing of the geometry of an annotation - position
/// and size of its rectangle, rotation and coordinates of its points. The dialog
/// offers only the operations supported by the annotation (see capabilities in
/// \ref PDFAnnotationManipulator). The user edits either the rectangle, or the
/// points - the rectangle is derived from the points, so they cannot be changed both.
/// Coordinates are displayed in a selected unit, the origin is the origin of the page
/// coordinate system (the y axis points upwards). The position of the rectangle is the
/// position of its reference point - the reference point stays, when the size is changed,
/// and the annotation is rotated around it. Length and direction of each segment between
/// the points can be typed directly (the end point of the segment is moved).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationGeometryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFAnnotationGeometryDialog(const QRectF& rectangle,
                                         const PDFAnnotationManipulator::EditablePoints& points,
                                         PDFAnnotationManipulator::Capabilities capabilities,
                                         QWidget* parent);

    /// Point of the rectangle (the y axis points upwards, so the top is the edge with the greatest y)
    enum class ReferencePoint
    {
        TopLeft,
        Top,
        TopRight,
        Left,
        Center,
        Right,
        BottomLeft,
        Bottom,
        BottomRight
    };

    /// Returns the rectangle in the page coordinates
    QRectF getRectangle() const;

    /// Returns the points in the page coordinates
    std::vector<QPointF> getPoints() const;

    /// Returns the rotation in degrees (positive angle is clockwise on the screen)
    qreal getRotation() const;

    bool isRectangleChanged() const { return m_isRectangleChanged; }
    bool isPointsChanged() const { return m_isPointsChanged; }

    /// Sets the rectangle (in the page coordinates), as if the user typed it
    void setRectangle(const QRectF& rectangle);

    /// Sets the point (in the page coordinates), as if the user typed it
    void setPoint(size_t index, const QPointF& point);

    /// Sets the rotation in degrees, as if the user typed it
    void setRotation(qreal degrees);

    /// Sets the unit, in which the coordinates are displayed (0 - points, 1 - millimeters, 2 - inches)
    void setUnit(int index);

    /// Returns the text describing the selected segment (length and angle)
    QString getLineInfo() const;

    /// Sets the reference point, as if the user selected it
    void setReferencePoint(ReferencePoint referencePoint);

    /// Returns the reference point of the rectangle (in the page coordinates)
    QPointF getReferencePoint(const QRectF& rectangle) const;

    /// Sets the size of the rectangle (in the page units), as if the user typed it.
    /// The reference point of the rectangle stays at its place.
    void setSize(const QSizeF& size);

    /// Selects the segment (zero based index), whose length and angle are edited
    void setSegment(int index);

    /// Sets the length of the selected segment (in the page units), as if the user typed it
    void setSegmentLength(qreal length);

    /// Sets the angle of the selected segment, as if the user typed it (in degrees,
    /// counterclockwise from the direction of the x axis of the page)
    void setSegmentAngle(qreal degrees);

private:
    void onUnitChanged();
    void onRectangleEdited();
    void onPointEdited();
    void onSegmentEdited();
    void updateWidgets();

    /// Returns the count of the segments between the points
    int getSegmentCount() const;

    /// Returns the count of the displayed units in a point (1/72 of inch)
    qreal getUnitFactor() const;

    QRectF m_originalRectangle;
    QRectF m_rectangle;
    std::vector<QPointF> m_points;
    PDFAnnotationManipulator::Capabilities m_capabilities;
    bool m_isClosed = false;
    bool m_isRectangleChanged = false;
    bool m_isPointsChanged = false;
    bool m_isUpdating = false;

    QComboBox* m_unitComboBox = nullptr;
    QGroupBox* m_rectangleGroupBox = nullptr;
    QComboBox* m_referencePointComboBox = nullptr;
    QDoubleSpinBox* m_leftSpinBox = nullptr;
    QDoubleSpinBox* m_bottomSpinBox = nullptr;
    QDoubleSpinBox* m_widthSpinBox = nullptr;
    QDoubleSpinBox* m_heightSpinBox = nullptr;
    QCheckBox* m_keepAspectRatioCheckBox = nullptr;
    QGroupBox* m_rotationGroupBox = nullptr;
    QDoubleSpinBox* m_rotationSpinBox = nullptr;
    QGroupBox* m_pointsGroupBox = nullptr;
    QTableWidget* m_pointsTable = nullptr;
    QLabel* m_lineInfoLabel = nullptr;
    QSpinBox* m_segmentSpinBox = nullptr;
    QDoubleSpinBox* m_segmentLengthSpinBox = nullptr;
    QDoubleSpinBox* m_segmentAngleSpinBox = nullptr;
};

}   // namespace pdf

#endif // PDFANNOTATIONGEOMETRYDIALOG_H
