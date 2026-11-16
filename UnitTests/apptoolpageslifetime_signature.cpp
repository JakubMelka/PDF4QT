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

// The lifetime scenarios of Sign with Certificate, Add Timestamp, Sign by Hand and the drawing (step 6
// of phase 2, package P4; plan 9): protectsign (the password and its live check job, a dot drawn by
// its creation tool on the page, the question 434:7495 and its answer, the run), protecttimestamp (the
// address of the authority, an invalid one, the run to a local address - no internet) and
// protectsignbyhand (the elements by the tools of the scene, the style, Clear All and its question,
// the run). The certificates are created at run time in temporary certificate folders of the test.

#include "apptestcertificatefolders.h"
#include "apptoolpageslifetime.h"

#include "pdfappdocumentview.h"
#include "pdfapptooldrawingpart.h"
#include "pdfapptoolgui_signature.h"
#include "pdfapptoolgui_signbyhand.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltimestampauthoritypart.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

#include <memory>

/// The certificate folders of the scenarios of P4 (a fresh .pfx file for each run, removed with the
/// application object)
class SignatureLifetimeFolders
{
public:
    static void cleanup()
    {
        instance().m_folders.reset();
    }

    static SignatureLifetimeFolders& instance()
    {
        static SignatureLifetimeFolders folders;
        return folders;
    }

    void prepare()
    {
        if (!m_isCleanupAdded)
        {
            m_isCleanupAdded = true;
            qAddPostRoutine(&SignatureLifetimeFolders::cleanup);
        }
        m_folders.reset();
        m_folders = std::make_unique<pdftest::AppTestCertificateFolders>();
        pdftest::AppTestCertificateFolders::create(m_folders->getApplicationFolder(), QStringLiteral("signer.pfx"), QStringLiteral("Lifetime Signer"), getPassword(), 2048);
    }

    static QString getPassword() { return QStringLiteral("lifetime-sign"); }

private:
    SignatureLifetimeFolders() = default;

    std::unique_ptr<pdftest::AppTestCertificateFolders> m_folders;
    bool m_isCleanupAdded = false;
};

