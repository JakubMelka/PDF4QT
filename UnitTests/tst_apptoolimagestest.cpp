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

// Images to PDF and Interleave Pages in the application (package D of step 3a of phase 2, plan
// 8): the frames 385:13263 (6 + 6 reversed: front 1, back 6, front 2, ...), 385:13738 (no scans),
// 385:14487 (7 + 5), the slots and Swap with Undo (K28), the rules of the list (K41: two PDF
// files, images only), 336:1976 (the rows, the page of the selected image, its caption, the
// summary), the turn of an image (5.8: the page follows the turned image), the image page
// preview (one rule with the core), Open in this tab (R16).

#include "apptoolowntabfixture.h"

#include "pdfappimagepagepreview.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolgui_interleave.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltab.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumenttool_images.h"
#include "pdfpagesizepresets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include "pdfanchorgrid.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapphomedialogs.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfappmainwindow.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfcms.h"
#include "pdfdocumenttoolrunner.h"

#include <QApplication>
#include <QColorSpace>
#include <QImage>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTimer>
#include <QtMath>
#include <QtTest>

#include <cmath>

using pdftest::AppToolOwnTabFixture;
using pdftest::AppDocumentSamples;

class AppToolImagesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void inputRulesOfTheTools();
    void interleaveFrameOfTwoScans();
    void interleaveSwapAndUndo();
    void interleaveUnevenScans();
    void interleaveWithoutScans();
    void imagesFrameOfTwoImages();
    void imagesTurnDuplicateRemove();
    void imagesRunParametersAndThisTab();
    void imagePagePreviewFollowsLayout();
    void imagesCropOfAnImage();
    void imagesCropOfATurnedImageAndAll();
    void imagesSelectionOfMoreImages();
    void imagesRename();
    void imagesAnchorComboBox();
    void imagesRowThumbnails();
    void imagesColorManagedPreview();
    void themesDirectionAndScreenshots();
    void imagesSaveAsFileReplacesTab();
    void imagesSaveAsFileNotOpenedKeepsTab();
    void imagesCancelAfterRun();
    void imagesCaptionAtThePage();
};

/// Helpers of the test
class ImagesTestHelper
{
public:
    ImagesTestHelper() = delete;

    static bool waitLoaded(const pdf::PDFAppToolInputList* list)
    {
        return AppToolOwnTabFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; });
    }

    /// An own tab of the tool with the files (read before the tab is shown)
    static pdf::PDFAppToolTab* open(AppToolOwnTabFixture& fixture, const char* toolId, const QStringList& files)
    {
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles(files);
        if (!waitLoaded(list))
        {
            delete list;
            return nullptr;
        }
        pdf::PDFAppToolTab* tab = fixture.createToolTab(QString::fromLatin1(toolId), list);
        AppToolOwnTabFixture::settle();
        return tab;
    }

    template<typename T>
    static T* gui(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFAppToolParameterModel* model(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? shell->getModel() : nullptr;
    }

    static QString status(const pdf::PDFAppToolTab* tab)
    {
        return tab->getToolPage()->getTabView()->getToolMessage();
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

    /// The captions of the tiles of the canvas of Interleave
    static QStringList captions(const pdf::PDFAppInterleaveToolGUI* gui)
    {
        QStringList result;
        const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();
        for (int row = 0; row < model->rowCount(); ++row)
        {
            result << model->data(model->index(row, 0), pdf::PDFComponentRoles::SubtitleRole).toString();
        }
        return result;
    }

    static bool isNear(double value, double expected, double tolerance = 0.5)
    {
        return std::abs(value - expected) <= tolerance;
    }
};

using H = ImagesTestHelper;

void AppToolImagesTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolImagesTest::inputRulesOfTheTools()
{
    // K41: Merge 2 to 1000 of both kinds, Interleave exactly two PDF files in its slots, Images to
    // PDF 1 to 1000 images
    AppToolOwnTabFixture fixture;
    const pdf::PDFAppToolInputRules merge = pdf::PDFAppToolInputChooser::getInputRules(QStringLiteral("pages.merge"));
    QCOMPARE(merge.minimum, 2);
    QCOMPARE(merge.maximum, 1000);
    QVERIFY(merge.acceptsPdf && merge.acceptsImages);

    const pdf::PDFAppToolInputRules interleave = pdf::PDFAppToolInputChooser::getInputRules(QStringLiteral("pages.interleave"));
    QCOMPARE(interleave.minimum, 2);
    QCOMPARE(interleave.maximum, 2);
    QVERIFY(interleave.acceptsPdf && !interleave.acceptsImages);
    QCOMPARE(interleave.slotTitles, QStringList({ QStringLiteral("Front sides"), QStringLiteral("Back sides") }));

    const pdf::PDFAppToolInputRules images = pdf::PDFAppToolInputChooser::getInputRules(QStringLiteral("convert.images-to-pdf"));
    QCOMPARE(images.minimum, 1);
    QCOMPARE(images.maximum, 1000);
    QVERIFY(!images.acceptsPdf && images.acceptsImages);

    // The list of the chooser of Interleave takes two PDF files: the third is refused, an image
    // is Unsupported with its reason (K29); the places are the slots
    pdf::PDFAppToolInputList list(nullptr);
    pdf::PDFAppToolInputChooser chooser(nullptr, QStringLiteral("pages.interleave"), &list, nullptr);
    QCOMPARE(list.getMaximumCount(), 2);
    QVERIFY(!list.areImagesAccepted());
    QCOMPARE(chooser.getPlaceText(0), QStringLiteral("Front sides"));
    QCOMPARE(chooser.getPlaceText(1), QStringLiteral("Back sides"));
    QCOMPARE(chooser.getPlaceText(2), QStringLiteral("3rd in the result"));

    const QString image = H::png(fixture.folder(), QStringLiteral("photo.png"), QSize(40, 30), 300);
    list.addFile(image);
    list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(H::waitLoaded(&list));
    QCOMPARE(list.getInput(0)->state, pdf::PDFAppToolInputState::Unsupported);
    QVERIFY(list.getInput(0)->reason.contains(QStringLiteral("needs PDF files")));
    QCOMPARE(list.getInput(1)->state, pdf::PDFAppToolInputState::Ready);
    const pdf::PDFAppToolInputList::AddResult third = list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2));
    QCOMPARE(third.refusal, pdf::PDFAppToolInputList::Refusal::TooMany);
    QCOMPARE(pdf::PDFAppToolInputList::getRefusalText(third.refusal, third.name, list.getMaximumCount()), QStringLiteral("The list has 2 files, no more can be added"));
}

