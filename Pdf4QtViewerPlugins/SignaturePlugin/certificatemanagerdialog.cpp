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

#include "certificatemanagerdialog.h"
#include "ui_certificatemanagerdialog.h"
#include "createcertificatedialog.h"

#include "pdfwidgetutils.h"

#include <QAction>
#include <QPushButton>
#include <QFileSystemModel>
#include <QDesktopServices>
#include <QMessageBox>
#include <QFileDialog>

namespace pdfplugin
{

CertificateManagerDialog::CertificateManagerDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CertificateManagerDialog),
    m_newCertificateButton(nullptr),
    m_openCertificateDirectoryButton(nullptr),
    m_deleteCertificateButton(nullptr),
    m_importCertificateButton(nullptr),
    m_certificateFileModel(nullptr)
{
    ui->setupUi(this);

    QDir::root().mkpath(CertificateManager::getCertificateDirectory());

    m_certificateFileModel = new QFileSystemModel(this);
    QModelIndex rootIndex = m_certificateFileModel->setRootPath(CertificateManager::getCertificateDirectory());
    ui->fileView->setModel(m_certificateFileModel);
    ui->fileView->setRootIndex(rootIndex);

    m_newCertificateButton = ui->buttonBox->addButton(tr("Create"), QDialogButtonBox::ActionRole);
    m_openCertificateDirectoryButton = ui->buttonBox->addButton(tr("Open Directory"), QDialogButtonBox::ActionRole);
    m_deleteCertificateButton = ui->buttonBox->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    m_importCertificateButton = ui->buttonBox->addButton(tr("Import"), QDialogButtonBox::ActionRole);

    connect(m_newCertificateButton, &QPushButton::clicked, this, &CertificateManagerDialog::onNewCertificateClicked);
    connect(m_openCertificateDirectoryButton, &QPushButton::clicked, this, &CertificateManagerDialog::onOpenCertificateDirectoryClicked);
    connect(m_deleteCertificateButton, &QPushButton::clicked, this, &CertificateManagerDialog::onDeleteCertificateClicked);
    connect(m_importCertificateButton, &QPushButton::clicked, this, &CertificateManagerDialog::onImportCertificateClicked);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 480)));
}

CertificateManagerDialog::~CertificateManagerDialog()
{
    delete ui;
}

void CertificateManagerDialog::onNewCertificateClicked()
{
    CreateCertificateDialog dialog(this);
    if (dialog.exec() == CreateCertificateDialog::Accepted)
    {
        const CertificateManager::NewCertificateInfo info = dialog.getNewCertificateInfo();
        m_certificateManager.createCertificate(info);
    }
}

void CertificateManagerDialog::onOpenCertificateDirectoryClicked()
{
    QDesktopServices::openUrl(QString("file:///%1").arg(CertificateManager::getCertificateDirectory(), QUrl::TolerantMode));
}

void CertificateManagerDialog::onDeleteCertificateClicked()
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

void CertificateManagerDialog::onImportCertificateClicked()
{
    QString selectedFile = QFileDialog::getOpenFileName(this, tr("Import Certificate"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Certificate file (*.pfx);;All files (*.*)"));

    if (selectedFile.isEmpty())
    {
        return;
    }

    QFile file(selectedFile);
    if (file.exists())
    {
        QString path = CertificateManager::getCertificateDirectory();
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

}   // namespace pdfplugin
