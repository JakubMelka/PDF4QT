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

#include "pdfconstants.h"
#include "pdfdocumentreader.h"
#include "pdfsecurityhandler.h"
#include "pdfapplicationtranslator.h"
#include "pdfwidgetutils.h"
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
    QCommandLineOption lightGui("theme-light", "Use a light theme for the GUI.");
    QCommandLineOption darkGui("theme-dark", "Use a dark theme for the GUI.");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addOption(noDrm);
    parser.addOption(lightGui);
    parser.addOption(darkGui);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file1", "The PDF file to be compared.");
    parser.addPositionalArgument("file2", "The PDF file to be compared.");
    parser.process(application);

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFWidgetUtils::setDarkTheme(parser.isSet(lightGui), parser.isSet(darkGui));

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
