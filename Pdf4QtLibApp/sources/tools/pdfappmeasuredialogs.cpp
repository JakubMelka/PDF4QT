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

#include "pdfappmeasuredialogs.h"
#include "pdfappmainwindow.h"
#include "pdfappmeasureoverlay.h"
#include "pdfapptoolpanelparts.h"
#include "pdfmeasurescalerepository.h"

#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace pdf
{

/// The parts of the dialogs of Measure
class PDFAppMeasureDialogsHelper
{
public:
    PDFAppMeasureDialogsHelper() = delete;

    /// The values of the fields (the frames show 4 places)
    static constexpr double MINIMUM_VALUE = 0.0001;
    static constexpr double MAXIMUM_VALUE = 1e9;

    /// Width of a value field and of a unit field (473:6438: 130 and 90)
    static constexpr int VALUE_WIDTH = 130;
    static constexpr int UNIT_WIDTH = 90;
    static constexpr int CONTROL_GAP = 8;

    /// The combo of the length units (their symbols, the ids as the data)
    static PDFComboBox* createUnitBox(const QByteArray& unitId, QWidget* parent)
    {
        PDFComboBox* box = new PDFComboBox(parent);
        for (const PDFMeasureUnit& unit : PDFMeasureUnits::getUnits(PDFMeasureQuantity::Length))
        {
            box->addItem(unit.getSymbol(), QString::fromLatin1(unit.id));
        }
        const int index = box->findData(QString::fromLatin1(unitId));
        box->setCurrentIndex(index >= 0 ? index : 0);
        box->setFixedWidth(UNIT_WIDTH);
        return box;
    }

    static PDFDoubleSpinBox* createValueBox(double value, QWidget* parent)
    {
        PDFDoubleSpinBox* box = new PDFDoubleSpinBox(parent);
        box->setDecimals(PDFMeasureScale::VALUE_DECIMALS);
        box->setRange(MINIMUM_VALUE, MAXIMUM_VALUE);
        box->setValue(qBound(MINIMUM_VALUE, value, MAXIMUM_VALUE));
        box->setFixedWidth(VALUE_WIDTH);
        return box;
    }

    /// A row of a field: the label and the value with its unit at the trailing edge
    static QWidget* createValueRow(const QString& label, QWidget* value, QWidget* unit, QWidget* parent)
    {
        QWidget* controls = new QWidget(parent);
        QHBoxLayout* layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(CONTROL_GAP);
        value->setParent(controls);
        unit->setParent(controls);
        layout->addWidget(value);
        layout->addWidget(unit);
        QWidget* row = PDFAppToolCard::createFieldRow(label, controls, parent);

        // The shared row gives a control the width of one field (140); the value and its unit
        // need both widths (473:6438), otherwise the unit is cut at the trailing edge
        controls->setFixedWidth(VALUE_WIDTH + CONTROL_GAP + UNIT_WIDTH);
        return row;
    }
};

// ------------------------------------------------------------------ PDFAppMeasureScaleDialog

PDFAppMeasureScaleDialog::PDFAppMeasureScaleDialog(Mode mode, const PDFMeasureScale& scale, PDFReal paperLength, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Information, QString(), QString(), QDialogButtonBox::NoButton, parent),
    m_mode(mode)
{
    using H = PDFAppMeasureDialogsHelper;

    setObjectName(QStringLiteral("measureScaleDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCardWidth(CARD_WIDTH);

    const PDFMeasureScale initialScale = scale.isValid() ? scale : PDFMeasureScale::createIdentity();
    PDFReal paperValue = initialScale.getPaperValue();
    QByteArray paperUnit = initialScale.getPaperUnitId();
    PDFReal realValue = initialScale.getRealValue();
    QString acceptText;
    QString acceptIcon = QStringLiteral("check");
    QString sectionTitle;
    switch (mode)
    {
        case Mode::Calibrate:
        {
            // 473:6438: the picked distance in millimeters, the real one by the scale of now in its unit
            setTitle(tr("Calibrate Scale"));
            setText(tr("Enter the real distance between the two points you have picked. The scale of the drawing is calculated from it."));
            paperValue = paperLength * PDF_POINT_TO_MM;
            paperUnit = "mm";
            const PDFMeasureUnit realUnit = PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, initialScale.getRealUnitId());
            realValue = paperLength * initialScale.getScaleFactor() * realUnit.factor;
            acceptText = tr("Use Scale");
            sectionTitle = tr("Save as a preset (optional)");
            break;
        }
        case Mode::Custom:
            setTitle(tr("Custom Scale"));
            setText(tr("Enter the distance measured on the paper and the real distance it represents. A drawing in the scale 1:50 is 1 mm = 50 mm."));
            acceptText = tr("Use Scale");
            sectionTitle = tr("Save as a preset (optional)");
            break;
        case Mode::Default:
            setTitle(tr("Default Scale"));
            setText(tr("Used for documents that were not calibrated yet and that do not define their own scale."));
            acceptText = tr("Set as Default");
            break;
        case Mode::AddPreset:
            setTitle(tr("Add Preset"));
            setText(tr("Enter the distance measured on the paper and the real distance it represents."));
            acceptText = tr("Add Preset");
            sectionTitle = tr("Preset");
            break;
        case Mode::EditPreset:
            setTitle(tr("Edit Preset"));
            setText(tr("Enter the distance measured on the paper and the real distance it represents."));
            acceptText = tr("Save Preset");
            sectionTitle = tr("Preset");
            break;
    }

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_paperValue = H::createValueBox(paperValue, content);
    m_paperValue->setObjectName(QStringLiteral("measureScalePaperValue"));
    m_paperUnit = H::createUnitBox(paperUnit, content);
    m_paperUnit->setObjectName(QStringLiteral("measureScalePaperUnit"));
    layout->addWidget(H::createValueRow(tr("Distance on the paper"), m_paperValue, m_paperUnit, content));
    m_realValue = H::createValueBox(realValue, content);
    m_realValue->setObjectName(QStringLiteral("measureScaleRealValue"));
    m_realUnit = H::createUnitBox(initialScale.getRealUnitId(), content);
    m_realUnit->setObjectName(QStringLiteral("measureScaleRealUnit"));
    layout->addWidget(H::createValueRow(tr("Real distance"), m_realValue, m_realUnit, content));

    // Resulting scale "1 : 50" (473:6438)
    PDFAppToolCard* resultCard = new PDFAppToolCard(content);
    m_result = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, resultCard);
    m_result->setObjectName(QStringLiteral("measureScaleResult"));
    resultCard->getBodyLayout()->addWidget(PDFAppToolCard::createFieldRow(tr("Resulting scale"), m_result, resultCard));
    layout->addWidget(resultCard);

    if (!sectionTitle.isEmpty())
    {
        layout->addWidget(new PDFLabel(sectionTitle, PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, content));
    }
    layout->addWidget(new PDFLabel(tr("Name"), PDFThemeFonts::Role::Body, PDFThemeColors::TextPrimary, content));
    m_name = new PDFLineEdit(initialScale.getName(), content);
    m_name->setObjectName(QStringLiteral("measureScaleName"));
    m_name->setMaxLength(PDFMeasureScale::MAXIMUM_DOCUMENT_NAME_LENGTH);
    layout->addWidget(m_name);

    // The description (473:7860, 473:7024, 473:6438 empty; 473:92070 the one of the default scale)
    layout->addWidget(new PDFLabel(tr("Description"), PDFThemeFonts::Role::Body, PDFThemeColors::TextPrimary, content));
    m_description = new PDFLineEdit(initialScale.getDescription(), content);
    m_description->setObjectName(QStringLiteral("measureScaleDescription"));
    m_description->setMaxLength(PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH);
    layout->addWidget(m_description);
    if (mode == Mode::Calibrate || mode == Mode::Custom || mode == Mode::AddPreset)
    {
        // A new scale: no name and no description of the scale of now
        m_name->clear();
        m_description->clear();
    }
    addContent(content);

    QPushButton* cancel = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("measureScaleCancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_acceptButton = addActionButton(acceptText, ActionPosition::Trailing, true);
    m_acceptButton->setObjectName(QStringLiteral("measureScaleAccept"));
    m_acceptButton->setIcon(PDFThemeIcons::getIcon(acceptIcon));
    connect(m_acceptButton.data(), &QPushButton::clicked, this, &PDFAppMeasureScaleDialog::onAcceptClicked);

    connect(m_paperValue.data(), &QDoubleSpinBox::valueChanged, this, &PDFAppMeasureScaleDialog::updateState);
    connect(m_realValue.data(), &QDoubleSpinBox::valueChanged, this, &PDFAppMeasureScaleDialog::updateState);
    connect(m_paperUnit.data(), &QComboBox::currentIndexChanged, this, &PDFAppMeasureScaleDialog::updateState);
    connect(m_realUnit.data(), &QComboBox::currentIndexChanged, this, &PDFAppMeasureScaleDialog::updateState);
    connect(m_name.data(), &QLineEdit::textChanged, this, &PDFAppMeasureScaleDialog::updateState);
    updateState();
}

PDFAppMeasureScaleDialog::~PDFAppMeasureScaleDialog() = default;

PDFMeasureScale PDFAppMeasureScaleDialog::getScale() const
{
    if (!m_paperValue || !m_paperUnit || !m_realValue || !m_realUnit || !m_name || !m_description)
    {
        return PDFMeasureScale();
    }
    PDFMeasureScale scale(m_paperValue->value(), m_paperUnit->currentData().toString().toLatin1(), m_realValue->value(), m_realUnit->currentData().toString().toLatin1(), m_name->text().trimmed());
    scale.setDescription(m_description->text().trimmed());
    return scale.isValid() ? scale : PDFMeasureScale();
}

void PDFAppMeasureScaleDialog::updateState()
{
    const PDFMeasureScale scale = getScale();
    if (m_result)
    {
        // "1 : 50" with the spaces of the frame
        const std::pair<PDFReal, PDFReal> proportion = scale.getProportion();
        m_result->setText(scale.isValid() ? tr("%1 : %2").arg(PDFAppMeasureText::formatTrimmed(proportion.first, PDFMeasureScale::PROPORTION_DECIMALS),
                                                                PDFAppMeasureText::formatTrimmed(proportion.second, PDFMeasureScale::PROPORTION_DECIMALS))
                                          : QString());
    }

    // K7.7: a preset needs its name (473:7860 "Enter a name for the preset.")
    const bool isNameRequired = m_mode == Mode::AddPreset || m_mode == Mode::EditPreset;
    const bool isNameMissing = isNameRequired && m_name && m_name->text().trimmed().isEmpty();
    if (m_name)
    {
        m_name->setInvalid(isNameMissing, isNameMissing ? tr("Enter a name for the preset.") : QString());
    }
    if (m_acceptButton)
    {
        m_acceptButton->setEnabled(scale.isValid() && !isNameMissing);
    }
}

void PDFAppMeasureScaleDialog::onAcceptClicked()
{
    const PDFMeasureScale scale = getScale();
    const bool isNameMissing = (m_mode == Mode::AddPreset || m_mode == Mode::EditPreset) && scale.getName().isEmpty();
    if (!scale.isValid() || isNameMissing)
    {
        return;
    }

    // The dialog closes first (deleted later), the decision is the last call
    accept();
    Q_EMIT scaleAccepted(scale);
}

// ------------------------------------------------------------------ PDFAppMeasurePresetsDialog

PDFAppMeasurePresetsDialog::PDFAppMeasurePresetsDialog(std::vector<PDFMeasureScale> presets, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Information, tr("Manage Scale Presets"), tr("Presets are offered in the Scale menu. Changes are applied when you save them."), QDialogButtonBox::NoButton, parent),
    m_presets(std::move(presets))
{
    setObjectName(QStringLiteral("measurePresetsDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCardWidth(CARD_WIDTH);

    QWidget* content = new QWidget(this);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    QHBoxLayout* layout = new QHBoxLayout();
    layout->setSpacing(12);
    contentLayout->addLayout(layout);

    // 473:7412: Name | Scale | Description
    m_model = new QStandardItemModel(0, 3, this);
    m_model->setHorizontalHeaderLabels({ tr("Name"), tr("Scale"), tr("Description") });
    m_table = new PDFTreeView(content);
    m_table->setObjectName(QStringLiteral("measurePresetsTable"));
    m_table->setModel(m_model);
    m_table->setRootIsDecorated(false);
    m_table->setUniformRowHeights(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAccessibleName(tr("Scale presets"));
    m_table->setMinimumHeight(240);
    layout->addWidget(m_table, 1);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppMeasurePresetsDialog::onSelectionChanged);
    connect(m_table.data(), &QAbstractItemView::doubleClicked, this, &PDFAppMeasurePresetsDialog::onRowDoubleClicked);

    QVBoxLayout* buttons = new QVBoxLayout();
    buttons->setSpacing(8);
    m_addButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("plus")), tr("Add…"), content);
    m_addButton->setObjectName(QStringLiteral("measurePresetsAdd"));
    connect(m_addButton.data(), &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::addPreset);
    m_editButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("edit")), tr("Edit…"), content);
    m_editButton->setObjectName(QStringLiteral("measurePresetsEdit"));
    connect(m_editButton.data(), &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::editPreset);
    m_removeButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("trash")), tr("Remove"), content);
    m_removeButton->setObjectName(QStringLiteral("measurePresetsRemove"));
    connect(m_removeButton.data(), &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::removePreset);
    m_restoreButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("trash-restore")), tr("Restore Defaults"), content);
    m_restoreButton->setObjectName(QStringLiteral("measurePresetsRestore"));
    connect(m_restoreButton.data(), &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::restoreDefaults);
    for (QPushButton* button : { m_addButton.data(), m_editButton.data(), m_removeButton.data(), m_restoreButton.data() })
    {
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_footer = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextSecondary, content);
    m_footer->setObjectName(QStringLiteral("measurePresetsFooter"));
    contentLayout->addWidget(m_footer);
    addContent(content);

    QPushButton* cancel = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("measurePresetsCancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_saveButton = addActionButton(tr("Save Presets"), ActionPosition::Trailing, true);
    m_saveButton->setObjectName(QStringLiteral("measurePresetsSave"));
    m_saveButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("check")));
    connect(m_saveButton.data(), &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::onSaveClicked);

    updateTable();
}

