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

#include "pdfpagecontenteditortools.h"
#include "pdfpagecontentelements.h"
#include "pdfpainterutils.h"
#include "pdftexteditpseudowidget.h"
#include "pdfdrawwidget.h"

#include <QPen>
#include <QPainter>
#include <QMouseEvent>
#include <QFileDialog>
#include <QImageReader>
#include <QGuiApplication>
#include <QStandardPaths>

namespace pdf
{

PDFCreatePCElementTool::PDFCreatePCElementTool(PDFDrawWidgetProxy* proxy,
                                               PDFPageContentScene* scene,
                                               QAction* action,
                                               QObject* parent) :
    PDFWidgetTool(proxy, action, parent),
    m_scene(scene),
    m_multipleElementCreationEnabled(false)
{

}

void PDFCreatePCElementTool::setMultipleElementCreationEnabled(bool enabled)
{
    m_multipleElementCreationEnabled = enabled;
    m_lastElementSize = QSizeF();
}

bool PDFCreatePCElementTool::isManualGeometryRequested(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::ShiftModifier);
}

void PDFCreatePCElementTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        // Jakub Melka: the size of the last created element is valid only for a single
        // session of the tool, so the user is not surprised by an element created by an
        // accidental click after the tool has been activated again.
        m_lastElementSize = QSizeF();
    }
}

void PDFCreatePCElementTool::finishElementCreation()
{
    if (!m_multipleElementCreationEnabled)
    {
        setActive(false);
    }
}

void PDFCreatePCElementTool::storeLastElementSize(QSizeF size)
{
    m_lastElementSize = m_multipleElementCreationEnabled ? size : QSizeF();
}

QRectF PDFCreatePCElementTool::getLastElementRectangle(const QPointF& point, Qt::KeyboardModifiers modifiers) const
{
    if (!m_multipleElementCreationEnabled || m_lastElementSize.isEmpty() || isManualGeometryRequested(modifiers))
    {
        return QRectF();
    }

    // Jakub Melka: the element is placed into the bottom right quadrant of the cross,
    // which marks the picked point, so the point is the corner from which the user would
    // drag the rectangle - the single click then places the element where the two picked
    // points would place it. The y axis of the page grows upwards, so the edge of the
    // rectangle lying at the picked point is its bottom edge in the page coordinates.
    return QRectF(point.x(),
                  point.y() - m_lastElementSize.height(),
                  m_lastElementSize.width(),
                  m_lastElementSize.height());
}

