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

// The GUI of Sign with Certificate and Add Timestamp (step 6 of phase 2, plan 2.1, 2.2, 9; package P4):
// the frames 434:4688, 434:6889, 434:6626, 434:5884, 434:7143, 434:7495, 434:7784, 434:8518, 435:7037,
// 435:7693, 435:8117 by their texts and states, the live check of the password (6.3), the questions
// before a run, the dialogs of the authority with the password kept until they close (R6-11), the
// canary of the password (6.2), the empty signature fields (Q6.3) and the entry of a document, which
// allows only the signing of a field (R6-8). The run itself is the core's (packages P1b, P2).

#include "apptestcertificatefolders.h"
#include "apptestcertificates.h"
#include "apptoolshellfixture.h"

#include "pdfappdocumentsignatures.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_certificates.h"
#include "pdfsettingdefinitions.h"
#include "pdftrustedcertificaterepository.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfapptoolgui_signature.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcertificatestore.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_signature.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfappcolorpicker.h"
#include "pdfapptooldrawingpart.h"

#include <QFocusFrame>
#include <QScrollBar>

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QLabel>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

using pdftest::AppDocumentSamples;
using pdftest::AppTestCertificateFolders;
using pdftest::AppToolShellFixture;
using pdf::PDFAppToolGUISignature;
using pdf::PDFDocumentToolShell;
using Tool = pdf::PDFDocumentToolSignature;

/// Helpers of the cases
class SignatureTestHelper
{
public:
    SignatureTestHelper() = delete;

    static QString password() { return QStringLiteral("jane-secret-7"); }

    /// An authority on the loopback, which refuses the connection (the discard port; no internet)
    static QString localAuthority() { return QStringLiteral("http://127.0.0.1:9/tsr"); }

    static pdf::PDFAppDocumentTab* openDocument(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab* tab, Tool::Mode mode)
    {
        PDFDocumentToolShell* shell = tab ? fixture.openTool(tab, Tool::getToolId(mode)) : nullptr;
        AppToolShellFixture::settle();
        return shell;
    }

    static PDFDocumentToolShell* open(AppToolShellFixture& fixture, Tool::Mode mode)
    {
        return openTool(fixture, openDocument(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2)), mode);
    }

    static PDFAppToolGUISignature* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUISignature*>(shell->getGUI()) : nullptr;
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

    /// A label of the widget shows the text
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

    static QToolButton* menuButton(PDFDocumentToolShell* shell, const QString& name)
    {
        pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->findChild<pdf::PDFAppToolPanelPopup*>(QStringLiteral("toolPanel-") + name);
        return panel ? panel->getButton() : nullptr;
    }

    /// Types the text as the user does, a key event per character
    static void typeText(QWidget* widget, const QString& text)
    {
        for (const QChar character : text)
        {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &release);
        }
        AppToolShellFixture::settle();
    }

    /// The live check has decided (6.3: 500 ms of rest and the job)
    static bool waitForCheck(PDFAppToolGUISignature* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return gui->getPasswordState() != PDFAppToolGUISignature::PasswordState::NotChecked; });
    }

    /// A document of two pages with an empty signature field "Approval" on its first page (Q6.3),
    /// encrypted by AES-256 with an empty user password, the owner password of the samples and the
    /// permissions \p permissions (Table 22), when \p permissions is not 0
    static QString withField(const QString& folder, const QString& name, uint32_t permissions)
    {
        const QString source = AppDocumentSamples::simple(folder, QStringLiteral("source-") + name, 2);
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        const pdf::PDFDocument document = reader.readFromFile(source);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            return QString();
        }
        pdf::PDFDocumentBuilder builder(&document);
        builder.createSignatureField(QStringLiteral("Approval"), pdf::PDFObjectReference(), document.getCatalog()->getPage(0)->getPageReference(), pdf::PDFObjectReference(), QRectF(300, 100, 200, 60));
        if (permissions != 0)
        {
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = AppDocumentSamples::getOwnerPassword();
            settings.permissions = permissions;
            builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }
        pdf::PDFDocument result = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return AppDocumentSamples::write(result, filePath) ? filePath : QString();
    }

    /// Table 22: bit 9 (fill in form fields, sign) allowed, bits 4 (modify) and 6 (annotations,
    /// fields) not; bits 1, 2 = 0, the others 1
    static uint32_t fillFormsOnly()
    {
        return 0xFFFFFFFCu & ~uint32_t(0x0008u | 0x0020u);
    }

    /// A Failed result of a run, whose authority failed (the requested data of P2)
    static pdf::PDFDocumentToolResult timestampFailure(const QString& status, int httpStatus)
    {
        pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolResult::createFailed(QStringLiteral("The timestamp authority failed."));
        result.data.insert(QString::fromLatin1(Tool::DATA_TIMESTAMP_ERROR), status);
        if (httpStatus > 0)
        {
            result.data.insert(QString::fromLatin1(Tool::DATA_HTTP_STATUS), httpStatus);
        }
        return result;
    }

    /// The certificate of the system store of the test (PDFAppToolGUICertificates::setSystemCertificatesProvider):
    /// the store of the machine is never read
    static QByteArray& systemCertificate()
    {
        static QByteArray certificate;
        return certificate;
    }

    static pdf::PDFCertificateEntries systemCertificates()
    {
        pdf::PDFCertificateEntries entries;
        if (const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateInfo::getCertificateInfo(systemCertificate()))
        {
            pdf::PDFCertificateEntry entry;
            entry.type = pdf::PDFCertificateEntry::EntryType::System;
            entry.info = *info;
            entries.push_back(entry);
        }
        return entries;
    }
};

using H = SignatureTestHelper;

/// A timestamp authority of the test on the loopback (S6.6: never the internet): every request is answered
/// by a token of OpenSSL (TS_RESP_CTX, RFC 3161), signed by a self-signed certificate made at run time with
/// the only and critical extended key usage timeStamping (RFC 3161, 2.3)
class SignatureTestAuthority : public QObject
{
public:
    static constexpr const char* NAME = "PDF4QT Test Timestamp Authority";

    SignatureTestAuthority()
    {
        m_key = EVP_EC_gen("P-256");
        m_certificate = X509_new();
        X509_set_version(m_certificate, X509_VERSION_3);
        ASN1_INTEGER_set(X509_get_serialNumber(m_certificate), 1);
        X509_gmtime_adj(X509_getm_notBefore(m_certificate), -3600);
        X509_gmtime_adj(X509_getm_notAfter(m_certificate), 365 * 24 * 3600);
        X509_set_pubkey(m_certificate, m_key);
        X509_NAME* name = X509_get_subject_name(m_certificate);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(NAME), -1, -1, 0);
        X509_set_issuer_name(m_certificate, name);

        X509V3_CTX context = { };
        X509V3_set_ctx_nodb(&context);
        X509V3_set_ctx(&context, m_certificate, m_certificate, nullptr, nullptr, 0);
        const std::vector<std::pair<int, QByteArray>> extensions = { { NID_basic_constraints, "critical,CA:FALSE" }, { NID_key_usage, "critical,digitalSignature" },
                                                                     { NID_ext_key_usage, "critical,timeStamping" } };
        for (const auto& [nid, value] : extensions)
        {
            if (X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.constData()))
            {
                X509_add_ext(m_certificate, extension, -1);
                X509_EXTENSION_free(extension);
            }
        }
        X509_sign(m_certificate, m_key, EVP_sha256());

        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, &SignatureTestAuthority::onNewConnection);
    }

    virtual ~SignatureTestAuthority() override
    {
        X509_free(m_certificate);
        EVP_PKEY_free(m_key);
    }

    bool isListening() const { return m_server.isListening(); }
    QString getUrl() const { return QStringLiteral("http://127.0.0.1:%1/tsr").arg(m_server.serverPort()); }
    int getRequestCount() const { return m_requestCount; }

    /// DER of the certificate of the authority
    QByteArray getCertificate() const
    {
        unsigned char* buffer = nullptr;
        const int size = i2d_X509(m_certificate, &buffer);
        const QByteArray result = size > 0 ? QByteArray(reinterpret_cast<const char*>(buffer), size) : QByteArray();
        OPENSSL_free(buffer);
        return result;
    }

