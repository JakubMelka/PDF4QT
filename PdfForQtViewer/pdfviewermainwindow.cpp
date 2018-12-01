#include "pdfviewermainwindow.h"
#include "ui_pdfviewermainwindow.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"

#include <QFileDialog>
#include <QMessageBox>

namespace pdfviewer
{

PDFViewerMainWindow::PDFViewerMainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::PDFViewerMainWindow)
{
    ui->setupUi(this);

    connect(ui->actionOpen, &QAction::triggered, this, &PDFViewerMainWindow::onActionOpenTriggered);
}

PDFViewerMainWindow::~PDFViewerMainWindow()
{
    delete ui;
}

void PDFViewerMainWindow::onActionOpenTriggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), "K:/Programming/PDF/testpdf", tr("PDF document (*.pdf)"));
    if (!fileName.isEmpty())
    {
        pdf::PDFDocumentReader reader;
        pdf::PDFDocument document = reader.readFromFile(fileName);
        pdf::PDFStatisticsCollector collector;
        pdf::PDFApplyVisitor(document, &collector);

        if (reader.isSuccessfull())
        {
            QMessageBox::information(this, tr("PDF Reader"), tr("Document '%1' was successfully loaded!").arg(fileName));
        }
        else
        {
            QMessageBox::information(this, tr("PDF Reader"), tr("Document read error: %1").arg(reader.getErrorMessage()));
        }
    }
}

}   // namespace pdfviewer
