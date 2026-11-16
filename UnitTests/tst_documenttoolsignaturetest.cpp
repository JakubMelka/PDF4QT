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

// Sign with Certificate and Add Timestamp of the core (step 6 of phase 2, plan 4.1, 9; package P2):
// the skeleton tests the fixed interface (plan 10.3 with chapters 12 and 13) - the ids, the modes,
// the parameters and their schema of MCP (owner answer Q6.1: the format pades / pkcs7, MCP name
// format, PAdES the default; the password and the drawing never in MCP). Package P2 adds the runs.

#include "pdfdocumenttool_signature.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfpagedrawing.h"
#include "pdftimestampclient.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumentwriter.h"
#include "pdfdocmdppermissions.h"
#include "pdfsignaturehandler.h"
#include "pdfcatalog.h"
#include "pdfencoding.h"
#include "pdfform.h"
#include "pdfobjectutils.h"

#include <QBuffer>
#include <QJsonArray>
#include <QtTest>

#include <atomic>
#include <initializer_list>

/// A transport of a test: the answer of an authority, which refuses (never the network)
class DocumentToolSignatureFailingTransport : public pdf::PDFTimestampTransport
{
public:
    explicit DocumentToolSignatureFailingTransport(int httpStatus) : m_httpStatus(httpStatus) { }

    virtual Response post(const Request& request, const pdf::PDFOperationControl* control) const override
    {
        Q_UNUSED(request);
        Q_UNUSED(control);
        ++m_requestCount;
        Response response;
        response.httpStatus = m_httpStatus;
        response.contentType = "text/html";
        return response;
    }

    int getRequestCount() const { return m_requestCount; }

private:
    int m_httpStatus;
    mutable std::atomic<int> m_requestCount = 0;
};

class DocumentToolSignatureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void idsAndModes();
    void parametersOfSign();
    void parametersOfTimestamp();

    // Package P2 (plan 4.1, 4.4, 5.1, 9 with R6-4, R6-8, R6-12, R6-25, Q6.3, Q6.17, Q6.18)
    void requiredPermissions();
    void emptySignatureFields();
    void seedValues_data();
    void seedValues();
    void validateCertifiedDocument();
    void validateAppearance();
    void signaturesOfFileNotPreserved();
    void timestampWithoutAuthority();
    void timestampAuthorityFailure();

    // The runs, which sign: they need the key of package P1b (PDFSignatureFactory::unlock)
    void signDocument_data();
    void signDocument();
    void signIntoField();
    void signTaggedDocument();
    void earlierSignatures();

    // Step 6 of phase 2, integration P2S6INTCORE
    void optionTextsOfFigma();
    void timeFactWithoutLocale();
    void signEncryptedDocument_data();
    void signEncryptedDocument();

    // Step 6 of phase 2, fix wave P2S6FIXCORE
    void existingFieldPermissions_data();
    void existingFieldPermissions();
    void signatureDateOfFormat_data();
    void signatureDateOfFormat();

private:
    using Tool = pdf::PDFDocumentToolSignature;
    using Dictionary = std::initializer_list<std::pair<const char*, pdf::PDFObject>>;

    static pdf::PDFObject dictionary(Dictionary entries);
    static pdf::PDFObject array(std::vector<pdf::PDFObject> items);
    static QByteArray write(const pdf::PDFDocument& document);
    static pdf::PDFDocumentPointer read(const QByteArray& data);
    static pdf::PDFDocumentToolContext createContext(const QByteArray& data, bool isModified = false);

    /// A document of \p pageCount pages written to bytes
    static QByteArray createDocument(int pageCount);

    /// A document of the page tree [[page 1, page 2], page 3] written to bytes (ISO 32000-2, 7.7.3.2)
    static QByteArray createNestedTreeDocument();

    /// A document certified with the level \p level (/Perms /DocMDP, Table 257)
    static QByteArray createCertifiedDocument(int level);

    /// A document with the signature fields of the cases of Q6.3 and R6-25 (Table 233, Table 234)
    static QByteArray createFieldDocument();

    /// A certificate of the test with its private key (made at run time, never on the disk)
    static bool createCertificate(pdf::PDFCertificateEntry* entry, QString* password);

    static QVariantMap createSignParameters(const pdf::PDFCertificateEntry& entry, const QString& password);
    static const pdf::PDFDocumentToolFact* findFact(const pdf::PDFDocumentToolResult& result, const char* id);
    static bool hasError(const std::vector<pdf::PDFDocumentToolParameterError>& errors, const QString& parameter);
};

pdf::PDFObject DocumentToolSignatureTest::dictionary(Dictionary entries)
{
    pdf::PDFDictionaryBuilder result;
    for (const auto& entry : entries)
    {
        result.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), pdf::PDFObject(entry.second));
    }
    return pdf::PDFObject::createDictionary(std::move(result));
}

pdf::PDFObject DocumentToolSignatureTest::array(std::vector<pdf::PDFObject> items)
{
    return pdf::PDFObject::createArray(pdf::PDFArrayBuilder(std::move(items)));
}

QByteArray DocumentToolSignatureTest::write(const pdf::PDFDocument& document)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !pdf::PDFDocumentWriter(nullptr).write(&buffer, &document))
    {
        return QByteArray();
    }
    return data;
}

pdf::PDFDocumentPointer DocumentToolSignatureTest::read(const QByteArray& data)
{
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    return pdf::PDFDocumentPointer(new pdf::PDFDocument(reader.readFromBuffer(data)));
}

pdf::PDFDocumentToolContext DocumentToolSignatureTest::createContext(const QByteArray& data, bool isModified)
{
    return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(read(data), data, QStringLiteral("C:/folder/contract.pdf"), isModified));
}

QByteArray DocumentToolSignatureTest::createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, 300, 400));
    }
    return write(builder.build());
}

