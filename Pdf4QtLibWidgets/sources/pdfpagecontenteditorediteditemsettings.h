//    Copyright (C) 2024 Jakub Melka
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
        Brush = 1 << 2
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
