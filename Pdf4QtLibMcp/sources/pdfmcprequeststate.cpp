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

#include "pdfmcprequeststate.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace pdf
{

PDFMcpRequestState::PDFMcpRequestState()
{
    m_key.resize(32);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(m_key.data()), 8);
}

PDFMcpRequestState::PDFMcpRequestState(const QByteArray& key) :
    m_key(key)
{
    if (m_key.size() < 32)
    {
        m_key.append(QByteArray(32 - m_key.size(), '\0'));
    }
}

QByteArray PDFMcpRequestState::computeMac(QByteArrayView payload) const
{
    return QMessageAuthenticationCode::hash(payload.toByteArray(), m_key, QCryptographicHash::Sha256);
}

QByteArray PDFMcpRequestState::seal(const PDFMcpRequestStateData& data) const
{
    QJsonObject object;
    object.insert(QStringLiteral("v"), data.version);
    object.insert(QStringLiteral("a"), data.assistantKey);
    object.insert(QStringLiteral("m"), data.method);
    object.insert(QStringLiteral("n"), data.name);
    object.insert(QStringLiteral("h"), QString::fromLatin1(PDFMcpJson::toBase64Url(data.argumentsHash)));
    object.insert(QStringLiteral("s"), data.allowedValues);
    object.insert(QStringLiteral("e"), data.expiresAt);
    object.insert(QStringLiteral("r"), QString::fromLatin1(PDFMcpJson::toBase64Url(data.nonce)));

    const QByteArray payload = PDFMcpJson::toBase64Url(PDFMcpJson::toCompactJson(object));
    QByteArray sealed = payload + '.' + PDFMcpJson::toBase64Url(computeMac(payload));
    if (sealed.size() > PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE)
    {
        return QByteArray();
    }
    return sealed;
}

PDFMcpRequestStateResult PDFMcpRequestState::unseal(QByteArrayView sealed, qint64 now, const QString& assistantKey, const QString& method,
                                                    const QString& name, const QByteArray& argumentsHash, PDFMcpRequestStateData* data) const
{
    if (sealed.isEmpty() || sealed.size() > PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE)
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    const qsizetype dot = sealed.indexOf('.');
    if (dot <= 0 || dot != sealed.lastIndexOf('.'))
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    const QByteArrayView payload = sealed.first(dot);
    const std::optional<QByteArray> mac = PDFMcpJson::fromBase64Url(sealed.sliced(dot + 1));
    if (!mac)
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    // The signature first: nothing of an unsigned payload is parsed
    if (!isEqualConstantTime(*mac, computeMac(payload)))
    {
        return PDFMcpRequestStateResult::BadSignature;
    }

    const std::optional<QByteArray> json = PDFMcpJson::fromBase64Url(payload);
    if (!json)
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(*json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    const QJsonObject object = document.object();
    PDFMcpRequestStateData result;
    result.version = object.value(QStringLiteral("v")).toInt(-1);
    result.assistantKey = object.value(QStringLiteral("a")).toString();
    result.method = object.value(QStringLiteral("m")).toString();
    result.name = object.value(QStringLiteral("n")).toString();
    result.argumentsHash = PDFMcpJson::fromBase64Url(object.value(QStringLiteral("h")).toString().toLatin1()).value_or(QByteArray());
    result.allowedValues = object.value(QStringLiteral("s")).toObject();
    result.expiresAt = object.value(QStringLiteral("e")).toInteger(0);
    result.nonce = PDFMcpJson::fromBase64Url(object.value(QStringLiteral("r")).toString().toLatin1()).value_or(QByteArray());

    if (result.version != PDFMcpRequestStateData::VERSION)
    {
        return PDFMcpRequestStateResult::Malformed;
    }

    if (now > result.expiresAt)
    {
        return PDFMcpRequestStateResult::Expired;
    }

    if (result.assistantKey != assistantKey || result.method != method || result.name != name ||
        !isEqualConstantTime(result.argumentsHash, argumentsHash))
    {
        return PDFMcpRequestStateResult::WrongBinding;
    }

    if (data)
    {
        *data = std::move(result);
    }
    return PDFMcpRequestStateResult::Valid;
}

QByteArray PDFMcpRequestState::hashArguments(const QJsonObject& arguments)
{
    return QCryptographicHash::hash(PDFMcpJson::toCompactJson(arguments), QCryptographicHash::Sha256);
}

QByteArray PDFMcpRequestState::createNonce()
{
    QByteArray nonce(16, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(nonce.data()), 4);
    return nonce;
}

bool PDFMcpRequestState::isEqualConstantTime(QByteArrayView first, QByteArrayView second)
{
    // The lengths are not secret (fixed sizes of hashes and tokens)
    if (first.size() != second.size())
    {
        return false;
    }

    unsigned char difference = 0;
    for (qsizetype i = 0; i < first.size(); ++i)
    {
        difference |= static_cast<unsigned char>(first[i] ^ second[i]);
    }
    return difference == 0;
}

}   // namespace pdf
