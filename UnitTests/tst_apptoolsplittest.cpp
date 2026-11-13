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

// Split and Extract Pages in the application (package A of step 3a of phase 2, plan 8): the texts
// of the frames 383:3315, 383:9088, 383:11374, 403:29978, 385:9405, 385:11029; the written files
// (R3) and the stopped run, the question of an existing file (356:11997, R3), a source never
// overwritten (R14), Delete Written Files (K46). The expected values are the texts and the numbers
// of the frames.

#include "apptoolshellfixture.h"
#include "mergetestpdf.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolfilesink.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfsettingdefinitions.h"

#include "pdfaccenthue.h"
#include "pdfcatalog.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleOptionViewItem>
#include <QThread>
#include <QToolButton>
#include <QtTest>

using pdf::PDFDocumentToolShell;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;
using mergetest::MergeTestPdf;

/// Helpers of the test
class SplitGuiHelper
{
public:
    SplitGuiHelper() = delete;

    static inline const QString SPLIT_ID = QStringLiteral("pages.split");
    static inline const QString EXTRACT_ID = QStringLiteral("pages.extract");

    static QString writePages(const QString& filePath, int pageCount)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, 595, 842));
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

    static void setValue(PDFDocumentToolShell* shell, const char* name, const QVariant& value)
    {
        shell->getModel()->setValue(QString::fromLatin1(name), value);
        AppToolShellFixture::settle();
    }

    /// The descriptions of the options of an Enum parameter (the rows of its menu)
    static QStringList optionDescriptions(PDFDocumentToolShell* shell, const char* name)
    {
        QStringList descriptions;
        const pdf::PDFDocumentToolParameterDefinition* definition = pdf::PDFDocumentToolParameters::find(shell->getModel()->getDefinitions(), QString::fromLatin1(name));
        for (int i = 0; definition && i < int(definition->getOptions().size()); ++i)
        {
            descriptions << definition->getOptions()[size_t(i)].getDescription();
        }
        return descriptions;
    }

    static QStringList factKeys(const pdf::PDFKeyValueList* list)
    {
        QStringList keys;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            keys << list->getKey(i);
        }
        return keys;
    }
};

class AppToolSplitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void registered();
    void splitFrame();
    void splitDuplicateNames();
    void splitOutputFilesPanel();
    void splitAtPagesClick();
    void splitBookmarks();
    void splitBySize();
    void splitWrittenMarks();
    void tileSizeDefaults();
    void gridRowsAndGroups();
    void splitWrittenFilesDialog();
    void splitConflictQuestion();
    void splitSourceNeverOverwritten();
    void splitStoppedDialog();
    void splitQuestionAgainAfterStop();
    void extractFrame();
    void extractPerPage();
    void themesDirectionAndScreenshots();
    void pageListTexts();
};

void AppToolSplitTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolSplitTest::registered()
{
    AppToolShellFixture fixture;
    for (const QString& toolId : { SplitGuiHelper::SPLIT_ID, SplitGuiHelper::EXTRACT_ID })
    {
        QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
    }
}

