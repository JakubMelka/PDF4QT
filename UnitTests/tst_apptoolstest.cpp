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

#include "apptoolsfixture.h"

#include "pdfappbuiltintoolcollections.h"
#include "pdfappdocumenttab.h"
#include "pdfapphomewidgets.h"
#include "pdfappnavigator.h"
#include "pdfapppagelayout.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfappsearchengine.h"
#include "pdfappsearchpage.h"
#include "pdfapptab.h"
#include "pdfapptoolcollections.h"
#include "pdfapptoolcollectionspage.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolspage.h"
#include "pdfapptoolstate.h"
#include "pdfapptoolcategorycard.h"
#include "pdfapptoolmenu.h"
#include "pdfapphomepage.h"
#include "pdfappmainwindow.h"
#include "pdfappsupportpage.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfdocumenttoolsearch.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdfdocumenttoolusage.h"
#include "pdftoolcollectionrepository.h"

#include "pdfdocumenttoolcatalog.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdfthemeiconengine.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimeZone>
#include <QTranslator>
#include <QtTest>

#include <map>

/// The Tools catalog of step 6: wave 0 (routes, Page Not Found of the tools, the
/// registry of the tool pages, the presentation, the built-in collections) and
/// package A (the Tools page)
class AppToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void routesAndNavHighlight_data();
    void routesAndNavHighlight();
    void notFoundToolTitle();
    void toolPageRegistryPath();
    void toolPageRegistryEdges();
    void presentationOfEveryTool();
    void builtinCollectionsData();

    // Package A - the Tools page
    void catalogLayout();
    void catalogMatchesFigma();
    void chipsAndCounts();
    void searchFiltersInRoute();
    void searchReason();
    void searchHostileTexts();
    void searchNoResults();
    void clearFilterGoesBack();
    void nearestTools();
    void pinByStarAndMenu();
    void pinnedOrderAndEmpty();
    void pinWriteRefused();
    void recentToolsAndClear();
    void collectionsSideList();
    void collectionFilter();
    void addToCollectionMenu();
    void toolActivationRoutes();
    void keyboardGrid();
    void performance();
    void hostileStoredRows();
    void otherInstanceChanges();
    void deletionMatrix_data();
    void deletionMatrix();
    void deleteFromOwnSignals_data();
    void deleteFromOwnSignals();
    void themesAndScales_data();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void categoryCardWidgets();
    void recentTimesFollowClock();
};

void AppToolsTest::initTestCase()
{
    // The style of the application (the overlay scroll bar of the page, the tokens)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppToolsTest::cleanup()
{
    pdftest::AppDocumentFixture::settle();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolsTest::routesAndNavHighlight_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("pageClass");     // empty = Page Not Found
    QTest::addColumn<QString>("shownRoute");    // the route of the history (empty = the route)

    const QString tools = QStringLiteral("pdf::PDFAppToolsPage");
    const QString collections = QStringLiteral("pdf::PDFAppToolCollectionsPage");

    QTest::newRow("tools") << "/tools" << tools << QString();
    QTest::newRow("category") << "/tools?category=protect" << tools << QString();
    QTest::newRow("pinned and text") << "/tools?category=pinned&q=merge" << tools << QString();
    QTest::newRow("text") << "/tools?q=sign%20pdf" << tools << QString();
    QTest::newRow("built-in collection") << "/tools?collection=prepare-for-sharing" << tools << QString();
    QTest::newRow("custom collection") << "/tools?collection=custom-1" << tools << QString();
    QTest::newRow("unknown collection") << "/tools?collection=unknown" << QString() << QString();
    QTest::newRow("collection custom-0") << "/tools?collection=custom-0" << QString() << QString();
    QTest::newRow("collection custom-99999999999") << "/tools?collection=custom-99999999999" << QString() << QString();
    QTest::newRow("collection and category") << "/tools?collection=prepare-for-sharing&category=pages" << QString() << QString();
    QTest::newRow("collection and text") << "/tools?collection=prepare-for-sharing&q=x" << QString() << QString();
    QTest::newRow("unknown category") << "/tools?category=bogus" << QString() << QString();
    QTest::newRow("unknown key") << "/tools?sort=name" << QString() << QString();
    QTest::newRow("10 kB text") << QStringLiteral("/tools?q=") + QString(10 * 1024, QLatin1Char('x')) << QString() << QString();
    QTest::newRow("null in text") << "/tools?q=a%00b" << QString() << QString();
    QTest::newRow("collections") << "/tools/collections" << collections << "/tools/collections/prepare-for-sharing";
    QTest::newRow("new collection") << "/tools/collections/new" << collections << QString();
    QTest::newRow("new collection with tool") << "/tools/collections/new?tool=pages.merge" << collections << QString();
    QTest::newRow("new collection unknown tool") << "/tools/collections/new?tool=pages.unknown" << QString() << QString();
    QTest::newRow("new collection invalid tool") << "/tools/collections/new?tool=Pages!" << QString() << QString();
    QTest::newRow("built-in collection page") << "/tools/collections/fix-a-scan" << collections << QString();
    QTest::newRow("custom collection page") << "/tools/collections/custom-1" << collections << QString();
    QTest::newRow("custom-0 page") << "/tools/collections/custom-0" << QString() << QString();
    QTest::newRow("custom-99999999999 page") << "/tools/collections/custom-99999999999" << QString() << QString();
    QTest::newRow("custom-01 page") << "/tools/collections/custom-01" << QString() << QString();
    QTest::newRow("unknown collection page") << "/tools/collections/unknown" << QString() << QString();
    QTest::newRow("tool without page") << "/tools/pages.merge" << QString() << QString();
    QTest::newRow("unknown tool") << "/tools/convert.epub" << QString() << QString();
    QTest::newRow("null in tool") << "/tools/pages.merge%00" << QString() << QString();
}

void AppToolsTest::routesAndNavHighlight()
{
    QFETCH(QString, route);
    QFETCH(QString, pageClass);
    QFETCH(QString, shownRoute);

    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Mine"), QString(), { QStringLiteral("pages.merge") }).isError());
    QCOMPARE(fixture.collections().getCollections().front().id, qint64(1));

    pdf::PDFAppPage* page = fixture.openTools(route);
    QVERIFY(page);
    if (pageClass.isEmpty())
    {
        QVERIFY2(fixture.notFoundPage(), page->metaObject()->className());
        QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::None);
    }
    else
    {
        QCOMPARE(QString::fromLatin1(page->metaObject()->className()), pageClass);
        QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::Tools);
        QCOMPARE(fixture.currentRoute(), shownRoute.isEmpty() ? pdf::PDFAppRoute::parse(route).toString() : shownRoute);
        QVERIFY(!page->getTitle().isEmpty());
    }

    // The builders give valid routes
    QCOMPARE(pdf::PDFAppRoutes::tools(QStringLiteral("pages"), QStringLiteral("sign"), QString()).toString(), QStringLiteral("/tools?category=pages&q=sign"));
    QCOMPARE(pdf::PDFAppRoutes::tools(QStringLiteral("pages"), QStringLiteral("sign"), QStringLiteral("custom-1")).toString(), QStringLiteral("/tools?collection=custom-1"));
    QCOMPARE(pdf::PDFAppRoutes::toolCollectionNew().toString(), QStringLiteral("/tools/collections/new"));
    QCOMPARE(pdf::PDFAppRoutes::toolCollectionNew(QStringLiteral("pages.merge")).toString(), QStringLiteral("/tools/collections/new?tool=pages.merge"));
    const pdf::PDFAppRoute hostile = pdf::PDFAppRoutes::tools(QString(), QString(QChar(0x0007)) + QString(20000, QChar(0x4E2D)) + QChar(0xD800));
    QVERIFY(hostile.isValid());
    QVERIFY(pdf::PDFAppRouteTable::createDefault().match(hostile).result != pdf::PDFAppRouteMatch::Result::InvalidParameter);
}

void AppToolsTest::notFoundToolTitle()
{
    pdftest::AppToolsFixture fixture;

    // A tool of the catalog without a page: its title (940:134845) and the search for it
    fixture.navigate(QStringLiteral("/tools/pages.merge"));
    pdf::PDFPageNotFoundPage* page = fixture.notFoundPage();
    QVERIFY(page);
    QCOMPARE(page->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));
    QCOMPARE(page->getRequest().searchText, QStringLiteral("Merge"));
    QCOMPARE(page->getSearchButton()->text(), QString::fromUtf8("Search for “Merge”"));
    QCOMPARE(page->getPathText(), QStringLiteral("/tools/pages.merge"));

    // An unknown tool keeps its id, bounded
    fixture.navigate(QStringLiteral("/tools/convert.epub"));
    page = fixture.notFoundPage();
    QVERIFY(page);
    QCOMPARE(page->getDescriptionText(), QString::fromUtf8("Tool “convert.epub”"));
    QCOMPARE(page->getRequest().searchText, QStringLiteral("convert epub"));

    // The route table: the titles of the tools and of the built-in collections
    const pdf::PDFAppRouteTable table = pdf::PDFAppRouteTable::createDefault();
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/tools/protect.sanitize"))), QString::fromUtf8("Tool “Remove Hidden Data”"));
    QCOMPARE(table.getSearchText(pdf::PDFAppRoute::parse(QStringLiteral("/tools/protect.sanitize"))), QStringLiteral("Remove Hidden Data"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/tools/collections/fix-a-scan"))), QString::fromUtf8("Tool collection “Fix a scan”"));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/tools/collections/custom-5"))), QString::fromUtf8("Tool collection “custom-5”"));
    const QString longId = QString(64, QLatin1Char('a'));
    QCOMPARE(table.describe(pdf::PDFAppRoute::parse(QStringLiteral("/tools/") + longId)), QString::fromUtf8("Tool “%1”").arg(longId.left(pdf::PDFAppRouteTable::MAXIMUM_SEARCH_TEXT_LENGTH)));

    // The tool in the tab of a document: Page Not Found in the document tab
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    const QString documentId = tab->getDocumentId();
    fixture.navigate(QStringLiteral("/document/%1/tool/pages.merge").arg(documentId));
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));
    page = fixture.notFoundPage();
    QVERIFY(page);
    QCOMPARE(page->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));
    QCOMPARE(page->getRequest().searchText, QStringLiteral("Merge"));
}

void AppToolsTest::toolPageRegistryPath()
{
    // The registry rules: catalog ids only, no null factory, no route factory
    QVERIFY(!pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("convert.epub"), std::make_shared<pdftest::AppToolsTestPageFactory>()));
    QVERIFY(!pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"), nullptr));
    QVERIFY(!pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"),
                                                          std::make_shared<pdf::PDFAppToolRoutePageFactory>(pdf::PDFAppToolPageKind::Start)));
    QVERIFY(pdf::PDFAppToolPageRegistry::getToolsWithPage(pdf::PDFAppToolPageKind::Start).isEmpty());
    QVERIFY(!pdf::PDFAppToolPageRegistry::getFactory(pdf::PDFAppToolPageKind::Start, QString(1000, QLatin1Char('x'))));

    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(fixture.registerTestToolPage(QStringLiteral("pages.merge")));
        QVERIFY(fixture.registerTestToolPage(QStringLiteral("review.statistics"), pdf::PDFAppToolPageKind::Document));
        QCOMPARE(pdf::PDFAppToolPageRegistry::getToolsWithPage(pdf::PDFAppToolPageKind::Start), QStringList({ QStringLiteral("pages.merge") }));
        QVERIFY(!pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, QStringLiteral("pages.merge")));

        // The route
        fixture.navigate(QStringLiteral("/tools/pages.merge"));
        pdftest::AppToolsTestPage* testPage = fixture.page<pdftest::AppToolsTestPage>();
        QVERIFY(testPage);
        QCOMPARE(testPage->getToolId(), QStringLiteral("pages.merge"));
        QCOMPARE(fixture.currentTab()->getNavItem(), pdf::PDFAppNavItem::Tools);

        // A row of the Tools page: a registered tool opens its page, another one Page Not Found
        fixture.openTools(QStringLiteral("/tools"));
        pdf::PDFToolRow* row = fixture.findToolRow(QStringLiteral("pages.merge"));
        QVERIFY(row);
        row->click();

        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.page<pdftest::AppToolsTestPage>());
        QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/pages.merge"));

        fixture.openTools(QStringLiteral("/tools"));
        row = fixture.findToolRow(QStringLiteral("pages.split"));
        QVERIFY(row);
        row->click();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.notFoundPage());
        QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Split”"));

        // The global search: the tool result navigates to the page of the tool
        fixture.navigate(QStringLiteral("/search?q=merge"));
        pdf::PDFAppSearchPage* searchPage = fixture.page<pdf::PDFAppSearchPage>();
        QVERIFY(searchPage);
        const pdf::PDFAppSearchResult* result = searchPage->getResults().findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge"));
        QVERIFY(result);
        const pdf::PDFAppSearchResult resultCopy = *result;
        searchPage->activateResult(resultCopy);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.page<pdftest::AppToolsTestPage>());

        // The document kind: the page of the tool in the tab of the document
        pdf::PDFAppDocumentTab* tab = fixture.openDocument();
        QVERIFY(tab);
        fixture.navigate(QStringLiteral("/document/%1/tool/review.statistics").arg(tab->getDocumentId()));
        QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));
        testPage = fixture.page<pdftest::AppToolsTestPage>();
        QVERIFY(testPage);
        QCOMPARE(testPage->getToolId(), QStringLiteral("review.statistics"));
        fixture.navigate(QStringLiteral("/document/%1/tool/pages.merge").arg(tab->getDocumentId()));
        QVERIFY(fixture.notFoundPage());

        // Unregistered: Page Not Found again
        pdf::PDFAppToolPageRegistry::unregisterFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"));
        pdf::PDFAppToolPageRegistry::unregisterFactory(pdf::PDFAppToolPageKind::Start, QStringLiteral("pages.merge"));
        fixture.navigate(QStringLiteral("/tools/pages.merge"));
        QVERIFY(fixture.notFoundPage());
    }

    // The fixture removes its pages
    QVERIFY(pdf::PDFAppToolPageRegistry::getToolsWithPage(pdf::PDFAppToolPageKind::Document).isEmpty());
    pdf::PDFAppToolPageRegistry::clear();
}

