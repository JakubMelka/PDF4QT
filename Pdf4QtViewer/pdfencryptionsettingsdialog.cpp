//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfencryptionsettingsdialog.h"
#include "ui_pdfencryptionsettingsdialog.h"

#include "pdfutils.h"
#include "pdfsecurityhandler.h"

namespace pdfviewer
{

PDFEncryptionSettingsDialog::PDFEncryptionSettingsDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFEncryptionSettingsDialog),
    m_isUpdatingUi(false)
{
    ui->setupUi(this);

    ui->algorithmComboBox->addItem(tr("None"), int(pdf::PDFSecurityHandlerFactory::None));
    ui->algorithmComboBox->addItem(tr("RC4 | R3"), int(pdf::PDFSecurityHandlerFactory::RC4));
    ui->algorithmComboBox->addItem(tr("AES 128-bit | R4"), int(pdf::PDFSecurityHandlerFactory::AES_128));
    ui->algorithmComboBox->addItem(tr("AES 256-bit | R6"), int(pdf::PDFSecurityHandlerFactory::AES_256));

    ui->algorithmComboBox->setCurrentIndex(0);

    ui->algorithmHintWidget->setFixedSize(ui->algorithmHintWidget->minimumSizeHint());
    ui->userPasswordStrengthHintWidget->setFixedSize(ui->userPasswordStrengthHintWidget->minimumSizeHint());
    ui->ownerPasswordStrengthHintWidget->setFixedSize(ui->ownerPasswordStrengthHintWidget->minimumSizeHint());

    ui->algorithmHintWidget->setMinValue(1);
    ui->algorithmHintWidget->setMaxValue(5);

    const int passwordOptimalEntropy = pdf::PDFSecurityHandlerFactory::getPasswordOptimalEntropy();

    ui->userPasswordStrengthHintWidget->setMinValue(0);
    ui->userPasswordStrengthHintWidget->setMaxValue(passwordOptimalEntropy);

    ui->ownerPasswordStrengthHintWidget->setMinValue(0);
    ui->ownerPasswordStrengthHintWidget->setMaxValue(passwordOptimalEntropy);

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

    updateUi();
    updatePasswordScore();
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

    const pdf::PDFSecurityHandlerFactory::Algorithm algorithm = static_cast<const pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    const bool encrypted = algorithm != pdf::PDFSecurityHandlerFactory::None;

    switch (algorithm)
    {
        case pdf::PDFSecurityHandlerFactory::None:
            ui->algorithmHintWidget->setCurrentValue(1);
            break;
        case pdf::PDFSecurityHandlerFactory::RC4:
            ui->algorithmHintWidget->setCurrentValue(2);
            break;
        case pdf::PDFSecurityHandlerFactory::AES_128:
            ui->algorithmHintWidget->setCurrentValue(4);
            break;
        case pdf::PDFSecurityHandlerFactory::AES_256:
            ui->algorithmHintWidget->setCurrentValue(5);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

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

    ui->userPasswordStrengthHintWidget->setEnabled(ui->userPasswordEnableCheckBox->isChecked());
    ui->ownerPasswordStrengthHintWidget->setEnabled(ui->ownerPasswordEnableCheckBox->isChecked());

    ui->encryptAllRadioButton->setEnabled(encrypted);
    ui->encryptAllExceptMetadataRadioButton->setEnabled(encrypted);
    ui->encryptFileAttachmentsOnlyRadioButton->setEnabled(encrypted);

    for (const auto& permissionItem : m_checkBoxToPermission)
    {
        permissionItem.first->setEnabled(encrypted);
    }
}

void PDFEncryptionSettingsDialog::updatePasswordScore()
{
    const pdf::PDFSecurityHandlerFactory::Algorithm algorithm = static_cast<const pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    const int userPasswordScore = pdf::PDFSecurityHandlerFactory::getPasswordEntropy(ui->userPasswordEdit->text(), algorithm);
    const int ownerPasswordScore = pdf::PDFSecurityHandlerFactory::getPasswordEntropy(ui->ownerPasswordEdit->text(), algorithm);

    ui->userPasswordStrengthHintWidget->setCurrentValue(userPasswordScore);
    ui->ownerPasswordStrengthHintWidget->setCurrentValue(ownerPasswordScore);
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

    settings.algorithm = static_cast<const pdf::PDFSecurityHandlerFactory::Algorithm>(ui->algorithmComboBox->currentData().toInt());
    settings.encryptContents = encryptContents;
    settings.userPassword = ui->userPasswordEdit->text();
    settings.ownerPassword = ui->ownerPasswordEdit->text();
    settings.permissions = 0;

    for (auto item : m_checkBoxToPermission)
    {
        if (item.first->isChecked())
        {
            settings.permissions += uint32_t(item.second);
        }
    }

    m_updatedSecurityHandler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);

    QDialog::accept();
}

}   // namespace pdfviewer
