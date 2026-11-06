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

#include "apptoolshellfixture.h"
#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmcphost.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumentwriter.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QtTest>

using pdf::PDFAppToolGUIExtractText;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolShellFixture;

/// Documents and parts of the tool Extract Text
class ExtractTextGuiHelper
{
public:
    ExtractTextGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("convert.extract-text");

    /// A file, whose pages draw their texts (an empty text is a blank page, a scan)
    static QString writeDocument(const QString& filePath, const QStringList& pageTexts, uint32_t permissions = 0)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QString& pageText : pageTexts)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
            if (pageText.isEmpty())
            {
                continue;
            }
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            QFont font(QStringLiteral("Inter"));
            font.setPixelSize(14);
            painter->setFont(font);
            painter->drawText(QPointF(60, 80), pageText);
            content.end(painter);
        }

        pdf::PDFDocument document = builder.build();
        if (permissions != 0)
        {
            pdf::PDFDocumentBuilder encrypted(&document);
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = QStringLiteral("owner-secret");
            settings.permissions = permissions;
            encrypted.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
            document = encrypted.build();
        }

        pdf::PDFDocumentWriter writer(nullptr);
        writer.write(filePath, &document, false);
        return filePath;
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

    static PDFAppToolGUIExtractText* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIExtractText*>(shell->getGUI()) : nullptr;
    }

    /// The estimate of the preview and the text of the shown page have come
    static bool waitPreviews(PDFAppToolGUIExtractText* extract)
    {
        return AppToolShellFixture::waitFor([extract]()
        {
            return extract && extract->getResultRows() && extract->getResultRows()->getRowCount() == 3 && extract->getResultRows()->getValue(0) != QStringLiteral("–") &&
                   extract->getPreviewPage() && !extract->getPreviewPage()->getTextLabel()->text().isEmpty();
        });
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    static QString rowValue(const pdf::PDFKeyValueList* list, const QString& key)
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

    /// The button of the toolbar with the text
    static QToolButton* findButton(PDFDocumentToolShell* shell, const QString& textStart)
    {
        for (QToolButton* button : shell->getToolActions()->findChildren<QToolButton*>())
        {
            if (button->text().startsWith(textStart))
            {
                return button;
            }
        }
        return nullptr;
    }

    static pdf::PDFAppToolPanelPopup* openPanel(QToolButton* button)
    {
        if (!button)
        {
            return nullptr;
        }
        QTest::mouseClick(button, Qt::LeftButton);
        pdf::PDFAppToolPanelPopup* panel = nullptr;
        AppToolShellFixture::waitFor([&panel]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (pdf::PDFAppToolPanelPopup* candidate = qobject_cast<pdf::PDFAppToolPanelPopup*>(widget); candidate && candidate->isVisible())
                {
                    panel = candidate;
                }
            }
            return panel != nullptr;
        });
        return panel;
    }
};

/// A language, which marks the texts of the canvas page of Extract Text and of its GUI
class ExtractTextTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("PDFAppToolTextPreviewPage") || name.contains("PDFAppToolGUIExtractText"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

class AppToolExtractTextTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registered();
    void frameGeometry();
    void rightPanelRows();
    void previewFollowsShownPage();
    void menus();
    void formatChangesPreview();
    void pagePictureSizes();
    void runningTexts();
    void resultDialogSavesFile();
    void resultDialogClipboard();
    void nothingToDoInfoBar();
    void permissionRefused();
    void documentWithoutPages();
    void languageChange();
    void themesAndDirection();
    void deletionWhilePreviewPending();
    void mcpReturnTextEncodings();
};

void AppToolExtractTextTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolExtractTextTest::registered()
{
    // The context registers the tool with its GUI once (PDFAppBuiltinTools::registerAll)
    AppToolShellFixture fixture;
    QVERIFY(pdf::PDFDocumentToolRegistry::getImplementedTools().contains(ExtractTextGuiHelper::TOOL_ID));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(ExtractTextGuiHelper::TOOL_ID);
    QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolExtractText>(tool));
    PDFAppToolGUIExtractText::registerTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(ExtractTextGuiHelper::TOOL_ID), tool);
}

