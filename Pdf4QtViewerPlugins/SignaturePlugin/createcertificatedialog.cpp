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

#include "createcertificatedialog.h"
#include "ui_createcertificatedialog.h"

#include "certificatemanager.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QDate>
#include <QCalendar>

namespace pdfplugin
{

CreateCertificateDialog::CreateCertificateDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateCertificateDialog)
{
    ui->setupUi(this);

    ui->fileNameEdit->setReadOnly(true);
    ui->fileNameEdit->setText(CertificateManager::generateCertificateFileName());

    ui->keyLengthCombo->addItem(tr("1024 bits"), 1024);
    ui->keyLengthCombo->addItem(tr("2048 bits"), 2048);
    ui->keyLengthCombo->addItem(tr("4096 bits"), 4096);
    ui->keyLengthCombo->setCurrentIndex(ui->keyLengthCombo->findData(2048));

    QList<QLocale> locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);
    std::sort(locales.begin(), locales.end(), [](const QLocale& left, const QLocale& right) { return QString::compare(left.nativeCountryName(), right.nativeCountryName(), Qt::CaseInsensitive) < 0; });

    int currentIndex = 0;
    QLocale currentLocale = QLocale::system();

    for (const QLocale& locale : locales)
    {
        if (locale.country() == QLocale::AnyCountry)
        {
            continue;
        }

        if (locale.nativeCountryName().isEmpty())
        {
            continue;
        }

        QString localeName = locale.name();
        QString countryCode = localeName.split(QChar('_')).back();
        QString text = QString("%1 | %2").arg(countryCode, locale.nativeCountryName());

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

CreateCertificateDialog::~CreateCertificateDialog()
{
    delete ui;
}

void CreateCertificateDialog::accept()
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

bool CreateCertificateDialog::validate()
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

} // namespace plugin
