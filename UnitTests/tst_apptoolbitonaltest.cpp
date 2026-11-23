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

// The GUI of Convert to Black & White (step 4 of phase 2, package P3; plan 8 row GUI, Figma 304:5102
// ... 307:6564): the texts of the toolbar and its menus, the modes of the items with Undo, the
// Selected items and the Summary, the preview of the core, the detection of blank pages with its
// question, Stop, the run to the result dialog.

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolgui_bitonal.h"
#include "pdfapptoolhost.h"
#include "mergetestpdf.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfappvaluestore.h"
#include "pdfcomponentpanels.h"
#include "pdfsettingdefinitions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_bitonal.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdffluentwidgets.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStyleOptionViewItem>
#include <QtTest>

using pdf::PDFAppToolGUIBitonal;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolShellFixture;

/// Helpers of the test
class BitonalGuiHelper
{
public:
    BitonalGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("optimize.bitonal");

    /// Three pages of 200 x 100 pt: the first and the third have the black left half, the second
    /// is empty (a blank page)
    static QString writeDocument(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 3; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 100));
            const QByteArray data = i == 1 ? QByteArray() : QByteArray("0 0 0 rg 0 0 100 100 re f");
            pdf::PDFDictionaryBuilder content;
            content.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
            const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(content), QByteArray(data))));
            pdf::PDFDictionaryBuilder update;
            update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        }
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
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

    static PDFAppToolGUIBitonal* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIBitonal*>(shell->getGUI()) : nullptr;
    }

    static QString items(PDFDocumentToolShell* shell)
    {
        return shell->getModel()->getValue(QStringLiteral("items")).toString();
    }

    /// One page with the images Im1 and Im2 (RGB 2 x 1, black and white); with \p isSecondBroken the
    /// stream of Im2 claims FlateDecode, but holds no compressed data - it cannot be decoded
    static QString writeImageDocument(const QString& filePath, bool isSecondBroken)
    {
        const QByteArray pixels("\x00\x00\x00\xFF\xFF\xFF", 6);
        const QByteArray image = "/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB";
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("B", 1, QByteArray(), { { 1, "/Resources << /XObject << /Im1 10 0 R /Im2 11 0 R >> >>" } });
        pdf.set(10, mergetest::MergeTestPdf::stream(pixels, image));
        pdf.set(11, isSecondBroken ? mergetest::MergeTestPdf::stream("this is not a compressed image stream at all", image + " /Filter /FlateDecode")
                                   : mergetest::MergeTestPdf::stream(pixels, image));
        pdf.set(101, mergetest::MergeTestPdf::stream("q 200 0 0 100 50 600 cm /Im1 Do Q q 200 0 0 100 50 400 cm /Im2 Do Q"));
        QFile file(filePath);
        const QByteArray bytes = pdf.write();
        return file.open(QFile::WriteOnly) && file.write(bytes) == bytes.size() ? filePath : QString();
    }

    /// The texts of the labels of a widget (a message box: its title, its text and its detail)
    static QStringList labelTexts(const QWidget* widget)
    {
        QStringList texts;
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            texts << label->text();
        }
        return texts;
    }

    /// Opens Black & White with Source: Images and waits for the list of the images
    static PDFDocumentToolShell* openImagesSource(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab* tab)
    {
        PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ID);
        PDFAppToolGUIBitonal* current = gui(shell);
        if (!current)
        {
            return nullptr;
        }
        shell->getModel()->setValue(QStringLiteral("source"), QStringLiteral("images"));
        AppToolShellFixture::settle();
        const bool isCollected = AppToolShellFixture::waitFor([current]() { return !current->isCollectingImages() && current->getItemCount() > 0; }, 30000);
        AppToolShellFixture::settle();
        return isCollected ? shell : nullptr;
    }

    /// The value of a row of a key-value list, empty = none
    static QString value(const pdf::PDFKeyValueList* list, const QString& key)
    {
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            if (list->getKey(i) == key)
            {
                return list->getValue(i);
            }
        }
        return QString();
    }
};

class AppToolBitonalTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registered();
    void toolbarTexts();
    void modesAndUndo();
    void contextMenuModeMayCloseTheTab();
    void previewOfTheCore();
    void canceledPreviewKeepsItsPage();
    void imageListSelectionKeepsCurrentRow();
    void blankPagesQuestion();
    void detectionStops();
    void runToTheResultDialog();
    void partialResultTexts();
    void partialResultQuestion();
    void imageModeBadge();
    void menuFieldsOfTheTheme();
    void canvasIsTheComparison();
    void imagesSourceHead();
    void previewOfBothSources();
    void previewUsesTheColorManagementOfTheRun();
    void numbersOfTheLocale();
    void spaceInTheImageList();
    void documentWithoutPages();
    void gridTagsOnThePanel();
    void modesFollowTheNewImageList();
    void screenshots();
};

void AppToolBitonalTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolBitonalTest::registered()
{
    AppToolShellFixture fixture;
    QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolBitonal>(pdf::PDFDocumentToolRegistry::getTool(BitonalGuiHelper::TOOL_ID)));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(BitonalGuiHelper::TOOL_ID));
}

