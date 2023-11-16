//    Copyright (C) 2023 Jakub Melka
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

#include "pdfcreateBitonaldocumentdialog.h"
#include "ui_pdfcreateBitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfimage.h"
#include "pdfdbgheap.h"
#include "pdfexception.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>
#include <QListWidget>

namespace pdfviewer
{

PDFCreateBitonalDocumentDialog::PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document,
                                                               const pdf::PDFCMS* cms,
                                                               QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_cms(cms),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_processed(false)
{
    ui->setupUi(this);

    m_classifier.classify(document);
    m_imageReferences = m_classifier.getObjectsByType(pdf::PDFObjectClassifier::Image);

    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Process"), QDialogButtonBox::ActionRole);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->automaticThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);
    connect(ui->manualThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(640, 380));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    loadImages();
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFCreateBitonalDocumentDialog::createBitonalDocument()
{

}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this]() { createBitonalDocument(); });
    updateUi();
}

void PDFCreateBitonalDocumentDialog::loadImages()
{
    QSize iconSize(QSize(256, 256));
    ui->imageListWidget->setIconSize(iconSize);
    QSize imageSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    pdf::PDFCMSGeneric genericCms;

    for (pdf::PDFObjectReference reference : m_imageReferences)
    {
        std::optional<pdf::PDFImage> pdfImage;
        pdf::PDFObject imageObject = m_document->getObjectByReference(reference);
        pdf::PDFRenderErrorReporterDummy errorReporter;

        if (!imageObject.isStream())
        {
            // Image is not stream
            continue;
        }

        const pdf::PDFStream* stream = imageObject.getStream();
        try
        {
            pdf::PDFColorSpacePointer colorSpace;
            const pdf::PDFDictionary* streamDictionary = stream->getDictionary();
            if (streamDictionary->hasKey("ColorSpace"))
            {
                const pdf::PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
                if (colorSpaceObject.isName() || colorSpaceObject.isArray())
                {
                    pdf::PDFDictionary dummyDictionary;
                    colorSpace = pdf::PDFAbstractColorSpace::createColorSpace(&dummyDictionary, m_document, colorSpaceObject);
                }
            }
            pdfImage.emplace(pdf::PDFImage::createImage(m_document,
                                                        stream,
                                                        colorSpace,
                                                        false,
                                                        pdf::RenderingIntent::Perceptual,
                                                        &errorReporter));
        }
        catch (pdf::PDFException)
        {
            continue;
        }

        QImage image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);

        if (image.isNull())
        {
            continue;
        }

        QListWidgetItem* item = new QListWidgetItem(ui->imageListWidget);
        QWidget* widget = new QWidget;
        QHBoxLayout* layout = new QHBoxLayout(widget);
        QCheckBox* checkbox = new QCheckBox(widget);
        layout->addWidget(checkbox);

        image = image.scaled(imageSize.width(), imageSize.height(), Qt::KeepAspectRatio, Qt::FastTransformation);
        item->setIcon(QIcon(QPixmap::fromImage(image)));

        ui->imageListWidget->setItemWidget(item, widget);
    }
}

void PDFCreateBitonalDocumentDialog::updateUi()
{
    ui->thresholdEditBox->setEnabled(ui->manualThresholdRadioButton->isChecked());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_processed && !m_conversionInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_conversionInProgress);
    m_createBitonalDocumentButton->setEnabled(!m_conversionInProgress);
}

}   // namespace pdfviewer
