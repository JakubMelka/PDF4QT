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
#include "mergetestpdf.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmcphost.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdffluentwidgets.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumenttool_structure.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdrawspacecontroller.h"
#include "pdfexecutionpolicy.h"
#include "pdfmcpcall.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfthreadannotations.h"
#include "pdfwidgetstyle.h"

#include <QPainter>
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

    /// The frames draw the tiles in the size Small (the slot of the design; owner 2026-11-07:
    /// Medium, 1.5 times of it, is the default): the setting of the page grids for a test of
    /// the geometry of a frame
    static void useFrameTileSize(AppToolShellFixture& fixture)
    {
        QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::toolsPageGridSize(), QStringLiteral("small")).isError());
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

    /// Pages covered by a red image of the size (a page, whose rendering takes a while: the
    /// image is decoded and scaled down for each picture); the pages share the content and
    /// the image of the first one
    static QString writeHeavyPages(const QString& filePath, int pageCount, int imageSize)
    {
        QImage image(imageSize, imageSize, QImage::Format_RGB32);
        image.fill(Qt::red);
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference firstPage = builder.appendPage(QRectF(0, 0, 300, 400));
        {
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(firstPage);
            if (!painter)
            {
                return QString();
            }
            painter->drawImage(QRectF(0, 0, 300, 400), image);
            content.end(painter);
        }

        const pdf::PDFDictionary* firstDictionary = builder.getObjectByReference(firstPage).getDictionary();
        const pdf::PDFObject contents = firstDictionary->get("Contents");
        const pdf::PDFObject resources = firstDictionary->get("Resources");
        for (int i = 1; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
            pdf::PDFDictionaryBuilder shared;
            shared.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), contents);
            shared.setEntry(pdf::PDFInplaceOrMemoryString("Resources"), resources);
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(shared)));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// Visible tiles of the grid and those of them, which have their pictures
    static std::pair<int, int> countVisiblePictures(const pdf::PDFAppToolPageGrid* grid)
    {
        const pdf::PDFAppToolPageGridView* view = grid->getView();
        int visible = 0;
        int pictures = 0;
        for (int i = 0; i < grid->getModel()->rowCount(); ++i)
        {
            if (view->visualRect(grid->getModel()->index(i)).intersects(view->viewport()->rect()))
            {
                ++visible;
                pictures += grid->getModel()->hasPicture(i) ? 1 : 0;
            }
        }
        return { visible, pictures };
    }

    static bool waitForVisiblePictures(const pdf::PDFAppToolPageGrid* grid)
    {
        return AppToolShellFixture::waitFor([grid]() { const std::pair<int, int> count = countVisiblePictures(grid); return count.first > 0 && count.first == count.second; }, 60000);
    }

    /// Color of the center of the picture of the page (the picture of the thumbnails)
    static QColor getPictureCenter(const pdf::PDFAppToolPageGrid* grid, int page)
    {
        const QPixmap pixmap = grid->getModel()->index(page).data(pdf::PDFComponentRoles::PixmapRole).value<QPixmap>();
        const QImage image = pixmap.toImage();
        return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
    }

    /// The colors are the same up to the rounding of the scaling of an image (2 of 255)
    static bool isNear(const QColor& color, const QColor& expected)
    {
        return color.isValid() && qAbs(color.red() - expected.red()) <= 2 && qAbs(color.green() - expected.green()) <= 2 && qAbs(color.blue() - expected.blue()) <= 2;
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
    void certifiedDocumentOnlyCopy_data();
    void certifiedDocumentOnlyCopy();
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
    void marksSurviveWholeFileRewrite();

    void gridPicturesInParallel();
    void gridPicturesLeaveQueueScrolledAway();

    void selectionOfThumbnailsAtOpen_data();
    void selectionOfThumbnailsAtOpen();
    void rangeWithoutTextSelectsNoPage_data();
    void rangeWithoutTextSelectsNoPage();
    void gridTileSize_data();
    void gridTileSize();
    void gridPicturesOfHostilePages();
    void gridWorkersLeaveGlobalPoolWhenIdle();
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
    H::useFrameTileSize(fixture);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    QVERIFY(tab);
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    AppToolShellFixture::settle();

    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // page-selection: Selected, "3 pages selected · 9, 10, 15" of the frame; owner D3 (2026-11-10):
    // a run of two pages is a range ("9–10, 15")
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(gui->getPageSelection()->getStatusText(), QStringLiteral("3 pages selected · 9–10, 15"));
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

    // Summary 389:20891 (owner D3: "3 · 9–10, 15"), the callout 389:20904
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("3 · 9–10, 15"), QStringLiteral("90° right"), QStringLiteral("45 unchanged") }));
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
    H::useFrameTileSize(fixture);
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

