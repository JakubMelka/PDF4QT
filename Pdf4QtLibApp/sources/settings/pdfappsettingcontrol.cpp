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

#include "pdfappsettingcontrol.h"
#include "pdfappcontext.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsysteminfo.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingsrepository.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QCheckBox>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the binder of a setting
class PDFAppSettingControlHelper
{
public:
    PDFAppSettingControlHelper() = delete;

    /// Most characters of a stored text looked at by formatValue
    static constexpr qsizetype MAXIMUM_SANITIZED_TEXT = 4096;

    /// Index, which does not split a surrogate pair (moved to its start)
    static qsizetype toCharacterBoundary(const QString& text, qsizetype index)
    {
        if (index > 0 && index < text.size() && text.at(index).isLowSurrogate() && text.at(index - 1).isHighSurrogate())
        {
            return index - 1;
        }
        return index;
    }

    /// A stored text (another version, a damaged file) on one line: no control,
    /// format or bidirectional characters, at most \p maximumLength characters -
    /// elided at its end or, for a path, in its middle (the file name stays)
    static QString toDisplayText(const QString& text, qsizetype maximumLength, bool isPath)
    {
        const QString sanitized = PDFSearchQuery::sanitize(text, MAXIMUM_SANITIZED_TEXT);
        if (sanitized.size() <= maximumLength || maximumLength < 3)
        {
            return sanitized;
        }

        const QChar ellipsis(0x2026);
        if (!isPath)
        {
            return sanitized.left(toCharacterBoundary(sanitized, maximumLength - 1)) + ellipsis;
        }

        const qsizetype head = toCharacterBoundary(sanitized, (maximumLength - 1) / 2);
        qsizetype tailStart = sanitized.size() - (maximumLength - 1 - (maximumLength - 1) / 2);
        if (sanitized.at(tailStart).isLowSurrogate())
        {
            // The tail does not start inside of a surrogate pair
            ++tailStart;
        }
        return sanitized.left(head) + ellipsis + sanitized.mid(tailStart);
    }

