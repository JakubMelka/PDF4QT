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

// The verification of signatures (step 6 of phase 2, plan 5.1, 6.7, 7.2, 9 with R6-2, R6-3, R6-22;
// package P1a): the shapes of /ByteRange (B109), the content of the gap (R6-22), the signer by its
// SignerInfo (R6-2), the classification of the later revisions (Q6.5, R6-3: our Timestamp an
// information, Sneaky Signature and Evil Annotation attacks a warning), the certifications (DocMDP
// P = 1, P = 2), the limits of 7.2. The keys, the certificates, the signatures (CMS of OpenSSL), the
// timestamp tokens (TS_RESP_CTX of OpenSSL, a local authority) and the files are made by the test at
// run time; the expected values come from the text of the file and from ISO 32000-2, never from a run.
//
// Environment: PDF4QT_TOOL_CORPUS_DIRECTORY=<folder> the files of the corpus (the default folder is
// <source>/testpdf, the test corpusAttacks reads only Signature/invalid of it).

#include "pdfcatalog.h"
#include "pdfcertificatestore.h"
#include "pdfdocumentreader.h"
#include "pdfform.h"
#include "pdfsignaturehandler.h"

#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimeZone>

#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ts.h>
#include <openssl/x509v3.h>

#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

using namespace pdf;

/// A key and its certificate made by the test
struct TestIdentity
{
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;
};

/// Options of a certificate made by the test
struct TestCertificateOptions
{
    bool isAuthority = false;
    QByteArray keyUsage = "critical,digitalSignature,nonRepudiation";
    QByteArray extendedKeyUsage;
};

/// Options of a CMS signature made by the test
struct TestSignOptions
{
    const TestIdentity* signer = nullptr;
    std::vector<X509*> bag;                 ///< The certificates of the signature in this order (the signer is not added by itself)
    const EVP_MD* digest = nullptr;         ///< SHA-256, when null
    bool useKeyIdentifier = false;          ///< The SignerInfo names the subject key identifier (RFC 5652, 5.3)
    bool usePss = false;                    ///< RSASSA-PSS
    bool noAttributes = false;              ///< No signed attributes
    bool encapsulateSha1 = false;           ///< The content is the SHA-1 digest of the data (adbe.pkcs7.sha1)
    bool useIndefiniteLength = false;       ///< The outer SEQUENCE in BER of the indefinite length
    X509* claimedSigner = nullptr;          ///< The SignerInfo names the serial number of this certificate (R6-2)
};

/// The layout of a signature written by the test: its byte range and the bytes of its gap
struct TestSignLayout
{
    std::vector<qint64> byteRange;          ///< Written instead of [0 a b c] of the text of the file
    QByteArray gapSuffix;                   ///< Bytes written behind the signature in the gap (R6-22)
    bool corruptSignature = false;          ///< The signature is over other bytes than the byte range
};

/// A PDF file written by the test: a classic cross-reference table of each revision, /Prev of the later ones
class TestPdfFile
{
public:
    explicit TestPdfFile() :
        m_data("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n")
    {

    }

    /// Writes the objects (number, body) as a revision: an incremental update after the first one (ISO 32000-2, 7.5.6)
    void addRevision(const std::map<int, QByteArray>& objects)
    {
        for (const auto& [number, body] : objects)
        {
            m_offsets[number] = m_data.size();
            m_data += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
            m_size = qMax(m_size, number + 1);
        }

        const qsizetype xrefOffset = m_data.size();
        m_data += "xref\n";
        if (m_previousXRef < 0)
        {
            m_data += "0 " + QByteArray::number(m_size) + "\n";
            for (int i = 0; i < m_size; ++i)
            {
                m_data += objects.count(i) ? getEntry(m_offsets[i]) : QByteArray("0000000000 65535 f\r\n");
            }
        }
        else
        {
            for (const auto& item : objects)
            {
                m_data += QByteArray::number(item.first) + " 1\n" + getEntry(m_offsets[item.first]);
            }
        }

        m_data += "trailer\n<< /Size " + QByteArray::number(m_size) + " /Root 1 0 R" + m_trailerEntries;
        if (m_previousXRef >= 0)
        {
            m_data += " /Prev " + QByteArray::number(m_previousXRef);
        }
        m_data += " >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
        m_previousXRef = xrefOffset;
    }

    QByteArray& data() { return m_data; }
    qsizetype getObjectOffset(int number) const { return m_offsets.at(number); }

    /// Entries of the trailer of each revision written from now on (" /Info 60 0 R")
    void setTrailerEntries(const QByteArray& entries) { m_trailerEntries = entries; }

private:
    static QByteArray getEntry(qsizetype offset) { return QByteArray::number(offset).rightJustified(10, '0') + " 00000 n\r\n"; }

    QByteArray m_data;
    QByteArray m_trailerEntries;
    qsizetype m_previousXRef = -1;
    int m_size = 1;
    std::map<int, qsizetype> m_offsets;
};

/// Helpers of the test: keys, certificates, signatures, timestamp tokens, files
class SignatureVerificationTestHelper
{
public:
    SignatureVerificationTestHelper() = delete;

    template<typename T>
    using Pointer = std::unique_ptr<T, void(*)(T*)>;

    static void freeCertificates(STACK_OF(X509)* certificates) { sk_X509_pop_free(certificates, X509_free); }

    /// Creates a key: RSA 2048 or EC P-256
    static EVP_PKEY* createKey(bool isRsa)
    {
        return isRsa ? EVP_RSA_gen(2048) : EVP_EC_gen("P-256");
    }

    /// Creates a certificate of version 3 of the key, issued by the issuer (its name and key identifier) and
    /// signed by the signing key; self-signed, when the issuer is null
    static X509* createCertificate(EVP_PKEY* key, const char* commonName, X509* issuer, EVP_PKEY* signingKey, const TestCertificateOptions& options)
    {
        static std::atomic<long> serialNumber = 1000;

        X509* certificate = X509_new();
        X509_set_version(certificate, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(certificate), serialNumber++);
        X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
        X509_gmtime_adj(X509_getm_notAfter(certificate), 3600L * 24 * 365);
        X509_set_pubkey(certificate, key);

        X509_NAME* name = X509_get_subject_name(certificate);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(commonName), -1, -1, 0);
        X509_set_issuer_name(certificate, issuer ? X509_get_subject_name(issuer) : name);

        X509V3_CTX context;
        X509V3_set_ctx_nodb(&context);
        X509V3_set_ctx(&context, issuer ? issuer : certificate, certificate, nullptr, nullptr, 0);

