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

// The tool Optimize Structure in the application (package 2C of the plan of the first batch,
// frames 302:4072, 303:4405, 303:4560, 303:4897). The expected values are the numbers and the
// texts of the frames.

#include "apptoolshellfixture.h"
#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolgui_structure.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumenttool_structure.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonObject>
#include <QTranslator>
#include <QtTest>

using pdf::PDFAppToolGUIStructure;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolShellFixture;

namespace pdftest
{

/// A translator of the texts of the tool
class StructureTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("Structure"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

}   // namespace pdftest

/// Helpers of the test
class StructureGuiHelper
{
public:
    StructureGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("optimize.structure");

    /// A PDF of two pages, which the tool makes smaller: an unused object, two identical
    /// streams and a Flate stream stored without compression
    static QString writeReducible(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 2; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            pdf::PDFDictionaryBuilder content;
            const QByteArray data = "1 0 0 rg 10 10 80 80 re f";
            content.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
            const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(content), QByteArray(data))));
            pdf::PDFDictionaryBuilder update;
            update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        }

        QByteArray text;
        for (int i = 0; i < 2000; ++i)
        {
            text += "A line of the text, which repeats itself. ";
        }
        const QByteArray stored = qCompress(text, 0).mid(4);
        pdf::PDFDictionaryBuilder flate;
        flate.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
        flate.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(stored.size()));
        builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(flate), QByteArray(stored))));
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// A file, which the tool has optimized already: it does not get smaller again
    static QString writeOptimized(const QString& folder, const QString& filePath)
    {
        const QString source = writeReducible(folder + QStringLiteral("/source-of-optimized.pdf"));
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly))
        {
            return QString();
        }
        const QByteArray data = file.readAll();
        pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        pdf::PDFDocumentToolStructure tool;
        const pdf::PDFDocumentToolResult result = tool.run(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data, source, false)), QVariantMap(), nullptr, nullptr);
        if (result.documents.empty())
        {
            return QString();
        }
        pdf::PDFDocument output(*result.documents.front().document);
        return pdftest::AppDocumentSamples::write(output, filePath) ? filePath : QString();
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

    static PDFAppToolGUIStructure* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIStructure*>(shell->getGUI()) : nullptr;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    /// Confirms the tool and waits for its result dialog
    static pdf::PDFAppToolResultDialog* confirmAndWait(PDFDocumentToolShell* shell)
    {
        shell->confirm();
        if (!AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return shell->getResultDialog();
    }

    static QString steps(PDFAppToolGUIStructure* gui)
    {
        return gui->getStepsMenu()->getButton()->text();
    }
};

class AppToolStructureTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registered();
    void frameGeometry();
    void documentFactsAndStatus();
    void stepsPanel();
    void selectAllAndClear();
    void runShowsResultOfTheDialog();
    void copyReportInTheDialog();
    void keepIsOneUndoStep();
    void nothingToDoInThePanel();
    void signedDocumentCopyOnly();
    void leakAndThemes_data();
    void leakAndThemes();
    void chipRightToLeft();
    void minimalWindow();
    void languageChange();
    void deletedInEachState();
    void mcpAction();
    void screenshots();
};

void AppToolStructureTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolStructureTest::registered()
{
    AppToolShellFixture fixture;
    QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolStructure>(pdf::PDFDocumentToolRegistry::getTool(StructureGuiHelper::TOOL_ID)));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(StructureGuiHelper::TOOL_ID));
}

