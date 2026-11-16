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

#ifndef PDFSECURITYHANDLER_H
#define PDFSECURITYHANDLER_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfcertificatestore.h"

#include <QByteArray>
#include <QSharedPointer>
#include <QString>

#include <map>
#include <functional>
#include <optional>

namespace pdf
{
class PDFObjectFactory;

enum class EncryptionMode
{
    None,       ///< Document is not encrypted
    Standard,   ///< Document is encrypted and using standard security handler
    PublicKey,  ///< Document is encrypted and using public key security handler
    Custom      ///< Document is encrypted and using custom security handler. Custom handlers must return this value.
};

enum class CryptFilterType
{
    None,       ///< The application shall decrypt the data using the security handler
    V2,         ///< Use file encryption key for RC4 algorithm
    AESV2,      ///< Use file encryption key for AES algorithm
    AESV3,      ///< Use file encryption key for AES 256 bit algorithm
    Identity,   ///< Don't decrypt anything, use identity function
};

enum class AuthEvent
{
    DocOpen,    ///< Authorize on document open
    EFOpen      ///< Authorize when accessing embedded file stream
};

enum class CryptFilterApplication
{
    String,         ///< Apply filter to decrypt/encrypt strings
    Stream,         ///< Apply filter to decrypt/encrypt streams
    EmbeddedFile    ///< Apply filter to decrypt/encrypt embedded file streams
};

struct CryptFilter
{
    bool operator ==(const CryptFilter&) const = default;
    bool operator !=(const CryptFilter&) const = default;

    CryptFilterType type = CryptFilterType::None;
    AuthEvent authEvent = AuthEvent::DocOpen;
    int keyLength = 0; ///< Key length in bytes
    QByteArrayList recipients; ///< Recipients for public key security handler
    bool encryptMetadata = true; ///< Encrypt metadata (for public key encryption)
};

class PDFSecurityHandler;
using PDFSecurityHandlerPointer = QSharedPointer<PDFSecurityHandler>;

class PDFStandardSecurityHandler;

/// Overwriting of secrets in the memory (plan of step 5 of phase 2, 6.1, S5.1): a password, a key
/// or a PKCS#12 buffer, which is no longer needed, is overwritten by zeros by OPENSSL_cleanse, which
/// the optimizer never removes (a plain memset before a free may be removed). It is a best effort:
/// a QString of a widget or a copy, which another owner holds, is not reached. Thread-safe.
class PDF4QTLIBCORESHARED_EXPORT PDFSecureMemory
{
public:
    PDFSecureMemory() = delete;

    /// Overwrites \p size bytes at \p data by zeros (nothing for null or zero size)
    static void clear(void* data, size_t size);

    /// Overwrites the bytes of \p data by zeros; its size stays (the caller releases it). A buffer
    /// shared with another QByteArray (implicit sharing) or raw data not owned by \p data
    /// (QByteArray::fromRawData) is not written - it belongs to the other owner, which clears it
    static void clear(QByteArray& data);

    /// Overwrites the characters of \p text by zeros; its size stays. A shared buffer is not
    /// written, as for a QByteArray
    static void clear(QString& text);
};

class PDF4QTLIBCORESHARED_EXPORT PDFSecurityHandler
{
public:
    explicit PDFSecurityHandler() = default;
    virtual ~PDFSecurityHandler() = default;

    static constexpr const char* IDENTITY_FILTER_NAME = "Identity";
    static constexpr const char* OBJECT_REFERENCE_DICTIONARY_NAME = "Pdf4Qt_ObjectReference";

    enum class Permission : uint32_t
    {
        PrintLowResolution       = (1 <<  2),
        Modify                   = (1 <<  3),
        CopyContent              = (1 <<  4),
        ModifyInteractiveItems   = (1 <<  5),
        ModifyFormFields         = (1 <<  8),
        Accessibility            = (1 <<  9),
        Assemble                 = (1 << 10),
        PrintHighResolution      = (1 << 11)
    };

    enum class AuthorizationResult
    {
        NoAuthorizationRequired,
        UserAuthorized,
        OwnerAuthorized,
        Failed,
        Canceled
    };

    enum class EncryptionScope
    {
        String,
        Stream,
        EmbeddedFile
    };

    /// Retrieve encryption mode (none/standard encryption/custom)
    virtual EncryptionMode getMode() const = 0;

    /// Creates a clone of this object
    virtual PDFSecurityHandler* clone() const = 0;

    /// Performs authentication of the document content access. First, algorithm should check,
    /// if empty password allows document access (so, for example, only owner password is provided).
    /// If this fails, function \p getPasswordCallback is called to retrieve user entered password.
    /// This callback function also has pointer to bool parameter, which sets to false, if user wants
    /// to cancel the authentication (and \p Canceled authentication result is returned) or true,
    /// to try provided password.
    /// \param getPasswordCallback Callback to get user password
    /// \param authorizeOwnerOnly Authorize owner only
    /// \returns Result of authentication
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>& getPasswordCallback, bool authorizeOwnerOnly) = 0;