void AppToolBitonalTest::toolbarTexts()
{
    // 304:5102: Source: Pages, Method: Auto, Compression: Auto, Mode, Blank pages, Convert (owner
    // 13. 11.); 306:5615 Method: Threshold; the fields of 306:5353 follow their method and source
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(gui->getSourceButton()->text(), QStringLiteral("Source: Pages"));
    QCOMPARE(gui->getMethodButton()->text(), QStringLiteral("Method: Auto"));
    QCOMPARE(gui->getCompressionButton()->text(), QStringLiteral("Compression: Auto"));
    QCOMPARE(gui->getModeButton()->text(), QStringLiteral("Mode"));
    QCOMPARE(gui->getBlankPagesButton()->text(), QStringLiteral("Blank pages"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Convert"));
    QVERIFY(!gui->getThresholdSpinBox()->isEnabled());
    QVERIFY(gui->getResolutionSpinBox()->isEnabled());

    // The status of 304:5102 "… · 120 pages · right-click the items to change their mode"
    QCOMPARE(gui->getStatusDetailText(), QStringLiteral("3 pages · right-click the items to change their mode"));

    shell->getModel()->setValue(QStringLiteral("method"), QStringLiteral("threshold"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMethodButton()->text(), QStringLiteral("Method: Threshold"));
    QVERIFY(gui->getThresholdSpinBox()->isEnabled());
    QVERIFY(gui->getFooterText().startsWith(QStringLiteral("User-defined threshold: 128")));

    shell->getModel()->setValue(QStringLiteral("compression"), QStringLiteral("jbig2"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCompressionButton()->text(), QStringLiteral("Compression: JBIG2"));

    // A threshold typed into the field is the value of the model
    gui->getThresholdSpinBox()->setValue(141);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("threshold")).toInt(), 141);

    // Images: the resolution is used for whole pages only; the callout of 306:5615
    shell->getModel()->setValue(QStringLiteral("source"), QStringLiteral("images"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSourceButton()->text(), QStringLiteral("Source: Images"));
    QVERIFY(!gui->getResolutionSpinBox()->isEnabled());
    QVERIFY(!gui->getBlankPagesButton()->isEnabled());
}

void AppToolBitonalTest::modesAndUndo()
{
    // 306:5615: the mode of the selected items is one undo step; the labels of the tiles, the
    // Selected items and the Summary of 304:5102 follow it; with nothing selected the mode applies
    // to the whole document
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->selectItems({ 0, 2 });
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("3 pages · 2 selected"));
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite);
    AppToolShellFixture::settle();
    QCOMPARE(H::items(shell), QStringLiteral("1=fill-white; 3=fill-white"));
    // 304:5102: the mode is the tag of the tile, the tile has no caption below it (RS4FIGMA)
    QCOMPARE(gui->getPageGrid()->getModel()->getTile(0).tag, QStringLiteral("white"));
    QVERIFY(gui->getPageGrid()->getModel()->getTile(0).caption.isEmpty());
    QCOMPARE(gui->getPageGrid()->getModel()->getTile(1).tag, QString());
    QCOMPARE(H::value(gui->getSelectedRows(), QStringLiteral("Pages")), QStringLiteral("1, 3"));
    QCOMPARE(H::value(gui->getSelectedRows(), QStringLiteral("Mode")), QStringLiteral("2 white"));
    QCOMPARE(gui->getSelectedHeader()->getHintLabel()->text(), QStringLiteral("2 of 3"));
    QCOMPARE(H::value(gui->getSummaryRows(), QStringLiteral("Fill with white")), QStringLiteral("2 pages"));
    QCOMPARE(H::value(gui->getSummaryRows(), QStringLiteral("Convert")), QStringLiteral("1 page"));

    // The second page inverted, then Undo brings the white fills alone back
    gui->selectItems({ 1 });
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::AlgorithmInverted);
    AppToolShellFixture::settle();
    QCOMPARE(H::items(shell), QStringLiteral("1=fill-white; 2=convert-inverted; 3=fill-white"));
    QCOMPARE(gui->getPageGrid()->getModel()->getTile(1).tag, QStringLiteral("inverted"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(H::items(shell), QStringLiteral("1=fill-white; 3=fill-white"));
    QCOMPARE(gui->getPageGrid()->getModel()->getTile(1).tag, QString());

    // Nothing selected: the whole document
    gui->selectItems({ });
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::Original);
    AppToolShellFixture::settle();
    QCOMPARE(H::items(shell), QStringLiteral("1=unchanged; 2=unchanged; 3=unchanged"));
    QCOMPARE(H::value(gui->getSummaryRows(), QStringLiteral("Leave unchanged")), QStringLiteral("3 pages"));

    // 304:5102: the callout of the pages
    QVERIFY(gui->getCallout()->isVisible());
}

void AppToolBitonalTest::contextMenuModeMayCloseTheTab()
{
    // P2AUDIOCRASH (lifetime convention "emit last"): a row of the mode menu of a right click (304:5102
    // "Right-click for the mode menu") sets the mode of the selected items - one undo step, whose slots
    // may close the document tab with the tool, the grid and the menu. QMenu reads its own members after
    // its signal triggered returns (QMenuPrivate::_q_actionTriggered), so the mode is set only after the
    // menu has finished its signal: the step of Undo comes after QAction::trigger() has returned, never
    // inside it, and a slot of it closes the tab safely
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    QPointer<PDFAppToolGUIBitonal> gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->selectItems({ 0 });
    AppToolShellFixture::settle();

    // The menu of a right click on the grid and its row Fill with white
    pdf::PDFAppToolPageGridView* view = gui->getPageGrid()->getView();
    const QPoint position = view->viewport()->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
    QApplication::sendEvent(view->viewport(), &event);
    AppToolShellFixture::settle();
    QPointer<QMenu> menu = view->findChild<QMenu*>(QStringLiteral("bitonalModeMenu"));
    QVERIFY(menu);
    QAction* fillWhite = nullptr;
    for (QAction* action : menu->actions())
    {
        fillWhite = action->data().isValid() && action->data().toInt() == int(pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite) ? action : fillWhite;
    }
    QVERIFY(fillWhite);

    // A slot of the step of Undo reads the mode and closes the tab - only outside of the signal of the
    // menu (inside it the deletion would free the menu under QMenu itself; the test only records it then)
    bool isTriggerReturned = false;
    int changesInsideTrigger = 0;
    int changesAfterTrigger = 0;
    QString itemsAtChange;
    QObject context;
    QObject::connect(shell->getModel(), &pdf::PDFAppToolUndoTarget::undoStateChanged, &context, [&]()
    {
        if (!isTriggerReturned)
        {
            ++changesInsideTrigger;
            return;
        }
        ++changesAfterTrigger;
        itemsAtChange = H::items(shell);
        pdftest::AppDocumentFixture::deleteNow(tab.data());
    });
    fillWhite->trigger();
    isTriggerReturned = true;
    QCOMPARE(changesInsideTrigger, 0);

    // From the event loop: the first page is filled with white (one step of Undo), the slot has closed
    // the tab with the tool and the menu, the window lives on
    QVERIFY(AppToolShellFixture::waitFor([&tab]() { return !tab; }));
    AppToolShellFixture::settle();
    QCOMPARE(changesAfterTrigger, 1);
    QCOMPARE(itemsAtChange, QStringLiteral("1=fill-white"));
    QVERIFY(!shell);
    QVERIFY(!gui);
    QVERIFY(!menu);
    QVERIFY(fixture.window);
}

void AppToolBitonalTest::previewOfTheCore()
{
    // 304:5102: the preview of the current item - the rendered page and its black and white image
    // (R4-22: PDFBitonalDocumentCreator::createPreview), the threshold of Otsu in the line below
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);

    gui->selectItems({ 0 });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending() && !gui->getComparison()->getAfterImage().isNull(); }, 30000));
    AppToolShellFixture::settle();

    const QImage after = gui->getComparison()->getAfterImage();
    QCOMPARE(after.format(), QImage::Format_Mono);
    QCOMPARE(qGray(after.pixel(after.width() / 4, after.height() / 2)), 0);
    QCOMPARE(qGray(after.pixel(after.width() * 3 / 4, after.height() / 2)), 255);
    QVERIFY(!gui->getComparison()->getBeforeImage().isNull());
    QVERIFY2(gui->getFooterText().startsWith(QStringLiteral("Automatic threshold (Otsu): ")), qPrintable(gui->getFooterText()));
    QCOMPARE(gui->getCanvasTitle()->text(), QStringLiteral("Page 1"));

    // Fill with black: the right card is black
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::FillBlack);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending(); }, 30000));
    AppToolShellFixture::settle();
    const QImage filled = gui->getComparison()->getAfterImage();
    QVERIFY(!filled.isNull());
    QCOMPARE(qGray(filled.pixel(filled.width() * 3 / 4, filled.height() / 2)), 0);
}

