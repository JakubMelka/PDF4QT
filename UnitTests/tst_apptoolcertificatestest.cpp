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

// Manage Certificates in its own tab, the detail of a certificate and the list of the certificates for
// signing (step 6 of phase 2, plan 2.4, 9; package P5): the frames 436:9398 ... 436:12324, one instance
// of the tab, the deletion only of the own folder, Add Trusted…, the table shared with Settings ›
// Security, portable mode (Q6.13), the core tool of protect.certificates. The certificates are made by
// the test at run time under its temporary folder (pdftest::AppTestCertificateFolders); the system store
// is a store in the memory of the process (PDFPersonalCertificateStore::createMemoryStore), never the
// store of the machine; the system rows of the trusted view come from a provider of the test.

#include "appasyncdialogs.h"
#include "appdocumentfixture.h"
#include "apptestcertificatefolders.h"
#include "apptestcertificates.h"
#include "apptoolowntabfixture.h"

#include "pdfappcertificatedetails.h"
#include "pdfappcertificatedialogs.h"
#include "pdfappcertificatetable.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocument.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolgui_certificates.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfcertificatecacherepository.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfrepositoryhelper.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

#include <openssl/evp.h>
#include <openssl/x509v3.h>

/// Helpers of the tests of Manage Certificates
class AppToolCertificatesTestHelper
{
public:
    using GUI = pdf::PDFAppToolGUICertificates;

    enum class KeyKind
    {
        Rsa,
        Ec,
        Ed25519
    };

    /// The info of a self-signed X.509 v3 certificate made by OpenSSL now: the key of the kind, the key
    /// usage and the extended key usage extensions in the syntax of OpenSSL (null = the extension is not
    /// there). The private key is dropped at once; an empty info on a failure.
    static pdf::PDFCertificateInfo der(KeyKind kind, const char* keyUsage, const char* extendedKeyUsage)
    {
        EVP_PKEY* key = kind == KeyKind::Rsa ? EVP_RSA_gen(2048) : (kind == KeyKind::Ec ? EVP_EC_gen("P-256") : EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"));
        X509* certificate = X509_new();
        QByteArray data;
        if (key && certificate)
        {
            X509_set_version(certificate, 2);
            ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
            X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
            X509_gmtime_adj(X509_getm_notAfter(certificate), 3600L * 24 * 365);
            X509_set_pubkey(certificate, key);
            X509_NAME* name = X509_get_subject_name(certificate);
            X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>("Usage"), -1, -1, 0);
            X509_set_issuer_name(certificate, name);

            X509V3_CTX context;
            X509V3_set_ctx_nodb(&context);
            X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
            for (const auto& [nid, value] : { std::make_pair(int(NID_key_usage), keyUsage), std::make_pair(int(NID_ext_key_usage), extendedKeyUsage) })
            {
                if (value)
                {
                    if (X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value))
                    {
                        X509_add_ext(certificate, extension, -1);
                        X509_EXTENSION_free(extension);
                    }
                }
            }

            // Ed25519 signs without a digest
            if (X509_sign(certificate, key, kind == KeyKind::Ed25519 ? nullptr : EVP_sha256()) > 0)
            {
                const int length = i2d_X509(certificate, nullptr);
                data.resize(qMax(length, 0));
                unsigned char* buffer = reinterpret_cast<unsigned char*>(data.data());
                i2d_X509(certificate, &buffer);
            }
        }
        X509_free(certificate);
        EVP_PKEY_free(key);

        const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateInfo::getCertificateInfo(data);
        return info.value_or(pdf::PDFCertificateInfo());
    }

    /// A self-signed certificate with its key created at run time (the core of P1b, B130): Acme Corp,
    /// Finance, jane@example.com, CZ; its path, empty on a failure
    static QString create(const QString& folder, const QString& fileName, const QString& name, const QString& password, int validityInSeconds = 5 * 365 * 24 * 3600, int keyLength = 2048)
    {
        QDir().mkpath(folder);
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(folder).absoluteFilePath(fileName);
        info.privateKeyPasword = password;
        info.certCommonName = name;
        info.certOrganization = QStringLiteral("Acme Corp");
        info.certOrganizationUnit = QStringLiteral("Finance");
        info.certEmail = QStringLiteral("jane@example.com");
        info.certCountryCode = QStringLiteral("CZ");
        info.rsaKeyLength = keyLength;
        info.validityInSeconds = validityInSeconds;
        QString error;
        return pdf::PDFCertificateManager().createCertificate(info, &error) ? info.fileName : QString();
    }

    static QByteArray read(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool write(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    /// The certificate of a .pfx file read by its password
    static std::optional<pdf::PDFCertificateInfo> certificate(const QString& filePath, const QString& password)
    {
        return pdf::PDFCertificateManager::readCertificate(read(filePath), password);
    }

    /// The certificate cache knows the file (as after its first unlock, Q5.11)
    static bool cache(pdftest::AppDocumentFixture& fixture, const QString& filePath, const QString& password)
    {
        std::optional<pdf::PDFCertificateInfo> info = certificate(filePath, password);
        if (!info)
        {
            return false;
        }
        pdf::PDFAppToolCertificateProvider(fixture.context.get()).storeCertificate(filePath, info->getCertificateData());
        return true;
    }

    /// Opens the own tab of Manage Certificates; its GUI
    static GUI* open(pdftest::AppToolOwnTabFixture& fixture, pdf::PDFAppToolTab** tab = nullptr)
    {
        pdf::PDFAppToolTab* toolTab = fixture.createToolTab(QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID));
        if (tab)
        {
            *tab = toolTab;
        }
        pdf::PDFDocumentToolShell* shell = toolTab ? pdftest::AppToolOwnTabFixture::getShell(toolTab) : nullptr;
        QPointer<GUI> gui = shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
        if (gui)
        {
            // The system certificates of the trusted view are read by a job
            const bool isRead = QTest::qWaitFor([&gui]() { return !gui || !gui->isLoadingSystemCertificates(); }, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
            pdftest::AppDocumentFixture::settle();
            if (!isRead)
            {
                return nullptr;
            }
        }
        return gui.data();
    }

    /// The text of the title and the subtitle of a panel header
    static QString title(const pdf::PDFAppToolPanelHeader* header) { return header ? header->getTitleLabel()->text() : QString(); }
    static QString subtitle(const pdf::PDFAppToolPanelHeader* header) { return header ? header->getSubtitleLabel()->text() : QString(); }

    /// The titles of the usage rows of the detail
    static QStringList usageTitles(const pdf::PDFAppCertificateDetails* details)
    {
        QStringList titles;
        for (const pdf::PDFTileRow* row : details->getUsageRows())
        {
            titles << row->getTitle();
        }
        return titles;
    }

    /// A label of the widget tree has the text
    static bool hasText(const QWidget* widget, const QString& text)
    {
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            if (label->text() == text)
            {
                return true;
            }
        }
        return false;
    }

    /// PEM of a DER certificate (RFC 7468: lines of 64 characters)
    static QByteArray pem(const QByteArray& der)
    {
        const QByteArray base64 = der.toBase64();
        QByteArray result("Subject: test\n-----BEGIN CERTIFICATE-----\n");
        for (qsizetype i = 0; i < base64.size(); i += 64)
        {
            result += base64.mid(i, 64) + "\n";
        }
        result += "-----END CERTIFICATE-----\n";
        return result;
    }

    /// The system certificate of the trusted view (pdftest::AppTestCertificates::second)
    static pdf::PDFCertificateEntries systemTrusted()
    {
        pdf::PDFCertificateEntry entry;
        entry.type = pdf::PDFCertificateEntry::EntryType::System;
        entry.info = *pdf::PDFCertificateInfo::getCertificateInfo(pdftest::AppTestCertificates::second());
        return { entry };
    }

    static pdf::PDFCertificateEntries noTrusted()
    {
        return { };
    }
};

/// The clock of the application fixed for a test (PDFAppLocale::setCurrentTimeOverride), given back
class AppToolCertificatesTimeGuard
{
public:
    explicit AppToolCertificatesTimeGuard(const QDateTime& now) { pdf::PDFAppLocale::setCurrentTimeOverride(now.toMSecsSinceEpoch()); }
    ~AppToolCertificatesTimeGuard() { pdf::PDFAppLocale::setCurrentTimeOverride(0); }
};

/// The providers of the system certificates of the trusted tables (Manage Certificates and
/// Settings › Security) for a test, given back
class AppToolCertificatesProviderGuard
{
public:
    explicit AppToolCertificatesProviderGuard(pdf::PDFAppSystemCertificatesTask::Provider provider)
    {
        pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(provider);
        pdf::PDFAppCertificateTable::setSystemCertificatesProvider(provider);
    }

    ~AppToolCertificatesProviderGuard()
    {
        pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(&AppToolCertificatesTestHelper::noTrusted);
        pdf::PDFAppCertificateTable::setSystemCertificatesProvider(&AppToolCertificatesTestHelper::noTrusted);
    }
};

class AppToolCertificatesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void signingRowState();
    void signingUsageRules();
    void listMakesRowsForUsage();
    void detailsDerFields();
    void registrationAndOwnTab();
    void singleInstance();
    void myCertificatesFrame();
    void systemStoreRows();
    void emptyState();
    void deleteCertificate();
    void importCollision();
    void trustedView();
    void addTrusted();
    void removeTrusted();
    void portableMode();
    void coreToolResult();

    // Review RS6FIGMA
    void screenshots();
};

void AppToolCertificatesTest::initTestCase()
{
    // Nothing of a test reads the certificates of the user or of the machine: temporary folders for
    // the whole process, no system store, no trusted certificates of the system
    pdftest::AppTestCertificateFolders::installForProcess();
    pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(&AppToolCertificatesTestHelper::noTrusted);
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(&AppToolCertificatesTestHelper::noTrusted);
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolCertificatesTest::signingRowState()
{
    // 5.3, B116: a certificate, which cannot sign, is a Disabled row with its reason
    pdf::PDFAppToolCertificateItem item;
    item.state = pdf::PDFAppToolCertificateItem::State::NoSigningUsage;
    QVERIFY(!item.isChoosable());
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(item), QStringLiteral("Not meant for signing – cannot be used"));
}

void AppToolCertificatesTest::signingUsageRules()
{
    // Plan 5.3, B116 (RFC 5280 4.2.1.3, 4.2.1.12): digitalSignature or nonRepudiation when the key
    // usage is present; an extended key usage needs a purpose of documents (emailProtection,
    // documentSigning, Adobe, Microsoft Document Signing or any). The rule is decided on real X.509
    // certificates (DER made by OpenSSL here): a list row is always a certificate.
    using Info = pdf::PDFCertificateInfo;
    using Provider = pdf::PDFAppToolCertificateProvider;
    using H = AppToolCertificatesTestHelper;

    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, nullptr, nullptr)));                                     // no extension: any use
    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,digitalSignature", nullptr)));
    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Ec, "critical,nonRepudiation", nullptr)));
    QVERIFY(!Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,keyEncipherment", nullptr)));
    QVERIFY(!Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,keyCertSign,cRLSign", nullptr)));
    QVERIFY(!Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,digitalSignature", "serverAuth")));
    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,digitalSignature", "serverAuth,emailProtection")));
    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,digitalSignature", "anyExtendedKeyUsage")));
    QVERIFY(Provider::isUsableForSigning(H::der(H::KeyKind::Rsa, "critical,digitalSignature", "1.3.6.1.4.1.311.10.3.12")));   // Microsoft Document Signing
    QVERIFY(!Provider::isUsableForSigning(H::der(H::KeyKind::Ed25519, "critical,digitalSignature", nullptr)));            // unlock signs by RSA and ECDSA only

    // An info without its certificate is no certificate: it cannot sign, whatever its flags say
    Info flagsOnly;
    flagsOnly.setPublicKey(Info::KeyRSA);
    flagsOnly.setKeyUsage(Info::KeyUsageFlags::fromInt(Info::KeyUsageDigitalSignature));
    QVERIFY(!Provider::isUsableForSigning(flagsOnly));

    // A certificate of Create Certificate… (B130: digitalSignature, nonRepudiation, keyEncipherment)
    // can sign and encrypt
    pdftest::AppTestCertificateFolders folders;
    const QString path = AppToolCertificatesTestHelper::create(folders.getApplicationFolder(), QStringLiteral("created.pfx"), QStringLiteral("Created"), QString());
    std::optional<Info> created = AppToolCertificatesTestHelper::certificate(path, QString());
    QVERIFY(created);
    QVERIFY(Provider::isUsableForSigning(*created));
    QVERIFY(pdf::PDFCertificateManager::isUsableForEncryption(created->getCertificateData()));
}

void AppToolCertificatesTest::listMakesRowsForUsage()
{
    using Item = pdf::PDFAppToolCertificateItem;
    using Usage = pdf::PDFAppToolCertificateUsage;

    // A known certificate, which can sign but not encrypt, and one of the system store, which can sign
    pdftest::AppTestCertificateFolders folders;
    const QString path = AppToolCertificatesTestHelper::create(folders.getApplicationFolder(), QStringLiteral("signer.pfx"), QStringLiteral("Signer"), QString());
    std::optional<pdf::PDFCertificateInfo> info = AppToolCertificatesTestHelper::certificate(path, QString());
    QVERIFY(info);
    pdf::PDFCertificateEntry fileEntry;
    fileEntry.pkcs12 = AppToolCertificatesTestHelper::read(path);
    fileEntry.pkcs12FilePath = path;
    fileEntry.info = *info;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    Item signOnly = pdf::PDFAppToolCertificateProvider::createItem(fileEntry, info, now, Usage::Encryption);
    QCOMPARE(signOnly.state, Item::State::Valid);
    signOnly.isUsableForEncryption = false;
    signOnly.filePath = QStringLiteral("C:/certificates/signer.pfx");

    pdf::PDFCertificateEntry systemEntry;
    systemEntry.info = *info;
    const Item system = pdf::PDFAppToolCertificateProvider::createItem(systemEntry, info, now, Usage::Encryption);
    QCOMPARE(system.state, Item::State::Valid);
    QCOMPARE(system.filePath, pdf::PDFAppToolCertificateProvider::getSystemStorePath(system.fingerprint));
    QVERIFY(pdf::PDFAppToolCertificateProvider::isSystemStorePath(system.filePath));

    // Encryption (step 5): the file without keyEncipherment Disabled; the system row a recipient by its
    // public certificate (the addendum of the owner of 15. 11.)
    QWidget window;
    pdf::PDFAppToolCertificateList* list = new pdf::PDFAppToolCertificateList(&window);
    // (the rows, which can be chosen, first - the system row before the file)
    list->setCertificates({ signOnly, system });
    QCOMPARE(list->getCertificates().size(), size_t(2));
    QCOMPARE(list->getCertificates()[0].filePath, system.filePath);
    QCOMPARE(list->getCertificates()[0].state, Item::State::Valid);
    QCOMPARE(list->getCertificates()[1].filePath, signOnly.filePath);
    QCOMPARE(list->getCertificates()[1].state, Item::State::NoEncryptionUsage);
    QVERIFY(list->getRow(0)->isClickable());
    QVERIFY(!list->getRow(1)->isClickable());
    list->choose(signOnly.filePath);
    QVERIFY(list->getChosenFilePath().isEmpty());

    // setUsage(Signing) makes the rows for signing (plan 10.3): both can be chosen, the system row on
    // Windows by the store (Q6.2); emits nothing
    QSignalSpy chosen(list, &pdf::PDFAppToolCertificateList::certificateChosen);
    list->setUsage(Usage::Signing);
    QCOMPARE(list->getCertificates()[0].state, Item::State::Valid);
    QCOMPARE(list->getCertificates()[1].state, Item::State::Valid);
    list->choose(system.filePath);
    QCOMPARE(list->getChosenFilePath(), system.filePath);
    QCOMPARE(list->getChosenFingerprint(), system.fingerprint);
    QCOMPARE(list->getCertificates()[0].filePath, system.filePath);
    QVERIFY(list->getRow(0)->isClickable());
    QCOMPARE(list->getRow(0)->getTag(), QStringLiteral("System store"));

    // A certificate, which cannot sign (5.3), is Disabled with its reason in red; the chosen row of a
    // usage, which it does not have, is dropped by the next change
    Item noSigning = signOnly;
    noSigning.isUsableForSigning = false;
    noSigning.isUsableForEncryption = true;
    list->setCertificates({ noSigning, system });
    QCOMPARE(list->getCertificates()[0].state, Item::State::Valid);     // the system row first (choosable first)
    QCOMPARE(list->getCertificates()[1].state, Item::State::NoSigningUsage);
    QVERIFY(!list->getRow(1)->isClickable());
    QCOMPARE(list->getRow(1)->getDescriptionColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::AccentRedIcon));

    // The system row is a recipient of the encryption too (the addendum of 15. 11.): it stays chosen; a
    // system row, which cannot encrypt (no keyEncipherment), is dropped by the change of the usage
    list->choose(system.filePath);
    list->setUsage(Usage::Encryption);
    QCOMPARE(list->getChosenFilePath(), system.filePath);
    Item systemNoEncryption = system;
    systemNoEncryption.isUsableForEncryption = false;
    list->setUsage(Usage::Signing);
    list->setCertificates({ noSigning, systemNoEncryption });
    list->choose(system.filePath);
    QCOMPARE(list->getChosenFilePath(), system.filePath);
    list->setUsage(Usage::Encryption);
    QVERIFY(list->getChosenFilePath().isEmpty());
    QCOMPARE(chosen.count(), 0);
    QCOMPARE(list->getCertificates().size(), size_t(2));
    QCOMPARE(list->getCertificates()[0].state, Item::State::Valid);
    QCOMPARE(list->getCertificates()[1].filePath, system.filePath);
    QCOMPARE(list->getCertificates()[1].state, Item::State::NoEncryptionUsage);
    QVERIFY(!list->getRow(1)->isClickable());
}

void AppToolCertificatesTest::detailsDerFields()
{
    using Details = pdf::PDFAppCertificateDetails;

    // RFC 5280 4.1.2.2: the serial number of the test certificate as "openssl x509 -noout -serial" gives
    // it (13BA1E01C505D6DC1DF76054703C5D9E88E2E0AA); the certificate is self-issued (6.1)
    QCOMPARE(Details::getSerialNumberText(pdftest::AppTestCertificates::first()), QStringLiteral("13:BA:1E:01:C5:05:D6:DC:1D:F7:60:54:70:3C:5D:9E:88:E2:E0:AA"));
    QVERIFY(Details::isSelfIssued(pdftest::AppTestCertificates::first()));

    // X.690 8.3.2: the leading zero byte of a positive INTEGER is no digit of the number; a minimal
    // TBSCertificate with empty names (equal = self-issued)
    const QByteArray minimal = QByteArray::fromHex("3013" "3011" "a003020102" "02020080" "3000" "3000" "3000" "3000");
    QCOMPARE(Details::getSerialNumberText(minimal), QStringLiteral("80"));
    QVERIFY(Details::isSelfIssued(minimal));

    // Hostile input: cut data, a length over the data, a length of five bytes, no INTEGER
    QVERIFY(Details::getSerialNumberText(pdftest::AppTestCertificates::first().left(40)).isEmpty());
    QVERIFY(Details::getSerialNumberText(QByteArray::fromHex("30ff3011")).isEmpty());
    QVERIFY(Details::getSerialNumberText(QByteArray::fromHex("3085ffffffffff")).isEmpty());
    QVERIFY(Details::getSerialNumberText(QByteArray::fromHex("300a3008a0030201020400")).isEmpty());
    QVERIFY(!Details::isSelfIssued(QByteArray()));
}

void AppToolCertificatesTest::registrationAndOwnTab()
{
    // Plan 10.2: protect.certificates is implemented by the application (an Analysis, K6.11); the
    // context of the application registers the tools (PDFAppBuiltinTools::registerAll)
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString toolId = QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID);
    QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(toolId));
    QVERIFY(pdf::PDFAppToolTabPageFactory::hasOwnTabPage(toolId));
    pdf::PDFAppToolTab* tab = nullptr;
    pdf::PDFAppToolGUICertificates* gui = AppToolCertificatesTestHelper::open(fixture, &tab);
    QVERIFY(gui);

    // tab-item 436:9402 "Certificates"; btn-cancel "Close"; btn-confirm hidden (K6.11)
    QCOMPARE(tab->getTabTitle(), QStringLiteral("Certificates"));
    pdf::PDFAppToolToolbar* toolbar = gui->getToolbar();
    QVERIFY(toolbar);
    QCOMPARE(toolbar->getCancelButton()->text(), QStringLiteral("Close"));
    QVERIFY(!toolbar->getConfirmButton()->isVisible());
    QVERIFY(!gui->isConfirmEnabled());

    // K6.11 (isConfirmShown): the toolbar keeps it hidden in every mode, Ctrl+Enter does nothing
    QVERIFY(!gui->isConfirmShown());
    QVERIFY(!toolbar->isConfirmShown());
    for (const pdf::PDFAppToolToolbar::Mode mode : { pdf::PDFAppToolToolbar::Mode::Finished, pdf::PDFAppToolToolbar::Mode::Running, pdf::PDFAppToolToolbar::Mode::Editing })
    {
        toolbar->setMode(mode);
        QVERIFY(toolbar->getConfirmButton()->isHidden());
    }
    pdf::PDFDocumentToolShell* shell = gui->getShell();
    shell->confirm();
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
}

