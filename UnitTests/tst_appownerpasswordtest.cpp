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

// The owner password, when an action needs a permission (package O of step 2 of phase 2,
// owner 2026-11-08): the shared check of the permissions (ISO 32000-2, 7.6.4.2, Table 22,
// the table of revision 2 - SECURITY_TODO 31), the unlock by the owner password of the
// standard security handler (7.6.4.4, Algorithm 7 for revisions 2 - 4, Algorithm 2.A for
// revision 6) for the session of the tab only, the dialog in its document mode (1390:34569,
// 398:27726 - no frame of its own, Q17), the routed actions (print, copy, annotations, the
// form, a tool, the rotation of the Thumbnails, MCP), the public-key handler (7.6.5: no
// owner password), saving (the encryption dictionary and /P unchanged) and the lifetime of
// the dialog. Samples are restricted per revision: R2 and R3 (RC4, made here by the
// algorithms of 7.6.4.3 - 7.6.4.4), R4 (RC4 and AES-128) and R6 (AES-256), each with an
// empty user password and an owner password.

#include "appmcpfixture.h"
#include "apptoolshellfixture.h"

#include "pdfappannotatecommands.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreator.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentpanelcommands.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentpropertiespage.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappformcontroller.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppassworddialog.h"
#include "pdfappprint.h"
#include "pdfapproute.h"
#include "pdfappworkspace.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolinputloader.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <array>

using pdf::PDFAppDocumentPermissions;
using pdf::PDFAppPermissionAction;
using pdf::PDFAppPermissionRequest;
using pdftest::AppDocumentFixture;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;
using Permission = pdf::PDFSecurityHandler::Permission;

/// Samples and helpers of the tests
class AppOwnerPasswordHelper
{
public:
    AppOwnerPasswordHelper() = delete;

    /// Owner password of every sample (the user password is empty)
    static QString owner() { return QStringLiteral("owner-secret"); }

    /// The bits of the samples (ISO 32000-2, Table 22): printing (3), copying (5), annotations
    /// (6), filling of forms (9) and the assembly (11) are not allowed; changes (4), the
    /// extraction for accessibility (10) and the print of high quality (12) are. Bits 1, 2
    /// are 0, bits 13 - 32 are 1 (Table 22).
    static uint32_t restrictedBits()
    {
        return 0xFFFFFFFCu & ~uint32_t(0x0004u | 0x0010u | 0x0020u | 0x0100u | 0x0400u);
    }

    /// Padding string of 7.6.4.3, Algorithm 2 step a
    static QByteArray padding()
    {
        static const unsigned char bytes[32] = { 0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                                 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A };
        return QByteArray(reinterpret_cast<const char*>(bytes), 32);
    }

    static QByteArray pad(const QByteArray& password)
    {
        return (password.left(32) + padding()).left(32);
    }

    static QByteArray md5(const QByteArray& data)
    {
        return QCryptographicHash::hash(data, QCryptographicHash::Md5);
    }

    /// RC4 (the cipher of 7.6.3.2 for V 1 and 2)
    static QByteArray rc4(const QByteArray& key, const QByteArray& data)
    {
        std::array<unsigned char, 256> state;
        for (int i = 0; i < 256; ++i)
        {
            state[i] = static_cast<unsigned char>(i);
        }
        int j = 0;
        for (int i = 0; i < 256; ++i)
        {
            j = (j + state[i] + static_cast<unsigned char>(key[i % key.size()])) & 0xFF;
            std::swap(state[i], state[j]);
        }
        QByteArray result = data;
        int x = 0;
        int y = 0;
        for (int k = 0; k < result.size(); ++k)
        {
            x = (x + 1) & 0xFF;
            y = (y + state[x]) & 0xFF;
            std::swap(state[x], state[y]);
            result[k] = char(static_cast<unsigned char>(result[k]) ^ state[(state[x] + state[y]) & 0xFF]);
        }
        return result;
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    /// The handler of revision 2 made by the algorithms of the specification (7.6.4.3 Algorithm
    /// 2, 7.6.4.4 Algorithms 3 and 4; key of 40 bits, an empty /ID): the factory of the core
    /// does not make revision 2
    static pdf::PDFSecurityHandlerPointer createRevision2(uint32_t permissions)
    {
        // Algorithm 3: O = RC4(MD5(owner padded)[0..4], user padded)
        const QByteArray ownerKey = md5(pad(owner().toLatin1())).left(5);
        const QByteArray O = rc4(ownerKey, pad(QByteArray()));

        // Algorithm 2: key = MD5(user padded, O, P little endian, ID[0])[0..4]
        QByteArray input = pad(QByteArray()) + O;
        for (int i = 0; i < 4; ++i)
        {
            input.append(char((permissions >> (8 * i)) & 0xFF));
        }
        const QByteArray key = md5(input).left(5);

        // Algorithm 4: U = RC4(key, padding)
        const QByteArray U = rc4(key, padding());
        const pdf::PDFObject encryption = dictionary({ { "Filter", pdf::PDFObject::createName(QByteArray("Standard")) }, { "V", pdf::PDFObject::createInteger(1) },
                                                       { "R", pdf::PDFObject::createInteger(2) }, { "Length", pdf::PDFObject::createInteger(40) },
                                                       { "O", pdf::PDFObject::createString(O) }, { "U", pdf::PDFObject::createString(U) },
                                                       { "P", pdf::PDFObject::createInteger(pdf::PDFInteger(int32_t(permissions))) } });
        return authenticateUser(pdf::PDFSecurityHandler::createSecurityHandler(encryption, QByteArray()));
    }

    /// The handler of revision 3: O, U and the key of revisions 3 and 4 are the same algorithms
    /// (7.6.4.3 Algorithm 2, 7.6.4.4 Algorithms 3 and 5) - the dictionary of the RC4 handler of
    /// revision 4 of the core with V 2, R 3 and a key of 128 bits (no crypt filters)
    static pdf::PDFSecurityHandlerPointer createRevision3(uint32_t permissions)
    {
        pdf::PDFSecurityHandlerPointer handler = createFactory(pdf::PDFSecurityHandlerFactory::RC4, permissions);
        const pdf::PDFObject source = handler->createEncryptionDictionaryObject();
        const pdf::PDFDictionary* sourceDictionary = source.getDictionary();
        const pdf::PDFObject encryption = dictionary({ { "Filter", pdf::PDFObject::createName(QByteArray("Standard")) }, { "V", pdf::PDFObject::createInteger(2) },
                                                       { "R", pdf::PDFObject::createInteger(3) }, { "Length", pdf::PDFObject::createInteger(128) },
                                                       { "O", sourceDictionary->get("O") }, { "U", sourceDictionary->get("U") }, { "P", sourceDictionary->get("P") } });
        return authenticateUser(pdf::PDFSecurityHandler::createSecurityHandler(encryption, QByteArray()));
    }

    static pdf::PDFSecurityHandlerPointer createFactory(pdf::PDFSecurityHandlerFactory::Algorithm algorithm, uint32_t permissions)
    {
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = algorithm;
        settings.userPassword = QString();
        settings.ownerPassword = owner();
        settings.permissions = permissions;
        return pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);
    }

    /// The handler authorized by the empty user password (a writer needs an authorized one)
    static pdf::PDFSecurityHandlerPointer authenticateUser(pdf::PDFSecurityHandlerPointer handler)
    {
        bool isFirst = true;
        handler->authenticate([&isFirst](bool* ok) { *ok = isFirst; isFirst = false; return QString(); }, false);
        return handler;
    }

    /// Handler of the revision \p revision ("2", "3", "4-rc4", "4-aes", "6")
    static pdf::PDFSecurityHandlerPointer createHandler(const QString& revision, uint32_t permissions)
    {
        if (revision == QLatin1String("2"))
        {
            return createRevision2(permissions);
        }
        if (revision == QLatin1String("3"))
        {
            return createRevision3(permissions);
        }
        if (revision == QLatin1String("4-rc4"))
        {
            return createFactory(pdf::PDFSecurityHandlerFactory::RC4, permissions);
        }
        if (revision == QLatin1String("4-aes"))
        {
            return createFactory(pdf::PDFSecurityHandlerFactory::AES_128, permissions);
        }
        return createFactory(pdf::PDFSecurityHandlerFactory::AES_256, permissions);
    }

    /// Writes the file \p source encrypted by the handler as \p name. \returns its path
    static QString encryptFile(const QString& source, const QString& folder, const QString& name, pdf::PDFSecurityHandlerPointer handler)
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        const pdf::PDFDocument document = reader.readFromFile(source);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK || !handler)
        {
            return QString();
        }
        pdf::PDFDocumentBuilder builder(&document);
        builder.setSecurityHandler(handler);
        pdf::PDFDocument encrypted = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return AppDocumentSamples::write(encrypted, filePath) ? filePath : QString();
    }

    /// 2 pages with text, restricted by the bits of restrictedBits, of the revision
    static QString sample(const QString& folder, const QString& revision, const QString& name = QString())
    {
        const QString source = AppDocumentSamples::simple(folder, QStringLiteral("source-%1.pdf").arg(revision), 2);
        return encryptFile(source, folder, name.isEmpty() ? QStringLiteral("restricted-r%1.pdf").arg(revision) : name, createHandler(revision, restrictedBits()));
    }

    /// The form of every field kind (fullForm), restricted by restrictedBits (AES-256)
    static QString formSample(const QString& folder)
    {
        return encryptFile(AppDocumentSamples::fullForm(folder), folder, QStringLiteral("restricted-form.pdf"), createHandler(QStringLiteral("6"), restrictedBits()));
    }

    /// Reads the file by the password; \p isOwnerOnly tries the password as the owner password
    /// only (else the empty user password of a sample opens it first, 7.6.4.4)
    static pdf::PDFDocumentPointer read(const QString& filePath, const QString& password = QString(), bool isOwnerOnly = false)
    {
        bool isFirst = true;
        pdf::PDFDocumentReader reader(nullptr, [&isFirst, password](bool* ok) { *ok = isFirst; isFirst = false; return password; }, false, isOwnerOnly);
        pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))) : pdf::PDFDocumentPointer();
    }

    /// Opens a sample and waits until its pages are shown
    static pdf::PDFAppDocumentTab* openReady(AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        const bool isReady = guard && QTest::qWaitFor([&guard]()
        {
            return guard && guard->getDocument() && guard->getDocument()->isReady() && guard->getView() && guard->getView()->getPageCount() > 0;
        }, 20000 * AppDocumentFixture::getTimeoutFactor());
        if (guard)
        {
            fixture.window->activateTab(guard.data());
        }
        AppDocumentFixture::settle();
        return isReady ? guard.data() : nullptr;
    }

    /// The open owner password dialog of the document (nullptr = none)
    static pdf::PDFAppPasswordDialog* findOwnerDialog(const pdf::PDFAppDocument* document)
    {
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            for (pdf::PDFAppPasswordDialog* dialog : topLevel->findChildren<pdf::PDFAppPasswordDialog*>())
            {
                if (dialog->isVisible() && dialog->isDocumentMode() && dialog->getDocument() == document)
                {
                    return dialog;
                }
            }
        }
        return nullptr;
    }

    /// Types the password into the dialog and clicks Unlock
    static void unlock(pdf::PDFAppPasswordDialog* dialog, const QString& password)
    {
        QTest::keyClicks(dialog->getPasswordField(), password);
        QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    }

    /// Decoded content of the first page (decrypted by the handler of the document)
    static QByteArray firstPageContent(const pdf::PDFDocument* document)
    {
        const pdf::PDFPage* page = document->getCatalog()->getPage(0);
        const pdf::PDFObject& contents = document->getObject(page->getContents());
        if (contents.isStream())
        {
            return document->getDecodedStream(contents.getStream());
        }
        if (contents.isArray() && contents.getArray()->getCount() > 0)
        {
            const pdf::PDFObject& first = document->getObject(contents.getArray()->getItem(0));
            return first.isStream() ? document->getDecodedStream(first.getStream()) : QByteArray();
        }
        return QByteArray();
    }

    /// /P of the encryption dictionary, which the handler writes
    static pdf::PDFInteger getP(const pdf::PDFSecurityHandler* handler)
    {
        const pdf::PDFObject object = handler->createEncryptionDictionaryObject();
        return object.isDictionary() ? object.getDictionary()->get("P").getInteger() : 0;
    }

    static QJsonObject json(const char* text)
    {
        return QJsonDocument::fromJson(QByteArray(text)).object();
    }

    static QString text(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? (result->kind == pdf::PDFMcpCallResult::Kind::Error ? result->errorMessage : result->getFirstText()) : QString();
    }

    static bool isError(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return !result || result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError;
    }
};

using H = AppOwnerPasswordHelper;

class AppOwnerPasswordTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void ruleOfRevision2();
    void ruleOfRevision2InCore();
    void ownerPasswordPerRevision_data();
    void ownerPasswordPerRevision();
    void ownerPasswordEqualsUserPassword();
    void certificateSecurity();
    void dialogTexts_data();
    void dialogTexts();
    void printAsksOwnerPassword_data();
    void printAsksOwnerPassword();
    void unlocksOnlyThisDocument();
    void undoAndSaveKeepEncryption();
    void copyAsksOwnerPassword();
    void rotateAsksOwnerPassword();
    void toolContinuesAfterUnlock();
    void annotationsAndForm();
    void propertiesUnlock();
    void mergeInputOfUnlockedTab();
    void mcpRefusalAndUnlock();
    void mergeRefusalTexts();
    void sanitizeAsksOwnerPassword();
    void sanitizeOwnerDialogLifetime();
    void dialogLifetime();
    void themesAndRightToLeft();
};

void AppOwnerPasswordTest::initTestCase()
{
    // The style of the application (main of PDF4QT): the fields are drawn by it, never by the palette
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppOwnerPasswordTest::ruleOfRevision2()
{
    // SECURITY_TODO 31: with /R 2 bits 9 - 12 do not exist (Table 22); assembling falls under
    // bit 4, filling of forms under bit 6, the extraction for accessibility under bit 5, the
    // print of high quality under bit 3. The sample clears bits 3, 4 and 6 and sets 9 - 12.
    AppDocumentFixture fixture;
    const uint32_t bits = 0xFFFFFFFCu & ~uint32_t(0x0004u | 0x0008u | 0x0020u);
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("r2-source.pdf"), 1);
    const QString filePath = H::encryptFile(source, fixture.folder(), QStringLiteral("r2.pdf"), H::createRevision2(bits));
    QVERIFY(!filePath.isEmpty());
    const pdf::PDFDocumentPointer document = H::read(filePath);
    QVERIFY(document);
    const pdf::PDFSecurityHandler* handler = document->getStorage().getSecurityHandler();
    QCOMPARE(handler->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized);
    QCOMPARE(PDFAppDocumentPermissions::getRevision(handler), 2);

    QVERIFY(!PDFAppDocumentPermissions::isAllowed(document.data(), Permission::Assemble));
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(document.data(), Permission::ModifyFormFields));
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(document.data(), Permission::PrintHighResolution));
    QVERIFY(PDFAppDocumentPermissions::isAllowed(document.data(), Permission::Accessibility));
    QVERIFY(PDFAppDocumentPermissions::isAllowed(document.data(), Permission::CopyContent));
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(document.data(), PDFAppPermissionRequest::createTool(QStringLiteral("pages.rotate"))));

    // The core still reads the bits literally (the old applications, phase 3)
    QVERIFY(handler->isAllowed(Permission::Assemble));
}

void AppOwnerPasswordTest::ruleOfRevision2InCore()
{
    // SECURITY_TODO 31, the one rule of the new code (PDFSecurityHandler::isAllowedByRevision):
    // the merge (its inputs in the application, MCP and PdfTool unite) and the check of a tool
    // before its run read a handler of revision 2 by the table of revision 2 as the application
    // asks by it. Table 22: with /R 2 the assembly is bit 4, filling of forms bit 6, the print
    // of high quality bit 3; bits 9 - 12 mean nothing.
    AppDocumentFixture fixture;
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("r2-core-source.pdf"), 1);

    // Bits 3 - 6 set, bits 9 - 12 cleared: everything is allowed by revision 2
    const uint32_t allowedBits = 0xFFFFFFFCu & ~uint32_t(0x0100u | 0x0200u | 0x0400u | 0x0800u);
    const pdf::PDFDocumentPointer allowed = H::read(H::encryptFile(source, fixture.folder(), QStringLiteral("r2-allowed.pdf"), H::createRevision2(allowedBits)));
    QVERIFY(allowed);
    const pdf::PDFSecurityHandler* allowedHandler = allowed->getStorage().getSecurityHandler();
    QVERIFY(!allowedHandler->isAllowed(Permission::Assemble));
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(allowedHandler, Permission::Assemble));
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(allowedHandler, Permission::ModifyFormFields));
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(allowedHandler, Permission::PrintHighResolution));
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(allowedHandler, Permission::Accessibility));
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(allowed.data()), pdf::PDFDocumentMerger::Permission::Permitted);
    QVERIFY(pdf::PDFAppToolInputLoader::isPermitted(allowed.data(), nullptr));
    const pdf::PDFDocumentToolCatalogEntry* rotate = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.rotate"));
    QVERIFY(rotate);
    QVERIFY(rotate->isPermitted(allowed.data()));
    QVERIFY(PDFAppDocumentPermissions::isAllowed(allowed.data(), PDFAppPermissionRequest::createTool(QStringLiteral("pages.rotate"))));

    // Bit 4 cleared, bits 9 - 12 set: no assembly (the merge says so first), no Rotate
    const uint32_t deniedBits = 0xFFFFFFFCu & ~uint32_t(0x0008u);
    const pdf::PDFDocumentPointer denied = H::read(H::encryptFile(source, fixture.folder(), QStringLiteral("r2-denied.pdf"), H::createRevision2(deniedBits)));
    QVERIFY(denied);
    const pdf::PDFSecurityHandler* deniedHandler = denied->getStorage().getSecurityHandler();
    QVERIFY(deniedHandler->isAllowed(Permission::Assemble));
    QVERIFY(!pdf::PDFSecurityHandler::isAllowedByRevision(deniedHandler, Permission::Assemble));
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(deniedHandler, Permission::ModifyFormFields));
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(denied.data()), pdf::PDFDocumentMerger::Permission::AssembleDenied);
    QVERIFY(!rotate->isPermitted(denied.data()));
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(denied.data(), PDFAppPermissionRequest::createTool(QStringLiteral("pages.rotate"))));

    // A handler of revision 3 reads its bits literally; no handler allows everything
    const pdf::PDFDocumentPointer revision3 = H::read(H::encryptFile(source, fixture.folder(), QStringLiteral("r3-core.pdf"), H::createRevision3(allowedBits)));
    QVERIFY(revision3);
    QVERIFY(!pdf::PDFSecurityHandler::isAllowedByRevision(revision3->getStorage().getSecurityHandler(), Permission::Assemble));
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(revision3.data()), pdf::PDFDocumentMerger::Permission::AssembleDenied);
    QVERIFY(pdf::PDFSecurityHandler::isAllowedByRevision(nullptr, Permission::Assemble));
}

void AppOwnerPasswordTest::ownerPasswordPerRevision_data()
{
    // The bits of restrictedBits: revisions 3 - 6 read bits 3, 5, 6, 9 and 11 as cleared;
    // revision 2 has no bits 9 - 12 (Table 22) - filling by bit 6, the extraction for
    // accessibility by bit 5, assembling by bit 4 (set) and the print of high quality by bit 3
    QTest::addColumn<QString>("revision");
    QTest::addColumn<bool>("isRevision2");
    QTest::newRow("R2 RC4-40") << QStringLiteral("2") << true;
    QTest::newRow("R3 RC4-128") << QStringLiteral("3") << false;
    QTest::newRow("R4 RC4") << QStringLiteral("4-rc4") << false;
    QTest::newRow("R4 AES-128") << QStringLiteral("4-aes") << false;
    QTest::newRow("R6 AES-256") << QStringLiteral("6") << false;
}

void AppOwnerPasswordTest::ownerPasswordPerRevision()
{
    // 7.6.4.4: the owner password authorizes the owner (Algorithm 7, revision 6 Algorithm
    // 2.A) without the file - the handler of the document holds O, U (OE, Perms); the
    // unlocked document decrypts the same content and writes the same dictionary
    QFETCH(QString, revision);
    QFETCH(bool, isRevision2);
    AppDocumentFixture fixture;
    const QString filePath = H::sample(fixture.folder(), revision);
    QVERIFY(!filePath.isEmpty());
    const pdf::PDFDocumentPointer document = H::read(filePath);
    QVERIFY(document);
    const pdf::PDFSecurityHandler* handler = document->getStorage().getSecurityHandler();
    QCOMPARE(handler->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized);
    QVERIFY(PDFAppDocumentPermissions::canUnlock(document.data()));
    QVERIFY(PDFAppDocumentPermissions::isRestricted(document.data()));
    const std::vector<Permission> forbidden = isRevision2 ? std::vector<Permission>({ Permission::PrintLowResolution, Permission::CopyContent, Permission::ModifyInteractiveItems, Permission::ModifyFormFields,
                                                                                       Permission::Accessibility, Permission::PrintHighResolution })
                                                          : std::vector<Permission>({ Permission::PrintLowResolution, Permission::CopyContent, Permission::ModifyInteractiveItems, Permission::ModifyFormFields,
                                                                                       Permission::Assemble });
    QCOMPARE(PDFAppDocumentPermissions::getForbiddenPermissions(document.data()), forbidden);

    // A wrong one, the empty user password: nothing
    QVERIFY(!PDFAppDocumentPermissions::authenticateOwner(handler, QStringLiteral("wrong")));
    QVERIFY(!PDFAppDocumentPermissions::authenticateOwner(handler, QString()));
    QVERIFY(!PDFAppDocumentPermissions::authenticateOwner(nullptr, H::owner()));

    pdf::PDFSecurityHandlerPointer ownerHandler = PDFAppDocumentPermissions::authenticateOwner(handler, H::owner());
    QVERIFY(ownerHandler);
    QCOMPARE(ownerHandler->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized);
    QCOMPARE(handler->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized);
    QVERIFY(PDFAppDocumentPermissions::hasSameEncryption(handler, ownerHandler.data()));

    const pdf::PDFDocumentPointer unlocked = PDFAppDocumentPermissions::createWithSecurityHandler(document.data(), ownerHandler);
    QVERIFY(unlocked);
    QVERIFY(!PDFAppDocumentPermissions::isRestricted(unlocked.data()));
    QVERIFY(!PDFAppDocumentPermissions::canUnlock(unlocked.data()));
    const QByteArray content = H::firstPageContent(document.data());
    QVERIFY(content.contains("BT"));
    QCOMPARE(H::firstPageContent(unlocked.data()), content);
    QCOMPARE(H::getP(unlocked->getStorage().getSecurityHandler()), H::getP(handler));
    QCOMPARE(uint32_t(H::getP(handler)) & 0xFFFu, H::restrictedBits() & 0xFFFu);
}

void AppOwnerPasswordTest::ownerPasswordEqualsUserPassword()
{
    // A document, whose owner password is its user password, opens by the password as the
    // owner (7.6.4.4: the owner password is tried first): nothing to unlock, no dialog
    AppDocumentFixture fixture;
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = QStringLiteral("same");
    settings.ownerPassword = QStringLiteral("same");
    settings.permissions = H::restrictedBits();
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("same-source.pdf"), 1);
    const QString filePath = H::encryptFile(source, fixture.folder(), QStringLiteral("same.pdf"), pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocumentPointer document = H::read(filePath, QStringLiteral("same"));
    QVERIFY(document);
    QCOMPARE(document->getStorage().getSecurityHandler()->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized);
    QVERIFY(!PDFAppDocumentPermissions::isRestricted(document.data()));
    QVERIFY(!PDFAppDocumentPermissions::canUnlock(document.data()));
    QVERIFY(PDFAppDocumentPermissions::isAllowed(document.data(), PDFAppPermissionRequest::create(PDFAppPermissionAction::Print)));
}

void AppOwnerPasswordTest::certificateSecurity()
{
    // 7.6.5: the public-key handler has no owner password - no dialog, the refusal names
    // the certificate (adbe.pkcs7.s3 reads /P as the standard bits)
    AppDocumentFixture fixture;
    const pdf::PDFObject encryption = H::dictionary({ { "Filter", pdf::PDFObject::createName(QByteArray("Adobe.PubSec")) }, { "SubFilter", pdf::PDFObject::createName(QByteArray("adbe.pkcs7.s3")) },
                                                      { "V", pdf::PDFObject::createInteger(1) }, { "P", pdf::PDFObject::createInteger(pdf::PDFInteger(int32_t(H::restrictedBits()))) } });
    pdf::PDFSecurityHandlerPointer handler = pdf::PDFSecurityHandler::createSecurityHandler(encryption, QByteArray());
    QCOMPARE(handler->getMode(), pdf::EncryptionMode::PublicKey);
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("certificate-source.pdf"), 1);
    const pdf::PDFDocumentPointer plain = H::read(source);
    QVERIFY(plain);
    const pdf::PDFDocumentPointer document = PDFAppDocumentPermissions::createWithSecurityHandler(plain.data(), handler);
    QVERIFY(document);

    const PDFAppPermissionRequest print = PDFAppPermissionRequest::create(PDFAppPermissionAction::Print);
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(document.data(), print));
    QVERIFY(PDFAppDocumentPermissions::isCertificateSecurity(document.data()));
    QVERIFY(!PDFAppDocumentPermissions::canUnlock(document.data()));
    QVERIFY(!PDFAppDocumentPermissions::isAvailable(document.data(), print));
    QVERIFY(!PDFAppDocumentPermissions::authenticateOwner(handler.data(), H::owner()));
    QCOMPARE(PDFAppDocumentPermissions::getRefusalText(document.data(), print),
             QStringLiteral("The document does not allow printing. Its permissions come from the certificate, with which it was opened; no password can change them."));
    QVERIFY(PDFAppDocumentPermissions::getMcpRefusalText(document.data(), print).contains(QStringLiteral("public-key security")));
    QVERIFY(!PDFAppDocumentPermissions::getMcpRefusalText(document.data(), print).contains(QStringLiteral("owner password")));

    // The shared question opens the refusal, never the owner password dialog
    pdf::PDFAppDocument appDocument(fixture.window->getContext(), nullptr);
    appDocument.setLoaded(document, QByteArray(), source, true);
    QVERIFY(appDocument.isReady());
    QDialog* dialog = nullptr;
    QVERIFY(!PDFAppDocumentPermissions::request(&appDocument, print, fixture.window, &dialog));
    QVERIFY(dialog);
    QCOMPARE(dialog->objectName(), QStringLiteral("permissionRefusalDialog"));
    QVERIFY(!qobject_cast<pdf::PDFAppPasswordDialog*>(dialog));
    QVERIFY(!pdf::PDFAppPasswordDialog::openOwnerDialog(&appDocument, print, fixture.window));
    QVERIFY(!appDocument.unlockWithOwnerPassword(H::owner()));
    dialog->reject();
    AppDocumentFixture::settle();
}

void AppOwnerPasswordTest::dialogTexts_data()
{
    QTest::addColumn<int>("action");
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QString>("expected");
    const QString tail = QStringLiteral(" If you own the document, enter its owner password to allow it until the document is closed.");
    QTest::newRow("print") << int(PDFAppPermissionAction::Print) << QString() << QStringLiteral("The author of this document does not allow printing.") + tail;
    QTest::newRow("copy") << int(PDFAppPermissionAction::CopyContent) << QString() << QStringLiteral("The author of this document does not allow copying text and images.") + tail;
    QTest::newRow("annotate") << int(PDFAppPermissionAction::Annotate) << QString() << QStringLiteral("The author of this document does not allow adding and changing annotations.") + tail;
    QTest::newRow("fill") << int(PDFAppPermissionAction::FillForm) << QString() << QStringLiteral("The author of this document does not allow filling in forms.") + tail;
    QTest::newRow("assemble") << int(PDFAppPermissionAction::AssemblePages) << QString() << QStringLiteral("The author of this document does not allow inserting, rotating and removing pages.") + tail;
    QTest::newRow("tool") << int(PDFAppPermissionAction::Tool) << QStringLiteral("pages.rotate")
                          << QStringLiteral("The author of this document does not allow inserting, rotating and removing pages, which Rotate Pages needs.") + tail;
    QTest::newRow("unlock") << int(PDFAppPermissionAction::Unlock) << QString()
                            << QStringLiteral("The author of this document restricts what can be done with it. If you own the document, enter its owner password to lift the restrictions until the document is closed.");
}

void AppOwnerPasswordTest::dialogTexts()
{
    // The dialog names the action and the missing permission in plain words, and what the
    // document forbids (the bits of the sample)
    QFETCH(int, action);
    QFETCH(QString, toolId);
    QFETCH(QString, expected);
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6")));
    QVERIFY(tab);
    PDFAppPermissionRequest request = toolId.isEmpty() ? PDFAppPermissionRequest::create(PDFAppPermissionAction(action)) : PDFAppPermissionRequest::createTool(toolId);

    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), request, tab);
    QVERIFY(dialog);
    AppDocumentFixture::settle();
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Owner password for restricted-r6.pdf"));
    QCOMPARE(dialog->getText(), expected);
    QCOMPARE(dialog->getForbiddenText(), QStringLiteral("Not allowed: printing, copying text and images, adding and changing annotations, filling in forms and inserting, rotating and removing pages."));
    QCOMPARE(dialog->getCardWidth(), 460);
    QVERIFY(dialog->isBadgeVisible());
    QVERIFY(!dialog->getRemoveButton());
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    QCOMPARE(dialog->getUnlockButton()->text(), QStringLiteral("Unlock"));
    QVERIFY(dialog->getPasswordField()->echoMode() == QLineEdit::Password);
    QVERIFY(!dialog->getUnlockButton()->isEnabled());

    // A second question raises the same dialog
    QCOMPARE(pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), request, tab), dialog.data());
    dialog->reject();
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
}

void AppOwnerPasswordTest::printAsksOwnerPassword_data()
{
    QTest::addColumn<QString>("revision");
    QTest::newRow("R3 RC4") << QStringLiteral("3");
    QTest::newRow("R4 AES-128") << QStringLiteral("4-aes");
    QTest::newRow("R6 AES-256") << QStringLiteral("6");
}

void AppOwnerPasswordTest::printAsksOwnerPassword()
{
    // Print of a document, which does not allow printing, asks for the owner password; the
    // wrong one shows the error, the right one unlocks the tab: no undo step, not modified
    QFETCH(QString, revision);
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), revision));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(!pdf::PDFAppPrintController::isPrintAllowed(document->getDocument().data()));
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::print()));

    handler->execute(pdf::PDFAppCommands::print());
    AppDocumentFixture::settle();
    QVERIFY(!handler->getPrintController()->getDialog());
    QPointer<pdf::PDFAppPasswordDialog> dialog = H::findOwnerDialog(document);
    QVERIFY(dialog);
    QVERIFY(dialog->getText().startsWith(QStringLiteral("The author of this document does not allow printing.")));

    H::unlock(dialog, QStringLiteral("not-the-owner"));
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->getErrorText(), QStringLiteral("Wrong password – check Caps Lock and try again"));
    // Owner 2026-11-08 (398:28125): the wrong password stays, all selected, in the Error state;
    // the next typing replaces it
    QCOMPARE(dialog->getPasswordField()->text(), QStringLiteral("not-the-owner"));
    QCOMPARE(dialog->getPasswordField()->selectedText().size(), QStringLiteral("not-the-owner").size());
    QVERIFY(dialog->getPasswordField()->isInvalid());
    QVERIFY(!document->isOwnerUnlocked());

    const quint64 version = document->getVersion();
    QSignalSpy acceptedSpy(dialog.data(), &QDialog::accepted);
    H::unlock(dialog, H::owner());
    QCOMPARE(acceptedSpy.count(), 1);
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(document->isOwnerUnlocked());
    QVERIFY(document->getVersion() > version);
    QVERIFY(!document->isModified());
    QCOMPARE(AppDocumentFixture::undoCount(document), 0);
    QVERIFY(pdf::PDFAppPrintController::isPrintAllowed(document->getDocument().data()));
    QVERIFY(PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::Print), tab));
    QVERIFY(!H::findOwnerDialog(document));
}

void AppOwnerPasswordTest::unlocksOnlyThisDocument()
{
    // The unlock belongs to the tab: another restricted tab stays locked, a closed and
    // reopened file is restricted again, a copy of the tab is restricted again
    AppDocumentFixture fixture;
    const QString first = H::sample(fixture.folder(), QStringLiteral("6"), QStringLiteral("first.pdf"));
    pdf::PDFAppDocumentTab* firstTab = H::openReady(fixture, first);
    pdf::PDFAppDocumentTab* secondTab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("4-aes"), QStringLiteral("second.pdf")));
    QVERIFY(firstTab && secondTab);
    QVERIFY(firstTab->getDocument()->unlockWithOwnerPassword(H::owner()));
    QVERIFY(!firstTab->getDocument()->unlockWithOwnerPassword(H::owner()));
    QVERIFY(firstTab->getDocument()->isOwnerUnlocked());
    QVERIFY(!secondTab->getDocument()->isOwnerUnlocked());
    QVERIFY(PDFAppDocumentPermissions::canUnlock(secondTab->getDocument()->getDocument().data()));

    pdf::PDFAppDocument copy(fixture.window->getContext(), nullptr);
    QVERIFY(copy.setCopyOf(firstTab->getDocument()));
    QVERIFY(!copy.isOwnerUnlocked());
    QVERIFY(PDFAppDocumentPermissions::canUnlock(copy.getDocument().data()));
    QCOMPARE(H::firstPageContent(copy.getDocument().data()), H::firstPageContent(firstTab->getDocument()->getDocument().data()));

    QVERIFY(fixture.window->closeTab(firstTab));
    AppDocumentFixture::settle();
    pdf::PDFAppDocumentTab* reopened = H::openReady(fixture, first);
    QVERIFY(reopened);
    QVERIFY(!reopened->getDocument()->isOwnerUnlocked());
    QVERIFY(PDFAppDocumentPermissions::canUnlock(reopened->getDocument()->getDocument().data()));
}

void AppOwnerPasswordTest::undoAndSaveKeepEncryption()
{
    // A version before the unlock (undo) stays unlocked; a save writes the encryption
    // dictionary of the file: reopened, the file is restricted again by the same /P
    AppDocumentFixture fixture;
    const QString filePath = H::sample(fixture.folder(), QStringLiteral("6"));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFInteger originalP = H::getP(document->getDocument()->getStorage().getSecurityHandler());

    // A change before the unlock (the bit 4 of the sample allows changes)
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Before the unlock"));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), nullptr, pdf::PDFModifiedDocument::None),
                             QStringLiteral("Title"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(document->isModified());

    QVERIFY(document->unlockWithOwnerPassword(H::owner()));
    QVERIFY(document->isModified());
    QCOMPARE(AppDocumentFixture::undoCount(document), 1);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    AppDocumentFixture::settle();
    QCOMPARE(AppDocumentFixture::undoCount(document), 0);
    QVERIFY(document->isOwnerUnlocked());
    QVERIFY(!PDFAppDocumentPermissions::isRestricted(document->getDocument().data()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::redo()));
    AppDocumentFixture::settle();
    QVERIFY(document->isOwnerUnlocked());
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Before the unlock"));

    // Save: the file keeps the encryption and /P
    QVERIFY(document->isModified());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::save()));
    QVERIFY(QTest::qWaitFor([document]() { return !document->isModified(); }, 20000 * AppDocumentFixture::getTimeoutFactor()));
    AppDocumentFixture::settle();
    const pdf::PDFDocumentPointer saved = H::read(filePath);
    QVERIFY(saved);
    const pdf::PDFSecurityHandler* savedHandler = saved->getStorage().getSecurityHandler();
    QCOMPARE(savedHandler->getMode(), pdf::EncryptionMode::Standard);
    QCOMPARE(PDFAppDocumentPermissions::getRevision(savedHandler), 6);
    QCOMPARE(savedHandler->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized);
    QCOMPARE(H::getP(savedHandler), originalP);
    QVERIFY(PDFAppDocumentPermissions::canUnlock(saved.data()));
    QCOMPARE(saved->getInfo()->title, QStringLiteral("Before the unlock"));
    const pdf::PDFDocumentPointer savedAsOwner = H::read(filePath, H::owner(), true);
    QVERIFY(savedAsOwner);
    QCOMPARE(savedAsOwner->getStorage().getSecurityHandler()->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized);
}