void AppToolSplitTest::splitFrame()
{
    // 383:3315: Handbook 48 pages, Every N pages, 12 per file - "Split into 4 Files", the files
    // Handbook_1.pdf ... Handbook_4.pdf of "Pages 1–12" ..., "4 files will be created"
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);

    // Pages to split "All" at the entry (no selection of the thumbnails)
    QCOMPARE(gui->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::All);
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-n-pages"));
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 12);

    QCOMPARE(gui->getFiles().size(), size_t(4));
    QCOMPARE(gui->getFiles()[3].fileName, QStringLiteral("Handbook_4.pdf"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Split into 4 Files"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getGrid()->getHeaderText(), QStringLiteral("4 files will be created"));
    QCOMPARE(gui->getPendingModel()->rowCount(), 4);
    QCOMPARE(gui->getPendingModel()->data(gui->getPendingModel()->index(1, 0), Qt::DisplayRole).toString(), QStringLiteral("Handbook_2.pdf"));

    // The preview of the names (383:3315 "Handbook_1.pdf … Handbook_4.pdf", "4 files · # is
    // replaced by the file number") and the summary "Every N pages · 12"
    QCOMPARE(gui->getOutputFiles()->getPreview()->text(), QStringLiteral("Handbook_1.pdf … Handbook_4.pdf"));
    QCOMPARE(gui->getOutputFiles()->getPreviewHint()->text(), QStringLiteral("4 files · # is replaced by the file number"));
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("Every N pages · 12"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("4"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Split · Handbook.pdf · 48 pages → 4 files · the open document is not changed"));

    // M13, tb-count 383:3315: the Toolbar field "12 pages per file" - no arrows (a line edit),
    // 32 high, as wide as its value and suffix (125 in the frame); Thumbnails: Small (owner
    // 2026-11-09: the default of Split without a choice of the user)
    pdf::PDFAppToolNumberField* field = gui->getPagesPerFileField();
    QVERIFY(field && field->isVisible());
    QCOMPARE(field->text(), QStringLiteral("12"));
    QCOMPARE(field->getSuffix(), QStringLiteral("pages per file"));
    QCOMPARE(field->height(), 32);
    QVERIFY2(std::abs(field->width() - 125) <= 3, qPrintable(QString::number(field->width())));
    QVERIFY(!gui->getSizeField()->isVisible() && !gui->getClearCutsButton()->isVisible());
    QCOMPARE(gui->getTileSizeButton()->text(), QStringLiteral("Thumbnails: Small"));

    // A value typed into the field is one undo step; a text, which is no number, goes back
    field->setText(QStringLiteral("16"));
    field->commit();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getFiles().size(), size_t(3));
    field->setText(QStringLiteral("many"));
    field->commit();
    QCOMPARE(field->text(), QStringLiteral("16"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(field->text(), QStringLiteral("12"));
    QCOMPARE(gui->getFiles().size(), size_t(4));

    // 383:3315 stage: a card per file 774 wide (the canvas 814 - 2 x 20), "Handbook_1.pdf",
    // "Pages 1–12", the tiles 85 x 128 eight in a line 8 apart: the card of 12 pages is 8 + 1 +
    // 15 + 8 + 2 x 128 + 8 + 10 + 1 = 307 high, the next card 10 below it
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(grid->isGrouped());
    QVERIFY(grid->getGroupView()->isVisible() && !grid->getView()->isVisible());
    QVERIFY(!grid->getHintLabel()->isVisible());
    const std::vector<pdf::PDFAppToolGroupedPageModel::Group>& groups = grid->getModel()->getGroups();
    QCOMPARE(groups.size(), size_t(4));
    QCOMPARE(groups[0].title, QStringLiteral("Handbook_1.pdf"));
    QCOMPARE(groups[1].subtitle, QStringLiteral("Pages 13–24"));
    QCOMPARE(groups[1].iconName, QStringLiteral("file"));
    QCOMPARE(groups[1].firstRow, 12);
    QCOMPARE(grid->getModel()->getPageOfRow(13), 13);
    QCOMPARE(grid->getModel()->getGroupOfRow(13), 1);
    pdf::PDFAppToolMergePageView* cards = grid->getGroupView();
    QCOMPARE(cards->getGroupRect(0).size(), QSize(774, 307));
    QCOMPARE(cards->getGroupRect(1).top() - cards->getGroupRect(0).bottom() - 1, 10);
    QCOMPARE(cards->getTileSize(0), QSize(85, 128));
    const QRect tile1 = cards->visualRect(grid->getModel()->index(0));
    const QRect tile2 = cards->visualRect(grid->getModel()->index(1));
    const QRect tile9 = cards->visualRect(grid->getModel()->index(8));
    QCOMPARE(tile2.left() - tile1.right() - 1, 8);
    QCOMPARE(tile9.left(), tile1.left());
    QCOMPARE(tile9.top() - tile1.bottom() - 1, 8);
    const QRect cardInWindow(cards->viewport()->mapTo(fixture.window, cards->getGroupRect(0).topLeft()), cards->getGroupRect(0).size());
    const QRect canvasInWindow(grid->mapTo(fixture.window, QPoint(0, 0)), grid->size());
    QCOMPARE(cardInWindow.left() - canvasInWindow.left(), 20);
    QCOMPARE(cardInWindow.top() - canvasInWindow.top(), 20 + 36 + 12);
    QCOMPARE(tile1.topLeft() - cards->getGroupRect(0).topLeft(), QPoint(11, 9 + 15 + 8));

    // Files to create: the row of the selected file has the accent tile, the others the muted one
    // (383:6771); a click on a row shows the card of its file
    pdf::PDFAppToolPendingPagesModel* rows = gui->getPendingModel();
    QCOMPARE(rows->data(rows->index(0, 0), pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Accent));
    QCOMPARE(rows->data(rows->index(2, 0), pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Muted));
    QVERIFY(gui->getPendingList()->getView()->selectionModel()->isRowSelected(0, QModelIndex()));
    QListView* fileView = gui->getPendingList()->getView();
    QTest::mouseClick(fileView->viewport(), Qt::LeftButton, Qt::NoModifier, fileView->visualRect(rows->index(3, 0)).center());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedFile(), 3);
    QCOMPARE(rows->data(rows->index(3, 0), pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Accent));
    QCOMPARE(rows->data(rows->index(0, 0), pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Muted));
    QVERIFY(fileView->selectionModel()->isRowSelected(3, QModelIndex()));
    QVERIFY(cards->verticalScrollBar()->value() > 0);
}

void AppToolSplitTest::splitDuplicateNames()
{
    // 383:9088: "Handbook" without a token - every file would be named Handbook.pdf, the Primary
    // button is disabled, the red callout says it
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 8));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    // 8 pages of 2 per file: four files (the default 10 per file would give only one)
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 2);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME, QStringLiteral("Handbook"));
    QCOMPARE(gui->getFilesError(), QStringLiteral("Every file would be named Handbook.pdf"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    // K53, 383:9088: the Primary button keeps its number, disabled
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Split into 4 Files"));

    // 383:9088 callout-red at the top of the right panel, above the section Output files
    pdf::PDFAppToolOutputFilesSection* output = gui->getOutputFiles();
    pdf::PDFAppToolCallout* callout = output->getErrorCallout();
    QVERIFY(callout->isVisible());
    QVERIFY(!output->getContainer()->isAncestorOf(callout));
    QVERIFY(callout->mapTo(fixture.window, QPoint(0, callout->height())).y() < output->getContainer()->mapTo(fixture.window, QPoint(0, 0)).y());
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("Every file would be named Handbook.pdf"));

    // 383:9088: the text of the callout says the remedy, the status bar the cause
    QCOMPARE(callout->getTextLabel()->text(), QStringLiteral("Add # to the file name so that each file gets its own number, for example Handbook_#."));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Split · the file name template gives the same name to every file"));

    // 383:9088 names: "Handbook.pdf × 4" in text/danger, "The names must differ"
    QVERIFY(output->getPreview()->isVisible());
    QCOMPARE(output->getPreview()->text(), QStringLiteral("Handbook.pdf × 4"));
    QCOMPARE(output->getPreview()->getTextColor(), pdf::PDFThemeColors::TextDanger);
    QCOMPARE(output->getPreviewHint()->text(), QStringLiteral("The names must differ"));

    // The rows of the files (59 high) and the cards say it: "Same name as another file" in
    // text/danger, the Tag "Duplicate name"
    pdf::PDFAppToolPendingPagesModel* rows = gui->getPendingModel();
    for (int i = 0; i < 4; ++i)
    {
        QCOMPARE(rows->data(rows->index(i, 0), pdf::PDFAppToolPendingPagesModel::StatusRole).toString(), QStringLiteral("Same name as another file"));
        QCOMPARE(rows->data(rows->index(i, 0), pdf::PDFAppToolPendingPagesModel::StatusColorRole).toInt(), int(pdf::PDFThemeColors::TextDanger));
        QCOMPARE(gui->getGrid()->getModel()->getGroups()[size_t(i)].tag, QStringLiteral("Duplicate name"));
    }
    QListView* fileView = gui->getPendingList()->getView();
    QCOMPARE(fileView->visualRect(rows->index(0, 0)).height(), 59);
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-9088-pages-split-invalid"));

    // A template with # again: no mark, the rows are 46 high
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME, QStringLiteral("Handbook_#"));
    QVERIFY(!callout->isVisible());
    QCOMPARE(output->getPreview()->getTextColor(), pdf::PDFThemeColors::TextPrimary);
    QVERIFY(rows->data(rows->index(0, 0), pdf::PDFAppToolPendingPagesModel::StatusRole).toString().isEmpty());
    QVERIFY(gui->getGrid()->getModel()->getGroups()[0].tag.isEmpty());
    QCOMPARE(fileView->visualRect(rows->index(0, 0)).height(), 46);
}

