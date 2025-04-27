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

#include "objectinspectorplugin.h"

#include "pdfcms.h"
#include "pdfutils.h"
#include "pdfdrawwidget.h"

#include "objectinspectordialog.h"
#include "objectstatisticsdialog.h"

#include <QAction>

namespace pdfplugin
{

ObjectInspectorPlugin::ObjectInspectorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_objectInspectorAction(nullptr),
    m_objectStatisticsAction(nullptr)
{

}

void ObjectInspectorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_objectInspectorAction = new QAction(QIcon(":/pdfplugins/objectinspector/object-inspector.svg"), tr("Object &Inspector"), this);
    m_objectInspectorAction->setCheckable(false);
    m_objectInspectorAction->setObjectName("actionObjectInspector_ObjectInspector");

    connect(m_objectInspectorAction, &QAction::triggered, this, &ObjectInspectorPlugin::onObjectInspectorTriggered);

    m_objectStatisticsAction = new QAction(QIcon(":/pdfplugins/objectinspector/object-statistics.svg"), tr("Object &Statistics"), this);
    m_objectStatisticsAction->setCheckable(false);
    m_objectStatisticsAction->setObjectName("actionObjectInspector_ObjectStatistics");

    connect(m_objectStatisticsAction, &QAction::triggered, this, &ObjectInspectorPlugin::onObjectStatisticsTriggered);

    updateActions();
}

void ObjectInspectorPlugin::setCMSManager(pdf::PDFCMSManager* manager)
{
    BaseClass::setCMSManager(manager);
}

void ObjectInspectorPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> ObjectInspectorPlugin::getActions() const
{
    return { m_objectInspectorAction, m_objectStatisticsAction };
}

QString ObjectInspectorPlugin::getPluginMenuName() const
{
    return tr("O&bject Inspector");
}

void ObjectInspectorPlugin::onObjectInspectorTriggered()
{
    pdf::PDFCMSPointer cms = m_cmsManager->getCurrentCMS();
    ObjectInspectorDialog dialog(cms.data(), m_document, m_widget);
    dialog.exec();
}

void ObjectInspectorPlugin::onObjectStatisticsTriggered()
{
    ObjectStatisticsDialog dialog(m_document, m_widget);
    dialog.exec();
}

void ObjectInspectorPlugin::updateActions()
{
    m_objectInspectorAction->setEnabled(m_widget && m_document);
    m_objectStatisticsAction->setEnabled(m_widget && m_document);
}

}
