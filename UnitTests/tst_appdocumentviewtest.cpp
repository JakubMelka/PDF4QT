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

// Package B of step 3: the document view - the toolbar (193:785), the panels File
// (196:2583), View (196:2749), Go to (196:3437), the Select menu (198:5517), the
// status bar (193:1319), the info bars (1028:24277, 1030:25657), the canvas and its
// commands, the settings of the view, full screen, magnifier and the view state.
// PDF4QT_APP_SCREENSHOTS=<folder> writes pictures of the view and of the panels
// (both themes, 1x and 2x, the minimal window, right-to-left).

#include "appdocumentfixture.h"

#include "pdfappannotationcontroller.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumenttoolspanel.h"
#include "pdfappdocumentview.h"
#include "pdfappnavigator.h"
#include "pdfappsearchengine.h"
#include "pdfappsendmail.h"
#include "pdfapptopbar.h"
#include "pdfappviewstate.h"
#include "pdfsettingdefinitions.h"

#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemecolors.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <cmath>
#include <limits>

/// Helpers of the tests of the view
class ViewTestHelper
{
public:
    ViewTestHelper() = delete;

    /// Opens the file and waits for the shown document (package A reads it in a worker thread)
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath, int page = 0)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath, page);
        if (!tab)
        {
            return nullptr;
        }

        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        const bool isReady = QTest::qWaitFor([&guard]()
        {
            return guard && guard->getDocument() && guard->getDocument()->isReady() && guard->getView() && guard->getView()->getPageCount() > 0;
        }, 20000);
        fixture.settle();
        return isReady ? guard.data() : nullptr;
    }

    static void trigger(pdftest::AppDocumentFixture& fixture, const QString& commandId)
    {
        QAction* action = fixture.window->getCommandAction(commandId);
        QVERIFY(action);
        action->trigger();
        fixture.settle();
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
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

    /// Color of the pixel of the widget at the logical point
    static QColor pixel(const QImage& image, QPoint point)
    {
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(QPoint(qRound(point.x() * ratio), qRound(point.y() * ratio)));
    }

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFThemeManager::getInstance()->getColor(color);
    }

    /// Picture of the window with the open popups drawn at their places
    static QImage renderWindowWithPopups(QWidget* window, const QList<QWidget*>& popups, qreal ratio)
    {
        QImage image = render(window, ratio);
        QPainter painter(&image);
        for (QWidget* popup : popups)
        {
            if (popup && popup->isVisible())
            {
                const QImage popupImage = render(popup, ratio);
                painter.drawImage(QPointF(popup->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0))), popupImage);
            }
        }
        return image;
    }

    static void save(const QString& folder, const QString& name, const QImage& image)
    {
        if (!folder.isEmpty())
        {
            QDir().mkpath(folder);
            image.save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
        }
    }
};

/// Helpers of the switch of the panels of the toolbar (menu bar behavior)
class PanelSwitchHelper
{
public:
    PanelSwitchHelper() = delete;

    using Panel = pdf::PDFAppDocumentToolbar::Panel;

    /// Moves the pointer over \p widget (its center) through the window system:
    /// an open panel grabs the mouse, the window of the toolbar gives it the move
    static void hover(QWidget* widget)
    {
        QWidget* window = widget->window();
        QTest::mouseMove(window->windowHandle(), window->mapFromGlobal(widget->mapToGlobal(widget->rect().center())));
    }

    /// Clicks \p widget through the window system (an open panel takes the press)
    static void click(QWidget* widget)
    {
        QWidget* window = widget->window();
        QTest::mouseClick(window->windowHandle(), Qt::LeftButton, Qt::NoModifier, window->mapFromGlobal(widget->mapToGlobal(widget->rect().center())));
    }

    /// Opens the panel of \p button (the Select menu by the switch from File, so
    /// that QToolButton::showMenu does not block)
    static bool open(pdf::PDFAppDocumentToolbar* toolbar, QToolButton* button)
    {
        if (button == toolbar->getSelectButton())
        {
            return toolbar->openPanel(Panel::File) && toolbar->switchPanel(button, false);
        }

        for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
        {
            if (toolbar->getMenuButton(panel) == button)
            {
                return toolbar->openPanel(panel) != nullptr;
            }
        }
        return false;
    }

    /// Window of the open panel (the Select menu or a panel)
    static QWidget* openWindow(pdf::PDFAppDocumentToolbar* toolbar)
    {
        if (toolbar->getSelectMenu() && toolbar->getSelectMenu()->isVisible())
        {
            return toolbar->getSelectMenu();
        }
        return toolbar->getOpenPanel();
    }

    /// Number of the open panels and menus of the toolbar
    static int countOpen(pdf::PDFAppDocumentToolbar* toolbar)
    {
        int count = toolbar->getSelectMenu() && toolbar->getSelectMenu()->isVisible() ? 1 : 0;
        for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
        {
            if (toolbar->findPanel(panel) && toolbar->findPanel(panel)->isVisible())
            {
                ++count;
            }
        }
        return count;
    }

    static void closeAll(pdf::PDFAppDocumentToolbar* toolbar)
    {
        if (toolbar->getSelectMenu() && toolbar->getSelectMenu()->isVisible())
        {
            toolbar->getSelectMenu()->hide();
        }
        if (pdf::PDFAppDocumentPanelPopup* panel = toolbar->getOpenPanel())
        {
            panel->close();
        }
    }

    /// Focuses \p widget of an open panel (as the keyboard does)
    static bool focus(QWidget* widget)
    {
        if (!widget)
        {
            return false;
        }
        widget->setFocus(Qt::TabFocusReason);
        return widget->hasFocus();
    }
};

/// Package B of step 3 (view, toolbar, panels, status bar, info bars)
class AppDocumentViewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void parts();
    void toolbarGeometry();
    void viewColors();
    void pageDecoration();
    void splitterHandle();
    void routePage();
    void navigationCommands();
    void canvasShortcuts();
    void canvasShortcutScope();
    void fitShortcuts();
    void toolManager();
    void textSelectionCommands();
    void textSelectionThemeColor();
    void selectMenuTools();
    void handMode();
    void cursorFollowsState();
    void magnifier();
    void layoutCommands();
    void zoomCommands();
    void zoomFieldHostile();
    void pageFieldAndLabels();
    void findPageIndexHostile();
    void colorModesAndAnnotations();
    void settingsApplied();
    void sidebarToggle();
    void fullScreen();
    void fullScreenTabSwitch();
    void fullScreenTabClosed();
    void statusBar();
    void infoBars();
    void infoBarsDeletion();
    void viewStateText();
    void viewStateApply();
    void filePanel();
    void filePanelWithoutMail();
    void filePanelCloseDocument();
    void viewPanel();
    void goToPanel();
    void goToPanelAutoGenerate();
    void closedPanelsNotRefreshed();
    void panelKeyboard();
    void panelDeletion();
    void panelHoverSwitch();
    void panelHoverSwitchSelectMenuExec();
    void panelArrowSwitch_data();
    void panelArrowSwitch();
    void panelSwitchDisabled();
    void panelSwitchDeletion();
    void requestedCommandAfterClose();
    void narrowToolbar();
    void rightToLeft();
    void themes();
    void accessibility();
    void languageChange();
    void largeDocument();
    void toolbarFieldsAndToolTips();
    void popoverStyleFunctions();
    void screenshots();
    void viewDestroyedSilently();
    void panelReopenFocus();
};

void AppDocumentViewTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppDocumentViewTest::parts()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Parts.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(view);

    // Figma 193:756: toolbar 52, sidebar 300, status bar 28
    QCOMPARE(view->getToolbar()->height(), pdf::PDFAppDocumentToolbar::HEIGHT);
    QCOMPARE(view->getStatusBar()->height(), pdf::PDFAppDocumentStatusBar::HEIGHT);
    QCOMPARE(view->getSidebar()->width(), pdf::PDFAppDocumentSidebar::DEFAULT_WIDTH);
    QVERIFY(view->getPdfWidget()->isVisible());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    QCOMPARE(view->getPageCount(), pdf::PDFInteger(3));
    QCOMPARE(view->getTab(), tab);

    // The search field of package D (object name kept for D)
    pdf::PDFLineEdit* searchField = view->getToolbar()->getSearchField();
    QVERIFY(searchField);
    QCOMPARE(searchField->objectName(), QStringLiteral("documentSearchField"));
    QCOMPARE(view->getToolbar()->findChild<pdf::PDFLineEdit*>(QStringLiteral("documentSearchField")), searchField);
    QCOMPARE(searchField->width(), pdf::PDFAppDocumentToolbar::SEARCH_FIELD_WIDTH);
    QCOMPARE(searchField->height(), 36);
    QCOMPARE(searchField->placeholderText(), QStringLiteral("Search in document…"));

    // Hand is the mode of a new document (no tool, owner 2026-10-25); the status bar says Ready
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
    QCOMPARE(view->getStatusBar()->getMessage(), QStringLiteral("Ready"));
    QVERIFY(view->getStatusBar()->getPageText().isEmpty());
    QVERIFY(!view->getStatusBar()->getPageItem()->isVisible());
}

void AppDocumentViewTest::toolbarGeometry()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Geometry.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QCOMPARE(toolbar->getCollapseLevel(), 0);

    // toolbar-left 193:786 at x 12; tb-sidebar 36 x 36; menu-file at x 194 (70 wide)
    QToolButton* sidebarButton = toolbar->getCommandButton(pdf::PDFAppCommands::toggleSidebar());
    QVERIFY(sidebarButton);
    QCOMPARE(sidebarButton->size(), QSize(36, 36));
    QCOMPARE(sidebarButton->mapTo(toolbar, QPoint(0, 0)), QPoint(12, 8));
    QToolButton* fileButton = toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File);
    QCOMPARE(fileButton->mapTo(toolbar, QPoint(0, 0)).x(), 194);
    QCOMPARE(fileButton->height(), 36);
    QVERIFY(qAbs(fileButton->width() - 70) <= 2);
    QVERIFY(qAbs(toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::View)->width() - 79) <= 2);

    // toolbar-right: the search field ends 12 px before the edge, the Select button is 50 wide
    pdf::PDFLineEdit* searchField = toolbar->getSearchField();
    QCOMPARE(searchField->mapTo(toolbar, QPoint(searchField->width(), 0)).x(), toolbar->width() - 12);
    QCOMPARE(toolbar->getSelectButton()->width(), 50);
    QCOMPARE(searchField->mapTo(toolbar, QPoint(0, 0)).x() - 8, toolbar->getSelectButton()->mapTo(toolbar, QPoint(toolbar->getSelectButton()->width(), 0)).x());

    // The page field 32 high with the suffix "/ 3", the zoom field without a suffix
    QCOMPARE(toolbar->getPageField()->height(), 32);
    QCOMPARE(toolbar->getPageField()->text(), QStringLiteral("1"));
    QCOMPARE(toolbar->getPageField()->getSuffix(), QStringLiteral("/ 3"));
    QVERIFY(!toolbar->getZoomField()->isSuffixVisible());
    QVERIFY(toolbar->getZoomField()->text().endsWith(QStringLiteral(" %")));

    // Annotate and Insert open the panels of step 4 (an unlocked document)
    QVERIFY(toolbar->getAnnotateButton()->isEnabled());
    QVERIFY(toolbar->getInsertButton()->isEnabled());
    QVERIFY(toolbar->getToolsButton()->isEnabled());
}

void AppDocumentViewTest::viewColors()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Colors.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();

    // The canvas has the color of the theme (document-canvas 193:1256 = bg/track, the
    // white page must be seen on it - not bg/canvas, which is white), never the violet
    // leak color
    pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
    QCOMPARE(proxy->getViewColors().canvas, manager->getColor(pdf::PDFThemeColors::BgTrack));
    QVERIFY(proxy->getViewColors().canvas != manager->getColor(pdf::PDFThemeColors::ReadingPaperPage));
    QVERIFY(proxy->getViewColors().canvas != pdf::PDFThemeColors::getLeakColor());
    QVERIFY(proxy->getViewColors().selection != pdf::PDFThemeColors::getLeakColor());

    manager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(proxy->getViewColors().canvas, manager->getColor(pdf::PDFThemeColors::BgTrack));
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(proxy->getViewColors().canvas, manager->getColor(pdf::PDFThemeColors::BgTrack));

    // The canvas around a page drawn by the widget (zoomed out, the page is smaller than the canvas)
    tab->getView()->setZoom(0.1);
    fixture.settle();
    pdf::PDFWidget* pdfWidget = tab->getView()->getPdfWidget();
    const QImage canvas = ViewTestHelper::render(pdfWidget);
    QCOMPARE(canvas.pixelColor(2, canvas.height() - 3), ViewTestHelper::token(pdf::PDFThemeColors::BgTrack));

    // Without colors of the application the proxy keeps the defaults of the old applications
    proxy->resetViewColors();
    QCOMPARE(proxy->getViewColors(), pdf::PDFViewColors::getDefaultColors());
}

void AppDocumentViewTest::pageDecoration()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Decoration.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFDrawWidgetProxy* proxy = view->getProxy();
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    pdf::PDFToolBarField* zoomField = view->getToolbar()->getZoomField();
    pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();

    // O18, page 193:1257 on the canvas 193:1256: 24 px of the canvas around the pages,
    // the shadow 0 / 2 / 10 of effect/shadow
    QCOMPARE(proxy->getPageDecoration().margin, 24);
    QCOMPARE(proxy->getPageDecoration().shadowOffset, QPointF(0.0, 2.0));
    QCOMPARE(proxy->getPageDecoration().shadowBlur, 10.0);
    QCOMPARE(proxy->getPageDecoration().shadowColor, manager->getColor(pdf::PDFThemeColors::EffectShadow));

    auto firstPageRect = [proxy]() -> QRect
    {
        const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
        for (const pdf::PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
        {
            if (item.pageIndex == 0)
            {
                return item.rect.toRect();
            }
        }
        return QRect();
    };
    auto zoomText = [view]()
    {
        return QString::number(qRound(view->getZoom() * 100.0)) + QStringLiteral(" %");
    };

    // Fit width (the default): the page lies 24 px below the top of the canvas and it is
    // centered with 24 px of the canvas at both sides; the zoom field shows the zoom
    QCOMPARE(proxy->getZoomFitHint(), std::make_optional(pdf::PDFDrawWidgetProxy::ZoomHint::FitWidth));
    QRect page = firstPageRect();
    QVERIFY(page.isValid());
    QCOMPARE(page.top(), 24);
    QVERIFY2(page.left() >= 24 && page.left() <= 26, qPrintable(QString::number(page.left())));
    QVERIFY2(qAbs(page.left() - (canvas->width() - 1 - page.right())) <= 1, qPrintable(QString("%1 %2 %3").arg(page.left()).arg(page.right()).arg(canvas->width())));
    QCOMPARE(zoomField->text(), zoomText());

    // Fit page: the whole page with the margins in the canvas, centered
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitPage());
    page = firstPageRect();
    QVERIFY(page.top() >= 24 && page.left() >= 24);
    QVERIFY(page.bottom() <= canvas->height() - 25 && page.right() <= canvas->width() - 25);
    QVERIFY(page.top() <= 26 || page.left() <= 26);
    QVERIFY2(qAbs(page.left() - (canvas->width() - 1 - page.right())) <= 1, qPrintable(QString("%1 %2 %3").arg(page.left()).arg(page.right()).arg(canvas->width())));
    QCOMPARE(zoomField->text(), zoomText());

    // Fit height: the page with the margins above and below is as high as the canvas
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitHeight());
    page = firstPageRect();
    QCOMPARE(page.top(), 24);
    QVERIFY2(qAbs(page.height() - (canvas->height() - 49)) <= 1, qPrintable(QString("%1 %2").arg(page.height()).arg(canvas->height())));
    QCOMPARE(zoomField->text(), zoomText());

    // A page reached by the navigation keeps the margin above it
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitWidth());
    view->goToPage(1);
    fixture.settle();
    QRect secondPage;
    for (const pdf::PDFWidgetSnapshot::SnapshotItem& item : proxy->getSnapshot().items)
    {
        if (item.pageIndex == 1)
        {
            secondPage = item.rect.toRect();
        }
    }
    QCOMPARE(secondPage.top(), 24);
    view->goToPage(0);
    fixture.settle();
    page = firstPageRect();
    QCOMPARE(page.top(), 24);

    // At 100 % the gap between the pages (5 mm) is smaller than the margin: the page
    // reached by the navigation lies the gap below the top, the page above it is not
    // seen, so it is the current page; the next page leaves the previous one hidden
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::actualSize());
    view->goToPage(1);
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    for (const pdf::PDFWidgetSnapshot::SnapshotItem& item : proxy->getSnapshot().items)
    {
        if (item.pageIndex == 1)
        {
            QVERIFY2(item.rect.top() > 0 && item.rect.top() < 24, qPrintable(QString::number(item.rect.top())));
        }
    }
    view->goToPage(0);
    fixture.settle();
    QCOMPARE(firstPageRect().top(), 24);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::nextPage());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitWidth());
    view->goToPage(0);
    fixture.settle();

    // Shadow pixels in both themes at 1x and 2x (fit page - the whole page is seen):
    // darker than the canvas right outside of the page, fading out, the canvas beyond
    // the blur; the offset makes the shadow below the page stronger than above it
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitPage());
    page = firstPageRect();
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        manager->setScheme(scheme);
        fixture.settle();
        QCOMPARE(proxy->getPageDecoration().shadowColor, manager->getColor(pdf::PDFThemeColors::EffectShadow));

        const QColor canvasColor = ViewTestHelper::token(pdf::PDFThemeColors::BgTrack);
        for (qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = ViewTestHelper::render(canvas, ratio);
            const int middle = page.center().y();
            const QString context = QString("scheme %1, ratio %2").arg(int(scheme)).arg(ratio);
            auto lightness = [&image](QPoint point) { return ViewTestHelper::pixel(image, point).lightness(); };

            QCOMPARE(ViewTestHelper::pixel(image, QPoint(page.left() - 16, middle)), canvasColor);
            QCOMPARE(ViewTestHelper::pixel(image, QPoint(page.center().x(), 3)), canvasColor);
            QVERIFY2(lightness(QPoint(page.left() - 1, middle)) < canvasColor.lightness(), qPrintable(context));
            QVERIFY2(lightness(QPoint(page.left() - 1, middle)) < lightness(QPoint(page.left() - 5, middle)), qPrintable(context));
            QVERIFY2(lightness(QPoint(page.left() - 5, middle)) <= lightness(QPoint(page.left() - 9, middle)), qPrintable(context));
            QVERIFY2(lightness(QPoint(page.right() + 1, middle)) < canvasColor.lightness(), qPrintable(context));
            QVERIFY2(lightness(QPoint(page.center().x(), page.top() - 2)) > lightness(QPoint(page.center().x(), page.bottom() + 2)), qPrintable(context));
            QVERIFY2(lightness(QPoint(page.center().x(), page.bottom() + 2)) < canvasColor.lightness(), qPrintable(context));

            // The page itself is not darkened by the shadow
            QCOMPARE(ViewTestHelper::pixel(image, QPoint(page.left() + 2, middle)), ViewTestHelper::pixel(image, QPoint(page.right() - 2, middle)));
            QCOMPARE(ViewTestHelper::countLeakPixels(image), 0);
        }
    }
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    fixture.settle();

    // Without the decoration the page runs from edge to edge in fit width (the old applications)
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitWidth());
    proxy->setPageDecoration(pdf::PDFPageDecoration());
    fixture.settle();
    page = firstPageRect();
    QVERIFY2(page.left() <= 1, qPrintable(QString::number(page.left())));
    QCOMPARE(page.top(), 0);

    // The theme gives the decoration back
    manager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    fixture.settle();
    QCOMPARE(proxy->getPageDecoration().margin, 24);
    QCOMPARE(firstPageRect().top(), 24);
}

