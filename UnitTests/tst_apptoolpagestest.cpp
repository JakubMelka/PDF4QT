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

// The tools Rotate Pages and Remove Pages in the application (package 2A of the plan of the
// first batch; frames 389:20177, 389:20910, 389:21590, 388:20614, 388:21466, 388:22315).
// The expected values are the numbers and the texts of the frames.

#include "apptoolshellfixture.h"
#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmcphost.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfmcpcall.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTranslator>
#include <QtTest>

using pdf::PDFAppToolGUIRemovePages;
using pdf::PDFAppToolGUIRotatePages;
using pdf::PDFDocumentToolShell;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;

namespace pdftest
{

/// A translator of the texts of the tools of the pages
class PagesTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("PageMarkingToolGUI") || name.contains("ToolGUIRotatePages") || name.contains("ToolGUIRemovePages"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

}   // namespace pdftest

/// Helpers of the test
class PagesGuiHelper
{
public:
    PagesGuiHelper() = delete;

    static inline const QString ROTATE_ID = QStringLiteral("pages.rotate");
    static inline const QString REMOVE_ID = QStringLiteral("pages.remove");

    /// A document of pages of the size (A4 by default)
    static QString writePages(const QString& filePath, int pageCount, const QSizeF& size = QSizeF(595, 842))
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            builder.appendPage(QRectF(QPointF(0, 0), size));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
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

    template<typename GUI>
    static GUI* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    /// The tile of the page in the coordinates of the window
    static QRect tileInWindow(const pdf::PDFAppToolPageGrid* grid, int page)
    {
        const pdf::PDFAppToolPageGridView* view = grid->getView();
        const QRect rect = view->visualRect(view->model()->index(page, 0));
        return QRect(view->viewport()->mapTo(view->window(), rect.topLeft()), rect.size());
    }

    /// Clicks the tile of the page with the mouse (the signal comes from the event loop)
    static void clickTile(const pdf::PDFAppToolPageGrid* grid, int page, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        pdf::PDFAppToolPageGridView* view = grid->getView();
        view->scrollTo(view->model()->index(page, 0));
        AppToolShellFixture::settle();
        const QPoint center = view->visualRect(view->model()->index(page, 0)).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, center);
        AppToolShellFixture::settle();
    }

    static void setPages(PDFDocumentToolShell* shell, const QString& range)
    {
        pdf::PDFDocumentToolPageSelection selection;
        selection.pages = range;
        shell->getModel()->setValue(QStringLiteral("pages"), selection.toVariant());
        AppToolShellFixture::settle();
    }

    static QStringList values(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; i < list->getRowCount(); ++i)
        {
            result << list->getValue(i);
        }
        return result;
    }

    static int countLeak(QWidget* widget)
    {
        return AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(widget, 1.0));
    }

    /// A change of the document by an assistant (pages.md: allowed below an open tool)
    static bool commitAssistantChange(pdf::PDFAppDocument* document, pdf::PDFDocument changed)
    {
        const pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(changed)));
        return document->commit(pdf::PDFModifiedDocument(pointer, document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp);
    }

    /// The document without the page (index from 0)
    static pdf::PDFDocument removePage(pdf::PDFAppDocument* document, pdf::PDFInteger page)
    {
        return pdf::PDFDocumentPageOperations::removePages(document->getDocument().data(), { page }, nullptr, nullptr);
    }

    /// The document with its last page moved in front of the first one
    static pdf::PDFDocument moveLastFirst(pdf::PDFAppDocument* document)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.flattenPageTree();
        std::vector<pdf::PDFObjectReference> pages = builder.getPages();
        std::rotate(pages.begin(), std::prev(pages.end()), pages.end());
        builder.setPages(pages);
        return builder.build();
    }

    /// Pages, whose widths name them: page n is 100 + n wide
    static QString writeNamedPages(const QString& filePath, int pageCount, uint32_t permissions = 0)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 1; i <= pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, 100 + i, 842));
        }
        if (permissions)
        {
            // An owner password only: it opens without a password, with the permissions
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = QStringLiteral("owner");
            settings.permissions = permissions;
            builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// The names of the pages of a document by their widths ("2 3 5")
    static QString pageNames(const pdf::PDFDocument* document)
    {
        QStringList names;
        for (size_t i = 0; i < document->getCatalog()->getPageCount(); ++i)
        {
            names << QString::number(qRound(document->getCatalog()->getPage(i)->getMediaBox().width()) - 100);
        }
        return names.join(QLatin1Char(' '));
    }

    static QStringList pendingTitles(const pdf::PDFAppToolPendingPagesModel* model)
    {
        QStringList titles;
        for (int i = 0; i < model->rowCount(); ++i)
        {
            titles << model->index(i).data(Qt::DisplayRole).toString();
        }
        return titles;
    }
};

/// Remove Pages, which lets the test change the document under its run (as an assistant would)
class PagesTestRemoveWithChange : public pdf::PDFDocumentToolRemovePages
{
public:
    std::function<void()> beforeRun;

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override
    {
        if (beforeRun)
        {
            beforeRun();
        }
        return pdf::PDFDocumentToolRemovePages::execute(context, parameters, control, progress);
    }
};

class AppToolPagesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void registered();
    void rotateFrame();
    void rotateLandscapeTip();
    void rotateClicksAndUndo();
    void rotateMixedAndReset();
    void rotateWithoutTurnsDisabled();
    void rotateEveryPageListed();
    void rotateRunKeepsInTab();
    void rotateEntryCurrentPage();
    void thumbnailsCommandStillRotates();

    void removeFrame();
    void removeEveryPage();
    void removeKeepAndDelete();
    void removeRunKeepsInTab();

    void gridOneChangeForManyTiles();
    void themesAndDirection();
    void minimalWindow();
    void languageChange();
    void deletedWithToolOpen();
    void screenshots();

    void removeMarksFollowPages();
    void rotatePlanFollowsPages();
    void mcpRerunFollowsPages();
    void pagesNeedAssemble();
};

void AppToolPagesTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolPagesTest::registered()
{
    // The context registers both tools with their GUI and their pages
    AppToolShellFixture fixture;
    for (const QString& toolId : { PagesGuiHelper::ROTATE_ID, PagesGuiHelper::REMOVE_ID })
    {
        QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
        QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, toolId));
    }
}

void AppToolPagesTest::rotateFrame()
{
    // 389:20177: 48 pages, 9, 10 and 15 selected in the thumbnails, every click on Rotate Right
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    QVERIFY(tab);
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    AppToolShellFixture::settle();

    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // page-selection: Selected, "3 pages selected · 9, 10, 15"
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(gui->getPageSelection()->getStatusText(), QStringLiteral("3 pages selected · 9, 10, 15"));
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 8, 9, 14 }));

    // K15: no turn yet
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Rotate Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    gui->getRotateRightButton()->click();
    AppToolShellFixture::settle();

    // The toolbar 389:20183: the actions and Rotate 3 Pages
    QCOMPARE(gui->getRotateLeftButton()->text(), QStringLiteral("Rotate Left"));
    QCOMPARE(gui->getRotateHalfButton()->text(), QStringLiteral("Rotate 180°"));
    QCOMPARE(gui->getResetButton()->text(), QStringLiteral("Reset Rotation"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Rotate 3 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The canvas 389:20210 at x 306, 814 wide: the bar at 20, 20; the tiles from 20, 68
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(grid && grid->isVisible());
    QCOMPARE(H::inWindow(grid), QRect(306, 90, 814, 842));
    QCOMPARE(grid->getHeaderLabel()->text(), QStringLiteral("3 of 48 pages will be turned"));
    QCOMPARE(H::inWindow(grid->getHeaderLabel()).left() + 6, 326);
    QCOMPARE(H::inWindow(grid->getHeaderLabel()).top(), 110);
    QCOMPARE(grid->getHeaderLabel()->height(), 36);

    // The tiles 389:20453: 116 x 132 at the gap 6, six in a row; the selected ones 118 x 150 with their caption
    QCOMPARE(H::tileInWindow(grid, 0), QRect(326, 158, 116, 132));
    QCOMPARE(H::tileInWindow(grid, 1), QRect(448, 158, 116, 132));
    QCOMPARE(H::tileInWindow(grid, 6).topLeft(), QPoint(326, 296));
    QCOMPARE(H::tileInWindow(grid, 8), QRect(570, 296, 118, 150));
    QCOMPARE(H::tileInWindow(grid, 9), QRect(694, 296, 118, 150));
    QCOMPARE(H::tileInWindow(grid, 10).left(), 818);
    QCOMPARE(H::tileInWindow(grid, 12).top(), 452);
    QCOMPARE(H::tileInWindow(grid, 14), QRect(570, 452, 118, 150));

    const pdf::PDFAppToolPageGridModel* model = grid->getModel();
    const pdf::PDFAppToolPageGridModel::Tile tile = model->getTile(8);
    QCOMPARE(tile.state, pdf::PDFComponentPainter::PageTileState::Selected);
    QCOMPARE(tile.caption, QStringLiteral("90° right"));
    QCOMPARE(tile.badgeIconName, QStringLiteral("rotate-cw"));
    QCOMPARE(model->getTile(0).state, pdf::PDFComponentPainter::PageTileState::Normal);

    // The page of the tile is saved turned: landscape (portrait → landscape)
    const QSizeF size = model->index(8).data(pdf::PDFComponentRoles::PageSizeRole).toSizeF();
    QVERIFY(size.width() > size.height());
    QCOMPARE(grid->getHintLabel()->text(), QStringLiteral("The preview shows the pages as they will be saved."));
    // 48 pages do not fit: the grid scrolls, the hint lies at the bottom of the canvas (padding 20)
    QCOMPARE(H::inWindow(grid->getHintLabel()).bottom() + 1, 932 - 20);
    QVERIFY(grid->getView()->verticalScrollBar()->maximum() > 0);

    // Pending rotation 389:20403: 3, the rows "Page 9 · 90° right · portrait → landscape" with Undo
    QCOMPARE(gui->getPendingList()->getHeader()->getTitleLabel()->text(), QStringLiteral("Pending rotation"));
    QCOMPARE(gui->getPendingList()->getHeader()->getHintLabel()->text(), QStringLiteral("3"));
    const pdf::PDFAppToolPendingPagesModel* pending = gui->getPendingModel();
    QCOMPARE(pending->rowCount(), 3);
    QCOMPARE(pending->index(0).data(Qt::DisplayRole).toString(), QStringLiteral("Page 9"));
    QCOMPARE(pending->index(0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("90° right · portrait → landscape"));
    QCOMPARE(pending->index(0).data(pdf::PDFComponentRoles::LinkTextRole).toString(), QStringLiteral("Undo"));
    QCOMPARE(pending->index(0).data(pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("rotate-cw"));
    QCOMPARE(gui->getPendingList()->getView()->visualRect(pending->index(0)).size(), QSize(279, 46));
    QCOMPARE(H::inWindow(gui->getPendingList()).left(), 10);

    // The hint 389:20402
    QCOMPARE(gui->getLeftHint()->text(), QStringLiteral("The selection comes from Thumbnails. Click pages in the preview to change it; every click on a Rotate button turns the selected pages by another 90°."));

    // Summary 389:20891, the callout 389:20904
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("3 · 9, 10, 15"), QStringLiteral("90° right"), QStringLiteral("45 unchanged") }));
    QCOMPARE(gui->getCallout()->getTitleLabel()->text(), QStringLiteral("The pages are turned in the file"));
    QCOMPARE(H::inWindow(gui->getSummary()->parentWidget()).left(), 1137);

    // The status bar 389:20213
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Rotate Pages · 3 pages turned 90° right · the rotation is saved in the document"));
}