void AppToolExtractTextTest::frameGeometry()
{
    // 339:8112 at 1440 x 960
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { QStringLiteral("Market Analysis"), QStringLiteral("Regional revenue"), QString() }));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitPreviews(gui));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("extract-text-339-8112"));

    // tool-actions 339:8132: Order: Auto, Include: 1 extra, Format: Text; Primary Extract Text
    QVERIFY(H::findButton(shell, QStringLiteral("Order: Auto")));
    QVERIFY(H::findButton(shell, QStringLiteral("Include: 1 extra")));
    QVERIFY(H::findButton(shell, QStringLiteral("Format: Text")));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Extract Text"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // sidebar-left 339:8139 300 wide, the page selection at x 12, 275 wide; the thumbnails below it
    QCOMPARE(H::inWindow(shell->getLeftPanel()).width(), 300);
    const pdf::PDFPageSelection* selection = shell->getLeftContent()->findChild<pdf::PDFPageSelection*>();
    QVERIFY(selection);
    QCOMPARE(H::inWindow(selection).left(), 12);
    QCOMPARE(selection->width(), 275);
    QVERIFY(gui->getThumbnailsPanel());

    // 339:8282: two columns of the items 124 wide, 132 apart, inside of the 275 of the panel
    const pdf::PDFAppThumbnailListView* thumbnails = gui->getThumbnailsPanel()->getListView();
    const QRect first = thumbnails->visualRect(thumbnails->model()->index(0, 0));
    const QRect second = thumbnails->visualRect(thumbnails->model()->index(1, 0));
    QCOMPARE(second.top(), first.top());
    QCOMPARE(second.left() - first.left(), 132);
    QCOMPARE(first.width(), 124);
    QVERIFY(H::inWindow(gui->getThumbnailsPanel()).top() > H::inWindow(selection).bottom());

    // sidebar-right 339:8146, the canvas 814 between them
    QCOMPARE(H::inWindow(shell->getRightPanel()), QRect(1120, 90, 320, 842));
    QCOMPARE(tab->getView()->getCanvasArea()->width(), 814);

    // document-canvas 339:8145: padding 20, the bar 36 with Fit / 100 % 120 x 32, two panes of 381
    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    QVERIFY(page && page->isVisible());
    QCOMPARE(page->getTitleLabel()->text(), QStringLiteral("Page 1 · preview of the extracted text"));
    QCOMPARE(page->getZoomControl()->size(), QSize(120, 32));
    QCOMPARE(page->getZoomControl()->getCurrentIndex(), 0);
    const QWidget* pagePane = page->findChild<QWidget*>(QStringLiteral("toolPanePage"));
    const QWidget* textPane = page->findChild<QWidget*>(QStringLiteral("toolPaneText"));
    QVERIFY(pagePane && textPane);
    QCOMPARE(pagePane->width(), 381);
    QCOMPARE(textPane->width(), 381);
    QCOMPARE(H::inWindow(textPane).left() - H::inWindow(pagePane).right() - 1, 12);
    QCOMPARE(H::inWindow(pagePane).left(), 306 + 20);

    // The text of the page as it is written (339:8711), the footers (339:8648, 339:8712)
    QCOMPARE(page->getTextLabel()->text(), QStringLiteral("Market Analysis\n\n"));
    QCOMPARE(page->getPageFooter()->text(), QStringLiteral("Page 1 · untagged document"));
    QCOMPARE(page->getTextFooter()->text(), QStringLiteral("Plain text · UTF-8"));

    // The status bar 339:8148
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Extract Text · Project_Report.pdf · 3 pages · about 4 words"));
}