void AppToolBitonalTest::canceledPreviewKeepsItsPage()
{
    // 304:5102: the ORIGINAL of the canvas is the rendered current page. The preview of page 2 is
    // canceled by a change of the method right after its start; the next preview of page 2 must
    // render page 2 (blank), not reuse the original of page 1 (its left half is black)
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);

    gui->selectItems({ 0 });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending() && !gui->getComparison()->getBeforeImage().isNull(); }, 30000));
    AppToolShellFixture::settle();
    const QImage first = gui->getComparison()->getBeforeImage();
    QCOMPARE(qGray(first.pixel(first.width() / 4, first.height() / 2)), 0);

    // The start of the next preview job: the method changes at once (the job is canceled)
    bool isCanceled = false;
    QObject context;
    QObject::connect(gui->getPreviewWatcher(), &QFutureWatcherBase::started, &context, [&isCanceled, shell]()
    {
        if (!isCanceled)
        {
            isCanceled = true;
            shell->getModel()->setValue(QStringLiteral("method"), QStringLiteral("threshold"));
        }
    });
    gui->selectItems({ 1 });
    QVERIFY(AppToolShellFixture::waitFor([&isCanceled]() { return isCanceled; }, 30000));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending(); }, 30000));
    AppToolShellFixture::settle();

    QCOMPARE(gui->getCanvasTitle()->text(), QStringLiteral("Page 2"));
    const QImage second = gui->getComparison()->getBeforeImage();
    QVERIFY(!second.isNull());
    QCOMPARE(qGray(second.pixel(second.width() / 4, second.height() / 2)), 255);
    QCOMPARE(qGray(second.pixel(second.width() * 3 / 4, second.height() / 2)), 255);
}

void AppToolBitonalTest::imageListSelectionKeepsCurrentRow()
{
    // Keyboard and Shift selection (plan 4.1): a selection made in the list stays as the user made
    // it - its current row is not moved to the first selected row by the update of the tool
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("images.pdf")), false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QCOMPARE(gui->getItemCount(), 2);

    QAbstractItemView* view = gui->getImageList()->getView();
    const QModelIndex firstRow = view->model()->index(0, 0);
    const QModelIndex secondRow = view->model()->index(1, 0);
    view->selectionModel()->setCurrentIndex(secondRow, QItemSelectionModel::NoUpdate);
    view->selectionModel()->select(QItemSelection(firstRow, secondRow), QItemSelectionModel::ClearAndSelect);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getSelectedItems().size() == 2; }));
    AppToolShellFixture::settle();

    QCOMPARE(view->selectionModel()->currentIndex().row(), 1);
    QVERIFY(view->selectionModel()->isSelected(firstRow));
    QVERIFY(view->selectionModel()->isSelected(secondRow));
}

void AppToolBitonalTest::blankPagesQuestion()
{
    // 307:6250: the detection finds the empty second page and asks; Replace with white fill gives
    // it the white fill (one undo step); 307:5873 the button counts the examined pages
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->toggleBlankPageDetection();
    QVERIFY(gui->isDetecting());
    QCOMPARE(gui->getPagesToExamineCount(), 3);
    QVERIFY(gui->getBlankPagesButton()->text().startsWith(QStringLiteral("Stop (")));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestion() && gui->getQuestion()->isVisible(); }, 30000));
    pdf::PDFMessageBox* question = gui->getQuestion();
    QVERIFY(!gui->isDetecting());
    QCOMPARE(question->findChild<QPushButton*>(QStringLiteral("bitonalBlankReplace"))->text(), QStringLiteral("Replace with white fill"));
    QCOMPARE(question->findChild<QPushButton*>(QStringLiteral("bitonalBlankNo"))->text(), QStringLiteral("No"));
    QCOMPARE(gui->getBlankPagesButton()->text(), QStringLiteral("Blank pages"));

    QTest::mouseClick(question->findChild<QPushButton*>(QStringLiteral("bitonalBlankReplace")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->getQuestion(); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::items(shell), QStringLiteral("2=fill-white"));
    QCOMPARE(gui->getPageGrid()->getModel()->getTile(1).tag, QStringLiteral("white"));
}

