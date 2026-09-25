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

#include "pdfocrbatchdialog.h"
#include "pdfocrlanguagesdialog.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrexport.h"
#include "pdfocrengine.h"
#include "pdfwidgetutils.h"

#include <QDir>
#include <QLabel>
#include <QThread>
#include <QMimeData>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QFormLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QDragEnterEvent>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QRegularExpression>

namespace pdfviewer
{

namespace
{

QString getSettingsGroup()
{
    return QStringLiteral("OCRBatchDialog");
}

/// Group of the settings of the OCR dialog (its last configuration and the named profiles)
QString getOCRDialogSettingsGroup()
{
    return QStringLiteral("OCRDialog");
}

constexpr int InputFileRole = Qt::UserRole;
constexpr int OutputFileRole = Qt::UserRole + 1;

enum CompressionChoice
{
    CompressionOff,
    CompressionLossless
};

}   // namespace

PDFOCRBatchDialog::PDFOCRBatchDialog(QWidget* parent) :
    BaseClass(parent),
    m_modelManager(new pdf::PDFOCRModelManager(this))
{
    setWindowTitle(tr("Batch Recognize Text"));
    setAcceptDrops(true);

    m_modelManager->loadBundledCatalog();
    m_modelManager->refresh();

    createUi();
    loadSettings();
    updateUi();

    resize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(1000, 760)));
}