void AppToolPagesTest::rotateLandscapeTip()
{
    // 389:20910: All + Landscape selects the pages, which lie on their side; Rotate Left fixes them
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < 18; ++i)
    {
        builder.appendPage(i == 2 || i == 6 || i == 11 ? QRectF(0, 0, 842, 595) : QRectF(0, 0, 595, 842));
    }
    pdf::PDFDocument document = builder.build();
    const QString filePath = fixture.path(QStringLiteral("scan.pdf"));
    QVERIFY(AppDocumentSamples::write(document, filePath));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::All);
    gui->getPageSelection()->getFilterCheckBox(pdf::PDFPageSelection::Filter::Landscape)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 2, 6, 11 }));
    QCOMPARE(gui->getLeftHint()->text(), QStringLiteral("Tip: All + Landscape picks every page that lies on its side – one click on Rotate Left then fixes a whole scan."));

    gui->getRotateLeftButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPendingModel()->index(0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("90° left · landscape → portrait"));
    QCOMPARE(gui->getGrid()->getModel()->getTile(2).badgeIconName, QStringLiteral("rotate-ccw"));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("3 · 3, 7, 12"), QStringLiteral("90° left"), QStringLiteral("15 unchanged") }));

    // The 18 tiles of the frame fit: the hint follows the last row, 10 below it (stage 389:20451:
    // the rows 150 + 150 + 132 with the gaps 6 = 444, the hint at 454 of the stage)
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QCOMPARE(H::tileInWindow(grid, 17).bottom() + 1, 158 + 444);
    QCOMPARE(H::inWindow(grid->getHintLabel()).top(), 158 + 454);
}

void AppToolPagesTest::rotateClicksAndUndo()
{
    // A click on a tile adds its page, another click takes it away, Shift adds a range; each is
    // one undo step of the tool, never of the document
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 12));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // The entry: the current page (no selection in the thumbnails)
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Current);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 0 }));
    QVERIFY(!shell->getModel()->canUndo());

    H::clickTile(gui->getGrid(), 3);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 0, 3 }));
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    H::clickTile(gui->getGrid(), 6, Qt::ShiftModifier);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 0, 3, 4, 5, 6 }));
    H::clickTile(gui->getGrid(), 0);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 3, 4, 5, 6 }));
    QCOMPARE(gui->getGrid()->getModel()->getTile(4).state, pdf::PDFComponentPainter::PageTileState::Selected);

    // Space on the current tile toggles it
    pdf::PDFAppToolPageGridView* view = gui->getGrid()->getView();
    view->setFocus();
    view->setCurrentIndex(view->model()->index(9, 0));
    QTest::keyClick(view, Qt::Key_Space);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 3, 4, 5, 6, 9 }));

    gui->rotateRight();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns().size(), size_t(5));
    const int documentUndo = AppToolShellFixture::undoCount(tab->getDocument());

    // Undo of the tool: the turn, then the click
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QVERIFY(gui->getTurns().empty());
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 3, 4, 5, 6 }));
    shell->getModel()->redo();
    shell->getModel()->redo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns().size(), size_t(5));
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), documentUndo);

    // Undo of a pending row leaves the page as it is
    Q_EMIT gui->getPendingList()->linkActivated(gui->getPendingModel()->index(0));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns().size(), size_t(4));
    QVERIFY(!gui->getTurns().count(3));
    QCOMPARE(gui->getGrid()->getModel()->getTile(3).caption, QString());
}

