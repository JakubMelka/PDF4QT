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

#include "pdfwidgetannotation.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"
#include "pdfdrawspacecontroller.h"
#include "pdfselectpagesdialog.h"
#include "pdfobjecteditorwidget.h"
#include "pdfdocumentbuilder.h"
#include "pdfannotationmanipulator.h"
#include "pdfcms.h"
#include "pdfrenderer.h"

#include <algorithm>
#include <cmath>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QDialog>
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QGroupBox>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QStyleOptionButton>
#include <QDrag>
#include <QMimeData>
#include <QDataStream>
#include <QLocale>
#include <QtMath>

namespace pdf
{

/// Serialization of the data, which describe the annotations dragged by the mouse.
/// The data are light - they hold only references to the dragged annotations, so
/// the drop inside the same document just moves (or copies) the annotations. For
/// a drop into another document (or another instance of the application) the
/// drag also carries the annotations serialized by the annotation manipulator.
class PDFAnnotationDragDataHelper
{
public:
    PDFAnnotationDragDataHelper() = delete;

    struct Payload
    {
        QUuid sourceId;
        PDFObjectReference pageReference;
        std::vector<PDFObjectReference> annotations;
        std::vector<QRectF> rectangles;
        QRectF boundingRectangle;
        QPointF cursorOffset;
    };

    /// Mime type of the drag payload
    static const char* getMimeType() { return "application/x-pdf4qt-annotation"; }

    /// Serializes the payload
    static QByteArray serialize(const Payload& payload);

    /// Deserializes the payload, returns false if the mime data does not contain a valid payload
    static bool deserialize(const QMimeData* data, Payload& payload);

private:
    static constexpr quint32 MAGIC = 0x5044414E; // "PDAN"
    static constexpr quint32 VERSION = 2;
};

QByteArray PDFAnnotationDragDataHelper::serialize(const Payload& payload)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << MAGIC << VERSION;
    stream << payload.sourceId;
    stream << qint32(payload.pageReference.objectNumber) << qint32(payload.pageReference.generation);
    stream << payload.boundingRectangle << payload.cursorOffset;
    stream << quint32(payload.annotations.size());
    for (size_t i = 0; i < payload.annotations.size(); ++i)
    {
        stream << qint32(payload.annotations[i].objectNumber) << qint32(payload.annotations[i].generation);
        stream << payload.rectangles[i];
    }
    return data;
}

bool PDFAnnotationDragDataHelper::deserialize(const QMimeData* data, Payload& payload)
{
    if (!data || !data->hasFormat(getMimeType()))
    {
        return false;
    }

    QByteArray raw = data->data(getMimeType());
    QDataStream stream(&raw, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    stream >> magic >> version;
    if (magic != MAGIC || version != VERSION)
    {
        return false;
    }

    qint32 pageObject = 0;
    qint32 pageGeneration = 0;
    quint32 count = 0;
    stream >> payload.sourceId;
    stream >> pageObject >> pageGeneration;
    stream >> payload.boundingRectangle >> payload.cursorOffset;
    stream >> count;
    payload.pageReference = PDFObjectReference(pageObject, pageGeneration);

    payload.annotations.clear();
    payload.rectangles.clear();
    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i)
    {
        qint32 annotationObject = 0;
        qint32 annotationGeneration = 0;
        QRectF rectangle;
        stream >> annotationObject >> annotationGeneration >> rectangle;

        const PDFObjectReference reference(annotationObject, annotationGeneration);
        if (!reference.isValid())
        {
            return false;
        }

        payload.annotations.push_back(reference);
        payload.rectangles.push_back(rectangle);
    }

    return stream.status() == QDataStream::Ok && payload.pageReference.isValid() && !payload.annotations.empty();
}

PDFWidgetAnnotationManager::PDFWidgetAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(proxy->getFontCache(), proxy->getCMSManager(), proxy->getOptionalContentActivity(), proxy->getMeshQualitySettings(), proxy->getFeatures(), Target::View, parent),
    m_proxy(proxy),
    m_dragSourceId(QUuid::createUuid())
{
    Q_ASSERT(proxy);
    m_proxy->registerDrawInterface(this);
}

PDFWidgetAnnotationManager::~PDFWidgetAnnotationManager()
{
    m_proxy->unregisterDrawInterface(this);
}

void PDFWidgetAnnotationManager::setDocument(const PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset() || document.getFlags().testFlag(PDFModifiedDocument::Annotation))
    {
        m_editableAnnotation = PDFObjectReference();
        m_editableAnnotationPage = PDFObjectReference();
        m_hoveredAnnotation = HoveredAnnotation();
        m_hoveredHandle = Handle::None;
        m_interaction = InteractionState();
        m_dragState = DragState();

        if (document.hasReset() || !m_document)
        {
            clearSelection();
        }
        else
        {
            updateSelectionAfterDocumentChange();
        }
    }
}

void PDFWidgetAnnotationManager::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (!m_document || isToolActive())
    {
        return;
    }

    // Jakub Melka: the application defines actions with the standard shortcuts
    // (copy text, select all text). We must claim the key sequences here, otherwise
    // the actions would consume the shortcuts before the key press reaches us.
    if ((event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) && hasSelection())
    {
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Paste) && canPasteAnnotations())
    {
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::SelectAll))
    {
        PDFWidget* pdfWidget = m_proxy->getWidget();
        if (hasAnyPageAnnotation(pdfWidget->getDrawWidget()->getCurrentPages()))
        {
            event->accept();
            return;
        }
    }
}

void PDFWidgetAnnotationManager::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

bool PDFWidgetAnnotationManager::isToolActive() const
{
    const PDFToolManager* toolManager = m_proxy->getWidget()->getToolManager();
    return toolManager && toolManager->getActiveTool();
}

bool PDFWidgetAnnotationManager::isModificationAllowed() const
{
    return m_document && m_document->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
}

bool PDFWidgetAnnotationManager::isAnnotationSelectable(const PageAnnotation& annotation) const
{
    if (!m_document || !annotation.annotation)
    {
        return false;
    }

    const AnnotationType type = annotation.annotation->getType();
    if (!PDFAnnotation::isTypeEditable(type) || type == AnnotationType::Link)
    {
        return false;
    }

    return !annotation.annotation->isReplyTo() && isAnnotationDrawEnabled(annotation);
}

bool PDFWidgetAnnotationManager::canTransformAnnotation(const PageAnnotation& annotation) const
{
    if (!isAnnotationSelectable(annotation) || !isModificationAllowed())
    {
        return false;
    }

    if (!PDFAnnotationManipulator::isTransformable(annotation.annotation->getType()))
    {
        return false;
    }

    const PDFAnnotation::Flags flags = annotation.annotation->getEffectiveFlags();
    return !flags.testFlag(PDFAnnotation::Locked) && !flags.testFlag(PDFAnnotation::ReadOnly);
}

bool PDFWidgetAnnotationManager::canDeleteAnnotation(const PageAnnotation& annotation) const
{
    if (!m_document || !annotation.annotation || !isModificationAllowed())
    {
        return false;
    }

    const PDFAnnotation::Flags flags = annotation.annotation->getEffectiveFlags();
    return PDFAnnotation::isTypeEditable(annotation.annotation->getType()) &&
           !flags.testFlag(PDFAnnotation::Locked) && !flags.testFlag(PDFAnnotation::ReadOnly) &&
           isAnnotationDrawEnabled(annotation);
}

std::vector<PDFObjectReference> PDFWidgetAnnotationManager::getSelectedAnnotations() const
{
    std::vector<PDFObjectReference> result;
    result.reserve(m_selection.size());
    for (const SelectedAnnotation& item : m_selection)
    {
        result.push_back(item.annotation);
    }
    return result;
}

bool PDFWidgetAnnotationManager::isAnnotationSelected(PDFObjectReference annotation) const
{
    return std::any_of(m_selection.cbegin(), m_selection.cend(), [annotation](const SelectedAnnotation& item) { return item.annotation == annotation; });
}

void PDFWidgetAnnotationManager::setSelectedAnnotations(const std::vector<PDFObjectReference>& annotations)
{
    std::vector<SelectedAnnotation> selection;

    if (m_document)
    {
        for (const PDFObjectReference& annotation : annotations)
        {
            if (std::any_of(selection.cbegin(), selection.cend(), [annotation](const SelectedAnnotation& item) { return item.annotation == annotation; }))
            {
                continue;
            }

            const PDFInteger pageIndex = findAnnotationPage(annotation);
            if (pageIndex == -1)
            {
                continue;
            }

            const PageAnnotation* pageAnnotation = findPageAnnotation(pageIndex, annotation);
            if (pageAnnotation && isAnnotationSelectable(*pageAnnotation))
            {
                selection.push_back(SelectedAnnotation{ annotation, pageIndex });
            }
        }
    }

    m_selection = std::move(selection);
    Q_EMIT selectionChanged();
    requestRepaint();
}

void PDFWidgetAnnotationManager::selectAnnotation(PDFObjectReference annotation, bool exclusive)
{
    if (exclusive)
    {
        setSelectedAnnotations({ annotation });
        return;
    }

    if (isAnnotationSelected(annotation))
    {
        return;
    }

    std::vector<PDFObjectReference> selection = getSelectedAnnotations();
    selection.push_back(annotation);
    setSelectedAnnotations(selection);
}

void PDFWidgetAnnotationManager::deselectAnnotation(PDFObjectReference annotation)
{
    const auto it = std::find_if(m_selection.begin(), m_selection.end(), [annotation](const SelectedAnnotation& item) { return item.annotation == annotation; });
    if (it != m_selection.end())
    {
        m_selection.erase(it);
        Q_EMIT selectionChanged();
        requestRepaint();
    }
}

void PDFWidgetAnnotationManager::clearSelection()
{
    if (!m_selection.empty())
    {
        m_selection.clear();
        Q_EMIT selectionChanged();
        requestRepaint();
    }
}

void PDFWidgetAnnotationManager::selectAllAnnotations()
{
    if (!m_document)
    {
        return;
    }

    std::vector<SelectedAnnotation> selection;
    for (const PDFInteger pageIndex : m_proxy->getWidget()->getDrawWidget()->getCurrentPages())
    {
        const PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
        for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (isAnnotationSelectable(pageAnnotation))
            {
                selection.push_back(SelectedAnnotation{ pageAnnotation.annotation->getSelfReference(), pageIndex });
            }
        }
    }

    m_selection = std::move(selection);
    Q_EMIT selectionChanged();
    requestRepaint();
}

