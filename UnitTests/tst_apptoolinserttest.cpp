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

// Insert Pages and Add Image as Page in the application (package C of step 3a of phase 2, plan
// 8): the frames 388:15487 ... 388:20121, the strip of the position, the drop at the place, the
// password of a source, Replace. The expected values are the numbers and the texts of the frames
// and of the plan (2.4, 2.5, K23, K35, K36, K53, K57).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappimagepagepreview.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolinsertstrip.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfappcontext.h"
#include "pdfappfiledrop.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaccenthue.h"
#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_insert.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QClipboard>
#include <QMimeData>
#include <QPainter>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QTranslator>
#include <QtTest>

using pdf::PDFAppAddImageToolGUI;
using pdf::PDFAppInsertToolGUI;
using pdf::PDFDocumentToolShell;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;

namespace pdftest
{

/// A translator of the texts of Insert Pages and Add Image as Page
class InsertTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("InsertToolGUI") || name.contains("AddImageToolGUI"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

}   // namespace pdftest

/// Helpers of the test
class InsertTestHelper
{
public:
    InsertTestHelper() = delete;

    static inline const QString INSERT_ID = QStringLiteral("pages.insert");
    static inline const QString ADD_IMAGE_ID = QStringLiteral("pages.add-image");

    /// A document, whose page n (from 1) is base + n points wide (the width names the page) and
    /// 842 high
    static QString writeNamedPages(const QString& filePath, int pageCount, int base)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 1; i <= pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, base + i, 842));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// A document of A4 pages
    static QString writeA4(const QString& filePath, int pageCount)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, 595.276, 841.89));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// A PNG of the size at 72 dpi
    static QString png(const QString& folder, const QString& name, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(200, 220, 240));
        image.setDotsPerMeterX(int(std::lround(72 / 0.0254)));
        image.setDotsPerMeterY(int(std::lround(72 / 0.0254)));
        const QString path = folder + QLatin1Char('/') + name;
        return image.save(path, "PNG") ? path : QString();
    }

    /// The tab of the document ready, the current page \p page (from 1)
    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& filePath, int page = 0)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath, page);
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

    static bool waitLoaded(const pdf::PDFAppToolInputList* list)
    {
        const bool isLoaded = AppToolShellFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; });
        AppToolShellFixture::settle();
        return isLoaded;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
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

    /// The items of the strip: "10@0,0" (a page from 1 at its position), "|@0,132" (the line)
    static QStringList stripItems(const pdf::PDFAppInsertPositionStrip* strip)
    {
        QStringList items;
        for (const pdf::PDFAppInsertPositionStrip::Item& item : strip->getItems())
        {
            items << QStringLiteral("%1@%2,%3").arg(item.pageIndex < 0 ? QStringLiteral("|") : QString::number(item.pageIndex + 1)).arg(item.rect.x()).arg(item.rect.y());
        }
        return items;
    }

    /// A point of the leading (true) or the trailing half of the tile of the page (from 0)
    static QPoint tilePoint(const pdf::PDFAppInsertPositionStrip* strip, int page, bool isLeading)
    {
        const QRect rect = strip->getTileRect(page);
        const bool isLeft = strip->layoutDirection() == Qt::RightToLeft ? !isLeading : isLeading;
        return QPoint(isLeft ? rect.left() + rect.width() / 4 : rect.right() - rect.width() / 4, rect.top() + 50);
    }

    static void click(QWidget* widget, const QPoint& point)
    {
        QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, point);
        AppToolShellFixture::settle();
    }

    static QString parameter(PDFDocumentToolShell* shell, const char* name)
    {
        return shell->getModel()->getValue(QString::fromLatin1(name)).toString();
    }

    static void drop(QWidget* target, const QPoint& point, const QStringList& files)
    {
        QMimeData mimeData;
        QList<QUrl> urls;
        for (const QString& file : files)
        {
            urls << QUrl::fromLocalFile(file);
        }
        mimeData.setUrls(urls);
        QDragEnterEvent enter(point, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        QDragMoveEvent move(point, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &move);
        QDropEvent drop(QPointF(point), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop);
        AppToolShellFixture::settle();
    }

    /// The tool Insert Pages over Handbook.pdf (48 pages, the current page 12) with Annex_A.pdf
    /// (24 pages) of the range 1-5 and site_photo.png (388:15487)
    static PDFDocumentToolShell* openInsertFrame(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab** tabOut)
    {
        pdf::PDFAppDocumentTab* tab = openReady(fixture, writeNamedPages(fixture.path(QStringLiteral("Handbook.pdf")), 48, 100), 12);
        if (!tab)
        {
            return nullptr;
        }
        *tabOut = tab;
        PDFDocumentToolShell* shell = fixture.openTool(tab, INSERT_ID);
        PDFAppInsertToolGUI* insert = gui<PDFAppInsertToolGUI>(shell);
        if (!insert)
        {
            return nullptr;
        }
        const QString annex = writeNamedPages(fixture.path(QStringLiteral("Annex_A.pdf")), 24, 300);
        insert->addFiles({ annex, png(fixture.folder(), QStringLiteral("site_photo.png"), QSize(403, 302)) });
        if (!waitLoaded(insert->getSourceList()))
        {
            return nullptr;
        }
        insert->setCurrentSource(insert->getSourceList()->getInput(0)->id);
        insert->setAllPages(false);
        insert->setSourceRange(QStringLiteral("1-5"));
        AppToolShellFixture::settle();
        return shell;
    }
};

using H = InsertTestHelper;

class AppToolInsertTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void registered();
    void insertFrame();
    void stripClicksKeysAndDrop();
    void positionMenuAndReplace();
    void blankPages();
    void rangeFieldErrorsAndClicks();
    void sourcePassword();
    void emptyStateAndOpenDocuments();
    void insertRunToDialog();
    void undoOfSources();
    void addImageFrame();
    void addImageEmptyAndRules();
    void addImageRunToDialog();
    void builderElements();
    void tileDelegateMarks();
    void themesDirectionAndScale();
    void languageChange();
    void deletedWithToolOpen();
    void screenshots();
};

void AppToolInsertTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolInsertTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
}

void AppToolInsertTest::registered()
{
    // The context registers both tools of the core with their GUI and their page of the document tab
    AppToolShellFixture fixture;
    for (const QString& toolId : { H::INSERT_ID, H::ADD_IMAGE_ID })
    {
        QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
        QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(toolId));
        QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, toolId));
    }
}

