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

// The GUI of Merge (pages.merge, plan of step 2 of phase 2, 4.4 - 4.6, chapter 8 row
// UnitTestsAppToolMerge): the plan of the pages and its text, the frames 378:1049 (three
// files), 378:1657 (no file), 378:2613 / 1390:34070 (the states of the files), 408:32003
// (Details), 378:1879 (the menus), 1370:34102 (the size of the tiles), 378:3038 / 378:4376
// (the run, the partial result), the keys of the frames, a drop of files, Undo / Redo of the
// files and the pages, both themes, right to left, 1x / 2x, a large input, the deletion of
// the tab in each state, the action of an assistant over HTTP and its schema (Q11).

#include "apptoolowntabfixture.h"
#include "appmcpfixture.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapppassworddialog.h"
#include "pdfappworkspace.h"
#include "pdfapptoolgui_merge.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltab.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdftoolstartpage.h"

#include "pdfcatalog.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfmcpcall.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

using pdftest::AppToolOwnTabFixture;
using pdftest::AppDocumentSamples;
using pdf::PDFAppMergeToolGUI;
using pdf::PDFAppToolMergePlan;

class AppToolMergeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void planText();
    void planOperations();
    void registeredAndOutOfSchema();
    void framesOfThreeFiles();
    void emptyStateAndOpenDocuments();
    void oneFileIsNotEnough();
    void undoRedoOfFilesAndPages();
    void pageActions();
    void keysOfThePages();
    void dragOfPages();
    void sortMenu();
    void moveOfFiles();
    void statesOfFiles();
    void restrictedFileTexts();
    void passwordOfFile();
    void detailsView();
    void outlineAndOptimizeMenus();
    void tileSizeOfPreview();
    void dropAndShortcutsAddFiles();
    void runParametersAndEnvironment();
    void runWithoutPagesOfFile();
    void createPdfOpensNewTab();
    void partialResult();
    void themesRightToLeftAndScale();
    void screenshotsOfFrames();
    void manyFilesKeepGuiResponsive();
    void largeInput();
    void deleteTabInEveryState_data();
    void deleteTabInEveryState();
    void mcpMergeOverHttp();
};

/// Helpers of the test
class MergeTestHelper
{
public:
    MergeTestHelper() = delete;

    static PDFAppMergeToolGUI* gui(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? qobject_cast<PDFAppMergeToolGUI*>(shell->getGUI()) : nullptr;
    }

    static bool waitLoaded(const pdf::PDFAppToolInputList* list)
    {
        return AppToolOwnTabFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; });
    }

    /// An own tab of Merge with the files (read before the tab is shown)
    static pdf::PDFAppToolTab* open(AppToolOwnTabFixture& fixture, const QStringList& files)
    {
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles(files);
        if (!waitLoaded(list))
        {
            delete list;
            return nullptr;
        }
        pdf::PDFAppToolTab* tab = fixture.createToolTab(QString::fromLatin1(pdf::PDFDocumentToolMerge::TOOL_ID), list);
        AppToolOwnTabFixture::settle();
        return tab;
    }

    /// A PNG of the size and the resolution (dpi)
    static QString png(const QString& folder, const QString& name, QSize size, int dpi)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(200, 220, 240));
        image.setDotsPerMeterX(int(std::lround(dpi / 0.0254)));
        image.setDotsPerMeterY(int(std::lround(dpi / 0.0254)));
        const QString path = folder + QLatin1Char('/') + name;
        return image.save(path, "PNG") ? path : QString();
    }

    /// A file with the bytes
    static QString file(const QString& folder, const QString& name, const QByteArray& data)
    {
        QFile file(folder + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly))
        {
            return QString();
        }
        file.write(data);
        return file.fileName();
    }

    /// The pages of the result in the order of the canvas: "A1", "B2@90"
    static QStringList pages(const PDFAppMergeToolGUI* gui)
    {
        QStringList pages;
        const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();
        const pdf::PDFAppToolInputList* list = model->getInputList();
        for (int row = 0; row < model->rowCount(); ++row)
        {
            const PDFAppToolMergePlan::Item* item = model->getItem(row);
            QString text = pdf::PDFAppToolInputList::getLetter(list->indexOf(item->inputId)) + QString::number(item->pageIndex + 1);
            if (item->turn != pdf::PageRotation::None)
            {
                text += QStringLiteral("@%1").arg(int(item->turn) * 90);
            }
            pages << text;
        }
        return pages;
    }

    static QString status(const pdf::PDFAppToolTab* tab)
    {
        return tab->getToolPage()->getTabView()->getToolMessage();
    }

    static QStringList rowLines(const PDFAppMergeToolGUI* gui, int index)
    {
        const std::vector<pdf::PDFAppToolInputRow*> rows = gui->getFilesPanel()->getRows();
        if (index < 0 || size_t(index) >= rows.size())
        {
            return QStringList();
        }
        const pdf::PDFAppToolInputRow::Content& content = rows[size_t(index)]->getContent();
        return { content.letter, content.name, content.tag, content.firstLine, content.secondLine, content.linkText };
    }

    static pdf::PDFAppToolParameterModel* model(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? shell->getModel() : nullptr;
    }
};

using H = MergeTestHelper;

void AppToolMergeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolMergeTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFAppFileDialogs::setOpenFilesHandler(nullptr);
}

