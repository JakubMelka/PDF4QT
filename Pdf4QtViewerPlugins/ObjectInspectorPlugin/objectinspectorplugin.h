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

#ifndef OBJECTINSPECTORPLUGIN_H
#define OBJECTINSPECTORPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

namespace pdfplugin
{

class ObjectInspectorPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Pdf4Qt.ObjectInspectorPlugin" FILE "ObjectInspectorPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    ObjectInspectorPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setCMSManager(pdf::PDFCMSManager* manager) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void onObjectInspectorTriggered();

    void updateActions();

    QAction* m_objectInspectorAction;
};

}   // namespace pdfplugin

#endif // OBJECTINSPECTORPLUGIN_H
