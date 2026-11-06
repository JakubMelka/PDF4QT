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

// The tools Remove Hidden Data and Remove External Links in the application (package 2B of the
// first batch of phase 2; frames 438:12588 … 448:15959 and 438:14751 … 438:16073). The expected
// values are the texts and numbers of the frames; the documents are written by hand.

#include "apptoolshellfixture.h"
#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmcpcontroller.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentlinkoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_protect.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfmcpcall.h"
#include "pdfstreamfilters.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include <map>

using pdftest::AppToolShellFixture;
using pdf::PDFAppToolGUISanitize;
using pdf::PDFAppToolGUIRemoveLinks;
using pdf::PDFDocumentToolShell;

/// PDF files written by hand
class ProtectAppHelper
{
public:
    ProtectAppHelper() = delete;

    static inline const QString SANITIZE_ID = QStringLiteral("protect.sanitize");
    static inline const QString LINKS_ID = QStringLiteral("protect.remove-links");

    static QByteArray build(const std::map<int, QByteArray>& objects, int info = 0)
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
        data += "trailer\n<< /Size " + QByteArray::number(maximum + 1) + " /Root 1 0 R";
        if (info)
        {
            data += " /Info " + QByteArray::number(info) + " 0 R";
        }
        data += " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    static QByteArray stream(const QByteArray& entries, const QByteArray& data)
    {
        const QByteArray content = pdf::PDFFlateDecodeFilter::compress(data);
        return "<< " + entries + " /Length " + QByteArray::number(content.size()) + " /Filter /FlateDecode >>\nstream\n" + content + "\nendstream";
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

    /// Document info, metadata, a comment, an outline and a thumbnail; the invisible text, if asked
    static QByteArray hiddenData(bool isInvisibleText, bool isSigned)
    {
        std::map<int, QByteArray> objects;
        objects[1] = "<< /Type /Catalog /Pages 2 0 R /Metadata 7 0 R /Outlines 8 0 R" + QByteArray(isSigned ? " /AcroForm << /Fields [20 0 R] /SigFlags 1 >>" : "") + " >>";
        objects[2] = "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>";
        objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 6 0 R >> >> /Annots [10 0 R" + QByteArray(isSigned ? " 20 0 R" : "") + "] /Thumb 12 0 R >>";
        objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>";
        objects[5] = stream("", isInvisibleText ? "BT /F1 12 Tf 72 700 Td (VisibleText) Tj ET BT 3 Tr /F1 12 Tf 72 650 Td (MarkerInvisible) Tj ET" : "BT /F1 12 Tf 72 700 Td (VisibleText) Tj ET");
        objects[6] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
        objects[7] = stream("/Type /Metadata /Subtype /XML", "<x:xmpmeta>MarkerXmp</x:xmpmeta>");
        objects[8] = "<< /Type /Outlines /First 9 0 R /Last 9 0 R /Count 1 >>";
        objects[9] = "<< /Title (MarkerOutline) /Parent 8 0 R /Dest [3 0 R /Fit] >>";
        objects[10] = "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (MarkerComment) >>";
        objects[11] = "<< /Title (MarkerTitle) /Author (MarkerAuthor) >>";
        objects[12] = stream("/Width 2 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8", "MarkerThumb");
        if (isSigned)
        {
            objects[20] = "<< /FT /Sig /T (Signature1) /V 21 0 R /Type /Annot /Subtype /Widget /Rect [0 0 0 0] /P 3 0 R >>";
            objects[21] = "<< /Type /Sig /Name (MarkerSigner) /Contents <00> >>";
        }
        return build(objects, 11);
    }

    static QByteArray clean()
    {
        std::map<int, QByteArray> objects;
        objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
        objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
        objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>";
        return build(objects);
    }

    /// Links: page 1 - a web link and an e-mail link, page 2 - a web link with its contents, an
    /// ftp link and an internal link; \p extraLinks more web links on further pages
    static QByteArray links(int extraLinks = 0)
    {
        std::map<int, QByteArray> objects;
        const int pageCount = 2 + extraLinks;
        QByteArray kids;
        for (int i = 0; i < pageCount; ++i)
        {
            kids += QByteArray::number(100 + i) + " 0 R ";
        }
        objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
        objects[2] = "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageCount) + " >>";
        objects[100] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [10 0 R 11 0 R] >>";
        objects[101] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [12 0 R 13 0 R 14 0 R] >>";
        objects[10] = "<< /Type /Annot /Subtype /Link /Rect [54 600 234 615] /A << /S /URI /URI (https://www.acme-corp.example/) >> >>";
        objects[11] = "<< /Type /Annot /Subtype /Link /Rect [54 500 234 515] /A << /S /URI /URI (mailto:jane.novak@example.com) >> >>";
        objects[12] = "<< /Type /Annot /Subtype /Link /Rect [54 600 234 615] /Contents (Market study 2026) /A << /S /URI /URI (https://research.example.org/study) >> >>";
        objects[13] = "<< /Type /Annot /Subtype /Link /Rect [54 500 234 515] /A << /S /URI /URI (ftp://files.example.org/data) >> >>";
        objects[14] = "<< /Type /Annot /Subtype /Link /Rect [54 400 234 415] /Dest [100 0 R /Fit] >>";
        for (int i = 0; i < extraLinks; ++i)
        {
            objects[102 + i] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [" + QByteArray::number(1000 + i) + " 0 R] >>";
            objects[1000 + i] = "<< /Type /Annot /Subtype /Link /Rect [54 600 234 615] /A << /S /URI /URI (https://extra.example/" + QByteArray::number(i) + ") >> >>";
        }
        return build(objects);
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static PDFAppToolGUISanitize* sanitize(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUISanitize*>(shell->getGUI()) : nullptr;
    }

    static PDFAppToolGUIRemoveLinks* links(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIRemoveLinks*>(shell->getGUI()) : nullptr;
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

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    static QString statusMessage(pdf::PDFAppDocumentTab* tab)
    {
        return tab->getView()->getStatusBar()->getMessage();
    }
};

class AppToolProtectTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registered();
    void sanitizeFrame();
    void sanitizePresets();
    void sanitizeCleanDocument();
    void sanitizeInvisibleTextCallout();
    void sanitizeSignatureFieldsRow();
    void sanitizeRunAndKeep();
    void sanitizeDeletedWithPresetPanel();
    void linksFrame();
    void linksUntickAndUndo();
    void linksFilterAndSelect();
    void linksNoneFound();
    void linksRowShowsPage();
    void linksMoreHint();
    void linksRunAndKeep();
    void mcpRemoveLinksAndSanitize();
    void screenshots();

    void sanitizePermissionRefusal();
    void mcpSanitizePermissions();
};

void AppToolProtectTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolProtectTest::registered()
{
    // The context registers both tools with their GUI
    AppToolShellFixture fixture;
    const QStringList implemented = pdf::PDFDocumentToolRegistry::getImplementedTools();
    QVERIFY(implemented.contains(ProtectAppHelper::SANITIZE_ID));
    QVERIFY(implemented.contains(ProtectAppHelper::LINKS_ID));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(ProtectAppHelper::SANITIZE_ID);
    pdf::PDFAppToolGUIProtect::registerTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(ProtectAppHelper::SANITIZE_ID), tool);
}

void AppToolProtectTest::sanitizeFrame()
{
    // 438:12588
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("Project_Report.pdf")), H::hiddenData(true, false)));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));
    AppToolShellFixture::settle();

    // The toolbar: Preset: Standard, the Primary button Remove Hidden Data
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Standard"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove Hidden Data"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The panels and the canvas: 300 + 6 + 814 + 320, the form 640 in the middle (x 87)
    QCOMPARE(H::inWindow(shell->getLeftPanel()).width(), 300);
    QCOMPARE(H::inWindow(shell->getRightPanel()), QRect(1120, 90, 320, 842));
    const QRect form = H::inWindow(gui->getForm());
    QCOMPARE(form.width(), 640);
    QCOMPARE(form.left(), 306 + 87);
    QCOMPARE(form.top(), 90 + 20);

    // Scan: 6 of the 9 kinds are in the document (no attachments, page labels, index)
    QCOMPARE(H::rows(gui->getScanRows()), QStringList({ QStringLiteral("Kinds found: 6 of 9"), QStringLiteral("File size: %1").arg(pdf::PDFDocumentToolFormat::formatSize(QFileInfo(fixture.path(QStringLiteral("Project_Report.pdf"))).size())), QStringLiteral("Pages: 2") }));

    // The rows: a kind, which is not there, has the tag Not found and cannot be ticked
    pdf::PDFTileRow* attachments = gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Attachments);
    QVERIFY(attachments);
    QCOMPARE(attachments->getTag(), QStringLiteral("Not found"));
    QCOMPARE(attachments->getDescription(), QStringLiteral("Not found in this document"));
    QVERIFY(!attachments->isCheckBoxEnabled());
    QVERIFY(!attachments->isCheckBoxChecked());

    pdf::PDFTileRow* comments = gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Comments);
    QCOMPARE(comments->getDescription(), QStringLiteral("1 comment, highlight or other markup annotation"));
    QVERIFY(comments->isCheckBoxChecked());
    QCOMPARE(gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Outline)->getDescription(), QStringLiteral("1 bookmark"));
    QCOMPARE(gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText)->getDescription(), QStringLiteral("On 1 scanned page"));
    QVERIFY(!gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText)->isCheckBoxChecked());
    QVERIFY(!gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::SignatureFields));

    // After removing: Removed 5 kinds, Kept 1 kind, Invisible text Kept
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Removed: 5 kinds of data"), QStringLiteral("Kept: 1 kind"), QStringLiteral("Invisible text: Kept") }));
    QVERIFY(!gui->getCleanCallout()->isVisible());
    QVERIFY(!gui->getInvisibleTextCallout()->isVisible());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("5 kinds of hidden data will be removed · nothing is changed until you confirm"));
}

