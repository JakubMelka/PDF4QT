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

#include "pdfappannotationgeometrydialog.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationoperations.h"
#include "pdfappannotationpropertiesdialog.h"
#include "pdfappdocument.h"
#include "pdfappmainwindow.h"

#include "pdffluentwidgets.h"
#include "pdfreferencepointpicker.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

namespace pdf
{

/// Texts and values of the geometry dialog
class PDFAppAnnotationGeometryDialogHelper
{
public:
    PDFAppAnnotationGeometryDialogHelper() = delete;

    /// Largest value of a field in points
    static constexpr double MAXIMUM_VALUE = 100000.0;

    static QString getTitle(PDFAppAnnotationController* controller, PDFObjectReference annotation)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
        return PDFAppAnnotationGeometryDialog::tr("Geometry of the %1").arg(PDFAppAnnotationInfo::getTypeName(info.type).toLower());
    }

    /// Name of the type in a sentence ("rectangle"; "annotation" without the annotation)
    static QString getTypeNoun(PDFAppAnnotationController* controller, PDFObjectReference annotation)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
        return info.isValid() ? PDFAppAnnotationInfo::getTypeName(info.type).toLower() : PDFAppAnnotationGeometryDialog::tr("annotation");
    }

    static QString getSubtitle(PDFAppAnnotationController* controller, PDFObjectReference annotation)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
        return PDFAppAnnotationGeometryDialog::tr("Page %1 · measured from the bottom left corner of the page").arg(info.pageIndex + 1);
    }

    static QString getUnitSuffix(PDFAppAnnotationGeometryDialog::Unit unit)
    {
        switch (unit)
        {
            case PDFAppAnnotationGeometryDialog::Unit::Points:
                return PDFAppAnnotationGeometryDialog::tr("pt");
            case PDFAppAnnotationGeometryDialog::Unit::Inches:
                return PDFAppAnnotationGeometryDialog::tr("in");
            default:
                break;
        }
        return PDFAppAnnotationGeometryDialog::tr("mm");
    }

    /// Value of a field in points: the exact value, while the field shows it (a
    /// field shows the value rounded to its decimals - a change of another field
    /// must not move or resize the annotation by the rounding)
    static double getFieldValue(const PDFDoubleSpinBox* field, double shown, double exact, double factor)
    {
        return qFuzzyCompare(1.0 + field->value(), 1.0 + shown) ? exact : field->value() / factor;
    }

    /// Fraction of the reference point in the rectangle (0 left / bottom - 1 right / top)
    static QPointF getFraction(Qt::Alignment alignment)
    {
        const double x = alignment.testFlag(Qt::AlignLeft) ? 0.0 : (alignment.testFlag(Qt::AlignRight) ? 1.0 : 0.5);
        const double y = alignment.testFlag(Qt::AlignBottom) ? 0.0 : (alignment.testFlag(Qt::AlignTop) ? 1.0 : 0.5);
        return QPointF(x, y);
    }
};