    static int toInt(qint64 value)
    {
        return int(std::clamp<qint64>(value, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    }

    static QHBoxLayout* createRow(QWidget* widget, int spacing)
    {
        QHBoxLayout* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }

    /// A number with its unit ("150 %", "3.0 ×")
    static QString withUnit(const QString& text, const QString& unit)
    {
        return unit.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, unit);
    }
};

PDFAppSettingComboBox::PDFAppSettingComboBox(QWidget* parent) :
    BaseClass(parent)
{

}

PDFAppSettingComboBox::~PDFAppSettingComboBox() = default;

void PDFAppSettingComboBox::keyPressEvent(QKeyEvent* event)
{
    // QComboBox emits activated from inside of its key handler (the arrows, Home /
    // End, Page Up / Page Down and the keyboard search of the closed combo box);
    // the open list has its own view, which takes the keys
    QPointer<PDFAppSettingComboBox> guard(this);
    ++m_closedInputDepth;
    BaseClass::keyPressEvent(event);
    if (guard)
    {
        --m_closedInputDepth;
    }
}

void PDFAppSettingComboBox::inputMethodEvent(QInputMethodEvent* event)
{
    // The keyboard search by a text of an input method
    QPointer<PDFAppSettingComboBox> guard(this);
    ++m_closedInputDepth;
    BaseClass::inputMethodEvent(event);
    if (guard)
    {
        --m_closedInputDepth;
    }
}

PDFAppSettingControl::PDFAppSettingControl(PDFAppContext* context, const PDFSettingDefinition& definition, QWidget* parent) :
    BaseClass(parent),
    m_context(context),
    m_definition(definition),
    m_kind(getKind(definition))
{
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(WRITE_DELAY);
    connect(&m_writeTimer, &QTimer::timeout, this, &PDFAppSettingControl::onWriteTimeout);

    createControl(parent);
    refresh();
}

PDFAppSettingControl::~PDFAppSettingControl()
{
    // A value of the user is never lost: it is written, but nobody is notified
    // (the receivers may be being destroyed with the page)
    writePending(false);
}

PDFAppSettingControl::Kind PDFAppSettingControl::getKind(const PDFSettingDefinition& definition)
{
    using Type = PDFSettingDefinition::Type;
    using Control = PDFSettingDefinition::Control;

    switch (definition.getType())
    {
        case Type::Bool:
            return Kind::CheckBox;
        case Type::Option:
            return Kind::ComboBox;
        case Type::Integer:
            return definition.getControl() == Control::Slider ? Kind::Slider : Kind::SpinBox;
        case Type::Real:
            return definition.getControl() == Control::Slider ? Kind::Slider : Kind::DoubleSpinBox;
        case Type::String:
            if (definition.getControl() == Control::Color)
            {
                return Kind::Color;
            }
            return definition.getControl() == Control::Path ? Kind::Path : Kind::TextField;
    }
    return Kind::TextField;
}

QCheckBox* PDFAppSettingControl::getCheckBox() const
{
    return qobject_cast<QCheckBox*>(m_editor.data());
}

PDFComboBox* PDFAppSettingControl::getComboBox() const
{
    return qobject_cast<PDFComboBox*>(m_editor.data());
}

PDFSpinBox* PDFAppSettingControl::getSpinBox() const
{
    return qobject_cast<PDFSpinBox*>(m_editor.data());
}

PDFDoubleSpinBox* PDFAppSettingControl::getDoubleSpinBox() const
{
    return qobject_cast<PDFDoubleSpinBox*>(m_editor.data());
}

QSlider* PDFAppSettingControl::getSlider() const
{
    return qobject_cast<QSlider*>(m_editor.data());
}

PDFLineEdit* PDFAppSettingControl::getLineEdit() const
{
    return qobject_cast<PDFLineEdit*>(m_editor.data());
}

PDFColorButton* PDFAppSettingControl::getColorButton() const
{
    return qobject_cast<PDFColorButton*>(m_editor.data());
}

void PDFAppSettingControl::createControl(QWidget* parent)
{
    using Helper = PDFAppSettingControlHelper;
    const QString unit = m_definition.getUnit();

    switch (m_kind)
    {
        case Kind::CheckBox:
        {
            QCheckBox* checkBox = new QCheckBox(tr("Enable"), parent);
            // clicked (the user, guarded by Qt against a deletion from its slots), not
            // toggled of QCheckBox, which Qt does not guard
            connect(checkBox, &QCheckBox::clicked, this, &PDFAppSettingControl::onCheckBoxToggled);
            m_editor = checkBox;
            m_widget = checkBox;
            break;
        }

        case Kind::ComboBox:
        {
            PDFComboBox* comboBox = new PDFAppSettingComboBox(parent);
            comboBox->setFixedWidth(CONTROL_WIDTH);
            connect(comboBox, &QComboBox::activated, this, &PDFAppSettingControl::onComboBoxActivated);
            if (m_definition.isCommittedOnActivation())
            {
                // Enter, Escape and the loss of the focus of a browsed combo box
                comboBox->installEventFilter(this);
            }
            m_editor = comboBox;
            m_widget = comboBox;
            break;
        }

        case Kind::SpinBox:
        {
            PDFSpinBox* spinBox = new PDFSpinBox(parent);
            spinBox->setFixedWidth(SPIN_BOX_WIDTH);
            spinBox->setRange(Helper::toInt(m_definition.getMinimum().toLongLong()), Helper::toInt(m_definition.getMaximum().toLongLong()));
            spinBox->setDisplaySuffix(unit);
            connect(spinBox, &QSpinBox::valueChanged, this, &PDFAppSettingControl::onEditorValueChanged);
            connect(spinBox, &QSpinBox::editingFinished, this, &PDFAppSettingControl::onEditingFinished);
            m_editor = spinBox;
            m_widget = spinBox;
            break;
        }

        case Kind::DoubleSpinBox:
        {
            PDFDoubleSpinBox* spinBox = new PDFDoubleSpinBox(parent);
            spinBox->setFixedWidth(SPIN_BOX_WIDTH);
            spinBox->setDecimals(m_definition.getDecimals());
            spinBox->setRange(m_definition.getMinimum().toDouble(), m_definition.getMaximum().toDouble());
            spinBox->setSingleStep(std::pow(10.0, -m_definition.getDecimals()));
            spinBox->setDisplaySuffix(unit);
            connect(spinBox, &QDoubleSpinBox::valueChanged, this, &PDFAppSettingControl::onEditorValueChanged);
            connect(spinBox, &QDoubleSpinBox::editingFinished, this, &PDFAppSettingControl::onEditingFinished);
            m_editor = spinBox;
            m_widget = spinBox;
            break;
        }

        case Kind::Slider:
            createSlider(parent);
            break;

        case Kind::TextField:
        {
            PDFLineEdit* lineEdit = new PDFLineEdit(parent);
            lineEdit->setFixedWidth(CONTROL_WIDTH);
            lineEdit->setMaxLength(int(std::clamp<qsizetype>(m_definition.getMaximumLength(), 0, 32767)));
            connect(lineEdit, &QLineEdit::textEdited, this, &PDFAppSettingControl::onEditorValueChanged);
            connect(lineEdit, &QLineEdit::editingFinished, this, &PDFAppSettingControl::onEditingFinished);
            m_editor = lineEdit;
            m_widget = lineEdit;
            break;
        }

        case Kind::Color:
        {
            PDFColorButton* button = new PDFColorButton(parent);
            button->setFixedWidth(CONTROL_WIDTH);
            connect(button, &PDFColorButton::colorSelected, this, &PDFAppSettingControl::onColorSelected);
            m_editor = button;
            m_widget = button;
            break;
        }

        case Kind::Path:
            createPath(parent);
            break;
    }

    if (m_editor)
    {
        // The object name finds the editor again after a rebuild of the page
        // (PDFAppPageRebuild), the title and the description are its accessible texts
        const QString anchor = m_definition.getAnchor().isEmpty() ? m_definition.getKey() : m_definition.getAnchor();
        m_editor->setObjectName(QStringLiteral("setting-%1").arg(anchor));
        m_editor->setAccessibleName(m_definition.getTitle());
        m_editor->setAccessibleDescription(m_definition.getDescription());
    }
}

void PDFAppSettingControl::createSlider(QWidget* parent)
{
    // Slider 180 and the value text Medium 12 (147:4128: gap 12)
    QWidget* container = new QWidget(parent);
    QHBoxLayout* layout = PDFAppSettingControlHelper::createRow(container, SLIDER_SPACING);

    QSlider* slider = new QSlider(Qt::Horizontal, container);
    slider->setFixedWidth(SLIDER_WIDTH);
    const double factor = getSliderFactor();
    const int minimum = PDFAppSettingControlHelper::toInt(qint64(std::llround(m_definition.getMinimum().toDouble() * factor)));
    const int maximum = PDFAppSettingControlHelper::toInt(qint64(std::llround(m_definition.getMaximum().toDouble() * factor)));
    slider->setRange(minimum, maximum);
    slider->setSingleStep(1);
    slider->setPageStep(qMax(1, (maximum - minimum) / 10));
    layout->addWidget(slider);

    m_valueLabel = new PDFLabel(QString(), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextSecondary, container);
    m_valueLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_valueLabel, 1);

