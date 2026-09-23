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

#include "pdfannotationgeometrydialog.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QLocale>
#include <QLineF>
#include <QtMath>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

PDFAnnotationGeometryDialog::PDFAnnotationGeometryDialog(const QRectF& rectangle,
                                                         const PDFAnnotationManipulator::EditablePoints& points,
                                                         PDFAnnotationManipulator::Capabilities capabilities,
                                                         QWidget* parent) :
    QDialog(parent),
    m_originalRectangle(rectangle.normalized()),
    m_rectangle(rectangle.normalized()),
    m_points(points.isQuadEnds ? std::vector<QPointF>() : points.points),
    m_capabilities(capabilities),
    m_isClosed(points.isClosed)
{
    setWindowTitle(tr("Geometry of Annotation"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Unit
    QFormLayout* unitLayout = new QFormLayout();
    m_unitComboBox = new QComboBox(this);
    m_unitComboBox->addItem(tr("Points (pt)"), 1.0);
    m_unitComboBox->addItem(tr("Millimeters (mm)"), 25.4 / 72.0);
    m_unitComboBox->addItem(tr("Inches (in)"), 1.0 / 72.0);
    unitLayout->addRow(tr("Unit"), m_unitComboBox);
    layout->addLayout(unitLayout);

    auto createSpinBox = [this](qreal minimum)
    {
        QDoubleSpinBox* spinBox = new QDoubleSpinBox(this);
        spinBox->setDecimals(3);
        spinBox->setRange(minimum, 1000000.0);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };

    // Rectangle. The origin is the bottom left corner, as in the page coordinate system.
    // Jakub Melka: the position is the position of the reference point of the rectangle. The
    // reference point stays at its place, when the size is changed, and it is the center of the
    // rotation. Data of the items are the relative coordinates of the point in the rectangle.
    m_rectangleGroupBox = new QGroupBox(tr("Position and Size"), this);
    QFormLayout* rectangleLayout = new QFormLayout(m_rectangleGroupBox);
    m_referencePointComboBox = new QComboBox(m_rectangleGroupBox);
    m_referencePointComboBox->addItem(tr("Top left"), QPointF(0.0, 1.0));
    m_referencePointComboBox->addItem(tr("Top"), QPointF(0.5, 1.0));
    m_referencePointComboBox->addItem(tr("Top right"), QPointF(1.0, 1.0));
    m_referencePointComboBox->addItem(tr("Left"), QPointF(0.0, 0.5));
    m_referencePointComboBox->addItem(tr("Center"), QPointF(0.5, 0.5));
    m_referencePointComboBox->addItem(tr("Right"), QPointF(1.0, 0.5));
    m_referencePointComboBox->addItem(tr("Bottom left"), QPointF(0.0, 0.0));
    m_referencePointComboBox->addItem(tr("Bottom"), QPointF(0.5, 0.0));
    m_referencePointComboBox->addItem(tr("Bottom right"), QPointF(1.0, 0.0));
    m_referencePointComboBox->setCurrentIndex(int(ReferencePoint::Center));
    m_referencePointComboBox->setToolTip(tr("The reference point stays at its place, when the size is changed, and the annotation is rotated around it."));
    m_leftSpinBox = createSpinBox(-1000000.0);
    m_bottomSpinBox = createSpinBox(-1000000.0);
    m_widthSpinBox = createSpinBox(0.001);
    m_heightSpinBox = createSpinBox(0.001);
    m_keepAspectRatioCheckBox = new QCheckBox(tr("Keep aspect ratio"), m_rectangleGroupBox);
    rectangleLayout->addRow(tr("Reference point"), m_referencePointComboBox);
    rectangleLayout->addRow(tr("X of the reference point"), m_leftSpinBox);
    rectangleLayout->addRow(tr("Y of the reference point"), m_bottomSpinBox);
    rectangleLayout->addRow(tr("Width"), m_widthSpinBox);
    rectangleLayout->addRow(tr("Height"), m_heightSpinBox);
    rectangleLayout->addRow(QString(), m_keepAspectRatioCheckBox);
    layout->addWidget(m_rectangleGroupBox);

    const bool canResize = m_capabilities.testFlag(PDFAnnotationManipulator::Resize);
    m_rectangleGroupBox->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Move));
    m_widthSpinBox->setEnabled(canResize);
    m_heightSpinBox->setEnabled(canResize);
    m_keepAspectRatioCheckBox->setEnabled(canResize);
    if (!canResize)
    {
        m_rectangleGroupBox->setToolTip(tr("This annotation can only be moved, its size is fixed."));
    }

    // Rotation
    const bool canRotate = m_capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle) || m_capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary);
    const bool canRotateArbitrary = m_capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary);
    m_rotationGroupBox = new QGroupBox(tr("Rotation"), this);
    QFormLayout* rotationLayout = new QFormLayout(m_rotationGroupBox);
    m_rotationSpinBox = new QDoubleSpinBox(m_rotationGroupBox);
    m_rotationSpinBox->setRange(-360.0, 360.0);
    m_rotationSpinBox->setDecimals(canRotateArbitrary ? 2 : 0);
    m_rotationSpinBox->setSingleStep(canRotateArbitrary ? 1.0 : 90.0);
    m_rotationSpinBox->setSuffix(QString::fromUtf8("°"));
    rotationLayout->addRow(tr("Rotate clockwise around the reference point by"), m_rotationSpinBox);
    m_rotationGroupBox->setEnabled(canRotate);
    if (!canRotate)
    {
        m_rotationGroupBox->setToolTip(tr("This annotation cannot be rotated."));
    }
    else if (!canRotateArbitrary)
    {
        m_rotationGroupBox->setToolTip(tr("This annotation can be rotated only in steps of 90°."));
    }
    layout->addWidget(m_rotationGroupBox);

    // Points
    m_pointsGroupBox = new QGroupBox(tr("Points"), this);
    QVBoxLayout* pointsLayout = new QVBoxLayout(m_pointsGroupBox);
    m_pointsTable = new QTableWidget(int(m_points.size()), 2, m_pointsGroupBox);
    m_pointsTable->setHorizontalHeaderLabels({ tr("X"), tr("Y") });
    m_pointsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_lineInfoLabel = new QLabel(m_pointsGroupBox);
    pointsLayout->addWidget(m_pointsTable);

    // Jakub Melka: length and direction of a segment can be typed directly (length and
    // absolute angle of a line). The start of the segment stays, its end is moved.
    QFormLayout* segmentLayout = new QFormLayout();
    m_segmentSpinBox = new QSpinBox(m_pointsGroupBox);
    m_segmentSpinBox->setRange(1, qMax(getSegmentCount(), 1));
    m_segmentLengthSpinBox = createSpinBox(0.001);
    m_segmentAngleSpinBox = new QDoubleSpinBox(m_pointsGroupBox);
    m_segmentAngleSpinBox->setRange(-360.0, 360.0);
    m_segmentAngleSpinBox->setDecimals(2);
    m_segmentAngleSpinBox->setKeyboardTracking(false);
    m_segmentAngleSpinBox->setSuffix(QString::fromUtf8("°"));
    m_segmentAngleSpinBox->setToolTip(tr("Angle of the segment, counterclockwise from the direction of the x axis."));
    segmentLayout->addRow(tr("Segment"), m_segmentSpinBox);
    segmentLayout->addRow(tr("Length"), m_segmentLengthSpinBox);
    segmentLayout->addRow(tr("Angle"), m_segmentAngleSpinBox);
    pointsLayout->addLayout(segmentLayout);

    const bool canEditSegment = m_capabilities.testFlag(PDFAnnotationManipulator::EditPoints) && getSegmentCount() > 0;
    m_segmentSpinBox->setEnabled(getSegmentCount() > 1);
    m_segmentLengthSpinBox->setEnabled(canEditSegment);
    m_segmentAngleSpinBox->setEnabled(canEditSegment);

    pointsLayout->addWidget(m_lineInfoLabel);
    m_pointsGroupBox->setVisible(!m_points.empty());
    layout->addWidget(m_pointsGroupBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_unitComboBox, &QComboBox::currentIndexChanged, this, &PDFAnnotationGeometryDialog::onUnitChanged);
    connect(m_pointsTable, &QTableWidget::cellChanged, this, &PDFAnnotationGeometryDialog::onPointEdited);
    connect(m_referencePointComboBox, &QComboBox::currentIndexChanged, this, &PDFAnnotationGeometryDialog::onUnitChanged);
    connect(m_segmentSpinBox, &QSpinBox::valueChanged, this, &PDFAnnotationGeometryDialog::onUnitChanged);
    connect(m_segmentLengthSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFAnnotationGeometryDialog::onSegmentEdited);
    connect(m_segmentAngleSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFAnnotationGeometryDialog::onSegmentEdited);

    for (QDoubleSpinBox* spinBox : { m_leftSpinBox, m_bottomSpinBox, m_widthSpinBox, m_heightSpinBox })
    {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, &PDFAnnotationGeometryDialog::onRectangleEdited);
    }

    updateWidgets();
    setMinimumWidth(PDFWidgetUtils::scaleDPI_x(this, 360));
}

