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

// Tests of the global search of the new application (step 2): the matching of
// the core (PDFSearchQuery, PDFDocumentToolSearch), the engine and its providers
// (every group, ranking, hostile input, translations, speed), the page of all
// results /search (states of the frames 941:7081 and 955:142130, groups, counts,
// filter route, activation, keyboard, notifications, geometry, pixels, right-to-
// left, minimal window, deletion) and the popup of the field of the header
// (285:3026: open / close, keys, pointer, focus, window, notifications, deletion,
// geometry, pixels). PDF4QT_APP_SCREENSHOTS=<folder> writes pictures.

#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfapphelpcatalog.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfappsearchengine.h"
#include "pdfappsearchpage.h"
#include "pdfappsearchwidgets.h"
#include "pdfapptab.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsearch.h"
#include "pdfrecentrepository.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfshortcutrepository.h"
#include "pdfsupportvideocatalog.h"

#include "pdfaccenthue.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTranslator>
#include <QWindow>
#include <QtTest>

#include <algorithm>
#include <memory>

/// Translator of the test: a few texts in another language
class SearchTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArray source(sourceText ? sourceText : "");
        const QByteArray contextName(context ? context : "");
        const auto it = entries.constFind(qMakePair(contextName, source));
        if (it != entries.cend())
        {
            return it.value();
        }
        if (contextName == "pdf::PDFDocumentToolCatalog" && source == "Merge")
        {
            return QStringLiteral("Zusammenführen");
        }
        if (contextName == "pdf::PDFAppCommands" && source == "New tab")
        {
            return QStringLiteral("Neuer Tab");
        }
        if (contextName == "pdf::PDFSettingDefinitions" && source == "Color scheme")
        {
            return QStringLiteral("Farbschema");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }

    /// Other translations (context, source text)
    QHash<QPair<QByteArray, QByteArray>, QString> entries;
};

/// Window of the application over a database of the test (portable memory
/// database with a name of the process, or a file database)
class SearchFixture
{
public:
    explicit SearchFixture(const QString& databaseFile = QString(), const QSize& size = QSize(1440, 977))
    {
        QStringList arguments = { QStringLiteral("PDF4QT") };
        if (databaseFile.isEmpty())
        {
            arguments << QStringLiteral("--portable");
        }
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));

        pdf::PDFDatabaseOptions options;
        options.memoryName = QStringLiteral("/pdf4qt-search-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        options.filePath = databaseFile;
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        window = new pdf::PDFAppMainWindow(context.get());
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(size);
        window->show();
        window->activateWindow();
        if (!QTest::qWaitForWindowActive(window.data()))
        {
            qWarning("The window was not activated");
        }
        if (window->getHomeTab() && window->getHomeTab()->getBanner())
        {
            window->getHomeTab()->getBanner()->hide();
        }
        settle();
    }

    ~SearchFixture()
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
    pdf::PDFLineEdit* field() const { return homeTab() ? homeTab()->getHeader()->getSearchField() : nullptr; }
    pdf::PDFAppSearchController* controller() const { return pdf::PDFAppSearchController::find(field()); }
    pdf::PDFAppSearchPopup* popup() const { return controller() ? controller()->getPopup() : nullptr; }
    pdf::PDFAppSearchPage* searchPage() const { return qobject_cast<pdf::PDFAppSearchPage*>(currentPage()); }

    void navigate(const QString& text)
    {
        navigator()->navigate(pdf::PDFAppRoute::parse(text), pdf::PDFAppNavigationMode::Push);
        settle();
    }

    /// Types the text into the field of the header and searches at once
    void type(const QString& text)
    {
        window->activateWindow();
        if (!QTest::qWaitForWindowActive(window.data()))
        {
            qWarning("The window was not activated");
        }
        field()->setFocus(Qt::OtherFocusReason);
        field()->clear();
        QTest::keyClicks(field(), text);
        controller()->updateSearch();
        settle();
    }

    /// Records the files as opened (Recent)
    void addRecent(const QStringList& paths)
    {
        for (const QString& path : paths)
        {
            QVERIFY(!context->getRecent().recordOpened(path).isError());
        }
        settle();
    }

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

    static int s_counter;
};

int SearchFixture::s_counter = 0;

/// Helpers of the test
class SearchTestHelper
{
public:
    SearchTestHelper() = delete;

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFStylePainter::color(color);
    }

    /// The color over the other (source over)
    static QColor blend(const QColor& over, const QColor& under)
    {
        const qreal alpha = over.alphaF();
        return QColor::fromRgbF(float(over.redF() * alpha + under.redF() * (1.0 - alpha)), float(over.greenF() * alpha + under.greenF() * (1.0 - alpha)),
                                float(over.blueF() * alpha + under.blueF() * (1.0 - alpha)));
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(token(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
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

    /// Factor of the time limits (valgrind is much slower)
    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        if (ok && factor > 0)
        {
            return factor;
        }
        return qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")) ? 50 : 1;
    }

    /// Search data of the static catalogs and the registries
    static pdf::PDFAppSearchData createData(const pdf::PDFAppCommandRegistry* commands, const pdf::PDFSettingRegistry* settings)
    {
        pdf::PDFAppSearchData data;
        data.commandRegistry = commands;
        data.settingRegistry = settings;
        data.helpCatalog = pdf::PDFAppSearchData::getDefaultHelpCatalog();
        data.now = QDateTime(QDate(2026, 10, 21), QTime(12, 0));
        return data;
    }

    static pdf::PDFRecentFile createFile(const QString& path, qint64 lastOpened, bool pinned = false)
    {
        pdf::PDFRecentFile file;
        file.path = path;
        file.name = QFileInfo(path).fileName();
        file.folder = QFileInfo(path).path();
        file.lastOpened = lastOpened;
        file.pinned = pinned;
        return file;
    }

    static QStringList keys(const pdf::PDFAppSearchResults& results, pdf::PDFAppSearchGroup group)
    {
        QStringList list;
        if (const pdf::PDFAppSearchGroupResults* groupResults = results.getGroup(group))
        {
            for (const pdf::PDFAppSearchResult& result : groupResults->results)
            {
                list << result.key;
            }
        }
        return list;
    }

    static QString randomText(QRandomGenerator& generator, int length)
    {
        QString text;
        for (int i = 0; i < length; ++i)
        {
            // Any 16 bit unit, also lone surrogates and controls
            text += QChar(char16_t(generator.bounded(0x10000)));
        }
        return text;
    }
};

class AppSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    // Core matching
    void sanitize_data();
    void sanitize();
    void normalize_data();
    void normalize();
    void matchWord_data();
    void matchWord();
    void toolSearch_data();
    void toolSearch();
    void toolSearchOrder();

    // Engine
    void groups();
    void engineGroups();
    void engineRanking();
    void engineEmptyAndHostile_data();
    void engineEmptyAndHostile();
    void engineRandom();
    void engineTranslations();
    void engineSpeed();
    void openedText();
    void visibleText();
    void painterHelpers();

    // Page
    void pageRoutes_data();
    void pageRoutes();
    void pageStates();
    void pageGroupsAndCounts();
    void pageFilterRoute();
    void pageActivation_data();
    void pageActivation();
    void pageKeyboard();
    void pageNotifications();
    void pageNotificationOfAnotherConnection();
    void pageGeometry();
    void pagePixels();
    void pageRightToLeft();
    void pageMinimalWindow();
    void pageDeletion();
    void pageLanguageChange();
    void pageBigRecentList();

    // Popup
    void popupOpenClose();
    void popupKeyboard();
    void popupEnterWithoutPopup();
    void popupMouse();
    void popupFocusAndWindow();
    void popupNotification();
    void popupDeletion_data();
    void popupDeletion();
    void popupGeometry();
    void popupPixels();
    void popupRightToLeft();
    void popupMinimalWindow();
    void popupCommand();
    void popupDocumentAndVideo();

    void engineEdgeCases();
    void pageDirect();
    void pageFocusAndCounts();
    void popupEdgeCases();

    // Coverage review of step 2 (reachable branches without a test)
    void normalizeDirect_data();
    void normalizeDirect();
    void splitWordsSurrogateCut();
    void isWithinOneEdit_data();
    void isWithinOneEdit();
    void toolSearchTranslations();
    void engineDocumentLimit();
    void engineHelpTopicFiles();
    void engineShortcutReadError();
    void pageWithoutDatabase();
    void pageFocusOfLaterGroup();
    void pageShowAllByKeyboard();
    void pageDeletedRow();
    void popupDisabled();
    void popupLowWindow();
    void popupWithoutAnchorAndShadow();
    void popupShowAllPressedOutside();
    void popupKeysWithoutPopup();
    void popupPressOnWindow();
    void popupEventsOnce();
    void preparedDocumentFields();
    void pageDeletedByRouteReplaced();
    void controllerEdgeCases();

    void screenshots();
    void screenshotsStates();
    void emptyStateFillsColumn();
    void headerClearButton();
};

void AppSearchTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppSearchTest::cleanup()
{
    SearchFixture::settle();
}

// ------------------------------------------------------------------ core

void AppSearchTest::sanitize_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "merge" << "merge";
    QTest::newRow("spaces") << "  merge \t  pdf  " << "merge pdf";
    QTest::newRow("newlines") << "a\nb\r\nc" << "a b c";
    QTest::newRow("controls") << QStringLiteral("me\u0000r\u0007ge") << "merge";
    QTest::newRow("bidi override") << QStringLiteral("\u202Eevil\u202C") << "evil";
    QTest::newRow("zero width") << QStringLiteral("si\u200Bgn") << "sign";
    QTest::newRow("line separator") << QStringLiteral("a\u2028b") << "a b";
    QTest::newRow("lone surrogate") << QString(QChar(0xD800)) + QStringLiteral("x") << "x";
    QTest::newRow("surrogate pair") << QStringLiteral("a\U0001F600b") << QStringLiteral("a\U0001F600b");
    QTest::newRow("empty") << "" << "";
    QTest::newRow("only spaces") << "     " << "";
}

void AppSearchTest::sanitize()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(pdf::PDFSearchQuery::sanitize(text), expected);
}

void AppSearchTest::normalize_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("words");

    QTest::newRow("case") << "Merge PDF" << QStringList({ "merge", "pdf" });
    QTest::newRow("diacritics") << QStringLiteral("Čeština Mérge") << QStringList({ "cestina", "merge" });
    QTest::newRow("punctuation") << "Contract_signed.pdf" << QStringList({ "contract", "signed", "pdf" });
    QTest::newRow("compatibility") << QStringLiteral("ﬁle") << QStringList({ "file" });
    QTest::newRow("numbers") << "Report 2026" << QStringList({ "report", "2026" });
    QTest::newRow("emoji only") << QStringLiteral("\U0001F600") << QStringList();
    QTest::newRow("cyrillic") << QStringLiteral("Документ") << QStringList({ QStringLiteral("документ") });
    QTest::newRow("empty") << "" << QStringList();
}

void AppSearchTest::normalize()
{
    QFETCH(QString, text);
    QFETCH(QStringList, words);
    QCOMPARE(pdf::PDFSearchQuery::splitWords(text), words);
}

void AppSearchTest::matchWord_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("word");
    QTest::addColumn<int>("quality");

    const int none = int(pdf::PDFSearchMatchQuality::None);
    const int typo = int(pdf::PDFSearchMatchQuality::Typo);
    const int prefix = int(pdf::PDFSearchMatchQuality::Prefix);
    const int exact = int(pdf::PDFSearchMatchQuality::Exact);
    QTest::newRow("exact") << "merge" << "merge" << exact;
    QTest::newRow("prefix") << "mer" << "merge" << prefix;
    QTest::newRow("longer query") << "merges" << "merge" << typo;
    QTest::newRow("substitution") << "merga" << "merge" << typo;
    QTest::newRow("transposition") << "mrege" << "merge" << typo;
    QTest::newRow("deletion") << "mrge" << "merge" << typo;
    QTest::newRow("insertion") << "mergee" << "merge" << typo;
    QTest::newRow("prefix typo") << "signt" << "signature" << typo;
    QTest::newRow("short prefix typo") << "sihn" << "signature" << none;
    QTest::newRow("first character") << "epub" << "publish" << none;
    QTest::newRow("short swap") << "sign" << "single" << none;
    QTest::newRow("short no typo") << "mrg" << "merge" << none;
    QTest::newRow("two edits") << "mxrgx" << "merge" << none;
    QTest::newRow("unrelated") << "zzzz" << "merge" << none;
    QTest::newRow("empty query") << "" << "merge" << none;
    QTest::newRow("empty word") << "merge" << "" << none;
    QTest::newRow("swap at end") << "compres" << "compress" << prefix;
    QTest::newRow("swap last") << "comprses" << "compress" << typo;
}

void AppSearchTest::matchWord()
{
    QFETCH(QString, query);
    QFETCH(QString, word);
    QFETCH(int, quality);
    QCOMPARE(int(pdf::PDFSearchQuery::matchWord(query, word)), quality);
}

void AppSearchTest::toolSearch_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<int>("field");

    // The sample queries of tools.md (join, combine, bind -> Merge), a typo, diacritics
    QTest::newRow("title") << "merge" << "pages.merge" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
    QTest::newRow("join") << "join" << "pages.merge" << int(pdf::PDFDocumentToolSearchMatch::Field::Keyword);
    QTest::newRow("combine") << "combine" << "pages.merge" << int(pdf::PDFDocumentToolSearchMatch::Field::Keyword);
    QTest::newRow("typo") << "mrege" << "pages.merge" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
    QTest::newRow("diacritics") << QStringLiteral("Mérge") << "pages.merge" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
    QTest::newRow("case") << "COMPRESS" << "optimize.compress" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
    QTest::newRow("prefix") << "compr" << "optimize.compress" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
    QTest::newRow("two words") << "sign certificate" << "protect.sign" << int(pdf::PDFDocumentToolSearchMatch::Field::Title);
}

void AppSearchTest::toolSearch()
{
    QFETCH(QString, query);
    QFETCH(QString, toolId);
    QFETCH(int, field);

    const std::vector<pdf::PDFDocumentToolSearchMatch> matches = pdf::PDFDocumentToolSearch::search(query);
    QVERIFY(!matches.empty());
    QVERIFY(matches.front().tool);
    QCOMPARE(matches.front().tool->getId(), toolId);
    QCOMPARE(int(matches.front().field), field);
    if (matches.front().field == pdf::PDFDocumentToolSearchMatch::Field::Keyword)
    {
        QVERIFY(!matches.front().keyword.isEmpty());
    }
}

void AppSearchTest::toolSearchOrder()
{
    // Exact title > title prefix > keyword; a tie keeps the order of the catalog
    const std::vector<pdf::PDFDocumentToolSearchMatch> sign = pdf::PDFDocumentToolSearch::search(QStringLiteral("sign"));
    QVERIFY(sign.size() >= 3);
    QCOMPARE(sign[0].tool->getId(), QStringLiteral("protect.sign"));
    QCOMPARE(sign[1].tool->getId(), QStringLiteral("protect.sign-by-hand"));
    for (size_t i = 1; i < sign.size(); ++i)
    {
        QVERIFY(sign[i - 1].score >= sign[i].score);
        if (sign[i - 1].score == sign[i].score)
        {
            QVERIFY(pdf::PDFDocumentToolCatalog::getToolIndex(sign[i - 1].tool->getId()) < pdf::PDFDocumentToolCatalog::getToolIndex(sign[i].tool->getId()));
        }
    }

    // Title matches come before keyword matches
    bool isKeywordSeen = false;
    for (const pdf::PDFDocumentToolSearchMatch& match : sign)
    {
        if (match.field != pdf::PDFDocumentToolSearchMatch::Field::Title)
        {
            isKeywordSeen = true;
        }
        else
        {
            QVERIFY(!isKeywordSeen);
        }
    }

    QVERIFY(pdf::PDFDocumentToolSearch::search(QString()).empty());
    QVERIFY(pdf::PDFDocumentToolSearch::search(QStringLiteral("!!!")).empty());
    QVERIFY(pdf::PDFDocumentToolSearch::search(QStringLiteral("qqqqqqqq")).empty());
    QVERIFY(!pdf::PDFDocumentToolSearch::matchTool(pdf::PDFDocumentToolCatalog::getTools().front(), pdf::PDFSearchQuery()));

    // The quality of a text match
    const pdf::PDFSearchQuery query(QStringLiteral("rep sign"));
    QCOMPARE(query.matchText(QStringLiteral("Report signed")), pdf::PDFSearchMatchQuality::Prefix);
    QCOMPARE(query.matchText(QStringLiteral("Report")), pdf::PDFSearchMatchQuality::None);
    QCOMPARE(pdf::PDFSearchQuery(QStringLiteral("report")).matchText(QStringLiteral("report")), pdf::PDFSearchMatchQuality::Exact);
    QCOMPARE(pdf::PDFSearchQuery().matchText(QStringLiteral("report")), pdf::PDFSearchMatchQuality::None);
    QVERIFY(!pdf::PDFSearchQuery(QStringLiteral("x")).match({}));
    QVERIFY(!pdf::PDFSearchQuery(QStringLiteral("  !! ")).match({ pdf::PDFSearchField({ QStringLiteral("x") }, 10, 5, 1) }));
    QVERIFY(pdf::PDFSearchQuery(QStringLiteral("x")).match({ pdf::PDFSearchField({ QStringLiteral("x") }, 10, 5, 1) }));
    QCOMPARE(pdf::PDFSearchQuery::sanitize(QStringLiteral("abc"), 0), QString());
    QCOMPARE(pdf::PDFSearchQuery::splitWords(QStringLiteral("a b c"), 0), QStringList());
    QCOMPARE(pdf::PDFSearchQuery::splitWords(QStringLiteral("a b c"), 2), QStringList({ "a", "b" }));
    QCOMPARE(pdf::PDFSearchQuery::splitWords(QString(70, QLatin1Char('x'))).front().size(), pdf::PDFSearchQuery::MAXIMUM_WORD_LENGTH);
    QCOMPARE(pdf::PDFSearchField().getScore(pdf::PDFSearchMatchQuality::None), 0);
}

// ---------------------------------------------------------------- engine

void AppSearchTest::groups()
{
    QCOMPARE(pdf::PDFAppSearchGroups::getGroups().size(), size_t(5));
    for (const pdf::PDFAppSearchGroup group : pdf::PDFAppSearchGroups::getGroups())
    {
        const QString id = pdf::PDFAppSearchGroups::getId(group);
        QVERIFY(!id.isEmpty());
        QCOMPARE(pdf::PDFAppSearchGroups::findGroup(id), std::make_optional(group));
        QVERIFY(!pdf::PDFAppSearchGroups::getTitle(group).isEmpty());
        QVERIFY(!pdf::PDFAppSearchGroups::getName(group).isEmpty());
    }

    QVERIFY(!pdf::PDFAppSearchGroups::findGroup(QStringLiteral("all")));
    QVERIFY(!pdf::PDFAppSearchGroups::findGroup(QString()));
    QVERIFY(!pdf::PDFAppSearchGroups::findGroup(QStringLiteral("TOOLS")));

    const pdf::PDFAppSearchGroup invalid = static_cast<pdf::PDFAppSearchGroup>(42);
    QVERIFY(pdf::PDFAppSearchGroups::getId(invalid).isEmpty());
    QVERIFY(pdf::PDFAppSearchGroups::getTitle(invalid).isEmpty());
    QVERIFY(pdf::PDFAppSearchGroups::getName(invalid).isEmpty());

    pdf::PDFAppSearchResults results;
    QVERIFY(!results.getGroup(invalid));
    QCOMPARE(results.getCount(invalid), 0);
    QVERIFY(!results.findResult(invalid, QStringLiteral("x")));
    QCOMPARE(pdf::PDFAppSearchEngine::getToolHue(42), pdf::PDFAccentHue::Neutral);
}

