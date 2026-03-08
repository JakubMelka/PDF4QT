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

#include "pdfpagegeometrydialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QVBoxLayout>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

QDoubleSpinBox* createMillimeterSpinBox(QWidget* parent, double value = 0.0)
{
    QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(-10000.0, 10000.0);
    spinBox->setDecimals(2);
    spinBox->setValue(value);
    spinBox->setSuffix(QObject::tr(" mm"));
    return spinBox;
}

}   // namespace

PDFPageGeometryDialog::PDFPageGeometryDialog(QWidget* parent) :
    QDialog(parent),
    m_pageCountLabel(nullptr),
    m_pageRangeEdit(nullptr),
    m_pageSubsetCombo(nullptr),
    m_referenceBoxCombo(nullptr),
    m_applyMediaBoxCheckBox(nullptr),
    m_applyCropBoxCheckBox(nullptr),
    m_applyBleedBoxCheckBox(nullptr),
    m_applyTrimBoxCheckBox(nullptr),
    m_applyArtBoxCheckBox(nullptr),
    m_useTargetPageSizeCheckBox(nullptr),
    m_pageSizePresetCombo(nullptr),
    m_widthSpinBox(nullptr),
    m_heightSpinBox(nullptr),
    m_marginLeftSpinBox(nullptr),
    m_marginTopSpinBox(nullptr),
    m_marginRightSpinBox(nullptr),
    m_marginBottomSpinBox(nullptr),
    m_anchorCombo(nullptr),
    m_offsetXSpinBox(nullptr),
    m_offsetYSpinBox(nullptr),
    m_scaleContentCheckBox(nullptr),
    m_preserveAspectRatioCheckBox(nullptr),
    m_scaleAnnotationsAndFormFieldsCheckBox(nullptr)
{
    setWindowTitle(tr("Page Geometry"));
    setModal(true);
    resize(680, 560);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGroupBox* pageFilterGroup = new QGroupBox(tr("Pages"), this);
    QFormLayout* pageFilterLayout = new QFormLayout(pageFilterGroup);
    m_pageCountLabel = new QLabel(tr("Document page count: 0"), pageFilterGroup);
    m_pageRangeEdit = new QLineEdit("-", pageFilterGroup);
    m_pageSubsetCombo = new QComboBox(pageFilterGroup);
    m_pageSubsetCombo->addItem(tr("All pages"), int(PDFPageGeometrySettings::PageSubset::AllPages));
    m_pageSubsetCombo->addItem(tr("Odd pages"), int(PDFPageGeometrySettings::PageSubset::OddPages));
    m_pageSubsetCombo->addItem(tr("Even pages"), int(PDFPageGeometrySettings::PageSubset::EvenPages));
    m_pageSubsetCombo->addItem(tr("Portrait pages"), int(PDFPageGeometrySettings::PageSubset::PortraitPages));
    m_pageSubsetCombo->addItem(tr("Landscape pages"), int(PDFPageGeometrySettings::PageSubset::LandscapePages));
    pageFilterLayout->addRow(m_pageCountLabel);
    pageFilterLayout->addRow(tr("Page range"), m_pageRangeEdit);
    pageFilterLayout->addRow(tr("Subset"), m_pageSubsetCombo);
    mainLayout->addWidget(pageFilterGroup);

    QGroupBox* boxGroup = new QGroupBox(tr("Reference and Target Boxes"), this);
    QGridLayout* boxLayout = new QGridLayout(boxGroup);
    m_referenceBoxCombo = new QComboBox(boxGroup);
    m_referenceBoxCombo->addItem(tr("Media box"), int(PDFPageGeometrySettings::ReferenceBox::MediaBox));
    m_referenceBoxCombo->addItem(tr("Crop box"), int(PDFPageGeometrySettings::ReferenceBox::CropBox));
    m_referenceBoxCombo->addItem(tr("Bleed box"), int(PDFPageGeometrySettings::ReferenceBox::BleedBox));
    m_referenceBoxCombo->addItem(tr("Trim box"), int(PDFPageGeometrySettings::ReferenceBox::TrimBox));
    m_referenceBoxCombo->addItem(tr("Art box"), int(PDFPageGeometrySettings::ReferenceBox::ArtBox));

    m_applyMediaBoxCheckBox = new QCheckBox(tr("Media"), boxGroup);
    m_applyCropBoxCheckBox = new QCheckBox(tr("Crop"), boxGroup);
    m_applyBleedBoxCheckBox = new QCheckBox(tr("Bleed"), boxGroup);
    m_applyTrimBoxCheckBox = new QCheckBox(tr("Trim"), boxGroup);
    m_applyArtBoxCheckBox = new QCheckBox(tr("Art"), boxGroup);
    m_applyMediaBoxCheckBox->setChecked(true);
    m_applyCropBoxCheckBox->setChecked(true);

    boxLayout->addWidget(new QLabel(tr("Reference box"), boxGroup), 0, 0);
    boxLayout->addWidget(m_referenceBoxCombo, 0, 1, 1, 4);
    boxLayout->addWidget(new QLabel(tr("Apply to"), boxGroup), 1, 0);
    boxLayout->addWidget(m_applyMediaBoxCheckBox, 1, 1);
    boxLayout->addWidget(m_applyCropBoxCheckBox, 1, 2);
    boxLayout->addWidget(m_applyBleedBoxCheckBox, 1, 3);
    boxLayout->addWidget(m_applyTrimBoxCheckBox, 1, 4);
    boxLayout->addWidget(m_applyArtBoxCheckBox, 1, 5);
    mainLayout->addWidget(boxGroup);

    QGroupBox* sizeGroup = new QGroupBox(tr("Size and Margins"), this);
    QGridLayout* sizeLayout = new QGridLayout(sizeGroup);

    m_useTargetPageSizeCheckBox = new QCheckBox(tr("Use target page size"), sizeGroup);
    m_pageSizePresetCombo = new QComboBox(sizeGroup);
    m_pageSizePresetCombo->addItem(tr("Custom"), QSizeF());
    m_pageSizePresetCombo->addItem(tr("A5 148 x 210"), QSizeF(148.0, 210.0));
    m_pageSizePresetCombo->addItem(tr("A4 210 x 297"), QSizeF(210.0, 297.0));
    m_pageSizePresetCombo->addItem(tr("A3 297 x 420"), QSizeF(297.0, 420.0));
    m_pageSizePresetCombo->addItem(tr("Letter 216 x 279"), QSizeF(216.0, 279.0));
    m_pageSizePresetCombo->addItem(tr("Legal 216 x 356"), QSizeF(216.0, 356.0));
    m_pageSizePresetCombo->setCurrentIndex(2);

    m_widthSpinBox = createMillimeterSpinBox(sizeGroup, 210.0);
    m_heightSpinBox = createMillimeterSpinBox(sizeGroup, 297.0);
    m_widthSpinBox->setRange(1.0, 10000.0);
    m_heightSpinBox->setRange(1.0, 10000.0);

    m_marginLeftSpinBox = createMillimeterSpinBox(sizeGroup, 0.0);
    m_marginTopSpinBox = createMillimeterSpinBox(sizeGroup, 0.0);
    m_marginRightSpinBox = createMillimeterSpinBox(sizeGroup, 0.0);
    m_marginBottomSpinBox = createMillimeterSpinBox(sizeGroup, 0.0);

    sizeLayout->addWidget(m_useTargetPageSizeCheckBox, 0, 0, 1, 3);
    sizeLayout->addWidget(new QLabel(tr("Preset"), sizeGroup), 1, 0);
    sizeLayout->addWidget(m_pageSizePresetCombo, 1, 1, 1, 2);
    sizeLayout->addWidget(new QLabel(tr("Width"), sizeGroup), 2, 0);
    sizeLayout->addWidget(m_widthSpinBox, 2, 1);
    sizeLayout->addWidget(new QLabel(tr("Height"), sizeGroup), 2, 2);
    sizeLayout->addWidget(m_heightSpinBox, 2, 3);
    sizeLayout->addWidget(new QLabel(tr("Left margin"), sizeGroup), 3, 0);
    sizeLayout->addWidget(m_marginLeftSpinBox, 3, 1);
    sizeLayout->addWidget(new QLabel(tr("Top margin"), sizeGroup), 3, 2);
    sizeLayout->addWidget(m_marginTopSpinBox, 3, 3);
    sizeLayout->addWidget(new QLabel(tr("Right margin"), sizeGroup), 4, 0);
    sizeLayout->addWidget(m_marginRightSpinBox, 4, 1);
    sizeLayout->addWidget(new QLabel(tr("Bottom margin"), sizeGroup), 4, 2);
    sizeLayout->addWidget(m_marginBottomSpinBox, 4, 3);
    mainLayout->addWidget(sizeGroup);

    QGroupBox* placementGroup = new QGroupBox(tr("Placement and Content"), this);
    QGridLayout* placementLayout = new QGridLayout(placementGroup);

    m_anchorCombo = new QComboBox(placementGroup);
    m_anchorCombo->addItem(tr("Top left"), int(PDFPageGeometrySettings::Anchor::TopLeft));
    m_anchorCombo->addItem(tr("Top center"), int(PDFPageGeometrySettings::Anchor::TopCenter));
    m_anchorCombo->addItem(tr("Top right"), int(PDFPageGeometrySettings::Anchor::TopRight));
    m_anchorCombo->addItem(tr("Middle left"), int(PDFPageGeometrySettings::Anchor::MiddleLeft));
    m_anchorCombo->addItem(tr("Middle center"), int(PDFPageGeometrySettings::Anchor::MiddleCenter));
    m_anchorCombo->addItem(tr("Middle right"), int(PDFPageGeometrySettings::Anchor::MiddleRight));
    m_anchorCombo->addItem(tr("Bottom left"), int(PDFPageGeometrySettings::Anchor::BottomLeft));
    m_anchorCombo->addItem(tr("Bottom center"), int(PDFPageGeometrySettings::Anchor::BottomCenter));
    m_anchorCombo->addItem(tr("Bottom right"), int(PDFPageGeometrySettings::Anchor::BottomRight));
    m_anchorCombo->setCurrentIndex(4);

    m_offsetXSpinBox = createMillimeterSpinBox(placementGroup, 0.0);
    m_offsetYSpinBox = createMillimeterSpinBox(placementGroup, 0.0);

    m_scaleContentCheckBox = new QCheckBox(tr("Scale content"), placementGroup);
    m_preserveAspectRatioCheckBox = new QCheckBox(tr("Preserve aspect ratio"), placementGroup);
    m_scaleAnnotationsAndFormFieldsCheckBox = new QCheckBox(tr("Scale comments and form fields"), placementGroup);
    m_preserveAspectRatioCheckBox->setChecked(true);
    m_scaleAnnotationsAndFormFieldsCheckBox->setChecked(true);

    placementLayout->addWidget(new QLabel(tr("Anchor"), placementGroup), 0, 0);
    placementLayout->addWidget(m_anchorCombo, 0, 1, 1, 3);
    placementLayout->addWidget(new QLabel(tr("Offset X"), placementGroup), 1, 0);
    placementLayout->addWidget(m_offsetXSpinBox, 1, 1);
    placementLayout->addWidget(new QLabel(tr("Offset Y"), placementGroup), 1, 2);
    placementLayout->addWidget(m_offsetYSpinBox, 1, 3);
    placementLayout->addWidget(m_scaleContentCheckBox, 2, 0, 1, 4);
    placementLayout->addWidget(m_preserveAspectRatioCheckBox, 3, 0, 1, 4);
    placementLayout->addWidget(m_scaleAnnotationsAndFormFieldsCheckBox, 4, 0, 1, 4);
    mainLayout->addWidget(placementGroup);

    QLabel* infoLabel = new QLabel(tr("Tip: if content scaling is disabled, page geometry is adjusted only by page boxes (non-destructive)."), this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &PDFPageGeometryDialog::onAcceptClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    connect(m_pageSizePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageGeometryDialog::onPageSizePresetChanged);
    connect(m_useTargetPageSizeCheckBox, &QCheckBox::toggled, this, &PDFPageGeometryDialog::onUseTargetPageSizeChanged);
    connect(m_scaleContentCheckBox, &QCheckBox::toggled, this, &PDFPageGeometryDialog::onScaleContentChanged);

    updateUi();
}

void PDFPageGeometryDialog::setPageCount(PDFInteger pageCount)
{
    m_pageCountLabel->setText(tr("Document page count: %1").arg(pageCount));
}

void PDFPageGeometryDialog::setSettings(const PDFPageGeometrySettings& settings)
{
    m_pageRangeEdit->setText(settings.pageRange);
    m_pageSubsetCombo->setCurrentIndex(m_pageSubsetCombo->findData(int(settings.pageSubset)));
    m_referenceBoxCombo->setCurrentIndex(m_referenceBoxCombo->findData(int(settings.referenceBox)));

    m_applyMediaBoxCheckBox->setChecked(settings.applyMediaBox);
    m_applyCropBoxCheckBox->setChecked(settings.applyCropBox);
    m_applyBleedBoxCheckBox->setChecked(settings.applyBleedBox);
    m_applyTrimBoxCheckBox->setChecked(settings.applyTrimBox);
    m_applyArtBoxCheckBox->setChecked(settings.applyArtBox);

    m_useTargetPageSizeCheckBox->setChecked(settings.useTargetPageSize);
    setPageSizeInMM(settings.targetPageSizeMM);
    int pageSizePresetIndex = 0;
    for (int i = 1; i < m_pageSizePresetCombo->count(); ++i)
    {
        const QSizeF preset = m_pageSizePresetCombo->itemData(i).toSizeF();
        if (qAbs(preset.width() - settings.targetPageSizeMM.width()) <= 0.01 &&
            qAbs(preset.height() - settings.targetPageSizeMM.height()) <= 0.01)
        {
            pageSizePresetIndex = i;
            break;
        }
    }
    m_pageSizePresetCombo->setCurrentIndex(pageSizePresetIndex);

    m_marginLeftSpinBox->setValue(settings.marginsMM.left());
    m_marginTopSpinBox->setValue(settings.marginsMM.top());
    m_marginRightSpinBox->setValue(settings.marginsMM.right());
    m_marginBottomSpinBox->setValue(settings.marginsMM.bottom());

    m_anchorCombo->setCurrentIndex(m_anchorCombo->findData(int(settings.anchor)));
    m_offsetXSpinBox->setValue(settings.offsetMM.x());
    m_offsetYSpinBox->setValue(settings.offsetMM.y());

    m_scaleContentCheckBox->setChecked(settings.scaleContent);
    m_preserveAspectRatioCheckBox->setChecked(settings.preserveAspectRatio);
    m_scaleAnnotationsAndFormFieldsCheckBox->setChecked(settings.scaleAnnotationsAndFormFields);
    updateUi();
}

PDFPageGeometrySettings PDFPageGeometryDialog::getSettings() const
{
    PDFPageGeometrySettings settings;
    settings.pageRange = m_pageRangeEdit->text();
    settings.pageSubset = PDFPageGeometrySettings::PageSubset(m_pageSubsetCombo->currentData().toInt());
    settings.referenceBox = PDFPageGeometrySettings::ReferenceBox(m_referenceBoxCombo->currentData().toInt());

    settings.applyMediaBox = m_applyMediaBoxCheckBox->isChecked();
    settings.applyCropBox = m_applyCropBoxCheckBox->isChecked();
    settings.applyBleedBox = m_applyBleedBoxCheckBox->isChecked();
    settings.applyTrimBox = m_applyTrimBoxCheckBox->isChecked();
    settings.applyArtBox = m_applyArtBoxCheckBox->isChecked();

    settings.useTargetPageSize = m_useTargetPageSizeCheckBox->isChecked();
    settings.targetPageSizeMM = getPageSizeInMM();
    settings.marginsMM = QMarginsF(m_marginLeftSpinBox->value(),
                                   m_marginTopSpinBox->value(),
                                   m_marginRightSpinBox->value(),
                                   m_marginBottomSpinBox->value());

    settings.anchor = PDFPageGeometrySettings::Anchor(m_anchorCombo->currentData().toInt());
    settings.offsetMM = QPointF(m_offsetXSpinBox->value(), m_offsetYSpinBox->value());

    settings.scaleContent = m_scaleContentCheckBox->isChecked();
    settings.preserveAspectRatio = m_preserveAspectRatioCheckBox->isChecked();
    settings.scaleAnnotationsAndFormFields = m_scaleAnnotationsAndFormFieldsCheckBox->isChecked();
    return settings;
}

void PDFPageGeometryDialog::onPageSizePresetChanged()
{
    const QSizeF presetSize = m_pageSizePresetCombo->currentData().toSizeF();
    if (presetSize.isValid())
    {
        setPageSizeInMM(presetSize);
    }
}

void PDFPageGeometryDialog::onUseTargetPageSizeChanged()
{
    updateUi();
}

void PDFPageGeometryDialog::onScaleContentChanged()
{
    updateUi();
}

void PDFPageGeometryDialog::onAcceptClicked()
{
    const PDFPageGeometrySettings settings = getSettings();

    if (!settings.hasAnyTargetBoxSelected())
    {
        QMessageBox::critical(this, tr("Error"), tr("Select at least one target page box."));
        return;
    }

    if (settings.useTargetPageSize)
    {
        const QSizeF pageSizeMM = settings.targetPageSizeMM;
        if (pageSizeMM.width() <= 0.0 || pageSizeMM.height() <= 0.0)
        {
            QMessageBox::critical(this, tr("Error"), tr("Target page size must be greater than zero."));
            return;
        }
    }

    accept();
}

void PDFPageGeometryDialog::setPageSizeInMM(const QSizeF& size)
{
    m_widthSpinBox->setValue(size.width());
    m_heightSpinBox->setValue(size.height());
}

QSizeF PDFPageGeometryDialog::getPageSizeInMM() const
{
    return QSizeF(m_widthSpinBox->value(), m_heightSpinBox->value());
}

void PDFPageGeometryDialog::updateUi()
{
    const bool useTargetPageSize = m_useTargetPageSizeCheckBox->isChecked();
    m_pageSizePresetCombo->setEnabled(useTargetPageSize);
    m_widthSpinBox->setEnabled(useTargetPageSize);
    m_heightSpinBox->setEnabled(useTargetPageSize);

    const bool scaleContent = m_scaleContentCheckBox->isChecked();
    m_preserveAspectRatioCheckBox->setEnabled(scaleContent);
    m_scaleAnnotationsAndFormFieldsCheckBox->setEnabled(scaleContent);
}

}   // namespace pdf
