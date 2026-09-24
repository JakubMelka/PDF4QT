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
#include "pdfannotationgeometrydialog.h"
#include "pdfdocumentbuilder.h"
#include "pdfannotationmanipulator.h"
#include "pdfcms.h"
#include "pdfrenderer.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"

#include <algorithm>
#include <cmath>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
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
#include <QColorDialog>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
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
        bool isTextBoxOnly = false;
    };

    /// Mime type of the drag payload
    static const char* getMimeType() { return "application/x-pdf4qt-annotation"; }

    /// Serializes the payload
    static QByteArray serialize(const Payload& payload);

    /// Deserializes the payload, returns false if the mime data does not contain a valid payload
    static bool deserialize(const QMimeData* data, Payload& payload);

private:
    static constexpr quint32 MAGIC = 0x5044414E; // "PDAN"
    static constexpr quint32 VERSION = 3;
};

QByteArray PDFAnnotationDragDataHelper::serialize(const Payload& payload)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << MAGIC << VERSION;
    stream << payload.sourceId;
    stream << qint32(payload.pageReference.objectNumber) << qint32(payload.pageReference.generation);
    stream << payload.boundingRectangle << payload.cursorOffset << payload.isTextBoxOnly;
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
    stream >> payload.boundingRectangle >> payload.cursorOffset >> payload.isTextBoxOnly;
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

    m_snapper.setSnapPointPixelSize(PDFWidgetUtils::scaleDPI_x(proxy->getWidget(), 10));
    m_snapper.setSnapPointTolerance(m_snapper.getSnapPointPixelSize());
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
        m_hoveredPoint = -1;
        m_interaction = InteractionState();
        m_dragState = DragState();
        m_isLinkPressed = false;
        m_pendingDeselection = PDFObjectReference();
        m_snapper.clear();

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

    // Alt + Left/Right selects the point of the selected annotation, which is edited from the keyboard
    const bool isPointSelectionKey = event->modifiers() == Qt::AltModifier && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right);
    if (isPointSelectionKey && getPointEditInfo().isValid())
    {
        event->accept();
        return;
    }

    // The shortcut is claimed only if there is something to select, otherwise
    // it would be taken away from the selection of the text for nothing
    if (event->matches(QKeySequence::SelectAll) && hasSelectableAnnotation())
    {
        event->accept();
        return;
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

bool PDFWidgetAnnotationManager::canModifyAnnotation(const PageAnnotation& annotation) const
{
    if (!m_document || !annotation.annotation || !isModificationAllowed())
    {
        return false;
    }

    const PDFAnnotation::Flags flags = annotation.annotation->getEffectiveFlags();
    return PDFAnnotation::isTypeEditable(annotation.annotation->getType()) &&
           !flags.testFlag(PDFAnnotation::Locked) && !flags.testFlag(PDFAnnotation::ReadOnly);
}

PDFAnnotationManipulator::Capabilities PDFWidgetAnnotationManager::getAnnotationCapabilities(const PageAnnotation& annotation) const
{
    if (!canTransformAnnotation(annotation))
    {
        return PDFAnnotationManipulator::NoCapability;
    }

    return PDFAnnotationManipulator::getCapabilities(annotation.annotation.data());
}

PDFAnnotationManipulator::Capabilities PDFWidgetAnnotationManager::getSelectionCapabilities(PDFInteger pageIndex) const
{
    PDFAnnotationManipulator::Capabilities capabilities = PDFAnnotationManipulator::NoCapability;

    const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(pageIndex, true);
    for (const PageAnnotation* pageAnnotation : annotations)
    {
        capabilities |= getAnnotationCapabilities(*pageAnnotation);
    }

    if (annotations.size() > 1)
    {
        // Jakub Melka: a group of annotations has a layout, which can always be
        // transformed. Annotations, which do not support the transformation, are
        // moved to the transformed position (and the preview shows it truthfully).
        capabilities |= PDFAnnotationManipulator::Resize;
        capabilities |= PDFAnnotationManipulator::RotateRightAngle;
        capabilities |= PDFAnnotationManipulator::RotateArbitrary;
        capabilities |= PDFAnnotationManipulator::Mirror;
    }

    return capabilities;
}

PDFAnnotationManipulator::Capabilities PDFWidgetAnnotationManager::getSelectionCapabilities() const
{
    PDFAnnotationManipulator::Capabilities capabilities = PDFAnnotationManipulator::NoCapability;

    std::vector<PDFInteger> pages;
    for (const SelectedAnnotation& item : m_selection)
    {
        if (std::find(pages.cbegin(), pages.cend(), item.pageIndex) == pages.cend())
        {
            pages.push_back(item.pageIndex);
            capabilities |= getSelectionCapabilities(item.pageIndex);
        }
    }

    return capabilities;
}

PDFWidgetAnnotationManager::TextBoxInfo PDFWidgetAnnotationManager::getTextBoxInfo() const
{
    TextBoxInfo info;

    if (!m_document || m_selection.size() != 1)
    {
        return info;
    }

    const SelectedAnnotation& item = m_selection.front();
    const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return info;
    }

    const PDFFreeTextAnnotation* freeTextAnnotation = dynamic_cast<const PDFFreeTextAnnotation*>(pageAnnotation->annotation.data());
    if (!freeTextAnnotation || freeTextAnnotation->getCalloutLine().getType() == PDFAnnotationCalloutLine::Type::Invalid)
    {
        return info;
    }

    const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(&m_document->getStorage(), item.annotation);
    if (textRectangle.isValid())
    {
        info.pageIndex = item.pageIndex;
        info.annotation = item.annotation;
        info.textRectangle = textRectangle;
    }

    return info;
}

int PDFWidgetAnnotationManager::hitTestPart(const QPoint& devicePosition, PDFObjectReference* annotation) const
{
    if (!m_document || m_selection.size() != 1)
    {
        return -1;
    }

    const SelectedAnnotation& item = m_selection.front();
    const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(item.pageIndex);
    if (!pageAnnotation || !snapshotItem || !canTransformAnnotation(*pageAnnotation))
    {
        return -1;
    }

    const PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(&m_document->getStorage(), item.annotation);
    if (!parts.canRemovePart())
    {
        return -1;
    }

    const QTransform matrix = getAnnotationToDeviceMatrix(*pageAnnotation, item.pageIndex, snapshotItem->pageToDeviceMatrix);
    const qreal tolerance = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 6);

    for (size_t i = 0; i < parts.shapes.size(); ++i)
    {
        const QPolygonF shape = matrix.map(parts.shapes[i]);

        bool isHit = false;
        if (parts.isFilled)
        {
            isHit = shape.containsPoint(QPointF(devicePosition), Qt::OddEvenFill);
        }
        else if (!shape.isEmpty())
        {
            QPainterPath path;
            path.addPolygon(shape);

            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * tolerance);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            isHit = stroker.createStroke(path).contains(QPointF(devicePosition));
        }

        if (isHit)
        {
            if (annotation)
            {
                *annotation = item.annotation;
            }
            return int(i);
        }
    }

    return -1;
}

void PDFWidgetAnnotationManager::modifySelectedAnnotations(const std::function<bool(PDFDocumentBuilder*, const PageAnnotation&)>& function)
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
        if (pageAnnotation && canModifyAnnotation(*pageAnnotation))
        {
            isModified = function(modifier.getBuilder(), *pageAnnotation) || isModified;
        }
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void PDFWidgetAnnotationManager::setSelectedAnnotationsColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    modifySelectedAnnotations([&color](PDFDocumentBuilder* builder, const PageAnnotation& pageAnnotation)
    {
        const PDFObjectReference annotation = pageAnnotation.annotation->getSelfReference();

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("C");
        factory << WrapAnnotationColor(color);
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
        builder->updateAnnotationAppearanceStreams(annotation);
        return true;
    });
}

void PDFWidgetAnnotationManager::setSelectedAnnotationsOpacity(PDFReal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);

    modifySelectedAnnotations([opacity](PDFDocumentBuilder* builder, const PageAnnotation& pageAnnotation)
    {
        const PDFObjectReference annotation = pageAnnotation.annotation->getSelfReference();

        // Opacity of the strokes and of the fills
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("CA");
        factory << opacity;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("ca");
        factory << opacity;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
        builder->updateAnnotationAppearanceStreams(annotation);
        return true;
    });
}

void PDFWidgetAnnotationManager::setSelectedAnnotationsBorderWidth(PDFReal width)
{
    width = qMax(0.0, width);

    modifySelectedAnnotations([width](PDFDocumentBuilder* builder, const PageAnnotation& pageAnnotation)
    {
        // Only the annotations, which are drawn by a line
        switch (pageAnnotation.annotation->getType())
        {
            case AnnotationType::Line:
            case AnnotationType::Square:
            case AnnotationType::Circle:
            case AnnotationType::Polygon:
            case AnnotationType::Polyline:
            case AnnotationType::Ink:
            case AnnotationType::FreeText:
                break;

            default:
                return false;
        }

        const PDFObjectReference annotation = pageAnnotation.annotation->getSelfReference();

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("BS");
        factory.beginDictionary();
        factory.beginDictionaryItem("W");
        factory << width;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
        builder->updateAnnotationAppearanceStreams(annotation);
        return true;
    });
}

bool PDFWidgetAnnotationManager::setAnnotationRectangle(PDFObjectReference annotation, const QRectF& rectangle)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    const QRectF newRectangle = rectangle.normalized();
    if (!pageAnnotation || !newRectangle.isValid())
    {
        return false;
    }

    const PDFAnnotationManipulator::Capabilities capabilities = getAnnotationCapabilities(*pageAnnotation);
    const QRectF oldRectangle = pageAnnotation->annotation->getRectangle().normalized();
    if (!capabilities.testFlag(PDFAnnotationManipulator::Move) || !oldRectangle.isValid())
    {
        return false;
    }

    // Jakub Melka: the rectangle is not just overwritten, the annotation is transformed,
    // so its geometry (points, callout line, ...) follows the rectangle
    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setRectangle(modifier.getBuilder(), annotation, newRectangle) || !modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::setAnnotationTextRectangle(PDFObjectReference annotation, const QRectF& textRectangle)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setFreeTextRectangle(modifier.getBuilder(), annotation, textRectangle))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::addAnnotationCalloutLine(PDFObjectReference annotation, const QPointF& tip)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(&m_document->getStorage(), annotation);
    if (!textRectangle.isValid() || textRectangle.contains(tip))
    {
        return false;
    }

    // The callout line ends in the middle of the edge of the text box, which is the nearest to the tip
    const std::array<QPointF, 4> edgeCenters = { QPointF(textRectangle.left(), textRectangle.center().y()),
                                                 QPointF(textRectangle.right(), textRectangle.center().y()),
                                                 QPointF(textRectangle.center().x(), textRectangle.top()),
                                                 QPointF(textRectangle.center().x(), textRectangle.bottom()) };
    const QPointF end = *std::min_element(edgeCenters.cbegin(), edgeCenters.cend(), [&tip](const QPointF& left, const QPointF& right)
    {
        return QLineF(tip, left).length() < QLineF(tip, right).length();
    });

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setFreeTextCalloutLine(modifier.getBuilder(), annotation, { tip, end }))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::removeAnnotationCalloutLine(PDFObjectReference annotation)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setFreeTextCalloutLine(modifier.getBuilder(), annotation, { }))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::removeAnnotationPart(PDFObjectReference annotation, size_t index)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::removePart(modifier.getBuilder(), annotation, index))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::addAnnotationParts(PDFObjectReference annotation, const std::vector<QPolygonF>& shapes)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation) || shapes.empty())
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    for (const QPolygonF& shape : shapes)
    {
        if (!PDFAnnotationManipulator::addPart(modifier.getBuilder(), annotation, shape))
        {
            return false;
        }
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::setAnnotationParts(PDFObjectReference annotation, const std::vector<QPolygonF>& shapes)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setParts(modifier.getBuilder(), annotation, shapes))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::eraseAnnotationInk(PDFObjectReference annotation, const QPointF& center, PDFReal radius)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::eraseInk(modifier.getBuilder(), annotation, center, radius))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::addAnnotationReply(PDFObjectReference annotation, const QString& contents)
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::addReply(modifier.getBuilder(), annotation, PDFAuthorSettings::getAuthorName(), contents).isValid() || !modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::setAnnotationFileAttachment(PDFObjectReference annotation, const QString& fileName, const QByteArray& data)
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation || !canModifyAnnotation(*pageAnnotation))
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setFileAttachment(modifier.getBuilder(), annotation, fileName, data) || !modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::beginPartEdit(PartEdit partEdit)
{
    m_partEdit = PartEdit::None;
    requestRepaint();

    const PageAnnotation* pageAnnotation = m_selection.size() == 1 ? findPageAnnotation(m_selection.front().pageIndex, m_selection.front().annotation) : nullptr;
    if (!m_document || partEdit == PartEdit::None || !pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    // Strokes are added to an ink, areas are marked by a text markup (by a redaction)
    const PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(&m_document->getStorage(), m_selection.front().annotation);
    if (!parts.isSupported || parts.isFilled == (partEdit == PartEdit::AddStroke))
    {
        return false;
    }

    m_partEdit = partEdit;
    m_cursor = QCursor(Qt::CrossCursor);
    return true;
}

std::vector<QPolygonF> PDFWidgetAnnotationManager::getMarkedShapes(PDFInteger pageIndex, const QPointF& start, const QPointF& end) const
{
    std::vector<QPolygonF> shapes;

    // Jakub Melka: lines of the text between the points, the same way, as the tools, which create
    // the text markups, mark them. The corners of a line are in the order top left, top right,
    // bottom left, bottom right, the shape of a part goes around the area.
    if (PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler())
    {
        PDFTextLayoutGetter textLayoutGetter = compiler->getTextLayoutLazy(pageIndex);
        PDFTextLayout textLayout = textLayoutGetter;
        PDFTextSelection textSelection = textLayout.createTextSelection(pageIndex, start, end, Qt::yellow);

        QPolygonF quadrilaterals;
        PDFTextSelectionPainter textSelectionPainter(&textSelection);
        textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

        for (qsizetype i = 3; i < quadrilaterals.size(); i += 4)
        {
            shapes.push_back(QPolygonF({ quadrilaterals[i - 3], quadrilaterals[i - 2], quadrilaterals[i], quadrilaterals[i - 1] }));
        }
    }

    // There is no text (a scanned page, a picture), so the area itself is marked.
    // The y axis of the page points upwards, so the top edge has the greatest y.
    const QRectF area = QRectF(start, end).normalized();
    if (shapes.empty() && area.width() > 1.0 && area.height() > 1.0)
    {
        shapes.push_back(QPolygonF({ QPointF(area.left(), area.bottom()), QPointF(area.right(), area.bottom()),
                                     QPointF(area.right(), area.top()), QPointF(area.left(), area.top()) }));
    }

    return shapes;
}

void PDFWidgetAnnotationManager::translateSelectedAnnotationsOnScreen(const std::function<QPointF(const QRectF&, const QRectF&, const std::vector<QRectF>&, size_t)>& function)
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
        bool invertible = false;
        const QTransform pageToDevice = getPageToDeviceMatrix(pageIndex);
        const QTransform deviceToPage = pageToDevice.inverted(&invertible);
        const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(pageIndex, true);
        if (!invertible || annotations.size() < 2)
        {
            continue;
        }

        // Rectangles of the annotations, as they are displayed
        QRectF frame;
        std::vector<QRectF> rectangles;
        for (const PageAnnotation* pageAnnotation : annotations)
        {
            const QTransform matrix = getAnnotationToDeviceMatrix(*pageAnnotation, pageIndex, pageToDevice);
            rectangles.push_back(matrix.mapRect(pageAnnotation->annotation->getRectangle().normalized()).normalized());
            frame = frame.united(rectangles.back());
        }

        for (size_t i = 0; i < annotations.size(); ++i)
        {
            const QPointF deviceOffset = function(rectangles[i], frame, rectangles, i);
            const QPointF pageOffset = deviceToPage.map(deviceOffset) - deviceToPage.map(QPointF(0.0, 0.0));
            if (qFuzzyIsNull(pageOffset.x()) && qFuzzyIsNull(pageOffset.y()))
            {
                continue;
            }

            const QTransform translation = QTransform::fromTranslate(pageOffset.x(), pageOffset.y());
            isModified = PDFAnnotationManipulator::transformAnnotation(modifier.getBuilder(), annotations[i]->annotation->getSelfReference(), translation) || isModified;
        }
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void PDFWidgetAnnotationManager::alignSelectedAnnotations(Alignment alignment)
{
    translateSelectedAnnotationsOnScreen([alignment](const QRectF& rectangle, const QRectF& frame, const std::vector<QRectF>&, size_t)
    {
        switch (alignment)
        {
            case Alignment::Left:
                return QPointF(frame.left() - rectangle.left(), 0.0);
            case Alignment::HorizontalCenter:
                return QPointF(frame.center().x() - rectangle.center().x(), 0.0);
            case Alignment::Right:
                return QPointF(frame.right() - rectangle.right(), 0.0);
            case Alignment::Top:
                return QPointF(0.0, frame.top() - rectangle.top());
            case Alignment::VerticalCenter:
                return QPointF(0.0, frame.center().y() - rectangle.center().y());
            case Alignment::Bottom:
                return QPointF(0.0, frame.bottom() - rectangle.bottom());
        }

        return QPointF();
    });
}

void PDFWidgetAnnotationManager::distributeSelectedAnnotations(Qt::Orientation orientation)
{
    translateSelectedAnnotationsOnScreen([orientation](const QRectF& rectangle, const QRectF& frame, const std::vector<QRectF>& rectangles, size_t index)
    {
        if (rectangles.size() < 3)
        {
            return QPointF();
        }

        const bool isHorizontal = orientation == Qt::Horizontal;
        auto getStart = [isHorizontal](const QRectF& r) { return isHorizontal ? r.left() : r.top(); };
        auto getSize = [isHorizontal](const QRectF& r) { return isHorizontal ? r.width() : r.height(); };

        // Order of the annotations on the screen
        std::vector<size_t> order(rectangles.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) { return getStart(rectangles[left]) < getStart(rectangles[right]); });

        // The same gap between the annotations, the first and the last one stay
        qreal totalSize = 0.0;
        for (const QRectF& r : rectangles)
        {
            totalSize += getSize(r);
        }
        const qreal gap = (getSize(frame) - totalSize) / qreal(rectangles.size() - 1);

        qreal position = getStart(frame);
        for (const size_t current : order)
        {
            if (current == index)
            {
                break;
            }
            position += getSize(rectangles[current]) + gap;
        }

        const qreal offset = position - getStart(rectangle);
        return isHorizontal ? QPointF(offset, 0.0) : QPointF(0.0, offset);
    });
}