PDFOCRBatchDialog::~PDFOCRBatchDialog()
{
    if (m_thread)
    {
        // The worker posts its results to this object, it must finish before
        m_cancelToken->cancel();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void PDFOCRBatchDialog::createUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Files
    QGroupBox* filesGroupBox = new QGroupBox(tr("Files"), this);
    QHBoxLayout* filesLayout = new QHBoxLayout(filesGroupBox);
    m_filesTable = new QTableWidget(0, ColumnCount, filesGroupBox);
    m_filesTable->setObjectName(QStringLiteral("filesTable"));
    m_filesTable->setHorizontalHeaderLabels({ tr("File"), tr("Status"), tr("Pages"), tr("Recognized"), tr("Failed"), tr("Words"), tr("To Review"), tr("Time [s]"), tr("Message") });
    m_filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_filesTable->verticalHeader()->hide();
    m_filesTable->horizontalHeader()->setSectionResizeMode(ColumnFile, QHeaderView::Interactive);
    m_filesTable->horizontalHeader()->setStretchLastSection(true);
    m_filesTable->setColumnWidth(ColumnFile, pdf::PDFWidgetUtils::scaleDPI_x(this, 280));
    m_filesTable->setToolTip(tr("PDF files to recognize. Files and folders can be dropped here."));
    filesLayout->addWidget(m_filesTable, 1);

    QVBoxLayout* fileButtonsLayout = new QVBoxLayout();
    m_addFilesButton = new QPushButton(tr("Add Files..."), filesGroupBox);
    m_addFilesButton->setObjectName(QStringLiteral("addFilesButton"));
    m_addFolderButton = new QPushButton(tr("Add Folder..."), filesGroupBox);
    m_addFolderButton->setObjectName(QStringLiteral("addFolderButton"));
    m_removeButton = new QPushButton(tr("Remove"), filesGroupBox);
    m_removeButton->setObjectName(QStringLiteral("removeButton"));
    m_clearButton = new QPushButton(tr("Clear"), filesGroupBox);
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    fileButtonsLayout->addWidget(m_addFilesButton);
    fileButtonsLayout->addWidget(m_addFolderButton);
    fileButtonsLayout->addWidget(m_removeButton);
    fileButtonsLayout->addWidget(m_clearButton);
    fileButtonsLayout->addStretch(1);
    filesLayout->addLayout(fileButtonsLayout);
    mainLayout->addWidget(filesGroupBox, 1);

    QHBoxLayout* settingsLayout = new QHBoxLayout();

    // Recognition
    QGroupBox* recognitionGroupBox = new QGroupBox(tr("Recognition"), this);
    QFormLayout* recognitionLayout = new QFormLayout(recognitionGroupBox);
    m_settingsSourceComboBox = new QComboBox(recognitionGroupBox);
    m_settingsSourceComboBox->setObjectName(QStringLiteral("settingsSourceComboBox"));
    m_settingsSourceComboBox->setToolTip(tr("Settings of the recognition: the last settings of the dialog Recognize Text, or a profile saved there (resolution, layout, preprocessing, restrictions of the characters, user words)."));
    recognitionLayout->addRow(tr("Settings:"), m_settingsSourceComboBox);

    QHBoxLayout* languagesLayout = new QHBoxLayout();
    m_languagesEdit = new QLineEdit(recognitionGroupBox);
    m_languagesEdit->setObjectName(QStringLiteral("languagesEdit"));
    m_languagesEdit->setPlaceholderText(tr("for example ces+eng"));
    m_languagesEdit->setToolTip(tr("Languages of the text in the order of their importance, joined by '+'."));
    m_manageLanguagesButton = new QPushButton(tr("Manage..."), recognitionGroupBox);
    m_manageLanguagesButton->setToolTip(tr("Download, import or remove the language models."));
    languagesLayout->addWidget(m_languagesEdit, 1);
    languagesLayout->addWidget(m_manageLanguagesButton);
    recognitionLayout->addRow(tr("Languages:"), languagesLayout);

    m_modelProfileComboBox = new QComboBox(recognitionGroupBox);
    m_modelProfileComboBox->setObjectName(QStringLiteral("modelProfileComboBox"));
    for (pdf::PDFOCRModelProfile profile : pdf::PDFOCRConfiguration::getProfiles())
    {
        m_modelProfileComboBox->addItem(pdf::PDFOCRConfiguration::getProfileName(profile), int(profile));
    }
    recognitionLayout->addRow(tr("Models:"), m_modelProfileComboBox);

    m_existingTextComboBox = new QComboBox(recognitionGroupBox);
    m_existingTextComboBox->setObjectName(QStringLiteral("existingTextComboBox"));
    m_existingTextComboBox->addItem(tr("Only pages without text"), int(pdf::PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
    m_existingTextComboBox->addItem(tr("Replace OCR created by PDF4QT"), int(pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer));
    m_existingTextComboBox->addItem(tr("Recognize for export only"), int(pdf::PDFOCRExistingTextPolicy::ReviewOnly));
    recognitionLayout->addRow(tr("Existing text:"), m_existingTextComboBox);

    m_decisionComboBox = new QComboBox(recognitionGroupBox);
    m_decisionComboBox->setObjectName(QStringLiteral("decisionComboBox"));
    m_decisionComboBox->addItem(tr("Skip them"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::Skip));
    m_decisionComboBox->addItem(tr("Recognize scans with a small text masked"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText));
    m_decisionComboBox->addItem(tr("Recognize them for export only"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::ReviewOnly));
    m_decisionComboBox->setToolTip(tr("Pages with both text and images, or with an ambiguous content, need a decision. The dialog Recognize Text asks for it, the batch uses this choice."));
    recognitionLayout->addRow(tr("Text and images:"), m_decisionComboBox);

    m_allowPageErrorsCheckBox = new QCheckBox(tr("Write the results even if some pages fail"), recognitionGroupBox);
    m_allowPageErrorsCheckBox->setObjectName(QStringLiteral("allowPageErrorsCheckBox"));
    m_allowPageErrorsCheckBox->setToolTip(tr("Without it, a file with a failed page is not written at all."));
    recognitionLayout->addRow(m_allowPageErrorsCheckBox);
    m_allowReducedDpiCheckBox = new QCheckBox(tr("Reduce the resolution of too large pages"), recognitionGroupBox);
    m_allowReducedDpiCheckBox->setObjectName(QStringLiteral("allowReducedDpiCheckBox"));
    m_allowReducedDpiCheckBox->setToolTip(tr("Without it, a page too large for the resolution is an error of the page."));
    recognitionLayout->addRow(m_allowReducedDpiCheckBox);

    m_settingsSummaryLabel = new QLabel(recognitionGroupBox);
    m_settingsSummaryLabel->setWordWrap(true);
    recognitionLayout->addRow(m_settingsSummaryLabel);
    settingsLayout->addWidget(recognitionGroupBox, 1);

    // Output
    QGroupBox* outputGroupBox = new QGroupBox(tr("Output"), this);
    QFormLayout* outputLayout = new QFormLayout(outputGroupBox);
    m_nextToOriginalRadioButton = new QRadioButton(tr("Next to the original file"), outputGroupBox);
    m_nextToOriginalRadioButton->setObjectName(QStringLiteral("nextToOriginalRadioButton"));
    m_outputDirectoryRadioButton = new QRadioButton(tr("Into the folder:"), outputGroupBox);
    m_outputDirectoryRadioButton->setObjectName(QStringLiteral("outputDirectoryRadioButton"));
    outputLayout->addRow(m_nextToOriginalRadioButton);
    QHBoxLayout* directoryLayout = new QHBoxLayout();
    m_outputDirectoryEdit = new QLineEdit(outputGroupBox);
    m_outputDirectoryEdit->setObjectName(QStringLiteral("outputDirectoryEdit"));
    m_browseOutputDirectoryButton = new QPushButton(tr("Browse..."), outputGroupBox);
    directoryLayout->addWidget(m_outputDirectoryEdit, 1);
    directoryLayout->addWidget(m_browseOutputDirectoryButton);
    outputLayout->addRow(m_outputDirectoryRadioButton, directoryLayout);

    m_suffixEdit = new QLineEdit(outputGroupBox);
    m_suffixEdit->setObjectName(QStringLiteral("suffixEdit"));
    m_suffixEdit->setToolTip(tr("Suffix of the names of the output files, the original files are never overwritten."));
    outputLayout->addRow(tr("Suffix:"), m_suffixEdit);

    m_writeDocumentCheckBox = new QCheckBox(tr("Write the PDF with the text layer"), outputGroupBox);
    m_writeDocumentCheckBox->setObjectName(QStringLiteral("writeDocumentCheckBox"));
    outputLayout->addRow(m_writeDocumentCheckBox);

    m_compressionComboBox = new QComboBox(outputGroupBox);
    m_compressionComboBox->setObjectName(QStringLiteral("compressionComboBox"));
    m_compressionComboBox->addItem(tr("Do not change the images"), CompressionOff);
    m_compressionComboBox->addItem(tr("Lossless (JBIG2 generic region, CCITT G4, Flate)"), CompressionLossless);
    m_compressionComboBox->setToolTip(tr("Compression of the scanned images of the written pages. The lossy conversion to black and white is offered only in the dialog Recognize Text, where its result can be checked in the preview."));
    outputLayout->addRow(tr("Compression:"), m_compressionComboBox);

    QHBoxLayout* exportsLayout = new QHBoxLayout();
    m_exportTextCheckBox = new QCheckBox(tr("Text"), outputGroupBox);
    m_exportTextCheckBox->setObjectName(QStringLiteral("exportTextCheckBox"));
    m_exportHocrCheckBox = new QCheckBox(tr("hOCR"), outputGroupBox);
    m_exportHocrCheckBox->setObjectName(QStringLiteral("exportHocrCheckBox"));
    m_exportAltoCheckBox = new QCheckBox(tr("ALTO"), outputGroupBox);
    m_exportAltoCheckBox->setObjectName(QStringLiteral("exportAltoCheckBox"));
    m_exportTsvCheckBox = new QCheckBox(tr("TSV"), outputGroupBox);
    m_exportTsvCheckBox->setObjectName(QStringLiteral("exportTsvCheckBox"));
    exportsLayout->addWidget(m_exportTextCheckBox);
    exportsLayout->addWidget(m_exportHocrCheckBox);
    exportsLayout->addWidget(m_exportAltoCheckBox);
    exportsLayout->addWidget(m_exportTsvCheckBox);
    exportsLayout->addStretch(1);
    outputLayout->addRow(tr("Export:"), exportsLayout);

    m_skipExistingCheckBox = new QCheckBox(tr("Skip the files, whose outputs already exist"), outputGroupBox);
    m_skipExistingCheckBox->setObjectName(QStringLiteral("skipExistingCheckBox"));
    outputLayout->addRow(m_skipExistingCheckBox);
    settingsLayout->addWidget(outputGroupBox, 1);
    mainLayout->addLayout(settingsLayout);

    // Progress
    QHBoxLayout* progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("progressBar"));
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_startButton = new QPushButton(tr("Start"), this);
    m_startButton->setObjectName(QStringLiteral("startButton"));
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    m_openResultButton = new QPushButton(tr("Open Result"), this);
    m_openResultButton->setObjectName(QStringLiteral("openResultButton"));
    m_openResultButton->setToolTip(tr("Close the dialog and open the result of the selected file in the editor."));
    progressLayout->addWidget(m_progressBar, 1);
    progressLayout->addWidget(m_startButton);
    progressLayout->addWidget(m_stopButton);
    progressLayout->addWidget(m_openResultButton);
    mainLayout->addLayout(progressLayout);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName(QStringLiteral("progressLabel"));
    mainLayout->addWidget(m_progressLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_addFilesButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onAddFiles);
    connect(m_addFolderButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onAddFolder);
    connect(m_removeButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onRemoveFiles);
    connect(m_clearButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onClearFiles);
    connect(m_settingsSourceComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRBatchDialog::onSettingsSourceChanged);
    connect(m_manageLanguagesButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onManageLanguages);
    connect(m_browseOutputDirectoryButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onBrowseOutputDirectory);
    connect(m_startButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onStart);
    connect(m_stopButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onStop);
    connect(m_openResultButton, &QPushButton::clicked, this, &PDFOCRBatchDialog::onOpenResult);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &PDFOCRBatchDialog::reject);
    connect(m_filesTable, &QTableWidget::itemSelectionChanged, this, &PDFOCRBatchDialog::updateUi);
    connect(m_filesTable, &QTableWidget::cellDoubleClicked, this, &PDFOCRBatchDialog::onOpenResult);
    connect(m_nextToOriginalRadioButton, &QRadioButton::toggled, this, &PDFOCRBatchDialog::updateUi);
    connect(m_writeDocumentCheckBox, &QCheckBox::toggled, this, &PDFOCRBatchDialog::updateUi);
}

void PDFOCRBatchDialog::updateSettingsSources()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getOCRDialogSettingsGroup());
    const QByteArray lastConfiguration = settings.value(QStringLiteral("configuration")).toByteArray();
    const QJsonArray profiles = QJsonDocument::fromJson(settings.value(QStringLiteral("profiles")).toByteArray()).array();
    settings.endGroup();

    m_settingsSourceComboBox->blockSignals(true);
    m_settingsSourceComboBox->clear();
    m_settingsSourceComboBox->addItem(tr("Last settings of the dialog Recognize Text"), lastConfiguration);
    for (const QJsonValue& value : profiles)
    {
        const pdf::PDFOCRProfile profile = pdf::PDFOCRProfile::fromJson(value.toObject());
        m_settingsSourceComboBox->addItem(tr("Profile: %1").arg(profile.name), QJsonDocument(profile.configuration.toJson()).toJson(QJsonDocument::Compact));
    }
    m_settingsSourceComboBox->blockSignals(false);
}

