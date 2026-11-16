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

#include "pdftimestampclient.h"
#include "pdfcertificatestore.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"
#include "pdfsecurityhandler.h"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QTimer>

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pkcs7.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <ctime>
#include <memory>

#include "pdfdbgheap.h"

namespace pdf
{

/// The network transport of the application (PDFTimestampClient::createNetworkTransport)
class PDFTimestampNetworkTransport : public PDFTimestampTransport
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFTimestampNetworkTransport)

public:
    virtual Response post(const Request& request, const PDFOperationControl* control) const override;

    /// Interval of the polling of the cancellation and of the time limit (the contract: at most 100 ms)
    static constexpr int POLL_INTERVAL_MILLISECONDS = 50;

    /// Largest buffer of a reply, which is not read yet (the body is read as it comes)
    static constexpr qint64 READ_BUFFER_SIZE = 64 * 1024;
};

/// The proxy of the system for the requests of one QNetworkAccessManager (R6-28: never the global
/// QNetworkProxyFactory::setUseSystemConfiguration, which changes the whole process); no proxy for a
/// loopback address (a local authority of a test or of an intranet service on the same computer)
class PDFTimestampProxyFactory : public QNetworkProxyFactory
{
public:
    virtual QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override;
};

/// Helpers of PDFTimestampClient
class PDFTimestampClientHelper
{
public:
    template<typename T>
    using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

    /// Frees a stack of certificates with the certificates
    static void freeCertificateStack(STACK_OF(X509)* certificates) { sk_X509_pop_free(certificates, X509_free); }

    /// Frees only the stack (the certificates belong to somebody else)
    static void freeStackOnly(STACK_OF(X509)* certificates) { sk_X509_free(certificates); }

    /// Gives the store and the certificates to the verification context (it owns them afterwards). The
    /// functions with 0 in their name have clear ownership semantics, but they were introduced in
    /// OpenSSL 3.4, so the older ones are used with the older versions (the project still supports them).
    static void setVerifyStore(TS_VERIFY_CTX* context, X509_STORE* store, STACK_OF(X509)* certificates)
    {
#if OPENSSL_VERSION_NUMBER >= 0x30400000L
        TS_VERIFY_CTX_set0_store(context, store);
        TS_VERIFY_CTX_set0_certs(context, certificates);
#else
        TS_VERIFY_CTX_set_store(context, store);
        TS_VERIFY_CTS_set_certs(context, certificates);
#endif
    }

    /// Gives the timestamped data to the verification context (it owns them afterwards)
    static void setVerifyData(TS_VERIFY_CTX* context, BIO* data)
    {
#if OPENSSL_VERSION_NUMBER >= 0x30400000L
        TS_VERIFY_CTX_set0_data(context, data);
#else
        TS_VERIFY_CTX_set_data(context, data);
#endif
    }

    /// The certificates of a token (a signed PKCS#7), null for another type
    static STACK_OF(X509)* getTokenCertificates(PKCS7* token)
    {
        return (token && PKCS7_type_is_signed(token) && token->d.sign) ? token->d.sign->cert : nullptr;
    }

    /// The certificate of the signer of a token (owned by the token), null when the token does not carry it
    static X509* getTokenSigner(PKCS7* token)
    {
        STACK_OF(X509)* certificates = getTokenCertificates(token);
        if (!certificates)
        {
            return nullptr;
        }

        openssl_ptr<STACK_OF(X509)> signers(PKCS7_get0_signers(token, certificates, 0), &freeStackOnly);
        return (signers && sk_X509_num(signers.get()) == 1) ? sk_X509_value(signers.get(), 0) : nullptr;
    }