QString PDFWidgetAnnotationManager::getCapabilitiesHint(PDFAnnotationManipulator::Capabilities capabilities) const
{
    if (!capabilities.testFlag(PDFAnnotationManipulator::Move))
    {
        return tr("This annotation cannot be modified.");
    }

    if (!capabilities.testFlag(PDFAnnotationManipulator::Resize))
    {
        return tr("This annotation can only be moved, its size is fixed.");
    }

    if (!capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle))
    {
        return tr("This annotation can be moved and resized, it cannot be rotated.");
    }

    if (!capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary))
    {
        return tr("This annotation can be rotated only in steps of 90°.");
    }

    return QString();
}

bool PDFWidgetAnnotationManager::hasSelectableAnnotation() const
{
    if (!m_document)
    {
        return false;
    }

    for (const PDFInteger pageIndex : m_proxy->getWidget()->getDrawWidget()->getCurrentPages())
    {
        const PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
        if (std::any_of(pageAnnotations.annotations.cbegin(), pageAnnotations.annotations.cend(), [this](const PageAnnotation& pageAnnotation) { return isAnnotationSelectable(pageAnnotation); }))
        {
            return true;
        }
    }

    return false;
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
    m_activePoint = -1;
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
        m_activePoint = -1;
        Q_EMIT selectionChanged();
        requestRepaint();
    }
}

void PDFWidgetAnnotationManager::clearSelection()
{
    if (!m_selection.empty())
    {
        m_selection.clear();
        m_activePoint = -1;
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

    // The point edited from the keyboard must still exist
    const PointEditInfo pointInfo = getPointEditInfo();
    if (!pointInfo.isValid() || m_activePoint >= int(pointInfo.points.points.size()))
    {
        m_activePoint = -1;
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

QTransform PDFWidgetAnnotationManager::getAnnotationToDeviceMatrix(const PageAnnotation& annotation, PDFInteger pageIndex, const QTransform& pageToDevice) const
{
    const PDFAnnotation::Flags flags = annotation.annotation->getEffectiveFlags();
    if (!flags.testFlag(PDFAnnotation::NoRotate) && !flags.testFlag(PDFAnnotation::NoZoom))
    {
        return pageToDevice;
    }

    // Jakub Melka: the annotation is not displayed by the matrix of the page - it is
    // not rotated with the page and/or it has a fixed size. We use the same function
    // as the renderer does, so the markers of the selection (and the hit tests) match
    // the displayed annotation. The annotation is drawn into the adjusted rectangle.
    const QRectF rectangle = annotation.annotation->getRectangle();
    QRectF displayedRectangle = rectangle;
    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const QTransform matrix = prepareTransformations(pageToDevice, m_proxy->getWidget(), flags, page, displayedRectangle);

    if (rectangle.isEmpty() || displayedRectangle.isEmpty())
    {
        return matrix;
    }

    const QTransform fit = QTransform::fromTranslate(-rectangle.left(), -rectangle.top()) *
                           QTransform::fromScale(displayedRectangle.width() / rectangle.width(), displayedRectangle.height() / rectangle.height()) *
                           QTransform::fromTranslate(displayedRectangle.left(), displayedRectangle.top());
    return fit * matrix;
}

QTransform PDFWidgetAnnotationManager::getSelectionToDeviceMatrix(PDFInteger pageIndex, const QTransform& pageToDevice) const
{
    const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(pageIndex, true);
    if (annotations.size() == 1)
    {
        return getAnnotationToDeviceMatrix(*annotations.front(), pageIndex, pageToDevice);
    }

    return pageToDevice;
}

void PDFWidgetAnnotationManager::keepDisplayedPosition(PDFDocumentBuilder* builder, PDFObjectReference annotation) const
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, annotation) : nullptr;
    if (!pageAnnotation)
    {
        return;
    }

    // Matrix, by which the annotation was displayed, when the user edited it
    const QTransform pageToDevice = getPageToDeviceMatrix(pageIndex);
    const QTransform annotationToDevice = getAnnotationToDeviceMatrix(*pageAnnotation, pageIndex, pageToDevice);

    bool invertible = false;
    const QTransform deviceToPage = pageToDevice.inverted(&invertible);
    if (annotationToDevice == pageToDevice || !invertible)
    {
        return;
    }

    const PDFAnnotationPtr editedAnnotation = PDFAnnotation::parse(builder->getStorage(), annotation);
    if (!editedAnnotation)
    {
        return;
    }

    // Jakub Melka: the anchor (the top left corner of the rectangle) is the only point, which
    // is displayed by the matrix of the page, the rest of the annotation is displayed relative
    // to it (and the relative matrix does not depend on the rectangle). The edited geometry was
    // displayed using the old anchor. So we move the annotation in such a way, that its new
    // anchor is at the place, where the old matrix displays it - then the new matrix displays
    // the whole annotation at the same place, as the old matrix does.
    const QPointF anchor = editedAnnotation->getRectangle().bottomLeft();
    const QPointF displayedAnchor = deviceToPage.map(annotationToDevice.map(anchor));
    const QPointF offset = displayedAnchor - anchor;
    if (!qFuzzyIsNull(offset.x()) || !qFuzzyIsNull(offset.y()))
    {
        PDFAnnotationManipulator::transformAnnotation(builder, annotation, QTransform::fromTranslate(offset.x(), offset.y()));
    }
}

QTransform PDFWidgetAnnotationManager::getPageToDeviceMatrix(PDFInteger pageIndex) const
{
    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    if (const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex))
    {
        return snapshotItem->pageToDeviceMatrix;
    }

    // The page is not displayed. The matrix has the right orientation (rotation
    // of the page and rotation of the view), the scale is not important.
    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    return m_proxy->createPagePointToDevicePointMatrix(page, QRectF(0.0, 0.0, 1.0, 1.0));
}

bool PDFWidgetAnnotationManager::isAnnotationShapeAtPosition(const PageAnnotation& pageAnnotation, const QTransform& annotationToDevice, const QPointF& devicePosition) const
{
    const PDFAnnotation* annotation = pageAnnotation.annotation.data();

    QPainterPath outline;   // Shape, which is drawn by a line
    QPainterPath area;      // Shape, which is filled

    if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(annotation))
    {
        // Leader lines move the displayed line away from its points and a caption
        // is a text next to the line, so such lines are tested by the rectangle
        const bool isPlainLine = qFuzzyIsNull(lineAnnotation->getLeaderLineLength()) &&
                                 qFuzzyIsNull(lineAnnotation->getLeaderLineExtension()) &&
                                 !lineAnnotation->isCaptionRendered();
        if (!isPlainLine || lineAnnotation->getLine().isNull())
        {
            return true;
        }

        outline.moveTo(lineAnnotation->getLine().p1());
        outline.lineTo(lineAnnotation->getLine().p2());
    }
    else if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation))
    {
        if (polygonalAnnotation->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Dimension)
        {
            // The measured value is displayed inside the shape
            return true;
        }

        const bool isPolygon = annotation->getType() == AnnotationType::Polygon;
        outline = polygonalAnnotation->getPath();
        if (outline.isEmpty())
        {
            QPolygonF polygon;
            for (const QPointF& vertex : polygonalAnnotation->getVertices())
            {
                polygon << vertex;
            }
            outline.addPolygon(polygon);
            if (isPolygon)
            {
                outline.closeSubpath();
            }
        }

        if (isPolygon && !polygonalAnnotation->getInteriorColor().empty())
        {
            area = outline;
        }
    }
    else if (const PDFInkAnnotation* inkAnnotation = dynamic_cast<const PDFInkAnnotation*>(annotation))
    {
        outline = inkAnnotation->getInkPath();
    }
    else if (const PDFHighlightAnnotation* highlightAnnotation = dynamic_cast<const PDFHighlightAnnotation*>(annotation))
    {
        area = highlightAnnotation->getHiglightArea().getPath();
    }
    else if (const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation))
    {
        area = redactAnnotation->getRedactionRegion().getPath();
    }

    if (outline.isEmpty() && area.isEmpty())
    {
        // The annotation fills its rectangle (or its shape is not known)
        return true;
    }

    if (!area.isEmpty() && annotationToDevice.map(area).contains(devicePosition))
    {
        return true;
    }

    if (outline.isEmpty())
    {
        return false;
    }

    // Tolerance in pixels, so thin lines can be hit comfortably at any zoom
    const qreal scale = std::sqrt(std::abs(annotationToDevice.determinant()));
    const qreal tolerance = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 5) + 0.5 * annotation->getBorder().getWidth() * scale;

    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * tolerance);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(annotationToDevice.map(outline)).contains(devicePosition);
}

std::vector<const PDFAnnotationManager::PageAnnotation*> PDFWidgetAnnotationManager::findSelectableAnnotations(QPoint widgetPos, PDFInteger* pageIndex) const
{
    std::vector<const PageAnnotation*> shapeHits;
    std::vector<const PageAnnotation*> rectangleHits;

    if (!m_document)
    {
        return shapeHits;
    }

    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();

    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        if (!snapshotItem.rect.contains(widgetPos))
        {
            continue;
        }

        const PageAnnotations& pageAnnotations = getPageAnnotations(snapshotItem.pageIndex);

        // Annotations are drawn in the order of the array, so the last one is on the top
        for (auto it = pageAnnotations.annotations.crbegin(); it != pageAnnotations.annotations.crend(); ++it)
        {
            const PageAnnotation& pageAnnotation = *it;
            if (!isAnnotationSelectable(pageAnnotation))
            {
                continue;
            }

            const QTransform matrix = getAnnotationToDeviceMatrix(pageAnnotation, snapshotItem.pageIndex, snapshotItem.pageToDeviceMatrix);
            QPainterPath path;
            path.addRect(pageAnnotation.annotation->getRectangle());
            path = matrix.map(path);

            if (!path.contains(widgetPos))
            {
                continue;
            }

            // Jakub Melka: a thin shape (a line, an outline of a polygon) must not block the
            // annotations inside its rectangle, so the annotations, whose shape is under the
            // cursor, go first. The rectangle of a selected annotation is a comfortable
            // target for moving it, so the selected annotation is always hit by its rectangle.
            if (isAnnotationSelected(pageAnnotation.annotation->getSelfReference()) || isAnnotationShapeAtPosition(pageAnnotation, matrix, widgetPos))
            {
                shapeHits.push_back(&pageAnnotation);
            }
            else
            {
                rectangleHits.push_back(&pageAnnotation);
            }
        }

        if (!shapeHits.empty() || !rectangleHits.empty())
        {
            if (pageIndex)
            {
                *pageIndex = snapshotItem.pageIndex;
            }
            break;
        }
    }

    shapeHits.insert(shapeHits.end(), rectangleHits.cbegin(), rectangleHits.cend());
    return shapeHits;
}

const PDFAnnotationManager::PageAnnotation* PDFWidgetAnnotationManager::findSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const
{
    const std::vector<const PageAnnotation*> annotations = findSelectableAnnotations(widgetPos, pageIndex);
    return annotations.empty() ? nullptr : annotations.front();
}

const PDFAnnotationManager::PageAnnotation* PDFWidgetAnnotationManager::findNextSelectableAnnotation(QPoint widgetPos, PDFInteger* pageIndex) const
{
    const std::vector<const PageAnnotation*> annotations = findSelectableAnnotations(widgetPos, pageIndex);
    if (annotations.empty())
    {
        return nullptr;
    }

    // The annotation after the last selected annotation in the list
    size_t nextIndex = 0;
    for (size_t i = 0; i < annotations.size(); ++i)
    {
        if (isAnnotationSelected(annotations[i]->annotation->getSelfReference()))
        {
            nextIndex = (i + 1) % annotations.size();
        }
    }

    return annotations[nextIndex];
}

