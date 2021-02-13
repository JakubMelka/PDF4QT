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

#include <QCloseEvent>
#include <QtConcurrent/QtConcurrent>

namespace pdfplugin
{

OutputPreviewDialog::OutputPreviewDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::OutputPreviewDialog),
    m_inkMapper(document),
    m_document(document),
    m_widget(widget),
    m_needUpdateImage(false),
    m_futureWatcher(nullptr)
{
    ui->setupUi(this);

    ui->pageIndexScrollBar->setMinimum(1);
    ui->pageIndexScrollBar->setValue(1);
    ui->pageIndexScrollBar->setMaximum(int(document->getCatalog()->getPageCount()));

    m_inkMapper.createSpotColors(true);
    updatePageImage();
}

OutputPreviewDialog::~OutputPreviewDialog()
{
    delete ui;
}

void OutputPreviewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePageImage();
}

void OutputPreviewDialog::closeEvent(QCloseEvent* event)
{
    if (!isRenderingDone())
    {
        event->ignore();
    }
}

void OutputPreviewDialog::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);

    updatePageImage();
}

void OutputPreviewDialog::updatePageImage()
{
    if (!isRenderingDone())
    {
        m_needUpdateImage = true;
        return;
    }

    m_needUpdateImage = false;

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(ui->pageIndexScrollBar->value() - 1);
    if (!page)
    {
        ui->imageLabel->setPixmap(QPixmap());
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    QSize renderSize = ui->imageLabel->size();
    auto renderImage = [this, page, renderSize]() -> RenderedImage
    {
        return renderPage(page, renderSize);
    };

    m_future = QtConcurrent::run(renderImage);
    m_futureWatcher = new QFutureWatcher<RenderedImage>();
    connect(m_futureWatcher, &QFutureWatcher<RenderedImage>::finished, this, &OutputPreviewDialog::onPageImageRendered);
    m_futureWatcher->setFuture(m_future);
}

OutputPreviewDialog::RenderedImage OutputPreviewDialog::renderPage(const pdf::PDFPage* page, QSize renderSize)
{
    RenderedImage result;

    QRectF pageRect = page->getRotatedMediaBox();
    QSizeF pageSize = pageRect.size();
    pageSize.scale(renderSize.width(), renderSize.height(), Qt::KeepAspectRatio);
    QSize imageSize = pageSize.toSize();

    if (!imageSize.isValid())
    {
        return result;
    }

    pdf::PDFTransparencyRendererSettings settings;

    // Jakub Melka: debug is very slow, use multithreading
#ifdef QT_DEBUG
    settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::MultithreadedPathSampler, true);
#endif

    QMatrix pagePointToDevicePoint = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), imageSize));
    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFTransparencyRenderer renderer(page, m_document, proxy->getFontCache(), cms.data(), proxy->getOptionalContentActivity(), &m_inkMapper, settings, pagePointToDevicePoint);

    renderer.beginPaint(imageSize);
    renderer.processContents();
    renderer.endPaint();

    QImage image = renderer.toImage(false, true, pdf::PDFRGB{ 1.0f, 1.0f, 1.0f });

    result.image = qMove(image);
    return result;
}

void OutputPreviewDialog::onPageImageRendered()
{
    QApplication::restoreOverrideCursor();

    RenderedImage result = m_future.result();
    m_future = QFuture<RenderedImage>();
    m_futureWatcher->deleteLater();
    m_futureWatcher = nullptr;

    ui->imageLabel->setPixmap(QPixmap::fromImage(result.image));

    if (m_needUpdateImage)
    {
        updatePageImage();
    }
}

bool OutputPreviewDialog::isRenderingDone() const
{
    return !(m_futureWatcher && m_futureWatcher->isRunning());
}

} // namespace pdfplugin