void AppSearchTest::engineGroups()
{
    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    pdf::PDFSettingRegistry settings;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&settings);

    pdf::PDFAppSearchData data = SearchTestHelper::createData(&commands, &settings);
    const qint64 yesterday = QDateTime(QDate(2026, 10, 20), QTime(9, 0)).toMSecsSinceEpoch();
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("C:/Users/Jakub/Documents/Contract_signed.pdf"), yesterday));
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("D:/Projects/Sales/Signed_Offer_2026.pdf"), yesterday - 86400000LL * 40, true));
    data.recentFiles.push_back(SearchTestHelper::createFile(QString(), yesterday));

    // A user shortcut replaces the default one
    data.userShortcuts[pdf::PDFAppCommands::newTab()] = QStringLiteral("Ctrl+Shift+N");

    const pdf::PDFAppSearchEngine engine;

    // Tools: the route /tools/{toolId}, the badge of the category
    const pdf::PDFAppSearchResults merge = engine.search(QStringLiteral("merge"), data);
    const pdf::PDFAppSearchResult* tool = merge.findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge"));
    QVERIFY(tool);
    QCOMPARE(tool->route, pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")));
    QCOMPARE(tool->hue, pdf::PDFAccentHue::Amber);
    QCOMPARE(tool->action, pdf::PDFAppSearchResult::Action::Navigate);
    QVERIFY(!tool->iconName.isEmpty());
    QVERIFY(!tool->meta.isEmpty());
    QCOMPARE(merge.getGroup(pdf::PDFAppSearchGroup::Tools)->results.front().key, QStringLiteral("pages.merge"));

    // Reason of a match by a keyword
    const pdf::PDFAppSearchResults join = engine.search(QStringLiteral("join"), data);
    const pdf::PDFAppSearchResult* joinTool = join.findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge"));
    QVERIFY(joinTool);
    QVERIFY(joinTool->subtitle.contains(QStringLiteral("“join")));

    // Commands: the shortcut of the user, the action RunCommand
    const pdf::PDFAppSearchResults tab = engine.search(QStringLiteral("new tab"), data);
    const pdf::PDFAppSearchResult* command = tab.findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab());
    QVERIFY(command);
    QCOMPARE(command->action, pdf::PDFAppSearchResult::Action::RunCommand);
    QCOMPARE(command->commandId, pdf::PDFAppCommands::newTab());
    QCOMPARE(command->meta, QKeySequence(QStringLiteral("Ctrl+Shift+N")).toString(QKeySequence::NativeText));
    QCOMPARE(tab.getGroup(pdf::PDFAppSearchGroup::Commands)->results.front().key, pdf::PDFAppCommands::newTab());

    // Settings: the route of the section and the anchor
    const pdf::PDFAppSearchResults color = engine.search(QStringLiteral("color scheme"), data);
    const pdf::PDFAppSearchResult* setting = color.findResult(pdf::PDFAppSearchGroup::Settings, pdf::PDFSettingKeys::colorScheme());
    QVERIFY(setting);
    QCOMPARE(setting->route.toString(), QStringLiteral("/settings/general#color-scheme"));
    QVERIFY(setting->subtitle.startsWith(QStringLiteral("Settings › General")));
    QCOMPARE(engine.search(QStringLiteral("dark mode"), data).getCount(pdf::PDFAppSearchGroup::Settings), 1);

    // Documents: the name and the folder, the action OpenDocument, Pinned / Recent
    const pdf::PDFAppSearchResults sign = engine.search(QStringLiteral("signed"), data);
    QCOMPARE(sign.getCount(pdf::PDFAppSearchGroup::Documents), 2);
    const pdf::PDFAppSearchResult* contract = sign.findResult(pdf::PDFAppSearchGroup::Documents, QStringLiteral("C:/Users/Jakub/Documents/Contract_signed.pdf"));
    QVERIFY(contract);
    QCOMPARE(contract->action, pdf::PDFAppSearchResult::Action::OpenDocument);
    QCOMPARE(contract->title, QStringLiteral("Contract_signed.pdf"));
    QVERIFY(contract->isFileName);
    QVERIFY(contract->subtitle.endsWith(QStringLiteral("Opened yesterday")));
    QCOMPARE(contract->meta, QStringLiteral("Recent"));
    const pdf::PDFAppSearchResult* offer = sign.findResult(pdf::PDFAppSearchGroup::Documents, QStringLiteral("D:/Projects/Sales/Signed_Offer_2026.pdf"));
    QVERIFY(offer);
    QCOMPARE(offer->meta, QStringLiteral("Pinned"));
    QCOMPARE(engine.search(QStringLiteral("projects sales"), data).getCount(pdf::PDFAppSearchGroup::Documents), 1);

    // Help: topics and headings route to /help/<id>#<anchor>; videos open https
    const pdf::PDFAppSearchResults portable = engine.search(QStringLiteral("portable"), data);
    const pdf::PDFAppSearchResult* topic = portable.findResult(pdf::PDFAppSearchGroup::Help, QStringLiteral("/help/portable-mode"));
    QVERIFY(topic);
    QCOMPARE(topic->route.toString(), QStringLiteral("/help/portable-mode"));
    QCOMPARE(topic->meta, QStringLiteral("Manual"));
    QVERIFY(topic->subtitle.startsWith(QStringLiteral("User manual")));

    bool hasHeading = false;
    const pdf::PDFAppSearchResults headingResults = engine.search(QStringLiteral("what is searched"), data);
    for (const pdf::PDFAppSearchResult& result : headingResults.getGroup(pdf::PDFAppSearchGroup::Help)->results)
    {
        if (result.key.contains(QLatin1Char('#')))
        {
            hasHeading = true;
            QVERIFY(result.route.isValid());
            QVERIFY(!result.route.getFragment().isEmpty());
        }
    }
    QVERIFY(hasHeading);

    const pdf::PDFSupportVideo& video = pdf::PDFSupportVideoCatalog::getVideos().front();
    const pdf::PDFAppSearchResults videoResults = engine.search(video.getTitle(), data);
    const pdf::PDFAppSearchResult* videoResult = videoResults.findResult(pdf::PDFAppSearchGroup::Help, QStringLiteral("video:%1").arg(video.getId()));
    QVERIFY(videoResult);
    QCOMPARE(videoResult->action, pdf::PDFAppSearchResult::Action::OpenUrl);
    QVERIFY(videoResult->isExternal);
    QCOMPARE(videoResult->url.scheme(), QStringLiteral("https"));
    QCOMPARE(videoResult->meta, QStringLiteral("YouTube"));

    data.includeVideos = false;
    QVERIFY(!engine.search(video.getTitle(), data).findResult(pdf::PDFAppSearchGroup::Help, QStringLiteral("video:%1").arg(video.getId())));

    // Without the registries and the manual only the tools and the videos remain
    pdf::PDFAppSearchData empty;
    const pdf::PDFAppSearchResults emptyResults = engine.search(QStringLiteral("tab"), empty);
    QCOMPARE(emptyResults.getCount(pdf::PDFAppSearchGroup::Commands), 0);
    QCOMPARE(emptyResults.getCount(pdf::PDFAppSearchGroup::Settings), 0);

    // A null context loads the static catalogs only
    const pdf::PDFAppSearchData loaded = pdf::PDFAppSearchData::load(nullptr);
    QVERIFY(!loaded.commandRegistry);
    QVERIFY(loaded.helpCatalog);
    QVERIFY(loaded.recentFiles.empty());
}

void AppSearchTest::engineRanking()
{
    pdf::PDFAppSearchData data = SearchTestHelper::createData(nullptr, nullptr);
    data.includeVideos = false;
    const qint64 time = QDateTime(QDate(2026, 10, 1), QTime(9, 0)).toMSecsSinceEpoch();
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/Reports/annual.pdf"), time));     // folder only
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/b/reportage.pdf"), time));        // title prefix
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/b/report.pdf"), time));           // title exact
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/b/report summary.pdf"), time));   // title starts by the query
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/b/repotr.pdf"), time));           // a typo

    const pdf::PDFAppSearchEngine engine;
    const pdf::PDFAppSearchResults ranked = engine.search(QStringLiteral("report"), data);
    const QStringList order = SearchTestHelper::keys(ranked, pdf::PDFAppSearchGroup::Documents);
    QCOMPARE(order.size(), 5);
    QCOMPARE(order[0], QStringLiteral("/a/b/report.pdf"));
    QCOMPARE(order.last(), QStringLiteral("/a/Reports/annual.pdf"));
    QVERIFY(order.indexOf(QStringLiteral("/a/b/report summary.pdf")) < order.indexOf(QStringLiteral("/a/b/repotr.pdf")));
    QVERIFY(order.indexOf(QStringLiteral("/a/b/report summary.pdf")) < order.indexOf(QStringLiteral("/a/b/reportage.pdf")));

    // The name keeps its extension: "report.pdf" and "report summary.pdf" both
    // start by the query (no exact title), the order of Recent decides the tie
    const pdf::PDFAppSearchResult* exact = ranked.findResult(pdf::PDFAppSearchGroup::Documents, QStringLiteral("/a/b/report.pdf"));
    const pdf::PDFAppSearchResult* summary = ranked.findResult(pdf::PDFAppSearchGroup::Documents, QStringLiteral("/a/b/report summary.pdf"));
    QVERIFY(exact && summary);
    QCOMPARE(exact->score, summary->score);
    std::swap(data.recentFiles[2], data.recentFiles[3]);
    QCOMPARE(SearchTestHelper::keys(engine.search(QStringLiteral("report"), data), pdf::PDFAppSearchGroup::Documents).front(), QStringLiteral("/a/b/report summary.pdf"));
    QCOMPARE(SearchTestHelper::keys(engine.search(QStringLiteral("report pdf"), data), pdf::PDFAppSearchGroup::Documents).front(), QStringLiteral("/a/b/report.pdf"));

    // The limit of a group keeps the count of all matches
    pdf::PDFAppSearchData many = SearchTestHelper::createData(nullptr, nullptr);
    for (int i = 0; i < pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS + 20; ++i)
    {
        many.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/x/report_%1.pdf").arg(i), time + i));
    }
    const pdf::PDFAppSearchResults manyResults = engine.search(QStringLiteral("report"), many);
    QCOMPARE(manyResults.getCount(pdf::PDFAppSearchGroup::Documents), pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS + 20);
    QCOMPARE(int(manyResults.getGroup(pdf::PDFAppSearchGroup::Documents)->results.size()), pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS);
    std::vector<pdf::PDFAppSearchResult> all;
    pdf::PDFAppDocumentSearchProvider().search(pdf::PDFSearchQuery(QStringLiteral("report")), many, all);
    QCOMPARE(int(all.size()), pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS + 20);
    QCOMPARE(pdf::PDFAppDocumentSearchProvider().getGroup(), pdf::PDFAppSearchGroup::Documents);
    QCOMPARE(manyResults.getTotalCount(), manyResults.getCount(pdf::PDFAppSearchGroup::Documents) + manyResults.getCount(pdf::PDFAppSearchGroup::Tools) +
                                             manyResults.getCount(pdf::PDFAppSearchGroup::Help));
}

void AppSearchTest::engineEmptyAndHostile_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("isEmpty");

    QTest::newRow("empty") << QString() << true;
    QTest::newRow("spaces") << "    " << true;
    QTest::newRow("punctuation") << "!!! ??? ..." << true;
    QTest::newRow("controls") << QStringLiteral("\u0001\u0002\u0003") << true;
    QTest::newRow("10 kB") << QString(10240, QLatin1Char('a')) << false;
    QTest::newRow("10 kB words") << QStringLiteral("merge ").repeated(2000) << false;
    QTest::newRow("html") << "<b>merge</b><img src=x>" << false;
    QTest::newRow("percent") << "%1 %2 %n" << false;
    QTest::newRow("route") << "/settings/../..%00" << false;
    QTest::newRow("bidi") << QStringLiteral("\u202Emerge") << false;
    QTest::newRow("surrogates") << QString(QChar(0xDC00)) + QString(QChar(0xD800)) << true;
}

void AppSearchTest::engineEmptyAndHostile()
{
    QFETCH(QString, text);
    QFETCH(bool, isEmpty);

    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    pdf::PDFSettingRegistry settings;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&settings);
    const pdf::PDFAppSearchData data = SearchTestHelper::createData(&commands, &settings);

    const pdf::PDFAppSearchEngine engine;
    const pdf::PDFAppSearchResults results = engine.search(text, data);
    QCOMPARE(results.isEmptyQuery, isEmpty);
    QVERIFY(results.text.size() <= pdf::PDFSearchQuery::MAXIMUM_LENGTH);
    QCOMPARE(results.groups.size(), size_t(5));
    if (isEmpty)
    {
        QCOMPARE(results.getTotalCount(), 0);
    }
}

void AppSearchTest::engineRandom()
{
    // Random untrusted texts never crash and give the same result again
    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    pdf::PDFSettingRegistry settings;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&settings);
    pdf::PDFAppSearchData data = SearchTestHelper::createData(&commands, &settings);
    QRandomGenerator generator(20261021);
    for (int i = 0; i < 50; ++i)
    {
        data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/r/") + SearchTestHelper::randomText(generator, 12) + QStringLiteral(".pdf"), i));
    }

    const pdf::PDFAppSearchEngine engine;
    for (int i = 0; i < 200; ++i)
    {
        const QString text = SearchTestHelper::randomText(generator, int(generator.bounded(40)));
        const pdf::PDFAppSearchResults first = engine.search(text, data);
        const pdf::PDFAppSearchResults second = engine.search(text, data);
        QCOMPARE(first.getTotalCount(), second.getTotalCount());
        QCOMPARE(first.text, second.text);
        QVERIFY(!pdf::PDFAppSearchEngine::getVisibleText(text, 64).isNull() || text.isEmpty());
    }
}

void AppSearchTest::engineTranslations()
{
    // The translated texts and the English ones are searched both
    SearchTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));

    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    pdf::PDFSettingRegistry settings;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&settings);
    const pdf::PDFAppSearchData data = SearchTestHelper::createData(&commands, &settings);
    const pdf::PDFAppSearchEngine engine;

    const pdf::PDFAppSearchResults german = engine.search(QStringLiteral("zusammenfuhren"), data);
    const pdf::PDFAppSearchResult* merge = german.findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge"));
    QVERIFY(merge);
    QCOMPARE(merge->title, QStringLiteral("Zusammenführen"));
    QVERIFY(engine.search(QStringLiteral("merge"), data).findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge")));

    QVERIFY(engine.search(QStringLiteral("neuer"), data).findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab()));
    QVERIFY(engine.search(QStringLiteral("new tab"), data).findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab()));
    QVERIFY(engine.search(QStringLiteral("farbschema"), data).findResult(pdf::PDFAppSearchGroup::Settings, pdf::PDFSettingKeys::colorScheme()));
    QVERIFY(engine.search(QStringLiteral("color scheme"), data).findResult(pdf::PDFAppSearchGroup::Settings, pdf::PDFSettingKeys::colorScheme()));

    QCoreApplication::removeTranslator(&translator);
}

void AppSearchTest::engineSpeed()
{
    // A big Recent list (pinned files are never pruned): one search of the popup
    // must not block the user interface
    pdf::PDFAppCommandRegistry commands;
    pdf::PDFAppCommands::registerBuiltinCommands(&commands);
    pdf::PDFSettingRegistry settings;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&settings);
    pdf::PDFAppSearchData data = SearchTestHelper::createData(&commands, &settings);
    constexpr int COUNT = 20000;
    for (int i = 0; i < COUNT; ++i)
    {
        data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("C:/Users/Someone/Documents/Folder %1/Quarterly report %2 (draft).pdf").arg(i % 97).arg(i), i, i % 3 == 0));
    }

    const pdf::PDFAppSearchEngine engine;
    QElapsedTimer timer;
    timer.start();
    const pdf::PDFAppSearchResults results = engine.search(QStringLiteral("quartely rep"), data);
    const qint64 elapsed = timer.elapsed();
    qInfo("Search of %d recent files: %lld ms", COUNT, elapsed);
    QCOMPARE(results.getCount(pdf::PDFAppSearchGroup::Documents), COUNT);
    // About 100 ms in Release with 30 test processes at once; the limit leaves
    // room for builds running on the machine at the same time
    QVERIFY2(elapsed < 1000 * SearchTestHelper::getTimeoutFactor(), qPrintable(QStringLiteral("%1 ms").arg(elapsed)));

    timer.restart();
    const pdf::PDFAppSearchResults none = engine.search(QStringLiteral("zzzzzz"), data);
    qInfo("Search of %d recent files without a match: %lld ms", COUNT, timer.elapsed());
    QCOMPARE(none.getCount(pdf::PDFAppSearchGroup::Documents), 0);
}

void AppSearchTest::openedText()
{
    // A fixed "now" and a fixed locale: the result depends neither on the real
    // date (midnight), nor on the locale of the machine; the times are local
    // times of the same zone as "now", so any time zone gives the same days
    const QLocale defaultLocale;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const QDateTime now(QDate(2026, 10, 21), QTime(12, 0));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 10, 21), QTime(0, 0)).toMSecsSinceEpoch(), now), QStringLiteral("Opened today"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 10, 21), QTime(8, 0)).toMSecsSinceEpoch(), now), QStringLiteral("Opened today"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 10, 20), QTime(23, 59)).toMSecsSinceEpoch(), now), QStringLiteral("Opened yesterday"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 10, 20), QTime(0, 0)).toMSecsSinceEpoch(), now), QStringLiteral("Opened yesterday"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 10, 19), QTime(23, 59)).toMSecsSinceEpoch(), now), QStringLiteral("Oct 19"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 9, 28), QTime(8, 0)).toMSecsSinceEpoch(), now), QStringLiteral("Sep 28"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2025, 9, 12), QTime(8, 0)).toMSecsSinceEpoch(), now), QStringLiteral("Sep 12, 2025"));

    // Yesterday of the first day of a year is in the previous year
    const QDateTime newYear(QDate(2027, 1, 1), QTime(0, 30));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 12, 31), QTime(23, 0)).toMSecsSinceEpoch(), newYear), QStringLiteral("Opened yesterday"));
    QCOMPARE(pdf::PDFAppSearchEngine::getOpenedText(QDateTime(QDate(2026, 12, 30), QTime(23, 0)).toMSecsSinceEpoch(), newYear), QStringLiteral("Dec 30, 2026"));

    // Not a time, or a time out of the range of the dates
    QVERIFY(pdf::PDFAppSearchEngine::getOpenedText(0, now).isEmpty());
    QVERIFY(pdf::PDFAppSearchEngine::getOpenedText(-5, now).isEmpty());
    QVERIFY(pdf::PDFAppSearchEngine::getOpenedText(1000, QDateTime()).isEmpty());
    const QString farFuture = pdf::PDFAppSearchEngine::getOpenedText(std::numeric_limits<qint64>::max(), now);
    QLocale::setDefault(defaultLocale);
    QVERIFY2(farFuture.isEmpty(), qPrintable(farFuture));
}

void AppSearchTest::visibleText()
{
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("sign"), 64), QStringLiteral("sign"));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("a\u202Eb"), 64), QStringLiteral("a%E2%80%AEb"));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QString(QChar(0xD800)), 64), QString(QChar(QChar::ReplacementCharacter)));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("abcdef"), 3), QStringLiteral("abc") + QChar(0x2026));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("abc"), 0), QString());
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("\U0001F600"), 64), QStringLiteral("\U0001F600"));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("\U000E0001"), 64), QStringLiteral("%F3%A0%80%81"));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("a b c"), 64), QStringLiteral("a%E2%80%A8b%E2%80%A9c"));
    QCOMPARE(pdf::PDFAppSearchEngine::getVisibleText(QStringLiteral("a\u0007​b"), 64), QStringLiteral("a%07%E2%80%8Bb"));
    QCOMPARE(pdf::PDFAppSearchEngine::getSettingsSectionTitle(QStringLiteral("security")), QStringLiteral("Security"));
    QCOMPARE(pdf::PDFAppSearchEngine::getSettingsSectionTitle(QStringLiteral("unknown")), QStringLiteral("unknown"));
}