void AppOwnerPasswordTest::copyAsksOwnerPassword()
{
    // Copy of the text (bit 5) asks for the owner password; nothing is copied before the unlock
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("4-rc4")));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationCreator* creator = pdf::PDFAppAnnotateCommandHandler::findCreator(tab->getView());
    QVERIFY(creator);
    tab->getView()->selectAllText();
    QVERIFY(QTest::qWaitFor([creator]() { return creator->hasTextSelection(); }, 10000 * AppDocumentFixture::getTimeoutFactor()));
    AppDocumentFixture::settle();

    QApplication::clipboard()->setText(QStringLiteral("before"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::copy()));
    AppDocumentFixture::settle();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("before"));
    QPointer<pdf::PDFAppPasswordDialog> dialog = H::findOwnerDialog(document);
    QVERIFY(dialog);
    QVERIFY(dialog->getText().contains(QStringLiteral("copying text and images")));

    H::unlock(dialog, H::owner());
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(document->isOwnerUnlocked());
    QVERIFY(QTest::qWaitFor([creator]() { return creator->hasTextSelection(); }, 10000 * AppDocumentFixture::getTimeoutFactor()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::copy()));
    AppDocumentFixture::settle();
    QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("quick brown fox")));
    QVERIFY(!H::findOwnerDialog(document));
}

void AppOwnerPasswordTest::rotateAsksOwnerPassword()
{
    // Rotate of the Thumbnails / the commands is assembling (bit 11, the catalog entry of
    // Rotate Pages): refused without the owner password - the page keeps its rotation
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("3")));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentPanelCommandHandler* handler = tab->findChild<pdf::PDFAppDocumentPanelCommandHandler*>();
    QVERIFY(handler);
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::rotatePagesRight()));

    QVERIFY(!handler->rotatePages({ 0 }, true));
    AppDocumentFixture::settle();
    QCOMPARE(document->getDocument()->getCatalog()->getPage(0)->getPageRotation(), pdf::PageRotation::None);
    QCOMPARE(AppDocumentFixture::undoCount(document), 0);
    QPointer<pdf::PDFAppPasswordDialog> dialog = H::findOwnerDialog(document);
    QVERIFY(dialog);
    QVERIFY(dialog->getText().contains(QStringLiteral("which Rotate Pages needs")));

    H::unlock(dialog, H::owner());
    AppDocumentFixture::settle();
    QVERIFY(handler->rotatePages({ 0 }, true));
    QCOMPARE(document->getDocument()->getCatalog()->getPage(0)->getPageRotation(), pdf::PageRotation::Rotate90);
    QVERIFY(document->isOwnerUnlocked());
}

void AppOwnerPasswordTest::toolContinuesAfterUnlock()
{
    // A tool, which the permissions forbid, asks for the owner password at its entry: Cancel
    // closes the tool (as Close of 339:10328), the right password lets it go on
    AppToolShellFixture fixture;
    const QString toolId = QStringLiteral("pages.rotate");
    QVERIFY(fixture.registerShellTool(toolId));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6")));
    QVERIFY(tab);

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
    QVERIFY(shell);
    AppToolShellFixture::settle();
    QVERIFY(shell->isPermissionRefused());
    QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getPermissionDialog());
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(dialog->getText().contains(QStringLiteral("which Rotate Pages needs")));
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!AppToolShellFixture::getShell(tab));
    QVERIFY(!tab->getDocument()->isOwnerUnlocked());

    QPointer<pdf::PDFDocumentToolShell> secondShell = fixture.openTool(tab, toolId);
    QVERIFY(secondShell);
    AppToolShellFixture::settle();
    dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(secondShell->getPermissionDialog());
    QVERIFY(dialog);
    H::unlock(dialog, H::owner());
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(secondShell);
    QCOMPARE(AppToolShellFixture::getShell(tab), secondShell.data());
    QVERIFY(!secondShell->isPermissionRefused());
    QCOMPARE(secondShell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(tab->getView()->isToolModeActive());
}

void AppOwnerPasswordTest::annotationsAndForm()
{
    // Annotate (bit 6) asks for the owner password; the form, which cannot be filled (bit 9),
    // shows its bar with the unlock; after the unlock both are allowed and the bar is gone
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::formSample(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    QVERIFY(!controller->isAnnotationEditingAllowed());
    QVERIFY(!controller->isFormFillingAllowed());
    QVERIFY(controller->isAnnotationEditingAvailable());
    QVERIFY(QTest::qWaitFor([view]() { return view->findInfoBar(pdf::PDFAppFormController::getRestrictedBarId()) != nullptr; }, 10000 * AppDocumentFixture::getTimeoutFactor()));
    const pdf::PDFAppInfoBarRequest* bar = view->findInfoBar(pdf::PDFAppFormController::getRestrictedBarId());
    QCOMPARE(bar->title, QStringLiteral("This form cannot be filled in"));
    QCOMPARE(bar->text, QStringLiteral("The document does not allow filling in forms. If you own it, unlock it with its owner password."));
    QCOMPARE(bar->actions.size(), size_t(1));

    // Annotate is enabled, it asks
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::annotatePanel()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotatePanel()));
    AppDocumentFixture::settle();
    QPointer<pdf::PDFAppPasswordDialog> dialog = H::findOwnerDialog(document);
    QVERIFY(dialog);
    QVERIFY(dialog->getText().contains(QStringLiteral("adding and changing annotations")));
    dialog->reject();
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    // The bar of the form asks too
    Q_EMIT view->infoBarActionTriggered(pdf::PDFAppFormController::getRestrictedBarId(), pdf::PDFAppFormController::getUnlockActionId());
    AppDocumentFixture::settle();
    dialog = H::findOwnerDialog(document);
    QVERIFY(dialog);
    QVERIFY(dialog->getText().contains(QStringLiteral("filling in forms")));
    H::unlock(dialog, H::owner());
    AppDocumentFixture::settle();
    QVERIFY(controller->isAnnotationEditingAllowed());
    QVERIFY(controller->isFormFillingAllowed());
    QVERIFY(QTest::qWaitFor([view]() { return view->findInfoBar(pdf::PDFAppFormController::getRestrictedBarId()) == nullptr; }, 10000 * AppDocumentFixture::getTimeoutFactor()));
}

void AppOwnerPasswordTest::propertiesUnlock()
{
    // The restrictions shown in the document properties (Security) offer the unlock too
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6")));
    QVERIFY(tab);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentProperties(tab->getDocumentId(), QStringLiteral("security")));
    AppDocumentFixture::settle();
    pdf::PDFAppDocumentPropertiesPage* page = qobject_cast<pdf::PDFAppDocumentPropertiesPage*>(tab->getPage());
    QVERIFY(page);
    page->rebuildNow();
    AppDocumentFixture::settle();
    QPushButton* button = page->findChild<QPushButton*>(QStringLiteral("unlockOwnerPassword"));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Unlock with Owner Password…"));
    button->click();
    AppDocumentFixture::settle();
    QPointer<pdf::PDFAppPasswordDialog> dialog = H::findOwnerDialog(tab->getDocument());
    QVERIFY(dialog);
    QVERIFY(dialog->getText().startsWith(QStringLiteral("The author of this document restricts what can be done with it.")));
    H::unlock(dialog, H::owner());
    AppDocumentFixture::settle();
    page->rebuildNow();
    AppDocumentFixture::settle();
    QVERIFY(!page->findChild<QPushButton*>(QStringLiteral("unlockOwnerPassword")));
    QVERIFY(page->findChildren<pdf::PDFLabel*>(QStringLiteral("notAllowed")).isEmpty());
    pdf::PDFAppParagraph* note = page->findChild<pdf::PDFAppParagraph*>(QStringLiteral("securityNoteText"));
    QVERIFY(note);
    QVERIFY(note->text().startsWith(QStringLiteral("You unlocked the document with its owner password")));
}

void AppOwnerPasswordTest::mergeInputOfUnlockedTab()
{
    // Q2 (c): a restricted input needs the owner password - an unlocked tab is permitted
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("4-aes")));
    QVERIFY(tab);
    QVERIFY(!pdf::PDFAppToolInputLoader::isPermitted(tab->getDocument()->getSnapshot().document.data(), nullptr));
    QVERIFY(tab->getDocument()->unlockWithOwnerPassword(H::owner()));
    const pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(snapshot.document.data()), pdf::PDFDocumentMerger::Permission::Permitted);
    QVERIFY(pdf::PDFAppToolInputLoader::isPermitted(snapshot.document.data(), nullptr));
}

void AppOwnerPasswordTest::mcpRefusalAndUnlock()
{
    // An assistant never gets the password: a refused action says which permission is
    // missing and that the user can unlock the document in PDF4QT; after the unlock by
    // the user the same call succeeds (over HTTP)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6")));
    QVERIFY(tab);
    if (!fixture.startServer())
    {
        QSKIP("The MCP server does not start");
    }
    fixture.setTransport(pdftest::AppMcpTransport::Http);

    QJsonObject annotate = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");
    annotate.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), annotate);
    QVERIFY(H::isError(result));
    const QString refusal = H::text(result);
    QVERIFY2(refusal.startsWith(QStringLiteral("The document does not allow adding and changing annotations (its security settings, ISO 32000-2 Table 22). The user can unlock it in PDF4QT with the owner password")), qPrintable(refusal));
    QVERIFY(refusal.contains(QStringLiteral("Never ask the user for the password")));
    QVERIFY(!refusal.contains(H::owner()));

    QJsonObject rotate = H::json(R"({"action":"rotate","result":"keep","rotate":{"pages":{"pages":"1"},"rotation":"right"}})");
    rotate.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    result = fixture.callTool(QStringLiteral("pages"), rotate);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).startsWith(QStringLiteral("The document does not allow inserting, rotating and removing pages")), qPrintable(H::text(result)));
    QCOMPARE(AppDocumentFixture::undoCount(tab->getDocument()), 0);

    QVERIFY(tab->getDocument()->unlockWithOwnerPassword(H::owner()));
    result = fixture.callTool(QStringLiteral("annotations"), annotate);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    result = fixture.callTool(QStringLiteral("pages"), rotate);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(QTest::qWaitFor([tab]() { return tab->getDocument()->getDocument()->getCatalog()->getPage(0)->getPageRotation() == pdf::PageRotation::Rotate90; },
                            10000 * AppDocumentFixture::getTimeoutFactor()));
    QVERIFY(tab->getDocument()->isOwnerUnlocked());
}

void AppOwnerPasswordTest::mergeRefusalTexts()
{
    // Q2 (c) by the rule of the owner password (owner 2026-11-08): the user unlocks a restricted
    // input in PDF4QT by its owner password; an assistant never asks for it
    QCOMPARE(pdf::PDFDocumentMerger::getPermissionText(pdf::PDFDocumentMerger::Permission::AssembleDenied, QStringLiteral("Contract.pdf")),
             QStringLiteral("Contract.pdf does not allow its pages to be assembled into another document. If you own it, unlock it with its owner password."));
    QCOMPARE(pdf::PDFDocumentMerger::getPermissionText(pdf::PDFDocumentMerger::Permission::OwnerPasswordRequired, QStringLiteral("Contract.pdf")),
             QStringLiteral("Contract.pdf restricts printing, copying or changes, and the merged document would not keep the restrictions. If you own it, unlock it with its owner password."));

    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

    // Table 22: bit 3 (print) is clear, bit 11 (assemble) is set - Merge would lose the restriction
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("print-source.pdf"), 2);
    const QString restrictedFile = H::encryptFile(source, fixture.folder(), QStringLiteral("print-restricted.pdf"), H::createHandler(QStringLiteral("6"), 0xFFFFFFFCu & ~0x0004u));
    pdf::PDFAppDocumentTab* restricted = H::openReady(fixture, restrictedFile);
    pdf::PDFAppDocumentTab* plain = H::openReady(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 1));
    QVERIFY(restricted && plain);
    const pdf::PDFDocument* restrictedDocument = restricted->getDocument()->getDocument().data();

    // The shared check knows the rule of the core (one list, PDFDocumentMerger::getRequiredPermissions)
    QVERIFY(PDFAppDocumentPermissions::isAllowed(restrictedDocument, PDFAppPermissionRequest::createTool(QStringLiteral("pages.rotate"))));
    QVERIFY(!PDFAppDocumentPermissions::isAllowed(restrictedDocument, PDFAppPermissionRequest::createTool(QStringLiteral("pages.merge"))));
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(restrictedDocument), pdf::PDFDocumentMerger::Permission::OwnerPasswordRequired);
    const QString refusal = PDFAppDocumentPermissions::getRefusalText(restrictedDocument, PDFAppPermissionRequest::createTool(QStringLiteral("pages.merge")));
    QVERIFY2(refusal.startsWith(QStringLiteral("The document does not allow printing")), qPrintable(refusal));
    QVERIFY2(refusal.endsWith(QStringLiteral("The new document of Merge is not encrypted and would not keep this restriction. If you own it, unlock it with its owner password.")), qPrintable(refusal));

    // MCP: refused before a run, with the unlock by the user and never the password
    QJsonObject merge;
    merge.insert(QStringLiteral("action"), QStringLiteral("merge"));
    merge.insert(QStringLiteral("documentIds"), QJsonArray({ restricted->getDocument()->getId(), plain->getDocument()->getId() }));
    merge.insert(QStringLiteral("result"), QStringLiteral("open_new_tab"));
    const size_t tabCount = fixture.window->getTabs().size();
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), merge);
    QVERIFY(H::isError(result));
    const QString text = H::text(result);
    QVERIFY2(text.startsWith(QStringLiteral("documentIds[0]: The document does not allow printing")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("(its security settings, ISO 32000-2 Table 22). The new document of Merge is not encrypted and would not keep this restriction. The user can unlock it in PDF4QT with the owner password")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("Never ask the user for the password")));
    QVERIFY(!text.contains(QStringLiteral("Open it with")));
    QCOMPARE(fixture.window->getTabs().size(), tabCount);

    // Unlocked by the user: the same call makes the new document
    QVERIFY(restricted->getDocument()->unlockWithOwnerPassword(H::owner()));
    result = fixture.callTool(QStringLiteral("pages"), merge);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);
}

void AppOwnerPasswordTest::sanitizeAsksOwnerPassword()
{
    // Remove Hidden Data (owner 2026-11-08): the comments of a document, which does not allow
    // changing annotations (Table 22, bit 6), are removed after the owner password - the Primary
    // button asks for it by the shared check like every other action; Cancel leaves the tool
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QStringLiteral("protect.sanitize"));
    QVERIFY(shell);
    QPointer<pdf::PDFAppToolGUISanitize> gui = qobject_cast<pdf::PDFAppToolGUISanitize*>(shell->getGUI());
    QVERIFY(gui && QTest::qWaitFor([gui]() { return gui && gui->isScanned(); }, 20000 * AppDocumentFixture::getTimeoutFactor()));
    AppToolShellFixture::settle();
    QVERIFY(!shell->isPermissionRefused());
    QVERIFY(shell->getModel()->getValue(QStringLiteral("comments")).toBool());
    QCOMPARE(gui->getRunPermissions(), std::vector<Permission>({ Permission::ModifyInteractiveItems }));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY2(shell->getStatusMessage().contains(QStringLiteral("which removing Comments and markup needs: Remove Hidden Data asks for its owner password, or untick Comments and markup")),
             qPrintable(shell->getStatusMessage()));

    // The question; Cancel: nothing runs, the tool and the document stay as they are
    shell->confirm();
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getRunPermissionDialog());
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY2(dialog->getText().contains(QStringLiteral("does not allow adding and changing annotations, which Remove Hidden Data needs")), qPrintable(dialog->getText()));
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(shell);
    QCOMPARE(AppToolShellFixture::getShell(tab), shell.data());
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(!tab->getDocument()->isOwnerUnlocked());
    QCOMPARE(AppDocumentFixture::undoCount(tab->getDocument()), 0);

    // The owner password: the document is unlocked, the user confirms again and the tool runs
    shell->confirm();
    AppToolShellFixture::settle();
    dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getRunPermissionDialog());
    QVERIFY(dialog);
    H::unlock(dialog, H::owner());
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(tab->getDocument()->isOwnerUnlocked());
    QVERIFY(shell && gui);
    QVERIFY(QTest::qWaitFor([gui]() { return gui && gui->isScanned(); }, 20000 * AppDocumentFixture::getTimeoutFactor()));
    AppToolShellFixture::settle();
    QVERIFY2(!shell->getStatusMessage().contains(QStringLiteral("owner password")), qPrintable(shell->getStatusMessage()));
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding));
    QVERIFY(!shell->getRunPermissionDialog());
}

void AppOwnerPasswordTest::sanitizeOwnerDialogLifetime()
{
    // The question of the Primary button of Remove Hidden Data: the tab closed while it is open,
    // the tab closed by a slot of the unlock (documentChanged reaches the shell and the tool)
    AppToolShellFixture fixture;
    for (int variant = 0; variant < 2; ++variant)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture, AppDocumentSamples::restricted(fixture.folder()));
        QVERIFY(tab);
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QStringLiteral("protect.sanitize"));
        QVERIFY(shell);
        QPointer<pdf::PDFAppToolGUISanitize> gui = qobject_cast<pdf::PDFAppToolGUISanitize*>(shell->getGUI());
        QVERIFY(gui && QTest::qWaitFor([gui]() { return gui && gui->isScanned(); }, 20000 * AppDocumentFixture::getTimeoutFactor()));
        AppToolShellFixture::settle();
        shell->confirm();
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppPasswordDialog> dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(shell->getRunPermissionDialog());
        QVERIFY(dialog);

        if (variant == 0)
        {
            QVERIFY(fixture.window->closeTab(tab.data()));
        }
        else
        {
            pdftest::AppToolShellFixture* fixturePointer = &fixture;
            QObject::connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, fixture.window, [fixturePointer, tab]()
            {
                if (tab)
                {
                    fixturePointer->window->closeTab(tab.data());
                }
            });
            H::unlock(dialog, H::owner());
        }
        AppToolShellFixture::settle();
        QVERIFY(!tab);
        QVERIFY(!shell);
        QVERIFY(!dialog);
    }
}

void AppOwnerPasswordTest::dialogLifetime()
{
    // The dialog deleted itself, the tab closed while the dialog is open, the tab closed by
    // a slot of the unlock (documentChanged), the window deleted while the dialog is open
    AppDocumentFixture fixture;
    const PDFAppPermissionRequest print = PDFAppPermissionRequest::create(PDFAppPermissionAction::Print);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6"), QStringLiteral("lifetime-1.pdf")));
    QVERIFY(tab);
    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), print, tab);
    QVERIFY(dialog);
    delete dialog.data();
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), print, tab);
    QVERIFY(dialog);
    QVERIFY(fixture.window->closeTab(tab));
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6"), QStringLiteral("lifetime-2.pdf")));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), print, tab);
    QVERIFY(dialog);
    pdftest::AppDocumentFixture* fixturePointer = &fixture;
    QObject::connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, fixture.window, [fixturePointer, tabGuard]()
    {
        if (tabGuard)
        {
            fixturePointer->window->closeTab(tabGuard.data());
        }
    });
    H::unlock(dialog, H::owner());
    AppDocumentFixture::settle();
    QVERIFY(!tabGuard);
    QVERIFY(!dialog);

    tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6"), QStringLiteral("lifetime-3.pdf")));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(document, print, tab);
    QVERIFY(dialog);
    delete fixture.window.data();
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
}

void AppOwnerPasswordTest::themesAndRightToLeft()
{
    // Both themes without the leak color; right to left Unlock is at the left edge
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            QApplication::setLayoutDirection(direction);
            AppDocumentFixture fixture;
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::sample(fixture.folder(), QStringLiteral("6")));
            QVERIFY(tab);
            QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openOwnerDialog(tab->getDocument(), PDFAppPermissionRequest::create(PDFAppPermissionAction::CopyContent), tab);
            QVERIFY(dialog);
            AppDocumentFixture::settle();
            AppToolShellFixture::screenshotDialog(dialog, QStringLiteral("pdf4qt-owner-password-%1-%2").arg(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark"),
                                                                                                         direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl")));
            const QImage image = AppToolShellFixture::render(dialog, 1.0);
            QCOMPARE(AppToolShellFixture::countLeakPixels(image), 0);
            const int unlockX = dialog->getUnlockButton()->mapTo(dialog, QPoint(0, 0)).x();
            const int cancelX = dialog->getCancelButton()->mapTo(dialog, QPoint(0, 0)).x();
            QVERIFY(direction == Qt::LeftToRight ? unlockX > cancelX : unlockX < cancelX);
            dialog->reject();
            AppDocumentFixture::settle();
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

QTEST_MAIN(AppOwnerPasswordTest)

#include "tst_appownerpasswordtest.moc"
