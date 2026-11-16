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

// The security handlers of the core (step 5 of phase 2, plan 9 with R5-13, package P1a): the
// shared cryptographic helpers (the random bytes of OpenSSL, S5.4, B82; the overwriting of
// secrets, S5.1), the algorithms of ISO 32000-2 7.6.4 against an independent implementation
// written here from the text of the specification over the hashes of Qt and the ciphers of
// OpenSSL (never values of a run of the handler), round trips through PDFDocumentReader for
// revision 4 (RC4, AES-128), revision 6 and the public-key handler with its three scopes,
// hostile encryption dictionaries and the old bugs B81 - B108 of the plan.

#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfsecurityhandler.h"
#include "pdfsignaturehandler.h"
#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"

#include "mergetestpdf.h"

#include <QtTest>
#include <QCryptographicHash>
#include <QElapsedTimer>

#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs7.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <set>

using namespace mergetest;
using Factory = pdf::PDFSecurityHandlerFactory;
using Permission = pdf::PDFSecurityHandler::Permission;
using AuthorizationResult = pdf::PDFSecurityHandler::AuthorizationResult;

/// The algorithms of ISO 32000-2, 7.6.3 and 7.6.4, written from the text of the specification
class IsoAlgorithms
{
public:
    /// 7.6.4.3.2, Algorithm 2 step a: the padding string
    static QByteArray padding()
    {
        static const unsigned char bytes[32] = { 0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                                 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A };
        return QByteArray(reinterpret_cast<const char*>(bytes), 32);
    }

    /// Algorithm 2 step a: the password padded or truncated to 32 bytes
    static QByteArray pad(const QByteArray& password) { return (password.left(32) + padding()).left(32); }

    static QByteArray md5(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Md5); }
    static QByteArray sha256(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }

    static QByteArray littleEndian(uint32_t value)
    {
        QByteArray result;
        for (int i = 0; i < 4; ++i)
        {
            result.append(char((value >> (8 * i)) & 0xFF));
        }
        return result;
    }

    /// RC4 (the cipher of 7.6.3.2 for /V 1 - 4 with V2)
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

    /// AES of OpenSSL (CBC or ECB by the cipher), with or without the padding of 7.6.3.2
    static QByteArray aes(const EVP_CIPHER* cipher, const QByteArray& key, const QByteArray& iv, const QByteArray& data, bool encrypt, bool usePadding)
    {
        std::unique_ptr<EVP_CIPHER_CTX, void(*)(EVP_CIPHER_CTX*)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        const unsigned char* ivData = iv.isEmpty() ? nullptr : reinterpret_cast<const unsigned char*>(iv.constData());
        EVP_CipherInit_ex(context.get(), cipher, nullptr, reinterpret_cast<const unsigned char*>(key.constData()), ivData, encrypt ? 1 : 0);
        EVP_CIPHER_CTX_set_padding(context.get(), usePadding ? 1 : 0);
        QByteArray result(data.size() + 32, '\0');
        int length = 0;
        int finalLength = 0;
        EVP_CipherUpdate(context.get(), reinterpret_cast<unsigned char*>(result.data()), &length, reinterpret_cast<const unsigned char*>(data.constData()), int(data.size()));
        EVP_CipherFinal_ex(context.get(), reinterpret_cast<unsigned char*>(result.data()) + length, &finalLength);
        result.resize(length + finalLength);
        return result;
    }

    /// Algorithm 2: the file encryption key of revisions 2 - 4
    static QByteArray fileKey(const QByteArray& password, const QByteArray& O, uint32_t P, const QByteArray& id, int revision, int lengthBytes, bool encryptMetadata = true)
    {
        QByteArray input = pad(password) + O + littleEndian(P) + id;
        if (revision >= 4 && !encryptMetadata)
        {
            input += QByteArray(4, '\xFF');
        }
        QByteArray hash = md5(input);
        const int n = (revision == 2) ? 5 : lengthBytes;
        if (revision >= 3)
        {
            for (int i = 0; i < 50; ++i)
            {
                hash = md5(hash.left(n));
            }
        }
        return hash.left(n);
    }

    /// Algorithm 3: /O of revisions 2 - 4 (a key of 128 bits for revisions 3 and 4); step a: an
    /// empty owner password is the user password
    static QByteArray ownerValue(const QByteArray& owner, const QByteArray& user, int revision)
    {
        QByteArray hash = md5(pad(owner.isEmpty() ? user : owner));
        if (revision >= 3)
        {
            for (int i = 0; i < 50; ++i)
            {
                hash = md5(hash);
            }
        }
        const QByteArray key = hash.left(revision == 2 ? 5 : 16);
        QByteArray result = rc4(key, pad(user));
        if (revision >= 3)
        {
            for (int i = 1; i <= 19; ++i)
            {
                result = rc4(xorKey(key, i), result);
            }
        }
        return result;
    }

    /// Algorithm 4 (revision 2) and 5 (revisions 3, 4; the first 16 bytes): /U
    static QByteArray userValue(const QByteArray& key, const QByteArray& id, int revision)
    {
        if (revision == 2)
        {
            return rc4(key, padding());
        }

        QByteArray result = rc4(key, md5(padding() + id));
        for (int i = 1; i <= 19; ++i)
        {
            result = rc4(xorKey(key, i), result);
        }
        return result;
    }

    static QByteArray xorKey(const QByteArray& key, int value)
    {
        QByteArray result = key;
        for (char& byte : result)
        {
            byte = char(static_cast<unsigned char>(byte) ^ value);
        }
        return result;
    }

    /// Algorithm 1: the key of an object (step b: 3 bytes of the object number and 2 of the
    /// generation, low order first; step c: "sAlT" for AESV2; step d: n + 5 bytes, at most 16)
    static QByteArray objectKey(const QByteArray& fileKey, int objectNumber, int generation, bool aes)
    {
        QByteArray input = fileKey;
        input.append(char(objectNumber & 0xFF)).append(char((objectNumber >> 8) & 0xFF)).append(char((objectNumber >> 16) & 0xFF));
        input.append(char(generation & 0xFF)).append(char((generation >> 8) & 0xFF));
        if (aes)
        {
            input.append("sAlT");
        }
        return md5(input).left(qMin(int(fileKey.size()) + 5, 16));
    }

    /// 7.6.3.2: the data encrypted by RC4 (V2), AES-128 (AESV2) or AES-256 (AESV3; the file key
    /// itself), the AES data begin by the 16 bytes of the IV
    static QByteArray encrypt(const QByteArray& fileKey, int objectNumber, const QByteArray& data, const QByteArray& method)
    {
        const QByteArray iv(16, char(0x5A));
        if (method == "V2")
        {
            return rc4(objectKey(fileKey, objectNumber, 0, false), data);
        }
        if (method == "AESV2")
        {
            return iv + aes(EVP_aes_128_cbc(), objectKey(fileKey, objectNumber, 0, true), iv, data, true, true);
        }
        return iv + aes(EVP_aes_256_cbc(), fileKey, iv, data, true, true);
    }

    /// Algorithm 2.B: the hash of revision 6 (the remainder modulo 3 of the 16 bytes as a big
    /// endian number is the sum of the bytes modulo 3, because 256 = 1 modulo 3)
    static QByteArray hash2B(const QByteArray& password, const QByteArray& salt, const QByteArray& userKey)
    {
        QByteArray K = sha256(password + salt + userKey);
        int round = 0;
        while (true)
        {
            const QByteArray K1 = (password + K + userKey).repeated(64);
            const QByteArray E = aes(EVP_aes_128_cbc(), K.left(16), K.mid(16, 16), K1, true, false);
            int sum = 0;
            for (int i = 0; i < 16; ++i)
            {
                sum += static_cast<unsigned char>(E[i]);
            }
            switch (sum % 3)
            {
                case 0:
                    K = QCryptographicHash::hash(E, QCryptographicHash::Sha256);
                    break;
                case 1:
                    K = QCryptographicHash::hash(E, QCryptographicHash::Sha384);
                    break;
                default:
                    K = QCryptographicHash::hash(E, QCryptographicHash::Sha512);
                    break;
            }
            ++round;

            // Steps e - g: 64 rounds, then until the last byte of E is at most the round - 32
            if (round >= 64 && static_cast<unsigned char>(E.back()) <= round - 32)
            {
                break;
            }
        }
        return K.left(32);
    }

    /// Algorithms 8, 9, 10: /U, /UE, /O, /OE, /Perms of revision 6 (or 5 by \p revision: SHA-256)
    struct Revision6
    {
        QByteArray U;
        QByteArray UE;
        QByteArray O;
        QByteArray OE;
        QByteArray Perms;
    };

    static Revision6 createRevision6(const QByteArray& user, const QByteArray& owner, const QByteArray& fileKey, uint32_t P, bool encryptMetadata, int revision)
    {
        auto hash = [revision](const QByteArray& password, const QByteArray& salt, const QByteArray& userKey)
        {
            return revision == 6 ? hash2B(password, salt, userKey) : sha256(password + salt + userKey);
        };

        const QByteArray userValidationSalt("uvsalt-1", 8);
        const QByteArray userKeySalt("uksalt-2", 8);
        const QByteArray ownerValidationSalt("ovsalt-3", 8);
        const QByteArray ownerKeySalt("oksalt-4", 8);

        Revision6 result;
        result.U = hash(user, userValidationSalt, QByteArray()) + userValidationSalt + userKeySalt;
        result.UE = aes(EVP_aes_256_cbc(), hash(user, userKeySalt, QByteArray()), QByteArray(16, '\0'), fileKey, true, false);
        result.O = hash(owner, ownerValidationSalt, result.U) + ownerValidationSalt + ownerKeySalt;
        result.OE = aes(EVP_aes_256_cbc(), hash(owner, ownerKeySalt, result.U), QByteArray(16, '\0'), fileKey, true, false);
        const QByteArray perms = littleEndian(P) + QByteArray(4, '\xFF') + QByteArray(encryptMetadata ? "T" : "F") + "adb" + "rand";
        result.Perms = aes(EVP_aes_256_ecb(), fileKey, QByteArray(), perms, true, false);
        return result;
    }
};

/// The documents, files, certificates and readers of the tests
class SecurityHandlerTestHelper
{
public:
    static QByteArray hex(const QByteArray& data) { return "<" + data.toHex() + ">"; }

    /// The password of the PKCS#12 of the tests, with diacritics outside of Latin-1 (B84: UTF-8)
    static QString pfxPassword() { return QString::fromUtf8("Pfx-\xC5\xBDlu\xC5\xA5ou\xC4\x8Dk\xC3\xBD"); }

    /// The first element of /ID of every test file
    static QByteArray id() { return QByteArray("0123456789ABCDEF"); }