void PDFOCRBatchDialog::loadSettings()
{
    updateSettingsSources();

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    const int sourceIndex = m_settingsSourceComboBox->findText(settings.value(QStringLiteral("settingsSource")).toString());
    m_settingsSourceComboBox->setCurrentIndex(qMax(0, sourceIndex));
    onSettingsSourceChanged();

    const QString languages = settings.value(QStringLiteral("languages")).toString();
    if (!languages.isEmpty())
    {
        m_languagesEdit->setText(languages);
    }
    if (settings.contains(QStringLiteral("modelProfile")))
    {
        m_modelProfileComboBox->setCurrentIndex(qMax(0, m_modelProfileComboBox->findData(settings.value(QStringLiteral("modelProfile")))));
    }
    if (settings.contains(QStringLiteral("existingText")))
    {
        m_existingTextComboBox->setCurrentIndex(qMax(0, m_existingTextComboBox->findData(settings.value(QStringLiteral("existingText")))));
    }
    m_decisionComboBox->setCurrentIndex(qMax(0, m_decisionComboBox->findData(settings.value(QStringLiteral("decision"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::Skip)))));
    m_allowPageErrorsCheckBox->setChecked(settings.value(QStringLiteral("allowPageErrors"), false).toBool());
    m_allowReducedDpiCheckBox->setChecked(settings.value(QStringLiteral("allowReducedDpi"), false).toBool());

    const bool nextToOriginal = settings.value(QStringLiteral("nextToOriginal"), true).toBool();
    m_nextToOriginalRadioButton->setChecked(nextToOriginal);
    m_outputDirectoryRadioButton->setChecked(!nextToOriginal);
    m_outputDirectoryEdit->setText(settings.value(QStringLiteral("outputDirectory")).toString());
    m_suffixEdit->setText(settings.value(QStringLiteral("suffix"), QStringLiteral("_ocr")).toString());
    m_writeDocumentCheckBox->setChecked(settings.value(QStringLiteral("writeDocument"), true).toBool());
    m_compressionComboBox->setCurrentIndex(qMax(0, m_compressionComboBox->findData(settings.value(QStringLiteral("compression"), int(CompressionOff)))));
    m_exportTextCheckBox->setChecked(settings.value(QStringLiteral("exportText"), false).toBool());
    m_exportHocrCheckBox->setChecked(settings.value(QStringLiteral("exportHocr"), false).toBool());
    m_exportAltoCheckBox->setChecked(settings.value(QStringLiteral("exportAlto"), false).toBool());
    m_exportTsvCheckBox->setChecked(settings.value(QStringLiteral("exportTsv"), false).toBool());
    m_skipExistingCheckBox->setChecked(settings.value(QStringLiteral("skipExisting"), true).toBool());
    settings.endGroup();
}

