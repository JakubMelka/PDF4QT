//    Copyright (C) 2023-2024 Jakub Melka
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

#include "editorplugin.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfstreamfilters.h"
#include "pdfoptimizer.h"

#include <QAction>
#include <QToolButton>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileDialog>

namespace pdfplugin
{

EditorPlugin::EditorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true),
    m_isSaving(false)
{
    m_scene.setIsPageContentDrawSuppressed(true);
}

// TODO: When text is edited, old text remains

void EditorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/editorplugin/activate.svg"), tr("&Edit page content"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-text.svg"), tr("Create &Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-freehand-curve.svg"), tr("Create &Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-yes-mark.svg"), tr("Create &Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-no-mark.svg"), tr("Create &Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rectangle.svg"), tr("Create R&ectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rounded-rectangle.svg"), tr("&Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-horizontal-line.svg"), tr("Create &Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-vertical-line.svg"), tr("Create &Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-line.svg"), tr("Create L&ine"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-dot.svg"), tr("Create &Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-svg-image.svg"), tr("Create &SVG Image"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/editorplugin/clear.svg"), tr("Clear A&ll Graphics"), this);
    QAction* certificatesAction = new QAction(QIcon(":/pdfplugins/editorplugin/certificates.svg"), tr("Certificates &Manager"), this);

    activateAction->setObjectName("editortool_activateAction");
    createTextAction->setObjectName("editortool_createTextAction");
    createFreehandCurveAction->setObjectName("editortool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("editortool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("editortool_createRejectMarkAction");
    createRectangleAction->setObjectName("editortool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("editortool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("editortool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("editortool_createVerticalLineAction");
    createLineAction->setObjectName("editortool_createLineAction");
    createDotAction->setObjectName("editortool_createDotAction");
    createSvgImageAction->setObjectName("editortool_createSvgImageAction");
    clearAction->setObjectName("editortool_clearAction");
    certificatesAction->setObjectName("editortool_certificatesAction");

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

    QFile acceptMarkFile(":/pdfplugins/editorplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/editorplugin/reject-mark.svg");
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
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &EditorPlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &EditorPlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &EditorPlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &EditorPlugin::onSceneEditElement);
    connect(clearAction, &QAction::triggered, &m_scene, &pdf::PDFPageContentScene::clear);
    connect(activateAction, &QAction::triggered, this, &EditorPlugin::onSetActive);
    connect(m_widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &EditorPlugin::onDrawSpaceChanged);
    connect(m_widget, &pdf::PDFWidget::sceneActivityChanged, this, &EditorPlugin::onSceneActivityChanged);

    updateActions();
}

void EditorPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> EditorPlugin::getActions() const
{
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);

    return result;
}

QString EditorPlugin::getPluginMenuName() const
{
    return tr("Edi&tor");
}