PDFWidgetAnnotationManager::HandleLayout PDFWidgetAnnotationManager::computeHandleLayout(PDFInteger pageIndex, const QTransform& pageToDevice) const
{
    HandleLayout layout;

    // Jakub Melka: the frame is computed from the displayed annotations - an annotation
    // can be displayed by another matrix, than the matrix of the page (flags NoRotate, NoZoom)
    QRectF frame;
    for (const PageAnnotation* pageAnnotation : getSelectedAnnotations(pageIndex, true))
    {
        const QRectF rectangle = pageAnnotation->annotation->getRectangle().normalized();
        if (rectangle.isValid())
        {
            frame = frame.united(getAnnotationToDeviceMatrix(*pageAnnotation, pageIndex, pageToDevice).mapRect(rectangle).normalized());
        }
    }

    if (!frame.isValid())
    {
        return layout;
    }

    const PDFAnnotationManipulator::Capabilities capabilities = getSelectionCapabilities(pageIndex);
    layout.hasResizeHandles = capabilities.testFlag(PDFAnnotationManipulator::Resize);
    layout.hasRotationHandle = capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle) || capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary);
    layout.isRotationArbitrary = capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary);

    // Jakub Melka: the rectangle of a free text annotation with a callout line covers the text
    // box and the callout line. The user wants to work with the text box - to move it or to resize
    // it, while the callout line still points to the same place. So the frame is the text box.
    const TextBoxInfo textBoxInfo = getTextBoxInfo();
    if (textBoxInfo.isValid() && textBoxInfo.pageIndex == pageIndex)
    {
        frame = getSelectionToDeviceMatrix(pageIndex, pageToDevice).mapRect(textBoxInfo.textRectangle).normalized();
        layout.isTextBox = true;
        layout.hasResizeHandles = true;
        layout.hasRotationHandle = false;
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
    if (layout.hasRotationHandle && std::hypot(rotationDifference.x(), rotationDifference.y()) <= layout.rotationHandleRadius + tolerance)
    {
        return Handle::Rotate;
    }

    if (!layout.hasResizeHandles)
    {
        return Handle::None;
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
        if (m_interaction.type != Interaction::None || m_partEdit != PartEdit::None)
        {
            cancelInteraction();
            event->accept();
            return;
        }

        if (m_activePoint != -1)
        {
            // End the editing of the point from the keyboard
            m_activePoint = -1;
            requestRepaint();
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

        if (event->matches(QKeySequence::SelectAll) && hasSelectableAnnotation())
        {
            selectAllAnnotations();
            event->accept();
            return;
        }

        // Properties of the selected annotation
        const bool isEditKey = event->key() == Qt::Key_F2 || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (isEditKey && m_selection.size() == 1 && isModificationAllowed())
        {
            const SelectedAnnotation& item = m_selection.front();
            if (const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation))
            {
                m_editableAnnotation = item.annotation;
                m_editableAnnotationPage = getPageReference(item.pageIndex, *pageAnnotation);
                event->accept();
                onEditAnnotation();
                return;
            }
        }

        // Context menu of the selection from the keyboard
        const bool isMenuKey = event->key() == Qt::Key_Menu || (event->key() == Qt::Key_F10 && event->modifiers().testFlag(Qt::ShiftModifier));
        if (isMenuKey && hasSelection())
        {
            PDFWidget* pdfWidget = m_proxy->getWidget();
            QPoint menuPosition = pdfWidget->rect().center();

            const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
            for (const SelectedAnnotation& item : m_selection)
            {
                const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(item.pageIndex);
                const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
                if (snapshotItem && pageAnnotation)
                {
                    const QTransform matrix = getAnnotationToDeviceMatrix(*pageAnnotation, item.pageIndex, snapshotItem->pageToDeviceMatrix);
                    menuPosition = matrix.map(pageAnnotation->annotation->getRectangle().center()).toPoint();
                    break;
                }
            }

            event->accept();
            showSelectionMenu(pdfWidget->mapToGlobal(menuPosition), std::nullopt, PointMenuContext());
            return;
        }
    }

    // Points of the selected annotation edited from the keyboard
    if (handlePointKeys(event))
    {
        event->accept();
        return;
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

        if (!deviceDirection.isNull() && getSelectionCapabilities().testFlag(PDFAnnotationManipulator::Move))
        {
            nudgeSelectedAnnotations(deviceDirection, event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0);
            event->accept();
            return;
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

bool PDFWidgetAnnotationManager::handlePointKeys(QKeyEvent* event)
{
    const PointEditInfo info = getPointEditInfo();
    if (!info.isValid())
    {
        return false;
    }

    const int pointCount = int(info.points.points.size());
    const bool isAlt = event->modifiers().testFlag(Qt::AltModifier);

    // Alt + Left/Right selects the point
    if (isAlt && (event->key() == Qt::Key_Right || event->key() == Qt::Key_Left))
    {
        const int step = event->key() == Qt::Key_Right ? 1 : -1;
        m_activePoint = m_activePoint == -1 ? (step == 1 ? 0 : pointCount - 1) : (m_activePoint + step + pointCount) % pointCount;
        requestRepaint();
        return true;
    }

    if (m_activePoint < 0 || m_activePoint >= pointCount || isAlt || event->modifiers().testFlag(Qt::ControlModifier))
    {
        return false;
    }

    const size_t pointIndex = size_t(m_activePoint);
    const int activePoint = m_activePoint;

    switch (event->key())
    {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            // Direction is given on the screen
            QPointF deviceDirection(event->key() == Qt::Key_Left ? -1.0 : (event->key() == Qt::Key_Right ? 1.0 : 0.0),
                                    event->key() == Qt::Key_Up ? -1.0 : (event->key() == Qt::Key_Down ? 1.0 : 0.0));

            // The annotation need not be displayed by the matrix of the page (flags NoRotate, NoZoom)
            bool invertible = false;
            const QTransform deviceToPage = getSelectionToDeviceMatrix(info.pageIndex, getPageToDeviceMatrix(info.pageIndex)).inverted(&invertible);
            const QPointF pageDirection = deviceToPage.map(deviceDirection) - deviceToPage.map(QPointF(0.0, 0.0));
            const qreal length = std::hypot(pageDirection.x(), pageDirection.y());
            if (!invertible || qFuzzyIsNull(length))
            {
                return true;
            }

            const qreal distance = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
            moveAnnotationPoint(info.annotation, pointIndex, info.points.points[pointIndex] + pageDirection * (distance / length));
            m_activePoint = activePoint;
            requestRepaint();
            return true;
        }

        case Qt::Key_Delete:
        {
            if (removeAnnotationPoint(info.annotation, pointIndex))
            {
                m_activePoint = qMin(activePoint, pointCount - 2);
                requestRepaint();
            }
            return true;
        }

        case Qt::Key_Insert:
        {
            // New point in the middle of the segment, which starts at the point
            const size_t segmentCount = info.points.isClosed ? info.points.points.size() : info.points.points.size() - 1;
            if (pointIndex < segmentCount)
            {
                const QPointF start = info.points.points[pointIndex];
                const QPointF end = info.points.points[(pointIndex + 1) % info.points.points.size()];
                if (insertAnnotationPoint(info.annotation, pointIndex, (start + end) * 0.5))
                {
                    m_activePoint = activePoint + 1;
                    requestRepaint();
                }
            }
            return true;
        }

        default:
            break;
    }

    return false;
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

        // The press starts a new gesture, the release finishes just this gesture
        m_isLinkPressed = false;
        m_pendingDeselection = PDFObjectReference();

        // 0) The user was asked to draw a new part of the selected annotation
        if (m_partEdit != PartEdit::None)
        {
            if (!beginPartInteraction(event->pos()))
            {
                cancelInteraction();
            }

            event->accept();
            return;
        }

        // 1) Points of the selected annotation and handles of the selection frame.
        //    Points go first - for a line, they are close to the corners of the frame.
        //    A handle is an explicit target, so it has precedence over the modifiers
        //    of the selection - the user can hold Shift (which constrains the
        //    transformation) before the handle is pressed.
        if (beginPointInteraction(event->pos()) || beginHandleInteraction(event->pos()))
        {
            event->accept();
            return;
        }

        // 2) Cycling through the annotations, which overlap under the cursor
        PDFInteger pageIndex = -1;
        if (event->modifiers().testFlag(Qt::AltModifier))
        {
            if (const PageAnnotation* pageAnnotation = findNextSelectableAnnotation(event->pos(), &pageIndex))
            {
                selectAnnotation(pageAnnotation->annotation->getSelfReference(), true);
                event->accept();
                return;
            }
        }

        // 3) Selectable annotation under the cursor
        if (const PageAnnotation* pageAnnotation = findSelectableAnnotation(event->pos(), &pageIndex))
        {
            const PDFObjectReference annotation = pageAnnotation->annotation->getSelfReference();

            if (!isAnnotationSelected(annotation))
            {
                selectAnnotation(annotation, !isSelectionModifier);
            }
            else if (isSelectionModifier)
            {
                // Jakub Melka: the annotation is removed from the selection, when the button is
                // released - the user can also start to drag the selection (Ctrl + drag copies
                // it), and the dragged annotation must stay selected.
                m_pendingDeselection = annotation;
            }

            // The pointer is still valid - selection does not change the annotation list
            if (canTransformAnnotation(*pageAnnotation))
            {
                beginAnnotationDrag(event, pageIndex);
            }

            event->accept();
            return;
        }

        // 4) Links
        if (getLinkActionAtPosition(event->pos()))
        {
            m_isLinkPressed = true;
            event->accept();
            return;
        }

        // 5) Empty area of the page - rubber band selection, or clearing of the selection.
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
                    m_interaction.pageToDeviceBase = snapshotItem->pageToDeviceMatrix;
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

        // Jakub Melka: a point (a segment) of the selected annotation has the same
        // precedence, as it has for the left button - the selection must not be switched
        // to another annotation, which overlaps the point.
        PointMenuContext pointContext = getPointMenuContext(event->pos());

        PDFInteger pageIndex = -1;
        const PageAnnotation* pageAnnotation = pointContext.annotation.isValid() ? nullptr : findSelectableAnnotation(event->pos(), &pageIndex);
        if (pageAnnotation)
        {
            const PDFObjectReference annotation = pageAnnotation->annotation->getSelfReference();
            if (!isAnnotationSelected(annotation))
            {
                selectAnnotation(annotation, !isSelectionModifier);
                pointContext = getPointMenuContext(event->pos());
            }
        }

        PDFWidget* pdfWidget = m_proxy->getWidget();
        if (hasSelection() || canPasteAnnotations() || hasSelectableAnnotation())
        {
            event->accept();
            showSelectionMenu(pdfWidget->mapToGlobal(event->pos()), event->pos(), pointContext);
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
    if (!pageAnnotation)
    {
        return;
    }

    if (!isAnnotationSelectable(*pageAnnotation))
    {
        // Jakub Melka: hidden annotations and replies are listed by the sidebar, but
        // they cannot be selected on the page. The user must still be able to manage
        // them - otherwise a hidden annotation could never be made visible again.
        showUnselectableAnnotationMenu(*pageAnnotation, pageReference, globalMenuPosition);
        return;
    }

    m_selection = { SelectedAnnotation{ annotationReference, PDFInteger(pageIndex) } };
    Q_EMIT selectionChanged();
    requestRepaint();

    showSelectionMenu(globalMenuPosition, std::nullopt, PointMenuContext());
}

void PDFWidgetAnnotationManager::showUnselectableAnnotationMenu(const PageAnnotation& annotation, PDFObjectReference pageReference, QPoint globalPosition)
{
    clearSelection();

    m_editableAnnotation = annotation.annotation->getSelfReference();
    m_editableAnnotationPage = pageReference;
    m_editableAnnotationGlobalPosition = globalPosition;

    QMenu menu(tr("Annotation"), m_proxy->getWidget());

    if (annotation.annotation->asMarkupAnnotation() && !annotation.annotation->isReplyTo())
    {
        QAction* showPopupAction = menu.addAction(tr("Show Popup Window"));
        connect(showPopupAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onShowPopupAnnotation);
    }

    QAction* editAction = menu.addAction(tr("Edit..."));
    editAction->setEnabled(isModificationAllowed());
    connect(editAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onEditAnnotation);

    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(canModifyAnnotation(annotation));
    connect(deleteAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onDeleteAnnotation);

    menu.exec(globalPosition);
}

void PDFWidgetAnnotationManager::showSelectionMenu(QPoint globalPosition, std::optional<QPoint> widgetPosition, const PointMenuContext& pointContext)
{
    PDFWidget* pdfWidget = m_proxy->getWidget();
    QMenu menu(tr("Annotation"), pdfWidget);

    const bool hasSingleSelection = m_selection.size() == 1;
    const PageAnnotation* singleAnnotation = hasSingleSelection ? findPageAnnotation(m_selection.front().pageIndex, m_selection.front().annotation) : nullptr;

    m_editableAnnotation = PDFObjectReference();
    m_editableAnnotationPage = PDFObjectReference();
    m_editableAnnotationGlobalPosition = globalPosition;

    if (m_selection.size() > 1)
    {
        // Extent of the selection - operations of the menu are applied to all
        // selected annotations, also to the annotations on the pages, which are not displayed
        std::vector<PDFInteger> pages;
        size_t lockedCount = 0;
        for (const SelectedAnnotation& item : m_selection)
        {
            if (std::find(pages.cbegin(), pages.cend(), item.pageIndex) == pages.cend())
            {
                pages.push_back(item.pageIndex);
            }

            const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
            if (pageAnnotation && !canModifyAnnotation(*pageAnnotation))
            {
                ++lockedCount;
            }
        }

        QString title = tr("Selected annotations: %1, pages: %2").arg(m_selection.size()).arg(pages.size());
        if (lockedCount > 0)
        {
            title = tr("%1 (%2 cannot be modified and will be skipped)").arg(title).arg(lockedCount);
        }

        QAction* titleAction = menu.addAction(title);
        titleAction->setEnabled(false);
        menu.addSeparator();
    }

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
        editAction->setShortcut(QKeySequence(Qt::Key_F2));
        editAction->setEnabled(isModificationAllowed());
        connect(editAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onEditAnnotation);

        // Exact position, size, rotation and coordinates of the points
        QAction* geometryAction = menu.addAction(tr("Geometry..."));
        geometryAction->setEnabled(canTransformAnnotation(*singleAnnotation));
        connect(geometryAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onEditGeometry);

        menu.addSeparator();
    }

    const bool canDeleteAny = !getDeletableSelectedAnnotations().empty();
    const PDFAnnotationManipulator::Capabilities capabilities = getSelectionCapabilities();
    const bool canRotate = capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle);
    const bool canMirror = capabilities.testFlag(PDFAnnotationManipulator::Mirror);

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

    QAction* pasteInPlaceAction = menu.addAction(tr("Paste at Original Position"));
    pasteInPlaceAction->setEnabled(canPasteAnnotations());
    connect(pasteInPlaceAction, &QAction::triggered, this, [this, widgetPosition]() { pasteAnnotations(widgetPosition, true); });

    if (hasSelection())
    {
        QAction* copyToPagesAction = menu.addAction(tr("Copy to Multiple Pages..."));
        copyToPagesAction->setEnabled(isModificationAllowed());
        connect(copyToPagesAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onCopyAnnotation);
    }

    menu.addSeparator();

    // Jakub Melka: only the operations, which really change the selection, are
    // enabled (for example, a sticky note or a text box cannot be rotated)
    QAction* rotateClockwiseAction = menu.addAction(tr("Rotate 90° Clockwise"));
    rotateClockwiseAction->setEnabled(canRotate);
    connect(rotateClockwiseAction, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(90.0); });

    QAction* rotateCounterclockwiseAction = menu.addAction(tr("Rotate 90° Counterclockwise"));
    rotateCounterclockwiseAction->setEnabled(canRotate);
    connect(rotateCounterclockwiseAction, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(-90.0); });

    QAction* rotate180Action = menu.addAction(tr("Rotate 180°"));
    rotate180Action->setEnabled(canRotate);
    connect(rotate180Action, &QAction::triggered, this, [this]() { rotateSelectedAnnotations(180.0); });

    QAction* flipHorizontalAction = menu.addAction(tr("Flip Horizontal"));
    flipHorizontalAction->setEnabled(canMirror);
    connect(flipHorizontalAction, &QAction::triggered, this, [this]() { flipSelectedAnnotations(Qt::Horizontal); });

    QAction* flipVerticalAction = menu.addAction(tr("Flip Vertical"));
    flipVerticalAction->setEnabled(canMirror);
    connect(flipVerticalAction, &QAction::triggered, this, [this]() { flipSelectedAnnotations(Qt::Vertical); });

    if (singleAnnotation)
    {
        const QString hint = getCapabilitiesHint(getAnnotationCapabilities(*singleAnnotation));
        if (!hint.isEmpty())
        {
            QAction* hintAction = menu.addAction(hint);
            hintAction->setEnabled(false);
        }
    }

    // Alignment of several annotations (as the user sees them on the screen)
    bool canAlign = false;
    bool canDistribute = false;
    for (const SelectedAnnotation& item : m_selection)
    {
        const size_t count = getSelectedAnnotations(item.pageIndex, true).size();
        canAlign = canAlign || count >= 2;
        canDistribute = canDistribute || count >= 3;
    }

    if (canAlign)
    {
        QMenu* alignMenu = menu.addMenu(tr("Align"));
        const std::array<std::pair<QString, Alignment>, 6> alignments = { std::make_pair(tr("Left"), Alignment::Left),
                                                                          std::make_pair(tr("Center Horizontally"), Alignment::HorizontalCenter),
                                                                          std::make_pair(tr("Right"), Alignment::Right),
                                                                          std::make_pair(tr("Top"), Alignment::Top),
                                                                          std::make_pair(tr("Center Vertically"), Alignment::VerticalCenter),
                                                                          std::make_pair(tr("Bottom"), Alignment::Bottom) };
        for (const auto& [text, alignment] : alignments)
        {
            const Alignment currentAlignment = alignment;
            QAction* alignAction = alignMenu->addAction(text);
            connect(alignAction, &QAction::triggered, this, [this, currentAlignment]() { alignSelectedAnnotations(currentAlignment); });
        }

        alignMenu->addSeparator();
        QAction* distributeHorizontallyAction = alignMenu->addAction(tr("Distribute Horizontally"));
        distributeHorizontallyAction->setEnabled(canDistribute);
        connect(distributeHorizontallyAction, &QAction::triggered, this, [this]() { distributeSelectedAnnotations(Qt::Horizontal); });

        QAction* distributeVerticallyAction = alignMenu->addAction(tr("Distribute Vertically"));
        distributeVerticallyAction->setEnabled(canDistribute);
        connect(distributeVerticallyAction, &QAction::triggered, this, [this]() { distributeSelectedAnnotations(Qt::Vertical); });
    }

    if (m_selection.size() > 1)
    {
        // Properties, which can be set to several annotations at once
        QMenu* propertiesMenu = menu.addMenu(tr("Common Properties"));
        propertiesMenu->setEnabled(isModificationAllowed());

        QAction* colorAction = propertiesMenu->addAction(tr("Color..."));
        connect(colorAction, &QAction::triggered, this, [this]()
        {
            setSelectedAnnotationsColor(QColorDialog::getColor(Qt::yellow, m_proxy->getWidget(), tr("Color of Annotations")));
        });

        QAction* opacityAction = propertiesMenu->addAction(tr("Opacity..."));
        connect(opacityAction, &QAction::triggered, this, [this]()
        {
            bool ok = false;
            const double opacity = QInputDialog::getDouble(m_proxy->getWidget(), tr("Opacity of Annotations"), tr("Opacity (%)"), 100.0, 0.0, 100.0, 0, &ok);
            if (ok)
            {
                setSelectedAnnotationsOpacity(opacity / 100.0);
            }
        });

        QAction* borderWidthAction = propertiesMenu->addAction(tr("Line Width..."));
        connect(borderWidthAction, &QAction::triggered, this, [this]()
        {
            bool ok = false;
            const double width = QInputDialog::getDouble(m_proxy->getWidget(), tr("Line Width of Annotations"), tr("Line width (pt)"), 1.0, 0.0, 100.0, 1, &ok);
            if (ok)
            {
                setSelectedAnnotationsBorderWidth(width);
            }
        });
    }

    // Content of a file attachment
    if (singleAnnotation && singleAnnotation->annotation->getType() == AnnotationType::FileAttachment && canModifyAnnotation(*singleAnnotation))
    {
        const PDFObjectReference annotation = m_selection.front().annotation;
        QAction* replaceFileAction = menu.addAction(tr("Replace Attached File..."));
        connect(replaceFileAction, &QAction::triggered, this, [this, annotation]()
        {
            const QString fileName = QFileDialog::getOpenFileName(m_proxy->getWidget(), tr("Select File to Be Attached"));
            if (fileName.isEmpty())
            {
                return;
            }

            QFile file(fileName);
            if (!file.open(QFile::ReadOnly))
            {
                QMessageBox::critical(m_proxy->getWidget(), tr("Error"), tr("File '%1' cannot be read: %2").arg(fileName, file.errorString()));
                return;
            }

            setAnnotationFileAttachment(annotation, QFileInfo(fileName).fileName(), file.readAll());
        });
    }

    // Parts of the annotation under the cursor, callout line of a free text annotation
    if (singleAnnotation && canTransformAnnotation(*singleAnnotation))
    {
        const PDFObjectReference annotation = m_selection.front().annotation;
        const PDFInteger annotationPageIndex = m_selection.front().pageIndex;

        PDFObjectReference partAnnotation;
        const int partIndex = widgetPosition ? hitTestPart(*widgetPosition, &partAnnotation) : -1;
        if (partIndex != -1)
        {
            const bool isInk = singleAnnotation->annotation->getType() == AnnotationType::Ink;
            QAction* removePartAction = menu.addAction(isInk ? tr("Delete This Stroke") : tr("Delete This Marked Area"));
            connect(removePartAction, &QAction::triggered, this, [this, partAnnotation, partIndex]() { removeAnnotationPart(partAnnotation, size_t(partIndex)); });
        }

        // New parts are drawn by the mouse
        const PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(&m_document->getStorage(), annotation);
        if (parts.isSupported && !parts.isFilled)
        {
            QPointF pagePoint;
            if (widgetPosition && m_proxy->getPageUnderPoint(*widgetPosition, &pagePoint) == annotationPageIndex)
            {
                // Jakub Melka: the stroke is erased around the place, where the menu was opened.
                // The radius of the eraser is given on the screen, the place is in the coordinate
                // system of the annotation. The action is offered, if there is something to erase.
                const QTransform annotationToDevice = getSelectionToDeviceMatrix(annotationPageIndex, getPageToDeviceMatrix(annotationPageIndex));
                const PDFReal scale = std::sqrt(std::abs(annotationToDevice.determinant()));
                bool invertible = false;
                const QTransform deviceToAnnotation = annotationToDevice.inverted(&invertible);
                if (invertible && scale > 0.0)
                {
                    const QPointF center = deviceToAnnotation.map(QPointF(*widgetPosition));
                    const PDFReal radius = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 8) / scale;

                    PDFDocumentBuilder testBuilder(m_document);
                    if (PDFAnnotationManipulator::eraseInk(&testBuilder, annotation, center, radius))
                    {
                        QAction* eraseAction = menu.addAction(tr("Erase Stroke at This Place"));
                        connect(eraseAction, &QAction::triggered, this, [this, annotation, center, radius]() { eraseAnnotationInk(annotation, center, radius); });
                    }
                }
            }

            QAction* addStrokeAction = menu.addAction(tr("Add Stroke"));
            addStrokeAction->setToolTip(tr("Draw the new stroke by the mouse. Escape cancels it."));
            connect(addStrokeAction, &QAction::triggered, this, [this]() { beginPartEdit(PartEdit::AddStroke); });
        }
        else if (parts.isSupported)
        {
            QAction* addAreaAction = menu.addAction(tr("Add Marked Text or Area"));
            addAreaAction->setToolTip(tr("Drag the mouse over the text (or over an area without a text), which should be marked too. Escape cancels it."));
            connect(addAreaAction, &QAction::triggered, this, [this]() { beginPartEdit(PartEdit::AddMarkedAreas); });

            QAction* replaceAreaAction = menu.addAction(tr("Mark Another Text or Area Instead"));
            replaceAreaAction->setToolTip(tr("Drag the mouse over the text (or over an area without a text), which should be marked instead of the text, which is marked now. Escape cancels it."));
            connect(replaceAreaAction, &QAction::triggered, this, [this]() { beginPartEdit(PartEdit::ReplaceMarkedAreas); });
        }

        if (singleAnnotation->annotation->getType() == AnnotationType::FreeText)
        {
            if (getTextBoxInfo().isValid())
            {
                QAction* removeCalloutAction = menu.addAction(tr("Remove Callout Line"));
                connect(removeCalloutAction, &QAction::triggered, this, [this, annotation]() { removeAnnotationCalloutLine(annotation); });
            }
            else
            {
                // The callout line points to the place, where the menu was opened (if it is
                // outside of the text box), otherwise to a place next to the text box
                const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(&m_document->getStorage(), annotation);
                const QRectF mediaBox = m_document->getCatalog()->getPage(annotationPageIndex)->getMediaBox().normalized();
                QPointF tip = textRectangle.topLeft() + QPointF(-40.0, -40.0);
                for (const QPointF& candidate : { textRectangle.topLeft() + QPointF(-40.0, -40.0), textRectangle.topRight() + QPointF(40.0, -40.0),
                                                  textRectangle.bottomLeft() + QPointF(-40.0, 40.0), textRectangle.bottomRight() + QPointF(40.0, 40.0) })
                {
                    if (mediaBox.contains(candidate))
                    {
                        tip = candidate;
                        break;
                    }
                }

                QPointF pagePoint;
                if (widgetPosition && m_proxy->getPageUnderPoint(*widgetPosition, &pagePoint) == annotationPageIndex && !textRectangle.contains(pagePoint))
                {
                    tip = pagePoint;
                }

                QAction* addCalloutAction = menu.addAction(tr("Add Callout Line"));
                connect(addCalloutAction, &QAction::triggered, this, [this, annotation, tip]() { addAnnotationCalloutLine(annotation, tip); });
            }
        }
    }

    if (pointContext.annotation.isValid() && (pointContext.insertSegment != -1 || pointContext.removedPoint != -1))
    {
        menu.addSeparator();

        if (pointContext.insertSegment != -1)
        {
            QAction* insertPointAction = menu.addAction(tr("Insert Point"));
            connect(insertPointAction, &QAction::triggered, this, [this, pointContext]() { insertAnnotationPoint(pointContext.annotation, size_t(pointContext.insertSegment), pointContext.insertPosition); });
        }

        if (pointContext.removedPoint != -1)
        {
            QAction* removePointAction = menu.addAction(tr("Delete Point"));
            connect(removePointAction, &QAction::triggered, this, [this, pointContext]() { removeAnnotationPoint(pointContext.annotation, size_t(pointContext.removedPoint)); });
        }
    }

    menu.addSeparator();

    if (widgetPosition)
    {
        // Annotations, which overlap under the cursor
        PDFInteger pageIndex = -1;
        if (findSelectableAnnotations(*widgetPosition, &pageIndex).size() > 1)
        {
            const QPoint position = *widgetPosition;
            QAction* selectNextAction = menu.addAction(tr("Select Next Annotation at This Place"));
            connect(selectNextAction, &QAction::triggered, this, [this, position]()
            {
                PDFInteger nextPageIndex = -1;
                if (const PageAnnotation* pageAnnotation = findNextSelectableAnnotation(position, &nextPageIndex))
                {
                    selectAnnotation(pageAnnotation->annotation->getSelfReference(), true);
                }
            });
        }
    }

    QAction* selectAllAction = menu.addAction(tr("Select All on Displayed Pages"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setEnabled(hasSelectableAnnotation());
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

bool PDFWidgetAnnotationManager::handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action, bool isSnappingEnabled)
{
    clearAnnotationDropFeedback();

    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    // Jakub Melka: the dropped annotations snap to the geometry of the page and of the other
    // annotations (by the corners and by the center of their bounding rectangle)
    auto getSnapCorrection = [this, isSnappingEnabled](PDFInteger pageIndex, const QRectF& rectangle)
    {
        bool isSnapped = false;
        QPointF snappedDevicePoint;
        return isSnappingEnabled ? snapMovedRectangle(pageIndex, rectangle, &isSnapped, &snappedDevicePoint) : QPointF();
    };

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
        QPointF delta = newTopLeft - payload.boundingRectangle.topLeft();
        delta += getSnapCorrection(pageIndex, payload.boundingRectangle.translated(delta));
        const bool isMoved = !qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y());
        const bool isCopy = action == Qt::CopyAction;

        if (!isCopy && !isMoved && targetPageReference == payload.pageReference)
        {
            // Nothing has changed
            return false;
        }

        const QTransform translation = QTransform::fromTranslate(delta.x(), delta.y());
        const bool isTextBoxMoved = payload.isTextBoxOnly && !isCopy && targetPageReference == payload.pageReference && payload.annotations.size() == 1;
        if (isTextBoxMoved)
        {
            // Only the text box is moved, the tip of the callout line stays
            const PDFObjectReference annotation = payload.annotations.front();
            const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(builder->getStorage(), annotation);
            if (PDFAnnotationManipulator::setFreeTextRectangle(builder, annotation, textRectangle.translated(delta)))
            {
                resultAnnotations.push_back(annotation);
            }
        }

        for (const PDFObjectReference& annotation : isTextBoxMoved ? std::vector<PDFObjectReference>() : payload.annotations)
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

        offset += getSnapCorrection(pageIndex, serializedAnnotations.boundingRectangle.translated(offset));

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

void PDFWidgetAnnotationManager::prepareSnapper(const PDFWidgetSnapshot& snapshot, PDFInteger pageIndex, PDFObjectReference excludedAnnotation, bool excludeSelection)
{
    // Jakub Melka: geometry of the page and of the other annotations. The manipulated
    // annotations are excluded, they would snap to themselves.
    m_snapper.clear();
    m_snapper.buildSnapPoints(snapshot);

    if (const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex))
    {
        m_snapper.addSnapInfo(pageIndex, snapshotItem->pageToDeviceMatrix, getSnapInfo(pageIndex, excludedAnnotation, excludeSelection));
    }
}