void AppToolProtectTest::sanitizePresets()
{
    // 438:12831, 438:13119, 438:13356, 438:13607
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("report.pdf")), H::hiddenData(true, false)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));

    // The menu: the chosen preset has its check
    QVERIFY(gui->getPresetRow(PDFAppToolGUISanitize::Preset::Standard)->isChecked());
    QCOMPARE(gui->getPresetRow(PDFAppToolGUISanitize::Preset::Everything)->getDescription(), QStringLiteral("All nine kinds including the OCR text layer"));

    // Before sending: a click on its row, one undo step
    gui->getPresetRow(PDFAppToolGUISanitize::Preset::BeforeSending)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Before sending"));
    QVERIFY(!gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Outline)->isCheckBoxChecked());
    QVERIFY(gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Comments)->isCheckBoxChecked());
    QVERIFY(gui->getPresetRow(PDFAppToolGUISanitize::Preset::BeforeSending)->isChecked());
    QVERIFY(shell->getModel()->canUndo());

    // Everything: the amber callout of the OCR layer (438:13356)
    gui->getPresetRow(PDFAppToolGUISanitize::Preset::Everything)->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getInvisibleTextCallout()->isVisible());
    QCOMPARE(H::rows(gui->getSummaryRows()).back(), QStringLiteral("Invisible text: Removed"));

    // Clear all: Custom, nothing to remove, the Primary button disabled (438:13607)
    gui->getPresetRow(PDFAppToolGUISanitize::Preset::Custom)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Custom"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Choose at least one kind of data to remove"));
    QCOMPARE(H::rows(gui->getSummaryRows()).front(), QStringLiteral("Removed: 0 kinds of data"));

    // Undo of the tool: Everything again
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Everything"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // A tick by hand: Custom (K18)
    gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Thumbnails)->getCheckBox()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Custom"));
    QVERIFY(!shell->getModel()->getValue(QStringLiteral("thumbnails")).toBool());
}

