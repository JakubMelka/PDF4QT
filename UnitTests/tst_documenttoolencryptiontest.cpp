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

// The tools of the encryption of the core (step 5 of phase 2, plan 9 with R5-13; package P1b
// implements the runs and their tests). The skeleton checks the fixed interface: the ids and the
// modes, the parameters of plan 4.1 with chapters 12 and 13, the Secrets outside of MCP (S5.2,
// Q5.6), the presets of Restrict Permissions (432:2698) and the fingerprint of a certificate.

// Package P1b adds the runs (plan 4.1, 4.4, 4.5, R5-1, R5-3, R5-5, R5-6, R5-10, B85, B86, B98, B99,
// B101, B103, B107, Q5.1, Q5.3 b, Q5.5): every expected value comes of ISO 32000-2 or of the frames
// of Figma; the certificates are created by the test into its own folders (never committed).

#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfcatalog.h"
#include "pdfcertificatemanager.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfstreamfilters.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

#include <map>
#include <memory>
#include <set>

class DocumentToolEncryptionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void modes();
    void parametersOfModes_data();
    void parametersOfModes();
    void secretsNeverInMcp();
    void presets();
    void permissionBits();
    void certificateFingerprint();

    void optionDescriptions();
    void encryptRoundTrip_data();
    void encryptRoundTrip();
    void existingIdStays();
    void extensionsOfCatalogKept();
    void removeProtection();
    void reencryptLeavesNoOldEncrypt();
    void ownerAuthorizationRequired();
    void emptyOwnerPasswordIsOpenPassword();
    void keptRestrictionsNeedOwnerPassword();
    void permissionsValidation();
    void permissionBitsWritten();
    void cryptFilterRemoved();
    void embeddedFilesOfEveryFileSpecification();
    void pdfaRemovedPdfuaKept();
    void conformanceRemovalXml_data();
    void conformanceRemovalXml();
    void attachmentsOnlyCheckSums();
    void attachmentsOnlyWithoutAttachments();
    void secretsNeverInResult();
    void certificateManagerFiles();
    void certificateManagerCreate();
    void certificateRoundTrip_data();
    void certificateRoundTrip();
    void certificateValidation();
    void certificateOfSystemStore();

private:
    std::unique_ptr<QTemporaryDir> m_folder;    ///< The certificate folders of the test
};

/// Helpers of the test
class DocumentToolEncryptionTestHelper
{
public:
    using Mode = pdf::PDFDocumentToolEncryption::Mode;

    static std::vector<Mode> getModes()
    {
        return { Mode::Password, Mode::Permissions, Mode::Certificate, Mode::Unlock };
    }

    static QStringList getNames(const pdf::PDFDocumentToolParameterDefinitions& definitions)
    {
        QStringList names;
        for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
        {
            names << definition.getName();
        }
        return names;
    }
};

/// A file of PDF written by hand: the version of its header and the entries of its trailer are
/// chosen by the test (no /ID unless the test gives it)
class EncryptionTestPdf
{
public:
    void add(int number, const QByteArray& body)
    {
        m_objects[number] = body;
    }

    void addStream(int number, const QByteArray& entries, const QByteArray& data)
    {
        m_objects[number] = "<< " + entries + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    }

    QByteArray build(int root, const QByteArray& version, const QByteArray& trailerEntries) const
    {
        QByteArray data = "%PDF-" + version + "\n%\xE2\xE3\xCF\xD3\n";
        std::map<int, qint64> offsets;
        int maximum = 0;
        for (const auto& [number, body] : m_objects)
        {
            offsets[number] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
            maximum = qMax(maximum, number);
        }

        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(maximum + 1) + "\n0000000000 65535 f \n";
        for (int number = 1; number <= maximum; ++number)
        {
            const auto it = offsets.find(number);
            data += it != offsets.end() ? QByteArray::number(it->second).rightJustified(10, '0') + " 00000 n \n" : QByteArray("0000000000 00000 f \n");
        }
        data += "trailer\n<< /Size " + QByteArray::number(maximum + 1) + " /Root " + QByteArray::number(root) + " 0 R " + trailerEntries + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// Helpers of the runs
class EncryptionTestHelper
{
public:
    using Tool = pdf::PDFDocumentToolEncryption;
    using Mode = Tool::Mode;
    using Permission = pdf::PDFSecurityHandler::Permission;
    using Authorization = pdf::PDFSecurityHandler::AuthorizationResult;

    static constexpr const char* OPEN_PASSWORD = "Open-Pw-1";
    static constexpr const char* OWNER_PASSWORD = "Owner-Pw-2";
    static constexpr const char* CERTIFICATE_PASSWORD = "Heslo-\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD";   ///< UTF-8 (B84)
    static constexpr const char* PAGE_CONTENT = "BT /F1 12 Tf 72 700 Td (MarkerPage) Tj ET";

    static QString getCertificatePassword() { return QString::fromUtf8(CERTIFICATE_PASSWORD); }

    /// A page (its text MarkerPage), an XMP packet, an information dictionary and an attachment
    /// of the name tree EmbeddedFiles, whose stream has no /Type (Table 44 allows it, R5-3)
    static void addSampleObjects(EncryptionTestPdf& pdf, const QByteArray& xmp)
    {
        pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R /Names << /EmbeddedFiles << /Names [(a.txt) 6 0 R] >> >> >>");
        pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 8 0 R >> >> >>");
        pdf.addStream(4, "/Type /Metadata /Subtype /XML", xmp);
        pdf.addStream(5, "", PAGE_CONTENT);
        pdf.add(6, "<< /Type /Filespec /F (a.txt) /EF << /F 7 0 R >> >>");
        pdf.addStream(7, "", "MarkerAttachment payload");
        pdf.add(8, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        pdf.add(9, "<< /Title (MarkerTitle) >>");
    }

    static QByteArray getSampleXmp()
    {
        return "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">MarkerXmp</x:xmpmeta>";
    }

    static QByteArray createSample(const QByteArray& version = "1.4", const QByteArray& trailerEntries = "/Info 9 0 R")
    {
        EncryptionTestPdf pdf;
        addSampleObjects(pdf, getSampleXmp());
        return pdf.build(1, version, trailerEntries);
    }

    /// Reads a file by one password; \p authorization the result of its handler
    static pdf::PDFDocument read(const QByteArray& data, const QString& password, bool isOwnerOnly, bool* isRead = nullptr, Authorization* authorization = nullptr,
                                 const pdf::PDFCertificateEntries* certificates = nullptr)
    {
        bool isFirstAttempt = true;
        auto getPassword = [&password, &isFirstAttempt](bool* ok)
        {
            *ok = isFirstAttempt;
            isFirstAttempt = false;
            return password;
        };

        pdf::PDFDocumentReader reader(nullptr, getPassword, false, isOwnerOnly);
        if (certificates)
        {
            reader.setCertificates(*certificates);
        }
        pdf::PDFDocument document = reader.readFromBuffer(data);
        const bool isOk = reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK;
        if (isRead)
        {
            *isRead = isOk;
        }
        if (authorization)
        {
            const pdf::PDFSecurityHandler* handler = isOk ? document.getStorage().getSecurityHandler() : nullptr;
            *authorization = handler ? handler->getAuthorizationResult() : Authorization::Failed;
        }
        return isOk ? document : pdf::PDFDocument();
    }

    static QByteArray write(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        const pdf::PDFOperationResult result = writer.write(&buffer, &document);
        return result ? data : QByteArray();
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, const QByteArray& data)
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data, QStringLiteral("Project_Report.pdf"), false));
    }

    static pdf::PDFDocumentToolResult run(Mode mode, const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters)
    {
        const Tool tool(mode);
        return tool.run(context, parameters, nullptr, nullptr);
    }

    /// The file of a successful result, empty otherwise
    static QByteArray getResultData(const pdf::PDFDocumentToolResult& result)
    {
        if (!result.isSuccess() || result.documents.size() != 1 || !result.documents.front().document)
        {
            return QByteArray();
        }
        return write(*result.documents.front().document);
    }

    static QVariantMap getPasswordParameters(const QString& algorithm, const QString& scope)
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("algorithm"), algorithm);
        parameters.insert(QStringLiteral("scope"), scope);
        parameters.insert(QStringLiteral("openPassword"), QString::fromLatin1(OPEN_PASSWORD));
        parameters.insert(QStringLiteral("ownerSameAsOpen"), false);
        parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(OWNER_PASSWORD));
        return parameters;
    }

    /// A sample protected by the passwords of OPEN_PASSWORD and OWNER_PASSWORD
    static QByteArray createProtected(const QString& algorithm)
    {
        const QByteArray data = createSample();
        const pdf::PDFDocument document = read(data, QString(), false);
        return getResultData(run(Mode::Password, createContext(document, data), getPasswordParameters(algorithm, QStringLiteral("everything"))));
    }

    static QByteArray getDecodedObject(const pdf::PDFDocument& document, int number)
    {
        const pdf::PDFObject& object = document.getObjectByReference(pdf::PDFObjectReference(number, 0));
        return object.isStream() ? document.getDecodedStream(object.getStream()) : QByteArray();
    }

    static const pdf::PDFDictionary* getEncryptDictionary(const pdf::PDFDocument& document)
    {
        return document.getDictionaryFromObject(document.getTrailerDictionary()->get("Encrypt"));
    }

    static uint32_t getP(const pdf::PDFDocument& document)
    {
        const pdf::PDFDictionary* encrypt = getEncryptDictionary(document);
        const pdf::PDFObject& value = encrypt ? document.getObject(encrypt->get("P")) : pdf::PDFObject();
        return value.isInt() ? uint32_t(int32_t(value.getInteger())) : 0;
    }

    static QByteArray getStreamType(const pdf::PDFDocument& document, int number)
    {
        const pdf::PDFObject& object = document.getObjectByReference(pdf::PDFObjectReference(number, 0));
        const pdf::PDFObject& type = object.isStream() ? object.getStream()->getDictionary()->get("Type") : pdf::PDFObject();
        return type.isName() ? type.getString() : QByteArray();
    }

    static QString getFact(const pdf::PDFDocumentToolResult& result, const char* id)
    {
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == QLatin1String(id))
            {
                return fact.value;
            }
        }
        return QString();
    }

    static bool hasError(const std::vector<pdf::PDFDocumentToolParameterError>& errors, const QString& parameter, const QString& message = QString())
    {
        for (const pdf::PDFDocumentToolParameterError& error : errors)
        {
            if (error.parameter == parameter && (message.isEmpty() || error.message == message))
            {
                return true;
            }
        }
        return false;
    }

    /// The level of the developer extension /ADBE of the catalog (0 = none)
    static int getAdbeExtensionLevel(const pdf::PDFDocument& document)
    {
        const pdf::PDFDictionary* catalog = document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"));
        const pdf::PDFDictionary* extensions = catalog ? document.getDictionaryFromObject(catalog->get("Extensions")) : nullptr;
        const pdf::PDFDictionary* adbe = extensions ? document.getDictionaryFromObject(extensions->get("ADBE")) : nullptr;
        if (!adbe)
        {
            return 0;
        }
        const pdf::PDFObject& baseVersion = document.getObject(adbe->get("BaseVersion"));
        const pdf::PDFObject& level = document.getObject(adbe->get("ExtensionLevel"));
        return baseVersion.isName() && baseVersion.getString() == "1.7" && level.isInt() ? int(level.getInteger()) : -1;
    }

    /// Creates a self-signed certificate with its private key (the core function of the application)
    static bool createCertificate(const QString& fileName, const QString& password, const QString& commonName, int validityInSeconds = 365 * 24 * 3600)
    {
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = fileName;
        info.privateKeyPasword = password;
        info.certCommonName = commonName;
        info.certOrganization = QStringLiteral("Acme Corp");
        info.rsaKeyLength = 2048;
        info.validityInSeconds = validityInSeconds;
        return pdf::PDFCertificateManager().createCertificate(info);
    }

    /// The entry of a file of the certificate folders (PDFCertificateManager::getCertificates)
    static pdf::PDFCertificateEntry findEntry(const QString& filePath)
    {
        for (const pdf::PDFCertificateEntry& entry : pdf::PDFCertificateManager::getCertificates())
        {
            if (QFileInfo(entry.pkcs12FilePath) == QFileInfo(filePath))
            {
                return entry;
            }
        }
        return pdf::PDFCertificateEntry();
    }

    static void removeCertificateFolders()
    {
        QDir(pdf::PDFCertificateManager::getCertificateDirectory()).removeRecursively();
        for (const QString& directory : pdf::PDFCertificateManager::getOldCertificateDirectories())
        {
            QDir(directory).removeRecursively();
        }
    }

    /// The XML of a packet as XML compares it: the elements and the attributes by their namespaces
    /// and local names, the attributes sorted (their order and the places of the declarations of
    /// the namespaces mean nothing in XML, QDom writes the attributes in the order of a hash), the
    /// text without the white space between the elements, the processing instructions
    static QString canonicalXml(const QByteArray& xml)
    {
        QXmlStreamReader reader(xml);
        QString result;
        while (!reader.atEnd())
        {
            switch (reader.readNext())
            {
                case QXmlStreamReader::StartElement:
                {
                    QStringList attributes;
                    for (const QXmlStreamAttribute& attribute : reader.attributes())
                    {
                        if (attribute.namespaceUri() == QLatin1String("http://www.w3.org/2000/xmlns/") || attribute.qualifiedName().startsWith(QLatin1String("xmlns")))
                        {
                            continue;
                        }
                        attributes << QStringLiteral(" {%1}%2=\"%3\"").arg(attribute.namespaceUri().toString(), attribute.name().toString(), attribute.value().toString());
                    }
                    attributes.sort();
                    result += QStringLiteral("<{%1}%2%3>").arg(reader.namespaceUri().toString(), reader.name().toString(), attributes.join(QString()));
                    break;
                }
                case QXmlStreamReader::EndElement:
                    result += QStringLiteral("</{%1}%2>").arg(reader.namespaceUri().toString(), reader.name().toString());
                    break;
                case QXmlStreamReader::Characters:
                    if (!reader.isWhitespace())
                    {
                        result += reader.text().toString();
                    }
                    break;
                case QXmlStreamReader::ProcessingInstruction:
                    result += QStringLiteral("<?%1 %2?>").arg(reader.processingInstructionTarget().toString(), reader.processingInstructionData().toString());
                    break;
                default:
                    break;
            }
        }
        return reader.hasError() ? QStringLiteral("Error: ") + reader.errorString() : result;
    }
};