void PDFOCRBatchDialog::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    settings.setValue(QStringLiteral("settingsSource"), m_settingsSourceComboBox->currentText());
    settings.setValue(QStringLiteral("languages"), m_languagesEdit->text());
    settings.setValue(QStringLiteral("modelProfile"), m_modelProfileComboBox->currentData());
    settings.setValue(QStringLiteral("existingText"), m_existingTextComboBox->currentData());
    settings.setValue(QStringLiteral("decision"), m_decisionComboBox->currentData());
    settings.setValue(QStringLiteral("allowPageErrors"), m_allowPageErrorsCheckBox->isChecked());
    settings.setValue(QStringLiteral("allowReducedDpi"), m_allowReducedDpiCheckBox->isChecked());
    settings.setValue(QStringLiteral("nextToOriginal"), m_nextToOriginalRadioButton->isChecked());
    settings.setValue(QStringLiteral("outputDirectory"), m_outputDirectoryEdit->text());
    settings.setValue(QStringLiteral("suffix"), m_suffixEdit->text());
    settings.setValue(QStringLiteral("writeDocument"), m_writeDocumentCheckBox->isChecked());
    settings.setValue(QStringLiteral("compression"), m_compressionComboBox->currentData());
    settings.setValue(QStringLiteral("exportText"), m_exportTextCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportHocr"), m_exportHocrCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportAlto"), m_exportAltoCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportTsv"), m_exportTsvCheckBox->isChecked());
    settings.setValue(QStringLiteral("skipExisting"), m_skipExistingCheckBox->isChecked());
    settings.endGroup();
}

void PDFOCRBatchDialog::onSettingsSourceChanged()
{
    const QByteArray configurationData = m_settingsSourceComboBox->currentData().toByteArray();
    pdf::PDFOCRConfiguration configuration;
    if (!configurationData.isEmpty())
    {
        configuration = pdf::PDFOCRConfiguration::fromJson(QJsonDocument::fromJson(configurationData).object());
    }
    if (configuration.languages.isEmpty())
    {
        configuration.languages << QStringLiteral("eng");
    }

    m_languagesEdit->setText(configuration.getLanguageString());
    m_modelProfileComboBox->setCurrentIndex(qMax(0, m_modelProfileComboBox->findData(int(configuration.profile))));
    const int existingTextIndex = m_existingTextComboBox->findData(int(configuration.existingTextPolicy));
    m_existingTextComboBox->setCurrentIndex(existingTextIndex >= 0 ? existingTextIndex : 0);

    QStringList summary;
    summary << tr("Engine: %1").arg(configuration.engineId);
    summary << tr("%1 DPI").arg(qRound(configuration.dpi));
    summary << pdf::PDFOCRConfiguration::getLayoutName(configuration.layout);
    if (configuration.preprocessing.deskew)
    {
        summary << tr("deskew");
    }
    if (configuration.preprocessing.autoOrientation)
    {
        summary << tr("automatic orientation");
    }
    if (configuration.preprocessing.denoise)
    {
        summary << tr("noise removal");
    }
    if (!configuration.userWords.isEmpty())
    {
        summary << tr("%n user word(s)", nullptr, int(configuration.userWords.size()));
    }
    m_settingsSummaryLabel->setText(summary.join(QStringLiteral(", ")));
}

