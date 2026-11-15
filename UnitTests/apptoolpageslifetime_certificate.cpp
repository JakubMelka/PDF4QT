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

// The lifetime scenarios of Encrypt with Certificate and of the dialogs of the certificates (step 5
// of phase 2, package P3; plan 9): protectcertificate (the list, the password, the run to the result
// dialog), certificatecreate (Create a self-signed certificate: the form, the creation, the new
// certificate chosen) and certificateimport (the file dialog, the collision 436:11538, Choose Another
// File…). The certificates are created at run time in certificate folders under a temporary folder
// of the test (PDFCertificateManager::setCertificateDirectories; nothing on the system disk); the
// folders go, when the process ends.

#include "apptestcertificatefolders.h"
#include "apptoolpageslifetime.h"

#include "pdfappcertificatedialogs.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcertificatemanager.h"
#include "pdffluentwidgets.h"

#include <QDir>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <memory>

/// The certificate folders of the scenarios: under a temporary folder of the test (the TEMP of the
/// runner, pdftest::AppTestCertificateFolders; never a folder of the user, nothing on the system
/// disk); the folders are given back and removed, when the application object goes
class CertificateLifetimeFolders
{
public:
    /// A post routine of QCoreApplication: the outer folders again, the temporary folder removed
    static void cleanup()
    {
        instance().m_folders.reset();
    }

    static CertificateLifetimeFolders& instance()
    {
        static CertificateLifetimeFolders folders;
        return folders;
    }

    /// The folders with one .pfx file with a password (a fresh state for each run)
    void prepare()
    {
        if (!m_isCleanupAdded)
        {
            m_isCleanupAdded = true;
            qAddPostRoutine(&CertificateLifetimeFolders::cleanup);
        }

        // The old folders go first (the outer folders are set again), then the new ones
        m_folders.reset();
        m_folders = std::make_unique<pdftest::AppTestCertificateFolders>();
        create(m_folders->getApplicationFolder(), QStringLiteral("lifetime.pfx"), QStringLiteral("Lifetime Holder"));
    }

    static QString create(const QString& folder, const QString& fileName, const QString& name)
    {
        return pdftest::AppTestCertificateFolders::create(folder, fileName, name, getPassword());
    }

    static QString getPassword() { return QStringLiteral("lifetime-pfx"); }

private:
    CertificateLifetimeFolders() = default;

    std::unique_ptr<pdftest::AppTestCertificateFolders> m_folders;
    bool m_isCleanupAdded = false;
};

