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

// Leaving a tool with the unsaved work of the user (owner 2026-11-20): every way of leaving a tool in the tab of a
// document (Esc, Cancel, the close of the tab, another route, the quit) asks the question of the tool, only while
// the user has made work in this session (none, or work removed or saved = no question); an assistant never closes
// such a tool; an interactive tool is recorded in Recent tools when it is left after an action or 10 s shown.

#include "appasyncdialogs.h"
#include "appmcpfixture.h"
#include "apptoolowntabfixture.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappleaveflow.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfappsessionend.h"
#include "pdfapptooltab.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_manage.h"
#include "pdfapptoolgui_measure.h"
#include "pdfapptoolgui_merge.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolgui_redact.h"
#include "pdfapptoolgui_signbyhand.h"
#include "pdfapptooldrawingpart.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolundotarget.h"
#include "pdfappundostack.h"
#include "pdfdocumenttoolshell.h"
#include "pdftoolrepository.h"

#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_measure.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfmeasurewriter.h"
#include "pdfmessagebox.h"
#include "pdfpagedrawing.h"
#include "pdfthememanager.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QTranslator>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <numeric>

using pdf::PDFDocumentToolShell;
using pdftest::AppDocumentSamples;
using pdftest::AppToolOwnTabFixture;
using pdftest::AppToolShellFixture;

/// The clock of the visible time of the tests (no test waits 10 real seconds)
class LeaveTestClock : public pdf::IPDFAppToolUseClock
{
public:
    virtual qint64 getMilliseconds() const override { return now; }

    qint64 now = 1000;
};

/// A tool with its work: how the work is made and taken away, and the texts of its question
class LeaveToolCase
{
public:
    QString toolId;
    int pageCount = 3;
    QString title;          ///< The title of the question with the work of makeWork
    QString keepText;       ///< The answer, which keeps the tool open
    QString closeText;      ///< The answer, which lets the tool go
    std::function<bool(AppToolShellFixture&, PDFDocumentToolShell*)> makeWork;
    std::function<bool(PDFDocumentToolShell*)> removeWork;
};

class LeaveTestHelper
{
public:
    LeaveTestHelper() = delete;

    static QString png(const QString& folder, const QString& name)
    {
        QImage image(QSize(120, 90), QImage::Format_RGB32);
        image.fill(QColor(200, 220, 240));
        const QString path = folder + QLatin1Char('/') + name;
        return image.save(path, "PNG") ? path : QString();
    }

    static bool undo(PDFDocumentToolShell* shell)
    {
        pdf::PDFAppToolUndoTarget* target = shell ? shell->getUndoTarget() : nullptr;
        if (!target || !target->canUndo())
        {
            return false;
        }
        target->undo();
        AppToolShellFixture::settle();
        return true;
    }

    /// Remove of the one source of Insert / Add Image (its × in the list)
    static bool removeSource(PDFDocumentToolShell* shell)
    {
        pdf::PDFAppInsertToolGUIBase* gui = qobject_cast<pdf::PDFAppInsertToolGUIBase*>(shell->getGUI());
        pdf::PDFAppToolInputList* list = gui ? gui->getSourceList() : nullptr;
        if (!list || list->getCount() != 1)
        {
            return false;
        }
        gui->removeInput(list->getInput(0)->id);
        AppToolShellFixture::settle();
        return list->getCount() == 0;
    }

