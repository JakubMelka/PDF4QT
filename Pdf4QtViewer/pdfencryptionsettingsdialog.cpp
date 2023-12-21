//    Copyright (C) 2021-2022 Jakub Melka
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

#include "pdfencryptionsettingsdialog.h"
#include "ui_pdfencryptionsettingsdialog.h"
#include "pdfencryptionstrengthhintwidget.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfsecurityhandler.h"
#include "pdfcertificatemanager.h"

#include <QMessageBox>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFEncryptionSettingsDialog::PDFEncryptionSettingsDialog(QByteArray documentId, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFEncryptionSettingsDialog),
    m_isUpdatingUi(false),
    m_documentId(documentId),
    m_userPasswordStrengthHintWidget(new PDFEncryptionStrengthHintWidget(this)),
    m_ownerPasswordStrengthHintWidget(new PDFEncryptionStrengthHintWidget(this)),
    m_algorithmHintWidget(new PDFEncryptionStrengthHintWidget(this))
{
    ui->setupUi(this);

    ui->algorithmComboBox->addItem(tr("None"), int(pdf::PDFSecurityHandlerFactory::None));
    ui->algorithmComboBox->addItem(tr("RC4 128-bit | R4"), int(pdf::PDFSecurityHandlerFactory::RC4));
    ui->algorithmComboBox->addItem(tr("AES 128-bit | R4"), int(pdf::PDFSecurityHandlerFactory::AES_128));
    ui->algorithmComboBox->addItem(tr("AES 256-bit | R6"), int(pdf::PDFSecurityHandlerFactory::AES_256));
    ui->algorithmComboBox->addItem(tr("Certificate Encryption"), int(pdf::PDFSecurityHandlerFactory::Certificate));

    ui->algorithmComboBox->setCurrentIndex(0);

    m_algorithmHintWidget->setFixedSize(m_algorithmHintWidget->minimumSizeHint());
    m_userPasswordStrengthHintWidget->setFixedSize(m_userPasswordStrengthHintWidget->minimumSizeHint());
    m_ownerPasswordStrengthHintWidget->setFixedSize(m_userPasswordStrengthHintWidget->minimumSizeHint());

    ui->passwordsGroupBoxLayout->addWidget(m_userPasswordStrengthHintWidget, 0, 2);
    ui->passwordsGroupBoxLayout->addWidget(m_ownerPasswordStrengthHintWidget, 1, 2);
    ui->methodGroupBoxLayout->addWidget(m_algorithmHintWidget, 0, 2);

    m_algorithmHintWidget->setMinValue(1);
    m_algorithmHintWidget->setMaxValue(5);

    const int passwordOptimalEntropy = pdf::PDFSecurityHandlerFactory::getPasswordOptimalEntropy();

    m_userPasswordStrengthHintWidget->setMinValue(0);
    m_userPasswordStrengthHintWidget->setMaxValue(passwordOptimalEntropy);

    m_ownerPasswordStrengthHintWidget->setMinValue(0);
    m_ownerPasswordStrengthHintWidget->setMaxValue(passwordOptimalEntropy);

    connect(ui->algorithmComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFEncryptionSettingsDialog::updateUi);
    connect(ui->userPasswordEnableCheckBox, &QCheckBox::clicked, this, &PDFEncryptionSettingsDialog::updateUi);
    connect(ui->ownerPasswordEnableCheckBox, &QCheckBox::clicked, this, &PDFEncryptionSettingsDialog::updateUi);
    connect(ui->userPasswordEdit, &QLineEdit::textChanged, this, &PDFEncryptionSettingsDialog::updatePasswordScore);
    connect(ui->ownerPasswordEdit, &QLineEdit::textChanged, this, &PDFEncryptionSettingsDialog::updatePasswordScore);

    m_checkBoxToPermission[ui->permPrintLowResolutionCheckBox] = pdf::PDFSecurityHandler::Permission::PrintLowResolution;
    m_checkBoxToPermission[ui->permModifyDocumentContentsCheckBox] = pdf::PDFSecurityHandler::Permission::Modify;
    m_checkBoxToPermission[ui->permCopyContentCheckBox] = pdf::PDFSecurityHandler::Permission::CopyContent;
    m_checkBoxToPermission[ui->permInteractiveItemsCheckBox] = pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems;
    m_checkBoxToPermission[ui->permFillInteractiveFormsCheckBox] = pdf::PDFSecurityHandler::Permission::ModifyFormFields;
    m_checkBoxToPermission[ui->permAccessibilityCheckBox] = pdf::PDFSecurityHandler::Permission::Accessibility;
    m_checkBoxToPermission[ui->permAssembleCheckBox] = pdf::PDFSecurityHandler::Permission::Assemble;
    m_checkBoxToPermission[ui->permPrintHighResolutionCheckBox] = pdf::PDFSecurityHandler::Permission::PrintHighResolution;

    updateCertificates();
    updateUi();
    updatePasswordScore();

    pdf::PDFWidgetUtils::style(this);
}

PDFEncryptionSettingsDialog::~PDFEncryptionSettingsDialog()
{
    delete ui;
}