void AppDocumentViewTest::splitterHandle()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Splitter.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QSplitter* splitter = view->getSplitter();
    QCOMPARE(splitter->handleWidth(), 6);

    // splitter 266:7854: 6 px of bg/sidebar between the sidebar and the canvas, grip
    // 266:7855 of three dots 2 x 2 (icon/muted) 3 px apart in the middle (y 415 of 842)
    pdf::PDFAppDocumentSplitterHandle* handle = qobject_cast<pdf::PDFAppDocumentSplitterHandle*>(splitter->handle(1));
    QVERIFY(handle);
    QCOMPARE(handle->width(), 6);
    QCOMPARE(handle->mapTo(view, QPoint(0, 0)).x(), view->getSidebar()->width());
    const std::vector<QRect> dots = handle->getGripDots();
    QCOMPARE(dots.size(), size_t(3));
    QCOMPARE(dots.front(), QRect(2, (handle->height() - 12) / 2, 2, 2));
    QCOMPARE(dots[1].top() - dots[0].bottom() - 1, 3);
    QCOMPARE(dots[2].top() - dots[1].bottom() - 1, 3);

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        for (qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = ViewTestHelper::render(handle, ratio);
            QCOMPARE(ViewTestHelper::pixel(image, QPoint(1, 10)), ViewTestHelper::token(pdf::PDFThemeColors::BgSidebar));
            QCOMPARE(ViewTestHelper::pixel(image, QPoint(4, handle->height() - 10)), ViewTestHelper::token(pdf::PDFThemeColors::BgSidebar));

            // The middle of each dot is drawn by icon/muted (anti-aliased at 1x)
            const QColor muted = ViewTestHelper::token(pdf::PDFThemeColors::IconMuted);
            const QColor sidebar = ViewTestHelper::token(pdf::PDFThemeColors::BgSidebar);
            for (const QRect& dot : dots)
            {
                const QColor color = image.pixelColor(QPoint(qFloor(dot.center().x() * ratio + ratio / 2), qFloor(dot.center().y() * ratio + ratio / 2)));
                const int toMuted = qAbs(color.red() - muted.red()) + qAbs(color.green() - muted.green()) + qAbs(color.blue() - muted.blue());
                const int toSidebar = qAbs(color.red() - sidebar.red()) + qAbs(color.green() - sidebar.green()) + qAbs(color.blue() - sidebar.blue());
                QVERIFY2(toMuted < toSidebar, qPrintable(color.name()));
            }
            QCOMPARE(ViewTestHelper::countLeakPixels(image), 0);
        }
    }
}

void AppDocumentViewTest::routePage()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Route.pdf"), 6), 4);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // A page out of range is ignored (the document stays open)
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId(), 1));
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId(), 99));
    fixture.settle();
    QCOMPARE(fixture.currentDocumentTab(), tab);
    QVERIFY(tab->isViewShown());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
}

void AppDocumentViewTest::navigationCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Navigation.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutSinglePage());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::layoutSinglePage())->isChecked());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::lastPage());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(4));
    QCOMPARE(view->getToolbar()->getPageField()->text(), QStringLiteral("5"));
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::firstPage());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::nextPage());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::previousPage());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));

    // The buttons of the toolbar run the commands from the event loop
    QTest::mouseClick(view->getToolbar()->getCommandButton(pdf::PDFAppCommands::nextPage()), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));

    // Canvas commands have no shortcut on the window (the canvas has them)
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::nextPage())->shortcut().isEmpty());
    QCOMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Plus));
    QCOMPARE(view->getCanvasAction(pdf::PDFAppCommands::nextPage())->shortcut(), QKeySequence(Qt::Key_PageDown));
    QVERIFY(!view->getCanvasAction(pdf::PDFAppCommands::zoomIn()));
}

void AppDocumentViewTest::canvasShortcuts()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Keys.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutSinglePage());

    // The keys of the canvas: PgDn / PgUp / End / Home on the focused canvas
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_PageDown);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QTest::keyClick(canvas, Qt::Key_End);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(4));
    QTest::keyClick(canvas, Qt::Key_Home);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));

    // A shortcut of the user changes the key of the canvas
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::nextPage(), QStringLiteral("Ctrl+J")).isError());
    QTRY_COMPARE(view->getCanvasAction(pdf::PDFAppCommands::nextPage())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_J));
    QTest::keyClick(canvas, Qt::Key_J, Qt::ControlModifier);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
}

void AppDocumentViewTest::canvasShortcutScope()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Scope.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // Ctrl+A in the search field selects its text, the document stays unselected
    pdf::PDFLineEdit* searchField = view->getToolbar()->getSearchField();
    searchField->setText(QStringLiteral("alpha"));
    searchField->setFocus();
    QTRY_VERIFY(searchField->hasFocus());
    searchField->deselect();
    QTest::keyClick(searchField, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(searchField->selectedText(), QStringLiteral("alpha"));
    fixture.settle();
    QVERIFY(!view->hasTextSelection());

    // PgDn in the page field is not the next page of the canvas
    pdf::PDFToolBarField* pageField = view->getToolbar()->getPageField();
    pageField->setFocus();
    QTRY_VERIFY(pageField->hasFocus());
    QTest::keyClick(pageField, Qt::Key_End);
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
}

void AppDocumentViewTest::fitShortcuts()
{
    // P / W / H fit the page / its width / its height (W and H of the old applications)
    // on the focused canvas; a letter typed into a field stays a letter
    using C = pdf::PDFAppCommands;
    using ZoomHint = pdf::PDFDrawWidgetProxy::ZoomHint;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFDrawWidgetProxy* proxy = view->getProxy();
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();

    // Keys of the canvas, not of the window (Canvas scope), no conflict
    QCOMPARE(view->getCanvasAction(C::fitPage())->shortcut(), QKeySequence(Qt::Key_P));
    QCOMPARE(view->getCanvasAction(C::fitWidth())->shortcut(), QKeySequence(Qt::Key_W));
    QCOMPARE(view->getCanvasAction(C::fitHeight())->shortcut(), QKeySequence(Qt::Key_H));
    QVERIFY(fixture.window->getCommandAction(C::fitWidth())->shortcut().isEmpty());
    QVERIFY(fixture.context->getCommandRegistry()->findConflicts({}).empty());

    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_P);
    fixture.settle();
    QCOMPARE(proxy->getZoomFitHint(), std::make_optional(ZoomHint::Fit));
    QTest::keyClick(canvas, Qt::Key_H);
    fixture.settle();
    QCOMPARE(proxy->getZoomFitHint(), std::make_optional(ZoomHint::FitHeight));
    QTest::keyClick(canvas, Qt::Key_W);
    fixture.settle();
    QCOMPARE(proxy->getZoomFitHint(), std::make_optional(ZoomHint::FitWidth));

    // Nothing in a field: the zoom stays as it is
    ViewTestHelper::trigger(fixture, C::actualSize());
    const std::optional<ZoomHint> hint = proxy->getZoomFitHint();
    const pdf::PDFReal zoom = view->getZoom();
    auto zoomUnchanged = [&]() { return proxy->getZoomFitHint() == hint && qFuzzyCompare(view->getZoom(), zoom); };

    pdf::PDFLineEdit* searchField = view->getToolbar()->getSearchField();
    searchField->setFocus();
    QTRY_VERIFY(searchField->hasFocus());
    searchField->clear();
    QTest::keyClicks(searchField, QStringLiteral("WHP"));
    fixture.settle();
    QCOMPARE(searchField->text(), QStringLiteral("WHP"));
    QVERIFY(zoomUnchanged());
    searchField->clear();

    pdf::PDFToolBarField* pageField = view->getToolbar()->getPageField();
    pageField->setFocus();
    QTRY_VERIFY(pageField->hasFocus());
    QTest::keyClicks(pageField, QStringLiteral("WHPwhp"));
    fixture.settle();
    QVERIFY(zoomUnchanged());
    QTest::keyClick(pageField, Qt::Key_Escape);

    // A text field of the form on the canvas (the editor of the form manager takes
    // the printable keys by ShortcutOverride)
    pdf::PDFWidgetFormManager* formManager = view->getFormManager();
    QVERIFY(formManager);
    const pdf::PDFFormField* nameField = nullptr;
    formManager->getForm()->apply([&nameField](const pdf::PDFFormField* formField)
    {
        if (!nameField && formField->getName(pdf::PDFFormField::Partial) == QStringLiteral("name"))
        {
            nameField = formField;
        }
    });
    QVERIFY(nameField);
    QVERIFY(!nameField->getWidgets().empty());
    const QPointF center = formManager->getWidgetRectangle(nameField->getWidgets().front()).center();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, center));
    fixture.settle();
    QVERIFY(formManager->getFocusedWidget().isValid());
    QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(canvas, QStringLiteral("WHP"));
    fixture.settle();
    QVERIFY(zoomUnchanged());
    QTest::keyClick(canvas, Qt::Key_Return);
    fixture.settle();
    QVERIFY(zoomUnchanged());
    const pdf::PDFDocument* document = formManager->getDocument();
    QVERIFY(document);
    pdf::PDFDocumentDataLoaderDecorator loader(document);
    QCOMPARE(loader.readTextString(document->getObject(nameField->getValue()), QString()), QStringLiteral("WHP"));

    // Without a focused field the canvas has the keys again; the user can change them
    QVERIFY(!formManager->getFocusedWidget().isValid());
    QVERIFY(!fixture.context->getShortcuts().setShortcut(C::fitWidth(), QStringLiteral("Q")).isError());
    QTRY_COMPARE(view->getCanvasAction(C::fitWidth())->shortcut(), QKeySequence(Qt::Key_Q));
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_W);
    fixture.settle();
    QVERIFY(zoomUnchanged());
    QTest::keyClick(canvas, Qt::Key_Q);
    fixture.settle();
    QCOMPARE(proxy->getZoomFitHint(), std::make_optional(ZoomHint::FitWidth));

    // The tiles of the View panel show the keys of the user in their tooltips
    pdf::PDFAppDocumentPanelPopup* panel = view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    QVERIFY(panel);
    QCOMPARE(panel->getCommandButton(C::fitPage())->toolTip(), QStringLiteral("Fit page (P)"));
    QCOMPARE(panel->getCommandButton(C::fitWidth())->toolTip(), QStringLiteral("Fit width (Q)"));
    QCOMPARE(panel->getCommandButton(C::fitHeight())->toolTip(), QStringLiteral("Fit height (H)"));
    panel->hide();
    fixture.settle();

    // The tooltips are built from the shortcuts of the user, which the view keeps (read
    // again only by a notification of the database), not read at every opening; the
    // toolbar shows the same keys (the time of the openings is printed)
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 100; ++i)
    {
        pdf::PDFAppDocumentPanelPopup* opened = view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
        QVERIFY(opened);
        opened->hide();
    }
    qInfo("100 openings of the View panel: %lld ms", timer.elapsed());
    fixture.settle();
    QVERIFY(!fixture.context->getShortcuts().setShortcut(C::fitPage(), QStringLiteral("Shift+P")).isError());
    QTRY_COMPARE(view->getCanvasAction(C::fitPage())->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_P));
    panel = view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    QVERIFY(panel);
    QCOMPARE(panel->getCommandButton(C::fitPage())->toolTip(), QStringLiteral("Fit page (Shift+P)"));
    panel->hide();
    fixture.settle();
    QVERIFY(!fixture.context->getShortcuts().setShortcut(C::zoomIn(), QStringLiteral("Ctrl+Shift+I")).isError());
    QTRY_COMPARE(view->getToolbar()->getCommandButton(C::zoomIn())->toolTip(), QStringLiteral("Zoom in (Ctrl+Shift+I)"));
}

void AppDocumentViewTest::toolManager()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Tools.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFToolManager* tools = tab->getView()->getToolManager();

    // The tool manager without actions: no find dialog, the tools are switched by the application
    QVERIFY(!tools->getFindTextTool());
    QSignalSpy activeToolSpy(tools, &pdf::PDFToolManager::activeToolChanged);
    tools->setActiveTool(pdf::PDFToolManager::ToolEnd);
    tools->setActiveTool(pdf::PDFToolManager::SelectTextTool);
    QCOMPARE(tools->getActiveTool(), tools->getPredefinedTool(pdf::PDFToolManager::SelectTextTool));
    QVERIFY(!activeToolSpy.isEmpty());

    pdf::PDFSelectTextTool* selectText = tools->getSelectTextTool();
    QVERIFY(selectText);
    QSignalSpy selectionSpy(selectText, &pdf::PDFSelectTextTool::selectionChanged);

    // The text layout of the pages is made in a worker thread, when the tool is activated
    QTRY_VERIFY(tab->getView()->getProxy()->getTextLayoutCompiler()->isTextLayoutReady());
    selectText->selectAll();
    QVERIFY(selectText->hasSelection());
    QCOMPARE(selectionSpy.count(), 1);
    selectText->deselect();
    QVERIFY(!selectText->hasSelection());

    tools->setActiveTool(pdf::PDFToolManager::ToolEnd);
    QVERIFY(!tools->getActiveTool());
    QVERIFY(!tools->getPredefinedTool(pdf::PDFToolManager::FindTextTool));
}

void AppDocumentViewTest::textSelectionThemeColor()
{
    // The selected text is bg/accent-subtle without a border (Figma 877:134209), not the
    // yellow selection with the darker border of the old applications
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ThemeSelection.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFToolManager* tools = view->getToolManager();
    tools->setActiveTool(pdf::PDFToolManager::SelectTextTool);
    pdf::PDFSelectTextTool* selectText = tools->getSelectTextTool();
    QVERIFY(selectText);
    QTRY_VERIFY(view->getProxy()->getTextLayoutCompiler()->isTextLayoutReady());
    QVERIFY(view->getProxy()->hasViewColors());
    pdftest::AppDocumentFixture::settle();

    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    // bg/accent-subtle is a translucent accent: on the white paper it is its blend with white
    const QColor token = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgAccentSubtle);
    const qreal alpha = token.alphaF();
    const QColor subtle = QColor::fromRgbF(float(1.0 - alpha + token.redF() * alpha), float(1.0 - alpha + token.greenF() * alpha), float(1.0 - alpha + token.blueF() * alpha));
    auto count = [](const QImage& image, auto predicate)
    {
        int result = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                result += predicate(image.pixelColor(x, y)) ? 1 : 0;
            }
        }
        return result;
    };
    auto isYellow = [](const QColor& color) { return color.red() > 180 && color.green() > 180 && color.blue() < 120; };
    auto isSubtle = [&subtle](const QColor& color) { return std::abs(color.red() - subtle.red()) <= 6 && std::abs(color.green() - subtle.green()) <= 6 && std::abs(color.blue() - subtle.blue()) <= 6; };

    const QImage before = canvas->grab().toImage();
    selectText->selectAll();
    QVERIFY(selectText->hasSelection());
    pdftest::AppDocumentFixture::settle();
    const QImage after = canvas->grab().toImage();
    QCOMPARE(count(after, isYellow), count(before, isYellow));
    QVERIFY2(count(after, isSubtle) > count(before, isSubtle) + 50, qPrintable(QStringLiteral("%1 %2").arg(count(after, isSubtle)).arg(count(before, isSubtle))));
    selectText->deselect();
}

void AppDocumentViewTest::textSelectionCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Selection.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // Copy and Deselect need a selection
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::copy()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::deselect()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::selectAll()));

    // Select all waits for the text layout, Ctrl+A on the canvas
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
    QTRY_VERIFY(view->hasTextSelection());
    QTRY_VERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::copy()));

    QApplication::clipboard()->clear();
    QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier);
    QTRY_VERIFY(QApplication::clipboard()->text().contains(QStringLiteral("alpha")));

    view->executeCommand(pdf::PDFAppCommands::deselect());
    QVERIFY(!view->hasTextSelection());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::copy()));

    // Select all from another tool activates Select text
    view->setCanvasTool(pdf::PDFToolManager::ScreenshotTool);
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ScreenshotTool));
    QVERIFY(view->executeCommand(pdf::PDFAppCommands::selectAll()));
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));
    QTRY_VERIFY(view->hasTextSelection());
}

void AppDocumentViewTest::viewDestroyedSilently()
{
    // RT4: the destructor of the view detached the tool manager from the document,
    // the Select text tool cleared its selection and the dying view emitted
    // viewStateChanged (its receivers read a view being destroyed)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Dying.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(view->executeCommand(pdf::PDFAppCommands::selectAll()));
    QTRY_VERIFY(view->hasTextSelection());
    pdftest::AppDocumentFixture::settle();

    int emitted = 0;
    bool isDying = false;
    QObject receiver;
    QObject::connect(view, &pdf::PDFAppDocumentView::viewStateChanged, &receiver, [&emitted, &isDying]() { emitted += isDying ? 1 : 0; });
    isDying = true;
    delete tab;
    QCOMPARE(emitted, 0);
    pdftest::AppDocumentFixture::settle();
}

void AppDocumentViewTest::selectMenuTools()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SelectMenu.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentSelectMenu* menu = view->getToolbar()->getSelectMenu();
    QVERIFY(menu);

    // Select menu 198:5517: Hand first and checked - the default mode of a new tab, as in
    // the old applications (owner 2026-10-25; the row without an icon until "hand" exists
    // in the library)
    QCOMPARE(menu->actions().size(), 5);
    QCOMPARE(menu->actions().first(), menu->getCommandAction(pdf::PDFAppCommands::handTool()));
    QCOMPARE(menu->actions().first()->text(), QStringLiteral("Hand"));
    QCOMPARE(menu->actions().first()->icon().isNull(), pdf::PDFAppCommands::getHandIconName().isEmpty());
    QCOMPARE(menu->actions().at(1), menu->getCommandAction(pdf::PDFAppCommands::selectText()));
    menu->updateState();
    QVERIFY(view->isHandMode());
    QVERIFY(!view->getActiveTool());
    QVERIFY(menu->getCommandAction(pdf::PDFAppCommands::handTool())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectText())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectTable())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectAll()));

    // Each item activates its tool; the active tool again returns to the mode (Hand)
    menu->getCommandAction(pdf::PDFAppCommands::selectTable())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTableTool));
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::selectTable())->isChecked());
    menu->updateState();
    QVERIFY(menu->getCommandAction(pdf::PDFAppCommands::selectTable())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectText())->isChecked());

    menu->getCommandAction(pdf::PDFAppCommands::screenshot())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ScreenshotTool));
    menu->getCommandAction(pdf::PDFAppCommands::extractImage())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ExtractImageTool));
    menu->getCommandAction(pdf::PDFAppCommands::extractImage())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));

    // Select text is a mode: it stays the tool, when it is chosen again
    menu->getCommandAction(pdf::PDFAppCommands::selectText())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));
    QVERIFY(!view->isHandMode());
    menu->getCommandAction(pdf::PDFAppCommands::selectText())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));

    // Esc of a tool of the menu returns to the chosen mode (Select text here; it left
    // no tool active before)
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    for (const QString& commandId : { pdf::PDFAppCommands::screenshot(), pdf::PDFAppCommands::selectTable(), pdf::PDFAppCommands::extractImage() })
    {
        menu->getCommandAction(commandId)->trigger();
        fixture.settle();
        QVERIFY2(!view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool), qPrintable(commandId));
        QTest::keyClick(canvas, Qt::Key_Escape);
        fixture.settle();
        QVERIFY2(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool), qPrintable(commandId));
    }

    // The menu opens by the Select button: menu-select 198:5517 is 232 px wide and its
    // panel lies 4 px below the toolbar (y 94 of 198:5380) as the panels of the toolbar
    QToolButton* selectButton = view->getToolbar()->getSelectButton();
    QRect panel;
    QTimer::singleShot(0, menu, [menu, &panel]()
    {
        panel = menu->geometry().marginsRemoved(pdf::PDFWidgetStyle::getPopoverMargins(menu));
        menu->close();
    });
    selectButton->showMenu();
    fixture.settle();
    QVERIFY(!menu->isVisible());
    QVERIFY(panel.isValid());
    QCOMPARE(panel.width(), pdf::PDFAppDocumentSelectMenu::PANEL_WIDTH);
    pdf::PDFAppDocumentToolbar* toolbar = view->getToolbar();
    QCOMPARE(panel.top(), toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + pdf::PDFAppDocumentToolbar::PANEL_GAP);
}

