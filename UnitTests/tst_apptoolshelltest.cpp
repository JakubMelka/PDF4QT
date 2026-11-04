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
#include "xfatestsamples.h"

#include "pdfappautorefresh.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumenttoolspanel.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfapptooloutofmemorydialog.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptooltoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpathrules.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptoolresultwriter.h"
#include "pdfappundostack.h"
#include "pdfappcommand.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfexecutionpolicy.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftoolparameterrepository.h"
#include "pdftoolrepository.h"

#include "pdfdrawwidget.h"
#include "pdfdocumentbuilder.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAccessible>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QThread>
#include <QThreadPool>
#include <QTest>
#include <QTranslator>
#include <QtTest>

Q_DECLARE_METATYPE(pdf::PDFAppToolResultOutcome)
Q_DECLARE_METATYPE(pdf::PDFAppToolResultWriter::Result)

using pdf::PDFAppToolResultDialog;
using pdf::PDFAppToolResultOutcome;
using pdf::PDFDocumentToolRunner;
using pdftest::AppToolShellFixture;
using pdftest::ShellTestGUI;
using pdftest::ShellTestTool;
using pdftest::TestDocumentTool;
using Choice = PDFAppToolResultDialog::Choice;
using Action = PDFAppToolResultOutcome::Action;
using RunnerState = PDFDocumentToolRunner::State;

/// Tool ids of the catalog with the columns of the kinds the tests need: Keep
/// preselected and IncrementalSave, Copy preselected and LostCopyOnly (tools.md 9.3 point
/// 11, plan chapter 4), a new document, an export, an analysis
#define TOOL_ROTATE QStringLiteral("pages.rotate")
#define TOOL_COMPRESS QStringLiteral("optimize.compress")
#define TOOL_MERGE QStringLiteral("pages.merge")
#define TOOL_EXTRACT QStringLiteral("convert.extract-text")
#define TOOL_ANALYSIS QStringLiteral("review.statistics")

/// Test translator: every text of the dialog gets a prefix
class ToolShellTestTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return QLatin1String(context) == QLatin1String("pdf::PDFAppToolResultDialog") ? QStringLiteral("XX ") + QString::fromUtf8(sourceText) : QString();
    }
};

class ToolShellTestHelper
{
public:
    static PDFAppToolResultOutcome firstOutcome(const QSignalSpy& spy)
    {
        return spy.first().first().value<PDFAppToolResultOutcome>();
    }

    /// Top of a widget in the card of the dialog
    static QRect inCard(const PDFAppToolResultDialog* dialog, const QWidget* widget)
    {
        const QRect card = AppToolShellFixture::getCardRect(dialog);
        return QRect(widget->mapTo(dialog, QPoint(0, 0)) - card.topLeft(), widget->size());
    }

    /// Color of a pixel of the image at a point in the logical coordinates
    static QColor pixel(const QImage& image, const QPoint& point)
    {
        const QPoint scaled(qRound(point.x() * image.devicePixelRatio()), qRound(point.y() * image.devicePixelRatio()));
        return image.pixelColor(scaled);
    }

    static QColor overSurface(const QColor& color, qreal alpha)
    {
        const QColor surface = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
        return QColor::fromRgbF(float(surface.redF() * (1.0 - alpha) + color.redF() * alpha),
                                float(surface.greenF() * (1.0 - alpha) + color.greenF() * alpha),
                                float(surface.blueF() * (1.0 - alpha) + color.blueF() * alpha));
    }

    static bool isNear(const QColor& first, const QColor& second, int tolerance = 2)
    {
        return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance && qAbs(first.blue() - second.blue()) <= tolerance;
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QString titleOf(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? document.getInfo()->title : QString();
    }

    static pdf::PDFDocumentToolParameterDefinitions createModelDefinitions()
    {
        using Definition = pdf::PDFDocumentToolParameterDefinition;

        pdf::PDFDocumentToolParameterDefinitions definitions;
        definitions.push_back(Definition::createEnum(pdftest::TestDocumentTool::tr, QStringLiteral("preset"), QT_TR_NOOP("Preset"), nullptr,
                                                     { { QStringLiteral("small"), QT_TR_NOOP("Small") }, { QStringLiteral("large"), QT_TR_NOOP("Large") } },
                                                     QStringLiteral("small")));
        definitions.push_back(Definition::createInteger(pdftest::TestDocumentTool::tr, QStringLiteral("quality"), QT_TR_NOOP("Quality"), nullptr, 75, 1, 100));
        definitions.push_back(Definition::createBool(pdftest::TestDocumentTool::tr, QStringLiteral("structure"), QT_TR_NOOP("Structure"), nullptr, true));
        definitions.push_back(Definition::createColor(pdftest::TestDocumentTool::tr, QStringLiteral("color"), QT_TR_NOOP("Color"), nullptr, QColor(0x25, 0x63, 0xEB)));
        Definition pages = Definition::createPageSelection(pdftest::TestDocumentTool::tr, QStringLiteral("pages"), QT_TR_NOOP("Pages"), nullptr);
        pages.setStored(false);
        definitions.push_back(pages);
        definitions.push_back(Definition::createSecret(pdftest::TestDocumentTool::tr, QStringLiteral("password"), QT_TR_NOOP("Password"), nullptr));
        return definitions;
    }

    /// Waits for the end of a write of the dialog
    static bool waitForDecision(QSignalSpy& spy)
    {
        return AppToolShellFixture::waitFor([&spy]() { return spy.size() >= 1; });
    }
};

class AppToolShellTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    // The runner
    void runnerRunAndCommit();
    void runnerProgress();
    void runnerUnsuccessfulStatuses_data();
    void runnerUnsuccessfulStatuses();
    void runnerStopAtOnce();
    void runnerCloseRunningQuestion();
    void previewDebounceAndCancel();
    void previewCache();
    void previewFinalIsTheResult();
    void previewFinalJoinedByRun();
    void previewFinalStoppedComputesAgain();
    void previewVersionChange();
    void commitOldVersionRerun();
    void commitRefusedForLockedDocument();
    void runnerEnvironment();
    void autoRefreshWaitsForOperation();
    void runnerDeletedDuringRun_data();
    void runnerDeletedDuringRun();
    void runnerDeletedFromSignals_data();
    void runnerDeletedFromSignals();
    void runnerRefusedWhileBlocked();
    void runnerStoppedFromAnnouncement();
    void runnerLostDocumentRefusesRun();
    void autoRefreshReaderDuringOperation();
    void contextKnowsUnsavedChanges();
    void runnerDocumentLeavesReady();
    void dialogClosedWhenDocumentLeavesReady();
    void previewCacheBounds();
    void previewRequestedDuringRun();

    // The parameter model
    void modelUndoRedo();
    void modelRefusesInvalidValues();
    void modelLastValues();
    void modelHostileStoredValues();
    void modelBinderStore();
    void modelBinderOutlivesModel();
    void modelDeletedFromChanged();

    // The rules of the paths
    void pathRules_data();
    void pathRules();
    void pathRulesForAssistants();

    // The writer
    void copyRuleByImpact();
    void writerRefusesDisplayVersion();
    void signedCopyWholeWithoutOldRevision();
    void signedCopyKeptWithoutFile_data();
    void signedCopyKeptWithoutFile();
    void signedCopyIncrementalOfRotate();
    void encryptedCopyStaysEncrypted();

    // The result dialog
    void dialogChoicesRules_data();
    void dialogChoicesRules();
    void dialogGeometry_data();
    void dialogGeometry();
    void dialogTexts();
    void dialogSelectionTexts();
    void dialogPixels_data();
    void dialogPixels();
    void dialogRightToLeft();
    void dialogMinimalWindowScrolls();
    void dialogKeyboard();
    void dialogAccessibility();
    void dialogLanguageChange();
    void dialogHoldsOperation();
    void dialogKeep();
    void dialogDiscard();
    void dialogCopyAndContinue();
    void dialogCopyOpenOffHidesContinue();
    void dialogWriteError();
    void dialogExistingFile();
    void dialogNameRefused_data();
    void dialogNameRefused();
    void dialogNetworkPathRefused();
    void dialogOverwrite();
    void dialogOverwriteFailureKeepsDocument();
    void dialogNoOverwriteWithoutFile();
    void dialogSignedVariant();
    void dialogLocked();
    void dialogNewDocument();
    void dialogFiles();
    void dialogAnalysis();
    void dialogRecordsUse();
    void dialogAnotherTool();
    void dialogRerun();
    void dialogDeletion_data();
    void dialogDeletion();
    void dialogDeletedFromDecided_data();
    void dialogDeletedFromDecided();
    void dialogHostileSuggestedName();
    void dialogTargetOpenInTab();
    void dialogOverwriteRefusedBeforeWrite();
    void dialogOverwriteLockedDuringWrite();
    void dialogDisplayVersion();
    void dialogConfirmIcons();
    void dialogGeometryStructure();
    void dialogLanguageChangeNames();
    void screenshots();

    // The shell
    void shellToolbarGeometry();
    void shellRightPanelGeometry();
    void shellLeftPanel();
    void shellStepsPanel();
    void shellEnumMenuAndSwitch();
    void shellRunningFrame();
    void shellBusyCalloutOnlyForChange();
    void shellFlowKeep();
    void shellFlowReplacesEntry();
    void shellFlowContinue();
    void shellFlowCopyOpensCopy();
    void shellCancelAndDiscard_data();
    void shellCancelAndDiscard();
    void shellKeys();
    void shellHelpTopic();
    void shellLeaveWhileRunning();
    void shellDocumentLeavesReady();
    void shellLock_data();
    void shellLock();
    void shellPageNotFoundWithoutGUI();
    void shellRegisteredFactoryRestored();
    void shellDisplayVersionRefused();
    void shellPermissionRefused();
    void shellPreviewVersionChange();
    void shellVersionChangeReruns();
    void shellNotAccepted_data();
    void shellNotAccepted();
    void shellResultShowsItsDocument();
    void shellResultFocusInItsTab();
    void shellDeletedFromActivation();
    void shellResultWaitsForModalDialog_data();
    void shellResultWaitsForModalDialog();
    void shellPairOfTheOpenedCopy();
    void shellPairEndsWithFirstNextTool_data();
    void shellPairEndsWithFirstNextTool();
    void shellContinueLongNames_data();
    void shellContinueLongNames();
    void shellThreeSuccessorsOfCompress();
    void signatureNoteFollowsTheVersion();
    void signatureNoteAfterWholeWrite();
    void signatureNoteNamesOnlyAPossibleUndo();
    void signatureNoteOfInvalidSignature();
    void signatureNoteEndsWithTheSameBytes();
    void saveNeverPromisesLostRevision();
    void dialogCopyMarkOfTheWeb();
    void shellSignedCopyOnly();
    void shellOpenFolderAfterExport();
    void shellToolsPanelRoute();
    void shellMinimalWindow();
    void shellThemesAndScales_data();
    void shellThemesAndScales();
    void shellRightToLeft();
    void shellAccessibility();
    void shellLanguageChange();
    void shellDeletion_data();
    void shellDeletion();
    void shellScreenshots();
    void shellThinToolStructure();
    void shellMenusOfTheFrames();
    void shellPanelHoverSwitch();
    void shellResultWhileAskedAboutLeaving();
    void shellLeaveQuestionOfLostDocument();
    void shellRouteOfOpenToolActivatesTab();
    void shellSameToolOpensAgainAfterClose();
    void shellConfirmCommitsTypedValue();
    void shellPageSelectionFollowsUndoAndDocument();
    void shellRunningTextsOfTool();
    void shellRunGetsViewAndOwnPlace();
    void shellKeysInPanelAndStates();
    void shellOverlayOverTheCanvas();
    void shellOwnTabHost();
    void shellViewStateInEveryExit_data();
    void shellViewStateInEveryExit();
    void shellTabOrder();

    // The dialog Not enough memory (owner 2026-11-04)
    void outOfMemoryDialog();
    void outOfMemoryUseOneThread();
    void outOfMemoryWriteRefused();
    void outOfMemorySingleThread();
    void outOfMemoryFollowsOtherInstance();
    void outOfMemoryOtherFailures_data();
    void outOfMemoryOtherFailures();
    void outOfMemoryMeasuringOnce();
    void outOfMemoryWaitsForModal_data();
    void outOfMemoryWaitsForModal();
    void outOfMemoryWaitsForPanel();
    void outOfMemoryDeletedFromActivation();
    void outOfMemoryThemesAndScales_data();
    void outOfMemoryThemesAndScales();
    void outOfMemoryRightToLeft();
    void outOfMemoryMinimalWindow();
    void outOfMemoryKeyboardAndAccessibility();
    void outOfMemoryLanguageChange();
    void outOfMemoryDocumentLeavesReady();
    void outOfMemoryDeletion_data();
    void outOfMemoryDeletion();
};

void AppToolShellTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
    qRegisterMetaType<PDFAppToolResultOutcome>();
    qRegisterMetaType<pdf::PDFAppToolResultWriter::Result>();
}

void AppToolShellTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppFileDialogs::setFolderHandler(nullptr);
    pdf::PDFAppFileDialogs::setPrintFileHandler(nullptr);
}

// ---------------------------------------------------------------- the runner

void AppToolShellTest::runnerRunAndCommit()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(5);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);

    QSignalSpy startedSpy(runner, &PDFDocumentToolRunner::runStarted);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    const quint64 version = document->getVersion();
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));

    // The run holds a blocking operation of the kind Tool from its start: the tab has
    // its progress and asks before it closes (tools.md 9.3 point 15)
    QCOMPARE(runner->getState(), RunnerState::Running);
    QCOMPARE(startedSpy.size(), 1);
    QCOMPARE(int(document->getOperations().size()), 1);
    pdf::PDFAppDocumentOperation* operation = document->getOperations().front();
    QCOMPARE(operation->getKind(), pdf::PDFAppDocumentOperationKind::Tool);
    QCOMPARE(operation->getText(), pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE)->getTitle());
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(operation->isAskedBeforeClose());
    QVERIFY(tab->getTabProgress() >= 0.0);

    // A second confirmation during the run does nothing
    QVERIFY(!runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitForRun(runner));
    QCOMPARE(tool->getExecuteCount(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(runner->getState(), RunnerState::Waiting);
    const pdf::PDFDocumentToolResult result = finishedSpy.first().first().value<pdf::PDFDocumentToolResult>();
    QVERIFY(result.isSuccess());
    QCOMPARE(int(result.documents.size()), 1);

    // The result waits for the decision: the operation still blocks, nothing runs
    // (F2): no question at a close, no progress on the tab
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(!document->getOperations().front()->isAskedBeforeClose());
    QCOMPARE(tab->getTabProgress(), -1.0);
    QCOMPARE(document->getVersion(), version);

    QString error;
    QCOMPARE(runner->commitResult(result, QStringLiteral("Rotate Pages"), &error), PDFDocumentToolRunner::CommitStatus::Committed);
    QVERIFY(error.isEmpty());
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QVERIFY(document->getOperations().empty());
    QCOMPARE(document->getVersion(), version + 1);
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Result title"));
}

void AppToolShellTest::runnerProgress()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(5);
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);

    QSignalSpy progressSpy(runner, &PDFDocumentToolRunner::runProgress);
    QSignalSpy tabProgressSpy(tab->getDocument(), &pdf::PDFAppDocument::operationProgressChanged);

    // The steps of the tool reach the runner in the thread of the user interface, in
    // their order, and the tab through the operation
    qreal tabProgress = -1.0;
    QObject context;
    QObject::connect(runner, &PDFDocumentToolRunner::runProgress, &context, [&]() { tabProgress = qMax(tabProgress, tab->getTabProgress()); });

    QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("count"));
    parameters.insert(QStringLiteral("repeat"), 100);
    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, parameters, &result));
    QCOMPARE(result.data.value(QStringLiteral("counted")).toInteger(), qint64(500));

    QVERIFY(progressSpy.size() > 2);
    QVERIFY(progressSpy.size() <= 202);
    int last = -1;
    for (const QList<QVariant>& arguments : progressSpy)
    {
        const int percentage = arguments.first().toInt();
        QVERIFY(percentage >= last);
        QVERIFY(percentage >= 0 && percentage <= 100);
        last = percentage;
    }
    QVERIFY(last > 90);
    QVERIFY(tabProgressSpy.size() >= progressSpy.size());
    QVERIFY(tabProgress > 0.9);
    QCOMPARE(runner->getProgress(), last);

    // The preview of a tool reports by its own signal, with no operation
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* previewRunner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
    tool->setPreview(true, true);
    runner->endDecision();
    QSignalSpy previewProgressSpy(previewRunner, &PDFDocumentToolRunner::previewProgress);
    QSignalSpy previewSpy(previewRunner, &PDFDocumentToolRunner::previewFinished);
    previewRunner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&previewSpy]() { return previewSpy.size() >= 1; }));
    QVERIFY(previewProgressSpy.size() > 2);
    QVERIFY(tab->getDocument()->getOperations().empty());
}

void AppToolShellTest::runnerUnsuccessfulStatuses_data()
{
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("message");

    QTest::newRow("nothing") << QStringLiteral("nothing") << int(pdf::PDFDocumentToolResultStatus::NothingToDo) << QStringLiteral("Nothing to count");
    QTest::newRow("fail") << QStringLiteral("fail") << int(pdf::PDFDocumentToolResultStatus::Failed) << QStringLiteral("Failed on purpose");
    QTest::newRow("throw") << QStringLiteral("throw") << int(pdf::PDFDocumentToolResultStatus::Failed) << QStringLiteral("Thrown on purpose");
    QTest::newRow("throw-memory") << QStringLiteral("throw-memory") << int(pdf::PDFDocumentToolResultStatus::Failed) << QString();
    QTest::newRow("throw-std") << QStringLiteral("throw-std") << int(pdf::PDFDocumentToolResultStatus::Failed) << QString();
}

void AppToolShellTest::runnerUnsuccessfulStatuses()
{
    QFETCH(QString, mode);
    QFETCH(int, status);
    QFETCH(QString, message);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);

    // Nothing to decide: the operation ends with the run, the document is free
    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(mode), &result, RunnerState::Idle));
    QCOMPARE(int(result.status), status);
    if (!message.isEmpty())
    {
        QVERIFY2(result.message.contains(message), qPrintable(result.message));
    }
    QVERIFY(tab->getDocument()->getOperations().empty());
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
}

void AppToolShellTest::runnerStopAtOnce()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);

    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    // Stop frees the document at once; nothing waits for the worker
    runner->stop();
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(finishedSpy.first().first().value<pdf::PDFDocumentToolResult>().status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(tab->getDocument()->getOperations().empty());

    // The worker sees the cancellation; its late result is dropped
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
    AppToolShellFixture::settle();
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(runner->getState(), RunnerState::Idle);

    // The next confirmation computes again
    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &result));
    QVERIFY(result.isSuccess());
    QCOMPARE(tool->getExecuteCount(), 2);
}

void AppToolShellTest::runnerCloseRunningQuestion()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));

    // The dialog close-running 955:145813 names the tool
    const QString toolTitle = pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE)->getTitle();
    QCOMPARE(pdf::PDFAppCloseRunningDialog::getTitle(tab->getDocument()), QStringLiteral("Stop %1 and close the tab?").arg(toolTitle));

    // Stop and Close of the dialog close-running asks the operation to stop
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    tab->getDocument()->getOperations().front()->requestCancel();
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(runner->getState(), RunnerState::Idle);
    AppToolShellFixture::settle();
    QVERIFY(tab->getDocument()->getOperations().empty());
}

void AppToolShellTest::previewDebounceAndCancel()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    QSignalSpy startedSpy(runner, &PDFDocumentToolRunner::previewStarted);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::previewFinished);

    // Three changes within the delay of 300 ms are one computation of the last ones
    QElapsedTimer sinceLastChange;
    for (const QString& title : { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") })
    {
        sinceLastChange.start();
        runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), title));
        QVERIFY(runner->isPreviewPending());
    }
    QCOMPARE(startedSpy.size(), 0);
    QCOMPARE(tool->getPreviewCount(), 0);
    QVERIFY(AppToolShellFixture::waitFor([&startedSpy]() { return startedSpy.size() >= 1; }));
    QVERIFY(sinceLastChange.elapsed() >= PDFDocumentToolRunner::PREVIEW_DELAY - 20);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 1; }));
    AppToolShellFixture::settle();
    QCOMPARE(startedSpy.size(), 1);
    QCOMPARE(tool->getPreviewCount(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    QVERIFY(runner->getPreview());

    // A preview does not touch the document: no operation
    QVERIFY(tab->getDocument()->getOperations().empty());

    // A change cancels the older computation: its result never comes
    tool->setHold(true);
    runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QStringLiteral("d")));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QStringLiteral("e")));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 2; }));
    AppToolShellFixture::settle();
    QCOMPARE(finishedSpy.size(), 2);

    // Nothing more is wanted
    runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QStringLiteral("f")));
    runner->cancelPreview();
    QVERIFY(!runner->isPreviewPending());
    QTest::qWait(PDFDocumentToolRunner::PREVIEW_DELAY + 50);
    QCOMPARE(tool->getPreviewCount(), 3);
    QVERIFY(!runner->getPreview());
}

void AppToolShellTest::previewCache()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::previewFinished);

    const QVariantMap first = AppToolShellFixture::parameters(QStringLiteral("count"), QStringLiteral("a"));
    runner->requestPreview(first);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 1; }));
    QCOMPARE(tool->getPreviewCount(), 1);

    const QVariantMap second = AppToolShellFixture::parameters(QStringLiteral("count"), QStringLiteral("b"));
    runner->requestPreview(second);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 2; }));
    QCOMPARE(tool->getPreviewCount(), 2);

    // The same parameters of the same version come from the cache at once
    runner->requestPreview(first);
    QCOMPARE(finishedSpy.size(), 3);
    QCOMPARE(tool->getPreviewCount(), 2);
    QVERIFY(!runner->isPreviewPending());
    QVERIFY(runner->getPreview());
}

void AppToolShellTest::previewFinalIsTheResult()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
    tool->setPreview(true, true);

    QSignalSpy previewSpy(runner, &PDFDocumentToolRunner::previewFinished);
    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    runner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&previewSpy]() { return previewSpy.size() >= 1; }));
    QCOMPARE(tool->getPreviewCount(), 1);
    QCOMPARE(int(previewSpy.first().first().value<pdf::PDFDocumentToolResult>().documents.size()), 1);

    // The confirmation computes nothing: the dialog gets the measured result at once
    QSignalSpy runSpy(runner, &PDFDocumentToolRunner::runFinished);
    QVERIFY(runner->run(parameters));
    QCOMPARE(runSpy.size(), 1);
    QCOMPARE(runner->getState(), RunnerState::Waiting);
    QCOMPARE(tool->getExecuteCount(), 0);
    QCOMPARE(tool->getPreviewCount(), 1);
    QVERIFY(tab->getDocument()->isBlockedByOperation());

    QString error;
    QCOMPARE(runner->commitResult(runSpy.first().first().value<pdf::PDFDocumentToolResult>(), QStringLiteral("Compress"), &error), PDFDocumentToolRunner::CommitStatus::Committed);
    QCOMPARE(tab->getDocument()->getDocument()->getInfo()->title, QStringLiteral("Result title"));
}

void AppToolShellTest::previewFinalJoinedByRun()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
    tool->setPreview(true, true);
    tool->setHold(true);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    runner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    // The confirmation joins the running computation (295:1711): it blocks the document now
    QSignalSpy runSpy(runner, &PDFDocumentToolRunner::runFinished);
    QSignalSpy previewSpy(runner, &PDFDocumentToolRunner::previewFinished);
    QVERIFY(runner->run(parameters));
    QCOMPARE(runner->getState(), RunnerState::Running);
    QVERIFY(tab->getDocument()->isBlockedByOperation());
    QTest::qWait(20);
    QCOMPARE(tool->getHeldCount(), 1);

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&runSpy]() { return runSpy.size() >= 1; }));
    QCOMPARE(tool->getPreviewCount(), 1);
    QCOMPARE(tool->getExecuteCount(), 0);
    QCOMPARE(previewSpy.size(), 0);
    QCOMPARE(runner->getState(), RunnerState::Waiting);
    QVERIFY(runSpy.first().first().value<pdf::PDFDocumentToolResult>().isSuccess());
}

void AppToolShellTest::previewFinalStoppedComputesAgain()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
    tool->setPreview(true, true);
    tool->setHold(true);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    runner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    QVERIFY(runner->run(parameters));
    runner->stop();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));

    // Stop leaves no result: the next confirmation computes
    tool->setHold(false);
    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, parameters, &result));
    QVERIFY(result.isSuccess());
    QCOMPARE(tool->getExecuteCount(), 1);
}

void AppToolShellTest::previewVersionChange()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::previewFinished);
    QSignalSpy droppedSpy(runner, &PDFDocumentToolRunner::previewDropped);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("count"));
    runner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 1; }));
    QVERIFY(runner->getPreview());

    // A new version (an assistant, an undo of the shell): the preview is dropped and
    // computed again on the new version
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation), QStringLiteral("change"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    QCOMPARE(droppedSpy.size(), 1);
    QVERIFY(!runner->getPreview());
    QVERIFY(runner->isPreviewPending());
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() >= 2; }));
    QCOMPARE(tool->getPreviewCount(), 2);
    QVERIFY(runner->getPreview());
}

void AppToolShellTest::commitOldVersionRerun()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    QVERIFY(runner->run(parameters));
    QVERIFY(AppToolShellFixture::waitForRun(runner));

    for (int attempt = 1; attempt <= PDFDocumentToolRunner::MAXIMUM_ATTEMPTS; ++attempt)
    {
        QCOMPARE(runner->getAttempt(), attempt);
        QCOMPARE(tool->getExecuteCount(), attempt);
        QCOMPARE(runner->getState(), RunnerState::Waiting);
        QCOMPARE(finishedSpy.size(), attempt);

        // The version changes behind the operation (nothing but a reload or a restore
        // can do it): the result of the old version is never committed
        document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
        const quint64 changedVersion = document->getVersion();

        QString error;
        const pdf::PDFDocumentToolResult result = finishedSpy.last().first().value<pdf::PDFDocumentToolResult>();
        const PDFDocumentToolRunner::CommitStatus status = runner->commitResult(result, QStringLiteral("Rotate"), &error);
        QCOMPARE(document->getVersion(), changedVersion);
        if (attempt < PDFDocumentToolRunner::MAXIMUM_ATTEMPTS)
        {
            // Never silently: a new result of the new version, which the user decides about
            QCOMPARE(status, PDFDocumentToolRunner::CommitStatus::Rerun);
            QCOMPARE(runner->getResultVersion(), changedVersion);
            QVERIFY(AppToolShellFixture::waitForRun(runner));
        }
        else
        {
            QCOMPARE(status, PDFDocumentToolRunner::CommitStatus::Refused);
            QVERIFY(!error.isEmpty());
        }
    }

    // At most 3 runs, the document is free again and has no undo step of the tool
    QCOMPARE(tool->getExecuteCount(), PDFDocumentToolRunner::MAXIMUM_ATTEMPTS);
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QVERIFY(document->getOperations().empty());
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
    QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Result title"));
}

void AppToolShellTest::commitRefusedForLockedDocument()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);

    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &result));
    tab->getDocument()->setLocked(true);

    QString error;
    const quint64 version = tab->getDocument()->getVersion();
    QCOMPARE(runner->commitResult(result, QStringLiteral("Rotate"), &error), PDFDocumentToolRunner::CommitStatus::Refused);
    QVERIFY(error.contains(QStringLiteral("locked")));
    QCOMPARE(tab->getDocument()->getVersion(), version);
}

void AppToolShellTest::runnerEnvironment()
{
    // The context of a snapshot (the shared part of the GUI and of MCP, F17)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(3);
    const pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();

    pdf::PDFAppToolRunEnvironment environment;
    environment.selectedPages = { 0, 2 };
    environment.currentPage = 1;
    environment.authorName = QStringLiteral("Jane");
    environment.temporaryFolder = fixture.folder();
    pdf::PDFDocumentToolInputDocument second = pdf::PDFDocumentToolInputDocument::create(snapshot.document, QByteArray(), QStringLiteral("other.pdf"), true);
    environment.additionalDocuments.push_back(second);

    const pdf::PDFDocumentToolContext context = PDFDocumentToolRunner::createContext(snapshot, environment);
    QCOMPARE(int(context.documents.size()), 2);
    QCOMPARE(context.getDocument(), snapshot.document.data());
    QCOMPARE(context.documents[0].fileName, QFileInfo(snapshot.filePath).fileName());
    QCOMPARE(context.documents[0].isModified, snapshot.isModified);
    QCOMPARE(context.documents[1].fileName, QStringLiteral("other.pdf"));
    QCOMPARE(context.selectedPages, environment.selectedPages);
    QCOMPARE(context.currentPage, pdf::PDFInteger(1));
    QCOMPARE(context.authorName, QStringLiteral("Jane"));
    QCOMPARE(context.temporaryFolder, fixture.folder());

    // No snapshot (a tool without a document): only the given inputs
    QCOMPARE(int(PDFDocumentToolRunner::createContext(pdf::PDFAppDocumentSnapshot(), environment).documents.size()), 1);
    QVERIFY(PDFDocumentToolRunner::createContext(pdf::PDFAppDocumentSnapshot()).documents.empty());
}

void AppToolShellTest::autoRefreshWaitsForOperation()
{
    // F2: the file changed on the disk does not replace a document under a run or under
    // a result waiting for the decision
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refresh.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    refresh->setEnabled(true);

    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);
    QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), nullptr));
    const QByteArray bytes = ToolShellTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5));
    {
        QFile target(file);
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write(bytes);
    }

    refresh->checkNow();
    QTest::qWait(pdf::PDFAppAutoRefresh::CHECK_DELAY + 100);
    AppToolShellFixture::settle();
    QCOMPARE(refresh->getReloadCount(), 0);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));

    // The decision ends the operation: now the file is read
    runner->endDecision();
    QVERIFY(AppToolShellFixture::waitFor([refresh]() { return refresh->getReloadCount() == 1; }));
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(5));
}

void AppToolShellTest::runnerDeletedDuringRun_data()
{
    QTest::addColumn<QString>("victim");
    QTest::newRow("runner") << QStringLiteral("runner");
    QTest::newRow("tab") << QStringLiteral("tab");
    QTest::newRow("window") << QStringLiteral("window");
}

void AppToolShellTest::runnerDeletedDuringRun()
{
    QFETCH(QString, victim);

    std::shared_ptr<TestDocumentTool> tool;
    {
        AppToolShellFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
        tool->setHold(true);
        QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        QElapsedTimer timer;
        timer.start();
        if (victim == QLatin1String("runner"))
        {
            // F12: the operation ends with the owner of the runner, not by the worker
            delete runner;
            QVERIFY(document->getOperations().empty());
            QVERIFY(!document->isBlockedByOperation());
        }
        else if (victim == QLatin1String("tab"))
        {
            delete tab;
            QVERIFY(document.isNull());
        }
        else
        {
            delete fixture.window.data();
            QVERIFY(document.isNull());
        }

        // Nobody waited for the worker: it has only got the cancellation
        QVERIFY(timer.elapsed() < 5000 * AppToolShellFixture::getTimeoutFactor());
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
        AppToolShellFixture::settle();
    }
    QCOMPARE(tool->getExecuteCount(), 1);
}

void AppToolShellTest::runnerDeletedFromSignals_data()
{
    QTest::addColumn<QString>("signalName");
    QTest::newRow("runStarted") << QStringLiteral("runStarted");
    QTest::newRow("runStarted of a cached result") << QStringLiteral("runStartedCached");
    QTest::newRow("runProgress") << QStringLiteral("runProgress");
    QTest::newRow("runFinished") << QStringLiteral("runFinished");
    QTest::newRow("previewStarted") << QStringLiteral("previewStarted");
    QTest::newRow("previewFinished") << QStringLiteral("previewFinished");
    QTest::newRow("previewDropped") << QStringLiteral("previewDropped");
    QTest::newRow("operationsChanged") << QStringLiteral("operationsChanged");
}

void AppToolShellTest::runnerDeletedFromSignals()
{
    QFETCH(QString, signalName);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    QPointer<PDFDocumentToolRunner> runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);

    bool isDeleted = false;
    QObject context;
    const std::function<void()> deleter = [&]()
    {
        if (runner && !isDeleted)
        {
            isDeleted = true;
            delete runner.data();
        }
    };

    if (signalName.startsWith(QLatin1String("runStarted")))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::runStarted, &context, deleter);
    }
    else if (signalName == QLatin1String("runProgress"))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::runProgress, &context, deleter);
    }
    else if (signalName == QLatin1String("runFinished"))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::runFinished, &context, deleter);
    }
    else if (signalName == QLatin1String("previewStarted"))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::previewStarted, &context, deleter);
    }
    else if (signalName == QLatin1String("previewFinished"))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::previewFinished, &context, deleter);
    }
    else if (signalName == QLatin1String("previewDropped"))
    {
        QObject::connect(runner.data(), &PDFDocumentToolRunner::previewDropped, &context, deleter);
    }
    else
    {
        QObject::connect(document, &pdf::PDFAppDocument::operationsChanged, &context, deleter);
    }

    if (signalName == QLatin1String("runStartedCached"))
    {
        // The result of a final preview is finished right after runStarted
        const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
        tool->setPreview(true, true);
        runner->requestPreview(parameters);
        QVERIFY(AppToolShellFixture::waitFor([&]() { return runner->getPreview() != nullptr; }));
        runner->run(parameters);
    }
    else if (signalName.startsWith(QLatin1String("preview")))
    {
        const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("count"));
        runner->requestPreview(parameters);
        if (signalName == QLatin1String("previewDropped"))
        {
            QVERIFY(AppToolShellFixture::waitFor([&]() { return !runner->isPreviewPending(); }));
            QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation), QStringLiteral("change"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
        }
    }
    else
    {
        runner->run(AppToolShellFixture::parameters(QStringLiteral("count")));
    }

    QVERIFY(AppToolShellFixture::waitFor([&]() { return isDeleted; }));
    QVERIFY(runner.isNull());

    // Whatever the signal was, the operation has gone with the runner
    AppToolShellFixture::settle();
    QVERIFY(document->getOperations().empty());
    QVERIFY(!document->isBlockedByOperation());
}

// ---------------------------------------------------------------- the model

void AppToolShellTest::modelUndoRedo()
{
    AppToolShellFixture fixture;
    pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);
    QSignalSpy changedSpy(&model, &pdf::PDFAppToolParameterModel::changed);
    QSignalSpy undoSpy(&model, &pdf::PDFAppToolUndoTarget::undoStateChanged);

    // K6: Undo and Redo are disabled until the first change
    QVERIFY(!model.canUndo());
    QVERIFY(!model.canRedo());
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(75));

    QVERIFY(model.setValue(QStringLiteral("quality"), 80).isEmpty());
    QCOMPARE(changedSpy.size(), 1);
    QCOMPARE(changedSpy.first().first().toString(), QStringLiteral("quality"));
    QCOMPARE(undoSpy.size(), 1);
    QVERIFY(model.canUndo());
    QVERIFY(!model.canRedo());

    // The value of the same canonical value is no change: no step, no signal
    QVERIFY(model.setValue(QStringLiteral("quality"), qint64(80)).isEmpty());
    QCOMPARE(changedSpy.size(), 1);

    // More values are one step, each changed name is announced once
    QVariantMap values;
    values.insert(QStringLiteral("preset"), QStringLiteral("large"));
    values.insert(QStringLiteral("structure"), false);
    QVERIFY(model.setValues(values).isEmpty());
    QCOMPARE(changedSpy.size(), 3);

    model.undo();
    QCOMPARE(model.getValue(QStringLiteral("preset")).toString(), QStringLiteral("small"));
    QCOMPARE(model.getValue(QStringLiteral("structure")).toBool(), true);
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(80));
    QCOMPARE(changedSpy.size(), 5);
    QVERIFY(model.canRedo());

    model.undo();
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(75));
    QVERIFY(!model.canUndo());

    model.redo();
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(80));
    model.redo();
    QCOMPARE(model.getValue(QStringLiteral("preset")).toString(), QStringLiteral("large"));
    QVERIFY(!model.canRedo());

    // A new change cuts the redo branch
    model.undo();
    QVERIFY(model.setValue(QStringLiteral("quality"), 90).isEmpty());
    QVERIFY(!model.canRedo());

    // The history is bounded
    for (int i = 0; i < pdf::PDFAppToolParameterModel::MAXIMUM_HISTORY + 20; ++i)
    {
        QVERIFY(model.setValue(QStringLiteral("quality"), 1 + i % 2).isEmpty());
    }
    int steps = 0;
    while (model.canUndo())
    {
        model.undo();
        ++steps;
    }
    QCOMPARE(steps, pdf::PDFAppToolParameterModel::MAXIMUM_HISTORY);
}

void AppToolShellTest::modelRefusesInvalidValues()
{
    AppToolShellFixture fixture;
    pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);
    QSignalSpy changedSpy(&model, &pdf::PDFAppToolParameterModel::changed);

    // A value out of its range, a foreign option, a wrong type, an unknown name
    QVERIFY(!model.setValue(QStringLiteral("quality"), 1000).isEmpty());
    QVERIFY(!model.setValue(QStringLiteral("preset"), QStringLiteral("unknown")).isEmpty());
    QVERIFY(!model.setValue(QStringLiteral("structure"), QVariantList()).isEmpty());
    QVERIFY(!model.setValue(QStringLiteral("nonsense"), 1).isEmpty());
    QCOMPARE(changedSpy.size(), 0);
    QVERIFY(!model.canUndo());

    // One invalid value refuses the whole set
    QVariantMap values;
    values.insert(QStringLiteral("preset"), QStringLiteral("large"));
    values.insert(QStringLiteral("quality"), -1);
    QVERIFY(!model.setValues(values).isEmpty());
    QCOMPARE(model.getValue(QStringLiteral("preset")).toString(), QStringLiteral("small"));
    QVERIFY(!model.canUndo());
    QVERIFY(model.getErrors().empty());

    // A required parameter without a value is an error of the values
    pdf::PDFDocumentToolParameterDefinitions definitions;
    pdf::PDFDocumentToolParameterDefinition required = pdf::PDFDocumentToolParameterDefinition::createString(pdftest::TestDocumentTool::tr, QStringLiteral("name"), QT_TR_NOOP("Name"), nullptr, QString());
    required.setRequired(true);
    definitions.push_back(required);
    pdf::PDFAppToolParameterModel requiredModel(nullptr, TOOL_COMPRESS, definitions, nullptr);
    QVERIFY(!requiredModel.getValues().contains(QStringLiteral("name")));
    QCOMPARE(int(requiredModel.getErrors().size()), 1);
    QCOMPARE(requiredModel.getErrors().front().parameter, QStringLiteral("name"));
    QVERIFY(requiredModel.setValue(QStringLiteral("name"), QStringLiteral("x")).isEmpty());
    QVERIFY(requiredModel.getErrors().empty());
}

void AppToolShellTest::modelLastValues()
{
    AppToolShellFixture fixture;
    const pdf::PDFDocumentToolParameterDefinitions definitions = ToolShellTestHelper::createModelDefinitions();
    {
        pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, definitions, nullptr);
        QVERIFY(model.setValue(QStringLiteral("preset"), QStringLiteral("large")).isEmpty());
        QVERIFY(model.setValue(QStringLiteral("quality"), 42).isEmpty());
        QVERIFY(model.setValue(QStringLiteral("color"), QColor(255, 0, 0)).isEmpty());
        QVERIFY(model.setValue(QStringLiteral("password"), QStringLiteral("secret")).isEmpty());
        pdf::PDFDocumentToolPageSelection selection;
        selection.pages = QStringLiteral("1-3");
        QVERIFY(model.setValue(QStringLiteral("pages"), selection.toVariant()).isEmpty());

        // Nothing is stored until the result is accepted
        pdf::PDFAppToolParameterModel notStored(fixture.context.get(), TOOL_COMPRESS, definitions, nullptr);
        QCOMPARE(notStored.getValue(QStringLiteral("quality")).toLongLong(), qint64(75));
        QVERIFY(!model.storeLastValues().isError());
    }

    pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, definitions, nullptr);
    QCOMPARE(model.getValue(QStringLiteral("preset")).toString(), QStringLiteral("large"));
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(42));
    QCOMPARE(model.getValue(QStringLiteral("color")).value<QColor>(), QColor(255, 0, 0));

    // A Secret and the selection of one document are never kept (setStored(false))
    QVERIFY(model.getValue(QStringLiteral("password")).toString().isEmpty());
    QCOMPARE(pdf::PDFDocumentToolPageSelection::fromVariant(model.getValue(QStringLiteral("pages")), nullptr)->pages, QString());
    QVERIFY(!model.canUndo());

    // ... and not in the database at all
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    pdf::PDFDatabaseStatement statement(connection, "SELECT name FROM tool_parameter WHERE tool_id = ?1");
    statement.bindText(1, TOOL_COMPRESS);
    QStringList names;
    while (statement.next())
    {
        names << statement.getText(0);
    }
    QVERIFY(!names.contains(QStringLiteral("password")));
    QVERIFY(!names.contains(QStringLiteral("pages")));
    QVERIFY(names.contains(QStringLiteral("quality")));
}

void AppToolShellTest::modelHostileStoredValues()
{
    AppToolShellFixture fixture;
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();

    // Rows of another version or of a hostile writer, also of what is never stored: the
    // selection of the pages is a valid stored value of its type
    pdf::PDFDocumentToolPageSelection selection;
    selection.pages = QStringLiteral("1-3");
    const pdf::PDFDocumentToolParameterDefinitions definitions = ToolShellTestHelper::createModelDefinitions();
    const QVariant storedSelection = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("pages"))->toStorageValue(selection.toVariant());
    QVERIFY(storedSelection.isValid());

    const QList<QPair<QString, QVariant>> rows = {
        { QStringLiteral("preset"), QStringLiteral("evil") },
        { QStringLiteral("quality"), qint64(1000000) },
        { QStringLiteral("structure"), QByteArray("\x01\x02") },
        { QStringLiteral("color"), QStringLiteral("not a color") },
        { QStringLiteral("pages"), storedSelection },
        { QStringLiteral("password"), QStringLiteral("leaked") },
        { QStringLiteral("unknown"), QStringLiteral("x") }
    };
    for (const auto& row : rows)
    {
        pdf::PDFDatabaseStatement statement(connection, "INSERT INTO tool_parameter(tool_id, name, value) VALUES (?1, ?2, ?3)");
        statement.bindText(1, TOOL_COMPRESS);
        statement.bindText(2, row.first);
        if (row.second.typeId() == QMetaType::QByteArray)
        {
            statement.bindBlob(3, row.second.toByteArray());
        }
        else
        {
            statement.bindVariant(3, row.second);
        }
        statement.execute();
        QVERIFY(!statement.getError().isError());
    }

    pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);
    QCOMPARE(model.getValue(QStringLiteral("preset")).toString(), QStringLiteral("small"));
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(75));
    QCOMPARE(model.getValue(QStringLiteral("structure")).toBool(), true);
    QCOMPARE(model.getValue(QStringLiteral("color")).value<QColor>(), QColor(0x25, 0x63, 0xEB));
    QVERIFY(model.getValue(QStringLiteral("password")).toString().isEmpty());
    QCOMPARE(pdf::PDFDocumentToolPageSelection::fromVariant(model.getValue(QStringLiteral("pages")), nullptr)->pages, QString());
    QVERIFY(!model.getValues().contains(QStringLiteral("unknown")));
}

void AppToolShellTest::modelBinderStore()
{
    AppToolShellFixture fixture;
    pdf::PDFAppToolParameterModel model(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);

    // The second store of the one binder (2.5): the control shows and writes the model
    QWidget parent;
    const std::optional<pdf::PDFSettingDefinition> definition = model.toSettingDefinition(QStringLiteral("quality"));
    QVERIFY(definition);
    QCOMPARE(definition->getType(), pdf::PDFSettingDefinition::Type::Integer);
    pdf::PDFAppSettingControl control(fixture.context.get(), *definition, &parent, model.createValueStore());
    QCOMPARE(control.getSpinBox()->value(), 75);

    control.getSpinBox()->setValue(60);
    control.commitPendingValue();
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(60));
    QVERIFY(model.canUndo());

    // Undo shows the older value in the control: nothing is written back
    model.undo();
    control.refresh();
    QCOMPARE(control.getSpinBox()->value(), 75);
    QVERIFY(!model.canUndo());

    // The settings stay untouched by the binder of a tool
    QVERIFY(!fixture.context->getSettings().getStoredValue(definition->getKey()).isValid());

    // Enum, Bool, Color; the page selection and a Secret have no control of the binder
    const std::optional<pdf::PDFSettingDefinition> preset = model.toSettingDefinition(QStringLiteral("preset"));
    QVERIFY(preset);
    pdf::PDFAppSettingControl presetControl(fixture.context.get(), *preset, &parent, model.createValueStore());
    QCOMPARE(presetControl.getComboBox()->count(), 2);
    QCOMPARE(presetControl.getComboBox()->itemText(1), QStringLiteral("Large"));

    const std::optional<pdf::PDFSettingDefinition> color = model.toSettingDefinition(QStringLiteral("color"));
    QVERIFY(color);
    pdf::PDFAppSettingControl colorControl(fixture.context.get(), *color, &parent, model.createValueStore());
    QCOMPARE(colorControl.getKind(), pdf::PDFAppSettingControl::Kind::Color);
    QCOMPARE(colorControl.getShownValue().toString().toLower(), QStringLiteral("#2563eb"));

    QVERIFY(model.toSettingDefinition(QStringLiteral("structure")));
    QVERIFY(!model.toSettingDefinition(QStringLiteral("pages")));
    QVERIFY(!model.toSettingDefinition(QStringLiteral("password")));
    QVERIFY(!model.toSettingDefinition(QStringLiteral("nonsense")));

    // A value the model refuses is reported and the stored one shown again
    QSignalSpy failedSpy(&control, &pdf::PDFAppSettingControl::writeFailed);
    std::shared_ptr<pdf::PDFAppValueStore> store = model.createValueStore();
    QVERIFY(!store->writeValue(QStringLiteral("tool.quality"), 100000).isEmpty());
    QCOMPARE(model.getValue(QStringLiteral("quality")).toLongLong(), qint64(75));
    QCOMPARE(failedSpy.size(), 0);
}

void AppToolShellTest::modelBinderOutlivesModel()
{
    AppToolShellFixture fixture;
    QWidget parent;
    std::unique_ptr<pdf::PDFAppToolParameterModel> model = std::make_unique<pdf::PDFAppToolParameterModel>(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);
    const std::optional<pdf::PDFSettingDefinition> definition = model->toSettingDefinition(QStringLiteral("quality"));
    pdf::PDFAppSettingControl control(fixture.context.get(), *definition, &parent, model->createValueStore());
    control.getSpinBox()->setValue(60);
    QVERIFY(control.hasPendingValue());

    // The model is deleted first: the binder has nothing to read and to write
    model.reset();
    control.commitPendingValue();
    control.refresh();
    QCOMPARE(control.getSpinBox()->value(), 75);
}

void AppToolShellTest::modelDeletedFromChanged()
{
    AppToolShellFixture fixture;
    QPointer<pdf::PDFAppToolParameterModel> model = new pdf::PDFAppToolParameterModel(fixture.context.get(), TOOL_COMPRESS, ToolShellTestHelper::createModelDefinitions(), nullptr);

    int calls = 0;
    QObject context;
    QObject::connect(model.data(), &pdf::PDFAppToolParameterModel::changed, &context, [&]()
    {
        ++calls;
        delete model.data();
    });

    QVariantMap values;
    values.insert(QStringLiteral("preset"), QStringLiteral("large"));
    values.insert(QStringLiteral("quality"), 10);
    model->setValues(values);

    // The second changed name is not announced by a deleted model
    QVERIFY(model.isNull());
    QCOMPARE(calls, 1);
}

// ---------------------------------------------------------------- the paths

void AppToolShellTest::pathRules_data()
{
    using Problem = pdf::PDFAppToolPathRules::NameProblem;

    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("problem");

    QTest::newRow("plain") << QStringLiteral("Project_Report_clean.pdf") << int(Problem::None);
    QTest::newRow("unicode") << QStringLiteral("Žluťoučký kůň.pdf") << int(Problem::None);
    QTest::newRow("dots inside") << QStringLiteral("a.b.c.pdf") << int(Problem::None);
    QTest::newRow("empty") << QString() << int(Problem::Empty);
    QTest::newRow("dot") << QStringLiteral(".") << int(Problem::Empty);
    QTest::newRow("dot dot") << QStringLiteral("..") << int(Problem::Empty);
    QTest::newRow("long") << QString(256, QLatin1Char('a')) << int(Problem::TooLong);
    QTest::newRow("separator") << QStringLiteral("a/b.pdf") << int(Problem::Folder);
#ifdef Q_OS_WIN
    QTest::newRow("backslash") << QStringLiteral("..\\b.pdf") << int(Problem::Folder);
    QTest::newRow("stream") << QStringLiteral("a.pdf::$DATA") << int(Problem::Stream);
    QTest::newRow("colon") << QStringLiteral("12:30 report.pdf") << int(Problem::Stream);
    QTest::newRow("trailing dot") << QStringLiteral("a.pdf.") << int(Problem::TrailingDotOrSpace);
    QTest::newRow("trailing space") << QStringLiteral("a.pdf ") << int(Problem::TrailingDotOrSpace);
#else
    // Valid names of this system: refused only where Windows reads them otherwise
    QTest::newRow("backslash") << QStringLiteral("..\\b.pdf") << int(Problem::None);
    QTest::newRow("colon") << QStringLiteral("12:30 report.pdf") << int(Problem::None);
    QTest::newRow("trailing dot") << QStringLiteral("a.pdf.") << int(Problem::None);
    QTest::newRow("trailing space") << QStringLiteral("a.pdf ") << int(Problem::None);
    QTest::newRow("windows character") << QStringLiteral("a?.pdf") << int(Problem::None);
#endif
    QTest::newRow("nul") << QStringLiteral("NUL.pdf") << int(Problem::ReservedDevice);
    QTest::newRow("con") << QStringLiteral("con") << int(Problem::ReservedDevice);
    QTest::newRow("com1") << QStringLiteral("COM1.txt") << int(Problem::ReservedDevice);
    QTest::newRow("lpt superscript") << QString(QStringLiteral("LPT") + QChar(0x00B9) + QStringLiteral(".pdf")) << int(Problem::ReservedDevice);
    QTest::newRow("com0 is a name") << QStringLiteral("COM0.pdf") << int(Problem::None);
    QTest::newRow("control") << QString(QStringLiteral("a") + QChar(0x0007) + QStringLiteral(".pdf")) << int(Problem::InvalidCharacter);
    QTest::newRow("bidi override") << QString(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("fdp.exe")) << int(Problem::InvalidCharacter);
#ifdef Q_OS_WIN
    QTest::newRow("windows character") << QStringLiteral("a?.pdf") << int(Problem::InvalidCharacter);
#endif
}

void AppToolShellTest::pathRules()
{
    QFETCH(QString, name);
    QFETCH(int, problem);

    const pdf::PDFAppToolPathRules::NameProblem found = pdf::PDFAppToolPathRules::checkFileName(name);
    QCOMPARE(int(found), problem);
    QCOMPARE(pdf::PDFAppToolPathRules::getNameProblemText(found).isEmpty(), found == pdf::PDFAppToolPathRules::NameProblem::None);
}

void AppToolShellTest::pathRulesForAssistants()
{
    // MCP keeps calling the same rules (T1, T2)
    AppToolShellFixture fixture;
    QString error;
    const QString folder = fixture.folder();
    QVERIFY(!pdf::PDFAppMcpArguments::checkPath(folder + QStringLiteral("/copy.pdf"), true, &error).isEmpty());
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QStringList refusedNames = { QStringLiteral("NUL.pdf"), QStringLiteral("COM1.pdf"), QString(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("fdp.exe")) };
#ifdef Q_OS_WIN
    refusedNames << QStringLiteral("a.pdf.") << QStringLiteral("a.pdf ");
#endif
    for (const QString& name : refusedNames)
    {
        QVERIFY2(pdf::PDFAppMcpArguments::checkPath(folder + QLatin1Char('/') + name, false, &error).isEmpty(), qPrintable(name));
        QCOMPARE(error, QStringLiteral("the file name is not allowed"));
    }

    // A stream in a folder of the path: the file name itself is fine
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(folder + QStringLiteral("/x:y:z/a.pdf"), false, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("the file name is not allowed"));

#ifdef Q_OS_WIN
    // The system resolves a trailing dot to the existing file: only the name as it was
    // given tells it, the canonical path has lost it
    const QString existing = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("existing.pdf"), 1);
    QVERIFY(!pdf::PDFAppMcpArguments::checkPath(existing, true, &error).isEmpty());
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(existing + QLatin1Char('.'), false, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("the file name is not allowed"));
#else
    // A valid Linux name is no regression of the rules of Windows (the rules before phase 2)
    for (const QString& name : { QStringLiteral("12:30 report.pdf"), QStringLiteral("a.b."), QStringLiteral("a b "), QStringLiteral("a\\b.pdf"), QStringLiteral("a?.pdf") })
    {
        QVERIFY2(!pdf::PDFAppMcpArguments::checkPath(folder + QLatin1Char('/') + name, false, &error).isEmpty(), qPrintable(error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    // Both layers are needed: a link of a hidden extension to a good file (only its own
    // name tells it) and a good link to a reserved name (only the target tells it)
    const QString target = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("target.pdf"), 1);
    const QString hiddenLink = folder + QStringLiteral("/a") + QChar(0x202E) + QStringLiteral("fdp.pdf");
    QVERIFY(QFile::link(target, hiddenLink));
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(hiddenLink, true, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("the file name is not allowed"));

    const QString reservedTarget = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("NUL.pdf"), 1);
    const QString goodLink = folder + QStringLiteral("/good.pdf");
    QVERIFY(QFile::link(reservedTarget, goodLink));
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(goodLink, true, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("the file name is not allowed"));
#endif

    QVERIFY(pdf::PDFAppToolPathRules::isDeviceOrNetworkPath(QStringLiteral("\\\\server\\share\\a.pdf")));
    QVERIFY(pdf::PDFAppToolPathRules::isDeviceOrNetworkPath(QStringLiteral("//server/share/a.pdf")));
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(QStringLiteral("//server/share/a.pdf"), true, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("paths of devices and of network locations are refused; use a local file"));

#ifdef Q_OS_WIN
    // The prefix of the object manager opens another computer: refused by its name, before the
    // path is touched - the folder of the file would be asked for at the server otherwise
    for (const char* path : { "\\??\\UNC\\host.invalid\\share\\a.pdf", "/?\?/UNC/host.invalid/share/a.pdf", "/x/../?\?/UNC/host.invalid/share/a.pdf",
                              "\\x\\..\\??\\UNC\\host.invalid\\share\\a.pdf", "\\??\\C:\\a.pdf",
                              "/x/..//host.invalid/share/a.pdf", "/.//host.invalid/share/a.pdf", "\\x\\..\\\\host.invalid\\share\\a.pdf",
                              "/.//?/UNC/host.invalid/share/a.pdf", "/x/..//?/UNC/host.invalid/share/a.pdf" })
    {
        const QString objectManagerPath = QString::fromLatin1(path);
        QVERIFY2(pdf::PDFAppToolPathRules::isDeviceOrNetworkPath(objectManagerPath), path);
        error.clear();
        QVERIFY2(pdf::PDFAppMcpArguments::checkPath(objectManagerPath, true, &error).isEmpty(), path);
        QCOMPARE(error, QStringLiteral("paths of devices and of network locations are refused; use a local file"));
    }
    QVERIFY(!pdf::PDFAppToolPathRules::isDeviceOrNetworkPath(folder + QStringLiteral("/x/../a.pdf")));
#endif
    QVERIFY(!pdf::PDFAppToolPathRules::isNetworkPath(folder + QStringLiteral("/a.pdf")));
    QVERIFY(pdf::PDFAppToolPathRules::hasStream(QStringLiteral("C:/a.pdf:stream")));
    QVERIFY(!pdf::PDFAppToolPathRules::hasStream(QStringLiteral("C:/a.pdf")));
}

void AppToolShellTest::runnerRefusedWhileBlocked()
{
    // A commit of a run started under another blocking operation would be refused
    // after the user has decided: the run does not start at all
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    QSignalSpy startedSpy(runner, &PDFDocumentToolRunner::runStarted);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    QPointer<pdf::PDFAppDocumentOperation> other = document->beginOperation(pdf::PDFAppDocumentOperationKind::Edit, QStringLiteral("Other"), true);
    QVERIFY(!runner->run(parameters));
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QCOMPARE(startedSpy.size(), 0);

    // A non-blocking operation (the text index) stops nothing
    other->finish();
    QPointer<pdf::PDFAppDocumentOperation> index = document->beginOperation(pdf::PDFAppDocumentOperationKind::Index, QStringLiteral("Index"), false);
    QVERIFY(fixture.runTool(runner, parameters, nullptr));
    QCOMPARE(tool->getExecuteCount(), 1);
    index->finish();
    runner->endDecision();
    QVERIFY(!document->isBlockedByOperation());
}

void AppToolShellTest::runnerStoppedFromAnnouncement()
{
    // A slot of the announcement of the operation stops the run: no operation stays
    // behind and the joined computation of the preview goes too
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
    tool->setPreview(true, true);
    tool->setHold(true);

    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    runner->requestPreview(parameters);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    QSignalSpy startedSpy(runner, &PDFDocumentToolRunner::runStarted);
    QObject context;
    QObject::connect(document, &pdf::PDFAppDocument::operationsChanged, &context, [runner]() { runner->stop(); });

    QVERIFY(runner->run(parameters));
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QVERIFY(!document->isBlockedByOperation());
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(finishedSpy.first().first().value<pdf::PDFDocumentToolResult>().status, pdf::PDFDocumentToolResultStatus::Stopped);
    QCOMPARE(startedSpy.size(), 0);

    const bool isCanceled = AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }, 2000);
    tool->setHold(false);
    QVERIFY(isCanceled);
    QCOMPARE(tool->getExecuteCount(), 0);
}

void AppToolShellTest::runnerLostDocumentRefusesRun()
{
    // A runner, whose document has gone, is no tool without a document
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool = std::make_shared<TestDocumentTool>(TOOL_ROTATE);
    pdf::PDFDocumentToolRunner runner(document.data(), tool, nullptr);
    QSignalSpy lostSpy(&runner, &PDFDocumentToolRunner::documentLost);

    delete tab;
    AppToolShellFixture::settle();
    QVERIFY(document.isNull());
    QCOMPARE(lostSpy.size(), 1);

    QVERIFY(!runner.run(AppToolShellFixture::parameters(QStringLiteral("title"))));
    QCOMPARE(runner.getState(), RunnerState::Idle);
    QTest::qWait(20);
    QCOMPARE(tool->getExecuteCount(), 0);
}

void AppToolShellTest::autoRefreshReaderDuringOperation()
{
    // F2: the file was being read when the run started: its result does not replace the
    // document under the run, the check is repeated after the decision
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refresh.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    refresh->setEnabled(true);

    const QByteArray bytes = ToolShellTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5));
    {
        QFile target(file);
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write(bytes);
    }

    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);
    refresh->checkNow();
    QVERIFY(refresh->isChecking());
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));
    QVERIFY(AppToolShellFixture::waitFor([refresh]() { return !refresh->isChecking(); }));
    AppToolShellFixture::settle();
    QCOMPARE(refresh->getReloadCount(), 0);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitForRun(runner));
    runner->endDecision();
    QVERIFY(AppToolShellFixture::waitFor([refresh]() { return refresh->getReloadCount() == 1; }));
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(5));
}

void AppToolShellTest::contextKnowsUnsavedChanges()
{
    // The size before of Compress is the bytes of the file only for an unchanged document
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(!PDFDocumentToolRunner::createContext(document->getSnapshot()).documents.front().isModified);

    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);
    pdf::PDFDocumentToolResult result;
    QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &result));
    QCOMPARE(runner->commitResult(result, QStringLiteral("Rotate"), nullptr), PDFDocumentToolRunner::CommitStatus::Committed);
    QVERIFY(document->isModified());
    QVERIFY(PDFDocumentToolRunner::createContext(document->getSnapshot()).documents.front().isModified);
}

void AppToolShellTest::runnerDocumentLeavesReady()
{
    // F2: the document leaves the state Ready (a reload): everything is canceled, the
    // operation ends, nothing waits for the worker and the open dialog goes
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);

    QSignalSpy lostSpy(runner, &PDFDocumentToolRunner::documentLost);
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    document->setPasswordRequired(file, false);
    QVERIFY(!document->isReady());
    QVERIFY(lostSpy.size() >= 1);
    QCOMPARE(runner->getState(), RunnerState::Idle);
    QVERIFY(!document->isBlockedByOperation());
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
    AppToolShellFixture::settle();
    QCOMPARE(finishedSpy.size(), 0);
    QVERIFY(!runner->run(AppToolShellFixture::parameters(QStringLiteral("title"))));
}

void AppToolShellTest::dialogClosedWhenDocumentLeavesReady()
{
    // F2: a result, whose document has left the state Ready, has no place to go
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    QPointer<PDFAppToolResultDialog> dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog.data(), &PDFAppToolResultDialog::decided);

    document->setPasswordRequired(file, false);
    QVERIFY(!document->isBlockedByOperation());
    QVERIFY(!dialog->isVisible());
    AppToolShellFixture::settle();
    QVERIFY(dialog.isNull());
    QCOMPARE(decidedSpy.size(), 0);
}

void AppToolShellTest::previewCacheBounds()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);

    // Plan chapter 3: at most one cached result holds a whole document
    {
        std::shared_ptr<TestDocumentTool> tool;
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS, &tool);
        tool->setPreview(true, true);
        QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::previewFinished);

        const QVariantMap first = AppToolShellFixture::parameters(QStringLiteral("title"), QStringLiteral("a"));
        const QVariantMap second = AppToolShellFixture::parameters(QStringLiteral("title"), QStringLiteral("b"));
        runner->requestPreview(first);
        QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 1; }));
        runner->requestPreview(second);
        QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 2; }));
        QCOMPARE(tool->getPreviewCount(), 2);

        // The latest is held, the older one computed again
        runner->requestPreview(second);
        QCOMPARE(finishedSpy.size(), 3);
        QCOMPARE(tool->getPreviewCount(), 2);
        runner->requestPreview(first);
        QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 4; }));
        QCOMPARE(tool->getPreviewCount(), 3);
        runner->cancelPreview();
    }

    // At most MAXIMUM_CACHED_PREVIEWS results without data, the oldest goes first
    {
        std::shared_ptr<TestDocumentTool> tool;
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
        QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::previewFinished);

        const int count = PDFDocumentToolRunner::MAXIMUM_CACHED_PREVIEWS + 1;
        for (int i = 0; i < count; ++i)
        {
            runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QString::number(i)));
            QVERIFY(AppToolShellFixture::waitFor([&finishedSpy, i]() { return finishedSpy.size() == i + 1; }));
        }
        QCOMPARE(tool->getPreviewCount(), count);

        // The second one is still there
        runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QString::number(1)));
        QCOMPARE(finishedSpy.size(), count + 1);
        QCOMPARE(tool->getPreviewCount(), count);

        // The first one has gone
        runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count"), QString::number(0)));
        QVERIFY(AppToolShellFixture::waitFor([&finishedSpy, count]() { return finishedSpy.size() == count + 2; }));
        QCOMPARE(tool->getPreviewCount(), count + 1);
        runner->cancelPreview();
    }
}

void AppToolShellTest::previewRequestedDuringRun()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    std::shared_ptr<TestDocumentTool> tool;
    PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE, &tool);
    tool->setHold(true);

    // The request waits for the end of the run and of the decision ...
    const QVariantMap parameters = AppToolShellFixture::parameters(QStringLiteral("title"));
    QSignalSpy finishedSpy(runner, &PDFDocumentToolRunner::runFinished);
    QSignalSpy previewSpy(runner, &PDFDocumentToolRunner::previewFinished);
    QVERIFY(runner->run(parameters));
    runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("count")));
    QTest::qWait(PDFDocumentToolRunner::PREVIEW_DELAY + 50);
    QCOMPARE(tool->getPreviewCount(), 0);

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 1; }));
    QCOMPARE(runner->getState(), RunnerState::Waiting);
    QTest::qWait(PDFDocumentToolRunner::PREVIEW_DELAY + 50);
    QCOMPARE(tool->getPreviewCount(), 0);

    // ... and is computed after it
    runner->endDecision();
    QVERIFY(AppToolShellFixture::waitFor([&previewSpy]() { return previewSpy.size() == 1; }));
    QCOMPARE(tool->getPreviewCount(), 1);

    // Stop means nothing computes behind the back of the user: no request, no preview
    tool->setHold(true);
    QVERIFY(runner->run(parameters));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    runner->stop();
    tool->setHold(false);
    QTest::qWait(PDFDocumentToolRunner::PREVIEW_DELAY + 50);
    QCOMPARE(tool->getPreviewCount(), 1);
    QCOMPARE(previewSpy.size(), 1);

    // A failed run serves a request made during it as well
    tool->setHold(true);
    QVERIFY(runner->run(AppToolShellFixture::parameters(QStringLiteral("fail"))));
    runner->requestPreview(AppToolShellFixture::parameters(QStringLiteral("nothing")));
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&previewSpy]() { return previewSpy.size() == 2; }));
    QCOMPARE(runner->getState(), RunnerState::Idle);
}

// ---------------------------------------------------------------- the writer

void AppToolShellTest::copyRuleByImpact()
{
    // F7: one rule of a copy by the signature impact
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(signedFile);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();
    QVERIFY(pdf::PDFAppDocumentFileWriter::getSignatureCount(snapshot.document.data()) > 0);

    using Writer = pdf::PDFAppToolResultWriter;
    const auto rotate = Writer::createCopyRequest(pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE), snapshot, snapshot.document, QStringLiteral("a.pdf"));
    QVERIFY(rotate.isIncremental);
    QCOMPARE(rotate.originalData, snapshot.originalData);
    QCOMPARE(rotate.filePath, QStringLiteral("a.pdf"));
    QCOMPARE(rotate.markSourceFilePath, snapshot.filePath);

    const auto compress = Writer::createCopyRequest(pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS), snapshot, snapshot.document, QStringLiteral("a.pdf"));
    QVERIFY(!compress.isIncremental);
    QVERIFY(compress.originalData.isEmpty());

    const auto unknown = Writer::createCopyRequest(nullptr, snapshot, snapshot.document, QStringLiteral("a.pdf"));
    QVERIFY(!unknown.isIncremental);

    // An unsigned result of a tool of IncrementalSave has nothing to keep
    pdf::PDFAppDocumentTab* plainTab = fixture.openDocument(2);
    const pdf::PDFAppDocumentSnapshot plain = plainTab->getDocument()->getSnapshot();
    QVERIFY(!Writer::createCopyRequest(pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE), snapshot, plain.document, QStringLiteral("a.pdf")).isIncremental);
}

void AppToolShellTest::writerRefusesDisplayVersion()
{
    // F5: the pages of a display version of an XFA form are not the pages of the file
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();
    snapshot.isPagedXFA = true;

    pdf::PDFAppToolResultWriter writer(nullptr);
    const QString target = fixture.path(QStringLiteral("xfa.pdf"));
    QVERIFY(!writer.writeCopy(pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE), snapshot, snapshot.document, target, QString(), false));
    QVERIFY(!writer.isWriting());
    QVERIFY(!QFileInfo::exists(target));

    // Nothing to write
    QVERIFY(!writer.writeDocument(pdf::PDFDocumentPointer(), target, QString(), false));
    QVERIFY(!writer.writeDocumentToMemory(pdf::PDFDocumentPointer()));
}

void AppToolShellTest::signedCopyWholeWithoutOldRevision()
{
    // O31, chapter 4: the copy of a tool of LostCopyOnly has no old revision in it
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(signedFile);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const QByteArray original = ToolShellTestHelper::readFile(signedFile);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getFileNameField()->setText(QStringLiteral("whole.pdf"));
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    const QByteArray written = ToolShellTestHelper::readFile(fixture.path(QStringLiteral("whole.pdf")));
    QVERIFY(!written.isEmpty());
    QCOMPARE(written.count("%%EOF"), qsizetype(1));
    QVERIFY(!written.contains("/Prev"));
    QVERIFY(!written.startsWith(original.left(original.size() - 16)));
    QCOMPARE(ToolShellTestHelper::titleOf(fixture.path(QStringLiteral("whole.pdf"))), QStringLiteral("Result title"));

    // The open document and its file stay as they are
    QCOMPARE(ToolShellTestHelper::readFile(signedFile), original);
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
}

void AppToolShellTest::signedCopyIncrementalOfRotate()
{
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(signedFile);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const QByteArray original = ToolShellTestHelper::readFile(signedFile);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    QVERIFY(!dialog->getSignedWarning());
    dialog->selectChoice(Choice::Copy);
    dialog->getFileNameField()->setText(QStringLiteral("incremental.pdf"));
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    // The signed revision stays byte by byte in the copy (F7)
    const QByteArray written = ToolShellTestHelper::readFile(fixture.path(QStringLiteral("incremental.pdf")));
    QVERIFY(written.size() > original.size());
    QVERIFY(written.startsWith(original));
    QVERIFY(written.contains("/Prev"));
}

void AppToolShellTest::signedCopyKeptWithoutFile_data()
{
    QTest::addColumn<QString>("step");
    QTest::addColumn<bool>("isIncremental");

    // Edit a Copy of a signed document, Compress, Keep (F5), then Save As: the signed bytes are
    // not the base of the kept result, the file is written whole (O31)
    QTest::newRow("kept") << QStringLiteral("kept") << false;
    QTest::newRow("kept-undone-redone") << QStringLiteral("redo") << false;
    QTest::newRow("kept-and-changed") << QStringLiteral("change") << false;

    // Undo brings the signed document back: its signatures stay by the incremental update
    QTest::newRow("kept-undone") << QStringLiteral("undo") << true;
}

void AppToolShellTest::signedCopyKeptWithoutFile()
{
    QFETCH(QString, step);
    QFETCH(bool, isIncremental);

    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* signedTab = fixture.open(signedFile);
    QVERIFY(signedTab && AppToolShellFixture::waitFor([signedTab]() { return signedTab->getDocument()->isReady(); }));
    const QByteArray original = ToolShellTestHelper::readFile(signedFile);

    signedTab->executeCommand(pdf::PDFAppCommands::editCopy());
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentTab* tab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(tab && tab != signedTab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->getFilePath().isEmpty());
    QCOMPARE(document->getOriginalData(), original);
    QVERIFY(pdf::PDFAppDocumentFileWriter::getSignatureCount(document->getDocument().data()) > 0);

    // F5: without a file the tool keeps its choice Keep and has nothing to overwrite
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QVERIFY(dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getOption(Choice::Overwrite));

    // The owner, 2026-11-02: the tool warns also here, by its own text - "Only a copy can be
    // saved" of a document with a file would not be true, Keep stays
    QWidget* warning = dialog->findChild<QWidget*>(QStringLiteral("signedWarning"));
    QVERIFY(warning);
    QCOMPARE(warning->findChild<pdf::PDFLabel*>(QStringLiteral("signedWarningTitle"))->text(), QStringLiteral("The signatures will stop being valid"));
    QCOMPARE(warning->findChild<pdf::PDFAppParagraph*>(QStringLiteral("signedWarningText"))->text(), QStringLiteral("This tool rewrites the whole file, so it cannot keep the signed revision. When you save the result, its signatures will no longer be valid."));

    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Result title"));

    if (step == QLatin1String("undo") || step == QLatin1String("redo"))
    {
        QVERIFY(document->getUndoStack()->undo());
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Result title"));
    }
    if (step == QLatin1String("redo"))
    {
        QVERIFY(document->getUndoStack()->redo());
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Result title"));
    }
    if (step == QLatin1String("change"))
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.addObject(pdf::PDFObject::createInteger(7));
        const pdf::PDFModifiedDocument modified(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::None);
        QVERIFY(document->commit(modified, QStringLiteral("Change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    }

    // The copy of a tool, which keeps the signed revision (F7), follows the same base
    const pdf::PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    const auto copyRequest = pdf::PDFAppToolResultWriter::createCopyRequest(pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE), snapshot, snapshot.document, QStringLiteral("a.pdf"));
    QCOMPARE(copyRequest.isIncremental, isIncremental);

    const QString target = fixture.path(QStringLiteral("saved-as.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    const bool isFinished = AppToolShellFixture::waitFor([&finishedSpy]() { return !finishedSpy.isEmpty(); });
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    QVERIFY(isFinished);
    QVERIFY(finishedSpy.first().first().toBool());

    const QByteArray written = ToolShellTestHelper::readFile(target);
    QVERIFY(!written.isEmpty());
    QCOMPARE(written.startsWith(original), isIncremental);
    QCOMPARE(written.contains("/Prev"), isIncremental);
    if (!isIncremental)
    {
        QCOMPARE(written.count("%%EOF"), qsizetype(1));
    }

    // The bytes of the file have become the base of the document
    QCOMPARE(document->getOriginalData(), written);
    QVERIFY(!document->isWholeFileRewritten());
}

void AppToolShellTest::encryptedCopyStaysEncrypted()
{
    AppToolShellFixture fixture;
    const QString encrypted = pdftest::AppDocumentSamples::ownerPassword(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(encrypted);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ANALYSIS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Copy);
    dialog->getFileNameField()->setText(QStringLiteral("encrypted-copy.pdf"));
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    // The writer encrypts the copy again by the handler of the document
    const QByteArray written = ToolShellTestHelper::readFile(fixture.path(QStringLiteral("encrypted-copy.pdf")));
    QVERIFY(written.contains("/Encrypt"));
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocument document = reader.readFromFile(fixture.path(QStringLiteral("encrypted-copy.pdf")));
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QVERIFY(document.getStorage().getSecurityHandler()->getMode() != pdf::EncryptionMode::None);
    QCOMPARE(document.getInfo()->title, QStringLiteral("Result title"));
}

// ---------------------------------------------------------------- the dialog

void AppToolShellTest::dialogChoicesRules_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<int>("kind");
    QTest::addColumn<bool>("hasFile");
    QTest::addColumn<bool>("isSigned");
    QTest::addColumn<bool>("isLocked");
    QTest::addColumn<QList<int>>("choices");
    QTest::addColumn<int>("preselected");

    const int change = int(PDFAppToolResultDialog::ResultKind::ChangeDocument);
    const int keep = int(Choice::Keep);
    const int copy = int(Choice::Copy);
    const int overwrite = int(Choice::Overwrite);

    QTest::newRow("compress: copy preselected (296:2761)") << TOOL_COMPRESS << change << true << false << false << QList<int>({ keep, copy, overwrite }) << copy;
    QTest::newRow("rotate: keep preselected (389:21590)") << TOOL_ROTATE << change << true << false << false << QList<int>({ keep, copy, overwrite }) << keep;
    QTest::newRow("compress signed: only a copy (O31)") << TOOL_COMPRESS << change << true << true << false << QList<int>({ copy }) << copy;
    QTest::newRow("rotate signed: all choices") << TOOL_ROTATE << change << true << true << false << QList<int>({ keep, copy, overwrite }) << keep;
    QTest::newRow("no file: no overwrite (F5)") << TOOL_ROTATE << change << false << false << false << QList<int>({ keep, copy }) << keep;
    QTest::newRow("no file signed: no forced copy (F5)") << TOOL_COMPRESS << change << false << true << false << QList<int>({ keep, copy }) << copy;
    QTest::newRow("locked: a copy only") << TOOL_ROTATE << change << true << false << true << QList<int>({ copy }) << copy;
    QTest::newRow("new document (378:3674)") << TOOL_MERGE << int(PDFAppToolResultDialog::ResultKind::NewDocument) << true << false << false << QList<int>({ int(Choice::NewTab), int(Choice::File) }) << int(Choice::NewTab);
    QTest::newRow("export (339:9601)") << TOOL_EXTRACT << int(PDFAppToolResultDialog::ResultKind::Files) << true << false << false << QList<int>({ int(Choice::File), int(Choice::Clipboard) }) << int(Choice::File);
    QTest::newRow("analysis") << TOOL_ANALYSIS << int(PDFAppToolResultDialog::ResultKind::Analysis) << true << false << false << QList<int>() << keep;
}

void AppToolShellTest::dialogChoicesRules()
{
    QFETCH(QString, toolId);
    QFETCH(int, kind);
    QFETCH(bool, hasFile);
    QFETCH(bool, isSigned);
    QFETCH(bool, isLocked);
    QFETCH(QList<int>, choices);
    QFETCH(int, preselected);

    PDFAppToolResultDialog::DocumentState state;
    state.hasFile = hasFile;
    state.isSigned = isSigned;
    state.isLocked = isLocked;

    const pdf::PDFDocumentToolCatalogEntry* tool = pdf::PDFDocumentToolCatalog::findTool(toolId);
    const std::vector<Choice> found = PDFAppToolResultDialog::getChoices(tool, PDFAppToolResultDialog::ResultKind(kind), state);
    QList<int> foundInts;
    for (const Choice choice : found)
    {
        foundInts << int(choice);
    }
    QCOMPARE(foundInts, choices);
    if (!found.empty())
    {
        QCOMPARE(int(PDFAppToolResultDialog::getPreselectedChoice(tool, found)), preselected);
    }
}

void AppToolShellTest::dialogGeometry_data()
{
    QTest::addColumn<QString>("frame");
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("cardHeightWithoutContinue");
    QTest::addColumn<QList<int>>("optionHeights");

    // Heights of the frames without the row of the suggestions, which depends on the tool
    QTest::newRow("296:2761 compress") << QStringLiteral("296:2761") << TOOL_COMPRESS << QStringLiteral("title") << 692 - 91 - 14 << QList<int>({ 76, 175, 76 });
    QTest::newRow("339:9601 extract") << QStringLiteral("339:9601") << TOOL_EXTRACT << QStringLiteral("files") << 447 << QList<int>({ 133, 60 });
    QTest::newRow("378:3674 merge") << QStringLiteral("378:3674") << TOOL_MERGE << QStringLiteral("new-document") << 481 - 93 - 14 << QList<int>({ 60, 60 });
}

void AppToolShellTest::dialogGeometry()
{
    QFETCH(QString, frame);
    QFETCH(QString, toolId);
    QFETCH(QString, mode);
    QFETCH(int, cardHeightWithoutContinue);
    QFETCH(QList<int>, optionHeights);

    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    // The screen of the platform offscreen is 800 x 800: the card must stay on it
    fixture.window->move(-400, -100);
    AppToolShellFixture::settle();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, toolId, mode, { QStringLiteral("Size"), QStringLiteral("24.8 MB → 6.1 MB (75 % smaller)"),
                                                                             QStringLiteral("Images"), QStringLiteral("398 recompressed · 14 kept"),
                                                                             QStringLiteral("Time"), QStringLiteral("1 min 12 s") });
    QVERIFY(dialog);
    QVERIFY(dialog->isModal());

    // The card: 520 wide, centered on the canvas, the scrim only over the canvas (312:7373)
    const QRect card = AppToolShellFixture::getCardRect(dialog);
    QCOMPARE(card.width(), 520);

    QWidget* canvas = tab->getView()->getCanvasArea();
    QCOMPARE(dialog->getScrimTarget(), canvas);
    QVERIFY(dialog->getScrim());
    QCOMPARE(dialog->getScrim()->size(), canvas->size());
    const QPoint canvasCenter = canvas->mapToGlobal(canvas->rect().center());
    const QPoint cardCenter = dialog->mapToGlobal(card.center());
    QVERIFY2(qAbs(canvasCenter.x() - cardCenter.x()) <= 1 && qAbs(canvasCenter.y() - cardCenter.y()) <= 1, qPrintable(frame));

    // The head, the facts and the options: padding 22 / 20 inside of the border
    using Helper = ToolShellTestHelper;
    const QWidget* head = dialog->findChild<QWidget*>(QStringLiteral("toolResultHead"));
    QVERIFY(head);
    QCOMPARE(Helper::inCard(dialog, head), QRect(23, 21, 474, 36));
    QCOMPARE(dialog->findChild<QWidget*>(QStringLiteral("toolResultBadge"))->size(), QSize(36, 36));
    QCOMPARE(dialog->findChild<QWidget*>(QStringLiteral("toolResultTitle"))->height(), 19);
    QCOMPARE(dialog->findChild<QWidget*>(QStringLiteral("toolResultSubtitle"))->height(), 15);

    QCOMPARE(Helper::inCard(dialog, dialog->getFactsWidget()), QRect(23, 71, 474, 92));
    QCOMPARE(dialog->getFacts()->getRowCount(), 3);
    QCOMPARE(dialog->getFacts()->getRowRect(0).height(), 28);
    QCOMPARE(dialog->getFacts()->width(), 450);

    int y = 177;
    const std::vector<Choice> choices = toolId == TOOL_COMPRESS ? std::vector<Choice>{ Choice::Keep, Choice::Copy, Choice::Overwrite }
                                                                : (toolId == TOOL_EXTRACT ? std::vector<Choice>{ Choice::File, Choice::Clipboard } : std::vector<Choice>{ Choice::NewTab, Choice::File });
    for (size_t i = 0; i < choices.size(); ++i)
    {
        const pdf::PDFAppToolResultOption* option = dialog->getOption(choices[i]);
        QVERIFY(option);
        QCOMPARE(Helper::inCard(dialog, option), QRect(23, y, 474, optionHeights[int(i)]));

        // The description in lines of 16 on every page (312:7399, 339:10300, 378:4336)
        const pdf::PDFAppParagraph* description = option->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultOptionDescription"));
        QVERIFY(description);
        QCOMPARE(description->getLineHeight(), 16);
        y += optionHeights[int(i)] + 6;
    }
    y -= 6;

    // Save as a copy: the controls of 312:7408
    if (toolId == TOOL_COMPRESS)
    {
        const pdf::PDFAppToolResultOption* option = dialog->getOption(Choice::Copy);
        QCOMPARE(option->getTag()->height(), 21);
        QCOMPARE(dialog->getFileNameField()->size(), QSize(422, 34));
        QCOMPARE(dialog->getBrowseButton()->height(), 36);

        // 118 of the frame = the border 2 + the paddings 16 / 18 + the icon 16 + its gap 8 +
        // the label 58. The paddings are exact, the label is as wide as Qt measures it
        // (hinted advances: 60 against 58 of Figma)
        QCOMPARE(dialog->getBrowseButton()->width(), 2 + 16 + 18 + 16 + 8 + dialog->getBrowseButton()->fontMetrics().size(Qt::TextShowMnemonic, QStringLiteral("Browse…")).width());
        QVERIFY(qAbs(dialog->getBrowseButton()->width() - 118) <= 2);
        QCOMPARE(dialog->getOpenCheckBox()->height(), 18);
        QCOMPARE(dialog->getFolderLabel()->mapTo(dialog, QPoint(0, 0)).x(), dialog->getFileNameField()->mapTo(dialog, QPoint(0, 0)).x());
        QCOMPARE(dialog->getFolderLabel()->width() + 8 + dialog->getBrowseButton()->width(), 422);
    }
    else if (toolId == TOOL_EXTRACT)
    {
        // 355:9882: the path 322 x 36, Browse… 94 x 36 = the border 2 + the paddings 16 / 18 +
        // the label 58 of Figma (Qt measures it 60); together they are the row
        QCOMPARE(dialog->getFilePathField()->height(), 36);
        QCOMPARE(dialog->getBrowseButton()->height(), 36);
        QCOMPARE(dialog->getBrowseButton()->width(), 2 + 16 + 18 + dialog->getBrowseButton()->fontMetrics().size(Qt::TextShowMnemonic, QStringLiteral("Browse…")).width());
        QVERIFY(qAbs(dialog->getBrowseButton()->width() - 94) <= 2);
        QCOMPARE(dialog->getFilePathField()->width() + 6 + dialog->getBrowseButton()->width(), 422);
        QVERIFY(!dialog->getContinueWith());
    }

    // Actions 36 at the end of the card, Discard at the leading edge, the confirmation
    // at the trailing one
    QCOMPARE(dialog->getDiscardButton()->height(), 36);
    QCOMPARE(dialog->getConfirmButton()->height(), 36);
    QCOMPARE(Helper::inCard(dialog, dialog->getDiscardButton()).left(), 23);
    QVERIFY(qAbs(Helper::inCard(dialog, dialog->getDiscardButton()).width() - 148) <= 2);
    QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).right(), 23 + 474 - 1);
    QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).bottom(), card.height() - 21 - 1);

    // Then continue with above the actions (K3), gaps 14
    int cardHeight = cardHeightWithoutContinue;
    if (dialog->getContinueWith())
    {
        const QRect continueRect = Helper::inCard(dialog, dialog->getContinueWith());
        QCOMPARE(continueRect.top(), y + 14);
        QCOMPARE(continueRect.width(), 474);
        QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).top(), continueRect.bottom() + 1 + 14);
        cardHeight += continueRect.height() + 14;
    }
    QCOMPARE(card.height(), cardHeight);

    // No scrolling in the window of the frames
    QVERIFY(!dialog->getScrollArea()->verticalScrollBar()->isVisible() || dialog->getScrollArea()->verticalScrollBar()->maximum() == 0);
}

void AppToolShellTest::dialogTexts()
{
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    // 296:2761 Compress
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Compress finished"));

    // The own head is the only head, also after the texts were set
    int visibleTitles = 0;
    for (const pdf::PDFLabel* label : dialog->findChildren<pdf::PDFLabel*>())
    {
        visibleTitles += label->isVisible() && label->text() == QStringLiteral("Nothing has been written yet – choose what to do with the result.") ? 1 : 0;
    }
    QCOMPARE(visibleTitles, 1);
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultSubtitle"))->text(), QStringLiteral("Nothing has been written yet – choose what to do with the result."));
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    QCOMPARE(dialog->getOption(Choice::Keep)->getRadioButton()->text(), QStringLiteral("Keep in this tab"));
    QCOMPARE(dialog->getOption(Choice::Keep)->getDescription(), QStringLiteral("The result replaces the document in this tab. It is not saved yet and Undo brings the original back."));
    QCOMPARE(dialog->getOption(Choice::Copy)->getRadioButton()->text(), QStringLiteral("Save as a copy"));
    QCOMPARE(dialog->getOption(Choice::Copy)->getDescription(), QStringLiteral("A new file is written, the open document and its file stay as they are."));
    QCOMPARE(dialog->getOption(Choice::Copy)->getTag()->text(), QStringLiteral("Recommended"));
    QVERIFY(dialog->getOption(Choice::Copy)->getTag()->isVisible());
    QVERIFY(!dialog->getOption(Choice::Keep)->getTag()->isVisibleTo(dialog));
    QCOMPARE(dialog->getOption(Choice::Overwrite)->getRadioButton()->text(), QStringLiteral("Overwrite the original file"));
    QCOMPARE(dialog->getOption(Choice::Overwrite)->getDescription(), QStringLiteral("The result replaces the document and is saved to Project_Report.pdf right away."));
    QCOMPARE(dialog->getOpenCheckBox()->text(), QStringLiteral("Open the copy in a new tab"));
    QVERIFY(dialog->getOpenCheckBox()->isChecked());
    QCOMPARE(dialog->getBrowseButton()->text(), QStringLiteral("Browse…"));
    QCOMPARE(dialog->getFolderLabel()->getPath(), QDir::toNativeSeparators(QFileInfo(file).absolutePath()));
    QCOMPARE(dialog->getDiscardButton()->text(), QStringLiteral("Discard result"));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy"));
    QCOMPARE(dialog->getStatusText(), QStringLiteral("Compress finished · the result is waiting for your decision"));
    QCOMPARE(dialog->getContinueWith()->getTitle(), QStringLiteral("Then continue with"));
    QCOMPARE(dialog->getContinueWith()->getTitleNote(), QStringLiteral("optional"));
    QCOMPARE(dialog->getContinueWith()->getLinkText(), QStringLiteral("Another tool…"));
    QCOMPARE(dialog->getContinueWith()->getCurrentCaption(), QStringLiteral("Nothing is chosen – the tool just finishes."));
    QCOMPARE(dialog->getContinueWith()->getChoices().size(), qsizetype(3));
    QCOMPARE(dialog->getContinueWith()->getChoices().front().name, pdf::PDFDocumentToolCatalog::findTool(TOOL_MERGE)->getTitle());
    QCOMPARE(dialog->getContinueWith()->getSelectedChoice(), -1);
    dialog->reject();
    AppToolShellFixture::settle();

    // 389:21590 Rotate: Keep preselected and the title of the tool
    {
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);
        pdf::PDFAppToolResultInput input;
        input.tool = pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE);
        input.title = QStringLiteral("Pages rotated");
        QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &input.result));
        PDFAppToolResultDialog* rotateDialog = PDFAppToolResultDialog::open(tab, runner, input);
        AppToolShellFixture::settle();
        QCOMPARE(rotateDialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Pages rotated"));
        QCOMPARE(rotateDialog->getSelectedChoice(), Choice::Keep);
        QCOMPARE(rotateDialog->getOption(Choice::Keep)->getTag()->text(), QStringLiteral("Recommended"));
        QCOMPARE(rotateDialog->getConfirmButton()->text(), QStringLiteral("Keep in This Tab"));

        // Save as a copy unselected is the height of one line (389:21590)
        QCOMPARE(rotateDialog->getOption(Choice::Copy)->height(), 60);
        QVERIFY(!rotateDialog->getFileNameField()->isVisible());
        rotateDialog->reject();
        AppToolShellFixture::settle();
    }

    // 339:9601 the texts of an export come from the tool
    {
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_EXTRACT);
        pdf::PDFAppToolResultInput input;
        input.tool = pdf::PDFDocumentToolCatalog::findTool(TOOL_EXTRACT);
        input.subtitle = QStringLiteral("Nothing has been written yet – choose what to do with the text.");
        input.fileDescription = QStringLiteral("The text is written to a new .txt file. The document is not changed.");
        input.fileConfirmText = QStringLiteral("Save Text");
        QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("files")), &input.result));
        PDFAppToolResultDialog* extractDialog = PDFAppToolResultDialog::open(tab, runner, input);
        AppToolShellFixture::settle();
        QCOMPARE(extractDialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), pdf::PDFDocumentToolCatalog::findTool(TOOL_EXTRACT)->getTitle() + QStringLiteral(" finished"));
        QCOMPARE(extractDialog->getOption(Choice::File)->getRadioButton()->text(), QStringLiteral("Save as a file"));
        QCOMPARE(extractDialog->getOption(Choice::File)->getDescription(), input.fileDescription);
        QCOMPARE(extractDialog->getOption(Choice::File)->getTag()->text(), QStringLiteral("Recommended"));
        QCOMPARE(extractDialog->getOption(Choice::Clipboard)->getRadioButton()->text(), QStringLiteral("Copy to clipboard"));
        QCOMPARE(extractDialog->getOption(Choice::Clipboard)->getDescription(), QStringLiteral("The whole text is put on the clipboard. Nothing is written to disk."));
        QCOMPARE(extractDialog->getOpenCheckBox()->text(), QStringLiteral("Open the folder afterwards"));
        QVERIFY(!extractDialog->getOpenCheckBox()->isChecked());
        QCOMPARE(extractDialog->getConfirmButton()->text(), QStringLiteral("Save Text"));
        QVERIFY(extractDialog->getFilePathField()->text().endsWith(QStringLiteral("extracted.txt")));
        extractDialog->reject();
        AppToolShellFixture::settle();
    }

    // 378:3674: no tag of the new document
    PDFAppToolResultDialog* mergeDialog = fixture.openDialog(tab, TOOL_MERGE, QStringLiteral("new-document"));
    QVERIFY(mergeDialog);
    QCOMPARE(mergeDialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultSubtitle"))->text(), QStringLiteral("The new document is ready – choose what to do with it."));
    QCOMPARE(mergeDialog->getStatusText(), QStringLiteral("Merge finished · the new document is waiting for your decision"));
    QCOMPARE(mergeDialog->getOption(Choice::NewTab)->getRadioButton()->text(), QStringLiteral("Open in a new tab"));
    QCOMPARE(mergeDialog->getOption(Choice::NewTab)->getDescription(), QStringLiteral("The new document opens unsaved. Save it when you are ready."));
    QCOMPARE(mergeDialog->getOption(Choice::File)->getDescription(), QStringLiteral("The document is written to a file of your choice."));
    QCOMPARE(mergeDialog->getOption(Choice::NewTab)->getTag()->text(), QString());
    QCOMPARE(mergeDialog->getConfirmButton()->text(), QStringLiteral("Open Document"));
    QCOMPARE(mergeDialog->getContinueWith()->getChoices().size(), 3);
    mergeDialog->reject();
}

void AppToolShellTest::dialogSelectionTexts()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);

    // The button is named by the result (tools.md 9.3 points 7, 12)
    dialog->selectChoice(Choice::Keep);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Keep in This Tab"));
    QVERIFY(!dialog->getFileNameField()->isVisible());
    dialog->getContinueWith()->setSelectedChoice(0);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Keep and Continue"));
    QCOMPARE(dialog->getContinueWith()->getCurrentCaption().contains(dialog->getContinueWith()->getChoices().front().name), true);

    dialog->selectChoice(Choice::Copy);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy and Continue"));
    QVERIFY(dialog->getFileNameField()->isVisible());
    dialog->getContinueWith()->setSelectedChoice(-1);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy"));

    dialog->selectChoice(Choice::Overwrite);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Overwrite File"));

    // A click anywhere on the card selects it
    QTest::mouseClick(dialog->getOption(Choice::Keep), Qt::LeftButton, Qt::NoModifier, QPoint(300, dialog->getOption(Choice::Keep)->height() - 5));
    QCOMPARE(dialog->getSelectedChoice(), Choice::Keep);
    QVERIFY(dialog->getOption(Choice::Keep)->isSelected());
    QVERIFY(!dialog->getOption(Choice::Overwrite)->isSelected());
    dialog->reject();
}

void AppToolShellTest::dialogPixels_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("ratio");

    for (const bool isDark : { false, true })
    {
        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QTest::addRow("%s-%g", isDark ? "dark" : "light", ratio) << isDark << ratio;
        }
    }
}

void AppToolShellTest::dialogPixels()
{
    QFETCH(bool, isDark);
    QFETCH(qreal, ratio);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB → 6.1 MB") });
    QVERIFY(dialog);

    // The context of the fixture sets the scheme of its settings
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface).lightness() < 128, isDark);
    AppToolShellFixture::settle();

    const QImage image = AppToolShellFixture::render(dialog, ratio);
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);

    using Helper = ToolShellTestHelper;
    const QPoint origin = dialog->rect().topLeft();
    const QRect card = AppToolShellFixture::getCardRect(dialog);

    // The surface of the card, the panel of the facts (bg/subtle), the selected option
    // (bg/accent 13 % over bg/surface) with its border border/focus, an unselected one
    // bg/surface with border/default, the badge accent/green/bg
    const auto mapPoint = [&](const QWidget* widget, const QPoint& point) { return widget->mapTo(dialog, point) - origin; };
    const QColor surface = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
    QVERIFY(Helper::isNear(Helper::pixel(image, card.topLeft() + QPoint(10, card.height() / 2)), surface));
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(dialog->getFactsWidget(), QPoint(6, 18))), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSubtle)));

    const pdf::PDFAppToolResultOption* selected = dialog->getOption(Choice::Copy);
    const QColor accent = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgAccent);
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(selected, QPoint(6, 60))), Helper::overSurface(accent, 0.13), 3));
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(selected, QPoint(0, 60))), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BorderFocus), 3));

    const pdf::PDFAppToolResultOption* unselected = dialog->getOption(Choice::Keep);
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(unselected, QPoint(6, 40))), surface));
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(unselected, QPoint(0, 40))), pdf::PDFStylePainter::color(pdf::PDFThemeColors::BorderDefault), 3));

    const QWidget* badge = dialog->findChild<QWidget*>(QStringLiteral("toolResultBadge"));
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(badge, QPoint(4, 18))), pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Green, pdf::PDFAccentHueColors::Role::Background))));

    // The Primary button
    QVERIFY(Helper::isNear(Helper::pixel(image, mapPoint(dialog->getConfirmButton(), QPoint(5, 18))), accent, 3));

    // The write error in the red of 316:7764
    QFile::remove(fixture.path(QStringLiteral("missing")));
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&) { return fixture.path(QStringLiteral("missing")); });
    dialog->getBrowseButton()->click();
    QSignalSpy statusSpy(dialog, &PDFAppToolResultDialog::statusTextChanged);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return dialog->getWriteError() && dialog->getWriteError()->isVisible(); }));
    AppToolShellFixture::settle();
    const QImage errorImage = AppToolShellFixture::render(dialog, ratio);
    QCOMPARE(AppToolShellFixture::countLeakPixels(errorImage), 0);
    QVERIFY(Helper::isNear(Helper::pixel(errorImage, mapPoint(dialog->getWriteError(), QPoint(6, dialog->getWriteError()->height() / 2))),
                           pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Red, pdf::PDFAccentHueColors::Role::Background))));
    dialog->reject();
}

void AppToolShellTest::dialogRightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB") });
    QVERIFY(dialog);
    QVERIFY(dialog->isRightToLeft());

    using Helper = ToolShellTestHelper;
    const QRect card = AppToolShellFixture::getCardRect(dialog);

    // Mirrored: the discard button at the right edge, the confirmation at the left one,
    // the radio button at the right side of its card and the details indented from the right
    QCOMPARE(Helper::inCard(dialog, dialog->getDiscardButton()).right(), card.width() - 23 - 1);
    QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).left(), 23);
    const pdf::PDFAppToolResultOption* option = dialog->getOption(Choice::Copy);
    const QRadioButton* radio = option->getRadioButton();
    QCOMPARE(radio->mapTo(option, QPoint(radio->width(), 0)).x(), option->width() - pdf::PDFAppToolResultOption::PADDING_H);
    const QRect field = dialog->getFileNameField()->geometry();
    QCOMPARE(dialog->getFileNameField()->mapTo(option, QPoint(field.width(), 0)).x(), option->width() - pdf::PDFAppToolResultOption::PADDING_H - pdf::PDFAppToolResultOption::DETAIL_INDENT);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);
    dialog->reject();
}

void AppToolShellTest::dialogMinimalWindowScrolls()
{
    AppToolShellFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB"), QStringLiteral("Time"), QStringLiteral("1 s") });
    QVERIFY(dialog);

    // The dialog of the height of 692 scrolls in the body of the window; the card does
    // not fit the canvas, so it covers the whole body of the window
    QWidget* host = pdf::PDFAppMainWindow::findDialogHost(fixture.window.data());
    QVERIFY(host);
    QCOMPARE(dialog->getScrimTarget(), host);

    const QRect card = AppToolShellFixture::getCardRect(dialog);
    QCOMPARE(card.width(), 520);
    QVERIFY(card.height() <= host->height());
    const QRect globalCard(dialog->mapToGlobal(card.topLeft()), card.size());
    const QRect globalHost(host->mapToGlobal(QPoint(0, 0)), host->size());
    QVERIFY2(globalHost.contains(globalCard), qPrintable(QStringLiteral("%1,%2 %3x%4").arg(globalCard.x()).arg(globalCard.y()).arg(globalCard.width()).arg(globalCard.height())));

    QScrollBar* scrollBar = dialog->getScrollArea()->verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);

    // The buttons stay visible; every part can be reached by the scrolling
    QVERIFY(dialog->getConfirmButton()->isVisible());
    QVERIFY(ToolShellTestHelper::inCard(dialog, dialog->getConfirmButton()).bottom() < card.height());
    scrollBar->setValue(scrollBar->maximum());
    AppToolShellFixture::settle();
    const QWidget* continueWith = dialog->getContinueWith();
    const QPoint continueBottom = continueWith->mapTo(dialog->getScrollArea()->viewport(), QPoint(0, continueWith->height()));
    QVERIFY(continueBottom.y() <= dialog->getScrollArea()->viewport()->height());

    // A window growing to the size of the frame ends the scrolling
    fixture.window->resize(1440, 960);
    AppToolShellFixture::settle();
    QCOMPARE(dialog->getScrollArea()->verticalScrollBar()->maximum(), 0);
    dialog->reject();
}

void AppToolShellTest::dialogKeyboard()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    // Esc discards the result
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        QTest::keyClick(dialog, Qt::Key_Escape);
        QCOMPARE(decidedSpy.size(), 1);
        QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Discarded);
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
    }

    // The first focus is on the Primary button, Enter confirms
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
        QVERIFY(dialog);
        QVERIFY(dialog->getConfirmButton()->isDefault());
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        QTest::keyClick(dialog, Qt::Key_Return);
        QCOMPARE(decidedSpy.size(), 1);
        QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Kept);
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);
    }

    // Enter in the field of the name confirms too; Esc during a write does nothing: the
    // write cannot be taken back
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        dialog->getFileNameField()->setText(QStringLiteral("keyboard.pdf"));
        dialog->getOpenCheckBox()->setChecked(false);
        dialog->getFileNameField()->setFocus();
        QTest::keyClick(dialog->getFileNameField(), Qt::Key_Return);
        QVERIFY(dialog->isWriting());
        QVERIFY(!dialog->getConfirmButton()->isEnabled());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QCOMPARE(decidedSpy.size(), 0);
        QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
        QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::CopySaved);
        QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("keyboard.pdf"))));
    }
}

void AppToolShellTest::dialogAccessibility()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);

    // The dialog is announced by its title and subtitle (the head is an own widget)
    QAccessibleInterface* dialogInterface = QAccessible::queryAccessibleInterface(dialog);
    QVERIFY(dialogInterface);
    QCOMPARE(dialogInterface->text(QAccessible::Name), QStringLiteral("Compress finished"));
    QCOMPARE(dialogInterface->text(QAccessible::Description), QStringLiteral("Nothing has been written yet – choose what to do with the result."));

    // A choice is a radio button named by its title and described by its text
    const QRadioButton* radio = dialog->getOption(Choice::Keep)->getRadioButton();
    QAccessibleInterface* radioInterface = QAccessible::queryAccessibleInterface(const_cast<QRadioButton*>(radio));
    QVERIFY(radioInterface);
    QCOMPARE(radioInterface->role(), QAccessible::RadioButton);
    QCOMPARE(radioInterface->text(QAccessible::Name), QStringLiteral("Keep in this tab"));
    QCOMPARE(radioInterface->text(QAccessible::Description), dialog->getOption(Choice::Keep)->getDescription());

    // One exclusive group: the arrows move the choice
    dialog->getOption(Choice::Keep)->getRadioButton()->setFocus();
    QTest::keyClick(dialog->getOption(Choice::Keep)->getRadioButton(), Qt::Key_Down);
    QCOMPARE(dialog->focusWidget(), dialog->getOption(Choice::Copy)->getRadioButton());
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    dialog->reject();
}

void AppToolShellTest::dialogLanguageChange()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB") });
    QVERIFY(dialog);
    dialog->getFileNameField()->setText(QStringLiteral("typed-name.pdf"));
    dialog->selectChoice(Choice::Keep);
    dialog->selectChoice(Choice::Copy);

    ToolShellTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();

    // Every text follows; the choice, the typed name and the facts of the run stay
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("XX Save Copy"));
    QCOMPARE(dialog->getDiscardButton()->text(), QStringLiteral("XX Discard result"));
    QCOMPARE(dialog->getOption(Choice::Keep)->getRadioButton()->text(), QStringLiteral("XX Keep in this tab"));
    QVERIFY(dialog->getOption(Choice::Copy)->getDescription().startsWith(QStringLiteral("XX ")));
    QCOMPARE(dialog->getOption(Choice::Copy)->getTag()->text(), QStringLiteral("XX Recommended"));
    QCOMPARE(dialog->getOpenCheckBox()->text(), QStringLiteral("XX Open the copy in a new tab"));
    QVERIFY(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultSubtitle"))->text().startsWith(QStringLiteral("XX ")));
    QCOMPARE(dialog->getContinueWith()->getTitle(), QStringLiteral("XX Then continue with"));
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("typed-name.pdf"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("24.8 MB"));

    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy"));
    dialog->reject();
}

void AppToolShellTest::dialogHoldsOperation()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);

    // F2: while the user reads the facts nothing changes the document
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(!document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation), QStringLiteral("change"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));

    // ... and the tab is not asked about a running operation: nothing runs
    for (const pdf::PDFAppDocumentOperation* operation : document->getOperations())
    {
        QVERIFY(!operation->isAskedBeforeClose());
    }
    QCOMPARE(tab->getTabProgress(), -1.0);

    // The operation ends just before the commit
    QSignalSpy operationsSpy(document, &pdf::PDFAppDocument::operationsChanged);
    QSignalSpy changedSpy(document, &pdf::PDFAppDocument::documentChanged);
    bool wasBlockedAtChange = true;
    QObject context;
    QObject::connect(document, &pdf::PDFAppDocument::documentChanged, &context, [&]() { wasBlockedAtChange = document->isBlockedByOperation(); });
    dialog->confirm();
    QCOMPARE(changedSpy.size(), 1);
    QCOMPARE(operationsSpy.size(), 1);
    QVERIFY(!wasBlockedAtChange);
    QVERIFY(!document->isBlockedByOperation());
}

void AppToolShellTest::dialogKeep()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString title = pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE)->getTitle();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QPointer<PDFAppToolResultDialog> guard(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getContinueWith()->setSelectedChoice(0);
    const QString nextToolId = dialog->getContinueToolIds().front();
    dialog->confirm();

    QCOMPARE(decidedSpy.size(), 1);
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QCOMPARE(outcome.action, Action::Kept);
    QCOMPARE(outcome.continueToolId, nextToolId);
    QCOMPARE(outcome.continueDocumentId, document->getId());

    // One undo step named by the tool (2.4), undone back to the original; not saved
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), title);
    QVERIFY(document->isModified());
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Result title"));

    // The dialog goes away
    AppToolShellFixture::settle();
    QVERIFY(guard.isNull());
}

void AppToolShellTest::dialogDiscard()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    const quint64 version = document->getVersion();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getContinueWith()->setSelectedChoice(0);
    dialog->getDiscardButton()->click();

    // Nothing happens to the document, the next tool does not run (tools.md 9.3 point 12)
    QCOMPARE(decidedSpy.size(), 1);
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QCOMPARE(outcome.action, Action::Discarded);
    QVERIFY(!outcome.isAccepted());
    QCOMPARE(document->getVersion(), version);
    QVERIFY(!document->isBlockedByOperation());
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
}

void AppToolShellTest::dialogCopyAndContinue()
{
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    const QByteArray original = ToolShellTestHelper::readFile(file);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getFileNameField()->setText(QStringLiteral("Report_small"));
    dialog->getContinueWith()->setSelectedChoice(0);
    QCOMPARE(dialog->getTargetFilePath(), fixture.path(QStringLiteral("Report_small.pdf")));
    QVERIFY(dialog->getFileNameField()->isVisible());
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    // A new file; the open document and its file stay as they are
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QCOMPARE(outcome.action, Action::CopySaved);
    QCOMPARE(QFileInfo(outcome.filePath).canonicalFilePath(), QFileInfo(fixture.path(QStringLiteral("Report_small.pdf"))).canonicalFilePath());
    QCOMPARE(ToolShellTestHelper::titleOf(outcome.filePath), QStringLiteral("Result title"));
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
    QVERIFY(!document->isModified());
    QVERIFY(!document->isBlockedByOperation());

    // The copy opens in a new tab and the next tool runs on it (F11)
    QVERIFY(AppToolShellFixture::waitFor([&]() { return fixture.context->getDocumentRegistry()->findDocument(outcome.continueDocumentId) != nullptr; }));
    QVERIFY(outcome.continueDocumentId != document->getId());
    QCOMPARE(outcome.continueToolId, TOOL_MERGE);
    const pdf::PDFAppDocument* opened = fixture.context->getDocumentRegistry()->findDocument(outcome.continueDocumentId);
    QCOMPARE(QFileInfo(opened->getFilePath()).fileName(), QStringLiteral("Report_small.pdf"));
    QCOMPARE(fixture.currentDocumentTab()->getDocument(), opened);
}

void AppToolShellTest::dialogCopyOpenOffHidesContinue()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const int tabCount = fixture.window->getTabs().size();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getContinueWith()->setSelectedChoice(0);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy and Continue"));

    // F11: without the opened copy the next tool has nothing to run on
    dialog->getOpenCheckBox()->setChecked(false);
    QVERIFY(!dialog->getContinueWith()->isVisible());
    QCOMPARE(dialog->getContinueWith()->getSelectedChoice(), -1);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy"));

    // Keep offers it again: it runs on this document
    dialog->selectChoice(Choice::Keep);
    QVERIFY(dialog->getContinueWith()->isVisible());
    dialog->selectChoice(Choice::Copy);
    QVERIFY(!dialog->getContinueWith()->isVisible());

    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QVERIFY(outcome.continueToolId.isEmpty());
    QVERIFY(outcome.continueDocumentId.isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount);
}

void AppToolShellTest::dialogWriteError()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("1"), QStringLiteral("Images"), QStringLiteral("2"), QStringLiteral("Time"), QStringLiteral("3") });
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    QSignalSpy statusSpy(dialog, &PDFAppToolResultDialog::statusTextChanged);
    const int heightBefore = AppToolShellFixture::getCardRect(dialog).height();
    const int optionHeightBefore = dialog->getOption(Choice::Copy)->height();
    QCOMPARE(optionHeightBefore, 175);
    QCOMPARE(dialog->getChooseLocationButton(), nullptr);

    // A folder, which cannot be written (it does not exist)
    const QString missingFolder = fixture.path(QStringLiteral("no/such/folder"));
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&) { return missingFolder; });
    dialog->getBrowseButton()->click();
    QCOMPARE(dialog->getFolderLabel()->getPath(), QDir::toNativeSeparators(missingFolder));
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));

    // 316:7293: the error at the choice, the dialog stays, nothing is lost
    QCOMPARE(decidedSpy.size(), 0);
    pdf::PDFTintedCard* error = dialog->getWriteError();
    QVERIFY(error && error->isVisible());
    QCOMPARE(error->findChild<pdf::PDFLabel*>(QStringLiteral("writeErrorTitle"))->text(), QStringLiteral("The copy could not be written"));
    QVERIFY(error->findChild<pdf::PDFAppParagraph*>(QStringLiteral("writeErrorText"))->text().contains(QStringLiteral("Nothing was changed – the result is still waiting.")));
    QCOMPARE(dialog->getChooseLocationButton()->text(), QStringLiteral("Choose location…"));
    QVERIFY(dialog->getChooseLocationButton()->isVisible());
    QCOMPARE(dialog->focusWidget(), dialog->getChooseLocationButton());
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    QVERIFY(dialog->getConfirmButton()->isEnabled());
    QVERIFY(tab->getDocument()->isBlockedByOperation());
    QCOMPARE(statusSpy.size(), 1);
    QVERIFY2(dialog->getStatusText().startsWith(QStringLiteral("Compress: the copy could not be written – ")), qPrintable(dialog->getStatusText()));
    QVERIFY2(!dialog->getStatusText().endsWith(QLatin1Char('.')), qPrintable(dialog->getStatusText()));
    AppToolShellFixture::settle();

    // Geometry of 316:7764: the box 422 wide between the folder and the check box, the
    // card 130 higher (692 -> 822): 8 + its height 122
    QCOMPARE(error->width(), 422);
    QCOMPARE(error->height(), 122);
    QCOMPARE(dialog->getOption(Choice::Copy)->height(), optionHeightBefore + 130);
    QCOMPARE(AppToolShellFixture::getCardRect(dialog).height(), heightBefore + 130);
    QCOMPARE(dialog->getChooseLocationButton()->height(), 36);
    QCOMPARE(dialog->getChooseLocationButton()->width(), 2 + 16 + 18 + dialog->getChooseLocationButton()->fontMetrics().size(Qt::TextShowMnemonic, QStringLiteral("Choose location…")).width());
    QVERIFY(qAbs(dialog->getChooseLocationButton()->width() - 149) <= 2);

    // Choose location… is Secondary also with the focus (316:7770): Enter stays on the confirmation
    QVERIFY(!dialog->getChooseLocationButton()->isDefault());
    QVERIFY(dialog->getConfirmButton()->isDefault());
    QCOMPARE(error->findChild<pdf::PDFAppParagraph*>(QStringLiteral("writeErrorText"))->getLineHeight(), 16);
    const int errorTop = error->mapTo(dialog, QPoint(0, 0)).y();
    QVERIFY(errorTop > dialog->getBrowseButton()->mapTo(dialog, QPoint(0, 36)).y());
    QCOMPARE(dialog->getOpenCheckBox()->mapTo(dialog, QPoint(0, 0)).y(), errorTop + 122 + 8);

    // Choose location… writes to another folder: the next attempt succeeds
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&) { return fixture.folder(); });
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->getChooseLocationButton()->click();
    QVERIFY(!error->isVisible());
    QCOMPARE(dialog->getStatusText(), QStringLiteral("Compress finished · the result is waiting for your decision"));
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QCOMPARE(outcome.action, Action::CopySaved);
    QCOMPARE(ToolShellTestHelper::titleOf(outcome.filePath), QStringLiteral("Result title"));
}

void AppToolShellTest::dialogExistingFile()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    const QString existing = fixture.path(QStringLiteral("existing.pdf"));
    {
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not replaced");
    }

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->getFileNameField()->setText(QStringLiteral("existing.pdf"));
    QTest::keyClicks(dialog->getFileNameField(), QString());
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));

    // The state of the dialog (tools.md 9.3 point 11): a warning below the name and the
    // button Replace File, the file is untouched
    QCOMPARE(decidedSpy.size(), 0);
    QVERIFY(dialog->isReplaceConfirmed());
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Replace File"));
    QVERIFY(dialog->getNameError()->isVisible());
    QVERIFY(dialog->getNameError()->text().contains(QStringLiteral("exists")));
    QCOMPARE(ToolShellTestHelper::readFile(existing), QByteArray("not replaced"));

    // Another name cancels the question
    QTest::keyClicks(dialog->getFileNameField(), QStringLiteral("x"));
    QVERIFY(!dialog->isReplaceConfirmed());
    QVERIFY(!dialog->getNameError()->isVisible());
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Copy"));

    // Back to the name, asked again and now replaced
    dialog->getFileNameField()->setText(QStringLiteral("existing.pdf"));
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
    QVERIFY(dialog->isReplaceConfirmed());
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(ToolShellTestHelper::titleOf(existing), QStringLiteral("Result title"));
}

void AppToolShellTest::dialogNameRefused_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("device") << QStringLiteral("NUL.pdf");
#ifdef Q_OS_WIN
    QTest::newRow("stream") << QStringLiteral("a.pdf::$DATA.pdf");
    QTest::newRow("trailing dot") << QStringLiteral("a.pdf.");
#endif
    QTest::newRow("folder") << QStringLiteral("../outside.pdf");
    QTest::newRow("bidi") << QString(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("b.pdf"));
}

void AppToolShellTest::dialogNameRefused()
{
    QFETCH(QString, name);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);

    // T2: the name is checked before anything is written, the reason below it
    const QStringList filesBefore = QDir(fixture.folder()).entryList(QDir::Files);
    dialog->getFileNameField()->setText(name);
    dialog->confirm();
    QVERIFY(!dialog->isWriting());
    QVERIFY(dialog->getNameError()->isVisible());
    QVERIFY(!dialog->getNameError()->text().isEmpty());
    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(QDir(fixture.folder()).entryList(QDir::Files), filesBefore);
    dialog->reject();
}

void AppToolShellTest::dialogNetworkPathRefused()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);

    // T1: a location of another computer typed into the path of an export needs Browse…
    dialog->getFilePathField()->setText(QStringLiteral("//server/share/a.txt"));
    dialog->confirm();
    QVERIFY(!dialog->isWriting());
    QVERIFY(dialog->getNameError()->isVisible());
    QVERIFY(dialog->getNameError()->text().contains(QStringLiteral("Browse")));
    QCOMPARE(decidedSpy.size(), 0);

    // A relative path and a stream too
    dialog->getFilePathField()->setText(QStringLiteral("relative.txt"));
    dialog->confirm();
    QVERIFY(!dialog->isWriting());
    QVERIFY(dialog->getNameError()->isVisible());
    QVERIFY(!QFileInfo::exists(QStringLiteral("relative.txt")));

    // The path chosen by Browse… is taken (a local one here: the test contacts nothing)
    pdf::PDFAppFileDialogs::setPrintFileHandler([&](QWidget*, const QString&) { return fixture.path(QStringLiteral("browsed.txt")); });
    dialog->getBrowseButton()->click();
    QVERIFY(!dialog->getNameError()->isVisible());
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::FileSaved);
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("browsed.txt"))));
}

void AppToolShellTest::dialogOverwrite()
{
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    refresh->setEnabled(true);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Overwrite);
    dialog->getContinueWith()->setSelectedChoice(0);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Overwrite and Continue"));

    // F6: the file first ...
    const quint64 version = document->getVersion();
    dialog->confirm();
    QVERIFY(dialog->isWriting());
    QCOMPARE(document->getVersion(), version);
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    // ... then the commit and the saved state: one undo step, not modified
    const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
    QCOMPARE(outcome.action, Action::Overwritten);
    QCOMPARE(outcome.continueDocumentId, document->getId());
    QCOMPARE(ToolShellTestHelper::titleOf(file), QStringLiteral("Result title"));
    QCOMPARE(document->getVersion(), version + 1);
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QVERIFY(!document->isModified());
    QCOMPARE(document->getOriginalData(), ToolShellTestHelper::readFile(file));
    QVERIFY(!document->isBlockedByOperation());

    // An own write is no change of the file for the automatic refresh
    refresh->checkNow();
    QVERIFY(AppToolShellFixture::waitFor([refresh]() { return !refresh->isChecking(); }));
    QTest::qWait(pdf::PDFAppAutoRefresh::CHECK_DELAY + 50);
    AppToolShellFixture::settle();
    QCOMPARE(refresh->getReloadCount(), 0);
}

void AppToolShellTest::dialogOverwriteFailureKeepsDocument()
{
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    const QByteArray original = ToolShellTestHelper::readFile(file);

    // A file, which cannot be written (read-only)
    QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::ReadUser));

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Overwrite);
    const quint64 version = document->getVersion();
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
    AppToolShellFixture::settle();

    // F6: a failed write changes nothing in the tab; the error stays at its choice
    // (tools.md 9.3 point 16) without Choose location…
    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(dialog->getWriteError() && dialog->getWriteError()->isVisible());
    QVERIFY(dialog->getOption(Choice::Overwrite)->isAncestorOf(dialog->getWriteError()));
    QVERIFY(!dialog->getChooseLocationButton()->isVisible());
    QVERIFY(dialog->getWriteError()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("writeErrorText"))->text().contains(QStringLiteral("read-only")));

    // The other choices still work
    dialog->selectChoice(Choice::Keep);
    QVERIFY(!dialog->getWriteError()->isVisible());
    dialog->confirm();
    QCOMPARE(decidedSpy.size(), 1);
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
}

void AppToolShellTest::dialogNoOverwriteWithoutFile()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* source = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(source && AppToolShellFixture::waitFor([source]() { return source->getDocument()->isReady(); }));

    // A document of the memory: no file to overwrite (F5)
    pdf::PDFAppDocumentTab* tab = fixture.window->openDocumentFromMemory(ToolShellTestHelper::readFile(source->getDocument()->getFilePath()), QStringLiteral("Memory.pdf"), QString());
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    QVERIFY(tab->getDocument()->getFilePath().isEmpty());

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QVERIFY(!dialog->getOption(Choice::Overwrite));
    QVERIFY(dialog->getOption(Choice::Keep));
    QVERIFY(dialog->getOption(Choice::Copy));

    // The folder of a copy without a file: the documents of the user, never an empty path
    dialog->selectChoice(Choice::Copy);
    QVERIFY(!dialog->getFolderLabel()->getPath().isEmpty());
    QVERIFY(QFileInfo(dialog->getTargetFilePath()).isAbsolute());
    dialog->reject();
}

void AppToolShellTest::dialogSignedVariant()
{
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(signedFile);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("1 MB") });
    QVERIFY(dialog);

    // Chapter 4 (O31): the warning below the facts, only Save as a copy without a tag
    QVERIFY(!dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getOption(Choice::Overwrite));
    pdf::PDFAppToolResultOption* option = dialog->getOption(Choice::Copy);
    QVERIFY(option);
    QVERIFY(option->isSelected());
    QVERIFY(option->getRadioButton()->isChecked());
    QVERIFY(!option->getTag()->isVisibleTo(option));
    pdf::PDFTintedCard* warning = dialog->getSignedWarning();
    QVERIFY(warning && warning->isVisible());
    QCOMPARE(warning->getHue().value(), pdf::PDFAccentHue::Amber);
    QCOMPARE(warning->findChild<pdf::PDFLabel*>(QStringLiteral("signedWarningTitle"))->text(), QStringLiteral("The signatures of the copy will not be valid"));
    QCOMPARE(warning->findChild<pdf::PDFAppParagraph*>(QStringLiteral("signedWarningText"))->text(), QStringLiteral("This tool rewrites the whole file, so it cannot keep the signed revision. Only a copy can be saved; the open document and its file stay as they are."));
    QVERIFY(warning->mapTo(dialog, QPoint(0, 0)).y() > dialog->getFactsWidget()->mapTo(dialog, QPoint(0, dialog->getFactsWidget()->height() - 1)).y());
    QVERIFY(warning->mapTo(dialog, QPoint(0, 0)).y() < option->mapTo(dialog, QPoint(0, 0)).y());
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);
    dialog->reject();
    AppToolShellFixture::settle();

    // Another tool keeps all the choices and has no warning
    PDFAppToolResultDialog* rotateDialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(rotateDialog);
    QVERIFY(!rotateDialog->getSignedWarning());
    QVERIFY(rotateDialog->getOption(Choice::Keep));
    QVERIFY(rotateDialog->getOption(Choice::Overwrite));
    rotateDialog->reject();
}

void AppToolShellTest::dialogLocked()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    tab->getDocument()->setLocked(true);

    // A locked document is never changed: only a copy
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QVERIFY(!dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getOption(Choice::Overwrite));
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);
    dialog->reject();
}

void AppToolShellTest::dialogNewDocument()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const QString sourceId = tab->getDocumentId();
    const quint64 version = tab->getDocument()->getVersion();

    // Open in a new tab: a new unsaved document, the next tool runs on it
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_MERGE, QStringLiteral("new-document"));
        QVERIFY(dialog);
        QCOMPARE(dialog->getSelectedChoice(), Choice::NewTab);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        int compressIndex = -1;
        const auto choices = dialog->getContinueWith()->getChoices();
        for (int i = 0; i < choices.size(); ++i)
        {
            compressIndex = choices[i].name == pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS)->getTitle() ? i : compressIndex;
        }
        QVERIFY(compressIndex >= 0);
        dialog->getContinueWith()->setSelectedChoice(compressIndex);
        QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Open and Continue"));
        dialog->confirm();
        QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

        const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
        QCOMPARE(outcome.action, Action::OpenedInNewTab);
        QCOMPARE(outcome.continueToolId, QStringLiteral("optimize.compress"));
        QVERIFY(!outcome.continueDocumentId.isEmpty());
        QVERIFY(outcome.continueDocumentId != sourceId);
        pdf::PDFAppDocument* opened = fixture.context->getDocumentRegistry()->findDocument(outcome.continueDocumentId);
        QVERIFY(opened);
        QVERIFY(AppToolShellFixture::waitFor([opened]() { return opened->isReady(); }));
        QVERIFY(opened->getFilePath().isEmpty());
        QVERIFY(opened->isModified());
        QCOMPARE(opened->getDocument()->getInfo()->title, QStringLiteral("Result title"));
        QCOMPARE(opened->getTitle(), QStringLiteral("titled.pdf"));
        QCOMPARE(tab->getDocument()->getVersion(), version);
    }

    // Save as a file, with the opening of the file
    {
        fixture.window->activateTab(tab);
        AppToolShellFixture::settle();
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_MERGE, QStringLiteral("new-document"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        dialog->selectChoice(Choice::File);
        QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("titled.pdf"));
        QVERIFY(dialog->getOpenCheckBox()->isChecked());
        QCOMPARE(dialog->getOpenCheckBox()->text(), QStringLiteral("Open the file in a new tab"));
        dialog->confirm();
        QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
        const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
        QCOMPARE(outcome.action, Action::FileSaved);
        QCOMPARE(ToolShellTestHelper::titleOf(fixture.path(QStringLiteral("titled.pdf"))), QStringLiteral("Result title"));
        QVERIFY(AppToolShellFixture::waitFor([&]() { return fixture.context->getDocumentRegistry()->findDocument(outcome.continueDocumentId) != nullptr; }));
    }
}

void AppToolShellTest::dialogFiles()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    // Save as a file: the bytes of the result in the file, the document untouched, no next tool
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"));
        QVERIFY(dialog);
        QVERIFY(!dialog->getContinueWith());
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        QCOMPARE(QDir::fromNativeSeparators(dialog->getFilePathField()->text()), fixture.path(QStringLiteral("extracted.txt")));
        dialog->confirm();
        QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
        const PDFAppToolResultOutcome outcome = ToolShellTestHelper::firstOutcome(decidedSpy);
        QCOMPARE(outcome.action, Action::FileSaved);
        QVERIFY(!outcome.isOpenFolderRequested);
        QCOMPARE(ToolShellTestHelper::readFile(fixture.path(QStringLiteral("extracted.txt"))), QByteArray("Result title"));
        QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    }

    // Copy to clipboard writes nothing
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        dialog->selectChoice(Choice::Clipboard);
        QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Copy to Clipboard"));
        QVERIFY(!dialog->getFilePathField()->isVisible());
        const QStringList filesBefore = QDir(fixture.folder()).entryList(QDir::Files);
        dialog->confirm();
        QCOMPARE(decidedSpy.size(), 1);
        QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::CopiedToClipboard);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Result title"));
        QCOMPARE(QDir(fixture.folder()).entryList(QDir::Files), filesBefore);
    }

    // An existing file of an export asks too
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
        dialog->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
        QVERIFY(dialog->isReplaceConfirmed());
        QCOMPARE(decidedSpy.size(), 0);
        dialog->reject();
    }
}

void AppToolShellTest::dialogAnalysis()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ANALYSIS, QStringLiteral("count"), { QStringLiteral("Pages"), QStringLiteral("2") });
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), PDFAppToolResultDialog::ResultKind::Analysis);

    // Nothing to decide: the facts and the button Close (no frame of the design)
    QVERIFY(!dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getContinueWith());
    QVERIFY(!dialog->getDiscardButton()->isVisible());
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Close"));
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Pages"));
    QVERIFY(tab->getDocument()->isBlockedByOperation());

    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QCOMPARE(decidedSpy.size(), 1);
    QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Closed);
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
}

void AppToolShellTest::dialogRecordsUse()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);

    // A discarded result and a preview are no use of a tool (wave notes, tools.md 9.3)
    PDFAppToolResultDialog* discarded = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(discarded);
    discarded->reject();
    AppToolShellFixture::settle();
    QVERIFY(fixture.tools().getUsedTools().isEmpty());

    // An accepted one is, with the pair of the tool before it
    {
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_ROTATE);
        pdf::PDFAppToolResultInput input;
        input.tool = pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE);
        input.previousToolId = TOOL_COMPRESS;
        QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &input.result));
        PDFAppToolResultDialog* dialog = PDFAppToolResultDialog::open(tab, runner, input);
        AppToolShellFixture::settle();
        dialog->confirm();
        AppToolShellFixture::settle();
    }
    QCOMPARE(fixture.tools().getUsedTools(), QStringList({ TOOL_ROTATE }));
    bool isPairFound = false;
    for (const pdf::PDFDocumentToolPair& pair : fixture.tools().getPairs({ TOOL_COMPRESS }))
    {
        isPairFound = isPairFound || pair.toTool == TOOL_ROTATE;
    }
    QVERIFY(isPairFound);
}

void AppToolShellTest::dialogAnotherTool()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);

    // Every tool of the catalog is offered, also a tool not implemented yet (owner answer B)
    dialog->getContinueWith()->getLinkWidget()->click();
    AppToolShellFixture::settle();
    QDialog* chooser = dialog->findChild<QDialog*>(QStringLiteral("toolChooserDialog"));
    QVERIFY(chooser && chooser->isVisible() && chooser->isModal());
    QCOMPARE(int(chooser->findChildren<pdf::PDFToolRow*>().size()), int(pdf::PDFDocumentToolCatalog::getTools().size()));

    // Cancel changes nothing
    QTest::keyClick(chooser, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(dialog->getContinueWith()->getSelectedChoice(), -1);

    // The search narrows them and a row chooses the tool
    dialog->getContinueWith()->getLinkWidget()->click();
    AppToolShellFixture::settle();
    chooser = dialog->findChild<QDialog*>(QStringLiteral("toolChooserDialog"));
    QVERIFY(chooser);
    pdf::PDFLineEdit* search = chooser->findChild<pdf::PDFLineEdit*>(QStringLiteral("toolChooserSearch"));
    QVERIFY(search);
    search->setText(QStringLiteral("Rotate"));
    AppToolShellFixture::settle();
    QVERIFY(int(chooser->findChildren<pdf::PDFToolRow*>().size()) <= PDFAppToolResultDialog::MAXIMUM_CHOOSER_ROWS);
    pdf::PDFToolRow* found = chooser->findChild<pdf::PDFToolRow*>(QStringLiteral("tool-pages.rotate"));
    QVERIFY(found);
    found->click();
    AppToolShellFixture::settle();

    // The tool is chosen; it takes the last place of the full row of three
    QVERIFY(dialog->getContinueWith()->getSelectedChoice() >= 0);
    QCOMPARE(dialog->getContinueWith()->getChoices().size(), qsizetype(3));
    QCOMPARE(dialog->getContinueWith()->getChoices().last().name, pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE)->getTitle());
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QCOMPARE(decidedSpy.size(), 1);
    QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).continueToolId, TOOL_ROTATE);
}

void AppToolShellTest::dialogRerun()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    pdf::PDFAppDocument* document = tab->getDocument();
    PDFDocumentToolRunner* runner = nullptr;
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"), QStringList(), &runner);
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    QSignalSpy runSpy(runner, &PDFDocumentToolRunner::runFinished);

    // The version changed behind the dialog (a reload): the old result is never kept, the
    // tool runs again and the user decides over the new facts - not silently (F2)
    document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
    dialog->confirm();
    QCOMPARE(decidedSpy.size(), 1);
    QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Rerun);
    QVERIFY(!ToolShellTestHelper::firstOutcome(decidedSpy).isAccepted());
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(AppToolShellFixture::waitFor([&runSpy]() { return runSpy.size() >= 1; }));
    QCOMPARE(runner->getAttempt(), 2);
    QCOMPARE(runner->getState(), RunnerState::Waiting);
    QVERIFY(fixture.tools().getUsedTools().isEmpty());
}

void AppToolShellTest::dialogDeletion_data()
{
    QTest::addColumn<QString>("victim");
    QTest::addColumn<bool>("isWriting");
    QTest::newRow("runner") << QStringLiteral("runner") << false;
    QTest::newRow("tab") << QStringLiteral("tab") << false;
    QTest::newRow("window") << QStringLiteral("window") << false;
    QTest::newRow("dialog") << QStringLiteral("dialog") << false;
    QTest::newRow("tab while writing") << QStringLiteral("tab") << true;
    QTest::newRow("window while writing") << QStringLiteral("window") << true;
    QTest::newRow("dialog while writing") << QStringLiteral("dialog") << true;
    QTest::newRow("tab with the chooser") << QStringLiteral("tab-chooser") << false;
    QTest::newRow("window with the chooser") << QStringLiteral("window-chooser") << false;
}

void AppToolShellTest::dialogDeletion()
{
    QFETCH(QString, victim);
    QFETCH(bool, isWriting);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFDocumentToolRunner* runner = nullptr;
    QPointer<PDFAppToolResultDialog> dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), QStringList(), &runner);
    QVERIFY(dialog);
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    QSignalSpy decidedSpy(dialog.data(), &PDFAppToolResultDialog::decided);

    if (isWriting)
    {
        dialog->getOpenCheckBox()->setChecked(false);
        dialog->confirm();
        QVERIFY(dialog->isWriting());
    }

    if (victim.endsWith(QLatin1String("-chooser")))
    {
        dialog->getContinueWith()->getLinkWidget()->click();
        AppToolShellFixture::settle();
        QVERIFY(dialog->findChild<QDialog*>(QStringLiteral("toolChooserDialog")));
    }

    if (victim == QLatin1String("runner"))
    {
        delete runner;
    }
    else if (victim.startsWith(QLatin1String("tab")))
    {
        delete tab;
    }
    else if (victim.startsWith(QLatin1String("window")))
    {
        delete fixture.window.data();
    }
    else
    {
        delete dialog.data();
    }

    AppToolShellFixture::settle();
    QVERIFY(dialog.isNull());

    // The operation has gone with the runner or with the undecided dialog (the tab itself
    // stays for a dialog or a runner)
    if (victim == QLatin1String("runner") || victim == QLatin1String("dialog"))
    {
        QVERIFY(document);
        QVERIFY(!document->isBlockedByOperation());
    }
    QCOMPARE(decidedSpy.size(), 0);

    // A started write ends by itself, nothing is delivered
    QVERIFY(AppToolShellFixture::waitFor([]() { return QThreadPool::globalInstance()->activeThreadCount() == 0; }));
    AppToolShellFixture::settle();
}

void AppToolShellTest::dialogDeletedFromDecided_data()
{
    QTest::addColumn<QString>("victim");
    QTest::addColumn<int>("choice");

    QTest::newRow("tab keep") << QStringLiteral("tab") << int(Choice::Keep);
    QTest::newRow("window keep") << QStringLiteral("window") << int(Choice::Keep);
    QTest::newRow("tab copy") << QStringLiteral("tab") << int(Choice::Copy);
    QTest::newRow("window copy") << QStringLiteral("window") << int(Choice::Copy);
    QTest::newRow("tab overwrite") << QStringLiteral("tab") << int(Choice::Overwrite);
    QTest::newRow("window overwrite") << QStringLiteral("window") << int(Choice::Overwrite);
    QTest::newRow("tab from the hide") << QStringLiteral("tab-hidden") << int(Choice::Keep);
    QTest::newRow("window from the hide") << QStringLiteral("window-hidden") << int(Choice::Keep);
}

void AppToolShellTest::dialogDeletedFromDecided()
{
    QFETCH(QString, victim);
    QFETCH(int, choice);

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);

    bool isDeleted = false;
    QObject context;
    const std::function<void()> deleter = [&]()
    {
        isDeleted = true;
        if (victim.startsWith(QLatin1String("tab")))
        {
            delete tab;
        }
        else
        {
            delete fixture.window.data();
        }
    };
    if (victim.endsWith(QLatin1String("-hidden")))
    {
        // A slot of the end of the modal dialog: the decision comes after it, the runner has gone
        QObject::connect(dialog, &QDialog::finished, &context, deleter);
    }
    else
    {
        QObject::connect(dialog, &PDFAppToolResultDialog::decided, &context, deleter);
    }

    dialog->selectChoice(Choice(choice));
    if (Choice(choice) == Choice::Copy)
    {
        dialog->getOpenCheckBox()->setChecked(false);
    }
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return isDeleted; }));
    AppToolShellFixture::settle();

    if (Choice(choice) == Choice::Overwrite || Choice(choice) == Choice::Copy)
    {
        const QString path = Choice(choice) == Choice::Copy ? fixture.path(QStringLiteral("titled.pdf")) : fixture.path(QStringLiteral("Report.pdf"));
        QCOMPARE(ToolShellTestHelper::titleOf(path), QStringLiteral("Result title"));
    }
}

void AppToolShellTest::dialogHostileSuggestedName()
{
    // T2: a name derived from an untrusted name of a document is not offered; the fallback
    // is no name of the file of the tab
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    for (const QString& hostileName : { QStringLiteral("NUL.pdf"), QStringLiteral("../outside.pdf"), QString(QStringLiteral("a") + QChar(0x202E) + QStringLiteral("fdp.exe")) })
    {
        PDFDocumentToolRunner* runner = fixture.createRunner(tab, TOOL_COMPRESS);
        pdf::PDFAppToolResultInput input;
        input.tool = pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS);
        QVERIFY(fixture.runTool(runner, AppToolShellFixture::parameters(QStringLiteral("title")), &input.result));
        QVERIFY(!input.result.documents.empty());
        input.result.documents.front().suggestedFileName = hostileName;

        PDFAppToolResultDialog* dialog = PDFAppToolResultDialog::open(tab, runner, std::move(input));
        AppToolShellFixture::settle();
        QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Report_copy.pdf"));
        dialog->reject();
        AppToolShellFixture::settle();
        delete runner;
    }
}

void AppToolShellTest::dialogTargetOpenInTab()
{
    // "The open document and its file stay as they are" (312:7399): a copy never replaces the
    // file of this tab, and a file of another tab as Save as (save-error) never does
    AppToolShellFixture fixture;
    const QString otherFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 2);
    pdf::PDFAppDocumentTab* otherTab = fixture.open(otherFile);
    QVERIFY(otherTab && AppToolShellFixture::waitFor([otherTab]() { return otherTab->getDocument()->isReady(); }));
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    const QByteArray original = ToolShellTestHelper::readFile(file);
    const QByteArray otherOriginal = ToolShellTestHelper::readFile(otherFile);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);

    QStringList names = { QStringLiteral("Report.pdf"), QStringLiteral("Report"), QStringLiteral("Other.pdf") };
#ifndef Q_OS_WIN
    // Another name of the same file
    QVERIFY(QFile::link(file, fixture.path(QStringLiteral("alias.pdf"))));
    names << QStringLiteral("alias.pdf");
#endif
    for (const QString& name : names)
    {
        dialog->getFileNameField()->setText(name);
        dialog->confirm();
        QVERIFY2(!dialog->isWriting(), qPrintable(name));
        QVERIFY2(dialog->getNameError()->isVisible(), qPrintable(name));
        QVERIFY2(dialog->getNameError()->text().contains(QStringLiteral("open in a tab")), qPrintable(name));
        QVERIFY2(!dialog->isReplaceConfirmed(), qPrintable(name));
    }
    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
    QCOMPARE(ToolShellTestHelper::readFile(otherFile), otherOriginal);

    // A free name passes
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->getFileNameField()->setText(QStringLiteral("free.pdf"));
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(ToolShellTestHelper::titleOf(fixture.path(QStringLiteral("free.pdf"))), QStringLiteral("Result title"));
}

void AppToolShellTest::dialogOverwriteRefusedBeforeWrite()
{
    // F6: what would refuse the commit is told before the file changes
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    const QByteArray original = ToolShellTestHelper::readFile(file);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Overwrite);
    const quint64 version = document->getVersion();
    const auto errorText = [dialog]() { return dialog->getWriteError()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("writeErrorText"))->text(); };

    // A lock turned on while the dialog is open
    document->setLocked(true);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
    AppToolShellFixture::settle();
    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
    QCOMPARE(document->getVersion(), version);
    QVERIFY(errorText().contains(QStringLiteral("locked")));
    QVERIFY(errorText().contains(QStringLiteral("Nothing was changed")));
    QVERIFY(document->isBlockedByOperation());

    // Another blocking operation (a change of an assistant, which started before the run)
    document->setLocked(false);
    QPointer<pdf::PDFAppDocumentOperation> other = document->beginOperation(pdf::PDFAppDocumentOperationKind::Mcp, QStringLiteral("Other"), true);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
    AppToolShellFixture::settle();
    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
    QVERIFY(errorText().contains(QStringLiteral("busy")));

    // Free again: the same choice works
    other->finish();
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Overwritten);
    QCOMPARE(ToolShellTestHelper::titleOf(file), QStringLiteral("Result title"));
    QCOMPARE(document->getVersion(), version + 1);
}

void AppToolShellTest::dialogOverwriteLockedDuringWrite()
{
    // Only the application itself can lock the document during the write. The file has the
    // bytes of the result then and the dialog must say so; the automatic refresh brings
    // them into the tab
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    refresh->setEnabled(true);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Overwrite);
    const quint64 version = document->getVersion();
    dialog->confirm();
    QVERIFY(dialog->isWriting());
    document->setLocked(true);
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
    AppToolShellFixture::settle();

    QCOMPARE(decidedSpy.size(), 0);
    QCOMPARE(ToolShellTestHelper::titleOf(file), QStringLiteral("Result title"));
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);

    const QString title = dialog->getWriteError()->findChild<pdf::PDFLabel*>(QStringLiteral("writeErrorTitle"))->text();
    const QString text = dialog->getWriteError()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("writeErrorText"))->text();
    QCOMPARE(title, QStringLiteral("The document in this tab was not changed"));
    QVERIFY2(text.contains(QStringLiteral("has been written")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("Nothing was changed")), qPrintable(text));

    // The decision ends the operation, the refresh takes the file
    dialog->reject();
    AppToolShellFixture::settle();
    QVERIFY(!document->isBlockedByOperation());
    document->setLocked(false);
    refresh->checkNow();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return refresh->getReloadCount() == 1 && document->isReady(); }));
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Result title"));
}

void AppToolShellTest::dialogDisplayVersion()
{
    // F5: the shell does not run a tool, which changes a display version of an XFA form. A
    // dialog opened anyway never does silently nothing: it says why, Close is its only button
    AppToolShellFixture fixture;
    const QString file = fixture.path(QStringLiteral("dynamic-xfa.pdf"));
    {
        const pdf::PDFDocument xfa = pdftest::XFATestSamples::createDocument({ pdftest::XFATestSamples::letter() }, pdftest::XFATestSamples::pagedTemplate(3));
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(file, &xfa, false));
    }
    const QByteArray original = ToolShellTestHelper::readFile(file);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->isPagedXFA());

    for (const bool isConfirmed : { true, false })
    {
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"));
        QVERIFY(dialog);
        QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);

        QCOMPARE(dialog->getResultKind(), PDFAppToolResultDialog::ResultKind::Analysis);
        for (const Choice choice : { Choice::Keep, Choice::Copy, Choice::Overwrite })
        {
            QVERIFY(!dialog->getOption(choice));
        }
        QVERIFY(!dialog->getContinueWith());
        QVERIFY(dialog->getDiscardButton()->isHidden());
        QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Close"));
        QCOMPARE(dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultSubtitle"))->text(), pdf::PDFAppDocument::getPagedXFARefusalText());

        if (isConfirmed)
        {
            dialog->confirm();
        }
        else
        {
            QTest::keyClick(dialog, Qt::Key_Escape);
        }
        AppToolShellFixture::settle();

        // The result is gone: no use of the tool, the document is free and unchanged
        QCOMPARE(decidedSpy.size(), 1);
        QCOMPARE(ToolShellTestHelper::firstOutcome(decidedSpy).action, Action::Discarded);
        QVERIFY(fixture.tools().getUsedTools().isEmpty());
        QVERIFY(!document->isBlockedByOperation());
    }
    QCOMPARE(ToolShellTestHelper::readFile(file), original);
}

void AppToolShellTest::dialogConfirmIcons()
{
    // The icons of the Primary button: icon/save-as of Save Copy (296:2761, 438:14342), check of Keep
    // in This Tab (389:21590) and of an export (339:9601), external-link of 378:3674
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

    const auto shown = [](const PDFAppToolResultDialog* dialog) { return dialog->getConfirmButton()->icon().pixmap(QSize(16, 16)).toImage(); };
    const auto icon = [](const char* name) { return pdf::PDFThemeIcons::getIcon(QString::fromLatin1(name)).pixmap(QSize(16, 16)).toImage(); };
    QVERIFY(icon("save-as") != icon("save"));

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QCOMPARE(shown(dialog), icon("save-as"));
    dialog->selectChoice(Choice::Keep);
    QCOMPARE(shown(dialog), icon("check"));
    dialog->selectChoice(Choice::Overwrite);
    QCOMPARE(shown(dialog), icon("save"));
    dialog->reject();
    AppToolShellFixture::settle();

    dialog = fixture.openDialog(tab, TOOL_MERGE, QStringLiteral("new-document"));
    QVERIFY(dialog);
    QCOMPARE(shown(dialog), icon("external-link"));
    dialog->reject();
    AppToolShellFixture::settle();

    dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"));
    QVERIFY(dialog);
    QCOMPARE(shown(dialog), icon("check"));
    dialog->reject();
}

void AppToolShellTest::dialogGeometryStructure()
{
    // 303:4897: Keep preselected with its tag, the numbers of the frame. Its button is Keep in
    // This Tab (K4), so only its height and end are of the frame
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    fixture.window->move(-400, -100);
    AppToolShellFixture::settle();

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, QStringLiteral("optimize.structure"), QStringLiteral("title"),
                                                        { QStringLiteral("Size"), QStringLiteral("24.8 MB → 21.7 MB (12.6 % smaller)"), QStringLiteral("Steps"), QStringLiteral("5 of 6 run · 1 skipped"), QStringLiteral("Time"), QStringLiteral("4.2 s") });
    QVERIFY(dialog);

    using Helper = ToolShellTestHelper;
    const QRect card = AppToolShellFixture::getCardRect(dialog);

    // 580 of the frame, whose row has one tool; the row of three tools approved by the owner on 2026-11-04
    // has two lines in its narrowest tile, 2 px more
    QCOMPARE(card.size(), QSize(520, 582));
    QCOMPARE(dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Optimize Structure finished"));
    QCOMPARE(Helper::inCard(dialog, dialog->getFactsWidget()), QRect(23, 71, 474, 92));
    QCOMPARE(dialog->getSelectedChoice(), Choice::Keep);
    QCOMPARE(Helper::inCard(dialog, dialog->getOption(Choice::Keep)), QRect(23, 177, 474, 79));
    QCOMPARE(Helper::inCard(dialog, dialog->getOption(Choice::Copy)), QRect(23, 262, 474, 60));
    QCOMPARE(Helper::inCard(dialog, dialog->getOption(Choice::Overwrite)), QRect(23, 328, 474, 76));
    QCOMPARE(dialog->getOption(Choice::Keep)->getTag()->text(), QStringLiteral("Recommended"));
    QVERIFY(dialog->getOption(Choice::Copy)->getTag()->isHidden());
    QCOMPARE(Helper::inCard(dialog, dialog->getContinueWith()), QRect(23, 418, 474, 93));
    QCOMPARE(Helper::inCard(dialog, dialog->getDiscardButton()), QRect(23, 525, 148, 36));
    QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).top(), 525);
    QCOMPARE(Helper::inCard(dialog, dialog->getConfirmButton()).right(), 496);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Keep in This Tab"));
    dialog->reject();
}

/// Test translator: the texts of the catalog and of the chooser get a prefix
class ToolShellCatalogTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const bool isTranslated = QLatin1String(context) == QLatin1String(pdf::PDFDocumentToolCatalog::TRANSLATION_CONTEXT) || QLatin1String(context) == QLatin1String("pdf::PDFAppToolChooserDialog");
        return isTranslated ? QStringLiteral("YY ") + QString::fromUtf8(sourceText) : QString();
    }
};

void AppToolShellTest::dialogLanguageChangeNames()
{
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB") });
    QVERIFY(dialog);

    pdf::PDFContinueWith* continueWith = dialog->getContinueWith();
    QVERIFY(!continueWith->getChoices().isEmpty());
    const QString name = continueWith->getChoices().first().name;
    continueWith->setSelectedChoice(0);

    continueWith->getLinkWidget()->click();
    AppToolShellFixture::settle();
    QDialog* chooser = dialog->findChild<QDialog*>(QStringLiteral("toolChooserDialog"));
    QVERIFY(chooser);
    pdf::PDFLineEdit* search = chooser->findChild<pdf::PDFLineEdit*>(QStringLiteral("toolChooserSearch"));
    QVERIFY(search);
    search->setText(QStringLiteral("merge"));
    AppToolShellFixture::settle();

    ToolShellCatalogTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();

    // The names of Then continue with: the chosen tool stays chosen
    QCOMPARE(continueWith->getChoices().first().name, QStringLiteral("YY ") + name);
    QCOMPARE(continueWith->getSelectedChoice(), 0);

    // The open chooser: its texts and rows, the typed search stays
    QCOMPARE(search->text(), QStringLiteral("merge"));
    QCOMPARE(search->placeholderText(), QStringLiteral("YY Search tools…"));
    QCOMPARE(chooser->findChild<QPushButton*>(QStringLiteral("toolChooserCancel"))->text(), QStringLiteral("YY Cancel"));
    const QList<pdf::PDFToolRow*> rows = chooser->findChildren<pdf::PDFToolRow*>();
    QVERIFY(!rows.isEmpty());
    for (const pdf::PDFToolRow* row : rows)
    {
        QVERIFY2(row->getName().startsWith(QStringLiteral("YY ")), qPrintable(row->getName()));
    }

    // The facts are data of the finished run, in the language it ran in
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("24.8 MB"));

    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(continueWith->getChoices().first().name, name);
    QCOMPARE(search->placeholderText(), QStringLiteral("Search tools…"));

    QTest::keyClick(chooser, Qt::Key_Escape);
    AppToolShellFixture::settle();
    dialog->reject();
}

void AppToolShellTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS writes the states of the dialog under the names of the frames
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const bool isDark : { false, true })
    {
        const QString suffix = isDark ? QStringLiteral("-dark") : QStringLiteral("-light");

        AppToolShellFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
        QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));

        const QStringList compressFacts = { QStringLiteral("Size"), QStringLiteral("24.8 MB → 6.1 MB (75 % smaller)"),
                                            QStringLiteral("Images"), QStringLiteral("398 recompressed · 14 kept"),
                                            QStringLiteral("Time"), QStringLiteral("1 min 12 s") };

        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), compressFacts);
        QVERIFY(dialog);
        dialog->getFileNameField()->setText(QStringLiteral("Project_Report_compressed.pdf"));
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("296-2761") + suffix);

        pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&) { return fixture.path(QStringLiteral("missing")); });
        dialog->getBrowseButton()->click();
        dialog->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&]() { return !dialog->isWriting(); }));
        AppToolShellFixture::settle();
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("316-7293") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        const QStringList rotateFacts = { QStringLiteral("Rotated"), QStringLiteral("3 pages · 9, 10, 15"), QStringLiteral("Rotation"), QStringLiteral("90° right") };
        dialog = fixture.openDialog(tab, TOOL_ROTATE, QStringLiteral("title"), rotateFacts);
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("389-21590") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        const QStringList structureFacts = { QStringLiteral("Size"), QStringLiteral("24.8 MB → 21.7 MB (12.6 % smaller)"), QStringLiteral("Steps"), QStringLiteral("5 of 6 run · 1 skipped"), QStringLiteral("Time"), QStringLiteral("4.2 s") };
        dialog = fixture.openDialog(tab, QStringLiteral("optimize.structure"), QStringLiteral("title"), structureFacts);
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("303-4897") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        const QStringList sanitizeFacts = { QStringLiteral("Removed"), QStringLiteral("7 kinds of hidden data"), QStringLiteral("File size"), QStringLiteral("48.6 MB → 44.1 MB"), QStringLiteral("Kept"), QStringLiteral("Invisible text (OCR layer)") };
        dialog = fixture.openDialog(tab, QStringLiteral("protect.sanitize"), QStringLiteral("title"), sanitizeFacts, nullptr, QStringLiteral("Hidden data removed"));
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("438-14342") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        dialog = fixture.openDialog(tab, QStringLiteral("protect.remove-links"), QStringLiteral("title"), rotateFacts, nullptr, QStringLiteral("14 external links removed"));
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("438-16073") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        const QStringList extractFacts = { QStringLiteral("Pages"), QStringLiteral("1,580 with text · 12 without"), QStringLiteral("Words"), QStringLiteral("412,880"), QStringLiteral("Size"), QStringLiteral("2.9 MB") };
        dialog = fixture.openDialog(tab, TOOL_EXTRACT, QStringLiteral("files"), extractFacts);
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("339-9601") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        const QStringList mergeFacts = { QStringLiteral("Pages"), QStringLiteral("16 from 3 files"), QStringLiteral("Outline"), QStringLiteral("3 document parts"), QStringLiteral("Size"), QStringLiteral("18.2 MB") };
        dialog = fixture.openDialog(tab, TOOL_MERGE, QStringLiteral("new-document"), mergeFacts);
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("378-3674") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();

        // The window with the scrim over the canvas
        dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), compressFacts);
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(fixture.window.data(), QStringLiteral("window-296-2761") + suffix);
        dialog->reject();
        AppToolShellFixture::settle();
        pdf::PDFAppFileDialogs::setFolderHandler(nullptr);
    }

    // The signed variant of chapter 4, 720 x 480 and right-to-left
    {
        AppToolShellFixture fixture(QSize(720, 480));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
        QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB → 6.1 MB") });
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("signed-copy-only"));
        AppToolShellFixture::screenshotDialog(fixture.window.data(), QStringLiteral("window-720x480"));
        dialog->reject();
    }
    {
        QApplication::setLayoutDirection(Qt::RightToLeft);
        AppToolShellFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
        PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"), { QStringLiteral("Size"), QStringLiteral("24.8 MB → 6.1 MB") });
        QVERIFY(dialog);
        AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("296-2761-rtl"));
        dialog->reject();
    }
}

// ---------------------------------------------------------------- the shell

/// Receives the addresses, which the application gives to the system
class UrlRecorder : public QObject
{
    Q_OBJECT

public slots:
    void record(const QUrl& url) { urls.append(url); }

public:
    QList<QUrl> urls;
};

/// Helpers of the tests of the shell
class ToolShellFlowHelper
{
public:
    using State = pdf::PDFDocumentToolShell::State;

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        return tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }) ? tab : nullptr;
    }

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, int pageCount = 3)
    {
        static int s_counter = 0;
        return openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Shell-%1.pdf").arg(++s_counter), pageCount));
    }

    /// A signed document without a file: Edit a Copy of a signed file, ready
    static pdf::PDFAppDocumentTab* openSignedCopy(AppToolShellFixture& fixture, const QString& filePath = QString())
    {
        pdf::PDFAppDocumentTab* signedTab = openReady(fixture, filePath.isEmpty() ? pdftest::AppDocumentSamples::signedDocument(fixture.folder()) : filePath);
        if (!signedTab)
        {
            return nullptr;
        }
        signedTab->executeCommand(pdf::PDFAppCommands::editCopy());
        AppToolShellFixture::settle();
        pdf::PDFAppDocumentTab* tab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
        return tab && tab != signedTab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }) ? tab : nullptr;
    }

    static QString bannerTitle(const pdf::PDFAppDocumentSignaturesPanel* panel)
    {
        const pdf::PDFTintedCard* banner = panel->getBanner();
        return banner ? banner->findChildren<pdf::PDFLabel*>().value(0)->text() : QString();
    }

    static QString bannerText(const pdf::PDFAppDocumentSignaturesPanel* panel)
    {
        const pdf::PDFTintedCard* banner = panel->getBanner();
        return banner ? banner->findChildren<pdf::PDFLabel*>().value(1)->text() : QString();
    }

    /// Rectangle of the widget in the coordinates of \p reference
    static QRect rectIn(const QWidget* widget, const QWidget* reference)
    {
        return QRect(widget->mapTo(reference->window(), QPoint(0, 0)) - reference->mapTo(reference->window(), QPoint(0, 0)), widget->size());
    }

    static QPoint globalCenter(const QWidget* widget)
    {
        return widget->mapToGlobal(widget->rect().center());
    }

    /// Runs the tool: the Primary button, then waits for the dialog
    static PDFAppToolResultDialog* confirmAndWait(pdf::PDFDocumentToolShell* shell)
    {
        shell->getToolbar()->getConfirmButton()->click();
        AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; });
        AppToolShellFixture::settle();
        return shell->getResultDialog();
    }

    static QColor pixel(const QImage& image, const QPoint& point, qreal scale)
    {
        // The middle of the logical pixel
        return image.pixelColor(int((point.x() + 0.5) * scale), int((point.y() + 0.5) * scale));
    }

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFThemeManager::getInstance()->getColor(color);
    }

    static QVariantMap modeTitle(const QString& title = QStringLiteral("New title"))
    {
        return AppToolShellFixture::parameters(QStringLiteral("title"), title);
    }
};

/// Test translator of the shell: every text of the toolbar and of the shell gets a prefix
class ToolShellFlowTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QLatin1String name(context);
        if (name == QLatin1String("pdftest::TestDocumentTool") && QByteArray(sourceText) == "Plain text")
        {
            // A text of the definition of a tool
            return QStringLiteral("Translated text");
        }
        return name == QLatin1String("pdf::PDFAppToolToolbar") || name == QLatin1String("pdf::PDFDocumentToolGUI") || name == QLatin1String("pdf::PDFAppToolOutOfMemoryDialog") ? QStringLiteral("XX ") + QString::fromUtf8(sourceText) : QString();
    }
};

void AppToolShellTest::shellToolbarGeometry()
{
    // Toolbar 291:1000 at 1440: the positions of the frame
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS, false, QString(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    QCOMPARE(shell->getState(), H::State::Editing);

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QVERIFY(toolbar && toolbar->isVisible());
    QCOMPARE(toolbar->size(), QSize(1440, 52));
    QCOMPARE(toolbar->getCollapseLevel(), 0);

    // toolbar-left at 12: tb-sidebar 36, the divider of 13 with gaps of 2, the title
    const QRect sidebar = H::rectIn(toolbar->getSidebarButton(), toolbar);
    QCOMPARE(sidebar, QRect(12, 8, 36, 36));
    const QRect title = H::rectIn(toolbar->getTitleGroup(), toolbar);
    QCOMPARE(title.left(), 12 + 53);

    // tool-title: padding 4, Tool icon 28 hue violet of Optimize, gap 8, Semi Bold 14
    const QRect badge = H::rectIn(toolbar->getBadge(), toolbar);
    QCOMPARE(badge.left(), 12 + 53 + 4);
    QCOMPARE(badge.size(), QSize(28, 28));
    QCOMPARE(toolbar->getBadge()->getHue(), pdf::PDFAccentHue::Violet);
    QCOMPARE(toolbar->getBadge()->getIconName(), pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS)->getIconName());
    const QRect name = H::rectIn(toolbar->getTitleLabel(), toolbar);
    QCOMPARE(name.left(), badge.right() + 1 + 8);
    QCOMPARE(toolbar->getTitleLabel()->font().pixelSize(), 14);
    QCOMPARE(toolbar->getTitleLabel()->font().weight(), QFont::DemiBold);
    QCOMPARE(title.right() + 1, name.right() + 1 + 6);

    // The divider, Undo 36, Redo 36 (x 186 and 224 after a title of 116), the divider, the actions
    const QRect undo = H::rectIn(toolbar->getUndoButton(), toolbar);
    const QRect redo = H::rectIn(toolbar->getRedoButton(), toolbar);
    QCOMPARE(undo.left(), title.right() + 1 + 17);
    QCOMPARE(undo.size(), QSize(36, 36));
    QCOMPARE(redo.left(), undo.left() + 38);
    const QRect actions = H::rectIn(shell->getToolActions(), toolbar);
    QCOMPARE(actions.left(), redo.right() + 1 + 17);

    // tool-actions 291:1135: the label Medium 12, 6 px, four segments of 91 x 32
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    QVERIFY(gui);
    pdf::PDFSegmentedControl* segments = gui->segments->getSegmentedControl();
    QVERIFY(segments);
    QCOMPARE(segments->size(), QSize(364, 32));
    pdf::PDFLabel* label = shell->getToolActions()->findChild<pdf::PDFLabel*>(QStringLiteral("toolLabel-preset"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Preset"));
    QCOMPARE(label->font().pixelSize(), 12);
    QCOMPARE(label->font().weight(), QFont::Medium);
    QCOMPARE(H::rectIn(segments, label->parentWidget()).left() - (H::rectIn(label, label->parentWidget()).right() + 1 - label->contentsMargins().right()), 6);

    // toolbar-right ends at 1428: Tutorial, Cancel 80 x 36, the Primary button, gaps of 8
    QPushButton* cancel = toolbar->getCancelButton();
    QPushButton* confirm = toolbar->getConfirmButton();
    const QRect confirmRect = H::rectIn(confirm, toolbar);
    QCOMPARE(confirmRect.right() + 1, 1428);
    QCOMPARE(confirmRect.height(), 36);
    QCOMPARE(H::rectIn(cancel, toolbar).size(), QSize(80, 36));
    QCOMPARE(H::rectIn(cancel, toolbar).right() + 1 + 8, confirmRect.left());
    QCOMPARE(H::rectIn(toolbar->getTutorialButton(), toolbar).right() + 1 + 8, H::rectIn(cancel, toolbar).left());
    QCOMPARE(toolbar->getTutorialButton()->height(), 36);
    QCOMPARE(confirm->text(), pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS)->getTitle());
    QVERIFY(confirm->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());
    QVERIFY(!cancel->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());

    // Owner answer Q5: Tutorial disabled with its place kept
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QVERIFY(!toolbar->getTutorialButton()->isEnabled());

    // The body 291:1029: the right panel 320, the sidebar stays, the canvas 814
    pdf::PDFAppDocumentView* view = tab->getView();
    QCOMPARE(H::rectIn(shell->getRightPanel(), view).left(), 1120);
    QCOMPARE(shell->getRightPanel()->width(), 320);
    QVERIFY(view->getSidebar()->isVisible());
    QCOMPARE(view->getCanvasArea()->width(), 814);
    QCOMPARE(H::rectIn(view->getCanvasArea(), view).left(), 306);

    // The message of the status bar (291:1065)
    QVERIFY(view->getToolMessage().startsWith(QStringLiteral("Compress · Shell-")));
    QVERIFY(view->getToolMessage().contains(QStringLiteral(" · 3 pages · ")));
    QVERIFY(view->getToolMessage().endsWith(QStringLiteral(" kB")) || view->getToolMessage().endsWith(QStringLiteral(" KB")) || view->getToolMessage().contains(QStringLiteral(" B")));
}

void AppToolShellTest::shellRightPanelGeometry()
{
    // sidebar-right 291:1177 and its sections 293:1354, 293:1366
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolSidePanel* panel = shell->getRightPanel();

    // Header at x 17, y 14, 287 x 16; the title Semi Bold 13, the hint Regular 11 text/muted 8 px after it
    const QRect header = H::rectIn(gui->resultHeader, panel);
    QCOMPARE(header, QRect(17, 14, 287, 16));
    QCOMPARE(gui->resultHeader->getTitleLabel()->font().pixelSize(), 13);
    QCOMPARE(gui->resultHeader->getTitleLabel()->font().weight(), QFont::DemiBold);
    QCOMPARE(gui->resultHeader->getHintLabel()->font().pixelSize(), 11);
    QCOMPARE(gui->resultHeader->getHintLabel()->text(), QStringLiteral("measured"));
    QCOMPARE(H::rectIn(gui->resultHeader->getHintLabel(), gui->resultHeader).left(), H::rectIn(gui->resultHeader->getTitleLabel(), gui->resultHeader).right() + 1 + 8);

    // The card 8 below the header, 287 x 126 with four rows of 28 (breakdown 293:1369)
    QWidget* card = gui->keyValues->parentWidget();
    QCOMPARE(H::rectIn(card, panel), QRect(17, 38, 287, 126));
    QCOMPARE(H::rectIn(gui->keyValues, card), QRect(13, 7, 261, 112));
    QCOMPARE(gui->keyValues->getRowRect(1).height(), 28);

    // The note 8 below the card, Regular 12 in lines of 17 (293:1382)
    QCOMPARE(H::rectIn(gui->note, panel).top(), 38 + 126 + 8);
    QCOMPARE(H::rectIn(gui->note, panel).width(), 287);
    QCOMPARE(gui->note->getLineHeight(), 17);
    QCOMPARE(gui->note->font().pixelSize(), 12);

    // The next section 16 below (the gap of Optimize) and the steps 319:7343
    pdf::PDFTileRow* step = gui->steps->getRow(ShellTestTool::getStepName(0));
    QVERIFY(step);
    QCOMPARE(H::rectIn(step, gui->steps), QRect(7, 7, 273, 41));
    pdf::PDFTileRow* secondStep = gui->steps->getRow(ShellTestTool::getStepName(1));
    QCOMPARE(H::rectIn(secondStep, gui->steps).top(), 7 + 41 + 2);
    QCOMPARE(gui->steps->width(), 287);
    QCOMPARE(gui->steps->height(), 270);

    // On: the check mark and the accent, off: minus, the muted tile, "Will be skipped"
    QCOMPARE(step->getEndIconName(), QStringLiteral("check"));
    QCOMPARE(step->getTone(), pdf::PDFTileRow::Tone::Accent);
    pdf::PDFTileRow* skipped = gui->steps->getRow(ShellTestTool::getStepName(4));
    QCOMPARE(skipped->getEndIconName(), QStringLiteral("minus"));
    QCOMPARE(skipped->getTone(), pdf::PDFTileRow::Tone::Muted);
    QCOMPARE(skipped->getDescription(), QStringLiteral("Will be skipped"));

    // A callout 295:2156: the hue, radius 10, 287 wide
    QCOMPARE(gui->callout->width(), 287);
    QCOMPARE(gui->callout->getHue().value(), pdf::PDFAccentHue::Blue);
    QCOMPARE(gui->callout->getRadius(), 10.0);
    QCOMPARE(H::rectIn(gui->callout->getTitleLabel(), gui->callout).left(), 13 + 16 + 8);
    QCOMPARE(gui->callout->getTextLabel()->getLineHeight(), 17);

    // bg/sidebar with the line towards the canvas
    QImage image = AppToolShellFixture::render(panel, 1.0);
    QCOMPARE(H::pixel(image, QPoint(5, 400), 1.0), H::token(pdf::PDFThemeColors::BgSidebar));
    QCOMPARE(H::pixel(image, QPoint(0, 400), 1.0), H::token(pdf::PDFThemeColors::BorderDefault));
}

void AppToolShellTest::shellLeftPanel()
{
    // 389:20177: the panel of the tool instead of the sidebar, the page selection and
    // the pending list of the builder
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE, true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 48);
    QVERIFY(tab);
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    AppToolShellFixture::settle();

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(view->isToolLeftPanelShown());
    QVERIFY(!view->getSidebar()->isVisible());
    QVERIFY(shell->getLeftPanel()->isVisible());
    QCOMPARE(shell->getLeftPanel()->width(), 300);

    // x 10, y 14, 279 wide; the mode of the thumbnails (tools.md 9.6), the label of the frame
    QCOMPARE(H::rectIn(gui->pageSelection, shell->getLeftPanel()).topLeft(), QPoint(10, 14));
    QCOMPARE(gui->pageSelection->width(), 279);
    QCOMPARE(gui->pageSelection->getLabel(), QStringLiteral("Pages to rotate"));
    QCOMPARE(gui->pageSelection->getMode(), pdf::PDFPageSelection::Mode::Selected);
    QCOMPARE(gui->pageSelection->getPageCount(), 48);

    // The selection of the thumbnails is the value of the parameter
    QString error;
    std::optional<pdf::PDFDocumentToolPageSelection> value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QVERIFY(value);
    QCOMPARE(value->pages, QStringLiteral("9-10, 15"));

    // K6: the pages of the thumbnails are no change of the user, Undo waits for the first one;
    // a new selection in the thumbnails and the current page follow the view without a step
    QVERIFY(!shell->getModel()->canUndo());
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    view->setSelectedPages({ 1, 2 });
    AppToolShellFixture::settle();
    value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QCOMPARE(value->pages, QStringLiteral("2-3"));
    QVERIFY(!shell->getModel()->canUndo());

    // A range: the value follows; a range with an error is no value, the Primary button is off
    gui->pageSelection->setMode(pdf::PDFPageSelection::Mode::Range);
    gui->pageSelection->setRange(QStringLiteral("2-4"));
    value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QVERIFY(value);
    QCOMPARE(value->pages, QStringLiteral("2-4"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    gui->pageSelection->setRange(QStringLiteral("2-x"));
    QVERIFY(!gui->pageSelection->isValid());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error)->pages, QStringLiteral("2-4"));
    gui->pageSelection->setRange(QStringLiteral("2-3"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // Undo through the model gives the widget the older range
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->pageSelection->getRange(), QStringLiteral("2-4"));

    // A filter is a part of the value
    gui->pageSelection->setFilters(pdf::PDFPageSelection::Filter::Even);
    value = pdf::PDFDocumentToolPageSelection::fromVariant(shell->getModel()->getValue(QStringLiteral("pages")), &error);
    QVERIFY(value->filters.testFlag(pdf::PDFDocumentToolPageSelection::Filter::Even));

    // The pending list 389:20177: the header with the count, a link of a row
    pdf::PDFAppToolPendingList* pending = gui->pendingList;
    QCOMPARE(pending->getHeader()->getTitleLabel()->text(), QStringLiteral("Pending rotation"));
    QCOMPARE(pending->getHeader()->getHintLabel()->text(), QStringLiteral("0"));
    QStandardItem* item = new QStandardItem(QStringLiteral("Page 9"));
    item->setData(QStringLiteral("rotate-cw"), pdf::PDFComponentRoles::IconNameRole);
    item->setData(QStringLiteral("Undo"), pdf::PDFComponentRoles::LinkTextRole);
    gui->pendingModel.appendRow(item);
    QCOMPARE(pending->getHeader()->getHintLabel()->text(), QStringLiteral("1"));

    // 389:20204: the header 279 x 22 - the title Semi Bold 13, the count Regular 12 at the
    // trailing edge -, 10 px, the rows 279 x 46, 10 px apart; the list takes the rest of the panel
    QStandardItem* secondItem = new QStandardItem(QStringLiteral("Page 10"));
    secondItem->setData(QStringLiteral("rotate-cw"), pdf::PDFComponentRoles::IconNameRole);
    secondItem->setData(QStringLiteral("Undo"), pdf::PDFComponentRoles::LinkTextRole);
    gui->pendingModel.appendRow(secondItem);
    AppToolShellFixture::settle();
    pdf::PDFAppToolSectionHeader* pendingHeader = pending->getHeader();
    QCOMPARE(pendingHeader->size(), QSize(279, 22));
    QCOMPARE(pendingHeader->getTitleLabel()->font().weight(), QFont::DemiBold);
    QCOMPARE(pendingHeader->getTitleLabel()->font().pixelSize(), 13);
    QCOMPARE(pendingHeader->getHintLabel()->text(), QStringLiteral("2"));
    QCOMPARE(pendingHeader->getHintLabel()->font().pixelSize(), 12);
    QCOMPARE(pendingHeader->getHintLabel()->geometry().right() + 1, 279);
    const QRect firstRow = pending->getView()->visualRect(gui->pendingModel.index(0, 0));
    const QRect secondRow = pending->getView()->visualRect(gui->pendingModel.index(1, 0));
    const QPoint rowInPending = pending->getView()->viewport()->mapTo(pending, firstRow.topLeft());
    QCOMPARE(rowInPending, QPoint(0, 22 + 10));
    QCOMPARE(firstRow.size(), QSize(279, 46));
    QCOMPARE(secondRow.top() - firstRow.top(), 56);
    const QWidget* leftPanel = shell->getLeftPanel();
    QCOMPARE(H::rectIn(pending, leftPanel).bottom() + 1, leftPanel->height() - 12);

    // F4: the sidebar of the user comes back with its state
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    shell->getToolbar()->getCancelButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(tabGuard);
    QVERIFY(!view->isToolModeActive());
    QVERIFY(view->getSidebar()->isVisible());
}

void AppToolShellTest::shellStepsPanel()
{
    // panel-steps 303:4560: 520 x 368, 4 px below the toolbar at its button; the frame
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolFlagsMenu* menu = gui->flagsMenu;
    QToolButton* button = menu->getButton();

    // tb-steps: the count of the steps which are on
    QCOMPARE(button->text(), QStringLiteral("Steps: 5 of 6"));
    QTest::mouseClick(button, Qt::LeftButton);
    AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = menu->getPanel();
    QVERIFY(panel->isVisible());
    QVERIFY(button->isChecked());
    QCOMPARE(shell->getToolbar()->getOpenPanel(), panel);

    const QRect panelRect = panel->getPanelRect();
    QCOMPARE(panelRect.size(), QSize(520, 368));
    const QPoint panelGlobal = panel->mapToGlobal(panelRect.topLeft());
    // At its button, kept inside of the screen (the offscreen screen is narrower than the window)
    const QRect available = button->screen()->availableGeometry();
    QCOMPARE(panelGlobal.x(), qMin(button->mapToGlobal(QPoint(0, 0)).x(), available.right() + 1 - 520));
    QCOMPARE(panelGlobal.y(), shell->getToolbar()->mapToGlobal(QPoint(0, shell->getToolbar()->height())).y() + 4);

    // The caption 313:7345: x 19, y 13, Medium 11 text/tertiary
    pdf::PDFLabel* caption = panel->findChild<pdf::PDFLabel*>(QStringLiteral("toolPanelCaption"));
    QVERIFY(caption);
    QCOMPARE(caption->text(), QStringLiteral("Optimization steps"));
    QCOMPARE(caption->mapTo(panel, QPoint(0, 0)) - panelRect.topLeft(), QPoint(19, 13));
    QCOMPARE(caption->font().pixelSize(), 11);
    QCOMPARE(caption->font().weight(), QFont::Medium);

    // The items: x 9, 502 x 46, y 34 and 2 px apart; the chosen one with its check mark
    QCOMPARE(int(panel->getRows().size()), ShellTestTool::STEP_COUNT);
    for (int i = 0; i < ShellTestTool::STEP_COUNT; ++i)
    {
        pdf::PDFTileRow* row = panel->getRows()[size_t(i)];
        QCOMPARE(row->getKind(), pdf::PDFTileRow::Kind::PanelStep);
        QCOMPARE(QRect(row->mapTo(panel, QPoint(0, 0)) - panelRect.topLeft(), row->size()), QRect(9, 34 + i * 48, 502, 46));
        QCOMPARE(row->isChecked(), i != 4);
    }

    // The divider row at 322 and the link at x 19, y 339 (the links 313:7347)
    QWidget* divider = panel->findChild<QWidget*>(QStringLiteral("toolPanelDivider"));
    QVERIFY(divider);
    QCOMPARE(divider->mapTo(panel, QPoint(0, 0)).y() - panelRect.top(), 322);
    QCOMPARE(divider->height(), 13);
    pdf::PDFTextLink* link = panel->findChild<pdf::PDFTextLink*>();
    QVERIFY(link);
    QCOMPARE(link->mapTo(panel, QPoint(0, 0)) - panelRect.topLeft(), QPoint(19, 339));

    // No leak color in the panel
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(panel, 1.0)), 0);

    // A row toggles its parameter; the panel stays; the count, the right panel and the undo follow
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QTest::mouseClick(panel->getRows()[4], Qt::LeftButton);
    QVERIFY(panel->isVisible());
    QCOMPARE(model->getValue(ShellTestTool::getStepName(4)).toBool(), true);
    QCOMPARE(button->text(), QStringLiteral("Steps: 6 of 6"));
    QCOMPARE(gui->steps->getRow(ShellTestTool::getStepName(4))->getEndIconName(), QStringLiteral("check"));
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());

    // Restore defaults
    link->click();
    QCOMPARE(model->getValue(ShellTestTool::getStepName(4)).toBool(), false);
    QCOMPARE(button->text(), QStringLiteral("Steps: 5 of 6"));

    // Esc closes the panel only, the tool stays
    QTest::keyClick(panel, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QVERIFY(!button->isChecked());
    QCOMPARE(shell->getState(), H::State::Editing);

    // An action of the toolbar writes the parameters as one step
    const int undoSteps = model->canUndo() ? 1 : 0;
    Q_UNUSED(undoSteps);
    gui->selectAllAction->click();
    QCOMPARE(menu->getCheckedCount(), 6);
    model->undo();
    QCOMPARE(menu->getCheckedCount(), 5);
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellEnumMenuAndSwitch()
{
    // Order / Format 339:8765: one choice closes the panel; the menu bar of the panels
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolEnumMenu* menu = gui->enumMenu;
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();

    QCOMPARE(menu->getButton()->text(), QStringLiteral("Order: Count pages"));
    QVERIFY(toolbar->openPanel(menu->getButton()));
    AppToolShellFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = menu->getPanel();
    QCOMPARE(panel->getPanelRect().width(), 270);
    pdf::PDFTileRow* chosen = menu->getRow(QStringLiteral("count"));
    pdf::PDFTileRow* other = menu->getRow(QStringLiteral("title"));
    QVERIFY(chosen && other);
    QVERIFY(chosen->isChecked());
    QVERIFY(!other->isChecked());
    QCOMPARE(chosen->getDescription(), QStringLiteral("Counts the selected pages"));
    QCOMPARE(chosen->getIconName(), QStringLiteral("hash"));

    // The menu bar: Left / Right go to the neighbor, the pointer over another button too
    QTest::keyClick(panel, Qt::Key_Left);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(toolbar->getOpenPanel(), gui->flagsMenu->getPanel());
    QVERIFY(gui->flagsMenu->getButton()->isChecked());
    QVERIFY(!menu->getButton()->isChecked());

    QVERIFY(toolbar->switchPanel(menu->getButton()));
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), panel);
    QVERIFY(!gui->flagsMenu->getButton()->isChecked());
    QVERIFY(menu->getButton()->isChecked());

    QTest::keyClick(panel, Qt::Key_Right);
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), gui->flagsMenu->getPanel());
    QVERIFY(toolbar->openPanel(menu->getButton()));
    AppToolShellFixture::settle();

    // A choice: the panel closes, the value, the title of the button and the undo follow
    QTest::mouseClick(other, Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QVERIFY(!menu->getButton()->isChecked());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("mode")).toString(), QStringLiteral("title"));
    QCOMPARE(menu->getButton()->text(), QStringLiteral("Order: Set title"));
    QVERIFY(other->isChecked());
    QVERIFY(!chosen->isChecked());

    // The segments of the binder write the same model and follow Undo
    pdf::PDFSegmentedControl* segments = gui->segments->getSegmentedControl();
    QCOMPARE(segments->getCurrentIndex(), 1);
    segments->setCurrentIndex(3);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("custom"));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(segments->getCurrentIndex(), 1);
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(menu->getButton()->text(), QStringLiteral("Order: Count pages"));
    QVERIFY(chosen->isChecked());
}

void AppToolShellTest::shellRunningFrame()
{
    // 295:1711: the card of the progress, the busy callout, Stop instead of Cancel and the
    // Primary button, the disabled parts, the message of the status bar
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_COMPRESS);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QSignalSpy operationsSpy(document, &pdf::PDFAppDocument::operationsChanged);

    tool->setHold(true);
    toolbar->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    QCOMPARE(shell->getState(), H::State::Running);
    AppToolShellFixture::settle();

    // The operation of the tool blocks the document and it is announced
    QVERIFY(operationsSpy.size() >= 1);
    QVERIFY(document->isBlockedByOperation());
    QCOMPARE(document->getOperations().size(), size_t(1));
    QCOMPARE(document->getOperations().front()->getKind(), pdf::PDFAppDocumentOperationKind::Tool);
    QVERIFY(document->getOperations().front()->isAskedBeforeClose());

    // The trailing side: Tutorial and Stop; 90 x 36 at the end of the toolbar
    QVERIFY(!toolbar->getCancelButton()->isVisible());
    QVERIFY(!toolbar->getConfirmButton()->isVisible());
    QVERIFY(toolbar->getStopButton()->isVisible());
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QCOMPARE(H::rectIn(toolbar->getStopButton(), toolbar).right() + 1, 1428);
    // 90 in the frame: the text of Inter rounds to a pixel at the metrics of the platform
    QVERIFY2(qAbs(toolbar->getStopButton()->width() - 90) <= 1, qPrintable(QString::number(toolbar->getStopButton()->width())));
    QCOMPARE(toolbar->getStopButton()->height(), 36);
    QVERIFY(!toolbar->getStopButton()->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());

    // The icon icon/stop of the frame (295:1744), not the larger square
    const QImage stopIcon = toolbar->getStopButton()->icon().pixmap(16, 16).toImage();
    QCOMPARE(stopIcon, pdf::PDFThemeIcons::getIcon(QStringLiteral("stop")).pixmap(16, 16).toImage());
    QVERIFY(stopIcon != pdf::PDFThemeIcons::getIcon(QStringLiteral("square")).pixmap(16, 16).toImage());

    // Segments and Undo disabled, the document (the panels for reading) usable
    QVERIFY(!gui->segments->getSegmentedControl()->isEnabled());
    QVERIFY(!toolbar->getUndoButton()->isEnabled());
    QVERIFY(!toolbar->getRedoButton()->isEnabled());
    QVERIFY(toolbar->getSidebarButton()->isEnabled());
    QVERIFY(!gui->steps->isEnabled());

    // The card 287 wide at the top, the callout 16 below it (K7): the texts of 295:2156
    pdf::PDFAppToolProgressCard* card = shell->getProgressCard();
    pdf::PDFAppToolCallout* callout = shell->getBusyCallout();
    QVERIFY(card && card->isVisible());
    QVERIFY(callout && callout->isVisible());
    QCOMPARE(H::rectIn(card, shell->getRightPanel()).topLeft(), QPoint(17, 14));
    QCOMPARE(card->width(), 287);
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("The document is busy"));
    QCOMPARE(callout->getTextLabel()->text(), QStringLiteral("It cannot be edited and no other tool can run on it until Compress finishes. Other tabs stay available. Stop ends the job and writes nothing."));
    QCOMPARE(callout->height(), 116);
    QCOMPARE(H::rectIn(callout, shell->getRightPanel()).top(), H::rectIn(card, shell->getRightPanel()).bottom() + 1 + 16);
    QCOMPARE(callout->getHue().value(), pdf::PDFAccentHue::Blue);
    QCOMPARE(card->getTitleLabel()->text(), QStringLiteral("Compress… 0 %"));

    // The geometry of result-card 295:1792 with the texts of the frame: 100 high
    card->setProgress(37, QStringLiteral("Compressing… 37 %"), QStringLiteral("Recompressing images · 152 of 412"), QStringLiteral("About 40 seconds left"));
    AppToolShellFixture::settle();
    QCOMPARE(card->height(), 100);
    QCOMPARE(card->getBar()->height(), 4);
    QCOMPARE(card->getBar()->width(), 261);
    QCOMPARE(card->getBar()->value(), 37);
    QCOMPARE(H::rectIn(card->getTitleLabel(), card).top(), 13);
    QCOMPARE(H::rectIn(card->getBar(), card).top(), 39);
    QCOMPARE(H::rectIn(card->getStepLabel(), card).top(), 51);
    QCOMPARE(H::rectIn(card->getTimeLabel(), card).top(), 74);
    QCOMPARE(card->getTitleLabel()->font().pixelSize(), 15);
    QCOMPARE(card->getStepLabel()->font().pixelSize(), 12);
    QCOMPARE(card->getTimeLabel()->font().pixelSize(), 11);

    // Texts of the status bar and of the time left (295:1821, 295:2155)
    QCOMPARE(tab->getView()->getToolMessage(), QStringLiteral("Compress · press Stop or Esc to stop"));
    QCOMPARE(pdf::PDFDocumentToolShell::formatRemainingTime(40), QStringLiteral("About 40 seconds left"));
    QCOMPARE(pdf::PDFDocumentToolShell::formatRemainingTime(150), QStringLiteral("About 3 minutes left"));
    QCOMPARE(pdf::PDFDocumentToolShell::formatRemainingTime(-1), QString());

    // Ctrl+Enter and the parameters do nothing now; Undo of the keys goes nowhere
    shell->confirm();
    QCOMPARE(tool->getExecuteCount(), 1);
    QVERIFY(!tab->getView()->isCommandEnabled(pdf::PDFAppCommands::undo()));

    // Stop: nothing is kept, the user edits again, the parts come back
    toolbar->getStopButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(!shell->getProgressCard());
    QVERIFY(!shell->getBusyCallout());
    QVERIFY(toolbar->getCancelButton()->isVisible());
    QVERIFY(toolbar->getConfirmButton()->isVisible());
    QVERIFY(!toolbar->getStopButton()->isVisible());
    QVERIFY(gui->segments->getSegmentedControl()->isEnabled());
    QVERIFY(gui->steps->isEnabled());
    QVERIFY(!document->isBlockedByOperation());
    QCOMPARE(AppToolShellFixture::undoCount(document), 0);
    QVERIFY(tab->getView()->getToolMessage().startsWith(QStringLiteral("Compress · Shell-")));

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
}

void AppToolShellTest::shellBusyCalloutOnlyForChange()
{
    // K7: only a tool, which changes the document, has the callout; Extract Text runs
    // with its card only (356:13079)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_EXTRACT);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    QVERIFY(shell->getProgressCard());
    QVERIFY(!shell->getBusyCallout());
    QCOMPARE(H::rectIn(shell->getProgressCard(), shell->getRightPanel()).topLeft(), QPoint(17, 14));
    shell->stop();
    tool->setHold(false);
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellFlowKeep()
{
    // The flow of 2.4: confirm, run, the dialog holding the operation, Keep in this tab:
    // one undo step, the last values stored, the state of the view back, F15
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 8);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();

    view->goToPage(2);
    AppToolShellFixture::settle();
    QTest::qWait(pdf::PDFAppDocumentPage::ROUTE_DELAY + 50);
    const QString documentRoute = fixture.currentRoute();
    QVERIFY(documentRoute.startsWith(QStringLiteral("/document/")));
    const int documentEntry = tab->getHistory().getCurrentIndex();
    const double zoom = view->getZoom();

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QCOMPARE(fixture.currentRoute(), pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_ROTATE).toString());
    QVERIFY(view->isToolModeActive());
    QCOMPARE(qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage())->getTitle(), pdf::PDFDocumentToolCatalog::findTool(TOOL_ROTATE)->getTitle());

    // The view changes in the tool: it is given back at the end
    view->setZoom(zoom * 2.0);
    view->goToPage(5);
    AppToolShellFixture::settle();

    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("From the tool"));
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(shell->getState(), H::State::Deciding);

    // K2: only Tutorial while the user decides; F2: the operation is held; no run asks
    QVERIFY(!shell->getToolbar()->getCancelButton()->isVisible());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isVisible());
    QVERIFY(!shell->getToolbar()->getStopButton()->isVisible());
    QVERIFY(shell->getToolbar()->getTutorialButton()->isVisible());
    QVERIFY(document->isBlockedByOperation());
    QVERIFY(!document->getOperations().front()->isAskedBeforeClose());
    QCOMPARE(dialog->getSelectedChoice(), Choice::Keep);

    // The scrim only over the canvas, the message of the shell (296:2761)
    QCOMPARE(dialog->getScrimTarget(), view->getCanvasArea());
    QCOMPARE(view->getToolMessage(), dialog->getStatusText());

    QPointer<pdf::PDFDocumentToolShell> shellGuard(shell);
    dialog->confirm();
    AppToolShellFixture::settle();

    QVERIFY(!view->isToolModeActive());
    QVERIFY(!shellGuard);
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("From the tool"));
    QVERIFY(document->isModified());
    QVERIFY(!document->isBlockedByOperation());

    // The state of the view is back
    QCOMPARE(view->getZoom(), zoom);
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));

    // F15: the entry of the tool is gone, Back never leads to it
    QVERIFY(qobject_cast<pdf::PDFAppDocumentPage*>(tab->getPage()) && !qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage()));
    QVERIFY(fixture.currentRoute().startsWith(QStringLiteral("/document/")));
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    const pdf::PDFAppHistory& history = tab->getHistory();
    for (int i = 0; i <= history.getCurrentIndex(); ++i)
    {
        QVERIFY2(!history.getEntry(i)->route.toString().contains(QStringLiteral("/tool/")), qPrintable(history.getEntry(i)->route.toString()));
    }

    // O25: the entry of the document before the tool is taken, the document is not twice next to itself
    QCOMPARE(history.getCurrentIndex(), documentEntry);

    // The last values of the tool: the next opening starts from them; the use is recorded
    pdf::PDFDocumentToolShell* again = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(again);
    QCOMPARE(again->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));
    QCOMPARE(document->getLastFinishedToolId(), TOOL_ROTATE);
    QVERIFY(fixture.tools().getUsedTools().contains(TOOL_ROTATE));
    again->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellFlowReplacesEntry()
{
    // F15 with another page of the document before the tool (the properties): the entry of
    // the tool is replaced by the document, Back goes to that page, never to the tool
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), QStringLiteral("general")));
    AppToolShellFixture::settle();
    const QString propertiesRoute = fixture.currentRoute();

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    const int count = tab->getHistory().getCount();
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    dialog->confirm();
    AppToolShellFixture::settle();

    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(qobject_cast<pdf::PDFAppDocumentPage*>(tab->getPage()));
    QCOMPARE(tab->getHistory().getCount(), count);
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    fixture.window->getNavigator()->goBack(tab);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.currentRoute(), propertiesRoute);
}

void AppToolShellTest::shellFlowContinue()
{
    // Then continue with on the document of the tab: the history entry of the tool is
    // replaced by the next tool (F15), the pair is recorded with the previous tool
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    const int historyCount = tab->getHistory().getCount();
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    pdf::PDFContinueWith* continueWith = dialog->getContinueWith();
    QVERIFY(continueWith);

    // A default successor of the catalog of Rotate: find the row of Compress or choose by the chooser
    // The first of the row (the order of O21)
    QStringList successors = dialog->getContinueToolIds();
    QVERIFY(!successors.isEmpty());
    const QString nextTool = successors.front();
    QVERIFY(fixture.registerShellTool(nextTool));
    continueWith->setSelectedChoice(0);
    dialog->confirm();
    AppToolShellFixture::settle();

    pdf::PDFDocumentToolShell* next = AppToolShellFixture::getShell(tab);
    QVERIFY(next);
    QCOMPARE(next->getTool()->getId(), nextTool);
    QCOMPARE(next->getState(), H::State::Editing);
    QCOMPARE(fixture.currentRoute(), pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), nextTool).toString());
    QCOMPARE(tab->getHistory().getCount(), historyCount);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);
    QCOMPARE(tab->getDocument()->getLastFinishedToolId(), TOOL_ROTATE);

    // Back goes to the document, not to the first tool
    fixture.window->getNavigator()->goBack(tab);
    AppToolShellFixture::settle();
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
}

void AppToolShellTest::shellFlowCopyOpensCopy()
{
    // Save as a copy with Then continue with: the next tool runs on the opened copy in
    // its tab (F11), the document of this tab is unchanged and its entry of the tool gone
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), Choice::Copy);

    // The first of the row (the order of O21)
    const QStringList successors = dialog->getContinueToolIds();
    QVERIFY(!successors.isEmpty());
    QVERIFY(fixture.registerShellTool(successors.front()));
    dialog->getFileNameField()->setText(QStringLiteral("copy-of-shell.pdf"));
    dialog->getContinueWith()->setSelectedChoice(0);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return fixture.currentTab() != tab; }));
    pdf::PDFAppDocumentTab* copyTab = nullptr;
    QVERIFY(AppToolShellFixture::waitFor([&]()
    {
        copyTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab());
        return copyTab && AppToolShellFixture::getShell(copyTab);
    }));
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("copy-of-shell.pdf"))));
    QCOMPARE(AppToolShellFixture::getShell(copyTab)->getTool()->getId(), successors.front());

    // The first tab: no tool, no entry of it, nothing changed
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getView()->isToolModeActive());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    QVERIFY(!tab->getHistory().getCurrentEntry()->route.toString().contains(QStringLiteral("/tool/")));
}

void AppToolShellTest::shellCancelAndDiscard_data()
{
    QTest::addColumn<QString>("way");
    QTest::newRow("cancel-button") << QStringLiteral("cancel-button");
    QTest::newRow("escape") << QStringLiteral("escape");
    QTest::newRow("back") << QStringLiteral("back");
    QTest::newRow("discard") << QStringLiteral("discard");
    QTest::newRow("dialog-escape") << QStringLiteral("dialog-escape");
}

void AppToolShellTest::shellCancelAndDiscard()
{
    // 2.4: Discard / Cancel / Esc / Back: the mode ends, the document stays, nothing stored
    using H = ToolShellFlowHelper;
    QFETCH(QString, way);

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const quint64 version = document->getVersion();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best"));

    if (way == QLatin1String("cancel-button"))
    {
        shell->getToolbar()->getCancelButton()->click();
    }
    else if (way == QLatin1String("escape"))
    {
        tab->getView()->getPdfWidget()->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
        QTest::keyClick(tab->getView()->getPdfWidget(), Qt::Key_Escape);
    }
    else if (way == QLatin1String("back"))
    {
        fixture.window->getNavigator()->goBack(tab);
    }
    else
    {
        PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
        QVERIFY(dialog);
        if (way == QLatin1String("discard"))
        {
            dialog->getDiscardButton()->click();
        }
        else
        {
            QTest::keyClick(dialog, Qt::Key_Escape);
        }
    }
    AppToolShellFixture::settle();

    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(document->getVersion(), version);
    QVERIFY(!document->isModified());
    QVERIFY(!document->isBlockedByOperation());
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    QVERIFY(tab->getView()->getToolbar()->isVisible());

    // The values of the tool are stored only by an accepted result
    pdf::PDFDocumentToolShell* again = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(again);
    QCOMPARE(again->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("balanced"));
    QVERIFY(!fixture.tools().getUsedTools().contains(TOOL_ROTATE));
    again->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellKeys()
{
    // Ctrl+Enter = the Primary button, Esc of a run = Stop, Ctrl+Z / Ctrl+Y = the tool
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // A change of the document before the tool: Undo of the tool does not touch it
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Before"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    QWidget* canvas = tab->getView()->getPdfWidget();
    canvas->setFocus();
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    QSignalSpy stateSpy(shell, &pdf::PDFDocumentToolShell::stateChanged);

    // K6: Undo off until the first change, then of the model
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QCOMPARE(model->setValue(QStringLiteral("preset"), QStringLiteral("best")), QString());
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(!shell->getToolbar()->getRedoButton()->isEnabled());

    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    AppToolShellFixture::settle();
    QCOMPARE(model->getValue(QStringLiteral("preset")).toString(), QStringLiteral("balanced"));
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(shell->getToolbar()->getRedoButton()->isEnabled());

    QTest::keyClick(canvas, Qt::Key_Y, Qt::ControlModifier);
    AppToolShellFixture::settle();
    QCOMPARE(model->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));

    // The buttons do the same from the event loop
    shell->getToolbar()->getUndoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(model->getValue(QStringLiteral("preset")).toString(), QStringLiteral("balanced"));
    shell->getToolbar()->getRedoButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(model->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));

    // Ctrl+Enter runs the tool; Esc of the run is Stop, the tool stays
    tool->setHold(true);
    QTest::keyClick(canvas, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    QCOMPARE(shell->getState(), H::State::Running);
    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(AppToolShellFixture::getShell(tab));
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));

    // The keypad Enter too; a disabled Primary button does nothing
    QTest::keyClick(canvas, Qt::Key_Enter, Qt::ControlModifier);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();

    // Tab goes through the toolbar in its order
    pdf::PDFDocumentToolShell* again = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(again);
    pdf::PDFAppToolToolbar* toolbar = again->getToolbar();
    toolbar->getSidebarButton()->setFocus(Qt::TabFocusReason);
    QWidget* next = toolbar->getSidebarButton()->nextInFocusChain();
    while (next && next->focusPolicy() == Qt::NoFocus)
    {
        next = next->nextInFocusChain();
    }
    QVERIFY(next);
    QVERIFY(toolbar->isAncestorOf(next));
    again->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellHelpTopic()
{
    // F1 of the page of a tool: the topic of the tool, without it the page Tools
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(fixture.registerShellTool(TOOL_ANALYSIS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QVERIFY(fixture.openTool(tab, TOOL_ANALYSIS));
    QCOMPARE(qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage())->getHelpTopic(), QStringLiteral("tools"));
    AppToolShellFixture::getShell(tab)->cancel();
    AppToolShellFixture::settle();

    QVERIFY(fixture.openTool(tab, TOOL_ROTATE));
    QCOMPARE(qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage())->getHelpTopic(), QStringLiteral("tool-pages-rotate"));
    fixture.window->getCommandAction(pdf::PDFAppCommands::help())->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tool-pages-rotate"));

    // The tool stays open in its tab
    QVERIFY(tab->getView()->isToolModeActive());
    QVERIFY(AppToolShellFixture::getShell(tab));
}

void AppToolShellTest::shellLeaveWhileRunning()
{
    // F14: a navigation during a run asks as close-running; Keep Running stays in the
    // tool and the history, Stop and Leave stops it and goes on
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    const QString toolRoute = fixture.currentRoute();
    const int historyCount = tab->getHistory().getCount();

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();

    // The answer is given from the event loop: run() of the dialog is nested
    QPointer<pdf::PDFAppToolDocumentPage> page = qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage());
    QString dialogTitle;
    QString dialogText;
    bool isAnswered = false;
    const auto answer = [&](const QString& buttonName)
    {
        QTimer::singleShot(0, fixture.window.data(), [&, buttonName]()
        {
            if (pdf::PDFAppDocumentChoiceDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>())
            {
                dialogTitle = dialog->property("titleText").toString();
                dialogText = dialog->objectName();
                if (QPushButton* button = dialog->findChild<QPushButton*>(buttonName))
                {
                    isAnswered = true;
                    button->click();
                }
            }
        });
    };

    // Keep Running refuses: the same page, the same entry, the run goes on
    answer(QStringLiteral("keepRunningButton"));
    fixture.window->getNavigator()->goBack(tab);
    AppToolShellFixture::settle();
    QVERIFY(isAnswered);
    QCOMPARE(dialogText, QStringLiteral("toolLeaveRunningDialog"));
    QVERIFY(page);
    QCOMPARE(tab->getPage(), page.data());
    QCOMPARE(fixture.currentRoute(), toolRoute);
    QCOMPARE(tab->getHistory().getCount(), historyCount);
    QCOMPARE(shell->getState(), H::State::Running);
    QVERIFY(tool->getHeldCount() == 1);

    // Another route of the tab (a forwarded file) asks too; the route of the tool itself does not
    isAnswered = false;
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_ROTATE));
    AppToolShellFixture::settle();
    QVERIFY(!isAnswered);
    QCOMPARE(shell->getState(), H::State::Running);

    answer(QStringLiteral("keepRunningButton"));
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
    AppToolShellFixture::settle();
    QVERIFY(isAnswered);
    QCOMPARE(fixture.currentRoute(), toolRoute);
    QCOMPARE(shell->getState(), H::State::Running);

    // Stop and Leave
    isAnswered = false;
    answer(QStringLiteral("stopAndLeaveButton"));
    fixture.window->getNavigator()->goBack(tab);
    AppToolShellFixture::settle();
    QVERIFY(isAnswered);
    QVERIFY(!page);
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellDocumentLeavesReady()
{
    // F2: the shell closes, when the document leaves the state Ready; the dialog goes with it
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lost.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    QPointer<PDFAppToolResultDialog> dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);

    tab->getDocument()->setPasswordRequired(file, false);
    AppToolShellFixture::settle();
    QVERIFY(!dialog || !dialog->isVisible());
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
}

void AppToolShellTest::shellLock_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<bool>("isAllowed");
    QTest::newRow("unavailable") << TOOL_ROTATE << false;
    QTest::newRow("available") << TOOL_EXTRACT << true;
}

void AppToolShellTest::shellLock()
{
    // A lock during the mode: Unavailable closes the shell with a status message, an
    // allowed tool stays; T3: the direct route of a locked document is Page Not Found
    using H = ToolShellFlowHelper;
    QFETCH(QString, toolId);
    QFETCH(bool, isAllowed);

    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(toolId));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);

    tab->getDocument()->setLocked(true);
    AppToolShellFixture::settle();
    QCOMPARE(AppToolShellFixture::getShell(tab) != nullptr, isAllowed);
    QCOMPARE(tab->getView()->isToolModeActive(), isAllowed);
    if (isAllowed)
    {
        AppToolShellFixture::getShell(tab)->cancel();
        AppToolShellFixture::settle();
    }
    else
    {
        QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("%1 was closed, because the document has been locked.").arg(pdf::PDFDocumentToolCatalog::findTool(toolId)->getTitle()));
    }

    // T3
    pdf::PDFDocumentToolShell* locked = fixture.openTool(tab, toolId);
    QCOMPARE(locked != nullptr, isAllowed);
    QCOMPARE(fixture.notFoundPage() != nullptr, !isAllowed);
    QCOMPARE(tab->getPage() && qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage()), isAllowed);
}

void AppToolShellTest::shellPageNotFoundWithoutGUI()
{
    // A tool without a GUI or without an implementation keeps Page Not Found, also a
    // route of another document; a document page without a tool behaves as before
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);

    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_MERGE));
    AppToolShellFixture::settle();
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(fixture.notFoundPage()->getDescriptionText(), QString::fromUtf8("Tool “Merge”"));

    // Only the implementation, only the GUI
    std::shared_ptr<TestDocumentTool> tool = std::make_shared<TestDocumentTool>(TOOL_MERGE);
    QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(tool), pdf::PDFDocumentToolRegistration::Registered);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
    AppToolShellFixture::settle();
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_MERGE));
    AppToolShellFixture::settle();
    QVERIFY(fixture.notFoundPage());
    QVERIFY(pdf::PDFDocumentToolRegistry::unregisterTool(TOOL_MERGE));

    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_MERGE, std::make_shared<pdftest::ShellTestGUIFactory>(false)));
    QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, TOOL_MERGE));
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
    AppToolShellFixture::settle();
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_MERGE));
    AppToolShellFixture::settle();
    QVERIFY(fixture.notFoundPage());
    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(TOOL_MERGE);
    QVERIFY(!pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, TOOL_MERGE));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getTools().isEmpty() || !pdf::PDFDocumentToolGUIRegistry::getTools().contains(TOOL_MERGE));

    // An id outside of the catalog is refused
    QVERIFY(!pdf::PDFDocumentToolGUIRegistry::registerFactory(QStringLiteral("no.such-tool"), std::make_shared<pdftest::ShellTestGUIFactory>(false)));

    // The route of a page registered by a test (AppToolsFixture) stays as it was
    QVERIFY(fixture.registerTestToolPage(TOOL_ROTATE, pdf::PDFAppToolPageKind::Document));
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(dynamic_cast<const pdftest::AppToolsTestPageFactory*>(pdf::PDFAppToolPageRegistry::getFactory(pdf::PDFAppToolPageKind::Document, TOOL_ROTATE).get()));

    // A document page without a tool: its route with the page
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoute::parse(QStringLiteral("/document/%1?page=2").arg(tab->getDocumentId())));
    AppToolShellFixture::settle();
    QVERIFY(qobject_cast<pdf::PDFAppDocumentPage*>(tab->getPage()));
    QVERIFY(!qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage()));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(!tab->getView()->isToolModeActive());
}

void AppToolShellTest::shellRegisteredFactoryRestored()
{
    // AppToolsFixture::registerTestToolPage gives the previous factory back (plan, chapter 8)
    std::shared_ptr<const pdf::PDFAppPageFactory> original = std::make_shared<pdftest::AppToolsTestPageFactory>();
    QVERIFY(pdf::PDFAppToolPageRegistry::registerFactory(pdf::PDFAppToolPageKind::Start, TOOL_MERGE, original));
    {
        AppToolShellFixture fixture;
        QVERIFY(fixture.registerTestToolPage(TOOL_MERGE));
        QVERIFY(pdf::PDFAppToolPageRegistry::getFactory(pdf::PDFAppToolPageKind::Start, TOOL_MERGE) != original);
    }
    QCOMPARE(pdf::PDFAppToolPageRegistry::getFactory(pdf::PDFAppToolPageKind::Start, TOOL_MERGE), original);
    pdf::PDFAppToolPageRegistry::unregisterFactory(pdf::PDFAppToolPageKind::Start, TOOL_MERGE);
}

void AppToolShellTest::shellDisplayVersionRefused()
{
    // F5: a display version of an XFA form refuses a tool, which changes the document,
    // with the reason; a tool writing a file works (Extract Text)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT));
    pdf::PDFDocument pdfDocument = pdftest::XFATestSamples::createDocument({ pdftest::XFATestSamples::letter() }, pdftest::XFATestSamples::pagedTemplate(3));
    const QString file = fixture.path(QStringLiteral("dynamic-xfa.pdf"));
    QVERIFY(pdftest::AppDocumentSamples::write(pdfDocument, file));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isPagedXFA());

    QVERIFY(!fixture.openTool(tab, TOOL_ROTATE));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), pdf::PDFAppDocument::getPagedXFARefusalText());

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    QCOMPARE(shell->getState(), H::State::Editing);
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellPermissionRefused()
{
    // 339:10328 (owner answer C): a document without the permission of the tool shows the
    // panels disabled for it and the message box over the canvas, Close leaves the tool
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    // Remove External Links needs the permission of the annotations, which the sample does not give
    const QString toolId = QStringLiteral("protect.remove-links");
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(toolId);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(!pdf::PDFDocumentToolCatalog::findTool(toolId)->isPermitted(tab->getDocument()->getDocument().data()));

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    QVERIFY(shell->isPermissionRefused());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(tab->getView()->getToolMessage().contains(QStringLiteral("does not allow")));

    pdf::PDFMessageBox* box = shell->getPermissionDialog();
    QVERIFY(box && box->isVisible());
    QCOMPARE(box->getCardWidth(), 480);
    QCOMPARE(box->getScrimTarget(), tab->getView()->getCanvasArea());
    QPushButton* closeButton = box->findChild<QPushButton*>(QStringLiteral("toolPermissionClose"));
    QVERIFY(closeButton);
    QCOMPARE(closeButton->text(), QStringLiteral("Close"));
    QVERIFY(!closeButton->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());

    // Nothing runs behind it
    shell->confirm();
    QCOMPARE(tool->getExecuteCount(), 0);

    closeButton->click();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
}

void AppToolShellTest::shellPreviewVersionChange()
{
    // A preview follows the parameters; a new version of the document (an assistant) drops
    // it and computes it again; the shell stays open
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    tool->setPreview(true, false);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->previewFacts.isEmpty(); }));
    QCOMPARE(gui->previewFacts, QStringList(QStringLiteral("3")));
    QCOMPARE(tool->getPreviewCount(), 1);
    const size_t previewsBefore = gui->previews.size();

    // A change of a parameter computes a new one
    shell->getModel()->setValue(QStringLiteral("detailed"), true);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getPreviewCount() == 2; }));

    // An assistant changes the document: dropped, computed again
    QVERIFY(AppToolShellFixture::waitFor([gui, previewsBefore]() { return gui->previews.size() > previewsBefore; }));
    const int previewCount = tool->getPreviewCount();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), H::State::Editing);
    QCOMPARE(gui->documentChanges, 1);
    QVERIFY(std::find(gui->previews.begin(), gui->previews.end(), nullptr) != gui->previews.end());
    QVERIFY(AppToolShellFixture::waitFor([&tool, previewCount]() { return tool->getPreviewCount() > previewCount; }));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellVersionChangeReruns()
{
    // A commit refused for an old version: the tool runs again and the dialog comes
    // again with the new result, at most MAXIMUM_ATTEMPTS computations
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    QPointer<PDFAppToolResultDialog> dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(tool->getExecuteCount(), 1);

    document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
    dialog->confirm();
    AppToolShellFixture::waitFor([&]() { return tool->getExecuteCount() == 2 && shell->getResultDialog() && shell->getResultDialog() != dialog.data(); });
    AppToolShellFixture::settle();
    QCOMPARE(tool->getExecuteCount(), 2);
    PDFAppToolResultDialog* second = shell->getResultDialog();
    QVERIFY(second);
    QCOMPARE(shell->getState(), H::State::Deciding);

    second->confirm();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QCOMPARE(AppToolShellFixture::undoCount(document), 1);
}

void AppToolShellTest::shellNotAccepted_data()
{
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("tone");
    QTest::addColumn<QString>("text");
    QTest::newRow("nothing") << QStringLiteral("nothing") << int(pdf::PDFAppInfoBarTone::Information) << QStringLiteral("Nothing to count");
    QTest::newRow("fail") << QStringLiteral("fail") << int(pdf::PDFAppInfoBarTone::Error) << QStringLiteral("Failed on purpose");
    QTest::newRow("throw") << QStringLiteral("throw") << int(pdf::PDFAppInfoBarTone::Error) << QStringLiteral("Thrown on purpose");
}

void AppToolShellTest::shellNotAccepted()
{
    // NothingToDo and Failed are no dialog: the tool stays with the reason, the next
    // confirmation hides it
    using H = ToolShellFlowHelper;
    QFETCH(QString, mode);
    QFETCH(int, tone);
    QFETCH(QString, text);

    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), mode);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return tab->getView()->findInfoBar(QStringLiteral("tool-result")) != nullptr; }));
    AppToolShellFixture::settle();

    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(!shell->getResultDialog());
    const pdf::PDFAppInfoBarRequest* bar = tab->getView()->findInfoBar(QStringLiteral("tool-result"));
    QCOMPARE(int(bar->tone), tone);
    QVERIFY2(bar->text.contains(text), qPrintable(bar->text));
    QVERIFY(!tab->getDocument()->isBlockedByOperation());

    // A run, which has nothing to do, is a use of the tool (newapp_tools.md 3, owner decision 7);
    // an error is not, nor are the last values of a result nobody took
    QCOMPARE(fixture.tools().getUsedTools().contains(TOOL_ROTATE), mode == QLatin1String("nothing"));

    // The run has ended: the user edits again
    QVERIFY(shell->getRightContent()->isEnabled());
    QVERIFY(shell->getToolActions()->isEnabled());
    QVERIFY(!shell->getProgressCard());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isVisible());

    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    shell->confirm();
    QVERIFY(!tab->getView()->findInfoBar(QStringLiteral("tool-result")));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
    QVERIFY(!tab->getView()->findInfoBar(QStringLiteral("tool-result")));
}

void AppToolShellTest::shellResultShowsItsDocument()
{
    // The run goes on behind another tab (F1, the Help tab): the decision about a document is never
    // made over a tab, which does not show it. The dialog brings its tab.
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    tool->setHold(true);
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Running);
    fixture.window->activateTab(fixture.window->getHomeTab());
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), fixture.window->getHomeTab());
    QCOMPARE(shell->getState(), H::State::Running);

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QVERIFY(shell->getResultDialog()->isVisible());
    QVERIFY(tab->getView()->isVisible());
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellResultFocusInItsTab()
{
    // The dialog brings its tab: the keyboard is in the dialog while it is open and in the tab of
    // the tool afterwards, never in a widget of the tab, which was shown before
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentTab* otherTab = H::openReady(fixture);
    QVERIFY(otherTab && otherTab != tab);
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    tool->setHold(true);
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Running);
    fixture.window->activateTab(otherTab);
    AppToolShellFixture::settle();
    otherTab->getView()->getPdfWidget()->setFocus();
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));

    PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog->isVisible());
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(dialog));
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();

    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));
    const QWidget* focused = fixture.window->focusWidget();
    QVERIFY2(!focused || !otherTab->isAncestorOf(focused), "The keyboard stays in the tab, which holds the result");
}

void AppToolShellTest::shellDeletedFromActivation()
{
    // The dialog brings its tab: a slot of the switch of the tabs may delete the tab with the shell (run it under ASan)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    tool->setHold(true);
    shell->confirm();
    pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
    fixture.window->activateTab(homeTab);
    AppToolShellFixture::settle();

    bool isDeleted = false;
    QObject receiver;
    QObject::connect(fixture.window->getTabBar(), &QTabBar::currentChanged, &receiver, [&]()
    {
        if (!isDeleted)
        {
            isDeleted = true;
            delete tab.data();
        }
    });
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&isDeleted]() { return isDeleted; }));
    AppToolShellFixture::settle();
    QVERIFY(!tab);
    QVERIFY(!shell);
    QVERIFY(fixture.window);
    QCOMPARE(fixture.window->getCurrentTab(), homeTab);
    QVERIFY(!fixture.window->findChild<PDFAppToolResultDialog*>());
}

void AppToolShellTest::shellResultWaitsForModalDialog_data()
{
    QTest::addColumn<QString>("end");
    QTest::newRow("closed") << QStringLiteral("closed");
    QTest::newRow("deleted") << QStringLiteral("deleted");
    QTest::newRow("another-dialog") << QStringLiteral("another-dialog");
    QTest::newRow("stop") << QStringLiteral("stop");
    QTest::newRow("tab-closed") << QStringLiteral("tab-closed");
    QTest::newRow("canceled-operation") << QStringLiteral("canceled-operation");
}

void AppToolShellTest::shellResultWaitsForModalDialog()
{
    // While a modal dialog of the application is open no new dialog appears (the rule of the opening
    // of documents and of the requests of an assistant, plan 2.5) and the tab below it stays: the user
    // decides about something else. The operation holds the document, the result comes afterwards.
    using H = ToolShellFlowHelper;
    QFETCH(QString, end);
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    tool->setHold(true);
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Running);
    pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
    fixture.window->activateTab(homeTab);
    AppToolShellFixture::settle();

    QPointer<QDialog> modal = new QDialog(fixture.window);
    modal->setObjectName(QStringLiteral("otherModalDialog"));
    modal->setModal(true);
    modal->show();
    AppToolShellFixture::settle();
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(modal.data()));

    tool->setHold(false);
    const pdf::PDFDocumentToolRunner* runner = shell->getRunner();
    QVERIFY(AppToolShellFixture::waitFor([runner]() { return runner->getState() == pdf::PDFDocumentToolRunner::State::Waiting; }));
    AppToolShellFixture::settle();

    QVERIFY2(!shell->getResultDialog(), "No dialog over the other dialog");
    QCOMPARE(fixture.window->getCurrentTab(), homeTab);
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(modal.data()));
    QVERIFY(tab->getDocument()->isBlockedByOperation());
    QCOMPARE(shell->getState(), H::State::Running);

    if (end == QLatin1String("stop"))
    {
        shell->stop();
        AppToolShellFixture::settle();
        QCOMPARE(shell->getState(), H::State::Editing);
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
        QVERIFY(!shell->getResultDialog());
        delete modal.data();
        AppToolShellFixture::settle();
        QVERIFY2(!shell->getResultDialog(), "A dropped result does not come back");
        QCOMPARE(fixture.window->getCurrentTab(), homeTab);
        return;
    }

    if (end == QLatin1String("tab-closed"))
    {
        delete tab;
        AppToolShellFixture::settle();
        QVERIFY(!shell);
        delete modal.data();
        AppToolShellFixture::settle();
        QVERIFY(fixture.window);
        QCOMPARE(fixture.window->getCurrentTab(), homeTab);
        return;
    }

    if (end == QLatin1String("canceled-operation"))
    {
        // Stop of a dialog close-running, which the user has answered "stop": the operation asks
        for (pdf::PDFAppDocumentOperation* operation : tab->getDocument()->getOperations())
        {
            operation->requestCancel();
        }
        QCOMPARE(runner->getState(), pdf::PDFDocumentToolRunner::State::Idle);
        modal->reject();
        delete modal.data();
        AppToolShellFixture::settle();
        QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getState() == H::State::Editing; }));
        QVERIFY2(!shell->getResultDialog(), "No dialog of a result without its operation");
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
        return;
    }

    if (end == QLatin1String("another-dialog"))
    {
        // The next dialog is waited for too
        QPointer<QDialog> second = new QDialog(fixture.window);
        second->setModal(true);
        modal->reject();
        second->show();
        AppToolShellFixture::settle();
        QVERIFY(!shell->getResultDialog());
        QCOMPARE(fixture.window->getCurrentTab(), homeTab);
        second->reject();
        delete second.data();
    }
    else if (end == QLatin1String("deleted"))
    {
        delete modal.data();
    }
    else
    {
        modal->reject();
    }

    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab));
    QVERIFY(shell->getResultDialog()->isVisible());
    QVERIFY(tab->getDocument()->isBlockedByOperation());
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
    delete modal.data();
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
}

void AppToolShellTest::shellPairEndsWithFirstNextTool_data()
{
    QTest::addColumn<QString>("between");
    QTest::newRow("copy-not-opened") << QStringLiteral("copy");
    QTest::newRow("nothing-to-do") << QStringLiteral("nothing");
}

void AppToolShellTest::shellPairEndsWithFirstNextTool()
{
    // newapp_tools.md 3: the pair A -> B is of B run as the first next tool over the result of A. A tool
    // accepted or nothing to do in between has been that first tool: the tool after it makes no pair with A.
    using H = ToolShellFlowHelper;
    QFETCH(QString, between);
    AppToolShellFixture fixture;
    const QString toolA = TOOL_ROTATE;
    const QString toolB = TOOL_COMPRESS;
    const QString toolC = QStringLiteral("protect.remove-links");
    for (const QString& id : { toolA, toolB, toolC })
    {
        QVERIFY(fixture.registerShellTool(id));
    }
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    const auto hasPair = [&fixture](const QString& from, const QString& to)
    {
        for (const pdf::PDFDocumentToolPair& pair : fixture.tools().getPairs({ from }))
        {
            if (pair.toTool == to)
            {
                return true;
            }
        }
        return false;
    };

    // A: kept in the tab, the tab holds its result
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, toolA);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QSignalSpy keptSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(keptSpy));
    AppToolShellFixture::settle();
    QCOMPARE(document->getLastFinishedToolId(), toolA);

    // B: the first next tool
    shell = fixture.openTool(tab, toolB);
    QVERIFY(shell);
    if (between == QLatin1String("copy"))
    {
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
        dialog = H::confirmAndWait(shell);
        QVERIFY(dialog);
        QCOMPARE(dialog->getPreviousToolId(), toolA);
        QSignalSpy copySpy(dialog, &PDFAppToolResultDialog::decided);
        dialog->selectChoice(Choice::Copy);
        dialog->getFileNameField()->setText(QStringLiteral("Between"));
        dialog->getOpenCheckBox()->setChecked(false);
        dialog->confirm();
        QVERIFY(ToolShellTestHelper::waitForDecision(copySpy));
        AppToolShellFixture::settle();
    }
    else
    {
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("nothing"));
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&]() { return tab->getView()->findInfoBar(QStringLiteral("tool-result")) != nullptr; }));
        AppToolShellFixture::settle();
        QCOMPARE(shell->getState(), H::State::Editing);
        QVERIFY(fixture.tools().getUsedTools().contains(toolB));
        shell->cancel();
        AppToolShellFixture::settle();
    }
    QVERIFY2(hasPair(toolA, toolB), "The first next tool is the pair");
    QVERIFY2(document->getLastFinishedToolId().isEmpty(), "That tool has left no result in this tab");

    // C: no pair with A
    shell = fixture.openTool(tab, toolC);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getPreviousToolId(), QString());
    QSignalSpy lastSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(lastSpy));
    AppToolShellFixture::settle();
    QVERIFY2(!hasPair(toolA, toolC), "The second next tool is no pair");
}

void AppToolShellTest::shellContinueLongNames_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::addColumn<bool>("isRightToLeft");
    QTest::newRow("1440") << QSize(1440, 960) << false;
    QTest::newRow("720x480") << QSize(720, 480) << false;
    QTest::newRow("720x480-rtl") << QSize(720, 480) << true;
}

void AppToolShellTest::shellContinueLongNames()
{
    // Then continue with of three tools with names of a long language: the three tiles lie inside of the
    // card, with one word longer than a tile - never cut by the card without a way to see them
    QFETCH(QSize, windowSize);
    QFETCH(bool, isRightToLeft);

    AppToolShellFixture fixture(windowSize);
    const Qt::LayoutDirection direction = QApplication::layoutDirection();
    QApplication::setLayoutDirection(isRightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);

    pdf::PDFContinueWith* continueWith = dialog->getContinueWith();
    QList<pdf::PDFContinueWith::Choice> choices = continueWith->getChoices();
    QCOMPARE(choices.size(), qsizetype(3));
    choices[0].name = QStringLiteral("Dokumentenzusammenfuehrungsassistent");
    choices[1].name = QStringLiteral("Kennwortschutzeinstellungen des Dokuments");
    choices[2].name = QStringLiteral("Zertifikatsunterzeichnungsvorgang");
    continueWith->setChoices(choices);
    AppToolShellFixture::settle();

    const QRect card = AppToolShellFixture::getCardRect(dialog);
    const QRect row(continueWith->mapTo(dialog, QPoint(0, 0)), continueWith->size());
    QVERIFY2(card.contains(row.left(), card.center().y()) && card.contains(row.right(), card.center().y()), qPrintable(QStringLiteral("card %1 .. %2, row %3 .. %4").arg(card.left()).arg(card.right()).arg(row.left()).arg(row.right())));
    int tileWidth = -1;
    for (int i = 0; i < 3; ++i)
    {
        const QWidget* tile = continueWith->getChoiceWidget(i);
        QVERIFY(tile);
        const QRect rect(tile->mapTo(dialog, QPoint(0, 0)), tile->size());
        QVERIFY2(rect.left() >= row.left() && rect.right() <= row.right(), qPrintable(QString::number(i)));
        QVERIFY(tileWidth < 0 || qAbs(tile->width() - tileWidth) <= 1);
        tileWidth = tile->width();
    }

    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("continue-long-names-%1x%2%3").arg(windowSize.width()).arg(windowSize.height()).arg(isRightToLeft ? QStringLiteral("-rtl") : QString()));
    dialog->reject();
    AppToolShellFixture::settle();
    QApplication::setLayoutDirection(direction);
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellPairOfTheOpenedCopy()
{
    // newapp_tools.md 3: the pair A -> B grows, when B runs over the result of A. A copy opened
    // by the dialog holds the result: the next tool there makes the pair, the original tab has
    // no result of A, a tool there makes none.
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* original = tab->getDocument();

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Copy);
    dialog->getFileNameField()->setText(QStringLiteral("Pair_copy"));
    dialog->getOpenCheckBox()->setChecked(true);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    const QString copyId = ToolShellTestHelper::firstOutcome(decidedSpy).continueDocumentId;
    QVERIFY(AppToolShellFixture::waitFor([&]() { return fixture.context->getDocumentRegistry()->findDocument(copyId) != nullptr; }));
    pdf::PDFAppDocument* copy = fixture.context->getDocumentRegistry()->findDocument(copyId);
    QVERIFY(AppToolShellFixture::waitFor([copy]() { return copy->isReady(); }));
    AppToolShellFixture::settle();

    QCOMPARE(copy->getLastFinishedToolId(), TOOL_ROTATE);
    QVERIFY2(original->getLastFinishedToolId().isEmpty(), "The tab of the original does not hold the result");

    // A tool in the original tab: no pair
    const auto findPair = [&fixture]()
    {
        for (const pdf::PDFDocumentToolPair& pair : fixture.tools().getPairs({ TOOL_ROTATE }))
        {
            if (pair.toTool == TOOL_COMPRESS)
            {
                return true;
            }
        }
        return false;
    };

    AppToolShellFixture::settle();
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    pdf::PDFDocumentToolShell* originalShell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(originalShell);
    originalShell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    dialog = H::confirmAndWait(originalShell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getPreviousToolId(), QString());
    dialog->reject();
    AppToolShellFixture::settle();
    QVERIFY(!findPair());

    // The next tool over the copy: the pair
    pdf::PDFAppDocumentTab* copyTab = nullptr;
    for (pdf::PDFAppTab* candidate : fixture.window->getTabs())
    {
        pdf::PDFAppDocumentTab* documentTab = qobject_cast<pdf::PDFAppDocumentTab*>(candidate);
        if (documentTab && documentTab->getDocument() == copy)
        {
            copyTab = documentTab;
        }
    }
    QVERIFY(copyTab);
    fixture.window->activateTab(copyTab);
    AppToolShellFixture::settle();
    pdf::PDFDocumentToolShell* copyShell = fixture.openTool(copyTab, TOOL_COMPRESS);
    QVERIFY(copyShell);
    copyShell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    dialog = H::confirmAndWait(copyShell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getPreviousToolId(), TOOL_ROTATE);
    QSignalSpy secondSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Copy);
    dialog->getFileNameField()->setText(QStringLiteral("Pair_second"));
    dialog->getOpenCheckBox()->setChecked(false);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(secondSpy));
    AppToolShellFixture::settle();
    QVERIFY(findPair());
}

void AppToolShellTest::shellThreeSuccessorsOfCompress()
{
    // Owner's remark 2: Then continue with of Compress offers the three tools of "Often used
    // next" of 276:1737, and the catalog row gives the same three to the start page
    const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS);
    QVERIFY(entry);
    QCOMPARE(entry->getDefaultSuccessors(), QStringList({ QStringLiteral("protect.password"), QStringLiteral("protect.sign"), QStringLiteral("pages.merge") }));

    // The defaults are shown in the order of the catalog (the rule of a tie of O21, newapp_tools.md 3);
    // the line of 276:1737 has another order, reported to the owner
    const QStringList expected({ QStringLiteral("pages.merge"), QStringLiteral("protect.password"), QStringLiteral("protect.sign") });

    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(2);
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QCOMPARE(dialog->getContinueToolIds(), expected);
    QCOMPARE(dialog->getContinueWith()->getChoices().size(), expected.size());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::signatureNoteFollowsTheVersion()
{
    // Owner decision 3: a signed document without a file after Keep of a tool, which rewrites it: the
    // panel and the status item say "not valid after saving"; undo, redo and later changes follow; a save ends it.
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* signedTab = fixture.open(signedFile);
    QVERIFY(signedTab && AppToolShellFixture::waitFor([signedTab]() { return signedTab->getDocument()->isReady(); }));
    signedTab->executeCommand(pdf::PDFAppCommands::editCopy());
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentTab* tab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(tab && tab != signedTab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();

    pdf::PDFAppDocumentSignatureVerifier* verifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab)->getSignatureVerifier();
    QVERIFY(verifier);
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return verifier->getVerification() && !verifier->isVerifying(); }));
    QVERIFY(!verifier->isSignatureLostOnSave());

    pdf::PDFAppSignatureStatusItem* item = tab->findChild<pdf::PDFAppSignatureStatusItem*>(QStringLiteral("signatureStatusItem"));
    QVERIFY(item);
    QVERIFY(!item->text().contains(QStringLiteral("not valid after saving")));
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentSignaturesPanel* panel = tab->findChild<pdf::PDFAppDocumentSignaturesPanel*>(QStringLiteral("signaturesPanel"));
    QVERIFY(panel);
    const auto bannerTitle = [panel]()
    {
        const pdf::PDFTintedCard* banner = panel->getBanner();
        return banner ? banner->findChildren<pdf::PDFLabel*>().value(0)->text() : QString();
    };
    const QString note = QStringLiteral("The signature will not be valid after saving");
    QVERIFY(bannerTitle() != note);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();

    QVERIFY(verifier->isSignatureLostOnSave());
    QVERIFY2(item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
    QCOMPARE(bannerTitle(), note);

    QVERIFY(document->getUndoStack()->undo());
    AppToolShellFixture::settle();
    QVERIFY(!verifier->isSignatureLostOnSave());
    QVERIFY2(!item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
    QVERIFY(bannerTitle() != note);

    QVERIFY(document->getUndoStack()->redo());
    AppToolShellFixture::settle();
    QVERIFY(verifier->isSignatureLostOnSave());
    QVERIFY2(item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
    QCOMPARE(bannerTitle(), note);

    // A later change stays a version of the rewritten document
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.addObject(pdf::PDFObject::createInteger(7));
    const pdf::PDFModifiedDocument modified(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::None);
    QVERIFY(document->commit(modified, QStringLiteral("Change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    AppToolShellFixture::settle();
    QVERIFY(verifier->isSignatureLostOnSave());
    QCOMPARE(bannerTitle(), note);

    // The save has happened: the panel says the verdict of the written bytes
    const QString target = fixture.path(QStringLiteral("note-saved.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    const bool isFinished = AppToolShellFixture::waitFor([&finishedSpy]() { return !finishedSpy.isEmpty(); });
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    QVERIFY(isFinished);
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return !verifier->isVerifying(); }));
    AppToolShellFixture::settle();
    QVERIFY(!verifier->isSignatureLostOnSave());
    QVERIFY2(!item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
    QVERIFY(bannerTitle() != note);
}

void AppToolShellTest::signatureNoteAfterWholeWrite()
{
    // Keep, a later change and Save: the file has been written whole. Undo goes back to the kept version with
    // the flag of a rewritten document, but nothing is left to lose - the panel, the status item and MCP say
    // the state of the written bytes, never "will not be valid after saving" and "undo brings it back".
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = ToolShellFlowHelper::openSignedCopy(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentSignatureVerifier* verifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab)->getSignatureVerifier();
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return verifier->getVerification() && !verifier->isVerifying(); }));
    pdf::PDFAppSignatureStatusItem* item = tab->findChild<pdf::PDFAppSignatureStatusItem*>(QStringLiteral("signatureStatusItem"));
    QVERIFY(item);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentSignaturesPanel* panel = tab->findChild<pdf::PDFAppDocumentSignaturesPanel*>(QStringLiteral("signaturesPanel"));
    QVERIFY(panel);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();
    QVERIFY(document->isSignatureLostOnSave());
    QVERIFY2(ToolShellFlowHelper::bannerText(panel).contains(QStringLiteral("Undo brings the signed version back")), qPrintable(ToolShellFlowHelper::bannerText(panel)));

    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.addObject(pdf::PDFObject::createInteger(7));
    const pdf::PDFModifiedDocument modified(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::None);
    QVERIFY(document->commit(modified, QStringLiteral("Change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    AppToolShellFixture::settle();
    QVERIFY(document->isSignatureLostOnSave());

    const QString target = fixture.path(QStringLiteral("after-whole-write.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    const bool isFinished = AppToolShellFixture::waitFor([&finishedSpy]() { return !finishedSpy.isEmpty(); });
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    QVERIFY(isFinished);
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return !verifier->isVerifying(); }));
    AppToolShellFixture::settle();
    QVERIFY(document->isSignedRevisionGone());

    // The written bytes do not hold the signature
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatus(document->getSignatureResults().front(), false), pdf::PDFComponentPainter::SignatureStatus::Invalid);

    QVERIFY(document->getUndoStack()->undo());
    AppToolShellFixture::settle();
    QVERIFY2(document->isWholeFileRewritten(), "The kept version has its flag back");
    QVERIFY(!document->isSignatureLostOnSave());
    QVERIFY(!verifier->isSignatureLostOnSave());
    QVERIFY2(!item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
    QCOMPARE(ToolShellFlowHelper::bannerTitle(panel), QStringLiteral("One signature is not valid"));
    QVERIFY2(!ToolShellFlowHelper::bannerText(panel).contains(QStringLiteral("Undo")), qPrintable(ToolShellFlowHelper::bannerText(panel)));
    QVERIFY(QString::fromUtf8(pdf::PDFAppMcpDocumentReader::getSignatures(document, false)).contains(QStringLiteral("\"invalidAfterSaving\":false")));
}

void AppToolShellTest::signatureNoteNamesOnlyAPossibleUndo()
{
    // "Undo brings the signed version back" is true only while undo can do it
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = ToolShellFlowHelper::openSignedCopy(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentSignatureVerifier* firstVerifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab)->getSignatureVerifier();
    QVERIFY(AppToolShellFixture::waitFor([firstVerifier]() { return firstVerifier->getVerification() && !firstVerifier->isVerifying(); }));
    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();
    QVERIFY(tab->getDocument()->getUndoStack()->canUndoWholeFileRewrite());

    // A copy of the kept version: the copy has no undo step, it is written whole too
    tab->executeCommand(pdf::PDFAppCommands::editCopy());
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentTab* copyTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(copyTab && copyTab != tab);
    QVERIFY(AppToolShellFixture::waitFor([copyTab]() { return copyTab->getDocument()->isReady(); }));
    pdf::PDFAppDocumentSignatureVerifier* verifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(copyTab)->getSignatureVerifier();
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return verifier->getVerification() && !verifier->isVerifying(); }));
    QVERIFY(copyTab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentSignaturesPanel* panel = copyTab->findChild<pdf::PDFAppDocumentSignaturesPanel*>(QStringLiteral("signaturesPanel"));
    QVERIFY(panel);
    QVERIFY(verifier->isSignatureLostOnSave());
    QCOMPARE(ToolShellFlowHelper::bannerTitle(panel), QStringLiteral("The signature will not be valid after saving"));
    QVERIFY2(!ToolShellFlowHelper::bannerText(panel).contains(QStringLiteral("Undo")), qPrintable(ToolShellFlowHelper::bannerText(panel)));
}

void AppToolShellTest::signatureNoteOfInvalidSignature()
{
    // A signature, which is invalid already, has nothing to lose: red stays red, no "will not be valid"
    AppToolShellFixture fixture;
    QByteArray bytes = ToolShellTestHelper::readFile(pdftest::AppDocumentSamples::signedDocument(fixture.folder()));

    // A byte of the comment after the header: read the same, covered by the signature
    const qsizetype comment = bytes.indexOf('%', 1);
    QVERIFY(comment > 0 && comment < 20);
    bytes[comment + 1] = char(bytes[comment + 1] ^ 0x01);
    const QString brokenFile = fixture.path(QStringLiteral("broken-signature.pdf"));
    {
        QFile file(brokenFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    pdf::PDFAppDocumentTab* tab = ToolShellFlowHelper::openSignedCopy(fixture, brokenFile);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentSignatureVerifier* verifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab)->getSignatureVerifier();
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return verifier->getVerification() && !verifier->isVerifying(); }));
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatus(document->getSignatureResults().front(), false), pdf::PDFComponentPainter::SignatureStatus::Invalid);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentSignaturesPanel* panel = tab->findChild<pdf::PDFAppDocumentSignaturesPanel*>(QStringLiteral("signaturesPanel"));
    QVERIFY(panel);
    pdf::PDFAppSignatureStatusItem* item = tab->findChild<pdf::PDFAppSignatureStatusItem*>(QStringLiteral("signatureStatusItem"));
    QVERIFY(item);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();

    QVERIFY2(document->isSignatureLostOnSave(), "The document itself is what an assistant reads");
    QVERIFY(!verifier->isSignatureLostOnSave());
    QCOMPARE(ToolShellFlowHelper::bannerTitle(panel), QStringLiteral("One signature is not valid"));
    QVERIFY2(!item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
}

void AppToolShellTest::signatureNoteEndsWithTheSameBytes()
{
    // A save of a kept version, whose bytes are the same as before (nothing to verify): the note ends all the same
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = ToolShellFlowHelper::openSignedCopy(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentSignatureVerifier* verifier = pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab)->getSignatureVerifier();
    QVERIFY(AppToolShellFixture::waitFor([verifier]() { return verifier->getVerification() && !verifier->isVerifying(); }));
    pdf::PDFAppSignatureStatusItem* item = tab->findChild<pdf::PDFAppSignatureStatusItem*>(QStringLiteral("signatureStatusItem"));
    QVERIFY(item);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    AppToolShellFixture::settle();
    QVERIFY2(item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));

    document->setSavedAs(document->getVersion(), fixture.path(QStringLiteral("same-bytes.pdf")), document->getOriginalData());
    QVERIFY(!verifier->isVerifying());
    QVERIFY2(!item->text().contains(QStringLiteral("not valid after saving")), qPrintable(item->text()));
}

void AppToolShellTest::saveNeverPromisesLostRevision()
{
    // After a whole write the signed revision is gone from the bytes: a next Save has nothing to keep and
    // the dialog save-signed, which says it stays, does not come - also after undo, which gives back a
    // version without the flag of a rewritten document.
    AppToolShellFixture fixture;
    const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* signedTab = fixture.open(signedFile);
    QVERIFY(signedTab && AppToolShellFixture::waitFor([signedTab]() { return signedTab->getDocument()->isReady(); }));
    signedTab->executeCommand(pdf::PDFAppCommands::editCopy());
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentTab* tab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(tab && tab != signedTab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(pdf::PDFAppDocumentFileWriter::getIncrementalSignatureCount(document->getSnapshot()) > 0);

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));

    const QString target = fixture.path(QStringLiteral("whole.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    const bool isFinished = AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 1; });
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    QVERIFY(isFinished);
    AppToolShellFixture::settle();
    QVERIFY(document->getSnapshot().isSignedRevisionGone);
    QCOMPARE(pdf::PDFAppDocumentFileWriter::getIncrementalSignatureCount(document->getSnapshot()), 0);

    // Undo gives back the signed content in the bytes of the whole write: no promise, no question
    QVERIFY(document->getUndoStack()->undo());
    AppToolShellFixture::settle();
    QCOMPARE(pdf::PDFAppDocumentFileWriter::getIncrementalSignatureCount(document->getSnapshot()), 0);
    QVERIFY(handler->save());
    AppToolShellFixture::settle();
    QVERIFY2(!fixture.window->findChild<QWidget*>(QStringLiteral("saveSignedDialog")), "The dialog promises a signed version, which is gone");
    QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.size() == 2; }));
    QVERIFY(finishedSpy.at(1).at(0).toBool());
    QVERIFY(!document->isModified());

    // A file opened again is a new base: its bytes are what they are
    QVERIFY(pdf::PDFAppDocumentFileWriter::getIncrementalSignatureCount(signedTab->getDocument()->getSnapshot()) > 0);
}

void AppToolShellTest::dialogCopyMarkOfTheWeb()
{
#ifdef Q_OS_WIN
    // O18: a copy of a document without a file (Edit a Copy) takes the mark of its origin, as every other way of a save
    AppToolShellFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Downloaded.pdf"), 2);
    const QByteArray zone("[ZoneTransfer]\r\nZoneId=3\r\n");
    {
        QFile stream(QDir::toNativeSeparators(file) + QStringLiteral(":Zone.Identifier"));
        if (!stream.open(QIODevice::WriteOnly) || stream.write(zone) != zone.size())
        {
            QSKIP("The folder of the test has no alternate data streams (not NTFS)");
        }
    }

    pdf::PDFAppDocumentTab* sourceTab = fixture.open(file);
    QVERIFY(sourceTab && AppToolShellFixture::waitFor([sourceTab]() { return sourceTab->getDocument()->isReady(); }));
    sourceTab->executeCommand(pdf::PDFAppCommands::editCopy());
    AppToolShellFixture::settle();
    pdf::PDFAppDocumentTab* tab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(tab && tab != sourceTab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    QVERIFY(tab->getDocument()->getFilePath().isEmpty());

    PDFAppToolResultDialog* dialog = fixture.openDialog(tab, TOOL_COMPRESS, QStringLiteral("title"));
    QVERIFY(dialog);
    QSignalSpy decidedSpy(dialog, &PDFAppToolResultDialog::decided);
    dialog->selectChoice(Choice::Copy);
    dialog->getFileNameField()->setText(QStringLiteral("Marked_copy"));
    dialog->getOpenCheckBox()->setChecked(false);

    // A document without a file offers the documents of the user: the folder of the test by Browse…
    const QString folder = fixture.folder();
    pdf::PDFAppFileDialogs::setFolderHandler([folder](QWidget*, const QString&) { return folder; });
    dialog->getBrowseButton()->click();
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
    const QString target = dialog->getTargetFilePath();
    QCOMPARE(QFileInfo(target).absolutePath(), QFileInfo(fixture.path(QStringLiteral("a"))).absolutePath());
    dialog->confirm();
    QVERIFY(ToolShellTestHelper::waitForDecision(decidedSpy));
    QCOMPARE(ToolShellTestHelper::readFile(QDir::toNativeSeparators(target) + QStringLiteral(":Zone.Identifier")), zone);
#else
    QSKIP("Alternate data streams exist only on Windows");
#endif
}

void AppToolShellTest::shellSignedCopyOnly()
{
    // O31: a signed document with a tool of the impact LostCopyOnly offers through the
    // shell only the copy; an IncrementalSave tool keeps its choices
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    const quint64 version = tab->getDocument()->getVersion();

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QVERIFY(dialog->getSignedWarning());
    QVERIFY(!dialog->getOption(Choice::Keep));
    QVERIFY(!dialog->getOption(Choice::Overwrite));
    QVERIFY(dialog->getOption(Choice::Copy));
    dialog->getDiscardButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(tab->getDocument()->getVersion(), version);

    shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QVERIFY(!dialog->getSignedWarning());
    QVERIFY(dialog->getOption(Choice::Keep));
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellOpenFolderAfterExport()
{
    // Open the folder afterwards (339:9601): the page asks for the folder of the written
    // file through the navigator, which checks the address
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("files"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), PDFAppToolResultDialog::ResultKind::Files);
    dialog->getFilePathField()->setText(fixture.path(QStringLiteral("extracted-shell.txt")));
    dialog->getOpenCheckBox()->setChecked(true);

    // One opening through the system (no window of the file manager in a test)
    UrlRecorder recorder;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &recorder, "record");
    fixture.window->getNavigator()->setExternalUrlsEnabled(true);
    QSignalSpy urlSpy(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    fixture.window->getNavigator()->setExternalUrlsEnabled(false);
    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    QCOMPARE(urlSpy.size(), 1);
    QCOMPARE(QDir(urlSpy.first().first().toUrl().toLocalFile()), QDir(fixture.folder()));
    QCOMPARE(recorder.urls.size(), 1);
    QCOMPARE(QDir(recorder.urls.first().toLocalFile()), QDir(fixture.folder()));
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("extracted-shell.txt"))));
}

void AppToolShellTest::shellToolsPanelRoute()
{
    // The Tools panel opens a tool with a registered GUI on the document by its route
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    QVERIFY(!fixture.tools().setPinned(TOOL_COMPRESS, true).isError());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);

    pdf::PDFAppDocumentToolbar* toolbar = tab->getView()->getToolbar();
    pdf::PDFAppDocumentToolsPanel* panel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel::Tools));
    QVERIFY(panel);
    AppToolShellFixture::settle();
    pdf::PDFToolShortcut* shortcut = nullptr;
    for (pdf::PDFToolShortcut* item : panel->getPinnedShortcuts())
    {
        if (pdf::PDFAppToolPresentation::getToolId(item) == TOOL_COMPRESS)
        {
            shortcut = item;
        }
    }
    QVERIFY(shortcut);
    shortcut->click();
    AppToolShellFixture::settle();
    QVERIFY(AppToolShellFixture::getShell(tab));
    QCOMPARE(AppToolShellFixture::getShell(tab)->getTool()->getId(), TOOL_COMPRESS);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(fixture.currentTab(), static_cast<pdf::PDFAppTab*>(tab));

    // The same route from elsewhere (the catalog in another window part) shows the same tool
    QPointer<pdf::PDFDocumentToolShell> shell = AppToolShellFixture::getShell(tab);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_COMPRESS));
    AppToolShellFixture::settle();
    QCOMPARE(AppToolShellFixture::getShell(tab), shell.data());
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellMinimalWindow()
{
    // 720 x 480: the toolbar narrows as the toolbar of the document, nothing overlaps, the
    // Primary button stays; the window is not made wider
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture(QSize(720, 480));
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS, false, QString(), true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->width(), 720);

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(toolbar->width(), 720);
    QVERIFY(toolbar->getCollapseLevel() > 0);
    QVERIFY(toolbar->getConfirmButton()->isVisible());
    QCOMPARE(H::rectIn(toolbar->getConfirmButton(), toolbar).right() + 1, 708);
    QVERIFY(H::rectIn(toolbar->getTutorialButton(), toolbar).left() >= 0);
    QVERIFY(toolbar->getSidebarButton()->isVisible());

    // The right panel keeps its width, its long content scrolls
    QCOMPARE(shell->getRightPanel()->width(), 320);
    QVERIFY(shell->getRightPanel()->height() < shell->getRightPanel()->getContentParent()->sizeHint().height());
    QCOMPARE(shell->getRightPanel()->getScrollArea()->horizontalScrollBar()->maximum(), 0);
    QVERIFY(shell->getRightPanel()->getScrollArea()->verticalScrollBar()->maximum() > 0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(fixture.window.data(), 1.0)), 0);

    // Wider again: everything comes back
    fixture.window->resize(1440, 960);
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QVERIFY(toolbar->getTitleLabel()->isVisible());
    QVERIFY(toolbar->getTutorialButton()->isVisible());
    QCOMPARE(toolbar->getTutorialButton()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QVERIFY(toolbar->getUndoButton()->isVisible());
    shell->cancel();
    AppToolShellFixture::settle();

    // The segments of Compress at 720 x 480 (four of 91 in 364 + the Primary button with
    // its text are 730): the Primary button has only its icon, its text in the tooltip and
    // for assistive technologies, Cancel stays, the four segments whole
    fixture.window->resize(720, 480);
    pdf::PDFDocumentToolShell* segmentsShell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(segmentsShell);
    AppToolShellFixture::settle();
    pdf::PDFAppToolToolbar* segmentsBar = segmentsShell->getToolbar();
    pdf::PDFSegmentedControl* segments = AppToolShellFixture::getGUI(segmentsShell)->segments->getSegmentedControl();
    const QString compressTitle = pdf::PDFDocumentToolCatalog::findTool(TOOL_COMPRESS)->getTitle();
    QCOMPARE(segments->width(), 364);
    QVERIFY(segmentsBar->getCancelButton()->isVisible());
    QVERIFY(H::rectIn(segments, segmentsBar).right() < H::rectIn(segmentsBar->getCancelButton(), segmentsBar).left());
    QCOMPARE(segmentsBar->getConfirmButton()->text(), QString());
    QCOMPARE(segmentsBar->getConfirmButton()->toolTip(), compressTitle);
    QCOMPARE(segmentsBar->getConfirmButton()->accessibleName(), compressTitle);
    QCOMPARE(segmentsBar->getConfirmText(), compressTitle);
    QCOMPARE(H::rectIn(segmentsBar->getConfirmButton(), segmentsBar).right() + 1, 708);
    QVERIFY(H::rectIn(segmentsBar->getConfirmButton(), segmentsBar).left() > H::rectIn(segmentsBar->getCancelButton(), segmentsBar).right());

    // Wider again: the text comes back
    fixture.window->resize(1440, 960);
    AppToolShellFixture::settle();
    QCOMPARE(segmentsBar->getConfirmButton()->text(), compressTitle);
    QVERIFY(segmentsBar->getConfirmButton()->toolTip().isEmpty());
    segmentsShell->cancel();
    AppToolShellFixture::settle();

    // The most actions of a toolbar: nothing overlaps, each action keeps its size, Cancel
    // gives its place up (Esc stays) and the Primary button stays at the edge
    fixture.window->resize(720, 480);
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFDocumentToolShell* narrow = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(narrow);
    AppToolShellFixture::settle();
    pdf::PDFAppToolToolbar* narrowBar = narrow->getToolbar();
    ShellTestGUI* narrowGui = AppToolShellFixture::getGUI(narrow);
    QVERIFY(!narrowBar->getCancelButton()->isVisible());
    QCOMPARE(H::rectIn(narrowBar->getConfirmButton(), narrowBar).right() + 1, 708);
    QWidget* const actions[] = { narrowGui->segments->getSegmentedControl(), narrowGui->flagsMenu->getButton(), narrowGui->selectAllAction, narrowGui->enumMenu->getButton() };
    int lastRight = 0;
    for (const QWidget* action : actions)
    {
        const QRect rect = H::rectIn(action, narrowBar);
        QVERIFY2(action->isVisible(), qPrintable(action->objectName()));
        QVERIFY2(rect.left() > lastRight, qPrintable(action->objectName()));
        QCOMPARE(rect.width(), action == actions[0] ? 364 : action->sizeHint().width());
        lastRight = rect.right();
    }
    QVERIFY(lastRight < H::rectIn(narrowBar->getConfirmButton(), narrowBar).left());

    // Esc is the Cancel of a hidden button
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    tab->getView()->getPdfWidget()->setFocus();
    QTest::keyClick(tab->getView()->getPdfWidget(), Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
}

void AppToolShellTest::shellThemesAndScales_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("scale");
    for (const bool isDark : { false, true })
    {
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::addRow("%s-%g", isDark ? "dark" : "light", scale) << isDark << scale;
        }
    }
}

void AppToolShellTest::shellThemesAndScales()
{
    // Pixel probes of the tokens of 291:994 and no leak color in the running state too
    using H = ToolShellFlowHelper;
    QFETCH(bool, isDark);
    QFETCH(qreal, scale);

    AppToolShellFixture fixture;
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE, true);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QImage image = AppToolShellFixture::render(toolbar, scale);
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
    QCOMPARE(H::pixel(image, QPoint(4, 26), scale), H::token(pdf::PDFThemeColors::BgSubtle));
    QCOMPARE(H::pixel(image, QPoint(4, 51), scale), H::token(pdf::PDFThemeColors::BorderDefault));

    // The fill of the Primary button is bg/accent
    const QRect confirm = H::rectIn(toolbar->getConfirmButton(), toolbar);
    QCOMPARE(H::pixel(image, QPoint(confirm.left() + 6, confirm.center().y()), scale), H::token(pdf::PDFThemeColors::BgAccent));

    image = AppToolShellFixture::render(shell->getRightPanel(), scale);
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
    QCOMPARE(H::pixel(image, QPoint(6, 6), scale), H::token(pdf::PDFThemeColors::BgSidebar));
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    const QPoint inCard = H::rectIn(gui->keyValues->parentWidget(), shell->getRightPanel()).topLeft() + QPoint(6, 3);
    QCOMPARE(H::pixel(image, inCard, scale), H::token(pdf::PDFThemeColors::BgSurface));
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(shell->getLeftPanel(), scale)), 0);

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    image = AppToolShellFixture::render(shell->getRightPanel(), scale);
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
    const QRect card = H::rectIn(shell->getProgressCard(), shell->getRightPanel());
    QCOMPARE(H::pixel(image, card.topLeft() + QPoint(6, 6), scale), H::token(pdf::PDFThemeColors::BgSurface));
    const QRect callout = H::rectIn(shell->getBusyCallout(), shell->getRightPanel());
    QCOMPARE(H::pixel(image, callout.topLeft() + QPoint(6, 6), scale), pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Background) == pdf::PDFThemeColors::Color() ? QColor() : H::token(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Background)));

    // A disabled label has text/disabled: the title of the section is not text/primary any more
    QCOMPARE(gui->resultHeader->getTitleLabel()->palette().color(QPalette::Disabled, QPalette::WindowText), H::token(pdf::PDFThemeColors::TextDisabled));
    QVERIFY(!gui->resultHeader->getTitleLabel()->isEnabled());

    shell->stop();
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellRightToLeft()
{
    using H = ToolShellFlowHelper;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(H::rectIn(toolbar->getSidebarButton(), toolbar).right() + 1, 1428);
    QCOMPARE(H::rectIn(toolbar->getConfirmButton(), toolbar).left(), 12);
    QVERIFY(H::rectIn(toolbar->getCancelButton(), toolbar).left() > H::rectIn(toolbar->getConfirmButton(), toolbar).right());

    // The right panel lies at the leading side (the view mirrors it), its line towards the canvas
    pdf::PDFAppToolSidePanel* panel = shell->getRightPanel();
    QCOMPARE(H::rectIn(panel, tab->getView()).left(), 0);
    QImage image = AppToolShellFixture::render(panel, 1.0);
    QCOMPARE(H::pixel(image, QPoint(panel->width() - 1, 400), 1.0), H::token(pdf::PDFThemeColors::BorderDefault));
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(toolbar, 1.0)), 0);

    // The contents mirrored: the section header at the right edge of the panel padding
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    QVERIFY(H::rectIn(gui->resultHeader->getTitleLabel(), panel).right() > panel->width() / 2);

    // The panel at the leading edge of its button
    pdf::PDFAppToolPanelPopup* popup = toolbar->openPanel(gui->flagsMenu->getButton());
    QVERIFY(popup);
    AppToolShellFixture::settle();
    const int panelRight = popup->mapToGlobal(popup->getPanelRect().topRight()).x();
    const int buttonRight = gui->flagsMenu->getButton()->mapToGlobal(gui->flagsMenu->getButton()->rect().topRight()).x();
    QCOMPARE(panelRight, buttonRight);
    popup->close();
    AppToolShellFixture::settle();
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellAccessibility()
{
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(toolbar->getUndoButton()->accessibleName(), QStringLiteral("Undo"));
    QCOMPARE(toolbar->getRedoButton()->accessibleName(), QStringLiteral("Redo"));
    QCOMPARE(toolbar->getSidebarButton()->accessibleName(), QStringLiteral("Sidebar"));
    QCOMPARE(toolbar->getTitleGroup()->accessibleName(), QStringLiteral("Compress"));
    QCOMPARE(toolbar->getTutorialButton()->accessibleName(), QStringLiteral("Tutorial"));

    // The tooltips of the commands name the shortcuts of the user
    QVERIFY2(toolbar->getUndoButton()->toolTip().contains(QStringLiteral("Ctrl+Z")), qPrintable(toolbar->getUndoButton()->toolTip()));

    // The window announces the tool as its page
    QCOMPARE(fixture.window->getNavigator()->getLastAnnouncement(), QStringLiteral("Compress page"));
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellLanguageChange()
{
    // The parts come again with new texts, the values and the state stay
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_COMPRESS);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best"));
    QPointer<QWidget> oldActions = shell->getToolActions();
    AppToolShellFixture::settle();

    ToolShellFlowTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();

    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(toolbar->getCancelButton()->text(), QStringLiteral("XX Cancel"));
    QCOMPARE(toolbar->getTutorialButton()->text(), QStringLiteral("XX Tutorial"));
    QVERIFY(!oldActions);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    QCOMPARE(gui->segments->getSegmentedControl()->getCurrentIndex(), 2);
    QVERIFY(gui->flagsMenu->getButton()->isVisible());
    QCOMPARE(toolbar->getOpenPanel(), nullptr);

    // A running state after the change: the card with the new texts
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    QVERIFY(shell->getProgressCard()->getTitleLabel()->text().startsWith(QStringLiteral("XX ")));

    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getStopButton()->text(), QStringLiteral("Stop"));
    QCOMPARE(shell->getState(), H::State::Running);
    QVERIFY(shell->getProgressCard() && shell->getProgressCard()->isVisible());
    QVERIFY(shell->getBusyCallout() && shell->getBusyCallout()->isVisible());
    QVERIFY(!AppToolShellFixture::getGUI(shell)->steps->isEnabled());
    shell->stop();
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellDeletion_data()
{
    QTest::addColumn<QString>("victim");
    QTest::addColumn<QString>("state");
    for (const char* victim : { "tab", "window", "page", "shell" })
    {
        for (const char* state : { "editing", "running", "dialog", "panel", "question", "refusal" })
        {
            QTest::addRow("%s-%s", victim, state) << QString::fromLatin1(victim) << QString::fromLatin1(state);
        }
    }
}

void AppToolShellTest::shellDeletion()
{
    // The tab, the window, the page and the shell deleted in each state; nothing waits for
    // the worker, the operation ends, the view works afterwards
    using H = ToolShellFlowHelper;
    QFETCH(QString, victim);
    QFETCH(QString, state);

    AppToolShellFixture fixture;

    // Remove External Links needs the permission of the annotations, which the sample does not give
    const QString toolId = state == QLatin1String("refusal") ? QStringLiteral("protect.remove-links") : TOOL_ROTATE;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(toolId);
    QVERIFY(tool);
    QPointer<pdf::PDFAppDocumentTab> tab = state == QLatin1String("refusal") ? H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder())) : H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();

    const auto destroyVictim = [&]()
    {
        if (victim == QLatin1String("tab"))
        {
            delete tab.data();
        }
        else if (victim == QLatin1String("window"))
        {
            delete fixture.window.data();
        }
        else if (victim == QLatin1String("page"))
        {
            delete tab->getPage();
        }
        else
        {
            delete shell.data();
        }
    };

    if (state == QLatin1String("question"))
    {
        // F14: the victim goes while the question of a run is open, inside of its own loop
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
        AppToolShellFixture::settle();

        QPointer<pdf::PDFAppDocumentChoiceDialog> question;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &timer, [&]()
        {
            question = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>();
            if (question)
            {
                destroyVictim();
            }
        });
        timer.start(0);
        fixture.window->getNavigator()->goBack(tab);
        AppToolShellFixture::settle();
        QVERIFY(!question || !question->isVisible());
        QVERIFY(!shell || shell->getState() == H::State::Closed);
        if (tab && view)
        {
            QVERIFY(!view->isToolModeActive());
            QVERIFY(!tab->getDocument()->isBlockedByOperation());
        }

        tool->setHold(false);
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
        AppToolShellFixture::settle();
        return;
    }

    if (state == QLatin1String("refusal"))
    {
        // 339:10328 is open
        AppToolShellFixture::settle();
        QVERIFY(shell->getPermissionDialog());
    }
    else if (state == QLatin1String("running"))
    {
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    }
    else if (state == QLatin1String("dialog"))
    {
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
        QVERIFY(H::confirmAndWait(shell));
    }
    else if (state == QLatin1String("panel"))
    {
        QVERIFY(shell->getToolbar()->openPanel(AppToolShellFixture::getGUI(shell)->flagsMenu->getButton()));
    }
    AppToolShellFixture::settle();

    QPointer<PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QPointer<pdf::PDFMessageBox> refusal = shell->getPermissionDialog();
    destroyVictim();
    AppToolShellFixture::settle();

    QVERIFY(!dialog || !dialog->isVisible());
    QVERIFY(!refusal || !refusal->isVisible());
    if (tab && view)
    {
        QVERIFY(!view->isToolModeActive());
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
    }
    if (fixture.window)
    {
        QVERIFY(fixture.window->isVisible());
    }

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellScreenshots()
{
    // PDF4QT_APP_SCREENSHOTS writes the states of the shell of the test tool
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = ToolShellFlowHelper;
    for (const bool isDark : { false, true })
    {
        const QString suffix = isDark ? QStringLiteral("-dark") : QStringLiteral("-light");
        AppToolShellFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_COMPRESS, false, QStringLiteral("thumbnails"));
        QVERIFY(tool);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 12);
        QVERIFY(tab);
        pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_COMPRESS);
        QVERIFY(shell);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-291-994") + suffix);

        // The panel 303:4560
        pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(AppToolShellFixture::getGUI(shell)->flagsMenu->getButton());
        QVERIFY(panel);
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-303-4405") + suffix);
        AppToolShellFixture::screenshotDialog(panel, QStringLiteral("tool-shell-panel-303-4560") + suffix);
        panel->close();
        AppToolShellFixture::settle();

        // The state 295:1711
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-295-1711") + suffix);
        shell->stop();
        tool->setHold(false);
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
        AppToolShellFixture::settle();
        shell->cancel();
        AppToolShellFixture::settle();
    }

    {
        AppToolShellFixture fixture;
        QVERIFY(fixture.registerShellTool(TOOL_ROTATE, true));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 48);
        QVERIFY(tab);
        tab->getView()->setSelectedPages({ 8, 9, 14 });
        QVERIFY(fixture.openTool(tab, TOOL_ROTATE));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-389-20177"));
    }
    {
        AppToolShellFixture fixture(QSize(720, 480));
        QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        QVERIFY(fixture.openTool(tab, TOOL_COMPRESS));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-720x480"));
    }
    {
        QApplication::setLayoutDirection(Qt::RightToLeft);
        AppToolShellFixture fixture;
        QVERIFY(fixture.registerShellTool(TOOL_COMPRESS));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
        QVERIFY(tab);
        QVERIFY(fixture.openTool(tab, TOOL_COMPRESS));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("tool-shell-rtl"));
    }
}

// ------------------------------------------------- the interface for a thin tool

/// Optimize Structure as a thin tool writes it (303:4405, 303:4560), on the parameters of
/// the test tool: the real tool comes with package 2
class ThinStructureGUI : public pdf::PDFAppDeclarativeToolGUI
{
public:
    static std::vector<pdf::PDFAppToolFlagItem> getItems()
    {
        struct Step { const char* title; const char* description; const char* panelDescription; const char* icon; };
        static const Step steps[] = {
            { "Embed simple objects", "Writes small values in place", "Small values are written in place instead of being referenced", "embed" },
            { "Remove null objects", "Drops entries without a value", "Entries without a value are dropped", "eraser" },
            { "Remove unused objects", "Deletes leftovers of earlier edits", "Leftovers of earlier edits are deleted", "trash" },
            { "Merge identical objects", "Stores duplicates once", "Duplicates are stored once", "copy" },
            { "Shrink object storage", "Renumbers the objects", "Objects are renumbered so no gaps remain", "hash" },
            { "Recompress flate streams", "Slowest step, saves the most", "Takes the longest, saves the most", "minimize-2" }
        };

        std::vector<pdf::PDFAppToolFlagItem> items;
        for (int i = 0; i < pdftest::ShellTestTool::STEP_COUNT; ++i)
        {
            pdf::PDFAppToolFlagItem item;
            item.parameter = pdftest::ShellTestTool::getStepName(i);
            item.title = QString::fromLatin1(steps[i].title);
            item.description = QString::fromLatin1(steps[i].panelDescription);
            item.iconName = QString::fromLatin1(steps[i].icon);
            item.offDescription = QStringLiteral("Will be skipped");
            items.push_back(item);
        }
        return items;
    }

    QString getConfirmText() const override { return QStringLiteral("Optimize Structure"); }
    QString getInitialSidebarPanel() const override { return QStringLiteral("thumbnails"); }
    int getRightPanelSpacing() const override { return 16; }

    void fillResultInput(pdf::PDFAppToolResultInput* input) const override
    {
        input->title = QStringLiteral("The structure is optimized");
    }

    void selectAll() { menu->setAll(true); }
    void clearAll() { menu->setAll(false); }

    pdf::PDFAppToolFlagsMenu* menu = nullptr;
    pdf::PDFTextLink* restoreLink = nullptr;
    QToolButton* selectAllButton = nullptr;
    QToolButton* clearButton = nullptr;
    pdf::PDFKeyValueList* documentFacts = nullptr;
    pdf::PDFAppToolStepList* steps = nullptr;
    pdf::PDFAppParagraph* resultText = nullptr;

protected:
    void describeToolbar(pdf::PDFAppToolPanelBuilder& builder) override
    {
        menu = builder.addFlagsMenu(getItems(), QStringLiteral("list"), QStringLiteral("Steps: %1 of %2"), 520, QStringLiteral("Optimization steps"));
        menu->getPanel()->addDividerRow();
        restoreLink = menu->getPanel()->addLink(QStringLiteral("Restore defaults"));
        connect(restoreLink, &pdf::PDFTextLink::clicked, menu, &pdf::PDFAppToolFlagsMenu::restoreDefaults);
        builder.addActionDivider();
        selectAllButton = builder.addAction(QStringLiteral("Select all"), QStringLiteral("check-circle"), this, &ThinStructureGUI::selectAll);
        clearButton = builder.addAction(QStringLiteral("Clear"), QStringLiteral("x"), this, &ThinStructureGUI::clearAll);
    }

    void describeRightPanel(pdf::PDFAppToolPanelBuilder& builder) override
    {
        builder.addSection(QStringLiteral("Document"));
        documentFacts = builder.addKeyValues();
        documentFacts->addRow(QStringLiteral("Objects"), QStringLiteral("18,240"));
        builder.addSection(QStringLiteral("Steps to run"));
        steps = builder.addSteps(getItems());
        builder.addSection(QStringLiteral("Result"));
        resultText = builder.addTextCard(QStringLiteral("The size is measured when the tool runs."));
    }
};

/// The parameters of the menus of Extract Text 339:8765
class MenusTestTool : public pdftest::ShellTestTool
{
public:
    using pdftest::ShellTestTool::ShellTestTool;

    pdf::PDFDocumentToolParameterDefinitions getParameters() const override
    {
        using Definition = pdf::PDFDocumentToolParameterDefinition;

        pdf::PDFDocumentToolParameterDefinitions definitions = pdftest::ShellTestTool::getParameters();
        definitions.push_back(Definition::createEnum(pdftest::TestDocumentTool::tr, QStringLiteral("format"), QT_TR_NOOP("Format"), nullptr, {
            { QStringLiteral("text"), QT_TR_NOOP("Plain text"), QT_TR_NOOP("A .txt file"), QStringLiteral("file-text") },
            { QStringLiteral("html"), QT_TR_NOOP("HTML"), QT_TR_NOOP("Headings and paragraphs for a browser"), QStringLiteral("code") },
            { QStringLiteral("xml"), QT_TR_NOOP("XML"), QT_TR_NOOP("Structured, for other programs"), QStringLiteral("code") } }, QStringLiteral("text")));
        definitions.push_back(Definition::createEnum(pdftest::TestDocumentTool::tr, QStringLiteral("encoding"), QT_TR_NOOP("Encoding"), nullptr, {
            { QStringLiteral("utf-8"), QT_TR_NOOP("UTF-8"), QT_TR_NOOP("Recommended, every language"), QStringLiteral("globe") },
            { QStringLiteral("latin-1"), QT_TR_NOOP("Latin-1"), QT_TR_NOOP("Western European legacy programs"), QStringLiteral("globe") },
            { QStringLiteral("system"), QT_TR_NOOP("System"), QT_TR_NOOP("The encoding of this computer"), QStringLiteral("globe") } }, QStringLiteral("utf-8")));
        return definitions;
    }
};

/// The menus of the frames, which the builder has to make: Include (a flag and a group),
/// Format (two parameters), Preset (rows of the tool, no parameter)
class ThinMenusGUI : public pdf::PDFAppDeclarativeToolGUI
{
public:
    void onPresetChosen()
    {
        QVariantMap values;
        for (int i = 0; i < pdftest::ShellTestTool::STEP_COUNT; ++i)
        {
            values.insert(pdftest::ShellTestTool::getStepName(i), i < 2);
        }
        getModel()->setValues(values);
        presetPanel->close();
    }

    pdf::PDFAppToolFlagsMenu* includeMenu = nullptr;
    pdf::PDFAppToolEnumMenu* formatMenu = nullptr;
    pdf::PDFAppToolPanelPopup* presetPanel = nullptr;

protected:
    void describeToolbar(pdf::PDFAppToolPanelBuilder& builder) override
    {
        static const char* const titles[] = { "Page numbers", "Structure titles", "Languages", "Alternative descriptions", "Expanded forms", "Actual text" };
        static const char* const descriptions[] = { "A line “Page N” starts every page", "Titles of tagged parts", "Language marks of tagged parts", "Text that describes pictures", "Abbreviations written out", "Replacement text of tagged content" };
        std::vector<pdf::PDFAppToolFlagItem> items;
        for (int i = 0; i < pdftest::ShellTestTool::STEP_COUNT; ++i)
        {
            pdf::PDFAppToolFlagItem item;
            item.parameter = pdftest::ShellTestTool::getStepName(i);
            item.title = QString::fromUtf8(titles[i]);
            item.description = QString::fromUtf8(descriptions[i]);
            item.iconName = QStringLiteral("hash");
            item.groupCaption = i == 1 ? QStringLiteral("FROM THE TAGS OF THE DOCUMENT") : QString();
            items.push_back(item);
        }
        includeMenu = builder.addFlagsMenu(items, QStringLiteral("list"), QStringLiteral("Include: %1 extra"), 280, QString(), pdf::PDFTileRow::Kind::MenuItem);

        formatMenu = builder.addEnumMenu(QStringLiteral("format"), QStringLiteral("file-text"), QStringLiteral("Format: %1"), 260, QStringLiteral("FORMAT"));
        formatMenu->addGroup(QStringLiteral("encoding"), QStringLiteral("ENCODING"));

        presetPanel = builder.addMenu(QStringLiteral("preset"), QStringLiteral("sliders"), QStringLiteral("Preset: Standard"), 350);
        presetPanel->addGroupCaption(QStringLiteral("Presets tick the list – adjust it freely afterwards"));
        presetPanel->addRow(QStringLiteral("Standard"), QStringLiteral("Everything except the invisible text"), QStringLiteral("hash"), pdf::PDFTileRow::Kind::MenuItem);
        presetPanel->addRow(QStringLiteral("Before sending"), QStringLiteral("Info, metadata, comments, index and thumbnails"), QStringLiteral("hash"), pdf::PDFTileRow::Kind::MenuItem);
        presetPanel->addRow(QStringLiteral("Before publishing"), QStringLiteral("Also attachments – outline and page labels stay"), QStringLiteral("hash"), pdf::PDFTileRow::Kind::MenuItem);
        presetPanel->addRow(QStringLiteral("Everything"), QStringLiteral("All nine kinds including the OCR text layer"), QStringLiteral("hash"), pdf::PDFTileRow::Kind::MenuItem);
        presetPanel->addGroupDivider();
        pdf::PDFTileRow* clearRow = presetPanel->addRow(QStringLiteral("Clear all"), QStringLiteral("Untick everything and choose by hand"), QStringLiteral("hash"), pdf::PDFTileRow::Kind::MenuItem);
        connect(clearRow, &pdf::PDFTileRow::clicked, this, &ThinMenusGUI::onPresetChosen);
    }
};

void AppToolShellTest::shellThinToolStructure()
{
    // Optimize Structure as the thin tool of plan 2.6 (about 40 lines, the class above):
    // every part it needs is there and works as the frames 303:4405, 303:4560, 319:7343 say
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    const QString toolId = QStringLiteral("optimize.structure");
    QVERIFY(fixture.registerShellTool(toolId));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<ThinStructureGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    ThinStructureGUI* gui = dynamic_cast<ThinStructureGUI*>(shell->getGUI());
    QVERIFY(gui);
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();

    // Thumbnails at the entry, the confirmation of the tool, the fifth step off by default
    QCOMPARE(tab->getView()->getCurrentPanelId(), QStringLiteral("thumbnails"));
    QCOMPARE(toolbar->getConfirmButton()->text(), QStringLiteral("Optimize Structure"));
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 5 of 6"));
    QVERIFY(gui->documentFacts->isVisible());
    QVERIFY(gui->resultText->isVisible());

    // 319:7343: the step off has the mark minus, the muted tile and its own description
    pdf::PDFTileRow* offStep = gui->steps->getRow(pdftest::ShellTestTool::getStepName(4));
    QVERIFY(offStep);
    QCOMPARE(offStep->getDescription(), QStringLiteral("Will be skipped"));
    QCOMPARE(gui->steps->getRow(pdftest::ShellTestTool::getStepName(0))->getDescription(), ThinStructureGUI::getItems().front().description);
    QCOMPARE(gui->steps->size().height(), 270);

    // 303:4560: the panel 520 x 368, 4 px below the toolbar at its button, the rows of 46
    pdf::PDFAppToolPanelPopup* panel = toolbar->openPanel(gui->menu->getButton());
    QVERIFY(panel);
    AppToolShellFixture::settle();
    QCOMPARE(panel->getPanelRect().size(), QSize(520, 368));
    QCOMPARE(panel->getRows().size(), size_t(6));
    QCOMPARE(panel->getRows().front()->height(), 46);

    // A row toggles its step and the panel stays open; each toggle one undo step of the tool
    QVERIFY(!model->canUndo());
    QTest::mouseClick(panel->getRows()[4], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(panel->isVisible());
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 6 of 6"));
    QVERIFY(model->getValue(pdftest::ShellTestTool::getStepName(4)).toBool());
    QCOMPARE(offStep->getDescription(), ThinStructureGUI::getItems()[4].description);
    QVERIFY(model->canUndo());

    // Restore defaults of the panel: one undo step, the fifth step off again
    QTest::mouseClick(gui->restoreLink, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 5 of 6"));
    QVERIFY(!model->getValue(pdftest::ShellTestTool::getStepName(4)).toBool());
    panel->close();
    AppToolShellFixture::settle();

    // Clear and Select all of the toolbar: all the steps as one undo step
    gui->clearButton->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 0 of 6"));
    model->undo();
    AppToolShellFixture::settle();
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 5 of 6"));
    gui->selectAllButton->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->menu->getButton()->text(), QStringLiteral("Steps: 6 of 6"));

    // The title of the dialog is the one of the tool
    model->setValue(QStringLiteral("mode"), QStringLiteral("title"));
    PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
    QVERIFY(dialog);
    bool hasTitle = false;
    for (const QLabel* label : dialog->findChildren<QLabel*>())
    {
        hasTitle = hasTitle || label->text() == QStringLiteral("The structure is optimized");
    }
    QVERIFY(hasTitle);
    dialog->reject();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
}

void AppToolShellTest::shellMenusOfTheFrames()
{
    // 339:8765 and 438:12831: a flag with a group of flags, two parameters in one panel, the
    // rows of a tool; the sizes of the frames follow from the rows, captions and dividers
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT));
    pdf::PDFDocumentToolRegistry::unregisterTool(TOOL_EXTRACT);
    std::shared_ptr<MenusTestTool> tool = std::make_shared<MenusTestTool>(TOOL_EXTRACT);
    QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(tool), pdf::PDFDocumentToolRegistration::Registered);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_EXTRACT, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<ThinMenusGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);

    // A format with one number only must not print "QString::arg: Argument missing"
    QTest::failOnWarning(QRegularExpression(QStringLiteral("QString::arg")));
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    ThinMenusGUI* gui = dynamic_cast<ThinMenusGUI*>(shell->getGUI());
    QVERIFY(gui);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    pdf::PDFAppToolParameterModel* model = shell->getModel();

    // Include 339:9492: 280 x 361, the rows of 44, the caption of the group of 21; one
    // row on as in the frame ("Include: 1 extra"): only a chosen row has the check mark,
    // so only its text is narrower
    QCOMPARE(gui->includeMenu->getButton()->text(), QStringLiteral("Include: 5 extra"));
    QVariantMap oneStep;
    for (int i = 0; i < pdftest::ShellTestTool::STEP_COUNT; ++i)
    {
        oneStep.insert(pdftest::ShellTestTool::getStepName(i), i == 3);
    }
    QCOMPARE(model->setValues(oneStep), QString());
    QCOMPARE(gui->includeMenu->getButton()->text(), QStringLiteral("Include: 1 extra"));
    pdf::PDFAppToolPanelPopup* includePanel = toolbar->openPanel(gui->includeMenu->getButton());
    QVERIFY(includePanel);
    AppToolShellFixture::settle();
    QCOMPARE(includePanel->getPanelRect().size(), QSize(280, 361 - 46));    // One row less than the frame
    QCOMPARE(includePanel->getRows().front()->height(), 44);
    const pdf::PDFLabel* groupCaption = includePanel->findChild<pdf::PDFLabel*>(QStringLiteral("toolPanelGroupCaption"));
    QVERIFY(groupCaption);
    QCOMPARE(groupCaption->text(), QStringLiteral("FROM THE TAGS OF THE DOCUMENT"));
    QCOMPARE(groupCaption->font().weight(), QFont::DemiBold);
    QCOMPARE(groupCaption->font().pixelSize(), 11);
    const QWidget* captionRow = groupCaption->parentWidget();
    QCOMPARE(captionRow->height(), 21);
    QCOMPARE(captionRow->y() - includePanel->getRows()[0]->geometry().bottom() - 1, 2);
    QCOMPARE(includePanel->getRows()[1]->y() - captionRow->geometry().bottom() - 1, 2);
    includePanel->close();
    AppToolShellFixture::settle();

    // Format 339:9546: 260 x 388 with the rows of 44 and of 57 (a description of two lines)
    pdf::PDFAppToolPanelPopup* formatPanel = toolbar->openPanel(gui->formatMenu->getButton());
    QVERIFY(formatPanel);
    AppToolShellFixture::settle();
    QCOMPARE(formatPanel->getPanelRect().size(), QSize(260, 388));
    QCOMPARE(gui->formatMenu->getRow(QStringLiteral("text"))->height(), 44);
    QCOMPARE(gui->formatMenu->getRow(QStringLiteral("html"))->height(), 57);
    QCOMPARE(gui->formatMenu->getButton()->text(), QStringLiteral("Format: Plain text"));

    // A panel of two parameters stays open and each parameter has its own choice; the
    // button says the first one
    QVERIFY(gui->formatMenu->getRow(QStringLiteral("utf-8"))->isChecked());
    QTest::mouseClick(gui->formatMenu->getRow(QStringLiteral("html")), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(formatPanel->isVisible());
    QCOMPARE(model->getValue(QStringLiteral("format")).toString(), QStringLiteral("html"));
    QCOMPARE(gui->formatMenu->getButton()->text(), QStringLiteral("Format: HTML"));
    QVERIFY(gui->formatMenu->getRow(QStringLiteral("html"))->isChecked());
    QVERIFY(!gui->formatMenu->getRow(QStringLiteral("text"))->isChecked());
    QVERIFY(gui->formatMenu->getRow(QStringLiteral("utf-8"))->isChecked());

    QTest::mouseClick(gui->formatMenu->getRow(QStringLiteral("latin-1")), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(model->getValue(QStringLiteral("encoding")).toString(), QStringLiteral("latin-1"));
    QCOMPARE(model->getValue(QStringLiteral("format")).toString(), QStringLiteral("html"));
    QCOMPARE(gui->formatMenu->getButton()->text(), QStringLiteral("Format: HTML"));
    QVERIFY(!gui->formatMenu->getRow(QStringLiteral("utf-8"))->isChecked());

    // A row of an Enum menu is a radio button for assistive technologies, its choice checked
    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(gui->formatMenu->getRow(QStringLiteral("html")));
    QVERIFY(interface);
    QCOMPARE(interface->role(), QAccessible::RadioButton);
    QVERIFY(interface->state().checked);
    QVERIFY(!QAccessible::queryAccessibleInterface(gui->formatMenu->getRow(QStringLiteral("xml")))->state().checked);
    formatPanel->close();
    AppToolShellFixture::settle();

    // Undo gives back the last choice in the rows
    model->undo();
    AppToolShellFixture::settle();
    QVERIFY(gui->formatMenu->getRow(QStringLiteral("utf-8"))->isChecked());

    // Preset 438:12831: the panel 350 x 280 of a tool with its own rows; a row writes the
    // list of the tool as one step
    pdf::PDFAppToolPanelPopup* presetPanel = toolbar->openPanel(gui->presetPanel->getButton());
    QCOMPARE(presetPanel, gui->presetPanel);
    AppToolShellFixture::settle();
    QCOMPARE(presetPanel->getPanelRect().size(), QSize(350, 280));
    QCOMPARE(presetPanel->getRows().size(), size_t(5));
    const pdf::PDFTileRow* lastRow = presetPanel->getRows().back();
    QCOMPARE(lastRow->y() - presetPanel->getRows()[3]->geometry().bottom() - 1, 2 + 9 + 2);
    QCOMPARE(presetPanel->getButton()->text(), QStringLiteral("Preset: Standard"));
    QTest::mouseClick(presetPanel->getRows().back(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!presetPanel->isVisible());
    QVERIFY(model->getValue(pdftest::ShellTestTool::getStepName(0)).toBool());
    QVERIFY(!model->getValue(pdftest::ShellTestTool::getStepName(3)).toBool());
    QCOMPARE(gui->includeMenu->getButton()->text(), QStringLiteral("Include: 2 extra"));

    // A change of the language: the definitions come again, the value stays
    QCOMPARE(model->setValue(QStringLiteral("format"), QStringLiteral("text")), QString());
    ToolShellFlowTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(gui->formatMenu->getButton()->text(), QStringLiteral("Format: Translated text"));
    QCOMPARE(model->getValue(QStringLiteral("format")).toString(), QStringLiteral("text"));
    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();

    shell->cancel();
    AppToolShellFixture::settle();
}

/// A tool, which remembers what its run has got from the view
class EnvironmentTestTool : public pdftest::ShellTestTool
{
public:
    using pdftest::ShellTestTool::ShellTestTool;

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgress* progress) const override
    {
        // The test reads it after the end of the run
        selectedPages = context.selectedPages;
        return pdftest::ShellTestTool::execute(context, parameters, control, progress);
    }

    mutable std::vector<pdf::PDFInteger> selectedPages;
};

/// A place of the card of the progress inside of the content of the tool (295:1792)
class ProgressPlaceGUI : public pdf::PDFAppDeclarativeToolGUI
{
public:
    pdf::PDFAppToolProgressPlace getProgressPlace() const override
    {
        pdf::PDFAppToolProgressPlace place;
        place.cardLayout = section->layout() ? qobject_cast<QBoxLayout*>(section->layout()) : nullptr;
        place.cardIndex = 1;
        return place;
    }

    QWidget* section = nullptr;

protected:
    void describeRightPanel(pdf::PDFAppToolPanelBuilder& builder) override
    {
        section = new QWidget(builder.getWidget());
        QVBoxLayout* layout = new QVBoxLayout(section);
        layout->addWidget(new QLabel(QStringLiteral("Result"), section));
        layout->addWidget(new QLabel(QStringLiteral("What changes"), section));
        builder.addWidget(section);
    }
};

void AppToolShellTest::shellRunGetsViewAndOwnPlace()
{
    // The run gets the selected pages of the view of the moment of the confirmation; the card
    // and the callout lie where the tool says (Compress: inside of the section Result, 295:1792)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFDocumentToolRegistry::unregisterTool(TOOL_ROTATE);
    std::shared_ptr<EnvironmentTestTool> tool = std::make_shared<EnvironmentTestTool>(TOOL_ROTATE);
    tool->setResultKindModes(true);
    QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(tool), pdf::PDFDocumentToolRegistration::Registered);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_ROTATE, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<ProgressPlaceGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 6);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    const ProgressPlaceGUI* gui = dynamic_cast<const ProgressPlaceGUI*>(shell->getGUI());
    QVERIFY(gui);
    tab->getView()->setSelectedPages({ 1, 4 });
    AppToolShellFixture::settle();

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    const QBoxLayout* layout = qobject_cast<const QBoxLayout*>(gui->section->layout());
    QVERIFY(layout);
    QCOMPARE(layout->indexOf(shell->getProgressCard()), 1);
    QCOMPARE(layout->indexOf(shell->getBusyCallout()), 2);

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&]() { return shell->getResultDialog() != nullptr || shell->getState() == H::State::Editing; }));
    AppToolShellFixture::settle();
    QCOMPARE(tool->selectedPages, (std::vector<pdf::PDFInteger>{ 1, 4 }));
    if (shell->getResultDialog())
    {
        shell->getResultDialog()->reject();
    }
    else
    {
        shell->cancel();
    }
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellPanelHoverSwitch()
{
    // The menu bar of the panels of a tool: the pointer over another panel button through
    // the window system (an open panel grabs the mouse, the moves have no button)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QToolButton* flagsButton = gui->flagsMenu->getButton();
    QToolButton* enumButton = gui->enumMenu->getButton();

    const auto hover = [&fixture](QWidget* widget)
    {
        QTest::mouseMove(fixture.window->windowHandle(), fixture.window->mapFromGlobal(widget->mapToGlobal(widget->rect().center())));
    };

    // Without an open panel the pointer opens nothing
    hover(flagsButton);
    AppToolShellFixture::settle();
    QVERIFY(!toolbar->getOpenPanel());

    QVERIFY(toolbar->openPanel(flagsButton));
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), gui->flagsMenu->getPanel());

    // The switch is done in the move itself: one panel, one marked button, the keyboard in it
    hover(enumButton);
    QCOMPARE(toolbar->getOpenPanel(), gui->enumMenu->getPanel());
    QVERIFY(enumButton->isChecked());
    QVERIFY(!flagsButton->isChecked());
    QVERIFY(!gui->flagsMenu->getPanel()->isVisible());
    QCOMPARE(QApplication::activePopupWidget(), gui->enumMenu->getPanel());
    AppToolShellFixture::settle();
    QCOMPARE(toolbar->getOpenPanel(), gui->enumMenu->getPanel());

    // Its own button and a button of no panel do nothing
    hover(enumButton);
    QCOMPARE(toolbar->getOpenPanel(), gui->enumMenu->getPanel());
    hover(gui->selectAllAction);
    QCOMPARE(toolbar->getOpenPanel(), gui->enumMenu->getPanel());

    // And back
    hover(flagsButton);
    QCOMPARE(toolbar->getOpenPanel(), gui->flagsMenu->getPanel());
    gui->flagsMenu->getPanel()->close();
    AppToolShellFixture::settle();
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellResultWhileAskedAboutLeaving()
{
    // F14: the run ends while the question is open; Keep Running decides the waiting result
    // with the panels of a finished run, Stop and Leave drops it. The title of a document is
    // no text of the further arguments of the question.
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report%1.pdf"), 3));
    QVERIFY(tab);

    for (const bool isKept : { true, false })
    {
        pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
        QVERIFY(shell);
        ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
        AppToolShellFixture::settle();
        QVERIFY(shell->getProgressCard());
        QVERIFY(!shell->getRightContent()->isEnabled());
        const int runStates = gui->runStates;

        bool isTextRight = false;
        QTimer::singleShot(0, fixture.window.data(), [&, isKept]()
        {
            pdf::PDFAppDocumentChoiceDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>();
            if (!dialog)
            {
                return;
            }

            for (const QLabel* label : dialog->findChildren<QLabel*>())
            {
                isTextRight = isTextRight || label->text().contains(QStringLiteral("Report%1"));
            }

            // The result comes, the question stays
            tool->setHold(false);
            pdf::PDFDocumentToolRunner* runner = shell->getRunner();
            AppToolShellFixture::waitFor([runner]() { return runner->getState() == pdf::PDFDocumentToolRunner::State::Waiting; });
            AppToolShellFixture::settle();
            dialog->findChild<QPushButton*>(isKept ? QStringLiteral("keepRunningButton") : QStringLiteral("stopAndLeaveButton"))->click();
        });
        fixture.window->getNavigator()->goBack(tab);
        AppToolShellFixture::settle();
        QVERIFY(isTextRight);

        if (isKept)
        {
            // The dialog of the result, nothing of the run is left behind it
            QCOMPARE(shell->getState(), H::State::Deciding);
            QVERIFY(shell->getResultDialog());
            QVERIFY(!shell->getProgressCard());
            QVERIFY(!shell->getBusyCallout());
            QVERIFY(shell->getRightContent()->isEnabled());
            QCOMPARE(gui->runStates, runStates + 1);
            shell->getResultDialog()->reject();
            AppToolShellFixture::settle();
        }

        QVERIFY(!AppToolShellFixture::getShell(tab));
        QVERIFY(!tab->getView()->isToolModeActive());
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
        QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    }
}

void AppToolShellTest::shellLeaveQuestionOfLostDocument()
{
    // F2 and F14: the document leaves the state Ready while the question of a run is
    // open: the tool ends and the question with it, nobody has to answer for a gone run
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("LostQuestion.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();

    bool isStillOpen = false;
    QTimer::singleShot(0, fixture.window.data(), [&]()
    {
        QPointer<pdf::PDFAppDocumentChoiceDialog> dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>();
        if (!dialog)
        {
            return;
        }
        tab->getDocument()->setPasswordRequired(file, false);
        AppToolShellFixture::settle();
        isStillOpen = dialog && dialog->isVisible();
        if (dialog && isStillOpen)
        {
            // The check has failed: the nested loop of the question ends anyway
            dialog->findChild<QPushButton*>(QStringLiteral("keepRunningButton"))->click();
        }
    });
    fixture.window->getNavigator()->goBack(tab);
    AppToolShellFixture::settle();
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();

    QVERIFY(!isStillOpen);
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getView()->isToolModeActive());
}

void AppToolShellTest::shellRouteOfOpenToolActivatesTab()
{
    // The route of the tool, which is open in a tab behind another one (the chooser of the
    // start page, a link): its tab comes to the front, the tool and its values stay
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best"));
    const int historyCount = tab->getHistory().getCount();

    fixture.window->activateTab(fixture.window->getHomeTab());
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), fixture.window->getHomeTab());

    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_ROTATE));
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("best"));
    QCOMPARE(tab->getHistory().getCount(), historyCount);
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellSameToolOpensAgainAfterClose()
{
    // Then continue with the same tool (Another tool… offers every tool, owner answer B):
    // the closed tool is left, its route opens it anew - a closed page never stays
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);

    shell->close();
    QVERIFY(!tab->getView()->isToolModeActive());
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), TOOL_ROTATE), pdf::PDFAppNavigationMode::Replace);
    AppToolShellFixture::settle();

    pdf::PDFDocumentToolShell* again = AppToolShellFixture::getShell(tab);
    QVERIFY(again);
    QVERIFY(!shell);
    QCOMPARE(again->getState(), H::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    again->cancel();
    AppToolShellFixture::settle();
}

/// A value control in the right panel (Custom… of Compress)
class ValueControlGUI : public pdf::PDFAppDeclarativeToolGUI
{
protected:
    void describeRightPanel(pdf::PDFAppToolPanelBuilder& builder) override
    {
        builder.addValueControl(QStringLiteral("title"));
    }
};

void AppToolShellTest::shellConfirmCommitsTypedValue()
{
    // Ctrl+Enter does not move the focus out of a field: the value being typed is a value
    // of the run, it never runs with the older one
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_ROTATE, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<ValueControlGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    QLineEdit* field = shell->getRightContent()->findChild<QLineEdit*>();
    QVERIFY(field);
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, QStringLiteral("Typed title"));
    QVERIFY(field->hasFocus());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("title")).toString(), QStringLiteral("Statistics"));

    QTest::keyClick(field, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("title")).toString(), QStringLiteral("Typed title"));
    dialog->selectChoice(Choice::Keep);
    dialog->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(tab->getDocument()->getDocument()->getInfo()->title, QStringLiteral("Typed title"));
}

void AppToolShellTest::shellPageSelectionFollowsUndoAndDocument()
{
    // The page selection of a tool (389:20177): Undo over a range with an error enables the
    // Primary button again; an assistant changing the pages below the open tool; page labels
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE, true);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 20);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFPageSelection* selection = gui->pageSelection;
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    pdf::PDFAppToolParameterModel* model = shell->getModel();

    selection->setMode(pdf::PDFPageSelection::Mode::Range);
    selection->setRange(QStringLiteral("2-3"));
    AppToolShellFixture::settle();
    QVERIFY(toolbar->getConfirmButton()->isEnabled());
    QVERIFY(model->canUndo());

    selection->setRange(QStringLiteral("2-x"));
    AppToolShellFixture::settle();
    QVERIFY(!selection->getError().isEmpty());
    QVERIFY(!toolbar->getConfirmButton()->isEnabled());

    // Ctrl+Enter is the Primary button: disabled it does nothing
    shell->confirm();
    QCOMPARE(shell->getState(), H::State::Editing);
    AppToolShellFixture::settle();
    QCOMPARE(tool->getExecuteCount(), 0);

    // The value of the model comes back with the selection, and with it the button
    model->undo();
    AppToolShellFixture::settle();
    QVERIFY(selection->getError().isEmpty());
    QVERIFY(toolbar->getConfirmButton()->isEnabled());

    // 389:20177: the pages chosen in the thumbnails are named
    tab->getView()->setSelectedPages({ 8, 9, 14 });
    selection->setMode(pdf::PDFPageSelection::Mode::Selected);
    AppToolShellFixture::settle();
    QCOMPARE(selection->getStatusText(), QStringLiteral("3 pages selected · 9, 10, 15"));
    selection->setMode(pdf::PDFPageSelection::Mode::All);
    AppToolShellFixture::settle();

    // Another version with other pages: the selection counts them
    QCOMPARE(selection->getPageCount(), 20);
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocumentPointer other = QSharedPointer<pdf::PDFDocument>::create(reader.readFromFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5)));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(other, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();
    QCOMPARE(selection->getPageCount(), 5);
    QCOMPARE(selection->getResultCount(), qint64(5));
    shell->cancel();
    AppToolShellFixture::settle();

    // The labels of the document are shown next to the numbers
    pdf::PDFAppDocumentTab* labeled = H::openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(labeled);
    pdf::PDFDocumentToolShell* labeledShell = fixture.openTool(labeled, TOOL_ROTATE);
    QVERIFY(labeledShell);
    const QString status = AppToolShellFixture::getGUI(labeledShell)->pageSelection->getStatusText();
    QVERIFY2(status.contains(QLatin1Char('(')), qPrintable(status));
    labeledShell->cancel();
    AppToolShellFixture::settle();
}

/// A tool with its own canvas and an overlay
class OverlayGUI : public pdf::PDFAppDeclarativeToolGUI
{
public:
    QWidget* createCanvasPage(QWidget* parent) override
    {
        return new QWidget(parent);
    }

    QWidget* createOverlay(QWidget* canvasArea) override
    {
        QWidget* overlay = new QWidget(canvasArea);
        overlay->setObjectName(QStringLiteral("testOverlay"));
        overlay->setGeometry(canvasArea->rect().center().x() - 50, 40, 100, 24);
        return overlay;
    }
};

/// The texts of a run of the frames, which differ from the default ones, and the hint of a left panel
class RunningTextsGUI : public pdf::PDFAppDeclarativeToolGUI
{
public:
    QString getRunningTitle(int percentage) const override
    {
        Q_UNUSED(percentage);
        return QStringLiteral("Removing hidden data…");
    }

    QString getRunningTimeText(int seconds) const override
    {
        Q_UNUSED(seconds);
        return QStringLiteral("About 20 s left · Stop ends the run, nothing is kept");
    }

    QString getBusyText() const override { return QString(); }

    QString getRunningStatusText(const QString& stepText) const override
    {
        Q_UNUSED(stepText);
        return QStringLiteral("Removing hidden data… step 3 of 8");
    }

    pdf::PDFAppParagraph* hint = nullptr;

protected:
    void describeLeftPanel(pdf::PDFAppToolPanelBuilder& builder) override
    {
        hint = builder.addHint(QStringLiteral("Ticked links are removed – untick a link to keep it."));
    }

    void describeRightPanel(pdf::PDFAppToolPanelBuilder& builder) override
    {
        builder.addSection(QStringLiteral("Result"));
        builder.addTextCard(QStringLiteral("Measured when the tool runs."));
    }
};

void AppToolShellTest::shellRunningTextsOfTool()
{
    // 438:14093 and 356:13079: a tool says its own title, time line and status message and it
    // has no busy callout, the card lies at the top of the right panel
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_ROTATE, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<RunningTextsGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    const RunningTextsGUI* gui = dynamic_cast<const RunningTextsGUI*>(shell->getGUI());
    QVERIFY(gui && gui->hint);

    // The hint 438:14939: Regular 11 in lines of 13, as wide as the panel gives (275 + 4)
    QCOMPARE(gui->hint->font().pixelSize(), 11);
    QCOMPARE(gui->hint->font().weight(), QFont::Normal);
    QCOMPARE(gui->hint->width(), 279);

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    pdf::PDFAppToolProgressCard* card = shell->getProgressCard();
    QVERIFY(card);
    QCOMPARE(card->getTitleLabel()->text(), QStringLiteral("Removing hidden data…"));
    QCOMPARE(card->getTimeLabel()->text(), QStringLiteral("About 20 s left · Stop ends the run, nothing is kept"));
    QVERIFY(!shell->getBusyCallout());
    QCOMPARE(tab->getView()->getToolMessage(), QStringLiteral("Removing hidden data… step 3 of 8"));
    shell->stop();
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    AppToolShellFixture::settle();
    shell->cancel();
    AppToolShellFixture::settle();
}

void AppToolShellTest::shellKeysInPanelAndStates()
{
    // Keys inside of an open panel and in the states of the flow: Esc of a panel closes only
    // the panel, Ctrl+Enter closes it and confirms the tool, F1 does nothing below a popup or
    // a modal dialog, works while the tool runs and the run goes on
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    // Esc in an open panel: the panel goes, the tool stays
    pdf::PDFAppToolPanelPopup* panel = toolbar->openPanel(gui->flagsMenu->getButton());
    QVERIFY(panel);
    AppToolShellFixture::settle();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), H::State::Editing);

    // F1 below an open panel: the popup takes the key, as every panel of the window
    panel = toolbar->openPanel(gui->flagsMenu->getButton());
    AppToolShellFixture::settle();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F1);
    AppToolShellFixture::settle();
    QVERIFY(panel->isVisible());
    QCOMPARE(fixture.window->getCurrentTab(), tab);

    // Ctrl+Enter in an open panel: it closes and the tool runs
    tool->setHold(true);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(shell->getState(), H::State::Running);

    // F1 of a run: the Help tab, the tool goes on in its tab
    fixture.window->getCommandAction(pdf::PDFAppCommands::help())->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tool-pages-rotate"));
    QCOMPARE(shell->getState(), H::State::Running);
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    tool->setHold(false);

    // The modal dialog of the result takes F1 and Ctrl+Enter is no second confirmation
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QCOMPARE(tool->getExecuteCount(), 1);
    QTest::keyClick(dialog, Qt::Key_F1);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QCOMPARE(shell->getResultDialog(), dialog);
    shell->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(tool->getExecuteCount(), 1);
    QCOMPARE(shell->getState(), H::State::Deciding);

    // Esc of the dialog: Discard
    QTest::keyClick(dialog, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
}

void AppToolShellTest::shellOverlayOverTheCanvas()
{
    // createOverlay: a shown child of the canvas area over the canvas of the mode, also
    // over a page of the tool; it goes with the end of the tool
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_ROTATE));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(TOOL_ROTATE, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<OverlayGUI>>()));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    AppToolShellFixture::settle();

    QPointer<QWidget> overlay = shell->getOverlay();
    QVERIFY(overlay);
    QVERIFY(overlay->isVisible());
    QVERIFY(shell->getCanvasPage() && shell->getCanvasPage()->isVisible());
    QWidget* canvasArea = tab->getView()->getCanvasArea();
    QCOMPARE(canvasArea->childAt(overlay->geometry().center()), overlay.data());

    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!overlay);
    QVERIFY(!tab->getView()->isToolModeActive());
}

/// The second host of a tool (Merge, its own tab): no document, no view, plain widgets
class OwnTabToolHost : public pdf::PDFAppToolHost
{
public:
    OwnTabToolHost(pdf::PDFAppContext* context, QWidget* window) : pdf::PDFAppToolHost(window), m_context(context), m_window(window) { }

    pdf::PDFAppContext* getContext() const override { return m_context; }
    pdf::PDFAppDocument* getDocument() const override { return nullptr; }
    pdf::PDFAppDocumentTab* getDocumentTab() const override { return nullptr; }
    pdf::PDFAppDocumentView* getView() const override { return nullptr; }
    pdf::PDFWidget* getPdfWidget() const override { return nullptr; }
    QWidget* getCanvasArea() const override { return m_window; }
    QWidget* getKeyWidget() const override { return m_window; }
    void activate() override { ++activateCount; }
    bool enterMode(const pdf::PDFAppToolModeParts& parts) override { m_parts = parts; parts.toolbar->setParent(m_window); parts.toolbar->show(); return true; }
    void leaveMode() override { ++leaveCount; }
    bool isModeActive() const override { return leaveCount == 0; }
    void setMessage(const QString& text) override { message = text; }
    void showTransientMessage(const QString& text) override { message = text; }
    void showPanel(const QString&) override { }
    void showInfoBar(const pdf::PDFAppInfoBarRequest&) override { }
    void hideInfoBar(const QString&) override { }
    void saveViewState() override { }
    void restoreViewState() override { }
    bool isCommandEnabled(const QString& commandId) const override
    {
        return commandId == pdf::PDFAppCommands::undo() ? m_parts.undoTarget && m_parts.undoTarget->canUndo() : false;
    }
    bool isCommandChecked(const QString&) const override { return false; }
    void requestCommand(const QString& commandId) override
    {
        if (commandId == pdf::PDFAppCommands::undo() && m_parts.undoTarget)
        {
            m_parts.undoTarget->undo();
        }
    }
    QString getCommandToolTip(const QString&) const override { return QString(); }
    pdf::PDFAppToolRunEnvironment createEnvironment() const override { return environment; }
    pdf::PDFInteger getCurrentPage() const override { return -1; }
    std::vector<pdf::PDFInteger> getSelectedPages() const override { return { }; }

    int activateCount = 0;
    int leaveCount = 0;
    QString message;
    pdf::PDFAppToolRunEnvironment environment;

private:
    pdf::PDFAppContext* m_context = nullptr;
    QWidget* m_window = nullptr;
    pdf::PDFAppToolModeParts m_parts;
};

void AppToolShellTest::shellOwnTabHost()
{
    // F17, 2.2: the shell knows only the host. A tool with its own tab has no document
    // and no view: its toolbar, Undo, a run, the result dialog of a new document and a
    // question about the leaving work through the host
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QWidget window;
    window.resize(900, 600);
    window.show();
    OwnTabToolHost* host = new OwnTabToolHost(fixture.context.get(), &window);

    // The inputs of a tool of more documents come from its environment
    const QString inputFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Input.pdf"), 2);
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocumentPointer input = QSharedPointer<pdf::PDFDocument>::create(reader.readFromFile(inputFile));
    host->environment.additionalDocuments.push_back(pdf::PDFDocumentToolInputDocument::create(input, QByteArray(), inputFile, false));

    std::shared_ptr<ShellTestTool> tool = std::make_shared<ShellTestTool>(TOOL_MERGE);
    tool->setResultKindModes(true);
    QPointer<pdf::PDFDocumentToolShell> shell = new pdf::PDFDocumentToolShell(host, tool, new ShellTestGUI(false), &window);
    QVERIFY(shell->enter());
    QCOMPARE(shell->getState(), H::State::Editing);
    QVERIFY(shell->getToolbar()->isVisible());
    QCOMPARE(host->message, pdf::PDFDocumentToolCatalog::findTool(TOOL_MERGE)->getTitle());

    // Undo of the model through the host
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    shell->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("best"));
    host->requestCommand(pdf::PDFAppCommands::undo());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("preset")).toString(), QStringLiteral("balanced"));

    // F14 without a tab: the host brings the tool to the front
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("new-document"));
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    QCOMPARE(shell->getState(), H::State::Running);
    QTimer::singleShot(0, &window, [&]()
    {
        if (pdf::PDFAppDocumentChoiceDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>())
        {
            dialog->findChild<QPushButton*>(QStringLiteral("keepRunningButton"))->click();
        }
    });
    QVERIFY(!shell->confirmLeave());
    QCOMPARE(host->activateCount, 1);

    // The result of a new document: Esc of the dialog is a close request of the reason Discarded
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultDialog()->getResultKind(), PDFAppToolResultDialog::ResultKind::NewDocument);
    QSignalSpy closeSpy(shell.data(), &pdf::PDFDocumentToolShell::closeRequested);
    shell->getResultDialog()->reject();
    AppToolShellFixture::settle();
    QCOMPARE(closeSpy.size(), 1);
    QCOMPARE(closeSpy.first().first().value<pdf::PDFAppToolCloseRequest>().reason, pdf::PDFAppToolCloseRequest::Reason::Discarded);

    shell->close();
    QCOMPARE(host->leaveCount, 1);
    delete shell.data();
    QVERIFY(window.findChildren<pdf::PDFAppToolToolbar*>().isEmpty());
}

void AppToolShellTest::shellViewStateInEveryExit_data()
{
    QTest::addColumn<QString>("way");
    QTest::newRow("cancel") << "cancel";
    QTest::newRow("discard") << "discard";
    QTest::newRow("keep") << "keep";
    QTest::newRow("back") << "back";
    QTest::newRow("lock") << "lock";
    QTest::newRow("stop-and-leave") << "stop-and-leave";
    QTest::newRow("document-route") << "document-route";
}

void AppToolShellTest::shellViewStateInEveryExit()
{
    // Plan 2.4: whatever ends the tool, the view comes back as it was: the layout, the
    // zoom, the rotation, the sidebar with its panel, the page and the place on the page
    using H = ToolShellFlowHelper;
    QFETCH(QString, way);

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE, false, QStringLiteral("thumbnails"));
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 8);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    view->showPanel(QStringLiteral("bookmarks"));
    view->setZoom(1.5);
    view->goToPage(3);
    AppToolShellFixture::settle();
    const pdf::PDFAppViewState state = view->getViewState();
    const pdf::PDFInteger page = view->getCurrentPageIndex();
    QCOMPARE(state.panelId.value_or(QString()), QStringLiteral("bookmarks"));

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QCOMPARE(view->getCurrentPanelId(), QStringLiteral("thumbnails"));

    // Reading works in the tool and the user moves around
    view->setZoom(0.75);
    view->goToPage(6);
    AppToolShellFixture::settle();
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));

    if (way == QLatin1String("cancel"))
    {
        shell->cancel();
    }
    else if (way == QLatin1String("discard") || way == QLatin1String("keep"))
    {
        PDFAppToolResultDialog* dialog = H::confirmAndWait(shell);
        QVERIFY(dialog);
        if (way == QLatin1String("keep"))
        {
            dialog->selectChoice(Choice::Keep);
            dialog->confirm();
        }
        else
        {
            dialog->reject();
        }
    }
    else if (way == QLatin1String("back"))
    {
        fixture.window->getNavigator()->goBack(tab);
    }
    else if (way == QLatin1String("lock"))
    {
        tab->getDocument()->setLocked(true);
    }
    else if (way == QLatin1String("document-route"))
    {
        fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()));
    }
    else
    {
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
        AppToolShellFixture::settle();
        QTimer::singleShot(0, fixture.window.data(), []()
        {
            if (pdf::PDFAppDocumentChoiceDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppDocumentChoiceDialog>())
            {
                dialog->findChild<QPushButton*>(QStringLiteral("stopAndLeaveButton"))->click();
            }
        });
        fixture.window->getNavigator()->goBack(tab);
        tool->setHold(false);
        QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    }
    AppToolShellFixture::settle();

    QVERIFY(!view->isToolModeActive());
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!fixture.currentRoute().contains(QStringLiteral("/tool/")));
    QCOMPARE(view->getCurrentPageIndex(), page);
    QCOMPARE(view->getCurrentPanelId(), QStringLiteral("bookmarks"));
    QCOMPARE(view->getViewState().toText(), state.toText());
}

void AppToolShellTest::shellTabOrder()
{
    // Tab through the mode: the toolbar in its visual order, the panels and the canvas, and
    // on around: every part is reached, nothing traps the focus (O20)
    using H = ToolShellFlowHelper;
    AppToolShellFixture fixture;
    QVERIFY(fixture.registerShellTool(TOOL_EXTRACT, true));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_EXTRACT);
    QVERIFY(shell);
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    ShellTestGUI* gui = AppToolShellFixture::getGUI(shell);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();

    toolbar->getSidebarButton()->setFocus(Qt::TabFocusReason);
    QCOMPARE(QApplication::focusWidget(), toolbar->getSidebarButton());

    std::vector<QWidget*> order;
    for (int i = 0; i < 80; ++i)
    {
        QWidget* focus = QApplication::focusWidget();
        QVERIFY(focus);
        if (!order.empty() && focus == order.front())
        {
            break;
        }
        order.push_back(focus);
        QTest::keyClick(focus, Qt::Key_Tab);
    }

    const auto indexOf = [&order](const QWidget* widget)
    {
        const auto it = std::find(order.cbegin(), order.cend(), widget);
        return it != order.cend() ? int(std::distance(order.cbegin(), it)) : -1;
    };

    // The toolbar has the order of what the eye sees
    const int flags = indexOf(gui->flagsMenu->getButton());
    const int selectAll = indexOf(gui->selectAllAction);
    const int enumMenu = indexOf(gui->enumMenu->getButton());
    const int cancel = indexOf(toolbar->getCancelButton());
    const int confirm = indexOf(toolbar->getConfirmButton());
    QVERIFY2(flags > 0 && selectAll > flags && enumMenu > selectAll && cancel > enumMenu && confirm == cancel + 1,
             qPrintable(QStringLiteral("flags %1, select all %2, order %3, cancel %4, confirm %5").arg(flags).arg(selectAll).arg(enumMenu).arg(cancel).arg(confirm)));

    // Behind the toolbar: the left panel, the canvas, the right panel, in the end the first one again
    QWidget* leftControl = gui->pageSelection->getModeControl();
    const QWidget* canvas = tab->getView()->getPdfWidget();
    int leftIndex = -1;
    int canvasIndex = -1;
    for (int i = 0; i < int(order.size()); ++i)
    {
        leftIndex = leftIndex < 0 && (order[i] == leftControl || leftControl->isAncestorOf(order[i])) ? i : leftIndex;
        canvasIndex = canvasIndex < 0 && (order[i] == canvas || canvas->isAncestorOf(order[i])) ? i : canvasIndex;
    }
    QVERIFY2(leftIndex > confirm, qPrintable(QString::number(leftIndex)));
    QVERIFY2(canvasIndex > leftIndex, qPrintable(QString::number(canvasIndex)));
    QVERIFY2(order.size() < 80, "Tab never came back to the first control");
    shell->cancel();
    AppToolShellFixture::settle();
}

// ---------------------------------------------------------------- the dialog Not enough memory

/// Helpers of the tests of the dialog Not enough memory
class ToolShellMemoryHelper
{
public:
    ToolShellMemoryHelper() = delete;

    using Dialog = pdf::PDFAppToolOutOfMemoryDialog;

    /// The tool without its preview in the mode, which runs out of memory
    static pdf::PDFDocumentToolShell* openThrowing(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab* tab, ShellTestTool* tool)
    {
        tool->setPreview(false, false);
        pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
        if (shell)
        {
            shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("throw-memory"));
        }
        return shell;
    }

    /// A real run, then the dialog
    static Dialog* runToDialog(pdf::PDFDocumentToolShell* shell)
    {
        shell->confirm();
        AppToolShellFixture::waitFor([shell]() { return shell->getOutOfMemoryDialog() != nullptr; });
        AppToolShellFixture::settle();
        return shell->getOutOfMemoryDialog();
    }

    /// The stored setting read by another connection, as another instance reads it
    static QString readInOtherConnection(AppToolShellFixture& fixture)
    {
        QString value;
        pdf::PDFDatabase* database = fixture.context->getDatabase();
        const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
        std::unique_ptr<QThread> thread(QThread::create([&value, database, registry]() { value = pdf::PDFSettingsRepository(database, registry).getString(pdf::PDFSettingKeys::renderingMultithreading()); }));
        thread->start();
        thread->wait();
        return value;
    }

    static bool writeInOtherConnection(AppToolShellFixture& fixture, const QString& value)
    {
        const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
        return fixture.writeFromOtherConnection([registry, value](pdf::PDFDatabase* database) { return pdf::PDFSettingsRepository(database, registry).setValue(pdf::PDFSettingKeys::renderingMultithreading(), value); },
                                                pdf::PDFSettingsRepository::getTopic());
    }

    static QString setting(const AppToolShellFixture& fixture)
    {
        return fixture.context->getSettings().getString(pdf::PDFSettingKeys::renderingMultithreading());
    }

    /// The value shown by the row of Settings › Advanced
    static QVariant shownByRow(AppToolShellFixture& fixture)
    {
        fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::settings(QStringLiteral("advanced"), QString()), pdf::PDFAppNavigationMode::Push);
        AppToolShellFixture::settle();
        const pdf::PDFAppSettingsPage* page = fixture.page<pdf::PDFAppSettingsPage>();
        const pdf::PDFAppSettingRow* row = page ? page->findRow(QStringLiteral("multithreading-strategy")) : nullptr;
        return row ? row->getControl()->getShownValue() : QVariant();
    }

    /// The card in the coordinates of the dialog (the window without its shadow margins)
    static QRect cardRect(const Dialog* dialog)
    {
        return dialog->rect().marginsRemoved(dialog->getShadowMargins());
    }

    /// The process keeps its strategy over the tests of one process: the next view applies
    /// its setting anyway, the core tests of the same process have the default
    static auto restoreStrategy()
    {
        return qScopeGuard([]() { pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded); });
    }
};

void AppToolShellTest::outOfMemoryDialog()
{
    // A real run: the dialog of the design over the body of the window, no info bar. Close leaves
    // the tool open, nothing is written, nothing has changed.
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    const quint64 version = tab->getDocument()->getVersion();
    const QString settingBefore = M::setting(fixture);
    QVERIFY(settingBefore != QLatin1String("singleThread"));

    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(dialog.data()));
    QCOMPARE(tool->getExecuteCount(), 1);

    // Over the body of the window, as the other dialogs of the shell
    QCOMPARE(dialog->getScrimTarget(), pdf::PDFAppMainWindow::findDialogHost(tab));
    QVERIFY(dialog->getScrim() && dialog->getScrim()->isVisible());
    QCOMPARE(dialog->getCardWidth(), 440);
    QVERIFY(dialog->isBadgeVisible());

    // The texts of the brief
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Not enough memory"));
    const QString body = dialog->accessibleDescription();
    QVERIFY2(body.contains(QStringLiteral("Nothing was changed")), qPrintable(body));
    QVERIFY2(body.contains(QStringLiteral("several images at once")) && body.contains(QStringLiteral("ran out of memory")), qPrintable(body));
    QVERIFY2(body.contains(QStringLiteral("slower")) && body.contains(QStringLiteral("one image at a time")), qPrintable(body));
    QVERIFY2(body.contains(QStringLiteral("slows down the drawing of pages")) && body.contains(QStringLiteral("Settings › Advanced")), qPrintable(body));

    // Close (Secondary), then Use One Thread (Primary) at the trailing edge
    const QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), 2);
    QCOMPARE(buttons[0], dialog->getCloseButton());
    QCOMPARE(buttons[1], dialog->getOneThreadButton());
    QCOMPARE(buttons[0]->text(), QStringLiteral("Close"));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Use One Thread"));
    QVERIFY(!buttons[0]->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());
    QVERIFY(buttons[1]->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());
    QVERIFY(buttons[0]->geometry().right() < buttons[1]->geometry().left());
    QVERIFY(buttons[0]->isVisible() && buttons[1]->isVisible());
    QVERIFY(!dialog->getErrorLabel()->isVisible());

    // No second way of telling it, nothing has changed
    QVERIFY(!tab->getView()->findInfoBar(QStringLiteral("tool-result")));
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(tab->getDocument()->getVersion(), version);
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 0);
    QVERIFY(!tab->getDocument()->isBlockedByOperation());
    QVERIFY(!shell->getResultDialog());
    QVERIFY(!shell->getProgressCard());

    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("tool-out-of-memory"));

    buttons[0]->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(M::setting(fixture), settingBefore);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(tool->getExecuteCount(), 1);
    QVERIFY(!tab->getView()->getStatusBar()->getMessage().contains(QStringLiteral("One thread")));
    QVERIFY(pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));
}

void AppToolShellTest::outOfMemoryUseOneThread()
{
    // Use One Thread writes the setting by the path of the row: the process follows, another
    // connection reads it, the row shows it. The tool is not run again, the tab says so.
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QVERIFY(pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));

    QSignalSpy changedSpy(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    dialog->getOneThreadButton()->click();
    AppToolShellFixture::settle();

    // The dialog is closed, the setting written and announced
    QVERIFY(!dialog);
    QCOMPARE(M::setting(fixture), QStringLiteral("singleThread"));
    QCOMPARE(M::readInOtherConnection(fixture), QStringLiteral("singleThread"));
    bool isAnnounced = false;
    for (const QList<QVariant>& arguments : std::as_const(changedSpy))
    {
        isAnnounced = isAnnounced || (arguments.value(0).toString() == pdf::PDFSettingsRepository::getTopic() && arguments.value(1).toString() == pdf::PDFSettingKeys::renderingMultithreading());
    }
    QVERIFY(isAnnounced);

    // The open document applies it to the process
    QVERIFY(AppToolShellFixture::waitFor([]() { return !pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page); }));

    // The short message in the tab, the tool stays open and is not run again
    const QString message = tab->getView()->getStatusBar()->getMessage();
    QVERIFY2(message.contains(QStringLiteral("One thread")) && message.contains(QStringLiteral("Rotate")), qPrintable(message));
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QTest::qWait((PDFDocumentToolRunner::PREVIEW_DELAY + 100) * AppToolShellFixture::getTimeoutFactor());
    AppToolShellFixture::settle();
    QCOMPARE(tool->getExecuteCount(), 1);
    QCOMPARE(tool->getPreviewCount(), 0);

    // A real run always shows the dialog, now without the button
    QPointer<M::Dialog> second = M::runToDialog(shell);
    QVERIFY(second);
    QCOMPARE(tool->getExecuteCount(), 2);
    QVERIFY(second->getOneThreadButton()->isHidden());
    second->reject();
    AppToolShellFixture::settle();

    // The row of Settings shows the choice
    QCOMPARE(M::shownByRow(fixture).toString(), QStringLiteral("singleThread"));
}

void AppToolShellTest::outOfMemoryWriteRefused()
{
    // A refused write is said in the dialog, which stays open; it can be tried again
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    const QString settingBefore = M::setting(fixture);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);

    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(!connection->execute("CREATE TEMP TRIGGER test_refuse_insert BEFORE INSERT ON main.setting BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").isError());
    QVERIFY(!connection->execute("CREATE TEMP TRIGGER test_refuse_update BEFORE UPDATE ON main.setting BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").isError());

    const QString messageBefore = tab->getView()->getStatusBar()->getMessage();
    dialog->getOneThreadButton()->click();
    AppToolShellFixture::settle();

    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->getErrorLabel()->isVisible());
    QVERIFY2(dialog->getErrorLabel()->text().contains(QStringLiteral("The setting was not changed")), qPrintable(dialog->getErrorLabel()->text()));
    QVERIFY2(dialog->getErrorLabel()->text().contains(QStringLiteral("refused by the test")), qPrintable(dialog->getErrorLabel()->text()));
    QCOMPARE(M::setting(fixture), settingBefore);
    QVERIFY(pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));
    QVERIFY(dialog->getOneThreadButton()->isVisible());
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), messageBefore);
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);

    // The database takes it again
    QVERIFY(!connection->execute("DROP TRIGGER test_refuse_insert").isError());
    QVERIFY(!connection->execute("DROP TRIGGER test_refuse_update").isError());
    dialog->getOneThreadButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(M::setting(fixture), QStringLiteral("singleThread"));
}

void AppToolShellTest::outOfMemorySingleThread()
{
    // The strategy is single thread already: nothing is offered, Enter and Esc close
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::renderingMultithreading(), QStringLiteral("singleThread")).isError());
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([]() { return !pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page); }));
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);

    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Not enough memory"));
    QCOMPARE(dialog->accessibleDescription(), QStringLiteral("There is not enough memory for this tool. Nothing was changed."));
    QVERIFY(dialog->getOneThreadButton()->isHidden());
    QVERIFY(dialog->getCloseButton()->isVisible());
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getCloseButton()));
    QVERIFY(dialog->getCloseButton()->isDefault());
    QVERIFY(!dialog->getOneThreadButton()->isDefault());
    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("tool-out-of-memory-single-thread"));

    QTest::keyClick(dialog.data(), Qt::Key_Return);
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);

    QPointer<M::Dialog> second = M::runToDialog(shell);
    QVERIFY(second);
    QTest::keyClick(second.data(), Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!second);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);
    QCOMPARE(tool->getExecuteCount(), 2);
    QVERIFY(!tab->getView()->getStatusBar()->getMessage().contains(QStringLiteral("One thread")));
}

void AppToolShellTest::outOfMemoryFollowsOtherInstance()
{
    // Another instance changes the setting while the dialog is open: the dialog stays true
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    const QString offeredText = dialog->accessibleDescription();
    QVERIFY(dialog->getOneThreadButton()->isVisible());

    QVERIFY(M::writeInOtherConnection(fixture, QStringLiteral("singleThread")));
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->getOneThreadButton()->isHidden());
    QCOMPARE(dialog->accessibleDescription(), QStringLiteral("There is not enough memory for this tool. Nothing was changed."));

    QVERIFY(M::writeInOtherConnection(fixture, QStringLiteral("loadBalanced")));
    QVERIFY(dialog->getOneThreadButton()->isVisible());
    QVERIFY(dialog->getOneThreadButton()->isDefault());
    QVERIFY(!dialog->getCloseButton()->isDefault());
    QCOMPARE(dialog->accessibleDescription(), offeredText);
    dialog->getOneThreadButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(M::readInOtherConnection(fixture), QStringLiteral("singleThread"));
}

void AppToolShellTest::outOfMemoryOtherFailures_data()
{
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("text");
    QTest::newRow("fail") << QStringLiteral("fail") << QStringLiteral("Failed on purpose");
    QTest::newRow("throw") << QStringLiteral("throw") << QStringLiteral("Thrown on purpose");
    QTest::newRow("throw-std") << QStringLiteral("throw-std") << QStringLiteral("unexpected error");
}

void AppToolShellTest::outOfMemoryOtherFailures()
{
    // Another failure of the tool keeps its info bar, no dialog: a text of a failure mentioning
    // memory is no lack of memory either (a typed field decides)
    using H = ToolShellFlowHelper;
    QFETCH(QString, mode);
    QFETCH(QString, text);

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    tool->setPreview(false, false);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    shell->getModel()->setValue(QStringLiteral("mode"), mode);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&]() { return tab->getView()->findInfoBar(QStringLiteral("tool-result")) != nullptr; }));
    AppToolShellFixture::settle();

    const pdf::PDFAppInfoBarRequest* bar = tab->getView()->findInfoBar(QStringLiteral("tool-result"));
    QCOMPARE(int(bar->tone), int(pdf::PDFAppInfoBarTone::Error));
    QVERIFY2(bar->text.contains(text), qPrintable(bar->text));
    QVERIFY(!shell->getOutOfMemoryDialog());
    QVERIFY(!fixture.window->findChild<ToolShellMemoryHelper::Dialog*>());
    QVERIFY(!QApplication::activeModalWidget());
}

void AppToolShellTest::outOfMemoryMeasuringOnce()
{
    // The automatic preview runs out of memory: at most one dialog for an opened tool, not after
    // every change of a parameter. A real run always shows it, the next opening has its own.
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    tool->setPreview(true, false);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getRunner()->isPreviewPending(); }));
    const int previewsBefore = tool->getPreviewCount();

    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("throw-memory"));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getOutOfMemoryDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(tool->getExecuteCount(), 0);
    QCOMPARE(tool->getPreviewCount(), previewsBefore + 1);
    QPointer<M::Dialog> dialog = shell->getOutOfMemoryDialog();
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->accessibleDescription().contains(QStringLiteral("Nothing was changed")));
    dialog->getCloseButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);

    // Each change measures again, nobody is asked again
    for (int repeat = 2; repeat <= 4; ++repeat)
    {
        const int previews = tool->getPreviewCount();
        shell->getModel()->setValue(QStringLiteral("repeat"), repeat);
        QVERIFY(AppToolShellFixture::waitFor([&tool, previews]() { return tool->getPreviewCount() > previews; }));
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getRunner()->isPreviewPending(); }));
        AppToolShellFixture::settle();
        QVERIFY2(!shell->getOutOfMemoryDialog(), qPrintable(QString::number(repeat)));
        QVERIFY(!QApplication::activeModalWidget());
    }
    QVERIFY(!tab->getView()->findInfoBar(QStringLiteral("tool-result")));

    // A real run always shows it
    for (int run = 1; run <= 2; ++run)
    {
        QPointer<M::Dialog> ofRun = M::runToDialog(shell);
        QVERIFY(ofRun);
        QCOMPARE(tool->getExecuteCount(), run);
        ofRun->reject();
        AppToolShellFixture::settle();
        QVERIFY(!ofRun);
    }

    // The tool opened again: once more
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    pdf::PDFDocumentToolShell* reopened = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(reopened);
    QVERIFY(AppToolShellFixture::waitFor([reopened]() { return !reopened->getRunner()->isPreviewPending(); }));
    reopened->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("throw-memory"));
    QVERIFY(AppToolShellFixture::waitFor([reopened]() { return reopened->getOutOfMemoryDialog() != nullptr; }));
    reopened->getOutOfMemoryDialog()->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryWaitsForModal_data()
{
    QTest::addColumn<bool>("isPreview");
    QTest::addColumn<QString>("end");
    for (const bool isPreview : { false, true })
    {
        for (const char* end : { "closed", "deleted", "another-dialog", "tab-closed" })
        {
            QTest::addRow("%s-%s", isPreview ? "measuring" : "run", end) << isPreview << QString::fromLatin1(end);
        }
    }
    QTest::newRow("measuring-run-started") << true << QStringLiteral("run-started");
}

void AppToolShellTest::outOfMemoryWaitsForModal()
{
    // No new dialog over a modal dialog of the application, the tab below it stays. The dialog comes
    // after it, with the document of the failure shown.
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    QFETCH(bool, isPreview);
    QFETCH(QString, end);
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    tool->setPreview(isPreview, false);
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell->getRunner()->isPreviewPending(); }));

    tool->setHold(true);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("throw-memory"));
    if (!isPreview)
    {
        shell->confirm();
    }
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
    fixture.window->activateTab(homeTab);
    AppToolShellFixture::settle();

    QPointer<QDialog> modal = new QDialog(fixture.window);
    modal->setModal(true);
    modal->show();
    AppToolShellFixture::settle();
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(modal.data()));

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool, &shell]() { return tool->getHeldCount() == 0 && shell->getRunner()->getState() == RunnerState::Idle && !shell->getRunner()->isPreviewPending(); }));
    AppToolShellFixture::settle();

    QVERIFY2(!shell->getOutOfMemoryDialog(), "No dialog over the other dialog");
    QCOMPARE(fixture.window->getCurrentTab(), homeTab);
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(modal.data()));
    QCOMPARE(shell->getState(), H::State::Editing);

    if (end == QLatin1String("tab-closed"))
    {
        delete tab.data();
        AppToolShellFixture::settle();
        QVERIFY(!shell);
        delete modal.data();
        AppToolShellFixture::settle();
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(fixture.window->getCurrentTab(), homeTab);
        return;
    }

    if (end == QLatin1String("run-started"))
    {
        // A measuring out of date: the run started behind the other window says its own
        QVERIFY(tool->getExecuteCount() == 0);
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("count"));
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getRunner()->getState() == RunnerState::Waiting; }));
        AppToolShellFixture::settle();
        QVERIFY(!shell->getResultDialog());
        modal->reject();
        delete modal.data();
        QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getResultDialog() != nullptr; }));
        AppToolShellFixture::settle();
        QVERIFY(!shell->getOutOfMemoryDialog());
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();
        return;
    }

    if (end == QLatin1String("another-dialog"))
    {
        // The next dialog is waited for too
        QPointer<QDialog> second = new QDialog(fixture.window);
        second->setModal(true);
        modal->reject();
        second->show();
        AppToolShellFixture::settle();
        QVERIFY(!shell->getOutOfMemoryDialog());
        QCOMPARE(fixture.window->getCurrentTab(), homeTab);
        second->reject();
        delete second.data();
    }
    else if (end == QLatin1String("deleted"))
    {
        delete modal.data();
    }
    else
    {
        modal->reject();
    }

    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return shell->getOutOfMemoryDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(tab.data()));
    QVERIFY(tab->getView()->isVisible());
    QVERIFY(shell->getOutOfMemoryDialog()->isVisible());
    QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(shell->getOutOfMemoryDialog()));
    shell->getOutOfMemoryDialog()->reject();
    AppToolShellFixture::settle();
    delete modal.data();
}

void AppToolShellTest::outOfMemoryWaitsForPanel()
{
    // An open panel of the tool ends first: a popup would keep the keys of the dialog
    using H = ToolShellFlowHelper;
    const auto restore = ToolShellMemoryHelper::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    tool->setPreview(true, false);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, TOOL_ROTATE);
    QVERIFY(shell);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getRunner()->isPreviewPending(); }));

    tool->setHold(true);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("throw-memory"));
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(AppToolShellFixture::getGUI(shell)->flagsMenu->getButton());
    QVERIFY(panel);
    AppToolShellFixture::settle();
    QVERIFY(QApplication::activePopupWidget());

    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&tool, shell]() { return tool->getHeldCount() == 0 && !shell->getRunner()->isPreviewPending(); }));
    AppToolShellFixture::settle();
    QVERIFY(!shell->getOutOfMemoryDialog());
    QVERIFY(panel->isVisible());

    panel->close();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getOutOfMemoryDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QVERIFY(shell->getOutOfMemoryDialog()->isVisible());
    shell->getOutOfMemoryDialog()->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryDeletedFromActivation()
{
    // The dialog brings its tab: a slot of the switch of the tabs may delete the tab with the shell (run it under ASan)
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
    fixture.window->activateTab(homeTab);
    AppToolShellFixture::settle();

    bool isDeleted = false;
    QObject receiver;
    QObject::connect(fixture.window->getTabBar(), &QTabBar::currentChanged, &receiver, [&]()
    {
        if (!isDeleted)
        {
            isDeleted = true;
            delete tab.data();
        }
    });
    tool->setHold(false);
    QVERIFY(AppToolShellFixture::waitFor([&isDeleted]() { return isDeleted; }));
    AppToolShellFixture::settle();
    QVERIFY(!tab);
    QVERIFY(!shell);
    QCOMPARE(fixture.window->getCurrentTab(), homeTab);
    QVERIFY(!fixture.window->findChild<M::Dialog*>());
    QVERIFY(!QApplication::activeModalWidget());
}

void AppToolShellTest::outOfMemoryThemesAndScales_data()
{
    QTest::addColumn<bool>("isDark");
    QTest::addColumn<qreal>("scale");
    for (const bool isDark : { false, true })
    {
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::addRow("%s-%g", isDark ? "dark" : "light", scale) << isDark << scale;
        }
    }
}

void AppToolShellTest::outOfMemoryThemesAndScales()
{
    // The tokens of the card and of the Primary button, no leak color; the theme changes while it is open
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    QFETCH(bool, isDark);
    QFETCH(qreal, scale);
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Light : pdf::PDFThemeColors::Scheme::Dark);
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);

    pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    AppToolShellFixture::settle();
    QVERIFY(dialog && dialog->isVisible());

    const QImage image = AppToolShellFixture::render(dialog, scale);
    QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
    const QRect card = M::cardRect(dialog);
    QCOMPARE(H::pixel(image, QPoint(card.left() + 8, card.center().y()), scale), H::token(pdf::PDFThemeColors::BgSurface));

    const QRect primary = H::rectIn(dialog->getOneThreadButton(), dialog);
    QCOMPARE(H::pixel(image, QPoint(primary.left() + 6, primary.center().y()), scale), H::token(pdf::PDFThemeColors::BgAccent));
    const QRect secondary = H::rectIn(dialog->getCloseButton(), dialog);
    QVERIFY(H::pixel(image, QPoint(secondary.left() + 6, secondary.center().y()), scale) != H::token(pdf::PDFThemeColors::BgAccent));
    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("tool-out-of-memory-%1").arg(isDark ? QStringLiteral("dark") : QStringLiteral("light")));

    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryRightToLeft()
{
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();
    QApplication::setLayoutDirection(Qt::RightToLeft);

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);

    // The row mirrored: the Primary button at the trailing edge, which is the left one
    const QRect primary = H::rectIn(dialog->getOneThreadButton(), dialog);
    const QRect secondary = H::rectIn(dialog->getCloseButton(), dialog);
    QVERIFY(primary.right() < secondary.left());
    QVERIFY(primary.left() < M::cardRect(dialog).center().x());
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);
    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("tool-out-of-memory-rtl"));
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryMinimalWindow()
{
    // 720 x 480: the whole card with the wrapped text inside of the body of the window
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture(QSize(720, 480));
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QCOMPARE(fixture.window->width(), 720);

    const QWidget* host = pdf::PDFAppMainWindow::findDialogHost(tab);
    QVERIFY(host);
    const QRect hostRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    const QRect card = M::cardRect(dialog).translated(dialog->mapToGlobal(QPoint(0, 0)));
    QVERIFY2(hostRect.contains(card), qPrintable(QStringLiteral("%1 %2 %3 x %4").arg(card.x()).arg(card.y()).arg(card.width()).arg(card.height())));
    QCOMPARE(qAbs(card.center().x() - hostRect.center().x()) <= 1, true);
    for (const QPushButton* button : dialog->getActionButtons())
    {
        QVERIFY(button->isVisible());
        QVERIFY(card.contains(QRect(button->mapToGlobal(QPoint(0, 0)), button->size())));
    }
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);
    AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("tool-out-of-memory-720"));
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryKeyboardAndAccessibility()
{
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    const QString settingBefore = M::setting(fixture);

    // Esc closes without a choice, the tool stays
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QTest::keyClick(dialog.data(), Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(M::setting(fixture), settingBefore);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);

    // The first focus on the default button, Tab reaches both; names for assistive technologies
    dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getOneThreadButton()));
    QVERIFY(dialog->getOneThreadButton()->isDefault());
    QVERIFY(dialog->getCloseButton()->focusPolicy() & Qt::TabFocus);
    QAccessibleInterface* closeInterface = QAccessible::queryAccessibleInterface(dialog->getCloseButton());
    QAccessibleInterface* oneThreadInterface = QAccessible::queryAccessibleInterface(dialog->getOneThreadButton());
    QVERIFY(closeInterface && oneThreadInterface);
    QCOMPARE(closeInterface->text(QAccessible::Name), QStringLiteral("Close"));
    QCOMPARE(oneThreadInterface->text(QAccessible::Name), QStringLiteral("Use One Thread"));
    QVERIFY(dialog->accessibleDescription().contains(QStringLiteral("one image at a time")));

    // Enter is Use One Thread
    QTest::keyClick(dialog.data(), Qt::Key_Return);
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(M::setting(fixture), QStringLiteral("singleThread"));
}

void AppToolShellTest::outOfMemoryLanguageChange()
{
    // The open dialog follows the language, with its error, and comes back
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    const QString body = dialog->accessibleDescription();

    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(!connection->execute("CREATE TEMP TRIGGER test_refuse_insert BEFORE INSERT ON main.setting BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").isError());
    dialog->getOneThreadButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(dialog->getErrorLabel()->isVisible());

    ToolShellFlowTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolShellFixture::settle();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->accessibleName(), QStringLiteral("XX Not enough memory"));
    QCOMPARE(dialog->accessibleDescription(), QStringLiteral("XX ") + body);
    QCOMPARE(dialog->getCloseButton()->text(), QStringLiteral("XX Close"));
    QCOMPARE(dialog->getOneThreadButton()->text(), QStringLiteral("XX Use One Thread"));
    QVERIFY(dialog->getErrorLabel()->text().startsWith(QStringLiteral("XX ")));
    QCOMPARE(AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)), 0);

    QCoreApplication::removeTranslator(&translator);
    AppToolShellFixture::settle();
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Not enough memory"));
    QCOMPARE(dialog->getCloseButton()->text(), QStringLiteral("Close"));
    QVERIFY(!connection->execute("DROP TRIGGER test_refuse_insert").isError());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolShellTest::outOfMemoryDocumentLeavesReady()
{
    // F2: the document leaves the state Ready: the shell closes and the dialog goes with it
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("LostMemory.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QPointer<QWidget> scrim = dialog->getScrim();
    QVERIFY(scrim);

    tab->getDocument()->setPasswordRequired(file, false);
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(!scrim || !scrim->isVisible());
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!QApplication::activeModalWidget());
}

void AppToolShellTest::outOfMemoryDeletion_data()
{
    QTest::addColumn<QString>("victim");
    QTest::addColumn<QString>("moment");
    for (const char* victim : { "tab", "window", "page", "shell" })
    {
        for (const char* moment : { "open", "notified", "accepted", "rejected" })
        {
            QTest::addRow("%s-%s", victim, moment) << QString::fromLatin1(victim) << QString::fromLatin1(moment);
        }
    }
}

void AppToolShellTest::outOfMemoryDeletion()
{
    // The tab, the window, the page and the shell deleted with the dialog open and from the signals
    // of its buttons and of the notification of the setting (run it under ASan)
    using H = ToolShellFlowHelper;
    using M = ToolShellMemoryHelper;
    QFETCH(QString, victim);
    QFETCH(QString, moment);
    const auto restore = M::restoreStrategy();

    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool = fixture.registerShellTool(TOOL_ROTATE);
    QVERIFY(tool);
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = M::openThrowing(fixture, tab, tool.get());
    QVERIFY(shell);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();
    QObject receiver;
    bool isDeleted = false;

    const auto destroyVictim = [&]()
    {
        if (isDeleted)
        {
            return;
        }
        isDeleted = true;
        if (victim == QLatin1String("tab"))
        {
            delete tab.data();
        }
        else if (victim == QLatin1String("window"))
        {
            delete fixture.window.data();
        }
        else if (victim == QLatin1String("page"))
        {
            delete tab->getPage();
        }
        else
        {
            delete shell.data();
        }
    };

    QPointer<M::Dialog> dialog = M::runToDialog(shell);
    QVERIFY(dialog);
    QPointer<QWidget> scrim = dialog->getScrim();
    if (moment == QLatin1String("open"))
    {
        destroyVictim();
    }
    else if (moment == QLatin1String("notified"))
    {
        QObject::connect(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed, &receiver, destroyVictim);
        dialog->getOneThreadButton()->click();
    }
    else if (moment == QLatin1String("accepted"))
    {
        QObject::connect(dialog.data(), &QDialog::accepted, &receiver, destroyVictim);
        dialog->getOneThreadButton()->click();
    }
    else
    {
        QObject::connect(dialog.data(), &QDialog::rejected, &receiver, destroyVictim);
        dialog->getCloseButton()->click();
    }
    AppToolShellFixture::settle();

    QVERIFY(isDeleted);
    QVERIFY(!dialog || !dialog->isVisible());
    QVERIFY(!scrim || !scrim->isVisible());
    QVERIFY(!QApplication::activeModalWidget());
    if (tab && view && !AppToolShellFixture::getShell(tab))
    {
        QVERIFY(!view->isToolModeActive());
    }
    if (tab)
    {
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
    }
    if (fixture.window)
    {
        QVERIFY(fixture.window->isVisible());
    }
}

QTEST_MAIN(AppToolShellTest)

#include "tst_apptoolshelltest.moc"
