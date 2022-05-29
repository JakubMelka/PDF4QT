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

#ifndef CERTIFICATEMANAGERDIALOG_H
#define CERTIFICATEMANAGERDIALOG_H

#include "certificatemanager.h"

#include <QDialog>

class QAction;
class QFileSystemModel;

namespace Ui
{
class CertificateManagerDialog;
}

namespace pdfplugin
{

class CertificateManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CertificateManagerDialog(QWidget* parent);
    virtual ~CertificateManagerDialog() override;

private:
    void onNewCertificateClicked();
    void onOpenCertificateDirectoryClicked();
    void onDeleteCertificateClicked();
    void onImportCertificateClicked();

    Ui::CertificateManagerDialog* ui;
    CertificateManager m_certificateManager;
    QPushButton* m_newCertificateButton;
    QPushButton* m_openCertificateDirectoryButton;
    QPushButton* m_deleteCertificateButton;
    QPushButton* m_importCertificateButton;
    QFileSystemModel* m_certificateFileModel;
};

}   // namespace pdfplugin

#endif // CERTIFICATEMANAGERDIALOG_H