void AppToolsTest::toolPageRegistryEdges()
{
    // Edges of the route factory of the tools and of the state (review RT6)
    pdftest::AppToolsFixture fixture;
    using Factory = pdf::PDFAppToolRoutePageFactory;
    using Kind = pdf::PDFAppToolPageKind;
    const pdf::PDFAppRoute start = pdf::PDFAppRoute::parse(QStringLiteral("/tools/pages.merge"));
    const pdf::PDFAppRoute document = pdf::PDFAppRoute::parse(QStringLiteral("/document/abc/tool/pages.merge"));
    const pdf::PDFAppRoute otherDocument = pdf::PDFAppRoute::parse(QStringLiteral("/document/xyz/tool/pages.merge"));
    const pdf::PDFAppRoute otherTool = pdf::PDFAppRoute::parse(QStringLiteral("/document/abc/tool/pages.split"));

    // The route alone chooses the page: the factory without a route creates nothing
    Factory startFactory(Kind::Start);
    Factory documentFactory(Kind::Document);
    QVERIFY(!startFactory.createPage(fixture.context.get(), nullptr));
    QCOMPARE(Factory::getToolId(Kind::Start, pdf::PDFAppRoute()), QString());
    QCOMPARE(Factory::getToolId(Kind::Start, document), QString());
    QCOMPARE(Factory::getToolId(Kind::Document, start), QString());
    QCOMPARE(Factory::getToolId(Kind::Document, document), QStringLiteral("pages.merge"));

    // The same page: the same tool of the same document (the page of a registered
    // factory decides itself; without one the route is the page)
    QVERIFY(documentFactory.isSamePage(document, document));
    QVERIFY(!documentFactory.isSamePage(document, otherDocument));
    QVERIFY(!documentFactory.isSamePage(document, otherTool));
    QVERIFY(!documentFactory.isSamePage(start, start));
    QVERIFY(startFactory.isSamePage(start, start));
    QVERIFY(fixture.registerTestToolPage(QStringLiteral("pages.merge"), Kind::Document));
    QVERIFY(documentFactory.isSamePage(document, document));
    QVERIFY(!documentFactory.isSamePage(document, otherTool));

    // tools.suggestNext stored off, an integer only (a row of another version is untrusted)
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(pdf::PDFAppToolState::read(fixture.context->getDatabase()).isSuggestNextEnabled);
    QVERIFY(!connection->execute("INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('tools.suggestNext', 0, 1)").isError());
    QVERIFY(!pdf::PDFAppToolState::read(fixture.context->getDatabase()).isSuggestNextEnabled);
    QVERIFY(!connection->execute("INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('tools.suggestNext', 'off', 1)").isError());
    QVERIFY(pdf::PDFAppToolState::read(fixture.context->getDatabase()).isSuggestNextEnabled);
    const pdf::PDFAppToolState noDatabase = pdf::PDFAppToolState::read(nullptr);
    QVERIFY(noDatabase.pinnedTools.isEmpty() && noDatabase.usage.empty());
    QCOMPARE(noDatabase.collections.size(), pdf::PDFAppBuiltinToolCollections::getCollections().size());

    // The last use of a tool, which was never used, and of a used one
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("pages.merge")).isError());
    const pdf::PDFAppToolState state = pdf::PDFAppToolState::read(fixture.context->getDatabase());
    QCOMPARE(state.getLastUsed(QStringLiteral("pages.split")), qint64(0));
    QVERIFY(state.getLastUsed(QStringLiteral("pages.merge")) > 0);

    // The menu of a tool knows the collections it lists only
    pdf::PDFAppToolsPage* page = qobject_cast<pdf::PDFAppToolsPage*>(fixture.openTools());
    QVERIFY(page);
    QPointer<pdf::PDFAppToolMenu> menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(10, 10));
    QVERIFY(menu);
    QVERIFY(!menu->findCollectionAction(QStringLiteral("no-such-collection")));
    QVERIFY(menu->findCollectionAction(QStringLiteral("fix-a-scan")));
    menu->close();
    pdftest::AppDocumentFixture::settle();
}

void AppToolsTest::presentationOfEveryTool()
{
    pdftest::AppToolsFixture fixture;
    QWidget parent;

    // Hues: every hue of the catalog is a hue of the components, never Neutral
    QSet<int> hues;
    for (const pdf::PDFDocumentToolCategoryInfo& category : pdf::PDFDocumentToolCatalog::getCategories())
    {
        const pdf::PDFAccentHue hue = pdf::PDFAppToolPresentation::getCategoryHue(category.getCategory());
        QVERIFY(hue != pdf::PDFAccentHue::Neutral);
        QCOMPARE(pdf::PDFAppToolPresentation::getHue(category.getHue()), hue);
        hues.insert(int(hue));
    }
    QCOMPARE(hues.size(), 6);
    QCOMPARE(pdf::PDFAppToolPresentation::getHue(static_cast<pdf::PDFDocumentToolHue>(100)), pdf::PDFAccentHue::Neutral);
    QCOMPARE(pdf::PDFAppSearchEngine::getToolHue(int(pdf::PDFDocumentToolHue::Violet)), pdf::PDFAccentHue::Violet);

    const qint64 now = pdf::PDFAppHomeFormat::getCurrentTime();
    int advanced = 0;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        const QString id = tool.getId();
        const QString objectName = QStringLiteral("tool-") + id;
        const pdf::PDFAccentHue hue = pdf::PDFAppToolPresentation::getHue(tool.getHue());
        const QString route = QStringLiteral("/tools/") + id;

        pdf::PDFToolRow* row = pdf::PDFAppToolPresentation::createToolRow(tool, true, &parent);
        QCOMPARE(row->objectName(), objectName);
        QCOMPARE(row->getName(), tool.getTitle());
        QCOMPARE(row->getDescription(), tool.getDescription());
        QCOMPARE(row->getIconName(), tool.getIconName());
        QCOMPARE(row->getHue(), hue);
        QCOMPARE(row->getTag(), tool.isAdvanced() ? QStringLiteral("Advanced") : QString());
        QVERIFY(row->isPinned());
        QCOMPARE(pdf::PDFAppToolPresentation::getToolId(row), id);
        QCOMPARE(pdf::PDFAppToolPresentation::getRoute(row).toString(), route);
        QVERIFY(pdf::PDFThemeIcons::hasIcon(row->getIconName()));
        advanced += tool.isAdvanced() ? 1 : 0;

        pdf::PDFQuickActionTile* tile = pdf::PDFAppToolPresentation::createQuickActionTile(tool, false, &parent);
        QCOMPARE(tile->objectName(), objectName);
        QVERIFY(tile->isStarVisible());
        QVERIFY(!tile->isPinned());
        QCOMPARE(tile->getHue(), hue);
        QCOMPARE(pdf::PDFAppToolPresentation::getRoute(tile).toString(), route);

        pdf::PDFToolShortcut* shortcut = pdf::PDFAppToolPresentation::createToolShortcut(tool, &parent);
        QCOMPARE(shortcut->getName(), tool.getTitle());
        QCOMPARE(shortcut->getHue(), hue);
        QCOMPARE(pdf::PDFAppToolPresentation::getToolId(shortcut), id);

        pdf::PDFRecentToolRow* recent = pdf::PDFAppToolPresentation::createRecentToolRow(tool, now - 120000, now, &parent);
        QCOMPARE(recent->getName(), tool.getTitle());
        QCOMPARE(recent->getLastUsed(), pdf::PDFAppHomeFormat::formatTime(now - 120000, now));
        QCOMPARE(pdf::PDFAppToolPresentation::getRoute(recent).toString(), route);
    }
    QCOMPARE(advanced, 7);

    // Remove Hidden Data has the short description of its tile (272:12885)
    QCOMPARE(pdf::PDFAppToolPresentation::getTileDescription(*pdf::PDFDocumentToolCatalog::findTool(u"protect.sanitize")), QStringLiteral("Strip metadata"));
    QCOMPARE(pdf::PDFAppToolPresentation::getTileDescription(*pdf::PDFDocumentToolCatalog::findTool(u"pages.merge")), QStringLiteral("Combine several files into one PDF"));

    // Texts
    QCOMPARE(pdf::PDFAppToolPresentation::getMatchReasonText(QStringLiteral("combine")), QString::fromUtf8("matches “combine”"));
    QCOMPARE(pdf::PDFAppToolPresentation::getToolCountText(1), QStringLiteral("1 tool"));
    QCOMPARE(pdf::PDFAppToolPresentation::getToolCountText(46), QStringLiteral("46 tools"));
    QCOMPARE(pdf::PDFAppToolPresentation::getCollectionMetaText({ QStringLiteral("optimize.compress"), QStringLiteral("protect.sanitize"), QStringLiteral("protect.password") }),
             QString::fromUtf8("3 tools · Compress, Remove Hidden Data, Protect with Password"));
    QCOMPARE(pdf::PDFAppToolPresentation::getCollectionMetaText({ QStringLiteral("unknown"), QStringLiteral("pages.merge") }), QString::fromUtf8("1 tool · Merge"));
    QCOMPARE(pdf::PDFAppToolPresentation::getCollectionMetaText({ QStringLiteral("unknown") }), QStringLiteral("0 tools"));
    QCOMPARE(pdf::PDFAppToolPresentation::getRoute(nullptr).isValid(), false);
    QVERIFY(pdf::PDFAppToolPresentation::getToolId(&parent).isEmpty());

    // Collection rows: the route of Q1
    const std::vector<pdf::PDFAppToolCollection> collections = pdf::PDFAppToolState::read(fixture.context->getDatabase()).collections;
    pdf::PDFCollectionRow* collectionRow = pdf::PDFAppToolPresentation::createCollectionRow(collections.front(), &parent);
    QCOMPARE(collectionRow->objectName(), QStringLiteral("collection-prepare-for-sharing"));
    QCOMPARE(collectionRow->getName(), QStringLiteral("Prepare for sharing"));
    QCOMPARE(collectionRow->getIconName(), QStringLiteral("mail"));
    QCOMPARE(collectionRow->getMeta(), QString::fromUtf8("3 tools · Compress, Remove Hidden Data, Protect with Password"));
    QCOMPARE(pdf::PDFAppToolPresentation::getRoute(collectionRow).toString(), QStringLiteral("/tools?collection=prepare-for-sharing"));

    // The stub of the Tools page shows the stars of the pins (W0)
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("review.compare")));
    fixture.openTools(QStringLiteral("/tools"));
    QVERIFY(fixture.findToolRow(QStringLiteral("review.compare"))->isPinned());
    QVERIFY(!fixture.findToolRow(QStringLiteral("pages.merge"))->isPinned());
    QVERIFY(fixture.findToolWidget(QStringLiteral("pages.merge")));
}

void AppToolsTest::builtinCollectionsData()
{
    // The seven built-in collections of 272:12821 (side column) and 283:1888
    struct Expected
    {
        const char* id;
        const char* name;
        const char* icon;
        QStringList tools;
    };

    const Expected expected[] = {
        { "prepare-for-sharing", "Prepare for sharing", "mail", { "optimize.compress", "protect.sanitize", "protect.password" } },
        { "scan-to-searchable-pdf", "Scan to searchable PDF", "camera", { "convert.scan", "convert.ocr", "optimize.compress" } },
        { "fix-a-scan", "Fix a scan", "rotate-cw", { "pages.rotate", "pages.crop", "pages.interleave", "pages.remove" } },
        { "sign-a-contract", "Sign a contract", "pen-tool", { "protect.sign-by-hand", "protect.sign", "protect.timestamp" } },
        { "archive-documents", "Archive documents", "briefcase", { "pages.merge", "edit.page-numbers", "protect.password" } },
        { "prepare-for-print", "Prepare for print", "printer", { "pages.boxes", "review.output-preview", "review.ink-coverage", "review.soft-proof" } },
        { "extract-content", "Extract content", "download", { "convert.extract-text", "convert.extract-images", "convert.pdf-to-images" } },
    };

    const std::vector<pdf::PDFAppBuiltinToolCollection>& collections = pdf::PDFAppBuiltinToolCollections::getCollections();
    QCOMPARE(collections.size(), std::size(expected));
    for (size_t i = 0; i < collections.size(); ++i)
    {
        QCOMPARE(collections[i].getId(), QString::fromLatin1(expected[i].id));
        QCOMPARE(collections[i].getName(), QString::fromLatin1(expected[i].name));
        QCOMPARE(collections[i].getSourceName(), QString::fromLatin1(expected[i].name));
        QCOMPARE(collections[i].getIconName(), QString::fromLatin1(expected[i].icon));
        QCOMPARE(collections[i].getToolIds(), expected[i].tools);
        QVERIFY(pdf::PDFAppBuiltinToolCollections::isValidIconName(collections[i].getIconName()));
        QCOMPARE(pdf::PDFAppBuiltinToolCollections::getIndex(collections[i].getId()), int(i));
        QCOMPARE(pdf::PDFAppBuiltinToolCollections::findCollection(collections[i].getId()), &collections[i]);
        QVERIFY(pdf::PDFAppToolCollections::isValidRouteId(collections[i].getId()));
    }

    // The icon chooser of 283:1888, every icon exists in the library
    QCOMPARE(pdf::PDFAppBuiltinToolCollections::getIconChoices(),
             QStringList({ "rotate-cw", "camera", "mail", "pen-tool", "briefcase", "printer", "download", "folder", "star", "zap" }));
    for (const QString& icon : pdf::PDFAppBuiltinToolCollections::getIconChoices())
    {
        QVERIFY2(pdf::PDFThemeIcons::hasIcon(icon), qPrintable(icon));
    }
    QCOMPARE(pdf::PDFAppBuiltinToolCollections::getDefaultIconName(), QStringLiteral("folder"));
    QVERIFY(!pdf::PDFAppBuiltinToolCollections::isValidIconName(QStringLiteral("book-open")));
    QVERIFY(!pdf::PDFAppBuiltinToolCollections::findCollection(QString(1000, QLatin1Char('a'))));
    QCOMPARE(pdf::PDFAppBuiltinToolCollections::getIndex(QString()), -1);

    // The merge without rows: the built-in collections in their order
    const std::vector<pdf::PDFAppToolCollection> merged = pdf::PDFAppToolCollections::merge({});
    QCOMPARE(merged.size(), std::size(expected));
    QCOMPARE(merged[2].routeId, QStringLiteral("fix-a-scan"));
    QCOMPARE(merged[2].toolIds, expected[2].tools);
    QVERIFY(merged[2].isBuiltin && !merged[2].isOverridden && merged[2].rowId == 0);
    QCOMPARE(pdf::PDFAppToolCollections::find(merged, QStringLiteral("sign-a-contract")), &merged[3]);
    QVERIFY(!pdf::PDFAppToolCollections::find(merged, QString()));
    QCOMPARE(pdf::PDFAppToolState::read(nullptr).collections.size(), std::size(expected));
}

/// Translator of the tests of the language change (context, source text)
class AppToolsTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return entries.value(qMakePair(QByteArray(context ? context : ""), QByteArray(sourceText ? sourceText : "")));
    }

    virtual bool isEmpty() const override { return false; }

    QHash<QPair<QByteArray, QByteArray>, QString> entries;
};

/// Helpers of the tests of the Tools page (package A)
class AppToolsPageTestHelper
{
public:
    AppToolsPageTestHelper() = delete;

    static pdf::PDFAppToolsPage* open(pdftest::AppToolsFixture& fixture, const QString& route = QStringLiteral("/tools"))
    {
        fixture.openTools(route);
        return fixture.page<pdf::PDFAppToolsPage>();
    }

    static QRect rectIn(const QWidget* widget, const QWidget* ancestor)
    {
        return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
    }