void DocumentToolEncryptionTest::modes()
{
    using Helper = DocumentToolEncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // Four tools of the subgroup Encryption of the catalog, one class (plan 4.1)
    const QStringList expectedIds = { QStringLiteral("protect.password"), QStringLiteral("protect.permissions"),
                                      QStringLiteral("protect.certificate-encrypt"), QStringLiteral("protect.unlock") };
    QStringList ids;
    for (const Tool::Mode mode : Helper::getModes())
    {
        const Tool tool(mode);
        ids << tool.getId();
        QCOMPARE(tool.getMode(), mode);
        QVERIFY(Tool::findMode(tool.getId()) == std::optional<Tool::Mode>(mode));

        const pdf::PDFDocumentToolCatalogEntry* entry = tool.getInfo();
        QVERIFY(entry);
        QCOMPARE(entry->getSubgroupId(), QStringLiteral("encryption"));
        QVERIFY(entry->isSecurityChange());

        // B80: only a copy written whole for a signed document; Save as a copy preselected (2.3)
        QCOMPARE(tool.getSignatureImpact(QVariantMap()), pdf::PDFDocumentToolSignatureImpact::LostCopyOnly);
        QCOMPARE(tool.getPreselectedResult(QVariantMap()), pdf::PDFDocumentToolPreselectedResult::Copy);

        QString error;
        QVERIFY2(pdf::PDFDocumentToolParameters::isValid(tool.getParameters(), &error), qPrintable(error));
    }
    QCOMPARE(ids, expectedIds);
    QVERIFY(!Tool::findMode(u"pages.merge"));
    QVERIFY(!Tool::findMode(u""));
}

void DocumentToolEncryptionTest::parametersOfModes_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QStringList>("names");

    using Mode = pdf::PDFDocumentToolEncryption::Mode;
    const QStringList permissions = { QStringLiteral("printLow"), QStringLiteral("printHigh"), QStringLiteral("modify"), QStringLiteral("assemble"),
                                      QStringLiteral("annotate"), QStringLiteral("fillForms"), QStringLiteral("copy") };

    // Plan 4.1: the parameters of each mode (owner answer Q5.1 ownerSameAsOpen, K5.19 keepRestrictions)
    QTest::newRow("password") << int(Mode::Password)
                              << QStringList({ QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("openPassword"), QStringLiteral("ownerSameAsOpen"),
                                               QStringLiteral("ownerPassword"), QStringLiteral("keepRestrictions") });
    QTest::newRow("permissions") << int(Mode::Permissions)
                                 << (QStringList({ QStringLiteral("algorithm"), QStringLiteral("scope"), QStringLiteral("ownerPassword"), QStringLiteral("requireOpenPassword"),
                                                   QStringLiteral("openPassword") }) + permissions);
    QTest::newRow("certificate") << int(Mode::Certificate)
                                 << (QStringList({ QStringLiteral("scope") }) + permissions + QStringList({ QStringLiteral("certificate"), QStringLiteral("certificatePassword") }));
    QTest::newRow("unlock") << int(Mode::Unlock) << QStringList();
}

void DocumentToolEncryptionTest::parametersOfModes()
{
    QFETCH(int, mode);
    QFETCH(QStringList, names);

    const pdf::PDFDocumentToolEncryption tool(static_cast<pdf::PDFDocumentToolEncryption::Mode>(mode));
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QCOMPARE(DocumentToolEncryptionTestHelper::getNames(definitions), names);

    // The defaults: AES 256-bit (6.5: also for MCP), everything encrypted, the open password gives
    // the owner access (Figma 430:1068), the restrictions kept, no open password of Restrict Permissions
    const QVariantMap defaults = pdf::PDFDocumentToolParameters::getDefaultValues(definitions);
    if (names.contains(QStringLiteral("algorithm")))
    {
        QCOMPARE(defaults.value(QStringLiteral("algorithm")).toString(), QStringLiteral("aes-256"));
        const pdf::PDFDocumentToolParameterDefinition* algorithm = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("algorithm"));
        QStringList options;
        for (const pdf::PDFDocumentToolParameterOption& option : algorithm->getOptions())
        {
            options << option.id;
        }
        QCOMPARE(options, QStringList({ QStringLiteral("aes-256"), QStringLiteral("aes-128"), QStringLiteral("rc4") }));
    }
    if (names.contains(QStringLiteral("scope")))
    {
        QCOMPARE(defaults.value(QStringLiteral("scope")).toString(), QStringLiteral("everything"));
        const pdf::PDFDocumentToolParameterDefinition* scope = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("scope"));
        QStringList options;
        for (const pdf::PDFDocumentToolParameterOption& option : scope->getOptions())
        {
            options << option.id;
        }
        QCOMPARE(options, QStringList({ QStringLiteral("everything"), QStringLiteral("except-metadata"), QStringLiteral("attachments") }));
    }
    if (names.contains(QStringLiteral("ownerSameAsOpen")))
    {
        QCOMPARE(defaults.value(QStringLiteral("ownerSameAsOpen")).toBool(), true);
        QCOMPARE(defaults.value(QStringLiteral("keepRestrictions")).toBool(), true);
    }
    if (names.contains(QStringLiteral("requireOpenPassword")))
    {
        QCOMPARE(defaults.value(QStringLiteral("requireOpenPassword")).toBool(), false);
    }

    // No Secret is required by the definitions: the model never holds a password, the
    // validation of the tool reports an empty needed password (S5.2)
    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        if (definition.getType() == pdf::PDFDocumentToolParameterType::Secret)
        {
            QVERIFY2(!definition.isRequired(), qPrintable(definition.getName()));
        }
    }
}

void DocumentToolEncryptionTest::secretsNeverInMcp()
{
    using Helper = DocumentToolEncryptionTestHelper;

    // Plan 6.3, owner answer Q5.6: the passwords and the certificate are typed only in PDF4QT -
    // never in the schema of an action, never in a JSON of the values, never stored
    const QString canary = QStringLiteral("Canary-7f3a");
    for (const auto mode : Helper::getModes())
    {
        const pdf::PDFDocumentToolEncryption tool(mode);
        const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject();

        QVariantMap values = pdf::PDFDocumentToolParameters::getDefaultValues(definitions);
        for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
        {
            const bool isSecret = definition.getType() == pdf::PDFDocumentToolParameterType::Secret;
            const bool isExcluded = isSecret || definition.getName() == QLatin1String("certificate");
            QCOMPARE(definition.isMcpExcluded(), isExcluded);
            QCOMPARE(properties.contains(definition.getName()), !isExcluded);
            if (isSecret)
            {
                QVERIFY(!definition.isStored());
                values.insert(definition.getName(), canary);
            }
            if (definition.getName() == QLatin1String("certificate"))
            {
                values.insert(definition.getName(), canary);
            }
        }

        const QByteArray json = QJsonDocument(pdf::PDFDocumentToolParameters::toJson(definitions, values)).toJson();
        QVERIFY2(!json.contains(canary.toUtf8()), json.constData());
    }
}

void DocumentToolEncryptionTest::presets()
{
    using Tool = pdf::PDFDocumentToolEncryption;
    using Preset = Tool::Preset;

    // 432:2698: View only 1 of 8, View and print 3 of 8 (432:2105), Print and fill forms 4 of 8
    // (432:3072), Allow everything 8 of 8 (432:4069) - the accessibility is always allowed (K5.7)
    // and is no parameter
    const std::vector<std::pair<Preset, QStringList>> expected =
    {
        { Preset::ViewOnly, QStringList() },
        { Preset::ViewAndPrint, QStringList({ QStringLiteral("printLow"), QStringLiteral("printHigh") }) },
        { Preset::PrintAndFillForms, QStringList({ QStringLiteral("printLow"), QStringLiteral("printHigh"), QStringLiteral("fillForms") }) },
        { Preset::AllowEverything, QStringList({ QStringLiteral("printLow"), QStringLiteral("printHigh"), QStringLiteral("modify"), QStringLiteral("assemble"),
                                                 QStringLiteral("annotate"), QStringLiteral("fillForms"), QStringLiteral("copy") }) },
    };
    for (const auto& item : expected)
    {
        const QVariantMap values = Tool::getPresetValues(item.first);
        QCOMPARE(values.size(), qsizetype(7));
        QStringList allowed;
        for (const auto& parameter : Tool::getPermissionParameters())
        {
            if (values.value(QString::fromLatin1(parameter.first)).toBool())
            {
                allowed << QString::fromLatin1(parameter.first);
            }
        }
        QCOMPARE(allowed, item.second);
        QCOMPARE(Tool::findPreset(values), item.first);
    }

    // A changed list is Custom; Custom gives every permission off (= View only)
    QVariantMap custom = Tool::getPresetValues(Preset::ViewAndPrint);
    custom.insert(QStringLiteral("copy"), true);
    QCOMPARE(Tool::findPreset(custom), Preset::Custom);
    QCOMPARE(Tool::findPreset(Tool::getPresetValues(Preset::Custom)), Preset::ViewOnly);

    // The defaults: Restrict Permissions starts with View and print (432:2105), a certificate
    // allows everything (433:3049, the collapsed card "Everything is allowed")
    const Tool permissions(Tool::Mode::Permissions);
    QCOMPARE(Tool::findPreset(pdf::PDFDocumentToolParameters::getDefaultValues(permissions.getParameters())), Preset::ViewAndPrint);
    const Tool certificate(Tool::Mode::Certificate);
    QCOMPARE(Tool::findPreset(pdf::PDFDocumentToolParameters::getDefaultValues(certificate.getParameters())), Preset::AllowEverything);
}

void DocumentToolEncryptionTest::permissionBits()
{
    using Tool = pdf::PDFDocumentToolEncryption;
    using Permission = pdf::PDFSecurityHandler::Permission;

    // ISO 32000-2, Table 22: bit 3 print, 12 high-quality print, 4 modify, 11 assemble, 6 annotations
    // and fields, 9 fill in forms, 5 copy, 10 accessibility (bit n = 1 << (n - 1))
    const std::vector<std::pair<QString, uint32_t>> expected =
    {
        { QStringLiteral("printLow"), 1u << 2 },
        { QStringLiteral("printHigh"), 1u << 11 },
        { QStringLiteral("modify"), 1u << 3 },
        { QStringLiteral("assemble"), 1u << 10 },
        { QStringLiteral("annotate"), 1u << 5 },
        { QStringLiteral("fillForms"), 1u << 8 },
        { QStringLiteral("copy"), 1u << 4 },
    };
    const auto parameters = Tool::getPermissionParameters();
    QCOMPARE(parameters.size(), expected.size());
    for (size_t i = 0; i < parameters.size(); ++i)
    {
        QCOMPARE(QString::fromLatin1(parameters[i].first), expected[i].first);
        QCOMPARE(uint32_t(parameters[i].second), expected[i].second);
    }

    // The ids of the data of MCP: the seven and the accessibility, unique, every bit once
    const auto ids = Tool::getPermissionIds();
    QCOMPARE(ids.size(), size_t(8));
    std::set<QString> names;
    uint32_t mask = 0;
    for (const auto& id : ids)
    {
        QVERIFY(names.insert(QString::fromLatin1(id.first)).second);
        QVERIFY((mask & uint32_t(id.second)) == 0);
        mask |= uint32_t(id.second);
    }
    QCOMPARE(ids.back().second, Permission::Accessibility);
    QCOMPARE(mask, uint32_t((1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11)));
}

