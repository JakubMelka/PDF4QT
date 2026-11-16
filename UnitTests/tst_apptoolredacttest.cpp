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

// The GUI of Redact Content and its search (step 6 of phase 2, plan 2.5, 9; package P5): the frames
// 437:10393 ... 448:17473 (texts and counts), the marks as Redact annotations of the document (ISO
// 32000-2 12.5.6.23: /Rect, /QuadPoints in the space of the page) and one Undo step of the document
// each, the inputs Area and Text on the canvas, Pages…, the search (a regular expression, Across line
// breaks K6.21, the selection of the results), the menus Fill and Keep, the preview, the question at
// the close (Remove Marks in one step) and the canary of the search term (R6-19). The documents are
// written by hand.

#include "apptoolshellfixture.h"

#include "pdfappannotationoperations.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolgui_redact.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolredactsearchpart.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolundotarget.h"
#include "pdfappundostack.h"
#include "pdfdatabase.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocument.h"
#include "pdfdocumenttool_redact.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstreamfilters.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QFile>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

#include <map>

using pdftest::AppToolShellFixture;
using pdf::PDFAppToolGUIRedact;
using pdf::PDFDocumentToolShell;
using Tool = pdf::PDFDocumentToolRedact;

/// A Redact annotation read from the dictionary of the document (not through the GUI)
struct RedactAnnotationEntry
{
    int pageIndex = -1;
    pdf::PDFObjectReference reference;
    std::vector<pdf::PDFReal> rect;
    std::vector<pdf::PDFReal> quadPoints;
    QString subject;
    bool hasInteriorColor = false;
};

/// PDF files written by hand and the helpers of the tests
class RedactAppHelper
{
public:
    RedactAppHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("protect.redact");

    static QByteArray build(const std::map<int, QByteArray>& objects)
    {
        QByteArray data = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::map<int, qint64> offsets;
        int maximum = 0;
        for (const auto& [number, body] : objects)
        {
            offsets[number] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
            maximum = qMax(maximum, number);
        }
        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(maximum + 1) + "\n0000000000 65535 f \n";
        for (int number = 1; number <= maximum; ++number)
        {
            const auto it = offsets.find(number);
            data += it != offsets.end() ? QByteArray::number(it->second).rightJustified(10, '0') + " 00000 n \n" : QByteArray("0000000000 00000 f \n");
        }
        data += "trailer\n<< /Size " + QByteArray::number(maximum + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    static QByteArray stream(const QByteArray& data)
    {
        const QByteArray content = pdf::PDFFlateDecodeFilter::compress(data);
        return "<< /Length " + QByteArray::number(content.size()) + " /Filter /FlateDecode >>\nstream\n" + content + "\nendstream";
    }

    /// Page 1 (crop box [36 36 576 756]): "Contact jane.novak@example.com today", the phrase "Revenue grew
    /// by" / "18 percent this year" over two lines of one paragraph, "Secret plan"; page 2: "The Secret
    /// project"; page 3: a blue square, no text (a scanned page)
    static QByteArray document()
    {
        std::map<int, QByteArray> objects;
        objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
        objects[2] = "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>";
        objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /CropBox [36 36 576 756] /Contents 6 0 R /Resources << /Font << /F1 9 0 R >> >> >>";
        objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R /Resources << /Font << /F1 9 0 R >> >> >>";
        objects[5] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 8 0 R >>";
        objects[6] = stream("BT /F1 12 Tf 72 700 Td (Contact jane.novak@example.com today) Tj ET\n"
                            "BT /F1 12 Tf 72 640 Td (Revenue grew by) Tj 0 -14 Td (18 percent this year) Tj ET\n"
                            "BT /F1 12 Tf 72 560 Td (Secret plan) Tj ET");
        objects[7] = stream("BT /F1 12 Tf 72 700 Td (The Secret project) Tj ET");
        objects[8] = stream("0 0 1 rg 100 100 200 200 re f");
        objects[9] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
        return build(objects);
    }

    /// /QuadPoints of \p count quadrilaterals (each the square 100..110 x 400..410, ISO 32000-2 Table 183 order)
    static QByteArray quadPoints(int count)
    {
        QByteArray result = "[";
        for (int i = 0; i < count; ++i)
        {
            result += " 100 410 110 410 100 400 110 400";
        }
        return result + " ]";
    }

    /// document() with marks of another application on page 1: a Locked one (/F 128, ISO 32000-2 Table
    /// 167) over 100..150 x 600..650, a free one over 300..350 x 600..650, one of 1000 quadrilaterals and
    /// one of 1001 (the limit of a run is 1000, plan 7.2)
    static QByteArray documentWithForeignMarks()
    {
        std::map<int, QByteArray> objects;
        objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
        objects[2] = "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>";
        objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /CropBox [36 36 576 756] /Contents 6 0 R /Resources << /Font << /F1 9 0 R >> >> /Annots [10 0 R 11 0 R 12 0 R 13 0 R] >>";
        objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R /Resources << /Font << /F1 9 0 R >> >> >>";
        objects[5] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 8 0 R >>";
        objects[6] = stream("BT /F1 12 Tf 72 700 Td (Contact jane.novak@example.com today) Tj ET\n"
                            "BT /F1 12 Tf 72 640 Td (Revenue grew by) Tj 0 -14 Td (18 percent this year) Tj ET\n"
                            "BT /F1 12 Tf 72 560 Td (Secret plan) Tj ET");
        objects[7] = stream("BT /F1 12 Tf 72 700 Td (The Secret project) Tj ET");
        objects[8] = stream("0 0 1 rg 100 100 200 200 re f");
        objects[9] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
        objects[10] = "<< /Type /Annot /Subtype /Redact /P 3 0 R /F 128 /Rect [100 600 150 650] /C [1 0 0] >>";
        objects[11] = "<< /Type /Annot /Subtype /Redact /P 3 0 R /Rect [300 600 350 650] /C [1 0 0] >>";
        objects[12] = "<< /Type /Annot /Subtype /Redact /P 3 0 R /Rect [100 400 110 410] /C [1 0 0] /QuadPoints " + quadPoints(1000) + " >>";
        objects[13] = "<< /Type /Annot /Subtype /Redact /P 3 0 R /Rect [100 400 110 410] /C [1 0 0] /QuadPoints " + quadPoints(1001) + " >>";
        return build(objects);
    }

    static QString write(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly))
        {
            return QString();
        }
        file.write(data);
        return filePath;
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& name = QStringLiteral("Project_Report.pdf"))
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(write(fixture.path(name), document()));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static PDFAppToolGUIRedact* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIRedact*>(shell->getGUI()) : nullptr;
    }

    /// The Redact annotations of the document as its dictionaries say
    static std::vector<RedactAnnotationEntry> annotations(const pdf::PDFAppDocument* appDocument)
    {
        std::vector<RedactAnnotationEntry> entries;
        const pdf::PDFDocumentPointer document = appDocument ? appDocument->getDocument() : pdf::PDFDocumentPointer();
        if (!document)
        {
            return entries;
        }

        pdf::PDFDocumentDataLoaderDecorator loader(document.data());
        const pdf::PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            for (const pdf::PDFObjectReference& reference : catalog->getPage(i)->getAnnotations())
            {
                const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(reference));
                if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") != "Redact")
                {
                    continue;
                }

                RedactAnnotationEntry entry;
                entry.pageIndex = int(i);
                entry.reference = reference;
                entry.rect = loader.readNumberArrayFromDictionary(dictionary, "Rect");
                entry.quadPoints = loader.readNumberArrayFromDictionary(dictionary, "QuadPoints");
                entry.subject = loader.readTextStringFromDictionary(dictionary, "Subj", QString());
                entry.hasInteriorColor = !loader.readNumberArrayFromDictionary(dictionary, "IC").empty();
                entries.push_back(entry);
            }
        }
        return entries;
    }

    static QStringList rows(const pdf::PDFKeyValueList* list)
    {
        QStringList rows;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            rows << list->getKey(i) + QStringLiteral(": ") + list->getValue(i);
        }
        return rows;
    }

    static QString statusMessage(pdf::PDFAppDocumentTab* tab)
    {
        return tab->getView()->getStatusBar()->getMessage();
    }

    static QToolButton* segment(PDFAppToolGUIRedact* gui, int index)
    {
        const QList<QToolButton*> segments = gui->getOverlay()->getModeControl()->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        return index < segments.size() ? segments[index] : nullptr;
    }

    /// /Rect [llx lly urx ury] holds the area and at most its border of 1 pt around it
    static bool isRectAround(const std::vector<pdf::PDFReal>& rect, const QRectF& area)
    {
        return rect.size() == 4 && rect[0] <= area.left() && rect[0] >= area.left() - 1.0 && rect[1] <= area.top() && rect[1] >= area.top() - 1.0 &&
               rect[2] >= area.right() && rect[2] <= area.right() + 1.0 && rect[3] >= area.bottom() && rect[3] <= area.bottom() + 1.0;
    }

    /// The message box shows the text (its title, its text)
    static bool hasLabel(const QWidget* dialog, const QString& text)
    {
        for (const QLabel* label : dialog->findChildren<QLabel*>())
        {
            if (label->text() == text)
            {
                return true;
            }
        }
        return false;
    }

    static QStringList buttonTexts(const pdf::PDFMessageBox* dialog)
    {
        QStringList texts;
        for (const QPushButton* button : dialog->getActionButtons())
        {
            texts << button->text();
        }
        return texts;
    }

    /// Types the term into the field of the search and presses Enter, then waits for the end of the search
    static bool search(PDFAppToolGUIRedact* gui, const QString& term)
    {
        pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
        if (!part || !part->getField())
        {
            return false;
        }
        part->getField()->clear();
        QTest::keyClicks(part->getField(), term);
        QTest::keyClick(part->getField(), Qt::Key_Return);
        return AppToolShellFixture::waitFor([part]()
        {
            return part->getState() != pdf::PDFAppToolRedactSearchPart::State::Indexing && part->getState() != pdf::PDFAppToolRedactSearchPart::State::Searching;
        });
    }

    static bool contains(const QByteArray& data, const QString& text)
    {
        const QByteArray utf16(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
        return data.contains(text.toUtf8()) || data.contains(utf16);
    }

    /// The point of a rectangle of the page in the picture of the canvas
    static QColor canvasPixel(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint)
    {
        bool isShown = false;
        const QPoint point = pdftest::AppDocumentFixture::pageToWidget(view, pageIndex, pagePoint, &isShown);
        QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
        if (!isShown || !canvas)
        {
            return QColor();
        }
        const QImage image = canvas->grab().toImage();
        const QPoint pixel(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio()));
        return image.rect().contains(pixel) ? image.pixelColor(pixel) : QColor();
    }

    static bool isNear(const QColor& color, const QColor& expected, int tolerance = 8)
    {
        return color.isValid() && qAbs(color.red() - expected.red()) <= tolerance && qAbs(color.green() - expected.green()) <= tolerance && qAbs(color.blue() - expected.blue()) <= tolerance;
    }
};

