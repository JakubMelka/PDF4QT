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
    m_scene(scene)
{

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
                                               QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

const PDFPageContentElement* PDFCreatePCElementRectangleTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementRectangleTool::getElement()
{
    return m_element;
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

    setActive(false);
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
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

const PDFPageContentElement* PDFCreatePCElementLineTool::getElement() const
{
    return m_element;
}

PDFPageContentElement* PDFCreatePCElementLineTool::getElement()
{
    return m_element;
}

void PDFCreatePCElementLineTool::clear()
{
    m_startPoint = std::nullopt;
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

    setActive(false);
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
                                         QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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
        painter->setPen(Qt::DotLine);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rectangle);
    }

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
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

    QList<QByteArray> mimeTypes = QImageReader::supportedMimeTypes();
    QStringList mimeTypeFilters;
    for (const QByteArray& mimeType : mimeTypes)
    {
        mimeTypeFilters.append(mimeType);
    }

    QFileDialog dialog(getProxy()->getWidget(), tr("Select Image"));
    dialog.setDirectory(m_imageDirectory);
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/svg+xml");
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

void PDFCreatePCElementImageTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(pageRectangle);
    m_scene->addElement(m_element->clone());

    setActive(false);
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
                                         QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    QPointF point = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());

    PDFPainterStateGuard guard(painter);
    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(m_element->getPen());
    painter->setBrush(m_element->getBrush());
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

    setActive(false);
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
                                                   QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    if (pageIndex != m_element->getPageIndex() || m_element->isEmpty())
    {
        return;
    }

    m_element->drawPage(painter, m_scene, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
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

void PDFCreatePCElementFreehandCurveTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->buttons() & Qt::LeftButton && m_element->getPageIndex() != -1)
    {
        // Try to add point to the path
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex == m_element->getPageIndex())
        {
            m_element->addPoint(pagePoint);
        }

        Q_EMIT getProxy()->repaintNeeded();
    }
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
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

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
        parameters.colorConvertor = getProxy()->getCMSManager()->getColorConvertor();
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

void PDFCreatePCElementTextTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

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
    }

    resetTool();
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