void AppToolSplitTest::splitOutputFilesPanel()
{
    // 383:3315 section-Output files: the header with the link Name tokens, the card of 287 with
    // the padding 12 + border - Folder (the field and Browse… 6 apart), File name, the names in
    // their box, Existing files with its combo box of 140 in one row; nothing of the right panel
    // reaches over its edge (the controls of the settings are 260 wide: the panel used to be
    // wider than 320 and cut Browse… and the values of Summary)
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 12);

    pdf::PDFAppToolOutputFilesSection* output = gui->getOutputFiles();
    QWidget* panel = shell->getRightPanel();
    const QRect panelRect(panel->mapTo(fixture.window, QPoint(0, 0)), panel->size());
    QWidget* card = output->getContainer()->findChild<QWidget*>(QStringLiteral("toolOutputCard"));
    QVERIFY(card);
    const QRect cardRect(card->mapTo(fixture.window, QPoint(0, 0)), card->size());
    const QRect browseRect(output->getBrowseButton()->mapTo(fixture.window, QPoint(0, 0)), output->getBrowseButton()->size());
    const QRect folderRect(output->getFolderField()->mapTo(fixture.window, QPoint(0, 0)), output->getFolderField()->size());
    QCOMPARE(cardRect.width(), 287);
    QCOMPARE(panelRect.right() - cardRect.right(), 16);
    QCOMPARE(cardRect.right() - browseRect.right(), 13);
    QCOMPARE(browseRect.left() - folderRect.right() - 1, 6);
    QCOMPARE(folderRect.left() - cardRect.left(), 13);
    // 383:4410: a long folder ends with "…" without the focus, its start stays visible
    const pdf::PDFLineEdit* folderEdit = qobject_cast<const pdf::PDFLineEdit*>(output->getFolderField());
    QVERIFY(folderEdit && folderEdit->isElidedWhenInactive());
    QWidget* existing = output->getContainer()->findChild<QWidget*>(QStringLiteral("toolOutputExistingFiles"));
    QVERIFY(existing);
    QCOMPARE(existing->width(), 140);
    QCOMPARE(cardRect.right() - existing->mapTo(fixture.window, QPoint(existing->width() - 1, 0)).x(), 13);
    QWidget* existingLabel = output->getContainer()->findChild<QWidget*>(QStringLiteral("toolOutputExistingLabel"));
    QVERIFY(existingLabel);
    QVERIFY(std::abs(existingLabel->mapTo(fixture.window, existingLabel->rect().center()).y() - existing->mapTo(fixture.window, existing->rect().center()).y()) <= 1);
    const QRect summaryRect(gui->getSummary()->mapTo(fixture.window, QPoint(0, 0)), gui->getSummary()->size());
    QVERIFY2(summaryRect.right() < panelRect.right() - 16, qPrintable(QStringLiteral("%1 %2").arg(summaryRect.right()).arg(panelRect.right())));

    // The rows of Summary are 36 apart (383:3315)
    QCOMPARE(gui->getSummary()->getRowRect(1).top() - gui->getSummary()->getRowRect(0).top(), 36);

    // 383:4447 panel-tokens: a row per token; @ goes to the cursor of File name (its end here)
    QCOMPARE(output->getTokensLink()->text(), QStringLiteral("Name tokens"));
    output->onTokensClicked();
    AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* tokens = output->getTokensPanel();
    QVERIFY(tokens);
    QVERIFY(tokens->isVisible());
    QStringList titles;
    QStringList descriptions;
    for (const QPointer<pdf::PDFTileRow>& row : tokens->getRows())
    {
        titles << row->getTitle();
        descriptions << row->getDescription();
    }
    QCOMPARE(titles, QStringList({ QStringLiteral("#"), QStringLiteral("@"), QStringLiteral("{source_base}"), QStringLiteral("{group_name}"), QStringLiteral("{date}") }));
    QCOMPARE(descriptions.mid(0, 4), QStringList({ QStringLiteral("File number · ## pads with zeros (01, 02)"), QStringLiteral("Number of the first page in the file"),
                                                  QStringLiteral("Name of the source file without .pdf"), QStringLiteral("Bookmark title or group name") }));
    const QString before = shell->getModel()->getValue(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString();
    output->getNameField()->setCursorPosition(int(before.size()));
    tokens->getRows().at(1)->click();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString(), before + QStringLiteral("@"));
    QVERIFY(!tokens->isVisible());

    // 383:4447: the descriptions of the menus Split, Optimize and Existing files
    const QStringList rules({ QStringLiteral("One file per page"), QStringLiteral("Files with the same number of pages"), QStringLiteral("Click between pages to place the cuts"),
                              QStringLiteral("A new file starts at each chosen bookmark"), QStringLiteral("Files of about the size you enter"), QStringLiteral("Two files – odd pages and even pages") });
    QCOMPARE(H::optionDescriptions(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE), rules);
    QCOMPARE(H::optionDescriptions(shell, pdf::PDFDocumentToolSplit::PARAMETER_OPTIMIZE_IMAGES),
             QStringList({ QStringLiteral("Images are stored exactly as they are"), QStringLiteral("Downsamples large images, keeps detail"), QStringLiteral("Smallest file, stronger compression") }));
    QCOMPARE(H::optionDescriptions(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES),
             QStringList({ QStringLiteral("Decide when the first conflict is found"), QStringLiteral("Replace the existing files"), QStringLiteral("Keep the existing files, write the rest"), QStringLiteral("Add (2), (3) … to the new files") }));
}

void AppToolSplitTest::splitAtPagesClick()
{
    // 383:5715: At selected pages - a click on the tile of page 10 starts a new file there, a
    // second click removes the cut; the cuts are one undo step each
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("at-pages"));
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_CUTS, QStringLiteral("20, 35"));
    gui->clickPage(9, Qt::NoModifier);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("10, 20, 35"));
    QCOMPARE(gui->getFiles().size(), size_t(4));
    QCOMPARE(gui->getFiles()[0].pages.back(), pdf::PDFInteger(8));
    gui->clickPage(19, Qt::NoModifier);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("10, 35"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("10, 20, 35"));

    // The marks of the tiles (K45, 383:5715): the cut before page 10, none before page 9; the
    // dot of the file of each page - pages 1 and 9 of one file, page 10 of the next one; no
    // caption (the frame has only the dot and the number)
    pdf::PDFAppToolPageGridModel* model = gui->getGrid()->getModel();
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-5715-pages-split-at-pages"));
    QVERIFY(model->getTile(9).markerBefore);
    QVERIFY(!model->getTile(8).markerBefore && !model->getTile(0).markerBefore);
    QVERIFY(model->getTile(0).dotColor.has_value() && model->getTile(9).dotColor.has_value());
    QCOMPARE(model->getTile(0).dotColor, model->getTile(8).dotColor);
    QVERIFY(model->getTile(9).dotColor != model->getTile(8).dotColor);
    QVERIFY(model->getTile(9).caption.isEmpty());

    // 383:5715: one grid of every page (no cards), "Click between two pages to start a new file",
    // the hint 10 above the tiles, the tiles 4 apart; Clear Cuts in the toolbar, the section Cuts
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    QVERIFY(!grid->isGrouped());
    QVERIFY(grid->getView()->isVisible() && !grid->getGroupView()->isVisible());
    QCOMPARE(grid->getModel()->rowCount(), 48);
    QCOMPARE(grid->getHeaderText(), QStringLiteral("Click between two pages to start a new file"));
    QCOMPARE(grid->getHintLabel()->text(), QStringLiteral("The dot shows the file a page goes to. Click a cut line to remove it, or edit the page numbers on the right."));
    QVERIFY(gui->getClearCutsButton()->isVisible() && gui->getClearCutsButton()->isEnabled());
    QCOMPARE(gui->getClearCutsButton()->text(), QStringLiteral("Clear Cuts"));
    pdf::PDFAppToolPageGridView* view = grid->getView();
    const QRect tile1 = view->visualRect(model->index(0));
    const QRect tile2 = view->visualRect(model->index(1));
    QCOMPARE(tile2.left() - tile1.right() - 1, 4);
    const QRect hintRect(grid->getHintLabel()->mapTo(grid, QPoint(0, 0)), grid->getHintLabel()->size());
    const QPoint tileTop = view->viewport()->mapTo(grid, tile1.topLeft());
    QCOMPARE(tileTop.y() - hintRect.bottom() - 1, 10);
    QCOMPARE(tileTop.x(), 20);

    // insert-marker 383:5715: the tile with a cut is wider by the mark 10 + 4; the amber line of 3
    // in the middle of the mark, as high as the page (73 x 100 in Small), the page 14 behind
    const QRect tile10 = view->visualRect(model->index(9));
    const QRect tile9 = view->visualRect(model->index(8));
    QCOMPARE(tile10.width(), tile9.width() + 14);
    QCOMPARE(tile9.size(), QSize(85, 128));
    pdf::PDFAppToolPageTileDelegate* delegate = qobject_cast<pdf::PDFAppToolPageTileDelegate*>(grid->getDelegate());
    QVERIFY(delegate);
    QStyleOptionViewItem option;
    option.direction = Qt::LeftToRight;
    option.rect = QRect(QPoint(100, 50), delegate->sizeHint(option, model->index(9)));
    pdf::PDFAppToolPageTileDelegate::MarkRects rects = delegate->getMarkRects(option, model->index(9));
    QVERIFY(!rects.page.isEmpty());
    QCOMPARE(rects.cut, QRectF(103.5, 50, 3, 100));
    QCOMPARE(rects.tile.left(), 114.0);
    QVERIFY2(rects.cut.right() <= rects.page.left(), qPrintable(QStringLiteral("%1 %2").arg(rects.cut.right()).arg(rects.page.left())));
    QVERIFY(rects.dot.right() < rects.tile.center().x() && rects.dot.top() > rects.page.bottom());
    QVERIFY(delegate->getMarkRects(option, model->index(8)).cut.isEmpty());
    option.direction = Qt::RightToLeft;
    const pdf::PDFAppToolPageTileDelegate::MarkRects mirrored = delegate->getMarkRects(option, model->index(9));
    QCOMPARE(mirrored.cut.right(), QRectF(option.rect).right() - 3.5);
    QCOMPARE(mirrored.tile.right(), QRectF(option.rect).right() - 14);
    QVERIFY(mirrored.dot.left() > mirrored.tile.center().x());

    // A click between two pages (the gap and the edges of the tiles beside it) places a cut, a
    // click on the cut removes it, a click in the middle of a page does nothing; one undo step each
    const QRect tile5 = view->visualRect(model->index(4));
    const QRect tile6 = view->visualRect(model->index(5));
    QCOMPARE(view->getGapAt(QPoint(tile5.right() + 2, tile5.center().y())), 5);
    QCOMPARE(view->getGapAt(QPoint(tile6.left() + 3, tile6.center().y())), 5);
    QCOMPARE(view->getGapAt(QPoint(tile5.right() - 3, tile5.center().y())), 5);
    QCOMPARE(view->getGapAt(tile5.center()), -1);
    QCOMPARE(view->getGapAt(QPoint(tile1.left() + 2, tile1.center().y())), -1);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(tile6.left() + 2, tile6.center().y()));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("6, 10, 20, 35"));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(model->index(2)).center());
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("6, 10, 20, 35"));
    const QRect cutTile = view->visualRect(model->index(5));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(cutTile.left() + 5, cutTile.top() + 40));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("10, 20, 35"));

    // Clear Cuts (383:5715): no cut, one undo step; without a cut no file is split off (the rule
    // of the core), the pages stay as they are (no page is muted)
    gui->getClearCutsButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(QStringLiteral("cuts")).toString().isEmpty());
    QVERIFY(gui->getFiles().empty());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!model->getTile(3).isDimmed);
    QVERIFY(!gui->getClearCutsButton()->isEnabled());
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("cuts")).toString(), QStringLiteral("10, 20, 35"));

    // Every N pages again: the cards of the files, no click between pages
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-n-pages"));
    QVERIFY(grid->isGrouped());
    QVERIFY(!gui->getClearCutsButton()->isVisible());
    QVERIFY(!view->isGapClicks());
}