PDFAppAnnotationGeometryDialog::PDFAppAnnotationGeometryDialog(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, PDFAppAnnotationGeometryDialogHelper::getTitle(controller, annotation),
              PDFAppAnnotationGeometryDialogHelper::getSubtitle(controller, annotation), QDialogButtonBox::NoButton, parent),
    m_controller(controller),
    m_annotation(annotation)
{
    setObjectName(QStringLiteral("annotationGeometryDialog"));
    setBadgeIconName(QStringLiteral("transform"));
    setCardWidth(CARD_WIDTH);

    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const PDFAnnotationPtr parsed = pdfDocument ? PDFAnnotation::parse(&pdfDocument->getStorage(), annotation) : PDFAnnotationPtr();
    if (parsed)
    {
        m_capabilities = PDFAnnotationManipulator::getCapabilities(parsed.data());
        m_originalRectangle = parsed->getRectangle().normalized();
        const PDFAnnotationManipulator::EditablePoints points = PDFAnnotationManipulator::getEditablePoints(parsed.data());
        if (points.isValid() && points.points.size() <= MAXIMUM_POINTS && m_capabilities.testFlag(PDFAnnotationManipulator::EditPoints))
        {
            m_originalPoints = points.points;
        }
    }
    m_rectangle = m_originalRectangle;
    m_points = m_originalPoints;

    createBody();

    QPushButton* cancel = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("geometryCancel"));
    connect(cancel, &QPushButton::clicked, this, &PDFAppAnnotationGeometryDialog::reject);
    m_cancelButton = cancel;
    m_applyButton = addActionButton(tr("Apply"), ActionPosition::Trailing, true);
    m_applyButton->setObjectName(QStringLiteral("geometryApply"));
    connect(m_applyButton.data(), &QPushButton::clicked, this, &PDFAppAnnotationGeometryDialog::onApplyClicked);

    onReferencePointChanged(m_referencePicker->getCurrentIndex());
    updateFields();
    PDFAppAnnotationPropertiesDialog::fitScrollArea(this, m_scrollArea);

    connect(this, &QDialog::finished, this, &PDFAppAnnotationGeometryDialog::onDialogFinished);
    if (controller)
    {
        // The tab (with its controller) deleted: the dialog closes
        connect(controller, &QObject::destroyed, this, &PDFAppAnnotationGeometryDialog::onControllerDestroyed);
    }
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppAnnotationGeometryDialog::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppAnnotationGeometryDialog::onDocumentChanged);
    }
    if (controller)
    {
        connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotationGeometryDialog::onDocumentChanged);
    }
}

PDFAppAnnotationGeometryDialog::~PDFAppAnnotationGeometryDialog() = default;

PDFAppAnnotationGeometryDialog* PDFAppAnnotationGeometryDialog::open(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent)
{
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    if (!document || !controller->isAnnotationEditingAllowed() || !PDFAppAnnotationOperations::canModifyAnnotation(document->getDocument(), annotation))
    {
        return nullptr;
    }

    PDFAppAnnotationGeometryDialog* dialog = new PDFAppAnnotationGeometryDialog(controller, annotation, parent);
    PDFAppMainWindow::prepareDialog(dialog, parent);
    PDFAppAnnotationPropertiesDialog::fitScrollArea(dialog, dialog->m_scrollArea);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->BaseClass::open();
    return dialog;
}