void PDFWidgetAnnotationManager::updateSelectionAfterDocumentChange()
{
    std::vector<SelectedAnnotation> selection;

    for (const SelectedAnnotation& item : m_selection)
    {
        // Annotation is still on its page
        if (item.pageIndex >= 0 && item.pageIndex < PDFInteger(m_document->getCatalog()->getPageCount()) && findPageAnnotation(item.pageIndex, item.annotation))
        {
            selection.push_back(item);
            continue;
        }

        // Annotation has been moved to another page
        const PDFInteger pageIndex = findAnnotationPage(item.annotation);
        if (pageIndex != -1)
        {
            selection.push_back(SelectedAnnotation{ item.annotation, pageIndex });
        }
    }

    if (selection.size() != m_selection.size())
    {
        m_selection = std::move(selection);
        Q_EMIT selectionChanged();
    }
    else
    {
        m_selection = std::move(selection);
    }
}

const PDFAnnotationManager::PageAnnotation* PDFWidgetAnnotationManager::findPageAnnotation(PDFInteger pageIndex, PDFObjectReference annotation) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= PDFInteger(m_document->getCatalog()->getPageCount()))
    {
        return nullptr;
    }

    const PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
    for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
    {
        if (pageAnnotation.annotation->getSelfReference() == annotation)
        {
            return &pageAnnotation;
        }
    }

    return nullptr;
}

PDFInteger PDFWidgetAnnotationManager::findAnnotationPage(PDFObjectReference annotation) const
{
    if (!m_document || !annotation.isValid())
    {
        return -1;
    }

    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&m_document->getStorage(), annotation);
    if (!parsedAnnotation)
    {
        return -1;
    }

    const size_t pageIndex = m_document->getCatalog()->getPageIndexFromPageReference(parsedAnnotation->getPageReference());
    if (pageIndex != PDFCatalog::INVALID_PAGE_INDEX && findPageAnnotation(PDFInteger(pageIndex), annotation))
    {
        return PDFInteger(pageIndex);
    }

    // Jakub Melka: the entry P is optional, so we try the pages, which are
    // displayed - the user can select annotations only on them anyway.
    for (const PDFInteger currentPageIndex : m_proxy->getWidget()->getDrawWidget()->getCurrentPages())
    {
        if (findPageAnnotation(currentPageIndex, annotation))
        {
            return currentPageIndex;
        }
    }

    return -1;
}

std::vector<const PDFAnnotationManager::PageAnnotation*> PDFWidgetAnnotationManager::getSelectedAnnotations(PDFInteger pageIndex, bool transformableOnly) const
{
    std::vector<const PageAnnotation*> result;

    if (!m_document || m_selection.empty())
    {
        return result;
    }

    for (const SelectedAnnotation& item : m_selection)
    {
        if (item.pageIndex != pageIndex)
        {
            continue;
        }

        const PageAnnotation* pageAnnotation = findPageAnnotation(pageIndex, item.annotation);
        if (!pageAnnotation)
        {
            continue;
        }

        if (transformableOnly && !canTransformAnnotation(*pageAnnotation))
        {
            continue;
        }

        result.push_back(pageAnnotation);
    }

    return result;
}

QRectF PDFWidgetAnnotationManager::getSelectionBoundingRectangle(PDFInteger pageIndex, bool transformableOnly) const
{
    QRectF result;

    for (const PageAnnotation* pageAnnotation : getSelectedAnnotations(pageIndex, transformableOnly))
    {
        const QRectF rectangle = pageAnnotation->annotation->getRectangle().normalized();
        if (rectangle.isValid())
        {
            result = result.united(rectangle);
        }
    }

    return result;
}

PDFObjectReference PDFWidgetAnnotationManager::getPageReference(PDFInteger pageIndex, const PageAnnotation& annotation) const
{
    PDFObjectReference pageReference = annotation.annotation->getPageReference();
    if (!pageReference.isValid() && m_document && pageIndex >= 0 && pageIndex < PDFInteger(m_document->getCatalog()->getPageCount()))
    {
        pageReference = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
    }
    return pageReference;
}

const PDFAnnotationManager::PageAnnotation* PDFWidgetAnnotationManager::findSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const
{
    if (!m_document)
    {
        return nullptr;
    }

    PDFWidget* widget = m_proxy->getWidget();
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();

    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        if (!snapshotItem.rect.contains(widgetPos))
        {
            continue;
        }

        const PDFPage* page = m_document->getCatalog()->getPage(snapshotItem.pageIndex);
        const PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);

        // Annotations are drawn in the order of the array, so the last one is on the top
        for (auto it = pageAnnotations.annotations.crbegin(); it != pageAnnotations.annotations.crend(); ++it)
        {
            const PageAnnotation& pageAnnotation = *it;
            if (!isAnnotationSelectable(pageAnnotation))
            {
                continue;
            }

            QRectF annotationRect = pageAnnotation.annotation->getRectangle();
            QTransform matrix = prepareTransformations(snapshotItem.pageToDeviceMatrix, widget, pageAnnotation.annotation->getEffectiveFlags(), page, annotationRect);
            QPainterPath path;
            path.addRect(annotationRect);
            path = matrix.map(path);

            if (path.contains(widgetPos))
            {
                if (pageIndex)
                {
                    *pageIndex = snapshotItem.pageIndex;
                }
                return &pageAnnotation;
            }
        }
    }

    return nullptr;
}

PDFWidgetAnnotationManager::HandleLayout PDFWidgetAnnotationManager::computeHandleLayout(const QRectF& boundingRectangle, const QTransform& pageToDevice) const
{
    HandleLayout layout;

    if (!boundingRectangle.isValid())
    {
        return layout;
    }

    QRectF frame = pageToDevice.mapRect(boundingRectangle).normalized();
    if (frame.isNull())
    {
        return layout;
    }

    const QWidget* widget = m_proxy->getWidget();
    layout.handleSize = PDFWidgetUtils::scaleDPI_x(widget, 8);
    layout.rotationHandleRadius = PDFWidgetUtils::scaleDPI_x(widget, 5);
    const qreal rotationHandleDistance = PDFWidgetUtils::scaleDPI_x(widget, 24);

    layout.isValid = true;
    layout.frame = frame;
    layout.handles[0] = frame.topLeft();
    layout.handles[1] = QPointF(frame.center().x(), frame.top());
    layout.handles[2] = frame.topRight();
    layout.handles[3] = QPointF(frame.right(), frame.center().y());
    layout.handles[4] = frame.bottomRight();
    layout.handles[5] = QPointF(frame.center().x(), frame.bottom());
    layout.handles[6] = frame.bottomLeft();
    layout.handles[7] = QPointF(frame.left(), frame.center().y());
    layout.rotationHandleBase = layout.handles[1];
    layout.rotationHandle = layout.rotationHandleBase - QPointF(0.0, rotationHandleDistance);

    return layout;
}

PDFWidgetAnnotationManager::Handle PDFWidgetAnnotationManager::hitTestHandle(const HandleLayout& layout, const QPointF& devicePosition) const
{
    if (!layout.isValid)
    {
        return Handle::None;
    }

    const qreal tolerance = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 2);

    const QPointF rotationDifference = devicePosition - layout.rotationHandle;
    if (std::hypot(rotationDifference.x(), rotationDifference.y()) <= layout.rotationHandleRadius + tolerance)
    {
        return Handle::Rotate;
    }

    // Corners are tested first, because they overlap with the edge handles, when the frame is small
    constexpr std::array<Handle, 8> handleOrder = { Handle::TopLeft, Handle::TopRight, Handle::BottomRight, Handle::BottomLeft,
                                                    Handle::Top, Handle::Right, Handle::Bottom, Handle::Left };
    const qreal halfSize = layout.handleSize * 0.5 + tolerance;
    for (const Handle handle : handleOrder)
    {
        const QPointF handlePosition = layout.handles[size_t(handle) - size_t(Handle::TopLeft)];
        if (std::abs(devicePosition.x() - handlePosition.x()) <= halfSize && std::abs(devicePosition.y() - handlePosition.y()) <= halfSize)
        {
            return handle;
        }
    }

    return Handle::None;
}

Qt::CursorShape PDFWidgetAnnotationManager::getCursorShapeForHandle(Handle handle)
{
    switch (handle)
    {
        case Handle::Top:
        case Handle::Bottom:
            return Qt::SizeVerCursor;

        case Handle::Left:
        case Handle::Right:
            return Qt::SizeHorCursor;

        case Handle::TopLeft:
        case Handle::BottomRight:
            return Qt::SizeFDiagCursor;

        case Handle::TopRight:
        case Handle::BottomLeft:
            return Qt::SizeBDiagCursor;

        case Handle::Rotate:
            return Qt::CrossCursor;

        default:
            break;
    }

    return Qt::ArrowCursor;
}

