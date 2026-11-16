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

#include "pdfsignaturehandler.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfdocumentreader.h"
#include "pdfencoding.h"
#include "pdfform.h"
#include "pdfparser.h"
#include "pdfutils.h"
#include "pdfxreftable.h"
#include "pdfsignaturehandler_impl.h"

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/ts.h>
#include <openssl/tserr.h>

#include <QDir>
#include <QMutex>
#include <QFileInfo>
#include <QLockFile>
#include <QDataStream>
#include <QMutexLocker>
#include <QStandardPaths>

#include "pdfdbgheap.h"

#include <array>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#ifdef Q_OS_UNIX
#include <time.h>
#endif

namespace pdf
{

namespace signaturehandler
{

/// Fills the verification context of a RFC 3161 timestamp. The functions with
/// the 0 in their name have clear ownership semantics, but they were introduced
/// in OpenSSL 3.4, so the older ones are used with the older versions, which the
/// project still supports. All objects are owned by the context in both cases.
void setTimestampVerifyContextData(TS_VERIFY_CTX* context, X509_STORE* store, STACK_OF(X509)* certificates, BIO* data)
{
#if OPENSSL_VERSION_NUMBER >= 0x30400000L
    TS_VERIFY_CTX_set0_store(context, store);
    TS_VERIFY_CTX_set0_certs(context, certificates);
    TS_VERIFY_CTX_set0_data(context, data);
#else
    TS_VERIFY_CTX_set_store(context, store);
    TS_VERIFY_CTS_set_certs(context, certificates);
    TS_VERIFY_CTX_set_data(context, data);
#endif
}

}   // namespace signaturehandler

using namespace signaturehandler;

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

PDFSignatureReference PDFSignatureReference::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignatureReference result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, PDFSignatureReference::TransformMethod>, 3> types = {
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "DocMDP", PDFSignatureReference::TransformMethod::DocMDP },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "UR", PDFSignatureReference::TransformMethod::UR },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "FieldMDP", PDFSignatureReference::TransformMethod::FieldMDP }
        };

        // Jakub Melka: parse the signature reference dictionary
        result.m_transformMethod = loader.readEnumByName(dictionary->get("TransformMethod"), types.cbegin(), types.cend(), PDFSignatureReference::TransformMethod::Invalid);
        result.m_transformParams = dictionary->get("TransformParams");
        result.m_data = dictionary->get("Data");
        result.m_digestMethod = loader.readNameFromDictionary(dictionary, "DigestMethod");
    }

    return result;
}

PDFSignature PDFSignature::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignature result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        if (object.isReference())
        {
            result.m_selfReference = object.getReference();
        }

        constexpr const std::array<std::pair<const char*, Type>, 2> types = {
            std::pair<const char*, Type>{ "Sig", Type::Sig },
            std::pair<const char*, Type>{ "DocTimeStamp", Type::DocTimeStamp }
        };

        // Jakub Melka: parse the signature dictionary
        result.m_type = loader.readEnumByName(dictionary->get("Type"), types.cbegin(), types.cend(), Type::Sig);
        result.m_filter = loader.readNameFromDictionary(dictionary, "Filter");
        result.m_subfilter = loader.readNameFromDictionary(dictionary, "SubFilter");
        result.m_contents = loader.readStringFromDictionary(dictionary, "Contents");

        if (dictionary->hasKey("Cert"))
        {
            PDFObject certificates = storage->getObject(dictionary->get("Cert"));
            if (certificates.isString())
            {
                result.m_certificates = { loader.readString(certificates) };
            }
            else if (certificates.isArray())
            {
                result.m_certificates = loader.readStringArray(certificates);
            }
        }

        std::vector<PDFInteger> byteRangesArray = loader.readIntegerArrayFromDictionary(dictionary, "ByteRange");
        const size_t byteRangeCount = byteRangesArray.size() / 2;
        result.m_byteRanges.reserve(byteRangeCount);
        for (size_t i = 0; i < byteRangeCount; ++i)
        {
            ByteRange byteRange = { byteRangesArray[2 * i], byteRangesArray[2 * i + 1] };
            result.m_byteRanges.push_back(byteRange);
        }

        result.m_references = loader.readObjectList<PDFSignatureReference>(dictionary->get("Reference"));
        for (const PDFSignatureReference& reference : result.m_references)
        {
            if (reference.getTransformMethod() == PDFSignatureReference::TransformMethod::DocMDP && result.m_docMDPPermission == 0)
            {
                // ISO 32000-2, Table 257: P is 1, 2 or 3, the default value is 2
                const PDFDictionary* parameters = storage->getDictionaryFromObject(reference.getTransformParams());
                const PDFInteger permission = parameters ? loader.readIntegerFromDictionary(parameters, "P", 2) : 2;
                result.m_docMDPPermission = (permission == 1 || permission == 3) ? permission : 2;
            }
        }

        std::vector<PDFInteger> changes = loader.readIntegerArrayFromDictionary(dictionary, "Changes");

        if (changes.size() == 3)
        {
            result.m_changes = { changes[0], changes[1], changes[2] };
        }

        result.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());
        result.m_signingDateTime = PDFEncoding::convertToDateTime(loader.readStringFromDictionary(dictionary, "M"));
        result.m_location = loader.readTextStringFromDictionary(dictionary, "Location", QString());
        result.m_reason = loader.readTextStringFromDictionary(dictionary, "Reason", QString());
        result.m_contactInfo = loader.readTextStringFromDictionary(dictionary, "ContactInfo", QString());
        result.m_R = loader.readIntegerFromDictionary(dictionary, "R", 0);
        result.m_V = loader.readIntegerFromDictionary(dictionary, "V", 0);
        result.m_propBuild = dictionary->get("Prop_Build");
        result.m_propTime = loader.readIntegerFromDictionary(dictionary, "Prop_AuthTime", 0);

        constexpr const std::array<std::pair<const char*, AuthentificationType>, 3> authentificationTypes = {
            std::pair<const char*, AuthentificationType>{ "PIN", AuthentificationType::PIN },
            std::pair<const char*, AuthentificationType>{ "Password", AuthentificationType::Password },
            std::pair<const char*, AuthentificationType>{ "Fingerprint", AuthentificationType::Fingerprint }
        };
        result.m_propType = loader.readEnumByName(dictionary->get("Prop_AuthType"), authentificationTypes.cbegin(), authentificationTypes.cend(), AuthentificationType::Invalid);
    }

    return result;
}

PDFSignatureHandler* PDFSignatureHandler::createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters, const PDFSignatureLayout& layout)
{
    Q_ASSERT(signatureField);

    const QByteArray& subfilter = signatureField->getSignature().getSubfilter();
    if (subfilter == "adbe.pkcs7.detached" || subfilter == "ETSI.CAdES.detached")
    {
        return new PDFCMSSignatureHandler(signatureField, sourceData, parameters, layout, PDFCMSSignatureHandler::Content::Detached);
    }
    else if (subfilter == "adbe.pkcs7.sha1")
    {
        return new PDFCMSSignatureHandler(signatureField, sourceData, parameters, layout, PDFCMSSignatureHandler::Content::SHA1Digest);
    }
    else if (subfilter == "adbe.x509.rsa_sha1")
    {
        return new PDFSignatureHandler_adbe_pkcs7_rsa_sha1(signatureField, sourceData, parameters, layout);
    }
    else if (subfilter == "ETSI.RFC3161")
    {
        return new PDFCMSSignatureHandler(signatureField, sourceData, parameters, layout, PDFCMSSignatureHandler::Content::TimestampToken);
    }

    return nullptr;
}

void PDFSignatureHandler::releaseThreadResources()
{
    OPENSSL_thread_stop();
}

/// Helpers of the verification of the signatures of a file: the revisions of the file and the layout
/// of a signature in it (B109, R6-22, B111, B123, B124)
class PDFSignatureHandlerHelper
{
public:
    PDFSignatureHandlerHelper() = delete;

    /// A revision of the file (ISO 32000-2, 7.5.6): "startxref", the offset of its cross-reference section and %%EOF
    struct Revision
    {
        PDFInteger eofEnd = 0;      ///< Offset right behind %%EOF
        PDFInteger end = 0;         ///< Offset behind %%EOF and its end of line (CR, LF or CR LF)
    };

    /// What a scan of a signature dictionary in the bytes of the file found
    struct DictionaryScan
    {
        int contentsCount = 0;              ///< Count of the keys /Contents
        PDFInteger contentsStart = -1;      ///< Offset of the first byte of the value of /Contents, when it is a string
        PDFInteger contentsEnd = -1;        ///< Offset behind the last byte of the value of /Contents
        QByteArray contents;                ///< The string of /Contents
        int byteRangeCount = 0;             ///< Count of the keys /ByteRange
        bool isByteRangeIntegers = false;   ///< /ByteRange is a direct array of integers
        std::vector<PDFInteger> byteRange;  ///< The integers of /ByteRange
    };

    /// A callback of the password, which refuses it: a reader decides without the password of an encrypted file
    static QString refusePassword(bool* ok)
    {
        *ok = false;
        return QString();
    }

    /// Frees a stack of certificates together with the certificates
    static void freeCertificates(STACK_OF(X509)* certificates)
    {
        sk_X509_pop_free(certificates, X509_free);
    }

    /// Returns true, if the reader of the application restores the objects of the file from its bytes
    /// \param securityHandler The authenticated handler of the document of the file (R6-1), may be null
    static bool isFileRestoredByReader(const QByteArray& sourceData, const PDFSecurityHandler* securityHandler)
    {
        // An encrypted file: the reader decides before it needs the password, a refused password changes nothing
        PDFDocumentReader reader(nullptr, &PDFSignatureHandlerHelper::refusePassword, true, false);
        reader.setAuthenticatedSecurityHandler(securityHandler);
        reader.readFromBuffer(sourceData);
        return reader.isDocumentRestored();
    }