QByteArray DocumentToolSignatureTest::createNestedTreeDocument()
{
    QByteArray data = "%PDF-1.7\n";
    const std::vector<QByteArray> objects = { "<< /Type /Catalog /Pages 2 0 R >>",
                                              "<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 3 >>",
                                              "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 /MediaBox [0 0 300 400] >>",
                                              "<< /Type /Page /Parent 3 0 R /Resources << >> >>",
                                              "<< /Type /Page /Parent 3 0 R /Resources << >> >>",
                                              "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << >> >>" };
    std::vector<qsizetype> offsets;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(data.size());
        data += QByteArray::number(qint64(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qsizetype xref = data.size();
    data += "xref\n0 " + QByteArray::number(qint64(objects.size() + 1)) + "\n0000000000 65535 f\r\n";
    for (const qsizetype offset : offsets)
    {
        data += QByteArray::number(qint64(offset)).rightJustified(10, '0') + " 00000 n\r\n";
    }
    data += "trailer\n<< /Size " + QByteArray::number(qint64(objects.size() + 1)) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(qint64(xref)) + "\n%%EOF\n";
    return data;
}

QByteArray DocumentToolSignatureTest::createCertifiedDocument(int level)
{
    using pdf::PDFObject;
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFObject reference = dictionary({ { "TransformMethod", PDFObject::createName("DocMDP") },
                                             { "TransformParams", dictionary({ { "P", PDFObject::createInteger(level) } }) } });
    const pdf::PDFObjectReference certification = builder.addObject(dictionary({ { "Type", PDFObject::createName("Sig") }, { "Reference", array({ reference }) } }));
    builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Perms", dictionary({ { "DocMDP", PDFObject::createReference(certification) } }) } }));
    return write(builder.build());
}

QByteArray DocumentToolSignatureTest::createFieldDocument()
{
    using pdf::PDFObject;
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));

    std::vector<PDFObject> fields;
    std::vector<PDFObject> annotations;
    const auto addField = [&](const char* name, Dictionary entries, bool isTopLevel = true)
    {
        pdf::PDFDictionaryBuilder field;
        field.setEntry(pdf::PDFInplaceOrMemoryString("FT"), PDFObject::createName("Sig"));
        field.setEntry(pdf::PDFInplaceOrMemoryString("T"), pdf::PDFObjectFactory::createTextString(QString::fromLatin1(name)));
        field.setEntry(pdf::PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
        field.setEntry(pdf::PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Widget"));
        field.setEntry(pdf::PDFInplaceOrMemoryString("P"), PDFObject::createReference(page));
        field.setEntry(pdf::PDFInplaceOrMemoryString("Rect"), array({ PDFObject::createInteger(100), PDFObject::createInteger(100), PDFObject::createInteger(300), PDFObject::createInteger(150) }));
        for (const auto& entry : entries)
        {
            field.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), pdf::PDFObject(entry.second));
        }
        const pdf::PDFObjectReference reference = builder.addObject(PDFObject::createDictionary(std::move(field)));
        if (isTopLevel)
        {
            fields.push_back(PDFObject::createReference(reference));
        }
        annotations.push_back(PDFObject::createReference(reference));
        return reference;
    };

    const auto names = [](std::vector<const char*> values)
    {
        std::vector<PDFObject> items;
        for (const char* value : values)
        {
            items.push_back(PDFObject::createName(value));
        }
        return array(std::move(items));
    };

    addField("Empty", {});
    addField("Signed", { { "V", dictionary({ { "Type", PDFObject::createName("Sig") }, { "Filter", PDFObject::createName("Adobe.PPKLite") }, { "Contents", PDFObject::createString(QByteArray(16, '\x30')) } }) } });
    addField("Locked", { { "Lock", dictionary({ { "Action", PDFObject::createName("All") } }) } });
    addField("Certify", { { "SV", dictionary({ { "MDP", dictionary({ { "P", PDFObject::createInteger(2) } }) } }) } });
    addField("PadesOnly", { { "SV", dictionary({ { "Ff", PDFObject::createInteger(2) }, { "SubFilter", names({ "ETSI.CAdES.detached" }) } }) } });
    addField("NoOurFormat", { { "SV", dictionary({ { "Ff", PDFObject::createInteger(2) }, { "SubFilter", names({ "adbe.x509.rsa_sha1" }) } }) } });
    addField("NeedsTimestamp", { { "SV", dictionary({ { "TimeStamp", dictionary({ { "URL", PDFObject::createString("http://tsa.test") }, { "Ff", PDFObject::createInteger(1) } }) } }) } });
    addField("Reasons", { { "SV", dictionary({ { "Ff", PDFObject::createInteger(8) }, { "Reasons", array({ pdf::PDFObjectFactory::createTextString(QStringLiteral("Approved")),
                                                                                                                 pdf::PDFObjectFactory::createTextString(QStringLiteral("Reviewed")) }) } }) } });
    addField("RevocationInfo", { { "SV", dictionary({ { "Ff", PDFObject::createInteger(32) }, { "AddRevInfo", PDFObject::createBool(true) } }) } });
    addField("Sha1Only", { { "SV", dictionary({ { "Ff", PDFObject::createInteger(64) }, { "DigestMethod", names({ "SHA1" }) } }) } });

    // A parent with a kid, the kid also under a second parent (a node shared by two paths), a field,
    // which is its own kid (a cycle): each field once, the walk ends (one visited set, a depth limit)
    const pdf::PDFObjectReference child = addField("Child", {}, false);
    const pdf::PDFObjectReference group = builder.addObject(dictionary({ { "FT", PDFObject::createName("Sig") }, { "T", pdf::PDFObjectFactory::createTextString(QStringLiteral("Group")) },
                                                                          { "Kids", array({ PDFObject::createReference(child) }) } }));
    const pdf::PDFObjectReference other = builder.addObject(dictionary({ { "FT", PDFObject::createName("Sig") }, { "T", pdf::PDFObjectFactory::createTextString(QStringLiteral("Other")) },
                                                                          { "Kids", array({ PDFObject::createReference(child) }) } }));
    builder.mergeTo(child, dictionary({ { "Parent", PDFObject::createReference(group) } }));
    const pdf::PDFObjectReference loop = builder.addObject(dictionary({ { "FT", PDFObject::createName("Sig") }, { "T", pdf::PDFObjectFactory::createTextString(QStringLiteral("Loop")) } }));
    builder.mergeTo(loop, dictionary({ { "Kids", array({ PDFObject::createReference(loop) }) } }));
    fields.push_back(PDFObject::createReference(group));
    fields.push_back(PDFObject::createReference(other));
    fields.push_back(PDFObject::createReference(loop));

    builder.mergeTo(page, dictionary({ { "Annots", array(annotations) } }));
    builder.mergeTo(builder.getCatalogReference(), dictionary({ { "AcroForm", dictionary({ { "Fields", array(fields) } }) } }));
    return write(builder.build());
}

bool DocumentToolSignatureTest::createCertificate(pdf::PDFCertificateEntry* entry, QString* password)
{
    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.privateKeyPasword = QStringLiteral("test-password");
    info.certCommonName = QStringLiteral("Jane Novak");
    info.rsaKeyLength = 2048;
    QString error;
    entry->pkcs12 = pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error);
    *password = info.privateKeyPasword;
    const std::optional<pdf::PDFCertificateInfo> certificate = pdf::PDFCertificateManager::readCertificate(entry->pkcs12, *password);
    if (!certificate)
    {
        return false;
    }
    entry->info = *certificate;
    return true;
}

QVariantMap DocumentToolSignatureTest::createSignParameters(const pdf::PDFCertificateEntry& entry, const QString& password)
{
    return { { QStringLiteral("certificate"), pdf::PDFDocumentToolEncryption::getCertificateFingerprint(entry.info.getCertificateData()) },
             { QStringLiteral("certificatePassword"), password },
             { QStringLiteral("appearance"), QStringLiteral("invisible") } };
}

const pdf::PDFDocumentToolFact* DocumentToolSignatureTest::findFact(const pdf::PDFDocumentToolResult& result, const char* id)
{
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String(id))
        {
            return &fact;
        }
    }
    return nullptr;
}

bool DocumentToolSignatureTest::hasError(const std::vector<pdf::PDFDocumentToolParameterError>& errors, const QString& parameter)
{
    return std::any_of(errors.cbegin(), errors.cend(), [&parameter](const pdf::PDFDocumentToolParameterError& error) { return error.parameter == parameter; });
}