void AppToolPagesTest::rotateMixedAndReset()
{
    // K14: different turns are "Mixed"; Reset Rotation turns a page with /Rotate 90 to 0 (Q9)
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < 4; ++i)
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
        if (i == 1)
        {
            builder.setPageRotation(page, pdf::PageRotation::Rotate90);
        }
    }
    pdf::PDFDocument document = builder.build();
    const QString filePath = fixture.path(QStringLiteral("mixed.pdf"));
    QVERIFY(AppDocumentSamples::write(document, filePath));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->rotateRight();
    H::setPages(shell, QStringLiteral("2"));
    gui->resetRotation();
    H::setPages(shell, QStringLiteral("3"));
    gui->rotateHalf();
    AppToolShellFixture::settle();

    QCOMPARE(gui->getTurns().at(0), pdf::PageRotation::Rotate90);
    QCOMPARE(gui->getTurns().at(1), pdf::PageRotation::Rotate270);
    QCOMPARE(gui->getTurns().at(2), pdf::PageRotation::Rotate180);
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("Mixed"));
    QCOMPARE(gui->getGrid()->getModel()->getTile(1).caption, QStringLiteral("90° left"));
    QCOMPARE(gui->getGrid()->getModel()->getTile(2).badgeIconName, QStringLiteral("rotate-180"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Rotate Pages · 3 pages turned · the rotation is saved in the document"));
    QCOMPARE(gui->getPendingModel()->index(2).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("180° · portrait"));

    // A second Rotate Right of page 1 turns it by another 90°: 180°
    H::setPages(shell, QStringLiteral("1"));
    gui->rotateRight();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns().at(0), pdf::PageRotation::Rotate180);

    // Reset of a page without /Rotate removes its pending turn
    gui->resetRotation();
    AppToolShellFixture::settle();
    QVERIFY(!gui->getTurns().count(0));
}

void AppToolPagesTest::rotateWithoutTurnsDisabled()
{
    // K15 and no page selected: the actions do nothing, the Primary button is disabled
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 3));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    H::clickTile(gui->getGrid(), 0);
    QVERIFY(gui->getTargetPages().empty());
    QVERIFY(!gui->getRotateRightButton()->isEnabled());
    QVERIFY(!gui->getResetButton()->isEnabled());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("0 of 3 pages will be turned"));
    QVERIFY(!gui->getPendingList()->isVisible());

    // The pages selected in the thumbnails do not come back by themselves
    tab->getView()->setSelectedPages({ 1, 2 });
    AppToolShellFixture::settle();
    QVERIFY(gui->getTargetPages().empty());
}

void AppToolPagesTest::rotateEveryPageListed()
{
    // A turn of every page (a whole scan) is a valid plan: Pending rotation (389:20403) lists
    // every page with its Undo; only Remove Pages refuses every page (388:21466 has no list)
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("scan.pdf")), 3));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    H::setPages(shell, QStringLiteral("1-3"));
    gui->rotateLeft();
    AppToolShellFixture::settle();

    QCOMPARE(gui->getTurns().size(), size_t(3));
    QCOMPARE(gui->getPendingModel()->rowCount(), 3);
    QVERIFY(gui->getPendingList()->isVisible());
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("3 of 3 pages will be turned"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // Undo of a row of the list keeps that page as it is
    Q_EMIT gui->getPendingList()->linkActivated(gui->getPendingModel()->index(1));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns().size(), size_t(2));
    QVERIFY(!gui->getTurns().count(1));
    QVERIFY(gui->getPendingList()->isVisible());
}

void AppToolPagesTest::rotateRunKeepsInTab()
{
    // 389:21590: Pages rotated, Keep in this tab preselected; the document has its pages
    // turned as one undo step
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 20));
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    AppToolShellFixture::settle();
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    gui->rotateRight();
    AppToolShellFixture::settle();
    const int undoBefore = AppToolShellFixture::undoCount(tab->getDocument());

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Rotate Pages finished · the result is waiting for your decision"));

    // Nothing to turn below the dialog (K2)
    QVERIFY(!gui->getRotateRightButton()->isEnabled());

    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();

    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    QCOMPARE(document->getCatalog()->getPage(8)->getPageRotation(), pdf::PageRotation::Rotate90);
    QCOMPARE(document->getCatalog()->getPage(14)->getPageRotation(), pdf::PageRotation::Rotate90);
    QCOMPARE(document->getCatalog()->getPage(10)->getPageRotation(), pdf::PageRotation::None);
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), undoBefore + 1);
}

void AppToolPagesTest::rotateEntryCurrentPage()
{
    // Without a selection of the thumbnails the tool works on the current page, with one
    // selected page, which is not the current one, on that page
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 8));
    tab->getView()->goToPage(4);
    AppToolShellFixture::settle();
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 4 }));
    QVERIFY(!shell->getModel()->canUndo());
    shell->cancel();
    AppToolShellFixture::settle();

    tab->getView()->setSelectedPages({ 6 });
    AppToolShellFixture::settle();
    shell = fixture.openTool(tab, H::REMOVE_ID);
    gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 6 }));
}

void AppToolPagesTest::thumbnailsCommandStillRotates()
{
    // The commands of the thumbnails go through PDFDocumentPageOperations::rotatePages (2.7)
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 4));
    tab->getView()->setSelectedPages({ 1, 2 });
    AppToolShellFixture::settle();
    tab->executeCommand(pdf::PDFAppCommands::rotatePagesLeft());
    AppToolShellFixture::settle();
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    QCOMPARE(document->getCatalog()->getPage(1)->getPageRotation(), pdf::PageRotation::Rotate270);
    QCOMPARE(document->getCatalog()->getPage(2)->getPageRotation(), pdf::PageRotation::Rotate270);
    QCOMPARE(document->getCatalog()->getPage(0)->getPageRotation(), pdf::PageRotation::None);
}

