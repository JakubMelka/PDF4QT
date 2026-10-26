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

#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfapphistory.h"
#include "pdfappinstance.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppage.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfapptab.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptopbar.h"
#include "pdfappwindowfactory.h"
#include "pdfappwindowframe.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfaccenthue.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFocusFrame>
#include <QLabel>
#include <QLocalSocket>
#include <QLockFile>
#include <QMimeData>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QToolButton>
#include <QTextDocumentFragment>
#include <QToolTip>
#include <QVBoxLayout>
#include <QStyleHints>
#include <QtMath>
#include <QElapsedTimer>
#include <QtTest>

#ifdef PDF4QT_TEST_PLATFORM_THEME
#include "colorschemetesttheme.h"
#endif

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <thread>

/// Page of the tests: a title, a view state (an integer), hooks
class NavigationTestPage : public pdf::PDFAppPage
{
    Q_OBJECT

public:
    NavigationTestPage(pdf::PDFAppContext* context, QWidget* parent, const QString& title, bool isRouteAccepted) :
        pdf::PDFAppPage(context, parent),
        m_title(title),
        m_isRouteAccepted(isRouteAccepted)
    {

    }

    virtual bool setRoute(const pdf::PDFAppRoute& route) override
    {
        ++setRouteCount;
        if (!m_isRouteAccepted)
        {
            return false;
        }

        pdf::PDFAppPage::setRoute(route);
        if (onSetRoute)
        {
            onSetRoute(this);
        }
        return true;
    }

    virtual QString getTitle() const override { return m_title; }
    virtual QString getHeaderTitle() const override { return m_title; }
    virtual QBoxLayout* getBannerLayout() const override { return bannerLayout ? bannerLayout.data() : pdf::PDFAppPage::getBannerLayout(); }

    virtual QVariantMap saveViewState() const override
    {
        QVariantMap state;
        state[QStringLiteral("value")] = value;
        return state;
    }

    virtual void restoreViewState(const QVariantMap& state) override
    {
        value = state.value(QStringLiteral("value")).toInt();
        isRestored = true;
        if (onRestore)
        {
            onRestore(this);
        }
    }

    bool navigateTo(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode = pdf::PDFAppNavigationMode::Push)
    {
        return requestNavigation(route, mode);
    }

    bool replaceRouteTo(const pdf::PDFAppRoute& route)
    {
        return replaceRoute(route);
    }

    void requestBack()
    {
        Q_EMIT backRequested();
    }

    int value = 0;
    int setRouteCount = 0;
    bool isRestored = false;
    QPointer<QBoxLayout> bannerLayout;
    std::function<void(NavigationTestPage*)> onSetRoute;
    std::function<void(NavigationTestPage*)> onRestore;

private:
    QString m_title;
    bool m_isRouteAccepted = true;
};

/// Factory of the test page
class NavigationTestFactory : public pdf::PDFAppPageFactory
{
public:
    explicit NavigationTestFactory(const QString& title, bool isRouteAccepted = true) :
        m_title(title),
        m_isRouteAccepted(isRouteAccepted)
    {

    }

    virtual pdf::PDFAppPage* createPage(pdf::PDFAppContext* context, QWidget* parent) const override
    {
        ++createCount;
        if (onCreate && !onCreate())
        {
            return nullptr;
        }

        NavigationTestPage* page = new NavigationTestPage(context, parent, m_title, m_isRouteAccepted);
        page->onSetRoute = onSetRoute;
        page->onRestore = onRestore;
        lastPage = page;
        return page;
    }

    mutable int createCount = 0;
    mutable QPointer<NavigationTestPage> lastPage;
    std::function<bool()> onCreate;
    std::function<void(NavigationTestPage*)> onSetRoute;
    std::function<void(NavigationTestPage*)> onRestore;

private:
    QString m_title;
    bool m_isRouteAccepted = true;
};

/// Window, whose quitting the user cancels
class CancelingMainWindow : public pdf::PDFAppMainWindow
{
    Q_OBJECT

public:
    using pdf::PDFAppMainWindow::PDFAppMainWindow;

    int confirmCount = 0;

protected:
    virtual bool confirmClose() override
    {
        ++confirmCount;
        return false;
    }
};

/// Window, whose questions about unsaved changes run a function of the test
class QuestioningMainWindow : public pdf::PDFAppMainWindow
{
    Q_OBJECT

public:
    using pdf::PDFAppMainWindow::PDFAppMainWindow;

    std::function<bool()> onConfirm;

protected:
    virtual bool confirmClose() override
    {
        return onConfirm ? onConfirm() : true;
    }
};

/// Context (portable, own memory database) and the window of a test
class NavigationFixture
{
public:
    explicit NavigationFixture(bool createWindow = true, const QStringList& arguments = QStringList())
    {
        QStringList commandLineArguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
        commandLineArguments << arguments;
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(commandLineArguments, QDir::currentPath()));

        // The shell only: the tests register their own pages
        context->setPagesEnabled(false);

        pdf::PDFDatabaseOptions options;
        memoryName = QStringLiteral("/pdf4qt-navigation-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        options.memoryName = memoryName;
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        if (createWindow)
        {
            window = new pdf::PDFAppMainWindow(context.get());
            prepareWindow();
        }
    }

    ~NavigationFixture()
    {
        delete window.data();
        context.reset();
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    }

    void prepareWindow()
    {
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(1440, 977);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        window->activateWindow();
        (void)QTest::qWaitForWindowActive(window.data());
        QCoreApplication::processEvents();
    }

    pdf::PDFAppNavigator* navigator() const { return window ? window->getNavigator() : nullptr; }
    pdf::PDFAppTab* currentTab() const { return window ? window->getCurrentTab() : nullptr; }
    pdf::PDFAppPage* currentPage() const { return currentTab() ? currentTab()->getPage() : nullptr; }

    void navigate(const QString& text, pdf::PDFAppNavigationMode mode = pdf::PDFAppNavigationMode::Push)
    {
        navigator()->navigate(pdf::PDFAppRoute::parse(text), mode);
    }

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QString memoryName;

    static int s_counter;
};

int NavigationFixture::s_counter = 0;

/// Translator of the texts of the footer and of the banner ("[text]"), a language with longer texts
class FooterTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (QByteArray(context) == QByteArrayLiteral("pdf::PDFAppFooter") || QByteArray(context) == QByteArrayLiteral("pdf::PDFAppStorageBanner"))
        {
            return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
        }
        return QString();
    }

    virtual bool isEmpty() const override
    {
        return false;
    }
};

/// Translator of the buttons of Page Not Found only ("[text]", longer texts)
class NotFoundButtonTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray text(sourceText);
        if (QByteArray(context) == QByteArrayLiteral("pdf::PDFPageNotFoundPage") &&
            (text == QByteArrayLiteral("Back") || text == QByteArrayLiteral("Go to Home") || text.startsWith(QByteArrayLiteral("Search for "))))
        {
            return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
        }
        return QString();
    }

    virtual bool isEmpty() const override
    {
        return false;
    }
};

class AppNavigationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void routeParse_data();
    void routeParse();
    void routeRoundTrip();
    void routeCreate();
    void routeBuilders();
    void routeValues();
    void routeTable_data();
    void routeTable();
    void routeTableInvalid_data();
    void routeTableInvalid();
    void routeTableRegistration();
    void routeDescriptions();
    void history();

    void navigationAllRoutes_data();
    void navigationAllRoutes();
    void navigationInvalidRoutes_data();
    void navigationInvalidRoutes();
    void notFoundPage();
    void notFoundUntrustedText();
    void historyAndViewState();
    void navigationFromOwnSignal();
    void replaceRoute();
    void replaceRouteToolPages();
    void deletionDuringNavigation();
    void deletionInAnyOrder();
    void newTab();
    void helpTab();
    void externalLinks();
    void themeSwitch();
    void themeSwitchSystemScheme_data();
    void themeSwitchSystemScheme();
    void themeSwitchTestThemeRestored();
    void themeSwitchNativeScheme();
    void shortcutsFromDatabase();
    void searchField();
    void banners_data();
    void banners();
    void bannerFallbackTryAgain();
    void hitTest_data();
    void hitTest();
    void hitTestLogicalPosition_data();
    void hitTestLogicalPosition();
    void hitTestWindow();
    void maximizedClientRect();
    void nativeWindowsFrame();
    void windowControls();
    void closeAndCancel();
    void quitWithPendingRequests();
    void cancelQuitWhileLocked();
    void instanceRequests();
    void shellDesign();
    void footerAxis();
    void headerTexts();
    void minimumWindow();
    void notFoundNarrowWindow();
    void rightToLeftIcons();
    void navigationButtonsFilter();
    void scrollAreas();
    void openDocumentsRecent();
    void helpTabLimit();
    void breadcrumbParts();
    void fileDragOver();
    void bannerLayoutOfPage();
    void overlayPageScrollBar();
    void headerSearchWidth();
    void messageScrim();
    void paragraphOverhang();
    void screenshots();

private:
    static QString documentId() { return QStringLiteral("abcdefghijklmnopqrstuvwxyz"); }

    /// Reads the reply of the server of this thread (the event loop runs)
    static bool readReply(QLocalSocket& socket, int timeout, bool* ok);

    /// Picture of the window drawn at the device pixel ratio
    static QImage renderWindow(QWidget* window, qreal scale);

    /// Image of an icon (16 px) for a comparison
    static QImage iconImage(const QIcon& icon);

    /// Color scheme of the system before the style has requested any
    Qt::ColorScheme m_systemColorScheme = Qt::ColorScheme::Unknown;
};

bool AppNavigationTest::readReply(QLocalSocket& socket, int timeout, bool* ok)
{
    QByteArray buffer;
    QByteArray payload;
    QDeadlineTimer deadline(timeout);
    while (!deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        buffer += socket.readAll();
        if (pdf::PDFAppInstanceProtocol::readFrame(buffer, pdf::PDFAppInstanceProtocol::MAXIMUM_REPLY_SIZE, &payload) == pdf::PDFAppInstanceProtocol::FrameResult::Complete)
        {
            QString error;
            return pdf::PDFAppInstanceProtocol::decodeReply(payload, ok, &error);
        }
    }
    return false;
}

QImage AppNavigationTest::renderWindow(QWidget* window, qreal scale)
{
    QImage image(window->size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
    QPainter painter(&image);
    window->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return image;
}

QImage AppNavigationTest::iconImage(const QIcon& icon)
{
    return icon.pixmap(QSize(16, 16), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
}

void AppNavigationTest::initTestCase()
{
    m_systemColorScheme = QGuiApplication::styleHints()->colorScheme();
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppNavigationTest::cleanup()
{
    // Message boxes of a failed test do not block the next one
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<pdf::PDFMessageBox*>(widget))
        {
            widget->deleteLater();
        }
    }
    QCoreApplication::processEvents();
}

void AppNavigationTest::routeParse_data()
{
    using Error = pdf::PDFAppRoute::Error;

    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("error");
    QTest::addColumn<QStringList>("segments");
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("fragment");

    QTest::newRow("root") << "/" << int(Error::None) << QStringList() << QString() << QString();
    QTest::newRow("home") << "/home" << int(Error::None) << QStringList({ "home" }) << QString() << QString();
    QTest::newRow("settings anchor") << "/settings/security#signature-verification" << int(Error::None) << QStringList({ "settings", "security" }) << QString() << "signature-verification";
    QTest::newRow("query") << "/recent?filter=pinned&sort=name" << int(Error::None) << QStringList({ "recent" }) << "filter=pinned;sort=name" << QString();
    QTest::newRow("encoded space") << "/search?q=merge%20pdf" << int(Error::None) << QStringList({ "search" }) << "q=merge pdf" << QString();
    QTest::newRow("encoded utf8") << "/search?q=%C5%BElu%C5%A5ou%C4%8Dk%C3%BD" << int(Error::None) << QStringList({ "search" }) << QString::fromUtf8("q=žluťoučký") << QString();
    QTest::newRow("raw unicode") << QString::fromUtf8("/help/č") << int(Error::None) << QStringList({ QStringLiteral("help"), QString::fromUtf8("č") }) << QString() << QString();
    QTest::newRow("empty query") << "/home?" << int(Error::None) << QStringList({ "home" }) << QString() << QString();
    QTest::newRow("key only") << "/search?q" << int(Error::None) << QStringList({ "search" }) << "q=" << QString();

    QTest::newRow("empty") << "" << int(Error::Empty) << QStringList() << QString() << QString();
    QTest::newRow("relative") << "settings" << int(Error::NotAbsolute) << QStringList() << QString() << QString();
    QTest::newRow("https") << "https://pdf4qt.com" << int(Error::NotAbsolute) << QStringList() << QString() << QString();
    QTest::newRow("double slash") << "//home" << int(Error::EmptySegment) << QStringList() << QString() << QString();
    QTest::newRow("inner empty") << "/a//b" << int(Error::EmptySegment) << QStringList() << QString() << QString();
    QTest::newRow("trailing slash") << "/settings/" << int(Error::EmptySegment) << QStringList() << QString() << QString();
    QTest::newRow("dot dot") << "/settings/../home" << int(Error::DotSegment) << QStringList() << QString() << QString();
    QTest::newRow("dot") << "/./home" << int(Error::DotSegment) << QStringList() << QString() << QString();
    QTest::newRow("encoded dot dot") << "/settings/%2e%2E" << int(Error::DotSegment) << QStringList() << QString() << QString();
    QTest::newRow("null") << "/home%00" << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("null in query") << "/search?q=a%00b" << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("raw control") << QStringLiteral("/home\n") << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("raw null") << QString::fromUtf16(u"/ho\u0000me", 7) << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("encoded slash") << "/tools/a%2Fb" << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("encoded backslash") << "/tools/a%5Cb" << int(Error::InvalidCharacter) << QStringList() << QString() << QString();
    QTest::newRow("bad percent") << "/tools/%zz" << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("short percent") << "/tools/a%2" << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("percent at end") << "/tools/a%" << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("invalid utf8") << "/tools/%C3%28" << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("overlong utf8") << "/tools/%C0%AF" << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("lone surrogate") << QString(QStringLiteral("/tools/a") + QChar(0xD800)) << int(Error::InvalidEncoding) << QStringList() << QString() << QString();
    QTest::newRow("empty query item") << "/recent?filter=files&&sort=name" << int(Error::InvalidQuery) << QStringList() << QString() << QString();
    QTest::newRow("empty key") << "/recent?=files" << int(Error::InvalidQuery) << QStringList() << QString() << QString();
    QTest::newRow("digit key") << "/recent?1filter=files" << int(Error::InvalidQuery) << QStringList() << QString() << QString();
    QTest::newRow("duplicate key") << "/recent?filter=files&filter=pinned" << int(Error::DuplicateQueryKey) << QStringList() << QString() << QString();
    QTest::newRow("long fragment") << QStringLiteral("/settings/general#") + QString(200, QLatin1Char('a')) << int(Error::InvalidFragment) << QStringList() << QString() << QString();
    QTest::newRow("10 kB") << QStringLiteral("/tools/") + QString(10 * 1024, QLatin1Char('x')) << int(Error::TooLong) << QStringList() << QString() << QString();
    QTest::newRow("many segments") << QStringLiteral("/a").repeated(17) << int(Error::TooManySegments) << QStringList() << QString() << QString();
    QTest::newRow("many query items") << QStringLiteral("/search?") + QStringList({ "a=1", "b=1", "c=1", "d=1", "e=1", "f=1", "g=1", "h=1", "i=1", "j=1", "k=1", "l=1", "m=1", "n=1", "o=1", "p=1", "q=1" }).join(QLatin1Char('&'))
                                      << int(Error::InvalidQuery) << QStringList() << QString() << QString();
}

void AppNavigationTest::routeParse()
{
    QFETCH(QString, text);
    QFETCH(int, error);
    QFETCH(QStringList, segments);
    QFETCH(QString, query);
    QFETCH(QString, fragment);

    const pdf::PDFAppRoute route = pdf::PDFAppRoute::parse(text);
    QCOMPARE(int(route.getError()), error);
    QCOMPARE(route.isValid(), error == int(pdf::PDFAppRoute::Error::None));

    // The requested text is kept (shortened) for Page Not Found
    QVERIFY(route.getRequestedText().size() <= pdf::PDFAppRoute::MAXIMUM_LENGTH + 1);
    if (text.size() <= pdf::PDFAppRoute::MAXIMUM_LENGTH)
    {
        QCOMPARE(route.getRequestedText(), text);
    }

    if (!route.isValid())
    {
        QVERIFY(route.getSegments().isEmpty() || error != int(pdf::PDFAppRoute::Error::None));
        return;
    }

    QCOMPARE(route.getSegments(), segments);
    QStringList items;
    for (const pdf::PDFAppRoute::QueryItem& item : route.getQueryItems())
    {
        items << item.first + QLatin1Char('=') + item.second;
    }
    QCOMPARE(items.join(QLatin1Char(';')), query);
    QCOMPARE(route.getFragment(), fragment);

    // The text of a valid route parses to the same route
    const pdf::PDFAppRoute again = pdf::PDFAppRoute::parse(route.toString());
    QVERIFY(again.isValid());
    QCOMPARE(again, route);
}

void AppNavigationTest::routeRoundTrip()
{
    // Random segments and values survive toString / parse; every result is either
    // valid and equal, or refused (never a crash)
    QRandomGenerator generator(4711);
    const QString alphabet = QString::fromUtf8("abcXYZ019-._~ %/?#&=+\\žč\u00E9\u4E2D");
    for (int i = 0; i < 2000; ++i)
    {
        QStringList segments;
        const int segmentCount = generator.bounded(1, 5);
        for (int j = 0; j < segmentCount; ++j)
        {
            QString segment;
            const int length = generator.bounded(1, 12);
            for (int k = 0; k < length; ++k)
            {
                segment += alphabet[generator.bounded(int(alphabet.size()))];
            }
            segments << segment;
        }

        const pdf::PDFAppRoute route = pdf::PDFAppRoute::create(segments, { { QStringLiteral("q"), segments.join(QLatin1Char('&')) } }, QStringLiteral("anchor"));
        if (!route.isValid())
        {
            continue;
        }

        const pdf::PDFAppRoute parsed = pdf::PDFAppRoute::parse(route.toString());
        QVERIFY2(parsed.isValid(), qPrintable(route.toString()));
        QCOMPARE(parsed, route);
        QCOMPARE(parsed.getQueryValue(QStringLiteral("q")), segments.join(QLatin1Char('&')));
    }

    // Random bytes never crash the parser
    for (int i = 0; i < 2000; ++i)
    {
        QString text = QStringLiteral("/");
        const int length = generator.bounded(0, 40);
        for (int k = 0; k < length; ++k)
        {
            text += QChar(char16_t(generator.bounded(1, 0x3000)));
        }
        const pdf::PDFAppRoute route = pdf::PDFAppRoute::parse(text);
        if (route.isValid())
        {
            QCOMPARE(pdf::PDFAppRoute::parse(route.toString()), route);
        }
    }
}

void AppNavigationTest::routeCreate()
{
    using Error = pdf::PDFAppRoute::Error;

    QCOMPARE(pdf::PDFAppRoute::create({ "tools", "a/b" }).getError(), Error::InvalidCharacter);
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", ".." }).getError(), Error::DotSegment);
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", "" }).getError(), Error::EmptySegment);
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", QString(QChar(0)) }).getError(), Error::InvalidCharacter);
    QCOMPARE(pdf::PDFAppRoute::create({ "search" }, { { "q", "a" }, { "q", "b" } }).getError(), Error::DuplicateQueryKey);
    QCOMPARE(pdf::PDFAppRoute::create({ "search" }, { { "", "a" } }).getError(), Error::InvalidQuery);
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", QString(3000, QLatin1Char('a')) }).getError(), Error::TooLong);
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", QString(3000, QLatin1Char('a')) }).getRequestedText().size(), pdf::PDFAppRoute::MAXIMUM_LENGTH + 1);

    // A lone surrogate (a created route, MCP) has no UTF-8 form: refused, a valid
    // route always parses back from its text
    const QString loneHigh = QStringLiteral("a") + QChar(0xD800);
    const QString loneLow = QString(QChar(0xDC00)) + QStringLiteral("b");
    QCOMPARE(pdf::PDFAppRoute::create({ "tools", loneHigh }).getError(), Error::InvalidEncoding);
    QCOMPARE(pdf::PDFAppRoute::create({ "search" }, { { "q", loneLow } }).getError(), Error::InvalidEncoding);
    QCOMPARE(pdf::PDFAppRoute::create({ "about" }, {}, loneHigh).getError(), Error::InvalidFragment);
    const pdf::PDFAppRoute emoji = pdf::PDFAppRoute::create({ "search" }, { { "q", QString::fromUtf8("\xF0\x9F\x93\x84") } });
    QVERIFY(emoji.isValid());
    QCOMPARE(pdf::PDFAppRoute::parse(emoji.toString()), emoji);
    QVERIFY(!pdf::PDFAppRoute().isValid());
    QCOMPARE(pdf::PDFAppRoute().getError(), Error::Empty);

    const pdf::PDFAppRoute route = pdf::PDFAppRoute::create({ "settings", "security" }, { { "q", "a b&c=d" } }, "row");
    QVERIFY(route.isValid());
    QCOMPARE(route.toString(), QStringLiteral("/settings/security?q=a%20b%26c%3Dd#row"));
    QCOMPARE(route.getPath(), QStringLiteral("/settings/security"));
    QCOMPARE(route.getSegment(1), QStringLiteral("security"));
    QCOMPARE(route.getSegment(5), QString());
    QVERIFY(route.hasQueryItem("q"));
    QVERIFY(!route.hasQueryItem("x"));
    QCOMPARE(route.withFragment(QString()).toString(), QStringLiteral("/settings/security?q=a%20b%26c%3Dd"));
    QCOMPARE(route.withoutQuery().toString(), QStringLiteral("/settings/security"));
    QVERIFY(route != route.withoutQuery());
}

void AppNavigationTest::routeBuilders()
{
    using Routes = pdf::PDFAppRoutes;

    QCOMPARE(Routes::home().toString(), QStringLiteral("/home"));
    QCOMPARE(Routes::open().toString(), QStringLiteral("/open"));
    QCOMPARE(Routes::recent().toString(), QStringLiteral("/recent"));
    QCOMPARE(Routes::recent("pinned").toString(), QStringLiteral("/recent?filter=pinned"));
    QCOMPARE(Routes::recent("files", "name", "a & b").toString(), QStringLiteral("/recent?filter=files&sort=name&q=a%20%26%20b"));
    QCOMPARE(Routes::recent(QString(), "folder").toString(), QStringLiteral("/recent?sort=folder"));
    QCOMPARE(Routes::recent(QString(), QString(), "x").toString(), QStringLiteral("/recent?q=x"));
    QCOMPARE(Routes::tools().toString(), QStringLiteral("/tools"));
    QCOMPARE(Routes::tools("protect").toString(), QStringLiteral("/tools?category=protect"));
    QCOMPARE(Routes::tool("compress").toString(), QStringLiteral("/tools/compress"));
    QCOMPARE(Routes::toolCollections().toString(), QStringLiteral("/tools/collections"));
    QCOMPARE(Routes::toolCollection("mine").toString(), QStringLiteral("/tools/collections/mine"));
    QCOMPARE(Routes::ai().toString(), QStringLiteral("/ai"));
    QCOMPARE(Routes::whatsNew().toString(), QStringLiteral("/whats-new"));
    QCOMPARE(Routes::whatsNew("1.6.0").toString(), QStringLiteral("/whats-new/1.6.0"));
    QCOMPARE(Routes::releaseNotes("1.6.0").toString(), QStringLiteral("/whats-new/1.6.0/notes"));
    QCOMPARE(Routes::about("third-party").toString(), QStringLiteral("/about#third-party"));
    QCOMPARE(Routes::support().toString(), QStringLiteral("/support"));
    QCOMPARE(Routes::support("forms").toString(), QStringLiteral("/support?topic=forms"));
    QCOMPARE(Routes::support("for-you", "sign").toString(), QStringLiteral("/support?topic=for-you&q=sign"));
    QCOMPARE(Routes::support(QString(), "sign").toString(), QStringLiteral("/support?q=sign"));
    QCOMPARE(Routes::sponsor().toString(), QStringLiteral("/sponsor"));
    QCOMPARE(Routes::help("getting-started").toString(), QStringLiteral("/help/getting-started"));
    QCOMPARE(Routes::settings().toString(), QStringLiteral("/settings/general"));
    QCOMPARE(Routes::settings("security", "signature-verification").toString(), QStringLiteral("/settings/security#signature-verification"));
    QCOMPARE(Routes::search("merge pdf").toString(), QStringLiteral("/search?q=merge%20pdf"));
    QCOMPARE(Routes::search("x", "tools").toString(), QStringLiteral("/search?q=x&group=tools"));

    // A text of the search field with control characters, a lone surrogate (the field
    // cut at its maximum length in an emoji) or too long gives a valid route of the table
    {
        const pdf::PDFAppRouteTable searchTable = pdf::PDFAppRouteTable::createDefault();
        const QString hostileTexts[] =
        {
            QStringLiteral("a") + QChar(0x0001) + QStringLiteral("b") + QChar(0x007F) + QChar(0x0090),
            QString(255, QLatin1Char('x')) + QChar(0xD83D),
            QString(1000, QLatin1Char('y')),
            QStringLiteral("  merge \t pdf  "),
        };
        for (const QString& text : hostileTexts)
        {
            const pdf::PDFAppRoute route = Routes::search(text);
            QVERIFY(route.isValid());
            QVERIFY2(searchTable.match(route).result == pdf::PDFAppRouteMatch::Result::NotImplemented, qPrintable(route.toString()));
            QCOMPARE(pdf::PDFAppRoute::parse(route.toString()), route);
        }
        QCOMPARE(Routes::search(hostileTexts[0]).toString(), QStringLiteral("/search?q=ab"));
        QCOMPARE(Routes::search(hostileTexts[3]).toString(), QStringLiteral("/search?q=merge%20pdf"));
    }
    QCOMPARE(Routes::document(documentId(), 12).toString(), QStringLiteral("/document/%1?page=12").arg(documentId()));
    QCOMPARE(Routes::document(documentId()).toString(), QStringLiteral("/document/%1").arg(documentId()));
    QCOMPARE(Routes::documentProperties(documentId(), "fonts").toString(), QStringLiteral("/document/%1/properties/fonts").arg(documentId()));
    QCOMPARE(Routes::documentTool(documentId(), "compress").toString(), QStringLiteral("/document/%1/tool/compress").arg(documentId()));
    QCOMPARE(Routes::toolTab("merge", "m1").toString(), QStringLiteral("/tool/merge/m1"));

    // Every builder gives a route of the table
    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    for (const pdf::PDFAppRoute& route : { Routes::home(), Routes::open(), Routes::recent("files"), Routes::tools("pages"), Routes::tool("compress"),
                                            Routes::toolCollections(), Routes::toolCollection("mine"), Routes::ai(), Routes::whatsNew(), Routes::whatsNew("1.6"),
                                            Routes::about("third-party"), Routes::support(), Routes::sponsor(), Routes::help("topic"), Routes::settings(),
                                            Routes::recent("workspaces", "last-opened", "q"), Routes::support("pages", "merge"), Routes::releaseNotes("1.6.0"),
                                            Routes::settings("advanced", "row"), Routes::search("x", "help"), Routes::document(documentId(), 3),
                                            Routes::documentProperties(documentId(), "xmp"), Routes::documentTool(documentId(), "ocr"), Routes::toolTab("merge", "m1"),
                                            Routes::tools("pinned", "merge"), Routes::tools(QString(), QString(), "custom-1"), Routes::toolCollectionNew("pages.merge") })
    {
        // The list of the collections is redirected to the first collection (step 6)
        const pdf::PDFAppRouteMatch match = table.match(route);
        const bool isRedirect = route == Routes::toolCollections() && match.result == pdf::PDFAppRouteMatch::Result::Redirect;
        QVERIFY2(match.result == pdf::PDFAppRouteMatch::Result::NotImplemented || isRedirect, qPrintable(route.toString()));
    }
}

void AppNavigationTest::routeValues()
{
    using Values = pdf::PDFAppRouteValues;

    QVERIFY(Values::number().accepts("1"));
    QVERIFY(Values::number().accepts("2147483647"));
    QVERIFY(!Values::number().accepts("2147483648"));
    QVERIFY(!Values::number().accepts("0"));
    QVERIFY(!Values::number().accepts("01"));
    QVERIFY(!Values::number().accepts("-1"));
    QVERIFY(!Values::number().accepts("+1"));
    QVERIFY(!Values::number().accepts(" 1"));
    QVERIFY(!Values::number().accepts(""));
    QVERIFY(!Values::number().accepts(QString::fromUtf8("١")));
    QVERIFY(Values::slug().accepts("convert.epub"));
    QVERIFY(Values::slug().accepts("a-b_c"));
    QVERIFY(!Values::slug().accepts("-a"));
    QVERIFY(!Values::slug().accepts("A"));
    QVERIFY(!Values::slug().accepts(QString(65, QLatin1Char('a'))));
    QVERIFY(!Values::slug().accepts(""));
    QVERIFY(Values::version().accepts("1.6"));
    QVERIFY(Values::version().accepts("1.6.0.0"));
    QVERIFY(!Values::version().accepts("1"));
    QVERIFY(!Values::version().accepts("1.6.0.0.0"));
    QVERIFY(!Values::version().accepts("1..6"));
    QVERIFY(!Values::version().accepts("v1.6"));
    QVERIFY(Values::documentId().accepts(documentId()));
    QVERIFY(Values::documentId().accepts("234567abcdefghijklmnopqrst"));
    QVERIFY(!Values::documentId().accepts("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
    QVERIFY(!Values::documentId().accepts("abcdefghijklmnopqrstuvwxy1"));
    QVERIFY(!Values::documentId().accepts("abc"));
    QVERIFY(Values::options({ "a", "b" }).accepts("a"));
    QVERIFY(!Values::options({ "a", "b" }).accepts("c"));
    QVERIFY(Values::text(3).accepts("abc"));
    QVERIFY(!Values::text(3).accepts("abcd"));
    QVERIFY(Values::text(-5).accepts(""));
}

void AppNavigationTest::routeTable_data()
{
    using Tab = pdf::PDFAppTabKind;
    using Nav = pdf::PDFAppNavItem;

    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("resolved");
    QTest::addColumn<int>("tabKind");
    QTest::addColumn<int>("navItem");

    const QString id = documentId();
    auto add = [](const QString& route, const QString& pattern, const QString& resolved, Tab tab, Nav nav)
    {
        QTest::newRow(qPrintable(route)) << route << pattern << resolved << int(tab) << int(nav);
    };

    add("/", "/home", "/home", Tab::Home, Nav::Home);
    add("/home", "/home", "/home", Tab::Home, Nav::Home);
    add("/open", "/open", "/open", Tab::Home, Nav::Open);
    add("/recent", "/recent", "/recent", Tab::Home, Nav::Recent);
    add("/recent?filter=pinned&sort=last-opened&q=report", "/recent", "/recent?filter=pinned&sort=last-opened&q=report", Tab::Home, Nav::Recent);
    add("/recent?filter=workspaces", "/recent", "/recent?filter=workspaces", Tab::Home, Nav::Recent);
    add("/tools", "/tools", "/tools", Tab::Home, Nav::Tools);
    add("/tools?category=optimize&q=size", "/tools", "/tools?category=optimize&q=size", Tab::Home, Nav::Tools);
    // The list of the collections shows the first (built-in) collection (step 6)
    add("/tools/collections", "/tools/collections/{collectionId}", "/tools/collections/prepare-for-sharing", Tab::Home, Nav::Tools);
    add("/tools?collection=custom-1", "/tools", "/tools?collection=custom-1", Tab::Home, Nav::Tools);
    add("/tools/collections/new?tool=pages.merge", "/tools/collections/new", "/tools/collections/new?tool=pages.merge", Tab::Home, Nav::Tools);
    add("/tools/collections/new", "/tools/collections/new", "/tools/collections/new", Tab::Home, Nav::Tools);
    add("/tools/collections/my-tools", "/tools/collections/{collectionId}", "/tools/collections/my-tools", Tab::Home, Nav::Tools);
    add("/tools/compress", "/tools/{toolId}", "/tools/compress", Tab::Home, Nav::Tools);
    add("/ai", "/ai", "/ai", Tab::Home, Nav::Ai);
    add("/whats-new", "/whats-new", "/whats-new", Tab::Home, Nav::WhatsNew);
    add("/whats-new/1.6.0", "/whats-new/{version}", "/whats-new/1.6.0", Tab::Home, Nav::WhatsNew);
    add("/whats-new/1.6.0/notes", "/whats-new/{version}/notes", "/whats-new/1.6.0/notes", Tab::Home, Nav::WhatsNew);
    add("/about", "/about", "/about", Tab::Home, Nav::About);
    add("/about#third-party", "/about", "/about#third-party", Tab::Home, Nav::About);
    add("/support", "/support", "/support", Tab::Home, Nav::Support);
    add("/sponsor", "/sponsor", "/sponsor", Tab::Home, Nav::Sponsor);
    add("/help/getting-started", "/help/{topic}", "/help/getting-started", Tab::Help, Nav::None);
    add("/settings", "/settings/{section}", "/settings/general", Tab::Home, Nav::Settings);
    add("/settings?q=color", "/settings", "/settings?q=color", Tab::Home, Nav::Settings);
    for (const char* section : { "general", "viewing", "color-modes", "editing", "security", "speech", "shortcuts", "color-management", "advanced", "storage" })
    {
        add(QStringLiteral("/settings/%1").arg(QLatin1String(section)), "/settings/{section}", QStringLiteral("/settings/%1").arg(QLatin1String(section)), Tab::Home, Nav::Settings);
    }
    add("/settings/security#signature-verification", "/settings/{section}", "/settings/security#signature-verification", Tab::Home, Nav::Settings);
    add("/search?q=merge", "/search", "/search?q=merge", Tab::Home, Nav::None);
    add("/search?q=merge&group=tools", "/search", "/search?q=merge&group=tools", Tab::Home, Nav::None);
    add(QStringLiteral("/document/%1").arg(id), "/document/{documentId}", QStringLiteral("/document/%1").arg(id), Tab::Document, Nav::None);
    add(QStringLiteral("/document/%1?page=3&panel=outline&q=x").arg(id), "/document/{documentId}", QStringLiteral("/document/%1?page=3&panel=outline&q=x").arg(id), Tab::Document, Nav::None);
    for (const char* section : { "general", "security", "fonts", "display", "xmp" })
    {
        const QString route = QStringLiteral("/document/%1/properties/%2").arg(id, QLatin1String(section));
        add(route, "/document/{documentId}/properties/{section}", route, Tab::Document, Nav::None);
    }
    add(QStringLiteral("/document/%1/tool/compress").arg(id), "/document/{documentId}/tool/{toolId}", QStringLiteral("/document/%1/tool/compress").arg(id), Tab::Document, Nav::None);
    add("/tool/merge/merge-1", "/tool/{toolId}/{instanceId}", "/tool/merge/merge-1", Tab::Tool, Nav::None);
}

void AppNavigationTest::routeTable()
{
    QFETCH(QString, route);
    QFETCH(QString, pattern);
    QFETCH(QString, resolved);
    QFETCH(int, tabKind);
    QFETCH(int, navItem);

    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    pdf::PDFAppRoute current = pdf::PDFAppRoute::parse(route);
    QVERIFY(current.isValid());

    pdf::PDFAppRouteMatch match = table.match(current);
    for (int i = 0; i < 4 && match.result == pdf::PDFAppRouteMatch::Result::Redirect; ++i)
    {
        current = match.redirectedRoute;
        match = table.match(current);
    }

    QCOMPARE(int(match.result), int(pdf::PDFAppRouteMatch::Result::NotImplemented));
    QVERIFY(match.entry);
    QCOMPARE(match.entry->pattern, pattern);
    QCOMPARE(current.toString(), resolved);
    QCOMPARE(int(match.entry->tabKind), tabKind);
    QCOMPARE(int(match.entry->navItem), navItem);
    QVERIFY(!match.entry->getTitle().isEmpty());
}

void AppNavigationTest::routeTableInvalid_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<int>("result");
    QTest::addColumn<QString>("refused");

    using Result = pdf::PDFAppRouteMatch::Result;
    const QString id = documentId();

    QTest::newRow("unknown") << "/unknown" << int(Result::UnknownRoute) << QString();
    QTest::newRow("unknown nested") << "/home/more" << int(Result::UnknownRoute) << QString();
    QTest::newRow("help without topic") << "/help" << int(Result::UnknownRoute) << QString();
    QTest::newRow("foreign section") << "/settings/foreign" << int(Result::InvalidParameter) << "section";
    QTest::newRow("section case") << "/settings/General" << int(Result::InvalidParameter) << "section";
    QTest::newRow("unknown query key") << "/recent?unknown=1" << int(Result::InvalidParameter) << "unknown";
    QTest::newRow("unknown filter") << "/recent?filter=bogus" << int(Result::InvalidParameter) << "filter";
    QTest::newRow("unknown category") << "/tools?category=bogus" << int(Result::InvalidParameter) << "category";
    QTest::newRow("query of home") << "/home?x=1" << int(Result::InvalidParameter) << "x";
    QTest::newRow("fragment of home") << "/home#x" << int(Result::InvalidParameter) << "#";
    QTest::newRow("tool id") << "/tools/Convert!" << int(Result::InvalidParameter) << "toolId";
    QTest::newRow("version") << "/whats-new/latest" << int(Result::InvalidParameter) << "version";
    QTest::newRow("document id") << "/document/123" << int(Result::InvalidParameter) << "documentId";
    QTest::newRow("document page") << QStringLiteral("/document/%1?page=0").arg(id) << int(Result::InvalidParameter) << "page";
    QTest::newRow("document page text") << QStringLiteral("/document/%1?page=two").arg(id) << int(Result::InvalidParameter) << "page";
    QTest::newRow("properties section") << QStringLiteral("/document/%1/properties/other").arg(id) << int(Result::InvalidParameter) << "section";
    QTest::newRow("search group") << "/search?q=x&group=all" << int(Result::InvalidParameter) << "group";
    QTest::newRow("long search") << QStringLiteral("/search?q=") + QString(300, QLatin1Char('a')) << int(Result::InvalidParameter) << "q";
    QTest::newRow("invalid") << "/a/../b" << int(Result::InvalidRoute) << QString();
}

void AppNavigationTest::routeTableInvalid()
{
    QFETCH(QString, route);
    QFETCH(int, result);
    QFETCH(QString, refused);

    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    const pdf::PDFAppRouteMatch match = table.match(pdf::PDFAppRoute::parse(route));
    QCOMPARE(int(match.result), result);
    QCOMPARE(match.refusedParameter, refused);
    QVERIFY(!match.isFound());
}

void AppNavigationTest::routeTableRegistration()
{
    pdf::PDFAppRouteTable table;
    pdf::PDFAppRouteEntry entry;
    entry.pattern = QStringLiteral("/a/{id}");
    QVERIFY(!table.addEntry(entry));    // a parameter without values
    entry.parameters[QStringLiteral("id")] = pdf::PDFAppRouteValues::slug();
    QVERIFY(table.addEntry(entry));
    QVERIFY(!table.addEntry(entry));    // twice
    entry.pattern = QStringLiteral("a");
    QVERIFY(!table.addEntry(entry));    // not absolute
    entry.pattern = QStringLiteral("/a//b");
    QVERIFY(!table.addEntry(entry));    // empty segment
    entry.pattern = QStringLiteral("/a/b");
    QVERIFY(table.addEntry(entry));

    // A literal segment wins over a parameter
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/a/b")).entry->pattern, QStringLiteral("/a/b"));
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/a/c")).entry->pattern, QStringLiteral("/a/{id}"));
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/a/c")).getParameter("id"), QStringLiteral("c"));
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/a/c")).getParameter("other"), QString());

    QVERIFY(!table.setFactory(QStringLiteral("/none"), std::make_shared<NavigationTestFactory>("x")));
    QVERIFY(table.setFactory(QStringLiteral("/a/b"), std::make_shared<NavigationTestFactory>("x")));
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/a/b")).result, pdf::PDFAppRouteMatch::Result::Found);
    QVERIFY(table.findEntry(QStringLiteral("/a/b")));
    QVERIFY(!table.findEntry(QStringLiteral("/a/x")));

    // A cycle of redirects ends on Page Not Found in the navigator (tested there); the
    // table itself gives one step
    pdf::PDFAppRouteEntry loop;
    loop.pattern = QStringLiteral("/loop");
    loop.redirect = QStringList({ "loop" });
    QVERIFY(table.addEntry(loop));
    QCOMPARE(table.match(pdf::PDFAppRoute::parse("/loop")).result, pdf::PDFAppRouteMatch::Result::Redirect);

    // The default table has every pattern once and all patterns are valid
    const pdf::PDFAppRouteTable defaultTable = pdf::PDFAppRouteTable::createDefault();
    QCOMPARE(defaultTable.getEntries().size(), size_t(24));
}

void AppNavigationTest::routeDescriptions()
{
    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/tools/convert.epub")), QString::fromUtf8("Tool “convert.epub”"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/tools/convert.epub")), QStringLiteral("convert epub"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/settings/foreign")), QString::fromUtf8("Settings section “foreign”"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/settings/foreign")), QStringLiteral("foreign"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/home")), QString::fromUtf8("Page “Home”"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/home")), QStringLiteral("Home"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/unknown-page")), QString::fromUtf8("Page “unknown-page”"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/unknown-page")), QStringLiteral("unknown page"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/a/../b")), QStringLiteral("An invalid link"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/a/../b")), QString());
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/document/123")), QString());
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse("/search?q=%20merge%20%20pdf")), QStringLiteral("merge pdf"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/whats-new/1.6")), QStringLiteral("Release notes of version 1.6"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/help/x")), QString::fromUtf8("Help topic “x”"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/tools/collections/x")), QString::fromUtf8("Tool collection “x”"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse("/search?q=x")), QStringLiteral("Search results"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/document/%1").arg(documentId()))), QStringLiteral("A document, which is not open"));

    // The texts are bounded
    const QString longSegment(1000, QLatin1Char('x'));
    QVERIFY(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/") + longSegment)).size() < 100);
    QVERIFY(table.getSearchText(pdf::PDFAppRoute::parse(QStringLiteral("/") + longSegment)).size() <= pdf::PDFAppRouteTable::MAXIMUM_SEARCH_TEXT_LENGTH);
}

