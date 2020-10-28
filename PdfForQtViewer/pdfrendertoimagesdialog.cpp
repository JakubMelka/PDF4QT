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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfrendertoimagesdialog.h"
#include "ui_pdfrendertoimagesdialog.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfoptionalcontent.h"
#include "pdfdrawspacecontroller.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QImageWriter>

namespace pdfviewer
{

PDFRenderToImagesDialog::PDFRenderToImagesDialog(const pdf::PDFDocument* document,
                                                 pdf::PDFDrawWidgetProxy* proxy,
                                                 pdf::PDFProgress* progress,
                                                 QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFRenderToImagesDialog),
    m_document(document),
    m_proxy(proxy),
    m_progress(progress),
    m_imageExportSettings(document),
    m_isLoadingData(false),
    m_optionalContentActivity(nullptr),
    m_rasterizerPool(nullptr)
{
    ui->setupUi(this);

    qRegisterMetaType<pdf::PDFRenderError>("PDFRenderError");

    // Load image formats
    for (const QByteArray& format : m_imageWriterSettings.getFormats())
    {
        ui->formatComboBox->addItem(QString::fromLatin1(format), format);
    }

    connect(ui->pagesAllButton, &QRadioButton::clicked, this, &PDFRenderToImagesDialog::onPagesButtonClicked);
    connect(ui->pagesSelectButton, &QRadioButton::clicked, this, &PDFRenderToImagesDialog::onPagesButtonClicked);
    connect(ui->selectedPagesEdit, &QLineEdit::textChanged, this, &PDFRenderToImagesDialog::onSelectedPagesChanged);
    connect(ui->directoryEdit, &QLineEdit::textChanged, this, &PDFRenderToImagesDialog::onDirectoryChanged);
    connect(ui->fileTemplateEdit, &QLineEdit::textChanged, this, &PDFRenderToImagesDialog::onFileTemplateChanged);
    connect(ui->resolutionDPIButton, &QRadioButton::clicked, this, &PDFRenderToImagesDialog::onResolutionButtonClicked);
    connect(ui->resolutionPixelsButton, &QRadioButton::clicked, this, &PDFRenderToImagesDialog::onResolutionButtonClicked);
    connect(ui->resolutionDPIEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFRenderToImagesDialog::onResolutionDPIChanged);
    connect(ui->resolutionPixelsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFRenderToImagesDialog::onResolutionPixelsChanged);
    connect(ui->formatComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFRenderToImagesDialog::onFormatChanged);
    connect(ui->subtypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFRenderToImagesDialog::onSubtypeChanged);
    connect(ui->compressionEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFRenderToImagesDialog::onCompressionChanged);
    connect(ui->qualityEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFRenderToImagesDialog::onQualityChanged);
    connect(ui->gammaEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFRenderToImagesDialog::onGammaChanged);
    connect(ui->optimizedWriteCheckBox, &QCheckBox::clicked, this, &PDFRenderToImagesDialog::onOptimizedWriteChanged);
    connect(ui->progressiveScanWriteCheckBox, &QCheckBox::clicked, this, &PDFRenderToImagesDialog::onProgressiveScanWriteChanged);
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &PDFRenderToImagesDialog::onRenderingFinished);

    ui->resolutionDPIEdit->setRange(pdf::PDFPageImageExportSettings::getMinDPIResolution(), pdf::PDFPageImageExportSettings::getMaxDPIResolution());
    ui->resolutionPixelsEdit->setRange(pdf::PDFPageImageExportSettings::getMinPixelResolution(), pdf::PDFPageImageExportSettings::getMaxPixelResolution());

    loadImageWriterSettings();
    loadImageExportSettings();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1000, 600));
}

PDFRenderToImagesDialog::~PDFRenderToImagesDialog()
{
    delete ui;

    Q_ASSERT(!m_optionalContentActivity);
    Q_ASSERT(!m_rasterizerPool);
}

void PDFRenderToImagesDialog::loadImageWriterSettings()
{
    if (m_isLoadingData)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isLoadingData, true);
    ui->formatComboBox->setCurrentIndex(ui->formatComboBox->findData(m_imageWriterSettings.getCurrentFormat()));

    ui->subtypeComboBox->setUpdatesEnabled(false);
    ui->subtypeComboBox->clear();
    for (const QByteArray& subtype : m_imageWriterSettings.getSubtypes())
    {
        ui->subtypeComboBox->addItem(QString::fromLatin1(subtype), subtype);
    }
    ui->subtypeComboBox->setCurrentIndex(ui->subtypeComboBox->findData(m_imageWriterSettings.getCurrentSubtype()));
    ui->subtypeComboBox->setUpdatesEnabled(true);

    ui->compressionEdit->setValue(m_imageWriterSettings.getCompression());
    ui->qualityEdit->setValue(m_imageWriterSettings.getQuality());
    ui->gammaEdit->setValue(m_imageWriterSettings.getGamma());
    ui->optimizedWriteCheckBox->setChecked(m_imageWriterSettings.hasOptimizedWrite());
    ui->progressiveScanWriteCheckBox->setChecked(m_imageWriterSettings.hasProgressiveScanWrite());

    ui->subtypeComboBox->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::SupportedSubTypes));
    ui->compressionEdit->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::CompressionRatio));
    ui->qualityEdit->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::Quality));
    ui->gammaEdit->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::Gamma));
    ui->optimizedWriteCheckBox->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::OptimizedWrite));
    ui->progressiveScanWriteCheckBox->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::ProgressiveScanWrite));
}