void AppToolSplitTest::splitBookmarks()
{
    // 383:6771 At bookmarks: "Start a new file at" with Top level in the place of the page
    // selection, "N of M bookmarks · pages before the first bookmark join the first file", a row
    // per bookmark (a child indented by 20, its page "p. 7"); the checked rows are the value of
    // the parameter, the files "Introduction · pages 1–6"
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    MergeTestPdf pdf = MergeTestPdf::createPages("", 20, "/Outlines 30 0 R");
    pdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 33 0 R >>");
    pdf.set(31, "<< /Title (Introduction) /Parent 30 0 R /Next 32 0 R /First 34 0 R /Last 34 0 R /Dest [" + MergeTestPdf::page(1) + " /Fit] >>");
    pdf.set(32, "<< /Title (Installation) /Parent 30 0 R /Prev 31 0 R /Next 33 0 R /Dest [" + MergeTestPdf::page(7) + " /Fit] >>");
    pdf.set(33, "<< /Title (Appendix) /Parent 30 0 R /Prev 32 0 R /Dest [" + MergeTestPdf::page(15) + " /Fit] >>");
    pdf.set(34, "<< /Title (Windows) /Parent 31 0 R /Dest [" + MergeTestPdf::page(4) + " /Fit] >>");
    const QString filePath = fixture.path(QStringLiteral("Handbook.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(pdf.write());
    file.close();
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    QVERIFY(!gui->getBookmarksPart()->isVisible());
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("at-bookmarks"));
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_BOOKMARKS, QStringLiteral("top-level"));

    QVERIFY(gui->getBookmarksPart()->isVisible());
    QVERIFY(!gui->getPageSelection()->isVisible());
    QCOMPARE(gui->getTopLevelLink()->text(), QStringLiteral("Top level"));
    pdf::PDFAppSplitBookmarkModel* bookmarks = gui->getBookmarkModel();
    QCOMPARE(bookmarks->rowCount(), 4);
    QCOMPARE(bookmarks->data(bookmarks->index(1), Qt::DisplayRole).toString(), QStringLiteral("Windows"));
    QCOMPARE(bookmarks->data(bookmarks->index(1), pdf::PDFAppSplitBookmarkModel::LevelRole).toInt(), 1);
    QCOMPARE(bookmarks->data(bookmarks->index(2), pdf::PDFAppSplitBookmarkModel::PageTextRole).toString(), QStringLiteral("p. 7"));
    QCOMPARE(bookmarks->getCheckedRows(), std::vector<int>({ 0, 2, 3 }));
    QCOMPARE(gui->getBookmarksHint()->text(), QStringLiteral("3 of 4 bookmarks · pages before the first bookmark join the first file"));
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-6771-pages-split-bookmarks"));

    // The rows of the files and the cards: "Introduction · pages 1–6"
    QCOMPARE(gui->getFiles().size(), size_t(3));
    pdf::PDFAppToolPendingPagesModel* rows = gui->getPendingModel();
    QCOMPARE(rows->data(rows->index(0, 0), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Introduction · pages 1–6"));
    QCOMPARE(gui->getGrid()->getModel()->getGroups()[1].subtitle, QStringLiteral("Installation · pages 7–14"));

    // The check box of the child is indented by 20 (383:6771), mirrored for right-to-left
    QListView* view = gui->getBookmarkView();
    const QRect rowRect = view->visualRect(bookmarks->index(1));
    QCOMPARE(rowRect.height(), 24);
    QCOMPARE(pdf::PDFAppSplitBookmarkDelegate::getCheckRect(rowRect, 1, false).left() - pdf::PDFAppSplitBookmarkDelegate::getCheckRect(rowRect, 0, false).left(), 20);
    QCOMPARE(pdf::PDFAppSplitBookmarkDelegate::getCheckRect(rowRect, 1, true).right(), rowRect.right() - 20);

    // A click on the child starts a file at it (one undo step); Top level again
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rowRect.center());
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("bookmarks")).toString(), QStringLiteral("1-4"));
    QCOMPARE(gui->getFiles().size(), size_t(4));
    QCOMPARE(gui->getBookmarksHint()->text(), QStringLiteral("4 of 4 bookmarks · pages before the first bookmark join the first file"));
    view->setCurrentIndex(bookmarks->index(2));
    QTest::keyClick(view, Qt::Key_Space);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("bookmarks")).toString(), QStringLiteral("1, 2, 4"));
    gui->getTopLevelLink()->click();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("bookmarks")).toString(), QStringLiteral("top-level"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(bookmarks->getCheckedRows(), std::vector<int>({ 0, 1, 3 }));

    // Another rule: the page selection is back
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    QVERIFY(gui->getPageSelection()->isVisible() && !gui->getBookmarksPart()->isVisible());
}

void AppToolSplitTest::splitBySize()
{
    // 383:7775 By file size: the Toolbar field "10 MB" (64, no arrows), the blue callout "The size
    // is approximate" at the top of the right panel, the files "Pages 1–17 · about 9.8 MB"
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 6));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("by-size"));

    pdf::PDFAppToolNumberField* field = gui->getSizeField();
    QVERIFY(field->isVisible() && !gui->getPagesPerFileField()->isVisible());
    QCOMPARE(field->text(), QStringLiteral("10"));
    QCOMPARE(field->getSuffix(), QStringLiteral("MB"));
    QCOMPARE(field->height(), 32);
    QCOMPARE(field->width(), 64);
    pdf::PDFAppToolCallout* callout = gui->getSizeCallout();
    QVERIFY(callout->isVisible());
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("The size is approximate"));
    QVERIFY(callout->mapTo(fixture.window, QPoint(0, callout->height())).y() < gui->getOutputFiles()->getContainer()->mapTo(fixture.window, QPoint(0, 0)).y());
    QVERIFY(gui->getPendingModel()->data(gui->getPendingModel()->index(0, 0), pdf::PDFComponentRoles::SubtitleRole).toString().startsWith(QStringLiteral("Pages 1–6 · about ")));
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-7775-pages-split-size"));

    // A decimal number (the real parameter): 0.5 MB, one undo step
    field->setText(QStringLiteral("0.5"));
    field->commit();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("sizeMB")).toDouble(), 0.5);
    QCOMPARE(field->text(), QStringLiteral("0.5"));

    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("odd-even"));
    QVERIFY(!callout->isVisible() && !field->isVisible());
}