void AppToolImagesTest::interleaveFrameOfTwoScans()
{
    // pages-interleave 385:13263: scan_front.pdf and scan_back.pdf, 6 + 6, Back sides: Reversed
    AppToolOwnTabFixture fixture;
    const QString front = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_front.pdf"), 6);
    const QString back = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_back.pdf"), 6);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolInterleave::TOOL_ID, { front, back });
    QVERIFY(tab);
    pdf::PDFAppInterleaveToolGUI* gui = H::gui<pdf::PDFAppInterleaveToolGUI>(tab);
    QVERIFY(gui);

    // Sources: "2 files · 6 + 6 pages", the slots with their captions and their scans
    QCOMPARE(gui->getSourcesHeader()->getSubtitleLabel()->text(), QStringLiteral("2 files · 6 + 6 pages"));
    QCOMPARE(gui->getSlotCaption(pdf::PDFAppInterleaveToolGUI::FrontSlot)->text(), QStringLiteral("Front sides · result pages 1, 3, 5 …"));
    QCOMPARE(gui->getSlotCaption(pdf::PDFAppInterleaveToolGUI::BackSlot)->text(), QStringLiteral("Back sides · result pages 2, 4, 6 …"));
    const pdf::PDFAppToolInputRow::Content frontRow = gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot)->getContent();
    const pdf::PDFAppToolInputRow::Content backRow = gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::BackSlot)->getContent();
    QCOMPARE(frontRow.letter, QStringLiteral("A"));
    QCOMPARE(frontRow.name, QStringLiteral("scan_front.pdf"));
    QVERIFY(frontRow.firstLine.startsWith(QStringLiteral("6 pages")));
    QVERIFY(frontRow.secondLine.isEmpty());
    QCOMPARE(backRow.letter, QStringLiteral("B"));
    QCOMPARE(backRow.secondLine, QStringLiteral("Read from the last page to the first"));
    QCOMPARE(frontRow.linkText, QStringLiteral("Change…"));
    QVERIFY(!gui->getSlotEmpty(pdf::PDFAppInterleaveToolGUI::FrontSlot)->isVisible());

    // The canvas: "Result · 12 pages", front 1, back 6, front 2, back 5 ... (5.7), the dots blue
    // (front) and green (back), the hint with the names
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Result · 12 pages"));
    QCOMPARE(H::captions(gui), QStringList({ "front 1", "back 6", "front 2", "back 5", "front 3", "back 4", "front 4", "back 3", "front 5", "back 2", "front 6", "back 1" }));
    const pdf::PDFAppToolMergePageModel* model = gui->getCanvas()->getModel();
    QVERIFY(qobject_cast<pdf::PDFAppToolPageTileDelegate*>(gui->getCanvas()->getDelegate()));
    QCOMPARE(model->data(model->index(0, 0), pdf::PDFComponentRoles::HueRole).toInt(), int(pdf::PDFAccentHue::Blue));
    QCOMPARE(model->data(model->index(1, 0), pdf::PDFComponentRoles::HueRole).toInt(), int(pdf::PDFAccentHue::Green));
    QCOMPARE(gui->getCanvas()->getHintLabel()->text(), QStringLiteral("Blue pages come from scan_front.pdf, green pages from scan_back.pdf. Check that every sheet has its two sides next to each other."));

    // Summary and the status bar
    QCOMPARE(gui->getSummary()->getValue(0), QStringLiteral("6 pages"));
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("6 pages · reversed"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("None"));
    QCOMPARE(gui->getSummary()->getValue(3), QStringLiteral("12 pages"));
    QVERIFY(!gui->getUnevenCallout()->isVisible());
    QCOMPARE(H::status(tab), QStringLiteral("Interleave Pages · 6 front and 6 back sides → 12 pages · the source files are not changed"));
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("12 pages"));

    // Back sides: In order - back 1 after front 1, the second line of the back row goes
    QVERIFY(H::model(tab)->setValue(QStringLiteral("backSides"), QStringLiteral("in-order")).isEmpty());
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::captions(gui).mid(0, 4), QStringList({ "front 1", "back 1", "front 2", "back 2" }));
    QVERIFY(gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::BackSlot)->getContent().secondLine.isEmpty());
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("6 pages"));
}

void AppToolImagesTest::interleaveSwapAndUndo()
{
    // K28: Swap is one undo step; the front sides are then the file of the back sides
    AppToolOwnTabFixture fixture;
    const QString front = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("front.pdf"), 3);
    const QString back = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("back.pdf"), 2);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolInterleave::TOOL_ID, { front, back });
    QVERIFY(tab);
    pdf::PDFAppInterleaveToolGUI* gui = H::gui<pdf::PDFAppInterleaveToolGUI>(tab);
    QVERIFY(gui);
    QVERIFY(!H::model(tab)->canUndo());

    gui->swapSlots();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot)->getContent().name, QStringLiteral("back.pdf"));
    QCOMPARE(gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::BackSlot)->getContent().name, QStringLiteral("front.pdf"));
    QCOMPARE(gui->getSourcesHeader()->getSubtitleLabel()->text(), QStringLiteral("2 files · 2 + 3 pages"));
    // 2 + 3 reversed: front 1, back 3, front 2, back 2, back 1 (the back side without a pair at the end)
    QCOMPARE(H::captions(gui), QStringList({ "front 1", "back 3", "front 2", "back 2", "back 1" }));
    QVERIFY(H::model(tab)->canUndo());
    QCOMPARE(gui->getRunParameters().value(QStringLiteral("inputs")).toString(), tab->getInputList()->getInputIds().join(QLatin1Char(',')));

    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot)->getContent().name, QStringLiteral("front.pdf"));
    QCOMPARE(H::captions(gui), QStringList({ "front 1", "back 2", "front 2", "back 1", "front 3" }));
}

void AppToolImagesTest::interleaveUnevenScans()
{
    // pages-interleave-uneven 385:14487: 7 + 5 - "2 front sides have no back side", front 6 and
    // front 7 at the end; Extra pages: Leave out - 10 pages
    AppToolOwnTabFixture fixture;
    const QString front = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_front.pdf"), 7);
    const QString back = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_back.pdf"), 5);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolInterleave::TOOL_ID, { front, back });
    QVERIFY(tab);
    pdf::PDFAppInterleaveToolGUI* gui = H::gui<pdf::PDFAppInterleaveToolGUI>(tab);
    QVERIFY(gui);

    QVERIFY(gui->getUnevenCallout()->isVisible());
    QCOMPARE(gui->getUnevenCallout()->getTitleLabel()->text(), QStringLiteral("2 front sides have no back side"));
    QCOMPARE(gui->getUnevenCallout()->getTextLabel()->text(), QStringLiteral("scan_front.pdf has 7 pages, scan_back.pdf only 5. The extra pages are placed at the end of the result. Check whether a sheet was skipped while scanning, or choose Extra pages: Leave out."));
    QCOMPARE(H::status(tab), QStringLiteral("Interleave Pages · the files differ by 2 pages – 2 front sides have no back side"));
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Result · 12 pages"));
    QCOMPARE(H::captions(gui).mid(10), QStringList({ "front 6", "front 7" }));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("2 · at the end"));

    QVERIFY(H::model(tab)->setValue(QStringLiteral("extraPages"), QStringLiteral("leave-out")).isEmpty());
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCanvas()->getHeaderLabel()->text(), QStringLiteral("Result · 10 pages"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("2 · left out"));
    QCOMPARE(gui->getSummary()->getValue(3), QStringLiteral("10 pages"));
}

