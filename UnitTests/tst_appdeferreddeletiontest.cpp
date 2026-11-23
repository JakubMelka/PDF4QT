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

// Deferred deletion in the application (step A skeleton PA0): the shared tokens of the App (canvas input / draw
// interface, status-bar message, cursor on a foreign widget), the A0 helpers ended by retire (the dialog answer, the
// step of a leave flow, a file dialog request) and the fixture's retire. Expected values from the contract of the
// tokens (pdfapplifetimetokens.h) and of retire (newapp_robustness.md 12-16), never from a run.

#include "appdocumentfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappleaveflow.h"
#include "pdfapplifetimetokens.h"
#include "pdfapptoolpagestage.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

/// An input interface of a test overlay (never called by the tests)
class TestInputInterface : public pdf::IDrawWidgetInputInterface
{
public:
    virtual void shortcutOverrideEvent(QWidget*, QKeyEvent*) override { }
    virtual void keyPressEvent(QWidget*, QKeyEvent*) override { }
    virtual void keyReleaseEvent(QWidget*, QKeyEvent*) override { }
    virtual void mousePressEvent(QWidget*, QMouseEvent*) override { }
    virtual void mouseDoubleClickEvent(QWidget*, QMouseEvent*) override { }
    virtual void mouseReleaseEvent(QWidget*, QMouseEvent*) override { }
    virtual void mouseMoveEvent(QWidget*, QMouseEvent*) override { }
    virtual void wheelEvent(QWidget*, QWheelEvent*) override { }
    virtual QString getTooltip() const override { return QString(); }
    virtual const std::optional<QCursor>& getCursor() const override { return m_cursor; }
    virtual int getInputPriority() const override { return UserPriority; }

private:
    std::optional<QCursor> m_cursor;
};

/// A draw interface of a test overlay: counts the paintings of the canvas, which reach it
class TestDrawInterface : public pdf::IDocumentDrawInterface
{
public:
    virtual void drawPostRendering(QPainter*, QRect) const override { ++drawCount; }

    mutable int drawCount = 0;
};

/// Records the answers of the A0 helpers
class DeferredReceiver : public QObject
{
public:
    int codeCount = 0;
    int code = -1;
    int stepCount = 0;
    pdf::PDFAppLeaveResult step = pdf::PDFAppLeaveResult::Pending;
    int fileCount = 0;

    void onCode(int value)
    {
        ++codeCount;
        code = value;
    }

    void onStep(pdf::PDFAppLeaveResult result)
    {
        ++stepCount;
        step = result;
    }

    void onFile(const QString&)
    {
        ++fileCount;
    }
};

/// Helpers of the test
class AppDeferredDeletionTestHelper
{
public:
    AppDeferredDeletionTestHelper() = delete;

    /// Only the queued calls: the retired objects are not deleted yet
    static void sendQueuedCalls()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }

    static pdf::PDFAppDocumentView* openView(pdftest::AppDocumentFixture& fixture)
    {
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("tokens.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        return tab ? tab->getView() : nullptr;
    }

    /// A change of the user (the title): the document has unsaved changes
    static bool change(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    /// Number of the visible dialogs of the application (QApplication::topLevelWidgets lists every window, also a
    /// dialog with a parent)
    static int countVisibleDialogs()
    {
        int count = 0;
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (qobject_cast<QDialog*>(topLevel) && topLevel->isVisible())
            {
                ++count;
            }
        }
        return count;
    }
};

class AppDeferredDeletionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void canvasInputTokenEndsOnRetire();
    void canvasDrawTokenEndsOnRetire();
    void statusMessageTokenGivesBackMessage();
    void cursorTokenGivesBackCursor();
    void dialogAnswerEndsOnRetiredDialog();
    void leaveFlowStepEndsOnRetiredReporter();
    void fileRequestToRetiredReceiverCut();
    void fixtureRetireClosesTab();
    void retiredStageRendersNothing();
    void retiringParentOpensNoDialog();
};