void AppToolSplitTest::splitWrittenMarks()
{
    // 383:10232: while the files are written, the card "Writing the files" says "Handbook_3.pdf ·
    // file 3 of 4", the files written before are "Written" (the green line of their rows, the Tag
    // of their cards), the status "… keeps the 2 files already written"; 403:29978: Stop keeps the
    // marks of the written files, the decision ends them
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 4));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* gui = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(gui);
    const QString folder = fixture.path(QStringLiteral("marks"));
    QDir().mkpath(folder);
    QFile existing(QDir(folder).filePath(QStringLiteral("Handbook_3.pdf")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("EXISTING");
    existing.close();
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("ask"));

    // The run waits for the question of Handbook_3.pdf: the files 1 and 2 are written
    shell->confirm();
    QPointer<pdf::PDFAppToolFileConflictDialog> question;
    QVERIFY(AppToolShellFixture::waitFor([&question]() { question = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget()); return question != nullptr; }));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getWrittenFiles() == std::set<int>({ 0, 1 }); }));
    pdf::PDFAppToolProgressCard* card = shell->getProgressCard();
    QVERIFY(card);
    QVERIFY(AppToolShellFixture::waitFor([card]() { return card->getStepLabel()->text() == QStringLiteral("Handbook_3.pdf · file 3 of 4"); }));
    QCOMPARE(card->getTitleLabel()->text(), QStringLiteral("Writing the files"));
    // Owner D4 (2026-11-10): one style of the card of a running tool, the one of Compress (result-card
    // 295:1792: the title Semi Bold 15, the step Regular 12), not 12 / 11 of the frame 383:10232
    QCOMPARE(card->getTitleLabel()->font().pixelSize(), 15);
    QCOMPARE(card->getTitleLabel()->font().weight(), QFont::DemiBold);
    QCOMPARE(card->getStepLabel()->font().pixelSize(), 12);
    QCOMPARE(card->getStepLabel()->font().weight(), QFont::Normal);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Writing the files · Stop ends the operation and keeps the 2 files already written"));
    pdf::PDFAppToolPendingPagesModel* rows = gui->getPendingModel();
    QCOMPARE(rows->data(rows->index(1, 0), pdf::PDFAppToolPendingPagesModel::StatusRole).toString(), QStringLiteral("Written"));
    QCOMPARE(rows->data(rows->index(1, 0), pdf::PDFAppToolPendingPagesModel::StatusColorRole).toInt(),
             int(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Green, pdf::PDFAccentHueColors::Role::Icon)));
    QVERIFY(rows->data(rows->index(2, 0), pdf::PDFAppToolPendingPagesModel::StatusRole).toString().isEmpty());
    QCOMPARE(gui->getGrid()->getModel()->getGroups()[0].tag, QStringLiteral("Written"));
    QVERIFY(gui->getGrid()->getModel()->getGroups()[2].tag.isEmpty());
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-10232-pages-split-running"));

    // Stop Export: the dialog of the stopped run, the two files keep their marks (403:29978)
    question->getStopButton()->click();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    QCOMPARE(gui->getWrittenFiles(), std::set<int>({ 0, 1 }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog && dialog->isStoppedRun());

    // The decision ends the marks (Close: the tool is edited again)
    dialog->getDiscardButton()->click();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Editing));
    QVERIFY(gui->getWrittenFiles().empty());
    QVERIFY(rows->data(rows->index(0, 0), pdf::PDFAppToolPendingPagesModel::StatusRole).toString().isEmpty());
}

void AppToolSplitTest::tileSizeDefaults()
{
    // Owner 2026-11-09: Split and Extract start with Thumbnails: Small, its row says "The default
    // size"; Rotate keeps Medium. The size chosen by the user stays for every grid - also a chosen
    // Medium, which is the default of Rotate (it is stored)
    using H = SplitGuiHelper;
    using TileSize = pdf::PDFAppToolPageGrid::TileSize;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 8));
    QVERIFY(tab);
    for (const QString& toolId : { H::SPLIT_ID, H::EXTRACT_ID })
    {
        PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
        pdf::PDFAppPageMarkingToolGUI* gui = H::gui<pdf::PDFAppPageMarkingToolGUI>(shell);
        QVERIFY(gui);
        QCOMPARE(gui->getTileSize(), TileSize::Small);
        QCOMPARE(gui->getTileSizeButton()->text(), QStringLiteral("Thumbnails: Small"));
        QCOMPARE(gui->getTileSizeRow(TileSize::Small)->getDescription(), QStringLiteral("The default size"));
        QVERIFY(gui->getTileSizeRow(TileSize::Medium)->getDescription() != QStringLiteral("The default size"));
        shell->cancel();
        AppToolShellFixture::settle();
    }

    PDFDocumentToolShell* shell = fixture.openTool(tab, QStringLiteral("pages.rotate"));
    pdf::PDFAppPageMarkingToolGUI* rotate = H::gui<pdf::PDFAppPageMarkingToolGUI>(shell);
    QVERIFY(rotate);
    QCOMPARE(rotate->getTileSize(), TileSize::Medium);
    QCOMPARE(rotate->getTileSizeRow(TileSize::Medium)->getDescription(), QStringLiteral("The default size"));

    // Medium chosen in Rotate: stored, Split opens with Medium
    rotate->getTileSizeButton()->click();
    AppToolShellFixture::settle();
    rotate->getTileSizeRow(TileSize::Medium)->click();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.context->getSettings().getString(pdf::PDFSettingKeys::toolsPageGridSize()), QStringLiteral("medium"));
    shell->cancel();
    AppToolShellFixture::settle();
    shell = fixture.openTool(tab, H::SPLIT_ID);
    pdf::PDFAppSplitToolGUI* split = H::gui<pdf::PDFAppSplitToolGUI>(shell);
    QVERIFY(split);
    QCOMPARE(split->getTileSize(), TileSize::Medium);
    QCOMPARE(split->getTileSizeButton()->text(), QStringLiteral("Thumbnails: Medium"));
    QCOMPARE(split->getGrid()->getGroupView()->getTileSize(0), QSize(85 + 37, 128 + 50));
}

void AppToolSplitTest::gridRowsAndGroups()
{
    // The rows of the grid of a tool (Extract 385:9405: the pages of the result; Split 383:3315:
    // the files in groups): a page has its tile and its picture in every row it is in, an
    // invalid page and a second row of one page are left out, the groups take their rows in
    // their order; a new document gives every page again
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 6));
    QVERIFY(tab);
    pdf::PDFAppToolPageGridModel model(nullptr);
    model.setPageDocument(tab->getDocument()->getDocument(), QStringLiteral("doc"));
    QCOMPARE(model.rowCount(), 6);
    std::vector<pdf::PDFAppToolPageGridModel::Tile> tiles(6);
    tiles[4].label = QStringLiteral("1");
    tiles[4].caption = QStringLiteral("from page 5");
    model.setTiles(tiles);
    const QString keyOfPage4 = model.getKey(4);

    QSignalSpy groupsChanged(&model, &pdf::PDFAppToolGroupedPageModel::groupsChanged);
    pdf::PDFAppToolGroupedPageModel::Group first;
    first.rowCount = 2;
    pdf::PDFAppToolGroupedPageModel::Group second;
    second.rowCount = 5;
    model.setRows(std::vector<int>({ 4, 1, 4, 9, -1, 2 }), { first, second });
    QCOMPARE(groupsChanged.count(), 1);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.getPageOfRow(0), 4);
    QCOMPARE(model.getPageOfRow(2), 2);
    QCOMPARE(model.getRowOfPage(1), 1);
    QCOMPARE(model.getRowOfPage(0), -1);
    QCOMPARE(model.getGroups()[1].firstRow, 2);
    QCOMPARE(model.getGroups()[1].rowCount, 1);
    QCOMPARE(model.getGroupOfRow(1), 0);
    QCOMPARE(model.getGroupOfRow(2), 1);
    QCOMPARE(model.getKey(0), keyOfPage4);
    QCOMPARE(model.getPictureSource(0).pageIndex, 4);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(model.index(0), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("from page 5"));
    QCOMPARE(model.data(model.index(1), Qt::DisplayRole).toString(), QStringLiteral("2"));

    // The same rows again: no signal; every page again
    model.setRows(std::vector<int>({ 4, 1, 2 }), { first, second });
    QCOMPARE(groupsChanged.count(), 1);
    model.setPageDocument(tab->getDocument()->getDocument(), QStringLiteral("doc"));
    QVERIFY(model.hasAllPages());
    QCOMPARE(model.rowCount(), 6);
    QVERIFY(model.getGroups().empty());
}

