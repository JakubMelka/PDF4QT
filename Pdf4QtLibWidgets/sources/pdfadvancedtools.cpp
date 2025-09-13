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

#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfcompiler.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetutils.h"

#include <QActionGroup>
#include <QInputDialog>
#include <QColorDialog>
#include <QKeyEvent>
#include <QVector2D>
#include <QApplication>

#include "pdfdbgheap.h"

namespace pdf
{

PDFCreateStickyNoteTool::PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr),
    m_icon(pdf::TextAnnotationIcon::Comment)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateStickyNoteTool::onPointPicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateStickyNoteTool::onActionTriggered);

    updateActions();
}

void PDFCreateStickyNoteTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateStickyNoteTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());

    if (action)
    {
        m_icon = static_cast<TextAnnotationIcon>(action->data().toInt());
    }
}

void PDFCreateStickyNoteTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    bool ok = false;
    QString text = QInputDialog::getText(getProxy()->getWidget(), tr("Sticky note"), tr("Enter text to be displayed in the sticky note"), QLineEdit::Normal, QString(), &ok);

    if (ok && !text.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationText(page, QRectF(pagePoint, QSizeF(0, 0)), m_icon, userName, QString(), text, false);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
    else
    {
        m_pickTool->resetTool();
    }
}

PDFCreateHyperlinkTool::PDFCreateHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateHyperlinkTool::onRectanglePicked);

    updateActions();
}

LinkHighlightMode PDFCreateHyperlinkTool::getHighlightMode() const
{
    return m_highlightMode;
}

void PDFCreateHyperlinkTool::setHighlightMode(const LinkHighlightMode& highlightMode)
{
    m_highlightMode = highlightMode;
}

void PDFCreateHyperlinkTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    bool ok = false;
    QString url = QInputDialog::getText(getProxy()->getWidget(), tr("Hyperlink"), tr("Enter url address of the hyperlink"), QLineEdit::Normal, QString(), &ok);

    if (ok && !url.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationLink(page, pageRectangle, url, m_highlightMode);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
}

PDFCreateFreeTextTool::PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateFreeTextTool::onRectanglePicked);

    updateActions();
}

void PDFCreateFreeTextTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(getProxy()->getWidget(), tr("Text"), tr("Enter text for free text panel"), QString(), &ok);

    if (ok && !text.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationFreeText(page, pageRectangle, userName, QString(), text, TextAlignment(Qt::AlignLeft | Qt::AlignTop));
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
}

PDFCreateAnnotationTool::PDFCreateAnnotationTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent)
{

}

void PDFCreateAnnotationTool::updateActions()
{
    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

PDFCreateLineTypeTool::PDFCreateLineTypeTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, PDFCreateLineTypeTool::Type type, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr),
    m_type(type),
    m_penWidth(1.0),
    m_strokeColor(Qt::red),
    m_fillColor(Qt::yellow)
{
    PDFPickTool::Mode mode = (type != Type::Rectangle) ? PDFPickTool::Mode::Points : PDFPickTool::Mode::Rectangles;
    m_pickTool = new PDFPickTool(proxy, mode, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateLineTypeTool::onPointPicked);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateLineTypeTool::onRectanglePicked);
    m_pickTool->setDrawSelectionRectangle(false);

    m_fillColor.setAlphaF(0.2f);

    updateActions();
}

void PDFCreateLineTypeTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(pagePoint);

    if (isOrthogonalMode())
    {
        m_pickTool->makeLastPointOrthogonal();
    }

    if (m_type == Type::Line && m_pickTool->getPickedPoints().size() == 2)
    {
        finishDefinition();
    }
}

void PDFCreateLineTypeTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    m_rectPageIndex = pageIndex;
    m_rectOnPage = pageRectangle;

    finishDefinition();
}

