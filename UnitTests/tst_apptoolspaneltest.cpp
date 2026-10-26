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

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumenttoolspanel.h"
#include "pdfappdocumentview.h"
#include "pdfapphomepage.h"
#include "pdfapphomewidgets.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfappsearchpage.h"
#include "pdfappsearchwidgets.h"
#include "pdfappsupportpage.h"
#include "pdfappwhatsnewpage.h"
#include "pdfapptoolpresentation.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"

#include "pdfdocumenttoolcatalog.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QPointer>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimeZone>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

/// Helpers of the tests of the Tools panel
class ToolsPanelTestHelper
{
public:
    ToolsPanelTestHelper() = delete;

    using Panel = pdf::PDFAppDocumentToolbar::Panel;

    /// The six pinned tools of 196:3650 (and of 272:12876) in their order
    static QStringList figmaPins()
    {
        return { QStringLiteral("pages.merge"), QStringLiteral("convert.ocr"), QStringLiteral("protect.sign"),
                 QStringLiteral("protect.sanitize"), QStringLiteral("optimize.compress"), QStringLiteral("review.compare") };
    }

    /// Pins the tools in their order (the connection of the window)
    static bool pin(pdftest::AppToolsFixture& fixture, const QStringList& toolIds)
    {
        pdf::PDFToolRepository repository = fixture.tools();
        for (const QString& toolId : toolIds)
        {
            if (repository.setPinned(toolId, true).isError())
            {
                return false;
            }
        }
        pdftest::AppDocumentFixture::settle();
        return true;
    }

    /// Records the use of the tool at the time (the connection of the window)
    static bool use(pdftest::AppToolsFixture& fixture, const QString& toolId, qint64 time)
    {
        pdf::PDFToolRepository repository = fixture.tools();
        repository.setCurrentTime(time);
        const bool isOk = !repository.recordUsage(toolId).isError();
        pdftest::AppDocumentFixture::settle();
        return isOk;
    }

    /// Opens a document and waits until its view shows the pages
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppToolsFixture& fixture, int pageCount = 3)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openDocument(pageCount);
        if (!tab)
        {
            return nullptr;
        }
        const bool isReady = QTest::qWaitFor([&tab]()
        {
            return tab && tab->getDocument() && tab->getDocument()->isReady() && tab->getView() && tab->getView()->getPageCount() > 0;
        }, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        pdftest::AppDocumentFixture::settle();
        return isReady ? tab.data() : nullptr;
    }

    static pdf::PDFAppDocumentToolbar* toolbar(pdf::PDFAppDocumentTab* tab)
    {
        return tab && tab->getView() ? tab->getView()->getToolbar() : nullptr;
    }

    /// Opens the Tools panel of the tab
    static pdf::PDFAppDocumentToolsPanel* open(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFAppDocumentToolbar* bar = toolbar(tab);
        if (!bar)
        {
            return nullptr;
        }
        pdf::PDFAppDocumentToolsPanel* panel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(bar->openPanel(Panel::Tools));
        pdftest::AppDocumentFixture::settle();
        return panel;
    }

    template<typename T>
    static QStringList toolIds(const std::vector<T*>& widgets)
    {
        QStringList ids;
        for (const T* widget : widgets)
        {
            ids << pdf::PDFAppToolPresentation::getToolId(widget);
        }
        return ids;
    }

    static QStringList resultKeys(const pdf::PDFAppDocumentToolsPanel* panel)
    {
        QStringList keys;
        for (const pdf::PDFAppSearchResultRow* row : panel->getResultRows())
        {
            keys << row->getResult().key;
        }
        return keys;
    }

    /// The widget is the focus widget of its window (the offscreen platform does not
    /// activate the popups, so QWidget::hasFocus is not reliable there)
    static bool isFocused(QWidget* widget)
    {
        const bool isFocused = widget && widget->window()->focusWidget() == widget;
        if (!isFocused && widget)
        {
            const QWidget* focus = widget->window()->focusWidget();
            qInfo() << "focus widget" << (focus ? focus->metaObject()->className() : "none") << (focus ? focus->objectName() : QString())
                    << "app focus" << (QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QString()) << "expected" << widget->objectName();
        }
        return isFocused;
    }

    /// Rectangle of the widget in the global coordinates
    static QRect globalRect(const QWidget* widget)
    {
        return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
    }

    static QRect panelRect(const pdf::PDFAppDocumentToolsPanel* panel)
    {
        const QRect rect = panel->getPanelRect();
        return QRect(panel->mapToGlobal(rect.topLeft()), rect.size());
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground));
        return image;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if (qAlpha(line[x]) == 255 && (line[x] & 0x00FFFFFF) == (leak & 0x00FFFFFF))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    static bool isClose(const QColor& a, const QColor& b, int tolerance = 6)
    {
        return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance && qAbs(a.blue() - b.blue()) <= tolerance;
    }

    /// Writes the picture of the panel (PDF4QT_APP_SCREENSHOTS=<folder>)
    static void screenshot(QWidget* widget, const QString& name, qreal ratio = 1.0)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (folder.isEmpty() || !widget)
        {
            return;
        }
        QDir().mkpath(folder);
        render(widget, ratio).save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
    }

    /// Moves the pointer over the widget through the window system (an open panel grabs the mouse)
    static void hover(QWidget* widget)
    {
        QWidget* window = widget->window();
        QTest::mouseMove(window->windowHandle(), window->mapFromGlobal(widget->mapToGlobal(widget->rect().center())));
    }

    /// The panel lies 4 px below the toolbar at the leading edge of its button; the
    /// panel, which does not fit, is moved inside of the available area of the screen
    static void checkPlacement(const QRect& panelRect, QToolButton* button, pdf::PDFAppDocumentToolbar* toolbar)
    {
        const QRect available = button->screen()->availableGeometry();
        const QRect buttonRect = globalRect(button);
        const int expectedTop = toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + pdf::PDFAppDocumentToolbar::PANEL_GAP;
        if (expectedTop + panelRect.height() <= available.bottom() + 1)
        {
            QVERIFY2(panelRect.top() == expectedTop, qPrintable(QStringLiteral("top %1 expected %2 available %3,%4 %5x%6 toolbar %7 window %8")
                .arg(panelRect.top()).arg(expectedTop).arg(available.x()).arg(available.y()).arg(available.width()).arg(available.height())
                .arg(toolbar->mapToGlobal(QPoint(0, 0)).y()).arg(toolbar->window()->geometry().y())));
        }
        else
        {
            QCOMPARE(panelRect.bottom(), available.bottom());
        }

        const bool isRightToLeft = button->layoutDirection() == Qt::RightToLeft;
        const int expectedLeft = isRightToLeft ? buttonRect.right() + 1 - panelRect.width() : buttonRect.left();
        const int boundedLeft = qBound(available.left(), expectedLeft, qMax(available.left(), available.right() + 1 - panelRect.width()));
        QCOMPARE(panelRect.left(), boundedLeft);
    }

    /// Goes back in the history of the tab (the document view after a tool route)
    static void back(pdftest::AppToolsFixture& fixture, pdf::PDFAppTab* tab)
    {
        fixture.window->getNavigator()->goBack(tab);
        pdftest::AppDocumentFixture::settle();
    }

    static QString documentToolRoute(pdf::PDFAppDocumentTab* tab, const QString& toolId)
    {
        return QStringLiteral("/document/%1/tool/%2").arg(tab->getDocumentId(), toolId);
    }
};

/// Translator of the language change test: every text in brackets
class ToolsPanelTestTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return sourceText && *sourceText ? QStringLiteral("[") + QString::fromUtf8(sourceText) + QStringLiteral("]") : QString();
    }
};

/// The Tools panel of the document toolbar (step 6, package C; Figma 196:3650)
class AppToolsPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void panelGeometry();
    void panelPinnedAndRecent();
    void panelEmptyStates();
    void panelSearchToolsAndCommands();
    void panelSearchHostileTexts();
    void panelNearestTools();
    void panelLockedDocument();
    void panelNoDocumentTools();
    void panelSwitchWithTools_data();
    void panelSwitchWithTools();
    void panelFollowsOtherInstance();
    void homeWhatsNewSupportFollowPins();
    void globalSearchToolRoute();
    void hostileStoredRows();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales_data();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void panelReopenFocus();
    void panelRecentTimesFollowClock();
};