void AppNavigationTest::history()
{
    pdf::PDFAppHistory history;
    QVERIFY(history.isEmpty());
    QVERIFY(!history.getCurrentEntry());
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());
    QVERIFY(!history.goBack());
    QVERIFY(!history.goForward());
    history.setCurrentViewState({ { "a", 1 } });
    QVERIFY(!history.getEntry(0));

    history.replace(pdf::PDFAppRoutes::home());
    QCOMPARE(history.getCount(), 1);
    history.push(pdf::PDFAppRoutes::open());
    history.push(pdf::PDFAppRoutes::recent());
    history.setCurrentViewState({ { "value", 3 } });
    QCOMPARE(history.getCurrentIndex(), 2);
    QVERIFY(history.goBack());
    QVERIFY(history.goBack());
    QVERIFY(!history.goBack());
    QCOMPARE(history.getCurrentEntry()->route, pdf::PDFAppRoutes::home());
    QVERIFY(history.goForward());
    QVERIFY(history.goForward());
    QCOMPARE(history.getCurrentEntry()->viewState.value("value").toInt(), 3);

    // A push after back removes the forward entries
    history.goBack();
    history.push(pdf::PDFAppRoutes::tools());
    QCOMPARE(history.getCount(), 3);
    QVERIFY(!history.canGoForward());
    QCOMPARE(history.getCurrentEntry()->route, pdf::PDFAppRoutes::tools());

    // Replace clears the view state
    history.setCurrentViewState({ { "value", 5 } });
    history.replace(pdf::PDFAppRoutes::ai());
    QVERIFY(history.getCurrentEntry()->viewState.isEmpty());

    // At most 50 entries, the oldest are dropped
    for (int i = 0; i < 120; ++i)
    {
        history.push(pdf::PDFAppRoutes::help(QStringLiteral("topic-%1").arg(i)));
    }
    QCOMPARE(history.getCount(), pdf::PDFAppHistory::MAXIMUM_ENTRIES);
    QCOMPARE(history.getCurrentIndex(), pdf::PDFAppHistory::MAXIMUM_ENTRIES - 1);
    QCOMPARE(history.getEntry(0)->route, pdf::PDFAppRoutes::help(QStringLiteral("topic-70")));
    QVERIFY(!history.getEntry(-1));
    QVERIFY(!history.getEntry(50));

    history.clear();
    QVERIFY(history.isEmpty());
    QCOMPARE(history.getCurrentIndex(), -1);
}

void AppNavigationTest::navigationAllRoutes_data()
{
    routeTable_data();
}

void AppNavigationTest::navigationAllRoutes()
{
    QFETCH(QString, route);
    QFETCH(QString, pattern);
    QFETCH(QString, resolved);
    QFETCH(int, tabKind);
    QFETCH(int, navItem);

    NavigationFixture fixture;
    QVERIFY(fixture.window);
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);

    // Step 1: no page exists, every route ends on Page Not Found in the Home tab
    // (the manual in the Help tab, owner decision O13)
    fixture.navigate(route);
    QCOMPARE(navigated.count(), 1);
    QVERIFY(navigated.last().at(2).toBool());
    if (tabKind == int(pdf::PDFAppTabKind::Help))
    {
        QCOMPARE(fixture.currentTab()->getKind(), pdf::PDFAppTabKind::Help);
        QCOMPARE(fixture.currentTab(), fixture.window->findHelpTab());
    }
    else
    {
        QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    }
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::None);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), resolved);
    QCOMPARE(fixture.navigator()->getLastAnnouncement(), QStringLiteral("Page not found page"));
    QCOMPARE(fixture.window->windowTitle(), QString::fromUtf8("Page not found – PDF4QT"));

    // A later step registers the factory: the route opens its page with its highlight
    auto factory = std::make_shared<NavigationTestFactory>(pattern);
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(pattern, factory));
    fixture.navigate(route);

    if (tabKind == int(pdf::PDFAppTabKind::Home) || tabKind == int(pdf::PDFAppTabKind::Help))
    {
        NavigationTestPage* page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
        QVERIFY(page);
        QCOMPARE(page->getRoute().toString(), resolved);
        QCOMPARE(int(fixture.currentTab()->getNavItem()), navItem);
        QCOMPARE(int(fixture.currentTab()->getKind()), tabKind);
        QCOMPARE(fixture.window->windowTitle(), QString::fromUtf8("%1 – PDF4QT").arg(pattern));
        QCOMPARE(fixture.navigator()->getLastAnnouncement(), QStringLiteral("%1 page").arg(pattern));

        // The header shows the texts of the page, the search stays
        pdf::PDFAppWorkspaceTab* workspaceTab = static_cast<pdf::PDFAppWorkspaceTab*>(fixture.currentTab());
        QCOMPARE(workspaceTab->getHeader()->getTitleLabel()->text(), pattern);
        QVERIFY(workspaceTab->getSearchField());
    }
    else
    {
        // No document tab of the id exists: Page Not Found
        QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
        QCOMPARE(factory->createCount, 0);
    }

    // The same route again replaces the entry of the history
    const int count = fixture.currentTab()->getHistory().getCount();
    fixture.navigate(route);
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), count);
}

void AppNavigationTest::navigationInvalidRoutes_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("description");

    const QString id = documentId();
    QTest::newRow("unknown") << "/unknown" << QString::fromUtf8("Page “unknown”");
    QTest::newRow("foreign section") << "/settings/foreign" << QString::fromUtf8("Settings section “foreign”");
    QTest::newRow("unknown document") << QStringLiteral("/document/%1").arg(id) << QStringLiteral("A document, which is not open");
    QTest::newRow("invalid document id") << "/document/1" << QStringLiteral("A document, which is not open");
    QTest::newRow("tool without GUI") << "/tools/compress" << QString::fromUtf8("Tool “compress”");
    QTest::newRow("document tool") << QStringLiteral("/document/%1/tool/compress").arg(id) << QString::fromUtf8("Tool “compress”");
    QTest::newRow("tool tab") << "/tool/merge/one" << QString::fromUtf8("Tool “merge”");
    QTest::newRow("null") << "/tools/a%00" << QStringLiteral("An invalid link");
    QTest::newRow("dot dot") << "/settings/../ai" << QStringLiteral("An invalid link");
    QTest::newRow("10 kB") << QStringLiteral("/tools/") + QString(10 * 1024, QLatin1Char('y')) << QStringLiteral("An invalid link");
    QTest::newRow("unknown parameter") << "/ai?mode=1" << QString::fromUtf8("Page “AI”");
    QTest::newRow("relative") << "ai" << QStringLiteral("An invalid link");
    QTest::newRow("empty") << "" << QStringLiteral("An invalid link");
}

void AppNavigationTest::navigationInvalidRoutes()
{
    QFETCH(QString, route);
    QFETCH(QString, description);

    NavigationFixture fixture;

    // Pages exist, but the route is not valid for them
    for (const QString& pattern : { QStringLiteral("/ai"), QStringLiteral("/settings/{section}"), QStringLiteral("/home") })
    {
        fixture.navigator()->getRouteTable().setFactory(pattern, std::make_shared<NavigationTestFactory>(pattern));
    }

    fixture.window->getHomeTab()->setNavItem(pdf::PDFAppNavItem::Ai);
    fixture.navigator()->navigateLink(route);

    pdf::PDFPageNotFoundPage* page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::None);
    QCOMPARE(page->getRequest().description, description);

    // The texts are elided to the box (a 10 kB route)
    QFontMetricsF metrics(page->findChild<QLabel*>(QStringLiteral("requestedRoute"))->font());
    QVERIFY(metrics.horizontalAdvance(page->getPathText()) <= pdf::PDFPageNotFoundPage::MAXIMUM_TEXT_WIDTH);
    QVERIFY(page->getDescriptionText().size() < 200);
    QVERIFY(page->findChild<QWidget*>(QStringLiteral("requestedPath"))->width() <= pdf::PDFPageNotFoundPage::MAXIMUM_TEXT_WIDTH + 2);

    // A page, which refuses the route, is Page Not Found too
    auto refusing = std::make_shared<NavigationTestFactory>("refusing", false);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/support"), refusing);
    fixture.navigate("/support");
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCOMPARE(refusing->createCount, 1);

    // A factory, which creates nothing, too
    auto empty = std::make_shared<NavigationTestFactory>("empty");
    empty->onCreate = []() { return false; };
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/sponsor"), empty);
    fixture.navigate("/sponsor");
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));

    // A cycle of redirects ends on Page Not Found
    pdf::PDFAppRouteEntry loop;
    loop.pattern = QStringLiteral("/loop");
    loop.redirect = QStringList({ "loop" });
    QVERIFY(fixture.navigator()->getRouteTable().addEntry(loop));
    fixture.navigate("/loop");
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
}

void AppNavigationTest::notFoundPage()
{
    NavigationFixture fixture;
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), std::make_shared<NavigationTestFactory>("About"));

    // The first page of the window has no history: Back is disabled
    pdf::PDFPageNotFoundPage* page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QVERIFY(!page->getBackButton()->isEnabled());
    QCOMPARE(page->getTitle(), QStringLiteral("Page not found"));

    fixture.navigate("/about");
    fixture.navigate("/tools/convert.epub");
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(page->getDescriptionText(), QString::fromUtf8("Tool “convert.epub”"));
    QCOMPARE(page->getPathText(), QStringLiteral("/tools/convert.epub"));
    QCOMPARE(page->getSearchButton()->text(), QString::fromUtf8("Search for “convert epub”"));
    QVERIFY(page->getBackButton()->isEnabled());
    QVERIFY(pdf::PDFWidgetStyleProperties::isSet(page->getHomeButton(), pdf::PDFWidgetStyleProperties::PRIMARY));

    // The hints show the shortcuts of Back and of the search (Alt ←, Ctrl K)
    QStringList keys;
    for (const pdf::PDFAppKeyCap* keyCap : page->findChildren<pdf::PDFAppKeyCap*>())
    {
        keys << keyCap->getText();
    }
    QCOMPARE(keys.size(), 4);
    QCOMPARE(keys[1], QString(QChar(0x2190)));
    QCOMPARE(keys[3], QStringLiteral("K"));
#if !defined(Q_OS_MACOS)
    QCOMPARE(keys[0], QStringLiteral("Alt"));
    QCOMPARE(keys[2], QStringLiteral("Ctrl"));
    QCOMPARE(pdf::PDFAppKeyCap::getKeyNames(QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_F5)), QStringList({ "Shift", "Meta", "F5" }));
#endif
    QCOMPARE(pdf::PDFAppKeyCap::getKeyNames(QKeySequence()), QStringList());
    QCOMPARE(pdf::PDFAppKeyCap::getKeyNames(QKeySequence(Qt::ALT | Qt::Key_Right)).last(), QString(QChar(0x2192)));
    QCOMPARE(pdf::PDFAppKeyCap::getKeyNames(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up)).size(), 3);
    QVERIFY(page->findChildren<pdf::PDFAppKeyCap*>().first()->sizeHint().height() == 18);

    // Search for … asks for the global search (Page Not Found in step 1)
    QTest::mouseClick(page->getSearchButton(), Qt::LeftButton);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/search?q=convert%20epub"));
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(page->getRequest().searchText, QStringLiteral("convert epub"));

    // Back goes to the previous entry, Go to Home to /home
    QTest::mouseClick(page->getBackButton(), Qt::LeftButton);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/tools/convert.epub"));
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QTest::mouseClick(page->getHomeButton(), Qt::LeftButton);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::home());

    // Without a search text there is no search button; the language change keeps the texts
    fixture.navigate(QStringLiteral("/document/%1").arg(documentId()));
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page->getSearchButton()->isHidden());
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(page, &languageChange);
    QCOMPARE(page->getDescriptionText(), QStringLiteral("A document, which is not open"));

    // A page without the context (no shortcuts known) still works
    pdf::PDFPageNotFoundPage standalone(nullptr);
    pdf::PDFAppNotFoundRequest request;
    standalone.setRequest(request);
    QCOMPARE(standalone.getDescriptionText(), QStringLiteral("An unknown page"));
    QVERIFY(standalone.findChildren<pdf::PDFAppKeyCap*>().isEmpty());
    std::unique_ptr<pdf::PDFAppPage> created(pdf::PDFPageNotFoundFactory().createPage(nullptr, nullptr));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(created.get()));
}

void AppNavigationTest::notFoundUntrustedText()
{
    NavigationFixture fixture;

    // A requested text, which looks like HTML, stays a plain text: the labels are
    // plain and the tooltip of the description (Qt shows a tooltip, which looks
    // like HTML, as rich text - formatting, pictures) shows the same text
    fixture.navigate(QStringLiteral("/tools/<img src=x><b>bold"));
    pdf::PDFPageNotFoundPage* page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    const QString description = page->getRequest().description;
    QVERIFY(description.contains(QStringLiteral("<img src=x><b>bold")));
    QLabel* descriptionLabel = page->findChild<QLabel*>(QStringLiteral("requestedItem"));
    QLabel* pathLabel = page->findChild<QLabel*>(QStringLiteral("requestedRoute"));
    QVERIFY(descriptionLabel);
    QVERIFY(pathLabel);
    QCOMPARE(descriptionLabel->textFormat(), Qt::PlainText);
    QCOMPARE(pathLabel->textFormat(), Qt::PlainText);
    QCOMPARE(page->getDescriptionText(), description);
    const QString toolTip = descriptionLabel->toolTip();
    const QString shownToolTip = Qt::mightBeRichText(toolTip) ? QTextDocumentFragment::fromHtml(toolTip).toPlainText() : toolTip;
    QCOMPARE(shownToolTip, description);

    // '&' of the requested text is not a mnemonic of Search for …
    fixture.navigate(QStringLiteral("/tools/a&b"));
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(page->getRequest().searchText, QStringLiteral("a&b"));
    QCOMPARE(page->getSearchButton()->text(), QString::fromUtf8("Search for “a&&b”"));

    // Invisible characters (a bidirectional override, a zero width space, also in
    // a refused route) cannot make the requested link look like another one
    const QString override = QStringLiteral("/tools/fdp") + QChar(0x202E) + QStringLiteral("exe") + QChar(0x200B);
    for (const QString& text : { override, override + QStringLiteral("%zz") })
    {
        fixture.navigate(text);
        page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
        QVERIFY(page);
        for (const QString& shown : { page->getPathText(), page->getDescriptionText(), page->findChild<QLabel*>(QStringLiteral("requestedItem"))->toolTip(),
                                      page->getSearchButton()->text() })
        {
            QVERIFY2(!shown.contains(QChar(0x202E)) && !shown.contains(QChar(0x200B)), qPrintable(shown));
        }
        QVERIFY2(page->getPathText().contains(QStringLiteral("fdp%E2%80%AEexe%E2%80%8B")), qPrintable(page->getPathText()));
    }
}

void AppNavigationTest::historyAndViewState()
{
    NavigationFixture fixture;
    auto about = std::make_shared<NavigationTestFactory>("About");
    auto support = std::make_shared<NavigationTestFactory>("Support");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), about);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/support"), support);

    fixture.navigate("/about");
    QPointer<NavigationTestPage> aboutPage = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(aboutPage);
    aboutPage->value = 42;

    fixture.navigate("/support");
    QPointer<NavigationTestPage> supportPage = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(supportPage);
    supportPage->value = 7;

    // The previous page is deleted later (it may be the sender of the navigation)
    QTRY_VERIFY(aboutPage.isNull());

    // Back (Alt+Left) restores the state of the view of the page
    QAction* back = fixture.window->getCommandAction(pdf::PDFAppCommands::back());
    QVERIFY(back);
    QCOMPARE(back->shortcut(), QKeySequence(Qt::ALT | Qt::Key_Left));
    back->trigger();
    NavigationTestPage* restored = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(restored);
    QCOMPARE(restored->getTitle(), QStringLiteral("About"));
    QVERIFY(restored->isRestored);
    QCOMPARE(restored->value, 42);
    QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::About);

    // Forward by the key Alt+Right
    QTest::keyClick(fixture.window.data(), Qt::Key_Right, Qt::AltModifier);
    restored = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(restored);
    QCOMPARE(restored->getTitle(), QStringLiteral("Support"));
    QCOMPARE(restored->value, 7);

    // Mouse buttons back and forward anywhere in the window
    QWidget* target = fixture.window->getHomeTab()->getFooter();
    QMouseEvent backPress(QEvent::MouseButtonPress, QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), Qt::BackButton, Qt::BackButton, Qt::NoModifier);
    QApplication::sendEvent(target, &backPress);
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("About"));
    QMouseEvent forwardPress(QEvent::MouseButtonPress, QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), Qt::ForwardButton, Qt::ForwardButton, Qt::NoModifier);
    QApplication::sendEvent(target, &forwardPress);
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Support"));

    // ... also over a child, which takes every button of the mouse (the search
    // field): the buttons come through the QWindow as from the system
    pdf::PDFLineEdit* searchField = fixture.window->getHomeTab()->getSearchField();
    QVERIFY(searchField && searchField->isVisible());
    QWindow* windowHandle = fixture.window->windowHandle();
    QVERIFY(windowHandle);
    const QPoint fieldCenter = searchField->mapTo(fixture.window.data(), searchField->rect().center());
    QCOMPARE(fixture.window->childAt(fieldCenter), searchField);
    QTest::mousePress(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter);
    QTest::mouseRelease(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter);
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("About"));
    QTest::mousePress(windowHandle, Qt::ForwardButton, Qt::NoModifier, fieldCenter);
    QTest::mouseRelease(windowHandle, Qt::ForwardButton, Qt::NoModifier, fieldCenter);
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Support"));

    // The field still gets the left button and the text
    QTest::mouseClick(windowHandle, Qt::LeftButton, Qt::NoModifier, fieldCenter);
    QTRY_VERIFY(searchField->hasFocus());
    QTest::keyClicks(searchField, QStringLiteral("abc"));
    QCOMPARE(searchField->text(), QStringLiteral("abc"));
    searchField->clear();
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Support"));

    // Nothing forward: no change
    QVERIFY(!fixture.navigator()->goForward());

    // Back to the first entry (Home), then no more back
    QVERIFY(fixture.navigator()->goBack());
    QVERIFY(fixture.navigator()->goBack());
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QVERIFY(!fixture.navigator()->goBack());

    // A page asks for back by its signal
    QVERIFY(fixture.navigator()->goForward());
    NavigationTestPage* aboutAgain = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(aboutAgain);
    aboutAgain->requestBack();
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));

    // Replace does not add an entry
    const int count = fixture.currentTab()->getHistory().getCount();
    fixture.navigate("/about", pdf::PDFAppNavigationMode::Replace);
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), count);
}

void AppNavigationTest::navigationFromOwnSignal()
{
    NavigationFixture fixture;
    auto about = std::make_shared<NavigationTestFactory>("About");
    auto support = std::make_shared<NavigationTestFactory>("Support");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), about);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/support"), support);

    // A page navigates from its own signal: it is replaced and deleted later
    fixture.navigate("/about");
    QPointer<NavigationTestPage> page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(page);
    page->navigateTo(pdf::PDFAppRoutes::support());
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Support"));
    QTRY_VERIFY(page.isNull());

    // A page navigates from setRoute (a redirect of the page): done after the navigation
    about->onSetRoute = [](NavigationTestPage* aboutPage) { aboutPage->navigateTo(pdf::PDFAppRoutes::support(), pdf::PDFAppNavigationMode::Replace); };
    fixture.navigate("/about");
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Support"));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::support());

    // Two pages, which redirect to each other, are stopped
    support->onSetRoute = [](NavigationTestPage* supportPage) { supportPage->navigateTo(pdf::PDFAppRoutes::about()); };
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Navigation stopped"));
    fixture.navigate("/about");
    QVERIFY(fixture.currentPage());
    about->onSetRoute = nullptr;
    support->onSetRoute = nullptr;

    // A page navigates from restoreViewState
    fixture.navigate("/about");
    about->onRestore = [](NavigationTestPage* aboutPage) { aboutPage->navigateTo(pdf::PDFAppRoutes::home()); };
    fixture.navigate("/support");
    QVERIFY(fixture.navigator()->goBack());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::home());
    about->onRestore = nullptr;

    // The sidebar asks for the route of its item
    pdf::PDFAppSidebar* sidebar = fixture.window->getHomeTab()->getSidebar();
    QVERIFY(sidebar);
    QTest::mouseClick(sidebar->getNavigation()->getItem(int(pdf::PDFAppNavItem::About)), Qt::LeftButton);
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("About"));
    QCOMPARE(sidebar->getCurrentItem(), pdf::PDFAppNavItem::About);
    QTest::mouseClick(sidebar->getNavigation()->getItem(int(pdf::PDFAppNavItem::Recent)), Qt::LeftButton);
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCOMPARE(sidebar->getCurrentItem(), pdf::PDFAppNavItem::None);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::recent());
    for (int i = 0; i <= int(pdf::PDFAppNavItem::Settings); ++i)
    {
        QVERIFY(pdf::PDFAppSidebar::getRoute(static_cast<pdf::PDFAppNavItem>(i)).isValid());
    }
    QVERIFY(!pdf::PDFAppSidebar::getRoute(pdf::PDFAppNavItem::None).isValid());
}

