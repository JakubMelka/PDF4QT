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

#ifndef PDFTIMESTAMPCLIENT_H
#define PDFTIMESTAMPCLIENT_H

#include "pdfglobal.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace pdf
{

class PDFOperationControl;

/// The transport of a request to an RFC 3161 timestamp authority (plan of step 6 of phase 2, 6.6,
/// 10.3; package P1b): one HTTP POST, nothing else. The network transport of the application
/// (PDFTimestampClient::createNetworkClient) and a local test authority of a test (no internet in a
/// test, plan 9) implement it.
///
/// Contract of an implementation:
///  - Threads: post is called by the thread of the run of a tool (never the GUI thread, no nested
///    event loop of the GUI, B120); it blocks until the answer, the limits or the cancellation. An
///    implementation is immutable after its construction: one instance serves the runs of more threads
///    at once (each call has its own network objects in its thread).
///  - Limits: the whole exchange at most Request::timeoutMilliseconds; the body of the answer at most
///    Request::maximumResponseSize bytes (the transfer is aborted, when Content-Length or the received
///    bytes exceed it); no redirection is followed (ManualRedirectPolicy - a redirection is an error);
///    TLS with the verification of the certificate of the server, never ignoreSslErrors; no cookies,
///    no authentication, the header User-Agent "PDF4QT" only (no version, no name of the user); the
///    proxy of the system by the own QNetworkAccessManager (setProxyFactory, never the global
///    QNetworkProxyFactory::setUseSystemConfiguration, R6-28).
///  - Cancellation: \p control is polled at least every 100 ms; a canceled request is aborted.
///  - The body of the request is a TimeStampReq only (a hash of the document, a nonce, certReq): no
///    secret of the user ever goes through a transport.
class PDF4QTLIBCORESHARED_EXPORT PDFTimestampTransport
{
public:
    virtual ~PDFTimestampTransport();

    /// What went wrong with an exchange
    enum class Error
    {
        None,           ///< An answer of the server came (its HTTP status may still be an error)
        NoNetwork,      ///< The host cannot be reached (no network, a name, which does not resolve)
        Timeout,        ///< The time limit has passed
        TooLarge,       ///< The answer is larger than the limit
        Redirected,     ///< The server answered by a redirection (never followed)
        Tls,            ///< The certificate of the server of an https address is not valid
        Canceled,       ///< The run was stopped
        Other           ///< Another error of the transport (its text is in Response::errorMessage)
    };

    /// One request
    struct Request
    {
        QUrl url;                       ///< The address of the authority (PDFTimestampClient::validateUrl has accepted it)
        QByteArray body;                ///< DER of the TimeStampReq
        QByteArray contentType = "application/timestamp-query";
        qint64 maximumResponseSize = 0; ///< Largest body of the answer in bytes
        int timeoutMilliseconds = 0;    ///< The whole exchange at most
    };

    /// The answer of the server
    struct Response
    {
        Error error = Error::None;
        int httpStatus = 0;             ///< The HTTP status (0 without an answer)
        QByteArray contentType;         ///< The value of Content-Type of the answer
        QByteArray body;                ///< The body of the answer (DER of a TimeStampResp)
        QString errorMessage;           ///< Translated text of the error of the transport (never the address with its user info)
    };

    /// Sends the request and waits for the answer (see the class)
    /// \param request The request
    /// \param control Cancellation (may be null)
    virtual Response post(const Request& request, const PDFOperationControl* control) const = 0;
};

/// The client of an RFC 3161 timestamp authority (plan of step 6 of phase 2, 4.1, 5.2, 6.6, 10.3;
/// package P1b implements it, moving createTimestampToken of PDFSignatureFactory here). It builds the
/// TimeStampReq of a SHA-256 hash (imprint, a nonce of RAND_bytes - B133 -, certReq), sends it by its
/// transport and checks the TimeStampResp: the status granted, the nonce, the imprint, the signature of
/// the authority, the critical extended key usage timeStamping of its certificate (RFC 3161, 2.3) and
/// genTime at most 10 minutes after the clock of the computer. The trust in the authority is not
/// decided here (a fact of the result, "Authority not trusted"; the run goes on).
///
/// A plain immutable value (no QObject), shared by the context of a run
/// (PDFDocumentToolContext::timestampClient) and used only by the thread of the run. Network access
/// happens only by request, which only the run of Sign with a timestamp and of Add Timestamp calls -
/// never at the opening of a page, a choice of an address or a validation (S6.6).
class PDF4QTLIBCORESHARED_EXPORT PDFTimestampClient
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFTimestampClient)