    connect(slider, &QSlider::valueChanged, this, &PDFAppSettingControl::onSliderValueChanged);
    connect(slider, &QSlider::sliderReleased, this, &PDFAppSettingControl::onSliderReleased);
    m_editor = slider;
    m_widget = container;
}

void PDFAppSettingControl::createPath(QWidget* parent)
{
    // Text field and Browse… (146:4194: 158 + 8 + 94)
    QWidget* container = new QWidget(parent);
    container->setFixedWidth(CONTROL_WIDTH);
    QHBoxLayout* layout = PDFAppSettingControlHelper::createRow(container, PATH_SPACING);

    PDFLineEdit* lineEdit = new PDFLineEdit(container);
    lineEdit->setMaxLength(int(std::clamp<qsizetype>(m_definition.getMaximumLength(), 0, 32767)));
    connect(lineEdit, &QLineEdit::textEdited, this, &PDFAppSettingControl::onEditorValueChanged);
    connect(lineEdit, &QLineEdit::editingFinished, this, &PDFAppSettingControl::onEditingFinished);
    layout->addWidget(lineEdit, 1);

    m_browseButton = PDFAppPageWidgets::createButton(tr("Browse…"), QString(), false, container);
    m_browseButton->setIcon(QIcon());
    m_browseButton->setObjectName(QStringLiteral("browseButton"));
    connect(m_browseButton.data(), &QPushButton::clicked, this, &PDFAppSettingControl::onBrowseClicked);
    layout->addWidget(m_browseButton);

    m_editor = lineEdit;
    m_widget = container;
}

