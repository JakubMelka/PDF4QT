// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfaboutdialog.h"
#include "pdfocrmodelmanager.h"

#ifdef PDF4QT_OCR_TESSERACT
#include "pdftesseractocrengine.h"
#endif
#include "ui_pdfaboutdialog.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFAboutDialog::PDFAboutDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFAboutDialog)
{
    ui->setupUi(this);

    QString html = ui->copyrightLabel->text();
    html.replace("PdfForQtViewer", QString("%1 %2").arg(QApplication::applicationDisplayName(), QApplication::applicationVersion()));
    ui->copyrightLabel->setText(html);

    std::vector<pdf::PDFDependentLibraryInfo> infos = pdf::PDFDependentLibraryInfo::getLibraryInfo();

#ifdef PDF4QT_OCR_TESSERACT
    {
        // Versions and licenses of the distributed OCR components and of the built-in models (OPS-02)
        pdf::PDFTesseractOCREngineFactory factory;
        pdf::PDFDependentLibraryInfo tesseractInfo;
        tesseractInfo.library = QStringLiteral("Tesseract OCR");
        tesseractInfo.version = factory.getVersion();
        tesseractInfo.license = QStringLiteral("Apache-2.0");
        tesseractInfo.url = QStringLiteral("https://github.com/tesseract-ocr/tesseract");
        infos.push_back(tesseractInfo);

        pdf::PDFDependentLibraryInfo leptonicaInfo;
        leptonicaInfo.library = QStringLiteral("Leptonica");
        leptonicaInfo.version = pdf::PDFTesseractOCREngineFactory::getLeptonicaVersion();
        leptonicaInfo.license = QStringLiteral("BSD-2-Clause");
        leptonicaInfo.url = QStringLiteral("http://www.leptonica.org/");
        infos.push_back(leptonicaInfo);

        pdf::PDFOCRModelManager modelManager(nullptr);
        pdf::PDFDependentLibraryInfo modelsInfo;
        modelsInfo.library = QStringLiteral("Tesseract language models (built-in)");
        modelsInfo.version = modelManager.getBuiltInSetId(QStringLiteral("tesseract"));
        modelsInfo.license = QStringLiteral("Apache-2.0");
        modelsInfo.url = QStringLiteral("https://github.com/tesseract-ocr/tessdata_fast");
        infos.push_back(modelsInfo);
    }
#endif

    ui->tableWidget->setColumnCount(4);
    ui->tableWidget->setRowCount(static_cast<int>(infos.size()));
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << tr("Library") << tr("Version") << tr("License") << tr("URL"));
    ui->tableWidget->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableWidget->setSelectionMode(QTableView::SingleSelection);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < int(infos.size()); ++i)
    {
        const pdf::PDFDependentLibraryInfo& info = infos[i];
        ui->tableWidget->setItem(i, 0, new QTableWidgetItem(info.library));
        ui->tableWidget->setItem(i, 1, new QTableWidgetItem(info.version));
        ui->tableWidget->setItem(i, 2, new QTableWidgetItem(info.license));
        ui->tableWidget->setItem(i, 3, new QTableWidgetItem(info.url));
    }

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(750, 600));
    pdf::PDFWidgetUtils::style(this);
}

PDFAboutDialog::~PDFAboutDialog()
{
    delete ui;
}

}   // namespace viewer
