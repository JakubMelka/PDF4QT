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

#include "pdfconstants.h"
#include "pdfsecurityhandler.h"
#include "pdfwidgetutils.h"
#include "pdfapplicationtranslator.h"
#include "pdfsettings.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMenu>
#include <QProxyStyle>

namespace
{

class PageMasterStyle : public QProxyStyle
{
public:
    explicit PageMasterStyle(const QString& key) :
        QProxyStyle(key)
    {

    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override
    {
        if (metric == QStyle::PM_SmallIconSize && qobject_cast<const QMenu*>(widget))
        {
            return pdf::PDFWidgetUtils::scaleDPI_x(widget, 24);
        }

        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

void installPageMasterStyle(QApplication& application)
{
    QStyle* currentStyle = application.style();
    application.setStyle(new PageMasterStyle(currentStyle ? currentStyle->objectName() : QString()));
}

}   // namespace

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
    QCommandLineOption configPath = pdf::PDFSettings::getConfigPathOption();

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addOption(noDrm);
    parser.addOption(lightGui);
    parser.addOption(darkGui);
    parser.addOption(configPath);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "The PDF file to open.");
    parser.process(application);
    pdf::PDFSettings::applyCommandLineSettingsPath(parser);

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFWidgetUtils::setDarkTheme(parser.isSet(lightGui), parser.isSet(darkGui));
    installPageMasterStyle(application);

    pdf::PDFApplicationTranslator translator;
    translator.installTranslator();

    QIcon appIcon(":/app-icon.svg");
    QApplication::setWindowIcon(appIcon);

    pdfpagemaster::MainWindow mainWindow(nullptr);
    mainWindow.show();

    return application.exec();
}
