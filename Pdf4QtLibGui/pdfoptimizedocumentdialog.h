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

#ifndef PDFOPTIMIZEDOCUMENTDIALOG_H
#define PDFOPTIMIZEDOCUMENTDIALOG_H

#include "pdfoptimizer.h"

#include <QDialog>
#include <QFuture>

namespace Ui
{
class PDFOptimizeDocumentDialog;
}

namespace pdfviewer
{

class PDFOptimizeDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFOptimizeDocumentDialog(const pdf::PDFDocument* document, QWidget* parent);
    virtual ~PDFOptimizeDocumentDialog() override;

    pdf::PDFDocument takeOptimizedDocument() { return qMove(m_optimizedDocument); }

signals:
    void displayOptimizationInfo();

private:
    void optimize();
    void onOptimizeButtonClicked();
    void onOptimizationStarted();
    void onOptimizationProgress(QString progressText);
    void onOptimizationFinished();
    void onDisplayOptimizationInfo();

    void updateUi();

    struct OptimizationInfo
    {
        qreal msecsElapsed = 0.0;
        qint64 bytesBeforeOptimization = -1;
        qint64 bytesAfterOptimization = -1;
    };

    Ui::PDFOptimizeDocumentDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFOptimizer m_optimizer;
    QPushButton* m_optimizeButton;
    bool m_optimizationInProgress;
    bool m_wasOptimized;
    QFuture<void> m_future;
    pdf::PDFDocument m_optimizedDocument;
    OptimizationInfo m_optimizationInfo;
};

}   // namespace pdfviewer

#endif // PDFOPTIMIZEDOCUMENTDIALOG_H