pdf::PDFOCRConfiguration PDFOCRBatchDialog::getConfiguration() const
{
    const QByteArray configurationData = m_settingsSourceComboBox->currentData().toByteArray();
    pdf::PDFOCRConfiguration configuration;
    if (!configurationData.isEmpty())
    {
        configuration = pdf::PDFOCRConfiguration::fromJson(QJsonDocument::fromJson(configurationData).object());
    }

    configuration.languages.clear();
    for (const QString& language : m_languagesEdit->text().split(QRegularExpression(QStringLiteral("[+,\\s]")), Qt::SkipEmptyParts))
    {
        if (!configuration.languages.contains(language))
        {
            configuration.languages << language;
        }
    }
    configuration.profile = pdf::PDFOCRModelProfile(m_modelProfileComboBox->currentData().toInt());
    configuration.existingTextPolicy = pdf::PDFOCRExistingTextPolicy(m_existingTextComboBox->currentData().toInt());

    // The lossy compression of a profile is never applied without the preview
    configuration.compression.mode = m_compressionComboBox->currentData().toInt() == CompressionLossless ? pdf::PDFOCRCompressionMode::Lossless : pdf::PDFOCRCompressionMode::Off;
    configuration.compression.excludedImages.clear();
    return configuration;
}

QStringList PDFOCRBatchDialog::getOutputFiles(const QString& inputFile) const
{
    const QFileInfo inputInfo(inputFile);
    const QDir directory = m_nextToOriginalRadioButton->isChecked() ? inputInfo.absoluteDir() : QDir(m_outputDirectoryEdit->text());
    const QString baseName = directory.absoluteFilePath(inputInfo.completeBaseName() + m_suffixEdit->text());

    QStringList files;
    if (m_writeDocumentCheckBox->isChecked())
    {
        files << baseName + QStringLiteral(".pdf");
    }
    if (m_exportTextCheckBox->isChecked())
    {
        files << baseName + QStringLiteral(".txt");
    }
    if (m_exportHocrCheckBox->isChecked())
    {
        files << baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Hocr);
    }
    if (m_exportAltoCheckBox->isChecked())
    {
        files << baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Alto);
    }
    if (m_exportTsvCheckBox->isChecked())
    {
        files << baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Tsv);
    }
    return files;
}

pdf::PDFOCRDocumentRunner::FileTask PDFOCRBatchDialog::createTask(const QString& inputFile, const pdf::PDFOCRConfiguration& configuration) const
{
    const QFileInfo inputInfo(inputFile);
    const QDir directory = m_nextToOriginalRadioButton->isChecked() ? inputInfo.absoluteDir() : QDir(m_outputDirectoryEdit->text());
    const QString baseName = directory.absoluteFilePath(inputInfo.completeBaseName() + m_suffixEdit->text());

    pdf::PDFOCRDocumentRunner::FileTask task;
    task.inputFile = inputFile;
    task.outputFile = m_writeDocumentCheckBox->isChecked() ? baseName + QStringLiteral(".pdf") : QString();
    task.exportText = m_exportTextCheckBox->isChecked() ? baseName + QStringLiteral(".txt") : QString();
    task.exportHocr = m_exportHocrCheckBox->isChecked() ? baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Hocr) : QString();
    task.exportAlto = m_exportAltoCheckBox->isChecked() ? baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Alto) : QString();
    task.exportTsv = m_exportTsvCheckBox->isChecked() ? baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Tsv) : QString();
    task.keepReviewData = configuration.keepReviewDataInDocument;
    task.allowPageErrors = m_allowPageErrorsCheckBox->isChecked();
    task.configure = [configuration](const pdf::PDFOCRProject*, pdf::PDFOCRConfiguration* taskConfiguration)
    {
        *taskConfiguration = configuration;
        return QString();
    };
    return task;
}

QStringList PDFOCRBatchDialog::validate(const pdf::PDFOCRConfiguration& configuration) const
{
    QStringList errors;

    if (m_filesTable->rowCount() == 0)
    {
        errors << tr("Add the files to recognize.");
    }

    if (configuration.languages.isEmpty())
    {
        errors << tr("Select the languages of the text.");
    }
    errors << configuration.validate();

    std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
    QString reason;
    if (!factory || !factory->isAvailable(&reason))
    {
        errors << tr("OCR engine '%1' is not available. %2").arg(configuration.engineId, reason);
    }
    else if (factory->usesManagedModels() && !configuration.languages.isEmpty())
    {
        const QStringList missing = m_modelManager->getMissingModels(configuration.engineId, configuration.languages, configuration.profile);
        if (!missing.isEmpty())
        {
            errors << tr("The language models are not installed: %1. Use the button 'Manage...' to install them.").arg(missing.join(QStringLiteral(", ")));
        }
    }

    const bool writesAnything = m_writeDocumentCheckBox->isChecked() || m_exportTextCheckBox->isChecked() || m_exportHocrCheckBox->isChecked() ||
                                m_exportAltoCheckBox->isChecked() || m_exportTsvCheckBox->isChecked();
    if (!writesAnything)
    {
        errors << tr("Select the PDF with the text layer or at least one export.");
    }

    if (m_outputDirectoryRadioButton->isChecked() && m_outputDirectoryEdit->text().trimmed().isEmpty())
    {
        errors << tr("Select the output folder.");
    }

    // The originals are never overwritten
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        const QString inputFile = m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString();
        for (const QString& outputFile : getOutputFiles(inputFile))
        {
            if (QFileInfo(outputFile).absoluteFilePath().compare(QFileInfo(inputFile).absoluteFilePath(), Qt::CaseInsensitive) == 0)
            {
                errors << tr("The output would overwrite the file '%1', change the suffix.").arg(QDir::toNativeSeparators(inputFile));
                return errors;
            }
        }
    }

    return errors;
}