void AppToolsPanelTest::initTestCase()
{
    // The style of the application (popovers with their shadows, the tokens)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppToolsPanelTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolsPanelTest::panelGeometry()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(H::use(fixture, QStringLiteral("edit.content"), now - 3 * 86400000LL));
    QVERIFY(H::use(fixture, QStringLiteral("protect.redact"), now - 86400000LL));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), now - 120000));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);
    QToolButton* toolsButton = toolbar->getToolsButton();
    QCOMPARE(toolbar->getMenuButton(H::Panel::Tools), toolsButton);
    QVERIFY(toolsButton->isCheckable() && toolsButton->isEnabled());

    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel && panel->isVisible());
    QCOMPARE(panel->objectName(), QStringLiteral("toolsPanel"));
    QCOMPARE(panel->accessibleName(), QStringLiteral("Tools"));
    QCOMPARE(toolbar->findPanel(H::Panel::Tools), panel);
    QCOMPARE(toolbar->getOpenPanelButton(), toolsButton);
    QVERIFY(toolsButton->isChecked());

    // 196:3650: 400 wide, 4 px below the toolbar, at the leading edge of its button
    // (kept inside of the screen - the screen of the offscreen platform is small)
    const QRect panelRect = H::panelRect(panel);
    QCOMPARE(panelRect.width(), 400);
    H::checkPlacement(panelRect, toolsButton, toolbar);

    // tools-search 196:3651: 366 x 36 at 17 / 17 (padding 16 inside of the border)
    const QRect search = H::globalRect(panel->getSearchField());
    QCOMPARE(search.topLeft() - panelRect.topLeft(), QPoint(17, 17));
    QCOMPARE(search.size(), QSize(366, 36));
    QCOMPARE(panel->getSearchField()->placeholderText(), QString::fromUtf8("Search tools and actions…"));
    QVERIFY(H::isFocused(panel->getSearchField()));

    // Pinned 196:3655: the caption 14 below the field, the shortcuts 179 x 48 in two columns, gaps 8
    const std::vector<pdf::PDFToolShortcut*> shortcuts = panel->getPinnedShortcuts();
    QCOMPARE(H::toolIds(shortcuts), H::figmaPins());
    const QRect first = H::globalRect(shortcuts.front());
    QCOMPARE(first.size(), QSize(179, 48));
    QCOMPARE(first.left(), panelRect.left() + 17);
    QCOMPARE(first.top(), search.bottom() + 1 + 14 + 13 + 8);
    for (size_t i = 0; i < shortcuts.size(); ++i)
    {
        const QRect rect = H::globalRect(shortcuts[i]);
        QCOMPARE(rect.size(), QSize(179, 48));
        QCOMPARE(rect.topLeft() - first.topLeft(), QPoint(int(i % 2) * (179 + 8), int(i / 2) * (48 + 8)));
        QCOMPARE(shortcuts[i]->getName(), pdf::PDFDocumentToolCatalog::findTool(H::figmaPins().at(int(i)))->getTitle());
    }
    QVERIFY(!panel->getPinnedEmptyLabel());

    // Recent 196:3704: 14 below the shortcuts, the caption, three rows 366 x 48 without gaps
    const std::vector<pdf::PDFRecentToolRow*> recent = panel->getRecentRows();
    QCOMPARE(H::toolIds(recent), QStringList({ QStringLiteral("review.measure"), QStringLiteral("protect.redact"), QStringLiteral("edit.content") }));
    const QRect lastShortcut = H::globalRect(shortcuts.back());
    for (size_t i = 0; i < recent.size(); ++i)
    {
        const QRect rect = H::globalRect(recent[i]);
        QCOMPARE(rect.size(), QSize(366, 48));
        QCOMPARE(rect.left(), panelRect.left() + 17);
        QCOMPARE(rect.top(), lastShortcut.bottom() + 1 + 14 + 13 + 8 + int(i) * 48);
    }
    QCOMPARE(recent.front()->getLastUsed(), pdf::PDFAppHomeFormat::formatTime(now - 120000, QDateTime::currentMSecsSinceEpoch()));

    // divider 196:3737 and the Link row All tools 196:3738 (366 x 50), the panel 523 high
    pdf::PDFLinkRow* allTools = panel->getAllToolsRow();
    const QRect allToolsRect = H::globalRect(allTools);
    QCOMPARE(allToolsRect.size(), QSize(366, 50));
    QCOMPARE(allToolsRect.top(), H::globalRect(recent.back()).bottom() + 1 + 14 + 1 + 14);
    QCOMPARE(allTools->getTitle(), QStringLiteral("All tools"));
    QCOMPARE(allTools->getSubtitle(), QStringLiteral("Browse the complete catalog of 46 tools"));
    QCOMPARE(allTools->getIconName(), QStringLiteral("tools"));
    QCOMPARE(panelRect.bottom() - allToolsRect.bottom(), 17);
    QCOMPARE(panelRect.height(), 523);

    // The panel draws bg/surface with its border (no leak color)
    const QImage image = H::render(panel, 1.0);
    QCOMPARE(H::countLeakPixels(image), 0);
    const QPoint inside = panel->getPanelRect().topLeft() + QPoint(8, 8);
    QVERIFY(H::isClose(image.pixelColor(inside), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface)));
    H::screenshot(panel, QStringLiteral("tools-panel-light-1x"));
    H::screenshot(panel, QStringLiteral("tools-panel-light-2x"), 2.0);
    fixture.screenshot(QStringLiteral("tools-panel-window"));

    // A click on the button of the open panel closes it
    panel->close();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!toolsButton->isChecked());
    QVERIFY(!toolbar->getOpenPanelButton());
}

void AppToolsPanelTest::panelPinnedAndRecent()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;

    // Eight pins: the first six in the order of the pins
    const QStringList pins = { QStringLiteral("review.compare"), QStringLiteral("pages.split"), QStringLiteral("protect.sign"), QStringLiteral("pages.merge"),
                               QStringLiteral("optimize.images"), QStringLiteral("convert.ocr"), QStringLiteral("edit.content"), QStringLiteral("review.measure") };
    QVERIFY(H::pin(fixture, pins));

    // Five uses: the three last ones, the most recent first
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QStringList used = { QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("optimize.compress"), QStringLiteral("protect.redact"), QStringLiteral("review.measure") };
    for (int i = 0; i < used.size(); ++i)
    {
        QVERIFY(H::use(fixture, used.at(i), now - (used.size() - i) * 60000LL));
    }

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), pins.mid(0, pdf::PDFAppDocumentToolsPanel::MAXIMUM_PINNED));
    QCOMPARE(H::toolIds(panel->getRecentRows()), QStringList({ QStringLiteral("review.measure"), QStringLiteral("protect.redact"), QStringLiteral("optimize.compress") }));
    QCOMPARE(panel->getRecentRows().front()->getLastUsed(), pdf::PDFAppHomeFormat::formatTime(now - 60000LL, QDateTime::currentMSecsSinceEpoch()));
    QCOMPARE(panel->getPinnedShortcuts().front()->toolTip(), pdf::PDFDocumentToolCatalog::findTool(pins.front())->getDescription());

    // A shortcut opens the tool on the document: Page Not Found of the tool in the
    // tab of the document until phase 2 registers its page
    QCOMPARE(panel->getToolRoute(QStringLiteral("pages.merge")).toString(), H::documentToolRoute(tab, QStringLiteral("pages.merge")));
    panel->getPinnedShortcuts().at(3)->click();
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QCOMPARE(fixture.currentRoute(), H::documentToolRoute(tab, QStringLiteral("pages.merge")));
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));
    QVERIFY(!H::toolbar(tab)->getToolsButton()->isChecked());

    // A recent row with a registered page of the tool: the page in the tab of the document
    H::back(fixture, tab);
    QVERIFY(H::toolbar(tab)->isVisible());
    QVERIFY(fixture.registerTestToolPage(QStringLiteral("protect.redact"), pdf::PDFAppToolPageKind::Document));
    panel = H::open(tab);
    QVERIFY(panel);
    pdf::PDFRecentToolRow* redact = panel->getRecentRows().at(1);
    QCOMPARE(pdf::PDFAppToolPresentation::getToolId(redact), QStringLiteral("protect.redact"));
    redact->click();
    pdftest::AppDocumentFixture::settle();
    pdftest::AppToolsTestPage* testPage = fixture.page<pdftest::AppToolsTestPage>();
    QVERIFY(testPage);
    QCOMPARE(testPage->getToolId(), QStringLiteral("protect.redact"));
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));

    // All tools: the catalog in the Home tab
    H::back(fixture, tab);
    panel = H::open(tab);
    QVERIFY(panel);
    panel->getAllToolsRow()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools"));

    // An unknown tool is refused
    QVERIFY(!panel->activateTool(QStringLiteral("convert.epub")));
    QVERIFY(!panel->getToolRoute(QStringLiteral("convert.epub")).isValid());
}