    static std::map<QString, LeaveToolCase> cases()
    {
        std::map<QString, LeaveToolCase> result;

        LeaveToolCase measure;
        measure.toolId = QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID);
        measure.title = QStringLiteral("Discard the temporary measurement?");
        measure.keepText = QStringLiteral("Keep Measuring");
        measure.closeText = QStringLiteral("Discard Measurement");
        measure.makeWork = [](AppToolShellFixture&, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
            return gui && gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear) &&
                   gui->getMeasurements().size() == 1;
        };
        measure.removeWork = [](PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
            if (gui)
            {
                gui->clearMeasurements();
            }
            return gui && gui->getMeasurements().empty();
        };
        result[QStringLiteral("measure")] = measure;

        LeaveToolCase hand;
        hand.toolId = QStringLiteral("protect.sign-by-hand");
        hand.title = QStringLiteral("Discard the drawing?");
        hand.keepText = QStringLiteral("Keep Drawing");
        hand.closeText = QStringLiteral("Discard Drawing");
        hand.makeWork = [](AppToolShellFixture&, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUISignByHand* gui = qobject_cast<pdf::PDFAppToolGUISignByHand*>(shell->getGUI());
            if (!gui || !gui->getDrawingPart())
            {
                return false;
            }
            pdf::PDFPageDrawing::Element element;
            element.kind = pdf::PDFPageDrawing::Kind::Rectangle;
            element.pageIndex = 0;
            element.rectangle = QRectF(100, 100, 80, 30);
            gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ element }));
            AppToolShellFixture::settle();
            return gui->getDrawingPart()->getElementCount() == 1;
        };
        hand.removeWork = [](PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUISignByHand* gui = qobject_cast<pdf::PDFAppToolGUISignByHand*>(shell->getGUI());
            if (gui && gui->getDrawingPart())
            {
                gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing());
                AppToolShellFixture::settle();
            }
            return gui && gui->getDrawingPart() && gui->getDrawingPart()->isEmpty();
        };
        result[QStringLiteral("signbyhand")] = hand;

        LeaveToolCase redact;
        redact.toolId = QStringLiteral("protect.redact");
        redact.title = QStringLiteral("Keep the new mark in the document?");
        redact.keepText = QStringLiteral("Back to Redacting");
        redact.closeText = QStringLiteral("Keep Marks and Close");
        redact.makeWork = [](AppToolShellFixture&, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUIRedact* gui = qobject_cast<pdf::PDFAppToolGUIRedact*>(shell->getGUI());
            const bool isMarked = gui && gui->markPages({ 1 });
            AppToolShellFixture::settle();
            return isMarked && gui->getMarks().size() == 1;
        };
        redact.removeWork = [](PDFDocumentToolShell* shell)
        {
            // Undo of the tool takes the mark back (one step of the document)
            pdf::PDFAppToolGUIRedact* gui = qobject_cast<pdf::PDFAppToolGUIRedact*>(shell->getGUI());
            return undo(shell) && gui && gui->getMarks().empty();
        };
        result[QStringLiteral("redact")] = redact;

        LeaveToolCase manage;
        manage.toolId = QStringLiteral("pages.manage");
        manage.title = QStringLiteral("Discard the changes to the pages?");
        manage.keepText = QStringLiteral("Keep Arranging");
        manage.closeText = QStringLiteral("Discard Changes");
        manage.makeWork = [](AppToolShellFixture&, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppManageToolGUI* gui = qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI());
            if (!gui || !gui->isDocumentHost())
            {
                return false;
            }
            gui->selectAll();
            gui->rotateRight();
            AppToolShellFixture::settle();
            return true;
        };
        manage.removeWork = [](PDFDocumentToolShell* shell) { return undo(shell); };
        result[QStringLiteral("manage")] = manage;

        LeaveToolCase insert;
        insert.toolId = QStringLiteral("pages.insert");
        insert.title = QStringLiteral("Discard the 2 pages to insert?");
        insert.keepText = QStringLiteral("Keep Inserting");
        insert.closeText = QStringLiteral("Discard Pages");
        insert.makeWork = [](AppToolShellFixture& fixture, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppInsertToolGUI* gui = qobject_cast<pdf::PDFAppInsertToolGUI*>(shell->getGUI());
            if (!gui)
            {
                return false;
            }
            gui->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex.pdf"), 2) });
            AppToolShellFixture::settle();
            return gui->getSourceList() && gui->getSourceList()->getCount() == 1 && AppToolShellFixture::waitFor([gui]() { return gui->getInsertedPageCount() == 2; });
        };
        insert.removeWork = &LeaveTestHelper::removeSource;
        result[QStringLiteral("insert")] = insert;

        LeaveToolCase addImage;
        addImage.toolId = QStringLiteral("pages.add-image");
        addImage.title = QStringLiteral("Discard the added image?");
        addImage.keepText = QStringLiteral("Keep Adding");
        addImage.closeText = QStringLiteral("Discard Images");
        addImage.makeWork = [](AppToolShellFixture& fixture, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppAddImageToolGUI* gui = qobject_cast<pdf::PDFAppAddImageToolGUI*>(shell->getGUI());
            if (!gui)
            {
                return false;
            }
            gui->addFiles({ png(fixture.folder(), QStringLiteral("photo.png")) });
            AppToolShellFixture::settle();
            return gui->getSourceList() && gui->getSourceList()->getCount() == 1;
        };
        addImage.removeWork = &LeaveTestHelper::removeSource;
        result[QStringLiteral("addimage")] = addImage;

        LeaveToolCase rotate;
        rotate.toolId = QStringLiteral("pages.rotate");
        rotate.title = QStringLiteral("Discard the rotation of the page?");
        rotate.keepText = QStringLiteral("Keep Rotating");
        rotate.closeText = QStringLiteral("Discard Rotations");
        rotate.makeWork = [](AppToolShellFixture&, PDFDocumentToolShell* shell)
        {
            pdf::PDFAppToolGUIRotatePages* gui = qobject_cast<pdf::PDFAppToolGUIRotatePages*>(shell->getGUI());
            if (!gui || gui->getTargetPages().size() != 1)
            {
                return false;
            }
            gui->rotateRight();
            AppToolShellFixture::settle();
            return gui->getTurns().size() == 1;
        };
        rotate.removeWork = [](PDFDocumentToolShell* shell) { return undo(shell); };
        result[QStringLiteral("rotate")] = rotate;
        return result;
    }

    /// Opens a document of \p pageCount pages and the tool in its tab
    static PDFDocumentToolShell* open(AppToolShellFixture& fixture, const LeaveToolCase& tool, pdf::PDFAppDocumentTab** tabOut)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Leave.pdf"), tool.pageCount));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        *tabOut = tab;
        PDFDocumentToolShell* shell = fixture.openTool(tab, tool.toolId);
        AppToolShellFixture::settle();
        return shell && shell->getState() == PDFDocumentToolShell::State::Editing ? shell : nullptr;
    }

    /// The open question of the tool (nullptr = none is shown)
    static QDialog* question(PDFDocumentToolShell* shell)
    {
        QDialog* dialog = shell ? shell->getCloseQuestion() : nullptr;
        return dialog && dialog->isVisible() ? dialog : nullptr;
    }

    /// Every shown question of a tool in the application (the dialogs of askClose)
    static int countShownQuestions()
    {
        int count = 0;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            for (pdf::PDFMessageBox* box : widget->findChildren<pdf::PDFMessageBox*>())
            {
                count += box->isVisible() && (box->objectName() == QLatin1String("toolCloseQuestion") || box->objectName() == QLatin1String("signByHandDiscardDialog") ||
                                              box->objectName() == QLatin1String("redactClosePrompt")) ? 1 : 0;
            }
        }
        return count;
    }

    /// A label of the dialog shows the text (the title, the text of the question)
    static bool hasLabel(const QDialog* dialog, const QString& text)
    {
        for (const QLabel* label : dialog ? dialog->findChildren<QLabel*>() : QList<QLabel*>())
        {
            if (label->text() == text)
            {
                return true;
            }
        }
        return false;
    }

    static QPushButton* button(QDialog* dialog, const QString& text)
    {
        for (QPushButton* candidate : dialog ? dialog->findChildren<QPushButton*>() : QList<QPushButton*>())
        {
            if (candidate->text() == text)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    /// Clicks the button \p text of the open question of the tool as the user does (A0: the leave has shown it and
    /// returned; the continuation runs in the click) and settles. \returns false without the question or the button.
    static bool answer(PDFDocumentToolShell* shell, const QString& text)
    {
        QPushButton* answerButton = button(question(shell), text);
        if (!answerButton)
        {
            return false;
        }
        answerButton->click();
        AppToolShellFixture::settle();
        return true;
    }

    /// The annotations of all pages of the document
    static size_t annotationCount(const pdf::PDFAppDocument* appDocument)
    {
        const pdf::PDFDocumentPointer document = appDocument ? appDocument->getDocument() : pdf::PDFDocumentPointer();
        size_t count = 0;
        for (size_t i = 0; document && i < document->getCatalog()->getPageCount(); ++i)
        {
            count += document->getCatalog()->getPage(i)->getAnnotations().size();
        }
        return count;
    }

    /// The measurement annotations of all pages (ISO 32000-2, 12.9)
    static size_t measureAnnotationCount(const pdf::PDFAppDocument* appDocument)
    {
        const pdf::PDFDocumentPointer document = appDocument ? appDocument->getDocument() : pdf::PDFDocumentPointer();
        if (!document)
        {
            return 0;
        }
        std::vector<pdf::PDFInteger> pages(document->getCatalog()->getPageCount());
        std::iota(pages.begin(), pages.end(), pdf::PDFInteger(0));
        return pdf::PDFMeasureAnnotationInfo::readPages(document.data(), pages, 1000, 1000, nullptr, nullptr).size();
    }

    /// Answers the open question of the unsaved changes of the close of a tab by \p choice (Cancel rejects it) and
    /// settles. \returns false without the question.
    static bool answerUnsaved(pdf::PDFAppDocumentChoice choice)
    {
        pdf::PDFAppCloseUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>();
        if (!dialog)
        {
            return false;
        }
        if (choice == pdf::PDFAppDocumentChoice::Cancel)
        {
            dialog->reject();
        }
        else if (QPushButton* choiceButton = dialog->getChoiceButton(choice))
        {
            choiceButton->click();
        }
        AppToolShellFixture::settle();
        return true;
    }

    /// Two temporary measurements of Measure: one on page 1, which the writer stores, and one on page 2, whose length
    /// overflows to infinity - its value cannot be computed and the writer refuses it (PDFMeasureWriter)
    static bool makeIncompleteWork(PDFDocumentToolShell* shell)
    {
        pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell ? shell->getGUI() : nullptr);
        const bool isAdded = gui && gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear) &&
                             gui->addMeasurement(1, pdf::PDFMeasureKind::Distance, { QPointF(-1.5e308, 100), QPointF(1.5e308, 100) }, pdf::PDFAppMeasureDrawKind::Linear);
        AppToolShellFixture::settle();
        return isAdded && gui->getMeasurements().size() == 2;
    }

    static pdf::PDFAppToolGUIMeasure* measure(PDFDocumentToolShell* shell)
    {
        return qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell ? shell->getGUI() : nullptr);
    }

    /// The open question of Measure about the measurements, which could not be saved (nullptr = none is shown)
    static pdf::PDFMessageBox* incompleteQuestion(PDFDocumentToolShell* shell)
    {
        pdf::PDFAppToolGUIMeasure* gui = measure(shell);
        pdf::PDFMessageBox* box = gui ? gui->getIncompleteQuestion() : nullptr;
        return box && box->isVisible() ? box : nullptr;
    }

    /// Clicks the button \p buttonName of the open question of Measure about the measurements, which could not be
    /// saved, and settles. \returns false without the question or the button.
    static bool answerIncomplete(PDFDocumentToolShell* shell, const QString& buttonName)
    {
        pdf::PDFMessageBox* box = incompleteQuestion(shell);
        QPushButton* answerButton = box ? box->findChild<QPushButton*>(buttonName) : nullptr;
        if (!answerButton)
        {
            return false;
        }
        answerButton->click();
        AppToolShellFixture::settle();
        return true;
    }

    /// Cancels the open question of the unsaved documents of the quit and settles. \returns false without it.
    static bool cancelQuitUnsaved()
    {
        pdf::PDFAppQuitUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
        if (!dialog)
        {
            return false;
        }
        dialog->reject();
        AppToolShellFixture::settle();
        return true;
    }

    /// Clicks Cancel of the open question of the close of an own tab of a tool (K16) and settles. \returns false
    /// without it.
    static bool cancelToolTabClose(QWidget* window)
    {
        QDialog* dialog = window ? window->findChild<QDialog*>(QStringLiteral("toolTabCloseDialog")) : nullptr;
        QPushButton* cancel = dialog && dialog->isVisible() ? dialog->findChild<QPushButton*>(QStringLiteral("cancelButton")) : nullptr;
        if (!cancel)
        {
            return false;
        }
        cancel->click();
        AppToolShellFixture::settle();
        return true;
    }

    /// The new mark of Redact made Locked (ISO 32000-2 Table 167: /F 132 = Print + Locked) by a change of the tool:
    /// Remove New Marks cannot remove it
    static bool lockNewMark(pdf::PDFAppDocument* document, PDFDocumentToolShell* shell)
    {
        pdf::PDFAppToolGUIRedact* gui = qobject_cast<pdf::PDFAppToolGUIRedact*>(shell ? shell->getGUI() : nullptr);
        const pdf::PDFDocumentPointer pdf = document ? document->getDocument() : pdf::PDFDocumentPointer();
        if (!gui || !pdf || gui->getNewMarks().size() != 1)
        {
            return false;
        }
        const pdf::PDFObjectReference mark = gui->getNewMarks().front();
        const pdf::PDFDictionary* dictionary = pdf->getDictionaryFromObject(pdf->getObjectByReference(mark));
        if (!dictionary)
        {
            return false;
        }
        pdf::PDFDictionaryBuilder locked(*dictionary);
        locked.setEntry(pdf::PDFInplaceOrMemoryString("F"), pdf::PDFObject::createInteger(132));
        pdf::PDFDocumentBuilder builder(pdf.data());
        builder.setObject(mark, pdf::PDFObject::createDictionary(std::move(locked)));
        const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
        const bool isCommitted = shell->commitDocumentChange(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Lock the mark"),
                                                             document->getVersion());
        AppToolShellFixture::settle();
        return isCommitted && gui->getNewMarks().size() == 1;
    }

    static qint64 usageCount(const pdftest::AppDocumentFixture& fixture, const QString& toolId)
    {
        for (const pdf::PDFToolUsage& usage : pdf::PDFToolRepository(fixture.context->getDatabase()).getUsage())
        {
            if (usage.toolId == toolId)
            {
                return usage.count;
            }
        }
        return 0;
    }
};

/// A translator, which changes nothing: installing it sends LanguageChange to every widget
class LeaveTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
    virtual QString translate(const char*, const char*, const char*, int) const override { return QString(); }
};

/// The session manager of the system replaced by a recorder (QSessionManager has no public constructor)
class LeaveTestSessionManager : public pdf::PDFAppSessionManager
{
public:
    virtual bool allowsInteraction() override { return isInteractionAllowed; }
    virtual void release() override { ++releaseCount; }
    virtual void cancel() override { ++cancelCount; }