void AppToolExtractTextTest::rightPanelRows()
{
    // sidebar-right 339:8146: Document, Settings, Result and the callout of 339:8759
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("one two three"), QString(), QStringLiteral("four five"), QString() }));
    QVERIFY(tab);
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(H::waitPreviews(gui));

    QCOMPARE(H::rowValue(gui->getDocumentRows(), QStringLiteral("Tagged")), QStringLiteral("No"));
    QCOMPARE(H::rowValue(gui->getDocumentRows(), QStringLiteral("Pages with text")), QStringLiteral("2 of 4"));
    QCOMPARE(H::rowValue(gui->getDocumentRows(), QStringLiteral("Copying content")), QStringLiteral("Allowed"));

    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Pages")), QStringLiteral("All · 4"));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Reading order")), QStringLiteral("Auto → layout"));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Included")), QStringLiteral("Alt. descriptions"));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Format")), QStringLiteral("Plain text · UTF-8"));

    QCOMPARE(H::rowValue(gui->getResultRows(), QStringLiteral("Words")), QStringLiteral("about 5"));
    QCOMPARE(H::rowValue(gui->getResultRows(), QStringLiteral("Characters")), QStringLiteral("about 22"));
    QVERIFY(H::rowValue(gui->getResultRows(), QStringLiteral("Size")).startsWith(QStringLiteral("about ")));

    QVERIFY(gui->getNoTextCallout()->isVisible());
    QCOMPARE(gui->getNoTextCallout()->getTitleLabel()->text(), QStringLiteral("2 pages have no text"));
    QCOMPARE(gui->getPreviewPage()->getEstimateLabel()->text(), QStringLiteral("Whole document: about 5 words"));
    QVERIFY(gui->getPreviewPage()->getEstimateDetailLabel()->text().endsWith(QStringLiteral(" of text · 2 pages have no text")));

    // A selection of pages: its rows and the estimate of the selected pages
    gui->getModel()->setValue(QStringLiteral("pages"), pdf::PDFDocumentToolPageSelection{ QStringLiteral("3"), {} }.toVariant());
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return H::rowValue(gui->getResultRows(), QStringLiteral("Words")) == QStringLiteral("about 2"); }));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Pages")), QStringLiteral("3 · 1"));
    QVERIFY(!gui->getNoTextCallout()->isVisible());
    QCOMPARE(gui->getPreviewPage()->getEstimateLabel()->text(), QStringLiteral("Selected pages: about 2 words"));
}

void AppToolExtractTextTest::previewFollowsShownPage()
{
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha"), QStringLiteral("beta"), QString() }));
    QVERIFY(tab);
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(H::waitPreviews(gui));
    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    QCOMPARE(page->getTextLabel()->text(), QStringLiteral("alpha\n\n"));

    // A click into the thumbnails shows another page (339:8282 → the panes)
    tab->getView()->goToPage(1);
    QVERIFY(AppToolShellFixture::waitFor([page]() { return page->getTextLabel()->text() == QStringLiteral("beta\n\n"); }));
    QCOMPARE(page->getTitleLabel()->text(), QStringLiteral("Page 2 · preview of the extracted text"));
    QCOMPARE(page->getPicture()->getPageIndex(), pdf::PDFInteger(1));

    tab->getView()->goToPage(2);
    QVERIFY(AppToolShellFixture::waitFor([page]() { return page->getTextLabel()->text() == QStringLiteral("This page has no text."); }));
    QCOMPARE(page->getPageFooter()->text(), QStringLiteral("Page 3 · untagged document"));
}