void AppToolSplitTest::splitWrittenFilesDialog()
{
    // 383:11374: "Split finished" / "Handbook.pdf was not changed.", Files "4 · Handbook_1.pdf …
    // Handbook_4.pdf", Folder, Size; Open the files in new tabs; Close and Open Folder; no
    // Then continue with (9.3 point 12)
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 8));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("written"));
    QDir().mkpath(folder);
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 2);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::WrittenFiles);
    QVERIFY(!dialog->isStoppedRun());
    QCOMPARE(H::factKeys(dialog->getFacts()), QStringList({ "Files", "Folder", "Size" }));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("4 · Handbook_1.pdf … Handbook_4.pdf"));
    // The folder is elided by its folders: the root and the last folder stay (fluent-style.md)
    QCOMPARE(dialog->getFacts()->getValue(1), QDir::toNativeSeparators(folder));
    QCOMPARE(dialog->getFacts()->getValueKind(1), pdf::PDFComponentPainter::TextKind::Path);
    QVERIFY(dialog->getOpenFilesCheckBox());
    QCOMPARE(dialog->getOpenFilesCheckBox()->text(), QStringLiteral("Open the files in new tabs"));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Open Folder"));
    QCOMPARE(dialog->getCloseButton()->text(), QStringLiteral("Close"));
    QVERIFY(!dialog->getDiscardButton()->isVisible());
    QVERIFY(!dialog->getContinueWith());

    // The toolbar after the run (owner 2026-11-09, 383:11374): Cancel Enabled, Split into 4
    // Files shown and Disabled in its place, no Stop
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar->getCancelButton()->isVisible() && toolbar->getCancelButton()->isEnabled());
    QVERIFY(toolbar->getConfirmButton()->isVisible() && !toolbar->getConfirmButton()->isEnabled());
    QCOMPARE(toolbar->getConfirmText(), QStringLiteral("Split into 4 Files"));
    QVERIFY(!toolbar->getStopButton()->isVisible());
    for (int i = 1; i <= 4; ++i)
    {
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_%1.pdf").arg(i))));
    }

    // Close: the files stay, the tool closes
    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getCloseButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_4.pdf"))));
}

void AppToolSplitTest::splitConflictQuestion()
{
    // 356:11997, R3: Existing files Ask - the run stops at the first existing file and asks;
    // "The folder … already contains 2 files", "Do the same for the other 1 files"; Skip for all
    // keeps both files, the others are written
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 4));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("exports"));
    QDir().mkpath(folder);
    for (const char* name : { "Handbook_1.pdf", "Handbook_3.pdf" })
    {
        QFile file(QDir(folder).filePath(QString::fromLatin1(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("EXISTING");
    }
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("ask"));

    shell->confirm();
    QPointer<pdf::PDFAppToolFileConflictDialog> question;
    QVERIFY(AppToolShellFixture::waitFor([&question]() { question = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget()); return question != nullptr; }));
    QCOMPARE(question->getQuestion().fileName, QStringLiteral("Handbook_1.pdf"));
    QCOMPARE(question->getQuestion().conflictCount, 2);
    QCOMPARE(question->getQuestion().otherCount, 1);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    if (QCheckBox* forAll = question->findChild<QCheckBox*>())
    {
        forAll->setChecked(true);
    }
    question->getSkipButton()->click();

    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("2 · Handbook_2.pdf, Handbook_4.pdf"));
    for (const char* name : { "Handbook_1.pdf", "Handbook_3.pdf" })
    {
        QFile file(QDir(folder).filePath(QString::fromLatin1(name)));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("EXISTING"));
    }
    dialog->getCloseButton()->click();
}

void AppToolSplitTest::splitSourceNeverOverwritten()
{
    // R14: a file of the run named as the source (open in its tab) is never overwritten, also by
    // Overwrite - it is not written, the dialog names it
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const QString source = H::writePages(fixture.path(QStringLiteral("Handbook_1.pdf")), 2);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, source);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    QVERIFY(shell);
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray before = original.readAll();
    original.close();

    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME, QStringLiteral("Handbook_#"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("overwrite"));
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(H::factKeys(dialog->getFacts()).contains(QStringLiteral("Not written")));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), before);
    QVERIFY(QFile::exists(fixture.path(QStringLiteral("Handbook_2.pdf"))));
    dialog->getCloseButton()->click();
}

void AppToolSplitTest::splitStoppedDialog()
{
    // 403:29978: Stop Export of the question stops the run - "Split stopped", Written "1 of 3 ·
    // …", Not written, Folder; Delete Written Files deletes only the files of the run (K46), the
    // tool keeps its settings
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 3));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("stopped"));
    QDir().mkpath(folder);
    QFile existing(QDir(folder).filePath(QStringLiteral("Handbook_2.pdf")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("EXISTING");
    existing.close();
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("ask"));

    shell->confirm();
    QPointer<pdf::PDFAppToolFileConflictDialog> question;
    QVERIFY(AppToolShellFixture::waitFor([&question]() { question = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget()); return question != nullptr; }));
    question->getStopButton()->click();

    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isStoppedRun());
    QCOMPARE(H::factKeys(dialog->getFacts()), QStringList({ "Written", "Not written", "Folder" }));
    QCOMPARE(dialog->getFacts()->getValueKind(2), pdf::PDFComponentPainter::TextKind::Path);
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("1 of 3 · Handbook_1.pdf"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("Handbook_2.pdf, Handbook_3.pdf"));
    QVERIFY(dialog->getDiscardButton()->isVisible());
    QCOMPARE(dialog->getDiscardButton()->text(), QStringLiteral("Delete Written Files"));
    QCOMPARE(dialog->getDeletableFiles().size(), 1);

    dialog->getDiscardButton()->click();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Editing));
    QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_1.pdf"))));
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_2.pdf"))));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("rule")).toString(), QStringLiteral("every-page"));
}