    bool isInteractionAllowed = true;
    int releaseCount = 0;
    int cancelCount = 0;
};

using H = LeaveTestHelper;

class AppToolLeaveTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void noWorkClosesWithoutQuestion_data();
    void noWorkClosesWithoutQuestion();
    void removedWorkClosesWithoutQuestion_data();
    void removedWorkClosesWithoutQuestion();
    void everyLeaveAsks_data();
    void everyLeaveAsks();
    void escapeTwiceOneQuestion();
    void measureSaveAsAnnotations();
    void measureSavedClosesWithoutQuestion();
    void redactEntryMarksNoQuestion();
    void assistantRefused();
    void interactiveUseByAction();
    void interactiveUseByVisibleTime();
    void interactiveShortOpenNotRecorded();
    void interactiveBackgroundTimeNotCounted();
    void interactiveVisibleTimeByEvents();
    void interactiveOneRecord();
    void runToolNotRecordedWithoutRun();
    void assistantRunNotRecorded();
    void ownTabUntouchedListNoQuestion_data();
    void ownTabUntouchedListNoQuestion();
    void interactiveUseRecordedOnTabClose();
    void interactiveUseRecordedOnQuit();
    void assistantOpenedInteractiveRecorded();
    void answeredQuestionAsksAgain();
    void waitTabDeleted();
    void waitSecondLeaveRefused();
    void waitDocumentLost();
    void waitLanguageAndThemeChange();
    void escapeQuestionReusedByTabClose();
    void quitClosingAnswerClosesWindow();
    void quitDiscardThenKeepKeepsWork();
    void quitAsksDocumentToolsFirst();
    void lockAsksToolQuestion();
    void lockRedactRemoveNewMarks();
    void tabCloseMeasureSaveAndClose();
    void tabCloseCanceledKeepsAppliedAction();
    void incompleteQuitKeepMeasuring();
    void incompleteQuitDiscardAndClose();
    void incompleteTabCloseKeepMeasuring();
    void incompleteTabCloseDiscardAndClose();
    void incompleteRouteKeepMeasuring();
    void incompleteEscapeBothAnswers();
    void incompleteLockRefused();
    void sessionEndListsToolWork();
    void assistantNavigateNotLeaving();
    void assistantRefusedWhileQuestionOpen();
    void assistantRefusedDuringFlow();
    void assistantDuringCloseSaveAs();
    void assistantChangeDuringQuit();
    void flowEndedOutsideReleasesWaits();
    void ownTabCancelRefusedResumes();
    void thumbnailsMenuToolQueued();

private:
    void addRows();
};

void AppToolLeaveTest::cleanup()
{
    PDFDocumentToolShell::setUseClock(nullptr);
}

void AppToolLeaveTest::addRows()
{
    QTest::addColumn<QString>("name");
    for (const auto& [name, tool] : H::cases())
    {
        Q_UNUSED(tool);
        QTest::newRow(qPrintable(name)) << name;
    }
}

void AppToolLeaveTest::noWorkClosesWithoutQuestion_data()
{
    addRows();
}

void AppToolLeaveTest::noWorkClosesWithoutQuestion()
{
    // Owner 2026-11-20: a tool, in which the user changed nothing, closes without a question
    QFETCH(QString, name);
    const LeaveToolCase tool = H::cases()[name];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(!shell->getGUI()->hasUnsavedWork());
    QVERIFY(!tab->getPage()->hasUnsavedWork());

    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(!shell);
    QVERIFY(!tab->getView()->isToolModeActive());
}

void AppToolLeaveTest::removedWorkClosesWithoutQuestion_data()
{
    addRows();
}

void AppToolLeaveTest::removedWorkClosesWithoutQuestion()
{
    // Work, which the user has taken away again (removed, undone), is no work: no question
    QFETCH(QString, name);
    const LeaveToolCase tool = H::cases()[name];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(shell->getGUI()->hasUnsavedWork());
    QVERIFY(tool.removeWork(shell));
    QVERIFY(!shell->getGUI()->hasUnsavedWork());

    // Another route (a navigation of the user) leaves at once
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(!shell);
    QVERIFY(!tab->getView()->isToolModeActive());
}

void AppToolLeaveTest::everyLeaveAsks_data()
{
    addRows();
}

void AppToolLeaveTest::everyLeaveAsks()
{
    // Esc / Cancel, the close of the tab, another route and the quit ask the same question; its answer Keep keeps
    // the tool with its work, the closing answer lets it go
    QFETCH(QString, name);
    const LeaveToolCase tool = H::cases()[name];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(tab->getPage()->hasUnsavedWork());

    // Esc (Cancel of the shell): the question, the tool stays until the answer
    shell->cancel();
    AppToolShellFixture::settle();
    QDialog* dialog = H::question(shell);
    QVERIFY(dialog);
    QVERIFY2(H::hasLabel(dialog, tool.title), qPrintable(tool.title));
    QVERIFY(H::button(dialog, tool.keepText));
    QVERIFY(H::button(dialog, tool.closeText));
    QVERIFY(shell->getState() == PDFDocumentToolShell::State::Editing);
    H::button(dialog, tool.keepText)->click();
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QVERIFY(!H::question(shell));
    QVERIFY(shell->getGUI()->hasUnsavedWork());

    // The close of the tab: Keep refuses it (A0: the question is shown, the close returns; its answer continues)
    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(shell);
    QVERIFY(fixture.window->findDocumentTab(tab->getDocumentId()));
    QVERIFY(tab->getView()->isToolModeActive());
    QVERIFY(!tab->isLeaving());

    // The quit of the window: the question per tab, Keep keeps the window
    QVERIFY(!fixture.window->close());
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(fixture.window->isVisible());
    QVERIFY(shell);

    // Another route: Keep stays in the tool
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(shell);
    QVERIFY(shell->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());

    // Another route with the closing answer: the tool goes, the route is shown
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::answer(shell, tool.closeText));
    QVERIFY(!shell);
    QVERIFY(!tab->getView()->isToolModeActive());
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::escapeTwiceOneQuestion()
{
    // Measure (the report of the owner): the first Esc ends the drawing mode, the second asks; a key repeat or an Esc
    // while the question opens gives no second question, Esc in the question is its Keep
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui->isDrawing());
    QAction* escape = shell->findChild<QAction*>(QStringLiteral("toolEscape"));
    QVERIFY(escape);

    escape->trigger();
    AppToolShellFixture::settle();
    QVERIFY(!gui->isDrawing());
    QCOMPARE(H::countShownQuestions(), 0);

    // The second Esc and its repeat before the question has the keys
    escape->trigger();
    escape->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 1);
    QDialog* dialog = H::question(shell);
    QVERIFY(dialog);
    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 1);
    QCOMPARE(H::question(shell), dialog);

    // Esc in the question = Keep Measuring: the tool and its measurement stay
    QTest::keyClick(dialog, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(shell);
    QCOMPARE(gui->getMeasurements().size(), size_t(1));

    // RESCCODE M3: the question is open and its work goes meanwhile - another Esc never ends the tool behind the open
    // question (the open question counts, not the work of the moment); the user answers it
    shell->cancel();
    AppToolShellFixture::settle();
    dialog = H::question(shell);
    QVERIFY(dialog);
    gui->clearMeasurements();
    QVERIFY(!gui->hasUnsavedWork());
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(H::question(shell), dialog);
    QCOMPARE(H::countShownQuestions(), 1);
    QTest::keyClick(dialog, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(shell);
}

void AppToolLeaveTest::measureSaveAsAnnotations()
{
    // Save as Annotations of the question (as Redact keeps its marks): the temporary measurements become measurement
    // annotations of the document and the tool closes
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->addMeasurement(1, pdf::PDFMeasureKind::Distance, { QPointF(100, 200), QPointF(172, 200) }, pdf::PDFAppMeasureDrawKind::Linear));

    shell->cancel();
    AppToolShellFixture::settle();
    QDialog* dialog = H::question(shell);
    QVERIFY(dialog);
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Discard the 2 temporary measurements?")));
    QVERIFY(H::hasLabel(dialog, QStringLiteral("Closing the tool removes the 2 measurements. Save them as annotations to keep them in the document.")));
    QVERIFY(H::button(dialog, QStringLiteral("Keep Measuring")));
    QVERIFY(H::button(dialog, QStringLiteral("Discard Measurements")));
    QPushButton* save = H::button(dialog, QStringLiteral("Save and Close"));
    QVERIFY(save);
    save->click();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QVERIFY(!tab->getView()->isToolModeActive());

    // ISO 32000-2, 12.9: two measurement annotations (one on each page), one step of Undo
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    std::vector<pdf::PDFInteger> pages(document->getCatalog()->getPageCount());
    std::iota(pages.begin(), pages.end(), pdf::PDFInteger(0));
    QCOMPARE(pdf::PDFMeasureAnnotationInfo::readPages(document.data(), pages, 1000, 1000, nullptr, nullptr).size(), size_t(2));
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);
}

void AppToolLeaveTest::measureSavedClosesWithoutQuestion()
{
    // Work saved as annotations (the Primary button) is in the document: no question
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui->saveAsAnnotations());
    QVERIFY(gui->getMeasurements().empty());

    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(!shell);
}