void AppNavigationTest::replaceRoute()
{
    NavigationFixture fixture;
    auto recent = std::make_shared<NavigationTestFactory>("Recent");
    auto about = std::make_shared<NavigationTestFactory>("About");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/recent"), recent);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), about);
    QSignalSpy replacedSpy(fixture.navigator(), &pdf::PDFAppNavigator::routeReplaced);
    QSignalSpy navigatedSpy(fixture.navigator(), &pdf::PDFAppNavigator::navigated);

    fixture.navigate("/about");
    fixture.navigate("/recent");
    QPointer<NavigationTestPage> page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(page);
    page->value = 7;
    const QString announcement = fixture.navigator()->getLastAnnouncement();
    const int entryCount = fixture.currentTab()->getHistory().getCount();
    navigatedSpy.clear();

    // The filter of the page: the same page, the current entry has the route, no
    // new entry, no announcement, no navigation, the window title follows
    const pdf::PDFAppRoute filtered = pdf::PDFAppRoutes::recent("pinned", "name", "report");
    QVERIFY(page->replaceRouteTo(filtered));
    QCOMPARE(fixture.currentPage(), page.data());
    QCOMPARE(page->getRoute(), filtered);
    QCOMPARE(page->setRouteCount, 1);
    QCOMPARE(recent->createCount, 1);
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), entryCount);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, filtered);
    QCOMPARE(replacedSpy.count(), 1);
    QCOMPARE(replacedSpy[0].at(0).value<pdf::PDFAppTab*>(), fixture.currentTab());
    QCOMPARE(replacedSpy[0].at(1).value<pdf::PDFAppRoute>(), filtered);
    QCOMPARE(navigatedSpy.count(), 0);
    QCOMPARE(fixture.navigator()->getLastAnnouncement(), announcement);
    QVERIFY(fixture.window->windowTitle().startsWith(QStringLiteral("Recent")));

    // The same route again does nothing
    QVERIFY(page->replaceRouteTo(filtered));
    QCOMPARE(replacedSpy.count(), 1);

    // An invalid route is ignored, a route of another page or with a refused value
    // is refused (the history keeps its entry)
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("invalid route"));
    QVERIFY(page->replaceRouteTo(pdf::PDFAppRoute::parse(QStringLiteral("recent"))));
    QCOMPARE(page->getRoute(), filtered);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("route of another page"));
    QVERIFY(page->replaceRouteTo(pdf::PDFAppRoutes::about()));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, filtered);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("route of another page"));
    QVERIFY(page->replaceRouteTo(pdf::PDFAppRoute::parse(QStringLiteral("/recent?filter=everything"))));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, filtered);
    QCOMPARE(replacedSpy.count(), 1);
    QVERIFY(page->replaceRouteTo(filtered));
    QCOMPARE(replacedSpy.count(), 2);

    // Back and Forward show the filtered route again (a new page of it with the
    // state of the view saved, when the tab left it)
    QVERIFY(fixture.navigator()->goBack());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::about());
    QVERIFY(fixture.navigator()->goForward());
    QPointer<NavigationTestPage> forwardPage = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(forwardPage && forwardPage != page);
    QCOMPARE(forwardPage->getRoute(), filtered);
    QCOMPARE(forwardPage->value, 7);

    // A page, which is not the shown page of a tab, or a page outside of a tab
    // changes only its own route
    QTRY_VERIFY(page.isNull());
    {
        NavigationTestPage loosePage(fixture.context.get(), nullptr, QStringLiteral("Loose"), true);
        QVERIFY(loosePage.replaceRouteTo(pdf::PDFAppRoutes::recent("files")));
        QCOMPARE(loosePage.getRoute(), pdf::PDFAppRoutes::recent("files"));
    }
    QCOMPARE(replacedSpy.count(), 2);

    // A page replacing its route from setRoute (it is not shown yet): only the page
    recent->onSetRoute = [](NavigationTestPage* recentPage) { recentPage->replaceRouteTo(pdf::PDFAppRoutes::recent("folders")); };
    fixture.navigate("/recent?filter=files");
    QCOMPARE(fixture.currentPage()->getRoute(), pdf::PDFAppRoutes::recent("folders"));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::recent("files"));
    recent->onSetRoute = nullptr;
    QCOMPARE(replacedSpy.count(), 2);

    // The page deleted by a slot of its signal: replaceRoute reports it
    QPointer<NavigationTestPage> deletedPage = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(deletedPage);
    QObject::connect(deletedPage.data(), &pdf::PDFAppPage::routeReplaced, fixture.window.data(), [&deletedPage]() { delete deletedPage.data(); });
    QVERIFY(!deletedPage->replaceRouteTo(pdf::PDFAppRoutes::recent("workspaces")));
    QVERIFY(deletedPage.isNull());

    // The window deleted by a slot of routeReplaced of the navigator
    fixture.navigate("/recent");
    QPointer<NavigationTestPage> lastPage = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(lastPage);
    QObject::connect(fixture.navigator(), &pdf::PDFAppNavigator::routeReplaced, fixture.context.get(), [&fixture]() { delete fixture.window.data(); });
    QVERIFY(!lastPage->replaceRouteTo(pdf::PDFAppRoutes::recent("pinned")));
    QVERIFY(fixture.window.isNull());
}

/// Factory of a tool page, whose destructor uses the registry of the tool pages
class NavigationRegistryUserFactory : public NavigationTestFactory
{
public:
    NavigationRegistryUserFactory() : NavigationTestFactory(QStringLiteral("Tool")) { }
    ~NavigationRegistryUserFactory() { hadPage = pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge")); }

    static inline bool hadPage = false;
};

/// A tool page factory, whose page depends on more of the route (it refuses every replacement)
class NavigationStrictToolFactory : public NavigationTestFactory
{
public:
    NavigationStrictToolFactory() : NavigationTestFactory(QStringLiteral("Strict")) { }

    virtual bool isSamePage(const pdf::PDFAppRoute& pageRoute, const pdf::PDFAppRoute& route) const override
    {
        Q_UNUSED(pageRoute);
        Q_UNUSED(route);
        return false;
    }
};

void AppNavigationTest::replaceRouteToolPages()
{
    // The pages of the tools (step 6) are chosen by the route (createPageForRoute):
    // all of them match /tools/{toolId}, the page of a tool replaces its route only
    // by a route of the same tool
    NavigationFixture fixture;
    auto merge = std::make_shared<NavigationTestFactory>("Merge");
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"), merge));
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.split"), std::make_shared<NavigationTestFactory>("Split")));
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/tools/{toolId}"), std::make_shared<pdf::PDFAppToolRoutePageFactory>(pdf::PDFAppToolPageKind::Start));

    fixture.navigate("/tools/pages.merge");
    QPointer<NavigationTestPage> page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(page->getTitle(), QStringLiteral("Merge"));

    QSignalSpy replacedSpy(fixture.navigator(), &pdf::PDFAppNavigator::routeReplaced);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("route of another page"));
    QVERIFY(page->replaceRouteTo(pdf::PDFAppRoutes::tool(QStringLiteral("pages.split"))));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")));
    QCOMPARE(replacedSpy.count(), 0);

    // Back and Forward still show the page of Merge
    fixture.navigate("/about");
    QVERIFY(fixture.navigator()->goBack());
    QCOMPARE(fixture.currentPage()->getTitle(), QStringLiteral("Merge"));

    pdf::PDFAppToolRoutePageFactory start(pdf::PDFAppToolPageKind::Start);
    QVERIFY(start.isSamePage(pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")), pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge"))));
    QVERIFY(!start.isSamePage(pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")), pdf::PDFAppRoutes::tool(QStringLiteral("pages.split"))));
    pdf::PDFAppToolRoutePageFactory document(pdf::PDFAppToolPageKind::Document);
    QVERIFY(document.isSamePage(pdf::PDFAppRoute::parse(QStringLiteral("/document/a/tool/pages.merge")), pdf::PDFAppRoute::parse(QStringLiteral("/document/a/tool/pages.merge"))));
    QVERIFY(!document.isSamePage(pdf::PDFAppRoute::parse(QStringLiteral("/document/a/tool/pages.merge")), pdf::PDFAppRoute::parse(QStringLiteral("/document/b/tool/pages.merge"))));
    QVERIFY(!document.isSamePage(pdf::PDFAppRoute::parse(QStringLiteral("/document/a/tool/pages.merge")), pdf::PDFAppRoute::parse(QStringLiteral("/document/a/tool/pages.split"))));

    // The registered factory, which creates the page, decides too (review RC6-1)
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.split"), std::make_shared<NavigationStrictToolFactory>()));
    QVERIFY(!start.isSamePage(pdf::PDFAppRoutes::tool(QStringLiteral("pages.split")), pdf::PDFAppRoutes::tool(QStringLiteral("pages.split"))));
    QVERIFY(start.isSamePage(pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")), pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge"))));

    // A replaced factory is released outside of the lock of the registry: its
    // destructor may use the registry (a deadlock otherwise)
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"), std::make_shared<NavigationRegistryUserFactory>()));
    NavigationRegistryUserFactory::hadPage = false;
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"), merge));
    QVERIFY(NavigationRegistryUserFactory::hadPage);
    pdf::PDFAppToolPageRegistry::clear();
}

void AppNavigationTest::deletionDuringNavigation()
{
    // The window is deleted by the factory of the page
    {
        NavigationFixture fixture;
        auto factory = std::make_shared<NavigationTestFactory>("About");
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        factory->onCreate = [window]() { delete window.data(); return false; };
        fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), factory);
        fixture.navigate("/about");
        QVERIFY(fixture.window.isNull());
    }

    // The window is deleted by setRoute of the page
    {
        NavigationFixture fixture;
        auto factory = std::make_shared<NavigationTestFactory>("About");
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        factory->onSetRoute = [window](NavigationTestPage*) { delete window.data(); };
        fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), factory);
        fixture.navigate("/about");
        QVERIFY(fixture.window.isNull());
    }

    // The window is deleted by a slot of the signal of a page (navigation from a page)
    {
        NavigationFixture fixture;
        auto factory = std::make_shared<NavigationTestFactory>("About");
        fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), factory);
        fixture.navigate("/about");
        NavigationTestPage* page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        connect(page, &pdf::PDFAppPage::navigationRequested, window.data(), [window]() { delete window.data(); });
        QVERIFY(!page->navigateTo(pdf::PDFAppRoutes::support()));
        QVERIFY(fixture.window.isNull());
    }

    // The window is deleted, while it announces the navigation
    {
        NavigationFixture fixture;
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        connect(fixture.navigator(), &pdf::PDFAppNavigator::navigated, window.data(), [window]() { delete window.data(); });
        fixture.navigate("/ai");
        QVERIFY(fixture.window.isNull());
    }

    // The tab New tab is deleted by the page shown in it and from the slot of the navigation
    {
        NavigationFixture fixture;
        auto factory = std::make_shared<NavigationTestFactory>("Open");
        fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/open"), factory);
        factory->onSetRoute = [](NavigationTestPage* page) { delete page->parentWidget(); };
        QVERIFY(!fixture.window->openNewTab());
        QCOMPARE(fixture.window->getTabs().size(), 1);
        factory->onSetRoute = nullptr;

        pdf::PDFAppTab* newTab = fixture.window->openNewTab();
        QVERIFY(newTab);
        QCOMPARE(fixture.window->getTabs().size(), 2);
        QPointer<pdf::PDFAppTab> tabGuard(newTab);
        connect(fixture.navigator(), &pdf::PDFAppNavigator::navigated, newTab, [tabGuard]() { delete tabGuard.data(); }, Qt::SingleShotConnection);
        fixture.navigate("/open");
        QVERIFY(tabGuard.isNull());
        QCOMPARE(fixture.window->getTabs().size(), 1);
        QVERIFY(fixture.window->getCurrentTab());
    }

    // The Home tab is deleted: it is created again by the next navigation
    {
        NavigationFixture fixture;
        delete fixture.window->getHomeTab();
        QCOMPARE(fixture.window->getTabs().size(), 0);
        fixture.navigate("/ai");
        QCOMPARE(fixture.window->getTabs().size(), 1);
        QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    }

    // The page is deleted by the application: the next navigation works
    {
        NavigationFixture fixture;
        delete fixture.currentPage();
        QVERIFY(!fixture.currentPage());
        fixture.navigate("/ai");
        QVERIFY(fixture.currentPage());
        QVERIFY(fixture.navigator()->goBack());
        QVERIFY(fixture.currentPage());
    }
}

void AppNavigationTest::deletionInAnyOrder()
{
    // Parts of the window deleted first, then the window is used and deleted
    const QStringList parts = { "topBar", "tabContent", "sidebar", "workspaceHeader", "storageBanner", "footer", "pageArea", "navigation", "themeSwitch", "windowControls", "tabBar", "newTabButton" };
    for (const QString& part : parts)
    {
        NavigationFixture fixture;
        fixture.window->openNewTab();
        QWidget* widget = fixture.window->findChild<QWidget*>(part);
        QVERIFY2(widget, qPrintable(part));
        delete widget;

        fixture.navigate("/ai");
        fixture.navigate("/open", pdf::PDFAppNavigationMode::NewTab);
        fixture.navigator()->goBack();
        fixture.window->focusSearch();
        fixture.window->toggleMaximized();
        fixture.window->toggleMaximized();
        for (const QString& id : { pdf::PDFAppCommands::newTab(), pdf::PDFAppCommands::nextTab(), pdf::PDFAppCommands::previousTab(), pdf::PDFAppCommands::closeTab(),
                                   pdf::PDFAppCommands::back(), pdf::PDFAppCommands::forward(), pdf::PDFAppCommands::search() })
        {
            if (QAction* action = fixture.window->getCommandAction(id))
            {
                action->trigger();
            }
        }
        if (pdf::PDFAppTopBar* topBar = fixture.window->getTopBar())
        {
            topBar->createHitTestInput(fixture.window, QPoint(10, 10));
        }
        if (fixture.window->getFrame())
        {
            fixture.window->getFrame()->hitTest(QPoint(10, 10));
        }
        QCoreApplication::processEvents();
    }

    // The navigator and the frame deleted by the application
    {
        NavigationFixture fixture;
        delete fixture.window->getNavigator();
        delete fixture.window->getFrame();
        fixture.window->openNewTab();
        fixture.window->focusSearch();
        QTest::keyClick(fixture.window.data(), Qt::Key_Left, Qt::AltModifier);
        QVERIFY(fixture.window->close());
    }

    // The context deleted before the window
    {
        NavigationFixture fixture;
        fixture.context.reset();
        fixture.navigate("/ai");
        fixture.window->openNewTab();
        QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
        QVERIFY(fixture.window->close());
    }

    // Tabs deleted in the reverse order and in the order of creation
    for (const bool reverse : { false, true })
    {
        NavigationFixture fixture;
        for (int i = 0; i < 4; ++i)
        {
            fixture.window->openNewTab();
        }
        QList<pdf::PDFAppTab*> tabs = fixture.window->getTabs();
        QCOMPARE(tabs.size(), 5);
        if (reverse)
        {
            std::reverse(tabs.begin(), tabs.end());
        }
        for (pdf::PDFAppTab* tab : tabs)
        {
            delete tab;
            QCoreApplication::processEvents();
        }
        QCOMPARE(fixture.window->getTabBar()->count(), 0);
        fixture.navigate("/home");
        QCOMPARE(fixture.window->getTabs().size(), 1);
    }

    // Widgets created without the window are deleted alone
    {
        NavigationFixture fixture(false);
        delete new pdf::PDFAppTopBar();
        delete new pdf::PDFAppWorkspaceTab(pdf::PDFAppTabKind::Home, fixture.context.get());
        delete new pdf::PDFAppWorkspaceTab(pdf::PDFAppTabKind::Document, nullptr);
        delete new pdf::PDFAppSidebar(nullptr);
        delete new pdf::PDFAppFooter();
        delete new pdf::PDFAppStorageBanner();
        pdf::PDFAppWorkspaceTab tab(pdf::PDFAppTabKind::NewTab, nullptr);
        QCOMPARE(tab.getKind(), pdf::PDFAppTabKind::NewTab);
        QVERIFY(!tab.getSidebar());
        QVERIFY(!tab.getBanner());
        QVERIFY(!tab.getFooter());
        QCOMPARE(tab.getTabTitle(), QStringLiteral("New tab"));
        QVERIFY(tab.isClosable());
        tab.setPage(new NavigationTestPage(nullptr, nullptr, "x", true));
        QVERIFY(tab.getPage());
        tab.setPage(nullptr);
        QVERIFY(!tab.getPage());
    }
}

void AppNavigationTest::helpTab()
{
    // The manual opens in its own Help tab (owner decision O13): at most one per
    // window, reused by the next help link, closable, without the sidebar; links to
    // other routes go to the Home tab
    NavigationFixture fixture;
    auto help = std::make_shared<NavigationTestFactory>("Help topic");
    auto recent = std::make_shared<NavigationTestFactory>("Recent");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/help/{topic}"), help);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/recent"), recent);
    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    QVERIFY(!fixture.window->findHelpTab());

    // F1 opens the default topic in a new Help tab
    QAction* helpAction = fixture.window->getCommandAction(pdf::PDFAppCommands::help());
    QVERIFY(helpAction);
    QCOMPARE(helpAction->shortcut(), QKeySequence(Qt::Key_F1));
    helpAction->trigger();
    pdf::PDFAppTab* helpTab = fixture.window->findHelpTab();
    QVERIFY(helpTab);
    QCOMPARE(fixture.currentTab(), helpTab);
    QCOMPARE(helpTab->getKind(), pdf::PDFAppTabKind::Help);
    QCOMPARE(helpTab->objectName(), QStringLiteral("helpTab"));
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->tabText(1), QStringLiteral("Help"));
    QVERIFY(tabBar->isTabClosable(1));
    QVERIFY(helpTab->isClosable());
    QCOMPARE(helpTab->getTabIconName(), QStringLiteral("help-circle"));
    pdf::PDFAppWorkspaceTab* workspaceTab = static_cast<pdf::PDFAppWorkspaceTab*>(helpTab);
    QVERIFY(!workspaceTab->getSidebar());
    QVERIFY(!workspaceTab->getFooter());
    QVERIFY(workspaceTab->getHeader());
    QVERIFY(helpTab->getSearchField());
    QCOMPARE(helpTab->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));
    QCOMPARE(qobject_cast<NavigationTestPage*>(helpTab->getPage())->getTitle(), QStringLiteral("Help topic"));

    // Another help link from the Home tab reuses the tab (its own history)
    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.navigate("/help/window-and-tabs#tabs");
    QCOMPARE(fixture.currentTab(), helpTab);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(helpTab->getHistory().getCount(), 2);
    QCOMPARE(fixture.window->getHomeTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::home());

    // Back / Forward between the topics of the Help tab
    QVERIFY(fixture.navigator()->goBack());
    QCOMPARE(fixture.currentTab(), helpTab);
    QCOMPARE(helpTab->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));
    QVERIFY(fixture.navigator()->goForward());
    QCOMPARE(helpTab->getHistory().getCurrentEntry()->route.getFragment(), QStringLiteral("tabs"));

    // A link of the help page to another route goes to the Home tab
    qobject_cast<NavigationTestPage*>(helpTab->getPage())->navigateTo(pdf::PDFAppRoutes::recent());
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Recent"));
    QCOMPARE(helpTab->getHistory().getCount(), 2);

    // A help link from the help page stays in the tab; an unknown topic is Page Not
    // Found in the Help tab
    fixture.window->activateTab(helpTab);
    qobject_cast<NavigationTestPage*>(helpTab->getPage())->navigateTo(pdf::PDFAppRoutes::help(QStringLiteral("settings")));
    QCOMPARE(fixture.currentTab(), helpTab);
    help->onCreate = []() { return false; };
    fixture.navigate("/help/unknown");
    QCOMPARE(fixture.currentTab(), helpTab);
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(helpTab->getPage()));
    help->onCreate = nullptr;

    // Ctrl+W closes it, the next help link creates a new one
    QPointer<pdf::PDFAppTab> closedTab = helpTab;
    fixture.window->getCommandAction(pdf::PDFAppCommands::closeTab())->trigger();
    QTRY_VERIFY(closedTab.isNull());
    QCOMPARE(tabBar->count(), 1);
    QVERIFY(!fixture.window->findHelpTab());
    fixture.navigate("/help/search");
    QVERIFY(fixture.window->findHelpTab());
    QCOMPARE(fixture.window->findHelpTab()->getHistory().getCount(), 1);

    // The page of the Help tab deletes its tab from its own navigation (close from setRoute)
    help->onSetRoute = [&fixture](NavigationTestPage* page)
    {
        pdf::PDFAppTab* tab = fixture.window->findHelpTab();
        if (tab && page->getRoute().getSegment(1) == QStringLiteral("close"))
        {
            fixture.window->closeTab(tab);
        }
    };
    fixture.navigate("/help/close");
    QCoreApplication::processEvents();
    QTRY_VERIFY(!fixture.window->findHelpTab());
    help->onSetRoute = nullptr;

    // The Help tab deleted by the application, then the window (in both orders)
    fixture.navigate("/help/a");
    delete fixture.window->findHelpTab();
    QCoreApplication::processEvents();
    QVERIFY(!fixture.window->findHelpTab());
    fixture.navigate("/help/b");
    QVERIFY(fixture.window->findHelpTab());
    QCOMPARE(fixture.currentTab(), fixture.window->findHelpTab());
}

void AppNavigationTest::newTab()
{
    NavigationFixture fixture;
    auto open = std::make_shared<NavigationTestFactory>("Open");
    auto recent = std::make_shared<NavigationTestFactory>("Recent");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/open"), open);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/recent"), recent);

    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    QCOMPARE(tabBar->count(), 1);
    QVERIFY(!tabBar->isTabClosable(0));

    // The New tab button follows the last tab (the tab bar is not wider than its tabs)
    QToolButton* newTabButton = fixture.window->getTopBar()->getNewTabButton();
    QCOMPARE(newTabButton->x(), tabBar->geometry().right() + 1);
    QCOMPARE(tabBar->x(), 16);
    QCOMPARE(tabBar->width(), tabBar->sizeHint().width());
    QCOMPARE(tabBar->tabText(0), QStringLiteral("Home"));

    // + opens a tab New tab with /open, closable, without the sidebar
    QTest::mouseClick(fixture.window->getTopBar()->getNewTabButton(), Qt::LeftButton);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->currentIndex(), 1);
    QCOMPARE(tabBar->tabText(1), QStringLiteral("New tab"));
    QVERIFY(tabBar->isTabClosable(1));
    pdf::PDFAppTab* newTab = fixture.currentTab();
    QCOMPARE(newTab->getKind(), pdf::PDFAppTabKind::NewTab);
    QVERIFY(!static_cast<pdf::PDFAppWorkspaceTab*>(newTab)->getSidebar());
    QCOMPARE(qobject_cast<NavigationTestPage*>(newTab->getPage())->getTitle(), QStringLiteral("Open"));
    QCOMPARE(newTab->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::open());

    // Ctrl+T: another one (several are allowed)
    QAction* newTabAction = fixture.window->getCommandAction(pdf::PDFAppCommands::newTab());
    QCOMPARE(newTabAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_T));
    newTabAction->trigger();
    QCOMPARE(tabBar->count(), 3);
    QCOMPARE(tabBar->currentIndex(), 2);
    QTRY_COMPARE(tabBar->width(), tabBar->sizeHint().width());
    QCOMPARE(newTabButton->x(), tabBar->geometry().right() + 1);

    // /open from a New tab stays in it
    pdf::PDFAppTab* secondNewTab = fixture.currentTab();
    fixture.navigate("/open");
    QCOMPARE(fixture.currentTab(), secondNewTab);

    // Any other link switches to the Home tab and navigates there
    qobject_cast<NavigationTestPage*>(secondNewTab->getPage())->navigateTo(pdf::PDFAppRoutes::recent());
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(qobject_cast<NavigationTestPage*>(fixture.currentPage())->getTitle(), QStringLiteral("Recent"));
    QCOMPARE(secondNewTab->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::open());

    // The search of a New tab goes to the Home tab
    fixture.window->activateTab(secondNewTab);
    pdf::PDFLineEdit* search = secondNewTab->getSearchField();
    QVERIFY(search);
    search->setText(QStringLiteral("merge"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::search(QStringLiteral("merge")));

    // Next / previous tab (Ctrl+Tab / Ctrl+Shift+Tab) cycle
    QAction* next = fixture.window->getCommandAction(pdf::PDFAppCommands::nextTab());
    QAction* previous = fixture.window->getCommandAction(pdf::PDFAppCommands::previousTab());
    fixture.window->activateTab(fixture.window->getHomeTab());
    next->trigger();
    QCOMPARE(tabBar->currentIndex(), 1);
    previous->trigger();
    previous->trigger();
    QCOMPARE(tabBar->currentIndex(), 2);

    // Ctrl+W closes a New tab, never the Home tab
    QAction* closeAction = fixture.window->getCommandAction(pdf::PDFAppCommands::closeTab());
    QCOMPARE(closeAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
    QPointer<pdf::PDFAppTab> closed = fixture.currentTab();
    closeAction->trigger();
    QCOMPARE(tabBar->count(), 2);
    QTRY_VERIFY(closed.isNull());
    fixture.window->activateTab(fixture.window->getHomeTab());
    closeAction->trigger();
    QCOMPARE(tabBar->count(), 2);
    QVERIFY(!fixture.window->closeTab(fixture.window->getHomeTab()));
    QVERIFY(!fixture.window->closeTab(nullptr));

    // The close button of the tab
    QAbstractButton* closeButton = qobject_cast<QAbstractButton*>(tabBar->tabButton(1, QTabBar::RightSide));
    if (!closeButton)
    {
        closeButton = qobject_cast<QAbstractButton*>(tabBar->tabButton(1, QTabBar::LeftSide));
    }
    QVERIFY(closeButton);
    closeButton->click();
    QCOMPARE(tabBar->count(), 1);

    // Mode NewTab of another route goes to the Home tab
    fixture.navigate("/recent", pdf::PDFAppNavigationMode::NewTab);
    QCOMPARE(tabBar->count(), 1);

    // Tabs can be reordered; the content follows the tab, not the position
    fixture.window->openNewTab();
    pdf::PDFAppTab* movedTab = fixture.currentTab();
    tabBar->moveTab(1, 0);
    QCOMPARE(fixture.window->getTabs().first(), movedTab);
    tabBar->setCurrentIndex(1);
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.window->findChild<QStackedWidget*>(QStringLiteral("tabContent"))->currentWidget(), static_cast<QWidget*>(fixture.window->getHomeTab()));

    // The number of tabs is bounded
    for (int i = 0; i < pdf::PDFAppMainWindow::MAXIMUM_TABS + 5; ++i)
    {
        fixture.window->createNewTab();
    }
    QCOMPARE(tabBar->count(), pdf::PDFAppMainWindow::MAXIMUM_TABS);
    QVERIFY(!fixture.window->createNewTab());
    QVERIFY(!fixture.window->findDocumentTab(documentId()));
}

void AppNavigationTest::externalLinks()
{
    NavigationFixture fixture;
    QSignalSpy opened(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    fixture.navigator()->navigateLink(QStringLiteral("https://github.com/JakubMelka/PDF4QT"));
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.last().at(0).toUrl(), QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT")));

    // Only https (and a local folder); anything else is a route or is refused
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not allowed"));
    QVERIFY(!fixture.navigator()->openExternalUrl(QUrl(QStringLiteral("http://example.com"))));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not allowed"));
    QVERIFY(!fixture.navigator()->openExternalUrl(QUrl(QStringLiteral("https://user:secret@example.com"))));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not allowed"));
    QVERIFY(!fixture.navigator()->openExternalUrl(QUrl(QStringLiteral("file:///nonexistent/file.pdf"))));
    QVERIFY(pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl::fromLocalFile(QDir::tempPath())));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl(QStringLiteral("javascript:alert(1)"))));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl()));
    fixture.navigator()->navigateLink(QStringLiteral("javascript:alert(1)"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCOMPARE(opened.count(), 1);

    // A folder of another computer is never touched (SMB would get the credentials
    // of the user): file://server/share, an UNC path - also of this computer, whose
    // administrative share exists -, a device path
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl(QStringLiteral("file://server.invalid/share"))));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl::fromLocalFile(QStringLiteral("//localhost/C$/Windows"))));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl::fromLocalFile(QStringLiteral("\\\\localhost\\C$\\Windows"))));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl::fromLocalFile(QStringLiteral("\\\\?\\C:\\Windows"))));
    QVERIFY(!pdf::PDFAppNavigator::isExternalUrlAllowed(QUrl(QStringLiteral("file:relative/folder"))));

    // An overlong address is refused, not shortened into another one
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not allowed"));
    fixture.navigator()->navigateLink(QStringLiteral("https://example.com/") + QString(pdf::PDFAppRoute::MAXIMUM_LENGTH, QLatin1Char('a')));
    QCOMPARE(opened.count(), 1);

    // MIT License of the footer
    pdf::PDFAppFooter* footer = fixture.window->getHomeTab()->getFooter();
    QTest::mouseClick(footer->getLicenseLink(), Qt::LeftButton);
    QCOMPARE(opened.count(), 2);
    QCOMPARE(opened.last().at(0).toUrl(), pdf::PDFAppFooter::getLicenseUrl());

    // The version has three parts (1.6.0 of 1.6.0.0, newapp_whatsnew.md 2)
    const QString version = pdf::PDFAppFooter::getDisplayVersion();
    QCOMPARE(version.count(QLatin1Char('.')), 2);
    QVERIFY(QStringLiteral(PDF4QT_PROJECT_VERSION).startsWith(version));
    QVERIFY2(footer->getVersionLabel()->text().endsWith(QStringLiteral("Version %1").arg(version)), qPrintable(footer->getVersionLabel()->text()));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("1.6.0.0")), QStringLiteral("1.6.0"));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("10.20.30.40")), QStringLiteral("10.20.30"));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("1.6")), QStringLiteral("1.6.0"));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("2")), QStringLiteral("2.0.0"));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("1.6.0.0-rc1")), QStringLiteral("1.6.0.0-rc1"));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QString()), QString());
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QString(100, QLatin1Char('x'))).size(), 32);

    // An untrusted text: one line, a surrogate pair is never cut at the limit
    const QString emoji = QString::fromUcs4(U"\U0001F600");
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QString(31, QLatin1Char('x')) + emoji + QStringLiteral("yy")), QString(31, QLatin1Char('x')));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QString(30, QLatin1Char('x')) + emoji + QStringLiteral("yy")), QString(30, QLatin1Char('x')) + emoji);
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion(QStringLiteral("1.6\nbeta") + QChar(0x202E) + QChar(0x200B) + QStringLiteral("\t1")), QStringLiteral("1.6beta1"));
}