double PDFAppSettingControl::clampReal(double value) const
{
    return std::clamp(value, m_definition.getMinimum().toDouble(), m_definition.getMaximum().toDouble());
}

double PDFAppSettingControl::getSliderFactor() const
{
    return std::pow(10.0, m_definition.getDecimals());
}

QColor PDFAppSettingControl::parseColor(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!expression.match(text).hasMatch())
    {
        return QColor();
    }
    return QColor::fromString(text);
}

QString PDFAppSettingControl::formatValue(const PDFSettingDefinition& definition, const QVariant& value)
{
    using Helper = PDFAppSettingControlHelper;
    using Type = PDFSettingDefinition::Type;

    const QString unit = definition.getUnit();

    switch (definition.getType())
    {
        case Type::Bool:
            return value.toBool() ? tr("On") : tr("Off");

        case Type::Integer:
            return Helper::withUnit(PDFAppLocale::formatNumber(value.toLongLong()), unit);

        case Type::Real:
            return Helper::withUnit(PDFAppLocale::getUiLocale().toString(value.toDouble(), 'f', definition.getDecimals()), unit);

        case Type::Option:
        {
            const QString id = value.toString();
            for (const PDFSettingOption& option : definition.getOptions())
            {
                if (option.id == id)
                {
                    return option.getTitle();
                }
            }
            // A stored text of another version is shown without control and
            // bidirectional characters (review RC5-BC)
            return PDFSearchQuery::sanitize(id, MAXIMUM_OPTION_TEXT);
        }

        case Type::String:
        {
            // A stored text of another version or of a damaged file is shown on one
            // line without control and bidirectional characters, bounded (review RC5-S)
            const QString text = Helper::toDisplayText(value.toString(), MAXIMUM_STRING_TEXT, definition.getControl() == PDFSettingDefinition::Control::Path);
            return text.isEmpty() ? tr("(empty)") : text;
        }
    }

    return value.toString();
}

QVariant PDFAppSettingControl::getShownValue() const
{
    switch (m_kind)
    {
        case Kind::CheckBox:
            return getCheckBox() ? QVariant(getCheckBox()->isChecked()) : QVariant();

        case Kind::ComboBox:
        {
            PDFComboBox* comboBox = getComboBox();
            if (!comboBox || comboBox->currentIndex() < 0 || (m_isNotAvailableShown && comboBox->currentIndex() == comboBox->count() - 1))
            {
                return QVariant();
            }
            return comboBox->currentData();
        }

        case Kind::SpinBox:
            return getSpinBox() ? QVariant(qint64(getSpinBox()->value())) : QVariant();

        case Kind::DoubleSpinBox:
            // The minimum of the spin box is rounded to its decimals (0.0001 is shown as 0.000)
            return getDoubleSpinBox() ? QVariant(clampReal(getDoubleSpinBox()->value())) : QVariant();

        case Kind::Slider:
        {
            QSlider* slider = getSlider();
            if (!slider)
            {
                return QVariant();
            }
            if (m_definition.getType() == PDFSettingDefinition::Type::Integer)
            {
                return QVariant(qint64(slider->value()));
            }
            return QVariant(clampReal(double(slider->value()) / getSliderFactor()));
        }

        case Kind::TextField:
        case Kind::Path:
            return getLineEdit() ? QVariant(getLineEdit()->text()) : QVariant();

        case Kind::Color:
        {
            PDFColorButton* button = getColorButton();
            if (!button || !button->getColor().isValid())
            {
                return QVariant();
            }
            return QVariant(button->getColor().name(QColor::HexRgb).toUpper());
        }
    }
    return QVariant();
}