private:
    /// DER of the TimeStampResp of the TimeStampReq
    QByteArray respond(const QByteArray& request) const
    {
        TS_RESP_CTX* context = TS_RESP_CTX_new();
        ASN1_OBJECT* policy = OBJ_txt2obj("1.3.6.1.4.1.13762.3", 1);
        TS_RESP_CTX_set_signer_cert(context, m_certificate);
        TS_RESP_CTX_set_signer_key(context, m_key);
        TS_RESP_CTX_set_signer_digest(context, EVP_sha256());
        TS_RESP_CTX_set_def_policy(context, policy);
        TS_RESP_CTX_add_md(context, EVP_sha256());

        BIO* requestBuffer = BIO_new_mem_buf(request.constData(), int(request.size()));
        TS_RESP* response = TS_RESP_create_response(context, requestBuffer);
        unsigned char* buffer = nullptr;
        const int size = response ? i2d_TS_RESP(response, &buffer) : -1;
        const QByteArray result = size > 0 ? QByteArray(reinterpret_cast<const char*>(buffer), size) : QByteArray();
        OPENSSL_free(buffer);
        TS_RESP_free(response);
        BIO_free_all(requestBuffer);
        ASN1_OBJECT_free(policy);
        TS_RESP_CTX_free(context);
        return result;
    }

    void onNewConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
        {
            socket->setParent(this);
            connect(socket, &QTcpSocket::readyRead, this, &SignatureTestAuthority::onReadyRead);
        }
    }

    void onReadyRead()
    {
        QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
        if (!socket)
        {
            return;
        }
        QByteArray& data = m_pending[socket];
        data += socket->readAll();
        const qsizetype headerEnd = data.indexOf("\r\n\r\n");
        if (headerEnd < 0)
        {
            return;
        }

        // The body of Content-Length (the TimeStampReq) completes the request
        qsizetype length = 0;
        for (const QByteArray& line : data.left(headerEnd).split('\n'))
        {
            if (line.trimmed().toLower().startsWith("content-length:"))
            {
                length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
            }
        }
        if (data.size() < headerEnd + 4 + length)
        {
            return;
        }

        ++m_requestCount;
        const QByteArray body = respond(data.mid(headerEnd + 4, length));
        m_pending.remove(socket);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/timestamp-reply\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    EVP_PKEY* m_key = nullptr;
    X509* m_certificate = nullptr;
    QTcpServer m_server;
    int m_requestCount = 0;
    QHash<QTcpSocket*, QByteArray> m_pending;
};

class AppToolSignatureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void registered();
    void signStart();
    void signNoCertificate();
    void signGuiDetails();
    void livePasswordCheck();
    void invisibleAppearance();
    void timestampTypeAndAddress();
    void questionNoGraphics();
    void questionMultiplePages();
    void questionNoGraphicsField();
    void timestampErrorKeepsPassword();
    void timestampErrorCloseForgets();
    void timestampStart();
    void emptySignatureFields();
    void entryWithFieldPermission();
    void cardSignerAndTrust();

    // Step 6 of phase 2, integration P2S6INTCORE
    void systemStoreCertificateSigns();
    void timeFactOfLocale();

    // Review RS6APP
    void timestampRetryNotStartedForgets();
    void timestampRetryKeepsAnswer();

    // Review RS6FIGMA
    void panelsFitTheirWidth();
    void screenshots();

    // Fix wave of step 6 (left by P2S6FIXCORE)
    void runAsksAnyOfPermissions();
    void authorityTrust_data();
    void authorityTrust();
    void cardClaimedDate();

    // Owner 2026-11-18: Esc clears the selection of the drawing before it leaves the tool
    void escapeClearsDrawingSelection();

    // Owner 2026-11-20: leaving the tool with a drawing of the appearance asks
    void leaveAsksAboutDrawing();
};

void AppToolSignatureTest::initTestCase()
{
    // The certificate folders of the whole process are temporary folders of the test (never the
    // folders of the user); the personal store is a store in the memory of the process made of a
    // certificate of the test - never the store of the machine (Windows; null elsewhere: no store)
    AppTestCertificateFolders::installForProcess();

    // The widgets as the user sees them (review RS6FIGMA: the cases ran in the platform style)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QApplication::setLayoutDirection(Qt::LeftToRight);
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString filePath = AppTestCertificateFolders::create(folder.path(), QStringLiteral("store.pfx"), QStringLiteral("Store Holder"), H::password(), 2048);
    QFile file(filePath);
    QVERIFY(file.open(QFile::ReadOnly));
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(file.readAll(), H::password().toUtf8()));
    pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(false);

    // The trusted root certificates of the system store of a run with an authority: those of the test only
    H::systemCertificate().clear();
    pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(&H::systemCertificates);
}

void AppToolSignatureTest::cleanupTestCase()
{
    pdf::PDFAppToolGUICertificates::setSystemCertificatesProvider(nullptr);
    pdf::PDFPersonalCertificateStore::setStore(nullptr);
}

void AppToolSignatureTest::registered()
{
    AppToolShellFixture fixture;
    for (const Tool::Mode mode : { Tool::Mode::Sign, Tool::Mode::Timestamp })
    {
        const QString toolId = Tool::getToolId(mode);
        QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
        QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(toolId));
    }
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(Tool::getToolId(Tool::Mode::Sign));
    PDFAppToolGUISignature::registerSignatureTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(Tool::getToolId(Tool::Mode::Sign)), tool);
}

void AppToolSignatureTest::signStart()
{
    // 434:4688: a certificate, the empty password, nothing drawn - Save Signed Copy Disabled
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getMode(), Tool::Mode::Sign);

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Save Signed Copy"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::menuButton(shell, QStringLiteral("type"))->text(), QStringLiteral("Signature"));
    QCOMPARE(H::menuButton(shell, QStringLiteral("appearance"))->text(), QStringLiteral("Visible"));
    QCOMPARE(H::menuButton(shell, QStringLiteral("draw"))->text(), QStringLiteral("Draw"));

    // The left panel 434:5080: Signer, the row of the certificate chosen, "Password of jane_novak.pfx"
    QCOMPARE(gui->getLeftHeader()->getTitleLabel()->text(), QStringLiteral("Signer"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("Certificate that signs the document"));
    QVERIFY(gui->getCertificateList()->isVisible());
    QVERIFY(!gui->getEmptyState()->isVisible());
    QCOMPARE(gui->getCertificateList()->getChosenItem()->getTitle(), QStringLiteral("jane_novak.pfx"));
    QCOMPARE(gui->getPasswordLabel()->text(), QStringLiteral("Password of jane_novak.pfx"));
    QVERIFY(!gui->getAuthoritySection()->isVisible());

    // The right panel 434:6889 / 434:4688: Appearance, Nothing drawn yet, the callout, the summary
    QCOMPARE(gui->getRightHeader()->getTitleLabel()->text(), QStringLiteral("Appearance"));
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("Nothing drawn yet"));
    QVERIFY(gui->getNoAppearanceCallout()->isVisible());
    QCOMPARE(gui->getNoAppearanceCallout()->getTitleLabel()->text(), QStringLiteral("No appearance drawn"));
    QVERIFY(!gui->getElementsSection()->isVisible());
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Type: Signature"), QStringLiteral("Format: PAdES"), QStringLiteral("Appearance: Visible · not drawn"),
                                                           QStringLiteral("Signer: jane_novak.pfx"), QStringLiteral("Saved as: New file") }));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Enter the certificate password to sign"));
    QCOMPARE(gui->getDrawingPart()->getCanvasHint()->getText(), QStringLiteral("Draw the appearance on the page with the Draw tools – the signature field takes its outline"));
    QVERIFY(!gui->getUnsavedCallout()->isVisible());
    QVERIFY(!gui->getCertifiedCallout()->isVisible());
}

/// A translator of the change of the language (every text of the application: "[text]")
class SignatureTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (!context || !QByteArray(context).startsWith("pdf") || !sourceText || !*sourceText)
        {
            return QString();
        }
        return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
    }
};