void DocumentToolEncryptionTest::certificateFingerprint()
{
    // SHA-256 in lower case hexadecimal digits: the test vectors of FIPS 180-2 (Appendix B.1, "abc")
    // and of the empty message
    using Tool = pdf::PDFDocumentToolEncryption;
    QCOMPARE(Tool::getCertificateFingerprint(QByteArray("abc")), QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    QCOMPARE(Tool::getCertificateFingerprint(QByteArray()), QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void DocumentToolEncryptionTest::initTestCase()
{
    // The certificate folders of the test under its temporary folder (the owner rule: nothing of a
    // test on the system disk; the runners give TEMP under build/work): the folder of the
    // application and, as its siblings, the folders of the old Editor and Viewer (B97 / Q5.5)
    m_folder = std::make_unique<QTemporaryDir>(QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("pdf4qt-encryption-XXXXXX")));
    QVERIFY(m_folder->isValid());
    const QDir folder(m_folder->path());
    pdf::PDFCertificateManager::setCertificateDirectories(folder.absoluteFilePath(QStringLiteral("PDF4QT/certificates")),
                                                          { folder.absoluteFilePath(QStringLiteral("PDF4QT Editor/certificates")),
                                                            folder.absoluteFilePath(QStringLiteral("PDF4QT Viewer/certificates")) });
}

void DocumentToolEncryptionTest::cleanupTestCase()
{
    pdf::PDFCertificateManager::setCertificateDirectories(QString(), QStringList());
    m_folder.reset();
}

void DocumentToolEncryptionTest::optionDescriptions()
{
    using Tool = pdf::PDFDocumentToolEncryption;

    // The menus Algorithm and Encrypt of 430:1497 and Encrypt of 433:3480
    auto getDescriptions = [](Tool::Mode mode, const QString& name)
    {
        const Tool tool(mode);
        const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
        QStringList descriptions;
        for (const pdf::PDFDocumentToolParameterOption& option : pdf::PDFDocumentToolParameters::find(definitions, name)->getOptions())
        {
            descriptions << option.getTitle() + QStringLiteral(" / ") + option.getDescription();
        }
        return descriptions;
    };

    const QStringList algorithms = { QStringLiteral("AES 256-bit / Very strong · recommended"),
                                     QStringLiteral("AES 128-bit / Strong · for readers older than 2010"),
                                     QStringLiteral("RC4 128-bit / Weak · only for very old readers") };
    QCOMPARE(getDescriptions(Tool::Mode::Password, QStringLiteral("algorithm")), algorithms);
    QCOMPARE(getDescriptions(Tool::Mode::Permissions, QStringLiteral("algorithm")), algorithms);

    const QStringList passwordScopes = { QStringLiteral("Everything / All contents including document metadata"),
                                         QStringLiteral("Everything except metadata / Only the XMP metadata stays readable for search engines"),
                                         QStringLiteral("File attachments only / The document opens freely, attachments need the password") };
    QCOMPARE(getDescriptions(Tool::Mode::Password, QStringLiteral("scope")), passwordScopes);
    QCOMPARE(getDescriptions(Tool::Mode::Permissions, QStringLiteral("scope")), passwordScopes);

    QStringList certificateScopes = passwordScopes;
    certificateScopes.back() = QStringLiteral("File attachments only / The document opens freely, attachments need the certificate");
    QCOMPARE(getDescriptions(Tool::Mode::Certificate, QStringLiteral("scope")), certificateScopes);
}

void DocumentToolEncryptionTest::encryptRoundTrip_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<QString>("algorithm");
    QTest::addColumn<QString>("scope");
    QTest::addColumn<QByteArray>("version");
    QTest::addColumn<QByteArray>("expectedVersion");
    QTest::addColumn<int>("expectedExtensionLevel");
    QTest::addColumn<int>("expectedRevision");

    using Mode = pdf::PDFDocumentToolEncryption::Mode;

    // B98, R5-16 (ISO 32000-2, Table 20 / 25, 7.12.3 the extension of Adobe): AESV3 below PDF 2.0
    // is PDF 1.7 with /ADBE level 8, AESV2 PDF 1.6, V 4 with RC4 PDF 1.5; never lowered, never 2.0.
    // Revision 6 for AES 256-bit, 4 for AES 128-bit and RC4 (Table 21)
    QTest::newRow("password-aes256-everything") << int(Mode::Password) << "aes-256" << "everything" << QByteArray("1.4") << QByteArray("1.7") << 8 << 6;
    QTest::newRow("password-aes256-except-metadata") << int(Mode::Password) << "aes-256" << "except-metadata" << QByteArray("1.4") << QByteArray("1.7") << 8 << 6;
    QTest::newRow("password-aes256-attachments") << int(Mode::Password) << "aes-256" << "attachments" << QByteArray("1.4") << QByteArray("1.7") << 8 << 6;
    QTest::newRow("password-aes128-everything") << int(Mode::Password) << "aes-128" << "everything" << QByteArray("1.4") << QByteArray("1.6") << 0 << 4;
    QTest::newRow("password-aes128-except-metadata") << int(Mode::Password) << "aes-128" << "except-metadata" << QByteArray("1.4") << QByteArray("1.6") << 0 << 4;
    QTest::newRow("password-aes128-attachments") << int(Mode::Password) << "aes-128" << "attachments" << QByteArray("1.4") << QByteArray("1.6") << 0 << 4;
    QTest::newRow("password-rc4-everything") << int(Mode::Password) << "rc4" << "everything" << QByteArray("1.4") << QByteArray("1.5") << 0 << 4;
    QTest::newRow("password-rc4-except-metadata") << int(Mode::Password) << "rc4" << "except-metadata" << QByteArray("1.4") << QByteArray("1.5") << 0 << 4;
    QTest::newRow("password-rc4-attachments") << int(Mode::Password) << "rc4" << "attachments" << QByteArray("1.4") << QByteArray("1.5") << 0 << 4;
    QTest::newRow("password-aes256-pdf20") << int(Mode::Password) << "aes-256" << "everything" << QByteArray("2.0") << QByteArray("2.0") << 0 << 6;
    QTest::newRow("password-aes128-pdf17") << int(Mode::Password) << "aes-128" << "everything" << QByteArray("1.7") << QByteArray("1.7") << 0 << 4;
    QTest::newRow("permissions-aes256") << int(Mode::Permissions) << "aes-256" << "everything" << QByteArray("1.4") << QByteArray("1.7") << 8 << 6;
    QTest::newRow("permissions-rc4-attachments") << int(Mode::Permissions) << "rc4" << "attachments" << QByteArray("1.4") << QByteArray("1.5") << 0 << 4;
}

void DocumentToolEncryptionTest::encryptRoundTrip()
{
    QFETCH(int, mode);
    QFETCH(QString, algorithm);
    QFETCH(QString, scope);
    QFETCH(QByteArray, version);
    QFETCH(QByteArray, expectedVersion);
    QFETCH(int, expectedExtensionLevel);
    QFETCH(int, expectedRevision);

    using Helper = EncryptionTestHelper;
    using Authorization = Helper::Authorization;

    const QByteArray input = Helper::createSample(version);
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVERIFY(document.getCatalog());
    QVERIFY(document.getIdPart(0).isEmpty());

    const bool isPermissions = mode == int(Helper::Mode::Permissions);
    const bool hasOpenPassword = !isPermissions || algorithm == QLatin1String("aes-256");
    QVariantMap parameters = Helper::getPasswordParameters(algorithm, scope);
    if (isPermissions)
    {
        parameters = pdf::PDFDocumentToolEncryption::getPresetValues(pdf::PDFDocumentToolEncryption::Preset::ViewAndPrint);
        parameters.insert(QStringLiteral("algorithm"), algorithm);
        parameters.insert(QStringLiteral("scope"), scope);
        parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OWNER_PASSWORD));
        parameters.insert(QStringLiteral("requireOpenPassword"), hasOpenPassword);
        parameters.insert(QStringLiteral("openPassword"), QString::fromLatin1(Helper::OPEN_PASSWORD));
    }

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode(mode), Helper::createContext(document, input), parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.front().suggestedFileName, isPermissions ? QStringLiteral("Project_Report_restricted.pdf") : QStringLiteral("Project_Report_protected.pdf"));

    // RS5SEC L5 (7.6.6: File attachments only has /StmF and /StrF Identity): the document opens
    // without a password, only the attachments need it - never "Open password: Required"
    const bool isAttachmentsScope = scope == QLatin1String("attachments");
    QCOMPARE(result.data.value(QStringLiteral("openPasswordRequired")).toBool(), hasOpenPassword && !isAttachmentsScope);
    const QString openPasswordFact = Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_OPEN_PASSWORD);
    if (hasOpenPassword)
    {
        QCOMPARE(openPasswordFact, isAttachmentsScope ? QStringLiteral("Not required · attachments need the password") : QStringLiteral("Required"));
    }
    else
    {
        QVERIFY(openPasswordFact.isEmpty());
    }
    QVERIFY(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_NO_ATTACHMENTS).isEmpty());

    // The verified file is the file of the result (it is saved as it is): its size is the
    // reported size and it opens by the owner password
    const QByteArray data = result.documents.front().writtenData;
    QVERIFY(!data.isEmpty());
    QCOMPARE(result.data.value(QStringLiteral("sizeAfter")).toInteger(), qint64(data.size()));

    // B98: the header and the extension of Adobe
    QVERIFY2(data.startsWith("%PDF-" + expectedVersion), data.left(8).constData());

    // The owner password gives the owner access
    bool isRead = false;
    Authorization authorization = Authorization::Failed;
    const pdf::PDFDocument owner = Helper::read(data, QString::fromLatin1(Helper::OWNER_PASSWORD), true, &isRead, &authorization);
    QVERIFY(isRead);
    QCOMPARE(authorization, Authorization::OwnerAuthorized);
    const pdf::PDFStandardSecurityHandler* handler = dynamic_cast<const pdf::PDFStandardSecurityHandler*>(owner.getStorage().getSecurityHandler());
    QVERIFY(handler);
    QCOMPARE(handler->getRevision(), expectedRevision);
    QCOMPARE(Helper::getAdbeExtensionLevel(owner), expectedExtensionLevel);

    // B85, R5-5 (Table 15): /ID is written with /Encrypt, a new one of 16 bytes, both parts equal (14.4)
    QCOMPARE(owner.getIdPart(0).size(), qsizetype(16));
    QCOMPARE(owner.getIdPart(1), owner.getIdPart(0));

    // The open password opens the document as a user; without an open password it opens freely
    const QString userPassword = hasOpenPassword ? QString::fromLatin1(Helper::OPEN_PASSWORD) : QString();
    const pdf::PDFDocument user = Helper::read(data, userPassword, false, &isRead, &authorization);
    QVERIFY(isRead);
    QCOMPARE(authorization, Authorization::UserAuthorized);
    if (isPermissions)
    {
        // View and print (432:2105)
        QVERIFY(user.getStorage().getSecurityHandler()->isAllowed(Helper::Permission::PrintHighResolution));
        QVERIFY(!user.getStorage().getSecurityHandler()->isAllowed(Helper::Permission::CopyContent));
        QVERIFY(!user.getStorage().getSecurityHandler()->isAllowed(Helper::Permission::Modify));
    }

    // The content is the same; the attachment is an embedded file now (R5-3)
    QCOMPARE(Helper::getDecodedObject(owner, 5), QByteArray(Helper::PAGE_CONTENT));
    QCOMPARE(Helper::getDecodedObject(owner, 7), QByteArray("MarkerAttachment payload"));
    QCOMPARE(Helper::getStreamType(owner, 7), QByteArray("EmbeddedFile"));

    // 7.6.6: what is encrypted (the strings, the streams, the metadata, the embedded files)
    const bool isEverything = scope == QLatin1String("everything");
    const bool isAttachments = scope == QLatin1String("attachments");
    QCOMPARE(data.contains("MarkerPage"), isAttachments);
    QCOMPARE(data.contains("MarkerTitle"), isAttachments);
    QCOMPARE(data.contains("MarkerXmp"), !isEverything);
    QVERIFY(!data.contains("MarkerAttachment"));
}

