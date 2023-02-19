//    Copyright (C) 2023 Jakub Melka
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

#ifndef PDFSANITIZEDOCUMENTDIALOG_H
#define PDFSANITIZEDOCUMENTDIALOG_H

#include "pdfdocumentsanitizer.h"

#include <QDialog>
#include <QFuture>

namespace Ui
{
class PDFSanitizeDocumentDialog;
}

namespace pdfviewer
{

class PDFSanitizeDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFSanitizeDocumentDialog(const pdf::PDFDocument* document, QWidget* parent);
    virtual ~PDFSanitizeDocumentDialog() override;

    pdf::PDFDocument takeSanitizedDocument() { return qMove(m_sanitizedDocument); }

signals:
    void displaySanitizationInfo();

private:
    void sanitize();
    void onSanitizeButtonClicked();
    void onSanitizationStarted();
    void onSanitizationProgress(QString progressText);
    void onSanitizationFinished();
    void onDisplaySanitizationInfo();

    void updateUi();

    struct SanitizationInfo
    {
        qreal msecsElapsed = 0.0;
        qint64 bytesBeforeSanitization = -1;
        qint64 bytesAfterSanitization = -1;
    };

    Ui::PDFSanitizeDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFDocumentSanitizer m_sanitizer;
    QPushButton* m_sanitizeButton;
    bool m_sanitizationInProgress;
    bool m_wasSanitized;
    QFuture<void> m_future;
    pdf::PDFDocument m_sanitizedDocument;
    SanitizationInfo m_sanitizationInfo;
};

}   // namespace pdfviewer

#endif // PDFSANITIZEDOCUMENTDIALOG_H