QPointF PDFWidgetAnnotationManager::snapMovedRectangle(PDFInteger pageIndex, const QRectF& rectangle, bool* isSnapped, QPointF* snappedDevicePoint)
{
    *isSnapped = false;

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex);
    if (!snapshotItem || !rectangle.isValid())
    {
        return QPointF();
    }

    bool invertible = false;
    const QTransform deviceToPage = snapshotItem->pageToDeviceMatrix.inverted(&invertible);
    if (!invertible)
    {
        return QPointF();
    }

    prepareSnapper(snapshot, pageIndex, PDFObjectReference(), true);

    // The nearest snap wins
    QPointF correction;
    qreal distance = std::numeric_limits<qreal>::max();
    for (const QPointF& point : { rectangle.topLeft(), rectangle.topRight(), rectangle.bottomLeft(), rectangle.bottomRight(), rectangle.center() })
    {
        const QPointF devicePoint = snapshotItem->pageToDeviceMatrix.map(point);
        m_snapper.updateSnappedPoint(devicePoint);
        if (!m_snapper.isSnapped())
        {
            continue;
        }

        const QPointF snappedPoint = m_snapper.getSnappedPoint();
        const qreal currentDistance = QLineF(devicePoint, snappedPoint).length();
        if (currentDistance < distance)
        {
            distance = currentDistance;
            correction = deviceToPage.map(snappedPoint) - point;
            *isSnapped = true;
            *snappedDevicePoint = snappedPoint;
        }
    }

    m_snapper.clear();
    return correction;
}