void AppDocumentViewTest::handMode()
{
    // Hand (owner 2026-10-25, no functionality of the old applications is lost): no
    // tool - a left drag pans the pages, links, fields and annotations take their clicks
    using C = pdf::PDFAppCommands;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentSelectMenu* menu = view->getToolbar()->getSelectMenu();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    auto send = [canvas](QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, position, canvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &event);
    };
    auto widgetPoint = [view](const QPointF& pagePoint)
    {
        bool ok = false;
        const QPoint point = pdftest::AppDocumentFixture::pageToWidget(view, 0, pagePoint, &ok);
        return ok ? point : QPoint(-1, -1);
    };

    // The command: Ctrl+H (macOS: Control+H - Qt::META, Command+H hides the application)
    // without a conflict on every platform, checkable, found by the search of the commands
    const pdf::PDFAppCommand* command = fixture.context->getCommandRegistry()->findCommand(C::handTool());
    QVERIFY(command);
    QCOMPARE(C::getHandShortcut(false), QKeySequence(Qt::CTRL | Qt::Key_H));
    QCOMPARE(C::getHandShortcut(true), QKeySequence(Qt::META | Qt::Key_H));
#if defined(Q_OS_MACOS)
    QVERIFY(C::isMacOS());
#else
    QVERIFY(!C::isMacOS());
#endif
    QCOMPARE(command->defaultShortcut, C::getHandShortcut(C::isMacOS()));
    QVERIFY(command->isCheckable);
    QCOMPARE(command->iconName, C::getHandIconName());
    QVERIFY(fixture.context->getCommandRegistry()->findConflicts({}).empty());
    for (const bool isMacOS : { false, true })
    {
        const pdf::PDFAppCommandRegistry::UserShortcuts platformShortcuts = { { C::handTool(), C::getHandShortcut(isMacOS).toString(QKeySequence::PortableText) } };
        QVERIFY2(fixture.context->getCommandRegistry()->findConflicts(platformShortcuts).empty(), isMacOS ? "macOS" : "Windows / Linux");
        QCOMPARE(fixture.context->getCommandRegistry()->getShortcut(C::handTool(), platformShortcuts), C::getHandShortcut(isMacOS));
    }
    const pdf::PDFAppSearchData data = pdf::PDFAppSearchData::load(fixture.context.get());
    const pdf::PDFAppSearchEngine engine;
    QVERIFY(engine.search(QStringLiteral("hand"), data).findResult(pdf::PDFAppSearchGroup::Commands, C::handTool()));
    QVERIFY(engine.search(QStringLiteral("pan"), data).findResult(pdf::PDFAppSearchGroup::Commands, C::handTool()));

    // Hand is the mode of a new tab (owner 2026-10-25); chosen again from the menu it
    // stays: no tool, checked, the Select button shows its icon, the open hand without
    // a move of the mouse
    QVERIFY(view->isHandMode());
    QVERIFY(!view->getActiveTool());
    menu->getCommandAction(C::handTool())->trigger();
    fixture.settle();
    QVERIFY(view->isHandMode());
    QVERIFY(!view->getActiveTool());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
    QVERIFY(fixture.window->getCommandAction(C::handTool())->isChecked());
    QVERIFY(!fixture.window->getCommandAction(C::selectText())->isChecked());
    menu->updateState();
    QVERIFY(menu->getCommandAction(C::handTool())->isChecked());
    const QString handIcon = C::getHandIconName().isEmpty() ? QStringLiteral("cursor") : C::getHandIconName();
    QCOMPARE(pdf::PDFThemeIcons::getIconName(view->getToolbar()->getSelectButton()->icon()), handIcon);
    QTRY_COMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);

    // A left drag pans the page with the closed hand; no text is selected
    const QPoint start = widgetPoint(QPointF(250, 380));
    const QPoint before = widgetPoint(QPointF(150, 200));
    QVERIFY(canvas->rect().contains(start));
    send(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
    send(QEvent::MouseMove, start + QPoint(0, -30), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, start + QPoint(0, -60), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
    send(QEvent::MouseButtonRelease, start + QPoint(0, -60), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    fixture.settle();
    QCOMPARE(widgetPoint(QPointF(150, 200)).y() - before.y(), -60);
    QVERIFY(!view->hasTextSelection());
    QVERIFY(view->isHandMode());

    // Annotations are selected and moved as in Select text (the manager takes the press
    // over an annotation before the pan)
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    send(QEvent::MouseMove, widgetPoint(QPointF(42, 122)), Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(42, 122)));
    fixture.settle();
    QVERIFY(controller->hasAnnotationSelection());
    // A drag of the selected annotation is its drag (drag and drop of the engine, which
    // offscreen drops nowhere), not a pan of the page
    const QPoint squareBefore = widgetPoint(QPointF(42, 122));
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(42, 122), QPointF(42, 160)));
    fixture.settle();
    QCOMPARE(widgetPoint(QPointF(42, 122)), squareBefore);
    QVERIFY(controller->hasAnnotationSelection());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(250, 380)));
    fixture.settle();
    QVERIFY(!controller->hasAnnotationSelection());
    QVERIFY(view->isHandMode());
    QVERIFY(!view->getActiveTool());

    // Esc of a tool of the menu and of a creation tool, the active tool chosen again:
    // back to Hand
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    for (const QString& commandId : { C::screenshot(), C::selectTable(), C::extractImage(), C::insertRectangle() })
    {
        ViewTestHelper::trigger(fixture, commandId);
        QVERIFY2(view->getActiveTool(), qPrintable(commandId));
        QTest::keyClick(canvas, Qt::Key_Escape);
        fixture.settle();
        QVERIFY2(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd), qPrintable(commandId));
    }
    ViewTestHelper::trigger(fixture, C::selectTable());
    ViewTestHelper::trigger(fixture, C::selectTable());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
    QTRY_COMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);

    // A locked annotation (a locked document) shows the arrow - a press selects it
    ViewTestHelper::trigger(fixture, C::lockDocument());
    QVERIFY(tab->getDocument()->isLocked());
    send(QEvent::MouseMove, widgetPoint(QPointF(42, 122)), Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);
    send(QEvent::MouseMove, widgetPoint(QPointF(250, 380)), Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    ViewTestHelper::trigger(fixture, C::lockDocument());

    // Select text again (the menu, Ctrl+H toggles back to Hand); Select all of the text
    // chooses Select text
    ViewTestHelper::trigger(fixture, C::selectText());
    QVERIFY(!view->isHandMode());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));
    QCOMPARE(fixture.window->getCommandAction(C::handTool())->shortcut(), C::getHandShortcut(C::isMacOS()));
    QTest::keyClick(canvas, Qt::Key_H, C::isMacOS() ? Qt::MetaModifier : Qt::ControlModifier);
    QTRY_VERIFY(view->isHandMode());
    QVERIFY(view->executeCommand(C::selectAll()));
    QVERIFY(!view->isHandMode());
    QTRY_VERIFY(view->hasTextSelection());

    // A link and a field take their click in Hand; a new tab starts in Hand also after
    // Select text was chosen in another tab (the mode belongs to its tab)
    pdf::PDFAppDocumentTab* linksTab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(linksTab);
    pdf::PDFAppDocumentView* linksView = linksTab->getView();
    QVERIFY(!view->isHandMode());
    QVERIFY(linksView->isHandMode());
    QVERIFY(linksView->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
    QCOMPARE(linksView->getCurrentPageIndex(), pdf::PDFInteger(0));

    // A drag, which starts on a link, pans the page with the closed hand (owner
    // 2026-10-25) and follows nothing; a click follows it
    {
        QWidget* linksCanvas = pdftest::AppDocumentFixture::canvasWidget(linksView);
        auto sendLinks = [linksCanvas](QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
        {
            QMouseEvent event(type, position, linksCanvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(linksCanvas, &event);
        };
        bool ok = false;
        const QPoint onLink = pdftest::AppDocumentFixture::pageToWidget(linksView, 0, QPointF(130, 108), &ok);
        QVERIFY(ok);
        sendLinks(QEvent::MouseMove, onLink, Qt::NoButton, Qt::NoButton);
        QVERIFY(linksCanvas->cursor().shape() != Qt::OpenHandCursor);
        sendLinks(QEvent::MouseButtonPress, onLink, Qt::LeftButton, Qt::LeftButton);
        sendLinks(QEvent::MouseMove, onLink + QPoint(0, -40), Qt::NoButton, Qt::LeftButton);
        QCOMPARE(linksCanvas->cursor().shape(), Qt::ClosedHandCursor);
        sendLinks(QEvent::MouseMove, onLink + QPoint(0, -60), Qt::NoButton, Qt::LeftButton);
        sendLinks(QEvent::MouseButtonRelease, onLink + QPoint(0, -60), Qt::LeftButton, Qt::NoButton);
        fixture.settle();
        QCOMPARE(pdftest::AppDocumentFixture::pageToWidget(linksView, 0, QPointF(130, 108), &ok), onLink + QPoint(0, -60));
        QCOMPARE(linksView->getCurrentPageIndex(), pdf::PDFInteger(0));
        QVERIFY(linksCanvas->cursor().shape() != Qt::ClosedHandCursor);
        QVERIFY(linksView->isHandMode());
    }
    QVERIFY(pdftest::AppDocumentFixture::clickPage(linksView, 0, QPointF(130, 108)));
    fixture.settle();
    QTRY_COMPARE(linksView->getCurrentPageIndex(), pdf::PDFInteger(2));

    pdf::PDFAppDocumentTab* formTab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(formTab);
    pdf::PDFAppDocumentView* formView = formTab->getView();
    QVERIFY(formView->isHandMode());
    QVERIFY(!formView->getFormManager()->getFocusedWidget().isValid());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(formView, 0, QPointF(130, 262)));
    fixture.settle();
    QVERIFY(formView->getFormManager()->getFocusedWidget().isValid());
    QVERIFY(formView->isHandMode());
}

void AppDocumentViewTest::cursorFollowsState()
{
    // The cursor follows the real state (owner 2026-10-25): the automatic scrolling of
    // the middle button shows its own cursor over the always active Select text, its end
    // (the middle button, Esc, a lost release) gives the cursor of the tool back at once;
    // each tool of the Select menu shows its cursor without a move of the mouse
    using C = pdf::PDFAppCommands;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Cursor.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    auto send = [canvas](QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, position, canvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &event);
    };
    auto middleClick = [&send](QPoint position)
    {
        send(QEvent::MouseButtonPress, position, Qt::MiddleButton, Qt::MiddleButton);
        send(QEvent::MouseButtonRelease, position, Qt::MiddleButton, Qt::NoButton);
    };
    bool ok = false;
    const QPoint empty = pdftest::AppDocumentFixture::pageToWidget(view, 0, QPointF(290, 10), &ok);
    QVERIFY(ok);
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    ViewTestHelper::trigger(fixture, C::selectText());

    // Select text, nothing under the pointer: the arrow; the automatic scrolling: its cursor
    send(QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    send(QEvent::MouseMove, empty + QPoint(1, 0), Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);

    // Esc ends it (not full screen, Select text stays)
    middleClick(empty);
    QTest::keyClick(canvas, Qt::Key_Escape);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));

    // A left press ends it and selects no text
    middleClick(empty);
    send(QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, empty, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);
    QVERIFY(!view->hasTextSelection());

    // The focus taken by a popup or another window ends it (its release never comes)
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    QFocusEvent focusOut(QEvent::FocusOut, Qt::PopupFocusReason);
    QCoreApplication::sendEvent(canvas, &focusOut);
    QCOMPARE(canvas->cursor().shape(), Qt::ArrowCursor);

    // Each tool shows its cursor at once, the automatic scrolling works also in the pick
    // tools of Screenshot / Select table
    ViewTestHelper::trigger(fixture, C::screenshot());
    QTRY_COMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    ViewTestHelper::trigger(fixture, C::selectTable());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTableTool));
    QTRY_COMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    middleClick(empty);
    ViewTestHelper::trigger(fixture, C::extractImage());
    QTRY_COMPARE(canvas->cursor().shape(), Qt::CrossCursor);
    ViewTestHelper::trigger(fixture, C::magnifier());
    QTRY_COMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    ViewTestHelper::trigger(fixture, C::magnifier());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));

    // Hand: the open hand, the closed hand while dragging, a lost release (a move
    // without the button) gives the open hand back
    ViewTestHelper::trigger(fixture, C::handTool());
    QTRY_COMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    send(QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
    send(QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    middleClick(empty);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeAllCursor);
    QTest::keyClick(canvas, Qt::Key_Escape);
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    QVERIFY(view->isHandMode());
}

void AppDocumentViewTest::magnifier()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Magnifier.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // Settings › Viewing › Magnifier: size 100 px (the diameter of the circle of
    // 1234:164735 - the tool takes the radius), zoom 2.0
    pdf::PDFMagnifierTool* magnifier = view->getToolManager()->getMagnifierTool();
    QVERIFY(magnifier);
    QCOMPARE(magnifier->getMagnifierSize(), 50);
    QCOMPARE(magnifier->getMagnifierZoom(), 2.0);
    QCOMPARE(pdf::PDFAppDocumentView::getMagnifierRadius(101), 50);
    QCOMPARE(pdf::PDFAppDocumentView::getMagnifierRadius(0), 1);
    QCOMPARE(pdf::PDFAppDocumentView::getMagnifierRadius(-20), 1);

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::magnifier());
    QVERIFY(magnifier->isActive());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::magnifier())->isChecked());

    // The settings apply also to an active magnifier
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::magnifierSize(), 180).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.5).isError());
    QTRY_COMPARE(magnifier->getMagnifierSize(), 90);
    QCOMPARE(magnifier->getMagnifierZoom(), 3.5);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::magnifierSize(), 100).isError());
    QTRY_COMPARE(magnifier->getMagnifierSize(), 50);

    // The pointer over the canvas draws the magnifier: a circle of the diameter 100
    // with the outline border/strong of 1 px inside of it (never the white line in the
    // difference mode of the old applications) and the shadow of effect/shadow
    const pdf::PDFViewColors colors = view->getProxy()->getViewColors();
    QCOMPARE(colors.magnifierBorder, pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderStrong));
    QCOMPARE(colors.magnifierShadow, pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::EffectShadow));
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    const QPoint center = canvas->rect().center();
    QTest::mouseMove(canvas, center);
    fixture.settle();
    const QImage image = ViewTestHelper::render(canvas);
    QVERIFY(!image.isNull());
    const QColor border = colors.magnifierBorder;
    const auto isBorder = [&border](QColor color)
    {
        return qAbs(color.red() - border.red()) <= 24 && qAbs(color.green() - border.green()) <= 24 && qAbs(color.blue() - border.blue()) <= 24;
    };
    for (const QPoint offset : { QPoint(49, 0), QPoint(-50, 0), QPoint(0, 49), QPoint(0, -50) })
    {
        QVERIFY2(isBorder(image.pixelColor(center + offset)), qPrintable(image.pixelColor(center + offset).name()));
    }
    QVERIFY(!isBorder(image.pixelColor(center + QPoint(30, 0))));
    QVERIFY(!isBorder(image.pixelColor(center + QPoint(56, 0))));
    QVERIFY(!isBorder(image.pixelColor(center + QPoint(99, 0))));

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::magnifier());
    QVERIFY(!magnifier->isActive());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::ToolEnd));
}

void AppDocumentViewTest::layoutCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Layout.pdf"), 6));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFDrawWidgetProxy* proxy = view->getProxy();

    // Default of a new document: viewing.pageLayout = continuous
    QCOMPARE(view->getViewLayout(), pdf::PDFAppViewLayout::Continuous);
    QCOMPARE(proxy->getPageLayout(), pdf::PageLayout::OneColumn);
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::layoutContinuous())->isChecked());

    const std::pair<QString, pdf::PageLayout> layouts[] =
    {
        { pdf::PDFAppCommands::layoutSinglePage(), pdf::PageLayout::SinglePage },
        { pdf::PDFAppCommands::layoutTwoPages(), pdf::PageLayout::TwoPagesLeft },
        { pdf::PDFAppCommands::layoutTwoColumns(), pdf::PageLayout::TwoColumnLeft },
        { pdf::PDFAppCommands::layoutContinuous(), pdf::PageLayout::OneColumn }
    };
    for (const auto& layout : layouts)
    {
        ViewTestHelper::trigger(fixture, layout.first);
        QCOMPARE(proxy->getPageLayout(), layout.second);
        QVERIFY(fixture.window->getCommandAction(layout.first)->isChecked());
    }

    // First page on the right side: the right variants of the two-page layouts
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::firstPageRight());
    QVERIFY(view->isFirstPageRight());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::firstPageRight())->isChecked());
    QCOMPARE(proxy->getPageLayout(), pdf::PageLayout::OneColumn);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutTwoPages());
    QCOMPARE(proxy->getPageLayout(), pdf::PageLayout::TwoPagesRight);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutTwoColumns());
    QCOMPARE(proxy->getPageLayout(), pdf::PageLayout::TwoColumnRight);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::firstPageRight());
    QCOMPARE(proxy->getPageLayout(), pdf::PageLayout::TwoColumnLeft);

    // Rotation of the view
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::rotateViewRight());
    QCOMPARE(view->getRotation(), 90);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::rotateViewLeft());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::rotateViewLeft());
    QCOMPARE(view->getRotation(), 270);

    // The default of a new document from the settings
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::viewingPageLayout(), QStringLiteral("twoPages")).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::viewingFirstPageRight(), true).isError());
    pdf::PDFAppDocumentTab* second = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Layout2.pdf"), 4));
    QVERIFY(second);
    QCOMPARE(second->getView()->getViewLayout(), pdf::PDFAppViewLayout::TwoPages);
    QCOMPARE(second->getView()->getProxy()->getPageLayout(), pdf::PageLayout::TwoPagesRight);
}

void AppDocumentViewTest::zoomCommands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Zoom.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFToolBarField* zoomField = view->getToolbar()->getZoomField();

    // Default viewing.zoom = fitWidth
    QCOMPARE(view->getProxy()->getZoomFitHint(), std::make_optional(pdf::PDFDrawWidgetProxy::ZoomHint::FitWidth));

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::actualSize());
    QCOMPARE(view->getZoom(), 1.0);
    QCOMPARE(zoomField->text(), QStringLiteral("100 %"));
    QVERIFY(!view->getProxy()->getZoomFitHint());

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::zoomIn());
    QVERIFY(view->getZoom() > 1.0);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::zoomOut());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::zoomOut());
    QVERIFY(view->getZoom() < 1.0);

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitPage());
    QCOMPARE(view->getProxy()->getZoomFitHint(), std::make_optional(pdf::PDFDrawWidgetProxy::ZoomHint::Fit));
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fitHeight());
    QCOMPARE(view->getProxy()->getZoomFitHint(), std::make_optional(pdf::PDFDrawWidgetProxy::ZoomHint::FitHeight));

    // The zoom field: a typed value in percent
    zoomField->setFocus();
    QTRY_VERIFY(zoomField->hasFocus());
    zoomField->selectAll();
    QTest::keyClicks(zoomField, QStringLiteral("250"));
    QTest::keyClick(zoomField, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(view->getZoom(), 2.5);
    QCOMPARE(zoomField->text(), QStringLiteral("250 %"));
    QVERIFY(!zoomField->isInvalid());

    // The buttons of the toolbar
    QTest::mouseClick(view->getToolbar()->getCommandButton(pdf::PDFAppCommands::zoomIn()), Qt::LeftButton);
    fixture.settle();
    QTRY_VERIFY(view->getZoom() > 2.5);
}