void AppToolBitonalTest::detectionStops()
{
    // Stop (n/N) ends the detection at once: no question, no mode changed, the Primary button works
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->toggleBlankPageDetection();
    QVERIFY(gui->isDetecting());
    gui->toggleBlankPageDetection();
    QVERIFY(!gui->isDetecting());
    QTest::qWait(500);
    AppToolShellFixture::settle();
    QVERIFY(!gui->getQuestion());
    QCOMPARE(H::items(shell), QString());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolBitonalTest::runToTheResultDialog()
{
    // 307:6564: Convert runs the tool, the result dialog shows Pages and Size, Save as a copy is
    // preselected with the name <name>_bw.pdf
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("dpi"), 72);
    gui->selectItems({ 1 });
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::Original);
    AppToolShellFixture::settle();

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
    AppToolShellFixture::settle();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);

    QCOMPARE(H::value(dialog->getFacts(), QStringLiteral("Pages")), QStringLiteral("2 converted · 1 unchanged"));
    QVERIFY(H::value(dialog->getFacts(), QStringLiteral("Size")).contains(QStringLiteral(" → ")));
    QVERIFY(dialog->getFileNameField());
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Scanned_Archive_bw.pdf"));

    // Every item converted: no question of a partial result before the dialog (307:6388)
    QVERIFY(!gui->getQuestion());
    QVERIFY(!shell->isConfirmingResult());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolBitonalTest::partialResultTexts()
{
    // 307:6388: "3 of 120 items could not be converted and they are left unchanged. Do you want to
    // use the partially converted document?", the detail "Not converted: pages 17, 43, 44
    // (rendering errors)" - the counts of the data of the core (a failed item counts only as not
    // converted), the detail its fact
    pdf::PDFDocumentToolResult result;
    QJsonArray notConverted;
    for (const int page : { 17, 43, 44 })
    {
        notConverted.append(QJsonObject({ { QStringLiteral("page"), page }, { QStringLiteral("reason"), QStringLiteral("rendering errors") } }));
    }
    result.data.insert(QStringLiteral("notConverted"), notConverted);
    result.data.insert(QStringLiteral("converted"), 100);
    result.data.insert(QStringLiteral("filledWhite"), 10);
    result.data.insert(QStringLiteral("filledBlack"), 2);
    result.data.insert(QStringLiteral("unchanged"), 5);
    result.facts.push_back({ QStringLiteral("notConverted"), QStringLiteral("Not converted"), QStringLiteral("pages 17, 43, 44 (rendering errors)") });

    const auto [text, detail] = PDFAppToolGUIBitonal::getPartialResultTexts(result);
    QCOMPARE(text, QStringLiteral("3 of 120 items could not be converted and they are left unchanged. Do you want to use the partially converted document?"));
    QCOMPARE(detail, QStringLiteral("Not converted: pages 17, 43, 44 (rendering errors)"));

    // Every item converted: no question
    result.data.insert(QStringLiteral("notConverted"), QJsonArray());
    QVERIFY(PDFAppToolGUIBitonal::getPartialResultTexts(result).first.isEmpty());

    // Owner 14. 11. 2026: pages with rendering issues are converted - they alone never ask
    result.facts.clear();
    result.facts.push_back({ QStringLiteral("renderingIssues"), QStringLiteral("Rendering issues"), QStringLiteral("pages 17, 43 – check these pages") });
    result.data.insert(QStringLiteral("renderingIssuePages"), QJsonArray({ 17, 43 }));
    result.renderingIssues.push_back({ 16, QStringLiteral("Can't load system font 'System'.") });
    QVERIFY(PDFAppToolGUIBitonal::getPartialResultTexts(result).first.isEmpty());
}