void AppToolInsertTest::insertFrame()
{
    // 388:15487: Handbook.pdf of 48 pages, the current page 12; Annex_A.pdf pages 1-5 and an image
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);

    // The toolbar 388:15507: Position: After page 12, Insert 6 Pages
    QCOMPARE(gui->getAction(QStringLiteral("add"))->text(), QStringLiteral("Add"));
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: After page 12"));
    QCOMPARE(gui->getAction(QStringLiteral("thumbnailSize"))->text().left(12), QStringLiteral("Thumbnails: "));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 6 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // sidebar-left 388:15514: "Insert position" / "Handbook.pdf · 48 pages" at 12, 104 of the
    // window; the strip 275 wide at 12, 145; the hint 10 below it
    QCOMPARE(gui->getPositionHeader()->getTitleLabel()->text(), QStringLiteral("Insert position"));
    QCOMPARE(gui->getPositionHeader()->getSubtitleLabel()->text(), QStringLiteral("Handbook.pdf · 48 pages"));
    pdf::PDFAppInsertPositionStrip* strip = gui->getStrip();
    QCOMPARE(H::inWindow(strip).topLeft(), QPoint(12, 145));
    QCOMPARE(strip->width(), 275);

    // position 388:15601: pages 10-12 in the first row, the line in front of page 13, page 15 in
    // the third row; the tiles 85 x 128, the line 10 x 100
    QCOMPARE(H::stripItems(strip), QStringList({ QStringLiteral("10@0,0"), QStringLiteral("11@89,0"), QStringLiteral("12@178,0"), QStringLiteral("|@0,132"),
                                                 QStringLiteral("13@14,132"), QStringLiteral("14@103,132"), QStringLiteral("15@0,264") }));
    QCOMPARE(strip->getTileRect(9).size(), QSize(85, 128));
    QCOMPARE(strip->getLineRect().size(), QSize(10, 100));
    QCOMPARE(strip->height(), 392);
    QCOMPARE(strip->viewport()->height(), 392);
    // Every tile of the window is drawn: the paper of page 15 in the third row differs from the
    // background of the panel next to it
    const QImage windowImage = AppToolShellFixture::render(fixture.window, 1.0);
    const QPoint page15 = strip->viewport()->mapTo(fixture.window, strip->getTileRect(14).topLeft());
    QVERIFY(windowImage.pixelColor(page15 + QPoint(42, 50)) != windowImage.pixelColor(page15 + QPoint(125, 50)));
    QCOMPARE(gui->getStripHint()->text(), QStringLiteral("Click between two pages to move the insertion line, or drop files there."));
    QCOMPARE(H::inWindow(gui->getStripHint()).top(), 145 + 392 + 10);

    // The canvas 388:15520: "Pages to insert · 6 pages from 2 files" at 326, 110; the cards
    pdf::PDFAppInsertCanvas* canvas = gui->getCanvas();
    QCOMPARE(canvas->getHeaderLabel()->text(), QStringLiteral("Pages to insert · 6 pages from 2 files"));
    QCOMPARE(H::inWindow(canvas->getHeaderLabel()).topLeft(), QPoint(326, 110));
    const pdf::PDFAppInsertSourceModel* model = canvas->getModel();
    QCOMPARE(model->getGroups().size(), size_t(2));
    QCOMPARE(model->getGroups()[0].title, QStringLiteral("Annex_A.pdf"));
    QCOMPARE(model->getGroups()[0].subtitle, QStringLiteral("5 of 24 pages · range 1-5"));
    QCOMPARE(model->getGroups()[0].tag, QStringLiteral("selected source"));
    QCOMPARE(model->getGroups()[0].rowCount, 24);
    QCOMPARE(model->getGroups()[1].title, QStringLiteral("site_photo.png"));
    QCOMPARE(model->getGroups()[1].subtitle, QStringLiteral("Image · becomes 1 page"));
    QVERIFY(model->getGroups()[1].tag.isEmpty());
    // 388:15756: the inserted pages 1-5 selected, the others dimmed (muted, not marked for a
    // removal); the image page as it is
    QCOMPARE(model->getRow(4)->state, pdf::PDFComponentPainter::PageTileState::Selected);
    QVERIFY(!model->data(model->index(4), pdf::PDFAppToolPageTileDelegate::DimmedRole).toBool());
    QCOMPARE(model->getRow(5)->state, pdf::PDFComponentPainter::PageTileState::Normal);
    QVERIFY(model->data(model->index(5), pdf::PDFAppToolPageTileDelegate::DimmedRole).toBool());
    QCOMPARE(model->getRow(24)->state, pdf::PDFComponentPainter::PageTileState::Normal);
    QVERIFY(!model->data(model->index(24), pdf::PDFAppToolPageTileDelegate::DimmedRole).toBool());
    // 388:15751, 388:16038: the icon of the kind of the source in front of its name
    QCOMPARE(model->getGroups()[0].iconName, QStringLiteral("file"));
    QCOMPARE(model->getGroups()[1].iconName, QStringLiteral("image"));
    QCOMPARE(canvas->getHintLabel()->text(), QStringLiteral("Sources are inserted in this order. Click pages of a PDF to choose them, or type a range on the right. Dimmed pages are not inserted."));
    // 388:16070: the hint 10 below the cards - the view hugs its cards (the tiles Small of the
    // frame: the cards fit the canvas)
    gui->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Small);
    AppToolShellFixture::settle();
    pdf::PDFAppToolMergePageView* view = canvas->getPageView();
    QCOMPARE(view->getTileSize(0), QSize(85, 128));
    QCOMPARE(view->height(), view->getContentHeight());
    QCOMPARE(view->getContentHeight(), view->getGroupRect(1).bottom() + 1 - view->getGroupRect(0).top());
    QCOMPARE(H::inWindow(canvas->getHintLabel()).top(), H::inWindow(view).bottom() + 1 + 10);

    // sidebar-right 388:15521: the source section at 1137, All pages / Page range, "1-5",
    // the examples, "5 of 24 pages selected"; Summary
    QVERIFY(gui->getSourceSection()->isVisible());
    QCOMPARE(H::inWindow(gui->getSourceSection()).left(), 1137);
    QCOMPARE(gui->getSourceTitle()->text(), QStringLiteral("Annex_A.pdf"));
    QCOMPARE(gui->getSourceTag()->text(), QStringLiteral("24 pages"));
    QCOMPARE(gui->getChoiceSwitch()->getButton(QStringLiteral("all"))->text(), QStringLiteral("All pages"));
    QCOMPARE(gui->getChoiceSwitch()->getButton(QStringLiteral("range"))->text(), QStringLiteral("Page range"));
    QCOMPARE(gui->getChoiceSwitch()->getChoice(), QStringLiteral("range"));
    QCOMPARE(gui->getRangeField()->getText(), QStringLiteral("1-5"));
    QCOMPARE(gui->getRangeField()->getExamplesLabel()->text(), QStringLiteral("Examples: 1-5, 7, 10-, odd, even"));
    QCOMPARE(gui->getRangeField()->getStatusLabel()->text(), QStringLiteral("5 of 24 pages selected"));
    QCOMPARE(gui->getRangeField()->getField()->height(), 34);
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("6 pages"), QStringLiteral("After page 12"), QStringLiteral("48 → 54 pages") }));
    QVERIFY(!gui->getAttentionCallout()->isVisible());

    // The status bar 388:15522
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · 6 pages after page 12 · 48 → 54 pages"));

    // The run gets the usable inputs with their pages in the same order
    const QVariantMap parameters = gui->getRunParameters();
    const QStringList inputs = parameters.value(QStringLiteral("inputs")).toString().split(QLatin1Char(','));
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(parameters.value(QStringLiteral("sourcePages")).toString(), QStringLiteral("1-5;"));
    QCOMPARE(parameters.value(QStringLiteral("position")).toString(), QStringLiteral("after"));
    QCOMPARE(parameters.value(QStringLiteral("page")).toLongLong(), 12);
    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    QCOMPARE(environment.additionalDocuments.size(), size_t(1));
    QCOMPARE(environment.images.size(), size_t(1));
    QCOMPARE(environment.additionalDocuments.front().inputId, inputs[0]);
}

void AppToolInsertTest::stripClicksKeysAndDrop()
{
    // The strip (plan 4.3): a click on the leading half of a page puts the line in front of it,
    // on the trailing half after it; the keys move the line; a drop of files at a place adds them
    // there - one undo step
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFAppInsertPositionStrip* strip = gui->getStrip();

    // The trailing half of page 14 (index 13): after page 14
    H::click(strip->viewport(), H::tilePoint(strip, 13, false));
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: After page 14"));
    QCOMPARE(strip->getGap(), 14);
    // The window follows the line: pages 12-17, the line in front of page 15
    QCOMPARE(strip->getWindow(), std::make_pair(11, 16));

    // The leading half of page 14: after page 13
    H::click(strip->viewport(), H::tilePoint(strip, 13, true));
    QCOMPARE(H::parameter(shell, "position"), QStringLiteral("after"));
    QCOMPARE(H::parameter(shell, "page"), QStringLiteral("13"));

    // The keys: Home = At the beginning, Right = after page 1, End = At the end, Left = after 47
    strip->setFocus();
    QTest::keyClick(strip, Qt::Key_Home);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("At the beginning"));
    QCOMPARE(strip->getGap(), 0);
    QCOMPARE(H::stripItems(strip).value(0), QStringLiteral("|@0,0"));
    QTest::keyClick(strip, Qt::Key_Right);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 1"));
    QTest::keyClick(strip, Qt::Key_End);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("At the end"));
    QCOMPARE(strip->getWindow(), std::make_pair(42, 47));
    QTest::keyClick(strip, Qt::Key_Left);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 47"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · 6 pages after page 47 · 48 → 54 pages"));

    // A drop of a PDF on the leading half of page 46: the file is the third source, the place
    // after page 45, one undo step of both
    const pdf::PDFAppToolInputList* list = gui->getSourceList();
    const QString dropped = H::writeNamedPages(fixture.path(QStringLiteral("dropped.pdf")), 2, 500);
    H::drop(strip->viewport(), H::tilePoint(strip, 45, true), { dropped });
    QVERIFY(H::waitLoaded(list));
    QCOMPARE(list->getCount(), 3);
    QCOMPARE(list->getInput(2)->name, QStringLiteral("dropped.pdf"));
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 45"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 8 Pages"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 47"));

    // A drop anywhere else in the tab adds the file at the end of the sources, the place stays
    H::drop(gui->getCanvas(), QPoint(300, 300), { dropped });
    QVERIFY(H::waitLoaded(list));
    QCOMPARE(list->getCount(), 3);
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 47"));
}