void DocumentToolEncryptionTest::existingIdStays()
{
    using Helper = EncryptionTestHelper;

    // ISO 32000-2, 14.4: the first part of /ID is permanent; the key of revision 4 is computed
    // of it (Algorithm 2, step e), so the result opens by its passwords
    const QByteArray id = "<00112233445566778899AABBCCDDEEFF>";
    const QByteArray input = Helper::createSample("1.7", "/Info 9 0 R /ID [" + id + " <FFEEDDCCBBAA99887766554433221100>]");
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QCOMPARE(document.getIdPart(0), QByteArray::fromHex("00112233445566778899AABBCCDDEEFF"));

    for (const QString& algorithm : { QStringLiteral("rc4"), QStringLiteral("aes-128") })
    {
        const QByteArray data = Helper::getResultData(Helper::run(Helper::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(algorithm, QStringLiteral("everything"))));
        QVERIFY(!data.isEmpty());

        bool isRead = false;
        Helper::Authorization authorization = Helper::Authorization::Failed;
        const pdf::PDFDocument user = Helper::read(data, QString::fromLatin1(Helper::OPEN_PASSWORD), false, &isRead, &authorization);
        QVERIFY(isRead);
        QCOMPARE(authorization, Helper::Authorization::UserAuthorized);
        QCOMPARE(user.getIdPart(0), QByteArray::fromHex("00112233445566778899AABBCCDDEEFF"));
        QCOMPARE(user.getIdPart(1), QByteArray::fromHex("FFEEDDCCBBAA99887766554433221100"));
    }
}

void DocumentToolEncryptionTest::extensionsOfCatalogKept()
{
    using Helper = EncryptionTestHelper;

    // B98 (ISO 32000-2, 7.12.3): the extension /ADBE of a higher level and other extensions stay,
    // a lower level of /ADBE is raised to 8 (the levels are cumulative)
    for (const int level : { 3, 11 })
    {
        EncryptionTestPdf pdf;
        Helper::addSampleObjects(pdf, Helper::getSampleXmp());
        pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Extensions 10 0 R >>");
        pdf.add(10, "<< /ADBE << /BaseVersion /1.7 /ExtensionLevel " + QByteArray::number(level) + " >> /XYZW << /BaseVersion /1.7 /ExtensionLevel 2 >> >>");
        const QByteArray input = pdf.build(1, "1.7", QByteArray());
        const pdf::PDFDocument document = Helper::read(input, QString(), false);
        QCOMPARE(Helper::getAdbeExtensionLevel(document), level);
        QCOMPARE(pdf::PDFDocumentWriter::getEncryptionVersion(&document).adbeExtensionLevel, 0);

        const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything")));
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(pdf::PDFDocumentWriter::getEncryptionVersion(result.documents.front().document.data()).adbeExtensionLevel, 8);

        const pdf::PDFDocument owner = Helper::read(Helper::getResultData(result), QString::fromLatin1(Helper::OWNER_PASSWORD), true);
        QVERIFY(owner.getCatalog());
        QCOMPARE(Helper::getAdbeExtensionLevel(owner), qMax(level, 8));
        const pdf::PDFDictionary* catalog = owner.getDictionaryFromObject(owner.getTrailerDictionary()->get("Root"));
        const pdf::PDFDictionary* extensions = owner.getDictionaryFromObject(catalog->get("Extensions"));
        QVERIFY(extensions && extensions->hasKey("XYZW"));
    }
}

void DocumentToolEncryptionTest::removeProtection()
{
    using Helper = EncryptionTestHelper;

    // 433:5671: nothing to remove of a document without encryption
    const QByteArray plain = Helper::createSample();
    const pdf::PDFDocument plainDocument = Helper::read(plain, QString(), false);
    QCOMPARE(Helper::run(Helper::Mode::Unlock, Helper::createContext(plainDocument, plain), QVariantMap()).status, pdf::PDFDocumentToolResultStatus::NothingToDo);

    const QByteArray protectedData = Helper::createProtected(QStringLiteral("aes-256"));
    QVERIFY(protectedData.contains("/Standard"));
    const pdf::PDFDocument owner = Helper::read(protectedData, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Unlock, Helper::createContext(owner, protectedData), QVariantMap());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Project_Report_unprotected.pdf"));

    // 433:5999
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_ENCRYPTION), QStringLiteral("None (was AES 256-bit)"));
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_OPEN_PASSWORD), QStringLiteral("Not required"));
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_PERMISSIONS), QStringLiteral("All allowed"));
    QCOMPARE(result.data.value(QStringLiteral("algorithm")).toString(), QStringLiteral("none"));
    QCOMPARE(result.data.value(QStringLiteral("permissions")).toArray().size(), qsizetype(8));

    // No /Encrypt, no orphan of it (B86: the verification values of the old passwords would be
    // written in the clear), the content in the clear and the same
    const QByteArray data = Helper::getResultData(result);
    QVERIFY(!data.isEmpty());
    QVERIFY(!data.contains("/Encrypt"));
    QVERIFY(!data.contains("/Standard"));
    QVERIFY(data.contains("MarkerPage"));
    QVERIFY(data.contains("MarkerAttachment"));

    bool isRead = false;
    const pdf::PDFDocument unprotected = Helper::read(data, QString(), false, &isRead);
    QVERIFY(isRead);
    QCOMPARE(unprotected.getStorage().getSecurityHandler()->getMode(), pdf::EncryptionMode::None);
    QCOMPARE(Helper::getDecodedObject(unprotected, 5), QByteArray(Helper::PAGE_CONTENT));
    QCOMPARE(unprotected.getIdPart(0), owner.getIdPart(0));
}

void DocumentToolEncryptionTest::reencryptLeavesNoOldEncrypt()
{
    using Helper = EncryptionTestHelper;

    // B86: the old encryption dictionary is no orphan of the new file
    const QByteArray protectedData = Helper::createProtected(QStringLiteral("aes-128"));
    QCOMPARE(protectedData.count("/Standard"), qsizetype(1));
    const pdf::PDFDocument owner = Helper::read(protectedData, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(owner, protectedData), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything")));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray data = Helper::getResultData(result);
    QCOMPARE(data.count("/Standard"), qsizetype(1));
    QVERIFY(!data.contains("/AESV2"));

    bool isRead = false;
    Helper::Authorization authorization = Helper::Authorization::Failed;
    Helper::read(data, QString::fromLatin1(Helper::OWNER_PASSWORD), true, &isRead, &authorization);
    QVERIFY(isRead);
    QCOMPARE(authorization, Helper::Authorization::OwnerAuthorized);
}

void DocumentToolEncryptionTest::ownerAuthorizationRequired()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // ISO 32000-2, 7.6.4.1, plan R5-4: only the owner changes the security
    const QByteArray protectedData = Helper::createProtected(QStringLiteral("aes-256"));
    const pdf::PDFDocument user = Helper::read(protectedData, QString::fromLatin1(Helper::OPEN_PASSWORD), false);
    QVERIFY(user.getCatalog());
    QVERIFY(!Tool::isSecurityChangeAllowed(&user));

    for (const Tool::Mode mode : { Tool::Mode::Password, Tool::Mode::Permissions, Tool::Mode::Unlock })
    {
        const Tool tool(mode);
        const pdf::PDFDocumentToolContext context = Helper::createContext(user, protectedData);
        QVERIFY(Helper::hasError(tool.validate(context, Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything"))), QString()));
        QCOMPARE(Helper::run(mode, context, Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything"))).status, pdf::PDFDocumentToolResultStatus::Failed);
    }

    const pdf::PDFDocument owner = Helper::read(protectedData, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(Tool::isSecurityChangeAllowed(&owner));
    const pdf::PDFDocument plain = Helper::read(Helper::createSample(), QString(), false);
    QVERIFY(Tool::isSecurityChangeAllowed(&plain));
}

void DocumentToolEncryptionTest::emptyOwnerPasswordIsOpenPassword()
{
    using Helper = EncryptionTestHelper;

    // B81, R5-2 (ISO 32000-2, Algorithm 3 a): "Use the open password for owner access too" - the
    // document never opens without a password, the open password is the owner password
    for (const QString& algorithm : { QStringLiteral("rc4"), QStringLiteral("aes-128"), QStringLiteral("aes-256") })
    {
        const QByteArray input = Helper::createSample();
        const pdf::PDFDocument document = Helper::read(input, QString(), false);
        QVariantMap parameters = Helper::getPasswordParameters(algorithm, QStringLiteral("everything"));
        parameters.insert(QStringLiteral("ownerSameAsOpen"), true);

        const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.data.value(QStringLiteral("ownerPasswordSeparate")).toBool(), false);
        QCOMPARE(result.data.value(QStringLiteral("openPasswordRequired")).toBool(), true);
        const QByteArray data = Helper::getResultData(result);

        bool isRead = true;
        Helper::read(data, QString(), false, &isRead);
        QVERIFY2(!isRead, qPrintable(algorithm));

        Helper::Authorization authorization = Helper::Authorization::Failed;
        Helper::read(data, QString::fromLatin1(Helper::OPEN_PASSWORD), true, &isRead, &authorization);
        QVERIFY(isRead);
        QCOMPARE(authorization, Helper::Authorization::OwnerAuthorized);
    }
}

void DocumentToolEncryptionTest::keptRestrictionsNeedOwnerPassword()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // A restricted input (View and print, only the owner password) opened by its owner
    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument plain = Helper::read(input, QString(), false);
    QVariantMap restrict = Tool::getPresetValues(Tool::Preset::ViewAndPrint);
    restrict.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OWNER_PASSWORD));
    const QByteArray restricted = Helper::getResultData(Helper::run(Helper::Mode::Permissions, Helper::createContext(plain, input), restrict));
    QVERIFY(!restricted.isEmpty());
    const pdf::PDFDocument owner = Helper::read(restricted, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());

    // Table 22: printing (bits 3, 12) and the accessibility (bit 10) only
    using Permission = Helper::Permission;
    QCOMPARE(Tool::getDocumentPermissions(&owner), uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::PrintHighResolution) | uint32_t(Permission::Accessibility));

    // Owner answer Q5.1: the kept restrictions need an owner password of their own
    const Tool tool(Tool::Mode::Password);
    const pdf::PDFDocumentToolContext context = Helper::createContext(owner, restricted);
    QVariantMap parameters = Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything"));
    parameters.insert(QStringLiteral("keepRestrictions"), true);
    parameters.insert(QStringLiteral("ownerSameAsOpen"), true);
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("ownerSameAsOpen")));
    QCOMPARE(Helper::run(Helper::Mode::Password, context, parameters).status, pdf::PDFDocumentToolResultStatus::Failed);

    parameters.insert(QStringLiteral("ownerSameAsOpen"), false);
    parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OPEN_PASSWORD));
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("ownerPassword")));

    // K5.19: the bits stay, the old owner password does not
    parameters.insert(QStringLiteral("ownerPassword"), QStringLiteral("New-Owner-3"));
    const pdf::PDFDocumentToolResult kept = Helper::run(Helper::Mode::Password, context, parameters);
    QVERIFY2(kept.isSuccess(), qPrintable(kept.message));
    const QByteArray keptData = Helper::getResultData(kept);
    const pdf::PDFDocument keptUser = Helper::read(keptData, QString::fromLatin1(Helper::OPEN_PASSWORD), false);
    QVERIFY(keptUser.getCatalog());
    QVERIFY(keptUser.getStorage().getSecurityHandler()->isAllowed(Permission::PrintHighResolution));
    QVERIFY(!keptUser.getStorage().getSecurityHandler()->isAllowed(Permission::CopyContent));
    QVERIFY(!keptUser.getStorage().getSecurityHandler()->isAllowed(Permission::Modify));
    bool isRead = true;
    Helper::read(keptData, QString::fromLatin1(Helper::OWNER_PASSWORD), true, &isRead);
    QVERIFY(!isRead);

    // Without the restrictions the open password may be the owner password
    parameters.insert(QStringLiteral("keepRestrictions"), false);
    parameters.insert(QStringLiteral("ownerSameAsOpen"), true);
    const QByteArray freeData = Helper::getResultData(Helper::run(Helper::Mode::Password, context, parameters));
    const pdf::PDFDocument freeUser = Helper::read(freeData, QString::fromLatin1(Helper::OPEN_PASSWORD), false);
    QVERIFY(freeUser.getCatalog());
    QVERIFY(freeUser.getStorage().getSecurityHandler()->isAllowed(Permission::CopyContent));
}

