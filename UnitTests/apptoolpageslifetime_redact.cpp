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

// The lifetime scenarios of Redact Content, Manage Certificates and the job of the creation of a key
// (step 6 of phase 2, package P5; plan 9): protectredact, protectcertificates, certificatecreatejob. The
// file has two sections: Redact Content (RedactLifetimeScenarios) and Manage Certificates with the job of
// the creation of a key (CertificatesLifetimeScenarios); each one adds its rows.

#include "apptestcertificatefolders.h"
#include "apptestcertificates.h"
#include "apptoolpageslifetime.h"

#include "pdfappcertificatedialogs.h"
#include "pdfappcommand.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapptoolgui_certificates.h"
#include "pdftrustedcertificaterepository.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolgui_redact.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolredactsearchpart.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <atomic>
#include <memory>

// ------------------------------------------------------------------ section Redact Content

/// The steps of the scenario protectredact: every input through the guards of the test (the victim may
/// be deleted inside of any of them); a step, whose part is gone, ends without a failure
class RedactLifetimeHelper
{
public:
    RedactLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;
    using GUI = pdf::PDFAppToolGUIRedact;

    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    /// A mark of an area (the pick of the mode Area)
    static bool markArea(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            current->addAreaMark(0, QRectF(100, 500, 80, 60));
        }
        Steps::settle();
        return true;
    }

    /// The mode Area by its button, a rectangle dragged on the canvas, the mode ended
    static bool areaByMouse(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getAreaButton()))
        {
            return true;
        }
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(100, 700), QPointF(200, 640));
        }
        Steps::settle();
        if (GUI* again = gui(run))
        {
            Steps::click(again->getAreaButton());
        }
        return true;
    }

    /// Search…: "alpha" typed and searched, Mark N Selected Matches, Back to Marked Areas
    static bool searchAndMark(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getSearchButton()))
        {
            return true;
        }

        current = gui(run);
        QPointer<pdf::PDFAppToolRedactSearchPart> part = current ? current->getSearchPart() : nullptr;
        QPointer<pdf::PDFLineEdit> field = part ? part->getField() : nullptr;
        if (!field)
        {
            return true;
        }
        for (const QChar character : QStringLiteral("alpha"))
        {
            if (!field)
            {
                return true;
            }
            QTest::keyClick(field.data(), character.toLatin1());
        }
        Steps::key(field, Qt::Key_Return);
        Steps::wait(run, [part]() { return !part || part->getState() == pdf::PDFAppToolRedactSearchPart::State::Finished || part->getState() == pdf::PDFAppToolRedactSearchPart::State::Failed; });
        if (part)
        {
            Steps::click(part->getMarkButton());
        }
        if (part)
        {
            Steps::click(part->getBackButton());
        }
        Steps::settle();
        return true;
    }

    /// Marks | Preview result (the right panel)
    static bool preview(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        pdf::PDFSegmentedControl* control = current ? current->getModeControl() : nullptr;
        const QList<QToolButton*> segments = control ? control->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly) : QList<QToolButton*>();
        if (segments.size() < 2)
        {
            return true;
        }
        QPointer<QToolButton> marks = segments[0];
        Steps::click(segments[1]);
        Steps::settle();
        Steps::click(marks);
        return true;
    }

    /// The menu Fill: White, Custom color… and a swatch
    static bool fillMenu(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getFillButton()))
        {
            return true;
        }
        Steps::settle();
        if (GUI* again = gui(run))
        {
            Steps::click(again->getFillRow(GUI::Fill::White));
        }
        if (GUI* again = gui(run); again && Steps::click(again->getFillButton()))
        {
            Steps::settle();
        }
        if (GUI* again = gui(run))
        {
            Steps::click(again->getFillRow(GUI::Fill::Custom));
        }
        if (GUI* again = gui(run); again && again->getFillPanel() && again->getFillPanel()->getColorRow())
        {
            Steps::click(again->getFillPanel()->getColorRow()->getSwatch(0));
        }
        Steps::settle();
        return true;
    }

    /// The menu Keep: the outline
    static bool keepMenu(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getKeepButton()))
        {
            return true;
        }
        Steps::settle();
        GUI* again = gui(run);
        if (again && again->getKeepMenu() && again->getKeepMenu()->getPanel() && again->getKeepMenu()->getPanel()->getRows().size() > 2)
        {
            Steps::click(again->getKeepMenu()->getPanel()->getRows()[2]);
        }
        if (GUI* last = gui(run); last && last->getKeepMenu() && last->getKeepMenu()->getPanel())
        {
            last->getKeepMenu()->getPanel()->close();
        }
        Steps::settle();
        return true;
    }

    /// Pages…: the dialog, Mark 1 Page
    static bool pagesDialog(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getPagesButton()))
        {
            return true;
        }
        Steps::settle();
        if (GUI* again = gui(run))
        {
            Steps::click(again->getPagesMarkButton());
        }
        Steps::settle();
        return true;
    }

    /// The × of the first mark (its action), Delete
    static bool removeMark(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (current && !current->getMarks().empty())
        {
            current->removeMark(current->getMarks().front().annotation);
        }
        Steps::settle();
        return true;
    }

    /// The Primary button: the run (the result dialog, when the core gives one, closed without a file)
    static bool runTool(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* toolbar = Steps::toolbar(run);
        if (!toolbar || !Steps::click(toolbar->getConfirmButton()))
        {
            return true;
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* shell = Steps::shell(run); return !shell || shell->getState() != pdf::PDFDocumentToolShell::State::Running; });
        if (pdf::PDFDocumentToolShell* shell = Steps::shell(run))
        {
            if (pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog())
            {
                dialog->reject();
            }
        }
        Steps::settle();
        return true;
    }

    /// Clear All: the question, Remove All Marks
    static bool clearAll(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getClearButton()))
        {
            return true;
        }
        Steps::settle();
        GUI* again = gui(run);
        QPointer<pdf::PDFMessageBox> dialog = again ? again->getClearDialog() : nullptr;
        if (dialog && !dialog->getActionButtons().isEmpty())
        {
            Steps::click(dialog->getActionButtons().back());
        }
        Steps::settle();
        return true;
    }

    /// Owner 2026-11-17: a mark selected on the canvas, a drag of the mark left in progress (the victim
    /// comes during it), a drag of its handle, the arrows, Delete on the canvas
    static bool canvasEdit(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->addAreaMark(0, QRectF(320, 500, 80, 60));
        current = gui(run);
        if (!current || current->getMarks().empty())
        {
            return true;
        }
        current->selectMark(current->getMarks().back().annotation);
        Steps::settle();

        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        QPointer<QWidget> canvas = tab && tab->getView() ? pdftest::AppDocumentFixture::canvasWidget(tab->getView()) : nullptr;
        bool ok = false;
        const QPoint from = tab && tab->getView() ? pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 0, QPointF(360, 530), &ok) : QPoint();
        if (canvas && ok)
        {
            // The press and a move: the drag is in progress, while the next events run
            QMouseEvent press(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &press);
            const QPointF to = QPointF(from) + QPointF(30, 20);
            if (canvas)
            {
                QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(canvas, &move);
            }
            Steps::settle();
            if (canvas)
            {
                QMouseEvent release(QEvent::MouseButtonRelease, to, canvas->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(canvas, &release);
            }
        }
        Steps::settle();
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::dragPage(tab->getView(), 0, QPointF(400, 500), QPointF(420, 480));
        }
        Steps::settle();
        if (canvas)
        {
            Steps::key(canvas, Qt::Key_Right);
        }
        if (canvas)
        {
            Steps::key(canvas, Qt::Key_Delete);
        }
        Steps::settle();
        return true;
    }

    /// Owner 2026-11-18: Ctrl + drag of a selected Area mark copies it - the victim comes during the
    /// Ctrl + drag (the button held) and while its copy is the selected mark
    static bool canvasCopy(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->addAreaMark(0, QRectF(320, 420, 80, 60));
        current = gui(run);
        if (!current || current->getMarks().empty())
        {
            return true;
        }
        current->selectMark(current->getMarks().back().annotation);
        Steps::settle();

        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        QPointer<QWidget> canvas = tab && tab->getView() ? pdftest::AppDocumentFixture::canvasWidget(tab->getView()) : nullptr;
        bool ok = false;
        const QPoint from = tab && tab->getView() ? pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 0, QPointF(360, 450), &ok) : QPoint();
        if (canvas && ok)
        {
            // The press and a move with Ctrl: the copy is dragged, while the next events run
            QMouseEvent press(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(canvas, &press);
            const QPointF to = QPointF(from) + QPointF(40, 30);
            if (canvas)
            {
                QMouseEvent move(QEvent::MouseMove, to, canvas->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
                QCoreApplication::sendEvent(canvas, &move);
            }
            Steps::settle();
            if (canvas)
            {
                QMouseEvent release(QEvent::MouseButtonRelease, to, canvas->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
                QCoreApplication::sendEvent(canvas, &release);
            }
        }

        // The copy is the selected mark, while the next events run
        Steps::settle();
        return true;
    }

    /// Owner 2026-11-17: Esc during a drag of Area, Esc of the mode, Esc of the tool (its question)
    static bool escapes(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getAreaButton()))
        {
            return true;
        }
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        QPointer<QWidget> canvas = tab && tab->getView() ? pdftest::AppDocumentFixture::canvasWidget(tab->getView()) : nullptr;
        bool ok = false;
        const QPoint from = tab && tab->getView() ? pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 0, QPointF(120, 400), &ok) : QPoint();
        if (canvas && ok)
        {
            QMouseEvent press(QEvent::MouseButtonPress, from, canvas->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &press);
        }
        for (int i = 0; i < 3; ++i)
        {
            // The shortcut of the shell (its GUI first)
            GUI* again = gui(run);
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            if (!again || !shell)
            {
                break;
            }
            if (QAction* escape = shell->findChild<QAction*>(QStringLiteral("toolEscape")))
            {
                escape->trigger();
            }
            Steps::settle();
        }
        if (canvas && ok)
        {
            QMouseEvent release(QEvent::MouseButtonRelease, from, canvas->mapToGlobal(from), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &release);
        }
        Steps::settle();

        // The question of the close (with marks), Back to Redacting
        GUI* last = gui(run);
        QPointer<pdf::PDFMessageBox> prompt = last ? last->getClosePrompt() : nullptr;
        if (prompt && !prompt->getActionButtons().isEmpty())
        {
            Steps::click(prompt->getActionButtons().front());
        }
        Steps::settle();
        return true;
    }

    /// Owner 2026-11-18: Esc with a selected mark deselects it (canvas and list), the next Esc asks the
    /// question of the close; Back to Redacting
    static bool escapeSelection(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->addAreaMark(0, QRectF(200, 300, 60, 40));
        current = gui(run);
        if (!current || current->getMarks().empty())
        {
            return true;
        }
        current->selectMark(current->getMarks().front().annotation);
        Steps::settle();
        for (int i = 0; i < 2; ++i)
        {
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            if (!gui(run) || !shell)
            {
                break;
            }
            if (QAction* escape = shell->findChild<QAction*>(QStringLiteral("toolEscape")))
            {
                escape->trigger();
            }
            Steps::settle();
        }

        GUI* last = gui(run);
        QPointer<pdf::PDFMessageBox> prompt = last ? last->getClosePrompt() : nullptr;
        if (prompt && !prompt->getActionButtons().isEmpty())
        {
            Steps::click(prompt->getActionButtons().front());
        }
        Steps::settle();
        return true;
    }

    /// The question at the close (448:17473): Back to Redacting, then Keep Marks and Close
    static bool closePrompt(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->addAreaMark(0, QRectF(300, 500, 60, 60));
        current = gui(run);
        QPointer<pdf::PDFMessageBox> prompt = current ? current->openClosePrompt() : nullptr;
        if (prompt && !prompt->getActionButtons().isEmpty())
        {
            Steps::click(prompt->getActionButtons().front());
        }
        Steps::settle();

        // Esc of the shell asks the question (PDFDocumentToolGUI::askClose), Keep Marks and Close
        current = gui(run);
        if (current && current->getShell())
        {
            current->getShell()->cancel();
        }
        Steps::settle();
        current = gui(run);
        prompt = current ? current->getClosePrompt() : nullptr;
        if (prompt && prompt->getActionButtons().size() > 1)
        {
            Steps::click(prompt->getActionButtons()[1]);
        }
        Steps::settle();
        return true;
    }

    /// Lock document with a new mark (owner 2026-11-20, decisions 6 and 8): the question waits in the loop of the
    /// leave, Remove New Marks is remembered, the end of the leave removes the mark while the tool edits, then the
    /// document is locked and the tool ends; the document is unlocked afterwards
    static bool lockRemoveNewMarks(ToolPagesLifetimeRun& run)
    {
        if (!gui(run) && !Steps::openTool(run))
        {
            return true;
        }
        markArea(run);
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        if (!gui(run) || !tab)
        {
            return true;
        }
        QPointer<QTimer> answer = Steps::answerToolQuestionLater(run, QStringLiteral("redactCloseRemove"));
        tab->executeCommand(pdf::PDFAppCommands::lockDocument());
        delete answer.data();
        Steps::settle();
        if (pdf::PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
        {
            document->setLocked(false);
        }
        Steps::settle();
        return true;
    }
};