void DocumentToolSignatureTest::idsAndModes()
{
    using Tool = pdf::PDFDocumentToolSignature;
    for (const Tool::Mode mode : { Tool::Mode::Sign, Tool::Mode::Timestamp })
    {
        const Tool tool(mode);
        QVERIFY(tool.getInfo());
        QCOMPARE(tool.getId(), Tool::getToolId(mode));
        QVERIFY(Tool::findMode(tool.getId()) == mode);
        QVERIFY(pdf::PDFDocumentToolParameters::isValid(tool.getParameters()));

        // R6-8: an incremental update of the input, no new document of the inputs; the result is only a file
        QVERIFY(!tool.isNewDocumentOfInputs(QVariantMap()));
        QVERIFY(tool.getInfo()->isFileOnlyResult());
    }
    QCOMPARE(Tool::getToolId(Tool::Mode::Sign), QStringLiteral("protect.sign"));
    QCOMPARE(Tool::getToolId(Tool::Mode::Timestamp), QStringLiteral("protect.timestamp"));
    QVERIFY(!Tool::findMode(u"protect.redact"));

    // Q6.1: ETSI.CAdES.detached (PAdES) and adbe.pkcs7.detached (ISO 32000-2, 12.8.3.3, 12.8.3.4)
    QCOMPARE(Tool::getFormat(QStringLiteral("pades")), pdf::PDFSignatureFormat::PAdES);
    QCOMPARE(Tool::getFormat(QStringLiteral("pkcs7")), pdf::PDFSignatureFormat::Pkcs7);
    QCOMPARE(pdf::PDFSignatureFactory::getSubFilter(pdf::PDFSignatureFormat::PAdES), QByteArrayLiteral("ETSI.CAdES.detached"));
    QCOMPARE(pdf::PDFSignatureFactory::getSubFilter(pdf::PDFSignatureFormat::Pkcs7), QByteArrayLiteral("adbe.pkcs7.detached"));
}

void DocumentToolSignatureTest::parametersOfSign()
{
    using Tool = pdf::PDFDocumentToolSignature;
    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();

    // Owner answer Q6.1: the user chooses the format, PAdES is the default
    const pdf::PDFDocumentToolParameterDefinition* format = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("format"));
    QVERIFY(format);
    QCOMPARE(format->getType(), pdf::PDFDocumentToolParameterType::Enum);
    QCOMPARE(format->getDefaultValue().toString(), QStringLiteral("pades"));
    QCOMPARE(format->getOptions().size(), size_t(2));
    QCOMPARE(format->getOptions()[1].id, QStringLiteral("pkcs7"));

    // 6.5, Q5.6: the password and the drawing never go through MCP; the certificate is a preselection
    const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject();
    QVERIFY(!properties.contains(QStringLiteral("certificatePassword")));
    QVERIFY(!properties.contains(QStringLiteral("drawing")));
    for (const char* name : { "certificate", "format", "type", "appearance", "field", "reason", "contactInfo", "tsaUrl" })
    {
        QVERIFY2(properties.contains(QString::fromLatin1(name)), name);
    }
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("certificatePassword"))->isMcpExcluded());
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("certificatePassword"))->isStored());

    // The default authority is the first preset (Figma 434:6241)
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("tsaUrl"))->getDefaultValue().toString(), QStringLiteral("http://timestamp.digicert.com"));
}

void DocumentToolSignatureTest::parametersOfTimestamp()
{
    // Add Timestamp has only the authority (435:7037)
    const pdf::PDFDocumentToolSignature tool(pdf::PDFDocumentToolSignature::Mode::Timestamp);
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QCOMPARE(definitions.size(), size_t(1));
    QCOMPARE(definitions.front().getName(), QStringLiteral("tsaUrl"));
}

void DocumentToolSignatureTest::requiredPermissions()
{
    // ISO 32000-2, Table 22 (5.1, R6-8): a new signature field needs bit 4 and bit 6 ("create or
    // modify interactive form fields (including signature fields)"), an existing empty field bit 9
    // ("fill in existing interactive form fields (including signature fields)")
    using Permission = pdf::PDFSecurityHandler::Permission;
    const Tool sign(Tool::Mode::Sign);
    const Tool timestamp(Tool::Mode::Timestamp);
    const std::vector<Permission> newField = { Permission::Modify, Permission::ModifyInteractiveItems };

    std::vector<Permission> permissions = sign.getRequiredPermissions(QVariantMap());
    std::sort(permissions.begin(), permissions.end());
    QCOMPARE(permissions, newField);
    QCOMPARE(sign.getAnyOfPermissions(QVariantMap()), std::vector<Permission>());

    // An existing empty field: bit 9 or bit 6, nothing else (RS6APP M10, existingFieldPermissions)
    QCOMPARE(sign.getRequiredPermissions({ { QStringLiteral("field"), QStringLiteral("Approval") } }), std::vector<Permission>());
    permissions = sign.getAnyOfPermissions({ { QStringLiteral("field"), QStringLiteral("Approval") } });
    std::sort(permissions.begin(), permissions.end());
    QCOMPARE(permissions, std::vector<Permission>({ Permission::ModifyInteractiveItems, Permission::ModifyFormFields }));

    permissions = timestamp.getRequiredPermissions({ { QStringLiteral("field"), QStringLiteral("Approval") } });
    std::sort(permissions.begin(), permissions.end());
    QCOMPARE(permissions, newField);
}

void DocumentToolSignatureTest::emptySignatureFields()
{
    // Q6.3, R6-25: the empty signature fields (/FT /Sig, no /V; Table 226, Table 233) with the reason,
    // why the tool cannot sign one of them (/Lock, the seed values of Table 234 the tool cannot meet)
    const pdf::PDFDocumentPointer document = read(createFieldDocument());
    const std::vector<Tool::EmptySignatureField> fields = Tool::getEmptySignatureFields(document.data());

    std::map<QString, Tool::EmptySignatureField> byName;
    for (const Tool::EmptySignatureField& field : fields)
    {
        QVERIFY2(byName.emplace(field.name, field).second, qPrintable(field.name));
    }

    // A signed field is not empty; the kid shared by two parents is listed once by its first path
    QVERIFY(!byName.count(QStringLiteral("Signed")));
    QVERIFY(byName.count(QStringLiteral("Group.Child")));
    QVERIFY(!byName.count(QStringLiteral("Other.Child")));

    const Tool::EmptySignatureField& empty = byName.at(QStringLiteral("Empty"));
    QVERIFY(empty.unusableReason.isEmpty());
    QCOMPARE(empty.pageIndex, pdf::PDFInteger(1));
    QCOMPARE(empty.rectangle, QRectF(100, 100, 200, 50));
    QVERIFY(empty.widget.isValid());

    for (const char* unusable : { "Locked", "Certify", "NoOurFormat", "RevocationInfo", "Sha1Only" })
    {
        QVERIFY2(!byName.at(QString::fromLatin1(unusable)).unusableReason.isEmpty(), unusable);
    }
    QCOMPARE(byName.at(QStringLiteral("PadesOnly")).subFilters, QStringList({ QStringLiteral("ETSI.CAdES.detached") }));
    QVERIFY(byName.at(QStringLiteral("PadesOnly")).unusableReason.isEmpty());
    QVERIFY(byName.at(QStringLiteral("NeedsTimestamp")).isTimestampRequired);
    QVERIFY(!byName.at(QStringLiteral("Empty")).isTimestampRequired);
    QCOMPARE(byName.at(QStringLiteral("Reasons")).reasons, QStringList({ QStringLiteral("Approved"), QStringLiteral("Reviewed") }));

    QVERIFY(Tool::getEmptySignatureFields(nullptr).empty());
    QVERIFY(Tool::getEmptySignatureFields(read(createDocument(1)).data()).empty());
}