void AppToolStructureTest::frameGeometry()
{
    // 302:4072 at 1440 x 960
    using H = StructureGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    // toolbar 302:4078: Steps at 348 (tool-actions 302:4092), its divider, Select all, Clear;
    // the confirmation Optimize Structure with the icon layers (302:4105)
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(H::inWindow(toolbar), QRect(0, 38, 1440, 52));
    // The title "Optimize Structure" has the width of a text: at most 1 px from the 348 of Figma
    QVERIFY2(qAbs(H::inWindow(gui->getStepsMenu()->getButton()).left() - 348) <= 1, qPrintable(QString::number(H::inWindow(gui->getStepsMenu()->getButton()).left())));
    QCOMPARE(gui->getStepsMenu()->getButton()->height(), 36);
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 6 of 6"));
    QCOMPARE(gui->getSelectAllButton()->text(), QStringLiteral("Select all"));
    QCOMPARE(gui->getClearButton()->text(), QStringLiteral("Clear"));
    QVERIFY(H::inWindow(gui->getSelectAllButton()).left() > H::inWindow(gui->getStepsMenu()->getButton()).right());
    QVERIFY(H::inWindow(gui->getClearButton()).left() > H::inWindow(gui->getSelectAllButton()).right());
    QCOMPARE(toolbar->getConfirmText(), QStringLiteral("Optimize Structure"));
    QCOMPARE(gui->getConfirmIconName(), QStringLiteral("layers"));
    QVERIFY(toolbar->getConfirmButton()->isEnabled());
    QCOMPARE(H::inWindow(toolbar->getConfirmButton()).right(), 1427);

    // The body: the thumbnails of the document left, the canvas 814, the right panel 320
    QCOMPARE(tab->getView()->getCurrentPanelId(), QStringLiteral("thumbnails"));
    QVERIFY(!shell->getLeftPanel());
    QCOMPARE(H::inWindow(shell->getRightPanel()), QRect(1120, 90, 320, 842));
    QCOMPARE(tab->getView()->getCanvasArea()->width(), 814);

    // sidebar-right 302:4148: Document at 17 / 14, the card 287 x 126; Steps to run at 180,
    // its card 270; Result at 490, its card 60
    const QPoint panel = H::inWindow(shell->getRightPanel()).topLeft();
    QCOMPARE(H::inWindow(gui->getDocumentHeader()).topLeft() - panel, QPoint(17, 14));
    QCOMPARE(gui->getDocumentHeader()->width(), 287);
    QWidget* factsCard = gui->getDocumentFacts()->parentWidget();
    QCOMPARE(H::inWindow(factsCard).topLeft() - panel, QPoint(17, 38));
    QCOMPARE(factsCard->size(), QSize(287, 126));
    QCOMPARE(H::inWindow(gui->getStepsHeader()).top() - panel.y(), 180);
    QCOMPARE(gui->getStepsHeader()->getHintLabel()->text(), QStringLiteral("6 of 6"));
    QCOMPARE(H::inWindow(gui->getStepList()).top() - panel.y(), 204);
    QCOMPARE(gui->getStepList()->size(), QSize(287, 270));
    QCOMPARE(H::inWindow(gui->getResultHeader()).top() - panel.y(), 490);
    QWidget* resultCard = gui->getResultText()->parentWidget();
    QCOMPARE(H::inWindow(resultCard).top() - panel.y(), 514);
    QCOMPARE(resultCard->size(), QSize(287, 60));
    QCOMPARE(gui->getResultText()->text(), QStringLiteral("Run the tool to see how much the file shrinks. The steps are lossless and can be undone."));
    QVERIFY(!gui->getDoneResultHeader()->isVisible());
    QVERIFY(!gui->getPerformedList()->isVisible());

    // steps 319:7288: the rows of 41, 2 apart, 7 inside of the card
    pdf::PDFTileRow* first = gui->getStepList()->getRow(QStringLiteral("embedSimpleObjects"));
    QCOMPARE(first->getTitle(), QStringLiteral("Embed simple objects"));
    QCOMPARE(first->getDescription(), QStringLiteral("Writes small values in place"));
    QCOMPARE(first->getEndIconName(), QStringLiteral("check"));
    QCOMPARE(first->geometry(), QRect(7, 7, 273, 41));
    QCOMPARE(gui->getStepList()->getRow(QStringLiteral("removeNullObjects"))->y(), 50);

    // chip-lossless 302:4542: 249 x 24 at 283 / 36 of the canvas
    pdf::PDFAppToolCanvasChip* chip = gui->getChip();
    QVERIFY(chip && chip->isVisible());
    QCOMPARE(chip->getText(), QStringLiteral("Lossless · pages look exactly the same"));
    QCOMPARE(chip->getIconName(), QStringLiteral("shield-check"));
    QCOMPARE(chip->height(), 24);
    QVERIFY2(qAbs(chip->width() - 249) <= 2, qPrintable(QString::number(chip->width())));
    QCOMPARE(chip->y(), 36);
    QVERIFY(qAbs(chip->x() - 283) <= 1);
    QVERIFY(chip->testAttribute(Qt::WA_TransparentForMouseEvents));
}

void AppToolStructureTest::documentFactsAndStatus()
{
    // document-facts 302:4549 and the status message 302:4182
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    const QString filePath = H::writeReducible(fixture.path(QStringLiteral("Project_Report.pdf")));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);

    const pdf::PDFDocumentToolStructure::Counts counts = pdf::PDFDocumentToolStructure::getCounts(tab->getDocument()->getDocument().data());
    QVERIFY(counts.objects > 0 && counts.streams >= 3);
    pdf::PDFKeyValueList* facts = gui->getDocumentFacts();
    QCOMPARE(facts->getRowCount(), 4);
    QCOMPARE(facts->getKey(0), QStringLiteral("File size"));
    QCOMPARE(facts->getValue(0), pdf::PDFDocumentToolFormat::formatSize(QFileInfo(filePath).size()));
    QCOMPARE(facts->getKey(1), QStringLiteral("Objects"));
    QCOMPARE(facts->getValue(1), QString::number(counts.objects));
    QCOMPARE(facts->getKey(2), QStringLiteral("Streams"));
    QCOMPARE(facts->getValue(2), QString::number(counts.streams));
    QCOMPARE(facts->getKey(3), QStringLiteral("PDF version"));
    QCOMPARE(facts->getValue(3), QStringLiteral("%1.%2").arg(tab->getDocument()->getDocument()->getInfo()->version.major).arg(tab->getDocument()->getDocument()->getInfo()->version.minor));

    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Optimize Structure · %1 · %2 objects · %3").arg(tab->getDocument()->getTitle()).arg(counts.objects)
                                                                                                    .arg(pdf::PDFDocumentToolFormat::formatSize(QFileInfo(filePath).size())));
}