void PDFCreateLineTypeTool::finishDefinition()
{
    const std::vector<QPointF>& pickedPoints = m_pickTool->getPickedPoints();

    switch (m_type)
    {
        case Type::Line:
        {
            if (pickedPoints.size() >= 2)
            {
                PDFDocumentModifier modifier(getDocument());

                QString userName = PDFSysUtils::getUserName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                modifier.getBuilder()->createAnnotationLine(page, QRectF(), pickedPoints.front(), pickedPoints.back(), m_penWidth, m_fillColor, m_strokeColor, userName, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::PolyLine:
        {
            if (pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : pickedPoints)
                {
                    polygon << point;
                }

                QString userName = PDFSysUtils::getUserName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                modifier.getBuilder()->createAnnotationPolyline(page, polygon, m_penWidth, m_fillColor, m_strokeColor, userName, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::Polygon:
        {
            if (pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : pickedPoints)
                {
                    polygon << point;
                }
                if (!polygon.isClosed())
                {
                    polygon << pickedPoints.front();
                }

                QString userName = PDFSysUtils::getUserName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                PDFObjectReference annotation = modifier.getBuilder()->createAnnotationPolygon(page, polygon, m_penWidth, m_fillColor, m_strokeColor, userName, QString(), QString());
                modifier.getBuilder()->setAnnotationFillOpacity(annotation, m_fillColor.alphaF());
                modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::Rectangle:
        {
            if (!m_rectOnPage.isEmpty())
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                polygon << m_rectOnPage.topLeft();
                polygon << m_rectOnPage.topRight();
                polygon << m_rectOnPage.bottomRight();
                polygon << m_rectOnPage.bottomLeft();
                polygon << m_rectOnPage.topLeft();

                QString userName = PDFSysUtils::getUserName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                PDFObjectReference annotation = modifier.getBuilder()->createAnnotationPolygon(page, polygon, m_penWidth, m_fillColor, m_strokeColor, userName, QString(), QString());
                modifier.getBuilder()->setAnnotationFillOpacity(annotation, m_fillColor.alphaF());
                modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    m_pickTool->resetTool();
}

QColor PDFCreateLineTypeTool::getFillColor() const
{
    return m_fillColor;
}

void PDFCreateLineTypeTool::setFillColor(const QColor& fillColor)
{
    m_fillColor = fillColor;
}

bool PDFCreateLineTypeTool::canHaveOrthogonalMode() const
{
    switch (m_type)
    {
    case Type::Line:
    case Type::PolyLine:
    case Type::Polygon:
        return true;

    case Type::Rectangle:
        break;
    }

    return false;
}

bool PDFCreateLineTypeTool::isOrthogonalMode() const
{
    return canHaveOrthogonalMode() && m_orthogonalMode;
}

QColor PDFCreateLineTypeTool::getStrokeColor() const
{
    return m_strokeColor;
}

void PDFCreateLineTypeTool::setStrokeColor(const QColor& strokeColor)
{
    m_strokeColor = strokeColor;
}

PDFReal PDFCreateLineTypeTool::getPenWidth() const
{
    return m_penWidth;
}

void PDFCreateLineTypeTool::setPenWidth(PDFReal penWidth)
{
    m_penWidth = penWidth;
}

void PDFCreateLineTypeTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (event->key() == Qt::Key_O && canHaveOrthogonalMode())
    {
        m_orthogonalMode = !m_orthogonalMode;
        Q_EMIT getProxy()->repaintNeeded();

        if (m_orthogonalMode)
        {
            Q_EMIT messageDisplayRequest(tr("Orthogonal mode is enabled."), 5000);
        }
        else
        {
            Q_EMIT messageDisplayRequest(tr("Orthogonal mode is disabled."), 5000);
        }
    }

    switch (m_type)
    {
        case Type::PolyLine:
        case Type::Polygon:
        {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
            {
                finishDefinition();
                event->accept();
            }
            else
            {
                event->ignore();
            }

            break;
        }

        default:
            event->ignore();
            break;
    }

    if (!event->isAccepted())
    {
        BaseClass::keyPressEvent(widget, event);
    }
}

void PDFCreateLineTypeTool::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    switch (m_type)
    {
        case Type::PolyLine:
        case Type::Polygon:
        {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
            {
                event->accept();
            }
            else
            {
                event->ignore();
            }

            break;
        }

        default:
            event->ignore();
            break;
    }

    if (!event->isAccepted())
    {
        BaseClass::keyReleaseEvent(widget, event);
    }
}

void PDFCreateLineTypeTool::drawPage(QPainter* painter,
                                     PDFInteger pageIndex,
                                     const PDFPrecompiledPage* compiledPage,
                                     PDFTextLayoutGetter& layoutGetter,
                                     const QTransform& pagePointToDevicePointMatrix,
                                     const PDFColorConvertor& convertor,
                                     QList<PDFRenderError>& errors) const
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pickTool->getPageIndex())
    {
        return;
    }

    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    if (points.empty())
    {
        return;
    }

    QPointF snappedPoint = m_pickTool->getSnappedPoint();
    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(snappedPoint);

    if (isOrthogonalMode())
    {
        mousePoint = m_pickTool->getOrthogonalPoint(mousePoint);
    }

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    QPen pen = convertor.convert(QPen(m_strokeColor));
    QBrush brush = convertor.convert(QBrush(m_fillColor, Qt::SolidPattern));
    pen.setWidthF(m_penWidth);
    painter->setPen(qMove(pen));
    painter->setBrush(qMove(brush));
    painter->setRenderHint(QPainter::Antialiasing);

    switch (m_type)
    {
        case Type::Line:
        case Type::PolyLine:
        {
            for (size_t i = 1; i < points.size(); ++i)
            {
                painter->drawLine(points[i - 1], points[i]);
            }
            painter->drawLine(points.back(), mousePoint);
            break;
        }

        case Type::Polygon:
        {
            QPainterPath path;
            path.moveTo(points.front());
            for (size_t i = 1; i < points.size(); ++i)
            {
                path.lineTo(points[i]);
            }
            path.lineTo(mousePoint);
            path.closeSubpath();

            painter->drawPath(path);
            break;
        }

        case Type::Rectangle:
        {
            QPointF startPoint = points.front();
            qreal x1 = qMin(startPoint.x(), mousePoint.x());
            qreal y1 = qMin(startPoint.y(), mousePoint.y());
            qreal x2 = qMax(startPoint.x(), mousePoint.x());
            qreal y2 = qMax(startPoint.y(), mousePoint.y());

            QRectF rect(x1, y1, x2 - x1, y2 - y1);
            painter->drawRect(rect);
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFCreateLineTypeTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_EMIT messageDisplayRequest(tr("Use key 'C' to show/hide large cross. Use key 'O' to switch on/off orthogonal mode."), 25000);
    }
}

PDFCreateEllipseTool::PDFCreateEllipseTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr),
    m_penWidth(1.0),
    m_strokeColor(Qt::red),
    m_fillColor(Qt::yellow)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateEllipseTool::onRectanglePicked);

    m_fillColor.setAlphaF(0.2f);

    updateActions();
}

PDFReal PDFCreateEllipseTool::getPenWidth() const
{
    return m_penWidth;
}

void PDFCreateEllipseTool::setPenWidth(PDFReal penWidth)
{
    m_penWidth = penWidth;
}

QColor PDFCreateEllipseTool::getStrokeColor() const
{
    return m_strokeColor;
}

void PDFCreateEllipseTool::setStrokeColor(const QColor& strokeColor)
{
    m_strokeColor = strokeColor;
}

QColor PDFCreateEllipseTool::getFillColor() const
{
    return m_fillColor;
}

void PDFCreateEllipseTool::setFillColor(const QColor& fillColor)
{
    m_fillColor = fillColor;
}

void PDFCreateEllipseTool::drawPage(QPainter* painter,
                                    PDFInteger pageIndex,
                                    const PDFPrecompiledPage* compiledPage,
                                    PDFTextLayoutGetter& layoutGetter,
                                    const QTransform& pagePointToDevicePointMatrix,
                                    const PDFColorConvertor& convertor,
                                    QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pickTool->getPageIndex())
    {
        return;
    }

    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    if (points.empty())
    {
        return;
    }

    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    QPen pen = convertor.convert(QPen(m_strokeColor));
    QBrush brush = convertor.convert(QBrush(m_fillColor, Qt::SolidPattern));
    pen.setWidthF(m_penWidth);
    painter->setPen(qMove(pen));
    painter->setBrush(qMove(brush));
    painter->setRenderHint(QPainter::Antialiasing);

    QPointF point = points.front();
    qreal xMin = qMin(point.x(), mousePoint.x());
    qreal xMax = qMax(point.x(), mousePoint.x());
    qreal yMin = qMin(point.y(), mousePoint.y());
    qreal yMax = qMax(point.y(), mousePoint.y());
    qreal width = xMax - xMin;
    qreal height = yMax - yMin;

    if (!qFuzzyIsNull(width) && !qFuzzyIsNull(height))
    {
        QRectF rect(xMin, yMin, width, height);
        painter->drawEllipse(rect);
    }
}

void PDFCreateEllipseTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    QString userName = PDFSysUtils::getUserName();
    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    PDFObjectReference annotation = modifier.getBuilder()->createAnnotationCircle(page, pageRectangle, m_penWidth, m_fillColor, m_strokeColor, userName, QString(), QString());
    modifier.getBuilder()->setAnnotationFillOpacity(annotation, m_fillColor.alphaF());
    modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateFreehandCurveTool::PDFCreateFreehandCurveTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pageIndex(-1),
    m_penWidth(1.0),
    m_strokeColor(Qt::red)
{

}

void PDFCreateFreehandCurveTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pageIndex || m_pickedPoints.empty())
    {
        return;
    }

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    QPen pen = convertor.convert(QPen(m_strokeColor));
    pen.setWidthF(m_penWidth);
    painter->setPen(qMove(pen));
    painter->setRenderHint(QPainter::Antialiasing);

    for (size_t i = 1; i < m_pickedPoints.size(); ++i)
    {
        painter->drawLine(m_pickedPoints[i - 1], m_pickedPoints[i]);
    }
}

void PDFCreateFreehandCurveTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_pageIndex == -1 || m_pageIndex == pageIndex)) // We are under current page
        {
            m_pageIndex = pageIndex;
            m_pickedPoints.push_back(pagePoint);
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateFreehandCurveTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_pageIndex == pageIndex)) // We are under current page
        {
            m_pageIndex = pageIndex;
            m_pickedPoints.push_back(pagePoint);

            if (m_pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : m_pickedPoints)
                {
                    polygon << point;
                }

                QString userName = PDFSysUtils::getUserName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pageIndex)->getPageReference();
                modifier.getBuilder()->createAnnotationPolyline(page, polygon, m_penWidth, Qt::black, m_strokeColor, userName, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
        }

        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateFreehandCurveTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->buttons() & Qt::LeftButton && m_pageIndex != -1)
    {
        // Try to add point to the path
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex == m_pageIndex)
        {
            m_pickedPoints.push_back(pagePoint);
        }

        getProxy()->repaintNeeded();
    }
}

PDFReal PDFCreateFreehandCurveTool::getPenWidth() const
{
    return m_penWidth;
}

void PDFCreateFreehandCurveTool::setPenWidth(const PDFReal& penWidth)
{
    m_penWidth = penWidth;
}

QColor PDFCreateFreehandCurveTool::getStrokeColor() const
{
    return m_strokeColor;
}