void AppDocumentViewTest::zoomFieldHostile()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ZoomHostile.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFToolBarField* zoomField = view->getToolbar()->getZoomField();
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::actualSize());

    // Invalid values keep the zoom and mark the field
    const QString texts[] = { QStringLiteral("abc"), QStringLiteral("-5"), QStringLiteral("0"), QStringLiteral("NaN"), QStringLiteral("inf"), QStringLiteral("1e309"), QString(1000, QLatin1Char('9')).left(16), QStringLiteral("%%%") };
    for (const QString& text : texts)
    {
        zoomField->setFocus();
        zoomField->setText(text);
        QTest::keyClick(zoomField, Qt::Key_Return);
        fixture.settle();
        QVERIFY(std::isfinite(view->getZoom()));
        QVERIFY(view->getZoom() >= pdf::PDFDrawWidgetProxy::getMinZoom());
        QVERIFY(view->getZoom() <= pdf::PDFDrawWidgetProxy::getMaxZoom());
    }

    // Out of the range: bounded to 8 % - 6400 %
    view->setZoom(1000000.0);
    QCOMPARE(view->getZoom(), pdf::PDFDrawWidgetProxy::getMaxZoom());
    view->setZoom(0.0001);
    QCOMPARE(view->getZoom(), pdf::PDFDrawWidgetProxy::getMinZoom());
    const double before = view->getZoom();
    view->setZoom(std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(view->getZoom(), before);

    zoomField->setFocus();
    zoomField->setText(QStringLiteral("abc"));
    QTest::keyClick(zoomField, Qt::Key_Return);
    QVERIFY(zoomField->isInvalid());
}

void AppDocumentViewTest::pageFieldAndLabels()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFToolBarField* pageField = view->getToolbar()->getPageField();
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutSinglePage());

    // Labels i, ii, iii, 1 - 5, A-1, A-2: the field shows the label, the suffix the number
    QVERIFY(view->hasPageLabels());
    QCOMPARE(view->getPageLabel(0), QStringLiteral("i"));
    QCOMPARE(view->getPageLabel(9), QStringLiteral("A-2"));
    QCOMPARE(view->getPageLabel(10), QString());
    QCOMPARE(pageField->text(), QStringLiteral("i"));
    QCOMPARE(pageField->getSuffix(), QStringLiteral("(1 / 10)"));

    // The status bar has the page item of a document with labels
    QCOMPARE(view->getStatusBar()->getPageText(), QStringLiteral("Page 1 (i) of 10"));
    QVERIFY(view->getStatusBar()->getPageItem()->isVisible());

    // status-page 1234:165228: the first trailing item, left of the signatures and of
    // the rendering issues, the message at the leading edge
    {
        pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
        QWidget* pageItem = statusBar->getPageItem();
        fixture.settle();
        QVERIFY(pageItem->geometry().left() > statusBar->width() / 2);
        for (QWidget* item : statusBar->getItems())
        {
            QVERIFY2(!item->isVisible() || item->geometry().left() >= pageItem->geometry().right(), qPrintable(item->objectName()));
        }
        QVERIFY(statusBar->width() - 8 - pageItem->geometry().right() <= 12 + [statusBar]()
        {
            int width = 0;
            for (QWidget* item : statusBar->getItems())
            {
                width += item->isVisible() ? item->width() + 12 : 0;
            }
            return width;
        }());
    }

    // A label goes first, a number of a page otherwise
    QCOMPARE(view->findPageIndex(QStringLiteral("A-1")), std::make_optional(pdf::PDFInteger(8)));
    QCOMPARE(view->findPageIndex(QStringLiteral("5")), std::make_optional(pdf::PDFInteger(7)));
    QCOMPARE(view->findPageIndex(QStringLiteral("10")), std::make_optional(pdf::PDFInteger(9)));
    QCOMPARE(view->findPageIndex(QStringLiteral(" iii ")), std::make_optional(pdf::PDFInteger(2)));
    QVERIFY(!view->findPageIndex(QStringLiteral("11")));

    pageField->setFocus();
    QTRY_VERIFY(pageField->hasFocus());
    pageField->selectAll();
    QTest::keyClicks(pageField, QStringLiteral("A-1"));
    QTest::keyClick(pageField, Qt::Key_Return);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(8));
    QCOMPARE(pageField->text(), QStringLiteral("A-1"));
    QCOMPARE(view->getStatusBar()->getPageText(), QStringLiteral("Page 9 (A-1) of 10"));

    // An unknown page keeps the text and marks the field
    pageField->setFocus();
    pageField->selectAll();
    QTest::keyClicks(pageField, QStringLiteral("xyz"));
    QTest::keyClick(pageField, Qt::Key_Return);
    QVERIFY(pageField->isInvalid());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(8));

    // A click on the page item opens Go to
    QTest::mouseClick(view->getStatusBar()->getPageItem(), Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppDocumentPanelPopup* panel = view->getToolbar()->getOpenPanel();
    QVERIFY(panel);
    QCOMPARE(panel, view->getToolbar()->findPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo));
    panel->close();
    fixture.settle();

    // A document without labels: no page item, the number in the field
    pdf::PDFAppDocumentTab* simple = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("NoLabels.pdf"), 2));
    QVERIFY(simple);
    QVERIFY(!simple->getView()->hasPageLabels());
    QCOMPARE(simple->getView()->getPageLabel(1), QStringLiteral("2"));
    QVERIFY(simple->getView()->getStatusBar()->getPageText().isEmpty());
}

void AppDocumentViewTest::findPageIndexHostile()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Hostile.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    const QString texts[] = { QString(), QStringLiteral(" "), QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("4"), QStringLiteral("9223372036854775807"),
                              QStringLiteral("99999999999999999999999"), QString(10000, QLatin1Char('1')), QStringLiteral("1") + QChar(0) + QStringLiteral("2"), QString(QChar(0x202E)) + QStringLiteral("1"), QStringLiteral("0x2") };
    for (const QString& text : texts)
    {
        QVERIFY2(!view->findPageIndex(text), qPrintable(text.left(32)));
    }
    QCOMPARE(view->findPageIndex(QStringLiteral("3")), std::make_optional(pdf::PDFInteger(2)));

    // Out of range indices are ignored
    view->goToPage(-1);
    view->goToPage(3);
    view->goToPage(std::numeric_limits<pdf::PDFInteger>::max());
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    QCOMPARE(view->getPageLabel(-5), QString());

    // Selected pages of package C are bounded to the document
    QSignalSpy selectedSpy(view, &pdf::PDFAppDocumentView::selectedPagesChanged);
    view->setSelectedPages({ 2, -1, 0, 2, 17 });
    QCOMPARE(view->getSelectedPages(), std::vector<pdf::PDFInteger>({ 0, 2 }));
    QCOMPARE(selectedSpy.count(), 1);
    view->setSelectedPages({});
    QCOMPARE(view->getSelectedPages(), std::vector<pdf::PDFInteger>({ 0 }));
}

void AppDocumentViewTest::colorModesAndAnnotations()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ColorModes.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFDrawWidgetProxy* proxy = view->getProxy();

    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::colorModeNormal())->isChecked());
    const std::pair<QString, pdf::PDFRenderer::Feature> modes[] =
    {
        { pdf::PDFAppCommands::colorModeInverted(), pdf::PDFRenderer::ColorAdjust_Invert },
        { pdf::PDFAppCommands::colorModeGrayscale(), pdf::PDFRenderer::ColorAdjust_Grayscale },
        { pdf::PDFAppCommands::colorModeHighContrast(), pdf::PDFRenderer::ColorAdjust_HighContrast },
        { pdf::PDFAppCommands::colorModeBitonal(), pdf::PDFRenderer::ColorAdjust_Bitonal },
        { pdf::PDFAppCommands::colorModeCustom(), pdf::PDFRenderer::ColorAdjust_CustomColors }
    };
    for (const auto& mode : modes)
    {
        ViewTestHelper::trigger(fixture, mode.first);
        QVERIFY(proxy->getFeatures().testFlag(mode.second));
        QVERIFY(fixture.window->getCommandAction(mode.first)->isChecked());
        QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::colorModeNormal())->isChecked());
    }

    // The colors of Custom colors and the strength of the modes from the settings
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("#00FF00")).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::colorModesBitonalThreshold(), 50).isError());
    QTRY_COMPARE(view->getCMSManager()->getSettings().foregroundColor, QColor(0, 255, 0));
    QCOMPARE(view->getCMSManager()->getSettings().bitonalThreshold, 50);

    // A damaged color of the database gives the default color
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("not a color")).isError());
    QTRY_COMPARE(view->getCMSManager()->getSettings().foregroundColor, QColor(0xFF, 0xB0, 0x00));

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::colorModeNormal());
    QVERIFY(!(proxy->getFeatures() & pdf::PDFRenderer::Features(pdf::PDFRenderer::ColorAdjust_Invert | pdf::PDFRenderer::ColorAdjust_CustomColors)));

    // Show annotations is the setting viewing.showAnnotations
    QVERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DisplayAnnotations));
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::showAnnotations())->isChecked());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::showAnnotations());
    QVERIFY(!proxy->getFeatures().testFlag(pdf::PDFRenderer::DisplayAnnotations));
    QVERIFY(!fixture.context->getSettings().getBool(pdf::PDFSettingKeys::viewingShowAnnotations()));
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::showAnnotations())->isChecked());

    // The developer overlays of package F stay, when the settings are applied
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedShowRenderTimes(), true).isError());
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DisplayTimes));
    view->applySettings();
    QVERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::DisplayTimes));
}

void AppDocumentViewTest::settingsApplied()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFDrawWidgetProxy* proxy = view->getProxy();
    pdf::PDFSettingsRepository settings = fixture.context->getSettings();

    // Defaults
    QVERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));
    QCOMPARE(proxy->getRendererEngine(), pdf::RendererEngine::Blend2D_MultiThread);
    QVERIFY(view->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QCOMPARE(settings.getInteger(pdf::PDFSettingKeys::cacheThumbnails()), qint64(65536)); // 64 MB as the old Editor (owner, 23. 10. 2026)

    // A change of another connection arrives by the notifier
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingAntialiasing(), false).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingClipToCropBox(), false).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingEngine(), QStringLiteral("qpainter")).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::formsHighlightFields(), false).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::formsAllowEditReadOnly(), true).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::cmsSystem(), QStringLiteral("generic")).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingColorTolerance(), 0.05).isError());
    QTRY_VERIFY(!proxy->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));
    QTRY_COMPARE(proxy->getRendererEngine(), pdf::RendererEngine::QPainter);
    QVERIFY(!proxy->getFeatures().testFlag(pdf::PDFRenderer::ClipToCropBox));
    QVERIFY(!view->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QVERIFY(view->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));
    QCOMPARE(view->getCMSManager()->getSettings().system, pdf::PDFCMSSettings::System::Generic);
    QCOMPARE(proxy->getMeshQualitySettings().tolerance, 0.05);

    // Every option of color management
    const std::tuple<QString, QString, pdf::RenderingIntent, pdf::PDFCMSSettings::Accuracy, pdf::PDFCMSSettings::ColorAdaptationXYZ> options[] =
    {
        { QStringLiteral("perceptual"), QStringLiteral("low"), pdf::RenderingIntent::Perceptual, pdf::PDFCMSSettings::Accuracy::Low, pdf::PDFCMSSettings::ColorAdaptationXYZ::None },
        { QStringLiteral("relativeColorimetric"), QStringLiteral("high"), pdf::RenderingIntent::RelativeColorimetric, pdf::PDFCMSSettings::Accuracy::High, pdf::PDFCMSSettings::ColorAdaptationXYZ::XYZScaling },
        { QStringLiteral("absoluteColorimetric"), QStringLiteral("medium"), pdf::RenderingIntent::AbsoluteColorimetric, pdf::PDFCMSSettings::Accuracy::Medium, pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT97 },
        { QStringLiteral("saturation"), QStringLiteral("low"), pdf::RenderingIntent::Saturation, pdf::PDFCMSSettings::Accuracy::Low, pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT02 },
        { QStringLiteral("auto"), QStringLiteral("medium"), pdf::RenderingIntent::Auto, pdf::PDFCMSSettings::Accuracy::Medium, pdf::PDFCMSSettings::ColorAdaptationXYZ::Bradford }
    };
    const QString adaptations[] = { QStringLiteral("none"), QStringLiteral("xyzScaling"), QStringLiteral("cat97"), QStringLiteral("cat02"), QStringLiteral("bradford") };
    int adaptationIndex = 0;
    for (const auto& option : options)
    {
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::cmsRenderingIntent(), std::get<0>(option)).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::cmsAccuracy(), std::get<1>(option)).isError());
        QVERIFY(!settings.setValue(pdf::PDFSettingKeys::cmsColorAdaptation(), adaptations[adaptationIndex++]).isError());
        view->applySettings();
        QCOMPARE(view->getCMSManager()->getSettings().intent, std::get<2>(option));
        QCOMPARE(view->getCMSManager()->getSettings().accuracy, std::get<3>(option));
        QCOMPARE(view->getCMSManager()->getSettings().colorAdaptationXYZ, std::get<4>(option));
    }

    // The threads of the process and the prefetch of the pages
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingMultithreading(), QStringLiteral("singleThread")).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingPrefetchPages(), false).isError());
    view->applySettings();
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingMultithreading(), QStringLiteral("maximumThreads")).isError());
    view->applySettings();
    QVERIFY(!settings.resetValue(pdf::PDFSettingKeys::renderingMultithreading()).isError());
    QVERIFY(!settings.resetValue(pdf::PDFSettingKeys::renderingPrefetchPages()).isError());
    view->applySettings();

    // The view renders with every engine
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingEngine(), QStringLiteral("blend2dSingleThread")).isError());
    QTRY_COMPARE(proxy->getRendererEngine(), pdf::RendererEngine::Blend2D_SingleThread);
    QVERIFY(!ViewTestHelper::render(view).isNull());

    // Restore defaults of the sections give the defaults back
    QVERIFY(!settings.resetValue(pdf::PDFSettingKeys::renderingAntialiasing()).isError());
    QTRY_VERIFY(proxy->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));
}

void AppDocumentViewTest::sidebarToggle()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Sidebar.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // viewing.showSidebar = true: the sidebar is shown, F4 hides it (198:6178)
    QVERIFY(view->isSidebarVisible());
    QVERIFY(view->getSidebar()->isVisible());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::toggleSidebar())->isChecked());
    const int canvasWidth = view->getPdfWidget()->width();

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::toggleSidebar());
    QVERIFY(!view->isSidebarVisible());
    QVERIFY(!view->getSidebar()->isVisible());
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::toggleSidebar())->isChecked());
    QTRY_VERIFY(view->getPdfWidget()->width() > canvasWidth);

    // The button of the toolbar
    QTest::mouseClick(view->getToolbar()->getCommandButton(pdf::PDFAppCommands::toggleSidebar()), Qt::LeftButton);
    fixture.settle();
    QVERIFY(view->isSidebarVisible());
    QTRY_COMPARE(view->getSidebar()->width(), pdf::PDFAppDocumentSidebar::DEFAULT_WIDTH);

    // A new document without the sidebar (Settings › General)
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::viewingShowSidebar(), false).isError());
    pdf::PDFAppDocumentTab* second = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Sidebar2.pdf"), 2));
    QVERIFY(second);
    QVERIFY(!second->getView()->isSidebarVisible());
    QVERIFY(!second->getView()->getSidebar()->isVisible());
}

void AppDocumentViewTest::fullScreen()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("FullScreen.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fullScreen());
    QVERIFY(view->isFullScreen());
    QVERIFY(fixture.window->isFullScreenMode());
    QVERIFY(fixture.window->getTopBar()->isHidden());
    QVERIFY(view->getToolbar()->isHidden());
    QVERIFY(view->getStatusBar()->isHidden());
    QVERIFY(view->getSidebar()->isHidden());
    QVERIFY(view->isSidebarVisible());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::fullScreen())->isChecked());

    // pdf4qt-document-full-screen 1234:29322: only the canvas - it covers the whole
    // window (no top bar, toolbar, sidebar, splitter or status bar), the first page
    // centered 24 px below the top, no hint of Esc
    fixture.settle();
    {
        QWidget* pdfWidget = view->getPdfWidget();
        const QRect canvasRect(pdfWidget->mapTo(fixture.window, QPoint(0, 0)), pdfWidget->size());
        QCOMPARE(canvasRect, fixture.window->rect());
        view->goToPage(0);
        fixture.settle();
        const pdf::PDFWidgetSnapshot snapshot = view->getProxy()->getSnapshot();
        QVERIFY(!snapshot.items.empty());
        const QRect page = snapshot.items.front().rect.toRect();
        QCOMPARE(snapshot.items.front().pageIndex, pdf::PDFInteger(0));
        QCOMPARE(page.top(), 24);
        QWidget* drawWidget = view->getPdfWidget()->getDrawWidget()->getWidget();
        QVERIFY(qAbs(page.center().x() - drawWidget->width() / 2) <= 1);
        for (QSplitterHandle* handle : view->findChildren<QSplitterHandle*>())
        {
            QVERIFY(!handle->isVisible());
        }
        for (QLabel* label : fixture.window->findChildren<QLabel*>())
        {
            QVERIFY(!label->isVisible() || !label->text().contains(QStringLiteral("Esc")));
        }
    }

    // Ctrl+G does not open a panel of the hidden toolbar
    view->openGoToPage();
    fixture.settle();
    QVERIFY(!view->getToolbar()->getOpenPanel());

    // Esc leaves the full screen
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QTest::keyClick(canvas, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!view->isFullScreen());
    QVERIFY(!fixture.window->isFullScreenMode());
    QVERIFY(!fixture.window->getTopBar()->isHidden());
    QVERIFY(!view->getToolbar()->isHidden());
    QVERIFY(!view->getStatusBar()->isHidden());
    QVERIFY(!view->getSidebar()->isHidden());

    // F11 again, then the command again
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fullScreen());
    QVERIFY(view->isFullScreen());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fullScreen());
    QVERIFY(!view->isFullScreen());
    QVERIFY(!fixture.window->isFullScreenMode());
}

void AppDocumentViewTest::fullScreenTabSwitch()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* first = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("First.pdf"), 2));
    pdf::PDFAppDocumentTab* second = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 2));
    QVERIFY(first && second);
    QCOMPARE(fixture.currentDocumentTab(), second);

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fullScreen());
    QVERIFY(second->getView()->isFullScreen());

    // Another tab ends the full screen of the hidden view
    fixture.window->activateTab(first);
    fixture.settle();
    QVERIFY(!second->getView()->isFullScreen());
    QVERIFY(!fixture.window->isFullScreenMode());
    QVERIFY(!fixture.window->getTopBar()->isHidden());
}

void AppDocumentViewTest::fullScreenTabClosed()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Closed.pdf"), 2));
    QVERIFY(tab);
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::fullScreen());
    QVERIFY(fixture.window->isFullScreenMode());

    // The tab deleted in the full screen gives the window back its top bar
    delete tab;
    fixture.settle();
    QVERIFY(!fixture.window->isFullScreenMode());
    QVERIFY(!fixture.window->getTopBar()->isHidden());
}