void AppToolsPanelTest::panelEmptyStates()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);

    // No pins and no use (all of phase 1): the texts of the empty sections
    QVERIFY(panel->getPinnedShortcuts().empty());
    QVERIFY(panel->getRecentRows().empty());
    QVERIFY(panel->getPinnedEmptyLabel() && panel->getPinnedEmptyLabel()->isVisible());
    QVERIFY(panel->getRecentEmptyLabel() && panel->getRecentEmptyLabel()->isVisible());
    QVERIFY(panel->getPinnedEmptyLabel()->text().contains(QStringLiteral("Pin a tool")));
    QCOMPARE(panel->getRecentEmptyLabel()->text(), QStringLiteral("The tools you use appear here."));
    QVERIFY(panel->getAllToolsRow()->isVisible());
    QVERIFY(!panel->getNoResultsLabel());
    const int emptyHeight = H::panelRect(panel).height();
    QVERIFY2(emptyHeight < 523 && emptyHeight > 200, qPrintable(QString::number(emptyHeight)));
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-empty"));

    // A pin while the panel is open: the shortcut, the panel grows
    QVERIFY(H::pin(fixture, { QStringLiteral("pages.merge") }));
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), QStringList({ QStringLiteral("pages.merge") }));
    QVERIFY(!panel->getPinnedEmptyLabel());
    QVERIFY(H::panelRect(panel).height() > emptyHeight);
    QVERIFY(panel->getRecentEmptyLabel());
}

void AppToolsPanelTest::panelSearchToolsAndCommands()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    const int sectionsHeight = H::panelRect(panel).height();

    // A tool: the results replace the sections, the first result is current
    QTest::keyClicks(panel->getSearchField(), QStringLiteral("merge"));
    QVERIFY(panel->isSearching());
    QVERIFY(!panel->getResultRows().empty());
    QCOMPARE(panel->getResultRows().front()->getResult().key, QStringLiteral("pages.merge"));
    QVERIFY(panel->getResultRows().front()->isCurrent());
    QVERIFY(panel->getResultRows().size() <= size_t(2 * pdf::PDFAppDocumentToolsPanel::MAXIMUM_GROUP_RESULTS));
    QVERIFY(!panel->getPinnedShortcuts().front()->isVisible());
    QVERIFY(panel->getAllToolsRow()->isVisible());
    QVERIFY(H::panelRect(panel).height() != sectionsHeight);
    QVERIFY(H::isFocused(panel->getSearchField()));
    H::screenshot(panel, QStringLiteral("tools-panel-search"));

    // Enter opens the current tool as its shortcut does
    QTest::keyClick(panel->getSearchField(), Qt::Key_Return);
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), H::documentToolRoute(tab, QStringLiteral("pages.merge")));
    H::back(fixture, tab);

    // The panel opens with an empty search
    panel = H::open(tab);
    QVERIFY(panel);
    QVERIFY(panel->getSearchField()->text().isEmpty());
    QVERIFY(!panel->isSearching());
    QCOMPARE(H::panelRect(panel).height(), sectionsHeight);

    // A command runs on the view of the panel
    QTest::keyClicks(panel->getSearchField(), QStringLiteral("zoom in"));
    pdf::PDFAppSearchResultRow* zoomIn = panel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:") + pdf::PDFAppCommands::zoomIn());
    QVERIFY(zoomIn);
    QCOMPARE(zoomIn->getResult().group, pdf::PDFAppSearchGroup::Commands);
    QVERIFY(zoomIn->isEnabled());
    const double zoom = tab->getView()->getZoom();
    zoomIn->click();
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(tab->getView()->getZoom() > zoom);
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));

    // Down goes into the results, the focused result is current; Up back
    panel = H::open(tab);
    QTest::keyClicks(panel->getSearchField(), QStringLiteral("compress"));
    QVERIFY(panel->getResultRows().size() >= 1);
    QTest::keyClick(panel->getSearchField(), Qt::Key_Down);
    pdf::PDFAppSearchResultRow* focused = qobject_cast<pdf::PDFAppSearchResultRow*>(panel->focusWidget());
    QVERIFY(focused);
    QCOMPARE(focused, panel->getResultRows().front());
    QVERIFY(focused->isCurrent());
    if (panel->getResultRows().size() > 1)
    {
        QTest::keyClick(focused, Qt::Key_Down);
        QVERIFY(H::isFocused(panel->getResultRows().at(1)));
        QVERIFY(panel->getResultRows().at(1)->isCurrent());
        QVERIFY(!panel->getResultRows().front()->isCurrent());
        QTest::keyClick(panel->getResultRows().at(1), Qt::Key_Up);
    }
    QTest::keyClick(panel->getResultRows().front(), Qt::Key_Up);
    QVERIFY(H::isFocused(panel->getSearchField()));

    // No match: the message, Enter does nothing
    panel->getSearchField()->setText(QStringLiteral("xyzzyqw"));
    QVERIFY(panel->getResultRows().empty());
    QVERIFY(panel->getNoResultsLabel() && panel->getNoResultsLabel()->isVisible());
    QCOMPARE(panel->getNoResultsLabel()->text(), QString::fromUtf8("No tools or actions match “xyzzyqw”."));
    QTest::keyClick(panel->getSearchField(), Qt::Key_Return);
    QVERIFY(panel->isVisible());
    H::screenshot(panel, QStringLiteral("tools-panel-no-results"));

    // All tools takes the text to the catalog
    panel->getSearchField()->setText(QStringLiteral("merge"));

    // Esc clears the text first, then it closes the panel
    QTest::keyClick(panel->getSearchField(), Qt::Key_Escape);
    QVERIFY(panel->isVisible());
    QVERIFY(panel->getSearchField()->text().isEmpty());
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(panel->getPinnedShortcuts().front()->isVisible());
    QVERIFY(panel->getResultRows().empty());
    QCOMPARE(H::panelRect(panel).height(), sectionsHeight);
    QTest::keyClick(panel->getSearchField(), Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();

    panel = H::open(tab);
    panel->getSearchField()->setText(QStringLiteral("merge"));
    panel->getAllToolsRow()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools?q=merge"));
}

void AppToolsPanelTest::panelSearchHostileTexts()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);

    // 10 kB: the field keeps 256 characters
    panel->getSearchField()->setText(QString(10 * 1024, QLatin1Char('m')));
    QCOMPARE(panel->getSearchField()->text().size(), qsizetype(pdf::PDFAppDocumentToolsPanel::MAXIMUM_SEARCH_LENGTH));
    QVERIFY(panel->getNoResultsLabel());
    QVERIFY(panel->getNoResultsLabel()->text().size() < 200);

    // Bidirectional overrides and control characters are never shown as they are
    panel->getSearchField()->setText(QChar(0x202E) + QStringLiteral("qq") + QChar(0x0001) + QStringLiteral("zz") + QChar(0x200B));
    QVERIFY(panel->getNoResultsLabel());
    QVERIFY(!panel->getNoResultsLabel()->text().contains(QChar(0x202E)));
    QVERIFY(!panel->getNoResultsLabel()->text().contains(QChar(0x0001)));
    QCOMPARE(panel->getNoResultsLabel()->textFormat(), Qt::PlainText);

    // Rich text stays a text, white space only is no search
    panel->getSearchField()->setText(QStringLiteral("<b>merge</b>"));
    QCOMPARE(panel->getNoResultsLabel() ? panel->getNoResultsLabel()->textFormat() : Qt::PlainText, Qt::PlainText);
    panel->getSearchField()->setText(QStringLiteral("    "));
    QVERIFY(!panel->isSearching());
    QVERIFY(panel->getAllToolsRow()->isVisible());

    // Random texts: never more rows than the limits, the panel inside of the screen
    QRandomGenerator generator(20261026);
    const QRect available = panel->screen()->availableGeometry();
    for (int i = 0; i < 200; ++i)
    {
        QString text;
        const int length = int(generator.bounded(12));
        for (int j = 0; j < length; ++j)
        {
            text.append(QChar(char16_t(generator.bounded(0x20, 0x250))));
        }
        panel->getSearchField()->setText(text);
        QVERIFY(panel->getResultRows().size() <= size_t(2 * pdf::PDFAppDocumentToolsPanel::MAXIMUM_GROUP_RESULTS));
        QVERIFY(H::panelRect(panel).bottom() <= available.bottom());
    }
    QVERIFY(panel->isVisible());
}

