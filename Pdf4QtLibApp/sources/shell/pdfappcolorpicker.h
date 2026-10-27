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

#ifndef PDFAPPCOLORPICKER_H
#define PDFAPPCOLORPICKER_H

#include "pdfappglobal.h"

#include <QColor>
#include <QObject>
#include <QPointer>

class QWidget;

namespace pdf
{

class PDFColorButton;
class PDFColorPalette;
class PDFColorPickerDialog;
class PDFColorSwatchRow;

/// "More colors…" of a color swatch row or of a color button: opens the
/// dialog Color picker 1294:1695 (PDFColorPickerDialog) with the color of the
/// row / button and applies the chosen color exactly as a swatch of the palette
/// does (PDFColorPalette::selectColor - the color becomes a recent color, the
/// row / button emits its colorSelected, so an annotation change is one undo
/// step and a setting is written once). Cancel changes nothing.
///
/// The helper is a child of its row / button: a deleted row closes the dialog
/// without a color, a row hidden or disabled meanwhile (a locked document ends
/// the tool and hides its bar) closes it too and takes no color.
/// The dialog is a window of the window of the row (a popover of the row - a
/// panel of the document toolbar, the menu of Mark as annotations - is hidden
/// while the dialog is open; \p setPopupRestored shows it again after the
/// dialog), its scrim covers the body of the main window. It never runs a
/// nested event loop.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorPicker : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    virtual ~PDFAppColorPicker() override;

    /// Connects "More colors…" of the row / button (once; a second call returns
    /// the existing helper). Null gives null.
    static PDFAppColorPicker* attach(PDFColorSwatchRow* row);
    static PDFAppColorPicker* attach(PDFColorButton* button);

    /// Returns the helper of the row / button (nullptr, if none is attached)
    static PDFAppColorPicker* find(const QWidget* owner);

    /// Opens the dialog for the current color of the row / button (an open
    /// dialog is raised); false, if the owner is gone, hidden or disabled
    bool openDialog();

    /// Returns the open dialog (nullptr, if none is open)
    PDFColorPickerDialog* getDialog() const;

    /// The popover of the row, hidden while the dialog is open, is shown again
    /// after the dialog (the menu of Mark as annotations: its button applies the
    /// chosen color); off by default - a panel closes as after a choice
    bool isPopupRestored() const { return m_isPopupRestored; }
    void setPopupRestored(bool isPopupRestored) { m_isPopupRestored = isPopupRestored; }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit PDFAppColorPicker(QWidget* owner);

    /// The row / button can take a color: enabled and shown (also inside of its
    /// popover hidden for the dialog)
    bool isOwnerUsable() const;

    /// Closes the dialog without a color, when the row / button is not usable any more
    void onOwnerChanged();

    /// Returns the color shown by the row / button (an invalid color is no color)
    QColor getOwnerColor() const;

    /// Returns the palette of the row / button (the path of a chosen color)
    PDFColorPalette* getOwnerPalette() const;

    /// Returns the window, over which the dialog is shown, and the body of the
    /// main window covered by its scrim
    QWidget* getDialogParent() const;
    static QWidget* findScrimTarget(QWidget* widget);

    void onMoreColorsRequested();
    void onColorSelected(const QColor& color);
    void onDialogFinished();

    QPointer<QWidget> m_owner;
    QPointer<PDFColorSwatchRow> m_row;
    QPointer<PDFColorButton> m_button;
    QPointer<PDFColorPickerDialog> m_dialog;
    QPointer<QWidget> m_hiddenPopup;
    bool m_isPopupRestored = false;
    bool m_isHidingPopup = false;
    bool m_isOwnerCheckPending = false;
};

}   // namespace pdf

#endif // PDFAPPCOLORPICKER_H