void AppDocumentViewTest::statusBar()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Status.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();

    QWidget* trailing = new QWidget();
    trailing->setFixedSize(134, 22);
    view->addStatusItem(trailing, pdf::PDFAppDocumentStatusBar::Area::Trailing);
    QWidget* leading = new QWidget();
    leading->setFixedSize(40, 22);
    view->addStatusItem(leading, pdf::PDFAppDocumentStatusBar::Area::Leading);
    view->addStatusItem(nullptr, pdf::PDFAppDocumentStatusBar::Area::Leading);
    fixture.settle();
    QCOMPARE(trailing->parentWidget(), statusBar);

    // status-bar 193:1319: the trailing item 8 px from the edge, the leading after the message
    QCOMPARE(trailing->mapTo(statusBar, QPoint(trailing->width(), 0)).x(), statusBar->width() - 8);
    QVERIFY(leading->x() < statusBar->width() / 2);

    // A leading item deleted by its package: the next one is still leading
    delete leading;
    QWidget* nextLeading = new QWidget();
    nextLeading->setFixedSize(40, 22);
    view->addStatusItem(nextLeading, pdf::PDFAppDocumentStatusBar::Area::Leading);
    fixture.settle();
    QVERIFY(nextLeading->x() < statusBar->width() / 2);
    QCOMPARE(trailing->mapTo(statusBar, QPoint(trailing->width(), 0)).x(), statusBar->width() - 8);

    // A timed message gives the permanent message back
    statusBar->showMessage(QStringLiteral("Copied"), 50);
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Copied"));
    QTRY_COMPARE(statusBar->getMessage(), QStringLiteral("Ready"));

    // The colors of the design: bg/sidebar and the border border/default at the top
    const QImage image = ViewTestHelper::render(statusBar);
    QCOMPARE(ViewTestHelper::pixel(image, QPoint(statusBar->width() / 2, 14)), ViewTestHelper::token(pdf::PDFThemeColors::BgSidebar));
    QCOMPARE(ViewTestHelper::pixel(image, QPoint(statusBar->width() / 2, 0)), ViewTestHelper::token(pdf::PDFThemeColors::BorderDefault));

    // Messages of the tools (the screenshot tool) are shown in the status bar
    QMetaObject::invokeMethod(view->getToolManager(), "messageDisplayRequest", Q_ARG(QString, QStringLiteral("Tool message")), Q_ARG(int, 0));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Tool message"));
}

void AppDocumentViewTest::infoBars()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("InfoBars.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QSignalSpy actionSpy(view, &pdf::PDFAppDocumentView::infoBarActionTriggered);

    // link-blocked 1028:24277: an error with two buttons and the close button
    pdf::PDFAppInfoBarRequest request;
    request.id = QStringLiteral("link-blocked");
    request.tone = pdf::PDFAppInfoBarTone::Error;
    request.title = QStringLiteral("The web link was not opened – web links are turned off");
    request.actions = { { QStringLiteral("copy"), QStringLiteral("Copy Link") }, { QStringLiteral("settings"), QStringLiteral("Security Settings") } };
    view->showInfoBar(request);
    view->showInfoBar(request);
    fixture.settle();
    QCOMPARE(view->getInfoBarIds(), QStringList({ QStringLiteral("link-blocked") }));

    pdf::PDFAppInfoBar* infoBar = view->findInfoBarWidget(QStringLiteral("link-blocked"));
    QVERIFY(infoBar);
    QVERIFY(infoBar->isVisible());
    QCOMPARE(infoBar->getActionButtons().size(), size_t(2));
    QVERIFY(infoBar->getCloseButton());
    QVERIFY(!infoBar->getProgressBar());

    // The card: 48 high, 12 px below the toolbar, centered over the canvas
    const QRect card = QRect(infoBar->mapTo(view, infoBar->getCardRect().topLeft()), infoBar->getCardRect().size());
    const QRect canvas = QRect(view->getPdfWidget()->mapTo(view, QPoint(0, 0)), view->getPdfWidget()->size());
    QCOMPARE(card.height(), pdf::PDFAppInfoBar::CARD_HEIGHT);
    QCOMPARE(card.top(), canvas.top() + pdf::PDFAppDocumentView::INFO_BAR_TOP_GAP);
    QVERIFY(qAbs(card.center().x() - canvas.center().x()) <= 1);
    QVERIFY(card.width() <= canvas.width() - 24);

    // The margins of the shadow are not a part of the bar for the pointer: the
    // canvas below them gets the clicks; the shadow is a sibling below the bar
    QWidget* canvasArea = view->getPdfWidget()->parentWidget();
    const QRect cardRect = infoBar->getCardRect();
    for (const QPoint& margin : { QPoint(cardRect.center().x(), cardRect.bottom() + 10), QPoint(cardRect.left() - 8, cardRect.center().y()), QPoint(cardRect.right() + 8, cardRect.center().y()) })
    {
        QWidget* hit = canvasArea->childAt(infoBar->mapTo(canvasArea, margin));
        QVERIFY(hit);
        QVERIFY(hit == view->getPdfWidget() || view->getPdfWidget()->isAncestorOf(hit));
    }
    QWidget* cardHit = canvasArea->childAt(infoBar->mapTo(canvasArea, cardRect.center()));
    QVERIFY(cardHit == infoBar || infoBar->isAncestorOf(cardHit));
    pdf::PDFAppInfoBarShadow* shadow = canvasArea->findChild<pdf::PDFAppInfoBarShadow*>();
    QVERIFY(shadow);
    QVERIFY(shadow->isVisible());
    QCOMPARE(shadow->geometry(), infoBar->geometry());
    QVERIFY(canvasArea->children().indexOf(shadow) < canvasArea->children().indexOf(infoBar));

    // The colors of the card: bg/surface inside, no leak color (the bar is masked to
    // its card: QWidget::render would place the card at the origin of the image)
    QImage image(infoBar->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    infoBar->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren | QWidget::IgnoreMask));
    QCOMPARE(ViewTestHelper::pixel(image, infoBar->getCardRect().topLeft() + QPoint(14, 2)), ViewTestHelper::token(pdf::PDFThemeColors::BgSurface));
    QCOMPARE(ViewTestHelper::countLeakPixels(image), 0);

    // The buttons report their actions, the bar stays
    QTest::mouseClick(infoBar->getActionButtons().front(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.back().at(0).toString(), QStringLiteral("link-blocked"));
    QCOMPARE(actionSpy.back().at(1).toString(), QStringLiteral("copy"));
    QVERIFY(view->findInfoBar(QStringLiteral("link-blocked")));

    // A second bar below the first one; the progress line of the tone Progress
    pdf::PDFAppInfoBarRequest printing;
    printing.id = QStringLiteral("printing");
    printing.tone = pdf::PDFAppInfoBarTone::Progress;
    printing.title = QStringLiteral("Printing on HP LaserJet M404dn");
    printing.text = QStringLiteral("Page 313 of copy 1 · 5 of 20 pages");
    printing.iconName = QStringLiteral("printer");
    printing.progress = 25;
    printing.isClosable = false;
    printing.actions = { { QStringLiteral("stop"), QStringLiteral("Stop"), QStringLiteral("stop") } };
    view->showInfoBar(printing);
    fixture.settle();
    pdf::PDFAppInfoBar* printingBar = view->findInfoBarWidget(QStringLiteral("printing"));
    QVERIFY(printingBar);
    QVERIFY(printingBar->getProgressBar());
    QCOMPARE(printingBar->getProgressBar()->value(), 25);
    QCOMPARE(printingBar->getProgressBar()->width(), pdf::PDFAppInfoBar::PROGRESS_WIDTH);
    QVERIFY(!printingBar->getCloseButton());
    const int secondTop = printingBar->mapTo(view, printingBar->getCardRect().topLeft()).y();
    QCOMPARE(secondTop, card.top() + pdf::PDFAppInfoBar::CARD_HEIGHT + pdf::PDFAppDocumentView::INFO_BAR_GAP);

    // btn-stop 1030:25657 has the icon stop 16 before its text; a button without an icon has none
    QCOMPARE(printingBar->getActionButtons().size(), size_t(1));
    QVERIFY(!printingBar->getActionButtons().front()->icon().isNull());
    QCOMPARE(printingBar->getActionButtons().front()->iconSize(), QSize(16, 16));
    QVERIFY(infoBar->getActionButtons().front()->icon().isNull());

    // text 1030:25657: the title (16) and the second line (13) 1 px apart in the
    // middle of the card (y 9 and 26), not spread over its height
    {
        QWidget* title = printingBar->findChild<QWidget*>(QStringLiteral("infoBarTitle"));
        QWidget* text = printingBar->findChild<QWidget*>(QStringLiteral("infoBarText"));
        QVERIFY(title && text);
        const QPoint cardTopLeft = printingBar->getCardRect().topLeft();
        const int titleTop = title->mapTo(printingBar, QPoint(0, 0)).y() - cardTopLeft.y();
        const int textTop = text->mapTo(printingBar, QPoint(0, 0)).y() - cardTopLeft.y();
        QVERIFY2(qAbs(titleTop - 9) <= 1, qPrintable(QString::number(titleTop)));
        QCOMPARE(textTop - (titleTop + title->height()), 1);
        QVERIFY2(qAbs(textTop - 26) <= 1, qPrintable(QString::number(textTop)));
        QVERIFY(title->height() <= 17);
        QVERIFY(text->height() <= 14);
    }

    // Progress updates replace the content
    printing.progress = 80;
    view->showInfoBar(printing);
    QCOMPARE(printingBar->getProgressBar()->value(), 80);
    QCOMPARE(view->findInfoBar(QStringLiteral("printing"))->progress, 80);

    // The close button hides the bar and is reported with an empty action
    QTest::mouseClick(infoBar->getCloseButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(view->getInfoBarIds(), QStringList({ QStringLiteral("printing") }));
    QCOMPARE(actionSpy.back().at(1).toString(), QString());
    QTRY_COMPARE(printingBar->mapTo(view, printingBar->getCardRect().topLeft()).y(), card.top());

    // A narrow window: the bar is not wider than the canvas
    fixture.window->resize(720, 480);
    fixture.settle();
    QTRY_VERIFY(printingBar->getCardRect().width() <= view->getPdfWidget()->width() - 24);

    view->hideInfoBar(QStringLiteral("printing"));
    view->hideInfoBar(QStringLiteral("unknown"));
    view->showInfoBar(pdf::PDFAppInfoBarRequest());
    QVERIFY(view->getInfoBarIds().isEmpty());

    // The shadows go with their bars
    fixture.settle();
    QVERIFY(canvasArea->findChildren<pdf::PDFAppInfoBarShadow*>().isEmpty());
}

void AppDocumentViewTest::infoBarsDeletion()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("InfoBarsDeletion.pdf"), 1));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();

    pdf::PDFAppInfoBarRequest request;
    request.id = QStringLiteral("bar");
    request.title = QStringLiteral("Title");
    request.actions = { { QStringLiteral("hide"), QStringLiteral("Hide") }, { QStringLiteral("replace"), QStringLiteral("Replace") }, { QStringLiteral("close"), QStringLiteral("Close tab") } };
    view->showInfoBar(request);
    fixture.settle();

    // The owner hides or replaces the bar from the slot of its button
    connect(view.data(), &pdf::PDFAppDocumentView::infoBarActionTriggered, view.data(), [&](const QString& id, const QString& action)
    {
        if (action == QStringLiteral("hide"))
        {
            view->hideInfoBar(id);
        }
        else if (action == QStringLiteral("replace"))
        {
            pdf::PDFAppInfoBarRequest replaced = request;
            replaced.title = QStringLiteral("Replaced");
            replaced.actions = { { QStringLiteral("close"), QStringLiteral("Close tab") } };
            view->showInfoBar(replaced);
        }
        else if (action == QStringLiteral("close"))
        {
            delete tab;
        }
    });

    pdf::PDFAppInfoBar* bar = view->findInfoBarWidget(QStringLiteral("bar"));
    QTest::mouseClick(bar->getActionButtons().at(1), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(view->findInfoBar(QStringLiteral("bar"))->title, QStringLiteral("Replaced"));
    QCOMPARE(view->findInfoBarWidget(QStringLiteral("bar"))->getActionButtons().size(), size_t(1));

    request.actions = { { QStringLiteral("hide"), QStringLiteral("Hide") } };
    view->showInfoBar(request);
    fixture.settle();
    QPointer<pdf::PDFAppInfoBar> hiddenBar = view->findInfoBarWidget(QStringLiteral("bar"));
    QTest::mouseClick(hiddenBar->getActionButtons().front(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(view->getInfoBarIds().isEmpty());
    QVERIFY(!hiddenBar);

    // The tab deleted from the slot of a button of its bar
    request.actions = { { QStringLiteral("close"), QStringLiteral("Close tab") } };
    view->showInfoBar(request);
    fixture.settle();
    QTest::mouseClick(view->findInfoBarWidget(QStringLiteral("bar"))->getActionButtons().front(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!view);
}

void AppDocumentViewTest::viewStateText()
{
    // Round trip of every value
    pdf::PDFAppViewState state;
    state.layout = pdf::PDFAppViewLayout::TwoColumns;
    state.firstPageRight = true;
    state.zoomMode = pdf::PDFAppViewZoomMode::Custom;
    state.zoom = 1.5;
    state.rotation = 270;
    state.sidebarVisible = false;
    state.sidebarWidth = 420;
    state.panelId = QStringLiteral("thumbnails");
    QCOMPARE(pdf::PDFAppViewState::fromText(state.toText()), state);
    QVERIFY(pdf::PDFAppViewState().isEmpty());
    QCOMPARE(pdf::PDFAppViewState::fromText(pdf::PDFAppViewState().toText()), pdf::PDFAppViewState());

    for (pdf::PDFAppViewZoomMode mode : { pdf::PDFAppViewZoomMode::FitPage, pdf::PDFAppViewZoomMode::FitWidth, pdf::PDFAppViewZoomMode::FitHeight })
    {
        pdf::PDFAppViewState fit;
        fit.zoomMode = mode;
        QCOMPARE(pdf::PDFAppViewState::fromText(fit.toText()), fit);
    }

    // Untrusted text of the database: nothing invalid is accepted
    const QString hostile[] =
    {
        QString(),
        QStringLiteral("garbage"),
        QStringLiteral("[1,2,3]"),
        QStringLiteral("{\"layout\":"),
        QStringLiteral("{\"layout\":\"unknown\",\"zoom\":-1,\"rotation\":45,\"sidebarWidth\":5,\"panel\":\"../etc\",\"firstPageRight\":\"yes\"}"),
        QStringLiteral("{\"zoom\":1e308,\"rotation\":360,\"sidebarWidth\":1e9,\"panel\":\"ThumbNails\"}"),
        QStringLiteral("{\"zoom\":\"2\",\"rotation\":90.5,\"sidebar\":1,\"panel\":\"") + QString(100, QLatin1Char('a')) + QStringLiteral("\"}"),
        QStringLiteral("{\"zoomMode\":\"custom\"}"),
        QStringLiteral("{\"layout\":\"twoPages\"") + QString(5000, QLatin1Char(' ')) + QStringLiteral("}")
    };
    for (const QString& text : hostile)
    {
        QVERIFY2(pdf::PDFAppViewState::fromText(text).isEmpty(), qPrintable(text.left(64)));
    }

    // Valid values beside invalid ones are kept
    const pdf::PDFAppViewState mixed = pdf::PDFAppViewState::fromText(QStringLiteral("{\"layout\":\"twoPages\",\"zoom\":\"x\",\"rotation\":180,\"unknown\":[1,2]}"));
    QCOMPARE(mixed.layout, std::make_optional(pdf::PDFAppViewLayout::TwoPages));
    QCOMPARE(mixed.rotation, std::make_optional(180));
    QVERIFY(!mixed.zoom);
    QVERIFY(pdf::PDFAppViewState::isValidPanelId(QStringLiteral("my-bookmarks2")));
    QVERIFY(!pdf::PDFAppViewState::isValidPanelId(QStringLiteral("a b")));
    QVERIFY(!pdf::PDFAppViewState::isValidPanelId(QString()));

    // Out of range values of the application are bounded in the text
    pdf::PDFAppViewState large;
    large.zoom = 1000.0;
    large.sidebarWidth = 100000;
    const pdf::PDFAppViewState bounded = pdf::PDFAppViewState::fromText(large.toText());
    QCOMPARE(bounded.zoom, std::make_optional(pdf::PDFAppViewState::MAXIMUM_ZOOM));
    QCOMPARE(bounded.sidebarWidth, std::make_optional(pdf::PDFAppViewState::MAXIMUM_SIDEBAR_WIDTH));
    QVERIFY(large.toText().size() < pdf::PDFAppViewState::MAXIMUM_TEXT_LENGTH);
}

void AppDocumentViewTest::viewStateApply()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ViewState.pdf"), 4));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The state of the view
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutTwoPages());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::firstPageRight());
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::rotateViewRight());
    view->setZoom(1.75);
    pdf::PDFAppViewState state = view->getViewState();
    QCOMPARE(state.layout, std::make_optional(pdf::PDFAppViewLayout::TwoPages));
    QCOMPARE(state.firstPageRight, std::make_optional(true));
    QCOMPARE(state.rotation, std::make_optional(90));
    QCOMPARE(state.zoomMode, std::make_optional(pdf::PDFAppViewZoomMode::Custom));
    QCOMPARE(state.zoom, std::make_optional(1.75));
    QCOMPARE(state.sidebarVisible, std::make_optional(true));

    // Another document gets the state (package A restores it from Recent)
    pdf::PDFAppDocumentTab* second = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ViewState2.pdf"), 4));
    QVERIFY(second);
    pdf::PDFAppDocumentView* secondView = second->getView();
    state.sidebarVisible = false;
    secondView->applyViewState(pdf::PDFAppViewState::fromText(state.toText()));
    QCOMPARE(secondView->getViewLayout(), pdf::PDFAppViewLayout::TwoPages);
    QCOMPARE(secondView->getProxy()->getPageLayout(), pdf::PageLayout::TwoPagesRight);
    QCOMPARE(secondView->getRotation(), 90);
    QCOMPARE(secondView->getZoom(), 1.75);
    QVERIFY(!secondView->isSidebarVisible());

    // A fit is kept as the fit
    pdf::PDFAppViewState fit;
    fit.zoomMode = pdf::PDFAppViewZoomMode::FitHeight;
    fit.rotation = 0;
    secondView->applyViewState(fit);
    QCOMPARE(secondView->getProxy()->getZoomFitHint(), std::make_optional(pdf::PDFDrawWidgetProxy::ZoomHint::FitHeight));
    QCOMPARE(secondView->getRotation(), 0);
    QCOMPARE(secondView->getViewState().zoomMode, std::make_optional(pdf::PDFAppViewZoomMode::FitHeight));

    // An unknown panel and an empty state change nothing
    pdf::PDFAppViewState unknownPanel;
    unknownPanel.panelId = QStringLiteral("unknown-panel");
    secondView->applyViewState(unknownPanel);
    secondView->applyViewState(pdf::PDFAppViewState());
    QCOMPARE(secondView->getViewLayout(), pdf::PDFAppViewLayout::TwoPages);
}