void PDFCreatePCElementTool::setPen(const QPen& pen)
{
    if (PDFPageContentStyledElement* styledElement = dynamic_cast<PDFPageContentStyledElement*>(getElement()))
    {
        styledElement->setPen(pen);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFCreatePCElementTool::setBrush(const QBrush& brush)
{
    if (PDFPageContentStyledElement* styledElement = dynamic_cast<PDFPageContentStyledElement*>(getElement()))
    {
        styledElement->setBrush(brush);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFCreatePCElementTool::setFont(const QFont& font)
{
    if (PDFPageContentElementTextBox* textBoxElement = dynamic_cast<PDFPageContentElementTextBox*>(getElement()))
    {
        textBoxElement->setFont(font);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFCreatePCElementTool::setAlignment(Qt::Alignment alignment)
{
    if (PDFPageContentElementTextBox* textBoxElement = dynamic_cast<PDFPageContentElementTextBox*>(getElement()))
    {
        textBoxElement->setAlignment(alignment);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFCreatePCElementTool::setTextAngle(PDFReal angle)
{
    if (PDFPageContentElementTextBox* textBoxElement = dynamic_cast<PDFPageContentElementTextBox*>(getElement()))
    {
        textBoxElement->setAngle(angle);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

QRectF PDFCreatePCElementTool::getRectangleFromPickTool(PDFPickTool* pickTool,
                                                        const QTransform& pagePointToDevicePointMatrix)
{
    const std::vector<QPointF>& points = pickTool->getPickedPoints();
    if (points.empty())
    {
        return QRectF();
    }

    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(pickTool->getSnappedPoint());
    QPointF point = points.front();
    qreal xMin = qMin(point.x(), mousePoint.x());
    qreal xMax = qMax(point.x(), mousePoint.x());
    qreal yMin = qMin(point.y(), mousePoint.y());
    qreal yMax = qMax(point.y(), mousePoint.y());
    qreal width = xMax - xMin;
    qreal height = yMax - yMin;

    if (!qFuzzyIsNull(width) && !qFuzzyIsNull(height))
    {
        return QRectF(xMin, yMin, width, height);
    }

    return QRectF();
}

PDFCreatePCElementRectangleTool::PDFCreatePCElementRectangleTool(PDFDrawWidgetProxy* proxy,
                                                                 PDFPageContentScene* scene,
                                                                 QAction* action,
                                                                 bool isRounded,
                                                                 QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementRectangleTool::onPointPicked);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreatePCElementRectangleTool::onRectanglePicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(1.0);

    m_element = new PDFPageContentElementRectangle();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));
    m_element->setRounded(isRounded);

    updateActions();
}

PDFCreatePCElementRectangleTool::~PDFCreatePCElementRectangleTool()
{
    delete m_element;
}

void PDFCreatePCElementRectangleTool::drawPage(QPainter* painter,
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

    QRectF rectangle = getRectangleFromPickTool(m_pickTool, pagePointToDevicePointMatrix);
    if (!rectangle.isValid())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(rectangle);

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);
}

const PDFPageContentElement* PDFCreatePCElementRectangleTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementRectangleTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementRectangleTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    if (m_pickTool->getPickedPoints().size() != 1)
    {
        // The user is already defining the rectangle manually, this point is its
        // second corner - it must not be replaced by the size of the last element.
        return;
    }

    QRectF rectangle = getLastElementRectangle(pagePoint, m_pickTool->getLastPickModifiers());
    if (rectangle.isEmpty())
    {
        // We do not reuse the size of the last created element, so the rectangle
        // is defined by the two points picked by the user.
        return;
    }

    // The first picked point has already been stored by the pick tool, it must be
    // discarded, otherwise it would be used as a corner of the next rectangle.
    m_pickTool->resetTool();

    onRectanglePicked(pageIndex, rectangle);
}

void PDFCreatePCElementRectangleTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(pageRectangle);
    m_scene->addElement(m_element->clone());

    storeLastElementSize(pageRectangle.size());
    finishElementCreation();
}

PDFCreatePCElementLineTool::PDFCreatePCElementLineTool(PDFDrawWidgetProxy* proxy,
                                                       PDFPageContentScene* scene,
                                                       QAction* action,
                                                       bool isHorizontal,
                                                       bool isVertical,
                                                       QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementLineTool::onPointPicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);

    PDFPageContentElementLine::LineGeometry geometry = PDFPageContentElementLine::LineGeometry::General;

    if (isHorizontal)
    {
        geometry = PDFPageContentElementLine::LineGeometry::Horizontal;
    }

    if (isVertical)
    {
        geometry = PDFPageContentElementLine::LineGeometry::Vertical;
    }

    m_element = new PDFPageContentElementLine();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));
    m_element->setGeometry(geometry);

    updateActions();
}

PDFCreatePCElementLineTool::~PDFCreatePCElementLineTool()
{
    delete m_element;
}

void PDFCreatePCElementLineTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pickTool->getPageIndex() || !m_startPoint)
    {
        return;
    }

    m_element->setPageIndex(pageIndex);

    QPointF startPoint = *m_startPoint;
    QPointF endPoint = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());
    QLineF line(startPoint, endPoint);

    if (!qFuzzyIsNull(line.length()))
    {
        m_element->setLine(line);
    }

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);
}

const PDFPageContentElement* PDFCreatePCElementLineTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementLineTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementLineTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        clear();
    }
}

void PDFCreatePCElementLineTool::clear()
{
    // Jakub Melka: both the start point and the page of the line must be forgotten.
    // A start point left from an unfinished line would be used as the start point of
    // the line created in the next session of the tool, so the line would be created
    // somewhere else than the user clicked, or it would not be created at all.
    m_startPoint = std::nullopt;
    m_element->setPageIndex(-1);
}

void PDFCreatePCElementLineTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    if (!m_startPoint || m_element->getPageIndex() != pageIndex)
    {
        m_startPoint = pagePoint;
        m_element->setPageIndex(pageIndex);
        m_element->setLine(QLineF(pagePoint, pagePoint));
        return;
    }

    if (qFuzzyCompare(m_startPoint.value().x(), pagePoint.x()) &&
        qFuzzyCompare(m_startPoint.value().y(), pagePoint.y()))
    {
        // Jakub Melka: Point is same as the start point
        clear();
        return;
    }

    QLineF line = m_element->getLine();
    line.setP2(pagePoint);
    m_element->setLine(line);
    m_scene->addElement(m_element->clone());
    clear();

    finishElementCreation();
}

PDFCreatePCElementImageTool::PDFCreatePCElementImageTool(PDFDrawWidgetProxy* proxy,
                                                     PDFPageContentScene* scene,
                                                     QAction* action,
                                                     QByteArray content,
                                                     bool askSelectImage,
                                                     QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr),
    m_askSelectImage(askSelectImage)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementImageTool::onPointPicked);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreatePCElementImageTool::onRectanglePicked);

    m_element = new PDFPageContentImageElement();
    m_element->setContent(content);

    updateActions();
}

PDFCreatePCElementImageTool::~PDFCreatePCElementImageTool()
{
    delete m_element;
}

void PDFCreatePCElementImageTool::drawPage(QPainter* painter,
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

    QRectF rectangle = getRectangleFromPickTool(m_pickTool, pagePointToDevicePointMatrix);
    if (!rectangle.isValid())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(rectangle);

    {
        PDFPainterStateGuard guard(painter);
        painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(convertor.convert(QPen(Qt::DotLine)));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rectangle);
    }

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);
}

const PDFPageContentElement* PDFCreatePCElementImageTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementImageTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementImageTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active && m_askSelectImage)
    {
        QTimer::singleShot(0, this, &PDFCreatePCElementImageTool::selectImage);
    }
}

void PDFCreatePCElementImageTool::selectImage()
{
    if (m_imageDirectory.isEmpty())
    {
        QStringList pictureDirectiories = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
        if (!pictureDirectiories.isEmpty())
        {
            m_imageDirectory = pictureDirectiories.last();
        }
        else
        {
            m_imageDirectory = QDir::currentPath();
        }
    }

    // Both vector and raster images can be inserted, so all image formats
    // supported by the image reader are offered by default. Filtering by
    // a single format would hide the images the user wants to insert.
    QStringList suffixes = { "*.svg", "*.svgz" };
    for (const QByteArray& format : QImageReader::supportedImageFormats())
    {
        suffixes.append(QString("*.%1").arg(QString::fromLatin1(format).toLower()));
    }
    suffixes.removeDuplicates();
    suffixes.sort();

    QStringList nameFilters;
    nameFilters << tr("Images (%1)").arg(suffixes.join(QChar(' ')));
    nameFilters << tr("All files (*)");

    QFileDialog dialog(getProxy()->getWidget(), tr("Select Image"));
    dialog.setDirectory(m_imageDirectory);
    dialog.setNameFilters(nameFilters);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);

    if (dialog.exec() == QFileDialog::Accepted)
    {
        QString fileName = dialog.selectedFiles().constFirst();
        QFile file(fileName);
        if (file.open(QFile::ReadOnly))
        {
            m_element->setContent(file.readAll());
            file.close();
        }
        else
        {
            setActive(false);
        }
    }
    else
    {
        setActive(false);
    }
}

void PDFCreatePCElementImageTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    if (m_pickTool->getPickedPoints().size() != 1)
    {
        // The user is already defining the rectangle manually, this point is its
        // second corner - it must not be replaced by the size of the last element.
        return;
    }

    QRectF rectangle = getLastElementRectangle(pagePoint, m_pickTool->getLastPickModifiers());
    if (rectangle.isEmpty())
    {
        // We do not reuse the size of the last created element, so the rectangle
        // is defined by the two points picked by the user.
        return;
    }

    // The first picked point has already been stored by the pick tool, it must be
    // discarded, otherwise it would be used as a corner of the next rectangle.
    m_pickTool->resetTool();

    onRectanglePicked(pageIndex, rectangle);
}

void PDFCreatePCElementImageTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(pageRectangle);
    m_scene->addElement(m_element->clone());

    storeLastElementSize(pageRectangle.size());
    finishElementCreation();
}

PDFCreatePCElementDotTool::PDFCreatePCElementDotTool(PDFDrawWidgetProxy* proxy,
                                                     PDFPageContentScene* scene,
                                                     QAction* action,
                                                     QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementDotTool::onPointPicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(5.0);
    pen.setCapStyle(Qt::RoundCap);

    m_element = new PDFPageContentElementDot();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));

    updateActions();
}

PDFCreatePCElementDotTool::~PDFCreatePCElementDotTool()
{
    delete m_element;
}

void PDFCreatePCElementDotTool::drawPage(QPainter* painter,
                                         PDFInteger pageIndex,
                                         const PDFPrecompiledPage* compiledPage,
                                         PDFTextLayoutGetter& layoutGetter,
                                         const QTransform& pagePointToDevicePointMatrix,
                                         const PDFColorConvertor& convertor,
                                         QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    QPointF point = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());

    PDFPainterStateGuard guard(painter);
    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(convertor.convert(m_element->getPen()));
    painter->setBrush(convertor.convert(m_element->getBrush()));
    painter->drawPoint(point);
}

const PDFPageContentElement* PDFCreatePCElementDotTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementDotTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementDotTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    m_element->setPageIndex(pageIndex);
    m_element->setPoint(pagePoint);

    m_scene->addElement(m_element->clone());
    m_element->setPageIndex(-1);

    finishElementCreation();
}

PDFCreatePCElementFreehandCurveTool::PDFCreatePCElementFreehandCurveTool(PDFDrawWidgetProxy* proxy,
                                                                         PDFPageContentScene* scene,
                                                                         QAction* action,
                                                                         QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_element(nullptr)
{
    QPen pen(Qt::SolidLine);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);

    m_element = new PDFPageContentElementFreehandCurve();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));

    // Jakub Melka: the tool draws the same large cross as the tools which pick points,
    // so the user aims the curve the same way as the rest of the creation tools.
    setCursor(Qt::BlankCursor);
}

PDFCreatePCElementFreehandCurveTool::~PDFCreatePCElementFreehandCurveTool()
{
    delete m_element;
}

void PDFCreatePCElementFreehandCurveTool::drawPage(QPainter* painter,
                                                   PDFInteger pageIndex,
                                                   const PDFPrecompiledPage* compiledPage,
                                                   PDFTextLayoutGetter& layoutGetter,
                                                   const QTransform& pagePointToDevicePointMatrix,
                                                   const PDFColorConvertor& convertor,
                                                   QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_element->getPageIndex() || m_element->isEmpty())
    {
        return;
    }

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);
}

const PDFPageContentElement* PDFCreatePCElementFreehandCurveTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementFreehandCurveTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementFreehandCurveTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_element->getPageIndex() == -1 || m_element->getPageIndex() == pageIndex)) // We are under current page
        {
            m_element->setPageIndex(pageIndex);
            m_element->addStartPoint(pagePoint);
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreatePCElementFreehandCurveTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_element->getPageIndex() == pageIndex)) // We are under current page
        {
            m_element->setPageIndex(pageIndex);
            m_element->addPoint(pagePoint);

            if (!m_element->isEmpty())
            {
                m_scene->addElement(m_element->clone());
            }
        }

        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreatePCElementFreehandCurveTool::drawPostRendering(QPainter* painter, QRect rect) const
{
    if (!isActive())
    {
        return;
    }

    drawCross(painter, rect, m_mousePosition, std::nullopt);
}

void PDFCreatePCElementFreehandCurveTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (m_mousePosition == event->pos())
    {
        return;
    }

    m_mousePosition = event->pos();

    if (event->buttons() & Qt::LeftButton && m_element->getPageIndex() != -1)
    {
        // Try to add point to the path
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(m_mousePosition, &pagePoint);
        if (pageIndex == m_element->getPageIndex())
        {
            m_element->addPoint(pagePoint);
        }
    }

    // The cross marking the position of the mouse must be repainted on every move,
    // not only while the curve is being drawn.
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreatePCElementFreehandCurveTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        resetTool();
    }
}

