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

#ifndef PDFCERTIFICATEMANAGERDIALOG_H
#define PDFCERTIFICATEMANAGERDIALOG_H

#include "pdfwidgetsglobal.h"
#include "pdfcertificatemanager.h"

#include <QDialog>

class QAction;
class QFileSystemModel;

namespace Ui
{
class PDFCertificateManagerDialog;
}

namespace pdf
{

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCertificateManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFCertificateManagerDialog(QWidget* parent);
    virtual ~PDFCertificateManagerDialog() override;

private:
    void onNewCertificateClicked();
    void onOpenCertificateDirectoryClicked();
    void onDeleteCertificateClicked();
    void onImportCertificateClicked();

    Ui::PDFCertificateManagerDialog* ui;
    PDFCertificateManager m_certificateManager;
    QPushButton* m_newCertificateButton;
    QPushButton* m_openCertificateDirectoryButton;
    QPushButton* m_deleteCertificateButton;
    QPushButton* m_importCertificateButton;
    QFileSystemModel* m_certificateFileModel;
};

}   // namespace pdf

#endif // PDFCERTIFICATEMANAGERDIALOG_H