void PDFRenderToImagesDialog::loadImageExportSettings()
{
    if (m_isLoadingData)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isLoadingData, true);

    const pdf::PDFPageImageExportSettings::PageSelectionMode pageSelectionMode = m_imageExportSettings.getPageSelectionMode();
    ui->pagesAllButton->setChecked(pageSelectionMode == pdf::PDFPageImageExportSettings::PageSelectionMode::All);
    ui->pagesSelectButton->setChecked(pageSelectionMode == pdf::PDFPageImageExportSettings::PageSelectionMode::Selection);

    if (pageSelectionMode == pdf::PDFPageImageExportSettings::PageSelectionMode::Selection)
    {
        ui->selectedPagesEdit->setEnabled(true);
        ui->selectedPagesEdit->setText(m_imageExportSettings.getPageSelection());
    }
    else
    {
        ui->selectedPagesEdit->setEnabled(false);
        ui->selectedPagesEdit->setText(QString());
    }

    ui->directoryEdit->setText(m_imageExportSettings.getDirectory());
    ui->fileTemplateEdit->setText(m_imageExportSettings.getFileTemplate());

    const pdf::PDFPageImageExportSettings::ResolutionMode resolutionMode = m_imageExportSettings.getResolutionMode();
    ui->resolutionDPIButton->setChecked(resolutionMode == pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
    ui->resolutionPixelsButton->setChecked(resolutionMode == pdf::PDFPageImageExportSettings::ResolutionMode::Pixels);
    ui->resolutionDPIEdit->setValue(m_imageExportSettings.getDpiResolution());
    ui->resolutionPixelsEdit->setValue(m_imageExportSettings.getPixelResolution());
    ui->resolutionDPIEdit->setEnabled(resolutionMode == pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
    ui->resolutionPixelsEdit->setEnabled(resolutionMode == pdf::PDFPageImageExportSettings::ResolutionMode::Pixels);
}

void PDFRenderToImagesDialog::onFormatChanged()
{
    m_imageWriterSettings.selectFormat(ui->formatComboBox->currentData().toByteArray());
    loadImageWriterSettings();
}

void PDFRenderToImagesDialog::onSubtypeChanged()
{
    m_imageWriterSettings.setCurrentSubtype(ui->subtypeComboBox->currentData().toByteArray());
}

void PDFRenderToImagesDialog::onPagesButtonClicked(bool checked)
{
    if (checked)
    {
        const pdf::PDFPageImageExportSettings::PageSelectionMode pageSelectionMode = (sender() == ui->pagesAllButton) ? pdf::PDFPageImageExportSettings::PageSelectionMode::All
                                                                                                                      : pdf::PDFPageImageExportSettings::PageSelectionMode::Selection;
        m_imageExportSettings.setPageSelectionMode(pageSelectionMode);
        loadImageExportSettings();
    }
}

void PDFRenderToImagesDialog::onSelectedPagesChanged(const QString& text)
{
    m_imageExportSettings.setPageSelection(text);
}

void PDFRenderToImagesDialog::onDirectoryChanged(const QString& text)
{
    m_imageExportSettings.setDirectory(text);
}

void PDFRenderToImagesDialog::onFileTemplateChanged(const QString& text)
{
    m_imageExportSettings.setFileTemplate(text);
}

void PDFRenderToImagesDialog::onResolutionButtonClicked(bool checked)
{
    if (checked)
    {
        const pdf::PDFPageImageExportSettings::ResolutionMode resolutionMode = (sender() == ui->resolutionDPIButton) ? pdf::PDFPageImageExportSettings::ResolutionMode::DPI
                                                                                                                     : pdf::PDFPageImageExportSettings::ResolutionMode::Pixels;
        m_imageExportSettings.setResolutionMode(resolutionMode);
        loadImageExportSettings();
    }
}

void PDFRenderToImagesDialog::onResolutionDPIChanged(int value)
{
    m_imageExportSettings.setDpiResolution(value);
}

void PDFRenderToImagesDialog::onResolutionPixelsChanged(int value)
{
    m_imageExportSettings.setPixelResolution(value);
}

void PDFRenderToImagesDialog::onCompressionChanged(int value)
{
    m_imageWriterSettings.setCompression(value);
}

void PDFRenderToImagesDialog::onQualityChanged(int value)
{
    m_imageWriterSettings.setQuality(value);
}

void PDFRenderToImagesDialog::onGammaChanged(double value)
{
    m_imageWriterSettings.setGamma(value);
}

void PDFRenderToImagesDialog::onOptimizedWriteChanged(bool value)
{
    m_imageWriterSettings.setOptimizedWrite(value);
}

void PDFRenderToImagesDialog::onProgressiveScanWriteChanged(bool value)
{
    m_imageWriterSettings.setProgressiveScanWrite(value);
}

void PDFRenderToImagesDialog::onRenderError(pdf::PDFInteger pageIndex, pdf::PDFRenderError error)
{
    ui->progressMessagesEdit->setPlainText(QString("%1\n%2").arg(ui->progressMessagesEdit->toPlainText()).arg(tr("Page %1: %2").arg(pageIndex + 1).arg(error.message)));
}

void PDFRenderToImagesDialog::onRenderingFinished()
{
    setEnabled(true);

    delete m_rasterizerPool;
    m_rasterizerPool = nullptr;

    delete m_optionalContentActivity;
    m_optionalContentActivity = nullptr;

    m_cms.reset();
}

void PDFRenderToImagesDialog::on_selectDirectoryButton_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Select output directory"), ui->directoryEdit->text());
    if (!directory.isEmpty())
    {
        ui->directoryEdit->setText(directory);
    }
}

