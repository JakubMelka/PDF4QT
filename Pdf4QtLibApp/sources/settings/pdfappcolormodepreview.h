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

#ifndef PDFAPPCOLORMODEPREVIEW_H
#define PDFAPPCOLORMODEPREVIEW_H

#include "pdfappglobal.h"

#include <QColor>
#include <QFont>
#include <QString>
#include <QWidget>

#include <vector>

class QTextLayout;

namespace pdf
{

/// Preview of the Custom colors (panel 160:4432): the page color with the border
/// border/strong (radius 10, paddings 18 / 16) and three sample texts in the text
/// color, 6 apart - the heading Semi Bold 15, the paragraph Regular 13 with the
/// line 1.5 and the small print Regular 11. The two colors are data (the stored
/// colors of the user), only the border follows the theme. The height follows
/// the width (the texts wrap).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorModePreview : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Paddings of 160:4432 (18 / 16) and its border of 1 px, which takes its own place in Figma
    static constexpr int PADDING_HORIZONTAL = 18 + 1;
    static constexpr int PADDING_VERTICAL = 16 + 1;
    static constexpr int SPACING = 6;
    static constexpr qreal RADIUS = 10.0;

    explicit PDFAppColorModePreview(QWidget* parent = nullptr);
    virtual ~PDFAppColorModePreview() override;

    const QColor& getPageColor() const { return m_pageColor; }
    const QColor& getTextColor() const { return m_textColor; }

    /// Colors of the sample (an invalid color is black page / white text)
    void setColors(const QColor& pageColor, const QColor& textColor);

    /// Sample texts (heading, paragraph, small print)
    QStringList getTexts() const;

    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// A sample text with its font and line height
    struct Paragraph
    {
        QString text;
        QFont font;
        qreal lineHeight = 0.0;
    };

    std::vector<Paragraph> getParagraphs() const;

    /// Lays the paragraph out into \p layout at the width; \returns its height
    qreal layoutParagraph(const Paragraph& paragraph, QTextLayout& layout, qreal width) const;

    QColor m_pageColor;
    QColor m_textColor;
};

}   // namespace pdf

#endif // PDFAPPCOLORMODEPREVIEW_H