using H = RedactAppHelper;

class AppToolRedactTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void registered();
    void startFrame();
    void areaMarkFrame();
    void areaByMouse();
    void textByMouse();
    void textOnPageWithoutText();
    void pagesDialog();
    void searchMarksSelected();
    void searchInvalidExpression();
    void searchAcrossLineBreaks();
    void searchCanary();
    void searchClearForgets();
    void searchResultsOfOldPagesDropped();
    void removeRow();
    void clearAll();
    void closePromptRemove();
    void closePromptKeepAndBack();
    void closePromptAfterDiscard();
    void undoMarksInTool();
    void undoMarksNotForeignChange();
    void undoMarksWaitForBlockedDocument();
    void fillMenu();
    void foreignMarksBounded();
    void lockedMarkStays();
    void escapeEndsDrag();
    void fillSwatches();
    void keepMenu();
    void previewPaintsFill();
    void marksKeepContentVisible();
    void runParameters();

    // Review RS6FIGMA
    void searchHitColors();
    void summaryRowGap();
    void screenshots();
};

void AppToolRedactTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolRedactTest::registered()
{
    // The context registers the core tool and its GUI (PDFAppBuiltinTools::registerAll); a second call
    // replaces nothing
    AppToolShellFixture fixture;
    QVERIFY(pdf::PDFDocumentToolRegistry::getImplementedTools().contains(H::TOOL_ID));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(H::TOOL_ID));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(H::TOOL_ID);
    const auto factory = pdf::PDFDocumentToolGUIRegistry::getFactory(H::TOOL_ID);
    PDFAppToolGUIRedact::registerRedactTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(H::TOOL_ID), tool);
    QCOMPARE(pdf::PDFDocumentToolGUIRegistry::getFactory(H::TOOL_ID), factory);
}

void AppToolRedactTest::startFrame()
{
    // 437:10726 (with the corrections of P2S6FIGMA): nothing marked - the Primary button, Preview result,
    // Clear All and Undo (K6.10) Disabled
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Create Redacted Copy"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QCOMPARE(gui->getAreaButton()->text(), QStringLiteral("Area"));
    QCOMPARE(gui->getTextButton()->text(), QStringLiteral("Text"));
    QCOMPARE(gui->getPagesButton()->text(), QStringLiteral("Pages…"));
    QCOMPARE(gui->getSearchButton()->text(), QStringLiteral("Search…"));
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: Black"));
    QCOMPARE(gui->getKeepButton()->text(), QStringLiteral("Keep"));
    QCOMPARE(gui->getClearButton()->text(), QStringLiteral("Clear All"));
    QVERIFY(!gui->getClearButton()->isEnabled());
    QVERIFY(gui->getAreaButton()->isEnabled());

    // The left panel: Marked areas / Nothing marked yet and the empty state
    QCOMPARE(gui->getLeftHeader()->getTitleLabel()->text(), QStringLiteral("Marked areas"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("Nothing marked yet"));
    QVERIFY(gui->getEmptyCard()->isVisible());
    QVERIFY(!gui->getMarksCard()->isVisible());
    QVERIFY(gui->getEmptyCard()->findChild<pdf::PDFLabel*>(QStringLiteral("redactEmptyTitle"))->text() == QStringLiteral("Mark what should disappear"));

    // The right panel: no amber callout, Result "Nothing yet", the blue callout with K6.23
    QCOMPARE(gui->getRightHeader()->getTitleLabel()->text(), QStringLiteral("Redacted copy"));
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("What the new file will contain"));
    QVERIFY(!gui->getMarksCallout()->isVisible());
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Removed: Nothing yet"), QStringLiteral("Fill: Black"), QStringLiteral("Title: Kept"),
                                                            QStringLiteral("Metadata: Kept"), QStringLiteral("Outline: Not copied"), QStringLiteral("Saved as: New file") }));
    QCOMPARE(gui->getPagesOnlyCallout()->getTitleLabel()->text(), QStringLiteral("The copy contains pages only"));
    QCOMPARE(gui->getPagesOnlyCallout()->getTextLabel()->text(), QStringLiteral("Every page is rebuilt. Comments, form fields, links and attachments are not carried over, so nothing hidden survives. Text becomes shapes and cannot be searched."));

    // The canvas: Page 1, Marks | Preview result, Preview Disabled without marks
    QCOMPARE(gui->getOverlay()->getPageText(), QStringLiteral("Page 1"));
    QCOMPARE(H::segment(gui, 0)->text(), QStringLiteral("Marks"));
    QCOMPARE(H::segment(gui, 1)->text(), QStringLiteral("Preview result"));
    QVERIFY(!H::segment(gui, 1)->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Mark the content that should be removed"));
}

