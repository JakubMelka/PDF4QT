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

#include "objectinspectordialog.h"
#include "ui_objectinspectordialog.h"

#include "pdfwidgetutils.h"
#include "pdfobjectinspectortreeitemmodel.h"

#include <QSplitter>

namespace pdfplugin
{

ObjectInspectorDialog::ObjectInspectorDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::ObjectInspectorDialog),
    m_document(document),
    m_model(nullptr)
{
    ui->setupUi(this);

    ui->modeComboBox->addItem(tr("Document"), int(PDFObjectInspectorTreeItemModel::Document));
    ui->modeComboBox->addItem(tr("Pages"), int(PDFObjectInspectorTreeItemModel::Page));
    ui->modeComboBox->addItem(tr("Images"), int(PDFObjectInspectorTreeItemModel::Image));
    ui->modeComboBox->addItem(tr("Object List"), int(PDFObjectInspectorTreeItemModel::List));

    ui->modeComboBox->setCurrentIndex(ui->modeComboBox->findData(int(PDFObjectInspectorTreeItemModel::Document)));
    connect(ui->modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectInspectorDialog::onModeChanged);

    m_model = new PDFObjectInspectorTreeItemModel(this);
    onModeChanged();
    m_model->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), nullptr, pdf::PDFModifiedDocument::Reset));

    ui->objectTreeView->setRootIsDecorated(true);
    ui->objectTreeView->setModel(m_model);

    QSplitter* splitter = new QSplitter(this);
    splitter->addWidget(ui->objectTreeView);
    splitter->addWidget(ui->tabWidget);

    ui->objectTreeView->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 200));
    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));
}

ObjectInspectorDialog::~ObjectInspectorDialog()
{
    delete ui;
}

void ObjectInspectorDialog::onModeChanged()
{
    const PDFObjectInspectorTreeItemModel::Mode mode = static_cast<const PDFObjectInspectorTreeItemModel::Mode>(ui->modeComboBox->currentData().toInt());
    m_model->setMode(mode);
}

}   // namespace pdfplugin