void AppNavigationTest::themeSwitch()
{
    NavigationFixture fixture;
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    pdf::PDFAppSidebar* sidebar = fixture.window->getHomeTab()->getSidebar();
    pdf::PDFSegmentedControl* themeSwitch = sidebar->getThemeSwitch();
    QCOMPARE(themeSwitch->getCount(), 3);
    QCOMPARE(themeSwitch->getCurrentIndex(), 2);    // Auto (default)
    QCOMPARE(themeSwitch->width(), 182);

    // The switch writes the setting and the theme follows
    themeSwitch->setCurrentIndex(1);
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::colorScheme()), QStringLiteral("dark"));
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    themeSwitch->setCurrentIndex(0);
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::colorScheme()), QStringLiteral("light"));
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);

    // Another connection (another thread or instance) changes the setting: the switch follows
    pdf::PDFDatabaseError error;
    sqlite3* handle = pdf::PDFDatabase::openMemoryConnection(fixture.memoryName, &error);
    QVERIFY2(handle, qPrintable(error.toString()));
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'dark', 1)").isError());
    QTRY_COMPARE(themeSwitch->getCurrentIndex(), 1);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);

    // An invalid stored value means the default (Auto)
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'purple', 1)").isError());
    QTRY_COMPARE(themeSwitch->getCurrentIndex(), 2);
    pdf::PDFDatabase::closeHandle(handle);

    // Mapping of the values
    QCOMPARE(pdf::PDFAppSidebar::getSchemeIndex(QStringLiteral("light")), 0);
    QCOMPARE(pdf::PDFAppSidebar::getSchemeIndex(QStringLiteral("dark")), 1);
    QCOMPARE(pdf::PDFAppSidebar::getSchemeIndex(QStringLiteral("auto")), 2);
    QCOMPARE(pdf::PDFAppSidebar::getSchemeIndex(QStringLiteral("x")), 2);
    QCOMPARE(pdf::PDFAppSidebar::getSchemeValue(0), QStringLiteral("light"));
    QCOMPARE(pdf::PDFAppSidebar::getSchemeValue(1), QStringLiteral("dark"));
    QCOMPARE(pdf::PDFAppSidebar::getSchemeValue(7), QStringLiteral("auto"));

    // The navigation has three groups as the design
    pdf::PDFNavList* navigation = sidebar->getNavigation();
    QCOMPARE(navigation->getCount(), 10);
    QStringList texts;
    for (int i = 0; i < navigation->getCount(); ++i)
    {
        texts << navigation->getItem(i)->text();
    }
    QCOMPARE(texts, QStringList({ "Home", "Open", "Recent", "Tools", "AI", "What's New", "About", "Support", "Sponsor", "Settings" }));
    QCOMPARE(sidebar->width(), pdf::PDFAppSidebar::WIDTH);
}

void AppNavigationTest::themeSwitchSystemScheme_data()
{
    QTest::addColumn<bool>("synchronous");
    QTest::newRow("synchronous (Windows)") << true;
    QTest::newRow("event loop (GNOME, GTK)") << false;
}

void AppNavigationTest::themeSwitchSystemScheme()
{
#ifdef PDF4QT_TEST_PLATFORM_THEME
    // A platform theme, which keeps the scheme requested by the application (as QWindowsTheme):
    // a scheme requested for Light / Dark must not hide the scheme of the system from Auto
    QFETCH(bool, synchronous);
    ColorSchemeTestTheme platformTheme(Qt::ColorScheme::Light, synchronous);
    NavigationFixture fixture;
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    pdf::PDFSegmentedControl* themeSwitch = fixture.window->getHomeTab()->getSidebar()->getThemeSwitch();
    QCOMPARE(themeSwitch->getCurrentIndex(), 2);    // Auto (default)
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);

    // Dark, then Auto with a light system: the theme of the system (report of the owner)
    themeSwitch->setCurrentIndex(1);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QTRY_COMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Dark);    // native dialogs, title bars
    themeSwitch->setCurrentIndex(2);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    QTRY_COMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Light);

    // Auto follows a change of the system
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    // No scheme of the system (Windows in high contrast reports Unknown): Light
    platformTheme.setSystemScheme(Qt::ColorScheme::Unknown);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    platformTheme.setSystemScheme(Qt::ColorScheme::Light);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);

    // Light, then Auto with a dark system
    themeSwitch->setCurrentIndex(0);
    QTRY_COMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Light);
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    QTest::qWait(50);
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    themeSwitch->setCurrentIndex(2);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);

    // A chosen theme does not follow the system
    themeSwitch->setCurrentIndex(0);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    platformTheme.setSystemScheme(Qt::ColorScheme::Light);
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    QTest::qWait(50);
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Light);
    themeSwitch->setCurrentIndex(1);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    platformTheme.setSystemScheme(Qt::ColorScheme::Light);
    QTest::qWait(50);
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Dark);

    // Another connection writes Auto: the scheme of the system again
    pdf::PDFDatabaseError error;
    sqlite3* handle = pdf::PDFDatabase::openMemoryConnection(fixture.memoryName, &error);
    QVERIFY2(handle, qPrintable(error.toString()));
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'auto', 1)").isError());
    pdf::PDFDatabase::closeHandle(handle);
    QTRY_COMPARE(themeSwitch->getCurrentIndex(), 2);
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);

    // The requests converge (a change of the scheme notifies the context, which applies
    // the setting again), they do not repeat endlessly
    const int requestCount = platformTheme.getRequestCount();
    QTest::qWait(100);
    QCOMPARE(platformTheme.getRequestCount(), requestCount);
#else
    QSKIP("Needs the private headers of QtGui (Qt6::GuiPrivate)");
#endif
}

void AppNavigationTest::themeSwitchTestThemeRestored()
{
#ifdef PDF4QT_TEST_PLATFORM_THEME
    // The platform theme of a test passes its last request to the theme it has replaced.
    // The outer theme plays the native theme of a dark system, which keeps a request
    // (QWindowsTheme): the scheme requested before the test theme was installed would
    // hide the dark system from Auto of the next test (themeSwitchNativeScheme of a serial
    // run after themeSwitchSystemScheme, "-platform windows", dark system)
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    ColorSchemeTestTheme nativeTheme(Qt::ColorScheme::Dark);
    themeManager->setFollowingSystemColorScheme(false);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QCOMPARE(nativeTheme.getRequestedScheme(), Qt::ColorScheme::Light);

    {
        ColorSchemeTestTheme testTheme(Qt::ColorScheme::Light);
        NavigationFixture fixture;      // Auto
        QVERIFY(themeManager->isFollowingSystemColorScheme());
        QCOMPARE(testTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
        QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    }
    QCOMPARE(nativeTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);

    // The next window with Auto (the theme manager follows the system already, nothing is requested)
    NavigationFixture fixture;
    QVERIFY(themeManager->isFollowingSystemColorScheme());
    QTRY_COMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
#else
    QSKIP("Needs the private headers of QtGui (Qt6::GuiPrivate)");
#endif
}

void AppNavigationTest::themeSwitchNativeScheme()
{
    // The platform theme of the run: offscreen ignores a requested scheme, run this test also
    // by "UnitTestsAppNavigation -platform windows themeSwitchNativeScheme" (QWindowsTheme
    // keeps the requested scheme and reports it as the scheme of the system)
    NavigationFixture fixture;
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    pdf::PDFSegmentedControl* themeSwitch = fixture.window->getHomeTab()->getSidebar()->getThemeSwitch();
    const pdf::PDFThemeColors::Scheme systemScheme = m_systemColorScheme == Qt::ColorScheme::Dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light;
    const pdf::PDFThemeColors::Scheme otherScheme = systemScheme == pdf::PDFThemeColors::Scheme::Dark ? pdf::PDFThemeColors::Scheme::Light : pdf::PDFThemeColors::Scheme::Dark;
    QTRY_COMPARE(themeManager->getTheme().getScheme(), systemScheme);

    for (int i = 0; i < 2; ++i)
    {
        // The scheme other than the scheme of the system, then Auto
        themeSwitch->setCurrentIndex(otherScheme == pdf::PDFThemeColors::Scheme::Dark ? 1 : 0);
        QTRY_COMPARE(themeManager->getTheme().getScheme(), otherScheme);
        themeSwitch->setCurrentIndex(2);
        QTRY_COMPARE(themeManager->getTheme().getScheme(), systemScheme);
        if (m_systemColorScheme != Qt::ColorScheme::Unknown)
        {
            QTRY_COMPARE(QGuiApplication::styleHints()->colorScheme(), m_systemColorScheme);
        }
    }
}

void AppNavigationTest::shortcutsFromDatabase()
{
    NavigationFixture fixture;
    QAction* newTab = fixture.window->getCommandAction(pdf::PDFAppCommands::newTab());
    QAction* search = fixture.window->getCommandAction(pdf::PDFAppCommands::search());
    QVERIFY(newTab);
    QVERIFY(search);
    QCOMPARE(search->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
    QVERIFY(!fixture.window->getCommandAction(QStringLiteral("unknown.command")));
    for (const QString& id : { pdf::PDFAppCommands::newTab(), pdf::PDFAppCommands::closeTab(), pdf::PDFAppCommands::nextTab(), pdf::PDFAppCommands::previousTab(),
                               pdf::PDFAppCommands::quit(), pdf::PDFAppCommands::back(), pdf::PDFAppCommands::forward(), pdf::PDFAppCommands::search() })
    {
        QAction* action = fixture.window->getCommandAction(id);
        QVERIFY2(action, qPrintable(id));
        QVERIFY(fixture.window->actions().contains(action));
        QVERIFY(!action->shortcut().isEmpty());
    }

    // A shortcut of the user (own connection)
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::newTab(), QStringLiteral("Ctrl+N")).isError());
    QTRY_COMPARE(newTab->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_N));

    // A shortcut removed by the user
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::newTab(), QString()).isError());
    QTRY_VERIFY(newTab->shortcut().isEmpty());

    // Another connection; an invalid stored text means the default
    pdf::PDFDatabaseError error;
    sqlite3* handle = pdf::PDFDatabase::openMemoryConnection(fixture.memoryName, &error);
    QVERIFY(handle);
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT OR REPLACE INTO shortcut(command_id, keys) VALUES ('navigation.search', 'Ctrl+E')").isError());
    QTRY_COMPARE(search->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_E));
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT OR REPLACE INTO shortcut(command_id, keys) VALUES ('navigation.search', 'Bogus+++Key')").isError());
    QTRY_COMPARE(search->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "DELETE FROM shortcut").isError());
    QTRY_COMPARE(newTab->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_T));
    pdf::PDFDatabase::closeHandle(handle);

    // The key works
    const int count = fixture.window->getTabBar()->count();
    QTest::keyClick(fixture.window.data(), Qt::Key_T, Qt::ControlModifier);
    QCOMPARE(fixture.window->getTabBar()->count(), count + 1);
}

void AppNavigationTest::searchField()
{
    NavigationFixture fixture;
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    pdf::PDFLineEdit* search = homeTab->getSearchField();
    QVERIFY(search);
    QCOMPARE(search->placeholderText(), QStringLiteral("Search tools, documents, or help…"));
    QCOMPARE(search->width(), 320);

    // Ctrl+K focuses it (also from a New tab, which has its own field)
    fixture.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    search->setText(QStringLiteral("old"));
    QTest::keyClick(fixture.window.data(), Qt::Key_K, Qt::ControlModifier);
    QVERIFY(search->hasFocus());
    QCOMPARE(search->selectedText(), QStringLiteral("old"));

    // Enter navigates /search?q= (Page Not Found until step 2); an empty text does nothing
    search->setText(QStringLiteral("  compress   pdf "));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::search(QStringLiteral("compress pdf")));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    const int count = fixture.currentTab()->getHistory().getCount();
    search->setText(QStringLiteral("   "));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), count);

    // A page without the header (Settings): Ctrl+K uses the Home tab of the field, hidden header = no field
    auto settings = std::make_shared<NavigationTestFactory>("Settings");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/settings/{section}"), settings);
    fixture.navigate("/settings/general");
    QVERIFY(fixture.currentPage());
    QVERIFY(homeTab->getHeader()->isVisible());

    fixture.window->openNewTab();
    pdf::PDFLineEdit* newTabSearch = fixture.currentTab()->getSearchField();
    QVERIFY(newTabSearch);
    QTest::keyClick(fixture.window.data(), Qt::Key_K, Qt::ControlModifier);
    QVERIFY(newTabSearch->hasFocus());
}

void AppNavigationTest::banners_data()
{
    using Kind = pdf::PDFAppStorageBanner::Kind;
    using Mode = pdf::PDFDatabaseStorageMode;

    QTest::addColumn<int>("mode");
    QTest::addColumn<QString>("damaged");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("tag");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QStringList>("buttons");

    QTest::newRow("disk") << int(Mode::Disk) << QString() << int(Kind::None) << QString() << QString() << QStringList();
    QTest::newRow("portable") << int(Mode::Portable) << QString() << int(Kind::Portable) << "Portable"
                              << QString::fromUtf8("Portable mode – settings and history are kept only until PDF4QT closes") << QStringList();
    QTest::newRow("fallback") << int(Mode::MemoryFallback) << QString() << int(Kind::Fallback) << "Not saved"
                              << QString::fromUtf8("Settings could not be opened – changes are kept only until PDF4QT closes") << QStringList({ "bannerTryAgain", "bannerShowInFolder" });
    QTest::newRow("read only") << int(Mode::ReadOnlyNewerSchema) << QString() << int(Kind::ReadOnly) << "Read only"
                               << QString::fromUtf8("Settings are read only – they were saved by a newer PDF4QT") << QStringList({ "bannerCheckForUpdates" });
    QTest::newRow("replaced") << int(Mode::Disk) << "C:/data/pdf4qt.db.damaged-2026-10-18" << int(Kind::Replaced) << QString()
                              << QStringLiteral("The settings were damaged and have been reset") << QStringList({ "bannerShowInFolder", "bannerDismiss" });
}

void AppNavigationTest::banners()
{
    QFETCH(int, mode);
    QFETCH(QString, damaged);
    QFETCH(int, kind);
    QFETCH(QString, tag);
    QFETCH(QString, title);
    QFETCH(QStringList, buttons);

    pdf::PDFDatabaseState state;
    state.mode = static_cast<pdf::PDFDatabaseStorageMode>(mode);
    state.filePath = mode == int(pdf::PDFDatabaseStorageMode::Portable) ? QString() : QStringLiteral("C:/data/pdf4qt.db");
    state.damagedFilePath = damaged;
    state.error = pdf::PDFDatabaseError::fromMessage(QStringLiteral("disk I/O error"));

    QCOMPARE(int(pdf::PDFAppStorageBanner::getKind(state)), kind);

    QWidget host;
    QVBoxLayout* layout = new QVBoxLayout(&host);
    pdf::PDFAppStorageBanner* banner = new pdf::PDFAppStorageBanner(&host);
    pdf::PDFAppFooter* footer = new pdf::PDFAppFooter(&host);
    layout->addWidget(banner);
    layout->addWidget(footer);
    host.resize(842, 300);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    banner->setState(state);
    footer->setState(state);
    QCOMPARE(int(banner->getKind()), kind);
    QCOMPARE(banner->isVisible(), kind != int(pdf::PDFAppStorageBanner::Kind::None));
    QCOMPARE(banner->getTitle(), title);
    QCOMPARE(footer->getTagText(), tag);

    QStringList visibleButtons;
    for (QPushButton* button : banner->findChildren<QPushButton*>())
    {
        if (button->isVisibleTo(banner))
        {
            visibleButtons << button->objectName();
        }
    }
    visibleButtons.sort();
    QStringList expectedButtons = buttons;
    expectedButtons.sort();
    QCOMPARE(visibleButtons, expectedButtons);

    if (kind == int(pdf::PDFAppStorageBanner::Kind::Fallback))
    {
        QVERIFY(banner->getText().contains(QStringLiteral("disk I/O error")));
        QSignalSpy tryAgain(banner, &pdf::PDFAppStorageBanner::tryAgainRequested);
        QSignalSpy folder(banner, &pdf::PDFAppStorageBanner::showInFolderRequested);
        banner->getTryAgainButton()->click();
        banner->getShowInFolderButton()->click();
        QCOMPARE(tryAgain.count(), 1);
        QCOMPARE(folder.count(), 1);
    }
    if (kind == int(pdf::PDFAppStorageBanner::Kind::ReadOnly))
    {
        QSignalSpy updates(banner, &pdf::PDFAppStorageBanner::checkForUpdatesRequested);
        banner->getCheckForUpdatesButton()->click();
        QCOMPARE(updates.count(), 1);
    }
    if (kind == int(pdf::PDFAppStorageBanner::Kind::Replaced))
    {
        QVERIFY(banner->getText().contains(QStringLiteral("pdf4qt.db.damaged-2026-10-18")));

        // Dismiss hides it for the rest of the run, another damaged file shows it again
        banner->getDismissButton()->click();
        QVERIFY(!banner->isVisible());
        QCOMPARE(banner->getKind(), pdf::PDFAppStorageBanner::Kind::None);
        banner->setState(state);
        QVERIFY(!banner->isVisible());
        state.damagedFilePath = QStringLiteral("C:/data/pdf4qt.db.damaged-2026-10-19");
        banner->setState(state);
        QVERIFY(banner->isVisible());
    }

    // The banner paints in both themes, also right-to-left (no crash, its own colors)
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        banner->setLayoutDirection(Qt::RightToLeft);
        const QImage image = host.grab().toImage();
        QVERIFY(!image.isNull());
        banner->setLayoutDirection(Qt::LeftToRight);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(banner, &languageChange);
}

void AppNavigationTest::bannerFallbackTryAgain()
{
    // The portable window shows the Portable banner and tag on every page of the Home tab
    {
        NavigationFixture fixture;
        pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
        QCOMPARE(homeTab->getBanner()->getKind(), pdf::PDFAppStorageBanner::Kind::Portable);
        QVERIFY(homeTab->getBanner()->isVisible());
        QCOMPARE(homeTab->getFooter()->getTagText(), QStringLiteral("Portable"));
        fixture.navigate("/settings/general");
        QVERIFY(homeTab->getBanner()->isVisible());

        // A New tab has neither the banner nor the footer
        pdf::PDFAppWorkspaceTab* newTab = static_cast<pdf::PDFAppWorkspaceTab*>(fixture.window->openNewTab());
        QVERIFY(newTab);
        QVERIFY(!newTab->getBanner());
        QVERIFY(!newTab->getFooter());

        // Check for Updates goes to What's New
        Q_EMIT homeTab->getBanner()->checkForUpdatesRequested();
        QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(homeTab));
        QCOMPARE(homeTab->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::whatsNew());
    }

    // A database, which cannot be opened (a folder in the place of the file): Try Again
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString folderAsFile = directory.filePath(QStringLiteral("folder.db"));
    QVERIFY(QDir().mkpath(folderAsFile));

    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
    pdf::PDFDatabaseOptions options;
    options.filePath = folderAsFile;
    options.memoryName = QStringLiteral("/pdf4qt-navigation-fallback-%1").arg(QCoreApplication::applicationPid());
    options.busyTimeout = 100;
    context.open(options);
    QCOMPARE(context.getStorageState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);

    std::unique_ptr<pdf::PDFAppMainWindow> window = std::make_unique<pdf::PDFAppMainWindow>(&context);
    window->getNavigator()->setExternalUrlsEnabled(false);
    pdf::PDFAppWorkspaceTab* homeTab = window->getHomeTab();
    QCOMPARE(homeTab->getBanner()->getKind(), pdf::PDFAppStorageBanner::Kind::Fallback);
    QCOMPARE(homeTab->getFooter()->getTagText(), QStringLiteral("Not saved"));

    // Try Again fails again (the folder is still there): the banner stays
    homeTab->getBanner()->getTryAgainButton()->click();
    QCOMPARE(homeTab->getBanner()->getKind(), pdf::PDFAppStorageBanner::Kind::Fallback);

    // Show in Folder opens the folder of the database (the system is not asked in the test)
    QSignalSpy opened(window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    homeTab->getBanner()->getShowInFolderButton()->click();
    QCOMPARE(opened.count(), 1);
    QCOMPARE(QDir(opened.last().at(0).toUrl().toLocalFile()), QDir(directory.path()));

    // The folder is removed: Try Again opens the database on the disk, the banner disappears
    QVERIFY(QDir(folderAsFile).removeRecursively());
    homeTab->getBanner()->getTryAgainButton()->click();
    QCOMPARE(context.getStorageState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(homeTab->getBanner()->getKind(), pdf::PDFAppStorageBanner::Kind::None);
    QVERIFY(homeTab->getFooter()->getTagText().isEmpty());
    window.reset();
    context.close();
}

void AppNavigationTest::hitTest_data()
{
    using Result = pdf::PDFAppHitTestResult;

    QTest::addColumn<QPoint>("position");
    QTest::addColumn<bool>("maximized");
    QTest::addColumn<bool>("fullScreen");
    QTest::addColumn<bool>("resizable");
    QTest::addColumn<bool>("sideBorders");
    QTest::addColumn<bool>("rightToLeft");
    QTest::addColumn<bool>("maximizeEnabled");
    QTest::addColumn<int>("result");

    auto row = [](const char* name, QPoint position, bool maximized, bool fullScreen, bool resizable, bool sideBorders, bool rightToLeft, bool maximizeEnabled, Result result)
    {
        QTest::newRow(name) << position << maximized << fullScreen << resizable << sideBorders << rightToLeft << maximizeEnabled << int(result);
    };

    // Windows (the frame of the system has the side borders)
    row("win top", QPoint(500, 2), false, false, true, false, false, true, Result::Top);
    row("win top left", QPoint(3, 2), false, false, true, false, false, true, Result::TopLeft);
    row("win top right", QPoint(998, 2), false, false, true, false, false, true, Result::TopRight);
    row("win top over maximize", QPoint(930, 2), false, false, true, false, false, true, Result::Top);
    row("win caption", QPoint(500, 20), false, false, true, false, false, true, Result::Caption);
    row("win caption below border", QPoint(500, 6), false, false, true, false, false, true, Result::Caption);
    row("win maximize", QPoint(930, 20), false, false, true, false, false, true, Result::MaximizeButton);
    row("win minimize", QPoint(880, 20), false, false, true, false, false, true, Result::Client);
    row("win close", QPoint(980, 20), false, false, true, false, false, true, Result::Client);
    row("win tab", QPoint(50, 20), false, false, true, false, false, true, Result::Client);
    row("win new tab", QPoint(320, 20), false, false, true, false, false, true, Result::Client);
    row("win page", QPoint(500, 300), false, false, true, false, false, true, Result::Client);
    row("win left edge", QPoint(2, 300), false, false, true, false, false, true, Result::Client);
    row("win bottom edge", QPoint(500, 698), false, false, true, false, false, true, Result::Client);
    row("win bar bottom", QPoint(500, 37), false, false, true, false, false, true, Result::Caption);
    row("win below bar", QPoint(500, 38), false, false, true, false, false, true, Result::Client);

    // Linux (the client area has all borders)
    row("linux left", QPoint(2, 300), false, false, true, true, false, true, Result::Left);
    row("linux right", QPoint(998, 300), false, false, true, true, false, true, Result::Right);
    row("linux bottom", QPoint(500, 698), false, false, true, true, false, true, Result::Bottom);
    row("linux bottom left", QPoint(2, 698), false, false, true, true, false, true, Result::BottomLeft);
    row("linux bottom right", QPoint(998, 698), false, false, true, true, false, true, Result::BottomRight);
    row("linux left near top", QPoint(2, 8), false, false, true, true, false, true, Result::TopLeft);
    row("linux right near top", QPoint(998, 8), false, false, true, true, false, true, Result::TopRight);
    row("linux left near bottom", QPoint(2, 690), false, false, true, true, false, true, Result::BottomLeft);
    row("linux bottom near right", QPoint(990, 698), false, false, true, true, false, true, Result::BottomRight);
    row("linux caption", QPoint(500, 20), false, false, true, true, false, true, Result::Caption);

    // Maximized: no borders, the tabs and the buttons are hit at the edge of the screen
    row("max top", QPoint(500, 0), true, false, true, true, false, true, Result::Caption);
    row("max tab at edge", QPoint(50, 0), true, false, true, true, false, true, Result::Client);
    row("max maximize at edge", QPoint(930, 0), true, false, true, true, false, true, Result::MaximizeButton);
    row("max left", QPoint(0, 300), true, false, true, true, false, true, Result::Client);

    // Full screen and fixed size: no borders
    row("full top", QPoint(500, 1), false, true, true, true, false, true, Result::Caption);
    row("fixed top", QPoint(500, 1), false, false, false, true, false, false, Result::Caption);
    row("fixed maximize", QPoint(930, 20), false, false, false, true, false, false, Result::Client);

    // Right-to-left: the controls at the left end
    row("rtl maximize", QPoint(70, 20), false, false, true, false, true, true, Result::MaximizeButton);
    row("rtl close", QPoint(20, 20), false, false, true, false, true, true, Result::Client);
    row("rtl tab", QPoint(950, 20), false, false, true, false, true, true, Result::Client);
    row("rtl caption", QPoint(500, 20), false, false, true, false, true, true, Result::Caption);

    // Outside
    row("outside left", QPoint(-1, 5), false, false, true, true, false, true, Result::Client);
    row("outside right", QPoint(1000, 5), false, false, true, true, false, true, Result::Client);
    row("outside bottom", QPoint(10, 700), false, false, true, true, false, true, Result::Client);
}

void AppNavigationTest::hitTest()
{
    QFETCH(QPoint, position);
    QFETCH(bool, maximized);
    QFETCH(bool, fullScreen);
    QFETCH(bool, resizable);
    QFETCH(bool, sideBorders);
    QFETCH(bool, rightToLeft);
    QFETCH(bool, maximizeEnabled);
    QFETCH(int, result);

    // Window 1000 x 700, controls 138 at the trailing end, tabs 16 / 109, new tab 310
    pdf::PDFAppHitTestInput input;
    input.windowSize = QSize(1000, 700);
    input.position = position;
    input.isMaximized = maximized;
    input.isFullScreen = fullScreen;
    input.isResizable = resizable;
    input.hasSideBorders = sideBorders;
    input.resizeBorder = 6;
    input.topBarHeight = 38;

    auto mirrored = [rightToLeft](const QRect& rect)
    {
        return rightToLeft ? QRect(1000 - rect.right() - 1, rect.top(), rect.width(), rect.height()) : rect;
    };

    const QRect minimize = mirrored(QRect(862, 0, 46, 38));
    const QRect maximize = mirrored(QRect(908, 0, 46, 38));
    const QRect close = mirrored(QRect(954, 0, 46, 38));
    if (maximizeEnabled)
    {
        input.maximizeButtonRect = maximize;
    }
    else
    {
        input.interactiveRects << maximize;
    }
    input.interactiveRects << minimize << close << mirrored(QRect(16, 0, 85, 38)) << mirrored(QRect(109, 0, 193, 38)) << mirrored(QRect(310, 10, 28, 28));

    QCOMPARE(int(pdf::PDFAppHitTest::hitTest(input)), result);
}

void AppNavigationTest::hitTestLogicalPosition_data()
{
    QTest::addColumn<QPoint>("physical");
    QTest::addColumn<double>("ratio");
    QTest::addColumn<QPoint>("logical");

    QTest::newRow("1") << QPoint(15, 57) << 1.0 << QPoint(15, 57);
    QTest::newRow("1.5") << QPoint(15, 57) << 1.5 << QPoint(10, 38);
    QTest::newRow("1.25") << QPoint(100, 47) << 1.25 << QPoint(80, 37);
    QTest::newRow("2") << QPoint(15, 57) << 2.0 << QPoint(7, 28);
    QTest::newRow("negative") << QPoint(-3, -3) << 2.0 << QPoint(-2, -2);
    QTest::newRow("zero ratio") << QPoint(15, 57) << 0.0 << QPoint(15, 57);
    QTest::newRow("nan ratio") << QPoint(15, 57) << std::numeric_limits<double>::quiet_NaN() << QPoint(15, 57);
    QTest::newRow("infinite ratio") << QPoint(15, 57) << std::numeric_limits<double>::infinity() << QPoint(15, 57);
}

void AppNavigationTest::hitTestLogicalPosition()
{
    QFETCH(QPoint, physical);
    QFETCH(double, ratio);
    QFETCH(QPoint, logical);

    QCOMPARE(pdf::PDFAppHitTest::toLogicalPosition(physical, ratio), logical);

    // The physical button of 46 x 38 logical pixels is hit at the scale
    if (ratio >= 1.0 && std::isfinite(ratio))
    {
        pdf::PDFAppHitTestInput input;
        input.windowSize = QSize(1000, 700);
        input.maximizeButtonRect = QRect(908, 0, 46, 38);
        input.hasSideBorders = false;
        input.position = pdf::PDFAppHitTest::toLogicalPosition(QPoint(qRound(930 * ratio), qRound(20 * ratio)), ratio);
        QCOMPARE(pdf::PDFAppHitTest::hitTest(input), pdf::PDFAppHitTestResult::MaximizeButton);
        input.position = pdf::PDFAppHitTest::toLogicalPosition(QPoint(qRound(907 * ratio), qRound(20 * ratio)), ratio);
        QCOMPARE(pdf::PDFAppHitTest::hitTest(input), pdf::PDFAppHitTestResult::Caption);
    }
}

void AppNavigationTest::hitTestWindow()
{
    NavigationFixture fixture;
    fixture.window->resize(1200, 800);
    fixture.window->openNewTab();
    QCoreApplication::processEvents();

    pdf::PDFAppTopBar* topBar = fixture.window->getTopBar();
    pdf::PDFAppWindowFrame* frame = fixture.window->getFrame();
    pdf::PDFWindowControls* controls = topBar->getWindowControls();
    pdf::PDFDocumentTabBar* tabBar = topBar->getTabBar();
    QCOMPARE(topBar->height(), 38);
    QCOMPARE(pdf::PDFAppWindowFrame::getPlatformMode(), pdf::PDFAppWindowFrame::Mode::None);
    QCOMPARE(frame->getResizeBorder(), 6);

    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        fixture.window->setLayoutDirection(direction);
        QCoreApplication::processEvents();

        // The controls are at the trailing end, flush with the edge
        const QRect maximizeRect = controls->getButtonRect(pdf::PDFWindowControls::Kind::Maximize);
        QVERIFY(!maximizeRect.isEmpty());
        if (direction == Qt::LeftToRight)
        {
            QCOMPARE(controls->getButtonRect(pdf::PDFWindowControls::Kind::Close).right(), fixture.window->width() - 1);
        }
        else
        {
            QCOMPARE(controls->getButtonRect(pdf::PDFWindowControls::Kind::Close).left(), 0);
        }
        QCOMPARE(frame->hitTest(maximizeRect.center()), pdf::PDFAppHitTestResult::MaximizeButton);
        QCOMPARE(frame->hitTest(controls->getButtonRect(pdf::PDFWindowControls::Kind::Minimize).center()), pdf::PDFAppHitTestResult::Client);

        // Tabs and the new tab button are controls, the rest of the bar is the caption
        for (int i = 0; i < tabBar->count(); ++i)
        {
            const QPoint tabCenter = tabBar->mapTo(fixture.window.data(), tabBar->tabRect(i).center());
            QCOMPARE(frame->hitTest(tabCenter), pdf::PDFAppHitTestResult::Client);
        }
        QToolButton* newTabButton = topBar->getNewTabButton();
        QCOMPARE(frame->hitTest(newTabButton->mapTo(fixture.window.data(), newTabButton->rect().center())), pdf::PDFAppHitTestResult::Client);
        const int freeX = direction == Qt::LeftToRight ? fixture.window->width() / 2 + 100 : fixture.window->width() / 2 - 100;
        QCOMPARE(frame->hitTest(QPoint(freeX, 20)), pdf::PDFAppHitTestResult::Caption);
        QCOMPARE(frame->hitTest(QPoint(freeX, 2)), pdf::PDFAppHitTestResult::Top);
        QCOMPARE(frame->hitTest(QPoint(freeX, 300)), pdf::PDFAppHitTestResult::Client);

        // A tab of a maximized window is hit at the top edge (its rectangle has the 6 px above the shape)
        fixture.window->showMaximized();
        QCoreApplication::processEvents();
        const QPoint tabTop = tabBar->mapTo(fixture.window.data(), QPoint(tabBar->tabRect(0).center().x(), 0));
        QCOMPARE(frame->hitTest(tabTop), pdf::PDFAppHitTestResult::Client);
        fixture.window->showNormal();
        QCoreApplication::processEvents();

        // A window of a fixed size: Maximize is disabled, no HTMAXBUTTON, no borders
        fixture.window->setFixedSize(fixture.window->size());

        // No state event tells the change of the size limits: the hit test knows it itself
        QVERIFY(controls->getButton(pdf::PDFWindowControls::Kind::Maximize)->isEnabled());
        QCOMPARE(frame->hitTest(controls->getButtonRect(pdf::PDFWindowControls::Kind::Maximize).center()), pdf::PDFAppHitTestResult::Client);

        fixture.window->setWindowState(Qt::WindowNoState);
        QEvent stateChange(QEvent::WindowStateChange);
        QCoreApplication::sendEvent(fixture.window.data(), &stateChange);
        QCoreApplication::processEvents();
        QVERIFY(!controls->isResizable());
        QCOMPARE(frame->hitTest(controls->getButtonRect(pdf::PDFWindowControls::Kind::Maximize).center()), pdf::PDFAppHitTestResult::Client);
        QCOMPARE(frame->hitTest(QPoint(freeX, 2)), pdf::PDFAppHitTestResult::Caption);
        fixture.window->setMinimumSize(720, 480);
        fixture.window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        QCoreApplication::sendEvent(fixture.window.data(), &stateChange);
        QVERIFY(controls->isResizable());
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);

    // The input of a foreign window has no top bar
    QWidget other;
    const pdf::PDFAppHitTestInput foreign = topBar->createHitTestInput(&other, QPoint(1, 1));
    QCOMPARE(foreign.topBarHeight, 0);
    QCOMPARE(topBar->createHitTestInput(nullptr, QPoint(1, 1)).windowSize, QSize());

    // Edges and cursors
    QCOMPARE(pdf::PDFAppHitTest::getEdges(pdf::PDFAppHitTestResult::TopLeft), Qt::TopEdge | Qt::LeftEdge);
    QCOMPARE(pdf::PDFAppHitTest::getEdges(pdf::PDFAppHitTestResult::BottomRight), Qt::BottomEdge | Qt::RightEdge);
    QCOMPARE(pdf::PDFAppHitTest::getEdges(pdf::PDFAppHitTestResult::Caption), Qt::Edges());
    QCOMPARE(pdf::PDFAppHitTest::getCursorShape(pdf::PDFAppHitTestResult::Left), Qt::SizeHorCursor);
    QCOMPARE(pdf::PDFAppHitTest::getCursorShape(pdf::PDFAppHitTestResult::Top), Qt::SizeVerCursor);
    QCOMPARE(pdf::PDFAppHitTest::getCursorShape(pdf::PDFAppHitTestResult::TopRight), Qt::SizeBDiagCursor);
    QCOMPARE(pdf::PDFAppHitTest::getCursorShape(pdf::PDFAppHitTestResult::BottomRight), Qt::SizeFDiagCursor);
    QCOMPARE(pdf::PDFAppHitTest::getCursorShape(pdf::PDFAppHitTestResult::Client), Qt::ArrowCursor);
    for (int i = 0; i <= int(pdf::PDFAppHitTestResult::BottomRight); ++i)
    {
        const pdf::PDFAppHitTestResult value = static_cast<pdf::PDFAppHitTestResult>(i);
        QCOMPARE(pdf::PDFAppHitTest::getEdges(value) == Qt::Edges(), pdf::PDFAppHitTest::getCursorShape(value) == Qt::ArrowCursor);
    }

    // Native events of another platform are not handled; nothing crashes
    qintptr result = 0;
    QVERIFY(!frame->handleNativeEvent(QByteArrayLiteral("windows_generic_MSG"), nullptr, &result));
    QVERIFY(!frame->handleNativeEvent(QByteArrayLiteral("xcb_generic_event_t"), &result, &result));
    frame->startSystemMove();
    frame->initialize();
}

