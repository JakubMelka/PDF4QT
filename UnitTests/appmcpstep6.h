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

#ifndef APPMCPSTEP6_H
#define APPMCPSTEP6_H

// The shared helpers of the MCP tests of step 6 of phase 2 (UnitTestsAppMcpProtect, package P6; plan
// 6.5, 8, R6-10, R6-20, Q6.7): documents with an empty signature field, documents signed by the core
// at run time (the certificates are made by the test, never committed, never the store of the
// computer), a local timestamp authority on the loopback (never the internet), the user's list of the
// authorities and the drawings of Sign by Hand.

#include "appmcpstep5.h"

#include "pdfappcontext.h"
#include "pdfdocumenttool_signature.h"
#include "pdfdocumenttoolparameter.h"
#include "pdftoolparameterrepository.h"

#include <QBuffer>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

namespace pdftest
{

/// A timestamp authority of a test on the loopback (S6.6: never the internet in a test): it counts the
/// connections, keeps the requests and answers every request by HTTP 503 (435:8117), so the run fails
/// with the error of the authority after the request reached it
class AppMcpLocalAuthority : public QObject
{
public:
    AppMcpLocalAuthority()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, &AppMcpLocalAuthority::onNewConnection);
    }

    bool isListening() const { return m_server.isListening(); }

    /// The address of the authority ("http://127.0.0.1:<port>/tsr")
    QString getUrl() const { return QStringLiteral("http://127.0.0.1:%1/tsr").arg(m_server.serverPort()); }

    int getConnectionCount() const { return m_connectionCount; }
    const QByteArray& getRequests() const { return m_requests; }

private:
    void onNewConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
        {
            ++m_connectionCount;
            socket->setParent(this);
            connect(socket, &QTcpSocket::readyRead, this, &AppMcpLocalAuthority::onReadyRead);
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

        m_requests += data;
        m_pending.remove(socket);
        socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    int m_connectionCount = 0;
    QByteArray m_requests;
    QHash<QTcpSocket*, QByteArray> m_pending;
};

/// Documents, signatures and drawings of the MCP actions of step 6
class AppMcpStep6
{
public:
    AppMcpStep6() = delete;

    using Signature = pdf::PDFDocumentToolSignature;

    /// A certificate of the user in the folder of the application (made at run time), whose .pfx file
    /// opens by the canary password of AppMcpStep5; its path, empty on a failure
    static QString createCertificate(const AppTestCertificateFolders& folders, const QString& fileName, const QString& name)
    {
        return AppTestCertificateFolders::create(folders.getApplicationFolder(), fileName, name, AppMcpStep5::getCanary(), 2048);
    }

    /// The SHA-256 fingerprint of the certificate of a .pfx file of the canary password
    static QString getFingerprint(const QString& pfxPath)
    {
        QFile file(pfxPath);
        if (!file.open(QFile::ReadOnly))
        {
            return QString();
        }
        const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(file.readAll(), AppMcpStep5::getCanary());
        return info ? pdf::PDFDocumentToolEncryption::getCertificateFingerprint(info->getCertificateData()) : QString();
    }

    /// The certificate of the .pfx file goes into the certificate cache of the application, as after its
    /// creation in PDF4QT (owner answer Q5.11): its row has its fingerprint without a password
    static bool cacheCertificate(AppMcpFixture& fixture, const QString& pfxPath)
    {
        QFile file(pfxPath);
        if (!file.open(QFile::ReadOnly))
        {
            return false;
        }
        const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(file.readAll(), AppMcpStep5::getCanary());
        if (!info)
        {
            return false;
        }
        pdf::PDFAppToolCertificateProvider(fixture.context.get()).storeCertificate(pfxPath, info->getCertificateData());
        return true;
    }

    /// A document of two pages with an empty signature field "Approval" of an invisible widget on page 1
    /// (ISO 32000-2, 12.7.5.5, Table 233), encrypted by the owner password with \p permissions (Table 22),
    /// when they are not 0
    static QString writeFieldDocument(AppMcpFixture& fixture, const QString& name, uint32_t permissions)
    {
        AppMcpImageDocument document;
        const pdf::PDFObjectReference page = document.addPage("<< >>", "0 0 1 rg 20 20 100 100 re f");
        document.addPage("<< >>", "1 0 0 rg 40 40 80 80 re f");
        const pdf::PDFObjectReference widget = document.builder.addObject(AppMcpImageDocument::parse("<< /Type /Annot /Subtype /Widget /FT /Sig /T (Approval) /Rect [0 0 0 0] /F 132 /P " +
                                                                                                      AppMcpImageDocument::ref(page) + " >>"));
        document.builder.mergeTo(page, AppMcpImageDocument::parse("<< /Annots [" + AppMcpImageDocument::ref(widget) + "] >>"));
        document.builder.mergeTo(document.builder.getCatalogReference(), AppMcpImageDocument::parse("<< /AcroForm << /Fields [" + AppMcpImageDocument::ref(widget) + "] /SigFlags 1 >> >>"));
        if (permissions)
        {
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = AppMcpStep5::getOwnerPassword();
            settings.permissions = permissions;
            document.builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }
        return document.write(fixture.path(name));
    }

    /// Every bit of Table 22 allowed except \p bits (bit 1 = the lowest)
    static uint32_t allowedWithout(std::initializer_list<int> bits)
    {
        uint32_t permissions = 0xFFFFFFFCu;
        for (int bit : bits)
        {
            permissions &= ~(uint32_t(1) << (bit - 1));
        }
        return permissions;
    }

    /// The bytes of a document of two pages (no file)
    static QByteArray createDocumentBytes()
    {
        AppMcpImageDocument document;
        document.addPage("<< >>", "0 0 1 rg 20 20 100 100 re f");
        document.addPage("<< >>", "1 0 0 rg 40 40 80 80 re f");
        const pdf::PDFDocument built = document.builder.build();
        QByteArray data;
        QBuffer buffer(&data);
        return buffer.open(QIODevice::WriteOnly) && pdf::PDFDocumentWriter(nullptr).write(&buffer, &built) ? data : QByteArray();
    }