void AppSearchTest::painterHelpers()
{
    QCOMPARE(pdf::PDFAppSearchPainter::getCountText(4, 6), QStringLiteral("4 of 6"));
    QCOMPARE(pdf::PDFAppSearchPainter::getCountText(6, 6), QStringLiteral("6"));
    QCOMPARE(pdf::PDFAppSearchPainter::getCountText(-1, 3), QStringLiteral("3"));
    QCOMPARE(pdf::PDFAppSearchPainter::getKeyCapSize(QStringLiteral("Enter")).height(), 18.0);

    // Defensive drawing: a null painter and empty rectangles draw nothing
    pdf::PDFAppSearchResult result;
    const pdf::PDFComponentPainter::State state;
    pdf::PDFAppSearchPainter::paintResultRow(nullptr, QRectF(0, 0, 100, 44), result, state, true);
    pdf::PDFAppSearchPainter::paintGroupHeader(nullptr, QRectF(0, 0, 100, 27), QStringLiteral("T"), QStringLiteral("1"), state);
    pdf::PDFAppSearchPainter::paintShowAll(nullptr, QRectF(0, 0, 100, 31), QStringLiteral("S"), state);
    pdf::PDFAppSearchPainter::paintKeyCap(nullptr, QRectF(0, 0, 10, 18), QStringLiteral("K"));
    QImage image(10, 10, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    pdf::PDFAppSearchPainter::paintResultRow(&painter, QRectF(), result, state, true);
    pdf::PDFAppSearchPainter::paintGroupHeader(&painter, QRectF(), QStringLiteral("T"), QStringLiteral("1"), state);
    pdf::PDFAppSearchPainter::paintShowAll(&painter, QRectF(), QStringLiteral("S"), state);
    pdf::PDFAppSearchPainter::paintKeyCap(&painter, QRectF(), QStringLiteral("K"));

    // A disabled row: text/disabled, no hover
    result.title = QStringLiteral("Merge");
    result.meta = QStringLiteral("Pages");
    result.isExternal = true;
    pdf::PDFAppSearchPainter::paintResultRow(&painter, QRectF(0, 0, 10, 10), result, pdf::PDFComponentPainter::State(QStyle::State_None), false);
    pdf::PDFAppSearchPainter::paintResultRow(&painter, QRectF(0, 0, 10, 10), result, pdf::PDFComponentPainter::State(QStyle::State_Enabled | QStyle::State_Sunken | QStyle::State_HasFocus), false);
    pdf::PDFAppSearchPainter::paintShowAll(&painter, QRectF(0, 0, 10, 10), QStringLiteral("S"), pdf::PDFComponentPainter::State(QStyle::State_None));
    pdf::PDFAppSearchPainter::paintShowAll(&painter, QRectF(0, 0, 10, 10), QStringLiteral("S"), pdf::PDFComponentPainter::State(QStyle::State_Enabled | QStyle::State_Sunken | QStyle::State_HasFocus, Qt::RightToLeft));
    pdf::PDFAppSearchPainter::paintGroupHeader(&painter, QRectF(0, 0, 10, 10), QStringLiteral("T"), QStringLiteral("1"), pdf::PDFComponentPainter::State(QStyle::State_None));
    painter.end();

    // Activation without a main window does nothing
    QWidget widget;
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(&widget, pdf::PDFAppCommands::newTab()));
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(nullptr, pdf::PDFAppCommands::newTab()));
    QVERIFY(!pdf::PDFAppSearchActions::openDocument(&widget, QStringLiteral("/x.pdf")));
    QVERIFY(!pdf::PDFAppSearchActions::openDocument(nullptr, QStringLiteral("/x.pdf")));

    // A controller of no field and a popup without a window are harmless
    pdf::PDFAppSearchController controller(nullptr);
    QVERIFY(!controller.isPopupOpen());
    controller.updateSearch();
    controller.closePopup();
    QVERIFY(!pdf::PDFAppSearchController::find(nullptr));

    pdf::PDFAppSearchPopup popup(nullptr);
    popup.open();
    popup.placeBelow(QRect(0, 0, 10, 10));
    QCOMPARE(popup.getRowCount(), 0);
    QVERIFY(!popup.getCurrentResult());
    QVERIFY(popup.getRowRect(0).isNull());
    QVERIFY(popup.getShowAllRect().isNull());
    popup.activateCurrent();
    popup.moveCurrentRow(1);
    QCOMPARE(popup.getCurrentRow(), -1);
}

// ------------------------------------------------------------------ page

void AppSearchTest::pageRoutes_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<bool>("isSearchPage");
    QTest::addColumn<QString>("group");

    QTest::newRow("text") << "/search?q=sign" << true << "";
    QTest::newRow("group") << "/search?q=sign&group=tools" << true << "tools";
    QTest::newRow("every group") << "/search?q=sign&group=help" << true << "help";
    QTest::newRow("empty text") << "/search?q=" << true << "";
    QTest::newRow("no text") << "/search" << true << "";
    QTest::newRow("invalid group") << "/search?q=sign&group=all" << false << "";
    QTest::newRow("unknown key") << "/search?q=sign&x=1" << false << "";
    QTest::newRow("encoded") << "/search?q=%E2%80%AEsign" << true << "";
    QTest::newRow("control") << "/search?q=%00" << false << "";
}

void AppSearchTest::pageRoutes()
{
    QFETCH(QString, route);
    QFETCH(bool, isSearchPage);
    QFETCH(QString, group);

    SearchFixture fixture;
    fixture.navigate(route);
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QCOMPARE(page != nullptr, isSearchPage);
    if (!page)
    {
        QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.currentPage()));
        return;
    }

    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.homeTab()));
    QCOMPARE(page->getGroup() ? pdf::PDFAppSearchGroups::getId(*page->getGroup()) : QString(), group);
    QCOMPARE(page->getTitle(), QStringLiteral("Search results"));
    QCOMPARE(fixture.homeTab()->getHeader()->getTitleLabel()->text(), QStringLiteral("Search results"));
    QCOMPARE(fixture.homeTab()->getHeader()->getSubtitleLabel()->text(), page->getHeaderSubtitle());

    // The chip of the group is checked; the field shows the text of the route
    QVERIFY(page->getChip(page->getGroup())->isChecked());
    QCOMPARE(fixture.field()->text(), page->getSearchText());
}

void AppSearchTest::pageStates()
{
    SearchFixture fixture;

    // Results (941:7081): the subtitle with the count, the rows, the current row
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(!page->getEmptyState());
    QVERIFY(!page->getRows().isEmpty());
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("%1 results for “sign” in tools, commands, settings, documents and help.").arg(page->getResults().getTotalCount()));
    QCOMPARE(page->getCurrentRow(), page->getRows().front());

    // No results (955:142130): the empty state, Search the User Manual opens the Help tab
    fixture.navigate(QStringLiteral("/search?q=epub"));
    page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(page->getEmptyState());
    QVERIFY(page->getRows().isEmpty());
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("No results for “epub” in tools, commands, settings, documents and help."));
    QVERIFY(page->getEmptyState()->findChild<pdf::PDFLabel*>(QStringLiteral("searchEmptyTitle"))->text() == QStringLiteral("No results for “epub”"));
    QVERIFY(page->getSearchManualButton());
    QVERIFY(page->getSearchManualButton()->isVisible());
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    page->getSearchManualButton()->click();
    SearchFixture::settle();
    QVERIFY(!navigated.isEmpty());
    QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::help(pdf::PDFAppRoutes::getDefaultHelpTopic()));
    QCOMPARE(fixture.currentTab()->getKind(), pdf::PDFAppTabKind::Help);

    // Empty text: the hint without the button
    fixture.window->activateTab(fixture.homeTab());
    fixture.navigate(QStringLiteral("/search?q=%20%20"));
    page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(page->getEmptyState());
    QVERIFY(!page->getSearchManualButton()->isVisible());
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Type in the search field to find tools, commands, settings, documents and help."));

    // A group without a result
    fixture.navigate(QStringLiteral("/search?q=merge&group=documents"));
    page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(page->getEmptyState());
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("No results for “merge” in documents."));

    // A group with results
    fixture.navigate(QStringLiteral("/search?q=merge&group=tools"));
    page = fixture.searchPage();
    QVERIFY(page);
    const int toolCount = page->getResults().getCount(pdf::PDFAppSearchGroup::Tools);
    QCOMPARE(page->getHeaderSubtitle(), toolCount == 1 ? QStringLiteral("1 result for “merge” in tools.") : QStringLiteral("%1 results for “merge” in tools.").arg(toolCount));

    // A hostile text is shown visibly (no bidirectional override, no rich text)
    fixture.navigate(QStringLiteral("/search?q=%E2%80%AE%3Cb%3Ex"));
    page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(!page->getHeaderSubtitle().contains(QChar(0x202E)));
    QVERIFY(page->getHeaderSubtitle().contains(QStringLiteral("“<b>x”")));
    QCOMPARE(fixture.homeTab()->getHeader()->getSubtitleLabel()->textFormat(), Qt::PlainText);
}

void AppSearchTest::pageGroupsAndCounts()
{
    SearchFixture fixture;
    QStringList paths;
    for (int i = 0; i < 8; ++i)
    {
        paths << QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Signed_%1.pdf").arg(i));
    }
    fixture.addRecent(paths);

    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const pdf::PDFAppSearchResults& results = page->getResults();

    // Chips: All and the groups with the counts of the engine
    QCOMPARE(page->getChip(std::nullopt)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), results.getTotalCount());
    int shownRows = 0;
    int showAll = 0;
    for (const pdf::PDFAppSearchGroup group : pdf::PDFAppSearchGroups::getGroups())
    {
        QCOMPARE(page->getChip(group)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), results.getCount(group));
        shownRows += qMin(results.getCount(group), pdf::PDFAppSearchPage::ALL_GROUP_ROWS);
        showAll += results.getCount(group) > pdf::PDFAppSearchPage::ALL_GROUP_ROWS ? 1 : 0;
    }
    QCOMPARE(page->getRows().size(), shownRows);
    QCOMPARE(page->getShowAllButtons().size(), showAll);
    QVERIFY(showAll >= 1);
    QCOMPARE(results.getCount(pdf::PDFAppSearchGroup::Documents), 8);

    // The group headers show the counts of all results
    for (pdf::PDFAppSearchGroupHeader* header : page->getGroupHeaders())
    {
        bool ok = false;
        QVERIFY(header->getCount().toInt(&ok) > 0 && ok);
    }

    // Show all (8) of Documents filters the group
    pdf::PDFAppSearchShowAllButton* documents = nullptr;
    for (pdf::PDFAppSearchShowAllButton* button : page->getShowAllButtons())
    {
        if (button->getGroup() == pdf::PDFAppSearchGroup::Documents)
        {
            documents = button;
        }
    }
    QVERIFY(documents);
    QCOMPARE(documents->getCount(), 8);
    QVERIFY(documents->text().contains(QStringLiteral("(8)")));
    documents->click();
    SearchFixture::settle();
    QCOMPARE(fixture.searchPage(), page);
    QCOMPARE(page->getGroup(), std::make_optional(pdf::PDFAppSearchGroup::Documents));
    QCOMPARE(page->getRows().size(), 8);
    QVERIFY(page->getShowAllButtons().isEmpty());
    QVERIFY(page->getChip(pdf::PDFAppSearchGroup::Documents)->isChecked());
}

void AppSearchTest::pageFilterRoute()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/home"));
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const int historyCount = fixture.homeTab()->getHistory().getCount();

    // A chip replaces the route of the current entry (the page stays)
    page->getChip(pdf::PDFAppSearchGroup::Help)->click();
    SearchFixture::settle();
    QCOMPARE(fixture.searchPage(), page);
    QCOMPARE(fixture.homeTab()->getHistory().getCount(), historyCount);
    QCOMPARE(fixture.homeTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/search?q=sign&group=help"));
    QCOMPARE(page->getRoute().toString(), QStringLiteral("/search?q=sign&group=help"));

    // The route round trip: Back and Forward show the filter again
    fixture.navigate(QStringLiteral("/about"));
    fixture.navigator()->goBack();
    SearchFixture::settle();
    page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(page->getGroup(), std::make_optional(pdf::PDFAppSearchGroup::Help));
    QVERIFY(page->getChip(pdf::PDFAppSearchGroup::Help)->isChecked());

    // All again
    page->getChip(std::nullopt)->click();
    SearchFixture::settle();
    QCOMPARE(fixture.homeTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/search?q=sign"));
    QVERIFY(!page->getGroup());

    // The same filter does nothing
    page->setGroupFilter(std::nullopt);
    QVERIFY(!page->getGroup());
}

void AppSearchTest::pageActivation_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("group");
    QTest::addColumn<QString>("key");

    QTest::newRow("tool") << "merge" << int(pdf::PDFAppSearchGroup::Tools) << "pages.merge";
    QTest::newRow("setting") << "color scheme" << int(pdf::PDFAppSearchGroup::Settings) << pdf::PDFSettingKeys::colorScheme();
    QTest::newRow("help") << "portable" << int(pdf::PDFAppSearchGroup::Help) << "/help/portable-mode";
    QTest::newRow("video") << pdf::PDFSupportVideoCatalog::getVideos().front().getTitle() << int(pdf::PDFAppSearchGroup::Help)
                           << QStringLiteral("video:%1").arg(pdf::PDFSupportVideoCatalog::getVideos().front().getId());
    QTest::newRow("command") << "new tab" << int(pdf::PDFAppSearchGroup::Commands) << pdf::PDFAppCommands::newTab();
    QTest::newRow("document") << "activation" << int(pdf::PDFAppSearchGroup::Documents) << "";
}

void AppSearchTest::pageActivation()
{
    QFETCH(QString, text);
    QFETCH(int, group);
    QFETCH(QString, key);

    SearchFixture fixture;
    const QString document = QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/activation.pdf"));
    fixture.addRecent({ document });
    if (key.isEmpty())
    {
        const std::vector<pdf::PDFRecentFile> files = fixture.context->getRecent().getFiles();
        QVERIFY(!files.empty());
        key = files.front().path;
    }

    fixture.navigate(pdf::PDFAppRoutes::search(text).toString());
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);

    pdf::PDFAppSearchResultRow* row = nullptr;
    for (pdf::PDFAppSearchResultRow* candidate : page->getRows())
    {
        if (int(candidate->getResult().group) == group && candidate->getResult().key == key)
        {
            row = candidate;
        }
    }
    QVERIFY(row);
    const pdf::PDFAppSearchResult result = row->getResult();

    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    const int tabCount = fixture.window->getTabs().size();
    QPointer<pdf::PDFAppSearchPage> guard(page);
    row->click();
    SearchFixture::settle();

    switch (result.action)
    {
        case pdf::PDFAppSearchResult::Action::Navigate:
            QVERIFY(!navigated.isEmpty());
            QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), result.route);
            QVERIFY(!guard || guard != fixture.currentPage());
            if (result.group == pdf::PDFAppSearchGroup::Help)
            {
                QCOMPARE(fixture.currentTab()->getKind(), pdf::PDFAppTabKind::Help);
            }
            else
            {
                // Tools and Settings have no pages in step 2: Page Not Found (expected)
                QVERIFY(navigated.last().at(2).toBool());
            }
            break;

        case pdf::PDFAppSearchResult::Action::OpenUrl:
            QCOMPARE(external.size(), 1);
            QCOMPARE(external.front().at(0).toUrl(), result.url);
            QCOMPARE(fixture.searchPage(), page);
            break;

        case pdf::PDFAppSearchResult::Action::RunCommand:
            QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);
            QCOMPARE(fixture.currentTab()->getKind(), pdf::PDFAppTabKind::NewTab);
            break;

        case pdf::PDFAppSearchResult::Action::OpenDocument:
            QVERIFY(fixture.window->getRequestedFiles().contains(result.filePath));
            break;
    }

    // A message box of the opening is closed by the fixture
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (QDialog* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible())
        {
            dialog->reject();
        }
    }
    for (QDialog* dialog : fixture.window->findChildren<QDialog*>())
    {
        dialog->reject();
    }
    SearchFixture::settle();
}

void AppSearchTest::pageKeyboard()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const QList<pdf::PDFAppSearchResultRow*> rows = page->getRows();
    QVERIFY(rows.size() >= 3);

    // The first row is current without the focus
    QCOMPARE(page->getCurrentRow(), rows[0]);

    rows[0]->setFocus(Qt::TabFocusReason);
    QTest::keyClick(rows[0], Qt::Key_Down);
    QVERIFY(rows[1]->hasFocus());
    QCOMPARE(page->getCurrentRow(), rows[1]);
    QVERIFY(rows[1]->isCurrent());
    QVERIFY(!rows[0]->isCurrent());

    QTest::keyClick(rows[1], Qt::Key_Up);
    QVERIFY(rows[0]->hasFocus());
    QTest::keyClick(rows[0], Qt::Key_Up);
    QVERIFY(rows[0]->hasFocus());
    QTest::keyClick(rows[0], Qt::Key_End);
    QVERIFY(rows.last()->hasFocus());
    QTest::keyClick(rows.last(), Qt::Key_Down);
    QVERIFY(rows.last()->hasFocus());
    QTest::keyClick(rows.last(), Qt::Key_Home);
    QVERIFY(rows[0]->hasFocus());

    // Accessible name and description of a row
    QCOMPARE(rows[0]->getAccessibleText(), rows[0]->getResult().title);
    QVERIFY(rows[0]->getAccessibleDescriptionText().contains(rows[0]->getResult().subtitle));

    // Enter opens the result (Page Not Found of the tool)
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QTest::keyClick(rows[0], Qt::Key_Return);
    SearchFixture::settle();
    QVERIFY(!navigated.isEmpty());

    // Esc clears the search field and focuses it
    fixture.navigate(QStringLiteral("/search?q=sign"));
    page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(fixture.field()->text(), QStringLiteral("sign"));
    page->getRows().front()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(page->getRows().front(), Qt::Key_Escape);
    QVERIFY(fixture.field()->text().isEmpty());
    QVERIFY(fixture.field()->hasFocus());

    // Other keys go to the row (Space activates it): a letter changes nothing
    pdf::PDFAppSearchResultRow* first = page->getRows().front();
    first->setFocus(Qt::TabFocusReason);
    navigated.clear();
    QTest::keyClick(first, Qt::Key_A);
    SearchFixture::settle();
    QVERIFY(navigated.isEmpty());
    QCOMPARE(fixture.searchPage(), page);
    QVERIFY(first->hasFocus());
    QCOMPARE(page->getCurrentRow(), first);
}

void AppSearchTest::pageNotifications()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=quarterly"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(page->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 0);

    // An own write is notified at once, the page reads the database again
    const QString path = QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Quarterly.pdf"));
    fixture.addRecent({ path });
    QTRY_COMPARE(page->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 1);
    QVERIFY(!page->getRows().isEmpty());

    // The focused row stays focused after a change
    page->getRows().front()->setFocus(Qt::TabFocusReason);
    const QString focusedKey = page->getRows().front()->getResult().key;
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    SearchFixture::settle();
    QVERIFY(page->getRows().front()->hasFocus());
    QCOMPARE(page->getRows().front()->getResult().key, focusedKey);

    // Removed: the result is gone, the focus goes to the first row
    const std::vector<pdf::PDFRecentFile> files = fixture.context->getRecent().getFiles();
    QVERIFY(!fixture.context->getRecent().removeFile(files.front().path).isError());
    QTRY_COMPARE(page->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 0);

    // A change of another table is ignored, a shortcut is read again
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::newTab(), QStringLiteral("Ctrl+Shift+Y")).isError());
    fixture.navigate(QStringLiteral("/search?q=new%20tab"));
    page = fixture.searchPage();
    QVERIFY(page);
    const pdf::PDFAppSearchResult* command = page->getResults().findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab());
    QVERIFY(command);
    QCOMPARE(command->meta, QKeySequence(QStringLiteral("Ctrl+Shift+Y")).toString(QKeySequence::NativeText));
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::newTab(), QStringLiteral("Ctrl+Shift+U")).isError());
    QTRY_COMPARE(page->getResults().findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab())->meta, QKeySequence(QStringLiteral("Ctrl+Shift+U")).toString(QKeySequence::NativeText));
}