void AppToolLeaveTest::redactEntryMarksNoQuestion()
{
    // The marks, which the document had when Redact opened, are no work of this session: no question
    const LeaveToolCase tool = H::cases()[QStringLiteral("redact")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));

    // Keep Marks and Close: the mark stays in the document
    shell->cancel();
    AppToolShellFixture::settle();
    QPushButton* keep = H::button(H::question(shell), tool.closeText);
    QVERIFY(keep);
    keep->click();
    AppToolShellFixture::settle();
    QVERIFY(!shell);

    shell = fixture.openTool(tab, tool.toolId);
    AppToolShellFixture::settle();
    QVERIFY(shell);
    pdf::PDFAppToolGUIRedact* gui = qobject_cast<pdf::PDFAppToolGUIRedact*>(shell->getGUI());
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getMarks().size() == 1; }));
    QVERIFY(!gui->hasUnsavedWork());
    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(!shell);
}

void AppToolLeaveTest::assistantRefused()
{
    // An assistant never closes a tool over the user's work (view.close, view.navigate refused); without the work
    // its close goes through
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plan.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const QString documentId = tab->getDocument()->getId();
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID)),
                                             pdf::PDFAppNavigationMode::Push, tab);
    pdftest::AppDocumentFixture::settle();
    QPointer<PDFDocumentToolShell> shell = AppToolShellFixture::getShell(tab);
    QVERIFY(shell);
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isReadingAnnotations(); }));
    QVERIFY(gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear));

    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("close") },
                                                                                                         { QStringLiteral("documentId"), documentId } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("The user has unsaved work in a tool")), qPrintable(result->getFirstText()));
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(fixture.window->findDocumentTab(tab->getDocumentId()));
    QVERIFY(shell);

    const QJsonObject navigate({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                 { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), pdf::PDFAppRoutes::document(tab->getDocumentId()).toString() } }) } });
    result = fixture.callTool(QStringLiteral("view"), navigate);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("The user has unsaved work in a tool")), qPrintable(result->getFirstText()));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(shell);
    QCOMPARE(gui->getMeasurements().size(), size_t(1));

    // Without the work the assistant's navigation leaves the tool
    gui->clearMeasurements();
    result = fixture.callTool(QStringLiteral("view"), navigate);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!shell);
}

void AppToolLeaveTest::interactiveUseByAction()
{
    // Owner 2026-11-20: an interactive tool (Measure) left after an action of the user is a use (Recent tools)
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(tool.removeWork(shell));
    clock.now += 2000;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));
}

void AppToolLeaveTest::interactiveUseByVisibleTime()
{
    // 10 s shown without an action is a use too
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    shell->accountVisibleTime();
    clock.now += PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS;
    shell->accountVisibleTime();
    QCOMPARE(shell->getVisibleTime(), PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS);
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));
}

void AppToolLeaveTest::interactiveShortOpenNotRecorded()
{
    // Opened and closed at once (no action, under 10 s): no use
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    clock.now += PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS - 1;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));
}

void AppToolLeaveTest::interactiveBackgroundTimeNotCounted()
{
    // The time of the tool in a background tab does not count
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    shell->accountVisibleTime();
    clock.now += 3000;
    shell->accountVisibleTime();
    QCOMPARE(shell->getVisibleTime(), qint64(3000));

    // Another tab is current: 30 s in the background
    pdf::PDFAppDocumentTab* other = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1));
    QVERIFY(other);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), other);
    shell->accountVisibleTime();
    clock.now += 30000;
    shell->accountVisibleTime();
    QCOMPARE(shell->getVisibleTime(), qint64(3000));

    // Back in the tab of the tool for 2 s more
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    shell->accountVisibleTime();
    clock.now += 2000;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));
}

void AppToolLeaveTest::interactiveVisibleTimeByEvents()
{
    // P2NOPOLL: no periodic timer runs while an interactive tool is open; the visible time follows the events of its
    // tab and its window (owner 2026-11-20: a background tab and a minimized window do not count), nobody calls
    // accountVisibleTime here
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    for (const QTimer* timer : shell->findChildren<QTimer*>())
    {
        QVERIFY2(!timer->isActive() || timer->isSingleShot(), timer->parent() ? timer->parent()->metaObject()->className() : "no parent");
    }

    // 3 s in view, then another tab is current: the hiding of the tab accounts them
    clock.now += 3000;
    pdf::PDFAppDocumentTab* other = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1));
    QVERIFY(other);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), other);
    QCOMPARE(shell->getVisibleTime(), qint64(3000));

    // 30 s in the background count nothing
    clock.now += 30000;
    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QCOMPARE(shell->getVisibleTime(), qint64(3000));

    // 1 s in view, then the window minimized for a minute
    clock.now += 1000;
    fixture.window->showMinimized();
    AppToolShellFixture::settle();
    QVERIFY(fixture.window->isMinimized());
    QCOMPARE(shell->getVisibleTime(), qint64(4000));
    clock.now += 60000;
    fixture.window->showNormal();
    AppToolShellFixture::settle();
    QVERIFY(!fixture.window->isMinimized());
    QCOMPARE(shell->getVisibleTime(), qint64(4000));

    // 2 s more and the leave: 6 s in view is no use
    clock.now += 2000;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));
}

void AppToolLeaveTest::interactiveOneRecord()
{
    // An action and 10 s shown give one record per opening
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(tool.removeWork(shell));
    shell->accountVisibleTime();
    clock.now += 2 * PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));

    // A new opening, closed at once: nothing more
    shell = fixture.openTool(tab, tool.toolId);
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QVERIFY(!shell->isUseRecorded());
    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));
}

void AppToolLeaveTest::runToolNotRecordedWithoutRun()
{
    // A tool, whose use is its run (Rotate Pages), keeps the rule of today: shown long, closed without a run - no use
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("rotate")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(!shell->getGUI()->isInteractive());
    clock.now += 3 * PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS;
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));
}

void AppToolLeaveTest::assistantRunNotRecorded()
{
    // A run of an interactive tool by an assistant (review.statistics) records nothing (newapp_tools.md 3)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Stats.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("review"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("statistics") },
                                                                                                                   { QStringLiteral("statistics"), QJsonObject() },
                                                                                                                   { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    pdftest::AppDocumentFixture::settle();
    for (const pdf::PDFToolUsage& usage : pdf::PDFToolRepository(fixture.context->getDatabase()).getUsage())
    {
        QVERIFY2(usage.toolId != QLatin1String("review.statistics"), qPrintable(usage.toolId));
    }
}

void AppToolLeaveTest::ownTabUntouchedListNoQuestion_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QString>("lossText");
    QTest::newRow("merge") << QStringLiteral("pages.merge") << QStringLiteral("The list of 2 files is lost.");
    QTest::newRow("compare") << QStringLiteral("review.compare") << QStringLiteral("The 2 chosen documents are lost.");
}

void AppToolLeaveTest::ownTabUntouchedListNoQuestion()
{
    // Owner 2026-11-20: the inputs, which the own tab came with (the document it was started over), are no work: no
    // dot, no question (K16); a file added by the user is
    QFETCH(QString, toolId);
    QFETCH(QString, lossText);
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.openDocument(3);
    QVERIFY(documentTab);
    QVERIFY(AppToolShellFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }));
    QPointer<pdf::PDFAppToolTab> tab = fixture.window->createToolTabForDocument(toolId, documentTab);
    AppToolShellFixture::settle();
    QVERIFY(tab && tab->getShell());
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QVERIFY(tab->getLossText().isEmpty());
    QVERIFY(!tab->isTabUnsaved());
    QVERIFY(!tab->getPage()->hasUnsavedWork());

    // A file added by the user (Merge: Add Files of its GUI, which writes its list into the model)
    const QString annex = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex.pdf"), 2);
    if (pdf::PDFAppMergeToolGUI* merge = qobject_cast<pdf::PDFAppMergeToolGUI*>(tab->getShell()->getGUI()))
    {
        merge->addFiles({ annex });
    }
    else
    {
        tab->getInputList()->addFile(annex);
    }
    AppToolShellFixture::settle();
    QCOMPARE(tab->getInputList()->getCount(), 2);
    QCOMPARE(tab->getLossText(), lossText);
    QVERIFY(tab->isTabUnsaved());
    QVERIFY(tab->getPage()->hasUnsavedWork());

    // The close of the tab asks (K16): Cancel keeps it
    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(tab->isAskingClose());
    QVERIFY(H::cancelToolTabClose(fixture.window));
    QVERIFY(tab);
    QVERIFY(!tab->isAskingClose());
}

void AppToolLeaveTest::interactiveUseRecordedOnTabClose()
{
    // ASan of P2ESCWORK: the destructor of the shell never reads its tab (half destroyed then) nor writes the database;
    // the close of the tab records the use of an interactive tool before the teardown (PDFAppPage::prepareClose)
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(tool.removeWork(shell));
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));

    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    QVERIFY(fixture.window->closeTab(tab));
    AppToolShellFixture::settle();
    QVERIFY(!tabGuard);
    QVERIFY(!shell);
    QCOMPARE(H::countShownQuestions(), 0);
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));
}

void AppToolLeaveTest::interactiveUseRecordedOnQuit()
{
    // The quit records the use before the windows are torn down (PDFAppMainWindow::prepareTabsClose)
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    shell->accountVisibleTime();
    clock.now += PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS;
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(0));

    QVERIFY(fixture.window->close());
    AppToolShellFixture::settle();
    QVERIFY(!fixture.window || !fixture.window->isVisible());
    QCOMPARE(H::usageCount(fixture, tool.toolId), qint64(1));
}