void AppDocumentViewTest::filePanel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("FilePanel.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QToolButton* fileButton = toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File);

    // The click on File opens the panel 4 px below the toolbar at the button
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->getOpenPanel();
    QVERIFY(panel);
    QVERIFY(qobject_cast<pdf::PDFAppDocumentFilePanel*>(panel));
    QVERIFY(fileButton->isChecked());
    const QRect panelRect(panel->mapToGlobal(panel->getPanelRect().topLeft()), panel->getPanelRect().size());
    QCOMPARE(panelRect.top(), toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + pdf::PDFAppDocumentToolbar::PANEL_GAP);
    QCOMPARE(panelRect.left(), fileButton->mapToGlobal(QPoint(0, 0)).x());
    QCOMPARE(panelRect.width(), 300);

    // Tiles and rows of 196:2583
    for (const QString& commandId : { pdf::PDFAppCommands::save(), pdf::PDFAppCommands::saveAs(), pdf::PDFAppCommands::print(), pdf::PDFAppCommands::sendMail(),
                                      pdf::PDFAppCommands::properties(), pdf::PDFAppCommands::autoRefresh(), pdf::PDFAppCommands::lockDocument() })
    {
        QAbstractButton* button = panel->getCommandButton(commandId);
        QVERIFY2(button, qPrintable(commandId));
        QCOMPARE(button->isEnabled(), tab->isCommandEnabled(commandId));
    }
    QToolButton* saveTile = qobject_cast<QToolButton*>(panel->getCommandButton(pdf::PDFAppCommands::save()));
    QCOMPARE(saveTile->height(), pdf::PDFAppDocumentPanelPopup::TILE_HEIGHT);
    QCOMPARE(saveTile->text(), QStringLiteral("Save"));
    pdf::PDFAppPanelMenuItem* closeItem = panel->findChild<pdf::PDFAppPanelMenuItem*>(QStringLiteral("item:") + pdf::PDFAppCommands::closeTab());
    QVERIFY(closeItem);
    QCOMPARE(closeItem->height(), pdf::PDFAppPanelMenuItem::HEIGHT);
    QCOMPARE(closeItem->getShortcutText(), QKeySequence(Qt::CTRL | Qt::Key_W).toString(QKeySequence::NativeText));
    QCOMPARE(ViewTestHelper::countLeakPixels(ViewTestHelper::render(panel)), 0);

    // The click on File again closes it
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(!fileButton->isChecked());

    // Settings… opens the Settings of the Home tab
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppPanelMenuItem* settingsItem = panel->findChild<pdf::PDFAppPanelMenuItem*>(QStringLiteral("item:settings"));
    QVERIFY(settingsItem);
    QTest::mouseClick(settingsItem, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(fixture.window->getCurrentTab() != tab);
}

void AppDocumentViewTest::filePanelWithoutMail()
{
    // D2: the tile E-mail is hidden on a platform without e-mail (macOS), the
    // other tiles of the row share its place; the state is asked at each opening
    struct OverrideGuard
    {
        ~OverrideGuard() { pdf::PDFAppSendMail::setAvailableOverride(std::nullopt); }
    } overrideGuard;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("FilePanelMail.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QToolButton* fileButton = toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File);

    pdf::PDFAppSendMail::setAvailableOverride(false);
    QVERIFY(!pdf::PDFAppSendMail::isAvailable());
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->getOpenPanel();
    QVERIFY(qobject_cast<pdf::PDFAppDocumentFilePanel*>(panel));
    QAbstractButton* mailTile = panel->getCommandButton(pdf::PDFAppCommands::sendMail());
    QVERIFY(mailTile);
    QVERIFY(!mailTile->isVisibleTo(panel));
    QWidget* printTile = panel->getCommandButton(pdf::PDFAppCommands::print());
    QWidget* saveTile = panel->getCommandButton(pdf::PDFAppCommands::save());
    QVERIFY(printTile && saveTile && printTile->isVisible() && saveTile->isVisible());
    QCOMPARE(saveTile->x(), 0);
    QCOMPARE(printTile->geometry().right(), printTile->parentWidget()->width() - 1);
    QCOMPARE(ViewTestHelper::countLeakPixels(ViewTestHelper::render(panel)), 0);
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(!fileButton->isChecked());

    // E-mail available: four tiles, E-mail at the end of the row
    pdf::PDFAppSendMail::setAvailableOverride(true);
    QTest::mouseClick(fileButton, Qt::LeftButton);
    fixture.settle();
    QCOMPARE(toolbar->getOpenPanel(), panel);
    QVERIFY(mailTile->isVisible());
    QVERIFY(printTile->geometry().right() < mailTile->x());
    QCOMPARE(mailTile->geometry().right(), mailTile->parentWidget()->width() - 1);
}

void AppDocumentViewTest::filePanelCloseDocument()
{
    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("CloseDocument.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
    QVERIFY(panel);

    // Close document closes the tab, which holds the panel (the command runs later)
    pdf::PDFAppPanelMenuItem* closeItem = panel->findChild<pdf::PDFAppPanelMenuItem*>(QStringLiteral("item:") + pdf::PDFAppCommands::closeTab());
    QTest::mouseClick(closeItem, Qt::LeftButton);
    fixture.settle();
    fixture.closeMessageBoxes();
    QTRY_VERIFY(!tab);
}

void AppDocumentViewTest::viewPanel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ViewPanel.pdf"), 4));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentViewPanel* panel = qobject_cast<pdf::PDFAppDocumentViewPanel*>(view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View));
    QVERIFY(panel);
    fixture.settle();
    QCOMPARE(panel->getPanelRect().width(), 700);

    // Continuous and Normal are selected (196:2749)
    QVERIFY(panel->getCard(pdf::PDFAppCommands::layoutContinuous())->isSelected());
    QVERIFY(!panel->getCard(pdf::PDFAppCommands::layoutSinglePage())->isSelected());
    QVERIFY(panel->getCard(pdf::PDFAppCommands::colorModeNormal())->isSelected());
    // Color mode card 125 x 122 (the columns of 196:2749 overflow its panel by 2 px)
    QVERIFY(qAbs(panel->getCard(pdf::PDFAppCommands::layoutSinglePage())->width() - 125) <= 1);
    QCOMPARE(panel->getCard(pdf::PDFAppCommands::layoutSinglePage())->height(), 122);
    QVERIFY(panel->getShowAnnotationsCheckBox()->isChecked());
    QVERIFY(!panel->getFirstPageRightCheckBox()->isChecked());

    // A card changes the layout and the panel stays open
    QTest::mouseClick(panel->getCard(pdf::PDFAppCommands::layoutTwoColumns()), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(view->getViewLayout(), pdf::PDFAppViewLayout::TwoColumns);
    QVERIFY(panel->isVisible());
    QVERIFY(panel->getCard(pdf::PDFAppCommands::layoutTwoColumns())->isSelected());
    QVERIFY(!panel->getCard(pdf::PDFAppCommands::layoutContinuous())->isSelected());

    QTest::mouseClick(panel->getCard(pdf::PDFAppCommands::colorModeGrayscale()), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(view->getColorMode(), QStringLiteral("grayscale"));
    QVERIFY(panel->getCard(pdf::PDFAppCommands::colorModeGrayscale())->isSelected());

    // The check boxes
    QTest::mouseClick(panel->getFirstPageRightCheckBox(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(view->isFirstPageRight());
    QVERIFY(panel->getFirstPageRightCheckBox()->isChecked());
    QCOMPARE(view->getProxy()->getPageLayout(), pdf::PageLayout::TwoColumnRight);
    QTest::mouseClick(panel->getShowAnnotationsCheckBox(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!view->isShowingAnnotations());

    // Icons of the tiles: Magnifier has its own icon (Figma 1188:1549), not Zoom In
    QAbstractButton* magnifierTile = panel->getCommandButton(pdf::PDFAppCommands::magnifier());
    QVERIFY(magnifierTile);
    QCOMPARE(pdf::PDFThemeIcons::getIconName(magnifierTile->icon()), QStringLiteral("magnifier"));
    QCOMPARE(pdf::PDFThemeIcons::getIconName(panel->getCommandButton(pdf::PDFAppCommands::fitPage())->icon()), QStringLiteral("maximize"));

    // A tile closes the panel and runs its command
    QAbstractButton* rotate = panel->getCommandButton(pdf::PDFAppCommands::rotateViewRight());
    QVERIFY(rotate);
    QTest::mouseClick(rotate, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(view->getRotation(), 90);

    // Rendering settings opens Settings › Viewing in the Home tab
    view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    fixture.settle();
    QVERIFY(panel->isVisible());
    QTest::mouseClick(panel->getRenderingSettingsLink(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(fixture.window->getCurrentTab() != tab);
    pdf::PDFAppPage* page = fixture.window->getCurrentTab()->getPage();
    QVERIFY(page);
    QCOMPARE(page->getRoute().getSegments().value(0), QStringLiteral("settings"));
}

void AppDocumentViewTest::goToPanel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("GoTo.pdf"), 6));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::layoutSinglePage());

    // Ctrl+G: the Go to panel with the focused field of the current page
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::goToPage());
    pdf::PDFAppDocumentGoToPanel* panel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(view->getToolbar()->getOpenPanel());
    QVERIFY(panel);
    QCOMPARE(panel->getPanelRect().width(), 340);
    QTRY_VERIFY(panel->getPageField()->hasFocus());
    QCOMPARE(panel->getPageField()->text(), QStringLiteral("1"));
    QCOMPARE(panel->getPageField()->selectedText(), QStringLiteral("1"));
    QCOMPARE(panel->getPageField()->width(), 72);

    // An unknown page keeps the panel open with the error
    QTest::keyClicks(panel->getPageField(), QStringLiteral("abc"));
    QTest::keyClick(panel->getPageField(), Qt::Key_Return);
    fixture.settle();
    QVERIFY(panel->isVisible());
    QVERIFY(panel->getPageField()->isInvalid());

    // Typing clears the error; Enter goes to the page and closes the panel
    panel->getPageField()->selectAll();
    QTest::keyClicks(panel->getPageField(), QStringLiteral("4"));
    QVERIFY(!panel->getPageField()->isInvalid());
    QTest::keyClick(panel->getPageField(), Qt::Key_Return);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(3));

    // The button Go
    view->openGoToPage();
    fixture.settle();
    QCOMPARE(panel->getPageField()->text(), QStringLiteral("4"));
    panel->getPageField()->setText(QStringLiteral("6"));
    QTest::mouseClick(panel->getGoButton(), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(5));

    // The tiles: First, Next line
    view->openGoToPage();
    fixture.settle();
    QTest::mouseClick(panel->getCommandButton(pdf::PDFAppCommands::firstPage()), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    QVERIFY(panel->getCommandButton(pdf::PDFAppCommands::nextLine()));
    QVERIFY(panel->getCommandButton(pdf::PDFAppCommands::addBookmark()));
    QCOMPARE(panel->getCommandButton(pdf::PDFAppCommands::addBookmark())->isEnabled(), tab->isCommandEnabled(pdf::PDFAppCommands::addBookmark()));
}

void AppDocumentViewTest::goToPanelAutoGenerate()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("AutoGenerate.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentGoToPanel* panel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(tab->getView()->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo));
    QVERIFY(panel);

    // Generate automatically is the setting bookmarks.autoGenerate (off by default, O10)
    pdf::PDFAppPanelMenuItem* item = panel->getAutoGenerateItem();
    QVERIFY(item->isCheckable());
    QVERIFY(!item->isChecked());
    QTest::mouseClick(item, Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.context->getSettings().getBool(pdf::PDFSettingKeys::bookmarksAutoGenerate()));
    QVERIFY(item->isChecked());
    QVERIFY(panel->isVisible());
    QTest::mouseClick(item, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!fixture.context->getSettings().getBool(pdf::PDFSettingKeys::bookmarksAutoGenerate()));
}

void AppDocumentViewTest::closedPanelsNotRefreshed()
{
    // A closed panel (File, View, Go to) does not follow every scroll step of the
    // view (Go to read the database each time): it reads its states, when it opens
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Closed.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QPointer<pdf::PDFAppDocumentGoToPanel> panel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo));
    QVERIFY(panel);
    QVERIFY(!panel->getAutoGenerateItem()->isChecked());
    panel->close();
    fixture.settle();
    QVERIFY(!panel->isVisible());

    // The setting changed by another instance (no notification yet), the view scrolled
    {
        const QSignalBlocker blocker(fixture.context->getNotifier());
        QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::bookmarksAutoGenerate(), true).isError());
    }
    for (int page = 1; page < 5; ++page)
    {
        tab->getView()->goToPage(page);
        fixture.settle();
    }
    QVERIFY(!panel->getAutoGenerateItem()->isChecked());

    // Opened: the current state
    QCOMPARE(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo), panel.data());
    QVERIFY(panel->getAutoGenerateItem()->isChecked());

    // An open panel follows the view
    {
        const QSignalBlocker blocker(fixture.context->getNotifier());
        QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::bookmarksAutoGenerate(), false).isError());
    }
    tab->getView()->goToPage(0);
    fixture.settle();
    QVERIFY(!panel->getAutoGenerateItem()->isChecked());
}

void AppDocumentViewTest::panelKeyboard()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Keyboard.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
    QVERIFY(panel);
    fixture.settle();

    // The first control has the focus, the arrows move it, Enter runs a row
    QWidget* first = panel->focusWidget();
    QVERIFY(first);
    QTest::keyClick(panel, Qt::Key_Down);
    QVERIFY(panel->focusWidget() != first);
    QTest::keyClick(panel, Qt::Key_Up);
    QCOMPARE(panel->focusWidget(), first);

    // Esc closes the panel, the button is not checked
    QTest::keyClick(panel, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(!toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File)->isChecked());

    // One panel at a time
    pdf::PDFAppDocumentPanelPopup* viewPanel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    pdf::PDFAppDocumentPanelPopup* goToPanel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo);
    fixture.settle();
    QVERIFY(!viewPanel->isVisible());
    QVERIFY(goToPanel->isVisible());
    QCOMPARE(toolbar->getOpenPanel(), goToPanel);

    // A row runs by Enter (Next line tile is a tool button: Space)
    pdf::PDFAppPanelMenuItem* exportItem = goToPanel->findChild<pdf::PDFAppPanelMenuItem*>(QStringLiteral("item:") + pdf::PDFAppCommands::exportBookmarks());
    QVERIFY(exportItem);
    goToPanel->close();
    fixture.settle();

    // The hidden toolbar closes its panel
    toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    fixture.settle();
    QVERIFY(viewPanel->isVisible());
    toolbar->hide();
    fixture.settle();
    QVERIFY(!viewPanel->isVisible());
    QVERIFY(!toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::View));
}

void AppDocumentViewTest::panelDeletion()
{
    // The tab deleted with an open panel
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deletion1.pdf"), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentPanelPopup> panel = tab->getView()->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
        QVERIFY(panel);
        delete tab;
        fixture.settle();
        QVERIFY(!panel);
    }

    // The window deleted with an open panel and a queued command
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deletion2.pdf"), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentPanelPopup> panel = tab->getView()->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo);
        tab->getView()->requestCommand(pdf::PDFAppCommands::nextPage());
        tab->getView()->openSettings(QString());
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!panel);
    }

    // The panel deleted by the application while it is open, then the toolbar opens a new one
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deletion3.pdf"), 2));
        QVERIFY(tab);
        pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
        delete toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
        fixture.settle();
        QVERIFY(!toolbar->findPanel(pdf::PDFAppDocumentToolbar::Panel::File));
        QVERIFY(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File));
        fixture.settle();

        // The tab deleted from the closing signal of its panel
        connect(toolbar->getOpenPanel(), &pdf::PDFAppDocumentPanelPopup::closed, tab, [tab]() { delete tab; });
        QPointer<pdf::PDFAppDocumentView> view = tab->getView();
        toolbar->getOpenPanel()->close();
        fixture.settle();
        QTRY_VERIFY(!view);
    }

    // The Select menu open while the tab is deleted
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deletion4.pdf"), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentSelectMenu> menu = tab->getView()->getToolbar()->getSelectMenu();
        QTimer::singleShot(0, tab, [tab]() { delete tab; });
        menu->exec(QPoint(10, 10));
        fixture.settle();
        QVERIFY(!menu);
    }
}

void AppDocumentViewTest::panelHoverSwitch()
{
    // Menu bar: with a panel open, the pointer over another panel button opens its
    // panel at once (no click) - every pair of the panel buttons, the Select menu too
    using H = PanelSwitchHelper;
    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("HoverSwitch.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    const std::vector<QToolButton*> buttons = toolbar->getPanelButtons();
    QCOMPARE(buttons.size(), size_t(7));
    QCOMPARE(buttons.back(), toolbar->getSelectButton());

    // Tools is a panel of the menu bar too (step 6, 196:3650), before the Select button
    QCOMPARE(buttons.at(buttons.size() - 2), toolbar->getToolsButton());
    for (QToolButton* button : buttons)
    {
        QVERIFY2(button->isVisible() && button->isEnabled(), qPrintable(button->objectName()));
    }
    const int panelTop = toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + pdf::PDFAppDocumentToolbar::PANEL_GAP;

    // Without an open panel the pointer opens nothing
    H::hover(buttons.front());
    fixture.settle();
    QVERIFY(!toolbar->getOpenPanelButton());
    QVERIFY(!toolbar->switchPanel(buttons.at(1), false));

    for (QToolButton* from : buttons)
    {
        for (QToolButton* to : buttons)
        {
            if (from == to)
            {
                continue;
            }

            const QByteArray pair = (from->objectName() + QStringLiteral(" -> ") + to->objectName()).toUtf8();
            QVERIFY2(H::open(toolbar, from), pair.constData());
            fixture.settle();
            QVERIFY2(toolbar->getOpenPanelButton() == from, pair.constData());

            // The switch is done in the move itself: one panel, one marked button
            H::hover(to);
            QVERIFY2(toolbar->getOpenPanelButton() == to, pair.constData());
            QCOMPARE(H::countOpen(toolbar), 1);
            QVERIFY2(!from->isChecked() && !from->isDown(), pair.constData());
            QVERIFY2(to == toolbar->getSelectButton() ? to->isDown() : to->isChecked(), pair.constData());

            // The new panel lies 4 px below the toolbar at its button and has the keyboard
            QWidget* window = H::openWindow(toolbar);
            QVERIFY(window && window->isVisible());
            const QRect panel = window->geometry().marginsRemoved(pdf::PDFWidgetStyle::getPopoverMargins(window));
            QCOMPARE(panel.top(), panelTop);
            const QRect screen = to->screen()->availableGeometry();
            if (to != toolbar->getSelectButton() && to->mapToGlobal(QPoint(0, 0)).x() + panel.width() <= screen.right() + 1)
            {
                // A panel, which fits into the screen of the offscreen platform (800 px)
                QCOMPARE(panel.left(), to->mapToGlobal(QPoint(0, 0)).x());
            }
            QCOMPARE(QApplication::activePopupWidget(), window);
            if (to != toolbar->getSelectButton())
            {
                QVERIFY2(window->focusWidget() && window->isAncestorOf(window->focusWidget()), pair.constData());
            }

            // The queued closing of the old panel changes nothing
            fixture.settle();
            QVERIFY2(toolbar->getOpenPanelButton() == to, pair.constData());
            QVERIFY2(!from->isChecked() && !from->isDown(), pair.constData());
            H::closeAll(toolbar);
            fixture.settle();
            QVERIFY(!toolbar->getOpenPanelButton());
            QVERIFY(!to->isChecked() && !to->isDown());
        }
    }

    // The pointer over the open panel, its own button and buttons of no panel (zoom,
    // page navigation, the search field, sidebar) keeps the panel
    QToolButton* fileButton = toolbar->getMenuButton(H::Panel::File);
    QVERIFY(H::open(toolbar, fileButton));
    fixture.settle();
    QPointer<pdf::PDFAppDocumentPanelPopup> filePanel = toolbar->getOpenPanel();
    H::hover(filePanel);
    H::hover(fileButton);
    for (QWidget* widget : std::initializer_list<QWidget*>{ toolbar->getCommandButton(pdf::PDFAppCommands::zoomIn()), toolbar->getCommandButton(pdf::PDFAppCommands::zoomOut()),
                                                           toolbar->getCommandButton(pdf::PDFAppCommands::nextPage()), toolbar->getPageField(), toolbar->getZoomField(),
                                                           toolbar->getSearchField(), toolbar->getCommandButton(pdf::PDFAppCommands::toggleSidebar()),
                                                           toolbar->getCommandButton(pdf::PDFAppCommands::undo()) })
    {
        if (widget && widget->isVisible())
        {
            H::hover(widget);
            QVERIFY2(toolbar->getOpenPanelButton() == fileButton, qPrintable(widget->objectName()));
            QCOMPARE(H::countOpen(toolbar), 1);
        }
    }
    fixture.settle();
    QCOMPARE(toolbar->getOpenPanel(), filePanel.data());

    // A click on the button of the open panel closes it (the press is not replayed)
    H::click(fileButton);
    fixture.settle();
    QVERIFY(!toolbar->getOpenPanelButton());
    QVERIFY(!fileButton->isChecked());

    // A panel reached by the pointer closes by a click on its button too
    QToolButton* viewButton = toolbar->getMenuButton(H::Panel::View);
    QVERIFY(H::open(toolbar, fileButton));
    fixture.settle();
    H::hover(viewButton);
    QCOMPARE(toolbar->getOpenPanelButton(), viewButton);
    H::click(viewButton);
    fixture.settle();
    QVERIFY(!toolbar->getOpenPanelButton());
    QVERIFY(!viewButton->isChecked());

    // A click on another panel button switches (the press reaches the button)
    QVERIFY(H::open(toolbar, fileButton));
    fixture.settle();
    QTest::mouseClick(toolbar->getMenuButton(H::Panel::GoTo), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::GoTo));
    QCOMPARE(H::countOpen(toolbar), 1);

    // The Select menu reached by the pointer closes by a click on the Select button
    H::hover(toolbar->getSelectButton());
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getSelectButton());
    H::click(toolbar->getSelectButton());
    fixture.settle();
    QVERIFY(!toolbar->getOpenPanelButton());
    QVERIFY(!toolbar->getSelectButton()->isDown());
}

