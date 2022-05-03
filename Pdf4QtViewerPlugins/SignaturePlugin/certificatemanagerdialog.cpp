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

namespace pdfplugin
{

CertificateManagerDialog::CertificateManagerDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CertificateManagerDialog),
    m_newCertificateButton(nullptr),
    m_openCertificateDirectoryButton(nullptr)
{
    ui->setupUi(this);

    m_newCertificateButton = ui->buttonBox->addButton(tr("Create Certificate"), QDialogButtonBox::ActionRole);
    m_openCertificateDirectoryButton = ui->buttonBox->addButton(tr("Show Certificate Directory"), QDialogButtonBox::ActionRole);

    connect(m_newCertificateButton, &QPushButton::clicked, this, &CertificateManagerDialog::onNewCertificateClicked);

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
        QDir::root().mkpath(CertificateManager::getCertificateDirectory());

        const CertificateManager::NewCertificateInfo info = dialog.getNewCertificateInfo();
        m_certificateManager.createCertificate(info);
    }
}

}   // namespace pdfplugin