    /// Reads the document of the bytes [0, end) of the file by the strict reader (a revision of the file,
    /// as a reader of that revision shows it). Returns nullptr, when the bytes are not a valid document.
    /// An encrypted revision is read by a clone of \p securityHandler, when its encryption is the encryption
    /// of the handler (R6-1: the revisions of a file share their encryption; a revision of another one,
    /// another /ID among them, is not read - a change), else it cannot be read without the password.
    /// \param securityHandler The authenticated handler of the document of the file, may be null
    static std::unique_ptr<PDFDocument> readRevision(const QByteArray& sourceData, PDFInteger end, const PDFSecurityHandler* securityHandler)
    {
        PDFDocumentReader reader(nullptr, &PDFSignatureHandlerHelper::refusePassword, false, false);
        reader.setAuthenticatedSecurityHandler(securityHandler);
        PDFDocument document = reader.readFromBuffer(end < sourceData.size() ? sourceData.left(end) : sourceData);
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK || reader.isDocumentRestored())
        {
            return nullptr;
        }
        return std::make_unique<PDFDocument>(document);
    }

    /// Returns the offset of the cross-reference section, which "startxref <offset>" in front of the
    /// %%EOF at the given offset names (only white space between the offset and %%EOF), or -1.
    static PDFInteger findStartXRef(const QByteArray& data, PDFInteger eofOffset)
    {
        constexpr PDFInteger WINDOW = 256;
        const qsizetype index = data.lastIndexOf(PDF_START_OF_XREF_MARK, eofOffset - 1);
        if (index < 0 || index < eofOffset - WINDOW)
        {
            return -1;
        }

        try
        {
            const PDFInteger offsetStart = index + PDFInteger(std::strlen(PDF_START_OF_XREF_MARK));
            PDFLexicalAnalyzer analyzer(data.constData() + offsetStart, data.constData() + eofOffset);
            const PDFLexicalAnalyzer::Token token = analyzer.fetch();
            if (token.type != PDFLexicalAnalyzer::TokenType::Integer || token.data.toLongLong() < 0)
            {
                return -1;
            }

            for (PDFInteger i = offsetStart + analyzer.pos(); i < eofOffset; ++i)
            {
                if (!PDFLexicalAnalyzer::isWhitespace(data[i]))
                {
                    return -1;
                }
            }
            return token.data.toLongLong();
        }
        catch (const PDFException&)
        {
            return -1;
        }
    }

    /// Returns the revisions of the file: each %%EOF, which follows "startxref <offset>"
    static std::vector<Revision> findRevisions(const QByteArray& data)
    {
        std::vector<Revision> revisions;
        const QByteArray eofMark(PDF_END_OF_FILE_MARK);

        qsizetype position = data.indexOf(eofMark);
        while (position >= 0)
        {
            if (findStartXRef(data, position) >= 0)
            {
                Revision revision;
                revision.eofEnd = position + eofMark.size();
                revision.end = revision.eofEnd;
                if (revision.end < data.size() && data[revision.end] == '\r')
                {
                    ++revision.end;
                }
                if (revision.end < data.size() && data[revision.end] == '\n')
                {
                    ++revision.end;
                }
                revisions.push_back(revision);
            }
            position = data.indexOf(eofMark, position + eofMark.size());
        }

        return revisions;
    }

    /// Returns the length of the encoded object (its header and its value) at the beginning of the data, or -1,
    /// when the data do not begin with an object of the tag. A definite length is read from the header in its
    /// shortest form (DER, X.690, 10.1). The indefinite length of BER is not DER, which ISO 32000-2, 12.8.3.3.1
    /// asks for, but signers of real documents write it: a ContentInfo of CMS of the indefinite length is
    /// measured by its parsing, its end is the end of its last octets, never a guess.
    static PDFInteger getSignatureObjectLength(const QByteArray& data, unsigned char tag)
    {
        if (data.size() < 2 || static_cast<unsigned char>(data[0]) != tag)
        {
            return -1;
        }

        const unsigned char first = static_cast<unsigned char>(data[1]);
        if (first == 0x80)
        {
            if (tag != 0x30)
            {
                return -1;
            }

            PDFOpenSSLGlobalLock lock;
            const unsigned char* begin = convertByteArrayToUcharPtr(data);
            const unsigned char* end = begin;
            CMS_ContentInfo* contentInfo = d2i_CMS_ContentInfo(nullptr, &end, long(data.size()));
            const PDFInteger length = contentInfo ? PDFInteger(end - begin) : -1;
            CMS_ContentInfo_free(contentInfo);
            ERR_clear_error();
            return length;
        }

        PDFInteger headerLength = 2;
        PDFInteger length = first;
        if (first > 0x80)
        {
            const int count = first & 0x7F;
            if (count > 4 || data.size() < 2 + count || data[2] == 0)
            {
                return -1;
            }

            length = 0;
            for (int i = 0; i < count; ++i)
            {
                length = (length << 8) | static_cast<unsigned char>(data[2 + i]);
            }

            if (length < 0x80)
            {
                return -1;
            }
            headerLength += count;
        }

        const PDFInteger total = headerLength + length;
        return total <= data.size() ? total : -1;
    }

    /// Skips the value of a dictionary or of an array, whose first token was fetched
    static void skipValue(PDFLexicalAnalyzer& analyzer, const PDFLexicalAnalyzer::Token& token)
    {
        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::ArrayStart:
            case PDFLexicalAnalyzer::TokenType::DictionaryStart:
            {
                int depth = 1;
                while (depth > 0)
                {
                    const PDFLexicalAnalyzer::Token item = analyzer.fetch();
                    switch (item.type)
                    {
                        case PDFLexicalAnalyzer::TokenType::ArrayStart:
                        case PDFLexicalAnalyzer::TokenType::DictionaryStart:
                            if (++depth > PDFParser::MAXIMUM_NESTING)
                            {
                                throw PDFException(PDFTranslationContext::tr("The signature dictionary is nested too deep."));
                            }
                            break;

                        case PDFLexicalAnalyzer::TokenType::ArrayEnd:
                        case PDFLexicalAnalyzer::TokenType::DictionaryEnd:
                            --depth;
                            break;

                        case PDFLexicalAnalyzer::TokenType::EndOfFile:
                            throw PDFException(PDFTranslationContext::tr("The signature dictionary is not terminated."));

                        default:
                            break;
                    }
                }
                break;
            }

            case PDFLexicalAnalyzer::TokenType::Integer:
            {
                // A reference "n g R" is one value
                const PDFInteger position = analyzer.pos();
                const PDFLexicalAnalyzer::Token generation = analyzer.fetch();
                if (generation.type == PDFLexicalAnalyzer::TokenType::Integer)
                {
                    const PDFLexicalAnalyzer::Token command = analyzer.fetch();
                    if (command.type == PDFLexicalAnalyzer::TokenType::Command && command.data.toByteArray() == "R")
                    {
                        break;
                    }
                }
                analyzer.seek(position);
                break;
            }

            case PDFLexicalAnalyzer::TokenType::ArrayEnd:
            case PDFLexicalAnalyzer::TokenType::DictionaryEnd:
            case PDFLexicalAnalyzer::TokenType::EndOfFile:
                throw PDFException(PDFTranslationContext::tr("The signature dictionary is invalid."));

            default:
                break;
        }
    }

    /// Scans the keys of a dictionary, whose start was fetched: /Contents and /ByteRange with their positions
    static void scanDictionary(PDFLexicalAnalyzer& analyzer, DictionaryScan& scan)
    {
        while (true)
        {
            const PDFLexicalAnalyzer::Token key = analyzer.fetch();
            if (key.type == PDFLexicalAnalyzer::TokenType::DictionaryEnd)
            {
                return;
            }
            if (key.type != PDFLexicalAnalyzer::TokenType::Name)
            {
                throw PDFException(PDFTranslationContext::tr("The signature dictionary is invalid."));
            }

            const QByteArray name = key.data.toByteArray();
            analyzer.skipWhitespaceAndComments();
            const PDFInteger valueStart = analyzer.pos();
            const PDFLexicalAnalyzer::Token value = analyzer.fetch();

            if (name == "Contents")
            {
                ++scan.contentsCount;
                if (value.type == PDFLexicalAnalyzer::TokenType::String)
                {
                    scan.contentsStart = valueStart;
                    scan.contentsEnd = analyzer.pos();
                    scan.contents = value.data.toByteArray();
                }
                else
                {
                    scan.contentsStart = -1;
                    skipValue(analyzer, value);
                }
            }
            else if (name == "ByteRange")
            {
                ++scan.byteRangeCount;
                scan.byteRange.clear();
                scan.isByteRangeIntegers = value.type == PDFLexicalAnalyzer::TokenType::ArrayStart;
                if (scan.isByteRangeIntegers)
                {
                    while (true)
                    {
                        const PDFLexicalAnalyzer::Token item = analyzer.fetch();
                        if (item.type == PDFLexicalAnalyzer::TokenType::ArrayEnd)
                        {
                            break;
                        }

                        if (item.type == PDFLexicalAnalyzer::TokenType::Integer)
                        {
                            scan.byteRange.push_back(item.data.toLongLong());
                        }
                        else
                        {
                            scan.isByteRangeIntegers = false;
                            skipValue(analyzer, item);
                        }
                    }
                }
                else
                {
                    skipValue(analyzer, value);
                }
            }
            else
            {
                skipValue(analyzer, value);
            }
        }
    }

    /// Scans the signature dictionary of the object "n g obj" at the offset: the object itself, or the
    /// dictionary /V of the object of a field (a direct signature dictionary). Returns false, when the bytes
    /// at the offset are not the object. Never throws.
    static bool scanSignatureDictionary(const QByteArray& data, PDFInteger offset, PDFObjectReference reference, bool isDirectInField, DictionaryScan& scan)
    {
        try
        {
            PDFLexicalAnalyzer analyzer(data.constData(), data.constData() + data.size());
            analyzer.seek(offset);

            const PDFLexicalAnalyzer::Token number = analyzer.fetch();
            const PDFLexicalAnalyzer::Token generation = analyzer.fetch();
            const PDFLexicalAnalyzer::Token command = analyzer.fetch();
            if (number.type != PDFLexicalAnalyzer::TokenType::Integer || number.data.toLongLong() != reference.objectNumber ||
                generation.type != PDFLexicalAnalyzer::TokenType::Integer || generation.data.toLongLong() != reference.generation ||
                command.type != PDFLexicalAnalyzer::TokenType::Command || command.data.toByteArray() != PDF_OBJECT_START_MARK)
            {
                return false;
            }

            if (analyzer.fetch().type != PDFLexicalAnalyzer::TokenType::DictionaryStart)
            {
                return false;
            }

            if (!isDirectInField)
            {
                scanDictionary(analyzer, scan);
                return true;
            }

            // The signature dictionary is the only value /V of the field
            int valueCount = 0;
            while (true)
            {
                const PDFLexicalAnalyzer::Token key = analyzer.fetch();
                if (key.type == PDFLexicalAnalyzer::TokenType::DictionaryEnd)
                {
                    return valueCount == 1;
                }
                if (key.type != PDFLexicalAnalyzer::TokenType::Name)
                {
                    return false;
                }

                const PDFLexicalAnalyzer::Token value = analyzer.fetch();
                if (key.data.toByteArray() == "V")
                {
                    ++valueCount;
                    if (value.type != PDFLexicalAnalyzer::TokenType::DictionaryStart)
                    {
                        return false;
                    }
                    scanDictionary(analyzer, scan);
                }
                else
                {
                    skipValue(analyzer, value);
                }
            }
        }
        catch (const PDFException&)
        {
            return false;
        }
    }

    /// Returns the offsets, where the reader reads the object: its entry of the cross-reference table of the
    /// file, as the reader reads it. A file, whose table cannot be read (the reader restores its objects from
    /// the bytes, Warning_Signature_FileRestored), gives the offsets of each "n g obj" of the object in front
    /// of the gap.
    static std::vector<PDFInteger> getObjectOffsets(const QByteArray& data, const PDFXRefTable* xrefTable, PDFObjectReference reference, PDFInteger gapStart)
    {
        std::vector<PDFInteger> offsets;

        if (xrefTable)
        {
            const PDFXRefTable::Entry& entry = xrefTable->getEntry(reference);
            if (entry.type == PDFXRefTable::EntryType::Occupied && entry.reference == reference)
            {
                offsets.push_back(entry.offset);
            }
            return offsets;
        }

        // Each "n" in front of the gap, the scan of the dictionary checks "n g obj" (at most 1024 of them)
        constexpr int MAXIMUM_CANDIDATES = 1024;
        const QByteArray number = QByteArray::number(reference.objectNumber);
        qsizetype position = gapStart > 0 ? data.lastIndexOf(number, gapStart - 1) : -1;
        while (position >= 0 && offsets.size() < MAXIMUM_CANDIDATES)
        {
            if (position == 0 || PDFLexicalAnalyzer::isWhitespace(data[position - 1]))
            {
                offsets.push_back(position);
            }
            position = position > 0 ? data.lastIndexOf(number, position - 1) : -1;
        }

        return offsets;
    }

    /// Checks the layout of a signature in its file (B109, R6-22, B111, B123): /ByteRange is exactly
    /// [0 a b c], 0 < a < b, b + c is the end of a revision, data[a] = '<', data[b - 1] = '>', the gap
    /// [a, b) is the value of /Contents of this signature dictionary at the offset, where the reader
    /// reads it, the dictionary has one /Contents and one /ByteRange, and /Contents is one DER object
    /// of its exact length followed only by zeros.
    static PDFSignatureLayout checkLayout(const PDFFormFieldSignature* signatureField,
                                          const QByteArray& data,
                                          const std::vector<Revision>& revisions,
                                          const PDFXRefTable* xrefTable)
    {
        PDFSignatureLayout layout;
        const PDFSignature& signature = signatureField->getSignature();
        const QByteArray& contents = signature.getContents();

        if (contents.size() > PDFSignatureHandler::MAXIMUM_SIGNATURE_SIZE)
        {
            layout.error = PDFTranslationContext::tr("The signature is too large.");
            return layout;
        }

        // The shape [0 a b c], each value checked against the size of the file before it is added (B111)
        const PDFSignature::ByteRanges& byteRanges = signature.getByteRanges();
        const PDFInteger size = data.size();
        if (byteRanges.size() != 2 || byteRanges[0].offset != 0)
        {
            layout.error = PDFTranslationContext::tr("The byte range of the signature is not [0 a b c].");
            return layout;
        }

        const PDFInteger a = byteRanges[0].size;
        const PDFInteger b = byteRanges[1].offset;
        const PDFInteger c = byteRanges[1].size;
        if (a <= 0 || b <= a || b > size || c < 0 || c > size - b)
        {
            layout.error = PDFTranslationContext::tr("The byte range of the signature is outside of the file.");
            return layout;
        }

        const PDFInteger end = b + c;
        auto revisionIt = std::find_if(revisions.cbegin(), revisions.cend(), [end](const Revision& revision) { return revision.eofEnd == end || revision.end == end; });
        if (revisionIt == revisions.cend())
        {
            layout.error = PDFTranslationContext::tr("The byte range of the signature does not end at the end of a revision of the file.");
            return layout;
        }

        if (data[a] != '<' || data[b - 1] != '>')
        {
            layout.error = PDFTranslationContext::tr("The gap of the byte range of the signature is not a hexadecimal string.");
            return layout;
        }

        // The gap is the value of /Contents of this signature dictionary (R6-22): the bytes of the object,
        // where the reader reads it, never a search for the bytes of the signature in the file
        const bool isDirectInField = !signature.getSelfReference().isValid();
        const PDFObjectReference reference = isDirectInField ? signatureField->getSelfReference() : signature.getSelfReference();
        bool isGapOfThisDictionary = false;
        for (const PDFInteger offset : getObjectOffsets(data, xrefTable, reference, a))
        {
            DictionaryScan scan;
            if (scanSignatureDictionary(data, offset, reference, isDirectInField, scan) &&
                scan.contentsCount == 1 && scan.contentsStart == a && scan.contentsEnd == b && scan.contents == contents &&
                scan.byteRangeCount == 1 && scan.isByteRangeIntegers && scan.byteRange == std::vector<PDFInteger>{ 0, a, b, c })
            {
                isGapOfThisDictionary = true;
                break;
            }
        }

        if (!isGapOfThisDictionary)
        {
            layout.error = PDFTranslationContext::tr("The gap of the byte range is not the signature of this signature dictionary.");
            return layout;
        }

        // /Contents: one object of its exact length, the rest zeros (R6-22): data in the gap,
        // which are not the signature, could be read as a part of the document
        const unsigned char tag = signature.getSubfilter() == "adbe.x509.rsa_sha1" ? 0x04 : 0x30;
        const PDFInteger derLength = getSignatureObjectLength(contents, tag);
        if (derLength < 0 || std::any_of(contents.cbegin() + derLength, contents.cend(), [](char value) { return value != 0; }))
        {
            layout.error = PDFTranslationContext::tr("The signature is not one encoded object followed only by zeros.");
            return layout;
        }

        layout.isValid = true;
        layout.gapStart = a;
        layout.gapEnd = b;
        layout.end = end;
        layout.revision = int(std::distance(revisions.cbegin(), revisionIt));
        layout.der = contents.left(derLength);
        return layout;
    }

    /// Reads the cross-reference table of the file as the reader reads it (the last "startxref" of the last
    /// 1024 bytes); nullptr, when it cannot be read
    static std::unique_ptr<PDFXRefTable> readXRefTable(const QByteArray& data)
    {
        const qsizetype eofOffset = data.lastIndexOf(PDF_END_OF_FILE_MARK);
        if (eofOffset < 0 || eofOffset < data.size() - PDF_FOOTER_SCAN_LIMIT)
        {
            return nullptr;
        }

        const PDFInteger offset = findStartXRef(data, eofOffset);
        if (offset < 0)
        {
            return nullptr;
        }

        try
        {
            auto table = std::make_unique<PDFXRefTable>();
            table->readXRefTable(nullptr, data, offset);
            return table;
        }
        catch (const PDFException&)
        {
            return nullptr;
        }
    }
};

/// Classification of a revision of a file after a signed revision (Q6.5, R6-3; ISO 32000-2, 12.8.1,
/// 12.8.2.2.1 and Table 257): its objects are compared by value with the previous revision, each
/// changed object must be explained by the allowed list, anything else is a change of the document.
/// Allowed: new signature fields, whose value is a signature dictionary of a valid signature, which
/// ends this revision, and their widgets in /Annots of the pages (a visible one is an information);
/// an empty signature field signed in this revision (its /V, /AP, /StructParent); /AcroForm /Fields and
/// /SigFlags; /DSS; in a tagged document the new element /Form of the widget of such a signature, its
/// key in /ParentTree and /ParentTreeNextKey (Q6.18); and, for a certification with P = 2, the values
/// of form fields with their appearances.
class PDFSignatureRevisionClassifier
{
public:
    /// Kind of a revision, from the least to the most significant
    enum class Kind
    {
        Unchanged,              ///< Nothing shown was changed
        DocumentSecurityStore,  ///< Only validation data (/DSS) were added or changed
        Timestamp,              ///< Only invisible document timestamps were added (and validation data)
        Signatures,             ///< Signatures were added (and timestamps, validation data)
        FormFilling,            ///< Values of form fields were filled in (and signatures, ...)
        Changed                 ///< The document was changed
    };

    struct Classification
    {
        Kind kind = Kind::Changed;
        QStringList infos;
    };

    /// A signature field, whose signature is valid (B109, R6-22, R6-2 and its value)
    struct SignedField
    {
        int revision = -1;  ///< Index of the revision, which the signature ends
        PDFSignature::Type type = PDFSignature::Type::Invalid;
    };
    using SignedFields = std::map<PDFObjectReference, SignedField>;

    explicit PDFSignatureRevisionClassifier(const PDFDocument& previous, const PDFDocument& current, int revision, const SignedFields& signedFields) :
        m_previous(previous),
        m_current(current),
        m_revision(revision),
        m_signedFields(signedFields)
    {

    }

    /// Classifies the revision
    Classification classify();

    /// Classifies the revisions after each signature (R6-3) and evaluates the certifications (DocMDP): sets
    /// LaterRevisions, the warning of a change, the error of a not allowed change and the informations
    /// \param sourceData Bytes of the file
    /// \param revisions Revisions of the file
    /// \param signatureFields Signature fields
    /// \param layouts Layouts of the signatures of the fields (an invalid one for a field without a verified signature)
    /// \param results Results of the fields
    /// \param securityHandler The authenticated handler of the document of the file, by which the revisions of an encrypted file are read (R6-1), may be null
    static void classifyLaterRevisions(const QByteArray& sourceData,
                                       const PDFSecurityHandler* securityHandler,
                                       const std::vector<PDFSignatureHandlerHelper::Revision>& revisions,
                                       const std::vector<const PDFFormFieldSignature*>& signatureFields,
                                       const std::vector<PDFSignatureLayout>& layouts,
                                       std::vector<PDFSignatureVerificationResult>& results);

    /// Returns true, if the object refers to one of the references (also inside of its arrays and dictionaries)
    static bool refersTo(const PDFObject& object, const std::set<PDFObjectReference>& references);

private:
    /// Returns true, if the dictionaries differ only in the values of the keys (a missing key is a null value)
    static bool isOnlyChanged(const PDFDictionary* previous, const PDFDictionary* current, std::initializer_list<const char*> keys);

    /// Returns true, if the dictionary has only the keys
    static bool hasOnlyKeys(const PDFDictionary* dictionary, std::initializer_list<const char*> keys);

    /// Returns items of an array (a reference is resolved), an empty list for a null object, false for another object
    static bool getArrayItems(const PDFDocument& document, const PDFObject& object, std::vector<PDFObject>& items);

    /// Returns the items of the current array, which are not in the previous one, false, when an item of the previous array is missing
    static bool getAddedItems(const std::vector<PDFObject>& previous, const std::vector<PDFObject>& current, std::vector<PDFObject>& added);

    /// Returns true, if the widget annotation is shown: a nonempty /Rect, an appearance /N and not hidden (Table 167, 170)
    bool isVisibleWidget(const PDFDictionary* widget) const;

    /// Returns true, if the object of the current revision is a new field of a valid signature of this revision
    bool isNewSignatureField(PDFObjectReference reference) const;

    /// Records a signature field of this revision (its kind)
    void addSignatureField(PDFObjectReference reference);

    /// Records a widget of a signature field of this revision, which is visible
    void addVisibleWidget(PDFObjectReference widget, std::optional<size_t> pageIndex);

    /// Returns the index of the page of the current revision, whose /Annots has the annotation
    std::optional<size_t> findPageOfAnnotation(PDFObjectReference annotation) const;

    /// Returns the type of the field of a dictionary (its /FT or of its parents), an empty array, when it is not a field
    QByteArray getFieldType(const PDFDictionary* dictionary) const;

    /// Returns true, if the value of the field of the object (its /V or of its parent with /V) changed in this revision
    bool isFieldValueChanged(PDFObjectReference reference) const;

    /// Erases an explained object from the changed objects
    void explain(PDFObjectReference reference) { m_modified.erase(reference); }
    bool isModified(PDFObjectReference reference) const { return m_modified.count(reference); }
    bool isCreated(PDFObjectReference reference) const { return m_created.count(reference); }

    void classifyCatalogAndForm(const PDFDictionary* previousCatalog, const PDFDictionary* currentCatalog);
    void classifySignedEmptyFields();
    void classifyDocumentSecurityStore(const PDFDictionary* previousCatalog, const PDFDictionary* currentCatalog);
    void classifyPages();
    void classifyStructure(const PDFDictionary* currentCatalog);
    void classifyMetadata(const PDFDictionary* currentTrailer, const PDFDictionary* currentCatalog);
    void classifyFieldsAndWidgets();
    void classifyAppearanceStreams();