void AppToolProtectTest::sanitizeCleanDocument()
{
    // 438:13839
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("clean.pdf")), H::clean()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));
    AppToolShellFixture::settle();

    QVERIFY(gui->getCleanCallout()->isVisible());
    QCOMPARE(H::rows(gui->getScanRows()).front(), QStringLiteral("Kinds found: 0 of 9"));
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Removed: 0 kinds of data"), QStringLiteral("Kept: –"), QStringLiteral("Invisible text: Kept") }));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("No hidden data found in this document"));
    QCOMPARE(gui->getPresetButton()->text(), QStringLiteral("Preset: Standard"));
    for (pdf::PDFDocumentHiddenDataScan::Kind kind : { pdf::PDFDocumentHiddenDataScan::Kind::DocumentInfo, pdf::PDFDocumentHiddenDataScan::Kind::Thumbnails })
    {
        QCOMPARE(gui->getKindRow(kind)->getTag(), QStringLiteral("Not found"));
    }
}

void AppToolProtectTest::sanitizeInvisibleTextCallout()
{
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("scan.pdf")), H::hiddenData(true, false)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));

    gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText)->getCheckBox()->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getInvisibleTextCallout()->isVisible());
    QCOMPARE(gui->getInvisibleTextCallout()->getTitleLabel()->text(), QStringLiteral("Search and copy will stop working on 1 page"));

    // A document without invisible text has no such kind: the row cannot be ticked
    pdf::PDFAppDocumentTab* other = H::openReady(fixture, H::write(fixture.path(QStringLiteral("plain.pdf")), H::hiddenData(false, false)));
    PDFAppToolGUISanitize* otherGui = H::sanitize(fixture.openTool(other, H::SANITIZE_ID));
    QVERIFY(otherGui && AppToolShellFixture::waitFor([otherGui]() { return otherGui->isScanned(); }));
    QVERIFY(!otherGui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText)->isCheckBoxEnabled());
}

void AppToolProtectTest::sanitizeSignatureFieldsRow()
{
    // Owner answer Q10: the tenth kind is a row of a signed document, never ticked by Standard
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("signed.pdf")), H::hiddenData(false, true)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));
    AppToolShellFixture::settle();

    pdf::PDFTileRow* signatures = gui->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::SignatureFields);
    QVERIFY(signatures);
    QVERIFY(signatures->isCheckBoxEnabled());
    QVERIFY(!signatures->isCheckBoxChecked());
    QCOMPARE(signatures->getDescription(), QStringLiteral("1 signature field with the name of its signer"));
    QCOMPARE(H::rows(gui->getScanRows()).front(), QStringLiteral("Kinds found: 6 of 10"));
    QCOMPARE(gui->getPresetRow(PDFAppToolGUISanitize::Preset::Everything)->getDescription(), QStringLiteral("All ten kinds including the OCR text layer and the signature fields"));

    gui->getPresetRow(PDFAppToolGUISanitize::Preset::Everything)->click();
    AppToolShellFixture::settle();
    QVERIFY(signatures->isCheckBoxChecked());
}

void AppToolProtectTest::sanitizeRunAndKeep()
{
    // 438:14342: the dialog, the steps beside it; Keep in this tab is one undo step of the document
    using H = ProtectAppHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("Project_Report.pdf")), H::hiddenData(true, false)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    AppToolShellFixture::settle();

    // Save as a copy is preselected (LostCopyOnly)
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Hidden data removed – choose what to do with the result"));

    // The right panel: the steps of the five removed kinds, the size before and after
    QVERIFY(gui->getResultContent()->isVisible());
    QVERIFY(!gui->getEditingContent()->isVisible());
    const QList<pdf::PDFTileRow*> steps = gui->getStepsCard()->findChildren<pdf::PDFTileRow*>();
    QCOMPARE(steps.size(), 5);
    QCOMPARE(steps.front()->getTitle(), QStringLiteral("Document info"));
    QCOMPARE(steps.front()->getDescription(), QStringLiteral("Document info was removed"));
    QCOMPARE(H::rows(gui->getSizeRows()).size(), 3);
    QVERIFY(H::rows(gui->getSizeRows()).front().startsWith(QStringLiteral("Before: ")));

    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));

    // The document of the tab has no document info, no comment, no outline
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    QVERIFY(document->getInfo()->title.isEmpty());
    QVERIFY(document->getCatalog()->getOutlineRootPtr() == nullptr || document->getCatalog()->getOutlineRootPtr()->getTotalCount() == 0);
    QVERIFY(pdf::PDFDocumentHiddenDataScan::scan(document.data(), true, nullptr, nullptr).get(pdf::PDFDocumentHiddenDataScan::Kind::Comments).count == 0);
    QVERIFY(pdf::PDFDocumentHiddenDataScan::scan(document.data(), true, nullptr, nullptr).get(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText).isPresent);
    QVERIFY(tab->getDocument()->isModified());
}

