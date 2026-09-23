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
#include "pdfocrconfiguration.h"

#ifdef PDF4QT_OCR_TESSERACT
#include "pdftesseractocrengine.h"
#endif
#include "ui_pdfaboutdialog.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

#include <QDir>
#include <QFile>
#include <QUrl>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDesktopServices>

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

    // License texts distributed with the application, one entry per row of the table
    // (empty, when the text is not distributed)
    std::vector<QString> licenseFiles(infos.size());

#ifdef PDF4QT_OCR_TESSERACT
    {
        // Versions and licenses of the distributed OCR components and of the built-in models (OPS-02).
        // The license texts are installed with the OCR data: the texts of the engine libraries
        // in the directory 'licenses', the text of a built-in model set next to its manifest,
        // as the manifest declares it (the license of the models is not the license of the engine).
        pdf::PDFOCRModelManager modelManager(nullptr);
        const QString ocrDirectory = modelManager.getBuiltInDirectory();
        const QString licenseDirectory = ocrDirectory + QStringLiteral("/licenses");

        pdf::PDFTesseractOCREngineFactory factory;
        pdf::PDFDependentLibraryInfo tesseractInfo;
        tesseractInfo.library = QStringLiteral("Tesseract OCR");
        tesseractInfo.version = factory.getVersion();
        tesseractInfo.license = QStringLiteral("Apache-2.0");
        tesseractInfo.url = QStringLiteral("https://github.com/tesseract-ocr/tesseract");
        infos.push_back(tesseractInfo);
        licenseFiles.push_back(licenseDirectory + QStringLiteral("/Tesseract_LICENSE.txt"));

        pdf::PDFDependentLibraryInfo leptonicaInfo;
        leptonicaInfo.library = QStringLiteral("Leptonica");
        leptonicaInfo.version = pdf::PDFTesseractOCREngineFactory::getLeptonicaVersion();
        leptonicaInfo.license = QStringLiteral("BSD-2-Clause");
        leptonicaInfo.url = QStringLiteral("http://www.leptonica.org/");
        infos.push_back(leptonicaInfo);
        licenseFiles.push_back(licenseDirectory + QStringLiteral("/Leptonica_license.txt"));

        // Built-in model sets, one row per distributed profile
        for (pdf::PDFOCRModelProfile profile : pdf::PDFOCRConfiguration::getProfiles())
        {
            const QString profileDirectory = ocrDirectory + QStringLiteral("/tesseract/") + pdf::PDFOCRConfiguration::getProfileIdentifier(profile);
            QFile manifestFile(profileDirectory + QStringLiteral("/manifest.json"));
            if (!manifestFile.open(QFile::ReadOnly))
            {
                continue;
            }

            const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            const QJsonArray models = manifest.value(QStringLiteral("models")).toArray();
            QString source;
            for (const QJsonValue& model : models)
            {
                source = model.toObject().value(QStringLiteral("source")).toString();
                if (!source.isEmpty())
                {
                    break;
                }
            }

            pdf::PDFDependentLibraryInfo modelsInfo;
            modelsInfo.library = tr("Tesseract language models (built-in, %1, %2 models)").arg(pdf::PDFOCRConfiguration::getProfileName(profile)).arg(models.size());
            modelsInfo.version = manifest.value(QStringLiteral("setId")).toString();
            modelsInfo.license = manifest.value(QStringLiteral("license")).toString(QStringLiteral("Apache-2.0"));
            modelsInfo.url = source.isEmpty() ? QStringLiteral("https://github.com/tesseract-ocr") : source;
            infos.push_back(modelsInfo);

            const QString licenseFile = manifest.value(QStringLiteral("licenseFile")).toString();
            licenseFiles.push_back(licenseFile.isEmpty() ? licenseDirectory + QStringLiteral("/tessdata_LICENSE.txt") : profileDirectory + QStringLiteral("/") + licenseFile);
        }
    }
#endif

    Q_ASSERT(infos.size() == licenseFiles.size());

    ui->tableWidget->setColumnCount(5);
    ui->tableWidget->setRowCount(static_cast<int>(infos.size()));
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << tr("Library") << tr("Version") << tr("License") << tr("URL") << tr("License text"));
    ui->tableWidget->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableWidget->setSelectionMode(QTableView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QTableView::SelectRows);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < int(infos.size()); ++i)
    {
        const pdf::PDFDependentLibraryInfo& info = infos[i];
        ui->tableWidget->setItem(i, 0, new QTableWidgetItem(info.library));
        ui->tableWidget->setItem(i, 1, new QTableWidgetItem(info.version));
        ui->tableWidget->setItem(i, 2, new QTableWidgetItem(info.license));
        ui->tableWidget->setItem(i, 3, new QTableWidgetItem(info.url));

        // License text distributed with the application; a missing text is reported,
        // so an incomplete distribution is visible (OPS-02)
        QTableWidgetItem* licenseItem = new QTableWidgetItem();
        const QString& licenseFile = licenseFiles[i];
        if (!licenseFile.isEmpty())
        {
            const QString nativeLicenseFile = QDir::toNativeSeparators(licenseFile);
            if (QFile::exists(licenseFile))
            {
                licenseItem->setText(QFileInfo(licenseFile).fileName());
                licenseItem->setToolTip(tr("%1\n\nDouble-click the row to open the license text.").arg(nativeLicenseFile));
                licenseItem->setData(Qt::UserRole, licenseFile);
            }
            else
            {
                licenseItem->setText(tr("%1 (missing)").arg(QFileInfo(licenseFile).fileName()));
                licenseItem->setToolTip(tr("The license text %1 is not distributed with this installation.").arg(nativeLicenseFile));
            }
        }
        ui->tableWidget->setItem(i, 4, licenseItem);
    }

    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this, &PDFAboutDialog::onLibraryDoubleClicked);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(750, 600));
    pdf::PDFWidgetUtils::style(this);
}

PDFAboutDialog::~PDFAboutDialog()
{
    delete ui;
}

void PDFAboutDialog::onLibraryDoubleClicked(int row, int column)
{
    Q_UNUSED(column);

    // The distributed license text is opened, when it exists, otherwise the web page of the library
    if (const QTableWidgetItem* licenseItem = ui->tableWidget->item(row, 4))
    {
        const QString licenseFile = licenseItem->data(Qt::UserRole).toString();
        if (!licenseFile.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(licenseFile));
            return;
        }
    }

    if (const QTableWidgetItem* urlItem = ui->tableWidget->item(row, 3))
    {
        const QUrl url(urlItem->text());
        if (url.isValid() && !url.scheme().isEmpty())
        {
            QDesktopServices::openUrl(url);
        }
    }
}

}   // namespace viewer
