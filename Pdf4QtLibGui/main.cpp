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

#include "pdfviewermainwindow.h"

#include <QResource>
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT Editor");
    QCoreApplication::setApplicationVersion("1.0.0");
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Editor"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "The PDF file to open.");
    parser.process(application);

    pdfviewer::PDFViewerMainWindow mainWindow;
    mainWindow.show();

    QStringList arguments = application.arguments();
    if (arguments.size() > 1)
    {
        mainWindow.getProgramController()->openDocument(arguments[1]);
    }

    return application.exec();
}
