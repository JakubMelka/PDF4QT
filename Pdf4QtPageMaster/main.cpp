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
#include "pdfsecurityhandler.h"
#include "pdfwidgetutils.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT PageMaster");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF4QT PageMaster"));

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
    parser.addPositionalArgument("file", "The PDF file to open.");
    parser.process(application);

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFWidgetUtils::setDarkTheme(parser.isSet(lightGui), parser.isSet(darkGui));

    QIcon appIcon(":/app-icon.svg");
    QApplication::setWindowIcon(appIcon);

    pdfpagemaster::MainWindow mainWindow(nullptr);
    mainWindow.show();

    return application.exec();
}