void AppToolsPanelTest::panelNearestTools()
{
    // O25, tools.md 9.1 point 4: a search without a tool or an action offers up to
    // three nearest available tools below the text of no results
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    auto keys = [](const pdf::PDFAppDocumentToolsPanel* shown)
    {
        QStringList result;
        for (pdf::PDFAppSearchResultRow* row : shown->getNearestRows())
        {
            result << row->getResult().key;
        }
        return result;
    };

    QTest::keyClicks(panel->getSearchField(), QStringLiteral("tiemstmap"));
    QVERIFY(panel->getResultRows().empty());
    QVERIFY(panel->getNoResultsLabel() && panel->getNoResultsLabel()->isVisible());
    QVERIFY(panel->getNearestCaption()->isVisible());
    QCOMPARE(panel->getNearestCaption()->text(), QStringLiteral("Closest tools"));
    QCOMPARE(keys(panel), QStringList({ QStringLiteral("protect.timestamp") }));
    pdf::PDFAppSearchResultRow* row = panel->getNearestRows().front();
    QVERIFY(row->isVisible() && row->isEnabled());
    QCOMPARE(row->getResult().group, pdf::PDFAppSearchGroup::Tools);
    QVERIFY(!row->isCurrent());
    QVERIFY(H::panelRect(panel).contains(H::globalRect(row)));
    H::screenshot(panel, QStringLiteral("tools-panel-nearest"));

    // Enter of the field opens no guess; Down goes to the closest tool, Enter opens it
    QTest::keyClick(panel->getSearchField(), Qt::Key_Return);
    QVERIFY(panel->isVisible());
    QTest::keyClick(panel->getSearchField(), Qt::Key_Down);
    QVERIFY(H::isFocused(row));
    QTest::keyClick(row, Qt::Key_Up);
    QVERIFY(H::isFocused(panel->getSearchField()));

    // A query of more words: the order of the core; nothing close: no caption
    panel->getSearchField()->setText(QStringLiteral("compress scan quality"));
    QCOMPARE(keys(panel), QStringList({ "optimize.images", "convert.scan", "optimize.compress" }));
    panel->getSearchField()->setText(QStringLiteral("xyzzyqw"));
    QVERIFY(panel->getNearestRows().empty());
    QVERIFY(!panel->getNearestCaption()->isVisible());
    QVERIFY(panel->getNoResultsLabel() && panel->getNoResultsLabel()->isVisible());

    // A result hides them, Esc clears the search with them
    panel->getSearchField()->setText(QStringLiteral("merge"));
    QVERIFY(panel->getNearestRows().empty());
    QVERIFY(!panel->getNearestCaption()->isVisible());
    panel->getSearchField()->setText(QStringLiteral("converting"));
    QCOMPARE(panel->getNearestRows().size(), size_t(2));
    QTest::keyClick(panel->getSearchField(), Qt::Key_Escape);
    QVERIFY(panel->getNearestRows().empty());
    QVERIFY(panel->isVisible());

    // Both themes, right-to-left: the rows at the edges of the panel, no leak color
    for (const bool dark : { false, true })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        panel->getSearchField()->setText(QStringLiteral("converting"));
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(H::countLeakPixels(H::render(panel, dark ? 2.0 : 1.5)), 0);
        panel->getSearchField()->clear();
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // A click opens the tool on the document
    panel->getSearchField()->setText(QStringLiteral("tiemstmap"));
    panel->getNearestRows().front()->click();
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), H::documentToolRoute(tab, QStringLiteral("protect.timestamp")));
    H::back(fixture, tab);

    // A locked document: only the tools, which can run, are offered
    tab->getDocument()->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    panel = H::open(tab);
    QVERIFY(panel);
    QStringList unavailable;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        if (!panel->isToolAvailable(tool.getId()))
        {
            unavailable << tool.getId();
        }
    }
    QVERIFY(!unavailable.isEmpty());
    for (const QString& text : { QStringLiteral("converting"), QStringLiteral("compress scan quality"), QStringLiteral("spliting"), QStringLiteral("tiemstmap") })
    {
        panel->getSearchField()->setText(text);
        QStringList expected;
        for (const pdf::PDFDocumentToolSearchMatch& match : pdf::PDFDocumentToolSearch::searchNearest(text, unavailable))
        {
            expected << match.tool->getId();
        }
        QCOMPARE(keys(panel), expected);
        for (pdf::PDFAppSearchResultRow* nearest : panel->getNearestRows())
        {
            QVERIFY(nearest->isEnabled());
            QVERIFY(!unavailable.contains(nearest->getResult().key));
        }
    }
    QVERIFY(pdf::PDFDocumentToolSearch::searchNearest(QStringLiteral("converting")).size() > pdf::PDFDocumentToolSearch::searchNearest(QStringLiteral("converting"), unavailable).size());

    // The lock while the panel is open disables a closest tool, which cannot run then
    panel->getSearchField()->clear();
    tab->getDocument()->setLocked(false);
    pdftest::AppDocumentFixture::settle();
    panel->getSearchField()->setText(QStringLiteral("converting"));
    QCOMPARE(panel->getNearestRows().size(), size_t(2));
    tab->getDocument()->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    for (pdf::PDFAppSearchResultRow* nearest : panel->getNearestRows())
    {
        QCOMPARE(nearest->isEnabled(), panel->isToolAvailable(nearest->getResult().key));
        QCOMPARE(nearest->toolTip().isEmpty(), nearest->isEnabled());
    }
}

void AppToolsPanelTest::panelLockedDocument()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    QVERIFY(H::use(fixture, QStringLiteral("protect.redact"), QDateTime::currentMSecsSinceEpoch() - 1000));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QAction* lock = fixture.window->getCommandAction(pdf::PDFAppCommands::lockDocument());
    QVERIFY(lock);
    lock->trigger();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(tab->getDocument()->isLocked());

    // The Tools button stays (the catalog does not change the document)
    QToolButton* toolsButton = H::toolbar(tab)->getToolsButton();
    QVERIFY(toolsButton->isEnabled());
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);

    // tools.md 4.7: the tools, which change the document, are disabled with the reason
    auto checkLocked = [panel](bool isLocked)
    {
        std::vector<QAbstractButton*> buttons;
        for (pdf::PDFToolShortcut* shortcut : panel->getPinnedShortcuts())
        {
            buttons.push_back(shortcut);
        }
        for (pdf::PDFRecentToolRow* row : panel->getRecentRows())
        {
            buttons.push_back(row);
        }
        int disabled = 0;
        for (QAbstractButton* button : buttons)
        {
            const QString toolId = pdf::PDFAppToolPresentation::getToolId(button);
            const bool isUnavailable = pdf::PDFDocumentToolCatalog::findTool(toolId)->getLockedAccess() == pdf::PDFDocumentToolLockedAccess::Unavailable;
            const bool expectedEnabled = !(isLocked && isUnavailable);
            if (button->isEnabled() != expectedEnabled)
            {
                return false;
            }
            if (!expectedEnabled)
            {
                ++disabled;
                if (button->toolTip() != pdf::PDFAppDocumentToolsPanel::getLockedToolTip())
                {
                    return false;
                }
            }
        }
        return !isLocked || disabled > 0;
    };
    QVERIFY(checkLocked(true));
    QCOMPARE(pdf::PDFAppDocumentToolsPanel::getLockedToolTip(), QStringLiteral("Not available for a locked document. Unlock it or use Edit a Copy."));
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-locked"));

    // A disabled shortcut opens nothing, the panel refuses the tool
    pdf::PDFToolShortcut* locked = nullptr;
    for (pdf::PDFToolShortcut* shortcut : panel->getPinnedShortcuts())
    {
        if (!locked && !shortcut->isEnabled())
        {
            locked = shortcut;
        }
    }
    QVERIFY(locked);
    const QString lockedId = pdf::PDFAppToolPresentation::getToolId(locked);
    const pdf::PDFDocumentToolCatalogEntry* lockedTool = pdf::PDFDocumentToolCatalog::findTool(lockedId);
    QVERIFY(!panel->activateTool(lockedId));
    QTest::mouseClick(locked, Qt::LeftButton);
    QVERIFY(panel->isVisible());

    // A tool, which does not change the document, opens (Compare)
    QVERIFY(panel->isToolAvailable(QStringLiteral("review.compare")));

    // The results of the search too: the disabled result is never the current one
    panel->getSearchField()->setText(lockedTool->getTitle());
    pdf::PDFAppSearchResultRow* lockedResult = panel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:") + lockedId);
    QVERIFY(lockedResult);
    QVERIFY(!lockedResult->isEnabled());
    QCOMPARE(lockedResult->toolTip(), pdf::PDFAppDocumentToolsPanel::getLockedToolTip());
    QVERIFY(!lockedResult->isCurrent());
    lockedResult->click();
    QVERIFY(panel->isVisible());
    panel->getSearchField()->clear();

    // Unlocked while the panel is open: the tools come back
    tab->getDocument()->setLocked(false);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(checkLocked(false));
    QVERIFY(locked->isEnabled());
    QCOMPARE(locked->toolTip(), lockedTool->getDescription());
}