void AppToolExtractTextTest::menus()
{
    // 339:8765: Order 270 x 252 (4 rows of 57), Include 280 x 361 (7 rows + the caption of the
    // tags), Format 260 x 388 (FORMAT, 3 rows, ENCODING, 3 rows)
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(H::waitPreviews(gui));

    pdf::PDFAppToolPanelPopup* order = H::openPanel(H::findButton(shell, QStringLiteral("Order:")));
    QVERIFY(order);
    QStringList titles;
    for (const QPointer<pdf::PDFTileRow>& row : order->getRows())
    {
        titles << row->getTitle();
    }
    QCOMPARE(titles, QStringList({ "Auto", "Layout", "Content stream", "Structure" }));
    QVERIFY(order->getRows().front()->isChecked());
    order->close();
    AppToolShellFixture::settle();

    pdf::PDFAppToolPanelPopup* include = H::openPanel(H::findButton(shell, QStringLiteral("Include:")));
    QVERIFY(include);
    titles.clear();
    for (const QPointer<pdf::PDFTileRow>& row : include->getRows())
    {
        titles << row->getTitle();
    }
    QCOMPARE(titles, QStringList({ "Page numbers", "Structure titles", "Languages", "Alternative descriptions", "Expanded forms", "Actual text", "Phonemes" }));

    // A row toggles its item, the panel stays open, the button counts the items
    QTest::mouseClick(include->getRows().front(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(include->isVisible());
    QVERIFY(gui->getModel()->getValue(QStringLiteral("includePageNumbers")).toBool());
    QVERIFY(H::findButton(shell, QStringLiteral("Include: 2 extras")));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Included")), QStringLiteral("2 items"));
    include->close();
    AppToolShellFixture::settle();

    pdf::PDFAppToolPanelPopup* format = H::openPanel(H::findButton(shell, QStringLiteral("Format:")));
    QVERIFY(format);
    titles.clear();
    for (const QPointer<pdf::PDFTileRow>& row : format->getRows())
    {
        titles << row->getTitle();
    }
    QCOMPARE(titles, QStringList({ "Plain text", "HTML", "XML", "UTF-8", "Latin-1", "System" }));
    format->close();
}

void AppToolExtractTextTest::formatChangesPreview()
{
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(H::waitPreviews(gui));

    // The pane shows the file as it is written: an XML file shows its elements
    gui->getModel()->setValue(QStringLiteral("format"), QStringLiteral("xml"));
    gui->getModel()->setValue(QStringLiteral("encoding"), QStringLiteral("latin-1"));
    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    QVERIFY(AppToolShellFixture::waitFor([page]() { return page->getTextLabel()->text().contains(QStringLiteral("<pdftool:text>alpha</pdftool:text>")); }));
    QCOMPARE(page->getTextFooter()->text(), QStringLiteral("XML · Latin-1"));
    QVERIFY(H::findButton(shell, QStringLiteral("Format: XML")));
    QCOMPARE(H::rowValue(gui->getSettingsRows(), QStringLiteral("Format")), QStringLiteral("XML · Latin-1"));
}

void AppToolExtractTextTest::pagePictureSizes()
{
    // The pane PAGE (339:8647): the paper at the top of the area, centered; 100 % has the size of
    // the page at the resolution of the screen and scrolls
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(H::waitPreviews(gui));
    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    pdf::PDFAppToolPagePicture* picture = page->getPicture();

    const QImage image = picture->grab().toImage();
    const QRect paper = picture->getPaperRect();
    QVERIFY(paper.isValid());
    QCOMPARE(paper.top(), 14);
    QVERIFY(qAbs(paper.center().x() - picture->width() / 2) <= 1);
    QVERIFY(qAbs(double(paper.width()) / double(paper.height()) - 595.0 / 842.0) < 0.01);
    QCOMPARE(QColor(image.pixel(paper.right() - 3, paper.bottom() - 3)), QColor(Qt::white));

    page->getZoomControl()->setCurrentIndex(1);
    AppToolShellFixture::settle();
    QVERIFY(!picture->isFit());
    QCOMPARE(picture->getActualSize(), (QSizeF(595, 842) * (picture->logicalDpiX() / 72.0)).toSize());
    QCOMPARE(picture->width(), qMax(picture->getActualSize().width() + 28, page->getPictureArea()->viewport()->width()));
}

void AppToolExtractTextTest::runningTexts()
{
    // 356:13079: the card "Reading page 640 of 1,592" without a step line, "About 20 s left ·
    // Stop ends the run, nothing is kept", the status bar
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(gui);

    const QString step = QStringLiteral("Reading page · 640 of 1,592");
    QCOMPARE(gui->getRunningCardTitle(40, step), QStringLiteral("Reading page 640 of 1,592"));
    QCOMPARE(gui->getRunningCardStepText(step), QString());
    QCOMPARE(gui->getRunningTimeText(20), QStringLiteral("About 20 s left · Stop ends the run, nothing is kept"));
    QCOMPARE(gui->getRunningTimeText(-1), QStringLiteral("Stop ends the run, nothing is kept"));
    QCOMPARE(gui->getRunningStatusText(step), QStringLiteral("Extract Text · reading page 640 of 1,592 · Stop ends the run"));
}

void AppToolExtractTextTest::resultDialogSavesFile()
{
    // 339:9601: Save as a file preselected (Recommended), Copy to clipboard, Save Text, no
    // Then continue with (K20); the facts Pages, Words, Size
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { QStringLiteral("alpha beta"), QString() }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(H::waitPreviews(H::gui(shell)));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("extract-text-339-9601"));
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::Files);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::File);
    QVERIFY(dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Clipboard));
    QVERIFY(dialog->getContinueToolIds().isEmpty());
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Text"));
    QCOMPARE(H::rowValue(dialog->getFacts(), QStringLiteral("Pages")), QStringLiteral("1 with text · 1 without"));
    QCOMPARE(H::rowValue(dialog->getFacts(), QStringLiteral("Words")), QStringLiteral("2"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Extract Text finished · the text is waiting for your decision"));
    QVERIFY(dialog->getTargetFilePath().endsWith(QStringLiteral("Project_Report.txt")));

    // 339:9606: the actions of the toolbar are disabled below the dialog
    QVERIFY(!H::findButton(shell, QStringLiteral("Order:"))->isEnabled());
    QVERIFY(!H::findButton(shell, QStringLiteral("Format:"))->isEnabled());

    // The page of the tool deletes its shell after the decision: a gone shell has no dialog
    const QPointer<PDFDocumentToolShell> shellGuard(shell);
    const QString target = dialog->getTargetFilePath();
    QTest::mouseClick(dialog->getConfirmButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([target]() { return QFile::exists(target); }));
    QVERIFY(AppToolShellFixture::waitFor([shellGuard]() { return !shellGuard || !shellGuard->getResultDialog(); }));
    QFile file(target);
    QVERIFY(file.open(QFile::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("alpha beta\n\n"));

    // The document is not changed
    QVERIFY(!tab->getDocument()->isModified());
}

void AppToolExtractTextTest::resultDialogClipboard()
{
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("café") }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(H::waitPreviews(gui));

    // Latin-1 on the clipboard: the text of the file decoded by its charset, not as UTF-8
    gui->getModel()->setValue(QStringLiteral("encoding"), QStringLiteral("latin-1"));
    AppToolShellFixture::settle();
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    dialog->selectChoice(pdf::PDFAppToolResultDialog::Choice::Clipboard);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Copy to Clipboard"));
    // The page of the tool deletes its shell after the decision: a gone shell has no dialog
    const QPointer<PDFDocumentToolShell> shellGuard(shell);
    QTest::mouseClick(dialog->getConfirmButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shellGuard]() { return !shellGuard || !shellGuard->getResultDialog(); }));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("café\n\n"));
}