qreal PDFAnnotationGeometryDialog::getUnitFactor() const
{
    return m_unitComboBox->currentData().toDouble();
}

QRectF PDFAnnotationGeometryDialog::getRectangle() const
{
    return m_rectangle;
}

std::vector<QPointF> PDFAnnotationGeometryDialog::getPoints() const
{
    return m_points;
}

qreal PDFAnnotationGeometryDialog::getRotation() const
{
    const qreal rotation = m_rotationGroupBox->isEnabled() ? m_rotationSpinBox->value() : 0.0;

    // The annotation can be rotated only by the right angle
    return m_capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary) ? rotation : std::round(rotation / 90.0) * 90.0;
}

int PDFAnnotationGeometryDialog::getSegmentCount() const
{
    if (m_points.size() < 2)
    {
        return 0;
    }

    return int(m_isClosed ? m_points.size() : m_points.size() - 1);
}

QPointF PDFAnnotationGeometryDialog::getReferencePoint(const QRectF& rectangle) const
{
    // The y axis points upwards, so QRectF::top() is the bottom edge of the rectangle
    const QRectF normalizedRectangle = rectangle.normalized();
    const QPointF relativePosition = m_referencePointComboBox->currentData().toPointF();
    return QPointF(normalizedRectangle.left() + relativePosition.x() * normalizedRectangle.width(),
                   normalizedRectangle.top() + relativePosition.y() * normalizedRectangle.height());
}

