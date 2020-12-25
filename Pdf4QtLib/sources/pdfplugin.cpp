//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfplugin.h"

namespace pdf
{

PDFPlugin::PDFPlugin(QObject* parent) :
    QObject(parent),
    m_widget(nullptr),
    m_cmsManager(nullptr),
    m_document(nullptr)
{

}

void PDFPlugin::setWidget(PDFWidget* widget)
{
    m_widget = widget;
}

void PDFPlugin::setCMSManager(PDFCMSManager* manager)
{
    m_cmsManager = manager;
}

void PDFPlugin::setDocument(const PDFModifiedDocument& document)
{
    m_document = document;
}

std::vector<QAction*> PDFPlugin::getActions() const
{
    return std::vector<QAction*>();
}

PDFPluginInfo PDFPluginInfo::loadFromJson(const QJsonObject* json)
{
    PDFPluginInfo result;

    QJsonObject metadata = json->value("MetaData").toObject();
    result.name = metadata.value(QLatin1String("Name")).toString();
    result.author = metadata.value(QLatin1String("Author")).toString();
    result.version = metadata.value(QLatin1String("Version")).toString();
    result.license = metadata.value(QLatin1String("License")).toString();
    result.description = metadata.value(QLatin1String("Description")).toString();

    return result;
}


}   // namespace pdf