    static bool isNear(int value, int expected, int tolerance = 1)
    {
        return qAbs(value - expected) <= tolerance;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        int count = 0;
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

    static QColor pixel(const QImage& image, const QPoint& point)
    {
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(qFloor(point.x() * ratio), qFloor(point.y() * ratio));
    }

    static bool isClose(const QColor& first, const QColor& second, int tolerance = 3)
    {
        return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance && qAbs(first.blue() - second.blue()) <= tolerance;
    }

    static QStringList visibleIds(const pdf::PDFAppToolsPage* page)
    {
        QStringList ids;
        for (pdf::PDFToolRow* row : page->getVisibleToolRows())
        {
            ids << pdf::PDFAppToolPresentation::getToolId(row);
        }
        return ids;
    }

    static int chipCount(const pdf::PDFAppToolsPage* page, const QString& category)
    {
        QPushButton* chip = page->getChip(category);
        return chip ? chip->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt() : -1;
    }

    /// Counts of the matches of the engine: All, the categories and Pinned
    static std::map<QString, int> engineCounts(const QString& text, const QStringList& pinned)
    {
        std::map<QString, int> counts;
        for (const pdf::PDFDocumentToolSearchMatch& match : pdf::PDFDocumentToolSearch::search(text))
        {
            ++counts[QString()];
            ++counts[pdf::PDFDocumentToolCatalog::getCategoryId(match.tool->getCategory())];
            if (pinned.contains(match.tool->getId()))
            {
                ++counts[QStringLiteral("pinned")];
            }
        }
        return counts;
    }

    static pdf::PDFAppTab* homeTab(pdftest::AppToolsFixture& fixture)
    {
        return fixture.window ? fixture.window->getHomeTab() : nullptr;
    }

    static void hideBanner(pdftest::AppToolsFixture& fixture)
    {
        if (fixture.window && fixture.window->getHomeTab() && fixture.window->getHomeTab()->getBanner())
        {
            fixture.window->getHomeTab()->getBanner()->hide();
        }
        pdftest::AppDocumentFixture::settle();
    }

    static bool sendKey(QWidget* widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        if (!widget)
        {
            return false;
        }
        QTest::keyClick(widget, Qt::Key(key), modifiers);
        return true;
    }

    /// Accepted run of Compress after Merge (a pair of Then continue with and a
    /// run of the tool history)
    static pdf::PDFDatabaseError recordPairRun(pdf::PDFDatabase* database)
    {
        return pdf::PDFDocumentToolUsage(database).recordRun(QStringLiteral("optimize.compress"), QStringLiteral("pages.merge"));
    }
};

void AppToolsTest::catalogLayout()
{
    pdftest::AppToolsFixture fixture;
    AppToolsPageTestHelper::hideBanner(fixture);
    const QStringList pinned({ QStringLiteral("pages.merge"), QStringLiteral("convert.ocr"), QStringLiteral("protect.sign"),
                               QStringLiteral("protect.sanitize"), QStringLiteral("optimize.compress"), QStringLiteral("review.compare") });
    for (const QString& toolId : pinned)
    {
        QVERIFY(!fixture.tools().setPinned(toolId, true).isError());
    }

    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    // tools-grid 272:12862: main column 802, side column 340, gap 24
    QWidget* main = page->findChild<QWidget*>(QStringLiteral("mainColumn"));
    QWidget* side = page->findChild<QWidget*>(QStringLiteral("sideColumn"));
    QVERIFY(main && side);
    QCOMPARE(side->width(), 340);
    QCOMPARE(AppToolsPageTestHelper::rectIn(side, page).left() - AppToolsPageTestHelper::rectIn(main, page).right() - 1, 24);

    // toolbar 272:12864: the search 802 x 36, the chips 32 high 48 below its top
    QCOMPARE(page->getSearchField()->width(), 802);
    QCOMPARE(page->getSearchField()->height(), 36);
    for (QPushButton* chip : page->getChips())
    {
        QCOMPARE(chip->height(), 32);
    }
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getChip(QString()), main).top(), 48);
    QCOMPARE(page->getChips().size(), 8);

    // pinned 272:12876 at 104: six tiles 259 x 88 in two rows (gap 12)
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getPinnedSection(), main).top(), 104);
    const QList<pdf::PDFQuickActionTile*> tiles = page->getTiles();
    QCOMPARE(tiles.size(), 6);
    for (int i = 0; i < tiles.size(); ++i)
    {
        QCOMPARE(pdf::PDFAppToolPresentation::getToolId(tiles[i]), pinned[i]);
        QVERIFY2(AppToolsPageTestHelper::isNear(tiles[i]->width(), 259), qPrintable(QString::number(tiles[i]->width())));
        QCOMPARE(tiles[i]->height(), 88);
    }
    QCOMPARE(AppToolsPageTestHelper::rectIn(tiles[3], page->getTileGrid()).top(), 100);
    QVERIFY(AppToolsPageTestHelper::isNear(AppToolsPageTestHelper::rectIn(tiles[1], page->getTileGrid()).left(), 271));

    // all-tools 272:12888 at 346: the cards 393 wide in rows of two (gap 16 / 12), top aligned
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getAllToolsHeader(), main).top(), 346);
    const QList<pdf::PDFAppToolCategoryCard*> cards = page->getCategoryCards();
    QCOMPARE(cards.size(), 6);
    const int expectedHeights[] = { 748, 548, 173, 698, 371, 548 };
    const int expectedTops[] = { 0, 0, 760, 760, 1470, 1470 };
    for (int i = 0; i < cards.size(); ++i)
    {
        const QRect rect = AppToolsPageTestHelper::rectIn(cards[i], page->getCategoryGrid());
        QVERIFY2(AppToolsPageTestHelper::isNear(rect.width(), 393), qPrintable(QString::number(rect.width())));
        QCOMPARE(rect.left(), (i % 2) == 0 ? 0 : 409);
        QCOMPARE(rect.height(), expectedHeights[i]);
        QCOMPARE(rect.top(), expectedTops[i]);
        QCOMPARE(cards[i]->getHeader()->height(), 61);
        QCOMPARE(AppToolsPageTestHelper::rectIn(cards[i]->getHeader(), cards[i]).topLeft(), QPoint(1, 1));
        for (pdf::PDFToolRow* row : cards[i]->getRows())
        {
            QCOMPARE(row->height(), 48);
            QVERIFY2(AppToolsPageTestHelper::isNear(row->width(), 379), qPrintable(QString::number(row->width())));
        }
    }
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getCategoryGrid(), page->getAllToolsHeader()->parentWidget()).top(), 30);

    // The subgroups of Organize Pages (273:1658: 19 at 6, 273:1718: 25 at 227) and the first row at 27
    const QList<pdf::PDFLabel*> captions = cards[0]->getCaptions();
    QCOMPARE(captions.size(), 3);
    QCOMPARE(captions[0]->height(), 19);
    QCOMPARE(captions[1]->height(), 25);
    QWidget* list = cards[0]->findChild<QWidget*>(QStringLiteral("toolList"));
    QCOMPARE(AppToolsPageTestHelper::rectIn(captions[0], list).top(), 6);
    QCOMPARE(AppToolsPageTestHelper::rectIn(cards[0]->getRows().front(), list).top(), 27);
    QCOMPARE(AppToolsPageTestHelper::rectIn(captions[1], list).top(), 227);
    QVERIFY(cards[2]->getCaptions().isEmpty());

    // side 272:12942: the collections (rows 322 x 52 at 9 / 9, new collection 54), the recent tools at 504
    const QList<pdf::PDFCollectionRow*> collectionRows = page->getCollectionRows();
    QCOMPARE(collectionRows.size(), 7);
    QWidget* collectionsList = page->findChild<QWidget*>(QStringLiteral("collectionsList"));
    QCOMPARE(AppToolsPageTestHelper::rectIn(collectionRows.front(), collectionsList).topLeft(), QPoint(9, 9));
    for (pdf::PDFCollectionRow* row : collectionRows)
    {
        QCOMPARE(row->size(), QSize(322, 52));
    }
    QCOMPARE(page->getNewCollectionRow()->size(), QSize(322, 54));
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getNewCollectionRow(), collectionsList).top(), 387);
    QCOMPARE(collectionsList->height(), 450);
    QCOMPARE(AppToolsPageTestHelper::rectIn(collectionsList, side).top(), 30);
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->findChild<QWidget*>(QStringLiteral("recentTools")), side).top(), 504);

    // The recent tools: rows 322 x 48 (gap 2)
    const qint64 now = pdf::PDFAppHomeFormat::getCurrentTime();
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("optimize.compress"), now - 120000));
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("convert.ocr"), now - 3600000));
    QTRY_COMPARE(page->getRecentRows().size(), 2);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getRecentRows().front()->size(), QSize(322, 48));
    QCOMPARE(page->getRecentRows().front()->getName(), QStringLiteral("Compress"));
    // The times of Home ("Today 10:24"; Figma writes "2 minutes ago")
    QCOMPARE(page->getRecentRows().front()->getLastUsed(), pdf::PDFAppHomeFormat::formatTime(now - 120000, pdf::PDFAppHomeFormat::getCurrentTime()));
    QWidget* recentList = page->findChild<QWidget*>(QStringLiteral("recentToolsList"));
    QCOMPARE(AppToolsPageTestHelper::rectIn(page->getRecentRows()[1], recentList).top(), 59);

    fixture.screenshot(QStringLiteral("tools-catalog"));
}

void AppToolsTest::catalogMatchesFigma()
{
    // The 46 rows of 272:12821 (step6_figma_map.md 3): title, description, icon, subgroup, tag
    struct Row
    {
        const char* id;
        const char* subgroup;
        const char* title;
        const char* description;
        const char* icon;
        bool isAdvanced;
    };

    const Row rows[] = {
        { "pages.merge", "COMBINE & SPLIT", "Merge", "Combine several files into one PDF", "layers", false },
        { "pages.split", "COMBINE & SPLIT", "Split", "Separate a document into several files", "scissors", false },
        { "pages.extract", "COMBINE & SPLIT", "Extract Pages", "Save selected pages as a new PDF", "extract", false },
        { "pages.interleave", "COMBINE & SPLIT", "Interleave Pages", "Mix front and back sides of a scan", "repeat", false },
        { "pages.manage", "ARRANGE", "Manage Pages", "Reorder, group and arrange pages", "grid", false },
        { "pages.insert", "ARRANGE", "Insert Pages", "Add PDF pages, images or blank pages", "page-insert", false },
        { "pages.add-image", "ARRANGE", "Add Image as Page", "Insert a picture as a new page", "page-image", false },
        { "pages.remove", "ARRANGE", "Remove Pages", "Delete unwanted pages", "trash", false },
        { "pages.rotate", "ARRANGE", "Rotate Pages", "Turn pages in 90° steps", "rotate-cw", false },
        { "pages.crop", "PAGE SIZE", "Crop Pages", "Trim margins or set a crop area", "crop", false },
        { "pages.resize", "PAGE SIZE", "Resize Pages", "Change paper size, margins and scale", "expand", false },
        { "pages.boxes", "PAGE SIZE", "Set Page Boxes", "Media, crop, bleed, trim and art box", "crop-handles", true },
        { "convert.images-to-pdf", "CREATE", "Images to PDF", "Turn photos and scans into a PDF", "image", false },
        { "convert.blank", "CREATE", "Blank PDF", "Start a new document with empty pages", "file", false },
        { "convert.scan", "CREATE", "Scan to PDF", "Scan paper pages from a scanner", "scanner", false },
        { "convert.ocr", "RECOGNIZE", "Recognize Text (OCR)", "Make scanned pages searchable", "ocr", false },
        { "convert.pdf-to-images", "EXPORT", "PDF to Images", "Save pages as PNG, JPEG or TIFF", "download", false },
        { "convert.extract-text", "EXPORT", "Extract Text", "Save the document text as a text file", "file-text", false },
        { "convert.extract-images", "EXPORT", "Extract Images", "Save embedded pictures to a folder", "images", false },
        { "convert.audiobook", "EXPORT", "Create Audio Book", "Turn the document text into MP3", "headphones", false },
        { "edit.content", "", "Edit Content", "Edit or add text, images and graphics", "edit", false },
        { "edit.page-numbers", "", "Add Page Numbers", "Number pages in a chosen style", "hash", false },
        { "protect.password", "ENCRYPTION", "Protect with Password", "Require a password to open the PDF", "lock", false },
        { "protect.permissions", "ENCRYPTION", "Restrict Permissions", "Limit printing, copying and editing", "shield-lock", false },
        { "protect.certificate-encrypt", "ENCRYPTION", "Encrypt with Certificate", "Only the certificate holder can open it", "certificate-lock", false },
        { "protect.unlock", "ENCRYPTION", "Remove Password", "Remove encryption you have access to", "unlock", false },
        { "protect.sign", "SIGNATURES", "Sign with Certificate", "Add a digital signature", "sign-certificate", false },
        { "protect.timestamp", "SIGNATURES", "Add Timestamp", "Prove the document existed at a time", "timestamp", false },
        { "protect.sign-by-hand", "SIGNATURES", "Sign by Hand", "Draw, type or insert a signature", "signature", false },
        { "protect.certificates", "SIGNATURES", "Manage Certificates", "Create and trust digital IDs", "certificate", false },
        { "protect.redact", "PRIVACY", "Redact Content", "Permanently remove sensitive content", "redact", false },
        { "protect.sanitize", "PRIVACY", "Remove Hidden Data", "Strip metadata, comments and more", "hidden-data", false },
        { "protect.remove-links", "PRIVACY", "Remove External Links", "Delete links that leave the document", "link-off", false },
        { "optimize.compress", "FILE SIZE", "Compress", "Reduce the file size in one step", "minimize-2", false },
        { "optimize.images", "FILE SIZE", "Optimize Images", "Downsample and recompress images", "optimize-images", false },
        { "optimize.structure", "FILE SIZE", "Optimize Structure", "Clean up and recompress PDF objects", "optimize-structure", false },
        { "optimize.bitonal", "COLOR", "Convert to Black & White", "Make scans 1-bit and much smaller", "droplet", false },
        { "optimize.grayscale", "COLOR", "Convert Images to Grayscale", "Remove color from images", "sun", false },
        { "review.compare", "REVIEW", "Compare", "Find differences between two PDFs", "compare", false },
        { "review.measure", "REVIEW", "Measure", "Distances, areas, perimeters, angles", "ruler", false },
        { "review.output-preview", "PRINT PRODUCTION", "Output Preview", "Separations, overprint, color warnings", "separations", true },
        { "review.ink-coverage", "PRINT PRODUCTION", "Ink Coverage", "Ink usage of every page", "ink-coverage", true },
        { "review.soft-proof", "PRINT PRODUCTION", "Soft Proofing", "Simulate print colors, check gamut", "soft-proof", true },
        { "review.statistics", "DEVELOPER", "Document Statistics", "What takes up space in the file", "bar-chart-2", true },
        { "review.object-inspector", "DEVELOPER", "Object Inspector", "Browse the internal PDF objects", "object", true },
        { "review.report", "DEVELOPER", "Document Report", "Structure, scripts, destinations as a report", "report", true },
    };

    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    QCOMPARE(int(std::size(rows)), pdf::PDFDocumentToolCatalog::getToolCount());

    // The rows of the page in the order of the cards, the captions of the subgroups
    QStringList pageOrder;
    QStringList captionTexts;
    for (pdf::PDFAppToolCategoryCard* card : page->getCategoryCards())
    {
        for (pdf::PDFToolRow* row : card->getRows())
        {
            pageOrder << pdf::PDFAppToolPresentation::getToolId(row);
        }
        for (pdf::PDFLabel* caption : card->getCaptions())
        {
            QVERIFY(caption->font().capitalization() == QFont::AllUppercase);
            captionTexts << caption->text().toUpper();
        }
    }

    QStringList figmaCaptions;
    for (int i = 0; i < int(std::size(rows)); ++i)
    {
        const Row& expected = rows[i];
        QCOMPARE(pageOrder.value(i), QString::fromLatin1(expected.id));
        pdf::PDFToolRow* row = page->findToolRow(QString::fromLatin1(expected.id));
        QVERIFY(row);
        QCOMPARE(row->getName(), QString::fromUtf8(expected.title));
        QCOMPARE(row->getDescription(), QString::fromUtf8(expected.description));
        QCOMPARE(row->getIconName(), QString::fromLatin1(expected.icon));
        QCOMPARE(row->getTag(), expected.isAdvanced ? QStringLiteral("Advanced") : QString());
        const pdf::PDFDocumentToolCatalogEntry* tool = pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(expected.id));
        QCOMPARE(row->getHue(), pdf::PDFAppToolPresentation::getCategoryHue(tool->getCategory()));
        const QString subgroup = QString::fromUtf8(expected.subgroup);
        QCOMPARE(tool->getSubgroupTitle().toUpper(), subgroup);
        if (!subgroup.isEmpty() && (i == 0 || subgroup != QString::fromUtf8(rows[i - 1].subgroup) ||
                                    tool->getCategory() != pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(rows[i - 1].id))->getCategory()))
        {
            figmaCaptions << subgroup;
        }
    }
    QCOMPARE(captionTexts, figmaCaptions);

    // The category headers and their counts (273:1648 ...)
    struct Category
    {
        const char* title;
        const char* description;
        const char* icon;
        const char* count;
    };
    const Category categories[] = {
        { "Organize Pages", "Merge, split, reorder and resize pages", "pages", "12 tools" },
        { "Create & Convert", "Create PDFs and get content out of them", "repeat", "8 tools" },
        { "Edit", "Change the content of pages", "edit", "2 tools" },
        { "Protect & Sign", "Protect, sign and remove sensitive data", "shield", "11 tools" },
        { "Optimize", "Make files smaller, cleaner and healthier", "minimize-2", "5 tools" },
        { "Review & Inspect", "Compare, measure, proof and look inside", "columns", "8 tools" },
    };
    const QList<pdf::PDFAppToolCategoryCard*> cards = page->getCategoryCards();
    QCOMPARE(cards.size(), int(std::size(categories)));
    for (int i = 0; i < cards.size(); ++i)
    {
        QCOMPARE(cards[i]->getHeader()->getTitle(), QString::fromLatin1(categories[i].title));
        QCOMPARE(cards[i]->getHeader()->getDescription(), QString::fromLatin1(categories[i].description));
        QCOMPARE(cards[i]->getHeader()->getIconName(), QString::fromLatin1(categories[i].icon));
        QCOMPARE(cards[i]->getHeader()->getCount(), QString::fromLatin1(categories[i].count));
    }

    // The texts of the page (272:12858, 272:12867, 272:12889, 272:12943, 272:12958)
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Tools"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Every PDF4QT tool in one place. Search, browse by category, and pin the ones you use most."));
    QCOMPARE(page->getSearchField()->placeholderText(), QString::fromUtf8("Search tools by name, action or keyword…"));
    QStringList chipTexts;
    for (QPushButton* chip : page->getChips())
    {
        chipTexts << chip->text().replace(QStringLiteral("&&"), QStringLiteral("&"));
    }
    QCOMPARE(chipTexts, QStringList({ "All", "Pinned", "Pages", "Convert", "Edit", "Protect & Sign", "Optimize", "Review" }));
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QStringLiteral("All tools"));
    QCOMPARE(page->getAllToolsHint()->text(), QStringLiteral("46 tools in 6 categories"));
    QCOMPARE(page->getManageLink()->text(), QStringLiteral("Manage"));
    QCOMPARE(page->getClearRecentLink()->text(), QStringLiteral("Clear"));
    QCOMPARE(page->getNewCollectionRow()->getName(), QString::fromUtf8("New collection…"));
    QCOMPARE(page->getNewCollectionRow()->getMeta(), QStringLiteral("Group the tools you use for one task"));
    QVERIFY(page->getNewCollectionRow()->isCreateVariant());
    QCOMPARE(page->getNewCollectionRow()->getIconName(), QStringLiteral("plus-circle"));
}

