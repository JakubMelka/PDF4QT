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

// Pages About, Support and Sponsor of the new application (step 2 of phase 1):
// routes and their validation, the build information and the third-party
// libraries, the anchor #third-party, the catalog of the video tutorials, the
// filters of Support in the route, For you from tool_usage written by another
// connection, links (https only), geometry and pixels against the Figma frames
// in both themes, right-to-left, the minimal window, keyboard, accessible names,
// deletion of the pages at any moment. PDF4QT_APP_SCREENSHOTS=<folder> writes
// pictures of the pages.

#include "pdfappaboutinfo.h"
#include "pdfappaboutpage.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappinfowidgets.h"
#include "pdfapplog.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapppaths.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfappsponsorpage.h"
#include "pdfappsupportpage.h"
#include "pdfapptab.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfsupportvideocatalog.h"
#include "pdftoolrepository.h"
#include "pdfwhatsnew.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDir>
#include <QHeaderView>
#include <QMenu>
#include <QStyleOptionHeader>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTranslator>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

#include <memory>

class InfoFixture
{
public:
    explicit InfoFixture(const QStringList& arguments = QStringList({ QStringLiteral("--portable") }), const QString& databaseFile = QString())
    {
        QStringList commandLineArguments = { QStringLiteral("PDF4QT") };
        commandLineArguments << arguments;
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(commandLineArguments, QDir::currentPath()));

        pdf::PDFDatabaseOptions options;
        memoryName = QStringLiteral("/pdf4qt-infopages-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        options.memoryName = memoryName;
        options.filePath = databaseFile;
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        window = new pdf::PDFAppMainWindow(context.get());
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(1440, 1067);
        window->show();
        if (!QTest::qWaitForWindowExposed(window.data()))
        {
            qWarning("The window was not exposed");
        }
        QCoreApplication::processEvents();
    }

    ~InfoFixture()
    {
        delete window.data();
        context.reset();
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        QApplication::setLayoutDirection(Qt::LeftToRight);
    }

    pdf::PDFAppNavigator* navigator() const { return window ? window->getNavigator() : nullptr; }
    pdf::PDFAppTab* currentTab() const { return window ? window->getCurrentTab() : nullptr; }
    pdf::PDFAppPage* currentPage() const { return currentTab() ? currentTab()->getPage() : nullptr; }
    pdf::PDFAppWorkspaceTab* homeTab() const { return window ? window->getHomeTab() : nullptr; }

    /// Hides the banner of the portable mode (the frames of Figma have none)
    void hideBanner()
    {
        homeTab()->getBanner()->hide();
        settle();
    }

    void navigate(const QString& text)
    {
        navigator()->navigate(pdf::PDFAppRoute::parse(text), pdf::PDFAppNavigationMode::Push);
        settle();
    }

    template<typename Page>
    Page* page() const
    {
        return qobject_cast<Page*>(currentPage());
    }

    /// Lets the deleted pages go and the layouts settle
    static void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
    }

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QString memoryName;

    static int s_counter;
};

int InfoFixture::s_counter = 0;

/// Helpers of the test
class InfoTestHelper
{
public:
    InfoTestHelper() = delete;

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFStylePainter::color(color);
    }

    static QColor accent(pdf::PDFAccentHue hue, pdf::PDFAccentHueColors::Role role)
    {
        return token(pdf::PDFAccentHueColors::getColor(hue, role));
    }

    /// Picture of the widget at the device pixel ratio
    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Color of the pixel at the logical position of the picture
    static QColor pixel(const QImage& image, const QPoint& point)
    {
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(qFloor(point.x() * ratio), qFloor(point.y() * ratio));
    }

    static bool isClose(const QColor& first, const QColor& second, int tolerance = 3)
    {
        return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance && qAbs(first.blue() - second.blue()) <= tolerance;
    }

    /// Number of the pixels of the leak color (a color of the palette)
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    /// Rectangle of the widget in the coordinates of the ancestor
    static QRect rectIn(const QWidget* widget, const QWidget* ancestor)
    {
        return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
    }

    /// Texts of the labels, buttons, fact pills and fields of the widget and its descendants
    static QStringList texts(const QWidget* widget)
    {
        QStringList result;
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            result << label->text();
        }
        for (const QAbstractButton* button : widget->findChildren<QAbstractButton*>())
        {
            result << button->text();
        }
        for (const pdf::PDFFactPill* pill : widget->findChildren<pdf::PDFFactPill*>())
        {
            result << pill->getText();
        }
        return result;
    }

    static QString accessibleName(QWidget* widget)
    {
        QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(widget);
        return interface ? interface->text(QAccessible::Name) : QString();
    }

    /// Writes the tool usage from another thread (its own connection of the database)
    static bool recordUsageInThread(pdf::PDFDatabase* database, const QString& toolId)
    {
        bool isOk = false;
        QThread* thread = QThread::create([database, toolId, &isOk]()
        {
            pdf::PDFToolRepository repository(database);
            isOk = !repository.recordUsage(toolId).isError();
        });
        thread->start();
        thread->wait();
        delete thread;
        return isOk;
    }

    /// Inserts a row of the tool usage from another thread by a statement without a
    /// transaction: the notifier learns about it only by its polling (not woken up
    /// by the commit of a transaction of this process)
    static bool insertUsageRowInThread(pdf::PDFDatabase* database, const QString& toolId)
    {
        bool isOk = false;
        QThread* thread = QThread::create([database, toolId, &isOk]()
        {
            pdf::PDFDatabaseStatement statement(database->getConnection(), "INSERT INTO tool_usage(tool_id, last_used, count) VALUES(?, 1, 1)");
            statement.bindText(1, toolId);
            isOk = statement.execute();
        });
        thread->start();
        thread->wait();
        delete thread;
        return isOk;
    }

    /// Number of the notifications of the tool usage recorded by a spy of PDFDatabaseNotifier::changed
    static int countUsageNotifications(const QSignalSpy& spy)
    {
        int count = 0;
        for (const QList<QVariant>& arguments : spy)
        {
            count += arguments.value(0).toString() == pdf::PDFToolRepository::getUsageTopic() ? 1 : 0;
        }
        return count;
    }

    /// Text of the UTF-16 code units (also lone surrogates and control characters)
    static QString fromCodes(std::initializer_list<char16_t> codes)
    {
        QString text;
        for (const char16_t code : codes)
        {
            text += QChar(code);
        }
        return text;
    }

    /// Record of the name table of a font made by the test
    struct NameRecord
    {
        quint16 platform = 3;
        quint16 nameId = 5;
        QByteArray text;

        /// Length and offset written into the record (-1 = the real ones)
        int length = -1;
        int offset = -1;
    };

    /// Table directory and name table of a TrueType font (big endian)
    struct FontSpec
    {
        QList<NameRecord> records;

        /// A table 'head' before the table 'name' (4 bytes of data)
        bool hasHeadTable = false;
        bool hasNameTable = true;

        /// Number of the tables / of the name records written (-1 = the real ones)
        int tableCount = -1;
        int nameCount = -1;

        /// Offset of the name table written into its record (0 = the real one)
        quint32 nameTableOffset = 0;
    };

    static void appendUInt16(QByteArray& data, quint16 value)
    {
        data.append(char(value >> 8));
        data.append(char(value & 0xFF));
    }

    static void appendUInt32(QByteArray& data, quint32 value)
    {
        appendUInt16(data, quint16(value >> 16));
        appendUInt16(data, quint16(value & 0xFFFF));
    }

    /// UTF-16BE of the text (platforms 0 and 3 of the name table)
    static QByteArray toUtf16BigEndian(const QString& text)
    {
        QByteArray data;
        for (const QChar character : text)
        {
            appendUInt16(data, character.unicode());
        }
        return data;
    }

    static QByteArray createFont(const FontSpec& spec)
    {
        const int realTableCount = (spec.hasHeadTable ? 1 : 0) + (spec.hasNameTable ? 1 : 0);
        const quint32 headOffset = quint32(12 + 16 * realTableCount);
        const quint32 nameOffset = headOffset + (spec.hasHeadTable ? 4 : 0);

        QByteArray data;
        appendUInt32(data, 0x00010000);
        appendUInt16(data, quint16(spec.tableCount >= 0 ? spec.tableCount : realTableCount));
        appendUInt16(data, 0);
        appendUInt16(data, 0);
        appendUInt16(data, 0);
        if (spec.hasHeadTable)
        {
            appendUInt32(data, 0x68656164); // 'head'
            appendUInt32(data, 0);
            appendUInt32(data, headOffset);
            appendUInt32(data, 4);
        }
        if (spec.hasNameTable)
        {
            appendUInt32(data, 0x6E616D65); // 'name'
            appendUInt32(data, 0);
            appendUInt32(data, spec.nameTableOffset ? spec.nameTableOffset : nameOffset);
            appendUInt32(data, 0);
        }
        if (spec.hasHeadTable)
        {
            appendUInt32(data, 0xDEADBEEF);
        }
        if (!spec.hasNameTable)
        {
            return data;
        }

        // Name table: format, count, offset of the strings, the records, the strings
        const int recordCount = int(spec.records.size());
        appendUInt16(data, 0);
        appendUInt16(data, quint16(spec.nameCount >= 0 ? spec.nameCount : recordCount));
        appendUInt16(data, quint16(6 + 12 * recordCount));
        QByteArray strings;
        for (const NameRecord& record : spec.records)
        {
            appendUInt16(data, record.platform);
            appendUInt16(data, 0);
            appendUInt16(data, 0);
            appendUInt16(data, record.nameId);
            appendUInt16(data, quint16(record.length >= 0 ? record.length : int(record.text.size())));
            appendUInt16(data, quint16(record.offset >= 0 ? record.offset : int(strings.size())));
            strings += record.text;
        }
        data += strings;
        return data;
    }
};

/// Content page of the tests of the scrolling of PDFAppContentPage (the default
/// scrollToAnchor, the deferred scroll requested by a test)
class ScrollTestPage : public pdf::PDFAppContentPage
{
    Q_OBJECT

public:
    using pdf::PDFAppContentPage::PDFAppContentPage;

    virtual QString getTitle() const override { return QStringLiteral("Scroll"); }

    void requestScroll(int position, const QString& anchor) { requestDeferredScroll(position, anchor); }
    void setAnchor(QWidget* widget) { setAnchorWidget(widget); }

    /// Named anchors asked by the page (the default implementation knows none)
    QStringList anchors;

protected:
    virtual bool scrollToAnchor(const QString& anchor) override
    {
        anchors << anchor;
        return pdf::PDFAppContentPage::scrollToAnchor(anchor);
    }
};

/// Translator of the test: the texts of its table in another language
class InfoTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return entries.value(qMakePair(QByteArray(context ? context : ""), QByteArray(sourceText ? sourceText : "")));
    }

    virtual bool isEmpty() const override { return false; }

    /// Translations (context, source text)
    QHash<QPair<QByteArray, QByteArray>, QString> entries;
};

class AppInfoPagesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void routes_data();
    void routes();
    void supportRouteEntry();
    void setRouteDirect_data();
    void setRouteDirect();

    void buildInfo();
    void buildInfoTexts();
    void buildInfoDiskDatabase();
    void buildInfoLogFolder();
    void buildInfoWithoutStorage();
    void buildInfoStorageStateChanged();
    void copyBuildInfo();
    void libraries();
    void libraryModel();
    void fontVersion();
    void fontVersionRecords_data();
    void fontVersionRecords();
    void thirdPartyAnchor();
    void aboutLinks();
    void aboutReleaseText();
    void languageChange();

    void videoCatalog();
    void videoCatalogSearch_data();
    void videoCatalogSearch();
    void supportFilters_data();
    void supportFilters();
    void supportRouteInHistory();
    void supportForYou();
    void supportEmptyTexts();
    void supportNotificationTopics();
    void supportSanitize_data();
    void supportSanitize();
    void supportSanitizeRouteLength_data();
    void supportSanitizeRouteLength();
    void supportTagsAndLinks();
    void supportRandomQueries();
    void infoPagePartsDeleted();

    void sponsorLinks();

    void geometryAbout();
    void geometrySupport();
    void geometrySponsor();
    void pixels_data();
    void pixels();
    void themeChange();
    void rightToLeft();
    void minimalWindow();
    void keyboard();
    void accessibleNames();

    void deleteWithOpenMenu();
    void deleteWithPendingNotification();
    void deleteWithPendingScroll();
    void deleteFromOwnLink();
    void deleteWindow();

    void flowLayout();
    void flowLayoutEdges();
    void responsiveRow();
    void grid();
    void gridFractionalColumns();
    void columns();
    void columnsEdges();
    void sectionHeader();
    void contentPageLinks();
    void contentPageScroll();
    void contentPageDeferredScroll();

    void screenshots();
};

void AppInfoPagesTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppInfoPagesTest::cleanup()
{
    InfoFixture::settle();
}

void AppInfoPagesTest::routes_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("pageClass");

    QTest::newRow("about") << "/about" << "pdf::PDFAppAboutPage";
    QTest::newRow("about anchor") << "/about#third-party" << "pdf::PDFAppAboutPage";
    QTest::newRow("support") << "/support" << "pdf::PDFAppSupportPage";
    QTest::newRow("support topic") << "/support?topic=pages" << "pdf::PDFAppSupportPage";
    QTest::newRow("support for you") << "/support?topic=for-you" << "pdf::PDFAppSupportPage";
    QTest::newRow("support search") << "/support?q=merge%20pdf" << "pdf::PDFAppSupportPage";
    QTest::newRow("support both") << "/support?topic=review&q=compare" << "pdf::PDFAppSupportPage";
    QTest::newRow("sponsor") << "/sponsor" << "pdf::PDFAppSponsorPage";

    QTest::newRow("about unknown anchor") << "/about#nothing" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("about query") << "/about?x=1" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support unknown topic") << "/support?topic=bogus" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support topic case") << "/support?topic=Pages" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support unknown key") << "/support?sort=name" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support long search") << QStringLiteral("/support?q=%1").arg(QString(300, QLatin1Char('a'))) << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support anchor") << "/support#videos" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("support duplicate") << "/support?topic=pages&topic=forms" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("sponsor query") << "/sponsor?topic=pages" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("sponsor anchor") << "/sponsor#top" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("about segment") << "/about/third-party" << "pdf::PDFPageNotFoundPage";
}

void AppInfoPagesTest::routes()
{
    QFETCH(QString, route);
    QFETCH(QString, pageClass);

    InfoFixture fixture;
    fixture.navigate(route);
    QVERIFY(fixture.currentPage());
    QCOMPARE(QString::fromLatin1(fixture.currentPage()->metaObject()->className()), pageClass);

    if (pageClass != QLatin1String("pdf::PDFPageNotFoundPage"))
    {
        // The navigation item of the page is highlighted, the header has its texts
        QVERIFY(!fixture.currentPage()->getHeaderTitle().isEmpty());
        QVERIFY(!fixture.currentPage()->getHeaderSubtitle().isEmpty());
        QCOMPARE(fixture.homeTab()->getHeader()->getTitleLabel()->text(), fixture.currentPage()->getHeaderTitle());
        QVERIFY(fixture.homeTab()->getNavItem() != pdf::PDFAppNavItem::None);
    }
}

void AppInfoPagesTest::supportRouteEntry()
{
    // The route table accepts exactly the topics of the catalog and For you
    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    for (const QString& topic : pdf::PDFSupportVideoCatalog::getRouteTopicIds())
    {
        const pdf::PDFAppRouteMatch match = table.match(pdf::PDFAppSupportPage::createRoute(topic, QStringLiteral("x")));
        QVERIFY2(match.result != pdf::PDFAppRouteMatch::Result::InvalidParameter, qPrintable(topic));
    }

    const pdf::PDFAppRoute route = pdf::PDFAppSupportPage::createRoute(QStringLiteral("forms"), QStringLiteral("sign & fill"));
    QVERIFY(route.isValid());
    QCOMPARE(pdf::PDFAppRoute::parse(route.toString()), route);
    QCOMPARE(route.getQueryValue(QStringLiteral("q")), QStringLiteral("sign & fill"));
    QCOMPARE(pdf::PDFAppSupportPage::createRoute(QString(), QString()).toString(), QStringLiteral("/support"));

    // A long text is cut to the longest search
    const pdf::PDFAppRoute longRoute = pdf::PDFAppSupportPage::createRoute(QString(), QString(1000, QLatin1Char('x')));
    QCOMPARE(longRoute.getQueryValue(QStringLiteral("q")).size(), qsizetype(pdf::PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH));
}

void AppInfoPagesTest::setRouteDirect_data()
{
    QTest::addColumn<QString>("pageName");
    QTest::addColumn<QString>("route");
    QTest::addColumn<bool>("isAccepted");

    // The route table refuses these routes before the page; the pages refuse them
    // too, when a route is given to them directly
    QTest::newRow("about") << "about" << "/about" << true;
    QTest::newRow("about anchor") << "about" << "/about#third-party" << true;
    QTest::newRow("about unknown anchor") << "about" << "/about#nothing" << false;
    QTest::newRow("support") << "support" << "/support" << true;
    QTest::newRow("support topic and search") << "support" << "/support?topic=forms&q=sign" << true;
    QTest::newRow("support for you") << "support" << "/support?topic=for-you" << true;
    QTest::newRow("support unknown topic") << "support" << "/support?topic=bogus" << false;
    QTest::newRow("support topic case") << "support" << "/support?topic=Forms" << false;
    QTest::newRow("support longest search") << "support" << QStringLiteral("/support?q=%1").arg(QString(256, QLatin1Char('a'))) << true;
    QTest::newRow("support long search") << "support" << QStringLiteral("/support?q=%1").arg(QString(257, QLatin1Char('a'))) << false;
    QTest::newRow("support anchor") << "support" << "/support#videos" << false;
    QTest::newRow("sponsor") << "sponsor" << "/sponsor" << true;
    QTest::newRow("sponsor query") << "sponsor" << "/sponsor?topic=pages" << false;
    QTest::newRow("sponsor anchor") << "sponsor" << "/sponsor#top" << false;
}

void AppInfoPagesTest::setRouteDirect()
{
    QFETCH(QString, pageName);
    QFETCH(QString, route);
    QFETCH(bool, isAccepted);

    const pdf::PDFAppRoute parsed = pdf::PDFAppRoute::parse(route);
    QVERIFY(parsed.isValid());

    std::unique_ptr<pdf::PDFAppPage> page;
    if (pageName == QLatin1String("about"))
    {
        page = std::make_unique<pdf::PDFAppAboutPage>(nullptr);
    }
    else if (pageName == QLatin1String("support"))
    {
        page = std::make_unique<pdf::PDFAppSupportPage>(nullptr);
    }
    else
    {
        page = std::make_unique<pdf::PDFAppSponsorPage>(nullptr);
    }

    QCOMPARE(page->setRoute(parsed), isAccepted);
    QCOMPARE(page->getRoute() == parsed, isAccepted);

    // A refused route keeps the filter of Support (All, no search)
    if (pdf::PDFAppSupportPage* support = qobject_cast<pdf::PDFAppSupportPage*>(page.get()))
    {
        QCOMPARE(support->getTopic(), isAccepted ? parsed.getQueryValue(QStringLiteral("topic")) : QString());
        QCOMPARE(support->getSearchText(), isAccepted ? parsed.getQueryValue(QStringLiteral("q")) : QString());
        QCOMPARE(support->getSearchField()->text(), support->getSearchText());
    }
}