public:
    /// Time limit of the whole exchange (Figma 434:8518 "15 s"; a test scales it by
    /// PDF4QT_TEST_TIMEOUT_FACTOR, never the application)
    static constexpr int TIMEOUT_MILLISECONDS = 15000;

    /// Largest answer of an authority (plan 7.2)
    static constexpr qint64 MAXIMUM_RESPONSE_SIZE = 1024 * 1024;

    /// Longest address of an authority (S6.6)
    static constexpr int MAXIMUM_URL_LENGTH = 2048;

    /// Length of a SHA-256 hash, the only imprint of a request
    static constexpr int DIGEST_LENGTH = 32;

    /// Length of the nonce of a request in bytes (64 bits of RAND_bytes, B133; RFC 3161, 2.4.1)
    static constexpr int NONCE_LENGTH = 8;

    /// genTime of a token may be at most this many seconds after the clock of the computer (5.2)
    static constexpr int MAXIMUM_TIME_AHEAD_SECONDS = 10 * 60;

    /// The outcome of a request
    enum class Status
    {
        Granted,            ///< The token is valid for the hash (status granted or grantedWithMods)
        InvalidUrl,         ///< The address is not valid (validateUrl)
        InvalidDigest,      ///< The hash is not 32 bytes long
        NoTransport,        ///< The client has no transport
        NoNetwork,          ///< The authority cannot be contacted ("No network connection", 434:8518)
        Timeout,            ///< The authority did not answer in time
        HttpError,          ///< The authority answered by an HTTP status other than 200 (435:8117 "HTTP 503")
        InvalidResponse,    ///< The answer is no valid TimeStampResp for the request (Content-Type, DER, nonce, imprint, signature, extended key usage, time)
        Rejected,           ///< The authority refused the request (PKIStatus rejection / waiting / revocation)
        Canceled            ///< The run was stopped
    };

    /// The result of request
    struct Result
    {
        Status status = Status::NoTransport;
        QByteArray token;           ///< DER of the TimeStampToken (ContentInfo of the SignedData), Granted only
        QDateTime time;             ///< genTime of the token (UTC), Granted only
        QString authorityName;      ///< The common name of the certificate of the authority (the fact Authority, 434:9276), Granted only
        int httpStatus = 0;         ///< The HTTP status of an answer (HttpError)
        QString errorMessage;       ///< Translated reason of a failure: never the address with its user info, never a secret

        bool isGranted() const { return status == Status::Granted; }
    };

    /// \param transport The transport of the requests (null = none: every request fails with NoTransport)
    explicit PDFTimestampClient(std::shared_ptr<const PDFTimestampTransport> transport);

    /// \param transport The transport of the requests (null = none: every request fails with NoTransport)
    /// \param timeoutMilliseconds The time limit of the whole exchange (a test scales TIMEOUT_MILLISECONDS
    ///        by PDF4QT_TEST_TIMEOUT_FACTOR; the old applications give the timeout of their settings);
    ///        0 or less = TIMEOUT_MILLISECONDS
    PDFTimestampClient(std::shared_ptr<const PDFTimestampTransport> transport, int timeoutMilliseconds);
    ~PDFTimestampClient();

    /// The time limit of the whole exchange of a request in milliseconds
    int getTimeoutMilliseconds() const { return m_timeoutMilliseconds; }

    /// The client of the application: the network transport over its own QNetworkAccessManager in
    /// the thread of the run (package P1b)
    static std::shared_ptr<const PDFTimestampClient> createNetworkClient();

    /// The network transport of createNetworkClient (see PDFTimestampTransport): one HTTP POST over an
    /// own QNetworkAccessManager created in the calling thread, a local event loop of that thread
    /// (QEventLoop::ExcludeUserInputEvents), no redirection, no cookies, no authentication, the proxy
    /// of the system by its own proxy factory (never for a loopback address)
    static std::shared_ptr<const PDFTimestampTransport> createNetworkTransport();

    /// True, if the token is a correctly signed RFC 3161 timestamp of the data (moved here from
    /// PDFSignatureFactory::verifyTimestampToken, which calls it): the signature of the token, the
    /// certificate identifier stored in it, the extended key usage timeStamping of the certificate of
    /// the authority (X509_PURPOSE_TIMESTAMP_SIGN of OpenSSL: the only and critical one, RFC 3161, 2.3)
    /// and the message imprint of the data. The authority is not trusted here (its certificate ends
    /// the chain) - the trust is decided when the signatures of a document are verified.
    /// \param data The timestamped data
    /// \param token DER of the TimeStampToken
    static bool verifyToken(const QByteArray& data, const QByteArray& token);

    /// The address is a valid address of an authority (S6.6): only http and https, a host is
    /// required, no user info, the port optional, at most MAXIMUM_URL_LENGTH characters. An address
    /// stored in the database is untrusted input too. A path of a file system is never accepted.
    /// \param url The address as typed
    /// \param errorMessage Translated reason (435:7693 "Address of the timestamp authority is not
    ///        valid …"), may be null
    static bool validateUrl(const QString& url, QString* errorMessage);

    /// The address for a text shown to the user: without its user info and its password (R6-28) and
    /// without its query and fragment (an API key of the authority): the scheme, the host, the port, the path
    static QString getDisplayUrl(const QUrl& url);

    /// The addresses offered by the application (Figma 434:6241 and the old signing dialog):
    /// http://timestamp.digicert.com (the default), http://timestamp.sectigo.com,
    /// https://freetsa.org/tsr. The token is signed and holds the nonce, so http does not harm the
    /// integrity (K6.20).
    static QStringList getPresetUrls();

    /// Asks the authority for the timestamp of the hash: one request (no retry - a second contact of
    /// the authority only by a new call, at most one more after a too small reservation, R6-28)
    /// \param url The address of the authority (validated again here)
    /// \param digest SHA-256 of the timestamped data (DIGEST_LENGTH bytes)
    /// \param control Cancellation of the run (may be null)
    Result request(const QString& url, const QByteArray& digest, const PDFOperationControl* control) const;

private:
    std::shared_ptr<const PDFTimestampTransport> m_transport;
    int m_timeoutMilliseconds = TIMEOUT_MILLISECONDS;
};

}   // namespace pdf

#endif // PDFTIMESTAMPCLIENT_H