void AppToolPagesTest::certifiedDocumentOnlyCopy_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::newRow("rotate") << PagesGuiHelper::ROTATE_ID;
    QTest::newRow("remove") << PagesGuiHelper::REMOVE_ID;
}

void AppToolPagesTest::certifiedDocumentOnlyCopy()
{
    // Q12, R10: no level of a certification allows a change of the pages (ISO 32000-2, 12.8.2.2,
    // Table 257) - the result dialog offers only Save as a copy with the reason; the copy is
    // written whole (one end of the file, no incremental update) without /Perms, the open
    // document and its file stay as they are
    QFETCH(QString, toolId);
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("C", 3, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >>", { { 1, "/Annots [60 0 R]" } });
    pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>] >>");
    const QString filePath = fixture.path(QStringLiteral("Contract.pdf"));
    const QByteArray original = pdf.write();
    {
        QFile file(filePath);
        QVERIFY(file.open(QFile::WriteOnly) && file.write(original) == original.size());
    }
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    if (toolId == H::ROTATE_ID)
    {
        H::gui<PDFAppToolGUIRotatePages>(shell)->rotateRight();
    }
    else
    {
        H::setPages(shell, QStringLiteral("3"));
    }
    AppToolShellFixture::settle();

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Keep));
    QVERIFY(!dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Overwrite));
    QVERIFY(dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Copy));
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Copy);
    const pdf::PDFLabel* warningTitle = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("signedWarningTitle"));
    QVERIFY(warningTitle);
    QCOMPARE(warningTitle->text(), QStringLiteral("The certification is removed in the copy"));

    const QString target = dialog->getTargetFilePath();
    QVERIFY(!target.isEmpty());
    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));

    QFile copy(target);
    QVERIFY(copy.open(QFile::ReadOnly));
    const QByteArray written = copy.readAll();
    QCOMPARE(written.count("%%EOF"), qsizetype(1));
    QVERIFY(!written.contains("/Perms"));
    QFile source(filePath);
    QVERIFY(source.open(QFile::ReadOnly));
    QCOMPARE(source.readAll(), original);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(3));
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
    H::useFrameTileSize(fixture);
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
    // The status names the pages as the frame 388:20614 does (owner D2: canonical ranges)
    QCOMPARE(gui->getPageSelection()->getStatusText(), QStringLiteral("4 pages selected · 4–6, 20"));
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
    QVERIFY(gui->getGrid()->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_PAGES);
}

void AppToolPagesTest::themesAndDirection()
{
    // No color of the system in the parts, both themes, right-to-left
    using H = PagesGuiHelper;
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            // The fixture sets the light scheme of its context: the theme of the row comes after it
            AppToolShellFixture fixture(QSize(1440, 960));
            pdf::PDFThemeManager::getInstance()->setTheme(theme);
            AppToolShellFixture::settle();
            QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), theme == QLatin1String("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
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
    H::useFrameTileSize(fixture);
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
        // Posted from the worker thread: the copy of the functor into the event of Qt
        // is read by the thread of the user interface (CLAUDE.md, TransportScope)
        pdf::PDFThreadAnnotations::TransportScope transportScope;
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
    // The shared check of the application names the missing permission and the unlock by the
    // user (owner 2026-11-08), before the run
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("The document does not allow inserting, rotating and removing pages (its security settings, ISO 32000-2 Table 22).")), qPrintable(result->getFirstText()));
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

