//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfcompiler.h"

#include <QActionGroup>
#include <QInputDialog>
#include <QKeyEvent>

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
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateLineTypeTool::onPointPicked);

    m_fillColor.setAlphaF(0.2);

    updateActions();
}

void PDFCreateLineTypeTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(pagePoint);

    if (m_type == Type::Line && m_pickTool->getPickedPoints().size() == 2)
    {
        finishDefinition();
    }
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
                    emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
                    emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
                    emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
                                     const QMatrix& pagePointToDevicePointMatrix,
                                     QList<PDFRenderError>& errors) const
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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

    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);

    QPen pen(m_strokeColor);
    QBrush brush(m_fillColor, Qt::SolidPattern);
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

        default:
            Q_ASSERT(false);
            break;
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

    m_fillColor.setAlphaF(0.2);

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
                                    const QMatrix& pagePointToDevicePointMatrix,
                                    QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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

    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);

    QPen pen(m_strokeColor);
    QBrush brush(m_fillColor, Qt::SolidPattern);
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
        emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
                                          const QMatrix& pagePointToDevicePointMatrix,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    if (pageIndex != m_pageIndex || m_pickedPoints.empty())
    {
        return;
    }

    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);

    QPen pen(m_strokeColor);
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

    getProxy()->repaintNeeded();
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
                    emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
        }

        resetTool();
    }

    getProxy()->repaintNeeded();
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
                                  const QMatrix& pagePointToDevicePointMatrix,
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
    QMatrix matrix = getProxy()->getAnnotationManager()->prepareTransformations(pagePointToDevicePointMatrix, painter->device(), m_stampAnnotation.getFlags(), page, rectangle);
    painter->setWorldMatrix(matrix, true);

    AnnotationDrawParameters parameters;
    parameters.painter = painter;
    parameters.annotation = const_cast<PDFStampAnnotation*>(&m_stampAnnotation);
    parameters.key.first = PDFAppeareanceStreams::Appearance::Normal;
    parameters.invertColors = getProxy()->getFeatures().testFlag(PDFRenderer::InvertColors);

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
        emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateHighlightTextTool::PDFCreateHighlightTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_type(AnnotationType::Highlight),
    m_isCursorOverText(false)
{
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateHighlightTextTool::onActionTriggered);

    updateActions();
}

void PDFCreateHighlightTextTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QMatrix& pagePointToDevicePointMatrix,
                                          QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix);
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
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint));

                QPolygonF quadrilaterals;
                PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
                QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QMatrix(), &quadrilaterals);

                if (!path.isEmpty())
                {
                    PDFDocumentModifier modifier(getDocument());

                    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
                    PDFObjectReference annotationReference;
                    switch (m_type)
                    {
                        case AnnotationType::Highlight:
                            annotationReference = modifier.getBuilder()->createAnnotationHighlight(page, quadrilaterals, Qt::yellow);
                            modifier.getBuilder()->setAnnotationOpacity(annotationReference, 0.2);
                            modifier.getBuilder()->updateAnnotationAppearanceStreams(annotationReference);
                            break;

                        case AnnotationType::Underline:
                            annotationReference = modifier.getBuilder()->createAnnotationUnderline(page, quadrilaterals, Qt::black);
                            break;

                        case AnnotationType::Squiggly:
                            annotationReference = modifier.getBuilder()->createAnnotationSquiggly(page, quadrilaterals, Qt::red);
                            break;

                        case AnnotationType::StrikeOut:
                            annotationReference = modifier.getBuilder()->createAnnotationStrikeout(page, quadrilaterals, Qt::red);
                            break;

                        default:
                            Q_ASSERT(false);
                            break;
                    }

                    modifier.markAnnotationsChanged();

                    if (modifier.finalize())
                    {
                        emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint));
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
    }
}

void PDFCreateHighlightTextTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());

    if (action)
    {
        m_type = static_cast<AnnotationType>(action->data().toInt());
    }
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
        getProxy()->repaintNeeded();
    }
}

} // namespace pdf