    /// Returns true, if the object of the current revision is a widget of a signature field of this revision
    bool isSignatureWidget(PDFObjectReference reference) const;

    /// Returns true, if the object is a new structure element /Form of a widget of a signature of this
    /// revision (ISO 32000-2, 14.7.4.4, a reference of the object /OBJR; ISO 14289-1, 7.18.1, Q6.18)
    bool isNewFormElement(const PDFObject& object) const;

    /// Returns the kids /K of a structure element as a list (a single kid is a list of one item)
    static bool getStructureKids(const PDFDocument& document, const PDFDictionary* element, std::vector<PDFObject>& kids);

    /// Returns true, if the number tree /ParentTree of the current revision only added pairs of a new key
    /// and a new element /Form to the previous one (its /Nums, or new leaves in its /Kids; 7.9.7)
    bool isParentTreeOnlyExtended(const PDFObject& previousTree, const PDFObject& currentTree) const;

    const PDFDocument& m_previous;
    const PDFDocument& m_current;
    int m_revision;
    const SignedFields& m_signedFields;

    std::set<PDFObjectReference> m_modified;            ///< Objects of both revisions with another value, not explained yet
    std::set<PDFObjectReference> m_created;             ///< Objects of the current revision only
    std::set<PDFObjectReference> m_signatureFields;     ///< Signature fields signed in this revision
    std::set<PDFObjectReference> m_filledFields;        ///< Fields and widgets, whose value was filled in
    std::set<PDFObjectReference> m_visibleWidgets;      ///< Visible widgets of the signature fields of this revision
    bool m_isChanged = false;
    bool m_hasSignature = false;
    bool m_hasTimestamp = false;
    bool m_hasVisibleTimestamp = false;
    bool m_hasDocumentSecurityStore = false;
    QStringList m_infos;
};

bool PDFSignatureRevisionClassifier::isOnlyChanged(const PDFDictionary* previous, const PDFDictionary* current, std::initializer_list<const char*> keys)
{
    if (!previous || !current)
    {
        return false;
    }

    auto isAllowed = [&keys](const PDFInplaceOrMemoryString& key)
    {
        return std::any_of(keys.begin(), keys.end(), [&key](const char* allowedKey) { return key == allowedKey; });
    };

    for (size_t i = 0; i < previous->getCount(); ++i)
    {
        const PDFInplaceOrMemoryString& key = previous->getKey(i);
        if (!isAllowed(key) && previous->getValue(i) != current->get(key))
        {
            return false;
        }
    }

    for (size_t i = 0; i < current->getCount(); ++i)
    {
        const PDFInplaceOrMemoryString& key = current->getKey(i);
        if (!isAllowed(key) && current->getValue(i) != previous->get(key))
        {
            return false;
        }
    }

    return true;
}

bool PDFSignatureRevisionClassifier::hasOnlyKeys(const PDFDictionary* dictionary, std::initializer_list<const char*> keys)
{
    for (size_t i = 0; i < dictionary->getCount(); ++i)
    {
        const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
        if (!dictionary->getValue(i).isNull() && std::none_of(keys.begin(), keys.end(), [&key](const char* allowedKey) { return key == allowedKey; }))
        {
            return false;
        }
    }
    return true;
}

bool PDFSignatureRevisionClassifier::getArrayItems(const PDFDocument& document, const PDFObject& object, std::vector<PDFObject>& items)
{
    items.clear();
    const PDFObject& dereferencedObject = document.getObject(object);
    if (dereferencedObject.isNull())
    {
        return true;
    }
    if (!dereferencedObject.isArray())
    {
        return false;
    }

    const PDFArray* array = dereferencedObject.getArray();
    items.assign(array->begin(), array->end());
    return true;
}

bool PDFSignatureRevisionClassifier::getAddedItems(const std::vector<PDFObject>& previous, const std::vector<PDFObject>& current, std::vector<PDFObject>& added)
{
    // Each item of the previous array stays (as a multiset), the rest of the current one was added
    std::vector<bool> isUsed(current.size(), false);
    for (const PDFObject& item : previous)
    {
        bool isFound = false;
        for (size_t i = 0; i < current.size(); ++i)
        {
            if (!isUsed[i] && current[i] == item)
            {
                isUsed[i] = true;
                isFound = true;
                break;
            }
        }

        if (!isFound)
        {
            return false;
        }
    }

    added.clear();
    for (size_t i = 0; i < current.size(); ++i)
    {
        if (!isUsed[i])
        {
            added.push_back(current[i]);
        }
    }
    return true;
}

bool PDFSignatureRevisionClassifier::refersTo(const PDFObject& object, const std::set<PDFObjectReference>& references)
{
    // The nesting of direct objects is limited by the parser (PDFParser::MAXIMUM_NESTING)
    switch (object.getType())
    {
        case PDFObject::Type::Reference:
            return references.count(object.getReference());

        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            return std::any_of(array->begin(), array->end(), [&references](const PDFObject& item) { return refersTo(item, references); });
        }

        case PDFObject::Type::Dictionary:
        case PDFObject::Type::Stream:
        {
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                if (refersTo(dictionary->getValue(i), references))
                {
                    return true;
                }
            }
            return false;
        }

        default:
            return false;
    }
}

bool PDFSignatureRevisionClassifier::isVisibleWidget(const PDFDictionary* widget) const
{
    PDFDocumentDataLoaderDecorator loader(&m_current);
    const PDFInteger flags = loader.readIntegerFromDictionary(widget, "F", 0);
    if (flags & (2 | 32))
    {
        // Hidden or NoView (ISO 32000-2, Table 167)
        return false;
    }

    const QRectF rect = loader.readRectangle(widget->get("Rect"), QRectF());
    const PDFDictionary* appearance = m_current.getDictionaryFromObject(widget->get("AP"));
    return !rect.normalized().isEmpty() && appearance && !m_current.getObject(appearance->get("N")).isNull();
}

bool PDFSignatureRevisionClassifier::isNewSignatureField(PDFObjectReference reference) const
{
    auto it = m_signedFields.find(reference);
    if (it == m_signedFields.cend() || it->second.revision != m_revision || !isCreated(reference))
    {
        return false;
    }

    const PDFDictionary* field = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
    return field && getFieldType(field) == "Sig";
}

void PDFSignatureRevisionClassifier::addSignatureField(PDFObjectReference reference)
{
    m_signatureFields.insert(reference);
    auto it = m_signedFields.find(reference);
    if (it != m_signedFields.cend() && it->second.type == PDFSignature::Type::DocTimeStamp)
    {
        m_hasTimestamp = true;
    }
    else
    {
        m_hasSignature = true;
    }
}

void PDFSignatureRevisionClassifier::addVisibleWidget(PDFObjectReference widget, std::optional<size_t> pageIndex)
{
    if (!m_visibleWidgets.insert(widget).second)
    {
        return;
    }

    m_hasVisibleTimestamp = m_hasVisibleTimestamp || m_hasTimestamp;
    if (pageIndex.has_value())
    {
        m_infos << PDFTranslationContext::tr("A visible signature was added on page %1.").arg(pageIndex.value() + 1);
    }
    else
    {
        m_infos << PDFTranslationContext::tr("A visible signature was added.");
    }
}

std::optional<size_t> PDFSignatureRevisionClassifier::findPageOfAnnotation(PDFObjectReference annotation) const
{
    const PDFCatalog* catalog = m_current.getCatalog();
    const PDFObject annotationObject = PDFObject::createReference(annotation);
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFDictionary* page = m_current.getDictionaryFromObject(m_current.getObjectByReference(catalog->getPage(i)->getPageReference()));
        std::vector<PDFObject> annotations;
        if (page && getArrayItems(m_current, page->get("Annots"), annotations) && std::count(annotations.cbegin(), annotations.cend(), annotationObject))
        {
            return i;
        }
    }
    return std::nullopt;
}

QByteArray PDFSignatureRevisionClassifier::getFieldType(const PDFDictionary* dictionary) const
{
    // The type of a field is inheritable (ISO 32000-2, Table 226), its parents are walked with a limit
    constexpr int MAXIMUM_DEPTH = 32;
    std::set<const PDFDictionary*> visited;
    for (int depth = 0; dictionary && depth < MAXIMUM_DEPTH && visited.insert(dictionary).second; ++depth)
    {
        const PDFObject& type = m_current.getObject(dictionary->get("FT"));
        if (type.isName())
        {
            return type.getString();
        }
        dictionary = m_current.getDictionaryFromObject(dictionary->get("Parent"));
    }
    return QByteArray();
}

void PDFSignatureRevisionClassifier::classifyCatalogAndForm(const PDFDictionary* previousCatalog, const PDFDictionary* currentCatalog)
{
    const PDFObject& rootObject = m_current.getTrailerDictionary()->get("Root");
    if (rootObject.isReference() && isModified(rootObject.getReference()))
    {
        // The catalog may change only its form and its validation data (R6-3)
        if (!isOnlyChanged(previousCatalog, currentCatalog, { "AcroForm", "DSS" }))
        {
            m_isChanged = true;
            return;
        }
        explain(rootObject.getReference());
    }

    // The form: only its fields and the flags of the signatures (a new form also NeedAppearances false)
    const PDFObject& previousFormObject = previousCatalog->get("AcroForm");
    const PDFObject& currentFormObject = currentCatalog->get("AcroForm");
    const PDFObject& previousForm = m_previous.getObject(previousFormObject);
    const PDFObject& currentForm = m_current.getObject(currentFormObject);
    const PDFDictionary* previousFormDictionary = previousForm.isDictionary() ? previousForm.getDictionary() : nullptr;
    const PDFDictionary* currentFormDictionary = currentForm.isDictionary() ? currentForm.getDictionary() : nullptr;

    if (previousForm != currentForm)
    {
        if (!currentFormDictionary || (!previousFormDictionary && !previousForm.isNull()))
        {
            m_isChanged = true;
            return;
        }

        if (previousFormDictionary && !isOnlyChanged(previousFormDictionary, currentFormDictionary, { "Fields", "SigFlags" }))
        {
            m_isChanged = true;
            return;
        }

        const PDFObject& needAppearances = m_current.getObject(currentFormDictionary->get("NeedAppearances"));
        if (!previousFormDictionary && (!hasOnlyKeys(currentFormDictionary, { "Fields", "SigFlags", "NeedAppearances" }) ||
                                        (needAppearances.isBool() && needAppearances.getBool())))
        {
            m_isChanged = true;
            return;
        }
    }

    if (currentFormObject.isReference() && isModified(currentFormObject.getReference()))
    {
        // The same object in both revisions, the form of both of them
        if (!previousFormObject.isReference() || previousFormObject.getReference() != currentFormObject.getReference() || !previousFormDictionary)
        {
            m_isChanged = true;
            return;
        }
        explain(currentFormObject.getReference());
    }

    if (!currentFormDictionary)
    {
        return;
    }

    // The fields: only added fields of valid signatures of this revision
    const PDFObject previousFieldsObject = previousFormDictionary ? previousFormDictionary->get("Fields") : PDFObject();
    const PDFObject& currentFieldsObject = currentFormDictionary->get("Fields");
    if (currentFieldsObject.isReference() && isModified(currentFieldsObject.getReference()))
    {
        if (!previousFieldsObject.isReference() || previousFieldsObject.getReference() != currentFieldsObject.getReference() ||
            !m_previous.getObject(previousFieldsObject).isArray())
        {
            m_isChanged = true;
            return;
        }
        explain(currentFieldsObject.getReference());
    }

    std::vector<PDFObject> previousFields;
    std::vector<PDFObject> currentFields;
    std::vector<PDFObject> addedFields;
    if (!getArrayItems(m_previous, previousFieldsObject, previousFields) ||
        !getArrayItems(m_current, currentFieldsObject, currentFields) ||
        !getAddedItems(previousFields, currentFields, addedFields))
    {
        m_isChanged = true;
        return;
    }

    for (const PDFObject& field : addedFields)
    {
        if (!field.isReference() || !isNewSignatureField(field.getReference()))
        {
            // A new field, which is not signed by a valid signature of this revision: the Sneaky Signature
            // Attack covers the signed content by an unsigned signature field with its own appearance (R6-3)
            m_isChanged = true;
            return;
        }
        addSignatureField(field.getReference());
    }
}

void PDFSignatureRevisionClassifier::classifySignedEmptyFields()
{
    // An empty signature field of the previous revision signed in this one (Q6.3): only its value, its
    // appearance and, in a tagged document, the key of its widget in /ParentTree (Q6.18)
    const std::set<PDFObjectReference> modified = m_modified;
    for (const PDFObjectReference& reference : modified)
    {
        auto it = m_signedFields.find(reference);
        if (it == m_signedFields.cend() || it->second.revision != m_revision)
        {
            continue;
        }

        const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(reference));
        const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
        if (!previous || !current || !m_previous.getObject(previous->get("V")).isNull() || getFieldType(current) != "Sig" ||
            !isOnlyChanged(previous, current, { "V", "AP", "StructParent" }))
        {
            continue;
        }

        explain(reference);
        addSignatureField(reference);
        if (previous->get("AP") != current->get("AP") && isVisibleWidget(current))
        {
            addVisibleWidget(reference, findPageOfAnnotation(reference));
        }
    }
}

void PDFSignatureRevisionClassifier::classifyDocumentSecurityStore(const PDFDictionary* previousCatalog, const PDFDictionary* currentCatalog)
{
    const PDFObject& previousObject = previousCatalog->get("DSS");
    const PDFObject& currentObject = currentCatalog->get("DSS");
    const PDFDictionary* previousStore = m_previous.getDictionaryFromObject(previousObject);
    const PDFDictionary* currentStore = m_current.getDictionaryFromObject(currentObject);

    if (m_previous.getObject(previousObject) != m_current.getObject(currentObject))
    {
        // The validation data (ISO 32000-2, 12.8.4.3, Table 261)
        if (!currentStore || (!previousStore && !m_previous.getObject(previousObject).isNull()) ||
            (previousStore && !isOnlyChanged(previousStore, currentStore, { "Type", "Certs", "OCSPs", "CRLs", "VRI" })) ||
            (!previousStore && !hasOnlyKeys(currentStore, { "Type", "Certs", "OCSPs", "CRLs", "VRI" })))
        {
            m_isChanged = true;
            return;
        }
        m_hasDocumentSecurityStore = true;
    }

    if (!currentStore)
    {
        return;
    }

    if (currentObject.isReference() && isModified(currentObject.getReference()))
    {
        if (!previousObject.isReference() || previousObject.getReference() != currentObject.getReference() || !previousStore)
        {
            m_isChanged = true;
            return;
        }
        explain(currentObject.getReference());
    }

    // Its arrays of streams and the dictionary of the signatures, when they are objects of their own
    for (const char* key : { "Certs", "OCSPs", "CRLs", "VRI" })
    {
        const PDFObject& value = currentStore->get(key);
        if (!value.isReference() || !isModified(value.getReference()))
        {
            continue;
        }

        const bool isVRI = std::strcmp(key, "VRI") == 0;
        const PDFObject& previousValue = m_previous.getObjectByReference(value.getReference());
        const PDFObject& currentValue = m_current.getObjectByReference(value.getReference());
        bool isAllowed = previousStore && previousStore->get(key) == value &&
                         (isVRI ? previousValue.isDictionary() && currentValue.isDictionary() : previousValue.isArray() && currentValue.isArray());
        if (isAllowed && isVRI)
        {
            const PDFDictionary* dictionary = currentValue.getDictionary();
            for (size_t i = 0; i < dictionary->getCount() && isAllowed; ++i)
            {
                isAllowed = m_current.getObject(dictionary->getValue(i)).isDictionary();
            }
        }
        else if (isAllowed)
        {
            const PDFArray* array = currentValue.getArray();
            isAllowed = std::all_of(array->begin(), array->end(), [this](const PDFObject& item) { return m_current.getObject(item).isStream(); });
        }

        if (!isAllowed)
        {
            m_isChanged = true;
            return;
        }
        explain(value.getReference());
    }
}