void AppToolPagesTest::marksSurviveWholeFileRewrite()
{
    // An assistant runs Optimize Structure (keep) below the open Remove Pages: the whole file is
    // written again and Shrink object storage renumbers the objects (tools.md 4, "Renumbers objects
    // without gaps"). No page is removed or moved, so every mark stays on its page (owner
    // 2026-11-06: only a page, which no longer exists, loses its mark)
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("rewrite.pdf")), 10));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::REMOVE_ID);
    PDFAppToolGUIRemovePages* gui = H::gui<PDFAppToolGUIRemovePages>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->setTargetPages({});
    for (const int page : { 2, 7, 9 })
    {
        H::clickTile(gui->getGrid(), page);
    }
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 2, 7, 9 }));

    // The assistant removes page 1 (its objects leave free entries), then rewrites the file
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::commitAssistantChange(document, H::removePage(document, 0)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 6, 8 }));

    const pdf::PDFDocumentPointer before = document->getDocument();
    const pdf::PDFDocumentToolStructure structure;
    const pdf::PDFDocumentToolResult result = structure.run(pdf::PDFDocumentToolRunner::createContext(document->getSnapshot()), QVariantMap(), nullptr, nullptr);
    const pdf::PDFDocumentToolOutputDocument* output = pdf::PDFDocumentToolRunner::findChangedDocument(result);
    QVERIFY2(output, qPrintable(result.message));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(output->document, document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();

    // The case of the test: the same pages, some of them under other object numbers
    const pdf::PDFDocumentPointer after = document->getDocument();
    QCOMPARE(H::pageNames(after.data()), H::pageNames(before.data()));
    size_t renumbered = 0;
    for (size_t i = 0; i < after->getCatalog()->getPageCount(); ++i)
    {
        renumbered += after->getCatalog()->getPage(i)->getPageReference() != before->getCatalog()->getPage(i)->getPageReference() ? 1 : 0;
    }
    QVERIFY2(renumbered > 0, "the rewrite has kept every page object under its number");

    // Each page is at its place (also for the rerun of a call of MCP, PDFDocumentTool::mapParameters)
    QCOMPARE(pdf::PDFDocumentToolPageSelection::mapPageIndices(before.data(), after.data()), std::vector<pdf::PDFInteger>({ 0, 1, 2, 3, 4, 5, 6, 7, 8 }));
    QCOMPARE(gui->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 6, 8 }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 3 Pages"));
}

void AppToolPagesTest::gridPicturesInParallel()
{
    // Owner 2026-11-07: the tiles of a page grid are rendered in parallel, each picture shows
    // its page as the view draws it (the color mode of the view included)
    using H = PagesGuiHelper;
    QVERIFY(pdf::PDFAppThumbnailScheduler::getRenderThreadCount() >= 2);
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeHeavyPages(fixture.path(QStringLiteral("heavy.pdf")), 36, 1000));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(H::waitForVisiblePictures(grid));

    // More pages at once (the visible tiles are many more than one), never more than the threads
    const pdf::PDFAppThumbnailScheduler* scheduler = grid->getScheduler();
    QVERIFY(scheduler->getMaximumRenderingCount() >= 2);
    QVERIFY(scheduler->getMaximumRenderingCount() <= pdf::PDFAppThumbnailScheduler::getRenderThreadCount());
    QVERIFY(scheduler->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_PAGES);

    // The page is red
    QVERIFY2(H::isNear(H::getPictureCenter(grid, 0), QColor(255, 0, 0)), qPrintable(H::getPictureCenter(grid, 0).name()));

    // Inverted colors of the view: every picture is rendered again, red becomes cyan
    pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();
    proxy->setFeatures(proxy->getFeatures() | pdf::PDFRenderer::ColorAdjust_Invert);
    QVERIFY(AppToolShellFixture::waitFor([grid]() { return H::isNear(H::getPictureCenter(grid, 0), QColor(0, 255, 255)); }, 60000));
}