void AppToolBitonalTest::partialResultQuestion()
{
    // 307:6388: a run, in which an image could not be converted, asks before the result dialog;
    // No, discard drops the result (no dialog, the document stays), Use partial document opens the
    // result dialog with the fact Not converted
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Scan.pdf")), true));
    QVERIFY(tab);
    const pdf::PDFDocumentPointer original = tab->getDocument()->getDocument();

    QPointer<PDFDocumentToolShell> shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    QPointer<PDFAppToolGUIBitonal> gui = H::gui(shell);
    QCOMPARE(gui->getItemCount(), 2);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui && gui->getQuestion() && gui->getQuestion()->isVisible(); }, 60000));
    pdf::PDFMessageBox* question = gui->getQuestion();
    QCOMPARE(question->objectName(), QStringLiteral("bitonalPartialResultQuestion"));
    const QStringList texts = H::labelTexts(question);
    QVERIFY2(texts.contains(QStringLiteral("Convert to Black & White")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY2(texts.contains(QStringLiteral("1 of 2 items could not be converted and they are left unchanged. Do you want to use the partially converted document?")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY2(texts.contains(QStringLiteral("Not converted: image 2 (cannot be read)")), qPrintable(texts.join(QLatin1Char('|'))));
    QPushButton* discardButton = question->findChild<QPushButton*>(QStringLiteral("bitonalPartialDiscard"));
    QPushButton* useButton = question->findChild<QPushButton*>(QStringLiteral("bitonalPartialUse"));
    QVERIFY(discardButton && useButton);
    QCOMPARE(discardButton->text(), QStringLiteral("No, discard"));
    QCOMPARE(useButton->text(), QStringLiteral("Use partial document"));
    QVERIFY(useButton->isDefault());
    QVERIFY(!shell->getResultDialog());
    QVERIFY(shell->isConfirmingResult());

    // No, discard: as Discard of the dialog - the tool is left, the document is the same
    QTest::mouseClick(discardButton, Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell || shell->getState() == PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QVERIFY(!shell || !shell->getResultDialog());
    QCOMPARE(tab->getDocument()->getDocument(), original);
    QVERIFY(!tab->getDocument()->isBlockedByOperation());

    // Use partial document: the result dialog with the fact of the item, which was not converted
    shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    gui = H::gui(shell);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui && gui->getQuestion() && gui->getQuestion()->isVisible(); }, 60000));
    QTest::mouseClick(gui->getQuestion()->findChild<QPushButton*>(QStringLiteral("bitonalPartialUse")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell && shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 30000));
    AppToolShellFixture::settle();
    QVERIFY(!shell->isConfirmingResult());
    QCOMPARE(H::value(shell->getResultDialog()->getFacts(), QStringLiteral("Not converted")), QStringLiteral("image 2 (cannot be read)"));
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
}

void AppToolBitonalTest::imageModeBadge()
{
    // Source Images (306:5615): the mode of an image is the Tag of its row ("white"), its second line
    // keeps the size of its streams; a converted image has no Tag
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Photos.pdf")), false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QCOMPARE(gui->getItemCount(), 2);

    gui->selectItems({ 0 });
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite);
    AppToolShellFixture::settle();

    pdf::PDFAppToolImageListModel* model = gui->getImageList()->getModel();
    QVERIFY(model);
    QCOMPARE(model->rowCount(), 2);
    const QModelIndex white = model->index(0, 0);
    const QModelIndex converted = model->index(1, 0);
    QCOMPARE(white.data(pdf::PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("white"));
    QCOMPARE(white.data(pdf::PDFAppToolImageListModel::SizeTextRole).toString(), pdf::PDFDocumentToolFormat::formatSize(white.data(pdf::PDFAppToolImageListModel::BytesRole).toLongLong()));
    QCOMPARE(converted.data(pdf::PDFAppToolImageListModel::TagTextRole).toString(), QString());
    QCOMPARE(converted.data(pdf::PDFAppToolImageListModel::SizeTextRole).toString(), pdf::PDFDocumentToolFormat::formatSize(converted.data(pdf::PDFAppToolImageListModel::BytesRole).toLongLong()));
}

void AppToolBitonalTest::menuFieldsOfTheTheme()
{
    // 306:5353: the fields Resolution and Threshold of the menus are spin boxes of the theme, the
    // unit "dpi" drawn beside the value; a disabled field (Threshold with Method Auto) has no pixel
    // of the leak color of the palette (CLAUDE.md: a violet pixel is a bug)
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    pdf::PDFSpinBox* resolution = qobject_cast<pdf::PDFSpinBox*>(gui->getResolutionSpinBox());
    pdf::PDFSpinBox* threshold = qobject_cast<pdf::PDFSpinBox*>(gui->getThresholdSpinBox());
    QVERIFY(resolution && threshold);
    QCOMPARE(resolution->getDisplaySuffix(), QStringLiteral("dpi"));
    QVERIFY(resolution->suffix().isEmpty());
    QVERIFY(!threshold->isEnabled());

    // 304:5102 / 306:5353: the button Source and the row Whole pages have the icon "pages"
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getSourceButton()->icon()), QStringLiteral("pages"));

    QTest::mouseClick(gui->getMethodButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([threshold]() { return threshold->isVisible(); }));
    AppToolShellFixture::settle();
    const QImage picture = AppToolShellFixture::render(threshold, 1.0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(picture), 0);
}

void AppToolBitonalTest::canvasIsTheComparison()
{
    // 304:5102 as 297:3135: the canvas is the comparison - the title "Page 3" in the bar with Fit,
    // 100 %, Sync zoom and Show on page (the cards start 68 below the top: padding 20, the bar 36,
    // the gap 12), the value of the line below the cards and its muted note as two labels; Show
    // on page shows the page of the item in the view
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    gui->selectItems({ 2 });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending() && !gui->getComparison()->getAfterImage().isNull(); }, 30000));
    AppToolShellFixture::settle();

    pdf::PDFAppToolImageComparison* comparison = gui->getComparison();
    QCOMPARE(gui->getCanvasTitle(), comparison->getTitleLabel());
    QCOMPARE(comparison->getTitleLabel()->text(), QStringLiteral("Page 3"));
    QCOMPARE(comparison->getBeforePane()->mapTo(comparison, QPoint(0, 0)).y(), 68);
    QVERIFY(comparison->getShowOnPageButton()->isVisible());
    QVERIFY2(comparison->getFooterLabel()->text().startsWith(QStringLiteral("Automatic threshold (Otsu)")), qPrintable(comparison->getFooterLabel()->text()));
    QCOMPARE(comparison->getFooterDetailLabel()->text(), QStringLiteral("the preview updates shortly after the settings change"));

    // The status bar of 304:5102 ends with the hint, not with the size of the file
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Convert to Black & White · Scanned_Archive.pdf · 3 pages · right-click the items to change their mode"));

    // Show on page: a document of 12 pages, the item of page 10 - the view goes to it
    pdf::PDFAppDocumentTab* longTab = fixture.openDocument(12);
    QVERIFY(longTab && AppToolShellFixture::waitFor([longTab]() { return longTab->getDocument() && longTab->getDocument()->isReady(); }));
    AppToolShellFixture::settle();
    PDFDocumentToolShell* longShell = fixture.openTool(longTab, H::TOOL_ID);
    PDFAppToolGUIBitonal* longGui = H::gui(longShell);
    QVERIFY(longGui);
    longGui->selectItems({ 9 });
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentView* view = longShell->getHost()->getView();
    QVERIFY(view);
    QVERIFY(!view->isPageShown(9));
    QTest::mouseClick(longGui->getComparison()->getShowOnPageButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([view]() { return view->isPageShown(9) && !view->isPageShown(0); }));
}