    /// Authenticates as authenticate does, a public-key handler by \p certificates instead of the
    /// certificates of the user (PDFCertificateManager::getCertificates): the check of the factory
    /// by the chosen certificate (R5-1), a reader which knows the certificate (the read back of a
    /// tool, PDFDocumentReader::setCertificates). The password of the callback is the password of
    /// the PKCS#12 (UTF-8, B84). A standard handler ignores \p certificates.
    AuthorizationResult authenticateByCertificates(const PDFCertificateEntries& certificates, const std::function<QString(bool*)>& getPasswordCallback, bool authorizeOwnerOnly);

    /// Opens a handler of the encryption of the embedded files only (isEmbeddedFilesOnlyEncryption)
    /// without its password - Open without attachments (owner answer Q5.9 c, R5-8): the result is
    /// UserAuthorized, the strings and streams are readable (crypt filter Identity), the embedded
    /// files stay encrypted (isEmbeddedFilesLocked) and nothing can be written by this handler.
    /// Another encryption returns Failed and stays unauthorized.
    AuthorizationResult authenticateWithoutEmbeddedFiles();

    /// The handler and \p other are of one encryption (plan of step 6 of phase 2, R6-1): equal
    /// encryption dictionaries and the same first part of /ID of the trailer (the file encryption key
    /// of the standard handler depends on it, ISO 32000-2, 7.6.4.3, Algorithm 2), so the authorization
    /// of one opens the other - a reader of the bytes of a document, which has been opened already,
    /// takes a clone of its authenticated handler instead of its password (PDFDocumentReader::
    /// setAuthenticatedSecurityHandler). A handler read from a file knows the dictionary of the file
    /// (createSecurityHandler), a handler of the factory the dictionary it writes and the /ID of its
    /// settings (a file written by it is read back by its clone - the check of a recipient of the
    /// system store, whose key is not used). False for a document without encryption and for a null \p other.
    bool isSameEncryption(const PDFSecurityHandler* other) const;

    /// Decrypts the PDF object. This function works properly only (and only if)
    /// \p authenticate function returns user/owner authorization code.
    /// \param object Object to be decrypted
    /// \param reference Reference of indirect object (some algorithms require to generate key also from reference)
    /// \returns Decrypted object
    PDFObject decryptObject(const PDFObject& object, PDFObjectReference reference) const;

    /// Encrypts the PDF object. This function works properly only (and only if)
    /// \p authenticate function returns user/owner authorization code.
    /// \param object Object to be encrypted
    /// \param reference Reference of indirect object (some algorithms require to generate key also from reference)
    /// \returns Encrypted object
    PDFObject encryptObject(const PDFObject& object, PDFObjectReference reference) const;

    /// Decrypts the PDF object data. This function works properly only (and only if)
    /// \p authenticate function returns user/owner authorization code.
    /// \param data Data to be decrypted
    /// \param reference Reference of indirect object (some algorithms require to generate key also from reference)
    /// \param encryptionScope Scope of the encryption (if it is string/stream/...)
    /// \returns Decrypted object data
    virtual QByteArray decrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const = 0;

    /// Decrypts data using specified filter. Throws exception, if filter is not found.
    /// \param data Data to be decrypted
    /// \param filterName Filter name to be used to decrypt the data
    /// \param reference Reference object
    virtual QByteArray decryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const = 0;

    /// Encrypts the PDF object data. This function works properly only (and only if)
    /// \p authenticate function returns user/owner authorization code.
    /// \param data Data to be encrypted
    /// \param reference Reference of indirect object (some algorithms require to generate key also from reference)
    /// \param encryptionScope Scope of the encryption (if it is string/stream/...)
    /// \returns Encrypted object data
    virtual QByteArray encrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const = 0;

    /// Encrypts data using specified filter. Throws exception, if filter is not found.
    /// \param data Data to be encrypted
    /// \param filterName Filter name to be used to encrypt the data
    /// \param reference Reference object
    virtual QByteArray encryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const = 0;

    /// Returns true, if given permission is allowed in the current authorization context.
    /// If owner is authorized, then this function allways returns true.
    virtual bool isAllowed(Permission permission) const = 0;

    /// The permission by the table of the revision of the standard security handler (ISO
    /// 32000-2, 7.6.4.2, Table 22): bits 9 to 12 exist from revision 3, a handler of revision
    /// 2 answers them by its bits 6, 5, 4 and 3 (getRevision2Permission). The one rule of the
    /// new tools (the catalog, the merge, the new application; SECURITY_TODO 31) - isAllowed
    /// keeps the literal bits of the old applications. A null handler allows everything (a
    /// document without encryption).
    static bool isAllowedByRevision(const PDFSecurityHandler* securityHandler, Permission permission);

    /// The security of the opened document may be changed or removed (Remove Password, a new
    /// encryption; owner answer Q5.3 b, R5-4): no encryption - true; the standard handler - only
    /// opened as the owner (7.6.4.1); the public-key handler - bit 2 of the permissions of the
    /// recipient (Table 24) read from the decrypted envelope of the authorized recipient, never
    /// from the unprotected /P of the dictionary (R5-24: an envelope without the permission bytes
    /// allows nothing). False for an unauthorized handler and for a document opened without its
    /// attachments (Q5.9 c).
    bool isEncryptionChangeAllowed() const;