void PDFAnnotationGeometryDialog::setReferencePoint(ReferencePoint referencePoint)
{
    m_referencePointComboBox->setCurrentIndex(int(referencePoint));
}

void PDFAnnotationGeometryDialog::setSize(const QSizeF& size)
{
    const qreal factor = getUnitFactor();
    m_widthSpinBox->setValue(size.width() * factor);
    m_heightSpinBox->setValue(size.height() * factor);
}

void PDFAnnotationGeometryDialog::setSegment(int index)
{
    m_segmentSpinBox->setValue(index + 1);
}

void PDFAnnotationGeometryDialog::setSegmentLength(qreal length)
{
    m_segmentLengthSpinBox->setValue(length * getUnitFactor());
}

void PDFAnnotationGeometryDialog::setSegmentAngle(qreal degrees)
{
    m_segmentAngleSpinBox->setValue(degrees);
}

void PDFAnnotationGeometryDialog::setRectangle(const QRectF& rectangle)
{
    const qreal factor = getUnitFactor();
    QRectF normalizedRectangle = rectangle.normalized();
    if (!m_widthSpinBox->isEnabled())
    {
        // The size is fixed, the annotation is moved to the corner of the rectangle
        normalizedRectangle.setSize(m_rectangle.size());
    }

    const QPointF referencePoint = getReferencePoint(normalizedRectangle);
    m_leftSpinBox->setValue(referencePoint.x() * factor);
    m_bottomSpinBox->setValue(referencePoint.y() * factor);

    if (m_widthSpinBox->isEnabled())
    {
        const bool keepAspectRatio = m_keepAspectRatioCheckBox->isChecked();
        m_keepAspectRatioCheckBox->setChecked(false);
        m_widthSpinBox->setValue(normalizedRectangle.width() * factor);
        m_heightSpinBox->setValue(normalizedRectangle.height() * factor);
        m_keepAspectRatioCheckBox->setChecked(keepAspectRatio);
    }
}

void PDFAnnotationGeometryDialog::setPoint(size_t index, const QPointF& point)
{
    if (index < m_points.size())
    {
        const qreal factor = getUnitFactor();
        m_pointsTable->item(int(index), 0)->setText(QLocale().toString(point.x() * factor, 'f', 3));
        m_pointsTable->item(int(index), 1)->setText(QLocale().toString(point.y() * factor, 'f', 3));
    }
}

void PDFAnnotationGeometryDialog::setRotation(qreal degrees)
{
    m_rotationSpinBox->setValue(degrees);
}

void PDFAnnotationGeometryDialog::setUnit(int index)
{
    m_unitComboBox->setCurrentIndex(index);
}

QString PDFAnnotationGeometryDialog::getLineInfo() const
{
    return m_lineInfoLabel->text();
}

void PDFAnnotationGeometryDialog::onUnitChanged()
{
    updateWidgets();
}

