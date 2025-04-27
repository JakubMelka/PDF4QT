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

#ifndef PDFDOCUMENTPROPERTIESDIALOG_H
#define PDFDOCUMENTPROPERTIESDIALOG_H

#include "pdfglobal.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>
#include <QDateTime>

class QTreeWidgetItem;

namespace Ui
{
class PDFDocumentPropertiesDialog;
}

namespace pdf
{
class PDFDocument;
}

namespace pdfviewer
{

struct PDFFileInfo
{
    QString originalFileName;
    QString absoluteFilePath;
    QString fileName;
    QString path;
    pdf::PDFInteger fileSize = 0;
    bool writable = false;
    QDateTime creationTime;
    QDateTime lastModifiedTime;
    QDateTime lastReadTime;
};

class PDFDocumentPropertiesDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFDocumentPropertiesDialog(const pdf::PDFDocument* document,
                                         const PDFFileInfo* fileInfo,
                                         QWidget* parent);
    virtual ~PDFDocumentPropertiesDialog() override;

protected:
    virtual void closeEvent(QCloseEvent* event) override;

private:
    Ui::PDFDocumentPropertiesDialog* ui;

    void initializeProperties(const pdf::PDFDocument* document);
    void initializeFileInfoProperties(const PDFFileInfo* fileInfo);
    void initializeSecurity(const pdf::PDFDocument* document);
    void initializeFonts(const pdf::PDFDocument* document);
    void initializeDisplayAndPrintSettings(const pdf::PDFDocument* document);

    void onFontsFinished();

    std::vector<QTreeWidgetItem*> m_fontTreeWidgetItems;
    QFuture<void> m_future;
    QFutureWatcher<void> m_futureWatcher;
};

}   // namespace pdfviewer

#endif // PDFDOCUMENTPROPERTIESDIALOG_H
