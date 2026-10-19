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
#include "pdfapptopbar.h"
#include "pdfappwindowframe.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

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

    void requestBack()
    {
        Q_EMIT backRequested();
    }

    int value = 0;
    int setRouteCount = 0;
    bool isRestored = false;
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

/// Context (portable, own memory database) and the window of a test
class NavigationFixture
{
public:
    explicit NavigationFixture(bool createWindow = true, const QStringList& arguments = QStringList())
    {
        QStringList commandLineArguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
        commandLineArguments << arguments;
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(commandLineArguments, QDir::currentPath()));

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
    void historyAndViewState();
    void navigationFromOwnSignal();
    void deletionDuringNavigation();
    void deletionInAnyOrder();
    void newTab();
    void externalLinks();
    void themeSwitch();
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
    void windowControls();
    void closeAndCancel();
    void instanceRequests();
    void screenshots();

private:
    static QString documentId() { return QStringLiteral("abcdefghijklmnopqrstuvwxyz"); }
};

void AppNavigationTest::initTestCase()
{
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
    QCOMPARE(Routes::tools().toString(), QStringLiteral("/tools"));
    QCOMPARE(Routes::tools("protect").toString(), QStringLiteral("/tools?category=protect"));
    QCOMPARE(Routes::tool("compress").toString(), QStringLiteral("/tools/compress"));
    QCOMPARE(Routes::toolCollections().toString(), QStringLiteral("/tools/collections"));
    QCOMPARE(Routes::toolCollection("mine").toString(), QStringLiteral("/tools/collections/mine"));
    QCOMPARE(Routes::ai().toString(), QStringLiteral("/ai"));
    QCOMPARE(Routes::whatsNew().toString(), QStringLiteral("/whats-new"));
    QCOMPARE(Routes::whatsNew("1.6.0").toString(), QStringLiteral("/whats-new/1.6.0"));
    QCOMPARE(Routes::about("third-party").toString(), QStringLiteral("/about#third-party"));
    QCOMPARE(Routes::support().toString(), QStringLiteral("/support"));
    QCOMPARE(Routes::sponsor().toString(), QStringLiteral("/sponsor"));
    QCOMPARE(Routes::help("getting-started").toString(), QStringLiteral("/help/getting-started"));
    QCOMPARE(Routes::settings().toString(), QStringLiteral("/settings/general"));
    QCOMPARE(Routes::settings("security", "signature-verification").toString(), QStringLiteral("/settings/security#signature-verification"));
    QCOMPARE(Routes::search("merge pdf").toString(), QStringLiteral("/search?q=merge%20pdf"));
    QCOMPARE(Routes::search("x", "tools").toString(), QStringLiteral("/search?q=x&group=tools"));
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
                                            Routes::settings("advanced", "row"), Routes::search("x", "help"), Routes::document(documentId(), 3),
                                            Routes::documentProperties(documentId(), "xmp"), Routes::documentTool(documentId(), "ocr"), Routes::toolTab("merge", "m1") })
    {
        const pdf::PDFAppRouteMatch match = table.match(route);
        QVERIFY2(match.result == pdf::PDFAppRouteMatch::Result::NotImplemented, qPrintable(route.toString()));
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
    add("/tools/collections", "/tools/collections", "/tools/collections", Tab::Home, Nav::Tools);
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
    add("/help/getting-started", "/help/{topic}", "/help/getting-started", Tab::Home, Nav::None);
    add("/settings", "/settings/{section}", "/settings/general", Tab::Home, Nav::Settings);
    add("/settings?q=color", "/settings", "/settings?q=color", Tab::Home, Nav::Settings);
    for (const char* section : { "general", "viewing", "color-modes", "editing", "security", "speech", "shortcuts", "color-management", "advanced" })
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
    fixture.navigate(route);
    QCOMPARE(navigated.count(), 1);
    QVERIFY(navigated.last().at(2).toBool());
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
    QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::None);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), resolved);
    QCOMPARE(fixture.navigator()->getLastAnnouncement(), QStringLiteral("Page not found page"));
    QCOMPARE(fixture.window->windowTitle(), QString::fromUtf8("Page not found – PDF4QT"));

    // A later step registers the factory: the route opens its page with its highlight
    auto factory = std::make_shared<NavigationTestFactory>(pattern);
    QVERIFY(fixture.navigator()->getRouteTable().setFactory(pattern, factory));
    fixture.navigate(route);

    if (tabKind == int(pdf::PDFAppTabKind::Home))
    {
        NavigationTestPage* page = qobject_cast<NavigationTestPage*>(fixture.currentPage());
        QVERIFY(page);
        QCOMPARE(page->getRoute().toString(), resolved);
        QCOMPARE(int(fixture.currentTab()->getNavItem()), navItem);
        QCOMPARE(fixture.window->windowTitle(), QString::fromUtf8("%1 – PDF4QT").arg(pattern));
        QCOMPARE(fixture.navigator()->getLastAnnouncement(), QStringLiteral("%1 page").arg(pattern));

        // The header shows the texts of the page, the search stays
        pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
        QCOMPARE(homeTab->getHeader()->getTitleLabel()->text(), pattern);
        QVERIFY(homeTab->getSearchField());
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

    // MIT License of the footer
    pdf::PDFAppFooter* footer = fixture.window->getHomeTab()->getFooter();
    QTest::mouseClick(footer->getLicenseLink(), Qt::LeftButton);
    QCOMPARE(opened.count(), 2);
    QCOMPARE(opened.last().at(0).toUrl(), pdf::PDFAppFooter::getLicenseUrl());
    QVERIFY(footer->getVersionLabel()->text().contains(QStringLiteral(PDF4QT_PROJECT_VERSION)));
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
    QCOMPARE(search->placeholderText(), QStringLiteral("Search tools, documents, or help..."));
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
        QVERIFY(server->isSuspended());
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
        std::thread sender([&sendResult, serverName, request]()
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

void AppNavigationTest::instanceRequests()
{
    QTemporaryDir directory;
    const QString file = directory.filePath(QStringLiteral("document.pdf"));
    QFile output(file);
    QVERIFY(output.open(QIODevice::WriteOnly));
    output.write("%PDF-1.7\n");
    output.close();

    // Files and errors of the own command line are shown, when the window exists
    NavigationFixture fixture(true, { file, QStringLiteral("--bogus") });
    QTRY_COMPARE(fixture.window->getRequestedFiles(), QStringList({ QDir::cleanPath(file) }));
    QTRY_COMPARE(fixture.window->findChildren<pdf::PDFMessageBox*>().size(), 2);
    for (pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>())
    {
        box->reject();
    }
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
    QCOMPARE(fixture.window->findChildren<pdf::PDFMessageBox*>().size(), 2);

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
}

QTEST_MAIN(AppNavigationTest)

#include "tst_appnavigationtest.moc"
