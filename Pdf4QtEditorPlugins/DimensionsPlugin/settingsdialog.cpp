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

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "scaledialog.h"

#include "pdfwidgetutils.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFontDialog>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>

namespace
{

/// Creates the icon, which displays the color. The color is drawn over a checker
/// board, so the user is able to recognize, how transparent it is.
/// \param color Displayed color
/// \param size Size of the icon
QIcon createColorIcon(const QColor& color, QSize size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);

    const int checkerSize = qMax(size.height() / 3, 2);
    for (int y = 0; y < size.height(); y += checkerSize)
    {
        for (int x = 0; x < size.width(); x += checkerSize)
        {
            const bool isLight = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;
            painter.fillRect(QRect(x, y, checkerSize, checkerSize), isLight ? QColor(255, 255, 255) : QColor(205, 205, 205));
        }
    }

    if (color.isValid())
    {
        painter.fillRect(pixmap.rect(), color);
    }

    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(QRect(QPoint(0, 0), size - QSize(1, 1)));

    return QIcon(pixmap);
}

}   // namespace

SettingsDialog::SettingsDialog(QWidget* parent, pdfplugin::DimensionsPluginSettings& originalSettings) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_originalSettings(originalSettings)
{
    ui->setupUi(this);

    m_updatedSettings = m_originalSettings;

    m_lengthUnits = DimensionUnit::getLengthUnits();
    m_areaUnits = DimensionUnit::getAreaUnits();
    m_angleUnits = DimensionUnit::getAngleUnits();

    initComboBox(m_lengthUnits, m_updatedSettings.lengthUnit, ui->lengthsComboBox);
    initComboBox(m_areaUnits, m_updatedSettings.areaUnit, ui->areasComboBox);
    initComboBox(m_angleUnits, m_updatedSettings.angleUnit, ui->anglesComboBox);

    ui->storageComboBox->addItem(tr("Temporary measurements"), int(pdfplugin::DimensionsPluginSettings::StorageMode::Temporary));
    ui->storageComboBox->addItem(tr("Annotations in the document"), int(pdfplugin::DimensionsPluginSettings::StorageMode::Annotations));
    ui->storageComboBox->setCurrentIndex(ui->storageComboBox->findData(int(m_updatedSettings.storageMode)));

    ui->scalePerDocumentCheckBox->setChecked(m_updatedSettings.isScaleStoredPerDocument);
    ui->fontComboBox->setCurrentFont(m_updatedSettings.font);

    updateScaleEdit();
    updateStorageDescription();
    updateColorButtons();

    connect(ui->fontComboBox, &QFontComboBox::currentFontChanged, this, &SettingsDialog::setFont);
    connect(ui->changeScaleButton, &QPushButton::clicked, this, &SettingsDialog::onChangeScaleTriggered);
    connect(ui->storageComboBox, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateStorageDescription);
    connect(ui->textColorButton, &QPushButton::clicked, this, [this]() {
        QColor color = selectColor(m_updatedSettings.textColor, tr("Select Text Color"));
        if (color.isValid())
        {
            setTextColor(color);
            updateColorButtons();
        }
    });
    connect(ui->backgroundColorButton, &QPushButton::clicked, this, [this]() {
        QColor color = selectColor(m_updatedSettings.backgroundColor, tr("Select Background Color"));
        if (color.isValid())
        {
            setBackgroundColor(color);
            updateColorButtons();
        }
    });
    connect(ui->selectFontButton, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QFont font = QFontDialog::getFont(&ok, m_updatedSettings.font, this, tr("Select Font"));
        if (ok)
        {
            setFont(font);
            ui->fontComboBox->setCurrentFont(font);
        }
    });

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 480)));
    pdf::PDFWidgetUtils::style(this);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::initComboBox(const DimensionUnits& units, const DimensionUnit& currentUnit, QComboBox* comboBox)
{
    for (const DimensionUnit& unit : units)
    {
        comboBox->addItem(unit.symbol, unit.id);
    }

    const int index = comboBox->findData(currentUnit.id);
    comboBox->setCurrentIndex(index != -1 ? index : 0);
}

