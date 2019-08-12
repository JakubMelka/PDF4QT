//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFSECURITYHANDLER_H
#define PDFSECURITYHANDLER_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QSharedPointer>

#include <map>
#include <functional>

namespace pdf
{

enum class EncryptionMode
{
    None,       ///< Document is not encrypted
    Standard,   ///< Document is encrypted and using standard security handler
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
    CryptFilterType type = CryptFilterType::None;
    AuthEvent authEvent = AuthEvent::DocOpen;
};

class PDFSecurityHandler;
using PDFSecurityHandlerPointer = QSharedPointer<PDFSecurityHandler>;

class PDFSecurityHandler
{
public:
    explicit PDFSecurityHandler() = default;
    virtual ~PDFSecurityHandler() = default;

    enum class AuthorizationResult
    {
        UserAuthorized,
        OwnerAuthorized,
        Failed,
        Cancelled
    };

    virtual EncryptionMode getMode() const = 0;
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>& getPasswordCallback) = 0;

    /// Creates a security handler from the object. If object is null, then
    /// "None" security handler is created. If error occurs, then exception is thrown.
    /// \param encryptionDictionaryObject Encryption dictionary object
    /// \param id First part of the id of the document
    static PDFSecurityHandlerPointer createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id);

protected:
    /// Version of the encryption, shall be a number from 1 to 5, according the
    /// PDF specification. Other values are invalid.
    int m_V = 0;

    /// Length of the key to encrypt/decrypt the document in bits. Only valid
    /// for V = 2 or V = 3, otherwise it is invalid.
    int m_keyLength = 40;

    /// Map containing crypt filters.
    std::map<QByteArray, CryptFilter> m_cryptFilters;

    /// Crypt filter for decrypting strings
    CryptFilter m_filterStrings;

    /// Crypt filter for decrypting streams
    CryptFilter m_filterStreams;

    /// Crypt filter for decrypting embedded files
    CryptFilter m_filterEmbeddedFiles;
};

/// Specifies the security of unencrypted document
class PDFNoneSecurityHandler : public PDFSecurityHandler
{
public:
    virtual EncryptionMode getMode() const { return EncryptionMode::None; }
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>&) override { return AuthorizationResult::OwnerAuthorized; }
};

/// Specifies the security using standard security handler (see PDF specification
/// for details).
class PDFStandardSecurityHandler : public PDFSecurityHandler
{
public:
    virtual EncryptionMode getMode() const { return EncryptionMode::Standard; }
    virtual AuthorizationResult authenticate(const std::function<QString(bool*)>& getPasswordCallback) override;

    struct AuthorizationData
    {
        bool isAuthorized() const { return authorizationResult == AuthorizationResult::UserAuthorized || authorizationResult == AuthorizationResult::OwnerAuthorized; }

        AuthorizationResult authorizationResult = AuthorizationResult::Failed;
        QByteArray fileEncryptionKey;
    };

private:
    friend static PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id);

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

    /// Adjusts the password according to the PDF specification
    QByteArray adjustPassword(const QString& password);

    /// Returns true, if character with unicode code is non-ascii space character
    /// according the RFC 3454, section C.1.2
    /// \param unicode Unicode code to be tested
    static bool isUnicodeNonAsciiSpaceCharacter(ushort unicode);

    /// Returns true, if character with unicode code is mapped to nothing,
    /// according the RFC 3454, section B.1
    /// \param unicode Unicode code to be tested
    static bool isUnicodeMappedToNothing(ushort unicode);

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

    /// Authorization data
    AuthorizationData m_authorizationData;
};

}   // namespace pdf


#endif // PDFSECURITYHANDLER_H