void AppToolCertificatesTest::singleInstance()
{
    // Plan 4.5: one instance of the tab - the tab of the window is found
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    pdf::PDFAppToolTab* tab = nullptr;
    QVERIFY(AppToolCertificatesTestHelper::open(fixture, &tab));
    QCOMPARE(pdf::PDFAppToolGUICertificates::findCertificatesTab(fixture.window), tab);

    // 436:9398 / 436:12086: no history - Undo and Redo are hidden (tb-undo, tb-redo), one divider stays
    // between the title and the actions
    pdf::PDFAppToolToolbar* toolbar = tab->getShell() ? tab->getShell()->getToolbar() : nullptr;
    QVERIFY(toolbar);
    QVERIFY(!toolbar->isUndoShown());
    QVERIFY(!toolbar->getUndoButton()->isVisibleTo(toolbar));
    QVERIFY(!toolbar->getRedoButton()->isVisibleTo(toolbar));

    // A second start activates the tab (PDFAppMainWindow::createToolTab, isSingleOwnTab): the Home tab
    // is current first, then the start of the tool returns and shows the one tab
    fixture.window->activateTab(fixture.window->getHomeTab());
    pdftest::AppToolOwnTabFixture::settle();
    QVERIFY(fixture.window->getCurrentTab() != tab);
    pdf::PDFAppToolTab* second = fixture.createToolTab(QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID));
    QCOMPARE(second, tab);
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    int count = 0;
    for (const pdf::PDFAppToolTab* toolTab : fixture.window->getToolTabs())
    {
        count += toolTab->getToolId() == QLatin1String(pdf::PDFAppDocumentToolCertificates::TOOL_ID) ? 1 : 0;
    }
    QCOMPARE(count, 1);

    // RS6APP: a route of another instance of the tool (an entry of the history, a link) shows the one
    // tab with its own route - never a second tab (PDFAppNavigator::chooseTab)
    fixture.window->activateTab(fixture.window->getHomeTab());
    pdftest::AppToolOwnTabFixture::settle();
    const pdf::PDFAppRoute oldRoute = pdf::PDFAppRoutes::toolTab(QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID), pdf::PDFAppDocument::createId());
    QVERIFY(oldRoute != tab->getRoute());
    fixture.window->getNavigator()->navigate(oldRoute, pdf::PDFAppNavigationMode::Push, fixture.window->getHomeTab());
    pdftest::AppToolOwnTabFixture::settle();
    count = 0;
    for (const pdf::PDFAppToolTab* toolTab : fixture.window->getToolTabs())
    {
        count += toolTab->getToolId() == QLatin1String(pdf::PDFAppDocumentToolCertificates::TOOL_ID) ? 1 : 0;
    }
    QCOMPARE(count, 1);
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QVERIFY(tab->getHistory().getCurrentEntry());
    QCOMPARE(tab->getHistory().getCurrentEntry()->route, tab->getRoute());
    QVERIFY(tab->getShell());

    // Another tool of an own tab may have more tabs (Merge)
    const int before = int(fixture.window->getToolTabs().size());
    QVERIFY(fixture.createToolTab(QStringLiteral("pages.merge")));
    QVERIFY(fixture.createToolTab(QStringLiteral("pages.merge")));
    QCOMPARE(int(fixture.window->getToolTabs().size()), before + 2);
}

void AppToolCertificatesTest::myCertificatesFrame()
{
    using H = AppToolCertificatesTestHelper;

    // 436:9398: a .pfx file with a password known by the cache and an expired file without a password
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString jane = H::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"));
    const QString old = H::create(folders.getApplicationFolder(), QStringLiteral("cert_1.pfx"), QStringLiteral("Jane Novak (2020)"), QString(), 24 * 3600);
    QVERIFY(!jane.isEmpty() && !old.isEmpty());
    QVERIFY(H::cache(fixture, jane, QStringLiteral("pfx-password")));
    std::optional<pdf::PDFCertificateInfo> janeInfo = H::certificate(jane, QStringLiteral("pfx-password"));
    std::optional<pdf::PDFCertificateInfo> oldInfo = H::certificate(old, QString());
    QVERIFY(janeInfo && oldInfo);
    AppToolCertificatesTimeGuard time(QDateTime::currentDateTimeUtc().addDays(3));

    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);

    // The toolbar of My certificates 436:9418
    QCOMPARE(gui->getCreateButton()->text(), QStringLiteral("Create…"));
    QCOMPARE(gui->getImportButton()->text(), QStringLiteral("Import…"));
    QCOMPARE(gui->getDeleteButton()->text(), QStringLiteral("Delete"));
    QCOMPARE(gui->getFolderButton()->text(), QStringLiteral("Open Folder"));
    QVERIFY(!gui->getCreateButton()->isHidden() && !gui->getDeleteButton()->isHidden() && !gui->getActionsDivider()->isHidden());
    QVERIFY(gui->getAddTrustedButton()->isHidden() && gui->getRemoveTrustedButton()->isHidden());

    // Left 436:9521 ... 436:9549
    QCOMPARE(H::title(gui->getStoreHeader()), QStringLiteral("Certificate store"));
    QCOMPARE(H::subtitle(gui->getStoreHeader()), QStringLiteral("On this computer"));
    pdf::PDFTileRow* myRow = gui->getViewRow(H::GUI::View::My);
    pdf::PDFTileRow* trustedRow = gui->getViewRow(H::GUI::View::Trusted);
    QCOMPARE(myRow->getTitle(), QStringLiteral("My certificates"));
    QCOMPARE(myRow->getDescription(), QStringLiteral("Sign and encrypt with them"));
    QCOMPARE(myRow->getShortcutText(), QStringLiteral("2"));
    QCOMPARE(myRow->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(trustedRow->getTitle(), QStringLiteral("Trusted certificates"));
    QCOMPARE(trustedRow->getDescription(), QStringLiteral("Used to verify signatures"));
    QCOMPARE(trustedRow->getShortcutText(), QStringLiteral("0"));
    QCOMPARE(gui->getViewCallout()->getTitleLabel()->text(), QStringLiteral("Where the files are"));
    QCOMPARE(gui->getViewCallout()->getTextLabel()->text(), QStringLiteral("Certificate files (.pfx) are kept in your PDF4QT certificate folder. Certificates of the operating system are shown as well and are managed by the system."));

    // Canvas 436:9550: the heading, the card Certificate files - the usable certificate first, the
    // expired one in red; no card of the system store (no such certificate)
    QVERIFY(H::hasText(gui->getMyPage(), QStringLiteral("My certificates")));
    QVERIFY(H::hasText(gui->getMyPage(), QStringLiteral("Digital IDs with a private key – for signing and for certificate encryption.")));
    QVERIFY(gui->getFilesCard()->isVisible());
    QVERIFY(!gui->getSystemCard()->isVisible());
    QVERIFY(!gui->getEmptyState()->isVisible());
    QCOMPARE(gui->getCertificates().size(), size_t(2));
    pdf::PDFTileRow* janeRow = gui->getCertificateRow(jane);
    pdf::PDFTileRow* oldRow = gui->getCertificateRow(old);
    QVERIFY(janeRow && oldRow);
    QCOMPARE(gui->getCertificates().front().filePath, jane);
    QCOMPARE(janeRow->getTitle(), QStringLiteral("Jane Novak"));
    QCOMPARE(janeRow->getIconName(), QStringLiteral("pfx-file"));
    QCOMPARE(janeRow->getDescription(), QStringLiteral("jane_novak.pfx · Acme Corp · RSA 2048-bit · valid until %1").arg(pdf::PDFAppLocale::formatDate(janeInfo->getNotValidAfter().toLocalTime().date())));
    QVERIFY(janeRow->getTag().isEmpty());
    QCOMPARE(oldRow->getTitle(), QStringLiteral("Jane Novak (2020)"));
    QCOMPARE(oldRow->getDescription(), QStringLiteral("cert_1.pfx · expired on %1").arg(pdf::PDFAppLocale::formatDate(oldInfo->getNotValidAfter().toLocalTime().date())));
    QCOMPARE(oldRow->getDescriptionColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::AccentRedIcon));

    // The first row is chosen; the detail 436:9586 ... 436:9645 from the certificate itself (K6.14)
    QCOMPARE(janeRow->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(oldRow->getTone(), pdf::PDFTileRow::Tone::Muted);
    pdf::PDFAppCertificateDetails* details = gui->getDetails();
    QCOMPARE(H::title(details->getHeader()), QStringLiteral("Jane Novak"));
    QCOMPARE(H::subtitle(details->getHeader()), QStringLiteral("jane_novak.pfx"));
    QVERIFY(!details->getValidityCallout());
    using D = pdf::PDFAppCertificateDetails;
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Name")), QStringLiteral("Jane Novak"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Organization")), QStringLiteral("Acme Corp"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Unit")), QStringLiteral("Finance"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("E-mail")), QStringLiteral("jane@example.com"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Country")), QStringLiteral("CZ"));

    // section-Subject 436:9590, section-Certificate (review RS6FIGMA): the Key value rows of 28 are 8 apart
    QCOMPARE(details->getSubjectValues()->getRowGap(), 8);
    QCOMPARE(details->getCertificateValues()->getRowGap(), 8);
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Issued by")), QStringLiteral("Self-signed"));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Valid from")), pdf::PDFAppLocale::formatDate(janeInfo->getNotValidBefore().toLocalTime().date()));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Valid to")), pdf::PDFAppLocale::formatDate(janeInfo->getNotValidAfter().toLocalTime().date()));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Key")), QStringLiteral("RSA 2048-bit"));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Private key")), QStringLiteral("Password"));
    QCOMPARE(details->getUsageHeader()->getTitleLabel()->text(), QStringLiteral("Usable for"));
    QCOMPARE(H::usageTitles(details), QStringList({ QStringLiteral("Digital signatures"), QStringLiteral("Certificate encryption") }));

    // The status 436:9398 (no system store here)
    QCOMPARE(gui->getStatusText(), QStringLiteral("2 certificates · 2 files in your certificate folder"));

    // 436:11809: the expired certificate chosen - the red callout, "Private key: No password"
    QTest::mouseClick(oldRow, Qt::LeftButton);
    QCOMPARE(H::title(details->getHeader()), QStringLiteral("Jane Novak (2020)"));
    QVERIFY(details->getValidityCallout());
    QCOMPARE(details->getValidityCallout()->getTitleLabel()->text(), QStringLiteral("This certificate has expired"));
    QCOMPARE(details->getValidityCallout()->getTextLabel()->text(), QStringLiteral("It can no longer sign or encrypt. Keep it if you still need to open documents encrypted for it."));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Private key")), QStringLiteral("No password"));
}

void AppToolCertificatesTest::systemStoreRows()
{
    using H = AppToolCertificatesTestHelper;
    using Item = pdf::PDFAppToolCertificateItem;

    // 436:9830: a certificate of the system store - a store in the memory of the process (never the
    // store of the machine)
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString source = H::create(fixture.folder(), QStringLiteral("company.pfx"), QStringLiteral("Jane Novak – Company ID"), QStringLiteral("store-password"));
    QVERIFY(!source.isEmpty());
    std::shared_ptr<const pdf::PDFPersonalCertificateStore> store = pdf::PDFPersonalCertificateStore::createMemoryStore(H::read(source), QByteArrayLiteral("store-password"));
    if (!store)
    {
        QSKIP("No store of the system on this platform (Q6.2: Windows only)");
    }
    pdf::PDFPersonalCertificateStore::setStore(store);
    pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(true);
    std::optional<pdf::PDFCertificateInfo> info = H::certificate(source, QStringLiteral("store-password"));
    QVERIFY(info);
    const QString fingerprint = pdf::PDFDocumentToolEncryption::getCertificateFingerprint(info->getCertificateData());

    // The list of the signing (Q6.2): choosable, its entry has no PKCS#12 (the store signs); the list of
    // the encryption: choosable too (a recipient by its public certificate, the addendum of 15. 11.)
    pdf::PDFAppToolCertificateProvider provider(fixture.context.get());
    std::vector<Item> signing = provider.getCertificates(pdf::PDFAppToolCertificateUsage::Signing);
    QCOMPARE(signing.size(), size_t(1));
    QCOMPARE(signing.front().source, Item::Source::SystemStore);
    QCOMPARE(signing.front().state, Item::State::Valid);
    QCOMPARE(signing.front().fingerprint, fingerprint);
    std::optional<pdf::PDFCertificateEntry> entry = provider.findEntry(signing.front().filePath);
    QVERIFY(entry);
    QVERIFY(entry->pkcs12.isEmpty());
    QCOMPARE(entry->info.getCertificateData(), info->getCertificateData());
    std::vector<Item> encryption = provider.getCertificates(pdf::PDFAppToolCertificateUsage::Encryption);
    QCOMPARE(encryption.size(), size_t(1));
    QCOMPARE(encryption.front().state, Item::State::Valid);
    QVERIFY(encryption.front().isChoosable());

    // Manage Certificates: the card From the system store with the tag Read-only; Delete Disabled;
    // "Private key: In the system"
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);
    QVERIFY(gui->getSystemCard()->isVisible());
    QVERIFY(!gui->getFilesCard()->isVisible());
    pdf::PDFTileRow* row = gui->getCertificateRow(pdf::PDFAppToolCertificateProvider::getSystemStorePath(fingerprint));
    QVERIFY(row);
    QCOMPARE(row->getTitle(), QStringLiteral("Jane Novak – Company ID"));
    QCOMPARE(row->getTag(), QStringLiteral("Read-only"));
    QCOMPARE(row->getIconName(), QStringLiteral("monitor"));
    QCOMPARE(row->getDescription(), QStringLiteral("Acme Corp · self-signed · valid until %1").arg(pdf::PDFAppLocale::formatDate(info->getNotValidAfter().toLocalTime().date())));
    QTest::mouseClick(row, Qt::LeftButton);
    QVERIFY(!gui->getDeleteButton()->isEnabled());
    gui->openDeleteDialog();
    QVERIFY(!gui->getDeleteDialog());
    QCOMPARE(H::subtitle(gui->getDetails()->getHeader()), QStringLiteral("System store"));
    QCOMPARE(pdf::PDFAppCertificateDetails::getValue(gui->getDetails()->getCertificateValues(), QStringLiteral("Private key")), QStringLiteral("In the system"));
    QCOMPARE(gui->getStatusText(), QStringLiteral("1 certificate · 1 from the system store"));

    pdf::PDFPersonalCertificateStore::setStore(nullptr);
}

void AppToolCertificatesTest::emptyState()
{
    using H = AppToolCertificatesTestHelper;

    // 436:9646: no certificate
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);
    QVERIFY(gui->getEmptyState()->isVisible());
    QVERIFY(!gui->getFilesCard()->isVisible());
    QVERIFY(!gui->getSystemCard()->isVisible());
    QVERIFY(H::hasText(gui->getEmptyState(), QStringLiteral("No certificates yet")));
    QVERIFY(H::hasText(gui->getEmptyState(), QStringLiteral("Create a self-signed certificate for yourself, or import a .pfx file you received from a certificate authority.")));
    QPushButton* create = gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyCreate"));
    QPushButton* import = gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyImport"));
    QVERIFY(create && import);
    QCOMPARE(create->text(), QStringLiteral("Create Certificate…"));
    QCOMPARE(import->text(), QStringLiteral("Import .pfx…"));
    QVERIFY(create->isEnabled() && import->isEnabled());
    QVERIFY(!gui->getDeleteButton()->isEnabled());
    QCOMPARE(gui->getViewRow(H::GUI::View::My)->getShortcutText(), QStringLiteral("0"));
    QCOMPARE(H::title(gui->getDetails()->getHeader()), QStringLiteral("Details"));
    QCOMPARE(H::subtitle(gui->getDetails()->getHeader()), QStringLiteral("Select a certificate"));
    QCOMPARE(gui->getDetails()->getNoteText()->text(), QStringLiteral("The details of the selected certificate show here."));
    QCOMPARE(gui->getStatusText(), QStringLiteral("No certificates yet"));

    // Create Certificate… of the empty state opens the dialog of 436:10081
    QTest::mouseClick(create, Qt::LeftButton);
    QVERIFY(gui->getCreateDialog() && gui->getCreateDialog()->isVisible());

    // 436:10081 (review RS6FIGMA): every field shows its whole height - Text field 34, Password field 40;
    // the dialog is not shorter than its content (the fields were cut by 4 px)
    pdftest::AppDocumentFixture::settle();
    QPointer<pdf::PDFAppCertificateCreateDialog> createDialog = gui->getCreateDialog();
    QVERIFY(createDialog->height() >= createDialog->sizeHint().height());
    for (const char* name : { "certificateName", "certificateEmail", "certificateOrganization", "certificateUnit", "certificateFile" })
    {
        QWidget* field = createDialog->findChild<QWidget*>(QString::fromLatin1(name));
        QVERIFY2(field, name);
        QCOMPARE(field->height(), 34);
        QCOMPARE(field->visibleRegion().boundingRect().height(), 34);
    }
    for (const char* name : { "certificatePassword", "certificateRepeatPassword" })
    {
        QWidget* field = createDialog->findChild<QWidget*>(QString::fromLatin1(name));
        QVERIFY2(field, name);
        QCOMPARE(field->visibleRegion().boundingRect().height(), 40);
    }
    createDialog->reject();
    QTRY_VERIFY(!gui->getCreateDialog());
}

void AppToolCertificatesTest::deleteCertificate()
{
    using H = AppToolCertificatesTestHelper;

    // 436:11809: a file of the own folder, a file of an old application (Q5.5, read only)
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString own = H::create(folders.getApplicationFolder(), QStringLiteral("cert_1.pfx"), QStringLiteral("Jane Novak (2020)"), QString());
    const QString old = H::create(folders.getOldFolders().front(), QStringLiteral("editor.pfx"), QStringLiteral("Editor Holder"), QString());
    QVERIFY(!own.isEmpty() && !old.isEmpty());
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);

    // The file of an old application: Read-only, Delete Disabled, nothing opens
    pdf::PDFTileRow* oldRow = gui->getCertificateRow(old);
    QVERIFY(oldRow);
    QCOMPARE(oldRow->getTag(), QStringLiteral("Read-only"));
    QTest::mouseClick(oldRow, Qt::LeftButton);
    QVERIFY(!gui->getDeleteButton()->isEnabled());
    gui->openDeleteDialog();
    QVERIFY(!gui->getDeleteDialog());
    QVERIFY(gui->getCopyCallout()->isVisible());
    QVERIFY(QFile::exists(old));

    // The own file: the confirmation - Cancel (Primary) keeps the file
    QTest::mouseClick(gui->getCertificateRow(own), Qt::LeftButton);
    QVERIFY(gui->getDeleteButton()->isEnabled());
    QTest::mouseClick(gui->getDeleteButton(), Qt::LeftButton);
    QPointer<pdf::PDFMessageBox> dialog = gui->getDeleteDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(H::hasText(dialog, QStringLiteral("Delete certificate “Jane Novak (2020)”?")));
    QVERIFY(H::hasText(dialog, QStringLiteral("The file cert_1.pfx is deleted from your certificate folder. Documents encrypted for this certificate can no longer be opened with it. This cannot be undone.")));
    const QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(2));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Cancel"));
    QVERIFY(buttons[0]->isDefault());
    QCOMPARE(buttons[1]->text(), QStringLiteral("Delete Certificate"));
    QVERIFY(!buttons[1]->isDefault());
    QTest::mouseClick(buttons[0], Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    QVERIFY(QFile::exists(own));
    QCOMPARE(gui->getCertificates().size(), size_t(2));

    // Delete Certificate removes the file and its row of the certificate cache
    pdf::PDFAppToolCertificateProvider(fixture.context.get()).storeCertificate(own, H::certificate(own, QString())->getCertificateData());
    pdf::PDFCertificateCacheRepository cache(fixture.context->getDatabase());
    QVERIFY(cache.find(pdf::PDFRepositoryHelper::normalizePath(own)));
    QTest::mouseClick(gui->getDeleteButton(), Qt::LeftButton);
    dialog = gui->getDeleteDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getActionButtons()[1], Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    QVERIFY(!QFile::exists(own));
    QVERIFY(!cache.find(pdf::PDFRepositoryHelper::normalizePath(own)));
    QVERIFY(QFile::exists(old));
    QCOMPARE(gui->getCertificates().size(), size_t(1));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Certificate \"Jane Novak (2020)\" was deleted"));
}

void AppToolCertificatesTest::importCollision()
{
    using H = AppToolCertificatesTestHelper;

    // 436:11538: a file of the same name is never replaced; another file is imported, first, New
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString existing = H::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QString());
    const QString same = H::create(fixture.folder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Other Jane"), QString());
    const QString other = H::create(fixture.folder(), QStringLiteral("zz_imported.pfx"), QStringLiteral("Imported Holder"), QString());
    QVERIFY(!existing.isEmpty() && !same.isEmpty() && !other.isEmpty());
    const QByteArray existingData = H::read(existing);
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);

    pdf::PDFAppPanelFileDialogs::setOpenHandler([same](QWidget*, const QString&, const QString&, const QString&) { return same; });
    {
        // A0: the file comes from the event loop
        const pdftest::NestedEventLoopGuard loopGuard;
        QTest::mouseClick(gui->getImportButton(), Qt::LeftButton);
        pdftest::AppAsyncDialogs::settle();
    }
    QPointer<pdf::PDFAppCertificateImportDialog> dialog = gui->getImportDialog();
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->getOutcome(), pdf::PDFAppCertificateImportDialog::Outcome::Collision);
    QVERIFY(H::hasText(dialog, QStringLiteral("A certificate file with this name already exists")));
    QCOMPARE(H::read(existing), existingData);

    pdf::PDFAppPanelFileDialogs::setOpenHandler([other](QWidget*, const QString&, const QString&, const QString&) { return other; });
    QTest::mouseClick(dialog->getChooseButton(), Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());

    // The imported file is the first row with the tag New and it is chosen
    const QString imported = QDir(folders.getApplicationFolder()).absoluteFilePath(QStringLiteral("zz_imported.pfx"));
    QCOMPARE(gui->getCertificates().size(), size_t(2));
    QCOMPARE(QFileInfo(gui->getCertificates().front().filePath).fileName(), QStringLiteral("zz_imported.pfx"));
    pdf::PDFTileRow* row = gui->getCertificateRow(gui->getCertificates().front().filePath);
    QCOMPARE(row->getTag(), QStringLiteral("New"));
    QCOMPARE(row->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(H::read(imported), H::read(other));
    QCOMPARE(H::read(existing), existingData);
    QCOMPARE(gui->getStatusText(), QStringLiteral("The certificate file zz_imported.pfx was imported"));
}

void AppToolCertificatesTest::trustedView()
{
    using H = AppToolCertificatesTestHelper;
    using D = pdf::PDFAppCertificateDetails;

    // 436:12086: a certificate of the user and one of the system store
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    AppToolCertificatesProviderGuard provider(&H::systemTrusted);
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.addCertificate(pdftest::AppTestCertificates::first()).isError());
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);

    QTest::mouseClick(gui->getViewRow(H::GUI::View::Trusted), Qt::LeftButton);
    QCOMPARE(gui->getView(), H::GUI::View::Trusted);
    QCOMPARE(gui->getViewRow(H::GUI::View::Trusted)->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(gui->getViewRow(H::GUI::View::Trusted)->getShortcutText(), QStringLiteral("2"));

    // The toolbar 436:12106: Add Trusted…, Remove (icon list-remove)
    QVERIFY(!gui->getAddTrustedButton()->isHidden() && !gui->getRemoveTrustedButton()->isHidden());
    QVERIFY(gui->getCreateButton()->isHidden() && gui->getDeleteButton()->isHidden() && gui->getActionsDivider()->isHidden());
    QCOMPARE(gui->getAddTrustedButton()->text(), QStringLiteral("Add Trusted…"));
    QCOMPARE(gui->getRemoveTrustedButton()->text(), QStringLiteral("Remove"));

    // Left: the callout "Who do you trust?" with Settings › Security (K6.13)
    QCOMPARE(gui->getViewCallout()->getTitleLabel()->text(), QStringLiteral("Who do you trust?"));
    QCOMPARE(gui->getViewCallout()->getTextLabel()->text(), QStringLiteral("A signature is shown as valid when its certificate chain ends in one of these certificates. More verification options are in Settings › Security."));

    // Canvas 436:12226: the heading, the table of the model of Settings › Security with its header
    // view (1567:19625), the user row first, the note
    QVERIFY(gui->getTrustedPage()->isVisible() && !gui->getMyPage()->isVisible());
    QVERIFY(H::hasText(gui->getTrustedPage(), QStringLiteral("Trusted certificates")));
    QVERIFY(H::hasText(gui->getTrustedPage(), QStringLiteral("Certificates you rely on when signatures are verified.")));
    pdf::PDFAppCertificateTableModel* model = gui->getTrustedModel();
    QCOMPARE(gui->getTrustedView()->model(), model);
    QVERIFY(!gui->getTrustedView()->isHeaderHidden());
    const QStringList headers = { QStringLiteral("Type"), QStringLiteral("Certificate"), QStringLiteral("Organization"), QStringLiteral("Valid from"), QStringLiteral("Valid to") };
    for (int column = 0; column < headers.size(); ++column)
    {
        QCOMPARE(model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), headers[column]);
    }
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->getEntry(0)->type, pdf::PDFAppCertificateEntry::Type::User);
    QCOMPARE(model->data(model->index(0, pdf::PDFAppCertificateTableModel::TypeColumn), Qt::DisplayRole).toString(), QStringLiteral("User"));
    QCOMPARE(model->data(model->index(0, pdf::PDFAppCertificateTableModel::NameColumn), Qt::DisplayRole).toString(), QStringLiteral("PDF4QT test certificate 1"));
    QCOMPARE(model->data(model->index(1, pdf::PDFAppCertificateTableModel::TypeColumn), Qt::DisplayRole).toString(), QStringLiteral("System"));
    QCOMPARE(gui->getTrustedNote()->text(), QStringLiteral("“System” certificates come from the operating system and cannot be removed here. You can also trust a certificate from the Signatures panel of a signed document."));

    // The first row chosen; the detail 436:12273 ... 436:12323 (an EC key, no key usage = any use)
    const pdf::PDFCertificateInfo first = *pdf::PDFCertificateInfo::getCertificateInfo(pdftest::AppTestCertificates::first());
    pdf::PDFAppCertificateDetails* details = gui->getDetails();
    QCOMPARE(H::title(details->getHeader()), QStringLiteral("PDF4QT test certificate 1"));
    QCOMPARE(H::subtitle(details->getHeader()), QStringLiteral("Trusted · added by you"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Name")), QStringLiteral("PDF4QT test certificate 1"));
    QCOMPARE(D::getValue(details->getSubjectValues(), QStringLiteral("Organization")), QStringLiteral("PDF4QT tests"));
    QCOMPARE(details->getSubjectValues()->getRowCount(), 2);
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Valid from")), pdf::PDFAppLocale::formatDate(first.getNotValidBefore().toLocalTime().date()));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Valid to")), pdf::PDFAppLocale::formatDate(first.getNotValidAfter().toLocalTime().date()));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Key")), QStringLiteral("EC 256-bit"));
    QCOMPARE(D::getValue(details->getCertificateValues(), QStringLiteral("Serial number")), QStringLiteral("13:BA:1E:01:C5:05:D6:DC:1D:F7:60:54:70:3C:5D:9E:88:E2:E0:AA"));
    QCOMPARE(details->getUsageHeader()->getTitleLabel()->text(), QStringLiteral("Key usage"));
    QCOMPARE(H::usageTitles(details), QStringList({ QStringLiteral("Any use (no key usage is given)") }));
    QVERIFY(gui->getRemoveTrustedButton()->isEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("2 trusted certificates · 1 added by you, 1 from the system store"));

    // A system row cannot be removed here (436:12272)
    gui->chooseTrustedRow(1);
    QCOMPARE(H::subtitle(details->getHeader()), QStringLiteral("Trusted · from the system store"));
    QVERIFY(!gui->getRemoveTrustedButton()->isEnabled());
    gui->openRemoveTrustedDialog();
    QVERIFY(!gui->getRemoveTrustedDialog());

    // A change of Settings › Security (the repository) is shown by the notification of the database
    QVERIFY(!repository.addCertificate(pdftest::AppTestCertificates::second()).isError());
    QTRY_COMPARE_WITH_TIMEOUT(model->getCount(pdf::PDFAppCertificateEntry::Type::User), 2, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    QCOMPARE(gui->getViewRow(H::GUI::View::Trusted)->getShortcutText(), QStringLiteral("3"));
}

void AppToolCertificatesTest::addTrusted()
{
    using H = AppToolCertificatesTestHelper;

    // Add Trusted…: a PEM file, a DER file, a duplicate and a file, which is no certificate
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);
    gui->setView(H::GUI::View::Trusted);
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());

    const QString pemPath = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("second.pem"));
    const QString derPath = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("first.cer"));
    const QString textPath = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("text.crt"));
    QVERIFY(H::write(pemPath, H::pem(pdftest::AppTestCertificates::second())));
    QVERIFY(H::write(derPath, pdftest::AppTestCertificates::first()));
    QVERIFY(H::write(textPath, QByteArray("-----BEGIN CERTIFICATE-----\nnot a certificate\n-----END CERTIFICATE-----\n")));

    // The PEM: trusted, chosen, the status; Settings › Security reads the same row
    QString chosenPath = pemPath;
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&chosenPath](QWidget*, const QString&, const QString&, const QString&) { return chosenPath; });
    const pdftest::NestedEventLoopGuard loopGuard;
    QTest::mouseClick(gui->getAddTrustedButton(), Qt::LeftButton);
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(repository.contains(pdftest::AppTestCertificates::second()));
    QVERIFY(!gui->getAddTrustedDialog());
    QCOMPARE(gui->getChosenTrustedEntry()->data, pdftest::AppTestCertificates::second());
    QCOMPARE(gui->getStatusText(), QStringLiteral("“PDF4QT test certificate 2” is trusted now"));
    {
        pdf::PDFAppCertificateTable table(fixture.context.get());
        QCOMPARE(table.getModel()->getCount(pdf::PDFAppCertificateEntry::Type::User), 1);
        QCOMPARE(table.getModel()->getEntries().front().data, pdftest::AppTestCertificates::second());
    }

    // The DER file
    chosenPath = derPath;
    gui->addTrusted();
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(repository.contains(pdftest::AppTestCertificates::first()));
    QCOMPARE(int(repository.getRows().size()), 2);

    // The duplicate: refused with its message, Choose Another File… offered (2.6 as 436:11538)
    gui->addTrusted();
    pdftest::AppAsyncDialogs::settle();
    QPointer<pdf::PDFMessageBox> dialog = gui->getAddTrustedDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(H::hasText(dialog, QStringLiteral("This certificate is trusted already")));
    QVERIFY(H::hasText(dialog, QStringLiteral("“PDF4QT test certificate 1” is in your trusted certificates. Choose another file.")));
    QCOMPARE(dialog->getActionButtons().back()->text(), QStringLiteral("Choose Another File…"));
    QCOMPARE(int(repository.getRows().size()), 2);

    // Choose Another File… - the text file: refused
    chosenPath = textPath;
    QTest::mouseClick(dialog->getActionButtons().back(), Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    pdftest::AppAsyncDialogs::settle();
    dialog = gui->getAddTrustedDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(H::hasText(dialog, QStringLiteral("The certificate cannot be added")));
    QVERIFY(H::hasText(dialog, QStringLiteral("The file text.crt is not a certificate (DER or PEM) on this computer.")));
    QCOMPARE(int(repository.getRows().size()), 2);
    dialog->reject();
    QTRY_VERIFY(!dialog);
    pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());

    // RFC 7468: the first CERTIFICATE block of a PEM, other text around it
    QCOMPARE(H::GUI::readCertificateFile(H::pem(pdftest::AppTestCertificates::first())), pdftest::AppTestCertificates::first());
    QVERIFY(H::GUI::readCertificateFile(QByteArray("-----BEGIN CERTIFICATE-----\nMIIB")).isEmpty());
    QVERIFY(H::GUI::readCertificateFile(QByteArray()).isEmpty());
}

void AppToolCertificatesTest::removeTrusted()
{
    using H = AppToolCertificatesTestHelper;

    // 436:12324: Stop trusting …? - Cancel (Primary) keeps the trust, Remove removes it
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.addCertificate(pdftest::AppTestCertificates::first()).isError());
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);
    gui->setView(H::GUI::View::Trusted);
    QCOMPARE(gui->getTrustedModel()->rowCount(), 1);

    QTest::mouseClick(gui->getRemoveTrustedButton(), Qt::LeftButton);
    QPointer<pdf::PDFMessageBox> dialog = gui->getRemoveTrustedDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(H::hasText(dialog, QStringLiteral("Stop trusting “PDF4QT test certificate 1”?")));
    QVERIFY(H::hasText(dialog, QStringLiteral("Signatures made with certificates issued by it will no longer be shown as valid. You can add it again later.")));
    QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(2));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Cancel"));
    QVERIFY(buttons[0]->isDefault());
    QCOMPARE(buttons[1]->text(), QStringLiteral("Remove"));
    QTest::mouseClick(buttons[0], Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    QVERIFY(repository.contains(pdftest::AppTestCertificates::first()));

    QTest::mouseClick(gui->getRemoveTrustedButton(), Qt::LeftButton);
    dialog = gui->getRemoveTrustedDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getActionButtons()[1], Qt::LeftButton);
    QTRY_VERIFY(!dialog);
    QVERIFY(!repository.contains(pdftest::AppTestCertificates::first()));
    QCOMPARE(gui->getTrustedModel()->rowCount(), 0);
    QCOMPARE(gui->getStatusText(), QStringLiteral("“PDF4QT test certificate 1” is no longer trusted"));

    // Settings › Security reads the same table: the row is gone there too
    pdf::PDFAppCertificateTable table(fixture.context.get());
    QCOMPARE(table.getModel()->getCount(pdf::PDFAppCertificateEntry::Type::User), 0);
}

void AppToolCertificatesTest::portableMode()
{
    using H = AppToolCertificatesTestHelper;

    // Q6.13: --portable - Create, Import and Delete Disabled with the text of step 5, Open Folder and
    // the trusted certificates stay (the fixtures start with --portable; the test folders allow the
    // creation, which is turned off here)
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    QVERIFY(fixture.context->isPortable());
    pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(false);

    // Without a certificate: the buttons of the empty state Disabled too
    pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
    QVERIFY(gui);
    QPushButton* create = gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyCreate"));
    QPushButton* import = gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyImport"));
    QVERIFY(create && !create->isEnabled() && import && !import->isEnabled());
    QCOMPARE(create->toolTip(), pdf::PDFAppToolCertificatePresetPart::getPortableText());
    QVERIFY(H::hasText(gui->getEmptyState(), QStringLiteral("In portable mode no certificate can be created or imported – its private key would stay on this computer.")));
    gui->openCreateDialog();
    QVERIFY(!gui->getCreateDialog());
    gui->openImportDialog();
    QVERIFY(!gui->getImportDialog());

    // A file of the folder: Delete Disabled with the reason, the file stays; Open Folder works
    const QString own = H::create(folders.getApplicationFolder(), QStringLiteral("own.pfx"), QStringLiteral("Own Holder"), QString());
    QVERIFY(!own.isEmpty());
    gui->reload();
    QCOMPARE(gui->getCertificates().size(), size_t(1));
    for (QToolButton* button : { gui->getCreateButton(), gui->getImportButton(), gui->getDeleteButton() })
    {
        QVERIFY(!button->isEnabled());
        QCOMPARE(button->toolTip(), pdf::PDFAppToolCertificatePresetPart::getPortableText());
    }
    QVERIFY(gui->getFolderButton()->isEnabled());
    gui->openDeleteDialog();
    QVERIFY(!gui->getDeleteDialog());
    QVERIFY(QFile::exists(own));

    // The trusted certificates stay editable
    gui->setView(H::GUI::View::Trusted);
    QVERIFY(gui->getAddTrustedButton()->isEnabled());
    pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(true);
}

void AppToolCertificatesTest::coreToolResult()
{
    using H = AppToolCertificatesTestHelper;

    // Plan 6.5: the Analysis of protect.certificates lists the certificates and the trusted ones -
    // never a path, a file name or a secret
    pdftest::AppToolOwnTabFixture fixture;
    pdftest::AppTestCertificateFolders folders;
    const QString locked = H::create(folders.getApplicationFolder(), QStringLiteral("locked_canary.pfx"), QStringLiteral("Locked Holder"), QStringLiteral("Canary-Password-41"));
    const QString open = H::create(folders.getApplicationFolder(), QStringLiteral("open_canary.pfx"), QStringLiteral("Open Holder"), QString());
    QVERIFY(!locked.isEmpty() && !open.isEmpty());
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.addCertificate(pdftest::AppTestCertificates::first()).isError());

    std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(pdf::PDFAppDocumentToolCertificates::TOOL_ID));
    QVERIFY(tool);
    QVERIFY(tool->getParameters().empty());
    pdf::PDFDocumentToolContext context;
    context.certificateStore = pdf::PDFAppDocumentToolCertificates::createTrustedStore(fixture.context->getDatabase());
    const pdf::PDFDocumentToolResult result = tool->execute(context, QVariantMap(), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    QVERIFY(result.documents.empty() && result.writtenFiles.isEmpty());

    const QJsonArray certificates = result.data.value(QStringLiteral("certificates")).toArray();
    QCOMPARE(certificates.size(), 2);
    QJsonObject lockedRow;
    QJsonObject openRow;
    for (const QJsonValue& value : certificates)
    {
        (value.toObject().value(QStringLiteral("state")).toString() == QStringLiteral("locked") ? lockedRow : openRow) = value.toObject();
    }
    QCOMPARE(lockedRow.value(QStringLiteral("source")).toString(), QStringLiteral("file"));
    QVERIFY(!lockedRow.contains(QStringLiteral("name")));
    const QByteArray openCertificate = H::certificate(open, QString())->getCertificateData();
    QCOMPARE(openRow.value(QStringLiteral("state")).toString(), QStringLiteral("valid"));
    QCOMPARE(openRow.value(QStringLiteral("name")).toString(), QStringLiteral("Open Holder"));
    QCOMPARE(openRow.value(QStringLiteral("organization")).toString(), QStringLiteral("Acme Corp"));
    QCOMPARE(openRow.value(QStringLiteral("fingerprint")).toString(), QString::fromLatin1(QCryptographicHash::hash(openCertificate, QCryptographicHash::Sha256).toHex()));
    QCOMPARE(openRow.value(QStringLiteral("keyAlgorithm")).toString(), QStringLiteral("RSA"));
    QCOMPARE(openRow.value(QStringLiteral("keyBits")).toInteger(), qint64(2048));
    QCOMPARE(openRow.value(QStringLiteral("usableFor")).toArray(), QJsonArray({ QStringLiteral("signing"), QStringLiteral("encryption") }));
    QCOMPARE(openRow.value(QStringLiteral("readOnly")).toBool(), false);

    const QJsonArray trusted = result.data.value(QStringLiteral("trusted")).toArray();
    QCOMPARE(trusted.size(), 1);
    QCOMPARE(trusted.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("user"));
    QCOMPARE(trusted.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("PDF4QT test certificate 1"));
    QCOMPARE(trusted.at(0).toObject().value(QStringLiteral("fingerprint")).toString(), pdftest::AppTestCertificates::firstFingerprint());

    // No folder, no file name, no password anywhere in the result
    const QString text = QString::fromUtf8(QJsonDocument(result.toJson()).toJson(QJsonDocument::Compact));
    QVERIFY(!text.contains(QStringLiteral("canary"), Qt::CaseInsensitive));
    QVERIFY(!text.contains(QStringLiteral("Canary-Password-41")));
    QVERIFY(!text.contains(QDir::fromNativeSeparators(folders.getApplicationFolder()), Qt::CaseInsensitive));
    QVERIFY(!text.contains(QDir::toNativeSeparators(folders.getApplicationFolder()).replace(QLatin1Char('\\'), QStringLiteral("\\\\")), Qt::CaseInsensitive));
}

void AppToolCertificatesTest::screenshots()
{
    using H = AppToolCertificatesTestHelper;

    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS6FIGMA): the states of Manage Certificates under the ids
    // of their frames (436:9398 ... 436:12324) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color in any of them (Fluent)
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
        QStringList leaks;

        {
            // 436:9646: no certificate
            pdftest::AppToolOwnTabFixture fixture;
            pdftest::AppTestCertificateFolders folders;
            pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
            pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
            QVERIFY(gui);
            pdftest::AppDocumentFixture::settle();
            if (pdftest::AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("436-9646") + suffix)) > 0)
            {
                leaks << QStringLiteral("436-9646") + suffix;
            }
        }

        pdftest::AppToolOwnTabFixture fixture;
        pdftest::AppTestCertificateFolders folders;
        AppToolCertificatesProviderGuard provider(&H::systemTrusted);
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        const QString jane = H::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"));
        const QString old = H::create(folders.getApplicationFolder(), QStringLiteral("cert_1.pfx"), QStringLiteral("Jane Novak (2020)"), QString(), 24 * 3600);
        QVERIFY(!jane.isEmpty() && !old.isEmpty());
        QVERIFY(H::cache(fixture, jane, QStringLiteral("pfx-password")));
        pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
        QVERIFY(!repository.addCertificate(pdftest::AppTestCertificates::first()).isError());
        AppToolCertificatesTimeGuard time(QDateTime::currentDateTimeUtc().addDays(3));
        pdf::PDFAppToolGUICertificates* gui = H::open(fixture);
        QVERIFY(gui);

        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            pdftest::AppDocumentFixture::settle();
            if (pdftest::AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto shotWidget = [&suffix, &leaks](QWidget* widget, const QString& frame)
        {
            pdftest::AppDocumentFixture::settle();
            pdftest::AppToolShellFixture::screenshotDialog(widget, frame + suffix);
            if (widget && pdftest::AppToolShellFixture::countLeakPixels(pdftest::AppToolShellFixture::render(widget, 1.0)) > 0)
            {
                leaks << frame + suffix;
            }
        };

        QTest::mouseClick(gui->getCertificateRow(jane), Qt::LeftButton);
        shot(QStringLiteral("436-9398"));

        QTest::mouseClick(gui->getCertificateRow(old), Qt::LeftButton);
        gui->openDeleteDialog();
        QVERIFY(gui->getDeleteDialog());
        shot(QStringLiteral("436-11809"));
        shotWidget(gui->getDeleteDialog(), QStringLiteral("436-11809-dialog"));
        gui->getDeleteDialog()->reject();
        pdftest::AppDocumentFixture::settle();

        gui->openCreateDialog();
        QVERIFY(gui->getCreateDialog());
        shot(QStringLiteral("436-10081"));
        shotWidget(gui->getCreateDialog(), QStringLiteral("436-10081-dialog"));
        gui->getCreateDialog()->reject();
        pdftest::AppDocumentFixture::settle();

        gui->setView(H::GUI::View::Trusted);
        shot(QStringLiteral("436-12086"));
        gui->openRemoveTrustedDialog();
        if (gui->getRemoveTrustedDialog())
        {
            shot(QStringLiteral("436-12324"));
            shotWidget(gui->getRemoveTrustedDialog(), QStringLiteral("436-12324-dialog"));
            gui->getRemoveTrustedDialog()->reject();
            pdftest::AppDocumentFixture::settle();
        }

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolCertificatesTest)

#include "tst_apptoolcertificatestest.moc"
