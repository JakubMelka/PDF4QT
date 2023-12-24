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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "signatureplugin.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "signdialog.h"
#include "pdfdocumentwriter.h"

#include <QBuffer>
#include <QAction>
#include <QToolButton>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileDialog>

namespace pdfplugin
{

SignaturePlugin::SignaturePlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true)
{

}

void SignaturePlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/signatureplugin/activate.svg"), tr("Activate signature creator"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-text.svg"), tr("Create Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-freehand-curve.svg"), tr("Create Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-yes-mark.svg"), tr("Create Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-no-mark.svg"), tr("Create Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-rectangle.svg"), tr("Create Rectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-rounded-rectangle.svg"), tr("Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-horizontal-line.svg"), tr("Create Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-vertical-line.svg"), tr("Create Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-line.svg"), tr("Create Line"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-dot.svg"), tr("Create Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-svg-image.svg"), tr("Create SVG Image"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/signatureplugin/clear.svg"), tr("Clear All Graphics"), this);
    QAction* signElectronicallyAction = new QAction(QIcon(":/pdfplugins/signatureplugin/sign-electronically.svg"), tr("Sign Electronically"), this);
    QAction* signDigitallyAction = new QAction(QIcon(":/pdfplugins/signatureplugin/sign-digitally.svg"), tr("Sign Digitally With Certificate"), this);
    QAction* certificatesAction = new QAction(QIcon(":/pdfplugins/signatureplugin/certificates.svg"), tr("Certificates Manager"), this);

    activateAction->setObjectName("signaturetool_activateAction");
    createTextAction->setObjectName("signaturetool_createTextAction");
    createFreehandCurveAction->setObjectName("signaturetool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("signaturetool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("signaturetool_createRejectMarkAction");
    createRectangleAction->setObjectName("signaturetool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("signaturetool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("signaturetool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("signaturetool_createVerticalLineAction");
    createLineAction->setObjectName("signaturetool_createLineAction");
    createDotAction->setObjectName("signaturetool_createDotAction");
    createSvgImageAction->setObjectName("signaturetool_createSvgImageAction");
    clearAction->setObjectName("signaturetool_clearAction");
    signElectronicallyAction->setObjectName("signaturetool_signElectronicallyAction");
    signDigitallyAction->setObjectName("signaturetool_signDigitallyAction");
    certificatesAction->setObjectName("signaturetool_certificatesAction");

    activateAction->setCheckable(true);
    createTextAction->setCheckable(true);
    createFreehandCurveAction->setCheckable(true);
    createAcceptMarkAction->setCheckable(true);
    createRejectMarkAction->setCheckable(true);
    createRectangleAction->setCheckable(true);
    createRoundedRectangleAction->setCheckable(true);
    createHorizontalLineAction->setCheckable(true);
    createVerticalLineAction->setCheckable(true);
    createLineAction->setCheckable(true);
    createDotAction->setCheckable(true);
    createSvgImageAction->setCheckable(true);

    m_actions[Activate] = activateAction;
    m_actions[Text] = createTextAction;
    m_actions[FreehandCurve] = createFreehandCurveAction;
    m_actions[AcceptMark] = createAcceptMarkAction;
    m_actions[RejectMark] = createRejectMarkAction;
    m_actions[Rectangle] = createRectangleAction;
    m_actions[RoundedRectangle] = createRoundedRectangleAction;
    m_actions[HorizontalLine] = createHorizontalLineAction;
    m_actions[VerticalLine] = createVerticalLineAction;
    m_actions[Line] = createLineAction;
    m_actions[Dot] = createDotAction;
    m_actions[SvgImage] = createSvgImageAction;
    m_actions[Clear] = clearAction;
    m_actions[SignElectronically] = signElectronicallyAction;
    m_actions[SignDigitally] = signDigitallyAction;
    m_actions[Certificates] = certificatesAction;

    QFile acceptMarkFile(":/pdfplugins/signatureplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/signatureplugin/reject-mark.svg");
    QByteArray rejectMarkContent;
    if (rejectMarkFile.open(QFile::ReadOnly))
    {
        rejectMarkContent = rejectMarkFile.readAll();
        rejectMarkFile.close();
    }

    m_tools[TextTool] = new pdf::PDFCreatePCElementTextTool(widget->getDrawWidgetProxy(), &m_scene, createTextAction, this);
    m_tools[FreehandCurveTool] = new pdf::PDFCreatePCElementFreehandCurveTool(widget->getDrawWidgetProxy(), &m_scene, createFreehandCurveAction, this);
    m_tools[AcceptMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createAcceptMarkAction, acceptMarkContent, false, this);
    m_tools[RejectMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createRejectMarkAction, rejectMarkContent, false, this);
    m_tools[RectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRectangleAction, false, this);
    m_tools[RoundedRectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRoundedRectangleAction, true, this);
    m_tools[HorizontalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createHorizontalLineAction, true, false, this);
    m_tools[VerticalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createVerticalLineAction, false, true, this);
    m_tools[LineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createLineAction, false, false, this);
    m_tools[DotTool] = new pdf::PDFCreatePCElementDotTool(widget->getDrawWidgetProxy(), &m_scene, createDotAction, this);
    m_tools[ImageTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createSvgImageAction, QByteArray(), true, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        toolManager->addTool(tool);
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &SignaturePlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &SignaturePlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &SignaturePlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &SignaturePlugin::onSceneEditElement);
    connect(clearAction, &QAction::triggered, &m_scene, &pdf::PDFPageContentScene::clear);
    connect(activateAction, &QAction::triggered, this, &SignaturePlugin::setActive);
    connect(signElectronicallyAction, &QAction::triggered, this, &SignaturePlugin::onSignElectronically);
    connect(signDigitallyAction, &QAction::triggered, this, &SignaturePlugin::onSignDigitally);
    connect(certificatesAction, &QAction::triggered, this, &SignaturePlugin::onOpenCertificatesManager);

    updateActions();
}

void SignaturePlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> SignaturePlugin::getActions() const
{
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);
    result.push_back(m_actions[SignElectronically]);
    result.push_back(m_actions[SignDigitally]);
    result.push_back(m_actions[Certificates]);

    return result;
}

void SignaturePlugin::onSceneChanged(bool graphicsOnly)
{
    if (!graphicsOnly)
    {
        updateActions();
    }

    if (m_editorWidget)
    {
        m_editorWidget->updateItemsInListWidget();
    }

    updateGraphics();
}

void SignaturePlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void SignaturePlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* SignaturePlugin::getActiveTool()
{
    for (pdf::PDFWidgetTool* currentTool : m_tools)
    {
        if (currentTool->isActive())
        {
            return currentTool;
        }
    }

    return nullptr;
}

void SignaturePlugin::onToolActivityChanged()
{
    if (m_editorWidget)
    {
        pdf::PDFWidgetTool* activeTool = getActiveTool();

        const pdf::PDFPageContentElement* element = nullptr;
        pdf::PDFCreatePCElementTool* tool = qobject_cast<pdf::PDFCreatePCElementTool*>(activeTool);
        if (tool)
        {
            element = tool->getElement();
        }

        m_editorWidget->loadStyleFromElement(element);
    }
}

void SignaturePlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
{
    if (elements.empty())
    {
        return;
    }

    pdf::PDFPageContentElement* element = nullptr;
    for (pdf::PDFInteger id : elements)
    {
        element = m_scene.getElementById(id);
        if (element)
        {
            break;
        }
    }

    if (!element)
    {
        return;
    }

    if (pdf::PDFPageContentEditorStyleSettings::showEditElementStyleDialog(m_dataExchangeInterface->getMainWindow(), element))
    {
        updateGraphics();
    }
}

void SignaturePlugin::onSignElectronically()
{
    Q_ASSERT(m_document);
    Q_ASSERT(!m_scene.isEmpty());

    if (QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Signature"), tr("Document will be signed electronically. Do you want to continue?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
        pdf::PDFDocumentModifier modifier(m_document);

        std::set<pdf::PDFInteger> pageIndices = m_scene.getPageIndices();
        for (pdf::PDFInteger pageIndex : pageIndices)
        {
            const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
            pdf::PDFPageContentStreamBuilder pageContentStreamBuilder(modifier.getBuilder(),
                                                                      pdf::PDFContentStreamBuilder::CoordinateSystem::PDF,
                                                                      pdf::PDFPageContentStreamBuilder::Mode::PlaceAfter);
            QPainter* painter = pageContentStreamBuilder.begin(page->getPageReference());
            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter nullGetter(nullptr, pageIndex);
            m_scene.drawElements(painter, pageIndex, nullGetter, QTransform(), nullptr, errors);
            pageContentStreamBuilder.end(painter);
            modifier.markPageContentsChanged();
        }
        m_scene.clear();

        if (modifier.finalize())
        {
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void SignaturePlugin::onSignDigitally()
{
    // Jakub Melka: do we have certificates? If not,
    // open certificate dialog, so the user can create
    // a new one.
    if (pdf::PDFCertificateManager::getCertificates().empty())
    {
        onOpenCertificatesManager();

        if (pdf::PDFCertificateManager::getCertificates().empty())
        {
            return;
        }
    }

    SignDialog dialog(m_dataExchangeInterface->getMainWindow(), m_scene.isEmpty());
    if (dialog.exec() == SignDialog::Accepted)
    {
        const pdf::PDFCertificateEntry* certificate = dialog.getCertificate();
        Q_ASSERT(certificate);

        QByteArray data = "SampleDataToBeSigned" + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
        QByteArray signature;
        if (!pdf::PDFSignatureFactory::sign(*certificate, dialog.getPassword(), data, signature))
        {
            QMessageBox::critical(m_widget, tr("Error"), tr("Failed to create digital signature."));
            return;
        }

        QString signatureName = QString("pdf4qt_signature_%1").arg(QString::number(QDateTime::currentMSecsSinceEpoch()));

        pdf::PDFInteger offsetMark = 123456789123;
        constexpr const char* offsetMarkString = "123456789123";
        const auto offsetMarkStringLength = std::strlen(offsetMarkString);

        pdf::PDFDocumentBuilder builder(m_document);
        pdf::PDFObjectReference signatureDictionary = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", signature, QDateTime::currentDateTime(), offsetMark);
        pdf::PDFObjectReference formField = builder.createFormFieldSignature(signatureName, { }, signatureDictionary);
        builder.createAcroForm({ formField });

        const pdf::PDFCatalog* catalog = m_document->getCatalog();
        if (dialog.getSignMethod() == SignDialog::SignDigitallyInvisible)
        {
            if (catalog->getPageCount() > 0)
            {
                const pdf::PDFObjectReference pageReference = catalog->getPage(0)->getPageReference();
                builder.createInvisibleFormFieldWidget(formField, pageReference);
            }
        }
        else if (dialog.getSignMethod() == SignDialog::SignDigitally)
        {
            Q_ASSERT(!m_scene.isEmpty());
            const pdf::PDFInteger pageIndex = *m_scene.getPageIndices().begin();
            const pdf::PDFPage* page = catalog->getPage(pageIndex);

            pdf::PDFContentStreamBuilder contentBuilder(page->getMediaBox().size(), pdf::PDFContentStreamBuilder::CoordinateSystem::PDF);
            QPainter* painter = contentBuilder.begin();
            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter nullGetter(nullptr, pageIndex);
            m_scene.drawPage(painter, pageIndex, nullptr, nullGetter, QTransform(), errors);
            pdf::PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);

            QRectF boundingRect = m_scene.getBoundingBox(pageIndex);
            std::vector<pdf::PDFObject> copiedObjects = builder.copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
            Q_ASSERT(copiedObjects.size() == 2);

            pdf::PDFObjectReference resourcesReference = copiedObjects[0].getReference();
            pdf::PDFObjectReference formReference = copiedObjects[1].getReference();

            // Create form object
            pdf::PDFObjectFactory formFactory;

            formFactory.beginDictionary();

            formFactory.beginDictionaryItem("Type");
            formFactory << pdf::WrapName("XObject");
            formFactory.endDictionaryItem();

            formFactory.beginDictionaryItem("Subtype");
            formFactory << pdf::WrapName("Form");
            formFactory.endDictionaryItem();

            formFactory.beginDictionaryItem("BBox");
            formFactory << boundingRect;
            formFactory.endDictionaryItem();

            formFactory.beginDictionaryItem("Resources");
            formFactory << resourcesReference;
            formFactory.endDictionaryItem();

            formFactory.endDictionary();

            builder.mergeTo(formReference, formFactory.takeObject());

            builder.createFormFieldWidget(formField, page->getPageReference(), formReference, boundingRect);
        }

        QString reasonText = dialog.getReasonText();
        if (!reasonText.isEmpty())
        {
            builder.setSignatureReason(signatureDictionary, reasonText);
        }

        QString contactInfoText = dialog.getContactInfoText();
        if (!contactInfoText.isEmpty())
        {
            builder.setSignatureContactInfo(signatureDictionary, contactInfoText);
        }

        pdf::PDFDocument signedDocument = builder.build();

        // 1) Save the document with incorrect signature
        QBuffer buffer;
        pdf::PDFDocumentWriter writer(m_widget->getDrawWidgetProxy()->getProgress());
        buffer.open(QBuffer::ReadWrite);
        writer.write(&buffer, &signedDocument);

        const int indexOfSignature = buffer.data().indexOf(signature.toHex());
        if (indexOfSignature == -1)
        {
            QMessageBox::critical(m_widget, tr("Error"), tr("Failed to create digital signature."));
            buffer.close();
            return;
        }

        // 2) Write ranges to be checked
        const pdf::PDFInteger i1 = 0;
        const pdf::PDFInteger i2 = indexOfSignature - 1;
        const pdf::PDFInteger i3 = i2 + signature.size() * 2 + 2;
        const pdf::PDFInteger i4 = buffer.data().size() - i3;

        auto writeInt = [&](pdf::PDFInteger offset)
        {
            QString offsetString = QString::number(offset);
            offsetString = offsetString.leftJustified(static_cast<int>(offsetMarkStringLength), ' ', true);
            const auto index = buffer.data().lastIndexOf(QByteArray(offsetMarkString, offsetMarkStringLength), indexOfSignature);
            buffer.seek(index);
            buffer.write(offsetString.toLocal8Bit());
        };

        writeInt(i4);
        writeInt(i3);
        writeInt(i2);
        writeInt(i1);

        // 3) Sign the data
        QByteArray dataToBeSigned;
        buffer.seek(i1);
        dataToBeSigned.append(buffer.read(i2));
        buffer.seek(i3);
        dataToBeSigned.append(buffer.read(i4));

        if (!pdf::PDFSignatureFactory::sign(*certificate, dialog.getPassword(), dataToBeSigned, signature))
        {
            QMessageBox::critical(m_widget, tr("Error"), tr("Failed to create digital signature."));
            buffer.close();
            return;
        }

        buffer.seek(i2 + 1);
        buffer.write(signature.toHex());

        buffer.close();

        QString fileName = QFileDialog::getSaveFileName(m_dataExchangeInterface->getMainWindow(), tr("Save Signed Document"), getSignedFileName(), tr("Portable Document (*.pdf);;All files (*.*)"));
        if (!fileName.isEmpty())
        {
            QFile signedFile(fileName);
            if (signedFile.open(QFile::WriteOnly | QFile::Truncate))
            {
                signedFile.write(buffer.data());
                signedFile.close();
            }
        }
    }
}

QString SignaturePlugin::getSignedFileName() const
{
    QFileInfo fileInfo(m_dataExchangeInterface->getOriginalFileName());

    return fileInfo.path() + "/" + fileInfo.baseName() + "_SIGNED.pdf";
}

void SignaturePlugin::onOpenCertificatesManager()
{
    pdf::PDFCertificateManagerDialog dialog(m_dataExchangeInterface->getMainWindow());
    dialog.exec();
}

void SignaturePlugin::onPenChanged(const QPen& pen)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void SignaturePlugin::onBrushChanged(const QBrush& brush)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void SignaturePlugin::onFontChanged(const QFont& font)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void SignaturePlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void SignaturePlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void SignaturePlugin::setActive(bool active)
{
    if (m_scene.isActive() != active)
    {
        // Abort active tool, if we are deactivating the plugin
        if (!active)
        {
            if (pdf::PDFWidgetTool* tool = m_widget->getToolManager()->getActiveTool())
            {
                auto it = std::find(m_tools.cbegin(), m_tools.cend(), tool);
                if (it != m_tools.cend())
                {
                    m_widget->getToolManager()->setActiveTool(nullptr);
                }
            }
        }

        m_scene.setActive(active);
        if (!active)
        {
            m_scene.clear();
        }
        else
        {
            updateDockWidget();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();
    }
}

void SignaturePlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action and certificates
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate] ||
                action == m_actions[Certificates])
            {
                continue;
            }

            action->setEnabled(false);
        }

        return;
    }

    const bool isSceneNonempty = !m_scene.isEmpty();

    // Tool actions
    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        m_actions[actionId]->setEnabled(true);
    }

    // Clear action
    QAction* clearAction = m_actions[Clear];
    clearAction->setEnabled(isSceneNonempty);

    // Sign actions
    QAction* signElectronicallyAction = m_actions[SignElectronically];
    signElectronicallyAction->setEnabled(isSceneNonempty);
    QAction* signDigitallyAction = m_actions[SignDigitally];
    signDigitallyAction->setEnabled(m_document);
}

void SignaturePlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void SignaturePlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Signature Toolbox"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &SignaturePlugin::onWidgetSelectionChanged);

    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignTop))->setIcon(QIcon(":/resources/pce-align-top.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterVertically))->setIcon(QIcon(":/resources/pce-align-v-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignBottom))->setIcon(QIcon(":/resources/pce-align-bottom.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignLeft))->setIcon(QIcon(":/resources/pce-align-left.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterHorizontally))->setIcon(QIcon(":/resources/pce-align-h-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignRight))->setIcon(QIcon(":/resources/pce-align-right.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameHeight))->setIcon(QIcon(":/resources/pce-same-height.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameWidth))->setIcon(QIcon(":/resources/pce-same-width.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameSize))->setIcon(QIcon(":/resources/pce-same-size.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorizontally))->setIcon(QIcon(":/resources/pce-center-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterVertically))->setIcon(QIcon(":/resources/pce-center-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorAndVert))->setIcon(QIcon(":/resources/pce-center-vh.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutVertically))->setIcon(QIcon(":/resources/pce-layout-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutHorizontally))->setIcon(QIcon(":/resources/pce-layout-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutForm))->setIcon(QIcon(":/resources/pce-layout-form.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutGrid))->setIcon(QIcon(":/resources/pce-layout-grid.svg"));

    for (QAction* action : m_actions)
    {
        m_editorWidget->addAction(action);
    }

    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::penChanged, this, &SignaturePlugin::onPenChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::brushChanged, this, &SignaturePlugin::onBrushChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::fontChanged, this, &SignaturePlugin::onFontChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::alignmentChanged, this, &SignaturePlugin::onAlignmentChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::textAngleChanged, this, &SignaturePlugin::onTextAngleChanged);
}

}