void AppToolsPanelTest::panelNoDocumentTools()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    const QStringList noDocument = { QStringLiteral("convert.images-to-pdf"), QStringLiteral("convert.blank"), QStringLiteral("protect.certificates") };
    for (const QString& toolId : noDocument)
    {
        QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(toolId)->getLockedAccess(), pdf::PDFDocumentToolLockedAccess::NoDocument);
    }
    QVERIFY(H::pin(fixture, noDocument));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    pdftest::AppDocumentFixture::settle();

    // A tool without a document: its page in the Home tab, also from a locked document
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    for (pdf::PDFToolShortcut* shortcut : panel->getPinnedShortcuts())
    {
        const QString toolId = pdf::PDFAppToolPresentation::getToolId(shortcut);
        QVERIFY(shortcut->isEnabled());
        QCOMPARE(panel->getToolRoute(toolId).toString(), QStringLiteral("/tools/") + toolId);
    }
    panel->getPinnedShortcuts().front()->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(fixture.window->getHomeTab()));
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/convert.images-to-pdf"));
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Images to PDF”"));

    // With the page of the tool (phase 2) its page
    QVERIFY(fixture.registerTestToolPage(QStringLiteral("convert.blank")));
    fixture.window->activateTab(tab);
    pdftest::AppDocumentFixture::settle();
    panel = H::open(tab);
    QVERIFY(panel);
    panel->getPinnedShortcuts().at(1)->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.page<pdftest::AppToolsTestPage>());
    QCOMPARE(fixture.page<pdftest::AppToolsTestPage>()->getToolId(), QStringLiteral("convert.blank"));
}

void AppToolsPanelTest::panelSwitchWithTools_data()
{
    QTest::addColumn<bool>("rightToLeft");
    QTest::newRow("ltr") << false;
    QTest::newRow("rtl") << true;
}

void AppToolsPanelTest::panelSwitchWithTools()
{
    using H = ToolsPanelTestHelper;
    QFETCH(bool, rightToLeft);
    QApplication::setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    const Qt::Key nextKey = rightToLeft ? Qt::Key_Left : Qt::Key_Right;
    const Qt::Key previousKey = rightToLeft ? Qt::Key_Right : Qt::Key_Left;

    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);
    QToolButton* toolsButton = toolbar->getToolsButton();
    QToolButton* goToButton = toolbar->getMenuButton(H::Panel::GoTo);

    // The order of the menu bar: … Go to, Tools, Select
    const std::vector<QToolButton*> buttons = toolbar->getPanelButtons();
    const auto toolsIt = std::find(buttons.cbegin(), buttons.cend(), toolsButton);
    QVERIFY(toolsIt != buttons.cend());
    QCOMPARE(*std::prev(toolsIt), goToButton);
    QCOMPARE(*std::next(toolsIt), toolbar->getSelectButton());

    // The pointer: from every panel to Tools and back, one checked button
    for (QToolButton* from : buttons)
    {
        if (from == toolsButton || from == toolbar->getSelectButton())
        {
            continue;
        }
        bool isOpen = false;
        for (H::Panel kind : { H::Panel::File, H::Panel::View, H::Panel::GoTo, H::Panel::Annotate, H::Panel::Insert })
        {
            if (toolbar->getMenuButton(kind) == from)
            {
                isOpen = toolbar->openPanel(kind) != nullptr;
            }
        }
        QVERIFY2(isOpen, qPrintable(from->objectName()));
        pdftest::AppDocumentFixture::settle();
        H::hover(toolsButton);
        QCOMPARE(toolbar->getOpenPanelButton(), toolsButton);
        QVERIFY(toolsButton->isChecked());
        QVERIFY(!from->isChecked());
        pdf::PDFAppDocumentToolsPanel* panel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(toolbar->getOpenPanel());
        QVERIFY(panel);
        QVERIFY(H::isFocused(panel->getSearchField()));
        QCOMPARE(QApplication::activePopupWidget(), panel);
        H::hover(from);
        QCOMPARE(toolbar->getOpenPanelButton(), from);
        QVERIFY(!toolsButton->isChecked());
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!panel->isVisible());
        toolbar->getOpenPanel()->close();
        pdftest::AppDocumentFixture::settle();
    }

    // Tools -> Select by the pointer
    QVERIFY(H::open(tab));
    H::hover(toolbar->getSelectButton());
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getSelectButton());
    toolbar->getSelectMenu()->hide();
    pdftest::AppDocumentFixture::settle();

    // The keyboard: the field keeps the arrows; a shortcut moves in its row and at
    // the edge of the row asks for the neighbor; the rows ask for the neighbor
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    QTest::keyClick(panel->getSearchField(), nextKey);
    QCOMPARE(toolbar->getOpenPanel(), panel);
    pdf::PDFToolShortcut* first = panel->getPinnedShortcuts().at(0);
    pdf::PDFToolShortcut* second = panel->getPinnedShortcuts().at(1);
    first->setFocus(Qt::TabFocusReason);
    QTest::keyClick(first, nextKey);
    QVERIFY(H::isFocused(second));
    QCOMPARE(toolbar->getOpenPanel(), panel);
    QTest::keyClick(second, nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getSelectButton());
    QVERIFY(toolbar->getSelectMenu()->isVisible());
    QTest::keyClick(toolbar->getSelectMenu(), previousKey);
    QCOMPARE(toolbar->getOpenPanel(), panel);
    panel->getAllToolsRow()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(panel->getAllToolsRow(), previousKey);
    QCOMPARE(toolbar->getOpenPanelButton(), goToButton);

    // Esc closes the panel with an empty search; a click on the button toggles it
    panel = H::open(tab);
    QTest::keyClick(panel->getSearchField(), Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!toolsButton->isChecked());
    toolsButton->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), panel);
    toolsButton->click();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!toolbar->getOpenPanel());

    // The panel at the leading edge of its button in both directions
    panel = H::open(tab);
    H::checkPlacement(H::panelRect(panel), toolsButton, toolbar);
}

void AppToolsPanelTest::panelFollowsOtherInstance()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, { QStringLiteral("pages.merge") }));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    QPointer<pdf::PDFToolShortcut> merge = panel->getPinnedShortcuts().front();
    const int height = H::panelRect(panel).height();

    // Another instance pins a tool: the shortcut without opening the panel again
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.split")));
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), QStringList({ QStringLiteral("pages.merge"), QStringLiteral("pages.split") }));
    QVERIFY(panel->isVisible());

    // Another instance uses a tool: the row, the shortcuts stay the same widgets
    merge = panel->getPinnedShortcuts().front();
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
    QCOMPARE(H::toolIds(panel->getRecentRows()), QStringList({ QStringLiteral("review.measure") }));
    QVERIFY(merge);
    QCOMPARE(panel->getPinnedShortcuts().front(), merge.data());
    QVERIFY(H::panelRect(panel).height() > height);

    // Clear of the history (Q2) in another instance: the empty state
    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).clearUsage(); }, pdf::PDFToolRepository::getUsageTopic()));
    QVERIFY(panel->getRecentRows().empty());
    QVERIFY(panel->getRecentEmptyLabel()->isVisible());

    // A search keeps its results while the pins change
    panel->getSearchField()->setText(QStringLiteral("split"));
    const QStringList keys = H::resultKeys(panel);
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.split"), false));
    QCOMPARE(H::resultKeys(panel), keys);
    QVERIFY(H::isFocused(panel->getSearchField()));
    panel->getSearchField()->clear();
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), QStringList({ QStringLiteral("pages.merge") }));

    // The focus stays at its tool over a refresh
    panel->getPinnedShortcuts().front()->setFocus(Qt::TabFocusReason);
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("review.compare")));
    QCOMPARE(panel->getPinnedShortcuts().size(), size_t(2));
    QVERIFY(H::isFocused(panel->getPinnedShortcuts().front()));

    // A recent row keeps the focus over a new use of another tool; a row, which
    // leaves the list, gives the focus to the search field
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.split"), QDateTime::currentMSecsSinceEpoch() - 60000));
    QCOMPARE(H::toolIds(panel->getRecentRows()), QStringList({ QStringLiteral("pages.split") }));
    panel->getRecentRows().front()->setFocus(Qt::TabFocusReason);
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
    QCOMPARE(H::toolIds(panel->getRecentRows()), QStringList({ QStringLiteral("review.measure"), QStringLiteral("pages.split") }));
    QVERIFY(H::isFocused(panel->getRecentRows().at(1)));
    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).clearUsage(); }, pdf::PDFToolRepository::getUsageTopic()));
    QVERIFY(H::isFocused(panel->getSearchField()));

    // A result row with the focus, the text changed by the application: the field
    panel->getSearchField()->setText(QStringLiteral("merge"));
    panel->getResultRows().front()->setFocus(Qt::TabFocusReason);
    panel->getSearchField()->setText(QStringLiteral("compare"));
    QVERIFY(H::isFocused(panel->getSearchField()));
    panel->getSearchField()->clear();

    // A closed panel does nothing at a notification, it reads the data at the opening
    panel->close();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("optimize.compress")));
    QCOMPARE(panel->getPinnedShortcuts().size(), size_t(2));
    QCOMPARE(H::open(tab), panel);
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), QStringList({ QStringLiteral("pages.merge"), QStringLiteral("review.compare"), QStringLiteral("optimize.compress") }));

    // Unknown changes (everythingChanged) read the data again
    pdf::PDFToolRepository(fixture.context->getDatabase()).setPinned(QStringLiteral("pages.merge"), false);
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::toolIds(panel->getPinnedShortcuts()), QStringList({ QStringLiteral("review.compare"), QStringLiteral("optimize.compress") }));
}

