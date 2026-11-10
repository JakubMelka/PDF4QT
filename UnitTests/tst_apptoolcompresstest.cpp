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

// The tool Compress in the application (wave D of the plan of the first batch, frames
// 291:994, 294:1340, 295:1711, 295:2182, 296:2761, 316:7293) and the start page of a tool
// (276:1737). The expected values are the numbers of the frames.

#include "apptoolshellfixture.h"

#include "pdfappcontext.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapphelpcatalog.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkdownview.h"
#include "pdfapppagenotfound.h"
#include "pdfapppagerendercontext.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolbeforeafteroverlay.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolgui_compress.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpageregistry.h"
#include "pdfappworkspace.h"
#include "pdftoolrepository.h"
#include "pdftoolstartpage.h"

#include "pdfaccenthue.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappvaluestore.h"
#include "pdfdocumenttool_compress.h"
#include "pdfexecutionpolicy.h"
#include "pdfimagecompressor.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcms.h"
#include "pdfdrawwidget.h"
#include "pdfcomponentpanels.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include "testprogressrecorder.h"

#include <QAccessible>
#include <QCheckBox>
#include <QDrag>
#include <QFile>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSlider>
#include <QThreadPool>
#include <QTranslator>
#include <QtTest>

#include <map>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

using pdf::PDFAppToolBeforeAfterOverlay;
using pdf::PDFAppToolGUICompress;
using pdf::PDFDocumentToolShell;
using pdf::PDFToolStartPage;
using pdftest::AppDocumentFixture;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;

namespace pdftest
{

/// A translator of every text: the panels are built again with its texts
class CompressTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("Compress") || name.contains("ToolStartPage") || name.contains("DocumentChooser") || name.contains("DocumentRow"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

}   // namespace pdftest

/// Helpers of the test
class CompressGuiHelper
{
public:
    CompressGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("optimize.compress");

    /// A PDF with a noisy image of a size far over the resolution of the presets
    static QString writePhoto(const QString& filePath, int imageSize = 900, int pageCount = 1)
    {
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        QRandomGenerator generator(11);
        for (int i = 0; i < pageCount; ++i)
        {
            QImage image(imageSize, imageSize, QImage::Format_RGB32);
            for (int y = 0; y < imageSize; ++y)
            {
                QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < imageSize; ++x)
                {
                    line[x] = qRgb(120 + generator.bounded(40), 80 + generator.bounded(40), 40 + generator.bounded(40));
                }
            }
            painter.drawImage(QRectF(50, 50, 200, 200), image);
            if (i + 1 < pageCount)
            {
                writer.newPage();
            }
        }
        return filePath;
    }

    /// A PDF of one page filled by the color
    static QString writeSolid(const QString& filePath, const QColor& color)
    {
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.fillRect(QRectF(0, 0, 600, 900), color);
        return filePath;
    }

    /// A file, which Compress has compressed already: Best quality saves nothing more
    static QString writeCompressed(const QString& folder, const QString& filePath, const QString& preset = QStringLiteral("balanced"))
    {
        const QString source = writePhoto(folder + QStringLiteral("/source-of-compressed.pdf"));
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly))
        {
            return QString();
        }
        const QByteArray data = file.readAll();
        pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);

        pdf::PDFDocumentToolCompress tool;
        QVariantMap parameters;
        parameters.insert(QStringLiteral("preset"), preset);
        pdf::PDFDocumentToolResult result = tool.run(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data, source, false)), parameters, nullptr, nullptr);
        if (result.documents.empty())
        {
            return QString();
        }

        pdf::PDFDocument output(*result.documents.front().document);
        return AppDocumentSamples::write(output, filePath) ? filePath : QString();
    }

    /// A PDF of different small images, 100 of them on a page
    static QString writeManyImages(const QString& filePath, int imageCount)
    {
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        QRandomGenerator generator(13);
        for (int i = 0; i < imageCount; ++i)
        {
            QImage image(64, 64, QImage::Format_RGB32);
            for (int y = 0; y < 64; ++y)
            {
                QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < 64; ++x)
                {
                    line[x] = qRgb(generator.bounded(256), generator.bounded(256), generator.bounded(256));
                }
            }
            painter.drawImage(QRectF(20 + 50 * (i % 10), 20 + 50 * ((i / 10) % 10), 8, 8), image);
            if (i % 100 == 99 && i + 1 < imageCount)
            {
                writer.newPage();
            }
        }
        return filePath;
    }

    /// A PDF of pages filled by the color
    static QString writeSolidPages(const QString& filePath, const QColor& color, int pageCount)
    {
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        for (int i = 0; i < pageCount; ++i)
        {
            painter.fillRect(QRectF(0, 0, 600, 900), color);
            if (i + 1 < pageCount)
            {
                writer.newPage();
            }
        }
        return filePath;
    }

    /// The noisy photo encrypted by AES-256 with the owner password of the samples, the user
    /// password and permissions of the arguments (by default every permission allowed)
    static QString writeEncryptedPhoto(const QString& folder, const QString& filePath, const QString& userPassword = AppDocumentSamples::getUserPassword(), uint32_t permissions = 0xFFFFFFFCu)
    {
        const pdf::PDFDocumentPointer plain = readDocument(writePhoto(folder + QStringLiteral("/plain-of-encrypted.pdf")));
        pdf::PDFDocumentBuilder builder(plain.data());
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = AppDocumentSamples::getOwnerPassword();
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// Reads the data with a password given once
    static pdf::PDFDocument readWithPassword(const QByteArray& data, const QString& password, pdf::PDFDocumentReader::Result* result)
    {
        bool isAsked = false;
        const auto getPassword = [&password, &isAsked](bool* ok)
        {
            *ok = !password.isEmpty() && !isAsked;
            isAsked = true;
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        *result = reader.getReadingResult();
        return document;
    }

    /// Opens the file and enters the user password of the samples on its password screen
    static pdf::PDFAppDocumentTab* openWithPassword(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; }))
        {
            return nullptr;
        }
        pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
        if (!screen || !screen->getPasswordField())
        {
            return nullptr;
        }
        QTest::keyClicks(screen->getPasswordField(), AppDocumentSamples::getUserPassword());
        QTest::keyClick(screen->getPasswordField(), Qt::Key_Return);
        if (!AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    /// Time of a context of the overlay with its color management and of a drawing of the first page
    static qint64 measureContext(const pdf::PDFDocument* document, const pdf::PDFCMSSettings& settings, pdf::PDFRenderer::Features features)
    {
        QElapsedTimer timer;
        timer.start();
        pdf::PDFAppPageRenderContext context(document, settings, features);
        qInfo("BENCHMARK context alone %lld ms", timer.elapsed());
        QImage image(800, 1000, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        const pdf::PDFPage* page = document->getCatalog()->getPage(0);
        QTransform matrix;
        matrix.scale(800.0 / page->getCropBox().width(), 1000.0 / page->getCropBox().height());
        QPainter painter(&image);
        context.draw(&painter, 0, matrix, false, nullptr);
        return timer.elapsed();
    }

    /// The commit charge of the process in MB (0 where it is not known)
    static qint64 getCurrentMemoryMB()
    {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS counters = {};
        counters.cb = sizeof(counters);
        return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) ? qint64(counters.PagefileUsage / (1024 * 1024)) : 0;
#else
        return 0;
#endif
    }

    /// Peak of the commit charge of the process in MB (0 where it is not known)
    static qint64 getPeakMemoryMB()
    {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS counters = {};
        counters.cb = sizeof(counters);
        return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) ? qint64(counters.PeakPagefileUsage / (1024 * 1024)) : 0;
#else
        return 0;
#endif
    }

    static pdf::PDFDocumentPointer readDocument(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(reader.readFromFile(filePath)));
    }

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static PDFDocumentToolShell* openCompress(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab* tab)
    {
        return fixture.openTool(tab, TOOL_ID);
    }

    static PDFAppToolGUICompress* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUICompress*>(shell->getGUI()) : nullptr;
    }

    static bool waitMeasured(PDFAppToolGUICompress* gui)
    {
        return AppToolShellFixture::waitFor([gui]()
        {
            return gui->getMeasureState() != PDFAppToolGUICompress::MeasureState::Measuring;
        }, 60000);
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    /// The last row of the ink of a label of a text without a descender, in the coordinates of the window
    static int inkBottom(QWidget* label)
    {
        const QImage image = label->window()->grab().toImage().convertToFormat(QImage::Format_RGB32);
        const QRect rect = inWindow(label);
        const QRgb background = image.pixel(rect.right(), rect.top());
        for (int y = rect.bottom(); y >= rect.top(); --y)
        {
            for (int x = rect.left(); x <= rect.right(); ++x)
            {
                if (qAbs(qGray(image.pixel(x, y)) - qGray(background)) > 48)
                {
                    return y;
                }
            }
        }
        return -1;
    }

    /// The last row of the ink of a label in an image of \p reference at a device pixel ratio
    static int inkBottomInImage(const QImage& image, const QWidget* reference, const QWidget* label, qreal ratio)
    {
        const QRect logical(label->mapTo(reference, QPoint(0, 0)), label->size());
        const QRect rect(qFloor(logical.left() * ratio), qFloor(logical.top() * ratio), qCeil(logical.width() * ratio), qCeil(logical.height() * ratio));
        const QRgb background = image.pixel(rect.right() - 1, rect.top());
        for (int y = qMin(rect.bottom(), image.height() - 1); y >= rect.top(); --y)
        {
            for (int x = rect.left(); x <= rect.right() && x < image.width(); ++x)
            {
                if (qAbs(qGray(image.pixel(x, y)) - qGray(background)) > 48)
                {
                    return y;
                }
            }
        }
        return -1;
    }

    static QString preset(PDFDocumentToolShell* shell)
    {
        return shell->getModel()->getValue(QStringLiteral("preset")).toString();
    }

    static QStringList rowKeys(const pdf::PDFKeyValueList* list)
    {
        QStringList keys;
        for (int i = 0; i < list->getRowCount(); ++i)
        {
            keys << list->getKey(i);
        }
        return keys;
    }

    /// Paints the canvas at its size
    static QImage grabCanvas(PDFAppToolBeforeAfterOverlay* overlay, pdf::PDFAppDocumentTab* tab)
    {
        Q_UNUSED(overlay);
        return AppDocumentFixture::canvasWidget(tab->getView())->grab().toImage().convertToFormat(QImage::Format_RGB32);
    }
};

class AppToolCompressTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Compress
    void registered();
    void oneThreadReachesTheNextRun_data();
    void oneThreadReachesTheNextRun();
    void frameGeometry();
    void measuredPanel();
    void changeRowsNotElided();
    void measuringState();
    void presetChangeMeasuresAgain();
    void undoOfThePreset();
    void confirmDuringMeasuring();
    void stopGoesBackWithoutResult();
    void reviewStoppedHasExits();
    void reviewPresetChangedBeforeConfirm();
    void reviewVersionChangeWhileMeasuring();
    void confirmMeasuredOpensDialog();
    void writeCopySmaller();
    void writeErrorFrame();
    void notReduced();
    void notReducedSmallerFile();
    void customPanelGeometry();
    void customPanelValues();
    void customPanelClickTogglesAndNarrow();
    void lastValuesOnlyAccepted();
    void keys();
    void signedDocument();
    void encryptedCopy();
    void encryptedRefusedWithoutPermission();
    void lockedRefused();
    void reviewSegmentsDisabledWithLabel();
    void reviewNotReducedCalloutLines();
    void reviewPermissionRefusedIsNotMeasuring();
    void reviewMeasuredCardDirection();
    void reviewCustomPanelRightToLeft();
    void reviewCustomPanelNoRingAfterClick();
    void reviewHandleInTabOrder();
    void reviewOverlayRotatedAndTwoPages();
    void reviewOverlayDevicePixelRatio();
    void reviewSameResultKeepsPixmaps();
    void reviewMeasuringBenchmark();
    void leakAndThemes_data();
    void leakAndThemes();
    void minimalWindow();
    void languageChange();
    void panelsTranslatedSchemaEnglish();
    void accessibility();
    void deletedDuringMeasuring();
    void measuringKeepsTheEventLoop();

    // The overlay
    void overlayLeftOriginalRightResult();
    void overlayFollowsZoomAndScroll();
    void overlayDropsOlderResult();
    void overlayHandleKeysAndMouse();
    void overlayChipsAtTheEdges();
    void overlayDeletedWhileRendering();

    // The start page
    void startPageGeometry();
    void startPageTexts();
    void chooserRows();
    void chooserChoosesDocument();
    void chooserKeyboard();
    void openFileGoesToTool();
    void reviewOpenFileOfAnotherWindow();
    void reviewOpenFileLockedDocument();
    void reviewOpenFileWithPassword();
    void reviewChooserLongPath();
    void dropGoesToTool();
    void noGuiStaysNotFound();
    void startPageLanguageChange();
    void startPageDeletedWithDialog();
    void screenshots();
};

void AppToolCompressTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolCompressTest::registered()
{
    // The context registers the tool with its GUI and both of its pages, once
    AppToolShellFixture fixture;
    const QStringList implemented = pdf::PDFDocumentToolRegistry::getImplementedTools();
    QVERIFY(implemented.contains(CompressGuiHelper::TOOL_ID));
    QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, CompressGuiHelper::TOOL_ID));
    QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Start, CompressGuiHelper::TOOL_ID));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(CompressGuiHelper::TOOL_ID);
    pdf::PDFAppToolGUICompress::registerTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(CompressGuiHelper::TOOL_ID), tool);
}

void AppToolCompressTest::oneThreadReachesTheNextRun_data()
{
    QTest::addColumn<QString>("way");
    QTest::newRow("this-instance") << QStringLiteral("own");
    QTest::newRow("other-instance-tab-hidden") << QStringLiteral("hidden");
    QTest::newRow("other-window-closed") << QStringLiteral("window");
}