        auto addExtension = [&context, certificate](int nid, const QByteArray& value)
        {
            if (X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.constData()))
            {
                X509_add_ext(certificate, extension, -1);
                X509_EXTENSION_free(extension);
            }
        };

        addExtension(NID_basic_constraints, options.isAuthority ? "critical,CA:TRUE" : "critical,CA:FALSE");
        addExtension(NID_key_usage, options.isAuthority ? QByteArray("critical,keyCertSign,cRLSign") : options.keyUsage);
        if (!options.extendedKeyUsage.isEmpty())
        {
            addExtension(NID_ext_key_usage, options.extendedKeyUsage);
        }
        addExtension(NID_subject_key_identifier, "hash");
        if (issuer)
        {
            addExtension(NID_authority_key_identifier, "keyid:always");
        }

        X509_sign(certificate, signingKey ? signingKey : key, EVP_sha256());
        return certificate;
    }

    static TestIdentity createIdentity(const char* commonName, const TestIdentity* issuer, const TestCertificateOptions& options, bool isRsa = false)
    {
        TestIdentity identity;
        identity.key = createKey(isRsa);
        identity.certificate = createCertificate(identity.key, commonName, issuer ? issuer->certificate : nullptr, issuer ? issuer->key : nullptr, options);
        return identity;
    }

    static void freeIdentity(TestIdentity& identity)
    {
        X509_free(identity.certificate);
        EVP_PKEY_free(identity.key);
        identity = TestIdentity();
    }

    static QByteArray getDer(X509* certificate)
    {
        const int length = i2d_X509(certificate, nullptr);
        QByteArray der(length, 0);
        unsigned char* buffer = reinterpret_cast<unsigned char*>(der.data());
        i2d_X509(certificate, &buffer);
        return der;
    }

    /// Creates a CMS SignedData of the data (RFC 5652): detached, or the SHA-1 digest of the data encapsulated
    static QByteArray signCms(const QByteArray& data, const TestSignOptions& options)
    {
        unsigned int flags = CMS_BINARY | CMS_PARTIAL | CMS_NOCERTS;
        QByteArray content = data;
        if (options.encapsulateSha1)
        {
            content = QByteArray(SHA_DIGEST_LENGTH, 0);
            SHA1(reinterpret_cast<const unsigned char*>(data.constData()), size_t(data.size()), reinterpret_cast<unsigned char*>(content.data()));
        }
        else
        {
            flags |= CMS_DETACHED;
        }
        Pointer<CMS_ContentInfo> cms(CMS_sign(nullptr, nullptr, nullptr, nullptr, flags), CMS_ContentInfo_free);
        for (X509* certificate : options.bag)
        {
            CMS_add1_cert(cms.get(), certificate);
        }

        unsigned int signerFlags = CMS_BINARY | CMS_NOCERTS;
        signerFlags |= options.useKeyIdentifier ? CMS_USE_KEYID : 0;
        signerFlags |= options.noAttributes ? CMS_NOATTR : 0;
        signerFlags |= options.usePss ? CMS_KEY_PARAM : 0;
        CMS_SignerInfo* signerInfo = CMS_add1_signer(cms.get(), options.signer->certificate, options.signer->key, options.digest ? options.digest : EVP_sha256(), signerFlags);
        if (!signerInfo)
        {
            return QByteArray();
        }

        if (options.usePss)
        {
            EVP_PKEY_CTX_set_rsa_padding(CMS_SignerInfo_get0_pkey_ctx(signerInfo), RSA_PKCS1_PSS_PADDING);
        }

        Pointer<BIO> contentBuffer(BIO_new_mem_buf(content.constData(), int(content.size())), BIO_free_all);
        if (CMS_final(cms.get(), contentBuffer.get(), nullptr, flags) <= 0)
        {
            return QByteArray();
        }

        if (options.claimedSigner)
        {
            // The SignerInfo names another certificate of the same issuer: the identifier is not signed
            ASN1_OCTET_STRING* keyIdentifier = nullptr;
            X509_NAME* issuer = nullptr;
            ASN1_INTEGER* serialNumber = nullptr;
            CMS_SignerInfo_get0_signer_id(signerInfo, &keyIdentifier, &issuer, &serialNumber);
            ASN1_STRING_copy(serialNumber, X509_get0_serialNumber(options.claimedSigner));
        }

        const int length = i2d_CMS_ContentInfo(cms.get(), nullptr);
        QByteArray der(length, 0);
        unsigned char* buffer = reinterpret_cast<unsigned char*>(der.data());
        i2d_CMS_ContentInfo(cms.get(), &buffer);

        if (options.useIndefiniteLength)
        {
            // BER: the SEQUENCE of ContentInfo of the indefinite length, closed by the end-of-contents octets
            // (X.690, 8.1.3.6); the signed part inside is unchanged
            const qsizetype headerLength = static_cast<unsigned char>(der[1]) < 0x80 ? 2 : 2 + (static_cast<unsigned char>(der[1]) & 0x7F);
            return QByteArray("\x30\x80", 2) + der.mid(headerLength) + QByteArray(2, '\0');
        }
        return der;
    }

    /// Creates a timestamp token of RFC 3161 over the data by the local authority (SHA-256 imprint, the certificate
    /// of the authority and its root in the token)
    static QByteArray createTimestampToken(const QByteArray& data, const TestIdentity& authority, X509* root)
    {
        std::array<unsigned char, SHA256_DIGEST_LENGTH> digest = { };
        SHA256(reinterpret_cast<const unsigned char*>(data.constData()), size_t(data.size()), digest.data());

        Pointer<TS_REQ> request(TS_REQ_new(), TS_REQ_free);
        Pointer<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new(), TS_MSG_IMPRINT_free);
        Pointer<X509_ALGOR> algorithm(X509_ALGOR_new(), X509_ALGOR_free);
        X509_ALGOR_set0(algorithm.get(), OBJ_nid2obj(NID_sha256), V_ASN1_NULL, nullptr);
        TS_MSG_IMPRINT_set_algo(imprint.get(), algorithm.get());
        TS_MSG_IMPRINT_set_msg(imprint.get(), digest.data(), int(digest.size()));
        TS_REQ_set_version(request.get(), 1);
        TS_REQ_set_msg_imprint(request.get(), imprint.get());
        TS_REQ_set_cert_req(request.get(), 1);

        Pointer<BIO> requestBuffer(BIO_new(BIO_s_mem()), BIO_free_all);
        i2d_TS_REQ_bio(requestBuffer.get(), request.get());

        Pointer<TS_RESP_CTX> context(TS_RESP_CTX_new(), TS_RESP_CTX_free);
        Pointer<ASN1_OBJECT> policy(OBJ_txt2obj("1.2.3.4.1", 1), ASN1_OBJECT_free);
        Pointer<STACK_OF(X509)> chain(sk_X509_new_null(), &SignatureVerificationTestHelper::freeCertificates);
        X509_up_ref(root);
        sk_X509_push(chain.get(), root);
        TS_RESP_CTX_set_signer_cert(context.get(), authority.certificate);
        TS_RESP_CTX_set_signer_key(context.get(), authority.key);
        TS_RESP_CTX_set_def_policy(context.get(), policy.get());
        TS_RESP_CTX_add_md(context.get(), EVP_sha256());
        TS_RESP_CTX_set_certs(context.get(), chain.get());

        Pointer<TS_RESP> response(TS_RESP_create_response(context.get(), requestBuffer.get()), TS_RESP_free);
        PKCS7* token = response ? TS_RESP_get_token(response.get()) : nullptr;
        if (!token)
        {
            return QByteArray();
        }

        const int length = i2d_PKCS7(token, nullptr);
        QByteArray der(length, 0);
        unsigned char* buffer = reinterpret_cast<unsigned char*>(der.data());
        i2d_PKCS7(token, &buffer);
        return der;
    }

    /// A signature dictionary with the placeholders of /ByteRange and /Contents (ISO 32000-2, Table 255)
    static QByteArray signatureDictionary(const QByteArray& subFilter, const QByteArray& type = "Sig", const QByteArray& extra = QByteArray(), int contentsSize = 8192)
    {
        return "<< /Type /" + type + " /Filter /Adobe.PPKLite /SubFilter /" + subFilter +
               " /ByteRange [0 0000000000 0000000000 0000000000" + QByteArray(64, ' ') + "] /Contents <" + QByteArray(2 * contentsSize, '0') + ">" + extra + " >>";
    }

    /// A stream object
    static QByteArray stream(const QByteArray& dictionary, const QByteArray& content)
    {
        return "<< " + dictionary + " /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream";
    }

    /// An appearance stream of a widget (a form XObject)
    static QByteArray appearance(const QByteArray& content)
    {
        return stream("/Type /XObject /Subtype /Form /BBox [0 0 100 100]", content);
    }

    /// Signs the signature dictionary of the object (written by signatureDictionary): writes /ByteRange - [0 a b c]
    /// computed from the text of the file (a = '<' of /Contents, b = behind its '>', c = the rest; ISO 32000-2,
    /// 12.8.1), or the values of the layout - and the hexadecimal signature of the bytes of the byte range into
    /// /Contents. Returns false, when it does not fit.
    static bool sign(TestPdfFile& file, int objectNumber, const std::function<QByteArray(const QByteArray&)>& signer, const TestSignLayout& layout = TestSignLayout())
    {
        QByteArray& data = file.data();
        const qsizetype objectOffset = file.getObjectOffset(objectNumber);
        const qsizetype byteRangeStart = data.indexOf("/ByteRange [", objectOffset) + 12;
        const qsizetype byteRangeEnd = data.indexOf(']', byteRangeStart);
        const qsizetype a = data.indexOf("/Contents <", objectOffset) + 10;
        const qsizetype b = data.indexOf('>', a) + 1;

        const std::vector<qint64> byteRange = layout.byteRange.empty() ? std::vector<qint64>{ 0, a, b, data.size() - b } : layout.byteRange;
        QByteArray text;
        for (const qint64 value : byteRange)
        {
            text += QByteArray::number(value) + ' ';
        }
        text = text.trimmed();
        if (text.size() > byteRangeEnd - byteRangeStart)
        {
            return false;
        }
        data.replace(byteRangeStart, byteRangeEnd - byteRangeStart, text.leftJustified(byteRangeEnd - byteRangeStart, ' '));

        // The bytes of the declared ranges (the parts inside of the file)
        QByteArray signedData;
        for (size_t i = 0; i + 1 < byteRange.size(); i += 2)
        {
            const qint64 offset = qBound<qint64>(0, byteRange[i], data.size());
            const qint64 length = qBound<qint64>(0, byteRange[i + 1], data.size() - offset);
            signedData += data.mid(offset, length);
        }

        const QByteArray signature = signer(layout.corruptSignature ? signedData + "changed" : signedData);
        const QByteArray hexadecimal = (signature + layout.gapSuffix).toHex();
        if (signature.isEmpty() || hexadecimal.size() > b - a - 2)
        {
            return false;
        }
        data.replace(a + 1, hexadecimal.size(), hexadecimal);
        return true;
    }

    /// Reads the file and verifies its signatures, the root is the only trusted certificate
    static std::vector<PDFSignatureVerificationResult> verify(const QByteArray& data, X509* root, bool verifyCertificates = true)
    {
        PDFDocumentReader reader(nullptr, nullptr, true, false);
        const PDFDocument document = reader.readFromBuffer(data);
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
        {
            return { };
        }

        PDFCertificateStore store;
        if (root)
        {
            store.add(PDFCertificateEntry::EntryType::User, getDer(root));
        }

        PDFSignatureHandler::Parameters parameters;
        parameters.store = &store;
        parameters.dss = &document.getCatalog()->getDocumentSecurityStore();
        parameters.useSystemCertificateStore = false;
        parameters.aatlCertificates = PDFSignatureHandler::AATLCertificates::Never;
        parameters.verifyCertificates = verifyCertificates;

        const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
        return PDFSignatureHandler::verifySignatures(form, data, parameters);
    }

    /// Returns the result of the field of the name, or nullptr
    static const PDFSignatureVerificationResult* findResult(const std::vector<PDFSignatureVerificationResult>& results, const QString& name)
    {
        auto it = std::find_if(results.cbegin(), results.cend(), [&name](const PDFSignatureVerificationResult& result) { return result.getSignatureFieldQualifiedName() == name; });
        return it != results.cend() ? &*it : nullptr;
    }

    /// Returns the common names of the certificates of the result
    static QStringList getCommonNames(const PDFSignatureVerificationResult& result)
    {
        QStringList names;
        for (const PDFCertificateInfo& info : result.getCertificateInfos())
        {
            names << info.getName(PDFCertificateInfo::CommonName);
        }
        return names;
    }
};