void AppToolBitonalTest::imagesSourceHead()
{
    // 306:5615: Source Images has one head - the list "Images", "2 images · 1 selected", Sort - and
    // the thumbnails of its rows
    using H = BitonalGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Photos.pdf")), false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    gui->selectItems({ 0 });
    AppToolShellFixture::settle();

    QVERIFY(!gui->getLeftHeader()->isVisible());
    pdf::PDFAppToolImageList* list = gui->getImageList();
    QVERIFY(list->isVisible());
    QCOMPARE(list->getHeadLabel()->text(), QStringLiteral("2 images · 1 selected"));
    pdf::PDFAppToolImageListModel* model = list->getModel();
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->hasThumbnail(0) && model->hasThumbnail(1); }));

    // Back to the pages: the head of the panel again
    shell->getModel()->setValue(QStringLiteral("source"), QStringLiteral("pages"));
    AppToolShellFixture::settle();
    QVERIFY(gui->getLeftHeader()->isVisible());
    QCOMPARE(gui->getLeftHeader()->getTitleLabel()->text(), QStringLiteral("Pages"));
}

void AppToolBitonalTest::previewOfBothSources()
{
    // Owner 14. 11. (question 6): the list (or the grid) and the comparison ORIGINAL / BITONAL for
    // both sources - the preview really finishes: both cards draw their images, never the
    // placeholder "Computing the preview…"
    using H = BitonalGuiHelper;
    const auto isDrawn = [](const pdf::PDFAppToolImageComparisonPane* pane)
    {
        return pane && pane->isVisible() && !pane->getView()->getImage().isNull() && !pane->getView()->getImageRect().isEmpty();
    };

    AppToolShellFixture fixture(QSize(1440, 960));

    // Source Pages: page 1 has the black left half
    pdf::PDFAppDocumentTab* pagesTab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(pagesTab);
    PDFDocumentToolShell* pagesShell = fixture.openTool(pagesTab, H::TOOL_ID);
    PDFAppToolGUIBitonal* pagesGui = H::gui(pagesShell);
    QVERIFY(pagesGui);
    pagesGui->selectItems({ 0 });
    pdf::PDFAppToolImageComparison* comparison = pagesGui->getComparison();
    QVERIFY(AppToolShellFixture::waitFor([pagesGui, comparison, isDrawn]() { return !pagesGui->isPreviewPending() && isDrawn(comparison->getBeforePane()) && isDrawn(comparison->getAfterPane()); }, 30000));
    QCOMPARE(comparison->getBeforePane()->getCaptionLabel()->text(), QStringLiteral("ORIGINAL"));
    QVERIFY2(comparison->getAfterPane()->getCaptionLabel()->text().startsWith(QStringLiteral("BITONAL")), qPrintable(comparison->getAfterPane()->getCaptionLabel()->text()));
    const QImage pageBefore = comparison->getBeforeImage();
    const QImage pageAfter = comparison->getAfterImage();
    QCOMPARE(qGray(pageBefore.pixel(pageBefore.width() / 4, pageBefore.height() / 2)), 0);
    QCOMPARE(qGray(pageBefore.pixel(pageBefore.width() * 3 / 4, pageBefore.height() / 2)), 255);
    QCOMPARE(pageAfter.format(), QImage::Format_Mono);
    QCOMPARE(qGray(pageAfter.pixel(pageAfter.width() / 4, pageAfter.height() / 2)), 0);
    QCOMPARE(qGray(pageAfter.pixel(pageAfter.width() * 3 / 4, pageAfter.height() / 2)), 255);

    // Source Images: the image of a black and a white pixel, the original and the converted one
    pdf::PDFAppDocumentTab* imagesTab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Photos.pdf")), false));
    QVERIFY(imagesTab);
    PDFDocumentToolShell* imagesShell = H::openImagesSource(fixture, imagesTab);
    QVERIFY(imagesShell);
    PDFAppToolGUIBitonal* imagesGui = H::gui(imagesShell);
    QVERIFY(imagesGui->getImageList()->isVisible());
    imagesGui->selectItems({ 0 });
    comparison = imagesGui->getComparison();
    QVERIFY(AppToolShellFixture::waitFor([imagesGui, comparison, isDrawn]() { return !imagesGui->isPreviewPending() && isDrawn(comparison->getBeforePane()) && isDrawn(comparison->getAfterPane()); }, 30000));
    QCOMPARE(comparison->getTitleLabel()->text(), QStringLiteral("Image 1"));
    QCOMPARE(comparison->getBeforePane()->getCaptionLabel()->text(), QStringLiteral("ORIGINAL"));
    const QImage imageBefore = comparison->getBeforeImage();
    const QImage imageAfter = comparison->getAfterImage();
    QCOMPARE(imageBefore.size(), QSize(2, 1));
    QCOMPARE(qGray(imageBefore.pixel(0, 0)), 0);
    QCOMPARE(qGray(imageBefore.pixel(1, 0)), 255);
    QCOMPARE(imageAfter.size(), QSize(2, 1));
    QCOMPARE(imageAfter.format(), QImage::Format_Mono);
    QCOMPARE(qGray(imageAfter.pixel(0, 0)), 0);
    QCOMPARE(qGray(imageAfter.pixel(1, 0)), 255);
}

void AppToolBitonalTest::previewUsesTheColorManagementOfTheRun()
{
    // RS4APP: the preview renders the page with the color management of the settings, as the run
    // does (PDFAppDocumentView::createToolRenderContext), not with the defaults of the core. The
    // left half of the page is 100 % K (0 0 0 1 k): LittleCMS of the default settings converts it
    // by the CMYK profile to a dark gray, the generic conversion to black (ISO 32000-2, 10.4.2.4:
    // 1 - min(1, C + K) = 0)
    using H = BitonalGuiHelper;
    const auto writeCmykPage = [](const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 100));
        const QByteArray data("0 0 0 1 k 0 0 100 100 re f");
        pdf::PDFDictionaryBuilder content;
        content.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(content), QByteArray(data))));
        pdf::PDFDictionaryBuilder update;
        update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    };
    const auto originalLeftGray = [](AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
        PDFAppToolGUIBitonal* gui = tab ? H::gui(fixture.openTool(tab, H::TOOL_ID)) : nullptr;
        if (!gui)
        {
            return -1;
        }
        gui->selectItems({ 0 });
        if (!AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending() && !gui->getComparison()->getBeforeImage().isNull(); }, 30000))
        {
            return -1;
        }
        const QImage before = gui->getComparison()->getBeforeImage();
        return qGray(before.pixel(before.width() / 4, before.height() / 2));
    };

    AppToolShellFixture fixture;
    pdf::PDFAppSettingsValueStore store(fixture.context.get());
    QVERIFY(store.isAvailable());
    QCOMPARE(pdf::PDFAppDocumentView::createToolRenderContext(fixture.context.get()).cmsSettings->system, pdf::PDFCMSSettings::System::LittleCMS2);
    const int managed = originalLeftGray(fixture, writeCmykPage(fixture.path(QStringLiteral("Managed.pdf"))));
    QVERIFY2(managed > 10 && managed < 128, qPrintable(QString::number(managed)));

    QCOMPARE(store.writeValue(pdf::PDFSettingKeys::cmsSystem(), QStringLiteral("generic")), QString());
    const int generic = originalLeftGray(fixture, writeCmykPage(fixture.path(QStringLiteral("Generic.pdf"))));
    QCOMPARE(generic, 0);
}

