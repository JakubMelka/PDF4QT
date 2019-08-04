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

    virtual EncryptionMode getMode() const = 0;

    /// Creates a security handler from the object. If object is null, then
    /// "None" security handler is created. If error occurs, then exception is thrown.
    /// \param encryptionDictionaryObject Encryption dictionary object
    static PDFSecurityHandlerPointer createSecurityHandler(const PDFObject& encryptionDictionaryObject);

private:
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
};

/// Specifies the security using standard security handler (see PDF specification
/// for details).
class PDFStandardSecurityHandler : public PDFSecurityHandler
{
public:
    virtual EncryptionMode getMode() const { return EncryptionMode::Standard; }

private:

};

}   // namespace pdf


#endif // PDFSECURITYHANDLER_H
