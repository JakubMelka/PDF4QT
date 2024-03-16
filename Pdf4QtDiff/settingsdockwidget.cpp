//    Copyright (C) 2021 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "settingsdockwidget.h"
#include "ui_settingsdockwidget.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"

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
    for (QComboBox* comboBox : findChildren<QComboBox*>())
    {
        for (const QString& colorName : colorNames)
        {
            QColor color(colorName);
            comboBox->addItem(getIconForColor(color), colorName, color);
        }

        connect(comboBox, &QComboBox::editTextChanged, this, &SettingsDockWidget::onEditColorChanged);
    }

    connect(ui->transparencySlider, &QSlider::valueChanged, this, &SettingsDockWidget::transparencySliderChanged);
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
        color.fromString(comboBox->currentText());
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
