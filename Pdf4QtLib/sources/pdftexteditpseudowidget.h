//    Copyright (C) 2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFTEXTEDITPSEUDOWIDGET_H
#define PDFTEXTEDITPSEUDOWIDGET_H

#include "pdfglobal.h"
#include "pdfform.h"

#include <QRectF>
#include <QColor>

class QWidget;
class QKeyEvent;

namespace pdf
{

/// "Pseudo" widget, which is emulating text editor, which can be single line, or multiline.
/// Passwords can also be edited and editor can be read only.
class PDFTextEditPseudowidget
{
public:
    explicit PDFTextEditPseudowidget(PDFFormField::FieldFlags flags);

    void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    void keyPressEvent(QWidget* widget, QKeyEvent* event);

    inline bool isReadonly() const { return m_flags.testFlag(PDFFormField::ReadOnly); }
    inline bool isMultiline() const { return m_flags.testFlag(PDFFormField::Multiline); }
    inline bool isPassword() const { return m_flags.testFlag(PDFFormField::Password); }
    inline bool isFileSelect() const { return m_flags.testFlag(PDFFormField::FileSelect); }
    inline bool isComb() const { return m_flags.testFlag(PDFFormField::Comb); }

    inline bool isEmpty() const { return m_editText.isEmpty(); }
    inline bool isTextSelected() const { return !isEmpty() && getSelectionLength() > 0; }
    inline bool isWholeTextSelected() const { return !isEmpty() && getSelectionLength() == m_editText.length(); }

    inline int getTextLength() const { return m_editText.length(); }
    inline int getSelectionLength() const { return m_selectionEnd - m_selectionStart; }

    inline int getPositionCursor() const { return m_positionCursor; }
    inline int getPositionStart() const { return 0; }
    inline int getPositionEnd() const { return getTextLength(); }

    inline void clearSelection() { m_selectionStart = m_selectionEnd = 0; }

    inline const QString& getText() const { return m_editText; }
    inline QString getSelectedText() const { return m_editText.mid(m_selectionStart, getSelectionLength()); }

    /// Sets (updates) text selection
    /// \param startPosition From where we are selecting text
    /// \param selectionLength Selection length (positive - to the right, negative - to the left)
    void setSelection(int startPosition, int selectionLength);

    /// Moves cursor position. It behaves as usual in text boxes,
    /// when user moves the cursor. If \p select is true, then
    /// selection is updated.
    /// \param position New position of the cursor
    /// \param select Select text when moving the cursor?
    void setCursorPosition(int position, bool select);

    /// Sets text content of the widget. This functions sets the text,
    /// even if widget is readonly.
    /// \param text Text to be set
    void setText(const QString& text);

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param appearance Appearance
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param maxTextLength Maximal text length
    void setAppearance(const PDFAnnotationDefaultAppearance& appearance,
                       Qt::Alignment textAlignment,
                       QRectF rect,
                       int maxTextLength);

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param font Font
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param maxTextLength Maximal text length
    /// \param textColor Color
    void setAppearance(const QFont& font,
                       Qt::Alignment textAlignment,
                       QRectF rect,
                       int maxTextLength,
                       QColor textColor);

    void performCut();
    void performCopy();
    void performPaste();
    void performClear();
    void performSelectAll();
    void performBackspace();
    void performDelete();
    void performRemoveSelectedText();
    void performInsertText(const QString& text);

    /// Draw text edit using given parameters
    /// \param parameters Parameters
    void draw(AnnotationDrawParameters& parameters, bool edit) const;

    /// Returns valid cursor position retrieved from position in the widget.
    /// \param point Point in page coordinate space
    /// \param edit Are we using edit transformations?
    /// \returns Cursor position
    int getCursorPositionFromWidgetPosition(const QPointF& point, bool edit) const;