const QCursor& PDFWidgetAnnotationManager::getRotationCursor() const
{
    if (m_rotationCursor)
    {
        return *m_rotationCursor;
    }

    // Jakub Melka: Qt has no cursor for rotation, so we draw a circular arrow
    const QWidget* widget = m_proxy->getWidget();
    const qreal devicePixelRatio = widget->devicePixelRatioF();
    const int size = 24;

    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF arcRectangle(5.0, 5.0, 14.0, 14.0);
    QPainterPath path;
    path.arcMoveTo(arcRectangle, 45.0);
    path.arcTo(arcRectangle, 45.0, 270.0);

    // Arrow head at the end of the arc
    const QPointF arrowTip = path.currentPosition();
    path.moveTo(arrowTip + QPointF(-4.5, -1.5));
    path.lineTo(arrowTip);
    path.lineTo(arrowTip + QPointF(1.5, 4.5));

    QPen outline(Qt::white, 4.0);
    outline.setCapStyle(Qt::RoundCap);
    outline.setJoinStyle(Qt::RoundJoin);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    QPen pen(Qt::black, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPath(path);
    painter.end();

    m_rotationCursor = QCursor(pixmap, size / 2, size / 2);
    return *m_rotationCursor;
}

void PDFWidgetAnnotationManager::requestRepaint()
{
    Q_EMIT m_proxy->repaintNeeded();
}

void PDFWidgetAnnotationManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (!m_document)
    {
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        if (m_interaction.type != Interaction::None)
        {
            cancelInteraction();
            event->accept();
            return;
        }

        if (hasSelection())
        {
            clearSelection();
            event->accept();
            return;
        }

        return;
    }

    if (m_interaction.type != Interaction::None)
    {
        // Keyboard is not used during the mouse interaction
        return;
    }

    if (!isToolActive())
    {
        if (event->matches(QKeySequence::Copy) && hasSelection())
        {
            copySelectedAnnotations();
            event->accept();
            return;
        }

        if (event->matches(QKeySequence::Cut) && hasSelection())
        {
            cutSelectedAnnotations();
            event->accept();
            return;
        }

        if (event->matches(QKeySequence::Paste) && canPasteAnnotations())
        {
            QWidget* deviceWidget = m_proxy->getWidget()->getDrawWidget()->getWidget();
            const QPoint widgetPosition = deviceWidget->mapFromGlobal(QCursor::pos());
            pasteAnnotations(deviceWidget->rect().contains(widgetPosition) ? std::optional<QPoint>(widgetPosition) : std::nullopt);
            event->accept();
            return;
        }

        if (event->matches(QKeySequence::SelectAll))
        {
            PDFWidget* pdfWidget = m_proxy->getWidget();
            if (hasAnyPageAnnotation(pdfWidget->getDrawWidget()->getCurrentPages()))
            {
                selectAllAnnotations();
                event->accept();
                return;
            }
        }
    }

    // Arrows nudge the selection. The direction is given on the screen, so it must
    // be converted to the page coordinate system (the page can be rotated).
    if (hasSelection() && event->modifiers().testFlag(Qt::ControlModifier) == false)
    {
        QPointF deviceDirection;
        switch (event->key())
        {
            case Qt::Key_Left:
                deviceDirection = QPointF(-1.0, 0.0);
                break;
            case Qt::Key_Right:
                deviceDirection = QPointF(1.0, 0.0);
                break;
            case Qt::Key_Up:
                deviceDirection = QPointF(0.0, -1.0);
                break;
            case Qt::Key_Down:
                deviceDirection = QPointF(0.0, 1.0);
                break;
            default:
                break;
        }

        if (!deviceDirection.isNull())
        {
            PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
            for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
            {
                if (getSelectedAnnotations(snapshotItem.pageIndex, true).empty())
                {
                    continue;
                }

                bool invertible = false;
                const QTransform deviceToPage = snapshotItem.pageToDeviceMatrix.inverted(&invertible);
                if (!invertible)
                {
                    continue;
                }

                QPointF pageDirection = deviceToPage.map(deviceDirection) - deviceToPage.map(QPointF(0.0, 0.0));
                const qreal length = std::hypot(pageDirection.x(), pageDirection.y());
                if (qFuzzyIsNull(length))
                {
                    continue;
                }

                const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
                pageDirection *= step / length;
                translateSelectedAnnotations(pageDirection);
                event->accept();
                return;
            }
        }
    }

    if (event->key() != Qt::Key_Delete)
    {
        return;
    }

    // Jakub Melka: deleting an annotation modifies the document, so it must be
    // allowed by the security handler. Without this check, a stray press of the
    // Delete key would destroy an annotation even in a document, which the author
    // protected against the modification of the interactive items.
    if (!isModificationAllowed())
    {
        return;
    }

    if (hasSelection())
    {
        deleteSelectedAnnotations();
        event->accept();
        return;
    }

    PDFWidget* pdfWidget = m_proxy->getWidget();
    std::vector<PDFInteger> currentPages = pdfWidget->getDrawWidget()->getCurrentPages();

    if (!hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return;
    }

    // Delete the annotation under the mouse cursor, so the user doesn't have to
    // open the context menu of the annotation just to delete it.
    for (PDFInteger pageIndex : currentPages)
    {
        PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
        for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (!pageAnnotation.isHovered)
            {
                continue;
            }

            if (!canDeleteAnnotation(pageAnnotation))
            {
                continue;
            }

            m_editableAnnotation = pageAnnotation.annotation->getSelfReference();
            m_editableAnnotationPage = getPageReference(pageIndex, pageAnnotation);

            onDeleteAnnotation();
            event->accept();
            return;
        }
    }
}

void PDFWidgetAnnotationManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);

    if (!m_document)
    {
        return;
    }

    const bool isSelectionModifier = event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::ShiftModifier);

    if (event->button() == Qt::LeftButton)
    {
        if (m_interaction.type != Interaction::None)
        {
            event->accept();
            return;
        }

        // 1) Handles of the selection frame
        if (!isSelectionModifier && beginHandleInteraction(event->pos()))
        {
            event->accept();
            return;
        }

        // 2) Selectable annotation under the cursor
        PDFInteger pageIndex = -1;
        if (const PageAnnotation* pageAnnotation = findSelectableAnnotation(event->pos(), &pageIndex))
        {
            const PDFObjectReference annotation = pageAnnotation->annotation->getSelfReference();

            if (isSelectionModifier)
            {
                if (isAnnotationSelected(annotation))
                {
                    deselectAnnotation(annotation);
                }
                else
                {
                    selectAnnotation(annotation, false);
                }
                event->accept();
                return;
            }

            if (!isAnnotationSelected(annotation))
            {
                selectAnnotation(annotation, true);
            }

            // The pointer is still valid - selection does not change the annotation list
            if (canTransformAnnotation(*pageAnnotation))
            {
                beginAnnotationDrag(event, pageIndex);
            }

            event->accept();
            return;
        }

        // 3) Links
        if (getLinkActionAtPosition(event->pos()))
        {
            event->accept();
            return;
        }

        // 4) Empty area of the page - rubber band selection, or clearing of the selection.
        //    Plain click is not accepted, so the widget can scroll the document by mouse drag.
        QPointF pagePoint;
        pageIndex = m_proxy->getPageUnderPoint(event->pos(), &pagePoint);
        if (isSelectionModifier && pageIndex != -1)
        {
            const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
            if (const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex))
            {
                bool invertible = false;
                const QTransform deviceToPage = snapshotItem->pageToDeviceMatrix.inverted(&invertible);
                if (invertible)
                {
                    m_interaction = InteractionState();
                    m_interaction.type = Interaction::RubberBand;
                    m_interaction.pageIndex = pageIndex;
                    m_interaction.pageToDevice = snapshotItem->pageToDeviceMatrix;
                    m_interaction.deviceToPage = deviceToPage;
                    m_interaction.startDevicePosition = event->pos();
                    m_interaction.currentDevicePosition = event->pos();
                    m_interaction.isAdditive = true;
                    event->accept();
                    requestRepaint();
                    return;
                }
            }
        }

        if (!isSelectionModifier && hasSelection())
        {
            clearSelection();
        }
    }

    // Show context menu?
    if (event->button() == Qt::RightButton)
    {
        if (m_interaction.type != Interaction::None)
        {
            cancelInteraction();
            event->accept();
            return;
        }

        PDFInteger pageIndex = -1;
        if (const PageAnnotation* pageAnnotation = findSelectableAnnotation(event->pos(), &pageIndex))
        {
            const PDFObjectReference annotation = pageAnnotation->annotation->getSelfReference();
            if (!isAnnotationSelected(annotation))
            {
                selectAnnotation(annotation, !isSelectionModifier);
            }
        }

        PDFWidget* pdfWidget = m_proxy->getWidget();
        const bool hasAnnotations = hasAnyPageAnnotation(pdfWidget->getDrawWidget()->getCurrentPages());
        if (hasSelection() || canPasteAnnotations() || hasAnnotations)
        {
            event->accept();
            showSelectionMenu(pdfWidget->mapToGlobal(event->pos()), event->pos());
        }
    }
}

void PDFWidgetAnnotationManager::showAnnotationMenu(PDFObjectReference annotationReference,
                                                    PDFObjectReference pageReference,
                                                    QPoint globalMenuPosition)
{
    if (!m_document || !annotationReference.isValid())
    {
        return;
    }

    const size_t pageIndex = m_document->getCatalog()->getPageIndexFromPageReference(pageReference);
    if (pageIndex == PDFCatalog::INVALID_PAGE_INDEX)
    {
        return;
    }

    const PageAnnotation* pageAnnotation = findPageAnnotation(PDFInteger(pageIndex), annotationReference);
    if (!pageAnnotation || !isAnnotationSelectable(*pageAnnotation))
    {
        return;
    }

    m_selection = { SelectedAnnotation{ annotationReference, PDFInteger(pageIndex) } };
    Q_EMIT selectionChanged();
    requestRepaint();

    showSelectionMenu(globalMenuPosition, std::nullopt);
}