void AppToolInsertTest::positionMenuAndReplace()
{
    // panel-position 388:16769 and Replace 388:17167 (K36: available with a selection of Thumbnails)
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);

    struct ExpectedRow
    {
        const char* id;
        QString title;
        QString description;
        bool isChecked;
    };
    const ExpectedRow rows[] = {
        { "before", QStringLiteral("Before page 12"), QStringLiteral("Ahead of the current page"), false },
        { "after", QStringLiteral("After page 12"), QStringLiteral("Following the current page"), true },
        { "start", QStringLiteral("At the beginning"), QStringLiteral("Before page 1"), false },
        { "end", QStringLiteral("At the end"), QStringLiteral("After page 48"), false },
        { "replace", QStringLiteral("Replace selected pages"), QStringLiteral("Available when pages are selected in Thumbnails"), false },
    };
    for (const ExpectedRow& expected : rows)
    {
        pdf::PDFTileRow* row = gui->getPanelRow(QStringLiteral("position-") + QString::fromLatin1(expected.id));
        QVERIFY(row);
        QCOMPARE(row->getTitle(), expected.title);
        QCOMPARE(row->getDescription(), expected.description);
        QCOMPARE(row->isChecked(), expected.isChecked);
    }
    // The panels open below their buttons, 300 and 290 wide
    gui->getAction(QStringLiteral("position"))->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getPanel(QStringLiteral("position"))->isVisible());
    QCOMPARE(gui->getPanel(QStringLiteral("position"))->getPanelRect().width(), 300);
    gui->getPanel(QStringLiteral("position"))->close();
    gui->getAction(QStringLiteral("add"))->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPanel(QStringLiteral("add"))->getPanelRect().width(), 290);
    gui->getPanel(QStringLiteral("add"))->close();
    AppToolShellFixture::settle();

    // 388:16103 "Available when pages are selected in Thumbnails": the current page alone is no
    // selection - Replace is disabled and the place stays
    QVERIFY(tab->getView()->getExplicitlySelectedPages().empty());
    QVERIFY(!gui->getPanelRow(QStringLiteral("position-replace"))->isEnabled());
    gui->choosePosition(QStringLiteral("replace"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: After page 12"));

    // Page 12 chosen in Thumbnails (a click on the current page): a selection of one page
    tab->getView()->setSelectedPages({}, 11);
    AppToolShellFixture::settle();
    QCOMPARE(tab->getView()->getExplicitlySelectedPages(), std::vector<pdf::PDFInteger>({ 11 }));
    QVERIFY(gui->getPanelRow(QStringLiteral("position-replace"))->isEnabled());
    gui->getPanelRow(QStringLiteral("position-replace"))->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: Replace pages 12"));
    QCOMPARE(gui->getStripHint()->text(), QStringLiteral("Page 12 is selected in Thumbnails and will be replaced by the inserted pages."));
    tab->getView()->setSelectedPages({});
    AppToolShellFixture::settle();
    QVERIFY(!gui->getPanelRow(QStringLiteral("position-replace"))->isEnabled());

    // Before page 12 by the row
    gui->getPanelRow(QStringLiteral("position-before"))->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: Before page 12"));
    QCOMPARE(gui->getStrip()->getGap(), 11);

    // Pages 13 and 14 selected in Thumbnails: Replace is available, it replaces them
    tab->getView()->setSelectedPages({ 12, 13 });
    AppToolShellFixture::settle();
    QVERIFY(gui->getPanelRow(QStringLiteral("position-replace"))->isEnabled());
    gui->getPanelRow(QStringLiteral("position-replace"))->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: Replace pages 13–14"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Replace with 6 Pages"));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("6 pages"), QStringLiteral("Replace 13–14"), QStringLiteral("48 → 52 pages") }));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · 6 pages replace pages 13–14 · 48 → 52 pages"));

    // The strip 388:17167: no line, pages 13 and 14 with the caption "replaced" (the tiles 16 higher)
    pdf::PDFAppInsertPositionStrip* strip = gui->getStrip();
    QVERIFY(strip->getLineRect().isEmpty());
    QCOMPARE(strip->getReplacedPages(), std::vector<pdf::PDFInteger>({ 12, 13 }));
    QCOMPARE(H::stripItems(strip), QStringList({ QStringLiteral("10@0,0"), QStringLiteral("11@89,0"), QStringLiteral("12@178,0"),
                                                 QStringLiteral("13@0,148"), QStringLiteral("14@89,148"), QStringLiteral("15@178,148") }));
    QCOMPARE(gui->getStripHint()->text(), QStringLiteral("Pages 13–14 are selected in Thumbnails and will be replaced by the inserted pages."));

    // A click on the strip leaves Replace
    H::click(strip->viewport(), H::tilePoint(strip, 9, false));
    QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 10"));
    QVERIFY(gui->getStrip()->getReplacedPages().empty());
}

void AppToolInsertTest::blankPages()
{
    // 388:16811: Blank pages (Ctrl+Shift+I) - "New page 13", "New page 14", "blank · A4 portrait";
    // Count 2, Size "As page 12", the orientation of the page
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook.pdf")), 48), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::INSERT_ID);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);

    QCOMPARE(gui->getPanelRow(QStringLiteral("add-blank"))->getShortcutText(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I).toString(QKeySequence::NativeText));
    gui->getCanvas()->setFocus();
    QTest::keyClick(gui->getCanvas(), Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    AppToolShellFixture::settle();
    gui->addBlankPages();
    AppToolShellFixture::settle();

    const pdf::PDFAppInsertSourceModel* model = gui->getCanvas()->getModel();
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Pages to insert · 2 blank pages"));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->getRow(0)->label, QStringLiteral("New page 13"));
    QCOMPARE(model->getRow(1)->label, QStringLiteral("New page 14"));
    QCOMPARE(model->getRow(1)->caption, QStringLiteral("blank · A4 portrait"));
    // 388:17076: alone the blank pages lie on the stage without a card, the tiles 173 x 264 in
    // the size Small (the page 161 x 220), 8 apart, from the leading edge of the stage
    gui->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Small);
    AppToolShellFixture::settle();
    pdf::PDFAppToolMergePageView* view = gui->getCanvas()->getPageView();
    QVERIFY(model->getGroups()[0].isPlain);
    QCOMPARE(view->getTileSize(0), QSize(173, 264));
    QCOMPARE(view->visualRect(model->index(0)), QRect(0, 0, 173, 264));
    QCOMPARE(view->visualRect(model->index(1)), QRect(181, 0, 173, 264));
    QCOMPARE(view->getContentHeight(), 264);
    // 388:16811 has no hint below the blank pages
    QVERIFY(gui->getCanvas()->getHintLabel()->isHidden());
    // The page of the tile is the slot of the delegate scaled for the blank pages
    const pdf::PDFAppToolPageTileDelegate* delegate = qobject_cast<const pdf::PDFAppToolPageTileDelegate*>(gui->getCanvas()->getDelegate());
    QVERIFY(delegate);
    QCOMPARE(delegate->getTileContent(model->index(0), false).slotSize, QSize(161, 220));
    // With another source they have their card "Blank pages" as every source (no frame)
    gui->addFiles({ H::png(fixture.folder(), QStringLiteral("photo.png"), QSize(100, 80)) });
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    QVERIFY(!model->getGroups()[0].isPlain);
    QVERIFY(!gui->getCanvas()->getHintLabel()->isHidden());
    QCOMPARE(model->getGroups()[0].title, QStringLiteral("Blank pages"));
    QCOMPARE(view->getTileSize(0), QSize(173, 264));
    QCOMPARE(view->getTileSize(1), QSize(85, 128));
    const int headerHeight = model->getGroups()[0].tag.isEmpty() ? 15 : 21;
    QCOMPARE(view->visualRect(model->index(0)).topLeft(), QPoint(pdf::PDFAppToolMergePageView::CARD_PADDING_SIDE, view->getGroupRect(0).top() + 9 + headerHeight + 8));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QVERIFY(model->getGroups()[0].isPlain);
    gui->setCurrentSource(QStringLiteral("blank"));
    AppToolShellFixture::settle();
    QVERIFY(gui->getBlankSection()->isVisible());
    QVERIFY(!gui->getSourceSection()->isVisible());
    QCOMPARE(gui->getBlankSizeCombo()->currentText(), QStringLiteral("As page 12"));
    QCOMPARE(gui->getBlankNote()->text(), QStringLiteral("Same as page 12: A4, 210 × 297 mm."));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("2 blank pages"), QStringLiteral("After page 12"), QStringLiteral("48 → 50 pages") }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 2 Pages"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · 2 pages after page 12 · 48 → 50 pages"));
    const QVariantMap parameters = gui->getRunParameters();
    QCOMPARE(parameters.value(QStringLiteral("inputs")).toString(), QStringLiteral("blank"));
    QCOMPARE(parameters.value(QStringLiteral("blankPages")).toLongLong(), 2);

    // Letter, landscape: the tiles say it
    const int letter = gui->getBlankSizeCombo()->findData(QStringLiteral("letter"));
    QVERIFY(letter > 0);
    gui->getBlankSizeCombo()->setCurrentIndex(letter);
    Q_EMIT gui->getBlankSizeCombo()->activated(letter);
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->setValue(QStringLiteral("blankOrientation"), QStringLiteral("landscape")).isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(model->getRow(0)->caption, QStringLiteral("blank · Letter landscape"));

    // The removal of the source of the blank pages gives the count 0 and the drop zone
    gui->removeCurrentSource();
    AppToolShellFixture::settle();
    QCOMPARE(H::parameter(shell, "blankPages"), QStringLiteral("0"));
    QVERIFY(gui->getEmptyState());
}