    /// A literal string of PDF (7.3.4.2), so plain ASCII stays readable in the bytes of a file
    static QByteArray literal(const QByteArray& data)
    {
        QByteArray result = "(";
        for (const char character : data)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (character == '(' || character == ')' || character == '\\')
            {
                result += '\\';
                result += character;
            }
            else if (byte >= 0x20 && byte < 0x7F)
            {
                result += character;
            }
            else
            {
                result += '\\' + QByteArray::number(byte, 8).rightJustified(3, '0');
            }
        }
        return result + ")";
    }

    /// The text of an object (direct objects only)
    static QByteArray toText(const pdf::PDFObject& object)
    {
        if (object.isNull())
        {
            return "null";
        }
        if (object.isBool())
        {
            return object.getBool() ? "true" : "false";
        }
        if (object.isInt())
        {
            return QByteArray::number(qint64(object.getInteger()));
        }
        if (object.isReal())
        {
            return QByteArray::number(object.getReal());
        }
        if (object.isName())
        {
            return "/" + object.getString();
        }
        if (object.isString())
        {
            return literal(object.getString());
        }
        if (object.isReference())
        {
            return QByteArray::number(qint64(object.getReference().objectNumber)) + " " + QByteArray::number(qint64(object.getReference().generation)) + " R";
        }
        if (object.isArray())
        {
            QByteArray result = "[";
            for (size_t i = 0; i < object.getArray()->getCount(); ++i)
            {
                result += " " + toText(object.getArray()->getItem(i));
            }
            return result + " ]";
        }
        if (object.isDictionary())
        {
            return toText(object.getDictionary());
        }
        if (object.isStream())
        {
            const pdf::PDFStream* stream = object.getStream();
            return toText(stream->getDictionary()) + "\nstream\n" + *stream->getContent() + "\nendstream";
        }
        return "null";
    }

    static QByteArray toText(const pdf::PDFDictionary* dictionary)
    {
        QByteArray result = "<<";
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            result += " /" + dictionary->getKey(i).getString() + " " + toText(dictionary->getValue(i));
        }
        return result + " >>";
    }

    /// The plain test document; the markers are searched in the bytes of the files. Objects: 101
    /// the page content, 5 the file specification, 6 the XMP metadata, 7 the embedded file, 8 the
    /// document information, 9 a signature dictionary without /Type (B88), 11 a stream with its
    /// own crypt filter Identity (B87, B106)
    static MergeTestPdf createPlain()
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("CONTENT-MARKER-", 1, "/Metadata 6 0 R /Names << /EmbeddedFiles << /Names [(a.txt) 5 0 R] >> >>");
        pdf.set(5, "<< /Type /Filespec /F (a.txt) /UF (a.txt) /EF << /F 7 0 R >> >>");
        pdf.set(6, MergeTestPdf::stream("<x:xmpmeta>METADATA-MARKER</x:xmpmeta>", "/Type /Metadata /Subtype /XML"));
        pdf.set(7, MergeTestPdf::stream("ATTACHMENT-MARKER-7", "/Type /EmbeddedFile /Params << /Size 19 >>"));
        pdf.set(8, "<< /Title (TITLE-MARKER) >>");
        pdf.set(9, "<< /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 10 20 30] /Contents (SIGNATURE-MARKER) /M (D:20261114) >>");
        pdf.set(11, MergeTestPdf::stream("CRYPT-IDENTITY-MARKER", "/Filter /Crypt /DecodeParms << /Name /Identity >>"));
        pdf.setTrailerEntries("/Info 8 0 R /ID [" + hex(id()) + " " + hex(id()) + "]");
        return pdf;
    }

    /// The file of the plain document encrypted object by object by \p handler (its visitor);
    /// the encryption dictionary is the last object
    static QByteArray encryptDocument(const MergeTestPdf& plain, const pdf::PDFSecurityHandler* handler)
    {
        const pdf::PDFDocument document = plain.read();
        const pdf::PDFObjectStorage::PDFObjects& objects = document.getStorage().getObjects();
        MergeTestPdf encrypted;
        for (size_t i = 1; i < objects.size(); ++i)
        {
            if (!objects[i].object.isNull())
            {
                encrypted.set(int(i), toText(handler->encryptObject(objects[i].object, pdf::PDFObjectReference(pdf::PDFInteger(i), 0))));
            }
        }
        const int encryptNumber = int(objects.size());
        encrypted.set(encryptNumber, toText(handler->createEncryptionDictionaryObject()));
        encrypted.setTrailerEntries("/Info 8 0 R /Encrypt " + QByteArray::number(encryptNumber) + " 0 R /ID [" + hex(id()) + " " + hex(id()) + "]");
        return encrypted.write();
    }

    /// The objects of a file of another writer with /EncryptMetadata false (Table 20), revision 4
    /// (AESV2, Algorithms 2, 3, 5 with step f of Algorithm 2) or 6 (AESV3, Algorithms 8 - 10), no
    /// open password: the catalog 1 (/Metadata 6 0 R) - in the object stream 20 together with a
    /// second catalog 50, or an object of its own -, the page 3 with its XMP 12, the content 101,
    /// the document XMP 6 in the clear. With the catalog in the object stream the XMP of the page is
    /// in the clear too (written by the rule of /Type), otherwise it is encrypted.
    static QByteArray createMetadataFile(int revision, bool isCatalogInObjectStream)
    {
        const uint32_t P = 0xFFFFFFFCu;
        QByteArray fileKey;
        QByteArray method;
        QByteArray encrypt;
        if (revision == 6)
        {
            fileKey = QByteArray("0123456789abcdef0123456789ABCDEF");
            method = "AESV3";
            const IsoAlgorithms::Revision6 values = IsoAlgorithms::createRevision6(QByteArray(), "owner-m", fileKey, P, false, 6);
            encrypt = "<< /Filter /Standard /V 5 /R 6 /Length 256 /CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> >> /StmF /StdCF /StrF /StdCF /O " + hex(values.O) +
                      " /U " + hex(values.U) + " /OE " + hex(values.OE) + " /UE " + hex(values.UE) + " /Perms " + hex(values.Perms);
        }
        else
        {
            method = "AESV2";
            const QByteArray O = IsoAlgorithms::ownerValue("owner-m", QByteArray(), 4);
            fileKey = IsoAlgorithms::fileKey(QByteArray(), O, P, id(), 4, 16, false);
            const QByteArray U = IsoAlgorithms::userValue(fileKey, id(), 4) + QByteArray(16, '\0');
            encrypt = "<< /Filter /Standard /V 4 /R 4 /Length 128 /CF << /StdCF << /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> >> /StmF /StdCF /StrF /StdCF /O " + hex(O) + " /U " + hex(U);
        }
        encrypt += " /P " + QByteArray::number(int32_t(P)) + " /EncryptMetadata false >>";

        const QByteArray catalog = "<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R >>";
        std::map<int, QByteArray> objects;
        objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
        objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 101 0 R /Metadata 12 0 R >>";
        objects[6] = MergeTestPdf::stream("DOCUMENT-XMP-MARKER", "/Type /Metadata /Subtype /XML");
        objects[12] = MergeTestPdf::stream(isCatalogInObjectStream ? QByteArray(PAGE_XMP) : IsoAlgorithms::encrypt(fileKey, 12, PAGE_XMP, method), "/Type /Metadata /Subtype /XML");
        objects[13] = encrypt;
        objects[101] = MergeTestPdf::stream(IsoAlgorithms::encrypt(fileKey, 101, PAGE_CONTENT, method));
        std::vector<int> inObjectStream;
        if (isCatalogInObjectStream)
        {
            // 7.5.7: the object stream is encrypted as a stream, the objects in it are not encrypted again
            const QByteArray header = "1 0 50 " + QByteArray::number(catalog.size() + 1) + " ";
            const QByteArray objectStreamData = header + catalog + " " + catalog;
            objects[20] = MergeTestPdf::stream(IsoAlgorithms::encrypt(fileKey, 20, objectStreamData, method), "/Type /ObjStm /N 2 /First " + QByteArray::number(header.size()));
            inObjectStream = { 1, 50 };
        }
        else
        {
            objects[1] = catalog;
        }
        return writeWithCrossReferenceStream(objects, 20, inObjectStream, "/Encrypt 13 0 R /ID [" + hex(id()) + " " + hex(id()) + "]");
    }

    static constexpr const char* PAGE_XMP = "PAGE-XMP-MARKER";
    static constexpr const char* PAGE_CONTENT = "0 0 m 10 10 l S";

    /// A file of \p objects with a cross-reference stream (7.5.8; /W [1 4 2], no filter), whose
    /// dictionary is the trailer with \p trailerEntries; the objects \p inObjectStream are in the
    /// object stream \p objectStream (rows of type 2 by their index, ISO 32000-2 7.5.8.3)
    static QByteArray writeWithCrossReferenceStream(const std::map<int, QByteArray>& objects, int objectStream, const std::vector<int>& inObjectStream, const QByteArray& trailerEntries)
    {
        QByteArray data = "%PDF-1.7\n";
        const int xrefNumber = objects.rbegin()->first + 1;
        const int size = xrefNumber + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : objects)
        {
            offsets[size_t(number)] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + text + "\nendobj\n";
        }
        offsets[size_t(xrefNumber)] = data.size();

        QByteArray rows;
        for (int i = 0; i < size; ++i)
        {
            const auto it = std::find(inObjectStream.cbegin(), inObjectStream.cend(), i);
            const bool isInObjectStream = it != inObjectStream.cend();
            const bool isUsed = i > 0 && offsets[size_t(i)] >= 0;
            const quint32 field = isInObjectStream ? quint32(objectStream) : (isUsed ? quint32(offsets[size_t(i)]) : 0);
            const quint32 index = isInObjectStream ? quint32(std::distance(inObjectStream.cbegin(), it)) : ((isUsed || i == 0) ? 0 : 0xFFFF);
            rows.append(char(isInObjectStream ? 2 : (isUsed ? 1 : 0)));
            for (int shift = 24; shift >= 0; shift -= 8)
            {
                rows.append(char((field >> shift) & 0xFF));
            }
            const bool isFree = !isInObjectStream && !isUsed;
            rows.append(char(isFree ? 0xFF : ((index >> 8) & 0xFF)));
            rows.append(char(isFree ? 0xFF : (index & 0xFF)));
        }
        data += QByteArray::number(xrefNumber) + " 0 obj\n<< /Type /XRef /Size " + QByteArray::number(size) + " /W [1 4 2] /Root 1 0 R " + trailerEntries +
                " /Length " + QByteArray::number(rows.size()) + " >>\nstream\n" + rows + "\nendstream\nendobj\n";
        data += "startxref\n" + QByteArray::number(offsets[size_t(xrefNumber)]) + "\n%%EOF\n";
        return data;
    }

    /// The file \p original updated incrementally by PDFDocumentWriter: the objects of \p changes
    /// (their text) replace the objects of the document read without a password; \p root (when
    /// not 0) is the new /Root of the trailer
    static QByteArray updateIncrementally(const QByteArray& original, const std::map<int, QByteArray>& changes, int root = 0)
    {
        const ReadResult read = SecurityHandlerTestHelper::read(original, QString());
        pdf::PDFDocumentBuilder builder(&read.document);
        for (const auto& [number, text] : changes)
        {
            builder.setObject(pdf::PDFObjectReference(number, 0), parse(text));
        }
        pdf::PDFDocument updated = builder.build();
        if (root > 0)
        {
            pdf::PDFObjectStorage storage = updated.getStorage();
            pdf::PDFDictionaryBuilder trailer(*updated.getTrailerDictionary());
            trailer.setEntry(pdf::PDFInplaceOrMemoryString("Root"), pdf::PDFObject::createReference(pdf::PDFObjectReference(root, 0)));
            storage.setTrailerDictionary(pdf::PDFObject::createDictionary(std::move(trailer)));
            updated = pdf::PDFDocument(std::move(storage), updated.getInfo()->version, updated.getSourceDataHash());
        }

        QByteArray result;
        QBuffer buffer(&result);
        buffer.open(QBuffer::WriteOnly);
        return pdf::PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, original, &updated) ? result : QByteArray();
    }

    struct ReadResult
    {
        pdf::PDFDocument document;
        pdf::PDFDocumentReader::Result result = pdf::PDFDocumentReader::Result::Failed;
        QString errorMessage;
        int callbackCount = 0;
        bool isEmbeddedFilesOnlyEncryption = false;
    };

    /// Reads the bytes by PDFDocumentReader, the callback gives \p password once
    static ReadResult read(const QByteArray& bytes, const QString& password, bool authorizeOwnerOnly = false,
                           const pdf::PDFCertificateEntries* certificates = nullptr, bool openWithoutAttachments = false, bool permissive = false)
    {
        ReadResult result;
        auto callback = [&password, &result](bool* ok) -> QString
        {
            *ok = result.callbackCount == 0;
            ++result.callbackCount;
            return password;
        };
        pdf::PDFDocumentReader reader(nullptr, callback, permissive, authorizeOwnerOnly);
        if (certificates)
        {
            reader.setCertificates(*certificates);
        }
        reader.setOpenWithoutAttachments(openWithoutAttachments);
        result.document = reader.readFromBuffer(bytes);
        result.result = reader.getReadingResult();
        result.errorMessage = reader.getErrorMessage();
        result.isEmbeddedFilesOnlyEncryption = reader.isEmbeddedFilesOnlyEncryption();
        return result;
    }

    static const pdf::PDFObject& object(const pdf::PDFDocument& document, int number)
    {
        return document.getObjectByReference(pdf::PDFObjectReference(number, 0));
    }

    static QByteArray streamData(const pdf::PDFDocument& document, int number)
    {
        const pdf::PDFObject& streamObject = object(document, number);
        return streamObject.isStream() ? *streamObject.getStream()->getContent() : QByteArray();
    }

    /// The object of PDF text (object 99 of a small document)
    static pdf::PDFObject parse(const QByteArray& text)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
        pdf.set(99, text);
        const pdf::PDFDocument document = pdf.read();
        return object(document, 99);
    }

    static const pdf::PDFDictionary* dictionary(const pdf::PDFObject& object)
    {
        return object.isDictionary() ? object.getDictionary() : nullptr;
    }

    /// The encryption dictionary of a handler of the factory
    static pdf::PDFObject encryptionDictionary(const pdf::PDFSecurityHandlerPointer& handler)
    {
        return handler->createEncryptionDictionaryObject();
    }

    static QByteArray getString(const pdf::PDFObject& encryption, const char* key)
    {
        const pdf::PDFObject& value = encryption.getDictionary()->get(key);
        return value.isString() ? value.getString() : QByteArray();
    }

    static uint32_t getP(const pdf::PDFObject& encryption)
    {
        return uint32_t(int32_t(encryption.getDictionary()->get("P").getInteger()));
    }

    static Factory::SecuritySettings settings(Factory::Algorithm algorithm, const QString& user, const QString& owner, uint32_t permissions = 0xFFFFFFFF)
    {
        Factory::SecuritySettings result;
        result.algorithm = algorithm;
        result.userPassword = user;
        result.ownerPassword = owner;
        result.permissions = permissions;
        result.id = id();
        return result;
    }

    /// Every permission of Table 22
    static uint32_t allPermissions()
    {
        return uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::Modify) | uint32_t(Permission::CopyContent) | uint32_t(Permission::ModifyInteractiveItems) |
               uint32_t(Permission::ModifyFormFields) | uint32_t(Permission::Accessibility) | uint32_t(Permission::Assemble) | uint32_t(Permission::PrintHighResolution);
    }

    /// A PKCS#12 of a self-signed RSA 2048 certificate made now (never a committed key)
    static QByteArray createPkcs12(const QString& password)
    {
        std::unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
        std::unique_ptr<X509, void(*)(X509*)> certificate(X509_new(), X509_free);
        X509_set_version(certificate.get(), 2);
        ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 4242);
        X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -3600);
        X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600L * 24 * 365);
        X509_NAME* name = X509_get_subject_name(certificate.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>("PDF4QT Test Recipient"), -1, -1, 0);
        X509_set_issuer_name(certificate.get(), name);
        X509_set_pubkey(certificate.get(), key.get());
        X509_sign(certificate.get(), key.get(), EVP_sha256());

        const QByteArray passwordUtf8 = password.toUtf8();
        std::unique_ptr<PKCS12, void(*)(PKCS12*)> pkcs12(PKCS12_create(passwordUtf8.constData(), "test", key.get(), certificate.get(), nullptr, 0, 0, 0, 0, 0), PKCS12_free);
        unsigned char* buffer = nullptr;
        const int length = i2d_PKCS12(pkcs12.get(), &buffer);
        QByteArray result(reinterpret_cast<const char*>(buffer), length);
        OPENSSL_free(buffer);
        return result;
    }

    /// The certificate entry of a PKCS#12 only: its certificate is not read without the password
    /// (a .pfx of OpenSSL 3 encrypts it, B103) - the handler needs nothing else (R5-1)
    static pdf::PDFCertificateEntry createEntry(const QByteArray& pkcs12)
    {
        pdf::PDFCertificateEntry entry;
        entry.pkcs12 = pkcs12;
        entry.pkcs12fileName = QStringLiteral("test.pfx");
        return entry;
    }

    /// Reads the bytes by a reader, which opens them by a clone of \p handler (R6-1 of step 6); the
    /// password callback refuses (and counts), no certificate of the user is read
    static ReadResult readByHandler(const QByteArray& bytes, const pdf::PDFSecurityHandler* handler, bool authorizeOwnerOnly)
    {
        ReadResult result;
        auto callback = [&result](bool* ok) -> QString
        {
            *ok = false;
            ++result.callbackCount;
            return QString();
        };
        pdf::PDFDocumentReader reader(nullptr, callback, false, authorizeOwnerOnly);
        reader.setCertificates(pdf::PDFCertificateEntries());
        reader.setAuthenticatedSecurityHandler(handler);
        result.document = reader.readFromBuffer(bytes);
        result.result = reader.getReadingResult();
        result.errorMessage = reader.getErrorMessage();
        return result;
    }

    /// Replaces the iteration count of the MAC of a PKCS#12; the MAC is not computed again (6.3 of
    /// step 6: the file must be refused before its MAC is verified)
    static QByteArray setMacIterations(const QByteArray& pkcs12Data, int64_t iterations)
    {
        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(pkcs12Data.constData());
        std::unique_ptr<PKCS12, void(*)(PKCS12*)> pkcs12(d2i_PKCS12(nullptr, &pointer, long(pkcs12Data.size())), PKCS12_free);
        const ASN1_OCTET_STRING* mac = nullptr;
        const X509_ALGOR* algorithm = nullptr;
        const ASN1_OCTET_STRING* salt = nullptr;
        const ASN1_INTEGER* iterationCount = nullptr;
        if (!pkcs12)
        {
            return QByteArray();
        }
        PKCS12_get0_mac(&mac, &algorithm, &salt, &iterationCount, pkcs12.get());
        if (!iterationCount)
        {
            return QByteArray();
        }
        ASN1_INTEGER_set_int64(const_cast<ASN1_INTEGER*>(iterationCount), iterations);
        unsigned char* buffer = nullptr;
        const int length = i2d_PKCS12(pkcs12.get(), &buffer);
        QByteArray result(reinterpret_cast<const char*>(buffer), length);
        OPENSSL_free(buffer);
        return result;
    }

    static bool isThrown(const std::function<void()>& function)
    {
        try
        {
            function();
        }
        catch (const pdf::PDFException&)
        {
            return true;
        }
        return false;
    }
};

using H = SecurityHandlerTestHelper;
using Iso = IsoAlgorithms;

class SecurityHandlerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void randomBytes();
    void randomBytesLimits();
    void secureMemoryClear();
    void rc4KnownAnswer();
    void revision4Factory_data();
    void revision4Factory();
    void revision2And3And4Reader_data();
    void revision2And3And4Reader();
    void revision6Factory();
    void revision5And6Reader_data();
    void revision5And6Reader();
    void emptyOwnerPassword_data();
    void emptyOwnerPassword();
    void restrictionsNeedOwnerPassword();
    void permissionBits();
    void randomnessOfHandlers();
    void roundTrip_data();
    void roundTrip();
    void certificateRoundTrip_data();
    void certificateRoundTrip();
    void certificatePermissions();
    void certificateEnvelope_data();
    void certificateEnvelope();
    void certificateFailures();
    void embeddedFilesOnly_data();
    void embeddedFilesOnly();
    void embeddedFilesLockedNoOtherKey();
    void cryptFilterStreams();
    void externalFileStream();
    void documentMetadataOnly_data();
    void documentMetadataOnly();
    void documentMetadataCatalogUpdated_data();
    void documentMetadataCatalogUpdated();
    void documentMetadataWritten_data();
    void documentMetadataWritten();
    void hostileEncryptionDictionaries_data();
    void hostileEncryptionDictionaries();
    void hostileCryptFilterLength();
    void tooManyRecipients();
    void saslPrep();
    void passwordEncodingRevision4();

    // Step 6 of phase 2, integration P2S6INTCORE (R6-1, Q6.2, 6.3)
    void authenticatedHandlerReadBack_data();
    void authenticatedHandlerReadBack();
    void authenticatedHandlerOfCertificate();
    void certificateOfSystemStore();
    void certificateIterationLimit();
    void poolTasksReleaseOpenSslState_data();
    void poolTasksReleaseOpenSslState();

private:
    /// The PKCS#12 of the session with a password with diacritics (B84)
    QByteArray m_pkcs12;
};

void SecurityHandlerTest::initTestCase()
{
    m_pkcs12 = H::createPkcs12(H::pfxPassword());
    QVERIFY(!m_pkcs12.isEmpty());
}

void SecurityHandlerTest::randomBytes()
{
    // The asked length: a key of revision 6 (32), an IV of AES (16), the seed of the public-key
    // handler (20), the 4 random bytes of /Perms; nothing for 0
    for (const int size : { 0, 1, 4, 16, 20, 32, 1000 })
    {
        QCOMPARE(Factory::generateRandomBytes(size).size(), qsizetype(size));
    }

    // Two keys differ, and so do 1000 IVs (a cryptographic generator repeats 16 bytes with the
    // probability 2^-128; a fixed or a zero generator repeats at once)
    const QByteArray first = Factory::generateRandomBytes(32);
    const QByteArray second = Factory::generateRandomBytes(32);
    QVERIFY(first != second);
    QVERIFY(first != QByteArray(32, '\0'));

    std::set<QByteArray> ivs;
    for (int i = 0; i < 1000; ++i)
    {
        ivs.insert(Factory::generateRandomBytes(16));
    }
    QCOMPARE(ivs.size(), size_t(1000));

    // Every byte value appears in 64 KiB (each misses with the probability (255/256)^65536)
    const QByteArray block = Factory::generateRandomBytes(65536);
    std::set<char> values(block.cbegin(), block.cend());
    QCOMPARE(values.size(), size_t(256));
}

void SecurityHandlerTest::randomBytesLimits()
{
    // A size out of the range fails loudly - never an empty or a short array used as a key
    QVERIFY(H::isThrown([]() { Factory::generateRandomBytes(-1); }));
    QVERIFY(H::isThrown([]() { Factory::generateRandomBytes(Factory::MAXIMUM_RANDOM_BYTES + 1); }));
    QCOMPARE(Factory::generateRandomBytes(Factory::MAXIMUM_RANDOM_BYTES).size(), qsizetype(Factory::MAXIMUM_RANDOM_BYTES));
}

void SecurityHandlerTest::secureMemoryClear()
{
    // A raw buffer (the 32 bytes of a key of revision 4) is overwritten by zeros
    std::array<char, 32> key;
    key.fill('k');
    pdf::PDFSecureMemory::clear(key.data(), key.size());
    for (const char value : key)
    {
        QCOMPARE(value, '\0');
    }

    // Nothing for null or zero bytes
    key.fill('k');
    pdf::PDFSecureMemory::clear(key.data(), 0);
    QCOMPARE(key.front(), 'k');
    pdf::PDFSecureMemory::clear(nullptr, 16);

    // A password of its own buffer: every byte is zero, the size stays
    QByteArray password("Canary-7f3a");
    password.detach();
    pdf::PDFSecureMemory::clear(password);
    QCOMPARE(password, QByteArray(11, '\0'));

    // A buffer shared with another copy is not written: the other owner keeps its value
    QByteArray shared("Canary-shared");
    const QByteArray copy = shared;
    pdf::PDFSecureMemory::clear(shared);
    QCOMPARE(copy, QByteArray("Canary-shared"));

    // Raw data of another owner are not written either
    char foreign[] = "foreign";
    QByteArray raw = QByteArray::fromRawData(foreign, 7);
    pdf::PDFSecureMemory::clear(raw);
    QCOMPARE(QByteArray(foreign), QByteArray("foreign"));

    // A password as text (UTF-16), the size stays
    QString text = QString::fromUtf8("Heslo-\xC5\xBE\xC4\x8D");
    text.detach();
    const qsizetype length = text.size();
    pdf::PDFSecureMemory::clear(text);
    QCOMPARE(text.size(), length);
    for (const QChar character : text)
    {
        QVERIFY(character.isNull());
    }
}

void SecurityHandlerTest::rc4KnownAnswer()
{
    // The RC4 of this test (the oracle of revisions 2 - 4) against the published test vectors of
    // the cipher (key "Key", "Plaintext"; key "Wiki", "pedia")
    QCOMPARE(Iso::rc4("Key", "Plaintext").toHex(), QByteArray("bbf316e8d940af0ad3"));
    QCOMPARE(Iso::rc4("Wiki", "pedia").toHex(), QByteArray("1021bf0420"));
}

void SecurityHandlerTest::revision4Factory_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<QString>("user");
    QTest::addColumn<QString>("owner");
    QTest::addColumn<QByteArray>("method");

    QTest::newRow("rc4") << int(Factory::RC4) << QStringLiteral("user") << QStringLiteral("owner") << QByteArray("V2");
    QTest::newRow("rc4-empty-user") << int(Factory::RC4) << QString() << QStringLiteral("owner") << QByteArray("V2");
    QTest::newRow("aes128") << int(Factory::AES_128) << QStringLiteral("user") << QStringLiteral("owner") << QByteArray("AESV2");
    QTest::newRow("aes128-pdfdoc") << int(Factory::AES_128) << QString::fromUtf8("u\xC5\xBE\xE2\x82\xAC") << QStringLiteral("owner") << QByteArray("AESV2");
}

void SecurityHandlerTest::revision4Factory()
{
    // ISO 32000-2, 7.6.4.3 and 7.6.4.4: the values of the factory against Algorithms 2, 3 and 5
    // computed here (the key of 128 bits, /ID[0] of the settings, R5-5)
    QFETCH(int, algorithm);
    QFETCH(QString, user);
    QFETCH(QString, owner);
    QFETCH(QByteArray, method);

    const uint32_t permissions = uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::CopyContent);
    QString errorMessage;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::Algorithm(algorithm), user, owner, permissions), &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));
    QCOMPARE(handler->getAuthorizationResult(), AuthorizationResult::OwnerAuthorized);

    const pdf::PDFObject encryption = H::encryptionDictionary(handler);
    const pdf::PDFDictionary* dictionary = encryption.getDictionary();
    QCOMPARE(dictionary->get("Filter").getString(), QByteArray("Standard"));
    QCOMPARE(dictionary->get("V").getInteger(), pdf::PDFInteger(4));
    QCOMPARE(dictionary->get("R").getInteger(), pdf::PDFInteger(4));
    QCOMPARE(dictionary->get("Length").getInteger(), pdf::PDFInteger(128));
    QCOMPARE(dictionary->get("StmF").getString(), QByteArray("StdCF"));
    QCOMPARE(dictionary->get("StrF").getString(), QByteArray("StdCF"));
    const pdf::PDFDictionary* filters = dictionary->get("CF").getDictionary();
    QVERIFY(filters);
    QCOMPARE(filters->get("StdCF").getDictionary()->get("CFM").getString(), method);

    // Identity is predefined: never an entry of /CF (Table 25)
    QVERIFY(!filters->hasKey("Identity"));

    // PDFDocEncoding of the passwords (Algorithm 2 step a)
    const QByteArray userBytes = pdf::PDFEncoding::convertToEncoding(user, pdf::PDFEncoding::Encoding::PDFDoc);
    const QByteArray ownerBytes = pdf::PDFEncoding::convertToEncoding(owner, pdf::PDFEncoding::Encoding::PDFDoc);
    const QByteArray O = H::getString(encryption, "O");
    const QByteArray U = H::getString(encryption, "U");
    const uint32_t P = H::getP(encryption);

    QCOMPARE(O.toHex(), Iso::ownerValue(ownerBytes, userBytes, 4).toHex());
    const QByteArray key = Iso::fileKey(userBytes, O, P, H::id(), 4, 16);
    QCOMPARE(U.size(), qsizetype(32));
    QCOMPARE(U.left(16).toHex(), Iso::userValue(key, H::id(), 4).toHex());

    // A string encrypted by the handler is the string of Algorithm 1 (RC4 deterministic; AES with
    // its IV: decrypted by the key of the object computed here)
    const QByteArray plain("Secret text of object 12");
    const QByteArray encrypted = handler->encrypt(plain, pdf::PDFObjectReference(12, 0), pdf::PDFSecurityHandler::EncryptionScope::String);
    if (method == "V2")
    {
        QCOMPARE(encrypted.toHex(), Iso::encrypt(key, 12, plain, method).toHex());
    }
    else
    {
        QCOMPARE(Iso::aes(EVP_aes_128_cbc(), Iso::objectKey(key, 12, 0, true), encrypted.left(16), encrypted.mid(16), false, true), plain);
    }
}

void SecurityHandlerTest::revision2And3And4Reader_data()
{
    QTest::addColumn<int>("revision");
    QTest::addColumn<int>("version");
    QTest::addColumn<QByteArray>("method");

    QTest::newRow("r2-rc40") << 2 << 1 << QByteArray("V2");
    QTest::newRow("r3-rc128") << 3 << 2 << QByteArray("V2");
    QTest::newRow("r4-rc4") << 4 << 4 << QByteArray("V2");
    QTest::newRow("r4-aesv2") << 4 << 4 << QByteArray("AESV2");
}

void SecurityHandlerTest::revision2And3And4Reader()
{
    // Files written here by Algorithms 1 - 5 (7.6.4.3.2, 7.6.4.4) are opened by our reader: the
    // user password, the owner password (Algorithm 7) and a wrong one (Algorithms 6, 7)
    QFETCH(int, revision);
    QFETCH(int, version);
    QFETCH(QByteArray, method);

    const QByteArray user("user-pw");
    const QByteArray owner("owner-pw");
    const uint32_t P = 0xFFFFF0C0u | 0x0004u | 0x0200u;
    const int lengthBytes = (revision == 2) ? 5 : 16;
    const QByteArray O = Iso::ownerValue(owner, user, revision);
    const QByteArray key = Iso::fileKey(user, O, P, H::id(), revision, lengthBytes);
    QByteArray U = Iso::userValue(key, H::id(), revision);
    if (revision >= 3)
    {
        U += QByteArray(16, char(0x11));
    }

    QByteArray encrypt = "<< /Filter /Standard /V " + QByteArray::number(version) + " /R " + QByteArray::number(revision) +
                         " /Length " + QByteArray::number(lengthBytes * 8) + " /O " + H::hex(O) + " /U " + H::hex(U) + " /P " + QByteArray::number(int32_t(P));
    if (version == 4)
    {
        encrypt += " /CF << /StdCF << /CFM /" + method + " /AuthEvent /DocOpen /Length 16 >> >> /StmF /StdCF /StrF /StdCF";
    }
    encrypt += " >>";

    MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
    pdf.set(12, "<< /Text " + H::hex(Iso::encrypt(key, 12, "Plain text of revision", method)) + " >>");
    pdf.set(13, encrypt);
    pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
    const QByteArray bytes = pdf.write();

    const H::ReadResult asUser = H::read(bytes, QString::fromLatin1(user));
    QCOMPARE(asUser.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(asUser.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::UserAuthorized);
    QCOMPARE(H::object(asUser.document, 12).getDictionary()->get("Text").getString(), QByteArray("Plain text of revision"));

    const H::ReadResult asOwner = H::read(bytes, QString::fromLatin1(owner));
    QCOMPARE(asOwner.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(asOwner.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::OwnerAuthorized);
    QCOMPARE(H::object(asOwner.document, 12).getDictionary()->get("Text").getString(), QByteArray("Plain text of revision"));

    const H::ReadResult wrong = H::read(bytes, QStringLiteral("wrong"));
    QCOMPARE(wrong.result, pdf::PDFDocumentReader::Result::Canceled);
}

void SecurityHandlerTest::revision6Factory()
{
    // 7.6.4.4.7 - 7.6.4.4.9 (Algorithms 8, 9, 10) against Algorithm 2.B computed here: /U, /UE,
    // /O, /OE give one file key, /Perms holds /P, 'T' and "adb"
    const QString user = QString::fromUtf8("u\xC5\xBEivatel");
    const QString owner = QStringLiteral("owner-256");
    QString errorMessage;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, user, owner, uint32_t(Permission::PrintLowResolution)), &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));

    const pdf::PDFObject encryption = H::encryptionDictionary(handler);
    const pdf::PDFDictionary* dictionary = encryption.getDictionary();
    QCOMPARE(dictionary->get("V").getInteger(), pdf::PDFInteger(5));
    QCOMPARE(dictionary->get("R").getInteger(), pdf::PDFInteger(6));
    QCOMPARE(dictionary->get("CF").getDictionary()->get("StdCF").getDictionary()->get("CFM").getString(), QByteArray("AESV3"));

    const QByteArray U = H::getString(encryption, "U");
    const QByteArray UE = H::getString(encryption, "UE");
    const QByteArray O = H::getString(encryption, "O");
    const QByteArray OE = H::getString(encryption, "OE");
    const QByteArray Perms = H::getString(encryption, "Perms");
    QCOMPARE(U.size(), qsizetype(48));
    QCOMPARE(O.size(), qsizetype(48));
    QCOMPARE(UE.size(), qsizetype(32));
    QCOMPARE(OE.size(), qsizetype(32));
    QCOMPARE(Perms.size(), qsizetype(16));

    // SASLprep of these passwords is the identity (no mapping), UTF-8
    const QByteArray userBytes = user.toUtf8();
    const QByteArray ownerBytes = owner.toUtf8();

    // Algorithm 8: U = hash(user, validation salt) + salts, UE = AES-256(hash(user, key salt), key)
    QCOMPARE(U.left(32).toHex(), Iso::hash2B(userBytes, U.mid(32, 8), QByteArray()).toHex());
    const QByteArray userFileKey = Iso::aes(EVP_aes_256_cbc(), Iso::hash2B(userBytes, U.mid(40, 8), QByteArray()), QByteArray(16, '\0'), UE, false, false);

    // Algorithm 9: the same with the owner password and the 48 bytes of U
    QCOMPARE(O.left(32).toHex(), Iso::hash2B(ownerBytes, O.mid(32, 8), U).toHex());
    const QByteArray ownerFileKey = Iso::aes(EVP_aes_256_cbc(), Iso::hash2B(ownerBytes, O.mid(40, 8), U), QByteArray(16, '\0'), OE, false, false);
    QCOMPARE(userFileKey.size(), qsizetype(32));
    QCOMPARE(userFileKey.toHex(), ownerFileKey.toHex());

    // Algorithm 10
    const QByteArray perms = Iso::aes(EVP_aes_256_ecb(), userFileKey, QByteArray(), Perms, false, false);
    QCOMPARE(perms.left(4).toHex(), Iso::littleEndian(H::getP(encryption)).toHex());
    QCOMPARE(perms.mid(4, 4), QByteArray(4, '\xFF'));
    QCOMPARE(perms.at(8), 'T');
    QCOMPARE(perms.mid(9, 3), QByteArray("adb"));

    // A string of the handler decrypts by the file key (AESV3: the key itself, 7.6.3.3)
    const QByteArray plain("Secret text of revision 6");
    const QByteArray encrypted = handler->encrypt(plain, pdf::PDFObjectReference(5, 0), pdf::PDFSecurityHandler::EncryptionScope::String);
    QCOMPARE(Iso::aes(EVP_aes_256_cbc(), userFileKey, encrypted.left(16), encrypted.mid(16), false, true), plain);
}