void AppToolPagesTest::removeFrame()
{
    // 388:20614: 48 pages A4, the range "4-6, 20"
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::Range);
    QTest::keyClicks(gui->getPageSelection()->getRangeField(), QStringLiteral("4-6, 20"));
    AppToolShellFixture::settle();
    // "4-6," on the way is a range of the field, which the core refuses (its error is shown); the
    // next valid text is written again
    // The status of the range of PDFPageSelection says its first and last page ("4 – 20"), the
    // frame 388:20614 names the pages ("4–6, 20"): a deviation of the component, in the report
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 3, 4, 5, 19 }));

    // The toolbar 388:20620: Keep All Pages, Remove 4 Pages
    QCOMPARE(gui->getKeepAllButton()->text(), QStringLiteral("Keep All Pages"));
    QVERIFY(gui->getKeepAllButton()->isEnabled());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 4 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The canvas: "44 of 48 pages stay", tiles 94 x 140, marked 94 x 156, seven in a row
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QCOMPARE(grid->getHeaderLabel()->text(), QStringLiteral("44 of 48 pages stay"));
    QCOMPARE(H::tileInWindow(grid, 0), QRect(326, 158, 94, 140));
    QCOMPARE(H::tileInWindow(grid, 3), QRect(626, 158, 94, 156));
    QCOMPARE(H::tileInWindow(grid, 7).topLeft(), QPoint(326, 320));
    QCOMPARE(H::tileInWindow(grid, 19), QRect(826, 466, 94, 156));
    const pdf::PDFAppToolPageGridModel::Tile tile = grid->getModel()->getTile(3);
    QCOMPARE(tile.state, pdf::PDFComponentPainter::PageTileState::Marked);
    QCOMPARE(tile.caption, QStringLiteral("will be removed"));
    QCOMPARE(tile.badgeIconName, QStringLiteral("trash"));
    QCOMPARE(grid->getHintLabel()->text(), QStringLiteral("Click a page to mark it or keep it; Shift and Ctrl select more pages. Marked pages stay visible until you confirm."));

    // Marked for removal 388:20818: "Page 4", "A4 portrait", the number in the red tile, Keep
    QCOMPARE(gui->getPendingList()->getHeader()->getTitleLabel()->text(), QStringLiteral("Marked for removal"));
    QCOMPARE(gui->getPendingList()->getHeader()->getHintLabel()->text(), QStringLiteral("4"));
    const pdf::PDFAppToolPendingPagesModel* pending = gui->getPendingModel();
    QCOMPARE(pending->rowCount(), 4);
    QCOMPARE(pending->index(3).data(Qt::DisplayRole).toString(), QStringLiteral("Page 20"));
    QCOMPARE(pending->index(3).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("A4 portrait"));
    QCOMPARE(pending->index(3).data(pdf::PDFComponentRoles::TileTextRole).toString(), QStringLiteral("20"));
    QCOMPARE(pending->index(3).data(pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Danger));
    QCOMPARE(pending->index(3).data(pdf::PDFComponentRoles::LinkTextRole).toString(), QStringLiteral("Keep"));

    // Summary 388:21455 and the note 388:21465
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("4 pages · 4–6, 20"), QStringLiteral("44 pages") }));
    QVERIFY(!gui->getAllPagesCallout()->isVisible());
    QCOMPARE(gui->getNote()->text(), QStringLiteral("Removed pages come back with Undo until the document is closed. To keep them in another file first, use Extract Pages."));

    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Remove Pages · 4 pages will be removed · 44 of 48 pages stay"));
}

void AppToolPagesTest::removeEveryPage()
{
    // 388:21466: every page marked
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::All);
    AppToolShellFixture::settle();

    QVERIFY(gui->isEveryPageMarked());
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("No page would stay"));
    QVERIFY(gui->getAllPagesCallout()->isVisible());
    QCOMPARE(gui->getAllPagesCallout()->getTitleLabel()->text(), QStringLiteral("All pages are selected"));
    QCOMPARE(H::inWindow(gui->getAllPagesCallout()).topLeft(), QPoint(1137, 104));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("48 pages"), QStringLiteral("0 pages") }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!gui->getPendingList()->isVisible());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Remove Pages · a document must keep at least one page"));

    // Ctrl+Enter does nothing
    shell->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
}

void AppToolPagesTest::removeKeepAndDelete()
{
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 10));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    H::setPages(shell, QStringLiteral("2-4"));

    // Keep of a row
    Q_EMIT gui->getPendingList()->linkActivated(gui->getPendingModel()->index(1));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 3 }));

    // Delete marks the current tile
    pdf::PDFAppToolPageGridView* view = gui->getGrid()->getView();
    view->setFocus();
    view->setCurrentIndex(view->model()->index(7, 0));
    QTest::keyClick(view, Qt::Key_Delete);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 3, 7 }));

    // Keep All Pages: no page, not every page (the mode Selected without pages)
    gui->getKeepAllButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getTargetPages().empty());
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("10 of 10 pages stay"));

    // Undo brings the marks back, through the value of no page
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 3, 7 }));
    shell->getModel()->redo();
    AppToolShellFixture::settle();
    QVERIFY(gui->getTargetPages().empty());
}