void AppToolStructureTest::stepsPanel()
{
    // panel-steps 303:4560: 520 x 368 at 348 / 94, the caption, six rows of 46, Restore defaults;
    // 303:4405: a step off - "Steps: 5 of 6", "5 of 6", the row "Will be skipped" with the mark minus
    using H = StructureGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppToolParameterModel* model = shell->getModel();

    pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(gui->getStepsMenu()->getButton());
    QVERIFY(panel);
    AppToolShellFixture::settle();
    QCOMPARE(panel->getPanelRect().size(), QSize(520, 368));
    // At its button (348 of the frame), 4 px below the toolbar (94), inside of the offscreen screen
    const QPoint panelGlobal = panel->mapToGlobal(panel->getPanelRect().topLeft());
    QToolButton* stepsButton = gui->getStepsMenu()->getButton();
    const QRect available = stepsButton->screen()->availableGeometry();
    QCOMPARE(panelGlobal.x(), qMin(stepsButton->mapToGlobal(QPoint(0, 0)).x(), available.right() + 1 - 520));
    QCOMPARE(panelGlobal.y(), shell->getToolbar()->mapToGlobal(QPoint(0, shell->getToolbar()->height())).y() + 4);
    QCOMPARE(panel->getRows().size(), size_t(6));
    QCOMPARE(panel->getRows().front()->height(), 46);
    QCOMPARE(panel->getRows().front()->getTitle(), QStringLiteral("Embed (dereference) simple objects, such as int, bool, real"));
    QCOMPARE(panel->getRows().front()->getDescription(), QStringLiteral("Small values are written in place instead of being referenced"));
    QCOMPARE(panel->getRows()[4]->getTitle(), QStringLiteral("Shrink object storage (squeeze free entries)"));
    QCOMPARE(gui->getRestoreDefaultsLink()->text(), QStringLiteral("Restore defaults"));
    const pdf::PDFLabel* caption = panel->findChild<pdf::PDFLabel*>();
    QVERIFY(caption);

    // A row toggles its step, the panel stays open, one undo step of the tool
    QVERIFY(!model->canUndo());
    QTest::mouseClick(panel->getRows()[4], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(panel->isVisible());
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 5 of 6"));
    QCOMPARE(gui->getStepsHeader()->getHintLabel()->text(), QStringLiteral("5 of 6"));
    pdf::PDFTileRow* shrink = gui->getStepList()->getRow(QStringLiteral("shrinkObjectStorage"));
    QCOMPARE(shrink->getDescription(), QStringLiteral("Will be skipped"));
    QCOMPARE(shrink->getEndIconName(), QStringLiteral("minus"));
    QCOMPARE(shrink->getTone(), pdf::PDFTileRow::Tone::Muted);
    QVERIFY(model->canUndo());
    QVERIFY(!panel->getRows()[4]->isChecked());

    // Restore defaults of the panel: all six on, one undo step
    QTest::mouseClick(gui->getRestoreDefaultsLink(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 6 of 6"));
    QCOMPARE(shrink->getDescription(), QStringLiteral("Renumbers objects without gaps"));
    QCOMPARE(shrink->getEndIconName(), QStringLiteral("check"));
    model->undo();
    AppToolShellFixture::settle();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 5 of 6"));
    panel->close();
    AppToolShellFixture::settle();

    // The run takes the steps of the panel
    pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("5 of 6 run · 1 skipped"));
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolStructureTest::selectAllAndClear()
{
    // Select all and Clear of the toolbar: all the steps as one undo step; nothing to run - the
    // Primary button is disabled and the card Result says why (no frame)
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);

    gui->getClearButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 0 of 6"));
    QCOMPARE(gui->getCheckedStepCount(), 0);
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getResultText()->text(), QStringLiteral("Choose at least one step to run."));
    shell->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);

    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 6 of 6"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    shell->getModel()->setValue(QStringLiteral("mergeIdenticalObjects"), false);
    gui->getSelectAllButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 6 of 6"));
    shell->getModel()->undo();
    QCOMPARE(H::steps(gui), QStringLiteral("Steps: 5 of 6"));
}

