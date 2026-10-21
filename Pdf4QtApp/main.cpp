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

#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappinstance.h"
#include "pdfapplog.h"
#include "pdfapppaths.h"
#include "pdfappwindowfactory.h"
#include "pdfupdatechecker.h"

#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QDir>
#include <QTimer>
#include <QWidget>

#include <memory>

int main(int argc, char* argv[])
{
    // The window activation token of the launcher is read before QApplication exists:
    // the platform plugin consumes it (X11) - it is forwarded with the files
    const QString activationToken = pdf::PDFAppActivation::readStartupToken();

    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("MelkaJ"));
    QCoreApplication::setApplicationName(QStringLiteral("PDF4QT"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PDF4QT_PROJECT_VERSION));

    // Invalid arguments are shown in the window, never a crash
    const pdf::PDFAppCommandLine commandLine = pdf::PDFAppCommandLine::parse(QCoreApplication::arguments(), QDir::currentPath());

    pdf::PDFAppLogOptions logOptions;
    logOptions.directory = pdf::PDFAppPaths::getLogDirectory();
    logOptions.portable = commandLine.isPortable();
    pdf::PDFAppLog::install(logOptions);

    for (const QString& message : commandLine.getErrorMessages())
    {
        qWarning("%s", qPrintable(message));
    }

    // Decision 11: files of a second start are opened by the instance, which holds the
    // name of the forwarding; a start without files and --new-window forward nothing
    // and take the name only, when it is free; the portable mode does not take part
    std::unique_ptr<pdf::PDFAppInstanceServer> instanceServer;
    if (commandLine.isListeningAllowed())
    {
        instanceServer = std::make_unique<pdf::PDFAppInstanceServer>();

        // Relative paths are already absolute (the working directory of this process)
        const pdf::PDFAppInstanceRequest request = pdf::PDFAppInstanceProtocol::createRequest(commandLine, activationToken);
        const pdf::PDFAppInstanceGuard::Mode mode = commandLine.isForwardingAllowed() ? pdf::PDFAppInstanceGuard::Mode::Forward
                                                                                       : pdf::PDFAppInstanceGuard::Mode::ListenIfFree;

        QString error;
        const pdf::PDFAppInstanceGuard::Result result = pdf::PDFAppInstanceGuard::start(pdf::PDFAppInstanceGuard::getServerName(), request, instanceServer.get(),
                                                                                       pdf::PDFAppInstanceClient::DEFAULT_TIMEOUT, &error, mode);
        switch (result)
        {
            case pdf::PDFAppInstanceGuard::Result::Forwarded:
                qInfo("Files were passed to the running instance of PDF4QT.");
                pdf::PDFAppLog::uninstall();
                return 0;

            case pdf::PDFAppInstanceGuard::Result::Listening:
                break;

            case pdf::PDFAppInstanceGuard::Result::Standalone:
                qInfo("PDF4QT starts without receiving forwarded files: %s", qPrintable(error));
                instanceServer.reset();
                break;
        }
    }

    QApplication::setStyle(new pdf::PDFWidgetStyle());

    int exitCode = 0;
    {
        pdf::PDFAppContext context(commandLine);
        context.setInstanceServer(instanceServer.release());
        context.open();

        std::unique_ptr<QWidget> window(pdf::PDFAppWindowFactory::createMainWindow(&context));
        window->show();

        // The automatic check for updates (at most once a day, newapp_whatsnew.md 5)
        pdf::PDFUpdateChecker::getInstance(&context)->startAutomaticChecks();

        // Smoke tests of the executable quit it after the given time
        bool isQuitTimeValid = false;
        const int quitAfter = qEnvironmentVariableIntValue("PDF4QT_APP_QUIT_AFTER_MS", &isQuitTimeValid);
        if (isQuitTimeValid && quitAfter > 0)
        {
            QTimer::singleShot(quitAfter, &application, &QCoreApplication::quit);
        }

        exitCode = application.exec();
        window.reset();
    }

    pdf::PDFAppLog::uninstall();
    return exitCode;
}