void AppToolsTest::chipsAndCounts()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("convert.ocr"), true).isError());
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // Without a search: the counts of the catalog (272:12867), every chip enabled, All checked
    const std::pair<const char*, int> expected[] = { { "", 46 }, { "pinned", 2 }, { "pages", 12 }, { "convert", 8 }, { "edit", 2 }, { "protect", 11 }, { "optimize", 5 }, { "review", 8 } };
    for (const auto& [category, count] : expected)
    {
        QCOMPARE(AppToolsPageTestHelper::chipCount(page, QString::fromLatin1(category)), count);
        QVERIFY(page->getChip(QString::fromLatin1(category))->isEnabled());
    }
    QVERIFY(page->getChip(QString())->isChecked());

    // The search "image": the counts of the engine, the chips without a match disabled (530:2957)
    fixture.navigate(QStringLiteral("/tools?category=convert&q=image"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    const std::map<QString, int> counts = AppToolsPageTestHelper::engineCounts(QStringLiteral("image"), { QStringLiteral("pages.merge"), QStringLiteral("convert.ocr") });
    for (QPushButton* chip : page->getChips())
    {
        const QString category = chip == page->getChip(QString()) ? QString() : chip->objectName().mid(5);
        const int count = counts.count(category) ? counts.at(category) : 0;
        QCOMPARE(AppToolsPageTestHelper::chipCount(page, category), count);
        QCOMPARE(chip->isEnabled(), count > 0 || category == QStringLiteral("convert"));
    }
    QVERIFY(page->getChip(QStringLiteral("convert"))->isChecked());

    // A pin changes the count of Pinned; outside of a search Pinned with 0 stays enabled
    fixture.navigate(QStringLiteral("/tools"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.merge"), false));
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("convert.ocr"), false));
    QTRY_COMPARE(AppToolsPageTestHelper::chipCount(page, QStringLiteral("pinned")), 0);
    QVERIFY(page->getChip(QStringLiteral("pinned"))->isEnabled());
}

void AppToolsTest::searchFiltersInRoute()
{
    pdftest::AppToolsFixture fixture;
    QPointer<pdf::PDFAppToolsPage> page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    pdf::PDFAppTab* tab = fixture.currentTab();
    const int historyCount = tab->getHistory().getCount();

    // The first text: a new entry of the history, the new page has the text and the focus
    page->getSearchField()->setFocus(Qt::TabFocusReason);
    QTest::keyClicks(page->getSearchField(), QStringLiteral("merge "));
    QTRY_COMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=merge"));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page.isNull());
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    QCOMPARE(tab->getHistory().getCount(), historyCount + 1);
    QCOMPARE(page->getSearchField()->text(), QStringLiteral("merge "));
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getSearchField()));
    QCOMPARE(AppToolsPageTestHelper::visibleIds(page).front(), QStringLiteral("pages.merge"));
    QVERIFY(page->getPinnedSection()->isHidden());

    // Further typing replaces the route of the same page
    QPointer<pdf::PDFAppToolsPage> filtered = page;
    QTest::keyClicks(page->getSearchField(), QStringLiteral("pdf"));
    QTRY_COMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=merge%20pdf"));
    QCOMPARE(fixture.page<pdf::PDFAppToolsPage>(), filtered.data());
    QCOMPARE(tab->getHistory().getCount(), historyCount + 1);

    // A chip: the route keeps the text, the same page
    page->setFilter(QStringLiteral("protect"), QStringLiteral("sign"));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?category=protect&q=sign"));
    QCOMPARE(fixture.page<pdf::PDFAppToolsPage>(), filtered.data());
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QString::fromUtf8("Tools matching “sign” in Protect & Sign"));
    QCOMPARE(page->getCategoryGrid()->getVisibleCards().size(), 1);
    QCOMPARE(page->getCategoryGrid()->getVisibleCards().front()->width(), page->getCategoryGrid()->width());

    // Back: the whole catalog; Forward: the filter again
    fixture.window->getNavigator()->goBack(tab);
    pdftest::AppDocumentFixture::settle();
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page && page->getCategory().isEmpty() && page->getSearchText().isEmpty());
    QVERIFY(page->getSearchField()->text().isEmpty());
    QVERIFY(!page->getPinnedSection()->isHidden());
    fixture.window->getNavigator()->goForward(tab);
    pdftest::AppDocumentFixture::settle();
    page = fixture.page<pdf::PDFAppToolsPage>();
    QCOMPARE(page->getCategory(), QStringLiteral("protect"));
    QCOMPARE(page->getSearchField()->text(), QStringLiteral("sign"));

    // A category without a text (a missing frame): one card 802
    fixture.navigate(QStringLiteral("/tools?category=pages"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QCOMPARE(page->getCategoryGrid()->getVisibleCards().size(), 1);
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QStringLiteral("Tools in Pages"));
    QCOMPARE(page->getVisibleToolRows().size(), 12);
    QCOMPARE(page->getVisibleToolRows().front()->width(), 788);

    // 530:2957: the texts of the search in one category, show all N matches
    fixture.navigate(QStringLiteral("/tools?category=convert&q=image"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    const std::map<QString, int> counts = AppToolsPageTestHelper::engineCounts(QStringLiteral("image"), QStringList());
    const int convert = counts.at(QStringLiteral("convert"));
    const int total = counts.at(QString());
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QString::fromUtf8("Tools matching “image” in Convert"));
    QVERIFY(!page->getSearchHint()->isHidden());
    QVERIFY(page->getSearchHintText()->text().startsWith(QString::fromUtf8("%1 tools in Convert match “image”.").arg(convert)));
    QVERIFY(page->getSearchHintText()->text().contains(QStringLiteral("%1 more match in other categories: Pages ").arg(total - convert)));
    QCOMPARE(page->getShowAllLink()->text(), QStringLiteral("show all %1 matches").arg(total));
    QVERIFY(page->getAllToolsHint()->isHidden());
    QVERIFY(page->getPinnedSection()->isHidden());
    QCOMPARE(page->getVisibleToolRows().size(), convert);
    // Captions without a match are hidden (RECOGNIZE only when OCR matches)
    pdf::PDFAppToolCategoryCard* convertCard = page->getCategoryCards()[1];
    QVERIFY(!convertCard->isHidden());
    page->getShowAllLink()->click();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=image"));
    QCOMPARE(page->getVisibleToolRows().size(), total);
    QVERIFY(page->getSearchHint()->isHidden());
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QString::fromUtf8("Tools matching “image”"));

    // The collection (Q1) excludes the filter; a filter of a collection page is a new entry
    fixture.navigate(QStringLiteral("/tools?collection=fix-a-scan"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QCOMPARE(page->getCollection(), QStringLiteral("fix-a-scan"));
    const int collectionHistory = tab->getHistory().getCount();
    page->setFilter(QString(), QStringLiteral("rotate"));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=rotate"));
    QCOMPARE(tab->getHistory().getCount(), collectionHistory + 1);

    // Clearing the filter of a filtered page replaces its route with the whole catalog
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->setFilter(QString(), QString());
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    QVERIFY(!page->getPinnedSection()->isHidden());
    QCOMPARE(page->getVisibleToolRows().size(), 46);

    // Esc in the search field clears the search; the whole catalog is the entry before
    // the search, so the tab goes Back to it (O25, clearFilterGoesBack)
    fixture.navigate(QStringLiteral("/tools?q=crop"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getSearchField()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(page->getSearchField(), Qt::Key_Escape);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    QVERIFY(page->getSearchField()->text().isEmpty());
}

void AppToolsTest::searchReason()
{
    pdftest::AppToolsFixture fixture;
    fixture.navigate(QStringLiteral("/tools?q=combine"));
    pdf::PDFAppToolsPage* page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);

    // Q3: a tool found only by a keyword says which one, the others keep their description
    int reasons = 0;
    for (const pdf::PDFDocumentToolSearchMatch& match : pdf::PDFDocumentToolSearch::search(QStringLiteral("combine")))
    {
        pdf::PDFToolRow* row = page->findToolRow(match.tool->getId());
        QVERIFY(row && !row->isHidden());
        if (match.field == pdf::PDFDocumentToolSearchMatch::Field::Keyword)
        {
            QCOMPARE(row->getDescription(), QString::fromUtf8("matches “%1”").arg(match.keyword));
            ++reasons;
        }
        else
        {
            QCOMPARE(row->getDescription(), match.tool->getDescription());
        }
    }
    QVERIFY(reasons > 0);
    QCOMPARE(page->findToolRow(QStringLiteral("pages.merge"))->getDescription(), QString::fromUtf8("matches “combine”"));

    // Without the search the descriptions come back
    page->setFilter(QString(), QString());
    QCOMPARE(page->findToolRow(QStringLiteral("pages.merge"))->getDescription(), QStringLiteral("Combine several files into one PDF"));
}

void AppToolsTest::searchHostileTexts()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture, QStringLiteral("/tools?q=x"));
    QVERIFY(page);

    QStringList texts;
    texts << QStringLiteral("a\x01\x07" "b") << (QString(QChar(0x202E)) + QStringLiteral("merge") + QChar(0x202C))
          << QString(10 * 1024, QLatin1Char('m')) << QString(5000, QChar(0x4E2D)) << (QStringLiteral("merge") + QChar(0xD800))
          << QStringLiteral("%00%2F..") << QStringLiteral("<b>merge</b>&amp;") << QString(300, QLatin1Char(' '));
    QRandomGenerator random(42);
    for (int i = 0; i < 50; ++i)
    {
        QString text;
        const int length = int(random.bounded(64));
        for (int j = 0; j < length; ++j)
        {
            text += QChar(char16_t(random.bounded(1, 0xFFFF)));
        }
        texts << text;
    }

    for (const QString& text : std::as_const(texts))
    {
        QVERIFY(page->setFilter(QString(), text));
        pdftest::AppDocumentFixture::settle();
        page = fixture.page<pdf::PDFAppToolsPage>();
        QVERIFY(page);
        const pdf::PDFAppRoute route = pdf::PDFAppRoute::parse(fixture.currentRoute());
        QVERIFY2(route.isValid(), qPrintable(fixture.currentRoute().left(64)));
        QVERIFY(route.getQueryValue(QStringLiteral("q")).size() <= pdf::PDFSearchQuery::MAXIMUM_LENGTH);
        QCOMPARE(page->getSearchText(), route.getQueryValue(QStringLiteral("q")));
        for (QChar character : page->getSearchText())
        {
            QVERIFY(character.unicode() >= 0x20 && character.category() != QChar::Other_Format);
        }
        if (!page->getSearchText().isEmpty())
        {
            // The text is a plain text of the header, never a rich text
            QVERIFY(page->getAllToolsHeader()->getTitleLabel()->textFormat() != Qt::RichText);
        }
    }

    // The route of a hostile text opens the same filter again
    page->setFilter(QStringLiteral("pages"), QStringLiteral("<i>crop</i>"));
    const QString route = fixture.currentRoute();
    fixture.navigate(route);
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    QCOMPARE(page->getCategory(), QStringLiteral("pages"));
}

void AppToolsTest::searchNoResults()
{
    pdftest::AppToolsFixture fixture;
    fixture.navigate(QStringLiteral("/tools?q=zzqqxx"));
    pdf::PDFAppToolsPage* page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    QVERIFY(!page->getNoResults()->isHidden());
    QVERIFY(page->getCategoryGrid()->isHidden());
    QCOMPARE(page->getNoResults()->findChild<QLabel*>(QStringLiteral("noResultsTitle"))->text(), QString::fromUtf8("No tools match “zzqqxx”"));
    QVERIFY(page->getNearestSection()->isHidden());
    QVERIFY(!page->getClearSearchButton()->isHidden());
    QVERIFY(!page->getSearchEverywhereButton()->isHidden());
    for (QPushButton* chip : page->getChips())
    {
        QCOMPARE(chip->isEnabled(), chip == page->getChip(QString()));
    }

    // Clear Search: the whole catalog, the focus in the field
    page->getClearSearchButton()->click();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    QVERIFY(page->getNoResults()->isHidden());
    QVERIFY(page->getSearchField()->text().isEmpty());

    // Search Everywhere: the global search with the text
    fixture.navigate(QStringLiteral("/tools?q=zzqqxx"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getSearchEverywhereButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/search?q=zzqqxx"));

    // Pinned without pins: the empty state without the buttons
    fixture.navigate(QStringLiteral("/tools?category=pinned"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(!page->getNoResults()->isHidden());
    QCOMPARE(page->getNoResults()->findChild<QLabel*>(QStringLiteral("noResultsTitle"))->text(), QStringLiteral("No pinned tools"));
    QVERIFY(page->getClearSearchButton()->isHidden());
    fixture.screenshot(QStringLiteral("tools-no-pins"));

    // A search in one category without a match, while other categories have matches:
    // the hint above offers them, the card never says that no category matches (RFW6)
    fixture.navigate(QStringLiteral("/tools?category=pages&q=ocr"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(!page->getNoResults()->isHidden());
    QVERIFY(!page->getSearchHint()->isHidden());
    QVERIFY(page->getSearchHintText()->text().contains(QStringLiteral("No tools in")));
    const QString otherText = page->getNoResults()->findChild<QLabel*>(QStringLiteral("noResultsText"))->text();
    QVERIFY2(!otherText.contains(QStringLiteral("any category")), qPrintable(otherText));

    // No category matches: the card says so
    fixture.navigate(QStringLiteral("/tools?category=pages&q=zzqqxx"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(!page->getNoResults()->isHidden());
    QVERIFY(page->getSearchHint()->isHidden());
    QVERIFY(page->getNoResults()->findChild<QLabel*>(QStringLiteral("noResultsText"))->text().contains(QStringLiteral("any category")));
}

void AppToolsTest::clearFilterGoesBack()
{
    // O25: a filter cleared to the route of the previous entry of the history goes Back
    // to it instead of replacing the route - the history never has two equal entries
    pdftest::AppToolsFixture fixture;
    QPointer<pdf::PDFAppToolsPage> page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    pdf::PDFAppTab* tab = fixture.currentTab();
    const pdf::PDFAppHistory& history = tab->getHistory();
    const int count = history.getCount();
    const int index = history.getCurrentIndex();
    QVERIFY(index > 0);
    const QString beforeTools = history.getEntry(index - 1)->route.toString();
    QVERIFY(beforeTools != QStringLiteral("/tools"));

    // Typed: the first text is a new entry, further typing replaces it
    page->getSearchField()->setFocus(Qt::TabFocusReason);
    QTest::keyClicks(page->getSearchField(), QStringLiteral("merge"));
    QTRY_COMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=merge"));
    pdftest::AppDocumentFixture::settle();
    page = fixture.page<pdf::PDFAppToolsPage>();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("x"));
    QTRY_COMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=mergex"));
    QCOMPARE(history.getCount(), count + 1);

    // The text deleted by the keys: Back to the catalog before the search, the focus
    // stays in the field of the page of that entry
    QPointer<pdf::PDFAppToolsPage> filtered = page;
    page->getSearchField()->selectAll();
    QTest::keyClick(page->getSearchField(), Qt::Key_Backspace);
    QTRY_COMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(filtered.isNull());
    QCOMPARE(history.getCurrentIndex(), index);
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page && page->getSearchField()->text().isEmpty());
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getSearchField()));

    // Forward (documented): the filtered entry with its last text is still the next entry
    QVERIFY(history.canGoForward());
    QCOMPARE(history.getEntry(index + 1)->route.toString(), QStringLiteral("/tools?q=mergex"));
    fixture.window->getNavigator()->goForward(tab);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=mergex"));
    QCOMPARE(fixture.page<pdf::PDFAppToolsPage>()->getSearchField()->text(), QStringLiteral("mergex"));
    fixture.window->getNavigator()->goBack(tab);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));

    // Back then leads to the page before the search (no second /tools entry)
    fixture.window->getNavigator()->goBack(tab);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), beforeTools);
    fixture.window->getNavigator()->goForward(tab);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));

    // The chip All after a chip and Clear Search: Back as well
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getChip(QStringLiteral("pages"))->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?category=pages"));
    QCOMPARE(history.getCurrentIndex(), index + 1);
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getChip(QString())->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    QCOMPARE(history.getCurrentIndex(), index);

    fixture.navigate(QStringLiteral("/tools?q=zzqqxx"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QPointer<QWidget> replacedField = page->getSearchField();
    page->getClearSearchButton()->setFocus(Qt::TabFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getClearSearchButton()));
    page->getClearSearchButton()->click();

    // The replaced page (hidden, deleted later) never takes the focus (RFW6): the
    // page of the entry gets it from the event loop
    QVERIFY(replacedField && !page->isVisible());
    QVERIFY(QApplication::focusWidget() != replacedField.data());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    QCOMPARE(history.getCurrentIndex(), index);
    page = fixture.page<pdf::PDFAppToolsPage>();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getSearchField()));

    // Another previous entry: the route is replaced as before (the first change a
    // Push, then replaceRoute)
    fixture.navigate(QStringLiteral("/tools?category=convert"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    const int convertCount = history.getCount();
    const int convertIndex = history.getCurrentIndex();
    QVERIFY(page->setFilter(QStringLiteral("convert"), QStringLiteral("image")));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?category=convert&q=image"));
    QVERIFY(page->setFilter(QStringLiteral("convert"), QString()));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?category=convert"));
    QCOMPARE(fixture.page<pdf::PDFAppToolsPage>(), page.data());
    QCOMPARE(history.getCount(), convertCount);
    QCOMPARE(history.getCurrentIndex(), convertIndex);
    QCOMPARE(history.getEntry(convertIndex - 1)->route.toString(), QStringLiteral("/tools"));
}

void AppToolsTest::nearestTools()
{
    // O25, tools.md 9.1 point 4: a search without a match offers up to three nearest
    // tools (PDFDocumentToolSearch::searchNearest) as standard tool rows
    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("protect.timestamp"), true).isError());
    fixture.navigate(QStringLiteral("/tools?q=tiemstmap"));
    QPointer<pdf::PDFAppToolsPage> page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();
    auto ids = [](const pdf::PDFAppToolsPage* shown)
    {
        QStringList result;
        for (pdf::PDFToolRow* row : shown->getNearestRows())
        {
            result << pdf::PDFAppToolPresentation::getToolId(row);
        }
        return result;
    };

    QVERIFY(!page->getNoResults()->isHidden());
    QVERIFY(!page->getClearSearchButton()->isHidden());
    QVERIFY(!page->getSearchEverywhereButton()->isHidden());
    QVERIFY(page->getNearestSection()->isVisible());
    QCOMPARE(ids(page), QStringList({ QStringLiteral("protect.timestamp") }));
    pdf::PDFAppSectionHeader* header = page->getNearestSection()->findChild<pdf::PDFAppSectionHeader*>(QStringLiteral("nearestToolsHeader"));
    QVERIFY(header);
    QCOMPARE(header->getTitleLabel()->text(), QStringLiteral("Closest tools"));
    pdf::PDFToolRow* timestamp = page->getNearestRows().front();
    QVERIFY(timestamp->isPinned());
    QVERIFY(timestamp->isVisible());
    QCOMPARE(timestamp->getStarButton()->focusPolicy(), Qt::NoFocus);
    fixture.screenshot(QStringLiteral("tools-nearest"));

    // A query of more words: the order of the core; the same tools keep the rows
    QVERIFY(page->setFilter(QString(), QStringLiteral("compress scan quality")));
    QCOMPARE(ids(page), QStringList({ "optimize.images", "convert.scan", "optimize.compress" }));
    QPointer<pdf::PDFToolRow> first = page->getNearestRows().front();
    QVERIFY(page->setFilter(QString(), QStringLiteral("compress scan quality qqq")));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(ids(page), QStringList({ "optimize.images", "convert.scan", "optimize.compress" }));
    QCOMPARE(page->getNearestRows().front(), first.data());

    // Nothing close enough: no section, only the empty state with its buttons
    QVERIFY(page->setFilter(QString(), QStringLiteral("xyzq")));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page->getNearestSection()->isHidden());
    QVERIFY(page->getNearestRows().isEmpty());
    QVERIFY(first.isNull());
    QVERIFY(!page->getNoResults()->isHidden());

    // A match hides the section
    QVERIFY(page->setFilter(QString(), QStringLiteral("spliting")));
    QCOMPARE(ids(page), QStringList({ QStringLiteral("pages.split") }));
    QVERIFY(page->setFilter(QString(), QStringLiteral("split")));
    QVERIFY(page->getNearestSection()->isHidden());
    QVERIFY(page->getNoResults()->isHidden());

    // A category without a match anywhere: the nearest tools of all categories
    fixture.navigate(QStringLiteral("/tools?category=review&q=spliting"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QCOMPARE(ids(page), QStringList({ QStringLiteral("pages.split") }));

    // The star writes the pin (one write, the row follows)
    page->getNearestRows().front()->getStarButton()->click();
    QTRY_VERIFY(fixture.tools().isPinned(QStringLiteral("pages.split")));
    QVERIFY(page->getNearestRows().front()->isPinned());

    // The keyboard: the rows are one stop of Tab, the arrows move, Enter opens the tool
    fixture.navigate(QStringLiteral("/tools?q=converting"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getNearestRows().size(), 2);
    int stops = 0;
    for (pdf::PDFToolRow* row : page->getNearestRows())
    {
        stops += row->focusPolicy() == Qt::StrongFocus ? 1 : 0;
    }
    QCOMPARE(stops, 1);
    page->getNearestRows().front()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getNearestRows().at(1)));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/optimize.grayscale"));

    // A click opens the tool; a collection never shows the section
    fixture.navigate(QStringLiteral("/tools?q=tiemstmap"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getNearestRows().front()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/protect.timestamp"));
    fixture.navigate(QStringLiteral("/tools?collection=fix-a-scan"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page->getNearestSection()->isHidden());

    // Right-to-left, both themes: the rows fill the card, no leak color
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        QApplication::setLayoutDirection(direction);
        for (const bool isDark : { false, true })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
            fixture.navigate(QStringLiteral("/tools?q=converting"));
            page = fixture.page<pdf::PDFAppToolsPage>();
            pdftest::AppDocumentFixture::settle();
            const QWidget* list = page->getNearestSection()->findChild<QWidget*>(QStringLiteral("nearestToolsList"));
            QVERIFY(list);
            for (pdf::PDFToolRow* row : page->getNearestRows())
            {
                QCOMPARE(row->layoutDirection(), direction);
                QVERIFY(list->rect().contains(AppToolsPageTestHelper::rectIn(row, list)));
                QVERIFY(row->width() >= list->width() - 2 * 9 - 1);
            }
            const QImage image = AppToolsPageTestHelper::render(page, isDark ? 2.0 : 1.0);
            QCOMPARE(AppToolsPageTestHelper::countLeakPixels(image), 0);
            const QPoint inside = AppToolsPageTestHelper::rectIn(list, page).topLeft() + QPoint(list->width() / 2, 4);
            QVERIFY(AppToolsPageTestHelper::isClose(AppToolsPageTestHelper::pixel(image, inside), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface)));
            fixture.screenshot(QStringLiteral("tools-nearest-%1-%2").arg(direction == Qt::RightToLeft ? "rtl" : "ltr", isDark ? "dark" : "light"));
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolsTest::pinByStarAndMenu()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    QSignalSpy spy(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);

    // The star of a row: one write, the tile appears, the star of the tile is on
    pdf::PDFToolRow* row = page->findToolRow(QStringLiteral("review.compare"));
    row->getStarButton()->click();
    QVERIFY(row->isPinned());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("review.compare") }));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page->findTile(QStringLiteral("review.compare")));
    QVERIFY(page->findTile(QStringLiteral("review.compare"))->isPinned());
    int pinNotifications = 0;
    for (const QList<QVariant>& arguments : std::as_const(spy))
    {
        pinNotifications += arguments.value(0).toString() == pdf::PDFToolRepository::getPinTopic() ? 1 : 0;
    }
    QCOMPARE(pinNotifications, 1);

    // The star of the tile unpins: the row follows, the tile goes
    page->findTile(QStringLiteral("review.compare"))->getStarButton()->click();
    QVERIFY(!row->isPinned());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->findTile(QStringLiteral("review.compare")));
    QVERIFY(fixture.tools().getPinnedTools().isEmpty());

    // The context menu: Pin, then Unpin
    pdf::PDFAppToolMenu* menu = page->showToolMenu(QStringLiteral("pages.split"), QPoint(100, 100));
    QVERIFY(menu);
    QCOMPARE(menu->getPinAction()->text(), QStringLiteral("Pin"));
    menu->getPinAction()->trigger();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page->findToolRow(QStringLiteral("pages.split"))->isPinned());
    QVERIFY(page->findTile(QStringLiteral("pages.split")));
    menu = page->showToolMenu(QStringLiteral("pages.split"), QPoint(100, 100));
    QCOMPARE(menu->getPinAction()->text(), QStringLiteral("Unpin"));
    menu->getPinAction()->trigger();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->findToolRow(QStringLiteral("pages.split"))->isPinned());

    // Shift+F10 / the menu key on a row opens its menu (tools.md 9.7): the
    // platform sends the context menu event of the keyboard
    pdf::PDFToolRow* split = page->findToolRow(QStringLiteral("pages.split"));
    split->setFocus(Qt::TabFocusReason);
    QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, split->rect().center(), split->mapToGlobal(split->rect().center()));
    QApplication::sendEvent(split, &keyboardMenu);
    QTRY_VERIFY(page->getToolMenu() && page->getToolMenu()->isVisible());
    QCOMPARE(page->getToolMenu()->getToolId(), QStringLiteral("pages.split"));
    page->getToolMenu()->hide();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->getToolMenu());
    QVERIFY(!page->showToolMenu(QStringLiteral("pages.unknown"), QPoint()));
}

