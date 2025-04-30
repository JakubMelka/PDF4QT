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