void DocumentToolEncryptionTest::permissionsValidation()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    const pdf::PDFDocumentToolContext context = Helper::createContext(document, input);
    const Tool tool(Tool::Mode::Permissions);

    // The owner password is needed
    QVariantMap parameters = Tool::getPresetValues(Tool::Preset::ViewAndPrint);
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("ownerPassword")));

    // 432:3721: the same open and owner password, both fields
    parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OPEN_PASSWORD));
    parameters.insert(QStringLiteral("requireOpenPassword"), true);
    parameters.insert(QStringLiteral("openPassword"), QString::fromLatin1(Helper::OPEN_PASSWORD));
    const QString text = QStringLiteral("Use a password different from the open password – otherwise every reader gets owner access.");
    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(context, parameters);
    QVERIFY(Helper::hasError(errors, QStringLiteral("ownerPassword"), text));
    QVERIFY(Helper::hasError(errors, QStringLiteral("openPassword"), text));

    // The open password is needed only when it is required
    parameters.insert(QStringLiteral("openPassword"), QString());
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("openPassword")));
    parameters.insert(QStringLiteral("requireOpenPassword"), false);
    QVERIFY(tool.validate(context, parameters).empty());

    // The open password of Protect with Password is needed
    const Tool password(Tool::Mode::Password);
    QVariantMap passwordParameters = Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything"));
    passwordParameters.insert(QStringLiteral("openPassword"), QString());
    QVERIFY(Helper::hasError(password.validate(context, passwordParameters), QStringLiteral("openPassword")));
}

void DocumentToolEncryptionTest::permissionBitsWritten()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // Only "Print in high resolution" and "Edit comments and form fields" checked: K5.8 (Table
    // 22) - bit 12 needs bit 3, bit 6 includes the filling of forms (bit 9); bit 10 always
    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVariantMap parameters = Tool::getPresetValues(Tool::Preset::Custom);
    parameters.insert(QStringLiteral("printHigh"), true);
    parameters.insert(QStringLiteral("annotate"), true);
    parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OWNER_PASSWORD));

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Permissions, Helper::createContext(document, input), parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const pdf::PDFDocument written = Helper::read(Helper::getResultData(result), QString(), false);
    QVERIFY(written.getCatalog());
    const uint32_t p = Helper::getP(written);
    for (int bit : { 3, 6, 9, 10, 12 })
    {
        QVERIFY2(p & (1u << (bit - 1)), qPrintable(QString::number(bit)));
    }
    for (int bit : { 4, 5, 11 })
    {
        QVERIFY2(!(p & (1u << (bit - 1))), qPrintable(QString::number(bit)));
    }

    const QJsonArray ids = result.data.value(QStringLiteral("permissions")).toArray();
    QCOMPARE(ids, QJsonArray({ QStringLiteral("print-low"), QStringLiteral("print-high"), QStringLiteral("annotate"), QStringLiteral("fill-forms"), QStringLiteral("accessibility") }));
    QCOMPARE(result.data.value(QStringLiteral("revision")).toInt(), 6);
    QCOMPARE(result.data.value(QStringLiteral("ownerPasswordSeparate")).toBool(), true);
    QCOMPARE(result.data.value(QStringLiteral("openPasswordRequired")).toBool(), false);

    // 432:4388: the groups of the list
    QCOMPARE(Helper::getFact(result, Tool::FACT_ALLOWED), QStringLiteral("Viewing, printing, editing (partly), forms, screen readers"));
    QCOMPARE(Helper::getFact(result, Tool::FACT_NOT_ALLOWED), QStringLiteral("Copying"));
    QCOMPARE(Helper::getFact(result, Tool::FACT_ENCRYPTION), QStringLiteral("AES 256-bit · owner password set"));

    // View and print of 432:4388
    QVariantMap viewAndPrint = Tool::getPresetValues(Tool::Preset::ViewAndPrint);
    viewAndPrint.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OWNER_PASSWORD));
    const pdf::PDFDocumentToolResult printing = Helper::run(Helper::Mode::Permissions, Helper::createContext(document, input), viewAndPrint);
    QCOMPARE(Helper::getFact(printing, Tool::FACT_ALLOWED), QStringLiteral("Viewing, printing, screen readers"));
    QCOMPARE(Helper::getFact(printing, Tool::FACT_NOT_ALLOWED), QStringLiteral("Editing, forms, copying"));
}

void DocumentToolEncryptionTest::cryptFilterRemoved()
{
    using Helper = EncryptionTestHelper;

    // R5-10 (ISO 32000-2, 7.4.10): a stream of the crypt filter Identity (and FlateDecode after it)
    // and a stream of the crypt filter only - the new /Encrypt knows no such crypt filter
    EncryptionTestPdf pdf;
    Helper::addSampleObjects(pdf, Helper::getSampleXmp());
    const QByteArray compressed = pdf::PDFFlateDecodeFilter::compress(QByteArray("MarkerCrypt payload"));
    pdf.addStream(10, "/Filter [/Crypt /FlateDecode] /DecodeParms [<< /Name /Identity >> null]", compressed);
    pdf.addStream(11, "/Filter /Crypt /DecodeParms << /Type /CryptFilterDecodeParms /Name /Identity >>", "MarkerCryptOnly");
    const QByteArray input = pdf.build(1, "1.7", "/Info 9 0 R");
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QCOMPARE(Helper::getDecodedObject(document, 10), QByteArray("MarkerCrypt payload"));

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything")));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument& output = *result.documents.front().document;

    const pdf::PDFDictionary* first = output.getObjectByReference(pdf::PDFObjectReference(10, 0)).getStream()->getDictionary();
    QVERIFY(first->get("Filter").isName());
    QCOMPARE(first->get("Filter").getString(), QByteArray("FlateDecode"));
    QVERIFY(!first->hasKey("DecodeParms"));
    const pdf::PDFDictionary* second = output.getObjectByReference(pdf::PDFObjectReference(11, 0)).getStream()->getDictionary();
    QVERIFY(!second->hasKey("Filter"));
    QVERIFY(!second->hasKey("DecodeParms"));

    // Read back by the password
    const pdf::PDFDocument owner = Helper::read(Helper::getResultData(result), QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());
    QCOMPARE(Helper::getDecodedObject(owner, 10), QByteArray("MarkerCrypt payload"));
    QCOMPARE(Helper::getDecodedObject(owner, 11), QByteArray("MarkerCryptOnly"));

    // A crypt filter, which the reader could not decrypt (no such filter of the document): the run
    // fails and names the stream - its data would be written as garbage under the new /Encrypt
    EncryptionTestPdf unknown;
    Helper::addSampleObjects(unknown, Helper::getSampleXmp());
    unknown.addStream(12, "/Filter /Crypt /DecodeParms << /Name /SecretFilter >>", "MarkerUnknownCrypt");
    const QByteArray unknownInput = unknown.build(1, "1.7", "/Info 9 0 R");
    const pdf::PDFDocument unknownDocument = Helper::read(unknownInput, QString(), false);
    QVERIFY(unknownDocument.getCatalog());
    const pdf::PDFDocumentToolResult failed = Helper::run(Helper::Mode::Password, Helper::createContext(unknownDocument, unknownInput), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything")));
    QCOMPARE(failed.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(failed.message.contains(QStringLiteral("12")) && failed.message.contains(QStringLiteral("SecretFilter")), qPrintable(failed.message));
}

void DocumentToolEncryptionTest::embeddedFilesOfEveryFileSpecification()
{
    using Helper = EncryptionTestHelper;

    // R5-3 (Table 43: /EF and /RF; 7.6.6: the same crypt filter): the file specifications of the
    // name tree, of /AF (the same node as the name tree), of a FileAttachment annotation (direct),
    // of related files and of two objects, which refer to each other (a cycle); the streams have
    // no /Type or a wrong one, one stream is shared by two specifications
    EncryptionTestPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [(a.txt) 6 0 R] >> >> /AF [6 0 R] /Next 20 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Annots [11 0 R] >>");
    pdf.addStream(5, "", Helper::PAGE_CONTENT);
    pdf.add(6, "<< /Type /Filespec /F (a.txt) /EF << /F 7 0 R /UF 7 0 R >> /RF << /F 12 0 R >> >>");
    pdf.addStream(7, "", "MarkerAttach7");
    pdf.add(11, "<< /Type /Annot /Subtype /FileAttachment /Rect [10 10 30 30] /FS << /Type /Filespec /F (b.txt) /EF << /F 14 0 R /UF 7 0 R >> >> >>");
    pdf.add(12, "[(r.txt) 13 0 R (s.txt) 15 0 R]");
    pdf.addStream(13, "", "MarkerRelated13");
    pdf.addStream(14, "/Type /XObject", "MarkerWrongType14");
    pdf.addStream(15, "/Type /EmbeddedFile", "MarkerRelated15");
    pdf.add(20, "<< /Next 21 0 R /FS << /EF << /F 22 0 R >> >> >>");
    pdf.add(21, "<< /Next 20 0 R >>");
    pdf.addStream(22, "", "MarkerCycle22");
    const QByteArray input = pdf.build(1, "1.7", QByteArray());
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVERIFY(document.getCatalog());

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("attachments")));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument& output = *result.documents.front().document;
    for (int number : { 7, 13, 14, 15, 22 })
    {
        QCOMPARE(Helper::getStreamType(output, number), QByteArray("EmbeddedFile"));
    }
    QCOMPARE(Helper::getStreamType(output, 5), QByteArray());

    // File attachments only: the page in the clear, no attachment in the clear
    const QByteArray data = Helper::getResultData(result);
    QVERIFY(data.contains("MarkerPage"));
    for (const char* marker : { "MarkerAttach7", "MarkerRelated13", "MarkerWrongType14", "MarkerRelated15", "MarkerCycle22" })
    {
        QVERIFY2(!data.contains(marker), marker);
    }

    const pdf::PDFDocument owner = Helper::read(data, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());
    QCOMPARE(Helper::getDecodedObject(owner, 13), QByteArray("MarkerRelated13"));
    QCOMPARE(Helper::getDecodedObject(owner, 22), QByteArray("MarkerCycle22"));
}

void DocumentToolEncryptionTest::pdfaRemovedPdfuaKept()
{
    using Helper = EncryptionTestHelper;

    // R5-6, owner answer Q5.7: PDF/A forbids the encryption (ISO 19005-2, 6.1.3) - its declaration
    // goes; PDF/UA does not (ISO 14289-1, 7.16 - bit 10 is set), its declaration stays
    const QByteArray xmp = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" "
                           "xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" "
                           "pdfaid:part=\"2\" pdfaid:conformance=\"B\" pdfuaid:part=\"1\"/></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    EncryptionTestPdf pdf;
    Helper::addSampleObjects(pdf, xmp);
    const QByteArray input = pdf.build(1, "1.7", "/Info 9 0 R");
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QStringList declarations;
    QVERIFY(pdf::PDFOCRPagePreparer::hasConformanceDeclaration(&document, &declarations));
    declarations.sort();
    QCOMPARE(declarations, QStringList({ QStringLiteral("PDF/A"), QStringLiteral("PDF/UA") }));

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything")));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("conformanceRemoved")).toArray(), QJsonArray({ QStringLiteral("PDF/A") }));
    QVERIFY(!Helper::getFact(result, "conformance").isEmpty());

    const pdf::PDFDocument owner = Helper::read(Helper::getResultData(result), QString::fromLatin1(Helper::OWNER_PASSWORD), true);
    QVERIFY(owner.getCatalog());
    declarations.clear();
    QVERIFY(pdf::PDFOCRPagePreparer::hasConformanceDeclaration(&owner, &declarations));
    QCOMPARE(declarations, QStringList({ QStringLiteral("PDF/UA") }));

    // The packet of the result exactly: only the two properties of PDF/A are gone (pdfaid:part and
    // pdfuaid:part have the same local name on the same element)
    const QByteArray expected = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                                "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" "
                                "xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" pdfuaid:part=\"1\"/></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    const pdf::PDFObject& metadata = owner.getObject(owner.getCatalog()->getMetadata());
    QVERIFY(metadata.isStream());
    QCOMPARE(Helper::canonicalXml(owner.getDecodedStream(metadata.getStream())), Helper::canonicalXml(expected));
}

