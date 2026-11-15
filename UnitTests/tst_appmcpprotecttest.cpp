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

// The MCP actions protect.password, protect.permissions, protect.certificate-encrypt and
// protect.unlock (step 5 of phase 2, package P4; plan 6.3, 8, 9 with R5-12, owner answer Q5.6): the
// schema without a password, the confirmation with the passwords (also by Allow), its time limit and
// password_required, Cancel = denied_user, a canceled call and a closed tab withdraw it, unlock without
// the owner = isError, keep refused for a signed document, no password in a result or an activity row.
// The runs need the cores and the GUI parts of the other packages (P1b the tool, P2 the password card
// and ChangeSecurity, P3 the certificates): those functions skip with the name of the missing package
// until the integration.

#include "appmcpstep5.h"
#include "mergetestpdf.h"

#include "pdfappbuiltintools.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcpsecretconfirmation.h"
#include "pdfappcertificatedialogs.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfdocumenttoolcatalog.h"

#include "pdffluentwidgets.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QTranslator>
#include <QtTest>

using pdftest::AppMcpFixture;
using pdftest::AppMcpStep4;
using pdftest::AppMcpStep5;

/// "~" before every text of the confirmation with the passwords (a language change in the test)
class AppMcpProtectTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextName(context ? context : "");
        if (contextName == "pdf::PDFAppMcpSecretConfirmation" && sourceText)
        {
            return QStringLiteral("~") + QString::fromUtf8(sourceText);
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

class AppMcpProtectTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void secretConfirmationContract();
    void strengthOrder();
    void toolsListWithoutSecrets();
    void docsReference();
    void secretArgumentRefused();
    void confirmationAlsoWithAllow();
    void confirmationOfKeep();
    void confirmationCards();
    void confirmationRetranslated();
    void timeoutPasswordRequired();
    void canceledCallWithdrawsDialog();
    void closedTabEndsCall();
    void oneConfirmationAtATime();
    void downgradeWarning();
    void signedDocumentKeepRefused();
    void unlockWithoutOwnerRefused();
    void passwordRunWithoutSecretInResults();
    void samePasswordStaysInDialog();
    void certificateRunWithoutSecretInResults();
    void unlockRunWithoutSecretInResults();

private:
    /// Starts the server with every permission Allow (Http: the calls of the fixture go over HTTP)
    static bool start(AppMcpFixture& fixture, bool isHttp);

    /// Opens a document of two pages (encrypted by the owner password with \p permissions, unless 0)
    static pdf::PDFAppDocumentTab* openDocument(AppMcpFixture& fixture, const QString& name, uint32_t permissions = 0);
};

void AppMcpProtectTest::initTestCase()
{
    // No test reads or writes the certificates of the user (the confirmation of
    // certificate_encrypt lists them): temporary folders for the whole process
    pdftest::AppTestCertificateFolders::installForProcess();

    // The tools of the application, as the controller of MCP sees them, and the four tools of the
    // core, while package P2 has not registered them with their GUI
    pdf::PDFAppBuiltinTools::registerAll();
    AppMcpStep5::registerTools();
}

void AppMcpProtectTest::cleanup()
{
    pdf::PDFAppMcpSecretConfirmation::setTimeout(pdf::PDFAppMcpSecretConfirmation::TIMEOUT);
}

bool AppMcpProtectTest::start(AppMcpFixture& fixture, bool isHttp)
{
    if (!fixture.setPermissions(pdf::PDFAppMcpPermission::Allow))
    {
        return false;
    }
    if (isHttp)
    {
        if (!fixture.startServer())
        {
            return false;
        }
        fixture.setTransport(pdftest::AppMcpTransport::Http);
    }
    return true;
}

pdf::PDFAppDocumentTab* AppMcpProtectTest::openDocument(AppMcpFixture& fixture, const QString& name, uint32_t permissions)
{
    return AppMcpStep4::open(fixture, AppMcpStep5::writeDocument(fixture, name, permissions));
}

void AppMcpProtectTest::secretConfirmationContract()
{
    // R5-12: the confirmation waits at most 2 minutes, as the password screen of files.open
    // (newapp_mcp.md); a call, which ends without the user, ends it Withdrawn - once, the values are
    // never given after that
    QCOMPARE(pdf::PDFAppMcpSecretConfirmation::TIMEOUT, 2 * 60 * 1000);
    QCOMPARE(pdf::PDFAppMcpSecretConfirmation::getTimeout(), 2 * 60 * 1000);

    QWidget parent;
    QPointer<pdf::PDFAppMcpSecretConfirmation> confirmation = new pdf::PDFAppMcpSecretConfirmation(nullptr, QStringLiteral("protect.password"), QVariantMap(), &parent);
    QSignalSpy ended(confirmation.data(), &pdf::PDFAppMcpSecretConfirmation::confirmationEnded);
    QCOMPARE(confirmation->getOutcome(), pdf::PDFAppMcpSecretConfirmation::Outcome::Pending);
    QVERIFY(!confirmation->isConfirmEnabled());
    QVERIFY(confirmation->takeRunValues().isEmpty());
    QVERIFY(confirmation->takeUserCertificates().empty());
    confirmation->withdraw();
    QCOMPARE(confirmation->getOutcome(), pdf::PDFAppMcpSecretConfirmation::Outcome::Withdrawn);
    QCOMPARE(ended.count(), 1);
    confirmation->withdraw();
    QCOMPARE(ended.count(), 1);
    QVERIFY(confirmation->takeRunValues().isEmpty());

    // The dialog goes by itself (deleteLater)
    AppMcpFixture::settle();
    QVERIFY(!confirmation);
}

void AppMcpProtectTest::strengthOrder()
{
    // 6.9 (the downgrade): ISO 32000-2, 7.6.4.1 deprecates RC4 and AES 128-bit (revisions 2 - 5) for
    // AES 256-bit (revision 6); the 40-bit key of revision 2 is the weakest
    using C = pdf::PDFAppMcpSecretConfirmation;
    QVERIFY(C::getMethodStrength(QStringLiteral("RC4 40-bit")) < C::getMethodStrength(QStringLiteral("RC4")));
    QVERIFY(C::getMethodStrength(QStringLiteral("RC4")) < C::getMethodStrength(QStringLiteral("AES 128-bit")));
    QVERIFY(C::getMethodStrength(QStringLiteral("AES 128-bit")) < C::getMethodStrength(QStringLiteral("AES 256-bit")));
    QCOMPARE(C::getMethodStrength(QString()), 0);
    QCOMPARE(C::getAlgorithmStrength(QStringLiteral("rc4"), false), C::getMethodStrength(QStringLiteral("RC4")));
    QCOMPARE(C::getAlgorithmStrength(QStringLiteral("aes-128"), false), C::getMethodStrength(QStringLiteral("AES 128-bit")));
    QCOMPARE(C::getAlgorithmStrength(QStringLiteral("aes-256"), false), C::getMethodStrength(QStringLiteral("AES 256-bit")));
    QCOMPARE(C::getAlgorithmStrength(QString(), true), C::getMethodStrength(QStringLiteral("AES 256-bit")));
}

void AppMcpProtectTest::toolsListWithoutSecrets()
{
    // Plan 4.1 and 6.3: the actions have the algorithm, the scope, the switches and the permissions;
    // a password and the certificate are never in the schema (Q5.6: the user types them in PDF4QT)
    AppMcpFixture fixture;
    QVERIFY(start(fixture, true));
    std::unique_ptr<pdftest::McpTestClient> client = fixture.client();
    const QJsonObject list = client->listTools();
    const QByteArray listText = QJsonDocument(list).toJson(QJsonDocument::Compact);
    QVERIFY(!listText.isEmpty());

    QJsonObject protect;
    for (const QJsonValue& value : list.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray())
    {
        if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("protect"))
        {
            protect = value.toObject();
        }
    }
    QVERIFY(!protect.isEmpty());
    const QJsonObject properties = protect.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("properties")).toObject();

    const QStringList permissions = { QStringLiteral("printLow"), QStringLiteral("printHigh"), QStringLiteral("modify"), QStringLiteral("assemble"),
                                      QStringLiteral("annotate"), QStringLiteral("fillForms"), QStringLiteral("copy") };
    const std::vector<std::pair<QString, QStringList>> expected = {
        { QStringLiteral("password"), { QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("ownerSameAsOpen"), QStringLiteral("keepRestrictions") } },
        { QStringLiteral("permissions"), QStringList({ QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("requireOpenPassword") }) + permissions },
        { QStringLiteral("certificate-encrypt"), QStringList({ QStringLiteral("scope") }) + permissions },
        { QStringLiteral("unlock"), QStringList() }
    };
    const QStringList actions = properties.value(QStringLiteral("action")).toObject().value(QStringLiteral("enum")).toVariant().toStringList();
    for (const auto& item : expected)
    {
        QVERIFY2(actions.contains(item.first), qPrintable(item.first));
        QStringList names = properties.value(item.first).toObject().value(QStringLiteral("properties")).toObject().keys();
        QStringList expectedNames = item.second;
        names.sort();
        expectedNames.sort();
        QCOMPARE(names, expectedNames);
    }

    // P2S5INT: every permission of Table 22 says in the schema what its bit allows (English, the
    // description of its parameter definition)
    const std::vector<std::pair<QString, QString>> bits = { { QStringLiteral("printLow"), QStringLiteral("(bit 3)") }, { QStringLiteral("printHigh"), QStringLiteral("(bit 12)") },
                                                            { QStringLiteral("modify"), QStringLiteral("(bit 4)") }, { QStringLiteral("assemble"), QStringLiteral("(bit 11)") },
                                                            { QStringLiteral("annotate"), QStringLiteral("(bit 6)") }, { QStringLiteral("fillForms"), QStringLiteral("(bit 9)") },
                                                            { QStringLiteral("copy"), QStringLiteral("(bit 5)") } };
    for (const QString& action : { QStringLiteral("permissions"), QStringLiteral("certificate-encrypt") })
    {
        const QJsonObject actionProperties = properties.value(action).toObject().value(QStringLiteral("properties")).toObject();
        for (const auto& [name, bit] : bits)
        {
            const QString description = actionProperties.value(name).toObject().value(QStringLiteral("description")).toString();
            QVERIFY2(description.contains(bit), qPrintable(action + QStringLiteral(".") + name + QStringLiteral(": ") + description));
        }
    }

    // No password nor certificate anywhere in the list, no writeOnly property of a Secret
    for (const char* name : { "\"openPassword\"", "\"ownerPassword\"", "\"certificatePassword\"", "\"certificate\"", "\"writeOnly\"" })
    {
        QVERIFY2(!listText.contains(name), name);
    }

    // The default result is the preselected Copy of the catalog (430:3226: Save as a copy recommended)
    for (const char* id : { "protect.password", "protect.permissions", "protect.certificate-encrypt", "protect.unlock" })
    {
        const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(id));
        QVERIFY(entry);
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*entry), QStringLiteral("save_copy"));
    }
}

void AppMcpProtectTest::docsReference()
{
    // Plan 8: pdf4qt://docs/tools/protect.md has the generated reference of the contract of the four
    // actions (PDF4QT_MCP_DOCS_DUMP=<folder> writes it there for the maintainers, as UnitTestsAppMcpContent)
    const std::optional<pdf::PDFAppMcpToolSpec> spec = pdf::PDFAppMcpCatalogTools::createToolSpec(pdf::PDFDocumentToolCategory::Protect);
    QVERIFY(spec);
    const QString dumpDirectory = qEnvironmentVariable("PDF4QT_MCP_DOCS_DUMP");
    if (!dumpDirectory.isEmpty())
    {
        QDir().mkpath(dumpDirectory);
        QFile file(QDir(dumpDirectory).filePath(QStringLiteral("protect.md")));
        QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
        QString text = pdf::PDFAppMcpContent::createToolSummary(*spec);
        for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
        {
            text += QStringLiteral("\n@@ACTION %1@@\n").arg(action.name) + pdf::PDFAppMcpContent::createActionReference(*spec, action);
        }
        file.write(text.toUtf8());
    }

    const pdf::PDFMcpResourceDefinition* resource = pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/tools/protect.md"));
    QVERIFY(resource);
    const QString text = QString::fromUtf8(resource->content);
    QVERIFY2(text.contains(pdf::PDFAppMcpContent::createToolSummary(*spec)), "the generated summary differs (PDF4QT_MCP_DOCS_DUMP)");
    int checked = 0;
    for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
    {
        if (pdf::PDFDocumentToolEncryption::findMode(QStringLiteral("protect.") + action.name))
        {
            QVERIFY2(text.contains(pdf::PDFAppMcpContent::createActionReference(*spec, action)), qPrintable(action.name));
            ++checked;
        }
    }
    QCOMPARE(checked, 4);

    // The rules of the passwords for the assistant (6.3): typed only in PDF4QT, password_required
    QVERIFY(text.contains(QStringLiteral("password_required")));
    for (const char* uri : { "pdf4qt://docs/security.md", "pdf4qt://docs/errors.md" })
    {
        const pdf::PDFMcpResourceDefinition* document = pdf::PDFAppMcpContent::findStaticResource(QString::fromLatin1(uri));
        QVERIFY(document);
        QVERIFY2(QString::fromUtf8(document->content).contains(QStringLiteral("password_required")), uri);
    }
}

void AppMcpProtectTest::secretArgumentRefused()
{
    // A password sent by an assistant is refused by the strict schema and never echoed (S5.2); no
    // confirmation opens
    AppMcpFixture fixture;
    QVERIFY(start(fixture, true));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Report_protected.pdf")) } });
    for (const QString& name : { QStringLiteral("openPassword"), QStringLiteral("ownerPassword") })
    {
        const QJsonObject parameters({ { name, AppMcpStep5::getCanary() } });
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), parameters, common));
        QVERIFY(result);
        QVERIFY(result->isError || result->kind == pdf::PDFMcpCallResult::Kind::Error);
        QVERIFY2(!AppMcpStep5::resultText(*result).contains(AppMcpStep5::getCanary().toUtf8()), qPrintable(name));
        QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>());
    }
    QVERIFY(!AppMcpStep5::activityText(fixture).contains(AppMcpStep5::getCanary().toUtf8()));
    QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("Report_protected.pdf"))));
}

void AppMcpProtectTest::confirmationAlsoWithAllow()
{
    // Q5.6, R5-12: with every permission Allow the confirmation with the password opens anyway (the
    // tool cannot run without it) - the variant Save a copy of 958:27539 with the facts of the asked
    // protection (plan 4.1 defaults: AES 256-bit, Everything, the owner password same as open -
    // 430:1068); Deny ends the call as denied_user and nothing is written
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Report_protected.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QVERIFY(!call->isFinished());
    QVERIFY(tab->isAncestorOf(dialog));

    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to save a copy as Report_protected.pdf"));
    const QStringList facts = AppMcpStep5::facts(dialog);
    QVERIFY2(facts.contains(QStringLiteral("Encryption: AES 256-bit · Everything")), qPrintable(facts.join(QLatin1Char('|'))));
    QVERIFY2(facts.contains(QStringLiteral("Open password: Required")), qPrintable(facts.join(QLatin1Char('|'))));
    QVERIFY2(facts.contains(QStringLiteral("Owner password: Same as open")), qPrintable(facts.join(QLatin1Char('|'))));
    QVERIFY(dialog->getOpenPasswordCard());
    QCOMPARE(dialog->getOpenPasswordCard()->getKind(), pdf::PDFAppToolPasswordCard::Kind::Open);
    QVERIFY(!dialog->getOwnerPasswordCard());
    QVERIFY(!dialog->getCertificateList());
    QVERIFY(!dialog->isConfirmEnabled());
    QVERIFY(!dialog->getAllowButton()->isEnabled());

    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("The user denied")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));
    AppMcpFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(!QFileInfo::exists(copyPath));
}

void AppMcpProtectTest::confirmationOfKeep()
{
    // keep: the variant of a change (940:133693) - one row of the tool with the asked protection
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QJsonObject parameters({ { QStringLiteral("algorithm"), QStringLiteral("aes-128") }, { QStringLiteral("ownerSameAsOpen"), false } });
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), parameters, common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);

    const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("protect.password"));
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to change Report.pdf"));
    QCOMPARE(dialog->getRowTexts(), QStringList({ entry->getTitle() }));

    // ownerSameAsOpen false: the owner password has its own card (430:2228); both cards know AES 128-bit
    QVERIFY(dialog->getOpenPasswordCard());
    QVERIFY(dialog->getOwnerPasswordCard());
    QCOMPARE(dialog->getOwnerPasswordCard()->getKind(), pdf::PDFAppToolPasswordCard::Kind::Owner);

    // Escape is Cancel as in every confirmation
    QTest::keyClick(dialog, Qt::Key_Escape);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));
}

void AppMcpProtectTest::confirmationCards()
{
    // Restrict Permissions: the owner password always (432:2105), the open password only with
    // requireOpenPassword (432:3377). Encrypt with Certificate: the list and the password of the .pfx
    // file (433:3049); a restricted permission warns that the user cannot remove the protection (R5-21)
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });

    struct Case
    {
        QString action;
        QJsonObject parameters;
        bool hasOpen;
        bool hasOwner;
        bool hasCertificate;
        bool hasLockout;
    };
    const std::vector<Case> cases = {
        { QStringLiteral("permissions"), QJsonObject(), false, true, false, false },
        { QStringLiteral("permissions"), QJsonObject({ { QStringLiteral("requireOpenPassword"), true } }), true, true, false, false },
        { QStringLiteral("certificate-encrypt"), QJsonObject(), false, false, true, false },
        { QStringLiteral("certificate-encrypt"), QJsonObject({ { QStringLiteral("modify"), false } }), false, false, true, true },
    };

    for (const Case& item : cases)
    {
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(item.action, item.parameters, common));
        QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
        QVERIFY2(dialog, qPrintable(item.action));
        QCOMPARE(dialog->getOpenPasswordCard() != nullptr, item.hasOpen);
        QCOMPARE(dialog->getOwnerPasswordCard() != nullptr, item.hasOwner);
        QCOMPARE(dialog->getCertificateList() != nullptr, item.hasCertificate);
        QCOMPARE(dialog->getCertificatePasswordCard() != nullptr, item.hasCertificate);
        if (item.hasCertificate)
        {
            QCOMPARE(dialog->getCertificatePasswordCard()->getKind(), pdf::PDFAppToolPasswordCard::Kind::Certificate);
        }
        QCOMPARE(dialog->getCalloutTexts().contains(QStringLiteral("You will not be able to change or remove this protection yourself")), item.hasLockout);
        QVERIFY(!dialog->isConfirmEnabled());

        // The cards are below each other without a heading (review RS5APP): their labels and the
        // names of their fields say which password it is - never "Password" twice
        const std::vector<std::pair<pdf::PDFAppToolPasswordCard*, QString>> cards = { { dialog->getOwnerPasswordCard(), QStringLiteral("Owner password") },
                                                                                      { dialog->getOpenPasswordCard(), QStringLiteral("Open password") } };
        for (const auto& [card, name] : cards)
        {
            if (card)
            {
                const QLabel* label = card->findChild<QLabel*>(QStringLiteral("passwordLabel"));
                const QLabel* repeatLabel = card->findChild<QLabel*>(QStringLiteral("repeatLabel"));
                QVERIFY(label && repeatLabel);
                QCOMPARE(label->text(), name);
                QCOMPARE(repeatLabel->text(), QStringLiteral("Repeat %1").arg(name.toLower()));
                QCOMPARE(card->getPasswordField()->accessibleName(), name);
            }
        }

        // The focus starts in the first field: the owner password is the first card of Restrict
        // Permissions (432:2105), also with the open password below it (review RS5APP)
        if (item.action == QLatin1String("permissions"))
        {
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getOwnerPasswordCard()->getPasswordField()));
        }

        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        QVERIFY(AppMcpFixture::waitResult(call));
        AppMcpFixture::settle();
        QVERIFY(!dialog);
    }
}

void AppMcpProtectTest::confirmationRetranslated()
{
    // Review RS5APP (fix wave P2S5FIXAPP, item 9): a change of the language while the confirmation is
    // open translates the labels of its cards again - Owner password / Open password and their
    // repetitions, the accessible names of the fields - and back
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("permissions"), QJsonObject({ { QStringLiteral("requireOpenPassword"), true } }), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog && dialog->getOwnerPasswordCard() && dialog->getOpenPasswordCard());

    const auto checkLabels = [&dialog](const QString& prefix)
    {
        const std::vector<std::pair<pdf::PDFAppToolPasswordCard*, QString>> cards = { { dialog->getOwnerPasswordCard(), QStringLiteral("Owner password") },
                                                                                      { dialog->getOpenPasswordCard(), QStringLiteral("Open password") } };
        for (const auto& [card, name] : cards)
        {
            QCOMPARE(card->findChild<QLabel*>(QStringLiteral("passwordLabel"))->text(), prefix + name);
            QCOMPARE(card->findChild<QLabel*>(QStringLiteral("repeatLabel"))->text(), prefix + QStringLiteral("Repeat %1").arg(name.toLower()));
            QCOMPARE(card->getPasswordField()->accessibleName(), prefix + name);
        }
    };
    checkLabels(QString());

    AppMcpProtectTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    AppMcpFixture::settle();
    QVERIFY(dialog);
    checkLabels(QStringLiteral("~"));

    QVERIFY(QCoreApplication::removeTranslator(&translator));
    AppMcpFixture::settle();
    QVERIFY(dialog);
    checkLabels(QString());

    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(call));
    AppMcpFixture::settle();
}

void AppMcpProtectTest::timeoutPasswordRequired()
{
    // R5-12: no answer in the time of the user ends the call with isError and the state
    // password_required (as files.open), the activity denied_timeout; the dialog goes
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);
    pdf::PDFAppMcpSecretConfirmation::setTimeout(300);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Out.pdf")) } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("state password_required")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedTimeout));
    AppMcpFixture::settle();
    QVERIFY(!dialog);
}

void AppMcpProtectTest::canceledCallWithdrawsDialog()
{
    // R5-12: a call canceled by the assistant closes the dialog (new + QPointer: the call ends while
    // the dialog is open); the activity says canceled
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Out.pdf")) } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("permissions"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QSignalSpy ended(dialog.data(), &pdf::PDFAppMcpSecretConfirmation::confirmationEnded);

    call->cancel(pdf::PDFMcpCancelReason::ClientCanceled);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY(result->getFirstText().contains(QStringLiteral("canceled")));
    QVERIFY(QTest::qWaitFor([&dialog]() { return dialog.isNull(); }, 5000 * AppMcpFixture::getTimeoutFactor()));
    QCOMPARE(ended.count(), 1);
    QCOMPARE(ended.at(0).at(0).value<pdf::PDFAppMcpSecretConfirmation::Outcome>(), pdf::PDFAppMcpSecretConfirmation::Outcome::Withdrawn);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));
}

void AppMcpProtectTest::closedTabEndsCall()
{
    // The tab closed while the dialog waits: the scrim and the dialog go with the tab, the call ends
    // denied_closed (no use of the deleted dialog - the request holds it by QPointer)
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);

    QVERIFY(fixture.window->closeTab(tab));
    AppMcpFixture::settle();    // The tab is deleted later (the event loop of the application)
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("closed")), qPrintable(result->getFirstText()));
    AppMcpFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedClosed));
}

void AppMcpProtectTest::oneConfirmationAtATime()
{
    // One form with a password at a time: a second call while one waits is refused as busy; after the
    // answer the next call asks again
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });

    const std::shared_ptr<pdf::PDFMcpCall> first = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);

    const std::optional<pdf::PDFMcpCallResult> second = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("permissions"), QJsonObject(), common));
    QVERIFY(second && second->isError);
    QVERIFY2(second->getFirstText().contains(QStringLiteral("already asks the user for a password")), qPrintable(second->getFirstText()));
    QVERIFY(!first->isFinished());

    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(first));

    const std::shared_ptr<pdf::PDFMcpCall> third = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("permissions"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> thirdDialog = AppMcpStep5::waitDialog();
    QVERIFY(thirdDialog);
    QVERIFY(thirdDialog != dialog);
    QTest::mouseClick(thirdDialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(third));
}

void AppMcpProtectTest::downgradeWarning()
{
    // 6.9, SECURITY_TODO 45: an assistant asking for a weaker algorithm than the current AES 256-bit
    // (revision 6) is shown to the user before the password is typed; the same algorithm is no warning
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Secret.pdf"), AppMcpStep5::getAllPermissions());
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->unlockWithOwnerPassword(AppMcpStep5::getOwnerPassword()));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });

    for (const QString& algorithm : { QStringLiteral("rc4"), QStringLiteral("aes-128"), QStringLiteral("aes-256") })
    {
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject({ { QStringLiteral("algorithm"), algorithm } }), common));
        QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
        QVERIFY2(dialog, qPrintable(call->isFinished() && call->getResult() ? call->getResult()->getFirstText() : algorithm));
        const QStringList callouts = dialog->getCalloutTexts();
        QCOMPARE(callouts.contains(QStringLiteral("The new encryption is weaker than the current one")), algorithm != QLatin1String("aes-256"));
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        QVERIFY(AppMcpFixture::waitResult(call));
        AppMcpFixture::settle();
    }
}

void AppMcpProtectTest::signedDocumentKeepRefused()
{
    // B80, O31: the four tools rewrite the whole file (LostCopyOnly) - keep and overwrite of a signed
    // document are refused before any question; save_copy asks with the lost signatures named
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("S", 2, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >>", { { 1, "/Annots [60 0 R]" } });
    pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    const QString filePath = fixture.path(QStringLiteral("Signed.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QFile::WriteOnly) && file.write(pdf.write()) > 0);
    file.close();
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, filePath);
    QVERIFY(tab);

    for (const QString& mode : { QStringLiteral("keep"), QStringLiteral("overwrite") })
    {
        const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), mode } });
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("The document is signed")), qPrintable(result->getFirstText()));
        QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>());
    }

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Signed_protected.pdf")) } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QVERIFY2(AppMcpStep5::facts(dialog).contains(QStringLiteral("Signatures: 1 no longer valid in the written file")), qPrintable(AppMcpStep5::facts(dialog).join(QLatin1Char('|'))));
    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(call));
}

void AppMcpProtectTest::unlockWithoutOwnerRefused()
{
    // 6.3, R5-4: Remove Password of a document opened without its owner password is refused with the
    // text of the unlock by the user (never a password from the assistant), no question; ISO 32000-2
    // 7.6.4.1 - only the owner changes the security, also of a document, which restricts nothing
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Secret.pdf"), AppMcpStep5::getAllPermissions());
    QVERIFY(tab);
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    if (pdf::PDFAppDocumentPermissions::isAllowed(document, pdf::PDFAppPermissionRequest::createTool(QStringLiteral("protect.unlock"))))
    {
        QSKIP("Needs ChangeSecurity of package P2 (R5-4): integration");
    }

    for (const QString& action : { QStringLiteral("unlock"), QStringLiteral("password") })
    {
        const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(action, QJsonObject(), common));
        QVERIFY(result && result->isError);
        QCOMPARE(result->getFirstText(), pdf::PDFAppDocumentPermissions::getMcpRefusalText(document, pdf::PDFAppPermissionRequest::createTool(QStringLiteral("protect.") + action)));
        QVERIFY(!result->getFirstText().contains(AppMcpStep5::getOwnerPassword()));
        QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>());
    }
    QVERIFY(tab->getDocument()->getDocument()->getStorage().getSecurityHandler()->getMode() != pdf::EncryptionMode::None);
}

void AppMcpProtectTest::passwordRunWithoutSecretInResults()
{
    // Q5.6, S5.2: the user types the password in PDF4QT, the run writes a copy, which opens only by
    // it; the password is in no result, no text block, no structured content and no activity row
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Report_protected.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("password"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    if (!AppMcpStep5::isCardImplemented(dialog->getOpenPasswordCard()))
    {
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        AppMcpFixture::waitResult(call);
        QSKIP("Needs PDFAppToolPasswordCard of package P2: integration");
    }

    AppMcpStep5::type(dialog->getOpenPasswordCard(), AppMcpStep5::getCanary());
    QVERIFY(dialog->isConfirmEnabled());
    AppMcpStep5::confirm(dialog);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call, 60000);
    QVERIFY(result);
    if (result->isError && result->getFirstText().contains(QStringLiteral("not implemented")))
    {
        QSKIP("Needs PDFDocumentToolEncryption::execute of package P1b: integration");
    }
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    // The result says what was done (plan 4.1: the data of MCP), never the password
    const QJsonObject content = AppMcpStep4::content(*result, QStringLiteral("password"));
    QCOMPARE(content.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("algorithm")).toString(), QStringLiteral("aes-256"));
    QCOMPARE(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("openPasswordRequired")).toBool(), true);
    QVERIFY(!AppMcpStep5::resultText(*result).contains(AppMcpStep5::getCanary().toUtf8()));
    QVERIFY(!AppMcpStep5::activityText(fixture).contains(AppMcpStep5::getCanary().toUtf8()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Allowed));

    // ISO 32000-2 7.6.4: the copy needs the password, the tab is unchanged (save_copy)
    QString reason;
    QVERIFY2(AppMcpStep5::opensWithPassword(copyPath, AppMcpStep5::getCanary(), &reason), qPrintable(reason));
    QVERIFY(!AppMcpStep5::opensWithoutPassword(copyPath));
    QCOMPARE(tab->getDocument()->getDocument()->getStorage().getSecurityHandler()->getMode(), pdf::EncryptionMode::None);
    AppMcpFixture::settle();
    QVERIFY(!dialog);
}

void AppMcpProtectTest::samePasswordStaysInDialog()
{
    // 432:3721: the owner password equal to the open password is refused by the validation of the
    // tool at its card - the dialog stays open and the call waits (the user fixes it in PDF4QT)
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("permissions"), QJsonObject({ { QStringLiteral("requireOpenPassword"), true } }), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    if (!AppMcpStep5::isCardImplemented(dialog->getOwnerPasswordCard()))
    {
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        AppMcpFixture::waitResult(call);
        QSKIP("Needs PDFAppToolPasswordCard of package P2: integration");
    }

    AppMcpStep5::type(dialog->getOwnerPasswordCard(), AppMcpStep5::getCanary());
    AppMcpStep5::type(dialog->getOpenPasswordCard(), AppMcpStep5::getCanary());
    QVERIFY(dialog->isConfirmEnabled());
    AppMcpStep5::confirm(dialog);
    AppMcpFixture::settle();
    if (!dialog)
    {
        const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY(!AppMcpStep5::resultText(*result).contains(AppMcpStep5::getCanary().toUtf8()));
        if (result->getFirstText().contains(QStringLiteral("not implemented")))
        {
            QSKIP("Needs PDFDocumentToolEncryption::validate of package P1b: integration");
        }
        QFAIL("The same owner and open password was not refused in the dialog");
    }
    QCOMPARE(dialog->getOutcome(), pdf::PDFAppMcpSecretConfirmation::Outcome::Pending);
    QVERIFY(!call->isFinished());
    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(call));
}

