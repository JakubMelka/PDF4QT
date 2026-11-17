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

// The GUI of Output Preview (step 7 of phase 2, plan 2.3, 4.6, 8; package P4): the frames 474:8386 …
// 476:11703 - the picture rendered by one job, the modes from the stored result (no new rendering), the
// sections of the right panel of each mode (the row Overprint, K7.12), the inks unchecked into the parameter,
// the legend of the map Ink Coverage (the scale of the core) and the presets of the paper.

#include "apptoolshellfixture.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfappoutputpreviewcanvas.h"
#include "pdfapptoolgui_outputpreview.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QButtonGroup>
#include <QHeaderView>
#include <QPainter>
#include <QScopeGuard>
#include <QToolButton>
#include <QStandardItemModel>
#include <QtTest>

using pdftest::AppToolShellFixture;
using Mode = pdf::PDFOutputPreviewAnalyzer::Mode;

/// Helpers of the test
class AppToolOutputPreviewTestHelper
{
public:
    AppToolOutputPreviewTestHelper() = delete;

    static constexpr const char* TOOL_ID = "review.output-preview";

    static pdf::PDFAppToolGUIOutputPreview* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<pdf::PDFAppToolGUIOutputPreview*>(shell->getGUI()) : nullptr;
    }

    /// The tool over a document of three pages, its first picture rendered
    static pdf::PDFDocumentToolShell* openRendered(AppToolShellFixture& fixture)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Brochure_Print.pdf"), 3));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(TOOL_ID));
        pdf::PDFAppToolGUIOutputPreview* current = gui(shell);
        // Nothing more pending (fix wave of step 7: a rendering scheduled after the first picture - its timer is
        // not isRendering - would replace the result, which the test keeps)
        if (!current || !AppToolShellFixture::waitFor([current]() { return current->getResult() && !current->isRenderingPending(); }))
        {
            return nullptr;
        }
        return shell;
    }

    static bool isSectionShown(pdf::PDFAppToolGUIOutputPreview* gui, const QString& name)
    {
        QWidget* section = gui->getSection(name);
        return section && section->isVisibleTo(section->parentWidget());
    }
};

class AppToolOutputPreviewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void legendOfTheScale();
    void paperPresets();
    void renderAndModes();
    void uncheckedInkIsParameter();
    void imageRectChangedIsQueued();
    void accessibleInks();
    void limitRowsAndGroupBands();
    void screenshots();
};