void PDFWidgetAnnotationManager::showSelectionMenu(QPoint globalPosition, std::optional<QPoint> widgetPosition)
{
    PDFWidget* pdfWidget = m_proxy->getWidget();
    QMenu menu(tr("Annotation"), pdfWidget);

    const bool hasSingleSelection = m_selection.size() == 1;
    const PageAnnotation* singleAnnotation = hasSingleSelection ? findPageAnnotation(m_selection.front().pageIndex, m_selection.front().annotation) : nullptr;

    m_editableAnnotation = PDFObjectReference();
    m_editableAnnotationPage = PDFObjectReference();
    m_editableAnnotationGlobalPosition = globalPosition;

    if (singleAnnotation)
    {
        m_editableAnnotation = m_selection.front().annotation;
        m_editableAnnotationPage = getPageReference(m_selection.front().pageIndex, *singleAnnotation);

        if (singleAnnotation->annotation->asMarkupAnnotation())
        {
            QAction* showPopupAction = menu.addAction(tr("Show Popup Window"));
            connect(showPopupAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onShowPopupAnnotation);
        }

        QAction* editAction = menu.addAction(tr("Edit..."));
        editAction->setEnabled(isModificationAllowed());
        connect(editAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onEditAnnotation);

        menu.addSeparator();
    }

    bool canDeleteAny = false;
    bool canTransformAny = false;
    for (const SelectedAnnotation& item : m_selection)
    {
        if (const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation))
        {
            canDeleteAny = canDeleteAny || canDeleteAnnotation(*pageAnnotation);
            canTransformAny = canTransformAny || canTransformAnnotation(*pageAnnotation);
        }
    }

    QAction* cutAction = menu.addAction(tr("Cut"));
    cutAction->setShortcut(QKeySequence::Cut);
    cutAction->setEnabled(canDeleteAny);
    connect(cutAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::cutSelectedAnnotations);

    QAction* copyAction = menu.addAction(tr("Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(hasSelection());
    connect(copyAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::copySelectedAnnotations);

    QAction* pasteAction = menu.addAction(tr("Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setEnabled(canPasteAnnotations());
    connect(pasteAction, &QAction::triggered, this, [this, widgetPosition]() { pasteAnnotations(widgetPosition); });

    if (singleAnnotation)
    {
        QAction* copyToPagesAction = menu.addAction(tr("Copy to Multiple Pages..."));
        copyToPagesAction->setEnabled(isModificationAllowed());
        connect(copyToPagesAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onCopyAnnotation);
    }

    menu.addSeparator();

    QAction* rotateClockwiseAction = menu.addAction(tr("Rotate 90° Clockwise"));
    rotateClockwiseAction->setEnabled(canTransformAny);
    connect(rotateClockwiseAction, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(90.0); });

    QAction* rotateCounterclockwiseAction = menu.addAction(tr("Rotate 90° Counterclockwise"));
    rotateCounterclockwiseAction->setEnabled(canTransformAny);
    connect(rotateCounterclockwiseAction, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(-90.0); });

    QAction* rotate180Action = menu.addAction(tr("Rotate 180°"));
    rotate180Action->setEnabled(canTransformAny);
    connect(rotate180Action, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(180.0); });

    QAction* flipHorizontalAction = menu.addAction(tr("Flip Horizontal"));
    flipHorizontalAction->setEnabled(canTransformAny);
    connect(flipHorizontalAction, &QAction::triggered, this, [this]() { flipSelectedAnnotations(Qt::Horizontal); });

    QAction* flipVerticalAction = menu.addAction(tr("Flip Vertical"));
    flipVerticalAction->setEnabled(canTransformAny);
    connect(flipVerticalAction, &QAction::triggered, this, [this]() { flipSelectedAnnotations(Qt::Vertical); });

    menu.addSeparator();

    QAction* selectAllAction = menu.addAction(tr("Select All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setEnabled(hasAnyPageAnnotation(pdfWidget->getDrawWidget()->getCurrentPages()));
    connect(selectAllAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::selectAllAnnotations);

    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setEnabled(canDeleteAny);
    connect(deleteAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::deleteSelectedAnnotations);

    // Shortcuts are displayed only, they are handled by the key press event of the widget
    for (QAction* action : menu.actions())
    {
        action->setShortcutContext(Qt::WidgetShortcut);
    }

    menu.exec(globalPosition);
}

void PDFWidgetAnnotationManager::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_dragState.isActive || m_interaction.type != Interaction::None)
    {
        return;
    }

    updateFromMouseEvent(event);

    PDFWidget* pdfWidget = m_proxy->getWidget();
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();

    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);
        for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (!pageAnnotation.isHovered || pageAnnotation.annotation->isReplyTo())
            {
                continue;
            }

            const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
            if (!markupAnnotation)
            {
                continue;
            }

            QDialog* dialog = createDialogForMarkupAnnotations(pdfWidget, pageAnnotation, pageAnnotations);

            if (const PageAnnotation* popupAnnotation = pageAnnotations.getPopupAnnotation(pageAnnotation))
            {
                QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(popupAnnotation->annotation->getRectangle().bottomLeft()).toPoint();
                popupPoint = pdfWidget->mapToGlobal(popupPoint);
                dialog->move(popupPoint);
            }
            else if (markupAnnotation->getRectangle().isValid())
            {
                QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(markupAnnotation->getRectangle().bottomRight()).toPoint();
                popupPoint = pdfWidget->mapToGlobal(popupPoint);
                dialog->move(popupPoint);
            }

            event->accept();
            dialog->exec();
            return;
        }
    }
}

bool PDFWidgetAnnotationManager::canAcceptAnnotationDrag(const QMimeData* data) const
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    PDFAnnotationDragDataHelper::Payload payload;
    if (PDFAnnotationDragDataHelper::deserialize(data, payload))
    {
        if (payload.sourceId == m_dragSourceId)
        {
            // Annotations of this document
            return true;
        }

        // Annotations of another document must be serialized
        return data->hasFormat(PDFAnnotationManipulator::getMimeType());
    }

    return data && data->hasFormat(PDFAnnotationManipulator::getMimeType());
}

bool PDFWidgetAnnotationManager::handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action)
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    QPointF pagePoint;
    const PDFInteger pageIndex = m_proxy->getPageUnderPoint(widgetPos, &pagePoint);
    if (pageIndex < 0)
    {
        return false;
    }

    const PDFObjectReference targetPageReference = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
    if (!targetPageReference.isValid())
    {
        return false;
    }

    PDFAnnotationDragDataHelper::Payload payload;
    const bool hasPayload = PDFAnnotationDragDataHelper::deserialize(data, payload);
    const bool isInternal = hasPayload && payload.sourceId == m_dragSourceId;

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();
    PDFDocumentBuilder* builder = modifier.getBuilder();

    std::vector<PDFObjectReference> resultAnnotations;

    if (isInternal)
    {
        // Verify, that the annotations still exist in the document
        for (const PDFObjectReference& annotation : payload.annotations)
        {
            if (!PDFAnnotation::parse(&m_document->getStorage(), annotation))
            {
                return false;
            }
        }

        const QPointF newTopLeft = pagePoint - payload.cursorOffset;
        const QPointF delta = newTopLeft - payload.boundingRectangle.topLeft();
        const bool isMoved = !qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y());
        const bool isCopy = action == Qt::CopyAction;

        if (!isCopy && !isMoved && targetPageReference == payload.pageReference)
        {
            // Nothing has changed
            return false;
        }

        const QTransform translation = QTransform::fromTranslate(delta.x(), delta.y());
        for (const PDFObjectReference& annotation : payload.annotations)
        {
            PDFObjectReference targetAnnotation = annotation;
            if (isCopy)
            {
                targetAnnotation = PDFAnnotationManipulator::copyAnnotation(builder, annotation, targetPageReference);
            }
            else if (targetPageReference != payload.pageReference)
            {
                PDFAnnotationManipulator::moveAnnotationToPage(builder, annotation, payload.pageReference, targetPageReference);
            }

            if (!targetAnnotation.isValid())
            {
                continue;
            }

            if (isMoved)
            {
                PDFAnnotationManipulator::transformAnnotation(builder, targetAnnotation, translation);
            }

            resultAnnotations.push_back(targetAnnotation);
        }
    }
    else
    {
        // Annotations from another document (or another instance of the application),
        // they are always copied, the source document is left untouched.
        const PDFAnnotationManipulator::SerializedAnnotations serializedAnnotations = PDFAnnotationManipulator::deserializeAnnotations(data->data(PDFAnnotationManipulator::getMimeType()));
        if (!serializedAnnotations.isValid())
        {
            return false;
        }

        QPointF offset;
        if (hasPayload)
        {
            const QPointF newTopLeft = pagePoint - payload.cursorOffset;
            offset = newTopLeft - serializedAnnotations.boundingRectangle.topLeft();
        }
        else
        {
            offset = pagePoint - serializedAnnotations.boundingRectangle.center();
        }

        resultAnnotations = PDFAnnotationManipulator::insertAnnotations(builder, targetPageReference, serializedAnnotations, offset);
    }

    if (resultAnnotations.empty())
    {
        return false;
    }

    if (modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        setSelectedAnnotations(resultAnnotations);
    }

    return true;
}

PDFSnapInfo PDFWidgetAnnotationManager::getSnapInfo(PDFInteger pageIndex) const
{
    PDFSnapInfo result;

    if (!m_document || pageIndex < 0)
    {
        return result;
    }

    auto addQuadrilaterals = [&result](const PDFAnnotationQuadrilaterals& quadrilaterals)
    {
        for (const PDFAnnotationQuadrilaterals::Quadrilateral& quadrilateral : quadrilaterals.getQuadrilaterals())
        {
            for (size_t i = 0; i < quadrilateral.size(); ++i)
            {
                result.addAnnotationLine(quadrilateral[i], quadrilateral[(i + 1) % quadrilateral.size()]);
            }
        }
    };

    const PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
    for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
    {
        const PDFAnnotation* annotation = pageAnnotation.annotation.data();
        if (!annotation || !PDFAnnotation::isTypeEditable(annotation->getType()))
        {
            continue;
        }

        if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(annotation))
        {
            result.addAnnotationLine(lineAnnotation->getLine().p1(), lineAnnotation->getLine().p2());
            continue;
        }

        if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation))
        {
            const std::vector<QPointF>& vertices = polygonalAnnotation->getVertices();
            if (vertices.size() == 1)
            {
                result.addAnnotationPoint(vertices.front());
            }

            const size_t lineCount = vertices.size() >= 2 ? (annotation->getType() == AnnotationType::Polygon ? vertices.size() : vertices.size() - 1) : 0;
            for (size_t i = 0; i < lineCount; ++i)
            {
                result.addAnnotationLine(vertices[i], vertices[(i + 1) % vertices.size()]);
            }
            continue;
        }

        if (const PDFHighlightAnnotation* highlightAnnotation = dynamic_cast<const PDFHighlightAnnotation*>(annotation))
        {
            addQuadrilaterals(highlightAnnotation->getHiglightArea());
            continue;
        }

        if (const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation))
        {
            addQuadrilaterals(redactAnnotation->getRedactionRegion());
            continue;
        }

        if (const PDFSimpleGeometryAnnotation* geometryAnnotation = dynamic_cast<const PDFSimpleGeometryAnnotation*>(annotation))
        {
            if (geometryAnnotation->getGeometryRectangle().isValid())
            {
                result.addAnnotationRectangle(geometryAnnotation->getGeometryRectangle());
                continue;
            }
        }

        result.addAnnotationRectangle(annotation->getRectangle());
    }

    return result;
}

void PDFWidgetAnnotationManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        if (m_interaction.type != Interaction::None)
        {
            m_interaction.currentDevicePosition = event->pos();
            finishInteraction();
            event->accept();
            return;
        }

        if (m_suppressLinkActivationOnRelease)
        {
            m_suppressLinkActivationOnRelease = false;
            event->accept();
            return;
        }

        if (m_dragState.isActive)
        {
            m_dragState = DragState();
            requestRepaint();
        }

        if (const PDFAction* linkAction = getLinkActionAtPosition(event->pos()))
        {
            Q_EMIT actionTriggered(linkAction);
            event->accept();
            return;
        }
    }
}

void PDFWidgetAnnotationManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);

    if (m_interaction.type != Interaction::None)
    {
        switch (m_interaction.type)
        {
            case Interaction::RubberBand:
                m_interaction.currentDevicePosition = event->pos();
                m_cursor = QCursor(Qt::CrossCursor);
                break;

            case Interaction::Handle:
                updateHandleInteraction(event->pos(), event->modifiers());
                m_cursor = m_interaction.handle == Handle::Rotate ? getRotationCursor() : QCursor(getCursorShapeForHandle(m_interaction.handle));
                break;

            default:
                break;
        }

        event->accept();
        requestRepaint();
        return;
    }

    if (m_dragState.isActive)
    {
        m_dragState.isCopy = event->modifiers().testFlag(Qt::ControlModifier);

        if (!m_dragState.isDragging)
        {
            const int distance = (event->pos() - m_dragState.startDevicePos).manhattanLength();
            if (distance >= QApplication::startDragDistance())
            {
                m_dragState.isDragging = true;
                startAnnotationDrag(event);
                event->accept();
                return;
            }
        }

        if (m_dragState.isDragging)
        {
            m_cursor = QCursor(m_dragState.isCopy ? Qt::DragCopyCursor : Qt::ClosedHandCursor);
        }
    }
}

void PDFWidgetAnnotationManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::updateFromMouseEvent(QMouseEvent* event)
{
    PDFWidget* widget = m_proxy->getWidget();
    std::vector<PDFInteger> currentPages = widget->getDrawWidget()->getCurrentPages();

    m_tooltip = QString();
    m_cursor = std::nullopt;

    const HoveredAnnotation oldHoveredAnnotation = m_hoveredAnnotation;
    const Handle oldHoveredHandle = m_hoveredHandle;
    m_hoveredAnnotation = HoveredAnnotation();
    m_hoveredHandle = Handle::None;

    if (!m_document || !hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return;
    }

    bool appearanceChanged = false;

    // We must update appearance states, and update tooltip
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const bool isDown = event->buttons().testFlag(Qt::LeftButton);
    const PDFAppeareanceStreams::Appearance hoverAppearance = isDown ? PDFAppeareanceStreams::Appearance::Down : PDFAppeareanceStreams::Appearance::Rollover;

    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);
        for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (pageAnnotation.annotation->isReplyTo())
            {
                // Annotation is reply to another annotation, do not interact with it
                continue;
            }

            const PDFAppeareanceStreams::Appearance oldAppearance = pageAnnotation.appearance;
            QRectF annotationRect = pageAnnotation.annotation->getRectangle();
            QTransform matrix = prepareTransformations(snapshotItem.pageToDeviceMatrix, widget, pageAnnotation.annotation->getEffectiveFlags(), m_document->getCatalog()->getPage(snapshotItem.pageIndex), annotationRect);
            QPainterPath path;
            path.addRect(annotationRect);
            path = matrix.map(path);

            if (path.contains(event->pos()))
            {
                pageAnnotation.appearance = hoverAppearance;
                pageAnnotation.isHovered = true;

                // Generate tooltip
                if (m_tooltip.isEmpty())
                {
                    const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
                    if (markupAnnotation)
                    {
                        QString title = markupAnnotation->getWindowTitle();
                        if (title.isEmpty())
                        {
                            title = markupAnnotation->getSubject();
                        }
                        if (title.isEmpty())
                        {
                            title = PDFTranslationContext::tr("Info");
                        }

                        const size_t repliesCount = pageAnnotations.getReplies(pageAnnotation).size();
                        if (repliesCount > 0)
                        {
                            title = PDFTranslationContext::tr("%1 (%2 replies)").arg(title).arg(repliesCount);
                        }

                        m_tooltip = QString("<p><b>%1</b></p><p>%2</p>").arg(title, markupAnnotation->getContents());
                    }
                }

                const AnnotationType annotationType = pageAnnotation.annotation->getType();
                if (annotationType == AnnotationType::Link)
                {
                    const PDFLinkAnnotation* linkAnnotation = dynamic_cast<const PDFLinkAnnotation*>(pageAnnotation.annotation.data());
                    Q_ASSERT(linkAnnotation);

                    // We must check, if user clicked to the link area
                    QPainterPath activationPath = linkAnnotation->getActivationRegion().getPath();
                    activationPath = snapshotItem.pageToDeviceMatrix.map(activationPath);
                    if (activationPath.contains(event->pos()) && linkAnnotation->getAction())
                    {
                        m_cursor = QCursor(Qt::PointingHandCursor);
                    }
                }
                if (annotationType == AnnotationType::Widget)
                {
                    if (m_formManager && m_formManager->hasFormFieldWidgetText(pageAnnotation.annotation->getSelfReference()))
                    {
                        m_cursor = QCursor(Qt::IBeamCursor);
                    }
                    else
                    {
                        m_cursor = QCursor(Qt::ArrowCursor);
                    }
                }

            }
            else
            {
                pageAnnotation.appearance = PDFAppeareanceStreams::Appearance::Normal;
                pageAnnotation.isHovered = false;
            }

            const bool currentAppearanceChanged = oldAppearance != pageAnnotation.appearance;
            if (currentAppearanceChanged)
            {
                // We have changed appearance - we must mark stream as dirty
                pageAnnotation.appearanceStream.dirty();
                appearanceChanged = true;
            }
        }
    }

    // Selection frame handles and the annotation under the cursor
    if (m_interaction.type == Interaction::None && !m_dragState.isActive)
    {
        for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
        {
            const HandleLayout layout = computeHandleLayout(getSelectionBoundingRectangle(snapshotItem.pageIndex, true), snapshotItem.pageToDeviceMatrix);
            const Handle handle = hitTestHandle(layout, event->pos());
            if (handle != Handle::None)
            {
                m_hoveredHandle = handle;
                m_cursor = handle == Handle::Rotate ? getRotationCursor() : QCursor(getCursorShapeForHandle(handle));
                break;
            }
        }

        if (m_hoveredHandle == Handle::None)
        {
            PDFInteger pageIndex = -1;
            if (const PageAnnotation* pageAnnotation = findSelectableAnnotation(event->pos(), &pageIndex))
            {
                m_hoveredAnnotation.pageIndex = pageIndex;
                m_hoveredAnnotation.annotation = pageAnnotation->annotation->getSelfReference();

                if (canTransformAnnotation(*pageAnnotation) && !m_cursor)
                {
                    m_cursor = QCursor(Qt::SizeAllCursor);
                }
            }
        }
    }

    // If appearance has changed, then we must redraw the page
    if (appearanceChanged ||
        oldHoveredHandle != m_hoveredHandle ||
        oldHoveredAnnotation.pageIndex != m_hoveredAnnotation.pageIndex ||
        oldHoveredAnnotation.annotation != m_hoveredAnnotation.annotation)
    {
        requestRepaint();
    }
}

bool PDFWidgetAnnotationManager::beginHandleInteraction(const QPoint& devicePosition)
{
    if (!m_document || !hasSelection())
    {
        return false;
    }

    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        const QRectF boundingRectangle = getSelectionBoundingRectangle(snapshotItem.pageIndex, true);
        const HandleLayout layout = computeHandleLayout(boundingRectangle, snapshotItem.pageToDeviceMatrix);
        const Handle handle = hitTestHandle(layout, devicePosition);
        if (handle == Handle::None)
        {
            continue;
        }

        bool invertible = false;
        const QTransform deviceToPage = snapshotItem.pageToDeviceMatrix.inverted(&invertible);
        if (!invertible)
        {
            return false;
        }

        m_interaction = InteractionState();
        m_interaction.type = Interaction::Handle;
        m_interaction.pageIndex = snapshotItem.pageIndex;
        m_interaction.pageToDevice = snapshotItem.pageToDeviceMatrix;
        m_interaction.deviceToPage = deviceToPage;
        m_interaction.startDevicePosition = devicePosition;
        m_interaction.currentDevicePosition = devicePosition;
        m_interaction.handle = handle;
        m_interaction.layout = layout;
        m_hoveredAnnotation = HoveredAnnotation();
        requestRepaint();
        return true;
    }

    return false;
}

void PDFWidgetAnnotationManager::updateHandleInteraction(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers)
{
    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.previewTransform = computeHandleTransform(devicePosition, modifiers, &m_interaction.previewAngle);
}

QTransform PDFWidgetAnnotationManager::computeHandleTransform(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers, qreal* angle) const
{
    const QRectF& frame = m_interaction.layout.frame;
    QTransform deviceTransform;

    if (angle)
    {
        *angle = 0.0;
    }

    if (m_interaction.handle == Handle::Rotate)
    {
        const QPointF center = frame.center();
        const QPointF startVector = QPointF(m_interaction.startDevicePosition) - center;
        const QPointF currentVector = QPointF(devicePosition) - center;

        if (qFuzzyIsNull(currentVector.x()) && qFuzzyIsNull(currentVector.y()))
        {
            return QTransform();
        }

        qreal degrees = qRadiansToDegrees(std::atan2(currentVector.y(), currentVector.x()) - std::atan2(startVector.y(), startVector.x()));
        while (degrees > 180.0)
        {
            degrees -= 360.0;
        }
        while (degrees <= -180.0)
        {
            degrees += 360.0;
        }

        if (modifiers.testFlag(Qt::ShiftModifier))
        {
            // Snap to the multiples of 15 degrees
            degrees = std::round(degrees / 15.0) * 15.0;
        }

        if (angle)
        {
            *angle = degrees;
        }

        deviceTransform = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(degrees) * QTransform::fromTranslate(center.x(), center.y());
    }
    else
    {
        const qreal minimalSize = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 4);
        const QPointF position(devicePosition);
        QRectF newFrame = frame;

        const bool isLeft = m_interaction.handle == Handle::TopLeft || m_interaction.handle == Handle::Left || m_interaction.handle == Handle::BottomLeft;
        const bool isRight = m_interaction.handle == Handle::TopRight || m_interaction.handle == Handle::Right || m_interaction.handle == Handle::BottomRight;
        const bool isTop = m_interaction.handle == Handle::TopLeft || m_interaction.handle == Handle::Top || m_interaction.handle == Handle::TopRight;
        const bool isBottom = m_interaction.handle == Handle::BottomLeft || m_interaction.handle == Handle::Bottom || m_interaction.handle == Handle::BottomRight;

        if (isLeft)
        {
            newFrame.setLeft(std::min(position.x(), frame.right() - minimalSize));
        }
        if (isRight)
        {
            newFrame.setRight(std::max(position.x(), frame.left() + minimalSize));
        }
        if (isTop)
        {
            newFrame.setTop(std::min(position.y(), frame.bottom() - minimalSize));
        }
        if (isBottom)
        {
            newFrame.setBottom(std::max(position.y(), frame.top() + minimalSize));
        }

        const bool isCorner = (isLeft || isRight) && (isTop || isBottom);
        if (isCorner && modifiers.testFlag(Qt::ShiftModifier) && frame.width() > 0.0 && frame.height() > 0.0)
        {
            // Keep the aspect ratio - the larger relative change wins
            const qreal scale = std::max(newFrame.width() / frame.width(), newFrame.height() / frame.height());
            const qreal width = frame.width() * scale;
            const qreal height = frame.height() * scale;
            const qreal left = isLeft ? frame.right() - width : frame.left();
            const qreal top = isTop ? frame.bottom() - height : frame.top();
            newFrame = QRectF(left, top, width, height);
        }

        const qreal scaleX = frame.width() > 0.0 ? newFrame.width() / frame.width() : 1.0;
        const qreal scaleY = frame.height() > 0.0 ? newFrame.height() / frame.height() : 1.0;
        deviceTransform = QTransform::fromTranslate(-frame.left(), -frame.top()) * QTransform::fromScale(scaleX, scaleY) * QTransform::fromTranslate(newFrame.left(), newFrame.top());
    }

    // The transformation is computed in the device space (where the handles live),
    // so it must be converted to the page space
    return m_interaction.pageToDevice * deviceTransform * m_interaction.deviceToPage;
}