void AppSearchTest::pageNotificationOfAnotherConnection()
{
    // Another instance (a second database on the same file) records a file: the
    // page shows it within the poll interval
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    SearchFixture fixture(databasePath);
    QCOMPARE(fixture.context->getStorageState().mode, pdf::PDFDatabaseStorageMode::Disk);
    fixture.navigate(QStringLiteral("/search?q=another"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);

    pdf::PDFDatabase writer;
    pdf::PDFDatabaseOptions options;
    options.filePath = databasePath;
    writer.open(options);
    QCOMPARE(writer.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    pdf::PDFRecentRepository recent(&writer, nullptr);
    QVERIFY(!recent.recordOpened(directory.filePath(QStringLiteral("Another instance.pdf"))).isError());

    QTRY_COMPARE_WITH_TIMEOUT(page->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 1, 5000 * SearchTestHelper::getTimeoutFactor());

    // The open popup is updated the same way
    fixture.type(QStringLiteral("another"));
    QVERIFY(fixture.controller()->isPopupOpen());
    const int rows = fixture.popup()->getRowCount();
    QVERIFY(!recent.recordOpened(directory.filePath(QStringLiteral("Another one.pdf"))).isError());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.popup()->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 2, 5000 * SearchTestHelper::getTimeoutFactor());
    QVERIFY(fixture.popup()->getRowCount() >= rows);
    writer.close();
}

void AppSearchTest::pageGeometry()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    SearchFixture::settle();

    // search-grid: the main column and the side column 340, gap 24 (941:7514)
    QWidget* side = page->getSideColumn();
    QCOMPARE(side->width(), 340);
    pdf::PDFAppCard* card = page->getResultsCard();
    const QRect cardRect(card->mapTo(page, QPoint(0, 0)), card->size());
    QCOMPARE(side->mapTo(page, QPoint(0, 0)).x(), cardRect.right() + 1 + 24);

    // The chips are 32 high, the card 16 below them
    QPushButton* all = page->getChip(std::nullopt);
    QCOMPARE(all->height(), 32);
    QCOMPARE(cardRect.top(), all->mapTo(page, QPoint(0, 0)).y() + 32 + 16);

    // Rows: 44 high, 7 from the edges of the card (border 1 + row-wrap 6), the
    // first header 7 below the top of the card (border 1 + padding 6), 27 high
    const QList<pdf::PDFAppSearchGroupHeader*> headers = page->getGroupHeaders();
    QVERIFY(!headers.isEmpty());
    QCOMPARE(headers.front()->height(), 27);
    QCOMPARE(headers.front()->mapTo(card, QPoint(0, 0)).y(), 7);
    QCOMPARE(headers.front()->mapTo(card, QPoint(0, 0)).x(), 1);
    for (pdf::PDFAppSearchResultRow* row : page->getRows())
    {
        QCOMPARE(row->height(), 44);
        QCOMPARE(row->mapTo(card, QPoint(0, 0)).x(), 7);
        QCOMPARE(row->width(), card->width() - 14);
    }
    for (pdf::PDFAppSearchShowAllButton* button : page->getShowAllButtons())
    {
        QCOMPARE(button->height(), 31);
    }

    // The first row follows its header
    const pdf::PDFAppSearchResultRow* first = page->getRows().front();
    QCOMPARE(first->mapTo(card, QPoint(0, 0)).y(), 7 + 27);
}

void AppSearchTest::pagePixels()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    SearchFixture::settle();

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        SearchFixture::settle();
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            const QImage image = SearchTestHelper::render(page->getResultsCard(), ratio);
            const QColor surface = SearchTestHelper::token(pdf::PDFThemeColors::BgSurface);
            const QColor hover = SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), surface);

            // The current row bg/hover, the next row bg/surface (a point of the row padding)
            const QList<pdf::PDFAppSearchResultRow*> rows = page->getRows();
            const QPoint current = rows[0]->mapTo(page->getResultsCard(), QPoint(rows[0]->width() / 2, 3));
            const QPoint other = rows[1]->mapTo(page->getResultsCard(), QPoint(rows[1]->width() / 2, 3));
            QVERIFY2(SearchTestHelper::isClose(SearchTestHelper::pixel(image, current), hover), qPrintable(SearchTestHelper::pixel(image, current).name()));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, other), surface));

            // The badge of a tool: the background of its hue in the center of the circle edge
            const QPoint badge = rows[0]->mapTo(page->getResultsCard(), QPoint(10 + 3, 22));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, badge),
                                              SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFAccentHueColors::getColor(rows[0]->getResult().hue, pdf::PDFAccentHueColors::Role::Background)), hover), 12));

            QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
        }
        QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(fixture.window)), 0);
    }

    // The empty state in both themes
    fixture.navigate(QStringLiteral("/search?q=epub"));
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        SearchFixture::settle();
        const QImage image = SearchTestHelper::render(fixture.window);
        QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
        QWidget* circle = fixture.searchPage()->getEmptyState()->findChild<QWidget*>(QStringLiteral("searchEmptyIcon"));
        QVERIFY(circle);
        QCOMPARE(circle->size(), QSize(72, 72));
        const QImage circleImage = SearchTestHelper::render(circle);
        QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(circleImage, QPoint(36, 8)), SearchTestHelper::token(pdf::PDFThemeColors::AccentNeutralBg)));
    }
}

void AppSearchTest::pageRightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    SearchFixture::settle();

    // The side column is on the left, the chip All on the right
    QVERIFY(page->getSideColumn()->mapTo(page, QPoint(0, 0)).x() < page->getResultsCard()->mapTo(page, QPoint(0, 0)).x());
    QVERIFY(page->getChip(std::nullopt)->mapTo(page, QPoint(0, 0)).x() > page->getChip(pdf::PDFAppSearchGroup::Help)->mapTo(page, QPoint(0, 0)).x());

    // The badge of the row is at its right edge
    pdf::PDFAppSearchResultRow* row = page->getRows().front();
    const QImage image = SearchTestHelper::render(row);
    const QColor badge = SearchTestHelper::token(pdf::PDFAccentHueColors::getColor(row->getResult().hue, pdf::PDFAccentHueColors::Role::Background));
    const QColor hover = SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), SearchTestHelper::token(pdf::PDFThemeColors::BgSurface));
    QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, QPoint(row->width() - 10 - 3, 22)), SearchTestHelper::blend(badge, hover), 12));
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(fixture.window)), 0);
}

void AppSearchTest::pageMinimalWindow()
{
    SearchFixture fixture(QString(), QSize(720, 480));
    fixture.window->resize(720, 480);
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    SearchFixture::settle();

    // No horizontal scroll bar, the side column below the results, the rows inside of the viewport
    pdf::PDFAppScrollArea* area = fixture.homeTab()->getPageScrollArea();
    QVERIFY(!area->horizontalScrollBar()->isVisible() || area->horizontalScrollBar()->maximum() == 0);
    QVERIFY(page->getSideColumn()->mapTo(page, QPoint(0, 0)).y() > page->getResultsCard()->mapTo(page, QPoint(0, 0)).y());
    for (pdf::PDFAppSearchResultRow* row : page->getRows())
    {
        QVERIFY(row->mapTo(area->viewport(), QPoint(row->width(), 0)).x() <= area->viewport()->width());
    }

    // The chips wrap into the width (no chip outside of the page)
    QVERIFY(page->width() <= area->viewport()->width());
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(fixture.window)), 0);

    // The empty state fits too
    fixture.navigate(QStringLiteral("/search?q=epub"));
    QVERIFY(!area->horizontalScrollBar()->isVisible() || area->horizontalScrollBar()->maximum() == 0);
}

void AppSearchTest::pageDeletion()
{
    // A refresh pending at the deletion of the page
    {
        SearchFixture fixture;
        fixture.navigate(QStringLiteral("/search?q=sign"));
        QPointer<pdf::PDFAppSearchPage> page = fixture.searchPage();
        QVERIFY(page);
        Q_EMIT fixture.context->getNotifier()->everythingChanged();
        delete page.data();
        SearchFixture::settle();
        QVERIFY(!page);
    }

    // The window deleted with the page and a pending refresh
    {
        SearchFixture fixture;
        fixture.navigate(QStringLiteral("/search?q=sign"));
        Q_EMIT fixture.context->getNotifier()->everythingChanged();
        delete fixture.window.data();
        SearchFixture::settle();
    }

    // The page deleted by its own activation (a navigation replaces it)
    {
        SearchFixture fixture;
        fixture.navigate(QStringLiteral("/search?q=merge"));
        QPointer<pdf::PDFAppSearchPage> page = fixture.searchPage();
        QVERIFY(page);
        const pdf::PDFAppSearchResult result = page->getRows().front()->getResult();
        QVERIFY(!page->activateResult(result) || page);
        SearchFixture::settle();
        QVERIFY(!page);
    }

    // The page deleted from a slot of the navigation of its row
    {
        SearchFixture fixture;
        fixture.navigate(QStringLiteral("/search?q=merge"));
        QPointer<pdf::PDFAppSearchPage> page = fixture.searchPage();
        QObject::connect(page.data(), &pdf::PDFAppPage::navigationRequested, page.data(), &QObject::deleteLater);
        page->getRows().front()->click();
        SearchFixture::settle();
        QVERIFY(!page);
    }

    // The context closed under an open page (no database)
    {
        SearchFixture fixture;
        fixture.navigate(QStringLiteral("/search?q=sign"));
        pdf::PDFAppSearchPage* page = fixture.searchPage();
        fixture.context->close();
        page->refresh();
        QVERIFY(!page->getRows().isEmpty());
    }
}

void AppSearchTest::pageLanguageChange()
{
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign&group=tools"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const int rows = page->getRows().size();

    SearchTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    SearchFixture::settle();
    QCOMPARE(page->getRows().size(), rows);
    QVERIFY(page->getChip(pdf::PDFAppSearchGroup::Tools)->isChecked());
    QCoreApplication::removeTranslator(&translator);
    SearchFixture::settle();

    // The current time of the dates
    page->setCurrentTime(QDateTime(QDate(2030, 1, 1), QTime(0, 0)));
    QCOMPARE(page->getRows().size(), rows);
}

void AppSearchTest::pageBigRecentList()
{
    // 20 000 pinned files in the database: the page reads and searches them fast
    SearchFixture fixture;
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(connection);
    QVERIFY(!connection->execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 20000) "
                                 "INSERT INTO recent_file(path, name, folder, last_opened, open_count, pinned, pin_order) "
                                 "SELECT '/docs/folder' || (i % 50) || '/report_' || i || '.pdf', 'report_' || i || '.pdf', '/docs/folder' || (i % 50), 1700000000000 + i, 1, 1, i FROM n").isError());
    SearchFixture::settle();

    QElapsedTimer timer;
    timer.start();
    fixture.navigate(QStringLiteral("/search?q=report_19&group=documents"));
    const qint64 elapsed = timer.elapsed();
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    qInfo("Page of the results of 20000 recent files: %lld ms (%d matches)", elapsed, page->getResults().getCount(pdf::PDFAppSearchGroup::Documents));

    // The list of the files has the newest MAXIMUM_FILE_ROWS rows: the matches of
    // "report_19" among them (report_19, report_19x, report_19xx, report_19xxx)
    int expectedMatches = 0;
    for (int i = 20000 - pdf::PDFRecentRepository::MAXIMUM_FILE_ROWS + 1; i <= 20000; ++i)
    {
        expectedMatches += QString::number(i).startsWith(QLatin1String("19")) ? 1 : 0;
    }
    QVERIFY(expectedMatches >= 1000);
    QCOMPARE(page->getResults().getCount(pdf::PDFAppSearchGroup::Documents), expectedMatches);
    QCOMPARE(page->getRows().size(), pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS);
    QVERIFY2(elapsed < 5000 * SearchTestHelper::getTimeoutFactor(), qPrintable(QStringLiteral("%1 ms").arg(elapsed)));

    // One search of the popup (the data are read at its opening)
    timer.restart();
    fixture.type(QStringLiteral("report_1999"));
    const qint64 popupElapsed = timer.elapsed();
    qInfo("Popup of the results of 20000 recent files: %lld ms", popupElapsed);
    QVERIFY(fixture.controller()->isPopupOpen());
    QVERIFY2(popupElapsed < 3000 * SearchTestHelper::getTimeoutFactor(), qPrintable(QStringLiteral("%1 ms").arg(popupElapsed)));
}

// ----------------------------------------------------------------- popup

void AppSearchTest::popupOpenClose()
{
    SearchFixture fixture;
    QVERIFY(fixture.controller());
    QVERIFY(!fixture.popup());

    // Typing searches after the delay
    fixture.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    fixture.field()->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(fixture.field(), QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isSearchPending());
    QTRY_VERIFY(fixture.controller()->isPopupOpen());
    QVERIFY(!fixture.controller()->isSearchPending());

    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->getRowCount() > 0 && popup->getRowCount() <= pdf::PDFAppSearchPopup::MAXIMUM_ROWS);
    QCOMPARE(popup->getCurrentRow(), 0);
    QVERIFY(popup->getShadow()->isVisible());
    QVERIFY(fixture.field()->hasFocus());
    QVERIFY(!fixture.field()->accessibleDescription().isEmpty());

    // Each group with a result has a row; the headers count "4 of 6"
    const pdf::PDFAppSearchResults& results = popup->getResults();
    int groups = 0;
    for (const pdf::PDFAppSearchPopup::Item& item : popup->getItems())
    {
        if (item.kind == pdf::PDFAppSearchPopup::Item::Kind::GroupHeader)
        {
            ++groups;
            QVERIFY(item.count.endsWith(QString::number(results.getCount(item.group))));
        }
    }
    int nonEmptyGroups = 0;
    for (const pdf::PDFAppSearchGroup group : pdf::PDFAppSearchGroups::getGroups())
    {
        nonEmptyGroups += results.getCount(group) > 0 ? 1 : 0;
    }
    QCOMPARE(groups, nonEmptyGroups);
    QCOMPARE(popup->getShowAllText(), QStringLiteral("Show all %1 results").arg(results.getTotalCount()));

    // Esc closes the popup, a second Esc clears the field
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QVERIFY(!fixture.controller()->isPopupOpen());
    QVERIFY(!popup->getShadow()->isVisible());
    QCOMPARE(fixture.field()->text(), QStringLiteral("sign"));
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QVERIFY(fixture.field()->text().isEmpty());
    QTest::keyClick(fixture.field(), Qt::Key_Escape);

    // Down opens it again; deleting the text closes it
    QTest::keyClicks(fixture.field(), QStringLiteral("merge"));
    fixture.controller()->updateSearch();
    QVERIFY(fixture.controller()->isPopupOpen());
    fixture.field()->selectAll();
    QTest::keyClick(fixture.field(), Qt::Key_Backspace);
    QVERIFY(!fixture.controller()->isPopupOpen());
    QTest::keyClicks(fixture.field(), QStringLiteral("merge"));
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QVERIFY(!fixture.controller()->isSearchPending());
    QVERIFY(!fixture.controller()->isPopupOpen());
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QVERIFY(fixture.controller()->isPopupOpen());

    // No results: the message row, no link
    fixture.type(QStringLiteral("epub"));
    QVERIFY(fixture.controller()->isPopupOpen());
    QCOMPARE(fixture.popup()->getRowCount(), 0);
    QCOMPARE(fixture.popup()->getItems().size(), size_t(1));
    QCOMPARE(fixture.popup()->getItems().front().kind, pdf::PDFAppSearchPopup::Item::Kind::Message);
    QVERIFY(fixture.popup()->getShowAllText().isEmpty());
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(fixture.popup())), 0);

    // Up does nothing without a popup
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QTest::keyClick(fixture.field(), Qt::Key_Up);
    QVERIFY(!fixture.controller()->isPopupOpen());
}

void AppSearchTest::popupKeyboard()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());
    const int rows = popup->getRowCount();
    QVERIFY(rows >= 3);

    QSignalSpy current(popup, &pdf::PDFAppSearchPopup::currentRowChanged);
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QCOMPARE(popup->getCurrentRow(), 1);
    QTest::keyClick(fixture.field(), Qt::Key_Up);
    QTest::keyClick(fixture.field(), Qt::Key_Up);
    QCOMPARE(popup->getCurrentRow(), 0);
    QTest::keyClick(fixture.field(), Qt::Key_PageDown);
    QCOMPARE(popup->getCurrentRow(), rows - 1);
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QCOMPARE(popup->getCurrentRow(), rows - 1);
    QTest::keyClick(fixture.field(), Qt::Key_PageUp);
    QCOMPARE(popup->getCurrentRow(), 0);
    QVERIFY(current.size() >= 3);
    QVERIFY(fixture.field()->accessibleDescription().contains(popup->getCurrentResult()->title));

    // Enter opens the current result; the field is cleared
    const pdf::PDFAppSearchResult result = *popup->getCurrentResult();
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());
    QVERIFY(fixture.field()->text().isEmpty());
    QVERIFY(!navigated.isEmpty());
    QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), result.route);

    // Enter right after typing searches first (the results of the typed text)
    fixture.window->activateTab(fixture.homeTab());
    fixture.field()->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(fixture.field(), QStringLiteral("merge"));
    QVERIFY(fixture.controller()->isSearchPending());
    navigated.clear();
    QTest::keyClick(fixture.field(), Qt::Key_Enter);
    SearchFixture::settle();
    QVERIFY(!navigated.isEmpty());
    QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::tool(QStringLiteral("pages.merge")));

    // Tab closes the popup and moves the focus on
    fixture.window->activateTab(fixture.homeTab());
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    QTest::keyClick(fixture.field(), Qt::Key_Tab);
    QVERIFY(!fixture.controller()->isPopupOpen());

    // A modifier is not a key of the popup
    fixture.type(QStringLiteral("sign"));
    QTest::keyClick(fixture.field(), Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(fixture.popup()->getCurrentRow(), 0);

    // Esc is not a shortcut of the window, while the field uses it
    QKeyEvent shortcutOverride(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.field(), &shortcutOverride);
    QVERIFY(shortcutOverride.isAccepted());
}