    /// Creates the verification store of a token: the certificate of its authority ends the chain (its
    /// issuers are not verified, the trust is decided by the verification of the signatures), OpenSSL
    /// checks its validity and its purpose (X509_PURPOSE_TIMESTAMP_SIGN). Sets the store and the
    /// certificates of the token to the context.
    static bool setTokenStore(TS_VERIFY_CTX* context, PKCS7* token)
    {
        X509* signer = getTokenSigner(token);
        STACK_OF(X509)* tokenCertificates = getTokenCertificates(token);
        X509_STORE* store = X509_STORE_new();
        STACK_OF(X509)* certificates = sk_X509_new_null();
        if (!signer || !tokenCertificates || !store || !certificates)
        {
            X509_STORE_free(store);
            sk_X509_free(certificates);
            return false;
        }

        X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN);
        bool isAdded = X509_STORE_add_cert(store, signer) == 1;
        for (int i = 0; isAdded && i < sk_X509_num(tokenCertificates); ++i)
        {
            X509* certificate = sk_X509_value(tokenCertificates, i);
            isAdded = X509_up_ref(certificate) == 1;
            if (isAdded && sk_X509_push(certificates, certificate) <= 0)
            {
                X509_free(certificate);
                isAdded = false;
            }
        }

        setVerifyStore(context, store, certificates);
        return isAdded;
    }

    /// DER of an OpenSSL object by its i2d function
    template<typename T>
    static QByteArray encode(const T* object, int (*i2d)(const T*, unsigned char**))
    {
        unsigned char* buffer = nullptr;
        const int size = object ? i2d(object, &buffer) : -1;
        QByteArray result;
        if (size > 0 && buffer)
        {
            result = QByteArray(reinterpret_cast<const char*>(buffer), size);
        }
        OPENSSL_free(buffer);
        return result;
    }

    /// The time of a token in UTC (an invalid time for an invalid value)
    static QDateTime getDateTime(const ASN1_GENERALIZEDTIME* time)
    {
        std::tm value = { };
        if (!time || ASN1_TIME_to_tm(time, &value) != 1)
        {
            return QDateTime();
        }

        const QDate date(value.tm_year + 1900, value.tm_mon + 1, value.tm_mday);
        const QTime clock(value.tm_hour, value.tm_min, value.tm_sec);
        return QDateTime(date, clock, QTimeZone::UTC);
    }

    /// The Content-Type of an answer of an authority (RFC 3161, 3.4: application/timestamp-reply; the
    /// older application/timestamp-response is tolerated, 6.6), its parameters are ignored
    static bool isTimestampReply(const QByteArray& contentType)
    {
        const qsizetype parametersStart = contentType.indexOf(';');
        const QByteArray mediaType = (parametersStart >= 0 ? contentType.left(parametersStart) : contentType).trimmed().toLower();
        return mediaType == "application/timestamp-reply" || mediaType == "application/timestamp-response";
    }

    /// The name of the authority shown to the user: the common name of its certificate, else the
    /// organization
    static QString getAuthorityName(X509* certificate)
    {
        const PDFCertificateInfo info = PDFCertificateInfo::getCertificateInfo(certificate);
        const QString commonName = info.getName(PDFCertificateInfo::CommonName);
        return commonName.isEmpty() ? info.getName(PDFCertificateInfo::OrganizationName) : commonName;
    }

    /// Creates the TimeStampReq of a SHA-256 hash (RFC 3161, 2.4.1): version 1, the imprint, a nonce,
    /// certReq true. Returns null on a failure (also a failure of the random generator).
    static TS_REQ* createRequest(const QByteArray& digest)
    {
        openssl_ptr<TS_REQ> request(TS_REQ_new(), &TS_REQ_free);
        openssl_ptr<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new(), &TS_MSG_IMPRINT_free);
        openssl_ptr<X509_ALGOR> algorithm(X509_ALGOR_new(), &X509_ALGOR_free);
        if (!request || !imprint || !algorithm)
        {
            return nullptr;
        }

        // B133: the nonce comes from the cryptographic generator (it throws on its failure)
        QByteArray nonceBytes;
        try
        {
            nonceBytes = PDFSecurityHandlerFactory::generateRandomBytes(PDFTimestampClient::NONCE_LENGTH);
        }
        catch (const PDFException&)
        {
            return nullptr;
        }

        openssl_ptr<BIGNUM> nonceNumber(BN_bin2bn(reinterpret_cast<const unsigned char*>(nonceBytes.constData()), int(nonceBytes.size()), nullptr), &BN_free);
        openssl_ptr<ASN1_INTEGER> nonce(nonceNumber ? BN_to_ASN1_INTEGER(nonceNumber.get(), nullptr) : nullptr, &ASN1_INTEGER_free);
        if (!nonce)
        {
            return nullptr;
        }

        // TS_MSG_IMPRINT_set_algo, TS_REQ_set_msg_imprint and TS_REQ_set_nonce copy their arguments
        if (X509_ALGOR_set0(algorithm.get(), OBJ_nid2obj(NID_sha256), V_ASN1_NULL, nullptr) != 1 ||
            TS_REQ_set_version(request.get(), 1) != 1 ||
            TS_MSG_IMPRINT_set_algo(imprint.get(), algorithm.get()) != 1 ||
            TS_MSG_IMPRINT_set_msg(imprint.get(), reinterpret_cast<unsigned char*>(const_cast<char*>(digest.constData())), int(digest.size())) != 1 ||
            TS_REQ_set_msg_imprint(request.get(), imprint.get()) != 1 ||
            TS_REQ_set_nonce(request.get(), nonce.get()) != 1 ||
            TS_REQ_set_cert_req(request.get(), 1) != 1)
        {
            return nullptr;
        }

        return request.release();
    }
};

