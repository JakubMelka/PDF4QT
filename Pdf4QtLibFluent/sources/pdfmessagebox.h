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
class QVBoxLayout;

namespace pdf
{

class PDFLabel;
class PDFPlainTextEdit;

/// Message box of the design: a badge with an icon, a title, a text, optional
/// details in a note box, an optional check box and the buttons. It replaces
/// QMessageBox, whose texts take their colors from a palette. The message box
/// is a frameless card (radius 16, shadow 0 / 8 / 24) without the title bar of
/// the system, centered over its parent widget, which is covered by a scrim.
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
    QAbstractButton* getClickedButton() const;

    QDialogButtonBox* getButtonBox() const { return m_buttonBox.data(); }

    virtual ~PDFMessageBox() override;

    virtual void reject() override;
    virtual void setVisible(bool visible) override;

    /// Returns the transparent margins of the window around the card, where
    /// its shadow is drawn. Margins are empty for a message box shown inside
    /// of another widget or, if windows can not be translucent.
    QMargins getShadowMargins() const;

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
    virtual void hideEvent(QHideEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void updateMargins();
    void moveOverParent();
    void showScrim();
    void hideScrim();

    static QDialogButtonBox::StandardButton showMessage(PDFThemeIcons::Badge badge, QWidget* parent, const QString& title, const QString& text,
                                                        QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton);
    void onButtonClicked(QAbstractButton* button);
    void ensureDefaultButton();
    QAbstractButton* getEscapeButton() const;
    static QString getBadgeName(PDFThemeIcons::Badge badge);

    static constexpr int DETAILS_HEIGHT = 150;
    static constexpr int DETAILS_LONG_SIZE = 600;
    static constexpr int DETAILS_LONG_LINES = 8;
    static constexpr int PADDING_H = 22;
    static constexpr int PADDING_V = 20;
    static constexpr qreal CARD_RADIUS = 16.0;

    // The parts are children of the message box, which the application may
    // delete (Qt deletes them with the message box), so they are held by QPointer
    QPointer<QVBoxLayout> m_mainLayout;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_textLabel;
    QPointer<PDFLabel> m_detailsLabel;
    QPointer<PDFPlainTextEdit> m_detailsEdit;
    QPointer<QFrame> m_detailsFrame;
    QString m_detailedText;
    QPointer<QAbstractButton> m_escapeButton;
    QPointer<QCheckBox> m_checkBox;
    QPointer<QDialogButtonBox> m_buttonBox;
    QPointer<QAbstractButton> m_clickedButton;
    QDialogButtonBox::StandardButton m_clickedStandardButton = QDialogButtonBox::NoButton;
    QPointer<QWidget> m_scrim;
};

}   // namespace pdf

#endif // PDFMESSAGEBOX_H