void AppToolExtractTextTest::nothingToDoInfoBar()
{
    // Never an empty file: a scan without text says so over the canvas, no dialog
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("scan.pdf")), { QString(), QString() }));
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return H::rowValue(gui->getDocumentRows(), QStringLiteral("Pages with text")) == QStringLiteral("0 of 2"); }));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getView()->findInfoBar(QStringLiteral("tool-result")); }));
    QVERIFY(tab->getView()->findInfoBar(QStringLiteral("tool-result"))->title.contains(QStringLiteral("no text")) ||
            tab->getView()->findInfoBar(QStringLiteral("tool-result"))->text.contains(QStringLiteral("no text")));
    QVERIFY(!shell->getResultDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
}

void AppToolExtractTextTest::permissionRefused()
{
    // 339:10328 (Q7: only Close, the scrim over the canvas): the canvas is empty, copying "Not allowed"
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const uint32_t permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::CopyContent);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("restricted.pdf")), { QStringLiteral("secret") }, permissions));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIExtractText* gui = H::gui(shell);
    QVERIFY(shell->isPermissionRefused());
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getPermissionDialog() && shell->getPermissionDialog()->isVisible(); }));
    QStringList texts;
    for (const pdf::PDFLabel* label : shell->getPermissionDialog()->findChildren<pdf::PDFLabel*>())
    {
        texts << label->text();
    }
    QVERIFY2(texts.contains(QStringLiteral("This document does not allow copying")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY2(texts.contains(QStringLiteral("Its author restricted the extraction of text and images.")), qPrintable(texts.join(QLatin1Char('|'))));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("extract-text-339-10328"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Extract Text · the document does not allow copying its content"));
    QCOMPARE(H::rowValue(gui->getDocumentRows(), QStringLiteral("Copying content")), QStringLiteral("Not allowed"));
    QCOMPARE(H::rowValue(gui->getResultRows(), QStringLiteral("Words")), QStringLiteral("–"));
    QVERIFY(!gui->getPreviewPage()->findChild<QWidget*>(QStringLiteral("toolPanePage"))->isVisibleTo(gui->getPreviewPage()));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!gui->getPageRunner());
    QVERIFY(!H::findButton(shell, QStringLiteral("Order:"))->isEnabled());
    QVERIFY(!H::findButton(shell, QStringLiteral("Include:"))->isEnabled());
}

void AppToolExtractTextTest::documentWithoutPages()
{
    // A page tree without a page (/Count 0, hostile or damaged): the pane PAGE has no page to
    // show - no title, no paper - in Fit and at 100 %, the canvas is drawn
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("empty.pdf")), {}));
    QVERIFY(tab);
    QCOMPARE(tab->getDocument()->getDocument()->getCatalog()->getPageCount(), size_t(0));
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(gui && gui->getPreviewPage());
    AppToolShellFixture::settle();

    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    pdf::PDFAppToolPagePicture* picture = page->getPicture();
    QCOMPARE(gui->getPreviewPageIndex(), pdf::PDFInteger(-1));
    QCOMPARE(page->getTitleLabel()->text(), QString());
    QCOMPARE(picture->getActualSize(), QSize());

    const QImage fit = picture->grab().toImage();
    QVERIFY(!fit.isNull());
    QVERIFY(!picture->getPaperRect().isValid());

    page->getZoomControl()->setCurrentIndex(1);
    AppToolShellFixture::settle();
    QVERIFY(!picture->isFit());
    QVERIFY(!picture->grab().toImage().isNull());
    QVERIFY(!picture->getPaperRect().isValid());
}

