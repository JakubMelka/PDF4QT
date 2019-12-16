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

#include <QAction>
#include <QLineEdit>
#include <QMessageBox>
#include <QListWidgetItem>

namespace pdfviewer
{

PDFViewerSettingsDialog::PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings, QList<QAction*> actions, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFViewerSettingsDialog),
    m_settings(settings),
    m_actions(),
    m_isLoadingData(false)
{
    ui->setupUi(this);

    new QListWidgetItem(QIcon(":/resources/engine.svg"), tr("Engine"), ui->optionsPagesWidget, EngineSettings);
    new QListWidgetItem(QIcon(":/resources/rendering.svg"), tr("Rendering"), ui->optionsPagesWidget, RenderingSettings);
    new QListWidgetItem(QIcon(":/resources/shading.svg"), tr("Shading"), ui->optionsPagesWidget, ShadingSettings);
    new QListWidgetItem(QIcon(":/resources/cache.svg"), tr("Cache"), ui->optionsPagesWidget, CacheSettings);
    new QListWidgetItem(QIcon(":/resources/shortcuts.svg"), tr("Shortcuts"), ui->optionsPagesWidget, ShortcutSettings);
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
    for (QSpinBox* spinBox : findChildren<QSpinBox*>())
    {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFViewerSettingsDialog::saveData);
    }

    for (QAction* action : actions)
    {
        if (!action->objectName().isEmpty())
        {
            m_actions.append(action);
        }
    }

    ui->optionsPagesWidget->setCurrentRow(0);
    adjustSize();
    loadData();
    loadActionShortcutsTable();
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

        case CacheSettings:
            ui->stackedWidget->setCurrentWidget(ui->cachePage);
            break;

        case ShortcutSettings:
            ui->stackedWidget->setCurrentWidget(ui->shortcutsPage);
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
    ui->prefetchPagesCheckBox->setChecked(m_settings.m_prefetchPages);

    // Rendering
    ui->antialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::Antialiasing));
    ui->textAntialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::TextAntialiasing));
    ui->smoothPicturesCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::SmoothImages));
    ui->ignoreOptionalContentCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::IgnoreOptionalContent));
    ui->clipToCropBoxCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::ClipToCropBox));
    ui->displayTimeCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::DisplayTimes));

    // Shading
    ui->preferredMeshResolutionEdit->setValue(m_settings.m_preferredMeshResolutionRatio);
    ui->minimalMeshResolutionEdit->setValue(m_settings.m_minimalMeshResolutionRatio);
    ui->colorToleranceEdit->setValue(m_settings.m_colorTolerance);

    // Cache
    ui->compiledPageCacheSizeEdit->setValue(m_settings.m_compiledPageCacheLimit);
    ui->thumbnailCacheSizeEdit->setValue(m_settings.m_thumbnailsCacheLimit);
    ui->cachedFontLimitEdit->setValue(m_settings.m_fontCacheLimit);
    ui->cachedInstancedFontLimitEdit->setValue(m_settings.m_instancedFontCacheLimit);

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
    else if (sender == ui->prefetchPagesCheckBox)
    {
        m_settings.m_prefetchPages = ui->prefetchPagesCheckBox->isChecked();
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
    else if (sender == ui->displayTimeCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::DisplayTimes, ui->displayTimeCheckBox->isChecked());
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
    else if (sender == ui->compiledPageCacheSizeEdit)
    {
        m_settings.m_compiledPageCacheLimit = ui->compiledPageCacheSizeEdit->value();
    }
    else if (sender == ui->thumbnailCacheSizeEdit)
    {
        m_settings.m_thumbnailsCacheLimit = ui->thumbnailCacheSizeEdit->value();
    }
    else if (sender == ui->cachedFontLimitEdit)
    {
        m_settings.m_fontCacheLimit = ui->cachedFontLimitEdit->value();
    }
    else if (sender == ui->cachedInstancedFontLimitEdit)
    {
        m_settings.m_instancedFontCacheLimit = ui->cachedInstancedFontLimitEdit->value();
    }

    loadData();
}

void PDFViewerSettingsDialog::loadActionShortcutsTable()
{
    ui->shortcutsTableWidget->setRowCount(m_actions.size());
    ui->shortcutsTableWidget->setColumnCount(2);
    ui->shortcutsTableWidget->setHorizontalHeaderLabels({ tr("Action"), tr("Shortcut")});
    ui->shortcutsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* action = m_actions[i];

        // Action name and icon
        QTableWidgetItem* actionItem = new QTableWidgetItem(action->icon(), action->text());
        actionItem->setFlags(Qt::ItemIsEnabled);
        ui->shortcutsTableWidget->setItem(i, 0, actionItem);

        // Action shortcut
        QTableWidgetItem* shortcutItem = new QTableWidgetItem(action->shortcut().toString(QKeySequence::NativeText));
        ui->shortcutsTableWidget->setItem(i, 1, shortcutItem);
    }
}

bool PDFViewerSettingsDialog::saveActionShortcutsTable()
{
    // Jakub Melka: we need validation here
    for (int i = 0; i < m_actions.size(); ++i)
    {
        QString shortcut = ui->shortcutsTableWidget->item(i, 1)->data(Qt::DisplayRole).toString();
        if (!shortcut.isEmpty())
        {
            QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::NativeText);
            if (sequence.toString(QKeySequence::PortableText).isEmpty())
            {
                QMessageBox::critical(this, tr("Error"), tr("Shortcut '%1' is invalid for action %2.").arg(shortcut, m_actions[i]->text()));
                return false;
            }
        }
    }

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* action = m_actions[i];

        // Set shortcut to the action
        QString shortcut = ui->shortcutsTableWidget->item(i, 1)->data(Qt::DisplayRole).toString();
        QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::NativeText);
        action->setShortcut(sequence);
    }

    return true;
}

void PDFViewerSettingsDialog::accept()
{
    if (saveActionShortcutsTable())
    {
        QDialog::accept();
    }
}

}   // namespace pdfviewer