    /// The bit of revision 2, which governs the permission (Table 22): filling of forms by
    /// bit 6, the extraction for accessibility by bit 5, the assembly by bit 4 and the
    /// high-quality print by bit 3; the other permissions are their own bits
    static Permission getRevision2Permission(Permission permission);

    /// The encryption covers only the embedded files (ISO 32000-2, 7.6.6: /StmF and /StrF are
    /// Identity, /EFF has /AuthEvent /EFOpen) - File attachments only of the tools of step 5 of
    /// phase 2; the document opens without a password by Open without attachments (owner answer
    /// Q5.9 c, R5-8, PDFDocumentReader::setOpenWithoutAttachments).
    bool isEmbeddedFilesOnlyEncryption() const;

    /// The handler was opened without the password of the embedded files (Open without
    /// attachments, authenticateWithoutEmbeddedFiles): the document is readable, its embedded
    /// files stay encrypted and are never decrypted (a decryption by their crypt filter throws);
    /// isEncryptionAllowed is false, so no writer writes the document (tools.md 4.7); the
    /// Attachments panel shows "Locked – enter the password".
    bool isEmbeddedFilesLocked() const { return m_isEmbeddedFilesLocked; }

    /// The stream is an embedded file stream, which the crypt filter /EFF encrypts (ISO 32000-2,
    /// 7.6.6, Table 44: /Type /EmbeddedFile). /Type is optional in Table 44, so the tool, which
    /// encrypts only the attachments, marks every stream of /EF and /RF by /Type first (R5-3,
    /// B104) - this test is the one rule of the reader, the writer and that walk.
    static bool isEmbeddedFileStream(const PDFDictionary* streamDictionary);

    /// The dictionary is a signature dictionary, whose /Contents is never encrypted (ISO 32000-2,
    /// 7.6.2, Table 255, B88): /Type /Sig or /DocTimeStamp, or without /Type (it is optional) a
    /// dictionary with the required /Filter (name), /ByteRange (array) and /Contents (string)
    static bool isSignatureDictionary(const PDFDictionary* dictionary);

    /// The first filter of the stream is /Crypt (ISO 32000-2, 7.4.10: the Crypt filter is the
    /// first one of /Filter; B87, B106): the stream is decrypted by the crypt filter named by its
    /// /DecodeParms (/Name, default Identity), not by /StmF. The reader decrypts this step at the
    /// opening and removes it from the stream (the whole document is decrypted at the opening);
    /// a stream keeps it only, when its crypt filter cannot decrypt it then (an unknown name, a
    /// locked embedded file)
    static bool isCryptFilterStream(const PDFDictionary* streamDictionary);

    /// Returns true, if metadata are encrypted
    virtual bool isMetadataEncrypted() const = 0;

    /// The document-level metadata stream (the /Metadata of the catalog, 14.3.2) - the only stream,
    /// which /EncryptMetadata false leaves unencrypted (ISO 32000-2, Table 20; owner 2026-11-15).
    /// std::nullopt: the catalog is not known (a reader meets a catalog in an object stream only
    /// after the decryption) - then every stream of /Type /Metadata stays unencrypted, as readers
    /// did before. An invalid reference: the catalog has no metadata stream. The reader sets it of
    /// the file before it decrypts, the writer of the file it writes (on its own copy of the
    /// handler), so both decide the same; a handler nobody told keeps the rule of /Type.
    void setDocumentMetadataReference(std::optional<PDFObjectReference> reference) { m_documentMetadataReference = reference; }
    std::optional<PDFObjectReference> getDocumentMetadataReference() const { return m_documentMetadataReference; }

    /// The document-level metadata stream of \p catalog, the object of /Root of the trailer as
    /// the file stores it: its /Metadata reference (an invalid reference without one); std::nullopt,
    /// when \p catalog is no dictionary (not known yet, in an object stream, or damaged)
    static std::optional<PDFObjectReference> findDocumentMetadataReference(const PDFObject& catalog);

    /// The stream \p reference stays unencrypted because of /EncryptMetadata false: a stream of
    /// /Type /Metadata (Table 315), which is the document-level metadata stream - or any one of
    /// them, when that stream is not known (getDocumentMetadataReference)
    bool isUnencryptedMetadataStream(PDFObjectReference reference, const PDFDictionary* streamDictionary) const;

    /// Returns true, if encryption is allowed (and document
    /// can be encrypted, if it was decrypted)
    virtual bool isEncryptionAllowed() const = 0;

    /// Returns result of authorization process
    virtual AuthorizationResult getAuthorizationResult() const = 0;

    /// Creates encryption dictionary object
    virtual PDFObject createEncryptionDictionaryObject() const = 0;

    /// Returns version of the encryption
    int getVersion() const { return m_V; }