void PDFOCRBatchDialog::addFiles(const QStringList& files)
{
    QStringList pdfFiles;
    for (const QString& file : files)
    {
        const QFileInfo fileInfo(file);
        if (fileInfo.isDir())
        {
            for (const QFileInfo& entry : QDir(fileInfo.absoluteFilePath()).entryInfoList({ QStringLiteral("*.pdf") }, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase))
            {
                pdfFiles << entry.absoluteFilePath();
            }
        }
        else if (fileInfo.isFile())
        {
            pdfFiles << fileInfo.absoluteFilePath();
        }
    }

    const QStringList existingFiles = getFiles();
    for (const QString& file : pdfFiles)
    {
        if (existingFiles.contains(file, Qt::CaseInsensitive))
        {
            continue;
        }

        const int row = m_filesTable->rowCount();
        m_filesTable->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column)
        {
            QTableWidgetItem* item = new QTableWidgetItem();
            if (column != ColumnFile && column != ColumnStatus && column != ColumnMessage)
            {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            m_filesTable->setItem(row, column, item);
        }
        QTableWidgetItem* fileItem = m_filesTable->item(row, ColumnFile);
        fileItem->setText(QFileInfo(file).fileName());
        fileItem->setToolTip(QDir::toNativeSeparators(file));
        fileItem->setData(InputFileRole, file);
        setRowStatus(row, tr("Waiting"), QString());
    }

    updateUi();
}

QStringList PDFOCRBatchDialog::getFiles() const
{
    QStringList files;
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        files << m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString();
    }
    return files;
}

bool PDFOCRBatchDialog::isRunning() const
{
    return m_thread != nullptr;
}

void PDFOCRBatchDialog::setRowStatus(int row, const QString& status, const QString& message)
{
    m_filesTable->item(row, ColumnStatus)->setText(status);
    m_filesTable->item(row, ColumnMessage)->setText(message);
    m_filesTable->item(row, ColumnMessage)->setToolTip(message);
}

void PDFOCRBatchDialog::updateUi()
{
    const bool running = isRunning();
    const bool hasSelection = !m_filesTable->selectionModel()->selectedRows().isEmpty();

    m_addFilesButton->setEnabled(!running);
    m_addFolderButton->setEnabled(!running);
    m_removeButton->setEnabled(!running && hasSelection);
    m_clearButton->setEnabled(!running && m_filesTable->rowCount() > 0);
    m_settingsSourceComboBox->setEnabled(!running);
    m_languagesEdit->setEnabled(!running);
    m_modelProfileComboBox->setEnabled(!running);
    m_manageLanguagesButton->setEnabled(!running);
    m_existingTextComboBox->setEnabled(!running);
    m_decisionComboBox->setEnabled(!running);
    m_allowPageErrorsCheckBox->setEnabled(!running);
    m_allowReducedDpiCheckBox->setEnabled(!running);
    m_nextToOriginalRadioButton->setEnabled(!running);
    m_outputDirectoryRadioButton->setEnabled(!running);
    m_outputDirectoryEdit->setEnabled(!running && m_outputDirectoryRadioButton->isChecked());
    m_browseOutputDirectoryButton->setEnabled(!running && m_outputDirectoryRadioButton->isChecked());
    m_suffixEdit->setEnabled(!running);
    m_writeDocumentCheckBox->setEnabled(!running);
    m_compressionComboBox->setEnabled(!running && m_writeDocumentCheckBox->isChecked());
    m_exportTextCheckBox->setEnabled(!running);
    m_exportHocrCheckBox->setEnabled(!running);
    m_exportAltoCheckBox->setEnabled(!running);
    m_exportTsvCheckBox->setEnabled(!running);
    m_skipExistingCheckBox->setEnabled(!running);
    m_startButton->setEnabled(!running && m_filesTable->rowCount() > 0);
    m_stopButton->setEnabled(running && !m_cancelToken->isOperationCancelled());

    bool canOpen = false;
    if (!running && hasSelection)
    {
        const int row = m_filesTable->selectionModel()->selectedRows().front().row();
        const QString outputFile = m_filesTable->item(row, ColumnFile)->data(OutputFileRole).toString();
        canOpen = !outputFile.isEmpty() && QFileInfo::exists(outputFile);
    }
    m_openResultButton->setEnabled(canOpen);
}

void PDFOCRBatchDialog::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files"), QString(), tr("Portable Document (*.pdf)"));
    addFiles(files);
}

void PDFOCRBatchDialog::onAddFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
    if (!directory.isEmpty())
    {
        addFiles({ directory });
    }
}