void AppToolMergeTest::planText()
{
    // The syntax of the core (parsePlan: "A1-6,B1-9,C1,A3@90,A3+") with the letters by the
    // position in the list and a pending input as "B*"; the clone is derived from the order
    const QStringList ids{ QStringLiteral("i1"), QStringLiteral("i2"), QStringLiteral("i3") };
    std::optional<PDFAppToolMergePlan> plan = PDFAppToolMergePlan::fromText(QStringLiteral("A1-3,B*,C2,A2@90+,A2+"), ids, { 3, -1, 2 }, nullptr);
    QVERIFY(plan);
    QCOMPARE(plan->getItems().size(), size_t(7));
    QVERIFY(plan->getItems()[3].isPending);
    QCOMPARE(plan->getItems()[3].inputId, QStringLiteral("i2"));
    QCOMPARE(plan->getItems()[5].turn, pdf::PageRotation::Rotate90);
    QVERIFY(plan->isClone(5));
    QVERIFY(plan->isClone(6));
    QVERIFY(!plan->isClone(1));
    QCOMPARE(plan->getPageCount(), pdf::PDFInteger(6));
    QCOMPARE(plan->toText(ids, true), QStringLiteral("A1-3,B*,C2,A2@90+,A2+"));

    // The run: only the usable inputs, their letters among them, no pending input
    QCOMPARE(plan->toText({ QStringLiteral("i1"), QStringLiteral("i3") }, false), QStringLiteral("A1-3,B2,A2@90+,A2+"));

    // The pending input becomes its pages, when they are known; a page out of the range of its
    // input is left out; an input without an item is no part of the result (its pages were
    // removed by the user)
    const PDFAppToolMergePlan resolved = plan->resolved(ids, { 3, 2, 1 });
    QCOMPARE(resolved.toText(ids, true), QStringLiteral("A1-3,B1-2,A2@90+,A2+"));
    const PDFAppToolMergePlan withNew = PDFAppToolMergePlan::fromText(QStringLiteral("A1"), { QStringLiteral("i1") }, { 1 }, nullptr)->resolved({ QStringLiteral("i1"), QStringLiteral("i4") }, { 1, 2 });
    QCOMPARE(withNew.toText({ QStringLiteral("i1"), QStringLiteral("i4") }, true), QStringLiteral("A1"));

    // An empty text is every page of every input; an invalid text is refused with its reason
    QCOMPARE(PDFAppToolMergePlan::fromText(QString(), ids, { 3, -1, 2 }, nullptr)->resolved(ids, { 3, -1, 2 }).toText(ids, true), QStringLiteral("A1-3,B*,C1-2"));
    QString error;
    QVERIFY(!PDFAppToolMergePlan::fromText(QStringLiteral("D*"), ids, { 3, -1, 2 }, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PDFAppToolMergePlan::fromText(QStringLiteral("A4"), ids, { 3, -1, 2 }, &error));
    QVERIFY(!PDFAppToolMergePlan::fromText(QStringLiteral("B1"), ids, { 3, -1, 2 }, &error));
}

void AppToolMergeTest::planOperations()
{
    const QStringList ids{ QStringLiteral("a"), QStringLiteral("b") };
    PDFAppToolMergePlan plan = PDFAppToolMergePlan::createDefault(ids).resolved(ids, { 3, 2 });
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A1-3,B1-2"));

    // Move of A1 and A3 in front of B2 (the target counts among the items, which stay)
    std::vector<size_t> moved = plan.moveItems({ 0, 2 }, 4);
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A2,B1,A1,A3,B2"));
    QCOMPARE(moved, (std::vector<size_t>{ 2, 3 }));

    // A clone follows its page; the first occurrence is the page, the later one the clone
    std::vector<size_t> clones = plan.cloneItems({ 1 });
    QCOMPARE(clones, std::vector<size_t>{ 2 });
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A2,B1,B1+,A1,A3,B2"));
    plan.removeItems({ 1 });
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A2,B1,A1,A3,B2"));

    // A turn is a quarter turn more each time, relative to the rotation of the page
    plan.rotateItems({ 0 }, true);
    plan.rotateItems({ 0 }, true);
    plan.rotateItems({ 4 }, false);
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A2@180,B1,A1,A3,B2@270"));

    // K13: by the original page numbers, equal numbers by the order of the files
    plan.sortByPageNumber(ids);
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A1,B1,A2@180,B2@270,A3"));

    // The order of the files takes the pages with them, each file keeps its own order
    plan.orderByInputs({ QStringLiteral("b"), QStringLiteral("a") });
    QCOMPARE(plan.toText(ids, true), QStringLiteral("B1,B2@270,A1,A2@180,A3"));

    // Reverse: the runs in the reverse order, a run keeps its pages
    plan.reverseRuns();
    QCOMPARE(plan.toText(ids, true), QStringLiteral("A1,A2@180,A3,B1,B2@270"));
    plan.removeInput(QStringLiteral("a"));
    QCOMPARE(plan.toText(ids, true), QStringLiteral("B1,B2@270"));
}

void AppToolMergeTest::registeredAndOutOfSchema()
{
    // The context registers the tool and its GUI; Q11: the parameters inputs and plan are of
    // the panel only - the schema of the assistant has documentIds instead of them
    AppToolOwnTabFixture fixture;
    QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolMerge>(pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.merge"))));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(QStringLiteral("pages.merge")));

    const std::optional<pdf::PDFAppMcpToolSpec> spec = pdf::PDFAppMcpCatalogTools::createToolSpec(pdf::PDFDocumentToolCategory::Pages);
    QVERIFY(spec);
    bool isFound = false;
    for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
    {
        if (action.name == QLatin1String("merge"))
        {
            isFound = true;
            const QJsonObject properties = QJsonDocument::fromJson(action.input.toJson()).object().value(QStringLiteral("properties")).toObject();
            QVERIFY(properties.contains(QStringLiteral("outline")));
            QVERIFY(properties.contains(QStringLiteral("optimizeImages")));
            QVERIFY(!properties.contains(QStringLiteral("inputs")));
            QVERIFY(!properties.contains(QStringLiteral("plan")));
        }
    }
    QVERIFY(isFound);

    // At a glance of the start page (K9): a locked document can be merged
    std::unique_ptr<pdf::PDFDocumentToolGUI> gui(pdf::PDFDocumentToolGUIRegistry::getFactory(QStringLiteral("pages.merge"))->createGUI());
    QCOMPARE(gui->getLockedGlanceText(), QStringLiteral("Can be merged"));
}

void AppToolMergeTest::framesOfThreeFiles()
{
    // pages-merge 378:1049: Contract.pdf (6 pages), Annex_A.pdf (9 pages), an image of 2480 x
    // 3508 px at 300 dpi (A4)
    AppToolOwnTabFixture fixture;
    const QString contract = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9);
    const QString image = H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(2480, 3508), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, { contract, annex, image });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    const pdf::PDFAppToolInputList* list = tab->getInputList();
    const qint64 totalSize = QFileInfo(contract).size() + QFileInfo(annex).size() + QFileInfo(image).size();

    // Files: "3 files · 16 pages · 18.6 MB", the rows A, B, C with their runs of the result
    QCOMPARE(gui->getFilesPanel()->getTitleLabel()->text(), QStringLiteral("Files"));
    QCOMPARE(gui->getFilesPanel()->getSubtitleLabel()->text(), QStringLiteral("3 files · 16 pages · %1").arg(pdf::PDFDocumentToolFormat::formatSize(totalSize)));
    QCOMPARE(H::rowLines(gui, 0).mid(0, 2), QStringList({ QStringLiteral("A"), QStringLiteral("Contract.pdf") }));
    QVERIFY(H::rowLines(gui, 0)[3].startsWith(QStringLiteral("6 pages · ")));
    QCOMPARE(H::rowLines(gui, 0)[4], QStringLiteral("Pages 1–6 of the result"));
    QCOMPARE(H::rowLines(gui, 1)[4], QStringLiteral("Pages 7–15 of the result"));
    QCOMPARE(H::rowLines(gui, 2)[3], QStringLiteral("Image · 2,480 × 3,508 px · %1").arg(pdf::PDFDocumentToolFormat::formatSize(QFileInfo(image).size())));
    QCOMPARE(H::rowLines(gui, 2)[4], QStringLiteral("Page 16 of the result"));
    QCOMPARE(gui->getFilesPanel()->getHintLabel()->text(), QStringLiteral("Drag a file to change the order of the files. Drag pages in the preview to move single pages. Drop more files anywhere in the window."));

    // The rows: 275 wide (the content of the panel 279 without 2 + 2), 59 high, 10 apart
    const std::vector<pdf::PDFAppToolInputRow*> rows = gui->getFilesPanel()->getRows();
    QCOMPARE(rows.size(), size_t(3));
    QCOMPARE(rows[0]->size(), QSize(275, 59));
    QCOMPARE(rows[1]->y() - rows[0]->y(), 59 + 10);

    // K15 / K40: the hues blue, green, violet by the position
    QCOMPARE(rows[0]->getContent().hue, pdf::PDFAccentHue::Blue);
    QCOMPARE(rows[1]->getContent().hue, pdf::PDFAccentHue::Green);
    QCOMPARE(rows[2]->getContent().hue, pdf::PDFAccentHue::Violet);

    // The canvas: "Result · 16 pages", three groups by the runs (K14) with their subtitles, the
    // image with its Tag
    pdf::PDFAppToolMergeCanvas* canvas = gui->getCanvas();
    QCOMPARE(canvas->getHeaderLabel()->text(), QStringLiteral("Result · 16 pages"));
    const std::vector<pdf::PDFAppToolMergePageModel::Group>& groups = canvas->getModel()->getGroups();
    QCOMPARE(groups.size(), size_t(3));
    QCOMPARE(groups[0].title, QStringLiteral("A · Contract.pdf"));
    QCOMPARE(groups[0].subtitle, QStringLiteral("Pages 1–6"));
    QCOMPARE(groups[1].subtitle, QStringLiteral("Pages 7–15"));
    QCOMPARE(groups[2].subtitle, QStringLiteral("Page 16"));
    QCOMPARE(groups[2].tag, QStringLiteral("Image"));
    QVERIFY(groups[0].tag.isEmpty());

    // The tiles of the size Small: 85 x 128 (378:1249), 8 apart; the cards 10 apart, the
    // first tile 11 from the edge of its card (the padding 10 and the border)
    gui->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Small);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolMergePageView* view = canvas->getPageView();
    const QRect first = view->visualRect(canvas->getModel()->index(0, 0));
    const QRect second = view->visualRect(canvas->getModel()->index(1, 0));
    QCOMPARE(first.size(), QSize(85, 128));
    QCOMPARE(second.x() - first.x(), 85 + 8);
    QCOMPARE(first.x() - view->getGroupRect(0).x(), 11);
    QCOMPARE(view->getGroupRect(1).top() - view->getGroupRect(0).bottom() - 1, 10);
    QCOMPARE(view->getGroupRect(0).height(), 9 + 15 + 8 + 128 + 11);
    QCOMPARE(view->getGroupRect(2).height(), 9 + 21 + 8 + 128 + 11);

    // The right panel: Result (378:1643) and the note of the outline
    QVERIFY(!gui->getAttentionCallout()->isVisible());
    QVERIFY(!gui->getHowSection()->isVisible());
    QCOMPARE(gui->getResultValues()->getValue(0), QStringLiteral("16 from 3 files"));
    QCOMPARE(gui->getResultValues()->getValue(1), QStringLiteral("3 document parts"));
    QCOMPARE(gui->getResultValues()->getValue(2), QStringLiteral("Stored as they are"));
    QCOMPARE(gui->getResultValues()->getValue(3), QStringLiteral("about %1").arg(pdf::PDFDocumentToolFormat::formatSize(totalSize)));
    QCOMPARE(gui->getOutlineNote()->text(), QStringLiteral("Document parts: the result gets one bookmark per source file. The source files are never changed."));
    // 378:1656: Regular 11 text/muted in lines of 13, 14 below the card of Result
    QCOMPARE(gui->getOutlineNote()->font().pixelSize(), 11);
    QCOMPARE(gui->getOutlineNote()->getLineHeight(), 13);
    QCOMPARE(gui->getOutlineNote()->getTextColor(), pdf::PDFThemeColors::TextMuted);

    // Owner 2026-11-08, 378:1643 / 378:1630: the rows of the cards of 28 with a pitch of 36 (the
    // rows of the card of Result at 7, 43, 79, 115)
    for (pdf::PDFKeyValueList* values : { gui->getResultValues(), gui->getSelectedValues() })
    {
        QCOMPARE(values->getRowGap(), 8);
        QCOMPARE(values->getRowRect(1).top() - values->getRowRect(0).top(), 36);
        QCOMPARE(values->getRowRect(0).height(), 28);
    }
    QCOMPARE(gui->getResultValues()->heightForWidth(261), 4 * 28 + 3 * 8);

    // Owner 2026-11-08, panel-add 378:2497: Files… and Paste image show their keys at the
    // trailing edge (378:2505 "Ctrl+I", 378:2513 "Ctrl+V"), the native texts of the platform
    QCOMPARE(gui->getPanelRow(QStringLiteral("files"))->getShortcutText(), QKeySequence(Qt::CTRL | Qt::Key_I).toString(QKeySequence::NativeText));
    QCOMPARE(gui->getPanelRow(QStringLiteral("paste"))->getShortcutText(), QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText));
#if !defined(Q_OS_MACOS)
    QCOMPARE(gui->getPanelRow(QStringLiteral("files"))->getShortcutText(), QStringLiteral("Ctrl+I"));
    QCOMPARE(gui->getPanelRow(QStringLiteral("paste"))->getShortcutText(), QStringLiteral("Ctrl+V"));