void PDFAppSettingControl::refresh()
{
    // A value of the user, which is not written yet, stays shown (it is written
    // after the delay, at Enter or at the loss of the focus); so does an item of
    // a combo box browsed by the arrows (Enter writes it, Escape and the loss of
    // the focus show the stored value)
    if (m_hasPendingValue || m_isBrowsing || !m_editor)
    {
        return;
    }

    QVariant storedValue;
    QVariant value = m_definition.getDefaultValue();
    if (m_context && m_context->getDatabase())
    {
        const PDFSettingsRepository settings = m_context->getSettings();
        storedValue = settings.getStoredValue(m_definition.getKey());
        value = m_definition.validate(storedValue);
    }
    showValue(value, storedValue);
}

void PDFAppSettingControl::updateComboItems(const QVariant& storedValue, const QVariant& value)
{
    PDFComboBox* comboBox = getComboBox();
    if (!comboBox)
    {
        return;
    }

    // A stored option, which the provider does not offer (another system), is
    // shown as not available; a hostile value of the static options is the default
    QString notAvailableId;
    const std::vector<PDFSettingOption> options = m_definition.getOptions();
    if (m_definition.hasOptionsProvider() && storedValue.typeId() == QMetaType::QString && storedValue.toString() != value.toString())
    {
        notAvailableId = storedValue.toString().left(MAXIMUM_OPTION_TEXT);
    }

    QStringList ids;
    QStringList titles;
    for (const PDFSettingOption& option : options)
    {
        ids << option.id;
        titles << option.getTitle();
    }
    if (!notAvailableId.isEmpty())
    {
        ids << notAvailableId;
        titles << tr("%1 (not available)").arg(PDFSearchQuery::sanitize(notAvailableId, MAXIMUM_OPTION_TEXT));
    }

    bool isSame = comboBox->count() == ids.size();
    for (int i = 0; isSame && i < ids.size(); ++i)
    {
        isSame = comboBox->itemData(i).toString() == ids[i] && comboBox->itemText(i) == titles[i];
    }

    if (!isSame)
    {
        const QSignalBlocker blocker(comboBox);
        comboBox->clear();
        for (int i = 0; i < ids.size(); ++i)
        {
            comboBox->addItem(titles[i], ids[i]);
        }
    }

    m_isNotAvailableShown = !notAvailableId.isEmpty();
    const QSignalBlocker blocker(comboBox);
    comboBox->setCurrentIndex(m_isNotAvailableShown ? int(ids.size()) - 1 : int(ids.indexOf(value.toString())));
}

