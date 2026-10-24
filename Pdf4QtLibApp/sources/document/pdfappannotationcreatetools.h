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

#ifndef PDFAPPANNOTATIONCREATETOOLS_H
#define PDFAPPANNOTATIONCREATETOOLS_H

#include "pdfappglobal.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfwidgettool.h"

#include <QColor>
#include <QPointer>
#include <QRectF>

namespace pdf
{

/// Tool of Insert - Text box (note 1172:28734): the user drags a rectangle, the
/// tool keeps it outlined (dashed bg/accent) with a live preview of the text,
/// while the text box popup is open; a new rectangle can be drawn only after the
/// area has been cleared (Add, Cancel). The tool creates nothing itself - the
/// creator creates the free text from the popup.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTextBoxTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFAppTextBoxTool(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFAppTextBoxTool() override;

    /// The rectangle of a new text box is drawn
    bool hasArea() const { return m_pageIndex >= 0; }
    PDFInteger getPageIndex() const { return m_pageIndex; }
    const QRectF& getRectangle() const { return m_rectangle; }

    /// Sets the area (as if it was drawn; an invalid page or an empty rectangle is ignored)
    void setArea(PDFInteger pageIndex, const QRectF& rectangle);

    /// Removes the area (the next rectangle can be drawn)
    void clearArea();

    /// Live preview of the text in the area
    void setPreview(const QString& text, const PDFFreeTextStyle& style, const QColor& borderColor, PDFReal borderWidth);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

Q_SIGNALS:
    /// The rectangle has been drawn (page coordinates, normalized)
    void areaPicked(pdf::PDFInteger pageIndex, QRectF rectangle);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool = nullptr;
    PDFInteger m_pageIndex = -1;
    QRectF m_rectangle;
    QString m_previewText;
    PDFFreeTextStyle m_previewStyle;
    QColor m_previewBorderColor;
    PDFReal m_previewBorderWidth = 0.0;
};

/// Tool of Insert - Link to page in two steps (note 1172:28734, frames
/// 1169:26752 / 1170:27127): step 1 draws the link area; step 2 keeps the area
/// outlined on its page, the user navigates freely (scroll, the page field,
/// thumbnails, Go to) and picks the target - a click for Whole page / Page width /
/// Page height / Position, a rectangle for Area; the target under the pointer is
/// highlighted with a tip. The picked target emits linkRequested (the creator
/// creates the link, one undo step) and the tool returns to step 1. goBack (Back,
/// Esc in step 2) returns to step 1 without a link.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageLinkTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    enum class Step
    {
        LinkArea,   ///< 1 / 2 - draw the link area
        Target      ///< 2 / 2 - pick the target
    };

    explicit PDFAppPageLinkTool(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFAppPageLinkTool() override;

    Step getStep() const { return m_step; }

    /// Target of the link (Fit, FitH, FitV, FitR, XYZ; other types are ignored)
    DestinationType getDestinationType() const { return m_destinationType; }
    void setDestinationType(DestinationType type);

    /// Position (XYZ) keeps the zoom of the reader (the zoom is left out)
    bool isKeepZoom() const { return m_keepZoom; }
    void setKeepZoom(bool keepZoom) { m_keepZoom = keepZoom; }

    /// Link area of the step 2
    PDFInteger getLinkPageIndex() const { return m_linkPageIndex; }
    const QRectF& getLinkRectangle() const { return m_linkRectangle; }

    /// Sets the link area as if it was drawn (the step 2 starts)
    void setLinkArea(PDFInteger pageIndex, const QRectF& rectangle);

    /// Picks the target as if the user clicked (\p point) or drew the area
    /// (\p rectangle, Area only) on the page
    void pickTarget(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle);

    /// Returns to the step 1 (the link area is forgotten)
    void goBack();

    /// Destination of a target picked on the page (an invalid destination for an
    /// invalid page or an empty rectangle of Area)
    PDFDestination createDestination(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle) const;

    /// Text of the tip of the target under the pointer (1170:27670): the page
    /// label of the hovered page and the target view ("Page iii · Whole page")
    QString getTipText() const;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

Q_SIGNALS:
    /// The step has changed
    void stepChanged();

    /// The target has been picked: the link area of the step 1 and the destination
    void linkRequested(pdf::PDFInteger pageIndex, QRectF rectangle, pdf::PDFDestination destination);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void onLinkRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void onTargetPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);
    void onTargetRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void setStep(Step step);
    void updatePickTool();

    PDFPickTool* m_areaPickTool = nullptr;
    PDFPickTool* m_pointPickTool = nullptr;
    PDFPickTool* m_rectanglePickTool = nullptr;
    PDFPickTool* m_currentPickTool = nullptr;
    Step m_step = Step::LinkArea;
    DestinationType m_destinationType = DestinationType::Fit;
    bool m_keepZoom = false;
    PDFInteger m_linkPageIndex = -1;
    QRectF m_linkRectangle;
    PDFInteger m_hoverPageIndex = -1;
    QPointF m_hoverPagePoint;
    QPoint m_hoverWidgetPoint;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONCREATETOOLS_H