void AppToolPagesTest::gridPicturesLeaveQueueScrolledAway()
{
    // The queued tiles, which scroll away before a worker takes them, are not rendered: one
    // thread renders the pages one after another, the grid scrolls to its end at once
    using H = PagesGuiHelper;
    const int threadCount = pdf::PDFExecutionPolicy::getMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page);
    pdf::PDFExecutionPolicy::setMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page, 1);
    auto restore = qScopeGuard([threadCount]() { pdf::PDFExecutionPolicy::setMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page, threadCount); });
    QCOMPARE(pdf::PDFAppThumbnailScheduler::getRenderThreadCount(), 1);

    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeHeavyPages(fixture.path(QStringLiteral("heavy.pdf")), 80, 1600));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    pdf::PDFAppToolPageGridView* view = grid->getView();

    // The first screen is queued; its pictures drawn by now
    const std::pair<int, int> first = H::countVisiblePictures(grid);
    QVERIFY(first.first >= 12);
    QVERIFY(first.second < first.first);
    view->scrollToBottom();
    QVERIFY(H::waitForVisiblePictures(grid));

    // Only the page, which the worker had taken, has come after the scroll
    int topPictures = 0;
    for (int i = 0; i < first.first; ++i)
    {
        topPictures += grid->getModel()->hasPicture(i) ? 1 : 0;
    }
    QVERIFY2(topPictures <= first.second + 1, qPrintable(QStringLiteral("%1 pictures at the top, %2 before the scroll").arg(topPictures).arg(first.second)));
    QCOMPARE(grid->getScheduler()->getMaximumRenderingCount(), 1);
}

void AppToolPagesTest::selectionOfThumbnailsAtOpen_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::newRow("rotate") << PagesGuiHelper::ROTATE_ID;
    QTest::newRow("remove") << PagesGuiHelper::REMOVE_ID;
    QTest::newRow("extract-text") << QStringLiteral("convert.extract-text");
}

void AppToolPagesTest::selectionOfThumbnailsAtOpen()
{
    // tools.md "Operace v panelu Thumbnails (E2)", 9.6: a tool started from the catalog takes
    // the pages selected in the panel Thumbnails as its preselected range (owner 2026-11-07:
    // three pages selected in Thumbnails, the mode Selected of the tool has them, not the
    // current page)
    QFETCH(QString, toolId);
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 20));
    QVERIFY(tab);
    pdf::PDFAppDocumentSidebar* sidebar = tab->getView()->getSidebar();
    QVERIFY(sidebar && sidebar->showPanel(QStringLiteral("thumbnails")));
    pdf::PDFAppThumbnailsPanel* thumbnails = qobject_cast<pdf::PDFAppThumbnailsPanel*>(sidebar->findPanel(QStringLiteral("thumbnails")));
    QVERIFY(thumbnails);
    AppToolShellFixture::settle();
    thumbnails->selectPages({ 2, 4, 6 });
    AppToolShellFixture::settle();
    QCOMPARE(tab->getView()->getSelectedPages(), std::vector<pdf::PDFInteger>({ 2, 4, 6 }));

    PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    pdf::PDFPageSelection* selection = shell->getLeftPanel() ? shell->getLeftPanel()->findChild<pdf::PDFPageSelection*>() : nullptr;
    QVERIFY(selection);
    QCOMPARE(selection->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(selection->getStatusText(), QStringLiteral("3 pages selected · 3, 5, 7"));
    pdf::PDFDocumentToolPageSelection expected;
    expected.pages = QStringLiteral("3, 5, 7");
    QString error;
    const std::optional<pdf::PDFDocumentToolPageSelection> value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QVERIFY2(value, qPrintable(error));
    QCOMPARE(value->getPageIndices(tab->getDocument()->getDocument().data(), &error), std::vector<pdf::PDFInteger>({ 2, 4, 6 }));
}

void AppToolPagesTest::rangeWithoutTextSelectsNoPage_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::newRow("rotate") << PagesGuiHelper::ROTATE_ID;
    QTest::newRow("remove") << PagesGuiHelper::REMOVE_ID;
    QTest::newRow("extract-text") << QStringLiteral("convert.extract-text");
}

