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

#include "objectinspectordialog.h"
#include "ui_objectinspectordialog.h"

#include "pdfwidgetutils.h"
#include "pdfobjectinspectortreeitemmodel.h"

#include <QSplitter>

namespace pdfplugin
{

ObjectInspectorDialog::ObjectInspectorDialog(const pdf::PDFCMS* cms, const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::ObjectInspectorDialog),
    m_cms(cms),
    m_document(document),
    m_model(nullptr),
    m_viewerWidget(new ObjectViewerWidget(this))
{
    ui->setupUi(this);

    m_objectClassifier.classify(document);

    m_viewerWidget->setCms(cms);
    m_viewerWidget->setDocument(document);
    ui->currentObjectTabLayout->addWidget(m_viewerWidget);

    ui->modeComboBox->addItem(tr("Document"), int(PDFObjectInspectorTreeItemModel::Document));
    ui->modeComboBox->addItem(tr("Pages"), int(PDFObjectInspectorTreeItemModel::Page));

    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::ContentStream))
    {
        ui->modeComboBox->addItem(tr("Content streams"), int(PDFObjectInspectorTreeItemModel::ContentStream));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::GraphicState))
    {
        ui->modeComboBox->addItem(tr("Graphic states"), int(PDFObjectInspectorTreeItemModel::GraphicState));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::ColorSpace))
    {
        ui->modeComboBox->addItem(tr("Color spaces"), int(PDFObjectInspectorTreeItemModel::ColorSpace));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Pattern))
    {
        ui->modeComboBox->addItem(tr("Patterns"), int(PDFObjectInspectorTreeItemModel::Pattern));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Shading))
    {
        ui->modeComboBox->addItem(tr("Shadings"), int(PDFObjectInspectorTreeItemModel::Shading));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Image))
    {
        ui->modeComboBox->addItem(tr("Images"), int(PDFObjectInspectorTreeItemModel::Image));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Form))
    {
        ui->modeComboBox->addItem(tr("Forms"), int(PDFObjectInspectorTreeItemModel::Form));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Font))
    {
        ui->modeComboBox->addItem(tr("Fonts"), int(PDFObjectInspectorTreeItemModel::Font));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Action))
    {
        ui->modeComboBox->addItem(tr("Actions"), int(PDFObjectInspectorTreeItemModel::Action));
    }
    if (m_objectClassifier.hasType(pdf::PDFObjectClassifier::Annotation))
    {
        ui->modeComboBox->addItem(tr("Annotations"), int(PDFObjectInspectorTreeItemModel::Annotation));
    }

    ui->modeComboBox->addItem(tr("Object List"), int(PDFObjectInspectorTreeItemModel::List));

    ui->modeComboBox->setCurrentIndex(ui->modeComboBox->findData(int(PDFObjectInspectorTreeItemModel::Document)));
    connect(ui->modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectInspectorDialog::onModeChanged);

    m_model = new PDFObjectInspectorTreeItemModel(&m_objectClassifier, this);
    onModeChanged();
    m_model->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), nullptr, pdf::PDFModifiedDocument::Reset));

    ui->objectTreeView->setRootIsDecorated(true);
    ui->objectTreeView->setModel(m_model);

    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setCollapsible(0, true);
    ui->splitter->setCollapsible(1, true);
    ui->splitter->setSizes(QList<int>() << pdf::PDFWidgetUtils::scaleDPI_x(this, 300) << pdf::PDFWidgetUtils::scaleDPI_x(this, 200));

    connect(ui->objectTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &ObjectInspectorDialog::onCurrentIndexChanged);
    connect(m_viewerWidget, &ObjectViewerWidget::pinRequest, this, &ObjectInspectorDialog::onPinRequest);
    connect(m_viewerWidget, &ObjectViewerWidget::unpinRequest, this, &ObjectInspectorDialog::onUnpinRequest);

    ui->objectTreeView->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 200));
    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));
    pdf::PDFWidgetUtils::style(this);
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

void ObjectInspectorDialog::onPinRequest()
{
    ObjectViewerWidget* source = qobject_cast<ObjectViewerWidget*>(sender());

    if (!source || source != m_viewerWidget)
    {
        return;
    }

    ObjectViewerWidget* cloned = m_viewerWidget->clone(true, this);
    connect(cloned, &ObjectViewerWidget::pinRequest, this, &ObjectInspectorDialog::onPinRequest);
    connect(cloned, &ObjectViewerWidget::unpinRequest, this, &ObjectInspectorDialog::onUnpinRequest);
    ui->tabWidget->addTab(cloned, cloned->getTitleText());
}

void ObjectInspectorDialog::onUnpinRequest()
{
    ObjectViewerWidget* source = qobject_cast<ObjectViewerWidget*>(sender());

    if (!source || source == m_viewerWidget)
    {
        return;
    }

    ui->tabWidget->removeTab(ui->tabWidget->indexOf(source));
    source->deleteLater();
}

void ObjectInspectorDialog::onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);

    pdf::PDFObject object = m_model->getObjectFromIndex(current);
    pdf::PDFObjectReference reference = m_model->getObjectReferenceFromIndex(current);
    bool isRoot = m_model->isRootObject(current);

    if (!isRoot && object.isReference())
    {
        reference = object.getReference();
        object = m_document->getObjectByReference(reference);
        isRoot = true;
    }

    m_viewerWidget->setData(reference, qMove(object), isRoot);
}

}   // namespace pdfplugin