void AppToolSignatureTest::signGuiDetails()
{
    // Review RS6APP / RS6FIGMA of the left panel and the toolbar of Sign with Certificate
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("alice.pfx"), QStringLiteral("Alice"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    // A change of the language makes the panels again: the certificate chosen by the user stays chosen
    // (the first certificate is chosen at the start: the user chooses the other one)
    pdf::PDFAppToolCertificateList* list = gui->getCertificateList();
    QVERIFY(list->getChosenItem());
    const QString firstTitle = list->getChosenItem()->getTitle();
    const QString otherTitle = firstTitle == QLatin1String("alice.pfx") ? QStringLiteral("jane_novak.pfx") : QStringLiteral("alice.pfx");
    int otherIndex = -1;
    for (size_t i = 0; i < list->getCertificates().size(); ++i)
    {
        if (list->getCertificates()[i].getTitle() == otherTitle)
        {
            otherIndex = int(i);
        }
    }
    QVERIFY(otherIndex >= 0);
    list->getRow(size_t(otherIndex))->click();
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCertificateList()->getChosenItem()->getTitle(), otherTitle);
    {
        SignatureTestTranslator translator;
        QCoreApplication::installTranslator(&translator);
        AppToolShellFixture::settle();
        QCoreApplication::removeTranslator(&translator);
        AppToolShellFixture::settle();
    }
    QVERIFY(gui->getCertificateList() && gui->getCertificateList() != list);
    QCOMPARE(gui->getCertificateList()->getChosenItem()->getTitle(), otherTitle);

    // The password field is labelled by "Password of <file>" for a screen reader (its buddy and its
    // accessible name), never by the password
    pdf::PDFLineEdit* field = gui->getPasswordCard()->getPasswordField();
    QCOMPARE(gui->getPasswordLabel()->buddy(), static_cast<QWidget*>(field));
    QCOMPARE(field->accessibleName(), QStringLiteral("Password of %1").arg(otherTitle));
    H::typeText(field, H::password());
    QVERIFY(!field->accessibleName().contains(H::password()));

    // A tool with a history keeps Undo and Redo (434:4704, 434:4705)
    QVERIFY(shell->getToolbar()->isUndoShown());
    QVERIFY(shell->getToolbar()->getUndoButton()->isVisibleTo(shell->getToolbar()));

    // 434:5884: the button Signature type shows the icon of "Signature with timestamp" (its row)
    QToolButton* typeButton = H::menuButton(shell, QString::fromLatin1(Tool::PARAMETER_TYPE));
    QVERIFY(typeButton);
    QCOMPARE(typeButton->property(pdf::PDFAppToolEnumMenu::ICON_PROPERTY).toString(), QStringLiteral("sign-certificate"));
    QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP)).isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(typeButton->property(pdf::PDFAppToolEnumMenu::ICON_PROPERTY).toString(), QStringLiteral("timestamp"));
    QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE)).isEmpty());
    AppToolShellFixture::settle();
    QCOMPARE(typeButton->property(pdf::PDFAppToolEnumMenu::ICON_PROPERTY).toString(), QStringLiteral("sign-certificate"));

    // 434:4850: the tag Optional right after the title Details (title at 0, tag at 52: 8 apart)
    QWidget* tag = fixture.window->findChild<QWidget*>(QStringLiteral("signatureOptionalTag"));
    QVERIFY(tag && tag->isVisible());
    pdf::PDFAppToolSectionHeader* header = qobject_cast<pdf::PDFAppToolSectionHeader*>(tag->parentWidget());
    QVERIFY(header);
    const QRect titleRect = header->getTitleLabel()->geometry();
    QCOMPARE(tag->geometry().left() - (titleRect.left() + header->getTitleLabel()->sizeHint().width()), pdf::PDFAppToolSectionHeader::GAP);
    QVERIFY(tag->geometry().right() < header->width() - 1);

    // 434:5884: the size of the text "13", not "13.0" (the precision stays for 2.5)
    pdf::PDFDoubleSpinBox* size = fixture.window->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("drawingFontSize"));
    QVERIFY(size);
    QCOMPARE(size->textFromValue(13.0), QStringLiteral("13"));
    QCOMPARE(size->textFromValue(12.5), QStringLiteral("12.5"));
    pdf::PDFDoubleSpinBox* width = fixture.window->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("drawingPenWidth"));
    QVERIFY(width);
    QCOMPARE(width->textFromValue(2.0), QStringLiteral("2"));
}

void AppToolSignatureTest::signNoCertificate()
{
    // 434:6889: no certificate - the empty state with Create Certificate… and Import .pfx…, "Signer:
    // Not selected", the status of the frame
    AppTestCertificateFolders folders;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->getEmptyState()->isVisible());
    QVERIFY(!gui->getCertificateList()->isVisible());
    QVERIFY(!gui->getPasswordFrame()->isVisible());
    QVERIFY(gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("signatureEmptyCreate")));
    QVERIFY(gui->getEmptyState()->findChild<QPushButton*>(QStringLiteral("signatureEmptyImport")));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Create or import a certificate to sign"));
    QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Signer: Not selected")));
}

void AppToolSignatureTest::livePasswordCheck()
{
    // 6.3, 434:7143: 500 ms after the last key one job checks the password - a wrong one is an error
    // of the field and the Primary button is Disabled; the right one enables it
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing());

    H::typeText(gui->getPasswordCard()->getPasswordField(), QStringLiteral("wrong"));
    QCOMPARE(gui->getPasswordState(), PDFAppToolGUISignature::PasswordState::NotChecked);
    QVERIFY(H::waitForCheck(gui));
    QCOMPARE(gui->getPasswordState(), PDFAppToolGUISignature::PasswordState::Invalid);
    QCOMPARE(gui->getPasswordCard()->getErrorText(), QStringLiteral("Password to open certificate is invalid."));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Password to open certificate is invalid"));

    gui->getPasswordCard()->clear();
    AppToolShellFixture::settle();
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));
    QCOMPARE(gui->getPasswordState(), PDFAppToolGUISignature::PasswordState::Valid);
    QVERIFY(gui->getPasswordCard()->getErrorText().isEmpty());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // 6.2: the password is never a value of the model
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD)).toString().isEmpty());
}

void AppToolSignatureTest::invisibleAppearance()
{
    // 434:6626: Invisible - Draw, Arrange and Clear All Disabled, the summary Appearance Invisible
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    QTest::mouseClick(gui->getAppearanceRow(QString::fromLatin1(Tool::APPEARANCE_INVISIBLE)), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE)).toString(), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));
    QCOMPARE(H::menuButton(shell, QStringLiteral("appearance"))->text(), QStringLiteral("Invisible"));
    QVERIFY(!H::menuButton(shell, QStringLiteral("draw"))->isEnabled());
    QVERIFY(!gui->getDrawingPart()->getClearButton()->isEnabled());
    QVERIFY(!gui->getNoAppearanceCallout()->isVisible());
    QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Appearance: Invisible")));
    // 434:6626: the subtitle and the canvas hint of the frame (review RS6FIGMA)
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("Invisible signature"));
    QCOMPARE(gui->getDrawingPart()->getCanvasHint()->getText(), QStringLiteral("Invisible signature – nothing is drawn on the page, the signature shows in the Signatures panel"));

    // Undo: Visible again
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::menuButton(shell, QStringLiteral("draw"))->isEnabled());
}

void AppToolSignatureTest::timestampTypeAndAddress()
{
    // 434:5884, 435:7693: Signature with timestamp shows the authority; an invalid address is the
    // error of its field and Disables the Primary button; a valid one is the value tsaUrl
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP)).isEmpty());
    AppToolShellFixture::settle();
    QVERIFY(gui->getAuthoritySection()->isVisible());
    QCOMPARE(H::menuButton(shell, QStringLiteral("type"))->text(), QStringLiteral("Signature with timestamp"));
    QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Type: With timestamp")));
    pdf::PDFAppToolTimestampAuthorityPart* authority = gui->getAuthorityPart();
    QCOMPARE(authority->getUrl(), QStringLiteral("http://timestamp.digicert.com"));
    QVERIFY(authority->getUrls().startsWith(QStringLiteral("http://timestamp.digicert.com")));

    authority->setUrl(QStringLiteral("tsa.acme-corp"));
    AppToolShellFixture::settle();
    QVERIFY(!authority->isUrlValid());
    QVERIFY(!authority->getErrorText().isEmpty());
    QCOMPARE(authority->getTextLabel()->text(), authority->getErrorText());
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Address of the timestamp authority is not valid"));

    authority->setUrl(QStringLiteral("https://tsa.example.com/tsr"));
    AppToolShellFixture::settle();
    QVERIFY(authority->isUrlValid());
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_TSA_URL)).toString(), QStringLiteral("https://tsa.example.com/tsr"));
    QCOMPARE(gui->getRunParameters().value(QString::fromLatin1(Tool::PARAMETER_TSA_URL)).toString(), QStringLiteral("https://tsa.example.com/tsr"));
}

void AppToolSignatureTest::questionNoGraphics()
{
    // 434:7495: Save Signed Copy with a visible signature without graphics asks; nothing runs while
    // the question is open; Back keeps the tool; Sign Invisibly makes the signature invisible
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));
    QVERIFY(gui->isConfirmEnabled());

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    pdf::PDFMessageBox* dialog = gui->getQuestionDialog();
    const QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(2));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Back"));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Sign Invisibly"));
    QVERIFY(!gui->isConfirmEnabled());

    // 6.1: the run was refused - nothing of the certificate is kept for it
    QVERIFY(gui->getCertificateRunValues().isEmpty());

    QTest::mouseClick(buttons[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!gui->getQuestionDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE)).toString(), QString::fromLatin1(Tool::APPEARANCE_VISIBLE));
    QCOMPARE(gui->getPasswordCard()->getPassword(), H::password());

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
    QTest::mouseClick(gui->getQuestionDialog()->getActionButtons()[1], Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE)).toString(), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));

    // The answer has confirmed again: the run has taken the password (6.1: the field is empty)
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getPasswordCard() && gui->getPasswordCard()->isEmpty(); }));
}

void AppToolSignatureTest::questionNoGraphicsField()
{
    // 434:7495 also for "In field Approval" (Q6.3: the drawing fits the field): the visible appearance
    // without graphics asks the same question before any run - never the refusal of the core only; Sign
    // Invisibly signs the field invisibly (the field stays chosen) and the signed copy is made
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openTool(fixture, H::openDocument(fixture, H::withField(fixture.folder(), QStringLiteral("Contract.pdf"), 0)), Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QTest::mouseClick(gui->getAppearanceRow(QStringLiteral("Approval")), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FIELD)).toString(), QStringLiteral("Approval"));
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE)).toString(), QString::fromLatin1(Tool::APPEARANCE_VISIBLE));
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));
    QVERIFY(gui->isConfirmEnabled());

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    const QList<QPushButton*> buttons = gui->getQuestionDialog()->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(2));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Sign Invisibly"));
    QVERIFY(gui->getCertificateRunValues().isEmpty());

    QTest::mouseClick(buttons[1], Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE)).toString(), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FIELD)).toString(), QStringLiteral("Approval"));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
    QCOMPARE(shell->getResultDialog()->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    // The FileOnly result closes the tool: its page with the shell is deleted later
    const QPointer<PDFDocumentToolShell> guardedShell = shell;
    shell->getResultDialog()->confirm();
    QVERIFY(AppToolShellFixture::waitFor([guardedShell]() { return !guardedShell || !guardedShell->getResultDialog(); }, 30000));
}

void AppToolSignatureTest::questionMultiplePages()
{
    // 434:7784: graphics on more pages - only the first page with graphics is used
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    pdf::PDFPageDrawing::Element first;
    first.kind = pdf::PDFPageDrawing::Kind::Rectangle;
    first.pageIndex = 0;
    first.rectangle = QRectF(100, 100, 80, 30);
    pdf::PDFPageDrawing::Element second = first;
    second.pageIndex = 1;
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ first, second }));
    AppToolShellFixture::settle();
    QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Appearance: Visible · page 1")));
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("2 elements on 2 pages"));

    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
    const QList<QPushButton*> buttons = gui->getQuestionDialog()->getActionButtons();
    QCOMPARE(buttons[0]->text(), QStringLiteral("Back"));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Sign With Page 1 Graphics"));

    // One other page is "page 2", not "pages 2" (review RS6FIGMA; 434:7784 names more pages "pages 12 and 308")
    QVERIFY(H::hasLabel(gui->getQuestionDialog(), QStringLiteral("A signature has one appearance. Only the graphics on page 1, the first page that contains them, will be used. Graphics on page 2 are left out.")));
    QTest::mouseClick(buttons[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
}

void AppToolSignatureTest::timestampErrorKeepsPassword()
{
    // R6-11, 434:8518: the error of the authority opens its dialog; the password of the run stays
    // until it closes - Try Again runs with it (the field is empty since the run started)
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(shell->getModel()->setValues({ { QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP) },
                                           { QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE) } }).isEmpty());

    // No internet in a test: the authority is a local address, which refuses the connection
    gui->getAuthorityPart()->setUrl(H::localAuthority());
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));

    // The run of the shell as it happens: started (the field empties), ended, its result failed
    gui->onRunStateChanged(true);
    QVERIFY(gui->getPasswordCard()->isEmpty());
    QVERIFY(gui->isRetryPasswordKept());
    gui->onRunStateChanged(false);
    gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("timeout"), 0));
    AppToolShellFixture::settle();
    pdf::PDFMessageBox* dialog = gui->getTimestampErrorDialog();
    QVERIFY(dialog);
    QVERIFY(gui->isRetryPasswordKept());
    QVERIFY(H::hasLabel(dialog, QStringLiteral("The timestamp authority cannot be contacted")));

    // 434:8518: Close, Change Authority | Sign Without Timestamp, Try Again (Primary); Authority, Problem
    const QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(4));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Close"));
    QCOMPARE(buttons[1]->text(), QStringLiteral("Change Authority"));
    QCOMPARE(buttons[2]->text(), QStringLiteral("Sign Without Timestamp"));

    // The card of 434:8518 is 680 wide: the leading pair (Change Authority ends at 655) and the trailing
    // pair (Sign Without Timestamp starts at 718) keep a gap
    QCOMPARE(dialog->getCardWidth(), 680);
    const int leadingEnd = buttons[1]->mapTo(dialog, QPoint(buttons[1]->width(), 0)).x();
    const int trailingStart = buttons[2]->mapTo(dialog, QPoint(0, 0)).x();
    QVERIFY2(trailingStart - leadingEnd > 12, qPrintable(QStringLiteral("%1 %2").arg(leadingEnd).arg(trailingStart)));
    QCOMPARE(buttons[3]->text(), QStringLiteral("Try Again"));
    QVERIFY(buttons[3]->isDefault());
    const pdf::PDFKeyValueList* rows = dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("signatureTimestampErrorRows"));
    QVERIFY(rows);
    QCOMPARE(H::rows(rows), QStringList({ QStringLiteral("Authority: 127.0.0.1"), QStringLiteral("Problem: Connection timed out") }));

    // Try Again: the run starts with the kept password, although the field is empty
    QVERIFY(gui->getPasswordCard()->isEmpty());
    QTest::mouseClick(buttons[3], Qt::LeftButton);
    QVERIFY(!gui->getTimestampErrorDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);

    // The run to the local address ends (refused), the tool edits or decides again
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() != PDFDocumentToolShell::State::Running; }));
    fixture.closeMessageBoxes();
}

void AppToolSignatureTest::timestampRetryNotStartedForgets()
{
    // Review RS6APP, R6-11: the kept password lives until the error dialog closes and its run starts.
    // Try Again, whose run does not start (here the .pfx file cannot be opened any more), forgets it:
    // the Primary button does not sign later with a password the field does not show
    AppTestCertificateFolders folders;
    const QString certificatePath = AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048);
    QVERIFY(!certificatePath.isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(shell->getModel()->setValues({ { QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP) },
                                           { QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE) } }).isEmpty());
    gui->getAuthorityPart()->setUrl(H::localAuthority());
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));

    gui->onRunStateChanged(true);
    gui->onRunStateChanged(false);
    gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("timeout"), 0));
    AppToolShellFixture::settle();
    QVERIFY(gui->getTimestampErrorDialog());
    QVERIFY(gui->isRetryPasswordKept());

    // The file goes away; Try Again cannot unlock it - no run
    QVERIFY(QFile::remove(certificatePath));
    QTest::mouseClick(gui->getTimestampErrorDialog()->getActionButtons()[3], Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QVERIFY(!gui->getTimestampErrorDialog());
    QVERIFY(!gui->isRetryPasswordKept());
    QVERIFY(gui->getPasswordCard()->isEmpty());
    QVERIFY(gui->getCertificateRunValues().isEmpty());
    QVERIFY(!gui->isConfirmEnabled());
}

void AppToolSignatureTest::timestampRetryKeepsAnswer()
{
    // Review RS6APP, R6-11 + 434:7784: Try Again runs the failed run again - the question answered for
    // it (Sign With Page 1 Graphics) is not asked again; the run starts with the kept password
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(shell->getModel()->setValues({ { QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP) } }).isEmpty());

    pdf::PDFPageDrawing::Element first;
    first.kind = pdf::PDFPageDrawing::Kind::Rectangle;
    first.pageIndex = 0;
    first.rectangle = QRectF(100, 100, 80, 30);
    pdf::PDFPageDrawing::Element second = first;
    second.pageIndex = 1;
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ first, second }));
    gui->getAuthorityPart()->setUrl(H::localAuthority());
    AppToolShellFixture::settle();
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));

    // The question of the first run is answered, the run fails at the (refusing) local authority
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
    QTest::mouseClick(gui->getQuestionDialog()->getActionButtons()[1], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getTimestampErrorDialog() != nullptr; }, 60000));
    QVERIFY(gui->isRetryPasswordKept());

    // Try Again: the run starts at once, no second question
    QTest::mouseClick(gui->getTimestampErrorDialog()->getActionButtons()[3], Qt::LeftButton);
    QVERIFY(!gui->getQuestionDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Running);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() != PDFDocumentToolShell::State::Running; }, 60000));
    fixture.closeMessageBoxes();
}

void AppToolSignatureTest::timestampErrorCloseForgets()
{
    // R6-11: Close forgets the password - the Primary button needs it typed again; 435:8117 of Add
    // Timestamp has no Sign Without Timestamp and says the HTTP status
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
        PDFAppToolGUISignature* gui = H::gui(shell);
        QVERIFY(gui);
        QVERIFY(shell->getModel()->setValues({ { QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP) },
                                               { QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE) } }).isEmpty());
        H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
        QVERIFY(H::waitForCheck(gui));
        gui->onRunStateChanged(true);
        gui->onRunStateChanged(false);
        gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("no-network"), 0));
        AppToolShellFixture::settle();
        QVERIFY(gui->getTimestampErrorDialog());
        QTest::mouseClick(gui->getTimestampErrorDialog()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();
        QVERIFY(!gui->getTimestampErrorDialog());
        QVERIFY(!gui->isRetryPasswordKept());
        QVERIFY(!gui->isConfirmEnabled());
    }

    {
        AppToolShellFixture fixture(QSize(1440, 960));
        PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Timestamp);
        PDFAppToolGUISignature* gui = H::gui(shell);
        QVERIFY(gui);
        gui->onRunStateChanged(true);
        gui->onRunStateChanged(false);
        gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("http-error"), 503));
        AppToolShellFixture::settle();
        pdf::PDFMessageBox* dialog = gui->getTimestampErrorDialog();
        QVERIFY(dialog);
        QVERIFY(H::hasLabel(dialog, QStringLiteral("The timestamp was not added")));
        QStringList texts;
        for (QPushButton* button : dialog->getActionButtons())
        {
            texts << button->text();
        }
        QCOMPARE(texts, QStringList({ QStringLiteral("Close"), QStringLiteral("Change Authority"), QStringLiteral("Try Again") }));
        QCOMPARE(H::rows(dialog->findChild<pdf::PDFKeyValueList*>()), QStringList({ QStringLiteral("Authority: timestamp.digicert.com"), QStringLiteral("Problem: HTTP 503 · service unavailable") }));
        dialog->reject();
        AppToolShellFixture::settle();
    }
}

void AppToolSignatureTest::timestampStart()
{
    // 435:7037 without signatures: no cards, No certificate is needed, the authority with its hint,
    // the summary Result, Save Timestamped Copy enabled with the preset authority
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Timestamp);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getMode(), Tool::Mode::Timestamp);
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Save Timestamped Copy"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(gui->getNoCertificateNeededCallout()->isVisible());
    QVERIFY(gui->getSignatureCards().empty());
    QCOMPARE(gui->getRightHeader()->getTitleLabel()->text(), QStringLiteral("Timestamp authority"));
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("RFC 3161 server"));
    QCOMPARE(gui->getAuthorityPart()->getTextLabel()->text(), QStringLiteral("Pick an authority from the list or type your own address. The last used address is remembered."));
    QCOMPARE(H::rows(gui->getSummaryRows()), QStringList({ QStringLiteral("Type: Timestamp"), QStringLiteral("Appearance: Invisible"), QStringLiteral("Signatures: None"), QStringLiteral("Saved as: New file") }));
    QCOMPARE(gui->getStatusText(), QStringLiteral("The timestamped copy is written to a new file · the open document stays unchanged"));
    QVERIFY(!H::menuButton(shell, QStringLiteral("type")));
}

void AppToolSignatureTest::emptySignatureFields()
{
    // Q6.3: the empty signature field of the form is a row of the menu Visible; its choice is the
    // parameter field (one step), the summary and the button name it
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openTool(fixture, H::openDocument(fixture, H::withField(fixture.folder(), QStringLiteral("Contract.pdf"), 0)), Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getEmptyFields().size(), size_t(1));
    QCOMPARE(gui->getEmptyFields().front().name, QStringLiteral("Approval"));

    // The builder makes a field without an appearance invisible (/Rect [0 0 0 0])
    QVERIFY(!gui->getEmptyFields().front().isVisible);
    QVERIFY(gui->getEmptyFields().front().disabledReason.isEmpty());

    pdf::PDFTileRow* row = gui->getAppearanceRow(QStringLiteral("Approval"));
    QVERIFY(row);
    QCOMPARE(row->getTitle(), QStringLiteral("In field Approval"));
    QTest::mouseClick(row, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FIELD)).toString(), QStringLiteral("Approval"));
    QCOMPARE(H::menuButton(shell, QStringLiteral("appearance"))->text(), QStringLiteral("In field Approval"));
    QVERIFY(H::rows(gui->getSummaryRows()).contains(QStringLiteral("Appearance: In field Approval")));

    // Back to a new field
    QTest::mouseClick(gui->getAppearanceRow(QString::fromLatin1(Tool::APPEARANCE_VISIBLE)), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FIELD)).toString().isEmpty());
}

void AppToolSignatureTest::entryWithFieldPermission()
{
    // The entry (coordinator, R6-8): a document, which allows the signing of a field (bit 9) but no
    // new field (bits 4 and 6), opens the tool without the owner password, when it has an empty
    // field; without such a field the entry is refused (no way of the run is permitted)
    {
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFAppDocumentTab* tab = H::openDocument(fixture, H::withField(fixture.folder(), QStringLiteral("Form.pdf"), H::fillFormsOnly()));
        QVERIFY(tab);
        PDFDocumentToolShell* shell = H::openTool(fixture, tab, Tool::Mode::Sign);
        QVERIFY(shell);
        QVERIFY(!shell->isPermissionRefused());
        QVERIFY(!shell->getPermissionDialog());
        PDFAppToolGUISignature* gui = H::gui(shell);
        QVERIFY(gui->handlesPermissionRefusal());

        // The run of a new field needs bits 4 and 6 (asked at the run, the owner password dialog)
        const std::vector<pdf::PDFSecurityHandler::Permission> permissions = gui->getRunPermissions();
        QVERIFY(std::find(permissions.cbegin(), permissions.cend(), pdf::PDFSecurityHandler::Permission::Modify) != permissions.cend());
    }

    {
        AppToolShellFixture fixture(QSize(1440, 960));
        const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2);
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        const pdf::PDFDocument document = reader.readFromFile(source);
        pdf::PDFDocumentBuilder builder(&document);
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = AppDocumentSamples::getOwnerPassword();
        settings.permissions = H::fillFormsOnly();
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        pdf::PDFDocument restricted = builder.build();
        const QString filePath = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("NoField.pdf"));
        QVERIFY(AppDocumentSamples::write(restricted, filePath));
        pdf::PDFAppDocumentTab* tab = H::openDocument(fixture, filePath);
        QVERIFY(tab);
        PDFDocumentToolShell* shell = fixture.openTool(tab, Tool::getToolId(Tool::Mode::Sign));
        AppToolShellFixture::settle();
        QVERIFY(shell);
        QVERIFY(shell->isPermissionRefused());
        QVERIFY(!H::gui(shell)->handlesPermissionRefusal());
        fixture.closeMessageBoxes();
    }
}

void AppToolSignatureTest::cardSignerAndTrust()
{
    // R6-2, B118 (plan 6.7): the card names the signer of the SignerInfo (the first certificate, P1a)
    // and offers to trust only a certificate of the chain built from it - the signer of a chain without
    // a root, the self-signed root of a chain, the signer itself when it is self-signed; nothing, when
    // the signer was not identified. The later revisions of the allowed list say so (R6-3).
    using Result = pdf::PDFSignatureVerificationResult;
    using Verifier = pdf::PDFAppDocumentSignatureVerifier;
    const std::optional<pdf::PDFCertificateInfo> signer = pdf::PDFCertificateInfo::getCertificateInfo(pdftest::AppTestCertificates::first());
    const std::optional<pdf::PDFCertificateInfo> root = pdf::PDFCertificateInfo::getCertificateInfo(pdftest::AppTestCertificates::second());
    QVERIFY(signer && root);

    // A chain without a trusted root (TrustedNotFound): the signer, not the last certificate
    Result partial;
    partial.setCertificateInfos({ *signer, *root }, true);
    partial.addCertificateTrustedNotFoundError();
    QCOMPARE(Verifier::getTrustableCertificateIndex(partial, {}), 0);

    // A chain ending by its self-signed root, which is not trusted: the root
    Result rooted;
    rooted.setCertificateInfos({ *signer, *root }, true);
    rooted.addCertificateSelfSignedInChainError();
    QCOMPARE(Verifier::getTrustableCertificateIndex(rooted, {}), 1);

    // The root is trusted already: nothing
    QCOMPARE(Verifier::getTrustableCertificateIndex(rooted, { pdftest::AppTestCertificates::second() }), -1);

    // A self-signed signer: the signer
    Result selfSigned;
    selfSigned.setCertificateInfos({ *signer }, true);
    selfSigned.addCertificateSelfSignedError();
    QCOMPARE(Verifier::getTrustableCertificateIndex(selfSigned, {}), 0);

    // The signer was not identified: no name of a certificate of the bag, nothing to trust
    Result unknown;
    unknown.setCertificateInfos({ *root }, false);
    unknown.addCertificateTrustedNotFoundError();
    QCOMPARE(Verifier::getTrustableCertificateIndex(unknown, {}), -1);
    QCOMPARE(Verifier::getCard(unknown, false, {}).signer, QStringLiteral("Unknown signer"));
    QCOMPARE(Verifier::getCard(partial, false, {}).signer, QStringLiteral("PDF4QT test certificate 1"));
}

void AppToolSignatureTest::systemStoreCertificateSigns()
{
#ifdef Q_OS_WIN
    // Q6.2, 5.3: the list of Sign with Certificate is of the usage Signing - a certificate of the store of
    // Windows (here the store in the memory of the process of initTestCase, never the store of the machine)
    // is a row, which can be chosen (of the usage Encryption it would be Disabled, Q5.4); the run gets the
    // entry without a password and the native window of the tool for the PIN dialog of the system
    AppTestCertificateFolders folders;
    pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(true);
    struct SystemStoreGuard
    {
        ~SystemStoreGuard() { pdf::PDFAppToolCertificateProvider::setSystemStoreIncluded(false); }
    } systemStoreGuard;

    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->getCertificateList()->isVisible());
    const pdf::PDFAppToolCertificateItem* chosen = gui->getCertificateList()->getChosenItem();
    QVERIFY(chosen);
    QCOMPARE(chosen->source, pdf::PDFAppToolCertificateItem::Source::SystemStore);
    QCOMPARE(chosen->state, pdf::PDFAppToolCertificateItem::State::Valid);

    // Invisible: nothing to draw - the key of the store needs no password, Save Signed Copy is enabled
    QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE)).isEmpty());
    AppToolShellFixture::settle();
    QVERIFY(gui->isConfirmEnabled());
    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    QCOMPARE(environment.userCertificates.size(), size_t(1));
    QVERIFY(environment.userCertificates.front().pkcs12.isEmpty());
    QVERIFY(!gui->getCertificateRunValues().contains(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD)));
    QCOMPARE(environment.userCertificates.front().type, pdf::PDFCertificateEntry::EntryType::System);
    QVERIFY(environment.parentWindow != 0);
    QCOMPARE(environment.parentWindow, quintptr(gui->getCertificateList()->window()->winId()));
    QCOMPARE(pdf::PDFDocumentToolRunner::createContext(pdf::PDFAppDocumentSnapshot(), environment).parentWindow, environment.parentWindow);
#else
    QSKIP("The certificate store of the system exists only on Windows (Q6.2).");
#endif
}

void AppToolSignatureTest::timeFactOfLocale()
{
    // O17: the fact Time of the result dialog (434:9276 "Sep 30, 2026 · 14:32:08 UTC") is formatted in the
    // language of the user interface by PDFAppLocale from the time the core gives as data (ISO 8601 UTC)
    AppTestCertificateFolders folders;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    const auto createInput = []()
    {
        pdf::PDFAppToolResultInput input;
        input.result.facts.push_back({ QString::fromLatin1(Tool::FACT_TIME), QStringLiteral("Time"), QStringLiteral("of the core") });
        input.result.data.insert(QString::fromLatin1(Tool::DATA_SIGNING_TIME), QStringLiteral("2026-09-30T14:32:08Z"));
        return input;
    };

    pdf::PDFAppToolResultInput english = createInput();
    gui->fillResultInput(&english);
    QCOMPARE(english.result.facts.front().value, QStringLiteral("Sep 30, 2026 · 14:32:08 UTC"));

    const QLocale previous = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::Czech, QLocale::Czechia));
    pdf::PDFAppToolResultInput czech = createInput();
    gui->fillResultInput(&czech);
    const QString czechDate = pdf::PDFAppLocale::formatDate(QDate(2026, 9, 30));
    pdf::PDFAppLocale::setUiLocale(previous);
    QVERIFY(czechDate != QStringLiteral("Sep 30, 2026"));
    QCOMPARE(czech.result.facts.front().value, czechDate + QStringLiteral(" · 14:32:08 UTC"));
}

void AppToolSignatureTest::panelsFitTheirWidth()
{
    // 434:4338 (review RS6FIGMA): the left panel 300 and the right panel 320 hold their content - the
    // cards 275 / 287 wide; the Text color row 1567:19183 (black, white, red, orange, yellow, green, blue,
    // "+") fits its card of 261, the text fields of Details 434:4502 take the width of their card; the
    // summary 434:4938 has the Key value rows 8 apart; the facts of the error of the authority 434:8518
    // lie on the filled note
    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);

    pdf::PDFPageDrawing::Element text;
    text.kind = pdf::PDFPageDrawing::Kind::Text;
    text.rectangle = QRectF(100, 100, 90, 18);
    text.text = QStringLiteral("Jane Novak");
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ text }));
    AppToolShellFixture::settle();
    gui->getDrawingPart()->selectElements({ gui->getDrawingPart()->getElementIds().front() });
    AppToolShellFixture::settle();
    QVERIFY(gui->getDrawingPart()->getStylePanel()->isTextStyleShown());

    // Every visible widget of a panel lies inside of the panel (nothing is cut at its edge)
    for (QWidget* panel : { shell->getLeftPanel(), shell->getRightPanel() })
    {
        QVERIFY(panel);
        const QRect bounds = panel->rect();
        for (QWidget* child : panel->findChildren<QWidget*>())
        {
            if (!child->isVisible() || child->isWindow() || qobject_cast<QScrollBar*>(child) || qobject_cast<QFocusFrame*>(child))
            {
                continue;
            }
            const QRect rect(child->mapTo(panel, QPoint(0, 0)), child->size());
            QVERIFY2(rect.left() >= bounds.left() && rect.right() <= bounds.right(),
                     qPrintable(QStringLiteral("%1 %2 at %3..%4 of %5").arg(QString::fromLatin1(child->metaObject()->className()), child->objectName()).arg(rect.left()).arg(rect.right()).arg(bounds.width())));
        }
    }

    pdf::PDFColorSwatchRow* textColors = gui->getDrawingPart()->getStylePanel()->getTextColorRow();
    QCOMPARE(textColors->getSwatchCount(), 7);
    QCOMPARE(textColors->getEntry(0).name, QStringLiteral("Black"));
    QCOMPARE(textColors->getEntry(6).name, QStringLiteral("Blue"));
    QVERIFY(textColors->getMoreButton() && textColors->getMoreButton()->isVisible());
    QVERIFY(pdf::PDFAppColorPicker::find(textColors));
    QVERIFY(pdf::PDFAppColorPicker::find(gui->getDrawingPart()->getStylePanel()->getPenColorRow()));

    // card 434:4614: Combo box 150, Spin box 90, the alignment 195 (its "Center" whole)
    pdf::PDFAppToolDrawingStylePanel* style = gui->getDrawingPart()->getStylePanel();
    QCOMPARE(style->getFontCombo()->width(), 150);
    QCOMPARE(style->getFontSizeSpin()->width(), 90);
    QCOMPARE(style->getAlignmentControl()->width(), 195);
    QCOMPARE(style->getAngleSpin()->width(), 90);

    QWidget* reason = gui->getLeftHeader()->parentWidget()->findChild<QWidget*>(QStringLiteral("signatureField-") + QString::fromLatin1(Tool::PARAMETER_REASON));
    QVERIFY(reason);
    QCOMPARE(reason->width(), reason->parentWidget()->width());

    QCOMPARE(gui->getSummaryRows()->getRowGap(), 8);

    gui->onRunStateChanged(true);
    gui->onRunStateChanged(false);
    gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("timeout"), 0));
    AppToolShellFixture::settle();
    QVERIFY(gui->getTimestampErrorDialog());
    const QWidget* facts = gui->getTimestampErrorDialog()->findChild<pdf::PDFKeyValueList*>(QStringLiteral("signatureTimestampErrorRows"))->parentWidget();
    QVERIFY(pdf::PDFWidgetStyleProperties::isSet(facts, pdf::PDFWidgetStyleProperties::NOTE));
    gui->getTimestampErrorDialog()->reject();
    AppToolShellFixture::settle();

    // Add Timestamp: the summary 435:7185 has the same gap
    PDFDocumentToolShell* timestampShell = H::openTool(fixture, H::openDocument(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1)), Tool::Mode::Timestamp);
    QVERIFY(H::gui(timestampShell));
    QCOMPARE(H::gui(timestampShell)->getSummaryRows()->getRowGap(), 8);
}

void AppToolSignatureTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS6FIGMA): the states of Sign with Certificate and Add
    // Timestamp under the ids of their frames (434:4338 ... 435:8117) in both themes, at 1x / 2x and
    // right-to-left, for the comparison with Figma; no pixel of the leak color in any of them (Fluent)
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
        AppTestCertificateFolders folders;
        QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());

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
        const auto shotWidget = [&suffix, &leaks](QWidget* widget, const QString& frame)
        {
            AppToolShellFixture::settle();
            AppToolShellFixture::screenshotDialog(widget, frame + suffix);
            if (widget && AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(widget, 1.0)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto openMenu = [&shot, &shotWidget](PDFDocumentToolShell* shell, const QString& name, const QString& frame)
        {
            QToolButton* button = H::menuButton(shell, name);
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            AppToolShellFixture::settle();
            shot(frame);
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                shotWidget(popup, frame + QStringLiteral("-popup"));
                popup->close();
            }
            AppToolShellFixture::settle();
        };

        PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
        PDFAppToolGUISignature* gui = H::gui(shell);
        QVERIFY(gui);
        shot(QStringLiteral("434-4688"));

        // 434:4338: a curve, a horizontal line and the text label, the text label selected
        pdf::PDFPageDrawing::Element curve;
        curve.kind = pdf::PDFPageDrawing::Kind::FreehandCurve;
        curve.points = { QPointF(320, 120), QPointF(330, 160), QPointF(345, 110), QPointF(360, 150), QPointF(400, 140) };
        curve.style.penColor = QColor(0x1D, 0x4E, 0xD8);
        curve.style.penWidth = 2.0;
        pdf::PDFPageDrawing::Element line;
        line.kind = pdf::PDFPageDrawing::Kind::Line;
        line.points = { QPointF(310, 100), QPointF(500, 100) };
        pdf::PDFPageDrawing::Element text;
        text.kind = pdf::PDFPageDrawing::Kind::Text;
        text.rectangle = QRectF(315, 80, 90, 18);
        text.text = QStringLiteral("Jane Novak");
        text.style.fontSize = 13.0;
        gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ curve, line, text }));
        AppToolShellFixture::settle();
        const std::vector<pdf::PDFInteger> ids = gui->getDrawingPart()->getElementIds();
        QCOMPARE(ids.size(), size_t(3));
        gui->getDrawingPart()->selectElements({ ids.back() });
        H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
        QVERIFY(H::waitForCheck(gui));
        shot(QStringLiteral("434-4338"));

        openMenu(shell, QStringLiteral("type"), QStringLiteral("434-4951-type"));
        openMenu(shell, QStringLiteral("appearance"), QStringLiteral("434-4951-appearance"));
        openMenu(shell, QStringLiteral("draw"), QStringLiteral("434-4951-draw"));
        openMenu(shell, QStringLiteral("arrange"), QStringLiteral("434-5421"));

        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP)).isEmpty());
        shot(QStringLiteral("434-5884"));
        gui->getAuthorityPart()->setUrl(QStringLiteral("tsa.acme-corp"));
        shot(QStringLiteral("434-invalid-url"));
        gui->getAuthorityPart()->setUrl(QStringLiteral("http://timestamp.digicert.com"));
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE)).isEmpty());

        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE)).isEmpty());
        shot(QStringLiteral("434-6626"));
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_VISIBLE)).isEmpty());

        // 434:7495, 434:7784: the questions
        gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing());
        AppToolShellFixture::settle();
        if (gui->getPasswordCard()->isEmpty())
        {
            H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
            QVERIFY(H::waitForCheck(gui));
        }
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
        shot(QStringLiteral("434-7495"));
        shotWidget(gui->getQuestionDialog(), QStringLiteral("434-7495-dialog"));
        QTest::mouseClick(gui->getQuestionDialog()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();

        pdf::PDFPageDrawing::Element first;
        first.kind = pdf::PDFPageDrawing::Kind::Rectangle;
        first.rectangle = QRectF(100, 100, 80, 30);
        pdf::PDFPageDrawing::Element second = first;
        second.pageIndex = 1;
        gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ first, second }));
        AppToolShellFixture::settle();
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getQuestionDialog() != nullptr; }));
        shot(QStringLiteral("434-7784"));
        shotWidget(gui->getQuestionDialog(), QStringLiteral("434-7784-dialog"));
        QTest::mouseClick(gui->getQuestionDialog()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();

        // 434:7143: a wrong password
        gui->getPasswordCard()->clear();
        AppToolShellFixture::settle();
        H::typeText(gui->getPasswordCard()->getPasswordField(), QStringLiteral("wrong"));
        QVERIFY(H::waitForCheck(gui));
        shot(QStringLiteral("434-7143"));

        // 434:8518: the error of the authority
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP)).isEmpty());
        gui->getPasswordCard()->clear();
        H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
        QVERIFY(H::waitForCheck(gui));
        gui->onRunStateChanged(true);
        gui->onRunStateChanged(false);
        gui->onRunNotAccepted(H::timestampFailure(QStringLiteral("timeout"), 0));
        AppToolShellFixture::settle();
        QVERIFY(gui->getTimestampErrorDialog());
        shot(QStringLiteral("434-8518"));
        shotWidget(gui->getTimestampErrorDialog(), QStringLiteral("434-8518-dialog"));
        QTest::mouseClick(gui->getTimestampErrorDialog()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();

        // 434:9276: a real run (a visible signature, no authority): the dialog of a file
        QVERIFY(shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE)).isEmpty());
        gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ curve, line, text }));
        H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
        QVERIFY(H::waitForCheck(gui));
        QVERIFY(gui->isConfirmEnabled());
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
        shot(QStringLiteral("434-9276"));
        shotWidget(shell->getResultDialog(), QStringLiteral("434-9276-dialog"));
        shell->getResultDialog()->confirm();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getResultDialog(); }, 30000));
        AppToolShellFixture::settle();

        // 435:7037: Add Timestamp over the signed copy (opened in a new tab), 435:7693, 435:8117
        pdf::PDFAppDocumentTab* signedTab = nullptr;
        QVERIFY(AppToolShellFixture::waitFor([&fixture, &signedTab]()
        {
            for (pdf::PDFAppDocumentTab* candidate : fixture.window->getDocumentTabs())
            {
                if (candidate->getDocument() && candidate->getDocument()->isReady() && candidate->getDocument()->getTitle().contains(QStringLiteral("_SIGNED")))
                {
                    signedTab = candidate;
                    return true;
                }
            }
            return false;
        }, 30000));
        fixture.window->activateTab(signedTab);
        AppToolShellFixture::settle();
        PDFDocumentToolShell* timestampShell = H::openTool(fixture, signedTab, Tool::Mode::Timestamp);
        PDFAppToolGUISignature* timestampGui = H::gui(timestampShell);
        QVERIFY(timestampGui);
        QVERIFY(AppToolShellFixture::waitFor([timestampGui]() { return !timestampGui->getSignatureCards().empty(); }, 30000));
        shot(QStringLiteral("435-7037"));
        timestampGui->getAuthorityPart()->setUrl(QStringLiteral("tsa.acme-corp"));
        shot(QStringLiteral("435-7693"));
        timestampGui->getAuthorityPart()->setUrl(QStringLiteral("http://timestamp.digicert.com"));
        timestampGui->onRunStateChanged(true);
        timestampGui->onRunStateChanged(false);
        timestampGui->onRunNotAccepted(H::timestampFailure(QStringLiteral("http-error"), 503));
        AppToolShellFixture::settle();
        QVERIFY(timestampGui->getTimestampErrorDialog());
        shot(QStringLiteral("435-8117"));
        shotWidget(timestampGui->getTimestampErrorDialog(), QStringLiteral("435-8117-dialog"));
        timestampGui->getTimestampErrorDialog()->reject();
        AppToolShellFixture::settle();

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

void AppToolSignatureTest::runAsksAnyOfPermissions()
{
    // ISO 32000-2, Table 22: the signature of an existing empty field needs bit 9 or bit 6 (and nothing
    // else), a new field bits 4 and 6. The run asks with the alternatives (the rule of the core,
    // PDFDocumentTool::isPermittedRun): a document, which allows neither, offers the owner password (owner
    // 2026-11-08: never only a refusal, no way around it); a document with bit 9 signs without a question
    using Permission = pdf::PDFSecurityHandler::Permission;
    using Permissions = pdf::PDFAppDocumentPermissions;

    AppTestCertificateFolders folders;
    QVERIFY(!AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("jane_novak.pfx"), QStringLiteral("Jane Novak"), H::password(), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));

    // Bits 4, 6 and 9 forbidden (the tool cannot open there; its document is the check of the request)
    pdf::PDFAppDocumentTab* lockedTab = H::openDocument(fixture, H::withField(fixture.folder(), QStringLiteral("Locked.pdf"), H::fillFormsOnly() & ~uint32_t(0x0100u)));
    QVERIFY(lockedTab);
    const pdf::PDFDocument* locked = lockedTab->getDocument()->getDocument().data();
    QVERIFY(!Permissions::isAllowed(locked, Permission::ModifyFormFields));
    QVERIFY(!Permissions::isAllowed(locked, Permission::ModifyInteractiveItems));

    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture, H::withField(fixture.folder(), QStringLiteral("Form.pdf"), H::fillFormsOnly()));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = H::openTool(fixture, tab, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!shell->isPermissionRefused());
    const pdf::PDFDocument* form = tab->getDocument()->getDocument().data();

    // A new field: bits 4 and 6 are asked
    std::optional<pdf::PDFAppPermissionRequest> request = shell->getRunPermissionRequest();
    QVERIFY(request);
    QVERIFY(!Permissions::isAllowed(form, *request));

    // The empty field: bit 9 or bit 6 are asked, bit 9 of the document allows it
    QTest::mouseClick(gui->getAppearanceRow(QStringLiteral("Approval")), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FIELD)).toString(), QStringLiteral("Approval"));
    request = shell->getRunPermissionRequest();
    QVERIFY(request);
    QVERIFY(Permissions::isAllowed(form, *request));

    // The same values over the document without bits 6 and 9: missing is "filling in forms" (bit 9, the
    // first alternative) and the question is the owner password dialog, not the refusal of the core
    QCOMPARE(Permissions::getMissingPermissions(locked, *request), std::vector<Permission>({ Permission::ModifyFormFields }));
    QDialog* dialog = nullptr;
    QVERIFY(!Permissions::request(lockedTab->getDocument(), *request, nullptr, &dialog));
    QPointer<QDialog> ownerDialog = dialog;
    QVERIFY(qobject_cast<pdf::PDFAppPasswordDialog*>(ownerDialog.data()));
    ownerDialog->reject();
    AppToolShellFixture::settle();

    // The run of the field (its drawing fits the field, Q6.3): no question, the signed copy
    pdf::PDFPageDrawing::Element text;
    text.kind = pdf::PDFPageDrawing::Kind::Text;
    text.rectangle = QRectF(100, 100, 90, 18);
    text.text = QStringLiteral("Jane Novak");
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ text }));
    AppToolShellFixture::settle();
    H::typeText(gui->getPasswordCard()->getPasswordField(), H::password());
    QVERIFY(H::waitForCheck(gui));
    QVERIFY(gui->isConfirmEnabled());
    shell->confirm();
    QVERIFY(!shell->getRunPermissionDialog());
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
    QVERIFY(!shell->getRunPermissionDialog());
    // The FileOnly result closes the tool: its page with the shell is deleted later
    const QPointer<PDFDocumentToolShell> guardedShell = shell;
    shell->getResultDialog()->confirm();
    QVERIFY(AppToolShellFixture::waitFor([guardedShell]() { return !guardedShell || !guardedShell->getResultDialog(); }, 30000));
}

void AppToolSignatureTest::authorityTrust_data()
{
    QTest::addColumn<bool>("isUserTrusted");
    QTest::addColumn<bool>("isSystemTrusted");
    QTest::addColumn<bool>("isSystemStoreUsed");
    QTest::addColumn<bool>("isTrusted");

    QTest::newRow("untrusted") << false << false << true << false;
    QTest::newRow("user") << true << false << false << true;
    QTest::newRow("system") << false << true << true << true;
    QTest::newRow("system-store-off") << false << true << false << false;
}

void AppToolSignatureTest::authorityTrust()
{
    // Add Timestamp against a local authority (the loopback, never the internet): the authority is trusted
    // by the trust of the Signatures panel - the trusted certificates of the user and, with the setting
    // signatures.useSystemStore, the system store (here a store of the test, never the one of the machine);
    // the fact Authority of the result says "· Authority not trusted" otherwise (the rule of the core)
    QFETCH(bool, isUserTrusted);
    QFETCH(bool, isSystemTrusted);
    QFETCH(bool, isSystemStoreUsed);
    QFETCH(bool, isTrusted);

    SignatureTestAuthority authority;
    QVERIFY(authority.isListening());
    H::systemCertificate() = isSystemTrusted ? authority.getCertificate() : QByteArray();

    AppToolShellFixture fixture(QSize(1440, 960));
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::signaturesUseSystemStore(), isSystemStoreUsed).isError());
    if (isUserTrusted)
    {
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(authority.getCertificate()).isError());
    }

    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Timestamp);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    gui->getAuthorityPart()->setUrl(authority.getUrl());
    AppToolShellFixture::settle();
    QVERIFY(gui->isConfirmEnabled());
    shell->confirm();
    const bool isFinished = AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000);
    H::systemCertificate().clear();
    QVERIFY(isFinished);
    QVERIFY(authority.getRequestCount() >= 1);

    const QString name = QString::fromLatin1(SignatureTestAuthority::NAME);
    const QStringList facts = H::rows(shell->getResultDialog()->getFacts());
    const QString expected = QStringLiteral("Authority: ") + (isTrusted ? name : name + QStringLiteral(" · Authority not trusted"));
    QVERIFY2(facts.contains(expected), qPrintable(facts.join(QLatin1Char('|'))));
    // The FileOnly result closes the tool: its page with the shell is deleted later
    const QPointer<PDFDocumentToolShell> guardedShell = shell;
    shell->getResultDialog()->confirm();
    QVERIFY(AppToolShellFixture::waitFor([guardedShell]() { return !guardedShell || !guardedShell->getResultDialog(); }, 30000));
}

void AppToolSignatureTest::cardClaimedDate()
{
    // RS6FIGMA L-M1, ISO 32000-2 Table 255: the date of /M, when the CMS has no signing-time, is only claimed
    // by the signer - the card says so; a date of the signed attribute signing-time is shown as it is
    using Result = pdf::PDFSignatureVerificationResult;
    using Verifier = pdf::PDFAppDocumentSignatureVerifier;
    const QDateTime date(QDate(2026, 9, 28), QTime(14, 2), QTimeZone::UTC);
    const QString shown = pdf::PDFAppLocale::formatDateTime(date.toLocalTime());

    const auto signedOn = [](const Result& result)
    {
        for (const auto& detail : Verifier::getCard(result, false, {}).details)
        {
            if (detail.key == QLatin1String("Signed on"))
            {
                return detail.value;
            }
        }
        return QString();
    };

    Result claimed;
    claimed.setSignatureDate(date);
    claimed.setSignatureDateSource(Result::SignatureDateSource::SignatureDictionary);
    QVERIFY(claimed.isSignatureDateClaimed());
    QCOMPARE(signedOn(claimed), shown + QStringLiteral(" · claimed by the signer"));

    Result attribute;
    attribute.setSignatureDate(date);
    attribute.setSignatureDateSource(Result::SignatureDateSource::SigningTimeAttribute);
    QCOMPARE(signedOn(attribute), shown);

    // A claimed date beside an embedded timestamp stays claimed (the timestamp has its own row)
    Result timestamped = claimed;
    timestamped.setTimestampDate(date.addSecs(60));
    QCOMPARE(signedOn(timestamped), shown + QStringLiteral(" · claimed by the signer"));
}

void AppToolSignatureTest::escapeClearsDrawingSelection()
{
    // Owner 2026-11-18: the Esc of the tool (PDFDocumentToolGUI::handleEscape) ends the creation tool
    // of the appearance first, then it deselects the selected elements (in the scene and in the list);
    // only then the shell leaves the tool. The hint follows the same order.
    AppTestCertificateFolders folders;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppToolDrawingPart* part = gui->getDrawingPart();
    QVERIFY(part);
    pdf::PDFPageDrawing::Element text;
    text.kind = pdf::PDFPageDrawing::Kind::Text;
    text.rectangle = QRectF(100, 100, 90, 18);
    text.text = QStringLiteral("Jane Novak");
    part->setDrawing(pdf::PDFPageDrawing({ text }));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc leaves the tool"));
    QVERIFY(!gui->handleEscape());

    part->selectElements({ part->getElementIds().front() });
    AppToolShellFixture::settle();
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc clears the selection"));
    QVERIFY(gui->handleEscape());
    AppToolShellFixture::settle();
    QVERIFY(part->getSelectedElements().empty());
    QVERIFY(!part->getElementList()->getRows().front()->isChecked());
    QCOMPARE(part->getElementCount(), 1);
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc leaves the tool"));
    QVERIFY(!gui->handleEscape());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
}

void AppToolSignatureTest::leaveAsksAboutDrawing()
{
    // Owner 2026-11-20: Esc / Cancel with a drawing of the appearance asks (the question of Sign by Hand 435:10500);
    // Keep Drawing stays, Discard Drawing closes; without a drawing (none, or cleared) the tool closes at once. A typed
    // password is no work: it is never kept.
    AppTestCertificateFolders folders;
    AppToolShellFixture fixture(QSize(1440, 960));
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, Tool::Mode::Sign);
    PDFAppToolGUISignature* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!gui->hasUnsavedWork());
    pdf::PDFPageDrawing::Element rectangle;
    rectangle.kind = pdf::PDFPageDrawing::Kind::Rectangle;
    rectangle.rectangle = QRectF(100, 100, 80, 30);
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing({ rectangle }));
    AppToolShellFixture::settle();
    QVERIFY(gui->hasUnsavedWork());

    shell->cancel();
    AppToolShellFixture::settle();
    QPointer<QDialog> question = shell->getCloseQuestion();
    QVERIFY(question && question->isVisible());
    QStringList labels;
    for (const QLabel* label : question->findChildren<QLabel*>())
    {
        labels << label->text();
    }
    QVERIFY2(labels.contains(QStringLiteral("Discard the drawing of the appearance?")), qPrintable(labels.join(QLatin1Char('|'))));
    QVERIFY(labels.contains(QStringLiteral("Closing the tool removes the element you have drawn for the appearance of the signature. The document stays as it was.")));
    QPushButton* keep = question->findChild<QPushButton*>(QStringLiteral("toolCloseKeep"));
    QPushButton* discard = question->findChild<QPushButton*>(QStringLiteral("toolCloseDiscard"));
    QVERIFY(keep && discard);
    QCOMPARE(keep->text(), QStringLiteral("Keep Drawing"));
    QCOMPARE(discard->text(), QStringLiteral("Discard Drawing"));
    keep->click();
    AppToolShellFixture::settle();
    QVERIFY(shell);
    QCOMPARE(gui->getDrawingPart()->getElementCount(), 1);

    // Cleared: no question
    gui->getDrawingPart()->setDrawing(pdf::PDFPageDrawing());
    AppToolShellFixture::settle();
    QVERIFY(!gui->hasUnsavedWork());
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(!shell);
}

QTEST_MAIN(AppToolSignatureTest)

#include "tst_apptoolsignaturetest.moc"