void AppToolStructureTest::runShowsResultOfTheDialog()
{
    // 303:4897: the dialog with its facts, Keep preselected (K4: "Keep in This Tab"); the panel
    // beside it shows Result and Steps performed, the actions are disabled
    using H = StructureGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("Project_Report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("shrinkObjectStorage"), false);

    pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Deciding);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);
    QVERIFY(dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Copy));
    QVERIFY(dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Overwrite));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Keep in This Tab"));
    pdf::PDFKeyValueList* facts = dialog->getFacts();
    QCOMPARE(facts->getRowCount(), 3);
    QCOMPARE(facts->getKey(0), QStringLiteral("Size"));
    QVERIFY2(facts->getValue(0).contains(QStringLiteral(" → ")) && facts->getValue(0).endsWith(QStringLiteral("% smaller)")), qPrintable(facts->getValue(0)));
    QCOMPARE(facts->getKey(1), QStringLiteral("Steps"));
    QCOMPARE(facts->getValue(1), QStringLiteral("5 of 6 run · 1 skipped"));
    QCOMPARE(facts->getKey(2), QStringLiteral("Time"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Optimize Structure finished · the result is waiting for your decision"));

    // The panel: Result 303:5001 and Steps performed 303:5370 with Copy report
    QVERIFY(gui->isShowingResult());
    QVERIFY(!gui->getDocumentHeader()->isVisible());
    QVERIFY(!gui->getStepList()->isVisible());
    QVERIFY(gui->getDoneResultHeader()->isVisible());
    const QPoint panel = H::inWindow(shell->getRightPanel()).topLeft();
    QCOMPARE(H::inWindow(gui->getDoneResultHeader()).topLeft() - panel, QPoint(17, 14));
    pdf::PDFKeyValueList* rows = gui->getResultRows();
    QCOMPARE(rows->getRowCount(), 5);
    const QStringList keys = { QStringLiteral("Size before"), QStringLiteral("Size after"), QStringLiteral("Saved"), QStringLiteral("Objects"), QStringLiteral("Time") };
    for (int i = 0; i < 5; ++i)
    {
        QCOMPARE(rows->getKey(i), keys[i]);
    }
    QCOMPARE(rows->parentWidget()->height(), 154);
    QCOMPARE(H::inWindow(gui->getPerformedHeader()).top() - panel.y(), 208);
    QCOMPARE(gui->getCopyReportLink()->text(), QStringLiteral("Copy report"));
    QCOMPARE(H::inWindow(gui->getCopyReportLink()).right(), H::inWindow(gui->getPerformedHeader()).right());

    // 319:7398: check-circle accent/green/icon and the text text/secondary; the skipped step muted
    pdf::PDFTileRow* embed = gui->getPerformedList()->getRow(QStringLiteral("embedSimpleObjects"));
    QCOMPARE(embed->getEndIconName(), QStringLiteral("check-circle"));
    QCOMPARE(embed->getEndIconColor(), pdf::PDFThemeColors::AccentGreenIcon);
    QCOMPARE(embed->getDescriptionColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::TextSecondary));
    QVERIFY(embed->getDescription().endsWith(QStringLiteral("embedded")));
    pdf::PDFTileRow* shrink = gui->getPerformedList()->getRow(QStringLiteral("shrinkObjectStorage"));
    QCOMPARE(shrink->getDescription(), QStringLiteral("Skipped – not selected"));
    QCOMPARE(shrink->getEndIconName(), QStringLiteral("minus"));
    QCOMPARE(shrink->getTone(), pdf::PDFTileRow::Tone::Muted);
    QVERIFY(!shrink->getDescriptionColor().has_value());

    // The actions below the dialog (303:4897), the chip stays over the canvas
    QVERIFY(!shell->getToolActions()->isEnabled());
    QVERIFY(gui->getChip()->isVisible());

    // Discard: the tool ends, the document is unchanged
    dialog->reject();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
}

void AppToolStructureTest::copyReportInTheDialog()
{
    // The modal dialog keeps the link of the panel out of reach: the dialog has its own link (a
    // deviation without a frame); the report is the one of the core
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);

    // The panel is below a modal dialog
    QCOMPARE(QApplication::activeModalWidget(), dialog);
    QVERIFY(!dialog->isAncestorOf(gui->getCopyReportLink()));

    QGuiApplication::clipboard()->clear();
    pdf::PDFTextLink* link = dialog->getReportLink();
    QVERIFY(link && link->isVisible());
    QCOMPARE(link->text(), QStringLiteral("Copy report"));
    QTest::mouseClick(link, Qt::LeftButton);
    AppToolShellFixture::settle();
    const QString report = QGuiApplication::clipboard()->text();
    QVERIFY2(report.startsWith(QStringLiteral("Optimize Structure\nSize before: ")), qPrintable(report));
    QVERIFY(report.contains(QStringLiteral("\nSteps performed\nEmbed simple objects: ")));
    QCOMPARE(link->text(), QStringLiteral("Report copied"));

    // The head keeps its height (312:7535: 36)
    QCOMPARE(dialog->findChild<QWidget*>(QStringLiteral("toolResultHead"))->height(), 36);

    // The link of the panel copies the same report
    QGuiApplication::clipboard()->clear();
    gui->getCopyReportLink()->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), report);
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolStructureTest::keepIsOneUndoStep()
{
    // Keep in This Tab: the result in the tab as one undo step, fewer objects, the file unchanged
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    const QString filePath = H::writeReducible(fixture.path(QStringLiteral("report.pdf")));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    file.close();

    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    const qint64 objectsBefore = pdf::PDFDocumentToolStructure::getCounts(tab->getDocument()->getDocument().data()).objects;
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();

    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 1);
    QVERIFY(tab->getDocument()->isModified());
    QVERIFY(pdf::PDFDocumentToolStructure::getCounts(tab->getDocument()->getDocument().data()).objects < objectsBefore);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
}