void AppToolCompressTest::oneThreadReachesTheNextRun()
{
    // The setting Multithreading strategy = Single thread, written by the path of its row of Settings ›
    // Advanced or by another instance, reaches the process through the views of the documents: the next
    // computation of the tool, in its tool mode, with its tab hidden, in a second window, whose first one
    // has gone, really has one image at a time in one thread - counted at the decoding, not read from
    // the policy.
    using H = CompressGuiHelper;
    if (pdf::PDFExecutionPolicy::getMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page) < 3)
    {
        QSKIP("The pool has too few threads to tell the strategies");
    }
    QFETCH(QString, way);

    std::atomic<int> held = 0;
    std::atomic<int> peak = 0;
    pdf::PDFImageCompressor::setTestDecodeHook([&](pdf::PDFObjectReference, QImage image) -> QImage
    {
        const int current = ++held;
        int previous = peak.load();
        while (previous < current && !peak.compare_exchange_weak(previous, current))
        {
        }
        QThread::msleep(40);
        --held;
        return image;
    });
    const auto removeHook = qScopeGuard([]() { pdf::PDFImageCompressor::setTestDecodeHook(pdf::PDFImageCompressor::TestDecodeHook()); });
    const auto restoreStrategy = qScopeGuard([]() { pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded); });

    AppToolShellFixture fixture(QSize(1440, 960));
    const QString firstFile = H::writePhoto(fixture.path(QStringLiteral("first.pdf")), 120, 10);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, firstFile);
    QVERIFY(tab);

    // The tool of the row: its tab, or the tab of a second window
    std::unique_ptr<pdf::PDFAppMainWindow> secondWindow;
    pdf::PDFAppDocumentTab* toolTab = tab;
    PDFDocumentToolShell* firstShell = H::openCompress(fixture, tab);
    QVERIFY(firstShell);

    // The control: the same computation in more threads holds more images at once
    QVERIFY(H::waitMeasured(H::gui(firstShell)));
    QVERIFY2(peak.load() >= 2, qPrintable(QString::number(peak.load())));
    AppToolShellFixture::settle();

    PDFDocumentToolShell* shell = firstShell;
    if (way == QLatin1String("window"))
    {
        secondWindow = std::make_unique<pdf::PDFAppMainWindow>(fixture.context.get());
        secondWindow->resize(1440, 960);
        secondWindow->show();
        const QString secondFile = H::writePhoto(fixture.path(QStringLiteral("second.pdf")), 120, 10);
        secondWindow->openDocuments(QStringList({ secondFile }), 0);
        QVERIFY(AppToolShellFixture::waitFor([&]() { const auto tabs = secondWindow->getDocumentTabs(); return !tabs.isEmpty() && tabs.front()->getDocument() && tabs.front()->getDocument()->isReady(); }));
        AppToolShellFixture::settle();
        toolTab = secondWindow->getDocumentTabs().front();
        secondWindow->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(toolTab->getDocumentId(), CompressGuiHelper::TOOL_ID));
        AppToolShellFixture::settle();
        shell = AppToolShellFixture::getShell(toolTab);
        QVERIFY(shell);
    }
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();

    if (way == QLatin1String("hidden"))
    {
        fixture.window->activateTab(fixture.window->getHomeTab());
        AppToolShellFixture::settle();
        QVERIFY(!tab->getView()->isVisible());
        QVERIFY(tab->getView()->isToolModeActive());
        const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
        QVERIFY(fixture.writeFromOtherConnection([registry](pdf::PDFDatabase* database) { return pdf::PDFSettingsRepository(database, registry).setValue(pdf::PDFSettingKeys::renderingMultithreading(), QStringLiteral("singleThread")); },
                                                 pdf::PDFSettingsRepository::getTopic()));
    }
    else
    {
        // The path of the row of Settings › Advanced (PDFAppSettingControl) in this instance
        pdf::PDFAppSettingsValueStore store(fixture.context.get());
        QVERIFY(store.isAvailable());
        QCOMPARE(store.writeValue(pdf::PDFSettingKeys::renderingMultithreading(), QStringLiteral("singleThread")), QString());
        AppToolShellFixture::settle();
    }

    if (way == QLatin1String("window"))
    {
        // The first window goes, with its document and its view
        delete fixture.window.data();
        AppToolShellFixture::settle();
    }

    // The next computation: another preset measures, in the mode of the tool
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getRunner()->isPreviewPending(); }, 60000));
    peak.store(0);
    QVERIFY(shell->getModel()->setValue(QStringLiteral("preset"), QString::fromLatin1(pdf::PDFDocumentToolCompress::PRESET_SMALLER_FILE)).isEmpty());
    QVERIFY(AppToolShellFixture::waitFor([&peak]() { return peak.load() > 0; }, 60000));
    QVERIFY(H::waitMeasured(gui));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getRunner()->isPreviewPending(); }, 60000));
    AppToolShellFixture::settle();
    QCOMPARE(peak.load(), 1);
    QVERIFY(!pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));
    secondWindow.reset();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::frameGeometry()
{
    // 291:994 at 1440 x 960
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(H::inWindow(toolbar), QRect(0, 38, 1440, 52));

    // tool-actions 291:1135: the label at 289, the segments 333 x 48, 364 x 32
    const pdf::PDFSegmentedControl* segments = gui->getPresetControl()->getSegmentedControl();
    // The label "Preset" has a width of a text: at most 1 px from the 38 of Figma
    QVERIFY2(qAbs(H::inWindow(segments).left() - 333) <= 1, qPrintable(QString::number(H::inWindow(segments).left())));
    QCOMPARE(H::inWindow(segments).top(), 48);
    QCOMPARE(segments->size(), QSize(364, 32));
    QCOMPARE(segments->getCount(), 4);
    const QList<QToolButton*> buttons = segments->findChildren<QToolButton*>();
    QCOMPARE(buttons.size(), 4);
    QCOMPARE(H::inWindow(buttons[1]).left() - H::inWindow(segments).left(), 91);
    QCOMPARE(buttons[1]->size(), QSize(91, 32));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Smaller file"));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Balanced"));
    QCOMPARE(buttons[2]->text(), QStringLiteral("Best quality"));
    QCOMPARE(buttons[3]->text(), QStringLiteral("Custom…"));
    QCOMPARE(segments->getCurrentIndex(), 1);

    // toolbar-right 291:1026: Tutorial, disabled with its place kept (Q5), Cancel, Compress
    QVERIFY(!toolbar->getTutorialButton()->isEnabled());
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QCOMPARE(H::inWindow(toolbar->getConfirmButton()).right(), 1427);
    QCOMPARE(toolbar->getConfirmText(), QStringLiteral("Compress"));
    QVERIFY(toolbar->getConfirmButton()->isEnabled());

    // The panels and the canvas: 300 + 6 + 814 + 320; the thumbnails of the document stay left
    QCOMPARE(H::inWindow(shell->getRightPanel()), QRect(1120, 90, 320, 842));
    QCOMPARE(tab->getView()->getCanvasArea()->width(), 814);
    QCOMPARE(H::inWindow(tab->getView()->getCanvasArea()).left(), 306);
    QVERIFY(!shell->getLeftPanel());
    QCOMPARE(gui->getInitialSidebarPanel(), QStringLiteral("thumbnails"));

    // split-line 293:1346 at 712 of the window, the handle 699 x 470, the chips 126
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QVERIFY(overlay);
    QCOMPARE(overlay->getLineRect(), QRect(406, 0, 2, 842));
    QCOMPARE(overlay->getHandleRect(), QRect(393, 380, 28, 28));
    QCOMPARE(overlay->getBeforeChipRect().right() + 1, 393);
    QCOMPARE(overlay->getBeforeChipRect().top(), 36);
    QCOMPARE(overlay->getBeforeChipRect().height(), 21);
    QCOMPARE(overlay->getAfterChipRect().left(), 421);
}

void AppToolCompressTest::measuredPanel()
{
    // sidebar-right 291:1177 with the numbers of the real file
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("report.pdf")));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    AppToolShellFixture::settle();

    // header 293:1355 "Result measured", the card 287 x 85 at 1137 x 128
    QCOMPARE(gui->getResultHeader()->getTitleLabel()->text(), QStringLiteral("Result"));
    QCOMPARE(gui->getResultHeader()->getHintLabel()->text(), QStringLiteral("measured"));
    pdf::PDFAppToolMeasuredCard* card = gui->getMeasuredCard();
    QVERIFY(card->isVisible());
    QCOMPARE(H::inWindow(card), QRect(1137, 128, 287, 85));

    // 293:1359 items-baseline: both sizes end on one line, y 160 of 291:994
    const int beforeBottom = H::inkBottom(card->getBeforeLabel());
    const int afterBottom = H::inkBottom(card->getAfterLabel());
    QCOMPARE(beforeBottom, afterBottom);
    QVERIFY2(qAbs(afterBottom + 1 - 160) <= 1, qPrintable(QString::number(afterBottom)));

    // 293:1355 items-baseline: "Result" and "measured" on one line
    QCOMPARE(H::inkBottom(gui->getResultHeader()->getHintLabel()), H::inkBottom(gui->getResultHeader()->getTitleLabel()));

    // The places of the frames: 293:1359 before y 5 height 18, after y 0 height 24; 293:1355 hint y 2
    const QWidget* sizesRow = card->getBeforeLabel()->parentWidget();
    QCOMPARE(card->getBeforeLabel()->contentsRect().translated(card->getBeforeLabel()->mapTo(sizesRow, QPoint(0, 0))).top(), 5);
    QCOMPARE(card->getBeforeLabel()->contentsRect().height(), 18);
    QCOMPARE(card->getAfterLabel()->contentsRect().translated(card->getAfterLabel()->mapTo(sizesRow, QPoint(0, 0))).top(), 0);
    QCOMPARE(card->getAfterLabel()->contentsRect().height(), 24);
    QCOMPARE(gui->getResultHeader()->getHintLabel()->contentsRect().translated(gui->getResultHeader()->getHintLabel()->mapTo(gui->getResultHeader(), QPoint(0, 0))).top(), 2);

    // One line at every scale, also in the dark theme and right-to-left: at most a device pixel apart by rounding
    for (const qreal ratio : { 1.0, 1.5, 2.0 })
    {
        const QImage image = AppToolShellFixture::render(card, ratio).convertToFormat(QImage::Format_RGB32);
        const int before = H::inkBottomInImage(image, card, card->getBeforeLabel(), ratio);
        const int after = H::inkBottomInImage(image, card, card->getAfterLabel(), ratio);
        QVERIFY2(before >= 0 && qAbs(before - after) <= 1, qPrintable(QStringLiteral("%1: %2, %3").arg(ratio).arg(before).arg(after)));
    }

    // The sizes are the size of the file and of the written result
    const qint64 sizeBefore = QFileInfo(filePath).size();
    QCOMPARE(card->getBeforeLabel()->text(), pdf::PDFDocumentToolFormat::formatSize(sizeBefore));
    QVERIFY(card->getBar()->isVisible());
    QCOMPARE(card->getBar()->height(), 4);
    QVERIFY2(card->getBar()->value() > 0 && card->getBar()->value() < 100, "The bar shows the share of the size after");
    QCOMPARE(card->getSavingLabel()->getTextColor(), pdf::PDFThemeColors::TextAccent);
    QVERIFY2(card->getSavingLabel()->text().contains(QStringLiteral("% smaller · ")), qPrintable(card->getSavingLabel()->text()));
    QVERIFY(!gui->getMeasuringCard()->isVisible());
    QVERIFY(!gui->getNotReducedCallout()->isVisible());

    // section-What changes 293:1366: at 229, the rows of 28 at 253, the note
    QCOMPARE(gui->getChangesHeader()->getTitleLabel()->text(), QStringLiteral("What changes"));
    QCOMPARE(H::inWindow(gui->getChangesHeader()).top(), 229);
    QCOMPARE(H::inWindow(gui->getChangeRows()->parentWidget()), QRect(1137, 253, 287, 126));
    const QStringList keys = H::rowKeys(gui->getChangeRows());
    QCOMPARE(keys.size(), 4);
    QVERIFY2(keys[0].startsWith(QStringLiteral("Images (")), qPrintable(keys[0]));
    QCOMPARE(keys.mid(1), QStringList({ QStringLiteral("Fonts"), QStringLiteral("Page content"), QStringLiteral("Structure") }));
    QVERIFY(gui->getChangeRows()->getValue(0).contains(QStringLiteral("→")));
    QCOMPARE(gui->getPresetNote()->text(), QStringLiteral("Balanced: images are reduced to 200 dpi where no difference is visible, unused objects are removed, identical objects merged and streams recompressed."));
    QCOMPARE(H::inWindow(gui->getPresetNote()).top(), 387);

    // status-message 291:1065
    const QString status = shell->getStatusMessage();
    QVERIFY2(status.startsWith(QStringLiteral("Compress · report.pdf · 1 page · ")), qPrintable(status));

    // The chips say the same sizes
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QCOMPARE(overlay->getBeforeText(), QStringLiteral("BEFORE · ") + pdf::PDFDocumentToolFormat::formatSize(sizeBefore));
    QCOMPARE(overlay->getAfterText(), QStringLiteral("AFTER · ") + card->getAfterLabel()->text());
}

void AppToolCompressTest::changeRowsNotElided()
{
    // A real value beside a short key has more room than the half of a panel: drawn in the
    // list of 261 px it ends as in a wide list, it is not elided
    pdf::PDFKeyValueList narrow;
    pdf::PDFKeyValueList wide;
    const QString value = QStringLiteral("804.2 kB → 41.5 kB");
    narrow.addRow(QStringLiteral("Images (1)"), value);
    wide.addRow(QStringLiteral("Images (1)"), value);
    narrow.resize(261, 28);
    wide.resize(600, 28);
    const int valueWidth = QFontMetrics(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallMedium)).horizontalAdvance(value);
    QVERIFY2(valueWidth > 261 - 20 - 120 - 12, "The value does not fit the part behind a key column of 120");
    const QImage first = narrow.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const QImage second = wide.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int checked = valueWidth + 12;
    for (int y = 0; y < 28; ++y)
    {
        for (int x = 0; x < checked; ++x)
        {
            QCOMPARE(first.pixel(first.width() - 1 - x, y), second.pixel(second.width() - 1 - x, y));
        }
    }
}

void AppToolCompressTest::measuringState()
{
    // No frame (Q1, named deviation): the card Measuring…, the hint measuring, Compress
    // stays active, no line and no chip AFTER, the rows without numbers
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    if (gui->getMeasureState() != PDFAppToolGUICompress::MeasureState::Measuring)
    {
        QSKIP("The measuring has ended while the tool was opened");
    }

    QVERIFY(gui->getMeasuringCard()->isVisible());
    QVERIFY(gui->getMeasuringCard()->getTitleLabel()->text().startsWith(QStringLiteral("Measuring… ")));
    QVERIFY(!gui->getMeasuredCard()->isVisible());
    QCOMPARE(gui->getResultHeader()->getHintLabel()->text(), QStringLiteral("measuring"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(gui->getPresetControl()->getSegmentedControl()->isEnabled());
    QVERIFY(!gui->getBeforeAfterOverlay()->hasResult());
    QVERIFY(!gui->getBeforeAfterOverlay()->isVisible());
    QVERIFY(!gui->getBeforeAfterOverlay()->getBeforeText().isEmpty());
    QCOMPARE(H::rowKeys(gui->getChangeRows()), QStringList({ QStringLiteral("Images"), QStringLiteral("Fonts"), QStringLiteral("Page content"), QStringLiteral("Structure") }));
    QCOMPARE(gui->getChangeRows()->getValue(0), QStringLiteral("–"));

    QVERIFY(H::waitMeasured(gui));
    QVERIFY(gui->getBeforeAfterOverlay()->hasResult());
    QVERIFY(gui->getBeforeAfterOverlay()->isVisible());
}

void AppToolCompressTest::presetChangeMeasuresAgain()
{
    // A change cancels and measures the new preset; Best quality keeps the images
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);

    // While the first measuring runs
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(gui->getPresetNote()->text().startsWith(QStringLiteral("Smaller file: ")));
    QVERIFY(H::waitMeasured(gui));
    const QString smaller = gui->getMeasuredCard()->getAfterLabel()->text();

    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best-quality"));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(!gui->getBeforeAfterOverlay()->hasResult());
    QVERIFY(H::waitMeasured(gui));
    QVERIFY(gui->getPresetNote()->text().startsWith(QStringLiteral("Best quality: ")));
    QVERIFY2(gui->getMeasuredCard()->getAfterLabel()->text() != smaller || gui->getMeasureState() == PDFAppToolGUICompress::MeasureState::NotReduced, "Another preset measures another size");
}

void AppToolCompressTest::undoOfThePreset()
{
    // K6: disabled until the first change; Undo of the shell gives the preset back
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!shell->getModel()->canUndo());
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());

    // A click on the segment, as the user: written by the binder from the event loop
    const QList<QToolButton*> buttons = gui->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>();
    QTest::mouseClick(buttons[0], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return H::preset(shell) == QStringLiteral("smaller-file"); }));
    QVERIFY(shell->getModel()->canUndo());
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());

    QTest::mouseClick(shell->getToolbar()->getUndoButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return H::preset(shell) == QStringLiteral("balanced"); }));
    QCOMPARE(gui->getPresetControl()->getSegmentedControl()->getCurrentIndex(), 1);
    QVERIFY(H::waitMeasured(gui));
    QVERIFY(gui->getPresetNote()->text().startsWith(QStringLiteral("Balanced: ")));
}

void AppToolCompressTest::confirmDuringMeasuring()
{
    // 295:1711: confirming joins the running preview; the segments and Undo disabled, Stop,
    // the card inside of Result, the callout below it, What changes text/disabled
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 2400, 2));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);

    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    AppToolShellFixture::settle();
    if (shell->getState() != PDFDocumentToolShell::State::Running)
    {
        QSKIP("The run has ended in the moment");
    }

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(!gui->getPresetControl()->getSegmentedControl()->isEnabled());
    QVERIFY(!toolbar->getUndoButton()->isEnabled());
    QVERIFY(toolbar->getStopButton()->isVisible());
    QVERIFY(!toolbar->getCancelButton()->isVisible());
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QVERIFY2(tab->getDocument()->isBlockedByOperation(), "295:1711: the run holds the document");

    pdf::PDFAppToolProgressCard* card = shell->getProgressCard();
    QVERIFY(card && card->isVisible());
    QVERIFY(card->getTitleLabel()->text().startsWith(QStringLiteral("Compressing… ")));
    QCOMPARE(H::inWindow(card).top(), 128);
    QCOMPARE(H::inWindow(card).left(), 1137);
    QCOMPARE(card->width(), 287);
    QVERIFY(card->isEnabled());
    QVERIFY(!gui->getMeasuredCard()->isVisible());
    QVERIFY(!gui->getMeasuringCard()->isVisible());

    pdf::PDFAppToolCallout* callout = shell->getBusyCallout();
    QVERIFY(callout && callout->isVisible());
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("The document is busy"));
    QCOMPARE(callout->getTextLabel()->text(), QStringLiteral("It cannot be edited and no other tool can run on it until Compress finishes. Other tabs stay available. Stop ends the job and writes nothing."));
    QCOMPARE(H::inWindow(callout).top(), H::inWindow(card).bottom() + 1 + 16);

    QVERIFY(!gui->getChangesHeader()->isEnabled());
    QVERIFY(!gui->getPresetNote()->isEnabled());
    QVERIFY(gui->getResultHeader()->isEnabled());
    QVERIFY(!gui->getBeforeAfterOverlay()->hasResult());

    // The step of the frame 295:1821 in the status bar, by the texts of the core
    QCOMPARE(gui->getRunningStatusText(QStringLiteral("Recompressing images · 152 of 412")), QStringLiteral("Compress: recompressing images 152 of 412 · press Stop or Esc to stop"));
    QCOMPARE(gui->getRunningTitle(37), QStringLiteral("Compressing… 37 %"));

    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }, 120000));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);

    // 296:2761: the panel beside the dialog has the result, the section enabled again
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    QVERIFY(gui->getMeasuredCard()->isVisible());
    QVERIFY(!gui->getMeasuringCard()->isVisible());
    QVERIFY(gui->getChangesHeader()->isEnabled());
    QVERIFY(gui->getChangeRows()->getValue(0).contains(QStringLiteral("→")));
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::stopGoesBackWithoutResult()
{
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 2400, 2));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    const quint64 version = tab->getDocument()->getVersion();

    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    shell->stop();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    AppToolShellFixture::settle();

    // Back without a result: nothing written, Compress computes again, no dialog
    QVERIFY(!shell->getResultDialog());
    QCOMPARE(tab->getDocument()->getVersion(), version);
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Stopped);
    QVERIFY(gui->getMeasuringCard()->isVisible());
    QCOMPARE(gui->getMeasuringCard()->getTitleLabel()->text(), QStringLiteral("Not measured"));

    // No hint "measured" next to "Not measured"
    QVERIFY(gui->getResultHeader()->getHintLabel()->isHidden());
    QVERIFY(gui->getChangesHeader()->isEnabled());
    QVERIFY(gui->getPresetControl()->getSegmentedControl()->isEnabled());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!shell->getProgressCard());
}