void AppToolImagesTest::interleaveWithoutScans()
{
    // pages-interleave-empty 385:13738: two empty slots, "Two files are needed", the drop zone,
    // How Interleave works; Create PDF is off
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolInterleave::TOOL_ID, {});
    QVERIFY(tab);
    pdf::PDFAppInterleaveToolGUI* gui = H::gui<pdf::PDFAppInterleaveToolGUI>(tab);
    QVERIFY(gui);
    QCOMPARE(gui->getSourcesHeader()->getSubtitleLabel()->text(), QStringLiteral("Two files are needed"));
    QVERIFY(gui->getSlotEmpty(pdf::PDFAppInterleaveToolGUI::FrontSlot)->isVisible());
    QVERIFY(gui->getSlotEmpty(pdf::PDFAppInterleaveToolGUI::BackSlot)->isVisible());
    QVERIFY(!gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot)->isVisible());
    QVERIFY(!gui->getSwapButton()->isEnabled());
    QVERIFY(gui->getCanvas()->isEmpty());
    QVERIFY(gui->getHowSection()->isVisible());
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Interleave Pages · choose the file with the front sides and the file with the back sides"));

    // A file into the back slot: the front slot stays empty, nothing runs yet
    gui->setSlotFile(pdf::PDFAppInterleaveToolGUI::FrontSlot, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("only.pdf"), 2));
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot)->isVisible());
    QVERIFY(gui->getSlotEmpty(pdf::PDFAppInterleaveToolGUI::BackSlot)->isVisible());
    QVERIFY(!gui->isConfirmEnabled());
}

void AppToolImagesTest::imagesFrameOfTwoImages()
{
    // convert-images-to-pdf 336:1976: a landscape and a portrait image at 300 dpi, A4 with Auto
    // orientation - "Page 1 · A4 landscape", "Page 2 · A4 portrait"
    AppToolOwnTabFixture fixture;
    const QString landscape = H::png(fixture.folder(), QStringLiteral("IMG_2041.png"), QSize(400, 300), 300);
    const QString portrait = H::png(fixture.folder(), QStringLiteral("scan_002.png"), QSize(300, 400), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { landscape, portrait });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);

    const qint64 totalSize = QFileInfo(landscape).size() + QFileInfo(portrait).size();
    QCOMPARE(gui->getImagesPanel()->getTitleLabel()->text(), QStringLiteral("Images"));
    QCOMPARE(gui->getImagesPanel()->getSubtitleLabel()->text(), QStringLiteral("2 images · %1").arg(pdf::PDFDocumentToolFormat::formatSize(totalSize)));
    const std::vector<pdf::PDFAppToolInputRow*> rows = gui->getImagesPanel()->getRows();
    QCOMPARE(rows.size(), size_t(2));
    QCOMPARE(rows[0]->getContent().firstLine, QStringLiteral("400 × 300 px · PNG · %1").arg(pdf::PDFDocumentToolFormat::formatSize(QFileInfo(landscape).size())));
    QCOMPARE(rows[0]->getContent().secondLine, QStringLiteral("Page 1 · A4 landscape"));
    QCOMPARE(rows[1]->getContent().secondLine, QStringLiteral("Page 2 · A4 portrait"));

    // The canvas: the first image is selected; "Page 1 of 2 · IMG_2041.png", the caption
    QCOMPARE(gui->getSelectedImage(), tab->getInputList()->getInput(0)->id);
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 1 of 2 · IMG_2041.png"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel()->text(), QStringLiteral("A4 landscape · 297 × 210 mm · image fits inside 10 mm margins, centered"));

    // Selected image and Summary
    QCOMPARE(gui->getSelectedCard()->getTag()->text(), QStringLiteral("1 of 2"));
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(0), QStringLiteral("IMG_2041.png"));
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(2), QStringLiteral("400 × 300 px"));
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(3), QStringLiteral("None"));
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(5), QStringLiteral("A4 landscape"));
    // 336:1976: the rows of Selected image are 28 apart (no gap), as the rows of Summary
    QCOMPARE(gui->getSelectedCard()->getValues()->getRowRect(1).top() - gui->getSelectedCard()->getValues()->getRowRect(0).top(), 28);
    QCOMPARE(gui->getSummary()->getValue(0), QStringLiteral("2"));
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("A4, auto"));
    QCOMPARE(gui->getSummary()->getValue(2), QStringLiteral("Off"));
    QVERIFY(!gui->getLayoutFields()->getEditor(QStringLiteral("width"))->isVisibleTo(gui->getLayoutFields()));
    QVERIFY(!gui->getLayoutFields()->getEditor(QStringLiteral("height"))->isVisibleTo(gui->getLayoutFields()));

    // The second image: its page
    gui->selectImage(tab->getInputList()->getInput(1)->id);
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 2 of 2 · scan_002.png"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel()->text(), QStringLiteral("A4 portrait · 210 × 297 mm · image fits inside 10 mm margins, centered"));

    // Custom (356:9556): Width and Height appear
    QVERIFY(H::model(tab)->setValue(QStringLiteral("pageSize"), QStringLiteral("custom")).isEmpty());
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getLayoutFields()->getEditor(QStringLiteral("width"))->isVisibleTo(gui->getLayoutFields()));
    QVERIFY(gui->getLayoutFields()->getEditor(QStringLiteral("height"))->isVisibleTo(gui->getLayoutFields()));
    QCOMPARE(gui->getSummary()->getValue(1), QStringLiteral("Custom, auto"));
    QCOMPARE(H::status(tab), QStringLiteral("Images to PDF · 2 images · %1 · the PDF will have 2 pages, about %1").arg(pdf::PDFDocumentToolFormat::formatSize(totalSize)));
}

