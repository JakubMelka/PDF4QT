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
