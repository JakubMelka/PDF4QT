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