PDFAppMeasurePresetsDialog::~PDFAppMeasurePresetsDialog()
{
    // The child dialogs go with it
    for (PDFMessageBox* dialog : { static_cast<PDFMessageBox*>(m_scaleDialog.data()), m_restoreDialog.data() })
    {
        if (dialog)
        {
            dialog->disconnect(this);
            dialog->hide();
            dialog->deleteLater();
        }
    }
}

void PDFAppMeasurePresetsDialog::updateTable()
{
    if (!m_model)
    {
        return;
    }

    const int selected = getSelectedPreset();
    m_model->removeRows(0, m_model->rowCount());
    for (const PDFMeasureScale& preset : m_presets)
    {
        QList<QStandardItem*> row;
        row << new QStandardItem(preset.getName()) << new QStandardItem(PDFAppMeasureText::formatRatio(preset)) << new QStandardItem(preset.getDescription());
        m_model->appendRow(row);
    }
    if (m_table)
    {
        m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_table->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    }
    selectPreset(qMin(selected, int(m_presets.size()) - 1));

    // 473:7412 "13 presets · double-click a row to edit it"
    if (m_footer)
    {
        const int count = int(m_presets.size());
        m_footer->setText(count == 1 ? tr("%Ln preset · double-click a row to edit it", nullptr, count) : tr("%Ln presets · double-click a row to edit it", nullptr, count));
    }
    updateButtons();
}