void DocumentToolSignatureTest::seedValues_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<QString>("errorParameter");
    QTest::addColumn<bool>("isError");

    // R6-25 (ISO 32000-2, 12.8.3.4.4: "should enforce seed value constraints at signing time"): the
    // field restricts the format (Q6.1), the timestamp and the reason; a field, which cannot be
    // signed, is an error of the parameter field
    const QVariantMap pkcs7 = { { QStringLiteral("format"), QStringLiteral("pkcs7") } };
    const QVariantMap pades = { { QStringLiteral("format"), QStringLiteral("pades") } };
    QTest::newRow("pades-only-pkcs7") << QStringLiteral("PadesOnly") << pkcs7 << QStringLiteral("format") << true;
    QTest::newRow("pades-only-pades") << QStringLiteral("PadesOnly") << pades << QStringLiteral("format") << false;
    QTest::newRow("timestamp-missing") << QStringLiteral("NeedsTimestamp") << QVariantMap({ { QStringLiteral("type"), QStringLiteral("signature") } }) << QStringLiteral("type") << true;
    QTest::newRow("timestamp-given") << QStringLiteral("NeedsTimestamp") << QVariantMap({ { QStringLiteral("type"), QStringLiteral("signature-timestamp") } }) << QStringLiteral("type") << false;
    QTest::newRow("reason-other") << QStringLiteral("Reasons") << QVariantMap({ { QStringLiteral("reason"), QStringLiteral("Other") } }) << QStringLiteral("reason") << true;
    QTest::newRow("reason-listed") << QStringLiteral("Reasons") << QVariantMap({ { QStringLiteral("reason"), QStringLiteral("Reviewed") } }) << QStringLiteral("reason") << false;
    QTest::newRow("locked") << QStringLiteral("Locked") << QVariantMap() << QStringLiteral("field") << true;
    QTest::newRow("signed") << QStringLiteral("Signed") << QVariantMap() << QStringLiteral("field") << true;
    QTest::newRow("missing") << QStringLiteral("Missing") << QVariantMap() << QStringLiteral("field") << true;
    QTest::newRow("empty") << QStringLiteral("Empty") << QVariantMap() << QStringLiteral("field") << false;
}

void DocumentToolSignatureTest::seedValues()
{
    QFETCH(QString, field);
    QFETCH(QVariantMap, parameters);
    QFETCH(QString, errorParameter);
    QFETCH(bool, isError);

    const Tool tool(Tool::Mode::Sign);
    QVariantMap values = parameters;
    values.insert(QStringLiteral("field"), field);
    values.insert(QStringLiteral("appearance"), QStringLiteral("invisible"));
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), values, &normalized).empty());

    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(createContext(createFieldDocument()), normalized);
    QCOMPARE(hasError(errors, errorParameter), isError);
}

void DocumentToolSignatureTest::validateCertifiedDocument()
{
    // B135, R6-4 (12.8.2.2.1, Table 257 /P 1): no signature of a certified document with no changes
    // allowed; a timestamp only without unsaved changes (the text of R6-4)
    const QByteArray noChanges = createCertifiedDocument(1);
    const Tool sign(Tool::Mode::Sign);
    const Tool timestamp(Tool::Mode::Timestamp);
    const QString undoText = QStringLiteral("This document is certified with no changes allowed. Undo the unsaved changes to add a timestamp.");

    const auto messages = [](const std::vector<pdf::PDFDocumentToolParameterError>& errors)
    {
        QStringList texts;
        for (const pdf::PDFDocumentToolParameterError& error : errors)
        {
            texts << error.message;
        }
        return texts;
    };

    QVariantMap signParameters;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(sign.getParameters(), QVariantMap({ { QStringLiteral("appearance"), QStringLiteral("invisible") } }), &signParameters).empty());
    QVERIFY(messages(sign.validate(createContext(noChanges), signParameters)).contains(QStringLiteral("This document is certified with no changes allowed. It cannot be signed.")));
    QVERIFY(!messages(sign.validate(createContext(createCertifiedDocument(2)), signParameters)).join(' ').contains(QLatin1String("certified")));

    QVariantMap timestampParameters;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(timestamp.getParameters(), QVariantMap(), &timestampParameters).empty());
    QVERIFY(messages(timestamp.validate(createContext(noChanges, true), timestampParameters)).contains(undoText));
    QVERIFY(!messages(timestamp.validate(createContext(noChanges, false), timestampParameters)).contains(undoText));
}

void DocumentToolSignatureTest::validateAppearance()
{
    // 434:4688 "No appearance drawn": a visible signature needs its drawing on a page of the document
    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolContext context = createContext(createDocument(2));
    const QString drawingParameter = QStringLiteral("drawing");

    pdf::PDFPageDrawing::Element dot;
    dot.kind = pdf::PDFPageDrawing::Kind::Dot;
    dot.rectangle = QRectF(10, 10, 20, 20);
    const pdf::PDFPageDrawing onPage({ dot });
    dot.pageIndex = 2;
    const pdf::PDFPageDrawing outside({ dot });

    const auto validate = [&](const QString& appearance, const pdf::PDFPageDrawing& drawing)
    {
        QVariantMap normalized;
        pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), QVariantMap({ { QStringLiteral("appearance"), appearance }, { drawingParameter, QVariant::fromValue(drawing) } }), &normalized);
        return tool.validate(context, normalized);
    };

    QVERIFY(hasError(validate(QStringLiteral("visible"), pdf::PDFPageDrawing()), drawingParameter));
    QVERIFY(hasError(validate(QStringLiteral("visible"), outside), drawingParameter));
    QVERIFY(!hasError(validate(QStringLiteral("visible"), onPage), drawingParameter));
    QVERIFY(!hasError(validate(QStringLiteral("invisible"), pdf::PDFPageDrawing()), drawingParameter));

    // Choose a certificate first (no preselection of an assistant)
    QVERIFY(hasError(validate(QStringLiteral("invisible"), pdf::PDFPageDrawing()), QStringLiteral("certificate")));
}

void DocumentToolSignatureTest::signaturesOfFileNotPreserved()
{
    // 4.4, R6-12: a file rewritten whole in the tab (Compress Keep) or restored of a damaged file is no
    // base of an update: a file with signatures fails (434:8913), a file without them is written whole
    // A file with a signed field (its /Contents is not empty; the value need not verify here)
    QByteArray signedData;
    {
        const QByteArray data = createDocument(1);
        const pdf::PDFDocumentPointer document = read(data);
        pdf::PDFDocumentBuilder builder(document.data());
        const pdf::PDFObjectReference signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", QByteArray(32, '\x30'), QDateTime::currentDateTimeUtc(), 0);
        builder.createSignatureField(QStringLiteral("First"), signature, document->getCatalog()->getPage(0)->getPageReference());
        const pdf::PDFDocument signedDocument = builder.build();
        QBuffer buffer(&signedData);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(pdf::PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, data, &signedDocument));
    }
    QVERIFY(pdf::PDFDocumentSigner::hasExistingSignatures(signedData));

    const Tool timestamp(Tool::Mode::Timestamp);
    const QString refusal = pdf::PDFDocumentSigner::getResultMessage(pdf::PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    for (const bool isRestored : { false, true })
    {
        pdf::PDFDocumentToolContext context = createContext(signedData);
        context.documents.front().isWholeFileRewritten = !isRestored;
        context.documents.front().isRestored = isRestored;
        const pdf::PDFDocumentToolResult result = timestamp.execute(context, QVariantMap(), nullptr, nullptr);
        QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
        QCOMPARE(result.message, refusal);
        QVERIFY(result.documents.empty());
    }

    // Without signatures the run goes on (here to the missing authority)
    pdf::PDFDocumentToolContext unsignedContext = createContext(createDocument(1));
    unsignedContext.documents.front().isWholeFileRewritten = true;
    const pdf::PDFDocumentToolResult result = timestamp.execute(unsignedContext, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message != refusal);
}

void DocumentToolSignatureTest::timestampWithoutAuthority()
{
    // S6.6: the network only by the client of the context; none = the run fails, nothing is sent
    const Tool timestamp(Tool::Mode::Timestamp);
    const pdf::PDFDocumentToolResult result = timestamp.execute(createContext(createDocument(1)), QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("No timestamp authority can be contacted."));
    QVERIFY(result.documents.empty());
}

