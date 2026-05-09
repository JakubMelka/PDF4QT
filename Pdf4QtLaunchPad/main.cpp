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

#include "launchdialog.h"
#include "launchapplication.h"
#include "pdfapplicationtranslator.h"
#include "pdfsettings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QStringList>

#include <cstdlib>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT LaunchPad");
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT LaunchPad"));

    QCommandLineParser parser;
    QCommandLineOption configPath = pdf::PDFSettings::getConfigPathOption();
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addOption(configPath);
    parser.addHelpOption();
    parser.addPositionalArgument("file", "The PDF file to open in PDF4QT Viewer.");
    parser.process(application);
    pdf::PDFSettings::applyCommandLineSettingsPath(parser);

    const QStringList positionalArguments = parser.positionalArguments();
    if (!positionalArguments.isEmpty())
    {
        QFileInfo fileInfo(positionalArguments.front());
        if (fileInfo.exists() && fileInfo.isFile())
        {
            const bool launched = LaunchApplication::start("Pdf4QtViewer", QStringList{ fileInfo.absoluteFilePath() }, nullptr);
            return launched ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    pdf::PDFApplicationTranslator translator;
    translator.installTranslator();

    QIcon appIcon(":/app-icon.svg");
    QApplication::setWindowIcon(appIcon);

    LaunchDialog mainWindow(nullptr);
    mainWindow.show();

    return application.exec();
}
