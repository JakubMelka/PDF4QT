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

#include "pdfocrlanguagesdialog.h"
#include "ui_pdfocrlanguagesdialog.h"
#include "pdfwidgetutils.h"

#include <QPushButton>
#include <QDir>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDesktopServices>

namespace pdfviewer
{

enum ModelColumn
{
    ColumnLanguage,
    ColumnCode,
    ColumnProfile,
    ColumnOrigin,
    ColumnVersion,
    ColumnSize,
    ColumnState
};

PDFOCRLanguagesDialog::PDFOCRLanguagesDialog(pdf::PDFOCRModelManager* manager, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFOCRLanguagesDialog),
    m_manager(manager)
{
    ui->setupUi(this);

    // No button is the default one: Enter pressed in an edit box (page range, text of a
    // word, search) must not click an unrelated button of the dialog.
    for (QPushButton* button : findChildren<QPushButton*>())
    {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    if (!m_manager)
    {
        m_manager = new pdf::PDFOCRModelManager(this);
        m_manager->loadBundledCatalog();
        m_manager->refresh();
    }

    ui->profileFilterComboBox->addItem(tr("All profiles"), -1);
    for (const pdf::PDFOCRModelProfile profile : pdf::PDFOCRConfiguration::getProfiles())
    {
        ui->profileFilterComboBox->addItem(pdf::PDFOCRConfiguration::getProfileName(profile), int(profile));
    }

    ui->stateFilterComboBox->addItem(tr("All states"), -1);
    ui->stateFilterComboBox->addItem(tr("Installed and built-in"), -2);
    for (pdf::PDFOCRModelState state : { pdf::PDFOCRModelState::BuiltIn, pdf::PDFOCRModelState::Installed, pdf::PDFOCRModelState::Available,
                                         pdf::PDFOCRModelState::UpdateAvailable, pdf::PDFOCRModelState::Downloading, pdf::PDFOCRModelState::Error })
    {
        ui->stateFilterComboBox->addItem(pdf::PDFOCRModelInfo::getStateName(state), int(state));
    }

    ui->modelsTreeWidget->sortByColumn(ColumnLanguage, Qt::AscendingOrder);
    ui->modelsTreeWidget->header()->setSectionResizeMode(ColumnLanguage, QHeaderView::Stretch);

    connect(ui->searchEdit, &QLineEdit::textChanged, this, &PDFOCRLanguagesDialog::updateModels);
    connect(ui->profileFilterComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRLanguagesDialog::updateModels);
    connect(ui->stateFilterComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRLanguagesDialog::updateModels);
    connect(ui->modelsTreeWidget, &QTreeWidget::itemSelectionChanged, this, &PDFOCRLanguagesDialog::updateUi);
    connect(ui->downloadButton, &QPushButton::clicked, this, [this]() { onDownloadClicked(false); });
    connect(ui->updateButton, &QPushButton::clicked, this, [this]() { onDownloadClicked(true); });
    connect(ui->retryButton, &QPushButton::clicked, this, [this]() { onDownloadClicked(false); });
    connect(ui->cancelButton, &QPushButton::clicked, this, &PDFOCRLanguagesDialog::onCancelClicked);
    connect(ui->importButton, &QPushButton::clicked, this, &PDFOCRLanguagesDialog::onImportClicked);
    connect(ui->removeButton, &QPushButton::clicked, this, &PDFOCRLanguagesDialog::onRemoveClicked);
    connect(ui->hideButton, &QPushButton::clicked, this, &PDFOCRLanguagesDialog::onHideClicked);
    connect(ui->openFolderButton, &QPushButton::clicked, this, &PDFOCRLanguagesDialog::onOpenFolderClicked);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_manager, &pdf::PDFOCRModelManager::modelsChanged, this, &PDFOCRLanguagesDialog::updateModels);
    connect(m_manager, &pdf::PDFOCRModelManager::modelStateChanged, this, &PDFOCRLanguagesDialog::updateModelItem);
    connect(m_manager, &pdf::PDFOCRModelManager::downloadFinished, this, &PDFOCRLanguagesDialog::onDownloadFinished);
    connect(m_manager, &pdf::PDFOCRModelManager::downloadQueueChanged, this, &PDFOCRLanguagesDialog::updateUi);

    ui->infoLabel->setText(tr("Built-in models are part of the application and work without an internet connection. Other models are downloaded from the official "
                              "Tesseract repositories (tessdata_fast, tessdata_best) according to the catalog shipped with the application; every file is verified "
                              "by its size and SHA-256 checksum. Documents and recognized text are never sent to any server. User models are stored in: %1")
                           .arg(QDir::toNativeSeparators(m_manager->getUserDirectory())));

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(980, 620));
    updateModels();
    pdf::PDFWidgetUtils::style(this);
}