void PDFSignatureRevisionClassifier::classifyPages()
{
    // The pages of the current revision and their arrays of annotations, which are objects of their own
    const PDFCatalog* catalog = m_current.getCatalog();
    std::map<PDFObjectReference, size_t> pages;
    std::map<PDFObjectReference, size_t> annotationArrays;
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFObjectReference pageReference = catalog->getPage(i)->getPageReference();
        pages[pageReference] = i;
        if (const PDFDictionary* page = m_current.getDictionaryFromObject(m_current.getObjectByReference(pageReference)))
        {
            const PDFObject& annotations = page->get("Annots");
            if (annotations.isReference())
            {
                annotationArrays[annotations.getReference()] = i;
            }
        }
    }

    // A page may change only its annotations, an array of annotations of a page is the same one
    std::set<size_t> changedPages;
    const std::set<PDFObjectReference> modified = m_modified;
    for (const PDFObjectReference& reference : modified)
    {
        auto pageIt = pages.find(reference);
        if (pageIt != pages.cend())
        {
            const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(reference));
            const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
            if (isOnlyChanged(previous, current, { "Annots" }))
            {
                explain(reference);
                changedPages.insert(pageIt->second);
            }
            continue;
        }

        auto arrayIt = annotationArrays.find(reference);
        if (arrayIt != annotationArrays.cend())
        {
            const PDFObjectReference pageReference = catalog->getPage(arrayIt->second)->getPageReference();
            const PDFDictionary* previousPage = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(pageReference));
            if (previousPage && previousPage->get("Annots") == PDFObject::createReference(reference) && m_previous.getObjectByReference(reference).isArray())
            {
                explain(reference);
                changedPages.insert(arrayIt->second);
            }
        }
    }

    for (const size_t pageIndex : changedPages)
    {
        const PDFObjectReference pageReference = catalog->getPage(pageIndex)->getPageReference();
        const PDFDictionary* previousPage = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(pageReference));
        const PDFDictionary* currentPage = m_current.getDictionaryFromObject(m_current.getObjectByReference(pageReference));

        std::vector<PDFObject> previousAnnotations;
        std::vector<PDFObject> currentAnnotations;
        std::vector<PDFObject> addedAnnotations;
        if (!previousPage || !currentPage ||
            !getArrayItems(m_previous, previousPage->get("Annots"), previousAnnotations) ||
            !getArrayItems(m_current, currentPage->get("Annots"), currentAnnotations) ||
            !getAddedItems(previousAnnotations, currentAnnotations, addedAnnotations))
        {
            // An annotation was removed or the array is not an array
            m_isChanged = true;
            return;
        }

        for (const PDFObject& annotation : addedAnnotations)
        {
            // Only a new widget of a signature field of this revision: any other annotation can cover the
            // signed content (Evil Annotation Attack, R6-3)
            const PDFDictionary* widget = annotation.isReference() && isCreated(annotation.getReference()) ? m_current.getDictionaryFromObject(annotation) : nullptr;
            const PDFObject subtype = widget ? m_current.getObject(widget->get("Subtype")) : PDFObject();
            if (!widget || !subtype.isName() || subtype.getString() != "Widget")
            {
                m_isChanged = true;
                return;
            }

            const PDFObject& parent = widget->get("Parent");
            const bool isFieldOfThisRevision = m_signatureFields.count(annotation.getReference()) ||
                                               (parent.isReference() && m_signatureFields.count(parent.getReference()));
            if (!isFieldOfThisRevision)
            {
                m_isChanged = true;
                return;
            }

            if (isVisibleWidget(widget))
            {
                addVisibleWidget(annotation.getReference(), pageIndex);
            }
        }
    }
}

bool PDFSignatureRevisionClassifier::isSignatureWidget(PDFObjectReference reference) const
{
    if (m_signatureFields.count(reference))
    {
        // A field merged with its widget, or a field of kids (its own /OBJR is then not a widget)
        const PDFDictionary* field = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
        const PDFObject subtype = field ? m_current.getObject(field->get("Subtype")) : PDFObject();
        return subtype.isName() && subtype.getString() == "Widget";
    }

    const PDFDictionary* widget = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
    const PDFObject subtype = widget ? m_current.getObject(widget->get("Subtype")) : PDFObject();
    const PDFObject parent = widget ? widget->get("Parent") : PDFObject();
    return subtype.isName() && subtype.getString() == "Widget" && parent.isReference() && m_signatureFields.count(parent.getReference());
}

bool PDFSignatureRevisionClassifier::isNewFormElement(const PDFObject& object) const
{
    if (!object.isReference() || !isCreated(object.getReference()))
    {
        return false;
    }

    // ISO 32000-2, Table 355: /Type /StructElem, /S /Form, /P, /Pg, /K - nothing else (no /A, /ActualText, ...)
    const PDFDictionary* element = m_current.getDictionaryFromObject(object);
    const PDFObject type = element ? m_current.getObject(element->get("S")) : PDFObject();
    if (!element || !type.isName() || type.getString() != "Form" || !hasOnlyKeys(element, { "Type", "S", "P", "Pg", "K" }))
    {
        return false;
    }

    // Its content is the reference of the object /OBJR of the widget of the signature (Table 359)
    const PDFObject& content = element->get("K");
    if (content.isReference() && !isCreated(content.getReference()))
    {
        return false;
    }

    const PDFDictionary* objectReference = m_current.getDictionaryFromObject(content);
    const PDFObject objectType = objectReference ? m_current.getObject(objectReference->get("Type")) : PDFObject();
    const PDFObject widget = objectReference ? objectReference->get("Obj") : PDFObject();
    return objectType.isName() && objectType.getString() == "OBJR" && hasOnlyKeys(objectReference, { "Type", "Obj", "Pg" }) &&
           widget.isReference() && isSignatureWidget(widget.getReference());
}

bool PDFSignatureRevisionClassifier::getStructureKids(const PDFDocument& document, const PDFDictionary* element, std::vector<PDFObject>& kids)
{
    kids.clear();
    const PDFObject& value = element->get("K");
    if (document.getObject(value).isArray())
    {
        return getArrayItems(document, value, kids);
    }

    if (!value.isNull())
    {
        kids.push_back(value);
    }
    return true;
}

bool PDFSignatureRevisionClassifier::isParentTreeOnlyExtended(const PDFObject& previousTree, const PDFObject& currentTree) const
{
    const PDFDictionary* previous = m_previous.getDictionaryFromObject(previousTree);
    const PDFDictionary* current = m_current.getDictionaryFromObject(currentTree);
    if (!current || (!previous && !m_previous.getObject(previousTree).isNull()))
    {
        return false;
    }

    if (previous ? !isOnlyChanged(previous, current, { "Nums", "Kids" }) : !hasOnlyKeys(current, { "Nums", "Kids" }))
    {
        return false;
    }
    const PDFObject previousNums = previous ? previous->get("Nums") : PDFObject();
    const PDFObject previousKids = previous ? previous->get("Kids") : PDFObject();

    // Pairs of a key and of a new element /Form
    auto isPairsOfNewElements = [this](const std::vector<PDFObject>& items)
    {
        if (items.size() % 2 != 0)
        {
            return false;
        }

        for (size_t i = 0; i < items.size(); i += 2)
        {
            if (!m_current.getObject(items[i]).isInt() || !isNewFormElement(items[i + 1]))
            {
                return false;
            }
        }
        return true;
    };

    std::vector<PDFObject> previousItems;
    std::vector<PDFObject> currentItems;
    std::vector<PDFObject> addedItems;
    if (!getArrayItems(m_previous, previousNums, previousItems) ||
        !getArrayItems(m_current, current->get("Nums"), currentItems) ||
        currentItems.size() < previousItems.size() ||
        !std::equal(previousItems.cbegin(), previousItems.cend(), currentItems.cbegin()) ||
        !isPairsOfNewElements(std::vector<PDFObject>(currentItems.cbegin() + previousItems.size(), currentItems.cend())))
    {
        return false;
    }

    // New leaves of /Kids: /Limits and /Nums of new pairs only
    if (!getArrayItems(m_previous, previousKids, previousItems) ||
        !getArrayItems(m_current, current->get("Kids"), currentItems) ||
        !getAddedItems(previousItems, currentItems, addedItems))
    {
        return false;
    }

    for (const PDFObject& leaf : addedItems)
    {
        const PDFDictionary* leafDictionary = leaf.isReference() && isCreated(leaf.getReference()) ? m_current.getDictionaryFromObject(leaf) : nullptr;
        if (!leafDictionary || !hasOnlyKeys(leafDictionary, { "Limits", "Nums" }) ||
            !getArrayItems(m_current, leafDictionary->get("Nums"), currentItems) || !isPairsOfNewElements(currentItems))
        {
            return false;
        }
    }
    return true;
}

void PDFSignatureRevisionClassifier::classifyStructure(const PDFDictionary* currentCatalog)
{
    // A visible signature of a tagged document (Q6.18; ISO 14289-1, 7.18.1; ISO 32000-2, 14.7.4.4, 14.7.5.4):
    // a new element /Form (its /OBJR is the widget of the signature) added to the kids of an element, its key
    // in /ParentTree and /ParentTreeNextKey of the root. Nothing else of the structure may change.
    const PDFObject& rootObject = currentCatalog->get("StructTreeRoot");
    if (!rootObject.isReference())
    {
        return;
    }

    const PDFObjectReference rootReference = rootObject.getReference();
    const PDFDictionary* previousRoot = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(rootReference));
    const PDFDictionary* currentRoot = m_current.getDictionaryFromObject(m_current.getObjectByReference(rootReference));
    if (!previousRoot || !currentRoot)
    {
        return;
    }

    if (isModified(rootReference))
    {
        if (!isOnlyChanged(previousRoot, currentRoot, { "ParentTreeNextKey", "ParentTree", "K" }))
        {
            m_isChanged = true;
            return;
        }

        const PDFObject previousKey = m_previous.getObject(previousRoot->get("ParentTreeNextKey"));
        const PDFObject currentKey = m_current.getObject(currentRoot->get("ParentTreeNextKey"));
        if (previousKey != currentKey && (!currentKey.isInt() || (previousKey.isInt() && currentKey.getInteger() < previousKey.getInteger())))
        {
            m_isChanged = true;
            return;
        }
    }

    // The parent tree: only pairs of new elements /Form added (an object of its own written again, or new)
    const PDFObject& previousTree = previousRoot->get("ParentTree");
    const PDFObject& currentTree = currentRoot->get("ParentTree");
    if (m_previous.getObject(previousTree) != m_current.getObject(currentTree))
    {
        if (!isParentTreeOnlyExtended(previousTree, currentTree))
        {
            m_isChanged = true;
            return;
        }

        if (currentTree.isReference() && isModified(currentTree.getReference()))
        {
            if (previousTree != currentTree)
            {
                m_isChanged = true;
                return;
            }
            explain(currentTree.getReference());
        }
    }

    // The elements (the root included), whose kids only got new elements /Form
    const std::set<PDFObjectReference> modified = m_modified;
    for (const PDFObjectReference& reference : modified)
    {
        const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(reference));
        const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
        const bool isElement = reference == rootReference || (previous && m_previous.getObject(previous->get("S")).isName());
        if (!isElement || !previous || !current || (reference != rootReference && !isOnlyChanged(previous, current, { "K" })))
        {
            continue;
        }

        std::vector<PDFObject> previousKids;
        std::vector<PDFObject> currentKids;
        std::vector<PDFObject> addedKids;
        if (!getStructureKids(m_previous, previous, previousKids) || !getStructureKids(m_current, current, currentKids) ||
            !getAddedItems(previousKids, currentKids, addedKids) ||
            !std::all_of(addedKids.cbegin(), addedKids.cend(), [this](const PDFObject& kid) { return isNewFormElement(kid); }))
        {
            m_isChanged = true;
            return;
        }
        explain(reference);
    }
}

void PDFSignatureRevisionClassifier::classifyMetadata(const PDFDictionary* currentTrailer, const PDFDictionary* currentCatalog)
{
    // Owner decision of 15. 11. 2026 (plan chapter 13, addendum): only a revision, which adds a valid signature
    // or a document timestamp, may also update the metadata as signers do (Acrobat): /ModDate of the same
    // /Info object, and the content of the same XMP stream of the catalog. Anything else stays a change.
    if (!m_hasSignature && !m_hasTimestamp)
    {
        return;
    }

    const PDFObject& infoObject = currentTrailer->get("Info");
    if (infoObject.isReference() && isModified(infoObject.getReference()))
    {
        const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(infoObject.getReference()));
        const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(infoObject.getReference()));
        if (isOnlyChanged(previous, current, { "ModDate" }))
        {
            explain(infoObject.getReference());
        }
    }

    const PDFObject& metadataObject = currentCatalog->get("Metadata");
    if (metadataObject.isReference() && isModified(metadataObject.getReference()))
    {
        const PDFObject& previous = m_previous.getObjectByReference(metadataObject.getReference());
        const PDFObject& current = m_current.getObjectByReference(metadataObject.getReference());
        auto isMetadataStream = [](const PDFDocument& document, const PDFObject& object)
        {
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : nullptr;
            const PDFObject type = dictionary ? document.getObject(dictionary->get("Type")) : PDFObject();
            const PDFObject subtype = dictionary ? document.getObject(dictionary->get("Subtype")) : PDFObject();
            return type.isName() && type.getString() == "Metadata" && subtype.isName() && subtype.getString() == "XML";
        };

        // ISO 32000-2, 14.3.2: the stream of the metadata stays one; only its content (and its length and filters) changes
        if (isMetadataStream(m_previous, previous) && isMetadataStream(m_current, current) &&
            isOnlyChanged(previous.getStream()->getDictionary(), current.getStream()->getDictionary(), { "Length", "Filter", "DecodeParms" }))
        {
            explain(metadataObject.getReference());
        }
    }
}

void PDFSignatureRevisionClassifier::classifyFieldsAndWidgets()
{
    const std::set<PDFObjectReference> modified = m_modified;
    for (const PDFObjectReference& reference : modified)
    {
        const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(reference));
        const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(reference));
        if (!previous || !current)
        {
            continue;
        }

        // A widget of an empty signature field signed in this revision: only its appearance (and its key in /ParentTree)
        const PDFObject& parent = current->get("Parent");
        if (parent.isReference() && m_signatureFields.count(parent.getReference()) && previous->get("Parent") == parent &&
            isOnlyChanged(previous, current, { "AP", "StructParent" }))
        {
            explain(reference);
            if (isVisibleWidget(current))
            {
                addVisibleWidget(reference, findPageOfAnnotation(reference));
            }
            continue;
        }

        // A field (or its widget) filled in: its value, its state and its appearance (ISO 32000-2, 12.7.4), and
        // the value of the field changed in this revision - a new appearance of the same value is not filling in
        const QByteArray fieldType = getFieldType(current);
        if ((fieldType == "Tx" || fieldType == "Ch" || fieldType == "Btn") &&
            isOnlyChanged(previous, current, { "V", "AS", "AP" }) && isFieldValueChanged(reference))
        {
            explain(reference);
            m_filledFields.insert(reference);
        }
    }
}

bool PDFSignatureRevisionClassifier::isFieldValueChanged(PDFObjectReference reference) const
{
    // The value is in the field or in one of its parents (inheritable, ISO 32000-2, Table 226): the first
    // one, which has /V, or the root of the field; the parents are walked with a visited set and a limit
    constexpr int MAXIMUM_DEPTH = 32;
    std::set<PDFObjectReference> visited;
    PDFObjectReference fieldReference = reference;
    for (int depth = 0; depth < MAXIMUM_DEPTH && visited.insert(fieldReference).second; ++depth)
    {
        const PDFDictionary* current = m_current.getDictionaryFromObject(m_current.getObjectByReference(fieldReference));
        const PDFDictionary* previous = m_previous.getDictionaryFromObject(m_previous.getObjectByReference(fieldReference));
        if (!current || !previous)
        {
            return false;
        }

        const PDFObject& parent = current->get("Parent");
        if (current->hasKey("V") || !parent.isReference())
        {
            return m_previous.getObject(previous->get("V")) != m_current.getObject(current->get("V"));
        }
        fieldReference = parent.getReference();
    }
    return false;
}

void PDFSignatureRevisionClassifier::classifyAppearanceStreams()
{
    // An appearance stream of a filled field written again under its number: each object of both revisions,
    // which refers to it, is a filled field or widget, which refers to it only from its appearance /AP
    std::set<PDFObjectReference> streams;
    for (const PDFObjectReference& reference : m_modified)
    {
        if (m_previous.getObjectByReference(reference).isStream() && m_current.getObjectByReference(reference).isStream())
        {
            streams.insert(reference);
        }
    }

    if (streams.empty() || m_filledFields.empty())
    {
        return;
    }

    std::set<PDFObjectReference> refused;
    for (const PDFDocument* document : { &m_previous, &m_current })
    {
        const PDFObjectStorage::PDFObjects& objects = document->getStorage().getObjects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const PDFObject& object = objects[i].object;
            if (!refersTo(object, streams))
            {
                continue;
            }

            // A widget of a filled field: filled itself, or its parent (a field of more widgets) was filled
            const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
            const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : nullptr;
            const PDFObject parent = dictionary ? dictionary->get("Parent") : PDFObject();
            const bool isFilledWidget = dictionary && (m_filledFields.count(reference) || (parent.isReference() && m_filledFields.count(parent.getReference())));
            for (const PDFObjectReference& stream : streams)
            {
                const std::set<PDFObjectReference> single = { stream };
                if (!refersTo(object, single))
                {
                    continue;
                }

                bool isOnlyAppearance = false;
                if (isFilledWidget)
                {
                    // The stream only in /AP of the widget
                    isOnlyAppearance = true;
                    for (size_t j = 0; j < dictionary->getCount() && isOnlyAppearance; ++j)
                    {
                        isOnlyAppearance = dictionary->getKey(j) == "AP" || !refersTo(dictionary->getValue(j), single);
                    }
                }

                if (!isOnlyAppearance)
                {
                    refused.insert(stream);
                }
            }
        }
    }

    for (const PDFObjectReference& stream : streams)
    {
        if (!refused.count(stream))
        {
            explain(stream);
        }
    }
}

PDFSignatureRevisionClassifier::Classification PDFSignatureRevisionClassifier::classify()
{
    Classification classification;

    const PDFDictionary* previousTrailer = m_previous.getTrailerDictionary();
    const PDFDictionary* currentTrailer = m_current.getTrailerDictionary();
    if (!previousTrailer || !currentTrailer)
    {
        return classification;
    }

    // The catalog, the encryption and the information of the document are the same objects
    for (const char* key : { "Root", "Encrypt", "Info" })
    {
        if (previousTrailer->get(key) != currentTrailer->get(key))
        {
            return classification;
        }
    }

    const PDFDictionary* previousCatalog = m_previous.getDictionaryFromObject(previousTrailer->get("Root"));
    const PDFDictionary* currentCatalog = m_current.getDictionaryFromObject(currentTrailer->get("Root"));
    if (!previousCatalog || !currentCatalog)
    {
        return classification;
    }

    // The objects of both revisions compared by value: a changed, a new and a removed object
    const PDFObjectStorage::PDFObjects& previousObjects = m_previous.getStorage().getObjects();
    const PDFObjectStorage::PDFObjects& currentObjects = m_current.getStorage().getObjects();
    const size_t count = qMax(previousObjects.size(), currentObjects.size());
    for (size_t i = 0; i < count; ++i)
    {
        const PDFObjectStorage::Entry* previous = i < previousObjects.size() ? &previousObjects[i] : nullptr;
        const PDFObjectStorage::Entry* current = i < currentObjects.size() ? &currentObjects[i] : nullptr;
        const bool hasPrevious = previous && !previous->object.isNull();
        const bool hasCurrent = current && !current->object.isNull();

        if (!hasPrevious && !hasCurrent)
        {
            continue;
        }

        if (!hasCurrent || (hasPrevious && previous->generation != current->generation))
        {
            // A removed object, or an object replaced by another generation
            return classification;
        }

        if (!hasPrevious)
        {
            m_created.insert(PDFObjectReference(PDFInteger(i), current->generation));
        }
        else if (previous->object != current->object)
        {
            m_modified.insert(PDFObjectReference(PDFInteger(i), current->generation));
        }
    }

    // A new object of a number, to which the previous revision refers, changes what an unchanged object shows
    // (an object prepared by an attacker before the signing, a reference to a missing object is null)
    if (!m_created.empty())
    {
        for (size_t i = 0; i < previousTrailer->getCount(); ++i)
        {
            if (refersTo(previousTrailer->getValue(i), m_created))
            {
                return classification;
            }
        }

        for (const PDFObjectStorage::Entry& entry : previousObjects)
        {
            if (refersTo(entry.object, m_created))
            {
                return classification;
            }
        }
    }

    classifyCatalogAndForm(previousCatalog, currentCatalog);
    if (!m_isChanged)
    {
        classifySignedEmptyFields();
        classifyDocumentSecurityStore(previousCatalog, currentCatalog);
    }
    if (!m_isChanged)
    {
        classifyPages();
    }
    if (!m_isChanged)
    {
        classifyStructure(currentCatalog);
        classifyMetadata(currentTrailer, currentCatalog);
    }
    if (!m_isChanged)
    {
        classifyFieldsAndWidgets();
        classifyAppearanceStreams();
    }

    if (m_isChanged || !m_modified.empty())
    {
        return classification;
    }

    if (!m_filledFields.empty())
    {
        classification.kind = Kind::FormFilling;
    }
    else if (m_hasSignature || m_hasVisibleTimestamp)
    {
        classification.kind = Kind::Signatures;
    }
    else if (m_hasTimestamp)
    {
        classification.kind = Kind::Timestamp;
    }
    else if (m_hasDocumentSecurityStore)
    {
        classification.kind = Kind::DocumentSecurityStore;
    }
    else
    {
        classification.kind = Kind::Unchanged;
    }

    classification.infos = m_infos;
    return classification;
}

std::vector<PDFSignatureVerificationResult> PDFSignatureHandler::verifySignatures(const PDFForm& form, const QByteArray& sourceData, const Parameters& parameters)
{
    std::vector<PDFSignatureVerificationResult> result;

    // A form of no type has only the signature widgets of a document without /AcroForm
    // (PDFForm::parse): they are verified as well, a signature is never overlooked
    if (parameters.enableVerification)
    {
        std::vector<const PDFFormFieldSignature*> signatureFields;
        auto getSignatureFields = [&signatureFields](const PDFFormField* field)
        {
            if (field->getFieldType() == PDFFormField::FieldType::Signature)
            {
                const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field);
                Q_ASSERT(signatureField);
                signatureFields.push_back(signatureField);
            }
        };
        form.apply(getSignatureFields);
        result.reserve(signatureFields.size());

        // A signature proves its bytes, not the shown objects. Without a cross-reference table, which can
        // be read, the reader takes the objects from the bytes, also those no reader by the table shows
        // and the signer has never seen, so a signature over the whole file proves nothing about them.
        // R6-1: an encrypted file and its revisions are read by a clone of the authenticated handler of the
        // document of the form (PDFDocumentReader::setAuthenticatedSecurityHandler), when it is of the file
        const PDFSecurityHandler* securityHandler = parameters.securityHandler;
        const bool isFileRestored = !signatureFields.empty() && PDFSignatureHandlerHelper::isFileRestoredByReader(sourceData, securityHandler);

        // The revisions of the file and its cross-reference table, as the reader reads it (B109, R6-22)
        const std::vector<PDFSignatureHandlerHelper::Revision> revisions = !signatureFields.empty() ? PDFSignatureHandlerHelper::findRevisions(sourceData) : std::vector<PDFSignatureHandlerHelper::Revision>();
        const std::unique_ptr<PDFXRefTable> xrefTable = !signatureFields.empty() ? PDFSignatureHandlerHelper::readXRefTable(sourceData) : nullptr;

        // The layout of each verified signature, for the classification of the later revisions
        std::vector<PDFSignatureLayout> layouts;
        layouts.reserve(signatureFields.size());
        int verifiedCount = 0;

        for (const PDFFormFieldSignature* signatureField : signatureFields)
        {
            const PDFSignature& signature = signatureField->getSignature();
            PDFSignatureLayout layout;
            std::unique_ptr<PDFSignatureHandler> signatureHandler;

            if (signature.getType() != PDFSignature::Type::Invalid && verifiedCount >= MAXIMUM_VERIFIED_SIGNATURES)
            {
                // B123: each signature copies its covered bytes, a limit of the work for one document
                PDFSignatureVerificationResult verificationResult(signature.getType(), signatureField->getSelfReference(), signatureField->getName(PDFFormField::NameType::FullyQualified));
                verificationResult.setSignatureHandler(signature.getSubfilter());
                verificationResult.addNotVerifiedError(PDFTranslationContext::tr("The signature was not verified, the document has more than %1 signatures.").arg(MAXIMUM_VERIFIED_SIGNATURES));
                result.emplace_back(std::move(verificationResult));
                layouts.emplace_back();
                continue;
            }

            layout = PDFSignatureHandlerHelper::checkLayout(signatureField, sourceData, revisions, xrefTable.get());
            signatureHandler.reset(createHandler(signatureField, sourceData, parameters, layout));

            if (signatureHandler && layout.isValid)
            {
                ++verifiedCount;
                result.emplace_back(signatureHandler->verify());

                if (isFileRestored)
                {
                    result.back().addSignatureFileRestoredWarning();
                }
            }
            else if (signatureHandler)
            {
                // The layout of the signature is invalid: its bytes are not what a reader shows, no part of it is verified
                ++verifiedCount;
                PDFSignatureVerificationResult verificationResult(signature.getType(), signatureField->getSelfReference(), signatureField->getName(PDFFormField::NameType::FullyQualified));
                verificationResult.setSignatureHandler(signature.getSubfilter());
                verificationResult.setCertificationPermission(signature.getDocMDPPermission());
                verificationResult.addByteRangeError(layout.error);
                result.emplace_back(std::move(verificationResult));
            }
            else
            {
                PDFObjectReference signatureFieldReference = signatureField->getSelfReference();
                QString qualifiedName = signatureField->getName(PDFFormField::NameType::FullyQualified);
                PDFSignatureVerificationResult verificationResult(signature.getType(), signatureFieldReference, qMove(qualifiedName));
                verificationResult.addNoHandlerError(signature.getSubfilter());
                result.emplace_back(qMove(verificationResult));
            }

            layouts.push_back(std::move(layout));
        }

        PDFSignatureRevisionClassifier::classifyLaterRevisions(sourceData, securityHandler, revisions, signatureFields, layouts, result);
    }

    return result;
}

void PDFSignatureRevisionClassifier::classifyLaterRevisions(const QByteArray& sourceData,
                                                 const PDFSecurityHandler* securityHandler,
                                                 const std::vector<PDFSignatureHandlerHelper::Revision>& revisions,
                                                 const std::vector<const PDFFormFieldSignature*>& signatureFields,
                                                 const std::vector<PDFSignatureLayout>& layouts,
                                                 std::vector<PDFSignatureVerificationResult>& results)
{
    Q_ASSERT(signatureFields.size() == layouts.size() && layouts.size() == results.size());

    // The revisions to classify: from the first signed one to the end of the file. Bytes behind the
    // last revision, which are not white space, are compared as a revision of their own.
    std::vector<PDFInteger> ends;
    for (const PDFSignatureHandlerHelper::Revision& revision : revisions)
    {
        ends.push_back(revision.end);
    }
    const PDFInteger lastEnd = ends.empty() ? 0 : ends.back();
    if (std::any_of(sourceData.cbegin() + lastEnd, sourceData.cend(), [](char value) { return !PDFLexicalAnalyzer::isWhitespace(value); }))
    {
        ends.push_back(sourceData.size());
    }

    int firstRevision = int(ends.size());
    PDFSignatureRevisionClassifier::SignedFields signedFields;
    for (size_t i = 0; i < layouts.size(); ++i)
    {
        if (!layouts[i].isValid)
        {
            continue;
        }

        firstRevision = qMin(firstRevision, layouts[i].revision);
        if (results[i].isSignatureValid())
        {
            PDFSignatureRevisionClassifier::SignedField& signedField = signedFields[signatureFields[i]->getSelfReference()];
            signedField.revision = layouts[i].revision;
            signedField.type = results[i].getType();
        }
    }

    const int lastRevision = int(ends.size()) - 1;
    if (firstRevision >= lastRevision)
    {
        // No signature has a later revision
        return;
    }

    // Each revision is read once and compared with the previous one, at most MAXIMUM_CLASSIFIED_REVISIONS of
    // them (each one is a whole reading of the document); an earlier revision stays a change
    std::vector<PDFSignatureRevisionClassifier::Classification> classifications(ends.size());
    const int firstClassified = qMax(firstRevision, lastRevision - PDFSignatureHandler::MAXIMUM_CLASSIFIED_REVISIONS);
    std::unique_ptr<PDFDocument> previousDocument = PDFSignatureHandlerHelper::readRevision(sourceData, ends[firstClassified], securityHandler);
    for (int revision = firstClassified + 1; revision <= lastRevision; ++revision)
    {
        std::unique_ptr<PDFDocument> currentDocument = PDFSignatureHandlerHelper::readRevision(sourceData, ends[revision], securityHandler);
        if (previousDocument && currentDocument)
        {
            try
            {
                PDFSignatureRevisionClassifier classifier(*previousDocument, *currentDocument, revision, signedFields);
                classifications[revision] = classifier.classify();
            }
            catch (const PDFException&)
            {
                classifications[revision] = PDFSignatureRevisionClassifier::Classification();
            }
        }
        previousDocument = std::move(currentDocument);
    }

    using Kind = PDFSignatureRevisionClassifier::Kind;
    for (size_t i = 0; i < layouts.size(); ++i)
    {
        const PDFSignatureLayout& layout = layouts[i];
        PDFSignatureVerificationResult& signatureResult = results[i];
        if (!layout.isValid || layout.revision >= lastRevision)
        {
            continue;
        }

        // The most significant kind of the later revisions and their informations
        Kind kind = Kind::Unchanged;
        QStringList infos;
        for (int revision = layout.revision + 1; revision <= lastRevision; ++revision)
        {
            const PDFSignatureRevisionClassifier::Classification& classification = revision > firstClassified ? classifications[revision] : PDFSignatureRevisionClassifier::Classification();
            kind = qMax(kind, classification.kind);
            infos << classification.infos;
        }

        // A certification (DocMDP, ISO 32000-2, 12.8.2.2.1, Table 257): P = 1 allows only validation data
        // and document timestamps, P = 2 also the filling in of forms and signing; P = 3 is not evaluated
        // (annotations, SECURITY_TODO), its later revisions are classified as of an approval signature
        const PDFInteger permission = signatureResult.getCertificationPermission();
        const Kind allowedKind = permission == 1 ? Kind::Timestamp : (permission == 2 ? Kind::FormFilling : Kind::Signatures);
        if ((permission == 1 || permission == 2) && kind > allowedKind)
        {
            signatureResult.addChangesNotAllowedError(permission);
        }

        if (kind > allowedKind)
        {
            signatureResult.setLaterRevisions(PDFSignatureVerificationResult::LaterRevisions::Changed);
            signatureResult.addSignatureNotCoveredBytesWarning(sourceData.size() - layout.end);
            continue;
        }

        signatureResult.setLaterRevisions(PDFSignatureVerificationResult::LaterRevisions::Allowed);
        switch (kind)
        {
            case Kind::Unchanged:
                signatureResult.addInfo(PDFTranslationContext::tr("The file was saved again after it was signed, nothing shown was changed."));
                break;

            case Kind::DocumentSecurityStore:
                signatureResult.addInfo(PDFTranslationContext::tr("Validation data were added after the document was signed."));
                break;

            case Kind::Timestamp:
            case Kind::Signatures:
                signatureResult.addInfo(PDFTranslationContext::tr("Later signatures or timestamps were added after the document was signed."));
                break;

            case Kind::FormFilling:
                signatureResult.addInfo(PDFTranslationContext::tr("Form fields were filled in after the document was certified, the certification allows it."));
                break;

            case Kind::Changed:
                Q_ASSERT(false);
                break;
        }

        for (const QString& info : infos)
        {
            signatureResult.addInfo(info);
        }
    }
}

void PDFSignatureVerificationResult::addNoHandlerError(const QByteArray& format)
{
    m_flags.setFlag(Error_NoHandler);
    m_errors << PDFTranslationContext::tr("No signature handler for signature format '%1'.").arg(QString::fromLatin1(format));
}

void PDFSignatureVerificationResult::addInvalidCertificateError()
{
    m_flags.setFlag(Error_Certificate_Invalid);
    m_errors << PDFTranslationContext::tr("Certificate format is invalid.");
}

void PDFSignatureVerificationResult::addNoSignaturesError()
{
    m_flags.setFlag(Error_Certificate_NoSignatures);
    m_errors << PDFTranslationContext::tr("No signatures in certificate data.");
}

void PDFSignatureVerificationResult::addCertificateMissingError()
{
    m_flags.setFlag(Error_Certificate_Missing);
    m_errors << PDFTranslationContext::tr("Certificate is missing.");
}

void PDFSignatureVerificationResult::addCertificateGenericError()
{
    m_flags.setFlag(Error_Certificate_Generic);
    m_errors << PDFTranslationContext::tr("Generic error occured during certificate validation.");
}

void PDFSignatureVerificationResult::addCertificateExpiredError()
{
    m_flags.setFlag(Error_Certificate_Expired);
    m_errors << PDFTranslationContext::tr("Certificate has expired.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedError()
{
    m_flags.setFlag(Error_Certificate_SelfSigned);
    m_errors << PDFTranslationContext::tr("Certificate is self-signed.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedInChainError()
{
    m_flags.setFlag(Error_Certificate_SelfSignedChain);
    m_errors << PDFTranslationContext::tr("Self-signed certificate in chain.");
}

void PDFSignatureVerificationResult::addCertificateTrustedNotFoundError()
{
    m_flags.setFlag(Error_Certificate_TrustedNotFound);
    m_errors << PDFTranslationContext::tr("Trusted certificate not found.");
}

void PDFSignatureVerificationResult::addCertificateRevokedError()
{
    m_flags.setFlag(Error_Certificate_Revoked);
    m_errors << PDFTranslationContext::tr("Certificate has been revoked.");
}

void PDFSignatureVerificationResult::addCertificateOtherError(int error)
{
    m_flags.setFlag(Error_Certificate_Other);
    m_errors << PDFTranslationContext::tr("Certificate validation failed with code %1.").arg(error);
}

void PDFSignatureVerificationResult::addInvalidSignatureError()
{
    m_flags.setFlag(Error_Signature_Invalid);
    m_errors << PDFTranslationContext::tr("Signature is invalid.");
}

void PDFSignatureVerificationResult::addSignatureNoSignaturesFoundError()
{
    m_flags.setFlag(Error_Signature_NoSignaturesFound);
    m_errors << PDFTranslationContext::tr("No signatures found in certificate.");
}

void PDFSignatureVerificationResult::addSignatureCertificateMissingError()
{
    m_flags.setFlag(Error_Signature_SourceCertificateMissing);
    m_errors << PDFTranslationContext::tr("Signature certificate is missing.");
}

void PDFSignatureVerificationResult::addSignatureDigestFailureError()
{
    m_flags.setFlag(Error_Signature_DigestFailure);
    m_errors << PDFTranslationContext::tr("Signed data has different hash function digest.");
}

void PDFSignatureVerificationResult::addSignatureDataOtherError()
{
    m_flags.setFlag(Error_Signature_DataOther);
    m_errors << PDFTranslationContext::tr("Signed data are invalid.");
}

void PDFSignatureVerificationResult::addSignatureDataCoveredBySignatureMissingError()
{
    m_flags.setFlag(Error_Signature_DataCoveredBySignatureMissing);
    m_errors << PDFTranslationContext::tr("Data covered by signature are not present.");
}

void PDFSignatureVerificationResult::addSignatureNotCoveredBytesWarning(PDFInteger count)
{
    if (!m_flags.testFlag(Warning_Signature_NotCoveredBytes))
    {
        m_flags.setFlag(Warning_Signature_NotCoveredBytes);
        m_warnings << PDFTranslationContext::tr("The document was changed after it was signed, %1 bytes are not covered by the signature.").arg(count);
    }
}

void PDFSignatureVerificationResult::addByteRangeError(const QString& reason)
{
    m_flags.setFlag(Error_Signature_ByteRange);
    m_errors << reason;
}

void PDFSignatureVerificationResult::addChangesNotAllowedError(PDFInteger permission)
{
    if (m_flags.testFlag(Error_Signature_ChangesNotAllowed))
    {
        return;
    }

    m_flags.setFlag(Error_Signature_ChangesNotAllowed);
    m_flags.setFlag(Signature_OK, false);
    m_flags.setFlag(OK, false);
    if (permission == 1)
    {
        m_errors << PDFTranslationContext::tr("The document was changed after it was certified, the certification allows no changes.");
    }
    else
    {
        m_errors << PDFTranslationContext::tr("The document was changed after it was certified, the certification allows only filling in forms and signing.");
    }
}

void PDFSignatureVerificationResult::addCertificatePurposeError()
{
    m_flags.setFlag(Error_Certificate_Other);
    m_errors << PDFTranslationContext::tr("The certificate of the signer may not be used for signing documents.");
}

void PDFSignatureVerificationResult::addWeakAlgorithmWarning(const QString& algorithm)
{
    if (!m_flags.testFlag(Warning_Signature_WeakAlgorithm))
    {
        m_flags.setFlag(Warning_Signature_WeakAlgorithm);
        m_warnings << PDFTranslationContext::tr("Weak algorithm (%1) protects the signature.").arg(algorithm);
    }
}

void PDFSignatureVerificationResult::addNotVerifiedError(const QString& reason)
{
    // A limit of the verification (plan 7.2): the signature is not shown as a valid one
    m_flags.setFlag(Error_Generic);
    m_flags.setFlag(Error_Signature_ByteRange);
    m_errors << reason;
}

void PDFSignatureVerificationResult::addInfo(const QString& info)
{
    if (!m_infos.contains(info))
    {
        m_infos << info;
    }
}

void PDFSignatureVerificationResult::addSignatureTimestampNotVerifiedWarning()
{
    if (!m_flags.testFlag(Warning_Signature_TimestampNotVerified))
    {
        m_flags.setFlag(Warning_Signature_TimestampNotVerified);
        m_warnings << PDFTranslationContext::tr("Timestamp of the signature could not be verified, the time of the signing is not attested by a timestamp authority.");
    }
}

void PDFSignatureVerificationResult::addSignatureFileRestoredWarning()
{
    if (!m_flags.testFlag(Warning_Signature_FileRestored))
    {
        m_flags.setFlag(Warning_Signature_FileRestored);
        m_warnings << PDFTranslationContext::tr("The file is damaged and its content was restored from its bytes, the shown content may not be what was signed.");
    }
}

void PDFSignatureVerificationResult::addCertificateCRLValidityTimeExpiredWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_CRLValidityTimeExpired))
    {
        m_flags.setFlag(Warning_Certificate_CRLValidityTimeExpired);
        m_warnings << PDFTranslationContext::tr("Certificate revocation list (CRL) not checked, validity time has expired.");
    }
}

void PDFSignatureVerificationResult::addCertificateQualifiedStatementNotVerifiedWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_QualifiedStatement))
    {
        m_flags.setFlag(Warning_Certificate_QualifiedStatement);
        m_warnings << PDFTranslationContext::tr("Qualified certificate statement not verified.");
    }
}

void PDFSignatureVerificationResult::addCertificateUnableToGetCRLWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_UnableToGetCRL))
    {
        m_flags.setFlag(Warning_Certificate_UnableToGetCRL);
        m_warnings << PDFTranslationContext::tr("Unable to get CRL.");
    }
}

void PDFSignatureVerificationResult::setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName)
{
    m_signatureFieldQualifiedName = signatureFieldQualifiedName;
}

void PDFSignatureVerificationResult::setSignatureFieldReference(PDFObjectReference signatureFieldReference)
{
    m_signatureFieldReference = signatureFieldReference;
}

void PDFSignatureVerificationResult::addHashAlgorithm(const QString& algorithm)
{
    if (!m_hashAlgorithms.contains(algorithm))
    {
        m_hashAlgorithms << algorithm;
    }
}

void PDFSignatureVerificationResult::validate()
{
    if (isCertificateValid() && isSignatureValid())
    {
        m_flags.setFlag(OK);
    }
}

QDateTime PDFSignatureVerificationResult::getSignatureDate() const
{
    return m_signatureDate;
}

void PDFSignatureVerificationResult::setSignatureDate(const QDateTime& signatureDate)
{
    m_signatureDate = signatureDate;
}

QDateTime PDFSignatureVerificationResult::getTimestampDate() const
{
    return m_timestampDate;
}

void PDFSignatureVerificationResult::setTimestampDate(const QDateTime& timestampDate)
{
    m_timestampDate = timestampDate;
}

QByteArray PDFSignatureVerificationResult::getSignatureHandler() const
{
    return m_signatureHandler;
}

void PDFSignatureVerificationResult::setSignatureHandler(const QByteArray& signatureFilter)
{
    m_signatureHandler = signatureFilter;
}

PDFSignatureVerificationResult::Status PDFSignatureVerificationResult::getCertificateStatus() const
{
    if (hasCertificateError())
    {
        return Status::Error;
    }

    if (hasCertificateWarning())
    {
        return Status::Warning;
    }

    return Status::OK;
}

PDFSignatureVerificationResult::Status PDFSignatureVerificationResult::getSignatureStatus() const
{
    if (hasSignatureError())
    {
        return Status::Error;
    }

    if (hasSignatureWarning())
    {
        return Status::Warning;
    }

    return Status::OK;
}

QString PDFSignatureVerificationResult::getStatusText(Status status)
{
    switch (status)
    {
        case Status::OK:
            return PDFTranslationContext::tr("OK");

        case Status::Warning:
            return PDFTranslationContext::tr("Warning");

        case Status::Error:
            return PDFTranslationContext::tr("Error");

        default:
            break;
    }

    return QString();
}

const PDFClosedIntervalSet& PDFSignatureVerificationResult::getBytesCoveredBySignature() const
{
    return m_bytesCoveredBySignature;
}

void PDFSignatureVerificationResult::setBytesCoveredBySignature(const PDFClosedIntervalSet& bytesCoveredBySignature)
{
    m_bytesCoveredBySignature = bytesCoveredBySignature;
}

PDFSignature::Type PDFSignatureVerificationResult::getType() const
{
    return m_type;
}

void PDFSignatureVerificationResult::setType(const PDFSignature::Type& type)
{
    m_type = type;
}

void PDFPublicKeySignatureHandler::initializeResult(PDFSignatureVerificationResult& result) const
{
    PDFObjectReference signatureFieldReference = m_signatureField->getSelfReference();
    QString signatureFieldQualifiedName = m_signatureField->getName(PDFFormField::NameType::FullyQualified);
    const PDFSignature& signature = m_signatureField->getSignature();
    result.setType(signature.getType());
    result.setSignatureFieldReference(signatureFieldReference);
    result.setSignatureFieldQualifiedName(signatureFieldQualifiedName);
    result.setSignatureHandler(signature.getSubfilter());
    result.setCertificationPermission(signature.getDocMDPPermission());

    // The signature covers the file up to the end of its revision, its gap is its own /Contents (checked layout)
    PDFClosedIntervalSet bytesCoveredBySignature;
    bytesCoveredBySignature.addInterval(0, m_layout.end - 1);
    result.setBytesCoveredBySignature(bytesCoveredBySignature);
}

QByteArray PDFPublicKeySignatureHandler::getSignedData() const
{
    // The layout is checked: 0 < a < b <= b + c <= size of the data (B111, B124)
    QByteArray data;
    data.reserve(m_layout.gapStart + (m_layout.end - m_layout.gapEnd));
    data.append(m_sourceData.constData(), m_layout.gapStart);
    data.append(m_sourceData.constData() + m_layout.gapEnd, m_layout.end - m_layout.gapEnd);
    return data;
}

STACK_OF(X509)* PDFPublicKeySignatureHandler::createDocumentSecurityStoreCertificates() const
{
    STACK_OF(X509)* certificates = sk_X509_new_null();
    if (m_parameters.dss)
    {
        PDFInteger size = 0;
        for (const QByteArray& certificateData : m_parameters.dss->getMasterItem()->Cert)
        {
            size += certificateData.size();
            if (sk_X509_num(certificates) >= MAXIMUM_CERTIFICATES || size > MAXIMUM_CERTIFICATES_SIZE)
            {
                break;
            }

            const unsigned char* certificateDataBuffer = convertByteArrayToUcharPtr(certificateData);
            if (X509* certificate = d2i_X509(nullptr, &certificateDataBuffer, long(certificateData.size())))
            {
                sk_X509_push(certificates, certificate);
            }
        }
    }
    return certificates;
}

bool PDFPublicKeySignatureHandler::isWeakDigestAlgorithm(int nid)
{
    switch (nid)
    {
        case NID_md2:
        case NID_md4:
        case NID_md5:
        case NID_mdc2:
        case NID_sha1:
            return true;

        default:
            return false;
    }
}

void PDFPublicKeySignatureHandler::addDigestAlgorithm(PDFSignatureVerificationResult& result, const ASN1_OBJECT* algorithm)
{
    if (!algorithm)
    {
        return;
    }

    std::array<char, 64> buffer = { };
    OBJ_obj2txt(buffer.data(), int(buffer.size() - 1), algorithm, 0);
    result.addHashAlgorithm(QString::fromLatin1(buffer.data()));

    const int nid = OBJ_obj2nid(algorithm);
    if (isWeakDigestAlgorithm(nid))
    {
        result.addWeakAlgorithmWarning(nid == NID_sha1 ? QStringLiteral("SHA-1") : QString::fromLatin1(OBJ_nid2sn(nid)));
    }
}

bool PDFPublicKeySignatureHandler::isDocumentSigningCertificate(X509* certificate)
{
    // Key usage (RFC 5280, 4.2.1.3), all usages when the extension is missing
    if ((X509_get_key_usage(certificate) & (KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION)) == 0)
    {
        return false;
    }

    // Extended key usage (RFC 5280, 4.2.1.12): a missing extension allows all purposes, -1 = missing
    int critical = 0;
    EXTENDED_KEY_USAGE* usages = static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(certificate, NID_ext_key_usage, &critical, nullptr));
    if (!usages)
    {
        return critical == -1;
    }

    bool isAllowed = false;
    for (int i = 0, count = sk_ASN1_OBJECT_num(usages); i < count && !isAllowed; ++i)
    {
        const ASN1_OBJECT* usage = sk_ASN1_OBJECT_value(usages, i);
        const int nid = OBJ_obj2nid(usage);

        std::array<char, 80> buffer = { };
        OBJ_obj2txt(buffer.data(), int(buffer.size() - 1), usage, 1);
        const QByteArray identifier(buffer.data());

        isAllowed = nid == NID_email_protect || nid == NID_anyExtendedKeyUsage ||
                    identifier == "1.3.6.1.5.5.7.3.36" ||       // id-kp-documentSigning (RFC 9336)
                    identifier == "1.3.6.1.4.1.311.10.3.12" ||  // Microsoft Document Signing (owner 2026-11-15)
                    identifier == "1.2.840.113583.1.1.5";       // Adobe Authentic Documents Trust
    }
    EXTENDED_KEY_USAGE_free(usages);
    return isAllowed;
}

void PDFPublicKeySignatureHandler::verifySignerCertificate(PDFSignatureVerificationResult& result, X509* signer, STACK_OF(X509)* untrusted, bool isTimestamp) const
{
    PDFCertificateInfos certificateInfos;
    certificateInfos.push_back(getCertificateInfo(signer));

    if (!m_parameters.verifyCertificates)
    {
        // B132: the chain and the trust are not verified, the signer is identified
        result.setCertificateInfos(std::move(certificateInfos), true);
        return;
    }

    openssl_ptr<X509_STORE> store(X509_STORE_new(), X509_STORE_free);
    openssl_ptr<X509_STORE_CTX> context(X509_STORE_CTX_new(), X509_STORE_CTX_free);
    addTrustedCertificates(store.get());

    // B117: the revocation is checked, when the document has the revocation lists (a missing list of a
    // certificate is then a warning); without them a missing list is not a reason of an invalid signature
    bool hasRevocationLists = false;
    if (m_parameters.dss)
    {
        for (const QByteArray& crlData : m_parameters.dss->getMasterItem()->CRL)
        {
            const unsigned char* crlDataBuffer = convertByteArrayToUcharPtr(crlData);
            if (X509_CRL* crl = d2i_X509_CRL(nullptr, &crlDataBuffer, long(crlData.size())))
            {
                hasRevocationLists = X509_STORE_add_crl(store.get(), crl) == 1 || hasRevocationLists;
                X509_CRL_free(crl);
            }
        }
    }

    if (!X509_STORE_CTX_init(context.get(), store.get(), signer, untrusted) ||
        (isTimestamp && !X509_STORE_CTX_set_purpose(context.get(), X509_PURPOSE_TIMESTAMP_SIGN)))
    {
        result.addCertificateGenericError();
        result.setCertificateInfos(std::move(certificateInfos), true);
        return;
    }

    unsigned long flags = X509_V_FLAG_TRUSTED_FIRST;
    if (hasRevocationLists)
    {
        flags |= X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL | X509_V_FLAG_EXTENDED_CRL_SUPPORT;
    }
    if (m_parameters.ignoreExpirationDate)
    {
        flags |= X509_V_FLAG_NO_CHECK_TIME;
    }
    X509_STORE_CTX_set_flags(context.get(), flags);
    X509_STORE_CTX_set_depth(context.get(), MAXIMUM_CHAIN_DEPTH);
    X509_STORE_CTX_set_app_data(context.get(), &result);
    X509_STORE_CTX_set_verify_cb(context.get(), &PDFPublicKeySignatureHandler::verifyCallback);

    if (X509_verify_cert(context.get()) <= 0)
    {
        const int error = X509_STORE_CTX_get_error(context.get());
        switch (error)
        {
            case X509_V_OK:
                // Strange, this should not occur... when X509_verify_cert fails
                result.addCertificateGenericError();
                break;

            case X509_V_ERR_CERT_HAS_EXPIRED:
                result.addCertificateExpiredError();
                break;

            case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                result.addCertificateSelfSignedError();
                break;

            case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                result.addCertificateSelfSignedInChainError();
                break;

            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                result.addCertificateTrustedNotFoundError();
                break;

            case X509_V_ERR_CERT_REVOKED:
                result.addCertificateRevokedError();
                break;

            default:
                result.addCertificateOtherError(error);
                break;
        }
    }

    // The chain built from the signer, also the part of a failed verification (R6-2): never another
    // certificate of the bag of the signature
    if (STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(context.get()))
    {
        const int count = sk_X509_num(chain);
        if (count > 0 && X509_cmp(sk_X509_value(chain, 0), signer) == 0)
        {
            for (int i = 1; i < count; ++i)
            {
                certificateInfos.push_back(getCertificateInfo(sk_X509_value(chain, i)));
            }
        }
    }

    // B116: the purpose of signing documents, by the rules of the PDF signatures (SMIME_SIGN refuses documentSigning)
    if (!isTimestamp && !isDocumentSigningCertificate(signer))
    {
        result.addCertificatePurposeError();
    }

    result.setCertificateInfos(std::move(certificateInfos), true);
    if (!result.hasCertificateError())
    {
        result.setFlag(PDFSignatureVerificationResult::Certificate_OK, true);
    }
}