void AppSearchTest::popupEnterWithoutPopup()
{
    // Enter without a popup (or without a result) asks for the page of all results
    SearchFixture fixture;
    fixture.type(QStringLiteral("epub"));
    QVERIFY(fixture.field()->hasFocus());
    QVERIFY(fixture.controller()->isPopupOpen());
    QVERIFY(!fixture.popup()->getCurrentResult());
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());
    QVERIFY(fixture.searchPage());
    QCOMPARE(fixture.searchPage()->getSearchText(), QStringLiteral("epub"));
    QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::search(QStringLiteral("epub")));

    // Enter with the popup closed by Esc (no search pending): the page of all results
    fixture.window->activateTab(fixture.homeTab());
    fixture.type(QStringLiteral("merge"));
    QVERIFY(fixture.controller()->isPopupOpen());
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QVERIFY(!fixture.controller()->isPopupOpen());
    QVERIFY(!fixture.controller()->isSearchPending());
    navigated.clear();
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());
    QVERIFY(!navigated.isEmpty());
    QCOMPARE(navigated.last().at(1).value<pdf::PDFAppRoute>(), pdf::PDFAppRoutes::search(QStringLiteral("merge")));
    QVERIFY(fixture.searchPage());
    QCOMPARE(fixture.searchPage()->getSearchText(), QStringLiteral("merge"));

    // Show all n results of the footer
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    const QRect link = fixture.popup()->getShowAllRect();
    QVERIFY(!link.isEmpty());
    QTest::mouseMove(fixture.popup(), link.center());
    QTest::mouseClick(fixture.popup(), Qt::LeftButton, Qt::NoModifier, link.center());
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());
    QCOMPARE(fixture.searchPage()->getSearchText(), QStringLiteral("sign"));

    // A press on the link released elsewhere does nothing
    fixture.type(QStringLiteral("merge"));
    navigated.clear();
    QTest::mousePress(fixture.popup(), Qt::LeftButton, Qt::NoModifier, fixture.popup()->getShowAllRect().center());
    QTest::mouseRelease(fixture.popup(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    SearchFixture::settle();
    QVERIFY(navigated.isEmpty());
    QTest::mouseClick(fixture.popup(), Qt::RightButton, Qt::NoModifier, fixture.popup()->getRowRect(0).center());
    SearchFixture::settle();
    QVERIFY(navigated.isEmpty());
}

void AppSearchTest::popupMouse()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());

    // The pointer moves the current row
    QTest::mouseMove(popup, popup->getRowRect(2).center());
    QCOMPARE(popup->getCurrentRow(), 2);
    QTest::mouseMove(popup, QPoint(2, 2));
    QCOMPARE(popup->getCurrentRow(), 2);

    // A press released on another row does not activate
    QSignalSpy activated(popup, &pdf::PDFAppSearchPopup::resultActivated);
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, popup->getRowRect(1).center());
    QCOMPARE(popup->getCurrentRow(), 1);
    QTest::mouseRelease(popup, Qt::LeftButton, Qt::NoModifier, popup->getRowRect(2).center());
    QCOMPARE(activated.size(), 0);
    QVERIFY(popup->isOpen());
    QVERIFY(fixture.field()->hasFocus());

    // A click activates the row
    const pdf::PDFAppSearchResult result = *popup->getRowResult(1);
    QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, popup->getRowRect(1).center());
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.front().at(0).value<pdf::PDFAppSearchResult>().key, result.key);
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());

    // A click outside of the popup and of the field closes it (the click goes on)
    fixture.window->activateTab(fixture.homeTab());
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    pdf::PDFAppFooter* footer = fixture.homeTab()->getFooter();
    QTest::mouseClick(footer, Qt::LeftButton, Qt::NoModifier, QPoint(footer->width() / 2, footer->height() / 2));
    QVERIFY(!fixture.controller()->isPopupOpen());

    // A click into the field keeps it; the shadow lets the pointer through
    fixture.type(QStringLiteral("sign"));
    QTest::mouseClick(fixture.field(), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(fixture.controller()->isPopupOpen());
    QVERIFY(fixture.popup()->getShadow()->testAttribute(Qt::WA_TransparentForMouseEvents));
    const QPoint shadowPoint = fixture.popup()->geometry().bottomLeft() + QPoint(4, 10);
    QVERIFY(fixture.window->childAt(shadowPoint) != fixture.popup()->getShadow());
}

void AppSearchTest::popupFocusAndWindow()
{
    SearchFixture fixture;

    // The focus leaves the field
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    fixture.homeTab()->getSidebar()->getThemeSwitch()->setFocus(Qt::OtherFocusReason);
    QVERIFY(!fixture.controller()->isPopupOpen());

    // The window is deactivated
    fixture.type(QStringLiteral("sign"));
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(fixture.window, &deactivate);
    QVERIFY(!fixture.controller()->isPopupOpen());

    // The window is resized: the popup follows the field
    fixture.type(QStringLiteral("sign"));
    fixture.window->resize(1200, 900);
    SearchFixture::settle();
    QVERIFY(fixture.controller()->isPopupOpen());
    const QRect fieldRect(fixture.field()->mapTo(fixture.window, QPoint(0, 0)), fixture.field()->size());
    QCOMPARE(fixture.popup()->geometry().right(), fieldRect.right());

    // The field is hidden (the header of a page without the search)
    fixture.homeTab()->getHeader()->hide();
    QVERIFY(!fixture.controller()->isPopupOpen());
    fixture.homeTab()->getHeader()->show();

    // The field has no focus: no popup
    fixture.field()->clearFocus();
    fixture.field()->setText(QStringLiteral("sign"));
    fixture.controller()->updateSearch();
    QVERIFY(!fixture.controller()->isPopupOpen());

    // A change of the language searches again
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    QEvent language(QEvent::LanguageChange);
    QCoreApplication::sendEvent(fixture.field(), &language);
    QVERIFY(fixture.controller()->isPopupOpen());

    // Ctrl+K of the window focuses the field and selects the text
    fixture.controller()->closePopup();
    fixture.homeTab()->getSidebar()->getThemeSwitch()->setFocus(Qt::OtherFocusReason);
    fixture.window->focusSearch();
    QVERIFY(fixture.field()->hasFocus());
    QCOMPARE(fixture.field()->selectedText(), fixture.field()->text());

    // The window is hidden
    fixture.type(QStringLiteral("sign"));
    QVERIFY(fixture.controller()->isPopupOpen());
    fixture.window->hide();
    QVERIFY(!fixture.controller()->isPopupOpen());
}

void AppSearchTest::popupNotification()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("budget"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->isOpen());
    QCOMPARE(popup->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 0);

    // A file recorded under the open popup appears at once (own write)
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Budget 2026.pdf")) });
    QTRY_COMPARE(popup->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 1);

    // The current result stays, when it is still there
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Budget 2027.pdf")) });
    QTRY_COMPARE(popup->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 2);
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    const QString key = popup->getCurrentResult()->key;
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    SearchFixture::settle();
    QCOMPARE(popup->getCurrentResult()->key, key);

    // A notification of another table is ignored; a closed popup reads again at the opening
    Q_EMIT fixture.context->getNotifier()->changed(QStringLiteral("tool_usage"), QStringLiteral("pages.merge"));
    fixture.controller()->closePopup();
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    QVERIFY(!fixture.controller()->isPopupOpen());
}

void AppSearchTest::popupDeletion_data()
{
    QTest::addColumn<int>("scenario");

    QTest::newRow("window") << 0;
    QTest::newRow("tab content") << 1;
    QTest::newRow("popup") << 2;
    QTest::newRow("field") << 3;
    QTest::newRow("from activation") << 4;
    QTest::newRow("context closed") << 5;
    QTest::newRow("from show all") << 6;
    QTest::newRow("controller from cleared field") << 7;
}

void AppSearchTest::popupDeletion()
{
    QFETCH(int, scenario);

    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    QPointer<pdf::PDFAppSearchPopup> popup = fixture.popup();
    QPointer<pdf::PDFAppSearchController> controller = fixture.controller();
    QVERIFY(popup && popup->isOpen());

    switch (scenario)
    {
        case 0:
            delete fixture.window.data();
            break;

        case 1:
            delete fixture.homeTab()->getHeader();
            break;

        case 2:
            delete popup.data();
            QVERIFY(controller);
            QVERIFY(!controller->isPopupOpen());
            fixture.type(QStringLiteral("merge"));
            QVERIFY(controller->isPopupOpen());
            break;

        case 3:
            delete fixture.field();
            break;

        case 4:
        {
            // The window is deleted by the navigation of the result
            QObject::connect(fixture.navigator(), &pdf::PDFAppNavigator::navigated, fixture.window.data(), &QObject::deleteLater);
            QTest::keyClick(fixture.field(), Qt::Key_Return);
            break;
        }

        case 5:
            fixture.context->close();
            fixture.type(QStringLiteral("merge"));
            QVERIFY(controller->isPopupOpen());
            break;

        case 6:
            QObject::connect(fixture.navigator(), &pdf::PDFAppNavigator::navigated, fixture.window.data(), &QObject::deleteLater);
            QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, popup->getShowAllRect().center());
            break;

        case 7:
        {
            // A slot of the application deletes the controller, when the activated
            // search clears the field: the activation stops (no navigation)
            const QString routeBefore = fixture.currentTab()->getHistory().getCurrentEntry()->route.toString();
            QObject::connect(fixture.field(), &QLineEdit::textChanged, controller.data(), [controller]() { delete controller.data(); });
            popup->setCurrentRow(0);
            QVERIFY(popup->getCurrentResult());
            popup->activateCurrent();
            QVERIFY(!controller);
            SearchFixture::settle();
            QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), routeBefore);
            break;
        }
    }

    SearchFixture::settle();
    if (scenario == 0 || scenario == 1 || scenario == 3 || scenario == 4 || scenario == 6)
    {
        QVERIFY(!popup);
    }

    // A press anywhere does not reach a deleted popup or controller
    QWidget other;
    other.show();
    QTest::mouseClick(&other, Qt::LeftButton);
    SearchFixture::settle();
}

void AppSearchTest::popupGeometry()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->isOpen());

    // 560 wide, the right edge on the right edge of the field, 6 below it (285:3026)
    const QRect fieldRect(fixture.field()->mapTo(fixture.window, QPoint(0, 0)), fixture.field()->size());
    QCOMPARE(popup->width(), 560);
    QCOMPARE(popup->geometry().right(), fieldRect.right());
    QCOMPARE(popup->geometry().top(), fieldRect.bottom() + 1 + 6);

    // The first header 7 below the top (border 1 + padding 6), rows 44 inside 7 of the edges, the footer 34
    const std::vector<pdf::PDFAppSearchPopup::Item>& items = popup->getItems();
    QCOMPARE(items.front().kind, pdf::PDFAppSearchPopup::Item::Kind::GroupHeader);
    QCOMPARE(items.front().rect.top(), 7);
    QCOMPARE(items.front().rect.height(), 27);
    QCOMPARE(popup->getRowRect(0).left(), 7);
    QCOMPARE(popup->getRowRect(0).width(), 546);
    QCOMPARE(popup->getRowRect(0).height(), 44);
    QCOMPARE(popup->getRowRect(0).top(), 34);
    QCOMPARE(popup->height(), items.back().rect.bottom() + 1 + 34 + 1);
    QCOMPARE(popup->getShowAllRect().right() + 1, popup->width() - 1 - 16);
    QCOMPARE(popup->getVisibleHintCount(), 3);

    // The shadow around the panel (0 / 8 / 24)
    const QRect shadow = popup->getShadow()->geometry();
    QCOMPARE(shadow, popup->geometry().marginsAdded(pdf::PDFAppSearchPopupShadow::getMargins()).intersected(fixture.window->rect()));

    // The popup lies above the content of the window
    QCOMPARE(fixture.window->childAt(popup->geometry().center()), static_cast<QWidget*>(popup));

    // Ten rows at most, with as many results as the design (4 of 6, 1 of 3, …)
    QVERIFY(popup->getRowCount() <= pdf::PDFAppSearchPopup::MAXIMUM_ROWS);
    QCOMPARE(popup->sizeHint(), popup->size());
}

void AppSearchTest::popupPixels()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->isOpen());

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        SearchFixture::settle();
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            const QImage image = SearchTestHelper::render(popup, ratio);
            const QColor surface = SearchTestHelper::token(pdf::PDFThemeColors::BgSurface);
            const QColor hover = SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), surface);

            // The panel bg/surface, the current row bg/hover, the next row bg/surface, the footer bg/subtle, the border
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, QPoint(popup->width() / 2, 3)), surface));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, popup->getRowRect(0).topLeft() + QPoint(popup->getRowRect(0).width() / 2, 3)), hover));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, popup->getRowRect(1).topLeft() + QPoint(popup->getRowRect(1).width() / 2, 3)), surface));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, QPoint(popup->width() / 2, popup->height() - 3)),
                                              SearchTestHelper::token(pdf::PDFThemeColors::BgSubtle)));
            QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, QPoint(popup->width() / 2, 0)), SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BorderDefault), surface), 8));
            QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
        }

        // The shadow is darker than the canvas below the panel (light theme)
        QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(fixture.window)), 0);
    }

    // A pressed row is bg/pressed
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, popup->getRowRect(1).center());
    QCursor::setPos(popup->mapToGlobal(popup->getRowRect(1).center()));
    QTest::mouseMove(popup, popup->getRowRect(1).center());
    const QPoint rowProbe = popup->getRowRect(1).topLeft() + QPoint(popup->getRowRect(1).width() / 2, 3);
    const QColor surface = SearchTestHelper::token(pdf::PDFThemeColors::BgSurface);
    const QImage pressed = SearchTestHelper::render(popup);
    QVERIFY2(SearchTestHelper::isClose(SearchTestHelper::pixel(pressed, rowProbe), SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgPressed), surface)),
             qPrintable(SearchTestHelper::pixel(pressed, rowProbe).name()));

    // The pointer moved out of the pressed row: the row is current (bg/hover), not pressed
    QCursor::setPos(popup->mapToGlobal(QPoint(2, 2)));
    const QImage outside = SearchTestHelper::render(popup);
    QVERIFY2(SearchTestHelper::isClose(SearchTestHelper::pixel(outside, rowProbe), SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), surface)),
             qPrintable(SearchTestHelper::pixel(outside, rowProbe).name()));
    QTest::mouseRelease(popup, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
}

void AppSearchTest::popupRightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->isOpen());

    // The left edge of the panel on the left edge of the field (the trailing edge)
    const QRect fieldRect(fixture.field()->mapTo(fixture.window, QPoint(0, 0)), fixture.field()->size());
    QCOMPARE(popup->geometry().left(), fieldRect.left());
    QCOMPARE(popup->layoutDirection(), Qt::RightToLeft);

    // The link of the footer at the left, the badge of the row at the right
    QCOMPARE(popup->getShowAllRect().left(), 1 + 16);
    const QImage image = SearchTestHelper::render(popup);
    const QRect row = popup->getRowRect(0);
    const QColor badge = SearchTestHelper::token(pdf::PDFAccentHueColors::getColor(popup->getRowResult(0)->hue, pdf::PDFAccentHueColors::Role::Background));
    const QColor hover = SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), SearchTestHelper::token(pdf::PDFThemeColors::BgSurface));
    QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(image, QPoint(row.right() - 10 - 3, row.center().y())), SearchTestHelper::blend(badge, hover), 12));
    QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
}

void AppSearchTest::popupMinimalWindow()
{
    SearchFixture fixture(QString(), QSize(720, 480));
    fixture.window->resize(720, 480);
    SearchFixture::settle();
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->isOpen());

    // The panel stays inside of the window: fewer rows, narrower, the hints which fit
    QVERIFY(popup->geometry().left() >= pdf::PDFAppSearchPopup::WINDOW_MARGIN);
    QVERIFY(popup->geometry().bottom() <= fixture.window->height() - pdf::PDFAppSearchPopup::WINDOW_MARGIN);
    QVERIFY(popup->getRowCount() >= 1);
    QVERIFY(popup->getRowCount() < pdf::PDFAppSearchPopup::MAXIMUM_ROWS);

    // A narrow window (narrower than the main window can be): the panel narrower
    // than 560, inside of the window, the hints which do not fit are left out
    QWidget narrow;
    narrow.resize(300, 480);
    narrow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&narrow));
    pdf::PDFAppSearchPopup* narrowPopup = new pdf::PDFAppSearchPopup(&narrow);
    narrowPopup->setResults(popup->getResults());
    narrowPopup->placeBelow(QRect(200, 10, 90, 32));
    narrowPopup->open();
    QVERIFY(narrowPopup->isOpen());
    QCOMPARE(narrowPopup->width(), 300 - 2 * pdf::PDFAppSearchPopup::WINDOW_MARGIN);
    QCOMPARE(narrowPopup->geometry().left(), pdf::PDFAppSearchPopup::WINDOW_MARGIN);
    QVERIFY(narrowPopup->geometry().bottom() < 480 - pdf::PDFAppSearchPopup::WINDOW_MARGIN);
    QVERIFY(narrowPopup->getVisibleHintCount() < popup->getVisibleHintCount());
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(narrowPopup)), 0);
}