class SignatureVerificationTest : public QObject
{
    Q_OBJECT

public:
    /// What a later revision of the file does (R6-3)
    enum class Scenario
    {
        None,                   ///< No later revision
        Timestamp,              ///< A document timestamp (an invisible signature field of ETSI.RFC3161)
        VisibleSignature,       ///< A visible approval signature in a new field
        DocumentSecurityStore,  ///< /DSS with a certificate
        SavedAgain,             ///< An object written again with the same value
        ContentChanged,         ///< The content stream of the page written again (incremental saving attack)
        SneakySignature,        ///< A new signature field without a signature, with its appearance (SSA)
        EvilAnnotation,         ///< A new FreeText annotation over the page (EAA)
        WidgetAppearance,       ///< The appearance of an existing widget changed, its value not
        FormResources,          ///< /DR of /AcroForm changed
        DanglingReference,      ///< An object, to which the signed revision refers, is created
        InvalidLaterSignature,  ///< A new signature field, whose signature is not of its bytes
        CatalogChanged,         ///< /OpenAction added to the catalog
        FormFilled,             ///< A text field filled in: its value and its appearance
        SharedFieldFilled,      ///< A field with two widgets filled in, their shared appearance written again
        ParentCycle,            ///< A field filled in, whose /Parent makes a cycle with its widget
        SignEmptyField,         ///< The empty signature field Signature0 signed: /V, /AP, /StructParent and its /Form (Q6.3, Q6.18)
        SignEmptyFieldMoved,    ///< The empty signature field signed and its /Rect changed
        TaggedSignature,        ///< A visible signature with its structure element /Form (Q6.18)
        TaggedForeignObject,    ///< A new element /Form, whose /OBJR is the widget of another field
        TaggedRoleMap,          ///< A visible signature with its /Form, the root of the structure got /RoleMap
        AcrobatSignature,       ///< A visible signature, /Info /ModDate and the XMP metadata updated (as Acrobat signs)
        AcrobatOtherInfoKey,    ///< The same with /Info /Producer changed too
        AcrobatTimestamp,       ///< A document timestamp, /Info /ModDate and the XMP metadata updated
        ModificationDateOnly    ///< /Info /ModDate updated, no signature
    };
    Q_ENUM(Scenario)

    /// A case of the layout of a signature in the file (B109, B111, R6-22, 7.2)
    enum class LayoutCase
    {
        Control,
        ThreeRanges,
        FirstOffsetNotZero,
        GapBeforeContents,
        OverlappingRanges,
        NegativeOffset,
        OutsideOfFile,
        OverflowingLength,
        NotEndOfRevision,
        DictionaryCopiedLater,
        DuplicateContents,
        NonZeroAfterSignature,
        SignatureTooLarge
    };
    Q_ENUM(LayoutCase)

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void validSignature_data();
    void validSignature();
    void byteRangeLayout_data();
    void byteRangeLayout();
    void signerBySignerInfo();
    void signerNotOfSignature();
    void certificatesNotVerified();
    void purposeOfDocumentSigning_data();
    void purposeOfDocumentSigning();
    void laterRevisions_data();
    void laterRevisions();
    void tooManySignatures();
    void tooManyCertificates();
    void documentSecurityStoreVri();
    void pdfToolSignerName();
    void pdfToolClaimedDate();
    void corpusAttacks();

private:
    /// The objects of the signed document: a page, its content, a signature field (Signature1, object 6, its
    /// dictionary 7), a text field (Name, 9) and a field of two widgets with a shared appearance (Shared, 11)
    std::map<int, QByteArray> createDocumentObjects(PDFInteger certification, bool hasDanglingReference) const;

    /// The page with the annotations
    static QByteArray page(const QByteArray& annotations, bool hasDanglingReference = false);

    /// The form with the fields
    static QByteArray form(const QByteArray& fields, const QByteArray& extra = QByteArray());

    /// The document information /Info (object 60) and the XMP metadata of the catalog (object 61)
    static QByteArray info(const QByteArray& modificationDate, const QByteArray& producer = "Test");
    static QByteArray metadata(const QByteArray& modifyDate);

    /// The catalog of the tagged document: a certification has /Perms /DocMDP (ISO 32000-2, 12.8.2.2.1)
    static QByteArray catalog(PDFInteger certification, const QByteArray& extra = QByteArray());

    /// Adds the structure of a visible signature in a tagged document (Q6.18): the element /Form of the object
    static void addFormStructure(std::map<int, QByteArray>& objects, int objectOfReference, const QByteArray& rootExtra = QByteArray());

    /// Creates a signed document (the signature of Signature1 ends its first revision)
    bool createSignedDocument(TestPdfFile& file, const QByteArray& subFilter, PDFInteger certification, bool hasDanglingReference) const;

    /// Adds the later revision of the scenario to the document of the certification
    bool addLaterRevision(TestPdfFile& file, Scenario scenario, PDFInteger certification) const;

    /// The signer of a CMS signature by the identity of the signer
    std::function<QByteArray(const QByteArray&)> getSigner(const TestIdentity& identity) const;

    TestIdentity m_root;
    TestIdentity m_signer;
    TestIdentity m_authority;
    TestIdentity m_victim;
    TestIdentity m_attacker;
    TestIdentity m_rsaSigner;
};

using Helper = SignatureVerificationTestHelper;

void SignatureVerificationTest::initTestCase()
{
    m_root = Helper::createIdentity("Test Root", nullptr, TestCertificateOptions{ true, QByteArray(), QByteArray() });
    m_signer = Helper::createIdentity("Test Signer", &m_root, TestCertificateOptions());
    m_victim = Helper::createIdentity("Victim Signer", &m_root, TestCertificateOptions());
    m_rsaSigner = Helper::createIdentity("Test RSA Signer", &m_root, TestCertificateOptions(), true);

    TestCertificateOptions authorityOptions;
    authorityOptions.keyUsage = "critical,digitalSignature";
    authorityOptions.extendedKeyUsage = "critical,timeStamping";
    m_authority = Helper::createIdentity("Test Timestamp Authority", &m_root, authorityOptions);

    // The attacker names the issuer of the victim, his certificate is signed by his own key
    m_attacker.key = Helper::createKey(false);
    m_attacker.certificate = Helper::createCertificate(m_attacker.key, "Attacker", m_root.certificate, m_attacker.key, TestCertificateOptions());

    QVERIFY(m_root.certificate && m_signer.certificate && m_victim.certificate && m_rsaSigner.certificate && m_authority.certificate && m_attacker.certificate);
}

void SignatureVerificationTest::cleanupTestCase()
{
    for (TestIdentity* identity : { &m_root, &m_signer, &m_authority, &m_victim, &m_attacker, &m_rsaSigner })
    {
        Helper::freeIdentity(*identity);
    }
}

std::function<QByteArray(const QByteArray&)> SignatureVerificationTest::getSigner(const TestIdentity& identity) const
{
    const TestIdentity* signer = &identity;
    X509* root = m_root.certificate;
    return [signer, root](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = signer;
        options.bag = { signer->certificate, root };
        return Helper::signCms(data, options);
    };
}

QByteArray SignatureVerificationTest::page(const QByteArray& annotations, bool hasDanglingReference)
{
    return "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents " + QByteArray(hasDanglingReference ? "[4 0 R 40 0 R]" : "4 0 R") +
           " /Annots [" + annotations + "] >>";
}

QByteArray SignatureVerificationTest::form(const QByteArray& fields, const QByteArray& extra)
{
    return "<< /Fields [" + fields + "] /SigFlags 3" + extra + " >>";
}

std::map<int, QByteArray> SignatureVerificationTest::createDocumentObjects(PDFInteger certification, bool hasDanglingReference) const
{
    std::map<int, QByteArray> objects;
    objects[1] = catalog(certification);
    objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    objects[3] = page("6 0 R 9 0 R 12 0 R 13 0 R 15 0 R", hasDanglingReference);
    objects[4] = Helper::stream(QByteArray(), "0 0 1 rg 20 20 100 100 re f");
    objects[5] = form("6 0 R 9 0 R 11 0 R 15 0 R");
    objects[15] = "<< /FT /Sig /T (Signature0) /Type /Annot /Subtype /Widget /Rect [10 60 100 80] /F 4 /P 3 0 R >>";
    objects[50] = "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /ParentTreeNextKey 1 >>";
    objects[51] = "<< /Type /StructElem /S /Document /P 50 0 R /K [] >>";
    objects[52] = "<< /Nums [] >>";
    objects[60] = info("D:20260101000000Z");
    objects[61] = metadata("2026-01-01T00:00:00Z");
    objects[6] ="<< /FT /Sig /T (Signature1) /V 7 0 R /Type /Annot /Subtype /Widget /Rect [0 0 0 0] /F 132 /P 3 0 R >>";
    objects[9] = "<< /FT /Tx /T (Name) /V (Alice) /Type /Annot /Subtype /Widget /Rect [10 150 100 170] /F 4 /P 3 0 R /AP << /N 10 0 R >> >>";
    objects[10] = Helper::appearance("0 0 0 rg 2 2 50 10 re f");
    objects[11] = "<< /FT /Tx /T (Shared) /V (A) /Kids [12 0 R 13 0 R] >>";
    objects[12] = "<< /Type /Annot /Subtype /Widget /Parent 11 0 R /Rect [10 120 100 140] /F 4 /P 3 0 R /AP << /N 14 0 R >> >>";
    objects[13] = "<< /Type /Annot /Subtype /Widget /Parent 11 0 R /Rect [10 90 100 110] /F 4 /P 3 0 R /AP << /N 14 0 R >> >>";
    objects[14] = Helper::appearance("0 0 0 rg 2 2 20 10 re f");
    return objects;
}