PDFTimestampTransport::~PDFTimestampTransport() = default;

QList<QNetworkProxy> PDFTimestampProxyFactory::queryProxy(const QNetworkProxyQuery& query)
{
    const QString host = query.peerHostName();
    QHostAddress address;
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || (address.setAddress(host) && address.isLoopback()))
    {
        return { QNetworkProxy(QNetworkProxy::NoProxy) };
    }

    QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(query);
    if (proxies.isEmpty())
    {
        proxies << QNetworkProxy(QNetworkProxy::NoProxy);
    }
    return proxies;
}

PDFTimestampTransport::Response PDFTimestampNetworkTransport::post(const Request& request, const PDFOperationControl* control) const
{
    Response response;

    if (!request.url.isValid() || request.timeoutMilliseconds <= 0 || request.maximumResponseSize <= 0)
    {
        response.error = Error::Other;
        response.errorMessage = tr("The request of the timestamp authority is not valid.");
        return response;
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        response.error = Error::Canceled;
        return response;
    }

    // Own network objects in the calling thread (the thread of the run, B120): nothing is shared with
    // another request or with the rest of the application
    QNetworkAccessManager manager;
    manager.setProxyFactory(new PDFTimestampProxyFactory());
    manager.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);

    QNetworkRequest networkRequest(request.url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, request.contentType);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("PDF4QT"));
    networkRequest.setRawHeader("Accept", "application/timestamp-reply");
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    networkRequest.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    networkRequest.setMaximumRedirectsAllowed(0);
    networkRequest.setTransferTimeout(request.timeoutMilliseconds);

    const QDeadlineTimer deadline(request.timeoutMilliseconds);
    QNetworkReply* reply = manager.post(networkRequest, request.body);
    reply->setReadBufferSize(READ_BUFFER_SIZE);

    Error abortReason = Error::None;
    QByteArray body;

    // The event loop is the context of every connection: they end with it, before the manager deletes
    // the reply
    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(POLL_INTERVAL_MILLISECONDS);

    auto abortReply = [&abortReason, reply](Error reason)
    {
        if (abortReason == Error::None && !reply->isFinished())
        {
            abortReason = reason;
            reply->abort();
        }
    };

    auto readBody = [&body, &request, reply, &abortReply]()
    {
        // At most one byte above the limit is read: the limit is exceeded, the rest is never stored
        const qint64 remaining = request.maximumResponseSize + 1 - body.size();
        if (remaining > 0)
        {
            body.append(reply->read(remaining));
        }

        if (body.size() > request.maximumResponseSize || (remaining <= 0 && reply->bytesAvailable() > 0))
        {
            abortReply(Error::TooLarge);
        }
    };

    QObject::connect(&pollTimer, &QTimer::timeout, &eventLoop, [control, &deadline, &abortReply]()
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            abortReply(Error::Canceled);
        }
        else if (deadline.hasExpired())
        {
            abortReply(Error::Timeout);
        }
    });
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &eventLoop, [reply, &request, &abortReply]()
    {
        // Content-Length above the limit: the body is not even started
        const QVariant contentLength = reply->header(QNetworkRequest::ContentLengthHeader);
        if (contentLength.isValid() && contentLength.toLongLong() > request.maximumResponseSize)
        {
            abortReply(Error::TooLarge);
        }
    });
    QObject::connect(reply, &QNetworkReply::readyRead, &eventLoop, readBody);
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    pollTimer.start();
    if (!reply->isFinished())
    {
        // A local loop of the thread of the run; the GUI thread of the new application never gets here
        // (the old applications do - SECURITY_TODO 52)
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    pollTimer.stop();

    if (abortReason == Error::None)
    {
        readBody();
    }

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QNetworkReply::NetworkError networkError = reply->error();
    response.httpStatus = statusCode.isValid() ? statusCode.toInt() : 0;
    response.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

    if (abortReason != Error::None)
    {
        response.error = abortReason;
    }
    else if (response.httpStatus >= 300 && response.httpStatus < 400)
    {
        // ManualRedirectPolicy: the redirection is reported, never followed (6.6)
        response.error = Error::Redirected;
    }
    else if (response.httpStatus > 0)
    {
        // An answer of the server: its HTTP status decides
        response.error = Error::None;
        response.body = body;
    }
    else
    {
        switch (networkError)
        {
            case QNetworkReply::NoError:
                response.error = Error::Other;
                response.errorMessage = tr("The timestamp authority sent no HTTP answer.");
                break;

            case QNetworkReply::TimeoutError:
                response.error = Error::Timeout;
                break;

            case QNetworkReply::SslHandshakeFailedError:
                response.error = Error::Tls;
                response.errorMessage = tr("The certificate of the server of the timestamp authority is not valid.");
                break;

            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::UnknownNetworkError:
            case QNetworkReply::ProxyConnectionRefusedError:
            case QNetworkReply::ProxyConnectionClosedError:
            case QNetworkReply::ProxyNotFoundError:
            case QNetworkReply::ProxyTimeoutError:
            case QNetworkReply::UnknownProxyError:
                response.error = Error::NoNetwork;
                response.errorMessage = tr("No network connection.");
                break;

            default:
                // The text of Qt names the address, which has no user info (validateUrl)
                response.error = Error::Other;
                response.errorMessage = reply->errorString();
                break;
        }
    }

    if (!reply->isFinished())
    {
        reply->abort();
    }
    return response;
}

