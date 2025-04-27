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

#ifndef PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H
#define PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H

#include <QDialog>
#include <QPen>
#include <QBrush>

class QComboBox;

namespace Ui
{
class PDFPageContentEditorEditedItemSettings;
}

namespace pdf
{
class PDFPageContentElementEdited;

class PDFPageContentEditorEditedItemSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PDFPageContentEditorEditedItemSettings(QWidget* parent);
    virtual ~PDFPageContentEditorEditedItemSettings() override;

    void loadFromElement(PDFPageContentElementEdited* editedElement);
    void saveToElement(PDFPageContentElementEdited* editedElement);

    void setPen(const QPen& pen, bool forceUpdate);
    void setBrush(const QBrush& brush, bool forceUpdate);

    const QPen& getPen() const;
    const QBrush& getBrush() const;

private:
    void onSelectPenColorButtonClicked();
    void onSelectBrushColorButtonClicked();
    void onPenWidthChanged(double value);
    void onPenStyleChanged();
    void onBrushStyleChanged();
    void onPenColorComboTextChanged();
    void onPenColorComboIndexChanged();
    void onBrushColorComboTextChanged();
    void onBrushColorComboIndexChanged();

    void setImage(QImage image);
    void selectImage();

    enum StyleFeature
    {
        None = 0,
        Pen = 1 << 0,
        PenColor = 1 << 1,
        Brush = 1 << 2,
        StrokeFill = 1 << 3,
    };
    Q_DECLARE_FLAGS(StyleFeatures, StyleFeature)

    void setColorToComboBox(QComboBox* comboBox, QColor color);
    QIcon getIconForColor(QColor color) const;

    void setPenColor(QColor color);
    void setBrushColor(QColor color);

    Ui::PDFPageContentEditorEditedItemSettings* ui;
    QImage m_image;
    QPen m_pen;
    QBrush m_brush;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITOREDITEDITEMSETTINGS_H
