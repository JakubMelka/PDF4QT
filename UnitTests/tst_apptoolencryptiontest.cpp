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

// The GUI of Protect with Password, Restrict Permissions and Remove Password (step 5 of phase 2,
// package P2; plan 9): the test of UnitTestsAppToolEncryption and the cases of P2. The expected
// values are the texts and the geometry of the frames (430:1068 ... 433:5999), the dependencies of
// ISO 32000-2 Table 22 (K5.8), the owner answers Q5.1, Q5.2 and the rules of plan chapter 6 (the
// canary of R5-22: a password never leaves its field but for the values of the run). The documents
// are written by the test (the encrypted ones by the factory of the core with a known owner password).

#include "apptestcertificatefolders.h"
#include "apptoolencryption.h"
#include "apptoolshellfixture.h"

#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentview.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfapptoolresultwriter.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabaseschema.h"
#include "pdfdatabasestatement.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdfdocumenttoolusage.h"
#include "pdftoolrepository.h"
#include "pdftoolstartpage.h"
#include "pdfrecentrepository.h"
#include "pdfappdocumenttab.h"
#include "pdfapppaths.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolchangelist.h"
#include "pdfapptoolgui_encryption.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolpermissionlist.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfpasswordstrength.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;
using pdf::PDFAppToolGUIEncryption;
using pdf::PDFDocumentToolShell;
using Tool = pdf::PDFDocumentToolEncryption;

/// Documents and parts of the cases of P2
class EncryptionAppHelper
{
public:
    EncryptionAppHelper() = delete;

    static QString owner() { return QStringLiteral("owner-secret"); }

    /// Table 22: bits 3 and 12 (printing) allowed, bits 4, 5, 6, 9, 11 not - "Printing only" of
    /// 430:2481, 433:5022; bits 1, 2 = 0, the others 1
    static uint32_t printingOnly()
    {
        return 0xFFFFFFFCu & ~uint32_t(0x0008u | 0x0010u | 0x0020u | 0x0100u | 0x0400u);
    }

    /// \p pages plain pages encrypted by AES-256 with an empty user password, the owner password
    /// owner() and the bits \p permissions
    static QString encrypted(const QString& folder, const QString& name, uint32_t permissions, const QString& userPassword = QString())
    {
        const QString source = AppDocumentSamples::simple(folder, QStringLiteral("source-") + name, 2);
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        const pdf::PDFDocument document = reader.readFromFile(source);
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = owner();
        settings.permissions = permissions;
        pdf::PDFSecurityHandlerPointer handler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK || !handler)
        {
            return QString();
        }
        pdf::PDFDocumentBuilder builder(&document);
        builder.setSecurityHandler(handler);
        pdf::PDFDocument result = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return AppDocumentSamples::write(result, filePath) ? filePath : QString();
    }

    /// The password of the attachments of attachmentsOnly
    static QString attachmentsPassword() { return AppDocumentSamples::getAttachmentsPassword(); }

    /// Three attachments encrypted by AES-256 with the user password attachmentsPassword(), the rest
    /// of the document plain (ISO 32000-2, 7.6.6: /StmF and /StrF Identity, /EFF with /EFOpen)
    static QString attachmentsOnly(const QString& folder)
    {
        // The shared sample (the lifetime scenario of the attachments uses it too)
        return AppDocumentSamples::attachmentsOnly(folder);
    }

    /// Opens attachmentsOnly by Open without attachments (Q5.9 c); nullptr, when it did not open
    static pdf::PDFAppDocumentTab* openWithoutAttachments(AppToolShellFixture& fixture)
    {
        const QString filePath = attachmentsOnly(fixture.folder());
        pdf::PDFAppDocumentTab* tab = filePath.isEmpty() ? nullptr : fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        QPushButton* withoutButton = tab->getStateScreen()->getWithoutAttachmentsButton();
        if (!withoutButton)
        {
            return nullptr;
        }
        QTest::mouseClick(withoutButton, Qt::LeftButton);
        if (!AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
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

    /// Opens the tool and, for an encrypted document, answers the owner password dialog of its
    /// entry (430:2709) by owner()
    static PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab* tab, const QString& toolId)
    {
        PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
        if (!shell)
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        if (pdf::PDFAppPasswordDialog* dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog()))
        {
            QTest::keyClicks(dialog->getPasswordField(), owner());
            QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
            AppToolShellFixture::settle();
        }
        return shell;
    }

    static PDFAppToolGUIEncryption* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIEncryption*>(shell->getGUI()) : nullptr;
    }

    static QStringList rows(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            result << list->getKey(i) + QStringLiteral(": ") + list->getValue(i);
        }
        return result;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    /// Types the text as the user does, a key event per character (also a character outside of
    /// ASCII, which QTest::keyClicks cannot send)
    static void typeText(QWidget* widget, const QString& text)
    {
        for (const QChar character : text)
        {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &release);
        }
    }

    /// Types the password into the field and its repetition as the user does
    static void type(pdf::PDFAppToolPasswordCard* card, const QString& password, const QString& repeat)
    {
        typeText(card->getPasswordField(), password);
        if (card->getRepeatField())
        {
            typeText(card->getRepeatField(), repeat);
        }
        AppToolShellFixture::settle();
    }

    /// The button of a toolbar menu by the name of its panel ("algorithm", "scope", "preset")
    static QToolButton* menuButton(PDFDocumentToolShell* shell, const QString& name)
    {
        pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->findChild<pdf::PDFAppToolPanelPopup*>(QStringLiteral("toolPanel-") + name);
        return panel ? panel->getButton() : nullptr;
    }

    static bool contains(const QByteArray& data, const QString& text)
    {
        const QByteArray utf16(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
        return data.contains(text.toUtf8()) || data.contains(utf16);
    }
};

using H = EncryptionAppHelper;

/// The fixture of the shell with the database of the application in a file (a folder under the
/// TEMP of the test, not the portable memory database of AppDocumentFixture): the window and the
/// context are made again over the folder, as tst_appsettingslifetimetest does
class EncryptionDiskFixture : public AppToolShellFixture
{
public:
    EncryptionDiskFixture() :
        AppToolShellFixture(QSize(1440, 960))
    {
        delete window.data();
        settle();
        context.reset();

        m_dataDirectory = path(QStringLiteral("data"));
        QDir().mkpath(m_dataDirectory);
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(QStringList({ QStringLiteral("PDF4QT") }), QDir::currentPath()));
        pdf::PDFDatabaseOptions options;
        options.directory = m_dataDirectory;
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        window = new pdf::PDFAppMainWindow(context.get());
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(QSize(1440, 960));
        window->show();
        if (!QTest::qWaitForWindowExposed(window.data()))
        {
            qWarning("The window was not exposed");
        }
        settle();
        closeMessageBoxes();
    }

    /// The database file of the context (a file, not the memory)
    QString getDatabaseFilePath() const { return pdf::PDFDatabaseFiles::getFilePath(m_dataDirectory, pdf::PDFDatabaseSchema::getCurrentVersion()); }

private:
    QString m_dataDirectory;
};

class AppToolEncryptionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void run_data();
    void run();
};

const std::vector<AppToolEncryptionCase>& AppToolEncryptionPackages::getCases()
{
    static const std::vector<AppToolEncryptionCase> cases = []()
    {
        std::vector<AppToolEncryptionCase> result;
        registerPasswordCases(&result);
        registerCertificateCases(&result);
        return result;
    }();
    return cases;
}

