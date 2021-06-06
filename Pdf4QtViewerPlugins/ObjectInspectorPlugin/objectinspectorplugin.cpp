//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "objectinspectorplugin.h"

#include "pdfcms.h"
#include "pdfutils.h"
#include "pdfdrawwidget.h"

#include "objectinspectordialog.h"

#include <QAction>

namespace pdfplugin
{

ObjectInspectorPlugin::ObjectInspectorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_objectInspectorAction(nullptr)
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
    return { m_objectInspectorAction };
}

void ObjectInspectorPlugin::onObjectInspectorTriggered()
{
    ObjectInspectorDialog dialog(m_document, m_widget);
    dialog.exec();
}

void ObjectInspectorPlugin::updateActions()
{
    m_objectInspectorAction->setEnabled(m_widget && m_document);
}

}
