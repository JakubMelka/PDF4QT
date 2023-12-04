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

#ifndef PDFCREATECERTIFICATEDIALOG_H
#define PDFCREATECERTIFICATEDIALOG_H

#include "pdfcertificatemanager.h"

#include <QDialog>

namespace Ui
{
class PDFCreateCertificateDialog;
}

namespace pdf
{

class PDF4QTLIBCORESHARED_EXPORT PDFCreateCertificateDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFCreateCertificateDialog(QWidget* parent);
    virtual ~PDFCreateCertificateDialog() override;

    const PDFCertificateManager::NewCertificateInfo& getNewCertificateInfo() const { return m_newCertificateInfo; }

public slots:
    virtual void accept() override;

private:
    bool validate();

    PDFCertificateManager::NewCertificateInfo m_newCertificateInfo;

    Ui::PDFCreateCertificateDialog* ui;
};

} // namespace pdf

#endif // PDFCREATECERTIFICATEDIALOG_H