void AppToolPagesTest::rangeWithoutTextSelectsNoPage()
{
    // Owner 2026-11-07: Range with an empty field selects no page (All is the mode of all
    // pages): the count says no page, no tile is marked, the tool has nothing to work on
    QFETCH(QString, toolId);
    using H = PagesGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 10));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    pdf::PDFPageSelection* selection = shell->getLeftPanel()->findChild<pdf::PDFPageSelection*>();
    QVERIFY(selection);

    // A range of two pages first: the tool has them
    selection->setMode(pdf::PDFPageSelection::Mode::Range);
    selection->setRange(QStringLiteral("2-3"));
    AppToolShellFixture::settle();
    QCOMPARE(selection->getStatusText(), QStringLiteral("2 pages selected · 2–3"));

    // The field emptied: no page
    selection->setRange(QString());
    AppToolShellFixture::settle();
    QCOMPARE(selection->getMode(), pdf::PDFPageSelection::Mode::Range);
    QCOMPARE(selection->getStatusText(), QStringLiteral("No pages selected"));
    QString error;
    const std::optional<pdf::PDFDocumentToolPageSelection> value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QVERIFY2(value, qPrintable(error));
    QVERIFY(value->isNone);
    QVERIFY(value->getPageIndices(tab->getDocument()->getDocument().data(), &error).empty());
    QCOMPARE(error, QStringLiteral("No page is selected."));

    if (pdf::PDFAppPageMarkingToolGUI* gui = qobject_cast<pdf::PDFAppPageMarkingToolGUI*>(shell->getGUI()))
    {
        // No tile is marked, nothing to turn or to remove
        QVERIFY(gui->getTargetPages().empty());
        for (int page = 0; page < 10; ++page)
        {
            QCOMPARE(gui->getGrid()->getModel()->getTile(page).state, pdf::PDFComponentPainter::PageTileState::Normal);
        }
        if (PDFAppToolGUIRotatePages* rotate = qobject_cast<PDFAppToolGUIRotatePages*>(gui))
        {
            QVERIFY(!rotate->getRotateRightButton()->isEnabled());
            QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("0 of 10 pages will be turned"));
        }
        else
        {
            QCOMPARE(gui->getGrid()->getHeaderLabel()->text(), QStringLiteral("10 of 10 pages stay"));
        }
    }

    // The Primary button is disabled: the tool has no page
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // Undo gives the range of two pages back
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());
    shell->getToolbar()->getUndoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(selection->getStatusText(), QStringLiteral("2 pages selected · 2–3"));
}

void AppToolPagesTest::gridTileSize_data()
{
    // The menu Thumbnails (pages-rotate-menus 1359:33500, owner 2026-11-07): Small is the slot
    // of the frames (104 x 104, 82 x 112), Medium (the default) 1.5 and Large 2 times of it
    // (help tool-pages-rotate.md); a tile is its slot with the padding 6 around it, the label
    // and the space below (+ 12, + 28)
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QSize>("smallTile");
    QTest::addColumn<QSize>("mediumTile");
    QTest::addColumn<QSize>("largeTile");
    QTest::newRow("rotate") << PagesGuiHelper::ROTATE_ID << QSize(116, 132) << QSize(168, 184) << QSize(220, 236);
    QTest::newRow("remove") << PagesGuiHelper::REMOVE_ID << QSize(94, 140) << QSize(135, 196) << QSize(176, 252);
}

