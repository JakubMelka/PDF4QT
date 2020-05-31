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