void PDFRenderToImagesDialog::on_buttonBox_clicked(QAbstractButton* button)
{
    if (button == ui->buttonBox->button(QDialogButtonBox::Apply))
    {
        QString message;
        if (m_imageExportSettings.validate(&message))
        {
            setEnabled(false);

            // We are ready to render the document
            m_pageIndices = m_imageExportSettings.getPages();
            m_optionalContentActivity = new pdf::PDFOptionalContentActivity(m_document, pdf::OCUsage::Export, this);
            m_cms = m_proxy->getCMSManager()->getCurrentCMS();
            m_rasterizerPool = new pdf::PDFRasterizerPool(m_document, m_proxy->getFontCache(), m_proxy->getCMSManager(),
                                                          m_optionalContentActivity, m_proxy->getFeatures(), m_proxy->getMeshQualitySettings(),
                                                          pdf::PDFRasterizerPool::getDefaultRasterizerCount(), m_proxy->isUsingOpenGL(), m_proxy->getSurfaceFormat(), this);
            connect(m_rasterizerPool, &pdf::PDFRasterizerPool::renderError, this, &PDFRenderToImagesDialog::onRenderError);

            auto process = [this]()
            {
                auto imageSizeGetter = [this](const pdf::PDFPage* page) -> QSize
                {
                    Q_ASSERT(page);

                    switch (m_imageExportSettings.getResolutionMode())
                    {
                        case pdf::PDFPageImageExportSettings::ResolutionMode::DPI:
                        {
                            QSizeF size = page->getRotatedMediaBox().size() * pdf::PDF_POINT_TO_INCH * m_imageExportSettings.getDpiResolution();
                            return size.toSize();
                        }

                        case pdf::PDFPageImageExportSettings::ResolutionMode::Pixels:
                        {
                            int pixelResolution = m_imageExportSettings.getPixelResolution();
                            QSizeF size = page->getRotatedMediaBox().size().scaled(pixelResolution, pixelResolution, Qt::KeepAspectRatio);
                            return size.toSize();
                        }

                        default:
                        {
                            Q_ASSERT(false);
                            break;
                        }
                    }

                    return QSize();
                };

                auto processImage = [this](pdf::PDFRenderedPageImage& renderedPageImage)
                {
                    QString fileName = m_imageExportSettings.getOutputFileName(renderedPageImage.pageIndex, m_imageWriterSettings.getCurrentFormat());

                    QImageWriter imageWriter(fileName, m_imageWriterSettings.getCurrentFormat());
                    imageWriter.setSubType(m_imageWriterSettings.getCurrentSubtype());
                    imageWriter.setCompression(m_imageWriterSettings.getCompression());
                    imageWriter.setQuality(m_imageWriterSettings.getQuality());
                    imageWriter.setGamma(m_imageWriterSettings.getGamma());
                    imageWriter.setOptimizedWrite(m_imageWriterSettings.hasOptimizedWrite());
                    imageWriter.setProgressiveScanWrite(m_imageWriterSettings.hasProgressiveScanWrite());

                    if (!imageWriter.write(renderedPageImage.pageImage))
                    {
                        emit m_rasterizerPool->renderError(renderedPageImage.pageIndex, pdf::PDFRenderError(pdf::RenderErrorType::Error, tr("Cannot write page image to file '%1', because: %2.").arg(fileName).arg(imageWriter.errorString())));
                    }
                };

                m_rasterizerPool->render(m_pageIndices, imageSizeGetter, processImage, m_progress);
            };
            m_watcher.setFuture(QtConcurrent::run(process));
        }
        else
        {
            QMessageBox::critical(this, tr("Error"), message);
        }
    }
}

}   // namespace pdfviewer