void AppDeferredDeletionTest::canvasInputTokenEndsOnRetire()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentView* view = AppDeferredDeletionTestHelper::openView(fixture);
    QVERIFY(view);
    pdf::PDFWidget* widget = view->getPdfWidget();
    QVERIFY(widget);

    TestInputInterface input;
    QPointer<QObject> owner = new QObject();
    pdf::PDFAppCanvasInputToken* token = new pdf::PDFAppCanvasInputToken(owner, widget, &input);
    const std::vector<pdf::IDrawWidgetInputInterface*>& interfaces = widget->getInputInterfaces();
    QCOMPARE(std::count(interfaces.cbegin(), interfaces.cend(), &input), 1);
    QCOMPARE(token->getWidget(), widget);

    // retire of the owner ends the registration at once, the owner lives until the event loop
    pdf::PDFDeferredDeletion::retire(owner.data());
    QVERIFY(owner);
    QVERIFY(token->isReleased());
    QCOMPARE(std::count(widget->getInputInterfaces().cbegin(), widget->getInputInterfaces().cend(), &input), 0);
    fixture.settle();
    QVERIFY(owner.isNull());

    // Without retire: the destruction of the token ends it
    {
        pdf::PDFAppCanvasInputToken scoped(nullptr, widget, &input);
        QCOMPARE(std::count(widget->getInputInterfaces().cbegin(), widget->getInputInterfaces().cend(), &input), 1);
    }
    QCOMPARE(std::count(widget->getInputInterfaces().cbegin(), widget->getInputInterfaces().cend(), &input), 0);
}

void AppDeferredDeletionTest::canvasDrawTokenEndsOnRetire()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentView* view = AppDeferredDeletionTestHelper::openView(fixture);
    QVERIFY(view);
    pdf::PDFDrawWidgetProxy* proxy = view->getPdfWidget()->getDrawWidgetProxy();
    QVERIFY(proxy);

    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    QVERIFY(canvas);

    // A painting of the canvas reaches the registered interface
    TestDrawInterface drawInterface;
    QPointer<QObject> owner = new QObject();
    new pdf::PDFAppCanvasDrawToken(owner, proxy, &drawInterface);
    canvas->grab();
    QVERIFY(drawInterface.drawCount > 0);

    // The release unregisters and asks the canvas to repaint (the overlay disappears)
    QSignalSpy repaints(proxy, &pdf::PDFDrawWidgetProxy::repaintNeeded);
    pdf::PDFDeferredDeletion::retire(owner.data());
    QCOMPARE(repaints.count(), 1);
    const int drawCount = drawInterface.drawCount;
    canvas->grab();
    QCOMPARE(drawInterface.drawCount, drawCount);
    fixture.settle();
    QVERIFY(owner.isNull());
}

void AppDeferredDeletionTest::statusMessageTokenGivesBackMessage()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentView* view = AppDeferredDeletionTestHelper::openView(fixture);
    QVERIFY(view);
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
    QVERIFY(statusBar);
    statusBar->showMessage(QStringLiteral("Before"));

    QPointer<QObject> owner = new QObject();
    pdf::PDFAppStatusMessageToken* token = new pdf::PDFAppStatusMessageToken(owner, statusBar, QStringLiteral("Hint of the overlay"));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Hint of the overlay"));
    token->setText(QStringLiteral("Second hint"));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Second hint"));

    pdf::PDFDeferredDeletion::retire(owner.data());
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Before"));
    token->setText(QStringLiteral("After the release"));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Before"));
    fixture.settle();

    // A newer message of somebody else stays
    QObject secondOwner;
    pdf::PDFAppStatusMessageToken* secondToken = new pdf::PDFAppStatusMessageToken(&secondOwner, statusBar, QStringLiteral("Hint"));
    statusBar->showMessage(QStringLiteral("Somebody else"));
    secondToken->releaseToken();
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Somebody else"));
}

