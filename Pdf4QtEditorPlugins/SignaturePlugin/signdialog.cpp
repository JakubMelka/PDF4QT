// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "signdialog.h"
#include "ui_signdialog.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatelisthelper.h"

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

    m_certificates = pdf::PDFCertificateManager::getCertificates();

    pdf::PDFCertificateListHelper::initComboBox(ui->certificateCombo);
    pdf::PDFCertificateListHelper::fillComboBox(ui->certificateCombo, m_certificates);
}

SignDialog::~SignDialog()
{
    delete ui;
}

SignDialog::SignMethod SignDialog::getSignMethod() const
{
    return static_cast<SignMethod>(ui->methodCombo->currentData().toInt());
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

const pdf::PDFCertificateEntry* SignDialog::getCertificate() const
{
    const int index = ui->certificateCombo->currentIndex();
    if (index >= 0 && index < m_certificates.size())
    {
        return &m_certificates.at(index);
    }

    return nullptr;
}

void SignDialog::accept()
{
    const pdf::PDFCertificateEntry* certificate = getCertificate();

    // Check certificate
    if (!certificate)
    {
        QMessageBox::critical(this, tr("Error"), tr("Certificate does not exist."));
        ui->certificateCombo->setFocus();
        return;
    }

    // Check we can access the certificate
    if (!pdf::PDFCertificateManager::isCertificateValid(*certificate, ui->certificatePasswordEdit->text()))
    {
        QMessageBox::critical(this, tr("Error"), tr("Password to open certificate is invalid."));
        ui->certificatePasswordEdit->setFocus();
        return;
    }

    QDialog::accept();
}

}   // namespace pdfplugin