void AppNavigationTest::maximizedClientRect()
{
    // A maximized window leaves 2 px free at the edges with an auto-hide taskbar
    const QRect client(-8, 0, 1936, 1040);
    const int inset = pdf::PDFAppWindowFrame::AUTO_HIDE_TASKBAR_INSET;
    QCOMPARE(inset, 2);
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(client, Qt::Edges()), client);
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(client, Qt::BottomEdge), client.adjusted(0, 0, 0, -inset));
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(client, Qt::TopEdge), client.adjusted(0, inset, 0, 0));
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(client, Qt::LeftEdge | Qt::RightEdge), client.adjusted(inset, 0, -inset, 0));
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(client, Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge), client.adjusted(inset, inset, -inset, -inset));

    // A rectangle too small for the insets is kept
    const QRect tiny(10, 10, 3, 3);
    QCOMPARE(pdf::PDFAppWindowFrame::adjustMaximizedClientRect(tiny, Qt::LeftEdge | Qt::RightEdge), tiny);
}

void AppNavigationTest::nativeWindowsFrame()
{
    // Non-client messages of a real window of Windows. The tests run offscreen,
    // run this one by "UnitTestsAppNavigation -platform windows nativeWindowsFrame".
    if (pdf::PDFAppWindowFrame::getPlatformMode() != pdf::PDFAppWindowFrame::Mode::Windows)
    {
        QSKIP("Needs the platform plugin windows (-platform windows)");
    }

#if defined(Q_OS_WIN)
    using Kind = pdf::PDFWindowControls::Kind;

    // The window shown as by main (no position, no size): Qt places it on a screen
    // and the whole top bar is visible (a native window created before the show had
    // no position from Qt - the window appeared beyond the screens, never exposed)
    {
        NavigationFixture probe(false);
        std::unique_ptr<QWidget> window(pdf::PDFAppWindowFactory::createMainWindow(probe.context.get()));
        QVERIFY(!window->internalWinId());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        const QRect topBar(window->mapToGlobal(QPoint(0, 0)), QSize(window->width(), pdf::PDFAppTopBar::HEIGHT));
        for (const QPoint& corner : { topBar.topLeft(), topBar.topRight(), topBar.bottomLeft(), topBar.bottomRight() })
        {
            QVERIFY2(QGuiApplication::screenAt(corner), qPrintable(QStringLiteral("%1, %2").arg(corner.x()).arg(corner.y())));
        }
        RECT windowRect = { };
        GetWindowRect(reinterpret_cast<HWND>(window->winId()), &windowRect);
        POINT origin = { 0, 0 };
        ClientToScreen(reinterpret_cast<HWND>(window->winId()), &origin);
        QCOMPARE(origin.y, windowRect.top);
    }

    NavigationFixture fixture;
    fixture.window->openNewTab();
    fixture.window->move(120, 120);
    fixture.window->resize(1200, 800);
    QTRY_COMPARE(fixture.window->size(), QSize(1200, 800));
    QCoreApplication::processEvents();

    pdf::PDFAppTopBar* topBar = fixture.window->getTopBar();
    pdf::PDFWindowControls* controls = topBar->getWindowControls();
    pdf::PDFDocumentTabBar* tabBar = topBar->getTabBar();
    const HWND hwnd = reinterpret_cast<HWND>(fixture.window->winId());
    const qreal ratio = fixture.window->windowHandle()->devicePixelRatio();

    auto clientOrigin = [hwnd]()
    {
        POINT origin = { 0, 0 };
        ClientToScreen(hwnd, &origin);
        return origin;
    };

    auto hitTest = [hwnd, ratio](const QPoint& windowPosition)
    {
        POINT point = { LONG(std::lround((windowPosition.x() + 0.5) * ratio)), LONG(std::lround((windowPosition.y() + 0.5) * ratio)) };
        ClientToScreen(hwnd, &point);
        return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };

    auto sendAtMaximize = [hwnd, ratio, controls](UINT message)
    {
        const QPoint center = controls->getButtonRect(Kind::Maximize).center();
        POINT point = { LONG(std::lround((center.x() + 0.5) * ratio)), LONG(std::lround((center.y() + 0.5) * ratio)) };
        ClientToScreen(hwnd, &point);
        return SendMessageW(hwnd, message, HTMAXBUTTON, MAKELPARAM(point.x, point.y));
    };

    // The title bar of the system is removed: the client area starts at the top
    // edge of the window, the frame of the system keeps the other edges, Qt knows
    // the client area (the size of the widget is the size asked for)
    RECT windowRect = { };
    RECT clientRect = { };
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    const QString geometryText = QStringLiteral("window %1,%2,%3,%4 client origin %5,%6 size %7x%8 zoomed %9 style %10").arg(windowRect.left).arg(windowRect.top).arg(windowRect.right).arg(windowRect.bottom)
                                     .arg(clientOrigin().x).arg(clientOrigin().y).arg(clientRect.right).arg(clientRect.bottom).arg(IsZoomed(hwnd)).arg(qulonglong(GetWindowLongPtrW(hwnd, GWL_STYLE)), 0, 16);
    QVERIFY2(clientOrigin().y == windowRect.top, qPrintable(geometryText));
    QVERIFY2(clientOrigin().x > windowRect.left, qPrintable(geometryText));
    QVERIFY(qAbs(clientRect.right - qRound(1200 * ratio)) <= 1);
    QVERIFY(qAbs(clientRect.bottom - qRound(800 * ratio)) <= 1);
    QVERIFY(qAbs(fixture.window->geometry().top() * ratio - clientOrigin().y) <= ratio + 1);

    // WM_NCHITTEST
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        fixture.window->setLayoutDirection(direction);
        QCoreApplication::processEvents();

        const int freeX = direction == Qt::LeftToRight ? 700 : 500;
        QCOMPARE(hitTest(controls->getButtonRect(Kind::Maximize).center()), LRESULT(HTMAXBUTTON));
        QCOMPARE(hitTest(controls->getButtonRect(Kind::Minimize).center()), LRESULT(HTCLIENT));
        QCOMPARE(hitTest(controls->getButtonRect(Kind::Close).center()), LRESULT(HTCLIENT));
        QCOMPARE(hitTest(tabBar->mapTo(fixture.window.data(), tabBar->tabRect(1).center())), LRESULT(HTCLIENT));
        QCOMPARE(hitTest(QPoint(freeX, 20)), LRESULT(HTCAPTION));
        QCOMPARE(hitTest(QPoint(freeX, 1)), LRESULT(HTTOP));
        QCOMPARE(hitTest(QPoint(freeX, 300)), LRESULT(HTCLIENT));
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);
    QCoreApplication::processEvents();

    // The side edges belong to the frame of the system
    const int middleY = (windowRect.top + windowRect.bottom) / 2;
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(windowRect.left + 1, middleY)), LRESULT(HTLEFT));
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(windowRect.right - 2, middleY)), LRESULT(HTRIGHT));
    QCOMPARE(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(windowRect.right - 2, windowRect.bottom - 2)), LRESULT(HTBOTTOMRIGHT));

    // Hover and press of Maximize come from the non-client messages
    sendAtMaximize(WM_NCMOUSEMOVE);
    QVERIFY(controls->getHoveredButton() == Kind::Maximize);
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    QVERIFY(!controls->getHoveredButton());

    QCOMPARE(sendAtMaximize(WM_NCLBUTTONDOWN), LRESULT(0));
    QVERIFY(controls->getPressedButton() == Kind::Maximize);
    SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
    QVERIFY(!controls->getPressedButton());
    sendAtMaximize(WM_NCLBUTTONDOWN);
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    QVERIFY(!controls->getPressedButton());

    // A release without the press does nothing, a click maximizes
    sendAtMaximize(WM_NCLBUTTONUP);
    QVERIFY(!fixture.window->isMaximized());
    sendAtMaximize(WM_NCLBUTTONDOWN);
    sendAtMaximize(WM_NCLBUTTONUP);
    QTRY_VERIFY(fixture.window->isMaximized());
    QVERIFY(!controls->getPressedButton());
    QCoreApplication::processEvents();

    // Maximized: the client area is the work area of the monitor (the frame of the
    // system is beyond the screen), the tabs and the buttons are hit at the top edge
    MONITORINFO monitorInfo = { };
    monitorInfo.cbSize = sizeof(monitorInfo);
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo));
    QVERIFY(clientOrigin().y >= monitorInfo.rcWork.top);
    QVERIFY(clientOrigin().y <= monitorInfo.rcWork.top + pdf::PDFAppWindowFrame::AUTO_HIDE_TASKBAR_INSET);
    GetClientRect(hwnd, &clientRect);
    QVERIFY(clientRect.bottom <= monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
    QCOMPARE(hitTest(QPoint(controls->getButtonRect(Kind::Maximize).center().x(), 0)), LRESULT(HTMAXBUTTON));
    QCOMPARE(hitTest(tabBar->mapTo(fixture.window.data(), QPoint(tabBar->tabRect(0).center().x(), 0))), LRESULT(HTCLIENT));
    QVERIFY(qAbs(fixture.window->geometry().top() * ratio - clientOrigin().y) <= ratio + 1);

    // Restore gives the size back (no drift by the height of a title bar)
    sendAtMaximize(WM_NCLBUTTONDOWN);
    sendAtMaximize(WM_NCLBUTTONUP);
    QTRY_VERIFY(!fixture.window->isMaximized());
    QTRY_COMPARE(fixture.window->size(), QSize(1200, 800));
    GetWindowRect(hwnd, &windowRect);
    QCOMPARE(clientOrigin().y, windowRect.top);

    // A window of a fixed size: no HTMAXBUTTON
    fixture.window->setFixedSize(fixture.window->size());
    QCoreApplication::processEvents();
    QCOMPARE(hitTest(controls->getButtonRect(Kind::Maximize).center()), LRESULT(HTCLIENT));
    fixture.window->setMinimumSize(720, 480);
    fixture.window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QCoreApplication::processEvents();

    // A change of the flags of the window keeps the frame without the title bar
    fixture.window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window.data()));
    QCoreApplication::processEvents();
    const HWND newHwnd = reinterpret_cast<HWND>(fixture.window->winId());
    POINT newOrigin = { 0, 0 };
    ClientToScreen(newHwnd, &newOrigin);
    GetWindowRect(newHwnd, &windowRect);
    QVERIFY2(newOrigin.y == windowRect.top, qPrintable(QStringLiteral("same hwnd %1 origin %2 top %3 style %4").arg(newHwnd == hwnd).arg(newOrigin.y).arg(windowRect.top).arg(qulonglong(GetWindowLongPtrW(newHwnd, GWL_STYLE)), 0, 16)));
    QTRY_COMPARE(fixture.window->size(), QSize(1200, 800));
#endif
}

void AppNavigationTest::windowControls()
{
    NavigationFixture fixture;
    pdf::PDFWindowControls* controls = fixture.window->getTopBar()->getWindowControls();
    QVERIFY(controls);
    QCOMPARE(controls->getPlatform(), pdf::PDFComponentPainter::getNativeWindowPlatform());
    QVERIFY(!controls->isMaximized());

    // Maximize / Restore
    controls->getButton(pdf::PDFWindowControls::Kind::Maximize)->click();
    QTRY_VERIFY(fixture.window->isMaximized());
    QVERIFY(controls->isMaximized());
    QVERIFY(controls->activate(pdf::PDFWindowControls::Kind::Restore));
    QTRY_VERIFY(!fixture.window->isMaximized());
    QVERIFY(!controls->isMaximized());

    // Double click on the free area of the bar and on the free part of the tab bar
    pdf::PDFAppTopBar* topBar = fixture.window->getTopBar();
    QTest::mouseDClick(topBar, Qt::LeftButton, Qt::NoModifier, QPoint(topBar->width() / 2, 20));
    QTRY_VERIFY(fixture.window->isMaximized());
    Q_EMIT topBar->getTabBar()->tabBarDoubleClicked(-1);
    QTRY_VERIFY(!fixture.window->isMaximized());
    Q_EMIT topBar->getTabBar()->tabBarDoubleClicked(0);
    QVERIFY(!fixture.window->isMaximized());

    // A double click inside of the tab bar is the tab bar's (no double toggle)
    QSignalSpy maximizeSpy(topBar, &pdf::PDFAppTopBar::maximizeRestoreRequested);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(topBar->getTabBar()->geometry().center()), topBar->mapToGlobal(QPointF(topBar->getTabBar()->geometry().center())),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(topBar, &doubleClick);
    QCOMPARE(maximizeSpy.count(), 0);

    // A press on the free area asks for the move of the window (the system moves it)
    QSignalSpy moveSpy(topBar, &pdf::PDFAppTopBar::moveRequested);
    QTest::mousePress(topBar, Qt::LeftButton, Qt::NoModifier, QPoint(topBar->width() / 2, 20));
    QTest::mouseRelease(topBar, Qt::LeftButton, Qt::NoModifier, QPoint(topBar->width() / 2, 20));
    QCOMPARE(moveSpy.count(), 1);
    pdf::PDFDocumentTabBar* tabBar = topBar->getTabBar();
    const QPoint emptyTabBar(tabBar->width() - 1, 20);
    if (tabBar->tabAt(emptyTabBar) < 0)
    {
        QTest::mousePress(tabBar, Qt::LeftButton, Qt::NoModifier, emptyTabBar);
        QCOMPARE(moveSpy.count(), 2);
    }

    // Minimize
    controls->getButton(pdf::PDFWindowControls::Kind::Minimize)->click();
    QTRY_VERIFY(fixture.window->isMinimized());
    fixture.window->showNormal();
    QTRY_VERIFY(!fixture.window->isMinimized());

    // Hover and press given by the window (non-client messages of Windows)
    controls->setHoveredButton(pdf::PDFWindowControls::Kind::Maximize);
    QVERIFY(controls->getButton(pdf::PDFWindowControls::Kind::Maximize)->isExternalHover());
    controls->setHoveredButton(std::nullopt);

    // Close (the window hides, the application quits by the last window)
    controls->getButton(pdf::PDFWindowControls::Kind::Close)->click();
    QTRY_VERIFY(!fixture.window->isVisible());
}

void AppNavigationTest::closeAndCancel()
{
    const QString serverName = QStringLiteral("pdf4qt-navigation-%1-close").arg(QCoreApplication::applicationPid());

    // The server stops listening at the start of the quitting
    {
        NavigationFixture fixture;
        pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
        QString error;
        QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), server, 5000, &error), pdf::PDFAppInstanceGuard::Result::Listening);
        fixture.context->setInstanceServer(server);
        QSignalSpy closing(fixture.window.data(), &pdf::PDFAppMainWindow::closing);
        QVERIFY(fixture.window->close());
        QCOMPARE(closing.count(), 1);
        QVERIFY(server->isQuitting());
        QVERIFY(!server->isSuspended());
        QVERIFY(!server->isListening());

        // The quit command closes the window as well
        fixture.window->show();
        fixture.window->getCommandAction(pdf::PDFAppCommands::quit())->trigger();
        QVERIFY(!fixture.window->isVisible());
        fixture.context->setInstanceServer(nullptr);
        delete server;
    }

    // The user cancels the quitting: the server listens again
    {
        NavigationFixture fixture(false);
        pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
        QString error;
        QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), server, 5000, &error), pdf::PDFAppInstanceGuard::Result::Listening);
        fixture.context->setInstanceServer(server);

        CancelingMainWindow* window = new CancelingMainWindow(fixture.context.get());
        fixture.window = window;
        fixture.prepareWindow();
        QVERIFY(!window->close());
        QCOMPARE(window->confirmCount, 1);
        QVERIFY(window->isVisible());
        QVERIFY(!server->isSuspended());
        QVERIFY(server->isListening());

        // A request of another process reaches the window again
        QSignalSpy openSpy(fixture.context.get(), &pdf::PDFAppContext::openRequested);
        pdf::PDFAppInstanceRequest request;
        request.errors << QStringLiteral("Unknown argument --x.");
        std::atomic<int> sendResult = -1;
        std::jthread sender([&sendResult, serverName, request]()
        {
            QString sendError;
            sendResult = int(pdf::PDFAppInstanceClient::send(serverName, request, 10000, &sendError));
        });
        QTRY_COMPARE_WITH_TIMEOUT(sendResult.load(), int(pdf::PDFAppInstanceClient::Result::Delivered), 15000);
        sender.join();
        QTRY_COMPARE(openSpy.count(), 1);
        QTRY_COMPARE(window->getForwardedErrors(), QStringList({ QStringLiteral("Unknown argument --x.") }));
        fixture.context->setInstanceServer(nullptr);
        delete server;
    }
}

void AppNavigationTest::quitWithPendingRequests()
{
    // A process has connected before the quitting started and its message comes
    // during the questions about unsaved changes: it is held without an answer;
    // the accepted quitting refuses it (the process starts by itself), the
    // canceled one opens it - the window never acknowledges a request it loses
    const QString serverName = QStringLiteral("pdf4qt-navigation-%1-pending").arg(QCoreApplication::applicationPid());
    for (const bool isAccepted : { true, false })
    {
        NavigationFixture fixture(false);
        pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
        QString error;
        QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), server, 5000, &error), pdf::PDFAppInstanceGuard::Result::Listening);
        fixture.context->setInstanceServer(server);

        QuestioningMainWindow* window = new QuestioningMainWindow(fixture.context.get());
        fixture.window = window;
        fixture.prepareWindow();
        QSignalSpy openSpy(fixture.context.get(), &pdf::PDFAppContext::openRequested);

        QLocalSocket pending;
        pending.connectToServer(serverName);
        QVERIFY(pending.waitForConnected(5000));

        pdf::PDFAppInstanceRequest request;
        request.errors << QStringLiteral("Unknown argument --pending.");
        int openedDuringQuestions = -1;
        window->onConfirm = [&]()
        {
            pending.write(pdf::PDFAppInstanceProtocol::createFrame(pdf::PDFAppInstanceProtocol::encodeRequest(request)));
            pending.flush();
            const bool isHeld = QTest::qWaitFor([server]() { return server->getHeldRequestCount() == 1; }, 10000);
            QTest::qWait(100);
            openedDuringQuestions = isHeld ? int(openSpy.count()) : -2;
            return isAccepted;
        };

        if (isAccepted)
        {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Request of another instance refused.*quitting"));
        }
        QCOMPARE(window->close(), isAccepted);
        QCOMPARE(openedDuringQuestions, 0);

        bool ok = !isAccepted;
        QVERIFY(readReply(pending, 10000, &ok));
        QCOMPARE(ok, !isAccepted);
        if (isAccepted)
        {
            QTest::qWait(100);
            QCOMPARE(openSpy.count(), 0);
            QVERIFY(window->getForwardedErrors().isEmpty());
            QVERIFY(server->isQuitting());
        }
        else
        {
            // The server listens again, when its own connections have closed (on
            // Windows an open instance of the pipe looks like a server of the name)
            QCOMPARE(openSpy.count(), 1);
            QCOMPARE(window->getForwardedErrors(), QStringList({ QStringLiteral("Unknown argument --pending.") }));
            pending.abort();
            QTRY_VERIFY_WITH_TIMEOUT(server->isListening(), 10000);
            QVERIFY(!server->isSuspended());
        }

        pending.abort();
        fixture.context->setInstanceServer(nullptr);
        delete server;
    }
}

void AppNavigationTest::cancelQuitWhileLocked()
{
    // The user cancels the quitting, while a starting process decides under the lock
    // of the instances: the window does not freeze waiting for the lock, the server
    // listens again from the event loop, when the lock is free
    const int factor = qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")) ? 20 : 1;
    const QString serverName = QStringLiteral("pdf4qt-navigation-%1-locked").arg(QCoreApplication::applicationPid());
    NavigationFixture fixture(false);
    pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
    QString error;
    QCOMPARE(pdf::PDFAppInstanceGuard::start(serverName, pdf::PDFAppInstanceRequest(), server, 5000, &error), pdf::PDFAppInstanceGuard::Result::Listening);
    fixture.context->setInstanceServer(server);

    QuestioningMainWindow* window = new QuestioningMainWindow(fixture.context.get());
    fixture.window = window;
    fixture.prepareWindow();

    // The lock is taken during the questions (a process starts meanwhile)
    QLockFile lockFile(pdf::PDFAppInstanceGuard::getLockFilePath(serverName));
    lockFile.setStaleLockTime(pdf::PDFAppInstanceGuard::STALE_LOCK_TIME);
    bool isLocked = false;
    window->onConfirm = [&lockFile, &isLocked]()
    {
        isLocked = lockFile.tryLock(5000);
        return false;
    };

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!window->close());
    qInfo("canceled quitting with the lock held: %lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 1000 * factor, qPrintable(QStringLiteral("closing took %1 ms").arg(timer.elapsed())));
    QVERIFY(isLocked);
    QVERIFY(window->isVisible());
    QVERIFY(server->isSuspended());
    QVERIFY(!server->isListening());
    QTest::qWait(3 * pdf::PDFAppInstanceServer::RETRY_INTERVAL);
    QVERIFY(!server->isListening());

    // The lock is released: the server listens again and a request reaches the window
    lockFile.unlock();
    QTRY_VERIFY_WITH_TIMEOUT(server->isListening(), 10000 * factor);
    QVERIFY(!server->isSuspended());

    QSignalSpy openSpy(fixture.context.get(), &pdf::PDFAppContext::openRequested);
    pdf::PDFAppInstanceRequest request;
    request.errors << QStringLiteral("Unknown argument --locked.");
    std::atomic<int> sendResult = -1;
    std::jthread sender([&sendResult, serverName, request, factor]()
    {
        QString sendError;
        sendResult = int(pdf::PDFAppInstanceClient::send(serverName, request, 10000 * factor, &sendError));
    });
    QTRY_COMPARE_WITH_TIMEOUT(sendResult.load(), int(pdf::PDFAppInstanceClient::Result::Delivered), 15000 * factor);
    sender.join();
    QTRY_COMPARE(openSpy.count(), 1);
    QTRY_COMPARE(window->getForwardedErrors(), QStringList({ QStringLiteral("Unknown argument --locked.") }));

    fixture.context->setInstanceServer(nullptr);
    delete server;
}

void AppNavigationTest::instanceRequests()
{
    QTemporaryDir directory;
    const QString file = directory.filePath(QStringLiteral("document.pdf"));
    QFile output(file);
    QVERIFY(output.open(QIODevice::WriteOnly));
    output.write("%PDF-1.7\n");
    output.close();

    // Files and errors of the own command line are shown, when the window exists
    // (step 3: the option and the file, which is not a valid PDF, are listed in the
    // dialog open-failed - the reading of the file ends later, so it may follow in a
    // second dialog, when the first one is closed)
    NavigationFixture fixture(true, { file, QStringLiteral("--bogus") });
    QTRY_COMPARE(fixture.window->getRequestedFiles(), QStringList({ QDir::cleanPath(file) }));
    QString commandLineTexts;
    QElapsedTimer failedTimer;
    failedTimer.start();
    while (failedTimer.elapsed() < 5000 && !(commandLineTexts.contains(QStringLiteral("--bogus")) && commandLineTexts.contains(QStringLiteral("document.pdf"))))
    {
        QTest::qWait(20);
        for (pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>())
        {
            if (box->isVisible())
            {
                for (const QLabel* label : box->findChildren<QLabel*>())
                {
                    commandLineTexts += label->text() + QLatin1Char(' ');
                }
                box->reject();
            }
        }
    }
    QVERIFY(commandLineTexts.contains(QStringLiteral("--bogus")));
    QVERIFY(commandLineTexts.contains(QStringLiteral("document.pdf")));

    // The first dialog may have listed both: the dialog of the reading of the file
    // (its tab is closed after the failure) can still follow - it is closed as well
    const int factor = qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")) ? 20 : 1;
    auto isDocumentTabOpen = [&fixture]()
    {
        for (const pdf::PDFAppTab* tab : fixture.window->getTabs())
        {
            if (tab && tab->getKind() == pdf::PDFAppTabKind::Document)
            {
                return true;
            }
        }
        return false;
    };
    failedTimer.restart();
    while (failedTimer.elapsed() < 10000 * factor && (isDocumentTabOpen() || !fixture.window->findChildren<pdf::PDFMessageBox*>().isEmpty()))
    {
        QTest::qWait(20);
        for (pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>())
        {
            if (box->isVisible())
            {
                box->reject();
            }
        }
    }
    QVERIFY(!isDocumentTabOpen());
    QTRY_VERIFY(fixture.window->findChildren<pdf::PDFMessageBox*>().isEmpty());

    // A request of another process: the window is raised, its errors and files are listed
    pdf::PDFAppInstanceServer* server = new pdf::PDFAppInstanceServer();
    fixture.context->setInstanceServer(server);
    fixture.window->showMinimized();
    QTRY_VERIFY(fixture.window->isMinimized());

    pdf::PDFAppInstanceRequest request;
    request.files << file << directory.filePath(QStringLiteral("other.pdf"));
    request.page = 3;
    request.errors << QStringLiteral("Unknown argument --forwarded.");
    QTest::ignoreMessage(QtWarningMsg, "Command line of another process: Unknown argument --forwarded.");
    QTest::ignoreMessage(QtInfoMsg, "Open requested by another process: 2 file(s), page 3");
    Q_EMIT server->openRequested(request);
    QVERIFY(!fixture.window->isMinimized());
    QCOMPARE(fixture.window->getForwardedErrors(), QStringList({ QStringLiteral("Unknown argument --forwarded.") }));
    QCOMPARE(fixture.window->getRequestedFiles().size(), 2);

    // The error and the missing file are listed in the dialog open-failed (step 3)
    QTRY_VERIFY(!fixture.window->findChildren<pdf::PDFMessageBox*>().isEmpty());
    QStringList texts;
    for (const pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>())
    {
        for (const QLabel* label : box->findChildren<QLabel*>())
        {
            texts << label->text();
        }
    }
    QVERIFY(texts.join(QLatin1Char('\n')).contains(QStringLiteral("Unknown argument --forwarded.")));
    QVERIFY(texts.join(QLatin1Char('\n')).contains(QStringLiteral("other.pdf")));

    // The lists are bounded
    pdf::PDFAppInstanceRequest many;
    for (int i = 0; i < 150; ++i)
    {
        many.errors << QStringLiteral("Error %1").arg(i);
    }
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("Open requested"));
    for (int i = 0; i < 150; ++i)
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Command line of another process"));
    }
    fixture.window->handleOpenRequest(many);
    QCOMPARE(fixture.window->getForwardedErrors().size(), pdf::PDFAppMainWindow::MAXIMUM_FORWARDED_ERRORS);

    // The window is deleted while the message boxes are open
    delete fixture.window.data();
    QCoreApplication::processEvents();
    fixture.context->setInstanceServer(nullptr);
    delete server;
}

