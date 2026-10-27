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

#ifndef PDFMCPREQUESTSTATE_H
#define PDFMCPREQUESTSTATE_H

#include "pdf4qtlibmcp_export.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QString>

namespace pdf
{

/// Data sealed into the requestState of an InputRequiredResult (MRTR, plan
/// 2.8): bound to the assistant, the method, the tool / prompt and the canonical
/// arguments, valid for 5 minutes
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpRequestStateData
{
public:
    static constexpr int VERSION = 1;

    int version = VERSION;
    QString assistantKey;
    QString method;                 ///< "tools/call" / "prompts/get"
    QString name;                   ///< Tool or prompt name
    QByteArray argumentsHash;       ///< SHA-256 of the canonical arguments (PDFMcpRequestState::hashArguments)
    QJsonObject allowedValues;      ///< Values allowed in the answers
    qint64 expiresAt = 0;           ///< Milliseconds since the epoch
    QByteArray nonce;               ///< 16 random bytes
};

/// Result of PDFMcpRequestState::unseal
enum class PDFMcpRequestStateResult
{
    Valid,
    Malformed,      ///< Not a sealed state of this format (or too long)
    BadSignature,   ///< The HMAC does not match (tampered, or sealed by another run of the server)
    Expired,        ///< Older than PDFMcpLimits::REQUEST_STATE_LIFETIME
    WrongBinding    ///< Another assistant, method, tool or arguments
};

/// Seal of the requestState of MRTR (plan 2.2): HMAC-SHA256 under a random
/// 256-bit key per server run. The sealed text is
/// base64url(payload) "." base64url(mac), at most
/// PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE characters; the payload is the
/// compact JSON of PDFMcpRequestStateData. Thread safe (immutable key).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpRequestState
{
public:
    /// A random key (QRandomGenerator::system)
    PDFMcpRequestState();

    /// A given key (tests); a key shorter than 32 bytes is padded by zeros
    explicit PDFMcpRequestState(const QByteArray& key);

    /// Sealed text, empty when the data do not fit the limit
    QByteArray seal(const PDFMcpRequestStateData& data) const;

    /// Verifies the sealed text: the signature (constant time), the format, the
    /// expiry against \p now and the binding to the assistant, the method, the
    /// name and the arguments. \p data gets the data of a valid state.
    PDFMcpRequestStateResult unseal(QByteArrayView sealed, qint64 now, const QString& assistantKey, const QString& method,
                                    const QString& name, const QByteArray& argumentsHash, PDFMcpRequestStateData* data) const;

    /// SHA-256 of the canonical (compact, sorted) JSON of the arguments
    static QByteArray hashArguments(const QJsonObject& arguments);

    /// 16 random bytes
    static QByteArray createNonce();

    /// Constant time comparison of two byte arrays (also of the tokens)
    static bool isEqualConstantTime(QByteArrayView first, QByteArrayView second);

private:
    QByteArray computeMac(QByteArrayView payload) const;

    QByteArray m_key;
};

}   // namespace pdf

#endif // PDFMCPREQUESTSTATE_H