    /// Creates a security handler from the object. If object is null, then
    /// "None" security handler is created. If error occurs, then exception is thrown.
    /// \param encryptionDictionaryObject Encryption dictionary object
    /// \param id First part of the id of the document
    static PDFSecurityHandlerPointer createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id);

    /// Disable DRM mode of documents.
    static void setNoDRMMode();

    /// Returns true, if no DRM mode is enabled
    static bool isNoDRM();

protected:
    friend class PDFSecurityHandlerFactory;
    friend class PDFDecryptOrEncryptObjectVisitor;

    /// The crypt filter \p filterName exists and can decrypt now (a locked embedded file cannot)
    bool canDecryptByFilter(const QByteArray& filterName) const;

    /// The crypt filter of the decryption of the embedded files is locked (Open without attachments)
    bool isLockedFilter(const CryptFilter& filter) const;

    static bool parseBool(const PDFDictionary* dictionary, const char* key, bool required, bool defaultValue = true);
    static QByteArray parseName(const PDFDictionary* dictionary, const char* key, bool required, const char* defaultValue = nullptr);
    static PDFInteger parseInt(const PDFDictionary* dictionary, const char* key, bool required, PDFInteger defaultValue = -1);
    static CryptFilter parseCryptFilter(PDFInteger length, const PDFObject& object, bool publicKey);
    static PDFSecurityHandlerPointer createSecurityHandlerInstance(const PDFDictionary* dictionary);
    static QByteArrayList parseRecipients(const PDFDictionary* dictionary);

    static void parseCryptFilters(const PDFDictionary* dictionary, PDFSecurityHandler& handler, int Length, bool publicKey);
    static void parseDataStandardSecurityHandler(const PDFDictionary* dictionary, const QByteArray& id, int Length, PDFStandardSecurityHandler& handler);

    /// Fills encryption dictionary with basic data
    /// \param factory Factory
    void fillEncryptionDictionary(PDFObjectFactory& factory, bool publicKeyHandler) const;

    /// Version of the encryption, shall be a number from 1 to 5, according the
    /// PDF specification. Other values are invalid.
    int m_V = 0;

    /// Length of the key to encrypt/decrypt the document in bits.
    int m_keyLength = 40;

    /// Map containing crypt filters.
    std::map<QByteArray, CryptFilter> m_cryptFilters;

    /// Default filter
    CryptFilter m_filterDefault;

    /// Crypt filter for decrypting strings
    CryptFilter m_filterStrings;

    /// Crypt filter for decrypting streams
    CryptFilter m_filterStreams;

    /// Crypt filter for decrypting embedded files
    CryptFilter m_filterEmbeddedFiles;

    /// Opened without the password of the embedded files (authenticateWithoutEmbeddedFiles)
    bool m_isEmbeddedFilesLocked = false;

    /// The document-level metadata stream (setDocumentMetadataReference), std::nullopt = not known
    std::optional<PDFObjectReference> m_documentMetadataReference;

    /// The encryption dictionary of the file, of which createSecurityHandler has created the handler,
    /// and the first part of /ID of its trailer (isSameEncryption); of a handler of the factory the
    /// dictionary it writes and the /ID of its settings
    PDFObject m_encryptionDictionary;
    QByteArray m_encryptionDocumentId;
};

/// Specifies the security of unencrypted document
class PDFNoneSecurityHandler : public PDFSecurityHandler
{
public:
    virtual EncryptionMode getMode() const override { return EncryptionMode::None; }
    virtual PDFSecurityHandler* clone() const override { return new PDFNoneSecurityHandler(); }
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>&, bool) override { return AuthorizationResult::OwnerAuthorized; }
    virtual QByteArray decrypt(const QByteArray& data, PDFObjectReference, EncryptionScope) const override { return data; }
    virtual QByteArray decryptByFilter(const QByteArray& data, const QByteArray&, PDFObjectReference) const override { return data; }
    virtual QByteArray encrypt(const QByteArray& data, PDFObjectReference, EncryptionScope) const override { return data; }
    virtual QByteArray encryptByFilter(const QByteArray& data, const QByteArray&, PDFObjectReference) const override { return data; }
    virtual bool isMetadataEncrypted() const override { return true; }
    virtual bool isAllowed(Permission) const override { return true; }
    virtual bool isEncryptionAllowed() const override { return true; }
    virtual AuthorizationResult getAuthorizationResult() const override { return AuthorizationResult::NoAuthorizationRequired; }
    virtual PDFObject createEncryptionDictionaryObject() const override { return PDFObject(); }
};

class PDFStandardOrPublicSecurityHandler : public PDFSecurityHandler
{
public:
    virtual QByteArray decrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const override;
    virtual QByteArray decryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const override;
    virtual QByteArray encrypt(const QByteArray& data, PDFObjectReference reference, EncryptionScope encryptionScope) const override;
    virtual QByteArray encryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const override;
    virtual AuthorizationResult getAuthorizationResult() const override { return m_authorizationData.authorizationResult; }
    virtual bool isEncryptionAllowed() const override { return m_authorizationData.isAuthorized() && !m_isEmbeddedFilesLocked; }