void AppToolsPanelTest::homeWhatsNewSupportFollowPins()
{
    // The pins and the use of the tools are one state: the Tools panel, Home (Recent
    // tools), What's New (stars of the new tools) and Support (For you) follow a
    // change of another instance
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);

    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(home);
    QVERIFY(home->getRecentToolRows().isEmpty());
    QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.merge"), QDateTime::currentMSecsSinceEpoch()));
    QTRY_COMPARE(home->getRecentToolRows().size(), 1);
    QCOMPARE(home->getRecentToolRows().front()->getName(), pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"))->getTitle());

    fixture.navigate(QStringLiteral("/support?topic=for-you"));
    pdf::PDFAppSupportPage* support = fixture.page<pdf::PDFAppSupportPage>();
    QVERIFY(support);
    QCOMPARE(support->getUsedToolIds().size(), 1);
    QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).clearUsage(); }, pdf::PDFToolRepository::getUsageTopic()));
    QTRY_COMPARE(support->getUsedToolIds().size(), 0);

    fixture.navigate(QStringLiteral("/whats-new"));
    pdf::PDFAppWhatsNewPage* whatsNew = fixture.page<pdf::PDFAppWhatsNewPage>();
    QVERIFY(whatsNew);
    const QList<pdf::PDFToolRow*> rows = whatsNew->getToolRows();
    if (!rows.isEmpty())
    {
        const QString toolId = pdf::PDFAppToolPresentation::getToolId(rows.front());
        const QString name = rows.front()->getName();
        QVERIFY(fixture.pinFromOtherConnection(toolId.isEmpty() ? QStringLiteral("pages.merge") : toolId));
        QTRY_VERIFY(whatsNew->getToolRows().front()->isPinned() || toolId.isEmpty());
        Q_UNUSED(name);
    }

    // The panel of the document shows the same state
    fixture.window->activateTab(tab);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("review.compare")));
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    QVERIFY(H::toolIds(panel->getPinnedShortcuts()).contains(QStringLiteral("review.compare")));
    QVERIFY(panel->getRecentRows().empty());
}

void AppToolsPanelTest::globalSearchToolRoute()
{
    // The global search opens a tool on its page /tools/{toolId} (Page Not Found
    // until phase 2), the panel of a document on the document
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);

    fixture.navigate(QStringLiteral("/search?q=merge"));
    pdf::PDFAppSearchPage* searchPage = fixture.page<pdf::PDFAppSearchPage>();
    QVERIFY(searchPage);
    const pdf::PDFAppSearchResult* result = searchPage->getResults().findResult(pdf::PDFAppSearchGroup::Tools, QStringLiteral("pages.merge"));
    QVERIFY(result);
    QCOMPARE(result->route.toString(), QStringLiteral("/tools/pages.merge"));
    const pdf::PDFAppSearchResult resultCopy = *result;
    searchPage->activateResult(resultCopy);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), QStringLiteral("/tools/pages.merge"));
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));

    // The same tool from the panel: on the document
    fixture.window->activateTab(tab);
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    panel->getSearchField()->setText(QStringLiteral("merge"));
    pdf::PDFAppSearchResultRow* row = panel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:pages.merge"));
    QVERIFY(row);
    QCOMPARE(row->getResult().route.toString(), QStringLiteral("/tools/pages.merge"));
    row->click();
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QCOMPARE(fixture.currentRoute(), H::documentToolRoute(tab, QStringLiteral("pages.merge")));
}

void AppToolsPanelTest::hostileStoredRows()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    const char* rows[] =
    {
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('security.encrypt', 1, 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES (x'00FF', 'x', 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('pages.merge', 'first', 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('pages.split', 9223372036854775807, 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('pages.crop', -5, 'yes')",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES (replace(hex(zeroblob(32768)), '0', 'a'), 2, 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('convert.ocr<b>', 3, 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('unknown.tool', 9, 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('optimize.compress', 'x', -4)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES (zeroblob(100), 8, 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('review.measure', 9223372036854775807, 9223372036854775807)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('protect.redact', -1, 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('edit.content', 1.5, 'many')",
    };
    for (const char* sql : rows)
    {
        QVERIFY2(fixture.writeFromOtherConnection([sql](pdf::PDFDatabase* database) { return database->getConnection()->execute(sql); },
                                                  QString::fromLatin1(sql).contains(QLatin1String("tool_pin")) ? pdf::PDFToolRepository::getPinTopic() : pdf::PDFToolRepository::getUsageTopic()), sql);
    }

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);

    // Only tools of the catalog, at most 6 and 3, every one once
    const QStringList pins = H::toolIds(panel->getPinnedShortcuts());
    const QStringList recent = H::toolIds(panel->getRecentRows());
    QVERIFY(pins.size() <= pdf::PDFAppDocumentToolsPanel::MAXIMUM_PINNED);
    QVERIFY(recent.size() <= pdf::PDFAppDocumentToolsPanel::MAXIMUM_RECENT);
    QVERIFY(pins.contains(QStringLiteral("pages.merge")));
    QVERIFY(pins.contains(QStringLiteral("pages.split")));
    QVERIFY(!recent.isEmpty());
    for (const QString& toolId : pins + recent)
    {
        QVERIFY2(pdf::PDFDocumentToolCatalog::isValidToolId(toolId), qPrintable(toolId));
    }
    QCOMPARE(QSet<QString>(pins.cbegin(), pins.cend()).size(), pins.size());
    for (pdf::PDFRecentToolRow* row : panel->getRecentRows())
    {
        QVERIFY(row->getLastUsed().size() < 64);
    }
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
}

void AppToolsPanelTest::deletionMatrix()
{
    using H = ToolsPanelTestHelper;

    // The tab deleted with the panel open and a search shown
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(H::pin(fixture, H::figmaPins()));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
        QVERIFY(panel);
        panel->getSearchField()->setText(QStringLiteral("merge"));
        delete tab;
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!panel);
    }

    // The window deleted with a refresh pending (a notification before the event loop)
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
        QVERIFY(panel);
        QVERIFY(!pdf::PDFToolRepository(fixture.context->getDatabase()).setPinned(QStringLiteral("pages.merge"), true).isError());
        Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFToolRepository::getPinTopic(), QStringLiteral("pages.merge"));
        delete fixture.window.data();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!panel);
    }

    // A tool activated, then the tab deleted before the navigation runs
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(H::pin(fixture, H::figmaPins()));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        panel->getPinnedShortcuts().front()->click();
        delete tab;
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.window);
        QVERIFY(fixture.currentTab());
    }

    // A tool activated, then the window deleted before the navigation runs
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(H::pin(fixture, H::figmaPins()));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        panel->getAllToolsRow()->click();
        delete fixture.window.data();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!fixture.window);
    }

    // A command result run, then the tab deleted before the command runs
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        panel->getSearchField()->setText(QStringLiteral("zoom in"));
        pdf::PDFAppSearchResultRow* zoomIn = panel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:") + pdf::PDFAppCommands::zoomIn());
        QVERIFY(zoomIn);
        zoomIn->click();
        delete tab;
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.window);
    }

    // The panel deleted by the application while it is open; the toolbar opens a new one
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        panel->getSearchField()->setText(QStringLiteral("sign"));
        delete panel;
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!toolbar->findPanel(H::Panel::Tools));
        QVERIFY(!toolbar->getToolsButton()->isChecked());
        panel = H::open(tab);
        QVERIFY(panel);
        QVERIFY(panel->getSearchField()->text().isEmpty());
    }

    // The tab deleted with the panel open: the dying toolbar deletes its panels
    // without unchecking the button - a slot of toggled would delete the tab again
    // (review RC6-1: the destroyed signal of the panel reached the toolbar)
    {
        pdftest::AppToolsFixture fixture;
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);
        QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
        QVERIFY(panel && panel->isVisible());
        QVERIFY(toolbar->getToolsButton()->isChecked());
        int toggledCount = 0;
        connect(toolbar->getToolsButton(), &QAbstractButton::toggled, fixture.window, [&tab, &toggledCount]() {
            ++toggledCount;
            delete tab.data();
        });
        delete tab.data();
        QCOMPARE(toggledCount, 0);
        QVERIFY(!panel);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.window);
    }

    // The panel closed with a refresh pending: the refresh is dropped, the opening reads
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        QVERIFY(!pdf::PDFToolRepository(fixture.context->getDatabase()).setPinned(QStringLiteral("pages.merge"), true).isError());
        Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFToolRepository::getPinTopic(), QStringLiteral("pages.merge"));
        panel->close();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(panel->getPinnedShortcuts().empty());
        QCOMPARE(H::open(tab), panel);
        QCOMPARE(panel->getPinnedShortcuts().size(), size_t(1));
    }
}