void AppToolProtectTest::sanitizeDeletedWithPresetPanel()
{
    // The tab goes with the panel Preset open: the panel goes with the parts of the tool
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("report.pdf")), H::hiddenData(false, false)));
    PDFAppToolGUISanitize* gui = H::sanitize(fixture.openTool(tab, H::SANITIZE_ID));
    QVERIFY(gui);
    QPointer<pdf::PDFAppToolPanelPopup> panel = gui->getPresetPanel();
    QPointer<PDFAppToolGUISanitize> guiGuard = gui;
    gui->getPresetButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(panel && panel->isVisible());

    delete tab;
    AppToolShellFixture::settle();
    QVERIFY(!guiGuard);
    QVERIFY(!panel || !panel->isVisible());
}

void AppToolProtectTest::linksFrame()
{
    // 438:14751
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));
    AppToolShellFixture::settle();

    // The captions of the pages and the links, every link ticked
    pdf::PDFAppToolLinkListModel* model = gui->getListModel();
    QCOMPARE(model->rowCount(), 6);
    QCOMPARE(model->data(model->index(0), Qt::DisplayRole).toString(), QStringLiteral("Page 1"));
    QCOMPARE(model->data(model->index(1), Qt::DisplayRole).toString(), QStringLiteral("acme-corp.example"));
    QCOMPARE(model->data(model->index(1), pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("https://www.acme-corp.example/"));
    QCOMPARE(model->data(model->index(2), pdf::PDFComponentRoles::IconNameRole).toString(), QStringLiteral("mail"));
    QCOMPARE(model->data(model->index(3), Qt::DisplayRole).toString(), QStringLiteral("Page 2"));
    QCOMPARE(model->data(model->index(4), Qt::DisplayRole).toString(), QStringLiteral("Market study 2026"));
    QCOMPARE(model->data(model->index(4), Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QVERIFY(!(model->flags(model->index(0)) & Qt::ItemIsEnabled));

    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("4 found · 4 will be removed"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 4 Links"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("External links: 4"), QStringLiteral("Removed: 4"), QStringLiteral("Kept: 0"), QStringLiteral("Internal links: 1 · not touched") }));
    QCOMPARE(gui->getShowButton()->text(), QStringLiteral("Show: All links"));
    QCOMPARE(gui->getFilterRow(PDFAppToolGUIRemoveLinks::Filter::Web)->getDescription(), QStringLiteral("2 links to http and https pages"));
    QCOMPARE(gui->getFilterRow(PDFAppToolGUIRemoveLinks::Filter::EMail)->getDescription(), QStringLiteral("1 mailto link"));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("4 external links found · 4 will be removed, 0 are kept"));

    // The caption of the shown page at 20, 20 of the canvas (438:14940)
    QVERIFY(gui->getOverlay());
    QCOMPARE(gui->getOverlay()->getPageText(), QStringLiteral("Page 1"));
    QCOMPARE(gui->getOverlay()->pos(), QPoint(20, 20));
    QVERIFY(!gui->getEmptyCard()->isVisible());
    QVERIFY(gui->getListCard()->isVisible());
    QVERIFY(!gui->getMoreHint()->isVisible());
}

void AppToolProtectTest::linksUntickAndUndo()
{
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));

    // The check box of a row (the delegate writes Qt::CheckStateRole): the link is kept
    pdf::PDFAppToolLinkListModel* model = gui->getListModel();
    QVERIFY(model->setData(model->index(4), Qt::Unchecked, Qt::CheckStateRole));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("exclude")).toString(), QStringLiteral("12:0"));
    QCOMPARE(model->data(model->index(4), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 3 Links"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("4 found · 3 will be removed"));
    QCOMPARE(H::statusMessage(tab), QStringLiteral("4 external links found · 3 will be removed, 1 is kept"));
    QCOMPARE(gui->getOverlay()->getLinkState(model->getLinks()[2]), pdf::PDFAppToolLinkOverlay::LinkState::Kept);

    // Undo of the tool ticks it again
    QVERIFY(shell->getModel()->canUndo());
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(model->data(model->index(4), Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 4 Links"));

    // Every link kept: Remove 0 Links, disabled (438:15578)
    gui->selectNone();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 0 Links"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("Select at least one link to remove"));
}

void AppToolProtectTest::linksFilterAndSelect()
{
    // 438:15346: the filter changes the list only; Select None of the shown links keeps only them
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));

    gui->getFilterRow(PDFAppToolGUIRemoveLinks::Filter::EMail)->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getShowButton()->text(), QStringLiteral("Show: E-mail addresses"));
    QCOMPARE(gui->getListModel()->rowCount(), 2);
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("1 of 4 shown · e-mail addresses"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 4 Links"));

    gui->getSelectNoneButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getKeptCount(), 1);
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove 3 Links"));

    // The other ftp link (K17) is in All links only
    gui->setFilter(PDFAppToolGUIRemoveLinks::Filter::Web);
    QCOMPARE(gui->getListModel()->getShownLinks().size(), size_t(2));
    gui->setFilter(PDFAppToolGUIRemoveLinks::Filter::All);
    QCOMPARE(gui->getListModel()->getShownLinks().size(), size_t(4));
    gui->getSelectAllButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getKeptCount(), 0);
}