void AppToolRedactTest::areaMarkFrame()
{
    // An area mark: one Redact annotation (12.5.6.23) of the area cut to the crop box, one Undo step of
    // the document, the tab unsaved; the frame 437:10393 counts it
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
    QVERIFY(!document->isModified());

    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 52, 80)));
    std::vector<RedactAnnotationEntry> entries = H::annotations(document);
    QCOMPARE(entries.size(), size_t(1));
    QCOMPARE(entries[0].pageIndex, 0);
    // QuadPoints: the corners of the area, the order of PDF (top left, top right, bottom left, bottom
    // right); /Rect holds the appearance of the mark (the area and its border of 1 pt, Table 166)
    QCOMPARE(entries[0].quadPoints, std::vector<pdf::PDFReal>({ 100, 680, 152, 680, 100, 600, 152, 600 }));
    QVERIFY(H::isRectAround(entries[0].rect, QRectF(100, 600, 52, 80)));
    QCOMPARE(entries[0].subject, QStringLiteral("Redact area"));
    QVERIFY(!entries[0].hasInteriorColor);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);
    QVERIFY(document->isModified());
    AppToolShellFixture::settle();

    // The parts: 1 area on 1 page, the row Area "52 × 80 pt", the caption, the amber callout
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("1 area on 1 page"));
    QVERIFY(gui->getMarksCard()->isVisible());
    pdf::PDFAppToolRedactMarkModel* model = gui->getMarkModel();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->index(0).data(Qt::DisplayRole).toString(), QStringLiteral("Page 1 · 1 area"));
    QCOMPARE(model->index(1).data(Qt::DisplayRole).toString(), QStringLiteral("Area"));
    QCOMPARE(model->index(1).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("52 × 80 pt"));
    QVERIFY(gui->getMarksCallout()->isVisible());
    QCOMPARE(gui->getMarksCallout()->getTitleLabel()->text(), QStringLiteral("1 area marked – not removed yet"));
    QCOMPARE(gui->getMarksCallout()->getTextLabel()->text(), QStringLiteral("The content is still in this document. It is removed only in the redacted copy."));
    QCOMPARE(H::rows(gui->getSummaryRows()).front(), QStringLiteral("Removed: 1 area · 1 page"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(gui->getClearButton()->isEnabled());
    QVERIFY(H::segment(gui, 1)->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("1 area marked on 1 page · not removed yet – the content is removed only in the redacted copy"));

    // An area over the edge of the crop box [36 36 576 756] is cut to it; a second area of page 2
    QVERIFY(gui->addAreaMark(0, QRectF(0, 0, 100, 100)));
    QVERIFY(gui->addAreaMark(1, QRectF(200, 200, 10, 20)));
    entries = H::annotations(document);
    QCOMPARE(entries.size(), size_t(3));
    QCOMPARE(entries[1].quadPoints, std::vector<pdf::PDFReal>({ 36, 100, 100, 100, 36, 36, 100, 36 }));
    QCOMPARE(entries[2].pageIndex, 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 3);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("3 areas on 2 pages"));
    QCOMPARE(gui->getMarksCallout()->getTitleLabel()->text(), QStringLiteral("3 areas marked – not removed yet"));
    QCOMPARE(model->index(0).data(Qt::DisplayRole).toString(), QStringLiteral("Page 1 · 2 areas"));

    // A click without an area is no mark
    QVERIFY(!gui->addAreaMark(0, QRectF(300, 300, 0.5, 40)));
    QCOMPARE(H::annotations(document).size(), size_t(3));
}

void AppToolRedactTest::areaByMouse()
{
    // 437:10982: the mode Area, a rectangle dragged over the page of the canvas
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getAreaButton()->click();
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::Area);
    QVERIFY(gui->getAreaButton()->isChecked());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Area · drag a rectangle over the content · Esc leaves the tool"));

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(120, 690), QPointF(220, 610)));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return H::annotations(tab->getDocument()).size() == 1; }));
    const std::vector<RedactAnnotationEntry> entries = H::annotations(tab->getDocument());
    QCOMPARE(entries[0].pageIndex, 0);
    QCOMPARE(entries[0].quadPoints.size(), size_t(8));
    // The pixels of the canvas: the area (its quadrilateral) is exact to a point of the page
    const std::vector<pdf::PDFReal>& quad = entries[0].quadPoints;
    QVERIFY2(qAbs(quad[0] - 120) < 1.0 && qAbs(quad[1] - 690) < 1.0 && qAbs(quad[2] - 220) < 1.0 && qAbs(quad[5] - 610) < 1.0,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(quad[0]).arg(quad[1]).arg(quad[2]).arg(quad[5])));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);

    // The mode stays (the next area), a second click of the button ends it
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::Area);
    gui->getAreaButton()->click();
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::None);
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(120, 590), QPointF(220, 520)));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(1));
}

void AppToolRedactTest::textByMouse()
{
    // 437:11317: the mode Text, a drag over the line "Contact jane.novak@example.com today": a mark of its
    // quadrilaterals, the row "Text" with the text under it
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getTextButton()->click();
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::Text);
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Text · drag over the text to mark it · Esc leaves the tool"));
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(73, 705), QPointF(330, 703)));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return H::annotations(tab->getDocument()).size() == 1; }));

    const std::vector<RedactAnnotationEntry> entries = H::annotations(tab->getDocument());
    QCOMPARE(entries[0].subject, QStringLiteral("Redact text"));
    QVERIFY(entries[0].quadPoints.size() >= 8 && entries[0].quadPoints.size() % 8 == 0);
    // The quadrilateral of the line lies on its text (the baseline 700, Helvetica 12) and /Rect is their
    // bounding box (Table 166, B153)
    QVERIFY(entries[0].rect[1] <= 700.0 && entries[0].rect[3] >= 708.0 && entries[0].rect[3] < 720.0);
    QVERIFY(entries[0].rect[0] >= 71.0 && entries[0].rect[0] < 80.0);
    AppToolShellFixture::settle();

    pdf::PDFAppToolRedactMarkModel* model = gui->getMarkModel();
    QCOMPARE(model->index(1).data(Qt::DisplayRole).toString(), QStringLiteral("Text"));
    const QString description = model->index(1).data(pdf::PDFComponentRoles::SubtitleRole).toString();
    QVERIFY2(description.startsWith(QStringLiteral("“Contact jane.novak@example.com")), qPrintable(description));
    QVERIFY(!gui->getNoTextCallout()->isVisible());
}

void AppToolRedactTest::textOnPageWithoutText()
{
    // 2.6: the mode Text on a scanned page (page 3 has a picture only) - the callout, no mark
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    tab->getView()->goToPage(2);
    AppToolShellFixture::settle();

    gui->getTextButton()->click();
    QVERIFY(pdftest::AppDocumentFixture::dragPage(tab->getView(), 2, QPointF(150, 250), QPointF(250, 250)));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getNoTextCallout()->isVisible(); }));
    QCOMPARE(gui->getNoTextCallout()->getTitleLabel()->text(), QStringLiteral("This page has no text. Mark the area instead."));
    QVERIFY(H::annotations(tab->getDocument()).empty());

    // The mode Area: the callout goes
    gui->getAreaButton()->click();
    QVERIFY(!gui->getNoTextCallout()->isVisible());
}

void AppToolRedactTest::pagesDialog()
{
    // 437:11653: Redact whole pages - the pages chosen by the Page selection, each one a mark of its crop
    // box (R6-30), all of them one Undo step
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getPagesButton()->click();
    QPointer<pdf::PDFMessageBox> dialog = gui->getPagesDialog();
    QVERIFY(dialog);
    QVERIFY(AppToolShellFixture::waitFor([dialog]() { return dialog && dialog->isVisible(); }));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Redact whole pages")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Everything on the selected pages is removed. The fill color from the toolbar is used.")));
    QCOMPARE(gui->getPagesSelection()->getLabel(), QStringLiteral("Pages to redact"));
    QCOMPARE(H::buttonTexts(dialog), QStringList({ QStringLiteral("Cancel"), QStringLiteral("Mark 1 Page") }));

    gui->getPagesSelection()->setMode(pdf::PDFPageSelection::Mode::Range);
    gui->getPagesSelection()->setRange(QStringLiteral("1, 3"));
    QCOMPARE(gui->getPagesMarkButton()->text(), QStringLiteral("Mark 2 Pages"));
    QVERIFY(gui->getPagesMarkButton()->isEnabled());

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QTest::mouseClick(gui->getPagesMarkButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!dialog || !dialog->isVisible());

    const std::vector<RedactAnnotationEntry> entries = H::annotations(tab->getDocument());
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries[0].pageIndex, 0);
    QCOMPARE(entries[0].quadPoints, std::vector<pdf::PDFReal>({ 36, 756, 576, 756, 36, 36, 576, 36 }));
    QCOMPARE(entries[0].subject, QStringLiteral("Redact page"));
    QCOMPARE(entries[1].pageIndex, 2);
    QCOMPARE(entries[1].quadPoints, std::vector<pdf::PDFReal>({ 0, 792, 612, 792, 0, 0, 612, 0 }));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);
    QCOMPARE(gui->getMarkModel()->index(1).data(Qt::DisplayRole).toString(), QStringLiteral("Whole page"));

    // A page marked whole already is not marked twice
    QVERIFY(gui->markPages({ 0, 1 }));
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(3));
    QVERIFY(!gui->markPages({ 0 }));
}

