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
