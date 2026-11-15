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

// The dialogs Create a self-signed certificate and Import of a .pfx file, the provider and the list
// of the certificates (step 5 of phase 2, package P3; plan 9: 436:10081, 436:10427, 436:10928,
// 436:11538, 433:3049; owner answers Q5.4, Q5.5, Q5.11; B99, B103). The certificates are created by
// the test at run time in certificate folders under its temporary folder
// (PDFCertificateManager::setCertificateDirectories; never the folders of the user, never a
// committed key, nothing on the system disk); the folders are removed at the end.

#include "apptestcertificatefolders.h"

#include "pdfappcertificatedialogs.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapplocale.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfcertificatecacherepository.h"
#include "pdfdatabase.h"
#include "pdfrepositoryhelper.h"

#include "pdfcertificatemanager.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_encryption.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

/// The certificate folders of the test (a temporary folder of the test, never a folder of the
/// user: PDFCertificateManager::setCertificateDirectories), the context with a database in the
/// memory, and the certificates created at run time
class CertificateTestEnvironment
{
public:
    CertificateTestEnvironment()
    {
        // The folders of PDF4QT and of the old Editor and Viewer (Q5.5) under the temporary folder
        // (m_folders, pdftest::AppTestCertificateFolders)
        ++s_counter;

        const QStringList arguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));
        pdf::PDFDatabaseOptions options;
        options.memoryName = QStringLiteral("/pdf4qt-certificates-%1-%2").arg(QCoreApplication::applicationPid()).arg(s_counter);
        context->open(options);
    }

    ~CertificateTestEnvironment()
    {
        context.reset();
        pdf::PDFAppLocale::setCurrentTimeOverride(0);
        pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
    }

    /// The folder of the application and the first folder of an old application (Q5.5)
    static QString applicationFolder() { return pdf::PDFCertificateManager::getCertificateDirectory(); }
    static QString oldFolder() { return pdf::PDFCertificateManager::getOldCertificateDirectories().front(); }

    /// A folder outside of the certificate folders (the source of an import)
    QString sourceFolder() const { return m_source.path(); }

    /// Creates a certificate with its key into the file (the core of P1b; keyEncipherment, B99)
    static QString create(const QString& folder, const QString& fileName, const QString& name, const QString& password, int validityInSeconds = 365 * 24 * 3600)
    {
        QDir().mkpath(folder);
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(folder).absoluteFilePath(fileName);
        info.privateKeyPasword = password;
        info.certCommonName = name;
        info.certOrganization = QStringLiteral("Acme Corp");
        info.certEmail = QStringLiteral("jane.novak@example.com");
        info.certCountryCode = QStringLiteral("CZ");
        info.rsaKeyLength = 1024;
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
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        QFile file(filePath);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    /// SHA-256 of the DER as 64 lower case hexadecimal digits (the definition of the parameter
    /// certificate, PDFDocumentToolEncryption)
    static QString fingerprint(const QByteArray& der)
    {
        return QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex());
    }

    /// A label of the widget tree has the text
    static bool hasLabel(const QWidget* widget, const QString& text)
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

    std::unique_ptr<pdf::PDFAppContext> context;

private:
    pdftest::AppTestCertificateFolders m_folders;
    QTemporaryDir m_source;
    static int s_counter;
};

int CertificateTestEnvironment::s_counter = 0;

class AppCertificateDialogsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void dialogsDeleteThemselves();
    void createDialogDefaults();
    void createDialogValidation();
    void createDialogCreatesCertificate();
    void createDialogClearsPasswords();
    void importCopiesFile();
    void importCollision();
    void importRefusesHostileFiles();
    void providerStatesOfFiles();
    void providerStatesOfCertificates();
    void providerOldFolder();
    void listChoosesRows();
};

void AppCertificateDialogsTest::initTestCase()
{
    // The fonts and the style of the application (offscreen has no fonts; without the style the
    // fields would show the leak palette)
    QVERIFY(pdf::PDFThemeFonts::loadFonts());
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void AppCertificateDialogsTest::dialogsDeleteThemselves()
{
    // CLAUDE.md: a popup is new + QPointer, never on the stack; closing it deletes it, the opener
    // only forgets its pointer
    CertificateTestEnvironment environment;
    QWidget parent;
    QPointer<pdf::PDFAppCertificateCreateDialog> create = new pdf::PDFAppCertificateCreateDialog(nullptr, &parent);
    create->show();
    create->close();
    QTRY_VERIFY(!create);

    QPointer<pdf::PDFAppCertificateImportDialog> import = new pdf::PDFAppCertificateImportDialog(nullptr, QStringLiteral("C:/certificates/a.pfx"), &parent);
    import->show();
    import->close();
    QTRY_VERIFY(!import);

    // Cancel of the form rejects it - also a deletion
    QPointer<pdf::PDFAppCertificateCreateDialog> canceled = new pdf::PDFAppCertificateCreateDialog(nullptr, &parent);
    canceled->show();
    QTest::mouseClick(canceled->getCancelButton(), Qt::LeftButton);
    QTRY_VERIFY(!canceled);
}

void AppCertificateDialogsTest::createDialogDefaults()
{
    CertificateTestEnvironment environment;
    QVERIFY(!CertificateTestEnvironment::create(CertificateTestEnvironment::applicationFolder(), QStringLiteral("cert_1.pfx"), QStringLiteral("Existing"), QStringLiteral("x")).isEmpty());

    QWidget parent;
    QPointer<pdf::PDFAppCertificateCreateDialog> dialog = new pdf::PDFAppCertificateCreateDialog(environment.context.get(), &parent);
    dialog->show();

    // 436:10336, 436:10337: the head
    QVERIFY(CertificateTestEnvironment::hasLabel(dialog, QStringLiteral("Create a self-signed certificate")));
    QVERIFY(CertificateTestEnvironment::hasLabel(dialog, QStringLiteral("A digital ID for signing and encryption. Others have to add it to their trusted certificates to see your signatures as valid.")));
    QCOMPARE(dialog->getCardWidth(), 640);

    // 436:10427: the key lengths with their descriptions, 2048 by default
    pdf::PDFComboBox* keyLength = dialog->getKeyLengthCombo();
    QCOMPARE(keyLength->count(), 3);
    QCOMPARE(keyLength->itemText(0), QStringLiteral("1024 bits"));
    QCOMPARE(keyLength->itemData(0, pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Weak · only for old systems"));
    QCOMPARE(keyLength->itemText(1), QStringLiteral("2048 bits"));
    QCOMPARE(keyLength->itemData(1, pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Recommended"));
    QCOMPARE(keyLength->itemText(2), QStringLiteral("4096 bits"));
    QCOMPARE(keyLength->itemData(2, pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("Strongest · slower signing"));
    QCOMPARE(keyLength->currentData().toInt(), 2048);
    QCOMPARE(keyLength->itemData(1, Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(keyLength->itemData(0, Qt::CheckStateRole).toInt(), int(Qt::Unchecked));

    // 436:10427: the countries "CZ | Česko", sorted by the code
    pdf::PDFComboBox* country = dialog->getCountryCombo();
    QVERIFY(country->count() > 100);
    const int czechia = country->findData(QStringLiteral("CZ"));
    QVERIFY(czechia >= 0);
    QCOMPARE(country->itemText(czechia), QStringLiteral("CZ | Česko"));
    for (int i = 1; i < country->count(); ++i)
    {
        QVERIFY2(country->itemData(i - 1).toString() < country->itemData(i).toString(), qPrintable(country->itemText(i)));
    }

    // Valid until: earliest today, five years ahead by default (the frame's caption of 436:10427)
    const QDate today = QDate::currentDate();
    QCOMPARE(dialog->getValidUntilEdit()->date(), today.addYears(5));
    QCOMPARE(dialog->getValidUntilEdit()->minimumDate(), today);
    QVERIFY(dialog->getValidUntilEdit()->calendarPopup());

    // Saved as: the next free cert_N.pfx of the certificate folder, not editable
    QCOMPARE(dialog->getFileField()->text(), QStringLiteral("cert_2.pfx"));
    QVERIFY(!dialog->getFileField()->isEnabled());
    QCOMPARE(QFileInfo(dialog->getFilePath()).absolutePath(), QFileInfo(CertificateTestEnvironment::applicationFolder()).absoluteFilePath());

    // The passwords are password fields; the empty form cannot be created
    QVERIFY(dialog->getPasswordField()->isPasswordField());
    QVERIFY(dialog->getRepeatField()->isPasswordField());
    QVERIFY(!dialog->getCreateButton()->isEnabled());
    QVERIFY(dialog->getNameError().isEmpty());

    dialog->close();
    QTRY_VERIFY(!dialog);
}

void AppCertificateDialogsTest::createDialogValidation()
{
    CertificateTestEnvironment environment;
    QWidget parent;
    QPointer<pdf::PDFAppCertificateCreateDialog> dialog = new pdf::PDFAppCertificateCreateDialog(environment.context.get(), &parent);
    dialog->show();

    // 436:10928: the name typed and removed - "Enter a name."
    QTest::keyClicks(dialog->getNameField(), QStringLiteral("J"));
    QTest::keyClick(dialog->getNameField(), Qt::Key_Backspace);
    QCOMPARE(dialog->getNameError(), QStringLiteral("Enter a name."));
    QVERIFY(dialog->getNameField()->isInvalid());

    QTest::keyClicks(dialog->getNameField(), QStringLiteral("Jane Novak"));
    QVERIFY(dialog->getNameError().isEmpty());
    QTest::keyClicks(dialog->getEmailField(), QStringLiteral("jane.novak"));
    QCOMPARE(dialog->getEmailError(), QStringLiteral("Enter a valid e-mail address."));
    QTest::keyClicks(dialog->getEmailField(), QStringLiteral("@example.com"));
    QVERIFY(dialog->getEmailError().isEmpty());
    QVERIFY(!dialog->getCreateButton()->isEnabled());
    QTest::keyClicks(dialog->getOrganizationField(), QStringLiteral("Acme Corp"));
    QVERIFY(dialog->getCreateButton()->isEnabled());

    // 436:10928: "The passwords do not match." under Repeat password, Create Disabled
    QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("secret-1"));
    QVERIFY(!dialog->getCreateButton()->isEnabled());
    QTest::keyClicks(dialog->getRepeatField(), QStringLiteral("secret-2"));
    QCOMPARE(dialog->getRepeatError(), QStringLiteral("The passwords do not match."));
    QVERIFY(dialog->getRepeatField()->isInvalid());
    QVERIFY(!dialog->getCreateButton()->isEnabled());
    QTest::keyClick(dialog->getRepeatField(), Qt::Key_Backspace);
    QTest::keyClicks(dialog->getRepeatField(), QStringLiteral("1"));
    QVERIFY(dialog->getRepeatError().isEmpty());
    QVERIFY(dialog->getCreateButton()->isEnabled());

    // 436:10412: the password may be empty
    dialog->getPasswordField()->clear();
    dialog->getRepeatField()->clear();
    QVERIFY(dialog->getCreateButton()->isEnabled());

    dialog->close();
    QTRY_VERIFY(!dialog);
}

void AppCertificateDialogsTest::createDialogCreatesCertificate()
{
    CertificateTestEnvironment environment;
    QWidget parent;
    QPointer<pdf::PDFAppCertificateCreateDialog> dialog = new pdf::PDFAppCertificateCreateDialog(environment.context.get(), &parent);
    dialog->show();
    QSignalSpy created(dialog.data(), &pdf::PDFAppCertificateCreateDialog::certificateCreated);

    const QString password = QStringLiteral("Heslo-ž-123");
    QTest::keyClicks(dialog->getNameField(), QStringLiteral("Jane Novak"));
    QTest::keyClicks(dialog->getEmailField(), QStringLiteral("jane.novak@example.com"));
    QTest::keyClicks(dialog->getOrganizationField(), QStringLiteral("Acme Corp"));
    QTest::keyClicks(dialog->getUnitField(), QStringLiteral("Finance"));
    dialog->getCountryCombo()->setCurrentIndex(dialog->getCountryCombo()->findData(QStringLiteral("DE")));
    dialog->getKeyLengthCombo()->setCurrentIndex(dialog->getKeyLengthCombo()->findData(1024));
    const QDate validUntil = QDate::currentDate().addYears(2).addDays(3);
    dialog->getValidUntilEdit()->setDate(validUntil);
    // QTest types only Latin-1 keys: the non-ASCII password is inserted as an input method does
    dialog->getPasswordField()->setFocus();
    dialog->getPasswordField()->insert(password);
    dialog->getRepeatField()->setFocus();
    dialog->getRepeatField()->insert(password);
    const QString filePath = dialog->getFilePath();

    QTest::mouseClick(dialog->getCreateButton(), Qt::LeftButton);
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.front().front().toString(), filePath);
    QTRY_VERIFY(!dialog);

    // The .pfx opens by the password (UTF-8, B84) and not without it
    const QByteArray pkcs12 = CertificateTestEnvironment::read(filePath);
    QVERIFY(pdf::PDFCertificateManager::isPkcs12(pkcs12));
    QVERIFY(!pdf::PDFCertificateManager::readCertificate(pkcs12, QStringLiteral("wrong")));
    std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(pkcs12, password);
    QVERIFY(info);

    // The subject of the form, the key length of the dialog (not the default 2048), keyEncipherment
    // for the encryption by a certificate (B99, ISO 32000-2 7.6.5), the end of the chosen day
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::OrganizationName), QStringLiteral("Acme Corp"));
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::OrganizationalUnitName), QStringLiteral("Finance"));
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::CountryName), QStringLiteral("DE"));
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::Email), QStringLiteral("jane.novak@example.com"));
    QCOMPARE(info->getKeySize(), 1024);
    QCOMPARE(info->getPublicKey(), pdf::PDFCertificateInfo::KeyRSA);
    QVERIFY(info->getKeyUsage().testFlag(pdf::PDFCertificateInfo::KeyUsageKeyEncipherment));
    QVERIFY(pdf::PDFCertificateManager::isUsableForEncryption(info->getCertificateData()));
    QCOMPARE(info->getNotValidAfter().toLocalTime().date(), validUntil);

    // Q5.11: the cache has the public certificate of the file (the DER, never the key or password)
    pdf::PDFCertificateCacheRepository repository(environment.context->getDatabase());
    std::optional<pdf::PDFCachedCertificate> cached = repository.find(pdf::PDFRepositoryHelper::normalizePath(filePath));
    QVERIFY(cached);
    QCOMPARE(cached->data, info->getCertificateData());
    QCOMPARE(cached->fingerprint, CertificateTestEnvironment::fingerprint(info->getCertificateData()));
    QVERIFY(!cached->data.contains(password.toUtf8()));
}

void AppCertificateDialogsTest::createDialogClearsPasswords()
{
    // S5.1: the passwords leave the fields, when the dialog is canceled (no undo brings them back)
    CertificateTestEnvironment environment;
    QWidget parent;
    QPointer<pdf::PDFAppCertificateCreateDialog> dialog = new pdf::PDFAppCertificateCreateDialog(environment.context.get(), &parent);
    dialog->show();
    QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("canary-secret"));
    QTest::keyClicks(dialog->getRepeatField(), QStringLiteral("canary-secret"));
    QPointer<pdf::PDFLineEdit> passwordField = dialog->getPasswordField();

    // The eye of the field shows the password (echo Normal): QLineEdit offers the undo of a shown
    // value, so a clear, which keeps the undo history, would bring the password back (review RS5APP)
    passwordField->setEchoMode(QLineEdit::Normal);
    dialog->getRepeatField()->setEchoMode(QLineEdit::Normal);
    dialog->reject();
    QVERIFY(passwordField);
    QVERIFY(passwordField->text().isEmpty());
    QVERIFY(!passwordField->isUndoAvailable());
    passwordField->undo();
    QVERIFY(passwordField->text().isEmpty());
    QVERIFY(dialog->getRepeatField()->text().isEmpty());
    QVERIFY(!dialog->getRepeatField()->isUndoAvailable());
    QTRY_VERIFY(!dialog);
}

void AppCertificateDialogsTest::importCopiesFile()
{
    CertificateTestEnvironment environment;
    const QString source = CertificateTestEnvironment::create(environment.sourceFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"));
    QVERIFY(!source.isEmpty());

    QWidget parent;
    QPointer<pdf::PDFAppCertificateImportDialog> dialog = new pdf::PDFAppCertificateImportDialog(environment.context.get(), source, &parent);
    QSignalSpy imported(dialog.data(), &pdf::PDFAppCertificateImportDialog::certificateImported);
    dialog->start();

    // Copied at once: the dialog is never shown and goes
    const QString target = QDir(CertificateTestEnvironment::applicationFolder()).absoluteFilePath(QStringLiteral("jane_novak.pfx"));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(QFileInfo(imported.front().front().toString()).absoluteFilePath(), QFileInfo(target).absoluteFilePath());
    QCOMPARE(CertificateTestEnvironment::read(target), CertificateTestEnvironment::read(source));
    QTRY_VERIFY(!dialog);

    // A .p12 file gets the extension .pfx (the folder lists .pfx files only)
    const QString p12 = QDir(environment.sourceFolder()).absoluteFilePath(QStringLiteral("company.p12"));
    QVERIFY(QFile::copy(source, p12));
    QString copy;
    QString error;
    QCOMPARE(pdf::PDFAppCertificateImportDialog::importFile(environment.context.get(), p12, CertificateTestEnvironment::applicationFolder(), &copy, &error),
             pdf::PDFAppCertificateImportDialog::Outcome::Imported);
    QCOMPARE(QFileInfo(copy).fileName(), QStringLiteral("company.pfx"));

    // The same file again counts as imported, nothing is duplicated
    QCOMPARE(pdf::PDFAppCertificateImportDialog::importFile(environment.context.get(), source, CertificateTestEnvironment::applicationFolder(), &copy, &error),
             pdf::PDFAppCertificateImportDialog::Outcome::Imported);
    QCOMPARE(QDir(CertificateTestEnvironment::applicationFolder()).entryList(QDir::Files).size(), 2);
}

void AppCertificateDialogsTest::importCollision()
{
    CertificateTestEnvironment environment;
    const QString existing = CertificateTestEnvironment::create(CertificateTestEnvironment::applicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("a"));
    const QString source = CertificateTestEnvironment::create(environment.sourceFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Other Jane"), QStringLiteral("b"));
    const QString another = CertificateTestEnvironment::create(environment.sourceFolder(), QStringLiteral("jane_novak_2.pfx"), QStringLiteral("Jane Novak 2"), QStringLiteral("c"));
    QVERIFY(!existing.isEmpty() && !source.isEmpty() && !another.isEmpty());
    const QByteArray existingData = CertificateTestEnvironment::read(existing);

    QWidget parent;
    QPointer<pdf::PDFAppCertificateImportDialog> dialog = new pdf::PDFAppCertificateImportDialog(environment.context.get(), source, &parent);
    QSignalSpy imported(dialog.data(), &pdf::PDFAppCertificateImportDialog::certificateImported);
    dialog->start();

    // 436:11538: the collision is shown, the existing file is never replaced
    QCOMPARE(dialog->getOutcome(), pdf::PDFAppCertificateImportDialog::Outcome::Collision);
    QVERIFY(dialog->isVisible());
    QVERIFY(CertificateTestEnvironment::hasLabel(dialog, QStringLiteral("A certificate file with this name already exists")));
    QVERIFY(CertificateTestEnvironment::hasLabel(dialog, QStringLiteral("jane_novak.pfx is already in your certificate folder. Rename the file you want to import, or choose another one.")));
    QCOMPARE(dialog->getChooseButton()->text(), QStringLiteral("Choose Another File…"));
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    QCOMPARE(CertificateTestEnvironment::read(existing), existingData);
    QCOMPARE(imported.count(), 0);

    // Choose Another File… imports the other file and the dialog goes
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&another](QWidget*, const QString&, const QString&, const QString&) { return another; });
    QTest::mouseClick(dialog->getChooseButton(), Qt::LeftButton);
    QCOMPARE(imported.count(), 1);
    QCOMPARE(QFileInfo(imported.front().front().toString()).fileName(), QStringLiteral("jane_novak_2.pfx"));
    QTRY_VERIFY(!dialog);
    QCOMPARE(CertificateTestEnvironment::read(existing), existingData);
}

void AppCertificateDialogsTest::importRefusesHostileFiles()
{
    // Plan 7.2: a file over 1 MiB or without PKCS#12 is an error; nothing is written
    CertificateTestEnvironment environment;
    const QString folder = CertificateTestEnvironment::applicationFolder();
    QString target;
    QString error;

    const QString large = QDir(environment.sourceFolder()).absoluteFilePath(QStringLiteral("large.pfx"));
    QVERIFY(CertificateTestEnvironment::write(large, QByteArray(int(pdf::PDFAppCertificateImportDialog::MAXIMUM_FILE_SIZE) + 1, '\x30')));
    QCOMPARE(pdf::PDFAppCertificateImportDialog::importFile(environment.context.get(), large, folder, &target, &error), pdf::PDFAppCertificateImportDialog::Outcome::Refused);
    QCOMPARE(error, QStringLiteral("The file large.pfx is too large for a certificate file."));

    const QString text = QDir(environment.sourceFolder()).absoluteFilePath(QStringLiteral("text.pfx"));
    QVERIFY(CertificateTestEnvironment::write(text, QByteArray("This is no PKCS#12 data.")));
    QCOMPARE(pdf::PDFAppCertificateImportDialog::importFile(environment.context.get(), text, folder, &target, &error), pdf::PDFAppCertificateImportDialog::Outcome::Refused);
    QCOMPARE(error, QStringLiteral("The file text.pfx is not a certificate file with a private key (.pfx)."));

    const QString missing = QDir(environment.sourceFolder()).absoluteFilePath(QStringLiteral("missing.pfx"));
    QCOMPARE(pdf::PDFAppCertificateImportDialog::importFile(environment.context.get(), missing, folder, &target, &error), pdf::PDFAppCertificateImportDialog::Outcome::Refused);
    QCOMPARE(error, QStringLiteral("The file missing.pfx does not exist."));

    QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());

    // The dialog shows the refusal with Choose Another File…
    QWidget parent;
    QPointer<pdf::PDFAppCertificateImportDialog> dialog = new pdf::PDFAppCertificateImportDialog(environment.context.get(), text, &parent);
    dialog->start();
    QCOMPARE(dialog->getOutcome(), pdf::PDFAppCertificateImportDialog::Outcome::Refused);
    QVERIFY(dialog->isVisible());
    QVERIFY(CertificateTestEnvironment::hasLabel(dialog, QStringLiteral("The certificate file cannot be imported")));
    dialog->reject();
    QTRY_VERIFY(!dialog);
}

void AppCertificateDialogsTest::providerStatesOfFiles()
{
    using State = pdf::PDFAppToolCertificateItem::State;

    CertificateTestEnvironment environment;
    const QString folder = CertificateTestEnvironment::applicationFolder();
    const QString open = CertificateTestEnvironment::create(folder, QStringLiteral("open.pfx"), QStringLiteral("Open Holder"), QString());
    const QString locked = CertificateTestEnvironment::create(folder, QStringLiteral("locked.pfx"), QStringLiteral("Jane Novak"), QStringLiteral("pfx-password"));
    QVERIFY(!open.isEmpty() && !locked.isEmpty());

    pdf::PDFAppToolCertificateProvider provider(environment.context.get());
    std::vector<pdf::PDFAppToolCertificateItem> items = provider.getCertificates();
    QCOMPARE(items.size(), size_t(2));

    // A file without a password: its details at once (Valid, not "password protected")
    const pdf::PDFAppToolCertificateItem* openItem = nullptr;
    const pdf::PDFAppToolCertificateItem* lockedItem = nullptr;
    for (const pdf::PDFAppToolCertificateItem& item : items)
    {
        (QFileInfo(item.filePath).fileName() == QStringLiteral("open.pfx") ? openItem : lockedItem) = &item;
    }
    QVERIFY(openItem && lockedItem);
    QCOMPARE(openItem->state, State::Valid);
    QVERIFY(!openItem->isPasswordProtected);
    QCOMPARE(openItem->name, QStringLiteral("Open Holder"));
    QCOMPARE(openItem->keyBits, 1024);
    QCOMPARE(openItem->fingerprint, CertificateTestEnvironment::fingerprint(pdf::PDFCertificateManager::readCertificate(CertificateTestEnvironment::read(open), QString())->getCertificateData()));
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(*openItem),
             QStringLiteral("Acme Corp · jane.novak@example.com · valid until %1").arg(pdf::PDFAppLocale::formatDate(openItem->validUntil.toLocalTime().date())));

    // Q5.11: a file with a password never unlocked shows its file name and the hint
    QCOMPARE(lockedItem->state, State::Locked);
    QVERIFY(lockedItem->isChoosable());
    QVERIFY(lockedItem->fingerprint.isEmpty());
    QCOMPARE(lockedItem->getTitle(), QStringLiteral("locked.pfx"));
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(*lockedItem), QStringLiteral("Details are shown after the password"));

    // 433:3972: a wrong password - one text, no entry
    QString error;
    QVERIFY(!provider.unlock(locked, QStringLiteral("wrong"), &error));
    QCOMPARE(error, QStringLiteral("Invalid certificate or password."));

    // The right password: the entry with its PKCS#12 and certificate; the cache keeps the public
    // certificate, the row has the details afterwards ("password protected")
    std::optional<pdf::PDFCertificateEntry> entry = provider.unlock(locked, QStringLiteral("pfx-password"), &error);
    QVERIFY(entry);
    QCOMPARE(entry->pkcs12, CertificateTestEnvironment::read(locked));
    QCOMPARE(entry->info.getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));
    items = provider.getCertificates();
    for (const pdf::PDFAppToolCertificateItem& item : items)
    {
        if (QFileInfo(item.filePath).fileName() == QStringLiteral("locked.pfx"))
        {
            QCOMPARE(item.state, State::Valid);
            QCOMPARE(item.getTitle(), QStringLiteral("Jane Novak"));
            QCOMPARE(item.fingerprint, CertificateTestEnvironment::fingerprint(entry->info.getCertificateData()));
            QVERIFY(pdf::PDFAppToolCertificateProvider::getDescription(item).endsWith(QStringLiteral(" · password protected")));
        }
    }

    // A file replaced by another one (another size) is Locked again: the cache never shows the
    // details of another certificate
    const QString other = CertificateTestEnvironment::create(environment.sourceFolder(), QStringLiteral("other.pfx"), QStringLiteral("Somebody Else With A Longer Name"), QStringLiteral("other-password"));
    QVERIFY(CertificateTestEnvironment::write(locked, CertificateTestEnvironment::read(other)));
    QVERIFY(!provider.findCachedCertificate(locked));
    for (const pdf::PDFAppToolCertificateItem& item : provider.getCertificates())
    {
        if (QFileInfo(item.filePath).fileName() == QStringLiteral("locked.pfx"))
        {
            QCOMPARE(item.state, State::Locked);
        }
    }
}

void AppCertificateDialogsTest::providerStatesOfCertificates()
{
    using State = pdf::PDFAppToolCertificateItem::State;

    CertificateTestEnvironment environment;
    const QString path = CertificateTestEnvironment::create(environment.sourceFolder(), QStringLiteral("jane.pfx"), QStringLiteral("Jane Novak"), QString());
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = CertificateTestEnvironment::read(path);
    entry.pkcs12FilePath = path;
    std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(entry.pkcs12, QString());
    QVERIFY(info);
    entry.info = *info;

    // 433:3220: after its validity the certificate is Expired, it cannot be chosen
    const QDateTime after = info->getNotValidAfter().addDays(1);
    pdf::PDFAppLocale::setCurrentTimeOverride(after.toMSecsSinceEpoch());
    pdf::PDFAppToolCertificateItem expired = pdf::PDFAppToolCertificateProvider::createItem(entry, info, after);
    QCOMPARE(expired.state, State::Expired);
    QVERIFY(!expired.isChoosable());
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(expired),
             QStringLiteral("Expired on %1 – cannot be used").arg(pdf::PDFAppLocale::formatDate(info->getNotValidAfter().toLocalTime().date())));

    // Before its validity it cannot be used either
    const QDateTime before = info->getNotValidBefore().addDays(-1);
    pdf::PDFAppLocale::setCurrentTimeOverride(before.toMSecsSinceEpoch());
    pdf::PDFAppToolCertificateItem early = pdf::PDFAppToolCertificateProvider::createItem(entry, info, before);
    QCOMPARE(early.state, State::Expired);
    QVERIFY(pdf::PDFAppToolCertificateProvider::getDescription(early).startsWith(QStringLiteral("Valid from ")));
    pdf::PDFAppLocale::setCurrentTimeOverride(0);

    // Q5.4: a certificate of the system store (no PKCS#12) is Disabled with its reason
    pdf::PDFCertificateEntry system;
    system.info = *info;
    pdf::PDFAppToolCertificateItem systemItem = pdf::PDFAppToolCertificateProvider::createItem(system, info, QDateTime::currentDateTimeUtc());
    QCOMPARE(systemItem.source, pdf::PDFAppToolCertificateItem::Source::SystemStore);
    QCOMPARE(systemItem.state, State::SystemStore);
    QVERIFY(!systemItem.isChoosable());
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(systemItem), QStringLiteral("System certificates will be supported with Manage Certificates"));

    // NoEncryptionUsage has its reason (B99: an old certificate without keyEncipherment)
    pdf::PDFAppToolCertificateItem unusable = expired;
    unusable.state = State::NoEncryptionUsage;
    QCOMPARE(pdf::PDFAppToolCertificateProvider::getDescription(unusable), QStringLiteral("Not meant for encryption – cannot be used"));
    QVERIFY(!unusable.isChoosable());
}

void AppCertificateDialogsTest::providerOldFolder()
{
    // Q5.5: the .pfx files of the old Editor are read (read only) and their copy is offered; a
    // copied file is listed once (the copy)
    CertificateTestEnvironment environment;
    const QString old = CertificateTestEnvironment::create(CertificateTestEnvironment::oldFolder(), QStringLiteral("editor.pfx"), QStringLiteral("Editor Holder"), QString());
    QVERIFY(!old.isEmpty());

    pdf::PDFAppToolCertificateProvider provider(environment.context.get());
    std::vector<pdf::PDFAppToolCertificateItem> items = provider.getCertificates();
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(items.front().isInOldFolder);
    QVERIFY(pdf::PDFAppToolCertificateProvider::getDescription(items.front()).endsWith(QStringLiteral(" · from an older PDF4QT application")));

    QString error;
    const QString copy = provider.copyToApplicationFolder(old, &error);
    QVERIFY2(!copy.isEmpty(), qPrintable(error));
    QCOMPARE(QFileInfo(copy).absolutePath(), QFileInfo(CertificateTestEnvironment::applicationFolder()).absoluteFilePath());
    QCOMPARE(CertificateTestEnvironment::read(copy), CertificateTestEnvironment::read(old));
    QVERIFY(QFile::exists(old));

    items = provider.getCertificates();
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(!items.front().isInOldFolder);
    QCOMPARE(QFileInfo(items.front().filePath).absoluteFilePath(), QFileInfo(copy).absoluteFilePath());
}

void AppCertificateDialogsTest::listChoosesRows()
{
    using Item = pdf::PDFAppToolCertificateItem;

    Item valid;
    valid.state = Item::State::Valid;
    valid.filePath = QStringLiteral("C:/certificates/jane_novak.pfx");
    valid.fingerprint = QString(64, QLatin1Char('a'));
    valid.name = QStringLiteral("Jane Novak");
    Item locked;
    locked.state = Item::State::Locked;
    locked.filePath = QStringLiteral("C:/certificates/locked.pfx");
    Item expired;
    expired.state = Item::State::Expired;
    expired.filePath = QStringLiteral("C:/certificates/old.pfx");
    expired.name = QStringLiteral("Jane Novak (2020)");
    expired.validUntil = QDateTime(QDate(2025, 1, 15), QTime(12, 0), QTimeZone::UTC);
    Item system;
    system.source = Item::Source::SystemStore;
    system.state = Item::State::SystemStore;
    system.name = QStringLiteral("Jane Novak – Company ID");

    QWidget window;
    pdf::PDFAppToolCertificateList* list = new pdf::PDFAppToolCertificateList(&window);
    list->resize(640, 300);
    window.show();
    list->setCertificates({ valid, locked, expired, system });
    QSignalSpy chosen(list, &pdf::PDFAppToolCertificateList::certificateChosen);

    // 433:3192 ...: the titles, the tags, the icons; the expired reason in accent/red/icon
    QCOMPARE(list->getRow(0)->getTitle(), QStringLiteral("Jane Novak"));
    QCOMPARE(list->getRow(0)->getTag(), QStringLiteral(".pfx file"));
    QCOMPARE(list->getRow(0)->getIconName(), QStringLiteral("pfx-file"));
    QCOMPARE(list->getRow(1)->getTitle(), QStringLiteral("locked.pfx"));
    QCOMPARE(list->getRow(3)->getTag(), QStringLiteral("System store"));
    QCOMPARE(list->getRow(3)->getIconName(), QStringLiteral("monitor"));
    QCOMPARE(list->getRow(2)->getDescriptionColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::AccentRedIcon));
    QVERIFY(!list->getRow(2)->isClickable());
    QVERIFY(!list->getRow(3)->isClickable());

    // A click on a Disabled row chooses nothing
    QTest::mouseClick(list->getRow(2), Qt::LeftButton);
    QCOMPARE(chosen.count(), 0);
    QVERIFY(list->getChosenFilePath().isEmpty());

    // A click on a row chooses it: ✓, bg/accent-subtle (checked), the tile Accent; one signal
    QTest::mouseClick(list->getRow(0), Qt::LeftButton);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.front().front().toString(), valid.fingerprint);
    QCOMPARE(list->getChosenFilePath(), valid.filePath);
    QCOMPARE(list->getRow(0)->getEndIconName(), QStringLiteral("check"));
    QVERIFY(list->getRow(0)->isChecked());
    QCOMPARE(list->getRow(0)->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(list->getRow(1)->getTone(), pdf::PDFTileRow::Tone::Muted);
    QVERIFY(list->getRow(1)->getEndIconName().isEmpty());

    // The chosen row again: no signal; choose by code emits nothing
    QTest::mouseClick(list->getRow(0), Qt::LeftButton);
    QCOMPARE(chosen.count(), 1);
    list->choose(locked.filePath);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(list->getChosenFilePath(), locked.filePath);
    QVERIFY(list->getChosenFingerprint().isEmpty());
    QVERIFY(!list->getRow(0)->isChecked());
    list->choose(expired.filePath);
    QCOMPARE(list->getChosenFilePath(), locked.filePath);

    // New rows keep the choice; a choice, which is gone, is dropped
    list->setCertificates({ valid, locked });
    QCOMPARE(list->getChosenFilePath(), locked.filePath);
    list->setCertificates({ valid });
    QVERIFY(list->getChosenFilePath().isEmpty());
    QVERIFY(!list->getChosenItem());
}

QTEST_MAIN(AppCertificateDialogsTest)

#include "tst_appcertificatedialogstest.moc"