void AppToolsPanelTest::deleteFromOwnSignals()
{
    using H = ToolsPanelTestHelper;

    // The tab deleted from the click of a shortcut (after the slot of the panel)
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(H::pin(fixture, H::figmaPins()));
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        connect(panel->getPinnedShortcuts().front(), &QAbstractButton::clicked, fixture.window, [&tab]() { delete tab.data(); });
        panel->getPinnedShortcuts().front()->click();
        QVERIFY(!tab);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(fixture.window);
    }

    // The tab deleted from the closing signal of the panel opened by a recent row
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(H::use(fixture, QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        connect(panel, &pdf::PDFAppDocumentPanelPopup::closed, fixture.window, [&tab]() { delete tab.data(); });
        panel->getRecentRows().front()->click();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!tab);
        QVERIFY(fixture.window);
    }

    // The window deleted from the click of a result (the queued navigation never runs)
    {
        pdftest::AppToolsFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
        QVERIFY(panel);
        panel->getSearchField()->setText(QStringLiteral("merge"));
        QVERIFY(!panel->getResultRows().empty());
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        connect(panel->getResultRows().front(), &QAbstractButton::clicked, fixture.context.get(), [window]() { delete window.data(); });
        panel->getResultRows().front()->click();
        QVERIFY(!window);
        QVERIFY(!panel);
        pdftest::AppDocumentFixture::settle();
    }

    // The tab deleted from the notification, which refreshes the open panel
    {
        pdftest::AppToolsFixture fixture;
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
        QVERIFY(panel);
        connect(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed, fixture.window, [&tab]() { delete tab.data(); });
        QVERIFY(fixture.pinFromOtherConnection(QStringLiteral("pages.merge")));
        QVERIFY(!tab);
        QVERIFY(!panel);
    }

    // The tab deleted from the closing of the panel after a command result (the
    // command queued on the view never runs)
    {
        pdftest::AppToolsFixture fixture;
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
        QVERIFY(panel);
        panel->getSearchField()->setText(QStringLiteral("zoom in"));
        pdf::PDFAppSearchResultRow* zoomIn = panel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:") + pdf::PDFAppCommands::zoomIn());
        QVERIFY(zoomIn);
        connect(panel, &pdf::PDFAppDocumentPanelPopup::closed, fixture.window, [&tab]() { delete tab.data(); });
        zoomIn->click();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!tab);
        QVERIFY(fixture.window);
    }
}

void AppToolsPanelTest::themesAndScales_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<qreal>("ratio");
    QTest::newRow("light 1x") << false << 1.0;
    QTest::newRow("light 1.5x") << false << 1.5;
    QTest::newRow("light 2x") << false << 2.0;
    QTest::newRow("dark 1x") << true << 1.0;
    QTest::newRow("dark 1.5x") << true << 1.5;
    QTest::newRow("dark 2x") << true << 2.0;
}

void AppToolsPanelTest::themesAndScales()
{
    using H = ToolsPanelTestHelper;
    QFETCH(bool, dark);
    QFETCH(qreal, ratio);

    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(H::use(fixture, QStringLiteral("edit.content"), now - 3 * 86400000LL));
    QVERIFY(H::use(fixture, QStringLiteral("protect.redact"), now - 86400000LL));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), now - 120000));
    pdf::PDFThemeManager::getInstance()->setScheme(dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    pdftest::AppDocumentFixture::settle();

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    const QString suffix = QStringLiteral("%1-%2x").arg(dark ? QStringLiteral("dark") : QStringLiteral("light")).arg(ratio);

    // The sections, a search and a locked document: no violet pixel, the tokens of the theme
    QImage image = H::render(panel, ratio);
    QCOMPARE(H::countLeakPixels(image), 0);
    const QPoint inside = panel->getPanelRect().topLeft() + QPoint(8, 8);
    QVERIFY(H::isClose(image.pixelColor(QPoint(qRound(inside.x() * ratio), qRound(inside.y() * ratio))),
                       pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface)));
    H::screenshot(panel, QStringLiteral("tools-panel-") + suffix, ratio);

    panel->getSearchField()->setText(QStringLiteral("sign"));
    image = H::render(panel, ratio);
    QCOMPARE(H::countLeakPixels(image), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-search-") + suffix, ratio);

    panel->getSearchField()->clear();
    tab->getDocument()->setLocked(true);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::countLeakPixels(H::render(panel, ratio)), 0);

    // A change of the theme while the panel is open
    pdf::PDFThemeManager::getInstance()->setScheme(dark ? pdf::PDFThemeColors::Scheme::Light : pdf::PDFThemeColors::Scheme::Dark);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::countLeakPixels(H::render(panel, ratio)), 0);
}

void AppToolsPanelTest::rightToLeft()
{
    using H = ToolsPanelTestHelper;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, H::figmaPins()));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    QCOMPARE(panel->layoutDirection(), Qt::RightToLeft);

    // The trailing edge of the panel at the trailing edge of the button, the grid mirrored
    const QRect panelRect = H::panelRect(panel);
    H::checkPlacement(panelRect, H::toolbar(tab)->getToolsButton(), H::toolbar(tab));
    const QRect first = H::globalRect(panel->getPinnedShortcuts().at(0));
    const QRect second = H::globalRect(panel->getPinnedShortcuts().at(1));
    QCOMPARE(first.right(), panelRect.right() - 17);
    QCOMPARE(first.left() - second.right() - 1, 8);
    QCOMPARE(H::globalRect(panel->getSearchField()).right(), panelRect.right() - 17);
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-rtl"));

    panel->getSearchField()->setText(QStringLiteral("merge"));
    QCOMPARE(H::globalRect(panel->getResultRows().front()).right(), panelRect.right() - 17);
    H::screenshot(panel, QStringLiteral("tools-panel-search-rtl"));

    // The closest tools of a search without a result (O25) as the results
    panel->getSearchField()->setText(QStringLiteral("converting"));
    QVERIFY(!panel->getNearestRows().empty());
    QCOMPARE(H::globalRect(panel->getNearestRows().front()).right(), panelRect.right() - 17);
    QCOMPARE(panel->getNearestRows().front()->layoutDirection(), Qt::RightToLeft);
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-nearest-rtl"));
}

void AppToolsPanelTest::minimalWindow()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture(QSize(720, 480));
    QVERIFY(H::pin(fixture, H::figmaPins()));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(H::use(fixture, QStringLiteral("edit.content"), now - 3000));
    QVERIFY(H::use(fixture, QStringLiteral("protect.redact"), now - 2000));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), now - 1000));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);

    // The narrow toolbar keeps the Tools button (an icon)
    QToolButton* toolsButton = toolbar->getToolsButton();
    QVERIFY(toolsButton->isVisible() && toolsButton->isEnabled());
    QVERIFY(toolbar->getCollapseLevel() > 0);
    QCOMPARE(toolsButton->toolButtonStyle(), Qt::ToolButtonIconOnly);

    // The panel stays inside of the screen, also with a search
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    const QRect available = panel->screen()->availableGeometry();
    QVERIFY(available.contains(H::panelRect(panel)));
    QCOMPARE(H::panelRect(panel).width(), 400);
    panel->getSearchField()->setText(QStringLiteral("page"));
    QVERIFY(available.contains(H::panelRect(panel)));
    QCOMPARE(H::countLeakPixels(H::render(panel, 1.0)), 0);
    H::screenshot(panel, QStringLiteral("tools-panel-720x480"));
    fixture.screenshot(QStringLiteral("tools-panel-window-720x480"));
}

