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

#include "objectviewerwidget.h"
#include "ui_objectviewerwidget.h"

namespace pdfplugin
{

ObjectViewerWidget::ObjectViewerWidget(QWidget *parent) :
    ObjectViewerWidget(false, parent)
{

}

ObjectViewerWidget::ObjectViewerWidget(bool isPinned, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ObjectViewerWidget),
    m_isPinned(isPinned),
    m_isRootObject(false)
{
    ui->setupUi(this);

    connect(ui->pinButton, &QPushButton::clicked, this, &ObjectViewerWidget::pinRequest);
    connect(ui->unpinButton, &QPushButton::clicked, this, &ObjectViewerWidget::pinRequest);

    updateUi();
    updatePinnedUi();
}

ObjectViewerWidget::~ObjectViewerWidget()
{
    delete ui;
}

void ObjectViewerWidget::setPinned(bool isPinned)
{
    if (m_isPinned != isPinned)
    {
        m_isPinned = isPinned;
        updatePinnedUi();
    }
}

void ObjectViewerWidget::setData(pdf::PDFObjectReference currentReference, pdf::PDFObject currentObject, bool isRootObject)
{
    if (m_currentReference != currentReference ||
        m_currentObject != currentObject ||
        m_isRootObject = isRootObject)
    {
        m_currentReference = currentReference;
        m_currentObject = currentObject;
        m_isRootObject = isRootObject;

        updateUi();
    }
}

void ObjectViewerWidget::updatePinnedUi()
{
    ui->pinButton->setEnabled(!m_isPinned);
    ui->unpinButton->setEnabled(m_isPinned);
}

}   // namespace pdfplugin