PDFTimestampClient::PDFTimestampClient(std::shared_ptr<const PDFTimestampTransport> transport) :
    m_transport(std::move(transport))
{

}

PDFTimestampClient::PDFTimestampClient(std::shared_ptr<const PDFTimestampTransport> transport, int timeoutMilliseconds) :
    m_transport(std::move(transport)),
    m_timeoutMilliseconds(timeoutMilliseconds > 0 ? timeoutMilliseconds : TIMEOUT_MILLISECONDS)
{

}

PDFTimestampClient::~PDFTimestampClient() = default;

std::shared_ptr<const PDFTimestampClient> PDFTimestampClient::createNetworkClient()
{
    return std::make_shared<const PDFTimestampClient>(createNetworkTransport());
}

std::shared_ptr<const PDFTimestampTransport> PDFTimestampClient::createNetworkTransport()
{
    return std::make_shared<const PDFTimestampNetworkTransport>();
}

bool PDFTimestampClient::validateUrl(const QString& url, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
        return false;
    };

    // Figma 435:7693
    const QString invalidText = tr("Address of the timestamp authority is not valid. It needs a scheme and a host, for example https://tsa.example.com/tsr.");

    if (url.size() > MAXIMUM_URL_LENGTH)
    {
        return setError(tr("Address of the timestamp authority is too long (at most %1 characters).").arg(MAXIMUM_URL_LENGTH));
    }

    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty() || trimmedUrl != url)
    {
        return setError(invalidText);
    }

    const QUrl parsedUrl(url, QUrl::StrictMode);
    if (!parsedUrl.isValid() || parsedUrl.isRelative() || parsedUrl.isLocalFile())
    {
        return setError(invalidText);
    }

    const QString scheme = parsedUrl.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
    {
        return setError(invalidText);
    }

    if (parsedUrl.host().isEmpty())
    {
        return setError(invalidText);
    }

    // A user name or a password would be sent to the authority and shown in the texts (S6.6, R6-28);
    // also an empty one ("http://@host") - the authority part of the text is checked as typed
    const qsizetype authorityStart = url.indexOf(QLatin1String("//")) + 2;
    if (authorityStart < 2)
    {
        return setError(invalidText);
    }

    qsizetype authorityEnd = url.size();
    for (const QChar delimiter : { QLatin1Char('/'), QLatin1Char('?'), QLatin1Char('#') })
    {
        const qsizetype index = url.indexOf(delimiter, authorityStart);
        if (index >= 0 && index < authorityEnd)
        {
            authorityEnd = index;
        }
    }

    if (!parsedUrl.userInfo().isEmpty() || url.mid(authorityStart, authorityEnd - authorityStart).contains(QLatin1Char('@')))
    {
        return setError(tr("Address of the timestamp authority must not contain a user name or a password."));
    }

    if (!parsedUrl.fragment().isNull())
    {
        return setError(invalidText);
    }

    if (errorMessage)
    {
        errorMessage->clear();
    }
    return true;
}

