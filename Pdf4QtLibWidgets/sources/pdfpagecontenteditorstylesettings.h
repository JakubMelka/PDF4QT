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

#ifndef PDFPAGECONTENTEDITORSTYLESETTINGS_H
#define PDFPAGECONTENTEDITORSTYLESETTINGS_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"

#include <QPen>
#include <QIcon>
#include <QFont>
#include <QBrush>
#include <QWidget>
#include <QSignalMapper>

namespace Ui
{
class PDFPageContentEditorStyleSettings;
}

class QComboBox;

namespace pdf
{
class PDFPageContentElement;

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentEditorStyleSettings : public QWidget
{
    Q_OBJECT

public:

    enum StyleFeature
    {
        None = 0,
        Pen = 1 << 0,
        PenColor = 1 << 1,
        Brush = 1 << 2,
        Text = 1 << 3
    };
    Q_DECLARE_FLAGS(StyleFeatures, StyleFeature)

    explicit PDFPageContentEditorStyleSettings(QWidget* parent);
    virtual ~PDFPageContentEditorStyleSettings() override;

    /// Loads data from element, element can be nullptr
    /// \param element Element
    void loadFromElement(const PDFPageContentElement* element, bool forceUpdate);

    void setPen(const QPen& pen, bool forceUpdate);
    void setBrush(const QBrush& brush, bool forceUpdate);
    void setFont(const QFont& font, bool forceUpdate);
    void setFontAlignment(Qt::Alignment alignment, bool forceUpdate);
    void setTextAngle(PDFReal angle, bool forceUpdate);

    static bool showEditElementStyleDialog(QWidget* parent, PDFPageContentElement* element);

    const QPen& getPen() const;
    const QBrush& getBrush() const;
    const QFont& getFont() const;
    Qt::Alignment getAlignment() const;
    PDFReal getTextAngle() const;

signals:
    void penChanged(const QPen& pen);
    void brushChanged(const QBrush& brush);
    void fontChanged(const QFont& font);
    void alignmentChanged(Qt::Alignment alignment);
    void textAngleChanged(pdf::PDFReal angle);

private slots:
    void onSelectFontButtonClicked();
    void onSelectPenColorButtonClicked();
    void onSelectBrushColorButtonClicked();
    void onPenWidthChanged(double value);
    void onTextAngleChanged(double value);
    void onAlignmentRadioButtonClicked(int alignment);
    void onPenStyleChanged();
    void onBrushStyleChanged();
    void onPenColorComboTextChanged();
    void onPenColorComboIndexChanged();
    void onBrushColorComboTextChanged();
    void onBrushColorComboIndexChanged();

private:
    Ui::PDFPageContentEditorStyleSettings* ui;

    void onFontChanged(const QFont& font);
    void setColorToComboBox(QComboBox* comboBox, QColor color);
    QIcon getIconForColor(QColor color) const;

    void setPenColor(QColor color);
    void setBrushColor(QColor color);

    QPen m_pen;
    QBrush m_brush;
    QFont m_font;
    Qt::Alignment m_alignment = Qt::AlignCenter;
    QSignalMapper m_alignmentMapper;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORSTYLESETTINGS_H