    /// Adjusts the password according to the PDF specification: revisions 2 - 4 PDFDocEncoding,
    /// revisions 5 and 6 SASLprep (RFC 4013: the mapping 2.1, the normalization KC 2.2) and UTF-8,
    /// the first 127 bytes; another revision UTF-8 (the password of a PKCS#12, B84). A reader
    /// accepts the characters, which SASLprep prohibits (isPasswordValidForRevision6 checks them
    /// before an encryption, B93).
    static QByteArray adjustPassword(const QString& password, int revision);

    /// The password satisfies SASLprep for an encryption of revision 6 (RFC 4013 2.3: no prohibited
    /// output of RFC 3454 tables C.1.2 - C.9 after the mapping and the normalization; 2.4 and RFC
    /// 3454 6: a string with a right-to-left character has no left-to-right one and begins and ends
    /// by a right-to-left one; ISO 32000-2, 7.6.4.3.3, R5-17). The prohibited characters are
    /// appended to \p invalidCharacters (may be null).
    static bool isPasswordValidForRevision6(const QString& password, QString* invalidCharacters);

    struct AuthorizationData
    {
        bool isAuthorized() const { return authorizationResult == AuthorizationResult::UserAuthorized || authorizationResult == AuthorizationResult::OwnerAuthorized; }

        AuthorizationResult authorizationResult = AuthorizationResult::Failed;
        QByteArray fileEncryptionKey;
    };

protected:
    friend class PDFSecurityHandler;
    friend class PDFSecurityHandlerFactory;

    /// Decrypts data using specified filter. This function can be called only, if authorization was successfull.
    /// \param data Data to be decrypted
    /// \param filter Filter to be used for decryption
    /// \param reference Object reference for key generation
    /// \returns Decrypted data
    QByteArray decryptUsingFilter(const QByteArray& data, CryptFilter filter, PDFObjectReference reference) const;

    /// Encrypts data using specified filter. This function can be called only, if authorization was successfull.
    /// \param data Data to be encrypted
    /// \param filter Filter to be used for encryption
    /// \param reference Object reference for key generation
    /// \returns Encrypted data
    QByteArray encryptUsingFilter(const QByteArray& data, CryptFilter filter, PDFObjectReference reference) const;

    /// A crypt filter of the method None (ISO 32000-2, Table 25: the security handler decrypts the
    /// data by its own method) as the method of the handler - its default filter with the
    /// /AuthEvent of \p filter; another filter as it is. A handler without a method of its own (a
    /// hostile file of the public-key handler) throws: its data are never read as empty or garbled.
    CryptFilter resolveMethodNone(const CryptFilter& filter) const;

    /// Returns true, if character with unicode code is non-ascii space character
    /// according the RFC 3454, section C.1.2
    /// \param unicode Unicode code to be tested
    static bool isUnicodeNonAsciiSpaceCharacter(char32_t unicode);

    /// Returns true, if character with unicode code is mapped to nothing,
    /// according the RFC 3454, section B.1
    /// \param unicode Unicode code to be tested
    static bool isUnicodeMappedToNothing(char32_t unicode);

    /// Returns true, if the code point is prohibited by SASLprep (RFC 4013 2.3: RFC 3454 tables
    /// C.1.2, C.2.1, C.2.2, C.3, C.4, C.5, C.6, C.7, C.8, C.9)
    static bool isUnicodeProhibited(char32_t unicode);

    /// The SASLprep mapping (RFC 4013 2.1) and the normalization KC (2.2) of a password
    static QString prepareSASLprep(const QString& password);

    std::vector<uint8_t> createV2_ObjectEncryptionKey(PDFObjectReference reference, CryptFilter filter) const;
    std::vector<uint8_t> createAESV2_ObjectEncryptionKey(PDFObjectReference reference) const;
    CryptFilter getCryptFilter(EncryptionScope encryptionScope) const;

    /// Authorization data
    AuthorizationData m_authorizationData;
};

/// Specifies the security using standard security handler (see PDF specification
/// for details).
class PDFStandardSecurityHandler : public PDFStandardOrPublicSecurityHandler
{
public:
    virtual EncryptionMode getMode() const override { return EncryptionMode::Standard; }
    virtual PDFSecurityHandler* clone() const override;
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>& getPasswordCallback, bool authorizeOwnerOnly) override;
    virtual bool isMetadataEncrypted() const override { return m_encryptMetadata; }
    virtual bool isAllowed(Permission permission) const override;
    virtual PDFObject createEncryptionDictionaryObject() const override;

    /// Revision of the handler (/R of its encryption dictionary)
    int getRevision() const { return m_R; }