void AppSearchTest::popupCommand()
{
    // A command of the popup runs the action of the window
    SearchFixture fixture;
    const int tabs = fixture.window->getTabs().size();
    fixture.type(QStringLiteral("new tab"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    int row = -1;
    for (int i = 0; i < popup->getRowCount(); ++i)
    {
        if (popup->getRowResult(i)->key == pdf::PDFAppCommands::newTab())
        {
            row = i;
        }
    }
    QVERIFY(row >= 0);
    popup->setCurrentRow(row);
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QCOMPARE(fixture.window->getTabs().size(), tabs + 1);

    // Close tab from the search of the New tab: the tab with its field and its
    // controller is deleted by the command of its own popup
    QPointer<pdf::PDFAppTab> newTab = fixture.currentTab();
    QVERIFY(newTab);
    QCOMPARE(newTab->getKind(), pdf::PDFAppTabKind::NewTab);
    QPointer<pdf::PDFLineEdit> newField = newTab->getSearchField();
    QVERIFY(newField);
    QPointer<pdf::PDFAppSearchController> newController = pdf::PDFAppSearchController::find(newField);
    QVERIFY(newController);
    QVERIFY(newController != fixture.controller());
    newField->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(newField, QStringLiteral("close tab"));
    newController->updateSearch();
    QVERIFY(newController->isPopupOpen());
    pdf::PDFAppSearchPopup* newPopup = newController->getPopup();
    int closeRow = -1;
    for (int i = 0; i < newPopup->getRowCount(); ++i)
    {
        if (newPopup->getRowResult(i)->key == pdf::PDFAppCommands::closeTab())
        {
            closeRow = i;
        }
    }
    QVERIFY(closeRow >= 0);
    newPopup->setCurrentRow(closeRow);
    QTest::keyClick(newField, Qt::Key_Return);
    SearchFixture::settle();
    QVERIFY(!newTab);
    QVERIFY(!newField);
    QVERIFY(!newController);
    QCOMPARE(fixture.window->getTabs().size(), tabs);
}

void AppSearchTest::popupDocumentAndVideo()
{
    SearchFixture fixture;
    const QString path = QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Invoice.pdf"));
    fixture.addRecent({ path });

    // A document is opened by the window (step 3 hook: the requested files)
    fixture.type(QStringLiteral("invoice"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup->getResults().getCount(pdf::PDFAppSearchGroup::Documents) == 1);
    for (int i = 0; i < popup->getRowCount(); ++i)
    {
        if (popup->getRowResult(i)->group == pdf::PDFAppSearchGroup::Documents)
        {
            popup->setCurrentRow(i);
        }
    }
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 1);
    for (QDialog* dialog : fixture.window->findChildren<QDialog*>())
    {
        dialog->reject();
    }
    SearchFixture::settle();

    // A video is an external address of the navigator
    QSignalSpy external(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    fixture.type(pdf::PDFSupportVideoCatalog::getVideos().front().getTitle());
    popup = fixture.popup();
    for (int i = 0; i < popup->getRowCount(); ++i)
    {
        if (popup->getRowResult(i)->action == pdf::PDFAppSearchResult::Action::OpenUrl)
        {
            popup->setCurrentRow(i);
            break;
        }
    }
    QCOMPARE(popup->getCurrentResult()->action, pdf::PDFAppSearchResult::Action::OpenUrl);
    QTest::keyClick(fixture.field(), Qt::Key_Return);
    SearchFixture::settle();
    QCOMPARE(external.size(), 1);
    QCOMPARE(external.front().at(0).toUrl().scheme(), QStringLiteral("https"));
}

void AppSearchTest::engineEdgeCases()
{
    // A manual of the test: more matching headings than shown, a heading equal to the title
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile topicFile(directory.filePath(QStringLiteral("alpha.md")));
    QVERIFY(topicFile.open(QIODevice::WriteOnly));
    topicFile.write("---\ntitle: Alpha topic\ncategory: settings\norder: 1\nsummary: Summary of the topic.\nkeywords: zeta\n---\n"
                    "Body text with a rare word quokka.\n\n## Gamma one\n\nText.\n\n## Gamma two\n\nText.\n\n## Gamma three\n\nText.\n\n## Alpha topic\n\nText.\n");
    topicFile.close();
    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load(directory.path());
    QCOMPARE(int(catalog.getTopics().size()), 1);

    // Registries of the test: a command without an icon (600 of them), a setting without a title
    pdf::PDFAppCommandRegistry commands;
    for (int i = 0; i < 600; ++i)
    {
        pdf::PDFAppCommand command;
        command.id = QStringLiteral("test.command%1").arg(i);
        command.title = "Repeated command";
        command.group = "Test";
        QVERIFY(commands.registerCommand(command));
    }
    pdf::PDFSettingRegistry settings;
    QVERIFY(settings.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("test.flag"), true).setSection(QStringLiteral("security")).setKeywords({ QStringLiteral("flagword") })));
    QVERIFY(settings.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("test.hidden"), true).setKeywords({ QStringLiteral("flagword") })));

    pdf::PDFAppSearchData data;
    data.commandRegistry = &commands;
    data.settingRegistry = &settings;
    data.helpCatalog = std::make_shared<const pdf::PDFAppHelpCatalog>(catalog);
    data.includeVideos = false;

    // A file without its name and folder (rows of another version)
    pdf::PDFRecentFile file;
    file.path = QStringLiteral("/data/folder/Unnamed report.pdf");
    data.recentFiles.push_back(file);

    const pdf::PDFAppSearchEngine engine;
    const pdf::PDFAppSearchResults gamma = engine.search(QStringLiteral("gamma"), data);
    // The topic (its text) and at most two of its three headings
    QCOMPARE(gamma.getCount(pdf::PDFAppSearchGroup::Help), 3);
    int headings = 0;
    for (const pdf::PDFAppSearchResult& result : gamma.getGroup(pdf::PDFAppSearchGroup::Help)->results)
    {
        if (!result.route.getFragment().isEmpty())
        {
            ++headings;
            QVERIFY(result.subtitle.contains(QStringLiteral("Alpha topic")));
        }
    }
    QCOMPARE(headings, 2);
    QCOMPARE(engine.search(QStringLiteral("quokka"), data).getCount(pdf::PDFAppSearchGroup::Help), 1);
    QCOMPARE(engine.search(QStringLiteral("alpha topic"), data).getCount(pdf::PDFAppSearchGroup::Help), 1);

    const pdf::PDFAppSearchResults repeated = engine.search(QStringLiteral("repeated"), data);
    QCOMPARE(repeated.getCount(pdf::PDFAppSearchGroup::Commands), 600);
    QCOMPARE(int(repeated.getGroup(pdf::PDFAppSearchGroup::Commands)->results.size()), pdf::PDFAppSearchEngine::MAXIMUM_GROUP_RESULTS);
    QCOMPARE(repeated.getGroup(pdf::PDFAppSearchGroup::Commands)->results.front().iconName, QStringLiteral("zap"));
    QVERIFY(repeated.getGroup(pdf::PDFAppSearchGroup::Commands)->results.front().meta.isEmpty());

    const pdf::PDFAppSearchResults flag = engine.search(QStringLiteral("flagword"), data);
    QCOMPARE(flag.getCount(pdf::PDFAppSearchGroup::Settings), 1);
    QCOMPARE(flag.getGroup(pdf::PDFAppSearchGroup::Settings)->results.front().title, QStringLiteral("test.flag"));
    QCOMPARE(flag.getGroup(pdf::PDFAppSearchGroup::Settings)->results.front().subtitle, QStringLiteral("Settings › Security"));

    const pdf::PDFAppSearchResults unnamed = engine.search(QStringLiteral("unnamed folder"), data);
    QCOMPARE(unnamed.getCount(pdf::PDFAppSearchGroup::Documents), 1);
    QCOMPARE(unnamed.getGroup(pdf::PDFAppSearchGroup::Documents)->results.front().title, QStringLiteral("Unnamed report.pdf"));
    QCOMPARE(unnamed.getGroup(pdf::PDFAppSearchGroup::Documents)->results.front().subtitle, QDir::toNativeSeparators(QStringLiteral("/data/folder")));

    // A translated tag of a video is searched together with the English one
    const pdf::PDFSupportVideo& video = pdf::PDFSupportVideoCatalog::getVideos().front();
    QVERIFY(!video.getSourceTags().isEmpty());
    SearchTestTranslator translator;
    translator.entries[qMakePair(QByteArray(pdf::PDFSupportVideoCatalog::TRANSLATION_CONTEXT), video.getSourceTags().front().toUtf8())] = QStringLiteral("Etikettwort");
    QCoreApplication::installTranslator(&translator);
    data.includeVideos = true;
    QVERIFY(engine.search(QStringLiteral("etikettwort"), data).findResult(pdf::PDFAppSearchGroup::Help, QStringLiteral("video:%1").arg(video.getId())));
    QVERIFY(engine.search(video.getSourceTags().front(), data).findResult(pdf::PDFAppSearchGroup::Help, QStringLiteral("video:%1").arg(video.getId())));
    QCoreApplication::removeTranslator(&translator);

    // The core query of a long word list and of a field without scores
    const pdf::PDFSearchQuery query(QStringLiteral("a b c d e f g h i j k l m n o p q r s"));
    QCOMPARE(query.getWords().size(), pdf::PDFSearchQuery::MAXIMUM_WORDS);
    QVERIFY(!query.getNormalizedText().isEmpty());
    QVERIFY(!pdf::PDFSearchQuery(QStringLiteral("zeta")).match({ pdf::PDFSearchField({ QStringLiteral("zeta") }, 0, 0, 0) }));
}

void AppSearchTest::pageDirect()
{
    // A page without a context and without a tab (the defaults of the shortcut, no tab to update)
    pdf::PDFAppSearchPage page(nullptr);
    QVERIFY(!page.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/search?q=sign&group=everything"))));
    QVERIFY(page.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/search?q=merge"))));
    QVERIFY(page.getResults().getCount(pdf::PDFAppSearchGroup::Tools) > 0);
    QCOMPARE(page.getResults().getCount(pdf::PDFAppSearchGroup::Commands), 0);
    page.setGroupFilter(pdf::PDFAppSearchGroup::Tools);
    QCOMPARE(page.getRoute().toString(), QStringLiteral("/search?q=merge&group=tools"));
    page.show();
    page.hide();

    // Activation without a window: a command, a document, an external address, an invalid route
    pdf::PDFAppSearchResult result;
    result.action = pdf::PDFAppSearchResult::Action::RunCommand;
    result.commandId = QStringLiteral("unknown.command");
    QVERIFY(page.activateResult(result));
    result.action = pdf::PDFAppSearchResult::Action::OpenDocument;
    result.filePath = QStringLiteral("/x.pdf");
    QVERIFY(page.activateResult(result));
    QSignalSpy external(&page, &pdf::PDFAppPage::externalUrlRequested);
    result.action = pdf::PDFAppSearchResult::Action::OpenUrl;
    result.url = QUrl(QStringLiteral("https://www.youtube.com/"));
    QVERIFY(page.activateResult(result));
    QCOMPARE(external.size(), 1);
    result.action = pdf::PDFAppSearchResult::Action::Navigate;
    result.route = pdf::PDFAppRoute();
    QVERIFY(page.activateResult(result));

    // Keys of a row: a modifier goes to the row, Esc without a tab does nothing
    QList<pdf::PDFAppSearchResultRow*> rows = page.getRows();
    QVERIFY(!rows.isEmpty());
    QTest::keyClick(rows.front(), Qt::Key_Down, Qt::ControlModifier);
    QTest::keyClick(rows.front(), Qt::Key_Escape);

    // A change of the language rebuilds the page
    QEvent language(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&page, &language);
    QVERIFY(!page.getRows().isEmpty());
    QVERIFY(page.getChip(pdf::PDFAppSearchGroup::Tools)->isChecked());

    // A factory creates the page
    std::unique_ptr<pdf::PDFAppPage> created(pdf::PDFAppSearchPageFactory().createPage(nullptr, nullptr));
    QVERIFY(qobject_cast<pdf::PDFAppSearchPage*>(created.get()));
}

void AppSearchTest::pageFocusAndCounts()
{
    SearchFixture fixture;
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Zyxwvu first.pdf")), QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Zyxwvu second.pdf")) });

    // One result: the singular texts
    fixture.navigate(QStringLiteral("/search?q=zyxwvu%20first"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("1 result for “zyxwvu first” in tools, commands, settings, documents and help."));
    fixture.navigate(QStringLiteral("/search?q=zyxwvu%20first&group=documents"));
    page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("1 result for “zyxwvu first” in documents."));

    // The focused row disappears: the focus goes to the first row
    fixture.navigate(QStringLiteral("/search?q=zyxwvu"));
    page = fixture.searchPage();
    QVERIFY(page);
    QCOMPARE(page->getRows().size(), 2);
    pdf::PDFAppSearchResultRow* focused = page->getRows().last();
    const QString removed = focused->getResult().key;
    focused->setFocus(Qt::TabFocusReason);
    QVERIFY(!fixture.context->getRecent().removeFile(removed).isError());
    QTRY_COMPARE(page->getRows().size(), 1);
    QVERIFY(page->getRows().front()->hasFocus());

    // Notifications are joined into one refresh, another table is ignored
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFRecentRepository::getFileTopic(), QString());
    Q_EMIT fixture.context->getNotifier()->changed(QStringLiteral("tool_usage"), QString());
    SearchFixture::settle();
    QCOMPARE(page->getRows().size(), 1);

    // The popup of a single result: "Show the result"
    fixture.type(QStringLiteral("zyxwvu"));
    QCOMPARE(fixture.popup()->getShowAllText(), QStringLiteral("Show the result"));
}

void AppSearchTest::popupEdgeCases()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    pdf::PDFAppSearchController* controller = fixture.controller();
    QVERIFY(popup->isOpen());

    // Rows out of the range, no current row and the arrows
    QVERIFY(!popup->getRowResult(-1));
    QVERIFY(!popup->getRowResult(1000));
    QVERIFY(popup->getRowRect(-1).isNull());
    popup->setCurrentRow(-1);
    popup->moveCurrentRow(-1);
    QCOMPARE(popup->getCurrentRow(), popup->getRowCount() - 1);
    popup->setCurrentRow(-1);
    popup->moveCurrentRow(1);
    QCOMPARE(popup->getCurrentRow(), 0);

    // The link hovered, pressed and left
    const QRect link = popup->getShowAllRect();
    QTest::mouseMove(popup, link.center());
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, link.center());
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(popup)), 0);
    QTest::mouseRelease(popup, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(popup, &leave);

    // Changes of the popup: direction, font, language, style
    for (const QEvent::Type type : { QEvent::LayoutDirectionChange, QEvent::FontChange, QEvent::LanguageChange, QEvent::StyleChange })
    {
        QEvent event(type);
        QCoreApplication::sendEvent(popup, &event);
    }
    QVERIFY(popup->isOpen());

    // A notification of the files and of the shortcuts searches again
    Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFRecentRepository::getFileTopic(), QString());
    Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFShortcutRepository::getTopic(), QString());
    QVERIFY(popup->isOpen());

    // A double click outside closes the popup; Backtab closes it
    QTest::mouseDClick(fixture.homeTab()->getFooter(), Qt::LeftButton);
    QVERIFY(!controller->isPopupOpen());
    fixture.type(QStringLiteral("sign"));
    QTest::keyClick(fixture.field(), Qt::Key_Backtab);
    QVERIFY(!controller->isPopupOpen());

    // A hide of the window (as the system does) closes it
    fixture.type(QStringLiteral("sign"));
    QEvent hide(QEvent::Hide);
    QCoreApplication::sendEvent(fixture.window, &hide);
    QVERIFY(!controller->isPopupOpen());

    // Show all of an empty field, a command, which the window does not have, an invalid route
    fixture.type(QStringLiteral("sign"));
    fixture.field()->setText(QString());
    QSignalSpy navigated(fixture.navigator(), &pdf::PDFAppNavigator::navigated);
    Q_EMIT popup->showAllRequested();
    QVERIFY(navigated.isEmpty());
    pdf::PDFAppSearchResult result;
    result.action = pdf::PDFAppSearchResult::Action::RunCommand;
    result.commandId = QStringLiteral("unknown.command");
    Q_EMIT popup->resultActivated(result);
    result.action = pdf::PDFAppSearchResult::Action::Navigate;
    result.route = pdf::PDFAppRoute();
    Q_EMIT popup->resultActivated(result);
    QVERIFY(navigated.isEmpty());

    // The field moved into another window: a new popup there
    QWidget other;
    other.resize(800, 600);
    other.show();
    pdf::PDFLineEdit* field = fixture.field();
    field->setParent(&other);
    field->move(400, 20);
    field->show();
    other.activateWindow();
    if (QTest::qWaitForWindowActive(&other))
    {
        field->setFocus(Qt::OtherFocusReason);
        field->setText(QStringLiteral("sign"));
        controller->updateSearch();
        QVERIFY(controller->isPopupOpen());
        QCOMPARE(controller->getPopup()->parentWidget(), &other);
    }
    delete field;

    // The shadow without a parent, without a panel
    pdf::PDFAppSearchPopupShadow shadow(nullptr);
    shadow.setPanelGeometry(QRect(0, 0, 100, 50));
    QCOMPARE(shadow.size(), QSize(100, 50).grownBy(pdf::PDFAppSearchPopupShadow::getMargins()));
    pdf::PDFAppSearchPopupShadow empty(nullptr);
    empty.resize(10, 10);
    QVERIFY(!SearchTestHelper::render(&empty).isNull());

    // Painting of the other states of a row
    QImage image(600, 50, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    pdf::PDFAppSearchResult row;
    row.title = QStringLiteral("Title");
    row.subtitle = QStringLiteral("Subtitle");
    pdf::PDFAppSearchPainter::paintResultRow(&painter, QRectF(0, 0, 600, 44), row, pdf::PDFComponentPainter::State(QStyle::State_Enabled | QStyle::State_MouseOver), false);
    pdf::PDFAppSearchPainter::paintResultRow(&painter, QRectF(0, 0, 600, 44), row, pdf::PDFComponentPainter::State(QStyle::State_None), true);
    pdf::PDFAppSearchPainter::paintShowAll(&painter, QRectF(0, 0, 600, 31), QStringLiteral("Show all (6)"), pdf::PDFComponentPainter::State(QStyle::State_Enabled | QStyle::State_MouseOver));
    painter.end();
}

void AppSearchTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the page, the empty state and the popup in
    // both themes at 1x and 2x (the comparison with 941:7081, 955:142130, 285:3026)
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    SearchFixture fixture(QString(), QSize(1440, 1240));
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Contract_signed.pdf")),
                        QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/NDA_for_signature.pdf")) });

    struct Shot
    {
        QString route;
        QString name;
        QString typed;
    };
    const Shot shots[] = { { QStringLiteral("/search?q=sign"), QStringLiteral("search-all"), QString() },
                           { QStringLiteral("/search?q=epub"), QStringLiteral("search-no-results"), QString() },
                           { QStringLiteral("/search?q=sign&group=tools"), QStringLiteral("search-tools"), QString() },
                           { QStringLiteral("/home"), QStringLiteral("search-popup"), QStringLiteral("sign") } };

    for (const Shot& shot : shots)
    {
        fixture.navigate(shot.route);
        if (!shot.typed.isEmpty())
        {
            fixture.type(shot.typed);
            QVERIFY(fixture.controller()->isPopupOpen());
        }

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            SearchFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = SearchTestHelper::render(fixture.window, ratio);
                QVERIFY(!image.isNull());
                QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(shot.name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(name)));
                }
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        if (fixture.controller())
        {
            fixture.controller()->closePopup();
        }
    }
}

void AppSearchTest::emptyStateFillsColumn()
{
    // pdf4qt-search-no-results 955:142130 (1440 x 961): the card of the results
    // reaches the footer (197 .. 890), the empty state is centered in it (the circle
    // at 394); the card of the results keeps the height of its rows
    SearchFixture fixture(QString(), QSize(1440, 961));
    fixture.navigate(QStringLiteral("/search?q=epub"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page && page->getEmptyState());
    SearchFixture::settle();
    QWidget* window = fixture.window;
    const QRect card(page->getResultsCard()->mapTo(window, QPoint(0, 0)), page->getResultsCard()->size());
    QCOMPARE(card.top(), 197);
    QCOMPARE(card.bottom() + 1, 890);
    QWidget* circle = page->getEmptyState()->findChild<QWidget*>(QStringLiteral("searchEmptyIcon"));
    QVERIFY(circle);
    QVERIFY2(qAbs(circle->mapTo(window, QPoint(0, 0)).y() - 394) <= 2, qPrintable(QString::number(circle->mapTo(window, QPoint(0, 0)).y())));

    // The empty query is centered as well; its chips have no counts (nothing was
    // searched), the chips of a search without results show 0
    QCOMPARE(page->getChip(std::nullopt)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 0);
    fixture.navigate(QStringLiteral("/search"));
    SearchFixture::settle();
    page = fixture.searchPage();
    QVERIFY(page && page->getEmptyState());
    QCOMPARE(page->getChip(std::nullopt)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), -1);
    for (const pdf::PDFAppSearchGroup group : pdf::PDFAppSearchGroups::getGroups())
    {
        QCOMPARE(page->getChip(group)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), -1);
    }
    QCOMPARE(page->getResultsCard()->mapTo(window, QPoint(0, page->getResultsCard()->height())).y(), 890);

    // A low window keeps the paddings of the state (the card is higher than the view)
    fixture.window->resize(1440, 600);
    fixture.navigate(QStringLiteral("/search?q=epub"));
    SearchFixture::settle();
    page = fixture.searchPage();
    QVERIFY(page && page->getEmptyState());
    circle = page->getEmptyState()->findChild<QWidget*>(QStringLiteral("searchEmptyIcon"));
    QVERIFY(circle);
    QVERIFY(circle->mapTo(page->getResultsCard(), QPoint(0, 0)).y() >= 72);
    const QWidget* hints = page->getEmptyState()->findChild<QWidget*>(QStringLiteral("searchEmptyHints"));
    QVERIFY(hints);
    QVERIFY(page->getResultsCard()->height() - hints->mapTo(page->getResultsCard(), QPoint(0, hints->height())).y() >= 72 + 40);

    // Results: the card ends below its rows
    fixture.window->resize(1440, 961);
    fixture.navigate(QStringLiteral("/search?q=sign"));
    SearchFixture::settle();
    page = fixture.searchPage();
    QVERIFY(page);
    QVERIFY(!page->getEmptyState());
    QVERIFY(!page->getRows().isEmpty());
    const QWidget* lastRow = page->getRows().back();
    QVERIFY(page->getResultsCard()->height() - lastRow->mapTo(page->getResultsCard(), QPoint(0, lastRow->height())).y() <= 12);
}

void AppSearchTest::headerClearButton()
{
    // Search field Filled=True (49:143, the search page 941:7123): the clear button
    // x-circle at the right of the typed text; a click clears the text
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFLineEdit* field = fixture.field();
    QVERIFY(field);
    QVERIFY(field->isClearButtonEnabled());
    auto findClearButton = [field]() -> QWidget*
    {
        for (QWidget* child : field->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            if (child->inherits("QLineEditIconButton") && child->isVisibleTo(field) && child->geometry().center().x() > field->width() / 2)
            {
                return child;
            }
        }
        return nullptr;
    };

    fixture.type(QStringLiteral("sign"));
    QTRY_VERIFY(findClearButton());
    QWidget* clearButton = findClearButton();
    QVERIFY(clearButton->geometry().right() < field->width());
    QTest::mouseClick(clearButton, Qt::LeftButton, Qt::NoModifier, clearButton->rect().center());
    SearchFixture::settle();
    QVERIFY(field->text().isEmpty());
}