void AppToolSplitTest::splitQuestionAgainAfterStop()
{
    // 356:11997, R3 (review RS3APP): the tool keeps its settings after Stop Export (403:29978), so
    // the next run meets the same existing file - it asks again (the question of the first run
    // is not taken for the same one, the run never waits without a question)
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 2));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("again"));
    QDir().mkpath(folder);
    QFile existing(QDir(folder).filePath(QStringLiteral("Handbook_1.pdf")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("EXISTING");
    existing.close();
    H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-page"));
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("ask"));

    for (int run = 0; run < 2; ++run)
    {
        // Handbook_1.pdf is the first file of the run: nothing is written before the question.
        // The GUI thread processes no event until the run asks (getFileQuestion is thread-safe),
        // so the first poll of the run already meets the question - as on a busy event loop
        shell->confirm();
        pdf::PDFDocumentToolRunner* runner = shell->getRunner();
        QVERIFY(runner);
        QElapsedTimer asking;
        asking.start();
        while (!runner->getFileQuestion() && asking.elapsed() < 15000 * AppToolShellFixture::getTimeoutFactor())
        {
            QThread::msleep(1);
        }
        QVERIFY(runner->getFileQuestion());
        QPointer<pdf::PDFAppToolFileConflictDialog> question;
        QVERIFY2(AppToolShellFixture::waitFor([&question]() { question = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget()); return question != nullptr; }, 1000),
                 qPrintable(QStringLiteral("no question in the run %1").arg(run + 1)));
        QCOMPARE(question->getQuestion().fileName, QStringLiteral("Handbook_1.pdf"));
        QCOMPARE(question->getQuestion().conflictCount, 1);
        question->getStopButton()->click();

        // Stopped before any file: no dialog of written files, the tool is edited again
        QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Editing));
        QVERIFY(!shell->getResultDialog());
        QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_2.pdf"))));
        AppToolShellFixture::settle();
    }

    QFile file(QDir(folder).filePath(QStringLiteral("Handbook_1.pdf")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("EXISTING"));
}

void AppToolSplitTest::extractFrame()
{
    // 385:9405: pages 1-3, 8, 10-12 - "Extract 7 Pages", "New document · 7 pages", the summary
    // Source "Handbook.pdf", Pages "7 of 48", Output "New document" (rows 36 apart), the note and
    // the status bar of the frame; Output files hidden for One file; the result is a new document
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::EXTRACT_ID);
    pdf::PDFAppExtractToolGUI* gui = H::gui<pdf::PDFAppExtractToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFDocumentToolPageSelection selection;
    selection.pages = QStringLiteral("1-3, 8, 10-12");
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_PAGES, selection.toVariant());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Extract 7 Pages"));
    QCOMPARE(gui->getGrid()->getHeaderText(), QStringLiteral("New document · 7 pages"));
    QCOMPARE(H::factKeys(gui->getSummary()), QStringList({ QStringLiteral("Source"), QStringLiteral("Pages"), QStringLiteral("Output") }));
    QCOMPARE(gui->getSummary()->getValue(0), QStringLiteral("Handbook.pdf"));
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("7 of 48"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("New document"));
    QCOMPARE(gui->getSummary()->getRowRect(1).top() - gui->getSummary()->getRowRect(0).top(), 36);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Extract Pages · 7 of 48 pages → a new document · Handbook.pdf is not changed"));
    QVERIFY(!gui->getOutputFiles()->getContainer()->isVisibleTo(gui->getOutputFiles()->getContainer()->parentWidget()));

    // RS3FIGMA2, 385:9405 / 385:10213: the button "Output: One file" has icon/file, the rows of
    // its menu icon/file and icon/split-pages; One file per page turns the button to
    // icon/split-pages (385:11029) and back
    QVERIFY(gui->getOutputButton());
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getOutputButton()->icon()), QStringLiteral("file"));
    const pdf::PDFDocumentToolParameterDefinition* output = pdf::PDFDocumentToolParameters::find(shell->getModel()->getDefinitions(), QString::fromLatin1(pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT));
    QVERIFY(output && output->getOptions().size() == 2);
    QCOMPARE(output->getOptions()[0].iconName, QStringLiteral("file"));
    QCOMPARE(output->getOptions()[1].iconName, QStringLiteral("split-pages"));
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT, QStringLiteral("one-file-per-page"));
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getOutputButton()->icon()), QStringLiteral("split-pages"));
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT, QStringLiteral("one-file"));
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getOutputButton()->icon()), QStringLiteral("file"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Extract 7 Pages"));

    // H2, 385:9405: the canvas shows only the 7 pages of the result - "4" and "from page 8", no
    // mark of a removal; the tiles 121 x 194 (Small, the default) 6 apart, the hint 12 below them
    pdf::PDFAppToolPageGrid* grid = gui->getGrid();
    pdf::PDFAppToolPageGridModel* model = grid->getModel();
    QVERIFY(!grid->isGrouped());
    QCOMPARE(model->rowCount(), 7);
    QCOMPARE(model->getPageOfRow(3), 7);
    QCOMPARE(model->data(model->index(3), Qt::DisplayRole).toString(), QStringLiteral("4"));
    QCOMPARE(model->data(model->index(3), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("from page 8"));
    for (int row = 0; row < model->rowCount(); ++row)
    {
        QCOMPARE(model->data(model->index(row), pdf::PDFComponentRoles::PageTileStateRole).toInt(), int(pdf::PDFComponentPainter::PageTileState::Normal));
        QVERIFY(model->data(model->index(row), pdf::PDFComponentRoles::StateIconRole).toString().isEmpty());
        QVERIFY(!model->data(model->index(row), pdf::PDFAppToolPageTileDelegate::DimmedRole).toBool());
    }
    QCOMPARE(grid->getHintLabel()->text(), QStringLiteral("The pages are copied – Handbook.pdf stays as it is. Click a page in the thumbnails to add or remove it."));
    pdf::PDFAppToolPageGridView* view = grid->getView();
    const QRect tile1 = view->visualRect(model->index(0));
    const QRect tile2 = view->visualRect(model->index(1));
    QCOMPARE(tile1.size(), QSize(121, 194));
    QCOMPARE(tile2.left() - tile1.right() - 1, 6);
    const QRect lastTile = view->visualRect(model->index(6));
    const QPoint lastBottom = view->viewport()->mapTo(grid, lastTile.bottomLeft());
    QCOMPARE(grid->getHintLabel()->mapTo(grid, QPoint(0, 0)).y() - lastBottom.y() - 1, 12);

    // The thumbnails of the source below the page selection (as Crop): the selected ones are the
    // pages to extract; a click on one adds or removes it (one undo step), Delete on a page of
    // the result leaves it out
    pdf::PDFAppThumbnailListView* thumbnails = gui->getThumbnailView();
    QVERIFY(thumbnails && thumbnails->isVisible());
    QVERIFY(thumbnails->mapTo(shell->getLeftPanel(), QPoint(0, 0)).y() > gui->getPageSelection()->mapTo(shell->getLeftPanel(), QPoint(0, 0)).y());
    QItemSelectionModel* thumbnailSelection = thumbnails->selectionModel();
    QVERIFY(thumbnailSelection->isRowSelected(7, QModelIndex()) && thumbnailSelection->isRowSelected(0, QModelIndex()));
    QVERIFY(!thumbnailSelection->isRowSelected(3, QModelIndex()));
    QCOMPARE(int(thumbnailSelection->selectedRows().size()), 7);
    thumbnails->scrollTo(thumbnails->model()->index(3, 0));
    AppToolShellFixture::settle();
    QTest::mouseClick(thumbnails->viewport(), Qt::LeftButton, Qt::NoModifier, thumbnails->visualRect(thumbnails->model()->index(3, 0)).center());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages().size(), size_t(8));
    QCOMPARE(model->rowCount(), 8);
    QVERIFY(thumbnailSelection->isRowSelected(3, QModelIndex()));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Extract 8 Pages"));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(model->index(0)).center());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages().size(), size_t(8));
    view->setCurrentIndex(model->index(3));
    QTest::keyClick(view, Qt::Key_Delete);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getTargetPages().size(), size_t(7));
    QVERIFY(!thumbnailSelection->isRowSelected(3, QModelIndex()));
    QCOMPARE(model->getPageOfRow(3), 7);
    const QList<pdf::PDFAppParagraph*> paragraphs = shell->getRightPanel()->findChildren<pdf::PDFAppParagraph*>();
    bool hasNote = false;
    for (const pdf::PDFAppParagraph* paragraph : paragraphs)
    {
        hasNote = hasNote || paragraph->text() == QStringLiteral("Extract Pages never changes the open document, so it also works when the document is locked. To delete pages, use Remove Pages.");
    }
    QVERIFY(hasNote);

    // 385:10213 the menu Output, 385:11029 One file per page: Output "7 files", "→ 7 files"
    QCOMPARE(H::optionDescriptions(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT),
             QStringList({ QStringLiteral("All selected pages in one new document"), QStringLiteral("Every selected page becomes its own file") }));
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT, QStringLiteral("one-file-per-page"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("7 files"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Extract Pages · 7 of 48 pages → 7 files · Handbook.pdf is not changed"));
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT, QStringLiteral("one-file"));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::NewDocument);
    dialog->reject();
}

