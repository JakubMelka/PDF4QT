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

#ifndef PDFMESSAGEBOX_H
#define PDFMESSAGEBOX_H

#include "pdffluentglobal.h"
#include "pdfthemeiconengine.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QPointer>

class QAbstractButton;
class QCheckBox;
class QFrame;

namespace pdf
{

class PDFLabel;
class PDFPlainTextEdit;

/// Message box of the design: a badge with an icon, a title, a text, optional
/// details in a note box, an optional check box and the buttons. It replaces
/// QMessageBox, whose texts take their colors from a palette.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFMessageBox : public QDialog
{
    Q_OBJECT

public:
    /// Creates the message box
    /// \param badge Badge of the message
    /// \param title Title of the message
    /// \param text Text of the message
    /// \param buttons Buttons of the message box
    /// \param parent Parent widget
    explicit PDFMessageBox(PDFThemeIcons::Badge badge,
                           const QString& title,
                           const QString& text,
                           QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                           QWidget* parent = nullptr);

    /// Sets the details shown in a note box below the text
    void setDetailedText(const QString& text);

    /// Shows a check box with the given text (for example "Don't show again")
    void setCheckBoxText(const QString& text);
    bool isCheckBoxChecked() const;

    /// Sets the button, which is activated by the Enter key (the primary button).
    /// Without it the default button is the first button, which accepts the
    /// message (Ok, Save, Yes, ...), as in QMessageBox.
    void setDefaultButton(QDialogButtonBox::StandardButton button);

    /// Sets the button, which is activated by the Escape key and by closing of
    /// the window. Without it the escape button is detected as in QMessageBox:
    /// the only button, the only button, which rejects the message (Cancel),
    /// or the only No button. A message box without an escape button can not
    /// be closed without a choice.
    void setEscapeButton(QDialogButtonBox::StandardButton button);

    /// Adds a button with its own text
    QAbstractButton* addButton(const QString& text, QDialogButtonBox::ButtonRole role);

    /// Returns the button, by which the message box has been closed
    QDialogButtonBox::StandardButton getClickedStandardButton() const { return m_clickedStandardButton; }
    QAbstractButton* getClickedButton() const { return m_clickedButton; }

    QDialogButtonBox* getButtonBox() const { return m_buttonBox; }

    virtual void reject() override;

    static QDialogButtonBox::StandardButton information(QWidget* parent, const QString& title, const QString& text,
                                                        QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                                                        QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);
    static QDialogButtonBox::StandardButton question(QWidget* parent, const QString& title, const QString& text,
                                                     QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::StandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No),
                                                     QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);
    static QDialogButtonBox::StandardButton warning(QWidget* parent, const QString& title, const QString& text,
                                                    QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                                                    QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);
    static QDialogButtonBox::StandardButton critical(QWidget* parent, const QString& title, const QString& text,
                                                     QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                                                     QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    static QDialogButtonBox::StandardButton showMessage(PDFThemeIcons::Badge badge, QWidget* parent, const QString& title, const QString& text,
                                                        QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton);
    void onButtonClicked(QAbstractButton* button);
    void ensureDefaultButton();
    QAbstractButton* getEscapeButton() const;
    static QString getBadgeName(PDFThemeIcons::Badge badge);

    static constexpr int DETAILS_HEIGHT = 150;
    static constexpr int DETAILS_LONG_SIZE = 600;
    static constexpr int DETAILS_LONG_LINES = 8;

    PDFLabel* m_titleLabel;
    PDFLabel* m_textLabel;
    PDFLabel* m_detailsLabel;
    PDFPlainTextEdit* m_detailsEdit;
    QFrame* m_detailsFrame;
    QString m_detailedText;
    QPointer<QAbstractButton> m_escapeButton;
    QCheckBox* m_checkBox;
    QDialogButtonBox* m_buttonBox;
    QAbstractButton* m_clickedButton = nullptr;
    QDialogButtonBox::StandardButton m_clickedStandardButton = QDialogButtonBox::NoButton;
};

}   // namespace pdf

#endif // PDFMESSAGEBOX_H
