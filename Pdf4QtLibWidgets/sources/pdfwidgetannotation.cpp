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

#include "pdfwidgetannotation.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"
#include "pdfwidgetformmanager.h"
#include "pdfdrawspacecontroller.h"
#include "pdfselectpagesdialog.h"
#include "pdfobjecteditorwidget.h"
#include "pdfdocumentbuilder.h"

#include <algorithm>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QDialog>
#include <QApplication>
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

namespace pdf
{

namespace
{
constexpr quint32 kAnnotationDragMagic = 0x5044414E; // "PDAN"
constexpr quint32 kAnnotationDragVersion = 1;
const char* kAnnotationDragMimeType = "application/x-pdf4qt-annotation";

struct AnnotationDragPayload
{
    PDFObjectReference annotationReference;
    PDFObjectReference pageReference;
    QRectF rect;
    QPointF cursorOffset;
};

QByteArray serializePayload(const AnnotationDragPayload& payload)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kAnnotationDragMagic << kAnnotationDragVersion;
    stream << qint32(payload.annotationReference.objectNumber) << qint32(payload.annotationReference.generation);
    stream << qint32(payload.pageReference.objectNumber) << qint32(payload.pageReference.generation);
    stream << payload.rect << payload.cursorOffset;
    return data;
}

bool deserializePayload(const QMimeData* data, AnnotationDragPayload& payload)
{
    if (!data || !data->hasFormat(kAnnotationDragMimeType))
    {
        return false;
    }

    QByteArray raw = data->data(kAnnotationDragMimeType);
    QDataStream stream(&raw, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    stream >> magic >> version;
    if (magic != kAnnotationDragMagic || version != kAnnotationDragVersion)
    {
        return false;
    }

    qint32 annotObj = 0;
    qint32 annotGen = 0;
    qint32 pageObj = 0;
    qint32 pageGen = 0;
    stream >> annotObj >> annotGen >> pageObj >> pageGen;
    stream >> payload.rect >> payload.cursorOffset;

    payload.annotationReference = PDFObjectReference(annotObj, annotGen);
    payload.pageReference = PDFObjectReference(pageObj, pageGen);
    return payload.annotationReference.isValid() && payload.pageReference.isValid();
}

bool removeAnnotationReferenceFromPage(PDFDocumentBuilder* builder,
                                       PDFObjectReference pageReference,
                                       PDFObjectReference annotationReference)
{
    if (!builder || !pageReference.isValid() || !annotationReference.isValid())
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    if (!storage)
    {
        return false;
    }

    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(pageReference));
    if (!pageDictionary)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFObjectReference> annots = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");
    const auto newEnd = std::remove(annots.begin(), annots.end(), annotationReference);
    if (newEnd == annots.end())
    {
        return false;
    }

    annots.erase(newEnd, annots.end());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    if (!annots.empty())
    {
        factory << annots;
    }
    else
    {
        factory << PDFObject();
    }
    factory.endDictionaryItem();
    factory.endDictionary();

    builder->mergeTo(pageReference, factory.takeObject());
    return true;
}
}

PDFWidgetAnnotationManager::PDFWidgetAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(proxy->getFontCache(), proxy->getCMSManager(), proxy->getOptionalContentActivity(), proxy->getMeshQualitySettings(), proxy->getFeatures(), Target::View, parent),
    m_proxy(proxy)
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
    }
}

void PDFWidgetAnnotationManager::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        if (beginAnnotationDrag(event))
        {
            event->accept();
            return;
        }

        if (getLinkActionAtPosition(event->pos()))
        {
            event->accept();
            return;
        }
    }

    // Show context menu?
    if (event->button() == Qt::RightButton)
    {
        PDFWidget* pdfWidget = m_proxy->getWidget();
        std::vector<PDFInteger> currentPages = pdfWidget->getDrawWidget()->getCurrentPages();

        if (!hasAnyPageAnnotation(currentPages))
        {
            // All pages doesn't have annotation
            return;
        }

        m_editableAnnotation = PDFObjectReference();
        m_editableAnnotationPage = PDFObjectReference();
        for (PDFInteger pageIndex : currentPages)
        {
            PageAnnotations& pageAnnotations = getPageAnnotations(pageIndex);
            for (PageAnnotation& pageAnnotation : pageAnnotations.annotations)
            {
                if (!pageAnnotation.isHovered)
                {
                    continue;
                }

                if (!PDFAnnotation::isTypeEditable(pageAnnotation.annotation->getType()))
                {
                    continue;
                }

                m_editableAnnotation = pageAnnotation.annotation->getSelfReference();
                m_editableAnnotationPage = pageAnnotation.annotation->getPageReference();

                if (!m_editableAnnotationPage.isValid())
                {
                    m_editableAnnotationPage = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
                }
                break;
            }
        }

        QPoint menuPosition = pdfWidget->mapToGlobal(event->pos());
        showAnnotationMenu(m_editableAnnotation, m_editableAnnotationPage, menuPosition);
    }
}