void AppToolCompressTest::reviewStoppedHasExits()
{
    // Not measured is no dead end: the next confirmation computes, another preset and a new
    // version of the document measure, the card says it
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 1200, 1));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();

    // A new version
    shell->confirm();
    shell->stop();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Stopped);
    QCOMPARE(gui->getMeasuringCard()->getTitleLabel()->text(), QStringLiteral("Not measured"));
    document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(gui->getMeasuringCard()->getTitleLabel()->text().startsWith(QStringLiteral("Measuring… ")));
    QVERIFY(H::waitMeasured(gui));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);

    // Another preset: the measuring does not ask for the document, it stays editable
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    shell->stop();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Stopped);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("balanced"));

    // The measured result of the preset is still in the cache of the runner: it comes at once
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    QVERIFY(!document->isBlockedByOperation());

    // A preset without a result measures
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best-quality"));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(H::waitMeasured(gui));

    // The confirmation after Stop computes again and comes to the dialog
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    shell->confirm();
    shell->stop();
    QVERIFY(!shell->getRunner()->isPreviewPending());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }, 120000));
    QVERIFY(shell->getResultDialog());
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    shell->getResultDialog()->getDiscardButton()->click();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewPresetChangedBeforeConfirm()
{
    // A preset changed in the gap between a finished measuring and the confirmation: the
    // dialog has the result of the new preset, never the older one of the cache
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    const QString balanced = gui->getMeasuredCard()->getAfterLabel()->text();

    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    QVERIFY(shell->getRunner()->isPreviewPending());
    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    QVERIFY2(!shell->getRunner()->isPreviewPending(), "The delay of the preview is over: the run computes");
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }, 120000));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    AppToolShellFixture::settle();
    const QString smaller = gui->getMeasuredCard()->getAfterLabel()->text();
    QVERIFY2(smaller != balanced, qPrintable(smaller));
    QVERIFY2(dialog->getFacts()->getValue(0).contains(smaller), qPrintable(dialog->getFacts()->getValue(0)));
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewVersionChangeWhileMeasuring()
{
    // A new version during the measuring (MCP, the automatic refresh): the older computation is
    // dropped and the new version measured; the result, which is measured, has the new version
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 1200, 1));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QTest::qWait(pdf::PDFDocumentToolRunner::PREVIEW_DELAY + 10);
    QVERIFY(shell->getRunner()->isPreviewPending());
    pdf::PDFAppDocument* document = tab->getDocument();
    document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(shell->getRunner()->isPreviewPending());
    QVERIFY(H::waitMeasured(gui));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    QVERIFY(shell->getRunner()->getPreview());

    // During a measured state: the chip AFTER goes until the new measuring has ended
    document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);
    QVERIFY(!gui->getBeforeAfterOverlay()->hasResult());
    QVERIFY(H::waitMeasured(gui));
    QVERIFY(gui->getBeforeAfterOverlay()->hasResult());
}

void AppToolCompressTest::confirmMeasuredOpensDialog()
{
    // 296:2761: a finished preview opens the dialog at once; only Tutorial, no overlay, Save
    // as a copy preselected with its suggested name 312:7410, the facts of the run
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();

    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Deciding);
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    AppToolShellFixture::settle();

    QVERIFY(dialog->isModal());
    QVERIFY(!gui->getBeforeAfterOverlay()->isOverlayShown());
    QVERIFY(!gui->getBeforeAfterOverlay()->isVisible());
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QVERIFY(toolbar->getCancelButton()->isVisible() && toolbar->getCancelButton()->isEnabled());
    QVERIFY(toolbar->getConfirmButton()->isVisible() && !toolbar->getConfirmButton()->isEnabled());
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    QVERIFY(dialog->getOption(Choice::Keep));
    QVERIFY(dialog->getOption(Choice::Overwrite));
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Project_Report_compressed.pdf"));
    const QStringList facts = H::rowKeys(dialog->getFacts());
    QCOMPARE(facts, QStringList({ QStringLiteral("Size"), QStringLiteral("Images"), QStringLiteral("Time") }));
    QVERIFY2(dialog->getFacts()->getValue(0).contains(QStringLiteral("% smaller)")), qPrintable(dialog->getFacts()->getValue(0)));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Compress finished · the result is waiting for your decision"));

    // Discard: the document stays, the overlay is gone with the tool
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab) || AppToolShellFixture::getShell(tab)->getState() == PDFDocumentToolShell::State::Closed);
}

void AppToolCompressTest::writeCopySmaller()
{
    // A real written file is smaller than the original, and the original stays
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf")));
    const qint64 sizeBefore = QFileInfo(filePath).size();
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    const quint64 version = tab->getDocument()->getVersion();
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    dialog->getOpenCheckBox()->setChecked(false);
    const QString target = dialog->getTargetFilePath();
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&target]() { return QFileInfo::exists(target); }));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    QVERIFY2(QFileInfo(target).size() < sizeBefore, "The written file is smaller");
    QCOMPARE(QFileInfo(filePath).size(), sizeBefore);
    QCOMPARE(tab->getDocument()->getVersion(), version);

    // The copy has the page; the use comes with the acceptance: tool_usage has the run
    QCOMPARE(H::readDocument(target)->getCatalog()->getPageCount(), size_t(1));
    const std::vector<pdf::PDFToolUsage> usage = pdf::PDFToolRepository(fixture.context->getDatabase()).getUsage();
    QCOMPARE(usage.size(), size_t(1));
    QCOMPARE(usage.front().toolId, H::TOOL_ID);
}

void AppToolCompressTest::writeErrorFrame()
{
    // 316:7293: the file name in the error state, the red box, the dialog stays
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);

    // The target folder removed behind the dialog: chosen by Browse…, gone at the write
    const QString folder = fixture.path(QStringLiteral("gone"));
    QVERIFY(QDir().mkpath(folder));
    pdf::PDFAppFileDialogs::setFolderHandler([folder](QWidget*, const QString&) { return folder; });
    dialog->getBrowseButton()->click();
    pdf::PDFAppFileDialogs::setFolderHandler(nullptr);
    QVERIFY(QDir(folder).removeRecursively());
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([dialog]() { return dialog->getWriteError() && dialog->getWriteError()->isVisible(); }));
    QVERIFY(shell->getResultDialog() == dialog);
    QVERIFY2(shell->getStatusMessage().startsWith(QStringLiteral("Compress: the copy could not be written – ")), qPrintable(shell->getStatusMessage()));
    QVERIFY(dialog->getChooseLocationButton()->isVisible());

    // file-name of 316:7293: the error state of a field (Widgets: text/danger; the frame has accent/red/border-strong, reported); a new name clears it
    pdf::PDFLineEdit* field = dialog->getFileNameField();
    QVERIFY(field->isInvalid());
    const QImage invalid = field->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(invalid.pixelColor(invalid.width() / 2, 0), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::TextDanger));
    QTest::keyClicks(field, QStringLiteral("x"));
    QVERIFY(!field->isInvalid());
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::notReduced()
{
    // 295:2182: the amber callout with Try Smaller file, Compress disabled, the line stays
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writeCompressed(fixture.folder(), fixture.path(QStringLiteral("small.pdf")));
    QVERIFY(!filePath.isEmpty());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best-quality"));
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::NotReduced);

    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    pdf::PDFAppToolMeasuredCard* card = gui->getMeasuredCard();
    QVERIFY(card->isVisible());
    QVERIFY(!card->getBar()->isVisible());
    QCOMPARE(card->getSavingLabel()->text(), QStringLiteral("No saving with this preset"));
    QCOMPARE(card->getSavingLabel()->getTextColor(), pdf::PDFThemeColors::TextSecondary);
    QCOMPARE(card->height(), 73);

    pdf::PDFAppToolCallout* callout = gui->getNotReducedCallout();
    QVERIFY(callout->isVisible());
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("File cannot be reduced with these settings"));
    QCOMPARE(callout->getTextLabel()->text(), QStringLiteral("Rewriting the whole file can add as much as the images save. A stronger preset may help – see the breakdown below."));
    QPushButton* button = gui->getTrySmallerButton();
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Try Smaller file"));

    // The line and AFTER of the frame, the original at both sides, no document of a result
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QVERIFY(overlay->hasResult());
    QVERIFY(overlay->getAfterText().startsWith(QStringLiteral("AFTER · ")));
    QVERIFY(overlay->isVisible());

    // No use without a run: the repository has no row of the tool
    QCOMPARE(pdf::PDFToolRepository(fixture.context->getDatabase()).getUsage().size(), size_t(0));

    // Try Smaller file switches the preset, which reduces the images of 200 dpi further
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(H::preset(shell), QStringLiteral("smaller-file"));
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    QVERIFY(!gui->getNotReducedCallout()->isVisible());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolCompressTest::notReducedSmallerFile()
{
    // Owner 2026-11-02: at Smaller file there is no stronger preset: the sentence and the
    // button are gone (a file, which Smaller file has compressed already)
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writeCompressed(fixture.folder(), fixture.path(QStringLiteral("small.pdf")), QStringLiteral("smaller-file"));
    QVERIFY(!filePath.isEmpty());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::NotReduced);
    QVERIFY(gui->getNotReducedCallout()->isVisible());
    QVERIFY(!gui->getTrySmallerButton()->isVisible());
    QCOMPARE(gui->getNotReducedCallout()->getTextLabel()->text(), QStringLiteral("Rewriting the whole file can add as much as the images save. See the breakdown below."));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolCompressTest::customPanelGeometry()
{
    // 294:1340: the panel 360 x 326 at 509 x 94 and its rows, by the numbers of the frame
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);

    const QList<QToolButton*> buttons = gui->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>();
    QTest::mouseClick(buttons[3], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCustomPanel()->isVisible(); }));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return H::preset(shell) == QStringLiteral("custom"); }));
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();

    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    const QPoint windowOrigin = fixture.window->mapToGlobal(QPoint(0, 0));

    // The offscreen screen is narrower than the frame: the place computed for the panel is
    // the place of the frame (x 509), the screen limits only the real window. At most 1 px by
    // the width of the text "Preset".
    const QRect anchor(gui->getPresetControl()->getSegmentedControl()->mapToGlobal(QPoint(0, 0)), QSize(364, 32));
    QVERIFY2(qAbs(panel->getPanelLeft(anchor, 360) - windowOrigin.x() - 509) <= 1, qPrintable(QString::number(panel->getPanelLeft(anchor, 360) - windowOrigin.x())));
    const QRect panelRect = panel->getPanelRect().translated(panel->pos());
    QCOMPARE(panelRect.top() - windowOrigin.y(), 94);
    QCOMPARE(panelRect.size(), QSize(360, 326));

    // In the coordinates of the frame
    const QPoint frameOffset = QPoint(509, 94) - panelRect.topLeft();
    const auto rectOf = [&frameOffset](const QWidget* widget) { return QRect(widget->mapToGlobal(QPoint(0, 0)) + frameOffset, widget->size()); };
    pdf::PDFAppSettingControl* dpi = panel->getControl(QStringLiteral("targetDpi"));
    pdf::PDFAppSettingControl* quality = panel->getControl(QStringLiteral("jpegQuality"));
    pdf::PDFAppSettingControl* goal = panel->getControl(QStringLiteral("goal"));
    pdf::PDFAppSettingControl* keep = panel->getControl(QStringLiteral("keepOriginalIfLarger"));
    pdf::PDFAppSettingControl* transparency = panel->getControl(QStringLiteral("preserveTransparency"));
    pdf::PDFAppSettingControl* structure = panel->getControl(QStringLiteral("cleanStructure"));
    QVERIFY(dpi && quality && goal && keep && transparency && structure);
    QCOMPARE(rectOf(dpi->getComboBox()), QRect(702, 138, 150, 34));
    QCOMPARE(rectOf(quality->getSlider()), QRect(649, 186, 180, 20));
    QCOMPARE(quality->getValueLabel()->text(), QStringLiteral("75"));
    QCOMPARE(rectOf(quality->getValueLabel()).right(), 851);
    QCOMPARE(rectOf(goal->getRadioButton(QStringLiteral("prefer-quality"))).topLeft(), QPoint(526, 220));
    QCOMPARE(rectOf(goal->getRadioButton(QStringLiteral("prefer-quality"))).height(), 18);
    QCOMPARE(rectOf(goal->getRadioButton(QStringLiteral("minimum-size"))).left(), rectOf(goal->getRadioButton(QStringLiteral("prefer-quality"))).right() + 1 + 14);
    QCOMPARE(rectOf(keep->getCheckBox()).topLeft(), QPoint(526, 252));
    QCOMPARE(rectOf(transparency->getCheckBox()).topLeft(), QPoint(526, 284));
    QCOMPARE(rectOf(structure->getCheckBox()).topLeft(), QPoint(526, 358));
    QCOMPARE(keep->getCheckBox()->text(), QStringLiteral("Keep original image if the new one is larger"));
    QCOMPARE(transparency->getCheckBox()->text(), QStringLiteral("Preserve transparency"));
    QCOMPARE(structure->getCheckBox()->text(), QStringLiteral("Clean up and recompress the file structure"));
    QCOMPARE(goal->getRadioButton(QStringLiteral("prefer-quality"))->text(), QStringLiteral("Prefer quality"));
    QCOMPARE(goal->getRadioButton(QStringLiteral("minimum-size"))->text(), QStringLiteral("Minimum size"));
    const pdf::PDFAppParagraph* help = panel->findChild<pdf::PDFAppParagraph*>(QStringLiteral("customHelp"));
    QVERIFY(help);
    QCOMPARE(help->text(), QStringLiteral("Lossless – the pages look exactly the same."));
    QCOMPARE(rectOf(help).top(), 390);

    // The offered resolutions (plan chapter 3) with the value of the frame
    QStringList items;
    for (int i = 0; i < dpi->getComboBox()->count(); ++i)
    {
        items << dpi->getComboBox()->itemText(i);
    }
    QCOMPARE(items, QStringList({ QStringLiteral("72 dpi"), QStringLiteral("96 dpi"), QStringLiteral("110 dpi"), QStringLiteral("150 dpi"), QStringLiteral("200 dpi"), QStringLiteral("300 dpi") }));
    QCOMPARE(dpi->getComboBox()->currentText(), QStringLiteral("150 dpi"));
    QVERIFY(goal->getRadioButton(QStringLiteral("minimum-size"))->isChecked());

    // The note of 294:1436
    QCOMPARE(gui->getPresetNote()->text(), QStringLiteral("Custom: images at 150 dpi, JPEG quality 75, minimum size preferred. Structure clean-up is on."));
    panel->hide();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::customPanelValues()
{
    // Every control writes the parameter through the binder; Undo goes back into the control
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("custom"));
    gui->openCustomPanel();
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QVERIFY(panel->isVisible());
    pdf::PDFAppToolParameterModel* model = shell->getModel();

    // Downsample to: a number of the model from an item of the combo box
    pdf::PDFAppSettingControl* dpi = panel->getControl(QStringLiteral("targetDpi"));
    dpi->getComboBox()->setCurrentIndex(0);
    Q_EMIT dpi->getComboBox()->activated(0);
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->getValue(QStringLiteral("targetDpi")).toInt() == 72; }));
    QVERIFY(gui->getPresetNote()->text().contains(QStringLiteral("72 dpi")));

    // JPEG quality: the slider, written at its release
    pdf::PDFAppSettingControl* quality = panel->getControl(QStringLiteral("jpegQuality"));
    quality->getSlider()->setValue(40);
    QCOMPARE(quality->getValueLabel()->text(), QStringLiteral("40"));
    quality->commitPendingValue();
    QCOMPARE(model->getValue(QStringLiteral("jpegQuality")).toInt(), 40);
    QVERIFY(gui->getPresetNote()->text().contains(QStringLiteral("JPEG quality 40")));

    // Radio: a click on Prefer quality
    pdf::PDFAppSettingControl* goal = panel->getControl(QStringLiteral("goal"));
    goal->getRadioButton(QStringLiteral("prefer-quality"))->click();
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->getValue(QStringLiteral("goal")).toString() == QStringLiteral("prefer-quality"); }));
    QVERIFY(goal->getRadioButton(QStringLiteral("prefer-quality"))->isChecked());
    QVERIFY(!goal->getRadioButton(QStringLiteral("minimum-size"))->isChecked());

    // A check box
    panel->getControl(QStringLiteral("cleanStructure"))->getCheckBox()->click();
    QVERIFY(!model->getValue(QStringLiteral("cleanStructure")).toBool());
    QVERIFY(gui->getPresetNote()->text().contains(QStringLiteral("Structure clean-up is off.")));

    // Undo reaches the controls
    model->undo();
    QVERIFY(panel->getControl(QStringLiteral("cleanStructure"))->getCheckBox()->isChecked());
    model->undo();
    QVERIFY(goal->getRadioButton(QStringLiteral("minimum-size"))->isChecked());
    model->undo();
    QCOMPARE(quality->getSlider()->value(), 75);
    model->undo();
    QCOMPARE(dpi->getComboBox()->currentText(), QStringLiteral("150 dpi"));

    // A value, which is not offered, stays shown as the value used
    panel->hide();
    model->setValue(QStringLiteral("targetDpi"), 123);
    const std::optional<pdf::PDFSettingDefinition> definition = model->toChoiceDefinition(QStringLiteral("targetDpi"), pdf::PDFDocumentToolCompress::getOfferedResolutions(), QStringLiteral("%1 dpi"));
    QVERIFY(definition);
    bool isOffered = false;
    for (const pdf::PDFSettingOption& option : definition->getOptions())
    {
        isOffered = isOffered || option.id == QStringLiteral("123");
    }
    QVERIFY(isOffered);
    AppToolShellFixture::settle();
}