bool SignatureVerificationTest::createSignedDocument(TestPdfFile& file, const QByteArray& subFilter, PDFInteger certification, bool hasDanglingReference) const
{
    std::map<int, QByteArray> objects = createDocumentObjects(certification, hasDanglingReference);
    const QByteArray reference = certification ? " /Reference [<< /Type /SigRef /TransformMethod /DocMDP /TransformParams << /Type /TransformParams /P " +
                                                 QByteArray::number(certification) + " /V /1.2 >> >>]" : QByteArray();
    objects[7] = Helper::signatureDictionary(subFilter, "Sig", reference);
    file.setTrailerEntries(" /Info 60 0 R");
    file.addRevision(objects);
    return Helper::sign(file, 7, getSigner(m_signer));
}

QByteArray SignatureVerificationTest::info(const QByteArray& modificationDate, const QByteArray& producer)
{
    return "<< /Title (Contract) /Producer (" + producer + ") /CreationDate (D:20260101000000Z) /ModDate (" + modificationDate + ") >>";
}

QByteArray SignatureVerificationTest::metadata(const QByteArray& modifyDate)
{
    // ISO 32000-2, 14.3.2: the metadata stream of the catalog (XMP)
    return Helper::stream("/Type /Metadata /Subtype /XML", "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><xmp:ModifyDate>" + modifyDate + "</xmp:ModifyDate></x:xmpmeta>");
}

QByteArray SignatureVerificationTest::catalog(PDFInteger certification, const QByteArray& extra)
{
    return "<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R /StructTreeRoot 50 0 R /MarkInfo << /Marked true >> /Metadata 61 0 R" +
           QByteArray(certification ? " /Perms << /DocMDP 7 0 R >>" : "") + extra + " >>";
}

void SignatureVerificationTest::addFormStructure(std::map<int, QByteArray>& objects, int objectOfReference, const QByteArray& rootExtra)
{
    // ISO 32000-2, 14.7.4.4 and 14.7.5.4; ISO 14289-1, 7.18.1: the element /Form of the widget (its /OBJR), the
    // last kid of /Document, its key 1 in /ParentTree, /ParentTreeNextKey 2 (the widget has /StructParent 1)
    objects[50] = "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /ParentTreeNextKey 2" + rootExtra + " >>";
    objects[51] = "<< /Type /StructElem /S /Document /P 50 0 R /K [54 0 R] >>";
    objects[52] = "<< /Nums [1 54 0 R] >>";
    objects[54] = "<< /Type /StructElem /S /Form /P 51 0 R /Pg 3 0 R /K << /Type /OBJR /Obj " + QByteArray::number(objectOfReference) + " 0 R /Pg 3 0 R >> >>";
}

bool SignatureVerificationTest::addLaterRevision(TestPdfFile& file, Scenario scenario, PDFInteger certification) const
{
    const QByteArray annotations = "6 0 R 9 0 R 12 0 R 13 0 R 15 0 R";
    const QByteArray fields = "6 0 R 9 0 R 11 0 R 15 0 R";
    std::map<int, QByteArray> objects;

    switch (scenario)
    {
        case Scenario::None:
            return true;

        case Scenario::Timestamp:
        case Scenario::AcrobatTimestamp:
        {
            // ISO 32000-2, 12.8.5: /Type /DocTimeStamp, /SubFilter /ETSI.RFC3161, an invisible widget (Rect [0 0 0 0])
            if (scenario == Scenario::AcrobatTimestamp)
            {
                objects[60] = info("D:20260202000000Z");
                objects[61] = metadata("2026-02-02T00:00:00Z");
            }
            objects[3] = page(annotations + " 20 0 R");
            objects[5] = form(fields + " 20 0 R");
            objects[20] = "<< /FT /Sig /T (Timestamp1) /V 21 0 R /Type /Annot /Subtype /Widget /Rect [0 0 0 0] /F 132 /P 3 0 R >>";
            objects[21] = Helper::signatureDictionary("ETSI.RFC3161", "DocTimeStamp");
            file.addRevision(objects);
            const TestIdentity* authority = &m_authority;
            X509* root = m_root.certificate;
            return Helper::sign(file, 21, [authority, root](const QByteArray& data) { return Helper::createTimestampToken(data, *authority, root); });
        }

        case Scenario::VisibleSignature:
        case Scenario::InvalidLaterSignature:
        case Scenario::TaggedSignature:
        case Scenario::TaggedForeignObject:
        case Scenario::TaggedRoleMap:
        case Scenario::AcrobatSignature:
        case Scenario::AcrobatOtherInfoKey:
        {
            const bool isTagged = scenario == Scenario::TaggedSignature || scenario == Scenario::TaggedForeignObject || scenario == Scenario::TaggedRoleMap;
            if (scenario == Scenario::AcrobatSignature || scenario == Scenario::AcrobatOtherInfoKey)
            {
                // The owner decision of 15. 11. 2026: /ModDate of the same /Info and the XMP of the catalog may follow a signature
                objects[60] = info("D:20260202000000Z", scenario == Scenario::AcrobatOtherInfoKey ? "Other" : "Test");
                objects[61] = metadata("2026-02-02T00:00:00Z");
            }
            objects[3] = page(annotations + " 20 0 R");
            objects[5] = form(fields + " 20 0 R");
            objects[20] = "<< /FT /Sig /T (Signature2) /V 21 0 R /Type /Annot /Subtype /Widget /Rect [20 20 120 60] /F 4 /P 3 0 R /AP << /N 22 0 R >>" +
                          QByteArray(isTagged ? " /StructParent 1" : "") + " >>";
            objects[21] = Helper::signatureDictionary("ETSI.CAdES.detached");
            objects[22] = Helper::appearance("0 0 0 RG 5 5 m 90 30 l S");
            if (isTagged)
            {
                addFormStructure(objects, scenario == Scenario::TaggedForeignObject ? 9 : 20, scenario == Scenario::TaggedRoleMap ? QByteArray(" /RoleMap << /Sig /Form >>") : QByteArray());
            }
            file.addRevision(objects);

            TestSignLayout layout;
            layout.corruptSignature = scenario == Scenario::InvalidLaterSignature;
            return Helper::sign(file, 21, getSigner(m_signer), layout);
        }

        case Scenario::SignEmptyField:
        case Scenario::SignEmptyFieldMoved:
        {
            // ISO 32000-2, 12.7.5.5: the value /V of the existing field, its appearance; /SigFlags is 3 already
            const QByteArray rect = scenario == Scenario::SignEmptyField ? "[10 60 100 80]" : "[10 160 100 180]";
            objects[15] = "<< /FT /Sig /T (Signature0) /V 21 0 R /Type /Annot /Subtype /Widget /Rect " + rect + " /F 4 /P 3 0 R /AP << /N 22 0 R >> /StructParent 1 >>";
            objects[21] = Helper::signatureDictionary("ETSI.CAdES.detached");
            objects[22] = Helper::appearance("0 0 0 RG 5 5 m 90 15 l S");
            addFormStructure(objects, 15);
            file.addRevision(objects);
            return Helper::sign(file, 21, getSigner(m_signer));
        }

        case Scenario::DocumentSecurityStore:
            // ISO 32000-2, 12.8.4.3, Table 261: /Certs, an array of streams
            objects[1] = catalog(certification, " /DSS 30 0 R");
            objects[30] = "<< /Certs [31 0 R] >>";
            objects[31] = Helper::stream(QByteArray(), Helper::getDer(m_root.certificate));
            break;

        case Scenario::SavedAgain:
            objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
            break;

        case Scenario::ContentChanged:
            objects[4] = Helper::stream(QByteArray(), "1 0 0 rg 20 20 100 100 re f");
            break;

        case Scenario::SneakySignature:
            objects[3] = page(annotations + " 20 0 R");
            objects[5] = form(fields + " 20 0 R");
            objects[20] = "<< /FT /Sig /T (Signature2) /Type /Annot /Subtype /Widget /Rect [20 20 120 120] /F 4 /P 3 0 R /AP << /N 22 0 R >> >>";
            objects[22] = Helper::appearance("1 1 1 rg 0 0 100 100 re f");
            break;

        case Scenario::EvilAnnotation:
            objects[3] = page(annotations + " 23 0 R");
            objects[22] = Helper::appearance("1 1 1 rg 0 0 100 100 re f");
            objects[23] = "<< /Type /Annot /Subtype /FreeText /Rect [20 20 120 120] /Contents (Paid) /DA (/Helv 12 Tf 0 g) /F 4 /AP << /N 22 0 R >> >>";
            break;

        case Scenario::WidgetAppearance:
            objects[9] = "<< /FT /Tx /T (Name) /V (Alice) /Type /Annot /Subtype /Widget /Rect [10 150 100 170] /F 4 /P 3 0 R /AP << /N 24 0 R >> >>";
            objects[24] = Helper::appearance("1 0 0 rg 0 0 100 100 re f");
            break;

        case Scenario::FormResources:
            objects[5] = form(fields, " /DR << /Font << >> >>");
            break;

        case Scenario::DanglingReference:
            objects[40] = Helper::stream(QByteArray(), "1 0 0 rg 0 0 200 200 re f");
            break;

        case Scenario::ModificationDateOnly:
            objects[60] = info("D:20260202000000Z");
            break;

        case Scenario::CatalogChanged:
            objects[1] = catalog(certification, " /OpenAction [3 0 R /Fit]");
            break;

        case Scenario::FormFilled:
            objects[9] = "<< /FT /Tx /T (Name) /V (Bob) /Type /Annot /Subtype /Widget /Rect [10 150 100 170] /F 4 /P 3 0 R /AP << /N 24 0 R >> >>";
            objects[24] = Helper::appearance("0 0 0 rg 2 2 60 10 re f");
            break;

        case Scenario::SharedFieldFilled:
            // The appearance shared by both widgets of the field written again under its number
            objects[11] = "<< /FT /Tx /T (Shared) /V (B) /Kids [12 0 R 13 0 R] >>";
            objects[14] = Helper::appearance("0 0 0 rg 2 2 30 10 re f");
            break;

        case Scenario::ParentCycle:
            // The value filled in, the widgets name each other as their parent: the walk of the parents ends
            objects[11] = "<< /FT /Tx /T (Shared) /V (B) /Kids [12 0 R 13 0 R] >>";
            objects[12] = "<< /Type /Annot /Subtype /Widget /Parent 13 0 R /Rect [10 120 100 140] /F 4 /P 3 0 R /AP << /N 14 0 R >> >>";
            objects[13] = "<< /Type /Annot /Subtype /Widget /Parent 12 0 R /Rect [10 90 100 110] /F 4 /P 3 0 R /AP << /N 14 0 R >> >>";
            break;
    }

    file.addRevision(objects);
    return true;
}