void AppNavigationTest::shellDesign()
{
    // Geometry of the shell measured against Figma (pdf4qt-not-found 940:134845,
    // pdf4qt-settings-general-portable 941:138200, storage banners 958:28036)
    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");
    QCoreApplication::processEvents();
    QWidget* window = fixture.window.data();
    auto geometry = [window](const QWidget* widget) -> QRect
    {
        return widget ? QRect(widget->mapTo(window, QPoint(0, 0)), widget->size()) : QRect();
    };

    // logo-text-group: the title 69 - 86, the subtitle from 88 (the tile 66 - 102)
    const QRect sidebar = geometry(window->findChild<pdf::PDFAppSidebar*>());
    QCOMPARE(geometry(window->findChild<QWidget*>(QStringLiteral("logoTitle"))).top() - sidebar.top(), 69 - 38);
    QCOMPARE(geometry(window->findChild<QWidget*>(QStringLiteral("logoTitle"))).height(), 17);
    QCOMPARE(geometry(window->findChild<QWidget*>(QStringLiteral("logoSubtitle"))).top() - sidebar.top(), 88 - 38);

    // empty-state: the circle 72, 20, the title 27, 8, the text of two lines of 21 px
    // (it was one line of 17 px and the gap below the circle was eaten); the items
    // are centered on the page itself
    pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
    const QRect page = geometry(homeTab->getPage());
    const QRect emptyState = geometry(window->findChild<QWidget*>(QStringLiteral("emptyState")));
    const QRect title = geometry(window->findChild<QWidget*>(QStringLiteral("notFoundTitle")));
    pdf::PDFAppParagraph* text = window->findChild<pdf::PDFAppParagraph*>(QStringLiteral("notFoundText"));
    QVERIFY(text);
    QCOMPARE(emptyState.width(), page.width());
    QCOMPARE(title.top() - emptyState.top(), 72 + 20);
    QCOMPARE(geometry(text).top(), title.bottom() + 1 + 8);
    QCOMPARE(geometry(text).size(), QSize(460, 2 * 21));
    QCOMPARE(geometry(text).left() - page.left(), 595 - 242);
    const QImage textImage = text->grab().toImage().convertToFormat(QImage::Format_RGB32);
    bool hasSecondLine = false;
    for (int y = 21; y < textImage.height() && !hasSecondLine; ++y)
    {
        for (int x = 0; x < textImage.width() && !hasSecondLine; ++x)
        {
            hasSecondLine = qGray(textImage.pixel(x, y)) < 200;
        }
    }
    QVERIFY(hasSecondLine);

    // hint: "back", 4 + 10 + 4, the next key cap
    QWidget* hints = window->findChild<QWidget*>(QStringLiteral("notFoundHints"));
    QVERIFY(hints);
    const QList<pdf::PDFAppKeyCap*> keyCaps = hints->findChildren<pdf::PDFAppKeyCap*>();
    const QList<pdf::PDFLabel*> hintLabels = hints->findChildren<pdf::PDFLabel*>();
    QVERIFY(keyCaps.size() >= 3 && !hintLabels.isEmpty());
    QCOMPARE(keyCaps[2]->x() - (hintLabels[0]->x() + hintLabels[0]->width()), 18);

    // app-footer 28:217 (31 = the padding 16 + the text 15): the line of 1 px lies
    // inside of the padding 16, the link (16) reaches 1 px into it, the bottom
    // padding of the workspace 16 (owner decision O11) is a part of the footer; the
    // tag 21 does not change the height (footerAxis)
    pdf::PDFAppFooter* footer = homeTab->findChild<pdf::PDFAppFooter*>();
    QVERIFY(footer && footer->isVisible());
    QCOMPARE(footer->height(), 31 + 16);
    QCOMPARE(geometry(footer).bottom() + 1, window->height());
    QCOMPARE(footer->getVersionLabel()->y(), 16);
    QCOMPARE(footer->getVersionLabel()->height(), 15);
    QCOMPARE(footer->getLicenseLink()->y(), 15);
    QVERIFY(footer->getTag()->isVisible());
    QCOMPARE(footer->getTag()->y(), 16 - 3);

    // Banners 842 wide: two lines of 16 px give the height 76; read only is blue,
    // the fallback amber
    QWidget host;
    QVBoxLayout* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    pdf::PDFAppStorageBanner* banner = new pdf::PDFAppStorageBanner(&host);
    layout->addWidget(banner);
    layout->addStretch(1);
    host.resize(842, 300);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    struct BannerCase
    {
        pdf::PDFDatabaseStorageMode mode;
        int height;
        pdf::PDFAccentHue hue;
    };
    for (const BannerCase& bannerCase : { BannerCase{ pdf::PDFDatabaseStorageMode::Portable, 76, pdf::PDFAccentHue::Blue },
                                          BannerCase{ pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema, 76, pdf::PDFAccentHue::Blue },
                                          BannerCase{ pdf::PDFDatabaseStorageMode::MemoryFallback, 92, pdf::PDFAccentHue::Amber } })
    {
        pdf::PDFDatabaseState state;
        state.mode = bannerCase.mode;
        state.filePath = QStringLiteral("C:/data/pdf4qt.db");
        banner->setState(state);
        layout->activate();
        QCoreApplication::processEvents();
        QCOMPARE(banner->height(), bannerCase.height);
        QCOMPARE(banner->findChild<pdf::PDFAppParagraph*>()->getLineHeight(), 16);
        const QImage image = banner->grab().toImage();
        const QColor background = pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(bannerCase.hue, pdf::PDFAccentHueColors::Role::Background));
        QCOMPARE(image.pixelColor(banner->width() / 2, banner->height() - 4).rgb(), background.rgb());
    }

    // The text starts after the icon circle (border 1 + 14 + 36 + 12), right-to-left
    // at the right side (the margins of a layout are not mirrored by Qt)
    pdf::PDFAppParagraph* bannerText = banner->findChild<pdf::PDFAppParagraph*>();
    QCOMPARE(bannerText->x(), 1 + 14 + 36 + 12);
    banner->setLayoutDirection(Qt::RightToLeft);
    layout->activate();
    QCoreApplication::processEvents();
    QCOMPARE(banner->width() - (bannerText->x() + bannerText->width()), 1 + 14 + 36 + 12);
    banner->setLayoutDirection(Qt::LeftToRight);
}

void AppNavigationTest::headerTexts()
{
    // The title and the subtitle of the header are one line each, elided with the
    // whole text as the tooltip; a stressed part of the subtitle is Semi Bold in
    // text/accent (Home 28:35), never a rich text
    const QString subtitle = QStringLiteral("A fast, modern and open-source PDF editor for Windows, Linux and macOS.");
    pdf::PDFAppWorkspaceHeader header;
    header.setTexts(QStringLiteral("Welcome to PDF4QT"), subtitle, QStringLiteral("PDF"));
    header.resize(1166, 55);
    header.show();
    QCoreApplication::processEvents();

    pdf::PDFAppHeaderText* label = header.getSubtitleLabel();
    QCOMPARE(label->text(), subtitle);
    QCOMPARE(label->textFormat(), Qt::PlainText);
    QCOMPARE(label->getAccentIndex(), 31);
    QCOMPARE(label->getAccentLength(), 3);
    QVERIFY(!label->isElided());
    QCOMPARE(label->height(), QFontMetrics(label->font()).height());
    QVERIFY(label->sizeHint().width() >= qCeil(label->getTextWidth()));
    QCOMPARE(label->minimumSizeHint().width(), 1);
    QCOMPARE(header.getTitleLabel()->height(), 34);

    // Pixels: the accent color only in the stressed word, in both themes
    auto countAccent = [](pdf::PDFAppHeaderText* text, int left, int right)
    {
        QImage image(text->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        text->render(&image);
        const QColor accent = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::TextAccent);
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = qMax(0, left); x < qMin(image.width(), right); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                count += (pixel.alpha() == 255 && qAbs(pixel.red() - accent.red()) <= 8 && qAbs(pixel.green() - accent.green()) <= 8 && qAbs(pixel.blue() - accent.blue()) <= 8) ? 1 : 0;
            }
        }
        return count;
    };
    const QFontMetricsF metrics(label->font());
    const int accentLeft = qFloor(metrics.horizontalAdvance(subtitle.left(31)));
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QVERIFY(countAccent(label, accentLeft - 2, accentLeft + 32) > 10);
        QCOMPARE(countAccent(label, 0, accentLeft - 4), 0);
        QCOMPARE(countAccent(label, accentLeft + 40, label->width()), 0);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // No tooltip of a text, which is whole
    // (the event is given to the label itself: QApplication does not deliver a
    // tooltip event to an inactive window)
    QHelpEvent wholeEvent(QEvent::ToolTip, QPoint(5, 5), label->mapToGlobal(QPoint(5, 5)));
    QVERIFY(static_cast<QObject*>(label)->event(&wholeEvent));
    QVERIFY(!wholeEvent.isAccepted());

    // Right-to-left: the text at the right edge (the base direction of the line is
    // the direction of the interface)
    header.setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    const int textLeft = label->width() - qCeil(label->getTextWidth());
    QVERIFY(countAccent(label, textLeft, label->width()) > 10);
    QCOMPARE(countAccent(label, 0, textLeft - 2), 0);
    header.setLayoutDirection(Qt::LeftToRight);

    // A narrow header (the minimal window): elided, the tooltip has the whole text,
    // the elided text has no accent
    header.resize(400, 55);
    QCoreApplication::processEvents();
    QVERIFY(label->isElided());
    QVERIFY(header.getTitleLabel()->isElided());
    QCOMPARE(label->toolTip(), subtitle);
    QCOMPARE(countAccent(label, 0, label->width()), 0);
    QHelpEvent elidedEvent(QEvent::ToolTip, QPoint(5, 5), label->mapToGlobal(QPoint(5, 5)));
    static_cast<QObject*>(label)->event(&elidedEvent);
    QVERIFY(elidedEvent.isAccepted());
    QToolTip::hideText();

    // An accent, which is not in the text (a translation), and no accent
    header.resize(1166, 55);
    header.setTexts(QStringLiteral("Title"), QStringLiteral("Ein schneller PDF-Editor"), QStringLiteral("XYZ"));
    QCOMPARE(label->getAccentIndex(), -1);
    QCOMPARE(label->getAccentLength(), 0);
    header.setTexts(QStringLiteral("Title"), subtitle);
    QCOMPARE(label->getAccentIndex(), -1);
    QCOMPARE(qCeil(label->getTextWidth()), qCeil(pdf::PDFStylePainter::textWidth(subtitle, label->font())));

    // The icon of a page (Manage collections 283:1932): none by default - the texts at
    // the edge -, the Tool icon 44 with the texts 14 px after it, centered on them;
    // mirrored right-to-left; the search field gives the texts the width of the icon
    QVERIFY(header.getIconBadge()->isHidden());
    QCOMPARE(header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).x(), 0);
    QCOMPARE(label->mapTo(&header, QPoint(0, 0)).x(), 0);
    const int titleTop = header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).y();
    pdf::PDFAppHeaderIcon icon;
    icon.iconName = QStringLiteral("layers");
    header.setIcon(icon);
    QCoreApplication::processEvents();
    pdf::PDFToolBadge* badge = header.getIconBadge();
    QVERIFY(badge->isVisible());
    QCOMPARE(badge->size(), QSize(44, 44));
    QCOMPARE(badge->getIconName(), QStringLiteral("layers"));
    QCOMPARE(badge->getHue(), pdf::PDFAccentHue::Neutral);
    QCOMPARE(badge->mapTo(&header, QPoint(0, 0)).x(), 0);
    QCOMPARE(header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).x(), 44 + 14);
    QCOMPARE(label->mapTo(&header, QPoint(0, 0)).x(), 44 + 14);
    QCOMPARE(header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).y(), titleTop);
    const int textsCenter = header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).y() + (label->mapTo(&header, QPoint(0, 0)).y() + label->height() - header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).y()) / 2;
    QVERIFY(qAbs(badge->mapTo(&header, QPoint(0, 0)).y() + 22 - textsCenter) <= 1);
    header.setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    QCOMPARE(badge->mapTo(&header, QPoint(0, 0)).x() + 44, header.width());
    QCOMPARE(header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).x() + header.getTitleLabel()->width(), header.width() - 44 - 14);
    header.setLayoutDirection(Qt::LeftToRight);
    header.setIcon(pdf::PDFAppHeaderIcon());
    QCoreApplication::processEvents();
    QVERIFY(badge->isHidden());
    QCOMPARE(header.getTitleLabel()->mapTo(&header, QPoint(0, 0)).x(), 0);

    // Empty texts hide the lines
    header.setTexts(QString(), QString());
    QVERIFY(label->isHidden());
    QVERIFY(header.getTitleLabel()->isHidden());
    QCOMPARE(label->getTextWidth(), 0.0);
}

void AppNavigationTest::footerAxis()
{
    // Owner decision O11 (Figma 941:138200, 955:150880): the workspace ends 16 px
    // above the bottom of the window, the switch Light / Dark / Auto 8 px; the texts
    // of the footer end 16 px above the bottom and their centers lie on the axis of
    // the switch; the tag (21) is centered on the text, it reaches 3 px above and
    // below it and the footer keeps its height in every state of the storage
    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");
    QWidget* window = fixture.window.data();
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    pdf::PDFAppFooter* footer = homeTab->getFooter();
    pdf::PDFSegmentedControl* themeSwitch = homeTab->getSidebar()->getThemeSwitch();
    QVERIFY(footer && themeSwitch);

    // MIT License is Semi Bold 12 as the texts around it (app-footer 28:217), its
    // box keeps the line of 16 px of the component; its ink is not higher than the
    // ink of the version (Regular 12)
    QCOMPARE(footer->getLicenseLink()->getLinkSize(), pdf::PDFComponentPainter::TextLinkSize::Small);
    QCOMPARE(footer->getLicenseLink()->height(), 16);
    QCOMPARE(pdf::PDFComponentPainter::getTextLinkFont(footer->getLicenseLink()->getLinkSize(), false).pixelSize(), footer->getVersionLabel()->font().pixelSize());
    {
        fixture.window->resize(1440, 977);
        QCoreApplication::processEvents();
        const QImage image = renderWindow(window, 1.0);
        const QColor canvas = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas);
        auto inkHeight = [&image, &canvas, window](const QWidget* widget)
        {
            const QRect rect(widget->mapTo(window, QPoint(0, 0)), widget->size());
            int first = -1;
            int last = -1;
            for (int y = rect.top(); y <= rect.bottom(); ++y)
            {
                for (int x = rect.left(); x <= rect.right(); ++x)
                {
                    const QColor color = image.pixelColor(x, y);
                    if (qAbs(color.red() - canvas.red()) + qAbs(color.green() - canvas.green()) + qAbs(color.blue() - canvas.blue()) > 120)
                    {
                        first = first < 0 ? y : first;
                        last = y;
                        break;
                    }
                }
            }
            return first < 0 ? -1 : last - first + 1;
        };
        const int linkInk = inkHeight(footer->getLicenseLink());
        const int versionInk = inkHeight(footer->getVersionLabel());
        QVERIFY2(linkInk > 0 && versionInk > 0 && linkInk <= versionInk + 1, qPrintable(QStringLiteral("link %1, version %2").arg(linkInk).arg(versionInk)));
    }
    auto geometry = [window](const QWidget* widget) -> QRect
    {
        return QRect(widget->mapTo(window, QPoint(0, 0)), widget->size());
    };

    for (const QSize& size : { QSize(1440, 977), QSize(1100, 701), QSize(720, 480) })
    {
        fixture.window->resize(size);
        for (const pdf::PDFDatabaseStorageMode mode : { pdf::PDFDatabaseStorageMode::Disk, pdf::PDFDatabaseStorageMode::Portable,
                                                         pdf::PDFDatabaseStorageMode::MemoryFallback, pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema })
        {
            pdf::PDFDatabaseState state;
            state.mode = mode;
            footer->setState(state);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            const QString context = QStringLiteral("%1x%2 mode %3").arg(size.width()).arg(size.height()).arg(int(mode));

            const QRect switchRect = geometry(themeSwitch);
            const QRect textRect = geometry(footer->getVersionLabel());
            QVERIFY2(window->height() == size.height(), qPrintable(context));
            QVERIFY2(window->height() - (switchRect.bottom() + 1) == 8, qPrintable(context));
            QVERIFY2(window->height() - (textRect.bottom() + 1) == 16, qPrintable(context));
            QVERIFY2(footer->height() == 31 + 16, qPrintable(context));

            // Centers (doubled, so the halves are integers) within 1 px
            const int switchCenter2 = 2 * switchRect.top() + switchRect.height();
            for (const QWidget* text : { static_cast<const QWidget*>(footer->getVersionLabel()), static_cast<const QWidget*>(footer->getLicenseLink()) })
            {
                const QRect rect = geometry(text);
                QVERIFY2(qAbs(2 * rect.top() + rect.height() - switchCenter2) <= 2, qPrintable(context));
            }

            pdf::PDFTag* tag = footer->getTag();
            QCOMPARE(tag->isVisible(), mode != pdf::PDFDatabaseStorageMode::Disk);
            if (tag->isVisible())
            {
                const QRect tagRect = geometry(tag);
                QVERIFY2(tagRect.height() == 21, qPrintable(context));
                QVERIFY2(window->height() - (tagRect.bottom() + 1) == 13, qPrintable(context));
                QVERIFY2(textRect.top() - tagRect.top() == 3, qPrintable(context));
                QVERIFY2(tagRect.left() - (textRect.right() + 1) == 8, qPrintable(context));
            }

            // The drawn text at the device pixel ratios 1, 1.5 and 2: the center of its
            // ink lies on the axis of the switch (within 1 px), the tag around it
            for (const qreal scale : { 1.0, 1.5, 2.0 })
            {
                const QImage image = renderWindow(window, scale);
                const QColor canvas = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas);
                auto findInk = [&image, &canvas, scale](const QRect& rect, int* first, int* last)
                {
                    *first = -1;
                    *last = -1;
                    for (int y = qFloor(rect.top() * scale); y < qCeil((rect.bottom() + 1) * scale) && y < image.height(); ++y)
                    {
                        for (int x = qFloor(rect.left() * scale); x < qCeil((rect.right() + 1) * scale) && x < image.width(); ++x)
                        {
                            const QColor color = image.pixelColor(x, y);
                            if (qAbs(color.red() - canvas.red()) + qAbs(color.green() - canvas.green()) + qAbs(color.blue() - canvas.blue()) > 60)
                            {
                                *first = *first < 0 ? y : *first;
                                *last = y;
                                break;
                            }
                        }
                    }
                };

                int first = -1;
                int last = -1;
                findInk(textRect, &first, &last);
                QVERIFY2(first >= 0, qPrintable(context));
                const qreal inkCenter = (first + last + 1) / 2.0 / scale;
                const qreal switchCenter = switchCenter2 / 2.0;
                QVERIFY2(qAbs(inkCenter - switchCenter) <= 1.0, qPrintable(QStringLiteral("%1 scale %2: ink %3, switch %4").arg(context).arg(scale).arg(inkCenter).arg(switchCenter)));

                if (tag->isVisible())
                {
                    findInk(geometry(tag), &first, &last);
                    const qreal tagCenter = (first + last + 1) / 2.0 / scale;
                    QVERIFY2(qAbs(tagCenter - inkCenter) <= 1.0, qPrintable(QStringLiteral("%1 scale %2: tag %3, ink %4").arg(context).arg(scale).arg(tagCenter).arg(inkCenter)));
                }
            }
        }
    }

    // Right-to-left: the tag is beside the version on the left
    fixture.window->resize(1440, 977);
    pdf::PDFDatabaseState state;
    state.mode = pdf::PDFDatabaseStorageMode::Portable;
    footer->setState(state);
    fixture.window->setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(geometry(footer->getVersionLabel()).left() - (geometry(footer->getTag()).right() + 1), 8);

    // The separators " • " are labels of their own between the parts (Free • Open
    // Source, MIT License, Version), in both directions: a separator at the end of
    // the text of a part was drawn at the outer side of the part right-to-left
    // ("• Version 1.6.0 MIT License Free • Open Source •")
    QWidget* freeLabel = footer->findChild<QWidget*>(QStringLiteral("freeLabel"));
    const QList<pdf::PDFLabel*> separators = footer->findChildren<pdf::PDFLabel*>(QStringLiteral("footerSeparator"));
    QVERIFY(freeLabel);
    QCOMPARE(separators.size(), 2);
    for (const pdf::PDFLabel* label : footer->findChildren<pdf::PDFLabel*>())
    {
        if (!label->objectName().startsWith(QStringLiteral("footerSeparator")) && label->parentWidget() == footer)
        {
            const QString text = label->text().trimmed();
            QVERIFY2(!text.startsWith(QChar(0x2022)) && !text.endsWith(QChar(0x2022)), qPrintable(label->text()));
        }
    }
    for (const Qt::LayoutDirection direction : { Qt::RightToLeft, Qt::LeftToRight })
    {
        fixture.window->setLayoutDirection(direction);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        // Reading order: Free • Open Source, separator, MIT License, separator, Version
        QList<QRect> parts = { geometry(freeLabel), geometry(separators[0]), geometry(footer->getLicenseLink()), geometry(separators[1]), geometry(footer->getVersionLabel()) };
        if (direction == Qt::RightToLeft)
        {
            std::reverse(parts.begin(), parts.end());
        }
        for (qsizetype i = 1; i < parts.size(); ++i)
        {
            QVERIFY2(parts[i].left() == parts[i - 1].right() + 1, qPrintable(QStringLiteral("direction %1 part %2").arg(int(direction)).arg(i)));
        }

        // The separator is drawn in its own box: the ink of the bullet lies in the
        // middle third of it (pixels at the device pixel ratio 1)
        const QImage image = renderWindow(window, 1.0);
        const QColor canvas = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas);
        for (const pdf::PDFLabel* separator : separators)
        {
            const QRect rect = geometry(separator);
            int inkLeft = -1;
            int inkRight = -1;
            for (int x = rect.left(); x <= rect.right(); ++x)
            {
                for (int y = rect.top(); y <= rect.bottom(); ++y)
                {
                    const QColor color = image.pixelColor(x, y);
                    if (qAbs(color.red() - canvas.red()) + qAbs(color.green() - canvas.green()) + qAbs(color.blue() - canvas.blue()) > 60)
                    {
                        inkLeft = inkLeft < 0 ? x : inkLeft;
                        inkRight = x;
                        break;
                    }
                }
            }
            QVERIFY(inkLeft >= 0);
            QVERIFY2(inkLeft - rect.left() >= rect.width() / 4 && rect.right() - inkRight >= rect.width() / 4,
                     qPrintable(QStringLiteral("separator %1 - %2, ink %3 - %4").arg(rect.left()).arg(rect.right()).arg(inkLeft).arg(inkRight)));
        }
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);

    // A change of the language translates every text of the footer again: the tag
    // of the stored state, its place in the layout (8 + its width), the version,
    // the license and the targets (the texts kept the old language before)
    {
        auto tagSpacerWidth = [footer]() -> int
        {
            for (int i = 0; i < footer->layout()->count(); ++i)
            {
                QSpacerItem* spacer = footer->layout()->itemAt(i)->spacerItem();
                if (spacer && spacer->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed)
                {
                    return spacer->geometry().width();
                }
            }
            return -1;
        };
        pdf::PDFDatabaseState portable;
        portable.mode = pdf::PDFDatabaseStorageMode::Portable;
        footer->setState(portable);
        QCoreApplication::processEvents();
        const int tagWidth = footer->getTag()->width();
        QCOMPARE(tagSpacerWidth(), 8 + tagWidth);

        // The buttons of the banner follow the language too
        pdf::PDFAppStorageBanner* banner = homeTab->getBanner();
        pdf::PDFDatabaseState fallback;
        fallback.mode = pdf::PDFDatabaseStorageMode::MemoryFallback;
        fallback.filePath = QStringLiteral("C:/data/pdf4qt.db");
        banner->setState(fallback);

        FooterTranslator translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(banner->getTryAgainButton()->text(), QStringLiteral("[Try Again]"));
        QCOMPARE(banner->getShowInFolderButton()->text(), QStringLiteral("[Show in Folder]"));
        QVERIFY(banner->getTitle().startsWith(QLatin1Char('[')));
        QCOMPARE(footer->getTagText(), QStringLiteral("[Portable]"));
        QCOMPARE(footer->getVersionLabel()->text(), QStringLiteral("[Version %1]").arg(pdf::PDFAppFooter::getDisplayVersion()));
        QCOMPARE(footer->getLicenseLink()->text(), QStringLiteral("[MIT License]"));
        QCOMPARE(footer->findChild<pdf::PDFLabel*>(QStringLiteral("freeLabel"))->text(), QString::fromUtf8("[Free \xE2\x80\xA2 Open Source]"));
        QStringList targetTexts;
        for (const pdf::PDFLabel* label : footer->getTargets()->findChildren<pdf::PDFLabel*>())
        {
            targetTexts << label->text();
            QVERIFY(label->isVisible());
            QCOMPARE(label->width(), label->sizeHint().width());
        }
        QCOMPARE(targetTexts, QStringList({ QStringLiteral("[Windows]"), QStringLiteral("[Linux]"), QStringLiteral("[macOS]") }));
        QVERIFY(footer->getTag()->width() > tagWidth);
        QCOMPARE(footer->getTag()->width(), footer->getTag()->sizeHint().width());
        QCOMPARE(tagSpacerWidth(), 8 + footer->getTag()->width());
        QCOMPARE(geometry(footer->getTag()).left() - (geometry(footer->getVersionLabel()).right() + 1), 8);

        QVERIFY(QCoreApplication::removeTranslator(&translator));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(footer->getTagText(), QStringLiteral("Portable"));
        QCOMPARE(footer->getLicenseLink()->text(), QStringLiteral("MIT License"));
        QCOMPARE(banner->getTryAgainButton()->text(), QStringLiteral("Try Again"));
        banner->setState(portable);
        QCOMPARE(tagSpacerWidth(), 8 + tagWidth);
    }

    // The New tab has no footer, its workspace keeps the bottom padding 32
    pdf::PDFAppTab* newTab = fixture.window->openNewTab();
    QVERIFY(newTab);
    QCoreApplication::processEvents();
    QWidget* workspace = newTab->findChild<QWidget*>(QStringLiteral("workspace"));
    QVERIFY(workspace && workspace->layout());
    QCOMPARE(workspace->layout()->contentsMargins().bottom(), 32);
    QVERIFY(!newTab->findChild<pdf::PDFAppFooter*>());
}