void AppToolBitonalTest::numbersOfTheLocale()
{
    // RS4APP, CLAUDE.md (O17): the numbers of Black & White are written by the locale of the
    // application (PDFAppLocale) - Arabic of Egypt has its own digits
    using H = BitonalGuiHelper;
    const QLocale previous = pdf::PDFAppLocale::getUiLocale();
    const auto restore = qScopeGuard([&previous]() { pdf::PDFAppLocale::setUiLocale(previous); });
    const QLocale arabic(QLocale::Arabic, QLocale::Egypt);
    QVERIFY(arabic.toString(3) != QStringLiteral("3"));

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFAppToolGUIBitonal* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(gui);

    // (after the fixture: the context applies the locale of its settings)
    pdf::PDFAppLocale::setUiLocale(arabic);
    QCOMPARE(pdf::PDFAppLocale::formatNumber(3), arabic.toString(3));
    gui->selectItems({ 0, 2 }, 2);
    AppToolShellFixture::settle();

    QCOMPARE(gui->getSelectedHeader()->getHintLabel()->text(), QStringLiteral("%1 of %2").arg(arabic.toString(2), arabic.toString(3)));
    QCOMPARE(gui->getComparison()->getTitleLabel()->text(), QStringLiteral("Page %1").arg(arabic.toString(3)));
    QCOMPARE(H::value(gui->getSelectedRows(), QStringLiteral("Pages")), QStringLiteral("%1, %2").arg(arabic.toString(1), arabic.toString(3)));
    QVERIFY2(H::value(gui->getSelectedRows(), QStringLiteral("Mode")).startsWith(arabic.toString(2)), qPrintable(H::value(gui->getSelectedRows(), QStringLiteral("Mode"))));
}

void AppToolBitonalTest::spaceInTheImageList()
{
    // RS4APP: Space of the list of the images does what it does in Optimize Images (Exclude) - the
    // selected images are left unchanged (mode Original), Space again converts them; one undo step
    using H = BitonalGuiHelper;
    using Mode = pdf::PDFBitonalDocumentCreator::ItemMode;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Photos.pdf")), false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    pdf::PDFAppToolImageList* list = gui->getImageList();
    QVERIFY(list && list->isVisible());

    // The first image selected, then Space in the list
    gui->selectItems({ 0 });
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedItems(), std::vector<int>({ 0 }));
    QCOMPARE(list->getSelectedKeys().size(), size_t(1));
    list->getView()->setFocus();
    QTest::keyClick(list->getView(), Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getItemMode(0) == Mode::Original; }));
    QCOMPARE(gui->getItemMode(1), Mode::Algorithm);

    QTest::keyClick(list->getView(), Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getItemMode(0) == Mode::Algorithm; }));

    // Undo returns the unchanged image
    shell->getToolbar()->getUndoButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getItemMode(0) == Mode::Original; }));
}

void AppToolBitonalTest::documentWithoutPages()
{
    // RS4APP (hostile input): a document without pages has nothing to convert - Convert is
    // disabled for Source Pages (the page tree of ISO 32000-2, 7.7.3.2 may have /Count 0)
    using H = BitonalGuiHelper;
    pdf::PDFDocumentBuilder builder;
    pdf::PDFDocument document = builder.build();
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(0));

    AppToolShellFixture fixture;
    const QString filePath = fixture.path(QStringLiteral("Empty.pdf"));
    QVERIFY(pdftest::AppDocumentSamples::write(document, filePath));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getItemCount(), 0);
    QVERIFY(!gui->isConfirmEnabled());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolBitonalTest::modesFollowTheNewImageList()
{
    // P2S4FIXCORE / coordinator: the modes of the items are cached by the identity of the list of the
    // images (a generation, never its address, which a new list may reuse). A new version of the
    // document without the first image: the second image is item 0 now and keeps its mode
    using H = BitonalGuiHelper;
    using Mode = pdf::PDFBitonalDocumentCreator::ItemMode;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Photos.pdf")), false));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openImagesSource(fixture, tab);
    QVERIFY(shell);
    PDFAppToolGUIBitonal* gui = H::gui(shell);
    QCOMPARE(gui->getItemCount(), 2);
    gui->selectItems({ 1 });
    gui->setModeToItems(Mode::Original);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getItemMode(1), Mode::Original);
    QCOMPARE(gui->getItemMode(0), Mode::Algorithm);

    // The new version: the page draws only the second image (object 11, the same id)
    const QByteArray pixels("\x00\x00\x00\xFF\xFF\xFF", 6);
    const QByteArray image = "/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB";
    mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("B", 1, QByteArray(), { { 1, "/Resources << /XObject << /Im2 11 0 R >> >>" } });
    pdf.set(11, mergetest::MergeTestPdf::stream(pixels, image));
    pdf.set(101, mergetest::MergeTestPdf::stream("q 200 0 0 100 50 400 cm /Im2 Do Q"));
    const QString secondPath = fixture.path(QStringLiteral("Second.pdf"));
    QFile file(secondPath);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(pdf.write());
    file.close();
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(reader.readFromFile(secondPath)));
    QVERIFY(changed->getCatalog()->getPageCount() == 1);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isCollectingImages() && gui->getItemCount() == 1; }, 30000));
    QCOMPARE(gui->getItemMode(0), Mode::Original);
}

