// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfrendertoimagesdialog.h"
#include "ui_pdfrendertoimagesdialog.h"

#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_split.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfoptionalcontent.h"
#include "pdfdrawspacecontroller.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QImageWriter>

#include <algorithm>

#include "pdfdbgheap.h"

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
    m_isLoadingData(false)
{
    ui->setupUi(this);

    // B54, K4.10: the gamma was stored, but no image writer of Qt 6 uses it - the option is gone
    ui->gammaLabel->hide();
    ui->gammaEdit->hide();

    qRegisterMetaType<pdf::PDFRenderError>("PDFRenderError");
    qRegisterMetaType<pdf::PDFInteger>("PDFInteger");

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
    connect(ui->optimizedWriteCheckBox, &QCheckBox::clicked, this, &PDFRenderToImagesDialog::onOptimizedWriteChanged);
    connect(ui->progressiveScanWriteCheckBox, &QCheckBox::clicked, this, &PDFRenderToImagesDialog::onProgressiveScanWriteChanged);
    connect(&m_watcher, &QFutureWatcher<pdf::PDFDocumentToolResult>::finished, this, &PDFRenderToImagesDialog::onRenderingFinished);

    ui->resolutionDPIEdit->setRange(pdf::PDFPageImageExportSettings::getMinDPIResolution(), pdf::PDFPageImageExportSettings::getMaxDPIResolution());
    ui->resolutionPixelsEdit->setRange(pdf::PDFPageImageExportSettings::getMinPixelResolution(), pdf::PDFPageImageExportSettings::getMaxPixelResolution());

    loadImageWriterSettings();
    loadImageExportSettings();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1000, 600));
    pdf::PDFWidgetUtils::style(this);
}

PDFRenderToImagesDialog::~PDFRenderToImagesDialog()
{
    // The run writes into the sink: it ends before the sink goes
    m_watcher.waitForFinished();
    delete ui;
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
    ui->optimizedWriteCheckBox->setChecked(m_imageWriterSettings.hasOptimizedWrite());
    ui->progressiveScanWriteCheckBox->setChecked(m_imageWriterSettings.hasProgressiveScanWrite());

    ui->subtypeComboBox->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::SupportedSubTypes));
    ui->compressionEdit->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::CompressionRatio));
    ui->qualityEdit->setEnabled(m_imageWriterSettings.isOptionSupported(QImageIOHandler::Quality));
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

void PDFRenderToImagesDialog::onOptimizedWriteChanged(bool value)
{
    m_imageWriterSettings.setOptimizedWrite(value);
}

void PDFRenderToImagesDialog::onProgressiveScanWriteChanged(bool value)
{
    m_imageWriterSettings.setProgressiveScanWrite(value);
}

void PDFRenderToImagesDialog::onRenderingFinished()
{
    setEnabled(true);
    m_sink.reset();

    // The problems of the pages and the files, which could not be written
    const pdf::PDFDocumentToolResult result = m_watcher.result();
    QStringList lines;
    if (!ui->progressMessagesEdit->toPlainText().isEmpty())
    {
        lines << ui->progressMessagesEdit->toPlainText();
    }
    if (result.status == pdf::PDFDocumentToolResultStatus::Failed)
    {
        lines << result.message;
    }
    for (const pdf::PDFDocumentToolPageProblem& problem : result.renderingIssues)
    {
        lines << tr("Page %1: %2").arg(QString::number(problem.pageIndex + 1), problem.message);
    }
    for (const pdf::PDFDocumentToolFileProblem& problem : result.failedFiles)
    {
        lines << tr("Cannot write page image to file '%1', because: %2.").arg(problem.fileName, problem.reason);
    }
    ui->progressMessagesEdit->setPlainText(lines.join(QLatin1Char('\n')));
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
            // B67: the tool PDF to Images renders and writes the pages (the image of the crop box with
            // /Rotate and /UserUnit, B57; the names checked before the first file, B55). The dialog
            // keeps its options: "%" of its template is the page ("@"), existing files are overwritten.
            std::vector<pdf::PDFInteger> pageIndices = m_imageExportSettings.getPages();
            std::sort(pageIndices.begin(), pageIndices.end());
            pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());

            QVariantMap parameters = pdf::PDFDocumentToolImageFormat::fromWriterSettings(m_imageWriterSettings).toParameters();
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_PAGES), pdf::PDFDocumentToolPageSelection::createRangeText(pageIndices));
            const bool isPixels = m_imageExportSettings.getResolutionMode() == pdf::PDFPageImageExportSettings::ResolutionMode::Pixels;
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_SIZE_BY), QString::fromLatin1(isPixels ? pdf::PDFDocumentToolPdfToImages::SIZE_BY_PIXELS : pdf::PDFDocumentToolPdfToImages::SIZE_BY_DPI));
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_DPI), m_imageExportSettings.getDpiResolution());
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolPdfToImages::PARAMETER_PIXELS), m_imageExportSettings.getPixelResolution());
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), m_imageExportSettings.getDirectory());
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), m_imageExportSettings.getFileTemplate().replace(QLatin1Char('%'), QLatin1Char('@')));
            parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), QString::fromLatin1(pdf::PDFDocumentToolFileOutput::EXISTING_OVERWRITE));

            pdf::PDFDocumentToolPdfToImages tool;
            QVariantMap normalizedParameters;
            std::vector<pdf::PDFDocumentToolParameterError> errors = pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), parameters, &normalizedParameters);

            // The worker gets values only: its own copy of the document and the settings of the view
            pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(*m_document)), QByteArray(), QString(), true));
            m_sink = std::make_unique<pdf::PDFDocumentToolFolderSink>(m_imageExportSettings.getDirectory(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
            context.fileSink = m_sink.get();
            if (errors.empty())
            {
                errors = tool.validate(context, normalizedParameters);
            }
            if (!errors.empty())
            {
                m_sink.reset();
                QMessageBox::critical(this, tr("Error"), errors.front().message);
                return;
            }

            pdf::PDFDocumentToolPageRenderSettings renderSettings;
            renderSettings.cmsSettings = m_proxy->getCMSManager()->getSettings();
            // The features of the view without its color modes (R4-10 b, as the tool of the application)
            renderSettings.features = pdf::PDFDocumentToolPageRenderSettings::getExportFeatures(m_proxy->getFeatures());
            renderSettings.engine = m_proxy->getRendererEngine();

            setEnabled(false);
            ui->progressMessagesEdit->clear();
            pdf::PDFProgress* progress = m_progress;
            auto process = [context, normalizedParameters, renderSettings, progress]()
            {
                return pdf::PDFDocumentToolPdfToImages().exportPages(context, normalizedParameters, renderSettings, nullptr, progress);
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

#include "moc_pdfrendertoimagesdialog.cpp"