void AppToolInsertTest::rangeFieldErrorsAndClicks()
{
    // K23: the ordered range of a source; the error of the core in the field and the Primary
    // button disabled; a click on a page of the card adds or takes away the page
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFAppToolPageRangeField* field = gui->getRangeField();

    field->getField()->setFocus();
    field->getField()->selectAll();
    QTest::keyClicks(field->getField(), QStringLiteral("60"));
    AppToolShellFixture::settle();
    QCOMPARE(field->getStatusLabel()->text(), QStringLiteral("Page 60 does not exist – the document has 24 pages"));
    QVERIFY(field->getField()->isInvalid());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    // The model keeps the last valid range
    QCOMPARE(H::parameter(shell, "sourcePages"), QStringLiteral("1-5;"));

    field->getField()->selectAll();
    QTest::keyClicks(field->getField(), QStringLiteral("odd"));
    field->flush();
    AppToolShellFixture::settle();
    QCOMPARE(field->getStatusLabel()->text(), QStringLiteral("12 of 24 pages selected"));
    QCOMPARE(H::parameter(shell, "sourcePages"), QStringLiteral("odd;"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 13 Pages"));

    // An empty Page range is no range
    field->getField()->selectAll();
    QTest::keyClick(field->getField(), Qt::Key_Delete);
    AppToolShellFixture::settle();
    QCOMPARE(field->getStatusLabel()->text(), QStringLiteral("Enter the pages, for example 1-5"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // A click on page 7 of the card: Page range "7" from All pages; then page 9 is added
    gui->setAllPages(true);
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getCanvas()->getModel()->getGroups()[0].subtitle, QStringLiteral("all 24 pages"));
    gui->clickSourcePage(6, 0);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getRangeField()->getText(), QStringLiteral("7"));
    gui->clickSourcePage(8, 0);
    gui->clickSourcePage(7, 0);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getRangeField()->getText(), QStringLiteral("7, 9-8"));
    gui->clickSourcePage(8, 0);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getRangeField()->getText(), QStringLiteral("7-8"));
    QCOMPARE(gui->getCanvas()->getModel()->getRow(7)->state, pdf::PDFComponentPainter::PageTileState::Selected);
    QCOMPARE(gui->getCanvas()->getModel()->getRow(8)->state, pdf::PDFComponentPainter::PageTileState::Normal);
    QVERIFY(gui->getCanvas()->getModel()->getRow(8)->isDimmed);
    QVERIFY(!gui->getCanvas()->getModel()->getRow(7)->isDimmed);

    // A click on the card of the image selects the image: no section of a PDF
    gui->clickSourcePage(24, 1);
    AppToolShellFixture::settle();
    QVERIFY(!gui->getSourceSection()->isVisible());
    QCOMPARE(gui->getCanvas()->getModel()->getGroups()[1].tag, QStringLiteral("selected source"));
}

void AppToolInsertTest::sourcePassword()
{
    // 388:17789: a source protected by a password - "Protected by a password", the callout
    // Password required, "1 file needs attention", the Primary button Insert Pages disabled;
    // Enter Password… opens the dialog of Merge
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Handbook.pdf")), 48, 100), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::INSERT_ID);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    const pdf::PDFAppToolInputList* list = gui->getSourceList();
    gui->addFiles({ AppDocumentSamples::userPassword(fixture.folder()), H::png(fixture.folder(), QStringLiteral("site_photo.png"), QSize(100, 80)) });
    QVERIFY(H::waitLoaded(list));
    const QString lockedId = list->getInput(0)->id;
    const QString lockedName = list->getInput(0)->name;
    gui->setCurrentSource(lockedId);
    AppToolShellFixture::settle();

    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Pages to insert · 1 file needs attention"));
    const pdf::PDFAppToolGroupedPageModel::Group& group = gui->getCanvas()->getModel()->getGroups()[0];
    QCOMPARE(group.subtitle, QStringLiteral("Protected by a password"));
    QCOMPARE(group.callout, pdf::PDFAppToolGroupedPageModel::GroupCallout::Locked);
    QCOMPARE(group.calloutTitle, QStringLiteral("Password required"));
    QCOMPARE(group.calloutText, QStringLiteral("%1 is encrypted. Enter its password to read the pages, or remove the file from the list.").arg(lockedName));
    QVERIFY(gui->getAttentionCallout()->isVisible());
    QCOMPARE(gui->getAttentionCallout()->getTitleLabel()->text(), QStringLiteral("1 file needs attention"));
    QCOMPARE(gui->getAttentionCallout()->getTextLabel()->text(), QStringLiteral("Insert Pages is available when every source can be read."));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("1 page"), QStringLiteral("After page 12"), QStringLiteral("–") }));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · %1 is protected by a password").arg(lockedName));
    // The right panel of 388:17789 has the callout and Summary only (no section of the source)
    QVERIFY(!gui->getSourceSection()->isVisible());
    QVERIFY(!gui->getBlankSection()->isVisible());

    // The card of the source (388:18052): the icon file, "Protected by a password", the callout,
    // Enter Password… and Remove File 8 below it, 8 apart, 36 high; the card 142 high (the
    // callout of one line 55, the padding 9 / 11)
    QCOMPARE(group.iconName, QStringLiteral("file"));
    QCOMPARE(group.actions.size(), size_t(2));
    pdf::PDFAppToolMergePageView* view = gui->getCanvas()->getPageView();
    QPushButton* enter = view->getActionButton(lockedId, QStringLiteral("password"));
    QPushButton* remove = view->getActionButton(lockedId, QStringLiteral("remove"));
    QVERIFY(enter && remove);
    QCOMPARE(enter->text(), QStringLiteral("Enter Password…"));
    QCOMPARE(remove->text(), QStringLiteral("Remove File"));
    QVERIFY(enter->isVisible() && remove->isVisible());
    // (the selected source has its Tag "selected source": the header 21 instead of 15, 378:1595)
    const QRect card = view->getGroupRect(0);
    const int tagHeight = group.tag.isEmpty() ? 0 : 21 - 15;
    QCOMPARE(card.height(), 142 + tagHeight);
    QCOMPARE(enter->geometry().topLeft(), card.topLeft() + QPoint(11, 95 + tagHeight));
    QCOMPARE(enter->height(), 36);
    QCOMPARE(remove->geometry().left(), enter->geometry().right() + 1 + 8);
    QCOMPARE(remove->geometry().top(), enter->geometry().top());
    // The image source has no buttons
    QVERIFY(!view->getActionButton(list->getInput(1)->id, QStringLiteral("remove")));

    // Enter Password…: the dialog; the right password reads the pages without an undo step
    QTest::mouseClick(enter, Qt::LeftButton);
    AppToolShellFixture::settle();
    pdf::PDFAppPasswordDialog* dialog = gui->getPasswordDialog();
    QVERIFY(dialog);
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getUserPassword());
    dialog->getUnlockButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([list, lockedId]() { return list->findInput(lockedId)->state == pdf::PDFAppToolInputState::Ready; }));
    AppToolShellFixture::settle();
    QVERIFY(!gui->getAttentionCallout()->isVisible());
    const int pages = int(list->findInput(lockedId)->pageCount) + 1;
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert %1 Pages").arg(pages));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    // The card has its pages now, no buttons
    QVERIFY(!view->getActionButton(lockedId, QStringLiteral("password")));

    // Remove File of the card of a locked source removes it by one undo step
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("third")));
    gui->addFiles({ AppDocumentSamples::userPassword(QDir(fixture.folder()).filePath(QStringLiteral("third"))) });
    QVERIFY(H::waitLoaded(list));
    const QString thirdId = list->getInput(2)->id;
    QPushButton* removeThird = view->getActionButton(thirdId, QStringLiteral("remove"));
    QVERIFY(removeThird);
    QPointer<QPushButton> removeGuard(removeThird);
    QTest::mouseClick(removeThird, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QVERIFY(list->isParked(thirdId));
    QVERIFY(!removeGuard);
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 3);
    QVERIFY(view->getActionButton(thirdId, QStringLiteral("password")));
    shell->getModel()->redo();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 2);

    // Remove File of the dialog of a second locked source removes it by one undo step
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("second")));
    gui->addFiles({ AppDocumentSamples::userPassword(QDir(fixture.folder()).filePath(QStringLiteral("second"))) });
    QVERIFY(H::waitLoaded(list));
    const QString secondId = list->getInput(2)->id;
    gui->openPasswordDialog(secondId);
    QVERIFY(gui->getPasswordDialog());
    gui->getPasswordDialog()->getRemoveButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QVERIFY(list->isParked(secondId));
}

