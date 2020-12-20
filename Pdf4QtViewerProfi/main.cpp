#include "pdfviewermainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity, true);
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName("MelkaJ");
    QCoreApplication::setApplicationName("PDF4QT Viewer Profi");
    QCoreApplication::setApplicationVersion("1.0.0");
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
