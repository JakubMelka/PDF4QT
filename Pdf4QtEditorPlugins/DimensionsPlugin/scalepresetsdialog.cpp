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

#include "scalepresetsdialog.h"
#include "ui_scalepresetsdialog.h"

#include "scaledialog.h"

#include "pdfwidgetutils.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

#include <iterator>

ScalePresetsDialog::ScalePresetsDialog(QWidget* parent, std::vector<DimensionScale> presets) :
    QDialog(parent),
    ui(new Ui::ScalePresetsDialog),
    m_presets(qMove(presets))
{
    ui->setupUi(this);

    ui->presetsTable->setColumnCount(3);
    ui->presetsTable->setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Scale") << tr("Description"));
    ui->presetsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->presetsTable->verticalHeader()->setVisible(false);

    connect(ui->addButton, &QPushButton::clicked, this, &ScalePresetsDialog::onAddTriggered);
    connect(ui->editButton, &QPushButton::clicked, this, &ScalePresetsDialog::onEditTriggered);
    connect(ui->removeButton, &QPushButton::clicked, this, &ScalePresetsDialog::onRemoveTriggered);
    connect(ui->restoreDefaultsButton, &QPushButton::clicked, this, &ScalePresetsDialog::onRestoreDefaultsTriggered);
    connect(ui->presetsTable, &QTableWidget::itemSelectionChanged, this, &ScalePresetsDialog::updateActions);
    connect(ui->presetsTable, &QTableWidget::itemDoubleClicked, this, &ScalePresetsDialog::onEditTriggered);

    updateTable();

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(620, 380)));
    pdf::PDFWidgetUtils::style(this);
}

ScalePresetsDialog::~ScalePresetsDialog()
{
    delete ui;
}

void ScalePresetsDialog::updateTable()
{
    const int selectedIndex = getSelectedIndex();

    ui->presetsTable->setRowCount(int(m_presets.size()));

    for (size_t i = 0; i < m_presets.size(); ++i)
    {
        const DimensionScale& preset = m_presets[i];

        ui->presetsTable->setItem(int(i), 0, new QTableWidgetItem(preset.getName()));
        ui->presetsTable->setItem(int(i), 1, new QTableWidgetItem(preset.getRatioText()));
        ui->presetsTable->setItem(int(i), 2, new QTableWidgetItem(preset.getDescription()));
    }

    ui->presetsTable->resizeColumnToContents(0);
    ui->presetsTable->resizeColumnToContents(1);

    if (selectedIndex >= 0 && selectedIndex < int(m_presets.size()))
    {
        ui->presetsTable->selectRow(selectedIndex);
    }

    updateActions();
}

void ScalePresetsDialog::updateActions()
{
    const bool hasSelection = getSelectedIndex() != -1;
    ui->editButton->setEnabled(hasSelection);
    ui->removeButton->setEnabled(hasSelection);
}

int ScalePresetsDialog::getSelectedIndex() const
{
    const QModelIndexList selection = ui->presetsTable->selectionModel()->selectedRows();

    if (selection.isEmpty())
    {
        return -1;
    }

    return selection.front().row();
}

void ScalePresetsDialog::onAddTriggered()
{
    ScaleDialog dialog(this, DimensionScale::createIdentity(), ScaleDialog::Mode::Edit);

    while (dialog.exec() == QDialog::Accepted)
    {
        DimensionScale scale = dialog.getScale();

        if (scale.getName().isEmpty())
        {
            QMessageBox::warning(this, tr("Scale Preset"), tr("Name of the preset must be filled."));
            continue;
        }

        m_presets.push_back(qMove(scale));
        updateTable();
        ui->presetsTable->selectRow(int(m_presets.size()) - 1);
        break;
    }
}

void ScalePresetsDialog::onEditTriggered()
{
    const int index = getSelectedIndex();

    if (index == -1)
    {
        return;
    }

    ScaleDialog dialog(this, m_presets[size_t(index)], ScaleDialog::Mode::Edit);

    while (dialog.exec() == QDialog::Accepted)
    {
        DimensionScale scale = dialog.getScale();

        if (scale.getName().isEmpty())
        {
            QMessageBox::warning(this, tr("Scale Preset"), tr("Name of the preset must be filled."));
            continue;
        }

        m_presets[size_t(index)] = qMove(scale);
        updateTable();
        break;
    }
}

void ScalePresetsDialog::onRemoveTriggered()
{
    const int index = getSelectedIndex();

    if (index == -1)
    {
        return;
    }

    m_presets.erase(std::next(m_presets.begin(), index));
    updateTable();
}

void ScalePresetsDialog::onRestoreDefaultsTriggered()
{
    if (QMessageBox::question(this, tr("Scale Presets"), tr("Do you want to replace the scale presets by the default ones?")) != QMessageBox::Yes)
    {
        return;
    }

    m_presets = DimensionScale::getDefaultPresets();
    updateTable();
}
