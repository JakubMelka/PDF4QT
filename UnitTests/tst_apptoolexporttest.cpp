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

// The GUI of PDF to Images and the written files of the result dialog (step 4 of phase 2, package
// P1; plan 8 rows GUI and Dialog výsledku, Figma 339:3546 ... 339:7293, K4.11): the texts of the
// toolbar, the canvas, the right panel and the status bar of 339:3546 (A4 at 300 dpi = 2480 x 3508
// px), the menus of 339:4239 / 356:11387 (the menu Format shared with Extract Images, 341:7936), the
// dialog of 339:6505 with Rendering issues, the dialog of 339:7293 with Failed, Retry N Files writing
// only the failed file.

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappsettingcontrol.h"
#include "pdfappvaluestore.h"
#include "pdfappfiledrop.h"
#include "pdfapptoolhost.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfoptionalcontent.h"
#include "pdfsettingdefinitions.h"
#include "pdfapptoolgui_extractimages.h"
#include "pdfapptoolgui_pdftoimages.h"
#include "pdfapptoolimageformatmenu.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptoolresultwriter.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_split.h"
#include "pdfparser.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImageWriter>
#include <QLabel>
#include <QScopeGuard>
#include <QtTest>

using namespace pdftest;
using pdf::PDFDocumentToolShell;

/// Helpers of the test
class ExportGuiHelper
{
public:
    ExportGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("convert.pdf-to-images");

    /// A4 pages; the content of the second page names a missing image (a problem of the page)
    static QString writeReport(const QString& filePath, int count, bool isSecondPageBroken)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < count; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595.28, 841.89));
            const QByteArray content = (i == 1 && isSecondPageBroken) ? QByteArray("/Missing Do") : QByteArray("0 0 1 rg 100 100 200 200 re f");
            pdf::PDFDictionaryBuilder dictionary;
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
            const pdf::PDFObjectReference contents = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(content))));
            pdf::PDFDictionaryBuilder update;
            update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contents));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// One A4 page with one inline image of 2 × 2 gray pixels (an image for Extract Images)
    static QString writeImagePage(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595.28, 841.89));
        const QByteArray content = QByteArray("q 100 0 0 100 100 100 cm BI /W 2 /H 2 /BPC 8 /CS /G ID ") + QByteArray("\x10\x40\x80\xF0", 4) + QByteArray(" EI Q");
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contents = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(content))));
        pdf::PDFDictionaryBuilder update;
        update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contents));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
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

    static pdf::PDFAppToolGUIPdfToImages* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<pdf::PDFAppToolGUIPdfToImages*>(shell->getGUI()) : nullptr;
    }

    static QStringList keys(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            result << list->getKey(i);
        }
        return result;
    }

    /// The output folder and 72 dpi (small images), files overwritten
    static void setOutput(PDFDocumentToolShell* shell, const QString& folder)
    {
        QVariantMap values;
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), folder);
        values.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), QString::fromLatin1(pdf::PDFDocumentToolFileOutput::EXISTING_OVERWRITE));
        values.insert(QStringLiteral("dpi"), 72);
        QVERIFY(shell->getModel()->setValues(values).isEmpty());
        AppToolShellFixture::settle();
    }

    static QStringList rowTitles(const QWidget* list)
    {
        QStringList titles;
        for (const pdf::PDFTileRow* row : list->findChildren<pdf::PDFTileRow*>(QString(), Qt::FindDirectChildrenOnly))
        {
            titles << row->text();
        }
        return titles;
    }
};

class AppToolExportTest : public QObject
{
    Q_OBJECT

private slots:
    void frameTexts();
    void formatAndResolutionMenus();
    void extractImagesFormatMenu();
    void writtenFilesWithRenderingIssues();
    void failedFileAndRetry();
    void chooseFolderWritesTheFailedFiles();
    void renderContextOfTheTab();
    void imageFieldsOfTheCards();
    void temporaryOutputIsCopied();
    void undoOfThePageRange();
    void copyImageWaitsForTheNewList();
    void estimateKeyAndCancellation();
    void screenshots();
};