/// The scenario protectredact
class RedactLifetimeScenarios
{
public:
    RedactLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = RedactLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        // Redact Content: the marks (Area by the API and by the mouse, the search, Pages…), the
        // preview, the menus, the removal of a mark, the run, Clear All and the question at the close
        ToolPagesLifetimeScenario redact;
        redact.name = QStringLiteral("protectredact");
        redact.toolId = QStringLiteral("protect.redact");
        redact.hasPanel = true;
        redact.prepareRun = &H::markArea;
        redact.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::markArea);
            steps->push_back(&H::areaByMouse);
            steps->push_back(&H::searchAndMark);
            steps->push_back(&H::preview);
            steps->push_back(&H::fillMenu);
            steps->push_back(&H::keepMenu);
            steps->push_back(&H::pagesDialog);
            steps->push_back(&H::removeMark);
            steps->push_back(&H::canvasEdit);
            steps->push_back(&H::canvasCopy);
            steps->push_back(&H::escapes);
            steps->push_back(&H::escapeSelection);
            steps->push_back(&H::runTool);
            steps->push_back(&H::clearAll);
            steps->push_back(&H::closePrompt);
            steps->push_back(&H::lockRemoveNewMarks);
        };
        scenarios->push_back(redact);
    }
};

// ------------------------------------------------------------------ section Manage Certificates