void AppDeferredDeletionTest::cursorTokenGivesBackCursor()
{
    // A child widget (QWidget::unsetCursor keeps WA_SetCursor of a window)
    QWidget container;
    QWidget& foreign = *new QWidget(&container);
    QVERIFY(!foreign.testAttribute(Qt::WA_SetCursor));

    // A widget without its own cursor: unset again
    QPointer<QObject> owner = new QObject();
    new pdf::PDFAppCursorToken(owner, &foreign, QCursor(Qt::CrossCursor));
    QCOMPARE(foreign.cursor().shape(), Qt::CrossCursor);
    pdf::PDFDeferredDeletion::retire(owner.data());
    QVERIFY(!foreign.testAttribute(Qt::WA_SetCursor));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(owner.isNull());

    // A widget with its own cursor: it gets it back
    foreign.setCursor(Qt::IBeamCursor);
    {
        QObject scopedOwner;
        pdf::PDFAppCursorToken* token = new pdf::PDFAppCursorToken(&scopedOwner, &foreign, QCursor(Qt::OpenHandCursor));
        QCOMPARE(foreign.cursor().shape(), Qt::OpenHandCursor);
        token->setCursor(QCursor(Qt::ClosedHandCursor));
        QCOMPARE(foreign.cursor().shape(), Qt::ClosedHandCursor);
    }
    QVERIFY(foreign.testAttribute(Qt::WA_SetCursor));
    QCOMPARE(foreign.cursor().shape(), Qt::IBeamCursor);
}

void AppDeferredDeletionTest::dialogAnswerEndsOnRetiredDialog()
{
    // A question retired (its logical owner closed) ends its answer Rejected from the event loop, before the dialog
    // is deleted - as a question deleted without finished (PDFAppDialogAnswer)
    QWidget host;
    DeferredReceiver receiver;
    QPointer<QDialog> question = new QDialog(&host);
    QVERIFY(pdf::PDFAppDialogAnswer::show(question.data(), &receiver, &DeferredReceiver::onCode));
    QVERIFY(question->isVisible());

    pdf::PDFDeferredDeletion::retire(question.data());
    QCOMPARE(receiver.codeCount, 0);
    AppDeferredDeletionTestHelper::sendQueuedCalls();
    QVERIFY(question);
    QCOMPARE(receiver.codeCount, 1);
    QCOMPARE(receiver.code, int(QDialog::Rejected));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(question.isNull());
    QCOMPARE(receiver.codeCount, 1);

    // A retired receiver gets nothing (the connections from the dialog are cut)
    QPointer<DeferredReceiver> retiredReceiver = new DeferredReceiver();
    QDialog* secondQuestion = new QDialog(&host);
    QVERIFY(pdf::PDFAppDialogAnswer::show(secondQuestion, retiredReceiver.data(), &DeferredReceiver::onCode));
    pdf::PDFDeferredDeletion::retire(retiredReceiver.data());
    secondQuestion->accept();
    QCOMPARE(retiredReceiver->codeCount, 0);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(retiredReceiver.isNull());
}

void AppDeferredDeletionTest::leaveFlowStepEndsOnRetiredReporter()
{
    // A reporter retired before it reports (its tab, its page) ends the step Refused from the event loop, exactly
    // once, before the reporter is deleted
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::CloseTab));
    DeferredReceiver receiver;
    QPointer<QObject> reporter = new QObject();
    QVERIFY(flow->waitForStep(reporter.data(), &receiver, &DeferredReceiver::onStep));

    pdf::PDFDeferredDeletion::retire(reporter.data());
    QCOMPARE(receiver.stepCount, 0);
    AppDeferredDeletionTestHelper::sendQueuedCalls();
    QVERIFY(reporter);
    QCOMPARE(receiver.stepCount, 1);
    QCOMPARE(receiver.step, pdf::PDFAppLeaveResult::Refused);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(reporter.isNull());
    QCOMPARE(receiver.stepCount, 1);

    // A retired waiter (outside the subtree of the flow) gets nothing
    QObject laterReporter;
    QPointer<DeferredReceiver> retiredReceiver = new DeferredReceiver();
    QVERIFY(flow->waitForStep(&laterReporter, retiredReceiver.data(), &DeferredReceiver::onStep));
    pdf::PDFDeferredDeletion::retire(retiredReceiver.data());
    QVERIFY(flow->finishStep(&laterReporter, pdf::PDFAppLeaveResult::Done));
    QCOMPARE(retiredReceiver->stepCount, 0);
    flow->finish(false);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(retiredReceiver.isNull());
}