QRectF PDFWidgetAnnotationManager::getRubberBandRectangle() const
{
    if (m_interaction.type != Interaction::RubberBand)
    {
        return QRectF();
    }

    const QPointF start = m_interaction.deviceToPage.map(QPointF(m_interaction.startDevicePosition));
    const QPointF current = m_interaction.deviceToPage.map(QPointF(m_interaction.currentDevicePosition));
    return QRectF(start, current).normalized();
}

void PDFWidgetAnnotationManager::finishInteraction()
{
    const InteractionState interaction = m_interaction;
    m_interaction = InteractionState();
    m_cursor = std::nullopt;

    switch (interaction.type)
    {
        case Interaction::RubberBand:
        {
            // A tiny rectangle is just a click (with the modifier) on an empty area
            const QPoint difference = interaction.currentDevicePosition - interaction.startDevicePosition;
            if (difference.manhattanLength() < QApplication::startDragDistance())
            {
                break;
            }

            const QPointF start = interaction.deviceToPage.map(QPointF(interaction.startDevicePosition));
            const QPointF current = interaction.deviceToPage.map(QPointF(interaction.currentDevicePosition));
            const QRectF rubberBand = QRectF(start, current).normalized();

            std::vector<PDFObjectReference> selection = interaction.isAdditive ? getSelectedAnnotations() : std::vector<PDFObjectReference>();
            const PageAnnotations& pageAnnotations = getPageAnnotations(interaction.pageIndex);
            for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
            {
                if (!isAnnotationSelectable(pageAnnotation))
                {
                    continue;
                }

                const PDFObjectReference annotation = pageAnnotation.annotation->getSelfReference();
                const QRectF rectangle = pageAnnotation.annotation->getRectangle().normalized();
                if (rubberBand.contains(rectangle) && std::find(selection.cbegin(), selection.cend(), annotation) == selection.cend())
                {
                    selection.push_back(annotation);
                }
            }

            setSelectedAnnotations(selection);
            break;
        }

        case Interaction::Handle:
        {
            if (interaction.previewTransform.type() != QTransform::TxNone)
            {
                transformSelectedAnnotations(interaction.pageIndex, interaction.previewTransform);
            }
            break;
        }

        default:
            break;
    }

    requestRepaint();
}

void PDFWidgetAnnotationManager::cancelInteraction()
{
    m_interaction = InteractionState();
    m_cursor = std::nullopt;
    requestRepaint();
}

