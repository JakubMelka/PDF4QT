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
