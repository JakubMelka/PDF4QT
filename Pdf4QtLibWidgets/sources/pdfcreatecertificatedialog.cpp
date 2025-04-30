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

#include "pdfcreatecertificatedialog.h"
#include "ui_pdfcreatecertificatedialog.h"

#include "pdfcertificatemanager.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QDate>
#include <QCalendar>

namespace pdf
{

PDFCreateCertificateDialog::PDFCreateCertificateDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateCertificateDialog)
{
    ui->setupUi(this);

    ui->fileNameEdit->setReadOnly(true);
    ui->fileNameEdit->setText(PDFCertificateManager::generateCertificateFileName());

    ui->keyLengthCombo->addItem(tr("1024 bits"), 1024);
    ui->keyLengthCombo->addItem(tr("2048 bits"), 2048);
    ui->keyLengthCombo->addItem(tr("4096 bits"), 4096);
    ui->keyLengthCombo->setCurrentIndex(ui->keyLengthCombo->findData(2048));

    QList<QLocale> locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);
    std::sort(locales.begin(), locales.end(), [](const QLocale& left, const QLocale& right) { return QString::compare(left.nativeTerritoryName(), right.nativeTerritoryName(), Qt::CaseInsensitive) < 0; });

    int currentIndex = 0;
    QLocale currentLocale = QLocale::system();

    for (const QLocale& locale : locales)
    {
        if (locale.territory() == QLocale::AnyTerritory)
        {
            continue;
        }

        if (locale.nativeTerritoryName().isEmpty())
        {
            continue;
        }

        QString localeName = locale.name();
        QString countryCode = localeName.split(QChar('_')).back();
        QString text = QString("%1 | %2").arg(countryCode, locale.nativeTerritoryName());

        if (ui->countryCombo->findText(text) >= 0)
        {
            continue;
        }

        if (locale.bcp47Name() == currentLocale.bcp47Name())
        {
            currentIndex = ui->countryCombo->count();
        }

        ui->countryCombo->addItem(text, countryCode);
    }

    ui->countryCombo->setCurrentIndex(currentIndex);
    ui->countryCombo->setMaxVisibleItems(25);

    QDate minDate = QDate::currentDate();
    ui->validTillEdit->setMinimumDate(minDate);

    QDate selectedDate = minDate;
    selectedDate = selectedDate.addYears(5, ui->validTillEdit->calendar());
    ui->validTillEdit->setSelectedDate(selectedDate);
}

PDFCreateCertificateDialog::~PDFCreateCertificateDialog()
{
    delete ui;
}

void PDFCreateCertificateDialog::accept()
{
    if (validate())
    {
        bool ok = false;
        QString password1 = QInputDialog::getText(this, tr("Certificate Protection"), tr("Enter password to protect your certificate."), QLineEdit::Password, QString(), &ok);

        if (!ok)
        {
            return;
        }

        QString password2 = QInputDialog::getText(this, tr("Certificate Protection"), tr("Enter password again to verify password text."), QLineEdit::Password, QString(), &ok);

        if (password1 != password2)
        {
            QMessageBox::critical(this, tr("Error"), tr("Reentered password is not equal to the first one!"));
            return;
        }

        QDate date = ui->validTillEdit->selectedDate();
        QDate currentDate = QDate::currentDate();
        int days = currentDate.daysTo(date);

        // Fill certificate info
        m_newCertificateInfo.fileName = ui->fileNameEdit->text();
        m_newCertificateInfo.privateKeyPasword = password1;
        m_newCertificateInfo.certCountryCode = ui->countryCombo->currentData().toString();
        m_newCertificateInfo.certOrganization = ui->organizationEdit->text();
        m_newCertificateInfo.certOrganizationUnit = ui->organizationUnitEdit->text();
        m_newCertificateInfo.certCommonName = ui->commonNameEdit->text();
        m_newCertificateInfo.certEmail = ui->emailEdit->text();
        m_newCertificateInfo.rsaKeyLength = ui->keyLengthCombo->currentData().toInt();
        m_newCertificateInfo.validityInSeconds = days * 24 * 3600;

        BaseClass::accept();
    }
}

bool PDFCreateCertificateDialog::validate()
{
    // validate empty text fields
    if (ui->commonNameEdit->text().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Please enter a name!"));
        ui->commonNameEdit->setFocus();
        return false;
    }

    if (ui->organizationEdit->text().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Please enter an organization name!"));
        ui->organizationEdit->setFocus();
        return false;
    }

    if (ui->emailEdit->text().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Please enter an email address!"));
        ui->emailEdit->setFocus();
        return false;
    }

    return true;
}

} // namespace pdf