void PDFCreatePCElementFreehandCurveTool::resetTool()
{
    m_element->clear();
}

PDFCreatePCElementTextTool::PDFCreatePCElementTextTool(PDFDrawWidgetProxy* proxy,
                                                       PDFPageContentScene* scene,
                                                       QAction* action,
                                                       QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr),
    m_textEditWidget(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(true);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementTextTool::onPointPicked);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreatePCElementTextTool::onRectanglePicked);

    QFont font = QGuiApplication::font();
    font.setPixelSize(16.0);

    m_element = new PDFPageContentElementTextBox();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(QPen(Qt::SolidLine));
    m_element->setFont(font);

    m_textEditWidget = new PDFTextEditPseudowidget(PDFFormField::Multiline);
}

PDFCreatePCElementTextTool::~PDFCreatePCElementTextTool()
{
    delete m_textEditWidget;
    delete m_element;
}

void PDFCreatePCElementTextTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_element->getPageIndex())
    {
        return;
    }

    if (isEditing())
    {
        PDFPainterStateGuard guard(painter);
        AnnotationDrawParameters parameters;
        parameters.painter = painter;
        parameters.boundingRectangle = m_element->getRectangle();
        parameters.key.first = PDFAppeareanceStreams::Appearance::Normal;
        parameters.colorConvertor = convertor;
        PDFRenderer::applyFeaturesToColorConvertor(getProxy()->getFeatures(), parameters.colorConvertor);

        painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);
        m_textEditWidget->draw(parameters, true);
    }
}

const PDFPageContentElement* PDFCreatePCElementTextTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementTextTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementTextTool::resetTool()
{
    m_textEditWidget->setText(QString());
    m_element->setText(QString());
    m_element->setPageIndex(-1);

    if (getTopToolstackTool())
    {
        removeTool();
    }
}

void PDFCreatePCElementTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_ASSERT(!getTopToolstackTool());
        addTool(m_pickTool);
    }
    else
    {
        resetTool();
    }

    m_pickTool->setActive(active);
}

void PDFCreatePCElementTextTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    if (m_pickTool->getPickedPoints().size() != 1)
    {
        // The user is already defining the rectangle manually, this point is its
        // second corner - it must not be replaced by the size of the last element.
        return;
    }

    QRectF rectangle = getLastElementRectangle(pagePoint, m_pickTool->getLastPickModifiers());
    if (rectangle.isEmpty())
    {
        // We do not reuse the size of the last created element, so the rectangle
        // is defined by the two points picked by the user.
        return;
    }

    // The first picked point has already been stored by the pick tool, it must be
    // discarded, otherwise it would be used as a corner of the next rectangle.
    m_pickTool->resetTool();

    startEditing(pageIndex, rectangle);
}

void PDFCreatePCElementTextTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    startEditing(pageIndex, pageRectangle);
}

void PDFCreatePCElementTextTool::startEditing(PDFInteger pageIndex, QRectF pageRectangle)
{
    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(pageRectangle);

    m_textEditWidget->setAppearance(m_element->getFont(),
                                    m_element->getAlignment(),
                                    m_element->getRectangle(),
                                    std::numeric_limits<int>::max(),
                                    m_element->getPen().color());

    removeTool();
}

void PDFCreatePCElementTextTool::finishEditing()
{
    m_element->setText(m_textEditWidget->getText());

    if (!m_element->getText().isEmpty())
    {
        m_scene->addElement(m_element->clone());
        storeLastElementSize(m_element->getRectangle().size());
    }

    resetTool();

    if (isMultipleElementCreationEnabled())
    {
        // Restore the pick tool, so the user can create the next text label
        Q_ASSERT(!getTopToolstackTool());
        addTool(m_pickTool);
        Q_EMIT getProxy()->repaintNeeded();
        return;
    }

    setActive(false);
}

std::optional<QPointF> PDFCreatePCElementTextTool::getPagePointUnderMouse(QMouseEvent* event) const
{
    QPointF pagePoint;
    PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (pageIndex == m_element->getPageIndex() &&
        m_element->getRectangle().contains(pagePoint))
    {
        return pagePoint;
    }

    return std::nullopt;
}