void SignatureVerificationTest::validSignature_data()
{
    QTest::addColumn<QByteArray>("subFilter");
    QTest::addColumn<bool>("isRsa");
    QTest::addColumn<bool>("useKeyIdentifier");
    QTest::addColumn<bool>("usePss");
    QTest::addColumn<bool>("noAttributes");
    QTest::addColumn<bool>("isSha1");
    QTest::addColumn<bool>("isWeak");
    QTest::addColumn<bool>("isIndefiniteLength");

    // ISO 32000-2, 12.8.3.3 (adbe.pkcs7.detached, adbe.pkcs7.sha1), 12.8.3.4 (ETSI.CAdES.detached); RFC 5652, 5.3
    // (the SignerInfo by the issuer and the serial number or by the subject key identifier, signed attributes
    // optional); RSASSA-PSS (RFC 4056); SHA-1 is weak (B114); BER of the indefinite length written by real signers,
    // its end is the end of its parsed object (X.690, 8.1.3.6)
    QTest::newRow("cades") << QByteArray("ETSI.CAdES.detached") << false << false << false << false << false << false << false;
    QTest::newRow("pkcs7") << QByteArray("adbe.pkcs7.detached") << false << false << false << false << false << false << false;
    QTest::newRow("key-identifier") << QByteArray("ETSI.CAdES.detached") << false << true << false << false << false << false << false;
    QTest::newRow("rsa-pss") << QByteArray("ETSI.CAdES.detached") << true << false << true << false << false << false << false;
    QTest::newRow("no-signed-attributes") << QByteArray("adbe.pkcs7.detached") << true << false << false << true << false << false << false;
    QTest::newRow("sha1-digest") << QByteArray("adbe.pkcs7.detached") << false << false << false << false << true << true << false;
    QTest::newRow("encapsulated-sha1") << QByteArray("adbe.pkcs7.sha1") << false << false << false << false << false << true << false;
    QTest::newRow("ber-indefinite-length") << QByteArray("adbe.pkcs7.detached") << false << false << false << false << false << false << true;
}

void SignatureVerificationTest::validSignature()
{
    QFETCH(QByteArray, subFilter);
    QFETCH(bool, isRsa);
    QFETCH(bool, useKeyIdentifier);
    QFETCH(bool, usePss);
    QFETCH(bool, noAttributes);
    QFETCH(bool, isSha1);
    QFETCH(bool, isWeak);
    QFETCH(bool, isIndefiniteLength);

    const TestIdentity* signer = isRsa ? &m_rsaSigner : &m_signer;
    X509* root = m_root.certificate;
    const bool isEncapsulated = subFilter == "adbe.pkcs7.sha1";
    auto signerFunction = [=](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = signer;
        options.bag = { signer->certificate, root };
        options.useKeyIdentifier = useKeyIdentifier;
        options.usePss = usePss;
        options.noAttributes = noAttributes;
        options.digest = isSha1 ? EVP_sha1() : nullptr;
        options.encapsulateSha1 = isEncapsulated;
        options.useIndefiniteLength = isIndefiniteLength;
        return Helper::signCms(data, options);
    };

    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary(subFilter);
    file.addRevision(objects);
    QVERIFY(Helper::sign(file, 7, signerFunction));

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    // The signature covers the whole file and the signer is trusted by the root
    QVERIFY2(result->isValid(), qPrintable(result->getErrors().join('\n')));
    QCOMPARE(result->getLaterRevisions(), PDFSignatureVerificationResult::LaterRevisions::None);
    QVERIFY(result->getInfos().isEmpty());
    QVERIFY(result->getSignerCertificate());
    QCOMPARE(result->getSignerCertificate()->getName(PDFCertificateInfo::CommonName), QString::fromUtf8(isRsa ? "Test RSA Signer" : "Test Signer"));

    // The chain built from the signer: the signer, then its root (R6-2)
    QCOMPARE(Helper::getCommonNames(*result), QStringList({ QString::fromUtf8(isRsa ? "Test RSA Signer" : "Test Signer"), QStringLiteral("Test Root") }));

    // A weak digest is a warning (B114); without it there is no warning, also not of a missing revocation list (B117)
    QCOMPARE(result->hasFlag(PDFSignatureVerificationResult::Warning_Signature_WeakAlgorithm), isWeak);
    QCOMPARE(result->hasWarning(), isWeak);
    QVERIFY(!result->hasFlag(PDFSignatureVerificationResult::Warning_Certificate_UnableToGetCRL));

    // The byte range [0 a b c] of the text of the file: the whole file without the gap
    const qsizetype gapStart = file.data().indexOf("/Contents <", file.getObjectOffset(7)) + 10;
    PDFClosedIntervalSet covered;
    covered.addInterval(0, file.data().size() - 1);
    QCOMPARE(result->getBytesCoveredBySignature().toText(false), covered.toText(false));
    QCOMPARE(file.data().at(gapStart), '<');

    // SEQUENCE of the indefinite length: 30 80 (X.690, 8.1.3.6)
    QCOMPARE(file.data().mid(gapStart + 1, 4) == "3080", isIndefiniteLength);
}

void SignatureVerificationTest::byteRangeLayout_data()
{
    QTest::addColumn<LayoutCase>("layoutCase");

    // ISO 32000-2, 12.8.1: /ByteRange covers the whole file except the value of /Contents, which is a hexadecimal
    // string; plan 6.7 (B109), R6-22 (the gap is /Contents of this dictionary at the position of the reader and one
    // DER object followed only by zeros), 7.2 (limits, B111)
    QTest::newRow("control") << LayoutCase::Control;
    QTest::newRow("three-ranges") << LayoutCase::ThreeRanges;
    QTest::newRow("first-offset-not-zero") << LayoutCase::FirstOffsetNotZero;
    QTest::newRow("gap-before-contents") << LayoutCase::GapBeforeContents;
    QTest::newRow("overlapping-ranges") << LayoutCase::OverlappingRanges;
    QTest::newRow("negative-offset") << LayoutCase::NegativeOffset;
    QTest::newRow("outside-of-file") << LayoutCase::OutsideOfFile;
    QTest::newRow("overflowing-length") << LayoutCase::OverflowingLength;
    QTest::newRow("not-end-of-revision") << LayoutCase::NotEndOfRevision;
    QTest::newRow("dictionary-copied-later") << LayoutCase::DictionaryCopiedLater;
    QTest::newRow("duplicate-contents") << LayoutCase::DuplicateContents;
    QTest::newRow("non-zero-after-signature") << LayoutCase::NonZeroAfterSignature;
    QTest::newRow("signature-too-large") << LayoutCase::SignatureTooLarge;
}

