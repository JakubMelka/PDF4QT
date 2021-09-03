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

#include "pdfconstants.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT Document Diff");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT Document Diff"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file1", "The PDF file to be compared.");
    parser.addPositionalArgument("file2", "The PDF file to be compared.");
    parser.process(application);

    pdfdocdiff::MainWindow mainWindow(nullptr);
    mainWindow.show();

    return application.exec();
}