void AppToolLeaveTest::assistantOpenedInteractiveRecorded()
{
    // Owner 2026-11-20, decision 9: an interactive tool opened by an assistant, which the user then has in view for
    // 10 s (or uses), is a use; opened and left by the assistant at once it is none
    LeaveTestClock clock;
    PDFDocumentToolShell::setUseClock(&clock);
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Assist.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID);
    auto navigate = [&fixture](const pdf::PDFAppRoute& route)
    {
        const QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                      { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), route.toString() } }) } });
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), arguments);
        pdftest::AppDocumentFixture::settle();
        return result && !result->isError;
    };

    QVERIFY(navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), toolId)));
    QPointer<PDFDocumentToolShell> shell = AppToolShellFixture::getShell(tab);
    QVERIFY(shell);
    shell->accountVisibleTime();
    clock.now += PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS;
    shell->accountVisibleTime();
    QCOMPARE(shell->getVisibleTime(), PDFDocumentToolShell::INTERACTIVE_USE_VISIBLE_MS);
    shell->cancel();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, toolId), qint64(1));

    // A second opening by the assistant, left by it after 2 s: nothing more
    QVERIFY(navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), toolId)));
    shell = AppToolShellFixture::getShell(tab);
    QVERIFY(shell);
    clock.now += 2000;
    QVERIFY(navigate(pdf::PDFAppRoutes::document(tab->getDocumentId())));
    QVERIFY(!shell);
    QCOMPARE(H::usageCount(fixture, toolId), qint64(1));
}

void AppToolLeaveTest::answeredQuestionAsksAgain()
{
    // RESCCODE L1: an answered question is hidden until its deletion - it is no open question; an Esc right after
    // Keep asks again (never a hidden question, which nobody can answer)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    shell->cancel();
    AppToolShellFixture::settle();
    QPointer<QDialog> answered = H::question(shell);
    QVERIFY(answered);
    H::button(answered, tool.keepText)->click();
    QVERIFY(answered && !answered->isVisible());

    // No event loop between: the answered question still exists
    shell->cancel();
    QDialog* again = H::question(shell);
    QVERIFY(again);
    QVERIFY(again != answered.data());
    QCOMPARE(H::countShownQuestions(), 1);
    QTest::keyClick(again, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(shell);
    QVERIFY(shell->getGUI()->hasUnsavedWork());
}

void AppToolLeaveTest::waitTabDeleted()
{
    // RESCCODE M3: the tab is deleted, while a leave waits for the answer - the wait ends, the caller returns
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    pdftest::NestedEventLoopGuard loopGuard;
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::question(shell));
    delete tabGuard.data();
    AppToolShellFixture::settle();
    QVERIFY(!tabGuard);
    QVERIFY(!shell);
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(fixture.window->isVisible());
}

void AppToolLeaveTest::waitSecondLeaveRefused()
{
    // RESCCODE M3: a second leave (the close of the tab) while the first one waits is refused - one question
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdftest::NestedEventLoopGuard loopGuard;
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    int questions = H::countShownQuestions();
    QVERIFY(H::question(shell));
    const pdf::PDFAppLeaveResult secondResult = fixture.window->requestCloseTab(tab);
    questions = std::max(questions, H::countShownQuestions());
    QVERIFY(H::answer(shell, tool.keepText));
    QCOMPARE(questions, 1);
    QCOMPARE(secondResult, pdf::PDFAppLeaveResult::Refused);
    QVERIFY(fixture.window->findDocumentTab(tab->getDocumentId()));
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(shell->getGUI()->hasUnsavedWork());
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::waitDocumentLost()
{
    // RESCCODE M3 + L2: the document is lost, while the close of its tab waits for the answer - the tool ends with its
    // question, nothing is left to lose: the close goes on
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::question(shell));

    // The close of the flow closes the tab (its flow ends Done), not the report of the failed document (the window
    // waits for the flow, M2 of the review of A0)
    QSignalSpy flowSpy(tab->findLeaveFlow(), &pdf::PDFAppLeaveFlow::finished);
    tabGuard->getDocument()->setFailed(QStringLiteral("Leave.pdf"), QStringLiteral("Damaged"));
    AppToolShellFixture::settle();
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(!tabGuard);
    QVERIFY(!shell);
    QCOMPARE(flowSpy.count(), 1);
    QCOMPARE(flowSpy.front().front().toBool(), true);
}

void AppToolLeaveTest::waitLanguageAndThemeChange()
{
    // RESCCODE M3: the language and the theme change, while a leave waits - the question stays, its answer decides
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    LeaveTestTranslator translator;
    pdftest::NestedEventLoopGuard loopGuard;
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    QDialog* question = H::question(shell);
    QVERIFY(question);
    QCoreApplication::installTranslator(&translator);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    AppToolShellFixture::settle();
    QCoreApplication::removeTranslator(&translator);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    AppToolShellFixture::settle();
    QCOMPARE(H::question(shell), question);
    QVERIFY(tab->isLeaving());
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QVERIFY(shell->getGUI()->hasUnsavedWork());
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::escapeQuestionReusedByTabClose()
{
    // RESCCODE M3: the question opened by Esc is the question of the close of the tab too (never a second one)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    shell->cancel();
    AppToolShellFixture::settle();
    QDialog* escQuestion = H::question(shell);
    QVERIFY(escQuestion);

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    const int questions = H::countShownQuestions();
    QDialog* waited = H::question(shell);
    QVERIFY(H::answer(shell, tool.keepText));
    QCOMPARE(questions, 1);
    QCOMPARE(waited, escQuestion);
    QVERIFY(shell);
    QVERIFY(fixture.window->findDocumentTab(tab->getDocumentId()));
    QVERIFY(shell->getGUI()->hasUnsavedWork());
}

void AppToolLeaveTest::quitClosingAnswerClosesWindow()
{
    // RESCCODE M3: the closing answer of the question of the quit lets the window close
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(H::answer(shell, tool.closeText));
    QVERIFY(!fixture.window || !fixture.window->isVisible());
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::quitDiscardThenKeepKeepsWork()
{
    // Owner 2026-11-20, decision 8: Discard for the first tool, Keep for the second - the quit is canceled and the
    // first tool keeps its work too (nothing is discarded, until the quit really goes)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* firstTab = nullptr;
    QPointer<PDFDocumentToolShell> first = H::open(fixture, tool, &firstTab);
    QVERIFY(first);
    QVERIFY(tool.makeWork(fixture, first));
    pdf::PDFAppDocumentTab* secondTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 3));
    QVERIFY(secondTab && AppToolShellFixture::waitFor([secondTab]() { return secondTab->getDocument()->isReady(); }));
    AppToolShellFixture::settle();
    QPointer<PDFDocumentToolShell> second = fixture.openTool(secondTab, tool.toolId);
    AppToolShellFixture::settle();
    QVERIFY(second);
    QVERIFY(tool.makeWork(fixture, second));

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(H::answer(first, tool.closeText));
    QVERIFY(H::answer(second, tool.keepText));
    QVERIFY(fixture.window->isVisible());
    QVERIFY(!pdf::PDFAppLeaveFlow::isRunningIn(fixture.window.data()));
    for (const QPointer<PDFDocumentToolShell>& shell : { first, second })
    {
        QVERIFY(shell);
        QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
        pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
        QVERIFY(gui);
        QCOMPARE(gui->getMeasurements().size(), size_t(1));
    }
    QVERIFY(firstTab->getView()->isToolModeActive());
    QVERIFY(secondTab->getView()->isToolModeActive());
}

void AppToolLeaveTest::quitAsksDocumentToolsFirst()
{
    // Decision 8: the tools of the document tabs ask before the own tabs of the tools - a Keep there cancels the quit
    // before an own tab is asked (its question of a run stops the run at once)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QPointer<pdf::PDFAppToolTab> toolTab = fixture.window->createToolTabForDocument(QStringLiteral("pages.merge"), tab);
    AppToolShellFixture::settle();
    QVERIFY(toolTab && toolTab->getShell());
    pdf::PDFAppMergeToolGUI* merge = qobject_cast<pdf::PDFAppMergeToolGUI*>(toolTab->getShell()->getGUI());
    QVERIFY(merge);
    merge->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex.pdf"), 2) });
    AppToolShellFixture::settle();
    QVERIFY(toolTab->getPage()->hasUnsavedWork());

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(!H::cancelToolTabClose(fixture.window));
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(!H::cancelToolTabClose(fixture.window));
    QVERIFY(toolTab);
    QVERIFY(shell);
}

