// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "scaledialog.h"
#include "ui_scaledialog.h"

#include "pdfwidgetutils.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>

ScaleDialog::ScaleDialog(QWidget* parent, const DimensionScale& scale, Mode mode) :
    QDialog(parent),
    ui(new Ui::ScaleDialog),
    m_mode(mode),
    m_measuredLength(0.0)
{
    initialize(scale, mode);
}

ScaleDialog::ScaleDialog(QWidget* parent, pdf::PDFReal measuredLength, const DimensionScale& currentScale) :
    QDialog(parent),
    ui(new Ui::ScaleDialog),
    m_mode(Mode::Calibration),
    m_measuredLength(measuredLength)
{
    // The distance measured on the paper is given by the picked line, only its unit
    // is taken from the current scale. The real distance is left to the user.
    const DimensionUnit paperUnit = currentScale.getPaperUnit();
    DimensionScale scale(qMax(measuredLength * paperUnit.scale, 0.0001),
                         paperUnit.id,
                         currentScale.getRealValue(),
                         currentScale.getRealUnitId());

    initialize(scale, Mode::Calibration);
}

ScaleDialog::~ScaleDialog()
{
    delete ui;
}

void ScaleDialog::initialize(const DimensionScale& scale, Mode mode)
{
    ui->setupUi(this);

    m_lengthUnits = DimensionUnit::getLengthUnits();

    const DimensionScale effectiveScale = scale.isValid() ? scale : DimensionScale::createIdentity();

    initializeUnitComboBox(ui->paperUnitComboBox, effectiveScale.getPaperUnitId());
    initializeUnitComboBox(ui->realUnitComboBox, effectiveScale.getRealUnitId());

    ui->paperValueSpinBox->setValue(effectiveScale.getPaperValue());
    ui->realValueSpinBox->setValue(effectiveScale.getRealValue());
    ui->nameEdit->setText(effectiveScale.getName());
    ui->descriptionEdit->setText(effectiveScale.getDescription());

    if (mode == Mode::Calibration)
    {
        setWindowTitle(tr("Calibrate Scale"));
        ui->infoLabel->setText(tr("Enter the real distance between the two points you have picked in the document. "
                                  "The scale of the drawing is calculated from it. If you name the scale, it is stored "
                                  "as a preset and you can use it for other documents."));
        ui->realValueSpinBox->setFocus();
    }
    else
    {
        setWindowTitle(tr("Scale"));
        ui->infoLabel->setText(tr("Enter the distance measured on the paper and the real distance, which it represents. "
                                  "For example, a drawing in the scale 1:50 is defined as 1 mm = 50 mm."));
        ui->nameEdit->setFocus();
    }

    connect(ui->paperUnitComboBox, &QComboBox::currentIndexChanged, this, &ScaleDialog::onPaperUnitChanged);
    connect(ui->realUnitComboBox, &QComboBox::currentIndexChanged, this, &ScaleDialog::updateResultLabel);
    connect(ui->paperValueSpinBox, &QDoubleSpinBox::valueChanged, this, &ScaleDialog::updateResultLabel);
    connect(ui->realValueSpinBox, &QDoubleSpinBox::valueChanged, this, &ScaleDialog::updateResultLabel);

    updateResultLabel();

    pdf::PDFWidgetUtils::style(this);
}

void ScaleDialog::initializeUnitComboBox(QComboBox* comboBox, const QByteArray& currentUnitId)
{
    for (const DimensionUnit& unit : m_lengthUnits)
    {
        comboBox->addItem(unit.symbol, unit.id);
    }

    const int index = comboBox->findData(currentUnitId);
    comboBox->setCurrentIndex(index != -1 ? index : 0);
}

void ScaleDialog::onPaperUnitChanged()
{
    if (m_mode == Mode::Calibration && m_measuredLength > 0.0)
    {
        // The picked line has a fixed length, only the unit, in which it is
        // displayed, has changed
        const DimensionUnit unit = DimensionUnit::getLengthUnit(ui->paperUnitComboBox->currentData().toByteArray());
        QSignalBlocker blocker(ui->paperValueSpinBox);
        ui->paperValueSpinBox->setValue(m_measuredLength * unit.scale);
    }

    updateResultLabel();
}

void ScaleDialog::updateResultLabel()
{
    const DimensionScale scale = getScale();

    if (!scale.isValid())
    {
        ui->resultLabel->setText(QString());
        return;
    }

    QLocale locale;
    const pdf::PDFReal factor = scale.getScaleFactor();

    QString ratio;
    if (factor >= 1.0)
    {
        ratio = QString("1 : %1").arg(locale.toString(factor, 'g', 6));
    }
    else if (factor > 0.0)
    {
        ratio = QString("%1 : 1").arg(locale.toString(1.0 / factor, 'g', 6));
    }

    ui->resultLabel->setText(tr("Resulting scale: %1").arg(ratio));
}

DimensionScale ScaleDialog::getScale() const
{
    return DimensionScale(ui->paperValueSpinBox->value(),
                          ui->paperUnitComboBox->currentData().toByteArray(),
                          ui->realValueSpinBox->value(),
                          ui->realUnitComboBox->currentData().toByteArray(),
                          ui->nameEdit->text().trimmed(),
                          ui->descriptionEdit->text().trimmed());
}
