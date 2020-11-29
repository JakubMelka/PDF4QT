//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

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

private:
    PDFObjectEditorWidget* m_widget;
    QDialogButtonBox* m_buttonBox;
};

} // namespace pdf

#endif // PDFOBJECTEDITORWIDGET_H