void SecurityHandlerTest::revision5And6Reader_data()
{
    QTest::addColumn<int>("revision");
    QTest::addColumn<bool>("encryptMetadata");

    QTest::newRow("r6") << 6 << true;
    QTest::newRow("r6-metadata-plain") << 6 << false;
    QTest::newRow("r5-adobe-extension-3") << 5 << true;
}

void SecurityHandlerTest::revision5And6Reader()
{
    // Files written here by Algorithms 8 - 10 (revision 6) or by SHA-256 of the extension level 3
    // of Adobe (revision 5: read, never written) open by our reader; /Perms is checked
    // (Algorithm 13): a manipulated /P refuses the file
    QFETCH(int, revision);
    QFETCH(bool, encryptMetadata);

    const QByteArray user("user-6");
    const QByteArray owner("owner-6");
    const QByteArray fileKey = QByteArray("0123456789abcdef0123456789ABCDEF");
    const uint32_t P = 0xFFFFF0C0u | 0x0004u | 0x0200u;
    const Iso::Revision6 values = Iso::createRevision6(user, owner, fileKey, P, encryptMetadata, revision);

    auto createFile = [&](uint32_t writtenP)
    {
        const QByteArray encrypt = "<< /Filter /Standard /V 5 /R " + QByteArray::number(revision) + " /Length 256 /CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> >>"
                                   " /StmF /StdCF /StrF /StdCF /O " + H::hex(values.O) + " /U " + H::hex(values.U) + " /OE " + H::hex(values.OE) + " /UE " + H::hex(values.UE) +
                                   " /Perms " + H::hex(values.Perms) + " /P " + QByteArray::number(int32_t(writtenP)) + " /EncryptMetadata " + (encryptMetadata ? "true" : "false") + " >>";
        MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
        pdf.set(12, "<< /Text " + H::hex(Iso::encrypt(fileKey, 12, "Plain text of revision 6", "AESV3")) + " >>");
        pdf.set(13, encrypt);
        pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
        return pdf.write();
    };

    const QByteArray bytes = createFile(P);
    const H::ReadResult asUser = H::read(bytes, QString::fromLatin1(user));
    QCOMPARE(asUser.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(asUser.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::UserAuthorized);
    QCOMPARE(H::object(asUser.document, 12).getDictionary()->get("Text").getString(), QByteArray("Plain text of revision 6"));
    QCOMPARE(asUser.document.getStorage().getSecurityHandler()->isMetadataEncrypted(), encryptMetadata);

    const H::ReadResult asOwner = H::read(bytes, QString::fromLatin1(owner));
    QCOMPARE(asOwner.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(asOwner.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::OwnerAuthorized);

    QCOMPARE(H::read(bytes, QStringLiteral("wrong")).result, pdf::PDFDocumentReader::Result::Canceled);

    // Algorithm 13: /P differs from /Perms - the file is refused
    const H::ReadResult manipulated = H::read(createFile(P | 0x0008u), QString::fromLatin1(user));
    QCOMPARE(manipulated.result, pdf::PDFDocumentReader::Result::Failed);
}

void SecurityHandlerTest::emptyOwnerPassword_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("rc4") << int(Factory::RC4);
    QTest::newRow("aes128") << int(Factory::AES_128);
    QTest::newRow("aes256") << int(Factory::AES_256);
}

void SecurityHandlerTest::emptyOwnerPassword()
{
    // B81, R5-2: an empty owner password is the user password (Algorithm 3 a for revision 4, the
    // same for revision 6) - the file never opens without a password (the reader tries the empty
    // one first), the user password opens it as its owner. Also the old overload (Editor).
    QFETCH(int, algorithm);

    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::Algorithm(algorithm), QStringLiteral("tajne"), QString()));
    QVERIFY(handler);

    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());
    const H::ReadResult withoutPassword = H::read(bytes, QString());
    QCOMPARE(withoutPassword.result, pdf::PDFDocumentReader::Result::Canceled);
    QCOMPARE(withoutPassword.callbackCount, 2);

    const H::ReadResult withPassword = H::read(bytes, QStringLiteral("tajne"));
    QCOMPARE(withPassword.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(withPassword.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::OwnerAuthorized);
    QCOMPARE(H::streamData(withPassword.document, 101), H::streamData(H::createPlain().read(), 101));

    if (algorithm != Factory::AES_256)
    {
        // Algorithm 3 a: /O of the user password as the owner password
        const pdf::PDFObject encryption = H::encryptionDictionary(handler);
        QCOMPARE(H::getString(encryption, "O").toHex(), Iso::ownerValue("tajne", "tajne", 4).toHex());
    }
}

void SecurityHandlerTest::restrictionsNeedOwnerPassword()
{
    // B81, R5-23, Q5.1 (7.6.4.1): restrictions without an owner password of their own would be
    // void - the factory with the reason refuses them; without restrictions an empty owner is fine
    const uint32_t printOnly = uint32_t(Permission::PrintLowResolution);
    for (const Factory::Algorithm algorithm : { Factory::RC4, Factory::AES_128, Factory::AES_256 })
    {
        QString errorMessage;
        QVERIFY(!Factory::createSecurityHandler(H::settings(algorithm, QStringLiteral("Canary-7f3a"), QString(), printOnly), &errorMessage));
        QVERIFY(!errorMessage.isEmpty());
        QVERIFY(!errorMessage.contains(QStringLiteral("Canary-7f3a")));

        errorMessage.clear();
        QVERIFY(!Factory::validate(H::settings(algorithm, QStringLiteral("same"), QStringLiteral("same"), printOnly), &errorMessage));
        QVERIFY(!errorMessage.isEmpty());
        QVERIFY(!Factory::validate(H::settings(algorithm, QStringLiteral("same"), QStringLiteral("same"), printOnly), nullptr));

        QVERIFY(Factory::validate(H::settings(algorithm, QStringLiteral("open"), QStringLiteral("owner"), printOnly), nullptr));
        QVERIFY(Factory::createSecurityHandler(H::settings(algorithm, QStringLiteral("open"), QString(), H::allPermissions()), &errorMessage));
    }

    // Revision 4 uses the first 32 bytes: passwords equal there are the same password
    const QString longPassword(40, QChar('x'));
    QVERIFY(!Factory::validate(H::settings(Factory::AES_128, longPassword, longPassword.left(32) + QStringLiteral("different"), printOnly), nullptr));
}

void SecurityHandlerTest::permissionBits()
{
    // B89 (PDF 1.7 Table 3.20, ISO 32000-2 7.6.4.2 NOTE, Table 22): bits 1, 2 cleared, bits 7, 8,
    // 10 and 13 - 32 set, the other bits as given
    for (const uint32_t given : { 0u, uint32_t(Permission::PrintLowResolution), H::allPermissions(), 0xFFFFFFFFu, 0x00000003u })
    {
        const uint32_t P = Factory::getStandardPermissions(given);
        QCOMPARE(P & 0x3u, 0u);
        QCOMPARE(P & 0xC0u, 0xC0u);
        QCOMPARE(P & 0x200u, 0x200u);
        QCOMPARE(P & 0xFFFFF000u, 0xFFFFF000u);
        QCOMPARE(P & 0xD3Cu, given & 0xD3Cu);
    }

    // The value written by the factory, also in /Perms of revision 6 (Algorithm 10)
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, QString(), QStringLiteral("owner"), 0));
    QVERIFY(handler);
    QCOMPARE(H::getP(H::encryptionDictionary(handler)), 0xFFFFF0C0u | 0x200u);

    // Table 24 (B90, R5-15, Q5.3 b): bit 10 always, bit 2 only with all seven permissions
    const uint32_t restricted = Factory::getPublicKeyPermissions(uint32_t(Permission::PrintLowResolution));
    QCOMPARE(restricted, 0x0004u | 0x0200u);
    const uint32_t everything = Factory::getPublicKeyPermissions(H::allPermissions() & ~uint32_t(Permission::Accessibility));
    QCOMPARE(everything, 0x0002u | 0x0F3Cu);
    QCOMPARE(Factory::getPublicKeyPermissions(0xFFFFFFFFu), 0x0002u | 0x0F3Cu);

    // R5-14: a reader treats bit 10 as set (Table 22 and 24) - a standard handler with bit 10
    // cleared allows the extraction for the accessibility to its user
    const QByteArray user("u");
    const QByteArray owner("o");
    const uint32_t P = 0xFFFFF0C0u | 0x0004u;
    const QByteArray O = Iso::ownerValue(owner, user, 4);
    const QByteArray key = Iso::fileKey(user, O, P, H::id(), 4, 16);
    const pdf::PDFObject encryption = H::parse("<< /Filter /Standard /V 2 /R 3 /Length 128 /O " + H::hex(O) + " /U " + H::hex(Iso::userValue(key, H::id(), 3) + QByteArray(16, '\0')) +
                                               " /P " + QByteArray::number(int32_t(P)) + " >>");
    pdf::PDFSecurityHandlerPointer standard = pdf::PDFSecurityHandler::createSecurityHandler(encryption, H::id());
    bool first = true;
    QCOMPARE(standard->authenticate([&first](bool* ok) { *ok = first; first = false; return QStringLiteral("u"); }, false), AuthorizationResult::UserAuthorized);
    QVERIFY(standard->isAllowed(Permission::Accessibility));
    QVERIFY(standard->isAllowed(Permission::PrintLowResolution));
    QVERIFY(!standard->isAllowed(Permission::CopyContent));
}

void SecurityHandlerTest::randomnessOfHandlers()
{
    // B82, S5.4: keys, salts, the random bytes of /Perms, the padding of /U and every IV come from
    // the cryptographic generator - two handlers of the same settings differ in all of them, the
    // same string encrypted twice has two IVs
    std::set<QByteArray> userValues;
    std::set<QByteArray> userKeys;
    std::set<QByteArray> ownerValues;
    std::set<QByteArray> perms;
    for (int i = 0; i < 100; ++i)
    {
        const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, QStringLiteral("u"), QStringLiteral("o"), 0));
        QVERIFY(handler);
        const pdf::PDFObject encryption = H::encryptionDictionary(handler);
        userValues.insert(H::getString(encryption, "U"));
        userKeys.insert(H::getString(encryption, "UE"));
        ownerValues.insert(H::getString(encryption, "O"));
        perms.insert(H::getString(encryption, "Perms"));
    }
    QCOMPARE(userValues.size(), size_t(100));
    QCOMPARE(userKeys.size(), size_t(100));
    QCOMPARE(ownerValues.size(), size_t(100));
    QCOMPARE(perms.size(), size_t(100));

    // Revision 4: the 16 arbitrary bytes of /U (Algorithm 5 step e)
    const pdf::PDFSecurityHandlerPointer first = Factory::createSecurityHandler(H::settings(Factory::AES_128, QStringLiteral("u"), QStringLiteral("o"), 0));
    const pdf::PDFSecurityHandlerPointer second = Factory::createSecurityHandler(H::settings(Factory::AES_128, QStringLiteral("u"), QStringLiteral("o"), 0));
    QCOMPARE(H::getString(H::encryptionDictionary(first), "U").left(16), H::getString(H::encryptionDictionary(second), "U").left(16));
    QVERIFY(H::getString(H::encryptionDictionary(first), "U").mid(16) != H::getString(H::encryptionDictionary(second), "U").mid(16));

    const QByteArray one = first->encrypt("same text", pdf::PDFObjectReference(3, 0), pdf::PDFSecurityHandler::EncryptionScope::String);
    const QByteArray two = first->encrypt("same text", pdf::PDFObjectReference(3, 0), pdf::PDFSecurityHandler::EncryptionScope::String);
    QVERIFY(one.left(16) != two.left(16));
}

void SecurityHandlerTest::roundTrip_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<int>("scope");

    for (const auto& [algorithm, name] : { std::pair<Factory::Algorithm, const char*>{ Factory::RC4, "rc4" }, { Factory::AES_128, "aes128" }, { Factory::AES_256, "aes256" } })
    {
        QTest::newRow(qPrintable(QString::fromLatin1(name) + "-all")) << int(algorithm) << int(Factory::All);
        QTest::newRow(qPrintable(QString::fromLatin1(name) + "-except-metadata")) << int(algorithm) << int(Factory::AllExceptMetadata);
        QTest::newRow(qPrintable(QString::fromLatin1(name) + "-attachments")) << int(algorithm) << int(Factory::EmbeddedFiles);
    }
}

