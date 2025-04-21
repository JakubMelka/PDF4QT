//    Copyright (C) 2021-2024 Jakub Melka
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

#include "pdfconstants.h"
#include "pdfdocumentreader.h"
#include "pdfsecurityhandler.h"
#include "pdfapplicationtranslator.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT Diff");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Diff"));

    QCommandLineOption noDrm("no-drm", "Disable DRM settings of documents.");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addOption(noDrm);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file1", "The PDF file to be compared.");
    parser.addPositionalArgument("file2", "The PDF file to be compared.");
    parser.process(application);

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFApplicationTranslator translator;
    translator.installTranslator();

    QIcon appIcon(":/app-icon.svg");
    QApplication::setWindowIcon(appIcon);

    pdfdiff::MainWindow mainWindow(nullptr);
    mainWindow.show();

    QStringList positionalArguments = parser.positionalArguments();

    if (positionalArguments.size() >= 1)
    {
        bool leftDocumentIsOK = false;
        bool rightDocumentIsOk = false;

        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument documentLeft = reader.readFromFile(positionalArguments.front());

        if (reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK)
        {
            leftDocumentIsOK = true;
            mainWindow.setLeftDocument(std::move(documentLeft));
        }

        if (positionalArguments.size() >= 2)
        {
            pdf::PDFDocument documentRight = reader.readFromFile(positionalArguments[1]);

            if (reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK)
            {
                rightDocumentIsOk = true;
                mainWindow.setRightDocument(std::move(documentRight));
            }
        }

        mainWindow.updateViewDocument();

        if (leftDocumentIsOK && rightDocumentIsOk && mainWindow.canPerformOperation(pdfdiff::MainWindow::Operation::Compare))
        {
            mainWindow.performOperation(pdfdiff::MainWindow::Operation::Compare);
        }
    }

    return application.exec();
}