void PDFWidgetAnnotationManager::updateAnnotationDropFeedback(const QMimeData* data, const QPoint& widgetPos, bool isSnappingEnabled)
{
    DropFeedback feedback;

    QPointF pagePoint;
    feedback.pageIndex = m_document ? m_proxy->getPageUnderPoint(widgetPos, &pagePoint) : -1;
    feedback.isActive = feedback.pageIndex >= 0;
    feedback.devicePosition = widgetPos;

    PDFAnnotationDragDataHelper::Payload payload;
    if (feedback.isActive && isSnappingEnabled && PDFAnnotationDragDataHelper::deserialize(data, payload))
    {
        const QRectF rectangle = payload.boundingRectangle.translated(pagePoint - payload.cursorOffset - payload.boundingRectangle.topLeft());
        snapMovedRectangle(feedback.pageIndex, rectangle, &feedback.isSnapped, &feedback.snappedDevicePoint);
    }

    m_dropFeedback = feedback;
    requestRepaint();
}

void PDFWidgetAnnotationManager::clearAnnotationDropFeedback()
{
    if (m_dropFeedback.isActive)
    {
        m_dropFeedback = DropFeedback();
        requestRepaint();
    }
}

std::optional<QPointF> PDFWidgetAnnotationManager::getAnnotationDropSnapPoint() const
{
    if (m_dropFeedback.isActive && m_dropFeedback.isSnapped)
    {
        return m_dropFeedback.snappedDevicePoint;
    }

    return std::nullopt;
}

QString PDFWidgetAnnotationManager::getInteractionScopeText(PDFInteger pageIndex) const
{
    const size_t changedCount = getSelectedAnnotations(pageIndex, true).size();
    if (changedCount == m_selection.size())
    {
        return QString();
    }

    return tr("changes %1 of %2 selected annotations (this page only)").arg(changedCount).arg(m_selection.size());
}

PDFSnapInfo PDFWidgetAnnotationManager::getSnapInfo(PDFInteger pageIndex, PDFObjectReference excludedAnnotation, bool excludeSelection) const
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
        if (!annotation || !PDFAnnotation::isTypeEditable(annotation->getType()) || annotation->getSelfReference() == excludedAnnotation)
        {
            continue;
        }

        if (excludeSelection && isAnnotationSelected(annotation->getSelfReference()))
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

        // Jakub Melka: the release finishes the gesture, which was started by the press.
        // A link is activated only if the button was pressed over a link - selecting
        // (or dragging) of an annotation, which lies over a link, must not navigate.
        const bool isLinkPressed = m_isLinkPressed;
        const PDFObjectReference pendingDeselection = m_pendingDeselection;
        m_isLinkPressed = false;
        m_pendingDeselection = PDFObjectReference();

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

        if (pendingDeselection.isValid())
        {
            // Click with a modifier on a selected annotation, which was not dragged
            deselectAnnotation(pendingDeselection);
            event->accept();
            return;
        }

        if (isLinkPressed)
        {
            if (const PDFAction* linkAction = getLinkActionAtPosition(event->pos()))
            {
                Q_EMIT actionTriggered(linkAction);
            }

            event->accept();
            return;
        }
    }
}

void PDFWidgetAnnotationManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);
    m_lastMousePosition = event->pos();

    if (m_interaction.type == Interaction::None && m_partEdit != PartEdit::None)
    {
        // The user was asked to draw a new part of the selected annotation
        m_cursor = QCursor(Qt::CrossCursor);
        event->accept();
        requestRepaint();
        return;
    }

    if (m_interaction.type != Interaction::None)
    {
        switch (m_interaction.type)
        {
            case Interaction::RubberBand:
                m_interaction.currentDevicePosition = event->pos();
                m_cursor = QCursor(Qt::CrossCursor);
                break;

            case Interaction::Part:
                updatePartInteraction(event->pos());
                m_cursor = QCursor(Qt::CrossCursor);
                break;

            case Interaction::Point:
                updatePointInteraction(event->pos(), event->modifiers());
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
    const int oldHoveredPoint = m_hoveredPoint;
    m_hoveredAnnotation = HoveredAnnotation();
    m_hoveredHandle = Handle::None;
    m_hoveredPoint = -1;

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
        // Points of the selected annotation have priority over the selection frame
        const PointEditInfo pointInfo = getPointEditInfo();
        if (pointInfo.isValid())
        {
            if (const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pointInfo.pageIndex))
            {
                m_hoveredPoint = hitTestPoint(pointInfo, getSelectionToDeviceMatrix(pointInfo.pageIndex, snapshotItem->pageToDeviceMatrix), event->pos());
                if (m_hoveredPoint != -1)
                {
                    m_cursor = QCursor(Qt::CrossCursor);
                    if (pointInfo.points.isQuadEnds)
                    {
                        m_tooltip = tr("Drag to make the marked line longer or shorter. Use the context menu to delete a marked line.");
                    }
                    else
                    {
                        m_tooltip = tr("Drag to move the point. Hold Shift to constrain the direction to multiples of 45°, hold Ctrl to disable snapping. Alt + Left/Right selects a point for the keyboard.");
                        if (pointInfo.points.minimalCount != pointInfo.points.maximalCount)
                        {
                            m_tooltip = tr("%1 Use the context menu (or Insert and Delete keys) to insert or delete points.").arg(m_tooltip);
                        }
                    }
                }
            }
        }

        for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
        {
            if (m_hoveredPoint != -1)
            {
                break;
            }

            const HandleLayout layout = computeHandleLayout(snapshotItem.pageIndex, snapshotItem.pageToDeviceMatrix);
            const Handle handle = hitTestHandle(layout, event->pos());
            if (handle == Handle::Rotate)
            {
                m_hoveredHandle = handle;
                m_cursor = getRotationCursor();
                m_tooltip = layout.isRotationArbitrary ? tr("Drag to rotate. Hold Shift to rotate in steps of 15°.")
                                                       : tr("Drag to rotate in steps of 90°. This annotation cannot be rotated by an arbitrary angle.");
                break;
            }
            if (handle != Handle::None)
            {
                m_hoveredHandle = handle;
                m_cursor = QCursor(getCursorShapeForHandle(handle));
                m_tooltip = layout.isTextBox ? tr("Drag to resize the text box, the callout line stays at its target. Hold Shift to keep the aspect ratio.")
                                             : tr("Drag to resize. Hold Shift to keep the aspect ratio.");
                break;
            }
        }

        if (m_hoveredHandle == Handle::None && m_hoveredPoint == -1)
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

                // Explain, why the selected annotation has no handles (or just some of them)
                if (m_selection.size() == 1 && isAnnotationSelected(m_hoveredAnnotation.annotation))
                {
                    if (getTextBoxInfo().isValid())
                    {
                        m_tooltip += QString("<p><i>%1</i></p>").arg(tr("Drag the text box to move it, the callout line stays at its target. Drag the callout line (or hold Alt) to move the whole annotation."));
                    }

                    const QString hint = getCapabilitiesHint(getAnnotationCapabilities(*pageAnnotation));
                    if (!hint.isEmpty())
                    {
                        m_tooltip += QString("<p><i>%1</i></p>").arg(hint);
                    }
                }
            }
        }
    }

    // If appearance has changed, then we must redraw the page
    if (appearanceChanged ||
        oldHoveredHandle != m_hoveredHandle ||
        oldHoveredPoint != m_hoveredPoint ||
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
        const HandleLayout layout = computeHandleLayout(snapshotItem.pageIndex, snapshotItem.pageToDeviceMatrix);
        const Handle handle = hitTestHandle(layout, devicePosition);
        if (handle == Handle::None)
        {
            continue;
        }

        // The handles live in the device space, the transformation is converted
        // to the page space by the matrix, which displays the selection
        bool invertible = false;
        const QTransform pageToDevice = getSelectionToDeviceMatrix(snapshotItem.pageIndex, snapshotItem.pageToDeviceMatrix);
        const QTransform deviceToPage = pageToDevice.inverted(&invertible);
        if (!invertible)
        {
            return false;
        }

        m_interaction = InteractionState();
        m_interaction.type = Interaction::Handle;
        m_interaction.pageIndex = snapshotItem.pageIndex;
        m_interaction.pageToDevice = pageToDevice;
        m_interaction.pageToDeviceBase = snapshotItem.pageToDeviceMatrix;
        m_interaction.deviceToPage = deviceToPage;
        m_interaction.startDevicePosition = devicePosition;
        m_interaction.currentDevicePosition = devicePosition;
        m_interaction.handle = handle;
        m_interaction.layout = layout;
        m_interaction.textRectangle = getTextBoxInfo().textRectangle;
        m_hoveredAnnotation = HoveredAnnotation();

        // The dragged edges of the frame snap to the geometry of the page and of the other annotations
        prepareSnapper(snapshot, snapshotItem.pageIndex, PDFObjectReference(), true);

        requestRepaint();
        return true;
    }

    return false;
}

void PDFWidgetAnnotationManager::updateHandleInteraction(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers)
{
    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.isSnapped = false;

    QPointF position(devicePosition);
    if (m_interaction.handle != Handle::Rotate)
    {
        // Jakub Melka: the user drags the handle - the corner, or the middle of the edge, of the
        // frame. It follows the cursor (the cursor need not be exactly at the handle, when the
        // dragging starts) and it snaps. Snapping is disabled by Ctrl, and it is not combined
        // with the fixed aspect ratio (Shift).
        const Handle handle = m_interaction.handle;
        const QRectF& frame = m_interaction.layout.frame;
        const bool isLeft = handle == Handle::TopLeft || handle == Handle::Left || handle == Handle::BottomLeft;
        const bool isRight = handle == Handle::TopRight || handle == Handle::Right || handle == Handle::BottomRight;
        const bool isTop = handle == Handle::TopLeft || handle == Handle::Top || handle == Handle::TopRight;
        const bool isBottom = handle == Handle::BottomLeft || handle == Handle::Bottom || handle == Handle::BottomRight;
        const QPointF handlePoint(isLeft ? frame.left() : (isRight ? frame.right() : frame.center().x()),
                                  isTop ? frame.top() : (isBottom ? frame.bottom() : frame.center().y()));
        position = handlePoint + QPointF(devicePosition - m_interaction.startDevicePosition);

        if (!modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::ShiftModifier))
        {
            m_snapper.updateSnappedPoint(position);
            if (m_snapper.isSnapped())
            {
                // The handle in the middle of an edge moves in one direction only
                const QPointF snappedPoint = m_snapper.getSnappedPoint();
                position = QPointF((isLeft || isRight) ? snappedPoint.x() : position.x(), (isTop || isBottom) ? snappedPoint.y() : position.y());
                m_interaction.isSnapped = true;
                m_interaction.snappedDevicePoint = snappedPoint;
            }
        }
    }

    m_interaction.previewTransform = computeHandleTransform(position, modifiers, &m_interaction.previewAngle);
    updateInteractionPreview();
}

void PDFWidgetAnnotationManager::updateInteractionPreview()
{
    m_interaction.previewAnnotations.clear();

    if (!m_document)
    {
        return;
    }

    PDFDocumentBuilder builder(m_document);
    std::vector<PDFObjectReference> annotations;

    if (m_interaction.type == Interaction::Point)
    {
        if (PDFAnnotationManipulator::setEditablePoints(&builder, m_interaction.pointAnnotation, m_interaction.previewPoints))
        {
            annotations.push_back(m_interaction.pointAnnotation);
        }
    }
    else if (m_interaction.type == Interaction::Handle && m_interaction.previewTransform.type() != QTransform::TxNone)
    {
        const std::vector<const PageAnnotation*> selectedAnnotations = getSelectedAnnotations(m_interaction.pageIndex, true);
        if (m_interaction.layout.isTextBox && selectedAnnotations.size() == 1)
        {
            const PDFObjectReference annotation = selectedAnnotations.front()->annotation->getSelfReference();
            if (PDFAnnotationManipulator::setFreeTextRectangle(&builder, annotation, m_interaction.previewTransform.mapRect(m_interaction.textRectangle)))
            {
                annotations.push_back(annotation);
            }
        }
        else
        {
            for (const PageAnnotation* pageAnnotation : selectedAnnotations)
            {
                // Jakub Melka: annotations, which are displayed just by their appearance stream (stamps),
                // and icons are not drawn directly - their existing appearance is transformed by the preview
                const PDFAnnotationManipulator::GeometryKind kind = PDFAnnotationManipulator::getGeometryKind(pageAnnotation->annotation->getType());
                if (kind != PDFAnnotationManipulator::GeometryKind::Points && kind != PDFAnnotationManipulator::GeometryKind::Box)
                {
                    continue;
                }

                const PDFObjectReference annotation = pageAnnotation->annotation->getSelfReference();
                if (PDFAnnotationManipulator::transformAnnotation(&builder, annotation, m_interaction.previewTransform))
                {
                    annotations.push_back(annotation);
                }
            }
        }
    }

    for (const PDFObjectReference& annotation : annotations)
    {
        if (PDFAnnotationPtr previewAnnotation = PDFAnnotation::parse(builder.getStorage(), annotation))
        {
            m_interaction.previewAnnotations[annotation] = std::move(previewAnnotation);
        }
    }
}

QTransform PDFWidgetAnnotationManager::computeHandleTransform(const QPointF& devicePosition, Qt::KeyboardModifiers modifiers, qreal* angle) const
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
        const QPointF currentVector = devicePosition - center;

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

        if (!m_interaction.layout.isRotationArbitrary)
        {
            // The selection can be rotated by the right angle only
            degrees = std::round(degrees / 90.0) * 90.0;
        }
        else if (modifiers.testFlag(Qt::ShiftModifier))
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
        const QPointF position = devicePosition;
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

PDFWidgetAnnotationManager::PointEditInfo PDFWidgetAnnotationManager::getPointEditInfo() const
{
    PointEditInfo info;

    // Jakub Melka: points are edited only if a single annotation is selected,
    // otherwise the handles of several annotations would be mixed together
    if (!m_document || m_selection.size() != 1)
    {
        return info;
    }

    const SelectedAnnotation& item = m_selection.front();
    const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return info;
    }

    info.points = PDFAnnotationManipulator::getEditablePoints(pageAnnotation->annotation.data());
    if (info.points.isValid())
    {
        info.pageIndex = item.pageIndex;
        info.annotation = item.annotation;
    }

    return info;
}

PDFAnnotationManipulator::EditablePoints PDFWidgetAnnotationManager::getModifiablePoints(PDFObjectReference annotation) const
{
    const PDFInteger pageIndex = findAnnotationPage(annotation);
    if (pageIndex == -1)
    {
        return PDFAnnotationManipulator::EditablePoints();
    }

    const PageAnnotation* pageAnnotation = findPageAnnotation(pageIndex, annotation);
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return PDFAnnotationManipulator::EditablePoints();
    }

    return PDFAnnotationManipulator::getEditablePoints(pageAnnotation->annotation.data());
}