void DocumentToolEncryptionTest::conformanceRemovalXml_data()
{
    QTest::addColumn<QByteArray>("xmp");
    QTest::addColumn<QStringList>("standards");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<QStringList>("declarations");

    // XMP (ISO 16684-1, 7.9.2.2: a simple property as an attribute or as an element); the properties
    // are found by their namespace (ISO 19005-2 6.6.4, ISO 14289-1 5): pdfaid:part and pdfuaid:part
    // share the local name. Everything else stays: the other attributes, the children, their order.
    const QByteArray begin = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">";
    const QByteArray end = "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    const QByteArray namespaces = " xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"";
    const QByteArray attributes = begin + "<rdf:Description rdf:about=\"\"" + namespaces + " pdfaid:part=\"3\" pdfaid:conformance=\"U\" pdfuaid:part=\"1\" dc:format=\"application/pdf\">"
                                  "<dc:title>MarkerTitle</dc:title></rdf:Description>" + end;
    const QByteArray elements = begin + "<rdf:Description rdf:about=\"\"" + namespaces + "><pdfaid:part>3</pdfaid:part><dc:title>MarkerTitle</dc:title>"
                                "<pdfuaid:part>1</pdfuaid:part><pdfaid:conformance>U</pdfaid:conformance></rdf:Description>" + end;
    const QStringList pdfA = { QStringLiteral("PDF/A") };
    const QStringList pdfUA = { QStringLiteral("PDF/UA") };
    const QStringList both = { QStringLiteral("PDF/A"), QStringLiteral("PDF/UA") };

    QTest::newRow("attributes-pdfa") << attributes << pdfA
                                     << QByteArray(begin + "<rdf:Description rdf:about=\"\"" + namespaces + " pdfuaid:part=\"1\" dc:format=\"application/pdf\"><dc:title>MarkerTitle</dc:title></rdf:Description>" + end)
                                     << pdfA;
    QTest::newRow("attributes-pdfua") << attributes << pdfUA
                                      << QByteArray(begin + "<rdf:Description rdf:about=\"\"" + namespaces + " pdfaid:part=\"3\" pdfaid:conformance=\"U\" dc:format=\"application/pdf\"><dc:title>MarkerTitle</dc:title></rdf:Description>" + end)
                                      << pdfUA;
    QTest::newRow("attributes-both") << attributes << both
                                     << QByteArray(begin + "<rdf:Description rdf:about=\"\"" + namespaces + " dc:format=\"application/pdf\"><dc:title>MarkerTitle</dc:title></rdf:Description>" + end)
                                     << both;
    QTest::newRow("elements-pdfa") << elements << pdfA
                                   << QByteArray(begin + "<rdf:Description rdf:about=\"\"" + namespaces + "><dc:title>MarkerTitle</dc:title><pdfuaid:part>1</pdfuaid:part></rdf:Description>" + end)
                                   << pdfA;
}

void DocumentToolEncryptionTest::conformanceRemovalXml()
{
    QFETCH(QByteArray, xmp);
    QFETCH(QStringList, standards);
    QFETCH(QByteArray, expected);
    QFETCH(QStringList, declarations);

    using Helper = EncryptionTestHelper;

    // R5-6, owner answer Q5.7 (ASan of the fix wave: no attribute is removed from a live element of
    // QDom, which leaked the second attribute of the same local name): the packet without the
    // declarations of the standards asked for, exactly
    QVERIFY(!Helper::canonicalXml(expected).startsWith(QStringLiteral("Error")));
    QStringList found;
    QByteArray withoutDeclarations;
    QVERIFY(pdf::PDFOCRTextLayerWriter::findConformanceDeclarations(xmp, &found, &withoutDeclarations, standards));
    found.sort();
    QCOMPARE(found, declarations);
    QCOMPARE(Helper::canonicalXml(withoutDeclarations), Helper::canonicalXml(expected));
}

void DocumentToolEncryptionTest::attachmentsOnlyCheckSums()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // Owner 2026-11-15 (Table 45: /CheckSum is the MD5 of the file; 7.6.6: File attachments only has
    // /StrF Identity): the checksums of the encrypted attachments are removed - in the parameters of
    // the stream and in parameters of an object of their own; /Size stays. Everything encrypts the
    // strings, the checksums stay there.
    const QByteArray first = "MarkerAttachment payload";
    const QByteArray second = "MarkerSecond payload";
    const QByteArray firstCheckSum = QCryptographicHash::hash(first, QCryptographicHash::Md5);
    const QByteArray secondCheckSum = QCryptographicHash::hash(second, QCryptographicHash::Md5);

    EncryptionTestPdf pdf;
    Helper::addSampleObjects(pdf, Helper::getSampleXmp());
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R /Names << /EmbeddedFiles << /Names [(a.txt) 6 0 R (b.txt) 10 0 R] >> >> >>");
    pdf.addStream(7, "/Params << /Size 24 /CheckSum <" + firstCheckSum.toHex() + "> >>", first);
    pdf.add(10, "<< /Type /Filespec /F (b.txt) /EF << /F 11 0 R >> >>");
    pdf.addStream(11, "/Params 12 0 R", second);
    pdf.add(12, "<< /Size 20 /CheckSum <" + secondCheckSum.toHex() + "> >>");
    const QByteArray input = pdf.build(1, "1.7", "/Info 9 0 R");
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVERIFY(document.getCatalog());

    for (const QString& scope : { QStringLiteral("attachments"), QStringLiteral("everything") })
    {
        const bool isAttachmentsOnly = scope == QLatin1String("attachments");
        const pdf::PDFDocumentToolResult result = Helper::run(Tool::Mode::Password, Helper::createContext(document, input), Helper::getPasswordParameters(QStringLiteral("aes-256"), scope));
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const QByteArray data = result.documents.front().writtenData;
        QVERIFY(!data.isEmpty());
        if (isAttachmentsOnly)
        {
            QVERIFY(!data.contains(firstCheckSum) && !data.contains(secondCheckSum));
            QVERIFY(!data.toLower().contains(firstCheckSum.toHex()) && !data.toLower().contains(secondCheckSum.toHex()));
        }

        const pdf::PDFDocument owner = Helper::read(data, QString::fromLatin1(Helper::OWNER_PASSWORD), true);
        QVERIFY(owner.getCatalog());
        const pdf::PDFDictionary* firstParameters = owner.getDictionaryFromObject(owner.getObjectByReference(pdf::PDFObjectReference(7, 0)).getStream()->getDictionary()->get("Params"));
        const pdf::PDFDictionary* secondParameters = owner.getDictionaryFromObject(owner.getObjectByReference(pdf::PDFObjectReference(11, 0)).getStream()->getDictionary()->get("Params"));
        QVERIFY(firstParameters && secondParameters);
        QCOMPARE(owner.getObject(firstParameters->get("Size")).getInteger(), pdf::PDFInteger(24));
        QCOMPARE(owner.getObject(secondParameters->get("Size")).getInteger(), pdf::PDFInteger(20));
        QCOMPARE(firstParameters->hasKey("CheckSum"), !isAttachmentsOnly);
        QCOMPARE(secondParameters->hasKey("CheckSum"), !isAttachmentsOnly);
        if (!isAttachmentsOnly)
        {
            QCOMPARE(owner.getObject(firstParameters->get("CheckSum")).getString(), firstCheckSum);
            QCOMPARE(owner.getObject(secondParameters->get("CheckSum")).getString(), secondCheckSum);
        }
        QCOMPARE(Helper::getDecodedObject(owner, 11), second);

        // Both attachments are there: no warning of a document without attachments
        QVERIFY(Helper::getFact(result, Tool::FACT_NO_ATTACHMENTS).isEmpty());
    }
}

void DocumentToolEncryptionTest::attachmentsOnlyWithoutAttachments()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;

    // RS5SEC L5 (plan 2.5 and 4.1 name no refusal): File attachments only of a document without
    // attachments runs, its result says, that nothing is protected; another scope says nothing
    EncryptionTestPdf pdf;
    Helper::addSampleObjects(pdf, Helper::getSampleXmp());
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>");
    pdf.add(6, "<< /Type /Filespec /F (a.txt) >>");
    pdf.addStream(7, "", "MarkerNoAttachment");
    const QByteArray input = pdf.build(1, "1.7", "/Info 9 0 R");
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVERIFY(document.getCatalog());

    for (const Tool::Mode mode : { Tool::Mode::Password, Tool::Mode::Permissions })
    {
        QVariantMap parameters = Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("attachments"));
        if (mode == Tool::Mode::Permissions)
        {
            parameters = Tool::getPresetValues(Tool::Preset::ViewAndPrint);
            parameters.insert(QStringLiteral("algorithm"), QStringLiteral("aes-256"));
            parameters.insert(QStringLiteral("scope"), QStringLiteral("attachments"));
            parameters.insert(QStringLiteral("ownerPassword"), QString::fromLatin1(Helper::OWNER_PASSWORD));
            parameters.insert(QStringLiteral("requireOpenPassword"), true);
            parameters.insert(QStringLiteral("openPassword"), QString::fromLatin1(Helper::OPEN_PASSWORD));
        }
        const pdf::PDFDocumentToolResult result = Helper::run(mode, Helper::createContext(document, input), parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(Helper::getFact(result, Tool::FACT_NO_ATTACHMENTS), QStringLiteral("None – the document has no attachments, so nothing is protected"));
        QCOMPARE(Helper::getFact(result, Tool::FACT_OPEN_PASSWORD), QStringLiteral("Not required · attachments need the password"));
        QCOMPARE(result.data.value(QStringLiteral("openPasswordRequired")).toBool(), false);

        parameters.insert(QStringLiteral("scope"), QStringLiteral("everything"));
        const pdf::PDFDocumentToolResult everything = Helper::run(mode, Helper::createContext(document, input), parameters);
        QVERIFY2(everything.isSuccess(), qPrintable(everything.message));
        QVERIFY(Helper::getFact(everything, Tool::FACT_NO_ATTACHMENTS).isEmpty());
        QCOMPARE(Helper::getFact(everything, Tool::FACT_OPEN_PASSWORD), QStringLiteral("Required"));
        QCOMPARE(everything.data.value(QStringLiteral("openPasswordRequired")).toBool(), true);
    }
}

void DocumentToolEncryptionTest::secretsNeverInResult()
{
    using Helper = EncryptionTestHelper;

    // S5.2: no password in the facts, the data, the messages or the JSON of MCP
    const QString open = QStringLiteral("CanaryOpen-5c1d");
    const QString ownerPassword = QStringLiteral("CanaryOwner-9e2f");
    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    QVariantMap parameters = Helper::getPasswordParameters(QStringLiteral("aes-256"), QStringLiteral("everything"));
    parameters.insert(QStringLiteral("openPassword"), open);
    parameters.insert(QStringLiteral("ownerPassword"), ownerPassword);

    const pdf::PDFDocumentToolResult result = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray json = QJsonDocument(result.toJson()).toJson();
    QVERIFY(!json.contains("Canary"));
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        QVERIFY(!fact.value.contains(QStringLiteral("Canary")));
    }

    // 430:3226
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_ENCRYPTION), QStringLiteral("AES 256-bit"));
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_OPEN_PASSWORD), QStringLiteral("Required"));
    QCOMPARE(Helper::getFact(result, pdf::PDFDocumentToolEncryption::FACT_ENCRYPTED), QStringLiteral("Everything, including metadata"));
    QCOMPARE(result.data.value(QStringLiteral("algorithm")).toString(), QStringLiteral("aes-256"));
    QCOMPARE(result.data.value(QStringLiteral("scope")).toString(), QStringLiteral("everything"));

    // A failure says why, without the password (č is no character of PDFDocEncoding, Annex D)
    parameters.insert(QStringLiteral("algorithm"), QStringLiteral("rc4"));
    parameters.insert(QStringLiteral("openPassword"), QStringLiteral("Canaryč"));
    const pdf::PDFDocumentToolResult failed = Helper::run(Helper::Mode::Password, Helper::createContext(document, input), parameters);
    QCOMPARE(failed.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(!QJsonDocument(failed.toJson()).toJson().contains("Canary"));
}

