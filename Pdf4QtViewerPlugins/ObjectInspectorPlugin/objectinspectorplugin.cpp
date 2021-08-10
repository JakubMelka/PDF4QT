//    Copyright (C) 2021 Jakub Melka
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

    m_objectInspectorAction = new QAction(QIcon(":/pdfplugins/objectinspector/object-inspector.svg"), tr("Object Inspector"), this);
    m_objectInspectorAction->setCheckable(false);
    m_objectInspectorAction->setObjectName("actionObjectInspector_ObjectInspector");

    connect(m_objectInspectorAction, &QAction::triggered, this, &ObjectInspectorPlugin::onObjectInspectorTriggered);

    m_objectStatisticsAction = new QAction(QIcon(":/pdfplugins/objectinspector/object-statistics.svg"), tr("Object Statistics"), this);
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
