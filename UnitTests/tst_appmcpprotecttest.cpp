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
//
// Step 6 of phase 2 (package P6; plan 6.5, 8, R6-8, R6-10, R6-20, R6-27, Q6.7): one test per gate of
// MCP - Sign always confirmed with the certificate chosen and its password typed in PDF4QT, Timestamp
// confirmed before anything is sent and only to an authority of the list of the user (a local authority
// on the loopback, never the internet), Sign by Hand always confirmed with its drawing on its page,
// Redact by the permissions only, the certificates read only; the verified file of save_file, the
// permission check by the parameters, the signer and the later revisions of document.get_signatures.

#include "appmcpstep5.h"
#include "appmcpstep6.h"
#include "mergetestpdf.h"

#include "pdfappbuiltintools.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcpsecretconfirmation.h"
#include "pdfappcertificatedialogs.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolgui_certificates.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolusage.h"
#include "pdfform.h"

#include "pdffluentwidgets.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QTranslator>
#include <QtTest>

#include <atomic>

using pdftest::AppMcpFixture;
using pdftest::AppMcpStep4;
using pdftest::AppMcpStep5;
using pdftest::AppMcpStep6;

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

/// Sign with Certificate, which records the context of its run (the parent window of the PIN dialog of
/// the store of Windows, Q6.2) and fails without signing
class AppMcpProtectRecordingSignTool : public pdf::PDFDocumentToolSignature
{
public:
    AppMcpProtectRecordingSignTool() : pdf::PDFDocumentToolSignature(Mode::Sign) { }

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control,
                                               pdf::PDFProgressSink* progress) const override
    {
        Q_UNUSED(parameters);
        Q_UNUSED(control);
        Q_UNUSED(progress);
        s_parentWindow = context.parentWindow;
        s_isRun = true;
        return pdf::PDFDocumentToolResult::createFailed(QStringLiteral("Recorded."));
    }

    static inline std::atomic<quintptr> s_parentWindow = 0;
    static inline std::atomic<bool> s_isRun = false;
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

    // Step 6 of phase 2 (package P6)
    void signConfirmedWithCertificateChoice();
    void signRunWritesVerifiedFile();
    void signEntryPermissionsByParameters();
    void timestampConfirmedBeforeAuthority();
    void signByHandConfirmedWithDrawing();
    void redactByPermissionsWithoutSecrets();
    void certificatesReadOnly();
    void signaturesSignerAndLaterRevisions();
    void signaturesClaimedDate_data();
    void signaturesClaimedDate();
    void signRunParentWindow();
    void certificateEncryptSystemStore();

    // Review RS6APP
    void signInvalidPreselectionKeepsAuthorityGate();

private:
    /// Starts the server with every permission Allow (Http: the calls of the fixture go over HTTP)
    static bool start(AppMcpFixture& fixture, bool isHttp);

    /// Opens a document of two pages (encrypted by the owner password with \p permissions, unless 0)
    static pdf::PDFAppDocumentTab* openDocument(AppMcpFixture& fixture, const QString& name, uint32_t permissions = 0);

    /// The system store of the trusted certificates of a run with an authority: empty (never the store of the machine)
    static pdf::PDFCertificateEntries noSystemCertificates() { return pdf::PDFCertificateEntries(); }
};

void AppMcpProtectTest::initTestCase()
{
    // No test reads or writes the certificates of the user (the confirmation of
    // certificate_encrypt lists them): temporary folders for the whole process
    pdftest::AppTestCertificateFolders::installForProcess();
    pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(&AppMcpProtectTest::noSystemCertificates);

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

    // Step 6 (plan 8 with Q6.1, Q6.3, Q6.7, R6-20): sign has the preselection of the certificate, the
    // format, the type, the field, the reason, the contact and the authority - no password, no appearance
    // (only invisible) and no drawing; sign-by-hand the JSON of its elements (no drawing of the panels);
    // certificates nothing (a list); redact its areas, its search and its options
    const QStringList permissions = { QStringLiteral("printLow"), QStringLiteral("printHigh"), QStringLiteral("modify"), QStringLiteral("assemble"),
                                      QStringLiteral("annotate"), QStringLiteral("fillForms"), QStringLiteral("copy") };
    const std::vector<std::pair<QString, QStringList>> expected = {
        { QStringLiteral("password"), { QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("ownerSameAsOpen"), QStringLiteral("keepRestrictions") } },
        { QStringLiteral("permissions"), QStringList({ QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("requireOpenPassword") }) + permissions },
        { QStringLiteral("certificate-encrypt"), QStringList({ QStringLiteral("scope") }) + permissions },
        { QStringLiteral("unlock"), QStringList() },
        { QStringLiteral("sign"), { QStringLiteral("certificate"), QStringLiteral("format"), QStringLiteral("type"), QStringLiteral("field"), QStringLiteral("reason"),
                                    QStringLiteral("contactInfo"), QStringLiteral("tsaUrl") } },
        { QStringLiteral("timestamp"), { QStringLiteral("tsaUrl") } },
        { QStringLiteral("sign-by-hand"), { QStringLiteral("elements") } },
        { QStringLiteral("certificates"), QStringList() },
        { QStringLiteral("redact"), { QStringLiteral("useMarks"), QStringLiteral("areas"), QStringLiteral("search"), QStringLiteral("searchRegex"), QStringLiteral("searchCaseSensitive"),
                                      QStringLiteral("searchWholeWords"), QStringLiteral("searchAcrossLineBreaks"), QStringLiteral("pages"), QStringLiteral("fillColor"),
                                      QStringLiteral("noFill"), QStringLiteral("keepTitle"), QStringLiteral("keepMetadata"), QStringLiteral("keepOutline") } }
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

    // No password anywhere in the list, no writeOnly property of a Secret, no appearance nor drawing of a
    // signature; a certificate only as the preselection of sign by its fingerprint (6.5)
    for (const char* name : { "\"openPassword\"", "\"ownerPassword\"", "\"certificatePassword\"", "\"writeOnly\"", "\"appearance\"", "\"drawing\"" })
    {
        QVERIFY2(!listText.contains(name), name);
    }
    QVERIFY(properties.value(QStringLiteral("sign")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("certificate")).toObject()
                .value(QStringLiteral("description")).toString().contains(QStringLiteral("fingerprint")));

    // The JSON of a drawing of an assistant and of the areas of Redact are structured data bounded by their
    // counts (plan 7.2, ASan D1 of step 6): the elements the longest structured String of MCP (4 Mi characters,
    // the longest string of a request), the areas 10 000 areas of 200 characters (protect.md, errors.md)
    QCOMPARE(properties.value(QStringLiteral("sign-by-hand")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("elements")).toObject()
                 .value(QStringLiteral("maxLength")).toInt(), 4 * 1024 * 1024);
    QCOMPARE(properties.value(QStringLiteral("redact")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("areas")).toObject()
                 .value(QStringLiteral("maxLength")).toInt(), 10000 * 200);

    // The default result is the preselected Copy of the catalog (430:3226: Save as a copy recommended);
    // step 6 (plan 8): the signed, timestamped and redacted files save_file, the drawing keep
    const std::vector<std::pair<const char*, QString>> defaults = { { "protect.password", QStringLiteral("save_copy") }, { "protect.permissions", QStringLiteral("save_copy") },
                                                                    { "protect.certificate-encrypt", QStringLiteral("save_copy") }, { "protect.unlock", QStringLiteral("save_copy") },
                                                                    { "protect.sign", QStringLiteral("save_file") }, { "protect.timestamp", QStringLiteral("save_file") },
                                                                    { "protect.redact", QStringLiteral("save_file") }, { "protect.sign-by-hand", QStringLiteral("keep") } };
    for (const auto& [id, result] : defaults)
    {
        const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(id));
        QVERIFY(entry);
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*entry), result);
    }

    // protect.certificates is a list: no result (protect.md)
    const pdf::PDFDocumentToolCatalogEntry* certificates = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("protect.certificates"));
    QVERIFY(certificates);
    QVERIFY(pdf::PDFAppMcpCatalogTools::getToolResultValues(*certificates, pdf::PDFDocumentToolRegistry::getTool(certificates->getId()).get()).isEmpty());

    // R6-10: the result of Sign and Timestamp is only a file without Then continue with; Redact offers
    // the next tools (Q6.16) - MCP offers no next tool, the property belongs to the result dialog
    QVERIFY(!pdf::PDFDocumentToolUsage::isOfferingSuccessors(QStringLiteral("protect.sign")));
    QVERIFY(!pdf::PDFDocumentToolUsage::isOfferingSuccessors(QStringLiteral("protect.timestamp")));
    QVERIFY(pdf::PDFDocumentToolUsage::isOfferingSuccessors(QStringLiteral("protect.redact")));
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

    // Every action of the category in the order of its table in protect.md - the four of the encryption
    // (step 5), the five of step 6 and the two of the first batch - with its generated reference
    QStringList names;
    for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
    {
        QVERIFY2(text.contains(pdf::PDFAppMcpContent::createActionReference(*spec, action)), qPrintable(action.name));
        names << action.name;
    }
    QCOMPARE(names, QStringList({ QStringLiteral("password"), QStringLiteral("permissions"), QStringLiteral("certificate-encrypt"), QStringLiteral("unlock"),
                                  QStringLiteral("sign"), QStringLiteral("timestamp"), QStringLiteral("sign-by-hand"), QStringLiteral("certificates"),
                                  QStringLiteral("redact"), QStringLiteral("sanitize"), QStringLiteral("remove-links") }));

    // protect.certificates refuses result and path (protect.md): its reference does not offer them as
    // common parameters, the other actions do
    for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
    {
        const QString reference = pdf::PDFAppMcpContent::createActionReference(*spec, action);
        if (action.name == QLatin1String("certificates"))
        {
            QVERIFY2(reference.contains(QStringLiteral("- Common parameters: none\n")), qPrintable(reference.left(400)));
        }
        else
        {
            QVERIFY2(reference.contains(QStringLiteral("`result`, `path`")), qPrintable(action.name));
        }
    }

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

    // P2NOPOLL: the report of the cancellation (queued to the host) ends it - no check in intervals
    QVERIFY(!call->isFinished());
    QCoreApplication::sendPostedEvents(fixture.host(), QEvent::MetaCall);
    QVERIFY(call->isFinished());
    QCOMPARE(ended.count(), 1);
    const std::optional<pdf::PDFMcpCallResult> result = call->getResult();
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

    // RS6APP: the slot of destroyed of the request only forgets the dialog - the call ends from the event
    // loop, not inside of the destructor of the dialog (a slot connected later sees it open)
    std::optional<bool> isFinishedAtDestruction;
    QObject context;
    QObject::connect(dialog.data(), &QObject::destroyed, &context, [&call, &isFinishedAtDestruction]() { isFinishedAtDestruction = call->isFinished(); });

    QVERIFY(fixture.window->closeTab(tab));
    AppMcpFixture::settle();    // The tab is deleted later (the event loop of the application)
    QVERIFY(isFinishedAtDestruction.has_value());
    QVERIFY(!*isFinishedAtDestruction);
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

void AppMcpProtectTest::signConfirmedWithCertificateChoice()
{
    // R6-10, Q6.7, plan 6.5: protect.sign always opens the confirmation of PDF4QT, also with every
    // permission Allow - the variant of a copy (958:27539) with the facts of the invisible signature and
    // the reason and the contact of the assistant, the list of the certificates in the signing mode with
    // the certificate of the assistant only preselected, the card of the password of its .pfx file.
    // Cancel ends the call as denied_user and nothing is written
    pdftest::AppTestCertificateFolders folders;
    const QString firstPath = AppMcpStep6::createCertificate(folders, QStringLiteral("aaron_first.pfx"), QStringLiteral("Aaron First"));
    const QString secondPath = AppMcpStep6::createCertificate(folders, QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"));
    QVERIFY(!firstPath.isEmpty() && !secondPath.isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    QVERIFY(AppMcpStep6::cacheCertificate(fixture, firstPath));
    QVERIFY(AppMcpStep6::cacheCertificate(fixture, secondPath));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Contract.pdf"));
    QVERIFY(tab);

    // The assistant preselects the certificate, which is not the first one of the list
    std::vector<pdf::PDFAppToolCertificateItem> choosable;
    for (const pdf::PDFAppToolCertificateItem& item : pdf::PDFAppToolCertificateProvider(fixture.context.get()).getCertificates(pdf::PDFAppToolCertificateUsage::Signing))
    {
        if (item.isChoosable())
        {
            choosable.push_back(item);
        }
    }
    QCOMPARE(int(choosable.size()), 2);
    const pdf::PDFAppToolCertificateItem preselected = choosable.back();
    QVERIFY(!preselected.fingerprint.isEmpty());

    const QString copyPath = fixture.path(QStringLiteral("Contract_SIGNED.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const QJsonObject parameters({ { QStringLiteral("certificate"), preselected.fingerprint }, { QStringLiteral("reason"), QStringLiteral("I approve this contract") },
                                   { QStringLiteral("contactInfo"), QStringLiteral("jane@example.com") } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), parameters, common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY2(dialog, qPrintable(call->isFinished() && call->getResult() ? call->getResult()->getFirstText() : QStringLiteral("no dialog")));
    QVERIFY(dialog->isSigning());
    QVERIFY(!call->isFinished());
    QVERIFY(tab->isAncestorOf(dialog));

    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to save a copy as Contract_SIGNED.pdf"));
    const QStringList facts = AppMcpStep5::facts(dialog);
    for (const QString& fact : { QStringLiteral("Signature: Signature · PAdES · invisible"), QStringLiteral("Reason: I approve this contract"), QStringLiteral("Contact: jane@example.com") })
    {
        QVERIFY2(facts.contains(fact), qPrintable(facts.join(QLatin1Char('|'))));
    }

    pdf::PDFAppToolCertificateList* list = dialog->getCertificateList();
    QVERIFY(list);
    QCOMPARE(list->getUsage(), pdf::PDFAppToolCertificateUsage::Signing);
    QCOMPARE(list->getChosenFilePath(), preselected.filePath);
    QVERIFY(dialog->getCertificatePasswordCard());
    QVERIFY(dialog->getCertificatePasswordCard()->isVisibleTo(dialog));
    QVERIFY(!dialog->isConfirmEnabled());

    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("The user denied")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));
    QVERIFY(!QFileInfo::exists(copyPath));
}

void AppMcpProtectTest::signRunWritesVerifiedFile()
{
    // Q6.7, R6-27, plan 4.4: the user types the password of the .pfx file in PDF4QT (a wrong one stays in
    // the dialog, 433:3972); the run signs invisibly in PAdES (Q6.1) and save_file writes exactly the
    // verified file - the exact bytes of the file of the document followed by the signature (ISO 32000-2,
    // 7.5.6) -, the tab and its file stay as they are; the password and the file of the certificate are in
    // no result and no activity row; the signer of the written file is the certificate of the user
    pdftest::AppTestCertificateFolders folders;
    QVERIFY(!AppMcpStep6::createCertificate(folders, QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak")).isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    const QString filePath = AppMcpStep5::writeDocument(fixture, QStringLiteral("Contract.pdf"));
    const QByteArray original = AppMcpStep5::readFile(filePath);
    QVERIFY(!original.isEmpty());
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, filePath);
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Contract_SIGNED.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QVERIFY(AppMcpStep5::isCardImplemented(dialog->getCertificatePasswordCard()));
    QCOMPARE(QFileInfo(dialog->getCertificateList()->getChosenFilePath()).fileName(), QStringLiteral("jane_novak.pfx"));

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

    const QJsonObject content = AppMcpStep4::content(*result, QStringLiteral("sign"));
    QCOMPARE(content.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(QFileInfo(content.value(QStringLiteral("path")).toString()).canonicalFilePath(), QFileInfo(copyPath).canonicalFilePath());
    QCOMPARE(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("subFilter")).toString(), QStringLiteral("ETSI.CAdES.detached"));
    for (const QByteArray& text : { AppMcpStep5::resultText(*result), AppMcpStep5::activityText(fixture) })
    {
        QVERIFY(!text.contains(AppMcpStep5::getCanary().toUtf8()));
        QVERIFY(!text.contains("jane_novak.pfx"));
    }

    // The verified file: the bytes of the file of the document, then the incremental update
    const QByteArray written = AppMcpStep5::readFile(copyPath);
    QVERIFY(written.size() > original.size());
    QVERIFY(written.startsWith(original));
    QVERIFY(written.contains("/ETSI.CAdES.detached"));

    // The tab and its file are unchanged
    QCOMPARE(pdf::PDFForm::getSignatureCount(tab->getDocument()->getDocument().data()), 0);
    QCOMPARE(AppMcpStep5::readFile(filePath), original);

    // R6-2: the signer of the signature is the certificate of the user; nothing was added after it
    pdf::PDFAppDocumentTab* signedTab = AppMcpStep4::open(fixture, copyPath);
    QVERIFY(signedTab);
    const QJsonArray signatures = AppMcpStep6::getSignatures(fixture, signedTab);
    QCOMPARE(signatures.size(), 1);
    const QJsonObject signature = signatures.at(0).toObject();
    QCOMPARE(signature.value(QStringLiteral("signer")).toString(), QStringLiteral("Jane Novak"));
    QCOMPARE(signature.value(QStringLiteral("laterRevisions")).toString(), QStringLiteral("none"));
    QCOMPARE(signature.value(QStringLiteral("coversWholeDocument")).toBool(), true);
    QCOMPARE(signature.value(QStringLiteral("modifiedAfter")).toBool(), false);
}

void AppMcpProtectTest::signEntryPermissionsByParameters()
{
    // R6-8, ISO 32000-2 Table 22: a document, which allows only filling in forms (bit 9). A new signature
    // field needs bits 4 and 6 - refused before any question with the text of the unlock by the user; the
    // signature of its empty field needs bit 9 only - the confirmation opens
    pdftest::AppTestCertificateFolders folders;
    QVERIFY(!AppMcpStep6::createCertificate(folders, QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak")).isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, AppMcpStep6::writeFieldDocument(fixture, QStringLiteral("Form.pdf"), AppMcpStep6::allowedWithout({ 3, 4, 5, 6, 11, 12 })));
    QVERIFY(tab);
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    QVERIFY(document->getStorage().getSecurityHandler()->getMode() != pdf::EncryptionMode::None);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Form_SIGNED.pdf")) } });
    const std::optional<pdf::PDFMcpCallResult> refused = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), QJsonObject(), common));
    QVERIFY(refused && refused->isError);
    QCOMPARE(refused->getFirstText(), pdf::PDFAppDocumentPermissions::getMcpRefusalText(document, pdf::PDFAppPermissionRequest::createTool(QStringLiteral("protect.sign"))));
    QVERIFY(!pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>());

    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), QJsonObject({ { QStringLiteral("field"), QStringLiteral("Approval") } }), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY2(dialog, qPrintable(call->isFinished() && call->getResult() ? call->getResult()->getFirstText() : QStringLiteral("no dialog")));
    QVERIFY2(AppMcpStep5::facts(dialog).contains(QStringLiteral("Field: Approval")), qPrintable(AppMcpStep5::facts(dialog).join(QLatin1Char('|'))));
    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(AppMcpFixture::waitResult(call));
}

void AppMcpProtectTest::signInvalidPreselectionKeepsAuthorityGate()
{
    // Review RS6APP, 6.5, Q6.7: the certificate of protect.sign is only a preselection, but a value the
    // parameters refuse (a NUL character passes the schema) must not switch off the gate of the authority:
    // Signature with timestamp to an address, which the user has never used, is an error before any
    // question - no confirmation, no connection
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdftest::AppMcpLocalAuthority authority;
    QVERIFY(authority.isListening());
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Contract.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Contract_SIGNED.pdf")) } });
    const QJsonObject parameters({ { QStringLiteral("certificate"), QString(QChar(0)) + QStringLiteral("abc") },
                                   { QStringLiteral("type"), QStringLiteral("signature-timestamp") },
                                   { QStringLiteral("tsaUrl"), authority.getUrl() } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), parameters, common));
    QVERIFY(QTest::qWaitFor([&call]() { return call->isFinished() || pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>() != nullptr; },
                            10000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = pdftest::AppDocumentFixture::findDialog<pdf::PDFAppMcpSecretConfirmation>();
    const bool isDialogShown = dialog != nullptr;
    if (dialog)
    {
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    }
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call);
    QVERIFY(!isDialogShown);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("tsaUrl: PDF4QT sends a request only to a timestamp authority of the list of the user")), qPrintable(result->getFirstText()));
    QCOMPARE(authority.getConnectionCount(), 0);
}

void AppMcpProtectTest::timestampConfirmedBeforeAuthority()
{
    // R6-10, S6.6, 6.5, Q6.7: protect.timestamp sends a hash of the document to a third party - only to an
    // authority of the list of the user (any other address is an error before any question) and only after
    // the user has confirmed it in PDF4QT, also with every permission Allow. A local authority on the
    // loopback counts the connections: none for the error and for the denial; the allowed run reaches it
    // once (one question for the call), its HTTP 503 (435:8117) fails the run and nothing is written
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdftest::AppMcpLocalAuthority authority;
    QVERIFY(authority.isListening());
    QVERIFY(AppMcpStep6::storeAuthorityUrl(fixture, authority.getUrl()));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Report_TIMESTAMPED.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });

    // The same server by another address, which the user has never used
    const QString otherUrl = QString(authority.getUrl()).replace(QStringLiteral("127.0.0.1"), QStringLiteral("localhost"));
    const std::optional<pdf::PDFMcpCallResult> unlisted = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("timestamp"), QJsonObject({ { QStringLiteral("tsaUrl"), otherUrl } }), common));
    QVERIFY(unlisted && unlisted->isError);
    QVERIFY2(unlisted->getFirstText().startsWith(QStringLiteral("tsaUrl: PDF4QT sends a request only to a timestamp authority of the list of the user")), qPrintable(unlisted->getFirstText()));
    QVERIFY(unlisted->getFirstText().contains(authority.getUrl()));
    QVERIFY(unlisted->getFirstText().contains(QStringLiteral("http://timestamp.digicert.com")));
    QVERIFY(fixture.confirmations()->shown.empty());

    const QJsonObject parameters({ { QStringLiteral("tsaUrl"), authority.getUrl() } });
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    const std::optional<pdf::PDFMcpCallResult> denied = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("timestamp"), parameters, common));
    QVERIFY(denied && denied->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmations()->shown.back();
    QVERIFY(confirmation->isAlwaysConfirmed);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::SaveCopy);
    QCOMPARE(confirmation->details.value(QStringLiteral("path")).toString(), copyPath);
    QStringList facts;
    for (const QJsonValue& value : confirmation->details.value(QStringLiteral("facts")).toArray())
    {
        facts << value.toObject().value(QStringLiteral("key")).toString() + QStringLiteral(": ") + value.toObject().value(QStringLiteral("value")).toString();
    }
    QVERIFY2(facts.contains(QStringLiteral("Timestamp authority: ") + authority.getUrl()), qPrintable(facts.join(QLatin1Char('|'))));
    QTest::qWait(200);
    QCOMPARE(authority.getConnectionCount(), 0);

    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
    const std::optional<pdf::PDFMcpCallResult> failed = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("timestamp"), parameters, common));
    QVERIFY(failed && failed->isError);
    QVERIFY2(failed->getFirstText().contains(QStringLiteral("timestampError http-error")), qPrintable(failed->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    QCOMPARE(authority.getConnectionCount(), 1);
    QVERIFY(authority.getRequests().startsWith("POST /tsr"));
    QVERIFY(authority.getRequests().contains("application/timestamp-query"));
    QVERIFY(!QFileInfo::exists(copyPath));
}

void AppMcpProtectTest::signByHandConfirmedWithDrawing()
{
    // R6-20, Q6.7: Sign by Hand is always confirmed, also with every permission Allow, with its drawing on
    // its page - the ghosts of Show on Page (a text by its box, a line by its points, a curve by its
    // stroke and its color) and the rows of the pages with the texts as written; Show on Page also in the
    // variant of a copy; an image is refused before any question; Allowed writes the drawing as one step
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);
    const quint64 version = tab->getDocument()->getVersion();

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() } });
    const QJsonObject parameters({ { QStringLiteral("elements"), AppMcpStep6::getElements() } });
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    const std::optional<pdf::PDFMcpCallResult> denied = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), parameters, common));
    QVERIFY(denied && denied->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmations()->shown.back();
    QVERIFY(confirmation->isAlwaysConfirmed);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::Change);
    QCOMPARE(tab->getDocument()->getVersion(), version);

    const QJsonArray ghosts = confirmation->details.value(QStringLiteral("ghosts")).toArray();
    QCOMPARE(ghosts.size(), 3);
    const std::optional<pdf::PDFAppMcpGhost> text = pdf::PDFAppMcpGhost::fromJson(ghosts.at(0).toObject());
    const std::optional<pdf::PDFAppMcpGhost> line = pdf::PDFAppMcpGhost::fromJson(ghosts.at(1).toObject());
    const std::optional<pdf::PDFAppMcpGhost> curve = pdf::PDFAppMcpGhost::fromJson(ghosts.at(2).toObject());
    QVERIFY(text && line && curve);
    QCOMPARE(text->shape, pdf::PDFAppMcpGhost::Shape::Rectangle);
    QCOMPARE(text->pageIndex, pdf::PDFInteger(0));
    QCOMPARE(text->getBoundingRect(), QRectF(40, 300, 120, 20));
    QCOMPARE(line->shape, pdf::PDFAppMcpGhost::Shape::Polyline);
    QCOMPARE(line->points, QPolygonF({ QPointF(40, 296), QPointF(160, 296) }));
    QCOMPARE(curve->shape, pdf::PDFAppMcpGhost::Shape::Ink);
    QCOMPARE(curve->pageIndex, pdf::PDFInteger(1));
    QCOMPARE(int(curve->strokes.size()), 1);
    QCOMPARE(curve->strokes.front(), QPolygonF({ QPointF(50, 50), QPointF(60, 70), QPointF(80, 55) }));
    QCOMPARE(curve->color, QColor(QStringLiteral("#1f3a93")));

    const QJsonArray rows = confirmation->details.value(QStringLiteral("rows")).toArray();
    QCOMPARE(rows.size(), 2);
    const pdf::PDFAppMcpChangeRow firstRow = pdf::PDFAppMcpChangeRow::fromJson(rows.at(0).toObject());
    const pdf::PDFAppMcpChangeRow secondRow = pdf::PDFAppMcpChangeRow::fromJson(rows.at(1).toObject());
    QCOMPARE(firstRow.page, QStringLiteral("Page 1"));
    QCOMPARE(firstRow.detail, QStringLiteral("“Jane Novak”"));
    QCOMPARE(secondRow.page, QStringLiteral("Page 2"));
    QVERIFY(secondRow.detail.isEmpty());

    // The variant of a copy shows the drawing too (Show on Page) and the texts as a fact
    const QJsonObject copyCommon({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("save_copy") },
                                   { QStringLiteral("path"), fixture.path(QStringLiteral("Report_signed.pdf")) } });
    QVERIFY(fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), parameters, copyCommon)));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    const std::shared_ptr<pdf::PDFAppMcpConfirmation> copyConfirmation = fixture.confirmations()->shown.back();
    QCOMPARE(copyConfirmation->kind, pdf::PDFAppMcpConfirmationKind::SaveCopy);
    QCOMPARE(copyConfirmation->details.value(QStringLiteral("ghosts")).toArray().size(), 3);
    QStringList facts;
    for (const QJsonValue& value : copyConfirmation->details.value(QStringLiteral("facts")).toArray())
    {
        facts << value.toObject().value(QStringLiteral("key")).toString() + QStringLiteral(": ") + value.toObject().value(QStringLiteral("value")).toString();
    }
    QVERIFY2(facts.contains(QStringLiteral("Texts: “Jane Novak”")), qPrintable(facts.join(QLatin1Char('|'))));
    {
        QWidget parent;
        pdf::PDFAppMcpConfirmDialog copyDialog(copyConfirmation, nullptr, &parent);
        QVERIFY(copyDialog.getShowOnPageLink());
    }
    QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("Report_signed.pdf"))));

    // No image (Q6.7): refused by the reading of the elements, no question
    const QJsonObject image({ { QStringLiteral("elements"), QStringLiteral(R"([{"kind": "svg-image", "page": 1, "rect": [0, 0, 10, 10]}])") } });
    const std::optional<pdf::PDFMcpCallResult> refused = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), image, common));
    QVERIFY(refused && refused->isError);
    QVERIFY2(refused->getFirstText().startsWith(QStringLiteral("Invalid arguments")), qPrintable(refused->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);

    // Every element is shown (review RS6SEC): Show on Page draws at most MAXIMUM_GHOSTS ghosts, a drawing
    // of more elements (500 dots and a curve after them) is refused before any question; a drawing of
    // MAXIMUM_GHOSTS elements is asked with all of them
    const auto createDots = [](int count)
    {
        QStringList elements;
        for (int i = 0; i < count; ++i)
        {
            elements << QStringLiteral(R"({"kind": "dot", "page": 1, "rect": [%1, 10, 2, 2]})").arg(10 + i % 400);
        }
        return elements;
    };
    QStringList hiddenCurve = createDots(pdf::PDFAppMcpGhost::MAXIMUM_GHOSTS);
    hiddenCurve << QStringLiteral(R"({"kind": "freehand-curve", "page": 1, "points": [[50, 50], [60, 70], [80, 55]]})");
    const QJsonObject tooMany({ { QStringLiteral("elements"), QLatin1Char('[') + hiddenCurve.join(QLatin1Char(',')) + QLatin1Char(']') } });
    const std::optional<pdf::PDFMcpCallResult> tooManyRefused = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), tooMany, common));
    QVERIFY(tooManyRefused && tooManyRefused->isError);
    QVERIFY2(tooManyRefused->getFirstText().startsWith(QStringLiteral("elements:")), qPrintable(tooManyRefused->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);

    const QJsonObject allShown({ { QStringLiteral("elements"), QLatin1Char('[') + createDots(pdf::PDFAppMcpGhost::MAXIMUM_GHOSTS).join(QLatin1Char(',')) + QLatin1Char(']') } });
    const std::optional<pdf::PDFMcpCallResult> allShownDenied = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), allShown, common));
    QVERIFY(allShownDenied && allShownDenied->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 3);
    QCOMPARE(fixture.confirmations()->shown.back()->details.value(QStringLiteral("ghosts")).toArray().size(), pdf::PDFAppMcpGhost::MAXIMUM_GHOSTS);
    QCOMPARE(tab->getDocument()->getVersion(), version);

    // Allowed: the drawing is in the pages as one undo step, the result counts it
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
    const std::optional<pdf::PDFMcpCallResult> allowed = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign-by-hand"), parameters, common));
    QVERIFY(allowed);
    QVERIFY2(!allowed->isError, qPrintable(allowed->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 4);
    const QJsonObject data = AppMcpStep4::content(*allowed, QStringLiteral("sign-by-hand")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("elementCount")).toInt(), 3);
    QCOMPARE(data.value(QStringLiteral("pages")).toArray(), QJsonArray({ 1, 2 }));
    QVERIFY(tab->getDocument()->getVersion() != version);
}

void AppMcpProtectTest::redactByPermissionsWithoutSecrets()
{
    // R6-10, Q6.7: Redact is no action, which is always confirmed - with Allow it runs without a question,
    // with Ask the user answers after the run (the facts of the copy); the result has only counts and the
    // state of the check (6.5: never a redacted text nor an area), save_file writes the checked copy
    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Report.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Report_REDACTED.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const QJsonObject parameters({ { QStringLiteral("useMarks"), false }, { QStringLiteral("areas"), QStringLiteral(R"([{"page": 1, "rect": [20, 20, 100, 100]}])") } });
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("redact"), parameters, common));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(fixture.confirmations()->shown.empty());

    const QJsonObject data = AppMcpStep4::content(*result, QStringLiteral("redact")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("areaCount")).toInt(), 1);
    QCOMPARE(data.value(QStringLiteral("pageCount")).toInt(), 1);
    QCOMPARE(data.value(QStringLiteral("verified")).toBool(), true);
    QVERIFY(!AppMcpStep5::resultText(*result).contains("\"areas\""));
    QVERIFY(!AppMcpStep5::activityText(fixture).contains("100, 100"));
    QVERIFY(AppMcpStep5::readFile(copyPath).startsWith("%PDF-"));

    // Ask: the question after the run; Deny writes nothing
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    const QString deniedPath = fixture.path(QStringLiteral("Report_REDACTED_2.pdf"));
    const QJsonObject deniedCommon({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), deniedPath } });
    const std::optional<pdf::PDFMcpCallResult> denied = fixture.callTool(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("redact"), parameters, deniedCommon));
    QVERIFY(denied && denied->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    QVERIFY(!fixture.confirmations()->shown.back()->isAlwaysConfirmed);
    QVERIFY(!QFileInfo::exists(deniedPath));
}

void AppMcpProtectTest::certificatesReadOnly()
{
    // 6.5, Q6.7: protect.certificates is a list - a .pfx file with a password, which the user has opened
    // in PDF4QT, by the public certificate of the certificate cache (Q5.11: its name and its fingerprint,
    // the preselection of sign), a file never opened "locked" without details; never a path, a password
    // or a key; it takes no result, no path and no parameter (an assistant never creates, deletes or
    // trusts a certificate)
    pdftest::AppTestCertificateFolders folders;
    const QString pfxPath = AppMcpStep6::createCertificate(folders, QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"));
    const QString lockedPath = AppMcpStep6::createCertificate(folders, QStringLiteral("peter_locked.pfx"), QStringLiteral("Peter Locked"));
    QVERIFY(!pfxPath.isEmpty() && !lockedPath.isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    QVERIFY(AppMcpStep6::cacheCertificate(fixture, pfxPath));

    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("protect"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("certificates") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject content = AppMcpStep4::content(*result, QStringLiteral("certificates"));
    QCOMPARE(content.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    const QJsonArray certificates = content.value(QStringLiteral("data")).toObject().value(QStringLiteral("certificates")).toArray();
    QCOMPARE(certificates.size(), 2);
    QJsonObject known;
    QJsonObject locked;
    for (const QJsonValue& value : certificates)
    {
        (value.toObject().contains(QStringLiteral("fingerprint")) ? known : locked) = value.toObject();
    }
    QCOMPARE(known.value(QStringLiteral("name")).toString(), QStringLiteral("Jane Novak"));
    QCOMPARE(known.value(QStringLiteral("fingerprint")).toString(), AppMcpStep6::getFingerprint(pfxPath));
    QCOMPARE(known.value(QStringLiteral("source")).toString(), QStringLiteral("file"));
    QCOMPARE(known.value(QStringLiteral("state")).toString(), QStringLiteral("valid"));
    QCOMPARE(known.value(QStringLiteral("usableFor")).toArray().contains(QStringLiteral("signing")), true);
    QCOMPARE(locked, QJsonObject({ { QStringLiteral("source"), QStringLiteral("file") }, { QStringLiteral("readOnly"), false },
                                   { QStringLiteral("state"), QStringLiteral("locked") } }));
    QVERIFY(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("trusted")).toArray().isEmpty());
    const QByteArray text = AppMcpStep5::resultText(*result);
    QVERIFY(!text.contains("jane_novak.pfx"));
    QVERIFY(!text.contains("peter_locked.pfx"));
    QVERIFY(!text.contains(AppMcpStep5::getCanary().toUtf8()));
    QVERIFY(!text.contains(QDir::toNativeSeparators(folders.getApplicationFolder()).toUtf8()));
    QVERIFY(!text.contains(folders.getApplicationFolder().toUtf8()));

    const std::optional<pdf::PDFMcpCallResult> withResult = fixture.callTool(QStringLiteral("protect"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("certificates") },
                                                                                                                    { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(withResult && withResult->isError);
    QVERIFY2(withResult->getFirstText().startsWith(QStringLiteral("result:")), qPrintable(withResult->getFirstText()));

    const std::optional<pdf::PDFMcpCallResult> withParameter = fixture.callTool(QStringLiteral("protect"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("certificates") },
                                                                                                                       { QStringLiteral("certificates"), QJsonObject({ { QStringLiteral("trusted"), true } }) } }));
    QVERIFY(withParameter && (withParameter->isError || withParameter->kind == pdf::PDFMcpCallResult::Kind::Error));
    QCOMPARE(QDir(folders.getApplicationFolder()).entryList(QDir::Files, QDir::Name), QStringList({ QStringLiteral("jane_novak.pfx"), QStringLiteral("peter_locked.pfx") }));
}

void AppMcpProtectTest::signaturesSignerAndLaterRevisions()
{
    // R6-2, R6-3 (ISO 32000-2, 12.8.1): document.get_signatures names the signer by its certificate and
    // says what was appended after each signature - a later signature is "allowed" (the earlier one does
    // not cover the whole file, but was not changed; infos say what was added), the last one "none", a
    // later revision, which rotates a page, "changed"
    pdf::PDFCertificateEntry jane;
    pdf::PDFCertificateEntry peter;
    QVERIFY(AppMcpStep6::createEntry(QStringLiteral("Jane Novak"), &jane));
    QVERIFY(AppMcpStep6::createEntry(QStringLiteral("Peter Second"), &peter));
    const QByteArray once = AppMcpStep6::sign(AppMcpStep6::createDocumentBytes(), jane);
    QVERIFY(!once.isEmpty());
    const QByteArray twice = AppMcpStep6::sign(once, peter);
    QVERIFY(twice.size() > once.size() && twice.startsWith(once));

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* twiceTab = AppMcpStep4::open(fixture, AppMcpStep6::writeFile(fixture, QStringLiteral("Twice.pdf"), twice));
    QVERIFY(twiceTab);
    const QJsonArray signatures = AppMcpStep6::getSignatures(fixture, twiceTab);
    QCOMPARE(signatures.size(), 2);
    QJsonObject first;
    QJsonObject second;
    for (const QJsonValue& value : signatures)
    {
        (value.toObject().value(QStringLiteral("signer")).toString() == QLatin1String("Jane Novak") ? first : second) = value.toObject();
    }
    QCOMPARE(second.value(QStringLiteral("signer")).toString(), QStringLiteral("Peter Second"));
    QCOMPARE(first.value(QStringLiteral("laterRevisions")).toString(), QStringLiteral("allowed"));
    QCOMPARE(first.value(QStringLiteral("coversWholeDocument")).toBool(), false);
    QCOMPARE(first.value(QStringLiteral("modifiedAfter")).toBool(), false);
    QVERIFY(!first.value(QStringLiteral("infos")).toArray().isEmpty());
    QCOMPARE(second.value(QStringLiteral("laterRevisions")).toString(), QStringLiteral("none"));
    QCOMPARE(second.value(QStringLiteral("coversWholeDocument")).toBool(), true);

    // The result keeps the output schema of document.get_signatures
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("document"), QStringLiteral("get_signatures"), QJsonObject(),
                                                                         QJsonObject({ { QStringLiteral("documentId"), twiceTab->getDocument()->getId() } }));
    QVERIFY(result && !result->isError);
    fixture.verifyOutputSchema(QStringLiteral("document"), *result);

    const QByteArray changed = AppMcpStep6::appendPageChange(once);
    QVERIFY(changed.size() > once.size() && changed.startsWith(once));
    pdf::PDFAppDocumentTab* changedTab = AppMcpStep4::open(fixture, AppMcpStep6::writeFile(fixture, QStringLiteral("Changed.pdf"), changed));
    QVERIFY(changedTab);
    const QJsonArray changedSignatures = AppMcpStep6::getSignatures(fixture, changedTab);
    QCOMPARE(changedSignatures.size(), 1);
    const QJsonObject changedSignature = changedSignatures.at(0).toObject();
    QCOMPARE(changedSignature.value(QStringLiteral("signer")).toString(), QStringLiteral("Jane Novak"));
    QCOMPARE(changedSignature.value(QStringLiteral("laterRevisions")).toString(), QStringLiteral("changed"));
    QCOMPARE(changedSignature.value(QStringLiteral("modifiedAfter")).toBool(), true);
    QCOMPARE(changedSignature.value(QStringLiteral("coversWholeDocument")).toBool(), false);
}

void AppMcpProtectTest::signaturesClaimedDate_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<bool>("isClaimed");

    // ETSI EN 319 142-1 (PAdES): no signing-time attribute in the CMS, the time is /M of the signature
    // dictionary (ISO 32000-2, Table 255) - only claimed by the signer; adbe.pkcs7.detached carries the
    // signed attribute signing-time (RFC 5652, 11.3), which is covered by the signature
    QTest::newRow("pades") << QString::fromLatin1(AppMcpStep6::Signature::FORMAT_PADES) << true;
    QTest::newRow("pkcs7") << QString::fromLatin1(AppMcpStep6::Signature::FORMAT_PKCS7) << false;
}

void AppMcpProtectTest::signaturesClaimedDate()
{
    // Owner 16. 11.: document.get_signatures says, beside signedAt, whether the date is only claimed by
    // the signer (signedAtClaimed), as the signature panel and PdfTool do
    QFETCH(QString, format);
    QFETCH(bool, isClaimed);

    pdf::PDFCertificateEntry jane;
    QVERIFY(AppMcpStep6::createEntry(QStringLiteral("Jane Novak"), &jane));
    const QDateTime before = QDateTime::currentDateTimeUtc();
    const QByteArray formatName = format.toLatin1();
    const QByteArray signedData = AppMcpStep6::sign(AppMcpStep6::createDocumentBytes(), jane, formatName.constData());
    QVERIFY(!signedData.isEmpty());

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, AppMcpStep6::writeFile(fixture, QStringLiteral("Signed.pdf"), signedData));
    QVERIFY(tab);
    const QJsonArray signatures = AppMcpStep6::getSignatures(fixture, tab);
    QCOMPARE(signatures.size(), 1);
    const QJsonObject signature = signatures.at(0).toObject();
    QCOMPARE(signature.value(QStringLiteral("signer")).toString(), QStringLiteral("Jane Novak"));

    // signedAt is the time of the signing in both formats (/M is written in whole seconds, 7.9.4), only
    // its source differs
    const QDateTime signedAt = QDateTime::fromString(signature.value(QStringLiteral("signedAt")).toString(), Qt::ISODate);
    QVERIFY2(signedAt.isValid(), qPrintable(signature.value(QStringLiteral("signedAt")).toString()));
    QVERIFY2(qAbs(before.secsTo(signedAt)) <= 60, qPrintable(signedAt.toString(Qt::ISODate)));
    QVERIFY(signature.value(QStringLiteral("signedAtClaimed")).isBool());
    QCOMPARE(signature.value(QStringLiteral("signedAtClaimed")).toBool(), isClaimed);

    // The result keeps the output schema of document.get_signatures
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("document"), QStringLiteral("get_signatures"), QJsonObject(),
                                                                         QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
    QVERIFY(result && !result->isError);
    fixture.verifyOutputSchema(QStringLiteral("document"), *result);
}