void AppToolCompressTest::customPanelClickTogglesAndNarrow()
{
    // Esc closes only the panel; in a narrower window the panel lies inside of the canvas (K13)
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1000, 700));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->openCustomPanel();
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QVERIFY(panel->isVisible());

    const QWidget* canvas = tab->getView()->getCanvasArea();
    const QRect canvasRect(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
    const QRect panelRect = panel->getPanelRect().translated(panel->pos());
    if (canvasRect.width() >= panelRect.width())
    {
        QVERIFY2(canvasRect.contains(QRect(panelRect.topLeft(), QSize(panelRect.width(), 1))), "The panel is inside of the canvas");
    }
    else
    {
        QCOMPARE(panelRect.left(), canvasRect.left());
    }

    QTest::keyClick(panel, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);

    // A panel closed; Space on the segment opens it; the arrows only choose
    QToolButton* custom = gui->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>().back();
    custom->setFocus();
    QTest::keyClick(custom, Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([panel]() { return panel->isVisible(); }));
    panel->hide();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::lastValuesOnlyAccepted()
{
    // The last values are kept after an accepted result only (K6, 2.1)
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    QVERIFY(H::gui(shell));
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    shell->cancel();
    AppToolShellFixture::settle();

    shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(H::preset(shell), QStringLiteral("balanced"));
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    QVERIFY(H::waitMeasured(gui));
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    dialog->getOption(Choice::Keep)->getRadioButton()->click();
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));

    // Keep is one undo step; a use recorded at the acceptance
    QVERIFY(tab->getDocument()->isModified());
    const std::vector<pdf::PDFToolUsage> usage = pdf::PDFToolRepository(fixture.context->getDatabase()).getUsage();
    QCOMPARE(usage.size(), size_t(1));
    QCOMPARE(usage.front().toolId, H::TOOL_ID);

    shell = H::openCompress(fixture, tab);
    QVERIFY(shell);
    QCOMPARE(H::preset(shell), QStringLiteral("smaller-file"));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::keys()
{
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);

    // F1: the topic of the tool
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(pdf::PDFAppHelpCatalog::getBuiltin()->findTopic(QStringLiteral("tool-optimize-compress")));
    QCOMPARE(AppToolShellFixture::getShell(tab)->getCatalogEntry()->getHelpTopic(), QStringLiteral("tool-optimize-compress"));
    fixture.pressHelpKey();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tool-optimize-compress"));
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();

    // Ctrl+Enter confirms, Esc of the dialog stays with the dialog, Esc of the tool cancels
    QVERIFY(H::waitMeasured(gui));
    QAction* confirm = shell->findChild<QAction*>(QStringLiteral("toolConfirm"));
    QVERIFY(confirm);
    QCOMPARE(confirm->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Return));
    confirm->trigger();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Deciding);
    shell->getResultDialog()->getDiscardButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));

    shell = H::openCompress(fixture, tab);
    QVERIFY(shell);
    QAction* escape = shell->findChild<QAction*>(QStringLiteral("toolEscape"));
    QVERIFY(escape);
    QCOMPARE(escape->shortcut(), QKeySequence(Qt::Key_Escape));

    // Esc of an open panel closes only the panel, then the tool
    H::gui(shell)->openCustomPanel();
    QVERIFY(H::gui(shell)->getCustomPanel()->isVisible());
    escape->trigger();
    AppToolShellFixture::settle();
    QVERIFY(!H::gui(shell)->getCustomPanel()->isVisible());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    escape->trigger();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(tab->getDocument()->isModified(), false);
}

void AppToolCompressTest::signedDocument()
{
    // O31: only Save as a copy with the warning, also in the real tool; At a glance says it
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));

    if (gui->getMeasureState() == PDFAppToolGUICompress::MeasureState::NotReduced)
    {
        QSKIP("The signed sample cannot get smaller");
    }
    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getSignedWarning());
    QVERIFY(!dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getOption(Choice::Overwrite));
    QVERIFY(dialog->getOption(Choice::Copy));
    QVERIFY2(!dialog->getOption(Choice::Copy)->getTag() || dialog->getOption(Choice::Copy)->getTag()->isHidden(), "Without the Tag Recommended, chapter 4");
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();

    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    const QStringList keys = H::rowKeys(page->getFacts());
    QCOMPARE(page->getFacts()->getValue(int(keys.indexOf(QStringLiteral("Signatures")))), QStringLiteral("Lost (copy only)"));
}

void AppToolCompressTest::encryptedCopy()
{
    // Plan chapter 7: the document is decrypted in the memory, the copy is encrypted again:
    // it asks for its password, a wrong one does not open it, the right one does
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writeEncryptedPhoto(fixture.folder(), fixture.path(QStringLiteral("Secret.pdf")));
    QVERIFY(!filePath.isEmpty());
    const qint64 sizeBefore = QFileInfo(filePath).size();

    pdf::PDFAppDocumentTab* tab = H::openWithPassword(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!shell->isPermissionRefused());
    QVERIFY(H::waitMeasured(gui));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);

    shell->confirm();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    dialog->getOpenCheckBox()->setChecked(false);
    const QString target = dialog->getTargetFilePath();
    QCOMPARE(QFileInfo(target).fileName(), QStringLiteral("Secret_compressed.pdf"));
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    QVERIFY(QFileInfo::exists(target));
    QVERIFY2(QFileInfo(target).size() < sizeBefore, "The encrypted copy is smaller");

    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(bytes.contains("/Encrypt"));

    pdf::PDFDocumentReader::Result result = pdf::PDFDocumentReader::Result::OK;
    H::readWithPassword(bytes, QString(), &result);
    QVERIFY2(result != pdf::PDFDocumentReader::Result::OK, "Without its password the copy does not open");
    result = pdf::PDFDocumentReader::Result::OK;
    H::readWithPassword(bytes, QStringLiteral("wrong"), &result);
    QVERIFY2(result != pdf::PDFDocumentReader::Result::OK, "A wrong password does not open the copy");

    result = pdf::PDFDocumentReader::Result::Failed;
    const pdf::PDFDocument copy = H::readWithPassword(bytes, AppDocumentSamples::getUserPassword(), &result);
    QCOMPARE(result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(copy.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(copy.getStorage().getSecurityHandler()->getMode() != pdf::EncryptionMode::None);
    const std::vector<pdf::PDFImageOptimizer::ImageInfo> images = pdf::PDFImageOptimizer::collectImageInfos(&copy);
    QCOMPARE(images.size(), size_t(1));
    QVERIFY2(images.front().image.width() < 900 && !images.front().image.isNull(), "The image of the copy can be decoded and is reduced");
}

void AppToolCompressTest::encryptedRefusedWithoutPermission()
{
    // 339:10328 of owner answer C: the owner has not allowed Modify (bit 4 of Table 22)
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = fixture.open(H::writeEncryptedPhoto(fixture.folder(), fixture.path(QStringLiteral("Refused.pdf")), QString(), 0xFFFFFFFCu & ~0x0008u));
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    QVERIFY(shell);
    QVERIFY(shell->isPermissionRefused());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getPermissionDialog() != nullptr; }));
    shell->getPermissionDialog()->reject();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    QVERIFY(!tab->getDocument()->isModified());
}

void AppToolCompressTest::reviewSegmentsDisabledWithLabel()
{
    // 295:1711 and 296:2761: the segments text/disabled, the label "Preset" text/tertiary as
    // in 291:994 (295:1732, 296:2782); Undo disabled
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 2400, 2));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    const pdf::PDFSegmentedControl* segments = gui->getPresetControl()->getSegmentedControl();
    const QWidget* label = shell->getToolActions()->findChild<QWidget*>(QStringLiteral("toolLabel-preset"));
    QVERIFY(label);
    QVERIFY(label->isEnabled());
    QVERIFY(segments->isEnabled());

    shell->confirm();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    QVERIFY(!segments->isEnabled());
    QVERIFY2(label->isEnabled(), "295:1732 keeps text/tertiary");

    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }, 120000));
    AppToolShellFixture::settle();
    QVERIFY2(!segments->isEnabled(), "296:2761 has the disabled segments");
    QVERIFY(label->isEnabled());

    // The dialog never gives the tool back: only Discard ends this state here
    shell->getResultDialog()->getDiscardButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
}

void AppToolCompressTest::reviewNotReducedCalloutLines()
{
    // callout-amber 295:2621: the title of two lines of 16 (32 high, head 295:2622), gap 8,
    // the body of three lines of 17, gap 8, the button 36; paddings 10 / 12 + the border
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writeCompressed(fixture.folder(), fixture.path(QStringLiteral("small.pdf")));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best-quality"));
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::NotReduced);

    pdf::PDFAppToolCallout* callout = gui->getNotReducedCallout();
    const QRect calloutRect = H::inWindow(callout);
    QCOMPARE(calloutRect.left(), 1137);
    QCOMPARE(calloutRect.width(), 287);
    QCOMPARE(H::inWindow(gui->getMeasuredCard()).bottom() + 1 + 16, calloutRect.top());
    QCOMPARE(callout->getTitleLabel()->height(), 32);
    QCOMPARE(H::inWindow(callout->getTitleLabel()).top() - calloutRect.top(), 11);
    QCOMPARE(H::inWindow(callout->getTitleLabel()).left(), 1174);
    QCOMPARE(H::inWindow(callout->getTextLabel()).top() - calloutRect.top(), 51);
    QCOMPARE(callout->getTextLabel()->height(), 51);
    QCOMPARE(H::inWindow(gui->getTrySmallerButton()).top() - calloutRect.top(), 110);
    QCOMPARE(gui->getTrySmallerButton()->height(), 36);
    QCOMPARE(calloutRect.height(), 159);

    // section-What changes follows at gap 16
    QCOMPARE(H::inWindow(gui->getChangesHeader()).top(), calloutRect.bottom() + 1 + 16);
}

void AppToolCompressTest::reviewPermissionRefusedIsNotMeasuring()
{
    // Nothing is measured for a document, which refuses the tool: the card does not say so
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = fixture.open(H::writeEncryptedPhoto(fixture.folder(), fixture.path(QStringLiteral("Refused.pdf")), QString(), 0xFFFFFFFCu & ~0x0008u));
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(shell->isPermissionRefused());
    AppToolShellFixture::settle();
    QVERIFY(!shell->getRunner()->isPreviewPending());
    QVERIFY2(!gui->getMeasuringCard()->getTitleLabel()->text().startsWith(QStringLiteral("Measuring")), qPrintable(gui->getMeasuringCard()->getTitleLabel()->text()));
    QVERIFY2(gui->getResultHeader()->getHintLabel()->text() != QStringLiteral("measuring"), "Nothing runs");
    if (shell->getPermissionDialog())
    {
        shell->getPermissionDialog()->reject();
    }
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewMeasuredCardDirection()
{
    // The sizes are data as the rows below them: "807.2 kB → 44.4 kB" in this order, at the
    // leading edge of the card also right-to-left
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    QApplication::setLayoutDirection(Qt::RightToLeft);
    const QScopeGuard directionGuard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); });
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    AppToolShellFixture::settle();

    pdf::PDFAppToolMeasuredCard* card = gui->getMeasuredCard();
    QCOMPARE(card->layoutDirection(), Qt::RightToLeft);
    QCOMPARE(card->getBeforeLabel()->layoutDirection(), Qt::LeftToRight);
    QCOMPARE(card->getAfterLabel()->layoutDirection(), Qt::LeftToRight);
    QVERIFY2(H::inWindow(card->getBeforeLabel()).right() < H::inWindow(card->getAfterLabel()).left(), "The size before stays before the size after");

    // The characters of a size are in their order: the painter of a widget has the direction of the
    // application, a plain text there would read "kB 807.2". The same pixels as left-to-right.
    const QImage beforeRightToLeft = card->getBeforeLabel()->grab().toImage();
    const QImage afterRightToLeft = card->getAfterLabel()->grab().toImage();
    QApplication::setLayoutDirection(Qt::LeftToRight);
    const QImage beforeLeftToRight = card->getBeforeLabel()->grab().toImage();
    const QImage afterLeftToRight = card->getAfterLabel()->grab().toImage();
    QCOMPARE(beforeRightToLeft, beforeLeftToRight);
    QCOMPARE(afterRightToLeft, afterLeftToRight);
    QApplication::setLayoutDirection(Qt::RightToLeft);
    AppToolShellFixture::settle();
    QCOMPARE(H::inWindow(card->getAfterLabel()).right() + 1, H::inWindow(card).right() + 1 - pdf::PDFAppToolMeasuredCard::PADDING);

    // The card itself stays in the panel at the leading side of the window
    QCOMPARE(H::inWindow(shell->getRightPanel()).left(), 0);
}

void AppToolCompressTest::reviewCustomPanelRightToLeft()
{
    // K13 mirrored: the place of the frame from the trailing edge of the segments; the panel
    // does not leave the canvas
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);

    // The direction of the application: a popup window takes it, not the one of a window
    QApplication::setLayoutDirection(Qt::RightToLeft);
    const QScopeGuard directionGuard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); });
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QCOMPARE(panel->layoutDirection(), Qt::RightToLeft);

    const pdf::PDFSegmentedControl* segments = gui->getPresetControl()->getSegmentedControl();
    const QRect anchor(segments->mapToGlobal(QPoint(0, 0)), segments->size());
    const QWidget* canvas = tab->getView()->getCanvasArea();
    const int canvasLeft = canvas->mapToGlobal(QPoint(0, 0)).x();
    const int left = panel->getPanelLeft(anchor, pdf::PDFAppToolCompressCustomPanel::PANEL_WIDTH);

    // The frame: 509 of 1440 at the left of the segments at 333 mirrors to the right edge of
    // the panel 176 from the right edge of the segments
    const int expected = anchor.right() + 1 - pdf::PDFAppToolCompressCustomPanel::SEGMENTS_OFFSET - pdf::PDFAppToolCompressCustomPanel::PANEL_WIDTH;
    QCOMPARE(left, qBound(canvasLeft, expected, canvasLeft + canvas->width() - pdf::PDFAppToolCompressCustomPanel::PANEL_WIDTH));
    QVERIFY(left >= canvasLeft);
    QVERIFY(left + pdf::PDFAppToolCompressCustomPanel::PANEL_WIDTH <= canvasLeft + canvas->width());

    // At most 1 px by the width of the text "Preset", as in the direction of the frame
    const int fromWindow = left - fixture.window->mapToGlobal(QPoint(0, 0)).x();
    QVERIFY2(qAbs(fromWindow - (1440 - 509 - pdf::PDFAppToolCompressCustomPanel::PANEL_WIDTH)) <= 1, qPrintable(QString::number(fromWindow)));
}