/// Helpers of the scenarios of package P3
class CertificateLifetimeHelper
{
public:
    CertificateLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    /// The folders with one certificate, then three plain pages
    static pdf::PDFAppDocumentTab* openDocument(pdftest::AppToolShellFixture& fixture)
    {
        CertificateLifetimeFolders::instance().prepare();
        return fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("certificate.pdf"), 3));
    }

    /// The part of the preset in the GUI of the shell (a child object of the GUI of P2)
    static pdf::PDFAppToolCertificatePresetPart* part(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        QObject* gui = shell ? shell->getGUI() : nullptr;
        return gui ? gui->findChild<pdf::PDFAppToolCertificatePresetPart*>() : nullptr;
    }

    /// The password of the .pfx file typed as the user does
    static bool typePassword(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolCertificatePresetPart* current = part(run);
        QPointer<pdf::PDFLineEdit> field = (current && current->getPasswordCard()) ? current->getPasswordCard()->getPasswordField() : nullptr;
        if (!field)
        {
            return false;
        }
        field->setFocus();
        for (const QChar character : CertificateLifetimeFolders::getPassword())
        {
            if (!field)
            {
                return true;
            }
            QTest::keyClick(field.data(), character.toLatin1());
        }
        Steps::settle();
        return true;
    }

    /// The button of the toolbar of the part (Create Certificate…, Import…)
    static QToolButton* toolbarButton(const ToolPagesLifetimeRun& run, const QString& objectName)
    {
        pdf::PDFAppToolToolbar* bar = Steps::toolbar(run);
        return bar ? bar->findChild<QToolButton*>(objectName) : nullptr;
    }

    /// Create Certificate…: the form filled, then created (\p isCreated) or canceled
    static bool createCertificate(ToolPagesLifetimeRun& run, bool isCreated)
    {
        if (!Steps::click(toolbarButton(run, QStringLiteral("certificateCreateButton"))))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolCertificatePresetPart* current = part(run); return !current || (current->getCreateDialog() && current->getCreateDialog()->isVisible()); });

        pdf::PDFAppToolCertificatePresetPart* current = part(run);
        QPointer<pdf::PDFAppCertificateCreateDialog> dialog = current ? current->getCreateDialog() : nullptr;
        if (!dialog)
        {
            return true;
        }
        QTest::keyClicks(dialog->getNameField(), QStringLiteral("Created Holder"));
        if (dialog)
        {
            QTest::keyClicks(dialog->getEmailField(), QStringLiteral("holder@example.com"));
        }
        if (dialog)
        {
            QTest::keyClicks(dialog->getOrganizationField(), QStringLiteral("Acme Corp"));
        }
        if (dialog)
        {
            dialog->getKeyLengthCombo()->setCurrentIndex(0);
        }
        if (dialog)
        {
            Steps::click(isCreated ? dialog->getCreateButton() : dialog->getCancelButton());
        }
        Steps::settle();
        return true;
    }

    /// Import…: the file dialog gives a file of the same name (436:11538), then Choose Another
    /// File… another file, which is imported
    static bool importCertificate(ToolPagesLifetimeRun& run)
    {
        const QString folder = run.fixture ? run.fixture->folder() : QString();
        const QString same = CertificateLifetimeFolders::create(folder, QStringLiteral("lifetime.pfx"), QStringLiteral("Same Name"));
        const QString other = CertificateLifetimeFolders::create(folder, QStringLiteral("imported.pfx"), QStringLiteral("Imported Holder"));
        if (same.isEmpty() || other.isEmpty())
        {
            return false;
        }

        pdf::PDFAppPanelFileDialogs::setOpenHandler([same](QWidget*, const QString&, const QString&, const QString&) { return same; });
        const bool isClicked = Steps::click(toolbarButton(run, QStringLiteral("certificateImportButton")));
        Steps::settle();
        if (!isClicked)
        {
            pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
            return false;
        }

        pdf::PDFAppToolCertificatePresetPart* current = part(run);
        QPointer<pdf::PDFAppCertificateImportDialog> dialog = current ? current->getImportDialog() : nullptr;
        if (dialog)
        {
            pdf::PDFAppPanelFileDialogs::setOpenHandler([other](QWidget*, const QString&, const QString&, const QString&) { return other; });
            Steps::click(dialog->getChooseButton());
        }
        pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
        Steps::settle();
        return true;
    }

    /// The Primary button to the result dialog, closed without a file
    static bool runToDialog(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run);
        if (dialog)
        {
            dialog->reject();
        }
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerCertificateScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = CertificateLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // Encrypt with Certificate: the password of the chosen .pfx file, the run (the unlock of the
    // file, the cache) and its result dialog
    ToolPagesLifetimeScenario certificate;
    certificate.name = QStringLiteral("protectcertificate");
    certificate.toolId = QStringLiteral("protect.certificate-encrypt");
    certificate.openDocument = &H::openDocument;
    certificate.hasResultDialog = true;
    certificate.hasPanel = true;
    certificate.prepareRun = &H::typePassword;
    certificate.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::typePassword);
        steps->push_back(&H::runToDialog);
    };
    scenarios->push_back(certificate);

    // Create a self-signed certificate: canceled, then created (the list chooses the new file)
    ToolPagesLifetimeScenario create;
    create.name = QStringLiteral("certificatecreate");
    create.toolId = QStringLiteral("protect.certificate-encrypt");
    create.openDocument = &H::openDocument;
    create.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::createCertificate(run, false); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::createCertificate(run, true); });
    };
    scenarios->push_back(create);

    // Import…: the collision, Choose Another File…, the imported file chosen
    ToolPagesLifetimeScenario import;
    import.name = QStringLiteral("certificateimport");
    import.toolId = QStringLiteral("protect.certificate-encrypt");
    import.openDocument = &H::openDocument;
    import.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::importCertificate);
    };
    scenarios->push_back(import);
}