void AppToolImagesTest::imagesTurnDuplicateRemove()
{
    // 5.8: the turn of an image turns the image, Auto turns the page with it ("rotated 90°");
    // Duplicate adds one more page of the image with its turn; each is one undo step
    AppToolOwnTabFixture fixture;
    const QString landscape = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(400, 300), 300);
    const QString second = H::png(fixture.folder(), QStringLiteral("b.png"), QSize(400, 300), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { landscape, second });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    const QString firstId = tab->getInputList()->getInput(0)->id;
    const QString secondId = tab->getInputList()->getInput(1)->id;

    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getAdjustment(firstId).turn, pdf::PageRotation::Rotate90);
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().tag, QStringLiteral("rotated 90°"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().secondLine, QStringLiteral("Page 1 · A4 portrait"));
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A@90"));

    // The order changes, the turn stays with its image (its letter follows the order)
    gui->reverseOrder();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getInputIds(), QStringList({ secondId, firstId }));
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("B@90"));
    QCOMPARE(gui->getAdjustment(firstId).turn, pdf::PageRotation::Rotate90);

    // Duplicate: one more page of the selected image, with its turn
    gui->selectImage(firstId);
    gui->duplicateSelected();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 3);
    QCOMPARE(gui->getAdjustment(tab->getInputList()->getInput(2)->id).turn, pdf::PageRotation::Rotate90);

    // Remove, then Undo of every step brings the start back
    gui->removeSelected();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 2);
    for (int i = 0; i < 4; ++i)
    {
        H::model(tab)->undo();
        AppToolOwnTabFixture::settle();
    }
    QCOMPARE(tab->getInputList()->getInputIds(), QStringList({ firstId, secondId }));
    QVERIFY(H::model(tab)->getValue(QStringLiteral("adjustments")).toString().isEmpty());
    QVERIFY(!H::model(tab)->canUndo());
}

void AppToolImagesTest::imagesRunParametersAndThisTab()
{
    // The run gets the usable images and their adjustments by their letters among them; a PDF
    // file is no image of the tool (Unsupported, K41); the result replaces the tab (R16)
    AppToolOwnTabFixture fixture;
    const QString pdfFile = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("doc.pdf"), 1);
    const QString image = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { pdfFile, image });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    QVERIFY(gui->isResultInThisTab());
    QCOMPARE(gui->getOwnTabTitle(), QStringLiteral("New document"));

    const pdf::PDFAppToolInput* pdfInput = tab->getInputList()->getInput(0);
    QCOMPARE(pdfInput->state, pdf::PDFAppToolInputState::Unsupported);
    QVERIFY(!gui->isConfirmEnabled());

    // Remove file (the link of the row) of the PDF; the image turns
    gui->selectImage(tab->getInputList()->getInput(1)->id);
    gui->rotateLeft();
    AppToolOwnTabFixture::settle();
    QStringList ids = tab->getInputList()->getInputIds();
    ids.removeFirst();
    const QString imageId = ids.front();
    gui->selectImage(pdfInput->id);
    gui->removeSelected();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getInputIds(), ids);
    QCOMPARE(gui->getSelectedImage(), imageId);
    QVERIFY(gui->isConfirmEnabled());
    const QVariantMap parameters = gui->getRunParameters();
    QCOMPARE(parameters.value(QStringLiteral("inputs")).toString(), ids.front());
    QCOMPARE(parameters.value(QStringLiteral("adjustments")).toString(), QStringLiteral("A@270"));
}

void AppToolImagesTest::imagePagePreviewFollowsLayout()
{
    // PDFAppImagePagePreview draws by PDFImagePageLayout: Fit of a 4:3 image on A4 landscape with
    // 10 mm margins - the area 277 x 190 mm, the image 253.3 x 190 mm in its middle (plan 5.8)
    pdf::PDFAppImagePagePreview preview(nullptr);
    preview.resize(842 + 40, 595 + 40);
    preview.setContentsMargins(20, 20, 20, 20);
    QImage image(400, 300, QImage::Format_RGB32);
    image.fill(Qt::red);
    preview.setImage(image, QSizeF(4032.0 / 300.0 * 72.0, 3024.0 / 300.0 * 72.0));
    pdf::PDFImagePageLayout layout;
    layout.pageSize = QSizeF(pdf::PDFPageSizePresets::getPoints(210.0), pdf::PDFPageSizePresets::getPoints(297.0));
    layout.margins = QMarginsF(pdf::PDFPageSizePresets::getPoints(10.0), pdf::PDFPageSizePresets::getPoints(10.0), pdf::PDFPageSizePresets::getPoints(10.0), pdf::PDFPageSizePresets::getPoints(10.0));
    layout.scaling = pdf::PDFImagePageLayout::Scaling::Fit;
    preview.setPageLayout(layout);

    const double scale = preview.getScale();
    QVERIFY(scale > 0.0);
    const QRectF page = preview.getPageRect();
    QVERIFY(H::isNear(page.width() / scale, pdf::PDFPageSizePresets::getPoints(297.0), 0.01));
    QVERIFY(H::isNear(page.height() / scale, pdf::PDFPageSizePresets::getPoints(210.0), 0.01));
    const QRectF imageRect = preview.getImageRect();
    QVERIFY(H::isNear(imageRect.width() / scale, pdf::PDFPageSizePresets::getPoints(190.0 * 4.0 / 3.0), 0.01));
    QVERIFY(H::isNear(imageRect.height() / scale, pdf::PDFPageSizePresets::getPoints(190.0), 0.01));
    QVERIFY(H::isNear(imageRect.center().x(), page.center().x(), 0.01));

    // The anchor is never mirrored (the page belongs to the document)
    layout.anchor = Qt::AlignLeft | Qt::AlignVCenter;
    preview.setPageLayout(layout);
    preview.setLayoutDirection(Qt::RightToLeft);
    QVERIFY(H::isNear(preview.getImageRect().left() - page.left(), pdf::PDFPageSizePresets::getPoints(10.0) * scale, 0.01));

    // The actual size: 96 dpi, the size hint is the page
    preview.setFit(false);
    QVERIFY(H::isNear(preview.getScale(), 96.0 / 72.0, 1e-9));
    QCOMPARE(preview.sizeHint().width(), int(std::round(pdf::PDFPageSizePresets::getPoints(297.0) * 96.0 / 72.0)) + 40);
}