void AppInfoPagesTest::buildInfo()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);

    pdf::PDFKeyValueList* list = page->getBuildInfo();
    QVERIFY(list);
    QStringList keys;
    for (int i = 0; i < list->getRowCount(); ++i)
    {
        keys << list->getKey(i);
    }

    // Nothing invented: the build date and the commit only, when the build defines them
    QStringList expected = { QStringLiteral("Version"), QStringLiteral("Compiler"), QStringLiteral("Qt"), QStringLiteral("Platform"), QStringLiteral("Settings"), QStringLiteral("Log folder") };
    if (!pdf::PDFAppAboutInfo::getBuildDate().isEmpty())
    {
        expected.insert(1, QStringLiteral("Build date"));
    }
    if (!pdf::PDFAppAboutInfo::getCommit().isEmpty())
    {
        expected.insert(expected.indexOf(QStringLiteral("Compiler")), QStringLiteral("Commit"));
    }
    QCOMPARE(keys, expected);

    const QString version = list->getValue(0);
    QVERIFY(version.startsWith(pdf::PDFAppFooter::getDisplayVersion()));
    QCOMPARE(pdf::PDFAppFooter::getDisplayVersion().count(QLatin1Char('.')), 2);
    QVERIFY(version.contains(pdf::PDFAppAboutInfo::getBuildType()));
    QVERIFY(list->getValue(keys.indexOf(QStringLiteral("Compiler"))).contains(QSysInfo::buildCpuArchitecture()));
    QVERIFY(list->getValue(keys.indexOf(QStringLiteral("Qt"))).startsWith(QString::fromLatin1(qVersion())));

    // Portable mode: nothing is written (the database and the logs)
    QCOMPARE(list->getValue(keys.indexOf(QStringLiteral("Settings"))), QStringLiteral("Not saved (portable mode)"));
    QCOMPARE(list->getValue(keys.indexOf(QStringLiteral("Log folder"))), QStringLiteral("None (portable mode)"));

    // The hero shows the version of the footer
    QLabel* versionLabel = page->findChild<QLabel*>(QStringLiteral("appVersion"));
    QVERIFY(versionLabel);
    QVERIFY(versionLabel->text().contains(pdf::PDFAppFooter::getDisplayVersion()));
}

void AppInfoPagesTest::buildInfoTexts()
{
    using Info = pdf::PDFAppAboutInfo;

    QCOMPARE(Info::getQtVersionText(QStringLiteral("6.11.2"), QStringLiteral("6.11.2")), QStringLiteral("6.11.2"));
    QCOMPARE(Info::getQtVersionText(QStringLiteral("6.11.3"), QStringLiteral("6.11.2")), QStringLiteral("6.11.3 (built with 6.11.2)"));
    QCOMPARE(Info::getQtVersionText(QStringLiteral("6.11.3"), QString()), QStringLiteral("6.11.3"));
    QVERIFY(!Info::getCompiler().isEmpty());
    QVERIFY(!Info::getPlatform().isEmpty());

    pdf::PDFDatabaseState state;
    state.filePath = QDir::temp().filePath(QStringLiteral("pdf4qt.db"));
    state.mode = pdf::PDFDatabaseStorageMode::Disk;
    QCOMPARE(Info::getSettingsText(state), QDir::toNativeSeparators(state.filePath));
    state.mode = pdf::PDFDatabaseStorageMode::Portable;
    QCOMPARE(Info::getSettingsText(state), QStringLiteral("Not saved (portable mode)"));
    state.mode = pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema;
    QVERIFY(Info::getSettingsText(state).contains(QDir::toNativeSeparators(state.filePath)));
    QVERIFY(Info::getSettingsText(state).contains(QStringLiteral("read only")));
    state.mode = pdf::PDFDatabaseStorageMode::MemoryFallback;
    QVERIFY(Info::getSettingsText(state).startsWith(QStringLiteral("Not saved")));
    QVERIFY(Info::getSettingsText(state).contains(QDir::toNativeSeparators(state.filePath)));
    state.mode = static_cast<pdf::PDFDatabaseStorageMode>(42);
    QVERIFY(Info::getSettingsText(state).isEmpty());

    QCOMPARE(Info::getLogText(QStringLiteral("C:/logs"), false), QDir::toNativeSeparators(QStringLiteral("C:/logs")));
    QCOMPARE(Info::getLogText(QString(), false), QStringLiteral("None"));
    QCOMPARE(Info::getLogText(QStringLiteral("C:/logs"), true), QStringLiteral("None (portable mode)"));

    const QList<pdf::PDFAppBuildInfoRow> rows = { { QStringLiteral("Version"), QStringLiteral("1.6.0 (Release)") }, { QStringLiteral("Qt"), QStringLiteral("6.11.2") } };
    QCOMPARE(Info::toText(rows), QStringLiteral("PDF4QT\nVersion: 1.6.0 (Release)\nQt: 6.11.2"));

    // Rows without a context: the portable texts
    const QList<pdf::PDFAppBuildInfoRow> noContext = Info::getBuildRows(nullptr);
    QVERIFY(noContext.size() >= 6);
    QCOMPARE(noContext.back().value, QStringLiteral("None (portable mode)"));
}

void AppInfoPagesTest::buildInfoDiskDatabase()
{
    // A database on the disk: the row Settings is its file, Log folder the folder of the logs
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("PDF4QT_APP_DATA_DIRECTORY", QDir::toNativeSeparators(directory.path()).toLocal8Bit());
    {
        const QString file = QDir(directory.path()).filePath(QStringLiteral("pdf4qt.db"));
        InfoFixture fixture(QStringList(), file);
        QVERIFY(fixture.context->getStorageState().isPersistent());
        fixture.navigate(QStringLiteral("/about"));
        pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
        QVERIFY(page);

        const QList<pdf::PDFAppBuildInfoRow>& rows = page->getBuildRows();
        QCOMPARE(rows[rows.size() - 2].value, QDir::toNativeSeparators(file));
        // No log is installed by the test: no folder is shown (no log files are written)
        QVERIFY(!pdf::PDFAppLog::isInstalled());
        QVERIFY(fixture.context->getLogDirectory().isEmpty());
        QCOMPARE(rows.back().value, QStringLiteral("None"));
    }
    qunsetenv("PDF4QT_APP_DATA_DIRECTORY");
}

void AppInfoPagesTest::buildInfoLogFolder()
{
    // The log is installed (not portable): the row Log folder is the folder of the log
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logDirectory = QDir(directory.path()).filePath(QStringLiteral("logs"));
    pdf::PDFAppLogOptions options;
    options.directory = logDirectory;
    QVERIFY(pdf::PDFAppLog::install(options));

    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
    QVERIFY(!context.isPortable());
    QCOMPARE(context.getLogDirectory(), logDirectory);
    const QList<pdf::PDFAppBuildInfoRow> rows = pdf::PDFAppAboutInfo::getBuildRows(&context);

    // The log of the test is removed: About shows no folder of the removed log
    pdf::PDFAppLog::uninstall();
    QVERIFY(pdf::PDFAppLog::getLogDirectory().isEmpty());
    QVERIFY(context.getLogDirectory().isEmpty());
    const QList<pdf::PDFAppBuildInfoRow> rowsAfterUninstall = pdf::PDFAppAboutInfo::getBuildRows(&context);
    QCOMPARE(rowsAfterUninstall.back().value, QStringLiteral("None"));

    QCOMPARE(rows.back().key, QStringLiteral("Log folder"));
    QCOMPARE(rows.back().value, QDir::toNativeSeparators(logDirectory));
}

void AppInfoPagesTest::buildInfoWithoutStorage()
{
    // A context without the storage (not opened, not portable): the folder of the
    // logs of the application, nothing about a database file
    qputenv("PDF4QT_APP_DATA_DIRECTORY", QDir::toNativeSeparators(QDir::tempPath()).toLocal8Bit());
    {
        pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
        QVERIFY(!context.getDatabase());
        QVERIFY(!context.getNotifier());
        const QList<pdf::PDFAppBuildInfoRow> rows = pdf::PDFAppAboutInfo::getBuildRows(&context);
        QCOMPARE(rows.back().value, QStringLiteral("None"));

        // Support reads no used tools and keeps For you empty
        pdf::PDFAppSupportPage support(&context);
        QVERIFY(support.getUsedToolIds().isEmpty());
        support.setFilter(QStringLiteral("for-you"), QString());
        QCOMPARE(support.getVisibleVideoCards().size(), 0);
        QCOMPARE(support.getEmptyText()->text(), QStringLiteral("Tutorials for the tools you use appear here. You have not used any tool yet."));
    }
    qunsetenv("PDF4QT_APP_DATA_DIRECTORY");

    // Pages without a context: the portable texts, no connection, no crash
    pdf::PDFAppAboutPage about(nullptr);
    const QList<pdf::PDFAppBuildInfoRow>& rows = about.getBuildRows();
    QCOMPARE(rows[rows.size() - 2].value, QStringLiteral("Not saved (portable mode)"));
    QCOMPARE(rows.back().value, QStringLiteral("None (portable mode)"));
    QCOMPARE(about.getBuildInfo()->getRowCount(), int(rows.size()));

    pdf::PDFAppSupportPage support(nullptr);
    support.refreshUsedTools();
    QVERIFY(support.getUsedToolIds().isEmpty());
    QCOMPARE(support.getVisibleVideoCards().size(), 12);
}

void AppInfoPagesTest::buildInfoStorageStateChanged()
{
    // A change of the state of the storage fills the build information again
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    const int rowCount = page->getBuildInfo()->getRowCount();
    QVERIFY(rowCount >= 6);
    page->getBuildInfo()->clear();
    QCOMPARE(page->getBuildInfo()->getRowCount(), 0);
    Q_EMIT fixture.context->storageStateChanged();
    QCOMPARE(page->getBuildInfo()->getRowCount(), rowCount);
    QCOMPARE(page->getBuildInfo()->getValue(rowCount - 2), QStringLiteral("Not saved (portable mode)"));

    // The list deleted by the application: the rows are still computed
    delete page->getBuildInfo();
    Q_EMIT fixture.context->storageStateChanged();
    QCOMPARE(page->getBuildRows().size(), rowCount);
    QVERIFY(!page->getBuildInfo());
}

void AppInfoPagesTest::copyBuildInfo()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page && page->getCopyButton());

    QApplication::clipboard()->clear();
    QTest::mouseClick(page->getCopyButton(), Qt::LeftButton);
    const QString text = QApplication::clipboard()->text();
    QCOMPARE(text, pdf::PDFAppAboutInfo::toText(page->getBuildRows()));
    QVERIFY(text.contains(pdf::PDFAppFooter::getDisplayVersion()));

    // The button is as wide as the card (button-wrap 2 / 8 / 2 / 2)
    QWidget* card = page->findChild<QWidget*>(QStringLiteral("buildCard"));
    QVERIFY(card);
    QCOMPARE(page->getCopyButton()->width(), card->width() - 2 * 9 - 2 * 2);
}

void AppInfoPagesTest::libraries()
{
    const std::vector<pdf::PDFAppThirdPartyLibrary> libraries = pdf::PDFAppAboutInfo::getLibraries();
    QStringList names;
    for (const pdf::PDFAppThirdPartyLibrary& library : libraries)
    {
        names << library.name;
        QVERIFY2(!library.purpose.isEmpty(), qPrintable(library.name));
        QVERIFY2(!library.license.isEmpty(), qPrintable(library.name));
        QVERIFY2(library.homepage.isValid() && library.homepage.scheme() == QLatin1String("https"), qPrintable(library.name));

        // Only the libraries without a version in their headers have no version
        if (library.name != QLatin1String("Brotli") && library.name != QLatin1String("bzip2"))
        {
            QVERIFY2(!library.version.isEmpty(), qPrintable(library.name));
            QVERIFY2(library.version.front().isDigit(), qPrintable(library.name + QStringLiteral(" ") + library.version));
        }
    }

    // The libraries, which Pdf4QtLibCore and Pdf4QtLibApp link on every platform
    for (const char* name : { "Qt", "FreeType", "OpenSSL", "Little CMS", "zlib", "libjpeg-turbo", "OpenJPEG", "Blend2D", "Clipper2", "SQLite", "Inter" })
    {
        QVERIFY2(names.contains(QString::fromLatin1(name)), name);
    }
    QCOMPARE(names.removeDuplicates(), 0);

    // Versions really linked
    for (const pdf::PDFAppThirdPartyLibrary& library : libraries)
    {
        if (library.name == QLatin1String("Qt"))
        {
            QCOMPARE(library.version, QString::fromLatin1(qVersion()));
        }
    }

    // The table of the page shows them all, the column Version a dash for an unknown one
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page && page->getLibraryView() && page->getLibraryView()->model());
    QAbstractItemModel* model = page->getLibraryView()->model();
    QCOMPARE(model->rowCount(), int(libraries.size()));
    QCOMPARE(model->columnCount(), 4);
    QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("License"));
    QCOMPARE(model->data(model->index(0, 0)).toString(), QStringLiteral("Qt"));
    QCOMPARE(model->data(model->index(0, 3), Qt::TextAlignmentRole).toInt() & int(Qt::AlignHorizontal_Mask), int(Qt::AlignTrailing));
    QVERIFY(!model->data(model->index(-1, 0)).isValid());
    QVERIFY(!model->data(model->index(0, 0, model->index(0, 0))).isValid());
    QVERIFY(!model->headerData(7, Qt::Horizontal).isValid());
    QVERIFY(!model->headerData(0, Qt::Vertical).isValid());
    QCOMPARE(model->rowCount(model->index(0, 0)), 0);

    // Colors of the cells are the tokens of the active theme
    QCOMPARE(model->data(model->index(0, 0), Qt::ForegroundRole).value<QColor>(), InfoTestHelper::token(pdf::PDFThemeColors::TextPrimary));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(model->data(model->index(0, 2), Qt::ForegroundRole).value<QColor>(), InfoTestHelper::token(pdf::PDFThemeColors::TextTertiary));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    for (int row = 0; row < model->rowCount(); ++row)
    {
        QVERIFY(!model->data(model->index(row, 1)).toString().isEmpty());
        QVERIFY(!model->data(model->index(row, 1), Qt::AccessibleTextRole).toString().isEmpty());
    }
}

void AppInfoPagesTest::libraryModel()
{
    pdf::PDFAppThirdPartyLibrary known;
    known.name = QStringLiteral("Known");
    known.version = QStringLiteral("1.2");
    known.purpose = QStringLiteral("Purpose of the library");
    known.license = QStringLiteral("MIT");
    known.homepage = QUrl(QStringLiteral("https://example.com/known"));

    pdf::PDFAppThirdPartyLibrary unknown;
    unknown.name = QStringLiteral("Unknown");
    unknown.purpose = QStringLiteral("Other purpose");
    unknown.license = QStringLiteral("BSD-3-Clause");
    unknown.licenseDetails = QStringLiteral("BSD-3-Clause, Zlib");
    unknown.homepage = QUrl(QStringLiteral("https://example.com/unknown"));

    using Model = pdf::PDFAppLibraryModel;
    Model model({ known, unknown });
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), int(Model::ColumnCount));
    QCOMPARE(model.columnCount(model.index(0, 0)), 0);

    // Texts and tooltips of the cells: the home page, the dash and the details of the license
    QCOMPARE(model.data(model.index(0, Model::LibraryColumn)).toString(), QStringLiteral("Known"));
    QCOMPARE(model.data(model.index(0, Model::LibraryColumn), Qt::ToolTipRole).toString(), QStringLiteral("https://example.com/known"));
    QCOMPARE(model.data(model.index(0, Model::VersionColumn)).toString(), QStringLiteral("1.2"));
    QCOMPARE(model.data(model.index(1, Model::VersionColumn)).toString(), QStringLiteral("—"));
    QCOMPARE(model.data(model.index(1, Model::VersionColumn), Qt::ToolTipRole).toString(), QStringLiteral("—"));
    QCOMPARE(model.data(model.index(0, Model::PurposeColumn), Qt::ToolTipRole).toString(), QStringLiteral("Purpose of the library"));
    QCOMPARE(model.data(model.index(0, Model::LicenseColumn)).toString(), QStringLiteral("MIT"));
    QCOMPARE(model.data(model.index(0, Model::LicenseColumn), Qt::ToolTipRole).toString(), QStringLiteral("MIT"));
    QCOMPARE(model.data(model.index(1, Model::LicenseColumn)).toString(), QStringLiteral("BSD-3-Clause"));
    QCOMPARE(model.data(model.index(1, Model::LicenseColumn), Qt::ToolTipRole).toString(), QStringLiteral("BSD-3-Clause, Zlib"));

    // Accessible texts: the unknown version is a word, other cells their texts
    QCOMPARE(model.data(model.index(1, Model::VersionColumn), Qt::AccessibleTextRole).toString(), QStringLiteral("Unknown version"));
    QCOMPARE(model.data(model.index(0, Model::VersionColumn), Qt::AccessibleTextRole).toString(), QStringLiteral("1.2"));
    QCOMPARE(model.data(model.index(1, Model::LibraryColumn), Qt::AccessibleTextRole).toString(), QStringLiteral("Unknown"));

    // Font of the name, alignment of the license, no data of other roles
    QVERIFY(model.data(model.index(0, Model::LibraryColumn), Qt::FontRole).canConvert<QFont>());
    QVERIFY(!model.data(model.index(0, Model::PurposeColumn), Qt::FontRole).isValid());
    QCOMPARE(model.data(model.index(0, Model::VersionColumn), Qt::ForegroundRole).value<QColor>(), InfoTestHelper::token(pdf::PDFThemeColors::TextSecondary));
    QCOMPARE(model.data(model.index(0, Model::PurposeColumn), Qt::TextAlignmentRole).toInt() & int(Qt::AlignHorizontal_Mask), int(Qt::AlignLeading));
    QVERIFY(!model.data(model.index(0, Model::LibraryColumn), Qt::DecorationRole).isValid());

    // Indices of another model outside of the range of rows and columns
    QStandardItemModel other(10, 10);
    QVERIFY(!model.data(other.index(5, 0)).isValid());
    QVERIFY(!model.data(other.index(0, 7)).isValid());
    QVERIFY(!model.data(QModelIndex()).isValid());

    // Header: texts, alignment, nothing for other sections and roles
    QCOMPARE(model.headerData(Model::LibraryColumn, Qt::Horizontal).toString(), QStringLiteral("Library"));
    QCOMPARE(model.headerData(Model::VersionColumn, Qt::Horizontal).toString(), QStringLiteral("Version"));
    QCOMPARE(model.headerData(Model::PurposeColumn, Qt::Horizontal).toString(), QStringLiteral("Purpose"));
    QCOMPARE(model.headerData(Model::LicenseColumn, Qt::Horizontal, Qt::TextAlignmentRole).toInt() & int(Qt::AlignHorizontal_Mask), int(Qt::AlignTrailing));
    QCOMPARE(model.headerData(Model::LibraryColumn, Qt::Horizontal, Qt::TextAlignmentRole).toInt() & int(Qt::AlignHorizontal_Mask), int(Qt::AlignLeading));
    QVERIFY(!model.headerData(-1, Qt::Horizontal).isValid());
    QVERIFY(!model.headerData(Model::ColumnCount, Qt::Horizontal).isValid());
    QVERIFY(!model.headerData(Model::LibraryColumn, Qt::Horizontal, Qt::ToolTipRole).isValid());

    // The view sets its columns only for a model of the four columns
    pdf::PDFAppLibraryView view;
    view.updateColumns();
    QStandardItemModel narrow(1, 2);
    view.setModel(&narrow);
    view.updateColumns();
    QCOMPARE(view.header()->sectionResizeMode(0), QHeaderView::Interactive);
    view.setModel(&model);
    view.resize(800, 200);
    view.updateColumns();
    QCOMPARE(view.header()->sectionResizeMode(Model::LibraryColumn), QHeaderView::Fixed);
    QCOMPARE(view.header()->sectionResizeMode(Model::PurposeColumn), QHeaderView::Stretch);
    QCOMPARE(view.header()->sectionSize(Model::LibraryColumn), pdf::PDFAppLibraryView::LIBRARY_WIDTH);
    QCOMPARE(view.header()->sectionSize(Model::LicenseColumn), pdf::PDFAppLibraryView::LICENSE_WIDTH);
}