int PDFWidgetAnnotationManager::hitTestPoint(const PointEditInfo& info, const QTransform& pageToDevice, const QPointF& devicePosition) const
{
    if (!info.isValid())
    {
        return -1;
    }

    const QWidget* widget = m_proxy->getWidget();
    const qreal radius = PDFWidgetUtils::scaleDPI_x(widget, 4) + PDFWidgetUtils::scaleDPI_x(widget, 3);

    int result = -1;
    qreal resultDistance = radius;
    for (size_t i = 0; i < info.points.points.size(); ++i)
    {
        const QPointF difference = pageToDevice.map(info.points.points[i]) - devicePosition;
        const qreal distance = std::hypot(difference.x(), difference.y());
        if (distance <= resultDistance)
        {
            result = int(i);
            resultDistance = distance;
        }
    }

    return result;
}

int PDFWidgetAnnotationManager::hitTestSegment(const PointEditInfo& info, const QTransform& pageToDevice, const QPointF& devicePosition, QPointF* pagePosition) const
{
    if (!info.isValid() || info.points.points.size() < 2)
    {
        return -1;
    }

    const std::vector<QPointF>& points = info.points.points;
    const size_t segmentCount = info.points.isClosed ? points.size() : points.size() - 1;

    int result = -1;
    qreal resultDistance = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 5);
    for (size_t i = 0; i < segmentCount; ++i)
    {
        const QPointF& pageStart = points[i];
        const QPointF& pageEnd = points[(i + 1) % points.size()];
        const QPointF start = pageToDevice.map(pageStart);
        const QPointF end = pageToDevice.map(pageEnd);
        const QPointF direction = end - start;
        const qreal lengthSquared = QPointF::dotProduct(direction, direction);
        if (qFuzzyIsNull(lengthSquared))
        {
            continue;
        }

        // The matrix is affine, so the parameter of the projection is the same in both spaces
        const qreal parameter = qBound(0.0, QPointF::dotProduct(devicePosition - start, direction) / lengthSquared, 1.0);
        const QPointF difference = start + direction * parameter - devicePosition;
        const qreal distance = std::hypot(difference.x(), difference.y());
        if (distance <= resultDistance)
        {
            result = int(i);
            resultDistance = distance;

            if (pagePosition)
            {
                *pagePosition = pageStart + (pageEnd - pageStart) * parameter;
            }
        }
    }

    return result;
}

bool PDFWidgetAnnotationManager::beginPartInteraction(const QPoint& devicePosition)
{
    const PageAnnotation* pageAnnotation = m_selection.size() == 1 ? findPageAnnotation(m_selection.front().pageIndex, m_selection.front().annotation) : nullptr;
    if (!m_document || !pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return false;
    }

    // The part is drawn on the page of the annotation
    const PDFInteger pageIndex = m_selection.front().pageIndex;
    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(pageIndex);
    if (!snapshotItem || m_proxy->getPageUnderPoint(devicePosition, nullptr) != pageIndex)
    {
        return false;
    }

    // Jakub Melka: a stroke is a part of the annotation, so it is drawn in its coordinate system
    // (see getSelectionToDeviceMatrix). The marked text is a text of the page.
    bool invertible = false;
    const QTransform pageToDevice = m_partEdit == PartEdit::AddStroke ? getSelectionToDeviceMatrix(pageIndex, snapshotItem->pageToDeviceMatrix) : snapshotItem->pageToDeviceMatrix;
    const QTransform deviceToPage = pageToDevice.inverted(&invertible);
    if (!invertible)
    {
        return false;
    }

    m_interaction = InteractionState();
    m_interaction.type = Interaction::Part;
    m_interaction.pageIndex = pageIndex;
    m_interaction.pageToDevice = pageToDevice;
    m_interaction.pageToDeviceBase = snapshotItem->pageToDeviceMatrix;
    m_interaction.deviceToPage = deviceToPage;
    m_interaction.startDevicePosition = devicePosition;
    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.pointAnnotation = m_selection.front().annotation;
    m_interaction.partPoints = { deviceToPage.map(QPointF(devicePosition)) };
    m_hoveredAnnotation = HoveredAnnotation();
    requestRepaint();
    return true;
}

void PDFWidgetAnnotationManager::updatePartInteraction(const QPoint& devicePosition)
{
    if (m_interaction.type != Interaction::Part || m_interaction.partPoints.empty())
    {
        return;
    }

    const QPointF pagePoint = m_interaction.deviceToPage.map(QPointF(devicePosition));

    if (m_partEdit == PartEdit::AddStroke)
    {
        // Points of the stroke follow the cursor (a point for each move by a pixel would be too many)
        const QPoint difference = devicePosition - m_interaction.currentDevicePosition;
        if (difference.manhattanLength() >= 2)
        {
            m_interaction.partPoints.push_back(pagePoint);
            m_interaction.currentDevicePosition = devicePosition;
        }
        return;
    }

    // Marked text between the start and the cursor
    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.partPoints.resize(1);
    m_interaction.partPoints.push_back(pagePoint);
    m_interaction.partShapes = getMarkedShapes(m_interaction.pageIndex, m_interaction.partPoints.front(), pagePoint);
}

bool PDFWidgetAnnotationManager::beginPointInteraction(const QPoint& devicePosition)
{
    const PointEditInfo info = getPointEditInfo();
    if (!info.isValid())
    {
        return false;
    }

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(info.pageIndex);
    if (!snapshotItem)
    {
        return false;
    }

    const QTransform pageToDevice = getSelectionToDeviceMatrix(info.pageIndex, snapshotItem->pageToDeviceMatrix);
    const int pointIndex = hitTestPoint(info, pageToDevice, devicePosition);
    if (pointIndex == -1)
    {
        return false;
    }

    bool invertible = false;
    const QTransform deviceToPage = pageToDevice.inverted(&invertible);
    if (!invertible)
    {
        return false;
    }

    m_interaction = InteractionState();
    m_interaction.type = Interaction::Point;
    m_interaction.pageIndex = info.pageIndex;
    m_interaction.pageToDevice = pageToDevice;
    m_interaction.pageToDeviceBase = snapshotItem->pageToDeviceMatrix;
    m_interaction.deviceToPage = deviceToPage;
    m_interaction.startDevicePosition = devicePosition;
    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.pointIndex = pointIndex;
    m_interaction.pointAnnotation = info.annotation;
    m_interaction.previewPoints = info.points.points;
    m_interaction.isPreviewClosed = info.points.isClosed;
    m_interaction.isPreviewQuadEnds = info.points.isQuadEnds;
    m_interaction.previewStrokeSizes = info.points.strokeSizes;
    m_hoveredAnnotation = HoveredAnnotation();
    m_activePoint = pointIndex;

    // Jakub Melka: the dragged point snaps to the geometry of the page and of the
    // other annotations. The edited annotation is excluded, the point would snap to itself.
    m_snapper.clear();
    if (!info.points.isQuadEnds)
    {
        prepareSnapper(snapshot, info.pageIndex, info.annotation, false);
    }

    requestRepaint();
    return true;
}

void PDFWidgetAnnotationManager::updatePointInteraction(const QPoint& devicePosition, Qt::KeyboardModifiers modifiers)
{
    std::vector<QPointF>& points = m_interaction.previewPoints;
    if (m_interaction.type != Interaction::Point || m_interaction.pointIndex < 0 || m_interaction.pointIndex >= int(points.size()))
    {
        return;
    }

    m_interaction.currentDevicePosition = devicePosition;
    m_interaction.isSnapped = false;
    QPointF position = m_interaction.deviceToPage.map(QPointF(devicePosition));

    // Snapping is disabled by Ctrl. It is not combined with the constrained direction.
    if (!modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::ShiftModifier))
    {
        m_snapper.updateSnappedPoint(QPointF(devicePosition));
        if (m_snapper.isSnapped())
        {
            m_interaction.isSnapped = true;
            position = m_interaction.deviceToPage.map(m_snapper.getSnappedPoint());
        }
    }

    if (modifiers.testFlag(Qt::ShiftModifier) && points.size() >= 2 && !m_interaction.isPreviewQuadEnds)
    {
        // Constrain the direction from the neighbouring point to the multiples of 45 degrees.
        // The page is displayed rotated by a multiple of 90 degrees, so the angles are the
        // same on the screen.
        const size_t pointIndex = size_t(m_interaction.pointIndex);
        const size_t neighbourIndex = pointIndex > 0 ? pointIndex - 1 : (m_interaction.isPreviewClosed ? points.size() - 1 : 1);
        const QPointF neighbour = points[neighbourIndex];
        const QPointF vector = position - neighbour;

        if (!qFuzzyIsNull(vector.x()) || !qFuzzyIsNull(vector.y()))
        {
            const qreal step = M_PI / 4.0;
            const qreal angle = std::round(std::atan2(vector.y(), vector.x()) / step) * step;
            const QPointF direction(std::cos(angle), std::sin(angle));
            position = neighbour + direction * QPointF::dotProduct(vector, direction);
        }
    }

    points[size_t(m_interaction.pointIndex)] = position;
    updateInteractionPreview();
}

PDFWidgetAnnotationManager::PointMenuContext PDFWidgetAnnotationManager::getPointMenuContext(const QPoint& devicePosition) const
{
    PointMenuContext context;

    const PointEditInfo info = getPointEditInfo();
    if (!info.isValid())
    {
        return context;
    }

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* snapshotItem = snapshot.getPageSnapshot(info.pageIndex);
    if (!snapshotItem)
    {
        return context;
    }

    // Jakub Melka: the context is valid also for a point, which cannot be removed -
    // the point of the selected annotation has precedence over other annotations
    const QTransform pageToDevice = getSelectionToDeviceMatrix(info.pageIndex, snapshotItem->pageToDeviceMatrix);
    const int pointIndex = hitTestPoint(info, pageToDevice, devicePosition);
    if (pointIndex != -1)
    {
        context.annotation = info.annotation;
        if (info.points.canRemovePoint(size_t(pointIndex)))
        {
            context.removedPoint = pointIndex;
        }
        return context;
    }

    if (info.points.canInsertPoint())
    {
        QPointF pagePosition;
        const int segmentIndex = hitTestSegment(info, pageToDevice, devicePosition, &pagePosition);
        if (segmentIndex != -1)
        {
            context.annotation = info.annotation;
            context.insertSegment = segmentIndex;
            context.insertPosition = pagePosition;
        }
    }

    return context;
}

bool PDFWidgetAnnotationManager::setAnnotationPoints(PDFObjectReference annotation, const std::vector<QPointF>& points)
{
    if (!m_document || !isModificationAllowed())
    {
        return false;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    if (!PDFAnnotationManipulator::setEditablePoints(modifier.getBuilder(), annotation, points))
    {
        return false;
    }

    keepDisplayedPosition(modifier.getBuilder(), annotation);

    if (!modifier.finalize())
    {
        return false;
    }

    Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool PDFWidgetAnnotationManager::moveAnnotationPoint(PDFObjectReference annotation, size_t pointIndex, const QPointF& pagePoint)
{
    PDFAnnotationManipulator::EditablePoints points = getModifiablePoints(annotation);
    if (!points.isValid() || pointIndex >= points.points.size())
    {
        return false;
    }

    points.points[pointIndex] = pagePoint;
    return setAnnotationPoints(annotation, points.points);
}

bool PDFWidgetAnnotationManager::insertAnnotationPoint(PDFObjectReference annotation, size_t segmentIndex, const QPointF& pagePoint)
{
    PDFAnnotationManipulator::EditablePoints points = getModifiablePoints(annotation);
    if (!points.canInsertPoint())
    {
        return false;
    }

    const size_t segmentCount = points.isClosed ? points.points.size() : points.points.size() - 1;
    if (segmentIndex >= segmentCount)
    {
        return false;
    }

    points.points.insert(std::next(points.points.begin(), segmentIndex + 1), pagePoint);
    return setAnnotationPoints(annotation, points.points);
}

bool PDFWidgetAnnotationManager::removeAnnotationPoint(PDFObjectReference annotation, size_t pointIndex)
{
    PDFAnnotationManipulator::EditablePoints points = getModifiablePoints(annotation);
    if (!points.canRemovePoint(pointIndex))
    {
        return false;
    }

    points.points.erase(std::next(points.points.begin(), pointIndex));
    return setAnnotationPoints(annotation, points.points);
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

            // The rubber band is compared with the annotations, as they are displayed
            const QRectF rubberBand = QRectF(QPointF(interaction.startDevicePosition), QPointF(interaction.currentDevicePosition)).normalized();

            std::vector<PDFObjectReference> selection = interaction.isAdditive ? getSelectedAnnotations() : std::vector<PDFObjectReference>();
            const PageAnnotations& pageAnnotations = getPageAnnotations(interaction.pageIndex);
            for (const PageAnnotation& pageAnnotation : pageAnnotations.annotations)
            {
                if (!isAnnotationSelectable(pageAnnotation))
                {
                    continue;
                }

                const PDFObjectReference annotation = pageAnnotation.annotation->getSelfReference();
                const QTransform matrix = getAnnotationToDeviceMatrix(pageAnnotation, interaction.pageIndex, interaction.pageToDeviceBase);
                const QRectF rectangle = matrix.mapRect(pageAnnotation.annotation->getRectangle().normalized()).normalized();
                if (rubberBand.contains(rectangle) && std::find(selection.cbegin(), selection.cend(), annotation) == selection.cend())
                {
                    selection.push_back(annotation);
                }
            }

            setSelectedAnnotations(selection);
            break;
        }

        case Interaction::Point:
        {
            if (interaction.currentDevicePosition != interaction.startDevicePosition && !interaction.previewPoints.empty())
            {
                setAnnotationPoints(interaction.pointAnnotation, interaction.previewPoints);
            }
            break;
        }

        case Interaction::Part:
        {
            // The part has been drawn, the edit is finished (also if nothing has been drawn)
            const PartEdit partEdit = m_partEdit;
            m_partEdit = PartEdit::None;

            if (partEdit == PartEdit::AddStroke)
            {
                if (interaction.partPoints.size() >= 2)
                {
                    addAnnotationParts(interaction.pointAnnotation, { QPolygonF(QList<QPointF>(interaction.partPoints.cbegin(), interaction.partPoints.cend())) });
                }
            }
            else if (partEdit == PartEdit::AddMarkedAreas)
            {
                addAnnotationParts(interaction.pointAnnotation, interaction.partShapes);
            }
            else if (!interaction.partShapes.empty())
            {
                setAnnotationParts(interaction.pointAnnotation, interaction.partShapes);
            }
            break;
        }

        case Interaction::Handle:
        {
            if (interaction.previewTransform.type() == QTransform::TxNone)
            {
                break;
            }

            QTransform transform = interaction.previewTransform;
            const std::vector<const PageAnnotation*> annotations = getSelectedAnnotations(interaction.pageIndex, true);
            if (interaction.layout.isTextBox && annotations.size() == 1)
            {
                // Only the text box is resized, the tip of the callout line stays
                setAnnotationTextRectangle(annotations.front()->annotation->getSelfReference(), transform.mapRect(interaction.textRectangle));
                break;
            }

            if (annotations.size() == 1 && interaction.pageToDevice != interaction.pageToDeviceBase)
            {
                // Jakub Melka: the annotation is not displayed by the matrix of the page (flags
                // NoRotate, NoZoom) - it is anchored at its top left corner, which is displayed by
                // the matrix of the page. The anchor of the transformed annotation must be at the
                // place, where the preview displayed it, otherwise the annotation would jump.
                const QRectF rectangle = annotations.front()->annotation->getRectangle().normalized();
                const QPointF anchor = transform.map(QPointF(rectangle.left(), rectangle.bottom()));
                const QPointF displayedAnchor = interaction.pageToDeviceBase.inverted().map(interaction.pageToDevice.map(anchor));
                transform = transform * QTransform::fromTranslate(displayedAnchor.x() - anchor.x(), displayedAnchor.y() - anchor.y());
            }

            transformSelectedAnnotations(interaction.pageIndex, transform);
            break;
        }

        default:
            break;
    }

    requestRepaint();
}