void SignatureVerificationTest::byteRangeLayout()
{
    QFETCH(LayoutCase, layoutCase);

    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    const QByteArray extra = layoutCase == LayoutCase::DuplicateContents ? QByteArray(" /Contents <00>") : QByteArray();
    const int contentsSize = layoutCase == LayoutCase::SignatureTooLarge ? int(PDFSignatureHandler::MAXIMUM_SIGNATURE_SIZE) + 1 : 8192;
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached", "Sig", extra, contentsSize);
    file.addRevision(objects);

    // [0 a b c] of the text of the file
    const QByteArray& data = file.data();
    const qint64 a = data.indexOf("/Contents <", file.getObjectOffset(7)) + 10;
    const qint64 b = data.indexOf('>', a) + 1;
    const qint64 c = data.size() - b;

    TestSignLayout layout;
    switch (layoutCase)
    {
        case LayoutCase::Control:
        case LayoutCase::DictionaryCopiedLater:
        case LayoutCase::DuplicateContents:
        case LayoutCase::SignatureTooLarge:
            break;

        case LayoutCase::ThreeRanges:
            layout.byteRange = { 0, a, b, c - 10, b + c - 10, 10 };
            break;

        case LayoutCase::FirstOffsetNotZero:
            layout.byteRange = { 1, a - 1, b, c };
            break;

        case LayoutCase::GapBeforeContents:
            layout.byteRange = { 0, a - 1, b, c };
            break;

        case LayoutCase::OverlappingRanges:
            layout.byteRange = { 0, a, a - 10, c + (b - a) + 10 };
            break;

        case LayoutCase::NegativeOffset:
            layout.byteRange = { 0, a, -1, c };
            break;

        case LayoutCase::OutsideOfFile:
            layout.byteRange = { 0, a, b, c + 100 };
            break;

        case LayoutCase::OverflowingLength:
            layout.byteRange = { 0, a, b, std::numeric_limits<qint64>::max() - 10 };
            break;

        case LayoutCase::NotEndOfRevision:
            // The signature ends 6 bytes before the end of the revision ("%%EOF\n" unsigned)
            layout.byteRange = { 0, a, b, c - 6 };
            break;

        case LayoutCase::NonZeroAfterSignature:
            // Data in the gap behind the DER object of the signature, which d2i functions ignore
            layout.gapSuffix = QByteArray("\x01\x02\x03\x04", 4);
            break;
    }

    QVERIFY(Helper::sign(file, 7, getSigner(m_signer), layout));

    if (layoutCase == LayoutCase::DictionaryCopiedLater)
    {
        // The signature dictionary written again with the same bytes in a later revision: the reader reads
        // the copy, the gap is /Contents of the original
        const qsizetype objectStart = file.getObjectOffset(7);
        const qsizetype bodyStart = file.data().indexOf("obj\n", objectStart) + 4;
        const qsizetype bodyEnd = file.data().indexOf("\nendobj", bodyStart);
        file.addRevision({ { 7, file.data().mid(bodyStart, bodyEnd - bodyStart) } });
    }

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    if (layoutCase == LayoutCase::Control)
    {
        QVERIFY2(result->isValid(), qPrintable(result->getErrors().join('\n')));
        QVERIFY(!result->hasFlag(PDFSignatureVerificationResult::Error_Signature_ByteRange));
        return;
    }

    // An invalid layout is an error, never a warning, and no part of the signature is valid
    QVERIFY(result->hasFlag(PDFSignatureVerificationResult::Error_Signature_ByteRange));
    QVERIFY(!result->isSignatureValid());
    QVERIFY(!result->isValid());
    QCOMPARE(result->getSignatureStatus(), PDFSignatureVerificationResult::Status::Error);
    QVERIFY(!result->getSignerCertificate());
}

void SignatureVerificationTest::signerBySignerInfo()
{
    // R6-2: the attacker puts the certificate of the victim first into the bag and signs by his own key. The
    // signer is the certificate of the SignerInfo, verified against the signature (RFC 5652, 5.3, 5.6).
    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    file.addRevision(objects);

    const TestIdentity* attacker = &m_attacker;
    X509* victim = m_victim.certificate;
    X509* root = m_root.certificate;
    QVERIFY(Helper::sign(file, 7, [attacker, victim, root](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = attacker;
        options.bag = { victim, root, attacker->certificate };
        return Helper::signCms(data, options);
    }));

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    // The bytes are signed by the attacker: the signature itself is intact, its certificate is not trusted
    QVERIFY2(result->isSignatureValid(), qPrintable(result->getErrors().join('\n')));
    QVERIFY(result->hasCertificateError());
    QVERIFY(!result->isValid());

    // The name of the signer, the certificates and the certificate to trust are the attacker's ones only
    QVERIFY(result->getSignerCertificate());
    QCOMPARE(result->getSignerCertificate()->getName(PDFCertificateInfo::CommonName), QStringLiteral("Attacker"));
    QCOMPARE(result->getCertificateInfos().front().getName(PDFCertificateInfo::CommonName), QStringLiteral("Attacker"));
    QVERIFY(!Helper::getCommonNames(*result).contains(QStringLiteral("Victim Signer")));
}

void SignatureVerificationTest::signerNotOfSignature()
{
    // R6-2: the SignerInfo names the certificate of the victim (the same issuer, the serial number of the victim),
    // the signature is of the key of the attacker. No certificate is the signer: an error and no name.
    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    file.addRevision(objects);

    const TestIdentity* attacker = &m_attacker;
    X509* victim = m_victim.certificate;
    X509* root = m_root.certificate;
    QVERIFY(Helper::sign(file, 7, [attacker, victim, root](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = attacker;
        options.bag = { victim, root };
        options.claimedSigner = victim;
        return Helper::signCms(data, options);
    }));

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    QVERIFY(!result->isSignatureValid());
    QVERIFY(result->hasSignatureError());
    QVERIFY(!result->getSignerCertificate());
    QVERIFY(!Helper::getCommonNames(*result).contains(QStringLiteral("Victim Signer")));
}

void SignatureVerificationTest::certificatesNotVerified()
{
    // B132: without the verification of the certificates (PdfTool --ver-no-cert-check) the signer is identified,
    // its chain and its trust are not verified: no certificate error and no valid certificate
    TestPdfFile file;
    QVERIFY(createSignedDocument(file, "ETSI.CAdES.detached", 0, false));

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), nullptr, false);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    QVERIFY(result->isSignatureValid());
    QVERIFY(!result->hasCertificateError());
    QVERIFY(!result->isCertificateValid());
    QVERIFY(!result->isValid());
    QVERIFY(result->getSignerCertificate());
    QCOMPARE(Helper::getCommonNames(*result), QStringList({ QStringLiteral("Test Signer") }));

    // The same file with the verification: the signer is not trusted without the trusted root
    const std::vector<PDFSignatureVerificationResult> verifiedResults = Helper::verify(file.data(), nullptr, true);
    QVERIFY(Helper::findResult(verifiedResults, "Signature1")->hasCertificateError());
}

void SignatureVerificationTest::purposeOfDocumentSigning_data()
{
    QTest::addColumn<QByteArray>("keyUsage");
    QTest::addColumn<QByteArray>("extendedKeyUsage");
    QTest::addColumn<bool>("isAllowed");

    // B116, plan 5.3: key usage digitalSignature or nonRepudiation (RFC 5280, 4.2.1.3); extended key usage
    // missing, emailProtection, documentSigning (RFC 9336) or the Adobe Authentic Documents Trust
    QTest::newRow("document-signing") << QByteArray("critical,digitalSignature") << QByteArray("1.3.6.1.5.5.7.3.36") << true;
    QTest::newRow("adobe-authentic-documents") << QByteArray("critical,nonRepudiation") << QByteArray("1.2.840.113583.1.1.5") << true;
    QTest::newRow("email-protection") << QByteArray("critical,digitalSignature,nonRepudiation") << QByteArray("emailProtection") << true;
    QTest::newRow("microsoft-document-signing") << QByteArray("critical,digitalSignature") << QByteArray("1.3.6.1.4.1.311.10.3.12") << true;
    QTest::newRow("any-extended-key-usage") << QByteArray("critical,digitalSignature") << QByteArray("anyExtendedKeyUsage") << true;
    QTest::newRow("no-extended-key-usage") << QByteArray("critical,nonRepudiation") << QByteArray() << true;
    QTest::newRow("server-authentication") << QByteArray("critical,digitalSignature") << QByteArray("serverAuth") << false;
    QTest::newRow("key-encipherment-only") << QByteArray("critical,keyEncipherment") << QByteArray() << false;
}

void SignatureVerificationTest::purposeOfDocumentSigning()
{
    QFETCH(QByteArray, keyUsage);
    QFETCH(QByteArray, extendedKeyUsage);
    QFETCH(bool, isAllowed);

    TestCertificateOptions options;
    options.keyUsage = keyUsage;
    options.extendedKeyUsage = extendedKeyUsage;
    TestIdentity signer = Helper::createIdentity("Purpose Signer", &m_root, options);

    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    file.addRevision(objects);
    const bool isSigned = Helper::sign(file, 7, getSigner(signer));
    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    Helper::freeIdentity(signer);

    QVERIFY(isSigned);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);
    QVERIFY(result->isSignatureValid());
    QCOMPARE(result->isCertificateValid(), isAllowed);
    QCOMPARE(result->getErrors().contains(QStringLiteral("The certificate of the signer may not be used for signing documents.")), !isAllowed);
}