void AppToolInsertTest::emptyStateAndOpenDocuments()
{
    // 398:28602: "Choose what to insert", Add Files…, Blank Pages, the open documents with the Tag
    // "unsaved changes" (K24), Summary "–" / "48 pages"
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* invoice = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Invoice.pdf")), 3, 400));
    QVERIFY(invoice);
    // An unsaved change of Invoice.pdf: its snapshot is inserted
    {
        pdf::PDFDocument changed = *invoice->getDocument()->getDocument();
        const pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(changed)));
        QVERIFY(invoice->getDocument()->commit(pdf::PDFModifiedDocument(pointer, invoice->getDocument()->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset), QStringLiteral("Change"),
                                                invoice->getDocument()->getVersion(), pdf::PDFAppChangeSource::User));
        QVERIFY(invoice->getDocument()->isModified());
    }
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Handbook.pdf")), 48, 100), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::INSERT_ID);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);

    pdf::PDFAppInsertEmptyState* empty = gui->getEmptyState();
    QVERIFY(empty && empty->isVisible());
    QCOMPARE(empty->getTitleLabel()->text(), QStringLiteral("Choose what to insert"));
    QCOMPARE(empty->getTextLabel()->text(), QStringLiteral("PDF documents and images. Drop them here, or onto a place between two thumbnails."));
    QCOMPARE(empty->getPrimaryButton()->text(), QStringLiteral("Add Files…"));
    QCOMPARE(empty->getSecondaryButton()->text(), QStringLiteral("Blank Pages"));
    QCOMPARE(empty->getCard()->width(), 540);
    QVERIFY(!gui->getCanvas()->getHeaderLabel()->isVisible());
    const std::vector<QPushButton*> addButtons = empty->getAddButtons();
    QCOMPARE(addButtons.size(), size_t(1));
    QVERIFY(empty->findChild<pdf::PDFTag*>(QStringLiteral("insertEmptyUnsavedTag")));
    QCOMPARE(empty->findChild<pdf::PDFTag*>(QStringLiteral("insertEmptyUnsavedTag"))->text(), QStringLiteral("unsaved changes"));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("–"), QStringLiteral("After page 12"), QStringLiteral("48 pages") }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Insert Pages · choose what to insert after page 12 of Handbook.pdf"));

    // The menu Add (388:16103): Invoice.pdf "3 pages · unsaved state is used"
    pdf::PDFTileRow* documentRow = nullptr;
    for (pdf::PDFTileRow* row : gui->getPanel(QStringLiteral("add"))->findChildren<pdf::PDFTileRow*>())
    {
        documentRow = row->objectName().startsWith(QStringLiteral("insertOpenDocument-")) ? row : documentRow;
    }
    QVERIFY(documentRow);
    QCOMPARE(documentRow->getTitle(), QStringLiteral("Invoice.pdf"));
    QCOMPARE(documentRow->getDescription(), QStringLiteral("3 pages · unsaved state is used"));

    // Add of the open document: its snapshot is the source
    addButtons.front()->click();
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    QCOMPARE(gui->getSourceList()->getCount(), 1);
    QCOMPARE(gui->getSourceList()->getInput(0)->kind, pdf::PDFAppToolInputKind::OpenTab);
    QVERIFY(gui->getSourceList()->getInput(0)->isModified);
    QVERIFY(!gui->getEmptyState());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 3 Pages"));
    QVERIFY(!documentRow->isEnabled());
}

void AppToolInsertTest::insertRunToDialog()
{
    // 388:18134: Pages inserted - "6 pages after page 12", "54 pages (was 48)"; Keep in this tab
    // gives the document the pages at their place
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Pages inserted"));
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Inserted"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("6 pages after page 12"));
    QCOMPARE(dialog->getFacts()->getKey(1), QStringLiteral("Document"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("54 pages (was 48)"));
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
    // Nothing can be changed below the dialog
    QVERIFY(!gui->getStrip()->isEnabled());
    QVERIFY(!gui->getAction(QStringLiteral("add"))->isEnabled());

    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();

    // The pages: 1-12 of the document, 1-5 of Annex_A (300 + n wide), the image, 13-48
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    QCOMPARE(document->getCatalog()->getPageCount(), size_t(54));
    QCOMPARE(qRound(document->getCatalog()->getPage(11)->getMediaBox().width()), 112);
    QCOMPARE(qRound(document->getCatalog()->getPage(12)->getMediaBox().width()), 301);
    QCOMPARE(qRound(document->getCatalog()->getPage(16)->getMediaBox().width()), 305);
    QCOMPARE(qRound(document->getCatalog()->getPage(18)->getMediaBox().width()), 113);
}

void AppToolInsertTest::undoOfSources()
{
    // The values of the sources are one undo step of each change; Undo parks a source (no second
    // reading), Redo takes it back
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    const pdf::PDFAppToolInputList* list = gui->getSourceList();
    const int readings = list->getStartedReadingCount();

    gui->removeCurrentSource();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 1);
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 1 Page"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(gui->getCanvas()->getModel()->getGroups()[0].subtitle, QStringLiteral("5 of 24 pages · range 1-5"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Insert 6 Pages"));
    shell->getModel()->redo();
    AppToolShellFixture::settle();
    QCOMPARE(list->getCount(), 1);
    QCOMPARE(list->getStartedReadingCount(), readings);
}