void AppToolExportTest::frameTexts()
{
    // 339:3546: Format: PNG, 300 dpi, Export Images; "Page 1 → Report_1.png · 2480 × 3508 px" (A4 at
    // 300 dpi), Image size, the names of the files, the status bar with the estimate
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 3, false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    pdf::PDFAppToolGUIPdfToImages* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Export Images"));
    QCOMPARE(gui->getFormatButton()->text(), QStringLiteral("Format: PNG"));
    QCOMPARE(gui->getResolutionButton()->text(), QStringLiteral("300 dpi"));
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 1 → Report_1.png · 2480 × 3508 px"));
    QCOMPARE(gui->getImageSizeList()->getKey(0), QStringLiteral("Image size"));
    QCOMPARE(gui->getImageSizeList()->getValue(0), QStringLiteral("2480 × 3508 px"));
    QCOMPARE(gui->getFileNames(), QStringList({ QStringLiteral("Report_1.png"), QStringLiteral("Report_2.png"), QStringLiteral("Report_3.png") }));

    // 339:3546: PNG has no subtype ("–", disabled), Compression and Quality are enabled (the writer of
    // PNG takes both)
    QCOMPARE(gui->getSubtypeComboBox()->currentText(), QStringLiteral("–"));
    QVERIFY(!gui->getSubtypeComboBox()->isEnabled());
    QVERIFY(gui->getCompressionControl()->getWidget()->isEnabled());
    QVERIFY(gui->getQualityControl()->getWidget()->isEnabled());

    // The status bar says the estimate of the worker: "about …"
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isEstimating(); }));
    AppToolShellFixture::settle();
    QVERIFY(gui->getEstimatedSize() > 0);
    QVERIFY2(shell->getStatusMessage().startsWith(QStringLiteral("PDF to Images · Report.pdf · 3 pages · PNG at 300 dpi · about ")), qPrintable(shell->getStatusMessage()));
}

void AppToolExportTest::formatAndResolutionMenus()
{
    // 339:4239: a format of the menu, More formats lists every format (356:11387), 150 dpi; Size by
    // Pixels: "2000 px", Longer side, 1414 × 2000 px and the status "PNG, longer side 2000 px"
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 2, false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    pdf::PDFAppToolGUIPdfToImages* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getFormatButton()->click();
    AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* formats = gui->getFormatPanel();
    pdf::PDFTileRow* jpeg = formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-jpeg"));
    QVERIFY(jpeg && jpeg->isVisible());
    QCOMPARE(jpeg->getDescription(), QStringLiteral("Small files, best for photos"));
    pdf::PDFTileRow* allBmp = formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-bmp"));
    QVERIFY(allBmp && !allBmp->isVisible());
    pdf::PDFTileRow* more = formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-more"));
    QVERIFY(more);
    QTest::mouseClick(more, Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(allBmp->isVisible() && !jpeg->isVisible());
    // ALL FORMATS lists a format once: jpg and tif are the aliases of jpeg and tiff
    QVERIFY(!formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-jpg")));
    QVERIFY(!formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-tif")));
    pdf::PDFTileRow* allJpeg = formats->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-jpeg"));
    QVERIFY(allJpeg);
    QTest::mouseClick(allJpeg, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("format")).toString(), QStringLiteral("jpeg"));
    QCOMPARE(gui->getFormatButton()->text(), QStringLiteral("Format: JPEG"));
    QCOMPARE(gui->getFileNames().front(), QStringLiteral("Report_1.jpg"));
    QVERIFY(gui->getQualityControl()->getWidget()->isEnabled());

    // The menu closed after the choice; opened again, it starts with the common formats and the
    // check mark at JPEG
    QVERIFY(!formats->isVisible());
    gui->getFormatButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(jpeg->isVisible() && jpeg->isChecked() && !allBmp->isVisible());
    formats->close();
    AppToolShellFixture::settle();

    gui->getResolutionButton()->click();
    AppToolShellFixture::settle();
    pdf::PDFTileRow* print = gui->getResolutionPanel()->findChild<pdf::PDFTileRow*>(QStringLiteral("toolResolution-300"));
    QVERIFY(print);
    QCOMPARE(print->getDescription(), QStringLiteral("Print quality"));
    pdf::PDFTileRow* screen = gui->getResolutionPanel()->findChild<pdf::PDFTileRow*>(QStringLiteral("toolResolution-150"));
    QVERIFY(screen);
    QTest::mouseClick(screen, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("dpi")).toInt(), 150);
    QCOMPARE(gui->getResolutionButton()->text(), QStringLiteral("150 dpi"));
    QCOMPARE(gui->getImageSizeList()->getValue(0), QStringLiteral("1240 × 1754 px"));

    // Undo returns 300 dpi (one step of the menu)
    shell->getToolbar()->getUndoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getResolutionButton()->text(), QStringLiteral("300 dpi"));

    QVERIFY(shell->getModel()->setValue(QStringLiteral("format"), QStringLiteral("png")).isEmpty());
    QVERIFY(shell->getModel()->setValue(QStringLiteral("sizeBy"), QStringLiteral("pixels")).isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getResolutionButton()->text(), QStringLiteral("2000 px"));
    QCOMPARE(gui->getImageSizeList()->getValue(0), QStringLiteral("1414 × 2000 px"));
    QVERIFY(gui->getPixelsControl()->getWidget()->isVisibleTo(fixture.window));
    QVERIFY(!gui->getDpiControl()->getWidget()->isVisibleTo(fixture.window));
    QVERIFY2(shell->getStatusMessage().startsWith(QStringLiteral("PDF to Images · Report.pdf · 2 pages · PNG, longer side 2000 px")), qPrintable(shell->getStatusMessage()));
}