void AppToolProtectTest::linksNoneFound()
{
    // 438:15852
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("plain.pdf")), H::clean()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));
    AppToolShellFixture::settle();

    QVERIFY(gui->getEmptyCard()->isVisible());
    QVERIFY(!gui->getListCard()->isVisible());
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("None in this document"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove Links"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!gui->getSelectAllButton()->isEnabled());
    QVERIFY(!gui->getSelectNoneButton()->isEnabled());
    QVERIFY(!gui->getShowButton()->isEnabled());
    QCOMPARE(H::statusMessage(tab), QStringLiteral("No external link annotations found"));
}

void AppToolProtectTest::linksRowShowsPage()
{
    // A click on a row shows its link on the page (438:14751)
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));
    AppToolShellFixture::settle();

    pdf::PDFListView* view = gui->getListView();
    const QRect rect = view->visualRect(gui->getListModel()->index(4));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center() + QPoint(40, 0));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedId(), QStringLiteral("12:0"));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getView()->getDisplayedPageIndex() == 1; }));
    QCOMPARE(gui->getOverlay()->getPageText(), QStringLiteral("Page 2"));
    QCOMPARE(gui->getOverlay()->getLinkState(gui->getListModel()->getLinks()[2]), pdf::PDFAppToolLinkOverlay::LinkState::Selected);

    // The click on the row is no change of what is removed
    QCOMPARE(gui->getKeptCount(), 0);
}

void AppToolProtectTest::linksMoreHint()
{
    // K16: the rows below the card are said under it, the card is at most 455 high
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("many.pdf")), H::links(30)));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));
    AppToolShellFixture::settle();

    QVERIFY(gui->getListCard()->height() <= PDFAppToolGUIRemoveLinks::LIST_MAXIMUM_HEIGHT);
    QVERIFY(gui->getListCard()->height() >= PDFAppToolGUIRemoveLinks::LIST_MAXIMUM_HEIGHT - 60);
    QVERIFY(gui->getMoreHint()->isVisible());
    QVERIFY2(gui->getMoreHint()->text().contains(QStringLiteral("more links on pages")) && gui->getMoreHint()->text().endsWith(QStringLiteral("32 · scroll to see them")),
             qPrintable(gui->getMoreHint()->text()));

    // Scrolled to the end: nothing more below
    gui->getListView()->scrollToBottom();
    AppToolShellFixture::settle();
    QVERIFY(!gui->getMoreHint()->isVisible());
}