void AppToolPagesTest::gridTileSize()
{
    // Owner 2026-11-07: the size of the thumbnails of a page grid, chosen in the toolbar and
    // remembered as a state setting of the database for every page grid
    QFETCH(QString, toolId);
    QFETCH(QSize, smallTile);
    QFETCH(QSize, mediumTile);
    QFETCH(QSize, largeTile);
    using H = PagesGuiHelper;
    using TileSize = pdf::PDFAppToolPageGrid::TileSize;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("doc.pdf")), 30));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    pdf::PDFAppPageMarkingToolGUI* gui = qobject_cast<pdf::PDFAppPageMarkingToolGUI*>(shell ? shell->getGUI() : nullptr);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // tb-thumbnail-size 1359:33481: "Thumbnails: Medium", the rows S / M / L, Medium checked
    QToolButton* button = gui->getTileSizeButton();
    QVERIFY(button && button->isVisible() && button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("Thumbnails: Medium"));
    QCOMPARE(button->toolTip(), QStringLiteral("Size of the page thumbnails"));
    QCOMPARE(gui->getTileSizeRow(TileSize::Small)->getTileText(), QStringLiteral("S"));
    QCOMPARE(gui->getTileSizeRow(TileSize::Large)->getTitle(), QStringLiteral("Large"));
    QCOMPARE(gui->getTileSizeRow(TileSize::Medium)->getDescription(), QStringLiteral("The default size"));
    QVERIFY(gui->getTileSizeRow(TileSize::Medium)->isChecked());
    QVERIFY(!gui->getTileSizeRow(TileSize::Large)->isChecked());
    // The tile 2 is no target (the current page 1 is the selection at the entry)
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QCOMPARE(H::tileInWindow(grid, 1).size(), mediumTile);

    // Large: the panel closes, the tiles are larger, their pictures are drawn for the new size
    button->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getTileSizePanel()->isVisible());
    gui->getTileSizeRow(TileSize::Large)->click();
    AppToolShellFixture::settle();
    QVERIFY(!gui->getTileSizePanel()->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Thumbnails: Large"));
    QVERIFY(gui->getTileSizeRow(TileSize::Large)->isChecked());
    QVERIFY(!gui->getTileSizeRow(TileSize::Medium)->isChecked());
    QCOMPARE(H::tileInWindow(grid, 1).size(), largeTile);
    QCOMPARE(H::tileInWindow(grid, 2).left(), H::tileInWindow(grid, 1).right() + 1 + pdf::PDFAppToolPageGrid::TILE_SPACING);
    QCOMPARE(grid->getModel()->getPictureSize(), largeTile - QSize(12, 28));
    QVERIFY(H::waitForVisiblePictures(grid));
    const QPixmap picture = grid->getModel()->index(1).data(pdf::PDFComponentRoles::PixmapRole).value<QPixmap>();
    QCOMPARE(qMax(picture.width(), picture.height()), qRound(qMax(largeTile.width() - 12, largeTile.height() - 28) * picture.devicePixelRatio()));
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::toolsPageGridSize()), QStringLiteral("large"));

    // A state setting: no row of the Settings page
    const pdf::PDFSettingDefinition* definition = fixture.context->getSettingRegistry()->findDefinition(pdf::PDFSettingKeys::toolsPageGridSize());
    QVERIFY(definition && !definition->isVisible());

    // The choice is remembered: the other page grid has it, the tool opened again too
    shell->cancel();
    AppToolShellFixture::settle();
    const QString otherToolId = toolId == H::ROTATE_ID ? H::REMOVE_ID : H::ROTATE_ID;
    shell = fixture.openTool(tab, otherToolId);
    gui = qobject_cast<pdf::PDFAppPageMarkingToolGUI*>(shell ? shell->getGUI() : nullptr);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTileSize(), TileSize::Large);
    QCOMPARE(gui->getTileSizeButton()->text(), QStringLiteral("Thumbnails: Large"));
    shell->cancel();
    AppToolShellFixture::settle();
    shell = fixture.openTool(tab, toolId);
    gui = qobject_cast<pdf::PDFAppPageMarkingToolGUI*>(shell ? shell->getGUI() : nullptr);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(H::tileInWindow(gui->getGrid(), 1).size(), largeTile);

    // Small
    gui->getTileSizeButton()->click();
    AppToolShellFixture::settle();
    gui->getTileSizeRow(TileSize::Small)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTileSizeButton()->text(), QStringLiteral("Thumbnails: Small"));
    QCOMPARE(H::tileInWindow(gui->getGrid(), 1).size(), smallTile);
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::toolsPageGridSize()), QStringLiteral("small"));
}

