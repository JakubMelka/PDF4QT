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

#include "pdfappcolorpicker.h"
#include "pdfappmainwindow.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentpanels.h"

#include <QAbstractButton>

namespace pdf
{

PDFAppColorPicker::PDFAppColorPicker(QWidget* owner) :
    BaseClass(owner),
    m_owner(owner)
{
    setObjectName(QStringLiteral("colorPicker"));
    owner->installEventFilter(this);
}

PDFAppColorPicker::~PDFAppColorPicker()
{
    // The row is gone (a closed tab, a rebuilt panel): its dialog closes
    // without a color. It may be inside of its own signal (the application has
    // deleted the row from colorSelected), so it is hidden and deleted later.
    if (PDFColorPickerDialog* dialog = m_dialog.data())
    {
        m_dialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

PDFAppColorPicker* PDFAppColorPicker::attach(PDFColorSwatchRow* row)
{
    if (!row)
    {
        return nullptr;
    }

    if (PDFAppColorPicker* existing = find(row))
    {
        return existing;
    }

    PDFAppColorPicker* picker = new PDFAppColorPicker(row);
    picker->m_row = row;
    connect(row, &PDFColorSwatchRow::moreColorsRequested, picker, &PDFAppColorPicker::onMoreColorsRequested);
    return picker;
}

PDFAppColorPicker* PDFAppColorPicker::attach(PDFColorButton* button)
{
    if (!button)
    {
        return nullptr;
    }

    if (PDFAppColorPicker* existing = find(button))
    {
        return existing;
    }

    PDFAppColorPicker* picker = new PDFAppColorPicker(button);
    picker->m_button = button;
    connect(button, &PDFColorButton::moreColorsRequested, picker, &PDFAppColorPicker::onMoreColorsRequested);
    return picker;
}

PDFAppColorPicker* PDFAppColorPicker::find(const QWidget* owner)
{
    return owner ? owner->findChild<PDFAppColorPicker*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

PDFColorPickerDialog* PDFAppColorPicker::getDialog() const
{
    return m_dialog.data();
}

QColor PDFAppColorPicker::getOwnerColor() const
{
    if (m_row)
    {
        return m_row->getColor();
    }
    if (m_button)
    {
        return m_button->getColor();
    }
    return QColor();
}

PDFColorPalette* PDFAppColorPicker::getOwnerPalette() const
{
    if (m_row)
    {
        return m_row->getPalette();
    }
    if (m_button)
    {
        return m_button->getPalette();
    }
    return nullptr;
}

QWidget* PDFAppColorPicker::findScrimTarget(QWidget* widget)
{
    // The body of the main window (a dialog opened over another dialog finds
    // the main window through the parents of the windows)
    constexpr int MAXIMUM_DEPTH = 64;
    QWidget* window = widget ? widget->window() : nullptr;
    for (int depth = 0; window && depth < MAXIMUM_DEPTH; ++depth)
    {
        if (qobject_cast<PDFAppMainWindow*>(window))
        {
            return PDFAppMainWindow::findDialogHost(window);
        }
        QWidget* parent = window->parentWidget();
        window = parent ? parent->window() : nullptr;
    }
    return nullptr;
}

QWidget* PDFAppColorPicker::getDialogParent() const
{
    QWidget* owner = m_owner.data();
    if (!owner)
    {
        return nullptr;
    }

    // A popover (the menu of Mark as annotations) is not a parent of a dialog:
    // the dialog belongs to the window below it
    QWidget* window = owner->window();
    if (window->windowType() == Qt::Popup && window->parentWidget())
    {
        return window->parentWidget()->window();
    }
    return window;
}

bool PDFAppColorPicker::openDialog()
{
    QWidget* owner = m_owner.data();
    if (!owner || !owner->isEnabled() || !owner->isVisible())
    {
        return false;
    }

    if (PDFColorPickerDialog* dialog = m_dialog.data())
    {
        dialog->raise();
        dialog->activateWindow();
        return true;
    }

    QWidget* parent = getDialogParent();
    if (!parent)
    {
        return false;
    }

    // A popover of the row hides while the dialog is open (a click into the
    // dialog would close it anyway), a restored one comes back after it
    QWidget* window = owner->window();
    if (window->windowType() == Qt::Popup)
    {
        m_hiddenPopup = window;
        QPointer<PDFAppColorPicker> guard(this);
        m_isHidingPopup = true;
        window->hide();
        if (!guard)
        {
            return false;
        }
        m_isHidingPopup = false;
        if (!m_owner)
        {
            return false;
        }
    }

    PDFColorPickerDialog* dialog = new PDFColorPickerDialog(getOwnerColor(), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    if (QWidget* host = findScrimTarget(parent))
    {
        dialog->setScrimTarget(host);
    }
    m_dialog = dialog;
    connect(dialog, &PDFColorPickerDialog::colorSelected, this, &PDFAppColorPicker::onColorSelected);
    connect(dialog, &QDialog::finished, this, &PDFAppColorPicker::onDialogFinished);

    // Shown without a nested event loop (the message box is modal)
    dialog->show();
    return true;
}

void PDFAppColorPicker::onMoreColorsRequested()
{
    openDialog();
}

void PDFAppColorPicker::onColorSelected(const QColor& color)
{
    // The color is copied (the reference belongs to the dialog); a row disabled
    // meanwhile (the document has been locked) takes no color
    const QColor selected = color;
    PDFColorPalette* palette = isOwnerUsable() ? getOwnerPalette() : nullptr;
    if (!palette || !selected.isValid())
    {
        return;
    }

    // The path of a swatch of the palette: the row / button emits colorSelected
    // (its slot may delete the row and this helper)
    palette->selectColor(selected);
}

bool PDFAppColorPicker::isOwnerUsable() const
{
    QWidget* owner = m_owner.data();
    if (!owner || !owner->isEnabled())
    {
        return false;
    }
    if (owner->isVisible())
    {
        return true;
    }

    // Inside of the popover hidden for the dialog: shown in it, the popover
    // belongs to a shown window
    QWidget* popup = m_hiddenPopup.data();
    QWidget* popupParent = popup ? popup->parentWidget() : nullptr;
    return popup && popup->isAncestorOf(owner) && owner->isVisibleTo(popup) && popupParent && popupParent->isVisible();
}

bool PDFAppColorPicker::eventFilter(QObject* watched, QEvent* event)
{
    // A row hidden or disabled while the dialog is open (a locked document ends
    // the tool): the dialog closes from the event loop (the event is not ours)
    if (watched == m_owner.data() && m_dialog && !m_isHidingPopup && !m_isOwnerCheckPending &&
        (event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange))
    {
        m_isOwnerCheckPending = true;
        QMetaObject::invokeMethod(this, &PDFAppColorPicker::onOwnerChanged, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppColorPicker::onOwnerChanged()
{
    m_isOwnerCheckPending = false;
    PDFColorPickerDialog* dialog = m_dialog.data();
    if (!dialog || isOwnerUsable())
    {
        return;
    }

    // No color and no popover shown again
    m_hiddenPopup = nullptr;
    dialog->reject();
}

void PDFAppColorPicker::onDialogFinished()
{
    // The dialog deletes itself (WA_DeleteOnClose)
    m_dialog = nullptr;

    QWidget* popup = m_hiddenPopup.data();
    m_hiddenPopup = nullptr;
    if (m_isPopupRestored && popup && m_owner && popup->isAncestorOf(m_owner.data()) && popup->isHidden())
    {
        popup->show();
        if (m_row)
        {
            if (QAbstractButton* more = m_row->getMoreButton())
            {
                more->setFocus(Qt::PopupFocusReason);
            }
        }
    }
}

}   // namespace pdf