/// The certificate folders of the scenarios (a temporary folder of the test, never a folder of the
/// user), a .pfx file of the folder of the application and one of an old application, a trusted
/// certificate; the generator of the job of the creation, which waits until it is released or
/// canceled (PDFAppCertificateCreateJob::setGenerator). Given back when the application object goes.
class CertificatesLifetimeHelper
{
public:
    CertificatesLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;
    using GUI = pdf::PDFAppToolGUICertificates;

    static QString toolId() { return QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID); }

    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    /// Fresh folders with two files, the generator of the jobs
    static void prepare()
    {
        if (!s_isCleanupAdded)
        {
            s_isCleanupAdded = true;
            qAddPostRoutine(&CertificatesLifetimeHelper::cleanup);
        }
        s_folders.reset();
        s_folders = std::make_unique<pdftest::AppTestCertificateFolders>();
        pdftest::AppTestCertificateFolders::create(s_folders->getApplicationFolder(), QStringLiteral("own.pfx"), QStringLiteral("Own Holder"), QString(), 2048);
        pdftest::AppTestCertificateFolders::create(s_folders->getOldFolders().front(), QStringLiteral("editor.pfx"), QStringLiteral("Editor Holder"), QString(), 2048);
        s_isReleased = false;
        pdf::PDFAppCertificateCreateJob::setGenerator(&CertificatesLifetimeHelper::generate);
    }

    static void cleanup()
    {
        s_isReleased = true;
        pdf::PDFAppCertificateCreateJob::setGenerator(nullptr);
        s_folders.reset();
    }

    /// The generator: waits for the release or the cancellation, then the core
    static QByteArray generate(const pdf::PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString* errorMessage)
    {
        ++s_running;
        QElapsedTimer timer;
        timer.start();
        while (!s_isReleased && !(cancelFlag && cancelFlag->load()) && timer.elapsed() < 30000)
        {
            QThread::msleep(2);
        }
        QByteArray result = pdf::PDFSignatureFactory::createCertificate(info, cancelFlag, errorMessage);
        --s_running;
        return result;
    }

    /// Waits until no job runs (the jobs of a step end, also when the dialog is gone)
    static void waitJobs(const ToolPagesLifetimeRun& run)
    {
        // The window or the tab may be gone (the victim of the site): the jobs are awaited anyway
        Steps::wait(run, []() { return s_running.load() == 0; });
        const bool isEnded = QTest::qWaitFor([]() { return s_running.load() == 0; }, 30000);
        Q_UNUSED(isEnded);
        Steps::settle();
    }

    /// Opens the own tab of Manage Certificates (a trusted certificate in the database)
    static bool openTab(ToolPagesLifetimeRun& run)
    {
        if (!run.window || !run.fixture)
        {
            return false;
        }
        prepare();
        pdf::PDFTrustedCertificateRepository(run.fixture->context->getDatabase()).addCertificate(pdftest::AppTestCertificates::first());
        run.toolTab = run.window->createToolTab(toolId(), nullptr);
        Steps::settle();
        return run.toolTab && Steps::wait(run, [&run]() { GUI* current = gui(run); return current && !current->isLoadingSystemCertificates(); });
    }

    /// A click on the second row of My certificates
    static bool chooseRow(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (current && current->getCertificates().size() > 1)
        {
            Steps::click(current->getCertificateRow(current->getCertificates()[1].filePath));
        }
        if (GUI* again = gui(run); again && !again->getCertificates().empty())
        {
            Steps::click(again->getCertificateRow(again->getCertificates()[0].filePath));
        }
        Steps::settle();
        return true;
    }

    /// Delete: the confirmation, \p button 0 = Cancel, 1 = Delete Certificate
    static bool deleteCertificate(ToolPagesLifetimeRun& run, int button)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        for (const pdf::PDFAppToolCertificateItem& item : current->getCertificates())
        {
            if (item.source == pdf::PDFAppToolCertificateItem::Source::PfxFile && !item.isInOldFolder)
            {
                Steps::click(current->getCertificateRow(item.filePath));
                break;
            }
        }
        current = gui(run);
        if (!current || !Steps::click(current->getDeleteButton()))
        {
            return true;
        }
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFMessageBox> dialog = current ? current->getDeleteDialog() : nullptr;
        if (dialog && dialog->getActionButtons().size() > button)
        {
            Steps::click(dialog->getActionButtons()[button]);
        }
        Steps::settle();
        return true;
    }

    /// The view of the trusted certificates and back
    static bool switchViews(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            Steps::click(current->getViewRow(GUI::View::Trusted));
        }
        Steps::settle();
        return true;
    }

    /// Add Trusted…: a PEM file; the duplicate - its message, Choose Another File… with a text file,
    /// Cancel
    static bool addTrusted(ToolPagesLifetimeRun& run)
    {
        const QString folder = run.fixture ? run.fixture->folder() : QString();
        const QString pem = QDir(folder).absoluteFilePath(QStringLiteral("lifetime.pem"));
        const QString text = QDir(folder).absoluteFilePath(QStringLiteral("lifetime.crt"));
        QFile pemFile(pem);
        QFile textFile(text);
        if (!pemFile.open(QIODevice::WriteOnly) || !textFile.open(QIODevice::WriteOnly))
        {
            return false;
        }
        pemFile.write("-----BEGIN CERTIFICATE-----\n" + pdftest::AppTestCertificates::second().toBase64() + "\n-----END CERTIFICATE-----\n");
        textFile.write("no certificate");
        pemFile.close();
        textFile.close();

        static QString s_path;
        s_path = pem;
        pdf::PDFAppPanelFileDialogs::setOpenHandler([](QWidget*, const QString&, const QString&, const QString&) { return s_path; });
        if (GUI* current = gui(run))
        {
            Steps::click(current->getAddTrustedButton());
        }
        Steps::settle();
        if (GUI* current = gui(run))
        {
            Steps::click(current->getAddTrustedButton());
        }
        Steps::settle();
        GUI* current = gui(run);
        QPointer<pdf::PDFMessageBox> dialog = current ? current->getAddTrustedDialog() : nullptr;
        if (dialog && !dialog->getActionButtons().isEmpty())
        {
            s_path = text;
            Steps::click(dialog->getActionButtons().back());
        }
        Steps::settle();
        current = gui(run);
        dialog = current ? current->getAddTrustedDialog() : nullptr;
        if (dialog && !dialog->getActionButtons().isEmpty())
        {
            Steps::click(dialog->getActionButtons().front());
        }
        pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
        Steps::settle();
        return true;
    }

    /// Remove: Stop trusting …? - Remove
    static bool removeTrusted(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->chooseTrustedRow(0);
        if (!Steps::click(current->getRemoveTrustedButton()))
        {
            return true;
        }
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFMessageBox> dialog = current ? current->getRemoveTrustedDialog() : nullptr;
        if (dialog && dialog->getActionButtons().size() > 1)
        {
            Steps::click(dialog->getActionButtons()[1]);
        }
        Steps::settle();
        return true;
    }

    /// The view My certificates, Create…: the form filled (the dialog of the GUI, nullptr = none)
    static pdf::PDFAppCertificateCreateDialog* openCreateDialog(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (current)
        {
            Steps::click(current->getViewRow(GUI::View::My));
        }
        current = gui(run);
        if (!current || !Steps::click(current->getCreateButton()))
        {
            return nullptr;
        }
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFAppCertificateCreateDialog> dialog = current ? current->getCreateDialog() : nullptr;
        for (const auto& [field, text] : { std::pair{ 0, QStringLiteral("Lifetime Holder") }, std::pair{ 1, QStringLiteral("holder@example.com") }, std::pair{ 2, QStringLiteral("Acme Corp") } })
        {
            if (!dialog)
            {
                return nullptr;
            }
            QTest::keyClicks(field == 0 ? dialog->getNameField() : field == 1 ? dialog->getEmailField() : dialog->getOrganizationField(), text);
        }
        return dialog.data();
    }

    /// Create…: the job started, then canceled by Cancel
    static bool createAndCancel(ToolPagesLifetimeRun& run)
    {
        s_isReleased = false;
        QPointer<pdf::PDFAppCertificateCreateDialog> dialog = openCreateDialog(run);
        if (dialog && Steps::click(dialog->getCreateButton()))
        {
            Steps::settle();
        }
        if (dialog)
        {
            Steps::click(dialog->getCancelButton());
        }
        waitJobs(run);
        return true;
    }

    /// Create…: two quick starts, the job released - one certificate, the new row
    static bool createTwice(ToolPagesLifetimeRun& run)
    {
        s_isReleased = false;
        QPointer<pdf::PDFAppCertificateCreateDialog> dialog = openCreateDialog(run);
        if (dialog)
        {
            Steps::click(dialog->getCreateButton());
        }
        if (dialog)
        {
            dialog->create();
        }
        s_isReleased = true;

        // The result of the job comes from the event loop after the end of its thread: the step ends
        // only when the dialog has reported it and gone (the emission stays in this step); a dialog of a
        // window or a tab, which was deleted, is gone at once
        const bool isClosed = QTest::qWaitFor([&dialog]() { return !dialog; }, 30000);
        Q_UNUSED(isClosed);
        waitJobs(run);
        return true;
    }

    /// Create…: the job started, then the tab is closed during the generation
    static bool closeDuringJob(ToolPagesLifetimeRun& run)
    {
        s_isReleased = false;
        QPointer<pdf::PDFAppCertificateCreateDialog> dialog = openCreateDialog(run);
        if (dialog)
        {
            Steps::click(dialog->getCreateButton());
        }
        Steps::settle();
        if (run.window && run.toolTab)
        {
            run.window->closeTab(run.toolTab.data());
        }
        Steps::settle();
        waitJobs(run);
        s_isReleased = true;
        return true;
    }

