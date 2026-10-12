#include "gallerywindow.h"

#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("MelkaJ"));
    QCoreApplication::setApplicationName(QStringLiteral("PDF4QT Widget Gallery"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineOption themeOption(QStringLiteral("theme"), QStringLiteral("Identifier of the theme (light, dark)."), QStringLiteral("id"), QStringLiteral("light"));
    QCommandLineOption exportOption(QStringLiteral("export"), QStringLiteral("Exports the samples of all pages into the directory and quits."), QStringLiteral("directory"));
    QCommandLineOption scaleOption(QStringLiteral("scale"), QStringLiteral("Device pixel ratio of the exported images."), QStringLiteral("ratio"), QStringLiteral("1"));
    QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Page shown at start (for example 05 or 05-spin-box-date-edit)."), QStringLiteral("id"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Saves the window into the file after it is shown and quits."), QStringLiteral("file"));
    QCommandLineOption stockOption(QStringLiteral("stock-style"), QStringLiteral("Uses the style Fusion of Qt instead of the style of the library (for a comparison)."));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Gallery of the widgets drawn by PDFWidgetStyle."));
    parser.addOption(themeOption);
    parser.addOption(exportOption);
    parser.addOption(scaleOption);
    parser.addOption(pageOption);
    parser.addOption(screenshotOption);
    parser.addOption(stockOption);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(application);

    if (!pdf::PDFThemeManager::getInstance()->setTheme(parser.value(themeOption)))
    {
        QTextStream(stderr) << "Unknown theme: " << parser.value(themeOption) << Qt::endl;
        return 2;
    }

    if (parser.isSet(stockOption))
    {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }
    else
    {
        QApplication::setStyle(new pdf::PDFWidgetStyle());
    }

    pdffluentgallery::GalleryWindow window;

    if (parser.isSet(pageOption))
    {
        window.setCurrentPage(parser.value(pageOption));
    }

    window.show();

    if (parser.isSet(exportOption))
    {
        bool isScaleValid = false;
        const qreal scale = parser.value(scaleOption).toDouble(&isScaleValid);
        if (!isScaleValid || scale <= 0.0 || scale > 8.0)
        {
            QTextStream(stderr) << "Invalid scale: " << parser.value(scaleOption) << Qt::endl;
            return 2;
        }

        QApplication::processEvents();
        const int count = window.exportPages(parser.value(exportOption), scale);
        QTextStream(stdout) << "Exported " << count << " images" << Qt::endl;
        return 0;
    }

    if (parser.isSet(screenshotOption))
    {
        const QString fileName = parser.value(screenshotOption);
        QTimer::singleShot(1500, &window, [&window, fileName]()
        {
            window.grab().save(fileName);
            QApplication::quit();
        });
    }

    return application.exec();
}