void AppNavigationTest::minimumWindow()
{
    // The minimal window (720 x 480) with the banner: the page and the navigation
    // scroll, the header, the banner, the footer and the switch stay; no two
    // visible widgets overlap
    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");
    QWidget* window = fixture.window.data();
    fixture.window->resize(fixture.window->minimumSize());
    QTest::qWait(50);
    QCOMPARE(window->size(), QSize(720, 480));

    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    QVERIFY(homeTab->getBanner()->isVisible());

    // Visible part of a widget: its rectangle clipped by all its parents
    auto visibleRect = [window](const QWidget* widget) -> QRect
    {
        QRect rect(widget->mapTo(window, QPoint(0, 0)), widget->size());
        for (const QWidget* parent = widget->parentWidget(); parent && parent != window; parent = parent->parentWidget())
        {
            rect &= QRect(parent->mapTo(window, QPoint(0, 0)), parent->size());
        }
        return rect & window->rect();
    };

    QList<QWidget*> widgets;
    for (QWidget* child : window->findChildren<QWidget*>())
    {
        if (child->isVisibleTo(window) && !child->isWindow() && !qobject_cast<QFocusFrame*>(child) && !visibleRect(child).isEmpty())
        {
            widgets << child;
        }
    }

    QStringList overlaps;
    for (qsizetype i = 0; i < widgets.size(); ++i)
    {
        for (qsizetype j = i + 1; j < widgets.size(); ++j)
        {
            QWidget* first = widgets[i];
            QWidget* second = widgets[j];
            if (first->isAncestorOf(second) || second->isAncestorOf(first))
            {
                continue;
            }

            // The overlay scroll bar of the page (and its container) lies over the
            // viewport and the content widget of its area, never over a part of the page
            auto overlayArea = [](const QWidget* widget) -> const pdf::PDFAppScrollArea*
            {
                // The scroll bar container (a child of the area) or the scroll bar in it
                const QWidget* parent = widget->parentWidget();
                const pdf::PDFAppScrollArea* area = qobject_cast<const pdf::PDFAppScrollArea*>(parent);
                if (area && area->isOverlayScrollBars() && widget == area->verticalScrollBar()->parentWidget())
                {
                    return area;
                }
                area = parent ? qobject_cast<const pdf::PDFAppScrollArea*>(parent->parentWidget()) : nullptr;
                return area && area->isOverlayScrollBars() && widget == area->verticalScrollBar() ? area : nullptr;
            };
            auto isOverlayPair = [&overlayArea](const QWidget* bar, const QWidget* other)
            {
                const pdf::PDFAppScrollArea* area = overlayArea(bar);
                return area && (other == area->viewport() || other == area->widget());
            };
            if (isOverlayPair(first, second) || isOverlayPair(second, first))
            {
                continue;
            }

            if (visibleRect(first).intersects(visibleRect(second)))
            {
                overlaps << QStringLiteral("%1 %2 - %3 %4").arg(first->metaObject()->className(), first->objectName(), second->metaObject()->className(), second->objectName());
            }
        }
    }
    QVERIFY2(overlaps.isEmpty(), qPrintable(overlaps.join(QLatin1Char('\n'))));

    // The page and the navigation scroll
    pdf::PDFAppScrollArea* pageArea = homeTab->getPageScrollArea();
    pdf::PDFAppScrollArea* navigationArea = homeTab->getSidebar()->getNavigationArea();
    QVERIFY(pageArea->verticalScrollBar()->maximum() > 0);
    QVERIFY(navigationArea->verticalScrollBar()->maximum() > 0);
    QCOMPARE(navigationArea->horizontalScrollBar()->maximum(), 0);

    // The last item of the navigation and the bottom of the page can be reached
    navigationArea->verticalScrollBar()->setValue(navigationArea->verticalScrollBar()->maximum());
    pageArea->verticalScrollBar()->setValue(pageArea->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    pdf::PDFNavItem* settings = homeTab->getSidebar()->getNavigation()->getItem(int(pdf::PDFAppNavItem::Settings));
    QVERIFY(settings);
    QCOMPARE(visibleRect(settings).size(), settings->size());
    QWidget* hints = window->findChild<QWidget*>(QStringLiteral("notFoundHints"));
    QVERIFY(hints);
    QCOMPARE(visibleRect(hints).height(), hints->height());

    // The switch and the footer are whole (the targets are hidden, they do not fit)
    pdf::PDFSegmentedControl* themeSwitch = homeTab->getSidebar()->getThemeSwitch();
    pdf::PDFAppFooter* footer = homeTab->getFooter();
    QCOMPARE(visibleRect(themeSwitch).size(), themeSwitch->size());
    QCOMPARE(visibleRect(footer).size(), footer->size());
    for (QWidget* child : { static_cast<QWidget*>(footer->getLicenseLink()), static_cast<QWidget*>(footer->getVersionLabel()), static_cast<QWidget*>(footer->getTag()) })
    {
        QVERIFY(child->isVisible());
        QCOMPARE(visibleRect(child).size(), child->size());
    }
    QVERIFY(footer->getTargets());
    for (const pdf::PDFLabel* label : footer->getTargets()->findChildren<pdf::PDFLabel*>())
    {
        QVERIFY(!label->isVisible());
    }

    // A logo hides with its text
    const QList<QWidget*> logos = footer->getTargets()->findChildren<QWidget*>(QStringLiteral("footerLogo"));
    QCOMPARE(logos.size(), 3);
    for (const QWidget* logo : logos)
    {
        QVERIFY(!logo->isVisible());
    }

    // The targets come back in a wide window
    fixture.window->resize(1440, 977);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const QList<pdf::PDFLabel*> targets = footer->getTargets()->findChildren<pdf::PDFLabel*>();
    QCOMPARE(targets.size(), 3);
    for (const pdf::PDFLabel* label : targets)
    {
        QVERIFY(label->isVisible());
    }

    // footer-os-targets 28:219: each target is a logo of 14 px (windows-logo 30:102,
    // linux-logo 74:799, apple-logo 30:106 in icon/default), 6 px and its text; the
    // logos lie on the axis of the switch and the targets end at the right edge of
    // the content (32 px from the window)
    {
        QWidget* targetsWidget = footer->getTargets();
        auto geometry = [window](const QWidget* widget) -> QRect
        {
            return QRect(widget->mapTo(window, QPoint(0, 0)), widget->size());
        };
        const QRect switchRect = geometry(themeSwitch);
        const QList<QWidget*> wideLogos = targetsWidget->findChildren<QWidget*>(QStringLiteral("footerLogo"));
        QCOMPARE(wideLogos.size(), 3);
        QCOMPARE(geometry(targets.last()).right() + 1, window->width() - 32);
        const QColor iconColor = pdf::PDFStylePainter::color(pdf::PDFThemeColors::IconDefault);
        for (qsizetype i = 0; i < wideLogos.size(); ++i)
        {
            const QRect logoRect = geometry(wideLogos[i]);
            const QRect labelRect = geometry(targets[i]);
            QVERIFY(wideLogos[i]->isVisible());
            QCOMPARE(logoRect.size(), QSize(14, 14));
            QCOMPARE(labelRect.left() - (logoRect.right() + 1), 6);
            QVERIFY(qAbs((2 * logoRect.top() + logoRect.height()) - (2 * switchRect.top() + switchRect.height())) <= 2);
            if (i > 0)
            {
                QCOMPARE(logoRect.left() - (geometry(targets[i - 1]).right() + 1), 16);
            }

            // The logo is drawn: ink of icon/default inside of its box at 1, 1.5 and 2
            for (const qreal scale : { 1.0, 1.5, 2.0 })
            {
                const QImage image = renderWindow(window, scale);
                int ink = 0;
                int exact = 0;
                for (int y = qFloor(logoRect.top() * scale); y < qCeil((logoRect.bottom() + 1) * scale); ++y)
                {
                    for (int x = qFloor(logoRect.left() * scale); x < qCeil((logoRect.right() + 1) * scale); ++x)
                    {
                        const QColor color = image.pixelColor(x, y);
                        if (color.lightness() < 200)
                        {
                            ++ink;
                        }
                        if (color.rgb() == iconColor.rgb())
                        {
                            ++exact;
                        }
                    }
                }
                const int area = int(14 * 14 * scale * scale);
                QVERIFY2(ink > area / 6 && exact > area / 20, qPrintable(QStringLiteral("logo %1 scale %2: ink %3, exact %4").arg(i).arg(scale).arg(ink).arg(exact)));
            }
        }

        // Right-to-left the logo is at the right side of its text
        fixture.window->setLayoutDirection(Qt::RightToLeft);
        QCoreApplication::processEvents();
        for (qsizetype i = 0; i < wideLogos.size(); ++i)
        {
            QCOMPARE(geometry(wideLogos[i]).left() - (geometry(targets[i]).right() + 1), 6);
        }
        fixture.window->setLayoutDirection(Qt::LeftToRight);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pageArea->verticalScrollBar()->maximum(), 0);
    QCOMPARE(navigationArea->verticalScrollBar()->maximum(), 0);

    // The viewports and the contents of the scroll areas draw nothing: no leak color
    const QImage image = renderWindow(window, 1.0);
    const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            QVERIFY2((image.pixel(x, y) & 0xFFFFFF) != (leak & 0xFFFFFF), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
        }
    }

    // The banners with buttons (958:28036) in the minimal window: the banner scrolls
    // with the page (a fixed banner of the fallback was cut to 185 px: its text ended
    // after four lines and the page kept 55 px), the buttons move below the text
    // (beside it the text was a column of 120 px); in a wide window they are beside
    // the text, centered on the banner
    pdf::PDFAppStorageBanner* banner = homeTab->getBanner();
    QVERIFY(pageArea->widget()->isAncestorOf(banner));
    struct BannerCase
    {
        pdf::PDFDatabaseStorageMode mode;
        QString damaged;
    };
    for (const BannerCase& bannerCase : { BannerCase{ pdf::PDFDatabaseStorageMode::MemoryFallback, QString() },
                                          BannerCase{ pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema, QString() },
                                          BannerCase{ pdf::PDFDatabaseStorageMode::Disk, QStringLiteral("C:/data/pdf4qt.db.damaged-2026-10-18") } })
    {
        pdf::PDFDatabaseState state;
        state.mode = bannerCase.mode;
        state.filePath = QStringLiteral("C:/data/pdf4qt.db");
        state.damagedFilePath = bannerCase.damaged;
        state.error = pdf::PDFDatabaseError::fromMessage(QString(200, QLatin1Char('x')).replace(QLatin1Char('x'), QStringLiteral("disk I/O error ")));
        banner->setState(state);
        QVERIFY(banner->isVisible());

        for (const QSize& size : { QSize(720, 480), QSize(1440, 977) })
        {
            for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
            {
                fixture.window->resize(size);
                fixture.window->setLayoutDirection(direction);
                QTest::qWait(50);
                pageArea->verticalScrollBar()->setValue(0);
                QCoreApplication::processEvents();
                const QString context = QStringLiteral("mode %1, %2x%3, direction %4").arg(int(bannerCase.mode)).arg(size.width()).arg(size.height()).arg(int(direction));

                // The paragraph has all its lines, the buttons lie inside of the banner
                pdf::PDFAppParagraph* text = banner->findChild<pdf::PDFAppParagraph*>(QStringLiteral("bannerText"));
                QWidget* actions = banner->findChild<QWidget*>(QStringLiteral("bannerActions"));
                QVERIFY2(text && actions && actions->isVisible(), qPrintable(context));
                QVERIFY2(text->height() >= text->heightForWidth(text->width()), qPrintable(context));
                QVERIFY2(banner->height() >= banner->heightForWidth(banner->width()), qPrintable(context));
                const QRect actionsRect(actions->mapTo(banner, QPoint(0, 0)), actions->size());
                const QRect textRect(text->mapTo(banner, QPoint(0, 0)), text->size());
                QVERIFY2(banner->rect().contains(actionsRect), qPrintable(context));
                QVERIFY2(textRect.width() >= pdf::PDFAppStorageBanner::MIN_TEXT_WIDTH || size.width() == 720, qPrintable(context));

                const bool isBelow = size.width() == 720;
                QVERIFY2(banner->isActionsBelowText() == isBelow, qPrintable(context));
                if (isBelow)
                {
                    // 12 px below the text, at its leading edge
                    const QRect visibleButtons = actions->childrenRect().translated(actionsRect.topLeft());
                    QVERIFY2(visibleButtons.top() == textRect.bottom() + 1 + 12, qPrintable(context));
                    if (direction == Qt::LeftToRight)
                    {
                        QVERIFY2(visibleButtons.left() == textRect.left(), qPrintable(context));
                    }
                    else
                    {
                        QVERIFY2(visibleButtons.right() == textRect.right(), qPrintable(context));
                    }
                }
                else
                {
                    // Beside the text, 12 px from it, centered on the banner
                    QVERIFY2(qAbs((2 * actionsRect.top() + actionsRect.height()) - banner->height()) <= 1, qPrintable(context));
                    QVERIFY2(direction == Qt::LeftToRight ? actionsRect.left() == textRect.right() + 1 + 12 : textRect.left() == actionsRect.right() + 1 + 12, qPrintable(context));
                }

                // The whole banner is reachable by the scroll bar, nothing overlaps the footer
                const QRect bannerInViewport(banner->mapTo(pageArea->viewport(), QPoint(0, 0)), banner->size());
                QVERIFY2(bannerInViewport.top() == 0, qPrintable(context));
                QVERIFY2(pageArea->widget()->height() >= banner->height() + 24, qPrintable(context));

                // No horizontal scroll bar: the banner is whole in the viewport (the
                // text 460 and the row of the buttons 473 of Page Not Found made the
                // page wider than the minimal window and cut the banner at its right)
                QVERIFY2(pageArea->horizontalScrollBar()->maximum() == 0, qPrintable(context));
                QVERIFY2(pageArea->viewport()->rect().contains(bannerInViewport), qPrintable(context));

                // The buttons of Page Not Found: a row (Figma) or a column in the minimal window
                QWidget* notFoundActions = window->findChild<QWidget*>(QStringLiteral("notFoundActions"));
                QVERIFY2(notFoundActions, qPrintable(context));
                QList<QRect> buttonRects;
                for (const char* name : { "notFoundBack", "notFoundSearch", "notFoundHome" })
                {
                    const QWidget* button = notFoundActions->findChild<QWidget*>(QString::fromLatin1(name));
                    QVERIFY2(button && button->isVisibleTo(notFoundActions), qPrintable(context));
                    QVERIFY2(notFoundActions->rect().contains(button->geometry()), qPrintable(context));
                    buttonRects << button->geometry();
                }
                for (qsizetype i = 1; i < buttonRects.size(); ++i)
                {
                    const QRect& previous = buttonRects[i - 1];
                    const QRect& current = buttonRects[i];
                    if (isBelow)
                    {
                        QVERIFY2(current.top() == previous.bottom() + 1 + 12 && current.center().x() / 2 == previous.center().x() / 2, qPrintable(context));
                    }
                    else if (direction == Qt::LeftToRight)
                    {
                        QVERIFY2(current.top() == previous.top() && current.left() == previous.right() + 1 + 12, qPrintable(context));
                    }
                    else
                    {
                        QVERIFY2(current.top() == previous.top() && previous.left() == current.right() + 1 + 12, qPrintable(context));
                    }
                }
                pdf::PDFAppParagraph* notFoundText = window->findChild<pdf::PDFAppParagraph*>(QStringLiteral("notFoundText"));
                QVERIFY2(notFoundText && notFoundText->height() == notFoundText->heightForWidth(notFoundText->width()), qPrintable(context));
                QVERIFY2(notFoundText->width() == (isBelow ? pageArea->viewport()->width() - 2 * homeTab->getPageInset() : 460), qPrintable(context));
            }
        }
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);
}

void AppNavigationTest::notFoundNarrowWindow()
{
    // Page Not Found of a long requested route in the minimal window: the requested
    // item, the route and Search for … are elided to the page (they were elided to
    // 460 px and the search text not at all, so the page was wider than the window
    // and had a horizontal scroll bar); the whole texts stay in the tooltips
    NavigationFixture fixture;
    QWidget* window = fixture.window.data();
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    pdf::PDFAppScrollArea* pageArea = homeTab->getPageScrollArea();
    const QString toolId = QStringLiteral("split-pages-by-bookmarks-and-merge-them-into-one-file-quickly-and-safely");
    fixture.navigate(QStringLiteral("/tools/") + toolId);
    pdf::PDFPageNotFoundPage* page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QTest::qWait(50);
    const QString searchText = page->getRequest().searchText;
    QVERIFY(searchText.size() >= 40);
    const QString fullSearchText = QStringLiteral("Search for \u201C%1\u201D").arg(searchText);

    auto checkPage = [&](const QString& context)
    {
        QVERIFY2(pageArea->horizontalScrollBar()->maximum() == 0, qPrintable(context));
        const QRect viewport = pageArea->viewport()->rect();
        QList<QWidget*> parts = { page->findChild<QWidget*>(QStringLiteral("requestedPath")), page->findChild<QWidget*>(QStringLiteral("notFoundActions")),
                                  page->getBackButton(), page->getSearchButton(), page->getHomeButton() };
        for (QWidget* part : parts)
        {
            QVERIFY2(part && part->isVisible(), qPrintable(context));
            const QRect rect(part->mapTo(pageArea->viewport(), QPoint(0, 0)), part->size());
            QVERIFY2(rect.left() >= viewport.left() && rect.right() <= viewport.right(), qPrintable(QStringLiteral("%1: %2").arg(context, part->objectName())));
            if (QPushButton* button = qobject_cast<QPushButton*>(part))
            {
                // A button has its whole (elided) text
                QVERIFY2(button->width() == button->sizeHint().width(), qPrintable(QStringLiteral("%1: %2").arg(context, part->objectName())));
            }
        }
        for (const char* name : { "requestedItem", "requestedRoute" })
        {
            const QLabel* label = page->findChild<QLabel*>(QString::fromLatin1(name));
            QVERIFY2(label && label->width() >= label->sizeHint().width(), qPrintable(QStringLiteral("%1: %2").arg(context, QString::fromLatin1(name))));
        }
    };

    // The wide window: the search text is whole
    QCOMPARE(page->getSearchButton()->text(), fullSearchText);
    QVERIFY(page->getSearchButton()->toolTip().isEmpty());
    checkPage(QStringLiteral("wide"));

    fixture.window->resize(fixture.window->minimumSize());
    QTest::qWait(50);
    QCOMPARE(window->size(), QSize(720, 480));
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        fixture.window->setLayoutDirection(direction);
        QTest::qWait(50);
        const QString context = QStringLiteral("minimal window, direction %1").arg(int(direction));
        checkPage(context);
        QVERIFY2(page->getSearchButton()->text() != fullSearchText && page->getSearchButton()->text().contains(QChar(0x2026)), qPrintable(page->getSearchButton()->text()));
        QCOMPARE(page->getSearchButton()->toolTip(), fullSearchText);
        QVERIFY2(page->getPathText().contains(QChar(0x2026)), qPrintable(page->getPathText()));
        QCOMPARE(page->getRequest().searchText, searchText);
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);
    QTest::qWait(50);

    // A request shown while the window is hidden is elided, when the page is shown
    pdf::PDFAppNotFoundRequest request = page->getRequest();
    request.description = QStringLiteral("Tool %1").arg(toolId + toolId);
    request.route = pdf::PDFAppRoute::parse(QStringLiteral("/tools/") + toolId + QStringLiteral("-") + toolId);
    fixture.window->hide();
    QCoreApplication::processEvents();
    page->setRequest(request);
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(50);
    checkPage(QStringLiteral("shown again"));
    QVERIFY2(page->getDescriptionText().contains(QChar(0x2026)), qPrintable(page->getDescriptionText()));

    // A clicked search looks for the whole text
    QTest::mouseClick(page->getSearchButton(), Qt::LeftButton);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::search(searchText));

    // The buttons of a page in a hidden window follow a change of their texts, when
    // the window is shown again: the row has no layout, a button tells only a visible
    // parent about its new size, so the row places its buttons again at its show
    fixture.window->resize(1440, 977);
    fixture.navigate(QStringLiteral("/tools/convert.epub"));
    page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QTest::qWait(50);
    checkPage(QStringLiteral("wide again"));
    fixture.window->hide();
    QCoreApplication::processEvents();
    page->getBackButton()->setText(QStringLiteral("Back to the previous page"));
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(50);
    checkPage(QStringLiteral("changed while hidden"));
    QCOMPARE(page->getSearchButton()->geometry().left(), page->getBackButton()->geometry().right() + 1 + 12);

    // A new language (also the hints of the page change then)
    fixture.window->hide();
    QCoreApplication::processEvents();
    NotFoundButtonTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(page->getHomeButton()->text(), QStringLiteral("[Go to Home]"));
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(50);
    checkPage(QStringLiteral("translated"));
    const QRect back = page->getBackButton()->geometry();
    const QRect search = page->getSearchButton()->geometry();
    const QRect home = page->getHomeButton()->geometry();
    QCOMPARE(search.left(), back.right() + 1 + 12);
    QCOMPARE(home.left(), search.right() + 1 + 12);
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QTest::qWait(50);
    checkPage(QStringLiteral("translator removed"));
}

void AppNavigationTest::rightToLeftIcons()
{
    // Qt does not mirror icons: the chevron of Back (Page Not Found) and the icons
    // of the commands Back / Forward / Next tab / Previous tab follow the direction
    using Page = pdf::PDFAppPage;
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("chevron-left"), Qt::LeftToRight), QStringLiteral("chevron-left"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("chevron-left"), Qt::RightToLeft), QStringLiteral("chevron-right"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("chevron-right"), Qt::RightToLeft), QStringLiteral("chevron-left"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("chevron-first"), Qt::RightToLeft), QStringLiteral("chevron-last"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("arrow-right"), Qt::RightToLeft), QStringLiteral("arrow-left"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("plus"), Qt::RightToLeft), QStringLiteral("plus"));
    QCOMPARE(Page::getDirectionalIconName(QStringLiteral("home"), Qt::RightToLeft), QStringLiteral("home"));
    QCOMPARE(Page::getDirectionalIconName(QString(), Qt::RightToLeft), QString());

    const QImage left = iconImage(pdf::PDFThemeIcons::getIcon(QStringLiteral("chevron-left")));
    const QImage right = iconImage(pdf::PDFThemeIcons::getIcon(QStringLiteral("chevron-right")));
    QVERIFY(left != right);

    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");
    pdf::PDFPageNotFoundPage* page = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(iconImage(page->getBackButton()->icon()), left);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::back())->icon()), left);

    fixture.window->setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    QCOMPARE(iconImage(page->getBackButton()->icon()), right);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::back())->icon()), right);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::forward())->icon()), left);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::nextTab())->icon()), left);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::previousTab())->icon()), right);

    // A page created in the right-to-left window, and back to left-to-right
    fixture.navigate("/tools/other.tool");
    pdf::PDFPageNotFoundPage* otherPage = qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage());
    QVERIFY(otherPage && otherPage != page);
    QCOMPARE(otherPage->layoutDirection(), Qt::RightToLeft);
    QCOMPARE(iconImage(otherPage->getBackButton()->icon()), right);
    fixture.window->setLayoutDirection(Qt::LeftToRight);
    QCoreApplication::processEvents();
    QCOMPARE(iconImage(otherPage->getBackButton()->icon()), left);
    QCOMPARE(iconImage(fixture.window->getCommandAction(pdf::PDFAppCommands::back())->icon()), left);
}

void AppNavigationTest::navigationButtonsFilter()
{
    // The buttons back / forward of the mouse are taken from the QWindow of the main
    // window: one press is one step (the release and the double click do nothing),
    // a QWindow created again (a change of the flags of the window) is filtered once,
    // a modal dialog or a menu over the window blocks them, other buttons pass
    NavigationFixture fixture;
    for (int i = 1; i <= 5; ++i)
    {
        fixture.navigate(QStringLiteral("/tools/page%1.tool").arg(i));
    }
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    auto lastPath = [&navigated]() -> QString
    {
        return navigated.isEmpty() ? QString() : navigated.last().at(1).value<pdf::PDFAppRoute>().getPath();
    };

    pdf::PDFLineEdit* searchField = fixture.window->getHomeTab()->getSearchField();
    QVERIFY(searchField);
    auto fieldCenter = [&fixture, searchField]() -> QPoint
    {
        return searchField->mapTo(fixture.window.data(), searchField->rect().center());
    };

    QPointer<QWindow> windowHandle = fixture.window->windowHandle();
    QVERIFY(windowHandle);
    QTest::mousePress(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
    QTest::mouseRelease(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 1);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page4.tool"));

    // A double click of the button: two presses are two steps, the double click
    // event itself does nothing
    QTest::mouseDClick(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 3);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page2.tool"));
    QTest::mouseClick(windowHandle, Qt::ForwardButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 4);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page3.tool"));

    // Other buttons are not taken
    QTest::mouseClick(windowHandle, Qt::MiddleButton, Qt::NoModifier, fieldCenter());
    QTest::mouseClick(windowHandle, Qt::RightButton, Qt::NoModifier, fieldCenter());
    QTest::qWait(50);
    for (QWidget* popup = QApplication::activePopupWidget(); popup; popup = QApplication::activePopupWidget())
    {
        popup->close();
    }
    QCOMPARE(navigated.count(), 4);

    // A window modal dialog over the window
    {
        QDialog dialog(fixture.window.data());
        dialog.setWindowModality(Qt::WindowModal);
        dialog.resize(200, 100);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::mouseClick(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
        QCOMPARE(navigated.count(), 4);
        dialog.close();
    }

    // A menu (a popup) over the window: the press closes the menu, nothing more
    {
        QMenu menu(fixture.window.data());
        menu.addAction(QStringLiteral("Item"));
        menu.popup(fixture.window->mapToGlobal(QPoint(fixture.window->width() - 300, fixture.window->height() - 300)));
        QVERIFY(QTest::qWaitForWindowExposed(&menu));
        QTRY_VERIFY(QApplication::activePopupWidget() == &menu);
        QTest::mousePress(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
        QTest::mouseRelease(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
        QCOMPARE(navigated.count(), 4);
        QTRY_VERIFY(!menu.isVisible());
    }
    QTest::mouseClick(windowHandle, Qt::BackButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 5);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page2.tool"));

    // The window is created again (a change of its flags): the new QWindow is
    // filtered, a press is still one step
    fixture.window->setWindowFlags(fixture.window->windowFlags() | Qt::WindowStaysOnTopHint);
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window.data()));
    fixture.window->activateWindow();
    (void)QTest::qWaitForWindowActive(fixture.window.data());
    QVERIFY(fixture.window->windowHandle());
    QTest::mouseClick(fixture.window->windowHandle(), Qt::ForwardButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 6);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page3.tool"));

    // Shown again after a hide (the event Show comes again)
    fixture.window->hide();
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window.data()));
    QTest::mouseClick(fixture.window->windowHandle(), Qt::ForwardButton, Qt::NoModifier, fieldCenter());
    QCOMPARE(navigated.count(), 7);
    QCOMPARE(lastPath(), QStringLiteral("/tools/page4.tool"));

    // The window is deleted: its QWindow does not call it any more
    delete fixture.window.data();
    QVERIFY(!windowHandle);
}

void AppNavigationTest::scrollAreas()
{
    // The minimal window: a new page starts at its top (a changed title keeps the
    // position), and a child focused by the keyboard (the arrows of the navigation,
    // Tab) is scrolled into the view
    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");
    fixture.window->resize(fixture.window->minimumSize());
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    pdf::PDFAppScrollArea* pageArea = homeTab->getPageScrollArea();
    QScrollBar* pageBar = pageArea->verticalScrollBar();
    QVERIFY(pageBar->maximum() > 0);
    pageBar->setValue(pageBar->maximum());
    QVERIFY(pageBar->value() > 0);

    // The same page with a new title keeps the position
    const int position = pageBar->value();
    Q_EMIT homeTab->getPage()->titleChanged();
    QCOMPARE(pageBar->value(), position);

    fixture.navigate("/tools/other.tool");
    QCoreApplication::processEvents();
    QCOMPARE(pageBar->value(), 0);

    // Back: a page of the history starts at its top too
    pageBar->setValue(pageBar->maximum());
    QVERIFY(fixture.navigator()->goBack());
    QCoreApplication::processEvents();
    QCOMPARE(pageBar->value(), 0);

    // The navigation of the sidebar: the arrows move the focus to Settings, the
    // last item, which is below the visible part
    pdf::PDFAppScrollArea* navigationArea = homeTab->getSidebar()->getNavigationArea();
    pdf::PDFNavList* navigation = homeTab->getSidebar()->getNavigation();
    QScrollBar* navigationBar = navigationArea->verticalScrollBar();
    QVERIFY(navigationBar->maximum() > 0);
    navigationBar->setValue(0);
    pdf::PDFNavItem* first = navigation->getItem(0);
    pdf::PDFNavItem* settings = navigation->getItem(int(pdf::PDFAppNavItem::Settings));
    QVERIFY(first && settings);
    first->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(first->hasFocus());
    for (int i = 0; i < 20 && !settings->hasFocus(); ++i)
    {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    }
    QVERIFY(settings->hasFocus());
    QCoreApplication::processEvents();
    const QRect visible = navigationArea->viewport()->rect();
    const QRect settingsRect(settings->mapTo(navigationArea->viewport(), QPoint(0, 0)), settings->size());
    QVERIFY2(visible.contains(settingsRect), qPrintable(QStringLiteral("viewport %1, item %2").arg(visible.height()).arg(settingsRect.top())));

    // A click focuses a widget without moving the content under the pointer (a
    // widget taller than the view was centered by the press, so the release hit
    // another place); the keyboard focus still moves it into the view
    {
        QWidget container;
        container.resize(300, 200);
        QVBoxLayout* containerLayout = new QVBoxLayout(&container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        QWidget* content = new QWidget();
        QVBoxLayout* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->addSpacing(100);
        QWidget* tall = new QWidget(content);
        tall->setObjectName(QStringLiteral("tall"));
        tall->setFocusPolicy(Qt::StrongFocus);
        tall->setFixedHeight(1000);
        contentLayout->addWidget(tall);
        pdf::PDFAppScrollArea* area = new pdf::PDFAppScrollArea(content, &container);
        containerLayout->addWidget(area);
        container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));
        container.activateWindow();
        (void)QTest::qWaitForWindowActive(&container);
        QCoreApplication::processEvents();

        // The activation of the window may have focused the widget already
        tall->clearFocus();
        QVERIFY(!tall->hasFocus());
        QScrollBar* bar = area->verticalScrollBar();
        QVERIFY(bar->maximum() > 0);
        bar->setValue(50);
        const QPoint windowPoint = tall->mapTo(&container, QPoint(20, 30));
        QTest::mousePress(container.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint);
        QVERIFY(tall->hasFocus());
        QCOMPARE(bar->value(), 50);
        QTest::mouseRelease(container.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint);
        QCOMPARE(bar->value(), 50);

        tall->clearFocus();
        bar->setValue(0);
        tall->setFocus(Qt::TabFocusReason);
        QVERIFY(tall->hasFocus());
        QVERIFY(bar->value() > 0);
    }
}

/// The pixel of \p image most different from its corner (the background): the
/// color of a fully covered pixel of a text
static QColor strongestPixel(const QImage& image)
{
    const QColor background = image.pixelColor(0, 0);
    QColor result = background;
    int bestDistance = -1;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            const int distance = qAbs(color.red() - background.red()) + qAbs(color.green() - background.green()) + qAbs(color.blue() - background.blue());
            if (distance > bestDistance)
            {
                bestDistance = distance;
                result = color;
            }
        }
    }
    return result;
}

static int colorDistance(const QColor& first, const QColor& second)
{
    return qAbs(first.red() - second.red()) + qAbs(first.green() - second.green()) + qAbs(first.blue() - second.blue());
}

void AppNavigationTest::openDocumentsRecent()
{
    // Step 2: openDocuments records the existing files in Recent (at most the files of
    // one request), keeps the requested files once, reports a failed record and
    // records nothing without a database
    NavigationFixture fixture;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList files;
    for (int i = 0; i < int(pdf::PDFAppInstanceProtocol::MAXIMUM_FILES) + 2; ++i)
    {
        const QString path = directory.filePath(QStringLiteral("file%1.pdf").arg(i, 3, 10, QLatin1Char('0')));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7\n");
        files << path;
    }
    const QString missing = directory.filePath(QStringLiteral("missing.pdf"));

    // A duplicate is requested once, but recorded twice (opened twice); a missing file
    // and a folder are requested, not recorded
    fixture.window->openDocuments({ files[0], files[0], missing, directory.path() }, 0);
    QCOMPARE(fixture.window->getRequestedFiles(), QStringList({ files[0], missing, directory.path() }));
    QCOMPARE(fixture.context->getRecent().getFileCount(), 1);
    QCOMPARE(fixture.context->getRecent().getFile(files[0])->openCount, qint64(2));
    QTRY_COMPARE(fixture.window->findChildren<pdf::PDFMessageBox*>().size(), 1);

    // At most MAXIMUM_FILES files of one request are recorded and requested (the list
    // of Recent keeps the newest of them by its own limit)
    fixture.window->openDocuments(files, 0);
    QVERIFY(fixture.context->getRecent().getFile(files[pdf::PDFAppInstanceProtocol::MAXIMUM_FILES - 1]));
    QVERIFY(!fixture.context->getRecent().getFile(files[pdf::PDFAppInstanceProtocol::MAXIMUM_FILES]));
    QVERIFY(!fixture.context->getRecent().getFile(files.last()));
    QCOMPARE(fixture.window->getRequestedFiles().size(), qsizetype(pdf::PDFAppInstanceProtocol::MAXIMUM_FILES));

    // A failed record is reported and the next request is still recorded
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(connection);
    const QString kept = files[pdf::PDFAppInstanceProtocol::MAXIMUM_FILES - 1];
    const std::optional<pdf::PDFRecentFile> keptFile = fixture.context->getRecent().getFile(kept);
    QVERIFY(keptFile);
    const qint64 openCount = keptFile->openCount;
    QVERIFY(!connection->execute("CREATE TEMP TRIGGER fault_update BEFORE UPDATE ON main.recent_file BEGIN SELECT RAISE(ABORT, 'injected fault'); END").isError());
    QVERIFY(!connection->execute("CREATE TEMP TRIGGER fault_insert BEFORE INSERT ON main.recent_file BEGIN SELECT RAISE(ABORT, 'injected fault'); END").isError());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("The file cannot be recorded in Recent"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("The file cannot be recorded in Recent"));
    fixture.window->openDocuments({ kept, files[0] }, 0);
    QCOMPARE(fixture.context->getRecent().getFile(kept)->openCount, openCount);
    QVERIFY(!fixture.context->getRecent().getFile(files[0]));
    QVERIFY(!connection->execute("DROP TRIGGER temp.fault_update").isError());
    QVERIFY(!connection->execute("DROP TRIGGER temp.fault_insert").isError());
    fixture.window->openDocuments({ kept }, 0);
    QCOMPARE(fixture.context->getRecent().getFile(kept)->openCount, openCount + 1);

    // Without a database (the storage is not opened) the files are only requested
    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
    context.setPagesEnabled(false);
    QVERIFY(!context.getDatabase());
    QPointer<pdf::PDFAppMainWindow> window = new pdf::PDFAppMainWindow(&context);
    window->openDocuments({ files[1] }, 3);
    QCOMPARE(window->getRequestedFiles(), QStringList({ files[1] }));
    QVERIFY(context.getRecent().getFiles().empty());
    QTRY_COMPARE(window->findChildren<pdf::PDFMessageBox*>().size(), 1);
    delete window.data();
}