#endif

    // 378:1049: the sidebar button of the own tab hides and shows the panel Files (enabled in
    // every frame of the tool, as in the tool mode of a document)
    const QString toggleSidebar = pdf::PDFAppCommands::toggleSidebar();
    QVERIFY(tab->isCommandEnabled(toggleSidebar));
    QVERIFY(gui->getFilesPanel()->isVisible());
    QVERIFY(tab->executeCommand(toggleSidebar));
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getFilesPanel()->isVisible());
    QVERIFY(tab->getToolPage()->getHost()->isCommandChecked(toggleSidebar) == false);
    QVERIFY(tab->executeCommand(toggleSidebar));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getFilesPanel()->isVisible());
    QVERIFY(tab->getToolPage()->getHost()->isCommandChecked(toggleSidebar));

    // Selected page (378:1625): page 3 of Contract.pdf, the size of the sample, no rotation
    gui->selectRows({ 2 });
    QVERIFY(gui->getSelectedSection()->isVisible());
    QCOMPARE(gui->getSelectedTag()->text(), QStringLiteral("Page 3 of 16"));
    QCOMPARE(gui->getSelectedValues()->getValue(0), QStringLiteral("Contract.pdf, p. 3"));
    QCOMPARE(gui->getSelectedValues()->getValue(1), QStringLiteral("106 × 141 mm"));
    QCOMPARE(gui->getSelectedValues()->getValue(2), QStringLiteral("None"));
    QVERIFY(gui->getFilesPanel()->findRow(list->getInput(0)->id)->isCurrent());

    // The image page at 300 dpi is A4 (Q6)
    gui->selectRows({ 15 });
    QCOMPARE(gui->getSelectedValues()->getValue(0), QStringLiteral("signature_page.png"));
    QCOMPARE(gui->getSelectedValues()->getValue(1), QStringLiteral("A4 · 210 × 297 mm"));
    gui->selectRows({ 2 });
    AppToolOwnTabFixture::settle();

    // The toolbar: Outline: Parts, Optimize: Off, the actions of the pages with a selection
    QCOMPARE(gui->getAction(QStringLiteral("outline"))->text(), QStringLiteral("Outline: Parts"));
    QCOMPARE(gui->getAction(QStringLiteral("optimize"))->text(), QStringLiteral("Optimize: Off"));
    QCOMPARE(gui->getAction(QStringLiteral("addFiles"))->text(), QStringLiteral("Add Files"));
    QVERIFY(gui->getAction(QStringLiteral("rotateLeft"))->isEnabled());
    QVERIFY(gui->getAction(QStringLiteral("sort"))->isEnabled());

    // The status bar 378:1084: the message, "16 pages", no zoom (K12); Create PDF works
    QCOMPARE(H::status(tab), QStringLiteral("Merge · 3 files · 16 pages · nothing is written until you choose Create PDF"));
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("16 pages"));
    QVERIFY(gui->getOwnTabZoomText().isEmpty());
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Create PDF"));
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(gui->getOwnTabLossText(), QStringLiteral("The list of 3 files is lost."));
    QVERIFY(tab->isTabUnsaved());
    fixture.screenshot(QStringLiteral("pages-merge"));
}

void AppToolMergeTest::emptyStateAndOpenDocuments()
{
    // pages-merge-empty 378:1657: no file, the drop zone with the open documents of the window
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.openDocument(3);
    QVERIFY(documentTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }));
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QVERIFY(gui);

    QCOMPARE(gui->getFilesPanel()->getSubtitleLabel()->text(), QStringLiteral("No files yet"));
    QCOMPARE(gui->getFilesPanel()->getHintLabel()->text(), QStringLiteral("The files you add are listed here in the order in which they are merged."));
    pdf::PDFAppToolMergeEmptyState* emptyState = gui->getEmptyState();
    QVERIFY(emptyState);
    QVERIFY(emptyState->isVisible());
    QCOMPARE(emptyState->getCard()->width(), 540);
    QCOMPARE(emptyState->getAddFilesButton()->text(), QStringLiteral("Add Files…"));
    QCOMPARE(emptyState->getAddButtons().size(), size_t(1));
    QVERIFY(!gui->getCanvas()->getHeaderLabel()->isVisible());
    QVERIFY(gui->getHowSection()->isVisible());
    QVERIFY(!gui->getResultSection()->isVisible());
    QVERIFY(!gui->getAction(QStringLiteral("rotateLeft"))->isEnabled());
    QVERIFY(!gui->getAction(QStringLiteral("sort"))->isEnabled());
    QVERIFY(gui->getAction(QStringLiteral("outline"))->isEnabled());
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Merge · add two or more files to combine them into one PDF"));
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("No pages"));
    QVERIFY(gui->getOwnTabLossText().isEmpty());
    // K21: nothing to undo at the first opening
    QVERIFY(!H::model(tab)->canUndo());

    // tab-1 378:1851: the name (line 15) and "3 pages · …" (line 13) 1 apart, together in the
    // middle of the row of 52 (8 + 36 + 8): 16 from the top of the name to the line below it
    {
        const QWidget* row = emptyState->findChild<QWidget*>(QStringLiteral("mergeEmptyDocument-") + documentTab->getDocument()->getId());
        QVERIFY(row);
        const QList<pdf::PDFLabel*> labels = row->findChildren<pdf::PDFLabel*>();
        QCOMPARE(labels.size(), 2);
        const QRect name = labels[0]->geometry();
        const QRect details = labels[1]->geometry();
        QCOMPARE(name.height(), 15);
        QCOMPARE(details.top() - name.top(), 16);
        QVERIFY(qAbs((name.top() + details.bottom() + 1) / 2.0 - row->height() / 2.0) <= 1.0);
    }
    fixture.screenshot(QStringLiteral("pages-merge-empty"));

    // Add of the open document: its snapshot (R11), the drop zone goes, one undo step
    emptyState->getAddButtons().front()->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QCOMPARE(tab->getInputList()->getInput(0)->kind, pdf::PDFAppToolInputKind::OpenTab);
    QVERIFY(!gui->getEmptyState());
    QVERIFY(H::model(tab)->canUndo());

    // Add Files offers the open document again as "already in the list" (K29)
    pdf::PDFTileRow* documentRow = gui->getPanelRow(documentTab->getDocument()->getId());
    QVERIFY(documentRow);
    QVERIFY(!documentRow->isEnabled());
    QVERIFY(documentRow->getDescription().endsWith(QStringLiteral("already in the list")));
    QCOMPARE(H::rowLines(gui, 0)[2], QStringLiteral("open tab"));

    // Undo brings the empty state back
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 0);
    QVERIFY(gui->getEmptyState());
}

void AppToolMergeTest::oneFileIsNotEnough()
{
    // K22 (no frame): Create PDF needs two usable files; a file without pages does not count
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("one.pdf"), 2) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Merge · add one more file"));

    gui->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("two.pdf"), 1) });
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("B1") }));
}

void AppToolMergeTest::undoRedoOfFilesAndPages()
{
    // R12: Undo / Redo of the files and of the pages; a file comes back without a second
    // reading; K21: the files of the opening are no step
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    pdf::PDFAppToolParameterModel* model = H::model(tab);
    pdf::PDFAppToolInputList* list = tab->getInputList();
    QVERIFY(!model->canUndo());
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("B1") }));

    // A third file: one step
    gui->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 2) });
    QVERIFY(H::waitLoaded(list));
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("B1"), QStringLiteral("C1"), QStringLiteral("C2") }));
    const int readings = list->getStartedReadingCount();

    // A turn: one step
    gui->selectRows({ 1 });
    gui->rotateRight();
    QCOMPARE(H::pages(gui)[1], QStringLiteral("A2@90"));
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 1 });

    // Removal of the file B: one step, the file parked
    const QString idB = list->getInput(1)->id;
    gui->removeInput(idB);
    QCOMPARE(list->getCount(), 2);
    QVERIFY(list->isParked(idB));
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2@90"), QStringLiteral("B1"), QStringLiteral("B2") }));

    // Undo: B back in its place with its page, without a second reading; then the turn goes
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 3);
    QCOMPARE(list->getInput(1)->id, idB);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2@90"), QStringLiteral("B1"), QStringLiteral("C1"), QStringLiteral("C2") }));
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::pages(gui)[1], QStringLiteral("A2"));
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QVERIFY(!model->canUndo());

    // Redo of everything; nothing is read again
    model->redo();
    model->redo();
    model->redo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2@90"), QStringLiteral("B1"), QStringLiteral("B2") }));
    QCOMPARE(list->getStartedReadingCount(), readings);

    // A new step after an Undo drops the redo steps: the parked file, which no step names, goes
    model->undo();
    AppToolOwnTabFixture::settle();
    QVERIFY(!list->isParked(idB));
    model->undo();
    AppToolOwnTabFixture::settle();
    gui->selectRows({ 0 });
    gui->rotateLeft();
    model->undo();
    model->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QVERIFY(list->isParked(QStringLiteral("i3")));
    gui->selectRows({ 0 });
    gui->rotateLeft();
    QVERIFY(!list->isParked(QStringLiteral("i3")));
    QVERIFY(!list->findInput(QStringLiteral("i3")));
}

void AppToolMergeTest::pageActions()
{
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);

    // No selection: the actions of the pages are disabled (378:1657 has them Disabled)
    gui->selectRows({});
    QVERIFY(!gui->getAction(QStringLiteral("clone"))->isEnabled());

    // Rotate Right twice and Left once: 90° right of the page; Selected page says it
    gui->selectRows({ 1 });
    gui->getAction(QStringLiteral("rotateRight"))->click();
    gui->getAction(QStringLiteral("rotateRight"))->click();
    gui->getAction(QStringLiteral("rotateLeft"))->click();
    QCOMPARE(H::pages(gui)[1], QStringLiteral("A2@90"));
    QCOMPARE(gui->getSelectedValues()->getValue(2), QStringLiteral("90° right"));
    // The turned page is landscape (300 x 400 points turned)
    QCOMPARE(gui->getSelectedValues()->getValue(1), QStringLiteral("141 × 106 mm"));

    // Clone: the copy right after its page, selected; the plan marks it as the copy
    gui->getAction(QStringLiteral("clone"))->click();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2@90"), QStringLiteral("A2@90"), QStringLiteral("A3"), QStringLiteral("B1"), QStringLiteral("B2") }));
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 2 });
    QVERIFY(gui->getPlan().isClone(2));
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("6 pages"));

    // Remove: the page goes, the next one is selected
    gui->selectRows({ 0, 1 });
    gui->getAction(QStringLiteral("remove"))->click();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A2@90"), QStringLiteral("A3"), QStringLiteral("B1"), QStringLiteral("B2") }));
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 0 });
    QCOMPARE(H::rowLines(gui, 0)[4], QStringLiteral("Pages 1–2 of the result"));

    // A file without any page in the result is no part of it; Create PDF needs a page
    gui->selectRows({ 0, 1 });
    gui->removeSelection();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("B1"), QStringLiteral("B2") }));
    QCOMPARE(H::rowLines(gui, 0)[4], QStringLiteral("Not in the result"));
    QCOMPARE(gui->getCanvas()->getModel()->getGroups().size(), size_t(1));
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Merge · the result needs pages of two or more files"));
}