PDFOCRLanguagesDialog::~PDFOCRLanguagesDialog()
{
    delete ui;
}

void PDFOCRLanguagesDialog::done(int result)
{
    if (m_manager->isDownloading())
    {
        if (QMessageBox::question(this, tr("Manage OCR Languages"), tr("Downloads are in progress. Do you want to cancel them and close the dialog?")) != QMessageBox::Yes)
        {
            return;
        }
        m_manager->cancelAllDownloads();
    }

    QDialog::done(result);
}

QString PDFOCRLanguagesDialog::formatSize(qint64 bytes)
{
    if (bytes <= 0)
    {
        return QString();
    }
    return tr("%1 MB").arg(QLocale().toString(double(bytes) / (1024.0 * 1024.0), 'f', 1));
}

bool PDFOCRLanguagesDialog::isModelVisible(const pdf::PDFOCRModelInfo& model) const
{
    const QString search = ui->searchEdit->text().trimmed();
    if (!search.isEmpty() && !model.name.contains(search, Qt::CaseInsensitive) && !model.language.contains(search, Qt::CaseInsensitive))
    {
        return false;
    }

    const int profile = ui->profileFilterComboBox->currentData().toInt();
    if (profile >= 0 && int(model.profile) != profile)
    {
        return false;
    }

    const int state = ui->stateFilterComboBox->currentData().toInt();
    if (state == -2 && !model.isUsable())
    {
        return false;
    }
    if (state >= 0 && int(model.state) != state)
    {
        return false;
    }

    return true;
}

void PDFOCRLanguagesDialog::fillItem(QTreeWidgetItem* item, const pdf::PDFOCRModelInfo& model) const
{
    QString name = model.name;
    if (model.isOrientationData())
    {
        name = tr("%1 (not a document language)").arg(model.name);
    }
    if (model.isHidden)
    {
        name = tr("%1 [hidden]").arg(name);
    }

    QString state = pdf::PDFOCRModelInfo::getStateName(model.state);
    if (model.state == pdf::PDFOCRModelState::Downloading)
    {
        state = tr("%1 (%2 %)").arg(state).arg(model.downloadProgress);
    }
    if (!model.errorMessage.isEmpty())
    {
        state = tr("%1 - %2").arg(state, model.errorMessage);
    }

    QString version = model.installedVersion.isEmpty() ? model.version : model.installedVersion;
    if (version.size() > 12)
    {
        version = version.left(12);
    }

    item->setData(ColumnLanguage, Qt::UserRole, model.id);
    item->setText(ColumnLanguage, name);
    item->setText(ColumnCode, model.language);
    item->setText(ColumnProfile, pdf::PDFOCRConfiguration::getProfileName(model.profile));
    item->setText(ColumnOrigin, pdf::PDFOCRModelInfo::getOriginName(model.origin));
    item->setText(ColumnVersion, version);
    item->setText(ColumnSize, formatSize(model.size));
    item->setTextAlignment(ColumnSize, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColumnState, state);
    item->setToolTip(ColumnState, state);
    item->setToolTip(ColumnLanguage, model.path.isEmpty() ? model.id : QDir::toNativeSeparators(model.path));
}

void PDFOCRLanguagesDialog::updateModels()
{
    // Preserve the selection
    QStringList selectedIds;
    for (QTreeWidgetItem* item : ui->modelsTreeWidget->selectedItems())
    {
        selectedIds << item->data(ColumnLanguage, Qt::UserRole).toString();
    }

    const bool sortingEnabled = ui->modelsTreeWidget->isSortingEnabled();
    ui->modelsTreeWidget->setSortingEnabled(false);
    ui->modelsTreeWidget->clear();

    for (const pdf::PDFOCRModelInfo& model : m_manager->getModels())
    {
        if (!isModelVisible(model))
        {
            continue;
        }

        QTreeWidgetItem* item = new QTreeWidgetItem(ui->modelsTreeWidget);
        fillItem(item, model);
        if (selectedIds.contains(model.id))
        {
            item->setSelected(true);
        }
    }

    ui->modelsTreeWidget->setSortingEnabled(sortingEnabled);
    updateUi();
}