void AppToolsPanelTest::keyboardAndAccessibility()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, { QStringLiteral("pages.merge"), QStringLiteral("review.compare") }));
    QVERIFY(H::use(fixture, QStringLiteral("review.measure"), QDateTime::currentMSecsSinceEpoch()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);

    // Names: the field, the shortcuts (title + description), the rows, the link
    QCOMPARE(panel->getSearchField()->accessibleName(), QStringLiteral("Search tools and actions"));
    pdf::PDFToolShortcut* merge = panel->getPinnedShortcuts().front();
    QCOMPARE(merge->text(), QStringLiteral("Merge"));
    QCOMPARE(merge->accessibleDescription(), pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"))->getDescription());
    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(merge);
    QVERIFY(interface);
    QCOMPARE(interface->text(QAccessible::Name), QStringLiteral("Merge"));

    // Down / Tab in the order of the panel: field, shortcuts, recent, All tools
    QVERIFY(H::isFocused(panel->getSearchField()));
    QTest::keyClick(panel->getSearchField(), Qt::Key_Down);
    QVERIFY(H::isFocused(merge));
    QTest::keyClick(merge, Qt::Key_Tab);
    QVERIFY(H::isFocused(panel->getPinnedShortcuts().at(1)));
    QTest::keyClick(panel->getPinnedShortcuts().at(1), Qt::Key_Down);
    QVERIFY(H::isFocused(panel->getRecentRows().front()));
    QTest::keyClick(panel->getRecentRows().front(), Qt::Key_Down);
    QVERIFY(H::isFocused(panel->getAllToolsRow()));
    QTest::keyClick(panel->getAllToolsRow(), Qt::Key_Up);
    QVERIFY(H::isFocused(panel->getRecentRows().front()));

    // Enter opens the focused tool
    QTest::keyClick(panel->getRecentRows().front(), Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.currentRoute(), H::documentToolRoute(tab, QStringLiteral("review.measure")));
}

void AppToolsPanelTest::languageChange()
{
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, { QStringLiteral("pages.merge") }));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolsPanel* panel = H::open(tab);
    QVERIFY(panel);
    panel->getSearchField()->setText(QStringLiteral("merge"));
    const size_t resultCount = panel->getResultRows().size();

    ToolsPanelTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(panel->getSearchField()->placeholderText(), QString::fromUtf8("[Search tools and actions…]"));
    QCOMPARE(panel->getAllToolsRow()->getTitle(), QStringLiteral("[All tools]"));
    QVERIFY(panel->isVisible());
    QCOMPARE(panel->getSearchField()->text(), QStringLiteral("merge"));
    QCOMPARE(panel->getResultRows().size(), resultCount);
    QCOMPARE(panel->accessibleName(), QStringLiteral("[Tools]"));

    QCoreApplication::removeTranslator(&translator);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(panel->getAllToolsRow()->getTitle(), QStringLiteral("All tools"));
    panel->getSearchField()->clear();
    QCOMPARE(panel->getPinnedShortcuts().front()->getName(), QStringLiteral("Merge"));
}

void AppToolsPanelTest::panelRecentTimesFollowClock()
{
    // O25: the recent tools of the panel show "5 minutes ago" and follow the clock by
    // the minute ticker of the context while the panel is open - the same rows, no
    // read of the database; a closed panel stops the ticker, a reopened one shows the
    // current texts; the tab deleted from the tick
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    const qint64 now = QDateTime(QDate(2026, 10, 26), QTime(15, 56), QTimeZone::LocalTime).toMSecsSinceEpoch();
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now);
    QVERIFY(H::use(fixture, QStringLiteral("pages.merge"), now - 5 * 60000));
    QVERIFY(H::use(fixture, QStringLiteral("optimize.compress"), now - 20000));

    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppMinuteTicker* ticker = fixture.context->getMinuteTicker();
    QVERIFY(ticker);

    // The document shows no time of recent activity: the ticker does not run
    QTRY_VERIFY(!ticker->isRunning());

    QPointer<pdf::PDFAppDocumentToolsPanel> panel = H::open(tab);
    QVERIFY(panel && panel->isVisible());
    QVERIFY(panel->getMinuteSubscription());
    QTRY_VERIFY(ticker->isRunning());
    const std::vector<pdf::PDFRecentToolRow*> rows = panel->getRecentRows();
    QCOMPARE(rows.size(), size_t(2));
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("Just now"));
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("5 minutes ago"));
    const int reads = panel->getReadCount();

    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 60000);
    ticker->tick();
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("1 minute ago"));
    QCOMPARE(rows[1]->getLastUsed(), QStringLiteral("6 minutes ago"));
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 2 * 60000);
    ticker->tick();
    QCOMPARE(rows[0]->getLastUsed(), QStringLiteral("2 minutes ago"));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(panel->getRecentRows(), rows);
    QCOMPARE(panel->getReadCount(), reads);

    // Closed: the ticker stops; reopened: the current texts
    panel->close();
    pdftest::AppDocumentFixture::settle();
    QTRY_VERIFY(!ticker->isRunning());
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 10 * 60000);
    ticker->tick();
    panel = H::open(tab);
    QVERIFY(panel && panel->isVisible());
    QCOMPARE(panel->getRecentRows().size(), size_t(2));
    QCOMPARE(panel->getRecentRows()[1]->getLastUsed(), QStringLiteral("15 minutes ago"));
    QTRY_VERIFY(ticker->isRunning());

    // The tab deleted from the tick of the panel
    QPointer<pdf::PDFAppDocumentTab> tabGuard = tab;
    QObject::connect(panel->getMinuteSubscription(), &pdf::PDFAppMinuteSubscription::minuteChanged, panel.data(), [tabGuard]() { delete tabGuard.data(); });
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(now + 11 * 60000);
    ticker->tick();
    QVERIFY(!tab);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!panel);
    QTRY_VERIFY(!ticker->isRunning() || ticker->getActiveSubscriptionCount() > 0);
    pdf::PDFAppHomeFormat::setCurrentTimeOverride(0);
}

void AppToolsPanelTest::panelReopenFocus()
{
    // Review RSP6 / RC6-1: a panel opened again starts at its first control - the search
    // field -, not at the control after the one focused before it was closed
    // (PDFAppDocumentPanelPopup::popup): by the mouse, by the keyboard and by the
    // switch between the panels
    using H = ToolsPanelTestHelper;
    pdftest::AppToolsFixture fixture;
    QVERIFY(H::pin(fixture, { QStringLiteral("pages.merge"), QStringLiteral("review.compare") }));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = H::toolbar(tab);
    QVERIFY(toolbar);
    QToolButton* button = toolbar->getToolsButton();
    QToolButton* viewButton = toolbar->getMenuButton(H::Panel::View);
    QVERIFY(button && viewButton);

    auto clickButton = [](QToolButton* target)
    {
        QWidget* window = target->window();
        QTest::mouseClick(window->windowHandle(), Qt::LeftButton, Qt::NoModifier, window->mapFromGlobal(target->mapToGlobal(target->rect().center())));
        pdftest::AppDocumentFixture::settle();
    };
    // The focus moves from the field to the second pinned tool
    auto moveFocus = [](pdf::PDFAppDocumentToolsPanel* panel)
    {
        QTest::keyClick(panel->getSearchField(), Qt::Key_Down);
        QTest::keyClick(panel->getPinnedShortcuts().front(), Qt::Key_Tab);
        return H::isFocused(panel->getPinnedShortcuts().at(1));
    };
    auto closeByEscape = [](pdf::PDFAppDocumentToolsPanel* panel)
    {
        QTest::keyClick(panel, Qt::Key_Escape);
        pdftest::AppDocumentFixture::settle();
        return !panel->isVisible();
    };

    // The mouse: the first opening, opened again after Esc and after a click on its button
    clickButton(button);
    QPointer<pdf::PDFAppDocumentToolsPanel> panel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(toolbar->getOpenPanel());
    QVERIFY(panel);
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(moveFocus(panel));
    QVERIFY(closeByEscape(panel));
    clickButton(button);
    QCOMPARE(toolbar->getOpenPanel(), panel.data());
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(moveFocus(panel));
    clickButton(button);
    QTRY_VERIFY(!panel->isVisible());
    clickButton(button);
    QCOMPARE(toolbar->getOpenPanel(), panel.data());
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(moveFocus(panel));
    QVERIFY(closeByEscape(panel));

    // The keyboard: Space on the button of the toolbar
    button->setFocus(Qt::TabFocusReason);
    QTest::keyClick(button, Qt::Key_Space);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), panel.data());
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(moveFocus(panel));

    // The switch between the panels (the pointer over a button, Left / Right)
    QVERIFY(toolbar->switchPanel(viewButton, true));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!panel->isVisible());
    QVERIFY(toolbar->switchPanel(button, true));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), panel.data());
    QVERIFY(H::isFocused(panel->getSearchField()));
    QVERIFY(closeByEscape(panel));
}

QTEST_MAIN(AppToolsPanelTest)

#include "tst_apptoolspaneltest.moc"
