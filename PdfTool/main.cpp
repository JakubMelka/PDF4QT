//    Copyright (C) 2020-2021 Jakub Melka
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

#include "pdftoolabstractapplication.h"
#include "pdfconstants.h"

#include <QGuiApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QGuiApplication a(argc, argv);
    QGuiApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity, true);
    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PdfTool");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);

    QStringList arguments = QCoreApplication::arguments();

    QCommandLineParser parser;
    parser.setApplicationDescription("PdfTool - work with pdf documents via command line");
    parser.addPositionalArgument("command", "Command to execute.");
    parser.parse(arguments);

    QStringList positionalArguments = parser.positionalArguments();
    QString command = !positionalArguments.isEmpty() ? positionalArguments.front() : QString();
    arguments.removeOne(command);

    pdftool::PDFToolAbstractApplication* application = pdftool::PDFToolApplicationStorage::getApplicationByCommand(command);
    if (!application)
    {
        application = pdftool::PDFToolApplicationStorage::getDefaultApplication();
    }
    else
    {
        parser.clearPositionalArguments();
    }

    application->initializeCommandLineParser(&parser);

    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(arguments);

    return application->execute(application->getOptions(&parser));
}