void SettingsDialog::updateColorButtons()
{
    const QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(32, 16));

    ui->textColorButton->setIconSize(iconSize);
    ui->textColorButton->setIcon(createColorIcon(m_updatedSettings.textColor, iconSize));

    ui->backgroundColorButton->setIconSize(iconSize);
    ui->backgroundColorButton->setIcon(createColorIcon(m_updatedSettings.backgroundColor, iconSize));
}

QColor SettingsDialog::selectColor(const QColor& color, const QString& title)
{
    QColor initialColor = color.isValid() ? color : QColor(Qt::black);

    if (initialColor.alpha() == 0)
    {
        // A fully transparent color is not drawn at all. If it was offered as it is,
        // then the alpha channel of the color dialog would silently stay at zero and
        // the newly picked color would still be invisible.
        initialColor.setAlpha(255);
    }

    return QColorDialog::getColor(initialColor, this, title, QColorDialog::ShowAlphaChannel);
}

void SettingsDialog::updateScaleEdit()
{
    ui->scaleEdit->setText(m_updatedSettings.defaultScale.getDisplayName());
    ui->scaleEdit->setToolTip(m_updatedSettings.defaultScale.getRatioText());
}

void SettingsDialog::updateStorageDescription()
{
    const auto storageMode = pdfplugin::DimensionsPluginSettings::StorageMode(ui->storageComboBox->currentData().toInt());

    if (storageMode == pdfplugin::DimensionsPluginSettings::StorageMode::Annotations)
    {
        ui->storageDescriptionLabel->setText(tr("Measurements are added to the document as measurement annotations, "
                                                "so they are saved with it and can be read by other applications. "
                                                "The scale, which was used, is stored in the annotation, so changing "
                                                "the scale later does not affect the measurements created before. "
                                                "Annotations are drawn by the annotation renderer, which uses its own "
                                                "font, so the font below applies to the temporary measurements only. "
                                                "The colors are used for both."));
    }
    else
    {
        ui->storageDescriptionLabel->setText(tr("Measurements are drawn over the document and are lost when the document "
                                                "is closed. They always use the current scale and the document itself "
                                                "is not modified."));
    }
}

void SettingsDialog::onChangeScaleTriggered()
{
    ScaleDialog dialog(this, m_updatedSettings.defaultScale, ScaleDialog::Mode::Edit);

    if (dialog.exec() == QDialog::Accepted)
    {
        DimensionScale scale = dialog.getScale();

        if (scale.isValid())
        {
            m_updatedSettings.defaultScale = qMove(scale);
            updateScaleEdit();
        }
    }
}

void SettingsDialog::accept()
{
    m_updatedSettings.lengthUnit = DimensionUnit::getLengthUnit(ui->lengthsComboBox->currentData().toByteArray());
    m_updatedSettings.areaUnit = DimensionUnit::getAreaUnit(ui->areasComboBox->currentData().toByteArray());
    m_updatedSettings.angleUnit = DimensionUnit::getAngleUnit(ui->anglesComboBox->currentData().toByteArray());
    m_updatedSettings.storageMode = pdfplugin::DimensionsPluginSettings::StorageMode(ui->storageComboBox->currentData().toInt());
    m_updatedSettings.isScaleStoredPerDocument = ui->scalePerDocumentCheckBox->isChecked();

    m_originalSettings = m_updatedSettings;
    QDialog::accept();
}

void SettingsDialog::setFont(const QFont& font)
{
    m_updatedSettings.font = font;
}

QFont SettingsDialog::getFont() const
{
    return m_updatedSettings.font;
}

void SettingsDialog::setTextColor(const QColor& color)
{
    m_updatedSettings.textColor = color;
}

QColor SettingsDialog::getTextColor() const
{
    return m_updatedSettings.textColor;
}

void SettingsDialog::setBackgroundColor(const QColor& color)
{
    m_updatedSettings.backgroundColor = color;
}

QColor SettingsDialog::getBackgroundColor() const
{
    return m_updatedSettings.backgroundColor;
}