void AppSearchTest::screenshotsStates()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the states at the size of their frames: no
    // results (955:142130, 1440 x 961), the empty query (no frame), the popup without
    // results and with one result (no frame), right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    SearchFixture fixture(QString(), QSize(1440, 961));
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Contract_signed.pdf")) });

    struct Shot
    {
        QString route;
        QString name;
        QString typed;
        bool isRightToLeft = false;
    };
    const Shot shots[] = { { QStringLiteral("/search?q=epub"), QStringLiteral("search-no-results-961"), QString(), false },
                           { QStringLiteral("/search"), QStringLiteral("search-empty-query"), QString(), false },
                           { QStringLiteral("/home"), QStringLiteral("search-popup-none"), QStringLiteral("epub"), false },
                           { QStringLiteral("/home"), QStringLiteral("search-popup-one"), QStringLiteral("Contract_signed"), false },
                           { QStringLiteral("/search?q=sign"), QStringLiteral("search-rtl"), QString(), true },
                           { QStringLiteral("/home"), QStringLiteral("search-popup-rtl"), QStringLiteral("sign"), true } };

    for (const Shot& shot : shots)
    {
        QApplication::setLayoutDirection(shot.isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        SearchFixture::settle();
        fixture.navigate(shot.route);
        if (!shot.typed.isEmpty())
        {
            fixture.type(shot.typed);
            QVERIFY(fixture.controller()->isPopupOpen());
        }

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            SearchFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = SearchTestHelper::render(fixture.window, ratio);
                QVERIFY(!image.isNull());
                QCOMPARE(SearchTestHelper::countLeakPixels(image), 0);
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(shot.name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(name)));
                }
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        if (fixture.controller())
        {
            fixture.controller()->closePopup();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

// ------------------------------------------------- coverage review of step 2

void AppSearchTest::normalizeDirect_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("ascii") << "Merge_PDF-2026" << "merge pdf 2026";
    QTest::newRow("non-BMP letter case folded") << QStringLiteral("\U00010400xé") << QStringLiteral("\U00010428xe");
    QTest::newRow("trailing high surrogate") << QStringLiteral("é") + QChar(0xD800) << "e ";
    QTest::newRow("high surrogate before a letter") << QStringLiteral("é") + QChar(0xD800) + QStringLiteral("a") << "e a";
    QTest::newRow("leading low surrogate") << QString(QChar(0xDC00)) + QStringLiteral("é") << " e";
    QTest::newRow("mark only") << QStringLiteral("́") << "";
    QTest::newRow("emoji") << QStringLiteral("é\U0001F600") << "e ";
}

void AppSearchTest::normalizeDirect()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(pdf::PDFSearchQuery::normalize(text), expected);
}

void AppSearchTest::splitWordsSurrogateCut()
{
    // A word of letters outside of the BMP is cut to MAXIMUM_WORD_LENGTH units
    // without a half of a surrogate pair at its end
    const QString letter = QStringLiteral("\U00010428");
    const QString word = QStringLiteral("a") + letter.repeated(40);
    const QStringList words = pdf::PDFSearchQuery::splitWords(word);
    QCOMPARE(words.size(), 1);
    QCOMPARE(words.front().size(), pdf::PDFSearchQuery::MAXIMUM_WORD_LENGTH - 1);
    QVERIFY(words.front().back().isLowSurrogate());
    QCOMPARE(words.front(), QStringLiteral("a") + letter.repeated(31));

    // An even cut keeps the whole limit
    const QStringList even = pdf::PDFSearchQuery::splitWords(letter.repeated(40));
    QCOMPARE(even.front().size(), pdf::PDFSearchQuery::MAXIMUM_WORD_LENGTH);
    QCOMPARE(even.front(), letter.repeated(32));

    // The query of such a word finds the word (prefix)
    QCOMPARE(pdf::PDFSearchQuery(letter.repeated(3)).matchText(letter.repeated(5)), pdf::PDFSearchMatchQuality::Prefix);
}

void AppSearchTest::isWithinOneEdit_data()
{
    QTest::addColumn<QString>("first");
    QTest::addColumn<QString>("second");
    QTest::addColumn<bool>("expected");

    QTest::newRow("equal") << "merge" << "merge" << true;
    QTest::newRow("both empty") << "" << "" << true;
    QTest::newRow("one empty") << "" << "a" << true;
    QTest::newRow("substitution first") << "merge" << "nerge" << true;
    QTest::newRow("substitution last") << "merge" << "mergx" << true;
    QTest::newRow("swap middle") << "merge" << "mrege" << true;
    QTest::newRow("swap last") << "merge" << "mereg" << true;
    QTest::newRow("swap and change") << "merge" << "rmegx" << false;
    QTest::newRow("two substitutions") << "merge" << "xergx" << false;
    QTest::newRow("insertion middle") << "merge" << "merxge" << true;
    QTest::newRow("insertion first") << "merge" << "xmerge" << true;
    QTest::newRow("insertion last") << "merge" << "mergex" << true;
    QTest::newRow("insertion and change") << "merge" << "xmergx" << false;
    QTest::newRow("two insertions") << "merge" << "mxerxge" << false;
    QTest::newRow("rotation") << "abcd" << "bcda" << false;
}

void AppSearchTest::isWithinOneEdit()
{
    QFETCH(QString, first);
    QFETCH(QString, second);
    QFETCH(bool, expected);
    QCOMPARE(pdf::PDFSearchQuery::isWithinOneEdit(first, second), expected);
    QCOMPARE(pdf::PDFSearchQuery::isWithinOneEdit(second, first), expected);
}

void AppSearchTest::toolSearchTranslations()
{
    // Translated title, description and keywords of a tool are searched together
    // with the English ones (a keyword in both lists is searched once)
    class ToolTranslator : public QTranslator
    {
    public:
        virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
        {
            Q_UNUSED(disambiguation);
            Q_UNUSED(n);

            const QByteArray source(sourceText ? sourceText : "");
            if (QByteArray(context ? context : "") != "pdf::PDFDocumentToolCatalog")
            {
                return QString();
            }
            if (source == "Merge")
            {
                return QStringLiteral("Zusammenführen");
            }
            if (source == "Combine several files into one PDF")
            {
                return QStringLiteral("Mehrere Dateien zu einem PDF vereinen");
            }
            if (source.startsWith("merge, join, combine"))
            {
                return QStringLiteral("verbinden, anfügen, join");
            }
            return QString();
        }

        virtual bool isEmpty() const override { return false; }
    };

    ToolTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    const pdf::PDFDocumentToolCatalogEntry* merge = nullptr;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        if (tool.getId() == QStringLiteral("pages.merge"))
        {
            merge = &tool;
        }
    }
    QVERIFY(merge);
    QCOMPARE(merge->getTranslatedKeywords(), QStringList({ "verbinden", QStringLiteral("anfügen"), "join" }));
    QVERIFY(merge->getDescription() != merge->getSourceDescription());

    struct Expected
    {
        QString query;
        pdf::PDFDocumentToolSearchMatch::Field field;
        QString keyword;
    };
    const Expected cases[] = { { QStringLiteral("zusammenfuhren"), pdf::PDFDocumentToolSearchMatch::Field::Title, QString() },
                               { QStringLiteral("merge"), pdf::PDFDocumentToolSearchMatch::Field::Title, QString() },
                               { QStringLiteral("verbinden"), pdf::PDFDocumentToolSearchMatch::Field::Keyword, QStringLiteral("verbinden") },
                               { QStringLiteral("join"), pdf::PDFDocumentToolSearchMatch::Field::Keyword, QStringLiteral("join") },
                               { QStringLiteral("concatenate"), pdf::PDFDocumentToolSearchMatch::Field::Keyword, QStringLiteral("concatenate") },
                               { QStringLiteral("dateien vereinen"), pdf::PDFDocumentToolSearchMatch::Field::Description, QString() },
                               { QStringLiteral("several files"), pdf::PDFDocumentToolSearchMatch::Field::Description, QString() } };
    for (const Expected& item : cases)
    {
        const std::optional<pdf::PDFDocumentToolSearchMatch> match = pdf::PDFDocumentToolSearch::matchTool(*merge, pdf::PDFSearchQuery(item.query));
        QVERIFY2(match, qPrintable(item.query));
        QCOMPARE(int(match->field), int(item.field));
        QCOMPARE(match->keyword, item.keyword);
    }

    // The title found by a translated word scores as the English title
    QCOMPARE(pdf::PDFDocumentToolSearch::matchTool(*merge, pdf::PDFSearchQuery(QStringLiteral("zusammenführen")))->score,
             pdf::PDFDocumentToolSearch::matchTool(*merge, pdf::PDFSearchQuery(QStringLiteral("merge")))->score);
    QCoreApplication::removeTranslator(&translator);

    // Without the translation the German words are not found
    QVERIFY(!pdf::PDFDocumentToolSearch::matchTool(*merge, pdf::PDFSearchQuery(QStringLiteral("verbinden"))));
    QVERIFY(!pdf::PDFDocumentToolSearch::matchTool(*merge, pdf::PDFSearchQuery(QStringLiteral("dateien vereinen"))));
}

void AppSearchTest::engineDocumentLimit()
{
    pdf::PDFAppSearchData data = SearchTestHelper::createData(nullptr, nullptr);
    const qint64 time = QDateTime(QDate(2026, 10, 1), QTime(9, 0)).toMSecsSinceEpoch();
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/Reports/annual.pdf"), time));    // folder only
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/annual report.pdf"), time));     // title word
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/report.pdf"), time));            // title starts by it
    data.recentFiles.push_back(SearchTestHelper::createFile(QStringLiteral("/a/other.pdf"), time));

    // The count of all matches is returned, the results are the best ones only
    // (a negative limit is no limit)
    const pdf::PDFAppDocumentSearchProvider provider;
    const pdf::PDFSearchQuery query(QStringLiteral("report"));
    for (const int limit : { -1, 0, 1, 2, 3, 100 })
    {
        std::vector<pdf::PDFAppSearchResult> results;
        QCOMPARE(provider.searchBest(query, data, limit, results), 3);
        QCOMPARE(int(results.size()), limit < 0 ? 3 : qMin(limit, 3));
        const QStringList best = { QStringLiteral("/a/report.pdf"), QStringLiteral("/a/annual report.pdf"), QStringLiteral("/a/Reports/annual.pdf") };
        for (size_t i = 0; i < results.size(); ++i)
        {
            QCOMPARE(results[i].key, best[qsizetype(i)]);
        }
    }

    // Results are appended to the given list
    std::vector<pdf::PDFAppSearchResult> results(2);
    QCOMPARE(provider.searchBest(query, data, 1, results), 3);
    QCOMPARE(int(results.size()), 3);
}

void AppSearchTest::engineHelpTopicFiles()
{
    // A file, whose name is not an id of a topic (no valid route), is refused by
    // the catalog; a heading without a text is not a result
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::pair<const char*, const char*> files[] = {
        { "bad%zz.md", "---\ntitle: Wombat broken\ncategory: settings\n---\nWombat text.\n\n## Wombat broken heading\n\nText.\n" },
        { "good.md", "---\ntitle: Wombat good\ncategory: settings\n---\nWombat text.\n\n## {#wombat-anchor}\n\nText.\n\n## Wombat details\n\nText.\n" }
    };
    for (const auto& file : files)
    {
        QFile topicFile(directory.filePath(QString::fromLatin1(file.first)));
        QVERIFY(topicFile.open(QIODevice::WriteOnly));
        topicFile.write(file.second);
    }

    const pdf::PDFAppHelpCatalog catalog = pdf::PDFAppHelpCatalog::load(directory.path());
    QCOMPARE(catalog.getErrors().size(), 1);
    QVERIFY(catalog.getErrors().front().startsWith(QStringLiteral("bad%zz.md:")));
    QCOMPARE(int(catalog.getTopics().size()), 1);
    QVERIFY(!catalog.findTopic(QStringLiteral("bad%zz")));
    const pdf::PDFAppHelpTopic* good = catalog.findTopic(QStringLiteral("good"));
    QVERIFY(good);
    QVERIFY(std::any_of(good->headings.cbegin(), good->headings.cend(), [](const pdf::PDFAppMarkdownHeading& heading) { return heading.text.isEmpty(); }));

    pdf::PDFAppSearchData data;
    data.helpCatalog = std::make_shared<const pdf::PDFAppHelpCatalog>(catalog);
    data.includeVideos = false;
    const pdf::PDFAppSearchResults results = pdf::PDFAppSearchEngine().search(QStringLiteral("wombat"), data);
    QCOMPARE(SearchTestHelper::keys(results, pdf::PDFAppSearchGroup::Help), QStringList({ "/help/good", "/help/good#wombat-details" }));
    QCOMPARE(results.getCount(pdf::PDFAppSearchGroup::Help), 2);
    for (const pdf::PDFAppSearchResult& result : results.getGroup(pdf::PDFAppSearchGroup::Help)->results)
    {
        QVERIFY(result.route.isValid());
        QVERIFY(!result.title.isEmpty());
    }
}

void AppSearchTest::engineShortcutReadError()
{
    // The shortcuts cannot be read (a damaged table): a warning, the default
    // shortcuts, the other data are read
    SearchFixture fixture;
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Shortcut probe.pdf")) });
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(connection);
    QVERIFY(!connection->execute("DROP TABLE shortcut").isError());

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The global search cannot read the shortcuts: ")));
    pdf::PDFAppSearchData data = pdf::PDFAppSearchData::load(fixture.context.get());
    QVERIFY(data.userShortcuts.empty());
    QCOMPARE(int(data.recentFiles.size()), 1);
    QVERIFY(data.commandRegistry);

    data.now = QDateTime(QDate(2026, 10, 21), QTime(12, 0));
    const pdf::PDFAppSearchResults results = pdf::PDFAppSearchEngine().search(QStringLiteral("new tab"), data);
    const pdf::PDFAppSearchResult* command = results.findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab());
    QVERIFY(command);
    QCOMPARE(command->meta, data.commandRegistry->getShortcut(pdf::PDFAppCommands::newTab(), {}).toString(QKeySequence::NativeText));
    QVERIFY(!command->meta.isEmpty());
    QCOMPARE(pdf::PDFAppSearchEngine().search(QStringLiteral("shortcut probe"), data).getCount(pdf::PDFAppSearchGroup::Documents), 1);
}

void AppSearchTest::pageWithoutDatabase()
{
    // A context without a database (not opened): no notifier, the registries and
    // the default shortcut Ctrl+K of the hints
    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
    QVERIFY(!context.getDatabase());
    QVERIFY(!context.getNotifier());

    pdf::PDFAppSearchPage page(&context);
    QVERIFY(page.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/search?q=new%20tab"))));
    QVERIFY(page.getResults().findResult(pdf::PDFAppSearchGroup::Commands, pdf::PDFAppCommands::newTab()));
    QCOMPARE(page.getResults().getCount(pdf::PDFAppSearchGroup::Documents), 0);
    QVERIFY(page.getCurrentRow());

    QVERIFY(page.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/search"))));
    QVERIFY(page.getEmptyState());
    QVERIFY(page.getRows().isEmpty());
    QVERIFY(!page.getCurrentRow());

    // The hints: Esc, then the keys of the search shortcut (the default one)
    QStringList keyCaps;
    for (const pdf::PDFAppKeyCap* keyCap : page.getEmptyState()->findChildren<pdf::PDFAppKeyCap*>())
    {
        keyCaps << keyCap->getText();
    }
    QCOMPARE(keyCaps, QStringList({ QStringLiteral("Esc") }) + pdf::PDFAppKeyCap::getKeyNames(QKeySequence(Qt::CTRL | Qt::Key_K)));

    // A chip of no group
    QVERIFY(!page.getChip(static_cast<pdf::PDFAppSearchGroup>(42)));
    QVERIFY(page.getChip(std::nullopt));
}

void AppSearchTest::pageFocusOfLaterGroup()
{
    // The focused row of a later group is focused again after a notification
    SearchFixture fixture;
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Signed later group.pdf")) });
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const QList<pdf::PDFAppSearchResultRow*> rows = page->getRows();
    QVERIFY(rows.size() >= 3);
    pdf::PDFAppSearchResultRow* focused = rows.last();
    QVERIFY(focused->getResult().group != rows.front()->getResult().group);
    const pdf::PDFAppSearchGroup group = focused->getResult().group;
    const QString key = focused->getResult().key;
    focused->setFocus(Qt::TabFocusReason);
    QCOMPARE(page->getCurrentRow(), focused);

    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    SearchFixture::settle();
    QVERIFY(!page->getRows().contains(focused));
    pdf::PDFAppSearchResultRow* current = page->getCurrentRow();
    QVERIFY(current);
    QCOMPARE(current->getResult().group, group);
    QCOMPARE(current->getResult().key, key);
    QVERIFY(current->hasFocus());
    QCOMPARE(current, page->getRows().last());
    QVERIFY(!page->getRows().front()->isCurrent());
}

void AppSearchTest::pageShowAllByKeyboard()
{
    // Show all activated by the keyboard: the focus goes to the first row of the group
    SearchFixture fixture;
    QStringList paths;
    for (int i = 0; i < 8; ++i)
    {
        paths << QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Signed keyboard %1.pdf").arg(i));
    }
    fixture.addRecent(paths);
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);

    pdf::PDFAppSearchShowAllButton* documents = nullptr;
    for (pdf::PDFAppSearchShowAllButton* button : page->getShowAllButtons())
    {
        if (button->getGroup() == pdf::PDFAppSearchGroup::Documents)
        {
            documents = button;
        }
    }
    QVERIFY(documents);
    documents->setFocus(Qt::TabFocusReason);
    QVERIFY(documents->hasFocus());
    QTest::keyClick(documents, Qt::Key_Space);
    SearchFixture::settle();
    QCOMPARE(page->getGroup(), std::make_optional(pdf::PDFAppSearchGroup::Documents));
    QCOMPARE(page->getRows().size(), 8);
    QVERIFY(page->getRows().front()->hasFocus());
    QCOMPARE(page->getCurrentRow(), page->getRows().front());

    // A click by the mouse leaves the focus where it is
    fixture.navigate(QStringLiteral("/search?q=sign"));
    page = fixture.searchPage();
    QVERIFY(page);
    fixture.field()->setFocus(Qt::OtherFocusReason);
    for (pdf::PDFAppSearchShowAllButton* button : page->getShowAllButtons())
    {
        if (button->getGroup() == pdf::PDFAppSearchGroup::Documents)
        {
            button->click();
            break;
        }
    }
    SearchFixture::settle();
    QCOMPARE(page->getGroup(), std::make_optional(pdf::PDFAppSearchGroup::Documents));
    QVERIFY(!page->getRows().front()->hasFocus());
}

void AppSearchTest::pageDeletedRow()
{
    // A row deleted by the application: the lists skip it, the arrows do not move
    // into it, a refresh builds the rows again
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    pdf::PDFAppSearchPage* page = fixture.searchPage();
    QVERIFY(page);
    const QList<pdf::PDFAppSearchResultRow*> rows = page->getRows();
    QVERIFY(rows.size() >= 3);
    const int count = rows.size();

    rows[0]->setFocus(Qt::TabFocusReason);
    delete rows[1];
    QCOMPARE(page->getRows().size(), count - 1);
    QCOMPARE(page->getCurrentRow(), rows[0]);
    QTest::keyClick(rows[0], Qt::Key_Down);
    QVERIFY(rows[0]->hasFocus());
    QTest::keyClick(rows[0], Qt::Key_End);
    QVERIFY(rows.last()->hasFocus());
    QCOMPARE(page->getCurrentRow(), rows.last());

    // The first row deleted: the current row is the next one which is left
    delete rows[0];
    rows.last()->clearFocus();
    QCOMPARE(page->getCurrentRow(), rows[2]);

    page->refresh();
    QCOMPARE(page->getRows().size(), count);
    QCOMPARE(page->getCurrentRow(), page->getRows().front());
}

void AppSearchTest::popupDisabled()
{
    // A disabled popup: no hover of the current row, the texts text/disabled
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());
    const QPoint probe = popup->getRowRect(0).topLeft() + QPoint(popup->getRowRect(0).width() / 2, 3);
    const QColor surface = SearchTestHelper::token(pdf::PDFThemeColors::BgSurface);
    const QColor hover = SearchTestHelper::blend(SearchTestHelper::token(pdf::PDFThemeColors::BgHover), surface);
    QVERIFY(SearchTestHelper::isClose(SearchTestHelper::pixel(SearchTestHelper::render(popup), probe), hover));

    popup->setEnabled(false);
    const QImage disabled = SearchTestHelper::render(popup);
    QVERIFY2(SearchTestHelper::isClose(SearchTestHelper::pixel(disabled, probe), surface), qPrintable(SearchTestHelper::pixel(disabled, probe).name()));
    QCOMPARE(SearchTestHelper::countLeakPixels(disabled), 0);
    popup->setEnabled(true);

    // A disabled group header of the page
    pdf::PDFAppSearchGroupHeader header(QStringLiteral("Tools"), QStringLiteral("3"));
    header.resize(400, 27);
    const QImage enabledHeader = SearchTestHelper::render(&header);
    header.setEnabled(false);
    const QImage disabledHeader = SearchTestHelper::render(&header);
    QCOMPARE(SearchTestHelper::countLeakPixels(disabledHeader), 0);
    QVERIFY(enabledHeader != disabledHeader);
}