void AppToolProtectTest::linksRunAndKeep()
{
    // 438:16073: Keep in this tab preselected, the kept link stays
    using H = ProtectAppHelper;
    using Choice = pdf::PDFAppToolResultDialog::Choice;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::LINKS_ID);
    PDFAppToolGUIRemoveLinks* gui = H::links(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isListed(); }));
    gui->getListModel()->setData(gui->getListModel()->index(2), Qt::Unchecked, Qt::CheckStateRole);
    AppToolShellFixture::settle();

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), Choice::Keep);
    QCOMPARE(H::statusMessage(tab), QStringLiteral("External links removed – choose what to do with the result"));
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));

    const pdf::PDFDocumentLinkOperations::LinkList after = pdf::PDFDocumentLinkOperations::listExternalLinks(tab->getDocument()->getDocument().data());
    QCOMPARE(int(after.links.size()), 1);
    QCOMPARE(after.links.front().title, QStringLiteral("jane.novak@example.com"));
    QCOMPARE(after.internalLinkCount, 1);
}

void AppToolProtectTest::mcpRemoveLinksAndSanitize()
{
    // The actions of the tool protect of MCP (no code of its own in the bridge): remove-links
    // kept in the tab, sanitize written as a copy (its default)
    using H = ProtectAppHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    QJsonObject links;
    links.insert(QStringLiteral("action"), QStringLiteral("remove-links"));
    links.insert(QStringLiteral("documentId"), document->getId());
    links.insert(QStringLiteral("result"), QStringLiteral("keep"));
    links.insert(QStringLiteral("remove-links"), QJsonObject({ { QStringLiteral("exclude"), QStringLiteral("11:0") } }));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), links);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const pdf::PDFDocumentLinkOperations::LinkList after = pdf::PDFDocumentLinkOperations::listExternalLinks(document->getDocument().data());
    QCOMPARE(int(after.links.size()), 1);
    QCOMPARE(after.links.front().id, QStringLiteral("11:0"));

    // An id, which is no external link, is an error, nothing changes
    links.insert(QStringLiteral("remove-links"), QJsonObject({ { QStringLiteral("exclude"), QStringLiteral("14:0") } }));
    result = fixture.callTool(QStringLiteral("protect"), links);
    QVERIFY(result && result->isError);
    QCOMPARE(pdf::PDFDocumentLinkOperations::listExternalLinks(document->getDocument().data()).links.size(), size_t(1));

    // Sanitize: save_copy is the default; the copy has no document info
    pdf::PDFAppDocumentTab* hiddenTab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("hidden.pdf")), H::hiddenData(false, false)));
    QVERIFY(hiddenTab);
    const QString copy = fixture.path(QStringLiteral("hidden_clean.pdf"));
    QJsonObject sanitize;
    sanitize.insert(QStringLiteral("action"), QStringLiteral("sanitize"));
    sanitize.insert(QStringLiteral("documentId"), hiddenTab->getDocument()->getId());
    sanitize.insert(QStringLiteral("path"), copy);
    result = fixture.callTool(QStringLiteral("protect"), sanitize);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QFile file(copy);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    QVERIFY(!written.contains("MarkerAuthor"));
    QVERIFY(!written.contains("MarkerComment"));
    QVERIFY(!hiddenTab->getDocument()->isModified());
}

void AppToolProtectTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> writes the frames under their ids, compared with Figma
    using H = ProtectAppHelper;
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* theme : { "light", "dark" })
    {
        const QString suffix = QString::fromLatin1(theme) == QStringLiteral("dark") ? QStringLiteral("-dark") : QString();
        for (const QSize size : { QSize(1440, 960), QSize(720, 480) })
        {
            const QString sizeSuffix = size.width() == 720 ? QStringLiteral("-720") : QString();
            AppToolShellFixture fixture(size);
            pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme));

            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("Project_Report.pdf")), H::hiddenData(true, false)));
            PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
            PDFAppToolGUISanitize* gui = H::sanitize(shell);
            QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-12588") + sizeSuffix + suffix);
            gui->getPresetButton()->click();
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-12831") + sizeSuffix + suffix);
            gui->getPresetRow(PDFAppToolGUISanitize::Preset::Everything)->click();
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-13356") + sizeSuffix + suffix);
            shell->confirm();
            QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-14342") + sizeSuffix + suffix);
            shell->getResultDialog()->reject();
            AppToolShellFixture::settle();

            pdf::PDFAppDocumentTab* linksTab = H::openReady(fixture, H::write(fixture.path(QStringLiteral("links.pdf")), H::links(12)));
            PDFDocumentToolShell* linksShell = fixture.openTool(linksTab, H::LINKS_ID);
            PDFAppToolGUIRemoveLinks* links = H::links(linksShell);
            QVERIFY(links && AppToolShellFixture::waitFor([links]() { return links->isListed(); }));
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-14751") + sizeSuffix + suffix);
            links->getShowButton()->click();
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-15032") + sizeSuffix + suffix);
            links->getFilterRow(PDFAppToolGUIRemoveLinks::Filter::EMail)->click();
            AppToolShellFixture::settle();
            fixture.screenshot(QStringLiteral("protect-438-15346") + sizeSuffix + suffix);
        }
    }
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolProtectTest::sanitizePermissionRefusal()
{
    // ISO 32000-2, Table 22: the sample allows the changes of bit 4, not those of annotations and
    // fields (bits 6 and 9); its square annotation is a comment. The tool is allowed, the kind
    // Comments and markup is not: the Primary button is off and the status bar says why.
    using H = ProtectAppHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::SANITIZE_ID);
    PDFAppToolGUISanitize* gui = H::sanitize(shell);
    QVERIFY(gui && AppToolShellFixture::waitFor([gui]() { return gui->isScanned(); }));
    AppToolShellFixture::settle();
    QVERIFY(!shell->isPermissionRefused());

    const QString refusal = QStringLiteral("The permissions of the document do not allow changing its annotations and form fields: untick Comments and markup to remove the other kinds.");
    QVERIFY(shell->getModel()->getValue(QStringLiteral("comments")).toBool());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getStatusMessage(), refusal);
    QCOMPARE(H::statusMessage(tab), refusal);

    // Without the comments the rest can be removed
    QCOMPARE(shell->getModel()->setValue(QStringLiteral("comments"), false), QString());
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(shell->getStatusMessage() != refusal);

    // Ticked again: refused again
    QCOMPARE(shell->getModel()->setValue(QStringLiteral("comments"), true), QString());
    AppToolShellFixture::settle();
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getStatusMessage(), refusal);
}

void AppToolProtectTest::mcpSanitizePermissions()
{
    // The same check refuses the call of an assistant with the same text; nothing is written
    using H = ProtectAppHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);

    const QString copy = fixture.path(QStringLiteral("restricted_clean.pdf"));
    QJsonObject sanitize;
    sanitize.insert(QStringLiteral("action"), QStringLiteral("sanitize"));
    sanitize.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    sanitize.insert(QStringLiteral("path"), copy);
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), sanitize);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("The permissions of the document do not allow changing its annotations and form fields: untick Comments and markup to remove the other kinds.")),
             qPrintable(result->getFirstText()));
    QVERIFY(!QFile::exists(copy));

    sanitize.insert(QStringLiteral("sanitize"), QJsonObject({ { QStringLiteral("comments"), false } }));
    result = fixture.callTool(QStringLiteral("protect"), sanitize);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(QFile::exists(copy));
}

QTEST_MAIN(AppToolProtectTest)

#include "tst_apptoolprotecttest.moc"
