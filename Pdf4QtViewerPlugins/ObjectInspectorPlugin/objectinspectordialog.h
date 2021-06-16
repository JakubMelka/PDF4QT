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

#ifndef OBJECTINSPECTORDIALOG_H
#define OBJECTINSPECTORDIALOG_H

#include "pdfdocument.h"
#include "pdfobjectutils.h"
#include "pdfcms.h"

#include <QDialog>

namespace Ui
{
class ObjectInspectorDialog;
}

namespace pdfplugin
{
class PDFObjectInspectorTreeItemModel;

class ObjectInspectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ObjectInspectorDialog(const pdf::PDFCMS* cms, const pdf::PDFDocument* document, QWidget* parent);
    virtual ~ObjectInspectorDialog() override;

private:
    void onModeChanged();
    void onPinRequest();
    void onUnpinRequest();
    void onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous);

    Ui::ObjectInspectorDialog* ui;
    const pdf::PDFCMS* m_cms;
    const pdf::PDFDocument* m_document;
    pdf::PDFObjectClassifier m_objectClassifier;
    PDFObjectInspectorTreeItemModel* m_model;
};

}   // namespace pdfplugin

#endif // OBJECTINSPECTORDIALOG_H