void AppSearchTest::popupLowWindow()
{
    // Little room below the field: one row only, from the first group with a result
    SearchFixture fixture;
    fixture.addRecent({ QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Signed low window.pdf")) });
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());

    std::optional<pdf::PDFAppSearchGroup> firstGroup;
    int groups = 0;
    for (const pdf::PDFAppSearchGroup group : pdf::PDFAppSearchGroups::getGroups())
    {
        if (popup->getResults().getCount(group) > 0)
        {
            ++groups;
            firstGroup = firstGroup ? firstGroup : std::make_optional(group);
        }
    }
    QVERIFY(groups >= 2);

    const int height = fixture.window->height();
    popup->placeBelow(QRect(400, height - 110, 400, 32));
    QCOMPARE(popup->getRowCount(), 1);
    QCOMPARE(popup->getItems().size(), size_t(2));
    QCOMPARE(popup->getRowResult(0)->group, *firstGroup);
    QCOMPARE(popup->getItems().front().count, pdf::PDFAppSearchPainter::getCountText(1, popup->getResults().getCount(*firstGroup)));

    // Room again: every group with a result has a row
    const QRect fieldRect(fixture.field()->mapTo(fixture.window, QPoint(0, 0)), fixture.field()->size());
    popup->placeBelow(fieldRect);
    int headers = 0;
    for (const pdf::PDFAppSearchPopup::Item& item : popup->getItems())
    {
        headers += item.kind == pdf::PDFAppSearchPopup::Item::Kind::GroupHeader ? 1 : 0;
    }
    QCOMPARE(headers, groups);
}

void AppSearchTest::popupWithoutAnchorAndShadow()
{
    QWidget host;
    host.resize(800, 600);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    pdf::PDFAppSearchData data = SearchTestHelper::createData(nullptr, nullptr);
    data.includeVideos = false;
    const pdf::PDFAppSearchResults results = pdf::PDFAppSearchEngine().search(QStringLiteral("sign"), data);
    QVERIFY(results.getTotalCount() > 1);

    // Opened before it was placed: no room is known, one row
    QPointer<pdf::PDFAppSearchPopup> popup = new pdf::PDFAppSearchPopup(&host);
    popup->setResults(results);
    popup->open();
    QVERIFY(popup->isOpen());
    QCOMPARE(popup->getRowCount(), 1);
    QVERIFY(popup->getShadow()->isVisible());

    // The shadow deleted by the application: the popup works without it
    delete popup->getShadow();
    QVERIFY(!popup->getShadow());
    popup->close();
    QVERIFY(!popup->isOpen());
    popup->placeBelow(QRect(200, 10, 560, 32));
    popup->open();
    QVERIFY(popup->isOpen());
    QVERIFY(popup->getRowCount() > 1);
    QCOMPARE(popup->geometry().top(), 10 + 32 + pdf::PDFAppSearchPopup::ANCHOR_GAP);
    QCOMPARE(SearchTestHelper::countLeakPixels(SearchTestHelper::render(popup)), 0);
    popup->close();
    delete popup.data();
}

void AppSearchTest::popupShowAllPressedOutside()
{
    // The link pressed and the pointer moved away: the link stays pressed (no
    // hover), the release outside does nothing
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());
    const QRect link = popup->getShowAllRect();
    QVERIFY(!link.isEmpty());

    QTest::mouseMove(popup, QPoint(2, 2));
    const QImage idle = SearchTestHelper::render(popup).copy(link);
    QTest::mouseMove(popup, link.center());
    QTest::mousePress(popup, Qt::LeftButton, Qt::NoModifier, link.center());
    const QImage pressedHovered = SearchTestHelper::render(popup).copy(link);
    QTest::mouseMove(popup, QPoint(2, 2));
    const QImage pressedOutside = SearchTestHelper::render(popup).copy(link);
    QVERIFY(idle != pressedOutside);
    QCOMPARE(pressedOutside, pressedHovered);

    QSignalSpy showAll(popup, &pdf::PDFAppSearchPopup::showAllRequested);
    QTest::mouseRelease(popup, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(showAll.size(), 0);
    QVERIFY(popup->isOpen());
    QCOMPARE(SearchTestHelper::render(popup).copy(link), idle);
}

void AppSearchTest::popupKeysWithoutPopup()
{
    SearchFixture fixture;
    pdf::PDFAppSearchController* controller = fixture.controller();
    fixture.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    fixture.field()->setFocus(Qt::OtherFocusReason);
    QVERIFY(fixture.field()->hasFocus());

    // Down in an empty field opens nothing
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QVERIFY(!controller->isPopupOpen());

    // Down right after typing shows the results of the typed text first (the
    // current row stays the first one)
    fixture.type(QStringLiteral("mer"));
    QVERIFY(controller->isPopupOpen());
    QCOMPARE(fixture.popup()->getResults().text, QStringLiteral("mer"));
    QCOMPARE(fixture.popup()->getCurrentRow(), 0);
    const pdf::PDFAppSearchResult previous = *fixture.popup()->getCurrentResult();
    QTest::keyClicks(fixture.field(), QStringLiteral("ge"));
    QVERIFY(controller->isSearchPending());
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QVERIFY(!controller->isSearchPending());
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QCOMPARE(popup->getResults().text, QStringLiteral("merge"));

    // The current result is the one of the shorter text (kept by its identity),
    // the arrow has not moved it
    int expectedRow = 0;
    for (int row = 0; row < popup->getRowCount(); ++row)
    {
        if (popup->getRowResult(row)->group == previous.group && popup->getRowResult(row)->key == previous.key)
        {
            expectedRow = row;
        }
    }
    QCOMPARE(popup->getCurrentRow(), expectedRow);
    QVERIFY(popup->getRowCount() > expectedRow + 1);
    QTest::keyClick(fixture.field(), Qt::Key_Down);
    QCOMPARE(popup->getCurrentRow(), expectedRow + 1);

    // Page keys and Up without the popup do nothing (the field keeps the text)
    QTest::keyClick(fixture.field(), Qt::Key_Escape);
    QVERIFY(!controller->isPopupOpen());
    for (const Qt::Key key : { Qt::Key_PageDown, Qt::Key_PageUp, Qt::Key_Up })
    {
        QTest::keyClick(fixture.field(), key);
        QVERIFY(!controller->isPopupOpen());
        QVERIFY(!controller->isSearchPending());
        QCOMPARE(fixture.field()->text(), QStringLiteral("merge"));
    }
}

void AppSearchTest::popupEventsOnce()
{
    // While the popup is open, the controller filters the field and its window, and
    // a filter of the application of its own takes only the presses outside: an
    // event of the field reaches the controller once (a change of the language
    // searched twice). The files of Recent are read and normalized once per
    // opening of the popup, not for each typed text.
    SearchFixture fixture;
    const QString document = QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Quarterly report.pdf"));
    fixture.addRecent({ document, QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Other.pdf")) });
    fixture.type(QStringLiteral("quarterly"));
    pdf::PDFAppSearchController* controller = fixture.controller();
    QVERIFY(controller->isPopupOpen());
    QCOMPARE(controller->getDataLoadCount(), 1);
    const int searches = controller->getSearchCount();
    QVERIFY(searches >= 1);
    QString key;
    for (const pdf::PDFRecentFile& file : fixture.context->getRecent().getFiles())
    {
        key = file.path.contains(QStringLiteral("Quarterly")) ? file.path : key;
    }
    QVERIFY(!key.isEmpty());
    QVERIFY(controller->getPopup()->getResults().findResult(pdf::PDFAppSearchGroup::Documents, key));

    // A change of the language searches once
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(fixture.field(), &languageChange);
    QCOMPARE(controller->getSearchCount(), searches + 1);

    // A pending search is done once
    QTest::keyClicks(fixture.field(), QStringLiteral(" rep"));
    QVERIFY(controller->isSearchPending());
    QCOMPARE(controller->getSearchCount(), searches + 1);
    controller->updateSearch();
    QCOMPARE(controller->getSearchCount(), searches + 2);
    QCOMPARE(controller->getPopup()->getResults().getCount(pdf::PDFAppSearchGroup::Documents), 1);

    // More typing uses the data read at the opening
    for (int i = 0; i < 5; ++i)
    {
        QTest::keyClick(fixture.field(), Qt::Key_Backspace);
        controller->updateSearch();
    }
    QCOMPARE(controller->getDataLoadCount(), 1);
    QCOMPARE(controller->getSearchCount(), searches + 7);

    // A key, which the controller does not take (Up in the open popup is taken, Left
    // is not), goes on to the field once: the cursor moves by one character
    fixture.field()->end(false);
    const int cursor = fixture.field()->cursorPosition();
    QTest::keyClick(fixture.field(), Qt::Key_Left);
    QCOMPARE(fixture.field()->cursorPosition(), cursor - 1);
    QVERIFY(controller->isPopupOpen());

    // A resize of the window places the popup once more below the field (the popup
    // stays below the field), a press outside closes it once
    fixture.window->resize(1300, 900);
    SearchFixture::settle();
    QVERIFY(controller->isPopupOpen());
    QWidget* popup = controller->getPopup();
    const QPoint fieldBottom = fixture.field()->mapTo(fixture.window.data(), QPoint(0, fixture.field()->height()));
    QVERIFY2(qAbs(popup->geometry().top() - fieldBottom.y()) <= 20, qPrintable(QStringLiteral("%1 / %2").arg(popup->geometry().top()).arg(fieldBottom.y())));
    QWidget* header = fixture.homeTab()->getHeader()->getTitleLabel();
    QTest::mousePress(header, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QTest::mouseRelease(header, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QVERIFY(!controller->isPopupOpen());

    // The next opening reads the data again (never stale data)
    fixture.type(QStringLiteral("other"));
    QVERIFY(controller->isPopupOpen());
    QCOMPARE(controller->getDataLoadCount(), 2);
    controller->closePopup();
}

void AppSearchTest::preparedDocumentFields()
{
    // The fields of a search item are normalized once (PDFSearchField::prepare); a
    // prepared field matches as an unprepared one, a field of other texts is split
    pdf::PDFSearchField field({ QStringLiteral("Quarterly Report"), QStringLiteral("Čtvrtletní zpráva") }, 100, 90, 60, true);
    QVERIFY(!field.isPrepared());
    field.prepare();
    QVERIFY(field.isPrepared());
    QCOMPARE(field.words.size(), size_t(2));
    QCOMPARE(field.words[0], QStringList({ QStringLiteral("quarterly"), QStringLiteral("report") }));
    QCOMPARE(field.words[1], pdf::PDFSearchQuery::splitWords(QStringLiteral("Čtvrtletní zpráva")));
    pdf::PDFSearchField unprepared({ QStringLiteral("Quarterly Report"), QStringLiteral("Čtvrtletní zpráva") }, 100, 90, 60, true);
    for (const QString& text : { QStringLiteral("quarterly"), QStringLiteral("report quart"), QStringLiteral("ctvrtletni"), QStringLiteral("quartrely"), QStringLiteral("missing"), QStringLiteral("quarterly report") })
    {
        const pdf::PDFSearchQuery query(text);
        const std::optional<pdf::PDFSearchFieldMatch> prepared = query.match({ field });
        const std::optional<pdf::PDFSearchFieldMatch> plain = query.match({ unprepared });
        QCOMPARE(prepared.has_value(), plain.has_value());
        if (prepared)
        {
            QCOMPARE(prepared->score, plain->score);
            QCOMPARE(prepared->weakestField, plain->weakestField);
            QCOMPARE(prepared->weakestText, plain->weakestText);
        }
    }

    // A field changed after its preparation (other count of the texts) is split again
    field.texts << QStringLiteral("Extra");
    QVERIFY(!field.isPrepared());
    QVERIFY(pdf::PDFSearchQuery(QStringLiteral("extra")).match({ field }).has_value());

    // The data of the context prepare the files of Recent; data filled by hand
    // (not prepared, or prepared for other files) give the same results
    SearchFixture fixture;
    QStringList paths;
    for (int i = 0; i < 40; ++i)
    {
        paths << QDir::temp().filePath(QStringLiteral("pdf4qt-search-test/Folder %1/Report %2.pdf").arg(i % 4).arg(i));
    }
    fixture.addRecent(paths);
    const pdf::PDFAppSearchData data = pdf::PDFAppSearchData::load(fixture.context.get());
    QVERIFY(data.recentFiles.size() >= 10);
    QCOMPARE(data.documentFields.size(), data.recentFiles.size());
    for (const std::vector<pdf::PDFSearchField>& fields : data.documentFields)
    {
        QCOMPARE(fields.size(), size_t(2));
        QVERIFY(fields[0].isPrepared() && fields[1].isPrepared());
        QVERIFY(fields[0].isTitle);
    }

    pdf::PDFAppSearchData plain;
    plain.recentFiles = data.recentFiles;
    pdf::PDFAppSearchData stale;
    stale.recentFiles = data.recentFiles;
    stale.prepareDocuments();
    stale.documentFields.resize(3);
    pdf::PDFAppSearchData prepared;
    prepared.recentFiles = data.recentFiles;
    prepared.prepareDocuments();
    QCOMPARE(prepared.documentFields.size(), data.recentFiles.size());

    const pdf::PDFAppSearchEngine engine;
    for (const QString& text : { QStringLiteral("report 3"), QStringLiteral("folder 2"), QStringLiteral("reprot"), QStringLiteral("pdf4qt search"), QStringLiteral("none at all") })
    {
        const pdf::PDFAppSearchResults first = engine.search(text, prepared);
        for (const pdf::PDFAppSearchData* other : { static_cast<const pdf::PDFAppSearchData*>(&plain), static_cast<const pdf::PDFAppSearchData*>(&stale), &data })
        {
            const pdf::PDFAppSearchResults second = engine.search(text, *other);
            QCOMPARE(second.getCount(pdf::PDFAppSearchGroup::Documents), first.getCount(pdf::PDFAppSearchGroup::Documents));
            const pdf::PDFAppSearchGroupResults* firstGroup = first.getGroup(pdf::PDFAppSearchGroup::Documents);
            const pdf::PDFAppSearchGroupResults* secondGroup = second.getGroup(pdf::PDFAppSearchGroup::Documents);
            QVERIFY(firstGroup && secondGroup);
            QCOMPARE(secondGroup->results.size(), firstGroup->results.size());
            for (size_t i = 0; i < firstGroup->results.size(); ++i)
            {
                QCOMPARE(secondGroup->results[i].key, firstGroup->results[i].key);
                QCOMPARE(secondGroup->results[i].score, firstGroup->results[i].score);
            }
        }
    }
}

void AppSearchTest::popupPressOnWindow()
{
    // Presses delivered by the window of the system (QWindow first, then the
    // widget): a press on the popup keeps it, a press outside closes it
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchPopup* popup = fixture.popup();
    QVERIFY(popup && popup->isOpen());
    QWindow* window = fixture.window->windowHandle();
    QVERIFY(window);

    const pdf::PDFAppSearchPopup::Item& header = popup->getItems().front();
    QCOMPARE(header.kind, pdf::PDFAppSearchPopup::Item::Kind::GroupHeader);
    const QPoint onHeader = popup->mapTo(fixture.window, header.rect.center());
    QCOMPARE(fixture.window->childAt(onHeader), static_cast<QWidget*>(popup));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onHeader);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, onHeader);
    SearchFixture::settle();
    QVERIFY(fixture.controller()->isPopupOpen());

    pdf::PDFAppFooter* footer = fixture.homeTab()->getFooter();
    const QPoint onFooter = footer->mapTo(fixture.window, QPoint(footer->width() / 2, footer->height() / 2));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onFooter);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, onFooter);
    SearchFixture::settle();
    QVERIFY(!fixture.controller()->isPopupOpen());
}

void AppSearchTest::controllerEdgeCases()
{
    SearchFixture fixture;
    fixture.type(QStringLiteral("sign"));
    pdf::PDFAppSearchController* controller = fixture.controller();
    QVERIFY(controller->isPopupOpen());

    // A timer of another owner is not the timer of the search
    QTimerEvent foreignTimer(123456);
    QCoreApplication::sendEvent(controller, &foreignTimer);
    QVERIFY(controller->isPopupOpen());
    QVERIFY(!controller->isSearchPending());

    // A disabled command is not run; an empty path is not opened
    const int tabs = fixture.window->getTabs().size();
    QAction* action = fixture.window->getCommandAction(pdf::PDFAppCommands::newTab());
    QVERIFY(action);
    action->setEnabled(false);
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(fixture.field(), pdf::PDFAppCommands::newTab()));
    QCOMPARE(fixture.window->getTabs().size(), tabs);
    action->setEnabled(true);
    QVERIFY(pdf::PDFAppSearchActions::runCommand(fixture.field(), pdf::PDFAppCommands::newTab()));
    SearchFixture::settle();
    QCOMPARE(fixture.window->getTabs().size(), tabs + 1);

    // The field of the hidden tab (with its text) opens no popup
    QVERIFY(!fixture.field()->isVisible());
    QVERIFY(!fixture.field()->text().isEmpty());
    controller->updateSearch();
    QVERIFY(!controller->isPopupOpen());

    const int requested = fixture.window->getRequestedFiles().size();
    QVERIFY(!pdf::PDFAppSearchActions::openDocument(fixture.field(), QString()));
    QCOMPARE(fixture.window->getRequestedFiles().size(), requested);
}

void AppSearchTest::pageDeletedByRouteReplaced()
{
    // A slot of routeReplaced deletes the page (a navigation of the application):
    // the filter stops, titleChanged is not emitted by a deleted page
    SearchFixture fixture;
    fixture.navigate(QStringLiteral("/search?q=sign"));
    QPointer<pdf::PDFAppSearchPage> page = fixture.searchPage();
    QVERIFY(page);
    int titleChanges = 0;
    QObject::connect(page.data(), &pdf::PDFAppPage::titleChanged, page.data(), [&titleChanges]() { ++titleChanges; });
    QObject::connect(page.data(), &pdf::PDFAppPage::routeReplaced, page.data(), [page]() { delete page.data(); });
    page->setGroupFilter(pdf::PDFAppSearchGroup::Tools);
    QVERIFY(!page);
    QCOMPARE(titleChanges, 0);
    SearchFixture::settle();
}

QTEST_MAIN(AppSearchTest)

#include "tst_appsearchtest.moc"