void AppToolRedactTest::searchMarksSelected()
{
    // 437:12113 / 437:12452: the search of "secret" finds it on pages 1 and 2; one result deselected,
    // Mark 1 Selected Match makes one mark of its quadrilaterals in one Undo step
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getSearchButton()->click();
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::Search);
    QVERIFY(gui->getSearchContent()->isVisible());
    QVERIFY(!gui->getMarksContent()->isVisible());
    pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
    QCOMPARE(part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::IgnoreSoftHyphens)->text(), QStringLiteral("Ignore soft hyphens"));
    QVERIFY(part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::IgnoreSoftHyphens)->isChecked());
    QVERIFY(!part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::IgnoreSoftHyphens)->isEnabled());
    QCOMPARE(part->getBackButton()->text(), QStringLiteral("Back to Marked Areas"));

    QVERIFY(H::search(gui, QStringLiteral("secret")));
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Finished);
    QCOMPARE(part->getMatches().size(), size_t(2));
    QCOMPARE(part->getResultsLabel()->text(), QStringLiteral("2 matches on 2 pages"));
    QCOMPARE(part->getMarkButton()->text(), QStringLiteral("Mark 2 Selected Matches"));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("2 matches of “secret” · 2 selected"));

    // The second result deselected (a click of its row)
    pdf::PDFListView* view = part->getResultsView();
    view->selectionModel()->select(view->model()->index(1, 0), QItemSelectionModel::Deselect);
    QCOMPARE(part->getSelectedCount(), 1);
    QCOMPARE(part->getMarkButton()->text(), QStringLiteral("Mark 1 Selected Match"));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("2 matches of “secret” · 1 selected"));

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QTest::mouseClick(part->getMarkButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    const std::vector<RedactAnnotationEntry> entries = H::annotations(tab->getDocument());
    QCOMPARE(entries.size(), size_t(1));
    QCOMPARE(entries[0].pageIndex, 0);
    QCOMPARE(entries[0].subject, QStringLiteral("Redact text"));
    // "Secret plan" at the baseline 560: the quadrilaterals of the word lie on it
    QVERIFY(entries[0].rect[1] <= 560.0 && entries[0].rect[3] >= 566.0 && entries[0].rect[3] < 580.0);
    QCOMPARE(entries[0].quadPoints.size(), size_t(8));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);

    // 437:12452: "1 Match Marked" Disabled, the status counts the areas
    QCOMPARE(part->getMarkButton()->text(), QStringLiteral("1 Match Marked"));
    QVERIFY(!part->getMarkButton()->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("1 match marked · 1 area in total"));

    // Select all, then all of them: two marks more in one step
    part->getSelectAllLink()->click();
    QCOMPARE(part->getSelectedCount(), 2);
    QTest::mouseClick(part->getMarkButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(3));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 2);

    // Back to Marked Areas
    QTest::mouseClick(part->getBackButton(), Qt::LeftButton);
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::None);
    QVERIFY(gui->getMarksContent()->isVisible());
}

void AppToolRedactTest::searchResultsOfOldPagesDropped()
{
    // Review RS6APP: the results are places on the pages of the version they were found in. The own
    // marks keep them (only annotations change); Undo of the marks ends "N Matches Marked"; a change of
    // the pages by somebody else (an assistant) drops them - Mark would mark other content
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->setMode(PDFAppToolGUIRedact::Mode::Search);
    pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
    QVERIFY(H::search(gui, QStringLiteral("secret")));
    QCOMPARE(part->getMatches().size(), size_t(2));

    QTest::mouseClick(part->getMarkButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(2));
    QCOMPARE(part->getMatches().size(), size_t(2));
    QCOMPARE(part->getMarkButton()->text(), QStringLiteral("2 Matches Marked"));

    // Undo of the marks: the matches can be marked again
    shell->getUndoTarget()->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(tab->getDocument()).empty());
    QCOMPARE(part->getMatches().size(), size_t(2));
    QCOMPARE(part->getMarkButton()->text(), QStringLiteral("Mark 2 Selected Matches"));
    QVERIFY(part->getMarkButton()->isEnabled());

    // The assistant changes the pages: no result of the old pages stays, nothing can be marked by them
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset),
                             QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();
    QVERIFY(part->getMatches().empty());
    QVERIFY(part->getSelectedMatches().empty());
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Idle);
    QVERIFY(!part->getMarkButton()->isVisible() || !part->getMarkButton()->isEnabled());
    QCOMPARE(part->getQuery(), QStringLiteral("secret"));

    // A new search finds them on the new version
    QVERIFY(H::search(gui, QStringLiteral("secret")));
    QCOMPARE(part->getMatches().size(), size_t(2));
}

void AppToolRedactTest::searchInvalidExpression()
{
    // 2.6: a wrong regular expression is an error of the field (as 435:7693); nothing is searched
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    gui->setMode(PDFAppToolGUIRedact::Mode::Search);
    pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
    part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::RegularExpression)->click();
    QVERIFY(part->isOptionChecked(pdf::PDFAppToolRedactSearchPart::Option::RegularExpression));

    QVERIFY(H::search(gui, QStringLiteral("jane(")));
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Failed);
    QVERIFY(part->getErrorLabel()->isVisible());
    QVERIFY2(part->getErrorLabel()->text().startsWith(QStringLiteral("The regular expression is not valid")), qPrintable(part->getErrorLabel()->text()));
    QVERIFY(part->getField()->isInvalid());
    QVERIFY(part->getMatches().empty());
    QVERIFY(!part->getMarkButton()->isVisible());

    // A valid expression of 437:12113 finds the address
    QVERIFY(H::search(gui, QStringLiteral("jane\\.novak@\\S+")));
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Finished);
    QVERIFY(!part->getErrorLabel()->isVisible());
    QVERIFY(!part->getField()->isInvalid());
    QCOMPARE(part->getMatches().size(), size_t(1));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("1 match of “jane\\.novak@\\S+” · 1 selected"));
}

void AppToolRedactTest::searchAcrossLineBreaks()
{
    // K6.21: "grew by 18" is split over two lines of one paragraph - found with Across line breaks only
    // (the rule of the core, PDFDocumentToolRedact::findMatches, tested in DocumentToolRedactTest)

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    gui->setMode(PDFAppToolGUIRedact::Mode::Search);
    pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
    QVERIFY(!part->isOptionChecked(pdf::PDFAppToolRedactSearchPart::Option::AcrossLineBreaks));

    QVERIFY(H::search(gui, QStringLiteral("grew by 18")));
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Finished);
    QCOMPARE(part->getMatches().size(), size_t(0));
    QCOMPARE(part->getResultsLabel()->text(), QStringLiteral("No matches"));

    // The chip searches again with the option
    part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::AcrossLineBreaks)->click();
    QVERIFY(AppToolShellFixture::waitFor([part]() { return part->getState() == pdf::PDFAppToolRedactSearchPart::State::Finished; }));
    QCOMPARE(part->getMatches().size(), size_t(1));
    // The match covers two lines: a quadrilateral of each line at least
    const QPolygonF& quadrilaterals = part->getMatches().front().quadrilaterals;
    QVERIFY(quadrilaterals.size() >= 8 && quadrilaterals.size() % 4 == 0);
    QVERIFY(quadrilaterals.boundingRect().top() < 630.0 && quadrilaterals.boundingRect().bottom() > 645.0);
    // A phrase on one line is found without the option too
    part->getOptionButton(pdf::PDFAppToolRedactSearchPart::Option::AcrossLineBreaks)->click();
    QVERIFY(H::search(gui, QStringLiteral("percent this")));
    QCOMPARE(part->getMatches().size(), size_t(1));
}