void PDFCreateFreehandCurveTool::setStrokeColor(const QColor& strokeColor)
{
    m_strokeColor = strokeColor;
}

void PDFCreateFreehandCurveTool::resetTool()
{
    m_pageIndex = -1;
    m_pickedPoints.clear();
}

PDFCreateStampTool::PDFCreateStampTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_pageIndex(-1),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateStampTool::onPointPicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateStampTool::onActionTriggered);

    m_stampAnnotation.setStrokingOpacity(0.5);
    m_stampAnnotation.setFillingOpacity(0.5);

    updateActions();
}

void PDFCreateStampTool::drawPage(QPainter* painter,
                                  PDFInteger pageIndex,
                                  const PDFPrecompiledPage* compiledPage,
                                  PDFTextLayoutGetter& layoutGetter,
                                  const QTransform& pagePointToDevicePointMatrix,
                                  const PDFColorConvertor& convertor,
                                  QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(pagePointToDevicePointMatrix);
    Q_UNUSED(errors);

    if (pageIndex != m_pageIndex)
    {
        return;
    }

    const PDFPage* page = getDocument()->getCatalog()->getPage(pageIndex);
    QRectF rectangle = m_stampAnnotation.getRectangle();
    QTransform matrix = getProxy()->getAnnotationManager()->prepareTransformations(pagePointToDevicePointMatrix, painter->device(), m_stampAnnotation.getFlags(), page, rectangle);
    painter->setWorldTransform(QTransform(matrix), true);

    AnnotationDrawParameters parameters;
    parameters.painter = painter;
    parameters.annotation = const_cast<PDFStampAnnotation*>(&m_stampAnnotation);
    parameters.key.first = PDFAppeareanceStreams::Appearance::Normal;
    parameters.colorConvertor = convertor;
    PDFRenderer::applyFeaturesToColorConvertor(getProxy()->getFeatures(), parameters.colorConvertor);

    m_stampAnnotation.draw(parameters);
}

void PDFCreateStampTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(widget, event);

    // Try to add point to the path
    QPointF pagePoint;
    m_pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (m_pageIndex != -1)
    {
        m_stampAnnotation.setRectangle(QRectF(pagePoint, QSizeF(0, 0)));
    }
}

void PDFCreateStampTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateStampTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());

    if (action)
    {
        m_stampAnnotation.setStamp(static_cast<Stamp>(action->data().toInt()));
    }
}

void PDFCreateStampTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    PDFDocumentModifier modifier(getDocument());

    QString userName = PDFSysUtils::getUserName();
    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    modifier.getBuilder()->createAnnotationStamp(page, QRectF(pagePoint, QSizeF(0, 0)), m_stampAnnotation.getStamp(), userName, QString(), QString());
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateHighlightTextTool::PDFCreateHighlightTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_colorDialog(nullptr),
    m_type(AnnotationType::Highlight),
    m_isCursorOverText(false)
{
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateHighlightTextTool::onActionTriggered);

    updateActions();
    updateInitialColor();
}

void PDFCreateHighlightTextTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);
    Q_UNUSED(convertor);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix, convertor);
}

void PDFCreateHighlightTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1)
        {
            m_selectionInfo.pageIndex = pageIndex;
            m_selectionInfo.selectionStartPoint = pagePoint;
            event->accept();
        }
        else
        {
            m_selectionInfo = SelectionInfo();
        }

        setSelection(pdf::PDFTextSelection());
        updateCursor();
    }
}

void PDFCreateHighlightTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        if (m_selectionInfo.pageIndex != -1)
        {
            QPointF pagePoint;
            const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

            if (m_selectionInfo.pageIndex == pageIndex)
            {
                // Jakub Melka: handle the selection
                PDFTextLayoutGetter textLayoutGetter = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                PDFTextLayout textLayout = textLayoutGetter;
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));

                QPolygonF quadrilaterals;
                PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
                QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

                if (!path.isEmpty())
                {
                    PDFDocumentModifier modifier(getDocument());

                    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
                    PDFObjectReference annotationReference;
                    switch (m_type)
                    {
                        case AnnotationType::Highlight:
                            annotationReference = modifier.getBuilder()->createAnnotationHighlight(page, quadrilaterals, m_color);
                            modifier.getBuilder()->setAnnotationOpacity(annotationReference, 0.2);
                            modifier.getBuilder()->updateAnnotationAppearanceStreams(annotationReference);
                            break;

                        case AnnotationType::Underline:
                            annotationReference = modifier.getBuilder()->createAnnotationUnderline(page, quadrilaterals, m_color);
                            break;

                        case AnnotationType::Squiggly:
                            annotationReference = modifier.getBuilder()->createAnnotationSquiggly(page, quadrilaterals, m_color);
                            break;

                        case AnnotationType::StrikeOut:
                            annotationReference = modifier.getBuilder()->createAnnotationStrikeout(page, quadrilaterals, m_color);
                            break;

                        default:
                            Q_ASSERT(false);
                            break;
                    }

                    modifier.markAnnotationsChanged();

                    if (modifier.finalize())
                    {
                        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                    }
                }
            }

            setSelection(pdf::PDFTextSelection());

            m_selectionInfo = SelectionInfo();
            event->accept();
            updateCursor();
        }
    }
}

void PDFCreateHighlightTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    m_isCursorOverText = textLayout.isHoveringOverTextBlock(pagePoint);

    if (m_selectionInfo.pageIndex != -1)
    {
        if (m_selectionInfo.pageIndex == pageIndex)
        {
            // Jakub Melka: handle the selection
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));
        }
        else
        {
            setSelection(pdf::PDFTextSelection());
        }

        event->accept();
    }

    updateCursor();
}

void PDFCreateHighlightTextTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateHighlightTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        // Just clear the text selection
        setSelection(PDFTextSelection());

        delete m_colorDialog;
        m_colorDialog = nullptr;
    }
    else
    {
        m_colorDialog = new QColorDialog(m_color, getProxy()->getWidget());
        m_colorDialog->setWindowTitle(tr("Select Color"));
        m_colorDialog->setOption(QColorDialog::ShowAlphaChannel, false);
        m_colorDialog->setOption(QColorDialog::NoButtons, true);
        m_colorDialog->setOption(QColorDialog::DontUseNativeDialog, true);
        m_colorDialog->setOption(QColorDialog::NoEyeDropperButton, true);
        m_colorDialog->setWindowFlag(Qt::Tool);
        m_colorDialog->move(pdf::PDFWidgetUtils::scaleDPI_x(m_colorDialog, 50), pdf::PDFWidgetUtils::scaleDPI_y(m_colorDialog, 50));
        connect(m_colorDialog, &QColorDialog::currentColorChanged, this, &PDFCreateHighlightTextTool::onColorChanged);
        m_colorDialog->show();
    }
}