void PDFAppSettingControl::showValue(const QVariant& value, const QVariant& storedValue)
{
    switch (m_kind)
    {
        case Kind::CheckBox:
            if (QCheckBox* checkBox = getCheckBox())
            {
                const QSignalBlocker blocker(checkBox);
                checkBox->setChecked(value.toBool());
            }
            break;

        case Kind::ComboBox:
            updateComboItems(storedValue, value);
            break;

        // A value, which the editor shows already, is not set again: the echo of the
        // own write reaches a field the user is still typing in, and setting its text
        // again moves the cursor to the end, drops the undo history and reformats an
        // unfinished number ("2." to "2.0")
        case Kind::SpinBox:
            if (PDFSpinBox* spinBox = getSpinBox(); spinBox && spinBox->value() != PDFAppSettingControlHelper::toInt(value.toLongLong()))
            {
                const QSignalBlocker blocker(spinBox);
                spinBox->setValue(PDFAppSettingControlHelper::toInt(value.toLongLong()));
            }
            break;

        case Kind::DoubleSpinBox:
            if (PDFDoubleSpinBox* spinBox = getDoubleSpinBox(); spinBox && clampReal(spinBox->value()) != value.toDouble())
            {
                const QSignalBlocker blocker(spinBox);
                spinBox->setValue(value.toDouble());
            }
            break;

        case Kind::Slider:
            if (QSlider* slider = getSlider())
            {
                const QSignalBlocker blocker(slider);
                slider->setValue(PDFAppSettingControlHelper::toInt(qint64(std::llround(value.toDouble() * getSliderFactor()))));
            }
            updateValueLabel();
            break;

        case Kind::TextField:
        case Kind::Path:
            if (PDFLineEdit* lineEdit = getLineEdit(); lineEdit && lineEdit->text() != value.toString())
            {
                const QSignalBlocker blocker(lineEdit);
                lineEdit->setText(value.toString());
            }
            break;

        case Kind::Color:
            if (PDFColorButton* button = getColorButton())
            {
                // A hostile stored text is the default color
                QColor color = parseColor(value.toString());
                if (!color.isValid())
                {
                    color = parseColor(m_definition.getDefaultValue().toString());
                }
                const QSignalBlocker blocker(button);
                button->setColor(color);
            }
            break;
    }
}

void PDFAppSettingControl::updateValueLabel()
{
    if (m_valueLabel)
    {
        m_valueLabel->setText(formatValue(m_definition, getShownValue()));
    }
}

void PDFAppSettingControl::setPending()
{
    m_hasPendingValue = true;
    m_writeTimer.start();
}

void PDFAppSettingControl::commitPendingValue()
{
    writePending(true);
}

void PDFAppSettingControl::writePending(bool isNotified)
{
    if (!m_hasPendingValue)
    {
        return;
    }

    m_writeTimer.stop();
    m_hasPendingValue = false;
    const bool wasNotAvailableShown = m_isNotAvailableShown;
    QPointer<PDFAppSettingControl> guard(this);
    if (writeValue(getShownValue(), isNotified) && isNotified && guard && wasNotAvailableShown)
    {
        // The stored option, which was not available, has been replaced
        refresh();
    }
}

bool PDFAppSettingControl::writeValue(const QVariant& value, bool isNotified)
{
    if (!m_context || !m_context->getDatabase() || !value.isValid())
    {
        return false;
    }

    const PDFDatabaseError error = m_context->getSettings().setValue(m_definition.getKey(), value);
    if (!isNotified)
    {
        return !error.isError();
    }

    // The slots may delete the page with this binder
    QPointer<PDFAppSettingControl> guard(this);
    if (error.isError())
    {
        Q_EMIT writeFailed(error.toString());
        if (guard)
        {
            // The control shows the stored value again
            refresh();
        }
        return false;
    }

    Q_EMIT valueWritten(m_definition.getKey());
    return true;
}

void PDFAppSettingControl::onCheckBoxToggled(bool checked)
{
    writeValue(QVariant(checked), true);
}

void PDFAppSettingControl::onComboBoxActivated(int index)
{
    PDFComboBox* comboBox = getComboBox();
    if (!comboBox || index < 0)
    {
        return;
    }

    // A setting, whose change is expensive (the language rebuilds the whole user
    // interface), is written only by an activated choice: an arrow or a typed
    // letter on the closed combo box only shows another item (owner 2026-10-25)
    const PDFAppSettingComboBox* settingComboBox = qobject_cast<const PDFAppSettingComboBox*>(comboBox);
    if (m_definition.isCommittedOnActivation() && settingComboBox && settingComboBox->isHandlingClosedInput())
    {
        m_isBrowsing = true;
        return;
    }
    m_isBrowsing = false;

    // The item "(not available)" cannot be stored again
    if (m_isNotAvailableShown && index == comboBox->count() - 1)
    {
        return;
    }

    // Written from the event loop: the slots of valueWritten may delete the combo
    // box, which is emitting its signals (Qt does not survive it)
    setPending();
    queueWrite();
}