void AppToolOutputPreviewTest::initTestCase()
{
    // The geometry of the frames is the geometry of the Fluent style (review RS7FIGMA)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolOutputPreviewTest::legendOfTheScale()
{
    // 474:10536: the scale of PDFOutputPreviewAnalyzer::createImage - from no ink to the highest coverage of the
    // page, at least 100 % - in five values from the top, the value at the cursor
    pdf::PDFAppOutputPreviewLegend legend(nullptr);
    const QLocale locale = pdf::PDFAppLocale::getUiLocale();
    legend.setValues(312.0, 248.0);
    QCOMPARE(legend.getScaleMaximum(), 312.0);
    const double expected[] = { 312.0, 234.0, 156.0, 78.0, 0.0 };
    for (int i = 0; i < 5; ++i)
    {
        QCOMPARE(legend.getLabelText(i), pdf::PDFAppToolGUIOutputPreview::tr("%1 %", "A percentage").arg(locale.toString(expected[i], 'f', 2)));
    }
    QCOMPARE(legend.getMarkerText(), pdf::PDFAppOutputPreviewLegend::tr("%1 at the cursor").arg(pdf::PDFAppToolGUIOutputPreview::tr("%1 %", "A percentage").arg(locale.toString(248.0, 'f', 2))));

    // A light page: the scale still ends at 100 %; no cursor, no marker
    legend.setValues(40.0, -1.0);
    QCOMPARE(legend.getScaleMaximum(), 100.0);
    QCOMPARE(legend.getLabelText(1), pdf::PDFAppToolGUIOutputPreview::tr("%1 %", "A percentage").arg(locale.toString(75.0, 'f', 2)));
    QVERIFY(legend.getMarkerText().isEmpty());
}

void AppToolOutputPreviewTest::paperPresets()
{
    // 476:9987: White, Cream, Gray and a custom color (its hexadecimal value)
    pdf::PDFAppPaperColorField field(nullptr);
    const auto presets = pdf::PDFAppPaperColorField::getPresets();
    QCOMPARE(presets.size(), size_t(3));
    QCOMPARE(pdf::PDFAppPaperColorField::getColorName(QColor(Qt::white)), pdf::PDFAppPaperColorField::tr("White"));
    QCOMPARE(pdf::PDFAppPaperColorField::getColorName(QColor(0x12, 0x34, 0x56)), QStringLiteral("#123456"));

    field.setColor(presets[1].first);
    QCOMPARE(field.getNameLabel()->text(), pdf::PDFAppPaperColorField::tr("Cream"));
    QAbstractButton* checked = field.getSwatchGroup()->checkedButton();
    QVERIFY(checked);
    QCOMPARE(qobject_cast<pdf::PDFColorSwatch*>(checked)->getColor(), presets[1].first);
}

void AppToolOutputPreviewTest::renderAndModes()
{
    // 474:8386: the separations of the first page with the inks of the page; a mode is a picture of the stored
    // result (no new rendering); the right panel shows the section of the mode
    using H = AppToolOutputPreviewTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openRendered(fixture);
    pdf::PDFAppToolGUIOutputPreview* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isVisible());
    // 474:8386: only Close (plan 2.3), never Cancel (RS7FIGMA)
    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), pdf::PDFAppToolGUIOutputPreview::tr("Close"));
    const pdf::PDFOutputPreviewResult* result = gui->getResult();
    QVERIFY(result->isRendered());
    QCOMPARE(result->pageIndex, pdf::PDFInteger(0));
    QVERIFY(!gui->getCanvas()->getImage().isNull());
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), pdf::PDFAppToolGUIOutputPreview::tr("Page %1 · %2").arg(QStringLiteral("1"), pdf::PDFAppToolGUIOutputPreview::tr("Separations")));
    QVERIFY(H::isSectionShown(gui, QStringLiteral("cursor")));
    QVERIFY(H::isSectionShown(gui, QStringLiteral("preview")));
    QVERIFY(!H::isSectionShown(gui, QStringLiteral("coverage")));

    // K7.12: overprint is simulated (as printing)
    QCOMPARE(gui->getPreviewRows()->getKey(1), pdf::PDFAppToolGUIOutputPreview::tr("Overprint"));
    QCOMPARE(gui->getPreviewRows()->getValue(1), pdf::PDFAppToolGUIOutputPreview::tr("Simulated"));

    // The process inks of the page in the group with its check box (K7.18)
    QVERIFY(gui->getInkModel()->rowCount() >= 1);
    QStandardItem* processGroup = gui->getInkModel()->item(0);
    QCOMPARE(processGroup->text(), pdf::PDFAppToolGUIOutputPreview::tr("Process Inks"));
    QCOMPARE(processGroup->rowCount(), 4);
    QCOMPARE(processGroup->checkState(), Qt::Checked);

    // ink-table 474:8521 (RS7FIGMA): the heads "At cursor" and "Page mm²" and the names of the inks are shown
    // whole (no "At c…", "Mage…"): every section is at least as wide as its head, the name (column 0) takes the
    // rest of the table, every ink row has the room of its check box, swatch and name
    AppToolShellFixture::settle();
    QHeaderView* header = gui->getInkView()->header();
    QCOMPARE(header->count(), 3);
    for (int column = 1; column < 3; ++column)
    {
        QVERIFY2(header->sectionSize(column) >= header->sectionSizeHint(column), qPrintable(QString::number(column)));
    }
    QCOMPARE(header->sectionSize(0) + header->sectionSize(1) + header->sectionSize(2), gui->getInkView()->viewport()->width());
    for (int row = 0; row < processGroup->rowCount(); ++row)
    {
        const QModelIndex index = processGroup->child(row)->index();
        QVERIFY2(gui->getInkView()->visualRect(index).width() >= gui->getInkView()->sizeHintForIndex(index).width(), qPrintable(processGroup->child(row)->text()));
    }

    // Coverage Warning: the same result, the section of the limit
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("coverage-warning"));
    AppToolShellFixture::settle();
    QVERIFY(!gui->isRenderingPending());
    QCOMPARE(gui->getResult(), result);
    QVERIFY(H::isSectionShown(gui, QStringLiteral("coverage")));
    QVERIFY(!H::isSectionShown(gui, QStringLiteral("preview")));

    // 474:9818 (RS7FIGMA): the limit in whole percent with its unit ("300 %", not "300.00"); the alarm color is
    // the swatch Red of the row, named "Red" (not a custom "#FF0000")
    {
        QWidget* section = gui->getSection(QStringLiteral("coverage"));
        pdf::PDFDoubleSpinBox* limit = section->findChild<pdf::PDFDoubleSpinBox*>();
        QVERIFY(limit);
        QCOMPARE(limit->decimals(), 0);
        QCOMPARE(limit->text(), QStringLiteral("300"));
        QCOMPARE(limit->getDisplaySuffix(), pdf::PDFAppToolGUIOutputPreview::tr("%", "Unit of a limit in percent"));
        QWidget* alarm = section->findChild<QWidget*>(QStringLiteral("outputPreviewCoverageAlarm"));
        QVERIFY(alarm);
        QLabel* alarmName = alarm->findChild<QLabel*>(QStringLiteral("proofColorName"));
        QVERIFY(alarmName);
        QCOMPARE(alarmName->text(), pdf::PDFColorSwatchRow::tr("Red"));
    }

    // The channels: no Under the cursor (474:10896)
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("shape-channel"));
    AppToolShellFixture::settle();
    QVERIFY(!gui->isRenderingPending());
    QCOMPARE(gui->getResult(), result);
    QVERIFY(H::isSectionShown(gui, QStringLiteral("channel")));
    QVERIFY(!H::isSectionShown(gui, QStringLiteral("cursor")));

    // Another page renders again
    QTest::keyClick(gui->getCanvas(), Qt::Key_PageDown);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getResult() && gui->getResult()->pageIndex == 1 && !gui->isRendering(); }));
}

