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

#include "pdftexteditpseudowidget.h"
#include "pdfpainterutils.h"

#include <QStyle>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QClipboard>

namespace pdf
{

PDFTextEditPseudowidget::PDFTextEditPseudowidget(PDFFormField::FieldFlags flags) :
    m_flags(flags),
    m_selectionStart(0),
    m_selectionEnd(0),
    m_positionCursor(0),
    m_maxTextLength(0)
{
    m_textLayout.setCacheEnabled(true);
    m_passwordReplacementCharacter = QChar(QCoreApplication::style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter));
}

void PDFTextEditPseudowidget::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    constexpr QKeySequence::StandardKey acceptedKeys[] = { QKeySequence::Delete, QKeySequence::Cut, QKeySequence::Copy, QKeySequence::Paste,
                                                           QKeySequence::SelectAll, QKeySequence::MoveToNextChar, QKeySequence::MoveToPreviousChar,
                                                           QKeySequence::MoveToNextWord, QKeySequence::MoveToPreviousWord, QKeySequence::MoveToNextLine,
                                                           QKeySequence::MoveToPreviousLine, QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
                                                           QKeySequence::MoveToStartOfBlock, QKeySequence::MoveToEndOfBlock, QKeySequence::MoveToStartOfDocument,
                                                           QKeySequence::MoveToEndOfDocument, QKeySequence::SelectNextChar, QKeySequence::SelectPreviousChar,
                                                           QKeySequence::SelectNextWord, QKeySequence::SelectPreviousWord, QKeySequence::SelectNextLine,
                                                           QKeySequence::SelectPreviousLine, QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
                                                           QKeySequence::SelectStartOfBlock, QKeySequence::SelectEndOfBlock, QKeySequence::SelectStartOfDocument,
                                                           QKeySequence::SelectEndOfDocument, QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
                                                           QKeySequence::DeleteEndOfLine, QKeySequence::Deselect, QKeySequence::DeleteCompleteLine, QKeySequence::Backspace };

    if (std::any_of(std::begin(acceptedKeys), std::end(acceptedKeys), [event](QKeySequence::StandardKey standardKey) { return event == standardKey; }))
    {
        event->accept();
        return;
    }

    switch (event->key())
    {
        case Qt::Key_Direction_L:
        case Qt::Key_Direction_R:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            event->accept();
            break;

        default:
            break;
    }

    if (!event->text().isEmpty())
    {
        event->accept();
        for (const QChar& character : event->text())
        {
            if (!character.isPrint())
            {
                event->ignore();
                break;
            }
        }
    }
}