void AppToolImagesTest::imagesCropOfAnImage()
{
    // convert-images-to-pdf-crop 336:3159: Crop is active, Create PDF is disabled, the image alone
    // with the box, Equal margins 120 px of 4032 x 3024 keeps 3792 x 2784 px; Apply Crop is one
    // undo step ("A#<left>,<top>,<right>,<bottom>", the Tag "cropped")
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2041.png"), QSize(4032, 3024), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    gui->getAction(QStringLiteral("crop"))->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isCropping());
    QVERIFY(gui->getAction(QStringLiteral("crop"))->isChecked());
    QVERIFY(!gui->getAction(QStringLiteral("rotateLeft"))->isEnabled());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(gui->getCanvas()->isCropMode());
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Crop · IMG_2041.png"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel()->text(), QStringLiteral("Drag the handles or type the values on the right · darkened parts are removed"));
    QVERIFY(gui->getCropPanel()->isVisibleTo(gui->getCropPanel()->window()));
    QCOMPARE(gui->getCropPanel()->getNameTag()->text(), QStringLiteral("IMG_2041.png"));
    QCOMPARE(gui->getCropPanel()->getCallout()->getTitleLabel()->text(), QStringLiteral("The image file is not changed"));
    QVERIFY(gui->getCanvas()->getCropStage()->isImageShown());
    QCOMPARE(gui->getCanvas()->getCropStage()->getPageSize(), QSizeF(4032, 3024));
    QCOMPARE(gui->getCropOverlay()->getBounds(), QRectF(0, 0, 4032, 3024));
    QCOMPARE(gui->getCropOverlay()->getRect(), QRectF(0, 0, 4032, 3024));
    QVERIFY(gui->getCropPanel()->getEqualRadio()->isChecked());

    // RS3FIGMA2: the card 336:3448 pads its rows 12 inside of its border of 1 (Equal margins at
    // the leading edge 13, the fields end 13 before the trailing edge, the first row 13 below
    // the top)
    QWidget* cropCard = gui->getCropPanel()->getEqualRadio()->parentWidget();
    QVERIFY(qobject_cast<pdf::PDFAppToolCard*>(cropCard));
    QCOMPARE(gui->getCropPanel()->getEqualRadio()->geometry().left(), 13);
    QCOMPARE(gui->getCropPanel()->getEqualRadio()->geometry().top(), 13);
    QCOMPARE(gui->getCropPanel()->getMarginField()->mapTo(cropCard, QPoint(0, 0)).x() + gui->getCropPanel()->getMarginField()->width(), cropCard->width() - 13);
    QCOMPARE(gui->getCropPanel()->getApplyToField()->geometry().bottom() + 1, cropCard->height() - 13);

    // Margin 120 (the field writes the box, the box and the Result follow)
    gui->getCropPanel()->getMarginField()->setValue(120);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCropRect(), QRect(120, 120, 3792, 2784));
    QCOMPARE(gui->getCropOverlay()->getRect(), QRectF(120, 120, 3792, 2784));
    QCOMPARE(gui->getCropPanel()->getResult()->getValue(0), QStringLiteral("4,032 × 3,024 px"));
    QCOMPARE(gui->getCropPanel()->getResult()->getValue(1), QStringLiteral("3,792 × 2,784 px"));
    QVERIFY(!gui->getCropPanel()->getLeftField()->isEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Images to PDF · cropping IMG_2041.png · 3,792 × 2,784 px will be kept"));

    // A key on the box moves it: the box is a crop box then (as Crop Pages)
    gui->getCropOverlay()->setFocus();
    QTest::keyClick(gui->getCropOverlay(), Qt::Key_Right);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCropRect(), QRect(121, 120, 3792, 2784));
    QVERIFY(gui->getCropPanel()->getBoxRadio()->isChecked());
    QCOMPARE(gui->getCropPanel()->getLeftField()->value(), 121);
    QVERIFY(gui->getCropPanel()->getLeftField()->isEnabled());

    // Nothing is written before Apply Crop
    QVERIFY(H::model(tab)->getValue(QStringLiteral("adjustments")).toString().isEmpty());
    QVERIFY(!H::model(tab)->canUndo());
    gui->getCropPanel()->getApplyButton()->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->isCropping());
    QVERIFY(!gui->getCanvas()->isCropMode());
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A#121,120,119,120"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().tag, QStringLiteral("cropped"));
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(4), QStringLiteral("3,792 × 2,784 px"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // Crop again starts from the crop; Reset Crop is the whole image, leaving by Crop writes nothing
    gui->setCropping(true);
    QCOMPARE(gui->getCropRect(), QRect(121, 120, 3792, 2784));
    QVERIFY(!gui->getCropPanel()->getEqualRadio()->isChecked());
    gui->getCropPanel()->getResetButton()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCropRect(), QRect(0, 0, 4032, 3024));
    gui->getAction(QStringLiteral("crop"))->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->isCropping());
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A#121,120,119,120"));

    // One undo step
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QVERIFY(H::model(tab)->getValue(QStringLiteral("adjustments")).toString().isEmpty());
    QVERIFY(!H::model(tab)->canUndo());
}

void AppToolImagesTest::imagesCropOfATurnedImageAndAll()
{
    // The crop is drawn on the turned image; the core keeps the margins of the image before its
    // turn (5.8): turned 90° clockwise, the left side of the turned image is the bottom of the
    // image. Apply to: All images gives the same margins to every image by its own turn.
    using GUI = pdf::PDFAppImagesToPdfToolGUI;
    QCOMPARE(GUI::getTurnedMargins(QMargins(1, 2, 3, 4), pdf::PageRotation::Rotate90), QMargins(4, 1, 2, 3));
    QCOMPARE(GUI::getTurnedMargins(QMargins(1, 2, 3, 4), pdf::PageRotation::Rotate180), QMargins(3, 4, 1, 2));
    QCOMPARE(GUI::getTurnedMargins(QMargins(1, 2, 3, 4), pdf::PageRotation::Rotate270), QMargins(2, 3, 4, 1));
    for (const pdf::PageRotation turn : { pdf::PageRotation::None, pdf::PageRotation::Rotate90, pdf::PageRotation::Rotate180, pdf::PageRotation::Rotate270 })
    {
        QCOMPARE(GUI::getUnturnedMargins(GUI::getTurnedMargins(QMargins(1, 2, 3, 4), turn), turn), QMargins(1, 2, 3, 4));
    }

    AppToolOwnTabFixture fixture;
    const QString first = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(300, 400), 300);
    const QString second = H::png(fixture.folder(), QStringLiteral("b.png"), QSize(400, 300), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { first, second });
    QVERIFY(tab);
    GUI* gui = H::gui<GUI>(tab);
    QVERIFY(gui);
    gui->rotateRight();
    AppToolOwnTabFixture::settle();

    // The turned image is 400 x 300: 10 px at its left are 10 px at the bottom of the image
    gui->setCropping(true);
    QCOMPARE(gui->getCanvas()->getCropStage()->getPageSize(), QSizeF(400, 300));
    gui->setCropRect(QRect(10, 0, 390, 300), false);
    gui->applyCrop();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A@90#0,0,0,10"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().tag, QStringLiteral("rotated 90°"));

    // All images: 5 px on every side of both
    gui->setCropping(true);
    gui->getCropPanel()->getEqualRadio()->click();
    gui->getCropPanel()->getMarginField()->setValue(5);
    gui->getCropPanel()->getApplyToField()->setCurrentIndex(1);
    Q_EMIT gui->getCropPanel()->getApplyToField()->activated(1);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCropPanel()->getApplyToField()->itemData(1, Qt::ToolTipRole).toString(), QStringLiteral("The same margins for all 2 images"));
    gui->getCropPanel()->getApplyButton()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A@90#5,5,5,5;B#5,5,5,5"));
}