void AppToolExtractTextTest::languageChange()
{
    // The shell keeps the canvas page over a change of the language: the captions of the panes,
    // Fit / 100 % and the bar 339:8629 are in the new language at once, the zoom stays
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(H::waitPreviews(gui));
    pdf::PDFAppToolTextPreviewPage* page = gui->getPreviewPage();
    page->getZoomControl()->setCurrentIndex(1);
    AppToolShellFixture::settle();

    const auto captions = [page]()
    {
        QStringList texts;
        for (const pdf::PDFLabel* label : page->findChildren<pdf::PDFLabel*>(QStringLiteral("toolPaneCaption")))
        {
            texts << label->text();
        }
        for (const QToolButton* button : page->getZoomControl()->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly))
        {
            texts << button->text();
        }
        return texts;
    };
    QCOMPARE(captions(), QStringList({ QStringLiteral("PAGE"), QStringLiteral("EXTRACTED TEXT"), QStringLiteral("Fit"), QStringLiteral("100 %") }));

    ExtractTextTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(captions(), QStringList({ QStringLiteral("[T] PAGE"), QStringLiteral("[T] EXTRACTED TEXT"), QStringLiteral("[T] Fit"), QStringLiteral("[T] 100 %") }));
    QCOMPARE(page->getZoomControl()->accessibleName(), QStringLiteral("[T] Size of the page"));
    QCOMPARE(page->getTitleLabel()->text(), QStringLiteral("[T] Page 1 · preview of the extracted text"));
    QCOMPARE(page->getZoomControl()->getCurrentIndex(), 1);
    QVERIFY(!page->getPicture()->isFit());

    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(captions(), QStringList({ QStringLiteral("PAGE"), QStringLiteral("EXTRACTED TEXT"), QStringLiteral("Fit"), QStringLiteral("100 %") }));
    QCOMPARE(page->getTitleLabel()->text(), QStringLiteral("Page 1 · preview of the extracted text"));
}

void AppToolExtractTextTest::themesAndDirection()
{
    // Both themes at 1 / 2, right-to-left: the canvas of the tool has no leak color
    using H = ExtractTextGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("alpha") }));
    PDFAppToolGUIExtractText* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(H::waitPreviews(gui));

    for (const char* theme : { "light", "dark" })
    {
        pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme));
        AppToolShellFixture::settle();
        for (const qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = AppToolShellFixture::render(gui->getPreviewPage(), ratio);
            QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
            const QImage right = AppToolShellFixture::render(gui->getPreviewPage()->window(), ratio);
            QCOMPARE(AppToolShellFixture::countLeakPixels(right), 0);
        }
    }

    // The canvas of the track (339:8145 bg/track) in the dark theme
    const QImage image = gui->getPreviewPage()->grab().toImage();
    QCOMPARE(QColor(image.pixel(5, 5)), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgTrack));
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));

    QApplication::setLayoutDirection(Qt::RightToLeft);
    AppToolShellFixture::settle();
    const QWidget* pagePane = gui->getPreviewPage()->findChild<QWidget*>(QStringLiteral("toolPanePage"));
    const QWidget* textPane = gui->getPreviewPage()->findChild<QWidget*>(QStringLiteral("toolPaneText"));
    QVERIFY(H::inWindow(pagePane).left() > H::inWindow(textPane).left());
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(gui->getPreviewPage(), 1.0)), 0);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    AppToolShellFixture::settle();
}

