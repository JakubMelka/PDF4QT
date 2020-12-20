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

#ifndef PDFPLUGIN_H
#define PDFPLUGIN_H

#include "pdfdocument.h"

#include <QObject>
#include <QJsonObject>

#include <vector>

class QAction;

namespace pdf
{
class PDFWidget;

struct Pdf4QtLIBSHARED_EXPORT PDFPluginInfo
{
    QString name;
    QString author;
    QString version;
    QString license;
    QString description;

    static PDFPluginInfo loadFromJson(const QJsonObject* json);
};
using PDFPluginInfos = std::vector<PDFPluginInfo>;

class Pdf4QtLIBSHARED_EXPORT PDFPlugin : public QObject
{
    Q_OBJECT

public:
    explicit PDFPlugin(QObject* parent);

    virtual void setWidget(PDFWidget* widget);
    virtual void setDocument(const PDFModifiedDocument& document);
    virtual std::vector<QAction*> getActions() const;

protected:
    PDFWidget* m_widget;
    PDFDocument* m_document;
};

}   // namespace pdf

#endif // PDFPLUGIN_H