private:
    static inline std::unique_ptr<pdftest::AppTestCertificateFolders> s_folders;
    static inline bool s_isCleanupAdded = false;
    static inline std::atomic<bool> s_isReleased = false;
    static inline std::atomic<int> s_running = 0;
};

/// The scenarios protectcertificates and certificatecreatejob
class CertificatesLifetimeScenarios
{
public:
    CertificatesLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = CertificatesLifetimeHelper;

        // Manage Certificates in its own tab: the rows, Delete (Cancel, then Delete Certificate), the
        // views, Add Trusted… (a file, the duplicate, Choose Another File…), Remove
        ToolPagesLifetimeScenario certificates;
        certificates.name = QStringLiteral("protectcertificates");
        certificates.toolId = H::toolId();
        certificates.isOwnTab = true;
        certificates.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&H::openTab);
            steps->push_back(&H::chooseRow);
            steps->push_back([](ToolPagesLifetimeRun& run) { return H::deleteCertificate(run, 0); });
            steps->push_back([](ToolPagesLifetimeRun& run) { return H::deleteCertificate(run, 1); });
            steps->push_back(&H::switchViews);
            steps->push_back(&H::addTrusted);
            steps->push_back(&H::removeTrusted);
        };
        scenarios->push_back(certificates);

        // The job of the creation of a key (plan 6.4): two quick starts (the certificate created), a
        // canceled job, the tab closed during the generation. The creation comes first: the recorder
        // of the test knows a hooked object by its address, a dialog at the address of a deleted
        // dialog of the same run would not always be hooked (its site certificateCreated would move);
        // the later dialogs have no site of their own (they are canceled).
        ToolPagesLifetimeScenario job;
        job.name = QStringLiteral("certificatecreatejob");
        job.toolId = H::toolId();
        job.isOwnTab = true;
        job.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&H::openTab);
            steps->push_back(&H::createTwice);
            steps->push_back(&H::createAndCancel);
            steps->push_back(&H::closeDuringJob);
        };
        scenarios->push_back(job);
    }
};

// ------------------------------------------------------------------ registration

void ToolPagesLifetimePackages::registerRedactScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    RedactLifetimeScenarios::add(scenarios);
    CertificatesLifetimeScenarios::add(scenarios);
}