void DocumentToolEncryptionTest::certificateManagerFiles()
{
    using Helper = EncryptionTestHelper;
    using Manager = pdf::PDFCertificateManager;
    Helper::removeCertificateFolders();

    // setCertificateDirectories: the folders of the test (nothing on the system disk); empty gives
    // the folders of the user back
    const QDir folder(m_folder->path());
    const QString directory = Manager::getCertificateDirectory();
    QCOMPARE(directory, folder.absoluteFilePath(QStringLiteral("PDF4QT/certificates")));
    Manager::setCertificateDirectories(QString(), { folder.absoluteFilePath(QStringLiteral("ignored")) });
    QVERIFY(Manager::getCertificateDirectory() != directory);
    QVERIFY(!Manager::getOldCertificateDirectories().contains(folder.absoluteFilePath(QStringLiteral("ignored"))));
    Manager::setCertificateDirectories(directory, { folder.absoluteFilePath(QStringLiteral("PDF4QT Editor/certificates")), folder.absoluteFilePath(QStringLiteral("PDF4QT Viewer/certificates")) });

    // B97, Q5.5: the folders of the old Editor and Viewer of the same organization, never the own one
    const QStringList oldDirectories = Manager::getOldCertificateDirectories();
    QCOMPARE(oldDirectories.size(), qsizetype(2));
    QVERIFY(oldDirectories[0].contains(QStringLiteral("/PDF4QT Editor/")));
    QVERIFY(oldDirectories[1].contains(QStringLiteral("/PDF4QT Viewer/")));
    QVERIFY(!oldDirectories.contains(directory));
    auto getOrganizationFolder = [](const QString& path) { return QFileInfo(QFileInfo(path).absolutePath()).absolutePath(); };
    QCOMPARE(getOrganizationFolder(oldDirectories[0]), getOrganizationFolder(directory));
    QCOMPARE(getOrganizationFolder(oldDirectories[1]), getOrganizationFolder(directory));

    QVERIFY(QDir().mkpath(directory));
    QVERIFY(QDir().mkpath(oldDirectories[0]));
    const QString ownFile = QDir(directory).absoluteFilePath(QStringLiteral("jane_novak.pfx"));
    const QString oldFile = QDir(oldDirectories[0]).absoluteFilePath(QStringLiteral("editor.pfx"));
    QVERIFY(Helper::createCertificate(ownFile, Helper::getCertificatePassword(), QStringLiteral("Jane Novak")));
    QVERIFY(Helper::createCertificate(oldFile, Helper::getCertificatePassword(), QStringLiteral("Old Editor")));

    // Files, which are not PKCS#12 or too large, are skipped
    QFile junk(QDir(directory).absoluteFilePath(QStringLiteral("junk.pfx")));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write(QByteArray(1000, 'x'));
    junk.close();
    QFile ownData(ownFile);
    QVERIFY(ownData.open(QIODevice::ReadOnly));
    const QByteArray pkcs12 = ownData.readAll();
    ownData.close();
    QFile large(QDir(directory).absoluteFilePath(QStringLiteral("large.pfx")));
    QVERIFY(large.open(QIODevice::WriteOnly));
    large.write(pkcs12 + QByteArray(int(Manager::MAXIMUM_CERTIFICATE_FILE_SIZE), '\0'));
    large.close();

    // B103: a file protected by its password is listed; B107 / R5-18: it is a file of the user
    std::vector<pdf::PDFCertificateEntry> files;
    for (const pdf::PDFCertificateEntry& entry : Manager::getCertificates())
    {
        if (!entry.pkcs12FilePath.isEmpty())
        {
            files.push_back(entry);
        }
    }
    QCOMPARE(files.size(), size_t(2));
    QCOMPARE(QFileInfo(files[0].pkcs12FilePath), QFileInfo(ownFile));
    QCOMPARE(files[0].pkcs12fileName, QStringLiteral("jane_novak.pfx"));
    QCOMPARE(files[0].pkcs12, pkcs12);
    QCOMPARE(files[0].type, pdf::PDFCertificateEntry::EntryType::User);
    QVERIFY(!files[0].isReadOnlyLocation);
    QCOMPARE(QFileInfo(files[1].pkcs12FilePath), QFileInfo(oldFile));
    QCOMPARE(files[1].type, pdf::PDFCertificateEntry::EntryType::User);
    QVERIFY(files[1].isReadOnlyLocation);

    // readCertificate: the password in UTF-8 (B84), the DER of the certificate (Q5.11)
    QVERIFY(!Manager::readCertificate(pkcs12, QStringLiteral("wrong")));
    QVERIFY(!Manager::readCertificate(pkcs12, QString()));
    const std::optional<pdf::PDFCertificateInfo> info = Manager::readCertificate(pkcs12, Helper::getCertificatePassword());
    QVERIFY(info);
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));
    const std::optional<pdf::PDFCertificateInfo> parsed = pdf::PDFCertificateInfo::getCertificateInfo(info->getCertificateData());
    QVERIFY(parsed);
    QCOMPARE(parsed->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));

    // isPkcs12: a PKCS#12, not a DER certificate, random bytes, nothing or a buffer over 1 MiB
    QVERIFY(Manager::isPkcs12(pkcs12));
    QVERIFY(!Manager::isPkcs12(info->getCertificateData()));
    QVERIFY(!Manager::isPkcs12(QByteArray(1000, 'x')));
    QVERIFY(!Manager::isPkcs12(QByteArray()));
    QVERIFY(!Manager::isPkcs12(pkcs12 + QByteArray(int(Manager::MAXIMUM_CERTIFICATE_FILE_SIZE), '\0')));

    // Q5.5: the copy of an old file; the same file again is no conflict, another one is
    QString error;
    QVERIFY(Manager::copyCertificateToUserDirectory(files[1], &error));
    QVERIFY(error.isEmpty());
    QFile copy(QDir(directory).absoluteFilePath(QStringLiteral("editor.pfx")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), files[1].pkcs12);
    copy.close();
    QVERIFY(Manager::copyCertificateToUserDirectory(files[1], &error));

    pdf::PDFCertificateEntry other = files[0];
    other.pkcs12fileName = QStringLiteral("editor.pfx");
    QVERIFY(!Manager::copyCertificateToUserDirectory(other, &error));
    QVERIFY(error.contains(QStringLiteral("editor.pfx")));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), files[1].pkcs12);
    copy.close();

    Helper::removeCertificateFolders();
}