void AppToolInsertTest::addImageFrame()
{
    // 388:18837: two images after page 12 - a landscape and a portrait one on "Same as page 12"
    // (A4): Orientation follows each image (5.8: Auto)
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook.pdf")), 48), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ADD_IMAGE_ID);
    PDFAppAddImageToolGUI* gui = H::gui<PDFAppAddImageToolGUI>(shell);
    QVERIFY(gui);
    gui->addFiles({ H::png(fixture.folder(), QStringLiteral("site_photo.png"), QSize(403, 302)), H::png(fixture.folder(), QStringLiteral("signature.png"), QSize(124, 175)) });
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    gui->setCurrentSource(gui->getSourceList()->getInput(0)->id);
    AppToolShellFixture::settle();

    // The toolbar 388:18837: Add Images, Rotate, Remove, Position, Page; Add 2 Pages
    QCOMPARE(gui->getAction(QStringLiteral("add"))->text(), QStringLiteral("Add Images"));
    QCOMPARE(gui->getAction(QStringLiteral("position"))->text(), QStringLiteral("Position: After page 12"));
    QCOMPARE(gui->getAction(QStringLiteral("pageSize"))->text(), QStringLiteral("Page: Same as page 12"));
    QVERIFY(gui->getAction(QStringLiteral("rotateLeft")));
    QVERIFY(!gui->getAction(QStringLiteral("thumbnailSize")));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Add 2 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // K36: no Replace, no descriptions in Position (388:19607)
    QVERIFY(!gui->getPanelRow(QStringLiteral("position-replace")));
    QVERIFY(gui->getPanelRow(QStringLiteral("position-after"))->getDescription().isEmpty());
    // Page 388:19607
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-same-as-page"))->getTitle(), QStringLiteral("Same as page 12"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-same-as-page"))->getDescription(), QStringLiteral("A4 · 210 × 297 mm"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-fit-to-image"))->getDescription(), QStringLiteral("The page is exactly as large as the image"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-a3"))->getDescription(), QStringLiteral("297 × 420 mm"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-letter"))->getDescription(), QStringLiteral("216 × 279 mm"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("page-custom"))->getDescription(), QStringLiteral("Width and height appear in Page layout"));
    QVERIFY(gui->getPanelRow(QStringLiteral("page-same-as-page"))->isChecked());
    QCOMPARE(gui->getPanelRow(QStringLiteral("add-images"))->getTitle(), QStringLiteral("Image files…"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("add-paste"))->getTitle(), QStringLiteral("Paste from clipboard"));

    // sidebar-left: Images "2 images · …", the rows, then the position (K57: the same strip)
    QCOMPARE(gui->getImagesPanel()->getTitleLabel()->text(), QStringLiteral("Images"));
    QVERIFY(gui->getImagesPanel()->getSubtitleLabel()->text().startsWith(QStringLiteral("2 images · ")));
    const std::vector<pdf::PDFAppToolInputRow*> rows = gui->getImagesPanel()->getRows();
    QCOMPARE(rows.size(), size_t(2));
    QVERIFY(rows[0]->getContent().firstLine.startsWith(QStringLiteral("403 × 302 px · ")));
    QCOMPARE(rows[0]->getContent().secondLine, QStringLiteral("New page 13 · A4 landscape"));
    QCOMPARE(rows[1]->getContent().secondLine, QStringLiteral("New page 14 · A4 portrait"));
    QVERIFY(gui->getStrip());
    QVERIFY(H::inWindow(gui->getStrip()).top() > H::inWindow(gui->getImagesPanel()).top());
    QCOMPARE(gui->getStrip()->getGap(), 12);

    // The canvas: "New pages · 2", the pages with their images (PDFAppImagePagePreview)
    pdf::PDFAppAddImageCanvas* canvas = gui->getCanvas();
    QCOMPARE(canvas->getHeaderLabel()->text(), QStringLiteral("New pages · 2"));
    const std::vector<pdf::PDFAppAddImagePageCard*> cards = canvas->getCards();
    QCOMPARE(cards.size(), size_t(2));
    QCOMPARE(cards[0]->getTitleLabel()->text(), QStringLiteral("New page 13"));
    QCOMPARE(cards[0]->getCaptionLabel()->text(), QStringLiteral("site_photo.png · A4 landscape"));
    QVERIFY(cards[0]->isSelected());
    QCOMPARE(cards[1]->getCaptionLabel()->text(), QStringLiteral("signature.png · A4 portrait"));
    // 5.8: Fit 403 × 302 on A4 landscape with 10 mm: the area 813.9 × 566.6 points, the image
    // fitted by its height
    const QSizeF pageSize = cards[0]->getPreview()->getPageSize();
    QVERIFY(qAbs(pageSize.width() - 841.89) < 0.5 && qAbs(pageSize.height() - 595.276) < 0.5);
    QCOMPARE(canvas->getHintLabel()->text(), QStringLiteral("Orientation follows each image. The image is fitted inside the margins and keeps its proportions."));

    // sidebar-right: Page layout and Summary
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("2"), QStringLiteral("After page 12"), QStringLiteral("A4, as page 12"), QStringLiteral("48 → 50 pages") }));
    QVERIFY(!gui->getSizeFields()->isVisible());
    QVERIFY(gui->getAnchorField());
    QCOMPARE(gui->getNote()->text(), QStringLiteral("Looking for something else? Images to PDF creates a new document from images. Edit Content places a picture onto an existing page."));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Add Image as Page · 2 new pages after page 12 · 48 → 50 pages"));

    // Rotate Right turns the selected image: the page is portrait (Auto), the Tag "rotated 90°"
    gui->rotateRight();
    AppToolShellFixture::settle();
    QCOMPARE(H::parameter(shell, "turns"), QStringLiteral("90,"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().secondLine, QStringLiteral("New page 13 · A4 portrait"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().tag, QStringLiteral("rotated 90°"));

    // Custom: Width and Height appear in Page layout
    QVERIFY(shell->getModel()->setValue(QStringLiteral("pageSize"), QStringLiteral("custom")).isEmpty());
    AppToolShellFixture::settle();
    QVERIFY(gui->getSizeFields()->isVisible());
    QCOMPARE(gui->getAction(QStringLiteral("pageSize"))->text(), QStringLiteral("Page: Custom"));
}

void AppToolInsertTest::addImageEmptyAndRules()
{
    // 388:19253: "Add the images", Choose Images…, "No images yet", Add Pages disabled; K41: a PDF
    // is no image (it stays as Unsupported, the Primary button disabled)
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook.pdf")), 48), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ADD_IMAGE_ID);
    PDFAppAddImageToolGUI* gui = H::gui<PDFAppAddImageToolGUI>(shell);
    QVERIFY(gui);

    pdf::PDFAppInsertEmptyState* empty = gui->getEmptyState();
    QVERIFY(empty);
    QCOMPARE(empty->getTitleLabel()->text(), QStringLiteral("Add the images"));
    QCOMPARE(empty->getTextLabel()->text(), QStringLiteral("Each image becomes a new page of Handbook.pdf. Drop image files here or paste a picture with %1.").arg(QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText)));
    QCOMPARE(empty->getPrimaryButton()->text(), QStringLiteral("Choose Images…"));
    QVERIFY(!empty->getSecondaryButton());
    QVERIFY(empty->getAddButtons().empty());
    QCOMPARE(gui->getImagesPanel()->getSubtitleLabel()->text(), QStringLiteral("No images yet"));
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("–"), QStringLiteral("After page 12"), QStringLiteral("A4, as page 12"), QStringLiteral("48 pages") }));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Add Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Add Image as Page · choose the images that become new pages of Handbook.pdf"));

    // Choose Images… opens the dialog of the files
    const QString image = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(60, 40));
    pdf::PDFAppFileDialogs::setOpenFilesHandler([image](QWidget*, const QString&) { return QStringList(image); });
    empty->getPrimaryButton()->click();
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    QCOMPARE(gui->getSourceList()->getCount(), 1);
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Add 1 Page"));

    // A PDF: Unsupported, the reason in its row, the Primary button disabled
    gui->addFiles({ H::writeA4(fixture.path(QStringLiteral("notes.pdf")), 1) });
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    QCOMPARE(gui->getSourceList()->getInput(1)->state, pdf::PDFAppToolInputState::Unsupported);
    QCOMPARE(gui->getImagesPanel()->getRows()[1]->getContent().firstLine, QStringLiteral("Add Image as Page takes images – insert PDF files with Insert Pages"));
    QCOMPARE(gui->getImagesPanel()->getRows()[1]->getContent().linkText, QStringLiteral("Remove file"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // Ctrl+V pastes the image of the clipboard (388:19607)
    QImage pasted(30, 20, QImage::Format_RGB32);
    pasted.fill(Qt::red);
    QApplication::clipboard()->setImage(pasted);
    gui->getCanvas()->setFocus();
    QTest::keyClick(gui->getCanvas(), Qt::Key_V, Qt::ControlModifier);
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    QCOMPARE(gui->getSourceList()->getCount(), 3);
    QCOMPARE(gui->getSourceList()->getInput(2)->kind, pdf::PDFAppToolInputKind::Clipboard);
}

void AppToolInsertTest::addImageRunToDialog()
{
    // 388:20121: Pages added - New pages "2 images after page 12", "50 pages (was 48)"
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook.pdf")), 48), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::ADD_IMAGE_ID);
    PDFAppAddImageToolGUI* gui = H::gui<PDFAppAddImageToolGUI>(shell);
    QVERIFY(gui);
    gui->addFiles({ H::png(fixture.folder(), QStringLiteral("site_photo.png"), QSize(403, 302)), H::png(fixture.folder(), QStringLiteral("signature.png"), QSize(124, 175)) });
    QVERIFY(H::waitLoaded(gui->getSourceList()));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Pages added"));
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("New pages"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("2 images after page 12"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("50 pages (was 48)"));

    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    QCOMPARE(document->getCatalog()->getPageCount(), size_t(50));
    // Page 13: A4 landscape, page 14: A4 portrait
    const QRectF landscape = document->getCatalog()->getPage(12)->getMediaBox();
    const QRectF portrait = document->getCatalog()->getPage(13)->getMediaBox();
    QVERIFY(qAbs(landscape.width() - 841.89) < 0.5 && qAbs(landscape.height() - 595.276) < 0.5);
    QVERIFY(qAbs(portrait.width() - 595.276) < 0.5 && qAbs(portrait.height() - 841.89) < 0.5);
}

void AppToolInsertTest::builderElements()
{
    // R1 d: the elements of package C bound to parameters - the switch of an Enum parameter and the
    // field of a Text parameter (one undo step, Undo shown; an error disables the Primary button)
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook.pdf")), 48), 12);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::INSERT_ID);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    gui->addBlankPages();
    AppToolShellFixture::settle();

    QWidget host;
    pdf::PDFAppToolPanelBuilder builder(gui, &host, Qt::Vertical, 8);
    pdf::PDFAppToolChoiceSwitch* choice = builder.addChoiceSwitch(QStringLiteral("blankOrientation"), QStringLiteral("Orientation"));
    QVERIFY(choice);
    QCOMPARE(choice->getOptionIds(), QStringList({ QStringLiteral("portrait"), QStringLiteral("landscape") }));
    QCOMPARE(choice->getChoice(), QStringLiteral("portrait"));
    choice->getButton(QStringLiteral("landscape"))->click();
    AppToolShellFixture::settle();
    QCOMPARE(H::parameter(shell, "blankOrientation"), QStringLiteral("landscape"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(choice->getChoice(), QStringLiteral("portrait"));

    pdf::PDFAppToolPageRangeField* field = builder.addPageRangeField(QStringLiteral("sourcePages"), QStringLiteral("Pages"), QStringLiteral("Examples: 2-4"));
    QVERIFY(field);
    QCOMPARE(field->getExamplesLabel()->text(), QStringLiteral("Examples: 2-4"));
    QCOMPARE(field->getPageCount(), 48);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    field->getField()->setFocus();
    QTest::keyClicks(field->getField(), QStringLiteral("49"));
    AppToolShellFixture::settle();
    QCOMPARE(field->getError(), QStringLiteral("Page 49 does not exist – the document has 48 pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    field->getField()->clear();
    QTest::keyClicks(field->getField(), QStringLiteral("2-4"));
    field->flush();
    AppToolShellFixture::settle();
    QCOMPARE(H::parameter(shell, "sourcePages"), QStringLiteral("2-4"));
    QCOMPARE(field->getStatusLabel()->text(), QStringLiteral("3 of 48 pages selected"));
}

void AppToolInsertTest::tileDelegateMarks()
{
    // The marks of the tile delegate from the roles of any model (R1 b, K45): a muted page is
    // drawn at 35 % over the color below the tiles (388:15756: the lines #9CA3AF of the dimmed
    // pages become #DCDFE3 on the white card), SlotScaleRole makes the slot larger (388:16811),
    // a caption adds its line (16, as 389:20177), HueRole gives the dot
    QStandardItemModel model;
    QStandardItem* item = new QStandardItem(QStringLiteral("6"));
    QPixmap black(73, 100);
    black.fill(Qt::black);
    item->setData(black, pdf::PDFComponentRoles::PixmapRole);
    item->setData(QSizeF(73, 100), pdf::PDFComponentRoles::PageSizeRole);
    model.appendRow(item);

    pdf::PDFAppToolPageTileDelegate delegate(nullptr);
    delegate.setSlotSize(QSize(73, 100));
    delegate.setBackgroundColor(pdf::PDFThemeColors::BgSurface);
    QStyleOptionViewItem option;
    option.direction = Qt::LeftToRight;
    option.state = QStyle::State_Enabled;
    option.rect = QRect(QPoint(0, 0), delegate.sizeHint(option, model.index(0, 0)));
    QCOMPARE(option.rect.size(), QSize(85, 128));

    const QColor surface = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface);
    const auto pageCenter = [&]()
    {
        QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(surface);
        QPainter painter(&image);
        delegate.paint(&painter, option, model.index(0, 0));
        painter.end();
        const QRectF page = delegate.getMarkRects(option, model.index(0, 0)).page;
        return image.pixelColor(page.center().toPoint());
    };
    QCOMPARE(pageCenter(), QColor(Qt::black));
    item->setData(true, pdf::PDFAppToolPageTileDelegate::DimmedRole);
    const QColor dimmed = pageCenter();
    QVERIFY2(qAbs(dimmed.red() - qRound(surface.red() * 0.65)) <= 2, qPrintable(dimmed.name()));
    QVERIFY2(qAbs(dimmed.blue() - qRound(surface.blue() * 0.65)) <= 2, qPrintable(dimmed.name()));

    // A blank page shows the border of its paper (388:17078 border/strong), only with the role
    QStandardItem* blank = new QStandardItem(QStringLiteral("New page 13"));
    blank->setData(QSizeF(73, 100), pdf::PDFComponentRoles::PageSizeRole);
    model.appendRow(blank);
    const auto pageEdge = [&]()
    {
        QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(surface);
        QPainter painter(&image);
        delegate.paint(&painter, option, model.index(1, 0));
        painter.end();
        const QRectF page = delegate.getMarkRects(option, model.index(1, 0)).page;
        return image.pixelColor(QPoint(int(page.left()), int(page.center().y())));
    };
    const QColor border = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderStrong);
    QVERIFY(pageEdge() != border);
    blank->setData(true, pdf::PDFAppToolPageTileDelegate::PaperBorderRole);
    QCOMPARE(pageEdge(), border);

    // The larger slot and the caption
    item->setData(2.2, pdf::PDFAppToolPageTileDelegate::SlotScaleRole);
    QCOMPARE(delegate.sizeHint(option, model.index(0, 0)), QSize(173, 248));
    item->setData(QStringLiteral("blank · A4 portrait"), pdf::PDFComponentRoles::SubtitleRole);
    QCOMPARE(delegate.sizeHint(option, model.index(0, 0)), QSize(173, 264));

    // The dot of HueRole in front of the label, none without the role
    option.rect = QRect(QPoint(0, 0), delegate.sizeHint(option, model.index(0, 0)));
    QVERIFY(delegate.getMarkRects(option, model.index(0, 0)).dot.isEmpty());
    item->setData(int(pdf::PDFAccentHue::Blue), pdf::PDFComponentRoles::HueRole);
    const QRectF dot = delegate.getMarkRects(option, model.index(0, 0)).dot;
    QCOMPARE(dot.size(), QSizeF(pdf::PDFAppToolPageTileDelegate::DOT_SIZE, pdf::PDFAppToolPageTileDelegate::DOT_SIZE));
    QVERIFY(dot.top() > delegate.getMarkRects(option, model.index(0, 0)).page.bottom());
}

void AppToolInsertTest::themesDirectionAndScale()
{
    // No color of the system in the parts, both themes, right-to-left (the strip flows from the
    // right; the page geometry - the anchor of the image - is never mirrored), 1x and 2x
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            AppToolShellFixture fixture(QSize(1440, 960));
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            pdf::PDFAppDocumentTab* tab = nullptr;
            PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
            PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
            QVERIFY(gui);
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(fixture.window, ratio)), 0);
            }

            // 388:15601 mirrored: page 10 at the right edge, the line at the right edge of its row
            pdf::PDFAppInsertPositionStrip* strip = gui->getStrip();
            const int width = strip->viewport()->width();
            if (direction == Qt::RightToLeft)
            {
                QCOMPARE(strip->getTileRect(9).right(), width - 1);
                QCOMPARE(strip->getLineRect().right(), width - 1);
                QVERIFY(strip->getTileRect(10).left() < strip->getTileRect(9).left());
                // Right moves the line backward in the flow from the right: after page 11
                strip->setFocus();
                QTest::keyClick(strip, Qt::Key_Right);
                AppToolShellFixture::settle();
                QCOMPARE(gui->getPositionValueText(), QStringLiteral("After page 11"));
            }
            else
            {
                QCOMPARE(strip->getTileRect(9).left(), 0);
            }
            shell->cancel();
            AppToolShellFixture::settle();

            // Add Image: the anchor Top left keeps the image at the left edge of the page
            shell = fixture.openTool(tab, H::ADD_IMAGE_ID);
            PDFAppAddImageToolGUI* addImage = H::gui<PDFAppAddImageToolGUI>(shell);
            QVERIFY(addImage);
            addImage->addFiles({ H::png(fixture.folder(), QStringLiteral("small.png"), QSize(100, 80)) });
            QVERIFY(H::waitLoaded(addImage->getSourceList()));
            QVERIFY(shell->getModel()->setValues({ { QStringLiteral("imageScaling"), QStringLiteral("original-size") }, { QStringLiteral("anchor"), QStringLiteral("top-left") } }).isEmpty());
            AppToolShellFixture::settle();
            pdf::PDFAppImagePagePreview* preview = addImage->getCanvas()->getCards().front()->getPreview();
            const QRectF page = preview->getPageRect();
            const QRectF image = preview->getImageRect();
            const double margin = 10.0 * 72.0 / 25.4 * preview->getScale();
            QVERIFY(qAbs(image.left() - page.left() - margin) < 1.0);
            QVERIFY(qAbs(image.top() - page.top() - margin) < 1.0);
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(fixture.window, ratio)), 0);
            }
            shell->cancel();
            AppToolShellFixture::settle();
        }
    }
}

