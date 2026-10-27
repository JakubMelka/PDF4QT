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

#ifndef PDFAPPSETTINGCONTROL_H
#define PDFAPPSETTINGCONTROL_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfsettingdefinition.h"

#include <QColor>
#include <QObject>
#include <QPushButton>
#include <QWidget>

#include "pdffluentwidgets.h"
#include <QPointer>
#include <QTimer>
#include <QVariant>

class QCheckBox;
class QHBoxLayout;
class QPushButton;
class QSlider;

namespace pdf
{

class PDFAppContext;
class PDFColorButton;
class PDFComboBox;
class PDFDoubleSpinBox;
class PDFLabel;
class PDFLineEdit;
class PDFSpinBox;

/// Combo box of a setting (PDFAppSettingControl). It tells, whether an activation
/// comes from the input of the closed combo box (an arrow, Home / End, Page Up /
/// Page Down, a typed letter or an input method), so a setting, whose change is
/// expensive (PDFSettingDefinition::isCommittedOnActivation), is written only by
/// a choice from the open list or by Enter. The wheel does not change a combo box
/// of the style at all (SH_ComboBox_AllowWheelScrolling = 0, the page scrolls).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingComboBox : public PDFComboBox
{
    Q_OBJECT

private:
    using BaseClass = PDFComboBox;

public:
    explicit PDFAppSettingComboBox(QWidget* parent);
    virtual ~PDFAppSettingComboBox() override;

    /// The combo box is handling a key or an input method event of its closed
    /// state (QComboBox emits activated from inside of it)
    bool isHandlingClosedInput() const { return m_closedInputDepth > 0; }

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void inputMethodEvent(QInputMethodEvent* event) override;

private:
    int m_closedInputDepth = 0;
};

/// Binder of one setting to its control (Settings, step 5, plan 2.2): the control
/// is chosen by the definition (type and presentation hints), it shows the stored
/// valid value and writes the value of the user.
///  - Check box, combo box and color button write at once (a combo box from the
///    event loop). A combo box of a definition with isCommittedOnActivation (the
///    language) writes only an activated choice: a click or Enter in the open
///    list (Alt+Down / F4 / Space opens it), or Enter on the closed combo box. The
///    arrows and typed letters on the closed combo box only show another item
///    (a screen reader announces it); Escape or leaving the combo box without
///    Enter shows the stored value again (an item, which is only shown, is not
///    a value of the user and it is never written).
///  - Spin boxes, sliders and text fields write after WRITE_DELAY ms of rest, at
///    Enter, at the loss of the focus (from the event loop), when the page is
///    hidden and when the binder is destroyed (a pending value is never lost).
///  - A value refused by the repository (invalid, a busy or read-only database) is
///    reported by writeFailed and the control shows the stored value again.
///  - refresh shows the stored value without writing it back; a control with a
///    pending value of the user keeps the value of the user.
///  - A stored option, which an options provider does not offer (a voice, a profile
///    of another system), is shown as an extra item "… (not available)"; the engine
///    uses the default then.
/// The binder holds a copy of the definition (a registry of a closed context is
/// never touched) and the context by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingControl : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Kind of the control
    enum class Kind
    {
        CheckBox,       ///< Bool: check box "Enable"
        ComboBox,       ///< Option: combo box of the options
        SpinBox,        ///< Integer: spin box with the unit
        DoubleSpinBox,  ///< Real: spin box with the decimals and the unit
        Slider,         ///< Integer / Real with Control::Slider: slider and the value text
        TextField,      ///< String: text field
        Color,          ///< String with Control::Color: color button, "#RRGGBB"
        Path,           ///< String with Control::Path: text field and Browse… of a folder
        Segmented       ///< Option with Control::Segmented: segmented control of the options (fixed 240 x 32, segments 80 - 955:143435)
    };

    /// Rest after an edit of a spin box, slider or text field, before it is written
    static constexpr int WRITE_DELAY = 300;

    /// Widths of the controls of the design (control-wrap 260 of 146:2843)
    static constexpr int CONTROL_WIDTH = 260;
    static constexpr int SPIN_BOX_WIDTH = 140;
    static constexpr int SLIDER_WIDTH = 180;
    static constexpr int SLIDER_SPACING = 12;
    static constexpr int PATH_SPACING = 8;
    static constexpr int SEGMENTED_WIDTH = 240;
    static constexpr int SEGMENTED_HEIGHT = 32;

    /// Longest text of a stored option shown as "… (not available)"
    static constexpr int MAXIMUM_OPTION_TEXT = 64;

    /// Longest stored text shown by formatValue (the rows of Restore defaults); a
    /// longer text is elided at its end, a path in its middle
    static constexpr int MAXIMUM_STRING_TEXT = 96;