void AppInfoPagesTest::fontVersion()
{
    QFile file(QStringLiteral(":/pdf4qtfluent/fonts/Inter-Regular.ttf"));
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray data = file.readAll();
    const QString version = pdf::PDFAppAboutInfo::getFontVersion(data);
    QVERIFY(!version.isEmpty());
    QVERIFY(version.front().isDigit());

    // Damaged data never crash and give nothing
    QVERIFY(pdf::PDFAppAboutInfo::getFontVersion(QByteArray()).isEmpty());
    QVERIFY(pdf::PDFAppAboutInfo::getFontVersion(QByteArray(5, '\xff')).isEmpty());

    // The table directory of Inter is longer than 64 bytes
    QVERIFY(pdf::PDFAppAboutInfo::getFontVersion(data.left(64)).isEmpty());

    // A truncated font gives nothing or the whole version, never a part of it
    for (qsizetype length = 6; length < data.size(); length = length * 3 / 2)
    {
        const QString truncated = pdf::PDFAppAboutInfo::getFontVersion(data.left(length));
        QVERIFY2(truncated.isEmpty() || truncated == version, qPrintable(QString::number(length)));
    }

    QRandomGenerator random(1234);
    for (int round = 0; round < 500; ++round)
    {
        QByteArray damaged = data.left(4096);
        for (int i = 0; i < 16; ++i)
        {
            damaged[int(random.bounded(damaged.size()))] = char(random.bounded(256));
        }
        const QString result = pdf::PDFAppAboutInfo::getFontVersion(damaged);
        QVERIFY(result.size() <= 32);
    }
}

void AppInfoPagesTest::fontVersionRecords_data()
{
    using Helper = InfoTestHelper;
    using Record = InfoTestHelper::NameRecord;
    using Spec = InfoTestHelper::FontSpec;

    QTest::addColumn<QByteArray>("font");
    QTest::addColumn<QString>("version");

    const auto windows = [](const QString& text, quint16 nameId = 5)
    {
        Record record;
        record.platform = 3;
        record.nameId = nameId;
        record.text = Helper::toUtf16BigEndian(text);
        return record;
    };
    const auto font = [](const QList<Record>& records, bool hasHeadTable = false)
    {
        Spec spec;
        spec.records = records;
        spec.hasHeadTable = hasHeadTable;
        return Helper::createFont(spec);
    };

    Record unicode = windows(QStringLiteral("Version 3.19"));
    unicode.platform = 0;
    Record macintosh;
    macintosh.platform = 1;
    macintosh.text = QByteArrayLiteral("Version 2.5; build");
    Record otherPlatform = windows(QStringLiteral("Version 9.9"));
    otherPlatform.platform = 2;
    Record oddLength = windows(QStringLiteral("Version 1.5"));
    oddLength.text.append('\x01');
    Record empty = windows(QString());
    Record beyondEnd = windows(QStringLiteral("Version 8.0"));
    beyondEnd.offset = 1000;
    Record longer = windows(QStringLiteral("Version 8.0"));
    longer.length = 200;

    QTest::newRow("windows") << font({ windows(QStringLiteral("Version 4.001;git-1234")) }) << "4.001";
    QTest::newRow("unicode") << font({ unicode }) << "3.19";
    QTest::newRow("macintosh latin-1") << font({ macintosh }) << "2.5";
    QTest::newRow("lowercase prefix") << font({ windows(QStringLiteral("  version 5.2  ")) }) << "5.2";
    QTest::newRow("no prefix") << font({ windows(QStringLiteral("1.2.3 beta")) }) << "1.2.3";
    QTest::newRow("digits only") << font({ windows(QStringLiteral("123.45")) }) << "123.45";
    QTest::newRow("longest version") << font({ windows(QStringLiteral("Version %1").arg(QString(40, QLatin1Char('7')))) }) << QString(32, QLatin1Char('7'));
    QTest::newRow("odd length") << font({ oddLength }) << "1.5";
    QTest::newRow("table head first") << font({ windows(QStringLiteral("Version 6.1")) }, true) << "6.1";
    QTest::newRow("other platform") << font({ otherPlatform }) << QString();
    QTest::newRow("other platform first") << font({ otherPlatform, windows(QStringLiteral("Version 7.1")) }) << "7.1";
    QTest::newRow("not a version first") << font({ windows(QStringLiteral("Version beta")), windows(QStringLiteral("Version 7.2")) }) << "7.2";
    QTest::newRow("other names") << font({ windows(QStringLiteral("Inter"), 1), windows(QStringLiteral("Version 1.0"), 4) }) << QString();
    QTest::newRow("no records") << font({}) << QString();
    QTest::newRow("empty string") << font({ empty }) << QString();
    QTest::newRow("string beyond the end") << font({ beyondEnd }) << QString();
    QTest::newRow("string longer than the data") << font({ longer }) << QString();

    Spec noName;
    noName.hasHeadTable = true;
    noName.hasNameTable = false;
    QTest::newRow("no name table") << Helper::createFont(noName) << QString();

    Spec missingTables = noName;
    missingTables.tableCount = 5;
    QTest::newRow("table records missing") << Helper::createFont(missingTables) << QString();

    Spec nameBeyondEnd;
    nameBeyondEnd.records = { windows(QStringLiteral("Version 1.0")) };
    nameBeyondEnd.nameTableOffset = 100000;
    QTest::newRow("name table beyond the end") << Helper::createFont(nameBeyondEnd) << QString();

    Spec missingRecords;
    missingRecords.records = { windows(QStringLiteral("Inter"), 1) };
    missingRecords.nameCount = 200;
    QTest::newRow("name records missing") << Helper::createFont(missingRecords) << QString();
}

void AppInfoPagesTest::fontVersionRecords()
{
    QFETCH(QByteArray, font);
    QFETCH(QString, version);

    QCOMPARE(pdf::PDFAppAboutInfo::getFontVersion(font), version);

    // Every truncation of the font gives nothing or the whole version
    for (qsizetype length = 0; length < font.size(); ++length)
    {
        const QString truncated = pdf::PDFAppAboutInfo::getFontVersion(font.left(length));
        QVERIFY2(truncated.isEmpty() || truncated == version, qPrintable(QString::number(length)));
    }
}

void AppInfoPagesTest::aboutReleaseText()
{
    // The hero of About shows the release of the running version (What's New), not
    // the build date (110:2163); the build date stays a row of the build information
    using Info = pdf::PDFAppAboutInfo;
    const std::shared_ptr<const pdf::PDFWhatsNewCatalog> catalog = pdf::PDFWhatsNewCatalog::getBuiltin();
    QVERIFY(catalog && !catalog->getReleases().empty());

    bool hasReleased = false;
    bool hasUnreleased = false;
    for (const pdf::PDFWhatsNewRelease& release : catalog->getReleases())
    {
        const QString text = Info::getReleaseText(*catalog, release.version);
        if (release.isUnreleased || !release.date.isValid())
        {
            hasUnreleased = true;
            QCOMPARE(text, QStringLiteral("Not released yet"));
        }
        else
        {
            hasReleased = true;
            QCOMPARE(text, QStringLiteral("Released %1").arg(QLocale(QLocale::English, QLocale::UnitedStates).toString(release.date, QStringLiteral("MMM d, yyyy"))));
        }
        QCOMPARE(Info::getHeroVersionText(*catalog, release.version),
                 QStringLiteral("Version %1 · %2").arg(pdf::PDFWhatsNewRelease::getDisplayVersion(release.version), text));
    }
    QVERIFY(hasReleased);
    QVERIFY(hasUnreleased);
    QCOMPARE(Info::getReleaseText(*catalog, QVersionNumber({ 1, 0, 0 })), QStringLiteral("Released Sep 19, 2021"));

    // A version without a release in the catalog: no date is invented
    QVERIFY(Info::getReleaseText(*catalog, QVersionNumber({ 0, 0, 1 })).isEmpty());
    QVERIFY(Info::getReleaseText(*catalog, QVersionNumber()).isEmpty());
    QCOMPARE(Info::getHeroVersionText(*catalog, QVersionNumber({ 0, 0, 1 })), QStringLiteral("Version 0.0.1"));
    QCOMPARE(Info::getHeroVersionText(pdf::PDFWhatsNewCatalog(), QVersionNumber({ 1, 0, 0 })), QStringLiteral("Version 1.0.0"));
    QCOMPARE(Info::getHeroVersionText(pdf::PDFWhatsNewCatalog(), QVersionNumber()), QStringLiteral("Version %1").arg(Info::getVersion()));

    // The page shows the line of the running version, never "Built"
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QLabel* versionLabel = page->findChild<QLabel*>(QStringLiteral("appVersion"));
    QVERIFY(versionLabel);
    QCOMPARE(versionLabel->text(), Info::getHeroVersionText());
    QCOMPARE(Info::getHeroVersionText(), Info::getHeroVersionText(*catalog, pdf::PDFWhatsNewCatalog::getRunningVersion()));
    QVERIFY(!versionLabel->text().contains(QStringLiteral("Built")));
}

void AppInfoPagesTest::languageChange()
{
    // About, Support and Sponsor show every text in the new language; the state of
    // the page (filter, search text, scroll, focus, build information) is kept
    InfoFixture fixture;
    fixture.window->resize(1200, 700);
    fixture.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));

    InfoTestTranslator translator;
    translator.entries.insert({ "pdf::PDFAppAboutPage", "About the author" }, QStringLiteral("Über den Autor"));
    translator.entries.insert({ "pdf::PDFAppAboutPage", "View on GitHub" }, QStringLiteral("Auf GitHub ansehen"));
    translator.entries.insert({ "pdf::PDFAppAboutInfo", "Version %1" }, QStringLiteral("Fassung %1"));
    translator.entries.insert({ "pdf::PDFAppAboutInfo", "Log folder" }, QStringLiteral("Protokollordner"));
    translator.entries.insert({ "pdf::PDFAppSupportPage", "All" }, QStringLiteral("Alle"));
    translator.entries.insert({ "pdf::PDFAppSupportPage", "Video tutorials" }, QStringLiteral("Videoanleitungen"));
    translator.entries.insert({ "pdf::PDFAppSupportPage", "Search tutorials…" }, QStringLiteral("Anleitungen suchen…"));
    translator.entries.insert({ "pdf::PDFAppSponsorPage", "Free since 2018" }, QStringLiteral("Kostenlos seit 2018"));
    translator.entries.insert({ "pdf::PDFAppSponsorPage", "If PDF4QT saves you time, consider giving some back." }, QStringLiteral("Wenn PDF4QT Ihnen Zeit spart, geben Sie etwas zurück."));

    // About: scrolled to the libraries, the focus on a button
    fixture.navigate(QStringLiteral("/about#third-party"));
    pdf::PDFAppAboutPage* about = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(about);
    QTRY_VERIFY(about->getScrollPosition() > 0);
    const int buildRows = about->getBuildInfo()->getRowCount();
    about->getGitHubButton()->setFocus(Qt::TabFocusReason);
    QCOMPARE(QApplication::focusWidget(), about->getGitHubButton());
    InfoFixture::settle();
    const int aboutScroll = about->getScrollPosition();
    QVERIFY(aboutScroll > 0);
    QPointer<pdf::PDFAppColumns> oldColumns = about->getColumns();
    QPointer<QPushButton> oldButton = about->getGitHubButton();

    QVERIFY(QCoreApplication::installTranslator(&translator));
    InfoFixture::settle();
    QVERIFY(oldColumns.isNull());
    QVERIFY(oldButton.isNull());
    QVERIFY(InfoTestHelper::texts(about).contains(QStringLiteral("Über den Autor")));
    QCOMPARE(about->getGitHubButton()->text(), QStringLiteral("Auf GitHub ansehen"));
    QVERIFY(about->findChild<QLabel*>(QStringLiteral("appVersion"))->text().startsWith(QStringLiteral("Fassung ")));
    QCOMPARE(about->getBuildInfo()->getRowCount(), buildRows);
    QCOMPARE(about->getBuildInfo()->getKey(buildRows - 1), QStringLiteral("Protokollordner"));
    QCOMPARE(about->getBuildRows().back().key, QStringLiteral("Protokollordner"));
    QTRY_COMPARE(about->getScrollPosition(), aboutScroll);
    QCOMPARE(QApplication::focusWidget(), about->getGitHubButton());
    QVERIFY(about->getColumns()->isVisible());

    QCoreApplication::removeTranslator(&translator);
    InfoFixture::settle();
    QVERIFY(InfoTestHelper::texts(about).contains(QStringLiteral("About the author")));
    QCOMPARE(about->getBuildInfo()->getKey(buildRows - 1), QStringLiteral("Log folder"));

    // Support: the filter, the text of the search field and the focus in it
    const QString topic = pdf::PDFSupportVideoCatalog::getTopicId(pdf::PDFSupportVideoCatalog::getTopics().front());
    fixture.navigate(QStringLiteral("/support?topic=%1&q=pdf").arg(topic));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    QStringList visibleIds;
    for (pdf::PDFVideoCard* card : support->getVisibleVideoCards())
    {
        visibleIds << card->objectName();
    }
    support->getSearchField()->setFocus(Qt::TabFocusReason);
    support->getSearchField()->setCursorPosition(1);
    QPointer<pdf::PDFLineEdit> oldField = support->getSearchField();
    const int quickActionCount = support->getQuickActions().size();
    const int chipCount = support->getChips().size();

    QVERIFY(QCoreApplication::installTranslator(&translator));
    InfoFixture::settle();
    QVERIFY(oldField.isNull());
    QCOMPARE(support->getTopic(), topic);
    QCOMPARE(support->getSearchText(), QStringLiteral("pdf"));
    QCOMPARE(support->getSearchField()->text(), QStringLiteral("pdf"));
    QCOMPARE(support->getSearchField()->cursorPosition(), 1);
    QCOMPARE(support->getSearchField()->placeholderText(), QStringLiteral("Anleitungen suchen…"));
    QCOMPARE(QApplication::focusWidget(), support->getSearchField());
    QCOMPARE(support->getChip(QString())->text(), QStringLiteral("Alle"));
    QVERIFY(support->getChip(topic)->isChecked());
    QVERIFY(InfoTestHelper::texts(support).contains(QStringLiteral("Videoanleitungen")));
    QCOMPARE(support->getQuickActions().size(), quickActionCount);
    QCOMPARE(support->getChips().size(), chipCount);
    QStringList visibleAfter;
    for (pdf::PDFVideoCard* card : support->getVisibleVideoCards())
    {
        visibleAfter << card->objectName();
    }
    QCOMPARE(visibleAfter, visibleIds);
    QCOMPARE(support->getRoute().getQueryValue(QStringLiteral("topic")), topic);

    // The filter works on the new widgets
    support->setFilter(QString(), QString());
    QCOMPARE(support->getVisibleVideoCards().size(), int(pdf::PDFSupportVideoCatalog::getVideos().size()));
    QVERIFY(support->getChip(QString())->isChecked());
    QCoreApplication::removeTranslator(&translator);
    InfoFixture::settle();
    QCOMPARE(support->getChip(QString())->text(), QStringLiteral("All"));

    // Sponsor: the hero and the columns again, the focus on the same button
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* sponsor = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(sponsor);
    sponsor->getStarButton()->setFocus(Qt::TabFocusReason);
    const int highlightCount = sponsor->getHighlightCards().size();
    const int linkRowCount = sponsor->getLinkRows().size();
    const int priorityCount = sponsor->getPriorityRows().size();
    QPointer<QWidget> oldHero = sponsor->getHero();

    QVERIFY(QCoreApplication::installTranslator(&translator));
    InfoFixture::settle();
    QVERIFY(oldHero.isNull());
    QVERIFY(sponsor->getHero()->isVisible());
    QVERIFY(InfoTestHelper::texts(sponsor).contains(QStringLiteral("Kostenlos seit 2018")));
    QVERIFY(InfoTestHelper::texts(sponsor).contains(QStringLiteral("Wenn PDF4QT Ihnen Zeit spart, geben Sie etwas zurück.")));
    QCOMPARE(QApplication::focusWidget(), sponsor->getStarButton());
    QCOMPARE(sponsor->getHighlightCards().size(), highlightCount);
    QCOMPARE(sponsor->getLinkRows().size(), linkRowCount);
    QCOMPARE(sponsor->getPriorityRows().size(), priorityCount);
    QCoreApplication::removeTranslator(&translator);
    InfoFixture::settle();
    QVERIFY(InfoTestHelper::texts(sponsor).contains(QStringLiteral("Free since 2018")));

    // Pages outside of a window (not shown, no context): no scroll, the anchor of the
    // route is kept for the showing of the page
    {
        pdf::PDFAppAboutPage hiddenAbout(nullptr);
        QVERIFY(hiddenAbout.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/about#third-party"))));
        pdf::PDFAppSupportPage hiddenSupport(nullptr);
        pdf::PDFAppSponsorPage hiddenSponsor(nullptr);
        QVERIFY(QCoreApplication::installTranslator(&translator));
        InfoFixture::settle();
        QVERIFY(InfoTestHelper::texts(&hiddenAbout).contains(QStringLiteral("Über den Autor")));
        QCOMPARE(hiddenSupport.getChip(QString())->text(), QStringLiteral("Alle"));
        QVERIFY(InfoTestHelper::texts(&hiddenSponsor).contains(QStringLiteral("Kostenlos seit 2018")));
        QCOMPARE(hiddenAbout.getRoute().getFragment(), QStringLiteral("third-party"));
        QCoreApplication::removeTranslator(&translator);
        InfoFixture::settle();
    }

    // The helpers of the rebuild with missing or hostile input
    QVERIFY(pdf::PDFAppPageRebuild::getFocusedName(nullptr).isEmpty());
    pdf::PDFAppPageRebuild::restoreFocus(nullptr, QStringLiteral("starButton"));
    pdf::PDFAppPageRebuild::restoreFocus(sponsor, QString());
    pdf::PDFAppPageRebuild::restoreFocus(sponsor, QStringLiteral("noSuchWidget"));
    QCOMPARE(QApplication::focusWidget(), sponsor->getStarButton());
    QCOMPARE(pdf::PDFAppPageRebuild::getFocusedName(sponsor), QStringLiteral("starButton"));
    sponsor->setFocus(Qt::OtherFocusReason);
    QVERIFY(pdf::PDFAppPageRebuild::getFocusedName(sponsor).isEmpty());
    pdf::PDFAppPageRebuild::restoreFocus(sponsor, QStringLiteral("hero"));
    QVERIFY(QApplication::focusWidget() != sponsor->getHero());
}