void AppToolExportTest::extractImagesFormatMenu()
{
    // 341:7936: Extract Images has the same menu Format as PDF to Images (one shared class):
    // the common formats, More formats with ALL FORMATS; the chosen row sets the parameter and the
    // button, and the names of the files take its extension
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImagePage(fixture.path(QStringLiteral("Scan.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QStringLiteral("convert.extract-images"));
    pdf::PDFAppToolGUIExtractImages* gui = shell ? qobject_cast<pdf::PDFAppToolGUIExtractImages*>(shell->getGUI()) : nullptr;
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isComputing() && gui->getPlan().has_value() && gui->getPlan()->images.size() == 1; }, 60000));
    AppToolShellFixture::settle();

    pdf::PDFAppToolImageFormatMenu* menu = gui->getFormatMenu();
    QVERIFY(menu && menu->getButton() == gui->getFormatButton());
    QCOMPARE(gui->getFormatButton()->text(), QStringLiteral("Format: PNG"));
    QVERIFY(gui->getFormatButton()->isEnabled());

    gui->getFormatButton()->click();
    AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = menu->getPanel();
    QVERIFY(panel && panel->isVisible());
    // TIFF is a common format where Qt can write it (the Linux Qt has no TIFF writer: the menu
    // hides the row there)
    pdf::PDFTileRow* tiff = panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-tiff"));
    if (QImageWriter::supportedImageFormats().contains("tiff"))
    {
        QVERIFY(tiff);
        QCOMPARE(tiff->getDescription(), QStringLiteral("Lossless, common in print"));
    }
    else
    {
        QVERIFY(!tiff);
    }
    pdf::PDFTileRow* more = panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-more"));
    QVERIFY(more);
    QTest::mouseClick(more, Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(menu->isAllFormatsShown());
    pdf::PDFTileRow* allBmp = panel->findChild<pdf::PDFTileRow*>(QStringLiteral("toolAllFormat-bmp"));
    QVERIFY(allBmp);
    QTest::mouseClick(allBmp, Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isComputing() && gui->getPlan().has_value() && !gui->getPlan()->images.empty(); }, 60000));
    AppToolShellFixture::settle();

    QVERIFY(!panel->isVisible());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("format")).toString(), QStringLiteral("bmp"));
    QCOMPARE(gui->getFormatButton()->text(), QStringLiteral("Format: BMP"));
    QVERIFY2(gui->getPlan()->images.front().fileName.endsWith(QStringLiteral(".bmp")), qPrintable(gui->getPlan()->images.front().fileName));

    // RS4FIGMA: the fields, which the format cannot use, are disabled as in PDF to Images (the
    // writer of BMP takes neither a compression nor a quality, PNG takes both, 339:3546)
    QVERIFY(gui->getCompressionControl() && gui->getQualityControl());
    QVERIFY(!gui->getCompressionControl()->getWidget()->isEnabled());
    QVERIFY(!gui->getQualityControl()->getWidget()->isEnabled());
    QVERIFY(shell->getModel()->setValue(QStringLiteral("format"), QStringLiteral("png")).isEmpty());
    AppToolShellFixture::settle();
    QVERIFY(gui->getCompressionControl()->getWidget()->isEnabled());
    QVERIFY(gui->getQualityControl()->getWidget()->isEnabled());
}

void AppToolExportTest::writtenFilesWithRenderingIssues()
{
    // 339:6505: "PDF to Images finished", "3 images were written to the folder images.", Files,
    // Total size, Time; Rendering issues with the row of page 2; Close at the leading edge, Open Folder
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 3, true));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("images"));
    QDir().mkpath(folder);
    H::setOutput(shell, folder);

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::WrittenFiles);
    QVERIFY(!dialog->isFinishedWithErrors());
    QCOMPARE(H::keys(dialog->getFacts()), QStringList({ "Files", "Total size", "Time" }));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("3 PNG · Report_1.png … Report_3.png"));
    const pdf::PDFAppParagraph* subtitle = dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultSubtitle"));
    QVERIFY(subtitle);
    QCOMPARE(subtitle->text(), QStringLiteral("3 images were written to the folder images."));

    const pdf::PDFLabel* issuesTitle = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultIssuesTitle"));
    QVERIFY(issuesTitle);
    QCOMPARE(issuesTitle->text(), QStringLiteral("Rendering issues"));
    const pdf::PDFTag* issuesTag = dialog->findChild<pdf::PDFTag*>(QStringLiteral("toolResultIssuesTag"));
    QVERIFY(issuesTag);
    QVERIFY(issuesTag->text().startsWith(QStringLiteral("1 ")));
    QCOMPARE(H::rowTitles(dialog->findChild<QWidget*>(QStringLiteral("toolResultIssueList"))), QStringList({ QStringLiteral("Page 2") }));

    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Open Folder"));
    QVERIFY(!dialog->getRetryButton());
    // 339:6505: Close at the leading edge of the dialog, Open Folder at the trailing one
    QVERIFY(dialog->getCloseButton()->mapTo(dialog, QPoint(dialog->getCloseButton()->width(), 0)).x() < dialog->width() / 2);
    QVERIFY(dialog->getConfirmButton()->mapTo(dialog, QPoint(0, 0)).x() > dialog->width() / 2);
    QCOMPARE(dialog->getStatusText(), QStringLiteral("PDF to Images finished · 3 files written"));
    fixture.screenshot(QStringLiteral("convert-pdf-to-images-done"));

    QPointer<PDFDocumentToolShell> guard(shell);
    dialog->getCloseButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&guard]() { return !guard || guard->getState() == PDFDocumentToolShell::State::Closed; }));
}