void AppToolLeaveTest::lockAsksToolQuestion()
{
    // Owner 2026-11-20, decision 6: Lock document with a tool, which the lock ends, asks its question first - Keep
    // cancels the lock, the closing answer locks
    const LeaveToolCase tool = H::cases()[QStringLiteral("rotate")];
    const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(tool.toolId);
    QVERIFY(entry && entry->getLockedAccess() == pdf::PDFDocumentToolLockedAccess::Unavailable);
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    QVERIFY(tab->isLeaving());
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(!tab->getDocument()->isLocked());
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(shell->getGUI()->hasUnsavedWork());
    QVERIFY(!tab->isLeaving());

    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    QVERIFY(H::answer(shell, tool.closeText));
    QVERIFY(tab->getDocument()->isLocked());
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::lockRedactRemoveNewMarks()
{
    // Owner 2026-11-20, decisions 6 and 8: Lock document with Redact and a new mark - Remove New Marks of its question
    // (448:17473) changes nothing while the lock waits for the answer; the lock goes on and the new mark is removed in
    // one Undo step of the tool while the tool still edits (PDFAppPage::finishLeave before the lock), then the
    // document is locked and the tool ends
    const LeaveToolCase tool = H::cases()[QStringLiteral("redact")];
    const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(tool.toolId);
    QVERIFY(entry && entry->getLockedAccess() == pdf::PDFDocumentToolLockedAccess::Unavailable);
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(H::annotationCount(document), size_t(1));
    const int undoCount = AppToolShellFixture::undoCount(document);

    // The answer is only remembered: right after the click the mark is still there and the document is not locked
    // A0: the continuation runs in the click of the answer (the remembered choice is applied right after the
    // question in the same flow, before the lock); the state at the answer is read by a slot of the dialog's finished
    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    QDialog* question = H::question(shell);
    QPushButton* remove = H::button(question, QStringLiteral("Remove New Marks"));
    QVERIFY(remove);
    size_t annotationsAfterAnswer = 0;
    bool isLockedAfterAnswer = true;
    QObject::connect(question, &QDialog::finished, question, [&]()
    {
        annotationsAfterAnswer = H::annotationCount(document);
        isLockedAfterAnswer = document->isLocked();
    });
    remove->click();
    AppToolShellFixture::settle();
    QCOMPARE(annotationsAfterAnswer, size_t(1));
    QVERIFY(!isLockedAfterAnswer);
    QVERIFY(document->isLocked());
    QCOMPARE(H::annotationCount(document), size_t(0));
    QCOMPARE(AppToolShellFixture::undoCount(document), undoCount + 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Remove the new redaction marks"));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!document->hasPendingEdits());
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::tabCloseMeasureSaveAndClose()
{
    // Decision 8: the close of the tab with Measure and a temporary measurement - Save and Close of its question is
    // applied when the close goes on, before the question of the unsaved changes (the measurement is an annotation
    // then, ISO 32000-2 12.9); Save of that question writes it into the file and the tab closes
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    const QString filePath = tab->getDocument()->getFilePath();
    QVERIFY(!filePath.isEmpty());
    QVERIFY(!tab->getDocument()->isModified());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(0));

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QVERIFY(H::answerUnsaved(pdf::PDFAppDocumentChoice::Save));
    QVERIFY(AppToolShellFixture::waitFor([&tabGuard]() { return !tabGuard; }, 20000));

    // The file has the measurement annotation
    pdf::PDFAppDocumentTab* reopened = fixture.open(filePath);
    QVERIFY(reopened && AppToolShellFixture::waitFor([reopened]() { return reopened->getDocument() && reopened->getDocument()->isReady(); }));
    QCOMPARE(H::measureAnnotationCount(reopened->getDocument()), size_t(1));
}

void AppToolLeaveTest::tabCloseCanceledKeepsAppliedAction()
{
    // Decision 8, the documented state: Save and Close is applied when the close of the tab goes on past the questions
    // of the tool; Cancel of the following question of the unsaved changes keeps the tab - the annotation stays in the
    // document (one Undo step, the tool stays open without temporary measurements) and Undo of the tool takes it back
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::answerUnsaved(pdf::PDFAppDocumentChoice::Cancel));
    QVERIFY(tabGuard);
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(!shell->hasDeferredCloseQuestionAction());
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui);
    QVERIFY(gui->getMeasurements().empty());
    QVERIFY(!shell->hasUnsavedWork());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QVERIFY(tab->getDocument()->isModified());
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);

    QVERIFY(H::undo(shell));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(0));
}

void AppToolLeaveTest::incompleteQuitKeepMeasuring()
{
    // Owner 2026-11-21: Save and Close of the quit stores only one of two measurements (the other cannot be computed)
    // - the stored one is in the document, the question says why the other is not; Keep Measuring cancels the quit
    // before the question of the unsaved documents: the window and the tool stay with the measurement temporary
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppToolGUIMeasure> gui = H::measure(shell);

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::incompleteQuestion(shell));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QVERIFY(H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("1 measurement could not be saved as an annotation")));
    QVERIFY(H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("The value of the measurement cannot be computed. Keep Measuring keeps it as a temporary "
                                                                    "measurement. Discard and Close removes it and closes the tool.")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteKeep")));
    QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>());
    QVERIFY(fixture.window->isVisible());
    QVERIFY(!pdf::PDFAppLeaveFlow::isRunningIn(fixture.window.data()));
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);
    QVERIFY(!shell->isQuestionOpen());
}

void AppToolLeaveTest::incompleteQuitDiscardAndClose()
{
    // Owner 2026-11-21: Discard and Close of the question discards the measurement, which could not be saved, and the
    // quit goes on to the question of the unsaved documents (the stored annotation is a change of the document)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppToolGUIMeasure> gui = H::measure(shell);

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteDiscard")));
    QVERIFY(pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QVERIFY(gui);
    QCOMPARE(gui->getMeasurements().size(), size_t(0));
    QVERIFY(H::cancelQuitUnsaved());
    QVERIFY(fixture.window->isVisible());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
}

void AppToolLeaveTest::incompleteTabCloseKeepMeasuring()
{
    // Owner 2026-11-21: the close of the tab with Save and Close, which stores one of two measurements: Keep
    // Measuring keeps the tab (no question of the unsaved changes) and the tool with the other measurement
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    QPointer<pdf::PDFAppToolGUIMeasure> gui = H::measure(shell);

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteKeep")));
    QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>());
    QVERIFY(tabGuard);
    QVERIFY(!tab->isLeaving());
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
}

void AppToolLeaveTest::incompleteTabCloseDiscardAndClose()
{
    // Owner 2026-11-21: Discard and Close lets the close of the tab go on: the question of the unsaved changes, Save
    // writes the stored annotation (ISO 32000-2 12.9) and the tab closes
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    const QString filePath = tab->getDocument()->getFilePath();

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteDiscard")));
    QVERIFY(H::answerUnsaved(pdf::PDFAppDocumentChoice::Save));
    QVERIFY(AppToolShellFixture::waitFor([&tabGuard]() { return !tabGuard; }, 20000));

    pdf::PDFAppDocumentTab* reopened = fixture.open(filePath);
    QVERIFY(reopened && AppToolShellFixture::waitFor([reopened]() { return reopened->getDocument() && reopened->getDocument()->isReady(); }));
    QCOMPARE(H::measureAnnotationCount(reopened->getDocument()), size_t(1));
}

void AppToolLeaveTest::incompleteRouteKeepMeasuring()
{
    // Owner 2026-11-21: another route of the tab - Keep Measuring refuses the navigation (the tool stays); the next
    // navigation asks again: Save and Close stores nothing more (the last measurement cannot be computed), the
    // question names it, Discard and Close lets the navigation go on (the tool ends, the stored annotation stays)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppToolGUIMeasure> gui = H::measure(shell);
    const pdf::PDFAppRoute route = pdf::PDFAppRoutes::document(tab->getDocumentId());

    pdftest::NestedEventLoopGuard loopGuard;
    fixture.window->getNavigator()->navigate(route, pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteKeep")));
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));

    fixture.window->getNavigator()->navigate(route, pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::answer(shell, QStringLiteral("Save and Close")));
    QVERIFY(H::incompleteQuestion(shell));
    QVERIFY(H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("1 measurement could not be saved as an annotation")));
    QVERIFY(H::answerIncomplete(shell, QStringLiteral("measureIncompleteDiscard")));
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!tab->getView()->isToolModeActive());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), 1);
}

void AppToolLeaveTest::incompleteEscapeBothAnswers()
{
    // Owner 2026-11-21: the same question for Esc / Close inside the tool - Keep Measuring keeps the tool open with
    // the measurement, which could not be saved; Discard and Close removes it and closes the tool
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(H::makeIncompleteWork(shell));
    QPointer<pdf::PDFAppToolGUIMeasure> gui = H::measure(shell);

    shell->cancel();
    AppToolShellFixture::settle();
    QPushButton* save = H::button(H::question(shell), QStringLiteral("Save and Close"));
    QVERIFY(save);
    save->click();
    AppToolShellFixture::settle();
    QVERIFY(gui && gui->getIncompleteQuestion() && gui->getIncompleteQuestion()->isVisible());
    QCOMPARE(shell->getCloseQuestion(), static_cast<QDialog*>(gui->getIncompleteQuestion()));
    QVERIFY(shell->isQuestionOpen());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
    QPushButton* keep = gui->getIncompleteQuestion()->findChild<QPushButton*>(QStringLiteral("measureIncompleteKeep"));
    QVERIFY(keep);
    keep->click();
    AppToolShellFixture::settle();
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QVERIFY(!shell->isQuestionOpen());

    shell->cancel();
    AppToolShellFixture::settle();
    save = H::button(H::question(shell), QStringLiteral("Save and Close"));
    QVERIFY(save);
    save->click();
    AppToolShellFixture::settle();
    QVERIFY(gui && gui->getIncompleteQuestion() && gui->getIncompleteQuestion()->isVisible());
    QPushButton* discard = gui->getIncompleteQuestion()->findChild<QPushButton*>(QStringLiteral("measureIncompleteDiscard"));
    QVERIFY(discard);
    discard->click();
    AppToolShellFixture::settle();
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!tab->getView()->isToolModeActive());
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));
}

