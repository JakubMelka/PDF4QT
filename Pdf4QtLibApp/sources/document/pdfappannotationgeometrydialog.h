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

#ifndef PDFAPPANNOTATIONGEOMETRYDIALOG_H
#define PDFAPPANNOTATIONGEOMETRYDIALOG_H

#include "pdfappglobal.h"

#include "pdfannotationmanipulator.h"
#include "pdfmessagebox.h"
#include "pdfobject.h"

#include <QPointer>
#include <QRectF>

#include <array>
#include <vector>

class QCheckBox;
class QHBoxLayout;
class QScrollArea;
class QVBoxLayout;

namespace pdf
{

class PDFAppAnnotationController;
class PDFDoubleSpinBox;
class PDFLabel;
class PDFReferencePointPicker;
class PDFSegmentedControl;

/// Geometry dialog of an annotation (Figma 1024:21252, 520 x 446; points 1027:22165):
/// units (pt / mm / in), the reference point (PDFReferencePointPicker), X / Y, W / H
/// measured from the bottom left corner of the page, Keep aspect ratio, the rotation
/// in 90 degree steps (or by any angle for the shapes, which turn by any angle) and
/// the table of the points of an annotation with points. The rectangle and the points
/// are two groups: after the first change the other group is disabled. Apply = one
/// undo step. A dialog of the design on PDFMessageBox, modal over the scrim; it
/// deletes itself, when it closes, and closes, when its annotation disappears.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationGeometryDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    enum class Unit
    {
        Points,
        Millimeters,
        Inches
    };

    /// Geometry of 1024:21754
    static constexpr int CARD_WIDTH = 520;
    static constexpr int LABEL_WIDTH = 104;
    static constexpr int FIELD_WIDTH = 120;

    /// Most points shown in the table (an ink of hundreds of points is not edited here)
    static constexpr size_t MAXIMUM_POINTS = 64;

    explicit PDFAppAnnotationGeometryDialog(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent);
    virtual ~PDFAppAnnotationGeometryDialog() override;

    /// Opens the modal dialog over \p parent (the window of the view of the
    /// controller), the dialog deletes itself, when it is closed. \returns the
    /// dialog, or nullptr, when it cannot be opened (no annotation, no document,
    /// the annotation or the document cannot be changed).
    static PDFAppAnnotationGeometryDialog* open(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent);

    PDFAppAnnotationController* getController() const;
    PDFObjectReference getAnnotation() const { return m_annotation; }

    Unit getUnit() const { return m_unit; }
    void setUnit(Unit unit);

    /// Rectangle of the annotation (page coordinates) and the rectangle of the fields
    const QRectF& getOriginalRectangle() const { return m_originalRectangle; }
    const QRectF& getRectangle() const { return m_rectangle; }

    /// Rotation clockwise in degrees (0, 90, 180, 270 or any angle)
    double getRotation() const;

    /// Points of the table (page coordinates)
    const std::vector<QPointF>& getPoints() const { return m_points; }

    /// Factor from points to the unit
    static double getUnitFactor(Unit unit);

    /// Apply: the change as one undo step, the dialog closes. \returns false,
    /// when the change is refused (the dialog stays open)
    bool apply();

    PDFReferencePointPicker* getReferencePointPicker() const;
    PDFSegmentedControl* getUnitControl() const;
    PDFDoubleSpinBox* getField(const QString& name) const;
    QCheckBox* getKeepAspectRatioCheckBox() const;
    PDFSegmentedControl* getRotationControl() const;
    PDFDoubleSpinBox* getAngleSpinBox() const;
    PDFDoubleSpinBox* getPointField(size_t index, bool isY) const;

Q_SIGNALS:
    /// The dialog has been closed (\p isApplied = the change has been committed)
    void dialogFinished(bool isApplied);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void createBody();
    void retranslate();
    PDFDoubleSpinBox* createField(const QString& objectName);
    QHBoxLayout* createRow(QVBoxLayout* layout, const QString& label, bool alignTop = false);
    void updateFields();
    void updateGroups();
    QPointF getReferencePoint(const QRectF& rectangle) const;

    void onUnitChanged(int index);
    void onReferencePointChanged(int index);
    void onRectangleFieldChanged();
    void onRotationChanged();
    void onPointFieldChanged();
    void onApplyClicked();
    void onDocumentChanged();
    void onDialogFinished(int result);
    void onControllerDestroyed();

    QPointer<PDFAppAnnotationController> m_controller;
    PDFObjectReference m_annotation;
    PDFAnnotationManipulator::Capabilities m_capabilities;
    QRectF m_originalRectangle;
    QRectF m_rectangle;
    std::vector<QPointF> m_originalPoints;
    std::vector<QPointF> m_points;
    std::array<double, 4> m_shownValues = { };  ///< Values shown by X, Y, W, H (rounded to the decimals of the fields)
    std::vector<QPointF> m_shownPoints;         ///< Values shown by the fields of the points
    Unit m_unit = Unit::Millimeters;
    bool m_isRectangleChanged = false;
    bool m_isPointsChanged = false;
    bool m_isUpdating = false;
    bool m_isApplied = false;

    QPointer<PDFSegmentedControl> m_unitControl;
    QPointer<PDFReferencePointPicker> m_referencePicker;
    QPointer<PDFLabel> m_referenceHint;
    QPointer<PDFDoubleSpinBox> m_xField;
    QPointer<PDFDoubleSpinBox> m_yField;
    QPointer<PDFDoubleSpinBox> m_widthField;
    QPointer<PDFDoubleSpinBox> m_heightField;
    QPointer<QCheckBox> m_keepAspectRatioCheckBox;
    QPointer<PDFSegmentedControl> m_rotationControl;
    QPointer<PDFDoubleSpinBox> m_angleSpinBox;
    std::vector<std::pair<QPointer<PDFDoubleSpinBox>, QPointer<PDFDoubleSpinBox>>> m_pointFields;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QPushButton> m_applyButton;
    QPointer<QPushButton> m_cancelButton;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONGEOMETRYDIALOG_H
