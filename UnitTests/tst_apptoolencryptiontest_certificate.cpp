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

// The GUI of Encrypt with Certificate (step 5 of phase 2, package P3; plan 9): the second source of
// UnitTestsAppToolEncryption with the cases of P3 - the preset part of the certificates
// (PDFAppToolCertificatePresetPart: 433:3049, 433:3282, 433:3742, 433:3972; R5-21, R5-22, S5.1,
// Q5.5, Q5.11). The certificates are created at run time in certificate folders under the temporary
// folder of the case (PDFCertificateManager::setCertificateDirectories; never a folder of the user,
// never a committed key, nothing on the system disk); the folders are removed at the end of each case.

#include "apptestcertificatefolders.h"
#include "apptoolencryption.h"
#include "apptoolshellfixture.h"

#include "pdfappcommandline.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttab.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"
#include "pdfappcontext.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfcertificatecacherepository.h"
#include "pdfdatabase.h"

#include "pdfapptoolgui_encryption.h"
#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_encryption.h"
#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <memory>

/// A translator, which translates nothing: installing and removing it changes the language (the
/// LanguageChange of every widget)
class CertificateTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(sourceText);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// The environment of a case: the certificate folders under the temporary folder of the test
/// (PDFCertificateManager::setCertificateDirectories), a context with a database in the memory,
/// the part in a shown window
class CertificatePresetFixture
{
public:
    CertificatePresetFixture()
    {
        // The certificate folders of m_folders (pdftest::AppTestCertificateFolders)
        ++s_counter;

        const QStringList arguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));
        pdf::PDFDatabaseOptions options;
        options.memoryName = QStringLiteral("/pdf4qt-certificate-preset-%1-%2").arg(QCoreApplication::applicationPid()).arg(s_counter);
        context->open(options);

        window = std::make_unique<QWidget>();
        window->resize(814, 842);
    }

    ~CertificatePresetFixture()
    {
        delete part.data();
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        context.reset();
    }

    /// Creates the part and its content in the window
    void createPart()
    {
        part = new pdf::PDFAppToolCertificatePresetPart(context.get(), nullptr);
        QWidget* content = part->createCanvasContent(window.get());
        content->setFixedWidth(640);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    }

    /// Creates a certificate with its key into the certificate folder of the application
    static QString create(const QString& fileName, const QString& name, const QString& password)
    {
        const QString folder = pdf::PDFCertificateManager::getCertificateDirectory();
        QDir().mkpath(folder);
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(folder).absoluteFilePath(fileName);
        info.privateKeyPasword = password;
        info.certCommonName = name;
        info.certOrganization = QStringLiteral("Acme Corp");
        info.rsaKeyLength = 1024;
        QString error;
        return pdf::PDFCertificateManager().createCertificate(info, &error) ? info.fileName : QString();
    }

    static QByteArray read(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// Types into the field of the password card of P2 as the user does
    void typePassword(const QString& text) const
    {
        pdf::PDFLineEdit* field = part->getPasswordCard() ? part->getPasswordCard()->getPasswordField() : nullptr;
        QVERIFY(field);
        field->setFocus();
        QTest::keyClicks(field, text);
    }

    /// Every text of the widgets of the content but the password field (labels, tooltips, the
    /// accessible texts, the texts of buttons and rows)
    QStringList collectTexts() const
    {
        QStringList texts;
        const QWidget* field = part->getPasswordCard() ? part->getPasswordCard()->getPasswordField() : nullptr;
        for (const QWidget* widget : part->getContent()->findChildren<QWidget*>())
        {
            if (widget == field)
            {
                continue;
            }
            texts << widget->toolTip() << widget->accessibleName() << widget->accessibleDescription() << widget->statusTip() << widget->whatsThis();
            if (const QLabel* label = qobject_cast<const QLabel*>(widget))
            {
                texts << label->text();
            }
            if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget))
            {
                texts << button->text();
            }
            if (const pdf::PDFTileRow* row = qobject_cast<const pdf::PDFTileRow*>(widget))
            {
                texts << row->getDescription() << row->getTag();
            }
        }
        return texts;
    }

    std::unique_ptr<pdf::PDFAppContext> context;
    std::unique_ptr<QWidget> window;
    QPointer<pdf::PDFAppToolCertificatePresetPart> part;

private:
    pdftest::AppTestCertificateFolders m_folders;
    static int s_counter;
};

int CertificatePresetFixture::s_counter = 0;

/// The cases of package P3
class CertificatePresetCases
{
public:
    CertificatePresetCases() = delete;

    /// 433:3282: no certificate - the empty state with Create Certificate… / Import .pfx…, no list,
    /// no password card, the status message; Encrypt Document Disabled
    static void emptyState()
    {
        CertificatePresetFixture fixture;
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(!part->hasCertificates());
        QVERIFY(part->getEmptyState()->isVisible());
        QVERIFY(!part->getList()->isVisible());
        QVERIFY(!part->getPasswordFrame()->isVisible());
        QVERIFY(!part->isConfirmEnabled());
        QCOMPARE(part->getStatusText(), QStringLiteral("Create or import a certificate with a private key first"));
        QVERIFY(!part->getChosenCertificate());

        const QPushButton* create = part->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyCreate"));
        const QPushButton* import = part->getEmptyState()->findChild<QPushButton*>(QStringLiteral("certificateEmptyImport"));
        QVERIFY(create && import);
        QCOMPARE(create->text(), QStringLiteral("Create Certificate…"));
        QCOMPARE(import->text(), QStringLiteral("Import .pfx…"));

        // A certificate appears (created outside): refresh shows the list and chooses it
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());
        part->refresh();
        QVERIFY(part->hasCertificates());
        QVERIFY(!part->getEmptyState()->isVisible());
        QVERIFY(part->getList()->isVisible());
        QVERIFY(part->getPasswordFrame()->isVisible());
        QVERIFY(part->getChosenCertificate());
    }

    /// 433:3742, 433:3972: the label of the chosen file, the status, a wrong password - the error
    /// at the field and in the status, Disabled until the password changes
    static void wrongPassword()
    {
        CertificatePresetFixture fixture;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;

        QCOMPARE(part->getPasswordLabel()->text(), QStringLiteral("Password of jane_novak.pfx"));
        QCOMPARE(part->getStatusText(), QStringLiteral("Enter the password of the certificate file"));
        QVERIFY(!part->isConfirmEnabled());

        QSignalSpy changed(part, &pdf::PDFAppToolCertificatePresetPart::stateChanged);
        fixture.typePassword(QStringLiteral("wrong"));
        QVERIFY(changed.count() > 0);
        QVERIFY(part->isConfirmEnabled());

        QVariantMap parameters;
        std::vector<pdf::PDFCertificateEntry> certificates;
        QString error;
        QVERIFY(!part->prepareRun(&parameters, &certificates, &error));
        QCOMPARE(error, QStringLiteral("Invalid certificate or password."));
        QCOMPARE(part->getUnlockError(), QStringLiteral("Invalid certificate or password."));
        QCOMPARE(part->getStatusText(), QStringLiteral("Invalid certificate or password"));
        QVERIFY(parameters.isEmpty());
        QVERIFY(certificates.empty());
        QVERIFY(!part->isConfirmEnabled());

        // A change of the password removes the error
        fixture.typePassword(QStringLiteral("x"));
        QVERIFY(part->getUnlockError().isEmpty());
        QVERIFY(part->isConfirmEnabled());
    }

    /// The values of a run (plan 4.1, 10.3): the fingerprint (SHA-256 of the DER), the password, the
    /// entry with its PKCS#12; the row has its details afterwards (Q5.11) and the status names the
    /// holder (433:3049); clearSecrets empties the field (S5.1)
    static void runValues()
    {
        CertificatePresetFixture fixture;
        const QString path = CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"));
        QVERIFY(!path.isEmpty());
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QCOMPARE(part->getChosenCertificate()->state, pdf::PDFAppToolCertificateItem::State::Locked);

        fixture.typePassword(QStringLiteral("pfx-password"));
        QVariantMap parameters;
        std::vector<pdf::PDFCertificateEntry> certificates;
        QString error;
        QVERIFY(part->prepareRun(&parameters, &certificates, &error));

        const QByteArray pkcs12 = CertificatePresetFixture::read(path);
        const QByteArray der = pdf::PDFCertificateManager::readCertificate(pkcs12, QStringLiteral("pfx-password"))->getCertificateData();
        const QString fingerprint = QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex());
        QCOMPARE(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolEncryption::PARAMETER_CERTIFICATE)).toString(), fingerprint);
        QCOMPARE(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolEncryption::PARAMETER_CERTIFICATE_PASSWORD)).toString(), QStringLiteral("pfx-password"));
        QCOMPARE(certificates.size(), size_t(1));
        QCOMPARE(certificates.front().pkcs12, pkcs12);
        QCOMPARE(certificates.front().info.getCertificateData(), der);

        // The row is unlocked now: its name, Valid, the status of 433:3049
        QCOMPARE(part->getChosenCertificate()->state, pdf::PDFAppToolCertificateItem::State::Valid);
        QCOMPARE(part->getChosenCertificate()->fingerprint, fingerprint);
        QCOMPARE(part->getChosenCertificate()->keyBits, 1024);
        QCOMPARE(part->getStatusText(), QStringLiteral("Only the holder of the private key of “Jane Novak” will be able to open the document"));

        // stateChanged comes from the event loop, not inside of prepareRun
        QSignalSpy changed(part, &pdf::PDFAppToolCertificatePresetPart::stateChanged);
        QCOMPARE(changed.count(), 0);
        QTRY_COMPARE(changed.count(), 1);

        // S5.1: the field is empty after clearSecrets, no undo brings it back
        part->clearSecrets();
        QVERIFY(part->getPasswordCard()->getPasswordField()->text().isEmpty());
        QVERIFY(!part->getPasswordCard()->getPasswordField()->isUndoAvailable());
        QVERIFY(!part->isConfirmEnabled());
    }

    /// A .pfx file without a password: "Not needed" instead of the field, ready at once
    static void fileWithoutPassword()
    {
        CertificatePresetFixture fixture;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("open.pfx"), QStringLiteral("Open Holder"), QString()).isEmpty());
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(!part->getChosenCertificate()->isPasswordProtected);
        QVERIFY(part->isConfirmEnabled());
        QVERIFY(part->getPasswordFrame()->findChild<QWidget*>(QStringLiteral("certificateNoPassword"))->isVisible());
        QVERIFY(!part->getPasswordCard()->isVisible());

        QVariantMap parameters;
        std::vector<pdf::PDFCertificateEntry> certificates;
        QString error;
        QVERIFY(part->prepareRun(&parameters, &certificates, &error));
        QCOMPARE(certificates.size(), size_t(1));
        QVERIFY(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolEncryption::PARAMETER_CERTIFICATE_PASSWORD)).toString().isEmpty());
    }

    /// A certificate of the system store (the addendum of the owner of 15. 11.) - a store in the memory
    /// of the process, never the store of the machine: the row is choosable, no password is asked (the
    /// field hidden, "Not needed" shown), the run gets the fingerprint, no password and the entry of
    /// the store - its public certificate without PKCS#12 (the recipient, 7.6.5.3)
    static void systemStoreRecipient()
    {
        CertificatePresetFixture fixture;
        QTemporaryDir storeFolder;
        QVERIFY(storeFolder.isValid());
        const QString source = pdftest::AppTestCertificateFolders::create(storeFolder.path(), QStringLiteral("company.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("store-password"));
        QVERIFY(!source.isEmpty());
        const QByteArray pkcs12 = CertificatePresetFixture::read(source);
        std::shared_ptr<const pdf::PDFPersonalCertificateStore> store = pdf::PDFPersonalCertificateStore::createMemoryStore(pkcs12, QByteArrayLiteral("store-password"));
        if (!store)
        {
            QSKIP("No store of the system on this platform (Q6.2: Windows only)");
        }
        pdf::PDFPersonalCertificateStore::setStore(store);
        pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(true);
        auto storeGuard = qScopeGuard([]() { pdf::PDFPersonalCertificateStore::setStore(nullptr); pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(false); });

        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(part->hasCertificates());
        const std::optional<pdf::PDFAppToolCertificateItem> chosen = part->getChosenCertificate();
        QVERIFY(chosen);
        QCOMPARE(chosen->source, pdf::PDFAppToolCertificateItem::Source::SystemStore);
        QCOMPARE(chosen->state, pdf::PDFAppToolCertificateItem::State::Valid);
        QVERIFY(part->isConfirmEnabled());
        QVERIFY(part->getPasswordFrame()->findChild<QWidget*>(QStringLiteral("certificateNoPassword"))->isVisible());
        QVERIFY(!part->getPasswordCard()->isVisible());
        QCOMPARE(part->getStatusText(), QStringLiteral("Only the holder of the private key of “Jane Novak” will be able to open the document"));

        QVariantMap parameters;
        std::vector<pdf::PDFCertificateEntry> certificates;
        QString error;
        QVERIFY2(part->prepareRun(&parameters, &certificates, &error), qPrintable(error));
        const QByteArray der = pdf::PDFCertificateManager::readCertificate(pkcs12, QStringLiteral("store-password"))->getCertificateData();
        const QString fingerprint = QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex());
        QCOMPARE(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolEncryption::PARAMETER_CERTIFICATE)).toString(), fingerprint);
        QVERIFY(!parameters.contains(QString::fromLatin1(pdf::PDFDocumentToolEncryption::PARAMETER_CERTIFICATE_PASSWORD)));
        QCOMPARE(certificates.size(), size_t(1));
        QCOMPARE(certificates.front().type, pdf::PDFCertificateEntry::EntryType::System);
        QVERIFY(certificates.front().pkcs12.isEmpty());
        QCOMPARE(certificates.front().info.getCertificateData(), der);
        QVERIFY(pdf::PDFSecurityHandlerFactory::isSystemStoreCertificate(certificates.front()));
    }

    /// The choice: the last used certificate (the cache, plan 4.1), a click on another row, the
    /// password of the other file is not kept (S5.1)
    static void choice()
    {
        CertificatePresetFixture fixture;
        const QString first = CertificatePresetFixture::create(QStringLiteral("a_first.pfx"), QStringLiteral("First"), QStringLiteral("p1"));
        const QString second = CertificatePresetFixture::create(QStringLiteral("b_second.pfx"), QStringLiteral("Second"), QStringLiteral("p2"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());

        // The second file was used last: it is chosen first
        pdf::PDFAppToolCertificateProvider provider(fixture.context.get());
        QVERIFY(provider.unlock(second, QStringLiteral("p2"), nullptr));
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QCOMPARE(QFileInfo(part->getChosenCertificate()->filePath).fileName(), QStringLiteral("b_second.pfx"));

        fixture.typePassword(QStringLiteral("typed"));
        QSignalSpy changed(part, &pdf::PDFAppToolCertificatePresetPart::stateChanged);
        pdf::PDFTileRow* firstRow = nullptr;
        for (size_t i = 0; i < part->getList()->getCertificates().size(); ++i)
        {
            if (QFileInfo(part->getList()->getCertificates()[i].filePath).fileName() == QStringLiteral("a_first.pfx"))
            {
                firstRow = part->getList()->getRow(i);
            }
        }
        QVERIFY(firstRow);
        QTest::mouseClick(firstRow, Qt::LeftButton);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(QFileInfo(part->getChosenCertificate()->filePath).fileName(), QStringLiteral("a_first.pfx"));
        QCOMPARE(part->getPasswordLabel()->text(), QStringLiteral("Password of a_first.pfx"));
        QVERIFY(part->getPasswordCard()->getPasswordField()->text().isEmpty());
    }

    /// Owner 2026-11-15 (fix wave P2S5FIXAPP, item 7): in portable mode (--portable) Create
    /// Certificate… and Import… are disabled with the reason - the empty state, the toolbar, a call of
    /// code -, the texts say why; a .pfx file of the certificate folder is still listed and used
    static void portableMode()
    {
        CertificatePresetFixture fixture;
        QVERIFY(fixture.context->isPortable());
        pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(false);
        const auto restore = qScopeGuard([]() { pdf::PDFAppToolCertificatePresetPart::setCreationInPortableModeAllowed(true); });
        const QString reason = QStringLiteral("Not available in portable mode – the private key would stay on this computer");
        QCOMPARE(pdf::PDFAppToolCertificatePresetPart::getPortableText(), reason);

        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(!part->isCreationAvailable());
        for (const QString& name : { QStringLiteral("certificateEmptyCreate"), QStringLiteral("certificateEmptyImport") })
        {
            const QPushButton* button = part->getEmptyState()->findChild<QPushButton*>(name);
            QVERIFY2(button, qPrintable(name));
            QVERIFY2(!button->isEnabled(), qPrintable(name));
            QCOMPARE(button->toolTip(), reason);
        }
        QCOMPARE(part->getEmptyState()->findChild<QLabel*>(QStringLiteral("certificateEmptyText"))->text(),
                 QStringLiteral("In portable mode no certificate can be created or imported – its private key would stay on this computer. A .pfx file, which is in your certificate folder already, can be used."));
        QCOMPARE(part->getStatusText(), QStringLiteral("No certificate with a private key – none can be created or imported in portable mode"));

        // A call of code opens nothing: no dialog, no file dialog
        int fileDialogs = 0;
        pdf::PDFAppPanelFileDialogs::setOpenHandler([&fileDialogs](QWidget*, const QString&, const QString&, const QString&) { ++fileDialogs; return QString(); });
        part->openCreateDialog();
        part->openImportDialog();
        QTest::qWait(0);
        pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
        QVERIFY(!part->getCreateDialog());
        QVERIFY(!part->getImportDialog());
        QCOMPARE(fileDialogs, 0);

        // An existing .pfx file is listed and can be used
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());
        part->refresh();
        QVERIFY(part->hasCertificates());
        fixture.typePassword(QStringLiteral("pfx-password"));
        QVERIFY(part->isConfirmEnabled());

        // The toolbar of the shell (the window of the application, also --portable)
        pdftest::AppToolShellFixture shellFixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = shellFixture.open(pdftest::AppDocumentSamples::simple(shellFixture.folder(), QStringLiteral("plain.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        QPointer<pdf::PDFDocumentToolShell> shell = shellFixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_CERTIFICATE));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();
        for (const QString& name : { QStringLiteral("certificateCreateButton"), QStringLiteral("certificateImportButton") })
        {
            QToolButton* button = shell->getToolbar()->findChild<QToolButton*>(name);
            QVERIFY2(button, qPrintable(name));
            QVERIFY2(!button->isEnabled(), qPrintable(name));
            QCOMPARE(button->toolTip(), reason);
            QTest::mouseClick(button, Qt::LeftButton);
        }
        pdftest::AppToolShellFixture::settle();
        pdf::PDFAppToolCertificatePresetPart* shellPart = shell->getGUI() ? shell->getGUI()->findChild<pdf::PDFAppToolCertificatePresetPart*>() : nullptr;
        QVERIFY(shellPart);
        QVERIFY(!shellPart->getCreateDialog());
        QVERIFY(!shellPart->getImportDialog());
    }

    /// R5-21, Q5.3 b: restricted permissions show "You will not be able to change or remove this
    /// protection yourself"
    static void restrictedCallout()
    {
        CertificatePresetFixture fixture;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("p")).isEmpty());
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(!part->getRestrictionCallout()->isVisible());
        part->setPermissionsRestricted(true);
        QVERIFY(part->getRestrictionCallout()->isVisible());
        QCOMPARE(part->getRestrictionCallout()->getTitleLabel()->text(), QStringLiteral("You will not be able to change or remove this protection yourself"));
        part->setPermissionsRestricted(false);
        QVERIFY(!part->getRestrictionCallout()->isVisible());
    }

    /// Q5.5: a file of the old Editor - the offer to copy it; the copy is chosen afterwards
    static void oldFolderCopy()
    {
        CertificatePresetFixture fixture;
        const QString oldFolder = pdf::PDFCertificateManager::getOldCertificateDirectories().front();
        QDir().mkpath(oldFolder);
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(oldFolder).absoluteFilePath(QStringLiteral("editor.pfx"));
        info.privateKeyPasword = QStringLiteral("p");
        info.certCommonName = QStringLiteral("Editor Holder");
        info.rsaKeyLength = 1024;
        QVERIFY(pdf::PDFCertificateManager().createCertificate(info, nullptr));

        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;
        QVERIFY(part->getChosenCertificate()->isInOldFolder);
        QVERIFY(part->getCopyCallout()->isVisible());
        QCOMPARE(part->getCopyCallout()->getTitleLabel()->text(), QStringLiteral("This certificate file belongs to an older PDF4QT application"));

        QPushButton* copy = part->getCopyCallout()->findChild<QPushButton*>(QStringLiteral("certificateCopyButton"));
        QVERIFY(copy);
        QTest::mouseClick(copy, Qt::LeftButton);
        QVERIFY(!part->getChosenCertificate()->isInOldFolder);
        QCOMPARE(QFileInfo(part->getChosenCertificate()->filePath).absolutePath(), QFileInfo(pdf::PDFCertificateManager::getCertificateDirectory()).absoluteFilePath());
        QVERIFY(!part->getCopyCallout()->isVisible());
        QVERIFY(QFile::exists(info.fileName));
    }

    /// R5-22 (the canary): the password of the .pfx file is in no text of the content, in no row of
    /// the certificate cache (incl. -wal / -shm: the database is in the memory, its rows are read)
    /// and in no clipboard after a run and clearSecrets
    static void canary()
    {
        const QString canary = QStringLiteral("Canary-7f3a-Kanarek");
        CertificatePresetFixture fixture;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), canary).isEmpty());
        fixture.createPart();
        pdf::PDFAppToolCertificatePresetPart* part = fixture.part;

        // The field: Copy is refused (the skeleton's PDFLineEdit), also shown
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        fixture.typePassword(canary);
        pdf::PDFLineEdit* field = part->getPasswordCard()->getPasswordField();
        field->selectAll();
        QTest::keyClick(field, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));

        QVariantMap parameters;
        std::vector<pdf::PDFCertificateEntry> certificates;
        QString error;
        QVERIFY(part->prepareRun(&parameters, &certificates, &error));
        QCoreApplication::processEvents();
        for (const QString& text : fixture.collectTexts())
        {
            QVERIFY2(!text.contains(canary), qPrintable(text));
        }
        QVERIFY(!part->getStatusText().contains(canary));

        pdf::PDFCertificateCacheRepository repository(fixture.context->getDatabase());
        const std::vector<pdf::PDFCachedCertificate> rows = repository.getCertificates();
        QCOMPARE(rows.size(), size_t(1));
        for (const pdf::PDFCachedCertificate& row : rows)
        {
            QVERIFY(!row.data.contains(canary.toUtf8()));
            QVERIFY(!row.path.contains(canary));
            QVERIFY(!row.fingerprint.contains(canary));
        }

        part->clearSecrets();
        QVERIFY(field->text().isEmpty());
    }
};

/// The preset in the shell of the application (the GUI of P2 places the part): the toolbar of
/// 433:3069, Encrypt Document Disabled without the password (433:3742) and enabled with it, the run
/// to the result dialog (433:4721)
class CertificateShellCases
{
public:
    CertificateShellCases() = delete;

    static void shellRun()
    {
        CertificatePresetFixture folders;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());

        pdftest::AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_CERTIFICATE));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();

        pdf::PDFAppToolCertificatePresetPart* part = shell->getGUI() ? shell->getGUI()->findChild<pdf::PDFAppToolCertificatePresetPart*>() : nullptr;
        QVERIFY(part);
        QToolButton* create = shell->getToolbar()->findChild<QToolButton*>(QStringLiteral("certificateCreateButton"));
        QToolButton* import = shell->getToolbar()->findChild<QToolButton*>(QStringLiteral("certificateImportButton"));
        QVERIFY(create && import);
        QCOMPARE(create->text(), QStringLiteral("Create Certificate…"));
        QCOMPARE(import->text(), QStringLiteral("Import…"));
        QVERIFY(part->getContent() && part->getContent()->isVisible());

        QPushButton* confirm = shell->getToolbar()->getConfirmButton();
        QVERIFY(!confirm->isEnabled());
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Enter the password of the certificate file"));

        pdf::PDFLineEdit* field = part->getPasswordCard()->getPasswordField();
        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("pfx-password"));
        pdftest::AppToolShellFixture::settle();
        QVERIFY(confirm->isEnabled());

        QTest::mouseClick(confirm, Qt::LeftButton);
        pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell || (shell->getResultDialog() && shell->getResultDialog()->isVisible()); });
        QVERIFY(shell);
        QVERIFY2(shell->getResultDialog(), qPrintable(shell->getStatusMessage()));

        // S5.1: the field is empty after the run
        QVERIFY(field->text().isEmpty());
        shell->getResultDialog()->reject();
        pdftest::AppToolShellFixture::settle();
    }

    /// Review RS5APP (fix wave P2S5FIXAPP, item 9): a change of the language in Encrypt with
    /// Certificate builds the content of the preset once - one heading, the choice kept, the typed
    /// password of the .pfx file gone with its old field (S5.1)
    static void languageChange()
    {
        CertificatePresetFixture folders;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());

        pdftest::AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_CERTIFICATE));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();
        pdf::PDFAppToolGUIEncryption* gui = qobject_cast<pdf::PDFAppToolGUIEncryption*>(shell->getGUI());
        QPointer<pdf::PDFAppToolCertificatePresetPart> part = gui ? gui->getCertificatePart() : nullptr;
        QVERIFY(part);
        QVERIFY(part->getChosenCertificate());
        const QString chosen = part->getList()->getChosenFilePath();
        part->getPasswordCard()->getPasswordField()->setFocus();
        QTest::keyClicks(part->getPasswordCard()->getPasswordField(), QStringLiteral("pfx-password"));
        QPointer<QWidget> oldContent = part->getContent();

        CertificateTestTranslator translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));
        pdftest::AppToolShellFixture::settle();
        QVERIFY(QCoreApplication::removeTranslator(&translator));
        pdftest::AppToolShellFixture::settle();

        QVERIFY(shell && part);
        QVERIFY(!oldContent);
        QVERIFY(part->getContent());
        QCOMPARE(part->getList()->getChosenFilePath(), chosen);
        QVERIFY(part->getPasswordCard()->getPasswordField()->text().isEmpty());
        int headings = 0;
        for (const QLabel* label : gui->getForm()->findChildren<QLabel*>())
        {
            headings += label->text() == QStringLiteral("Encrypt for your digital ID") ? 1 : 0;
        }
        QCOMPARE(headings, 1);
    }

    /// Review RS5APP (fix wave P2S5FIXAPP, item 9; S5.1): Encrypt Document, whose run does not start (a
    /// foreign blocking operation of the document), keeps no password of the .pfx file in the values
    /// of the GUI prepared for the run
    static void shellRunRefused()
    {
        CertificatePresetFixture folders;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());

        pdftest::AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_CERTIFICATE));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();
        pdf::PDFAppToolGUIEncryption* gui = qobject_cast<pdf::PDFAppToolGUIEncryption*>(shell->getGUI());
        pdf::PDFAppToolCertificatePresetPart* part = gui ? gui->getCertificatePart() : nullptr;
        QVERIFY(part);
        pdf::PDFLineEdit* field = part->getPasswordCard()->getPasswordField();
        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("pfx-password"));
        pdftest::AppToolShellFixture::settle();
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

        pdf::PDFAppDocumentOperation* operation = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Saving plain.pdf"), true);
        QVERIFY(operation);
        QVERIFY(tab->getDocument()->isBlockedByOperation());
        shell->confirm();
        pdftest::AppToolShellFixture::settle();
        QVERIFY(shell);
        QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
        QVERIFY(gui->getCertificateRunValues().isEmpty());
        operation->finish();
        pdftest::AppToolShellFixture::settle();
    }

    /// 433:3972 in the shell (review RS5APP): Encrypt Document with a wrong password of the .pfx file
    /// starts no run - the field says "Invalid certificate or password.", the status says it, Encrypt
    /// Document is Disabled until the password changes (the error is not cleared by a run)
    static void shellWrongPassword()
    {
        CertificatePresetFixture folders;
        QVERIFY(!CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password")).isEmpty());

        pdftest::AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_CERTIFICATE));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();
        const quint64 version = tab->getDocument()->getVersion();

        pdf::PDFAppToolCertificatePresetPart* part = shell->getGUI()->findChild<pdf::PDFAppToolCertificatePresetPart*>();
        QVERIFY(part);
        QPushButton* confirm = shell->getToolbar()->getConfirmButton();
        pdf::PDFLineEdit* field = part->getPasswordCard()->getPasswordField();
        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("wrong"));
        pdftest::AppToolShellFixture::settle();
        QVERIFY(confirm->isEnabled());

        QTest::mouseClick(confirm, Qt::LeftButton);
        pdftest::AppToolShellFixture::settle();
        QVERIFY(shell);
        QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
        QVERIFY(!shell->getResultDialog());
        QCOMPARE(part->getPasswordCard()->getErrorText(), QStringLiteral("Invalid certificate or password."));
        QCOMPARE(shell->getStatusMessage(), QStringLiteral("Invalid certificate or password"));
        QVERIFY(!confirm->isEnabled());
        QCOMPARE(tab->getDocument()->getVersion(), version);

        // A changed password removes the error and enables the button again
        QTest::keyClick(field, Qt::Key_Backspace);
        pdftest::AppToolShellFixture::settle();
        QVERIFY(part->getPasswordCard()->getErrorText().isEmpty());
        QVERIFY(confirm->isEnabled());
    }

    /// 433:5811 (P2S5INT, item 7): a document encrypted for a certificate (7.6.5, adbe.pkcs7.s5) is
    /// opened by the private key of the .pfx of the user; the handler keeps the public certificate of
    /// that recipient (never the key), so the Now card of Remove Password says for whom it opens -
    /// the common name of the certificate - instead of "Private key holder"
    static void opensFor()
    {
        CertificatePresetFixture folders;
        const QString pfxPath = CertificatePresetFixture::create(QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QString());
        QVERIFY(!pfxPath.isEmpty());

        // The document of the core: the factory encrypts it for the certificate with every permission
        pdftest::AppToolShellFixture fixture(QSize(1440, 960));
        const QString source = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 2);
        pdf::PDFDocumentReader sourceReader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        const pdf::PDFDocument document = sourceReader.readFromFile(source);
        QCOMPARE(sourceReader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::Certificate;
        settings.permissions = 0xFFFFFFFCu;
        settings.certificate.pkcs12 = CertificatePresetFixture::read(pfxPath);
        settings.certificate.pkcs12fileName = QFileInfo(pfxPath).fileName();
        QString error;
        pdf::PDFSecurityHandlerPointer handler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings, &error);
        QVERIFY2(handler, qPrintable(error));
        pdf::PDFDocumentBuilder builder(&document);
        builder.setSecurityHandler(handler);
        pdf::PDFDocument encrypted = builder.build();
        const QString filePath = fixture.path(QStringLiteral("For_Jane.pdf"));
        QVERIFY(pdftest::AppDocumentSamples::write(encrypted, filePath));

        // The core: the reader opens it by the .pfx of the certificate folder; the recipient is Jane
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument opened = reader.readFromFile(filePath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const pdf::PDFSecurityHandler* openedHandler = opened.getStorage().getSecurityHandler();
        QVERIFY(openedHandler && openedHandler->getMode() == pdf::EncryptionMode::PublicKey);
        const std::optional<pdf::PDFCertificateInfo>& recipient = static_cast<const pdf::PDFPublicKeySecurityHandler*>(openedHandler)->getAuthorizedCertificate();
        QVERIFY(recipient);
        QCOMPARE(recipient->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));
        QCOMPARE(recipient->getName(pdf::PDFCertificateInfo::OrganizationName), QStringLiteral("Acme Corp"));
        QCOMPARE(recipient->getCertificateData(), pdf::PDFCertificateManager::readCertificate(settings.certificate.pkcs12, QString())->getCertificateData());

        // The application: Remove Password, the Now card of 433:5811
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        QVERIFY(tab);
        QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
        pdftest::AppToolShellFixture::settle();
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolEncryption::TOOL_ID_UNLOCK));
        QVERIFY(shell);
        pdftest::AppToolShellFixture::settle();
        pdf::PDFAppToolGUIEncryption* gui = qobject_cast<pdf::PDFAppToolGUIEncryption*>(shell->getGUI());
        QVERIFY(gui);
        QStringList rows;
        for (int i = 0; i < gui->getNowRows()->getRowCount(); ++i)
        {
            rows << gui->getNowRows()->getKey(i) + QStringLiteral(": ") + gui->getNowRows()->getValue(i);
        }
        QCOMPARE(rows, QStringList({ QStringLiteral("Encryption: Certificate"), QStringLiteral("Opens for: Jane Novak"), QStringLiteral("Permissions: All allowed"),
                                     QStringLiteral("Your access: Private key") }));
    }
};

void AppToolEncryptionPackages::registerCertificateCases(std::vector<AppToolEncryptionCase>* cases)
{
    cases->push_back({ QStringLiteral("certificate-shell-run"), &CertificateShellCases::shellRun });
    cases->push_back({ QStringLiteral("certificate-shell-wrong-password"), &CertificateShellCases::shellWrongPassword });
    cases->push_back({ QStringLiteral("certificate-shell-run-refused"), &CertificateShellCases::shellRunRefused });
    cases->push_back({ QStringLiteral("certificate-language-change"), &CertificateShellCases::languageChange });
    cases->push_back({ QStringLiteral("certificate-opens-for"), &CertificateShellCases::opensFor });
    cases->push_back({ QStringLiteral("certificate-empty"), &CertificatePresetCases::emptyState });
    cases->push_back({ QStringLiteral("certificate-wrong-password"), &CertificatePresetCases::wrongPassword });
    cases->push_back({ QStringLiteral("certificate-run-values"), &CertificatePresetCases::runValues });
    cases->push_back({ QStringLiteral("certificate-no-password"), &CertificatePresetCases::fileWithoutPassword });
    cases->push_back({ QStringLiteral("certificate-system-store"), &CertificatePresetCases::systemStoreRecipient });
    cases->push_back({ QStringLiteral("certificate-choice"), &CertificatePresetCases::choice });
    cases->push_back({ QStringLiteral("certificate-restricted"), &CertificatePresetCases::restrictedCallout });
    cases->push_back({ QStringLiteral("certificate-portable"), &CertificatePresetCases::portableMode });
    cases->push_back({ QStringLiteral("certificate-old-folder"), &CertificatePresetCases::oldFolderCopy });
    cases->push_back({ QStringLiteral("certificate-canary"), &CertificatePresetCases::canary });
}