PDFSignatureVerificationResult PDFCMSSignatureHandler::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);

    PDFOpenSSLGlobalLock lock;
    OpenSSL_add_all_algorithms();
    ERR_clear_error();

    // The checked layout: exactly one DER object (R6-22), SignedData of CMS (RFC 5652, 5.1)
    const QByteArray& der = m_layout.der;
    const unsigned char* begin = convertByteArrayToUcharPtr(der);
    const unsigned char* data = begin;
    openssl_ptr<CMS_ContentInfo> cms(d2i_CMS_ContentInfo(nullptr, &data, long(der.size())), CMS_ContentInfo_free);
    if (!cms || data != begin + der.size() || OBJ_obj2nid(CMS_get0_type(cms.get())) != NID_pkcs7_signed)
    {
        result.addInvalidCertificateError();
        result.addInvalidSignatureError();
        result.validate();
        return result;
    }

    // Only a single signer (ISO 32000-2, 12.8.3.3.1)
    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms.get());
    const int signerInfoCount = signerInfos ? sk_CMS_SignerInfo_num(signerInfos) : 0;
    if (signerInfoCount != 1)
    {
        if (signerInfoCount <= 0)
        {
            result.addNoSignaturesError();
            result.addSignatureNoSignaturesFoundError();
        }
        else
        {
            result.addCertificateMissingError();
            result.addInvalidSignatureError();
        }
        result.validate();
        return result;
    }
    CMS_SignerInfo* signerInfo = sk_CMS_SignerInfo_value(signerInfos, 0);

    X509_ALGOR* digestAlgorithm = nullptr;
    CMS_SignerInfo_get0_algs(signerInfo, nullptr, nullptr, &digestAlgorithm, nullptr);
    if (digestAlgorithm)
    {
        const ASN1_OBJECT* digestAlgorithmObject = nullptr;
        X509_ALGOR_get0(&digestAlgorithmObject, nullptr, nullptr, digestAlgorithm);
        addDigestAlgorithm(result, digestAlgorithmObject);
    }
    addSignatureDate(result, signerInfo);

    // The certificates: the bag of the signature first, then the document security store (limits of 7.2)
    openssl_ptr<STACK_OF(X509)> bag(CMS_get1_certs(cms.get()), &PDFSignatureHandlerHelper::freeCertificates);
    if (bag && sk_X509_num(bag.get()) > MAXIMUM_CERTIFICATES)
    {
        result.addByteRangeError(PDFTranslationContext::tr("The signature has more than %1 certificates.").arg(MAXIMUM_CERTIFICATES));
        result.validate();
        return result;
    }

    openssl_ptr<STACK_OF(X509)> certificates(sk_X509_new_null(), &PDFSignatureHandlerHelper::freeCertificates);
    for (int i = 0, count = bag ? sk_X509_num(bag.get()) : 0; i < count; ++i)
    {
        X509* certificate = sk_X509_value(bag.get(), i);
        X509_up_ref(certificate);
        sk_X509_push(certificates.get(), certificate);
    }
    openssl_ptr<STACK_OF(X509)> storeCertificates(createDocumentSecurityStoreCertificates(), &PDFSignatureHandlerHelper::freeCertificates);
    while (X509* certificate = sk_X509_shift(storeCertificates.get()))
    {
        sk_X509_push(certificates.get(), certificate);
    }

    // The signer (R6-2): a certificate of the SignerInfo (issuer and serial number, or the subject key
    // identifier, B115), which verifies the signature of the signed attributes; the first certificate of
    // the bag is not the signer, an attacker can put any certificate there
    const bool hasSignedAttributes = CMS_signed_get_attr_count(signerInfo) > 0;
    X509* signer = nullptr;
    bool isSignerVerified = false;
    for (int i = 0, count = sk_X509_num(certificates.get()); i < count && !isSignerVerified; ++i)
    {
        X509* candidate = sk_X509_value(certificates.get(), i);
        if (CMS_SignerInfo_cert_cmp(signerInfo, candidate) != 0)
        {
            continue;
        }

        if (!signer)
        {
            signer = candidate;
        }

        if (hasSignedAttributes)
        {
            CMS_SignerInfo_set1_signer_cert(signerInfo, candidate);
            if (CMS_SignerInfo_verify(signerInfo) > 0)
            {
                signer = candidate;
                isSignerVerified = true;
            }
            ERR_clear_error();
        }
    }

    if (!signer)
    {
        result.addCertificateMissingError();
        result.addSignatureCertificateMissingError();
        result.validate();
        return result;
    }

    CMS_SignerInfo_set1_signer_cert(signerInfo, signer);
    if (hasSignedAttributes && !isSignerVerified)
    {
        // The value of the signature is not of the certificate of the SignerInfo
        result.addInvalidSignatureError();
    }

    if (m_content == Content::TimestampToken)
    {
        verifyTimestampToken(result);
    }
    else
    {
        const bool isContentVerified = verifyContent(result, cms.get(), signerInfo, hasSignedAttributes);
        isSignerVerified = hasSignedAttributes ? isSignerVerified : isContentVerified;
        verifySignatureTimestampAttribute(result, signerInfo);
    }

    if (isSignerVerified)
    {
        verifySignerCertificate(result, signer, certificates.get(), m_content == Content::TimestampToken);
    }
    else
    {
        // No certificate is the one of the signer, no name is shown (R6-2)
        result.addCertificateMissingError();
    }

    if (!result.hasSignatureError())
    {
        result.setFlag(PDFSignatureVerificationResult::Signature_OK, true);
    }

    result.validate();
    return result;
}

bool PDFCMSSignatureHandler::verifyContent(PDFSignatureVerificationResult& result, CMS_ContentInfo* cms, CMS_SignerInfo* signerInfo, bool hasSignedAttributes) const
{
    const QByteArray signedData = getSignedData();
    if (signedData.size() > std::numeric_limits<int>::max())
    {
        result.addSignatureDataOtherError();
        return false;
    }

    ASN1_OCTET_STRING** content = CMS_get0_content(cms);
    const bool hasContent = content && *content;
    openssl_ptr<BIO> dataBuffer(nullptr, BIO_free_all);

    // The digest of the signed data by the algorithm of the signer
    X509_ALGOR* digestAlgorithm = nullptr;
    CMS_SignerInfo_get0_algs(signerInfo, nullptr, nullptr, &digestAlgorithm, nullptr);
    const ASN1_OBJECT* digestAlgorithmObject = nullptr;
    if (digestAlgorithm)
    {
        X509_ALGOR_get0(&digestAlgorithmObject, nullptr, nullptr, digestAlgorithm);
    }
    const EVP_MD* digestMethod = digestAlgorithmObject ? EVP_get_digestbyobj(digestAlgorithmObject) : nullptr;

    switch (m_content)
    {
        case Content::Detached:
        {
            // No data are encapsulated, the bytes of the byte range are the content (ISO 32000-2, 12.8.3.3.1)
            if (hasContent)
            {
                result.addSignatureDataOtherError();
                return false;
            }

            if (hasSignedAttributes)
            {
                QByteArray digest;
                if (digestMethod)
                {
                    unsigned int digestSize = 0;
                    digest.resize(EVP_MD_size(digestMethod));
                    EVP_Digest(signedData.constData(), size_t(signedData.size()), convertByteArrayToUcharPtr(digest), &digestSize, digestMethod, nullptr);
                }

                const ASN1_OCTET_STRING* messageDigest = static_cast<const ASN1_OCTET_STRING*>(CMS_signed_get0_data_by_OBJ(signerInfo, OBJ_nid2obj(NID_pkcs9_messageDigest), -3, V_ASN1_OCTET_STRING));
                const QByteArray signedDigest = messageDigest ? QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(messageDigest)), ASN1_STRING_length(messageDigest)) : QByteArray();
                if (digest.isEmpty() || digest != signedDigest)
                {
                    result.addSignatureDigestFailureError();
                }
            }

            dataBuffer.reset(BIO_new_mem_buf(signedData.constData(), int(signedData.size())));
            break;
        }

        case Content::SHA1Digest:
        {
            // The SHA-1 digest of the bytes of the byte range is the encapsulated content (ISO 32000-2, 12.8.3.3.1)
            QByteArray digest(SHA_DIGEST_LENGTH, 0);
            SHA1(convertByteArrayToUcharPtr(signedData), size_t(signedData.size()), convertByteArrayToUcharPtr(digest));
            const QByteArray encapsulatedDigest = hasContent ? QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(*content)), ASN1_STRING_length(*content)) : QByteArray();
            if (digest != encapsulatedDigest)
            {
                result.addSignatureDigestFailureError();
            }
            result.addWeakAlgorithmWarning(QStringLiteral("SHA-1"));
            break;
        }

        case Content::TimestampToken:
            Q_ASSERT(false);
            return false;
    }

    // The content type of the signed attributes is the type of the content (RFC 5652, 11.1)
    if (hasSignedAttributes)
    {
        const ASN1_OBJECT* contentType = static_cast<const ASN1_OBJECT*>(CMS_signed_get0_data_by_OBJ(signerInfo, OBJ_nid2obj(NID_pkcs9_contentType), -3, V_ASN1_OBJECT));
        if (!contentType || OBJ_cmp(contentType, CMS_get0_eContentType(cms)) != 0)
        {
            result.addInvalidSignatureError();
        }
    }

    // The whole verification of OpenSSL: the signature of the signed attributes or of the content, the
    // digest of the content, the attribute signing-certificate(-v2) of ESS, when it is present
    ERR_clear_error();
    const bool isVerified = CMS_verify(cms, nullptr, nullptr, dataBuffer.get(), nullptr, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY) > 0;
    ERR_clear_error();
    if (!isVerified && !result.hasSignatureError())
    {
        result.addSignatureDataOtherError();
    }
    return isVerified;
}

void PDFCMSSignatureHandler::addSignatureDate(PDFSignatureVerificationResult& result, CMS_SignerInfo* signerInfo) const
{
    const int index = CMS_signed_get_attr_by_NID(signerInfo, NID_pkcs9_signingTime, -1);
    X509_ATTRIBUTE* attribute = index >= 0 ? CMS_signed_get_attr(signerInfo, index) : nullptr;
    ASN1_TYPE* value = attribute ? X509_ATTRIBUTE_get0_type(attribute, 0) : nullptr;
    QDateTime signingTime;
    if (value)
    {
        switch (value->type)
        {
            case V_ASN1_UTCTIME:
                signingTime = getDateTimeFromASN(value->value.utctime);
                break;

            case V_ASN1_GENERALIZEDTIME:
                signingTime = getDateTimeFromASN(value->value.generalizedtime);
                break;

            default:
                break;
        }
    }

    if (signingTime.isValid())
    {
        result.setSignatureDate(signingTime);
        result.setSignatureDateSource(PDFSignatureVerificationResult::SignatureDateSource::SigningTimeAttribute);
        return;
    }

    // RS6FIGMA L-M1: a PAdES signature has its time only in /M (Table 255) - the time of the signing
    // claimed by the signer; a document timestamp has the time of its token (setTimestampDate)
    const QDateTime claimedTime = m_content != Content::TimestampToken ? m_signatureField->getSignature().getSigningDateTime() : QDateTime();
    if (claimedTime.isValid())
    {
        result.setSignatureDate(claimedTime);
        result.setSignatureDateSource(PDFSignatureVerificationResult::SignatureDateSource::SignatureDictionary);
    }
}

STACK_OF(X509)* PDFCMSSignatureHandler::getCertificates(PKCS7* pkcs7)
{
    if (!pkcs7)
    {
        return nullptr;
    }

    if (PKCS7_type_is_signed(pkcs7))
    {
        return pkcs7->d.sign->cert;
    }

    if (PKCS7_type_is_signedAndEnveloped(pkcs7))
    {
        return pkcs7->d.signed_and_enveloped->cert;
    }

    return nullptr;
}

void PDFCMSSignatureHandler::verifyTimestampToken(PDFSignatureVerificationResult& result) const
{
    const QByteArray& content = m_layout.der;
    const QByteArray signedData = getSignedData();
    if (signedData.size() > std::numeric_limits<int>::max())
    {
        result.addSignatureDataOtherError();
        return;
    }

    const unsigned char* data = convertByteArrayToUcharPtr(content);
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, long(content.size())))
    {
        BIO* inputBuffer = BIO_new_mem_buf(signedData.constData(), int(signedData.size()));
        {
            X509_STORE* store = X509_STORE_new();

            // Above function can fail only if not enough memory. But in this
            // case, this library will crash anyway.
            Q_ASSERT(store);

            // The certificates of the token, then of the document security store
            STACK_OF(X509)* certificatesFromPkcs7 = getCertificates(pkcs7);
            STACK_OF(X509)* usedCertificates = createDocumentSecurityStoreCertificates();
            for (int i = sk_X509_num(certificatesFromPkcs7) - 1; i >= 0; --i)
            {
                X509* certificate = sk_X509_value(certificatesFromPkcs7, i);
                X509_up_ref(certificate);
                sk_X509_unshift(usedCertificates, certificate);
            }

            // Jakub Melka: the signature of the timestamp token and the trust in
            // the timestamp authority are two different things. The certificate
            // of the authority is verified by the function verifySignerCertificate
            // and its result is reported as a certificate error, so here we check
            // only, that the token is correctly signed and that it really covers
            // the data of the document - otherwise an untrusted or an expired
            // certificate of the authority would be reported as damaged signed
            // data. The certificate, which signed the token, therefore ends the
            // chain here and neither its issuers nor its validity are verified.
            X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_NO_CHECK_TIME);
            if (STACK_OF(X509)* signers = PKCS7_get0_signers(pkcs7, usedCertificates, 0))
            {
                if (sk_X509_num(signers) > 0)
                {
                    X509_STORE_add_cert(store, sk_X509_value(signers, 0));
                }

                sk_X509_free(signers);
            }

            // Initialization of verification context
            TS_VERIFY_CTX* ts_context = TS_VERIFY_CTX_new();
            TS_VERIFY_CTX_init(ts_context);
            TS_VERIFY_CTX_set_flags(ts_context, TS_VFY_ALL_DATA & ~TS_VFY_POLICY & ~TS_VFY_NONCE & ~TS_VFY_TSA_NAME);
            setTimestampVerifyContextData(ts_context, store, usedCertificates, inputBuffer);

            ERR_clear_error();
            const int verifyValue = TS_RESP_verify_token(ts_context, pkcs7);
            if (verifyValue > 0)
            {
                // The time of a token, which does not belong to the document,
                // says nothing about it, so it is read only from a verified one.
                if (TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(pkcs7))
                {
                    result.setTimestampDate(getDateTimeFromASN(TS_TST_INFO_get_time(info)));

                    // The digest of the document in the token (B114)
                    const X509_ALGOR* imprintAlgorithm = TS_MSG_IMPRINT_get_algo(TS_TST_INFO_get_msg_imprint(info));
                    const ASN1_OBJECT* imprintAlgorithmObject = nullptr;
                    if (imprintAlgorithm)
                    {
                        X509_ALGOR_get0(&imprintAlgorithmObject, nullptr, nullptr, imprintAlgorithm);
                    }
                    const int imprintAlgorithmNid = imprintAlgorithmObject ? OBJ_obj2nid(imprintAlgorithmObject) : NID_undef;
                    if (isWeakDigestAlgorithm(imprintAlgorithmNid))
                    {
                        result.addWeakAlgorithmWarning(imprintAlgorithmNid == NID_sha1 ? QStringLiteral("SHA-1") : QString::fromLatin1(OBJ_nid2sn(imprintAlgorithmNid)));
                    }

                    TS_TST_INFO_free(info);
                }
            }
            else
            {
                const int reason = ERR_GET_REASON(ERR_get_error());
                switch (reason)
                {
                    case TS_R_MESSAGE_IMPRINT_MISMATCH:
                        result.addSignatureDigestFailureError();
                        break;

                    default:
                        result.addSignatureDataOtherError();
                        break;
                }
            }
            ERR_clear_error();

            // Finalization of verification context. Function TS_VERIFY_CTX_cleanup also
            // frees all data, such as context, store, etc.
            TS_VERIFY_CTX_cleanup(ts_context);
            TS_VERIFY_CTX_free(ts_context);
        }

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidSignatureError();
    }
}