void AppToolExtractTextTest::deletionWhilePreviewPending()
{
    // The tab and the window deleted while the previews of the page and of the estimate compute:
    // nobody waits, the late results are dropped
    using H = ExtractTextGuiHelper;
    for (int victim = 0; victim < 3; ++victim)
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        QStringList pages;
        for (int i = 0; i < 40; ++i)
        {
            pages << QStringLiteral("page text number %1").arg(i);
        }
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), pages));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
        QPointer<PDFAppToolGUIExtractText> gui = H::gui(shell);
        QVERIFY(gui);
        tab->getView()->goToPage(5);
        QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
        if (victim == 0)
        {
            delete tab;
        }
        else if (victim == 1)
        {
            delete tab->window();
        }
        else
        {
            shell->close();
        }
        QTest::qWait(300 * AppToolShellFixture::getTimeoutFactor());
        AppToolShellFixture::settle();
        QVERIFY(victim == 2 || !tabGuard);
        QVERIFY(victim == 2 || !gui);
    }
}

void AppToolExtractTextTest::mcpReturnTextEncodings()
{
    // return_text of MCP (chapter 6 of the plan): the text decoded by the charset of the file -
    // Latin-1 and the encoding of the system, also on Windows without ICU
    using H = ExtractTextGuiHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("report.pdf")), { QStringLiteral("Příliš café") }));
    QVERIFY(tab);

    const QString charset = pdf::PDFDocumentTextExporter::getSystemCharset();
    const pdf::PDFDocumentTextExporter::EncodedText expected = pdf::PDFDocumentTextExporter::encode(QStringLiteral("Příliš café\n\n"), pdf::PDFOutputFormatter::Style::Text, pdf::PDFDocumentTextExporter::Encoding::System);
    QStringDecoder systemDecoder(QStringConverter::System);
    const QString systemText = systemDecoder.decode(expected.data);

    for (const char* encoding : { "latin-1", "system", "utf-8" })
    {
        QJsonObject parameters;
        parameters.insert(QStringLiteral("encoding"), QString::fromLatin1(encoding));
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), QStringLiteral("extract-text"));
        arguments.insert(QStringLiteral("result"), QStringLiteral("return_text"));
        arguments.insert(QStringLiteral("extract-text"), parameters);
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("convert"), arguments);
        QVERIFY(result);
        QVERIFY2(!result->isError, result->result.constData());
        const QJsonObject answer = QJsonDocument::fromJson(result->result).object();

        QString text;
        for (const QJsonValue& block : answer.value(QStringLiteral("content")).toArray())
        {
            text += block.toObject().value(QStringLiteral("text")).toString();
        }
        const QJsonObject content = answer.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("extract-text")).toObject();
        if (qstrcmp(encoding, "latin-1") == 0)
        {
            QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), QStringLiteral("text/plain; charset=ISO-8859-1"));
            QVERIFY2(text.contains(QStringLiteral("P?íli? café")), qPrintable(text));
        }
        else if (qstrcmp(encoding, "system") == 0)
        {
            QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), charset == QLatin1String("UTF-8") ? QStringLiteral("text/plain") : QStringLiteral("text/plain; charset=%1").arg(charset));
            QVERIFY2(text.contains(systemText.trimmed()), qPrintable(text));
        }
        else
        {
            QVERIFY2(text.contains(QStringLiteral("Příliš café")), qPrintable(text));
        }
    }
}

QTEST_MAIN(AppToolExtractTextTest)

#include "tst_apptoolextracttexttest.moc"