void AppDocumentViewTest::panelHoverSwitchSelectMenuExec()
{
    // The Select menu opened by its button runs QMenu::exec: the pointer over
    // another panel button opens the panel and the nested event loop ends
    using H = PanelSwitchHelper;
    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("HoverSwitchExec.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QToolButton* selectButton = toolbar->getSelectButton();
    QToolButton* viewButton = toolbar->getMenuButton(H::Panel::View);

    bool wasOpen = false;
    QTimer::singleShot(0, toolbar, [&]()
    {
        wasOpen = toolbar->getOpenPanelButton() == selectButton;
        H::hover(viewButton);
    });
    selectButton->showMenu();
    QVERIFY(wasOpen);
    QCOMPARE(toolbar->getOpenPanelButton(), viewButton);
    QVERIFY(!selectButton->isDown());
    fixture.settle();
    QCOMPARE(toolbar->getOpenPanelButton(), viewButton);

    // The keyboard from the menu of the button: Right opens the next panel (File
    // around the end), Left the previous one (Tools, step 6)
    H::closeAll(toolbar);
    fixture.settle();
    QTimer::singleShot(0, toolbar, [&]() { QTest::keyClick(toolbar->getSelectMenu(), Qt::Key_Right); });
    selectButton->showMenu();
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::File));
    H::closeAll(toolbar);
    fixture.settle();
    QTimer::singleShot(0, toolbar, [&]() { QTest::keyClick(toolbar->getSelectMenu(), Qt::Key_Left); });
    selectButton->showMenu();
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::Tools));

    // The tab deleted while the menu of the button runs, after the switch
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    H::closeAll(toolbar);
    fixture.settle();
    QTimer::singleShot(0, toolbar, [&]()
    {
        H::hover(viewButton);
        delete tabGuard.data();
    });
    selectButton->showMenu();
    fixture.settle();
    QVERIFY(!tabGuard);
}

void AppDocumentViewTest::panelArrowSwitch_data()
{
    QTest::addColumn<bool>("rightToLeft");
    QTest::newRow("ltr") << false;
    QTest::newRow("rtl") << true;
}

void AppDocumentViewTest::panelArrowSwitch()
{
    // Menu bar: Left / Right outside of a control, which uses the arrows, open the
    // neighboring panel (mirrored for right-to-left), around the ends
    using H = PanelSwitchHelper;
    using C = pdf::PDFAppCommands;
    QFETCH(bool, rightToLeft);

    struct DirectionGuard
    {
        ~DirectionGuard() { QApplication::setLayoutDirection(Qt::LeftToRight); }
    } directionGuard;
    QApplication::setLayoutDirection(rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    const Qt::Key nextKey = rightToLeft ? Qt::Key_Left : Qt::Key_Right;
    const Qt::Key previousKey = rightToLeft ? Qt::Key_Right : Qt::Key_Left;

    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ArrowSwitch.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    QCOMPARE(toolbar->layoutDirection(), rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);

    // File: a row asks for the next panel (View), whose first control gets the focus
    pdf::PDFAppDocumentPanelPopup* filePanel = toolbar->openPanel(H::Panel::File);
    QVERIFY(filePanel);
    fixture.settle();
    QVERIFY(H::focus(filePanel->getCommandButton(C::properties())));
    QTest::keyClick(filePanel->getCommandButton(C::properties()), nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::View));
    pdf::PDFAppDocumentViewPanel* viewPanel = qobject_cast<pdf::PDFAppDocumentViewPanel*>(toolbar->getOpenPanel());
    QVERIFY(viewPanel);
    QVERIFY(viewPanel->focusWidget() && viewPanel->isAncestorOf(viewPanel->focusWidget()));
    QVERIFY(!filePanel->isVisible());
    QVERIFY(!toolbar->getMenuButton(H::Panel::File)->isChecked());

    // View: the cards are a group of choices, the arrows stay in the panel
    QVERIFY(H::focus(viewPanel->getCard(C::layoutSinglePage())));
    QTest::keyClick(viewPanel->getCard(C::layoutSinglePage()), nextKey);
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);

    // A row of buttons: the arrow moves to the neighbor, at the edge to the next panel
    QVERIFY(H::focus(viewPanel->getShowAnnotationsCheckBox()));
    QTest::keyClick(viewPanel->getShowAnnotationsCheckBox(), nextKey);
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);
    QVERIFY(viewPanel->getFirstPageRightCheckBox()->hasFocus());
    QTest::keyClick(viewPanel->getFirstPageRightCheckBox(), nextKey);
    QVERIFY(viewPanel->getRenderingSettingsLink()->hasFocus());
    QTest::keyClick(viewPanel->getRenderingSettingsLink(), previousKey);
    QVERIFY(viewPanel->getFirstPageRightCheckBox()->hasFocus());
    QTest::keyClick(viewPanel->getFirstPageRightCheckBox(), nextKey);
    QTest::keyClick(viewPanel->getRenderingSettingsLink(), nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::Annotate));

    // Annotate: the swatches use the arrows; the row Show all comments switches
    pdf::PDFAppDocumentPanelPopup* annotatePanel = toolbar->getOpenPanel();
    QVERIFY(annotatePanel);
    pdf::PDFColorSwatchRow* colorRow = annotatePanel->findChild<pdf::PDFColorSwatchRow*>(QStringLiteral("annotateColors"));
    QVERIFY(colorRow && colorRow->getSwatch(1));
    QVERIFY(H::focus(colorRow->getSwatch(1)));
    QTest::keyClick(colorRow->getSwatch(1), nextKey);
    QCOMPARE(toolbar->getOpenPanel(), annotatePanel);
    QWidget* showComments = annotatePanel->findChild<QWidget*>(QStringLiteral("item:showAllComments"));
    QVERIFY(H::focus(showComments));
    QTest::keyClick(showComments, nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::Insert));

    // Insert: the first control (the leading tile) goes back to Annotate
    pdf::PDFAppDocumentPanelPopup* insertPanel = toolbar->getOpenPanel();
    QWidget* first = insertPanel->focusWidget();
    QVERIFY(first);
    QTest::keyClick(first, previousKey);
    QCOMPARE(toolbar->getOpenPanel(), annotatePanel);
    QVERIFY(H::focus(showComments));
    QTest::keyClick(annotatePanel, nextKey);
    QCOMPARE(toolbar->getOpenPanel(), insertPanel);
    QTest::keyClick(insertPanel->focusWidget(), Qt::Key_Down);
    QCOMPARE(toolbar->getOpenPanel(), insertPanel);
    H::closeAll(toolbar);
    fixture.settle();

    // Go to: the field uses the arrows; the button Go at the trailing edge opens the
    // Select menu (its current tool is the current item)
    pdf::PDFAppDocumentGoToPanel* goToPanel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(toolbar->openPanel(H::Panel::GoTo));
    QVERIFY(goToPanel);
    fixture.settle();
    goToPanel->focusPageField();
    QTest::keyClick(goToPanel->getPageField(), nextKey);
    QTest::keyClick(goToPanel->getPageField(), previousKey);
    QCOMPARE(toolbar->getOpenPanel(), goToPanel);
    QVERIFY(H::focus(goToPanel->getGoButton()));
    QTest::keyClick(goToPanel->getGoButton(), previousKey);
    QVERIFY(goToPanel->getPageField()->hasFocus());
    QVERIFY(H::focus(goToPanel->getGoButton()));
    QTest::keyClick(goToPanel->getGoButton(), nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::Tools));

    // Tools (step 6): the search field uses the arrows; the row All tools opens the
    // Select menu (its current tool is the current item)
    pdf::PDFAppDocumentToolsPanel* toolsPanel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(toolbar->getOpenPanel());
    QVERIFY(toolsPanel);
    QVERIFY(toolsPanel->getSearchField()->hasFocus());
    QTest::keyClick(toolsPanel->getSearchField(), nextKey);
    QCOMPARE(toolbar->getOpenPanel(), toolsPanel);
    QVERIFY(H::focus(toolsPanel->getAllToolsRow()));
    QTest::keyClick(toolsPanel->getAllToolsRow(), nextKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getSelectButton());
    pdf::PDFAppDocumentSelectMenu* menu = toolbar->getSelectMenu();
    QVERIFY(menu->activeAction());
    QVERIFY(menu->activeAction()->isChecked());

    // Select menu: around the end to File, back to Select and to Go to
    QTest::keyClick(menu, nextKey);
    QCOMPARE(toolbar->getOpenPanel(), filePanel);
    QTest::keyClick(filePanel->focusWidget(), Qt::Key_Down);
    QCOMPARE(toolbar->getOpenPanel(), filePanel);

    // A row of tiles: Save as -> Print in the panel; Save (not modified) is disabled,
    // so Save as is at the leading edge of the row
    QVERIFY(!filePanel->getCommandButton(C::save())->isEnabled());
    QVERIFY(H::focus(filePanel->getCommandButton(C::saveAs())));
    QTest::keyClick(filePanel->getCommandButton(C::saveAs()), nextKey);
    QVERIFY(filePanel->getCommandButton(C::print())->hasFocus());
    QTest::keyClick(filePanel->getCommandButton(C::print()), previousKey);
    QVERIFY(filePanel->getCommandButton(C::saveAs())->hasFocus());
    QTest::keyClick(filePanel->getCommandButton(C::saveAs()), previousKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getSelectButton());
    QTest::keyClick(menu, previousKey);
    QCOMPARE(toolbar->getOpenPanel(), toolsPanel);
    QVERIFY(H::focus(toolsPanel->getAllToolsRow()));
    QTest::keyClick(toolsPanel->getAllToolsRow(), previousKey);
    QCOMPARE(toolbar->getOpenPanel(), goToPanel);

    // The key sent to the panel itself (the focus at a row) switches too
    QVERIFY(H::focus(goToPanel->getCommandButton(C::exportBookmarks())));
    QTest::keyClick(goToPanel, previousKey);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::Insert));

    // The pointer switches in this direction too: the panel at the leading edge of
    // its button (Go to, 340 px - it fits into the screen of the offscreen platform)
    H::hover(toolbar->getMenuButton(H::Panel::View));
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);
    QToolButton* goToButton = toolbar->getMenuButton(H::Panel::GoTo);
    H::hover(goToButton);
    QCOMPARE(toolbar->getOpenPanel(), goToPanel);
    const QRect panelRect(goToPanel->mapToGlobal(goToPanel->getPanelRect().topLeft()), goToPanel->getPanelRect().size());
    if (rightToLeft)
    {
        QCOMPARE(panelRect.right(), goToButton->mapToGlobal(QPoint(goToButton->width() - 1, 0)).x());
    }
    else
    {
        QCOMPARE(panelRect.left(), goToButton->mapToGlobal(QPoint(0, 0)).x());
    }
    fixture.settle();
    QCOMPARE(H::countOpen(toolbar), 1);
}

void AppDocumentViewTest::panelSwitchDisabled()
{
    // A locked document disables Annotate and Insert: the pointer and the arrows pass them
    using H = PanelSwitchHelper;
    using C = pdf::PDFAppCommands;
    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SwitchDisabled.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    ViewTestHelper::trigger(fixture, C::lockDocument());
    QToolButton* annotateButton = toolbar->getMenuButton(H::Panel::Annotate);
    QToolButton* insertButton = toolbar->getMenuButton(H::Panel::Insert);
    QVERIFY(annotateButton->isVisible() && !annotateButton->isEnabled());
    QVERIFY(insertButton->isVisible() && !insertButton->isEnabled());

    pdf::PDFAppDocumentPanelPopup* viewPanel = toolbar->openPanel(H::Panel::View);
    QVERIFY(viewPanel);
    fixture.settle();
    H::hover(annotateButton);
    H::hover(insertButton);
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);
    QVERIFY(!toolbar->switchPanel(annotateButton, false));
    QVERIFY(!toolbar->openPanel(H::Panel::Annotate));
    fixture.settle();
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);
    QVERIFY(!toolbar->findPanel(H::Panel::Annotate));

    QVERIFY(H::focus(qobject_cast<pdf::PDFAppDocumentViewPanel*>(viewPanel)->getRenderingSettingsLink()));
    QTest::keyClick(viewPanel->focusWidget(), Qt::Key_Right);
    QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::GoTo));
    pdf::PDFAppDocumentGoToPanel* goToPanel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(toolbar->getOpenPanel());
    QVERIFY(H::focus(goToPanel->getCommandButton(C::exportBookmarks())));
    QTest::keyClick(goToPanel->focusWidget(), Qt::Key_Left);
    QCOMPARE(toolbar->getOpenPanel(), viewPanel);
    QVERIFY(!toolbar->findPanel(H::Panel::Annotate));
    QVERIFY(!toolbar->findPanel(H::Panel::Insert));
}

void AppDocumentViewTest::panelSwitchDeletion()
{
    using H = PanelSwitchHelper;
    using C = pdf::PDFAppCommands;

    // The tab (with the toolbar and the panels) deleted inside of the switch by a slot
    // of a button: the new button checked (pointer), the old one unchecked (pointer
    // to the Select menu, keyboard - the panel, which has asked, is deleted too)
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        pdftest::AppDocumentFixture fixture(QSize(780, 560));
        QPointer<pdf::PDFAppDocumentTab> tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SwitchDeletion%1.pdf").arg(scenario), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolbar> toolbar = tab->getView()->getToolbar();
        QPointer<pdf::PDFAppDocumentPanelPopup> filePanel = toolbar->openPanel(H::Panel::File);
        QVERIFY(filePanel);
        fixture.settle();
        QToolButton* viewButton = toolbar->getMenuButton(H::Panel::View);
        QToolButton* fileButton = toolbar->getMenuButton(H::Panel::File);
        switch (scenario)
        {
            case 0:
                connect(viewButton, &QToolButton::toggled, fixture.window, [&tab](bool checked) { if (checked) { delete tab.data(); } });
                H::hover(viewButton);
                break;
            case 1:
                connect(fileButton, &QToolButton::toggled, fixture.window, [&tab](bool checked) { if (!checked) { delete tab.data(); } });
                H::hover(toolbar->getSelectButton());
                break;
            default:
                connect(fileButton, &QToolButton::toggled, fixture.window, [&tab](bool checked) { if (!checked) { delete tab.data(); } });
                QVERIFY(H::focus(filePanel->getCommandButton(C::properties())));
                QTest::keyClick(filePanel->getCommandButton(C::properties()), Qt::Key_Right);
                break;
        }
        QVERIFY2(!tab, qPrintable(QString::number(scenario)));
        QVERIFY(!toolbar);
        QVERIFY(!filePanel);
        fixture.settle();
        QVERIFY(fixture.window);
    }

    // The window deleted from the closing of the old panel (after the switch)
    {
        pdftest::AppDocumentFixture fixture(QSize(780, 560));
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SwitchDeletion3.pdf"), 2));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentToolbar> toolbar = tab->getView()->getToolbar();
        pdf::PDFAppDocumentPanelPopup* filePanel = toolbar->openPanel(H::Panel::File);
        QVERIFY(filePanel);
        fixture.settle();
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        connect(filePanel, &pdf::PDFAppDocumentPanelPopup::closed, filePanel, [window]() { delete window.data(); });
        H::hover(toolbar->getMenuButton(H::Panel::Insert));
        fixture.settle();
        QVERIFY(!window);
        QVERIFY(!toolbar);
    }

    // The panel deleted from its own signal neighborRequested and from its closing;
    // the toolbar creates the panel again
    {
        pdftest::AppDocumentFixture fixture(QSize(780, 560));
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SwitchDeletion4.pdf"), 2));
        QVERIFY(tab);
        pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
        QPointer<pdf::PDFAppDocumentPanelPopup> filePanel = toolbar->openPanel(H::Panel::File);
        QVERIFY(filePanel);
        fixture.settle();
        connect(filePanel.data(), &pdf::PDFAppDocumentPanelPopup::neighborRequested, toolbar, [filePanel]() { delete filePanel.data(); });
        QVERIFY(H::focus(filePanel->getCommandButton(C::properties())));
        QTest::keyClick(filePanel->getCommandButton(C::properties()), Qt::Key_Right);
        QVERIFY(!filePanel);
        QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::View));
        fixture.settle();
        QVERIFY(!toolbar->findPanel(H::Panel::File));

        QPointer<pdf::PDFAppDocumentPanelPopup> viewPanel = toolbar->getOpenPanel();
        connect(viewPanel.data(), &pdf::PDFAppDocumentPanelPopup::closed, toolbar, [viewPanel]() { delete viewPanel.data(); });
        H::hover(toolbar->getMenuButton(H::Panel::File));
        QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::File));
        fixture.settle();
        QVERIFY(!viewPanel);
        H::hover(toolbar->getMenuButton(H::Panel::View));
        QCOMPARE(toolbar->getOpenPanelButton(), toolbar->getMenuButton(H::Panel::View));
        QVERIFY(toolbar->findPanel(H::Panel::View));
        H::closeAll(toolbar);
        fixture.settle();
    }

    // A modal dialog (opened from a panel) blocks the switch
    {
        pdftest::AppDocumentFixture fixture(QSize(780, 560));
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SwitchDeletion5.pdf"), 2));
        QVERIFY(tab);
        pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
        pdf::PDFAppDocumentPanelPopup* filePanel = toolbar->openPanel(H::Panel::File);
        QVERIFY(filePanel);
        fixture.settle();

        QPointer<QDialog> dialog = new QDialog(fixture.window);
        dialog->setModal(true);
        dialog->show();
        QVERIFY(QApplication::activeModalWidget() == dialog);
        QVERIFY(!toolbar->switchPanel(toolbar->getMenuButton(H::Panel::View), false));
        H::hover(toolbar->getMenuButton(H::Panel::View));
        QVERIFY(!toolbar->findPanel(H::Panel::View) || !toolbar->findPanel(H::Panel::View)->isVisible());
        if (filePanel->isVisible())
        {
            QVERIFY(H::focus(filePanel->getCommandButton(C::properties())));
            QTest::keyClick(filePanel->getCommandButton(C::properties()), Qt::Key_Right);
            QVERIFY(!toolbar->findPanel(H::Panel::View) || !toolbar->findPanel(H::Panel::View)->isVisible());
        }
        delete dialog.data();
        fixture.settle();
    }
}

void AppDocumentViewTest::requestedCommandAfterClose()
{
    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Requested.pdf"), 2));
    QVERIFY(tab);

    // A command requested by a button runs from the event loop; the deleted view drops it
    tab->getView()->requestCommand(pdf::PDFAppCommands::fullScreen());
    tab->getView()->requestCommand(pdf::PDFAppCommands::closeTab());
    tab->getView()->requestCommand(pdf::PDFAppCommands::nextPage());
    tab->getView()->requestCommand(QStringLiteral("unknown.command"));
    fixture.settle();
    fixture.closeMessageBoxes();
    QTRY_VERIFY(!tab);
    QVERIFY(!fixture.window->isFullScreenMode());
    QVERIFY(!fixture.window->getTopBar()->isHidden());
}