void AppToolCompressTest::reviewCustomPanelNoRingAfterClick()
{
    // 294:1340 has no ring: a panel opened by the mouse does not show the focus of the
    // keyboard on its first field; the keys still start there
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    const QList<QToolButton*> buttons = gui->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>();
    QTest::mouseClick(buttons[3], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCustomPanel()->isVisible(); }));
    AppToolShellFixture::settle();
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QWidget* combo = panel->getControl(QStringLiteral("targetDpi"))->getComboBox();
    QVERIFY(!combo->hasFocus());
    QVERIFY(panel->hasFocus());

    const QColor focus = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderFocus);
    const auto countRingPixels = [&combo, &focus]()
    {
        const QImage image = AppToolShellFixture::render(combo, 1.0);
        int ringPixels = 0;
        for (int x = 8; x < image.width() - 8; ++x)
        {
            ringPixels += image.pixelColor(x, 0) == focus ? 1 : 0;
        }
        return ringPixels;
    };
    QCOMPARE(countRingPixels(), 0);

    // Tab starts at the first control with its ring; Esc closes the panel only
    QTest::keyClick(panel, Qt::Key_Tab);
    AppToolShellFixture::settle();
    QVERIFY(combo->hasFocus());
    QVERIFY(countRingPixels() > 0);
    QTest::keyClick(combo, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);

    // By the keyboard (Enter on the segment) the first control has the focus at once
    QToolButton* custom = buttons[3];
    custom->setFocus(Qt::TabFocusReason);
    QTest::keyClick(custom, Qt::Key_Return);
    QVERIFY(AppToolShellFixture::waitFor([panel]() { return panel->isVisible(); }));
    AppToolShellFixture::settle();
    QVERIFY(combo->hasFocus());
    panel->hide();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewHandleInTabOrder()
{
    // The handle is reachable by Tab between the canvas and the panel of the tool, and it
    // takes no focus by a click on the canvas
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    AppToolShellFixture::settle();
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QVERIFY(overlay->isVisible());
    fixture.window->activateWindow();

    // Going forward from the first part of the tool reaches the handle in a bounded walk,
    // and the walk goes on behind it
    QWidget* start = gui->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>().front();
    QWidget* widget = start;
    QWidget* afterHandle = nullptr;
    bool isReached = false;
    for (int i = 0; i < 400 && widget; ++i)
    {
        QWidget* next = widget->nextInFocusChain();
        if (isReached && next->isVisible() && next->isEnabled() && (next->focusPolicy() & Qt::TabFocus))
        {
            afterHandle = next;
            break;
        }
        isReached = isReached || next == overlay;
        widget = next;
        if (widget == start)
        {
            break;
        }
    }
    QVERIFY2(isReached, "The handle is in the focus chain");
    QVERIFY2(afterHandle && afterHandle != overlay, "The handle is no trap of the keyboard");

    // Tab from the widget before the handle lands on it, the ring is drawn around the circle
    overlay->setFocus(Qt::TabFocusReason);
    QVERIFY(overlay->hasFocus());
    QTest::keyClick(overlay, Qt::Key_Tab);
    AppToolShellFixture::settle();
    QVERIFY2(!overlay->hasFocus(), "Tab leaves the handle");
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab, Qt::ShiftModifier);
    AppToolShellFixture::settle();
    QCOMPARE(QApplication::focusWidget(), overlay);

    // The ring of the keyboard around the circle inside of the margins of the widget
    const QColor focus = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderFocus);
    QVERIFY(PDFAppToolBeforeAfterOverlay::FOCUS_MARGIN >= 2);
    const QImage focused = overlay->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    int ringPixels = 0;
    for (int x = 0; x < focused.width(); ++x)
    {
        ringPixels += focused.pixelColor(x, focused.height() / 2) == focus ? 1 : 0;
    }
    QVERIFY2(ringPixels >= 2, qPrintable(QString::number(ringPixels)));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewOverlayRotatedAndTwoPages()
{
    // The clip and the pixmap under the rotation of the view and in a layout of two pages: the
    // pages keep their places, a page without a counterpart in the result stays the original
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSolidPages(fixture.path(QStringLiteral("red.pdf")), QColor(220, 0, 0), 2));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    H::waitMeasured(gui);
    AppToolShellFixture::settle();
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    pdf::PDFDocumentPointer blue = H::readDocument(H::writeSolid(fixture.path(QStringLiteral("blue.pdf")), QColor(0, 0, 220)));
    overlay->setResult(blue, QStringLiteral("AFTER · 1 MB"));

    pdf::PDFWidget* pdfWidget = tab->getView()->getPdfWidget();
    pdf::PDFDrawWidgetProxy* proxy = pdfWidget->getDrawWidgetProxy();
    QWidget* canvas = AppDocumentFixture::canvasWidget(tab->getView());
    const auto waitRendered = [&]()
    {
        return AppToolShellFixture::waitFor([&]()
        {
            canvas->repaint();
            return overlay->getFinishedRenderCount() > 0 && !overlay->isRenderPending();
        });
    };

    // Single page, the view rotated by 90: right of the line the blue of the result
    proxy->performOperation(pdf::PDFDrawWidgetProxy::RotateRight);
    proxy->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    canvas->repaint();
    QVERIFY(waitRendered());
    AppToolShellFixture::settle();
    QVERIFY(waitRendered());
    QImage image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int line = overlay->getLinePosition();
    const int y = image.height() / 2;
    QColor left = image.pixelColor(line - 20, y);
    QColor right = image.pixelColor(line + 20, y);
    QVERIFY2(left.red() > 180 && left.blue() < 60, qPrintable(left.name()));
    QVERIFY2(right.blue() > 180 && right.red() < 60, qPrintable(right.name()));
    proxy->performOperation(pdf::PDFDrawWidgetProxy::RotateLeft);

    // Two pages side by side: page 2 has no counterpart in the result of one page, so it
    // stays the original at both sides of the line; the first page right of the line is blue
    proxy->setPageLayout(pdf::PageLayout::TwoColumnLeft);
    proxy->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    canvas->repaint();
    AppToolShellFixture::settle();
    QVERIFY(waitRendered());
    AppToolShellFixture::settle();
    overlay->setLinePosition(canvas->width() / 4);
    canvas->repaint();
    image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int newLine = overlay->getLinePosition() + canvas->mapFrom(overlay->parentWidget(), QPoint(0, 0)).x();
    const QColor pageOneLeft = image.pixelColor(newLine - 12, y);
    const QColor pageOneRight = image.pixelColor(newLine + 12, y);
    const QColor pageTwo = image.pixelColor(image.width() * 3 / 4, y);
    QVERIFY2(pageOneLeft.red() > 180 && pageOneLeft.blue() < 60, qPrintable(pageOneLeft.name()));
    QVERIFY2(pageOneRight.blue() > 180 && pageOneRight.red() < 60, qPrintable(pageOneRight.name()));
    QVERIFY2(pageTwo.red() > 180 && pageTwo.blue() < 60, qPrintable(pageTwo.name()));
    QCOMPARE(overlay->getRenderedPages(), std::vector<pdf::PDFInteger>({ 0 }));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewOverlayDevicePixelRatio()
{
    // At a fractional scale the result is rendered in the pixels of the screen, bounded by
    // MAXIMUM_PIXELS: the line and the page of the result stay where the frame has them
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("red.pdf")), QColor(220, 0, 0)));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    H::waitMeasured(gui);
    AppToolShellFixture::settle();
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    overlay->setResult(H::readDocument(H::writeSolid(fixture.path(QStringLiteral("blue.pdf")), QColor(0, 0, 220))), QStringLiteral("AFTER · 1 MB"));
    QWidget* canvas = AppDocumentFixture::canvasWidget(tab->getView());
    QVERIFY(AppToolShellFixture::waitFor([&]()
    {
        canvas->repaint();
        return overlay->getFinishedRenderCount() > 0 && !overlay->isRenderPending();
    }));

    for (const qreal ratio : { 1.0, 1.5, 2.0 })
    {
        QImage image(canvas->size() * ratio, QImage::Format_RGB32);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::black);
        canvas->render(&image);
        const int line = overlay->getLinePosition() + canvas->mapFrom(overlay->parentWidget(), QPoint(0, 0)).x();
        const int y = int(canvas->height() / 2 * ratio);
        const QColor left = image.pixelColor(int((line - 4) * ratio), y);
        const QColor right = image.pixelColor(int((line + 4) * ratio), y);
        const QColor accent = image.pixelColor(int(line * ratio), y);
        QVERIFY2(left.red() > 180 && left.blue() < 60, qPrintable(QStringLiteral("%1 at %2").arg(left.name()).arg(ratio)));
        QVERIFY2(right.blue() > 180 && right.red() < 60, qPrintable(QStringLiteral("%1 at %2").arg(right.name()).arg(ratio)));
        QCOMPARE(accent, pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgAccent));
    }
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewSameResultKeepsPixmaps()
{
    // The same result again (a language change, a cached preview): no new rendering, only a new
    // text of the chip; a real change of the zoom asks for one
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("red.pdf")), QColor(220, 0, 0)));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    H::waitMeasured(gui);
    AppToolShellFixture::settle();
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    const pdf::PDFDocumentPointer blue = H::readDocument(H::writeSolid(fixture.path(QStringLiteral("blue.pdf")), QColor(0, 0, 220)));
    overlay->setResult(blue, QStringLiteral("AFTER · 1 MB"));
    QWidget* canvas = AppDocumentFixture::canvasWidget(tab->getView());
    QVERIFY(AppToolShellFixture::waitFor([&]()
    {
        canvas->repaint();
        return overlay->getFinishedRenderCount() == 1 && !overlay->isRenderPending();
    }));

    overlay->setResult(blue, QStringLiteral("AFTER · 2 MB"));
    canvas->repaint();
    QVERIFY2(!overlay->isRenderPending(), "The same document needs no rendering");
    QCOMPARE(overlay->getFinishedRenderCount(), 1);
    QCOMPARE(overlay->getRenderedPages(), std::vector<pdf::PDFInteger>({ 0 }));
    QCOMPARE(overlay->getAfterText(), QStringLiteral("AFTER · 2 MB"));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::measuringKeepsTheEventLoop()
{
    // The owner's remark 1: the images run in the threads of the policy, nothing of the thread of the user
    // interface waits for them - while they measure, when another preset comes and when they are stopped
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("many.pdf")), 900, 24));
    QVERIFY(tab);

    qint64 longestStop = 0;
    QElapsedTimer loopTimer;
    loopTimer.start();
    QTimer loopProbe;
    loopProbe.setInterval(5);
    QObject::connect(&loopProbe, &QTimer::timeout, [&]()
    {
        longestStop = qMax(longestStop, loopTimer.restart());
    });

    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    loopTimer.restart();
    loopProbe.start();

    // Another preset during the measuring: the panel says it at once
    QElapsedTimer timer;
    timer.start();
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    const qint64 changeTime = timer.elapsed();
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measuring);

    QVERIFY(H::waitMeasured(gui));
    const qint64 measureTime = timer.elapsed();
    loopProbe.stop();
    qInfo().noquote() << QStringLiteral("A change of the preset %1 ms, the measuring %2 ms, the longest stop of the event loop %3 ms").arg(changeTime).arg(measureTime).arg(longestStop);
    QVERIFY2(changeTime < 500 * AppToolShellFixture::getTimeoutFactor(), qPrintable(QString::number(changeTime)));
    QVERIFY2(longestStop < 1000 * AppToolShellFixture::getTimeoutFactor(), qPrintable(QString::number(longestStop)));
}

void AppToolCompressTest::reviewMeasuringBenchmark()
{
    // PDF4QT_COMPRESS_BENCHMARK=<files separated by ;>: a real file - time of the measuring,
    // the longest stop of the event loop, the time until the workers of a stopped measuring
    // end and more tabs measuring at once. The numbers are printed (review of check F1).
    using H = CompressGuiHelper;
    const QString files = qEnvironmentVariable("PDF4QT_COMPRESS_BENCHMARK");
    if (files.isEmpty())
    {
        QSKIP("PDF4QT_COMPRESS_BENCHMARK is not set");
    }

    // "core:<file>": the tool of the core alone in this thread, without the application: the
    // commit charge at each progress of its steps
    if (files.startsWith(QStringLiteral("core:")))
    {
        const QString filePath = files.mid(5);
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();
        pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
        const pdf::PDFDocumentPointer document(new pdf::PDFDocument(reader.readFromBuffer(data)));
        qInfo("BENCHMARK core: read %lld MB", H::getCurrentMemoryMB());

        // The calls of the computation, recorded one at a time (pdftest::TestProgressRecorder)
        class Listener : public pdftest::TestProgressRecorder
        {
        public:
            Listener()
            {
                onText = [this](const QString& stepText)
                {
                    const int percentage = percentages.back();
                    const QString step = stepText.section(QStringLiteral(" · "), 0, 0);
                    const qint64 memory = CompressGuiHelper::getCurrentMemoryMB();
                    if (step != lastStep || percentage / 5 != lastPercentage / 5)
                    {
                        qInfo("BENCHMARK core: %lld ms %d %% %s: %lld MB", timer.elapsed(), percentage, qPrintable(stepText), memory);
                        lastStep = step;
                        lastPercentage = percentage;
                    }
                };
            }

            QString lastStep;
            int lastPercentage = 0;
            QElapsedTimer timer;
        };

        // PDF4QT_COMPRESS_BENCHMARK_IMAGES=<n>: the first n images one by one - where the time of an image goes
        if (const int imageCount = qEnvironmentVariableIntValue("PDF4QT_COMPRESS_BENCHMARK_IMAGES"); imageCount > 0)
        {
            pdf::PDFImageOptimizer::Settings settings = pdf::PDFDocumentToolCompress::createSettings({ { QStringLiteral("preset"), qEnvironmentVariable("PDF4QT_COMPRESS_BENCHMARK_PRESET", QStringLiteral("balanced")) } }).images;
            pdf::PDFImageCompressor compressor;
            pdf::PDFImageCompressor::Environment environment(document.data());
            int printed = 0;
            QElapsedTimer stageTimer;
            stageTimer.start();
            compressor.visitImages(environment, compressor.collectImagePlacements(environment), [&](pdf::PDFImageCompressor::ImageStatistics statistics)
            {
                if (printed++ >= imageCount)
                {
                    return;
                }
                const qint64 decodeTime = stageTimer.restart();
                pdf::PDFImageOptimizer::ImageInfo info;
                info.reference = statistics.reference;
                info.image = statistics.image;
                info.minimalDpi = statistics.minimalDpi;
                const pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(info, settings);
                const qint64 planTime = stageTimer.restart();
                pdf::PDFRenderErrorReporterDummy reporter;
                const pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(info.image, plan.encodeOptions, &reporter);
                const qint64 encodeTime = stageTimer.restart();
                const pdf::PDFStream* original = document->getObjectByReference(statistics.reference).getStream();
                qInfo("BENCHMARK image %d: %d x %d format %d, %s, dpi %.0f, target %d x %d, mode %d, algorithm %d: decode %lld ms, plan %lld ms, encode %lld ms, %lld -> %lld bytes",
                      printed, info.image.width(), info.image.height(), int(info.image.format()), qPrintable(pdf::PDFImageOptimizer::readFilterName(document.data(), original->getDictionary())), statistics.minimalDpi.x(),
                      plan.targetSize.width(), plan.targetSize.height(), int(plan.resolvedColorMode), int(plan.algorithm), decodeTime, planTime, encodeTime, qint64(original->getContent()->size()), qint64(stream.getContent()->size()));
            });
            return;
        }

        // PDF4QT_COMPRESS_BENCHMARK_PRESET: the preset of the run, Smaller file by default
        Listener listener;
        listener.timer.start();
        pdf::PDFDocumentToolCompress tool;
        QVariantMap parameters;
        const QString benchmarkPreset = qEnvironmentVariable("PDF4QT_COMPRESS_BENCHMARK_PRESET", QStringLiteral("smaller-file"));
        parameters.insert(QStringLiteral("preset"), benchmarkPreset);

        // PDF4QT_COMPRESS_BENCHMARK_STRATEGY=single: the strategy Single thread (under a limit of the memory
        // of the process: one image at a time needs less memory)
        const bool isSingleThread = qEnvironmentVariable("PDF4QT_COMPRESS_BENCHMARK_STRATEGY") == QLatin1String("single");
        if (isSingleThread)
        {
            pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::SingleThreaded);
        }
        const auto restoreStrategy = qScopeGuard([]() { pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded); });

        const pdf::PDFDocumentToolResult result = tool.runPreview(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, data, filePath, false)), parameters, nullptr, &listener);
        qInfo("BENCHMARK core: %s%s status %d, time %lld ms, size %lld -> %lld, peak %lld MB, now %lld MB: %s", qPrintable(benchmarkPreset), isSingleThread ? " in one thread" : "", int(result.status), listener.timer.elapsed(),
              result.data.value(QStringLiteral("sizeBefore")).toInteger(), result.data.value(QStringLiteral("sizeAfter")).toInteger(), H::getPeakMemoryMB(), H::getCurrentMemoryMB(), qPrintable(result.message));
        return;
    }

    // "images:<n>" is a generated document of n different small images on pages of 100 of them
    QStringList paths = files.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QTemporaryDir generatedFolder;
    for (QString& path : paths)
    {
        if (path.startsWith(QStringLiteral("images:")))
        {
            path = H::writeManyImages(generatedFolder.filePath(QStringLiteral("images.pdf")), path.mid(7).toInt());
        }
    }

    for (const QString& path : paths)
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        QElapsedTimer openTimer;
        openTimer.start();
        pdf::PDFAppDocumentTab* tab = fixture.open(path);
        QVERIFY(tab);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, 600000));
        const qint64 openTime = openTimer.elapsed();
        AppToolShellFixture::settle();
        const qint64 peakAfterOpen = H::getPeakMemoryMB();

        // The longest time, for which the event loop has not run
        qint64 longestStop = 0;
        QElapsedTimer loopTimer;
        loopTimer.start();
        QTimer loopProbe;
        loopProbe.setInterval(5);
        PDFAppToolGUICompress* probedGui = nullptr;
        std::map<QString, qint64> memoryOfStep;
        QString currentStep;
        QObject::connect(&loopProbe, &QTimer::timeout, [&]()
        {
            longestStop = qMax(longestStop, loopTimer.elapsed());
            loopTimer.restart();

            // The memory by the step of the card (its text without the numbers)
            if (probedGui)
            {
                memoryOfStep[currentStep] = qMax(memoryOfStep[currentStep], H::getCurrentMemoryMB());
            }
        });
        loopProbe.start();

        QElapsedTimer timer;
        timer.start();
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        PDFAppToolGUICompress* gui = H::gui(shell);
        QVERIFY(gui);
        probedGui = gui;
        QObject::connect(shell->getRunner(), &pdf::PDFDocumentToolRunner::previewProgress, [&currentStep](int, const QString& stepText)
        {
            currentStep = stepText.section(QStringLiteral(" · "), 0, 0);
        });
        const qint64 enterTime = timer.elapsed();

        // A change of the preset at about a half of the measuring stops the old one
        QVERIFY(AppToolShellFixture::waitFor([gui, &timer]()
        {
            const int percentage = gui->getMeasuringCard()->getBar()->value();
            return percentage >= 20 || gui->getMeasureState() != PDFAppToolGUICompress::MeasureState::Measuring || timer.elapsed() > 300000;
        }, 400000));
        const qint64 firstPart = timer.elapsed();
        const int activeBefore = QThreadPool::globalInstance()->activeThreadCount();
        shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));

        timer.restart();
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getMeasureState() != PDFAppToolGUICompress::MeasureState::Measuring; }, 1200000));
        const qint64 measuringTime = timer.elapsed();
        const QString after = gui->getMeasuredCard()->getAfterLabel()->text();
        const qint64 stopDuringMeasuring = longestStop;
        const qint64 peakAfterMeasuring = H::getPeakMemoryMB();
        probedGui = nullptr;
        for (const auto& [step, memory] : memoryOfStep)
        {
            qInfo("BENCHMARK step \"%s\": %lld MB", qPrintable(step), memory);
        }
        longestStop = 0;

        // Overlay: the time of a rendering after a zoom (a CMS and a font cache per job)
        PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
        QWidget* canvas = AppDocumentFixture::canvasWidget(tab->getView());
        qint64 renderTime = -1;
        if (gui->getMeasureState() == PDFAppToolGUICompress::MeasureState::Measured)
        {
            const int renderCount = overlay->getFinishedRenderCount();
            canvas->repaint();
            timer.restart();
            QVERIFY(AppToolShellFixture::waitFor([&]()
            {
                canvas->repaint();
                return overlay->getFinishedRenderCount() > renderCount && !overlay->isRenderPending();
            }, 120000));
            renderTime = timer.elapsed() - PDFAppToolBeforeAfterOverlay::RENDER_DELAY;

            // What a job of the overlay costs: the context (a color management per job) and
            // the page of the original and of the result
            pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getPdfWidget()->getDrawWidgetProxy();
            const pdf::PDFCMSSettings cmsSettings = proxy->getCMSManager()->getSettings();
            const pdf::PDFDocumentToolResult* preview = shell->getRunner()->getPreview();
            const pdf::PDFDocumentToolOutputDocument* output = preview ? pdf::PDFDocumentToolRunner::findChangedDocument(*preview) : nullptr;
            const qint64 contextOriginal = H::measureContext(tab->getDocument()->getDocument().get(), cmsSettings, proxy->getFeatures());
            const qint64 contextResult = output ? H::measureContext(output->document.data(), cmsSettings, proxy->getFeatures()) : -1;
            qInfo("BENCHMARK %s: context and page of the overlay, original %lld ms, result %lld ms", qPrintable(QFileInfo(path).fileName()), contextOriginal, contextResult);
        }

        // The tool closed during a measuring: nobody waits, the worker ends by itself
        shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("custom"));
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getMeasuringCard()->getBar()->value() > 5 || gui->getMeasureState() != PDFAppToolGUICompress::MeasureState::Measuring; }, 300000));
        timer.restart();
        shell->cancel();
        const qint64 closeTime = timer.elapsed();
        timer.restart();
        AppToolShellFixture::waitFor([]() { return QThreadPool::globalInstance()->activeThreadCount() == 0; }, 600000);
        const qint64 workerEndTime = timer.elapsed();

        qInfo("BENCHMARK %s: size %.1f MB, open %lld ms, enter %lld ms, to 20 %% %lld ms (active %d), measuring Smaller file %lld ms -> %s, "
              "longest stop of the event loop %lld ms, rendering %lld ms, close %lld ms, worker ended after %lld ms, peak open %lld MB, measured %lld MB, end %lld MB",
              qPrintable(QFileInfo(path).fileName()), double(QFileInfo(path).size()) / 1048576.0, openTime, enterTime, firstPart, activeBefore,
              measuringTime, qPrintable(after), stopDuringMeasuring, renderTime, closeTime, workerEndTime, peakAfterOpen, peakAfterMeasuring, H::getPeakMemoryMB());
        QVERIFY2(closeTime < 2000, "Closing the tool does not wait for the workers");
        loopProbe.stop();
    }

    // More tabs measure at once: the shared pool holds them, no more threads than the pool
    if (paths.size() > 1)
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        std::vector<PDFAppToolGUICompress*> guis;
        for (const QString& path : paths)
        {
            pdf::PDFAppDocumentTab* tab = fixture.open(path);
            QVERIFY(tab);
            QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, 600000));
            AppToolShellFixture::settle();
            PDFAppToolGUICompress* gui = H::gui(H::openCompress(fixture, tab));
            QVERIFY(gui);
            guis.push_back(gui);
        }

        int mostActive = 0;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(AppToolShellFixture::waitFor([&]()
        {
            mostActive = qMax(mostActive, QThreadPool::globalInstance()->activeThreadCount());
            for (const PDFAppToolGUICompress* gui : guis)
            {
                if (gui->getMeasureState() == PDFAppToolGUICompress::MeasureState::Measuring)
                {
                    return false;
                }
            }
            return true;
        }, 1800000));
        qInfo("BENCHMARK %d tabs at once: %lld ms, most active threads of the pool %d of %d, peak %lld MB", int(guis.size()), timer.elapsed(), mostActive,
              QThreadPool::globalInstance()->maxThreadCount(), H::getPeakMemoryMB());
        QVERIFY(mostActive <= QThreadPool::globalInstance()->maxThreadCount());
    }
}