void AppToolPagesTest::removeRunKeepsInTab()
{
    // 388:22315: Pages removed; Keep in this tab
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    H::setPages(shell, QStringLiteral("4-6, 20"));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
    QVERIFY(!gui->getKeepAllButton()->isEnabled());

    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QCOMPARE(tab->getDocument()->getPageCount(), 44);
}

void AppToolPagesTest::gridOneChangeForManyTiles()
{
    // 2000 pages: Select All marks every tile by one dataChanged of the range (each emission
    // queues a layout of all tiles)
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("large.pdf")), 2000, QSizeF(300, 400)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QSignalSpy changes(gui->getGrid()->getModel(), &QAbstractItemModel::dataChanged);
    QElapsedTimer timer;
    timer.start();
    gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::All);
    gui->rotateRight();
    AppToolShellFixture::settle();
    qInfo("2000 tiles turned in %lld ms", timer.elapsed());

    // The changes of the tiles (no roles; a picture, which comes, has PixmapRole)
    std::vector<std::pair<int, int>> tileChanges;
    for (const QList<QVariant>& change : changes)
    {
        if (change.at(2).value<QList<int>>().isEmpty())
        {
            tileChanges.emplace_back(change.at(0).toModelIndex().row(), change.at(1).toModelIndex().row());
        }
    }
    QCOMPARE(tileChanges.size(), size_t(2));
    QCOMPARE(tileChanges.back(), std::make_pair(0, 1999));
    QCOMPARE(gui->getTurns().size(), size_t(2000));

    // Only the visible tiles ask for their pictures
    QVERIFY(gui->getGrid()->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_COMPILES);
}

void AppToolPagesTest::themesAndDirection()
{
    // No color of the system in the parts, both themes, right-to-left
    using H = PagesGuiHelper;
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            pdf::PDFThemeManager::getInstance()->setTheme(theme);
            QApplication::setLayoutDirection(direction);
            AppToolShellFixture fixture(QSize(1440, 960));
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 12));
            tab->getView()->setSelectedPages({ 2, 3 });
            AppToolShellFixture::settle();
            PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
            QVERIFY(shell);
            AppToolShellFixture::settle();
            QCOMPARE(H::countLeak(fixture.window), 0);
            if (direction == Qt::RightToLeft)
            {
                // The left panel lies at the trailing side
                QVERIFY(H::inWindow(shell->getLeftPanel()).left() > 1000);
            }
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolPagesTest::minimalWindow()
{
    // 720 x 480: the grid scrolls inside of the canvas, the hint stays below it
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 40));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    const QRect grid = H::inWindow(gui->getGrid());
    const QRect hint = H::inWindow(gui->getGrid()->getHintLabel());
    QVERIFY(grid.contains(hint));
    QVERIFY(gui->getGrid()->getView()->verticalScrollBar()->maximum() > 0);
    QCOMPARE(H::countLeak(fixture.window), 0);
}

void AppToolPagesTest::languageChange()
{
    // The parts are built again with the translated texts, the state stays
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 10));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    H::setPages(shell, QStringLiteral("2-3"));
    gui->rotateRight();
    AppToolShellFixture::settle();

    pdftest::PagesTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QVERIFY(gui->getGrid()->getHeaderLabel()->text().startsWith(QStringLiteral("[T] ")));
    QVERIFY(gui->getPendingList()->getHeader()->getTitleLabel()->text().startsWith(QStringLiteral("[T] ")));
    QCOMPARE(gui->getTurns().size(), size_t(2));
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 2 }));
    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("2 of 10 pages will be turned"));
}

void AppToolPagesTest::deletedWithToolOpen()
{
    // The tab or the window deleted with the tool open, with a click queued by the grid
    using H = PagesGuiHelper;
    for (const bool isWindow : { false, true })
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 6));
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
        PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
        QVERIFY(gui);
        AppToolShellFixture::settle();

        QPointer<PDFAppToolGUIRemovePages> guiGuard(gui);
        QPointer<pdf::PDFAppToolPageGrid> gridGuard(gui->getGrid());
        pdf::PDFAppToolPageGridView* view = gui->getGrid()->getView();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(2, 0)).center());
        if (isWindow)
        {
            delete fixture.window.data();
        }
        else
        {
            delete tab;
        }
        AppToolShellFixture::settle();
        QVERIFY(!guiGuard);
        QVERIFY(!gridGuard);
    }
}

void AppToolPagesTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS: the frames under their ids
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    AppToolShellFixture::settle();
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    H::gui<PDFAppToolGUIRotatePages>(shell)->rotateRight();
    AppToolShellFixture::settle();
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("389-20177-pages-rotate"));
    shell->cancel();
    AppToolShellFixture::settle();

    shell = fixture.openTool(tab, H::REMOVE_ID);
    H::setPages(shell, QStringLiteral("4-6, 20"));
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("388-20614-pages-remove"));
    H::setPages(shell, QString());
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("388-21466-pages-remove-all"));
}

