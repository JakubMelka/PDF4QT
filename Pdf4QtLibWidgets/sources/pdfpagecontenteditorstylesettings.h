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