void AppToolCompressTest::lockedRefused()
{
    // T3: Unavailable by the catalog: the tool is not entered for a locked document
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    AppToolShellFixture::settle();
    QVERIFY(!fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(!AppToolShellFixture::getShell(tab));
    tab->getDocument()->setLocked(false);
    AppToolShellFixture::settle();

    // Locked while open: the tool closes
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    QVERIFY(H::gui(shell));
    tab->getDocument()->setLocked(true);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
}

void AppToolCompressTest::leakAndThemes_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("ratio");
    QTest::addColumn<bool>("rightToLeft");
    for (const char* theme : { "light", "dark" })
    {
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QTest::addRow("%s-%.1f", theme, ratio) << QString::fromLatin1(theme) << ratio << false;
        }
        QTest::addRow("%s-rtl", theme) << QString::fromLatin1(theme) << 1.0 << true;
    }
}

void AppToolCompressTest::leakAndThemes()
{
    using H = CompressGuiHelper;
    QFETCH(QString, theme);
    QFETCH(qreal, ratio);
    QFETCH(bool, rightToLeft);

    // The fixture sets the light scheme of its context: the theme of the row comes after it
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFThemeManager::getInstance()->setTheme(theme);
    QCOMPARE(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface).lightness() < 128, theme == QStringLiteral("dark"));
    if (rightToLeft)
    {
        fixture.window->setLayoutDirection(Qt::RightToLeft);
    }
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    AppToolShellFixture::settle();

    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getRightPanel(), ratio)), 0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getToolbar(), ratio)), 0);
    gui->openCustomPanel();
    QVERIFY(gui->getCustomPanel()->isVisible());
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(gui->getCustomPanel(), ratio)), 0);
    gui->getCustomPanel()->hide();

    // The original stays at the left in the right-to-left direction too: the line of a
    // handle moved to the left has more of the result, not less
    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    const int line = overlay->getLinePosition();
    overlay->setFocus();
    QTest::keyClick(overlay, Qt::Key_Left);
    QCOMPARE(overlay->getLinePosition(), line - PDFAppToolBeforeAfterOverlay::KEY_STEP);
    QVERIFY(overlay->getBeforeChipRect().left() < overlay->getAfterChipRect().left());

    shell->cancel();
    AppToolShellFixture::settle();
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolCompressTest::minimalWindow()
{
    // RP2C2: the segments alone need 730 px, at 720 x 480 only the icon of Primary, then
    // Cancel hidden (Esc stays); the actions never overlap, the panel lies inside
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar->getCollapseLevel() > 0);
    QVERIFY(toolbar->getConfirmButton()->isVisible());
    QCOMPARE(toolbar->getConfirmText(), QStringLiteral("Compress"));
    QVERIFY(toolbar->width() <= fixture.window->width());
    QVERIFY(H::inWindow(toolbar->getConfirmButton()).right() < fixture.window->width());
    QVERIFY(!toolbar->getTitleLabel()->isVisible());
    if (toolbar->getCancelButton()->isVisible())
    {
        QVERIFY(H::inWindow(gui->getPresetControl()->getSegmentedControl()).right() < H::inWindow(toolbar->getCancelButton()).left());
    }
    const pdf::PDFSegmentedControl* segments = gui->getPresetControl()->getSegmentedControl();
    QVERIFY(segments->isVisible());
    QCOMPARE(segments->width(), 364);
    QVERIFY(H::inWindow(segments).right() < H::inWindow(toolbar->getConfirmButton()).left());
    QVERIFY(fixture.window->width() == 720);

    // The canvas of 190 px has no room for the chips in one line
    QVERIFY(H::waitMeasured(gui));
    AppToolShellFixture::settle();
    const PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QVERIFY(overlay->hasResult());
    QVERIFY(!overlay->getAfterChipRect().intersects(overlay->getBeforeChipRect()));

    gui->openCustomPanel();
    QVERIFY(gui->getCustomPanel()->isVisible());
    const QRect panelRect = gui->getCustomPanel()->getPanelRect().translated(gui->getCustomPanel()->pos());
    const QWidget* canvas = tab->getView()->getCanvasArea();
    QVERIFY(panelRect.left() >= canvas->mapToGlobal(QPoint(0, 0)).x() || panelRect.width() > canvas->width());
    gui->getCustomPanel()->hide();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::languageChange()
{
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("smaller-file"));
    QVERIFY(H::waitMeasured(gui));
    gui->openCustomPanel();
    QVERIFY(gui->getCustomPanel()->isVisible());
    QPointer<pdf::PDFAppToolCompressCustomPanel> oldPanel = gui->getCustomPanel();

    pdftest::CompressTestTranslator translator;
    QApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QVERIFY(AppToolShellFixture::waitFor([gui, oldPanel]() { return gui->getCustomPanel() != oldPanel.data(); }));
    AppToolShellFixture::settle();

    // The panels are new with the texts of the translator; the value lives in the model
    QVERIFY(gui->getResultHeader()->getTitleLabel()->text().startsWith(QStringLiteral("[T] ")));
    QVERIFY(gui->getChangesHeader()->getTitleLabel()->text().startsWith(QStringLiteral("[T] ")));
    QCOMPARE(gui->getPresetControl()->getSegmentedControl()->getCurrentIndex(), 0);
    QCOMPARE(H::preset(shell), QStringLiteral("smaller-file"));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    QVERIFY(gui->getMeasuredCard()->isVisible());
    QVERIFY(gui->getBeforeAfterOverlay()->getBeforeText().startsWith(QStringLiteral("[T] ")));
    QApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
}

void AppToolCompressTest::panelsTranslatedSchemaEnglish()
{
    // The owner, 2026-11-02: the user sees the translated texts of the parameters, the
    // assistant the English ones of the same definitions
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    gui->openCustomPanel();
    QVERIFY(gui->getCustomPanel()->isVisible());
    QPointer<pdf::PDFAppToolCompressCustomPanel> oldPanel = gui->getCustomPanel();

    const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(H::TOOL_ID);
    QVERIFY(tool);
    const QByteArray schemaBefore = QJsonDocument(pdf::PDFDocumentToolParameters::toJsonSchema(tool->getParameters())).toJson(QJsonDocument::Compact);

    pdftest::CompressTestTranslator translator;
    QApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QVERIFY(AppToolShellFixture::waitFor([gui, oldPanel]() { return gui->getCustomPanel() != oldPanel.data(); }));
    AppToolShellFixture::settle();

    // The segments of the preset in the panel of the left side
    pdf::PDFSegmentedControl* segments = gui->getPresetControl()->getSegmentedControl();
    QVERIFY(segments);
    QStringList segmentTexts;
    for (const QAbstractButton* button : segments->findChildren<QAbstractButton*>())
    {
        segmentTexts << button->text();
    }
    QVERIFY2(segmentTexts.contains(QStringLiteral("[T] Smaller file")), qPrintable(segmentTexts.join(QLatin1Char('|'))));
    QVERIFY2(segmentTexts.contains(QStringLiteral("[T] Balanced")), qPrintable(segmentTexts.join(QLatin1Char('|'))));
    QVERIFY2(segmentTexts.contains(QStringLiteral("[T] Best quality")), qPrintable(segmentTexts.join(QLatin1Char('|'))));

    // The custom panel: the labels of the rows, the radio buttons of the goal, the check boxes
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QVERIFY(panel);
    QStringList labels;
    for (const QLabel* label : panel->findChildren<QLabel*>())
    {
        labels << label->text();
    }
    QVERIFY2(labels.contains(QStringLiteral("[T] Downsample to")), qPrintable(labels.join(QLatin1Char('|'))));
    QVERIFY2(labels.contains(QStringLiteral("[T] JPEG quality")), qPrintable(labels.join(QLatin1Char('|'))));

    QStringList buttons;
    for (const QAbstractButton* button : panel->findChildren<QAbstractButton*>())
    {
        buttons << button->text();
    }
    QVERIFY2(buttons.contains(QStringLiteral("[T] Prefer quality")), qPrintable(buttons.join(QLatin1Char('|'))));
    QVERIFY2(buttons.contains(QStringLiteral("[T] Minimum size")), qPrintable(buttons.join(QLatin1Char('|'))));
    QVERIFY2(buttons.contains(QStringLiteral("[T] Preserve transparency")), qPrintable(buttons.join(QLatin1Char('|'))));
    QVERIFY2(buttons.contains(QStringLiteral("[T] Keep original image if the new one is larger")), qPrintable(buttons.join(QLatin1Char('|'))));
    QVERIFY2(buttons.contains(QStringLiteral("[T] Clean up and recompress the file structure")), qPrintable(buttons.join(QLatin1Char('|'))));

    // The schema of the same tool at the same time
    const QByteArray schema = QJsonDocument(pdf::PDFDocumentToolParameters::toJsonSchema(tool->getParameters())).toJson(QJsonDocument::Compact);
    QApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();

    QCOMPARE(schema, schemaBefore);
    QVERIFY(!schema.contains("[T]"));
    QVERIFY(schema.contains("\"title\":\"Downsample to\""));
    QVERIFY(schema.contains("Prefer quality"));
}

void AppToolCompressTest::accessibility()
{
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
    PDFAppToolGUICompress* gui = H::gui(shell);
    QVERIFY(gui && H::waitMeasured(gui));
    AppToolShellFixture::settle();

    PDFAppToolBeforeAfterOverlay* overlay = gui->getBeforeAfterOverlay();
    QVERIFY(!overlay->accessibleName().isEmpty());
    QVERIFY(overlay->accessibleDescription().contains(QStringLiteral("AFTER · ")));
    QCOMPARE(overlay->focusPolicy(), Qt::TabFocus);

    gui->openCustomPanel();
    pdf::PDFAppToolCompressCustomPanel* panel = gui->getCustomPanel();
    QVERIFY(!panel->accessibleName().isEmpty());
    QCOMPARE(panel->getControl(QStringLiteral("targetDpi"))->getComboBox()->accessibleName(), QStringLiteral("Downsample to"));
    QCOMPARE(panel->getControl(QStringLiteral("jpegQuality"))->getSlider()->accessibleName(), QStringLiteral("JPEG quality"));
    panel->hide();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::deletedDuringMeasuring()
{
    // The tab goes while the preview and the overlay work: nobody waits, nothing is left
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf")), 2400));
    QVERIFY(tab);
    QPointer<PDFDocumentToolShell> shell = H::openCompress(fixture, tab);
    QVERIFY(shell);
    QVERIFY(H::gui(shell));
    QPointer<PDFAppToolBeforeAfterOverlay> overlay = H::gui(shell)->getBeforeAfterOverlay();
    QVERIFY(overlay);
    QElapsedTimer timer;
    timer.start();
    delete tab;
    QVERIFY2(timer.elapsed() < 1000 * AppToolShellFixture::getTimeoutFactor(), "Nobody waits for the worker");
    QVERIFY(!shell);
    QVERIFY(!overlay);
    AppToolShellFixture::settle();
    QVERIFY(fixture.window->isVisible());
}

// --------------------------------------------------------------------- the overlay

/// The tool opened on a red page with an overlay with a blue result of its own
class OverlayScene
{
public:
    explicit OverlayScene(QSize size = QSize(1440, 960)) :
        fixture(size)
    {
        using H = CompressGuiHelper;
        tab = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("red.pdf")), QColor(220, 0, 0)));
        if (!tab)
        {
            return;
        }
        shell = H::openCompress(fixture, tab);
        gui = H::gui(shell);
        if (!gui)
        {
            return;
        }
        H::waitMeasured(gui);
        AppToolShellFixture::settle();
        overlay = gui->getBeforeAfterOverlay();
        blue = H::readDocument(H::writeSolid(fixture.path(QStringLiteral("blue.pdf")), QColor(0, 0, 220)));
    }

    /// Waits until the result is rendered
    bool waitRendered(int count = 1)
    {
        return AppToolShellFixture::waitFor([this, count]()
        {
            overlay->repaint();
            AppDocumentFixture::canvasWidget(tab->getView())->repaint();
            return overlay->getFinishedRenderCount() >= count && !overlay->isRenderPending();
        });
    }

    QImage canvasImage() const
    {
        return AppDocumentFixture::canvasWidget(tab->getView())->grab().toImage().convertToFormat(QImage::Format_RGB32);
    }

    /// Point of the page in the canvas: inside of the page at the height of the handle
    int pageY() const
    {
        return overlay->getHandleRect().center().y();
    }

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUICompress* gui = nullptr;
    PDFAppToolBeforeAfterOverlay* overlay = nullptr;
    pdf::PDFDocumentPointer blue;
};

void AppToolCompressTest::overlayLeftOriginalRightResult()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    scene.overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    QVERIFY(scene.waitRendered());
    QCOMPARE(scene.overlay->getRenderedPages(), std::vector<pdf::PDFInteger>({ 0 }));

    QImage image = scene.canvasImage();
    const int line = scene.overlay->getLinePosition();
    const int y = scene.pageY() + 60;
    const QColor left = image.pixelColor(line - 60, y);
    const QColor right = image.pixelColor(line + 60, y);
    QVERIFY2(left.red() > 180 && left.blue() < 60, qPrintable(left.name()));
    QVERIFY2(right.blue() > 180 && right.red() < 60, qPrintable(right.name()));

    // split-line 293:1346: 2 px bg/accent, drawn over the pages
    const QColor accent = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgAccent);
    QCOMPARE(image.pixelColor(line - 1, y), accent);
    QCOMPARE(image.pixelColor(line, y), accent);

    // The chips: icon/strong with their text, above the handle
    const QColor strong = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::IconStrong);
    QCOMPARE(image.pixelColor(scene.overlay->getBeforeChipRect().left() + 5, scene.overlay->getBeforeChipRect().center().y()), strong);
    QCOMPARE(image.pixelColor(scene.overlay->getAfterChipRect().right() - 5, scene.overlay->getAfterChipRect().center().y()), strong);

    // The line moved: the page below the handle is the result to the left of the old place
    scene.overlay->setLinePosition(line - 120);
    AppToolShellFixture::settle();
    image = scene.canvasImage();
    const QColor moved = image.pixelColor(line - 60, y);
    QVERIFY2(moved.blue() > 180 && moved.red() < 60, qPrintable(moved.name()));
    QCOMPARE(scene.overlay->getFinishedRenderCount(), 1);

    // Without a result: the original at both sides, no line
    scene.overlay->setResult(pdf::PDFDocumentPointer(), QString());
    AppToolShellFixture::settle();
    image = scene.canvasImage();
    QVERIFY(image.pixelColor(line + 60, y).red() > 180);
    QVERIFY(image.pixelColor(scene.overlay->getLinePosition(), y).red() > 180);
    QVERIFY(!scene.overlay->isVisible());
    QVERIFY(scene.overlay->getRenderedPages().empty());
}