void PDFEncryptionSettingsDialog::updateUi()
{
    if (m_isUpdatingUi)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isUpdatingUi, true);

    const pdf::PDFSecurityHandlerFactory::Algorithm algorithm = static_cast<pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    const bool encrypted = algorithm != pdf::PDFSecurityHandlerFactory::None;
    const bool isEncryptedUsingCertificate = algorithm == pdf::PDFSecurityHandlerFactory::Certificate;

    switch (algorithm)
    {
        case pdf::PDFSecurityHandlerFactory::None:
            m_algorithmHintWidget->setCurrentValue(1);
            break;
        case pdf::PDFSecurityHandlerFactory::RC4:
            m_algorithmHintWidget->setCurrentValue(2);
            break;
        case pdf::PDFSecurityHandlerFactory::AES_128:
            m_algorithmHintWidget->setCurrentValue(4);
            break;
        case pdf::PDFSecurityHandlerFactory::AES_256:
        case pdf::PDFSecurityHandlerFactory::Certificate:
            m_algorithmHintWidget->setCurrentValue(5);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    ui->certificateComboBox->setEnabled(isEncryptedUsingCertificate);

    if (!isEncryptedUsingCertificate)
    {
        ui->userPasswordEnableCheckBox->setEnabled(encrypted);
        ui->ownerPasswordEnableCheckBox->setEnabled(false);

        if (!encrypted)
        {
            ui->userPasswordEnableCheckBox->setChecked(false);
            ui->ownerPasswordEnableCheckBox->setChecked(false);

            ui->userPasswordEdit->clear();
            ui->ownerPasswordEdit->clear();
        }
        else
        {
            ui->ownerPasswordEnableCheckBox->setChecked(true);
        }

        ui->certificateComboBox->setCurrentIndex(-1);
    }
    else
    {
        ui->userPasswordEnableCheckBox->setEnabled(false);
        ui->ownerPasswordEnableCheckBox->setEnabled(false);

        ui->userPasswordEnableCheckBox->setChecked(true);
        ui->ownerPasswordEnableCheckBox->setChecked(false);

        if (ui->certificateComboBox->currentIndex() == -1 && ui->certificateComboBox->count() > 0)
        {
            ui->certificateComboBox->setCurrentIndex(0);
        }
    }

    ui->userPasswordEdit->setEnabled(ui->userPasswordEnableCheckBox->isChecked());
    ui->ownerPasswordEdit->setEnabled(ui->ownerPasswordEnableCheckBox->isChecked());

    if (!ui->userPasswordEdit->isEnabled())
    {
        ui->userPasswordEdit->clear();
    }

    if (!ui->ownerPasswordEdit->isEnabled())
    {
        ui->ownerPasswordEdit->clear();
    }

    m_userPasswordStrengthHintWidget->setEnabled(ui->userPasswordEnableCheckBox->isChecked());
    m_ownerPasswordStrengthHintWidget->setEnabled(ui->ownerPasswordEnableCheckBox->isChecked());

    ui->encryptAllRadioButton->setEnabled(encrypted);
    ui->encryptAllExceptMetadataRadioButton->setEnabled(encrypted);
    ui->encryptFileAttachmentsOnlyRadioButton->setEnabled(encrypted);

    for (const auto& permissionItem : m_checkBoxToPermission)
    {
        permissionItem.first->setEnabled(encrypted);
    }
}

void PDFEncryptionSettingsDialog::updateCertificates()
{
    QFileInfoList certificates = pdf::PDFCertificateManager::getCertificates();

    QVariant currentCertificate = ui->certificateComboBox->currentData();

    ui->certificateComboBox->clear();
    for (const QFileInfo& certificateItem : certificates)
    {
        ui->certificateComboBox->addItem(certificateItem.fileName(), certificateItem.absoluteFilePath());
    }

    ui->certificateComboBox->setCurrentIndex(ui->certificateComboBox->findData(currentCertificate));
}

void PDFEncryptionSettingsDialog::updatePasswordScore()
{
    const pdf::PDFSecurityHandlerFactory::Algorithm algorithm = static_cast<pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    const int userPasswordScore = pdf::PDFSecurityHandlerFactory::getPasswordEntropy(ui->userPasswordEdit->text(), algorithm);
    const int ownerPasswordScore = pdf::PDFSecurityHandlerFactory::getPasswordEntropy(ui->ownerPasswordEdit->text(), algorithm);

    m_userPasswordStrengthHintWidget->setCurrentValue(userPasswordScore);
    m_ownerPasswordStrengthHintWidget->setCurrentValue(ownerPasswordScore);
}

void PDFEncryptionSettingsDialog::accept()
{
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    pdf::PDFSecurityHandlerFactory::EncryptContents encryptContents = pdf::PDFSecurityHandlerFactory::All;

    if (ui->encryptAllExceptMetadataRadioButton->isChecked())
    {
        encryptContents = pdf::PDFSecurityHandlerFactory::AllExceptMetadata;
    }
    else if (ui->encryptFileAttachmentsOnlyRadioButton->isChecked())
    {
        encryptContents = pdf::PDFSecurityHandlerFactory::EmbeddedFiles;
    }

    settings.id = m_documentId;
    settings.algorithm = static_cast<pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    settings.encryptContents = encryptContents;
    settings.userPassword = ui->userPasswordEdit->text();
    settings.ownerPassword = ui->ownerPasswordEdit->text();
    settings.permissions = 0;
    settings.certificateFileName = ui->certificateComboBox->currentData().toString();

    for (auto item : m_checkBoxToPermission)
    {
        if (item.first->isChecked())
        {
            settings.permissions += uint32_t(item.second);
        }
    }

    QString errorMessage;
    if (!pdf::PDFSecurityHandlerFactory::validate(settings, &errorMessage))
    {
        QMessageBox::critical(this, tr("Error"), errorMessage);
        return;
    }

    m_updatedSecurityHandler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);

    QDialog::accept();
}

}   // namespace pdfviewer