void AppToolPagesTest::removeMarksFollowPages()
{
    // Owner 2026-11-06: an assistant removes or moves pages below the open tool (pages.md) - the
    // marks stay on their pages, a page, which is gone, loses its mark; the counts, the Primary
    // button, the tiles and the steps of Undo follow
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 10));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->setTargetPages({});
    for (const int page : { 1, 2, 4, 7 })
    {
        H::clickTile(gui->getGrid(), page);
    }
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 2, 4, 7 }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 4 Pages"));

    // The assistant removes page 2, which is marked: pages 3, 5, 8 are pages 2, 4, 7 now
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 1)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPageCount(), 9);
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 3, 6 }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 3 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(gui->getPageSelection()->getSelectedPages(), QList<int>({ 2, 4, 7 }));
    QCOMPARE(H::pendingTitles(gui->getPendingModel()), QStringList({ QStringLiteral("Page 2"), QStringLiteral("Page 4"), QStringLiteral("Page 7") }));
    const pdf::PDFAppToolPageGridModel* tiles = gui->getGrid()->getModel();
    for (int page = 0; page < 9; ++page)
    {
        const bool isMarked = page == 1 || page == 3 || page == 6;
        QCOMPARE(tiles->getTile(page).state == pdf::PDFComponentPainter::PageTileState::Marked, isMarked);
    }
    QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("6 of 9 pages stay"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Remove Pages · 3 pages will be removed · 6 of 9 pages stay"));

    // The assistant moves the last page to the front: the marks move with their pages
    QVERIFY(H::commitAssistantChange(document, H::moveLastFirst(document)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 2, 4, 7 }));
    QVERIFY(tiles->getTile(1).state != pdf::PDFComponentPainter::PageTileState::Marked);
    QCOMPARE(tiles->getTile(7).state, pdf::PDFComponentPainter::PageTileState::Marked);

    // Undo of the tool: the step before the last click marked the pages 2, 3, 5 of the first
    // version - page 2 is gone, pages 3 and 5 are pages 3 and 5 of this version again
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 2, 4 }));
    shell->getModel()->redo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 2, 4, 7 }));

    // A typed range follows too; the run removes the marked pages of this version
    H::setPages(shell, QStringLiteral("1, 9"));
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Range);
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 4)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Range);
    QCOMPARE(gui->getPageSelection()->getRange(), QStringLiteral("1, 8"));
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 0, 7 }));

    // The marked pages go: every mark gone leaves no page marked
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 7)));
    AppToolShellFixture::settle();
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 0)));
    AppToolShellFixture::settle();
    QVERIFY(gui->getTargetPages().empty());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolPagesTest::rotatePlanFollowsPages()
{
    // The plan of Rotate Pages follows its pages as the marks of Remove Pages do
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("named.pdf")), 10));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // Page 9 right, page 2 upside down
    gui->setTargetPages({ 8 });
    gui->rotateRight();
    gui->setTargetPages({ 1 });
    gui->rotateHalf();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns(), (std::map<pdf::PDFInteger, pdf::PageRotation>{ { 1, pdf::PageRotation::Rotate180 }, { 8, pdf::PageRotation::Rotate90 } }));

    // The assistant removes page 1: pages 2 and 9 are pages 1 and 8
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 0)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns(), (std::map<pdf::PDFInteger, pdf::PageRotation>{ { 0, pdf::PageRotation::Rotate180 }, { 7, pdf::PageRotation::Rotate90 } }));
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 0 }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Rotate 2 Pages"));
    QCOMPARE(H::pendingTitles(gui->getPendingModel()), QStringList({ QStringLiteral("Page 1"), QStringLiteral("Page 8") }));
    const pdf::PDFAppToolPageGridModel* tiles = gui->getGrid()->getModel();
    QCOMPARE(tiles->getTile(7).extraRotation, pdf::PageRotation::Rotate90);
    QCOMPARE(tiles->getTile(7).caption, QStringLiteral("90° right"));
    QCOMPARE(tiles->getTile(0).extraRotation, pdf::PageRotation::Rotate180);
    QCOMPARE(tiles->getTile(8).extraRotation, pdf::PageRotation::None);

    // The assistant removes the upside down page: its turn and its selection go
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 0)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTurns(), (std::map<pdf::PDFInteger, pdf::PageRotation>{ { 6, pdf::PageRotation::Rotate90 } }));
    QVERIFY(gui->getTargetPages().empty());
    QVERIFY(!gui->getRotateRightButton()->isEnabled());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The run turns page 9 of the first version
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    QPointer<PDFDocumentToolShell> guard(shell);
    shell->getResultDialog()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    const pdf::PDFDocumentPointer result = tab->getDocument()->getDocument();
    QCOMPARE(H::pageNames(result.data()), QStringLiteral("3 4 5 6 7 8 9 10"));
    for (size_t i = 0; i < result->getCatalog()->getPageCount(); ++i)
    {
        QCOMPARE(result->getCatalog()->getPage(i)->getPageRotation(), i == 6 ? pdf::PageRotation::Rotate90 : pdf::PageRotation::None);
    }
}