void AppInfoPagesTest::thirdPartyAnchor()
{
    InfoFixture fixture;
    fixture.window->resize(1440, 700);
    fixture.navigate(QStringLiteral("/about#third-party"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QTRY_VERIFY(fixture.homeTab()->getPageScrollArea()->verticalScrollBar()->value() > 0);

    // The section of the libraries is at the top of the scroll area
    QScrollArea* scrollArea = fixture.homeTab()->getPageScrollArea();
    const int top = page->getLibrariesSection()->mapTo(scrollArea->viewport(), QPoint(0, 0)).y();
    QVERIFY2(qAbs(top) <= 1 || scrollArea->verticalScrollBar()->value() == scrollArea->verticalScrollBar()->maximum(), qPrintable(QString::number(top)));

    // Without the anchor the page starts at its top
    fixture.navigate(QStringLiteral("/about"));
    QTRY_COMPARE(fixture.homeTab()->getPageScrollArea()->verticalScrollBar()->value(), 0);

    // Back keeps the position of the view (state of the history)
    QScrollBar* scrollBar = fixture.homeTab()->getPageScrollArea()->verticalScrollBar();
    scrollBar->setValue(120);
    const int saved = scrollBar->value();
    fixture.navigate(QStringLiteral("/sponsor"));
    QVERIFY(fixture.navigator()->goBack());
    InfoFixture::settle();
    QVERIFY(fixture.page<pdf::PDFAppAboutPage>());
    QTRY_COMPARE(scrollBar->value(), saved);

    // A hostile view state is ignored
    fixture.page<pdf::PDFAppAboutPage>()->restoreViewState({ { QStringLiteral("scroll"), QStringLiteral("abc") } });
    fixture.page<pdf::PDFAppAboutPage>()->restoreViewState({ { QStringLiteral("scroll"), -5 } });
    fixture.page<pdf::PDFAppAboutPage>()->restoreViewState({ { QStringLiteral("scroll"), 1000000 } });
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), scrollBar->maximum());
}

void AppInfoPagesTest::aboutLinks()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    QTest::mouseClick(page->getWebsiteButton(), Qt::LeftButton);
    QTest::mouseClick(page->getGitHubButton(), Qt::LeftButton);
    QTest::mouseClick(page->getProfileButton(), Qt::LeftButton);
    QTest::mouseClick(page->getNoticesLink(), Qt::LeftButton);
    QTest::mouseClick(page->getLicenseLink(), Qt::LeftButton);
    const QList<pdf::PDFLinkRow*> rows = page->getLinkRows();
    QCOMPARE(rows.size(), 5);
    QTest::mouseClick(rows[0], Qt::LeftButton);
    QTest::mouseClick(rows[2], Qt::LeftButton);
    QTest::mouseClick(rows[3], Qt::LeftButton);
    QTest::mouseClick(rows[4], Qt::LeftButton);

    const QList<QUrl> expected = { pdf::PDFAppProjectLinks::getWebsite(), pdf::PDFAppProjectLinks::getRepository(), pdf::PDFAppProjectLinks::getAuthorProfile(),
                                   pdf::PDFAppAboutPage::getThirdPartyNoticesUrl(), pdf::PDFAppProjectLinks::getLicense(), pdf::PDFAppProjectLinks::getWebsite(),
                                   pdf::PDFAppProjectLinks::getRepository(), pdf::PDFAppProjectLinks::getDiscussions(), pdf::PDFAppProjectLinks::getNewIssue() };
    QCOMPARE(external.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i)
    {
        const QUrl url = external[i].at(0).toUrl();
        QCOMPARE(url, expected[i]);
        QCOMPARE(url.scheme(), QStringLiteral("https"));
    }

    // Documentation is the manual of the application, Sponsor the project its page
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QTest::mouseClick(rows[1], Qt::LeftButton);
    InfoFixture::settle();
    QCOMPARE(navigated.size(), 1);
    QCOMPARE(navigated[0].at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));

    // The manual has its own Help tab (owner decision O13): back to the Home tab
    QVERIFY(fixture.window->findHelpTab());
    QCOMPARE(fixture.currentTab(), fixture.window->findHelpTab());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route, pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));
    fixture.window->activateTab(fixture.homeTab());
    InfoFixture::settle();
    page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QTest::mouseClick(page->getSponsorButton(), Qt::LeftButton);
    InfoFixture::settle();
    QVERIFY(fixture.page<pdf::PDFAppSponsorPage>());
    QCOMPARE(fixture.homeTab()->getNavItem(), pdf::PDFAppNavItem::Sponsor);

    // Every project link is https and allowed by the navigator
    for (const QUrl& url : { pdf::PDFAppProjectLinks::getWebsite(), pdf::PDFAppProjectLinks::getRepository(), pdf::PDFAppProjectLinks::getDiscussions(),
                             pdf::PDFAppProjectLinks::getNewIssue(), pdf::PDFAppProjectLinks::getPullRequests(), pdf::PDFAppProjectLinks::getAuthorProfile(),
                             pdf::PDFAppProjectLinks::getSponsors(), pdf::PDFAppProjectLinks::getOneTimeSponsorship(), pdf::PDFAppProjectLinks::getMicrosoftStore(),
                             pdf::PDFAppProjectLinks::getYouTubeChannel(), pdf::PDFAppProjectLinks::getLicense(), pdf::PDFAppAboutPage::getThirdPartyNoticesUrl() })
    {
        QVERIFY2(pdf::PDFAppNavigator::isExternalUrlAllowed(url), qPrintable(url.toString()));
    }
}

void AppInfoPagesTest::videoCatalog()
{
    using Catalog = pdf::PDFSupportVideoCatalog;

    const std::vector<pdf::PDFSupportVideo>& videos = Catalog::getVideos();
    QCOMPARE(videos.size(), size_t(12));

    QSet<QString> ids;
    int topicTotal = 0;
    for (const pdf::PDFSupportVideo& video : videos)
    {
        QVERIFY(!video.getId().isEmpty());
        QVERIFY2(!ids.contains(video.getId()), qPrintable(video.getId()));
        ids.insert(video.getId());
        QCOMPARE(Catalog::findVideo(video.getId()), &video);

        QVERIFY(!video.getTitle().isEmpty());
        QVERIFY(!video.getDescription().isEmpty());
        QVERIFY(!video.getTags().isEmpty());
        QCOMPARE(video.getTags().size(), video.getSourceTags().size());
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{1,2}:\\d{2}$")).match(video.getDuration()).hasMatch());

        // Links: https on www.youtube.com only
        QVERIFY2(Catalog::isAllowedLink(video.getLink()), qPrintable(video.getLink().toString()));
        QVERIFY(pdf::PDFAppNavigator::isExternalUrlAllowed(video.getLink()));

        // Every tool of a video is a tool of the catalog of the core
        for (const QString& toolId : video.getToolIds())
        {
            QVERIFY2(pdf::PDFDocumentToolCatalog::isValidToolId(toolId), qPrintable(video.getId() + QStringLiteral(": ") + toolId));
        }

        // Bundled pictures load (the mockup has none: the placeholder of the card)
        if (!video.getThumbnailPath().isEmpty())
        {
            QVERIFY2(!QPixmap(video.getThumbnailPath()).isNull(), qPrintable(video.getThumbnailPath()));
        }

        QVERIFY(Catalog::getTopicId(video.getTopic()).size() > 0);
    }

    for (const pdf::PDFSupportVideoTopic topic : Catalog::getTopics())
    {
        topicTotal += Catalog::getVideoCount(topic);
        QCOMPARE(Catalog::findTopic(Catalog::getTopicId(topic)), std::optional<pdf::PDFSupportVideoTopic>(topic));
        QVERIFY(!Catalog::getTopicTitle(topic).isEmpty());
    }
    QCOMPARE(topicTotal, 12);

    // Counts of the chips of Figma 116:2419
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::GettingStarted), 2);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Pages), 2);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Convert), 2);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Forms), 1);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Security), 2);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Optimize), 1);
    QCOMPARE(Catalog::getVideoCount(pdf::PDFSupportVideoTopic::Review), 2);

    QVERIFY(Catalog::isAllowedLink(Catalog::getChannelUrl()));
    QVERIFY(!Catalog::findVideo(u"nothing"));
    QVERIFY(!Catalog::findVideo(u""));
    QVERIFY(!Catalog::findVideo(QString(1000, QLatin1Char('a'))));
    QVERIFY(!Catalog::findTopic(u"for-you"));
    QVERIFY(!Catalog::findTopic(u""));
    QVERIFY(Catalog::getTopicId(static_cast<pdf::PDFSupportVideoTopic>(99)).isEmpty());
    QVERIFY(Catalog::getTopicTitle(static_cast<pdf::PDFSupportVideoTopic>(99)).isEmpty());
    QVERIFY(Catalog::getRouteTopicIds().contains(QStringLiteral("for-you")));

    // Hostile links are refused
    for (const char* link : { "http://www.youtube.com/watch?v=1", "https://youtube.com/watch", "https://www.youtube.com.evil.example/x", "https://user@www.youtube.com/",
                              "https://www.youtube.com:8443/", "javascript:alert(1)", "file:///C:/x", "" })
    {
        QVERIFY2(!Catalog::isAllowedLink(QUrl(QString::fromLatin1(link))), link);
    }

    // For you: a video of a used tool
    const pdf::PDFSupportVideo* merge = Catalog::findVideo(u"merge-and-organize-pages");
    QVERIFY(merge);
    QVERIFY(Catalog::isForYou(*merge, { QStringLiteral("pages.merge") }));
    QVERIFY(!Catalog::isForYou(*merge, { QStringLiteral("review.compare") }));
    QVERIFY(!Catalog::isForYou(*merge, {}));
}

void AppInfoPagesTest::videoCatalogSearch_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("count");

    QTest::newRow("empty") << QString() << 12;
    QTest::newRow("spaces") << QStringLiteral("   ") << 12;
    QTest::newRow("title") << QStringLiteral("merge") << 1;
    QTest::newRow("case") << QStringLiteral("OCR") << 1;
    QTest::newRow("tag") << QStringLiteral("sign") << 2;
    QTest::newRow("description") << QStringLiteral("hidden data") << 1;
    QTest::newRow("two words") << QStringLiteral("pages basics") << 1;
    QTest::newRow("nothing") << QStringLiteral("xyz-nothing") << 0;
    QTest::newRow("regexp characters") << QStringLiteral(".*[(") << 0;
}

void AppInfoPagesTest::videoCatalogSearch()
{
    QFETCH(QString, text);
    QFETCH(int, count);

    int matches = 0;
    for (const pdf::PDFSupportVideo& video : pdf::PDFSupportVideoCatalog::getVideos())
    {
        matches += pdf::PDFSupportVideoCatalog::matchesSearch(video, text) ? 1 : 0;
    }
    QCOMPARE(matches, count);
}

void AppInfoPagesTest::supportFilters_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("checkedChip");
    QTest::addColumn<int>("visibleCount");
    QTest::addColumn<bool>("isEmptyTextShown");

    QTest::newRow("all") << "/support" << "chip-all" << 12 << false;
    QTest::newRow("getting started") << "/support?topic=getting-started" << "chip-getting-started" << 2 << false;
    QTest::newRow("pages") << "/support?topic=pages" << "chip-pages" << 2 << false;
    QTest::newRow("convert") << "/support?topic=convert" << "chip-convert" << 2 << false;
    QTest::newRow("forms") << "/support?topic=forms" << "chip-forms" << 1 << false;
    QTest::newRow("security") << "/support?topic=security" << "chip-security" << 2 << false;
    QTest::newRow("optimize") << "/support?topic=optimize" << "chip-optimize" << 1 << false;
    QTest::newRow("review") << "/support?topic=review" << "chip-review" << 2 << false;
    QTest::newRow("for you empty") << "/support?topic=for-you" << "chip-for-you" << 0 << true;
    QTest::newRow("search") << "/support?q=sign" << "chip-all" << 2 << false;
    QTest::newRow("search and topic") << "/support?topic=security&q=sign" << "chip-security" << 1 << false;
    QTest::newRow("search nothing") << "/support?q=nothing-here" << "chip-all" << 0 << true;
}

void AppInfoPagesTest::supportFilters()
{
    QFETCH(QString, route);
    QFETCH(QString, checkedChip);
    QFETCH(int, visibleCount);
    QFETCH(bool, isEmptyTextShown);

    InfoFixture fixture;
    fixture.navigate(route);
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    QCOMPARE(page->getVisibleVideoCards().size(), visibleCount);
    QVERIFY(page->findChild<QPushButton*>(checkedChip));
    QVERIFY(page->findChild<QPushButton*>(checkedChip)->isChecked());
    QCOMPARE(page->getEmptyText()->isVisible(), isEmptyTextShown);
    QCOMPARE(page->getVideoGrid()->isVisible(), !isEmptyTextShown);
    QCOMPARE(page->getSearchField()->text(), pdf::PDFAppRoute::parse(route).getQueryValue(QStringLiteral("q")));
    if (isEmptyTextShown)
    {
        QVERIFY(!page->getEmptyText()->text().isEmpty());
    }

    // Exactly one chip is checked
    int checked = 0;
    for (QPushButton* chip : page->getChips())
    {
        checked += chip->isChecked() ? 1 : 0;
    }
    QCOMPARE(checked, 1);
    QCOMPARE(page->getChips().size(), 9);
}

void AppInfoPagesTest::supportRouteInHistory()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/home"));
    fixture.navigate(QStringLiteral("/support"));
    QPointer<pdf::PDFAppSupportPage> page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    // A chip changes the route of the current entry, the page stays
    QPushButton* pages = page->getChip(QStringLiteral("pages"));
    QVERIFY(pages);
    QTest::mouseClick(pages, Qt::LeftButton);
    InfoFixture::settle();
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>(), page.data());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/support?topic=pages"));
    QCOMPARE(page->getRoute().toString(), QStringLiteral("/support?topic=pages"));
    QCOMPARE(page->getVisibleVideoCards().size(), 2);

    // Typing into the search keeps the focus in the field
    page->getSearchField()->setFocus();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("rot"));
    InfoFixture::settle();
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>(), page.data());
    QVERIFY(page->getSearchField()->hasFocus());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/support?topic=pages&q=rot"));
    QCOMPARE(page->getVisibleVideoCards().size(), 1);
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), 2);

    // Back and forward restore the filter from the route
    QVERIFY(fixture.navigator()->goBack());
    InfoFixture::settle();
    QVERIFY(!fixture.page<pdf::PDFAppSupportPage>());
    QVERIFY(fixture.navigator()->goForward());
    InfoFixture::settle();
    pdf::PDFAppSupportPage* restored = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(restored);
    QCOMPARE(restored->getTopic(), QStringLiteral("pages"));
    QCOMPARE(restored->getSearchText(), QStringLiteral("rot"));
    QCOMPARE(restored->getSearchField()->text(), QStringLiteral("rot"));
    QVERIFY(restored->getChip(QStringLiteral("pages"))->isChecked());

    // The chip All removes the topic, an empty search removes q
    QTest::mouseClick(restored->getChip(QString()), Qt::LeftButton);
    restored->getSearchField()->clear();
    QTest::keyClick(restored->getSearchField(), Qt::Key_A);
    QTest::keyClick(restored->getSearchField(), Qt::Key_Backspace);
    InfoFixture::settle();
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/support"));

    // An unknown topic set by code is All
    restored->setFilter(QStringLiteral("unknown"), QString());
    QCOMPARE(restored->getTopic(), QString());
}

void AppInfoPagesTest::supportForYou()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support?topic=for-you"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);
    QPushButton* forYou = page->getChip(QStringLiteral("for-you"));
    QVERIFY(forYou && forYou->isVisible());
    QCOMPARE(forYou->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 0);
    QVERIFY(page->getEmptyText()->isVisible());
    const QString emptyText = page->getEmptyText()->text();

    // Another connection (another thread) records the use of Merge: the notifier
    // tells the page, which reads the database again
    QVERIFY(InfoTestHelper::recordUsageInThread(fixture.context->getDatabase(), QStringLiteral("pages.merge")));
    QTRY_COMPARE(page->getUsedToolIds().size(), 1);
    QCOMPARE(forYou->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 1);
    QCOMPARE(page->getVisibleVideoCards().size(), 1);
    QCOMPARE(page->getVisibleVideoCards().front()->objectName(), QStringLiteral("merge-and-organize-pages"));
    QVERIFY(!page->getEmptyText()->isVisible());

    // A tool, which no video shows: still one video, a tool of two videos
    QVERIFY(InfoTestHelper::recordUsageInThread(fixture.context->getDatabase(), QStringLiteral("edit.content")));
    QTRY_COMPARE(page->getUsedToolIds().size(), 2);
    QCOMPARE(page->getVisibleVideoCards().size(), 1);
    QVERIFY(InfoTestHelper::recordUsageInThread(fixture.context->getDatabase(), QStringLiteral("protect.sign")));
    QTRY_COMPARE(page->getUsedToolIds().size(), 3);
    QCOMPARE(page->getVisibleVideoCards().size(), 2);

    // A row of another version (an unknown tool, a hostile text) is skipped: the
    // notification of the row arrives (the page has read the database again, its
    // slot was connected before the spy), the used tools stay
    QSignalSpy usageChanged(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    QThread* thread = QThread::create([database]()
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(), "INSERT INTO tool_usage(tool_id, last_used, count) VALUES(?, 1, 1)");
        statement.bindText(1, QStringLiteral("future.tool") + QChar(0x202E) + QStringLiteral("<script>"));
        statement.execute();
    });
    thread->start();
    thread->wait();
    delete thread;
    QTRY_VERIFY_WITH_TIMEOUT(InfoTestHelper::countUsageNotifications(usageChanged) > 0, 10000);
    QCOMPARE(page->getUsedToolIds().size(), 3);
    QCOMPARE(forYou->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 2);

    // The search narrows For you; no match = the text of a search
    page->setFilter(QStringLiteral("for-you"), QStringLiteral("nothing-matches"));
    QVERIFY(page->getEmptyText()->isVisible());
    QVERIFY(page->getEmptyText()->text() != emptyText);
    QCOMPARE(page->getEmptyText()->text(), QStringLiteral("No tutorials match “nothing-matches”."));

    // The page shown again reads the database (nothing is cached)
    fixture.navigate(QStringLiteral("/home"));
    fixture.navigate(QStringLiteral("/support"));
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>()->getUsedToolIds().size(), 3);
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>()->getChip(QStringLiteral("for-you"))->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 2);
}