void AppToolRedactTest::searchCanary()
{
    // R6-19: a search term is no value of the model, of its history or of the database (the last values,
    // Recent tools); the run gets no search term; the close of the tool empties the field
    const QString canary = QStringLiteral("CanaryRedact7Q4");
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    gui->setMode(PDFAppToolGUIRedact::Mode::Search);
    QVERIFY(H::search(gui, canary));
    QCOMPARE(gui->getSearchPart()->getQuery(), canary);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));

    pdf::PDFAppToolParameterModel* model = shell->getModel();
    for (auto it = model->getValues().cbegin(); it != model->getValues().cend(); ++it)
    {
        QVERIFY2(!it.value().toString().contains(canary), qPrintable(it.key()));
    }
    QVERIFY(model->getValue(QString::fromLatin1(Tool::PARAMETER_SEARCH)).toString().isEmpty());
    for (const char* name : { Tool::PARAMETER_SEARCH, Tool::PARAMETER_AREAS })
    {
        for (const QVariant& value : model->getHistoryValues(QString::fromLatin1(name)))
        {
            QVERIFY(!value.toString().contains(canary));
        }
    }
    const QVariantMap runParameters = gui->getRunParameters();
    for (auto it = runParameters.cbegin(); it != runParameters.cend(); ++it)
    {
        QVERIFY2(!it.value().toString().contains(canary), qPrintable(it.key()));
    }

    // The run (the copy is not needed): the runner holds the parameters of the run; the last values
    // are stored as at the end of a run
    QPointer<pdf::PDFAppToolRedactSearchPart> part = gui->getSearchPart();
    QPointer<pdf::PDFLineEdit> field = part ? part->getField() : nullptr;
    gui->setMode(PDFAppToolGUIRedact::Mode::None);
    QPointer<PDFDocumentToolShell> shellGuard = shell;
    QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shellGuard]() { return !shellGuard || shellGuard->getState() != PDFDocumentToolShell::State::Running; }));
    AppToolShellFixture::settle();
    QVERIFY2(shellGuard, "the tool stays open after its run");
    if (pdf::PDFDocumentToolRunner* runner = shellGuard->getRunner())
    {
        const QVariantMap runnerParameters = runner->getRunParameters();
        QVERIFY(runnerParameters.value(QString::fromLatin1(Tool::PARAMETER_USE_MARKS)).toBool());
        for (auto it = runnerParameters.cbegin(); it != runnerParameters.cend(); ++it)
        {
            QVERIFY2(!it.value().toString().contains(canary), qPrintable(it.key()));
        }
    }
    shellGuard->getModel()->storeLastValues();
    if (QPointer<pdf::PDFAppToolResultDialog> dialog = shellGuard->getResultDialog())
    {
        // Discard of the result asks about the marks (448:17473); Keep Marks and Close ends the tool
        dialog->reject();
        AppToolShellFixture::settle();
        if (shellGuard && H::gui(shellGuard) && H::gui(shellGuard)->getClosePrompt())
        {
            QTest::mouseClick(H::gui(shellGuard)->getClosePrompt()->getActionButtons()[1], Qt::LeftButton);
            AppToolShellFixture::settle();
        }
    }

    // The whole database exported: no canary (UTF-8, UTF-16)
    const QString exported = fixture.path(QStringLiteral("canary-backup.pdf4qtbackup"));
    QVERIFY(!fixture.window->getContext()->getDatabase()->exportBackup(exported).isError());
    QFile backup(exported);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    const QByteArray backupBytes = backup.readAll();
    QVERIFY(backupBytes.size() > 0);
    QVERIFY(!H::contains(backupBytes, canary));

    // The close of the tool forgets the term: the part and its field are gone or empty
    if (shellGuard)
    {
        shellGuard->cancel();
        AppToolShellFixture::settle();
    }
    QVERIFY(!part || part->getQuery().isEmpty());
    QVERIFY(!field || field->text().isEmpty());
}

void AppToolRedactTest::searchClearForgets()
{
    // R6-19: clear() (the close of the tool) forgets the term, the results and the history of the field
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    gui->setMode(PDFAppToolGUIRedact::Mode::Search);
    pdf::PDFAppToolRedactSearchPart* part = gui->getSearchPart();
    QVERIFY(H::search(gui, QStringLiteral("secret")));
    QCOMPARE(part->getMatches().size(), size_t(2));
    QVERIFY(part->getField()->isUndoAvailable());

    part->clear();
    QVERIFY(part->getQuery().isEmpty());
    QVERIFY(part->getMatches().empty());
    QVERIFY(part->getField()->text().isEmpty());
    QVERIFY(!part->getField()->isUndoAvailable());
    QCOMPARE(part->getResultsView()->model()->rowCount(), 0);
    QCOMPARE(part->getState(), pdf::PDFAppToolRedactSearchPart::State::Idle);
}

void AppToolRedactTest::removeRow()
{
    // The × of a row (437:10535) removes its mark: one Undo step; Delete on the current row too
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    QVERIFY(gui->addAreaMark(0, QRectF(300, 600, 60, 40)));
    AppToolShellFixture::settle();
    const std::vector<RedactAnnotationEntry> before = H::annotations(tab->getDocument());
    QCOMPARE(before.size(), size_t(2));

    pdf::PDFListView* view = gui->getMarksView();
    const QModelIndex first = gui->getMarkModel()->index(1);
    QStyleOptionViewItem option;
    option.initFrom(view->viewport());
    option.rect = view->visualRect(first);
    pdf::PDFRowDelegate* delegate = qobject_cast<pdf::PDFRowDelegate*>(view->itemDelegate());
    QVERIFY(delegate);
    QRectF removeRect;
    for (const pdf::PDFRowDelegate::Action& action : delegate->getActions(option, first))
    {
        if (action.id == pdf::PDFAppToolRedactMarkDelegate::RemoveAction)
        {
            removeRect = action.rect;
        }
    }
    QVERIFY(removeRect.isValid());
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, removeRect.center().toPoint());
    AppToolShellFixture::settle();
    std::vector<RedactAnnotationEntry> after = H::annotations(tab->getDocument());
    QCOMPARE(after.size(), size_t(1));
    QCOMPARE(after[0].reference, before[1].reference);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);

    // Delete on the current row
    view->setFocus();
    view->setCurrentIndex(gui->getMarkModel()->index(1));
    QCOMPARE(gui->getSelectedMark(), before[1].reference);
    QTest::keyClick(view, Qt::Key_Delete);
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(tab->getDocument()).empty());
    QVERIFY(gui->getEmptyCard()->isVisible());
}

void AppToolRedactTest::clearAll()
{
    // Clear All asks first (no frame: "Remove all 3 marks?"), then removes the marks in one Undo step
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    QVERIFY(gui->addAreaMark(0, QRectF(300, 600, 50, 50)));
    QVERIFY(gui->markPages({ 2 }));
    AppToolShellFixture::settle();

    gui->getClearButton()->click();
    QPointer<pdf::PDFMessageBox> dialog = gui->getClearDialog();
    QVERIFY(dialog);
    QCOMPARE(H::buttonTexts(dialog), QStringList({ QStringLiteral("Cancel"), QStringLiteral("Remove All Marks") }));

    // Cancel keeps them
    QTest::mouseClick(dialog->getActionButtons().front(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(3));

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    dialog = gui->clearAll();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getActionButtons().back(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(tab->getDocument()).empty());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);
    QVERIFY(!gui->getClearButton()->isEnabled());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolRedactTest::closePromptRemove()
{
    // 448:17473: "Keep the 3 marks in the document?" Back to Redacting | Keep Marks and Close (Primary) |
    // Remove Marks; Remove Marks takes them away in one Undo step and closes the tool
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!gui->askClose());
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    QVERIFY(gui->addAreaMark(1, QRectF(100, 600, 50, 50)));
    QVERIFY(gui->markPages({ 2 }));

    // Cancel / Esc of the shell asks the question of the tool (PDFDocumentToolGUI::askClose); the tool stays
    shell->cancel();
    QPointer<pdf::PDFMessageBox> prompt = gui->getClosePrompt();
    QVERIFY(prompt);
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(AppToolShellFixture::waitFor([prompt]() { return prompt && prompt->isVisible(); }));
    QCOMPARE(H::buttonTexts(prompt), QStringList({ QStringLiteral("Back to Redacting"), QStringLiteral("Keep Marks and Close"), QStringLiteral("Remove Marks") }));

    // The card of 448:17473 is 600 wide: Back to Redacting (leading) and the trailing pair keep a gap
    // (593 -> 638 in the frame), the buttons do not touch
    QCOMPARE(prompt->getCardWidth(), 600);
    const QRect back(prompt->getActionButtons()[0]->mapTo(prompt, QPoint(0, 0)), prompt->getActionButtons()[0]->size());
    const QRect keep(prompt->getActionButtons()[1]->mapTo(prompt, QPoint(0, 0)), prompt->getActionButtons()[1]->size());
    QVERIFY2(keep.left() - back.right() > 12, qPrintable(QStringLiteral("%1 %2").arg(back.right()).arg(keep.left())));
    QVERIFY(prompt->getActionButtons()[1]->isDefault());
    QVERIFY(!prompt->getActionButtons()[2]->isDefault());
    QVERIFY(H::hasLabel(prompt, QStringLiteral("Keep the 3 marks in the document?")));
    QVERIFY(H::hasLabel(prompt, QStringLiteral("No redacted copy was created yet. Marks are redaction annotations – kept marks stay in the document so you can finish later. Nothing is removed from the content either way.")));

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QTest::mouseClick(prompt->getActionButtons()[2], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(tab->getDocument()).empty());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);
}

void AppToolRedactTest::closePromptKeepAndBack()
{
    // Back to Redacting returns to the tool; Keep Marks and Close keeps the annotations and closes it
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    QPointer<PDFDocumentToolShell> shellGuard = shell;

    shell->cancel();
    QPointer<pdf::PDFMessageBox> prompt = gui->getClosePrompt();
    QVERIFY(prompt);

    // Esc again while the question is open: still one question, the tool stays (the re-entry guard)
    shell->cancel();
    QCOMPARE(gui->getClosePrompt(), prompt.data());
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QTest::mouseClick(prompt->getActionButtons()[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!prompt || !prompt->isVisible());
    QVERIFY(shellGuard && shellGuard->getState() == PDFDocumentToolShell::State::Editing);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(1));

    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    shell->cancel();
    prompt = gui->getClosePrompt();
    QVERIFY(prompt);
    QTest::mouseClick(prompt->getActionButtons()[1], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(1));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore);
    QVERIFY(tab->getDocument()->isModified());
}