private:
    friend class PDFSecurityHandler;
    friend class PDFSecurityHandlerFactory;
    friend PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id);

    struct UserOwnerData_r6
    {
        QByteArray hash;
        QByteArray validationSalt;
        QByteArray keySalt;
    };

    /// Creates file encryption key from passed password, based on the revision
    /// \param password Password to be used to create file encryption key
    /// \note Password must be in PDFDocEncoding for revision 4 or earlier,
    ///       otherwise it must be encoded in UTF-8.
    QByteArray createFileEncryptionKey(const QByteArray& password) const;

    /// Length of the file encryption key of revisions 2 - 4 in bytes (Algorithm 2, step f: 5 for
    /// revision 2, /Length / 8 for revisions 3 and 4)
    int getKeyByteLength_r234() const;

    /// Creates entry value U based on the file encryption key. This function
    /// is valid only for revisions 2, 3 and 4.
    /// \param fileEncryptionKey File encryption key
    QByteArray createEntryValueU_r234(const QByteArray& fileEncryptionKey) const;

    /// Creates user password from the owner password. User password must be then
    /// authenticated.
    QByteArray createUserPasswordFromOwnerPassword(const QByteArray& password) const;

    /// Creates 32-byte padded password from the passed password. If password is empty,
    /// then padding password is returned.
    std::array<uint8_t, 32>  createPaddedPassword32(const QByteArray& password) const;

    /// Creates hash using algorithm 2.B for revision 6. Input is input of the hash,
    /// and if \p useUserKey is used, user key is considered in the hash.
    /// \param input Input of the hash
    /// \param inputPassword Input password
    /// \param useUserKey Use user key in the hash computation
    QByteArray createHash_r6(const QByteArray& input, const QByteArray& inputPassword, bool useUserKey) const;

    /// Parses parts of the user/owner data (U/O values of the encryption dictionary)
    UserOwnerData_r6 parseParts(const QByteArray& data) const;

    /// Revision number of standard security number
    int m_R = 0;

    /// 32 byte string if revision number is 4 or less, or 48 byte string,
    /// if revision number is 6, based on both owner and user passwords,
    /// used for authenticate owner, and create a file encryption key.
    QByteArray m_O;

    /// 32 byte string if revision number is 4 or less, or 48 byte string,
    /// if revision number is 6, based on both owner and user passwords,
    /// used for authenticate owner, and create a file encryption key.
    QByteArray m_U;

    /// For revision number 6 only. 32 bytes string based on both owner
    /// and user password, that shall be used to compute file encryption key.
    QByteArray m_OE;

    /// For revision number 6 only. 32 bytes string based on both owner
    /// and user password, that shall be used to compute file encryption key.
    QByteArray m_UE;

    /// What operations shall be permitted, when document is opened with user access.
    uint32_t m_permissions = 0;

    /// For revision number 6 only. 16 byte encrypted version of permissions.
    QByteArray m_Perms;

    /// Optional, meaningfull only if revision number is 4 or 5.
    bool m_encryptMetadata = true;

    /// First part of the id of the document
    QByteArray m_ID;
};

/// Specifies the security using public key security handler (see PDF specification
/// for details).
class PDFPublicKeySecurityHandler : public PDFStandardOrPublicSecurityHandler
{
public:
    virtual EncryptionMode getMode() const override { return EncryptionMode::PublicKey; }
    virtual PDFSecurityHandler* clone() const override;
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>& getPasswordCallback, bool authorizeOwnerOnly) override;
    virtual bool isMetadataEncrypted() const override;
    virtual bool isAllowed(Permission permission) const override;
    virtual PDFObject createEncryptionDictionaryObject() const override;

    /// The certificate of the recipient, whose private key has opened the document (7.6.5.3: the
    /// certificate of the PKCS#12, which decrypted its envelope) - public data only, never the key;
    /// empty, until an authentication succeeds. "Opens for" of the tools of step 5 shows its holder
    /// (433:5811).
    const std::optional<PDFCertificateInfo>& getAuthorizedCertificate() const { return m_authorizedCertificate; }

private:
    friend class PDFSecurityHandler;
    friend class PDFSecurityHandlerFactory;

    enum class PKCS7_Type
    {
        Unknown,
        PKCS7_S3,
        PKCS7_S4,
        PKCS7_S5
    };

    /// Bits of the permissions of a recipient (ISO 32000-2, Table 24)
    enum PermissionFlag : uint32_t
    {
        PKSH_Owner = 1 << 1,
        PKSH_PrintLowResolution = 1 << 2,
        PKSH_Modify = 1 << 3,
        PKSH_CopyContent = 1 << 4,
        PKSH_ModifyAnnotationsFillFormFields = 1 << 5,
        PKSH_FillFormFields = 1 << 8,
        PKSH_Accessibility = 1 << 9,
        PKSH_Assemble = 1 << 10,
        PKSH_PrintHighResolution = 1 << 11
    };

    /// Authenticates by the PKCS#12 of \p certificates (entry.pkcs12, R5-1, B102) and by the
    /// certificates of the system store among them (an entry of the type System without a PKCS#12,
    /// plan of step 6 of phase 2, Q6.2: PDFPersonalCertificateStore::decryptEnvelope - the key stays
    /// in the system, which may ask for its PIN in its own dialog). The empty password of the
    /// PKCS#12 first, then the system store, then the passwords of the callback (UTF-8, B84) - the
    /// callback only, when there is a PKCS#12.
    AuthorizationResult authenticateByCertificateEntries(const PDFCertificateEntries& certificates, const std::function<QString(bool*)>& getPasswordCallback);

    /// Opens the file encryption key by one PKCS#12 and its password (7.6.5.3); true on success. A
    /// PKCS#12 of more iterations than PDFCertificateManager::MAXIMUM_PKCS12_ITERATIONS is refused
    /// before its password is tried (plan of step 6 of phase 2, 6.3, R6-28).
    bool authenticateByPkcs12(const QByteArray& pkcs12Data, const QByteArray& passwordUtf8);

    /// Opens the file encryption key by the private key of a certificate of the system store (Q6.2),
    /// which decrypts the envelope of its recipient; true on success
    /// \param certificateData DER of the certificate (PDFCertificateInfo::getCertificateData)
    bool authenticateBySystemStore(const QByteArray& certificateData);

    /// Authorizes the handler by the decrypted envelope of its recipient (7.6.5.3: the 20 bytes of
    /// the seed and the 4 bytes of the permissions of Table 24): the file encryption key, the
    /// permissions of the envelope, the certificate of the recipient. \p decryptedData are cleared.
    /// \returns false for data shorter than the seed
    bool authorizeByEnvelope(QByteArray& decryptedData, std::optional<PDFCertificateInfo> certificate);

    /// What operations shall be permitted, when document is opened with user access.
    uint32_t m_permissions = 0;

    /// Type of the PKCS7 subfilter
    PKCS7_Type m_pkcs7Type = PKCS7_Type::Unknown;

    /// The certificate of the authorized recipient (getAuthorizedCertificate)
    std::optional<PDFCertificateInfo> m_authorizedCertificate;
};

