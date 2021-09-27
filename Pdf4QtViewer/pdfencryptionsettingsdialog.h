//    Copyright (C) 2021 Jakub Melka
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

#ifndef PDFENCRYPTIONSETTINGSDIALOG_H
#define PDFENCRYPTIONSETTINGSDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfsecurityhandler.h"

#include <QDialog>

namespace Ui
{
class PDFEncryptionSettingsDialog;
}

class QCheckBox;

namespace pdfviewer
{

class PDFEncryptionSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFEncryptionSettingsDialog(QByteArray documentId, QWidget* parent);
    virtual ~PDFEncryptionSettingsDialog() override;

    pdf::PDFSecurityHandlerPointer getUpdatedSecurityHandler() const { return m_updatedSecurityHandler; }

public slots:
    virtual void accept() override;

private:
    Ui::PDFEncryptionSettingsDialog* ui;

    void updateUi();
    void updatePasswordScore();

    bool m_isUpdatingUi;
    std::map<QCheckBox*, pdf::PDFSecurityHandler::Permission> m_checkBoxToPermission;
    pdf::PDFSecurityHandlerPointer m_updatedSecurityHandler;
    QByteArray m_documentId;
};

}   // namespace pdfviewer

#endif // PDFENCRYPTIONSETTINGSDIALOG_H