void AppToolPagesTest::gridPicturesOfHostilePages()
{
    // Hostile pages in the page grid (phase2_common.md, checklist 2): a media box without a
    // width, a huge one, a damaged content stream, contents of a wrong type. Each visible tile
    // gets its picture (a page, which cannot be drawn, is white), the drawable content is
    // drawn: the red fill of the page (ISO 32000-2, 8.6.8 rg, 8.5.3.1 re, 8.5.3.3 f), also
    // before the damaged part of a content stream
    using H = PagesGuiHelper;
    pdf::PDFDocumentBuilder builder;
    const auto addPage = [&builder](const QRectF& mediaBox, const pdf::PDFObject& contents)
    {
        const pdf::PDFObjectReference page = builder.appendPage(mediaBox);
        pdf::PDFDictionaryBuilder update;
        update.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), contents);
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
    };
    const auto stream = [&builder](const QByteArray& data)
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return pdf::PDFObject::createReference(builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(data)))));
    };
    addPage(QRectF(0, 0, 300, 400), stream("1 0 0 rg 0 0 300 400 re f"));
    addPage(QRectF(0, 0, 0, 400), stream("1 0 0 rg 0 0 300 400 re f"));
    addPage(QRectF(0, 0, 10000000, 10000000), stream("1 0 0 rg 0 0 10000000 10000000 re f"));
    addPage(QRectF(0, 0, 300, 400), stream("1 0 0 rg 0 0 300 400 re f Q Q ( unterminated BT ] } 1 2 cm /Missing Do /F9 12 Tf (x) Tj"));
    addPage(QRectF(0, 0, 300, 400), pdf::PDFObject::createInteger(42));
    pdf::PDFDocument document = builder.build();

    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = fixture.path(QStringLiteral("hostile.pdf"));
    QVERIFY(AppDocumentSamples::write(document, filePath));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(H::waitForVisiblePictures(grid));
    QCOMPARE(H::countVisiblePictures(grid).first, 5);

    QVERIFY2(H::isNear(H::getPictureCenter(grid, 0), QColor(255, 0, 0)), qPrintable(H::getPictureCenter(grid, 0).name()));
    QVERIFY2(H::isNear(H::getPictureCenter(grid, 1), QColor(255, 255, 255)), qPrintable(H::getPictureCenter(grid, 1).name()));
    QVERIFY2(H::isNear(H::getPictureCenter(grid, 2), QColor(255, 0, 0)), qPrintable(H::getPictureCenter(grid, 2).name()));
    QVERIFY2(H::isNear(H::getPictureCenter(grid, 3), QColor(255, 0, 0)), qPrintable(H::getPictureCenter(grid, 3).name()));
    QVERIFY2(H::isNear(H::getPictureCenter(grid, 4), QColor(255, 255, 255)), qPrintable(H::getPictureCenter(grid, 4).name()));

    // A picture is rendered once: the painted rows do not ask for it again
    const int drawn = grid->getScheduler()->getDrawnCount();
    grid->getView()->viewport()->repaint();
    AppToolShellFixture::settle();
    QCOMPARE(grid->getScheduler()->getPendingCount(), 0);
    QCOMPARE(grid->getScheduler()->getDrawnCount(), drawn);
}

void AppToolPagesTest::gridWorkersLeaveGlobalPoolWhenIdle()
{
    // The workers of the pictures are tasks of the global thread pool of Qt, which also runs
    // the reading and the saving of the documents and the runs of the tools (QtConcurrent::run,
    // PDFDocumentToolRunner). A worker waiting for the next page (WORKER_IDLE_TIMEOUT_MS) gives
    // its thread to the other tasks (QThreadPool::releaseThread): with a pool as large as the
    // workers, a task started after the last picture runs at once, not after the idle time
    using H = PagesGuiHelper;
    QThreadPool* pool = QThreadPool::globalInstance();
    const int poolThreadCount = pool->maxThreadCount();
    const int pageThreadCount = pdf::PDFExecutionPolicy::getMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page);
    pdf::PDFExecutionPolicy::setMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page, 2);
    pool->setMaxThreadCount(2);
    auto restore = qScopeGuard([pool, poolThreadCount, pageThreadCount]()
    {
        pool->setMaxThreadCount(poolThreadCount);
        pdf::PDFExecutionPolicy::setMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page, pageThreadCount);
    });
    QCOMPARE(pdf::PDFAppThumbnailScheduler::getRenderThreadCount(), 2);

    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeHeavyPages(fixture.path(QStringLiteral("heavy.pdf")), 24, 600));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ROTATE_ID);
    PDFAppToolGUIRotatePages* gui = H::gui<PDFAppToolGUIRotatePages>(shell);
    QVERIFY(gui);
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(H::waitForVisiblePictures(grid));
    QCOMPARE(grid->getScheduler()->getMaximumRenderingCount(), 2);

    // Both workers wait for the next page now: the task gets a thread at once
    std::shared_ptr<std::atomic_bool> isRun = std::make_shared<std::atomic_bool>(false);
    QElapsedTimer timer;
    timer.start();
    pool->start([isRun]() { isRun->store(true); });
    QVERIFY(AppToolShellFixture::waitFor([isRun]() { return isRun->load(); }, 10000));
    const qint64 limit = qint64(pdf::PDFAppThumbnailScheduler::WORKER_IDLE_TIMEOUT_MS / 2) * AppToolShellFixture::getTimeoutFactor();
    QVERIFY2(timer.elapsed() < limit, qPrintable(QStringLiteral("the task has waited %1 ms").arg(timer.elapsed())));
}

QTEST_MAIN(AppToolPagesTest)

#include "tst_apptoolpagestest.moc"