void PDFAppMeasurePresetsDialog::updateButtons()
{
    const bool hasSelection = getSelectedPreset() >= 0;
    if (m_editButton)
    {
        m_editButton->setEnabled(hasSelection);
    }
    if (m_removeButton)
    {
        m_removeButton->setEnabled(hasSelection);
    }
    if (m_addButton)
    {
        m_addButton->setEnabled(int(m_presets.size()) < 200);
    }
}

void PDFAppMeasurePresetsDialog::selectPreset(int index)
{
    if (!m_table || !m_model)
    {
        return;
    }
    if (index < 0 || index >= m_model->rowCount())
    {
        m_table->selectionModel()->clearSelection();
        return;
    }
    m_table->selectionModel()->setCurrentIndex(m_model->index(index, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

int PDFAppMeasurePresetsDialog::getSelectedPreset() const
{
    if (!m_table || !m_table->selectionModel())
    {
        return -1;
    }
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
}

PDFAppMeasureScaleDialog* PDFAppMeasurePresetsDialog::addPreset()
{
    if (m_scaleDialog)
    {
        m_scaleDialog->raise();
        return m_scaleDialog.data();
    }

    // 473:7860: a new preset of 1 mm = 1 mm
    m_editedPreset = -1;
    PDFAppMeasureScaleDialog* dialog = new PDFAppMeasureScaleDialog(PDFAppMeasureScaleDialog::Mode::AddPreset, PDFMeasureScale::createIdentity(), 0.0, this);
    connect(dialog, &PDFAppMeasureScaleDialog::scaleAccepted, this, &PDFAppMeasurePresetsDialog::onPresetAccepted);
    m_scaleDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, this);
    dialog->open();
    return m_scaleDialog.data();
}

PDFAppMeasureScaleDialog* PDFAppMeasurePresetsDialog::editPreset()
{
    const int index = getSelectedPreset();
    if (index < 0 || index >= int(m_presets.size()))
    {
        return nullptr;
    }
    if (m_scaleDialog)
    {
        m_scaleDialog->raise();
        return m_scaleDialog.data();
    }

    m_editedPreset = index;
    PDFAppMeasureScaleDialog* dialog = new PDFAppMeasureScaleDialog(PDFAppMeasureScaleDialog::Mode::EditPreset, m_presets[size_t(index)], 0.0, this);
    connect(dialog, &PDFAppMeasureScaleDialog::scaleAccepted, this, &PDFAppMeasurePresetsDialog::onPresetAccepted);
    m_scaleDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, this);
    dialog->open();
    return m_scaleDialog.data();
}

void PDFAppMeasurePresetsDialog::removePreset()
{
    const int index = getSelectedPreset();
    if (index < 0 || index >= int(m_presets.size()))
    {
        return;
    }
    m_presets.erase(m_presets.begin() + index);
    m_isRestored = false;
    updateTable();
}

PDFMessageBox* PDFAppMeasurePresetsDialog::restoreDefaults()
{
    if (m_restoreDialog)
    {
        m_restoreDialog->raise();
        return m_restoreDialog.data();
    }

    // 473:8249 (K7.7: the order of the buttons of the frame): Cancel | Restore Defaults
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Replace the presets by the default ones?"),
                                              tr("Your own presets are removed from the list. Nothing is stored until you save the presets."), QDialogButtonBox::NoButton, this);
    dialog->setObjectName(QStringLiteral("measurePresetsRestoreDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* cancel = dialog->addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("measurePresetsRestoreCancel"));
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QPushButton* restore = dialog->addActionButton(tr("Restore Defaults"), ActionPosition::Trailing, true);
    restore->setObjectName(QStringLiteral("measurePresetsRestoreConfirm"));
    restore->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash-restore")));
    connect(restore, &QPushButton::clicked, this, &PDFAppMeasurePresetsDialog::onRestoreConfirmed);
    m_restoreDialog = dialog;
    PDFAppMainWindow::prepareDialog(dialog, this);
    dialog->open();
    return m_restoreDialog.data();
}

void PDFAppMeasurePresetsDialog::onPresetAccepted(pdf::PDFMeasureScale scale)
{
    if (!scale.isValid())
    {
        return;
    }

    if (m_editedPreset >= 0 && m_editedPreset < int(m_presets.size()))
    {
        // The description is the one of the field of Edit Preset (prefilled with the description of the preset)
        m_presets[size_t(m_editedPreset)] = scale;
    }
    else if (int(m_presets.size()) < 200)
    {
        m_presets.push_back(scale);
        m_editedPreset = int(m_presets.size()) - 1;
    }
    m_isRestored = false;
    const int selected = m_editedPreset;
    m_editedPreset = -1;
    updateTable();
    selectPreset(selected);
}

void PDFAppMeasurePresetsDialog::onRestoreConfirmed()
{
    if (PDFMessageBox* dialog = m_restoreDialog.data())
    {
        dialog->accept();
    }
    m_presets = PDFMeasureScale::getDefaultPresets();
    m_isRestored = true;
    updateTable();
}

void PDFAppMeasurePresetsDialog::onSaveClicked()
{
    const std::vector<PDFMeasureScale> presets = m_presets;
    const bool isRestored = m_isRestored;

    // The dialog closes first (deleted later), the decision is the last call
    accept();
    Q_EMIT presetsAccepted(presets, isRestored);
}

void PDFAppMeasurePresetsDialog::onSelectionChanged()
{
    updateButtons();
}

void PDFAppMeasurePresetsDialog::onRowDoubleClicked()
{
    editPreset();
}

}   // namespace pdf

#include "moc_pdfappmeasuredialogs.cpp"