int PDFPublicKeySignatureHandler::verifyCallback(int ok, X509_STORE_CTX* context)
{
    // The result of the verification, given to the context by verifySignerCertificate (no global state)
    PDFSignatureVerificationResult* result = static_cast<PDFSignatureVerificationResult*>(X509_STORE_CTX_get_app_data(context));
    const int errorCode = X509_STORE_CTX_get_error(context);

    switch (errorCode)
    {
        case X509_V_ERR_CRL_NOT_YET_VALID:
        case X509_V_ERR_CRL_HAS_EXPIRED:
        {
            // We will treat this as only warning
            result->addCertificateCRLValidityTimeExpiredWarning();
            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        case X509_V_ERR_UNABLE_TO_GET_CRL:
        {
            // We will treat this as only warning. It means that
            // CRL cannot be downloaded or other error occured.
            result->addCertificateUnableToGetCRLWarning();
            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
        {
            // We must handle all critical extensions manually
            X509* certificate = X509_STORE_CTX_get_current_cert(context);
            const STACK_OF(X509_EXTENSION)* extensions = X509_get0_extensions(certificate);
            for (int i = 0, extensionsCount = sk_X509_EXTENSION_num(extensions); i < extensionsCount; ++i)
            {
                X509_EXTENSION* extension = sk_X509_EXTENSION_value(extensions, i);

                // Skip non-critical extensions
                if (!X509_EXTENSION_get_critical(extension))
                {
                    continue;
                }

                const ASN1_OBJECT* object = X509_EXTENSION_get_object(extension);
                const int nid = OBJ_obj2nid(object);

                switch (nid)
                {
                    case NID_basic_constraints:
                    case NID_key_usage:
                        // These are handled by OpenSSL
                        continue;

                    case NID_qcStatements:
                    {
                        // We will treat this as only warning
                        result->addCertificateQualifiedStatementNotVerifiedWarning();
                        X509_STORE_CTX_set_error(context, X509_V_OK);
                        continue;
                    }

                    default:
                        return ok;
                }
            }

            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        default:
            break;
    }

    return ok;
}

PDFSignatureVerificationResult PDFSignatureHandler_adbe_pkcs7_rsa_sha1::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);

    PDFOpenSSLGlobalLock lock;
    OpenSSL_add_all_algorithms();
    ERR_clear_error();

    // The certificates of /Cert, the first one is the signer (ISO 32000-2, Table 255), limits of 7.2 (B123)
    const PDFSignature& signature = m_signatureField->getSignature();
    const std::vector<QByteArray>* certificateData = signature.getCertificates();
    if (!certificateData || certificateData->empty())
    {
        result.addCertificateMissingError();
        result.addSignatureCertificateMissingError();
        result.validate();
        return result;
    }

    PDFInteger certificatesSize = 0;
    for (const QByteArray& data : *certificateData)
    {
        certificatesSize += data.size();
    }
    if (certificateData->size() > size_t(MAXIMUM_CERTIFICATES) || certificatesSize > MAXIMUM_CERTIFICATES_SIZE)
    {
        result.addByteRangeError(PDFTranslationContext::tr("The signature has more than %1 certificates or more than %2 bytes of them.").arg(MAXIMUM_CERTIFICATES).arg(MAXIMUM_CERTIFICATES_SIZE));
        result.validate();
        return result;
    }

    openssl_ptr<STACK_OF(X509)> certificates(sk_X509_new_null(), &PDFSignatureHandlerHelper::freeCertificates);
    for (const QByteArray& data : *certificateData)
    {
        const unsigned char* buffer = convertByteArrayToUcharPtr(data);
        X509* certificate = d2i_X509(nullptr, &buffer, long(data.size()));
        if (!certificate && sk_X509_num(certificates.get()) == 0)
        {
            result.addInvalidCertificateError();
            result.addSignatureCertificateMissingError();
            result.validate();
            return result;
        }

        if (certificate)
        {
            sk_X509_push(certificates.get(), certificate);
        }
    }
    X509* signer = sk_X509_value(certificates.get(), 0);

    // The signature: PKCS#1 of the signer over the digest of the signed data, the exact DER octet string
    bool isSignerVerified = false;
    EVP_PKEY* evpKey = X509_get0_pubkey(signer);
    openssl_ptr<RSA> rsa(evpKey ? EVP_PKEY_get1_RSA(evpKey) : nullptr, RSA_free);
    const QByteArray& der = m_layout.der;
    const unsigned char* encryptedSign = convertByteArrayToUcharPtr(der);
    const unsigned char* encryptedSignBegin = encryptedSign;
    openssl_ptr<ASN1_OCTET_STRING> encryptedString(d2i_ASN1_OCTET_STRING(nullptr, &encryptedSign, long(der.size())), ASN1_OCTET_STRING_free);

    if (!rsa)
    {
        result.addSignatureCertificateMissingError();
    }
    else if (!encryptedString || encryptedSign != encryptedSignBegin + der.size())
    {
        result.addSignatureDataOtherError();
    }
    else
    {
        int algorithmNID = NID_undef;
        QByteArray digestBuffer;
        if (!getMessageDigest(getSignedData(), encryptedString.get(), rsa.get(), algorithmNID, digestBuffer))
        {
            result.addSignatureDataOtherError();
        }
        else
        {
            addDigestAlgorithm(result, OBJ_nid2obj(algorithmNID));

            const unsigned char* digest = convertByteArrayToUcharPtr(digestBuffer);
            const unsigned int digestLength = static_cast<unsigned int>(digestBuffer.length());
            if (RSA_verify(algorithmNID, digest, digestLength, encryptedString->data, encryptedString->length, rsa.get()) > 0)
            {
                isSignerVerified = true;
            }
            else
            {
                // We have failed, probably due to invalid signature
                const unsigned long errorCode = ERR_GET_REASON(ERR_get_error());
                switch (errorCode)
                {
                    case RSA_R_DIGEST_DOES_NOT_MATCH:
                        result.addSignatureDigestFailureError();
                        break;

                    default:
                        result.addSignatureDataOtherError();
                        break;
                }
            }
        }
    }
    ERR_clear_error();

    if (isSignerVerified)
    {
        openssl_ptr<STACK_OF(X509)> storeCertificates(createDocumentSecurityStoreCertificates(), &PDFSignatureHandlerHelper::freeCertificates);
        while (X509* certificate = sk_X509_shift(storeCertificates.get()))
        {
            sk_X509_push(certificates.get(), certificate);
        }
        verifySignerCertificate(result, signer, certificates.get(), false);
    }
    else
    {
        // No certificate is the one of the signer, no name is shown (R6-2)
        result.addCertificateMissingError();
    }

    if (!result.hasSignatureError())
    {
        result.setFlag(PDFSignatureVerificationResult::Signature_OK, true);
    }

    result.validate();
    return result;
}

bool PDFSignatureHandler_adbe_pkcs7_rsa_sha1::getMessageDigest(const QByteArray& message,
                                                               ASN1_OCTET_STRING* encryptedString,
                                                               RSA* rsa,
                                                               int& algorithmNID,
                                                               QByteArray& digest) const
{
    if (!getMessageDigestAlgorithm(encryptedString, rsa, algorithmNID))
    {
        return false;
    }

    if (const EVP_MD* md = EVP_get_digestbynid(algorithmNID))
    {
        unsigned int messageDigestSize = EVP_MD_size(md);
        digest.resize(messageDigestSize);

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        Q_ASSERT(context);

        EVP_DigestInit(context, md);
        EVP_DigestUpdate(context, message.constData(), message.size());
        EVP_DigestFinal(context, convertByteArrayToUcharPtr(digest), &messageDigestSize);

        EVP_MD_CTX_free(context);
        return true;
    }

    return false;
}

bool PDFSignatureHandler_adbe_pkcs7_rsa_sha1::getMessageDigestAlgorithm(ASN1_OCTET_STRING* encryptedString,
                                                                        RSA* rsa,
                                                                        int& algorithmNID) const
{
    algorithmNID = 0;

    int size = RSA_size(rsa);
    std::vector<unsigned char> decryptedBuffer(size, 0);
    const int signatureSize = RSA_public_decrypt(encryptedString->length, encryptedString->data, decryptedBuffer.data(), rsa, RSA_PKCS1_PADDING);

    if (signatureSize <= 0)
    {
        return false;
    }

    Q_ASSERT(static_cast<std::size_t>(signatureSize) < decryptedBuffer.size());

    const unsigned char* decryptedBufferPtr = decryptedBuffer.data();
    if (X509_SIG* x509_sig = d2i_X509_SIG(nullptr, &decryptedBufferPtr, signatureSize))
    {
        const X509_ALGOR* algorithm = nullptr;
        const ASN1_OBJECT* algorithmDescriptor = nullptr;

        X509_SIG_get0(x509_sig, &algorithm, nullptr);
        X509_ALGOR_get0(&algorithmDescriptor, nullptr, nullptr, algorithm);
        algorithmNID = OBJ_obj2nid(algorithmDescriptor);

        X509_SIG_free(x509_sig);
        return true;
    }

    return false;
}

PDFCertificateInfo PDFPublicKeySignatureHandler::getCertificateInfo(X509* certificate)
{
    return PDFCertificateInfo::getCertificateInfo(certificate);
}

QString PDFPublicKeySignatureHandler::getStringFromX509Name(X509_NAME* name, int nid)
{
    QString result;

    const int stringLocation = X509_NAME_get_index_by_NID(name, nid, -1);
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, stringLocation);
    return getStringFromASN1_STRING(X509_NAME_ENTRY_get_data(entry));
}

QString pdf::PDFPublicKeySignatureHandler::getStringFromASN1_STRING(ASN1_STRING* string)
{
    QString result;

    if (string)
    {
        // Jakub Melka: we must convert entry to UTF8 encoding using function ASN1_STRING_to_UTF8
        unsigned char* utf8Buffer = nullptr;
        int errorCodeOrLength = ASN1_STRING_to_UTF8(&utf8Buffer, string);
        if (errorCodeOrLength > 0)
        {
            result = QString::fromUtf8(reinterpret_cast<const char*>(utf8Buffer), errorCodeOrLength);
        }
        OPENSSL_free(utf8Buffer);
    }

    return result;
}

QDateTime PDFPublicKeySignatureHandler::getDateTimeFromASN(const ASN1_TIME* time)
{
    QDateTime result;

    if (time)
    {
        tm internalTime = { };
        if (ASN1_TIME_to_tm(time, &internalTime) > 0)
        {
#if defined(Q_OS_WIN)
            time_t localTime = _mkgmtime(&internalTime);
#elif defined(Q_OS_UNIX)
            time_t localTime = timegm(&internalTime);
#else
            static_assert(false, "Implement this for another OS!");
#endif
            result = QDateTime::fromSecsSinceEpoch(localTime, Qt::UTC);
        }
    }

    return result;
}

void PDFCMSSignatureHandler::verifySignatureTimestampAttribute(PDFSignatureVerificationResult& result, CMS_SignerInfo* signerInfo) const
{
    const int index = CMS_unsigned_get_attr_by_NID(signerInfo, NID_id_smime_aa_timeStampToken, -1);
    X509_ATTRIBUTE* timestampAttribute = index >= 0 ? CMS_unsigned_get_attr(signerInfo, index) : nullptr;
    ASN1_TYPE* attribute = timestampAttribute ? X509_ATTRIBUTE_get0_type(timestampAttribute, 0) : nullptr;
    ASN1_OCTET_STRING* signatureValue = CMS_SignerInfo_get0_signature(signerInfo);

    if (index < 0)
    {
        // The signature is not timestamped
        return;
    }

    // Jakub Melka: the timestamp of the signature is an unsigned attribute, so it
    // is not covered by the signature of the signer and anyone can replace it. The
    // time it carries can therefore be used only when the token itself is verified -
    // its signature, the data it timestamps, which must be the signature value of
    // the signer, and the certificate of the timestamp authority, which must be
    // trusted (RFC 3161, chapter 2.4.2). An unverified time is never presented as
    // the time of the signing.
    bool isTimestampVerified = false;

    if (attribute && attribute->type == V_ASN1_SEQUENCE && signatureValue)
    {
        const unsigned char* tokenData = ASN1_STRING_get0_data(attribute->value.sequence);
        if (PKCS7* token = d2i_PKCS7(nullptr, &tokenData, ASN1_STRING_length(attribute->value.sequence)))
        {
            X509_STORE* store = X509_STORE_new();
            Q_ASSERT(store);
            addTrustedCertificates(store);

            // The certificates of the token, then of the document security store
            STACK_OF(X509)* certificatesFromToken = getCertificates(token);
            STACK_OF(X509)* usedCertificates = createDocumentSecurityStoreCertificates();
            for (int i = sk_X509_num(certificatesFromToken) - 1; i >= 0; --i)
            {
                X509* certificate = sk_X509_value(certificatesFromToken, i);
                X509_up_ref(certificate);
                sk_X509_unshift(usedCertificates, certificate);
            }

            // The token timestamps the signature value of the signer
            BIO* timestampedData = BIO_new_mem_buf(ASN1_STRING_get0_data(signatureValue), ASN1_STRING_length(signatureValue));

            if (m_parameters.ignoreExpirationDate)
            {
                X509_STORE_set_flags(store, X509_V_FLAG_NO_CHECK_TIME);
            }

            TS_VERIFY_CTX* ts_context = TS_VERIFY_CTX_new();
            TS_VERIFY_CTX_init(ts_context);
            TS_VERIFY_CTX_set_flags(ts_context, TS_VFY_ALL_DATA & ~TS_VFY_POLICY & ~TS_VFY_NONCE & ~TS_VFY_TSA_NAME);
            setTimestampVerifyContextData(ts_context, store, usedCertificates, timestampedData);

            ERR_clear_error();
            isTimestampVerified = TS_RESP_verify_token(ts_context, token) > 0;
            ERR_clear_error();

            if (isTimestampVerified)
            {
                if (TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(token))
                {
                    result.setTimestampDate(getDateTimeFromASN(TS_TST_INFO_get_time(info)));
                    TS_TST_INFO_free(info);
                }
            }

            // Function TS_VERIFY_CTX_cleanup also frees the store, the certificates
            // and the data buffer given to the context.
            TS_VERIFY_CTX_cleanup(ts_context);
            TS_VERIFY_CTX_free(ts_context);

            PKCS7_free(token);
        }
    }

    if (!isTimestampVerified)
    {
        result.addSignatureTimestampNotVerifiedWarning();
    }
}

}   // namespace pdf

#ifdef Q_OS_WIN
#include <Windows.h>
#include <wincrypt.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "crypt32.lib")
#endif
#endif

void pdf::PDFPublicKeySignatureHandler::addTrustedCertificates(X509_STORE* store) const
{
    if (m_parameters.store)
    {
        const PDFCertificateEntries& certificates = m_parameters.store->getCertificates();
        for (const auto& entry : certificates)
        {
            QByteArray certificateData = entry.info.getCertificateData();
            const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
            X509* certificate = d2i_X509(nullptr, &pointer, certificateData.length());
            if (certificate)
            {
                X509_STORE_add_cert(store, certificate);
                X509_free(certificate);
            }
        }
    }

#ifndef Q_OS_WIN
    if (m_parameters.useSystemCertificateStore)
    {
        // B119: the trusted certificates of the system on Linux and macOS (the default paths of OpenSSL)
        X509_STORE_set_default_paths(store);
    }
#endif

#ifdef Q_OS_WIN
    if (m_parameters.useSystemCertificateStore)
    {
        HCERTSTORE certStore = CertOpenSystemStore(0, L"ROOT");
        PCCERT_CONTEXT context = nullptr;
        if (certStore)
        {
            while (context = CertEnumCertificatesInStore(certStore, context))
            {
                const unsigned char* pointer = context->pbCertEncoded;
                X509* certificate = d2i_X509(nullptr, &pointer, context->cbCertEncoded);
                if (certificate)
                {
                    X509_STORE_add_cert(store, certificate);
                    X509_free(certificate);
                }
            }

            CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
        }
    }
#endif

    const bool useAATLCertificates = (m_parameters.aatlCertificates == PDFSignatureHandler::AATLCertificates::Always) ||
                                     (m_parameters.aatlCertificates == PDFSignatureHandler::AATLCertificates::WithSystemStore && m_parameters.useSystemCertificateStore);
    if (useAATLCertificates)
    {
        PDFCertificateEntries aatlCertificates = PDFCertificateStore::getAATLCertificates();
        for (const PDFCertificateEntry& entry : aatlCertificates)
        {
            QByteArray certificateData = entry.info.getCertificateData();
            const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
            X509* certificate = d2i_X509(nullptr, &pointer, certificateData.size());
            if (certificate)
            {
                X509_STORE_add_cert(store, certificate);
                X509_free(certificate);
            }
        }
    }
}

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