void AppToolMergeTest::keysOfThePages()
{
    // The keys of the preview: Delete removes, Alt + an arrow moves the selection, Shift
    // extends it, the same keys in Details (Alt + Up / Down)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 4) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    pdf::PDFAppToolMergePageView* view = gui->getCanvas()->getPageView();
    view->setFocus();
    gui->selectRows({ 0 });

    QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(gui->getSelectedRows(), (std::vector<int>{ 0, 1 }));
    QTest::keyClick(view, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A3"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A4") }));
    QCOMPARE(gui->getSelectedRows(), (std::vector<int>{ 1, 2 }));
    QTest::keyClick(view, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A4") }));
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A3"), QStringLiteral("A4") }));

    // Right to left: the left arrow goes on in the flow
    QApplication::setLayoutDirection(Qt::RightToLeft);
    AppToolOwnTabFixture::settle();
    gui->selectRows({ 0 });
    QTest::keyClick(view, Qt::Key_Left);
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 1 });
    QApplication::setLayoutDirection(Qt::LeftToRight);
    AppToolOwnTabFixture::settle();

    // Details: Alt + Down moves the selected row, Delete removes it
    gui->setDetailsShown(true);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolMergeDetailsView* details = gui->getCanvas()->getDetailsView();
    details->setFocus();
    gui->selectRows({ 0 });
    QTest::keyClick(details, Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A4"), QStringLiteral("A3") }));
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 1 });
    QTest::keyClick(details, Qt::Key_Delete);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A4") }));
}

void AppToolMergeTest::dragOfPages()
{
    // "Drag pages in the preview to move single pages" (378:1223): a press on a selected page,
    // a move, a release in front of another page
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    gui->setTileSize(pdf::PDFAppToolPageGrid::TileSize::Small);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolMergePageView* view = gui->getCanvas()->getPageView();
    const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();

    // A1 dragged to the leading half of B2: in front of B2
    const QPoint from = view->visualRect(model->index(0, 0)).center();
    const QRect target = view->visualRect(model->index(4, 0));
    const QPoint to(target.left() + 10, target.center().y());
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), from + QPoint(20, 0));
    QTest::mouseMove(view->viewport(), to);
    QVERIFY(view->isDragging());
    QCOMPARE(view->getDropTargetRow(), 4);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("B1"), QStringLiteral("A1"), QStringLiteral("B2") }));
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 3 });
    // K14: the moved page is a group of its own, the outline has 4 parts
    QCOMPARE(model->getGroups().size(), size_t(4));
    QCOMPARE(gui->getResultValues()->getValue(1), QStringLiteral("4 document parts"));

    // A click on a page of a selection of more pages without a move selects it alone
    gui->selectRows({ 0, 1 });
    const QPoint click = view->visualRect(model->index(1, 0)).center();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, click);
    QCOMPARE(gui->getSelectedRows(), std::vector<int>{ 1 });

    // Ctrl toggles, Shift selects the range from the anchor
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, view->visualRect(model->index(3, 0)).center());
    QCOMPARE(gui->getSelectedRows(), (std::vector<int>{ 1, 3 }));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, view->visualRect(model->index(4, 0)).center());
    QCOMPARE(gui->getSelectedRows(), (std::vector<int>{ 3, 4 }));
}

void AppToolMergeTest::sortMenu()
{
    // 378:2532: File name in the natural order, File type (PDF documents first), Page number
    // (K13), Reverse order
    AppToolOwnTabFixture fixture;
    const QString image = H::png(fixture.folder(), QStringLiteral("a1.png"), QSize(100, 100), 72);
    pdf::PDFAppToolTab* tab = H::open(fixture, { image, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a10.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a2.pdf"), 2) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const pdf::PDFAppToolInputList* list = tab->getInputList();

    QStringList names;
    gui->getPanelRow(QStringLiteral("sort-name"))->click();
    for (int i = 0; i < list->getCount(); ++i)
    {
        names << list->getInput(i)->name;
    }
    QCOMPARE(names, QStringList({ QStringLiteral("a1.png"), QStringLiteral("a2.pdf"), QStringLiteral("a10.pdf") }));
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("C1"), QStringLiteral("C2") }));

    gui->getPanelRow(QStringLiteral("sort-type"))->click();
    QCOMPARE(list->getInput(2)->name, QStringLiteral("a1.png"));
    QCOMPARE(list->getInput(0)->name, QStringLiteral("a2.pdf"));

    gui->getPanelRow(QStringLiteral("sort-page"))->click();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("C1"), QStringLiteral("A2"), QStringLiteral("B2") }));

    gui->getPanelRow(QStringLiteral("sort-reverse"))->click();
    QCOMPARE(list->getInput(0)->name, QStringLiteral("a1.png"));
    // The files reversed (a1.png, a10.pdf, a2.pdf) and the runs of their pages reversed
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("B2"), QStringLiteral("C2"), QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("C1") }));

    // Every sort is one undo step
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("C1"), QStringLiteral("A2"), QStringLiteral("B2") }));
}

void AppToolMergeTest::moveOfFiles()
{
    // "Drag a file to change the order of the files" and Alt + Up / Down on a row: the file
    // takes its pages with it, the letters and the hues follow the order
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("x.pdf"), 1), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("y.pdf"), 2) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const QString idX = tab->getInputList()->getInput(0)->id;

    pdf::PDFAppToolInputRow* row = gui->getFilesPanel()->findRow(idX);
    row->setFocus();
    QTest::keyClick(row, Qt::Key_Down, Qt::AltModifier);
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->indexOf(idX), 1);
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("B1") }));
    QCOMPARE(H::rowLines(gui, 1)[1], QStringLiteral("x.pdf"));
    QCOMPARE(H::rowLines(gui, 1)[0], QStringLiteral("B"));
    QCOMPARE(gui->getFilesPanel()->getRows()[1]->getContent().hue, pdf::PDFAccentHue::Green);

    // The drag of the row over the upper half of the first row: to the top
    row = gui->getFilesPanel()->findRow(idX);
    pdf::PDFAppToolInputRow* firstRow = gui->getFilesPanel()->getRows()[0];
    const QPoint start = row->rect().center();
    QTest::mousePress(row, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(row, start + QPoint(0, -10));
    const QPoint targetGlobal = firstRow->mapToGlobal(QPoint(20, 5));
    QTest::mouseMove(row, row->mapFromGlobal(targetGlobal));
    QTest::mouseRelease(row, Qt::LeftButton, Qt::NoModifier, row->mapFromGlobal(targetGlobal));
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->indexOf(idX), 0);

    // Delete on a row removes the file (one undo step)
    row = gui->getFilesPanel()->findRow(idX);
    row->setFocus();
    QTest::keyClick(row, Qt::Key_Delete);
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QVERIFY(H::model(tab)->canUndo());
}

