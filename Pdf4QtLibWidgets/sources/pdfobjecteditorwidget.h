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

#ifndef PDFOBJECTEDITORWIDGET_H
#define PDFOBJECTEDITORWIDGET_H

#include "pdfobjecteditormodel.h"

#include <QWidget>
#include <QDialog>

class QTabWidget;
class QDialogButtonBox;

namespace pdf
{
class PDFObjectEditorWidgetMapper;

enum class EditObjectType
{
    Annotation
};

class PDFObjectEditorWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFObjectEditorWidget(EditObjectType type, QWidget* parent);

    void setObject(PDFObject object);
    PDFObject getObject();

private:
    PDFObjectEditorWidgetMapper* m_mapper;
    QTabWidget* m_tabWidget;
};

class PDFEditObjectDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFEditObjectDialog(EditObjectType type, QWidget* parent);

    void setObject(PDFObject object);
    PDFObject getObject();

private:
    PDFObjectEditorWidget* m_widget;
    QDialogButtonBox* m_buttonBox;
};

} // namespace pdf

#endif // PDFOBJECTEDITORWIDGET_H