void PDFAppAnnotationGeometryDialog::createBody()
{
    // The fields of the dialog in a scrolled body (built again at a language change)
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("annotationGeometryBody"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    QWidget* body = new QWidget(scrollArea);
    body->setAutoFillBackground(false);
    QVBoxLayout* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // units 1024:21765 (segmented 180 x 32)
    m_unitControl = new PDFSegmentedControl(this);
    m_unitControl->setObjectName(QStringLiteral("geometryUnits"));
    m_unitControl->setAccessibleName(tr("Units"));
    m_unitControl->addSegment(tr("pt"));
    m_unitControl->addSegment(tr("mm"));
    m_unitControl->addSegment(tr("in"));
    m_unitControl->setFixedWidth(180);
    m_unitControl->setCurrentIndex(1);
    QHBoxLayout* unitRow = createRow(layout, tr("Units"));
    unitRow->addWidget(m_unitControl);
    unitRow->addStretch(1);
    connect(m_unitControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAnnotationGeometryDialog::onUnitChanged);

    // reference-point 1024:21781 + hint 1024:21794
    m_referencePicker = new PDFReferencePointPicker(this);
    m_referencePicker->setObjectName(QStringLiteral("geometryReferencePoint"));
    m_referencePicker->setAccessibleName(tr("Reference point"));
    m_referenceHint = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_referenceHint->setObjectName(QStringLiteral("geometryReferenceHint"));
    m_referenceHint->setWordWrap(true);
    m_referenceHint->setLineHeight(15);
    QHBoxLayout* referenceRow = createRow(layout, tr("Reference point"), true);
    referenceRow->addWidget(m_referencePicker, 0, Qt::AlignTop);
    referenceRow->addWidget(m_referenceHint, 1);
    connect(m_referencePicker.data(), &PDFReferencePointPicker::currentIndexChanged, this, &PDFAppAnnotationGeometryDialog::onReferencePointChanged);

    // X / Y, W / H (Spin box 120 x 34 with the unit)
    QHBoxLayout* positionRow = createRow(layout, tr("Position"));
    positionRow->addWidget(new PDFLabel(tr("X"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this));
    m_xField = createField(QStringLiteral("geometryX"));
    positionRow->addWidget(m_xField);
    positionRow->addWidget(new PDFLabel(tr("Y"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this));
    m_yField = createField(QStringLiteral("geometryY"));
    positionRow->addWidget(m_yField);
    positionRow->addStretch(1);

    QHBoxLayout* sizeRow = createRow(layout, tr("Size"));
    sizeRow->addWidget(new PDFLabel(tr("W"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this));
    m_widthField = createField(QStringLiteral("geometryWidth"));
    sizeRow->addWidget(m_widthField);
    sizeRow->addWidget(new PDFLabel(tr("H"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this));
    m_heightField = createField(QStringLiteral("geometryHeight"));
    sizeRow->addWidget(m_heightField);
    sizeRow->addStretch(1);
    m_xField->setAccessibleName(tr("X"));
    m_yField->setAccessibleName(tr("Y"));
    m_widthField->setAccessibleName(tr("Width"));
    m_heightField->setAccessibleName(tr("Height"));
    m_widthField->setMinimum(0.0);
    m_heightField->setMinimum(0.0);

    m_keepAspectRatioCheckBox = new QCheckBox(tr("Keep aspect ratio"), this);
    m_keepAspectRatioCheckBox->setObjectName(QStringLiteral("geometryKeepAspectRatio"));
    createRow(layout, QString())->addWidget(m_keepAspectRatioCheckBox, 1);

    // rotation 1024:21846: 0 / 90 / 180 / 270 (or Rotate by … ° of the shapes, which turn by any angle)
    if (m_capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary))
    {
        m_angleSpinBox = new PDFDoubleSpinBox(this);
        m_angleSpinBox->setObjectName(QStringLiteral("geometryAngle"));
        m_angleSpinBox->setAccessibleName(tr("Rotate by"));
        m_angleSpinBox->setRange(-360.0, 360.0);
        m_angleSpinBox->setDecimals(1);
        m_angleSpinBox->setDisplaySuffix(QStringLiteral("°"));
        m_angleSpinBox->setFixedWidth(FIELD_WIDTH);
        QHBoxLayout* angleRow = createRow(layout, tr("Rotate by"));
        angleRow->addWidget(m_angleSpinBox);
        angleRow->addWidget(new PDFLabel(tr("Clockwise around the reference point."), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this), 1);
        connect(m_angleSpinBox.data(), &PDFDoubleSpinBox::valueChanged, this, &PDFAppAnnotationGeometryDialog::onRotationChanged);
    }
    else if (m_capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle))
    {
        m_rotationControl = new PDFSegmentedControl(this);
        m_rotationControl->setObjectName(QStringLiteral("geometryRotation"));
        m_rotationControl->setAccessibleName(tr("Rotate by"));
        for (const QString& text : { QStringLiteral("0°"), QStringLiteral("90°"), QStringLiteral("180°"), QStringLiteral("270°") })
        {
            m_rotationControl->addSegment(text);
        }
        QHBoxLayout* rotationRow = createRow(layout, tr("Rotate by"), true);
        QVBoxLayout* rotationColumn = new QVBoxLayout();
        rotationColumn->setContentsMargins(0, 0, 0, 0);
        rotationColumn->setSpacing(6);
        rotationColumn->addWidget(m_rotationControl, 0, Qt::AlignLeft);
        rotationColumn->addWidget(new PDFLabel(tr("Clockwise around the reference point, in steps of 90°."), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this));
        rotationRow->addLayout(rotationColumn, 1);
        connect(m_rotationControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAnnotationGeometryDialog::onRotationChanged);
    }

    // Points 1027:22165: a row per point (Point, X, Y)
    if (!m_points.empty())
    {
        QGridLayout* grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(6);
        grid->addWidget(new PDFLabel(tr("Point"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this), 0, 0);
        grid->addWidget(new PDFLabel(tr("X"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this), 0, 1);
        grid->addWidget(new PDFLabel(tr("Y"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this), 0, 2);
        for (size_t i = 0; i < m_points.size(); ++i)
        {
            PDFDoubleSpinBox* xField = createField(QStringLiteral("geometryPointX:%1").arg(i));
            PDFDoubleSpinBox* yField = createField(QStringLiteral("geometryPointY:%1").arg(i));
            disconnect(xField, nullptr, this, nullptr);
            disconnect(yField, nullptr, this, nullptr);
            xField->setAccessibleName(tr("X of the point %1").arg(i + 1));
            yField->setAccessibleName(tr("Y of the point %1").arg(i + 1));
            connect(xField, &PDFDoubleSpinBox::valueChanged, this, &PDFAppAnnotationGeometryDialog::onPointFieldChanged);
            connect(yField, &PDFDoubleSpinBox::valueChanged, this, &PDFAppAnnotationGeometryDialog::onPointFieldChanged);
            grid->addWidget(new PDFLabel(QString::number(i + 1), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this), int(i) + 1, 0);
            grid->addWidget(xField, int(i) + 1, 1);
            grid->addWidget(yField, int(i) + 1, 2);
            m_pointFields.emplace_back(xField, yField);
        }
        grid->setColumnStretch(3, 1);
        layout->addLayout(grid);
        PDFLabel* hint = new PDFLabel(tr("Change either the rectangle or the points: after the first change the other group is disabled."),
                                      PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    scrollArea->setWidget(body);
    scrollArea->viewport()->setAutoFillBackground(false);
    body->setAutoFillBackground(false);
    addContent(scrollArea);
    m_scrollArea = scrollArea;

    // Move / Resize of the capabilities of the annotation
    m_xField->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Move));
    m_yField->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Move));
    m_widthField->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Resize));
    m_heightField->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Resize));
    m_keepAspectRatioCheckBox->setEnabled(m_capabilities.testFlag(PDFAnnotationManipulator::Resize));
}

void PDFAppAnnotationGeometryDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppAnnotationGeometryDialog::retranslate()
{
    using H = PDFAppAnnotationGeometryDialogHelper;

    // The head and the buttons
    setTitle(H::getTitle(m_controller, m_annotation));
    setText(H::getSubtitle(m_controller, m_annotation));
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
    if (m_applyButton)
    {
        m_applyButton->setText(tr("Apply"));
    }

    // The body is built again with the state of the dialog (the edited rectangle
    // and points, the unit, the reference point, the rotation); the old body goes
    // from the event loop
    QScrollArea* oldScrollArea = m_scrollArea.data();
    if (!oldScrollArea)
    {
        return;
    }

    const int unitIndex = m_unitControl ? m_unitControl->getCurrentIndex() : int(m_unit);
    const int referenceIndex = m_referencePicker ? m_referencePicker->getCurrentIndex() : 0;
    const bool isKeepAspectRatioChecked = m_keepAspectRatioCheckBox && m_keepAspectRatioCheckBox->isChecked();
    const int rotationIndex = m_rotationControl ? m_rotationControl->getCurrentIndex() : 0;
    const double angle = m_angleSpinBox ? m_angleSpinBox->value() : 0.0;
    const int scrollPosition = oldScrollArea->verticalScrollBar()->value();
    QWidget* focusWidget = QApplication::focusWidget();
    const QString focusName = focusWidget && oldScrollArea->isAncestorOf(focusWidget) ? focusWidget->objectName() : QString();
    const bool isRectangleChanged = m_isRectangleChanged;
    const bool isPointsChanged = m_isPointsChanged;

    m_pointFields.clear();
    oldScrollArea->hide();
    oldScrollArea->setParent(nullptr);
    oldScrollArea->deleteLater();

    createBody();
    m_isUpdating = true;
    m_unitControl->setCurrentIndex(unitIndex);
    m_referencePicker->setCurrentIndex(referenceIndex);
    m_keepAspectRatioCheckBox->setChecked(isKeepAspectRatioChecked);
    if (m_rotationControl)
    {
        m_rotationControl->setCurrentIndex(rotationIndex);
    }
    if (m_angleSpinBox)
    {
        m_angleSpinBox->setValue(angle);
    }
    m_isUpdating = false;
    m_unit = static_cast<Unit>(qBound(0, unitIndex, 2));
    m_isRectangleChanged = isRectangleChanged;
    m_isPointsChanged = isPointsChanged;
    onReferencePointChanged(m_referencePicker->getCurrentIndex());

    PDFAppAnnotationPropertiesDialog::fitScrollArea(this, m_scrollArea);
    if (m_scrollArea)
    {
        m_scrollArea->verticalScrollBar()->setValue(scrollPosition);
    }
    if (QWidget* widget = !focusName.isEmpty() && m_scrollArea ? m_scrollArea->findChild<QWidget*>(focusName) : nullptr)
    {
        widget->setFocus(Qt::OtherFocusReason);
    }
}