void AppInfoPagesTest::supportEmptyTexts()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support?topic=for-you"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    // No tool used yet
    QCOMPARE(page->getEmptyText()->text(), QStringLiteral("Tutorials for the tools you use appear here. You have not used any tool yet."));

    // A used tool, which no tutorial shows
    QVERIFY(!fixture.context->getTools().recordUsage(QStringLiteral("edit.content")).isError());
    page->refreshUsedTools();
    QCOMPARE(page->getUsedToolIds(), QSet<QString>({ QStringLiteral("edit.content") }));
    QCOMPARE(page->getVisibleVideoCards().size(), 0);
    QVERIFY(page->getEmptyText()->isVisible());
    QCOMPARE(page->getEmptyText()->text(), QStringLiteral("None of the tutorials shows a tool you have used."));
    QCOMPARE(page->getChip(QStringLiteral("for-you"))->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 0);

    // A search without a result: the text of the search (spaces of the text simplified)
    page->setFilter(QStringLiteral("for-you"), QStringLiteral("  merge   pages "));
    QCOMPARE(page->getEmptyText()->text(), QStringLiteral("No tutorials match “merge pages”."));
    page->setFilter(QString(), QStringLiteral("xyz"));
    QCOMPARE(page->getEmptyText()->text(), QStringLiteral("No tutorials match “xyz”."));

    // A result hides the text
    page->setFilter(QString(), QStringLiteral("merge"));
    QVERIFY(!page->getEmptyText()->isVisible());
    QVERIFY(page->getVideoGrid()->isVisible());

    // The chip For you deleted by the application: the used tools are still read
    delete page->getChip(QStringLiteral("for-you"));
    QVERIFY(!fixture.context->getTools().recordUsage(QStringLiteral("pages.merge")).isError());
    page->refreshUsedTools();
    QCOMPARE(page->getUsedToolIds().size(), 2);
    page->setFilter(QStringLiteral("for-you"), QString());
    QCOMPARE(page->getVisibleVideoCards().size(), 1);
}

void AppInfoPagesTest::supportNotificationTopics()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);
    pdf::PDFDatabaseNotifier* notifier = fixture.context->getNotifier();
    QVERIFY(notifier);

    // Another connection writes the use of a tool, the notifier does not poll (the
    // page learns nothing by itself)
    notifier->setPollInterval(3600 * 1000);
    QVERIFY(InfoTestHelper::insertUsageRowInThread(fixture.context->getDatabase(), QStringLiteral("pages.merge")));
    InfoFixture::settle();
    QVERIFY(page->getUsedToolIds().isEmpty());

    // A change of another topic does not read the used tools
    Q_EMIT notifier->changed(QStringLiteral("settings"), QStringLiteral("appearance.colorScheme"));
    QVERIFY(page->getUsedToolIds().isEmpty());

    // Everything changed (a gap of the change log, another database): read again
    Q_EMIT notifier->everythingChanged();
    QCOMPARE(page->getUsedToolIds(), QSet<QString>({ QStringLiteral("pages.merge") }));
    QCOMPARE(page->getChip(QStringLiteral("for-you"))->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 1);

    // The change of the usage topic reads them too
    QVERIFY(InfoTestHelper::insertUsageRowInThread(fixture.context->getDatabase(), QStringLiteral("protect.sign")));
    InfoFixture::settle();
    QCOMPARE(page->getUsedToolIds().size(), 1);
    Q_EMIT notifier->changed(pdf::PDFToolRepository::getUsageTopic(), QStringLiteral("protect.sign"));
    QCOMPARE(page->getUsedToolIds().size(), 2);
}

void AppInfoPagesTest::supportSanitize_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    using Helper = InfoTestHelper;

    const QString emoji = Helper::fromCodes({ 0xD83D, 0xDE00 });
    QTest::newRow("plain") << QStringLiteral("merge pages") << QStringLiteral("merge pages");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("tab and newline") << QStringLiteral("a\tb\nc\rd") << QStringLiteral("a b c d");
    QTest::newRow("nul and escape") << Helper::fromCodes({ 'a', 0x00, 'b', 0x1B, 'c', 0x1F, 'd' }) << QStringLiteral("a b c d");
    QTest::newRow("delete and C1") << Helper::fromCodes({ 'a', 0x7F, 'b', 0x85, 'c', 0x9F, 'd' }) << QStringLiteral("a b c d");
    QTest::newRow("line separators") << Helper::fromCodes({ 'a', 0x2028, 'b', 0x2029, 'c' }) << QStringLiteral("a b c");
    QTest::newRow("visible neighbors") << Helper::fromCodes({ 0x20, 0x7E, 0xA0, 'x', 0x2027, 'y', 0x202A }) << Helper::fromCodes({ 0x20, 0x7E, 0xA0, 'x', 0x2027, 'y', 0x202A });
    QTest::newRow("surrogate pair") << QStringLiteral("a%1b").arg(emoji) << QStringLiteral("a%1b").arg(emoji);
    QTest::newRow("lone high surrogate") << Helper::fromCodes({ 'a', 0xD83D, 'b' }) << QStringLiteral("ab");
    QTest::newRow("lone low surrogate") << Helper::fromCodes({ 'a', 0xDE00, 'b' }) << QStringLiteral("ab");
    QTest::newRow("high surrogate at the end") << Helper::fromCodes({ 'a', 'b', 0xD83D }) << QStringLiteral("ab");
    QTest::newRow("swapped pair") << Helper::fromCodes({ 0xDE00, 0xD83D, 'x' }) << QStringLiteral("x");
    QTest::newRow("longest") << QString(256, QLatin1Char('a')) << QString(256, QLatin1Char('a'));
    QTest::newRow("too long") << QString(300, QLatin1Char('a')) << QString(256, QLatin1Char('a'));
    QTest::newRow("pair at the limit") << QString(255, QLatin1Char('a')) + emoji << QString(255, QLatin1Char('a'));
    QTest::newRow("pair before the limit") << QString(254, QLatin1Char('a')) + emoji + QStringLiteral("b") << QString(254, QLatin1Char('a')) + emoji;
}

void AppInfoPagesTest::supportSanitize()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);

    const QString result = pdf::PDFAppSupportPage::sanitizeSearchText(QString(), text);
    QCOMPARE(result, expected);
    QVERIFY(result.isValidUtf16());
    QVERIFY(pdf::PDFAppSupportPage::createRoute(QString(), result).isValid());

    // The page shows the sanitized text and puts it into the route
    pdf::PDFAppSupportPage page(nullptr);
    page.setFilter(QStringLiteral("pages"), text);
    QCOMPARE(page.getSearchText(), pdf::PDFAppSupportPage::sanitizeSearchText(QStringLiteral("pages"), text));
    QCOMPARE(page.getSearchField()->text(), page.getSearchText());
}

void AppInfoPagesTest::supportSanitizeRouteLength_data()
{
    QTest::addColumn<QString>("topic");
    QTest::addColumn<QString>("text");

    // 256 characters, whose route is longer than the longest route (2048): 3 or 4
    // bytes of UTF-8 per character, 9 or 12 characters of the encoded route
    const QString cjk(256, QChar(0x4E00));
    QString mixed(200, QChar(0x4E00));
    while (mixed.size() + 2 <= 256)
    {
        mixed += InfoTestHelper::fromCodes({ 0xD83D, 0xDE00 });
    }
    QTest::newRow("cjk") << QString() << cjk;
    QTest::newRow("cjk with topic") << QStringLiteral("getting-started") << cjk;
    QTest::newRow("cjk and emoji") << QString() << mixed;
    QTest::newRow("cjk and emoji with topic") << QStringLiteral("for-you") << mixed;
}

void AppInfoPagesTest::supportSanitizeRouteLength()
{
    QFETCH(QString, topic);
    QFETCH(QString, text);

    QVERIFY(!pdf::PDFAppRoutes::support(topic, text).isValid());

    // The longest prefix, whose route is valid, without a half of a pair
    const QString result = pdf::PDFAppSupportPage::sanitizeSearchText(topic, text);
    QVERIFY(!result.isEmpty());
    QVERIFY(text.startsWith(result));
    QVERIFY(result.isValidUtf16());
    QVERIFY(pdf::PDFAppSupportPage::createRoute(topic, result).isValid());
    const qsizetype next = text[result.size()].isHighSurrogate() ? result.size() + 2 : result.size() + 1;
    QVERIFY(!pdf::PDFAppSupportPage::createRoute(topic, text.left(next)).isValid());

    // The page keeps the route of its filter valid
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);
    page->setFilter(topic, text);
    QCOMPARE(page->getSearchText(), result);
    QVERIFY(fixture.currentTab()->getHistory().getCurrentEntry()->route.isValid());
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.getQueryValue(QStringLiteral("q")), result);
}

void AppInfoPagesTest::supportTagsAndLinks()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support?topic=forms"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    // A tag shows the tutorials of the tag in all topics
    pdf::PDFVideoCard* card = page->getVideoCard(QStringLiteral("fill-and-sign-forms"));
    QVERIFY(card && card->isVisible());
    const QList<pdf::PDFTag*> tags = card->getTagWidgets();
    QCOMPARE(tags.size(), 2);
    QTest::mouseClick(tags[1], Qt::LeftButton);
    InfoFixture::settle();
    QCOMPARE(page->getTopic(), QString());
    QCOMPARE(page->getSearchText(), QStringLiteral("Sign"));
    QCOMPARE(page->getVisibleVideoCards().size(), 2);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/support?q=Sign"));
    QCOMPARE(external.size(), 0);

    // The card opens its video, the button the channel (https of YouTube)
    QTest::mouseClick(card, Qt::LeftButton, Qt::NoModifier, QPoint(card->width() / 2, 40));
    QTest::mouseClick(page->getChannelButton(), Qt::LeftButton);
    QCOMPARE(external.size(), 2);
    QCOMPARE(external[0].at(0).toUrl(), pdf::PDFSupportVideoCatalog::findVideo(u"fill-and-sign-forms")->getLink());
    QCOMPARE(external[1].at(0).toUrl(), pdf::PDFSupportVideoCatalog::getChannelUrl());

    // A card with a forged link opens nothing
    pdf::PDFAppPageWidgets::setLink(card, QStringLiteral("https://evil.example/"));
    QTest::mouseClick(card, Qt::LeftButton, Qt::NoModifier, QPoint(card->width() / 2, 40));
    QCOMPARE(external.size(), 2);

    // The quick actions: GitHub, the manual, the discussion
    const QList<pdf::PDFQuickActionTile*> tiles = page->getQuickActions();
    QCOMPARE(tiles.size(), 4);
    QTest::mouseClick(tiles[0], Qt::LeftButton);
    QTest::mouseClick(tiles[1], Qt::LeftButton);
    QTest::mouseClick(tiles[3], Qt::LeftButton);
    QCOMPARE(external.size(), 5);
    QCOMPARE(external[2].at(0).toUrl(), pdf::PDFAppProjectLinks::getNewIssue());
    QCOMPARE(external[4].at(0).toUrl(), pdf::PDFAppProjectLinks::getDiscussions());

    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QTest::mouseClick(tiles[2], Qt::LeftButton);
    InfoFixture::settle();
    QCOMPARE(navigated.size(), 1);
    QCOMPARE(navigated[0].at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));
}

void AppInfoPagesTest::supportRandomQueries()
{
    // Random search texts (also invisible and right-to-left characters) never crash
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support"));
    QPointer<pdf::PDFAppSupportPage> page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    QRandomGenerator random(42);
    const QStringList topics = QStringList() << QString() << pdf::PDFSupportVideoCatalog::getRouteTopicIds() << QStringLiteral("bad");
    for (int round = 0; round < 200; ++round)
    {
        QString text;
        const int length = int(random.bounded(40));
        for (int i = 0; i < length; ++i)
        {
            text += QChar(char16_t(random.bounded(1, 0xD7FF)));
        }
        page->setFilter(topics[int(random.bounded(topics.size()))], text);
        QVERIFY(page->getVisibleVideoCards().size() <= 12);
        QVERIFY(fixture.currentTab()->getHistory().getCurrentEntry()->route.isValid());
    }
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>(), page.data());

    // The route of the history opens the same filter again
    const pdf::PDFAppRoute route = fixture.currentTab()->getHistory().getCurrentEntry()->route;
    fixture.navigator()->navigate(route, pdf::PDFAppNavigationMode::Replace);
    InfoFixture::settle();
    QVERIFY(fixture.page<pdf::PDFAppSupportPage>());
    QCOMPARE(fixture.page<pdf::PDFAppSupportPage>()->getSearchText(), route.getQueryValue(QStringLiteral("q")));
}

void AppInfoPagesTest::infoPagePartsDeleted()
{
    // Parts of the pages deleted by the application: the pages forget them and
    // keep working with the rest
    pdf::PDFAppSupportPage support(nullptr);
    QVERIFY(!support.getChip(QStringLiteral("bogus")));
    QVERIFY(!support.getVideoCard(QStringLiteral("bogus")));
    delete support.getQuickActions().front();
    QCOMPARE(support.getQuickActions().size(), 3);
    delete support.getChip(QStringLiteral("for-you"));
    QCOMPARE(support.getChips().size(), 8);
    delete support.getVideoCard(QStringLiteral("ocr-made-easy"));
    QCOMPARE(support.getVideoCards().size(), 11);
    support.refreshUsedTools();

    // A chip after the deleted one, a button of another group
    support.getChip(QStringLiteral("pages"))->click();
    QCOMPARE(support.getTopic(), QStringLiteral("pages"));
    QButtonGroup* group = support.findChild<QButtonGroup*>();
    QVERIFY(group);
    QPushButton foreign;
    Q_EMIT group->buttonClicked(&foreign);
    QCOMPARE(support.getTopic(), QStringLiteral("pages"));

    support.setFilter(QStringLiteral("convert"), QString());
    QCOMPARE(support.getVisibleVideoCards().size(), 1);
    delete support.getSearchField();
    delete support.getEmptyText();
    support.setFilter(QStringLiteral("for-you"), QStringLiteral("merge"));
    QCOMPARE(support.getSearchText(), QStringLiteral("merge"));
    delete support.getVideoGrid();
    QVERIFY(support.getVideoCards().isEmpty());
    QVERIFY(support.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/support?topic=pages&q=rotate"))));
    QCOMPARE(support.getTopic(), QStringLiteral("pages"));
    QCOMPARE(support.getSearchText(), QStringLiteral("rotate"));

    pdf::PDFAppAboutPage about(nullptr);
    QCOMPARE(about.getLinkRows().size(), 5);
    delete about.getLinkRows().front();
    QCOMPARE(about.getLinkRows().size(), 4);

    pdf::PDFAppSponsorPage sponsor(nullptr);
    QCOMPARE(sponsor.getLinkRows().size(), 6);
    delete sponsor.getHighlightCards().front();
    delete sponsor.getLinkRows().front();
    delete sponsor.getPriorityRows().front();
    QCOMPARE(sponsor.getHighlightCards().size(), 3);
    QCOMPARE(sponsor.getLinkRows().size(), 5);
    QCOMPARE(sponsor.getPriorityRows().size(), 4);
}

void AppInfoPagesTest::sponsorLinks()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* page = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(page);
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    QTest::mouseClick(page->getSponsorButton(), Qt::LeftButton);
    QTest::mouseClick(page->getStarButton(), Qt::LeftButton);
    QTest::mouseClick(page->getStoreButton(), Qt::LeftButton);
    QTest::mouseClick(page->getOneTimeLink(), Qt::LeftButton);
    for (pdf::PDFLinkRow* row : page->getLinkRows())
    {
        QTest::mouseClick(row, Qt::LeftButton);
    }

    const QList<QUrl> expected = { pdf::PDFAppProjectLinks::getSponsors(), pdf::PDFAppProjectLinks::getRepository(), pdf::PDFAppProjectLinks::getMicrosoftStore(),
                                   pdf::PDFAppProjectLinks::getOneTimeSponsorship(), pdf::PDFAppProjectLinks::getSponsors(), pdf::PDFAppProjectLinks::getRepository(),
                                   pdf::PDFAppProjectLinks::getMicrosoftStore(), pdf::PDFAppProjectLinks::getWebsite(), pdf::PDFAppProjectLinks::getNewIssue(),
                                   pdf::PDFAppProjectLinks::getPullRequests() };
    QCOMPARE(external.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i)
    {
        QCOMPARE(external[i].at(0).toUrl(), expected[i]);
    }

    QCOMPARE(page->getHighlightCards().size(), 4);
    QCOMPARE(page->getPriorityRows().size(), 5);
    QVERIFY(page->getPriorityRows()[0]->isActive());
    QVERIFY(!page->getPriorityRows()[4]->isActive());
    QCOMPARE(page->getPriorityRows()[0]->getStatus(), QStringLiteral("In progress"));
    QVERIFY(page->getPriorityRows()[0]->focusPolicy() == Qt::NoFocus);
}