void AppMcpProtectTest::certificateRunWithoutSecretInResults()
{
    // Q5.6, 4.5: the user chooses the certificate and types the password of its .pfx file in PDF4QT;
    // a wrong password stays in the dialog (433:3972), the right one encrypts the copy for the
    // certificate (adbe.pkcs7.s5) - no password nor certificate path in any result or activity row
    pdftest::AppTestCertificateFolders folder;
    const QString pfxPath = pdftest::AppTestCertificateFolders::create(folder.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), AppMcpStep5::getCanary(), 2048);
    QVERIFY(!pfxPath.isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Contract.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Contract_encrypted.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("certificate-encrypt"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getCertificateList());
    if (dialog->getCertificateList()->getCertificates().empty() || !AppMcpStep5::isCardImplemented(dialog->getCertificatePasswordCard()))
    {
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        AppMcpFixture::waitResult(call);
        QSKIP("Needs the certificate list and provider of package P3: integration");
    }
    QCOMPARE(QFileInfo(dialog->getCertificateList()->getChosenFilePath()).fileName(), QStringLiteral("jane_novak.pfx"));

    // A wrong password: the dialog stays, the call waits
    AppMcpStep5::type(dialog->getCertificatePasswordCard(), QStringLiteral("wrong-password"));
    AppMcpStep5::confirm(dialog);
    AppMcpFixture::settle();
    QVERIFY(dialog);
    QCOMPARE(dialog->getOutcome(), pdf::PDFAppMcpSecretConfirmation::Outcome::Pending);
    QVERIFY(!call->isFinished());

    dialog->getCertificatePasswordCard()->clear();
    AppMcpStep5::type(dialog->getCertificatePasswordCard(), AppMcpStep5::getCanary());
    AppMcpStep5::confirm(dialog);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call, 60000);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject content = AppMcpStep4::content(*result, QStringLiteral("certificate-encrypt"));
    QCOMPARE(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("algorithm")).toString(), QStringLiteral("certificate"));
    for (const QByteArray& text : { AppMcpStep5::resultText(*result), AppMcpStep5::activityText(fixture) })
    {
        QVERIFY(!text.contains(AppMcpStep5::getCanary().toUtf8()));
        QVERIFY(!text.contains("jane_novak.pfx"));
    }

    // ISO 32000-2, 7.6.5: the copy is encrypted by the public-key handler, it never opens freely
    QVERIFY(!AppMcpStep5::opensWithoutPassword(copyPath));
    QVERIFY(AppMcpStep5::readFile(copyPath).contains("/Adobe.PubSec"));
}

void AppMcpProtectTest::unlockRunWithoutSecretInResults()
{
    // Remove Password of a document unlocked by its owner password in PDF4QT: no question with a
    // password (no Secret), the copy opens without a password (4.4), the owner password of the document
    // is in no result. The user always confirms it, also with every permission Allow (owner
    // 2026-11-15): a denied question writes nothing
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Secret.pdf"), AppMcpStep4::allowedWithout(5));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->unlockWithOwnerPassword(AppMcpStep5::getOwnerPassword()));

    const QString copyPath = fixture.path(QStringLiteral("Secret_unprotected.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    const std::optional<pdf::PDFMcpCallResult> denied = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("unlock"), QJsonObject(), common));
    QVERIFY(denied);
    QVERIFY(denied->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    QVERIFY(fixture.confirmations()->shown.back()->isAlwaysConfirmed);
    QCOMPARE(fixture.confirmations()->shown.back()->kind, pdf::PDFAppMcpConfirmationKind::SaveCopy);
    QVERIFY(!QFileInfo::exists(copyPath));

    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("unlock"), QJsonObject(), common));
    QVERIFY(result);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>());
    if (result->isError && result->getFirstText().contains(QStringLiteral("not implemented")))
    {
        QSKIP("Needs PDFDocumentToolEncryption::execute of package P1b: integration");
    }
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(!AppMcpStep5::resultText(*result).contains(AppMcpStep5::getOwnerPassword().toUtf8()));
    QVERIFY(!AppMcpStep5::activityText(fixture).contains(AppMcpStep5::getOwnerPassword().toUtf8()));
    QVERIFY(AppMcpStep5::opensWithoutPassword(copyPath));
}

QTEST_MAIN(AppMcpProtectTest)

#include "tst_appmcpprotecttest.moc"