void SecurityHandlerTest::roundTrip()
{
    // The handler of the factory encrypts the test document, our reader opens it by the user and
    // the owner password. The bytes of the file (7.6.2, 7.6.6, Table 27): the scope decides what is
    // readable; the attachment is never readable; /Contents of a signature dictionary without
    // /Type (B88) is never encrypted.
    QFETCH(int, algorithm);
    QFETCH(int, scope);

    Factory::SecuritySettings settings = H::settings(Factory::Algorithm(algorithm), QStringLiteral("user-pass"), QStringLiteral("owner-pass"), uint32_t(Permission::PrintLowResolution));
    settings.encryptContents = Factory::EncryptContents(scope);
    QString errorMessage;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));

    const MergeTestPdf plain = H::createPlain();
    const pdf::PDFDocument plainDocument = plain.read();
    const QByteArray bytes = H::encryptDocument(plain, handler.data());

    const bool isAll = scope == Factory::All;
    const bool isAttachmentsOnly = scope == Factory::EmbeddedFiles;
    QCOMPARE(bytes.contains("CONTENT-MARKER"), isAttachmentsOnly);
    QCOMPARE(bytes.contains("TITLE-MARKER"), isAttachmentsOnly);
    QCOMPARE(bytes.contains("METADATA-MARKER"), !isAll);
    QVERIFY(!bytes.contains("ATTACHMENT-MARKER"));
    QVERIFY(bytes.contains("(SIGNATURE-MARKER)"));

    // A stream of its own crypt filter Identity is not encrypted by /StmF (7.6.6, B87)
    QVERIFY(bytes.contains("CRYPT-IDENTITY-MARKER"));

    for (const QString& password : { QStringLiteral("user-pass"), QStringLiteral("owner-pass") })
    {
        const H::ReadResult read = H::read(bytes, password);
        QCOMPARE(read.result, pdf::PDFDocumentReader::Result::OK);
        QCOMPARE(read.isEmbeddedFilesOnlyEncryption, isAttachmentsOnly);
        const pdf::PDFSecurityHandler* readHandler = read.document.getStorage().getSecurityHandler();
        QCOMPARE(readHandler->getAuthorizationResult(), password.startsWith(QStringLiteral("owner")) ? AuthorizationResult::OwnerAuthorized : AuthorizationResult::UserAuthorized);
        QCOMPARE(readHandler->isMetadataEncrypted(), isAll);

        for (const int number : { 6, 7, 11, 101 })
        {
            QCOMPARE(H::streamData(read.document, number), H::streamData(plainDocument, number));
        }
        QCOMPARE(H::object(read.document, 8).getDictionary()->get("Title").getString(), QByteArray("TITLE-MARKER"));
        QCOMPARE(H::object(read.document, 9).getDictionary()->get("Contents").getString(), QByteArray("SIGNATURE-MARKER"));

        // The user has the permissions of /P, the owner every one (7.6.4.2)
        QVERIFY(readHandler->isAllowed(Permission::PrintLowResolution));
        QCOMPARE(readHandler->isAllowed(Permission::CopyContent), password.startsWith(QStringLiteral("owner")));

        // 7.6.4.1, R5-4: only the owner changes the security
        QCOMPARE(readHandler->isEncryptionChangeAllowed(), password.startsWith(QStringLiteral("owner")));
    }
}

void SecurityHandlerTest::certificateRoundTrip_data()
{
    QTest::addColumn<int>("scope");
    QTest::newRow("all") << int(Factory::All);
    QTest::newRow("except-metadata") << int(Factory::AllExceptMetadata);
    QTest::newRow("attachments") << int(Factory::EmbeddedFiles);
}

void SecurityHandlerTest::certificateRoundTrip()
{
    // R5-1 (B102), R5-9 (B105), B84: the public-key handler (7.6.5, adbe.pkcs7.s5, AESV3) for a
    // PKCS#12 made by this test with a password with diacritics (UTF-8) - created by the factory,
    // opened by our reader with the certificate and its password, for the three scopes
    QFETCH(int, scope);

    Factory::SecuritySettings settings = H::settings(Factory::Certificate, H::pfxPassword(), QString(), H::allPermissions());
    settings.certificate = H::createEntry(m_pkcs12);
    settings.encryptContents = Factory::EncryptContents(scope);
    QString errorMessage;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));
    QCOMPARE(handler->getMode(), pdf::EncryptionMode::PublicKey);

    const pdf::PDFObject encryption = H::encryptionDictionary(handler);
    const pdf::PDFDictionary* dictionary = encryption.getDictionary();
    QCOMPARE(dictionary->get("Filter").getString(), QByteArray("Adobe.PubSec"));
    QCOMPARE(dictionary->get("SubFilter").getString(), QByteArray("adbe.pkcs7.s5"));
    QCOMPARE(dictionary->get("V").getInteger(), pdf::PDFInteger(5));

    const MergeTestPdf plain = H::createPlain();
    const pdf::PDFDocument plainDocument = plain.read();
    const QByteArray bytes = H::encryptDocument(plain, handler.data());
    QVERIFY(!bytes.contains("ATTACHMENT-MARKER"));
    QCOMPARE(bytes.contains("CONTENT-MARKER"), scope == Factory::EmbeddedFiles);

    const pdf::PDFCertificateEntries certificates = { H::createEntry(m_pkcs12) };
    const H::ReadResult read = H::read(bytes, H::pfxPassword(), false, &certificates);
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    QCOMPARE(read.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::UserAuthorized);
    for (const int number : { 6, 7, 101 })
    {
        QCOMPARE(H::streamData(read.document, number), H::streamData(plainDocument, number));
    }
    QCOMPARE(H::object(read.document, 8).getDictionary()->get("Title").getString(), QByteArray("TITLE-MARKER"));

    // A wrong password of the PKCS#12 does not open it (the callback is asked once more)
    const H::ReadResult wrong = H::read(bytes, QStringLiteral("wrong"), false, &certificates);
    QCOMPARE(wrong.result, pdf::PDFDocumentReader::Result::Canceled);
    QCOMPARE(wrong.callbackCount, 2);

    // No certificate of the user: the reader fails, it never reads the file as not encrypted
    const pdf::PDFCertificateEntries none;
    QVERIFY(H::read(bytes, H::pfxPassword(), false, &none).result != pdf::PDFDocumentReader::Result::OK);
}

void SecurityHandlerTest::certificatePermissions()
{
    // Table 24 in the envelope (7.6.5.3, Q5.3 b): "Printing only" has no bit 2 and restricts the
    // recipient; every permission sets bit 2 (all operations). R5-24: /P of the dictionary is not
    // protected - a changed /P never widens the permissions of the envelope.
    const pdf::PDFCertificateEntries certificates = { H::createEntry(m_pkcs12) };
    for (const bool isEverything : { false, true })
    {
        Factory::SecuritySettings settings = H::settings(Factory::Certificate, H::pfxPassword(), QString(), isEverything ? H::allPermissions() : uint32_t(Permission::PrintLowResolution));
        settings.certificate = H::createEntry(m_pkcs12);
        const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, nullptr);
        QVERIFY(handler);
        QCOMPARE(H::getP(H::encryptionDictionary(handler)), Factory::getPublicKeyPermissions(settings.permissions));

        QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());

        // A hostile /P allowing everything (bit 2) in the dictionary (the same length: the
        // offsets of the cross-reference table stay)
        const QByteArray writtenP = "/P " + QByteArray::number(int32_t(H::getP(H::encryptionDictionary(handler))));
        QVERIFY(bytes.contains(writtenP));
        bytes.replace(writtenP, QByteArray("/P -1").leftJustified(writtenP.size(), ' '));

        const H::ReadResult read = H::read(bytes, H::pfxPassword(), false, &certificates);
        QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
        const pdf::PDFSecurityHandler* readHandler = read.document.getStorage().getSecurityHandler();
        QVERIFY(readHandler->isAllowed(Permission::PrintLowResolution));
        QCOMPARE(readHandler->isAllowed(Permission::Modify), isEverything);
        QCOMPARE(readHandler->isAllowed(Permission::CopyContent), isEverything);
        QVERIFY(readHandler->isAllowed(Permission::Accessibility));

        // Q5.3 b, R5-4: only bit 2 of the envelope lets the recipient change or remove the
        // encryption - never the /P -1 of the dictionary
        QCOMPARE(readHandler->isEncryptionChangeAllowed(), isEverything);

        // Not authorized: nothing
        QVERIFY(!pdf::PDFSecurityHandler::createSecurityHandler(H::parse(H::toText(H::encryptionDictionary(handler))), H::id())->isEncryptionChangeAllowed());
    }
}

void SecurityHandlerTest::certificateEnvelope_data()
{
    QTest::addColumn<bool>("hasPermissions");
    QTest::addColumn<bool>("encryptMetadata");
    QTest::newRow("seed-and-permissions") << true << true;
    QTest::newRow("seed-and-permissions-metadata-plain") << true << false;
    QTest::newRow("seed-only") << false << true;
}

void SecurityHandlerTest::certificateEnvelope()
{
    // 7.6.5.3 written here: the recipient is PKCS#7 enveloped data of the 20 bytes of the seed and
    // the 4 bytes of the permissions; the file key is SHA-256 of the seed, the bytes of every
    // recipient and 0xFFFFFFFF when the metadata are not encrypted. Our reader opens it by the
    // PKCS#12. An envelope without the permissions allows nothing (R5-24) - /P -1 is not protected.
    QFETCH(bool, hasPermissions);
    QFETCH(bool, encryptMetadata);

    const QByteArray passwordUtf8 = H::pfxPassword().toUtf8();
    const unsigned char* pkcs12Data = reinterpret_cast<const unsigned char*>(m_pkcs12.constData());
    std::unique_ptr<PKCS12, void(*)(PKCS12*)> pkcs12(d2i_PKCS12(nullptr, &pkcs12Data, long(m_pkcs12.size())), PKCS12_free);
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;
    QVERIFY(PKCS12_parse(pkcs12.get(), passwordUtf8.constData(), &key, &certificate, nullptr) == 1);
    std::unique_ptr<EVP_PKEY, void(*)(EVP_PKEY*)> keyGuard(key, EVP_PKEY_free);
    std::unique_ptr<X509, void(*)(X509*)> certificateGuard(certificate, X509_free);

    const QByteArray seed("SEED-of-twenty-bytes");
    QCOMPARE(seed.size(), qsizetype(20));
    const uint32_t permissions = 0x0004u | 0x0010u;
    const QByteArray envelope = hasPermissions ? seed + Iso::littleEndian(permissions) : seed;

    std::unique_ptr<BIO, void(*)(BIO*)> data(BIO_new_mem_buf(envelope.constData(), int(envelope.size())), BIO_free_all);
    STACK_OF(X509)* recipients = sk_X509_new_null();
    sk_X509_push(recipients, certificate);
    std::unique_ptr<PKCS7, void(*)(PKCS7*)> pkcs7(PKCS7_encrypt(recipients, data.get(), EVP_aes_256_cbc(), PKCS7_BINARY), PKCS7_free);
    sk_X509_free(recipients);
    QVERIFY(pkcs7);
    unsigned char* der = nullptr;
    const int derLength = i2d_PKCS7(pkcs7.get(), &der);
    const QByteArray recipient(reinterpret_cast<const char*>(der), derLength);
    OPENSSL_free(der);

    const QByteArray fileKey = Iso::sha256(seed + recipient + (encryptMetadata ? QByteArray() : QByteArray(4, '\xFF')));
    MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
    pdf.set(12, "<< /Text " + H::hex(Iso::encrypt(fileKey, 12, "Plain text of the recipient", "AESV3")) + " >>");
    pdf.set(13, "<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s5 /V 5 /CF << /DefaultCryptFilter << /CFM /AESV3 /Recipients [" + H::hex(recipient) +
                "] /EncryptMetadata " + (encryptMetadata ? "true" : "false") + " >> >> /StmF /DefaultCryptFilter /StrF /DefaultCryptFilter /P -1 >>");
    pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");

    const pdf::PDFCertificateEntries certificates = { H::createEntry(m_pkcs12) };
    const H::ReadResult read = H::read(pdf.write(), H::pfxPassword(), false, &certificates);
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    QCOMPARE(H::object(read.document, 12).getDictionary()->get("Text").getString(), QByteArray("Plain text of the recipient"));

    const pdf::PDFSecurityHandler* handler = read.document.getStorage().getSecurityHandler();
    QCOMPARE(handler->isAllowed(Permission::PrintLowResolution), hasPermissions);
    QCOMPARE(handler->isAllowed(Permission::CopyContent), hasPermissions);
    QVERIFY(!handler->isAllowed(Permission::Modify));
    QVERIFY(!handler->isAllowed(Permission::Assemble));

    // /P -1 has bit 2, the envelope has not (or has no permissions at all, R5-24)
    QVERIFY(!handler->isEncryptionChangeAllowed());
}

void SecurityHandlerTest::certificateFailures()
{
    // B101, B94, B96: every failure of the factory has its reason, which never holds the password
    Factory::SecuritySettings settings = H::settings(Factory::Certificate, H::pfxPassword(), QString(), H::allPermissions());

    // A certificate of the system store: no PKCS#12 (Q5.4)
    QString errorMessage;
    QVERIFY(!Factory::createSecurityHandler(settings, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(!Factory::validate(settings, nullptr));

    // A wrong password
    settings.certificate = H::createEntry(m_pkcs12);
    settings.userPassword = QStringLiteral("wrong-pfx-password");
    errorMessage.clear();
    QVERIFY(!Factory::createSecurityHandler(settings, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(!errorMessage.contains(QStringLiteral("wrong-pfx-password")));

    // Not a PKCS#12
    settings.certificate = H::createEntry(QByteArray("not a pkcs12 file"));
    settings.userPassword = H::pfxPassword();
    QVERIFY(!Factory::createSecurityHandler(settings, nullptr));

    // The old overload: no handler (B101: PdfTool and the tool check it)
    QVERIFY(!Factory::createSecurityHandler(settings));
}

void SecurityHandlerTest::embeddedFilesOnly_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("rc4") << int(Factory::RC4);
    QTest::newRow("aes128") << int(Factory::AES_128);
    QTest::newRow("aes256") << int(Factory::AES_256);
}

void SecurityHandlerTest::embeddedFilesOnly()
{
    // Owner answer Q5.9 c, R5-8: File attachments only (7.6.6: /StmF and /StrF Identity, /EFF with
    // /AuthEvent /EFOpen) opens by Open without attachments without the password: the document is
    // readable, the attachment stays encrypted, nothing can be written. Without the choice the
    // password is asked; the reader says what the file is also after a canceled reading.
    QFETCH(int, algorithm);

    Factory::SecuritySettings settings = H::settings(Factory::Algorithm(algorithm), QStringLiteral("attach"), QStringLiteral("owner"), H::allPermissions());
    settings.encryptContents = Factory::EmbeddedFiles;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, nullptr);
    QVERIFY(handler);
    QVERIFY(handler->isEmbeddedFilesOnlyEncryption());
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());
    const pdf::PDFDocument plainDocument = H::createPlain().read();

    const H::ReadResult canceled = H::read(bytes, QStringLiteral("wrong"));
    QCOMPARE(canceled.result, pdf::PDFDocumentReader::Result::Canceled);
    QVERIFY(canceled.isEmbeddedFilesOnlyEncryption);

    const H::ReadResult locked = H::read(bytes, QStringLiteral("never-asked"), false, nullptr, true);
    QCOMPARE(locked.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(locked.callbackCount, 0);
    const pdf::PDFSecurityHandler* lockedHandler = locked.document.getStorage().getSecurityHandler();
    QVERIFY(lockedHandler->isEmbeddedFilesLocked());
    QCOMPARE(lockedHandler->getAuthorizationResult(), AuthorizationResult::UserAuthorized);
    QVERIFY(!lockedHandler->isEncryptionAllowed());
    QVERIFY(!lockedHandler->isEncryptionChangeAllowed());
    QCOMPARE(H::streamData(locked.document, 101), H::streamData(plainDocument, 101));
    QCOMPARE(H::object(locked.document, 8).getDictionary()->get("Title").getString(), QByteArray("TITLE-MARKER"));
    QVERIFY(H::streamData(locked.document, 7) != H::streamData(plainDocument, 7));
    QVERIFY(!H::streamData(locked.document, 7).contains("ATTACHMENT-MARKER"));

    // No writer writes it (tools.md 4.7: the document is locked)
    QByteArray written;
    QBuffer buffer(&written);
    buffer.open(QBuffer::WriteOnly);
    QVERIFY(!pdf::PDFDocumentWriter(nullptr).write(&buffer, &locked.document));

    // The password opens it whole
    const H::ReadResult opened = H::read(bytes, QStringLiteral("attach"));
    QCOMPARE(opened.result, pdf::PDFDocumentReader::Result::OK);
    QVERIFY(!opened.document.getStorage().getSecurityHandler()->isEmbeddedFilesLocked());
    QCOMPARE(H::streamData(opened.document, 7), H::streamData(plainDocument, 7));

    // Another encryption is never opened without its password by the choice
    Factory::SecuritySettings everything = settings;
    everything.encryptContents = Factory::All;
    const QByteArray allBytes = H::encryptDocument(H::createPlain(), Factory::createSecurityHandler(everything, nullptr).data());
    const H::ReadResult notLocked = H::read(allBytes, QStringLiteral("wrong"), false, nullptr, true);
    QCOMPARE(notLocked.result, pdf::PDFDocumentReader::Result::Canceled);
    QVERIFY(!notLocked.isEmbeddedFilesOnlyEncryption);
}

void SecurityHandlerTest::embeddedFilesLockedNoOtherKey()
{
    // Review RS5SEC: a file of File attachments only (7.6.6: /StmF, /StrF Identity, /EFF with
    // /EFOpen) may name another crypt filter (/AuthEvent /DocOpen) in the /Crypt step of a stream
    // (7.4.10). Opened without the password there is no file encryption key at all: the step is
    // kept and its decoding fails loudly - never data decrypted by an empty key. The password
    // decrypts it (Algorithms 1, 2, 3, 5 of revision 4, the key computed here).
    const QByteArray user("u");
    const QByteArray owner("o");
    const uint32_t P = 0xFFFFFFFCu;
    const QByteArray O = Iso::ownerValue(owner, user, 4);
    const QByteArray key = Iso::fileKey(user, O, P, H::id(), 4, 16);
    const QByteArray U = Iso::userValue(key, H::id(), 4) + QByteArray(16, '\0');

    MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
    pdf.set(12, MergeTestPdf::stream(Iso::encrypt(key, 12, "DOCUMENT-FILTER-MARKER", "V2"), "/Filter /Crypt /DecodeParms << /Name /DocCF >>"));
    pdf.set(13, "<< /Filter /Standard /V 4 /R 4 /Length 128 /CF << /EmbCF << /CFM /AESV2 /AuthEvent /EFOpen /Length 16 >> /DocCF << /CFM /V2 /AuthEvent /DocOpen /Length 16 >> >>"
                " /StmF /Identity /StrF /Identity /EFF /EmbCF /O " + H::hex(O) + " /U " + H::hex(U) + " /P " + QByteArray::number(int32_t(P)) + " >>");
    pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
    const QByteArray bytes = pdf.write();

    const H::ReadResult locked = H::read(bytes, QStringLiteral("never-asked"), false, nullptr, true);
    QVERIFY2(locked.result == pdf::PDFDocumentReader::Result::OK, qPrintable(locked.errorMessage));
    QVERIFY(locked.document.getStorage().getSecurityHandler()->isEmbeddedFilesLocked());
    const pdf::PDFStream* kept = H::object(locked.document, 12).getStream();
    QVERIFY(kept->getDictionary()->get("Filter").isName());
    QCOMPARE(kept->getDictionary()->get("Filter").getString(), QByteArray("Crypt"));
    QCOMPARE(*kept->getContent(), Iso::encrypt(key, 12, "DOCUMENT-FILTER-MARKER", "V2"));
    QVERIFY(H::isThrown([&]() { locked.document.getDecodedStream(kept); }));

    const H::ReadResult opened = H::read(bytes, QString::fromLatin1(user));
    QVERIFY2(opened.result == pdf::PDFDocumentReader::Result::OK, qPrintable(opened.errorMessage));
    const pdf::PDFStream* decrypted = H::object(opened.document, 12).getStream();
    QCOMPARE(*decrypted->getContent(), QByteArray("DOCUMENT-FILTER-MARKER"));
    QVERIFY(!decrypted->getDictionary()->hasKey("Filter"));
}

void SecurityHandlerTest::cryptFilterStreams()
{
    // B87, R5-10 (B106), 7.4.10, 7.6.6: a stream with its own crypt filter is recognized by /Filter
    // (not by a key "Crypt"); the reader decrypts its step and removes it, so a writer never meets
    // a crypt filter name its new /Encrypt does not know. A stream of /StdCF in a filter array
    // keeps its other filters; an unknown name keeps the step and fails only at the decoding.
    const QByteArray user("u");
    const QByteArray fileKey = QByteArray("0123456789abcdef0123456789ABCDEF");
    const uint32_t P = 0xFFFFFFFCu;
    const Iso::Revision6 values = Iso::createRevision6(user, "owner", fileKey, P, true, 6);

    const QByteArray deflated = qCompress(QByteArray("FLATE-CRYPT-MARKER")).mid(4);
    MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
    pdf.set(11, MergeTestPdf::stream("IDENTITY-MARKER", "/Type /Metadata /Subtype /XML /Filter /Crypt /DecodeParms << /Name /Identity >>"));
    pdf.set(12, MergeTestPdf::stream(Iso::encrypt(fileKey, 12, deflated, "AESV3"), "/Filter [/Crypt /FlateDecode] /DecodeParms [<< /Name /StdCF >> null]"));
    pdf.set(13, MergeTestPdf::stream(Iso::encrypt(fileKey, 13, "UNKNOWN-MARKER", "AESV3"), "/Filter /Crypt /DecodeParms << /Name /Foreign >>"));
    // Table 25, /CFM /None (here no /CFM): the security handler decrypts by its own method, AESV3 of
    // revision 6 (review RS5SEC: formerly an empty stream, an assertion in a debug build)
    pdf.set(15, MergeTestPdf::stream(Iso::encrypt(fileKey, 15, "NONE-METHOD-MARKER", "AESV3"), "/Filter /Crypt /DecodeParms << /Name /HandlerCF >>"));
    pdf.set(14, "<< /Filter /Standard /V 5 /R 6 /Length 256 /CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> /HandlerCF << /AuthEvent /DocOpen >> >> /StmF /StdCF /StrF /StdCF /O " + H::hex(values.O) +
                " /U " + H::hex(values.U) + " /OE " + H::hex(values.OE) + " /UE " + H::hex(values.UE) + " /Perms " + H::hex(values.Perms) + " /P " + QByteArray::number(int32_t(P)) + " >>");
    pdf.setTrailerEntries("/Encrypt 14 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");

    const H::ReadResult read = H::read(pdf.write(), QString::fromLatin1(user));
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));

    // Identity: the plain data, no /Filter any more
    const pdf::PDFStream* identity = H::object(read.document, 11).getStream();
    QCOMPARE(*identity->getContent(), QByteArray("IDENTITY-MARKER"));
    QVERIFY(!identity->getDictionary()->hasKey("Filter"));
    QVERIFY(!identity->getDictionary()->hasKey("DecodeParms"));

    // /StdCF, then /FlateDecode: the step is gone, Flate stays
    const pdf::PDFStream* flate = H::object(read.document, 12).getStream();
    const pdf::PDFObject& filter = flate->getDictionary()->get("Filter");
    QVERIFY(filter.isArray());
    QCOMPARE(filter.getArray()->getCount(), size_t(1));
    QCOMPARE(filter.getArray()->getItem(0).getString(), QByteArray("FlateDecode"));
    QCOMPARE(read.document.getDecodedStream(flate), QByteArray("FLATE-CRYPT-MARKER"));

    // The method None: decrypted by the method of the handler, the step is gone
    QVERIFY(H::object(read.document, 15).isStream());
    const pdf::PDFStream* handlerMethod = H::object(read.document, 15).getStream();
    QCOMPARE(*handlerMethod->getContent(), QByteArray("NONE-METHOD-MARKER"));
    QVERIFY(!handlerMethod->getDictionary()->hasKey("Filter"));

    // An unknown crypt filter: kept, the decoding fails loudly (never garbage)
    const pdf::PDFStream* unknown = H::object(read.document, 13).getStream();
    QCOMPARE(unknown->getDictionary()->get("Filter").getString(), QByteArray("Crypt"));
    QVERIFY(H::isThrown([&]() { read.document.getDecodedStream(unknown); }));

    // Written again by a handler (its visitor): no /Crypt of the decrypted streams, the kept one
    // keeps its data and loses the private reference
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, QStringLiteral("x"), QStringLiteral("y")));
    const pdf::PDFObject rewritten = handler->encryptObject(H::object(read.document, 11), pdf::PDFObjectReference(11, 0));
    QVERIFY(!rewritten.getStream()->getDictionary()->hasKey("Filter"));
    const pdf::PDFObject rewrittenUnknown = handler->encryptObject(H::object(read.document, 13), pdf::PDFObjectReference(13, 0));
    QCOMPARE(*rewrittenUnknown.getStream()->getContent(), *unknown->getContent());
    QVERIFY(!H::toText(rewrittenUnknown).contains(pdf::PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME));
}

void SecurityHandlerTest::externalFileStream()
{
    // B108, 7.6.2: the data of a stream in an external file (/F, 7.3.8.2) are not a part of the
    // file and are never encrypted nor decrypted; its dictionary is (the strings of /F are)
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, QStringLiteral("u"), QStringLiteral("o")));
    QVERIFY(handler);

    pdf::PDFDictionaryBuilder dictionary;
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("F"), pdf::PDFObject::createString(QByteArray("external.dat")));
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(0));
    const pdf::PDFObject stream = pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray("EXTERNAL-DATA")));

    const pdf::PDFObject encrypted = handler->encryptObject(stream, pdf::PDFObjectReference(20, 0));
    QCOMPARE(*encrypted.getStream()->getContent(), QByteArray("EXTERNAL-DATA"));
    QVERIFY(encrypted.getStream()->getDictionary()->get("F").getString() != QByteArray("external.dat"));

    const pdf::PDFObject decrypted = handler->decryptObject(encrypted, pdf::PDFObjectReference(20, 0));
    QCOMPARE(*decrypted.getStream()->getContent(), QByteArray("EXTERNAL-DATA"));
    QCOMPARE(decrypted.getStream()->getDictionary()->get("F").getString(), QByteArray("external.dat"));
}

void SecurityHandlerTest::documentMetadataOnly_data()
{
    QTest::addColumn<int>("revision");
    QTest::addColumn<bool>("isCatalogInObjectStream");
    QTest::newRow("r4-catalog-object") << 4 << false;
    QTest::newRow("r4-catalog-in-object-stream") << 4 << true;
    QTest::newRow("r6-catalog-object") << 6 << false;
    QTest::newRow("r6-catalog-in-object-stream") << 6 << true;
}

void SecurityHandlerTest::documentMetadataOnly()
{
    // Owner 2026-11-15, ISO 32000-2 Table 20: /EncryptMetadata false leaves only the document-level
    // metadata stream (the /Metadata of the catalog, 14.3.2) unencrypted; the XMP of a page is
    // encrypted as any stream - a file of another writer (H::createMetadataFile). A catalog in an
    // object stream is decrypted only after the streams: then every metadata stream is taken as
    // unencrypted (the rule of the readers before). An incremental update by PDFDocumentWriter,
    // which keeps the catalog, keeps the rule of the file it updates.
    QFETCH(int, revision);
    QFETCH(bool, isCatalogInObjectStream);

    const QByteArray pageContent = H::PAGE_CONTENT;
    const QByteArray pageXmp = H::PAGE_XMP;
    const QByteArray bytes = H::createMetadataFile(revision, isCatalogInObjectStream);
    QVERIFY(bytes.contains("DOCUMENT-XMP-MARKER"));
    QCOMPARE(bytes.contains(pageXmp), isCatalogInObjectStream);

    const H::ReadResult read = H::read(bytes, QString());
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    const pdf::PDFSecurityHandler* handler = read.document.getStorage().getSecurityHandler();
    QVERIFY(!handler->isMetadataEncrypted());
    QVERIFY(read.document.getCatalog()->getPageCount() == 1);
    QCOMPARE(H::streamData(read.document, 6), QByteArray("DOCUMENT-XMP-MARKER"));
    QCOMPARE(H::streamData(read.document, 12), pageXmp);
    QCOMPARE(H::streamData(read.document, 101), pageContent);
    QCOMPARE(handler->getDocumentMetadataReference().has_value(), !isCatalogInObjectStream);
    if (!isCatalogInObjectStream)
    {
        QVERIFY(*handler->getDocumentMetadataReference() == pdf::PDFObjectReference(6, 0));
    }

    // The update writes both metadata streams by the rule, which its reader uses for the file
    const QByteArray updated = H::updateIncrementally(bytes, { { 6, MergeTestPdf::stream("DOCUMENT-XMP-UPDATED", "/Type /Metadata /Subtype /XML") },
                                                               { 12, MergeTestPdf::stream("PAGE-XMP-UPDATED", "/Type /Metadata /Subtype /XML") } });
    QVERIFY(updated.startsWith(bytes));
    const QByteArray appended = updated.mid(bytes.size());
    QVERIFY(appended.contains("DOCUMENT-XMP-UPDATED"));
    QCOMPARE(appended.contains("PAGE-XMP-UPDATED"), isCatalogInObjectStream);

    const H::ReadResult readUpdated = H::read(updated, QString());
    QVERIFY2(readUpdated.result == pdf::PDFDocumentReader::Result::OK, qPrintable(readUpdated.errorMessage));
    QCOMPARE(H::streamData(readUpdated.document, 6), QByteArray("DOCUMENT-XMP-UPDATED"));
    QCOMPARE(H::streamData(readUpdated.document, 12), QByteArray("PAGE-XMP-UPDATED"));
    QCOMPARE(H::streamData(readUpdated.document, 101), pageContent);
}

void SecurityHandlerTest::documentMetadataCatalogUpdated_data()
{
    QTest::addColumn<int>("revision");
    QTest::addColumn<int>("root");
    QTest::newRow("r4-catalog-changed") << 4 << 1;
    QTest::newRow("r4-root-moved") << 4 << 50;
    QTest::newRow("r6-catalog-changed") << 6 << 1;
    QTest::newRow("r6-root-moved") << 6 << 50;
}

void SecurityHandlerTest::documentMetadataCatalogUpdated()
{
    // Review of the fix wave: the catalog of the file is in an object stream, so its reader took
    // every metadata stream as unencrypted (Table 20, the rule of /Type) and the XMP of the page is
    // in the clear. An incremental update, after which the reader knows the catalog (a changed
    // catalog is written as an object of its own; /Root moved to the second catalog of the object
    // stream - the update writes it too), must write the XMP of the page again, encrypted, or its
    // reader would decrypt the clear XMP to garbage. Only the XMP of the catalog stays in the clear.
    QFETCH(int, revision);
    QFETCH(int, root);

    const QByteArray bytes = H::createMetadataFile(revision, true);
    QVERIFY(bytes.contains(H::PAGE_XMP));

    std::map<int, QByteArray> changes;
    if (root == 1)
    {
        changes[1] = "<< /Type /Catalog /Pages 2 0 R /Metadata 6 0 R /Lang (en-US) >>";
    }
    const QByteArray updated = H::updateIncrementally(bytes, changes, root == 1 ? 0 : root);
    QVERIFY(updated.startsWith(bytes));

    const H::ReadResult read = H::read(updated, QString());
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    QCOMPARE(H::streamData(read.document, 6), QByteArray("DOCUMENT-XMP-MARKER"));
    QCOMPARE(H::streamData(read.document, 12), QByteArray(H::PAGE_XMP));
    QCOMPARE(H::streamData(read.document, 101), QByteArray(H::PAGE_CONTENT));
    QVERIFY(read.document.getStorage().getSecurityHandler()->getDocumentMetadataReference() == std::optional<pdf::PDFObjectReference>(pdf::PDFObjectReference(6, 0)));

    // The catalog and the XMP of the page are in the update, the XMP encrypted
    const QByteArray appended = updated.mid(bytes.size());
    QVERIFY(appended.contains(QByteArray::number(root) + " 0 obj"));
    QVERIFY(appended.contains("12 0 obj"));
    QVERIFY(!appended.contains(H::PAGE_XMP));
}

void SecurityHandlerTest::documentMetadataWritten_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("rc4") << int(Factory::RC4);
    QTest::newRow("aes128") << int(Factory::AES_128);
    QTest::newRow("aes256") << int(Factory::AES_256);
}

void SecurityHandlerTest::documentMetadataWritten()
{
    // Owner 2026-11-15, Table 20: Everything except metadata (/EncryptMetadata false) written by
    // PDFDocumentWriter - the XMP of the catalog in the clear, the XMP of a page and the content
    // encrypted; our reader decides the same and reads every stream back
    QFETCH(int, algorithm);

    MergeTestPdf plain = MergeTestPdf::createPages("CONTENT-MARKER-", 1, "/Metadata 6 0 R", { { 1, "/Metadata 12 0 R" } });
    plain.set(6, MergeTestPdf::stream("DOCUMENT-XMP-MARKER", "/Type /Metadata /Subtype /XML"));
    plain.set(12, MergeTestPdf::stream("PAGE-XMP-MARKER", "/Type /Metadata /Subtype /XML"));
    plain.setTrailerEntries("/ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
    const pdf::PDFDocument plainDocument = plain.read();

    Factory::SecuritySettings settings = H::settings(Factory::Algorithm(algorithm), QStringLiteral("user-w"), QStringLiteral("owner-w"), uint32_t(Permission::PrintLowResolution));
    settings.encryptContents = Factory::AllExceptMetadata;
    QString errorMessage;
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));

    pdf::PDFDocumentBuilder builder(&plainDocument);
    builder.setSecurityHandler(handler);
    const pdf::PDFDocument encrypted = builder.build();
    const QByteArray bytes = MergeTestPdf::writeDocument(encrypted);
    QVERIFY(bytes.contains("DOCUMENT-XMP-MARKER"));
    QVERIFY(!bytes.contains("PAGE-XMP-MARKER"));
    QVERIFY(!bytes.contains("CONTENT-MARKER"));

    // The handler of the document is not changed by the write (it is shared)
    QVERIFY(!encrypted.getStorage().getSecurityHandler()->getDocumentMetadataReference().has_value());

    const H::ReadResult read = H::read(bytes, QStringLiteral("user-w"));
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    QCOMPARE(H::streamData(read.document, 6), QByteArray("DOCUMENT-XMP-MARKER"));
    QCOMPARE(H::streamData(read.document, 12), QByteArray("PAGE-XMP-MARKER"));
    QCOMPARE(H::streamData(read.document, 101), H::streamData(plainDocument, 101));
}

void SecurityHandlerTest::hostileEncryptionDictionaries_data()
{
    QTest::addColumn<QByteArray>("encrypt");

    const QByteArray ou = " /O " + H::hex(QByteArray(32, 'o')) + " /U " + H::hex(QByteArray(32, 'u')) + " /P -4";
    QTest::newRow("not-a-dictionary") << QByteArray("[1 2 3]");
    QTest::newRow("unknown-handler") << QByteArray("<< /Filter /Unknown /V 1 /R 2" + ou + " >>");
    QTest::newRow("unknown-subfilter") << QByteArray("<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s9 /V 5 >>");
    QTest::newRow("negative-length") << QByteArray("<< /Filter /Standard /V 2 /R 3 /Length -8" + ou + " >>");
    QTest::newRow("huge-length") << QByteArray("<< /Filter /Standard /V 2 /R 3 /Length 4096" + ou + " >>");
    QTest::newRow("odd-length") << QByteArray("<< /Filter /Standard /V 2 /R 3 /Length 41" + ou + " >>");
    QTest::newRow("version-6") << QByteArray("<< /Filter /Standard /V 6 /R 6" + ou + " >>");
    QTest::newRow("revision-7") << QByteArray("<< /Filter /Standard /V 2 /R 7 /Length 128" + ou + " >>");
    QTest::newRow("revision-1") << QByteArray("<< /Filter /Standard /V 1 /R 1" + ou + " >>");
    QTest::newRow("short-o") << QByteArray("<< /Filter /Standard /V 2 /R 3 /Length 128 /O <00> /U " + H::hex(QByteArray(32, 'u')) + " /P -4 >>");
    QTest::newRow("missing-p") << QByteArray("<< /Filter /Standard /V 2 /R 3 /Length 128 /O " + H::hex(QByteArray(32, 'o')) + " /U " + H::hex(QByteArray(32, 'u')) + " >>");
    QTest::newRow("unknown-cfm") << QByteArray("<< /Filter /Standard /V 4 /R 4 /CF << /StdCF << /CFM /ROT13 >> >> /StmF /StdCF /StrF /StdCF" + ou + " >>");
    QTest::newRow("unknown-stmf") << QByteArray("<< /Filter /Standard /V 4 /R 4 /CF << /StdCF << /CFM /V2 >> >> /StmF /Missing /StrF /StdCF" + ou + " >>");
    QTest::newRow("aesv3-with-v4") << QByteArray("<< /Filter /Standard /V 4 /R 4 /CF << /StdCF << /CFM /AESV3 >> >> /StmF /StdCF /StrF /StdCF" + ou + " >>");
    QTest::newRow("cf-not-dictionary") << QByteArray("<< /Filter /Standard /V 4 /R 4 /CF << /StdCF 5 >> /StmF /StdCF" + ou + " >>");
}

void SecurityHandlerTest::hostileEncryptionDictionaries()
{
    // 7.2 of the plan: a damaged or hostile /Encrypt refuses the document with an error (B83:
    // a /Length out of 40 - 128 or not a multiple of 8, Table 20) - never an access out of a buffer,
    // never a reading as not encrypted (also by the permissive reader)
    QFETCH(QByteArray, encrypt);

    QVERIFY(H::isThrown([&]() { pdf::PDFSecurityHandler::createSecurityHandler(H::parse(encrypt), H::id()); }));

    MergeTestPdf pdf = MergeTestPdf::createPages("HOSTILE-", 1);
    pdf.set(13, encrypt);
    pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
    for (const bool permissive : { false, true })
    {
        const H::ReadResult read = H::read(pdf.write(), QStringLiteral("x"), false, nullptr, false, permissive);
        QCOMPARE(read.result, pdf::PDFDocumentReader::Result::Failed);
        QVERIFY(!read.errorMessage.isEmpty());
    }
}

void SecurityHandlerTest::hostileCryptFilterLength()
{
    // B83: /Length of a crypt filter is not used for the key of an object (Algorithm 1 uses the
    // length of the file encryption key): negative, huge or in bits, the file opens and decrypts
    const QByteArray user("u");
    const QByteArray owner("o");
    const uint32_t P = 0xFFFFFFFCu;
    const QByteArray O = Iso::ownerValue(owner, user, 4);
    const QByteArray key = Iso::fileKey(user, O, P, H::id(), 4, 16);
    const QByteArray U = Iso::userValue(key, H::id(), 4) + QByteArray(16, '\0');

    for (const QByteArray& length : { QByteArray("-100"), QByteArray("100000"), QByteArray("128"), QByteArray("16") })
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("P", 1);
        pdf.set(12, "<< /Text " + H::hex(Iso::encrypt(key, 12, "Plain RC4 text", "V2")) + " >>");
        pdf.set(13, "<< /Filter /Standard /V 4 /R 4 /Length 128 /CF << /StdCF << /CFM /V2 /Length " + length + " >> >> /StmF /StdCF /StrF /StdCF /O " + H::hex(O) +
                    " /U " + H::hex(U) + " /P " + QByteArray::number(int32_t(P)) + " >>");
        pdf.setTrailerEntries("/Encrypt 13 0 R /ID [" + H::hex(H::id()) + " " + H::hex(H::id()) + "]");
        const H::ReadResult read = H::read(pdf.write(), QStringLiteral("u"));
        QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
        QCOMPARE(H::object(read.document, 12).getDictionary()->get("Text").getString(), QByteArray("Plain RC4 text"));
    }
}

void SecurityHandlerTest::tooManyRecipients()
{
    // B95: at most 256 recipients and 64 KiB each - the opening never makes a PKCS#7 operation per
    // recipient of a hostile file
    QByteArray recipients;
    for (int i = 0; i < Factory::MAXIMUM_RECIPIENT_COUNT + 44; ++i)
    {
        recipients += "<3000> ";
    }
    QVERIFY(H::isThrown([&]() { pdf::PDFSecurityHandler::createSecurityHandler(H::parse("<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s5 /V 5 /CF << /DefaultCryptFilter << /CFM /AESV3 /Recipients [" + recipients + "] >> >> /StmF /DefaultCryptFilter /StrF /DefaultCryptFilter >>"), H::id()); }));
    QVERIFY(H::isThrown([&]() { pdf::PDFSecurityHandler::createSecurityHandler(H::parse("<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s4 /V 2 /Length 128 /Recipients [" + recipients + "] >>"), H::id()); }));

    const QByteArray huge = H::hex(QByteArray(Factory::MAXIMUM_RECIPIENT_SIZE + 1, char(0x30)));
    QVERIFY(H::isThrown([&]() { pdf::PDFSecurityHandler::createSecurityHandler(H::parse("<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s5 /V 5 /CF << /DefaultCryptFilter << /CFM /AESV3 /Recipients [" + huge + "] >> >> /StmF /DefaultCryptFilter /StrF /DefaultCryptFilter >>"), H::id()); }));

    // The limit itself is accepted
    QByteArray limit;
    for (int i = 0; i < Factory::MAXIMUM_RECIPIENT_COUNT; ++i)
    {
        limit += "<3000> ";
    }
    QVERIFY(!H::isThrown([&]() { pdf::PDFSecurityHandler::createSecurityHandler(H::parse("<< /Filter /Adobe.PubSec /SubFilter /adbe.pkcs7.s5 /V 5 /CF << /DefaultCryptFilter << /CFM /AESV3 /Recipients [" + limit + "] >> >> /StmF /DefaultCryptFilter /StrF /DefaultCryptFilter >>"), H::id()); }));
}

void SecurityHandlerTest::saslPrep()
{
    // B93, R5-17, RFC 4013: an encryption of revision 6 refuses the prohibited output (2.3) and a
    // mixed direction (2.4); the mapping of table B.1 (incl. U+2060, U+FE0F, U+FEFF) and of the
    // non-ASCII spaces (C.1.2) makes equal passwords (7.6.4.3.3)
    QString invalid;
    QVERIFY(Factory::isPasswordValid(QStringLiteral("Plain-password 1"), Factory::AES_256, &invalid));
    QVERIFY(invalid.isEmpty());

    QVERIFY(!Factory::isPasswordValid(QString::fromUtf8("a\x07" "b"), Factory::AES_256, &invalid));     // C.2.1
    QVERIFY(invalid.contains(QStringLiteral("U+0007")));
    QVERIFY(!Factory::isPasswordValid(QString(QChar(0xE000)), Factory::AES_256, nullptr));            // C.3
    QVERIFY(!Factory::isPasswordValid(QString::fromUtf8("a\xE2\x80\x8E" "b"), Factory::AES_256, nullptr)); // C.8 U+200E
    QVERIFY(!Factory::isPasswordValid(QString(QChar(0xFFFF)), Factory::AES_256, nullptr));            // C.4
    QVERIFY(!Factory::isPasswordValid(QString(QChar(0xD800)), Factory::AES_256, nullptr));            // C.5

    // RFC 3454 6: right-to-left text without left-to-right characters, starting and ending by it
    const QString alef(QChar(0x05D0));
    const QString bet(QChar(0x05D1));
    QVERIFY(Factory::isPasswordValid(alef + bet, Factory::AES_256, nullptr));
    QVERIFY(Factory::isPasswordValid(alef + QStringLiteral("1") + bet, Factory::AES_256, nullptr));
    QVERIFY(!Factory::isPasswordValid(alef + QStringLiteral("a") + bet, Factory::AES_256, nullptr));
    QVERIFY(!Factory::isPasswordValid(QStringLiteral("1") + alef, Factory::AES_256, nullptr));

    // validate refuses it with the reason
    QString errorMessage;
    QVERIFY(!Factory::validate(H::settings(Factory::AES_256, QString(QChar(0xE000)), QStringLiteral("owner")), &errorMessage));
    QVERIFY(!errorMessage.isEmpty());

    // The mapping: the soft hyphen, U+FE0F, U+FEFF and U+2060 are nothing, a no-break space is a space
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::AES_256, QString::fromUtf8("pass\xC2\xAD" "wo\xEF\xB8\x8Frd\xE2\x81\xA0\xEF\xBB\xBF" "\xC2\xA0" "x"), QStringLiteral("owner")));
    QVERIFY(handler);
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());
    const H::ReadResult read = H::read(bytes, QStringLiteral("password x"));
    QCOMPARE(read.result, pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(read.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::UserAuthorized);

    // 7.6.4.3.3: only the first 127 bytes of UTF-8 count
    const QString longPassword = QString(127, QChar('p')) + QStringLiteral("ignored tail");
    const pdf::PDFSecurityHandlerPointer longHandler = Factory::createSecurityHandler(H::settings(Factory::AES_256, longPassword, QStringLiteral("owner")));
    QVERIFY(longHandler);
    QCOMPARE(H::read(H::encryptDocument(H::createPlain(), longHandler.data()), QString(127, QChar('p'))).result, pdf::PDFDocumentReader::Result::OK);
}

void SecurityHandlerTest::passwordEncodingRevision4()
{
    // Algorithm 2 step a: the password of revision 4 is PDFDocEncoding (a character outside of it
    // is refused before the encryption) and only its first 32 bytes count
    QString invalid;
    QVERIFY(Factory::isPasswordValid(QString::fromUtf8("\xC5\xBE\xC5\xA1\xC3\xA9 \xE2\x82\xAC"), Factory::AES_128, &invalid));
    QVERIFY(!Factory::isPasswordValid(QString::fromUtf8("\xE4\xB8\xAD"), Factory::RC4, &invalid));
    QVERIFY(invalid.contains(QString::fromUtf8("\xE4\xB8\xAD")));

    const QString longPassword = QString(32, QChar('q')) + QStringLiteral("tail");
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::RC4, longPassword, QStringLiteral("owner")));
    QVERIFY(handler);
    QCOMPARE(H::read(H::encryptDocument(H::createPlain(), handler.data()), QString(32, QChar('q'))).result, pdf::PDFDocumentReader::Result::OK);
}

void SecurityHandlerTest::authenticatedHandlerReadBack_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("rc4") << int(Factory::RC4);
    QTest::newRow("aes128") << int(Factory::AES_128);
    QTest::newRow("aes256") << int(Factory::AES_256);
}