void AppInfoPagesTest::geometryAbout()
{
    // pdf4qt-about 110:2163 at 1440 x 1067: the page is 1166 wide
    InfoFixture fixture;
    fixture.hideBanner();
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QCOMPARE(page->width(), 1166);

    pdf::PDFAppColumns* columns = page->getColumns();
    QVERIFY(!columns->isStacked());
    QCOMPARE(columns->getMainColumn()->width(), 802);
    QCOMPARE(columns->getSideColumn()->width(), 340);
    QCOMPARE(columns->getSideColumn()->x(), 826);

    // Cards of the main column: app-card 202, author at 226 (card 202), libraries at 482 (card 303)
    QCOMPARE(page->getAppCard()->geometry(), QRect(0, 0, 802, 202));
    QCOMPARE(page->getAuthorSection()->y(), 226);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("authorCard"))->height(), 202);
    QCOMPARE(page->getLibrariesSection()->y(), 482);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("librariesCard"))->height(), 303);

    // logo-box 88 at 21 / 21
    QWidget* logo = page->findChild<QWidget*>(QStringLiteral("logoBox"));
    QCOMPARE(InfoTestHelper::rectIn(logo, page->getAppCard()), QRect(21, 21, 88, 88));

    // Side column: build information (rows of 28 and the button; 288 with the 8 rows of
    // the design, the build date and the commit are shown only when the build has them),
    // license 145, links 276, gaps 24
    const int rowCount = page->getBuildInfo()->getRowCount();
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("buildCard"))->height(), 9 + rowCount * 28 + 46 + 9);
    QCOMPARE(page->getBuildInfo()->getRowRect(0).height(), 28);
    QWidget* buildSection = page->findChild<QWidget*>(QStringLiteral("buildSection"));
    QCOMPARE(buildSection->height(), 18 + 12 + 9 + rowCount * 28 + 46 + 9);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("licenseSection"))->y(), buildSection->height() + 24);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("licenseCard"))->height(), 145);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("linksSection"))->y(), buildSection->height() + 24 + 175 + 24);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("linksCard"))->height(), 276);

    // Library rows of 30 px, the fixed columns of the design
    pdf::PDFAppLibraryView* view = page->getLibraryView();
    QCOMPARE(view->visualRect(view->model()->index(0, 0)).height(), 30);
    QCOMPARE(view->header()->sectionSize(0), pdf::PDFAppLibraryView::LIBRARY_WIDTH);
    QVERIFY(!view->isColumnHidden(2));

    // columns 111:2291 and Library row 110:2158 (review R2-D2): the texts of the cells
    // (padding 10) at 10 / 172 / 274 from the row; the header has no band and no
    // separators, its labels lie 6 px below its top, the divider border/divider at 27
    // and the gap of 4 px before the first row
    QCOMPARE(view->header()->sectionPosition(1) + 10, 172);
    QCOMPARE(view->header()->sectionPosition(2) + 10, 274);
    QCOMPARE(view->header()->height(), 32);
    QCOMPARE(view->visualRect(view->model()->index(0, 0)).top(), 0);
    QCOMPARE(view->viewport()->mapTo(view, QPoint(0, 0)).y(), view->header()->mapTo(view, QPoint(0, 32)).y());
    QStyleOptionHeader headerOption;
    headerOption.initFrom(view->header());
    headerOption.orientation = Qt::Horizontal;
    headerOption.section = 1;
    headerOption.text = view->model()->headerData(1, Qt::Horizontal).toString();
    headerOption.rect = QRect(view->header()->sectionViewportPosition(1), 0, view->header()->sectionSize(1), view->header()->height());
    const QRect label = view->style()->subElementRect(QStyle::SE_HeaderLabel, &headerOption, view->header());
    QCOMPARE(label.left(), view->header()->sectionViewportPosition(1) + 10);
    QCOMPARE(label.top(), 6);
    QCOMPARE(label.height(), 13);
    const QImage headerImage = view->header()->grab().toImage();
    const QColor surface = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
    const int separatorX = view->header()->sectionViewportPosition(1) - 1;
    QCOMPARE(headerImage.pixelColor(separatorX, 13), surface);
    QCOMPARE(headerImage.pixelColor(separatorX, 3), surface);
    QCOMPARE(headerImage.pixelColor(separatorX, 27), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BorderDivider));
    QCOMPARE(headerImage.pixelColor(separatorX, 30), surface);

    // title-row 111:2406: the title of the license (17 px) centered by the icon circle 32
    QWidget* licenseCard = page->findChild<QWidget*>(QStringLiteral("licenseCard"));
    const QRect licenseTitle = InfoTestHelper::rectIn(page->findChild<QWidget*>(QStringLiteral("licenseTitle")), licenseCard);
    QCOMPARE(licenseTitle.height(), 17);
    QVERIFY2(qAbs(licenseTitle.top() - 22) <= 1, qPrintable(QString::number(licenseTitle.top())));
}

void AppInfoPagesTest::geometrySupport()
{
    // pdf4qt-support 116:2419: four tiles of 282.5 (gap 12), the tutorials card
    InfoFixture fixture;
    fixture.hideBanner();
    fixture.window->resize(1440, 1038);
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    // The page scrolls (twelve cards), the vertical scroll bar takes no width of the page
    const int pageWidth = page->width();
    QVERIFY(pageWidth <= 1166 && pageWidth >= 1150);

    const QList<pdf::PDFQuickActionTile*> tiles = page->getQuickActions();
    QCOMPARE(tiles.size(), 4);
    QCOMPARE(tiles[0]->y(), tiles[3]->y());
    QVERIFY(qAbs(tiles[0]->width() - (pageWidth - 36) / 4) <= 1);
    QCOMPARE(tiles[0]->height(), 88);
    QCOMPARE(page->getQuickActionGrid()->getColumnCount(pageWidth), 4);

    // tutorials: header at 17 / 17, chips 32 high, cards in rows of four with gaps 16
    QWidget* tutorials = page->findChild<QWidget*>(QStringLiteral("tutorials"));
    QCOMPARE(tutorials->y(), 112);
    QCOMPARE(page->getSearchField()->width(), 280);
    QCOMPARE(page->getSearchField()->height(), 36);
    QCOMPARE(page->getChannelButton()->height(), 36);
    QPushButton* all = page->getChip(QString());
    QCOMPARE(all->height(), 32);
    QCOMPARE(InfoTestHelper::rectIn(all, tutorials).topLeft(), QPoint(17, 67));

    const QList<pdf::PDFVideoCard*> cards = page->getVisibleVideoCards();
    QCOMPARE(cards.size(), 12);
    QCOMPARE(cards[0]->y(), cards[3]->y());
    QVERIFY(cards[4]->y() > cards[0]->y());
    QCOMPARE(cards[1]->x() - cards[0]->x() - cards[0]->width(), 16);
    QCOMPARE(InfoTestHelper::rectIn(page->getVideoGrid(), tutorials).topLeft(), QPoint(17, 113));
    QCOMPARE(cards[0]->height(), cards[1]->height());
    QVERIFY(cards[0]->height() >= 230);
}

void AppInfoPagesTest::geometrySponsor()
{
    // pdf4qt-sponsor 122:2652 at 1440 x 1172 (the page does not scroll)
    InfoFixture fixture;
    fixture.hideBanner();
    fixture.window->resize(1440, 1172);
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* page = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(page);
    QCOMPARE(page->width(), 1166);

    // hero 208, the heart circle 72 at 23 / 23, the grid at 232
    QCOMPARE(page->getHero()->geometry(), QRect(0, 0, 1166, 208));
    QCOMPARE(InfoTestHelper::rectIn(page->findChild<QWidget*>(QStringLiteral("heartBadge")), page->getHero()), QRect(23, 23, 72, 72));
    QCOMPARE(page->getColumns()->y(), 232);
    QCOMPARE(page->getColumns()->getSideColumn()->width(), 340);

    // Highlight cards 395 x 137 in two rows, gap 12
    const QList<pdf::PDFHighlightCard*> cards = page->getHighlightCards();
    QCOMPARE(cards.size(), 4);
    QCOMPARE(cards[0]->width(), 395);
    QCOMPARE(cards[0]->height(), 137);
    QCOMPARE(cards[2]->y() - cards[0]->y(), 149);

    // Ways to help: six rows of 50 with gaps 2 (card 328), priorities 46 px
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("waysCard"))->height(), 328);
    QCOMPARE(page->getPriorityRows()[0]->height(), 46);
    QCOMPARE(page->getPriorityRows()[0]->width(), 322);
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("priorities"))->y(), 300);
    QCOMPARE(page->getPromise()->y(), 602);
}

void AppInfoPagesTest::pixels_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("devicePixelRatio");

    QTest::newRow("light 1x") << false << 1.0;
    QTest::newRow("light 1.5x") << false << 1.5;
    QTest::newRow("light 2x") << false << 2.0;
    QTest::newRow("dark 1x") << true << 1.0;
    QTest::newRow("dark 1.5x") << true << 1.5;
    QTest::newRow("dark 2x") << true << 2.0;
}

void AppInfoPagesTest::pixels()
{
    QFETCH(bool, isDark);
    QFETCH(qreal, devicePixelRatio);

    using Helper = InfoTestHelper;
    using Color = pdf::PDFThemeColors;
    using Hue = pdf::PDFAccentHue;
    using Role = pdf::PDFAccentHueColors::Role;

    InfoFixture fixture;
    fixture.window->resize(1440, 1172);
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? Color::Scheme::Dark : Color::Scheme::Light);

    // About: app-card accent/blue, its border, logo-box bg/surface, author card bg/surface, avatar accent/blue
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* about = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(about);
    QImage image = Helper::render(about, devicePixelRatio);
    QCOMPARE(Helper::countLeakPixels(image), 0);
    const QRect appCard = about->getAppCard()->geometry();
    QVERIFY(Helper::isClose(Helper::pixel(image, QPoint(appCard.center().x(), appCard.top() + 8)), Helper::accent(Hue::Blue, Role::Background)));
    QVERIFY(Helper::isClose(Helper::pixel(image, QPoint(appCard.center().x(), appCard.top())), Helper::accent(Hue::Blue, Role::Border), 12));
    QWidget* logo = about->findChild<QWidget*>(QStringLiteral("logoBox"));
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(logo, about).topLeft() + QPoint(44, 5)), Helper::token(Color::BgSurface)));
    QWidget* authorCard = about->findChild<QWidget*>(QStringLiteral("authorCard"));
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(authorCard, about).topLeft() + QPoint(400, 8)), Helper::token(Color::BgSurface)));
    pdf::PDFAvatar* avatar = about->findChild<pdf::PDFAvatar*>();
    QVERIFY(avatar);
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(avatar, about).topLeft() + QPoint(28, 50)), Helper::accent(Hue::Blue, Role::Background)));
    pdf::PDFFactPill* pill = about->findChild<pdf::PDFFactPill*>();
    QVERIFY(pill);
    QCOMPARE(pill->height(), 25);
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(pill, about).topLeft() + QPoint(pill->width() / 2, 2)), Helper::token(Color::BgSurface)));

    // Sponsor: hero accent/blue, promise accent/green, the dots of the priorities
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* sponsor = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(sponsor);
    image = Helper::render(sponsor, devicePixelRatio);
    QCOMPARE(Helper::countLeakPixels(image), 0);
    QVERIFY(Helper::isClose(Helper::pixel(image, sponsor->getHero()->geometry().topLeft() + QPoint(600, 8)), Helper::accent(Hue::Blue, Role::Background)));
    const QRect promise = Helper::rectIn(sponsor->getPromise(), sponsor);
    QVERIFY(Helper::isClose(Helper::pixel(image, promise.topLeft() + QPoint(promise.width() / 2, 6)), Helper::accent(Hue::Green, Role::Background)));
    // The dot 8 of a status row lies 10 px from the leading edge, centered vertically
    pdf::PDFStatusRow* active = sponsor->getPriorityRows()[0];
    pdf::PDFStatusRow* planned = sponsor->getPriorityRows()[4];
    QVERIFY(active->isActive());
    QVERIFY(!planned->isActive());
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(active, sponsor).topLeft() + QPoint(14, active->height() / 2)), Helper::token(Color::BgAccent), 12));
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(planned, sponsor).topLeft() + QPoint(14, planned->height() / 2)), Helper::token(Color::BorderStrong), 12));

    // Support: the tutorials card bg/surface, the selected chip is not the full accent
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    image = Helper::render(support, devicePixelRatio);
    QCOMPARE(Helper::countLeakPixels(image), 0);
    QWidget* tutorials = support->findChild<QWidget*>(QStringLiteral("tutorials"));
    QVERIFY(Helper::isClose(Helper::pixel(image, Helper::rectIn(tutorials, support).topLeft() + QPoint(500, 8)), Helper::token(Color::BgSurface)));
    QPushButton* all = support->getChip(QString());
    QVERIFY(!Helper::isClose(Helper::pixel(image, Helper::rectIn(all, support).topLeft() + QPoint(4, 16)), Helper::token(Color::BgAccent)));
}

void AppInfoPagesTest::themeChange()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);

    const QPoint probe(page->getAppCard()->width() / 2, 8);
    QImage light = InfoTestHelper::render(page->getAppCard());
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    InfoFixture::settle();
    QImage dark = InfoTestHelper::render(page->getAppCard());
    QVERIFY(InfoTestHelper::isClose(InfoTestHelper::pixel(dark, probe), InfoTestHelper::accent(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Background)));
    QVERIFY(InfoTestHelper::pixel(light, probe) != InfoTestHelper::pixel(dark, probe));
    QCOMPARE(InfoTestHelper::countLeakPixels(InfoTestHelper::render(fixture.window)), 0);
}

void AppInfoPagesTest::rightToLeft()
{
    InfoFixture fixture;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* about = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(about);
    InfoFixture::settle();

    // The side column is on the left, the logo on the right of the app card
    QVERIFY(about->getColumns()->getSideColumn()->x() < about->getColumns()->getMainColumn()->x());
    QWidget* logo = about->findChild<QWidget*>(QStringLiteral("logoBox"));
    QCOMPARE(InfoTestHelper::rectIn(logo, about->getAppCard()).right(), about->getAppCard()->width() - 22);

    // The icon of a fact pill (13 px, 9 px from the leading edge) is at its right edge
    pdf::PDFFactPill* pill = about->findChild<pdf::PDFFactPill*>();
    QCOMPARE(pill->layoutDirection(), Qt::RightToLeft);
    const QImage pillImage = InfoTestHelper::render(pill);
    const QColor pillSurface = InfoTestHelper::token(pdf::PDFThemeColors::BgSurface);
    int rightInk = 0;
    int leftInk = 0;
    for (int y = 6; y < 19; ++y)
    {
        for (int x = 0; x < 13; ++x)
        {
            rightInk += InfoTestHelper::isClose(InfoTestHelper::pixel(pillImage, QPoint(pill->width() - 9 - 13 + x, y)), pillSurface, 20) ? 0 : 1;
            leftInk += InfoTestHelper::isClose(InfoTestHelper::pixel(pillImage, QPoint(9 + x, y)), pillSurface, 20) ? 0 : 1;
        }
    }
    QVERIFY(rightInk > 10);
    QVERIFY(rightInk != leftInk);

    // Sponsor: the dot of a priority on the right, the status on the left
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* sponsor = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(sponsor);
    InfoFixture::settle();
    pdf::PDFStatusRow* row = sponsor->getPriorityRows()[0];
    QCOMPARE(row->layoutDirection(), Qt::RightToLeft);
    const QImage rowImage = InfoTestHelper::render(row);
    QVERIFY(InfoTestHelper::isClose(InfoTestHelper::pixel(rowImage, QPoint(row->width() - 15, row->height() / 2)), InfoTestHelper::token(pdf::PDFThemeColors::BgAccent), 12));
    QVERIFY(!InfoTestHelper::isClose(InfoTestHelper::pixel(rowImage, QPoint(14, row->height() / 2)), InfoTestHelper::token(pdf::PDFThemeColors::BgAccent), 12));

    // Support: the cards fill the grid from the right
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    InfoFixture::settle();
    const QList<pdf::PDFVideoCard*> cards = support->getVisibleVideoCards();
    QVERIFY(cards[0]->x() > cards[1]->x());
    QCOMPARE(InfoTestHelper::countLeakPixels(InfoTestHelper::render(fixture.window)), 0);
}

void AppInfoPagesTest::minimalWindow()
{
    InfoFixture fixture;
    fixture.window->resize(720, 480);
    InfoFixture::settle();

    for (const QString& route : { QStringLiteral("/about"), QStringLiteral("/support"), QStringLiteral("/sponsor") })
    {
        fixture.navigate(route);
        pdf::PDFAppPage* page = fixture.currentPage();
        QVERIFY(page);
        QScrollArea* scrollArea = fixture.homeTab()->getPageScrollArea();

        // No horizontal scroll bar: every part fits the width of the page
        QCOMPARE(scrollArea->horizontalScrollBar()->maximum(), 0);
        QVERIFY(page->width() <= scrollArea->viewport()->width());
        for (QWidget* child : page->findChildren<QWidget*>())
        {
            if (child->isVisibleTo(page) && !qobject_cast<QScrollBar*>(child) && !child->inherits("QHeaderView") && child->parentWidget() &&
                !qobject_cast<QAbstractScrollArea*>(child->parentWidget()) && child->parentWidget()->objectName() != QStringLiteral("qt_scrollarea_viewport"))
            {
                const QRect rect = InfoTestHelper::rectIn(child, page);
                QVERIFY2(rect.right() <= page->width(), qPrintable(route + QStringLiteral(" ") + QString::fromLatin1(child->metaObject()->className()) + QStringLiteral(" ") + child->objectName()));
            }
        }

        if (pdf::PDFAppAboutPage* about = qobject_cast<pdf::PDFAppAboutPage*>(page))
        {
            QVERIFY(about->getColumns()->isStacked());
            QVERIFY(about->getColumns()->getSideColumn()->y() > about->getColumns()->getMainColumn()->y());
            QVERIFY(about->getLibraryView()->isColumnHidden(2));
        }
        if (pdf::PDFAppSupportPage* support = qobject_cast<pdf::PDFAppSupportPage*>(page))
        {
            QCOMPARE(support->getVideoGrid()->getColumnCount(support->getVideoGrid()->width()), 1);
            const QList<pdf::PDFVideoCard*> cards = support->getVisibleVideoCards();
            QCOMPARE(cards[0]->x(), cards[1]->x());
        }
        if (pdf::PDFAppSponsorPage* sponsor = qobject_cast<pdf::PDFAppSponsorPage*>(page))
        {
            QVERIFY(sponsor->getColumns()->isStacked());
            QCOMPARE(sponsor->getHighlightCards()[0]->x(), sponsor->getHighlightCards()[1]->x());
        }
    }
}

void AppInfoPagesTest::keyboard()
{
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* about = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(about);
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    // Tab order of the page: website, GitHub, profile, sponsor, notices, the table,
    // copy, the license, the links (the order of the design)
    QList<QWidget*> expected = { about->getWebsiteButton(), about->getGitHubButton(), about->getProfileButton(), about->getSponsorButton(),
                                 about->getNoticesLink(), about->getLibraryView(), about->getCopyButton(), about->getLicenseLink() };
    for (pdf::PDFLinkRow* row : about->getLinkRows())
    {
        expected << row;
    }
    about->getWebsiteButton()->setFocus(Qt::TabFocusReason);
    QList<QWidget*> order;
    QWidget* widget = about->getWebsiteButton();
    for (int i = 0; i < 60 && order.size() < expected.size(); ++i)
    {
        if (expected.contains(widget) && !order.contains(widget))
        {
            order << widget;
        }
        widget = widget->nextInFocusChain();
    }
    QCOMPARE(order, expected);

    // Enter and Space activate a link row
    pdf::PDFLinkRow* row = about->getLinkRows()[2];
    row->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row, Qt::Key_Space);
    QTest::keyClick(row, Qt::Key_Return);
    QCOMPARE(external.size(), 2);

    // Support: a chip by the keyboard
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    QPushButton* review = support->getChip(QStringLiteral("review"));
    review->setFocus(Qt::TabFocusReason);
    QTest::keyClick(review, Qt::Key_Space);
    InfoFixture::settle();
    QCOMPARE(support->getTopic(), QStringLiteral("review"));
    QVERIFY(review->hasFocus());
}