bool EditorPlugin::updatePageContent(pdf::PDFInteger pageIndex,
                                     const std::vector<const pdf::PDFPageContentElement*>& elements,
                                     pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFColorConvertor convertor;
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const pdf::PDFEditedPageContent& editedPageContent = m_editedPageContent.at(pageIndex);

    QRectF mediaBox = page->getMediaBox();
    QRectF mediaBoxMM = page->getMediaBoxMM();

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(m_document);
    contentStreamBuilder.setFontDictionary(editedPageContent.getFontDictionary());

    for (const pdf::PDFPageContentElement* element : elements)
    {
        const pdf::PDFPageContentElementEdited* editedElement = element->asElementEdited();
        const pdf::PDFPageContentElementRectangle* elementRectangle = element->asElementRectangle();
        const pdf::PDFPageContentElementLine* elementLine = element->asElementLine();
        const pdf::PDFPageContentElementDot* elementDot = element->asElementDot();
        const pdf::PDFPageContentElementFreehandCurve* elementFreehandCurve = element->asElementFreehandCurve();
        const pdf::PDFPageContentImageElement* elementImage = element->asElementImage();
        const pdf::PDFPageContentElementTextBox* elementTextBox = element->asElementTextBox();

        if (editedElement)
        {
            contentStreamBuilder.writeEditedElement(editedElement->getElement());
        }

        if (elementRectangle)
        {
            QRectF rect = elementRectangle->getRectangle();

            QPainterPath path;
            if (elementRectangle->isRounded())
            {
                qreal radius = qMin(rect.width(), rect.height()) * 0.25;
                path.addRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
            }
            else
            {
                path.addRect(rect);
            }

            const bool stroke = elementRectangle->getPen().style() != Qt::NoPen;
            const bool fill = elementRectangle->getBrush().style() != Qt::NoBrush;
            contentStreamBuilder.writeStyledPath(path, elementRectangle->getPen(), elementRectangle->getBrush(), stroke, fill);
        }

        if (elementLine)
        {
            QLineF line = elementLine->getLine();
            QPainterPath path;
            path.moveTo(line.p1());
            path.lineTo(line.p2());

            contentStreamBuilder.writeStyledPath(path, elementLine->getPen(), elementLine->getBrush(), true, false);
        }

        if (elementDot)
        {
            QPen pen = elementDot->getPen();
            const qreal radius = pen.widthF() * 0.5;

            QPainterPath path;
            path.addEllipse(elementDot->getPoint(), radius, radius);

            contentStreamBuilder.writeStyledPath(path, Qt::NoPen, QBrush(pen.color()), false, true);
        }

        if (elementFreehandCurve)
        {
            QPainterPath path = elementFreehandCurve->getCurve();
            contentStreamBuilder.writeStyledPath(path, elementFreehandCurve->getPen(), elementFreehandCurve->getBrush(), true, false);
        }

        if (elementImage)
        {
            QImage image = elementImage->getImage();
            if (!image.isNull())
            {
                contentStreamBuilder.writeImage(image, elementImage->getRectangle());
            }
            else
            {
                // It is probably an SVG image
                pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
                QPainter painter(&paintDevice);

                QList<pdf::PDFRenderError> errors;
                pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
                elementImage->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
            }
        }

        if (elementTextBox)
        {
            pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
            QPainter painter(&paintDevice);

            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            elementTextBox->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
        }
    }

    QStringList errors = contentStreamBuilder.getErrors();
    contentStreamBuilder.clearErrors();

    if (!errors.empty())
    {
        const int errorCount = errors.size();
        if (errors.size() > 3)
        {
            errors.resize(3);
        }

        QString message = tr("Errors (%2) occured while creating content stream on page %3.<br>%1").arg(errors.join("<br>")).arg(errorCount).arg(pageIndex + 1);
        if (QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Error"), message, QMessageBox::Abort, QMessageBox::Ignore) == QMessageBox::Abort)
        {
            return false;
        }
    }

    pdf::PDFDictionary fontDictionary = contentStreamBuilder.getFontDictionary();
    pdf::PDFDictionary xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
    pdf::PDFDictionary graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();

    builder->replaceObjectsByReferences(fontDictionary);
    builder->replaceObjectsByReferences(xobjectDictionary);
    builder->replaceObjectsByReferences(graphicStateDictionary);

    pdf::PDFArray array;
    array.appendItem(pdf::PDFObject::createName("FlateDecode"));

    // Compress the content stream
    QByteArray compressedData = pdf::PDFFlateDecodeFilter::compress(contentStreamBuilder.getOutputContent());
    pdf::PDFDictionary contentDictionary;
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedData.size()));
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(qMove(array))));
    pdf::PDFObject contentObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(qMove(contentDictionary), qMove(compressedData)));

    pdf::PDFObject pageObject = builder->getObjectByReference(page->getPageReference());

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();

    if (!fontDictionary.isEmpty())
    {
        factory.beginDictionaryItem("Font");
        factory << fontDictionary;
        factory.endDictionaryItem();
    }

    if (!xobjectDictionary.isEmpty())
    {
        factory.beginDictionaryItem("XObject");
        factory << xobjectDictionary;
        factory.endDictionaryItem();
    }

    if (!graphicStateDictionary.isEmpty())
    {
        factory.beginDictionaryItem("ExtGState");
        factory << graphicStateDictionary;
        factory.endDictionaryItem();
    }

    factory.endDictionary();
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Contents");
    factory << builder->addObject(std::move(contentObject));
    factory.endDictionaryItem();

    factory.endDictionary();

    pageObject = pdf::PDFObjectManipulator::merge(pageObject, factory.takeObject(), pdf::PDFObjectManipulator::RemoveNullObjects);
    builder->setObject(page->getPageReference(), std::move(pageObject));

    return true;
}

bool EditorPlugin::save()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);

    auto answer = QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Changes"), tr("The changes to the page content will be written to the document. Do you want to continue?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);

    if (answer == QMessageBox::Cancel)
    {
        return false;
    }

    if (answer == QMessageBox::Yes)
    {
        pdf::PDFDocumentModifier modifier(m_document);
        pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

        std::set<pdf::PDFInteger> pageIndices;
        for (const auto& item : m_editedPageContent)
        {
            pageIndices.insert(item.first);
        }

        std::map<pdf::PDFInteger, std::vector<const pdf::PDFPageContentElement*>> elementsByPage = m_scene.getElementsByPage();
        for (pdf::PDFInteger pageIndex : pageIndices)
        {
            if (m_editedPageContent.count(pageIndex) == 0)
            {
                continue;
            }

            std::vector<const pdf::PDFPageContentElement*> elements;
            auto it = elementsByPage.find(pageIndex);
            if (it != elementsByPage.cend())
            {
                elements = std::move(it->second);
            }

            if (!updatePageContent(pageIndex, elements, builder))
            {
                return false;
            }

            modifier.markReset();
        }

        m_scene.clear();
        m_editedPageContent.clear();

        if (modifier.finalize())
        {
            pdf::PDFDocument document = *modifier.getDocument();
            pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::DereferenceSimpleObjects |
                                        pdf::PDFOptimizer::RemoveNullObjects |
                                        pdf::PDFOptimizer::RemoveUnusedObjects |
                                        pdf::PDFOptimizer::MergeIdenticalObjects |
                                        pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
            optimizer.setDocument(&document);
            optimizer.optimize();
            document = optimizer.takeOptimizedDocument();

            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), nullptr, modifier.getFlags()));
        }
    }

    return true;
}

