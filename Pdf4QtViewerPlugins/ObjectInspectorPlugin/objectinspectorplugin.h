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

#ifndef OBJECTINSPECTORPLUGIN_H
#define OBJECTINSPECTORPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

namespace pdfplugin
{

class ObjectInspectorPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.ObjectInspectorPlugin" FILE "ObjectInspectorPlugin.json")

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
    void onObjectStatisticsTriggered();

    void updateActions();

    QAction* m_objectInspectorAction;
    QAction* m_objectStatisticsAction;
};

}   // namespace pdfplugin

#endif // OBJECTINSPECTORPLUGIN_H