void AppInfoPagesTest::accessibleNames()
{
    InfoFixture fixture;
    for (const QString& route : { QStringLiteral("/about"), QStringLiteral("/support"), QStringLiteral("/sponsor") })
    {
        fixture.navigate(route);
        pdf::PDFAppPage* page = fixture.currentPage();
        QVERIFY(page);
        for (QWidget* widget : page->findChildren<QWidget*>())
        {
            if (!widget->isVisibleTo(page))
            {
                continue;
            }

            // Every control the keyboard reaches has a name
            const bool isControl = qobject_cast<QAbstractButton*>(widget) || qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractItemView*>(widget);
            if (isControl && widget->focusPolicy() != Qt::NoFocus)
            {
                QVERIFY2(!InfoTestHelper::accessibleName(widget).isEmpty(), qPrintable(route + QStringLiteral(" ") + QString::fromLatin1(widget->metaObject()->className()) + QStringLiteral(" ") + widget->objectName()));
            }
        }
    }

    // The facts and the priorities are announced as texts
    fixture.navigate(QStringLiteral("/sponsor"));
    pdf::PDFAppSponsorPage* sponsor = fixture.page<pdf::PDFAppSponsorPage>();
    QVERIFY(sponsor);
    QCOMPARE(InfoTestHelper::accessibleName(sponsor->getPriorityRows()[0]), QStringLiteral("Unified application"));
    QVERIFY(sponsor->getPriorityRows()[0]->accessibleDescription().contains(QStringLiteral("In progress")));
    QCOMPARE(InfoTestHelper::accessibleName(sponsor->findChild<pdf::PDFFactPill*>()), QStringLiteral("Free since 2018"));
}

void AppInfoPagesTest::deleteWithOpenMenu()
{
    // The context menu of a build information row is open, the page is deleted
    // by a navigation (the menu is a child of the list, deleted with it)
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    pdf::PDFAppAboutPage* page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QPointer<pdf::PDFKeyValueList> list = page->getBuildInfo();

    bool isMenuSeen = false;
    QTimer::singleShot(50, fixture.window.data(), [&fixture, &isMenuSeen]()
    {
        isMenuSeen = qobject_cast<QMenu*>(QApplication::activePopupWidget()) != nullptr;
        fixture.navigate(QStringLiteral("/sponsor"));
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            popup->close();
        }
    });
    const QPoint position = list->getRowRect(0).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, list->mapToGlobal(position));
    QApplication::sendEvent(list.data(), &event);
    InfoFixture::settle();
    QVERIFY(isMenuSeen);
    QVERIFY(list.isNull());
    QVERIFY(fixture.page<pdf::PDFAppSponsorPage>());
}

void AppInfoPagesTest::deleteWithPendingNotification()
{
    // A change of tool_usage arrives after the page has gone
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support"));
    QPointer<pdf::PDFAppSupportPage> page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);

    // The notifier does not poll until the page has gone (the notification really
    // arrives after the page), then the test waits for the notification
    pdf::PDFDatabaseNotifier* notifier = fixture.context->getNotifier();
    QSignalSpy changed(notifier, &pdf::PDFDatabaseNotifier::changed);
    notifier->setPollInterval(3600 * 1000);
    QVERIFY(InfoTestHelper::insertUsageRowInThread(fixture.context->getDatabase(), QStringLiteral("review.compare")));
    fixture.navigate(QStringLiteral("/about"));
    QVERIFY(page.isNull());
    QCOMPARE(InfoTestHelper::countUsageNotifications(changed), 0);
    notifier->setPollInterval(20);
    QTRY_VERIFY_WITH_TIMEOUT(InfoTestHelper::countUsageNotifications(changed) > 0, 10000);
    QVERIFY(fixture.page<pdf::PDFAppAboutPage>());

    // The page deleted directly while the notification is pending
    fixture.navigate(QStringLiteral("/support"));
    page = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(page);
    changed.clear();
    notifier->setPollInterval(3600 * 1000);
    QVERIFY(InfoTestHelper::insertUsageRowInThread(fixture.context->getDatabase(), QStringLiteral("review.measure")));
    delete page.data();
    QCOMPARE(InfoTestHelper::countUsageNotifications(changed), 0);
    notifier->setPollInterval(20);
    QTRY_VERIFY_WITH_TIMEOUT(InfoTestHelper::countUsageNotifications(changed) > 0, 10000);

    // The storage state changes after the About page has gone
    fixture.navigate(QStringLiteral("/about"));
    QPointer<pdf::PDFAppAboutPage> about = fixture.page<pdf::PDFAppAboutPage>();
    delete about.data();
    Q_EMIT fixture.context->storageStateChanged();
    InfoFixture::settle();
}