PDFAppAnnotationController* PDFAppAnnotationGeometryDialog::getController() const
{
    return m_controller.data();
}

PDFReferencePointPicker* PDFAppAnnotationGeometryDialog::getReferencePointPicker() const { return m_referencePicker.data(); }
PDFSegmentedControl* PDFAppAnnotationGeometryDialog::getUnitControl() const { return m_unitControl.data(); }
QCheckBox* PDFAppAnnotationGeometryDialog::getKeepAspectRatioCheckBox() const { return m_keepAspectRatioCheckBox.data(); }
PDFSegmentedControl* PDFAppAnnotationGeometryDialog::getRotationControl() const { return m_rotationControl.data(); }
PDFDoubleSpinBox* PDFAppAnnotationGeometryDialog::getAngleSpinBox() const { return m_angleSpinBox.data(); }

PDFDoubleSpinBox* PDFAppAnnotationGeometryDialog::getField(const QString& name) const
{
    if (name == QStringLiteral("x"))
    {
        return m_xField.data();
    }
    if (name == QStringLiteral("y"))
    {
        return m_yField.data();
    }
    if (name == QStringLiteral("width"))
    {
        return m_widthField.data();
    }
    return name == QStringLiteral("height") ? m_heightField.data() : nullptr;
}

PDFDoubleSpinBox* PDFAppAnnotationGeometryDialog::getPointField(size_t index, bool isY) const
{
    if (index >= m_pointFields.size())
    {
        return nullptr;
    }
    return isY ? m_pointFields[index].second.data() : m_pointFields[index].first.data();
}

PDFDoubleSpinBox* PDFAppAnnotationGeometryDialog::createField(const QString& objectName)
{
    using H = PDFAppAnnotationGeometryDialogHelper;

    PDFDoubleSpinBox* field = new PDFDoubleSpinBox(this);
    field->setObjectName(objectName);
    field->setRange(-H::MAXIMUM_VALUE, H::MAXIMUM_VALUE);
    field->setDecimals(1);
    field->setFixedWidth(FIELD_WIDTH);
    field->setKeyboardTracking(false);
    connect(field, &PDFDoubleSpinBox::valueChanged, this, &PDFAppAnnotationGeometryDialog::onRectangleFieldChanged);
    return field;
}