void AppToolCompressTest::overlayFollowsZoomAndScroll()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    scene.overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    QVERIFY(scene.waitRendered(1));
    const int line = scene.overlay->getLinePosition();

    // A zoom: scaled meanwhile (the blue stays at once), rendered again after the rest
    pdf::PDFDrawWidgetProxy* proxy = scene.tab->getView()->getPdfWidget()->getDrawWidgetProxy();
    proxy->zoom(proxy->getZoom() * 1.5);
    AppDocumentFixture::canvasWidget(scene.tab->getView())->repaint();
    QImage image = scene.canvasImage();
    QVERIFY2(scene.overlay->isRenderPending(), "A zoom asks for a new rendering");
    const QColor scaled = image.pixelColor(line + 60, scene.pageY());
    QVERIFY2(scaled.blue() > 180 && scaled.red() < 60, qPrintable(scaled.name()));
    QVERIFY(scene.waitRendered(2));
    QCOMPARE(scene.overlay->getFinishedRenderCount(), 2);

    // A scroll: the same
    proxy->scrollByPixels(QPoint(0, -40));
    AppDocumentFixture::canvasWidget(scene.tab->getView())->repaint();
    QVERIFY(scene.overlay->isRenderPending());
    QVERIFY(scene.waitRendered(3));
    image = scene.canvasImage();
    const QColor scrolled = image.pixelColor(line + 60, scene.pageY());
    QVERIFY2(scrolled.blue() > 180 && scrolled.red() < 60, qPrintable(scrolled.name()));
}

void AppToolCompressTest::overlayDropsOlderResult()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    using H = CompressGuiHelper;
    pdf::PDFDocumentPointer green = H::readDocument(H::writeSolid(scene.fixture.path(QStringLiteral("green.pdf")), QColor(0, 200, 0)));
    scene.overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    AppDocumentFixture::canvasWidget(scene.tab->getView())->repaint();

    // The next result right away: only its pages are shown
    scene.overlay->setResult(green, QStringLiteral("AFTER · 2 MB"));
    QVERIFY(scene.overlay->getRenderedPages().empty());
    QCOMPARE(scene.overlay->getFinishedRenderCount(), 0);
    QVERIFY(scene.waitRendered(1));
    const QColor color = scene.canvasImage().pixelColor(scene.overlay->getLinePosition() + 60, scene.pageY());
    QVERIFY2(color.green() > 150 && color.blue() < 60 && color.red() < 60, qPrintable(color.name()));
}

void AppToolCompressTest::overlayHandleKeysAndMouse()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    PDFAppToolBeforeAfterOverlay* overlay = scene.overlay;
    overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    AppToolShellFixture::settle();
    const int line = overlay->getLinePosition();

    overlay->setFocus();
    QTest::keyClick(overlay, Qt::Key_Right);
    QCOMPARE(overlay->getLinePosition(), line + PDFAppToolBeforeAfterOverlay::KEY_STEP);
    QTest::keyClick(overlay, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(overlay->getLinePosition(), line + PDFAppToolBeforeAfterOverlay::KEY_STEP - 1);
    QTest::keyClick(overlay, Qt::Key_Home);
    QCOMPARE(overlay->getLinePosition(), PDFAppToolBeforeAfterOverlay::HANDLE_SIZE / 2);
    QTest::keyClick(overlay, Qt::Key_End);
    QCOMPARE(overlay->getLinePosition(), overlay->parentWidget()->width() - PDFAppToolBeforeAfterOverlay::HANDLE_SIZE / 2);

    // The handle follows the mouse
    overlay->setLinePosition(line);
    AppToolShellFixture::settle();
    const QPoint center = overlay->rect().center();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, center);
    QMouseEvent move(QEvent::MouseMove, QPointF(center + QPoint(40, 0)), overlay->mapToGlobal(QPointF(center + QPoint(40, 0))), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &move);
    QVERIFY2(qAbs(overlay->getLinePosition() - (line + 40)) <= 1, qPrintable(QString::number(overlay->getLinePosition())));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, overlay->rect().center());

    // The handle is the only input over the canvas: the place next to it reaches the canvas
    const QPoint beside = overlay->mapToParent(overlay->rect().center()) + QPoint(60, 0);
    QWidget* canvasArea = overlay->parentWidget();
    QVERIFY(canvasArea->childAt(beside) != overlay);
}

void AppToolCompressTest::overlayChipsAtTheEdges()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    PDFAppToolBeforeAfterOverlay* overlay = scene.overlay;
    overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    AppToolShellFixture::settle();
    const QWidget* canvasArea = overlay->parentWidget();

    // A chip stays inside of the canvas when the line is at its edge, the chips never cover
    // each other: the chip AFTER goes below the chip BEFORE
    overlay->setLinePosition(0);
    QCOMPARE(overlay->getBeforeChipRect().left(), 0);
    QVERIFY(!overlay->getAfterChipRect().intersects(overlay->getBeforeChipRect()));
    QCOMPARE(overlay->getAfterChipRect().top(), PDFAppToolBeforeAfterOverlay::CHIP_TOP + PDFAppToolBeforeAfterOverlay::CHIP_HEIGHT + PDFAppToolBeforeAfterOverlay::CHIP_GAP);
    overlay->setLinePosition(canvasArea->width());
    QCOMPARE(overlay->getAfterChipRect().right() + 1, canvasArea->width());
    QVERIFY(!overlay->getAfterChipRect().intersects(overlay->getBeforeChipRect()));
    overlay->setLinePosition(canvasArea->width() / 2);
    QCOMPARE(overlay->getAfterChipRect().top(), PDFAppToolBeforeAfterOverlay::CHIP_TOP);

    // A resize of the window keeps the fraction of the canvas, the handle goes with the line
    overlay->setLinePosition(canvasArea->width() / 4);
    const double fraction = double(overlay->getLinePosition()) / double(canvasArea->width());
    const int oldWidth = canvasArea->width();
    scene.fixture.window->resize(1240, 960);
    AppToolShellFixture::settle();
    QVERIFY(canvasArea->width() < oldWidth);
    QVERIFY2(qAbs(overlay->getLinePosition() - fraction * canvasArea->width()) <= 1.0, qPrintable(QString::number(overlay->getLinePosition())));
    QCOMPARE(overlay->geometry().center().x(), overlay->getHandleRect().center().x());
    QCOMPARE(overlay->getHandleRect().center().x() + 1, overlay->getLinePosition());
}

void AppToolCompressTest::overlayDeletedWhileRendering()
{
    OverlayScene scene;
    QVERIFY(scene.overlay);
    scene.overlay->setResult(scene.blue, QStringLiteral("AFTER · 1 MB"));
    AppDocumentFixture::canvasWidget(scene.tab->getView())->repaint();
    QVERIFY(scene.overlay->isRenderPending());
    QTest::qWait(PDFAppToolBeforeAfterOverlay::RENDER_DELAY + 10);

    // A running job or one waiting for the rest: the tool closes at once
    QElapsedTimer timer;
    timer.start();
    scene.shell->close();
    QVERIFY(timer.elapsed() < 1000 * AppToolShellFixture::getTimeoutFactor());
    AppToolShellFixture::settle();
    QTest::qWait(50);
    QVERIFY(scene.fixture.window->isVisible());

    // The canvas of the document has no part of the overlay after it
    AppDocumentFixture::canvasWidget(scene.tab->getView())->repaint();
    const QImage image = scene.canvasImage();
    const QColor color = image.pixelColor(image.width() / 2 + 60, image.height() / 2);
    QVERIFY2(color.red() > 180, qPrintable(color.name()));
}

// ------------------------------------------------------------------ the start page

void AppToolCompressTest::startPageGeometry()
{
    // 276:1737 at 1440 wide
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 1605));
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();

    // The columns of tool-start-grid: 802 + 24 + 340 at 242 and 1068
    QCOMPARE(H::inWindow(page->getColumns()->getMainColumn()).width(), 802);
    QCOMPARE(H::inWindow(page->getColumns()->getMainColumn()).left(), 242);
    QCOMPARE(H::inWindow(page->getColumns()->getSideColumn()).width(), 340);
    QCOMPARE(H::inWindow(page->getColumns()->getSideColumn()).left(), 1068);

    // Header: the breadcrumbs, the icon 44, the title and the Tag after it
    pdf::PDFAppWorkspaceHeader* header = fixture.window->findChild<pdf::PDFAppWorkspaceHeader*>();
    QVERIFY(header);
    QVERIFY(header->getTag()->isVisible());
    QCOMPARE(header->getTag()->text(), QStringLiteral("Optimize"));

    // title-row 276:1980: the icon 44 at 242, the texts at 300, the Tag 14 px behind them
    const QWidget* titleWidget = header->findChild<QWidget*>(QStringLiteral("headerTitle"));
    QVERIFY(titleWidget && titleWidget->isVisible());
    QCOMPARE(H::inWindow(header->getIconBadge()).left(), 242);
    QCOMPARE(H::inWindow(header->getIconBadge()).width(), 44);
    QCOMPARE(H::inWindow(titleWidget).left(), 300);

    // title-col 276:1987: the title 89 - 123, gap 2, the subtitle 125 - 142, the icon 93.5
    QCOMPARE(H::inWindow(titleWidget).top(), 89);
    QCOMPARE(titleWidget->height(), 34);
    QCOMPARE(H::inWindow(header->getSubtitleLabel()).top(), 125);
    QCOMPARE(header->getSubtitleLabel()->height(), 17);
    QVERIFY2(qAbs(H::inWindow(header->getIconBadge()).top() - 93) <= 1, qPrintable(QString::number(H::inWindow(header->getIconBadge()).top())));
    QVERIFY2(titleWidget->width() > 100, "The title has the width of its text");
    QCOMPARE(H::inWindow(header->getTag()).left(), H::inWindow(titleWidget).right() + 1 + 14);

    // The Tag follows the texts (x 521: the subtitle has 207 px), not the free space of the row
    QVERIFY2(qAbs(H::inWindow(header->getTag()).left() - 521) <= 3, qPrintable(QString::number(H::inWindow(header->getTag()).left())));
    QCOMPARE(header->getTag()->height(), 21);
    const QList<pdf::PDFAppBreadcrumbItem> items = page->getHeaderBreadcrumbs();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0].text, QStringLiteral("Tools"));
    QCOMPARE(items[1].text, QStringLiteral("Optimize"));
    QCOMPARE(items[1].route.toString(), QStringLiteral("/tools?category=optimize"));
    QCOMPARE(items[2].text, QStringLiteral("Compress"));
    QCOMPARE(page->getHeaderIcon().iconName, QStringLiteral("minimize-2"));
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Compress"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Reduce the file size in one step"));

    // Tutorial 340 x 369 with its place kept and disabled (Q5)
    QCOMPARE(page->getTutorialCard()->width(), 340);
    QCOMPARE(page->getTutorialCard()->height(), 369);
    QVERIFY(page->getWatchButton()->isVisible());
    QVERIFY(!page->getWatchButton()->isEnabled());

    // The tokens of 279:2028 and 279:2035, also disabled
    AppToolShellFixture::settle();
    const QImage card = page->getTutorialCard()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(card.pixelColor(card.width() / 2, 0), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Violet, pdf::PDFAccentHueColors::Role::BorderStrong)));
    QCOMPARE(card.pixelColor(card.width() / 2, 6), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Violet, pdf::PDFAccentHueColors::Role::Background)));
    QWidget* thumbnail = page->findChild<QWidget*>(QStringLiteral("toolTutorialThumbnail"));
    QVERIFY(thumbnail);
    QCOMPARE(thumbnail->width(), 310);
    QCOMPARE(thumbnail->height(), 170);
    QCOMPARE(H::inWindow(thumbnail).left() - H::inWindow(page->getTutorialCard()).left(), 15);
    const QImage picture = thumbnail->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(picture.pixelColor(20, 20), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgMuted));
    QCOMPARE(picture.pixelColor(picture.width() / 2, 0), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderDivider));
    QCOMPARE(page->getWatchButton()->text(), QStringLiteral("Watch on YouTube"));

    // Read the manual: the link row of 50 in the card of padding 6
    pdf::PDFLinkRow* link = page->getManualLink();
    QCOMPARE(link->getTitle(), QStringLiteral("Read the manual"));
    QCOMPARE(link->getSubtitle(), QStringLiteral("Compress – options and presets explained"));
    QCOMPARE(link->height(), 50);
    QCOMPARE(link->width(), 326);

    // Without a document: the group hidden, Open a file… and the drop zone stay
    pdf::PDFAppToolDocumentChooser* chooser = page->getChooser();
    QVERIFY(chooser->getRows().empty());
    QVERIFY(!chooser->getGroupLabel()->isVisible());
    QVERIFY(chooser->getOpenButton()->isVisible());
    QCOMPARE(chooser->getDropZone()->height(), 43);
    QCOMPARE(chooser->getOpenButton()->height(), 36);
}

void AppToolCompressTest::startPageTexts()
{
    // The texts of the frame, read again after the corrections of the owner (Q6)
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 1605));
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();

    const QStringList labels = [page]()
    {
        QStringList texts;
        for (const QLabel* label : page->findChildren<QLabel*>())
        {
            texts << label->text();
        }
        return texts;
    }();
    QVERIFY(labels.contains(QStringLiteral("Choose a document")));
    QVERIFY(labels.contains(QStringLiteral("Compress works on one PDF · pick an open document or open a file")));
    QVERIFY(labels.contains(QStringLiteral("The tool opens in the document’s tab. Nothing changes until you press Compress; a locked document cannot be compressed.")));
    QVERIFY(labels.contains(QStringLiteral("How Compress works")));
    QVERIFY(labels.contains(QStringLiteral("from the user manual")));
    QVERIFY(labels.contains(QStringLiteral("Watch the tutorial")));
    QVERIFY(labels.contains(QStringLiteral("At a glance")));
    QVERIFY(labels.contains(QStringLiteral("Often used next")));

    // At a glance 279:2050
    const pdf::PDFKeyValueList* facts = page->getFacts();
    QCOMPARE(H::rowKeys(facts), QStringList({ QStringLiteral("Input"), QStringLiteral("Result"), QStringLiteral("Open document"), QStringLiteral("Locked document"), QStringLiteral("Signatures"), QStringLiteral("Command line") }));
    QCOMPARE(facts->getValue(0), QStringLiteral("One open PDF"));
    QCOMPARE(facts->getValue(1), QStringLiteral("A smaller PDF"));
    QCOMPARE(facts->getValue(2), QStringLiteral("Your choice"));
    QCOMPARE(facts->getValue(3), QStringLiteral("Not available"));
    QCOMPARE(facts->getValue(4), QStringLiteral("Lost (copy only)"));
    QCOMPARE(facts->getValue(5), QStringLiteral("pdftool optimize"));

    // Often used next from the suggestions of O21: the three defaults of the catalog in its order, the
    // same three as Then continue with
    QCOMPARE(page->createOftenNextText(), QStringLiteral("Merge, Protect with Password, Sign with Certificate"));
    QCOMPARE(page->getOftenNextText()->text(), page->createOftenNextText());

    // The manual of the topic: K9, K10, the row of CMYK and Lab; the name of a preset of the
    // table of 280:2051 in one line (the column of the names is kept)
    QVERIFY(page->getManualView()->height() > 0);
    const pdf::PDFAppHelpTopic topic = pdf::PDFAppHelpCatalog::getBuiltin()->getLocalizedTopic(QStringLiteral("tool-optimize-compress"), QStringLiteral("en"));
    QCOMPARE(topic.categoryId, QStringLiteral("tools"));
    QVERIFY(topic.markdown.contains(QStringLiteral("Your settings in the Custom panel")));
    QVERIFY(!topic.markdown.contains(QStringLiteral("Your settings in Optimize Images")));
    QVERIFY(topic.markdown.contains(QStringLiteral("pdftool optimize --opt-images --opt-images-color-dpi 150 --opt-images-gray-dpi 150 input.pdf")));
    QVERIFY(topic.markdown.contains(QStringLiteral("CMYK and Lab images are converted to RGB")));
    QVERIFY(!topic.markdown.contains(QStringLiteral("What takes up space")));
    QCOMPARE(page->getHelpTopic(), QStringLiteral("tool-optimize-compress"));
}

