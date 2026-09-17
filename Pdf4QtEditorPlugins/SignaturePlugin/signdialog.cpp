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

#include "signdialog.h"
#include "ui_signdialog.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatelisthelper.h"

#include <QComboBox>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>

namespace pdfplugin
{

namespace
{

constexpr const char* SETTINGS_GROUP = "SignaturePlugin";
constexpr const char* SETTINGS_SIGNATURE_TYPE = "SignatureType";
constexpr const char* SETTINGS_TIMESTAMP_URL = "TimestampUrl";

/// Timestamp authorities offered to the user. The combo box is editable, so
/// any other authority can be used, too.
constexpr const char* DEFAULT_TIMESTAMP_URLS[] =
{
    "http://timestamp.digicert.com",
    "http://timestamp.sectigo.com",
    "https://freetsa.org/tsr"
};

}   // namespace

SignDialog::SignDialog(QWidget* parent, bool isSceneEmpty) :
    QDialog(parent),
    ui(new Ui::SignDialog)
{
    ui->setupUi(this);

    ui->methodCombo->addItem(tr("Sign digitally (visible signature)"), SignDigitally);
    ui->methodCombo->addItem(tr("Sign digitally (invisible signature)"), SignDigitallyInvisible);
    ui->methodCombo->setCurrentIndex(isSceneEmpty ? 1 : 0);

    ui->signatureTypeCombo->addItem(tr("Signature"), SignatureOnly);
    ui->signatureTypeCombo->addItem(tr("Signature with timestamp"), SignatureWithTimestamp);
    ui->signatureTypeCombo->addItem(tr("Document timestamp only"), TimestampOnly);
    ui->signatureTypeCombo->setCurrentIndex(0);

    for (const char* timestampUrl : DEFAULT_TIMESTAMP_URLS)
    {
        ui->timestampUrlCombo->addItem(QString::fromLatin1(timestampUrl));
    }
    ui->timestampUrlCombo->setCurrentIndex(0);

    m_certificates = pdf::PDFCertificateManager::getCertificates(pdf::PDFCertificateUsageFilter::DigitalSignature);

    pdf::PDFCertificateListHelper::initComboBox(ui->certificateCombo);
    pdf::PDFCertificateListHelper::fillComboBox(ui->certificateCombo, m_certificates);

    loadSettings();

    connect(ui->signatureTypeCombo, &QComboBox::currentIndexChanged, this, &SignDialog::updateUi);
    updateUi();
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
    const SignatureType signatureType = getSignatureType();

    // A document timestamp is not signed by a certificate of the user, it is
    // signed by the timestamp authority, so no certificate is needed for it.
    if (signatureType != TimestampOnly)
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
    }

    if (signatureType != SignatureOnly)
    {
        const QUrl timestampUrl(getTimestampUrl());
        if (!timestampUrl.isValid() || timestampUrl.scheme().isEmpty() || timestampUrl.host().isEmpty())
        {
            QMessageBox::critical(this, tr("Error"), tr("Address of the timestamp authority is not valid."));
            ui->timestampUrlCombo->setFocus();
            return;
        }
    }

    saveSettings();
    QDialog::accept();
}

void SignDialog::updateUi()
{
    const SignatureType signatureType = getSignatureType();
    const bool isCertificateUsed = signatureType != TimestampOnly;
    const bool isTimestampUsed = signatureType != SignatureOnly;

    // A document timestamp has no visible appearance and no signer, so the
    // settings of the signature are not used by it.
    ui->methodLabel->setEnabled(isCertificateUsed);
    ui->methodCombo->setEnabled(isCertificateUsed);
    ui->certificateLabel->setEnabled(isCertificateUsed);
    ui->certificateCombo->setEnabled(isCertificateUsed);
    ui->passwordLabel->setEnabled(isCertificateUsed);
    ui->certificatePasswordEdit->setEnabled(isCertificateUsed);
    ui->reasonLabel->setEnabled(isCertificateUsed);
    ui->reasonEdit->setEnabled(isCertificateUsed);
    ui->contactInfoLabel->setEnabled(isCertificateUsed);
    ui->contactInfoEdit->setEnabled(isCertificateUsed);

    ui->timestampUrlLabel->setEnabled(isTimestampUsed);
    ui->timestampUrlCombo->setEnabled(isTimestampUsed);
}

void SignDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    const int signatureTypeIndex = ui->signatureTypeCombo->findData(settings.value(SETTINGS_SIGNATURE_TYPE, SignatureOnly).toInt());
    if (signatureTypeIndex != -1)
    {
        ui->signatureTypeCombo->setCurrentIndex(signatureTypeIndex);
    }

    const QString timestampUrl = settings.value(SETTINGS_TIMESTAMP_URL).toString();
    if (!timestampUrl.isEmpty())
    {
        ui->timestampUrlCombo->setCurrentText(timestampUrl);
    }

    settings.endGroup();
}

void SignDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(SETTINGS_SIGNATURE_TYPE, int(getSignatureType()));
    settings.setValue(SETTINGS_TIMESTAMP_URL, getTimestampUrl());
    settings.endGroup();
}

}   // namespace pdfplugin