void AppToolRedactTest::closePromptAfterDiscard()
{
    // 448:17473 "No redacted copy was created yet": Discard result of the result dialog (437:13887) and
    // Cancel of the toolbar after the run do not pass the question by - the tool waits in Editing
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));

    const auto runToDialog = [&shell]()
    {
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        return AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getResultDialog(); }) && shell;
    };
    QVERIFY(runToDialog());
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    QTest::mouseClick(dialog->getDiscardButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QPointer<pdf::PDFMessageBox> prompt = gui->getClosePrompt();
    QVERIFY(prompt && prompt->isVisible());
    QVERIFY(H::hasLabel(prompt, QStringLiteral("Keep the 1 mark in the document?")));

    // Back to Redacting: the tool with its mark, the Primary button works again
    QTest::mouseClick(prompt->getActionButtons()[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // Cancel of the toolbar while the result dialog is open: the same question, Keep Marks and Close ends
    QVERIFY(runToDialog());
    QTest::mouseClick(shell->getToolbar()->getCancelButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);
    prompt = gui->getClosePrompt();
    QVERIFY(prompt && prompt->isVisible());
    QTest::mouseClick(prompt->getActionButtons()[1], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(1));
}

void AppToolRedactTest::undoMarksInTool()
{
    // P5 open point 3: Undo of the tool takes back the marks (changes of the document) and the choices
    // of the toolbar in one order - the last thing done in the tool first; Redo again; a new step ends
    // Redo; after the tool the Undo of the document goes on with the marks left
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    using Fill = PDFAppToolGUIRedact::Fill;
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppToolUndoTarget* target = tab->getView()->getToolUndoTarget();
    QVERIFY(target);
    QCOMPARE(target, shell->getUndoTarget());
    QVERIFY(target != shell->getModel());
    QVERIFY(!target->canUndo() && !target->canRedo());
    const int undoBase = pdftest::AppDocumentFixture::undoCount(document);

    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    gui->setFill(Fill::White);
    QVERIFY(gui->addAreaMark(1, QRectF(100, 600, 50, 50)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMarks().size(), size_t(2));
    QCOMPARE(gui->getFill(), Fill::White);
    QVERIFY(target->canUndo());
    QCOMPARE(target->getUndoText(), QStringLiteral("Mark an area for redaction"));
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());

    // Undo: the second mark, the fill, the first mark
    target->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMarks().size(), size_t(1));
    QCOMPARE(gui->getFill(), Fill::White);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBase + 1);
    target->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMarks().size(), size_t(1));
    QCOMPARE(gui->getFill(), Fill::Black);
    target->undo();
    AppToolShellFixture::settle();
    QVERIFY(gui->getMarks().empty());
    QVERIFY(H::annotations(document).empty());
    QVERIFY(!target->canUndo());
    QVERIFY(!document->isModified());

    // Redo in the same order
    QVERIFY(target->canRedo());
    QCOMPARE(target->getRedoText(), QStringLiteral("Mark an area for redaction"));
    target->redo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMarks().size(), size_t(1));
    QCOMPARE(gui->getFill(), Fill::Black);
    target->redo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getFill(), Fill::White);
    target->redo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMarks().size(), size_t(2));
    QVERIFY(!target->canRedo());

    // A new step of the user ends Redo
    target->undo();
    AppToolShellFixture::settle();
    QVERIFY(target->canRedo());
    gui->setFill(Fill::Black);
    QVERIFY(!target->canRedo());
    QCOMPARE(gui->getMarks().size(), size_t(1));

    // After the tool: the Undo of the document takes back the mark left by the tool
    shell->cancel();
    QPointer<pdf::PDFMessageBox> prompt = gui->getClosePrompt();
    QVERIFY(prompt);
    QTest::mouseClick(prompt->getActionButtons()[1], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBase + 1);
    QVERIFY(document->getUndoStack()->undo());
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(document).empty());
    QVERIFY(!document->isModified());
}

void AppToolRedactTest::undoMarksNotForeignChange()
{
    // Undo of the tool never takes back a change of somebody else (an assistant over MCP): a change
    // after the last mark of the tool ends the marks of Undo of the tool, the parameters stay
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppToolUndoTarget* target = shell->getUndoTarget();
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    AppToolShellFixture::settle();
    const std::vector<RedactAnnotationEntry> marks = H::annotations(document);
    QCOMPARE(marks.size(), size_t(1));
    QVERIFY(target->canUndo());

    // The assistant removes the mark (PDFAppChangeSource::Mcp - allowed while the tool is open)
    const std::optional<pdf::PDFModifiedDocument> removed = pdf::PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { marks.front().reference });
    QVERIFY(removed);
    QVERIFY(document->commit(*removed, QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();
    QVERIFY(gui->getMarks().empty());
    QVERIFY(!target->canUndo());
    target->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(document).empty());

    // A step of the parameters after it is taken back as usual
    gui->setFill(PDFAppToolGUIRedact::Fill::White);
    QVERIFY(target->canUndo());
    target->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getFill(), PDFAppToolGUIRedact::Fill::Black);
    QVERIFY(H::annotations(document).empty());
    QVERIFY(!target->canUndo());
}

void AppToolRedactTest::undoMarksWaitForBlockedDocument()
{
    // Review RS6APP: Undo of the tool takes back the last thing done in it. While the document takes no
    // change (an operation blocking the changes runs), its last mark waits - Undo does nothing, it never
    // takes back an older step of the parameters before it (and never loses the mark from its order)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppToolUndoTarget* target = shell->getUndoTarget();
    gui->setFill(PDFAppToolGUIRedact::Fill::White);
    QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 50, 50)));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(document).size(), size_t(1));

    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Saving"), true);
    QVERIFY(operation);
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(!target->canUndo());
    target->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getFill(), PDFAppToolGUIRedact::Fill::White);
    QCOMPARE(H::annotations(document).size(), size_t(1));

    // The operation ends: the mark first, then the fill
    operation->finish();
    AppToolShellFixture::settle();
    QVERIFY(!document->isBlockedByOperation());
    QVERIFY(target->canUndo());
    target->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(document).empty());
    QCOMPARE(gui->getFill(), PDFAppToolGUIRedact::Fill::White);
    target->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getFill(), PDFAppToolGUIRedact::Fill::Black);
}

void AppToolRedactTest::fillMenu()
{
    // 437:12794: Black, White, Custom color…, No fill - each choice one undo step of the parameters
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    using Fill = PDFAppToolGUIRedact::Fill;
    pdf::PDFAppToolParameterModel* model = shell->getModel();

    QCOMPARE(gui->getFillRow(Fill::Black)->getTitle(), QStringLiteral("Black"));
    QCOMPARE(gui->getFillRow(Fill::Black)->getDescription(), QStringLiteral("The classic redaction bar"));
    QCOMPARE(gui->getFillRow(Fill::White)->getDescription(), QStringLiteral("Blends with white paper"));
    QCOMPARE(gui->getFillRow(Fill::Custom)->getTitle(), QStringLiteral("Custom color…"));
    QCOMPARE(gui->getFillRow(Fill::Custom)->getDescription(), QStringLiteral("Pick any color"));
    QCOMPARE(gui->getFillRow(Fill::None)->getDescription(), QStringLiteral("Content is removed, the area stays empty"));
    QVERIFY(gui->getFillRow(Fill::Black)->isChecked());
    QVERIFY(!model->canUndo());

    gui->getFillRow(Fill::White)->click();
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_FILL_COLOR)).value<QColor>(), QColor(Qt::white));
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_NO_FILL)).toBool(), false);
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: White"));
    QVERIFY(gui->getFillRow(Fill::White)->isChecked());
    QCOMPARE(H::rows(gui->getSummaryRows())[1], QStringLiteral("Fill: White"));
    QVERIFY(model->canUndo());

    gui->getFillRow(Fill::None)->click();
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_NO_FILL)).toBool(), true);
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: No fill"));
    QCOMPARE(H::rows(gui->getSummaryRows())[1], QStringLiteral("Fill: No fill"));

    // Undo of the parameters (the toolbar Undo): White again (one step each)
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());
    model->undo();
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_NO_FILL)).toBool(), false);
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: White"));

    // Custom color…: the colors are shown, a swatch chooses the color (the canonical value of the swatch)
    gui->getFillRow(Fill::Custom)->click();
    pdf::PDFColorSwatchRow* colors = gui->getFillPanel()->getColorRow();
    QVERIFY(colors);
    QVERIFY(gui->getFillPanel()->isColorGroupVisible());
    int redIndex = -1;
    for (int i = 0; i < colors->getSwatchCount(); ++i)
    {
        if (colors->getEntry(i).token == pdf::PDFThemeColors::AnnotationRed)
        {
            redIndex = i;
        }
    }
    QVERIFY(redIndex >= 0);
    const QColor red = colors->getEntry(redIndex).canonicalColor;
    colors->getSwatch(redIndex)->click();
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_FILL_COLOR)).value<QColor>().rgb(), red.rgb());
    QCOMPARE(gui->getFill(), Fill::Custom);
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: Custom"));
    QCOMPARE(H::rows(gui->getSummaryRows())[1], QStringLiteral("Fill: Custom · %1").arg(red.name(QColor::HexRgb).toUpper()));

    // Black again
    gui->getFillRow(Fill::Black)->click();
    QCOMPARE(model->getValue(QString::fromLatin1(Tool::PARAMETER_FILL_COLOR)).value<QColor>(), QColor(Qt::black));
    QCOMPARE(gui->getFillButton()->text(), QStringLiteral("Fill: Black"));
}

