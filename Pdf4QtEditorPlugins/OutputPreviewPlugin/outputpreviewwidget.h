//    Copyright (C) 2021 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef OUTPUTPREVIEWWIDGET_H
#define OUTPUTPREVIEWWIDGET_H

#include "pdftransparencyrenderer.h"

#include <QWidget>

namespace pdfplugin
{

class OutputPreviewWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit OutputPreviewWidget(QWidget* parent);

    enum DisplayMode
    {
        Separations,
        ColorWarningInkCoverage,
        ColorWarningRichBlack,
        InkCoverage,
        ShapeChannel,
        OpacityChannel
    };

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Clears all widget contents
    void clear();

    /// Set active image
    void setPageImage(QImage image, pdf::PDFFloatBitmapWithColorSpace originalProcessBitmap, QSizeF pageSizeMM);

    const pdf::PDFInkMapper* getInkMapper() const;
    void setInkMapper(const pdf::PDFInkMapper* inkMapper);

    /// Returns page image size hint (ideal size of page image)
    QSize getPageImageSizeHint() const;

    QColor getAlarmColor() const;
    void setAlarmColor(const QColor& alarmColor);

    DisplayMode getDisplayMode() const;
    void setDisplayMode(const DisplayMode& displayMode);

    pdf::PDFColorComponent getInkCoverageLimit() const;
    void setInkCoverageLimit(pdf::PDFColorComponent inkCoverageLimit);

    pdf::PDFColorComponent getRichBlackLimit() const;
    void setRichBlackLimit(pdf::PDFColorComponent richBlackLimit);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;

private:
    QMargins getDrawMargins() const;
    QRect getContentRect() const;
    QRect getPageImageRect(QRect contentRect) const;

    int getInfoBoxWidth() const;
    int getInfoBoxContentHorizontalMargin() const;
    void buildInfoBoxItems();

    void addInfoBoxHeader(QString caption);
    void addInfoBoxSeparator();
    void addInfoBoxColoredItem(QColor color, QString caption, QString value);
    void addInfoBoxColoredRect(QColor color);

    struct AlarmImageInfo
    {
        QImage image;
        pdf::PDFColorComponent areaValid = 0.0f;
        pdf::PDFColorComponent areaInvalid = 0.0f;
    };

    struct InkCoverageInfo
    {
        QImage image;
        pdf::PDFColorComponent minValue = 0.0f;
        pdf::PDFColorComponent maxValue = 0.0f;
        pdf::PDFColorScale colorScale;
    };

    const std::vector<pdf::PDFColorComponent>& getInkCoverage() const;
    const AlarmImageInfo& getAlarmCoverageImage() const;
    const AlarmImageInfo& getAlarmRichBlackImage() const;
    const InkCoverageInfo& getInkCoverageInfo() const;
    const QImage& getShapeImage() const;
    const QImage& getOpacityImage() const;

    std::vector<pdf::PDFColorComponent> getInkCoverageImpl() const;
    AlarmImageInfo getAlarmCoverageImageImpl() const;
    AlarmImageInfo getAlarmRichBlackImageImpl() const;
    InkCoverageInfo getInkCoverageInfoImpl() const;
    QImage getShapeImageImpl() const;
    QImage getOpacityImageImpl() const;

    enum InfoBoxStyle
    {
        Header,
        Separator,
        ColoredItem,
        ColorOnly
    };

    struct InfoBoxItem
    {
        InfoBoxItem(InfoBoxStyle style, QColor color, QString caption, QString value) :
            style(style),
            color(color),
            caption(caption),
            value(value)
        {

        }

        InfoBoxStyle style = InfoBoxStyle::Separator;
        QColor color;
        QString caption;
        QString value;
    };

    const pdf::PDFInkMapper* m_inkMapper;
    DisplayMode m_displayMode;
    std::vector<InfoBoxItem> m_infoBoxItems;
    QColor m_alarmColor;
    std::optional<QPoint> m_imagePointUnderCursor;
    pdf::PDFColorComponent m_inkCoverageLimit;
    pdf::PDFColorComponent m_richBlackLimit;

    mutable pdf::PDFCachedItem<std::vector<pdf::PDFColorComponent>> m_inkCoverageMM;
    mutable pdf::PDFCachedItem<AlarmImageInfo> m_alarmCoverageImage;
    mutable pdf::PDFCachedItem<AlarmImageInfo> m_alarmRichBlackImage;
    mutable pdf::PDFCachedItem<InkCoverageInfo> m_inkCoverageImage;
    mutable pdf::PDFCachedItem<QImage> m_opacityMask;
    mutable pdf::PDFCachedItem<QImage> m_shapeMask;

    QImage m_pageImage;
    pdf::PDFFloatBitmapWithColorSpace m_originalProcessBitmap;
    QSizeF m_pageSizeMM;
};

}   // pdfplugin

#endif // OUTPUTPREVIEWWIDGET_H