void AppToolLeaveTest::incompleteLockRefused()
{
    // Owner 2026-11-21: Lock document asks only the tools, which the lock ends (Measure works on a locked document,
    // its question never comes with the lock). Redact Remove New Marks with a new mark, which is Locked (Table 167):
    // the mark cannot be removed, so the lock is refused - the document stays unlocked and the tool open with the mark
    const LeaveToolCase tool = H::cases()[QStringLiteral("redact")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::lockNewMark(document, shell));

    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    QVERIFY(H::answer(shell, QStringLiteral("Remove New Marks")));
    QVERIFY(!document->isLocked());
    QVERIFY(!tab->isLeaving());
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(H::annotationCount(document), size_t(1));
    QVERIFY(!shell->isQuestionOpen());
}

void AppToolLeaveTest::sessionEndListsToolWork()
{
    // Owner 2026-11-20, decision 7: the end of the system session lists the document of a tool with unsaved work;
    // without interaction the end is canceled; after Quit Without Saving the windows closing then do not ask again
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    QVERIFY(!tab->getDocument()->isModified());

    // A0 (owner 2026-11-21): the end of the session is canceled without a question inside the handler (also without
    // the interaction of the session manager); the question of the documents follows from the event loop and never a
    // question of the tool
    pdftest::NestedEventLoopGuard loopGuard;
    {
        LeaveTestSessionManager manager;
        manager.isInteractionAllowed = false;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::CanceledWithoutInteraction);
        QCOMPARE(manager.cancelCount, 1);
        QCOMPARE(H::countShownQuestions(), 0);
        QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>());
        AppToolShellFixture::settle();
        QCOMPARE(H::countShownQuestions(), 0);
        QVERIFY(H::cancelQuitUnsaved());
    }

    {
        LeaveTestSessionManager manager;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::Canceled);
        QCOMPARE(manager.cancelCount, 1);
        AppToolShellFixture::settle();
        pdf::PDFAppQuitUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
        QVERIFY(dialog);
        QCOMPARE(dialog->getList()->getItemCount(), 1);
        dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Cancel)->click();
        AppToolShellFixture::settle();
        QVERIFY(shell && shell->getGUI()->hasUnsavedWork());
        QVERIFY(!pdf::PDFAppSessionEnd::isToolWorkDiscarded(tab->getPage()));
    }

    {
        LeaveTestSessionManager manager;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::Canceled);
        AppToolShellFixture::settle();
        pdf::PDFAppQuitUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
        QVERIFY(dialog);
        QCOMPARE(dialog->getList()->getItemCount(), 1);
        dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Discard)->click();
        AppToolShellFixture::settle();
        QCOMPARE(manager.releaseCount, 0);
        QVERIFY(pdf::PDFAppSessionEnd::isToolWorkDiscarded(tab->getPage()));

        // H1 of the review of A0: a quit of the user asks the tool as before A0 (the end of the session was canceled)
        QVERIFY(!fixture.window->close());
        QVERIFY(H::answer(shell, tool.keepText));
        QVERIFY(fixture.window->isVisible());
        QVERIFY(shell && shell->getGUI()->hasUnsavedWork());

        // More work after the answer is not given up: the next end of the session is canceled and asks again
        pdf::PDFAppToolGUIMeasure* measure = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
        QVERIFY(measure && measure->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(120, 140), QPointF(190, 140) }, pdf::PDFAppMeasureDrawKind::Linear));
        QVERIFY(!pdf::PDFAppSessionEnd::isToolWorkDiscarded(tab->getPage()));
        LeaveTestSessionManager again;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&again), pdf::PDFAppSessionEnd::Outcome::Canceled);
        QCOMPARE(again.cancelCount, 1);
        AppToolShellFixture::settle();
        dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
        QVERIFY(dialog);
        QCOMPARE(dialog->getList()->getItemCount(), 1);
        dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Discard)->click();
        AppToolShellFixture::settle();
        QVERIFY(pdf::PDFAppSessionEnd::isToolWorkDiscarded(tab->getPage()));

        // The next end of the session goes on; the window, which closes then, asks the tool no second time
        LeaveTestSessionManager next;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&next), pdf::PDFAppSessionEnd::Outcome::NoChanges);
        QCOMPARE(next.cancelCount, 0);
        QVERIFY(fixture.window->close());
        AppToolShellFixture::settle();
        QCOMPARE(H::countShownQuestions(), 0);
        QVERIFY(!fixture.window || !fixture.window->isVisible());
    }
}

void AppToolLeaveTest::assistantNavigateNotLeaving()
{
    // RESCCODE L5: only a navigation, which would leave the tool, is refused - its own route with a query (the route
    // table refuses a query of a tool route: the navigator shows it in another tab), the manual (another tab) and a
    // tool with its own tab over the document (a new tab) go through, the tool keeps its work
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plan.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const pdf::PDFAppRoute toolRoute = pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID));
    fixture.window->getNavigator()->navigate(toolRoute, pdf::PDFAppNavigationMode::Push, tab);
    pdftest::AppDocumentFixture::settle();
    QPointer<PDFDocumentToolShell> shell = AppToolShellFixture::getShell(tab);
    QVERIFY(shell);
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isReadingAnnotations(); }));
    QVERIFY(gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear));

    const pdf::PDFAppRoute routes[] = { pdf::PDFAppRoute::create(toolRoute.getSegments(), { pdf::PDFAppRoute::QueryItem(QStringLiteral("page"), QStringLiteral("2")) }),
                                        pdf::PDFAppRoutes::help(QStringLiteral("annotations")),
                                        pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), QStringLiteral("pages.merge")) };
    for (const pdf::PDFAppRoute& route : routes)
    {
        const QJsonObject navigate({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                     { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), route.toString() } }) } });
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), navigate);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(route.toString() + QStringLiteral(": ") + result->getFirstText()));
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(H::countShownQuestions(), 0);
        QVERIFY2(shell, qPrintable(route.toString()));
        QCOMPARE(gui->getMeasurements().size(), size_t(1));
    }
    QCOMPARE(fixture.window->getToolTabs().size(), qsizetype(1));
}

void AppToolLeaveTest::assistantRefusedWhileQuestionOpen()
{
    // RESCCODE L6: while a question of a tool waits for the user, an assistant shows no other tab
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* other = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plan.pdf"), 2));
    QVERIFY(other && tab);
    QTRY_VERIFY(tab->getDocument()->isReady() && other->getDocument()->isReady());
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID)),
                                             pdf::PDFAppNavigationMode::Push, tab);
    pdftest::AppDocumentFixture::settle();
    QPointer<PDFDocumentToolShell> shell = AppToolShellFixture::getShell(tab);
    QVERIFY(shell);
    pdf::PDFAppToolGUIMeasure* gui = qobject_cast<pdf::PDFAppToolGUIMeasure*>(shell->getGUI());
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isReadingAnnotations(); }));
    QVERIFY(gui->addMeasurement(0, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, pdf::PDFAppMeasureDrawKind::Linear));
    shell->cancel();
    pdftest::AppDocumentFixture::settle();
    QDialog* question = H::question(shell);
    QVERIFY(question);
    QCOMPARE(fixture.window->getCurrentTab(), tab);

    const QString otherId = other->getDocument()->getId();
    const QJsonObject activate({ { QStringLiteral("action"), QStringLiteral("activate") }, { QStringLiteral("documentId"), otherId } });
    const QJsonObject navigate({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                 { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), pdf::PDFAppRoutes::document(other->getDocumentId()).toString() } }) } });
    for (const QJsonObject& arguments : { activate, navigate })
    {
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), arguments);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("answering a question of a tool")), qPrintable(result->getFirstText()));
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(fixture.window->getCurrentTab(), tab);
        QCOMPARE(H::question(shell), question);
    }

    // Answered: the assistant shows the other document again
    H::button(question, QStringLiteral("Keep Measuring"))->click();
    pdftest::AppDocumentFixture::settle();
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), activate);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(fixture.window->getCurrentTab(), other);
    QVERIFY(shell);
}

void AppToolLeaveTest::assistantRefusedDuringFlow()
{
    // A0 (RR3-M6): while a flow of the window shows its question (the close of a tab asks about the unsaved
    // changes), an assistant neither closes nor navigates (denied_dialog: the question is a modal dialog); answered,
    // it closes the other document again
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* clean = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Clean.pdf"), 1));
    pdf::PDFAppDocumentTab* changed = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Changed.pdf"), 1));
    QVERIFY(clean && changed);
    QTRY_VERIFY(clean->getDocument()->isReady() && changed->getDocument()->isReady());
    pdf::PDFDocumentBuilder builder(changed->getDocument()->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Changed"));
    pdf::PDFDocumentPointer changedDocument(new pdf::PDFDocument(builder.build()));
    QVERIFY(changed->getDocument()->commit(pdf::PDFModifiedDocument(changedDocument, nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("Changed"),
                                           changed->getDocument()->getVersion(), pdf::PDFAppChangeSource::User));
    const QString cleanId = clean->getDocument()->getId();
    QPointer<pdf::PDFAppDocumentTab> cleanGuard(clean);

    // (no NestedEventLoopGuard: the client of the MCP fixture waits for its HTTP answer in its own loop)
    QCOMPARE(fixture.window->requestCloseTab(changed), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>());
    QVERIFY(pdf::PDFAppLeaveFlow::isRunningIn(fixture.window.data()));

    const QJsonObject close({ { QStringLiteral("action"), QStringLiteral("close") }, { QStringLiteral("documentId"), cleanId } });
    const QJsonObject navigate({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                 { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), pdf::PDFAppRoutes::home().toString() } }) } });
    for (const QJsonObject& arguments : { close, navigate })
    {
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), arguments);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("dialog")), qPrintable(result->getFirstText()));
        pdftest::AppDocumentFixture::settle();
        QVERIFY(cleanGuard);
        QVERIFY(pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>());
    }

    // Answered (Cancel): the flow has ended, the assistant closes the clean document
    QVERIFY(H::answerUnsaved(pdf::PDFAppDocumentChoice::Cancel));
    QVERIFY(!pdf::PDFAppLeaveFlow::isRunningIn(fixture.window.data()));
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), close);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!cleanGuard);
}