void AppToolExportTest::failedFileAndRetry()
{
    // 339:7293: the file of page 2 cannot be written (a folder has its name): "PDF to Images finished
    // with errors", Written, Failed, Time, the red list, Choose Folder… and Retry N Files; Retry writes
    // only that file again with the same settings
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 3, false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("images"));
    QDir().mkpath(folder);
    H::setOutput(shell, folder);
    QVERIFY(QDir(folder).mkpath(QStringLiteral("Report_2.png")));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isFinishedWithErrors());
    const pdf::PDFAppParagraph* title = dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultTitle"));
    QVERIFY(title);
    QCOMPARE(title->text(), QStringLiteral("PDF to Images finished with errors"));
    QCOMPARE(H::keys(dialog->getFacts()), QStringList({ "Written", "Failed", "Time" }));
    QVERIFY(dialog->getFacts()->getValue(0).startsWith(QStringLiteral("2 PNG · ")));
    QCOMPARE(H::rowTitles(dialog->findChild<QWidget*>(QStringLiteral("toolResultFailedList"))), QStringList({ QStringLiteral("Report_2.png") }));
    QVERIFY(dialog->getRetryButton() && dialog->getChooseFolderButton());
    QVERIFY(dialog->getRetryButton()->text().startsWith(QStringLiteral("Retry 1 ")));
    QCOMPARE(dialog->getChooseFolderButton()->text(), QStringLiteral("Choose Folder…"));
    QVERIFY(!dialog->getConfirmButton()->isVisible());
    QVERIFY(dialog->getStatusText().startsWith(QStringLiteral("PDF to Images finished · 1 of ")));
    fixture.screenshot(QStringLiteral("convert-pdf-to-images-errors"));

    // The cause is removed, the first file deleted: Retry writes the second file only
    QVERIFY(QDir(folder).rmdir(QStringLiteral("Report_2.png")));
    QVERIFY(QFile::remove(QDir(folder).filePath(QStringLiteral("Report_1.png"))));
    QPointer<pdf::PDFAppToolResultDialog> firstDialog(dialog);
    dialog->getRetryButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell, &firstDialog]() { return !firstDialog && shell->getResultDialog() && shell->getState() == PDFDocumentToolShell::State::Deciding; }));
    pdf::PDFAppToolResultDialog* retried = shell->getResultDialog();
    QVERIFY(retried);
    QVERIFY(!retried->isFinishedWithErrors());
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Report_2.png"))));
    QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral("Report_1.png"))));
    QCOMPARE(retried->getFacts()->getValue(0), QStringLiteral("1 PNG · Report_2.png"));
    retried->getCloseButton()->click();
}

void AppToolExportTest::chooseFolderWritesTheFailedFiles()
{
    // 339:7293 (K4.11): Choose Folder… asks for a folder and writes only the failed files there
    // with the same settings; a canceled question keeps the dialog
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 3, false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(shell);
    const QString folder = fixture.path(QStringLiteral("images"));
    const QString otherFolder = fixture.path(QStringLiteral("other"));
    QDir().mkpath(folder);
    QDir().mkpath(otherFolder);
    H::setOutput(shell, folder);
    QVERIFY(QDir(folder).mkpath(QStringLiteral("Report_2.png")));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog && dialog->getChooseFolderButton());

    // Canceled: nothing happens, the dialog stays
    QString answer;
    pdf::PDFAppFileDialogs::setFolderHandler([&answer](QWidget*, const QString&) { return answer; });
    dialog->getChooseFolderButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultDialog(), dialog);
    QVERIFY(QDir(otherFolder).entryList(QDir::Files).isEmpty());

    // The other folder: only Report_2.png is written there
    answer = otherFolder;
    QPointer<pdf::PDFAppToolResultDialog> firstDialog(dialog);
    dialog->getChooseFolderButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell, &firstDialog]() { return !firstDialog && shell->getResultDialog() && shell->getState() == PDFDocumentToolShell::State::Deciding; }));
    QCOMPARE(QDir(otherFolder).entryList(QDir::Files), QStringList({ QStringLiteral("Report_2.png") }));
    QVERIFY(!shell->getResultDialog()->isFinishedWithErrors());
    shell->getResultDialog()->getCloseButton()->click();
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
}