void AppInfoPagesTest::deleteWithPendingScroll()
{
    // The anchor and the restored position are applied later: the page goes before
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about#third-party"));
    QPointer<pdf::PDFAppAboutPage> page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    page->restoreViewState({ { QStringLiteral("scroll"), 50 } });
    delete page.data();
    InfoFixture::settle();

    // A page outside of a tab has no scroll area
    pdf::PDFAppAboutPage standalone(fixture.context.get());
    QVERIFY(!standalone.getScrollArea());
    QVERIFY(!standalone.scrollToWidget(standalone.getLibrariesSection()));
    QVERIFY(standalone.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/about#third-party"))));
    standalone.restoreViewState({ { QStringLiteral("scroll"), 10 } });
    standalone.show();
    InfoFixture::settle();
    QCOMPARE(standalone.saveViewState(), QVariantMap());
}

void AppInfoPagesTest::deleteFromOwnLink()
{
    // A link navigates away from the page (the page is deleted later by the tab)
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/about"));
    QPointer<pdf::PDFAppAboutPage> page = fixture.page<pdf::PDFAppAboutPage>();
    QVERIFY(page);
    QTest::mouseClick(page->getSponsorButton(), Qt::LeftButton);
    InfoFixture::settle();
    QVERIFY(page.isNull());

    // The tab is closed / the window deleted from the slot of a click of the page
    fixture.navigate(QStringLiteral("/support"));
    QPointer<pdf::PDFAppSupportPage> support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    QObject::connect(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
    QTest::mouseClick(support->getChannelButton(), Qt::LeftButton);
    InfoFixture::settle();
    QVERIFY(fixture.window.isNull());
    QVERIFY(support.isNull());
}

void AppInfoPagesTest::deleteWindow()
{
    for (const QString& route : { QStringLiteral("/about#third-party"), QStringLiteral("/support?topic=for-you"), QStringLiteral("/sponsor") })
    {
        InfoFixture fixture;
        fixture.navigate(route);
        QPointer<pdf::PDFAppPage> page = fixture.currentPage();
        QVERIFY(page);
        delete fixture.window.data();
        QVERIFY(page.isNull());
        InfoFixture::settle();
    }

    // The context goes before the page (the connections to it are cut)
    InfoFixture fixture;
    fixture.navigate(QStringLiteral("/support"));
    std::unique_ptr<pdf::PDFAppSupportPage> page = std::make_unique<pdf::PDFAppSupportPage>(fixture.context.get());
    delete fixture.window.data();
    fixture.context.reset();
    page->refreshUsedTools();
    QCOMPARE(page->getUsedToolIds().size(), 0);
    page.reset();
}

void AppInfoPagesTest::flowLayout()
{
    QWidget widget;
    pdf::PDFAppFlowLayout* layout = new pdf::PDFAppFlowLayout(&widget, 8, 6);
    QList<QWidget*> items;
    for (int i = 0; i < 5; ++i)
    {
        QWidget* item = new QWidget(&widget);
        item->setFixedSize(100, 20 + i);
        layout->addWidget(item);
        items << item;
    }

    QCOMPARE(layout->count(), 5);
    QCOMPARE(layout->sizeHint(), QSize(5 * 100 + 4 * 8, 24));
    QCOMPARE(layout->minimumSize(), QSize(100, 24));
    QCOMPARE(layout->getRowCount(1000), 1);
    QCOMPARE(layout->getRowCount(216), 3);
    QCOMPARE(layout->getRowCount(50), 5);
    QCOMPARE(layout->heightForWidth(216), 21 + 6 + 23 + 6 + 24);
    QVERIFY(layout->hasHeightForWidth());
    QVERIFY(!layout->itemAt(-1));
    QVERIFY(!layout->itemAt(5));
    QVERIFY(!layout->takeAt(9));

    // Right-to-left: the first item at the right edge, items centered vertically in the row
    widget.setLayoutDirection(Qt::RightToLeft);
    widget.resize(216, layout->heightForWidth(216));
    layout->setGeometry(widget.rect());
    QCOMPARE(items[0]->geometry().right(), 215);
    QCOMPARE(items[1]->geometry().right(), 215 - 108);
    QCOMPARE(items[0]->y(), 0);
    QCOMPARE(items[2]->y(), 21 + 6);

    // Hidden items take no place
    items[1]->hide();
    QCOMPARE(layout->sizeHint().width(), 4 * 100 + 3 * 8);
    delete layout->takeAt(0);
    QCOMPARE(layout->count(), 4);
}

void AppInfoPagesTest::flowLayoutEdges()
{
    QWidget widget;
    pdf::PDFAppFlowLayout* layout = new pdf::PDFAppFlowLayout(&widget, 8, 6);
    layout->addItem(nullptr);
    QCOMPARE(layout->count(), 0);
    QVERIFY(!layout->takeAt(-1));
    QCOMPARE(layout->expandingDirections(), Qt::Orientations());
    QCOMPARE(layout->sizeHint(), QSize(0, 0));
    QCOMPARE(layout->heightForWidth(100), 0);
    QCOMPARE(layout->getRowCount(100), 0);

    // A hidden item takes no place in the rows and in the minimal size
    QWidget* wide = new QWidget(&widget);
    wide->setFixedSize(300, 40);
    QWidget* small = new QWidget(&widget);
    small->setFixedSize(50, 10);
    layout->addWidget(wide);
    layout->addWidget(small);
    QCOMPARE(layout->minimumSize(), QSize(300, 40));
    QCOMPARE(layout->getRowCount(320), 2);
    wide->hide();
    QCOMPARE(layout->minimumSize(), QSize(50, 10));
    QCOMPARE(layout->getRowCount(60), 1);
    QCOMPARE(layout->heightForWidth(60), 10);

    // Margins are a part of every size
    layout->setContentsMargins(1, 2, 3, 4);
    QCOMPARE(layout->minimumSize(), QSize(54, 16));
    QCOMPARE(layout->sizeHint(), QSize(54, 16));
    QCOMPARE(layout->heightForWidth(60), 16);
    layout->setContentsMargins(0, 0, 0, 0);

    // An item of no width does not start a new row
    wide->show();
    wide->setFixedSize(0, 12);
    QCOMPARE(layout->getRowCount(10), 1);

    // An item wider than the area is cut to it (a widget without a fixed width)
    QPushButton* button = new QPushButton(QString(80, QLatin1Char('x')), &widget);
    layout->addWidget(button);
    small->hide();
    QVERIFY(button->sizeHint().width() > 200);
    layout->setGeometry(QRect(0, 0, 200, 100));
    QCOMPARE(button->width(), 200);
    QCOMPARE(layout->getRowCount(200), 1);

    // A layout without a parent widget is laid out from left to right
    QWidget host;
    host.setLayoutDirection(Qt::RightToLeft);
    QWidget* first = new QWidget(&host);
    first->setFixedSize(40, 10);
    QWidget* second = new QWidget(&host);
    second->setFixedSize(40, 20);
    std::unique_ptr<pdf::PDFAppFlowLayout> orphan = std::make_unique<pdf::PDFAppFlowLayout>(nullptr, 5, 3);
    orphan->addItem(new QWidgetItem(first));
    orphan->addItem(new QWidgetItem(second));
    QVERIFY(!orphan->parentWidget());
    QCOMPARE(orphan->heightForWidth(60), 10 + 3 + 20);
    orphan->setGeometry(QRect(0, 0, 100, 40));
    QCOMPARE(first->geometry(), QRect(0, 5, 40, 10));
    QCOMPARE(second->geometry(), QRect(45, 0, 40, 20));
}

void AppInfoPagesTest::responsiveRow()
{
    // The trailing part below the leading part in a row narrower than the threshold
    QWidget* leading = new QWidget();
    leading->setMinimumSize(50, 20);
    QWidget* trailing = new QWidget();
    trailing->setMinimumSize(50, 20);
    pdf::PDFAppResponsiveRow row(leading, trailing, 300, 10);
    QCOMPARE(leading->parentWidget(), &row);
    QCOMPARE(trailing->parentWidget(), &row);
    row.resize(400, 60);
    row.show();
    QCoreApplication::processEvents();
    QVERIFY(!row.isStacked());
    QCOMPARE(leading->y(), trailing->y());
    row.resize(299, 60);
    QCoreApplication::processEvents();
    QVERIFY(row.isStacked());
    QVERIFY(trailing->y() >= leading->geometry().bottom() + 10);
    row.resize(300, 60);
    QCoreApplication::processEvents();
    QVERIFY(!row.isStacked());

    // No parts, a negative threshold and spacing
    pdf::PDFAppResponsiveRow empty(nullptr, nullptr, -5, -5);
    empty.resize(0, 0);
    empty.show();
    QCoreApplication::processEvents();
    QVERIFY(!empty.isStacked());
    QVERIFY(empty.findChildren<QWidget*>().isEmpty());

    // The layout deleted by the application: the arrangement stays
    delete row.layout();
    row.resize(100, 60);
    QCoreApplication::processEvents();
    QVERIFY(!row.isStacked());
}

void AppInfoPagesTest::grid()
{
    QWidget parent;
    pdf::PDFAppGrid* grid = new pdf::PDFAppGrid(200, 3, 10, 12, &parent);
    QVERIFY(grid->hasHeightForWidth());
    QCOMPARE(grid->getColumnCount(0), 1);
    QCOMPARE(grid->getColumnCount(409), 1);
    QCOMPARE(grid->getColumnCount(410), 2);
    QCOMPARE(grid->getColumnCount(10000), 3);
    QCOMPARE(grid->heightForWidth(600), 0);
    grid->addWidget(nullptr);

    QList<QWidget*> cells;
    for (int i = 0; i < 5; ++i)
    {
        QWidget* cell = new QWidget();
        cell->setMinimumHeight(10 * (i + 1));
        grid->addWidget(cell);
        cells << cell;
    }

    // Rows of three, the cells of a row have the height of the highest one
    QCOMPARE(grid->heightForWidth(620), 30 + 12 + 50);
    grid->resize(620, grid->heightForWidth(620));
    grid->updateCells();
    QCOMPARE(cells[0]->geometry(), QRect(0, 0, 200, 30));
    QCOMPARE(cells[2]->geometry(), QRect(420, 0, 200, 30));
    QCOMPARE(cells[3]->geometry(), QRect(0, 42, 200, 50));

    // A hidden cell takes no place, a deleted cell is forgotten (also its pointer)
    cells[1]->hide();
    grid->updateCells();
    QCOMPARE(cells[2]->geometry(), QRect(210, 0, 200, 40));
    QCOMPARE(grid->getVisibleWidgets().size(), 4);
    QCOMPARE(grid->getCellCount(), 5);
    delete cells[0];
    QCOMPARE(grid->getVisibleWidgets().size(), 3);
    QCOMPARE(grid->getCellCount(), 4);

    // A cell taken away by the page is not a cell any more, a cell added twice is one cell
    std::unique_ptr<QWidget> taken(cells[4]);
    taken->setParent(nullptr);
    QCOMPARE(grid->getCellCount(), 3);
    grid->addWidget(cells[3]);
    QCOMPARE(grid->getCellCount(), 3);

    // Cells replaced again and again (the refresh of a page) do not grow the grid
    for (int round = 0; round < 50; ++round)
    {
        QWidget* cell = new QWidget();
        grid->addWidget(cell);
        delete cell;
    }
    QCOMPARE(grid->getCellCount(), 3);
    QVERIFY(grid->sizeHint().width() > 0);
    QCOMPARE(grid->minimumSizeHint(), QSize(0, 0));
}

void AppInfoPagesTest::gridFractionalColumns()
{
    // The columns share the whole width, the edges are rounded (625 - 2 * 10 = 605 / 3)
    QWidget parent;
    pdf::PDFAppGrid* grid = new pdf::PDFAppGrid(200, 3, 10, 12, &parent);
    QList<QWidget*> cells;
    for (int i = 0; i < 4; ++i)
    {
        QWidget* cell = new QWidget();
        cell->setMinimumHeight(20);
        grid->addWidget(cell);
        cells << cell;
    }
    parent.show();
    grid->resize(625, grid->heightForWidth(625));
    grid->updateCells();
    QCOMPARE(cells[0]->geometry(), QRect(0, 0, 202, 20));
    QCOMPARE(cells[1]->geometry(), QRect(212, 0, 201, 20));
    QCOMPARE(cells[2]->geometry(), QRect(423, 0, 202, 20));
    QCOMPARE(cells[3]->geometry(), QRect(0, 32, 202, 20));

    // Right-to-left mirrors the columns
    grid->setLayoutDirection(Qt::RightToLeft);
    grid->updateCells();
    QCOMPARE(cells[0]->geometry().right(), 624);
    QCOMPARE(cells[2]->geometry().left(), 0);

    // A cell added to a shown grid is shown, a cell hidden by the page stays hidden
    QWidget* shown = new QWidget(grid);
    grid->addWidget(shown);
    QVERIFY(shown->isVisible());
    QWidget* hidden = new QWidget(grid);
    hidden->hide();
    grid->addWidget(hidden);
    QVERIFY(hidden->isHidden());
    QCOMPARE(grid->getVisibleWidgets().size(), 5);

    // A child of the grid shown by the page before it is added
    QWidget* visible = new QWidget(grid);
    visible->show();
    grid->addWidget(visible);
    QVERIFY(visible->isVisible());
    QCOMPARE(grid->getVisibleWidgets().size(), 6);
}

void AppInfoPagesTest::columns()
{
    // Main and side (createMainAndSide): beside each other from 400 + 24 + 340
    QWidget host;
    QVBoxLayout* hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    pdf::PDFAppColumns* columns = pdf::PDFAppColumns::createMainAndSide(&host);
    hostLayout->addWidget(columns);
    hostLayout->addStretch(1);
    QCOMPARE(columns->getColumnCount(), 2);
    QCOMPARE(columns->getMainColumn()->objectName(), QStringLiteral("mainColumn"));
    QCOMPARE(columns->getSideColumn()->objectName(), QStringLiteral("sideColumn"));
    QVERIFY(columns->getMainLayout() && columns->getSideLayout());
    QWidget* mainContent = new QWidget();
    mainContent->setFixedHeight(100);
    columns->getMainLayout()->addWidget(mainContent);
    QWidget* sideContent = new QWidget();
    sideContent->setFixedHeight(50);
    columns->getSideLayout()->addWidget(sideContent);
    QVERIFY(columns->isStackedAt(763));
    QVERIFY(!columns->isStackedAt(764));

    host.resize(1000, 400);
    host.show();
    QCoreApplication::processEvents();
    QVERIFY(!columns->isStacked());
    QCOMPARE(columns->getSideColumn()->width(), pdf::PDFAppColumns::SIDE_WIDTH);
    QCOMPARE(columns->getMainColumn()->width(), 1000 - 24 - 340);
    QCOMPARE(columns->getSideColumn()->x(), 1000 - 340);

    // A narrow page: the side column below the main column in the full width
    host.resize(700, 400);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QVERIFY(columns->isStacked());
    QCOMPARE(columns->getSideColumn()->width(), 700);
    QCOMPARE(columns->getSideColumn()->y(), columns->getMainColumn()->geometry().bottom() + 1 + 24);

    // Right-to-left: the side column at the left
    host.setLayoutDirection(Qt::RightToLeft);
    host.resize(1000, 400);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QVERIFY(!columns->isStacked());
    QCOMPARE(columns->getSideColumn()->x(), 0);

    // Columns of a page of the content: widgets of the page, each with its own height
    QWidget content;
    QWidget* main = new QWidget(&content);
    QVBoxLayout* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QWidget* mainBlock = new QWidget();
    mainBlock->setFixedHeight(300);
    mainLayout->addWidget(mainBlock);
    QWidget* side = new QWidget(&content);
    QVBoxLayout* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    QWidget* sideBlock = new QWidget();
    sideBlock->setFixedHeight(80);
    sideLayout->addWidget(sideBlock);
    pdf::PDFAppColumns* contentColumns = pdf::PDFAppColumns::createInContent(&content, main, side);
    QCOMPARE(contentColumns->objectName(), QStringLiteral("columns"));
    QCOMPARE(contentColumns->getColumn(0), main);
    QVERIFY(!contentColumns->getColumnLayout(5));
    QVERIFY(contentColumns->isStackedAt(pdf::PDFAppColumns::CONTENT_MINIMUM_MAIN_WIDTH + 24 + 340 - 1));
    content.resize(1166, 400);
    content.show();
    QCoreApplication::processEvents();
    QCOMPARE(main->width(), 1166 - 24 - 340);
    QCOMPARE(side->width(), 340);
    QCOMPARE(main->height(), 300);
    QCOMPARE(side->height(), 80);

    // A deleted column is forgotten; nothing is added for no widget
    delete side;
    QCOMPARE(contentColumns->getColumnCount(), 1);
    QVERIFY(!contentColumns->getColumn(1));
    QVERIFY(!contentColumns->isStackedAt(500));
    QVERIFY(!contentColumns->addColumn(nullptr, 100, 0, 0));
    QCOMPARE(contentColumns->getColumnCount(), 1);

    // A column added to shown columns is shown
    QWidget* added = contentColumns->addColumn(200, 0, 0);
    QVERIFY(added->isVisible());
    QCOMPARE(contentColumns->getColumnCount(), 2);

    // The trailing widget of a section header is replaced (the previous one is deleted)
    pdf::PDFAppSectionHeader* header = pdf::PDFAppPageWidgets::createSectionHeader(QStringLiteral("Title"), nullptr, &content);
    QVERIFY(!header->getTrailingWidget());
    QCOMPARE(header->getTitleLabel()->text(), QStringLiteral("Title"));
    QPointer<QWidget> first = pdf::PDFAppPageWidgets::createHint(QStringLiteral("hint"), nullptr);
    header->setTrailingWidget(first);
    header->setTrailingWidget(first);
    QVERIFY(first && header->getTrailingWidget() == first);
    header->setTrailingWidget(new QWidget());
    QVERIFY(first.isNull());
    QCOMPARE(pdf::PDFAppPageWidgets::createCard(8, 2, &content)->getLayout()->contentsMargins(), QMargins(9, 9, 9, 9));
}

void AppInfoPagesTest::columnsEdges()
{
    // A widget of no width yet (not laid out): the columns start beside each other
    pdf::PDFAppColumns columns;
    columns.resize(0, 50);
    QWidget* side = columns.addColumn(200, 0, 0);
    QWidget* main = columns.addColumn(0, 1, 300);
    QCOMPARE(columns.getColumnCount(), 2);
    QVERIFY(columns.isStackedAt(0));
    QVERIFY(!columns.isStacked());
    QCOMPARE(side->minimumWidth(), 200);
    QCOMPARE(side->maximumWidth(), 200);
    QCOMPARE(columns.getColumnLayout(0)->spacing(), pdf::PDFAppColumns::GAP);
    QVERIFY(!columns.getColumn(-1));

    // A column, which the page has taken away, is not a column any more
    QWidget other;
    main->setParent(&other);
    QCOMPARE(columns.getColumnCount(), 1);
    QCOMPARE(columns.getColumn(0), side);
    QVERIFY(!columns.getColumn(1));
    QVERIFY(!columns.isStackedAt(200));
    QVERIFY(columns.isStackedAt(199));

    // The fixed column of stacked columns has the whole width, beside each other its width again
    QWidget* third = columns.addColumn(0, 1, 400);
    columns.show();
    columns.resize(500, 50);
    QCoreApplication::processEvents();
    QVERIFY(columns.isStacked());
    QCOMPARE(side->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(side->width(), 500);
    columns.resize(700, 50);
    QCoreApplication::processEvents();
    QVERIFY(!columns.isStacked());
    QCOMPARE(side->width(), 200);
    QCOMPARE(third->width(), 700 - 200 - pdf::PDFAppColumns::GAP);

    // The layout deleted by the application: nothing is added, a resize does nothing
    pdf::PDFAppColumns broken;
    broken.addColumn(100, 0, 0);
    delete broken.layout();
    QWidget* refused = new QWidget();
    QCOMPARE(broken.addColumn(refused, 100, 0, 0), refused);
    QCOMPARE(broken.getColumnCount(), 1);
    QVERIFY(!refused->parentWidget());
    delete refused;
    broken.resize(50, 50);
    broken.show();
    QCoreApplication::processEvents();
    QVERIFY(!broken.isStacked());
}

void AppInfoPagesTest::sectionHeader()
{
    QWidget parent;
    pdf::PDFAppSectionHeader* header = pdf::PDFAppPageWidgets::createSectionHeader(QStringLiteral("Section"), new QWidget(), &parent);
    QPointer<QWidget> trailing = header->getTrailingWidget();
    QVERIFY(trailing);
    QCOMPARE(trailing->parentWidget(), header);

    // No trailing widget: the previous one is deleted
    header->setTrailingWidget(nullptr);
    QVERIFY(trailing.isNull());
    QVERIFY(!header->getTrailingWidget());
    header->setTrailingWidget(nullptr);
    QVERIFY(!header->getTrailingWidget());

    // A trailing widget deleted by the application is forgotten
    QWidget* link = new QWidget();
    header->setTrailingWidget(link);
    delete link;
    QVERIFY(!header->getTrailingWidget());
    QWidget* replacement = new QWidget();
    header->setTrailingWidget(replacement);
    QCOMPARE(header->getTrailingWidget(), replacement);

    // The layout deleted by the application: the widget is not placed
    // (the header still owns it: it is deleted, when it is replaced)
    delete header->layout();
    QPointer<QWidget> unplaced = new QWidget();
    header->setTrailingWidget(unplaced);
    QVERIFY(!unplaced->parentWidget());
    QCOMPARE(header->getTrailingWidget(), unplaced.data());
    header->setTrailingWidget(nullptr);
    QVERIFY(unplaced.isNull());
}

/// Page of the test of the links of PDFAppContentPage
class LinkTestPage : public pdf::PDFAppContentPage
{
    Q_OBJECT

public:
    using pdf::PDFAppContentPage::PDFAppContentPage;

    virtual QString getTitle() const override { return QStringLiteral("Links"); }

    QAbstractButton* addLink(QAbstractButton* button, const QString& link) { return withLink(button, link); }

    QStringList anchors;

protected:
    virtual bool scrollToAnchor(const QString& anchor) override
    {
        anchors << anchor;
        return true;
    }
};

void AppInfoPagesTest::contentPageLinks()
{
    LinkTestPage page(nullptr);
    QSignalSpy navigated(&page, &pdf::PDFAppPage::navigationRequested);
    QSignalSpy external(&page, &pdf::PDFAppPage::externalUrlRequested);

    // Routes, https addresses and anchors; an empty link does nothing
    QVERIFY(page.openLink(QString()));
    QVERIFY(page.openLink(QStringLiteral("/about")));
    QCOMPARE(navigated.count(), 1);
    QCOMPARE(navigated[0].at(0).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::about());
    QVERIFY(page.openLink(QStringLiteral("https://example.com/x")));
    QCOMPARE(external.count(), 1);
    QCOMPARE(external[0].at(0).toUrl(), QUrl(QStringLiteral("https://example.com/x")));
    QVERIFY(page.openLink(QStringLiteral("#third-party")));
    QCOMPARE(page.anchors, QStringList({ QStringLiteral("third-party") }));

    // Anything else is refused
    for (const QString& link : { QStringLiteral("http://example.com"), QStringLiteral("javascript:alert(1)"), QStringLiteral("file:///c:/x"),
                                 QStringLiteral("//server/share"), QStringLiteral("about"), QStringLiteral("https://user@example.com/") })
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("A link of the page was refused"));
        QVERIFY(page.openLink(link));
    }
    QCOMPARE(navigated.count(), 1);
    QCOMPARE(external.count(), 1);

    // A button with a link, its link property
    QPushButton* button = new QPushButton(&page);
    QCOMPARE(page.addLink(button, QStringLiteral("/sponsor")), button);
    QCOMPARE(pdf::PDFAppPageWidgets::getLink(button), QStringLiteral("/sponsor"));
    QVERIFY(!page.addLink(nullptr, QStringLiteral("/sponsor")));
    button->click();
    QCOMPARE(navigated.count(), 2);
    QCOMPARE(navigated[1].at(0).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::sponsor());
    pdf::PDFAppPageWidgets::setLink(nullptr, QStringLiteral("/x"));
    QVERIFY(pdf::PDFAppPageWidgets::getLink(nullptr).isEmpty());

    // Outside of a tab there is no scroll area
    QVERIFY(!page.getScrollArea());
    QCOMPARE(page.getScrollPosition(), -1);
    QVERIFY(!page.setScrollPosition(10));
    QVERIFY(!page.scrollToWidget(button));
    QVERIFY(page.saveViewState().isEmpty());
    page.restoreViewState(QVariantMap({ { pdf::PDFAppContentPage::getScrollKey(), QStringLiteral("x") } }));

    // The page deleted by the navigation of its link
    LinkTestPage* deletedPage = new LinkTestPage(nullptr);
    QObject::connect(deletedPage, &pdf::PDFAppPage::navigationRequested, &page, [deletedPage]() { delete deletedPage; });
    QVERIFY(!deletedPage->openLink(QStringLiteral("/home")));
    LinkTestPage* externalPage = new LinkTestPage(nullptr);
    QObject::connect(externalPage, &pdf::PDFAppPage::externalUrlRequested, &page, [externalPage]() { delete externalPage; });
    QVERIFY(!externalPage->openLink(QStringLiteral("https://example.com/")));
}

void AppInfoPagesTest::contentPageScroll()
{
    // A page in a scroll area of the application, whose content has no layout
    QScrollArea area;
    area.setWidgetResizable(false);
    QWidget* content = new QWidget();
    content->resize(400, 2000);
    ScrollTestPage* page = new ScrollTestPage(nullptr, content);
    page->setGeometry(0, 100, 400, 1800);
    QWidget* target = new QWidget(page);
    target->setGeometry(0, 500, 400, 50);
    area.setWidget(content);
    area.resize(420, 300);
    area.show();
    QCoreApplication::processEvents();

    QCOMPARE(page->getScrollArea(), &area);
    QCOMPARE(page->getScrollPosition(), 0);
    QVERIFY(page->setScrollPosition(200));
    QCOMPARE(page->getScrollPosition(), 200);
    QVERIFY(!page->setScrollPosition(100000));
    QCOMPARE(page->getScrollPosition(), area.verticalScrollBar()->maximum());
    QVERIFY(page->scrollToWidget(target));
    QCOMPARE(page->getScrollPosition(), 600);
    QVERIFY(page->scrollToWidget(target, -50));
    QCOMPARE(page->getScrollPosition(), 550);
    QCOMPARE(page->saveViewState().value(pdf::PDFAppContentPage::getScrollKey()).toInt(), 550);

    // No widget, a widget outside of the content
    QWidget outside;
    QVERIFY(!page->scrollToWidget(nullptr));
    QVERIFY(!page->scrollToWidget(&outside));
    QCOMPARE(page->getScrollPosition(), 550);

    // The default anchors of a page: none (the link is still handled)
    QVERIFY(page->openLink(QStringLiteral("#anything")));
    QCOMPARE(page->anchors, QStringList({ QStringLiteral("anything") }));
    QCOMPARE(page->getScrollPosition(), 550);

    // A page, which is a child of a scroll area, but not of its content
    QScrollArea empty;
    ScrollTestPage* lonely = new ScrollTestPage(nullptr, &empty);
    QVERIFY(!lonely->getScrollArea());
    QCOMPARE(lonely->getScrollPosition(), -1);
    QVERIFY(!lonely->setScrollPosition(10));
    QVERIFY(!lonely->scrollToWidget(lonely));

    QScrollArea other;
    other.setWidget(new QWidget());
    ScrollTestPage* beside = new ScrollTestPage(nullptr, other.viewport());
    QVERIFY(!beside->getScrollArea());
    QVERIFY(!beside->setScrollPosition(10));
}

void AppInfoPagesTest::contentPageDeferredScroll()
{
    QScrollArea area;
    area.setWidgetResizable(false);
    QWidget* content = new QWidget();
    content->resize(400, 2000);
    ScrollTestPage* page = new ScrollTestPage(nullptr, content);
    page->setGeometry(0, 0, 400, 2000);
    QWidget* target = new QWidget(page);
    target->setGeometry(0, 700, 400, 50);
    area.setWidget(content);
    area.resize(420, 300);
    area.show();
    QCoreApplication::processEvents();
    QScrollBar* scrollBar = area.verticalScrollBar();

    // A position is applied after the event loop; requests before it are joined (the last wins)
    page->requestScroll(100, QString());
    page->requestScroll(150, QString());
    QCOMPARE(scrollBar->value(), 0);
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 150);

    // A hidden page keeps the request until it is shown again
    page->requestScroll(300, QString());
    page->hide();
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 150);
    page->show();
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 300);

    // The anchor widget of a shown page (a route with an anchor given to a shown page)
    page->setAnchor(target);
    QCOMPARE(scrollBar->value(), 300);
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 700);

    // Nothing pending: a request without a position and an anchor, a cleared anchor
    page->requestScroll(-1, QString());
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 700);
    scrollBar->setValue(10);
    page->setAnchor(target);
    page->setAnchor(nullptr);
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 10);

    // A named anchor, which the page does not know (the default scrollToAnchor)
    page->requestScroll(-1, QStringLiteral("unknown"));
    InfoFixture::settle();
    QCOMPARE(scrollBar->value(), 10);
    QCOMPARE(page->anchors, QStringList({ QStringLiteral("unknown") }));

    // A named anchor of a hidden page is asked for, when the page is shown
    page->hide();
    page->requestScroll(-1, QStringLiteral("later"));
    InfoFixture::settle();
    QCOMPARE(page->anchors.size(), 1);
    page->show();
    InfoFixture::settle();
    QCOMPARE(page->anchors, QStringList({ QStringLiteral("unknown"), QStringLiteral("later") }));

    // An anchor widget of a page outside of a scroll area stays pending, until the
    // page is shown in a scroll area
    ScrollTestPage* standalone = new ScrollTestPage(nullptr);
    QWidget* anchor = new QWidget(standalone);
    anchor->setGeometry(0, 900, 400, 50);
    standalone->resize(400, 2000);
    standalone->setAnchor(anchor);
    standalone->show();
    InfoFixture::settle();
    QVERIFY(!standalone->getScrollArea());

    QScrollArea second;
    second.setWidgetResizable(false);
    QWidget* secondContent = new QWidget();
    secondContent->resize(400, 2000);
    second.setWidget(secondContent);
    second.resize(420, 300);
    second.show();
    standalone->setParent(secondContent);
    standalone->move(0, 0);
    standalone->show();
    InfoFixture::settle();
    QCOMPARE(standalone->getScrollArea(), &second);
    QCOMPARE(second.verticalScrollBar()->value(), 900);
}

void AppInfoPagesTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: pictures of the pages in both themes at 1x
    // and 2x (the comparison with Figma 110:2163, 116:2419, 122:2652)
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    InfoFixture fixture;

    struct Shot
    {
        QString route;
        QString name;
        QSize size;
    };
    const Shot shots[] = { { QStringLiteral("/about"), QStringLiteral("about"), QSize(1440, 1067) },
                           { QStringLiteral("/support"), QStringLiteral("support"), QSize(1440, 1038) },
                           { QStringLiteral("/support?topic=for-you"), QStringLiteral("support-for-you"), QSize(1440, 1038) },
                           { QStringLiteral("/sponsor"), QStringLiteral("sponsor"), QSize(1440, 1172) },
                           { QStringLiteral("/about"), QStringLiteral("about-portable"), QSize(1440, 1067) },
                           { QStringLiteral("/about"), QStringLiteral("about-minimal"), QSize(720, 480) },
                           { QStringLiteral("/support"), QStringLiteral("support-minimal"), QSize(720, 480) } };

    for (const Shot& shot : shots)
    {
        fixture.window->resize(shot.size);
        fixture.navigate(shot.route);
        QVERIFY(fixture.currentPage());
        fixture.homeTab()->getBanner()->setVisible(shot.name.endsWith(QLatin1String("portable")));
        InfoFixture::settle();
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            InfoFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = InfoTestHelper::render(fixture.window, ratio);
                QVERIFY(!image.isNull());
                QCOMPARE(InfoTestHelper::countLeakPixels(image), 0);
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(shot.name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(name)));
                }
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    }
}

QTEST_MAIN(AppInfoPagesTest)

#include "tst_appinfopagestest.moc"