void AppToolPagesTest::mcpRerunFollowsPages()
{
    // pages.md: the document changes while the tool runs - keep runs it again on the new
    // version; the pages of the call are the pages of the version it saw (owner 2026-11-06)
    using H = PagesGuiHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(H::writeNamedPages(fixture.path(QStringLiteral("rerun.pdf")), 5));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();

    const QString id = H::REMOVE_ID;
    const std::shared_ptr<const pdf::PDFDocumentTool> previous = pdf::PDFDocumentToolRegistry::getTool(id);
    const std::shared_ptr<PagesTestRemoveWithChange> tool = std::make_shared<PagesTestRemoveWithChange>();
    pdf::PDFDocumentToolRegistry::unregisterTool(id);
    pdf::PDFDocumentToolRegistry::registerTool(tool);
    fixture.host()->checkToolRegistry();
    const auto restore = qScopeGuard([&]()
    {
        pdf::PDFDocumentToolRegistry::unregisterTool(id);
        pdf::PDFDocumentToolRegistry::registerTool(previous);
        fixture.host()->checkToolRegistry();
    });

    // The first run: the assistant removes page 1 meanwhile (in the thread of the user interface)
    int changesLeft = 0;
    QPointer<pdf::PDFAppDocument> documentGuard(document);
    tool->beforeRun = [&changesLeft, documentGuard]()
    {
        QMetaObject::invokeMethod(documentGuard.data(), [&changesLeft, documentGuard]()
        {
            if (changesLeft > 0 && documentGuard)
            {
                --changesLeft;
                PagesGuiHelper::commitAssistantChange(documentGuard.data(), PagesGuiHelper::removePage(documentGuard.data(), 0));
            }
        }, Qt::BlockingQueuedConnection);
    };

    const auto call = [&](const QString& pages)
    {
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), QStringLiteral("remove"));
        arguments.insert(QStringLiteral("documentId"), document->getId());
        arguments.insert(QStringLiteral("result"), QStringLiteral("keep"));
        arguments.insert(QStringLiteral("remove"), QJsonObject({ { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), pages } }) } }));
        return pdftest::AppMcpFixture::waitResult(fixture.startCall(QStringLiteral("pages"), arguments));
    };

    // Page 4 of the call is page 3 of the new version: that one goes, not the new page 4
    changesLeft = 1;
    std::optional<pdf::PDFMcpCallResult> result = call(QStringLiteral("4"));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(changesLeft, 0);
    QCOMPARE(H::pageNames(document->getDocument().data()), QStringLiteral("2 3 5"));
    const QJsonObject data = result->toQJson().value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("remove")).toObject().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("removedPages")).toArray(), QJsonArray({ 3 }));

    // The page of the call is gone meanwhile: nothing more is removed, the call says so
    changesLeft = 1;
    result = call(QStringLiteral("1"));
    QVERIFY(result);
    QVERIFY(result->isError);
    QCOMPARE(H::pageNames(document->getDocument().data()), QStringLiteral("3 5"));
}

void AppToolPagesTest::pagesNeedAssemble()
{
    // ISO 32000-2, Table 22: bit 11 "Assemble the document (insert, rotate, or delete pages ...)";
    // bit 4 covers the changes other than those of bits 6, 9 and 11 - a document, which allows
    // every change but the assembly, refuses Rotate Pages and Remove Pages in the panel and in MCP
    using H = PagesGuiHelper;
    const uint32_t withoutAssemble = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Assemble);
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("locked.pdf")), 3, withoutAssemble));
        QVERIFY(tab);
        for (const QString& id : { H::ROTATE_ID, H::REMOVE_ID })
        {
            PDFDocumentToolShell* shell = fixture.openTool(tab, id);
            QVERIFY(shell);
            AppToolShellFixture::settle();
            QVERIFY2(shell->isPermissionRefused(), qPrintable(id));
            QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
            QPointer<PDFDocumentToolShell> guard(shell);
            shell->cancel();
            QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
            AppToolShellFixture::settle();
        }
    }

    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(H::writeNamedPages(fixture.path(QStringLiteral("locked.pdf")), 3, withoutAssemble));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    QJsonObject arguments;
    arguments.insert(QStringLiteral("action"), QStringLiteral("rotate"));
    arguments.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    arguments.insert(QStringLiteral("rotate"), QJsonObject({ { QStringLiteral("rotation"), QStringLiteral("right") } }));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("The permissions of the document do not allow this tool.")), qPrintable(result->getFirstText()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);

    // With the assembly allowed and the modification not, the pages turn
    pdf::PDFAppDocumentTab* assembleTab = fixture.open(H::writeNamedPages(fixture.path(QStringLiteral("assemble.pdf")), 3, uint32_t(pdf::PDFSecurityHandler::Permission::Assemble)));
    QVERIFY(assembleTab);
    QTRY_VERIFY(assembleTab->getDocument()->isReady());
    arguments.insert(QStringLiteral("documentId"), assembleTab->getDocument()->getId());
    result = fixture.callTool(QStringLiteral("pages"), arguments);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(assembleTab->getDocument()->getDocument()->getCatalog()->getPage(0)->getPageRotation(), pdf::PageRotation::Rotate90);
}

QTEST_MAIN(AppToolPagesTest)

#include "tst_apptoolpagestest.moc"
