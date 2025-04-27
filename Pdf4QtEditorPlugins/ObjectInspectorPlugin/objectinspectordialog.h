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

#ifndef OBJECTINSPECTORDIALOG_H
#define OBJECTINSPECTORDIALOG_H

#include "pdfdocument.h"
#include "pdfobjectutils.h"
#include "pdfcms.h"
#include "objectviewerwidget.h"

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
    ObjectViewerWidget* m_viewerWidget;
};

}   // namespace pdfplugin

#endif // OBJECTINSPECTORDIALOG_H