void PDFOCRLanguagesDialog::updateModelItem(const QString& modelId)
{
    std::optional<pdf::PDFOCRModelInfo> model = m_manager->getModel(modelId);
    if (!model)
    {
        return;
    }

    for (int i = 0; i < ui->modelsTreeWidget->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = ui->modelsTreeWidget->topLevelItem(i);
        if (item->data(ColumnLanguage, Qt::UserRole).toString() == modelId)
        {
            fillItem(item, *model);
            break;
        }
    }

    updateUi();
}

std::vector<pdf::PDFOCRModelInfo> PDFOCRLanguagesDialog::getSelectedModels() const
{
    std::vector<pdf::PDFOCRModelInfo> models;
    for (QTreeWidgetItem* item : ui->modelsTreeWidget->selectedItems())
    {
        if (std::optional<pdf::PDFOCRModelInfo> model = m_manager->getModel(item->data(ColumnLanguage, Qt::UserRole).toString()))
        {
            models.push_back(*model);
        }
    }
    return models;
}

void PDFOCRLanguagesDialog::updateUi()
{
    const std::vector<pdf::PDFOCRModelInfo> models = getSelectedModels();

    bool canDownload = false;
    bool canUpdate = false;
    bool canCancel = false;
    bool canRetry = false;
    bool canRemove = false;
    QStringList details;

    for (const pdf::PDFOCRModelInfo& model : models)
    {
        canDownload = canDownload || model.state == pdf::PDFOCRModelState::Available;
        canUpdate = canUpdate || model.state == pdf::PDFOCRModelState::UpdateAvailable;
        canCancel = canCancel || model.state == pdf::PDFOCRModelState::Downloading;
        canRetry = canRetry || model.state == pdf::PDFOCRModelState::Error;
        canRemove = canRemove || model.origin == pdf::PDFOCRModelOrigin::Downloaded || model.origin == pdf::PDFOCRModelOrigin::Imported;
    }

    if (models.size() == 1)
    {
        const pdf::PDFOCRModelInfo& model = models.front();
        details << pdf::PDFOCRModelManager::getModelDisplayText(model);
        if (!model.license.isEmpty())
        {
            details << tr("License: %1").arg(model.license);
        }
        if (!model.path.isEmpty())
        {
            details << tr("File: %1").arg(QDir::toNativeSeparators(model.path));
        }
        if (model.origin == pdf::PDFOCRModelOrigin::Imported)
        {
            details << tr("The origin of the imported model is not verified by the catalog.");
        }
    }
    else if (!models.empty())
    {
        qint64 size = 0;
        for (const pdf::PDFOCRModelInfo& model : models)
        {
            size += model.size;
        }
        details << tr("%n model(s) selected, %1", nullptr, int(models.size())).arg(formatSize(size));
    }

    if (!m_messages.isEmpty())
    {
        details << m_messages.last();
    }

    ui->detailLabel->setText(details.join(QStringLiteral(" | ")));
    ui->downloadButton->setEnabled(canDownload);
    ui->updateButton->setEnabled(canUpdate);
    ui->cancelButton->setEnabled(canCancel);
    ui->retryButton->setEnabled(canRetry);
    ui->removeButton->setEnabled(canRemove);
    ui->hideButton->setEnabled(!models.empty());

    const QStringList queue = m_manager->getDownloadQueue();
    ui->queueProgressBar->setVisible(!queue.isEmpty());
    ui->queueLabel->setVisible(!queue.isEmpty());
    if (!queue.isEmpty())
    {
        int progress = 0;
        for (const QString& modelId : queue)
        {
            if (std::optional<pdf::PDFOCRModelInfo> model = m_manager->getModel(modelId))
            {
                progress += model->downloadProgress;
            }
        }
        ui->queueProgressBar->setRange(0, 100);
        ui->queueProgressBar->setValue(progress / int(queue.size()));
        ui->queueLabel->setText(tr("Downloading %n model(s)...", nullptr, int(queue.size())));
    }
}