void AppToolExportTest::renderContextOfTheTab()
{
    // R4-10 b, owner answer Q4.6: in a document tab PDF to Images renders by the layers of the tab
    // and by the rendering settings (Show annotations off), not by the defaults of the core. A page
    // of 100 x 100 pt: the upper right quarter red in a layer, which /D shows; at the bottom (y 0 to
    // 20 pt) a Square annotation with /F 4 (Print) and a blue appearance.
    using H = ExportGuiHelper;
    const auto parse = [](const QByteArray& text) { return pdf::PDFParser(text, nullptr, pdf::PDFParser::None).getObject(); };
    const auto ref = [](pdf::PDFObjectReference reference) { return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R"; };
    const auto stream = [](pdf::PDFDocumentBuilder& builder, const pdf::PDFObject& dictionaryObject, const QByteArray& data)
    {
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(data))));
    };

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference layer = builder.addObject(parse("<< /Type /OCG /Name (Red) >>"));
    builder.mergeTo(builder.getCatalogReference(), parse("<< /OCProperties << /OCGs [" + ref(layer) + "] /D << /ON [" + ref(layer) + "] >> >> >>"));
    const pdf::PDFObjectReference appearance = stream(builder, parse("<< /Type /XObject /Subtype /Form /BBox [0 0 100 20] >>"), "0 0 1 rg 0 0 100 20 re f");
    const pdf::PDFObjectReference annotation = builder.addObject(parse("<< /Type /Annot /Subtype /Square /Rect [0 0 100 20] /F 4 /AP << /N " + ref(appearance) + " >> >>"));
    const pdf::PDFObjectReference contents = stream(builder, parse("<< >>"), "/OC /oc1 BDC 1 0 0 rg 50 20 50 80 re f EMC");
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100, 100));
    builder.mergeTo(page, parse("<< /Resources << /Properties << /oc1 " + ref(layer) + " >> >> /Contents " + ref(contents) + " /Annots [" + ref(annotation) + "] >>"));
    pdf::PDFDocument document = builder.build();

    AppToolShellFixture fixture(QSize(1440, 960));
    const QString filePath = fixture.path(QStringLiteral("Layers.pdf"));
    QVERIFY(AppDocumentSamples::write(document, filePath));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);

    // The tab hides the layer, the settings hide the annotations
    pdf::PDFOptionalContentActivity* activity = tab->getDocument()->getOptionalContentActivity();
    QVERIFY(activity);
    activity->setState(layer, pdf::OCState::OFF);
    pdf::PDFAppSettingsValueStore store(fixture.context.get());
    QVERIFY(store.isAvailable());
    QCOMPARE(store.writeValue(pdf::PDFSettingKeys::viewingShowAnnotations(), false), QString());
    AppToolShellFixture::settle();

    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(shell);

    // The environment of the run: copies of the states of the tab and of the settings
    const pdf::PDFAppToolRunEnvironment environment = shell->getHost()->createEnvironment();
    QVERIFY(environment.rendering.layerStates.has_value());
    QCOMPARE(environment.rendering.layerStates->at(layer), pdf::OCState::OFF);
    QVERIFY(environment.rendering.features.has_value());
    QVERIFY(!environment.rendering.features->testFlag(pdf::PDFRenderer::DisplayAnnotations));
    QVERIFY(environment.rendering.cmsSettings.has_value());

    const QString folder = fixture.path(QStringLiteral("images"));
    QDir().mkpath(folder);
    H::setOutput(shell, folder);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    QVERIFY(shell->getResultDialog());

    // 72 dpi: 100 x 100 px; the layer quarter and the annotation are the white paper
    const QImage image(QDir(folder).filePath(QStringLiteral("Layers_1.png")));
    QCOMPARE(image.size(), QSize(100, 100));
    QCOMPARE(QColor(image.pixel(75, 40)), QColor(Qt::white));
    QCOMPARE(QColor(image.pixel(50, 90)), QColor(Qt::white));
    shell->getResultDialog()->getCloseButton()->click();
}