void AppToolsTest::pinnedOrderAndEmpty()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // No pin: the empty state of the section (a missing frame of Figma)
    QVERIFY(page->getTiles().isEmpty());
    QVERIFY(page->getTileGrid()->isHidden());
    QVERIFY(!page->getPinnedEmpty()->parentWidget()->isHidden());
    QCOMPARE(page->getPinnedEmpty()->getTitle(), QStringLiteral("No pinned tools"));

    // The tiles in the order of the pins
    const QStringList order({ QStringLiteral("review.compare"), QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") });
    QVERIFY(fixture.writeFromOtherConnection([order](pdf::PDFDatabase* database)
                                             {
                                                 pdf::PDFToolRepository repository(database);
                                                 for (const QString& toolId : order)
                                                 {
                                                     const pdf::PDFDatabaseError error = repository.setPinned(toolId, true);
                                                     if (error.isError())
                                                     {
                                                         return error;
                                                     }
                                                 }
                                                 return repository.setPinnedOrder(order);
                                             },
                                             pdf::PDFToolRepository::getPinTopic()));
    QTRY_COMPARE(page->getTiles().size(), 3);
    for (int i = 0; i < order.size(); ++i)
    {
        QCOMPARE(pdf::PDFAppToolPresentation::getToolId(page->getTiles()[i]), order[i]);
    }
    QVERIFY(page->getPinnedEmpty()->parentWidget()->isHidden());
    QCOMPARE(page->findTile(QStringLiteral("protect.sanitize")), nullptr);

    // Pinned filter: the pinned rows in their categories
    page->setFilter(QStringLiteral("pinned"), QString());
    page = fixture.page<pdf::PDFAppToolsPage>();
    QCOMPARE(AppToolsPageTestHelper::visibleIds(page), QStringList({ "pages.merge", "optimize.compress", "review.compare" }));
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QStringLiteral("Pinned tools"));
    QCOMPARE(page->getAllToolsHint()->text(), QStringLiteral("3 tools"));

    // The tile of Remove Hidden Data has the short description of 272:12885
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("protect.sanitize")));
    page->setFilter(QString(), QString());
    QTRY_VERIFY(page->findTile(QStringLiteral("protect.sanitize")));
    QCOMPARE(page->findTile(QStringLiteral("protect.sanitize"))->getDescription(), QStringLiteral("Strip metadata"));
}

void AppToolsTest::pinWriteRefused()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // A fault of this connection: every write of tool_pin is refused
    QVERIFY(!fixture.context->getDatabase()->getConnection()->execute("CREATE TEMP TRIGGER fault_pin_insert BEFORE INSERT ON tool_pin BEGIN SELECT RAISE(ABORT, 'injected fault'); END").isError());
    QVERIFY(!fixture.context->getDatabase()->getConnection()->execute("CREATE TEMP TRIGGER fault_pin_update BEFORE UPDATE ON tool_pin BEGIN SELECT RAISE(ABORT, 'injected fault'); END").isError());

    pdf::PDFToolRow* row = page->findToolRow(QStringLiteral("pages.merge"));
    row->getStarButton()->click();
    QVERIFY(!row->isPinned());
    QVERIFY(!page->getStatusLabel()->isHidden());
    QVERIFY(page->getStatusLabel()->text().startsWith(QStringLiteral("The pin could not be saved")));
    QVERIFY(fixture.tools().getPinnedTools().isEmpty());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->findTile(QStringLiteral("pages.merge")));

    // The menu too
    QVERIFY(!page->setToolPinned(QStringLiteral("pages.split"), true));
    QVERIFY(!page->findToolRow(QStringLiteral("pages.split"))->isPinned());

    // The fault removed: the pin is written
    QVERIFY(!fixture.context->getDatabase()->getConnection()->execute("DROP TRIGGER temp.fault_pin_insert").isError());
    QVERIFY(!fixture.context->getDatabase()->getConnection()->execute("DROP TRIGGER temp.fault_pin_update").isError());
    QVERIFY(page->setToolPinned(QStringLiteral("pages.split"), true));
    QVERIFY(page->findToolRow(QStringLiteral("pages.split"))->isPinned());
}

