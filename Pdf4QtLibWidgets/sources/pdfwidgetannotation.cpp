//    Copyright (C) 2023 Jakub Melka
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

#include "pdfwidgetannotation.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"
#include "pdfwidgetformmanager.h"
#include "pdfdrawspacecontroller.h"
#include "pdfselectpagesdialog.h"
#include "pdfobjecteditorwidget.h"
#include "pdfdocumentbuilder.h"

#include <QMenu>
#include <QDialog>
#include <QApplication>
#include <QMouseEvent>
#include <QGroupBox>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QStyleOptionButton>

namespace pdf
{

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
    Q_UNUSED(event);
}

void PDFWidgetAnnotationManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);
}

void PDFWidgetAnnotationManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    updateFromMouseEvent(event);
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

                const PDFAction* linkAction = nullptr;
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
                        linkAction = linkAnnotation->getAction();
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

                // Generate popup window
                if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton)
                {
                    const PDFMarkupAnnotation* markupAnnotation = pageAnnotation.annotation->asMarkupAnnotation();
                    if (markupAnnotation)
                    {
                        QDialog* dialog = createDialogForMarkupAnnotations(widget, pageAnnotation, pageAnnotations);

                        // Set proper dialog position - according to the popup annotation. If we
                        // do not have popup annotation, then try to use annotations rectangle.
                        if (const PageAnnotation* popupAnnotation = pageAnnotations.getPopupAnnotation(pageAnnotation))
                        {
                            QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(popupAnnotation->annotation->getRectangle().bottomLeft()).toPoint();
                            popupPoint = widget->mapToGlobal(popupPoint);
                            dialog->move(popupPoint);
                        }
                        else if (markupAnnotation->getRectangle().isValid())
                        {
                            QPoint popupPoint = snapshotItem.pageToDeviceMatrix.map(markupAnnotation->getRectangle().bottomRight()).toPoint();
                            popupPoint = widget->mapToGlobal(popupPoint);
                            dialog->move(popupPoint);
                        }

                        dialog->exec();
                    }

                    if (linkAction)
                    {
                        Q_EMIT actionTriggered(linkAction);
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
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

}   // namespace pdf
