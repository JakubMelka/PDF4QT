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

#include "signdialog.h"
#include "ui_signdialog.h"

#include "pdfcertificatemanager.h"

#include <openssl/pkcs7.h>

#include <QMessageBox>

namespace pdfplugin
{

SignDialog::SignDialog(QWidget* parent, bool isSceneEmpty) :
    QDialog(parent),
    ui(new Ui::SignDialog)
{
    ui->setupUi(this);

    if (!isSceneEmpty)
    {
        ui->methodCombo->addItem(tr("Sign digitally"), SignDigitally);
    }

    ui->methodCombo->addItem(tr("Sign digitally (invisible signature)"), SignDigitallyInvisible);
    ui->methodCombo->setCurrentIndex(0);

    QFileInfoList certificates = pdf::PDFCertificateManager::getCertificates();
    for (const QFileInfo& certificateFileInfo : certificates)
    {
        ui->certificateCombo->addItem(certificateFileInfo.fileName(), certificateFileInfo.absoluteFilePath());
    }
}

SignDialog::~SignDialog()
{
    delete ui;
}

SignDialog::SignMethod SignDialog::getSignMethod() const
{
    return static_cast<SignMethod>(ui->methodCombo->currentData().toInt());
}

QString SignDialog::getCertificatePath() const
{
    return ui->certificateCombo->currentData().toString();
}

QString SignDialog::getPassword() const
{
    return ui->certificatePasswordEdit->text();
}

QString SignDialog::getReasonText() const
{
    return ui->reasonEdit->text();
}

QString SignDialog::getContactInfoText() const
{
    return ui->contactInfoEdit->text();
}

void SignDialog::accept()
{
    // Check certificate
    if (!QFile::exists(getCertificatePath()))
    {
        QMessageBox::critical(this, tr("Error"), tr("Certificate does not exist."));
        ui->certificateCombo->setFocus();
        return;
    }

    // Check we can access the certificate
    if (!pdf::PDFCertificateManager::isCertificateValid(getCertificatePath(), ui->certificatePasswordEdit->text()))
    {
        QMessageBox::critical(this, tr("Error"), tr("Password to open certificate is invalid."));
        ui->certificatePasswordEdit->setFocus();
        return;
    }

    QDialog::accept();
}

}   // namespace pdfplugin



