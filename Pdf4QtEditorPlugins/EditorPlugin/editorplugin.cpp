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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "editorplugin.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "pdfdocumentwriter.h"

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
    m_sceneSelectionChangeEnabled(true)
{

}

void EditorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/editorplugin/activate.svg"), tr("Edit page content"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-text.svg"), tr("Create Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-freehand-curve.svg"), tr("Create Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-yes-mark.svg"), tr("Create Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-no-mark.svg"), tr("Create Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rectangle.svg"), tr("Create Rectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rounded-rectangle.svg"), tr("Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-horizontal-line.svg"), tr("Create Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-vertical-line.svg"), tr("Create Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-line.svg"), tr("Create Line"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-dot.svg"), tr("Create Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-svg-image.svg"), tr("Create SVG Image"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/editorplugin/clear.svg"), tr("Clear All Graphics"), this);
    QAction* certificatesAction = new QAction(QIcon(":/pdfplugins/editorplugin/certificates.svg"), tr("Certificates Manager"), this);

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
    connect(activateAction, &QAction::triggered, this, &EditorPlugin::setActive);
    connect(m_widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &EditorPlugin::onDrawSpaceChanged);

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
        updateGraphics();
    }
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
    }
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
    if (!m_scene.isActive())
    {
        // Editor is not active
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
        for (size_t i = 0; i <elementCount; ++i)
        {
            pdf::PDFEditedPageContentElement* element = m_editedPageContent[pageIndex].getElement(i);

        }
/*
        std::vector<pdf::PDFEditedPageContent::ContentTextInfo> textInfos = m_editedPageContent[pageIndex].getTextInfos();
        for (const pdf::PDFEditedPageContent::ContentTextInfo& textInfo : textInfos)
        {
            if (textInfo.boundingRectangle.isEmpty())
            {
                continue;
            }

            pdf::PDFPageContentElementEditedContentTextBox* textBox = new pdf::PDFPageContentElementEditedContentTextBox();
            textBox->setPageIndex(pageIndex);
            textBox->setRectangle(textInfo.boundingRectangle);
            textBox->setContentId(textInfo.id);

            m_scene.addElement(textBox);
        }*/
    }
}

void EditorPlugin::onDrawSpaceChanged()
{
    updateEditedPages();
}

}