void AppToolImagesTest::imagesSelectionOfMoreImages()
{
    // 336:1976 "Ctrl or Shift selects more images": a click selects one, Shift the images up to
    // the clicked one, Ctrl adds or takes out one; Rotate and Remove act on all selected
    AppToolOwnTabFixture fixture;
    QStringList files;
    for (const char* name : { "a.png", "b.png", "c.png" })
    {
        files << H::png(fixture.folder(), QString::fromLatin1(name), QSize(40, 30), 300);
    }
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, files);
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    const QStringList ids = tab->getInputList()->getInputIds();
    QCOMPARE(gui->getImagesPanel()->getHintLabel()->text(), QStringLiteral("Drag to change the page order. Ctrl or Shift selects more images. Drop files anywhere in the window or paste with Ctrl+V."));

    const auto clickRow = [&](int index, Qt::KeyboardModifiers modifiers)
    {
        pdf::PDFAppToolInputRow* row = gui->getImagesPanel()->getRows()[size_t(index)];
        QTest::mouseClick(row, Qt::LeftButton, modifiers, row->rect().center());
        AppToolOwnTabFixture::settle();
    };
    clickRow(0, Qt::NoModifier);
    clickRow(2, Qt::ShiftModifier);
    QCOMPARE(gui->getSelectedImages(), ids);
    QCOMPARE(gui->getSelectedImage(), ids[2]);
    QVERIFY(gui->getImagesPanel()->getRows()[1]->isSelected());

    clickRow(1, Qt::ControlModifier);
    QCOMPARE(gui->getSelectedImages(), QStringList({ ids[0], ids[2] }));
    QVERIFY(!gui->getImagesPanel()->getRows()[1]->isSelected());

    // Rotate Right turns both selected images (one undo step)
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("adjustments")).toString(), QStringLiteral("A@90;C@90"));

    // Remove removes both: b stays
    gui->removeSelected();
    AppToolOwnTabFixture::settle();
    QCOMPARE(tab->getInputList()->getInputIds(), QStringList({ ids[1] }));
    QCOMPARE(gui->getSelectedImages(), QStringList({ ids[1] }));

    // A click without a key selects one again
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    clickRow(0, Qt::NoModifier);
    QCOMPARE(gui->getSelectedImages(), QStringList({ ids[0] }));
}

void AppToolImagesTest::imagesRename()
{
    // Rename (the link of Selected image 336:1976): the name of the image in the tool and of the
    // PDF, one undo step; the file keeps its name; an invalid name is refused with its reason
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2041.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSelectedCard()->getLink()->text(), QStringLiteral("Rename"));
    QVERIFY(gui->getSelectedCard()->getLink()->isVisibleTo(gui->getSelectedCard()));

    QString error;
    QVERIFY(!gui->renameImage(gui->getSelectedImage(), QStringLiteral("a/b.png"), &error));
    QCOMPARE(error, QStringLiteral("Enter a name of at most 255 characters without a slash or a backslash."));
    QVERIFY(!gui->renameImage(gui->getSelectedImage(), QStringLiteral("   "), &error));

    // The dialog: its field has the name, Rename writes the new one
    gui->getSelectedCard()->getLink()->click();
    AppToolOwnTabFixture::settle();
    QPointer<pdf::PDFAppNameDialog> dialog = gui->getRenameDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getNameField()->text(), QStringLiteral("IMG_2041.png"));
    dialog->getNameField()->setText(QStringLiteral("  Cover.png "));
    dialog->getActionButton()->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(!dialog || !dialog->isVisible());

    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(0), QStringLiteral("Cover.png"));
    QCOMPARE(gui->getImagesPanel()->getRows()[0]->getContent().name, QStringLiteral("Cover.png"));
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 1 of 1 · Cover.png"));
    QCOMPARE(gui->getAdjustment(gui->getSelectedImage()).turn, pdf::PageRotation::Rotate90);
    QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("IMG_2041.png"));
    QVERIFY(QFileInfo::exists(path));

    // The run gets the image under its name (the name of the PDF follows the first image)
    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    QCOMPARE(environment.images.size(), size_t(1));
    QCOMPARE(environment.images.front().fileName, QStringLiteral("Cover.png"));
    QCOMPARE(pdf::PDFDocumentToolImagesToPdf::getSuggestedFileName(environment.images.front().fileName), QStringLiteral("Cover.pdf"));

    // Undo brings the old name back (with the turn)
    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSelectedCard()->getValues()->getValue(0), QStringLiteral("IMG_2041.png"));
    QCOMPARE(gui->getAdjustment(gui->getSelectedImage()).turn, pdf::PageRotation::Rotate90);
}

void AppToolImagesTest::imagesAnchorComboBox()
{
    // K14 / 356:9556: Anchor is a combo box of 140 in the card Page layout ("Middle center"),
    // its list is the popover of the anchor grid of 36 x 36 cells; a cell and an arrow write the
    // parameter (one undo step), the combo box follows Undo
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    pdf::PDFAppToolAnchorComboBox* combo = gui->getAnchorComboBox();
    QVERIFY(combo);
    QCOMPARE(combo->currentText(), QStringLiteral("Middle center"));
    QCOMPARE(combo->width(), pdf::PDFAppToolMarginFields::FIELD_WIDTH);
    QCOMPARE(combo->count(), 9);
    QCOMPARE(gui->getLayoutFields()->getEditor(QStringLiteral("anchor")), combo);

    // The popover with the grid of 36 x 36 cells instead of the list
    combo->showPopup();
    AppToolOwnTabFixture::settle();
    QVERIFY(combo->getPanel()->isVisible());
    pdf::PDFAnchorGrid* grid = combo->getPanel()->getGrid();
    QCOMPARE(grid->getCellRect(0).size(), QSize(pdf::PDFAnchorGrid::COMPACT_CELL_SIZE, pdf::PDFAnchorGrid::COMPACT_CELL_SIZE));
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->getCellRect(0).center());
    AppToolOwnTabFixture::settle();
    QVERIFY(!combo->getPanel()->isVisible());
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("anchor")).toString(), QStringLiteral("top-left"));
    QCOMPARE(combo->currentText(), QStringLiteral("Top left"));
    QCOMPARE(gui->getCanvas()->getPreview()->getPageLayout().anchor, Qt::AlignTop | Qt::AlignLeft);

    // An arrow on the closed combo box: the next anchor
    combo->setFocus();
    QTest::keyClick(combo, Qt::Key_Down);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::model(tab)->getValue(QStringLiteral("anchor")).toString(), QStringLiteral("top-center"));

    H::model(tab)->undo();
    AppToolOwnTabFixture::settle();
    QCOMPARE(combo->currentText(), QStringLiteral("Top left"));
}