void AppToolOutputPreviewTest::uncheckedInkIsParameter()
{
    // 474:9162: an unchecked ink is the parameter inactiveInks (one undo step) and the plates are rendered
    // again without it; the group is Mixed
    using H = AppToolOutputPreviewTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openRendered(fixture);
    pdf::PDFAppToolGUIOutputPreview* gui = H::gui(shell);
    QVERIFY(gui);
    QStandardItem* processGroup = gui->getInkModel()->item(0);
    QVERIFY(processGroup && processGroup->rowCount() == 4);
    const QString name = processGroup->child(0)->text();
    const pdf::PDFOutputPreviewResult* before = gui->getResult();
    processGroup->child(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(processGroup->checkState(), Qt::PartiallyChecked);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getInactiveInks(), QStringList({ name }));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("inactiveInks")).toString(), name);

    // The plates are rendered again (a new result) and the ink stays unchecked
    QVERIFY(AppToolShellFixture::waitFor([gui, before]() { return gui->getResult() != before && !gui->isRendering(); }));
    QStandardItem* group = gui->getInkModel()->item(0);
    QCOMPARE(group->checkState(), Qt::PartiallyChecked);
    QCOMPARE(group->child(0)->checkState(), Qt::Unchecked);

    // The group checks all its inks again
    group->setCheckState(Qt::Checked);
    AppToolShellFixture::settle();
    QVERIFY(gui->getInactiveInks().isEmpty());
    QCOMPARE(gui->getInkModel()->item(0)->child(0)->checkState(), Qt::Checked);
}

void AppToolOutputPreviewTest::imageRectChangedIsQueued()
{
    // RS7APP L9: the picture emits imageRectChanged from its resizeEvent, inside of the layout of Qt, which goes on
    // with the picture; the canvas passes it on from the event loop (a slot of the status may delete the canvas
    // with the picture) - never during the resize
    pdf::PDFAppOutputPreviewCanvas canvas(nullptr);
    canvas.resize(400, 400);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QCoreApplication::processEvents();
    QSignalSpy changed(&canvas, &pdf::PDFAppOutputPreviewCanvas::imageRectChanged);

    canvas.getPicture()->resize(canvas.getPicture()->width() - 20, canvas.getPicture()->height() - 20);
    QCOMPARE(changed.count(), 0);
    QTRY_COMPARE(changed.count(), 1);
}