void AppToolsTest::recentToolsAndClear()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // Phase 1: no recent tool, Clear hidden
    QVERIFY(page->getRecentRows().isEmpty());
    QVERIFY(!page->getRecentEmpty()->isHidden());
    QVERIFY(page->getClearRecentLink()->isHidden());
    QVERIFY(!page->openClearRecentDialog());

    // Six uses: the five most recent in their order
    const qint64 now = pdf::PDFAppHomeFormat::getCurrentTime();
    const QStringList tools({ "pages.merge", "pages.split", "convert.ocr", "review.compare", "protect.redact", "optimize.compress" });
    for (int i = 0; i < tools.size(); ++i)
    {
        QVERIFY(fixture.recordUsageFromOtherConnection(tools[i], now - qint64(tools.size() - i) * 60000));
    }
    QTRY_COMPARE(page->getRecentRows().size(), pdf::PDFAppToolsPage::RECENT_TOOLS_LIMIT);
    QCOMPARE(page->getRecentRows().front()->getName(), QStringLiteral("Compress"));
    QCOMPARE(pdf::PDFAppToolPresentation::getToolId(page->getRecentRows().back()), QStringLiteral("pages.split"));
    QVERIFY(!page->getClearRecentLink()->isHidden());
    QVERIFY(page->getRecentEmpty()->isHidden());

    // Pins, collections and pairs (the history of Then continue with) are kept by Clear (O24)
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Mine"), QString(), { QStringLiteral("pages.merge") }).isError());
    QVERIFY(fixture.writeFromOtherConnection(AppToolsPageTestHelper::recordPairRun, pdf::PDFToolRepository::getPairTopic()));
    QVERIFY(!fixture.tools().getPairs().empty());
    const qint64 historyRuns = fixture.tools().getHistoryRuns();
    QVERIFY(historyRuns > 0);
    QCOMPARE(fixture.tools().getUsage().size(), size_t(tools.size()));

    // Cancel keeps the list
    page->getClearRecentLink()->click();
    pdf::PDFAppClearRecentToolsDialog* dialog = page->getClearRecentDialog();
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->getClearButton()->text(), QStringLiteral("Clear List"));
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    fixture.screenshot(QStringLiteral("tools-clear-recent"));
    if (!qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        dialog->grab().save(QDir(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS")).absoluteFilePath(QStringLiteral("tools-clear-recent-dialog.png")));
    }
    dialog->getCancelButton()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.tools().getUsage().size(), size_t(tools.size()));

    // Clear List: the list of the page, Home and Support For you are empty
    dialog = page->openClearRecentDialog();
    QVERIFY(dialog);
    dialog->getClearButton()->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.tools().getUsage().empty());
    QTRY_VERIFY(page->getRecentRows().isEmpty());
    QVERIFY(page->getClearRecentLink()->isHidden());
    QVERIFY(!page->getRecentEmpty()->isHidden());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));
    QCOMPARE(fixture.collections().getCollections().size(), size_t(1));
    QVERIFY(!fixture.tools().getPairs().empty());
    QCOMPARE(fixture.tools().getHistoryRuns(), historyRuns);

    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(home);
    QVERIFY(home->getRecentToolRows().isEmpty());
    fixture.navigate(QStringLiteral("/support"));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    QVERIFY(support->getUsedToolIds().isEmpty());
}

void AppToolsTest::collectionsSideList()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // The seven built-in collections of 272:12947 with their meta texts
    const QList<pdf::PDFCollectionRow*> rows = page->getCollectionRows();
    QCOMPARE(rows.size(), 7);
    QCOMPARE(rows[0]->getName(), QStringLiteral("Prepare for sharing"));
    QCOMPARE(rows[0]->getMeta(), QString::fromUtf8("3 tools · Compress, Remove Hidden Data, Protect with Password"));
    QCOMPARE(rows[0]->getIconName(), QStringLiteral("mail"));
    QCOMPARE(rows[6]->getName(), QStringLiteral("Extract content"));
    QCOMPARE(rows[6]->getMeta(), QString::fromUtf8("3 tools · Extract Text, Extract Images, PDF to Images"));

    // A custom collection of another instance at the end
    pdf::PDFToolCollectionDraft draft;
    draft.name = QStringLiteral("Monthly invoices");
    draft.iconName = QStringLiteral("folder");
    draft.toolIds = QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") });
    qint64 id = 0;
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft, &id));
    QTRY_COMPARE(page->getCollectionRows().size(), 8);
    QCOMPARE(page->getCollectionRows().back()->getName(), QStringLiteral("Monthly invoices"));
    QCOMPARE(page->getCollectionRows().back()->objectName(), QStringLiteral("collection-custom-%1").arg(id));
    QVERIFY(fixture.findCollectionRow(QStringLiteral("custom-%1").arg(id)));

    // A click shows the collection (Q1)
    page->getCollectionRows()[2]->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?collection=fix-a-scan"));

    // Manage and New collection… open the collections page
    page = fixture.page<pdf::PDFAppToolsPage>();
    page->getManageLink()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/prepare-for-sharing"));
    page = AppToolsPageTestHelper::open(fixture);
    page->getNewCollectionRow()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/new"));
}

void AppToolsTest::collectionFilter()
{
    pdftest::AppToolsFixture fixture;
    AppToolsPageTestHelper::hideBanner(fixture);
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture, QStringLiteral("/tools?collection=fix-a-scan"));
    QVERIFY(page);

    // One card with the tools in the order of the collection, its name, its count and Edit
    pdf::PDFAppToolCategoryCard* card = page->getCollectionCard();
    QVERIFY(card && !card->isHidden());
    QStringList ids;
    for (pdf::PDFToolRow* row : card->getRows())
    {
        ids << pdf::PDFAppToolPresentation::getToolId(row);
    }
    QCOMPARE(ids, QStringList({ "pages.rotate", "pages.crop", "pages.interleave", "pages.remove" }));
    QCOMPARE(card->getHeader()->getTitle(), QStringLiteral("Fix a scan"));
    QCOMPARE(card->getHeader()->getCount(), QStringLiteral("4 tools"));
    QCOMPARE(card->getHeader()->getIconName(), QStringLiteral("rotate-cw"));
    QCOMPARE(card->width(), page->getCategoryGrid()->parentWidget()->width());
    QVERIFY(page->getCategoryGrid()->isHidden());
    QVERIFY(page->getPinnedSection()->isHidden());
    QVERIFY(!page->getEditCollectionLink()->isHidden());
    QCOMPARE(page->getAllToolsHeader()->getTitleLabel()->text(), QString::fromUtf8("Tools in “Fix a scan”"));
    for (QPushButton* chip : page->getChips())
    {
        QVERIFY(!chip->isChecked());
    }
    QCOMPARE(page->findToolRow(QStringLiteral("pages.crop")), card->getRows()[1]);
    fixture.screenshot(QStringLiteral("tools-collection"));

    // A star of the card pins the tool
    card->getRows()[0]->getStarButton()->click();
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.rotate") }));

    // Another instance changes the collection: the card follows
    pdf::PDFToolCollectionDraft draft;
    draft.builtinId = QStringLiteral("fix-a-scan");
    draft.name = QStringLiteral("Fix a scan");
    draft.toolIds = QStringList({ QStringLiteral("pages.crop"), QStringLiteral("convert.ocr") });
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
    QTRY_COMPARE(page->getCollectionCard() ? page->getCollectionCard()->getRows().size() : 0, 2);
    QCOMPARE(pdf::PDFAppToolPresentation::getToolId(page->getCollectionCard()->getRows()[1]), QStringLiteral("convert.ocr"));

    // Edit opens the editor of the collection
    page->getEditCollectionLink()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/fix-a-scan"));

    // A custom collection deleted by another instance while shown
    qint64 id = 0;
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Mine"), QString(), { QStringLiteral("pages.merge") }, &id).isError());
    page = AppToolsPageTestHelper::open(fixture, QStringLiteral("/tools?collection=custom-%1").arg(id));
    QVERIFY(page && page->getCollectionCard());
    QVERIFY(fixture.writeFromOtherConnection([id](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(id); },
                                             pdf::PDFToolCollectionRepository::getCollectionTopic()));
    QTRY_VERIFY(!page->getNoResults()->isHidden());
    QVERIFY(!page->getCollectionCard());
    QCOMPARE(page->getNoResults()->findChild<QLabel*>(QStringLiteral("noResultsTitle"))->text(), QStringLiteral("This collection does not exist any more"));
    QVERIFY(page->getEditCollectionLink()->isHidden());

    // An unknown collection is Page Not Found
    fixture.navigate(QStringLiteral("/tools?collection=custom-%1").arg(id));
    QVERIFY(fixture.notFoundPage());

    // The chip All of a shown collection shows the whole catalog (review RC6-1: the
    // exclusive group checked All and the page stayed at the collection)
    page = AppToolsPageTestHelper::open(fixture, QStringLiteral("/tools?collection=fix-a-scan"));
    QVERIFY(page && page->getChip(QString()));
    page->getChip(QString())->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page && page->getCollection().isEmpty());
    QVERIFY(page->getChip(QString())->isChecked());
    QVERIFY(!page->getCategoryGrid()->isHidden());

    // An emptied search field of a collection changes nothing
    fixture.navigate(QStringLiteral("/tools?collection=fix-a-scan"));
    page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page && page->setFilter(QString(), QString()));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?collection=fix-a-scan"));
    QVERIFY(!page->getChip(QString())->isChecked());
}

void AppToolsTest::addToCollectionMenu()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // The collections of the tool are checked (Merge is in Archive documents)
    pdf::PDFAppToolMenu* menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(50, 50));
    QVERIFY(menu);
    QCOMPARE(menu->getOpenAction()->text(), QStringLiteral("Open"));
    QVERIFY(menu->findCollectionAction(QStringLiteral("archive-documents"))->isChecked());
    QVERIFY(!menu->findCollectionAction(QStringLiteral("fix-a-scan"))->isChecked());
    QCOMPARE(menu->getCollectionsMenu()->title(), QStringLiteral("Add to Collection"));

    // A built-in collection without an override gets one with its tools and Merge
    menu->findCollectionAction(QStringLiteral("fix-a-scan"))->trigger();
    pdftest::AppDocumentFixture::settle();
    const std::optional<pdf::PDFToolCollectionRow> override = fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan"));
    QVERIFY(override);
    QCOMPARE(override->toolIds, QStringList({ "pages.rotate", "pages.crop", "pages.interleave", "pages.remove", "pages.merge" }));
    QTRY_COMPARE(fixture.findCollectionRow(QStringLiteral("fix-a-scan"))->getMeta().left(7), QStringLiteral("5 tools"));

    // Unchecked: removed again
    menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(50, 50));
    QVERIFY(menu->findCollectionAction(QStringLiteral("fix-a-scan"))->isChecked());
    menu->findCollectionAction(QStringLiteral("fix-a-scan"))->trigger();
    pdftest::AppDocumentFixture::settle();
    // The built-in definition again: the override is deleted (no Reset to default)
    QVERIFY(!fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QTRY_VERIFY(!page->getState().findCollection(QStringLiteral("fix-a-scan"))->isOverridden);
    QCOMPARE(page->getState().findCollection(QStringLiteral("fix-a-scan"))->toolIds, QStringList({ "pages.rotate", "pages.crop", "pages.interleave", "pages.remove" }));

    // A renamed built-in collection keeps its override when its tools are the default ones
    pdf::PDFToolCollectionDraft renamed;
    renamed.builtinId = QStringLiteral("fix-a-scan");
    renamed.name = QStringLiteral("My scans");
    renamed.toolIds = QStringList({ "pages.rotate", "pages.crop", "pages.interleave", "pages.remove", "pages.merge" });
    QVERIFY(!fixture.collections().saveCollection(renamed).isError());
    QVERIFY(!fixture.collections().setToolIncluded(0, QStringLiteral("fix-a-scan"), QStringLiteral("pages.merge"), false).isError());
    QVERIFY(fixture.collections().getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QVERIFY(!fixture.collections().resetBuiltin(QStringLiteral("fix-a-scan")).isError());
    pdftest::AppDocumentFixture::settle();

    // The last tool of a custom collection is not removed: a message
    qint64 id = 0;
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Single & only"), QString(), { QStringLiteral("pages.split") }, &id).isError());
    pdftest::AppDocumentFixture::settle();
    menu = page->showToolMenu(QStringLiteral("pages.split"), QPoint(50, 50));
    QAction* custom = menu->findCollectionAction(QStringLiteral("custom-%1").arg(id));
    QVERIFY(custom && custom->isChecked());
    QCOMPARE(custom->text(), QStringLiteral("Single && only"));
    custom->trigger();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!page->getStatusLabel()->isHidden());
    QCOMPARE(fixture.collections().getCollection(id)->toolIds, QStringList({ QStringLiteral("pages.split") }));

    // New Collection…: the editor with the tool
    menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(50, 50));
    menu->getNewCollectionAction()->trigger();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/collections/new?tool=pages.merge"));

    // Open: the route of the tool
    page = AppToolsPageTestHelper::open(fixture);
    menu = page->showToolMenu(QStringLiteral("pages.split"), QPoint(50, 50));
    menu->getOpenAction()->trigger();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/pages.split"));
}

void AppToolsTest::toolActivationRoutes()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("optimize.compress"), true).isError());
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("review.compare")).isError());
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    // A row, a tile, a recent tool: Page Not Found of the tool (phase 2 has no pages yet)
    page->findToolRow(QStringLiteral("pages.merge"))->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));

    page = AppToolsPageTestHelper::open(fixture);
    page->findTile(QStringLiteral("optimize.compress"))->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/optimize.compress"));

    page = AppToolsPageTestHelper::open(fixture);
    QCOMPARE(page->getRecentRows().size(), 1);
    page->getRecentRows().front()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/review.compare"));

    // Enter on a row focused by the keyboard
    page = AppToolsPageTestHelper::open(fixture);
    pdf::PDFToolRow* row = page->findToolRow(QStringLiteral("pages.split"));
    row->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row, Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/pages.split"));

    // A registered page of the tool is shown
    QVERIFY(fixture.registerTestToolPage(QStringLiteral("pages.merge")));
    page = AppToolsPageTestHelper::open(fixture);
    page->findToolRow(QStringLiteral("pages.merge"))->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.page<pdftest::AppToolsTestPage>());
}

void AppToolsTest::keyboardGrid()
{
    pdftest::AppToolsFixture fixture;
    AppToolsPageTestHelper::hideBanner(fixture);
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    // One stop of Tab per group: the first row, the stars never
    int stops = 0;
    for (pdf::PDFToolRow* row : page->getVisibleToolRows())
    {
        stops += row->focusPolicy() == Qt::StrongFocus ? 1 : 0;
        QCOMPARE(row->getStarButton()->focusPolicy(), Qt::NoFocus);
    }
    QCOMPARE(stops, 1);
    QCOMPARE(page->getRowFocusGroup()->getCurrentWidget(), static_cast<QWidget*>(page->findToolRow(QStringLiteral("pages.merge"))));

    auto focused = []() { return pdf::PDFAppToolPresentation::getToolId(QApplication::focusWidget()); };
    page->findToolRow(QStringLiteral("pages.merge"))->setFocus(Qt::TabFocusReason);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(focused(), QStringLiteral("pages.split"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QVERIFY2(focused().startsWith(QStringLiteral("convert.")), qPrintable(focused()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
    QVERIFY(focused().startsWith(QStringLiteral("pages.")));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_End);
    QCOMPARE(focused(), QStringLiteral("review.report"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Home);
    QCOMPARE(focused(), QStringLiteral("pages.merge"));

    // Down from the last row of a card goes into the card below
    page->findToolRow(QStringLiteral("pages.boxes"))->setFocus(Qt::TabFocusReason);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(focused(), QStringLiteral("edit.content"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
    QCOMPARE(focused(), QStringLiteral("pages.boxes"));

    // The new stop of Tab is the focused row
    QCOMPARE(page->getRowFocusGroup()->getCurrentWidget(), static_cast<QWidget*>(page->findToolRow(QStringLiteral("pages.boxes"))));
    QCOMPARE(page->findToolRow(QStringLiteral("pages.merge"))->focusPolicy(), Qt::ClickFocus);

    // A filter hiding the stop: the first visible row is the stop
    page->setFilter(QStringLiteral("review"), QString());
    page = fixture.page<pdf::PDFAppToolsPage>();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getRowFocusGroup()->getCurrentWidget(), static_cast<QWidget*>(page->findToolRow(QStringLiteral("review.compare"))));

    // The collections and the recent tools are groups too (arrows move along the list)
    page = AppToolsPageTestHelper::open(fixture);
    pdftest::AppDocumentFixture::settle();
    page->getCollectionRows().front()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getCollectionRows()[1]));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_End);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getNewCollectionRow()));
}

