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

#include "pdfviewermainwindow.h"
#include "pdfconstants.h"
#include "pdfdbgheap.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
#if defined(PDF4QT_USE_DBG_HEAP)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity, true);

#if !defined(Q_OS_WIN)
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
#endif

    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT Viewer Profi");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Viewer Profi"));
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