void PDFAnnotationGeometryDialog::onRectangleEdited()
{
    if (m_isUpdating)
    {
        return;
    }

    const qreal factor = getUnitFactor();
    qreal width = m_widthSpinBox->value() / factor;
    qreal height = m_heightSpinBox->value() / factor;

    if (m_keepAspectRatioCheckBox->isChecked() && m_rectangle.width() > 0.0 && m_rectangle.height() > 0.0)
    {
        // The dimension, which was not edited, follows the edited one
        const qreal aspectRatio = m_rectangle.width() / m_rectangle.height();
        if (sender() == m_heightSpinBox)
        {
            width = height * aspectRatio;
        }
        else
        {
            height = width / aspectRatio;
        }
    }

    // The rectangle is in the page coordinate system (y axis points upwards), so
    // QRectF::top() is the bottom edge of the rectangle. The reference point is, where
    // the user has put it, the rectangle of the new size is placed around it.
    const QPointF relativePosition = m_referencePointComboBox->currentData().toPointF();
    m_rectangle = QRectF(m_leftSpinBox->value() / factor - relativePosition.x() * width,
                         m_bottomSpinBox->value() / factor - relativePosition.y() * height,
                         width, height);
    m_isRectangleChanged = true;

    // The rectangle is derived from the points, so both cannot be edited
    m_pointsGroupBox->setEnabled(false);
    m_pointsGroupBox->setToolTip(tr("Points cannot be edited, because the rectangle was changed."));
    updateWidgets();
}

void PDFAnnotationGeometryDialog::onPointEdited()
{
    if (m_isUpdating)
    {
        return;
    }

    const qreal factor = getUnitFactor();
    for (size_t i = 0; i < m_points.size(); ++i)
    {
        bool isValidX = false;
        bool isValidY = false;
        const qreal x = QLocale().toDouble(m_pointsTable->item(int(i), 0)->text(), &isValidX);
        const qreal y = QLocale().toDouble(m_pointsTable->item(int(i), 1)->text(), &isValidY);

        // Invalid text is replaced by the old value
        if (isValidX)
        {
            m_points[i].setX(x / factor);
        }
        if (isValidY)
        {
            m_points[i].setY(y / factor);
        }
    }

    m_isPointsChanged = true;
    m_rectangleGroupBox->setEnabled(false);
    m_rectangleGroupBox->setToolTip(tr("The rectangle cannot be edited, because the points were changed."));
    updateWidgets();
}

void PDFAnnotationGeometryDialog::onSegmentEdited()
{
    const int segment = m_segmentSpinBox->value() - 1;
    if (m_isUpdating || segment < 0 || segment >= getSegmentCount())
    {
        return;
    }

    // The start of the segment stays, the end is moved. The angle is counterclockwise
    // in the page coordinate system, where the y axis points upwards.
    const QPointF start = m_points[size_t(segment)];
    const qreal length = m_segmentLengthSpinBox->value() / getUnitFactor();
    const qreal angle = qDegreesToRadians(m_segmentAngleSpinBox->value());
    m_points[size_t(segment + 1) % m_points.size()] = start + QPointF(std::cos(angle), std::sin(angle)) * length;

    m_isPointsChanged = true;
    m_rectangleGroupBox->setEnabled(false);
    m_rectangleGroupBox->setToolTip(tr("The rectangle cannot be edited, because the points were changed."));
    updateWidgets();
}

void PDFAnnotationGeometryDialog::updateWidgets()
{
    m_isUpdating = true;

    const qreal factor = getUnitFactor();
    const QPointF referencePoint = getReferencePoint(m_rectangle);
    m_leftSpinBox->setValue(referencePoint.x() * factor);
    m_bottomSpinBox->setValue(referencePoint.y() * factor);
    m_widthSpinBox->setValue(m_rectangle.width() * factor);
    m_heightSpinBox->setValue(m_rectangle.height() * factor);

    for (size_t i = 0; i < m_points.size(); ++i)
    {
        for (int column = 0; column < 2; ++column)
        {
            QTableWidgetItem* item = m_pointsTable->item(int(i), column);
            if (!item)
            {
                item = new QTableWidgetItem();
                m_pointsTable->setItem(int(i), column, item);
            }

            if (!m_capabilities.testFlag(PDFAnnotationManipulator::EditPoints))
            {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }

            const qreal value = (column == 0 ? m_points[i].x() : m_points[i].y()) * factor;
            item->setText(QLocale().toString(value, 'f', 3));
        }
    }

    const int segment = m_segmentSpinBox->value() - 1;
    if (segment >= 0 && segment < getSegmentCount())
    {
        // Length and direction of the selected segment. QLineF::angle() is counterclockwise in
        // a coordinate system, where the y axis points downwards, the y axis of the page points upwards.
        const QLineF line(m_points[size_t(segment)], m_points[size_t(segment + 1) % m_points.size()]);
        const qreal angle = std::fmod(360.0 - line.angle(), 360.0);
        m_segmentLengthSpinBox->setValue(line.length() * factor);
        m_segmentAngleSpinBox->setValue(angle);
        m_lineInfoLabel->setText(tr("Segment %1: length %2, angle %3°").arg(segment + 1).arg(QLocale().toString(line.length() * factor, 'f', 3),
                                                                                              QLocale().toString(angle, 'f', 2)));
    }

    m_isUpdating = false;
}

}   // namespace pdf