    /// Creates the control of the definition as a child of \p parent (the control
    /// wrap of the row); the control shows the stored value at once
    explicit PDFAppSettingControl(PDFAppContext* context, const PDFSettingDefinition& definition, QWidget* parent);

    /// Writes a pending value (without notifying anybody)
    virtual ~PDFAppSettingControl() override;

    /// Kind of the control of the definition
    static Kind getKind(const PDFSettingDefinition& definition);

    Kind getKind() const { return m_kind; }
    const PDFSettingDefinition& getDefinition() const { return m_definition; }
    const QString& getKey() const { return m_definition.getKey(); }

    /// The widget placed into the control wrap (the editor, or a container of the
    /// slider with its value text, of the path field with its button)
    QWidget* getWidget() const { return m_widget; }

    /// The focusable editor (check box, combo box, spin box, slider, text field,
    /// color button)
    QWidget* getEditor() const { return m_editor; }

    QCheckBox* getCheckBox() const;
    PDFComboBox* getComboBox() const;
    PDFSpinBox* getSpinBox() const;
    PDFDoubleSpinBox* getDoubleSpinBox() const;
    QSlider* getSlider() const;
    PDFLineEdit* getLineEdit() const;
    PDFColorButton* getColorButton() const;
    PDFSegmentedControl* getSegmentedControl() const;

    /// Value text of the slider ("0.75")
    PDFLabel* getValueLabel() const { return m_valueLabel; }

    /// Button Browse… of a path
    QPushButton* getBrowseButton() const { return m_browseButton; }

    /// Value shown by the control converted to the type of the setting (invalid
    /// for the item "(not available)" of a combo box)
    QVariant getShownValue() const;

    /// The combo box shows the stored option, which is not offered, "(not available)"
    bool isNotAvailableShown() const { return m_isNotAvailableShown; }

    /// Shows the stored valid value (never writes); a pending value of the user is kept
    void refresh();

    /// The user has changed the value, it is not written yet
    bool hasPendingValue() const { return m_hasPendingValue; }

    /// The closed combo box of a definition with isCommittedOnActivation shows an
    /// item chosen by the arrows, which is not activated (not written) yet
    bool isBrowsing() const { return m_isBrowsing; }

    /// Writes the pending value now (writeFailed on an error)
    void commitPendingValue();

    /// Text of a value of the definition for the user: On / Off, the title of the
    /// option, a number with its decimals and unit ("3.0 ×", "150 %"), a color, a
    /// text (Restore defaults, the value text of a slider)
    static QString formatValue(const PDFSettingDefinition& definition, const QVariant& value);

    /// Color of a stored text "#RRGGBB" (an invalid color for anything else)
    static QColor parseColor(const QString& text);

    /// Enter, Escape and the loss of the focus of a combo box browsed by the arrows
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// A value of the user could not be stored (the control shows the stored value)
    void writeFailed(const QString& message);

    /// A value of the user has been stored
    void valueWritten(const QString& key);

private:
    void createControl(QWidget* parent);
    void createSlider(QWidget* parent);
    void createPath(QWidget* parent);
    void updateComboItems(const QVariant& storedValue, const QVariant& value);
    void showValue(const QVariant& value, const QVariant& storedValue);
    void updateValueLabel();
    double getSliderFactor() const;
    double clampReal(double value) const;

    /// Remembers a change of the user to be written after the delay
    void setPending();

    /// Writes the value; \p isNotified emits the signals (not from the destructor)
    bool writeValue(const QVariant& value, bool isNotified);
    void writePending(bool isNotified);

    /// Writes the pending value from the event loop (a Qt widget emitting its
    /// signals does not survive its deletion by a slot of valueWritten)
    void queueWrite();

    void onCheckBoxToggled(bool checked);
    void onComboBoxActivated(int index);
    void onSegmentChanged(int index);

    /// Writes the item of a combo box browsed by the arrows (Enter)
    void commitBrowsedValue();
    void onEditorValueChanged();
    void onEditingFinished();
    void onSliderValueChanged(int value);
    void onSliderReleased();
    void onColorSelected(const QColor& color);
    void onBrowseClicked();
    void onWriteTimeout();

    QPointer<PDFAppContext> m_context;
    PDFSettingDefinition m_definition;
    Kind m_kind = Kind::CheckBox;
    QPointer<QWidget> m_widget;
    QPointer<QWidget> m_editor;
    QPointer<PDFLabel> m_valueLabel;
    QPointer<QPushButton> m_browseButton;
    QTimer m_writeTimer;
    QStringList m_segmentIds;   ///< Option ids of the segments of Kind::Segmented
    bool m_hasPendingValue = false;
    bool m_isNotAvailableShown = false;
    bool m_isBrowsing = false;
};

}   // namespace pdf

#endif // PDFAPPSETTINGCONTROL_H