void PDFWidgetAnnotationManager::cancelInteraction()
{
    m_partEdit = PartEdit::None;
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

void PDFWidgetAnnotationManager::transformSelectedAnnotations(const std::function<QTransform(const QRectF&, PDFInteger)>& transformFactory)
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

        const QTransform transform = transformFactory(boundingRectangle, pageIndex);
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

    transformSelectedAnnotations([offset](const QRectF&, PDFInteger) { return QTransform::fromTranslate(offset.x(), offset.y()); });
}

void PDFWidgetAnnotationManager::nudgeSelectedAnnotations(const QPointF& deviceDirection, PDFReal distance)
{
    // Jakub Melka: the pages can be rotated differently (the rotation is a property of
    // the page), so the direction on the screen is converted for each page separately.
    // All pages are modified in a single step, so a single undo reverts the whole move.
    transformSelectedAnnotations([this, deviceDirection, distance](const QRectF&, PDFInteger pageIndex)
    {
        bool invertible = false;
        const QTransform deviceToPage = getPageToDeviceMatrix(pageIndex).inverted(&invertible);
        const QPointF pageDirection = deviceToPage.map(deviceDirection) - deviceToPage.map(QPointF(0.0, 0.0));
        const qreal length = std::hypot(pageDirection.x(), pageDirection.y());
        if (!invertible || qFuzzyIsNull(length))
        {
            return QTransform();
        }

        const QPointF offset = pageDirection * (distance / length);
        return QTransform::fromTranslate(offset.x(), offset.y());
    });
}

void PDFWidgetAnnotationManager::rotateSelectedAnnotations(qreal angleDegrees)
{
    // Jakub Melka: the page space has the y axis pointing upwards, so the rotation
    // by a positive angle is counterclockwise there. Displayed rotation of the page
    // does not matter - it is a rotation itself, and rotations commute.
    transformSelectedAnnotations([angleDegrees](const QRectF& boundingRectangle, PDFInteger)
    {
        const QPointF center = boundingRectangle.center();
        return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-angleDegrees) * QTransform::fromTranslate(center.x(), center.y());
    });
}

void PDFWidgetAnnotationManager::flipSelectedAnnotations(Qt::Orientation orientation)
{
    transformSelectedAnnotations([this, orientation](const QRectF& boundingRectangle, PDFInteger pageIndex)
    {
        // Jakub Melka: the orientation is given on the screen, so the mirroring is done
        // in the device space and converted to the page space. The matrix of the page
        // contains the rotation of the page and also the rotation of the view, so the
        // axis of the mirroring is right, however the page is displayed.
        bool invertible = false;
        const QTransform pageToDevice = getPageToDeviceMatrix(pageIndex);
        const QTransform deviceToPage = pageToDevice.inverted(&invertible);
        if (!invertible)
        {
            return QTransform();
        }

        const QPointF center = pageToDevice.map(boundingRectangle.center());
        const bool mirrorX = orientation == Qt::Horizontal;
        const QTransform deviceTransform = QTransform::fromTranslate(-center.x(), -center.y()) *
                                           QTransform::fromScale(mirrorX ? -1.0 : 1.0, mirrorX ? 1.0 : -1.0) *
                                           QTransform::fromTranslate(center.x(), center.y());
        return pageToDevice * deviceTransform * deviceToPage;
    });
}

void PDFWidgetAnnotationManager::copySelectedAnnotations()
{
    copyAnnotationsToClipboard(getSelectedAnnotations());
}

bool PDFWidgetAnnotationManager::copyAnnotationsToClipboard(const std::vector<PDFObjectReference>& annotations)
{
    if (!m_document || annotations.empty())
    {
        return false;
    }

    // Whole comment threads are serialized (with the replies and the popup windows)
    const QByteArray data = PDFAnnotationManipulator::serializeAnnotations(m_document, annotations);
    if (data.isEmpty())
    {
        return false;
    }

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(PDFAnnotationManipulator::getMimeType(), data);

    // Texts of the annotations are offered to the other applications
    QStringList texts;
    for (const PDFObjectReference& annotation : annotations)
    {
        const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&m_document->getStorage(), annotation);
        const QString contents = parsedAnnotation ? parsedAnnotation->getContents().trimmed() : QString();
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
    return true;
}

std::vector<PDFObjectReference> PDFWidgetAnnotationManager::getDeletableSelectedAnnotations() const
{
    std::vector<PDFObjectReference> result;

    for (const SelectedAnnotation& item : m_selection)
    {
        const PageAnnotation* pageAnnotation = findPageAnnotation(item.pageIndex, item.annotation);
        if (pageAnnotation && canDeleteAnnotation(*pageAnnotation))
        {
            result.push_back(item.annotation);
        }
    }

    return result;
}

void PDFWidgetAnnotationManager::cutSelectedAnnotations()
{
    if (!m_document || !hasSelection() || !isModificationAllowed())
    {
        return;
    }

    // Jakub Melka: only the annotations, which are going to be deleted, are copied -
    // otherwise pasting would duplicate the annotations, which cannot be deleted (locked
    // ones). And nothing is deleted, until the annotations are safely in the clipboard.
    if (copyAnnotationsToClipboard(getDeletableSelectedAnnotations()))
    {
        deleteSelectedAnnotations();
    }
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

void PDFWidgetAnnotationManager::pasteAnnotations(std::optional<QPoint> widgetPosition, bool keepPosition)
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
    if (isPositionValid && !keepPosition)
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

    // Jakub Melka: if the text box of a free text annotation with a callout line is dragged,
    // then only the text box is moved - the callout line still points to the same place. The
    // whole annotation is dragged by its callout line (by the rest of its rectangle), or with Alt.
    const TextBoxInfo textBoxInfo = getTextBoxInfo();
    if (textBoxInfo.isValid() && textBoxInfo.pageIndex == pageIndex && !event->modifiers().testFlag(Qt::AltModifier))
    {
        const QTransform matrix = getSelectionToDeviceMatrix(pageIndex, snapshotItem->pageToDeviceMatrix);
        m_dragState.isTextBoxOnly = matrix.mapRect(textBoxInfo.textRectangle).contains(QPointF(event->pos()));
    }

    const QPointF pagePoint = deviceToPage.map(QPointF(event->pos()));
    m_dragState.cursorOffset = pagePoint - m_dragState.boundingRectangle.topLeft();
    m_dragState.dragPixmap = createAnnotationDragPixmap(snapshotItem->pageToDeviceMatrix, m_dragState.dragHotSpot);
    m_dragState.isActive = true;
    return true;
}

PDFAnnotationPtr PDFWidgetAnnotationManager::getInteractionPreview(PDFObjectReference annotation) const
{
    auto it = m_interaction.previewAnnotations.find(annotation);
    return it != m_interaction.previewAnnotations.cend() ? it->second : PDFAnnotationPtr();
}

QMimeData* PDFWidgetAnnotationManager::createAnnotationDragData() const
{
    if (!m_dragState.isActive)
    {
        return nullptr;
    }

    PDFAnnotationDragDataHelper::Payload payload;
    payload.sourceId = m_dragSourceId;
    payload.pageReference = m_dragState.pageReference;
    payload.annotations = m_dragState.annotations;
    payload.rectangles = m_dragState.rectangles;
    payload.boundingRectangle = m_dragState.boundingRectangle;
    payload.cursorOffset = m_dragState.cursorOffset;
    payload.isTextBoxOnly = m_dragState.isTextBoxOnly;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(PDFAnnotationDragDataHelper::getMimeType(), PDFAnnotationDragDataHelper::serialize(payload));

    // Serialized annotations allow the drop into another document
    const QByteArray serializedAnnotations = PDFAnnotationManipulator::serializeAnnotations(m_document, m_dragState.annotations);
    if (!serializedAnnotations.isEmpty())
    {
        mimeData->setData(PDFAnnotationManipulator::getMimeType(), serializedAnnotations);
    }

    return mimeData;
}

void PDFWidgetAnnotationManager::startAnnotationDrag(QMouseEvent* event)
{
    QMimeData* mimeData = event ? createAnnotationDragData() : nullptr;
    if (!mimeData)
    {
        return;
    }

    QDrag* drag = new QDrag(m_proxy->getWidget()->getDrawWidget()->getWidget());
    drag->setMimeData(mimeData);
    if (!m_dragState.dragPixmap.isNull())
    {
        drag->setPixmap(m_dragState.dragPixmap);
        drag->setHotSpot(m_dragState.dragHotSpot);
    }

    // The annotation is dragged, so the click, which would remove it from the selection, is cancelled
    m_pendingDeselection = PDFObjectReference();

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
    painter.setBrush(QColor(90, 160, 255, 40));
    painter.drawRect(deviceRectF);

    // Jakub Melka: the dragged annotations are drawn, as they look like,
    // so the user sees, what is dragged (not just anonymous rectangles)
    painter.save();
    painter.resetTransform();
    const QTransform pageToPixmapMatrix = pagePointToDevicePointMatrix * QTransform::fromTranslate(-deviceRect.left(), -deviceRect.top());
    for (const PDFObjectReference& annotation : m_dragState.annotations)
    {
        if (const PageAnnotation* pageAnnotation = findPageAnnotation(m_dragState.pageIndex, annotation))
        {
            drawAnnotationPreview(&painter, *pageAnnotation, m_dragState.pageIndex, pageToPixmapMatrix);
        }
    }
    painter.restore();

    // Rectangles of the dragged annotations
    painter.setPen(QPen(QColor(40, 110, 220, 180), 1.0));
    painter.setBrush(Qt::NoBrush);
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
    pdf::PDFSelectPagesDialog dialog(tr("Copy Annotations"), tr("Copy Annotations onto Multiple Pages"),
                                     m_document->getCatalog()->getPageCount(), m_proxy->getWidget()->getDrawWidget()->getCurrentPages(), m_proxy->getWidget());
    if (dialog.exec() == QDialog::Accepted)
    {
        // The dialog returns page numbers
        std::vector<PDFInteger> pages = dialog.getSelectedPages();
        for (PDFInteger& pageIndex : pages)
        {
            --pageIndex;
        }

        copySelectedAnnotationsToPages(pages);
    }
}

void PDFWidgetAnnotationManager::copySelectedAnnotationsToPages(const std::vector<PDFInteger>& pageIndices)
{
    if (!m_document || !hasSelection() || !isModificationAllowed())
    {
        return;
    }

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();

    // Jakub Melka: the same function, which is used by the clipboard and by the drag and
    // drop, so the copies have their popup windows, replies and unique names
    bool isModified = false;
    const PDFInteger pageCount = PDFInteger(m_document->getCatalog()->getPageCount());
    for (const SelectedAnnotation& item : m_selection)
    {
        for (const PDFInteger pageIndex : pageIndices)
        {
            if (pageIndex < 0 || pageIndex >= pageCount || pageIndex == item.pageIndex)
            {
                continue;
            }

            const PDFObjectReference pageReference = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
            isModified = PDFAnnotationManipulator::copyAnnotation(modifier.getBuilder(), item.annotation, pageReference).isValid() || isModified;
        }
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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
            // Jakub Melka: if the rectangle was edited, then it must not be just overwritten - the
            // geometry of the annotation (points, callout line, ...) would stay at the old place.
            // So the object is stored with the old rectangle and the annotation is transformed.
            PDFDocumentDataLoaderDecorator loader(m_document);
            const PDFDictionary* originalDictionary = m_document->getDictionaryFromObject(originalObject);
            const PDFDictionary* editedDictionary = m_document->getDictionaryFromObject(object);
            const QRectF oldRectangle = originalDictionary ? loader.readRectangle(originalDictionary->get("Rect"), QRectF()).normalized() : QRectF();
            const QRectF newRectangle = editedDictionary ? loader.readRectangle(editedDictionary->get("Rect"), QRectF()).normalized() : QRectF();
            const bool isRectangleChanged = oldRectangle.isValid() && newRectangle.isValid() && oldRectangle != newRectangle;

            if (isRectangleChanged)
            {
                PDFDictionary dictionary = *editedDictionary;
                dictionary.setEntry(PDFInplaceOrMemoryString("Rect"), PDFObject(originalDictionary->get("Rect")));
                object = PDFObject::createDictionary(PDFDictionary(std::move(dictionary)));
            }

            PDFDocumentModifier modifier(m_document);
            modifier.markAnnotationsChanged();
            modifier.getBuilder()->setObject(m_editableAnnotation, object);
            modifier.getBuilder()->updateAnnotationAppearanceStreams(m_editableAnnotation);

            if (isRectangleChanged)
            {
                PDFAnnotationManipulator::setRectangle(modifier.getBuilder(), m_editableAnnotation, newRectangle);
            }

            if (modifier.finalize())
            {
                Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
            }
        }
    }
}