void DocumentToolSignatureTest::timestampAuthorityFailure()
{
    // 435:8117 "The timestamp was not added", HTTP 503: the run fails with the error of the authority
    // for the panel (Try Again); one request only (B134: no trial request)
    const auto transport = std::make_shared<DocumentToolSignatureFailingTransport>(503);
    pdf::PDFDocumentToolContext context = createContext(createDocument(1));
    context.timestampClient = std::make_shared<pdf::PDFTimestampClient>(transport);

    const Tool timestamp(Tool::Mode::Timestamp);
    const pdf::PDFDocumentToolResult result = timestamp.run(context, { { QStringLiteral("tsaUrl"), QStringLiteral("http://tsa.test/tsr") } }, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.data.value(QStringLiteral("timestampError")).toString(), QStringLiteral("http-error"));
    QCOMPARE(result.data.value(QStringLiteral("httpStatus")).toInt(), 503);
    QCOMPARE(transport->getRequestCount(), 1);
    QVERIFY(result.documents.empty());
}

void DocumentToolSignatureTest::signDocument_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<bool>("isVisible");
    QTest::addColumn<QByteArray>("subFilter");
    QTest::addColumn<bool>("isNestedTree");

    // Owner answer Q6.1: PAdES (ETSI.CAdES.detached) by default, Compatible (PKCS#7) adbe.pkcs7.detached
    QTest::newRow("pades-invisible") << QStringLiteral("pades") << false << QByteArray("ETSI.CAdES.detached") << false;
    QTest::newRow("pades-visible") << QStringLiteral("pades") << true << QByteArray("ETSI.CAdES.detached") << false;
    QTest::newRow("pkcs7-visible") << QStringLiteral("pkcs7") << true << QByteArray("adbe.pkcs7.detached") << false;

    // ISO 32000-2, 7.7.3.2: the page 2 is a leaf of an intermediate node /Pages, never the node itself
    QTest::newRow("pades-visible-nested-tree") << QStringLiteral("pades") << true << QByteArray("ETSI.CAdES.detached") << true;
    QTest::newRow("pades-invisible-nested-tree") << QStringLiteral("pades") << false << QByteArray("ETSI.CAdES.detached") << true;
}

void DocumentToolSignatureTest::signDocument()
{
    QFETCH(QString, format);
    QFETCH(bool, isVisible);
    QFETCH(QByteArray, subFilter);
    QFETCH(bool, isNestedTree);

    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    const QByteArray data = isNestedTree ? createNestedTreeDocument() : createDocument(2);
    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };

    pdf::PDFPageDrawing::Element text;
    text.kind = pdf::PDFPageDrawing::Kind::Text;
    text.pageIndex = 1;
    text.rectangle = QRectF(50, 60, 150, 40);
    text.text = QStringLiteral("Jane Novak");

    QVariantMap parameters = createSignParameters(entry, password);
    parameters.insert(QStringLiteral("format"), format);
    parameters.insert(QStringLiteral("reason"), QStringLiteral("Approval"));
    parameters.insert(QStringLiteral("contactInfo"), QStringLiteral("jane@example.test"));
    if (isVisible)
    {
        parameters.insert(QStringLiteral("appearance"), QStringLiteral("visible"));
        parameters.insert(QStringLiteral("drawing"), QVariant::fromValue(pdf::PDFPageDrawing({ text })));
    }

    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));

    // R6-27, 4.4: the result is the exact bytes of the input followed by the update
    const pdf::PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, pdf::PDFDocumentToolOutputDocument::Kind::New);
    QVERIFY(output.writtenData.startsWith(data));
    QCOMPARE(output.suggestedFileName, QStringLiteral("contract_SIGNED.pdf"));
    QCOMPARE(result.data.value(QStringLiteral("subFilter")).toString(), QString::fromLatin1(subFilter));
    QVERIFY(findFact(result, "signer") && findFact(result, "signer")->value == QStringLiteral("Jane Novak"));
    QCOMPARE(findFact(result, "type")->value, isVisible ? QStringLiteral("Signature · visible on page 2") : QStringLiteral("Signature · invisible"));
    QVERIFY(findFact(result, "time")->value.endsWith(QLatin1String(" UTC")));
    QVERIFY(!findFact(result, "earlierSignatures"));
    QCOMPARE(result.data.value(QStringLiteral("unsavedChanges")).toBool(), false);

    // The signature dictionary and the field (Table 255, Table 226 /TU - K6.22)
    const pdf::PDFDocumentPointer signedDocument = read(output.writtenData);
    const pdf::PDFForm form = pdf::PDFForm::parse(signedDocument.data(), signedDocument->getCatalog()->getFormObject());
    const pdf::PDFFormFieldSignature* field = nullptr;
    form.apply([&field](const pdf::PDFFormField* formField) { field = dynamic_cast<const pdf::PDFFormFieldSignature*>(formField); });
    QVERIFY(field);
    const pdf::PDFDictionary* fieldDictionary = signedDocument->getDictionaryFromObject(signedDocument->getObjectByReference(field->getSelfReference()));
    QCOMPARE(pdf::PDFEncoding::convertTextString(fieldDictionary->get("TU").getString()), QStringLiteral("Digital signature of Jane Novak"));
    const pdf::PDFDictionary* signature = signedDocument->getDictionaryFromObject(fieldDictionary->get("V"));
    QCOMPARE(signature->get("SubFilter").getString(), subFilter);
    QCOMPARE(pdf::PDFEncoding::convertTextString(signedDocument->getObject(signature->get("Reason")).getString()), QStringLiteral("Approval"));

    pdf::PDFDocumentDataLoaderDecorator loader(signedDocument.data());
    const QRectF rectangle = loader.readRectangle(fieldDictionary->get("Rect"), QRectF());
    QCOMPARE(rectangle, isVisible ? pdf::PDFPageDrawing({ text }).getBoundingBox(1) : QRectF());
    QCOMPARE(fieldDictionary->get("P").getReference(), signedDocument->getCatalog()->getPage(isVisible ? 1 : 0)->getPageReference());

    // The widget is an annotation of that page (Table 166 /P, 12.7.5.5): /Annots of the page object
    const std::vector<pdf::PDFObjectReference>& annotations = signedDocument->getCatalog()->getPage(isVisible ? 1 : 0)->getAnnotations();
    QVERIFY(std::find(annotations.cbegin(), annotations.cend(), field->getSelfReference()) != annotations.cend());

    // The new signature verifies over the written bytes
    pdf::PDFSignatureHandler::Parameters verification;
    verification.useSystemCertificateStore = false;
    const std::vector<pdf::PDFSignatureVerificationResult> results = pdf::PDFSignatureHandler::verifySignatures(form, output.writtenData, verification);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
}