void AppToolExportTest::imageFieldsOfTheCards()
{
    // 341:7516 Image of Extract Images: a card with the fields Compression and Quality, each with its
    // label beside it (as 339:3546 of PDF to Images, one shared row), and the note; -1 (the default
    // of the format, the parameter definition) is said by the word Default in both tools
    using H = ExportGuiHelper;
    const auto fieldsOf = [](const QWidget* card)
    {
        QStringList fields;
        for (const pdf::PDFSpinBox* spinBox : card->findChildren<pdf::PDFSpinBox*>())
        {
            QStringList labels;
            for (const QLabel* label : spinBox->parentWidget()->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
            {
                labels << label->text();
            }
            fields << QStringLiteral("%1=%2:%3").arg(labels.join(QLatin1Char('|')), spinBox->accessibleName(), spinBox->text());
        }
        return fields;
    };

    // The fields go into the card's own layout: a second layout on a card is refused by Qt (this
    // warning) and leaks with its items (ASan of step 4)
    QTest::failOnWarning(QRegularExpression(QStringLiteral("which already has a layout")));

    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImagePage(fixture.path(QStringLiteral("Scan.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QStringLiteral("convert.extract-images"));
    QVERIFY(shell && shell->getGUI());
    AppToolShellFixture::settle();
    const QWidget* card = fixture.window->findChild<QWidget*>(QStringLiteral("toolExtractImagesImageCard"));
    QVERIFY(card && card->isVisible());
    QCOMPARE(fieldsOf(card), QStringList({ QStringLiteral("Compression=Compression:Default"), QStringLiteral("Quality=Quality:Default") }));
    const QLabel* note = card->findChild<QLabel*>(QStringLiteral("toolExtractImagesImageNote"));
    QVERIFY(note);
    QCOMPARE(note->text(), QStringLiteral("Images keep their pixel size. Colors are converted with the current color management settings."));

    // A value of the user is a number again
    QVERIFY(shell->getModel()->setValue(QStringLiteral("quality"), 50).isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(fieldsOf(card).at(1), QStringLiteral("Quality=Quality:50"));

    // The status bar of 341:7516 ends with the estimate ("… will be exported · about 386 MB"), not
    // with the size of the file
    pdf::PDFAppToolGUIExtractImages* extract = qobject_cast<pdf::PDFAppToolGUIExtractImages*>(shell->getGUI());
    QVERIFY(extract);
    QVERIFY(AppToolShellFixture::waitFor([extract]() { return !extract->isComputing() && extract->getPlan().has_value(); }, 60000));
    AppToolShellFixture::settle();
    const QString status = shell->getStatusMessage();
    QVERIFY2(status.startsWith(QStringLiteral("Extract Images · Scan.pdf · ")), qPrintable(status));
    const qsizetype about = status.lastIndexOf(QStringLiteral(" · about "));
    QVERIFY2(about > 0 && !status.mid(about + 9).contains(QStringLiteral(" · ")), qPrintable(status));

    // PDF to Images 339:3546: the same rows and the same word
    pdf::PDFAppDocumentTab* reportTab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 1, false));
    QVERIFY(reportTab);
    pdf::PDFAppToolGUIPdfToImages* gui = H::gui(fixture.openTool(reportTab, H::TOOL_ID));
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCompressionControl()->getSpinBox()->text(), QStringLiteral("Default"));
    QCOMPARE(gui->getQualityControl()->getSpinBox()->text(), QStringLiteral("Default"));
    QCOMPARE(gui->getCompressionControl()->getSpinBox()->accessibleName(), QStringLiteral("Compression"));

    // 339:3546 Resolution "300 dpi", Longer side "2000 px": the units are the muted suffixes (RS4FIGMA),
    // not a part of the edited text
    QCOMPARE(gui->getDpiControl()->getSpinBox()->getDisplaySuffix(), QStringLiteral("dpi"));
    QCOMPARE(gui->getPixelsControl()->getSpinBox()->getDisplaySuffix(), QStringLiteral("px"));
    QVERIFY(gui->getDpiControl()->getSpinBox()->suffix().isEmpty());
}

void AppToolExportTest::temporaryOutputIsCopied()
{
    // Owner 14. 11.: an output in a temporary file (R4-1) is copied into the target folder, never
    // renamed - the saved file is created in its folder and gets its permissions; the temporary
    // file stays (a second Save as or a retry writes it again)
    AppToolShellFixture fixture(QSize(1440, 960));
    QVERIFY(QDir().mkpath(fixture.path(QStringLiteral("temporary"))));
    QVERIFY(QDir().mkpath(fixture.path(QStringLiteral("target"))));
    QString error;
    std::shared_ptr<pdf::PDFDocumentToolTemporaryFile> temporary = pdf::PDFDocumentToolTemporaryFile::create(fixture.path(QStringLiteral("temporary")), QStringLiteral(".wav"), &error);
    QVERIFY2(temporary, qPrintable(error));
    const QByteArray content("RIFF-content-of-the-output");
    {
        QFile file(temporary->getFilePath());
        QVERIFY(file.open(QFile::WriteOnly));
        QCOMPARE(file.write(content), content.size());
    }
    const pdf::PDFDocumentToolOutputFile output = pdf::PDFDocumentToolOutputFile::createTemporary(QStringLiteral("Book.wav"), QStringLiteral("audio/wav"), temporary);

    const QString target = fixture.path(QStringLiteral("target/Book.wav"));
    const pdf::PDFAppToolResultWriter::Result result = pdf::PDFAppToolResultWriter::writeOutputFile(output, target, QString(), false);
    QVERIFY2(result.isWritten, qPrintable(result.errorMessage));
    QFile written(target);
    QVERIFY(written.open(QFile::ReadOnly));
    QCOMPARE(written.readAll(), content);
    QVERIFY(!temporary->isMoved());
    QVERIFY(QFileInfo::exists(temporary->getFilePath()));

    // The same output once more into another file of the folder
    const QString second = fixture.path(QStringLiteral("target/Book-2.wav"));
    QVERIFY(pdf::PDFAppToolResultWriter::writeOutputFile(output, second, QString(), false).isWritten);
    QCOMPARE(QFileInfo(second).size(), qint64(content.size()));
}

void AppToolExportTest::undoOfThePageRange()
{
    // RS4APP: Undo of a page range of Extract Images (341:7936) returns all pages - the menu Pages
    // follows the value ("Pages: All"), it does not stay at Range
    using Mode = pdf::PDFAppToolGUIExtractImages::PagesMode;
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImagePage(fixture.path(QStringLiteral("Scan.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QStringLiteral("convert.extract-images"));
    pdf::PDFAppToolGUIExtractImages* gui = shell ? qobject_cast<pdf::PDFAppToolGUIExtractImages*>(shell->getGUI()) : nullptr;
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPagesButton()->text(), QStringLiteral("Pages: All"));

    gui->setPagesMode(Mode::Range, QStringLiteral("1"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPagesMode(), Mode::Range);
    QCOMPARE(gui->getPagesButton()->text(), QStringLiteral("Pages: Range"));

    shell->getToolbar()->getUndoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPagesMode(), Mode::All);
    QCOMPARE(gui->getPagesButton()->text(), QStringLiteral("Pages: All"));

    // Redo returns the range
    shell->getToolbar()->getRedoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPagesMode(), Mode::Range);
}

void AppToolExportTest::copyImageWaitsForTheNewList()
{
    // RS4APP: Copy Image right after a new version of the document (before its list of the images
    // is collected) never decodes the item of the old list in the new document; with the new
    // list it copies the image
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImagePage(fixture.path(QStringLiteral("Scan.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QStringLiteral("convert.extract-images"));
    pdf::PDFAppToolGUIExtractImages* gui = shell ? qobject_cast<pdf::PDFAppToolGUIExtractImages*>(shell->getGUI()) : nullptr;
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isComputing() && gui->getPlan().has_value() && gui->getPlan()->images.size() == 1; }, 60000));
    pdf::PDFAppToolImageList* list = gui->getImageListWidget();
    list->setSelectedKeys({ list->getModel()->getImageList()->getItems().front().key });
    QVERIFY(AppToolShellFixture::waitFor([list]() { return list->getCurrentKey().isValid(); }));

    // A new version (the same content): the list of the old version is still shown
    const std::shared_ptr<const pdf::PDFDocumentImageList> oldList = list->getModel()->getImageList();
    QVERIFY(oldList);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(*document->getDocument()));
    // (the assistant may change the document while a tool is open, the user may not)
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QApplication::clipboard()->clear();
    gui->copyImage();
    AppToolShellFixture::settle();
    QVERIFY(QApplication::clipboard()->image().isNull());

    // The list of the new version: the copy works
    QVERIFY(AppToolShellFixture::waitFor([gui, list, oldList]() { return list->getModel()->getImageList() && list->getModel()->getImageList() != oldList && !gui->isComputing(); }, 60000));
    list->setSelectedKeys({ list->getModel()->getImageList()->getItems().front().key });
    QVERIFY(AppToolShellFixture::waitFor([list]() { return list->getCurrentKey().isValid(); }));
    gui->copyImage();
    QVERIFY(AppToolShellFixture::waitFor([]() { return !QApplication::clipboard()->image().isNull(); }));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(2, 2));
}

void AppToolExportTest::estimateKeyAndCancellation()
{
    // RS4APP: the estimate of PDF to Images belongs to the version of the document (its identity,
    // never the address of a document) and to the rendering of the run; a canceled estimate stops
    // and says nothing
    using H = ExportGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 2, false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    pdf::PDFAppToolGUIPdfToImages* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isEstimating() && gui->getEstimatedSize() > 0; }));

    // A new version of the document: the estimate of the old one is not shown, a new one comes
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(*document->getDocument()));
    // (the assistant may change the document while a tool is open, the user may not)
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QCOMPARE(gui->getEstimatedSize(), qint64(-1));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isEstimating() && gui->getEstimatedSize() > 0; }));

    // Other rendering settings (no annotations): the estimate of the old rendering is not shown
    pdf::PDFAppSettingsValueStore store(fixture.context.get());
    QCOMPARE(store.writeValue(pdf::PDFSettingKeys::viewingShowAnnotations(), false), QString());
    QCOMPARE(gui->getEstimatedSize(), qint64(-1));

    // The worker: a canceled estimate renders nothing and says -1
    const pdf::PDFDocumentPointer pdf = document->getDocument();
    const QVariantMap values = shell->getModel()->getValues();
    const std::shared_ptr<pdf::PDFAppCancelFlag> running = std::make_shared<pdf::PDFAppCancelFlag>();
    QVERIFY(pdf::PDFAppToolGUIPdfToImages::estimatePageBytes(pdf, 0, values, pdf::PDFDocumentToolRenderContext(), running) > 0);
    const std::shared_ptr<pdf::PDFAppCancelFlag> canceled = std::make_shared<pdf::PDFAppCancelFlag>();
    canceled->cancel();
    QCOMPARE(pdf::PDFAppToolGUIPdfToImages::estimatePageBytes(pdf, 0, values, pdf::PDFDocumentToolRenderContext(), canceled), qint64(-1));
}

void AppToolExportTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> writes the frames under their ids (339:3546, 339:4239,
    // 356:11387, 339:6505, 339:7293) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color (CLAUDE.md, Fluent)
    using H = ExportGuiHelper;
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    // The style of the application (the other functions of this test run with the platform style;
    // this one is the last, the style stays)
    QApplication::setStyle(new pdf::PDFWidgetStyle());

    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString name = QString::fromLatin1(variant);
        const QString suffix = name == QStringLiteral("light") ? QString() : QStringLiteral("-") + name;
        pdf::PDFThemeManager::getInstance()->setTheme(name == QStringLiteral("dark") ? QStringLiteral("dark") : QStringLiteral("light"));
        QApplication::setLayoutDirection(name == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReport(fixture.path(QStringLiteral("Report.pdf")), 3, true));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
        pdf::PDFAppToolGUIPdfToImages* gui = H::gui(shell);
        QVERIFY(gui);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isEstimating(); }));
        QTest::qWait(1500);     // the picture of the page of the canvas (rendered in the background)
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("convert-pdf-to-images-339-3546") + suffix)), 0);

        gui->getFormatButton()->click();
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("convert-pdf-to-images-339-4239-format") + suffix);
        QTest::mouseClick(gui->getFormatPanel()->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFormat-more")), Qt::LeftButton);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("convert-pdf-to-images-356-11387-allformats") + suffix);
        gui->getFormatPanel()->close();
        AppToolShellFixture::settle();
        gui->getResolutionButton()->click();
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("convert-pdf-to-images-339-4239-resolution") + suffix);
        gui->getResolutionPanel()->close();
        AppToolShellFixture::settle();

        QVERIFY(shell->getModel()->setValue(QStringLiteral("sizeBy"), QStringLiteral("pixels")).isEmpty());
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("convert-pdf-to-images-356-11387") + suffix)), 0);
        QVERIFY(shell->getModel()->setValue(QStringLiteral("sizeBy"), QStringLiteral("dpi")).isEmpty());

        const QString folder = fixture.path(QStringLiteral("images"));
        QDir().mkpath(folder);
        H::setOutput(shell, folder);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("convert-pdf-to-images-339-6505-dialog") + suffix);
        QPointer<PDFDocumentToolShell> shellGuard(shell);
        shell->getResultDialog()->getCloseButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([&shellGuard]() { return !shellGuard || shellGuard->getState() == PDFDocumentToolShell::State::Closed; }));
        AppToolShellFixture::settle();

        // 339:7293: a folder has the name of the file of page 2
        shell = fixture.openTool(tab, H::TOOL_ID);
        QVERIFY(shell);
        const QString errorFolder = fixture.path(QStringLiteral("errors"));
        QDir().mkpath(errorFolder);
        H::setOutput(shell, errorFolder);
        QVERIFY(QDir(errorFolder).mkpath(QStringLiteral("Report_2.png")));
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("convert-pdf-to-images-339-7293-dialog") + suffix);
        shellGuard = shell;
        shell->getResultDialog()->getCloseButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([&shellGuard]() { return !shellGuard || shellGuard->getState() == PDFDocumentToolShell::State::Closed; }));
        AppToolShellFixture::settle();
    }
}

QTEST_MAIN(AppToolExportTest)

#include "tst_apptoolexporttest.moc"