QString PDFTimestampClient::getDisplayUrl(const QUrl& url)
{
    // RS6SEC Low of step 6: the query and the fragment may carry an API key of the authority, which
    // would reach the texts of the errors, MCP and its activity - only the scheme, the host, the port
    // and the path are shown
    return url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QStringList PDFTimestampClient::getPresetUrls()
{
    // Figma 434:6241, the old signing dialog (signdialog.cpp): the first one is the default
    return { QStringLiteral("http://timestamp.digicert.com"), QStringLiteral("http://timestamp.sectigo.com"), QStringLiteral("https://freetsa.org/tsr") };
}

PDFTimestampClient::Result PDFTimestampClient::request(const QString& url, const QByteArray& digest, const PDFOperationControl* control) const
{
    using Helper = PDFTimestampClientHelper;

    Result result;
    QString urlError;
    if (!validateUrl(url, &urlError))
    {
        result.status = Status::InvalidUrl;
        result.errorMessage = urlError;
        return result;
    }

    const QUrl parsedUrl(url, QUrl::StrictMode);
    const QString displayUrl = getDisplayUrl(parsedUrl);

    if (digest.size() != DIGEST_LENGTH)
    {
        result.status = Status::InvalidDigest;
        result.errorMessage = tr("The hash of the timestamped data is not a SHA-256 hash.");
        return result;
    }

    if (!m_transport)
    {
        result.status = Status::NoTransport;
        result.errorMessage = tr("The timestamp authority cannot be contacted: the application has no network access for it.");
        return result;
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.status = Status::Canceled;
        result.errorMessage = tr("The request of the timestamp authority was canceled.");
        return result;
    }

    // B125: no error of an earlier call is taken for an error of this one
    ERR_clear_error();

    auto invalidResponse = [&result](const QString& text)
    {
        ERR_clear_error();
        result = Result();
        result.status = Status::InvalidResponse;
        result.errorMessage = text;
        return result;
    };

    Helper::openssl_ptr<TS_REQ> request(Helper::createRequest(digest), &TS_REQ_free);
    const QByteArray requestData = request ? Helper::encode<TS_REQ>(request.get(), &i2d_TS_REQ) : QByteArray();
    if (requestData.isEmpty())
    {
        return invalidResponse(tr("The request of the timestamp authority cannot be created."));
    }

    PDFTimestampTransport::Request transportRequest;
    transportRequest.url = parsedUrl;
    transportRequest.body = requestData;
    transportRequest.maximumResponseSize = MAXIMUM_RESPONSE_SIZE;
    transportRequest.timeoutMilliseconds = m_timeoutMilliseconds;

    const PDFTimestampTransport::Response response = m_transport->post(transportRequest, control);
    switch (response.error)
    {
        case PDFTimestampTransport::Error::None:
            break;

        case PDFTimestampTransport::Error::Timeout:
        {
            // Figma 434:8518
            const int seconds = (m_timeoutMilliseconds + 999) / 1000;
            result.status = Status::Timeout;
            result.errorMessage = tr("%1 did not answer within %Ln second(s).", nullptr, seconds).arg(displayUrl);
            return result;
        }

        case PDFTimestampTransport::Error::TooLarge:
            return invalidResponse(tr("The answer of the timestamp authority “%1” is larger than %2 bytes.").arg(displayUrl).arg(MAXIMUM_RESPONSE_SIZE));

        case PDFTimestampTransport::Error::Redirected:
            result.status = Status::HttpError;
            result.httpStatus = response.httpStatus;
            result.errorMessage = tr("Timestamp authority “%1” answered by a redirection (http status code %2), which is not followed.").arg(displayUrl).arg(response.httpStatus);
            return result;

        case PDFTimestampTransport::Error::Canceled:
            result.status = Status::Canceled;
            result.errorMessage = tr("The request of the timestamp authority was canceled.");
            return result;

        case PDFTimestampTransport::Error::NoNetwork:
        case PDFTimestampTransport::Error::Tls:
        case PDFTimestampTransport::Error::Other:
            result.status = Status::NoNetwork;
            result.errorMessage = tr("Timestamp authority “%1” cannot be contacted. %2").arg(displayUrl, response.errorMessage);
            return result;
    }

    if (response.httpStatus != 200)
    {
        // Figma 435:8117
        result.status = Status::HttpError;
        result.httpStatus = response.httpStatus;
        result.errorMessage = tr("Timestamp authority “%1” answered with the http status code %2.").arg(displayUrl).arg(response.httpStatus);
        return result;
    }

    if (!Helper::isTimestampReply(response.contentType))
    {
        return invalidResponse(tr("Timestamp authority “%1” did not answer by a timestamp response.").arg(displayUrl));
    }

    if (response.body.isEmpty() || response.body.size() > MAXIMUM_RESPONSE_SIZE)
    {
        return invalidResponse(tr("Answer of the timestamp authority “%1” is not a timestamp response.").arg(displayUrl));
    }

    // DER of exactly the length of the body (plan 7.2): no bytes after the response
    const unsigned char* responsePointer = reinterpret_cast<const unsigned char*>(response.body.constData());
    const unsigned char* responseEnd = responsePointer + response.body.size();
    Helper::openssl_ptr<TS_RESP> timestampResponse(d2i_TS_RESP(nullptr, &responsePointer, long(response.body.size())), &TS_RESP_free);
    if (!timestampResponse || responsePointer != responseEnd)
    {
        return invalidResponse(tr("Answer of the timestamp authority “%1” is not a timestamp response.").arg(displayUrl));
    }

    TS_STATUS_INFO* statusInfo = TS_RESP_get_status_info(timestampResponse.get());
    const ASN1_INTEGER* status = statusInfo ? TS_STATUS_INFO_get0_status(statusInfo) : nullptr;
    const long statusValue = status ? ASN1_INTEGER_get(status) : -1;
    if (statusValue != TS_STATUS_GRANTED && statusValue != TS_STATUS_GRANTED_WITH_MODS)
    {
        ERR_clear_error();
        result.status = Status::Rejected;
        result.errorMessage = tr("Timestamp authority “%1” rejected the timestamp request, status is %2.").arg(displayUrl).arg(statusValue);
        return result;
    }

    // The token is owned by the response
    PKCS7* token = TS_RESP_get_token(timestampResponse.get());
    TS_TST_INFO* tokenInfo = TS_RESP_get_tst_info(timestampResponse.get());
    X509* authorityCertificate = Helper::getTokenSigner(token);
    if (!token || !tokenInfo || !authorityCertificate)
    {
        return invalidResponse(tr("Timestamp authority “%1” did not answer with a timestamp token with the certificate of the authority.").arg(displayUrl));
    }

    // RFC 3161, 2.4.2: the version, the imprint (SHA-256 of the request and its hash), the nonce of
    // this request, the signature of the token, the certificate identifier (ESS) and the purpose of the
    // certificate of the authority (the only and critical extended key usage timeStamping, 2.3)
    Helper::openssl_ptr<TS_VERIFY_CTX> verifyContext(TS_REQ_to_TS_VERIFY_CTX(request.get(), nullptr), &TS_VERIFY_CTX_free);
    if (!verifyContext || !Helper::setTokenStore(verifyContext.get(), token))
    {
        return invalidResponse(tr("Timestamp token of the authority “%1” cannot be verified.").arg(displayUrl));
    }

    TS_VERIFY_CTX_add_flags(verifyContext.get(), TS_VFY_SIGNATURE);
    if (TS_RESP_verify_response(verifyContext.get(), timestampResponse.get()) != 1)
    {
        return invalidResponse(tr("Timestamp token of the authority “%1” does not belong to the timestamp request or its signature is not valid.").arg(displayUrl));
    }

    // genTime at most 10 minutes after the clock of the computer (5.2)
    const ASN1_GENERALIZEDTIME* tokenTime = TS_TST_INFO_get_time(tokenInfo);
    int days = 0;
    int seconds = 0;
    if (!tokenTime || ASN1_TIME_diff(&days, &seconds, nullptr, tokenTime) != 1 ||
        qint64(days) * 86400 + seconds > MAXIMUM_TIME_AHEAD_SECONDS)
    {
        return invalidResponse(tr("The time of the timestamp of the authority “%1” is in the future.").arg(displayUrl));
    }

    result.time = Helper::getDateTime(tokenTime);
    result.token = Helper::encode<PKCS7>(token, &i2d_PKCS7);
    result.authorityName = Helper::getAuthorityName(authorityCertificate);
    if (result.token.isEmpty() || !result.time.isValid())
    {
        return invalidResponse(tr("Timestamp token of the authority “%1” cannot be encoded.").arg(displayUrl));
    }

    ERR_clear_error();
    result.status = Status::Granted;
    result.httpStatus = response.httpStatus;
    return result;
}

bool PDFTimestampClient::verifyToken(const QByteArray& data, const QByteArray& token)
{
    using Helper = PDFTimestampClientHelper;

    ERR_clear_error();
    const unsigned char* tokenPointer = reinterpret_cast<const unsigned char*>(token.constData());
    Helper::openssl_ptr<PKCS7> tokenObject(d2i_PKCS7(nullptr, &tokenPointer, long(token.size())), &PKCS7_free);
    Helper::openssl_ptr<TS_VERIFY_CTX> verifyContext(TS_VERIFY_CTX_new(), &TS_VERIFY_CTX_free);
    if (!tokenObject || !verifyContext || !Helper::getTokenCertificates(tokenObject.get()))
    {
        ERR_clear_error();
        return false;
    }

    // The certificate, which signed the token, is the only trusted one: the signature of the token, the
    // certificate identifier stored in it and the extended key usage of the certificate of the authority
    // are verified, its issuers are not (they are usually not all present in the token, and verifying
    // them here would mean deciding the trust)
    if (!Helper::setTokenStore(verifyContext.get(), tokenObject.get()))
    {
        ERR_clear_error();
        return false;
    }

    BIO* dataBuffer = BIO_new_mem_buf(data.constData(), int(data.size()));
    if (!dataBuffer)
    {
        ERR_clear_error();
        return false;
    }

    Helper::setVerifyData(verifyContext.get(), dataBuffer);
    TS_VERIFY_CTX_set_flags(verifyContext.get(), TS_VFY_SIGNATURE | TS_VFY_VERSION | TS_VFY_DATA);

    const bool isValid = TS_RESP_verify_token(verifyContext.get(), tokenObject.get()) == 1;
    ERR_clear_error();
    return isValid;
}

}   // namespace pdf

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