void DocumentToolSignatureTest::signIntoField()
{
    // Q6.3: the value of the empty field is the new signature, no new field; the appearance fills
    // /Rect of its widget; bit 9 is enough (Table 22)
    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    const QByteArray data = createFieldDocument();
    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };
    const size_t fieldCount = Tool::getEmptySignatureFields(context.getDocument()).size();

    pdf::PDFPageDrawing::Element dot;
    dot.kind = pdf::PDFPageDrawing::Kind::Dot;
    dot.rectangle = QRectF(10, 10, 20, 20);
    QVariantMap parameters = createSignParameters(entry, password);
    parameters.insert(QStringLiteral("field"), QStringLiteral("Empty"));
    parameters.insert(QStringLiteral("appearance"), QStringLiteral("visible"));
    parameters.insert(QStringLiteral("drawing"), QVariant::fromValue(pdf::PDFPageDrawing({ dot })));

    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(findFact(result, "type")->value, QStringLiteral("Signature · in field Empty"));

    const pdf::PDFDocumentPointer signedDocument = read(result.documents.front().writtenData);
    const std::vector<Tool::EmptySignatureField> fields = Tool::getEmptySignatureFields(signedDocument.data());
    QCOMPARE(fields.size(), fieldCount - 1);
    QVERIFY(std::none_of(fields.cbegin(), fields.cend(), [](const Tool::EmptySignatureField& field) { return field.name == QLatin1String("Empty"); }));

    const pdf::PDFForm form = pdf::PDFForm::parse(signedDocument.data(), signedDocument->getCatalog()->getFormObject());
    const pdf::PDFFormFieldSignature* signedField = nullptr;
    form.apply([&signedField](const pdf::PDFFormField* field)
    {
        if (field->getName(pdf::PDFFormField::NameType::FullyQualified) == QLatin1String("Empty"))
        {
            signedField = dynamic_cast<const pdf::PDFFormFieldSignature*>(field);
        }
    });
    QVERIFY(signedField && !signedField->getSignature().getContents().isEmpty());
    const pdf::PDFDictionary* widget = signedDocument->getDictionaryFromObject(signedDocument->getObjectByReference(signedField->getSelfReference()));
    QVERIFY(signedDocument->getDictionaryFromObject(widget->get("AP")));
    pdf::PDFDocumentDataLoaderDecorator loader(signedDocument.data());
    QCOMPARE(loader.readRectangle(widget->get("Rect"), QRectF()), QRectF(100, 100, 200, 50));
}

void DocumentToolSignatureTest::signTaggedDocument()
{
    // Q6.18, ISO 14289-1 7.18.1, ISO 32000-2 14.7.4.4: the widget of a visible signature of a tagged
    // document is the content of a /Form element (an /OBJR), its /StructParent a new key of /ParentTree
    using pdf::PDFObject;
    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFObjectReference root = builder.addObject(dictionary({ { "Type", PDFObject::createName("StructTreeRoot") } }));
    const pdf::PDFObjectReference paragraph = builder.addObject(dictionary({ { "Type", PDFObject::createName("StructElem") }, { "S", PDFObject::createName("P") }, { "Pg", PDFObject::createReference(page) },
                                                                             { "K", PDFObject::createInteger(0) } }));
    const pdf::PDFObjectReference documentElement = builder.addObject(dictionary({ { "Type", PDFObject::createName("StructElem") }, { "S", PDFObject::createName("Document") },
                                                                                   { "P", PDFObject::createReference(root) }, { "K", array({ PDFObject::createReference(paragraph) }) } }));
    builder.mergeTo(root, dictionary({ { "K", PDFObject::createReference(documentElement) },
                                       { "ParentTree", dictionary({ { "Nums", array({ PDFObject::createInteger(0), array({ PDFObject::createReference(paragraph) }) }) } }) },
                                       { "ParentTreeNextKey", PDFObject::createInteger(1) } }));
    builder.mergeTo(builder.getCatalogReference(), dictionary({ { "StructTreeRoot", PDFObject::createReference(root) }, { "MarkInfo", dictionary({ { "Marked", PDFObject::createBool(true) } }) } }));
    const QByteArray data = write(builder.build());

    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };
    pdf::PDFPageDrawing::Element dot;
    dot.kind = pdf::PDFPageDrawing::Kind::Dot;
    dot.rectangle = QRectF(10, 10, 20, 20);
    QVariantMap parameters = createSignParameters(entry, password);
    parameters.insert(QStringLiteral("appearance"), QStringLiteral("visible"));
    parameters.insert(QStringLiteral("drawing"), QVariant::fromValue(pdf::PDFPageDrawing({ dot })));

    const pdf::PDFDocumentToolResult result = Tool(Tool::Mode::Sign).run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocumentPointer document = read(result.documents.front().writtenData);

    // The /Form element is the last kid of /Document, its content the widget on its page
    const pdf::PDFDictionary* documentDictionary = document->getDictionaryFromObject(document->getObjectByReference(documentElement));
    const PDFObject kids = document->getObject(documentDictionary->get("K"));
    QCOMPARE(kids.getArray()->getCount(), size_t(2));
    const pdf::PDFDictionary* form = document->getDictionaryFromObject(kids.getArray()->getItem(1));
    QCOMPARE(form->get("S").getString(), QByteArray("Form"));
    QCOMPARE(form->get("P").getReference(), documentElement);
    const pdf::PDFDictionary* objectReference = document->getDictionaryFromObject(form->get("K"));
    QCOMPARE(objectReference->get("Type").getString(), QByteArray("OBJR"));
    const pdf::PDFObjectReference widget = objectReference->get("Obj").getReference();
    QCOMPARE(objectReference->get("Pg").getReference(), page);

    const pdf::PDFDictionary* widgetDictionary = document->getDictionaryFromObject(document->getObjectByReference(widget));
    QCOMPARE(widgetDictionary->get("Subtype").getString(), QByteArray("Widget"));
    QCOMPARE(widgetDictionary->get("StructParent").getInteger(), pdf::PDFInteger(1));

    const pdf::PDFDictionary* rootDictionary = document->getDictionaryFromObject(document->getObjectByReference(root));
    QCOMPARE(document->getObject(rootDictionary->get("ParentTreeNextKey")).getInteger(), pdf::PDFInteger(2));
    const std::vector<std::pair<pdf::PDFInteger, PDFObject>> entries = pdf::PDFObjectUtils::readNumberTree(&document->getStorage(), rootDictionary->get("ParentTree"));
    QCOMPARE(entries.size(), size_t(2));
    QCOMPARE(entries.back().first, pdf::PDFInteger(1));
    QVERIFY(document->getDictionaryFromObject(entries.back().second) == form);
}

void DocumentToolSignatureTest::earlierSignatures()
{
    // 4.1 step 5: the earlier signatures stay valid after the update (the fact Earlier signatures);
    // the unsaved changes of a signed file are included (Q6.17) in their own revision (R6-4)
    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    const QByteArray data = createDocument(1);
    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };
    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolResult first = tool.run(context, createSignParameters(entry, password), nullptr, nullptr);
    QVERIFY2(first.isSuccess(), qPrintable(first.message));
    const QByteArray signedData = first.documents.front().writtenData;

    // A signature over the saved file: the earlier one stays valid (only a signature was added, R6-3)
    pdf::PDFDocumentToolContext savedContext = createContext(signedData);
    savedContext.userCertificates = { entry };
    const pdf::PDFDocumentToolResult saved = tool.run(savedContext, createSignParameters(entry, password), nullptr, nullptr);
    QVERIFY2(saved.isSuccess(), qPrintable(saved.message));
    QCOMPARE(findFact(saved, "earlierSignatures")->value, QStringLiteral("Stays valid"));
    QCOMPARE(saved.data.value(QStringLiteral("earlierSignatures")).toArray(), QJsonArray({ QStringLiteral("valid") }));
    QVERIFY(!saved.data.value(QStringLiteral("unsavedChanges")).toBool());
    QCOMPARE(saved.documents.front().writtenData.mid(signedData.size()).count("%%EOF"), 1);

    // An unsaved change of the tab: its own revision before the signature (R6-4); the earlier
    // signature reports it as a change after the signing (R6-3) - never "Stays valid"
    const pdf::PDFDocumentPointer signedDocument = read(signedData);
    pdf::PDFDocumentBuilder edit(signedDocument.data());
    edit.setPageRotation(signedDocument->getCatalog()->getPage(0)->getPageReference(), pdf::PageRotation::Rotate90);
    pdf::PDFDocumentToolContext secondContext = pdf::PDFDocumentToolContext::create(
        pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(edit.build())), signedData, QStringLiteral("contract.pdf"), true));
    secondContext.userCertificates = { entry };

    const pdf::PDFDocumentToolResult second = tool.run(secondContext, createSignParameters(entry, password), nullptr, nullptr);
    QVERIFY2(second.isSuccess(), qPrintable(second.message));
    QCOMPARE(findFact(second, "earlierSignatures")->value, QStringLiteral("1 reports the unsaved changes"));
    QCOMPARE(second.data.value(QStringLiteral("earlierSignatures")).toArray(), QJsonArray({ QStringLiteral("changed") }));
    QVERIFY(second.data.value(QStringLiteral("unsavedChanges")).toBool());
    QVERIFY(findFact(second, "unsavedChanges"));
    QCOMPARE(second.documents.front().writtenData.mid(signedData.size()).count("%%EOF"), 2);
}

