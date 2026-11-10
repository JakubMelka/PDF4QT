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

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolfilesink.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcatalog.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QThread>
#include <QtTest>

using pdf::PDFDocumentToolShell;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;

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
    void splitWrittenFilesDialog();
    void splitConflictQuestion();
    void splitSourceNeverOverwritten();
    void splitStoppedDialog();
    void splitQuestionAgainAfterStop();
    void extractFrame();
    void extractPerPage();
    void themesDirectionAndScreenshots();
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
    QVERIFY(gui->getOutputFiles()->getErrorCallout()->isVisibleTo(gui->getOutputFiles()->getContainer()));

    // 383:9088: the text of the callout says the remedy, the status bar the cause
    QCOMPARE(gui->getOutputFiles()->getErrorCallout()->getTextLabel()->text(), QStringLiteral("Add # to the file name so that each file gets its own number, for example Handbook_#."));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Split · the file name template gives the same name to every file"));
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
    // dot of the file of each page - pages 1 and 9 of one file, page 10 of the next one
    pdf::PDFAppToolPageGridModel* model = gui->getGrid()->getModel();
    QVERIFY(model->getTile(9).markerBefore);
    QVERIFY(!model->getTile(8).markerBefore && !model->getTile(0).markerBefore);
    QVERIFY(model->getTile(0).dotColor.has_value() && model->getTile(9).dotColor.has_value());
    QCOMPARE(model->getTile(0).dotColor, model->getTile(8).dotColor);
    QVERIFY(model->getTile(9).dotColor != model->getTile(8).dotColor);

    // The cut lies at the leading edge inside of the tile, the dot in front of the label below
    // the page; both are mirrored in a right-to-left layout
    pdf::PDFAppToolPageTileDelegate* delegate = qobject_cast<pdf::PDFAppToolPageTileDelegate*>(gui->getGrid()->getDelegate());
    QVERIFY(delegate);
    QStyleOptionViewItem option;
    option.direction = Qt::LeftToRight;
    option.rect = QRect(QPoint(100, 50), delegate->sizeHint(option, model->index(9)));
    pdf::PDFAppToolPageTileDelegate::MarkRects rects = delegate->getMarkRects(option, model->index(9));
    QVERIFY(!rects.page.isEmpty());
    QCOMPARE(rects.cut.left(), 100.0);
    QCOMPARE(rects.cut.width(), qreal(pdf::PDFAppToolPageTileDelegate::CUT_WIDTH));
    QVERIFY2(rects.cut.right() <= rects.page.left(), qPrintable(QStringLiteral("%1 %2").arg(rects.cut.right()).arg(rects.page.left())));
    QVERIFY(rects.dot.right() < option.rect.center().x() && rects.dot.top() > rects.page.bottom());
    QVERIFY(delegate->getMarkRects(option, model->index(8)).cut.isEmpty());
    option.direction = Qt::RightToLeft;
    const pdf::PDFAppToolPageTileDelegate::MarkRects mirrored = delegate->getMarkRects(option, model->index(9));
    QCOMPARE(mirrored.cut.right(), QRectF(option.rect).right());
    QVERIFY(mirrored.dot.left() > option.rect.center().x());
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

QTEST_MAIN(AppToolSplitTest)

#include "tst_apptoolsplittest.moc"