QHBoxLayout* PDFAppAnnotationGeometryDialog::createRow(QVBoxLayout* layout, const QString& label, bool alignTop)
{
    // row-units 1024:21762: label Regular 12 text/secondary 104 wide, gap 12
    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    PDFLabel* rowLabel = new PDFLabel(label, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    rowLabel->setFixedWidth(LABEL_WIDTH);
    row->addWidget(rowLabel, 0, alignTop ? Qt::AlignTop : Qt::AlignVCenter);

    QHBoxLayout* control = new QHBoxLayout();
    control->setContentsMargins(0, 0, 0, 0);
    control->setSpacing(8);
    row->addLayout(control, 1);
    layout->addLayout(row);
    return control;
}

double PDFAppAnnotationGeometryDialog::getUnitFactor(Unit unit)
{
    switch (unit)
    {
        case Unit::Millimeters:
            return 25.4 / 72.0;
        case Unit::Inches:
            return 1.0 / 72.0;
        default:
            break;
    }
    return 1.0;
}

void PDFAppAnnotationGeometryDialog::setUnit(Unit unit)
{
    if (m_unitControl)
    {
        m_unitControl->setCurrentIndex(int(unit));
    }
}

QPointF PDFAppAnnotationGeometryDialog::getReferencePoint(const QRectF& rectangle) const
{
    const QPointF fraction = PDFAppAnnotationGeometryDialogHelper::getFraction(m_referencePicker ? m_referencePicker->getAlignment() : Qt::Alignment(Qt::AlignLeft | Qt::AlignTop));
    return QPointF(rectangle.left() + fraction.x() * rectangle.width(), rectangle.top() + fraction.y() * rectangle.height());
}

double PDFAppAnnotationGeometryDialog::getRotation() const
{
    if (m_angleSpinBox)
    {
        return m_angleSpinBox->value();
    }
    return m_rotationControl ? 90.0 * m_rotationControl->getCurrentIndex() : 0.0;
}

void PDFAppAnnotationGeometryDialog::updateFields()
{
    // The fields show the values in the unit (page coordinates grow upwards: the
    // rectangle of the page coordinates has its top at the minimal y)
    using H = PDFAppAnnotationGeometryDialogHelper;

    m_isUpdating = true;
    const double factor = getUnitFactor(m_unit);
    const QString suffix = H::getUnitSuffix(m_unit);
    const QPointF reference = getReferencePoint(m_rectangle);
    const std::pair<PDFDoubleSpinBox*, double> values[] = { { m_xField, reference.x() }, { m_yField, reference.y() },
                                                          { m_widthField, m_rectangle.width() }, { m_heightField, m_rectangle.height() } };
    for (size_t i = 0; i < std::size(values); ++i)
    {
        values[i].first->setDisplaySuffix(suffix);
        values[i].first->setDecimals(m_unit == Unit::Inches ? 3 : 1);
        values[i].first->setValue(values[i].second * factor);
        m_shownValues[i] = values[i].first->value();
    }
    for (size_t i = 0; i < m_pointFields.size() && i < m_points.size(); ++i)
    {
        for (PDFDoubleSpinBox* field : { m_pointFields[i].first.data(), m_pointFields[i].second.data() })
        {
            if (field)
            {
                field->setDisplaySuffix(suffix);
                field->setDecimals(m_unit == Unit::Inches ? 3 : 1);
            }
        }
        if (m_pointFields[i].first)
        {
            m_pointFields[i].first->setValue(m_points[i].x() * factor);
        }
        if (m_pointFields[i].second)
        {
            m_pointFields[i].second->setValue(m_points[i].y() * factor);
        }
    }
    m_shownPoints.assign(m_pointFields.size(), QPointF());
    for (size_t i = 0; i < m_pointFields.size(); ++i)
    {
        m_shownPoints[i] = QPointF(m_pointFields[i].first ? m_pointFields[i].first->value() : 0.0, m_pointFields[i].second ? m_pointFields[i].second->value() : 0.0);
    }
    m_isUpdating = false;
    updateGroups();
}

void PDFAppAnnotationGeometryDialog::updateGroups()
{
    // The rectangle and the points are two groups: the first change disables the other one
    const QString pointsTip = tr("The points cannot be edited, because the rectangle was changed.");
    const QString rectangleTip = tr("The rectangle cannot be edited, because the points were changed.");
    const bool isRectangleEnabled = !m_isPointsChanged;
    for (PDFDoubleSpinBox* field : { m_xField.data(), m_yField.data() })
    {
        field->setEnabled(isRectangleEnabled && m_capabilities.testFlag(PDFAnnotationManipulator::Move));
        field->setToolTip(isRectangleEnabled ? QString() : rectangleTip);
    }
    for (PDFDoubleSpinBox* field : { m_widthField.data(), m_heightField.data() })
    {
        field->setEnabled(isRectangleEnabled && m_capabilities.testFlag(PDFAnnotationManipulator::Resize));
        field->setToolTip(isRectangleEnabled ? QString() : rectangleTip);
    }
    for (const auto& fields : m_pointFields)
    {
        for (PDFDoubleSpinBox* field : { fields.first.data(), fields.second.data() })
        {
            if (field)
            {
                field->setEnabled(!m_isRectangleChanged);
                field->setToolTip(m_isRectangleChanged ? pointsTip : QString());
            }
        }
    }
}

void PDFAppAnnotationGeometryDialog::onUnitChanged(int index)
{
    m_unit = static_cast<Unit>(qBound(0, index, 2));
    updateFields();
}

void PDFAppAnnotationGeometryDialog::onReferencePointChanged(int index)
{
    // hint 1024:21794: "Top left stays in place when the size changes, and the rectangle turns around it."
    m_referenceHint->setText(tr("%1 stays in place when the size changes, and the %2 turns around it.")
                                 .arg(PDFReferencePointPicker::getPointName(index), PDFAppAnnotationGeometryDialogHelper::getTypeNoun(m_controller.data(), m_annotation)));
    updateFields();
}

void PDFAppAnnotationGeometryDialog::onRectangleFieldChanged()
{
    if (m_isUpdating)
    {
        return;
    }

    // The reference point stays in place, the size grows away from it; a field,
    // which the user has not changed, keeps its exact value
    using H = PDFAppAnnotationGeometryDialogHelper;
    const double factor = getUnitFactor(m_unit);
    const QPointF fraction = H::getFraction(m_referencePicker->getAlignment());
    const QPointF currentReference = getReferencePoint(m_rectangle);
    double width = qMax(0.0, H::getFieldValue(m_widthField, m_shownValues[2], m_rectangle.width(), factor));
    double height = qMax(0.0, H::getFieldValue(m_heightField, m_shownValues[3], m_rectangle.height(), factor));
    if (m_keepAspectRatioCheckBox->isChecked() && m_rectangle.width() > 0.0 && m_rectangle.height() > 0.0)
    {
        const double ratio = m_rectangle.height() / m_rectangle.width();
        if (sender() == m_heightField)
        {
            width = height / ratio;
        }
        else
        {
            height = width * ratio;
        }
    }

    const QPointF reference(H::getFieldValue(m_xField, m_shownValues[0], currentReference.x(), factor), H::getFieldValue(m_yField, m_shownValues[1], currentReference.y(), factor));
    m_rectangle = QRectF(reference.x() - fraction.x() * width, reference.y() - fraction.y() * height, width, height);
    m_isRectangleChanged = m_rectangle != m_originalRectangle;
    updateFields();
}

void PDFAppAnnotationGeometryDialog::onRotationChanged()
{
    m_isRectangleChanged = m_rectangle != m_originalRectangle || !qFuzzyIsNull(getRotation());
    updateGroups();
}

void PDFAppAnnotationGeometryDialog::onPointFieldChanged()
{
    if (m_isUpdating)
    {
        return;
    }

    // A field, which the user has not changed, keeps the exact value of its point
    using H = PDFAppAnnotationGeometryDialogHelper;
    const double factor = getUnitFactor(m_unit);
    for (size_t i = 0; i < m_pointFields.size() && i < m_points.size() && i < m_shownPoints.size(); ++i)
    {
        if (m_pointFields[i].first && m_pointFields[i].second)
        {
            m_points[i] = QPointF(H::getFieldValue(m_pointFields[i].first, m_shownPoints[i].x(), m_points[i].x(), factor),
                                  H::getFieldValue(m_pointFields[i].second, m_shownPoints[i].y(), m_points[i].y(), factor));
            m_shownPoints[i] = QPointF(m_pointFields[i].first->value(), m_pointFields[i].second->value());
        }
    }
    m_isPointsChanged = m_points != m_originalPoints;
    updateGroups();
}

bool PDFAppAnnotationGeometryDialog::apply()
{
    PDFAppAnnotationController* controller = m_controller.data();
    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    if (!controller || !document || !controller->isAnnotationEditingAllowed())
    {
        return false;
    }

    // The rectangle (and the rotation around the reference point), or the points:
    // the operations are chained, the result is one undo step
    PDFDocumentPointer current = document->getDocument();
    PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::Annotation;
    bool isModified = false;
    if (m_isPointsChanged)
    {
        if (const PDFAppAnnotationResult result = PDFAppAnnotationOperations::setEditablePoints(current, m_annotation, m_points))
        {
            current = PDFDocumentPointer(*result);
            flags |= result->getFlags();
            isModified = true;
        }
    }
    else
    {
        if (m_rectangle != m_originalRectangle && m_rectangle.isValid())
        {
            if (const PDFAppAnnotationResult result = PDFAppAnnotationOperations::setRectangle(current, m_annotation, m_rectangle))
            {
                current = PDFDocumentPointer(*result);
                flags |= result->getFlags();
                isModified = true;
            }
        }

        const double rotation = getRotation();
        if (!qFuzzyIsNull(rotation))
        {
            // Clockwise as seen on the page: the page coordinates grow upwards
            const QPointF reference = getReferencePoint(m_rectangle);
            QTransform transform;
            transform.translate(reference.x(), reference.y());
            transform.rotate(-rotation);
            transform.translate(-reference.x(), -reference.y());
            if (const PDFAppAnnotationResult result = PDFAppAnnotationOperations::transformAnnotation(current, m_annotation, transform))
            {
                current = PDFDocumentPointer(*result);
                flags |= result->getFlags();
                isModified = true;
            }
        }
    }

    if (!isModified)
    {
        accept();
        return true;
    }

    QPointer<PDFAppAnnotationGeometryDialog> guard(this);
    const bool isCommitted = controller->commit(PDFModifiedDocument(current, nullptr, flags), PDFAppAnnotationChangeKind::Annotation, tr("Change geometry"));
    if (guard && isCommitted)
    {
        m_isApplied = true;
        accept();
    }
    return isCommitted;
}

void PDFAppAnnotationGeometryDialog::onApplyClicked()
{
    apply();
}

void PDFAppAnnotationGeometryDialog::onDocumentChanged()
{
    if (m_isApplied || !isVisible())
    {
        return;
    }

    // The annotation is gone, or it cannot be changed any more
    const PDFAppAnnotationController* controller = m_controller.data();
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    if (!document || !document->isReady() || !controller->isAnnotationEditingAllowed() ||
        !PDFAppAnnotationOperations::canModifyAnnotation(document->getDocument(), m_annotation))
    {
        reject();
    }
}

void PDFAppAnnotationGeometryDialog::onControllerDestroyed()
{
    // The sender is dying: only the own state is changed, the dialog goes from the event loop
    m_controller = nullptr;
    if (isVisible())
    {
        reject();
    }
    deleteLater();
}

void PDFAppAnnotationGeometryDialog::onDialogFinished(int result)
{
    Q_UNUSED(result);
    if (m_controller)
    {
        m_controller->endMergeSession();
    }
    Q_EMIT dialogFinished(m_isApplied);
}

}   // namespace pdf