    inline int getCursorForward(QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(getSingleStepForward(), mode); }
    inline int getCursorBackward(QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(getSingleStepBackward(), mode); }
    inline int getCursorCharacterForward() const { return getCursorForward(QTextLayout::SkipCharacters); }
    inline int getCursorCharacterBackward() const { return getCursorBackward(QTextLayout::SkipCharacters); }
    inline int getCursorWordForward() const { return getCursorForward(QTextLayout::SkipWords); }
    inline int getCursorWordBackward() const { return getCursorBackward(QTextLayout::SkipWords); }
    inline int getCursorDocumentStart() const { return (getSingleStepForward() > 0) ? getPositionStart() : getPositionEnd(); }
    inline int getCursorDocumentEnd() const { return (getSingleStepForward() > 0) ? getPositionEnd() : getPositionStart(); }
    inline int getCursorLineStart() const { return (getSingleStepForward() > 0) ? getCurrentLineTextStart() : getCurrentLineTextEnd(); }
    inline int getCursorLineEnd() const { return (getSingleStepForward() > 0) ? getCurrentLineTextEnd() : getCurrentLineTextStart(); }
    inline int getCursorNextLine() const { return getCurrentLineTextEnd(); }
    inline int getCursorPreviousLine() const { return getNextPrevCursorPosition(getCurrentLineTextStart(), -1, QTextLayout::SkipCharacters); }

    const QRectF& getWidgetRect() const { return m_widgetRect; }
    QFont getFont() const { return m_textLayout.font(); }
    QColor getFontColor() const { return m_textColor; }

private:
    /// This function does following things:
    ///     1) Clamps edit text to fit maximum length
    ///     2) Creates display string from edit string
    ///     3) Updates text layout
    void updateTextLayout();

    /// Returns single step forward, which is determined
    /// by cursor move style and layout direction.
    int getSingleStepForward() const;

    /// Returns single step backward, which is determined
    /// by cursor move style and layout direction.
    int getSingleStepBackward() const { return -getSingleStepForward(); }

    /// Returns next/previous position, by number of steps,
    /// using given cursor mode (skipping characters or whole words).
    /// \param steps Number of steps to proceed (can be negative number)
    /// \param mode Skip mode - letters or words?
    int getNextPrevCursorPosition(int steps, QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(m_positionCursor, steps, mode); }

    /// Returns next/previous position from reference cursor position, by number of steps,
    /// using given cursor mode (skipping characters or whole words).
    /// \param referencePosition Reference cursor position
    /// \param steps Number of steps to proceed (can be negative number)
    /// \param mode Skip mode - letters or words?
    int getNextPrevCursorPosition(int referencePosition, int steps, QTextLayout::CursorMode mode) const;

    /// Returns current line text start position
    int getCurrentLineTextStart() const;

    /// Returns current line text end position
    int getCurrentLineTextEnd() const;

    /// Creates text box transform matrix, which transforms from
    /// widget space to page space.
    /// \param edit Create matrix for text editing?
    QTransform createTextBoxTransformMatrix(bool edit) const;

    /// Returns vector of cursor positions (which may be not equal
    /// to edit string length, because edit string can contain surrogates,
    /// or graphemes, which are single glyphs, but represented by more
    /// 16-bit unicode codes).
    std::vector<int> getCursorPositions() const;

    int getCursorLineUp() const;
    int getCursorLineDown() const;

    PDFFormField::FieldFlags m_flags;

    /// Text edited by the user
    QString m_editText;

    /// Text, which is displayed. It can differ from text
    /// edited by user, in case password is being entered.
    QString m_displayText;

    /// Text layout
    QTextLayout m_textLayout;

    /// Character for displaying passwords
    QChar m_passwordReplacementCharacter;

    int m_selectionStart;
    int m_selectionEnd;
    int m_positionCursor;
    int m_maxTextLength;

    QRectF m_widgetRect;
    QColor m_textColor;
};

}   // namespace pdf

#endif // PDFTEXTEDITPSEUDOWIDGET_H