void DocumentToolEncryptionTest::certificateManagerCreate()
{
    using Helper = EncryptionTestHelper;
    using Manager = pdf::PDFCertificateManager;
    Helper::removeCertificateFolders();
    const QString directory = Manager::getCertificateDirectory();
    QVERIFY(QDir().mkpath(directory));

    // B99: a created certificate can be a recipient (keyEncipherment, RFC 5280 4.2.1.3) and signs
    const QString fileName = QDir(directory).absoluteFilePath(QStringLiteral("created.pfx"));
    QVERIFY(Helper::createCertificate(fileName, Helper::getCertificatePassword(), QStringLiteral("Created")));
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const std::optional<pdf::PDFCertificateInfo> info = Manager::readCertificate(file.readAll(), Helper::getCertificatePassword());
    file.close();
    QVERIFY(info);
    QVERIFY(info->getKeyUsage().testFlag(pdf::PDFCertificateInfo::KeyUsageKeyEncipherment));
    QVERIFY(info->getKeyUsage().testFlag(pdf::PDFCertificateInfo::KeyUsageDigitalSignature));
    QCOMPARE(info->getKeySize(), 2048);
    QVERIFY(Manager::isUsableForEncryption(info->getCertificateData()));
    QVERIFY(!Manager::isUsableForEncryption(QByteArray("not a certificate")));

    // The e-mail is the attribute emailAddress of PKCS #9 (OpenSSL has no short name "E") and
    // the country a code of two letters (ISO 3166, RFC 5280 4.1.2.4)
    pdf::PDFCertificateManager::NewCertificateInfo named;
    named.fileName = QDir(directory).absoluteFilePath(QStringLiteral("named.pfx"));
    named.privateKeyPasword = Helper::getCertificatePassword();
    named.certCommonName = QStringLiteral("Jane Novak");
    named.certEmail = QStringLiteral("jane.novak@example.com");
    named.certCountryCode = QStringLiteral("CZ");
    named.certOrganization = QStringLiteral("Acme Corp");
    named.rsaKeyLength = 2048;
    QVERIFY(Manager().createCertificate(named));
    QFile namedFile(named.fileName);
    QVERIFY(namedFile.open(QIODevice::ReadOnly));
    const std::optional<pdf::PDFCertificateInfo> namedInfo = Manager::readCertificate(namedFile.readAll(), Helper::getCertificatePassword());
    namedFile.close();
    QVERIFY(namedInfo);
    QCOMPARE(namedInfo->getName(pdf::PDFCertificateInfo::Email), QStringLiteral("jane.novak@example.com"));
    QCOMPARE(namedInfo->getName(pdf::PDFCertificateInfo::CountryName), QStringLiteral("CZ"));
    QCOMPARE(namedInfo->getName(pdf::PDFCertificateInfo::OrganizationName), QStringLiteral("Acme Corp"));

    QString namedError;
    named.fileName = QDir(directory).absoluteFilePath(QStringLiteral("country.pfx"));
    named.certCountryCode = QStringLiteral("CZE");
    QVERIFY(!Manager().createCertificate(named, &namedError));
    QVERIFY(namedError.contains(QStringLiteral("CZE")));
    QVERIFY(!QFile::exists(named.fileName));

    // RFC 5280, 4.2.1.6: a certificate (public only, of a P-256 key, CN "SAN Test") with the e-mail
    // only in the rfc822Name of its subject alternative name; its key is not RSA, it cannot be a
    // recipient of the encryption by a certificate
    const QByteArray sanCertificate = QByteArray::fromHex(
        "3082019e30820144a00302010202144b33c589cef246db227ff2d5b373dcc718177871300a06082a8648ce3d04030230133111300f06035504030c0853414e"
        "20546573743020170d3236313131343139333832385a180f32313236313032313139333832385a30133111300f06035504030c0853414e20546573743059"
        "301306072a8648ce3d020106082a8648ce3d030107034200043da08139fead8ceb57b6450d38c131b4fcc7de0be907a94b9d3bb872f00b6a8eb5e4290d68"
        "e78ebcceb07648494341ea2ac2a944af4a06c3d4f4b8259fe7a482a3743072301d0603551d0e041604146286a1351559e34abc9a2fb2acda2c650885c03a30"
        "1f0603551d230418301680146286a1351559e34abc9a2fb2acda2c650885c03a300f0603551d130101ff040530030101ff301f0603551d1104183016811473"
        "616e2e6f6e6c79406578616d706c652e636f6d300a06082a8648ce3d04030203480030450221009f7729b38d25b65166d567b92c0ff2ac0a70e4a88f8e6e49"
        "cb588483ac9f2a74022015b9409fa01d59409a7992270f65a45240f3d19896d761ef86f80b2137244f71");
    const std::optional<pdf::PDFCertificateInfo> sanInfo = pdf::PDFCertificateInfo::getCertificateInfo(sanCertificate);
    QVERIFY(sanInfo);
    QCOMPARE(sanInfo->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("SAN Test"));
    QCOMPARE(sanInfo->getName(pdf::PDFCertificateInfo::Email), QStringLiteral("san.only@example.com"));
    QVERIFY(!Manager::isUsableForEncryption(sanCertificate));

    // A failure says why and writes nothing
    pdf::PDFCertificateManager::NewCertificateInfo invalid;
    invalid.fileName = QDir(directory).absoluteFilePath(QStringLiteral("invalid.pfx"));
    invalid.privateKeyPasword = Helper::getCertificatePassword();
    invalid.rsaKeyLength = 100;
    QString error;
    QVERIFY(!Manager().createCertificate(invalid, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(invalid.fileName));

    invalid.rsaKeyLength = 2048;
    invalid.fileName = QDir(directory).absoluteFilePath(QStringLiteral("missing/folder/invalid.pfx"));
    QVERIFY(!Manager().createCertificate(invalid, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!error.contains(Helper::getCertificatePassword()));

    Helper::removeCertificateFolders();
}

void DocumentToolEncryptionTest::certificateRoundTrip_data()
{
    QTest::addColumn<QString>("scope");
    QTest::addColumn<bool>("isRestricted");

    QTest::newRow("everything") << "everything" << false;
    QTest::newRow("except-metadata") << "except-metadata" << false;
    QTest::newRow("attachments") << "attachments" << false;
    QTest::newRow("everything-restricted") << "everything" << true;
}

void DocumentToolEncryptionTest::certificateRoundTrip()
{
    QFETCH(QString, scope);
    QFETCH(bool, isRestricted);

    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;
    Helper::removeCertificateFolders();

    // R5-1: a certificate created by the application, its .pfx in the folder of the application
    const QString directory = pdf::PDFCertificateManager::getCertificateDirectory();
    QVERIFY(QDir().mkpath(directory));
    const QString fileName = QDir(directory).absoluteFilePath(QStringLiteral("jane_novak.pfx"));
    QVERIFY(Helper::createCertificate(fileName, Helper::getCertificatePassword(), QStringLiteral("Jane Novak")));
    pdf::PDFCertificateEntry entry = Helper::findEntry(fileName);
    QVERIFY(!entry.pkcs12.isEmpty());
    const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(entry.pkcs12, Helper::getCertificatePassword());
    QVERIFY(info);
    entry.info = *info;

    // R5-1: the run uses the chosen entry, never the certificate folders - the file is gone
    QVERIFY(QFile::remove(fileName));
    const pdf::PDFCertificateEntries chosen = { entry };

    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    pdf::PDFDocumentToolContext context = Helper::createContext(document, input);
    context.userCertificates = { entry };

    QVariantMap parameters = Tool::getPresetValues(isRestricted ? Tool::Preset::ViewAndPrint : Tool::Preset::AllowEverything);
    parameters.insert(QStringLiteral("scope"), scope);
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(info->getCertificateData()));
    parameters.insert(QStringLiteral("certificatePassword"), Helper::getCertificatePassword());

    const pdf::PDFDocumentToolResult result = Helper::run(Tool::Mode::Certificate, context, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Project_Report_encrypted.pdf"));

    // 433:4721: the holder by its name (never a path), the method; File attachments only opens
    // for everyone (7.6.6: /StmF and /StrF Identity), only the attachments need the key (RS5SEC L5)
    const bool isAttachmentsOnly = scope == QLatin1String("attachments");
    QCOMPARE(Helper::getFact(result, Tool::FACT_OPENS_FOR), isAttachmentsOnly ? QStringLiteral("Everyone · attachments: Jane Novak (private key needed)")
                                                                              : QStringLiteral("Jane Novak (private key needed)"));
    QCOMPARE(result.data.value(QStringLiteral("openPasswordRequired")).toBool(), !isAttachmentsOnly);
    QCOMPARE(Helper::getFact(result, Tool::FACT_ENCRYPTION), QStringLiteral("Certificate · AES 256-bit"));
    QVERIFY(!QJsonDocument(result.toJson()).toJson().contains("jane_novak"));
    QCOMPARE(result.data.value(QStringLiteral("algorithm")).toString(), QStringLiteral("certificate"));
    QCOMPARE(result.data.value(QStringLiteral("revision")).toInt(), 0);

    // 4.5: /Adobe.PubSec, adbe.pkcs7.s5, AESV3 - PDF 1.7 with /ADBE level 8 below PDF 2.0 (B98)
    const QByteArray data = Helper::getResultData(result);
    QVERIFY(data.startsWith("%PDF-1.7"));
    QVERIFY(data.contains("/Adobe.PubSec"));
    QVERIFY(data.contains("/adbe.pkcs7.s5"));
    QCOMPARE(data.contains("MarkerPage"), scope == QLatin1String("attachments"));
    QVERIFY(!data.contains("MarkerAttachment"));

    // The private key opens it (7.6.5); Table 24 permissions of the recipient
    bool isRead = false;
    Helper::Authorization authorization = Helper::Authorization::Failed;
    const pdf::PDFDocument opened = Helper::read(data, Helper::getCertificatePassword(), false, &isRead, &authorization, &chosen);
    QVERIFY(isRead);
    QVERIFY(authorization == Helper::Authorization::UserAuthorized || authorization == Helper::Authorization::OwnerAuthorized);
    QCOMPARE(Helper::getAdbeExtensionLevel(opened), 8);
    QCOMPARE(Helper::getDecodedObject(opened, 5), QByteArray(Helper::PAGE_CONTENT));
    QCOMPARE(Helper::getDecodedObject(opened, 7), QByteArray("MarkerAttachment payload"));
    QCOMPARE(opened.getStorage().getSecurityHandler()->isAllowed(Helper::Permission::CopyContent), !isRestricted);

    // Owner answer Q5.3 b (Table 24, bit 2): with every permission the holder removes the
    // encryption itself; with restrictions it cannot
    QCOMPARE(Tool::isSecurityChangeAllowed(&opened), !isRestricted);
    const pdf::PDFDocumentToolResult removed = Helper::run(Tool::Mode::Unlock, Helper::createContext(opened, data), QVariantMap());
    if (isRestricted)
    {
        QCOMPARE(removed.status, pdf::PDFDocumentToolResultStatus::Failed);
    }
    else
    {
        QVERIFY2(removed.isSuccess(), qPrintable(removed.message));
        QCOMPARE(Helper::getFact(removed, Tool::FACT_ENCRYPTION), QStringLiteral("None (was Certificate · AES 256-bit)"));
        const QByteArray plain = Helper::getResultData(removed);
        QVERIFY(plain.contains("MarkerPage"));
        QVERIFY(!plain.contains("/Adobe.PubSec"));
    }

    Helper::removeCertificateFolders();
}

void DocumentToolEncryptionTest::certificateValidation()
{
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;
    Helper::removeCertificateFolders();

    const QString directory = pdf::PDFCertificateManager::getCertificateDirectory();
    QVERIFY(QDir().mkpath(directory));
    const QString validFile = QDir(directory).absoluteFilePath(QStringLiteral("valid.pfx"));
    const QString expiredFile = QDir(directory).absoluteFilePath(QStringLiteral("expired.pfx"));
    QVERIFY(Helper::createCertificate(validFile, Helper::getCertificatePassword(), QStringLiteral("Valid")));
    QVERIFY(Helper::createCertificate(expiredFile, Helper::getCertificatePassword(), QStringLiteral("Expired"), -3600));

    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    pdf::PDFDocumentToolContext context = Helper::createContext(document, input);
    context.userCertificates = { Helper::findEntry(validFile), Helper::findEntry(expiredFile) };
    const std::optional<pdf::PDFCertificateInfo> valid = pdf::PDFCertificateManager::readCertificate(context.userCertificates[0].pkcs12, Helper::getCertificatePassword());
    const std::optional<pdf::PDFCertificateInfo> expired = pdf::PDFCertificateManager::readCertificate(context.userCertificates[1].pkcs12, Helper::getCertificatePassword());
    QVERIFY(valid && expired);

    const Tool tool(Tool::Mode::Certificate);
    QVariantMap parameters = Tool::getPresetValues(Tool::Preset::AllowEverything);

    // No certificate chosen, an unknown one
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("certificate"), QStringLiteral("Choose a certificate.")));
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(QByteArray("unknown")));
    parameters.insert(QStringLiteral("certificatePassword"), Helper::getCertificatePassword());
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("certificate")));

    // 433:3972: a wrong password
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(valid->getCertificateData()));
    parameters.insert(QStringLiteral("certificatePassword"), QStringLiteral("wrong"));
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("certificatePassword"), QStringLiteral("Invalid certificate or password.")));
    QCOMPARE(Helper::run(Tool::Mode::Certificate, context, parameters).status, pdf::PDFDocumentToolResultStatus::Failed);

    // 433:3049: an expired certificate cannot be used
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(expired->getCertificateData()));
    parameters.insert(QStringLiteral("certificatePassword"), Helper::getCertificatePassword());
    QVERIFY(Helper::hasError(tool.validate(context, parameters), QStringLiteral("certificate")));

    // The valid one with its password
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(valid->getCertificateData()));
    QVERIFY(tool.validate(context, parameters).empty());

    Helper::removeCertificateFolders();
}

void DocumentToolEncryptionTest::certificateOfSystemStore()
{
#ifdef Q_OS_WIN
    // The addendum of the owner of 15. 11. (step 6 of phase 2, Q6.2): a certificate of the store of Windows
    // is a recipient by its public certificate only - no password, its key is never used by the run (the
    // store of the run has not even its key); the result opens by its key in the store. The tests use a
    // store in the memory of the process, never the store of the machine.
    using Helper = EncryptionTestHelper;
    using Tool = pdf::PDFDocumentToolEncryption;
    Helper::removeCertificateFolders();
    struct StoreGuard
    {
        ~StoreGuard() { pdf::PDFPersonalCertificateStore::setStore(nullptr); }
    } storeGuard;

    const QString directory = pdf::PDFCertificateManager::getCertificateDirectory();
    QVERIFY(QDir().mkpath(directory));
    const auto createPkcs12 = [&directory](const QString& name, int validity) -> QByteArray
    {
        const QString fileName = QDir(directory).absoluteFilePath(name + QStringLiteral(".pfx"));
        if (!Helper::createCertificate(fileName, Helper::getCertificatePassword(), name, validity))
        {
            return QByteArray();
        }
        QFile file(fileName);
        const QByteArray data = file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
        file.close();
        QFile::remove(fileName);
        return data;
    };
    const QByteArray holderPkcs12 = createPkcs12(QStringLiteral("Jane Novak"), 365 * 24 * 3600);
    const QByteArray unrelatedPkcs12 = createPkcs12(QStringLiteral("Somebody Else"), 365 * 24 * 3600);
    const QByteArray expiredPkcs12 = createPkcs12(QStringLiteral("Expired Holder"), -3600);
    QVERIFY(!holderPkcs12.isEmpty() && !unrelatedPkcs12.isEmpty() && !expiredPkcs12.isEmpty());

    const auto createSystemEntry = [](const QByteArray& pkcs12)
    {
        pdf::PDFCertificateEntry entry;
        entry.type = pdf::PDFCertificateEntry::EntryType::System;
        entry.info = pdf::PDFCertificateManager::readCertificate(pkcs12, Helper::getCertificatePassword()).value_or(pdf::PDFCertificateInfo());
        return entry;
    };
    const pdf::PDFCertificateEntry holder = createSystemEntry(holderPkcs12);
    const pdf::PDFCertificateEntry expired = createSystemEntry(expiredPkcs12);
    QVERIFY(!holder.info.getCertificateData().isEmpty());
    QVERIFY(pdf::PDFSecurityHandlerFactory::isSystemStoreCertificate(holder));

    // The store of the run has the key of another certificate only
    const QByteArray passwordUtf8 = Helper::getCertificatePassword().toUtf8();
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(unrelatedPkcs12, passwordUtf8));

    const QByteArray input = Helper::createSample();
    const pdf::PDFDocument document = Helper::read(input, QString(), false);
    pdf::PDFDocumentToolContext context = Helper::createContext(document, input);
    context.userCertificates = { holder, expired };

    const Tool tool(Tool::Mode::Certificate);
    QVariantMap parameters = Tool::getPresetValues(Tool::Preset::AllowEverything);
    parameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(holder.info.getCertificateData()));
    QVERIFY(tool.validate(context, parameters).empty());

    // 433:3049: an expired certificate of the store cannot be used either
    QVariantMap expiredParameters = parameters;
    expiredParameters.insert(QStringLiteral("certificate"), Tool::getCertificateFingerprint(expired.info.getCertificateData()));
    QVERIFY(Helper::hasError(tool.validate(context, expiredParameters), QStringLiteral("certificate")));

    const pdf::PDFDocumentToolResult result = Helper::run(Tool::Mode::Certificate, context, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(Helper::getFact(result, Tool::FACT_OPENS_FOR), QStringLiteral("Jane Novak (private key needed)"));
    const QByteArray data = Helper::getResultData(result);
    QVERIFY(data.contains("/Adobe.PubSec"));
    QVERIFY(!data.contains("MarkerPage"));

    // The holder opens it by the key in the store (7.6.5.3), no password is asked
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(holderPkcs12, passwordUtf8));
    const pdf::PDFCertificateEntries recipients = { holder };
    bool isRead = false;
    Helper::Authorization authorization = Helper::Authorization::Failed;
    const pdf::PDFDocument opened = Helper::read(data, QString(), false, &isRead, &authorization, &recipients);
    QVERIFY(isRead);
    QCOMPARE(authorization, Helper::Authorization::UserAuthorized);
    QCOMPARE(Helper::getDecodedObject(opened, 5), QByteArray(Helper::PAGE_CONTENT));

    // Another key does not open it
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(unrelatedPkcs12, passwordUtf8));
    bool isReadByOther = true;
    Helper::read(data, QString(), false, &isReadByOther, nullptr, &recipients);
    QVERIFY(!isReadByOther);

    Helper::removeCertificateFolders();
#else
    QSKIP("The certificate store of the system exists only on Windows (Q6.2).");
#endif
}

QTEST_GUILESS_MAIN(DocumentToolEncryptionTest)

#include "tst_documenttoolencryptiontest.moc"