void AppToolsTest::performance()
{
    // A new page reads the database once - not again at its first showing (review
    // RC6-1); a page shown again after a hide reads it again (the times of the
    // recent tools)
    {
        pdftest::AppToolsFixture fixture;
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
        QVERIFY(page && page->isVisible());
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(page->getRefreshCount(), 1);
        page->hide();
        page->show();
        QCOMPARE(page->getRefreshCount(), 2);
        page->hide();
        page->refresh();
        page->show();
        QCOMPARE(page->getRefreshCount(), 4);
    }

    pdftest::AppToolsFixture fixture;
    for (int i = 0; i < pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS; ++i)
    {
        QVERIFY(!fixture.collections().createCollection(QStringLiteral("Collection %1").arg(i), QString(), { QStringLiteral("pages.merge"), QStringLiteral("pages.split") }).isError());
    }

    QElapsedTimer timer;
    timer.start();
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture, QStringLiteral("/tools?q=x"));
    const qint64 openTime = timer.elapsed();
    QVERIFY(page);
    QCOMPARE(page->getCollectionRows().size(), 207);
    page->setFilter(QString(), QStringLiteral("merge"));
    QCOMPARE(fixture.page<pdf::PDFAppToolsPage>(), page);

    const int builds = page->getRowBuildCount();
    QPointer<pdf::PDFToolRow> row = page->findToolRow(QStringLiteral("pages.merge"));

    timer.restart();
    for (int i = 0; i < 10; ++i)
    {
        page->refresh();
    }
    const qint64 refreshTime = timer.elapsed() / 10;

    timer.restart();
    for (int i = 0; i < 10; ++i)
    {
        page->setFilter(QString(), (i % 2) == 0 ? QStringLiteral("sign") : QStringLiteral("image"));
    }
    const qint64 filterTime = timer.elapsed() / 10;

    // A pin changes the stars, not the rows
    QVERIFY(page->setToolPinned(QStringLiteral("pages.merge"), true));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getRowBuildCount(), builds);
    QCOMPARE(page->findToolRow(QStringLiteral("pages.merge")), row.data());

    qInfo("Tools page: open %lld ms, refresh %lld ms (200 custom collections), filter %lld ms", openTime, refreshTime, filterTime);
    QVERIFY(refreshTime < 1000 * pdftest::AppDocumentFixture::getTimeoutFactor());
}

void AppToolsTest::hostileStoredRows()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('pages.unknown', 0, 1)");
    database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('', 1, 1)");
    database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES (NULL, 3, 1)");
    database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES (x'00ff', 4, 1)");
    QVERIFY(!database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('protect.sign', 2, 1)").isError());
    database->getConnection()->execute("INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('x''); DROP TABLE tool_pin;--', 5, 1)");
    database->getConnection()->execute("INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('unknown.tool', 9223372036854775807, 1), ('pages.merge', -5, 1), ('pages.split', 'text', 'x')");
    const QByteArray longName = "INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES (NULL, '" + QByteArray(64 * 1024, 'n') + "', 'evil-icon', 'x')";
    database->getConnection()->execute(longName.constData());
    database->getConnection()->execute("INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES (NULL, '\xE2\x80\xAE" "evil&<b>x</b>', NULL, 1)");
    database->getConnection()->execute("INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES ('unknown-builtin', 'Builtin?', 'mail', 1)");
    database->getConnection()->execute("INSERT INTO tool_collection_item(collection_id, tool_id, position) SELECT id, 'pages.unknown', 0 FROM tool_collection");

    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    QCOMPARE(page->getVisibleToolRows().size(), 46);
    for (pdf::PDFQuickActionTile* tile : page->getTiles())
    {
        QVERIFY(pdf::PDFDocumentToolCatalog::isValidToolId(pdf::PDFAppToolPresentation::getToolId(tile)));
    }
    for (pdf::PDFRecentToolRow* row : page->getRecentRows())
    {
        QVERIFY(pdf::PDFDocumentToolCatalog::isValidToolId(pdf::PDFAppToolPresentation::getToolId(row)));
    }
    for (pdf::PDFCollectionRow* row : page->getCollectionRows())
    {
        QVERIFY(row->getName().size() <= 1024);
        QVERIFY(!row->getName().contains(QChar(0x202E)));
        QVERIFY(pdf::PDFAppBuiltinToolCollections::isValidIconName(row->getIconName()));
    }

    // The menu shows the names as data (no mnemonic)
    pdf::PDFAppToolMenu* menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(10, 10));
    QVERIFY(menu);
    for (QAction* action : menu->getCollectionsMenu()->actions())
    {
        QVERIFY(action->text().size() <= 2048);
    }
}

void AppToolsTest::otherInstanceChanges()
{
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    const int builds = page->getRowBuildCount();

    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("convert.ocr")));
    QTRY_VERIFY(page->findTile(QStringLiteral("convert.ocr")));
    QVERIFY(page->findToolRow(QStringLiteral("convert.ocr"))->isPinned());
    QCOMPARE(AppToolsPageTestHelper::chipCount(page, QStringLiteral("pinned")), 1);

    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.crop"), pdf::PDFAppHomeFormat::getCurrentTime()));
    QTRY_COMPARE(page->getRecentRows().size(), 1);
    QVERIFY(!page->getClearRecentLink()->isHidden());

    pdf::PDFToolCollectionDraft draft;
    draft.name = QStringLiteral("Other window");
    draft.toolIds = QStringList({ QStringLiteral("convert.ocr") });
    QVERIFY(fixture.writeCollectionFromOtherConnection(draft));
    QTRY_COMPARE(page->getCollectionRows().size(), 8);

    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).clearUsage(); }, pdf::PDFToolRepository::getUsageTopic()));
    QTRY_VERIFY(page->getRecentRows().isEmpty());

    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("convert.ocr"), false));
    QTRY_VERIFY(!page->findTile(QStringLiteral("convert.ocr")));
    QVERIFY(!page->findToolRow(QStringLiteral("convert.ocr"))->isPinned());
    QCOMPARE(page->getRowBuildCount(), builds);

    // A hidden page (another tab) reads nothing for the notifications; it reads the
    // state once, when it is shown again (RFW6)
    fixture.settle();
    page->hide();
    const int refreshes = page->getRefreshCount();
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.crop")));
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.split"), pdf::PDFAppHomeFormat::getCurrentTime()));
    fixture.settle();
    QCOMPARE(page->getRefreshCount(), refreshes);
    page->show();
    QCOMPARE(page->getRefreshCount(), refreshes + 1);
    QVERIFY(page->findToolRow(QStringLiteral("pages.crop"))->isPinned());
    QCOMPARE(page->getRecentRows().size(), 1);
    fixture.settle();
    QCOMPARE(page->getRefreshCount(), refreshes + 1);
}

void AppToolsTest::deletionMatrix_data()
{
    QTest::addColumn<QString>("open");
    QTest::addColumn<QString>("victim");

    const QStringList opens({ "menu", "submenu", "dialog", "searchTimer", "refresh", "status" });
    const QStringList victims({ "page", "window" });
    for (const QString& open : opens)
    {
        for (const QString& victim : victims)
        {
            QTest::newRow(qPrintable(open + QLatin1Char('-') + victim)) << open << victim;
        }
    }
}

void AppToolsTest::deletionMatrix()
{
    QFETCH(QString, open);
    QFETCH(QString, victim);

    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("pages.merge")).isError());
    QPointer<pdf::PDFAppToolsPage> page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);

    QPointer<QWidget> popup;
    if (open == QStringLiteral("menu") || open == QStringLiteral("submenu"))
    {
        pdf::PDFAppToolMenu* menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(100, 100));
        popup = menu;
        if (open == QStringLiteral("submenu"))
        {
            menu->setActiveAction(menu->getCollectionsMenu()->menuAction());
            menu->getCollectionsMenu()->popup(QPoint(200, 120));
            popup = menu->getCollectionsMenu();
        }
        QTRY_VERIFY(popup->isVisible());
    }
    else if (open == QStringLiteral("dialog"))
    {
        popup = page->openClearRecentDialog();
        QVERIFY(popup);
    }
    else if (open == QStringLiteral("searchTimer"))
    {
        QTest::keyClicks(page->getSearchField(), QStringLiteral("abc"));
    }
    else if (open == QStringLiteral("refresh"))
    {
        QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    }
    else
    {
        page->findToolRow(QStringLiteral("pages.split"))->getStarButton()->click();
    }

    if (victim == QStringLiteral("page"))
    {
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(page.isNull());
    }
    else
    {
        delete fixture.window.data();
    }

    QTest::qWait(pdf::PDFAppToolsPage::SEARCH_DELAY + 50);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page.isNull());
    QVERIFY(!popup || !popup->isVisible());
}

void AppToolsTest::deleteFromOwnSignals_data()
{
    QTest::addColumn<QString>("site");
    QTest::newRow("menu-open") << "menu-open";
    QTest::newRow("menu-pin") << "menu-pin";
    QTest::newRow("menu-collection") << "menu-collection";
    QTest::newRow("menu-new") << "menu-new";
    QTest::newRow("dialog-finished") << "dialog-finished";
    QTest::newRow("row-clicked") << "row-clicked";
    QTest::newRow("star-pinned") << "star-pinned";
    QTest::newRow("route-replaced") << "route-replaced";
    QTest::newRow("navigation") << "navigation";
    QTest::newRow("nearest-clicked") << "nearest-clicked";
    QTest::newRow("nearest-pinned") << "nearest-pinned";
    QTest::newRow("back-requested") << "back-requested";
}

void AppToolsTest::deleteFromOwnSignals()
{
    QFETCH(QString, site);

    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("pages.merge")).isError());
    const QString route = site == QStringLiteral("route-replaced") ? QStringLiteral("/tools?q=merge")
                        : site.startsWith(QStringLiteral("nearest-")) ? QStringLiteral("/tools?q=spliting")
                        : QStringLiteral("/tools");
    if (site == QStringLiteral("back-requested"))
    {
        // The filtered page after the catalog: its cleared filter goes Back (O25)
        QVERIFY(AppToolsPageTestHelper::open(fixture));
        fixture.navigate(QStringLiteral("/tools?q=merge"));
    }
    QPointer<pdf::PDFAppToolsPage> page = site == QStringLiteral("back-requested") ? fixture.page<pdf::PDFAppToolsPage>() : AppToolsPageTestHelper::open(fixture, route);
    QVERIFY(page);
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    QObject context;
    int deletions = 0;
    auto deleteWindow = [&window, &deletions]()
    {
        ++deletions;
        delete window.data();
    };

    if (site.startsWith(QStringLiteral("menu-")))
    {
        pdf::PDFAppToolMenu* menu = page->showToolMenu(QStringLiteral("pages.merge"), QPoint(100, 100));
        QObject::connect(menu, &pdf::PDFAppToolMenu::openRequested, &context, deleteWindow);
        QObject::connect(menu, &pdf::PDFAppToolMenu::pinRequested, &context, deleteWindow);
        QObject::connect(menu, &pdf::PDFAppToolMenu::collectionToggled, &context, deleteWindow);
        QObject::connect(menu, &pdf::PDFAppToolMenu::newCollectionRequested, &context, deleteWindow);
        QAction* action = site == QStringLiteral("menu-open") ? menu->getOpenAction()
                        : site == QStringLiteral("menu-pin") ? menu->getPinAction()
                        : site == QStringLiteral("menu-new") ? menu->getNewCollectionAction()
                        : menu->findCollectionAction(QStringLiteral("fix-a-scan"));
        action->trigger();
    }
    else if (site == QStringLiteral("dialog-finished"))
    {
        pdf::PDFAppClearRecentToolsDialog* dialog = page->openClearRecentDialog();
        QObject::connect(dialog, &QDialog::finished, &context, deleteWindow);
        dialog->getClearButton()->click();
    }
    else if (site == QStringLiteral("row-clicked"))
    {
        QObject::connect(page->findToolRow(QStringLiteral("pages.merge")), &pdf::PDFToolRow::clicked, &context, deleteWindow);
        page->findToolRow(QStringLiteral("pages.merge"))->click();
    }
    else if (site == QStringLiteral("star-pinned"))
    {
        QObject::connect(page->findToolRow(QStringLiteral("pages.merge")), &pdf::PDFToolRow::pinnedChanged, &context, deleteWindow);
        page->findToolRow(QStringLiteral("pages.merge"))->getStarButton()->click();
    }
    else if (site == QStringLiteral("nearest-clicked"))
    {
        QCOMPARE(page->getNearestRows().size(), 1);
        QObject::connect(page->getNearestRows().front(), &pdf::PDFToolRow::clicked, &context, deleteWindow);
        page->getNearestRows().front()->click();
    }
    else if (site == QStringLiteral("nearest-pinned"))
    {
        QCOMPARE(page->getNearestRows().size(), 1);
        QObject::connect(page->getNearestRows().front(), &pdf::PDFToolRow::pinnedChanged, &context, deleteWindow);
        page->getNearestRows().front()->getStarButton()->click();
    }
    else if (site == QStringLiteral("back-requested"))
    {
        QObject::connect(page.data(), &pdf::PDFAppPage::backRequested, &context, deleteWindow);
        page->setFilter(QString(), QString());
    }
    else if (site == QStringLiteral("route-replaced"))
    {
        QObject::connect(page.data(), &pdf::PDFAppPage::routeReplaced, &context, deleteWindow);
        page->setFilter(QStringLiteral("pages"), QStringLiteral("merge"));
    }
    else
    {
        QObject::connect(page.data(), &pdf::PDFAppPage::navigationRequested, &context, deleteWindow);
        page->setFilter(QStringLiteral("pages"), QString());
    }

    pdftest::AppDocumentFixture::settle();
    QCOMPARE(deletions, 1);
    QVERIFY(window.isNull());
    QVERIFY(page.isNull());
}

void AppToolsTest::themesAndScales_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("scale");
    for (bool isDark : { false, true })
    {
        for (qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(isDark ? "dark" : "light").arg(scale))) << isDark << scale;
        }
    }
}

