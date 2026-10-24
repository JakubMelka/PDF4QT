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

#ifndef PDFAPPMARKUPBAR_H
#define PDFAPPMARKUPBAR_H

#include "pdfappglobal.h"
#include "pdfappfloatingbar.h"

#include "pdfannotation.h"

#include <QPointer>

#include <map>

namespace pdf
{

class PDFAppAnnotationCreator;
class PDFAppFloatingBarDivider;
class PDFIconButton;

/// Markup bar over a text selection (Figma 877:133774, bar 877:134226, tools.md
/// 9.8): the compact floating bar (38 high, radius 10) centered on the selection,
/// 8 px above it (below it, when there is no room), clamped to the canvas.
/// Highlight (in its last color), Underline, Strikeout, Squiggly, Highlight and
/// comment, a divider and Copy - Icon buttons 28 with tooltips and shortcuts. A
/// click marks the selection at once (one undo step). A locked document or one
/// without the permission shows only Copy. The creator shows and hides the bar
/// (PDFAppAnnotationCreator::updateMarkupBar).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkupBar : public PDFAppFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppFloatingBar;

public:
    /// Buttons of the bar
    enum class Button
    {
        Highlight,
        Underline,
        Strikeout,
        Squiggly,
        HighlightComment,
        Copy
    };

    explicit PDFAppMarkupBar(PDFAppAnnotationCreator* creator, QWidget* parent);
    virtual ~PDFAppMarkupBar() override;

    /// Shows the bar at the rectangle of the selection (coordinates of the parent);
    /// \p isEditingAllowed false = only Copy
    void showAt(const QRect& selection, bool isEditingAllowed);

    PDFIconButton* getButton(Button button) const;

    /// Reads the color of the highlight and the shortcuts of the tooltips again
    void updateState();

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void addButton(Button button, const QString& iconName);
    void updateTexts();
    void onButtonClicked();

    QPointer<PDFAppAnnotationCreator> m_creator;
    std::map<Button, QPointer<PDFIconButton>> m_buttons;
    QPointer<PDFAppFloatingBarDivider> m_divider;
    QPointer<QWidget> m_leadingSpace;
    QPointer<QWidget> m_trailingSpace;
};

}   // namespace pdf

#endif // PDFAPPMARKUPBAR_H