void PDFAppSettingControl::commitBrowsedValue()
{
    m_isBrowsing = false;
    PDFComboBox* comboBox = getComboBox();
    if (!comboBox || comboBox->currentIndex() < 0 || (m_isNotAvailableShown && comboBox->currentIndex() == comboBox->count() - 1))
    {
        refresh();
        return;
    }

    // Written from the event loop (the combo box is handling its key event)
    setPending();
    queueWrite();
}

bool PDFAppSettingControl::eventFilter(QObject* watched, QEvent* event)
{
    // Only a combo box of a definition with isCommittedOnActivation is watched
    if (!m_isBrowsing || !watched || watched != m_editor.data())
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const bool isPlain = (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
            const int key = keyEvent->key();
            if (!isPlain || (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape))
            {
                break;
            }

            // The browsed combo box takes Enter and Escape before the shortcuts of
            // the window (QComboBox passes them to its parent)
            event->accept();
            if (event->type() == QEvent::KeyPress)
            {
                if (key == Qt::Key_Escape)
                {
                    m_isBrowsing = false;
                    refresh();
                }
                else
                {
                    commitBrowsedValue();
                }
            }
            return true;
        }

        case QEvent::FocusOut:
            // Leaving the combo box without Enter shows the stored value again (not
            // the opening of its list, which takes the focus as a popup)
            if (static_cast<const QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
            {
                m_isBrowsing = false;
                refresh();
            }
            break;

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppSettingControl::onEditorValueChanged()
{
    setPending();
}

void PDFAppSettingControl::onEditingFinished()
{
    // Enter and the loss of the focus: from the event loop (the spin box and the line
    // edit emit more signals after editingFinished)
    queueWrite();
}

void PDFAppSettingControl::queueWrite()
{
    if (m_hasPendingValue)
    {
        QMetaObject::invokeMethod(this, &PDFAppSettingControl::onWriteTimeout, Qt::QueuedConnection);
    }
}

void PDFAppSettingControl::onSliderValueChanged(int value)
{
    Q_UNUSED(value);
    updateValueLabel();
    setPending();
}

void PDFAppSettingControl::onSliderReleased()
{
    queueWrite();
}

void PDFAppSettingControl::onColorSelected(const QColor& color)
{
    if (color.isValid())
    {
        writeValue(QVariant(color.name(QColor::HexRgb).toUpper()), true);
    }
}

void PDFAppSettingControl::onBrowseClicked()
{
    PDFLineEdit* lineEdit = getLineEdit();
    if (!lineEdit)
    {
        return;
    }

    // The shown folder is untrusted (another version, another computer, a backup):
    // the dialog never starts in a device or a network path (a server, which does
    // not answer, would block the user interface)
    QString startFolder = lineEdit->text();
    if (!PDFRepositoryHelper::isValidStoredPath(startFolder) || PDFAppNetworkPaths::isNetworkPathByName(startFolder) || PDFAppNetworkPaths().isNetworkPath(startFolder))
    {
        startFolder.clear();
    }

    // The dialog runs its own event loop: the page may be deleted meanwhile
    QPointer<PDFAppSettingControl> guard(this);
    const QString folder = PDFAppFileDialogs::getExistingDirectory(lineEdit->window(), startFolder, m_definition.getTitle());
    if (!guard || folder.isEmpty())
    {
        return;
    }

    lineEdit = getLineEdit();
    if (lineEdit)
    {
        const QSignalBlocker blocker(lineEdit);
        lineEdit->setText(folder);
    }
    m_writeTimer.stop();
    m_hasPendingValue = false;
    writeValue(QVariant(folder), true);
}

void PDFAppSettingControl::onWriteTimeout()
{
    writePending(true);
}

}   // namespace pdf