void AppToolImagesTest::imagesRowThumbnails()
{
    // thumb 336:2128: the rows of the images have the thumbnail of the picture (decoded in the
    // pool) in the slot of 56 x 43 instead of the letter; the turn of the image turns it
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(400, 300), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    const QString id = gui->getSelectedImage();
    QVERIFY(gui->getImagesPanel()->getRows()[0]->getContent().hasThumbnail);
    QVERIFY(AppToolOwnTabFixture::waitFor([gui, id]() { return !gui->getRowThumbnail(id).isNull(); }));
    QCOMPARE(gui->getPendingThumbnailCount(), 0);

    // At most 128 px, the ratio of the image, its color
    const QImage thumbnail = gui->getRowThumbnail(id);
    QCOMPARE(thumbnail.size(), QSize(128, 96));
    QCOMPARE(QColor(thumbnail.pixel(64, 48)), QColor(200, 220, 240));

    gui->rotateRight();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getRowThumbnail(id).size(), QSize(96, 128));

    // The row draws it in the slot (the text starts 74 after the thumbnail of 56)
    pdf::PDFAppToolInputRow* row = gui->getImagesPanel()->getRows()[0];
    const QImage picture = AppToolOwnTabFixture::render(row, 1.0);
    QCOMPARE(QColor(picture.pixel(pdf::PDFAppToolInputRow::PADDING + 28, row->height() / 2)), QColor(200, 220, 240));
}

void AppToolImagesTest::imagesColorManagedPreview()
{
    // K43: the preview shows the colors of the new page as a viewer shows them - the image with an
    // ICC profile through its profile (ICCBased), the alpha kept; without a color management the
    // picture stays
    QImage image(4, 2, QImage::Format_ARGB32);
    image.fill(QColor(200, 100, 50, 200));
    QCOMPARE(pdf::PDFAppImagePagePreview::getColorManagedPicture(image, nullptr), image);

    pdf::PDFCMSManager manager(nullptr);
    manager.setSettings(manager.getDefaultSettings());
    const pdf::PDFCMSPointer cms = manager.getCurrentCMS();
    QVERIFY(cms);

    // Adobe RGB (1998) (200, 100, 50) to the sRGB output profile: by the matrices of the profiles
    // about (227, 100, 42) - the result of the conversion of Qt (another implementation of the
    // same profiles) within 3 levels; the red differs from the pixel
    image.setColorSpace(QColorSpace::AdobeRgb);
    const QImage managed = pdf::PDFAppImagePagePreview::getColorManagedPicture(image, cms.data());
    const QColor expected = image.convertedToColorSpace(QColorSpace::SRgb).pixelColor(1, 1);
    const QColor color = managed.pixelColor(1, 1);
    QVERIFY2(std::abs(color.red() - expected.red()) <= 3 && std::abs(color.green() - expected.green()) <= 3 && std::abs(color.blue() - expected.blue()) <= 3,
             qPrintable(QStringLiteral("%1 %2").arg(color.name(), expected.name())));
    QVERIFY2(std::abs(color.red() - 227) <= 4 && std::abs(color.green() - 100) <= 4 && std::abs(color.blue() - 42) <= 4, qPrintable(color.name()));
    QCOMPARE(color.alpha(), 200);

    // K43: the own tab has the color management of the settings also without a document tab
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("a.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    QVERIFY(fixture.window->getDocumentTabs().isEmpty());
    QVERIFY(tab->getToolPage()->getHost()->createEnvironment().cms);
}

void AppToolImagesTest::themesDirectionAndScreenshots()
{
    // Both themes without the leak color at 1x and 2x, right to left (the list of the images at the
    // right edge; the page and the box of the crop never mirrored); PDF4QT_APP_SCREENSHOTS writes
    // the frames 336:1976, 336:3159 (crop) and 385:13263 (Interleave)
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            const QString suffix = QStringLiteral("%1-%2").arg(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark"),
                                                              direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl"));
            {
                // The fixture sets the light scheme of its context: the scheme of the row comes after it
                AppToolOwnTabFixture fixture;
                pdf::PDFThemeManager::getInstance()->setScheme(scheme);
                AppToolOwnTabFixture::settle();
                QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), scheme);
                const QString first = H::png(fixture.folder(), QStringLiteral("IMG_2041.png"), QSize(400, 300), 300);
                const QString second = H::png(fixture.folder(), QStringLiteral("scan_002.png"), QSize(300, 400), 300);
                pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { first, second });
                QVERIFY(tab);
                pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
                QVERIFY(gui);
                QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getPendingThumbnailCount() == 0; }));
                AppToolOwnTabFixture::settle();
                pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
                for (const qreal ratio : { 1.0, 2.0 })
                {
                    QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(view, ratio)), 0);
                }
                const int panelX = gui->getImagesPanel()->mapTo(view, QPoint(0, 0)).x();
                QCOMPARE(panelX < 720, direction == Qt::LeftToRight);
                fixture.screenshot(QStringLiteral("convert-images-to-pdf-336-1976-") + suffix);

                // The crop: the box of 20 px is at the left of the image also right to left
                gui->setCropping(true);
                gui->setCropRect(QRect(20, 0, 380, 300), false);
                AppToolOwnTabFixture::settle();
                QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(view, 1.0)), 0);
                const QRectF page = gui->getCanvas()->getCropStage()->getPageRect();
                const QRectF box = gui->getCropOverlay()->getBoxRect();
                QVERIFY(std::abs((box.left() - page.left()) - 20.0 * page.width() / 400.0) < 1.0);
                QVERIFY(std::abs(box.right() - page.right()) < 1.0);
                fixture.screenshot(QStringLiteral("convert-images-to-pdf-crop-336-3159-") + suffix);
            }
            {
                // The fixture of the application sets the direction of its language at its end
                QApplication::setLayoutDirection(direction);
                AppToolOwnTabFixture fixture;
                pdf::PDFThemeManager::getInstance()->setScheme(scheme);
                AppToolOwnTabFixture::settle();
                QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), scheme);
                const QString front = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_front.pdf"), 6);
                const QString back = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("scan_back.pdf"), 6);
                pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolInterleave::TOOL_ID, { front, back });
                QVERIFY(tab);
                pdf::PDFAppInterleaveToolGUI* gui = H::gui<pdf::PDFAppInterleaveToolGUI>(tab);
                QVERIFY(gui);
                AppToolOwnTabFixture::settle();
                pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
                for (const qreal ratio : { 1.0, 2.0 })
                {
                    QCOMPARE(AppToolOwnTabFixture::countLeakPixels(AppToolOwnTabFixture::render(view, ratio)), 0);
                }
                QWidget* slotRow = gui->getSlotRow(pdf::PDFAppInterleaveToolGUI::FrontSlot);
                const int slotX = slotRow->mapTo(view, slotRow->rect().center()).x();
                QVERIFY2((slotX < 720) == (direction == Qt::LeftToRight), qPrintable(QString::number(slotX)));
                fixture.screenshot(QStringLiteral("pages-interleave-385-13263-") + suffix);
            }
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolImagesTest::imagesSaveAsFileReplacesTab()
{
    // 336:3572 "Save as a file": the PDF is written to the file and then opened in this tab - the
    // document tab of the written file takes the place of the tool tab (R16). The shell sends the
    // written file in PDFAppToolCloseRequest::newDocumentFilePath (its dialog writes into the
    // documents of the user, so the test sends the request of a file in its own folder)
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2041.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    QPointer<pdf::PDFAppToolTab> toolTab(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(QString::fromLatin1(pdf::PDFDocumentToolImagesToPdf::TOOL_ID)));
    QVERIFY(qobject_cast<pdf::PDFAppImagesToPdfToolGUI*>(shell->getGUI())->isResultInThisTab());
    const int toolIndex = int(fixture.window->getTabs().indexOf(tab));
    const QString written = QFileInfo(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Album.pdf"), 2)).absoluteFilePath();

    pdf::PDFAppToolCloseRequest request;
    request.reason = pdf::PDFAppToolCloseRequest::Reason::Accepted;
    request.newDocumentFilePath = written;
    Q_EMIT shell->closeRequested(request);
    QVERIFY(AppToolOwnTabFixture::waitFor([&]() { return !toolTab; }));
    QVERIFY(AppToolOwnTabFixture::waitFor([&]()
    {
        for (pdf::PDFAppDocumentTab* documentTab : fixture.window->getDocumentTabs())
        {
            if (documentTab->getDocument() && QFileInfo(documentTab->getDocument()->getFilePath()).absoluteFilePath() == written)
            {
                return true;
            }
        }
        return false;
    }));
    QVERIFY(fixture.window->getToolTabs().isEmpty());
    pdf::PDFAppDocumentTab* current = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(current);
    QCOMPARE(QFileInfo(current->getDocument()->getFilePath()).absoluteFilePath(), written);
    QCOMPARE(int(fixture.window->getTabs().indexOf(current)), toolIndex);
}

void AppToolImagesTest::imagesSaveAsFileNotOpenedKeepsTab()
{
    // RS3APP: "Save as a file", whose written file does not open (the window lists it in
    // open-failed), does not close the tab: the tool stays with its images
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2042.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    QPointer<pdf::PDFAppToolTab> toolTab(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell);
    const int tabCount = int(fixture.window->getTabs().size());

    pdf::PDFAppToolCloseRequest request;
    request.reason = pdf::PDFAppToolCloseRequest::Reason::Accepted;
    request.newDocumentFilePath = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("missing/Album.pdf"));
    Q_EMIT shell->closeRequested(request);
    AppToolOwnTabFixture::settle();
    QVERIFY(toolTab);
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount);
    QVERIFY(fixture.window->getRequestedFiles().contains(request.newDocumentFilePath));
    QCOMPARE(AppToolOwnTabFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(tab->getInputList()->getCount(), 1);
}