void AppToolCompressTest::chooserRows()
{
    // 277:13689: 784 x 49; a working copy Tag, a locked document Disabled with Locked (Q4)
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 1605));
    pdf::PDFAppDocumentTab* first = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("Project_Report.pdf")), Qt::red));
    pdf::PDFAppDocumentTab* second = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("Invoice.pdf")), Qt::green));
    QVERIFY(first && second);

    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    pdf::PDFAppToolDocumentChooser* chooser = page->getChooser();
    QVERIFY(chooser->getGroupLabel()->isVisible());
    QCOMPARE(chooser->getGroupLabel()->text(), QStringLiteral("OPEN DOCUMENTS"));
    std::vector<pdf::PDFAppToolDocumentRow*> rows = chooser->getRows();
    QCOMPARE(rows.size(), size_t(2));
    QCOMPARE(rows[0]->width(), 784);
    QCOMPARE(rows[0]->height(), 49);
    QCOMPARE(rows[0]->text(), QStringLiteral("Project_Report.pdf"));
    QVERIFY(rows[0]->getDetails().contains(QStringLiteral("1 page")));
    QVERIFY(rows[0]->getDetails().contains(QDir::toNativeSeparators(fixture.folder())));
    QVERIFY(!rows[0]->getTag());
    QVERIFY(rows[0]->isEnabled());

    // Locked: the registry tells it
    first->getDocument()->setLocked(true);
    QVERIFY(AppToolShellFixture::waitFor([chooser]() { return !chooser->getRows().empty() && !chooser->getRows().front()->isEnabled(); }));
    AppToolShellFixture::settle();
    rows = chooser->getRows();
    QVERIFY(rows[0]->getTag());
    QCOMPARE(rows[0]->getTag()->text(), QStringLiteral("Locked"));
    QVERIFY(rows[1]->isEnabled());

    // A click on a locked row chooses nothing
    QSignalSpy spy(chooser, &pdf::PDFAppToolDocumentChooser::documentChosen);
    QTest::mouseClick(rows[0], Qt::LeftButton);
    QCOMPARE(spy.size(), 0);

    // A row of the action at the trailing side, right-to-left mirrored
    const QRect actionRect = rows[1]->getActionRect();
    QCOMPARE(actionRect.right() + 1, 784 - 10 - 16 - 12);
    rows[1]->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(rows[1]->getActionRect().left(), 10 + 16 + 12);
    rows[1]->setLayoutDirection(Qt::LeftToRight);

    // No leak color in the rows, hovered or not
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(chooser, 1.0)), 0);
    first->getDocument()->setLocked(false);
    AppToolShellFixture::settle();
}

void AppToolCompressTest::chooserChoosesDocument()
{
    // The tool opens in the tab of the document; F13: only the documents of this window
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();

    std::vector<pdf::PDFAppToolDocumentRow*> rows = page->getChooser()->getRows();
    QCOMPARE(rows.size(), size_t(1));
    QTest::mouseClick(rows.front(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return AppToolShellFixture::getShell(tab) != nullptr; }));
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QVERIFY(H::gui(AppToolShellFixture::getShell(tab)));
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();

    // Another window of the context shows no document of this window
    std::unique_ptr<pdf::PDFAppMainWindow> otherWindow(new pdf::PDFAppMainWindow(fixture.context.get()));
    pdf::PDFAppToolDocumentChooser chooser(fixture.context.get(), H::TOOL_ID, otherWindow.get());
    chooser.refresh();
    QVERIFY(chooser.getRows().empty());
    QVERIFY(!chooser.getGroupLabel()->isVisibleTo(&chooser));
}

void AppToolCompressTest::chooserKeyboard()
{
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("report.pdf")), Qt::red));
    QVERIFY(tab);
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    pdf::PDFAppToolDocumentRow* row = page->getChooser()->getRows().front();
    QCOMPARE(row->focusPolicy() & Qt::TabFocus, Qt::TabFocus);
    QVERIFY(!row->accessibleDescription().isEmpty());
    row->setFocus();

    // Enter chooses the row, as Space
    QSignalSpy spy(page->getChooser(), &pdf::PDFAppToolDocumentChooser::documentChosen);
    QTest::keyClick(row, Qt::Key_Return);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return AppToolShellFixture::getShell(tab) != nullptr; }));
    QCOMPARE(spy.size(), 1);
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::openFileGoesToTool()
{
    // Open a file… opens the document and the tool on it, in its tab
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("report.pdf")));
    pdf::PDFAppFileDialogs::setOpenFilesHandler([filePath](QWidget*, const QString&) { return QStringList{ filePath }; });
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    page->getChooser()->getOpenButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&fixture]()
    {
        pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
        return tab && AppToolShellFixture::getShell(tab) != nullptr;
    }));
    pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
    QCOMPARE(QFileInfo(tab->getDocument()->getFilePath()).fileName(), QStringLiteral("report.pdf"));
    QVERIFY(H::gui(AppToolShellFixture::getShell(tab)));

    // The file open already: its tab with the tool, not a second tab
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();
    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    const int tabs = fixture.window->findChildren<pdf::PDFAppDocumentTab*>().size();
    page->getChooser()->getOpenButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return AppToolShellFixture::getShell(tab) != nullptr; }));
    QCOMPARE(fixture.window->findChildren<pdf::PDFAppDocumentTab*>().size(), tabs);
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
}

void AppToolCompressTest::reviewOpenFileOfAnotherWindow()
{
    // F13: the chooser of a window lists only its documents; a file open in another window
    // activates its tab there and this window does not read it a second time
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("report.pdf")));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);

    // The first window shows Home over the document, the second one has the start page
    fixture.window->activateTab(fixture.window->getHomeTab());
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    std::unique_ptr<pdf::PDFAppMainWindow> otherWindow(new pdf::PDFAppMainWindow(fixture.context.get()));
    otherWindow->show();
    otherWindow->getNavigator()->navigate(pdf::PDFAppRoutes::tool(H::TOOL_ID), pdf::PDFAppNavigationMode::Push, otherWindow->getHomeTab());
    AppToolShellFixture::settle();
    PDFToolStartPage* page = qobject_cast<PDFToolStartPage*>(otherWindow->getCurrentTab()->getPage());
    QVERIFY(page);
    QVERIFY(page->getChooser()->getRows().empty());
    QVERIFY(!page->getChooser()->getGroupLabel()->isVisibleTo(page->getChooser()));

    const size_t documents = fixture.context->getDocumentRegistry()->getDocuments().size();
    pdf::PDFAppFileDialogs::setOpenFilesHandler([filePath](QWidget*, const QString&) { return QStringList{ filePath }; });
    page->getChooser()->getOpenButton()->click();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
    AppToolShellFixture::settle();

    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QCOMPARE(fixture.context->getDocumentRegistry()->getDocuments().size(), documents);
    QVERIFY2(otherWindow->findChildren<pdf::PDFAppDocumentTab*>().isEmpty(), "No second tab of the file");
    QVERIFY2(!AppToolShellFixture::getShell(tab), "The tab is activated, the tool does not start in the other window");
    QVERIFY(qobject_cast<PDFToolStartPage*>(otherWindow->getCurrentTab()->getPage()));
}

void AppToolCompressTest::reviewOpenFileLockedDocument()
{
    // T3 is the rule of a manual route: Open a file… of a locked document, open already, has
    // its tab with the reason, never Page Not Found over the document
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("report.pdf")));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    AppToolShellFixture::settle();

    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    pdf::PDFAppFileDialogs::setOpenFilesHandler([filePath](QWidget*, const QString&) { return QStringList{ filePath }; });
    page->getChooser()->getOpenButton()->click();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
    AppToolShellFixture::settle();

    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QVERIFY2(!fixture.notFoundPage(), qPrintable(fixture.currentRoute()));
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(fixture.currentRoute().left(10), QStringLiteral("/document/"));
    QVERIFY2(!fixture.currentRoute().contains(QStringLiteral("/tool/")), qPrintable(fixture.currentRoute()));
    QVERIFY2(tab->getView()->getStatusBar()->getMessage().contains(QStringLiteral("locked")), qPrintable(tab->getView()->getStatusBar()->getMessage()));
}

void AppToolCompressTest::reviewOpenFileWithPassword()
{
    // A file with a password opened from the start page: the tool waits until the document is
    // shown, nothing is measured before
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writeEncryptedPhoto(fixture.folder(), fixture.path(QStringLiteral("Secret.pdf")));
    QVERIFY(!filePath.isEmpty());
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    pdf::PDFAppFileDialogs::setOpenFilesHandler([filePath](QWidget*, const QString&) { return QStringList{ filePath }; });
    page->getChooser()->getOpenButton()->click();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);

    pdf::PDFAppDocumentTab* tab = nullptr;
    QVERIFY(AppToolShellFixture::waitFor([&]()
    {
        tab = fixture.currentDocumentTab();
        return tab && tab->getDocument() && tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired;
    }));
    QVERIFY(!AppToolShellFixture::getShell(tab));
    pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
    QVERIFY(screen);
    QTest::keyClicks(screen->getPasswordField(), AppDocumentSamples::getUserPassword());
    QTest::keyClick(screen->getPasswordField(), Qt::Key_Return);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return AppToolShellFixture::getShell(tab) != nullptr; }));
    PDFAppToolGUICompress* gui = H::gui(AppToolShellFixture::getShell(tab));
    QVERIFY(gui && H::waitMeasured(gui));
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUICompress::MeasureState::Measured);
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();
}

void AppToolCompressTest::reviewChooserLongPath()
{
    // 277:13694: "<folder> · 1,592 pages · 24.8 MB": the folder gives way, the pages and the
    // size stay whole in a row of a long path
    pdf::PDFAppToolDocumentRow row(QStringLiteral("id"), QStringLiteral("Report.pdf"),
                                   QStringLiteral("C:\\Users\\Jakub\\Documents\\Projects\\2026\\A very long name of a folder of the documents\\Another very long name of a folder\\And one more folder of the documents"),
                                   QStringLiteral("1,592 pages · 24.8 MB"), QString(), nullptr);
    row.resize(784, 49);
    const QString shown = row.getShownDetails();
    QVERIFY2(shown.endsWith(QStringLiteral(" · 1,592 pages · 24.8 MB")), qPrintable(shown));
    QVERIFY2(shown.contains(QChar(0x2026)), qPrintable(shown));

    // A short folder stays whole, as in the frame
    pdf::PDFAppToolDocumentRow shortRow(QStringLiteral("id2"), QStringLiteral("Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("1,592 pages · 24.8 MB"), QString(), nullptr);
    shortRow.resize(784, 49);
    QCOMPARE(shortRow.getShownDetails(), QStringLiteral("C:\\Users\\Jakub\\Documents · 1,592 pages · 24.8 MB"));

    // A row without a file (Edit a Copy): only the numbers
    pdf::PDFAppToolDocumentRow noFile(QStringLiteral("id3"), QStringLiteral("Report.pdf"), QString(), QStringLiteral("3 pages"), QString(), nullptr);
    noFile.resize(784, 49);
    QCOMPARE(noFile.getShownDetails(), QStringLiteral("3 pages"));
}

void AppToolCompressTest::dropGoesToTool()
{
    // A file dropped on the start page goes to the tool, on another page it only opens
    using H = CompressGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = H::writePhoto(fixture.path(QStringLiteral("report.pdf")));
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    QVERIFY(fixture.page<PDFToolStartPage>());
    AppToolShellFixture::settle();

    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(filePath) });
    QDragEnterEvent enter(QPoint(100, 100), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(AppToolShellFixture::waitFor([&fixture]()
    {
        pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
        return tab && AppToolShellFixture::getShell(tab) != nullptr;
    }));
    AppToolShellFixture::getShell(fixture.currentDocumentTab())->cancel();
    AppToolShellFixture::settle();

    // The page of a document has no tool of opened files
    const QString other = H::writeSolid(fixture.path(QStringLiteral("other.pdf")), Qt::green);
    mimeData.setUrls({ QUrl::fromLocalFile(other) });
    QDragEnterEvent secondEnter(QPoint(100, 100), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &secondEnter);
    QVERIFY(secondEnter.isAccepted());
    QDropEvent secondDrop(QPointF(100, 100), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &secondDrop);
    QVERIFY(secondDrop.isAccepted());
    QVERIFY2(AppToolShellFixture::waitFor([&fixture]()
    {
        pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
        return tab && tab->getDocument() && tab->getDocument()->isReady() && QFileInfo(tab->getDocument()->getFilePath()).fileName() == QStringLiteral("other.pdf");
    }), qPrintable(fixture.currentRoute() + QStringLiteral(" tabs ") + QString::number(fixture.window->findChildren<pdf::PDFAppDocumentTab*>().size())));
    QVERIFY(!AppToolShellFixture::getShell(fixture.currentDocumentTab()));
}

void AppToolCompressTest::noGuiStaysNotFound()
{
    // Chapter 8: a tool without its GUI stays Page Not Found; Compress with its GUI has its page
    AppToolShellFixture fixture(QSize(1440, 960));
    fixture.navigate(QStringLiteral("/tools/edit.page-numbers"));
    QVERIFY(fixture.notFoundPage());
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    QVERIFY(fixture.page<PDFToolStartPage>());

    // The start page refuses other parts of a route
    fixture.navigate(QStringLiteral("/tools/optimize.compress?x=1"));
    QVERIFY(!fixture.page<PDFToolStartPage>());
}

void AppToolCompressTest::startPageLanguageChange()
{
    AppToolShellFixture fixture(QSize(1440, 960));
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppToolDocumentChooser> oldChooser = page->getChooser();

    pdftest::CompressTestTranslator translator;
    QApplication::installTranslator(&translator);
    QVERIFY(AppToolShellFixture::waitFor([page, oldChooser]() { return page->getChooser() != oldChooser.data(); }));
    AppToolShellFixture::settle();
    QVERIFY(page->getManualLink()->getTitle().startsWith(QStringLiteral("[T] ")));
    QVERIFY(page->getWatchButton()->text().startsWith(QStringLiteral("[T] ")));
    QVERIFY(page->getChooser()->getOpenButton()->text().startsWith(QStringLiteral("[T] ")));
    QApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
}

void AppToolCompressTest::startPageDeletedWithDialog()
{
    // The window goes from the native dialog of Open a file…: the page checks its guard
    AppToolShellFixture fixture(QSize(1440, 960));
    fixture.navigate(QStringLiteral("/tools/optimize.compress"));
    PDFToolStartPage* page = fixture.page<PDFToolStartPage>();
    QVERIFY(page);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    QPointer<PDFToolStartPage> guard(page);
    pdf::PDFAppFileDialogs::setOpenFilesHandler([window](QWidget*, const QString&)
    {
        delete window.data();
        return QStringList{ QStringLiteral("x.pdf") };
    });
    page->getChooser()->getOpenButton()->click();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
    QVERIFY(!window);
    QVERIFY(!guard);
    AppToolShellFixture::settle();
}

void AppToolCompressTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> writes the frames under their ids, compared with Figma
    using H = CompressGuiHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* theme : { "light", "dark" })
    {
        const QString suffix = QString::fromLatin1(theme) == QStringLiteral("dark") ? QStringLiteral("-dark") : QString();
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        PDFAppToolGUICompress* gui = H::gui(shell);
        QVERIFY(gui);
        fixture.screenshot(QStringLiteral("compress-measuring") + suffix);
        QVERIFY(H::waitMeasured(gui));
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getBeforeAfterOverlay()->getFinishedRenderCount() > 0; }));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-291-994") + suffix);

        gui->openCustomPanel();
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-294-1340") + suffix);
        AppToolShellFixture::screenshotDialog(gui->getCustomPanel(), QStringLiteral("compress-294-1787") + suffix);
        gui->getCustomPanel()->hide();

        shell->confirm();
        pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-296-2761") + suffix);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("compress-296-2761-dialog") + suffix);

        // 316:7293: the target folder gone at the write
        const QString folder = fixture.path(QStringLiteral("gone"));
        QVERIFY(QDir().mkpath(folder));
        pdf::PDFAppFileDialogs::setFolderHandler([folder](QWidget*, const QString&) { return folder; });
        dialog->getBrowseButton()->click();
        pdf::PDFAppFileDialogs::setFolderHandler(nullptr);
        QVERIFY(QDir(folder).removeRecursively());
        dialog->getConfirmButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([dialog]() { return dialog->getWriteError() && dialog->getWriteError()->isVisible(); }));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("compress-316-7293-dialog") + suffix);
        dialog->getDiscardButton()->click();
        AppToolShellFixture::settle();

        fixture.navigate(QStringLiteral("/tools/optimize.compress"));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-start-276-1737") + suffix);
    }

    {
        // The whole start page at the size of its frame, with a document and with a hover
        pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
        AppToolShellFixture fixture(QSize(1440, 1605));
        QVERIFY(H::openReady(fixture, H::writeSolid(fixture.path(QStringLiteral("Project_Report.pdf")), Qt::white)));
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.navigate(QStringLiteral("/tools/optimize.compress"));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-start-276-1737-full"));
    }

    {
        // Right-to-left and the minimal window
        pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
        QApplication::setLayoutDirection(Qt::RightToLeft);
        const QScopeGuard directionGuard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); });
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        PDFAppToolGUICompress* gui = H::gui(shell);
        QVERIFY(gui && H::waitMeasured(gui));
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getBeforeAfterOverlay()->getFinishedRenderCount() > 0; }));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-291-994-rtl"));
        AppToolShellFixture::screenshotDialog(gui->getCustomPanel(), QStringLiteral("compress-294-1787-rtl"));
    }

    {
        AppToolShellFixture fixture(QSize(720, 480));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf"))));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        PDFAppToolGUICompress* gui = H::gui(shell);
        QVERIFY(gui && H::waitMeasured(gui));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-720x480"));
    }

    {
        pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
        AppToolShellFixture fixture(QSize(1440, 960));
        const QString filePath = H::writeCompressed(fixture.folder(), fixture.path(QStringLiteral("Project_Report.pdf")));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        PDFAppToolGUICompress* gui = H::gui(shell);
        QVERIFY(gui);
        shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best-quality"));
        QVERIFY(H::waitMeasured(gui));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-295-2182"));
    }

    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePhoto(fixture.path(QStringLiteral("Project_Report.pdf")), 2400, 2));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openCompress(fixture, tab);
        QVERIFY(shell);
        shell->confirm();
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("compress-295-1711"));
        shell->stop();
        AppToolShellFixture::settle();
    }
}

QTEST_MAIN(AppToolCompressTest)

#include "tst_apptoolcompresstest.moc"