void EditorPlugin::onSceneActivityChanged()
{
    updateActions();
}

void EditorPlugin::onSceneChanged(bool graphicsOnly)
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

void EditorPlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void EditorPlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* EditorPlugin::getActiveTool()
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

void EditorPlugin::onToolActivityChanged()
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

void EditorPlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
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
        if (element->asElementEdited())
        {
            pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(element);
            if (editedElement->getElement()->asText())
            {
                updateTextElement(editedElement);
            }
        }

        updateGraphics();
    }
}

void EditorPlugin::onSceneEditSingleElement(pdf::PDFInteger elementId)
{
    onSceneEditElement({ elementId });
}

void EditorPlugin::onPenChanged(const QPen& pen)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void EditorPlugin::onBrushChanged(const QBrush& brush)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void EditorPlugin::onFontChanged(const QFont& font)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void EditorPlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void EditorPlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void EditorPlugin::setActive(bool active)
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
            m_editedPageContent.clear();
        }
        else
        {
            updateDockWidget();
            updateEditedPages();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();

        // If editor is not active, remove the widget
        if (m_editorWidget && !active)
        {
            delete m_editorWidget;
            m_editorWidget = nullptr;
        }
    }
}

void EditorPlugin::onSetActive(bool active)
{
    if (m_scene.isActive() && !active && !save())
    {
        updateActions();
        m_actions[Activate]->setChecked(true);
        return;
    }

    setActive(active);
}

void EditorPlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate])
            {
                action->setEnabled(m_widget && !m_widget->isAnySceneActive(&m_scene));
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
}

void EditorPlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void EditorPlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Editor Toolbox"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &EditorPlugin::onWidgetSelectionChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::editElementRequest, this, &EditorPlugin::onSceneEditSingleElement);

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

    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::penChanged, this, &EditorPlugin::onPenChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::brushChanged, this, &EditorPlugin::onBrushChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::fontChanged, this, &EditorPlugin::onFontChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::alignmentChanged, this, &EditorPlugin::onAlignmentChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::textAngleChanged, this, &EditorPlugin::onTextAngleChanged);
}

void EditorPlugin::updateEditedPages()
{
    if (!m_scene.isActive() || m_isSaving)
    {
        // Editor is not active or we are saving the document
        return;
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    for (pdf::PDFInteger pageIndex : currentPages)
    {
        if (m_editedPageContent.count(pageIndex))
        {
            continue;
        }

        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFPageContentEditorProcessor processor(page,
                                                     m_document,
                                                     m_widget->getDrawWidgetProxy()->getFontCache(),
                                                     cms.data(),
                                                     m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                     QTransform(),
                                                     pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        m_editedPageContent[pageIndex] = processor.takeEditedPageContent();

        size_t elementCount = m_editedPageContent[pageIndex].getElementCount();
        for (size_t i = 0; i < elementCount; ++i)
        {
            pdf::PDFEditedPageContentElement* element = m_editedPageContent[pageIndex].getElement(i);
            pdf::PDFPageContentElementEdited* editedElement = new pdf::PDFPageContentElementEdited(element);
            editedElement->setPageIndex(pageIndex);
            m_scene.addElement(editedElement);
        }
    }
}

bool EditorPlugin::updateTextElement(pdf::PDFPageContentElementEdited* element)
{
    pdf::PDFPageContentElementEdited* elementEdited = dynamic_cast<pdf::PDFPageContentElementEdited*>(element);
    pdf::PDFEditedPageContentElementText* targetTextElement = elementEdited->getElement()->asText();

    if (!targetTextElement)
    {
        return false;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    if (!updatePageContent(element->getPageIndex(), { element }, builder))
    {
        return false;
    }

    if (modifier.finalize())
    {
        pdf::PDFDocument* document = modifier.getDocument().get();

        const pdf::PDFPage* page = document->getCatalog()->getPage(element->getPageIndex());
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFFontCache fontCache(64, 64);
        pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(document, &activity));
        pdf::PDFPageContentEditorProcessor processor(page, document, &fontCache, cms.data(), &activity, QTransform(), pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        pdf::PDFEditedPageContent content = processor.takeEditedPageContent();
        if (content.getElementCount() == 1)
        {
            pdf::PDFEditedPageContentElement* sourceElement = content.getElement(0);
            pdf::PDFEditedPageContentElementText* sourceElementText = sourceElement->asText();
            targetTextElement->setState(sourceElementText->getState());
            targetTextElement->setTextPath(sourceElementText->getTextPath());
        }
        else
        {
            return false;
        }
    }

    return true;
}

void EditorPlugin::onDrawSpaceChanged()
{
    updateEditedPages();
}

}