void AppToolBitonalTest::gridTagsOnThePanel()
{
    // RS4FIGMA, 304:5102: the tiles of the pages lie on the side panel (no gray canvas of a tool),
    // the mode of a page is a Tag over the top trailing corner of its page (TAG_INSET from both edges)
    using H = BitonalGuiHelper;
    using Delegate = pdf::PDFAppToolPageTileDelegate;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
    QVERIFY(tab);
    PDFAppToolGUIBitonal* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(gui);
    gui->selectItems({ 0 });
    gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite);
    gui->selectItems({ });
    AppToolShellFixture::settle();

    pdf::PDFAppToolPageGrid* grid = gui->getPageGrid();
    QVERIFY(!grid->isCanvasPainted());
    Delegate* delegate = qobject_cast<Delegate*>(grid->getDelegate());
    QVERIFY(delegate);
    const QModelIndex first = grid->getModel()->index(0, 0);
    const QModelIndex second = grid->getModel()->index(1, 0);
    QStyleOptionViewItem option;
    option.initFrom(grid->getView()->viewport());
    option.rect = grid->getView()->visualRect(first);
    const Delegate::MarkRects rects = delegate->getMarkRects(option, first);
    QVERIFY(!rects.tag.isEmpty());
    QCOMPARE(rects.tag.top(), rects.page.top() + Delegate::TAG_INSET);
    QCOMPARE(rects.tag.right(), rects.page.right() - Delegate::TAG_INSET);
    option.rect = grid->getView()->visualRect(second);
    QVERIFY(delegate->getMarkRects(option, second).tag.isEmpty());

    // Drawn: the fill of the Tag (bg/subtle) over the white page
    const QImage window = fixture.window->grab().toImage();
    const QPoint tagPoint = grid->getView()->viewport()->mapTo(fixture.window, QPointF(rects.tag.left() + 4.0, rects.tag.center().y()).toPoint());
    const qreal ratio = window.devicePixelRatio();
    const QColor tagColor = window.pixelColor(QPointF(tagPoint * ratio).toPoint());
    QCOMPARE(tagColor, pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSubtle));

    // The panel below the tiles: the color of the panel, not bg/track
    const QPoint below = grid->mapTo(fixture.window, QPoint(grid->width() - 3, grid->height() - 3));
    const QColor belowColor = window.pixelColor(QPointF(below * ratio).toPoint());
    QVERIFY2(belowColor != pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgTrack), qPrintable(belowColor.name()));
}

void AppToolBitonalTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> writes the frames under their ids (304:5102, 306:5353,
    // 306:5615, 307:5873, 307:6250, 307:6388, 307:6564) in both themes, at 1x / 2x and
    // right-to-left, for the comparison with Figma; no pixel of the leak color (CLAUDE.md, Fluent)
    using H = BitonalGuiHelper;
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString name = QString::fromLatin1(variant);
        const QString suffix = name == QStringLiteral("light") ? QString() : QStringLiteral("-") + name;
        pdf::PDFThemeManager::getInstance()->setTheme(name == QStringLiteral("dark") ? QStringLiteral("dark") : QStringLiteral("light"));
        QApplication::setLayoutDirection(name == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Scanned_Archive.pdf"))));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
        PDFAppToolGUIBitonal* gui = H::gui(shell);
        QVERIFY(gui);
        gui->selectItems({ 0 });
        gui->setModeToItems(pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite);
        gui->selectItems({ 2 });
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending() && !gui->getComparison()->getAfterImage().isNull(); }, 30000));
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("optimize-bw-304-5102") + suffix)), 0);

        QTest::mouseClick(gui->getMethodButton(), Qt::LeftButton);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("optimize-bw-306-5353-method") + suffix);
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            popup->close();
        }
        AppToolShellFixture::settle();

        gui->toggleBlankPageDetection();
        fixture.screenshot(QStringLiteral("optimize-bw-307-5873") + suffix);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestion() && gui->getQuestion()->isVisible(); }, 30000));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("optimize-bw-307-6250") + suffix);
        AppToolShellFixture::screenshotDialog(gui->getQuestion(), QStringLiteral("optimize-bw-307-6250-dialog") + suffix);
        QTest::mouseClick(gui->getQuestion()->findChild<QPushButton*>(QStringLiteral("bitonalBlankNo")), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->getQuestion(); }));
        AppToolShellFixture::settle();

        shell->getModel()->setValue(QStringLiteral("dpi"), 72);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("optimize-bw-307-6564-dialog") + suffix);
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        // Source Images 306:5615 and the question of a partial result 307:6388
        pdf::PDFAppDocumentTab* imagesTab = H::openReady(fixture, H::writeImageDocument(fixture.path(QStringLiteral("Scan.pdf")), true));
        QVERIFY(imagesTab);
        PDFDocumentToolShell* imagesShell = H::openImagesSource(fixture, imagesTab);
        QVERIFY(imagesShell);
        PDFAppToolGUIBitonal* imagesGui = H::gui(imagesShell);
        imagesGui->selectItems({ 0 });
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("optimize-bw-306-5615") + suffix)), 0);
        imagesShell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([imagesGui]() { return imagesGui->getQuestion() && imagesGui->getQuestion()->isVisible(); }, 60000));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(imagesGui->getQuestion(), QStringLiteral("optimize-bw-307-6388-dialog") + suffix);
        QTest::mouseClick(imagesGui->getQuestion()->findChild<QPushButton*>(QStringLiteral("bitonalPartialDiscard")), Qt::LeftButton);
        AppToolShellFixture::settle();
    }
}

QTEST_MAIN(AppToolBitonalTest)

#include "tst_apptoolbitonaltest.moc"