void PDFWidgetAnnotationManager::transformSelectedAnnotations(PDFInteger pageIndex, const QTransform& transform)
{
    if (!m_document || !isModificationAllowed())
    {
        return;
    }

    const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(pageIndex, true);
    if (annotations.empty())
    {
        return;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    bool isModified = false;
    for (const PageAnnotation* pageAnnotation : annotations)
    {
        isModified = PDFAnnotationManipulator::transformAnnotation(modifier.getBuilder(), pageAnnotation->annotation->getSelfReference(), transform) || isModified;
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void PDFWidgetAnnotationManager::transformSelectedAnnotations(const std::function<QTransform(const QRectF&, const PDFPage*)>& transformFactory)
{
    if (!m_document || !isModificationAllowed())
    {
        return;
    }

    std::vector<PDFInteger> pages;
    for (const SelectedAnnotation& item : m_selection)
    {
        if (std::find(pages.cbegin(), pages.cend(), item.pageIndex) == pages.cend())
        {
            pages.push_back(item.pageIndex);
        }
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    bool isModified = false;
    for (const PDFInteger pageIndex : pages)
    {
        const QRectF boundingRectangle = getSelectionBoundingRectangle(pageIndex, true);
        if (!boundingRectangle.isValid())
        {
            continue;
        }

        const QTransform transform = transformFactory(boundingRectangle, m_document->getCatalog()->getPage(pageIndex));
        if (transform.type() == QTransform::TxNone)
        {
            continue;
        }

        for (const PageAnnotation* pageAnnotation : getSelectedAnnotations(pageIndex, true))
        {
            isModified = PDFAnnotationManipulator::transformAnnotation(modifier.getBuilder(), pageAnnotation->annotation->getSelfReference(), transform) || isModified;
        }
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void PDFWidgetAnnotationManager::translateSelectedAnnotations(const QPointF& offset)
{
    if (qFuzzyIsNull(offset.x()) && qFuzzyIsNull(offset.y()))
    {
        return;
    }

    transformSelectedAnnotations([offset](const QRectF&, const PDFPage*) { return QTransform::fromTranslate(offset.x(), offset.y()); });
}

void PDFWidgetAnnotationManager::rotateSelectedAnnotations(qreal angleDegrees)
{
    // Jakub Melka: the page space has the y axis pointing upwards, so the rotation
    // by a positive angle is counterclockwise there. Displayed rotation of the page
    // does not matter - it is a rotation itself, and rotations commute.
    transformSelectedAnnotations([angleDegrees](const QRectF& boundingRectangle, const PDFPage*)
    {
        const QPointF center = boundingRectangle.center();
        return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-angleDegrees) * QTransform::fromTranslate(center.x(), center.y());
    });
}

void PDFWidgetAnnotationManager::flipSelectedAnnotations(Qt::Orientation orientation)
{
    transformSelectedAnnotations([orientation](const QRectF& boundingRectangle, const PDFPage* page)
    {
        // Orientation is given on the screen. If the page is displayed rotated by
        // 90 or 270 degrees, then the horizontal axis of the screen is the vertical
        // axis of the page.
        bool mirrorX = orientation == Qt::Horizontal;
        if (page && (page->getPageRotation() == PageRotation::Rotate90 || page->getPageRotation() == PageRotation::Rotate270))
        {
            mirrorX = !mirrorX;
        }

        const QPointF center = boundingRectangle.center();
        return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(mirrorX ? -1.0 : 1.0, mirrorX ? 1.0 : -1.0) * QTransform::fromTranslate(center.x(), center.y());
    });
}

void PDFWidgetAnnotationManager::copySelectedAnnotations()
{
    if (!m_document || !hasSelection())
    {
        return;
    }

    const std::vector<PDFObjectReference> annotations = getSelectedAnnotations();
    const QByteArray data = PDFAnnotationManipulator::serializeAnnotations(m_document, annotations);
    if (data.isEmpty())
    {
        return;
    }

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(PDFAnnotationManipulator::getMimeType(), data);

    // Texts of the annotations are offered to the other applications
    QStringList texts;
    for (const SelectedAnnotation& item : m_selection)
    {
        const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
        if (!pageAnnotation)
        {
            continue;
        }

        const QString contents = pageAnnotation->annotation->getContents().trimmed();
        if (!contents.isEmpty())
        {
            texts << contents;
        }
    }
    if (!texts.isEmpty())
    {
        mimeData->setText(texts.join(QString("\n\n")));
    }

    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
}

void PDFWidgetAnnotationManager::cutSelectedAnnotations()
{
    if (!m_document || !hasSelection() || !isModificationAllowed())
    {
        return;
    }

    copySelectedAnnotations();
    deleteSelectedAnnotations();
}

bool PDFWidgetAnnotationManager::canPasteAnnotations() const
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    return mimeData && mimeData->hasFormat(PDFAnnotationManipulator::getMimeType());
}

void PDFWidgetAnnotationManager::pasteAnnotations(std::optional<QPoint> widgetPosition)
{
    if (!canPasteAnnotations())
    {
        return;
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    const PDFAnnotationManipulator::SerializedAnnotations serializedAnnotations = PDFAnnotationManipulator::deserializeAnnotations(mimeData->data(PDFAnnotationManipulator::getMimeType()));
    if (!serializedAnnotations.isValid())
    {
        return;
    }

    // Target page and position
    PDFInteger pageIndex = -1;
    QPointF pagePoint;
    bool isPositionValid = false;
    if (widgetPosition)
    {
        pageIndex = m_proxy->getPageUnderPoint(*widgetPosition, &pagePoint);
        isPositionValid = pageIndex != -1;
    }

    if (pageIndex == -1)
    {
        const std::vector<PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();
        if (currentPages.empty())
        {
            return;
        }
        pageIndex = currentPages.front();
    }

    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    if (!page || !page->getPageReference().isValid())
    {
        return;
    }

    const QRectF boundingRectangle = serializedAnnotations.boundingRectangle;
    QPointF offset;
    if (isPositionValid)
    {
        offset = pagePoint - boundingRectangle.center();
    }

    // Keep the annotations inside the page
    const QRectF mediaBox = page->getMediaBox().normalized();
    if (mediaBox.isValid() && boundingRectangle.isValid())
    {
        QRectF placedRectangle = boundingRectangle.translated(offset);
        if (placedRectangle.right() > mediaBox.right())
        {
            offset.rx() -= placedRectangle.right() - mediaBox.right();
            placedRectangle = boundingRectangle.translated(offset);
        }
        if (placedRectangle.left() < mediaBox.left())
        {
            offset.rx() += mediaBox.left() - placedRectangle.left();
            placedRectangle = boundingRectangle.translated(offset);
        }
        if (placedRectangle.bottom() > mediaBox.bottom())
        {
            offset.ry() -= placedRectangle.bottom() - mediaBox.bottom();
            placedRectangle = boundingRectangle.translated(offset);
        }
        if (placedRectangle.top() < mediaBox.top())
        {
            offset.ry() += mediaBox.top() - placedRectangle.top();
        }
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();
    const std::vector<PDFObjectReference> insertedAnnotations = PDFAnnotationManipulator::insertAnnotations(modifier.getBuilder(), page->getPageReference(), serializedAnnotations, offset);

    if (!insertedAnnotations.empty() && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        setSelectedAnnotations(insertedAnnotations);
    }
}

void PDFWidgetAnnotationManager::deleteSelectedAnnotations()
{
    if (!m_document || !hasSelection() || !isModificationAllowed())
    {
        return;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    bool isModified = false;
    for (const SelectedAnnotation& item : m_selection)
    {
        const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
        if (!pageAnnotation || !canDeleteAnnotation(*pageAnnotation))
        {
            continue;
        }

        modifier.getBuilder()->removeAnnotation(getPageReference(item.pageIndex, *pageAnnotation), item.annotation);
        isModified = true;
    }

    if (isModified && modifier.finalize())
    {
        clearSelection();
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

bool PDFWidgetAnnotationManager::beginAnnotationDrag(QMouseEvent* event, PDFInteger pageIndex)
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    m_dragState = DragState();

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex);
    if (!snapshotItem)
    {
        return false;
    }

    bool invertible = false;
    const QTransform deviceToPage = snapshotItem->pageToDeviceMatrix.inverted(&invertible);
    if (!invertible)
    {
        return false;
    }

    // All selected annotations of the page, which can be moved, are dragged together
    const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(pageIndex, true);
    if (annotations.empty())
    {
        return false;
    }

    m_dragState.isCopy = event->modifiers().testFlag(Qt::ControlModifier);
    m_dragState.startDevicePos = event->pos();
    m_dragState.pageIndex = pageIndex;
    m_dragState.pageToDevice = snapshotItem->pageToDeviceMatrix;
    m_dragState.deviceToPage = deviceToPage;
    m_dragState.pageReference = getPageReference(pageIndex, *annotations.front());

    for (const PageAnnotation* pageAnnotation : annotations)
    {
        const QRectF rectangle = pageAnnotation->annotation->getRectangle().normalized();
        if (!rectangle.isValid())
        {
            continue;
        }

        m_dragState.annotations.push_back(pageAnnotation->annotation->getSelfReference());
        m_dragState.rectangles.push_back(rectangle);
        m_dragState.boundingRectangle = m_dragState.boundingRectangle.united(rectangle);
    }

    if (m_dragState.annotations.empty() || !m_dragState.pageReference.isValid())
    {
        m_dragState = DragState();
        return false;
    }

    const QPointF pagePoint = deviceToPage.map(QPointF(event->pos()));
    m_dragState.cursorOffset = pagePoint - m_dragState.boundingRectangle.topLeft();
    m_dragState.dragPixmap = createAnnotationDragPixmap(snapshotItem->pageToDeviceMatrix, m_dragState.dragHotSpot);
    m_dragState.isActive = true;
    return true;
}

void PDFWidgetAnnotationManager::startAnnotationDrag(QMouseEvent* event)
{
    if (!m_dragState.isActive || !event)
    {
        return;
    }

    PDFAnnotationDragDataHelper::Payload payload;
    payload.sourceId = m_dragSourceId;
    payload.pageReference = m_dragState.pageReference;
    payload.annotations = m_dragState.annotations;
    payload.rectangles = m_dragState.rectangles;
    payload.boundingRectangle = m_dragState.boundingRectangle;
    payload.cursorOffset = m_dragState.cursorOffset;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(PDFAnnotationDragDataHelper::getMimeType(), PDFAnnotationDragDataHelper::serialize(payload));

    // Serialized annotations allow the drop into another document
    const QByteArray serializedAnnotations = PDFAnnotationManipulator::serializeAnnotations(m_document, m_dragState.annotations);
    if (!serializedAnnotations.isEmpty())
    {
        mimeData->setData(PDFAnnotationManipulator::getMimeType(), serializedAnnotations);
    }

    QDrag* drag = new QDrag(m_proxy->getWidget()->getDrawWidget()->getWidget());
    drag->setMimeData(mimeData);
    if (!m_dragState.dragPixmap.isNull())
    {
        drag->setPixmap(m_dragState.dragPixmap);
        drag->setHotSpot(m_dragState.dragHotSpot);
    }

    const Qt::DropAction defaultAction = m_dragState.isCopy ? Qt::CopyAction : Qt::MoveAction;
    m_suppressLinkActivationOnRelease = true;
    drag->exec(Qt::CopyAction | Qt::MoveAction, defaultAction);

    m_dragState = DragState();
    m_cursor = std::nullopt;
    requestRepaint();
}

QPixmap PDFWidgetAnnotationManager::createAnnotationDragPixmap(const QTransform& pagePointToDevicePointMatrix, QPoint& hotSpot) const
{
    hotSpot = QPoint();

    QWidget* deviceWidget = m_proxy->getWidget()->getDrawWidget()->getWidget();
    if (!deviceWidget)
    {
        return QPixmap();
    }

    const QRectF boundingRectangle = m_dragState.boundingRectangle;
    QRectF deviceRectF = pagePointToDevicePointMatrix.mapRect(boundingRectangle).normalized();
    if (deviceRectF.isEmpty())
    {
        return QPixmap();
    }

    QRect deviceRect = deviceRectF.toAlignedRect();
    if (deviceRect.isEmpty())
    {
        return QPixmap();
    }
    const int minSize = 12;
    if (deviceRect.width() < minSize || deviceRect.height() < minSize)
    {
        const QPoint center = deviceRect.center();
        const int width = std::max(deviceRect.width(), minSize);
        const int height = std::max(deviceRect.height(), minSize);
        deviceRect = QRect(center.x() - width / 2, center.y() - height / 2, width, height);
    }

    const qreal dpr = deviceWidget->devicePixelRatioF();
    const QSize imageSize = (QSizeF(deviceRect.size()) * dpr).toSize();
    if (imageSize.isEmpty())
    {
        return QPixmap();
    }

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    const int dpmX = qRound(deviceWidget->logicalDpiX() / 0.0254);
    const int dpmY = qRound(deviceWidget->logicalDpiY() / 0.0254);
    if (dpmX > 0 && dpmY > 0)
    {
        image.setDotsPerMeterX(dpmX);
        image.setDotsPerMeterY(dpmY);
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(-deviceRect.topLeft());

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(90, 160, 255, 60));
    painter.drawRect(deviceRectF);

    // Rectangles of the dragged annotations
    painter.setPen(QPen(QColor(40, 110, 220, 180), 1.0));
    painter.setBrush(QColor(90, 160, 255, 60));
    for (const QRectF& rectangle : m_dragState.rectangles)
    {
        painter.drawRect(pagePointToDevicePointMatrix.mapRect(rectangle).normalized());
    }

    painter.setBrush(Qt::NoBrush);
    painter.drawRect(deviceRectF);
    painter.end();

    const QPointF cursorPoint = boundingRectangle.topLeft() + m_dragState.cursorOffset;
    const QPointF cursorDevicePoint = pagePointToDevicePointMatrix.map(cursorPoint);
    QPoint computedHotSpot = cursorDevicePoint.toPoint() - deviceRect.topLeft();
    if (computedHotSpot.x() < 0 || computedHotSpot.y() < 0 ||
        computedHotSpot.x() >= deviceRect.width() || computedHotSpot.y() >= deviceRect.height())
    {
        computedHotSpot = QPoint(deviceRect.width() / 2, deviceRect.height() / 2);
    }
    hotSpot = computedHotSpot;

    return QPixmap::fromImage(image);
}

const PDFAction* PDFWidgetAnnotationManager::getLinkActionAtPosition(QPoint widgetPos) const
{
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        const PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);
        for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (!pageAnnotation.isHovered)
            {
                continue;
            }

            if (pageAnnotation.annotation->getType() != AnnotationType::Link)
            {
                continue;
            }

            const PDFLinkAnnotation* linkAnnotation = dynamic_cast<const PDFLinkAnnotation*>(pageAnnotation.annotation.data());
            if (!linkAnnotation || !linkAnnotation->getAction())
            {
                continue;
            }

            QPainterPath activationPath = linkAnnotation->getActivationRegion().getPath();
            activationPath = snapshotItem.pageToDeviceMatrix.map(activationPath);
            if (activationPath.contains(widgetPos))
            {
                return linkAnnotation->getAction();
            }
        }
    }

    return nullptr;
}

void PDFWidgetAnnotationManager::onShowPopupAnnotation()
{
    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);
        for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (pageAnnotation.annotation->isReplyTo())
            {
                // Annotation is reply to another annotation, do not interact with it
                continue;
            }

            if (pageAnnotation.annotation->getSelfReference() == m_editableAnnotation)
            {
                QDialog* dialog = createDialogForMarkupAnnotations(m_proxy->getWidget(), pageAnnotation, pageAnnotations);
                dialog->move(m_editableAnnotationGlobalPosition);
                dialog->exec();
                return;
            }
        }
    }
}

void PDFWidgetAnnotationManager::onCopyAnnotation()
{
    pdf::PDFSelectPagesDialog dialog(tr("Copy Annotation"), tr("Copy Annotation onto Multiple Pages"),
                                     m_document->getCatalog()->getPageCount(), m_proxy->getWidget()->getDrawWidget()->getCurrentPages(), m_proxy->getWidget());
    if (dialog.exec() == QDialog::Accepted)
    {
        std::vector<PDFInteger> pages = dialog.getSelectedPages();
        const PDFInteger currentPageIndex = m_document->getCatalog()->getPageIndexFromPageReference(m_editableAnnotationPage);

        for (PDFInteger& pageIndex : pages)
        {
            --pageIndex;
        }

        auto it = std::find(pages.begin(), pages.end(), currentPageIndex);
        if (it != pages.end())
        {
            pages.erase(it);
        }

        if (pages.empty())
        {
            return;
        }

        PDFDocumentModifier modifier(m_document);
        modifier.markAnnotationsChanged();

        for (const PDFInteger pageIndex : pages)
        {
            modifier.getBuilder()->copyAnnotation(m_document->getCatalog()->getPage(pageIndex)->getPageReference(), m_editableAnnotation);
        }

        if (modifier.finalize())
        {
            Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void PDFWidgetAnnotationManager::onEditAnnotation()
{
    PDFEditObjectDialog dialog(EditObjectType::Annotation, m_proxy->getWidget());

    PDFObject originalObject = m_document->getObjectByReference(m_editableAnnotation);
    dialog.setObject(originalObject);

    if (dialog.exec() == PDFEditObjectDialog::Accepted)
    {
        PDFObject object = dialog.getObject();
        if (object != originalObject)
        {
            PDFDocumentModifier modifier(m_document);
            modifier.markAnnotationsChanged();
            modifier.getBuilder()->setObject(m_editableAnnotation, object);
            modifier.getBuilder()->updateAnnotationAppearanceStreams(m_editableAnnotation);

            if (modifier.finalize())
            {
                Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
            }
        }
    }
}

void PDFWidgetAnnotationManager::onDeleteAnnotation()
{
    if (m_editableAnnotation.isValid())
    {
        PDFDocumentModifier modifier(m_document);
        modifier.markAnnotationsChanged();
        modifier.getBuilder()->removeAnnotation(m_editableAnnotationPage, m_editableAnnotation);

        if (modifier.finalize())
        {
            Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

QDialog* PDFWidgetAnnotationManager::createDialogForMarkupAnnotations(PDFWidget* widget,
                                                                      const PageAnnotation& pageAnnotation,
                                                                      const PageAnnotations& pageAnnotations)
{
    QDialog* dialog = new QDialog(widget->getDrawWidget()->getWidget(), Qt::Popup);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    createWidgetsForMarkupAnnotations(dialog, pageAnnotation, pageAnnotations);
    return dialog;
}

void PDFWidgetAnnotationManager::createWidgetsForMarkupAnnotations(QWidget* parentWidget,
                                                                   const PageAnnotation& pageAnnotation,
                                                                   const PageAnnotations& pageAnnotations)
{
    std::vector<const PageAnnotation*> replies = pageAnnotations.getReplies(pageAnnotation);
    replies.insert(replies.begin(), &pageAnnotation);

    QScrollArea* scrollArea = new QScrollArea(parentWidget);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QVBoxLayout* layout = new QVBoxLayout(parentWidget);
    layout->addWidget(scrollArea);
    layout->setContentsMargins(QMargins());

    QWidget* frameWidget = new QWidget(scrollArea);
    QVBoxLayout* frameLayout = new QVBoxLayout(frameWidget);
    frameLayout->setSpacing(0);
    scrollArea->setWidget(frameWidget);

    const PDFMarkupAnnotation* markupMainAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
    QColor color = markupMainAnnotation->getDrawColorFromAnnotationColor(markupMainAnnotation->getColor(), 1.0);
    QColor titleColor = QColor::fromHslF(color.hueF(), color.saturationF(), 0.2f, 1.0f);
    QColor backgroundColor = QColor::fromHslF(color.hueF(), color.saturationF(), 0.9f, 1.0f);

    QString style = "QGroupBox { "
                    "border: 2px solid black; "
                    "border-color: rgb(%4, %5, %6); "
                    "margin-top: 3ex; "
                    "background-color: rgb(%1, %2, %3); "
                    "}"
                    "QGroupBox::title { "
                    "subcontrol-origin: margin; "
                    "subcontrol-position: top center; "
                    "padding: 0px 8192px; "
                    "background-color: rgb(%4, %5, %6); "
                    "color: #FFFFFF;"
                    "}";
    style = style.arg(backgroundColor.red()).arg(backgroundColor.green()).arg(backgroundColor.blue()).arg(titleColor.red()).arg(titleColor.green()).arg(titleColor.blue());

    for (const PageAnnotation* annotation : replies)
    {
        const PDFMarkupAnnotation* markupAnnotation = annotation->annotation->asMarkupAnnotation();

        if (!markupAnnotation)
        {
            // This should not happen...
            continue;
        }

        QGroupBox* groupBox = new QGroupBox(scrollArea);
        frameLayout->addWidget(groupBox);

        QString title = markupAnnotation->getWindowTitle();
        if (title.isEmpty())
        {
            title = markupAnnotation->getSubject();
        }

        QString dateTimeString = QLocale::system().toString(markupAnnotation->getCreationDate().toLocalTime(), QLocale::LongFormat);
        title = QString("%1 (%2)").arg(title, dateTimeString).trimmed();

        groupBox->setStyleSheet(style);
        groupBox->setTitle(title);
        QVBoxLayout* groupBoxLayout = new QVBoxLayout(groupBox);

        QLabel* label = new QLabel(groupBox);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        label->setWordWrap(true);
        label->setText(markupAnnotation->getContents());
        label->setFixedWidth(PDFWidgetUtils::scaleDPI_x(label, 250));
        label->setMinimumHeight(label->sizeHint().height());
        groupBoxLayout->addWidget(label);
    }

    frameWidget->setFixedSize(frameWidget->minimumSizeHint());
    parentWidget->setFixedSize(scrollArea->sizeHint());
}

void PDFWidgetAnnotationManager::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);
}

void PDFWidgetAnnotationManager::drawPostRendering(QPainter* painter, QRect rect) const
{
    Q_UNUSED(rect);

    if (!m_document || m_proxy->getFeatures().testFlag(PDFRenderer::DenyExtraGraphics))
    {
        return;
    }

    // Jakub Melka: the same color convertor as the one used for the pages, so the
    // markers are adjusted in the dark mode the same way as the other tools do it
    PDFCMSPointer cms = m_proxy->getCMSManager()->getCurrentCMS();
    PDFColorConvertor convertor = cms->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(m_proxy->getFeatures(), convertor);

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        drawSelection(painter, snapshotItem.pageIndex, snapshotItem.pageToDeviceMatrix, convertor);
    }
}

void PDFWidgetAnnotationManager::drawSelection(QPainter* painter,
                                               PDFInteger pageIndex,
                                               const QTransform& pagePointToDevicePointMatrix,
                                               const PDFColorConvertor& convertor) const
{
    const std::vector<const PageAnnotation*> selectedAnnotations = getSelectedAnnotations(pageIndex, false);
    const bool hasRubberBand = m_interaction.type == Interaction::RubberBand && m_interaction.pageIndex == pageIndex;
    const bool hasHandlePreview = m_interaction.type == Interaction::Handle && m_interaction.pageIndex == pageIndex && m_interaction.previewTransform.type() != QTransform::TxNone;
    const bool isHoveredOnPage = m_hoveredAnnotation.pageIndex == pageIndex && m_hoveredAnnotation.annotation.isValid() && !isAnnotationSelected(m_hoveredAnnotation.annotation);

    if (selectedAnnotations.empty() && !hasRubberBand && !isHoveredOnPage)
    {
        return;
    }

    // Jakub Melka: these are UI markers, so the colors are adjusted for the dark
    // mode always (the same way as the markers of the other tools).
    const QColor selectionColor = convertor.convert(QColor(0, 120, 215), false, true);
    QColor fillColor = selectionColor;
    fillColor.setAlphaF(0.12f);

    QPen solidPen(selectionColor, 1.0);
    solidPen.setCosmetic(true);

    QPen dashedPen(selectionColor, 1.0);
    dashedPen.setCosmetic(true);
    dashedPen.setStyle(Qt::DashLine);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Annotation under the cursor, which can be selected
    if (isHoveredOnPage && m_interaction.type == Interaction::None && !m_dragState.isActive)
    {
        if (const PageAnnotation* pageAnnotation = findPageAnnotation(pageIndex, m_hoveredAnnotation.annotation))
        {
            painter->setPen(dashedPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(pagePointToDevicePointMatrix.mapRect(pageAnnotation->annotation->getRectangle().normalized()));
        }
    }

    // Selected annotations
    painter->setPen(solidPen);
    painter->setBrush(QBrush(fillColor));
    for (const PageAnnotation* pageAnnotation : selectedAnnotations)
    {
        const QRectF rectangle = pageAnnotation->annotation->getRectangle().normalized();
        if (!rectangle.isValid())
        {
            continue;
        }

        if (hasHandlePreview && canTransformAnnotation(*pageAnnotation))
        {
            const QPolygonF outline = PDFAnnotationManipulator::getTransformedOutline(pageAnnotation->annotation->getType(), rectangle, m_interaction.previewTransform);
            painter->drawPolygon(pagePointToDevicePointMatrix.map(outline));
        }
        else
        {
            painter->drawRect(pagePointToDevicePointMatrix.mapRect(rectangle));
        }
    }

    // Selection frame with the handles
    const QRectF boundingRectangle = getSelectionBoundingRectangle(pageIndex, true);
    if (boundingRectangle.isValid())
    {
        if (hasHandlePreview)
        {
            painter->setPen(dashedPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(pagePointToDevicePointMatrix.map(m_interaction.previewTransform.map(QPolygonF(boundingRectangle))));

            if (m_interaction.handle == Handle::Rotate)
            {
                const QString text = QString::fromUtf8("%1°").arg(QLocale::system().toString(m_interaction.previewAngle, 'f', 1));
                const QPointF textPosition = QPointF(m_interaction.currentDevicePosition) + QPointF(16.0, 16.0);
                const QRectF textRectangle = painter->fontMetrics().boundingRect(text).adjusted(-4.0, -2.0, 4.0, 2.0).translated(textPosition.x() + 4.0, textPosition.y());

                painter->setPen(Qt::NoPen);
                painter->setBrush(convertor.convert(QColor(255, 255, 255, 220), false, false));
                painter->drawRoundedRect(textRectangle, 3.0, 3.0);
                painter->setPen(convertor.convert(QColor(Qt::black), false, true));
                painter->drawText(textRectangle, Qt::AlignCenter, text);
            }
        }
        else if (m_interaction.type == Interaction::None && !m_dragState.isDragging)
        {
            const HandleLayout layout = computeHandleLayout(boundingRectangle, pagePointToDevicePointMatrix);
            if (layout.isValid)
            {
                painter->setPen(dashedPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(layout.frame);
                painter->drawLine(layout.rotationHandleBase, layout.rotationHandle);

                painter->setPen(solidPen);
                painter->setBrush(convertor.convert(QColor(Qt::white), false, false));

                const qreal halfSize = layout.handleSize * 0.5;
                for (const QPointF& handle : layout.handles)
                {
                    painter->drawRect(QRectF(handle.x() - halfSize, handle.y() - halfSize, layout.handleSize, layout.handleSize));
                }

                painter->drawEllipse(layout.rotationHandle, layout.rotationHandleRadius, layout.rotationHandleRadius);
            }
        }
    }

    // Rubber band
    if (hasRubberBand)
    {
        const QRectF rubberBand = getRubberBandRectangle();
        if (rubberBand.isValid())
        {
            painter->setPen(dashedPen);
            painter->setBrush(QBrush(fillColor));
            painter->drawRect(pagePointToDevicePointMatrix.mapRect(rubberBand));
        }
    }

    painter->restore();
}

}   // namespace pdf
