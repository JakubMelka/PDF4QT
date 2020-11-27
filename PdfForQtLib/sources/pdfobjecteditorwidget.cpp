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

#include "pdfobjecteditorwidget.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>

namespace pdf
{

class PDFObjectEditorWidgetMapper : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent);

    void initialize();

private:
    PDFObjectEditorAbstractModel* m_model;
};

PDFObjectEditorWidget::PDFObjectEditorWidget(PDFObjectEditorAbstractModel* model, QWidget* parent) :
    BaseClass(parent),
    m_mapper(nullptr),
    m_tabWidget(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    layout->addWidget(m_tabWidget);

    m_mapper = new PDFObjectEditorWidgetMapper(model, this);
}

PDFObjectEditorWidgetMapper::PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent) :
    BaseClass(parent),
    m_model(model)
{

}

void PDFObjectEditorWidgetMapper::initialize()
{
    size_t attributeCount = m_model->getAttributeCount();
}


} // namespace pdf
