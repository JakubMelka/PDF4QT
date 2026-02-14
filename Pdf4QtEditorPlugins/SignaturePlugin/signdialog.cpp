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
#include <QComboBox>

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

    ui->signatureTypeCombo->addItem(tr("Signature"), SignatureOnly);
    ui->signatureTypeCombo->addItem(tr("Signature + timestamp"), SignatureWithTimestamp);
    ui->signatureTypeCombo->addItem(tr("Timestamp only"), TimestampOnly);
    ui->signatureTypeCombo->setCurrentIndex(0);

    ui->timestampUrlCombo->setEditable(true);
    ui->timestampUrlCombo->addItem("https://freetsa.org/tsr");
    ui->timestampUrlCombo->addItem("http://timestamp.sectigo.com");
    ui->timestampUrlCombo->addItem("http://timestamp.digicert.com");
    ui->timestampUrlCombo->setCurrentIndex(0);

    m_certificates = pdf::PDFCertificateManager::getCertificates();

    pdf::PDFCertificateListHelper::initComboBox(ui->certificateCombo);
    pdf::PDFCertificateListHelper::fillComboBox(ui->certificateCombo, m_certificates);

    connect(ui->signatureTypeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) { updateUiForSignatureType(); });
    updateUiForSignatureType();
}

SignDialog::~SignDialog()
{
    delete ui;
}

SignDialog::SignMethod SignDialog::getSignMethod() const
{
    return static_cast<SignMethod>(ui->methodCombo->currentData().toInt());
}

SignDialog::SignatureType SignDialog::getSignatureType() const
{
    return static_cast<SignatureType>(ui->signatureTypeCombo->currentData().toInt());
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

QString SignDialog::getTimestampUrl() const
{
    return ui->timestampUrlCombo->currentText().trimmed();
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
    const bool requiresCertificate = getSignatureType() != TimestampOnly;
    const bool requiresTimestamp = getSignatureType() != SignatureOnly;

    if (requiresCertificate)
    {
        const pdf::PDFCertificateEntry* certificate = getCertificate();

        if (!certificate)
        {
            QMessageBox::critical(this, tr("Error"), tr("Certificate does not exist."));
            ui->certificateCombo->setFocus();
            return;
        }

        if (!pdf::PDFCertificateManager::isCertificateValid(*certificate, ui->certificatePasswordEdit->text()))
        {
            QMessageBox::critical(this, tr("Error"), tr("Password to open certificate is invalid."));
            ui->certificatePasswordEdit->setFocus();
            return;
        }
    }

    if (requiresTimestamp && getTimestampUrl().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Timestamp URL must not be empty."));
        ui->timestampUrlCombo->setFocus();
        return;
    }

    QDialog::accept();
}

void SignDialog::updateUiForSignatureType()
{
    const SignatureType signatureType = getSignatureType();
    const bool requiresCertificate = signatureType != TimestampOnly;
    const bool requiresTimestamp = signatureType != SignatureOnly;
    const bool allowVisibleSignature = signatureType != TimestampOnly;

    ui->certificateLabel->setEnabled(requiresCertificate);
    ui->certificateCombo->setEnabled(requiresCertificate);
    ui->passwordLabel->setEnabled(requiresCertificate);
    ui->certificatePasswordEdit->setEnabled(requiresCertificate);
    ui->methodLabel->setEnabled(allowVisibleSignature);
    ui->methodCombo->setEnabled(allowVisibleSignature);

    ui->timestampUrlLabel->setEnabled(requiresTimestamp);
    ui->timestampUrlCombo->setEnabled(requiresTimestamp);

    ui->reasonLabel->setEnabled(requiresCertificate);
    ui->reasonEdit->setEnabled(requiresCertificate);
    ui->contactInfoLabel->setEnabled(requiresCertificate);
    ui->contactInfoEdit->setEnabled(requiresCertificate);
}

}   // namespace pdfplugin



