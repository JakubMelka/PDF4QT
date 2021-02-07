//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "outputpreviewdialog.h"
#include "ui_outputpreviewdialog.h"

#include "pdfcms.h"
#include "pdfrenderer.h"
#include "pdfdrawspacecontroller.h"

namespace pdfplugin
{

OutputPreviewDialog::OutputPreviewDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::OutputPreviewDialog),
    m_inkMapper(document),
    m_document(document),
    m_widget(widget)
{
    ui->setupUi(this);

    ui->pageIndexScrollBar->setMinimum(1);
    ui->pageIndexScrollBar->setValue(1);
    ui->pageIndexScrollBar->setMaximum(int(document->getCatalog()->getPageCount()));

    m_inkMapper.createSpotColors(true);
    updateImage();
}

OutputPreviewDialog::~OutputPreviewDialog()
{
    delete ui;
}

void OutputPreviewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateImage();
}

void OutputPreviewDialog::updateImage()
{
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(ui->pageIndexScrollBar->value() - 1);
    if (!page)
    {
        ui->imageLabel->setPixmap(QPixmap());
    }

    QRectF pageRect = page->getRotatedMediaBox();
    QSizeF pageSize = pageRect.size();
    pageSize.scale(ui->imageLabel->width(), ui->imageLabel->height(), Qt::KeepAspectRatio);
    QSize imageSize = pageSize.toSize();

    if (!imageSize.isValid())
    {
        ui->imageLabel->setPixmap(QPixmap());
    }

    QMatrix pagePointToDevicePoint = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), imageSize));
    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFTransparencyRenderer renderer(page, m_document, proxy->getFontCache(), cms.data(), proxy->getOptionalContentActivity(), &m_inkMapper, pagePointToDevicePoint);

    renderer.beginPaint(imageSize);
    renderer.processContents();
    renderer.endPaint();

    QImage image = renderer.toImage(false);
    ui->imageLabel->setPixmap(QPixmap::fromImage(image));
}

} // namespace pdfplugin