bool PDFCreatePCElementTextTool::isEditing() const
{
    return isActive() && !getTopToolstackTool();
}

void PDFCreatePCElementTextTool::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (isEditing())
    {
        m_textEditWidget->shortcutOverrideEvent(widget, event);
    }
}

void PDFCreatePCElementTextTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    event->ignore();

    if (!isEditing())
    {
        BaseClass::keyPressEvent(widget, event);
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        return;
    }

    if (!m_textEditWidget->isMultiline() && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        // Commit the editor and create element
        finishEditing();
        event->accept();
        return;
    }

    m_textEditWidget->keyPressEvent(widget, event);

    if (event->isAccepted())
    {
        widget->update();
    }
}

void PDFCreatePCElementTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    if (isEditing())
    {
        if (event->button() == Qt::LeftButton)
        {
            std::optional<QPointF> pagePoint = getPagePointUnderMouse(event);
            if (pagePoint)
            {
                const int cursorPosition = m_textEditWidget->getCursorPositionFromWidgetPosition(pagePoint.value(), true);
                m_textEditWidget->setCursorPosition(cursorPosition, event->modifiers() & Qt::ShiftModifier);
            }
            else
            {
                finishEditing();
            }

            event->accept();
            widget->update();
        }
    }
    else
    {
        BaseClass::mousePressEvent(widget, event);
    }
}

void PDFCreatePCElementTextTool::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    if (isEditing())
    {
        if (event->button() == Qt::LeftButton)
        {
            std::optional<QPointF> pagePoint = getPagePointUnderMouse(event);
            if (pagePoint)
            {
                const int cursorPosition = m_textEditWidget->getCursorPositionFromWidgetPosition(pagePoint.value(), true);
                m_textEditWidget->setCursorPosition(cursorPosition, false);
                m_textEditWidget->setCursorPosition(m_textEditWidget->getCursorWordBackward(), false);
                m_textEditWidget->setCursorPosition(m_textEditWidget->getCursorWordForward(), true);
            }
            else
            {
                finishEditing();
            }

            event->accept();
            widget->update();
        }
    }
    else
    {
        BaseClass::mousePressEvent(widget, event);
    }
}

void PDFCreatePCElementTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (isEditing())
    {
        std::optional<QPointF> pagePoint = getPagePointUnderMouse(event);
        if (pagePoint)
        {
            // We must test, if left mouse button is pressed while
            // we are moving the mouse - if yes, then select the text.
            if (event->buttons() & Qt::LeftButton)
            {
                const int cursorPosition = m_textEditWidget->getCursorPositionFromWidgetPosition(pagePoint.value(), true);
                m_textEditWidget->setCursorPosition(cursorPosition, true);

                event->accept();
                widget->update();
            }
        }
    }
    else
    {
        BaseClass::mouseMoveEvent(widget, event);
    }
}

void PDFCreatePCElementTextTool::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    if (isEditing())
    {
        event->ignore();
    }
    else
    {
        BaseClass::wheelEvent(widget, event);
    }
}

void PDFCreatePCElementTextTool::setFont(const QFont& font)
{
    BaseClass::setFont(font);
    m_textEditWidget->setAppearance(font, m_element->getAlignment(), m_element->getRectangle(), std::numeric_limits<int>::max(), m_element->getPen().color());
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreatePCElementTextTool::setAlignment(Qt::Alignment alignment)
{
    BaseClass::setAlignment(alignment);
    m_textEditWidget->setAppearance(m_element->getFont(), alignment, m_element->getRectangle(), std::numeric_limits<int>::max(), m_element->getPen().color());
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreatePCElementTextTool::setPen(const QPen& pen)
{
    BaseClass::setPen(pen);

    QFont font = m_element->getFont();
    font.setHintingPreference(QFont::PreferNoHinting);
    if (font.pointSizeF() > 0.0)
    {
        font.setPixelSize(qRound(font.pointSizeF()));
    }

    m_textEditWidget->setAppearance(font, m_element->getAlignment(), m_element->getRectangle(), std::numeric_limits<int>::max(), pen.color());
    Q_EMIT getProxy()->repaintNeeded();
}

}   // namespace pdf
