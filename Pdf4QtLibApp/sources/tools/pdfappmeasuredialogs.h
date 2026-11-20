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

#ifndef PDFAPPMEASUREDIALOGS_H
#define PDFAPPMEASUREDIALOGS_H

#include "pdfappglobal.h"
#include "pdfmeasure.h"
#include "pdfmessagebox.h"

#include <QPointer>

#include <vector>

class QPushButton;
class QStandardItemModel;

namespace pdf
{

class PDFComboBox;
class PDFDoubleSpinBox;
class PDFLabel;
class PDFLineEdit;
class PDFTreeView;

/// The dialog of a scale of Measure (plan of step 7 of phase 2, 4.8): Calibrate Scale (473:6438), Custom Scale
/// (473:7024), the default scale (473:92070), Add Preset (473:7860) and the edit of a preset. Distance on the
/// paper and Real distance (a value and a length unit each), Resulting scale "1 : 50", the name (a preset of
/// Calibrate / Custom is optional; K7.7: Add Preset Disabled without a name - "Enter a name for the preset."),
/// the description (optional; empty for a new scale, the one of the scale for Default and Edit Preset; stored with
/// a preset and the default scale by PDFMeasureScaleRepository), Cancel and the Primary button. A popup: new +
/// QPointer, never on the stack (CLAUDE.md); WA_DeleteOnClose; scaleAccepted is emitted last, after the dialog has
/// closed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureScaleDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    enum class Mode
    {
        Calibrate,  ///< 473:6438: the picked distance on the paper, the real distance entered
        Custom,     ///< 473:7024: both distances entered
        Default,    ///< 473:92070: the default scale of the documents without their own one
        AddPreset,  ///< 473:7860: a new preset, its name required
        EditPreset  ///< The edit of a preset of Manage Presets, its name required
    };

    /// Width of the card of the frames (473:6438: 500)
    static constexpr int CARD_WIDTH = 500;

    /// \param mode What the dialog does
    /// \param scale The scale shown at the start (the current scale, the default one, the preset)
    /// \param paperLength Calibrate: the length of the picked line in points of the paper (shown in mm)
    /// \param parent The window
    explicit PDFAppMeasureScaleDialog(Mode mode, const PDFMeasureScale& scale, PDFReal paperLength, QWidget* parent);
    virtual ~PDFAppMeasureScaleDialog() override;

    Mode getMode() const { return m_mode; }

    /// The scale of the fields (an invalid scale, when they give none); the name and the description of the fields
    PDFMeasureScale getScale() const;

    PDFDoubleSpinBox* getPaperValueBox() const { return m_paperValue.data(); }
    PDFComboBox* getPaperUnitBox() const { return m_paperUnit.data(); }
    PDFDoubleSpinBox* getRealValueBox() const { return m_realValue.data(); }
    PDFComboBox* getRealUnitBox() const { return m_realUnit.data(); }
    PDFLineEdit* getNameEdit() const { return m_name.data(); }
    PDFLineEdit* getDescriptionEdit() const { return m_description.data(); }
    PDFLabel* getResultLabel() const { return m_result.data(); }
    QPushButton* getAcceptButton() const { return m_acceptButton.data(); }

Q_SIGNALS:
    /// The Primary button: the scale of the fields (valid); emitted after the dialog has closed
    void scaleAccepted(pdf::PDFMeasureScale scale);

private:
    void updateState();
    void onAcceptClicked();

    Mode m_mode;
    QPointer<PDFDoubleSpinBox> m_paperValue;
    QPointer<PDFComboBox> m_paperUnit;
    QPointer<PDFDoubleSpinBox> m_realValue;
    QPointer<PDFComboBox> m_realUnit;
    QPointer<PDFLabel> m_result;
    QPointer<PDFLineEdit> m_name;
    QPointer<PDFLineEdit> m_description;
    QPointer<QPushButton> m_acceptButton;
};

/// The dialog Manage Presets of Measure (473:7412): the table of the presets (Name, Scale, Description), Add…
/// (473:7860), Edit… (also a double click), Remove, Restore Defaults (473:8249: the question "Replace the presets
/// by the default ones?" - Cancel | Restore Defaults; nothing is stored until Save Presets), Cancel | Save Presets.
/// A popup: new + QPointer; its child dialogs are new + QPointer too and close with it; presetsAccepted is
/// emitted last, after the dialog has closed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasurePresetsDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Width of the card (473:7412: 700)
    static constexpr int CARD_WIDTH = 700;

    explicit PDFAppMeasurePresetsDialog(std::vector<PDFMeasureScale> presets, QWidget* parent);
    virtual ~PDFAppMeasurePresetsDialog() override;

    const std::vector<PDFMeasureScale>& getPresets() const { return m_presets; }

    /// The presets are the built-in ones of Restore Defaults (not edited since)
    bool isRestored() const { return m_isRestored; }

    PDFTreeView* getTable() const { return m_table.data(); }
    QPushButton* getAddButton() const { return m_addButton.data(); }
    QPushButton* getEditButton() const { return m_editButton.data(); }
    QPushButton* getRemoveButton() const { return m_removeButton.data(); }
    QPushButton* getRestoreButton() const { return m_restoreButton.data(); }
    QPushButton* getSaveButton() const { return m_saveButton.data(); }
    PDFAppMeasureScaleDialog* getScaleDialog() const { return m_scaleDialog.data(); }
    PDFMessageBox* getRestoreDialog() const { return m_restoreDialog.data(); }

    /// Selects the row of a preset (-1 = none)
    void selectPreset(int index);
    int getSelectedPreset() const;

    /// Add… and Edit… (473:7860); Restore Defaults (473:8249) - the opened dialogs
    PDFAppMeasureScaleDialog* addPreset();
    PDFAppMeasureScaleDialog* editPreset();
    PDFMessageBox* restoreDefaults();

    /// Remove of the selected preset
    void removePreset();

Q_SIGNALS:
    /// Save Presets: the presets of the table; \p isRestored = the built-in ones (Restore Defaults)
    void presetsAccepted(std::vector<pdf::PDFMeasureScale> presets, bool isRestored);

private:
    void updateTable();
    void updateButtons();
    void onPresetAccepted(pdf::PDFMeasureScale scale);
    void onRestoreConfirmed();
    void onSaveClicked();
    void onSelectionChanged();
    void onRowDoubleClicked();

    std::vector<PDFMeasureScale> m_presets;
    bool m_isRestored = false;
    int m_editedPreset = -1;    ///< The preset of the open Edit…, -1 = Add…
    QPointer<QStandardItemModel> m_model;
    QPointer<PDFTreeView> m_table;
    QPointer<PDFLabel> m_footer;
    QPointer<QPushButton> m_addButton;
    QPointer<QPushButton> m_editButton;
    QPointer<QPushButton> m_removeButton;
    QPointer<QPushButton> m_restoreButton;
    QPointer<QPushButton> m_saveButton;
    QPointer<PDFAppMeasureScaleDialog> m_scaleDialog;
    QPointer<PDFMessageBox> m_restoreDialog;
};

}   // namespace pdf

#endif // PDFAPPMEASUREDIALOGS_H