void AppMcpProtectTest::signRunParentWindow()
{
    // Q6.2: the run of Sign with Certificate of an assistant gives the system its parent window for the
    // dialog of the PIN of a key of the store of Windows - the main window of the tab of the document
    // (PDFDocumentToolContext::parentWindow); the recording tool replaces the tool for this test only
    pdftest::AppTestCertificateFolders folders;
    QVERIFY(!AppMcpStep6::createCertificate(folders, QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak")).isEmpty());

    const QString toolId = QStringLiteral("protect.sign");
    const std::shared_ptr<const pdf::PDFDocumentTool> original = pdf::PDFDocumentToolRegistry::getTool(toolId);
    QVERIFY(original);
    QVERIFY(pdf::PDFDocumentToolRegistry::unregisterTool(toolId));
    pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<AppMcpProtectRecordingSignTool>());
    const auto restore = qScopeGuard([&]()
    {
        pdf::PDFDocumentToolRegistry::unregisterTool(toolId);
        pdf::PDFDocumentToolRegistry::registerTool(original);
    });
    AppMcpProtectRecordingSignTool::s_parentWindow = 0;
    AppMcpProtectRecordingSignTool::s_isRun = false;

    AppMcpFixture fixture;
    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Contract.pdf"));
    QVERIFY(tab);

    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), fixture.path(QStringLiteral("Contract_SIGNED.pdf")) } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("sign"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    AppMcpStep5::type(dialog->getCertificatePasswordCard(), AppMcpStep5::getCanary());
    AppMcpStep5::confirm(dialog);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call, 60000);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("Recorded.")), qPrintable(result->getFirstText()));
    QVERIFY(AppMcpProtectRecordingSignTool::s_isRun);
    QVERIFY(AppMcpProtectRecordingSignTool::s_parentWindow != 0);
    QCOMPARE(quintptr(AppMcpProtectRecordingSignTool::s_parentWindow), quintptr(tab->window()->winId()));
}

void AppMcpProtectTest::certificateEncryptSystemStore()
{
    // The addendum of the owner of 15. 11.: Encrypt with Certificate encrypts for a personal certificate of
    // the store of Windows by its public certificate only - the confirmation offers the row of the store
    // ("system-store:<fingerprint>"), asks no password (its card is hidden) and sends none; the copy is
    // encrypted for certificates (ISO 32000-2, 7.6.5). A store in the memory of the process (never the
    // store of the machine)
    pdftest::AppTestCertificateFolders folders;
    AppMcpFixture fixture;
    const QString source = pdftest::AppTestCertificateFolders::create(fixture.folder(), QStringLiteral("company.pfx"), QStringLiteral("Jane Novak – Company ID"),
                                                             QStringLiteral("store-password"), 2048);
    QVERIFY(!source.isEmpty());
    std::shared_ptr<const pdf::PDFPersonalCertificateStore> store = pdf::PDFPersonalCertificateStore::createMemoryStore(AppMcpStep5::readFile(source), QByteArrayLiteral("store-password"));
    if (!store)
    {
        QSKIP("No store of the system on this platform (Q6.2: Windows only)");
    }
    pdf::PDFPersonalCertificateStore::setStore(store);
    pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(true);
    const auto restore = qScopeGuard([]()
    {
        pdf::PDFPersonalCertificateStore::setStore(nullptr);
        pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(false);
    });
    const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(AppMcpStep5::readFile(source), QStringLiteral("store-password"));
    QVERIFY(info);
    const QString fingerprint = pdf::PDFDocumentToolEncryption::getCertificateFingerprint(info->getCertificateData());

    QVERIFY(start(fixture, false));
    pdf::PDFAppDocumentTab* tab = openDocument(fixture, QStringLiteral("Contract.pdf"));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("Contract_encrypted.pdf"));
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("path"), copyPath } });
    const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("protect"), AppMcpStep5::arguments(QStringLiteral("certificate-encrypt"), QJsonObject(), common));
    QPointer<pdf::PDFAppMcpSecretConfirmation> dialog = AppMcpStep5::waitDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getCertificateList());
    QCOMPARE(dialog->getCertificateList()->getChosenFilePath(), pdf::PDFAppToolCertificateProvider::getSystemStorePath(fingerprint));
    QVERIFY(dialog->getCertificatePasswordCard());
    QVERIFY(!dialog->getCertificatePasswordCard()->isVisibleTo(dialog));
    QVERIFY(dialog->isConfirmEnabled());

    AppMcpStep5::confirm(dialog);
    const std::optional<pdf::PDFMcpCallResult> result = AppMcpFixture::waitResult(call, 60000);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject content = AppMcpStep4::content(*result, QStringLiteral("certificate-encrypt"));
    QCOMPARE(content.value(QStringLiteral("data")).toObject().value(QStringLiteral("algorithm")).toString(), QStringLiteral("certificate"));
    for (const QByteArray& text : { AppMcpStep5::resultText(*result), AppMcpStep5::activityText(fixture) })
    {
        QVERIFY(!text.contains("store-password"));
        QVERIFY(!text.contains("system-store:"));
    }
    QVERIFY(AppMcpStep5::readFile(copyPath).contains("/Adobe.PubSec"));

    // The recipient is the certificate of the store: the copy opens by the key of the store (no password
    // asked); a store with another certificate (in the memory too, never the store of the machine) does
    // not open it
    QVERIFY(AppMcpStep5::opensWithoutPassword(copyPath));
    const QString other = pdftest::AppTestCertificateFolders::create(fixture.folder(), QStringLiteral("other.pfx"), QStringLiteral("Somebody Else"), QStringLiteral("store-password"), 2048);
    std::shared_ptr<const pdf::PDFPersonalCertificateStore> otherStore = pdf::PDFPersonalCertificateStore::createMemoryStore(AppMcpStep5::readFile(other), QByteArrayLiteral("store-password"));
    QVERIFY(otherStore);
    pdf::PDFPersonalCertificateStore::setStore(otherStore);
    QVERIFY(!AppMcpStep5::opensWithoutPassword(copyPath));
}

QTEST_MAIN(AppMcpProtectTest)

#include "tst_appmcpprotecttest.moc"