void AppToolOutputPreviewTest::accessibleInks()
{
    // RS7APP L8: a screen reader says what the swatch and the group of an ink show - its kind (474:8521)
    using H = AppToolOutputPreviewTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openRendered(fixture);
    pdf::PDFAppToolGUIOutputPreview* gui = H::gui(shell);
    QVERIFY(gui);
    QStandardItem* processGroup = gui->getInkModel()->item(0);
    QVERIFY(processGroup && processGroup->rowCount() == 4);
    for (int row = 0; row < processGroup->rowCount(); ++row)
    {
        QCOMPARE(processGroup->child(row)->data(Qt::AccessibleDescriptionRole).toString(), pdf::PDFAppToolGUIOutputPreview::tr("Process ink"));
    }
}

void AppToolOutputPreviewTest::limitRowsAndGroupBands()
{
    // RS7FIGMA, 474:10097: the result of the limit - a band bg/subtle with the dot of the green accent before "Within
    // the limit" and the dot of the alarm color before "Over the limit"; 474:9958: the rows of the ink groups lie on a
    // band bg/subtle
    using H = AppToolOutputPreviewTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openRendered(fixture);
    pdf::PDFAppToolGUIOutputPreview* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("coverage-warning"));
    AppToolShellFixture::settle();

    pdf::PDFAppOutputPreviewLimitRows* rows = gui->getCoverageRows();
    QVERIFY(rows && !rows->isEmpty() && rows->isVisible());
    QVERIFY(!rows->getWithinValue().isEmpty() && !rows->getOverValue().isEmpty());
    QCOMPARE(rows->height(), 2 * pdf::PDFAppOutputPreviewLimitRows::PADDING_Y + 2 * pdf::PDFAppOutputPreviewLimitRows::ROW_HEIGHT + pdf::PDFAppOutputPreviewLimitRows::ROW_GAP);
    QVERIFY(rows->accessibleDescription().contains(pdf::PDFAppOutputPreviewLimitRows::getOverTitle()));
    const QImage image = rows->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal ratio = image.devicePixelRatio();
    const auto pixel = [&image, ratio](const QPoint& point) { return QColor(image.pixel(qRound(point.x() * ratio), qRound(point.y() * ratio))); };
    QCOMPARE(pixel(QPoint(3, rows->height() / 2)), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSubtle));
    QCOMPARE(pixel(rows->getDotRect(0).center()), pdf::PDFStylePainter::color(pdf::PDFThemeColors::AccentGreenIcon));
    QCOMPARE(pixel(rows->getDotRect(1).center()), rows->getAlarmColor());

    // The group Process Inks on its band, an ink on the card
    pdf::PDFAppInkTreeView* inkView = qobject_cast<pdf::PDFAppInkTreeView*>(gui->getInkView());
    QVERIFY(inkView);
    const QModelIndex group = gui->getInkModel()->item(0)->index();
    const QModelIndex ink = gui->getInkModel()->item(0)->child(0)->index();
    QVERIFY(inkView->isGroupRow(group));
    QVERIFY(!inkView->isGroupRow(ink));
    // The card with the table (the viewport alone has no background of its own)
    QWidget* card = inkView->parentWidget();
    QVERIFY(card);
    const QImage table = card->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal tableRatio = table.devicePixelRatio();
    const QRect groupRect = inkView->visualRect(group);
    const QRect inkRect = inkView->visualRect(ink);
    const QPoint groupPoint = inkView->viewport()->mapTo(card, QPoint(groupRect.right() - 2, groupRect.center().y()));
    const QPoint inkPoint = inkView->viewport()->mapTo(card, QPoint(inkRect.right() - 2, inkRect.center().y()));
    const QColor groupColor(table.pixel(qRound(groupPoint.x() * tableRatio), qRound(groupPoint.y() * tableRatio)));
    const QColor inkColor(table.pixel(qRound(inkPoint.x() * tableRatio), qRound(inkPoint.y() * tableRatio)));
    QCOMPARE(groupColor, pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSubtle));
    QVERIFY2(inkColor != pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSubtle), qPrintable(inkColor.name()));
    QVERIFY2(inkColor != pdf::PDFThemeColors::getLeakColor(), qPrintable(inkColor.name()));
}

void AppToolOutputPreviewTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Output Preview under the ids of their frames
    // (474:8386 … 476:10352) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma; no pixel
    // of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolOutputPreviewTestHelper;
    const QLocale locale = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    const QScopeGuard localeGuard([locale]() { pdf::PDFAppLocale::setUiLocale(locale); });
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
        pdf::PDFAppToolGUIOutputPreview* shownGui = nullptr;
        const auto shot = [&fixture, &suffix, &leaks, &shownGui](const QString& frame)
        {
            AppToolShellFixture::settle();
            QImage image = fixture.screenshot(frame + suffix);

            // The picture of the page is the document (the scale of Ink Coverage runs through blue and violet),
            // not a color of the user interface: it is left out of the count
            pdf::PDFAppOutputPreviewPicture* picture = shownGui && shownGui->getCanvas() ? shownGui->getCanvas()->getPicture() : nullptr;
            if (picture && !image.isNull())
            {
                QPainter painter(&image);
                painter.fillRect(QRect(picture->mapTo(fixture.window, picture->getImageRect().topLeft()), picture->getImageRect().size()), Qt::white);
            }
            if (AppToolShellFixture::countLeakPixels(image) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto setMode = [](pdf::PDFDocumentToolShell* shell, pdf::PDFAppToolGUIOutputPreview* gui, const char* mode)
        {
            shell->getModel()->setValue(QStringLiteral("mode"), QString::fromLatin1(mode));
            AppToolShellFixture::waitFor([gui]() { return !gui->isRenderingPending() && !gui->isRendering(); });
        };

        pdf::PDFDocumentToolShell* shell = H::openRendered(fixture);
        pdf::PDFAppToolGUIOutputPreview* gui = H::gui(shell);
        QVERIFY(gui);
        shownGui = gui;
        shot(QStringLiteral("474-8386"));

        if (pdf::PDFAppToolPanelPopup* panel = gui->getModePanel())
        {
            QTest::mouseClick(panel->getButton(), Qt::LeftButton);
            shot(QStringLiteral("474-8728"));
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                popup->close();
            }
            AppToolShellFixture::settle();
        }

        setMode(shell, gui, "coverage-warning");
        shot(QStringLiteral("474-9818"));
        setMode(shell, gui, "rich-black-warning");
        shot(QStringLiteral("474-10178"));
        setMode(shell, gui, "ink-coverage");
        shot(QStringLiteral("474-10536"));
        setMode(shell, gui, "shape-channel");
        shot(QStringLiteral("474-10896"));
        setMode(shell, gui, "opacity-channel");
        shot(QStringLiteral("476-9651"));
        setMode(shell, gui, "separations");

        gui->getPaperButton()->click();
        AppToolShellFixture::waitFor([gui]() { return !gui->isRenderingPending() && !gui->isRendering(); });
        shot(QStringLiteral("476-9987"));
        gui->getPaperButton()->click();
        gui->getSimulateButton()->click();
        AppToolShellFixture::waitFor([gui]() { return !gui->isRenderingPending() && !gui->isRendering(); });
        shot(QStringLiteral("476-10352"));
        gui->getSimulateButton()->click();
        AppToolShellFixture::waitFor([gui]() { return !gui->isRenderingPending() && !gui->isRendering(); });

        QStandardItem* processGroup = gui->getInkModel()->item(0);
        for (int i = 1; processGroup && i < processGroup->rowCount(); ++i)
        {
            processGroup->child(i)->setCheckState(Qt::Unchecked);
        }
        AppToolShellFixture::waitFor([gui]() { return !gui->isRenderingPending() && !gui->isRendering(); });
        shot(QStringLiteral("474-9162"));

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolOutputPreviewTest)

#include "tst_apptooloutputpreviewtest.moc"
