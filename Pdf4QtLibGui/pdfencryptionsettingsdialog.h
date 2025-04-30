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

#ifndef PDFENCRYPTIONSETTINGSDIALOG_H
#define PDFENCRYPTIONSETTINGSDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfsecurityhandler.h"
#include "pdfcertificatestore.h"

#include <QDialog>

namespace Ui
{
class PDFEncryptionSettingsDialog;
}

class QCheckBox;

namespace pdfviewer
{
class PDFEncryptionStrengthHintWidget;

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
    void updateCertificates();
    void updatePasswordScore();

    bool m_isUpdatingUi;
    std::map<QCheckBox*, pdf::PDFSecurityHandler::Permission> m_checkBoxToPermission;
    pdf::PDFSecurityHandlerPointer m_updatedSecurityHandler;
    QByteArray m_documentId;
    PDFEncryptionStrengthHintWidget* m_userPasswordStrengthHintWidget;
    PDFEncryptionStrengthHintWidget* m_ownerPasswordStrengthHintWidget;
    PDFEncryptionStrengthHintWidget* m_algorithmHintWidget;
    pdf::PDFCertificateEntries m_certificates;
};

}   // namespace pdfviewer

#endif // PDFENCRYPTIONSETTINGSDIALOG_H