void PDFTextEditPseudowidget::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    /*
       We will support following key sequences:
            Delete
            Cut,
            Copy,
            Paste,
            SelectAll,
            MoveToNextChar,
            MoveToPreviousChar,
            MoveToNextWord,
            MoveToPreviousWord,
            MoveToNextLine,
            MoveToPreviousLine,
            MoveToStartOfLine,
            MoveToEndOfLine,
            MoveToStartOfBlock,
            MoveToEndOfBlock,
            MoveToStartOfDocument,
            MoveToEndOfDocument,
            SelectNextChar,
            SelectPreviousChar,
            SelectNextWord,
            SelectPreviousWord,
            SelectNextLine,
            SelectPreviousLine,
            SelectStartOfLine,
            SelectEndOfLine,
            SelectStartOfBlock,
            SelectEndOfBlock,
            SelectStartOfDocument,
            SelectEndOfDocument,
            DeleteStartOfWord,
            DeleteEndOfWord,
            DeleteEndOfLine,
            Deselect,
            DeleteCompleteLine,
            Backspace,
     * */

    event->accept();

    if (event == QKeySequence::Delete)
    {
        performDelete();
    }
    else if (event == QKeySequence::Cut)
    {
        performCut();
    }
    else if (event == QKeySequence::Copy)
    {
        performCopy();
    }
    else if (event == QKeySequence::Paste)
    {
        performPaste();
    }
    else if (event == QKeySequence::SelectAll)
    {
        setSelection(0, getTextLength());
    }
    else if (event == QKeySequence::MoveToNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextWord)
    {
        setCursorPosition(getCursorWordForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextLine)
    {
        setCursorPosition(getCursorLineDown(), false);
    }
    else if (event == QKeySequence::MoveToPreviousLine)
    {
        setCursorPosition(getCursorLineUp(), false);
    }
    else if (event == QKeySequence::MoveToStartOfLine || event == QKeySequence::MoveToStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfLine || event == QKeySequence::MoveToEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), false);
    }
    else if (event == QKeySequence::MoveToStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), false);
    }
    else if (event == QKeySequence::SelectNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), true);
    }
    else if (event == QKeySequence::SelectNextWord)
    {
        setCursorPosition(getCursorWordForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), true);
    }
    else if (event == QKeySequence::SelectNextLine)
    {
        setCursorPosition(getCursorLineDown(), true);
    }
    else if (event == QKeySequence::SelectPreviousLine)
    {
        setCursorPosition(getCursorLineUp(), true);
    }
    else if (event == QKeySequence::SelectStartOfLine || event == QKeySequence::SelectStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfLine || event == QKeySequence::SelectEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), true);
    }
    else if (event == QKeySequence::SelectStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), true);
    }
    else if (event == QKeySequence::DeleteStartOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordBackward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordForward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfLine)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorLineEnd(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Deselect)
    {
        clearSelection();
    }
    else if (event == QKeySequence::DeleteCompleteLine)
    {
        if (!isReadonly())
        {
            m_selectionStart = getCurrentLineTextStart();
            m_selectionEnd = getCurrentLineTextEnd();
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Backspace || event->key() == Qt::Key_Backspace)
    {
        performBackspace();
    }
    else if (event->key() == Qt::Key_Direction_L)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Direction_R)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Up)
    {
        setCursorPosition(getCursorLineUp(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Down)
    {
        setCursorPosition(getCursorLineDown(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Left)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordBackward() : getCursorCharacterBackward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Right)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordForward() : getCursorCharacterForward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (isMultiline() && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        performInsertText(QString::fromUtf16(u"\u2028"));
    }
    else
    {
        QString text = event->text();
        if (!text.isEmpty())
        {
            performInsertText(text);
        }
        else
        {
            event->ignore();
        }
    }
}

void PDFTextEditPseudowidget::setSelection(int startPosition, int selectionLength)
{
    if (selectionLength > 0)
    {
        // We are selecting to the right
        m_selectionStart = startPosition;
        m_selectionEnd = qMin(startPosition + selectionLength, getTextLength());
        m_positionCursor = m_selectionEnd;
    }
    else if (selectionLength < 0)
    {
        // We are selecting to the left
        m_selectionStart = qMax(startPosition + selectionLength, 0);
        m_selectionEnd = startPosition;
        m_positionCursor = m_selectionStart;
    }
    else
    {
        // Clear the selection
        m_selectionStart = 0;
        m_selectionEnd = 0;
        m_positionCursor = startPosition;
    }
}

void PDFTextEditPseudowidget::setCursorPosition(int position, bool select)
{
    if (select)
    {
        const bool isTextSelected = this->isTextSelected();
        const bool isCursorAtStartOfSelection = isTextSelected && m_selectionStart == m_positionCursor;
        const bool isCursorAtEndOfSelection = isTextSelected && m_selectionEnd == m_positionCursor;

        // Do we have selected text, and cursor is at the end of selected text?
        // In this case, we must preserve start of the selection (we are manipulating
        // with the end of selection.
        if (isCursorAtEndOfSelection)
        {
            m_selectionStart = qMin(m_selectionStart, position);
            m_selectionEnd = qMax(m_selectionStart, position);
        }
        else if (isCursorAtStartOfSelection)
        {
            // We must preserve end of the text selection, because we are manipulating
            // with start of text selection.
            m_selectionStart = qMin(m_selectionEnd, position);
            m_selectionEnd = qMax(m_selectionEnd, position);
        }
        else
        {
            // Otherwise we are manipulating with cursor
            m_selectionStart = qMin(m_positionCursor, position);
            m_selectionEnd = qMax(m_positionCursor, position);
        }
    }

    // Why we are clearing text selection, even if we doesn't have it?
    // We can have, for example m_selectionStart == m_selectionEnd == 3,
    // and we want to have it both zero.
    if (!select || !isTextSelected())
    {
        clearSelection();
    }

    m_positionCursor = position;
}

void PDFTextEditPseudowidget::setText(const QString& text)
{
    clearSelection();
    m_editText = text;
    setCursorPosition(getPositionEnd(), false);
    updateTextLayout();
}

void PDFTextEditPseudowidget::setAppearance(const PDFAnnotationDefaultAppearance& appearance, Qt::Alignment textAlignment, QRectF rect, int maxTextLength)
{
    // Set appearance
    qreal fontSize = appearance.getFontSize();
    if (qFuzzyIsNull(fontSize))
    {
        fontSize = rect.height();
    }

    QFont font(appearance.getFontName());
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(qCeil(fontSize));
    font.setStyleStrategy(QFont::ForceOutline);
    m_textLayout.setFont(font);

    QTextOption option = m_textLayout.textOption();
    option.setWrapMode(isMultiline() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(textAlignment);
    option.setUseDesignMetrics(true);
    m_textLayout.setTextOption(option);

    m_textColor = appearance.getFontColor();
    if (!m_textColor.isValid())
    {
        m_textColor = Qt::black;
    }

    m_maxTextLength = maxTextLength;
    m_widgetRect = rect;
    updateTextLayout();
}

void PDFTextEditPseudowidget::setAppearance(const QFont& font,
                                            Qt::Alignment textAlignment,
                                            QRectF rect,
                                            int maxTextLength,
                                            QColor textColor)
{
    // Set appearance
    m_textLayout.setFont(font);

    QTextOption option = m_textLayout.textOption();
    option.setWrapMode(isMultiline() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(textAlignment);
    option.setUseDesignMetrics(true);
    m_textLayout.setTextOption(option);

    m_textColor = textColor;
    if (!m_textColor.isValid())
    {
        m_textColor = Qt::black;
    }

    m_maxTextLength = maxTextLength;
    m_widgetRect = rect;
    updateTextLayout();
}

void PDFTextEditPseudowidget::performCut()
{
    if (isReadonly())
    {
        return;
    }

    performCopy();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performCopy()
{
    if (isTextSelected() && !isPassword())
    {
        QApplication::clipboard()->setText(getSelectedText(), QClipboard::Clipboard);
    }
}

void PDFTextEditPseudowidget::performPaste()
{
    // We always insert text, even if it is empty. Because we want
    // to erase selected text.
    performInsertText(QApplication::clipboard()->text(QClipboard::Clipboard));
}

void PDFTextEditPseudowidget::performClear()
{
    if (isReadonly())
    {
        return;
    }

    performSelectAll();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performSelectAll()
{
    m_selectionStart = getPositionStart();
    m_selectionEnd = getPositionEnd();
}

void PDFTextEditPseudowidget::performBackspace()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.previousCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performDelete()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.nextCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performRemoveSelectedText()
{
    if (isTextSelected())
    {
        m_editText.remove(m_selectionStart, getSelectionLength());
        setCursorPosition(m_selectionStart, false);
        clearSelection();
        updateTextLayout();
    }
}

void PDFTextEditPseudowidget::performInsertText(const QString& text)
{
    if (isReadonly())
    {
        return;
    }

    // Insert text at the cursor
    performRemoveSelectedText();
    m_editText.insert(m_positionCursor, text);
    setCursorPosition(m_positionCursor + text.length(), false);
    updateTextLayout();
}

QTransform PDFTextEditPseudowidget::createTextBoxTransformMatrix(bool edit) const
{
    QTransform matrix;

    matrix.translate(m_widgetRect.left(), m_widgetRect.bottom());
    matrix.scale(1.0, -1.0);

    if (edit && !isComb() && m_textLayout.isValidCursorPosition(m_positionCursor))
    {
        // Jakub Melka: we must scroll the control, so cursor is always
        // visible in the widget area. If we are not editing, this not the
        // case, because we always show the text from the beginning.

        const QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
        if (line.isValid())
        {
            const qreal xCursorPosition = line.cursorToX(m_positionCursor);
            if (xCursorPosition >= m_widgetRect.width())
            {
                const qreal delta = xCursorPosition - m_widgetRect.width();
                matrix.translate(-delta, 0.0);
            }

            // Check, if we aren't outside of y position
            const qreal lineSpacing = line.leadingIncluded() ? line.height() : line.leading() + line.height();
            const qreal lineBottom = lineSpacing * (line.lineNumber() + 1);

            if (lineBottom >= m_widgetRect.height())
            {
                const qreal delta = lineBottom - m_widgetRect.height();
                matrix.translate(0.0, -delta);
            }
        }
    }

    if (!isMultiline() && !isComb())
    {
        // If text is single line, then adjust text position to the vertical center
        QTextLine textLine = m_textLayout.lineAt(0);
        if (textLine.isValid())
        {
            const qreal lineSpacing = textLine.leadingIncluded() ? textLine.height() : textLine.leading() + textLine.height();
            const qreal textBoxHeight = m_widgetRect.height();

            if (lineSpacing < textBoxHeight)
            {
                const qreal delta = (textBoxHeight - lineSpacing) * 0.5;
                matrix.translate(0.0, delta);
            }
        }
    }

    return matrix;
}

std::vector<int> PDFTextEditPseudowidget::getCursorPositions() const
{
    std::vector<int> result;
    result.reserve(m_editText.length());
    result.push_back(0);

    int currentPos = 0;
    while (currentPos < m_editText.length())
    {
        currentPos = m_textLayout.nextCursorPosition(currentPos, QTextLayout::SkipCharacters);
        result.push_back(currentPos);
    }

    return result;
}

void PDFTextEditPseudowidget::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    pdf::PDFPainterStateGuard guard(parameters.painter);

    if (parameters.annotation)
    {
        parameters.boundingRectangle = parameters.annotation->getRectangle();
    }

    QPalette palette = QApplication::palette();
    QPainter* painter = parameters.painter;

    if (edit)
    {
        pdf::PDFPainterStateGuard guard2(painter);
        painter->setPen(parameters.colorConvertor.convert(Qt::black, false, true));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(parameters.boundingRectangle);
    }

    painter->setClipRect(parameters.boundingRectangle, Qt::IntersectClip);
    painter->setWorldTransform(QTransform(createTextBoxTransformMatrix(edit)), true);
    painter->setPen(parameters.colorConvertor.convert(Qt::black, false, true));

    if (isComb())
    {
        const qreal combCount = qMax(m_maxTextLength, 1);
        qreal combWidth = m_widgetRect.width() / combCount;
        QRectF combRect(0.0, 0.0, combWidth, m_widgetRect.height());
        painter->setFont(m_textLayout.font());

        QColor textColor = parameters.colorConvertor.convert(m_textColor, false, true);
        QColor highlightTextColor = parameters.colorConvertor.convert(palette.color(QPalette::HighlightedText), false, true);
        QColor highlightColor = parameters.colorConvertor.convert(palette.color(QPalette::Highlight), false, false);

        std::vector<int> positions = getCursorPositions();
        for (size_t i = 1; i < positions.size(); ++i)
        {
            if (positions[i - 1] >= m_selectionStart && positions[i] - 1 < m_selectionEnd)
            {
                // We are in text selection
                painter->fillRect(combRect, highlightColor);
                painter->setPen(highlightTextColor);
            }
            else
            {
                // We are not in text selection
                painter->setPen(textColor);
            }

            int length = positions[i] - positions[i - 1];
            QString text = m_displayText.mid(positions[i - 1], length);
            painter->drawText(combRect, Qt::AlignCenter, text);

            // Draw the cursor?
            if (edit && m_positionCursor >= positions[i - 1] && m_positionCursor < positions[i])
            {
                QRectF cursorRect(combRect.left(), combRect.bottom() - 1, combRect.width(), 1);
                painter->fillRect(cursorRect, textColor);
            }

            combRect.translate(combWidth, 0.0);
        }

        // Draw the cursor onto next unfilled cell?
        if (edit && m_positionCursor == getPositionEnd())
        {
            QRectF cursorRect(combRect.left(), combRect.bottom() - 1, combRect.width(), 1);
            painter->fillRect(cursorRect, textColor);
        }
    }
    else
    {
        QVector<QTextLayout::FormatRange> selections;

        QTextLayout::FormatRange defaultFormat;
        defaultFormat.start = getPositionStart();
        defaultFormat.length = getTextLength();
        defaultFormat.format.clearBackground();
        defaultFormat.format.setForeground(QBrush(parameters.colorConvertor.convert(m_textColor, false, true), Qt::SolidPattern));

        // If we are editing, draw selections
        if (edit && isTextSelected())
        {
            QTextLayout::FormatRange before = defaultFormat;
            QTextLayout::FormatRange after = defaultFormat;

            before.start = getPositionStart();
            before.length = m_selectionStart;
            after.start = m_selectionEnd;
            after.length = getTextLength() - m_selectionEnd;

            QTextLayout::FormatRange selectedFormat = defaultFormat;
            selectedFormat.start = m_selectionStart;
            selectedFormat.length = getSelectionLength();
            selectedFormat.format.setForeground(QBrush(parameters.colorConvertor.convert(palette.color(QPalette::HighlightedText), false, true), Qt::SolidPattern));
            selectedFormat.format.setBackground(QBrush(parameters.colorConvertor.convert(palette.color(QPalette::Highlight), false, false), Qt::SolidPattern));

            selections = { before, selectedFormat, after};
        }
        else
        {
            selections.push_back(defaultFormat);
        }

        // Draw text
        m_textLayout.draw(painter, QPointF(0.0, 0.0), selections, QRectF());

        // If we are editing, also draw text
        if (edit && !isReadonly())
        {
            m_textLayout.drawCursor(painter, QPointF(0.0, 0.0), m_positionCursor);
        }
    }
}

int PDFTextEditPseudowidget::getCursorPositionFromWidgetPosition(const QPointF& point, bool edit) const
{
    QTransform textBoxSpaceToPageSpace = createTextBoxTransformMatrix(edit);
    QTransform pageSpaceToTextBoxSpace = textBoxSpaceToPageSpace.inverted();

    QPointF textBoxPoint = pageSpaceToTextBoxSpace.map(point);

    if (isComb())
    {
        // If it is comb, then characters are spaced equidistantly
        const qreal x = qBound(0.0, textBoxPoint.x(), m_widgetRect.width());
        const size_t position = qFloor(x * qreal(m_maxTextLength) / qreal(m_widgetRect.width()));
        std::vector<int> positions = getCursorPositions();
        if (position < positions.size())
        {
            return positions[position];
        }

        return positions.back();
    }
    else if (m_textLayout.lineCount() > 0)
    {
        QTextLine line;
        qreal yPos = 0.0;

        // Find line under cursor
        for (int i = 0; i < m_textLayout.lineCount(); ++i)
        {
            QTextLine currentLine = m_textLayout.lineAt(i);
            const qreal lineSpacing = currentLine.leadingIncluded() ? currentLine.height() : currentLine.leading() + currentLine.height();
            const qreal yNextPos = yPos + lineSpacing;

            if (textBoxPoint.y() >= yPos && textBoxPoint.y() < yNextPos)
            {
                line = currentLine;
                break;
            }

            yPos = yNextPos;
        }

        // If no line is found, select last line
        if (!line.isValid())
        {
            if (textBoxPoint.y() < 0.0)
            {
                line = m_textLayout.lineAt(0);
            }
            else
            {
                line = m_textLayout.lineAt(m_textLayout.lineCount() - 1);
            }
        }

        return line.xToCursor(textBoxPoint.x(), QTextLine::CursorBetweenCharacters);
    }

    return 0;
}

void PDFTextEditPseudowidget::updateTextLayout()
{
    // Prepare display text
    if (isPassword())
    {
        m_displayText.resize(m_editText.length(), m_passwordReplacementCharacter);
    }
    else
    {
        m_displayText = m_editText;
    }

    // Perform text layout
    m_textLayout.clearLayout();
    m_textLayout.setText(m_displayText);
    m_textLayout.beginLayout();

    QPointF textLinePosition(0.0, 0.0);

    while (true)
    {
        QTextLine textLine = m_textLayout.createLine();
        if (!textLine.isValid())
        {
            // We are finished with layout
            break;
        }

        textLinePosition.ry() += textLine.leading();
        textLine.setLineWidth(m_widgetRect.width());
        textLine.setPosition(textLinePosition);
        textLinePosition.ry() += textLine.height();
    }
    m_textLayout.endLayout();

    // Check length
    if (m_maxTextLength > 0)
    {
        int length = 0;
        int currentPos = 0;

        while (currentPos < m_editText.length())
        {
            currentPos = m_textLayout.nextCursorPosition(currentPos, QTextLayout::SkipCharacters);
            ++length;

            if (length == m_maxTextLength)
            {
                break;
            }
        }

        if (currentPos < m_editText.length())
        {
            m_editText = m_editText.left(currentPos);
            m_positionCursor = qBound(getPositionStart(), m_positionCursor, getPositionEnd());
            updateTextLayout();
        }
    }
}

int PDFTextEditPseudowidget::getSingleStepForward() const
{
    // If direction is right-to-left, then move backward (because
    // text is painted from right to left.
    return (m_textLayout.textOption().textDirection() == Qt::RightToLeft) ? -1 : 1;
}

int PDFTextEditPseudowidget::getNextPrevCursorPosition(int referencePosition, int steps, QTextLayout::CursorMode mode) const
{
    int cursor = referencePosition;

    if (steps > 0)
    {
        for (int i = 0; i < steps; ++i)
        {
            cursor = m_textLayout.nextCursorPosition(cursor, mode);
        }
    }
    else if (steps < 0)
    {
        for (int i = 0; i < -steps; ++i)
        {
            cursor = m_textLayout.previousCursorPosition(cursor, mode);
        }
    }

    return cursor;
}

int PDFTextEditPseudowidget::getCurrentLineTextStart() const
{
    return m_textLayout.lineForTextPosition(m_positionCursor).textStart();
}

int PDFTextEditPseudowidget::getCurrentLineTextEnd() const
{
    QTextLine textLine = m_textLayout.lineForTextPosition(m_positionCursor);
    return textLine.textStart() + textLine.textLength();
}

int PDFTextEditPseudowidget::getCursorLineUp() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() - 1;

    if (lineIndex >= 0)
    {
        QTextLine upLine = m_textLayout.lineAt(lineIndex);
        return upLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

int PDFTextEditPseudowidget::getCursorLineDown() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() + 1;

    if (lineIndex < m_textLayout.lineCount())
    {
        QTextLine downLine = m_textLayout.lineAt(lineIndex);
        return downLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

}   // namespace pdf