void SignatureVerificationTest::laterRevisions_data()
{
    QTest::addColumn<int>("certification");
    QTest::addColumn<Scenario>("scenario");
    QTest::addColumn<int>("laterRevisions");
    QTest::addColumn<bool>("isChangeError");
    QTest::addColumn<QString>("info");

    using Later = PDFSignatureVerificationResult::LaterRevisions;
    const QString laterSignatures = QStringLiteral("Later signatures or timestamps were added after the document was signed.");
    const QString visibleSignature = QStringLiteral("A visible signature was added on page 1.");
    const QString validationData = QStringLiteral("Validation data were added after the document was signed.");
    const QString savedAgain = QStringLiteral("The file was saved again after it was signed, nothing shown was changed.");
    const QString formFilled = QStringLiteral("Form fields were filled in after the document was certified, the certification allows it.");

    // An approval signature (plan R6-3): only signatures, timestamps and validation data are allowed later, the rest is a change
    QTest::newRow("approval-none") << 0 << Scenario::None << int(Later::None) << false << QString();
    QTest::newRow("approval-timestamp") << 0 << Scenario::Timestamp << int(Later::Allowed) << false << laterSignatures;
    QTest::newRow("approval-visible-signature") << 0 << Scenario::VisibleSignature << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("approval-dss") << 0 << Scenario::DocumentSecurityStore << int(Later::Allowed) << false << validationData;
    QTest::newRow("approval-saved-again") << 0 << Scenario::SavedAgain << int(Later::Allowed) << false << savedAgain;
    QTest::newRow("approval-content-changed") << 0 << Scenario::ContentChanged << int(Later::Changed) << false << QString();
    QTest::newRow("approval-sneaky-signature") << 0 << Scenario::SneakySignature << int(Later::Changed) << false << QString();
    QTest::newRow("approval-evil-annotation") << 0 << Scenario::EvilAnnotation << int(Later::Changed) << false << QString();
    QTest::newRow("approval-widget-appearance") << 0 << Scenario::WidgetAppearance << int(Later::Changed) << false << QString();
    QTest::newRow("approval-form-resources") << 0 << Scenario::FormResources << int(Later::Changed) << false << QString();
    QTest::newRow("approval-dangling-reference") << 0 << Scenario::DanglingReference << int(Later::Changed) << false << QString();
    QTest::newRow("approval-invalid-later-signature") << 0 << Scenario::InvalidLaterSignature << int(Later::Changed) << false << QString();
    QTest::newRow("approval-catalog-changed") << 0 << Scenario::CatalogChanged << int(Later::Changed) << false << QString();
    QTest::newRow("approval-form-filled") << 0 << Scenario::FormFilled << int(Later::Changed) << false << QString();
    QTest::newRow("approval-shared-field-filled") << 0 << Scenario::SharedFieldFilled << int(Later::Changed) << false << QString();
    QTest::newRow("approval-parent-cycle") << 0 << Scenario::ParentCycle << int(Later::Changed) << false << QString();
    QTest::newRow("approval-sign-empty-field") << 0 << Scenario::SignEmptyField << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("approval-sign-empty-field-moved") << 0 << Scenario::SignEmptyFieldMoved << int(Later::Changed) << false << QString();
    QTest::newRow("approval-tagged-signature") << 0 << Scenario::TaggedSignature << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("approval-tagged-foreign-object") << 0 << Scenario::TaggedForeignObject << int(Later::Changed) << false << QString();
    QTest::newRow("approval-tagged-role-map") << 0 << Scenario::TaggedRoleMap << int(Later::Changed) << false << QString();
    QTest::newRow("approval-acrobat-signature") << 0 << Scenario::AcrobatSignature << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("approval-acrobat-other-info-key") << 0 << Scenario::AcrobatOtherInfoKey << int(Later::Changed) << false << QString();
    QTest::newRow("approval-modification-date-only") << 0 << Scenario::ModificationDateOnly << int(Later::Changed) << false << QString();
    QTest::newRow("p1-acrobat-timestamp") << 1 << Scenario::AcrobatTimestamp << int(Later::Allowed) << false << laterSignatures;
    QTest::newRow("p2-acrobat-signature") << 2 << Scenario::AcrobatSignature << int(Later::Allowed) << false << visibleSignature;

    // A certification with P = 1 (ISO 32000-2, 12.8.2.2.1, Table 257): no changes, except validation data and document timestamps
    QTest::newRow("p1-timestamp") << 1 << Scenario::Timestamp << int(Later::Allowed) << false << laterSignatures;
    QTest::newRow("p1-dss") << 1 << Scenario::DocumentSecurityStore << int(Later::Allowed) << false << validationData;
    QTest::newRow("p1-visible-signature") << 1 << Scenario::VisibleSignature << int(Later::Changed) << true << QString();
    QTest::newRow("p1-sign-empty-field") << 1 << Scenario::SignEmptyField << int(Later::Changed) << true << QString();
    QTest::newRow("p1-form-filled") << 1 << Scenario::FormFilled << int(Later::Changed) << true << QString();
    QTest::newRow("p1-content-changed") << 1 << Scenario::ContentChanged << int(Later::Changed) << true << QString();

    // P = 2: also filling in forms and signing
    QTest::newRow("p2-visible-signature") << 2 << Scenario::VisibleSignature << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("p2-sign-empty-field") << 2 << Scenario::SignEmptyField << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("p2-tagged-signature") << 2 << Scenario::TaggedSignature << int(Later::Allowed) << false << visibleSignature;
    QTest::newRow("p2-tagged-foreign-object") << 2 << Scenario::TaggedForeignObject << int(Later::Changed) << true << QString();
    QTest::newRow("p2-form-filled") << 2 << Scenario::FormFilled << int(Later::Allowed) << false << formFilled;
    QTest::newRow("p2-shared-field-filled") << 2 << Scenario::SharedFieldFilled << int(Later::Allowed) << false << formFilled;
    QTest::newRow("p2-widget-appearance") << 2 << Scenario::WidgetAppearance << int(Later::Changed) << true << QString();
    QTest::newRow("p2-sneaky-signature") << 2 << Scenario::SneakySignature << int(Later::Changed) << true << QString();
    QTest::newRow("p2-evil-annotation") << 2 << Scenario::EvilAnnotation << int(Later::Changed) << true << QString();
    QTest::newRow("p2-parent-cycle") << 2 << Scenario::ParentCycle << int(Later::Changed) << true << QString();
    QTest::newRow("p2-content-changed") << 2 << Scenario::ContentChanged << int(Later::Changed) << true << QString();

    // P = 3 is not evaluated (SECURITY_TODO): its later revisions are those of an approval signature, no certification error
    QTest::newRow("p3-evil-annotation") << 3 << Scenario::EvilAnnotation << int(Later::Changed) << false << QString();
    QTest::newRow("p3-timestamp") << 3 << Scenario::Timestamp << int(Later::Allowed) << false << laterSignatures;
}

void SignatureVerificationTest::laterRevisions()
{
    QFETCH(int, certification);
    QFETCH(Scenario, scenario);
    QFETCH(int, laterRevisions);
    QFETCH(bool, isChangeError);
    QFETCH(QString, info);

    TestPdfFile file;
    QVERIFY(createSignedDocument(file, "ETSI.CAdES.detached", certification, scenario == Scenario::DanglingReference));
    const qsizetype signedEnd = file.data().size();
    QVERIFY(addLaterRevision(file, scenario, certification));

    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);

    using Later = PDFSignatureVerificationResult::LaterRevisions;
    const Later expectedLaterRevisions = Later(laterRevisions);
    QCOMPARE(result->getCertificationPermission(), PDFInteger(certification));
    QCOMPARE(result->getLaterRevisions(), expectedLaterRevisions);

    // A change is a warning of the approval signature; the certification adds an error of the not allowed change
    QCOMPARE(result->hasFlag(PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes), expectedLaterRevisions == Later::Changed);
    QCOMPARE(result->hasFlag(PDFSignatureVerificationResult::Error_Signature_ChangesNotAllowed), isChangeError);
    QVERIFY(result->isCertificateValid());
    QCOMPARE(result->isSignatureValid(), !isChangeError);
    QCOMPARE(result->isValid(), !isChangeError);
    if (expectedLaterRevisions == Later::Changed)
    {
        QVERIFY(result->getWarnings().contains(QStringLiteral("The document was changed after it was signed, %1 bytes are not covered by the signature.").arg(file.data().size() - signedEnd)));
    }
    else
    {
        QVERIFY2(!result->hasWarning(), qPrintable(result->getWarnings().join('\n')));
    }

    if (!info.isEmpty())
    {
        QVERIFY2(result->getInfos().contains(info), qPrintable(result->getInfos().join('\n')));
    }
    else
    {
        QVERIFY(result->getInfos().isEmpty());
    }

    // The later signature itself: valid, it ends the last revision
    if (scenario == Scenario::Timestamp || scenario == Scenario::VisibleSignature || scenario == Scenario::TaggedSignature || scenario == Scenario::SignEmptyField)
    {
        const QString laterName = scenario == Scenario::Timestamp ? QStringLiteral("Timestamp1") : (scenario == Scenario::SignEmptyField ? QStringLiteral("Signature0") : QStringLiteral("Signature2"));
        const PDFSignatureVerificationResult* laterResult = Helper::findResult(results, laterName);
        QVERIFY(laterResult);
        QVERIFY2(laterResult->isValid(), qPrintable(laterResult->getErrors().join('\n')));
        QCOMPARE(laterResult->getLaterRevisions(), Later::None);
        QCOMPARE(laterResult->getType(), scenario == Scenario::Timestamp ? PDFSignature::Type::DocTimeStamp : PDFSignature::Type::Sig);
    }
}

void SignatureVerificationTest::tooManySignatures()
{
    // Plan 7.2 (B123): at most MAXIMUM_VERIFIED_SIGNATURES signatures of a document are verified, the others
    // are not verified (an error); Signature1 (the widget of the page) and 256 fields share its value
    const int count = PDFSignatureHandler::MAXIMUM_VERIFIED_SIGNATURES + 1;
    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    QByteArray fields = "6 0 R";
    for (int i = 1; i < count; ++i)
    {
        objects[100 + i] = "<< /FT /Sig /T (S" + QByteArray::number(i) + ") /V 7 0 R >>";
        fields += " " + QByteArray::number(100 + i) + " 0 R";
    }
    objects[5] = form(fields);
    file.addRevision(objects);
    QVERIFY(Helper::sign(file, 7, getSigner(m_signer)));

    // The empty field Signature0 has no signature (a result of the type Invalid), it is not counted
    std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    std::erase_if(results, [](const PDFSignatureVerificationResult& result) { return result.getType() == PDFSignature::Type::Invalid; });
    QCOMPARE(results.size(), size_t(count));

    int validCount = 0;
    int notVerifiedCount = 0;
    for (const PDFSignatureVerificationResult& result : results)
    {
        if (result.isValid())
        {
            ++validCount;
            continue;
        }

        ++notVerifiedCount;
        QVERIFY(result.hasFlag(PDFSignatureVerificationResult::Error_Generic));
        QCOMPARE(result.getSignatureStatus(), PDFSignatureVerificationResult::Status::Error);
        QCOMPARE(result.getErrors(), QStringList({ QStringLiteral("The signature was not verified, the document has more than 256 signatures.") }));
    }

    QCOMPARE(validCount, PDFSignatureHandler::MAXIMUM_VERIFIED_SIGNATURES);
    QCOMPARE(notVerifiedCount, 1);
}

void SignatureVerificationTest::tooManyCertificates()
{
    // Plan 7.2 (B123): at most MAXIMUM_CERTIFICATES certificates in the bag of a signature
    std::vector<TestIdentity> identities;
    for (int i = 0; i <= PDFSignatureHandler::MAXIMUM_CERTIFICATES; ++i)
    {
        identities.push_back(Helper::createIdentity("Bag", nullptr, TestCertificateOptions()));
    }

    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached", "Sig", QByteArray(), 32768);
    file.addRevision(objects);

    const TestIdentity* signer = &m_signer;
    std::vector<X509*> bag = { signer->certificate };
    for (const TestIdentity& identity : identities)
    {
        bag.push_back(identity.certificate);
    }
    const bool isSigned = Helper::sign(file, 7, [signer, &bag](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = signer;
        options.bag = bag;
        return Helper::signCms(data, options);
    });
    const std::vector<PDFSignatureVerificationResult> results = Helper::verify(file.data(), m_root.certificate);
    for (TestIdentity& identity : identities)
    {
        Helper::freeIdentity(identity);
    }

    QVERIFY(isSigned);
    const PDFSignatureVerificationResult* result = Helper::findResult(results, "Signature1");
    QVERIFY(result);
    QVERIFY(!result->isSignatureValid());
    QVERIFY(result->hasFlag(PDFSignatureVerificationResult::Error_Signature_ByteRange));
    QVERIFY(result->getErrors().contains(QStringLiteral("The signature has more than 64 certificates.")));
}

void SignatureVerificationTest::documentSecurityStoreVri()
{
    // B127: /TU is a key of the dictionary of a signature in /VRI (ISO 32000-2, 12.8.4.3, Table 262)
    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[1] = "<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R /DSS << /VRI << /ABCDEF0123 << /TU (D:20260102030405Z) >> >> >> >>";
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    file.addRevision(objects);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(file.data());
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFDocumentSecurityStore& store = document.getCatalog()->getDocumentSecurityStore();
    const PDFDocumentSecurityStore::SecurityStoreItem* item = store.getItem("ABCDEF0123");
    QVERIFY(item && item != store.getMasterItem());
    QCOMPARE(item->created, QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC));
}

void SignatureVerificationTest::pdfToolSignerName()
{
    // R6-2 and B132 in PdfTool verify-signatures: the name of the signer is of the SignerInfo
#ifdef Q_OS_WIN
    const QString program = QCoreApplication::applicationDirPath() + QStringLiteral("/PdfTool.exe");
#else
    const QString program = QCoreApplication::applicationDirPath() + QStringLiteral("/PdfTool");
#endif
    if (!QFileInfo::exists(program))
    {
        QSKIP("PdfTool is not built next to the test.");
    }

    TestPdfFile file;
    std::map<int, QByteArray> objects = createDocumentObjects(0, false);
    objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached");
    file.addRevision(objects);
    const TestIdentity* attacker = &m_attacker;
    X509* victim = m_victim.certificate;
    QVERIFY(Helper::sign(file, 7, [attacker, victim](const QByteArray& data)
    {
        TestSignOptions options;
        options.signer = attacker;
        options.bag = { victim, attacker->certificate };
        return Helper::signCms(data, options);
    }));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fileName = directory.filePath(QStringLiteral("signed.pdf"));
    QFile output(fileName);
    QVERIFY(output.open(QFile::WriteOnly));
    output.write(file.data());
    output.close();

    auto run = [&program, &fileName](const QStringList& extraArguments)
    {
        QProcess process;
        process.start(program, QStringList({ QStringLiteral("verify-signatures"), QStringLiteral("--ver-no-user-cert"), QStringLiteral("--ver-no-sys-cert"), QStringLiteral("--ver-details") }) + extraArguments + QStringList({ fileName }));
        process.waitForFinished(60000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        return QString::fromUtf8(process.readAllStandardOutput());
    };

    const QString text = run(QStringList());
    QVERIFY2(text.contains(QStringLiteral("Signed by: Attacker")), qPrintable(text));
    QVERIFY(!text.contains(QStringLiteral("Victim Signer")));

    // --ver-no-cert-check: the certificate is not verified, the signer is still the one of the SignerInfo
    const QString skippedText = run(QStringList({ QStringLiteral("--ver-no-cert-check") }));
    QVERIFY2(skippedText.contains(QStringLiteral("Signed by: Attacker")), qPrintable(skippedText));
    QVERIFY(skippedText.contains(QStringLiteral("Certificate status: Skipped")));
    QVERIFY(!skippedText.contains(QStringLiteral("Victim Signer")));
}

void SignatureVerificationTest::pdfToolClaimedDate()
{
    // RS6FIGMA L-M1 in PdfTool verify-signatures: a PAdES signature without the signed attribute signing-time
    // has its date only in /M of the signature dictionary (ISO 32000-2 Table 255) - claimed by the signer,
    // no trusted time; the date of signing-time (RFC 5652, 11.3, covered by the signature) is not marked
#ifdef Q_OS_WIN
    const QString program = QCoreApplication::applicationDirPath() + QStringLiteral("/PdfTool.exe");
#else
    const QString program = QCoreApplication::applicationDirPath() + QStringLiteral("/PdfTool");
#endif
    if (!QFileInfo::exists(program))
    {
        QSKIP("PdfTool is not built next to the test.");
    }

    for (const bool noAttributes : { true, false })
    {
        TestPdfFile file;
        std::map<int, QByteArray> objects = createDocumentObjects(0, false);
        objects[7] = Helper::signatureDictionary("ETSI.CAdES.detached", "Sig", " /M (D:20260102030405Z)");
        file.addRevision(objects);
        const TestIdentity* signer = &m_signer;
        X509* root = m_root.certificate;
        QVERIFY(Helper::sign(file, 7, [signer, root, noAttributes](const QByteArray& data)
        {
            TestSignOptions options;
            options.signer = signer;
            options.bag = { signer->certificate, root };
            options.noAttributes = noAttributes;
            return Helper::signCms(data, options);
        }));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString fileName = directory.filePath(QStringLiteral("signed.pdf"));
        QFile output(fileName);
        QVERIFY(output.open(QFile::WriteOnly));
        output.write(file.data());
        output.close();

        QProcess process;
        process.start(program, QStringList({ QStringLiteral("verify-signatures"), QStringLiteral("--ver-no-user-cert"), QStringLiteral("--ver-no-sys-cert"), QStringLiteral("--ver-details"), fileName }));
        QVERIFY(process.waitForFinished(60000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"))));
        const QString text = QString::fromUtf8(process.readAllStandardOutput());

        QString signingDate;
        for (const QString& line : text.split(QLatin1Char('\n')))
        {
            if (line.trimmed().startsWith(QStringLiteral("Signing date: ")))
            {
                signingDate = line.trimmed();
            }
        }
        QVERIFY2(signingDate.size() > QStringLiteral("Signing date: ").size(), qPrintable(text));
        QCOMPARE(signingDate.endsWith(QStringLiteral(" (claimed by the signer)")), noAttributes);
        QCOMPARE(text.count(QStringLiteral("(claimed by the signer)")), noAttributes ? 2 : 0);
    }
}

void SignatureVerificationTest::corpusAttacks()
{
    // The known attacks on the signatures of real readers (testpdf/Signature/invalid, read only): incremental
    // saving (isa), signature wrapping (siwa, pdf-wrapping), universal signature forgery (usf), the bypass of
    // Nuance Power PDF, the exchanged content of Acrobat Reader DC. None of them is a clean signature.
    const QString corpus = qEnvironmentVariable("PDF4QT_TOOL_CORPUS_DIRECTORY", QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/../testpdf"));
    const QDir directory(corpus + QStringLiteral("/Signature/invalid"));
    if (!directory.exists())
    {
        QSKIP(qPrintable(QStringLiteral("The folder '%1' does not exist.").arg(directory.absolutePath())));
    }

    const QStringList files = directory.entryList({ QStringLiteral("isa*.pdf"), QStringLiteral("siwa*.pdf"), QStringLiteral("usf*.pdf"),
                                                    QStringLiteral("NuancePowerPDF-bypass*.pdf"), QStringLiteral("pdf-wrapping*.pdf"),
                                                    QStringLiteral("AdobeAcrobatReaderDC*.pdf") }, QDir::Files, QDir::Name);
    QVERIFY(!files.isEmpty());

    int signatureCount = 0;
    for (const QString& fileName : files)
    {
        QFile input(directory.filePath(fileName));
        QVERIFY(input.open(QFile::ReadOnly));
        const QByteArray data = input.readAll();

        PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const PDFDocument document = reader.readFromBuffer(data);
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
        {
            continue;
        }

        PDFSignatureHandler::Parameters parameters;
        parameters.dss = &document.getCatalog()->getDocumentSecurityStore();
        parameters.useSystemCertificateStore = false;
        parameters.aatlCertificates = PDFSignatureHandler::AATLCertificates::Never;
        const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
        const std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, data, parameters);

        for (const PDFSignatureVerificationResult& result : results)
        {
            if (result.getType() == PDFSignature::Type::Invalid)
            {
                continue;
            }

            ++signatureCount;
            QVERIFY2(result.hasSignatureError() || result.hasSignatureWarning(), qPrintable(fileName));
        }
    }

    QVERIFY(signatureCount > 0);
}

QTEST_GUILESS_MAIN(SignatureVerificationTest)

#include "tst_signatureverificationtest.moc"