void AppToolStructureTest::nothingToDoInThePanel()
{
    // No frame: a file, which does not get smaller, says it in the card Result with its sizes,
    // no dialog, the tool stays; a change of the steps gives the first text back
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeOptimized(fixture.folder(), fixture.path(QStringLiteral("optimized.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getResultText()->text().startsWith(QStringLiteral("File cannot be reduced with these steps: ")); }, 60000));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(!shell->getResultDialog());
    QVERIFY2(gui->getResultText()->text().contains(QStringLiteral(" → ")), qPrintable(gui->getResultText()->text()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);

    shell->getModel()->setValue(QStringLiteral("shrinkObjectStorage"), false);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getResultText()->text(), QStringLiteral("Run the tool to see how much the file shrinks. The steps are lossless and can be undone."));
}

void AppToolStructureTest::signedDocumentCopyOnly()
{
    // O31: a signed document with a file - only Save as a copy, written whole
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    QVERIFY(shell);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() || shell->getState() == PDFDocumentToolShell::State::Editing; }, 60000));
    AppToolShellFixture::settle();
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    if (!dialog)
    {
        QSKIP("The signed sample does not get smaller by the steps");
    }
    QVERIFY(!dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Keep));
    QVERIFY(!dialog->getOption(pdf::PDFAppToolResultDialog::Choice::Overwrite));
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Copy);
    QVERIFY(dialog->getSignedWarning());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolStructureTest::leakAndThemes_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("ratio");
    QTest::addColumn<bool>("rightToLeft");
    for (const char* theme : { "light", "dark" })
    {
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QTest::addRow("%s-%.1f", theme, ratio) << QString::fromLatin1(theme) << ratio << false;
        }
        QTest::addRow("%s-rtl", theme) << QString::fromLatin1(theme) << 1.0 << true;
    }
}

void AppToolStructureTest::leakAndThemes()
{
    using H = StructureGuiHelper;
    QFETCH(QString, theme);
    QFETCH(qreal, ratio);
    QFETCH(bool, rightToLeft);

    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFThemeManager::getInstance()->setTheme(theme);
    if (rightToLeft)
    {
        fixture.window->setLayoutDirection(Qt::RightToLeft);
    }
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getRightPanel(), ratio)), 0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getToolbar(), ratio)), 0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(gui->getChip(), ratio)), 0);
    pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(gui->getStepsMenu()->getButton());
    QVERIFY(panel);
    AppToolShellFixture::settle();
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(panel, ratio)), 0);
    panel->close();

    // The chip: the fill icon/strong of the theme
    const QImage chip = AppToolShellFixture::render(gui->getChip(), ratio);
    const QColor fill = pdf::PDFStylePainter::color(pdf::PDFThemeColors::IconStrong);
    const QColor middle = chip.pixelColor(chip.width() / 2, int(2 * ratio));
    QVERIFY2(qAbs(middle.red() - fill.red()) <= 2 && qAbs(middle.green() - fill.green()) <= 2 && qAbs(middle.blue() - fill.blue()) <= 2, qPrintable(middle.name()));

    pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getRightPanel(), ratio)), 0);
    dialog->reject();
    AppToolShellFixture::settle();
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolStructureTest::chipRightToLeft()
{
    // The icon of the chip at the leading side: 10 px from the left, right-to-left from the right
    using H = StructureGuiHelper;
    const auto lightColumns = [](const QImage& image)
    {
        // The icon and the text are bg/surface on the fill icon/strong: the columns of light pixels
        // (the round ends, where the canvas shows through, are skipped)
        std::vector<int> columns;
        for (int x = 6; x < image.width() - 6; ++x)
        {
            for (int y = 4; y < image.height() - 4; ++y)
            {
                if (qGray(image.pixel(x, y)) > 160)
                {
                    columns.push_back(x);
                    break;
                }
            }
        }
        return columns;
    };

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFAppToolGUIStructure* gui = H::gui(fixture.openTool(tab, H::TOOL_ID));
    QVERIFY(gui);
    AppToolShellFixture::settle();

    pdf::PDFAppToolCanvasChip* chip = gui->getChip();
    const std::vector<int> leftToRight = lightColumns(AppToolShellFixture::render(chip, 1.0));
    QVERIFY(!leftToRight.empty());
    QVERIFY2(leftToRight.front() >= 10 && leftToRight.front() <= 13, qPrintable(QString::number(leftToRight.front())));

    chip->setLayoutDirection(Qt::RightToLeft);
    const std::vector<int> rightToLeft = lightColumns(AppToolShellFixture::render(chip, 1.0));
    QVERIFY(!rightToLeft.empty());
    QVERIFY2(rightToLeft.back() <= chip->width() - 11 && rightToLeft.back() >= chip->width() - 14, qPrintable(QString::number(rightToLeft.back())));
    QVERIFY(rightToLeft.front() > leftToRight.front());
}