void AppToolsTest::themesAndScales()
{
    QFETCH(bool, isDark);
    QFETCH(qreal, scale);

    pdftest::AppToolsFixture fixture;
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("review.compare")).isError());

    const QStringList routes({ "/tools", "/tools?category=convert&q=image", "/tools?collection=fix-a-scan", "/tools?q=zzqqxx" });
    for (const QString& route : routes)
    {
        pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture, route);
        QVERIFY(page);
        pdftest::AppDocumentFixture::settle();
        const QImage image = AppToolsPageTestHelper::render(page, scale);
        QCOMPARE(AppToolsPageTestHelper::countLeakPixels(image), 0);

        if (route == QStringLiteral("/tools"))
        {
            // The new collection row: bg/subtle and the name text/accent (272:12952)
            pdf::PDFCollectionRow* create = page->getNewCollectionRow();
            const QPoint inside = AppToolsPageTestHelper::rectIn(create, page).topLeft() + QPoint(200, 4);
            QVERIFY(AppToolsPageTestHelper::isClose(AppToolsPageTestHelper::pixel(image, inside), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSubtle)));
            // The card of a category: bg/surface below its header
            pdf::PDFAppToolCategoryCard* card = page->getCategoryCards().front();
            const QPoint list = AppToolsPageTestHelper::rectIn(card, page).topLeft() + QPoint(200, 64);
            QVERIFY(AppToolsPageTestHelper::isClose(AppToolsPageTestHelper::pixel(image, list), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface)));
        }
    }

    if (scale == 1.0)
    {
        fixture.openTools(QStringLiteral("/tools"));
        fixture.screenshot(QStringLiteral("tools-%1").arg(isDark ? "dark" : "light"));
        fixture.openTools(QStringLiteral("/tools?category=convert&q=image"));
        fixture.screenshot(QStringLiteral("tools-search-%1").arg(isDark ? "dark" : "light"));
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolsTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
        QVERIFY(page);
        pdftest::AppDocumentFixture::settle();

        // The cards mirrored: Organize Pages at the right
        const QList<pdf::PDFAppToolCategoryCard*> cards = page->getCategoryCards();
        QVERIFY(cards[0]->x() > cards[1]->x());
        QCOMPARE(AppToolsPageTestHelper::countLeakPixels(AppToolsPageTestHelper::render(page, 1.0)), 0);

        // Left moves to the card at the left of the screen (Create & Convert)
        page->findToolRow(QStringLiteral("pages.merge"))->setFocus(Qt::TabFocusReason);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
        QVERIFY(pdf::PDFAppToolPresentation::getToolId(QApplication::focusWidget()).startsWith(QStringLiteral("convert.")));
        fixture.screenshot(QStringLiteral("tools-rtl"));
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolsTest::minimalWindow()
{
    pdftest::AppToolsFixture fixture(QSize(720, 480));
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("convert.ocr"), true).isError());
    for (const QString& route : { QStringLiteral("/tools"), QStringLiteral("/tools?category=convert&q=image"), QStringLiteral("/tools?collection=fix-a-scan") })
    {
        pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture, route);
        QVERIFY(page);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(page->findChild<pdf::PDFAppColumns*>()->isStacked());
        QVERIFY(page->getScrollArea());
        QCOMPARE(page->getScrollArea()->horizontalScrollBar()->maximum(), 0);
        QVERIFY(page->width() <= page->getScrollArea()->viewport()->width());
        for (pdf::PDFAppToolCategoryCard* card : page->getCategoryCards())
        {
            if (!card->isHidden())
            {
                QCOMPARE(card->width(), page->getCategoryGrid()->width());
            }
        }
        for (pdf::PDFToolRow* row : page->getVisibleToolRows())
        {
            QVERIFY(row->width() >= row->minimumSizeHint().width() || row->width() > 0);
        }
    }
    fixture.openTools(QStringLiteral("/tools"));
    fixture.screenshot(QStringLiteral("tools-720x480"));
}

void AppToolsTest::keyboardAndAccessibility()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("convert.ocr"), true).isError());
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("review.compare")).isError());
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    pdftest::AppDocumentFixture::settle();

    // Every group: one stop
    for (pdf::PDFAppToolFocusGroup* group : { page->getTileFocusGroup(), page->getRowFocusGroup(), page->getCollectionFocusGroup(), page->getRecentFocusGroup() })
    {
        int stops = 0;
        for (QWidget* widget : group->getVisibleWidgets())
        {
            stops += widget->focusPolicy() == Qt::StrongFocus ? 1 : 0;
        }
        QCOMPARE(stops, 1);
    }

    // Tab walks the groups: search, chips, tiles, rows, (links), collections, recent
    page->getSearchField()->setFocus(Qt::TabFocusReason);
    QStringList visited;
    for (int i = 0; i < 12; ++i)
    {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        visited << QApplication::focusWidget()->objectName();
    }
    QVERIFY2(visited.contains(QStringLiteral("chip-all")), qPrintable(visited.join(',')));
    QVERIFY(visited.contains(QStringLiteral("tool-pages.merge")));
    QVERIFY(visited.contains(QStringLiteral("collection-prepare-for-sharing")));
    QVERIFY(!visited.contains(QStringLiteral("tool-pages.split")));

    // Accessible names
    QCOMPARE(page->getSearchField()->accessibleName(), QStringLiteral("Search tools"));
    QCOMPARE(page->getTileGrid()->accessibleName(), QStringLiteral("Pinned tools"));
    QCOMPARE(page->getCategoryGrid()->accessibleName(), QStringLiteral("All tools"));
    QCOMPARE(page->findChild<QWidget*>(QStringLiteral("chips"))->accessibleName(), QStringLiteral("Categories"));
    QCOMPARE(page->getChip(QStringLiteral("pages"))->accessibleDescription(), QStringLiteral("12 tools"));
    QAccessibleInterface* rowInterface = QAccessible::queryAccessibleInterface(page->findToolRow(QStringLiteral("pages.merge")));
    QVERIFY(rowInterface);
    QCOMPARE(rowInterface->text(QAccessible::Name), QStringLiteral("Merge"));

    // The menu key opens the menu of a tile
    pdf::PDFQuickActionTile* tile = page->findTile(QStringLiteral("convert.ocr"));
    tile->setFocus(Qt::TabFocusReason);
    QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, tile->rect().center(), tile->mapToGlobal(tile->rect().center()));
    QApplication::sendEvent(tile, &keyboardMenu);
    QTRY_VERIFY(page->getToolMenu() && page->getToolMenu()->isVisible());
    QCOMPARE(page->getToolMenu()->getToolId(), QStringLiteral("convert.ocr"));
    page->getToolMenu()->hide();
}

void AppToolsTest::languageChange()
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    fixture.navigate(QStringLiteral("/tools?category=pages&q=mer"));
    pdf::PDFAppToolsPage* page = fixture.page<pdf::PDFAppToolsPage>();
    QVERIFY(page);
    page->getSearchField()->setText(QStringLiteral("mer "));
    page->getSearchField()->setFocus(Qt::TabFocusReason);
    page->getSearchField()->setCursorPosition(2);
    QPointer<pdf::PDFLineEdit> oldField = page->getSearchField();
    const QStringList visibleBefore = AppToolsPageTestHelper::visibleIds(page);
    QVERIFY(visibleBefore.contains(QStringLiteral("pages.merge")));

    AppToolsTestTranslator translator;
    translator.entries.insert({ "pdf::PDFAppToolsPage", "All" }, QStringLiteral("Alle"));
    translator.entries.insert({ "pdf::PDFAppToolsPage", "Recent tools" }, QStringLiteral("Zuletzt verwendet"));
    translator.entries.insert({ "pdf::PDFAppToolsPage", "Search tools by name, action or keyword…" }, QStringLiteral("Werkzeuge suchen…"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    pdftest::AppDocumentFixture::settle();

    QVERIFY(oldField.isNull());
    QCOMPARE(AppToolsPageTestHelper::visibleIds(page), visibleBefore);
    QCOMPARE(page->getChip(QString())->text(), QStringLiteral("Alle"));
    QCOMPARE(page->getSearchField()->placeholderText(), QStringLiteral("Werkzeuge suchen…"));
    QCOMPARE(page->getSearchField()->text(), QStringLiteral("mer "));
    QCOMPARE(page->getSearchField()->cursorPosition(), 2);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(page->getSearchField()));
    QCOMPARE(page->getCategory(), QStringLiteral("pages"));
    QVERIFY(page->getChip(QStringLiteral("pages"))->isChecked());
    QVERIFY(page->findToolRow(QStringLiteral("pages.merge"))->isPinned());

    // The dialog is translated while open
    QCoreApplication::removeTranslator(&translator);
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("pages.merge")).isError());
    page->refresh();
    pdf::PDFAppClearRecentToolsDialog* dialog = page->openClearRecentDialog();
    QVERIFY(dialog);
    translator.entries.insert({ "pdf::PDFAppClearRecentToolsDialog", "Clear List" }, QStringLiteral("Liste leeren"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(page->getClearRecentDialog());
    QCOMPARE(page->getClearRecentDialog()->getClearButton()->text(), QStringLiteral("Liste leeren"));
    QCoreApplication::removeTranslator(&translator);
    pdftest::AppDocumentFixture::settle();
}

void AppToolsTest::recentTimesFollowClock()
{
    // O25: Recent tools show "5 minutes ago" and follow the clock by the minute ticker
    // of the context: the same rows, no read of the database; a hidden page is not
    // updated, it is updated when shown; the page deleted from the tick
    pdftest::AppToolsFixture fixture;
    const qint64 now = QDateTime(QDate(2026, 10, 26), QTime(15, 56), QTimeZone::LocalTime).toMSecsSinceEpoch();
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now);
    pdf::PDFAppToolsPage* page = AppToolsPageTestHelper::open(fixture);
    QVERIFY(page);
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.merge"), now - 5 * 60000));
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("optimize.compress"), now - 20000));
    QTRY_COMPARE(page->getRecentRows().size(), 2);

    pdf::PDFAppMinuteTicker* ticker = fixture.context->getMinuteTicker();
    QVERIFY(page->getMinuteSubscription());
    QTRY_VERIFY(ticker->isRunning());
    const QList<pdf::PDFRecentToolRow*> rows = page->getRecentRows();
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("Just now"));
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("5 minutes ago"));
    const int refreshes = page->getRefreshCount();
    const int builds = page->getRowBuildCount();

    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 60000);
    ticker->tick();
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("1 minute ago"));
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("6 minutes ago"));
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 2 * 60000);
    ticker->tick();
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("2 minutes ago"));
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 3600 * 1000);
    ticker->tick();
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("Today 15:51"));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(page->getRecentRows(), rows);
    QCOMPARE(page->getRefreshCount(), refreshes);
    QCOMPARE(page->getRowBuildCount(), builds);

    // Hidden: no update (the ticker stops); shown: the texts of the current minute at once
    page->hide();
    QTRY_VERIFY(!ticker->isRunning());
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 3 * 60000);
    ticker->tick();
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("Today 15:51"));
    page->show();
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("8 minutes ago"));
    QCOMPARE(page->getRecentRows(), rows);
    QTRY_VERIFY(ticker->isRunning());

    // A use of another instance rebuilds the list; the times stay relative
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.split"), now + 3 * 60000 - 2 * 60000));
    QTRY_COMPARE(page->getRecentRows().size(), 3);
    QCOMPARE(pdf::PDFAppToolPresentation::getToolId(page->getRecentRows().front()), QStringLiteral("pages.split"));
    QCOMPARE(page->getRecentRows().front()->getLastUsed(), QStringLiteral("2 minutes ago"));

    // The window deleted from the tick of the page
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    QObject::connect(page->getMinuteSubscription(), &pdf::PDFAppMinuteSubscription::minuteChanged, page, [window]() { delete window.data(); });
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 4 * 60000);
    ticker->tick();
    QVERIFY(!window);
    pdftest::AppDocumentFixture::settle();
    QTRY_VERIFY(!ticker->isRunning());
    QCOMPARE(ticker->getActiveSubscriptionCount(), 0);
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(0);
}

void AppToolsTest::categoryCardWidgets()
{
    // The card, the grid, the focus group and the menu alone (hostile use by an application)
    QWidget host;
    host.resize(900, 900);
    pdf::PDFAppToolCategoryGrid* grid = new pdf::PDFAppToolCategoryGrid(&host);
    grid->addCard(nullptr);
    QVERIFY(grid->getCards().isEmpty());
    QCOMPARE(grid->heightForWidth(800), 0);
    QCOMPARE(grid->minimumSizeHint(), QSize(0, 0));

    // A card of another parent is taken over; rows without a caption
    QWidget other;
    pdf::PDFAppToolCategoryCard* first = new pdf::PDFAppToolCategoryCard(QStringLiteral("A & B"), QStringLiteral("Description"), QStringLiteral("2 tools"), QStringLiteral("pages"), pdf::PDFAccentHue::Amber, &other);
    first->addRow(nullptr);
    const pdf::PDFDocumentToolCatalogEntry* merge = pdf::PDFDocumentToolCatalog::findTool(u"pages.merge");
    const pdf::PDFDocumentToolCatalogEntry* split = pdf::PDFDocumentToolCatalog::findTool(u"pages.split");
    first->addRow(pdf::PDFAppToolPresentation::createToolRow(*merge, false, nullptr));
    first->addCaption(QStringLiteral("Second"));
    first->addRow(pdf::PDFAppToolPresentation::createToolRow(*split, false, nullptr));
    QCOMPARE(first->getRows().size(), 2);
    QCOMPARE(first->getCaptions().size(), 1);
    grid->addCard(first);
    grid->addCard(first);
    QCOMPARE(first->parentWidget(), static_cast<QWidget*>(grid));
    QCOMPARE(grid->getCards().size(), 1);
    QCOMPARE(first->updateVisibility(), 2);
    QCOMPARE(first->getVisibleRows().size(), 2);
    first->getRows().front()->hide();
    QCOMPARE(first->updateVisibility(), 1);
    QCOMPARE(first->getVisibleRows().size(), 1);
    QCOMPARE(first->getCaptions().front()->contentsMargins().top(), int(pdf::PDFAppToolCategoryCard::FIRST_CAPTION_TOP));

    pdf::PDFAppToolCategoryCard* second = new pdf::PDFAppToolCategoryCard(QStringLiteral("C"), QString(), QStringLiteral("1 tool"), QStringLiteral("edit"), pdf::PDFAccentHue::Green, grid);
    second->addRow(pdf::PDFAppToolPresentation::createToolRow(*merge, false, nullptr));
    grid->addCard(second);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    grid->resize(800, grid->heightForWidth(800));
    QCOMPARE(grid->getColumnCount(800), 2);
    QCOMPARE(grid->getColumnCount(500), 1);
    QVERIFY(grid->sizeHint().height() > 0);

    // A deleted card is forgotten, one card takes the whole width
    delete second;
    QCOMPARE(grid->getCards().size(), 1);
    QCOMPARE(grid->getColumnCount(800), 1);

    // The focus group: a deleted widget, a removed one, an unknown current widget
    pdf::PDFAppToolFocusGroup group(&host);
    group.addWidget(nullptr);
    group.addWidget(first->getRows()[0]);
    group.addWidget(first->getRows()[1]);
    group.addWidget(first->getRows()[1]);
    QCOMPARE(group.getWidgets().size(), 2);
    group.setCurrentWidget(&other);
    QCOMPARE(group.getCurrentWidget(), static_cast<QWidget*>(first->getRows()[1]));
    QVERIFY(!group.moveFocus(first->getRows()[1], Qt::Key_A));
    QVERIFY(group.moveFocus(nullptr, Qt::Key_Down));
    group.removeWidget(first->getRows()[1]);
    QVERIFY(!group.getCurrentWidget());
    group.updateStops();
    QVERIFY(!group.getCurrentWidget());
    QObject plain;
    QEvent focusEvent(QEvent::FocusIn);
    plain.installEventFilter(&group);
    QCoreApplication::sendEvent(&plain, &focusEvent);
    plain.removeEventFilter(&group);
    pdf::PDFAppToolFocusGroup rootless(nullptr);
    rootless.addWidget(first->getRows()[1]);
    rootless.updateStops();
    QCOMPARE(rootless.getCurrentWidget(), static_cast<QWidget*>(first->getRows()[1]));

    // The menu: at most MAXIMUM_COLLECTIONS collections, an unknown route id
    std::vector<pdf::PDFAppToolCollection> collections(pdf::PDFAppToolMenu::MAXIMUM_COLLECTIONS + 10);
    for (size_t i = 0; i < collections.size(); ++i)
    {
        collections[i].routeId = pdf::PDFAppToolCollections::getCustomRouteId(qint64(i + 1));
        collections[i].name = QStringLiteral("C%1").arg(i);
    }
    pdf::PDFAppToolMenu menu(QStringLiteral("pages.merge"), false, collections, nullptr);
    QVERIFY(menu.findCollectionAction(QStringLiteral("custom-1")));
    QVERIFY(!menu.findCollectionAction(pdf::PDFAppToolCollections::getCustomRouteId(qint64(collections.size()))));
    QVERIFY(!menu.findCollectionAction(QStringLiteral("unknown")));
    pdf::PDFAppToolMenu empty(QStringLiteral("pages.merge"), true, {}, nullptr);
    QCOMPARE(empty.getPinAction()->text(), QStringLiteral("Unpin"));
    QCOMPARE(empty.getCollectionsMenu()->actions().size(), 1);
}

QTEST_MAIN(AppToolsTest)

#include "tst_apptoolstest.moc"
