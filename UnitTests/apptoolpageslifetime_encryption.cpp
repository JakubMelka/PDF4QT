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

// The lifetime scenarios of Protect with Password, Restrict Permissions and Remove Password (step 5
// of phase 2, package P2; plan 9): protectpassword (the password cards, the check box of the owner,
// the menu Algorithm, the run), protectpermissions (the list with its dependencies, the menu Preset,
// the open password, the run), protectunlock (the owner password dialog of the entry - ChangeSecurity,
// 430:2709 - and the run), protectrestrictbutton (Open Restrict Permissions of the owner password
// dialog, Q5.12), protectattachments (Open without attachments, the locked Attachments card and the
// attachments password dialog, Q5.9 c). The run of the core may end by a result dialog or by an info
// bar. The MCP confirmation with the passwords is a scenario of UnitTestsAppMcpLifetime.

#include "apptoolpageslifetime.h"

#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentview.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_encryption.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolpermissionlist.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QPointer>
#include <QtTest>

/// Helpers of the scenarios of package P2 of step 5
class EncryptionLifetimeHelper
{
public:
    EncryptionLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    static pdf::PDFAppToolGUIEncryption* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUIEncryption*>(shell->getGUI()) : nullptr;
    }

    /// Types the text key by key (the victim may die inside of any key)
    static bool typeText(QWidget* widget, const QString& text)
    {
        QPointer<QWidget> guard(widget);
        for (const QChar character : text)
        {
            if (!guard)
            {
                return true;
            }
            QTest::keyClick(guard.data(), character.toLatin1());
        }
        Steps::settle();
        return true;
    }

    /// The password and its repetition of the open (\p isOwner false) or the owner card
    static bool typePassword(const ToolPagesLifetimeRun& run, bool isOwner, const QString& password)
    {
        pdf::PDFAppToolGUIEncryption* current = gui(run);
        pdf::PDFAppToolPasswordCard* card = current ? (isOwner ? current->getOwnerPasswordCard() : current->getOpenPasswordCard()) : nullptr;
        if (!card)
        {
            return false;
        }
        QPointer<pdf::PDFAppToolPasswordCard> guard(card);
        typeText(card->getPasswordField(), password);
        if (guard && guard->getRepeatField())
        {
            typeText(guard->getRepeatField(), password);
        }
        return true;
    }

    static bool clickCheck(QAbstractButton* button)
    {
        if (!button)
        {
            return false;
        }
        QPointer<QAbstractButton> guard(button);
        button->click();
        Steps::settle();
        return true;
    }

    static bool setValue(const ToolPagesLifetimeRun& run, const QString& name, const QVariant& value)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
        if (!model)
        {
            return false;
        }
        model->setValue(name, value);
        Steps::settle();
        return true;
    }

    /// A row of the list of the permissions (K5.8 dependencies, one undo step)
    static bool clickPermission(const ToolPagesLifetimeRun& run, const QString& parameter)
    {
        pdf::PDFAppToolGUIEncryption* current = gui(run);
        pdf::PDFTileRow* row = current && current->getPermissionList() ? current->getPermissionList()->getRow(parameter) : nullptr;
        return Steps::click(row);
    }

    /// The menu Preset by its button, then a row of its panel
    static bool choosePreset(const ToolPagesLifetimeRun& run, pdf::PDFDocumentToolEncryption::Preset preset)
    {
        pdf::PDFAppToolGUIEncryption* current = gui(run);
        pdf::PDFAppToolPanelPopup* panel = current ? current->getPresetPanel() : nullptr;
        if (!panel || !Steps::click(panel->getButton()))
        {
            return false;
        }
        current = gui(run);
        Steps::click(current ? current->getPresetRow(preset) : nullptr);
        return true;
    }

    /// Answers the owner password dialog of the entry (430:2709) by the owner password of the sample
    static bool unlockEntry(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* now = Steps::shell(run); return !now || now->getPermissionDialog(); });
        shell = Steps::shell(run);
        QPointer<pdf::PDFAppPasswordDialog> dialog = shell ? qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog()) : nullptr;
        if (!dialog)
        {
            return true;
        }
        typeText(dialog->getPasswordField(), pdftest::AppDocumentSamples::getOwnerPassword());
        if (dialog)
        {
            Steps::click(dialog->getUnlockButton());
        }
        Steps::settle();
        return true;
    }

    /// Open Restrict Permissions of the owner password dialog of the entry (owner answer Q5.12): the
    /// dialog of a tool, whose permission the document forbids, its Secondary button, then the owner
    /// password dialog of Restrict Permissions (ChangeSecurity), answered by the owner password
    static bool openRestrictPermissions(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* now = Steps::shell(run); return !now || now->getPermissionDialog(); });
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        pdf::PDFAppPasswordDialog* dialog = shell ? qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog()) : nullptr;
        if (!dialog)
        {
            return true;
        }
        if (!dialog->getRestrictPermissionsButton())
        {
            return false;
        }
        Steps::click(dialog->getRestrictPermissionsButton());
        Steps::wait(run, [&run]()
        {
            pdf::PDFDocumentToolShell* now = Steps::shell(run);
            return !now || (now->getCatalogEntry() && now->getCatalogEntry()->getId() == QLatin1String(pdf::PDFDocumentToolEncryption::TOOL_ID_PERMISSIONS) && now->getPermissionDialog());
        });
        return unlockEntry(run);
    }

    /// Opens the file encrypted in its attachments only in a tab of its own, which becomes the tab of
    /// the run, and chooses Open without attachments of its password screen (owner answer Q5.9 c)
    static bool openWithoutAttachments(ToolPagesLifetimeRun& run)
    {
        // The victim (the tab of the run) deleted while the file opens stays deleted: the new tab
        // becomes the tab of the run only, when the old one still lives
        if (!run.tab)
        {
            return true;
        }
        const QString filePath = run.fixture ? pdftest::AppDocumentSamples::attachmentsOnly(run.fixture->folder()) : QString();
        pdf::PDFAppDocumentTab* tab = filePath.isEmpty() ? nullptr : run.fixture->open(filePath);
        if (!run.tab || !run.window)
        {
            return true;
        }
        if (!tab)
        {
            return false;
        }
        run.tab = tab;
        Steps::wait(run, [&run]() { return run.tab->getDocument() && run.tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; });
        pdf::PDFAppDocumentStateScreen* screen = run.tab ? run.tab->getStateScreen() : nullptr;
        if (!screen || !screen->getWithoutAttachmentsButton())
        {
            return !run.tab;
        }
        Steps::click(screen->getWithoutAttachmentsButton());
        Steps::wait(run, [&run]() { return run.tab->getDocument() && run.tab->getDocument()->isReady(); });
        return true;
    }

    /// The Attachments panel with its locked card (Locked – enter the password), Enter Password…,
    /// a wrong password with its error, then the right one, which reads the document again
    static bool unlockAttachments(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppDocumentSidebar* sidebar = run.tab && run.tab->getView() ? run.tab->getView()->getSidebar() : nullptr;
        if (!sidebar)
        {
            return !run.tab;
        }
        if (!sidebar->showPanel(pdf::PDFAppDocumentPanelIds::attachments()))
        {
            return false;
        }
        Steps::settle();
        sidebar = run.tab && run.tab->getView() ? run.tab->getView()->getSidebar() : nullptr;
        pdf::PDFAppAttachmentsPanel* panel = sidebar ? qobject_cast<pdf::PDFAppAttachmentsPanel*>(sidebar->findPanel(pdf::PDFAppDocumentPanelIds::attachments())) : nullptr;
        if (!panel || !panel->isAttachmentsLocked())
        {
            return !run.tab;
        }
        Steps::click(panel->getUnlockButton());
        Steps::wait(run, [&run]() { return run.window && run.window->findChild<pdf::PDFAppAttachmentsPasswordDialog*>(); });
        QPointer<pdf::PDFAppAttachmentsPasswordDialog> dialog = run.window ? run.window->findChild<pdf::PDFAppAttachmentsPasswordDialog*>() : nullptr;
        if (!dialog)
        {
            return !run.tab;
        }
        typeText(dialog->getPasswordField(), QStringLiteral("wrong"));
        if (dialog)
        {
            Steps::click(dialog->getUnlockButton());
        }
        Steps::wait(run, [&dialog]() { return !dialog || !dialog->getErrorText().isEmpty(); });
        if (dialog)
        {
            dialog->getPasswordField()->clear();
            typeText(dialog->getPasswordField(), pdftest::AppDocumentSamples::getAttachmentsPassword());
        }
        if (dialog)
        {
            Steps::click(dialog->getUnlockButton());
        }
        Steps::wait(run, [&run]() { return !run.tab->getDocument() || !run.tab->getDocument()->isAttachmentsLocked(); });
        Steps::settle();
        return true;
    }

    /// The Primary button: a result dialog is closed, an info bar of a refused run stays
    static bool run(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* bar = Steps::toolbar(run);
        if (!bar || !Steps::click(bar->getConfirmButton()))
        {
            return true;
        }
        Steps::wait(run, [&run]()
        {
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            return !shell || shell->getState() != pdf::PDFDocumentToolShell::State::Running;
        });
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (QPointer<pdf::PDFAppToolResultDialog> dialog = shell ? shell->getResultDialog() : nullptr)
        {
            dialog->reject();
        }
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerEncryptionScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = EncryptionLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;
    using Tool = pdf::PDFDocumentToolEncryption;

    // Protect with Password: both cards, the owner of its own, the algorithm, the run
    ToolPagesLifetimeScenario password;
    password.name = QStringLiteral("protectpassword");
    password.toolId = QString::fromLatin1(Tool::TOOL_ID_PASSWORD);
    password.hasPanel = true;
    password.prepareRun = [](ToolPagesLifetimeRun& run) { return H::typePassword(run, false, QStringLiteral("Open-2026-x")); };
    password.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::typePassword(run, false, QStringLiteral("Open-2026-x")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { pdf::PDFAppToolGUIEncryption* current = H::gui(run); return H::clickCheck(current ? current->getOwnerSameCheck() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::typePassword(run, true, QStringLiteral("Owner-2026-y")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::setValue(run, QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_RC4)); });
        steps->push_back(&H::run);
    };
    scenarios->push_back(password);

    // Restrict Permissions: a row with its dependency, a preset of the menu, the open password, the run
    ToolPagesLifetimeScenario permissions;
    permissions.name = QStringLiteral("protectpermissions");
    permissions.toolId = QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS);
    permissions.hasPanel = true;
    permissions.prepareRun = [](ToolPagesLifetimeRun& run) { return H::typePassword(run, true, QStringLiteral("Owner-2026-y")); };
    permissions.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickPermission(run, QString::fromLatin1(Tool::PARAMETER_PRINT_LOW)); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickPermission(run, QString::fromLatin1(Tool::PARAMETER_ANNOTATE)); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::choosePreset(run, Tool::Preset::PrintAndFillForms); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::typePassword(run, true, QStringLiteral("Owner-2026-y")); });
        steps->push_back([](ToolPagesLifetimeRun& run) { pdf::PDFAppToolGUIEncryption* current = H::gui(run); return H::clickCheck(current ? current->getRequireOpenCheck() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::typePassword(run, false, QStringLiteral("Open-2026-x")); });
        steps->push_back(&H::run);
    };
    scenarios->push_back(permissions);

    // Remove Password of a document opened without its owner password: the owner password dialog
    // of the entry (ChangeSecurity), then the run
    ToolPagesLifetimeScenario unlock;
    unlock.name = QStringLiteral("protectunlock");
    unlock.toolId = QString::fromLatin1(Tool::TOOL_ID_UNLOCK);
    unlock.openDocument = [](pdftest::AppToolShellFixture& fixture) { return fixture.open(pdftest::AppDocumentSamples::ownerPassword(fixture.folder())); };
    unlock.prepareRun = [](ToolPagesLifetimeRun& run) { return H::unlockEntry(run); };
    unlock.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::unlockEntry(run); });
        steps->push_back(&H::run);
    };
    scenarios->push_back(unlock);

    // Open Restrict Permissions (Q5.12, fix wave P2S5FIXAPP item 10): Extract Text of a document,
    // which forbids copying, asks for the owner password; its Secondary button opens Restrict
    // Permissions, which asks for the owner password itself
    ToolPagesLifetimeScenario restrict;
    restrict.name = QStringLiteral("protectrestrictbutton");
    restrict.toolId = QStringLiteral("convert.extract-text");
    restrict.openDocument = [](pdftest::AppToolShellFixture& fixture) { return fixture.open(pdftest::AppDocumentSamples::ownerPassword(fixture.folder())); };
    restrict.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::openRestrictPermissions);
    };
    scenarios->push_back(restrict);

    // Open without attachments (Q5.9 c, item 10): the password screen of a file encrypted in its
    // attachments only, the locked card of the Attachments panel, the attachments password dialog
    ToolPagesLifetimeScenario attachments;
    attachments.name = QStringLiteral("protectattachments");
    attachments.toolId = QString::fromLatin1(Tool::TOOL_ID_PASSWORD);
    attachments.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&H::openWithoutAttachments);
        steps->push_back(&H::unlockAttachments);

        // Protect with Password of the document read again with its attachments (the page and the
        // shell of the victims; opened by the user password, the owner password dialog of the entry)
        steps->push_back([](ToolPagesLifetimeRun& run) { return !run.tab || Steps::openTool(run); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::unlockEntry(run); });
    };
    scenarios->push_back(attachments);
}