void DocumentToolSignatureTest::optionTextsOfFigma()
{
    // 434:5308 (Signature type, Format) and 434:5327 (Appearance): the title, the description and the
    // icon of every option, as the menus show them
    using Tool = pdf::PDFDocumentToolSignature;
    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    const auto findOption = [&definitions](const char* parameter, const char* option) -> std::optional<pdf::PDFDocumentToolParameterOption>
    {
        for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
        {
            if (definition.getName() == QLatin1String(parameter))
            {
                for (const pdf::PDFDocumentToolParameterOption& item : definition.getOptions())
                {
                    if (item.id == QLatin1String(option))
                    {
                        return item;
                    }
                }
            }
        }
        return std::nullopt;
    };

    struct Expected
    {
        const char* parameter;
        const char* option;
        const char* title;
        const char* description;
        const char* icon;
    };
    const Expected expected[] = {
        { "type", "signature", "Signature", "Signed with your certificate, time taken from this computer", "sign-certificate" },
        { "type", "signature-timestamp", "Signature with timestamp", "A timestamp authority also confirms the signing time", "timestamp" },
        { "format", "pades", "PAdES", "The current standard for signatures in PDF, recommended", "sig-format-pades" },
        { "format", "pkcs7", "Compatible (PKCS#7)", "For older software that cannot read PAdES", "sig-format-pkcs7" },
        { "appearance", "visible", "Visible", "The drawn appearance is placed on the page", "eye" },
        { "appearance", "invisible", "Invisible", "Recorded in the document only, nothing on the page", "eye-off" }
    };
    for (const Expected& item : expected)
    {
        const std::optional<pdf::PDFDocumentToolParameterOption> option = findOption(item.parameter, item.option);
        QVERIFY2(option, item.option);
        QCOMPARE(option->getTitle(), QString::fromUtf8(item.title));
        QCOMPARE(option->getDescription(), QString::fromUtf8(item.description));
        QCOMPARE(option->iconName, QString::fromLatin1(item.icon));
    }
}

void DocumentToolSignatureTest::timeFactWithoutLocale()
{
    // O17: the core gives the time as data (ISO 8601 UTC) and its fact in the form of 434:9276
    // ("Sep 30, 2026 · 14:32:08 UTC") independent of the default locale of the process - the new
    // application formats it by PDFAppLocale, MCP and PdfTool keep the text of the core
    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    const QLocale previous;
    QLocale::setDefault(QLocale(QLocale::Czech, QLocale::Czechia));
    pdf::PDFDocumentToolContext context = createContext(createDocument(1));
    context.userCertificates = { entry };
    const pdf::PDFDocumentToolSignature tool(pdf::PDFDocumentToolSignature::Mode::Sign);
    const pdf::PDFDocumentToolResult result = tool.run(context, createSignParameters(entry, password), nullptr, nullptr);
    QLocale::setDefault(previous);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QDateTime time = QDateTime::fromString(result.data.value(QStringLiteral("signingTime")).toString(), Qt::ISODate);
    QVERIFY(time.isValid());
    QCOMPARE(time.offsetFromUtc(), 0);
    static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const QString expected = QStringLiteral("%1 %2, %3 · %4 UTC").arg(QString::fromLatin1(months[time.date().month() - 1])).arg(time.date().day()).arg(time.date().year())
                                                                .arg(time.time().toString(QStringLiteral("HH:mm:ss")));
    QCOMPARE(findFact(result, "time")->value, expected);
}

void DocumentToolSignatureTest::signEncryptedDocument_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("rc4-open-password") << int(pdf::PDFSecurityHandlerFactory::RC4);
    QTest::newRow("aes256-open-password") << int(pdf::PDFSecurityHandlerFactory::AES_256);
}

void DocumentToolSignatureTest::signEncryptedDocument()
{
    // R6-1 (full): a document with an open password is signed - the original bytes, the signed file and
    // its revisions are read by a clone of the authenticated handler of the tab (no password is kept);
    // a second signature classifies the first one by its revisions (R6-3): it stays valid
    QFETCH(int, algorithm);

    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::Algorithm(algorithm);
    settings.userPassword = QStringLiteral("user");
    settings.ownerPassword = QStringLiteral("owner");
    settings.permissions = 0xFFFFFFFF;
    settings.id = builder.ensureDocumentId();
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const QByteArray data = write(builder.build());
    QVERIFY(!data.isEmpty());

    const auto readWithPassword = [](const QByteArray& bytes)
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = true; return QStringLiteral("user"); }, false, false);
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(reader.readFromBuffer(bytes)));
    };

    // The tab has opened the file by its open password
    const Tool tool(Tool::Mode::Sign);
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(readWithPassword(data), data, QStringLiteral("contract.pdf"), false));
    context.userCertificates = { entry };
    const pdf::PDFDocumentToolResult first = tool.run(context, createSignParameters(entry, password), nullptr, nullptr);
    QVERIFY2(first.isSuccess(), qPrintable(first.message));
    const QByteArray signedData = first.documents.front().writtenData;
    QVERIFY(signedData.startsWith(data));

    // The signed copy keeps the encryption (ISO 32000-2, 7.5.6: an update cannot change it): it opens only by the password
    pdf::PDFDocumentReader noPassword(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    noPassword.readFromBuffer(signedData);
    QCOMPARE(noPassword.getReadingResult(), pdf::PDFDocumentReader::Result::Canceled);

    // A second signature over the signed copy (its tab opened by the password): the first one stays valid
    pdf::PDFDocumentToolContext secondContext = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(readWithPassword(signedData), signedData, QStringLiteral("contract_SIGNED.pdf"), false));
    secondContext.userCertificates = { entry };
    const pdf::PDFDocumentToolResult second = tool.run(secondContext, createSignParameters(entry, password), nullptr, nullptr);
    QVERIFY2(second.isSuccess(), qPrintable(second.message));
    QCOMPARE(findFact(second, "earlierSignatures")->value, QStringLiteral("Stays valid"));
    QCOMPARE(second.data.value(QStringLiteral("earlierSignatures")).toArray(), QJsonArray({ QStringLiteral("valid") }));

    // The verifier with the handler of the document classifies the revision of the second signature as
    // allowed (only a signature was added); without it the encrypted revisions cannot be read - a change
    const QByteArray twiceSigned = second.documents.front().writtenData;
    const pdf::PDFDocumentPointer twiceDocument = readWithPassword(twiceSigned);
    const pdf::PDFForm form = pdf::PDFForm::parse(twiceDocument.data(), twiceDocument->getCatalog()->getFormObject());
    pdf::PDFSignatureHandler::Parameters verification;
    verification.useSystemCertificateStore = false;
    verification.verifyCertificates = false;
    verification.securityHandler = twiceDocument->getStorage().getSecurityHandler();
    const std::vector<pdf::PDFSignatureVerificationResult> results = pdf::PDFSignatureHandler::verifySignatures(form, twiceSigned, verification);
    QCOMPARE(results.size(), size_t(2));
    const auto firstResult = std::find_if(results.cbegin(), results.cend(), [](const pdf::PDFSignatureVerificationResult& result) { return result.getLaterRevisions() != pdf::PDFSignatureVerificationResult::LaterRevisions::None; });
    QVERIFY(firstResult != results.cend());
    QVERIFY2(firstResult->isSignatureValid(), qPrintable(firstResult->getErrors().join('\n')));
    QCOMPARE(firstResult->getLaterRevisions(), pdf::PDFSignatureVerificationResult::LaterRevisions::Allowed);

    verification.securityHandler = nullptr;
    const std::vector<pdf::PDFSignatureVerificationResult> withoutHandler = pdf::PDFSignatureHandler::verifySignatures(form, twiceSigned, verification);
    QVERIFY(std::any_of(withoutHandler.cbegin(), withoutHandler.cend(), [](const pdf::PDFSignatureVerificationResult& result) { return result.getLaterRevisions() == pdf::PDFSignatureVerificationResult::LaterRevisions::Changed; }));
}