void AppToolImagesTest::imagesCancelAfterRun()
{
    // RS3APP Low 6, owner 2026-11-09: Cancel of the toolbar after the run (Enabled, 336:3572 has
    // no target of it) is Cancel of the result dialog - the new document is discarded - and then
    // Cancel as in Editing: the own tab is closed, the window asks, because the images would be
    // lost (K16); Cancel of that question keeps the tool with its images
    AppToolOwnTabFixture fixture;
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2043.png"), QSize(40, 30), 300);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    QPointer<pdf::PDFAppToolTab> toolTab(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell);
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([shell]() { return shell->getState() == pdf::PDFDocumentToolShell::State::Deciding && shell->getResultDialog(); }));
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::NewDocument);
    QVERIFY(shell->getToolbar()->getCancelButton()->isEnabled());

    bool isAsked = false;
    QTimer* answer = new QTimer(qApp);
    answer->setInterval(5);
    QObject::connect(answer, &QTimer::timeout, answer, [answer, &isAsked]()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            QPushButton* button = widget->objectName() == QStringLiteral("toolTabCloseDialog") && widget->isVisible() ? widget->findChild<QPushButton*>(QStringLiteral("cancelButton")) : nullptr;
            if (button)
            {
                isAsked = true;
                answer->stop();
                answer->deleteLater();
                button->click();
                return;
            }
        }
    });
    answer->start();
    QTimer::singleShot(10000, answer, &QTimer::deleteLater);

    shell->getToolbar()->getCancelButton()->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([&isAsked]() { return isAsked; }));
    AppToolOwnTabFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(toolTab);
    QCOMPARE(AppToolOwnTabFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QVERIFY(fixture.window->getDocumentTabs().isEmpty());
}

void AppToolImagesTest::imagesCaptionAtThePage()
{
    // RS3FIGMA M11, 336:2234: the page of the image in Fit is 600 of the 774 wide stage (an A4
    // landscape page 600 x 424), the caption lies 12 below the page, the page with its caption in
    // the middle of the stage
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    const QString path = H::png(fixture.folder(), QStringLiteral("IMG_2044.png"), QSize(400, 300), 72);
    pdf::PDFAppToolTab* tab = H::open(fixture, pdf::PDFDocumentToolImagesToPdf::TOOL_ID, { path });
    QVERIFY(tab);
    pdf::PDFAppImagesToPdfToolGUI* gui = H::gui<pdf::PDFAppImagesToPdfToolGUI>(tab);
    QVERIFY(gui);
    QVERIFY(H::model(tab)->setValues({ { QStringLiteral("pageSize"), QStringLiteral("a4") }, { QStringLiteral("orientation"), QStringLiteral("landscape") } }).isEmpty());
    AppToolOwnTabFixture::settle();
    pdf::PDFAppImagesToPdfCanvas* canvas = gui->getCanvas();
    QVERIFY(canvas && canvas->getPreview()->isVisible());
    const QRectF page = canvas->getPreview()->getPageRect();
    const double stageWidth = canvas->getScrollArea()->viewport()->width();
    QVERIFY2(std::abs(page.width() - stageWidth * 600.0 / 774.0) <= 1.0, qPrintable(QString::number(page.width())));
    QVERIFY(H::isNear(page.width() / page.height(), 297.0 / 210.0, 0.01));
    const QPoint pageBottom = canvas->getPreview()->mapTo(canvas, QPoint(0, qCeil(page.bottom())));
    const QPoint captionTop = canvas->getCaptionLabel()->mapTo(canvas, QPoint(0, 0));
    QCOMPARE(captionTop.y() - pageBottom.y(), 12);
    QVERIFY(!canvas->getCaptionLabel()->text().isEmpty());

    // The page with its caption in the middle of the stage (336:2234 CENTER)
    QWidget* viewport = canvas->getScrollArea()->viewport();
    const double top = canvas->getPreview()->mapTo(viewport, page.topLeft().toPoint()).y();
    const double bottom = canvas->getCaptionLabel()->mapTo(viewport, QPoint(0, canvas->getCaptionLabel()->height())).y();
    QVERIFY2(std::abs((top + bottom) * 0.5 - viewport->height() * 0.5) <= 2.0, qPrintable(QStringLiteral("%1 %2 %3").arg(top).arg(bottom).arg(viewport->height())));
}

QTEST_MAIN(AppToolImagesTest)

#include "tst_apptoolimagestest.moc"
