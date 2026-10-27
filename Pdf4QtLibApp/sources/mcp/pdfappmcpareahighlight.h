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


#ifndef PDFAPPMCPAREAHIGHLIGHT_H
#define PDFAPPMCPAREAHIGHLIGHT_H

#include "pdfappglobal.h"

#include "pdfdocumentdrawinterface.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRectF>

class QTimer;

namespace pdf
{

class PDFAppDocumentView;
class PDFDrawWidgetProxy;

/// The "look here" outline of view.show_area (plan of step 7, 3.5): an accent
/// outline of an area of a page, which pulses for DURATION milliseconds and
/// then removes itself. A child of the document view, drawn by the proxy of
/// its canvas; a new area of the same view replaces the old one.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpAreaHighlight : public QObject, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Time of the outline in milliseconds
    static constexpr int DURATION = 3000;

    /// Shows the outline of the area (page space) on the page of the view
    static PDFAppMcpAreaHighlight* show(PDFAppDocumentView* view, PDFInteger pageIndex, const QRectF& area);

    /// The outline shown in the view (nullptr = none)
    static PDFAppMcpAreaHighlight* find(const PDFAppDocumentView* view);

    virtual ~PDFAppMcpAreaHighlight() override;

    PDFInteger getPageIndex() const { return m_pageIndex; }
    const QRectF& getArea() const { return m_area; }

    /// Opacity of the outline now (0 - 1)
    qreal getOpacity() const;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

private:
    PDFAppMcpAreaHighlight(PDFAppDocumentView* view, PDFInteger pageIndex, const QRectF& area);

    void onTimer();
    void onProxyDestroyed();

    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QTimer> m_timer;
    QElapsedTimer m_elapsed;
    PDFInteger m_pageIndex = -1;
    QRectF m_area;
};

}   // namespace pdf

#endif // PDFAPPMCPAREAHIGHLIGHT_H
