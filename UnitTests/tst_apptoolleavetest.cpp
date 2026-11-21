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

    /// Runs \p action once, when the question of the tool is shown (a leave waits for it in its own loop); the
    /// timer is a child of \p context
    static void runWhenShown(PDFDocumentToolShell* shell, QObject* context, std::function<void()> action)
    {
        QTimer* timer = new QTimer(context);
        timer->setInterval(10);
        QPointer<PDFDocumentToolShell> guard(shell);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, guard, action]()
        {
            if (!question(guard.data()))
            {
                return;
            }
            timer->stop();
            timer->deleteLater();
            action();
        });
        timer->start();
    }

    /// Answers the question of the tool by the button \p text, when it is shown (a leave waits for it in its own
    /// loop); \p asked becomes true then
    static void answerWhenShown(PDFDocumentToolShell* shell, const QString& text, bool* asked)
    {
        QTimer* timer = new QTimer(shell);
        timer->setInterval(10);
        QPointer<PDFDocumentToolShell> guard(shell);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, guard, text, asked]()
        {
            QPushButton* answer = button(question(guard.data()), text);
            if (!answer)
            {
                return;
            }
            *asked = true;
            timer->stop();
            timer->deleteLater();
            answer->click();
        });
        timer->start();
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

    /// Answers the question of the unsaved changes of the close of a tab by \p choice (Cancel rejects it), when it is
    /// shown; \p onShown runs first (the state at the question). The timer is a child of \p context.
    static void answerUnsavedWhenShown(QObject* context, pdf::PDFAppDocumentChoice choice, std::function<void()> onShown)
    {
        QTimer* timer = new QTimer(context);
        timer->setInterval(10);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, choice, onShown]()
        {
            pdf::PDFAppCloseUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppCloseUnsavedDialog>();
            if (!dialog)
            {
                return;
            }
            timer->stop();
            timer->deleteLater();
            onShown();
            if (choice == pdf::PDFAppDocumentChoice::Cancel)
            {
                dialog->reject();
            }
            else if (QPushButton* button = dialog->getChoiceButton(choice))
            {
                button->click();
            }
        });
        timer->start();
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

    /// Clicks the button \p buttonName of the question of Measure about the measurements, which could not be saved,
    /// when it is shown; \p onShown runs first (the state at the question)
    static void answerIncompleteWhenShown(PDFDocumentToolShell* shell, const QString& buttonName, std::function<void()> onShown)
    {
        QTimer* timer = new QTimer(qApp);
        timer->setInterval(10);
        QPointer<PDFDocumentToolShell> guard(shell);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, guard, buttonName, onShown]()
        {
            pdf::PDFAppToolGUIMeasure* gui = measure(guard.data());
            pdf::PDFMessageBox* box = gui ? gui->getIncompleteQuestion() : nullptr;
            QPushButton* button = box && box->isVisible() ? box->findChild<QPushButton*>(buttonName) : nullptr;
            if (!guard || button)
            {
                timer->stop();
                timer->deleteLater();
                if (button)
                {
                    onShown();
                    button->click();
                }
            }
        });
        timer->start();
        QTimer::singleShot(30000, timer, &QTimer::deleteLater);
    }

    /// Records, whether the question of the unsaved documents of the quit is shown, and cancels it; \p onShown runs
    /// first (the state at the question)
    static void cancelQuitUnsavedWhenShown(QObject* context, bool* isShown, std::function<void()> onShown)
    {
        QTimer* timer = new QTimer(context);
        timer->setInterval(10);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, isShown, onShown]()
        {
            pdf::PDFAppQuitUnsavedDialog* dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppQuitUnsavedDialog>();
            if (dialog)
            {
                *isShown = true;
                onShown();
                timer->stop();
                timer->deleteLater();
                dialog->reject();
            }
        });
        timer->start();
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

    // The close of the tab: Keep refuses it
    bool isAsked = false;
    H::answerWhenShown(shell, tool.keepText, &isAsked);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(isAsked);
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QVERIFY(fixture.window->findDocumentTab(tab->getDocumentId()));
    QVERIFY(tab->getView()->isToolModeActive());

    // The quit of the window: the question per tab, Keep keeps the window
    isAsked = false;
    H::answerWhenShown(shell, tool.keepText, &isAsked);
    fixture.window->close();
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(fixture.window->isVisible());
    QVERIFY(shell);

    // Another route: Keep stays in the tool
    isAsked = false;
    H::answerWhenShown(shell, tool.keepText, &isAsked);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(shell);
    QVERIFY(shell->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());

    // Another route with the closing answer: the tool goes, the route is shown
    isAsked = false;
    H::answerWhenShown(shell, tool.closeText, &isAsked);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
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
    bool isAsked = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&]()
    {
        QDialog* dialog = fixture.window ? fixture.window->findChild<QDialog*>(QStringLiteral("toolTabCloseDialog")) : nullptr;
        QPushButton* cancel = dialog && dialog->isVisible() ? dialog->findChild<QPushButton*>(QStringLiteral("cancelButton")) : nullptr;
        if (cancel)
        {
            isAsked = true;
            timer.stop();
            cancel->click();
        }
    });
    timer.start();
    QVERIFY(!fixture.window->closeTab(tab));
    timer.stop();
    QVERIFY(isAsked);
    QVERIFY(tab);
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
    bool isDeleted = false;
    H::runWhenShown(shell, fixture.window, [&isDeleted, tabGuard]()
    {
        isDeleted = true;
        delete tabGuard.data();
    });
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isDeleted);
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
    int questions = 0;
    bool isSecondClosed = true;
    H::runWhenShown(shell, fixture.window, [&]()
    {
        questions = H::countShownQuestions();
        isSecondClosed = fixture.window->closeTab(tab);
        questions = std::max(questions, H::countShownQuestions());
        H::button(H::question(shell), tool.keepText)->click();
    });
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QCOMPARE(questions, 1);
    QVERIFY(!isSecondClosed);
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
    bool isLost = false;
    H::runWhenShown(shell, fixture.window, [&isLost, tabGuard]()
    {
        isLost = true;
        tabGuard->getDocument()->setFailed(QStringLiteral("Leave.pdf"), QStringLiteral("Damaged"));
    });
    const bool isClosed = fixture.window->closeTab(tab);
    AppToolShellFixture::settle();
    QVERIFY(isLost);
    QCOMPARE(H::countShownQuestions(), 0);
    QVERIFY(isClosed);
    QVERIFY(!tabGuard);
    QVERIFY(!shell);
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
    bool isChanged = false;
    bool isAsked = false;
    QDialog* changedQuestion = nullptr;
    H::runWhenShown(shell, fixture.window, [&]()
    {
        QDialog* question = H::question(shell);
        QCoreApplication::installTranslator(&translator);
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
        AppToolShellFixture::settle();
        QCoreApplication::removeTranslator(&translator);
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        isChanged = true;
        changedQuestion = H::question(shell) == question ? question : nullptr;
        H::answerWhenShown(shell, tool.keepText, &isAsked);
    });
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isChanged);
    QVERIFY(changedQuestion);
    QVERIFY(isAsked);
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

    int questions = 0;
    QDialog* waited = nullptr;
    H::runWhenShown(shell, fixture.window, [&]()
    {
        questions = H::countShownQuestions();
        waited = H::question(shell);
        H::button(waited, tool.keepText)->click();
    });
    QVERIFY(!fixture.window->closeTab(tab));
    AppToolShellFixture::settle();
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
    bool isAsked = false;
    H::answerWhenShown(shell, tool.closeText, &isAsked);
    QVERIFY(fixture.window->close());
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
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

    bool isFirstAsked = false;
    bool isSecondAsked = false;
    H::answerWhenShown(first, tool.closeText, &isFirstAsked);
    H::answerWhenShown(second, tool.keepText, &isSecondAsked);
    QVERIFY(!fixture.window->close());
    AppToolShellFixture::settle();
    QVERIFY(isFirstAsked);
    QVERIFY(isSecondAsked);
    QVERIFY(fixture.window->isVisible());
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

    bool isOwnTabAsked = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&]()
    {
        QDialog* dialog = fixture.window ? fixture.window->findChild<QDialog*>(QStringLiteral("toolTabCloseDialog")) : nullptr;
        QPushButton* cancel = dialog && dialog->isVisible() ? dialog->findChild<QPushButton*>(QStringLiteral("cancelButton")) : nullptr;
        if (cancel)
        {
            isOwnTabAsked = true;
            cancel->click();
        }
    });
    timer.start();
    bool isToolAsked = false;
    H::answerWhenShown(shell, tool.keepText, &isToolAsked);
    QVERIFY(!fixture.window->close());
    AppToolShellFixture::settle();
    timer.stop();
    QVERIFY(isToolAsked);
    QVERIFY(!isOwnTabAsked);
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

    bool isAsked = false;
    H::answerWhenShown(shell, tool.keepText, &isAsked);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(!tab->getDocument()->isLocked());
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(shell->getGUI()->hasUnsavedWork());

    isAsked = false;
    H::answerWhenShown(shell, tool.closeText, &isAsked);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
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
    bool isAsked = false;
    size_t annotationsAfterAnswer = 0;
    bool isLockedAfterAnswer = true;
    bool isDeferredAfterAnswer = false;
    H::runWhenShown(shell, shell, [&]()
    {
        QDialog* question = H::question(shell);
        QPushButton* remove = H::button(question, QStringLiteral("Remove New Marks"));
        if (!remove)
        {
            // No such choice: the question is closed (Keep), so the lock does not wait forever; isAsked fails
            question->reject();
            return;
        }
        isAsked = true;
        remove->click();
        annotationsAfterAnswer = H::annotationCount(document);
        isLockedAfterAnswer = document->isLocked();
        isDeferredAfterAnswer = shell && shell->hasDeferredCloseQuestionAction();
    });
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QCOMPARE(annotationsAfterAnswer, size_t(1));
    QVERIFY(!isLockedAfterAnswer);
    QVERIFY(isDeferredAfterAnswer);
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

    bool isAsked = false;
    bool isUnsavedAsked = false;
    size_t annotationsAtUnsaved = 0;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerUnsavedWhenShown(fixture.window, pdf::PDFAppDocumentChoice::Save, [&]()
    {
        isUnsavedAsked = true;
        annotationsAtUnsaved = tabGuard ? H::measureAnnotationCount(tabGuard->getDocument()) : 0;
    });
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(AppToolShellFixture::waitFor([&tabGuard]() { return !tabGuard; }, 20000));
    QVERIFY(isAsked);
    QVERIFY(isUnsavedAsked);
    QCOMPARE(annotationsAtUnsaved, size_t(1));

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

    bool isAsked = false;
    bool isUnsavedAsked = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerUnsavedWhenShown(fixture.window, pdf::PDFAppDocumentChoice::Cancel, [&isUnsavedAsked]() { isUnsavedAsked = true; });
    QVERIFY(!fixture.window->closeTab(tab));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isUnsavedAsked);
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

    bool isAsked = false;
    bool isBoxShown = false;
    bool hasTexts = false;
    size_t annotationsAtBox = 0;
    size_t measurementsAtBox = 0;
    bool isUnsavedShown = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteKeep"), [&]()
    {
        isBoxShown = true;
        annotationsAtBox = H::measureAnnotationCount(tab->getDocument());
        measurementsAtBox = gui ? gui->getMeasurements().size() : 0;
        hasTexts = gui && H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("1 measurement could not be saved as an annotation")) &&
                   H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("The value of the measurement cannot be computed. Keep Measuring keeps it as a temporary "
                                                                            "measurement. Discard and Close removes it and closes the tool."));
    });
    H::cancelQuitUnsavedWhenShown(fixture.window, &isUnsavedShown, []() { });
    QVERIFY(!fixture.window->close());
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(hasTexts);
    QCOMPARE(annotationsAtBox, size_t(1));
    QCOMPARE(measurementsAtBox, size_t(1));
    QVERIFY(!isUnsavedShown);
    QVERIFY(fixture.window->isVisible());
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

    bool isAsked = false;
    bool isBoxShown = false;
    bool isUnsavedShown = false;
    size_t annotationsAtUnsaved = 0;
    size_t measurementsAtUnsaved = 1;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteDiscard"), [&isBoxShown]() { isBoxShown = true; });
    H::cancelQuitUnsavedWhenShown(fixture.window, &isUnsavedShown, [&]()
    {
        annotationsAtUnsaved = H::measureAnnotationCount(tab->getDocument());
        measurementsAtUnsaved = gui ? gui->getMeasurements().size() : 1;
    });
    QVERIFY(!fixture.window->close());
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(isUnsavedShown);
    QCOMPARE(annotationsAtUnsaved, size_t(1));
    QCOMPARE(measurementsAtUnsaved, size_t(0));
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

    bool isAsked = false;
    bool isBoxShown = false;
    bool isUnsavedShown = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteKeep"), [&isBoxShown]() { isBoxShown = true; });
    H::answerUnsavedWhenShown(fixture.window, pdf::PDFAppDocumentChoice::Cancel, [&isUnsavedShown]() { isUnsavedShown = true; });
    QVERIFY(!fixture.window->closeTab(tab));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(!isUnsavedShown);
    QVERIFY(tabGuard);
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

    bool isAsked = false;
    bool isBoxShown = false;
    bool isUnsavedShown = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteDiscard"), [&isBoxShown]() { isBoxShown = true; });
    H::answerUnsavedWhenShown(fixture.window, pdf::PDFAppDocumentChoice::Save, [&isUnsavedShown]() { isUnsavedShown = true; });
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(AppToolShellFixture::waitFor([&tabGuard]() { return !tabGuard; }, 20000));
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(isUnsavedShown);

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

    bool isAsked = false;
    bool isBoxShown = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteKeep"), [&isBoxShown]() { isBoxShown = true; });
    fixture.window->getNavigator()->navigate(route, pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(shell && gui);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(H::measureAnnotationCount(tab->getDocument()), size_t(1));

    isAsked = false;
    isBoxShown = false;
    bool hasTitle = false;
    H::answerWhenShown(shell, QStringLiteral("Save and Close"), &isAsked);
    H::answerIncompleteWhenShown(shell, QStringLiteral("measureIncompleteDiscard"), [&]()
    {
        isBoxShown = true;
        hasTitle = gui && H::hasLabel(gui->getIncompleteQuestion(), QStringLiteral("1 measurement could not be saved as an annotation"));
    });
    fixture.window->getNavigator()->navigate(route, pdf::PDFAppNavigationMode::Push, tab);
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(isBoxShown);
    QVERIFY(hasTitle);
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

    bool isAsked = false;
    H::answerWhenShown(shell, QStringLiteral("Remove New Marks"), &isAsked);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::lockDocument()));
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(!document->isLocked());
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

    {
        LeaveTestSessionManager manager;
        manager.isInteractionAllowed = false;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::CanceledWithoutInteraction);
        QCOMPARE(manager.cancelCount, 1);
        QCOMPARE(H::countShownQuestions(), 0);
    }

    auto answerQuit = [&fixture](pdf::PDFAppDocumentChoice choice, int* rows)
    {
        QTimer* timer = new QTimer(fixture.window);
        timer->setInterval(10);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, choice, rows]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                pdf::PDFAppQuitUnsavedDialog* dialog = qobject_cast<pdf::PDFAppQuitUnsavedDialog*>(widget);
                for (pdf::PDFAppQuitUnsavedDialog* child : widget->findChildren<pdf::PDFAppQuitUnsavedDialog*>())
                {
                    dialog = dialog ? dialog : child;
                }
                if (dialog && dialog->isVisible())
                {
                    *rows = dialog->getList()->getItemCount();
                    timer->stop();
                    timer->deleteLater();
                    dialog->getChoiceButton(choice)->click();
                    return;
                }
            }
        });
        timer->start();
    };

    {
        int rows = 0;
        answerQuit(pdf::PDFAppDocumentChoice::Cancel, &rows);
        LeaveTestSessionManager manager;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::Canceled);
        QCOMPARE(rows, 1);
        QCOMPARE(manager.cancelCount, 1);
        QVERIFY(shell && shell->getGUI()->hasUnsavedWork());
    }

    {
        int rows = 0;
        answerQuit(pdf::PDFAppDocumentChoice::Discard, &rows);
        LeaveTestSessionManager manager;
        QCOMPARE(pdf::PDFAppSessionEnd::commitData(&manager), pdf::PDFAppSessionEnd::Outcome::Discarded);
        QCOMPARE(rows, 1);
        QCOMPARE(manager.releaseCount, 1);
        QVERIFY(pdf::PDFAppSessionEnd::isToolWorkDiscarded(tab->getPage()));

        // The window, which closes then, asks the tool no second time (no nested question at the logoff)
        bool isAskedAgain = false;
        H::answerWhenShown(shell, tool.keepText, &isAskedAgain);
        QVERIFY(fixture.window->close());
        AppToolShellFixture::settle();
        QVERIFY(!isAskedAgain);
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

    bool isInsideTrigger = false;
    bool isAskedInside = false;
    bool isAsked = false;
    H::runWhenShown(shell, fixture.window, [&]()
    {
        isAsked = true;
        isAskedInside = isInsideTrigger;
        H::button(H::question(shell), tool.keepText)->click();
    });
    isInsideTrigger = true;
    insert->trigger();
    isInsideTrigger = false;
    QVERIFY(!isAsked);
    AppToolShellFixture::settle();
    QVERIFY(isAsked);
    QVERIFY(!isAskedInside);
    QVERIFY(shell);
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    delete menu.data();
}

QTEST_MAIN(AppToolLeaveTest)

#include "tst_apptoolleavetest.moc"