void PDFWidgetAnnotationManager::showAnnotationMenu(PDFObjectReference annotationReference,
                                                    PDFObjectReference pageReference,
                                                    QPoint globalMenuPosition)
{
    m_editableAnnotation = annotationReference;
    m_editableAnnotationPage = pageReference;

    if (m_editableAnnotation.isValid())
    {
        PDFWidget* pdfWidget = m_proxy->getWidget();

        QMenu menu(tr("Annotation"), pdfWidget);
        QAction* showPopupAction = menu.addAction(tr("Show Popup Window"));
        QAction* copyAction = menu.addAction(tr("Copy to Multiple Pages"));
        QAction* editAction = menu.addAction(tr("Edit"));
        QAction* deleteAction = menu.addAction(tr("Delete"));
        connect(showPopupAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onShowPopupAnnotation);
        connect(copyAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onCopyAnnotation);
        connect(editAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onEditAnnotation);
        connect(deleteAction, &QAction::triggered, this, &PDFWidgetAnnotationManager::onDeleteAnnotation);

        m_editableAnnotationGlobalPosition = globalMenuPosition;
        menu.exec(m_editableAnnotationGlobalPosition);
    }
}

void PDFWidgetAnnotationManager::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_dragState.isActive)
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

            dialog->exec();
            return;
        }
    }
}

bool PDFWidgetAnnotationManager::canAcceptAnnotationDrag(const QMimeData* data) const
{
    AnnotationDragPayload payload;
    if (!deserializePayload(data, payload))
    {
        return false;
    }

    if (!m_document)
    {
        return false;
    }

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_document->getStorage(), payload.annotationReference);
    if (!annotation)
    {
        return false;
    }

    return annotation->getType() != AnnotationType::Link;
}

bool PDFWidgetAnnotationManager::handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action)
{
    AnnotationDragPayload payload;
    if (!deserializePayload(data, payload))
    {
        return false;
    }

    if (!m_document)
    {
        return false;
    }

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_document->getStorage(), payload.annotationReference);
    if (!annotation || annotation->getType() == AnnotationType::Link)
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

    const QPointF newTopLeft = pagePoint - payload.cursorOffset;
    const QPointF delta = newTopLeft - payload.rect.topLeft();

    PDFDocumentModifier modifier(m_document);
    modifier.markAnnotationsChanged();
    PDFDocumentBuilder* builder = modifier.getBuilder();

    PDFObjectReference targetAnnotation = payload.annotationReference;
    PDFObjectReference popupReference;
    if (action == Qt::CopyAction)
    {
        targetAnnotation = copyAnnotationToPage(builder, targetPageReference, payload.annotationReference);
    }
    else if (targetPageReference != payload.pageReference)
    {
        const PDFObject& annotationObject = m_document->getObjectByReference(payload.annotationReference);
        const PDFDictionary* annotationDictionary = m_document->getDictionaryFromObject(annotationObject);
        if (annotationDictionary)
        {
            PDFObject popupObject = annotationDictionary->get("Popup");
            if (popupObject.isReference())
            {
                popupReference = popupObject.getReference();
            }
        }

        removeAnnotationReferenceFromPage(builder, payload.pageReference, payload.annotationReference);

        PDFObjectFactory pageFactory;
        pageFactory.beginDictionary();
        pageFactory.beginDictionaryItem("P");
        pageFactory << targetPageReference;
        pageFactory.endDictionaryItem();
        pageFactory.endDictionary();
        builder->mergeTo(payload.annotationReference, pageFactory.takeObject());

        PDFObjectFactory annotsFactory;
        annotsFactory.beginDictionary();
        annotsFactory.beginDictionaryItem("Annots");
        annotsFactory.beginArray();
        annotsFactory << payload.annotationReference;
        annotsFactory.endArray();
        annotsFactory.endDictionaryItem();
        annotsFactory.endDictionary();
        builder->appendTo(targetPageReference, annotsFactory.takeObject());

        if (popupReference.isValid())
        {
            removeAnnotationReferenceFromPage(builder, payload.pageReference, popupReference);

            PDFObjectFactory popupPageFactory;
            popupPageFactory.beginDictionary();
            popupPageFactory.beginDictionaryItem("P");
            popupPageFactory << targetPageReference;
            popupPageFactory.endDictionaryItem();
            popupPageFactory.endDictionary();
            builder->mergeTo(popupReference, popupPageFactory.takeObject());

            PDFObjectFactory popupAnnotsFactory;
            popupAnnotsFactory.beginDictionary();
            popupAnnotsFactory.beginDictionaryItem("Annots");
            popupAnnotsFactory.beginArray();
            popupAnnotsFactory << popupReference;
            popupAnnotsFactory.endArray();
            popupAnnotsFactory.endDictionaryItem();
            popupAnnotsFactory.endDictionary();
            builder->appendTo(targetPageReference, popupAnnotsFactory.takeObject());
        }
    }

    if (!targetAnnotation.isValid())
    {
        return false;
    }

    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()) && action != Qt::CopyAction && targetPageReference == payload.pageReference)
    {
        return false;
    }

    if (translateAnnotation(builder, targetAnnotation, delta))
    {
        builder->updateAnnotationAppearanceStreams(targetAnnotation);
    }

    if (action != Qt::CopyAction)
    {
        const PDFObject& annotationObject = m_document->getObjectByReference(payload.annotationReference);
        const PDFDictionary* annotationDictionary = m_document->getDictionaryFromObject(annotationObject);
        if (annotationDictionary)
        {
            PDFObject popupObject = annotationDictionary->get("Popup");
            if (popupObject.isReference())
            {
                translateAnnotation(builder, popupObject.getReference(), delta);
            }
        }
    }

    if (modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    return true;
}

void PDFWidgetAnnotationManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        if (m_suppressLinkActivationOnRelease)
        {
            m_suppressLinkActivationOnRelease = false;
            event->accept();
            return;
        }

        if (m_dragState.isActive)
        {
            m_dragState = DragState();
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

    if (!hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return;
    }

    m_tooltip = QString();
    m_cursor = std::nullopt;
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

    // If appearance has changed, then we must redraw the page
    if (appearanceChanged)
    {
        Q_EMIT widget->getDrawWidgetProxy()->repaintNeeded();
    }
}

bool PDFWidgetAnnotationManager::beginAnnotationDrag(QMouseEvent* event)
{
    if (!m_document)
    {
        return false;
    }

    m_dragState = DragState();
    m_dragState.isCopy = event->modifiers().testFlag(Qt::ControlModifier);

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

            if (!PDFAnnotation::isTypeEditable(pageAnnotation.annotation->getType()))
            {
                continue;
            }

            if (pageAnnotation.annotation->getType() == AnnotationType::Link)
            {
                continue;
            }

            const PDFAnnotation::Flags flags = pageAnnotation.annotation->getEffectiveFlags();
            if (flags.testFlag(PDFAnnotation::Locked) || flags.testFlag(PDFAnnotation::ReadOnly))
            {
                continue;
            }

            m_dragState.isActive = true;
            m_dragState.startDevicePos = event->pos();
            m_dragState.pageIndex = snapshotItem.pageIndex;
            m_dragState.pageToDevice = snapshotItem.pageToDeviceMatrix;
            bool invertible = false;
            m_dragState.deviceToPage = snapshotItem.pageToDeviceMatrix.inverted(&invertible);
            if (!invertible)
            {
                m_dragState = DragState();
                return false;
            }
            m_dragState.annotationReference = pageAnnotation.annotation->getSelfReference();
            m_dragState.pageReference = pageAnnotation.annotation->getPageReference();

            if (!m_dragState.pageReference.isValid())
            {
                m_dragState.pageReference = m_document->getCatalog()->getPage(snapshotItem.pageIndex)->getPageReference();
            }

            m_dragState.originalRect = pageAnnotation.annotation->getRectangle();
            if (!m_dragState.originalRect.isValid())
            {
                m_dragState = DragState();
                return false;
            }

            QPointF pagePoint = m_dragState.deviceToPage.map(event->pos());
            m_dragState.cursorOffset = pagePoint - m_dragState.originalRect.topLeft();

            const PDFObject& annotationObject = m_document->getObjectByReference(m_dragState.annotationReference);
            const PDFDictionary* annotationDictionary = m_document->getDictionaryFromObject(annotationObject);
            PDFDocumentDataLoaderDecorator loader(m_document);
            if (annotationDictionary)
            {
                m_dragState.originalQuadPoints = loader.readNumberArrayFromDictionary(annotationDictionary, "QuadPoints");
                PDFObject popupObject = annotationDictionary->get("Popup");
                if (popupObject.isReference())
                {
                    m_dragState.popupReference = popupObject.getReference();
                }
            }

            const PDFPage* page = m_document->getCatalog()->getPage(snapshotItem.pageIndex);
            if (page)
            {
                m_dragState.dragPixmap = createAnnotationDragPixmap(pageAnnotation,
                                                                    snapshotItem.pageToDeviceMatrix,
                                                                    page,
                                                                    m_dragState.cursorOffset,
                                                                    m_dragState.dragHotSpot);
            }

            return true;
        }
    }

    return false;
}

