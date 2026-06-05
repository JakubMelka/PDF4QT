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

#include "croppagesdialog.h"
#include "ui_croppagesdialog.h"

#include "pdfwidgetutils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QPixmap>
#include <QRadioButton>

namespace pdfpagemaster
{

CropPagesDialog::CropPagesDialog(QSizeF pageSizeMM, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::CropPagesDialog),
    m_pageSizeMM(pageSizeMM)
{
    ui->setupUi(this);
    ui->pageSizeValueLabel->setText(tr("%1 x %2 mm").arg(m_pageSizeMM.width(), 0, 'f', 1).arg(m_pageSizeMM.height(), 0, 'f', 1));
    ui->scopeComboBox->addItem(tr("Apply to selected pages"), false);
    ui->scopeComboBox->addItem(tr("Apply to all pages from same source"), true);

    const double maxMargin = qMax(0.0, qMin(m_pageSizeMM.width(), m_pageSizeMM.height()) / 2.0 - 0.5);
    for (QDoubleSpinBox* spinBox : { ui->equalMarginSpinBox, ui->cropLeftSpinBox, ui->cropTopSpinBox, ui->cropWidthSpinBox, ui->cropHeightSpinBox })
    {
        spinBox->setDecimals(1);
        spinBox->setSingleStep(1.0);
        spinBox->setSuffix(tr(" mm"));
    }
    ui->equalMarginSpinBox->setRange(0.0, maxMargin);
    ui->equalMarginSpinBox->setValue(qMin(5.0, maxMargin));
    ui->cropLeftSpinBox->setRange(0.0, qMax(0.0, m_pageSizeMM.width() - 1.0));
    ui->cropTopSpinBox->setRange(0.0, qMax(0.0, m_pageSizeMM.height() - 1.0));
    ui->cropWidthSpinBox->setRange(1.0, qMax(1.0, m_pageSizeMM.width()));
    ui->cropHeightSpinBox->setRange(1.0, qMax(1.0, m_pageSizeMM.height()));
    ui->cropWidthSpinBox->setValue(qMax(1.0, m_pageSizeMM.width()));
    ui->cropHeightSpinBox->setValue(qMax(1.0, m_pageSizeMM.height()));

    connect(ui->equalMarginsRadioButton, &QRadioButton::toggled, this, &CropPagesDialog::updateState);
    connect(ui->manualBoxRadioButton, &QRadioButton::toggled, this, &CropPagesDialog::updateState);
    connect(ui->equalMarginSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CropPagesDialog::updateState);
    connect(ui->cropLeftSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CropPagesDialog::updateState);
    connect(ui->cropTopSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CropPagesDialog::updateState);
    connect(ui->cropWidthSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CropPagesDialog::updateState);
    connect(ui->cropHeightSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CropPagesDialog::updateState);

    QSize size = pdf::PDFWidgetUtils::scaleDPI(this, QSize(460, 520));
    resize(size);
    pdf::PDFWidgetUtils::style(this);
    updateState();
}

CropPagesDialog::~CropPagesDialog()
{
    delete ui;
}

QMarginsF CropPagesDialog::getCropMarginsMM() const
{
    if (ui->equalMarginsRadioButton->isChecked())
    {
        const double margin = ui->equalMarginSpinBox->value();
        return QMarginsF(margin, margin, margin, margin);
    }

    const double left = ui->cropLeftSpinBox->value();
    const double top = ui->cropTopSpinBox->value();
    const double right = qMax(0.0, m_pageSizeMM.width() - left - ui->cropWidthSpinBox->value());
    const double bottom = qMax(0.0, m_pageSizeMM.height() - top - ui->cropHeightSpinBox->value());
    return QMarginsF(left, top, right, bottom);
}

bool CropPagesDialog::isApplyToSameSource() const
{
    return ui->scopeComboBox->currentData().toBool();
}

void CropPagesDialog::updateState()
{
    const bool isManual = ui->manualBoxRadioButton->isChecked();
    ui->equalMarginSpinBox->setEnabled(!isManual);
    ui->cropLeftSpinBox->setEnabled(isManual);
    ui->cropTopSpinBox->setEnabled(isManual);
    ui->cropWidthSpinBox->setEnabled(isManual);
    ui->cropHeightSpinBox->setEnabled(isManual);

    const double maxWidth = qMax(1.0, m_pageSizeMM.width() - ui->cropLeftSpinBox->value());
    const double maxHeight = qMax(1.0, m_pageSizeMM.height() - ui->cropTopSpinBox->value());
    ui->cropWidthSpinBox->setMaximum(maxWidth);
    ui->cropHeightSpinBox->setMaximum(maxHeight);

    const QSizeF croppedSize(m_pageSizeMM.width() - getCropMarginsMM().left() - getCropMarginsMM().right(),
                             m_pageSizeMM.height() - getCropMarginsMM().top() - getCropMarginsMM().bottom());
    ui->resultValueLabel->setText(tr("%1 x %2 mm").arg(croppedSize.width(), 0, 'f', 1).arg(croppedSize.height(), 0, 'f', 1));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(croppedSize.width() > 0.0 && croppedSize.height() > 0.0);
    updatePreview();
}

void CropPagesDialog::updatePreview()
{
    QSize previewSize = ui->previewLabel->size();
    if (!previewSize.isValid() || previewSize.width() <= 1 || previewSize.height() <= 1)
    {
        previewSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(220, 150));
    }

    QPixmap pixmap(previewSize);
    pixmap.fill(ui->previewLabel->palette().window().color());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF availableRect = pixmap.rect().adjusted(12.0, 12.0, -12.0, -12.0);
    if (m_pageSizeMM.isEmpty() || !availableRect.isValid())
    {
        ui->previewLabel->setPixmap(pixmap);
        return;
    }

    QSizeF scaledPageSize = m_pageSizeMM;
    scaledPageSize.scale(availableRect.size(), Qt::KeepAspectRatio);
    QRectF pageRect(QPointF(0.0, 0.0), scaledPageSize);
    pageRect.moveCenter(availableRect.center());

    painter.setPen(QPen(QColor(120, 120, 120), 1.0));
    painter.setBrush(Qt::white);
    painter.drawRect(pageRect);

    const QMarginsF cropMargins = getCropMarginsMM();
    QRectF cropRect = pageRect.adjusted(cropMargins.left() / m_pageSizeMM.width() * pageRect.width(),
                                        cropMargins.top() / m_pageSizeMM.height() * pageRect.height(),
                                        -cropMargins.right() / m_pageSizeMM.width() * pageRect.width(),
                                        -cropMargins.bottom() / m_pageSizeMM.height() * pageRect.height());
    cropRect = cropRect.intersected(pageRect);

    if (cropRect.isValid())
    {
        QPainterPath removedArea;
        removedArea.addRect(pageRect);
        QPainterPath keptArea;
        keptArea.addRect(cropRect);
        removedArea = removedArea.subtracted(keptArea);

        painter.fillPath(removedArea, QColor(210, 80, 80, 70));
        painter.fillRect(cropRect, QColor(80, 145, 220, 35));
        painter.setPen(QPen(QColor(65, 120, 190), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cropRect);
    }

    ui->previewLabel->setPixmap(pixmap);
}

}   // namespace pdfpagemaster