void AppToolMergeTest::statesOfFiles()
{
    // 378:2613 / 1390:34070 (K39): a password, a damaged file, an unsupported file, a file
    // without pages - the rows, the groups, the callout, the status, Create PDF
    AppToolOwnTabFixture fixture;
    const QString good = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2);
    const QString locked = AppDocumentSamples::userPassword(fixture.folder());
    const QString damaged = H::file(fixture.folder(), QStringLiteral("Appendix_old.pdf"), QByteArrayLiteral("%PDF-1.7\nthis is no document at all\n"));
    const QString unsupported = H::file(fixture.folder(), QStringLiteral("notes.docx"), QByteArrayLiteral("PK not an image, not a PDF"));
    pdf::PDFAppToolTab* tab = H::open(fixture, { good, locked, damaged, unsupported });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const pdf::PDFAppToolInputList* list = tab->getInputList();
    QCOMPARE(list->getInput(1)->state, pdf::PDFAppToolInputState::PasswordRequired);
    QCOMPARE(list->getInput(2)->state, pdf::PDFAppToolInputState::Damaged);
    QCOMPARE(list->getInput(3)->state, pdf::PDFAppToolInputState::Unsupported);

    QCOMPARE(gui->getFilesPanel()->getSubtitleLabel()->text(), QStringLiteral("4 files · 3 need attention"));
    QCOMPARE(H::rowLines(gui, 1).mid(3), QStringList({ QStringLiteral("Protected by a password"), QStringLiteral("The pages cannot be read yet"), QStringLiteral("Enter password…") }));
    QVERIFY(gui->getFilesPanel()->getRows()[1]->getContent().isFirstLineProblem);
    QCOMPARE(H::rowLines(gui, 2).mid(3), QStringList({ QStringLiteral("Damaged – the file cannot be read"), QStringLiteral("Remove it to continue"), QStringLiteral("Remove file") }));
    QVERIFY(H::rowLines(gui, 3)[3].startsWith(QStringLiteral("Unsupported – ")));
    QCOMPARE(gui->getFilesPanel()->getRows()[1]->height(), 76);

    // The canvas: A with its pages, B as a locked group (only a file, which waits, has a group)
    const std::vector<pdf::PDFAppToolMergePageModel::Group>& groups = gui->getCanvas()->getModel()->getGroups();
    QCOMPARE(groups.size(), size_t(2));
    QCOMPARE(groups[1].subtitle, QStringLiteral("Pages unknown until unlocked"));
    QCOMPARE(groups[1].callout, pdf::PDFAppToolMergePageModel::GroupCallout::Locked);
    QCOMPARE(groups[1].calloutTitle, QStringLiteral("Password required"));
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Result · 2 pages can be read"));

    // The callout of the right panel names the files and what to do
    QVERIFY(gui->getAttentionCallout()->isVisible());
    QCOMPARE(gui->getAttentionCallout()->getTitleLabel()->text(), QStringLiteral("3 files need attention"));
    QCOMPARE(gui->getAttentionCallout()->getTextLabel()->text(),
             QStringLiteral("Create PDF is available when every file in the list can be used. Enter the password of user-password.pdf and remove Appendix_old.pdf and notes.docx."));
    QCOMPARE(H::status(tab), QStringLiteral("Merge · 3 of 4 files cannot be used yet – enter the password or remove them"));
    QVERIFY(!gui->isConfirmEnabled());

    // callout-red 378:3015: paddings 10 / 12 / 10 / 12 (+ the border), the head and the text 4
    // apart, the text Regular 11 in lines of 13
    const pdf::PDFAppToolCallout* callout = gui->getAttentionCallout();
    QCOMPARE(callout->getBodyLayout()->contentsMargins(), QMargins(13, 11, 13, 11));
    QCOMPARE(callout->getBodyLayout()->spacing(), 4);
    QCOMPARE(callout->getTextLabel()->font().pixelSize(), 11);
    QCOMPARE(callout->getTextLabel()->getLineHeight(), 13);
    QCOMPARE(callout->getTitleLabel()->getLineHeight(), 15);

    // 378:2809 / 1390:34259: the name of a file, which adds no page, is text/tertiary; a file,
    // which waits for its password, keeps text/primary (378:2785)
    QVERIFY(!gui->getFilesPanel()->getRows()[0]->getContent().isNameMuted);
    QVERIFY(!gui->getFilesPanel()->getRows()[1]->getContent().isNameMuted);
    QVERIFY(gui->getFilesPanel()->getRows()[2]->getContent().isNameMuted);
    QVERIFY(gui->getFilesPanel()->getRows()[3]->getContent().isNameMuted);
    {
        // The darkest pixel of the name: text/tertiary #475569, not text/primary #0f172a
        const QImage damagedRow = AppToolOwnTabFixture::render(gui->getFilesPanel()->getRows()[2], 1.0).convertToFormat(QImage::Format_RGB32);
        const QImage lockedRow = AppToolOwnTabFixture::render(gui->getFilesPanel()->getRows()[1], 1.0).convertToFormat(QImage::Format_RGB32);
        const auto darkest = [](const QImage& image)
        {
            int result = 3 * 255;
            for (int y = pdf::PDFAppToolInputRow::PADDING; y < pdf::PDFAppToolInputRow::PADDING + pdf::PDFAppToolInputRow::NAME_HEIGHT; ++y)
            {
                for (int x = 48; x < 140; ++x)
                {
                    const QRgb pixel = image.pixel(x, y);
                    result = qMin(result, qRed(pixel) + qGreen(pixel) + qBlue(pixel));
                }
            }
            return result;
        };
        QVERIFY2(qAbs(darkest(damagedRow) - (0x47 + 0x55 + 0x69)) <= 12, qPrintable(QString::number(darkest(damagedRow))));
        QVERIFY2(qAbs(darkest(lockedRow) - (0x0f + 0x17 + 0x2a)) <= 12, qPrintable(QString::number(darkest(lockedRow))));
    }
    fixture.screenshot(QStringLiteral("pages-merge-problems"));

    // Remove file of the damaged and of the unsupported file; a file without pages does not
    // block Create PDF (1390:34070) and is not counted
    gui->getFilesPanel()->getRows()[3]->getLink()->click();
    AppToolOwnTabFixture::settle();
    gui->getFilesPanel()->getRows()[2]->getLink()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    gui->removeInput(list->getInput(1)->id);
    QCOMPARE(list->getCount(), 1);
    QVERIFY(!gui->isConfirmEnabled());
}

void AppToolMergeTest::restrictedFileTexts()
{
    // Owner 2026-11-08, 1390:34070: a restricted file (the sample forbids printing and copying,
    // bits 3 and 5 of Table 22) - the row says what is not allowed (1390:34148), the callout of
    // its group names the file after it (1390:34519)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1), AppDocumentSamples::ownerPassword(fixture.folder()) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const pdf::PDFAppToolInputList* list = tab->getInputList();
    QCOMPARE(list->getInput(1)->state, pdf::PDFAppToolInputState::Restricted);
    const QString name = list->getInput(1)->name;
    QCOMPARE(name, QStringLiteral("owner-password.pdf"));

    QCOMPARE(H::rowLines(gui, 1).mid(3), QStringList({ QStringLiteral("Restricted – enter the owner password"), QStringLiteral("Printing and copying are not allowed"), QStringLiteral("Enter owner password…") }));

    const std::vector<pdf::PDFAppToolMergePageModel::Group>& groups = gui->getCanvas()->getModel()->getGroups();
    QCOMPARE(groups.size(), size_t(2));
    QCOMPARE(groups[1].calloutTitle, QStringLiteral("Owner password required"));
    QCOMPARE(groups[1].calloutText, QStringLiteral("Printing and copying of owner-password.pdf are not allowed. The merged PDF is not encrypted and would lose these restrictions – enter the owner password, or remove the file."));
}

void AppToolMergeTest::passwordOfFile()
{
    // Enter password… opens 398:27726; the right password reads the pages without an undo
    // step, Remove File of the dialog removes the file by one undo step
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1), AppDocumentSamples::userPassword(fixture.folder()) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    pdf::PDFAppToolInputList* list = tab->getInputList();
    const QString lockedId = list->getInput(1)->id;

    gui->getFilesPanel()->getRows()[1]->getLink()->click();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppPasswordDialog* dialog = gui->getPasswordDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getInputId(), lockedId);
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getUserPassword());
    dialog->getUnlockButton()->click();
    QVERIFY(AppToolOwnTabFixture::waitFor([list, lockedId]() { return list->findInput(lockedId)->state == pdf::PDFAppToolInputState::Ready; }));
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getPasswordDialog());
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("B1"), QStringLiteral("B2") }));
    QVERIFY(!H::model(tab)->canUndo());
    QVERIFY(gui->isConfirmEnabled());

    // A second locked file: Remove File of the dialog removes it by one undo step (R12: the
    // dialog never removes it itself)
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("second")));
    gui->addFiles({ AppDocumentSamples::userPassword(QDir(fixture.folder()).filePath(QStringLiteral("second"))) });
    QVERIFY(H::waitLoaded(list));
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 3);
    const QString secondId = list->getInput(2)->id;
    gui->openPasswordDialog(secondId);
    QVERIFY(gui->getPasswordDialog());
    gui->getPasswordDialog()->getRemoveButton()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QVERIFY(list->isParked(secondId));
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 3);
    QCOMPARE(list->findInput(secondId)->state, pdf::PDFAppToolInputState::PasswordRequired);
}

void AppToolMergeTest::detailsView()
{
    // pages-merge-details 408:32003: the columns, the rows, the shared selection, the status
    AppToolOwnTabFixture fixture;
    const QString image = H::png(fixture.folder(), QStringLiteral("photo.png"), QSize(1200, 600), 150);
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2), image });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    gui->getCanvas()->getViewSwitch()->setCurrentIndex(1);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getCanvas()->isDetailsShown());
    QCOMPARE(H::status(tab), QStringLiteral("Merge · 2 files · 3 pages · Details view"));
    QVERIFY(!gui->getCanvas()->getTileSizeButton()->isEnabled());

    const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();
    QStringList headers;
    for (int column = 0; column < model->columnCount(); ++column)
    {
        headers << model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    }
    QCOMPARE(headers, QStringList({ QStringLiteral("Page"), QStringLiteral("Source"), QStringLiteral("Type"), QStringLiteral("Orig. page"), QStringLiteral("Size"), QStringLiteral("Orientation"), QStringLiteral("Rotation") }));
    QStringList second;
    QStringList third;
    for (int column = 0; column < model->columnCount(); ++column)
    {
        second << model->data(model->index(1, column), Qt::DisplayRole).toString();
        third << model->data(model->index(2, column), Qt::DisplayRole).toString();
    }
    QCOMPARE(second, QStringList({ QStringLiteral("2"), QStringLiteral("A · Contract.pdf"), QStringLiteral("PDF page"), QStringLiteral("2"), QStringLiteral("106 × 141 mm"), QStringLiteral("Portrait"), QStringLiteral("None") }));
    // An image of 1200 x 600 px at 150 dpi: 8 x 4 in = 203 x 102 mm, one frame: no page number
    QCOMPARE(third, QStringList({ QStringLiteral("3"), QStringLiteral("B · photo.png"), QStringLiteral("Image"), QStringLiteral("–"), QStringLiteral("203 × 102 mm"), QStringLiteral("Landscape"), QStringLiteral("None") }));

    // The widths of 408:32003 and the shared selection
    pdf::PDFAppToolMergeDetailsView* details = gui->getCanvas()->getDetailsView();
    // The columns of 408:32003: the texts at 337, 381, 555 (the cells 40, 170, 150 and the gap 4),
    // every cell with the padding 10 of the row; Page 4 wider for its header "Page" with the
    // paddings (Source 4 narrower, Type at its place); the rows 27, the header 29 (8 + 13 + 8)
    QCOMPARE(details->header()->sectionSize(0), 48);
    QCOMPARE(details->header()->sectionSize(1), 170);
    QCOMPARE(details->header()->sectionSize(0) + details->header()->sectionSize(1), 555 - 337);
    QCOMPARE(details->header()->sectionSize(2), 154);
    AppToolOwnTabFixture::settle();
    QCOMPARE(details->header()->height(), 29);
    QCOMPARE(details->viewport()->geometry().top(), 29);
    QCOMPARE(details->visualRect(model->index(1, 0)).height(), 27);
    QCOMPARE(details->visualRect(model->index(2, 0)).top() - details->visualRect(model->index(1, 0)).top(), 27);
    // The card hugs the header and the rows, the note is 10 below it (408:32592)
    QCOMPARE(details->height(), details->header()->height() + 3 * 27);
    const QWidget* card = details->parentWidget();
    QCOMPARE(gui->getCanvas()->getDetailsNote()->geometry().top() - card->geometry().bottom() - 1, 10);
    // The selection only fills the row, the texts keep their colors (the Library row)
    QCOMPARE(details->getRowStyle(), pdf::PDFTreeView::RowStyle::Library);
    // A row has the number of its page, no picture
    QVERIFY(!model->data(model->index(0, 0), Qt::DecorationRole).isValid());
    QCOMPARE(details->selectionModel(), gui->getCanvas()->getPageView()->selectionModel());
    gui->selectRows({ 2 });
    QVERIFY(details->selectionModel()->isRowSelected(2, QModelIndex()));
    fixture.screenshot(QStringLiteral("pages-merge-details"));

    // Back to the thumbnails
    gui->getCanvas()->getViewSwitch()->setCurrentIndex(0);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::status(tab), QStringLiteral("Merge · 2 files · 3 pages · nothing is written until you choose Create PDF"));
}