void PDFWidgetAnnotationManager::onEditGeometry()
{
    const PDFInteger pageIndex = findAnnotationPage(m_editableAnnotation);
    const PageAnnotation* pageAnnotation = pageIndex != -1 ? findPageAnnotation(pageIndex, m_editableAnnotation) : nullptr;
    if (!pageAnnotation || !canTransformAnnotation(*pageAnnotation))
    {
        return;
    }

    const PDFObjectReference annotation = m_editableAnnotation;
    const QRectF rectangle = pageAnnotation->annotation->getRectangle().normalized();
    const PDFAnnotationManipulator::Capabilities capabilities = getAnnotationCapabilities(*pageAnnotation);
    const PDFAnnotationManipulator::EditablePoints points = PDFAnnotationManipulator::getEditablePoints(pageAnnotation->annotation.data());

    PDFAnnotationGeometryDialog dialog(rectangle, points, capabilities, m_proxy->getWidget());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    // All changes are a single modification of the document. The page annotation
    // is not valid any more, when the document is changed.
    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();
    PDFDocumentBuilder* builder = modifier.getBuilder();

    bool isModified = false;
    if (dialog.isPointsChanged())
    {
        isModified = PDFAnnotationManipulator::setEditablePoints(builder, annotation, dialog.getPoints());
    }
    else if (dialog.isRectangleChanged())
    {
        isModified = PDFAnnotationManipulator::setRectangle(builder, annotation, dialog.getRectangle());
    }

    if (!qFuzzyIsNull(dialog.getRotation()))
    {
        // The annotation is rotated around the reference point of its (new) rectangle. Positive angle
        // is clockwise on the screen, which is a negative angle in the page coordinate system.
        const PDFDictionary* dictionary = builder->getStorage()->getDictionaryFromObject(builder->getStorage()->getObject(annotation));
        PDFDocumentDataLoaderDecorator loader(builder->getStorage());
        const QPointF center = dialog.getReferencePoint(loader.readRectangle(dictionary->get("Rect"), rectangle));
        const QTransform rotation = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-dialog.getRotation()) * QTransform::fromTranslate(center.x(), center.y());
        isModified = PDFAnnotationManipulator::transformAnnotation(builder, annotation, rotation) || isModified;
    }

    if (isModified && modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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

    if (isModificationAllowed())
    {
        // Jakub Melka: the reply is written directly in the popup window. The document is changed
        // by the reply, so the popup window is closed (the annotations displayed by it are not valid).
        const PDFObjectReference annotationReference = pageAnnotation.annotation->getSelfReference();

        QTextEdit* replyEdit = new QTextEdit(frameWidget);
        replyEdit->setObjectName("replyEdit");
        replyEdit->setAcceptRichText(false);
        replyEdit->setPlaceholderText(tr("Write a reply..."));
        replyEdit->setFixedSize(PDFWidgetUtils::scaleDPI(replyEdit, QSize(270, 60)));
        frameLayout->addWidget(replyEdit);

        QPushButton* replyButton = new QPushButton(tr("Reply"), frameWidget);
        replyButton->setObjectName("replyButton");
        replyButton->setEnabled(false);
        frameLayout->addWidget(replyButton);

        connect(replyEdit, &QTextEdit::textChanged, replyButton, [replyEdit, replyButton]() { replyButton->setEnabled(!replyEdit->toPlainText().trimmed().isEmpty()); });
        connect(replyButton, &QPushButton::clicked, this, [this, parentWidget, replyEdit, annotationReference]()
        {
            const QString contents = replyEdit->toPlainText();
            parentWidget->close();
            addAnnotationReply(annotationReference, contents);
        });
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

    if (m_dropFeedback.isActive)
    {
        // Jakub Melka: feedback of the drag and drop operation - the place, to which the dragged
        // annotations snap, and the scope of the operation (annotations of one page are dragged)
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (m_dropFeedback.isSnapped)
        {
            const qreal radius = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 4);
            painter->setPen(QPen(convertor.convert(QColor(0, 160, 0), false, true), 2.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(m_dropFeedback.snappedDevicePoint, radius + 5.0, radius + 5.0);
        }

        const QString scopeText = m_dragState.isActive ? getInteractionScopeText(m_dragState.pageIndex) : QString();
        if (!scopeText.isEmpty())
        {
            const QPointF textPosition = QPointF(m_dropFeedback.devicePosition) + QPointF(20.0, 16.0);
            const QRectF textRectangle = QRectF(painter->fontMetrics().boundingRect(scopeText)).adjusted(-4.0, -2.0, 4.0, 2.0).translated(textPosition);
            painter->setPen(Qt::NoPen);
            painter->setBrush(convertor.convert(QColor(255, 255, 255, 220), false, false));
            painter->drawRoundedRect(textRectangle, 3.0, 3.0);
            painter->setPen(convertor.convert(QColor(Qt::black), false, true));
            painter->drawText(textRectangle, Qt::AlignCenter, scopeText);
        }

        painter->restore();
    }
}

void PDFWidgetAnnotationManager::drawInfoText(QPainter* painter, const QString& text, const PDFColorConvertor& convertor) const
{
    const QPointF textPosition = QPointF(m_interaction.currentDevicePosition) + QPointF(16.0, 16.0);
    const QRectF textRectangle = painter->fontMetrics().boundingRect(text).adjusted(-4.0, -2.0, 4.0, 2.0).translated(textPosition.x() + 4.0, textPosition.y());

    painter->setPen(Qt::NoPen);
    painter->setBrush(convertor.convert(QColor(255, 255, 255, 220), false, false));
    painter->drawRoundedRect(textRectangle, 3.0, 3.0);
    painter->setPen(convertor.convert(QColor(Qt::black), false, true));
    painter->drawText(textRectangle, Qt::AlignCenter, text);
}

bool PDFWidgetAnnotationManager::drawInteractionPreview(QPainter* painter, PDFObjectReference annotation, PDFInteger pageIndex, const QTransform& annotationToDevice) const
{
    auto it = m_interaction.previewAnnotations.find(annotation);
    if (it == m_interaction.previewAnnotations.cend())
    {
        return false;
    }

    PageAnnotation previewAnnotation;
    previewAnnotation.annotation = it->second;
    drawAnnotationPreview(painter, previewAnnotation, pageIndex, annotationToDevice, true);
    return true;
}

void PDFWidgetAnnotationManager::drawAnnotationPreview(QPainter* painter, const PageAnnotation& annotation, PDFInteger pageIndex, const QTransform& annotationToDevice, bool isDrawnDirectly) const
{
    if (!m_features.testFlag(PDFRenderer::DisplayAnnotations))
    {
        return;
    }

    // Jakub Melka: the annotation is drawn the same way, as the page draws it, just by another
    // matrix and half transparent. It is a preview, so the errors are not reported.
    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
    QList<PDFRenderError> errors;

    int fontCacheLock = 0;
    m_fontCache->setCacheShrinkEnabled(&fontCacheLock, false);

    painter->save();
    painter->setOpacity(0.5);
    drawAnnotation(annotation, annotationToDevice, page, cms.data(), isDrawnDirectly, errors, painter);
    painter->restore();

    m_fontCache->setCacheShrinkEnabled(&fontCacheLock, true);
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
            const QTransform matrix = getAnnotationToDeviceMatrix(*pageAnnotation, pageIndex, pagePointToDevicePointMatrix);
            painter->setPen(dashedPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(matrix.map(QPolygonF(pageAnnotation->annotation->getRectangle().normalized())));
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

        // The annotation can be displayed by another matrix, than the matrix of the page
        const QTransform matrix = getAnnotationToDeviceMatrix(*pageAnnotation, pageIndex, pagePointToDevicePointMatrix);
        if (hasHandlePreview && m_interaction.layout.isTextBox)
        {
            // Only the text box is resized (the preview shows the new layout of the text and the new callout line)
            drawInteractionPreview(painter, pageAnnotation->annotation->getSelfReference(), pageIndex, matrix);

            painter->setPen(solidPen);
            painter->setBrush(QBrush(fillColor));
            painter->drawPolygon(matrix.map(m_interaction.previewTransform.map(QPolygonF(m_interaction.textRectangle))));
        }
        else if (hasHandlePreview && canTransformAnnotation(*pageAnnotation))
        {
            // Jakub Melka: the preview shows the annotation, as it will look like - it is the result
            // of the real operation (with the new measured value, ...). Annotations displayed just by
            // their appearance stream are displayed by the transformed appearance stream. An annotation,
            // which does not support the transformation, is just moved, so its preview is moved too.
            const AnnotationType type = pageAnnotation->annotation->getType();
            if (!drawInteractionPreview(painter, pageAnnotation->annotation->getSelfReference(), pageIndex, matrix))
            {
                const QTransform effectiveTransform = PDFAnnotationManipulator::getEffectiveTransform(type, rectangle, m_interaction.previewTransform);
                drawAnnotationPreview(painter, *pageAnnotation, pageIndex, effectiveTransform * pagePointToDevicePointMatrix);
            }

            painter->setPen(solidPen);
            painter->setBrush(QBrush(fillColor));
            painter->drawPolygon(matrix.map(PDFAnnotationManipulator::getTransformedOutline(type, rectangle, m_interaction.previewTransform)));
        }
        else
        {
            painter->drawPolygon(matrix.map(QPolygonF(rectangle)));
        }
    }

    // Selection frame with the handles
    if (!getSelectedAnnotations(pageIndex, true).empty())
    {
        if (hasHandlePreview)
        {
            // The frame is transformed in the device space, where the handles live
            const QTransform deviceTransform = m_interaction.deviceToPage * m_interaction.previewTransform * m_interaction.pageToDevice;
            painter->setPen(dashedPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(deviceTransform.map(QPolygonF(m_interaction.layout.frame)));

            if (m_interaction.isSnapped)
            {
                // Target of the snapping
                const qreal radius = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 4);
                painter->setPen(QPen(convertor.convert(QColor(0, 160, 0), false, true), 2.0));
                painter->drawEllipse(m_interaction.snappedDevicePoint, radius + 5.0, radius + 5.0);
            }

            // Numeric feedback - the angle of the rotation, or the new size (in the page units),
            // and the scope of the operation, if it does not change the whole selection
            QString text;
            if (m_interaction.handle == Handle::Rotate)
            {
                text = QString::fromUtf8("%1°").arg(QLocale::system().toString(m_interaction.previewAngle, 'f', 1));
            }
            else
            {
                const QRectF newFrame = m_interaction.deviceToPage.mapRect(deviceTransform.mapRect(m_interaction.layout.frame)).normalized();
                text = QString::fromUtf8("%1 × %2 pt").arg(QLocale::system().toString(newFrame.width(), 'f', 1), QLocale::system().toString(newFrame.height(), 'f', 1));
            }

            const QString scopeText = getInteractionScopeText(pageIndex);
            drawInfoText(painter, scopeText.isEmpty() ? text : QString("%1 | %2").arg(text, scopeText), convertor);
        }
        else if (m_interaction.type == Interaction::None && !m_dragState.isDragging)
        {
            // Jakub Melka: only the handles, which really do something with the
            // selection, are displayed (a sticky note has none, it can only be moved)
            const HandleLayout layout = computeHandleLayout(pageIndex, pagePointToDevicePointMatrix);
            if (layout.isValid)
            {
                painter->setPen(dashedPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(layout.frame);

                if (layout.hasRotationHandle)
                {
                    painter->drawLine(layout.rotationHandleBase, layout.rotationHandle);
                }

                painter->setPen(solidPen);
                painter->setBrush(convertor.convert(QColor(Qt::white), false, false));

                if (layout.hasResizeHandles)
                {
                    const qreal halfSize = layout.handleSize * 0.5;
                    for (const QPointF& handle : layout.handles)
                    {
                        painter->drawRect(QRectF(handle.x() - halfSize, handle.y() - halfSize, layout.handleSize, layout.handleSize));
                    }
                }

                if (layout.hasRotationHandle)
                {
                    painter->drawEllipse(layout.rotationHandle, layout.rotationHandleRadius, layout.rotationHandleRadius);
                }
            }
        }
    }

    // Points of the selected annotation, which can be edited one by one
    const bool hasPointPreview = m_interaction.type == Interaction::Point && m_interaction.pageIndex == pageIndex;
    const PointEditInfo pointInfo = getPointEditInfo();
    if (pointInfo.isValid() && pointInfo.pageIndex == pageIndex &&
        (hasPointPreview || (m_interaction.type == Interaction::None && !m_dragState.isDragging)))
    {
        const std::vector<QPointF>& points = hasPointPreview ? m_interaction.previewPoints : pointInfo.points.points;
        const QTransform pointToDeviceMatrix = hasPointPreview ? m_interaction.pageToDevice : getSelectionToDeviceMatrix(pageIndex, pagePointToDevicePointMatrix);

        QPolygonF devicePoints;
        devicePoints.reserve(qsizetype(points.size()));
        for (const QPointF& point : points)
        {
            devicePoints << pointToDeviceMatrix.map(point);
        }

        // Jakub Melka: handles of the points are red, so they can be distinguished
        // at first sight from the blue handles of the selection frame
        const QColor pointColor = convertor.convert(QColor(220, 0, 0), false, true);

        if (hasPointPreview)
        {
            // Preview of the annotation with the dragged point, as it will look like
            drawInteractionPreview(painter, m_interaction.pointAnnotation, pageIndex, pointToDeviceMatrix);

            // Shape with the dragged point
            QPen previewPen(pointColor, 1.0);
            previewPen.setCosmetic(true);
            previewPen.setStyle(Qt::DashLine);
            painter->setPen(previewPen);
            painter->setBrush(Qt::NoBrush);
            if (m_interaction.isPreviewQuadEnds)
            {
                // Each marked line has its start and its end
                for (qsizetype i = 1; i < devicePoints.size(); i += 2)
                {
                    painter->drawLine(devicePoints[i - 1], devicePoints[i]);
                }
            }
            else if (!m_interaction.previewStrokeSizes.empty())
            {
                // Strokes of an ink
                qsizetype start = 0;
                for (const size_t strokeSize : m_interaction.previewStrokeSizes)
                {
                    painter->drawPolyline(devicePoints.mid(start, qsizetype(strokeSize)));
                    start += qsizetype(strokeSize);
                }
            }
            else if (m_interaction.isPreviewClosed)
            {
                painter->drawPolygon(devicePoints);
            }
            else
            {
                painter->drawPolyline(devicePoints);
            }
        }

        const QColor handleColor = convertor.convert(QColor(Qt::white), false, false);
        const qreal radius = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 4);
        const int activePoint = hasPointPreview ? m_interaction.pointIndex : m_hoveredPoint;
        // Jakub Melka: a text markup of a long text has a lot of marked lines. All their handles
        // would cover the annotation, so only the handles near to the cursor are displayed.
        constexpr qsizetype CROWDED_POINT_COUNT = 64;
        const bool isCrowded = devicePoints.size() > CROWDED_POINT_COUNT;
        const qreal visibleDistance = PDFWidgetUtils::scaleDPI_x(m_proxy->getWidget(), 80);

        for (qsizetype i = 0; i < devicePoints.size(); ++i)
        {
            const bool isActive = int(i) == activePoint;
            if (isCrowded && !isActive && int(i) != m_activePoint && QLineF(devicePoints[i], QPointF(m_lastMousePosition)).length() > visibleDistance)
            {
                continue;
            }

            painter->setPen(isActive ? QPen(pointColor, 2.0) : QPen(handleColor, 1.0));
            painter->setBrush(isActive ? handleColor : pointColor);
            painter->drawEllipse(devicePoints[i], radius, radius);

            if (int(i) == m_activePoint && !hasPointPreview)
            {
                // The point edited from the keyboard
                painter->setPen(QPen(pointColor, 1.5));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(devicePoints[i], radius + 4.0, radius + 4.0);
            }
        }

        if (hasPointPreview && m_interaction.pointIndex >= 0 && m_interaction.pointIndex < int(points.size()))
        {
            const size_t pointIndex = size_t(m_interaction.pointIndex);

            if (m_interaction.isSnapped)
            {
                // Target of the snapping
                painter->setPen(QPen(convertor.convert(QColor(0, 160, 0), false, true), 2.0));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(devicePoints[qsizetype(pointIndex)], radius + 5.0, radius + 5.0);
            }

            // Numeric feedback - position of the point and the length of the segment, which ends at it
            const QPointF point = points[pointIndex];
            QString text = QString("%1; %2 pt").arg(QLocale::system().toString(point.x(), 'f', 1), QLocale::system().toString(point.y(), 'f', 1));
            if (!m_interaction.isPreviewQuadEnds && points.size() >= 2)
            {
                const QPointF neighbour = points[pointIndex > 0 ? pointIndex - 1 : (m_interaction.isPreviewClosed ? points.size() - 1 : 1)];
                const QLineF segment(neighbour, point);
                text = tr("%1 | length %2 pt, angle %3°").arg(text, QLocale::system().toString(segment.length(), 'f', 1), QLocale::system().toString(std::fmod(360.0 - segment.angle(), 360.0), 'f', 1));
            }
            drawInfoText(painter, text, convertor);
        }
    }

    // New part of the annotation, which is being drawn
    if (m_interaction.type == Interaction::Part && m_interaction.pageIndex == pageIndex)
    {
        QPen partPen(convertor.convert(QColor(220, 0, 0), false, true), 1.5);
        partPen.setCosmetic(true);
        painter->setPen(partPen);

        if (m_partEdit == PartEdit::AddStroke)
        {
            QPolygonF devicePoints;
            for (const QPointF& point : m_interaction.partPoints)
            {
                devicePoints << m_interaction.pageToDevice.map(point);
            }

            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(devicePoints);
        }
        else
        {
            painter->setBrush(QBrush(fillColor));
            for (const QPolygonF& shape : m_interaction.partShapes)
            {
                painter->drawPolygon(m_interaction.pageToDevice.map(shape));
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