void PDFWidgetAnnotationManager::startAnnotationDrag(QMouseEvent* event)
{
    if (!m_dragState.isActive || !event)
    {
        return;
    }

    AnnotationDragPayload payload;
    payload.annotationReference = m_dragState.annotationReference;
    payload.pageReference = m_dragState.pageReference;
    payload.rect = m_dragState.originalRect;
    payload.cursorOffset = m_dragState.cursorOffset;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData(kAnnotationDragMimeType, serializePayload(payload));

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
}

QPixmap PDFWidgetAnnotationManager::createAnnotationDragPixmap(const PageAnnotation& pageAnnotation,
                                                               const QTransform& pagePointToDevicePointMatrix,
                                                               const PDFPage* page,
                                                               const QPointF& cursorOffset,
                                                               QPoint& hotSpot) const
{
    hotSpot = QPoint();

    if (!page || !pageAnnotation.annotation)
    {
        return QPixmap();
    }

    QRectF annotationRect = pageAnnotation.annotation->getRectangle();
    QWidget* deviceWidget = m_proxy->getWidget()->getDrawWidget()->getWidget();
    if (!deviceWidget)
    {
        return QPixmap();
    }

    QRectF deviceRectF = pagePointToDevicePointMatrix.mapRect(annotationRect).normalized();
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
    painter.setOpacity(0.6);

    painter.save();
    painter.setOpacity(1.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(90, 160, 255, 80));
    painter.drawRect(QRect(QPoint(0, 0), deviceRect.size()));
    painter.restore();

    painter.save();
    painter.setOpacity(1.0);
    painter.setPen(QPen(QColor(40, 110, 220, 180), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(QPoint(0, 0), deviceRect.size()));
    painter.restore();

    const QPointF cursorPoint = annotationRect.topLeft() + cursorOffset;
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

bool PDFWidgetAnnotationManager::translateAnnotation(PDFDocumentBuilder* builder,
                                                     PDFObjectReference annotationReference,
                                                     const QPointF& delta)
{
    if (!builder || !annotationReference.isValid())
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFObject& annotationObject = storage->getObjectByReference(annotationReference);
    const PDFDictionary* annotationDictionary = storage->getDictionaryFromObject(annotationObject);
    if (!annotationDictionary)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    QRectF rect = loader.readRectangle(annotationDictionary->get("Rect"), QRectF());
    if (!rect.isValid())
    {
        return false;
    }

    QRectF translatedRect = rect.translated(delta);
    PDFDictionary dictionaryCopy = *annotationDictionary;

    PDFObjectFactory rectFactory;
    rectFactory << translatedRect;
    dictionaryCopy.setEntry(PDFInplaceOrMemoryString("Rect"), rectFactory.takeObject());

    if (annotationDictionary->hasKey("QuadPoints"))
    {
        std::vector<PDFReal> quadPoints = loader.readNumberArrayFromDictionary(annotationDictionary, "QuadPoints");
        if (quadPoints.size() % 2 == 0)
        {
            for (size_t i = 0; i < quadPoints.size(); i += 2)
            {
                quadPoints[i] += delta.x();
                quadPoints[i + 1] += delta.y();
            }

            PDFObjectFactory quadFactory;
            quadFactory << quadPoints;
            dictionaryCopy.setEntry(PDFInplaceOrMemoryString("QuadPoints"), quadFactory.takeObject());
        }
    }

    builder->setObject(annotationReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(dictionaryCopy))));
    return true;
}

PDFObjectReference PDFWidgetAnnotationManager::copyAnnotationToPage(PDFDocumentBuilder* builder,
                                                                    PDFObjectReference pageReference,
                                                                    PDFObjectReference annotationReference)
{
    if (!builder || !pageReference.isValid() || !annotationReference.isValid())
    {
        return PDFObjectReference();
    }

    PDFObjectReference copiedAnnotation = builder->addObject(builder->getObjectByReference(annotationReference));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << pageReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Popup");
    factory << PDFObject();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("IRT");
    factory << PDFObject();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(copiedAnnotation, factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory.beginArray();
    factory << copiedAnnotation;
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->appendTo(pageReference, factory.takeObject());

    return copiedAnnotation;
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

}   // namespace pdf
