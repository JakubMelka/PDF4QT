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

#include "pdfappcolormodepreview.h"

#include "pdfpainterutils.h"

#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QEvent>
#include <QPainter>
#include <QStyle>
#include <QTextLayout>

#include <cmath>

namespace pdf
{

PDFAppColorModePreview::PDFAppColorModePreview(QWidget* parent) :
    BaseClass(parent),
    m_pageColor(Qt::black),
    m_textColor(Qt::white)
{
    setObjectName(QStringLiteral("colorPreview"));
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setAccessibleName(tr("Preview of the custom colors"));
}

PDFAppColorModePreview::~PDFAppColorModePreview() = default;

void PDFAppColorModePreview::setColors(const QColor& pageColor, const QColor& textColor)
{
    const QColor page = pageColor.isValid() ? pageColor : QColor(Qt::black);
    const QColor text = textColor.isValid() ? textColor : QColor(Qt::white);
    if (page != m_pageColor || text != m_textColor)
    {
        m_pageColor = page;
        m_textColor = text;
        update();
    }
}

QStringList PDFAppColorModePreview::getTexts() const
{
    QStringList texts;
    for (const Paragraph& paragraph : getParagraphs())
    {
        texts << paragraph.text;
    }
    return texts;
}

std::vector<PDFAppColorModePreview::Paragraph> PDFAppColorModePreview::getParagraphs() const
{
    // Texts of 160:4433 … 160:4435 ("leading normal" of Inter: 18 for 15 px, 13 for 11 px;
    // the paragraph of 1.5 has its lines 20 apart in Figma - two lines are 40 high)
    std::vector<Paragraph> paragraphs;
    paragraphs.push_back({ tr("The quick brown fox jumps over the lazy dog"), PDFThemeFonts::getFont(QFont::DemiBold, 15), 18.0 });
    paragraphs.push_back({ tr("In the Custom colors mode the page and the text of every document are redrawn with this pair. Pictures keep their shapes and only their colors are mapped between the two."),
                           PDFThemeFonts::getFont(QFont::Normal, 13), 20.0 });
    paragraphs.push_back({ tr("Small print, footnotes and page numbers — 0123456789"), PDFThemeFonts::getFont(QFont::Normal, 11), 13.0 });
    return paragraphs;
}

qreal PDFAppColorModePreview::layoutParagraph(const Paragraph& paragraph, QTextLayout& layout, qreal width) const
{
    // QTextLayout takes the alignment as it is (AlignLeading = AlignLeft), only a
    // visual alignment puts the lines of a right-to-left interface to the right
    QTextOption option(QStyle::visualAlignment(layoutDirection(), Qt::AlignLeading));
    option.setTextDirection(layoutDirection());
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setText(paragraph.text);
    layout.setFont(paragraph.font);
    layout.setTextOption(option);

    // Lines of the height of the design, the text centered in its line
    qreal y = 0.0;
    layout.beginLayout();
    for (int guard = 0; guard < 10000; ++guard)
    {
        QTextLine line = layout.createLine();
        if (!line.isValid())
        {
            break;
        }
        line.setLineWidth(qMax(width, 1.0));
        line.setPosition(QPointF(0.0, y + (paragraph.lineHeight - line.height()) * 0.5));
        y += paragraph.lineHeight;
    }
    layout.endLayout();
    return y;
}

bool PDFAppColorModePreview::hasHeightForWidth() const
{
    return true;
}

int PDFAppColorModePreview::heightForWidth(int width) const
{
    const qreal textWidth = width - 2 * PADDING_HORIZONTAL;
    qreal height = 2 * PADDING_VERTICAL;
    const std::vector<Paragraph> paragraphs = getParagraphs();
    for (size_t i = 0; i < paragraphs.size(); ++i)
    {
        QTextLayout layout;
        height += layoutParagraph(paragraphs[i], layout, textWidth);
        height += i + 1 < paragraphs.size() ? SPACING : 0;
    }
    return int(std::ceil(height));
}

QSize PDFAppColorModePreview::sizeHint() const
{
    const int width = 600;
    return QSize(width, heightForWidth(width));
}

QSize PDFAppColorModePreview::minimumSizeHint() const
{
    const int width = 2 * PADDING_HORIZONTAL + 40;
    return QSize(width, 2 * PADDING_VERTICAL);
}

void PDFAppColorModePreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // The panel: the page color with the border of the theme inside the rectangle
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderStrong), 1.0));
    painter.setBrush(m_pageColor);
    painter.drawRoundedRect(panel, RADIUS, RADIUS);

    // The sample texts in the text color
    const qreal textWidth = width() - 2 * PADDING_HORIZONTAL;
    qreal y = PADDING_VERTICAL;
    painter.setPen(m_textColor);
    painter.setClipRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
    for (const Paragraph& paragraph : getParagraphs())
    {
        QTextLayout layout;
        const qreal height = layoutParagraph(paragraph, layout, textWidth);
        layout.draw(&painter, QPointF(PADDING_HORIZONTAL, y));
        y += height + SPACING;
    }
}

void PDFAppColorModePreview::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::LanguageChange || event->type() == QEvent::FontChange)
    {
        updateGeometry();
        update();
    }
}

}   // namespace pdf
