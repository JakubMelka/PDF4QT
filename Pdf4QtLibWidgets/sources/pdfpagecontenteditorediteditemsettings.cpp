//    Copyright (C) 2024 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpagecontenteditorediteditemsettings.h"
#include "ui_pdfpagecontenteditorediteditemsettings.h"

#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfwidgetutils.h"

namespace pdf
{

PDFPageContentEditorEditedItemSettings::PDFPageContentEditorEditedItemSettings(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFPageContentEditorEditedItemSettings)
{
    ui->setupUi(this);
}

PDFPageContentEditorEditedItemSettings::~PDFPageContentEditorEditedItemSettings()
{
    delete ui;
}

void PDFPageContentEditorEditedItemSettings::loadFromElement(PDFPageContentElementEdited* editedElement)
{
    const PDFEditedPageContentElement* contentElement = editedElement->getElement();

    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->imageTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->textTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->styleTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->transformationTab));

    if (const PDFEditedPageContentElementImage* imageElement = contentElement->asImage())
    {
        QSize imageSize = QSize(200, 200);

        QImage image = imageElement->getImage();
        image.setDevicePixelRatio(this->devicePixelRatioF());
        image = image.scaled(imageSize * this->devicePixelRatioF(), Qt::KeepAspectRatio);

        ui->tabWidget->addTab(ui->imageTab, tr("Image"));
        ui->imageLabel->setPixmap(QPixmap::fromImage(image));
        ui->imageLabel->setFixedSize(PDFWidgetUtils::scaleDPI(this, imageSize));
    }
}

}   // namespace pdf