void AppToolRedactTest::foreignMarksBounded()
{
    // 7.2 (RS6APP): a mark of another application with more /QuadPoints than a run accepts (1000
    // quadrilaterals) is listed by its rectangle - its points are neither copied nor drawn; 1000 are kept
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(H::write(fixture.path(QStringLiteral("Foreign.pdf")), H::documentWithForeignMarks()));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getMarks().size(), size_t(4));

    bool hasKept = false;
    bool hasBounded = false;
    for (const pdf::PDFAppToolRedactMark& mark : gui->getMarks())
    {
        if (mark.annotation == pdf::PDFObjectReference(12, 0))
        {
            hasKept = true;
            QCOMPARE(mark.quadrilaterals.size(), qsizetype(4000));
        }
        else if (mark.annotation == pdf::PDFObjectReference(13, 0))
        {
            hasBounded = true;
            QVERIFY(mark.quadrilaterals.isEmpty());
            QCOMPARE(mark.rectangle, QRectF(100, 400, 10, 10));
        }
    }
    QVERIFY(hasKept && hasBounded);
}

void AppToolRedactTest::lockedMarkStays()
{
    // RS6APP: a Locked mark (ISO 32000-2 Table 167, /F 128) cannot be removed - the × of its row says so
    // in the status bar instead of doing nothing; Remove Marks of the close question removes the others,
    // names the locked one and keeps the tool open, because a mark remains
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(H::write(fixture.path(QStringLiteral("Foreign.pdf")), H::documentWithForeignMarks()));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(4));

    QVERIFY(!gui->removeMark(pdf::PDFObjectReference(10, 0)));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(4));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("1 mark is locked and stays in the document."));

    // A free mark goes
    QVERIFY(gui->removeMark(pdf::PDFObjectReference(11, 0)));
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(tab->getDocument()).size(), size_t(3));

    // Remove Marks of the close question (448:17473)
    shell->cancel();
    QPointer<pdf::PDFMessageBox> prompt = gui->getClosePrompt();
    QVERIFY(prompt);
    QVERIFY(AppToolShellFixture::waitFor([prompt]() { return prompt && prompt->isVisible(); }));
    QTest::mouseClick(prompt->getActionButtons()[2], Qt::LeftButton);
    AppToolShellFixture::settle();
    const std::vector<RedactAnnotationEntry> remaining = H::annotations(tab->getDocument());
    QCOMPARE(remaining.size(), size_t(1));
    QCOMPARE(remaining[0].reference, pdf::PDFObjectReference(10, 0));
    QVERIFY(shell);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell.data());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getMarks().size(), size_t(1));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("1 mark is locked and stays in the document."));
}

void AppToolRedactTest::escapeEndsDrag()
{
    // RS6APP: Esc during a drag of the mode Area ends the drag - no rectangle stays on the page and the
    // release marks nothing; the tool stays (the next Esc leaves it)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->getAreaButton()->click();
    QCOMPARE(gui->getMode(), PDFAppToolGUIRedact::Mode::Area);
    pdf::PDFAppToolRedactOverlay* overlay = gui->getOverlay();
    QVERIFY(overlay);

    QWidget* canvas = AppToolShellFixture::canvasWidget(tab->getView());
    QVERIFY(canvas);
    bool ok = false;
    const QPoint start = AppToolShellFixture::pageToWidget(tab->getView(), 0, QPointF(120, 690), &ok);
    QVERIFY(ok);
    const QPoint end = AppToolShellFixture::pageToWidget(tab->getView(), 0, QPointF(220, 610), &ok);
    QVERIFY(ok);
    canvas->setFocus();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), canvas->mapToGlobal(QPointF(end)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QVERIFY(overlay->getDraggedArea().has_value());

    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!overlay->getDraggedArea().has_value());
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(!gui->getClosePrompt());

    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    AppToolShellFixture::settle();
    QVERIFY(H::annotations(tab->getDocument()).empty());
}

void AppToolRedactTest::fillSwatches()
{
    // 437:12794 (RS6FIGMA Low): Black and White are swatches of their color framed by border/default
    // (437:13131, 437:13132), Custom color… an empty framed swatch with "+" (1567:19612), No fill keeps
    // its icon tile "ban" (437:13160)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    using Fill = PDFAppToolGUIRedact::Fill;

    pdf::PDFTileRow* black = gui->getFillRow(Fill::Black);
    pdf::PDFTileRow* white = gui->getFillRow(Fill::White);
    pdf::PDFTileRow* custom = gui->getFillRow(Fill::Custom);
    pdf::PDFTileRow* none = gui->getFillRow(Fill::None);
    QVERIFY(black->isSwatchTile() && black->getSwatchColor() == pdf::PDFThemeColors::AnnotationBlack && black->getContent().iconName.isEmpty());
    QVERIFY(white->isSwatchTile() && white->getSwatchColor() == pdf::PDFThemeColors::AnnotationWhite && white->getContent().iconName.isEmpty());
    QVERIFY(custom->isSwatchTile() && !custom->getSwatchColor().has_value());
    QCOMPARE(custom->getContent().iconName, QStringLiteral("plus"));
    QVERIFY(!none->isSwatchTile());
    QCOMPARE(none->getContent().iconName, QStringLiteral("ban"));

    // The pixels: the middle of the 30 px tile (10 px from the leading edge) has the color of the swatch
    for (const auto& [row, expected] : { std::make_pair(black, QColor(Qt::black)), std::make_pair(white, QColor(Qt::white)) })
    {
        row->resize(282, 44);
        const QImage image = row->grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        const QColor center = image.pixelColor(QPoint(qRound(25 * ratio), qRound(22 * ratio)));
        QVERIFY2(H::isNear(center, expected), qPrintable(center.name()));
    }
}

void AppToolRedactTest::keepMenu()
{
    // 437:12794: Keep - the title, the metadata, the outline (R6-6: the text of the title is true)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);

    const std::vector<QPointer<pdf::PDFTileRow>>& rows = gui->getKeepMenu()->getPanel()->getRows();
    QCOMPARE(rows.size(), size_t(3));
    QCOMPARE(rows[0]->getTitle(), QStringLiteral("Document title"));
    QCOMPARE(rows[1]->getTitle(), QStringLiteral("Metadata"));
    QCOMPARE(rows[1]->getDescription(), QStringLiteral("Author, subject, keywords, dates"));
    QCOMPARE(rows[2]->getTitle(), QStringLiteral("Outline"));
    QCOMPARE(rows[2]->getDescription(), QStringLiteral("Bookmarks may reveal redacted headings"));

    rows[2]->click();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_KEEP_OUTLINE)).toBool(), true);
    QCOMPARE(H::rows(gui->getSummaryRows())[4], QStringLiteral("Outline: Kept"));
    rows[0]->click();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_KEEP_TITLE)).toBool(), false);
    QCOMPARE(H::rows(gui->getSummaryRows())[2], QStringLiteral("Title: Not copied"));
}