void AppToolInsertTest::languageChange()
{
    // The parts are built again with the translated texts; the sources and the place stay
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    const pdf::PDFAppToolInputList* list = gui->getSourceList();

    pdftest::InsertTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QVERIFY(gui->getStripHint()->text().startsWith(QStringLiteral("[T] ")));
    QVERIFY(gui->getCanvas()->getHeaderLabel()->text().startsWith(QStringLiteral("[T] ")));
    QCOMPARE(gui->getSourceList(), list);
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(gui->getRangeField()->getText(), QStringLiteral("1-5"));
    QCOMPARE(gui->getStrip()->getGap(), 12);
    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Pages to insert · 6 pages from 2 files"));
}

void AppToolInsertTest::deletedWithToolOpen()
{
    // The tab deleted while a source is read, while the password dialog is open, with a click of
    // the strip queued
    for (int variant = 0; variant < 3; ++variant)
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("doc.pdf")), 12), 4);
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::INSERT_ID);
        PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
        QVERIFY(gui);
        QPointer<PDFAppInsertToolGUI> guiGuard(gui);
        QPointer<pdf::PDFAppToolInputList> listGuard(gui->getSourceList());
        QPointer<pdf::PDFAppPasswordDialog> dialogGuard;
        switch (variant)
        {
            case 0:
                gui->addFiles({ H::writeA4(fixture.path(QStringLiteral("big.pdf")), 300) });
                break;
            case 1:
                gui->addFiles({ AppDocumentSamples::userPassword(fixture.folder()) });
                QVERIFY(H::waitLoaded(gui->getSourceList()));
                gui->openPasswordDialog(gui->getSourceList()->getInput(0)->id);
                dialogGuard = gui->getPasswordDialog();
                QVERIFY(dialogGuard);
                break;
            default:
                QTest::mouseClick(gui->getStrip()->viewport(), Qt::LeftButton, Qt::NoModifier, H::tilePoint(gui->getStrip(), 4, true));
                break;
        }
        delete tab;
        AppToolShellFixture::settle();
        QVERIFY(!guiGuard);
        QVERIFY(!listGuard);
        QVERIFY(!dialogGuard);
    }
}

void AppToolInsertTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS: the frames under their ids (compared with Figma by the review)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    AppToolShellFixture fixture(QSize(1440, 960));
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::toolsPageGridSize(), QStringLiteral("small")).isError());
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openInsertFrame(fixture, &tab);
    PDFAppInsertToolGUI* gui = H::gui<PDFAppInsertToolGUI>(shell);
    QVERIFY(gui);
    fixture.screenshot(QStringLiteral("388-15487-pages-insert"));
    gui->getAction(QStringLiteral("position"))->click();
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-16103-pages-insert-menus"));
    gui->getPanel(QStringLiteral("position"))->close();
    tab->getView()->setSelectedPages({ 12, 13 });
    gui->choosePosition(QStringLiteral("replace"));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-17167-pages-insert-replace"));
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-18134-pages-insert-done"));
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
    shell->cancel();
    AppToolShellFixture::settle();

    shell = fixture.openTool(tab, H::INSERT_ID);
    gui = H::gui<PDFAppInsertToolGUI>(shell);
    fixture.screenshot(QStringLiteral("398-28602-pages-insert-empty"));
    gui->addBlankPages();
    gui->addBlankPages();
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-16811-pages-insert-blank"));
    gui->addFiles({ AppDocumentSamples::userPassword(fixture.folder()) });
    QVERIFY(H::waitLoaded(gui->getSourceList()));
    gui->setCurrentSource(gui->getSourceList()->getInput(0)->id);
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-17789-pages-insert-problem"));
    shell->cancel();
    AppToolShellFixture::settle();

    // Add Image: an A4 document (388:18837 "Same as page 12" is A4)
    pdf::PDFAppDocumentTab* a4Tab = H::openReady(fixture, H::writeA4(fixture.path(QStringLiteral("Handbook_A4.pdf")), 48), 12);
    QVERIFY(a4Tab);
    shell = fixture.openTool(a4Tab, H::ADD_IMAGE_ID);
    PDFAppAddImageToolGUI* addImage = H::gui<PDFAppAddImageToolGUI>(shell);
    QVERIFY(addImage);
    fixture.screenshot(QStringLiteral("388-19253-pages-add-image-empty"));
    addImage->addFiles({ H::png(fixture.folder(), QStringLiteral("site_photo.png"), QSize(403, 302)), H::png(fixture.folder(), QStringLiteral("signature.png"), QSize(124, 175)) });
    QVERIFY(H::waitLoaded(addImage->getSourceList()));
    fixture.screenshot(QStringLiteral("388-18837-pages-add-image"));
    addImage->getAction(QStringLiteral("pageSize"))->click();
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-19607-pages-add-image-menus"));
    addImage->getPanel(QStringLiteral("pageSize"))->close();
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("388-20121-pages-add-image-done"));
}

QTEST_MAIN(AppToolInsertTest)

#include "tst_apptoolinserttest.moc"