void PDFOCRLanguagesDialog::onDownloadClicked(bool updateOnly)
{
    QStringList modelIds;
    QStringList names;
    qint64 size = 0;

    for (const pdf::PDFOCRModelInfo& model : getSelectedModels())
    {
        const bool eligible = updateOnly ? model.state == pdf::PDFOCRModelState::UpdateAvailable
                                         : (model.state == pdf::PDFOCRModelState::Available || model.state == pdf::PDFOCRModelState::Error || model.state == pdf::PDFOCRModelState::UpdateAvailable);
        if (eligible)
        {
            modelIds << model.id;
            names << QStringLiteral("%1 (%2, %3)").arg(model.name, model.language, pdf::PDFOCRConfiguration::getProfileName(model.profile));
            size += model.size;
        }
    }

    if (modelIds.isEmpty())
    {
        return;
    }

    // Languages, volume and target folder are shown before the download (LANG-10)
    const QString message = tr("The following models will be downloaded from the internet:\n\n%1\n\nTotal size: %2\nTarget folder: %3\n\nDo you want to continue?")
                            .arg(names.join(QChar('\n')), formatSize(size), QDir::toNativeSeparators(m_manager->getEngineUserDirectory(QStringLiteral("tesseract"))));
    if (QMessageBox::question(this, tr("Download OCR Languages"), message) != QMessageBox::Yes)
    {
        return;
    }

    m_manager->download(modelIds);
}

void PDFOCRLanguagesDialog::onCancelClicked()
{
    for (const pdf::PDFOCRModelInfo& model : getSelectedModels())
    {
        if (model.state == pdf::PDFOCRModelState::Downloading)
        {
            m_manager->cancelDownload(model.id);
        }
    }
}

void PDFOCRLanguagesDialog::onImportClicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Import OCR Language Model"), QString(), tr("Tesseract models (*.traineddata)"));
    if (fileName.isEmpty())
    {
        return;
    }

    // The profile must be given explicitly (LANG-13)
    QStringList profiles;
    for (const pdf::PDFOCRModelProfile profileItem : pdf::PDFOCRConfiguration::getProfiles())
    {
        profiles << pdf::PDFOCRConfiguration::getProfileName(profileItem);
    }
    bool ok = false;
    const QString profileName = QInputDialog::getItem(this, tr("Import OCR Language Model"), tr("Profile, in which the model will be offered:"), profiles, 0, false, &ok);
    if (!ok)
    {
        return;
    }

    const pdf::PDFOCRModelProfile profile = pdf::PDFOCRConfiguration::getProfiles()[size_t(qMax(0, int(profiles.indexOf(profileName))))];

    QString modelId;
    const pdf::PDFOCRError error = m_manager->importModel(fileName, QStringLiteral("tesseract"), profile, &modelId);
    if (error)
    {
        QMessageBox::critical(this, tr("Import OCR Language Model"), error.message);
        return;
    }

    QMessageBox::information(this, tr("Import OCR Language Model"), tr("The model was imported. Its origin is not verified by the catalog; it is offered as a separate language and never replaces a built-in model."));
}

void PDFOCRLanguagesDialog::onRemoveClicked()
{
    QStringList names;
    std::vector<pdf::PDFOCRModelInfo> models;
    for (const pdf::PDFOCRModelInfo& model : getSelectedModels())
    {
        if (model.origin == pdf::PDFOCRModelOrigin::Downloaded || model.origin == pdf::PDFOCRModelOrigin::Imported)
        {
            models.push_back(model);
            names << pdf::PDFOCRModelManager::getModelDisplayText(model);
        }
    }

    if (models.empty())
    {
        return;
    }

    if (QMessageBox::question(this, tr("Remove User Model"), tr("Do you want to remove the following user models?\n\n%1\n\nIf a built-in version of the language exists, it will be used again. OCR projects are not affected.").arg(names.join(QChar('\n')))) != QMessageBox::Yes)
    {
        return;
    }

    for (const pdf::PDFOCRModelInfo& model : models)
    {
        const pdf::PDFOCRError error = m_manager->removeUserModel(model.id);
        if (error)
        {
            QMessageBox::critical(this, tr("Remove User Model"), error.message);
        }
    }
}

void PDFOCRLanguagesDialog::onHideClicked()
{
    for (const pdf::PDFOCRModelInfo& model : getSelectedModels())
    {
        m_manager->setModelHidden(model.id, !model.isHidden);
    }
}

void PDFOCRLanguagesDialog::onOpenFolderClicked()
{
    const QString directory = m_manager->getUserDirectory();
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

void PDFOCRLanguagesDialog::onDownloadFinished(const QString& modelId, bool success, const QString& message)
{
    std::optional<pdf::PDFOCRModelInfo> model = m_manager->getModel(modelId);
    const QString name = model ? model->name : modelId;
    m_messages << (success ? tr("%1: installed.").arg(name) : tr("%1: %2").arg(name, message));
    updateUi();
}

}   // namespace pdfviewer
