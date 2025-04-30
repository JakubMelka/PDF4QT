// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFENCRYPTIONSTRENGTHHINTWIDGET_H
#define PDFENCRYPTIONSTRENGTHHINTWIDGET_H

#include "pdfviewerglobal.h"

#include <QWidget>

#include <array>

namespace pdfviewer
{

class PDFEncryptionStrengthHintWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFEncryptionStrengthHintWidget(QWidget* parent);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    int getCurrentValue() const;
    void setCurrentValue(int currentValue);

    int getMinValue() const;
    void setMinValue(int minValue);

    int getMaxValue() const;
    void setMaxValue(int maxValue);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

    void correctValue();

    QSize getMarkSize() const;
    int getMarkSpacing() const;
    QSize getTextSizeHint() const;

    struct Level
    {
        QColor color;
        QString text;
    };

    int m_minValue;
    int m_maxValue;
    int m_currentValue;

    std::array<Level, 5> m_levels;
};

}   // namespace pdfviewer

#endif // PDFENCRYPTIONSTRENGTHHINTWIDGET_H