void AppToolEncryptionPackages::registerPasswordCases(std::vector<AppToolEncryptionCase>* cases)
{
    // The four tools are implemented with this GUI (registerEncryptionTools by the context)
    cases->push_back({ QStringLiteral("registered"), []()
    {
        AppToolShellFixture fixture;
        for (const char* id : { Tool::TOOL_ID_PASSWORD, Tool::TOOL_ID_PERMISSIONS, Tool::TOOL_ID_CERTIFICATE, Tool::TOOL_ID_UNLOCK })
        {
            const QString toolId = QString::fromLatin1(id);
            QVERIFY2(pdf::PDFDocumentToolRegistry::isImplemented(toolId), id);
            QVERIFY2(pdf::PDFDocumentToolGUIRegistry::getFactory(toolId), id);
        }
        const auto tool = pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption::registerEncryptionTools();
        QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(Tool::TOOL_ID_PASSWORD)), tool);
    } });

    // tools.md 10.3, plan 2.4 (fix wave P2S5FIXAPP, item 3): nothing is offered after a change of the
    // security - no suggestions, no Often used next on the start page (the result frames are checked by
    // "screenshots"), no pair recorded from these tools, also when an older version stored one; a pair
    // to them is recorded as before (Protect with Password is a default successor of Merge)
    cases->push_back({ QStringLiteral("no-successors"), []()
    {
        AppToolShellFixture fixture;
        pdf::PDFDatabase* database = fixture.context->getDatabase();
        QVERIFY(database);
        pdf::PDFDocumentToolUsage usage(database);
        pdf::PDFDocumentToolAvailability availability;
        availability.hasDocument = true;
        QVERIFY(pdf::PDFDocumentToolUsage::isOfferingSuccessors(QStringLiteral("optimize.compress")));
        QVERIFY(!pdf::PDFDocumentToolUsage::isOfferingSuccessors(QStringLiteral("unknown.tool")));
        QVERIFY(!usage.getSuggestions(QStringLiteral("optimize.compress"), availability).isEmpty());

        for (const char* id : { Tool::TOOL_ID_PASSWORD, Tool::TOOL_ID_PERMISSIONS, Tool::TOOL_ID_CERTIFICATE, Tool::TOOL_ID_UNLOCK })
        {
            const QString toolId = QString::fromLatin1(id);
            QVERIFY2(!pdf::PDFDocumentToolUsage::isOfferingSuccessors(toolId), id);

            // A run over the result of the tool records its own use, never a pair from the tool
            const qint64 runs = fixture.tools().getHistoryRuns();
            QVERIFY(!usage.recordRun(QStringLiteral("optimize.compress"), toolId).isError());
            QVERIFY2(fixture.tools().getPairs({ toolId }).empty(), id);
            QCOMPARE(fixture.tools().getHistoryRuns(), runs);

            // A pair stored by an older version is offered nowhere
            pdf::PDFDatabaseStatement statement(database->getConnection(), "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES (?1, 'optimize.compress', 50, 1)");
            statement.bindText(1, toolId);
            QVERIFY(statement.execute());
            QVERIFY2(usage.getSuggestions(toolId, availability).isEmpty(), id);

            fixture.navigate(QStringLiteral("/tools/") + toolId);
            pdf::PDFToolStartPage* page = fixture.page<pdf::PDFToolStartPage>();
            QVERIFY2(page, id);
            QVERIFY2(page->createOftenNextText().isEmpty(), id);
            QVERIFY2(!page->findChild<QWidget*>(QStringLiteral("toolOftenNext")), id);
        }

        QVERIFY(!usage.recordRun(QString::fromLatin1(Tool::TOOL_ID_PASSWORD), QStringLiteral("pages.merge")).isError());
        bool hasPair = false;
        for (const pdf::PDFDocumentToolPair& pair : fixture.tools().getPairs({ QStringLiteral("pages.merge") }))
        {
            hasPair = hasPair || pair.toTool == QString::fromLatin1(Tool::TOOL_ID_PASSWORD);
        }
        QVERIFY(hasPair);
    } });

    // 430:1287: the empty password, a document without encryption
    cases->push_back({ QStringLiteral("password-start"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QCOMPARE(gui->getMode(), Tool::Mode::Password);

        // The toolbar: Algorithm: AES 256-bit, Encrypt: Everything, Protect Document disabled
        QCOMPARE(H::menuButton(shell, QStringLiteral("algorithm"))->text(), QStringLiteral("Algorithm: AES 256-bit"));
        QCOMPARE(H::menuButton(shell, QStringLiteral("scope"))->text(), QStringLiteral("Encrypt: Everything"));
        QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Protect Document"));
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

        // The panels 300 / 320, the form 620 in the middle of the canvas (430:1194: x 97, y 20)
        QCOMPARE(H::inWindow(shell->getLeftPanel()).width(), 300);
        QCOMPARE(H::inWindow(shell->getRightPanel()).width(), 320);
        const QRect form = H::inWindow(gui->getForm());
        QCOMPARE(form.width(), 620);
        QCOMPARE(form.left(), 306 + 97);
        QCOMPARE(form.top(), 90 + 20);
        QCOMPARE(gui->getHeadingTitle()->text(), QStringLiteral("Require a password to open the document"));
        QCOMPARE(gui->getHeadingText()->text(), QStringLiteral("Everyone who opens the file has to type this password first."));

        // Now 430:1178, After protecting 430:1258
        QCOMPARE(H::rows(gui->getNowRows()), QStringList({ QStringLiteral("Encryption: None"), QStringLiteral("Open password: Not required"), QStringLiteral("Permissions: All allowed") }));
        QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Algorithm: AES 256-bit"), QStringLiteral("Encrypted: Everything"), QStringLiteral("Open password: Not entered yet"),
                                                               QStringLiteral("Owner password: Same as open"), QStringLiteral("Permissions: All allowed") }));

        // The meter "Password strength · Type a password", no criterion; the algorithm Very strong
        pdf::PDFStrengthMeter* meter = gui->getOpenPasswordCard()->getStrengthMeter();
        QCOMPARE(meter->getLevel(), pdf::PDFStrengthMeter::Level::Empty);
        QCOMPARE(meter->getLevelText(), QStringLiteral("Password strength"));
        QCOMPARE(meter->getDetail(), QStringLiteral("Type a password"));
        QCOMPARE(gui->getAlgorithmMeter()->getLevel(), pdf::PDFStrengthMeter::Level::VeryStrong);
        QCOMPARE(gui->getAlgorithmText()->text(), QStringLiteral("Recommended. Opens in all current PDF readers."));

        // 430:1188 the lost password; nothing is replaced; the check box of 430:1244 on
        QVERIFY(gui->getLostCallout()->isVisible());
        QCOMPARE(gui->getLostCallout()->getTitleLabel()->text(), QStringLiteral("A lost password cannot be recovered"));
        QVERIFY(!gui->getReplacedCallout()->isVisible());
        QVERIFY(gui->getOwnerSameCheck()->isChecked());
        QVERIFY(gui->getOwnerSameCheck()->isEnabled());
        QVERIFY(!gui->getOwnerPasswordCard()->isVisible());
        QCOMPARE(gui->getOpenPasswordCard()->getPasswordField()->placeholderText(), QStringLiteral("Password"));
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Enter a password to protect the document"));
    } });

    // 430:1068 filled, 447:15766 do not match, 430:1776 weak
    cases->push_back({ QStringLiteral("password-filled"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        pdf::PDFAppToolPasswordCard* card = gui->getOpenPasswordCard();

        // 447:15766: the repetition differs - the error below it, Protect Document disabled
        H::type(card, QStringLiteral("Tr0ub4dor&3xyz"), QStringLiteral("Tr0ub4dor&3xyQ"));
        QCOMPARE(card->getRepeatErrorText(), QStringLiteral("The passwords do not match."));
        QVERIFY(card->getRepeatField()->isInvalid());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("The passwords do not match"));

        // A part of the password typed into the repetition is no error yet
        card->getRepeatField()->setText(QString());
        QTest::keyClicks(card->getRepeatField(), QStringLiteral("Tr0ub"));
        QVERIFY(card->getRepeatErrorText().isEmpty());

        // 430:1068: the same password - enabled, the summary says Required
        QTest::keyClicks(card->getRepeatField(), QStringLiteral("4dor&3xyz"));
        AppToolShellFixture::settle();
        QVERIFY(card->getRepeatErrorText().isEmpty());
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(H::rows(gui->getSummaryRows()).at(2), QStringLiteral("Open password: Required"));
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("The document will be encrypted with AES 256-bit · nothing is changed until you confirm"));

        // The meter shows the estimate of the core for AES-256 (Q5.8): its level and "N of 128 bits"
        const pdf::PDFPasswordStrength::Result strength = pdf::PDFPasswordStrength::evaluate(QStringLiteral("Tr0ub4dor&3xyz"), pdf::PDFSecurityHandlerFactory::AES_256);
        pdf::PDFStrengthMeter* meter = card->getStrengthMeter();
        QCOMPARE(int(meter->getLevel()), pdf::PDFAppToolPasswordCard::toMeterLevel(strength.level));
        QCOMPARE(meter->getDetail(), QStringLiteral("%1 of 128 bits").arg(strength.bits));

        // The passwords are no value of the model (S5.2), they are the values of the run only
        QVERIFY(shell->getModel()->getValues().value(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)).toString().isEmpty());
        const QVariantMap run = gui->getRunParameters();
        QCOMPARE(run.value(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)).toString(), QStringLiteral("Tr0ub4dor&3xyz"));
        QCOMPARE(run.value(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool(), true);
        QVERIFY(!run.contains(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD)));

        // 430:1776: a very weak password - the amber callout, the run is still allowed
        card->getPasswordField()->setText(QString());
        card->getRepeatField()->setText(QString());
        H::type(card, QStringLiteral("aaaa"), QStringLiteral("aaaa"));
        const pdf::PDFPasswordStrength::Result weak = pdf::PDFPasswordStrength::evaluate(QStringLiteral("aaaa"), pdf::PDFSecurityHandlerFactory::AES_256);
        const bool isWeak = weak.level == pdf::PDFPasswordStrength::Level::VeryWeak || weak.level == pdf::PDFPasswordStrength::Level::Weak;
        QCOMPARE(gui->getWeakCallout()->isVisible(), isWeak);
        if (weak.level == pdf::PDFPasswordStrength::Level::VeryWeak)
        {
            QCOMPARE(gui->getWeakCallout()->getTitleLabel()->text(), QStringLiteral("This password is easy to guess"));
            QCOMPARE(gui->getWeakCallout()->getTextLabel()->text(), QStringLiteral("A very weak password gives little protection even with AES 256-bit. You can still continue."));
            QCOMPARE(shell->getStatusMessage(), QStringLiteral("The password is very weak – the document can still be protected with it"));
        }
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    } });

    // 430:1999, 2.5: RC4 - the characters of PDFDocEncoding (ISO 32000-2, 7.6.4.3.3 and Annex D:
    // "č" has no code there, "é" has), the red callout, the algorithm Weak; 32 characters of R4
    cases->push_back({ QStringLiteral("password-legacy"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_RC4));
        AppToolShellFixture::settle();
        QCOMPARE(H::menuButton(shell, QStringLiteral("algorithm"))->text(), QStringLiteral("Algorithm: RC4 128-bit"));
        QVERIFY(gui->getLegacyCallout()->isVisible());
        QCOMPARE(gui->getLegacyCallout()->getTitleLabel()->text(), QStringLiteral("RC4 128-bit is weak and can be broken"));
        QCOMPARE(gui->getLegacyCallout()->getTextLabel()->text(), QStringLiteral("Choose it only when the recipient uses a very old PDF reader. AES 256-bit is strongly recommended."));
        QCOMPARE(gui->getAlgorithmMeter()->getLevel(), pdf::PDFStrengthMeter::Level::Weak);
        QCOMPARE(gui->getAlgorithmText()->text(), QStringLiteral("Only for backward compatibility with very old readers."));
        QCOMPARE(H::rows(gui->getSummaryRows()).at(0), QStringLiteral("Algorithm: RC4 128-bit"));

        pdf::PDFAppToolPasswordCard* card = gui->getOpenPasswordCard();
        H::type(card, QStringLiteral("Heslo-čaj-é1"), QStringLiteral("Heslo-čaj-é1"));
        QCOMPARE(card->getErrorText(), QStringLiteral("Character “č” cannot be used with RC4 128-bit. Use AES 256-bit or a password with basic Latin characters."));
        QVERIFY(card->getPasswordField()->isInvalid());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("The password contains characters that RC4 128-bit cannot use"));

        // AES 256-bit takes them (UTF-8 of revision 6): the error goes
        shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_AES_256));
        AppToolShellFixture::settle();
        QVERIFY(card->getErrorText().isEmpty());
        QVERIFY(!gui->getLegacyCallout()->isVisible());
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

        // AES 128-bit: Strong (430:1497 "Strong · for readers older than 2010"); a password of more
        // than 32 characters is used only by its beginning (7.6.4.3.3, the callout of 2.5)
        shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_AES_128));
        AppToolShellFixture::settle();
        QCOMPARE(gui->getAlgorithmMeter()->getLevel(), pdf::PDFStrengthMeter::Level::Strong);
        card->getPasswordField()->setText(QString());
        card->getRepeatField()->setText(QString());
        const QString longPassword = QStringLiteral("abcdefghij-ABCDEFGHIJ-0123456789-xyz");
        H::type(card, longPassword, longPassword);
        const bool isTruncated = pdf::PDFPasswordStrength::evaluate(longPassword, pdf::PDFSecurityHandlerFactory::AES_128).isTruncated;
        QCOMPARE(card->getLengthCallout()->isVisible(), isTruncated);
        if (isTruncated)
        {
            QCOMPARE(card->getLengthCallout()->getTitleLabel()->text(), QStringLiteral("Only the first 32 characters are used with AES 128-bit."));
        }
    } });

    // 430:2228: the owner password of its own; Q5.1 (432:3721): it differs from the open one
    cases->push_back({ QStringLiteral("password-owner"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        H::type(gui->getOpenPasswordCard(), QStringLiteral("Open-Password-2026"), QStringLiteral("Open-Password-2026"));

        QTest::mouseClick(gui->getOwnerSameCheck(), Qt::LeftButton, Qt::NoModifier, QPoint(9, gui->getOwnerSameCheck()->height() / 2));
        AppToolShellFixture::settle();
        QVERIFY(!gui->getOwnerSameCheck()->isChecked());
        QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool(), false);
        pdf::PDFAppToolPasswordCard* ownerCard = gui->getOwnerPasswordCard();
        QVERIFY(ownerCard->isVisible());
        QCOMPARE(ownerCard->findChild<QLabel*>(QStringLiteral("passwordLabel"))->text(), QStringLiteral("Owner password"));
        QCOMPARE(ownerCard->findChild<QLabel*>(QStringLiteral("repeatLabel"))->text(), QStringLiteral("Repeat owner password"));
        QCOMPARE(H::rows(gui->getSummaryRows()).at(3), QStringLiteral("Owner password: Not entered yet"));
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

        H::type(ownerCard, QStringLiteral("Owner-Password-2026"), QStringLiteral("Owner-Password-2026"));
        QVERIFY(ownerCard->getErrorText().isEmpty());
        QCOMPARE(H::rows(gui->getSummaryRows()).at(3), QStringLiteral("Owner password: Separate"));
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        const QVariantMap run = gui->getRunParameters();
        QCOMPARE(run.value(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD)).toString(), QStringLiteral("Owner-Password-2026"));
        QCOMPARE(run.value(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool(), false);

        // Undo returns the check box (K5.18); the passwords are no step of the history
        QVERIFY(shell->getModel()->canUndo());
        shell->getModel()->undo();
        AppToolShellFixture::settle();
        QVERIFY(gui->getOwnerSameCheck()->isChecked());
        QVERIFY(!gui->getOwnerPasswordCard()->isVisible());
        QVERIFY(!shell->getModel()->canUndo());
    } });

    // 430:2481, Q5.1, K5.19: an encrypted document with the restrictions Printing only, unlocked
    // by its owner password - the restrictions are kept, so the owner password is its own one
    cases->push_back({ QStringLiteral("password-encrypted"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Project_Report.pdf"), H::printingOnly()));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QVERIFY(!shell->isPermissionRefused());
        QVERIFY(tab->getDocument()->isOwnerUnlocked());

        QCOMPARE(H::rows(gui->getNowRows()), QStringList({ QStringLiteral("Encryption: AES 256-bit"), QStringLiteral("Open password: Not required"), QStringLiteral("Permissions: Printing only"),
                                                           QStringLiteral("Your access: Owner") }));
        QVERIFY(gui->getReplacedCallout()->isVisible());
        QCOMPARE(gui->getReplacedCallout()->getTitleLabel()->text(), QStringLiteral("The current protection is replaced"));
        QCOMPARE(gui->getReplacedCallout()->getTextLabel()->text(), QStringLiteral("The new password and algorithm replace the AES 256-bit protection. The current restrictions (printing only) are kept."));
        QCOMPARE(H::rows(gui->getSummaryRows()).at(4), QStringLiteral("Permissions: Printing only · kept"));

        // Q5.1: the check box off and disabled with the reason, the owner card shown
        QVERIFY(!gui->getOwnerSameCheck()->isChecked());
        QVERIFY(!gui->getOwnerSameCheck()->isEnabled());
        QVERIFY(gui->getOwnerSameHint()->isVisible());
        QVERIFY(gui->getOwnerSameHint()->text().startsWith(QStringLiteral("The document keeps its restrictions (printing only), so it needs an owner password of its own")));
        QVERIFY(gui->getOwnerPasswordCard()->isVisible());
        H::type(gui->getOpenPasswordCard(), QStringLiteral("Open-Password-2026"), QStringLiteral("Open-Password-2026"));
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Enter an owner password – the document keeps its restrictions"));

        // The same password as the open one: the validation of the core refuses it (Q5.1), the
        // owner field shows its text, the open field stays as it is
        pdf::PDFAppToolPasswordCard* ownerCard = gui->getOwnerPasswordCard();
        H::type(ownerCard, QStringLiteral("Open-Password-2026"), QStringLiteral("Open-Password-2026"));
        QCOMPARE(ownerCard->getErrorText(), QStringLiteral("The restrictions of the document are kept, so it needs an owner password different from the open password – otherwise every reader gets owner access."));
        QVERIFY(ownerCard->getPasswordField()->isInvalid());
        QVERIFY(gui->getOpenPasswordCard()->getErrorText().isEmpty());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("The owner password must differ from the open password"));

        ownerCard->getPasswordField()->setText(QString());
        ownerCard->getRepeatField()->setText(QString());
        H::type(gui->getOwnerPasswordCard(), QStringLiteral("Owner-Password-2026"), QStringLiteral("Owner-Password-2026"));
        QVERIFY(ownerCard->getErrorText().isEmpty());
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(gui->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)).toBool(), false);
    } });

    // 432:2105 (the default View and print), Q5.2 (Access for screen readers)
    cases->push_back({ QStringLiteral("permissions-frame"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QCOMPARE(H::menuButton(shell, QStringLiteral("preset"))->text(), QStringLiteral("Preset: View and print"));
        QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Apply Restrictions"));
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

        // form 432:2283: 640 at x 87; the heading and the three cards
        const QRect form = H::inWindow(gui->getForm());
        QCOMPARE(form.width(), 640);
        QCOMPARE(form.left(), 306 + 87);
        QCOMPARE(gui->getHeadingTitle()->text(), QStringLiteral("Choose what readers may do"));
        QCOMPARE(gui->getHeadingText()->text(), QStringLiteral("Checked actions stay allowed. Everything else needs the owner password."));
        pdf::PDFAppToolPermissionList* list = gui->getPermissionList();
        QCOMPARE(list->getRow(QStringLiteral("printLow"))->getTitle(), QStringLiteral("Print in low resolution"));
        QCOMPARE(list->getRow(QStringLiteral("printLow"))->getDescription(), QStringLiteral("Draft quality only"));
        QCOMPARE(list->getRow(QStringLiteral("assemble"))->getDescription(), QStringLiteral("Insert, rotate and delete pages, edit bookmarks"));
        QCOMPARE(list->getRow(QStringLiteral("fillForms"))->getDescription(), QStringLiteral("Fill existing form fields and sign"));
        QCOMPARE(H::inWindow(list->getRow(QStringLiteral("printLow"))).height(), 44);
        QVERIFY(list->getRow(QStringLiteral("printLow"))->isCheckBoxChecked());
        QVERIFY(list->getRow(QStringLiteral("printHigh"))->isCheckBoxChecked());
        QVERIFY(!list->getRow(QStringLiteral("copy"))->isCheckBoxChecked());
        pdf::PDFTileRow* accessibility = list->getRow(QString::fromLatin1(pdf::PDFAppToolPermissionList::ACCESSIBILITY_ROW));
        QVERIFY(accessibility->isCheckBoxChecked());
        QVERIFY(!accessibility->isCheckBoxEnabled());
        QCOMPARE(accessibility->getDescription(), QStringLiteral("Always allowed in PDF 2.0"));
        QCOMPARE(list->getAllowedCount(), 3);

        // 432:2373 Readers, 432:2389 Encryption, the callout 432:2402
        QCOMPARE(H::rows(gui->getReadersRows()), QStringList({ QStringLiteral("Allowed: 3 of 8 actions"), QStringLiteral("Printing: High resolution"), QStringLiteral("Editing: Not allowed"),
                                                               QStringLiteral("Copying: Not allowed") }));
        QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Algorithm: AES 256-bit"), QStringLiteral("Encrypted: Everything"), QStringLiteral("Open password: Not required"),
                                                               QStringLiteral("Owner password: Not entered yet") }));
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Enter an owner password to apply the restrictions"));

        // The owner password in the left panel (432:2235), the open password hidden (432:2278)
        H::type(gui->getOwnerPasswordCard(), QStringLiteral("Owner-Password-2026"), QStringLiteral("Owner-Password-2026"));
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("3 of 8 actions stay allowed · the document will be encrypted with AES 256-bit"));
        QVERIFY(!gui->getOpenPasswordCard()->isVisible());
        const QVariantMap run = gui->getRunParameters();
        QCOMPARE(run.value(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD)).toString(), QStringLiteral("Owner-Password-2026"));
        QVERIFY(!run.contains(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)));
    } });

    // K5.8: the dependencies of Table 22 (bit 12 only with bit 3; bit 6 includes filling of forms)
    cases->push_back({ QStringLiteral("permissions-dependencies"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("dependencies.pdf"), 1));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        pdf::PDFAppToolPermissionList* list = gui->getPermissionList();
        pdf::PDFAppToolParameterModel* model = shell->getModel();

        // Unchecking low resolution unchecks high resolution - one undo step
        QTest::mouseClick(list->getRow(QStringLiteral("printLow")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("printLow")).toBool(), false);
        QCOMPARE(model->getValue(QStringLiteral("printHigh")).toBool(), false);
        QVERIFY(!list->getRow(QStringLiteral("printHigh"))->isCheckBoxChecked());
        QCOMPARE(H::menuButton(shell, QStringLiteral("preset"))->text(), QStringLiteral("Preset: View only"));
        model->undo();
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("printLow")).toBool(), true);
        QCOMPARE(model->getValue(QStringLiteral("printHigh")).toBool(), true);

        // Checking high resolution checks low resolution
        model->setValues(Tool::getPresetValues(Tool::Preset::ViewOnly));
        AppToolShellFixture::settle();
        QTest::mouseClick(list->getRow(QStringLiteral("printHigh")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("printLow")).toBool(), true);
        QCOMPARE(model->getValue(QStringLiteral("printHigh")).toBool(), true);

        // Edit comments and form fields checks and disables Fill in forms
        QTest::mouseClick(list->getRow(QStringLiteral("annotate")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("fillForms")).toBool(), true);
        QVERIFY(list->getRow(QStringLiteral("fillForms"))->isCheckBoxChecked());
        QVERIFY(!list->getRow(QStringLiteral("fillForms"))->isCheckBoxEnabled());
        QTest::mouseClick(list->getRow(QStringLiteral("fillForms")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("fillForms")).toBool(), true);

        // Modify does not include Assemble ("even if bit 4 is clear")
        QTest::mouseClick(list->getRow(QStringLiteral("modify")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(model->getValue(QStringLiteral("modify")).toBool(), true);
        QCOMPARE(model->getValue(QStringLiteral("assemble")).toBool(), false);

        // The pure rule
        QVariantMap values = Tool::getPresetValues(Tool::Preset::AllowEverything);
        values = pdf::PDFAppToolPermissionList::applyChange(values, QStringLiteral("printLow"), false);
        QCOMPARE(values.value(QStringLiteral("printHigh")).toBool(), false);
        values = pdf::PDFAppToolPermissionList::applyChange(values, QStringLiteral("annotate"), false);
        QCOMPARE(values.value(QStringLiteral("fillForms")).toBool(), true);
    } });

    // 432:2698 presets, 432:3072 Print and fill forms (4 of 8), 432:4069 Allow everything
    cases->push_back({ QStringLiteral("permissions-presets"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("presets.pdf"), 1));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QToolButton* presetButton = H::menuButton(shell, QStringLiteral("preset"));
        QTest::mouseClick(presetButton, Qt::LeftButton);
        AppToolShellFixture::settle();
        QVERIFY(gui->getPresetPanel()->isVisible());
        QCOMPARE(gui->getPresetRow(Tool::Preset::ViewOnly)->getDescription(), QStringLiteral("No printing, editing or copying"));
        QCOMPARE(gui->getPresetRow(Tool::Preset::PrintAndFillForms)->getDescription(), QStringLiteral("Readers can also fill in form fields"));
        QVERIFY(gui->getPresetRow(Tool::Preset::ViewAndPrint)->isChecked());

        QTest::mouseClick(gui->getPresetRow(Tool::Preset::PrintAndFillForms), Qt::LeftButton);
        AppToolShellFixture::settle();
        QVERIFY(!gui->getPresetPanel()->isVisible());
        QCOMPARE(presetButton->text(), QStringLiteral("Preset: Print and fill forms"));
        QCOMPARE(gui->getPermissionList()->getAllowedCount(), 4);
        QCOMPARE(H::rows(gui->getReadersRows()).at(2), QStringLiteral("Editing: Forms only"));
        QVERIFY(!gui->getNothingRestrictedCallout()->isVisible());

        gui->getPresetRow(Tool::Preset::AllowEverything)->click();
        AppToolShellFixture::settle();
        QCOMPARE(presetButton->text(), QStringLiteral("Preset: Allow everything"));
        QCOMPARE(H::rows(gui->getReadersRows()), QStringList({ QStringLiteral("Allowed: 8 of 8 actions"), QStringLiteral("Printing: High resolution"), QStringLiteral("Editing: Allowed"),
                                                               QStringLiteral("Copying: Allowed") }));
        QVERIFY(gui->getNothingRestrictedCallout()->isVisible());
        QCOMPARE(gui->getNothingRestrictedCallout()->getTitleLabel()->text(), QStringLiteral("Nothing is restricted"));

        // A change by hand is Custom (K18 of step 1)
        QTest::mouseClick(gui->getPermissionList()->getRow(QStringLiteral("copy")), Qt::LeftButton);
        AppToolShellFixture::settle();
        QCOMPARE(presetButton->text(), QStringLiteral("Preset: Custom"));
    } });

    // 432:3377 Also require a password to open, 432:3721 the same password
    cases->push_back({ QStringLiteral("permissions-open-password"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("open.pdf"), 1));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        H::type(gui->getOwnerPasswordCard(), QStringLiteral("Same-Password-2026"), QStringLiteral("Same-Password-2026"));
        gui->getRequireOpenCheck()->click();
        AppToolShellFixture::settle();
        QVERIFY(gui->getOpenPasswordCard()->isVisible());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Enter the open password"));

        // Both cards show "Password" in their sections (432:2235, 432:3377): a screen reader tells
        // the owner password from the open password by the names of the fields (review RS5APP)
        QCOMPARE(gui->getOwnerPasswordCard()->getPasswordField()->accessibleName(), QStringLiteral("Owner password"));
        QCOMPARE(gui->getOwnerPasswordCard()->getRepeatField()->accessibleName(), QStringLiteral("Repeat owner password"));
        QCOMPARE(gui->getOpenPasswordCard()->getPasswordField()->accessibleName(), QStringLiteral("Open password"));
        QCOMPARE(gui->getOpenPasswordCard()->getRepeatField()->accessibleName(), QStringLiteral("Repeat open password"));

        H::type(gui->getOpenPasswordCard(), QStringLiteral("Same-Password-2026"), QStringLiteral("Same-Password-2026"));
        QCOMPARE(gui->getOwnerPasswordCard()->getErrorText(), QStringLiteral("Use a password different from the open password – otherwise every reader gets owner access."));
        QVERIFY(gui->getOpenPasswordCard()->getPasswordField()->isInvalid());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("The owner password must differ from the open password"));

        gui->getOpenPasswordCard()->getPasswordField()->setText(QString());
        gui->getOpenPasswordCard()->getRepeatField()->setText(QString());
        H::type(gui->getOpenPasswordCard(), QStringLiteral("Open-Password-2026"), QStringLiteral("Open-Password-2026"));
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(H::rows(gui->getSummaryRows()).at(2), QStringLiteral("Open password: Required"));
        QCOMPARE(gui->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)).toString(), QStringLiteral("Open-Password-2026"));
    } });

    // 433:5671 nothing to remove, 433:5022 the changes of a document with Printing only
    cases->push_back({ QStringLiteral("unlock-frames"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* plainTab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 1));
        PDFDocumentToolShell* plainShell = H::openTool(fixture, plainTab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK));
        PDFAppToolGUIEncryption* plainGui = H::gui(plainShell);
        QVERIFY(plainGui);
        QCOMPARE(plainGui->getHeadingTitle()->text(), QStringLiteral("Nothing to remove"));
        QCOMPARE(plainGui->getHeadingText()->text(), QStringLiteral("Project_Report.pdf has no password, encryption or restrictions."));
        QVERIFY(plainGui->getEmptyCard()->isVisible());
        QVERIFY(!plainGui->getChangeList()->isVisible());
        QVERIFY(!plainShell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(plainShell->getToolbar()->getConfirmText(), QStringLiteral("Remove Protection"));
        QCOMPARE(plainShell->getStatusMessage(), QStringLiteral("This document is not encrypted"));
        QCOMPARE(H::rows(plainGui->getSummaryRows()), QStringList({ QStringLiteral("Encryption: None"), QStringLiteral("Open password: Not required"), QStringLiteral("Permissions: All allowed") }));
        plainShell->cancel();
        AppToolShellFixture::settle();

        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Protected.pdf"), H::printingOnly()));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QCOMPARE(gui->getHeadingTitle()->text(), QStringLiteral("Remove the protection"));
        pdf::PDFAppToolChangeList* changes = gui->getChangeList();
        QVERIFY(changes->isVisible());
        QCOMPARE(changes->getChanges().size(), size_t(3));
        QCOMPARE(changes->getRow(0)->getTitle(), QStringLiteral("Encryption"));
        QCOMPARE(changes->getRow(0)->getDescription(), QStringLiteral("Contents are encrypted in the file"));
        QCOMPARE(changes->getBeforeLabel(0)->text(), QStringLiteral("AES 256-bit"));
        QCOMPARE(changes->getAfterLabel(0)->text(), QStringLiteral("None"));
        QCOMPARE(changes->getRow(1)->getTitle(), QStringLiteral("Open password"));
        QCOMPARE(changes->getBeforeLabel(1)->text(), QStringLiteral("Not required"));
        QCOMPARE(changes->getAfterLabel(1)->text(), QStringLiteral("Not needed"));
        QCOMPARE(changes->getBeforeLabel(2)->text(), QStringLiteral("Printing only"));
        QCOMPARE(changes->getAfterLabel(2)->text(), QStringLiteral("All allowed"));
        QVERIFY(gui->getRemoveCallout()->isVisible());
        QCOMPARE(gui->getRemoveCallout()->getTitleLabel()->text(), QStringLiteral("Anyone will be able to open, copy and edit the document"));
        QCOMPARE(gui->getInfoCallout()->getTitleLabel()->text(), QStringLiteral("PDF4QT does not break passwords"));
        QCOMPARE(H::rows(gui->getNowRows()).at(3), QStringLiteral("Your access: Owner"));
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Encryption, password and restrictions will be removed · nothing is changed until you confirm"));
    } });

    // R5-22, S5.1, S5.2: the canary - a password typed into the GUI is never a text of a widget but
    // its field, never on the clipboard, never in the model or its history, never in the database
    // files (-wal, -shm too); the fields are emptied (with their undo history) at the start of a run
    cases->push_back({ QStringLiteral("canary"), []()
    {
        const QString canary = QStringLiteral("Canary-7Q4x-PDF4QT-Secret");
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("canary.pdf"), 1));
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        H::type(gui->getOwnerPasswordCard(), canary, canary);
        gui->getRequireOpenCheck()->click();
        H::type(gui->getOpenPasswordCard(), canary + QStringLiteral("-open"), canary + QStringLiteral("-open"));
        QVERIFY(gui->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD)).toString() == canary);

        // The clipboard: Copy and Cut of the field do nothing, also when the password is shown
        QApplication::clipboard()->setText(QStringLiteral("before"));
        pdf::PDFLineEdit* field = gui->getOwnerPasswordCard()->getPasswordField();
        field->setFocus();
        field->selectAll();
        QTest::keyClick(field, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(field, Qt::Key_Insert, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("before"));

        // The texts of every widget of the window but the password fields
        const auto checkTexts = [&canary]()
        {
            for (QWidget* topLevel : QApplication::topLevelWidgets())
            {
                QList<QWidget*> widgets = topLevel->findChildren<QWidget*>();
                widgets << topLevel;
                for (QWidget* widget : widgets)
                {
                    const QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget);
                    QStringList texts = { widget->toolTip(), widget->accessibleName(), widget->accessibleDescription(), widget->windowTitle(), widget->statusTip(), widget->whatsThis() };
                    if (const QLabel* label = qobject_cast<QLabel*>(widget))
                    {
                        texts << label->text();
                    }
                    if (const QAbstractButton* button = qobject_cast<QAbstractButton*>(widget))
                    {
                        texts << button->text();
                    }
                    if (lineEdit && lineEdit->echoMode() == QLineEdit::Normal && lineEdit->text().contains(canary))
                    {
                        // A password field may show its password (the eye); no other field has it
                        QVERIFY2(qobject_cast<const pdf::PDFLineEdit*>(lineEdit) && static_cast<const pdf::PDFLineEdit*>(lineEdit)->isPasswordField(), qPrintable(widget->objectName()));
                    }
                    for (const QString& text : texts)
                    {
                        QVERIFY2(!text.contains(canary), qPrintable(widget->objectName() + QStringLiteral(": ") + text));
                    }
                }
            }
        };
        checkTexts();
        QVERIFY(!shell->getStatusMessage().contains(canary));

        // The model, its history and its stored values
        pdf::PDFAppToolParameterModel* model = shell->getModel();
        for (auto it = model->getValues().cbegin(); it != model->getValues().cend(); ++it)
        {
            QVERIFY2(!it.value().toString().contains(canary), qPrintable(it.key()));
        }
        for (const char* name : { Tool::PARAMETER_OWNER_PASSWORD, Tool::PARAMETER_OPEN_PASSWORD })
        {
            for (const QVariant& value : model->getHistoryValues(QString::fromLatin1(name)))
            {
                QVERIFY(!value.toString().contains(canary));
            }
        }
        model->storeLastValues();

        // The run: the fields of the cards are emptied at its start (S5.1) - the same fields, while
        // the result dialog is open (430:3226 / 432:4388); Ctrl+Z gives nothing back
        QPointer<pdf::PDFLineEdit> ownerField = gui->getOwnerPasswordCard()->getPasswordField();
        QPointer<pdf::PDFLineEdit> openField = gui->getOpenPasswordCard()->getPasswordField();
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }));
        AppToolShellFixture::settle();
        QVERIFY(ownerField && openField);
        QVERIFY(ownerField->text().isEmpty());
        QVERIFY(openField->text().isEmpty());
        QVERIFY(!ownerField->isUndoAvailable());
        ownerField->undo();
        QVERIFY(ownerField->text().isEmpty());
        checkTexts();

        // S5.1 (review RS5SEC): the runner holds the passwords of the run only while the decision
        // lasts (a refused commit runs again with them); the decision ended, its parameters of the
        // last run keep no password - the other values stay
        QPointer<pdf::PDFDocumentToolRunner> runner = shell->getRunner();
        QVERIFY(runner);
        QCOMPARE(runner->getState(), pdf::PDFDocumentToolRunner::State::Waiting);
        QCOMPARE(runner->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD)).toString(), canary);
        if (QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog())
        {
            dialog->reject();
            AppToolShellFixture::settle();
        }
        checkTexts();

        // The same on a runner, which outlives its decision: a decision ended (endDecision) and a
        // failed run drop the passwords, the other values of the last run stay
        QPointer<pdf::PDFAppDocumentTab> runnerTab = tab;
        QVERIFY(runnerTab && runnerTab->getDocument() && runnerTab->getDocument()->isReady());
        QPointer<pdf::PDFDocumentToolRunner> directRunner = new pdf::PDFDocumentToolRunner(runnerTab->getDocument(), pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(Tool::TOOL_ID_PASSWORD)), runnerTab);
        QVariantMap runValues;
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_AES_256));
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_SCOPE), QString::fromLatin1(Tool::SCOPE_EVERYTHING));
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD), canary);
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN), true);
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_KEEP_RESTRICTIONS), true);
        QVERIFY(directRunner->run(runValues));
        QVERIFY(AppToolShellFixture::waitFor([directRunner]() { return !directRunner || directRunner->getState() != pdf::PDFDocumentToolRunner::State::Running; }));
        QVERIFY(directRunner);
        QCOMPARE(directRunner->getState(), pdf::PDFDocumentToolRunner::State::Waiting);
        QCOMPARE(directRunner->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)).toString(), canary);
        directRunner->endDecision();
        QVERIFY(directRunner);
        QCOMPARE(directRunner->getState(), pdf::PDFDocumentToolRunner::State::Idle);
        QVERIFY(!directRunner->getRunParameters().contains(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD)));
        QCOMPARE(directRunner->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_ALGORITHM)).toString(), QString::fromLatin1(Tool::ALGORITHM_AES_256));

        // A failed run (no open password typed, an owner password of its own): Idle at once
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_OPEN_PASSWORD), QString());
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN), false);
        runValues.insert(QString::fromLatin1(Tool::PARAMETER_OWNER_PASSWORD), canary);
        QVERIFY(directRunner->run(runValues));
        QVERIFY(AppToolShellFixture::waitFor([directRunner]() { return !directRunner || directRunner->getState() != pdf::PDFDocumentToolRunner::State::Running; }));
        QVERIFY(directRunner);
        QCOMPARE(directRunner->getState(), pdf::PDFDocumentToolRunner::State::Idle);
        for (auto it = directRunner->getRunParameters().cbegin(); it != directRunner->getRunParameters().cend(); ++it)
        {
            QVERIFY2(!it.value().toString().contains(canary), qPrintable(it.key()));
        }
        QVERIFY(directRunner->getRunParameters().contains(QString::fromLatin1(Tool::PARAMETER_OWNER_SAME_AS_OPEN)));
        delete directRunner.data();
        AppToolShellFixture::settle();

        // The database of the application: the fixture has it in memory (portable), its whole
        // content is written by the export of the settings and searched; a database file of the
        // application (and its -wal, -shm, -journal), when one exists, is searched too
        const QString exported = fixture.path(QStringLiteral("canary-backup.pdf4qtbackup"));
        QVERIFY(!fixture.window->getContext()->getDatabase()->exportBackup(exported).isError());
        QFile backup(exported);
        QVERIFY(backup.open(QIODevice::ReadOnly));
        const QByteArray backupBytes = backup.readAll();
        QVERIFY(backupBytes.size() > 0);
        QVERIFY2(H::contains(backupBytes, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS)) || H::contains(backupBytes, QStringLiteral("printLow")), "the export has the values of the tool");
        QVERIFY(!H::contains(backupBytes, canary));
        const QString databasePath = pdf::PDFAppPaths::getDatabaseFilePath();
        for (const QString& suffix : { QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal") })
        {
            QFile file(databasePath + suffix);
            if (!databasePath.isEmpty() && file.open(QIODevice::ReadOnly))
            {
                QVERIFY2(!H::contains(file.readAll(), canary), qPrintable(databasePath + suffix));
            }
        }
    } });

    // R5-22 on a real database (P2S5INT, item 5): the database of the application is a file under
    // the test TEMP (not the memory of the other cases); Protect with Password runs with the canary,
    // the copy is saved and opened by the canary typed into the password screen. Neither the database
    // file nor its -wal / -shm (searched while PDF4QT has them open and after it has closed them) has
    // the bytes of the canary (UTF-8, UTF-16); the files do have the run (the id of the tool, the
    // file of the copy), so the search reads what PDF4QT wrote
    cases->push_back({ QStringLiteral("canary-database-file"), []()
    {
        const QString canary = QStringLiteral("Canary-9Wd2-PDF4QT-FileDb");
        EncryptionDiskFixture fixture;
        QVERIFY(fixture.window);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("canary-file.pdf"), 1));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        H::type(gui->getOpenPasswordCard(), canary, canary);
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

        // The run, then the copy (preselected, 4.6) opened at once
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == PDFDocumentToolShell::State::Deciding; }));
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Copy);
        QVERIFY(dialog->getOpenCheckBox());
        dialog->getOpenCheckBox()->setChecked(true);
        const QList<pdf::PDFAppDocumentTab*> tabsBefore = fixture.window->getDocumentTabs();
        QTest::mouseClick(dialog->getConfirmButton(), Qt::LeftButton);
        pdf::PDFAppDocumentTab* copyTab = nullptr;
        QVERIFY(AppToolShellFixture::waitFor([&fixture, &tabsBefore, &copyTab]()
        {
            for (pdf::PDFAppDocumentTab* candidate : fixture.window->getDocumentTabs())
            {
                if (!tabsBefore.contains(candidate) && candidate->getDocument() && candidate->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired)
                {
                    copyTab = candidate;
                    return true;
                }
            }
            return false;
        }));
        AppToolShellFixture::settle();
        const QString copyPath = copyTab->getDocument()->getFilePath();
        QVERIFY(QFileInfo::exists(copyPath));

        // The password screen of the copy: the canary opens it
        pdf::PDFAppDocumentStateScreen* screen = copyTab->getStateScreen();
        QVERIFY(screen && screen->getPasswordField());
        screen->getPasswordField()->setFocus();
        QTest::keyClicks(screen->getPasswordField(), canary);
        QTest::mouseClick(screen->getOpenButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([copyTab]() { return copyTab->getDocument()->isReady(); }));
        AppToolShellFixture::settle();
        QVERIFY(copyTab->getDocument()->isPasswordProtected());

        const auto checkFiles = [&canary, &fixture, &copyPath](bool isClosed)
        {
            const QString databasePath = fixture.getDatabaseFilePath();
            QByteArray all;
            for (const QString& suffix : { QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal") })
            {
                QFile file(databasePath + suffix);
                if (file.open(QIODevice::ReadOnly))
                {
                    const QByteArray bytes = file.readAll();
                    QVERIFY2(!H::contains(bytes, canary), qPrintable(databasePath + suffix));
                    all += bytes;
                }
            }

            // The files have the run: the id of the tool (tool usage, last values) and the copy (Recent)
            QVERIFY2(H::contains(all, QString::fromLatin1(Tool::TOOL_ID_PASSWORD)), isClosed ? "closed" : "open");
            QVERIFY2(H::contains(all, QFileInfo(copyPath).fileName()), isClosed ? "closed" : "open");
        };
        checkFiles(false);

        // PDF4QT ends: the window and the context (the database checkpoints its -wal into the file)
        delete fixture.window.data();
        AppToolShellFixture::settle();
        fixture.context.reset();
        checkFiles(true);
    } });

    // Owner answer Q5.9 c, R5-8 (ISO 32000-2, 7.6.6: /StmF and /StrF Identity, /EFF with /AuthEvent
    // /EFOpen): the password screen offers Open without attachments, the document opens locked,
    // the Attachments panel says Locked – enter the password, the password reads the file again
    cases->push_back({ QStringLiteral("attachments-only"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        const QString filePath = H::attachmentsOnly(fixture.folder());
        QVERIFY(!filePath.isEmpty());

        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        QVERIFY(tab);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; }));
        AppToolShellFixture::settle();
        pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
        QPushButton* withoutButton = screen->getWithoutAttachmentsButton();
        QVERIFY(withoutButton && !withoutButton->parentWidget()->isHidden());
        QCOMPARE(withoutButton->text(), QStringLiteral("Open without attachments"));

        QTest::mouseClick(withoutButton, Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        AppToolShellFixture::settle();
        pdf::PDFAppDocument* appDocument = tab->getDocument();
        QVERIFY(appDocument->isLocked());
        QVERIFY(tab->getLoader()->isAttachmentsLocked());
        QVERIFY(appDocument->getDocument()->getStorage().getSecurityHandler()->isEmbeddedFilesLocked());

        // The panel: no rows of encrypted attachments, the locked card instead
        pdf::PDFAppAttachmentsPanel panel(nullptr);
        panel.setDocument(appDocument, tab->getView());
        QVERIFY(panel.isAttachmentsLocked());
        QVERIFY(!panel.getLockedCard()->isHidden());
        QCOMPARE(panel.getLockedCard()->findChild<QLabel*>(QStringLiteral("attachmentsLockedTitle"))->text(), QStringLiteral("Locked – enter the password"));
        QCOMPARE(panel.getModel()->rowCount(), 0);

        // A wrong password keeps it locked with the error, the right one reads it again
        panel.openUnlockDialog();
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppAttachmentsPasswordDialog> dialog = panel.findChild<pdf::PDFAppAttachmentsPasswordDialog*>();
        if (!dialog)
        {
            dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppAttachmentsPasswordDialog>();
        }
        QVERIFY(dialog);
        QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("wrong"));
        QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return !dialog || !dialog->getErrorText().isEmpty(); }));
        QCOMPARE(dialog->getErrorText(), QStringLiteral("Wrong password – check Caps Lock and try again"));
        QVERIFY(appDocument->isLocked());

        dialog->getPasswordField()->setText(QString());
        QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("attachments-secret"));
        QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return !dialog; }));
        AppToolShellFixture::settle();
        QVERIFY(!appDocument->isLocked());
        QVERIFY(!tab->getLoader()->isAttachmentsLocked());
        QVERIFY(!panel.isAttachmentsLocked());
        QVERIFY(panel.getLockedCard()->isHidden());
        QCOMPARE(panel.getModel()->rowCount(), 3);

        // A file encrypted whole has no such button
        pdf::PDFAppDocumentTab* whole = fixture.open(AppDocumentSamples::userPassword(fixture.folder()));
        QVERIFY(AppToolShellFixture::waitFor([whole]() { return whole->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; }));
        AppToolShellFixture::settle();
        QVERIFY(whole->getStateScreen()->getWithoutAttachmentsButton()->parentWidget()->isHidden());
    } });

    // Q5.9 c (P2S5INT, item 1): a document opened without its attachments has them encrypted, so no
    // file may be written from it - every path of an unlock is refused (the document, the command Lock
    // document, the popover of the lock with the reason, Edit a Copy) until the password of the
    // attachments reads it again; that password unlocks it
    cases->push_back({ QStringLiteral("attachments-locked-unlock"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openWithoutAttachments(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        QVERIFY(document->isLocked());
        QVERIFY(document->isAttachmentsLocked());

        // The document itself
        document->setLocked(false);
        QVERIFY(document->isLocked());
        QVERIFY(!document->isEditable());

        // The command Lock document (a toggle): disabled, executing it does not unlock
        QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::lockDocument()));
        tab->executeCommand(pdf::PDFAppCommands::lockDocument());
        AppToolShellFixture::settle();
        QVERIFY(document->isLocked());

        // Edit a Copy: disabled, a copy is refused (it would be unlocked with encrypted attachments)
        QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::editCopy()));
        const int tabCount = int(fixture.window->getDocumentTabs().size());
        tab->executeCommand(pdf::PDFAppCommands::editCopy());
        AppToolShellFixture::settle();
        QCOMPARE(int(fixture.window->getDocumentTabs().size()), tabCount);
        pdf::PDFAppDocument copy(fixture.context.get(), nullptr);
        QVERIFY(!copy.setCopyOf(document.data()));

        // The popover of the lock: both buttons disabled, the text says what is needed
        tab->showLockPopover(QRect(tab->mapToGlobal(QPoint(40, 4)), QSize(16, 16)));
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppLockPopover> popover = tab->getLockPopover();
        QVERIFY(popover);
        QVERIFY(!popover->getUnlockButton()->isEnabled());
        QVERIFY(!popover->getEditCopyButton()->isEnabled());
        QCOMPARE(popover->findChild<QLabel*>(QStringLiteral("lockText"))->text(), pdf::PDFAppDocument::getAttachmentsLockedText());
        QVERIFY(pdf::PDFAppDocument::getAttachmentsLockedText().contains(QStringLiteral("Enter Password…")));
        QVERIFY(pdf::PDFAppDocument::getAttachmentsLockedText().contains(QStringLiteral("Attachments panel")));
        Q_EMIT popover->unlockRequested();
        AppToolShellFixture::settle();
        QVERIFY(document->isLocked());
        if (popover)
        {
            popover->close();
            AppToolShellFixture::settle();
        }

        // The lock is not the user's: it is not stored for the next opening of the file
        const std::optional<pdf::PDFRecentFile> recent = fixture.context->getRecent().getFile(document->getFilePath());
        QVERIFY(!recent || !recent->locked);

        // The password of the attachments reads the file again and unlocks it
        QVERIFY(tab->getLoader()->unlockAttachments(H::attachmentsPassword()));
        QVERIFY(AppToolShellFixture::waitFor([document]() { return document && !document->isAttachmentsLocked() && !document->isLocked(); }));
        QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::lockDocument()));
        QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::editCopy()));
        document->setLocked(true);
        QVERIFY(document->isLocked());
        document->setLocked(false);
        QVERIFY(!document->isLocked());
    } });

    // Owner 2026-11-15 (review RS5APP): the password of the attachments lifts only their lock - a lock,
    // which the user set on the file (recent_file.locked), stays, in the tab and in Recent
    cases->push_back({ QStringLiteral("attachments-locked-user-lock"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openWithoutAttachments(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        const QString filePath = document->getFilePath();
        QVERIFY(!fixture.context->getRecent().recordOpened(filePath).isError());
        QVERIFY(!fixture.context->getRecent().setLocked(filePath, true).isError());
        std::optional<pdf::PDFRecentFile> recent = fixture.context->getRecent().getFile(filePath);
        QVERIFY(recent && recent->locked);

        QPointer<pdf::PDFAppDocumentLoader> loader = tab->getLoader();
        QVERIFY(loader->unlockAttachments(H::attachmentsPassword()));
        QVERIFY(AppToolShellFixture::waitFor([document, loader]() { return document && loader && !loader->isRunning() && !document->isAttachmentsLocked(); }));
        AppToolShellFixture::settle();
        QVERIFY(document->isLocked());
        recent = fixture.context->getRecent().getFile(filePath);
        QVERIFY(recent && recent->locked);

        // It is the lock of the user now: the command Lock document is enabled and lifts it
        QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::lockDocument()));
        document->setLocked(false);
        QVERIFY(!document->isLocked());
    } });

    // Owner 2026-11-15 (review RS5APP): Save As of a document opened without its attachments writes the
    // bytes of its file (the document is never changed, the attachments stay encrypted in the new file
    // by their own password) - not the error "Writing of encrypted documents is not supported."
    cases->push_back({ QStringLiteral("attachments-locked-save-as"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openWithoutAttachments(fixture);
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        QVERIFY(document->isAttachmentsLocked());
        QVERIFY(document->getSnapshot().isOriginalDataWritten());
        QFile source(document->getFilePath());
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray original = source.readAll();
        source.close();
        QVERIFY(!original.isEmpty());

        pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
        QVERIFY(handler);
        const QString target = fixture.path(QStringLiteral("attachments-locked copy.pdf"));
        pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
        QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
        QVERIFY(handler->saveAs());
        QVERIFY(AppToolShellFixture::waitFor([&finishedSpy]() { return finishedSpy.count() == 1; }));
        QVERIFY(finishedSpy.at(0).at(0).toBool());
        QFile written(target);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), original);
        written.close();
        // The tab shows the saved file (Save As), its attachments still locked and the document locked
        QVERIFY(document && document->isAttachmentsLocked() && document->isLocked());
        QCOMPARE(QFileInfo(document->getFilePath()), QFileInfo(target));
    } });

    // Q5.9 c, R5-8 (review RS5APP): a document opened without its attachments is no source of a PDF -
    // Extract Pages (available for a locked document) keeps /EmbeddedFiles of the catalog (7.11.4),
    // whose streams are still encrypted, and the new document has no encryption: the attachments
    // would be written as their cipher text. An export of the text of the document still runs.
    cases->push_back({ QStringLiteral("attachments-locked-new-document"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openWithoutAttachments(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocument* document = tab->getDocument();
        QVERIFY(document->isAttachmentsLocked());
        const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolRunner::createContext(document->getSnapshot());

        const std::shared_ptr<const pdf::PDFDocumentTool> extract = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.extract"));
        QVERIFY(extract);
        const pdf::PDFDocumentToolResult refused = extract->run(context, QVariantMap({ { QStringLiteral("pages"), QStringLiteral("1") } }), nullptr, nullptr);
        QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
        QVERIFY2(refused.message.contains(QStringLiteral("attachments of the document are still encrypted")), qPrintable(refused.message));
        QVERIFY(refused.documents.empty());

        // The text of the pages is no attachment
        const std::shared_ptr<const pdf::PDFDocumentTool> text = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("convert.extract-text"));
        QVERIFY(text);
        const pdf::PDFDocumentToolResult exported = text->run(context, QVariantMap(), nullptr, nullptr);
        QVERIFY2(exported.isSuccess(), qPrintable(exported.message));

        // Review RS5APP (fix wave P2S5FIXAPP, item 9): a tool, whose output is a PDF, is refused already
        // at its entry with the reason in the status bar - not only when its run fails; an export opens
        QVERIFY(!fixture.openTool(tab, QStringLiteral("pages.extract")));
        QVERIFY(!tab->getView()->isToolModeActive());
        QCOMPARE(tab->getView()->getStatusBar()->getMessage(),
                 QStringLiteral("The attachments of this document are still encrypted, so Extract Pages cannot make a PDF of it. Enter their password by Enter Password… in the Attachments panel first."));
        const pdf::PDFDocumentToolCatalogEntry* textEntry = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("convert.extract-text"));
        QVERIFY(textEntry);
        QCOMPARE(pdf::PDFAppToolResultWriter::getDocumentRefusal(pdf::PDFDocumentToolOutputTraits(textEntry), document), pdf::PDFAppToolResultWriter::DocumentRefusal::None);
        const pdf::PDFDocumentToolCatalogEntry* extractEntry = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.extract"));
        QCOMPARE(pdf::PDFAppToolResultWriter::getDocumentRefusal(pdf::PDFDocumentToolOutputTraits(extractEntry), document), pdf::PDFAppToolResultWriter::DocumentRefusal::AttachmentsLocked);
    } });

    // Review RS5SEC L5 (fix wave P2S5FIXAPP, item 8; ISO 32000-2, 7.6.6: /StmF and /StrF Identity): a
    // protection of the attachments only never says "Open password: Required" - the document opens
    // freely, only its attachments need the password: the summary before a run, the rows Now and What
    // changes of a document encrypted so (the words of the fact of the result of the core)
    cases->push_back({ QStringLiteral("attachments-only-texts"), []()
    {
        const QString attachmentsText = QStringLiteral("Not required · attachments need the password");
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* plainTab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        QVERIFY(plainTab);
        PDFDocumentToolShell* shell = H::openTool(fixture, plainTab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_SCOPE), QString::fromLatin1(Tool::SCOPE_ATTACHMENTS)).isEmpty());
        H::type(gui->getOpenPasswordCard(), QStringLiteral("Attach-9Wd2-Secret"), QStringLiteral("Attach-9Wd2-Secret"));
        QVERIFY2(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Open password: ") + attachmentsText), qPrintable(H::rows(gui->getSummaryRows()).join(u'\n')));
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_SCOPE), QString::fromLatin1(Tool::SCOPE_EVERYTHING)).isEmpty());
        AppToolShellFixture::settle();
        QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Open password: Required")));
        shell->getToolbar()->getCancelButton()->click();
        AppToolShellFixture::settle();

        // A document encrypted in its attachments only, opened with their password, the owner unlocks
        pdf::PDFAppDocumentTab* tab = fixture.open(H::attachmentsOnly(fixture.folder()));
        QVERIFY(tab);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->getState() == pdf::PDFAppDocumentState::PasswordRequired; }));
        AppToolShellFixture::settle();
        pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
        screen->getPasswordField()->setFocus();
        QTest::keyClicks(screen->getPasswordField(), H::attachmentsPassword());
        QTest::mouseClick(screen->getOpenButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        AppToolShellFixture::settle();
        QVERIFY(!tab->getDocument()->isAttachmentsLocked());

        PDFDocumentToolShell* unlockShell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK));
        PDFAppToolGUIEncryption* unlockGui = H::gui(unlockShell);
        QVERIFY(unlockGui);
        const pdf::PDFAppToolProtectionFacts facts = pdf::PDFAppToolProtectionFacts::create(tab->getDocument()->getDocument().data());
        QVERIFY(facts.isAttachmentsOnly);
        QVERIFY(!facts.isOpenPasswordRequired);
        QVERIFY2(H::rows(unlockGui->getNowRows()).contains(QStringLiteral("Open password: ") + attachmentsText), qPrintable(H::rows(unlockGui->getNowRows()).join(u'\n')));
        bool isChangeFound = false;
        for (const pdf::PDFAppToolChangeList::Change& change : unlockGui->getChangeList()->getChanges())
        {
            if (change.title == QStringLiteral("Open password"))
            {
                isChangeFound = true;
                QCOMPARE(change.before, attachmentsText);
            }
        }
        QVERIFY(isChangeFound);
    } });

    // K5.13: the tool of a document opened by its user password asks for the owner first; the
    // refusal of a certificate is covered by UnitTestsAppOwnerPassword changeSecurityCertificate
    cases->push_back({ QStringLiteral("authorize-reader"), []()
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Project_Report.pdf"), H::printingOnly()));
        PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
        QVERIFY(shell);
        AppToolShellFixture::settle();
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(shell->isPermissionRefused());
        QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog());
        QVERIFY(dialog);
        QCOMPARE(dialog->accessibleName(), QStringLiteral("Owner password needed"));

        // 430:2709: Your access Reader, the replaced callout of a reader, Protect Document disabled
        QCOMPARE(H::rows(gui->getNowRows()).at(3), QStringLiteral("Your access: Reader"));
        QCOMPARE(gui->getReplacedCallout()->getTextLabel()->text(), QStringLiteral("The new password and settings take the place of the existing AES 256-bit protection."));
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Owner access is needed to change the security of this document"));

        QTest::keyClicks(dialog->getPasswordField(), H::owner());
        QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
        AppToolShellFixture::settle();
        QVERIFY(!dialog);
        QCOMPARE(H::rows(gui->getNowRows()).at(3), QStringLiteral("Your access: Owner"));
        QVERIFY(!shell->isPermissionRefused());
    } });

    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS5FIGMA): the states of the four tools under the ids
    // of their frames (430:1068 ... 433:5999) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color in any of them (CLAUDE.md, Fluent)
    cases->push_back({ QStringLiteral("screenshots"), []()
    {
        if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
        {
            QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
        }

        for (const char* variant : { "light", "dark", "rtl" })
        {
            const QString variantName = QString::fromLatin1(variant);
            const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
            QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
            const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });
            pdftest::AppTestCertificateFolders folders;

            // The scheme after the window: the context of the fixture applies the stored theme
            AppToolShellFixture fixture(QSize(1440, 960));
            pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
            AppToolShellFixture::settle();
            QStringList leaks;
            const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
            {
                AppToolShellFixture::settle();
                if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
                {
                    leaks << frame + suffix;
                }
            };
            const auto shotDialog = [&suffix, &leaks](QWidget* dialog, const QString& frame)
            {
                AppToolShellFixture::settle();
                AppToolShellFixture::screenshotDialog(dialog, frame + suffix);
                if (dialog && AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(dialog, 1.0)) > 0)
                {
                    leaks << frame + suffix;
                }
            };
            const auto closePopup = []()
            {
                if (QWidget* popup = QApplication::activePopupWidget())
                {
                    popup->close();
                }
                AppToolShellFixture::settle();
            };
            const auto clearCard = [](pdf::PDFAppToolPasswordCard* card)
            {
                card->getPasswordField()->setText(QString());
                if (card->getRepeatField())
                {
                    card->getRepeatField()->setText(QString());
                }
            };
            const auto runAndShotResult = [&shotDialog](PDFDocumentToolShell* shell, const QString& frame)
            {
                QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
                QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
                QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 30000));

                // The four result frames have no Then continue with (tools.md 10.3, plan 2.4)
                QVERIFY2(!shell->getResultDialog()->getContinueWith(), qPrintable(frame));
                QVERIFY2(!shell->getResultDialog()->findChild<QWidget*>(QStringLiteral("toolResultContinueWith")), qPrintable(frame));
                shotDialog(shell->getResultDialog(), frame);
                shell->getResultDialog()->reject();
                AppToolShellFixture::settle();
            };

            // Protect with Password (430:1068 ...)
            {
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
                PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
                PDFAppToolGUIEncryption* gui = H::gui(shell);
                QVERIFY(gui);
                shot(QStringLiteral("430-1287-password-start"));
                QTest::mouseClick(H::menuButton(shell, QStringLiteral("algorithm")), Qt::LeftButton);
                shot(QStringLiteral("430-1497-password-menus-algorithm"));
                closePopup();
                QTest::mouseClick(H::menuButton(shell, QStringLiteral("scope")), Qt::LeftButton);
                shot(QStringLiteral("430-1497-password-menus-scope"));
                closePopup();
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Tr0ub4dor&3xyz!Q"), QStringLiteral("Tr0ub4dor&3xyz!W"));
                shot(QStringLiteral("447-15766-password-mismatch"));
                clearCard(gui->getOpenPasswordCard());
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Tr0ub4dor&3xyz!Q"), QStringLiteral("Tr0ub4dor&3xyz!Q"));
                shot(QStringLiteral("430-1068-password"));
                clearCard(gui->getOpenPasswordCard());
                H::type(gui->getOpenPasswordCard(), QStringLiteral("aaaa"), QStringLiteral("aaaa"));
                shot(QStringLiteral("430-1776-password-weak"));
                clearCard(gui->getOpenPasswordCard());
                shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_RC4));
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Heslo-žaj-čé1"), QStringLiteral("Heslo-žaj-čé1"));
                shot(QStringLiteral("430-1999-password-legacy"));
                clearCard(gui->getOpenPasswordCard());
                shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_AES_128));
                shot(QStringLiteral("2-5-password-aes128"));
                shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_ALGORITHM), QString::fromLatin1(Tool::ALGORITHM_AES_256));
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Tr0ub4dor&3xyz!Q"), QStringLiteral("Tr0ub4dor&3xyz!Q"));
                gui->getOwnerSameCheck()->click();
                H::type(gui->getOwnerPasswordCard(), QStringLiteral("Owner-Pass-2026"), QStringLiteral("Owner-Pass-2026"));
                shot(QStringLiteral("430-2228-password-owner"));
                runAndShotResult(shell, QStringLiteral("430-3226-password-done"));
            }

            // Protect with Password over an encrypted document: the owner dialog (430:2709, 430:2969),
            // then the owner state (430:2481)
            {
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Encrypted_Report.pdf"), H::printingOnly()));
                QVERIFY(tab);
                PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID_PASSWORD));
                QVERIFY(shell);
                AppToolShellFixture::settle();
                QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog());
                QVERIFY(dialog);
                shot(QStringLiteral("430-2709-password-authorize"));
                shotDialog(dialog, QStringLiteral("430-2709-password-authorize-dialog"));
                QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("wrong-password"));
                QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
                shot(QStringLiteral("430-2969-password-authorize-denied"));
                shotDialog(dialog, QStringLiteral("430-2969-password-authorize-denied-dialog"));
                dialog->getPasswordField()->setText(QString());
                QTest::keyClicks(dialog->getPasswordField(), H::owner());
                QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
                QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return !dialog; }));
                shot(QStringLiteral("430-2481-password-encrypted"));
            }

            // Restrict Permissions (432:2105 ...)
            {
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report_2.pdf"), 2));
                PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
                PDFAppToolGUIEncryption* gui = H::gui(shell);
                QVERIFY(gui);
                shot(QStringLiteral("432-2408-permissions-start"));
                QTest::mouseClick(H::menuButton(shell, QStringLiteral("preset")), Qt::LeftButton);
                shot(QStringLiteral("432-2698-permissions-menus"));
                closePopup();
                H::type(gui->getOwnerPasswordCard(), QStringLiteral("Owner-Pass-2026"), QStringLiteral("Owner-Pass-2026"));
                shot(QStringLiteral("432-2105-permissions"));
                shell->getModel()->setValues(Tool::getPresetValues(Tool::Preset::PrintAndFillForms));
                shot(QStringLiteral("432-3072-permissions-forms"));
                shell->getModel()->setValues(Tool::getPresetValues(Tool::Preset::ViewAndPrint));
                gui->getRequireOpenCheck()->click();
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Open-Pass-2026"), QStringLiteral("Open-Pass-2026"));
                shot(QStringLiteral("432-3377-permissions-open-password"));
                clearCard(gui->getOpenPasswordCard());
                H::type(gui->getOpenPasswordCard(), QStringLiteral("Owner-Pass-2026"), QStringLiteral("Owner-Pass-2026"));
                shot(QStringLiteral("432-3721-permissions-same-password"));
                clearCard(gui->getOpenPasswordCard());
                gui->getRequireOpenCheck()->click();
                shell->getModel()->setValues(Tool::getPresetValues(Tool::Preset::AllowEverything));
                shot(QStringLiteral("432-4069-permissions-all-allowed"));
                shell->getModel()->setValues(Tool::getPresetValues(Tool::Preset::ViewAndPrint));
                runAndShotResult(shell, QStringLiteral("432-4388-permissions-done"));
            }

            // Restrict Permissions over an encrypted document opened by its user password (448:14765)
            {
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Encrypted_Report_2.pdf"), H::printingOnly()));
                QVERIFY(tab);
                PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID_PERMISSIONS));
                QVERIFY(shell);
                shot(QStringLiteral("448-14765-permissions-authorize"));
                if (QPointer<pdf::PDFMessageBox> dialog = shell->getPermissionDialog())
                {
                    dialog->reject();
                    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return !dialog; }));
                }
            }

            // Remove Password (433:5022 ...)
            {
                pdf::PDFAppDocumentTab* plainTab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plain_Report.pdf"), 1));
                QVERIFY(H::openTool(fixture, plainTab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK)));
                shot(QStringLiteral("433-5671-unlock-not-encrypted"));

                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Encrypted_Report_3.pdf"), H::printingOnly()));
                QVERIFY(tab);
                PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK));
                QVERIFY(shell);
                AppToolShellFixture::settle();
                QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog());
                QVERIFY(dialog);
                shot(QStringLiteral("433-5210-unlock-authorize"));
                QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("wrong-password"));
                QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
                shot(QStringLiteral("433-5442-unlock-authorize-denied"));
                dialog->getPasswordField()->setText(QString());
                QTest::keyClicks(dialog->getPasswordField(), H::owner());
                QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
                QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return !dialog; }));
                shot(QStringLiteral("433-5022-unlock"));
                runAndShotResult(shell, QStringLiteral("433-5999-unlock-done"));
            }

            // Encrypt with Certificate (433:3282, 433:3742, 433:3972, 433:3049, 433:4426, 433:4721)
            {
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report_3.pdf"), 2));
                PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_CERTIFICATE));
                PDFAppToolGUIEncryption* gui = H::gui(shell);
                QVERIFY(gui && gui->getCertificatePart());
                shot(QStringLiteral("433-3282-certificate-encrypt-empty"));
                QVERIFY(!pdftest::AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"), 2048).isEmpty());
                gui->getCertificatePart()->refresh();
                shot(QStringLiteral("433-3742-certificate-encrypt-start"));
                QTest::mouseClick(H::menuButton(shell, QStringLiteral("scope")), Qt::LeftButton);
                shot(QStringLiteral("433-3480-certificate-encrypt-menus"));
                closePopup();
                pdf::PDFLineEdit* field = gui->getCertificatePart()->getPasswordCard()->getPasswordField();
                field->setFocus();
                QTest::keyClicks(field, QStringLiteral("wrong"));
                QVariantMap parameters;
                std::vector<pdf::PDFCertificateEntry> certificates;
                QString error;
                QVERIFY(!gui->getCertificatePart()->prepareRun(&parameters, &certificates, &error));
                shot(QStringLiteral("433-3972-certificate-encrypt-wrong-password"));
                field->setText(QString());
                QTest::keyClicks(field, QStringLiteral("pfx-password"));
                QVERIFY(gui->getCertificatePart()->prepareRun(&parameters, &certificates, &error));
                parameters.clear();
                shot(QStringLiteral("433-3049-certificate-encrypt"));
                gui->getPermissionToggle()->click();
                shot(QStringLiteral("433-4426-certificate-encrypt-permissions"));
                gui->getPermissionToggle()->click();
                field->setText(QString());
                QTest::keyClicks(field, QStringLiteral("pfx-password"));
                runAndShotResult(shell, QStringLiteral("433-4721-certificate-encrypt-done"));
            }

            // Remove Password of a document encrypted for a certificate (433:5811)
            {
                const QString pfxPath = pdftest::AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_open.pfx"), QStringLiteral("Jane Novak"), QString(), 1024);
                QVERIFY(!pfxPath.isEmpty());
                const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("certificate-source.pdf"), 2);
                pdf::PDFDocumentReader sourceReader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
                const pdf::PDFDocument document = sourceReader.readFromFile(source);
                pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
                settings.algorithm = pdf::PDFSecurityHandlerFactory::Certificate;
                settings.permissions = H::printingOnly();
                QFile pfx(pfxPath);
                QVERIFY(pfx.open(QIODevice::ReadOnly));
                settings.certificate.pkcs12 = pfx.readAll();
                settings.certificate.pkcs12fileName = QFileInfo(pfxPath).fileName();
                QString error;
                pdf::PDFSecurityHandlerPointer handler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings, &error);
                QVERIFY2(handler, qPrintable(error));
                pdf::PDFDocumentBuilder builder(&document);
                builder.setSecurityHandler(handler);
                const QString filePath = fixture.path(QStringLiteral("For_Jane.pdf"));
                pdf::PDFDocument encrypted = builder.build();
                QVERIFY(AppDocumentSamples::write(encrypted, filePath));
                pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
                QVERIFY(tab);
                QVERIFY(fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK)));
                shot(QStringLiteral("433-5811-unlock-certificate"));
            }

            QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
        }
    } });

    // Review RS5FIGMA: the frames in the details, which the review found different - the rows of the
    // key value cards are 28 high and 8 apart (430:1178, 430:1258, 433:5193); the change of the
    // permissions says the groups in one line (433:5022); Opens for of no certificate (433:3282); the
    // title of the narrow callout has two lines (433:3049, 225 x 30), never a clipped one; the card of
    // the permissions of a certificate collapsed (433:3239: padding 14 + the border, 63 high) and open
    // (433:4426: padding 8 + the border, the caption 24 with its title 10 in, the rows 2 apart)
    cases->push_back({ QStringLiteral("figma-details"), []()
    {
        pdftest::AppTestCertificateFolders folders;
        AppToolShellFixture fixture(QSize(1440, 960));

        // Remove Password of Printing only (433:5022)
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::encrypted(fixture.folder(), QStringLiteral("Project_Report.pdf"), H::printingOnly()));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, QString::fromLatin1(Tool::TOOL_ID_UNLOCK));
        PDFAppToolGUIEncryption* gui = H::gui(shell);
        QVERIFY(gui);
        for (const pdf::PDFKeyValueList* list : { gui->getNowRows(), gui->getSummaryRows() })
        {
            QVERIFY(list->getRowCount() >= 3);
            QCOMPARE(list->getRowRect(0).height(), 28);
            QCOMPARE(list->getRowRect(1).top() - list->getRowRect(0).top(), 36);
            QCOMPARE(list->getRowRect(2).top() - list->getRowRect(1).top(), 36);
        }
        QCOMPARE(gui->getChangeList()->getRow(2)->getDescription(), QStringLiteral("Editing, forms and copying are restricted now"));
        shell->cancel();

        // The rows of the menus Algorithm and Encrypt have their icons (430:1497, 433:3480: icon/key,
        // icon/file-text, icon/metadata, icon/paperclip), not empty tiles
        for (const char* toolId : { Tool::TOOL_ID_PASSWORD, Tool::TOOL_ID_CERTIFICATE })
        {
            const pdf::PDFDocumentToolParameterDefinitions definitions = pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(toolId))->getParameters();
            QStringList icons;
            for (const char* parameter : { Tool::PARAMETER_ALGORITHM, Tool::PARAMETER_SCOPE })
            {
                if (const pdf::PDFDocumentToolParameterDefinition* definition = pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(parameter)))
                {
                    for (const pdf::PDFDocumentToolParameterOption& option : definition->getOptions())
                    {
                        icons << option.iconName;
                    }
                }
            }
            const QStringList scopeIcons = { QStringLiteral("file-text"), QStringLiteral("metadata"), QStringLiteral("paperclip") };
            QCOMPARE(icons, toolId == Tool::TOOL_ID_PASSWORD ? QStringList({ QStringLiteral("key"), QStringLiteral("key"), QStringLiteral("key") }) + scopeIcons : scopeIcons);
        }
        AppToolShellFixture::settle();

        // Encrypt with Certificate without a certificate (433:3282), then with one (433:3049, 433:4426)
        pdf::PDFAppDocumentTab* plainTab = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plain_Report.pdf"), 1));
        PDFDocumentToolShell* certificateShell = H::openTool(fixture, plainTab, QString::fromLatin1(Tool::TOOL_ID_CERTIFICATE));
        PDFAppToolGUIEncryption* certificateGui = H::gui(certificateShell);
        QVERIFY(certificateGui && certificateGui->getCertificatePart());
        QCOMPARE(H::rows(certificateGui->getSummaryRows()).at(3), QStringLiteral("Opens for: Nobody selected"));
        QCOMPARE(certificateGui->getAlgorithmMeter()->getLevelText(), QStringLiteral("No certificate chosen"));
        pdf::PDFLabel* lostTitle = certificateGui->getLostCallout()->getTitleLabel();
        QCOMPARE(lostTitle->text(), QStringLiteral("Without the private key the document is lost"));
        QVERIFY(lostTitle->width() < lostTitle->fontMetrics().horizontalAdvance(lostTitle->text()));
        QCOMPARE(lostTitle->height(), 30);

        QVERIFY(!pdftest::AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QString()).isEmpty());
        certificateGui->getCertificatePart()->refresh();
        AppToolShellFixture::settle();
        QWidget* card = certificateGui->getPermissionCard();
        QVERIFY(AppToolShellFixture::waitFor([card]() { return card->isVisible(); }));
        AppToolShellFixture::settle();
        QCOMPARE(card->height(), 63);
        QCOMPARE(H::inWindow(card->findChild<QWidget*>(QStringLiteral("encryptionPermissionTitle"))).top() - H::inWindow(card).top(), 15);

        certificateGui->getPermissionToggle()->click();
        AppToolShellFixture::settle();
        const QRect cardRect = H::inWindow(card);
        const QRect captionRect = H::inWindow(card->findChild<QWidget*>(QStringLiteral("encryptionPermissionTitle")));
        const QRect firstRow = H::inWindow(certificateGui->getPermissionList()->getRow(QStringLiteral("printLow")));
        const QRect secondRow = H::inWindow(certificateGui->getPermissionList()->getRow(QStringLiteral("printHigh")));
        QCOMPARE(captionRect.left() - cardRect.left(), 19);
        QCOMPARE(firstRow.left() - cardRect.left(), 9);
        QCOMPARE(firstRow.top() - cardRect.top(), 35);
        QCOMPARE(secondRow.top() - firstRow.top(), 46);
    } });
}

void AppToolEncryptionTest::initTestCase()
{
    // No case reads or writes the certificates of the user (the certificate preset lists them):
    // the folders of the whole process are temporary, the cases of P3 nest their own in them
    pdftest::AppTestCertificateFolders::installForProcess();
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QApplication::setLayoutDirection(Qt::LeftToRight);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolEncryptionTest::run_data()
{
    QTest::addColumn<int>("index");

    const std::vector<AppToolEncryptionCase>& cases = AppToolEncryptionPackages::getCases();
    if (cases.empty())
    {
        QSKIP("The packages P2 and P3 have not registered a case yet");
    }
    for (size_t i = 0; i < cases.size(); ++i)
    {
        QTest::newRow(qPrintable(cases[i].name)) << int(i);
    }
}

void AppToolEncryptionTest::run()
{
    QFETCH(int, index);
    AppToolEncryptionPackages::getCases().at(size_t(index)).run();
}

QTEST_MAIN(AppToolEncryptionTest)

#include "tst_apptoolencryptiontest.moc"