/// Factory, which creates security handler based on settings.
class PDF4QTLIBCORESHARED_EXPORT PDFSecurityHandlerFactory
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFSecurityHandlerFactory)

public:

    enum Algorithm
    {
        None,
        RC4,
        AES_128,
        AES_256,
        Certificate
    };

    enum EncryptContents
    {
        All,
        AllExceptMetadata,
        EmbeddedFiles
    };

    /// The settings of a new encryption - the fixed interface of the factory for the tool
    /// PDFDocumentToolEncryption (plan of step 5 of phase 2, 10.3, R5-13; the factory belongs to
    /// package P1a, the tool to P1b). The settings hold passwords: the caller clears its copy by
    /// PDFSecureMemory::clear after the call and never logs them.
    struct SecuritySettings
    {
        Algorithm algorithm = None;                 ///< RC4 and AES_128 write revision 4 (V 4), AES_256 revision 6 (V 5), Certificate the public-key handler (adbe.pkcs7.s5, AESV3); revision 5 is never written
        EncryptContents encryptContents = All;      ///< ISO 32000-2, 7.6.6: All, AllExceptMetadata (/EncryptMetadata false), EmbeddedFiles (/StmF and /StrF Identity, /EFF with /AuthEvent /EFOpen)
        QString userPassword;                       ///< Standard handler: the open (user) password, may be empty. Certificate: the password of the PKCS#12 of \p certificate (UTF-8, B84), only to check that the private key opens the result
        QString ownerPassword;                      ///< Standard handler: the owner password; empty = the user password is used for every revision (ISO 32000-2, Algorithm 3 a), B81, R5-2)
        uint32_t permissions = 0;                   ///< Mask of PDFSecurityHandler::Permission (Table 22 / Table 24); the factory normalizes the reserved bits (B89) and always sets bit 10 (K5.7, R5-14); for a certificate with every permission it sets bit 2 of the recipient (Table 24, owner answer Q5.3 b)
        QByteArray id;                              ///< The first element of /ID of the document, which will be written (R5-5): the caller creates /ID before the handler (an existing /ID[0] stays, 14.4) - the key of revisions 2-4 is computed of it (Algorithm 2, step e)
        PDFCertificateEntry certificate;            ///< Certificate: the only recipient (the encryption for oneself, tools.md 10.3 point 30) with its PKCS#12 (entry.pkcs12, R5-1); at most MAXIMUM_RECIPIENT_COUNT recipients are ever read (B95)
    };

    /// Most recipients of the public-key handler read from a file (B95: each recipient costs an
    /// RSA operation per certificate of the user at the opening - a hostile file must not make it
    /// a denial of service)
    static constexpr int MAXIMUM_RECIPIENT_COUNT = 256;

    /// Longest PKCS#7 enveloped data of one recipient read from a file (B95)
    static constexpr int MAXIMUM_RECIPIENT_SIZE = 64 * 1024;

    /// Creates security handler based on given settings. If security handler cannot
    /// be created, then nullptr is returned. Unlike the overload with the reason, it does not
    /// check the rules of validate (the old applications call validate first; the tests make
    /// documents of other producers by it) - the empty owner password is the user password
    /// all the same (B81, R5-2).
    /// \param settings Security handler settings
    static PDFSecurityHandlerPointer createSecurityHandler(const SecuritySettings& settings);

    /// Creates the security handler of the settings with the reason of a failure (B101: the tool and
    /// PdfTool never write a document without the encryption the user asked for). The settings are
    /// checked by validate first. The handler is authorized as the owner (standard handler) or by the
    /// private key of the certificate (public-key handler; the check uses the PKCS#12 of the settings
    /// and its password, R5-1).
    /// \param settings Security handler settings
    /// \param errorMessage Translated reason of a failure (may be null); never contains a password
    /// \returns the handler, nullptr on a failure
    static PDFSecurityHandlerPointer createSecurityHandler(const SecuritySettings& settings, QString* errorMessage);

    /// The value of /P of the standard handler written for \p permissions (a mask of
    /// PDFSecurityHandler::Permission; B89): the bits of Table 22 as given, bit 10 always set
    /// (PDF 2.0 requires it, K5.7, R5-14), bits 1 and 2 cleared, bits 7, 8 and 13 - 32 set
    /// (PDF 1.7 Table 3.20, ISO 32000-2 7.6.4.2 NOTE)
    static uint32_t getStandardPermissions(uint32_t permissions);

    /// The permissions of a recipient of the public-key handler written for \p permissions
    /// (ISO 32000-2, Table 24, B90): the bits 3, 4, 5, 6, 9, 11, 12 as given, bit 10 always, and
    /// bit 2 (the change of the encryption and every operation) when all seven permissions are
    /// allowed (owner answer Q5.3 b); nothing else
    static uint32_t getPublicKeyPermissions(uint32_t permissions);

    /// Checks, that \p password can encrypt by \p algorithm: RC4 and AES_128 (revision 4) - every
    /// character in PDFDocEncoding; AES_256 (revision 6) - SASLprep (RFC 4013 2.3 and 2.4, B93,
    /// R5-17). The characters, which cannot be used, are appended to \p invalidCharacters (may be
    /// null; empty for a violation of the bidirectional rule). Another algorithm accepts everything.
    static bool isPasswordValid(const QString& password, Algorithm algorithm, QString* invalidCharacters);

    /// Returns optimal number of bits (entropy) for strong password
    static int getPasswordOptimalEntropy();

    /// Calculates password entropy (number of bits), can be used
    /// for ranking the password security. Encryption algorithm must be also
    /// considered, because password is adjusted according to the specification
    /// before it's entropy is being computed.
    /// \param password Password to be scored
    /// \param algorithm Encryption algorithm
    static int getPasswordEntropy(const QString& password, Algorithm algorithm);

    /// Returns revision number of standard security handler for a given
    /// algorithm. If algorithm is invalid or None, zero is returned.
    /// \param algorithm Algorithm
    static int getRevisionFromAlgorithm(Algorithm algorithm);

    /// Most bytes of one call of generateRandomBytes (a key, a salt, an IV, a seed is at most 32)
    static constexpr int MAXIMUM_RANDOM_BYTES = 1024 * 1024;

    /// Random bytes of the cryptographic generator of OpenSSL (RAND_bytes; plan of step 5 of phase
    /// 2, 6.4, S5.4, B82): the only source of the keys, salts, IVs, the random bytes of /Perms and
    /// the seed of the public-key handler. Thread-safe (OpenSSL 3). It fails loudly: a size out of
    /// 0 ... MAXIMUM_RANDOM_BYTES or a failure of the generator throws PDFException, there is never
    /// a fallback to a generator, which is not cryptographic (no handler is created then).
    /// \param size Number of the bytes (0 = an empty array)
    static QByteArray generateRandomBytes(int size);

    /// Validates security settings: the passwords can encrypt by the algorithm (isPasswordValid);
    /// restrictions (any permission of Table 22 not allowed) need an owner password, which is not
    /// empty and differs from the user password (ISO 32000-2, 7.6.4.1: else every reader opens the
    /// document as its owner; B81, R5-23, Q5.1); a certificate has its PKCS#12 and opens by its
    /// password, or is a certificate of the system store, which can be a recipient
    /// (isSystemStoreCertificate, PDFCertificateManager::isUsableForEncryption).
    /// \param settings Settings
    /// \param[out] errorMessage Error message (may be null)
    static bool validate(const SecuritySettings& settings, QString* errorMessage);

    /// A certificate of the system store (plan of step 6 of phase 2, Q6.2 and the addendum of the owner
    /// of 15. 11.): the type System, no PKCS#12, its certificate known. As a recipient of the encryption
    /// by a certificate only its public certificate is used (no password, its key is never touched);
    /// a document encrypted for it is opened by its key in the store (PDFPersonalCertificateStore)
    static bool isSystemStoreCertificate(const PDFCertificateEntry& entry);

private:
    /// The creation of both overloads of createSecurityHandler (\p errorMessage may be null)
    static PDFSecurityHandlerPointer createSecurityHandlerImpl(const SecuritySettings& settings, QString* errorMessage);

    /// The creation itself; an exception (the random generator) is turned into the reason by
    /// createSecurityHandlerImpl
    static PDFSecurityHandlerPointer createSecurityHandlerUnchecked(const SecuritySettings& settings, QString* errorMessage);

    /// The recipient of the public-key handler: PKCS#7 enveloped data of the seed and the
    /// permissions for the certificate of \p pkcs12 (7.6.5.3); an empty array on a failure
    static QByteArray createRecipient(const PDFCertificateEntry& certificate, const QString& password, const QByteArray& seed, uint32_t permissions, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFSECURITYHANDLER_H