void AppNavigationTest::helpTabLimit()
{
    // A window with the largest number of tabs has no place for the Help tab: the
    // manual opens in the Home tab
    NavigationFixture fixture;
    auto help = std::make_shared<NavigationTestFactory>("Help topic");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/help/{topic}"), help);
    while (fixture.window->getTabBar()->count() < pdf::PDFAppMainWindow::MAXIMUM_TABS)
    {
        QVERIFY(fixture.window->createNewTab());
    }
    QVERIFY(!fixture.window->createHelpTab());

    fixture.navigate("/help/search");
    QVERIFY(!fixture.window->findHelpTab());
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    const pdf::PDFAppHistoryEntry* entry = fixture.window->getHomeTab()->getHistory().getCurrentEntry();
    QVERIFY(entry);
    QCOMPARE(entry->route, pdf::PDFAppRoutes::help(QStringLiteral("search")));
    NavigationTestPage* page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
    QVERIFY(page);
    QCOMPARE(page->getTitle(), QStringLiteral("Help topic"));
    QCOMPARE(fixture.window->getTabBar()->count(), pdf::PDFAppMainWindow::MAXIMUM_TABS);
}

void AppNavigationTest::breadcrumbParts()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppBreadcrumb breadcrumb;
    QSignalSpy activated(&breadcrumb, &pdf::PDFAppBreadcrumb::linkActivated);

    // A valid route is a link, an invalid one the current page (not a link)
    breadcrumb.setItems({ { QStringLiteral("Help"), pdf::PDFAppRoutes::help(QStringLiteral("getting-started")) },
                          { QStringLiteral("Category"), pdf::PDFAppRoutes::help(QStringLiteral("search")) },
                          { QStringLiteral("Topic"), pdf::PDFAppRoute() } });
    QList<pdf::PDFAppCrumbLink*> links = breadcrumb.getLinks();
    QCOMPARE(links.size(), 2);
    QCOMPARE(breadcrumb.getRoute(links[1]), pdf::PDFAppRoutes::help(QStringLiteral("search")).toString());

    // A link of another breadcrumb or no link has no route
    pdf::PDFAppCrumbLink foreignLink(QStringLiteral("Foreign"));
    QVERIFY(breadcrumb.getRoute(&foreignLink).isEmpty());
    QVERIFY(breadcrumb.getRoute(nullptr).isEmpty());

    // A click emits the route of the link
    links[0]->click();
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated[0][0].toString(), pdf::PDFAppRoutes::help(QStringLiteral("getting-started")).toString());

    // A link deleted by the application is no more listed
    delete links[0];
    links = breadcrumb.getLinks();
    QCOMPARE(links.size(), 1);
    QCOMPARE(breadcrumb.getRoute(links[0]), pdf::PDFAppRoutes::help(QStringLiteral("search")).toString());

    // Colors of the states: text/accent, text/accent-pressed, text/disabled
    pdf::PDFAppCrumbLink link(QStringLiteral("Protect & Sign"));
    link.resize(link.sizeHint());
    const pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    const QColor accent = themeManager->getColor(pdf::PDFThemeColors::TextAccent);
    const QColor pressed = themeManager->getColor(pdf::PDFThemeColors::TextAccentPressed);
    const QColor disabled = themeManager->getColor(pdf::PDFThemeColors::TextDisabled);

    const QColor normalColor = strongestPixel(link.grab().toImage());
    QVERIFY2(colorDistance(normalColor, accent) < colorDistance(normalColor, pressed), qPrintable(normalColor.name()));
    QVERIFY2(colorDistance(normalColor, accent) <= 24, qPrintable(normalColor.name()));

    link.setDown(true);
    const QColor pressedColor = strongestPixel(link.grab().toImage());
    QVERIFY2(colorDistance(pressedColor, pressed) < colorDistance(pressedColor, accent), qPrintable(pressedColor.name()));
    link.setDown(false);

    link.setEnabled(false);
    const QColor disabledColor = strongestPixel(link.grab().toImage());
    QVERIFY2(colorDistance(disabledColor, disabled) < colorDistance(disabledColor, accent), qPrintable(disabledColor.name()));
    QVERIFY2(colorDistance(disabledColor, disabled) <= 24, qPrintable(disabledColor.name()));
}

void AppNavigationTest::fileDragOver()
{
    // A drag of local files is accepted over the whole window (as a copy), any
    // other data is refused; nothing is read during the drag
    NavigationFixture fixture;
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("drag.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QMimeData files;
    files.setUrls({ QUrl::fromLocalFile(path) });
    QMimeData text;
    text.setText(QStringLiteral("not a file"));
    QMimeData remote;
    remote.setUrls({ QUrl(QStringLiteral("https://example.com/remote.pdf")) });

    const QPoint position(200, 200);
    for (QEvent::Type type : { QEvent::DragEnter, QEvent::DragMove })
    {
        for (QMimeData* data : { &files, &text, &remote })
        {
            const bool isFile = data == &files;
            std::unique_ptr<QDragMoveEvent> event;
            if (type == QEvent::DragEnter)
            {
                event = std::make_unique<QDragEnterEvent>(position, Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
            }
            else
            {
                event = std::make_unique<QDragMoveEvent>(position, Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
            }
            event->setAccepted(!isFile);
            QCoreApplication::sendEvent(fixture.window.data(), event.get());
            QCOMPARE(event->isAccepted(), isFile);
            if (isFile)
            {
                QCOMPARE(event->dropAction(), Qt::CopyAction);
            }
            if (type == QEvent::DragEnter)
            {
                // The drop zones show the drag of files only
                QCOMPARE(fixture.window->isFileDragActive(), isFile);
            }
        }
    }

    // The feedback ends by the leave of the drag (also Esc, which the system
    // reports as the leave) and by the drop
    QDragEnterEvent enter(position, Qt::CopyAction, &files, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window.data(), &enter);
    QVERIFY(fixture.window->isFileDragActive());
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(fixture.window.data(), &leave);
    QVERIFY(!fixture.window->isFileDragActive());

    // The drop of a file, which does not exist: the message lists it, the feedback ends
    QMimeData missing;
    missing.setUrls({ QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.pdf"))) });
    QDragEnterEvent missingEnter(position, Qt::CopyAction, &missing, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window.data(), &missingEnter);
    QVERIFY(fixture.window->isFileDragActive());
    QDropEvent drop(QPointF(position), Qt::CopyAction, &missing, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window.data(), &drop);
    QVERIFY(!fixture.window->isFileDragActive());
    if (QWidget* messageBox = fixture.window->findChild<QWidget*>(QStringLiteral("messageBox")))
    {
        messageBox->close();
    }
}

void AppNavigationTest::overlayPageScrollBar()
{
    // The vertical scroll bar of the page lies over the right padding of the
    // workspace (overlay scroll bar of the style): the page keeps the width of the
    // frames (1166 at 1440 - the columns end at 1408 as app-footer 28:217), the
    // header, the page and the footer have the same edges, the bar takes no width
    NavigationFixture fixture;
    auto factory = std::make_shared<NavigationTestFactory>(QStringLiteral("Tall page"));
    factory->onSetRoute = [](NavigationTestPage* page) { page->setMinimumHeight(3000); };
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), factory));
    fixture.navigate("/about");
    QCoreApplication::processEvents();

    QWidget* window = fixture.window.data();
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    pdf::PDFAppScrollArea* area = homeTab->getPageScrollArea();
    QWidget* page = homeTab->getPage();
    QVERIFY(qobject_cast<NavigationTestPage*>(page));
    QVERIFY(area->isOverlayScrollBars());
    QCOMPARE(area->getScrollBarOverlap(), 12);
    QCOMPARE(homeTab->getPageInset(), 12);
    QVERIFY(!homeTab->getSidebar()->getNavigationArea()->isOverlayScrollBars());
    auto geometry = [window](const QWidget* widget) -> QRect
    {
        return QRect(widget->mapTo(window, QPoint(0, 0)), widget->size());
    };

    QScrollBar* bar = area->verticalScrollBar();
    for (const QSize& size : { QSize(1440, 977), QSize(1100, 701), QSize(720, 480) })
    {
        fixture.window->resize(size);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        const QString context = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());

        QVERIFY2(bar->isVisible() && bar->maximum() > 0, qPrintable(context));
        QVERIFY2(area->viewport()->width() == area->width(), qPrintable(context));
        QCOMPARE(area->horizontalScrollBar()->maximum(), 0);

        const QRect pageRect = geometry(page);
        QVERIFY2(pageRect.left() == pdf::PDFAppSidebar::WIDTH + 32, qPrintable(context));
        QVERIFY2(pageRect.right() + 1 == size.width() - 32, qPrintable(context));
        for (const QWidget* part : { static_cast<const QWidget*>(homeTab->getHeader()), static_cast<const QWidget*>(homeTab->getFooter()) })
        {
            QVERIFY2(geometry(part).left() == pageRect.left() && geometry(part).right() == pageRect.right(), qPrintable(context));
        }

        // The bar lies in the right padding, beside the page
        const QRect barRect = geometry(bar);
        QVERIFY2(barRect.left() == size.width() - 32 && barRect.width() == 12, qPrintable(QStringLiteral("%1: bar %2").arg(context).arg(barRect.left())));
        QVERIFY(!barRect.intersects(pageRect));
    }

    fixture.window->resize(1440, 977);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(page->width(), 1166);
    QCOMPARE(geometry(page).right() + 1, 1408);

    // Pixels in both themes at 1 / 1.5 / 2: the slider (border/strong) in the padding,
    // the canvas in the gutters of the bar
    bar->setValue(0);
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QCoreApplication::processEvents();
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            const QImage image = renderWindow(window, scale);
            const QRect barRect = geometry(bar);
            auto pixelAt = [&image, scale](int x, int y) { return image.pixelColor(qFloor((x + 0.5) * scale), qFloor((y + 0.5) * scale)); };
            const QColor slider = pixelAt(barRect.left() + 6, barRect.top() + 6);
            const QColor gutter = pixelAt(barRect.left() + 1, barRect.top() + 6);
            const QColor outerGutter = pixelAt(barRect.right() - 1, barRect.top() + 6);
            QVERIFY2(colorDistance(slider, pdf::PDFStylePainter::color(pdf::PDFThemeColors::BorderStrong)) <= 9, qPrintable(slider.name()));
            QVERIFY2(colorDistance(gutter, pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas)) <= 6, qPrintable(gutter.name()));
            QVERIFY2(colorDistance(outerGutter, pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas)) <= 6, qPrintable(outerGutter.name()));
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // The wheel over the page, the keyboard of the area and the drag of the slider scroll
    QWheelEvent wheel(QPointF(100, 100), area->viewport()->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(area->viewport(), &wheel);
    QVERIFY(bar->value() > 0);
    bar->setValue(0);
    QTest::keyClick(area, Qt::Key_PageDown);
    QVERIFY(bar->value() > 0);
    bar->setValue(0);
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QTest::mouseMove(bar, QPoint(6, 120));
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, QPoint(6, 120));
    QVERIFY(bar->value() > 0);

    // Without the overlay (another style or the property off) the area keeps the
    // paddings outside and the bar takes its width inside of the area
    area->setOverlayScrollBars(false);
    QCOMPARE(area->getScrollBarOverlap(), 0);
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(homeTab, &styleChange);
    QCoreApplication::processEvents();
    QCOMPARE(homeTab->getPageInset(), 0);
    QCOMPARE(geometry(area).left(), pdf::PDFAppSidebar::WIDTH + 32);
    QCOMPARE(area->viewport()->width(), area->width() - 12);
    area->setOverlayScrollBars(true);
    QCoreApplication::sendEvent(homeTab, &styleChange);
    QCoreApplication::processEvents();
    QCOMPARE(homeTab->getPageInset(), 12);
    QCOMPARE(page->width(), 1166);

    // The Help tab (pdf4qt-help 955:152873): the page of 1166 px is centered in the
    // whole window (137 px at both sides of 1440), the header follows it, the bar
    // lies in the right padding of the window
    auto help = std::make_shared<NavigationTestFactory>(QStringLiteral("Help topic"));
    help->onSetRoute = [](NavigationTestPage* helpPage) { helpPage->setMinimumHeight(3000); };
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/help/{topic}"), help));
    fixture.navigate("/help/getting-started");
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    pdf::PDFAppWorkspaceTab* helpTab = qobject_cast<pdf::PDFAppWorkspaceTab*>(fixture.window->findHelpTab());
    QVERIFY(helpTab);
    QCOMPARE(fixture.currentTab(), helpTab);
    for (const int width : { 1440, 1300, 1000, 720 })
    {
        fixture.window->resize(width, 977);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QWidget* helpPage = helpTab->getPage();
        const QRect pageRect = geometry(helpPage);
        const int expectedWidth = qMin(1166, width - 64);
        QVERIFY2(pageRect.width() == expectedWidth, qPrintable(QStringLiteral("%1: %2").arg(width).arg(pageRect.width())));
        QVERIFY2(qAbs(pageRect.left() - (width - pageRect.width()) / 2) <= 1, qPrintable(QStringLiteral("%1: %2").arg(width).arg(pageRect.left())));
        pdf::PDFAppWorkspaceHeader* header = helpTab->getHeader();
        const QMargins margins = header->contentsMargins();
        QCOMPARE(header->mapTo(window, QPoint(margins.left(), 0)).x(), pageRect.left());
        QCOMPARE(header->width() - margins.left() - margins.right(), pageRect.width());
        QCOMPARE(geometry(helpTab->getPageScrollArea()->verticalScrollBar()).left(), width - 32);
    }
}

void AppNavigationTest::headerSearchWidth()
{
    // The search field of the header (320 px in the frames) gives its width to the
    // texts in a narrow window, down to 200 px, before the title is elided
    QCOMPARE(pdf::PDFAppWorkspaceHeader::getSearchWidth(1166, 300), 320);
    QCOMPARE(pdf::PDFAppWorkspaceHeader::getSearchWidth(500, 200), 276);
    QCOMPARE(pdf::PDFAppWorkspaceHeader::getSearchWidth(446, 400), 200);
    QCOMPARE(pdf::PDFAppWorkspaceHeader::getSearchWidth(0, 0), 200);
    QCOMPARE(pdf::PDFAppWorkspaceHeader::getSearchWidth(100000, -50), 320);

    NavigationFixture fixture;
    auto factory = std::make_shared<NavigationTestFactory>(QStringLiteral("Welcome to PDF4QT"));
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/home"), factory));
    fixture.navigate("/home");
    QCoreApplication::processEvents();
    pdf::PDFAppWorkspaceHeader* header = fixture.window->getHomeTab()->getHeader();
    pdf::PDFAppHeaderText* title = header->getTitleLabel();
    QCOMPARE(title->text(), QStringLiteral("Welcome to PDF4QT"));
    const int titleWidth = qCeil(title->getTextWidth());

    for (const int width : { 1440, 1100, 900, 800, 720 })
    {
        fixture.window->resize(width, 600);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        const QString context = QString::number(width);

        const int headerWidth = header->width();
        const int searchWidth = header->getSearchField()->width();
        QVERIFY2(searchWidth == pdf::PDFAppWorkspaceHeader::getSearchWidth(headerWidth, titleWidth), qPrintable(QStringLiteral("%1: %2").arg(context).arg(searchWidth)));
        QVERIFY2(searchWidth >= 200 && searchWidth <= 320, qPrintable(context));

        // The field ends at the right edge of the header, the title keeps the rest
        const QRect fieldRect(header->getSearchField()->mapTo(header, QPoint(0, 0)), header->getSearchField()->size());
        QCOMPARE(fieldRect.right() + 1, headerWidth);
        QVERIFY(title->width() >= headerWidth - 24 - searchWidth - 1);

        // The title is whole whenever the field can give it the room
        if (headerWidth - 24 - 200 >= titleWidth)
        {
            QVERIFY2(!title->isElided(), qPrintable(context));
        }
    }

    // 1440: the field of the frames; 720 (the header is 446 px): the field is
    // narrower than 320, so the title is less elided than with the fixed field
    fixture.window->resize(1440, 600);
    QCoreApplication::processEvents();
    QCOMPARE(header->getSearchField()->width(), 320);
    fixture.window->resize(720, 480);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QVERIFY(header->getSearchField()->width() < 320);
    QVERIFY(title->width() > header->width() - 24 - 320);

    // A shorter title gives the field its width back
    factory->onSetRoute = nullptr;
    auto shortFactory = std::make_shared<NavigationTestFactory>(QStringLiteral("Open"));
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/open"), shortFactory));
    fixture.navigate("/open");
    QCoreApplication::processEvents();
    QCOMPARE(header->getSearchField()->width(), pdf::PDFAppWorkspaceHeader::getSearchWidth(header->width(), qCeil(title->getTextWidth())));
    QCOMPARE(header->getSearchField()->width(), 320);
}

void AppNavigationTest::paragraphOverhang()
{
    // The size hint of a paragraph is its text on one line: QTextLayout breaks a
    // line, whose last glyph overhangs its advance (the right bearing of "x", of an
    // italic letter), at the width of the advance - the hint takes the width the
    // line needs (the title "Clip to crop box" of Settings wrapped before)
    QFont italic = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body);
    italic.setItalic(true);
    const std::vector<std::pair<QFont, QString>> cases = {
        { pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold), QStringLiteral("Clip to crop box") },
        { pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold), QStringLiteral("Flatten tax") },
        { pdf::PDFThemeFonts::getFont(QFont::Bold, 22), QStringLiteral("Box") },
        { italic, QStringLiteral("Proof of a self") },
        { italic, QStringLiteral("Half") },
        { pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Small), QStringLiteral("Wrapped text of a hint") }
    };

    int overhanging = 0;
    for (const auto& [font, text] : cases)
    {
        for (const int margin : { 0, 3 })
        {
            pdf::PDFAppParagraph paragraph(font, 16, pdf::PDFThemeColors::TextPrimary);
            paragraph.setContentsMargins(margin, 0, margin, 0);
            paragraph.setText(text);
            const QSize hint = paragraph.sizeHint();
            QVERIFY2(paragraph.getLines(hint.width()).size() == 1, qPrintable(text));
            QCOMPARE(hint.height(), 16);
            QCOMPARE(paragraph.heightForWidth(hint.width()), 16);

            // At most MAXIMUM_OVERHANG wider than the advance
            const int advance = qCeil(QFontMetricsF(font).horizontalAdvance(text)) + 2 * margin;
            QVERIFY2(hint.width() >= advance && hint.width() <= advance + pdf::PDFAppParagraph::MAXIMUM_OVERHANG, qPrintable(text));
            overhanging += hint.width() > advance ? 1 : 0;
        }
    }
    qDebug("%d of %d texts overhang their advance", overhanging, int(cases.size()) * 2);

    // A text of more lines and a paragraph of a fixed width keep their hints
    pdf::PDFAppParagraph lines(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body), 16, pdf::PDFThemeColors::TextPrimary);
    lines.setText(QStringLiteral("First line\nSecond line"));
    QCOMPARE(lines.sizeHint().width(), qCeil(QFontMetricsF(lines.font()).horizontalAdvance(lines.text())));
    pdf::PDFAppParagraph fixed(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body), 16, pdf::PDFThemeColors::TextPrimary);
    fixed.setText(QStringLiteral("Clip to crop box"));
    fixed.setFixedWidth(40);
    QCOMPARE(fixed.sizeHint().width(), 40);
    QVERIFY(fixed.sizeHint().height() > 16);
    pdf::PDFAppParagraph empty(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body), 16, pdf::PDFThemeColors::TextPrimary);
    QCOMPARE(empty.sizeHint(), QSize(0, 16));
}

void AppNavigationTest::messageScrim()
{
    // The messages of the window cover the body of the window (the content of the
    // tabs) by their scrim, not the top bar with the tabs and the buttons of the
    // window (pdf4qt-recent-clear-history 941:135742: the scrim starts at y = 38);
    // the card is centered over the body
    NavigationFixture fixture;
    fixture.navigate("/tools/convert.epub");

    // The window on the screen: the card is not moved by the edges of the screen
    fixture.window->setGeometry(fixture.window->screen()->availableGeometry());
    QCoreApplication::processEvents();
    QWidget* body = fixture.window->getDialogHost();
    QVERIFY(body);
    QCOMPARE(body->objectName(), QStringLiteral("tabContent"));
    QCOMPARE(pdf::PDFAppMainWindow::findDialogHost(fixture.window->getHomeTab()->getHeader()), body);
    QVERIFY(!pdf::PDFAppMainWindow::findDialogHost(nullptr));
    QWidget alone;
    QVERIFY(!pdf::PDFAppMainWindow::findDialogHost(&alone));
    pdf::PDFAppMainWindow::prepareDialog(nullptr, body);
    QCOMPARE(body->mapTo(fixture.window.data(), QPoint(0, 0)).y(), fixture.window->getTopBar()->height());

    // A drop of a file, which does not exist, shows the message of the refused items
    QTemporaryDir directory;
    QMimeData missing;
    missing.setUrls({ QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.pdf"))) });
    auto dropMissing = [&fixture, &missing]()
    {
        QDragEnterEvent enter(QPoint(200, 200), Qt::CopyAction, &missing, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(fixture.window.data(), &enter);
        QDropEvent drop(QPointF(200, 200), Qt::CopyAction, &missing, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(fixture.window.data(), &drop);
    };
    dropMissing();
    pdf::PDFMessageBox* box = fixture.window->findChild<pdf::PDFMessageBox*>(QStringLiteral("messageBox"));
    QVERIFY(box);
    QTRY_VERIFY(box->isVisible());
    QCOMPARE(box->getScrimTarget(), body);
    QVERIFY(box->getScrim());
    QCOMPARE(box->getScrim()->parentWidget(), body);
    QCOMPARE(box->getScrim()->geometry(), body->rect());

    const QRect cardRect(box->mapToGlobal(QPoint(0, 0)) + QPoint(box->getShadowMargins().left(), box->getShadowMargins().top()), box->rect().marginsRemoved(box->getShadowMargins()).size());
    const QPoint cardCenter = cardRect.center();

    // Centered over the body, the card stays on the screen (the offscreen screen is
    // smaller than the window)
    QRect expectedCard(QPoint(0, 0), cardRect.size());
    expectedCard.moveCenter(body->mapToGlobal(body->rect().center()));
    const QRect available = fixture.window.data()->screen()->availableGeometry();
    expectedCard.moveLeft(qBound(available.left(), expectedCard.left(), qMax(available.left(), available.right() - expectedCard.width() + 1)));
    expectedCard.moveTop(qBound(available.top(), expectedCard.top(), qMax(available.top(), available.bottom() - expectedCard.height() + 1)));
    const QPoint bodyCenter = expectedCard.center();
    QVERIFY2((cardCenter - bodyCenter).manhattanLength() <= 2, qPrintable(QStringLiteral("%1 %2 / %3 %4").arg(cardCenter.x()).arg(cardCenter.y()).arg(bodyCenter.x()).arg(bodyCenter.y())));

    // Pixels: the top bar is not dimmed, the body is
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QCoreApplication::processEvents();
        const QImage image = fixture.window->grab().toImage();
        const QImage topBar = fixture.window->getTopBar()->grab().toImage();
        const QPoint probe(fixture.window->getTopBar()->width() / 2, 2);
        QCOMPARE(image.pixelColor(probe), topBar.pixelColor(probe));
        const QColor canvas = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas);
        const QColor dimmed = image.pixelColor(body->mapTo(fixture.window.data(), QPoint(body->width() - 4, body->height() / 2)));
        QVERIFY2(colorDistance(dimmed, canvas) > 8, qPrintable(dimmed.name()));
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Closed: the scrim is gone; the window deleted while a message is shown
    box->close();
    QTRY_VERIFY(body->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size() == body->findChildren<pdf::PDFAppTab*>(Qt::FindDirectChildrenOnly).size());
    dropMissing();
    QTRY_VERIFY(fixture.window->findChild<pdf::PDFMessageBox*>(QStringLiteral("messageBox")));
    delete fixture.window.data();
    QVERIFY(!fixture.window);
}

void AppNavigationTest::bannerLayoutOfPage()
{
    // The storage banner of the Home tab is placed at the top of the layout, which
    // the page names; a layout outside of the page is refused (the banner stays
    // above the page), the banner returns, when the page is destroyed
    NavigationFixture fixture;
    auto recent = std::make_shared<NavigationTestFactory>("Recent");
    auto about = std::make_shared<NavigationTestFactory>("About");
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/recent"), recent);
    fixture.navigator()->getRouteTable().setFactory(QStringLiteral("/about"), about);
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    QPointer<QWidget> banner = homeTab->getBanner();
    QVERIFY(banner);
    QWidget* pageArea = banner->parentWidget();
    QVERIFY(pageArea);

    QWidget foreignWidget;
    QVBoxLayout* foreignLayout = new QVBoxLayout(&foreignWidget);
    QPointer<QWidget> column;
    recent->onSetRoute = [&column](NavigationTestPage* page)
    {
        column = new QWidget(page);
        page->bannerLayout = new QVBoxLayout(column);
    };
    about->onSetRoute = [foreignLayout](NavigationTestPage* page) { page->bannerLayout = foreignLayout; };

    fixture.navigate("/recent");
    QVERIFY(column);
    QCOMPARE(banner->parentWidget(), column.data());

    // The same page shown again (its title changed): the banner stays
    fixture.window->updateTab(homeTab);
    QCOMPARE(banner->parentWidget(), column.data());

    // A layout of another widget: the banner is above the page
    fixture.navigate("/about");
    QTRY_VERIFY(column.isNull());
    QVERIFY(banner);
    QCOMPARE(banner->parentWidget(), pageArea);
    QCOMPARE(foreignLayout->count(), 0);

    // A page without its own layout
    about->onSetRoute = nullptr;
    recent->onSetRoute = nullptr;
    fixture.navigate("/recent");
    QVERIFY(banner);
    QCOMPARE(banner->parentWidget(), pageArea);
}

void AppNavigationTest::screenshots()
{
    // PDF4QT_APP_NAVIGATION_SCREENSHOTS=<folder>: pictures of the shell in both themes
    // for the comparison with Figma (pdf4qt-not-found 940:134845, pdf4qt-new-tab 955:152339)
    const QString folder = qEnvironmentVariable("PDF4QT_APP_NAVIGATION_SCREENSHOTS");
    NavigationFixture fixture;
    fixture.window->resize(1440, 977);
    fixture.navigate("/tools/convert.epub");
    fixture.window->getHomeTab()->getBanner()->hide();
    QCoreApplication::processEvents();

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QCoreApplication::processEvents();
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");

        fixture.window->activateTab(fixture.window->getHomeTab());
        QCoreApplication::processEvents();
        const QImage notFound = fixture.window->grab().toImage();
        QVERIFY(!notFound.isNull());

        if (!folder.isEmpty() && scheme == pdf::PDFThemeColors::Scheme::Light)
        {
            // Geometry of the visible widgets in the coordinates of the window
            QDir().mkpath(folder);
            QFile file(QDir(folder).filePath(QStringLiteral("not-found-geometry.txt")));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream stream(&file);
                for (QWidget* child : fixture.window->findChildren<QWidget*>())
                {
                    if (child->isVisibleTo(fixture.window))
                    {
                        const QRect rect(child->mapTo(fixture.window, QPoint(0, 0)), child->size());
                        stream << child->metaObject()->className() << " " << child->objectName() << " " << rect.x() << "," << rect.y() << " " << rect.width() << "x" << rect.height() << "\n";
                    }
                }
            }
        }

        fixture.window->getHomeTab()->getBanner()->show();
        QCoreApplication::processEvents();
        const QImage portable = fixture.window->grab().toImage();
        fixture.window->getHomeTab()->getBanner()->hide();

        fixture.window->openNewTab();
        QCoreApplication::processEvents();
        const QImage newTab = fixture.window->grab().toImage();

        if (!folder.isEmpty())
        {
            QDir().mkpath(folder);
            notFound.save(QDir(folder).filePath(QStringLiteral("not-found-%1.png").arg(suffix)));
            portable.save(QDir(folder).filePath(QStringLiteral("portable-%1.png").arg(suffix)));
            newTab.save(QDir(folder).filePath(QStringLiteral("new-tab-%1.png").arg(suffix)));
        }
        fixture.window->closeTab(fixture.window->getCurrentTab());
    }

    // Right-to-left and the minimal size of the window (with the banner)
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.window->getHomeTab()->getBanner()->show();
    fixture.window->setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    const QImage rightToLeft = fixture.window->grab().toImage();
    fixture.window->setLayoutDirection(Qt::LeftToRight);
    fixture.window->resize(fixture.window->minimumSize());
    QCoreApplication::processEvents();
    const QImage minimum = fixture.window->grab().toImage();
    QVERIFY(!rightToLeft.isNull() && !minimum.isNull());
    if (!folder.isEmpty())
    {
        rightToLeft.save(QDir(folder).filePath(QStringLiteral("rtl-light.png")));
        minimum.save(QDir(folder).filePath(QStringLiteral("minimum-light.png")));
    }

    // The banners of the storage with buttons (pdf4qt-settings-storage-banners 958:28036)
    // in the wide and in the minimal window, in both themes and right-to-left
    struct BannerShot
    {
        const char* name;
        pdf::PDFDatabaseStorageMode mode;
        const char* damaged;
    };
    const BannerShot bannerShots[] = { { "fallback", pdf::PDFDatabaseStorageMode::MemoryFallback, "" },
                                       { "read-only", pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema, "" },
                                       { "replaced", pdf::PDFDatabaseStorageMode::Disk, "C:/data/pdf4qt.db.damaged-2026-10-18" } };
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    for (const BannerShot& shot : bannerShots)
    {
        pdf::PDFDatabaseState state;
        state.mode = shot.mode;
        state.filePath = QStringLiteral("C:/data/pdf4qt.db");
        state.damagedFilePath = QString::fromLatin1(shot.damaged);
        state.error = pdf::PDFDatabaseError::fromMessage(QStringLiteral("disk I/O error"));
        homeTab->getBanner()->setState(state);
        homeTab->getFooter()->setState(state);

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
            for (const QSize& size : { QSize(1440, 977), QSize(720, 480) })
            {
                for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
                {
                    fixture.window->resize(size);
                    fixture.window->setLayoutDirection(direction);
                    QCoreApplication::processEvents();
                    QCoreApplication::processEvents();
                    const QImage image = fixture.window->grab().toImage();
                    QVERIFY(!image.isNull());
                    if (!folder.isEmpty())
                    {
                        const QString name = QStringLiteral("%1-%2%3%4.png").arg(QString::fromLatin1(shot.name),
                                                                                 size.width() == 720 ? QStringLiteral("minimum-") : QString(),
                                                                                 direction == Qt::RightToLeft ? QStringLiteral("rtl-") : QString(),
                                                                                 suffix);
                        image.save(QDir(folder).filePath(name));
                    }
                }
            }
        }
    }
    fixture.window->setLayoutDirection(Qt::LeftToRight);
}

QTEST_MAIN(AppNavigationTest)

#include "tst_appnavigationtest.moc"