void PDFCreateHighlightTextTool::onActionTriggered(QAction* action)
{
    if (action)
    {
        AnnotationType type = static_cast<AnnotationType>(action->data().toInt());

        if (m_type != type)
        {
            m_type = type;
            updateInitialColor();
        }
    }

    setActive(action && action->isChecked());
}

void PDFCreateHighlightTextTool::onColorChanged(const QColor& color)
{
    m_color = color;
}

void PDFCreateHighlightTextTool::updateCursor()
{
    if (isActive())
    {
        if (m_isCursorOverText)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
        else
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
}

void PDFCreateHighlightTextTool::setSelection(PDFTextSelection&& textSelection)
{
    if (m_textSelection != textSelection)
    {
        m_textSelection = qMove(textSelection);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFCreateHighlightTextTool::updateInitialColor()
{
    switch (m_type)
    {
    case AnnotationType::Highlight:
        m_color = Qt::yellow;
        break;

    case AnnotationType::Underline:
        m_color = Qt::black;
        break;

    case AnnotationType::Squiggly:
        m_color = Qt::red;
        break;

    case AnnotationType::StrikeOut:
        m_color = Qt::red;
        break;
    }

    if (m_colorDialog)
    {
        m_colorDialog->setCurrentColor(m_color);
    }
}

PDFCreateRedactRectangleTool::PDFCreateRedactRectangleTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSelectionRectangleColor(Qt::black);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateRedactRectangleTool::onRectanglePicked);

    updateActions();
}

void PDFCreateRedactRectangleTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    PDFObjectReference annotation = modifier.getBuilder()->createAnnotationRedact(page, pageRectangle, Qt::black);
    modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateRedactTextTool::PDFCreateRedactTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_isCursorOverText(false)
{
    updateActions();
}

void PDFCreateRedactTextTool::drawPage(QPainter* painter,
                                       PDFInteger pageIndex,
                                       const PDFPrecompiledPage* compiledPage,
                                       PDFTextLayoutGetter& layoutGetter,
                                       const QTransform& pagePointToDevicePointMatrix,
                                       const PDFColorConvertor& convertor,
                                       QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);
    Q_UNUSED(convertor);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix, convertor);
}

void PDFCreateRedactTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1)
        {
            m_selectionInfo.pageIndex = pageIndex;
            m_selectionInfo.selectionStartPoint = pagePoint;
            event->accept();
        }
        else
        {
            m_selectionInfo = SelectionInfo();
        }

        setSelection(pdf::PDFTextSelection());
        updateCursor();
    }
}

void PDFCreateRedactTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        if (m_selectionInfo.pageIndex != -1)
        {
            QPointF pagePoint;
            const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

            if (m_selectionInfo.pageIndex == pageIndex)
            {
                // Jakub Melka: handle the selection
                PDFTextLayoutGetter textLayoutGetter = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                PDFTextLayout textLayout = textLayoutGetter;
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, Qt::black));

                QPolygonF quadrilaterals;
                PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
                QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

                if (!path.isEmpty())
                {
                    PDFDocumentModifier modifier(getDocument());

                    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
                    modifier.getBuilder()->createAnnotationRedact(page, quadrilaterals, Qt::black);
                    modifier.markAnnotationsChanged();

                    if (modifier.finalize())
                    {
                        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                    }
                }
            }

            setSelection(pdf::PDFTextSelection());

            m_selectionInfo = SelectionInfo();
            event->accept();
            updateCursor();
        }
    }
}

void PDFCreateRedactTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    m_isCursorOverText = textLayout.isHoveringOverTextBlock(pagePoint);

    if (m_selectionInfo.pageIndex != -1)
    {
        if (m_selectionInfo.pageIndex == pageIndex)
        {
            // Jakub Melka: handle the selection
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, Qt::black));
        }
        else
        {
            setSelection(pdf::PDFTextSelection());
        }

        event->accept();
    }

    updateCursor();
}

void PDFCreateRedactTextTool::updateActions()
{
    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

void PDFCreateRedactTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        // Just clear the text selection
        setSelection(PDFTextSelection());
    }
}

void PDFCreateRedactTextTool::updateCursor()
{
    if (isActive())
    {
        if (m_isCursorOverText)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
        else
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
}

void PDFCreateRedactTextTool::setSelection(PDFTextSelection&& textSelection)
{
    if (m_textSelection != textSelection)
    {
        m_textSelection = qMove(textSelection);
        getProxy()->repaintNeeded();
    }
}

} // namespace pdf