void DocumentToolSignatureTest::existingFieldPermissions_data()
{
    using Permission = pdf::PDFSecurityHandler::Permission;
    QTest::addColumn<uint32_t>("permissions");
    QTest::addColumn<bool>("isExistingFieldAllowed");
    QTest::addColumn<bool>("isNewFieldAllowed");

    // ISO 32000-2, Table 22: bit 9 "fill in existing interactive form fields (including signature
    // fields), even if bit 6 is clear"; bit 6 "add or modify text annotations, fill in interactive
    // form fields, and, if bit 4 is also set, create or modify interactive form fields (including
    // signature fields)"
    QTest::newRow("bit-9") << uint32_t(Permission::ModifyFormFields) << true << false;
    QTest::newRow("bit-6") << uint32_t(Permission::ModifyInteractiveItems) << true << false;
    QTest::newRow("bits-4-and-6") << (uint32_t(Permission::Modify) | uint32_t(Permission::ModifyInteractiveItems)) << true << true;
    QTest::newRow("bit-4") << uint32_t(Permission::Modify) << false << false;
    QTest::newRow("print-only") << uint32_t(Permission::PrintLowResolution) << false << false;
}

void DocumentToolSignatureTest::existingFieldPermissions()
{
    // RS6APP M10 / RS6SEC Low: the signature of an existing empty field needs bit 9 or bit 6 (any of
    // them), a new field bits 4 and 6 - the core validation, the signer and a whole run agree
    QFETCH(uint32_t, permissions);
    QFETCH(bool, isExistingFieldAllowed);
    QFETCH(bool, isNewFieldAllowed);

    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    // The document of the empty field "Empty" with no open password and the permissions of the row
    const pdf::PDFDocumentPointer plain = read(createFieldDocument());
    pdf::PDFDocumentBuilder builder(plain.data());
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = QStringLiteral("owner");
    settings.permissions = permissions;
    settings.id = builder.ensureDocumentId();
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const QByteArray data = write(builder.build());
    QVERIFY(!data.isEmpty());

    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };
    const pdf::PDFDocument* document = context.getDocument();
    QVERIFY(document && document->getStorage().getSecurityHandler()->getAuthorizationResult() == pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized);

    const Tool tool(Tool::Mode::Sign);
    QVariantMap existingField = createSignParameters(entry, password);
    existingField.insert(QStringLiteral("field"), QStringLiteral("Empty"));
    QCOMPARE(tool.isPermittedRun(document, existingField), isExistingFieldAllowed);
    QCOMPARE(tool.isPermittedRun(document, createSignParameters(entry, password)), isNewFieldAllowed);

    // A whole run into the field: the validation and the signer (PDFDocumentSigner::Parameters::anyOfPermissions)
    const pdf::PDFDocumentToolResult result = tool.run(context, existingField, nullptr, nullptr);
    QVERIFY2(result.isSuccess() == isExistingFieldAllowed, qPrintable(result.message));
    if (result.isSuccess())
    {
        QVERIFY(result.documents.front().writtenData.startsWith(data));
    }
}

void DocumentToolSignatureTest::signatureDateOfFormat_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<bool>("isClaimed");

    // ETSI EN 319 142-1 (PAdES): no signing-time attribute, the time is /M of the signature dictionary
    // (ISO 32000-2, Table 255) - claimed by the signer; the compatible PKCS#7 signs the attribute
    QTest::newRow("pades") << QStringLiteral("pades") << true;
    QTest::newRow("pkcs7") << QStringLiteral("pkcs7") << false;
}

void DocumentToolSignatureTest::signatureDateOfFormat()
{
    // RS6FIGMA L-M1: the verifier gives a PAdES signature its date of /M, marked as claimed by the signer
    // (not a timestamp); a signing-time attribute is the date covered by the signature
    QFETCH(QString, format);
    QFETCH(bool, isClaimed);

    pdf::PDFCertificateEntry entry;
    QString password;
    QVERIFY2(createCertificate(&entry, &password), "the key of P1b");

    const QByteArray data = createDocument(1);
    pdf::PDFDocumentToolContext context = createContext(data);
    context.userCertificates = { entry };
    QVariantMap parameters = createSignParameters(entry, password);
    parameters.insert(QStringLiteral("format"), format);

    const Tool tool(Tool::Mode::Sign);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray signedData = result.documents.front().writtenData;
    const QDateTime signingTime = QDateTime::fromString(result.data.value(QStringLiteral("signingTime")).toString(), Qt::ISODate);
    QVERIFY(signingTime.isValid());

    const pdf::PDFDocumentPointer document = read(signedData);
    const pdf::PDFForm form = pdf::PDFForm::parse(document.data(), document->getCatalog()->getFormObject());
    pdf::PDFSignatureHandler::Parameters verification;
    verification.useSystemCertificateStore = false;
    verification.verifyCertificates = false;
    const std::vector<pdf::PDFSignatureVerificationResult> results = pdf::PDFSignatureHandler::verifySignatures(form, signedData, verification);
    QCOMPARE(results.size(), size_t(1));
    const pdf::PDFSignatureVerificationResult& signature = results.front();
    QVERIFY(signature.getSignatureDate().isValid());
    QVERIFY(!signature.getTimestampDate().isValid());
    QCOMPARE(signature.isSignatureDateClaimed(), isClaimed);
    QCOMPARE(signature.getSignatureDateSource(), isClaimed ? pdf::PDFSignatureVerificationResult::SignatureDateSource::SignatureDictionary
                                                           : pdf::PDFSignatureVerificationResult::SignatureDateSource::SigningTimeAttribute);

    // The time of the signing (/M is written in whole seconds, Table 255 / 7.9.4; the attribute by the clock of the signer)
    QVERIFY2(qAbs(signature.getSignatureDate().secsTo(signingTime)) <= 60, qPrintable(signature.getSignatureDate().toString(Qt::ISODate)));
    if (isClaimed)
    {
        QCOMPARE(signature.getSignatureDate().toSecsSinceEpoch(), signingTime.toSecsSinceEpoch());
    }
}

QTEST_MAIN(DocumentToolSignatureTest)

#include "tst_documenttoolsignaturetest.moc"