void AppDeferredDeletionTest::fileRequestToRetiredReceiverCut()
{
    // A file dialog request connected to a receiver of another subtree: the retire of the receiver cuts the answer
    QObject requester;
    QWidget parent;
    pdf::PDFAppFileDialogRequest::Settings settings;
    settings.mode = pdf::PDFAppFileDialogRequest::Mode::OpenFile;
    pdf::PDFAppFileDialogRequest* request = pdf::PDFAppFileDialogRequest::create(&requester, &parent, settings);
    QVERIFY(request);
    QPointer<DeferredReceiver> receiver = new DeferredReceiver();
    request->connectTo(receiver.data(), &DeferredReceiver::onFile);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(receiver.data()), 1);

    pdf::PDFDeferredDeletion::retire(receiver.data());
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(receiver.data()), 0);
    Q_EMIT request->fileChosen(QStringLiteral("C:/a.pdf"));
    QCOMPARE(receiver->fileCount, 0);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(receiver.isNull());
}

void AppDeferredDeletionTest::fixtureRetireClosesTab()
{
    // The fixture closes a tab by the real close of the window: it leaves the lists of the window at once and is
    // deleted after settle
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("close.pdf"), 1);
    QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(file);
    QVERIFY(tab);
    QVERIFY(fixture.window->getDocumentTabs().contains(tab.data()));

    QVERIFY(fixture.retire(tab.data()));
    QVERIFY(!fixture.window->getDocumentTabs().contains(tab.data()));
    fixture.settle();
    QVERIFY(tab.isNull());

    // Anything else by PDFDeferredDeletion::retire
    QPointer<QObject> object = new QObject();
    QVERIFY(fixture.retire(object.data()));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(object.data()));
    fixture.settle();
    QVERIFY(object.isNull());
}

void AppDeferredDeletionTest::retiredStageRendersNothing()
{
    // A QObject held by value is outside the subtree of its object (step A integration): retire stops only the timers
    // of the subtree, so a timer member is made a child. The stage of a page tool waits RENDER_DELAY before it
    // renders its page in a worker; a stage retired in that time has nothing pending and never renders
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("stage.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);

    QPointer<pdf::PDFAppToolPageStage> stage = new pdf::PDFAppToolPageStage(nullptr);
    stage->setProxy(tab->getView()->getProxy());
    stage->setPage(tab->getDocument()->getDocument(), tab->getDocument()->getId(), 0);
    stage->resize(400, 400);
    stage->show();
    QVERIFY(stage->isRenderPending());
    QCOMPARE(stage->getFinishedRenderCount(), 0);

    pdf::PDFDeferredDeletion::retire(stage.data());
    QVERIFY(stage);
    QVERIFY(!stage->isRenderPending());

    // Four times the delay: nothing starts for the retired stage while it waits for its deletion (the deferred
    // deletion posted at the loop level 0 is not delivered by processEvents; RB-L2: the stage must still be there)
    QElapsedTimer elapsed;
    elapsed.start();
    while (stage && elapsed.elapsed() < 4 * pdf::PDFAppToolPageStage::RENDER_DELAY)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QVERIFY(stage);
    QVERIFY(!stage->isRenderPending());
    QCOMPARE(stage->getFinishedRenderCount(), 0);
    fixture.settle();
    QVERIFY(stage.isNull());
}

void AppDeferredDeletionTest::retiringParentOpensNoDialog()
{
    // The questions of a close belong to the close - the flow of the tab -, never to retire (newapp_robustness.md
    // 3.2, 7.5): a tab with unsaved changes, whose close question is open, is retired by somebody else. The question
    // belongs to the flow of the tab and closes with it at once; no other dialog opens, neither in retire nor later
    // from the event loop; a close request of the retired tab is refused without a question
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("unsaved.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    QVERIFY(AppDeferredDeletionTestHelper::change(tab->getDocument(), QStringLiteral("Changed")));
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 0);

    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QPointer<pdf::PDFAppCloseUnsavedDialog> question = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>();
    QVERIFY(question);
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 1);

    pdf::PDFDeferredDeletion::retire(tab);
    QVERIFY(guard);
    QVERIFY(question);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(question.data()));
    QVERIFY(!question->isVisible());
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 0);
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Refused);
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 0);

    AppDeferredDeletionTestHelper::sendQueuedCalls();
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 0);
    fixture.settle();
    QVERIFY(!guard);
    QVERIFY(!question);
    QCOMPARE(AppDeferredDeletionTestHelper::countVisibleDialogs(), 0);
}

QTEST_MAIN(AppDeferredDeletionTest)

#include "tst_appdeferreddeletiontest.moc"
