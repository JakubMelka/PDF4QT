//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfencryptionstrengthhintwidget.h"
#include "pdfwidgetutils.h"
#include "pdfutils.h"

#include <QPainter>
#include <QFontMetrics>

namespace pdfviewer
{

PDFEncryptionStrengthHintWidget::PDFEncryptionStrengthHintWidget(QWidget* parent) :
    BaseClass(parent),
    m_minValue(0),
    m_maxValue(100),
    m_currentValue(50)
{
    m_levels = {
        Level{ Qt::red, tr("Very weak") },
        Level{ QColor::fromRgbF(1.0, 0.5, 0.0, 1.0), tr("Weak") },
        Level{ Qt::yellow, tr("Moderate") },
        Level{ QColor::fromRgbF(0.0, 1.0, 0.5, 1.0), tr("Strong") },
        Level{ Qt::green, tr("Very strong") }
    };
}

QSize PDFEncryptionStrengthHintWidget::sizeHint() const
{
    const QSize markSize = getMarkSize();
    const QSize textSize = getTextSizeHint();
    const int markSpacing = getMarkSpacing();
    const int levels = int(m_levels.size());

    const int width = levels * (markSize.width() + markSpacing) + textSize.width() + markSpacing;
    const int height = qMax(markSize.height(), textSize.height());

    return QSize(width, height);
}

QSize PDFEncryptionStrengthHintWidget::minimumSizeHint() const
{
    return sizeHint();
}

void PDFEncryptionStrengthHintWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    Q_UNUSED(event);

    const QSize markSize = getMarkSize();
    const int markSpacing = getMarkSpacing();
    const int xAdvance = markSize.width() + markSpacing;
    const bool isEnabled = this->isEnabled();

    QRect rect = this->rect();
    painter.fillRect(rect, Qt::lightGray);

    painter.translate(markSpacing, 0);

    int currentLevel = qFloor(pdf::interpolate(m_currentValue, m_minValue, m_maxValue, 0, m_levels.size()));
    if (currentLevel == m_levels.size())
    {
        --currentLevel;
    }
    if (!isEnabled)
    {
        currentLevel = -1;
    }

    Q_ASSERT(currentLevel >= -1);
    Q_ASSERT(currentLevel == -1 || currentLevel < m_levels.size());

    QColor fillColor = Qt::darkGray;
    if (currentLevel >= 0)
    {
        fillColor = m_levels[currentLevel].color;
    }

    QColor invalidColor = Qt::darkGray;

    QRect markRect(QPoint(0, (rect.height() - markSize.height()) / 2), markSize);

    painter.save();
    for (size_t i = 0; i < m_levels.size(); ++i)
    {
        QColor color = (i <= currentLevel) ? fillColor : invalidColor;
        painter.fillRect(markRect, color);
        painter.translate(xAdvance, 0);
        rect.setLeft(rect.left() + xAdvance);
    }
    painter.restore();

    if (isEnabled)
    {
        painter.drawText(rect, Qt::TextSingleLine | Qt::TextDontClip | Qt::AlignLeft | Qt::AlignVCenter, m_levels[currentLevel].text);
    }
}

void PDFEncryptionStrengthHintWidget::correctValue()
{
    setCurrentValue(qBound(m_minValue, m_currentValue, m_maxValue));
}

QSize PDFEncryptionStrengthHintWidget::getMarkSize() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(15, 5));
}

int PDFEncryptionStrengthHintWidget::getMarkSpacing() const
{
    return pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
}

QSize PDFEncryptionStrengthHintWidget::getTextSizeHint() const
{
    QFontMetrics fontMetrics(font(), this);

    const int height = fontMetrics.lineSpacing() + pdf::PDFWidgetUtils::scaleDPI_y(this, 4);
    int width = 0;

    for (const auto& levelItem : m_levels)
    {
        width = qMax(width, fontMetrics.width(levelItem.text));
    }

    return QSize(width, height);
}

int PDFEncryptionStrengthHintWidget::getMaxValue() const
{
    return m_maxValue;
}

void PDFEncryptionStrengthHintWidget::setMaxValue(int maxValue)
{
    m_maxValue = maxValue;
    correctValue();
}

int PDFEncryptionStrengthHintWidget::getMinValue() const
{
    return m_minValue;
}

void PDFEncryptionStrengthHintWidget::setMinValue(int minValue)
{
    m_minValue = minValue;
    correctValue();
}

int PDFEncryptionStrengthHintWidget::getCurrentValue() const
{
    return m_currentValue;
}

void PDFEncryptionStrengthHintWidget::setCurrentValue(int currentValue)
{
    const int adjustedValue = qBound(m_minValue, currentValue, m_maxValue);

    if (m_currentValue != adjustedValue)
    {
        m_currentValue = adjustedValue;
        update();
    }
}

}   // namespace pdfviewer