void AppToolStructureTest::minimalWindow()
{
    // 720 x 480: the actions do not overlap the Primary button, the chip stays inside of the canvas
    using H = StructureGuiHelper;
    AppToolShellFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar->width() <= fixture.window->width());
    QVERIFY(toolbar->getConfirmButton()->isVisible());
    QVERIFY(H::inWindow(toolbar->getConfirmButton()).right() < fixture.window->width());
    if (gui->getClearButton()->isVisible())
    {
        QVERIFY(H::inWindow(gui->getClearButton()).right() < H::inWindow(toolbar->getConfirmButton()).left());
    }
    const QWidget* canvas = tab->getView()->getCanvasArea();
    pdf::PDFAppToolCanvasChip* chip = gui->getChip();
    QVERIFY(chip->x() >= 0);
    QVERIFY(chip->geometry().right() < canvas->width());
}

void AppToolStructureTest::languageChange()
{
    // The parts are made again with the translated texts; the steps live in the model
    using H = StructureGuiHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIStructure* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("shrinkObjectStorage"), false);
    QPointer<pdf::PDFAppToolStepList> oldList = gui->getStepList();

    pdftest::StructureTestTranslator translator;
    QApplication::installTranslator(&translator);
    QVERIFY(AppToolShellFixture::waitFor([gui, oldList]() { return gui->getStepList() != oldList.data(); }));
    AppToolShellFixture::settle();

    QCOMPARE(gui->getStepsHeader()->getTitleLabel()->text(), QStringLiteral("[T] Steps to run"));
    QCOMPARE(H::steps(gui), QStringLiteral("[T] Steps: 5 of 6"));
    QCOMPARE(gui->getStepList()->getRow(QStringLiteral("embedSimpleObjects"))->getTitle(), QStringLiteral("[T] Embed simple objects"));
    QCOMPARE(gui->getStepList()->getRow(QStringLiteral("shrinkObjectStorage"))->getDescription(), QStringLiteral("[T] Will be skipped"));
    QCOMPARE(gui->getChip()->getText(), QStringLiteral("[T] Lossless · pages look exactly the same"));
    QApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
}

