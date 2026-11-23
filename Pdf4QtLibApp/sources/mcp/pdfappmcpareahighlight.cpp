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


#include "pdfappmcpareahighlight.h"
#include "pdfappdocumentview.h"
#include "pdfapplifetimetokens.h"

#include "pdfdrawspacecontroller.h"
#include "pdfpainterutils.h"
#include "pdfthememanager.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>

namespace pdf
{

PDFAppMcpAreaHighlight::PDFAppMcpAreaHighlight(PDFAppDocumentView* view, PDFInteger pageIndex, const QRectF& area) :
    BaseClass(view),
    m_proxy(view ? view->getProxy() : nullptr),
    m_pageIndex(pageIndex),
    m_area(area.normalized())
{
    setObjectName(QStringLiteral("mcpAreaHighlight"));
    m_elapsed.start();

    // The registration on the canvas is a token (step A): retire of the outline or of its view
    // ends it at once, the destructor otherwise
    if (m_proxy)
    {
        m_drawToken = new PDFAppCanvasDrawToken(this, m_proxy, this);
        PDFDeferredDeletion::connect(m_proxy.data(), &QObject::destroyed, this, &PDFAppMcpAreaHighlight::onProxyDestroyed);
    }

    // The pulse is repainted about 16 times per second
    m_timer = new QTimer(this);
    m_timer->setInterval(60);
    connect(m_timer, &QTimer::timeout, this, &PDFAppMcpAreaHighlight::onTimer);
    m_timer->start();
}

PDFAppMcpAreaHighlight::~PDFAppMcpAreaHighlight()
{
    // The interface is unregistered while this object is whole (the token, a child, would end it
    // only in the destructor of QObject)
    if (m_drawToken)
    {
        m_drawToken->releaseToken();
    }
}

PDFAppMcpAreaHighlight* PDFAppMcpAreaHighlight::show(PDFAppDocumentView* view, PDFInteger pageIndex, const QRectF& area)
{
    // A closed (retired) view shows nothing new
    if (!view || !view->getProxy() || PDFDeferredDeletion::isRetired(view))
    {
        return nullptr;
    }

    // One outline per view: a new area replaces the old one (retired: off the canvas now,
    // deleted later - the old outline may be on the stack of a repaint). It leaves the view at
    // once (a plain object, no widget), so the view has one outline from now on.
    if (PDFAppMcpAreaHighlight* oldHighlight = find(view))
    {
        PDFDeferredDeletion::retire(oldHighlight);
        oldHighlight->setParent(nullptr);
    }

    // A receiver of the repaint may delete the view (and the new outline with it)
    QPointer<PDFAppMcpAreaHighlight> highlight = new PDFAppMcpAreaHighlight(view, pageIndex, area);
    Q_EMIT view->getProxy()->repaintNeeded();
    return highlight.data();
}

PDFAppMcpAreaHighlight* PDFAppMcpAreaHighlight::find(const PDFAppDocumentView* view)
{
    if (!view)
    {
        return nullptr;
    }

    // A replaced (retired) outline waits for its deletion: it is not shown any more
    for (PDFAppMcpAreaHighlight* highlight : view->findChildren<PDFAppMcpAreaHighlight*>(QStringLiteral("mcpAreaHighlight"), Qt::FindDirectChildrenOnly))
    {
        if (!PDFDeferredDeletion::isRetired(highlight))
        {
            return highlight;
        }
    }
    return nullptr;
}

qreal PDFAppMcpAreaHighlight::getOpacity() const
{
    const qint64 elapsed = m_elapsed.elapsed();
    if (elapsed >= DURATION)
    {
        return 0.0;
    }

    // Three pulses between 40 % and 100 %
    constexpr double pi = 3.14159265358979323846;
    const double phase = double(elapsed) / double(DURATION) * 3.0 * 2.0 * pi;
    return 0.7 + 0.3 * std::cos(phase);
}

void PDFAppMcpAreaHighlight::drawPage(QPainter* painter,
                                      PDFInteger pageIndex,
                                      const PDFPrecompiledPage* compiledPage,
                                      PDFTextLayoutGetter& layoutGetter,
                                      const QTransform& pagePointToDevicePointMatrix,
                                      const PDFColorConvertor& convertor,
                                      QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);

    const qreal opacity = getOpacity();
    if (!painter || pageIndex != m_pageIndex || opacity <= 0.0)
    {
        return;
    }

    // The outline is a part of the user interface: the accent of the theme, 3 px
    // outside of the area, a light fill inside
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    const QRectF deviceRect = pagePointToDevicePointMatrix.mapRect(m_area).adjusted(-3.0, -3.0, 3.0, 3.0);
    QColor border = manager->getColor(PDFThemeColors::BorderFocus);
    QColor fill = manager->getColor(PDFThemeColors::BgAccentSubtle);
    fill.setAlphaF(float(0.35 * opacity));

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(opacity);
    painter->setBrush(fill);
    painter->setPen(QPen(border, 3.0));
    painter->drawRoundedRect(deviceRect, 4.0, 4.0);
}

void PDFAppMcpAreaHighlight::onTimer()
{
    if (m_proxy)
    {
        // A receiver of the repaint may delete the view and this outline with it
        QPointer<PDFAppMcpAreaHighlight> self(this);
        Q_EMIT m_proxy->repaintNeeded();
        if (!self)
        {
            return;
        }
    }

    if (m_elapsed.elapsed() >= DURATION)
    {
        if (m_timer)
        {
            m_timer->stop();
        }
        deleteLater();
    }
}

void PDFAppMcpAreaHighlight::onProxyDestroyed()
{
    // The proxy is gone: nothing to unregister
    m_proxy = nullptr;
}

}   // namespace pdf

#include "moc_pdfappmcpareahighlight.cpp"