    /// A certificate with its private key in the memory (the core of the test signs by it)
    static bool createEntry(const QString& name, pdf::PDFCertificateEntry* entry)
    {
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.privateKeyPasword = AppMcpStep5::getCanary();
        info.certCommonName = name;
        info.rsaKeyLength = 2048;
        QString error;
        entry->pkcs12 = pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error);
        const std::optional<pdf::PDFCertificateInfo> certificate = pdf::PDFCertificateManager::readCertificate(entry->pkcs12, AppMcpStep5::getCanary());
        if (!certificate)
        {
            return false;
        }
        entry->info = *certificate;
        return true;
    }

    /// \p data signed invisibly by the core tool as an incremental update, in \p format (Signature::FORMAT_*,
    /// the default of the tool - PAdES - when nullptr); empty on a failure
    static QByteArray sign(const QByteArray& data, const pdf::PDFCertificateEntry& entry, const char* format = nullptr)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        pdf::PDFDocumentPointer document(new pdf::PDFDocument(reader.readFromBuffer(data)));
        pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, data, QStringLiteral("contract.pdf"), false));
        context.userCertificates = { entry };
        QVariantMap parameters = { { QString::fromLatin1(Signature::PARAMETER_CERTIFICATE), pdf::PDFDocumentToolEncryption::getCertificateFingerprint(entry.info.getCertificateData()) },
                                   { QString::fromLatin1(Signature::PARAMETER_CERTIFICATE_PASSWORD), AppMcpStep5::getCanary() },
                                   { QString::fromLatin1(Signature::PARAMETER_APPEARANCE), QString::fromLatin1(Signature::APPEARANCE_INVISIBLE) } };
        if (format)
        {
            parameters.insert(QString::fromLatin1(Signature::PARAMETER_FORMAT), QString::fromLatin1(format));
        }
        const pdf::PDFDocumentToolResult result = Signature(Signature::Mode::Sign).run(context, parameters, nullptr, nullptr);
        return result.status == pdf::PDFDocumentToolResultStatus::Success && !result.documents.empty() ? result.documents.front().writtenData : QByteArray();
    }

    /// \p data with an incremental update (ISO 32000-2, 7.5.6), which rotates its first page: a change of
    /// the document after its signatures (R6-3 - no signature, timestamp nor validation data); empty on a
    /// failure
    static QByteArray appendPageChange(const QByteArray& data)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromBuffer(data);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK || document.getCatalog()->getPageCount() == 0)
        {
            return QByteArray();
        }

        pdf::PDFDocumentBuilder builder(&document);
        builder.mergeTo(document.getCatalog()->getPage(0)->getPageReference(), AppMcpImageDocument::parse("<< /Rotate 90 >>"));
        const pdf::PDFDocument changed = builder.build();
        QByteArray result;
        QBuffer buffer(&result);
        return buffer.open(QIODevice::WriteOnly) && pdf::PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, data, &changed) ? result : QByteArray();
    }

    /// Writes \p data into the file of the fixture; its path, empty on a failure
    static QString writeFile(AppMcpFixture& fixture, const QString& name, const QByteArray& data)
    {
        const QString filePath = fixture.path(name);
        QFile file(filePath);
        return !data.isEmpty() && file.open(QFile::WriteOnly) && file.write(data) == data.size() ? filePath : QString();
    }

    /// The authority \p url becomes one the user has used (the last value of tsaUrl of Add Timestamp in
    /// the database, PDFAppToolTimestampAuthorityPart::getUserUrls)
    static bool storeAuthorityUrl(AppMcpFixture& fixture, const QString& url)
    {
        const Signature tool(Signature::Mode::Timestamp);
        const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        const pdf::PDFDocumentToolParameterDefinition* definition = pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(Signature::PARAMETER_TSA_URL));
        pdf::PDFDatabase* database = fixture.context ? fixture.context->getDatabase() : nullptr;
        return definition && database && !pdf::PDFToolParameterRepository(database).setValue(tool.getId(), *definition, url).isError();
    }

    /// The elements of a drawing of Sign by Hand (the JSON of PDFPageDrawing::toJson, Q6.7): the text
    /// "Jane Novak" and a line on page 1, a curve of three points on page 2
    static QString getElements()
    {
        return QStringLiteral(R"([{"kind": "text", "page": 1, "rect": [40, 300, 120, 20], "text": "Jane Novak"},)"
                              R"( {"kind": "line", "page": 1, "points": [[40, 296], [160, 296]]},)"
                              R"( {"kind": "freehand-curve", "page": 2, "points": [[50, 50], [60, 70], [80, 55]], "style": {"penColor": "#1f3a93", "penWidth": 1.5}}])");
    }

    /// The signatures of document.get_signatures of the tab (waits for the verification)
    static QJsonArray getSignatures(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* tab)
    {
        QJsonArray signatures;
        const int timeout = 30000 * AppDocumentFixture::getTimeoutFactor();
        const bool isVerified = QTest::qWaitFor([&fixture, tab, &signatures]()
        {
            const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("document"), QStringLiteral("get_signatures"), QJsonObject(),
                                                                                  QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() } }));
            signatures = result && !result->isError ? AppMcpStep4::content(*result, QStringLiteral("get_signatures")).value(QStringLiteral("signatures")).toArray() : QJsonArray();
            return !signatures.isEmpty() && signatures.at(0).toObject().value(QStringLiteral("status")).toString() != QLatin1String("pending");
        }, timeout);

        // Not verified in time: the last answer (the test fails on its values)
        Q_UNUSED(isVerified);
        return signatures;
    }
};

}   // namespace pdftest

#endif // APPMCPSTEP6_H