void AppToolMergeTest::outlineAndOptimizeMenus()
{
    // 378:1879: Outline (Document parts, Join outlines, No outline) and Optimize (Off, Prefer
    // quality, Minimum size) write their parameters; the button, the checks and the right
    // panel follow; K23: an optimized size is not estimated
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QVERIFY(gui->getPanelRow(QStringLiteral("outline-document-parts"))->isChecked());

    gui->getPanelRow(QStringLiteral("outline-join-outlines"))->click();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("outline")).toString(), QStringLiteral("join-outlines"));
    QCOMPARE(gui->getAction(QStringLiteral("outline"))->text(), QStringLiteral("Outline: Joined"));
    QVERIFY(gui->getPanelRow(QStringLiteral("outline-join-outlines"))->isChecked());
    QVERIFY(!gui->getPanelRow(QStringLiteral("outline-document-parts"))->isChecked());
    QCOMPARE(gui->getResultValues()->getValue(1), QStringLiteral("2 document parts with their bookmarks"));
    gui->getPanelRow(QStringLiteral("outline-no-outline"))->click();
    QCOMPARE(gui->getResultValues()->getValue(1), QStringLiteral("None"));
    QCOMPARE(gui->getOutlineNote()->text(), QStringLiteral("No outline: the result has no bookmarks. The source files are never changed."));

    gui->getPanelRow(QStringLiteral("optimize-minimum-size"))->click();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("optimizeImages")).toString(), QStringLiteral("minimum-size"));
    QCOMPARE(gui->getAction(QStringLiteral("optimize"))->text(), QStringLiteral("Optimize: Minimum"));
    QCOMPARE(gui->getResultValues()->getValue(3), QStringLiteral("depends on the images"));

    // Each choice is one undo step; the texts of the rows are of the frame
    QCOMPARE(gui->getPanelRow(QStringLiteral("optimize-off"))->getDescription(), QStringLiteral("Images are stored exactly as they are"));
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAction(QStringLiteral("optimize"))->text(), QStringLiteral("Optimize: Off"));
}

void AppToolMergeTest::tileSizeOfPreview()
{
    // K11 / 1370:34102: Size in the bar of the preview, S / M / L, the setting shared by the
    // page grids (owner: Medium by default)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QToolButton* button = gui->getCanvas()->getTileSizeButton();
    QCOMPARE(button->text(), QStringLiteral("Size: Medium"));
    const QSize medium = gui->getCanvas()->getPageView()->visualRect(gui->getCanvas()->getModel()->index(0, 0)).size();
    QCOMPARE(medium, QSize(110 + 12, 150 + 28));

    // The panel opens below its button in the bar of the preview, the button is Open (checked)
    button->click();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = gui->getTileSizeMenu()->getPanel();
    QVERIFY(panel->isVisible());
    QVERIFY(button->isChecked());
    QVERIFY(panel->mapToGlobal(QPoint(0, 0)).y() >= button->mapToGlobal(QPoint(0, button->height())).y() - 20);
    QVERIFY(gui->getTileSizeMenu()->getRow(pdf::PDFAppToolPageGrid::TileSize::Medium)->isChecked());
    gui->getTileSizeMenu()->getRow(pdf::PDFAppToolPageGrid::TileSize::Large)->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(!panel->isVisible());
    QVERIFY(!button->isChecked());
    AppToolOwnTabFixture::settle();
    QCOMPARE(button->text(), QStringLiteral("Size: Large"));
    QCOMPARE(gui->getCanvas()->getPageView()->visualRect(gui->getCanvas()->getModel()->index(0, 0)).size(), QSize(146 + 12, 200 + 28));
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::toolsPageGridSize()), QStringLiteral("large"));
}

void AppToolMergeTest::dropAndShortcutsAddFiles()
{
    // "Drop them anywhere in this window" (378:1657, K8): a drop on the tab adds PDF files and
    // images; Ctrl+I opens the files, Ctrl+V pastes an image; K29: a file twice is refused
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    pdf::PDFAppToolInputList* list = tab->getInputList();
    const QString pdfPath = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("dropped.pdf"), 2);
    const QString imagePath = H::png(fixture.folder(), QStringLiteral("dropped.png"), QSize(50, 50), 72);

    QWidget* target = tab->getToolPage()->getTabView();
    QVERIFY(target->acceptDrops());
    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(pdfPath), QUrl::fromLocalFile(imagePath) });
    QDragEnterEvent enter(QPoint(400, 400), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(400, 400), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    QVERIFY(H::waitLoaded(list));
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(list->getInput(0)->name, QStringLiteral("dropped.pdf"));
    QCOMPARE(list->getInput(1)->kind, pdf::PDFAppToolInputKind::Image);

    // Ctrl+I: the dialog of the files gives the same PDF again - refused, said in the status
    pdf::PDFAppFileDialogs::setOpenFilesHandler([pdfPath](QWidget*, const QString&) { return QStringList(pdfPath); });
    target->activateWindow();
    gui->getCanvas()->getPageView()->setFocus();
    QTest::keyClick(gui->getCanvas()->getPageView(), Qt::Key_I, Qt::ControlModifier);
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 2);
    QCOMPARE(tab->getToolPage()->getTabView()->getStatusBar()->getMessage(), QStringLiteral("dropped.pdf is already in the list"));

    // Ctrl+V: the image of the clipboard
    QImage pasted(30, 20, QImage::Format_RGB32);
    pasted.fill(Qt::red);
    QApplication::clipboard()->setImage(pasted);
    QTest::keyClick(gui->getCanvas()->getPageView(), Qt::Key_V, Qt::ControlModifier);
    QVERIFY(H::waitLoaded(list));
    AppToolOwnTabFixture::settle();
    QCOMPARE(list->getCount(), 3);
    QCOMPARE(list->getInput(2)->kind, pdf::PDFAppToolInputKind::Clipboard);
    QCOMPARE(H::pages(gui).size(), 4);
}

void AppToolMergeTest::runParametersAndEnvironment()
{
    // R16: the run gets only the usable inputs - the documents and the images of the context
    // in the order of the list, the plan with their letters among them
    AppToolOwnTabFixture fixture;
    const QString empty = H::file(fixture.folder(), QStringLiteral("garbage.pdf"), QByteArrayLiteral("%PDF-1.7\nnothing\n"));
    const QString image = H::png(fixture.folder(), QStringLiteral("c.png"), QSize(40, 40), 72);
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2), empty, image });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const pdf::PDFAppToolInputList* list = tab->getInputList();
    gui->selectRows({ 0 });
    gui->rotateRight();

    const QVariantMap parameters = gui->getRunParameters();
    QCOMPARE(parameters.value(QStringLiteral("inputs")).toString(), QStringLiteral("%1,%2").arg(list->getInput(0)->id, list->getInput(2)->id));
    QCOMPARE(parameters.value(QStringLiteral("plan")).toString(), QStringLiteral("A1@90,A2,B1"));
    QCOMPARE(parameters.value(QStringLiteral("outline")).toString(), QStringLiteral("document-parts"));

    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    QCOMPARE(environment.additionalDocuments.size(), size_t(1));
    QCOMPARE(environment.additionalDocuments.front().inputId, list->getInput(0)->id);
    QCOMPARE(environment.images.size(), size_t(1));
    QCOMPARE(environment.images.front().inputId, list->getInput(2)->id);

    // The context of the runner has the images (R16) and the core accepts the parameters
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolRunner::createContext(pdf::PDFAppDocumentSnapshot(), environment);
    QCOMPARE(context.images.size(), size_t(1));
    const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.merge"));
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool->getParameters(), parameters, &normalized).empty());
    QVERIFY(tool->validate(context, normalized).empty());
}

