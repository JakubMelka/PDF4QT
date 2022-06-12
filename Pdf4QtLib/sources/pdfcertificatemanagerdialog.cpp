//    Copyright (C) 2022 Jakub Melka
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

#include "pdfcertificatemanagerdialog.h"
#include "ui_pdfcertificatemanagerdialog.h"
#include "pdfcreatecertificatedialog.h"

#include "pdfwidgetutils.h"

#include <QAction>
#include <QPushButton>
#include <QFileSystemModel>
#include <QDesktopServices>
#include <QMessageBox>
#include <QFileDialog>

namespace pdf
{

PDFCertificateManagerDialog::PDFCertificateManagerDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFCertificateManagerDialog),
    m_newCertificateButton(nullptr),
    m_openCertificateDirectoryButton(nullptr),
    m_deleteCertificateButton(nullptr),
    m_importCertificateButton(nullptr),
    m_certificateFileModel(nullptr)
{
    ui->setupUi(this);

    QDir::root().mkpath(PDFCertificateManager::getCertificateDirectory());

    m_certificateFileModel = new QFileSystemModel(this);
    QModelIndex rootIndex = m_certificateFileModel->setRootPath(PDFCertificateManager::getCertificateDirectory());
    ui->fileView->setModel(m_certificateFileModel);
    ui->fileView->setRootIndex(rootIndex);

    m_newCertificateButton = ui->buttonBox->addButton(tr("Create"), QDialogButtonBox::ActionRole);
    m_openCertificateDirectoryButton = ui->buttonBox->addButton(tr("Open Directory"), QDialogButtonBox::ActionRole);
    m_deleteCertificateButton = ui->buttonBox->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    m_importCertificateButton = ui->buttonBox->addButton(tr("Import"), QDialogButtonBox::ActionRole);

    connect(m_newCertificateButton, &QPushButton::clicked, this, &PDFCertificateManagerDialog::onNewCertificateClicked);
    connect(m_openCertificateDirectoryButton, &QPushButton::clicked, this, &PDFCertificateManagerDialog::onOpenCertificateDirectoryClicked);
    connect(m_deleteCertificateButton, &QPushButton::clicked, this, &PDFCertificateManagerDialog::onDeleteCertificateClicked);
    connect(m_importCertificateButton, &QPushButton::clicked, this, &PDFCertificateManagerDialog::onImportCertificateClicked);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 480)));
}

PDFCertificateManagerDialog::~PDFCertificateManagerDialog()
{
    delete ui;
}

void PDFCertificateManagerDialog::onNewCertificateClicked()
{
    PDFCreateCertificateDialog dialog(this);
    if (dialog.exec() == PDFCreateCertificateDialog::Accepted)
    {
        const PDFCertificateManager::NewCertificateInfo info = dialog.getNewCertificateInfo();
        m_certificateManager.createCertificate(info);
    }
}

void PDFCertificateManagerDialog::onOpenCertificateDirectoryClicked()
{
    QDesktopServices::openUrl(QString("file:///%1").arg(PDFCertificateManager::getCertificateDirectory(), QUrl::TolerantMode));
}

void PDFCertificateManagerDialog::onDeleteCertificateClicked()
{
    QFileInfo fileInfo = m_certificateFileModel->fileInfo(ui->fileView->currentIndex());
    if (fileInfo.exists())
    {
        if (QMessageBox::question(this, tr("Confirm delete"), tr("Do you want to delete certificate '%1'?").arg(fileInfo.fileName()), QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
        {
            QFile file(fileInfo.filePath());
            if (!file.remove())
            {
                QMessageBox::critical(this, tr("Error"), tr("Cannot delete certificate '%1'").arg(fileInfo.fileName()));
            }
        }
    }
}

void PDFCertificateManagerDialog::onImportCertificateClicked()
{
    QString selectedFile = QFileDialog::getOpenFileName(this, tr("Import Certificate"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Certificate file (*.pfx);;All files (*.*)"));

    if (selectedFile.isEmpty())
    {
        return;
    }

    QFile file(selectedFile);
    if (file.exists())
    {
        QString path = PDFCertificateManager::getCertificateDirectory();
        QString targetFile = QString("%1/%2").arg(path, QFileInfo(file).fileName());
        if (QFile::exists(targetFile))
        {
            QMessageBox::critical(this, tr("Error"), tr("Target file exists. Please rename the certificate file to import."));
        }
        else
        {
            if (file.copy(targetFile))
            {
                QMessageBox::information(this, tr("Import Certificate"), tr("Certificate '%1' was successfully imported.").arg(file.fileName()));
            }
            else
            {
                QMessageBox::critical(this, tr("Import Certificate"), tr("Error occured during certificate '%1' import.").arg(file.fileName()));
            }
        }
    }
}

}   // namespace pdf