void PDFOCRBatchDialog::onRemoveFiles()
{
    QModelIndexList rows = m_filesTable->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) { return left.row() > right.row(); });
    for (const QModelIndex& index : rows)
    {
        m_filesTable->removeRow(index.row());
    }
    updateUi();
}

void PDFOCRBatchDialog::onClearFiles()
{
    m_filesTable->setRowCount(0);
    updateUi();
}

void PDFOCRBatchDialog::onBrowseOutputDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Output Folder"), m_outputDirectoryEdit->text());
    if (!directory.isEmpty())
    {
        m_outputDirectoryEdit->setText(QDir::toNativeSeparators(directory));
    }
}

void PDFOCRBatchDialog::onManageLanguages()
{
    PDFOCRLanguagesDialog dialog(m_modelManager, this);
    dialog.exec();
    m_modelManager->refresh();
}

void PDFOCRBatchDialog::onStart()
{
    if (isRunning())
    {
        return;
    }

    const pdf::PDFOCRConfiguration configuration = getConfiguration();
    const QStringList errors = validate(configuration);
    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("The batch cannot be started:\n\n%1").arg(errors.join(QChar('\n'))));
        return;
    }

    if (m_outputDirectoryRadioButton->isChecked())
    {
        const QString directory = m_outputDirectoryEdit->text().trimmed();
        if (!QDir(directory).exists() && !QDir().mkpath(directory))
        {
            QMessageBox::critical(this, windowTitle(), tr("The output folder '%1' cannot be created.").arg(directory));
            return;
        }
    }

    // Existing outputs are skipped, or overwritten after a confirmation
    std::vector<int> existingRows;
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        const QStringList outputs = getOutputFiles(m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString());
        if (!outputs.isEmpty() && std::any_of(outputs.cbegin(), outputs.cend(), [](const QString& output) { return QFileInfo::exists(output); }))
        {
            existingRows.push_back(row);
        }
    }
    if (!existingRows.empty() && !m_skipExistingCheckBox->isChecked() &&
        QMessageBox::question(this, windowTitle(), tr("The outputs of %n file(s) already exist. Do you want to overwrite them?", nullptr, int(existingRows.size()))) != QMessageBox::Yes)
    {
        return;
    }

    saveSettings();

    pdf::PDFOCRDocumentRunner::Settings settings;
    settings.decisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy(m_decisionComboBox->currentData().toInt());
    settings.allowReducedResolution = m_allowReducedDpiCheckBox->isChecked();
    settings.modelManager = m_modelManager;

    std::vector<std::pair<int, pdf::PDFOCRDocumentRunner::FileTask>> tasks;
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        for (int column = ColumnPages; column < ColumnMessage; ++column)
        {
            m_filesTable->item(row, column)->setText(QString());
        }

        const QString inputFile = m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString();
        m_filesTable->item(row, ColumnFile)->setData(OutputFileRole, QString());
        if (m_skipExistingCheckBox->isChecked() && std::find(existingRows.cbegin(), existingRows.cend(), row) != existingRows.cend())
        {
            setRowStatus(row, tr("Skipped"), tr("The outputs already exist."));
            continue;
        }

        setRowStatus(row, tr("Waiting"), QString());
        tasks.emplace_back(row, createTask(inputFile, configuration));
    }

    m_finishedFiles = 0;
    m_succeededFiles = 0;
    m_failedFiles = 0;
    m_skippedFiles = int(m_filesTable->rowCount() - tasks.size());
    m_totalFiles = int(tasks.size());
    m_progressBar->setRange(0, qMax(1, m_totalFiles));
    m_progressBar->setValue(0);

    if (tasks.empty())
    {
        m_progressLabel->setText(tr("All files were skipped, their outputs already exist."));
        updateUi();
        return;
    }

    m_cancelToken = std::make_shared<pdf::PDFOCRCancelToken>();
    std::shared_ptr<pdf::PDFOCRCancelToken> cancelToken = m_cancelToken;

    // The files are processed one after another on a worker thread (it has its own
    // event loop, which the runner needs); the results are posted into this dialog
    m_thread = QThread::create([this, tasks, settings, cancelToken]()
    {
        for (const auto& item : tasks)
        {
            const int row = item.first;
            if (cancelToken->isOperationCancelled())
            {
                break;
            }

            QMetaObject::invokeMethod(this, [this, row]() { onFileStarted(row); }, Qt::QueuedConnection);

            auto progress = [this, row](int finished, int total, const pdf::PDFOCRPageResult* page)
            {
                if (page)
                {
                    QMetaObject::invokeMethod(this, [this, row, finished, total]() { onFileProgress(row, finished, total); }, Qt::QueuedConnection);
                }
            };

            const pdf::PDFOCRDocumentRunner::FileResult fileResult = pdf::PDFOCRDocumentRunner::processFile(item.second, settings, progress, cancelToken.get());
            const pdf::PDFOCRConfidenceStatistics statistics = fileResult.result.getStatistics();

            FileSummary summary;
            summary.status = fileResult.status;
            summary.message = fileResult.message;
            summary.details = fileResult.warnings + fileResult.messages;
            summary.outputFile = fileResult.writeResult.isSuccess() && fileResult.writeResult.document ? item.second.outputFile : QString();
            summary.pageCount = fileResult.pageCount;
            summary.resultPages = fileResult.result.getResultPageCount();
            summary.failedPages = fileResult.result.getFailedPageCount();
            summary.wordCount = statistics.wordCount;
            summary.reviewWords = statistics.reviewRequiredCount;
            summary.elapsedMilliseconds = fileResult.elapsedMilliseconds;
            summary.cancelled = fileResult.result.cancelled;

            QMetaObject::invokeMethod(this, [this, row, summary]() { onFileFinished(row, summary); }, Qt::QueuedConnection);
        }
    });
    connect(m_thread, &QThread::finished, this, &PDFOCRBatchDialog::onBatchFinished, Qt::QueuedConnection);
    m_thread->start();

    m_progressLabel->setText(tr("Recognition of %n file(s) started.", nullptr, m_totalFiles));
    updateUi();
}