void AppToolMergeTest::runWithoutPagesOfFile()
{
    // A file, whose pages were all removed in the preview, stays in the list and is no input
    // of the run (R16): the context of the core has exactly the inputs of the parameter
    // "inputs" - the core refuses an input of the context, which its order does not name
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("cover.pdf"), 1),
                                                 AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 1) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const pdf::PDFAppToolInputList* list = tab->getInputList();

    // The page of the cover (B1, the third page) goes
    gui->selectRows({ 2 });
    gui->removeSelection();
    QCOMPARE(H::pages(gui), QStringList({ QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("C1") }));
    QCOMPARE(list->getCount(), 3);
    QVERIFY(gui->isConfirmEnabled());

    const QVariantMap parameters = gui->getRunParameters();
    QCOMPARE(parameters.value(QStringLiteral("inputs")).toString(), QStringLiteral("%1,%2").arg(list->getInput(0)->id, list->getInput(2)->id));
    // The plan of the run names its inputs by their letters among the inputs of the run (a run
    // of pages as a range, tools.md pages.merge)
    QCOMPARE(parameters.value(QStringLiteral("plan")).toString(), QStringLiteral("A1-2,B1"));

    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    QCOMPARE(environment.additionalDocuments.size(), size_t(2));
    QCOMPARE(environment.additionalDocuments[0].inputId, list->getInput(0)->id);
    QCOMPARE(environment.additionalDocuments[1].inputId, list->getInput(2)->id);

    // The core runs it: 3 pages, the cover is not among them
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolRunner::createContext(pdf::PDFAppDocumentSnapshot(), environment);
    const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.merge"));
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool->getParameters(), parameters, &normalized).empty());
    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool->validate(context, normalized);
    QVERIFY2(errors.empty(), errors.empty() ? "" : qPrintable(errors.front().message));
    const pdf::PDFDocumentToolResult result = tool->run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().document->getCatalog()->getPageCount(), size_t(3));
}

void AppToolMergeTest::createPdfOpensNewTab()
{
    // 378:3038 / 378:3674 / Q9: Create PDF runs the core, the dialog offers the new document,
    // Open Document opens it in a new tab after the tab of Merge, which closes
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex.pdf"), 3) });
    QVERIFY(tab);
    QPointer<pdf::PDFAppToolTab> tabGuard(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    gui->selectRows({ 4 });
    gui->rotateRight();

    // The texts of the run (378:3038)
    QCOMPARE(gui->getRunningCardTitle(40, QStringLiteral("Joining the files")), QStringLiteral("Creating the PDF"));
    QCOMPARE(gui->getRunningStatusText(QString()), QStringLiteral("Creating the PDF · Stop ends the operation, no file has been written yet"));
    QVERIFY(gui->getRunningTimeText(30).isEmpty());

    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    const int tabCount = int(fixture.window->getTabs().size());
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    AppToolOwnTabFixture::screenshotDialog(dialog, QStringLiteral("pages-merge-done"));
    // 378:3674: the body does not scroll in the window of the frame (1440 x 960)
    QVERIFY2(!dialog->getScrollArea()->verticalScrollBar()->isVisible(), qPrintable(QStringLiteral("maximum %1, body %2, area %3").arg(dialog->getScrollArea()->verticalScrollBar()->maximum()).arg(dialog->getScrollArea()->widget()->height()).arg(dialog->getScrollArea()->viewport()->height())));
    // Nothing can be changed below the dialog
    QVERIFY(!gui->getCanvas()->getPageView()->isEditable());
    dialog->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tabGuard]() { return tabGuard.isNull(); }));
    AppToolOwnTabFixture::settle();
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount);
    pdf::PDFAppDocumentTab* result = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(result);
    QVERIFY(AppToolOwnTabFixture::waitFor([result]() { return result->getDocument()->isReady(); }));
    const pdf::PDFDocument* document = result->getDocument()->getDocument().data();
    QCOMPARE(document->getCatalog()->getPageCount(), size_t(5));
    // The last page turned by 90° (the sample pages have no /Rotate)
    QCOMPARE(document->getCatalog()->getPage(4)->getPageRotation(), pdf::PageRotation::Rotate90);
}

void AppToolMergeTest::partialResult()
{
    // 378:4376 (K36 corrected, Q5): an input, whose copy failed, is left out; the title of the
    // dialog, the status, the row "Left out – could not be copied", the Tag of its group, the
    // header of Files - while the dialog is open
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 3) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    const QString annexId = tab->getInputList()->getInput(1)->id;

    pdf::PDFAppToolResultInput input;
    pdf::PDFDocumentToolLeftOutInput leftOut;
    leftOut.inputId = annexId;
    leftOut.name = QStringLiteral("Annex_A.pdf");
    leftOut.pageCount = 3;
    input.result.leftOutInputs.push_back(leftOut);
    gui->fillResultInput(&input);
    QCOMPARE(input.title, QStringLiteral("Merged without 1 file"));
    QCOMPARE(input.statusText, QStringLiteral("Merge finished without 1 file · the new document is waiting for your decision"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::rowLines(gui, 1)[4], QStringLiteral("Left out – could not be copied"));
    QCOMPARE(gui->getFilesPanel()->getSubtitleLabel()->text(), QStringLiteral("2 files · 1 left out"));
    QCOMPARE(gui->getCanvas()->getModel()->getGroups()[1].tag, QStringLiteral("Left out"));

    // A complete result keeps the default texts
    pdf::PDFAppToolResultInput complete;
    gui->fillResultInput(&complete);
    QVERIFY(complete.title.isEmpty());
}

void AppToolMergeTest::themesRightToLeftAndScale()
{
    // Both themes without the leak color, right to left (the files at the right edge, the
    // tiles flow from the right), 1x and 2x
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture;
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            AppToolOwnTabFixture::settle();
            pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3),
                                                         AppDocumentSamples::userPassword(fixture.folder()),
                                                         H::png(fixture.folder(), QStringLiteral("p.png"), QSize(300, 400), 72) });
            QVERIFY(tab);
            PDFAppMergeToolGUI* gui = H::gui(tab);
            gui->selectRows({ 1 });
            AppToolOwnTabFixture::settle();

            pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = AppToolOwnTabFixture::render(view, ratio);
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(image), 0);
            }

            pdf::PDFAppToolMergePageView* pages = gui->getCanvas()->getPageView();
            const QRect first = pages->visualRect(gui->getCanvas()->getModel()->index(0, 0));
            const QRect second = pages->visualRect(gui->getCanvas()->getModel()->index(1, 0));
            QCOMPARE(second.x() > first.x(), direction == Qt::LeftToRight);
            const int filesX = gui->getFilesPanel()->mapTo(view, QPoint(0, 0)).x();
            QCOMPARE(filesX < 720, direction == Qt::LeftToRight);
            fixture.screenshot(QStringLiteral("pages-merge-%1-%2").arg(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark"),
                                                                       direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl")));
        }
    }
}

void AppToolMergeTest::screenshotsOfFrames()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the frames, which no other test writes, under their ids
    // (compared with Figma by the review): the menus 378:1879, the password dialogs 398:27726
    // and 1390:34569, the states 1390:34070, a chosen next tool 412:32654, the start pages
    // 531:3208 / 551:149074 and the window of 720 x 480
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    {
        AppToolOwnTabFixture fixture;
        pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6),
                                                     AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9),
                                                     H::png(fixture.folder(), QStringLiteral("signature_page.png"), QSize(2480, 3508), 300) });
        QVERIFY(tab);
        PDFAppMergeToolGUI* gui = H::gui(tab);
        for (const QString& name : { QStringLiteral("addFiles"), QStringLiteral("sort"), QStringLiteral("outline"), QStringLiteral("optimize") })
        {
            gui->getAction(name)->click();
            AppToolOwnTabFixture::settle();
            QVERIFY(gui->getPanel(name)->isVisible());
            fixture.screenshot(QStringLiteral("pages-merge-menus-378-1879-") + name);
            gui->getPanel(name)->close();
            AppToolOwnTabFixture::settle();
        }

        // 408:32003: Details with the third page selected
        gui->getCanvas()->getViewSwitch()->setCurrentIndex(1);
        gui->selectRows({ 2 });
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pages-merge-details-408-32003"));
        gui->getCanvas()->getViewSwitch()->setCurrentIndex(0);
        AppToolOwnTabFixture::settle();

        // 412:32654: a next tool chosen in the dialog of the result
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        shell->confirm();
        QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
        pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
        QVERIFY(dialog && dialog->getContinueWith());
        dialog->getContinueWith()->setSelectedChoice(0);
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pages-merge-done-continue-412-32654"));
        AppToolOwnTabFixture::screenshotDialog(dialog, QStringLiteral("pages-merge-done-continue-412-32654-dialog"));
    }

    {
        // 1390:34070 and the password dialogs: a wrong password (398:27726), the owner password
        // of a restricted file (1390:34569)
        AppToolOwnTabFixture fixture;
        pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6),
                                                     AppDocumentSamples::userPassword(fixture.folder()),
                                                     AppDocumentSamples::ownerPassword(fixture.folder()),
                                                     H::file(fixture.folder(), QStringLiteral("Appendix_old.pdf"), QByteArrayLiteral("%PDF-1.7\nthis is no document at all\n")),
                                                     H::file(fixture.folder(), QStringLiteral("notes.docx"), QByteArrayLiteral("PK not an image, not a PDF")) });
        QVERIFY(tab);
        PDFAppMergeToolGUI* gui = H::gui(tab);
        fixture.screenshot(QStringLiteral("pages-merge-states-1390-34070"));

        pdf::PDFAppToolInputList* list = tab->getInputList();
        gui->openPasswordDialog(list->getInput(1)->id);
        QVERIFY(gui->getPasswordDialog());
        QTest::keyClicks(gui->getPasswordDialog()->getPasswordField(), QStringLiteral("wrong-password"));
        gui->getPasswordDialog()->getUnlockButton()->click();
        QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getPasswordDialog() && !gui->getPasswordDialog()->getErrorText().isEmpty(); }));
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pages-merge-password-398-27726"));
        AppToolOwnTabFixture::screenshotDialog(gui->getPasswordDialog(), QStringLiteral("pages-merge-password-398-27726-dialog"));
        gui->getPasswordDialog()->reject();
        AppToolOwnTabFixture::settle();

        gui->openPasswordDialog(list->getInput(2)->id);
        QVERIFY(gui->getPasswordDialog());
        QTest::keyClicks(gui->getPasswordDialog()->getPasswordField(), QStringLiteral("password"));
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pages-merge-password-owner-1390-34569"));
        AppToolOwnTabFixture::screenshotDialog(gui->getPasswordDialog(), QStringLiteral("pages-merge-password-owner-1390-34569-dialog"));
        gui->getPasswordDialog()->reject();
        AppToolOwnTabFixture::settle();
    }

    {
        // 531:3208: two open documents checked and a file from the disk; 551:149074: one
        AppToolOwnTabFixture fixture;
        QVERIFY(fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6)));
        QVERIFY(fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9)));
        AppToolOwnTabFixture::settle();
        fixture.navigate(QStringLiteral("/tools/pages.merge"));
        AppToolOwnTabFixture::settle();
        pdf::PDFToolStartPage* page = fixture.page<pdf::PDFToolStartPage>();
        QVERIFY(page && page->getInputChooser());
        pdf::PDFAppToolInputChooser* chooser = page->getInputChooser();
        std::vector<pdf::PDFAppToolDocumentRow*> rows = chooser->getDocumentRows();
        QVERIFY(!rows.empty());
        rows[0]->click();
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pdf4qt-tool-start-multi-one-551-149074"));
        // A check rebuilds the rows
        rows = chooser->getDocumentRows();
        if (rows.size() > 1)
        {
            rows[1]->click();
        }
        chooser->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Appendix.pdf"), 4) });
        QVERIFY(AppToolOwnTabFixture::waitFor([chooser]() { return chooser->getInputList()->getLoadingCount() == 0; }));
        AppToolOwnTabFixture::settle();
        fixture.screenshot(QStringLiteral("pdf4qt-tool-start-multi-531-3208"));
    }

    {
        // The smallest window (720 x 480)
        AppToolOwnTabFixture fixture(QSize(720, 480));
        pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6),
                                                     AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 9) });
        QVERIFY(tab);
        fixture.screenshot(QStringLiteral("pages-merge-720x480"));
    }
}