void AppToolRedactTest::previewPaintsFill()
{
    // 437:13219 (plan 4.5): Preview result fills the marked area by the fill on the canvas - black, No
    // fill = the white paper
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    // An area without text (the lines end before x 300)
    QVERIFY(gui->addAreaMark(0, QRectF(320, 600, 120, 80)));
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentView* view = tab->getView();
    const QPointF center(380, 640);

    QTest::mouseClick(H::segment(gui, 1), Qt::LeftButton);
    QVERIFY(gui->isPreview());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Preview of the redacted copy · the open document is not changed"));
    QVERIFY2(AppToolShellFixture::waitFor([view, center]() { return H::isNear(H::canvasPixel(view, 0, center), QColor(Qt::black)); }), qPrintable(H::canvasPixel(view, 0, center).name()));

    // No fill: the white paper
    gui->setFill(PDFAppToolGUIRedact::Fill::None);
    QVERIFY2(AppToolShellFixture::waitFor([view, center]() { return H::isNear(H::canvasPixel(view, 0, center), QColor(Qt::white)); }), qPrintable(H::canvasPixel(view, 0, center).name()));

    // A mode of the input returns to the marks
    gui->getAreaButton()->click();
    QVERIFY(!gui->isPreview());
    QCOMPARE(gui->getOverlay()->getModeControl()->getCurrentIndex(), 0);
}

void AppToolRedactTest::marksKeepContentVisible()
{
    // 437:10393 (the marks are framed, the content under them is seen, "not removed yet"): a mark has no
    // /IC, which Table 195 of ISO 32000-2 says leaves the region transparent (PDFRedactAnnotation::getFillColor)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addAreaMark(0, QRectF(320, 600, 120, 80)));
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY2(AppToolShellFixture::waitFor([view]() { const QColor color = H::canvasPixel(view, 0, QPointF(380, 640)); return color.isValid() && color.lightness() > 150; }),
             qPrintable(H::canvasPixel(view, 0, QPointF(380, 640)).name()));
}

void AppToolRedactTest::runParameters()
{
    // The run takes the marks of the document (useMarks) with the fill and Keep of the model; the search,
    // the areas and the pages of the core stay empty (the GUI makes marks of them)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    gui->setFill(PDFAppToolGUIRedact::Fill::White);

    const QVariantMap parameters = gui->getRunParameters();
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_USE_MARKS)).toBool(), true);
    QVERIFY(parameters.value(QString::fromLatin1(Tool::PARAMETER_SEARCH)).toString().isEmpty());
    QVERIFY(parameters.value(QString::fromLatin1(Tool::PARAMETER_AREAS)).toString().isEmpty());
    QString pagesError;
    const std::optional<pdf::PDFDocumentToolPageSelection> pages = pdf::PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(Tool::PARAMETER_PAGES)), &pagesError);
    QVERIFY(pages.has_value());
    QVERIFY(pages->isNone);
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_FILL_COLOR)).value<QColor>(), QColor(Qt::white));
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_NO_FILL)).toBool(), false);
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_KEEP_TITLE)).toBool(), true);
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_KEEP_METADATA)).toBool(), true);
    QCOMPARE(parameters.value(QString::fromLatin1(Tool::PARAMETER_KEEP_OUTLINE)).toBool(), false);

    // The texts of the FileOnly dialog (437:13887)
    pdf::PDFAppToolResultInput input;
    gui->fillResultInput(&input);
    QCOMPARE(input.title, QStringLiteral("The redacted copy is ready"));
    QCOMPARE(input.subtitle, QStringLiteral("The marked content is gone from the copy. The open document still contains it and keeps its marks."));
    QCOMPARE(input.fileConfirmText, QStringLiteral("Save Redacted Copy"));
    QCOMPARE(input.fileOpenText, QStringLiteral("Open the redacted copy in a new tab"));
    QCOMPARE(input.statusText, QStringLiteral("Redacted copy created – save it"));
}

void AppToolRedactTest::searchHitColors()
{
    // search-hit 437:12412 (review RS6FIGMA): a match of the search is accent/amber/bg-pressed at 50 % with
    // the border accent/amber/border-strong on the page - never the cyan of the Find of the view
    // (search/match #a5f3fc)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    gui->getSearchButton()->click();
    QVERIFY(H::search(gui, QStringLiteral("secret")));
    QCOMPARE(gui->getSearchPart()->getMatches().size(), size_t(2));
    AppToolShellFixture::settle();

    // "Secret plan" of page 1 (Helvetica 12 at 72 560): the pixels of the word, its glyphs and the paper
    // between them under the hit; the light amber over white paper is (254.5, 241.5, 225.5)
    const pdf::PDFThemeColors light;
    const QColor amber = light.getColor(pdf::PDFThemeColors::AccentAmberBgPressed);
    const QColor expected((255 + amber.red()) / 2, (255 + amber.green()) / 2, (255 + amber.blue()) / 2);
    const QColor cyan = light.getColor(pdf::PDFThemeColors::SearchMatch);
    int amberCount = 0;
    int cyanCount = 0;
    for (qreal x = 74.0; x <= 104.0; x += 1.0)
    {
        for (qreal y = 561.0; y <= 569.0; y += 1.0)
        {
            const QColor pixel = H::canvasPixel(tab->getView(), 0, QPointF(x, y));
            amberCount += H::isNear(pixel, expected, 6) ? 1 : 0;
            cyanCount += H::isNear(pixel, cyan, 12) ? 1 : 0;
        }
    }
    QVERIFY2(amberCount > 0, qPrintable(QString::number(amberCount)));
    QCOMPARE(cyanCount, 0);
}

void AppToolRedactTest::summaryRowGap()
{
    // section-Result 437:10427 (review RS6FIGMA): the Key value rows of 28 are 36 apart (8 between them)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIRedact* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSummaryRows()->getRowGap(), 8);
}

void AppToolRedactTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS6FIGMA): the states of Redact Content under the ids of
    // their frames (437:10393 ... 448:17473) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        QStringList leaks;
        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            AppToolShellFixture::settle();
            if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto shotWidget = [&suffix, &leaks](QWidget* widget, const QString& frame)
        {
            AppToolShellFixture::settle();
            AppToolShellFixture::screenshotDialog(widget, frame + suffix);
            if (widget && AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(widget, 1.0)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto openMenu = [&shot, &shotWidget](QToolButton* button, const QString& frame)
        {
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            AppToolShellFixture::settle();
            shot(frame);
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                shotWidget(popup, frame + QStringLiteral("-popup"));
                popup->close();
            }
            AppToolShellFixture::settle();
        };

        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
        PDFAppToolGUIRedact* gui = H::gui(shell);
        QVERIFY(gui);
        shot(QStringLiteral("437-10726"));

        gui->getAreaButton()->click();
        shot(QStringLiteral("437-10982"));
        gui->getTextButton()->click();
        shot(QStringLiteral("437-11317"));
        gui->setMode(PDFAppToolGUIRedact::Mode::None);

        QVERIFY(gui->addAreaMark(0, QRectF(100, 600, 52, 80)));
        QVERIFY(gui->addAreaMark(0, QRectF(300, 600, 120, 40)));
        QVERIFY(gui->addAreaMark(1, QRectF(100, 600, 50, 50)));
        QVERIFY(gui->markPages({ 2 }));
        shot(QStringLiteral("437-10393"));

        openMenu(gui->getFillButton(), QStringLiteral("437-12794-fill"));
        openMenu(gui->getKeepButton(), QStringLiteral("437-12794-keep"));

        gui->getPagesButton()->click();
        QVERIFY(gui->getPagesDialog());
        shot(QStringLiteral("437-11653"));
        shotWidget(gui->getPagesDialog(), QStringLiteral("437-11653-dialog"));
        gui->getPagesDialog()->reject();
        AppToolShellFixture::settle();

        gui->getSearchButton()->click();
        QVERIFY(H::search(gui, QStringLiteral("secret")));
        shot(QStringLiteral("437-12113"));
        gui->setMode(PDFAppToolGUIRedact::Mode::None);

        gui->setPreview(true);
        shot(QStringLiteral("437-13219"));
        gui->setPreview(false);

        QVERIFY(gui->openClosePrompt());
        shot(QStringLiteral("448-17473"));
        shotWidget(gui->getClosePrompt(), QStringLiteral("448-17473-dialog"));
        QTest::mouseClick(gui->getClosePrompt()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();

        QVERIFY(gui->clearAll());
        shotWidget(gui->getClearDialog(), QStringLiteral("redact-clear-dialog"));
        gui->getClearDialog()->reject();
        AppToolShellFixture::settle();

        // 437:13887: a real run, the dialog of a file
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
        shot(QStringLiteral("437-13887"));
        shotWidget(shell->getResultDialog(), QStringLiteral("437-13887-dialog"));
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolRedactTest)

#include "tst_apptoolredacttest.moc"
