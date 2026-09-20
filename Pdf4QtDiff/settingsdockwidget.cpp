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

#include "settingsdockwidget.h"
#include "ui_settingsdockwidget.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"

#include <QScrollArea>
#include <QSignalBlocker>

namespace pdfdiff
{

SettingsDockWidget::SettingsDockWidget(Settings* settings, QWidget* parent) :
    QDockWidget(parent),
    ui(new Ui::SettingsDockWidget),
    m_settings(settings),
    m_loadingColors(false)
{
    ui->setupUi(this);

    auto colorNames = QColor::colorNames();
    for (QComboBox* comboBox : { ui->removeColorCombo, ui->addColorCombo, ui->replaceColorCombo, ui->moveColorCombo })
    {
        for (const QString& colorName : colorNames)
        {
            QColor color(colorName);
            comboBox->addItem(getIconForColor(color), colorName, color);
        }

        connect(comboBox, &QComboBox::editTextChanged, this, &SettingsDockWidget::onEditColorChanged);
    }

    connect(ui->transparencySlider, &QSlider::valueChanged, this, &SettingsDockWidget::transparencySliderChanged);
    connect(ui->transparencySlider, &QSlider::valueChanged, this, [this](int value)
    {
        ui->blendValueLabel->setText(tr("%1% / %2%").arg(100 - value).arg(value));
    });

    ui->overlayScaleModeCombo->addItem(tr("Original sizes"), int(OverlaySettings::ScaleMode::Original));
    ui->overlayScaleModeCombo->addItem(tr("Fit smaller page to larger"), int(OverlaySettings::ScaleMode::Fit));
    ui->overlayScaleModeCombo->addItem(tr("Manual scale"), int(OverlaySettings::ScaleMode::Manual));
    connect(ui->overlayScaleModeCombo, &QComboBox::currentIndexChanged, this, [this]()
    {
        updateOverlayControls();
        Q_EMIT overlaySettingsChanged();
    });
    for (QDoubleSpinBox* spinBox : { ui->leftScaleSpinBox, ui->rightScaleSpinBox,
                                   ui->horizontalOffsetSpinBox, ui->verticalOffsetSpinBox })
    {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, &SettingsDockWidget::overlaySettingsChanged);
    }
    connect(ui->resetOverlayButton, &QPushButton::clicked, this, &SettingsDockWidget::resetOverlay);
    resetOverlay();
    setOverlayEnabled(false);

    // Keep every setting reachable on small screens and at high display scaling.
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(widget());
    setWidget(scrollArea);

    toggleViewAction()->setText(tr("S&ettings"));
}

SettingsDockWidget::~SettingsDockWidget()
{
    delete ui;
}

void SettingsDockWidget::setCompareTextsAsVectorGraphics(bool enabled)
{
    ui->compareTextsAsVectorGraphicsCheckBox->setChecked(enabled);
}

bool SettingsDockWidget::isCompareTextAsVectorGraphics() const
{
    return ui->compareTextsAsVectorGraphicsCheckBox->isChecked();
}

void SettingsDockWidget::setCompareTextCharactersInsteadOfWords(bool enabled)
{
    ui->compareCharactersInsteadOfWordsCheckBox->setChecked(enabled);
}

bool SettingsDockWidget::isCompareTextCharactersInsteadOfWords() const
{
    return ui->compareCharactersInsteadOfWordsCheckBox->isChecked();
}

QLineEdit* SettingsDockWidget::getLeftPageSelectionEdit() const
{
    return ui->leftPageSelectionEdit;
}

QLineEdit* SettingsDockWidget::getRightPageSelectionEdit() const
{
    return ui->rightPageSelectionEdit;
}

void SettingsDockWidget::loadColors()
{
    pdf::PDFTemporaryValueChange guard(&m_loadingColors, true);

    auto loadColor = [](QComboBox* comboBox, QColor color)
    {
        auto index = comboBox->findData(color);
        comboBox->setCurrentIndex(index);

        if (index == -1)
        {
            comboBox->setCurrentText(color.name());
        }
    };

    loadColor(ui->removeColorCombo, m_settings->colorRemoved);
    loadColor(ui->addColorCombo, m_settings->colorAdded);
    loadColor(ui->replaceColorCombo, m_settings->colorReplaced);
    loadColor(ui->moveColorCombo, m_settings->colorPageMove);
}

int SettingsDockWidget::getTransparencySliderValue() const
{
    return ui->transparencySlider->value();
}

OverlaySettings SettingsDockWidget::getOverlaySettings() const
{
    OverlaySettings settings;
    settings.scaleMode = static_cast<OverlaySettings::ScaleMode>(ui->overlayScaleModeCombo->currentData().toInt());
    settings.leftScale = ui->leftScaleSpinBox->value() * 0.01;
    settings.rightScale = ui->rightScaleSpinBox->value() * 0.01;
    settings.rightOffsetMM = QPointF(ui->horizontalOffsetSpinBox->value(), ui->verticalOffsetSpinBox->value());
    return settings;
}

void SettingsDockWidget::setOverlayEnabled(bool enabled)
{
    ui->overlayControls->setEnabled(enabled);
    ui->overlayHintLabel->setVisible(!enabled);
}

void SettingsDockWidget::updateOverlayControls()
{
    const bool manual = getOverlaySettings().scaleMode == OverlaySettings::ScaleMode::Manual;
    for (QWidget* widget : QList<QWidget*>{ ui->leftScaleSpinBox, ui->rightScaleSpinBox,
                                         ui->leftScaleLabel, ui->rightScaleLabel })
    {
        widget->setEnabled(manual);
        widget->setVisible(manual);
    }
}

void SettingsDockWidget::resetOverlay()
{
    {
        const QSignalBlocker blocker(this);
        ui->overlayScaleModeCombo->setCurrentIndex(0);
        ui->leftScaleSpinBox->setValue(100.0);
        ui->rightScaleSpinBox->setValue(100.0);
        ui->horizontalOffsetSpinBox->setValue(0.0);
        ui->verticalOffsetSpinBox->setValue(0.0);
        ui->transparencySlider->setValue(50);
        updateOverlayControls();
    }
    Q_EMIT overlaySettingsChanged();
    Q_EMIT transparencySliderChanged(getTransparencySliderValue());
}

QIcon SettingsDockWidget::getIconForColor(QColor color) const
{
    QIcon icon;

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(16, 16));

    QPixmap pixmap(iconSize.width(), iconSize.height());
    pixmap.fill(color);
    icon.addPixmap(pixmap);

    return icon;
}

void SettingsDockWidget::onEditColorChanged()
{
    if (m_loadingColors)
    {
        return;
    }

    bool isChanged = false;
    auto saveColor = [&isChanged](QComboBox* comboBox, QColor& color)
    {
        QColor oldColor = color;
        color = QColor::fromString(comboBox->currentText());
        isChanged = isChanged || oldColor != color;
    };

    saveColor(ui->removeColorCombo, m_settings->colorRemoved);
    saveColor(ui->addColorCombo, m_settings->colorAdded);
    saveColor(ui->replaceColorCombo, m_settings->colorReplaced);
    saveColor(ui->moveColorCombo, m_settings->colorPageMove);

    if (isChanged)
    {
        Q_EMIT colorsChanged();
    }
}

}   // namespace pdfdiff