void AppToolLeaveTest::assistantDuringCloseSaveAs()
{
    // A0 (newapp_robustness.md 15.6): Save of close-unsaved of a copy waits for its Save As (the file dialog, modal as
    // before A0) - an assistant neither closes the tab nor navigates (denied_dialog); Cancel of the file dialog keeps
    // the tab, the assistant closes it then
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* original = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Original.pdf"), 1));
    QVERIFY(original);
    QTRY_VERIFY(original->getDocument()->isReady());
    pdf::PDFAppDocumentTab* copy = fixture.window->openDocumentCopy(original->getDocument());
    QVERIFY(copy && copy->getDocument()->isModified());
    QPointer<pdf::PDFAppDocumentTab> copyGuard(copy);
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());

    // (no NestedEventLoopGuard: the client of the MCP fixture waits for its HTTP answer in its own loop)
    QCOMPARE(fixture.window->requestCloseTab(copy), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::answerUnsaved(pdf::PDFAppDocumentChoice::Save));
    QVERIFY(pdftest::AppAsyncDialogs::findFileRequest());
    QVERIFY(copy->isClosingAfterSave());

    const QJsonObject close({ { QStringLiteral("action"), QStringLiteral("close") }, { QStringLiteral("documentId"), copy->getDocument()->getId() } });
    const QJsonObject navigate({ { QStringLiteral("action"), QStringLiteral("navigate") },
                                 { QStringLiteral("navigate"), QJsonObject({ { QStringLiteral("route"), pdf::PDFAppRoutes::home().toString() } }) } });
    for (const QJsonObject& arguments : { close, navigate })
    {
        // (view.close of a document with unsaved changes is refused before the dialog is looked at)
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("view"), arguments);
        QVERIFY(result);
        QVERIFY(result->isError);
        const bool isClose = arguments.value(QStringLiteral("action")).toString() == QStringLiteral("close");
        QVERIFY2(isClose || result->getFirstText().contains(QStringLiteral("dialog")), qPrintable(result->getFirstText()));
        pdftest::AppDocumentFixture::settle();
        QVERIFY(copyGuard);
        QVERIFY(copy->isClosingAfterSave());
        QVERIFY(pdftest::AppAsyncDialogs::findFileRequest());
    }

    QVERIFY(pdftest::AppAsyncDialogs::cancelFile());
    QTRY_VERIFY(!copy->isClosingAfterSave());
    QVERIFY(copyGuard);
    QVERIFY(copy->getDocument()->isModified());
    QVERIFY(!copy->isLeaving());
}

void AppToolLeaveTest::assistantChangeDuringQuit()
{
    // RR3-L7 of the review of A0: while the flow of the quit runs without a question (its Save All), a change of an
    // assistant is refused (denied_busy) - it could miss the save of its document and be lost by the quit. The flow
    // is started here as the quit starts it; the server of the instance is not involved (MCP is never suspended).
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Quit.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const QJsonObject add({ { QStringLiteral("action"), QStringLiteral("add") },
                            { QStringLiteral("add"), QJsonObject({ { QStringLiteral("items"), QJsonArray({ QJsonObject({ { QStringLiteral("type"), QStringLiteral("note") },
                                                                                                                         { QStringLiteral("page"), 1 },
                                                                                                                         { QStringLiteral("point"), QJsonObject({ { QStringLiteral("x"), 20 }, { QStringLiteral("y"), 380 } }) } }) }) } }) } });

    pdf::PDFAppLeaveFlow* flow = fixture.window->getLeaveFlow();
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), add);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("closing")), qPrintable(result->getFirstText()));
    QVERIFY(!tab->getDocument()->isModified());

    // A tool of the catalog (Rotate of the pages, the result kept in the document) is refused the same way
    const QJsonObject rotate({ { QStringLiteral("action"), QStringLiteral("rotate") },
                               { QStringLiteral("documentId"), tab->getDocument()->getId() },
                               { QStringLiteral("result"), QStringLiteral("keep") },
                               { QStringLiteral("rotate"), QJsonObject({ { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("1") } }) },
                                                                         { QStringLiteral("rotation"), QStringLiteral("right") } }) } });
    result = fixture.callTool(QStringLiteral("pages"), rotate);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("closing")), qPrintable(result->getFirstText()));
    QVERIFY(!tab->getDocument()->isModified());

    // The quit has ended: the changes go
    flow->finish(false);
    result = fixture.callTool(QStringLiteral("annotations"), add);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(tab->getDocument()->isModified());
    const quint64 version = tab->getDocument()->getVersion();
    result = fixture.callTool(QStringLiteral("pages"), rotate);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QTRY_VERIFY(tab->getDocument()->getVersion() > version);
}

void AppToolLeaveTest::flowEndedOutsideReleasesWaits()
{
    // L1 of the review of A0: the flow of the tab ends without the answer of the question of the tool (canceled from
    // outside, as view.close of an assistant ends a Pending close) - the question stays the question of the tool:
    // Keep keeps it, the next close asks again (nothing waits for the ended flow); its closing answer closes the tool
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));

    pdftest::NestedEventLoopGuard loopGuard;
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::question(shell));
    tab->findLeaveFlow()->finish(false);
    QVERIFY(!shell->isWaitingForLeave());
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(fixture.window->requestCloseTab(tab), pdf::PDFAppLeaveResult::Pending);
    QVERIFY(H::question(shell));
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(!tab->isLeaving());

    // A navigation ended from outside: Discard of the question closes the tool as without a leave
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    QVERIFY(H::question(shell));
    tab->findLeaveFlow()->finish(false);
    QVERIFY(H::answer(shell, tool.closeText));
    QVERIFY(!tab->getView()->isToolModeActive());
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    QCOMPARE(H::countShownQuestions(), 0);
}

void AppToolLeaveTest::ownTabCancelRefusedResumes()
{
    // L2 of the review of A0: Cancel of a tool in its own tab closes the tab; its question (the inputs are lost) is
    // Pending - Cancel there keeps the tool, which takes the next Cancel again (the same question)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Base.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    QPointer<pdf::PDFAppToolTab> toolTab = fixture.window->createToolTabForDocument(QStringLiteral("pages.merge"), tab);
    AppToolShellFixture::settle();
    QVERIFY(toolTab && toolTab->getShell());
    pdf::PDFAppMergeToolGUI* merge = qobject_cast<pdf::PDFAppMergeToolGUI*>(toolTab->getShell()->getGUI());
    QVERIFY(merge);
    merge->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex.pdf"), 2) });
    AppToolShellFixture::settle();
    QVERIFY(toolTab->getPage()->hasUnsavedWork());

    pdftest::NestedEventLoopGuard loopGuard;
    for (int round = 0; round < 2; ++round)
    {
        toolTab->getShell()->cancel();
        AppToolShellFixture::settle();
        QVERIFY2(H::cancelToolTabClose(fixture.window), qPrintable(QString::number(round)));
        QVERIFY(toolTab);
        QVERIFY(!toolTab->isLeaving());
        QCOMPARE(toolTab->getShell()->getState(), PDFDocumentToolShell::State::Editing);
    }
}

void AppToolLeaveTest::thumbnailsMenuToolQueued()
{
    // RESCCODE L12: a page tool of the context menu of the thumbnails navigates from the event loop - the question of
    // the open tool is never asked inside of QMenu::triggered (Qt reads the menu after the signal)
    const LeaveToolCase tool = H::cases()[QStringLiteral("measure")];
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, tool, &tab);
    QVERIFY(shell);
    QVERIFY(tool.makeWork(fixture, shell));
    pdf::PDFAppDocumentSidebar* sidebar = tab->getView()->getSidebar();
    QVERIFY(sidebar && sidebar->showPanel(QStringLiteral("thumbnails")));
    pdf::PDFAppThumbnailsPanel* thumbnails = qobject_cast<pdf::PDFAppThumbnailsPanel*>(sidebar->findPanel(QStringLiteral("thumbnails")));
    QVERIFY(thumbnails);
    thumbnails->selectPages({ 0 });
    AppToolShellFixture::settle();
    QPointer<QMenu> menu = thumbnails->createContextMenu();
    QVERIFY(menu);
    QAction* insert = nullptr;
    for (QAction* action : menu->actions())
    {
        insert = action->objectName() == QLatin1String("extractPages") ? action : insert;
    }
    QVERIFY(insert && insert->isEnabled());

    pdftest::NestedEventLoopGuard loopGuard;
    insert->trigger();
    QVERIFY(!H::question(shell));
    AppToolShellFixture::settle();
    QVERIFY(H::answer(shell, tool.keepText));
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    delete menu.data();
}

QTEST_MAIN(AppToolLeaveTest)

#include "tst_apptoolleavetest.moc"
