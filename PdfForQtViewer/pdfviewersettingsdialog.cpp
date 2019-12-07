//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfviewersettingsdialog.h"
#include "ui_pdfviewersettingsdialog.h"

#include "pdfglobal.h"
#include "pdfutils.h"

#include <QMessageBox>
#include <QListWidgetItem>

namespace pdfviewer
{

PDFViewerSettingsDialog::PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFViewerSettingsDialog),
    m_settings(settings),
    m_isLoadingData(false)
{
    ui->setupUi(this);

    new QListWidgetItem(QIcon(":/resources/engine.svg"), tr("Engine"), ui->optionsPagesWidget, EngineSettings);
    new QListWidgetItem(QIcon(":/resources/rendering.svg"), tr("Rendering"), ui->optionsPagesWidget, RenderingSettings);
    new QListWidgetItem(QIcon(":/resources/shading.svg"), tr("Shading"), ui->optionsPagesWidget, ShadingSettings);
    new QListWidgetItem(QIcon(":/resources/security.svg"), tr("Security"), ui->optionsPagesWidget, SecuritySettings);

    ui->renderingEngineComboBox->addItem(tr("Software"), static_cast<int>(pdf::RendererEngine::Software));
    ui->renderingEngineComboBox->addItem(tr("Hardware accelerated (OpenGL)"), static_cast<int>(pdf::RendererEngine::OpenGL));

    for (int i : { 1, 2, 4, 8, 16 })
    {
        ui->multisampleAntialiasingSamplesCountComboBox->addItem(QString::number(i), i);
    }

    for (QWidget* widget : { ui->engineInfoLabel, ui->renderingInfoLabel, ui->securityInfoLabel })
    {
        widget->setMinimumWidth(widget->sizeHint().width());
    }

    for (QCheckBox* checkBox : findChildren<QCheckBox*>())
    {
        connect(checkBox, &QCheckBox::clicked, this, &PDFViewerSettingsDialog::saveData);
    }
    for (QComboBox* comboBox : findChildren<QComboBox*>())
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFViewerSettingsDialog::saveData);
    }
    for (QDoubleSpinBox* spinBox : findChildren<QDoubleSpinBox*>())
    {
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFViewerSettingsDialog::saveData);
    }

    ui->optionsPagesWidget->setCurrentRow(0);
    adjustSize();
    loadData();
}

PDFViewerSettingsDialog::~PDFViewerSettingsDialog()
{
    delete ui;
}

void PDFViewerSettingsDialog::on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    switch (current->type())
    {
        case EngineSettings:
            ui->stackedWidget->setCurrentWidget(ui->enginePage);
            break;

        case RenderingSettings:
            ui->stackedWidget->setCurrentWidget(ui->renderingPage);
            break;

        case ShadingSettings:
            ui->stackedWidget->setCurrentWidget(ui->shadingPage);
            break;

        case SecuritySettings:
            ui->stackedWidget->setCurrentWidget(ui->securityPage);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFViewerSettingsDialog::loadData()
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingData, true);
    ui->renderingEngineComboBox->setCurrentIndex(ui->renderingEngineComboBox->findData(static_cast<int>(m_settings.m_rendererEngine)));

    // Engine
    if (m_settings.m_rendererEngine == pdf::RendererEngine::OpenGL)
    {
        ui->multisampleAntialiasingCheckBox->setEnabled(true);
        ui->multisampleAntialiasingCheckBox->setChecked(m_settings.m_multisampleAntialiasing);

        if (m_settings.m_multisampleAntialiasing)
        {
            ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(true);
            ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(ui->multisampleAntialiasingSamplesCountComboBox->findData(m_settings.m_rendererSamples));
        }
        else
        {
            ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(false);
            ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(-1);
        }
    }
    else
    {
        ui->multisampleAntialiasingCheckBox->setEnabled(false);
        ui->multisampleAntialiasingCheckBox->setChecked(false);
        ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(false);
        ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(-1);
    }

    // Rendering
    ui->antialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::Antialiasing));
    ui->textAntialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::TextAntialiasing));
    ui->smoothPicturesCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::SmoothImages));
    ui->ignoreOptionalContentCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::IgnoreOptionalContent));
    ui->clipToCropBoxCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::ClipToCropBox));

    // Shading
    ui->preferredMeshResolutionEdit->setValue(m_settings.m_preferredMeshResolutionRatio);
    ui->minimalMeshResolutionEdit->setValue(m_settings.m_minimalMeshResolutionRatio);
    ui->colorToleranceEdit->setValue(m_settings.m_colorTolerance);

    // Security
    ui->allowLaunchCheckBox->setChecked(m_settings.m_allowLaunchApplications);
    ui->allowRunURICheckBox->setChecked(m_settings.m_allowLaunchURI);
}

void PDFViewerSettingsDialog::saveData()
{
    if (m_isLoadingData)
    {
        return;
    }

    QObject* sender = this->sender();

    if (sender == ui->renderingEngineComboBox)
    {
        m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(ui->renderingEngineComboBox->currentData().toInt());
    }
    else if (sender == ui->multisampleAntialiasingCheckBox)
    {
        m_settings.m_multisampleAntialiasing = ui->multisampleAntialiasingCheckBox->isChecked();
    }
    else if (sender == ui->multisampleAntialiasingSamplesCountComboBox)
    {
        m_settings.m_rendererSamples = ui->multisampleAntialiasingSamplesCountComboBox->currentData().toInt();
    }
    else if (sender == ui->antialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::Antialiasing, ui->antialiasingCheckBox->isChecked());
    }
    else if (sender == ui->textAntialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::TextAntialiasing, ui->textAntialiasingCheckBox->isChecked());
    }
    else if (sender == ui->smoothPicturesCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::SmoothImages, ui->smoothPicturesCheckBox->isChecked());
    }
    else if (sender == ui->ignoreOptionalContentCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::IgnoreOptionalContent, ui->ignoreOptionalContentCheckBox->isChecked());
    }
    else if (sender == ui->clipToCropBoxCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::ClipToCropBox, ui->clipToCropBoxCheckBox->isChecked());
    }
    else if (sender == ui->preferredMeshResolutionEdit)
    {
        m_settings.m_preferredMeshResolutionRatio = ui->preferredMeshResolutionEdit->value();
    }
    else if (sender == ui->minimalMeshResolutionEdit)
    {
        m_settings.m_minimalMeshResolutionRatio = ui->minimalMeshResolutionEdit->value();
    }
    else if (sender == ui->colorToleranceEdit)
    {
        m_settings.m_colorTolerance = ui->colorToleranceEdit->value();
    }
    else if (sender == ui->allowLaunchCheckBox)
    {
        m_settings.m_allowLaunchApplications = ui->allowLaunchCheckBox->isChecked();
    }
    else if (sender == ui->allowRunURICheckBox)
    {
        m_settings.m_allowLaunchURI = ui->allowRunURICheckBox->isChecked();
    }

    loadData();
}

}   // namespace pdfviewer