void SecurityHandlerTest::authenticatedHandlerReadBack()
{
    // R6-1 of step 6: the read back of a file with an open password (the incremental update, the signer,
    // the revisions of the verifier) by a clone of the authenticated handler of its document - never by a
    // password. Only a file of the same encryption dictionary and the same first part of /ID (Algorithm 2
    // of 7.6.4.3.2 takes the key from it) is opened so.
    QFETCH(int, algorithm);
    using Result = pdf::PDFDocumentReader::Result;

    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(H::settings(Factory::Algorithm(algorithm), QStringLiteral("user"), QStringLiteral("owner"), H::allPermissions()), nullptr);
    QVERIFY(handler);
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());
    const pdf::PDFDocument plainDocument = H::createPlain().read();

    // A handler of the factory knows the dictionary it writes and the /ID of its settings: the file it
    // wrote is read back by its clone (the check of a recipient of the system store, whose key is not used)
    QVERIFY(handler->isSameEncryption(handler.data()));
    const H::ReadResult writtenBack = H::readByHandler(bytes, handler.data(), false);
    QVERIFY2(writtenBack.result == Result::OK, qPrintable(writtenBack.errorMessage));
    QCOMPARE(writtenBack.callbackCount, 0);

    const H::ReadResult opened = H::read(bytes, QStringLiteral("user"));
    QCOMPARE(opened.result, Result::OK);
    const pdf::PDFSecurityHandler* authenticated = opened.document.getStorage().getSecurityHandler();
    QCOMPARE(authenticated->getAuthorizationResult(), AuthorizationResult::UserAuthorized);
    QVERIFY(authenticated->isSameEncryption(authenticated));
    QVERIFY(!authenticated->isSameEncryption(nullptr));

    // The clone opens the file: the callback is never asked, the objects are decrypted
    const H::ReadResult readBack = H::readByHandler(bytes, authenticated, false);
    QVERIFY2(readBack.result == Result::OK, qPrintable(readBack.errorMessage));
    QCOMPARE(readBack.callbackCount, 0);
    for (const int number : { 6, 101 })
    {
        QCOMPARE(H::streamData(readBack.document, number), H::streamData(plainDocument, number));
    }
    QCOMPARE(H::object(readBack.document, 8).getDictionary()->get("Title").getString(), QByteArray("TITLE-MARKER"));
    QCOMPARE(readBack.document.getStorage().getSecurityHandler()->getAuthorizationResult(), AuthorizationResult::UserAuthorized);

    // A reader of the owner only: a user authorization does not suffice, an owner one does
    QCOMPARE(H::readByHandler(bytes, authenticated, true).result, Result::Canceled);
    const H::ReadResult owner = H::read(bytes, QStringLiteral("owner"), true);
    QCOMPARE(owner.result, Result::OK);
    const H::ReadResult ownerReadBack = H::readByHandler(bytes, owner.document.getStorage().getSecurityHandler(), true);
    QCOMPARE(ownerReadBack.result, Result::OK);
    QCOMPARE(ownerReadBack.callbackCount, 0);

    // The same encryption dictionary with another first part of /ID: another file, never opened by the clone
    QByteArray otherId = bytes;
    QVERIFY(otherId.contains(H::hex(H::id())));
    otherId.replace(H::hex(H::id()), H::hex(QByteArray("FEDCBA9876543210")));
    const H::ReadResult other = H::readByHandler(otherId, authenticated, false);
    QCOMPARE(other.result, Result::Canceled);
    QCOMPARE(other.callbackCount, 1);

    // Another encryption dictionary (another password): the password is asked as without the handler
    const pdf::PDFSecurityHandlerPointer otherHandler = Factory::createSecurityHandler(H::settings(Factory::Algorithm(algorithm), QStringLiteral("other"), QStringLiteral("owner"), H::allPermissions()), nullptr);
    const H::ReadResult otherEncryption = H::readByHandler(H::encryptDocument(H::createPlain(), otherHandler.data()), authenticated, false);
    QCOMPARE(otherEncryption.result, Result::Canceled);
    QCOMPARE(otherEncryption.callbackCount, 1);
    QVERIFY(!otherHandler->isSameEncryption(authenticated));
    QVERIFY(!otherHandler->isSameEncryption(handler.data()));

    // A document without encryption is read as it is
    const H::ReadResult plain = H::readByHandler(H::createPlain().write(), authenticated, false);
    QCOMPARE(plain.result, Result::OK);
    QCOMPARE(plain.document.getStorage().getSecurityHandler()->getMode(), pdf::EncryptionMode::None);
}

void SecurityHandlerTest::authenticatedHandlerOfCertificate()
{
    // R6-1 of step 6 for the public-key handler (7.6.5): the clone keeps the key, the permissions of the
    // envelope and the certificate of the recipient; no certificate and no password is needed again
    Factory::SecuritySettings settings = H::settings(Factory::Certificate, H::pfxPassword(), QString(), uint32_t(Permission::PrintLowResolution));
    settings.certificate = H::createEntry(m_pkcs12);
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, nullptr);
    QVERIFY(handler);
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());

    const pdf::PDFCertificateEntries certificates = { H::createEntry(m_pkcs12) };
    const H::ReadResult opened = H::read(bytes, H::pfxPassword(), false, &certificates);
    QCOMPARE(opened.result, pdf::PDFDocumentReader::Result::OK);

    const H::ReadResult readBack = H::readByHandler(bytes, opened.document.getStorage().getSecurityHandler(), false);
    QVERIFY2(readBack.result == pdf::PDFDocumentReader::Result::OK, qPrintable(readBack.errorMessage));
    QCOMPARE(readBack.callbackCount, 0);
    QCOMPARE(H::streamData(readBack.document, 101), H::streamData(H::createPlain().read(), 101));
    const pdf::PDFSecurityHandler* readHandler = readBack.document.getStorage().getSecurityHandler();
    QVERIFY(readHandler->isAllowed(Permission::PrintLowResolution));
    QVERIFY(!readHandler->isAllowed(Permission::Modify));
    QCOMPARE(readHandler->getMode(), pdf::EncryptionMode::PublicKey);
    const pdf::PDFPublicKeySecurityHandler* publicKeyHandler = static_cast<const pdf::PDFPublicKeySecurityHandler*>(readHandler);
    QVERIFY(publicKeyHandler->getAuthorizedCertificate());
    QCOMPARE(publicKeyHandler->getAuthorizedCertificate()->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("PDF4QT Test Recipient"));

    // Without the handler the file is not opened (no certificate of the user is given)
    QVERIFY(H::readByHandler(bytes, nullptr, false).result != pdf::PDFDocumentReader::Result::OK);
}

void SecurityHandlerTest::certificateOfSystemStore()
{
#ifdef Q_OS_WIN
    // Q6.2 of step 6: a certificate of the store of Windows encrypts by its public certificate only and
    // opens the document by its key, which stays in the system (PDFPersonalCertificateStore::
    // decryptEnvelope). The store of the machine is never used: a store in the memory of the process.
    const QByteArray pfxPassword = QByteArrayLiteral("system-store-test");
    const QByteArray pkcs12 = H::createPkcs12(QString::fromLatin1(pfxPassword));
    std::shared_ptr<const pdf::PDFPersonalCertificateStore> store = pdf::PDFPersonalCertificateStore::createMemoryStore(pkcs12, pfxPassword);
    QVERIFY(store);
    struct StoreGuard
    {
        ~StoreGuard() { pdf::PDFPersonalCertificateStore::setStore(nullptr); }
    } storeGuard;

    // The encryption needs the public certificate only (the addendum of the owner of 15. 11.): the store
    // of the encryption has not its key at all (a store of another certificate)
    const QByteArray unrelatedPkcs12 = H::createPkcs12(QString::fromLatin1(pfxPassword));
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(unrelatedPkcs12, pfxPassword));

    const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateManager::readCertificate(pkcs12, QString::fromLatin1(pfxPassword));
    QVERIFY(info);
    pdf::PDFCertificateEntry systemEntry;
    systemEntry.type = pdf::PDFCertificateEntry::EntryType::System;
    systemEntry.info = *info;

    // The encryption: no PKCS#12, no password
    Factory::SecuritySettings settings = H::settings(Factory::Certificate, QString(), QString(), H::allPermissions());
    settings.certificate = systemEntry;
    QString errorMessage;
    QVERIFY2(Factory::validate(settings, &errorMessage), qPrintable(errorMessage));
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, &errorMessage);
    QVERIFY2(handler, qPrintable(errorMessage));
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());
    QVERIFY(!bytes.contains("CONTENT-MARKER"));

    // The opening by the key of the store: no password is asked
    pdf::PDFPersonalCertificateStore::setStore(store);
    const pdf::PDFCertificateEntries entries = { systemEntry };
    const H::ReadResult read = H::read(bytes, QStringLiteral("never-asked"), false, &entries);
    QVERIFY2(read.result == pdf::PDFDocumentReader::Result::OK, qPrintable(read.errorMessage));
    QCOMPARE(read.callbackCount, 0);
    QCOMPARE(H::streamData(read.document, 101), H::streamData(H::createPlain().read(), 101));
    QCOMPARE(read.document.getStorage().getSecurityHandler()->getMode(), pdf::EncryptionMode::PublicKey);
    const pdf::PDFPublicKeySecurityHandler* readHandler = static_cast<const pdf::PDFPublicKeySecurityHandler*>(read.document.getStorage().getSecurityHandler());
    QVERIFY(readHandler->getAuthorizedCertificate());
    QCOMPARE(readHandler->getAuthorizedCertificate()->getCertificateData(), info->getCertificateData());

    // Another certificate of the store is no recipient: not opened, and no password is asked (no .pfx)
    const QByteArray otherPkcs12 = H::createPkcs12(QString::fromLatin1(pfxPassword));
    pdf::PDFPersonalCertificateStore::setStore(pdf::PDFPersonalCertificateStore::createMemoryStore(otherPkcs12, pfxPassword));
    const std::optional<pdf::PDFCertificateInfo> otherInfo = pdf::PDFCertificateManager::readCertificate(otherPkcs12, QString::fromLatin1(pfxPassword));
    QVERIFY(otherInfo);
    pdf::PDFCertificateEntry otherEntry = systemEntry;
    otherEntry.info = *otherInfo;
    const pdf::PDFCertificateEntries otherEntries = { otherEntry };
    const H::ReadResult other = H::read(bytes, QStringLiteral("never-asked"), false, &otherEntries);
    QVERIFY(other.result != pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(other.callbackCount, 0);

    // The key of the right certificate, which the store has not, opens nothing either
    const H::ReadResult missing = H::read(bytes, QStringLiteral("never-asked"), false, &entries);
    QVERIFY(missing.result != pdf::PDFDocumentReader::Result::OK);
#else
    QSKIP("The certificate store of the system exists only on Windows (Q6.2).");
#endif
}

void SecurityHandlerTest::certificateIterationLimit()
{
    // 6.3, R6-28 of step 6: a PKCS#12 of more than MAXIMUM_PKCS12_ITERATIONS iterations of its MAC is
    // refused before its password is tried - the opening and the encryption never block the thread
    Factory::SecuritySettings settings = H::settings(Factory::Certificate, H::pfxPassword(), QString(), H::allPermissions());
    settings.certificate = H::createEntry(m_pkcs12);
    const pdf::PDFSecurityHandlerPointer handler = Factory::createSecurityHandler(settings, nullptr);
    QVERIFY(handler);
    const QByteArray bytes = H::encryptDocument(H::createPlain(), handler.data());

    // The limit itself is checked by PDFCertificateManager (UnitTestsTimestampClient pkcs12IterationLimit);
    // five times the limit here: tens of millions of iterations would take seconds for each tried password
    const QByteArray hostile = H::setMacIterations(m_pkcs12, pdf::PDFCertificateManager::MAXIMUM_PKCS12_ITERATIONS * 5);
    QVERIFY(!hostile.isEmpty() && hostile != m_pkcs12);
    const int timeoutFactor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFCertificateEntries certificates = { H::createEntry(hostile) };
    const H::ReadResult read = H::read(bytes, H::pfxPassword(), false, &certificates);
    QVERIFY(read.result != pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(read.callbackCount, 2);

    settings.certificate = H::createEntry(hostile);
    QVERIFY(!Factory::createSecurityHandler(settings, nullptr));
    QVERIFY2(timer.elapsed() < 1500 * timeoutFactor, qPrintable(QString::number(timer.elapsed())));
}

void SecurityHandlerTest::poolTasksReleaseOpenSslState_data()
{
    QTest::addColumn<bool>("isLoop");

    QTest::newRow("startTask") << false;
    QTest::newRow("execute") << true;
}

void SecurityHandlerTest::poolTasksReleaseOpenSslState()
{
    // ASan C of step 6: a task of a pool (a run of a tool, a parallel decryption) never leaves the
    // state of OpenSSL in its thread - the thread can outlive the cleanup of OpenSSL at the exit.
    // Each task finds the error queue of its thread empty and leaves an error in it; a thread runs
    // more tasks (more tasks than threads), so a state left by a task would be seen by the next one.
    QFETCH(bool, isLoop);

    // The test sees the state of the OpenSSL of the core only, when both use one library (the DLL
    // of Windows); a static OpenSSL linked to the core and to the test again has two states
    ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);
    QVERIFY(ERR_peek_error() != 0);
    pdf::PDFSignatureHandler::releaseThreadResources();
    if (ERR_peek_error() != 0)
    {
        ERR_clear_error();
        QSKIP("The test and the core use two copies of OpenSSL (static linking): the state of the core is not visible.");
    }

    struct Observation
    {
        Qt::HANDLE thread = nullptr;
        unsigned long leftError = 0;
        bool isRaised = false;
    };

    QMutex mutex;
    std::vector<Observation> observations;
    auto task = [&]()
    {
        Observation observation;
        observation.thread = QThread::currentThreadId();
        observation.leftError = ERR_peek_error();
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);
        observation.isRaised = ERR_peek_error() != 0;

        QMutexLocker locker(&mutex);
        observations.push_back(observation);
    };

    int count = 0;
    if (isLoop)
    {
        // The scope Page: one item in one task of the primary pool, which has at most
        // QThread::idealThreadCount() threads
        QVERIFY(pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));
        count = 2 * qMax(pdf::PDFExecutionPolicy::getMaxThreadCount(pdf::PDFExecutionPolicy::Scope::Page), QThread::idealThreadCount()) + 1;
        std::vector<int> items(count, 0);
        pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, items.cbegin(), items.cend(), [&](int) { task(); });
    }
    else
    {
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        count = 5;
        for (int i = 0; i < count; ++i)
        {
            pdf::PDFExecutionPolicy::startTask(&pool, task);
        }
        pool.waitForDone();
    }

    QMutexLocker locker(&mutex);
    QCOMPARE(int(observations.size()), count);
    std::set<Qt::HANDLE> threads;
    for (const Observation& observation : observations)
    {
        QVERIFY(observation.isRaised);
        QCOMPARE(observation.leftError, 0UL);
        threads.insert(observation.thread);
    }

    // The precondition of the check: some thread has run more tasks
    QVERIFY(int(threads.size()) < count);
}

QTEST_GUILESS_MAIN(SecurityHandlerTest)

#include "tst_securityhandlertest.moc"
