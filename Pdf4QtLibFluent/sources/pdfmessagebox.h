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
class QHBoxLayout;
class QPushButton;
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
/// An application with a top bar of its own (the tabs and the buttons of the
/// window) covers only its body: \p setScrimTarget chooses the covered widget,
/// over which the card is centered. The title, the text and the details are
/// plain texts (a name of a file is never interpreted as a rich text).
///
/// A dialog of the design (a form, a list, facts) is the message box with its
/// own parts: \p setBadgeVisible hides the badge (a form has none), \p addContent
/// adds widgets between the head and the buttons, \p addActionButton creates the
/// row of the buttons in the order of the design (not in the order of the
/// platform of QDialogButtonBox; such a button does not close the dialog, the
/// application connects it) and \p setCardWidth gives the card the width of the
/// frame, the texts wrap and the height follows the content.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFMessageBox : public QDialog
{
    Q_OBJECT

public:
    /// Place of a button of the own row of the buttons
    enum class ActionPosition
    {
        Leading,    ///< At the leading edge (a destructive action), in the order of the calls
        Trailing    ///< At the trailing edge, in the order of the calls
    };

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

    /// Shows or hides the badge of the head (a form has no badge; the title and
    /// the text then start at the edge of the card)
    bool isBadgeVisible() const;
    void setBadgeVisible(bool isVisible);

    /// Icon of the design drawn in the badge instead of the icon of its kind, in
    /// the colors of the kind (the external link of the question, whether a web
    /// link is opened); empty = the icon of the kind
    QString getBadgeIconName() const;
    void setBadgeIconName(const QString& iconName);

    /// Adds a part of the content below the head and the details, above the
    /// buttons; the widget becomes a child of the message box. Null is ignored.
    void addContent(QWidget* widget);

    /// Adds a button to the own row of the buttons, which replaces the buttons
    /// of the platform (the button box of the standard buttons is hidden, while
    /// it has no button). Leading buttons come first, trailing ones after the
    /// stretch, each in the order of the calls. The button does not close the
    /// message box by itself. \p isPrimary gives it the look of the primary
    /// button and makes it the default button (Enter). The focus is not moved
    /// (a form keeps the focus of its field).
    QPushButton* addActionButton(const QString& text, ActionPosition position, bool isPrimary);

    /// Returns the buttons of the own row in the order of the row
    QList<QPushButton*> getActionButtons() const;

    /// Width of the card (without the shadow) - 0: the card follows its content.
    /// A card of a fixed width wraps the texts and its height follows the content.
    int getCardWidth() const { return m_cardWidth; }
    void setCardWidth(int width);

    /// Returns the width of the content of a card of a fixed width (the card
    /// without its paddings), 0 for a card, which follows its content
    int getContentWidth() const;

    /// Widget covered by the scrim, over which the card is centered (the body of
    /// the window of an application with its own top bar). Null, or a deleted
    /// target: the parent widget. A change while the message box is shown moves
    /// the scrim and the card.
    QWidget* getScrimTarget() const;
    void setScrimTarget(QWidget* target);

    /// Scrim shown by the message box (nullptr, if none is shown)
    QWidget* getScrim() const { return m_scrim.data(); }

    virtual QSize sizeHint() const override;

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
    void updateSizeConstraint();
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
    QPointer<QHBoxLayout> m_actionsLayout;
    QPointer<QWidget> m_badgeWidget;
    QList<QPointer<QPushButton>> m_actionButtons;
    int m_cardWidth = 0;
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
    QPointer<QWidget> m_scrimTarget;
};

}   // namespace pdf

#endif // PDFMESSAGEBOX_H