void AppToolSplitTest::extractPerPage()
{
    // 385:11029: One file per page - "Extract 4 Files", Output files "Handbook_p1.pdf …
    // Handbook_p8.pdf", "4 files · @ is replaced by the page number"; the run writes the files
    using H = SplitGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 12));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::EXTRACT_ID);
    pdf::PDFAppExtractToolGUI* gui = H::gui<pdf::PDFAppExtractToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFDocumentToolPageSelection selection;
    selection.pages = QStringLiteral("1-3, 8");
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_PAGES, selection.toVariant());
    H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_OUTPUT, QStringLiteral("one-file-per-page"));
    const QString folder = fixture.path(QStringLiteral("pages"));
    QDir().mkpath(folder);
    H::setValue(shell, pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder);

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Extract 4 Files"));
    QCOMPARE(gui->getOutputFiles()->getPreview()->text(), QStringLiteral("Handbook_p1.pdf … Handbook_p8.pdf"));
    QCOMPARE(gui->getOutputFiles()->getPreviewHint()->text(), QStringLiteral("4 files · @ is replaced by the page number"));

    // 385:11029: "4 new files", the tile of a file has its name and "page 8"
    pdf::PDFAppToolPageGridModel* model = gui->getGrid()->getModel();
    QCOMPARE(gui->getGrid()->getHeaderText(), QStringLiteral("4 new files"));
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(model->data(model->index(3), Qt::DisplayRole).toString(), QStringLiteral("Handbook_p8.pdf"));
    QCOMPARE(model->data(model->index(3), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("page 8"));
    AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("385-11029-pages-extract-separate"));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::WrittenFiles);
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_p8.pdf"))));
    dialog->getCloseButton()->click();
}

void AppToolSplitTest::themesDirectionAndScreenshots()
{
    // Split (383:3315) and Extract (385:9405) in both themes, 1x and 2x, left to right and right
    // to left: no color of the system in the window (the violet leak color), the left panel at
    // the trailing side in RTL; PDF4QT_APP_SCREENSHOTS saves the frames under their ids
    using H = SplitGuiHelper;
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            const QString suffix = QStringLiteral("-%1%2").arg(theme, direction == Qt::RightToLeft ? QStringLiteral("-rtl") : QString());

            // The fixture sets the light scheme of its context: the theme of the row comes after it
            AppToolShellFixture fixture(QSize(1440, 960));
            pdf::PDFThemeManager::getInstance()->setTheme(theme);
            AppToolShellFixture::settle();
            QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), theme == QLatin1String("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
            QVERIFY(tab);

            PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
            QVERIFY(H::gui<pdf::PDFAppSplitToolGUI>(shell));
            H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-n-pages"));
            H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 12);
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(fixture.window, ratio)), 0);
            }
            const QRect splitLeft(shell->getLeftPanel()->mapTo(fixture.window, QPoint(0, 0)), shell->getLeftPanel()->size());
            QVERIFY2(direction == Qt::RightToLeft ? splitLeft.left() > 1000 : splitLeft.right() < 440, qPrintable(QString::number(splitLeft.left())));
            AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("383-3315-pages-split") + suffix);
            shell->cancel();
            AppToolShellFixture::settle();

            shell = fixture.openTool(tab, H::EXTRACT_ID);
            QVERIFY(H::gui<pdf::PDFAppExtractToolGUI>(shell));
            pdf::PDFDocumentToolPageSelection selection;
            selection.pages = QStringLiteral("1-3, 8, 10-12");
            H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_PAGES, selection.toVariant());
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(fixture.window, ratio)), 0);
            }
            AppToolShellFixture::screenshotDialog(fixture.window, QStringLiteral("385-9405-pages-extract") + suffix);
            shell->cancel();
            AppToolShellFixture::settle();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolSplitTest::pageListTexts()
{
    // Owner D2 / D3 (2026-11-10), in both themes: the status of the page selection names the pages
    // in canonical ranges as the frames (385:9405 "7 pages selected · 1–3, 8, 10–12"; every page:
    // 383:3315 "48 pages selected"), drawn whole in its line (icon 14 + gap 6 of 265:985 before the
    // Caption text); a file of two pages is "Pages 1–2" (388:17167 "13–14"), never "Pages 1, 2"
    using H = SplitGuiHelper;
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setTheme(theme);
        AppToolShellFixture::settle();
        QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), theme == QLatin1String("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48));
        QVERIFY(tab);

        const auto isWhole = [](const pdf::PDFPageSelection* selection)
        {
            const pdf::PDFComponentPainter::PageSelectionLayout layout = pdf::PDFComponentPainter::getPageSelectionLayout(selection->getContent(), selection->width());
            const qreal textWidth = QFontMetricsF(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Caption)).horizontalAdvance(selection->getStatusText());
            return textWidth <= layout.status.width() - 14 - 6;
        };

        PDFDocumentToolShell* shell = fixture.openTool(tab, H::SPLIT_ID);
        pdf::PDFAppSplitToolGUI* split = H::gui<pdf::PDFAppSplitToolGUI>(shell);
        QVERIFY(split);
        QCOMPARE(split->getPageSelection()->getStatusText(), QStringLiteral("48 pages selected"));
        QVERIFY(isWhole(split->getPageSelection()));
        H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("every-n-pages"));
        H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE, 2);
        QCOMPARE(split->getGrid()->getModel()->getGroups()[0].subtitle, QStringLiteral("Pages 1–2"));
        QCOMPARE(split->getPendingModel()->data(split->getPendingModel()->index(23, 0), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Pages 47–48"));

        // Odd and even pages: at most three parts of the pages of a file, then "…"
        H::setValue(shell, pdf::PDFDocumentToolSplit::PARAMETER_RULE, QStringLiteral("odd-even"));
        QCOMPARE(split->getGrid()->getModel()->getGroups()[0].subtitle, QStringLiteral("Pages 1, 3, 5, …"));

        // Selected pages of the thumbnails
        tab->getView()->setSelectedPages({ 8, 9, 14 });
        split->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::Selected);
        AppToolShellFixture::settle();
        QCOMPARE(split->getPageSelection()->getStatusText(), QStringLiteral("3 pages selected · 9–10, 15"));
        shell->cancel();
        AppToolShellFixture::settle();

        shell = fixture.openTool(tab, H::EXTRACT_ID);
        pdf::PDFAppExtractToolGUI* extract = H::gui<pdf::PDFAppExtractToolGUI>(shell);
        QVERIFY(extract);
        pdf::PDFDocumentToolPageSelection selection;
        selection.pages = QStringLiteral("1-3, 8, 10-12");
        H::setValue(shell, pdf::PDFDocumentToolExtract::PARAMETER_PAGES, selection.toVariant());
        AppToolShellFixture::settle();
        QCOMPARE(extract->getPageSelection()->getStatusText(), QStringLiteral("7 pages selected · 1–3, 8, 10–12"));
        QVERIFY(isWhole(extract->getPageSelection()));
        shell->cancel();
        AppToolShellFixture::settle();
    }
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

QTEST_MAIN(AppToolSplitTest)

#include "tst_apptoolsplittest.moc"
