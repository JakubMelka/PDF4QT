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

#include <QAction>

namespace pdfplugin
{

SignaturePlugin::SignaturePlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_scene(nullptr)
{

}

void SignaturePlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/signaturetool/activate.svg"), tr("Activate signature creator"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-text.svg"), tr("Create Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-freehand-curve.svg"), tr("Create Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-yes-mark.svg"), tr("Create Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-no-mark.svg"), tr("Create Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-rectangle.svg"), tr("Create Rectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-rounded-rectangle.svg"), tr("Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-horizontal-line.svg"), tr("Create Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-vertical-line.svg"), tr("Create Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-line.svg"), tr("Create Line"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-dot.svg"), tr("Create Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/signaturetool/create-svg-image.svg"), tr("Create SVG Image"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/signaturetool/clear.svg"), tr("Clear All Graphics"), this);
    QAction* signElectronicallyAction = new QAction(QIcon(":/pdfplugins/signaturetool/sign-electronically.svg"), tr("Sign Electronically"), this);
    QAction* signDigitallyAction = new QAction(QIcon(":/pdfplugins/signaturetool/sign-digitally.svg"), tr("Sign Digitally With Certificate"), this);
    QAction* certificatesAction = new QAction(QIcon(":/pdfplugins/signaturetool/certificates.svg"), tr("Certificates Manager"), this);

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

    m_tools[AcceptMarkTool] = new pdf::PDFCreatePCElementSvgTool(widget->getDrawWidgetProxy(), &m_scene, createAcceptMarkAction, acceptMarkContent, this);
    m_tools[RejectMarkTool] = new pdf::PDFCreatePCElementSvgTool(widget->getDrawWidgetProxy(), &m_scene, createRejectMarkAction, rejectMarkContent, this);
    m_tools[RectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRectangleAction, false, this);
    m_tools[RoundedRectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRoundedRectangleAction, true, this);
    m_tools[HorizontalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createHorizontalLineAction, true, false, this);
    m_tools[VerticalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createVerticalLineAction, false, true, this);
    m_tools[LineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createLineAction, false, false, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        toolManager->addTool(tool);
    }

    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &SignaturePlugin::onSceneChanged);
    connect(clearAction, &QAction::triggered, &m_scene, &pdf::PDFPageContentScene::clear);
    connect(activateAction, &QAction::triggered, this, &SignaturePlugin::setActive);

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
    result.push_back(m_actions[Text]);
    result.push_back(m_actions[FreehandCurve]);
    result.push_back(m_actions[AcceptMark]);
    result.push_back(m_actions[RejectMark]);
    result.push_back(m_actions[Rectangle]);
    result.push_back(m_actions[RoundedRectangle]);
    result.push_back(m_actions[HorizontalLine]);
    result.push_back(m_actions[VerticalLine]);
    result.push_back(m_actions[Line]);
    result.push_back(m_actions[Dot]);
    result.push_back(m_actions[Clear]);
    result.push_back(nullptr);
    result.push_back(m_actions[SignElectronically]);
    result.push_back(m_actions[SignDigitally]);
    result.push_back(m_actions[Certificates]);

    return result;
}

void SignaturePlugin::onSceneChanged()
{
    updateActions();
    updateGraphics();
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
                if (it == m_tools.cend())
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
    signDigitallyAction->setEnabled(isSceneNonempty);
}

void SignaturePlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

}