void AppToolMergeTest::manyFilesKeepGuiResponsive()
{
    // Plan 6: Merge takes up to 1 000 files (PDFAppToolInputList::MAXIMUM_INPUTS). Their readings
    // finish one by one in the workers; the work of the GUI thread for one finished reading must
    // not grow with the number of the files - the longest stall of the event loop is measured
    // while the files are read (a timer of 0 ms records the gaps between its timeouts)
    static constexpr int FILE_COUNT = 600;
    AppToolOwnTabFixture fixture;
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("one.pdf"), 1);
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QFile::ReadOnly));
    const QByteArray bytes = sourceFile.readAll();
    QStringList files;
    for (int i = 0; i < FILE_COUNT; ++i)
    {
        files << H::file(fixture.folder(), QStringLiteral("file-%1.pdf").arg(i, 4, 10, QLatin1Char('0')), bytes);
    }

    pdf::PDFAppToolTab* tab = fixture.createToolTab(QString::fromLatin1(pdf::PDFDocumentToolMerge::TOOL_ID));
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    const pdf::PDFAppToolInputList* list = tab->getInputList();

    QElapsedTimer total;
    QElapsedTimer gap;
    qint64 longestGap = 0;
    QTimer ticker;
    ticker.setInterval(0);
    QObject::connect(&ticker, &QTimer::timeout, &ticker, [&gap, &longestGap]()
    {
        longestGap = qMax(longestGap, gap.restart());
    });

    total.start();
    gui->addFiles(files);
    const qint64 addTime = total.elapsed();
    QCOMPARE(list->getCount(), FILE_COUNT);
    gap.start();
    ticker.start();
    const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();
    QVERIFY(AppToolOwnTabFixture::waitFor([list, model]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0 && model->rowCount() == FILE_COUNT; }, 120000));
    ticker.stop();
    const qint64 loadTime = total.elapsed();
    QCOMPARE(list->getUsableCount(), FILE_COUNT);
    QCOMPARE(gui->getCanvas()->getModel()->getGroups().size(), size_t(FILE_COUNT));
    qInfo("%d files: added in %lld ms, read in %lld ms, the longest stall of the GUI %lld ms", FILE_COUNT, addTime, loadTime, longestGap);

    // The GUI stays responsive: no stall of the event loop longer than a fraction of a second
    // (a slow machine or a sanitizer scales the limit)
    const qint64 factor = qMax<qint64>(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(longestGap < 250 * factor, qPrintable(QStringLiteral("the GUI stalled for %1 ms").arg(longestGap)));
}

void AppToolMergeTest::largeInput()
{
    // Plan 6: a large input is laid out without a widget per page, only the visible tiles ask
    // for their pictures, the layout of the groups is quick
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("big.pdf"), 3000), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("small.pdf"), 1) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QCOMPARE(gui->getCanvas()->getModel()->rowCount(), 3001);
    pdf::PDFAppToolMergePageView* view = gui->getCanvas()->getPageView();
    QVERIFY(view->getContentHeight() > view->viewport()->height() * 10);
    QVERIFY(gui->getCanvas()->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_PAGES);

    // A sort of 3001 pages and a reversal are one step each and quick
    const qint64 factor = qMax<qint64>(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QElapsedTimer timer;
    timer.start();
    gui->sortByPageNumber();
    gui->reverseOrder();
    AppToolOwnTabFixture::settle();
    QVERIFY2(timer.elapsed() < 5000 * factor, qPrintable(QString::number(timer.elapsed())));
    // The runs reversed: the pages 2 - 3000 of the large file (now B), the small file, page 1
    QCOMPARE(H::pages(gui).first(), QStringLiteral("B2"));
    QCOMPARE(H::pages(gui).last(), QStringLiteral("B1"));

    // The view scrolls to the end and paints without asking for more than the visible pictures
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getCanvas()->getScheduler()->getMaximumPendingCount() <= pdf::PDFAppThumbnailScheduler::MAXIMUM_PENDING_PAGES);
}

void AppToolMergeTest::deleteTabInEveryState_data()
{
    QTest::addColumn<QString>("state");
    QTest::newRow("loading") << QStringLiteral("loading");
    QTest::newRow("password") << QStringLiteral("password");
    QTest::newRow("menu") << QStringLiteral("menu");
    QTest::newRow("running") << QStringLiteral("running");
    QTest::newRow("deciding") << QStringLiteral("deciding");
}

void AppToolMergeTest::deleteTabInEveryState()
{
    // The tab goes in each state: nothing of it is used after (ASan), the window works on
    QFETCH(QString, state);
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 40), AppDocumentSamples::userPassword(fixture.folder()) });
    QVERIFY(tab);
    PDFAppMergeToolGUI* gui = H::gui(tab);
    QPointer<pdf::PDFAppToolTab> tabGuard(tab);

    if (state == QLatin1String("loading"))
    {
        gui->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("late.pdf"), 200) });
    }
    else if (state == QLatin1String("password"))
    {
        gui->openPasswordDialog(tab->getInputList()->getInput(1)->id);
        QVERIFY(gui->getPasswordDialog());
    }
    else if (state == QLatin1String("menu"))
    {
        gui->getAction(QStringLiteral("sort"))->click();
        AppToolOwnTabFixture::settle();
    }
    else
    {
        gui->removeInput(tab->getInputList()->getInput(1)->id);
        gui->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2) });
        QVERIFY(H::waitLoaded(tab->getInputList()));
        AppToolOwnTabFixture::settle();
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        shell->confirm();
        if (state == QLatin1String("deciding"))
        {
            QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
        }
    }

    tab->setCloseConfirmed(true);
    fixture.window->closeTab(tab);
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tabGuard]() { return tabGuard.isNull(); }));
    AppToolOwnTabFixture::settle();
    QVERIFY(fixture.window->getCurrentTab());
}

void AppToolMergeTest::mcpMergeOverHttp()
{
    // Q11: an assistant merges open documents by documentIds over HTTP with the real tool;
    // the outline of the call is used; the result opens in a new tab
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.startServer());
    fixture.setTransport(pdftest::AppMcpTransport::Http);

    pdf::PDFAppDocumentTab* first = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("first.pdf"), 3));
    pdf::PDFAppDocumentTab* second = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second.pdf"), 2));
    QVERIFY(first && second);
    QVERIFY(AppToolOwnTabFixture::waitFor([first, second]() { return first->getDocument()->isReady() && second->getDocument()->isReady(); }));

    QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("merge") }, { QStringLiteral("result"), QStringLiteral("open_new_tab") },
                           { QStringLiteral("documentIds"), QJsonArray({ second->getDocument()->getId(), first->getDocument()->getId() }) },
                           { QStringLiteral("merge"), QJsonObject({ { QStringLiteral("outline"), QStringLiteral("no-outline") } }) } };
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
    QVERIFY(result);
    QVERIFY2(!result->isError, result->result.constData());
    const QJsonObject answer = QJsonDocument::fromJson(result->result).object();
    const QString newId = answer.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("merge")).toObject().value(QStringLiteral("documentId")).toString();
    QVERIFY(!newId.isEmpty());
    pdf::PDFAppDocumentTab* newTab = nullptr;
    for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
    {
        if (tab->getDocument() && tab->getDocument()->getId() == newId)
        {
            newTab = tab;
        }
    }
    QVERIFY(newTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
    QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(5));
    // No outline: the result has no bookmarks (12.3.3)
    QVERIFY(!newTab->getDocument()->getDocument()->getCatalog()->getOutlineRootPtr() ||
            newTab->getDocument()->getDocument()->getCatalog()->getOutlineRootPtr()->getChildCount() == 0);

    // The plan of the panel is no parameter of an assistant
    arguments.insert(QStringLiteral("merge"), QJsonObject({ { QStringLiteral("plan"), QStringLiteral("A1") } }));
    result = fixture.callTool(QStringLiteral("pages"), arguments);
    QVERIFY(result);
    QVERIFY(result->isError);
}

QTEST_MAIN(AppToolMergeTest)

#include "tst_apptoolmergetest.moc"
