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

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappnavigator.h"
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
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
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

/// Package B of step 3 (view, toolbar, panels, status bar, info bars)
class AppDocumentViewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void parts();
    void toolbarGeometry();
    void viewColors();
    void splitterHandle();
    void routePage();
    void navigationCommands();
    void canvasShortcuts();
    void canvasShortcutScope();
    void toolManager();
    void textSelectionCommands();
    void selectMenuTools();
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
    void panelKeyboard();
    void panelDeletion();
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

    // Select text is the tool of a new document; the status bar says Ready
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));
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

    // Annotate and Insert are disabled in step 3
    QVERIFY(!toolbar->getAnnotateButton()->isEnabled());
    QVERIFY(!toolbar->getInsertButton()->isEnabled());
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

void AppDocumentViewTest::selectMenuTools()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SelectMenu.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentSelectMenu* menu = view->getToolbar()->getSelectMenu();
    QVERIFY(menu);

    // Select menu 198:5517: four items, Select text checked
    QCOMPARE(menu->actions().size(), 4);
    menu->updateState();
    QVERIFY(menu->getCommandAction(pdf::PDFAppCommands::selectText())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectTable())->isChecked());
    QVERIFY(!menu->getCommandAction(pdf::PDFAppCommands::selectAll()));

    // Each item activates its tool; the active tool again returns to Select text
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
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));

    // Select text stays the tool, when it is chosen again
    menu->getCommandAction(pdf::PDFAppCommands::selectText())->trigger();
    fixture.settle();
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));

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

void AppDocumentViewTest::magnifier()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = ViewTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Magnifier.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // Settings › Viewing › Magnifier: size 100 px, zoom 2.0
    pdf::PDFMagnifierTool* magnifier = view->getToolManager()->getMagnifierTool();
    QVERIFY(magnifier);
    QCOMPARE(magnifier->getMagnifierSize(), 100);
    QCOMPARE(magnifier->getMagnifierZoom(), 2.0);

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::magnifier());
    QVERIFY(magnifier->isActive());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::magnifier())->isChecked());

    // The settings apply also to an active magnifier
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::magnifierSize(), 180).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.5).isError());
    QTRY_COMPARE(magnifier->getMagnifierSize(), 180);
    QCOMPARE(magnifier->getMagnifierZoom(), 3.5);

    // The pointer over the canvas draws the magnifier (no crash, the canvas repaints)
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    QTest::mouseMove(canvas, canvas->rect().center());
    fixture.settle();
    QVERIFY(!ViewTestHelper::render(canvas).isNull());

    ViewTestHelper::trigger(fixture, pdf::PDFAppCommands::magnifier());
    QVERIFY(!magnifier->isActive());
    QVERIFY(view->isCanvasToolActive(pdf::PDFToolManager::SelectTextTool));
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
    QCOMPARE(view->getToolbar()->getPageField()->getSuffix(), QStringLiteral("/ %1").arg(QLocale().toString(2000)));
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

QTEST_MAIN(AppDocumentViewTest)
#include "tst_appdocumentviewtest.moc"