void AppDocumentViewTest::narrowToolbar()
{
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Narrow.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    fixture.settle();

    // The minimal window 720 x 480: the toolbar narrows itself, nothing overflows
    QVERIFY(toolbar->getCollapseLevel() > 0);
    QCOMPARE(toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File)->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(fixture.window->width() <= 720);
    for (QWidget* child : toolbar->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    {
        if (child->isVisible() && !child->isWindow())
        {
            QVERIFY2(child->geometry().right() < toolbar->width() && child->geometry().left() >= 0, qPrintable(child->objectName()));
        }
    }
    QVERIFY(toolbar->getSearchField()->width() < pdf::PDFAppDocumentToolbar::SEARCH_FIELD_WIDTH);

    // The panels stay inside of the screen
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::View);
    if (panel)
    {
        fixture.settle();
        const QRect screen = panel->screen()->availableGeometry();
        const QRect panelRect(panel->mapToGlobal(panel->getPanelRect().topLeft()), panel->getPanelRect().size());
        QVERIFY(panelRect.left() >= screen.left());
        QVERIFY(panelRect.right() <= screen.right());
        panel->close();
    }

    // A wide window shows everything again
    fixture.window->resize(1440, 960);
    fixture.settle();
    QTRY_COMPARE(toolbar->getCollapseLevel(), 0);
    QCOMPARE(toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File)->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
}

void AppDocumentViewTest::rightToLeft()
{
    // A window, whose buttons lie inside of the screen of the offscreen platform (800 x 600)
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("RightToLeft.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentToolbar* toolbar = view->getToolbar();

    // The toolbar is mirrored: the sidebar button at the right edge
    QToolButton* sidebarButton = toolbar->getCommandButton(pdf::PDFAppCommands::toggleSidebar());
    QCOMPARE(sidebarButton->mapTo(toolbar, QPoint(sidebarButton->width(), 0)).x(), toolbar->width() - 12);
    QCOMPARE(toolbar->getSearchField()->mapTo(toolbar, QPoint(0, 0)).x(), 12);

    // The panel is aligned with the trailing edge of its button
    QToolButton* fileButton = toolbar->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::File);
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
    QVERIFY(panel);
    fixture.settle();
    QCOMPARE(panel->layoutDirection(), Qt::RightToLeft);
    const QRect panelRect(panel->mapToGlobal(panel->getPanelRect().topLeft()), panel->getPanelRect().size());
    QCOMPARE(panelRect.right(), fileButton->mapToGlobal(QPoint(fileButton->width() - 1, 0)).x());
    panel->close();

    // The info bar stays centered
    pdf::PDFAppInfoBarRequest request;
    request.id = QStringLiteral("rtl");
    request.title = QStringLiteral("Title");
    view->showInfoBar(request);
    fixture.settle();
    pdf::PDFAppInfoBar* infoBar = view->findInfoBarWidget(QStringLiteral("rtl"));
    const QRect card(infoBar->mapTo(view, infoBar->getCardRect().topLeft()), infoBar->getCardRect().size());
    const QRect canvas(view->getPdfWidget()->mapTo(view, QPoint(0, 0)), view->getPdfWidget()->size());
    QVERIFY(qAbs(card.center().x() - canvas.center().x()) <= 1);
}

void AppDocumentViewTest::themes()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Themes.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentToolbar* toolbar = view->getToolbar();

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();

        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            // toolbar 193:785: bg/subtle with the border border/default at the bottom
            const QImage toolbarImage = ViewTestHelper::render(toolbar, ratio);
            const QPoint free(toolbar->getSelectButton()->mapTo(toolbar, QPoint(0, 0)).x() - 30, 4);
            QCOMPARE(ViewTestHelper::pixel(toolbarImage, free), ViewTestHelper::token(pdf::PDFThemeColors::BgSubtle));
            QCOMPARE(ViewTestHelper::pixel(toolbarImage, QPoint(toolbar->width() / 2, toolbar->height() - 1)), ViewTestHelper::token(pdf::PDFThemeColors::BorderDefault));
            QCOMPARE(ViewTestHelper::countLeakPixels(toolbarImage), 0);
            QCOMPARE(ViewTestHelper::countLeakPixels(ViewTestHelper::render(view, ratio)), 0);
        }

        // The panels and the menu without the leak color
        for (pdf::PDFAppDocumentToolbar::Panel panelKind : { pdf::PDFAppDocumentToolbar::Panel::File, pdf::PDFAppDocumentToolbar::Panel::View, pdf::PDFAppDocumentToolbar::Panel::GoTo })
        {
            pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(panelKind);
            QVERIFY(panel);
            fixture.settle();
            QCOMPARE(ViewTestHelper::countLeakPixels(ViewTestHelper::render(panel)), 0);
            panel->close();
            fixture.settle();
        }

        pdf::PDFAppInfoBarRequest request;
        request.id = QStringLiteral("theme");
        request.tone = pdf::PDFAppInfoBarTone::Warning;
        request.title = QStringLiteral("Warning");
        request.actions = { { QStringLiteral("a"), QStringLiteral("Action") } };
        view->showInfoBar(request);
        fixture.settle();
        QCOMPARE(ViewTestHelper::countLeakPixels(ViewTestHelper::render(view->findInfoBarWidget(QStringLiteral("theme")))), 0);
        view->hideInfoBar(QStringLiteral("theme"));
    }
}

void AppDocumentViewTest::accessibility()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Accessibility.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // Every button of the toolbar has a name and the icon buttons a tooltip
    for (QToolButton* button : toolbar->findChildren<QToolButton*>())
    {
        if (qobject_cast<QLineEdit*>(button->parentWidget()))
        {
            // The buttons of a field belong to the field
            continue;
        }
        QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(button->objectName()));
        QVERIFY2(!button->toolTip().isEmpty(), qPrintable(button->objectName()));
    }
    QCOMPARE(toolbar->getCommandButton(pdf::PDFAppCommands::zoomIn())->toolTip(), QStringLiteral("Zoom in (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_Plus).toString(QKeySequence::NativeText)));
    QVERIFY(!toolbar->getPageField()->accessibleName().isEmpty());
    QVERIFY(!toolbar->getSearchField()->accessibleName().isEmpty());

    // The tab order goes through the toolbar
    QToolButton* sidebarButton = toolbar->getCommandButton(pdf::PDFAppCommands::toggleSidebar());
    QVERIFY(sidebarButton->focusPolicy() & Qt::TabFocus);
    sidebarButton->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(sidebarButton->hasFocus());
    QTest::keyClick(sidebarButton, Qt::Key_Tab);
    QVERIFY(QApplication::focusWidget() != sidebarButton);

    // The rows of the panels have names (their texts)
    pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
    QVERIFY(panel);
    QCOMPARE(panel->accessibleName(), QStringLiteral("File"));
    for (pdf::PDFAppPanelMenuItem* item : panel->findChildren<pdf::PDFAppPanelMenuItem*>())
    {
        QVERIFY(!item->accessibleName().isEmpty());
    }
    panel->close();
}

void AppDocumentViewTest::languageChange()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentPanelPopup* panel = view->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::GoTo);
    QVERIFY(panel);

    // A change of the language translates every text again (the source texts here)
    QEvent languageChange(QEvent::LanguageChange);
    for (QWidget* widget : { static_cast<QWidget*>(view), static_cast<QWidget*>(view->getToolbar()), static_cast<QWidget*>(panel), static_cast<QWidget*>(view->getToolbar()->getSelectMenu()) })
    {
        QCoreApplication::sendEvent(widget, &languageChange);
    }
    fixture.settle();
    QCOMPARE(view->getToolbar()->getMenuButton(pdf::PDFAppDocumentToolbar::Panel::GoTo)->text(), QStringLiteral("Go to"));
    QCOMPARE(qobject_cast<QToolButton*>(panel->getCommandButton(pdf::PDFAppCommands::firstPage()))->text(), QStringLiteral("First"));
    QCOMPARE(view->getStatusBar()->getPageText(), QStringLiteral("Page 1 (i) of 10"));
    QCOMPARE(view->getStatusBar()->getMessage(), QStringLiteral("Ready"));
    panel->close();
}

void AppDocumentViewTest::largeDocument()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The last page, the field and the panel of a large document
    QElapsedTimer timer;
    timer.start();
    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::lastPage());
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1999));
    // page-field 193:847 / 1234:164802: the numbers of the field without the group separator
    QCOMPARE(view->getToolbar()->getPageField()->getSuffix(), QStringLiteral("/ 2000"));
    QCOMPARE(view->findPageIndex(QStringLiteral("1500")), std::make_optional(pdf::PDFInteger(1499)));
    view->goToPage(999);
    QTRY_COMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(999));
    QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
    qInfo("2000 pages: navigation %lld ms", static_cast<long long>(timer.elapsed()));

    // The number of a page is shown without the separators of the locale, so the
    // text of the fields can be confirmed again ("1,000" is no page)
    QCOMPARE(view->getPageLabel(1499), QStringLiteral("1500"));
    QCOMPARE(view->getToolbar()->getPageField()->text(), QStringLiteral("1000"));
    view->openGoToPage();
    fixture.settle();
    pdf::PDFAppDocumentGoToPanel* panel = qobject_cast<pdf::PDFAppDocumentGoToPanel*>(view->getToolbar()->getOpenPanel());
    QVERIFY(panel);
    QCOMPARE(panel->getPageField()->text(), QStringLiteral("1000"));
    QTest::mouseClick(panel->getGoButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!panel->getPageField()->isInvalid());
    QVERIFY(!panel->isVisible());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(999));
}

void AppDocumentViewTest::toolbarFieldsAndToolTips()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Fields.pdf"), 4));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentToolbar* toolbar = view->getToolbar();
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();

    // An invalid page: the error stays while the field has the focus and goes
    // with the text of the user, when the field shows the current page again
    pdf::PDFToolBarField* pageField = toolbar->getPageField();
    pageField->setFocus();
    QTRY_VERIFY(pageField->hasFocus());
    pageField->setText(QStringLiteral("xyz"));
    QTest::keyClick(pageField, Qt::Key_Return);
    QVERIFY(pageField->isInvalid());
    canvas->setFocus();
    QTRY_VERIFY(!pageField->hasFocus());
    fixture.settle();
    QCOMPARE(pageField->text(), QStringLiteral("1"));
    QVERIFY(!pageField->isInvalid());

    pdf::PDFToolBarField* zoomField = toolbar->getZoomField();
    zoomField->setFocus();
    QTRY_VERIFY(zoomField->hasFocus());
    zoomField->setText(QStringLiteral("abc"));
    QTest::keyClick(zoomField, Qt::Key_Return);
    QVERIFY(zoomField->isInvalid());
    canvas->setFocus();
    QTRY_VERIFY(!zoomField->hasFocus());
    fixture.settle();
    QVERIFY(zoomField->text().endsWith(QStringLiteral("%")));
    QVERIFY(!zoomField->isInvalid());

    // The tooltips of the buttons show the shortcuts of the user
    const pdf::PDFAppCommand* command = fixture.context->getCommandRegistry()->findCommand(pdf::PDFAppCommands::nextPage());
    QVERIFY(command);
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::nextPage(), QStringLiteral("Ctrl+J")).isError());
    const QString expected = QStringLiteral("%1 (%2)").arg(command->getTitle(), QKeySequence(Qt::CTRL | Qt::Key_J).toString(QKeySequence::NativeText));
    QTRY_COMPARE(toolbar->getCommandButton(pdf::PDFAppCommands::nextPage())->toolTip(), expected);
}

void AppDocumentViewTest::popoverStyleFunctions()
{
    // The functions of the style for the popovers of the application
    QWidget plain;
    QCOMPARE(pdf::PDFWidgetStyle::getPopoverMargins(nullptr), QMargins());
    QCOMPARE(pdf::PDFWidgetStyle::getPopoverMargins(&plain), QMargins());
    QWidget child(&plain);
    QCOMPARE(pdf::PDFWidgetStyle::getPopoverMargins(&child), QMargins());

    QImage image(QSize(64, 64), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    pdf::PDFWidgetStyle::drawPopoverPanel(nullptr, &plain, 12.0);
    pdf::PDFWidgetStyle::drawPopoverPanel(&painter, nullptr, 12.0);
    plain.resize(64, 64);
    pdf::PDFWidgetStyle::drawPopoverPanel(&painter, &plain, 12.0);
    painter.end();
    QCOMPARE(image.pixelColor(32, 32), ViewTestHelper::token(pdf::PDFThemeColors::BgSurface));
}

void AppDocumentViewTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the view (193:756), the panels File (196:2482),
    // View (196:2645), Go to (196:3327), the Select menu (198:5380), the sidebar hidden
    // (198:6178), the info bars (1028:23847, 1030:25227), both themes at 1x and 2x,
    // the minimal window and right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        pdftest::AppDocumentFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
        QVERIFY(tab);
        pdf::PDFAppDocumentView* view = tab->getView();
        pdf::PDFAppDocumentToolbar* toolbar = view->getToolbar();
        fixture.settle();

        for (qreal ratio : { 1.0, 2.0 })
        {
            const QString scale = ratio > 1.0 ? QStringLiteral("2x") : QStringLiteral("1x");
            ViewTestHelper::save(folder, QStringLiteral("view-document-%1-%2").arg(suffix, scale), ViewTestHelper::render(fixture.window, ratio));

            for (const auto& panelName : { std::make_pair(pdf::PDFAppDocumentToolbar::Panel::File, QStringLiteral("file")),
                                           std::make_pair(pdf::PDFAppDocumentToolbar::Panel::View, QStringLiteral("view")),
                                           std::make_pair(pdf::PDFAppDocumentToolbar::Panel::GoTo, QStringLiteral("goto")) })
            {
                pdf::PDFAppDocumentPanelPopup* panel = toolbar->openPanel(panelName.first);
                fixture.settle();
                ViewTestHelper::save(folder, QStringLiteral("view-panel-%1-%2-%3").arg(panelName.second, suffix, scale), ViewTestHelper::renderWindowWithPopups(fixture.window, { panel }, ratio));
                ViewTestHelper::save(folder, QStringLiteral("view-panel-only-%1-%2-%3").arg(panelName.second, suffix, scale), ViewTestHelper::render(panel, ratio));
                panel->close();
                fixture.settle();
            }

            pdf::PDFAppDocumentSelectMenu* menu = toolbar->getSelectMenu();
            menu->updateState();
            menu->popup(toolbar->getSelectButton()->mapToGlobal(QPoint(0, toolbar->getSelectButton()->height())));
            fixture.settle();
            ViewTestHelper::save(folder, QStringLiteral("view-select-menu-%1-%2").arg(suffix, scale), ViewTestHelper::renderWindowWithPopups(fixture.window, { menu }, ratio));
            menu->close();
            fixture.settle();

            pdf::PDFAppInfoBarRequest blocked;
            blocked.id = QStringLiteral("link-blocked");
            blocked.tone = pdf::PDFAppInfoBarTone::Error;
            blocked.title = QStringLiteral("The web link was not opened – web links are turned off");
            blocked.actions = { { QStringLiteral("copy"), QStringLiteral("Copy Link") }, { QStringLiteral("settings"), QStringLiteral("Security Settings") } };
            view->showInfoBar(blocked);
            pdf::PDFAppInfoBarRequest printing;
            printing.id = QStringLiteral("printing");
            printing.tone = pdf::PDFAppInfoBarTone::Progress;
            printing.iconName = QStringLiteral("printer");
            printing.title = QStringLiteral("Printing on HP LaserJet M404dn");
            printing.text = QStringLiteral("Page 313 of copy 1 · 5 of 20 pages · about 1 minute left");
            printing.progress = 25;
            printing.isClosable = false;
            printing.actions = { { QStringLiteral("stop"), QStringLiteral("Stop"), QStringLiteral("stop") } };
            view->showInfoBar(printing);
            fixture.settle();
            ViewTestHelper::save(folder, QStringLiteral("view-info-bars-%1-%2").arg(suffix, scale), ViewTestHelper::render(fixture.window, ratio));
            view->hideInfoBar(blocked.id);
            view->hideInfoBar(printing.id);

            view->setSidebarVisible(false);
            fixture.settle();
            ViewTestHelper::save(folder, QStringLiteral("view-sidebar-hidden-%1-%2").arg(suffix, scale), ViewTestHelper::render(fixture.window, ratio));
            view->setSidebarVisible(true);
            fixture.settle();
        }

        fixture.window->resize(720, 480);
        fixture.settle();
        ViewTestHelper::save(folder, QStringLiteral("view-minimal-%1").arg(suffix), ViewTestHelper::render(fixture.window));
    }

    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("RightToLeft.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentPanelPopup* panel = tab->getView()->getToolbar()->openPanel(pdf::PDFAppDocumentToolbar::Panel::File);
    fixture.settle();
    ViewTestHelper::save(folder, QStringLiteral("view-rtl"), ViewTestHelper::renderWindowWithPopups(fixture.window, { panel }, 1.0));
}

void AppDocumentViewTest::panelReopenFocus()
{
    // Review RSP6 / RC6-1: a panel opened again starts at its first control, not at the
    // control after the one focused before it was closed (PDFAppDocumentPanelPopup::popup
    // forgets the remembered focus of its window) - by the mouse, by the keyboard and
    // by the switch between the panels
    using Panel = pdf::PDFAppDocumentToolbar::Panel;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ReopenFocus.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();

    // The focus widget of the window of the panel (offscreen does not activate popups)
    auto focusOf = [](pdf::PDFAppDocumentPanelPopup* panel) { return panel->focusWidget(); };
    auto moveFocus = [&focusOf](pdf::PDFAppDocumentPanelPopup* panel)
    {
        QWidget* before = focusOf(panel);
        QTest::keyClick(before, Qt::Key_Tab);
        return focusOf(panel) && focusOf(panel) != before;
    };
    auto closeByEscape = [](pdf::PDFAppDocumentPanelPopup* panel)
    {
        QTest::keyClick(panel, Qt::Key_Escape);
        pdftest::AppDocumentFixture::settle();
        return !panel->isVisible();
    };

    for (const Panel kind : { Panel::File, Panel::View, Panel::Annotate, Panel::Insert, Panel::GoTo })
    {
        QToolButton* button = toolbar->getMenuButton(kind);
        QVERIFY(button && button->isVisible() && button->isEnabled());
        const QString name = button->objectName();

        // The mouse: the first opening gives the first control
        PanelSwitchHelper::click(button);
        fixture.settle();
        QPointer<pdf::PDFAppDocumentPanelPopup> panel = toolbar->getOpenPanel();
        QVERIFY2(panel, qPrintable(name));
        QPointer<QWidget> first = focusOf(panel);
        QVERIFY2(first && panel->isAncestorOf(first), qPrintable(name));

        // Opened again after Esc and after a click on its button
        QVERIFY2(moveFocus(panel), qPrintable(name));
        QVERIFY(closeByEscape(panel));
        PanelSwitchHelper::click(button);
        fixture.settle();
        QCOMPARE(toolbar->getOpenPanel(), panel.data());
        QVERIFY2(focusOf(panel) == first, qPrintable(name));
        QVERIFY(moveFocus(panel));
        PanelSwitchHelper::click(button);
        QTRY_VERIFY(!panel->isVisible());
        PanelSwitchHelper::click(button);
        fixture.settle();
        QCOMPARE(toolbar->getOpenPanel(), panel.data());
        QVERIFY2(focusOf(panel) == first, qPrintable(name));
        QVERIFY(moveFocus(panel));
        QVERIFY(closeByEscape(panel));

        // The keyboard: Space on the button of the toolbar
        button->setFocus(Qt::TabFocusReason);
        QTest::keyClick(button, Qt::Key_Space);
        fixture.settle();
        QCOMPARE(toolbar->getOpenPanel(), panel.data());
        QVERIFY2(focusOf(panel) == first, qPrintable(name));
        QVERIFY(moveFocus(panel));

        // The switch to another panel and back (the pointer, Left / Right)
        QToolButton* other = toolbar->getToolsButton();
        QVERIFY(toolbar->switchPanel(other, true));
        fixture.settle();
        QVERIFY(!panel->isVisible());
        QVERIFY(toolbar->switchPanel(button, true));
        fixture.settle();
        QCOMPARE(toolbar->getOpenPanel(), panel.data());
        QVERIFY2(focusOf(panel) == first, qPrintable(name));
        QVERIFY(closeByEscape(panel));
    }
}

QTEST_MAIN(AppDocumentViewTest)
#include "tst_appdocumentviewtest.moc"
