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
    m_capabilities(capabilities)
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
    m_rectangleGroupBox = new QGroupBox(tr("Position and Size"), this);
    QFormLayout* rectangleLayout = new QFormLayout(m_rectangleGroupBox);
    m_leftSpinBox = createSpinBox(-1000000.0);
    m_bottomSpinBox = createSpinBox(-1000000.0);
    m_widthSpinBox = createSpinBox(0.001);
    m_heightSpinBox = createSpinBox(0.001);
    m_keepAspectRatioCheckBox = new QCheckBox(tr("Keep aspect ratio"), m_rectangleGroupBox);
    rectangleLayout->addRow(tr("Left (X)"), m_leftSpinBox);
    rectangleLayout->addRow(tr("Bottom (Y)"), m_bottomSpinBox);
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
    rotationLayout->addRow(tr("Rotate clockwise by"), m_rotationSpinBox);
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
    pointsLayout->addWidget(m_lineInfoLabel);
    m_pointsGroupBox->setVisible(!m_points.empty());
    layout->addWidget(m_pointsGroupBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_unitComboBox, &QComboBox::currentIndexChanged, this, &PDFAnnotationGeometryDialog::onUnitChanged);
    connect(m_pointsTable, &QTableWidget::cellChanged, this, &PDFAnnotationGeometryDialog::onPointEdited);

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

void PDFAnnotationGeometryDialog::setRectangle(const QRectF& rectangle)
{
    const qreal factor = getUnitFactor();
    const QRectF normalizedRectangle = rectangle.normalized();
    m_leftSpinBox->setValue(normalizedRectangle.left() * factor);
    m_bottomSpinBox->setValue(normalizedRectangle.top() * factor);

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
    // QRectF::top() is the bottom edge of the rectangle
    m_rectangle = QRectF(m_leftSpinBox->value() / factor, m_bottomSpinBox->value() / factor, width, height);
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

void PDFAnnotationGeometryDialog::updateWidgets()
{
    m_isUpdating = true;

    const qreal factor = getUnitFactor();
    m_leftSpinBox->setValue(m_rectangle.left() * factor);
    m_bottomSpinBox->setValue(m_rectangle.top() * factor);
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

    if (m_points.size() >= 2)
    {
        // Length and direction of the first segment (of the line)
        const QLineF line(m_points[0], m_points[1]);
        m_lineInfoLabel->setText(tr("First segment: length %1, angle %2°").arg(QLocale().toString(line.length() * factor, 'f', 3),
                                                                             QLocale().toString(std::fmod(360.0 - line.angle(), 360.0), 'f', 2)));
    }

    m_isUpdating = false;
}

}   // namespace pdf
