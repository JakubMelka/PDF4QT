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
#include "pdfpainterutils.h"

#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QStandardPaths>

namespace pdf
{

PDFPageContentEditorEditedItemSettings::PDFPageContentEditorEditedItemSettings(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFPageContentEditorEditedItemSettings)
{
    ui->setupUi(this);
    connect(ui->loadImageButton, &QPushButton::clicked, this, &PDFPageContentEditorEditedItemSettings::selectImage);
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
        ui->tabWidget->addTab(ui->imageTab, tr("Image"));
        m_image = imageElement->getImage();
        setImage(imageElement->getImage());
    }

    if (PDFEditedPageContentElementText* textElement = editedElement->getElement()->asText())
    {
        ui->tabWidget->addTab(ui->textTab, tr("Text"));
        QString text = textElement->getItemsAsText();
        ui->plainTextEdit->setPlainText(text);
    }

    QTransform matrix = editedElement->getElement()->getState().getCurrentTransformationMatrix();
    PDFTransformationDecomposition decomposedTransformation = PDFPainterHelper::decomposeTransform(matrix);

    ui->rotationAngleEdit->setValue(qRadiansToDegrees(decomposedTransformation.rotationAngle));
    ui->scaleInXEdit->setValue(decomposedTransformation.scaleX);
    ui->scaleInYEdit->setValue(decomposedTransformation.scaleY);
    ui->shearFactorEdit->setValue(decomposedTransformation.shearFactor);
    ui->translateInXEdit->setValue(decomposedTransformation.translateX);
    ui->translateInYEdit->setValue(decomposedTransformation.translateY);

    ui->tabWidget->addTab(ui->transformationTab, tr("Transformation"));
}

void PDFPageContentEditorEditedItemSettings::saveToElement(PDFPageContentElementEdited* editedElement)
{
    if (PDFEditedPageContentElementImage* imageElement = editedElement->getElement()->asImage())
    {
        imageElement->setImage(m_image);
        imageElement->setImageObject(PDFObject());
    }
}

static int PDF_gcd(int a, int b)
{
    if (b == 0)
    {
        return a;
    }

    return PDF_gcd(b, a % b);
}

void PDFPageContentEditorEditedItemSettings::setImage(QImage image)
{
    QSize imageSize = QSize(200, 200);

    int width = image.width();
    int height = image.height();

    int n = width;
    int d = height;

    int divisor = PDF_gcd(n, d);
    if (divisor > 1)
    {
        n /= divisor;
        d /= divisor;
    }

    ui->imageWidthEdit->setText(QString("%1 px").arg(width));
    ui->imageHeightEdit->setText(QString("%1 px").arg(height));
    ui->imageRatioEdit->setText(QString("%1 : %2").arg(n).arg(d));

    image.setDevicePixelRatio(this->devicePixelRatioF());
    image = image.scaled(imageSize * this->devicePixelRatioF(), Qt::KeepAspectRatio);

    ui->imageLabel->setPixmap(QPixmap::fromImage(image));
    ui->imageLabel->setFixedSize(PDFWidgetUtils::scaleDPI(this, imageSize));
}

void PDFPageContentEditorEditedItemSettings::selectImage()
{
    QString imageDirectory;

    QStringList pictureDirectiories = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
    if (!pictureDirectiories.isEmpty())
    {
        imageDirectory = pictureDirectiories.last();
    }
    else
    {
        imageDirectory = QDir::currentPath();
    }

    QList<QByteArray> mimeTypes = QImageReader::supportedMimeTypes();
    QStringList mimeTypeFilters;
    for (const QByteArray& mimeType : mimeTypes)
    {
        mimeTypeFilters.append(mimeType);
    }

    QFileDialog dialog(this, tr("Select Image"));
    dialog.setDirectory(imageDirectory);
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/svg+xml");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);

    if (dialog.exec() == QFileDialog::Accepted)
    {
        QString fileName = dialog.selectedFiles().constFirst();
        QImageReader reader(fileName);
        QImage image = reader.read();

        if (!image.isNull())
        {
            setImage(image);
            m_image = std::move(image);
        }
    }
}

}   // namespace pdf