void AppToolStructureTest::deletedInEachState()
{
    // The tab or the window deleted with the panel Steps open, with the result dialog open, while
    // the tool runs: nothing is left, the clipboard of a late click is not touched
    using H = StructureGuiHelper;
    for (int state = 0; state < 3; ++state)
    {
        for (const bool isWindow : { false, true })
        {
            AppToolShellFixture fixture;
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
            QVERIFY(tab);
            QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
            QVERIFY(shell);
            QPointer<PDFAppToolGUIStructure> gui = H::gui(shell);
            QPointer<QWidget> popup;
            switch (state)
            {
                case 0:
                    popup = shell->getToolbar()->openPanel(gui->getStepsMenu()->getButton());
                    break;
                case 1:
                    popup = H::confirmAndWait(shell);
                    break;
                default:
                    shell->confirm();
                    break;
            }
            QPointer<pdf::PDFAppToolCanvasChip> chip = gui->getChip();
            QVERIFY(chip);

            if (isWindow)
            {
                delete fixture.window.data();
            }
            else
            {
                delete tab;
            }
            AppToolShellFixture::settle();
            QVERIFY(!shell);
            QVERIFY(!gui);
            QVERIFY(!chip);
            QVERIFY(!popup || !popup->isVisible());
        }
    }
}

void AppToolStructureTest::mcpAction()
{
    // newapp_mcp.md 6: optimize.structure for an assistant - keep is one undo step, the steps
    // of the call; at least one step; a signed document only as a copy (O31)
    using H = StructureGuiHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(H::writeReducible(fixture.path(QStringLiteral("report.pdf"))));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());

    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("structure") },
                                                                                                             { QStringLiteral("result"), QStringLiteral("keep") },
                                                                                                             { QStringLiteral("structure"), QJsonObject({ { QStringLiteral("shrinkObjectStorage"), false } }) } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject structure = result->toQJson().value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("structure")).toObject();
    QCOMPARE(structure.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(structure.value(QStringLiteral("data")).toObject().value(QStringLiteral("stepsRun")).toInt(), 5);
    QCOMPARE(structure.value(QStringLiteral("steps")).toArray().size(), 6);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 1);

    // All six off: refused by the tool, nothing changes
    QJsonObject none;
    for (const pdf::PDFDocumentToolStructure::Step& step : pdf::PDFDocumentToolStructure::getSteps())
    {
        none.insert(QString::fromLatin1(step.parameter), false);
    }
    result = fixture.callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("structure") }, { QStringLiteral("result"), QStringLiteral("keep") }, { QStringLiteral("structure"), none } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("at least one step")), qPrintable(result->getFirstText()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 1);

    // A signed document: keep is refused before a run
    pdf::PDFAppDocumentTab* signedTab = fixture.open(pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(signedTab);
    QTRY_VERIFY(signedTab->getDocument()->isReady());
    result = fixture.callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("structure") }, { QStringLiteral("documentId"), signedTab->getDocument()->getId() },
                                                                        { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("save_copy")), qPrintable(result->getFirstText()));
}

void AppToolStructureTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS: the frames by their ids
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = StructureGuiHelper;
    for (const char* theme : { "light", "dark" })
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme));
        const QString suffix = QString::fromLatin1(theme) == QLatin1String("dark") ? QStringLiteral("-dark") : QString();
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeReducible(fixture.path(QStringLiteral("Project_Report.pdf"))));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
        PDFAppToolGUIStructure* gui = H::gui(shell);
        QVERIFY(gui);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("optimize-structure-302-4072") + suffix);

        shell->getModel()->setValue(QStringLiteral("shrinkObjectStorage"), false);
        pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(gui->getStepsMenu()->getButton());
        QVERIFY(panel);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("optimize-structure-steps-303-4405") + suffix);
        AppToolShellFixture::screenshotDialog(panel, QStringLiteral("optimize-structure-panel-steps-303-4560") + suffix);
        panel->close();
        AppToolShellFixture::settle();

        pdf::PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
        QVERIFY(dialog);
        fixture.screenshot(QStringLiteral("optimize-structure-done-303-4897") + suffix);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("optimize-structure-dialog-312-7534") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();
    }
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

QTEST_MAIN(AppToolStructureTest)

#include "tst_apptoolstructuretest.moc"