/// Helpers of the scenarios of package P4
class SignatureLifetimeHelper
{
public:
    SignatureLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    static pdf::PDFAppDocumentTab* openDocument(pdftest::AppToolShellFixture& fixture)
    {
        SignatureLifetimeFolders::instance().prepare();
        return fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("signature.pdf"), 3));
    }

    static pdf::PDFAppToolGUISignature* signatureGui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUISignature*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFAppToolGUISignByHand* handGui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUISignByHand*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFAppToolDrawingPart* drawingPart(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QObject* gui = shell ? shell->getGUI() : nullptr;
        return gui ? gui->findChild<pdf::PDFAppToolDrawingPart*>() : nullptr;
    }

    /// The password typed as the user does, then the live check (its job) waited for
    static bool typePassword(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUISignature* gui = signatureGui(run);
        QPointer<pdf::PDFLineEdit> field = (gui && gui->getPasswordCard()) ? gui->getPasswordCard()->getPasswordField() : nullptr;
        if (!field)
        {
            return false;
        }
        field->setFocus();
        for (const QChar character : SignatureLifetimeFolders::getPassword())
        {
            if (!field)
            {
                return true;
            }
            QTest::keyClick(field.data(), character.toLatin1());
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUISignature* current = signatureGui(run); return !current || current->getPasswordState() != pdf::PDFAppToolGUISignature::PasswordState::NotChecked; });
        return true;
    }

    /// A dot by its creation tool: the button (Sign by Hand) or the menu Draw (Sign), a click on page 1
    static bool drawDot(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolDrawingPart* part = drawingPart(run);
        if (!part)
        {
            return false;
        }
        if (QToolButton* button = part->getElementButton(pdf::PDFAppToolDrawingPart::ElementKind::Dot))
        {
            Steps::click(button);
        }
        else
        {
            part->startElement(pdf::PDFAppToolDrawingPart::ElementKind::Dot);
        }
        Steps::settle();
        if (!run.tab || !run.tab->getView())
        {
            return true;
        }
        // The snapper of the pick tool follows the moves: a move over the point, then the click
        bool ok = false;
        const QPoint point = pdftest::AppToolShellFixture::pageToWidget(run.tab->getView(), 0, QPointF(200, 300), &ok);
        QPointer<QWidget> canvas = pdftest::AppToolShellFixture::canvasWidget(run.tab->getView());
        if (ok && canvas)
        {
            QMouseEvent move(QEvent::MouseMove, QPointF(point), canvas->mapToGlobal(QPointF(point)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas.data(), &move);
        }
        if (run.tab && run.tab->getView())
        {
            pdftest::AppToolShellFixture::clickPage(run.tab->getView(), 0, QPointF(200, 300));
        }
        Steps::settle();
        return true;
    }

    /// The style of the selected dot: a color of the swatches
    static bool changeStyle(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolDrawingPart* part = drawingPart(run);
        if (!part)
        {
            return true;
        }
        const std::vector<pdf::PDFInteger> ids = part->getElementIds();
        if (!ids.empty())
        {
            part->selectElements({ ids.front() });
            Steps::settle();
        }
        part = drawingPart(run);
        pdf::PDFAppToolDrawingStylePanel* panel = part ? part->getStylePanel() : nullptr;
        pdf::PDFColorSwatchRow* row = panel ? panel->getPenColorRow() : nullptr;
        if (row && row->getSwatchCount() > 2)
        {
            Steps::click(row->getSwatch(2));
        }
        Steps::settle();
        return true;
    }

    /// Clear All: its question answered by Keep Drawing (Sign by Hand), then by Clear All
    static bool clearAll(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolDrawingPart* part = drawingPart(run);
        if (!part || !part->getClearButton())
        {
            return true;
        }
        Steps::click(part->getClearButton());
        Steps::settle();
        pdf::PDFAppToolGUISignByHand* gui = handGui(run);
        QPointer<pdf::PDFMessageBox> dialog = gui ? gui->getClearDialog() : nullptr;
        if (dialog && dialog->getActionButtons().size() == 2)
        {
            Steps::click(dialog->getActionButtons()[0]);
        }
        Steps::settle();
        return true;
    }

    /// Cancel with a drawing asks "Discard the drawing?" (435:10500): Discard Drawing ends the tool
    static bool answerDiscard(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUISignByHand* gui = handGui(run);
        QPointer<pdf::PDFMessageBox> dialog = gui ? gui->getDiscardDialog() : nullptr;
        if (!dialog || !dialog->isVisible() || dialog->getActionButtons().size() != 2)
        {
            return false;
        }
        Steps::click(dialog->getActionButtons()[1]);
        Steps::settle();
        return true;
    }

    /// The Primary button: the question of a visible signature without graphics (434:7495) or the
    /// run; Back of the question
    static bool confirmWithQuestion(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (!shell)
        {
            return true;
        }
        shell->confirm();
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUISignature* gui = signatureGui(run); return !gui || gui->getQuestionDialog() || (Steps::shell(run) && Steps::shell(run)->getState() != pdf::PDFDocumentToolShell::State::Editing); });
        pdf::PDFAppToolGUISignature* gui = signatureGui(run);
        QPointer<pdf::PDFMessageBox> dialog = gui ? gui->getQuestionDialog() : nullptr;
        if (dialog && !dialog->getActionButtons().isEmpty())
        {
            Steps::click(dialog->getActionButtons().front());
        }
        Steps::settle();
        return true;
    }

    /// The address of the authority: an invalid one (435:7693), then a local one (no internet in a
    /// test: a refused connection on the loopback)
    static bool editAuthority(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUISignature* gui = signatureGui(run);
        pdf::PDFAppToolTimestampAuthorityPart* authority = gui ? gui->getAuthorityPart() : nullptr;
        QPointer<pdf::PDFComboBox> combo = authority ? authority->getCombo() : nullptr;
        if (!combo || !combo->lineEdit())
        {
            return false;
        }
        combo->lineEdit()->selectAll();
        QTest::keyClicks(combo->lineEdit(), QStringLiteral("tsa.acme"));
        Steps::settle();
        if (combo && combo->lineEdit())
        {
            combo->lineEdit()->selectAll();
            QTest::keyClicks(combo->lineEdit(), QStringLiteral("http://127.0.0.1:9/tsr"));
        }
        Steps::settle();
        return true;
    }

    /// The Primary button: the run (a timestamp of a local address fails, its dialog is closed; Sign
    /// by Hand runs to its result dialog with the core of P2)
    static bool run(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (!shell)
        {
            return true;
        }
        shell->confirm();
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getState() != pdf::PDFDocumentToolShell::State::Running; });
        pdf::PDFAppToolGUISignature* gui = signatureGui(run);
        if (QPointer<pdf::PDFMessageBox> dialog = gui ? gui->getTimestampErrorDialog() : nullptr)
        {
            dialog->reject();
        }
        pdf::PDFDocumentToolShell* current = Steps::shell(run);
        if (current && current->getResultDialog())
        {
            current->getResultDialog()->reject();
        }
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerSignatureScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = SignatureLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // Sign with Certificate: the password and its live check, the question of a visible signature
    // without graphics (Back), a dot by the menu Draw, the run
    ToolPagesLifetimeScenario sign;
    sign.name = QStringLiteral("protectsign");
    sign.toolId = QStringLiteral("protect.sign");
    sign.openDocument = &H::openDocument;
    sign.hasPanel = true;
    sign.prepareRun = &H::typePassword;
    sign.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::typePassword);
        steps->push_back(&H::confirmWithQuestion);
        steps->push_back(&H::drawDot);
        steps->push_back(&H::changeStyle);
        steps->push_back(&H::run);
    };
    scenarios->push_back(sign);

    // Add Timestamp: the address of the authority, the run to a local address and its error dialog
    ToolPagesLifetimeScenario timestamp;
    timestamp.name = QStringLiteral("protecttimestamp");
    timestamp.toolId = QStringLiteral("protect.timestamp");
    timestamp.openDocument = &H::openDocument;
    timestamp.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::editAuthority);
        steps->push_back(&H::run);
    };
    scenarios->push_back(timestamp);

    // Sign by Hand: a dot by its button, the style, Clear All and its question, the run
    ToolPagesLifetimeScenario hand;
    hand.name = QStringLiteral("protectsignbyhand");
    hand.toolId = QStringLiteral("protect.sign-by-hand");
    hand.openDocument = &H::openDocument;
    hand.hasPanel = true;
    hand.prepareRun = &H::drawDot;
    hand.answerCloseQuestion = &H::answerDiscard;
    hand.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::drawDot);
        steps->push_back(&H::changeStyle);
        steps->push_back(&H::clearAll);
        steps->push_back(&H::run);
    };
    scenarios->push_back(hand);
}