void PDFOCRBatchDialog::onStop()
{
    if (isRunning())
    {
        // The current file is stopped cooperatively, nothing of it is written
        m_cancelToken->cancel();
        m_progressLabel->setText(tr("Stopping..."));
        updateUi();
    }
}

void PDFOCRBatchDialog::onOpenResult()
{
    if (isRunning() || m_filesTable->selectionModel()->selectedRows().isEmpty())
    {
        return;
    }

    const int row = m_filesTable->selectionModel()->selectedRows().front().row();
    const QString outputFile = m_filesTable->item(row, ColumnFile)->data(OutputFileRole).toString();
    if (!outputFile.isEmpty() && QFileInfo::exists(outputFile))
    {
        m_documentToOpen = outputFile;
        saveSettings();
        accept();
    }
}

void PDFOCRBatchDialog::onFileStarted(int row)
{
    setRowStatus(row, tr("Recognizing"), QString());
    m_filesTable->scrollToItem(m_filesTable->item(row, ColumnFile));
    m_progressLabel->setText(tr("File %1 of %2: %3").arg(m_finishedFiles + 1).arg(m_totalFiles).arg(m_filesTable->item(row, ColumnFile)->text()));
}

void PDFOCRBatchDialog::onFileProgress(int row, int finished, int total)
{
    m_filesTable->item(row, ColumnStatus)->setText(tr("Recognizing (%1/%2)").arg(finished).arg(total));
}

void PDFOCRBatchDialog::onFileFinished(int row, FileSummary summary)
{
    ++m_finishedFiles;
    m_progressBar->setValue(m_finishedFiles);

    QString status;
    switch (summary.status)
    {
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Success:
            status = tr("Done");
            ++m_succeededFiles;
            break;
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Skipped:
            status = tr("Skipped");
            ++m_skippedFiles;
            break;
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Failed:
            status = summary.cancelled ? tr("Stopped") : tr("Failed");
            if (!summary.cancelled)
            {
                ++m_failedFiles;
            }
            break;
    }

    setRowStatus(row, status, summary.message);
    m_filesTable->item(row, ColumnPages)->setText(QString::number(summary.pageCount));
    m_filesTable->item(row, ColumnRecognized)->setText(QString::number(summary.resultPages));
    m_filesTable->item(row, ColumnFailed)->setText(QString::number(summary.failedPages));
    m_filesTable->item(row, ColumnWords)->setText(QString::number(summary.wordCount));
    m_filesTable->item(row, ColumnReview)->setText(QString::number(summary.reviewWords));
    m_filesTable->item(row, ColumnTime)->setText(QString::number(double(summary.elapsedMilliseconds) / 1000.0, 'f', 1));
    m_filesTable->item(row, ColumnFile)->setData(OutputFileRole, summary.outputFile);

    QStringList details = summary.details;
    if (!summary.message.isEmpty())
    {
        details.prepend(summary.message);
    }
    m_filesTable->item(row, ColumnMessage)->setToolTip(details.join(QChar('\n')));
    m_filesTable->item(row, ColumnStatus)->setToolTip(details.join(QChar('\n')));
}

void PDFOCRBatchDialog::onBatchFinished()
{
    const bool cancelled = m_cancelToken && m_cancelToken->isOperationCancelled();
    if (m_thread)
    {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    // Files, which were not started, are marked
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        if (m_filesTable->item(row, ColumnStatus)->text() == tr("Waiting"))
        {
            setRowStatus(row, tr("Stopped"), tr("The batch was stopped before the file."));
        }
    }

    QString text = tr("Finished: %1 succeeded, %2 skipped, %3 failed.").arg(m_succeededFiles).arg(m_skippedFiles).arg(m_failedFiles);
    if (cancelled)
    {
        text = tr("Stopped. ") + text;
    }
    m_progressLabel->setText(text);
    updateUi();

    if (m_closeRequested)
    {
        m_closeRequested = false;
        saveSettings();
        BaseClass::reject();
    }
}

void PDFOCRBatchDialog::reject()
{
    if (isRunning())
    {
        if (QMessageBox::question(this, windowTitle(), tr("The batch is running. Do you want to stop it? The file being recognized is not written.")) != QMessageBox::Yes)
        {
            return;
        }

        m_closeRequested = true;
        onStop();
        return;
    }

    saveSettings();
    BaseClass::reject();
}

void PDFOCRBatchDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (!isRunning() && event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void PDFOCRBatchDialog::dropEvent(QDropEvent* event)
{
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
        {
            files << url.toLocalFile();
        }
    }
    addFiles(files);
    event->acceptProposedAction();
}

}   // namespace pdfviewer
