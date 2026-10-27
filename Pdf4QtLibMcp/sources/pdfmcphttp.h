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

#ifndef PDFMCPHTTP_H
#define PDFMCPHTTP_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcplimits.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <utility>
#include <vector>

namespace pdf
{

// HTTP/1.1 layer of the MCP server (plan of step 7, 1.5): a strict incremental
// parser of the requests (RFC 9112 without the parts an MCP client never needs:
// no chunked request bodies, no obs-fold, no absolute-form targets) and the
// serialization of the responses, JSON and SSE (chunked).

/// One parsed request
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpHttpRequest
{
public:
    QByteArray method;                                      ///< "POST"
    QByteArray target;                                      ///< "/mcp"
    QByteArray version;                                     ///< "HTTP/1.1"
    std::vector<std::pair<QByteArray, QByteArray>> headers; ///< Names in lower case, values trimmed
    QByteArray body;

    /// Value of the first header of the name (lower case), empty = none
    QByteArray getHeader(QByteArrayView name) const;

    /// Number of the headers of the name
    int getHeaderCount(QByteArrayView name) const;

    /// The header of the name is present (also with an empty value)
    bool hasHeader(QByteArrayView name) const { return getHeaderCount(name) > 0; }

    /// The client asked to close the connection after the response (Connection: close)
    bool isConnectionClose() const;
};

/// One response (headers of every response are added by serialize)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpHttpResponse
{
public:
    int status = 200;
    std::vector<std::pair<QByteArray, QByteArray>> headers; ///< Additional headers (Content-Type, Mcp-Session-Id, WWW-Authenticate, Allow, ...)
    QByteArray body;
    bool closeConnection = false;   ///< "Connection: close" and the socket is closed after the write

    /// Response with a JSON-RPC error body {"jsonrpc":"2.0","id":null,"error":{code,message}}
    static PDFMcpHttpResponse createJsonRpcError(int status, int code, const QString& message);

    /// Serialized response: the status line, Server: PDF4QT/<version>,
    /// Cache-Control: no-store, X-Content-Type-Options: nosniff, the headers,
    /// Content-Length and Connection
    QByteArray serialize(const QByteArray& serverVersion) const;

    /// Serialized head of a streamed response (SSE): the headers of serialize,
    /// Content-Type: text/event-stream, X-Accel-Buffering: no and
    /// Transfer-Encoding: chunked instead of Content-Length (the body follows as
    /// chunks, the connection stays reusable)
    QByteArray serializeStreamHead(const QByteArray& serverVersion) const;

    /// Reason phrase of a status ("Service Unavailable")
    static QByteArray getReasonPhrase(int status);

    /// Value of the Server header of PDF4QT ("PDF4QT/1.6.0.0") - the probe of a
    /// port in use looks for its prefix "PDF4QT/" (plan 2.6)
    static QByteArray getServerHeader(const QByteArray& serverVersion);

    /// The text without CR, LF and NUL (headers never split a response)
    static QByteArray removeLineBreaks(const QByteArray& text);

private:
    QByteArray serializeHead(const QByteArray& serverVersion) const;
};

/// Limits of the parser (PDFMcpLimits by default; the tests lower them)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpHttpParserLimits
{
public:
    qsizetype maximumHeaderSize = PDFMcpLimits::MAXIMUM_HEADER_SIZE;
    int maximumHeaderCount = PDFMcpLimits::MAXIMUM_HEADER_COUNT;
    qsizetype maximumBodySize = PDFMcpLimits::MAXIMUM_BODY_SIZE;
    int maximumTargetLength = PDFMcpLimits::MAXIMUM_TARGET_LENGTH;
};

/// Strict incremental parser of HTTP/1.1 requests (plan 1.5). Pure: it gets the
/// bytes of a connection and gives the requests or an error status; the times
/// (408) are decided by the connection.
///
/// Rules (each violation is an error, the connection is answered and closed):
/// - lines end by CR LF; a bare LF, a bare CR or NUL is 400 (no smuggling by
///   ambiguous line ends); empty lines before the request line are ignored;
/// - request line "METHOD SP target SP HTTP/x.y" with single spaces, the method
///   a token, the target visible ASCII (at most maximumTargetLength) - 400;
///   another version than HTTP/1.1 is 505;
/// - header fields "name: value": the name a token directly followed by ':'
///   (white space before the colon is 400), obs-fold is 400, control
///   characters of a value are 400, the names are compared case-insensitively
///   (stored in lower case), the values are trimmed;
/// - the head larger than maximumHeaderSize or more than maximumHeaderCount
///   fields - 431;
/// - Host missing or repeated - 400; Transfer-Encoding (any) - 411; POST
///   without Content-Length - 411; Content-Length repeated or not a number -
///   400; larger than maximumBodySize - 413 (decided before the body is read);
///   Expect (any) - 417.
/// Bytes after a complete request stay buffered for the next one (reset).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpHttpParser
{
public:
    enum class Status
    {
        NeedMoreData,
        RequestComplete,
        Error
    };

    PDFMcpHttpParser() = default;
    explicit PDFMcpHttpParser(const PDFMcpHttpParserLimits& limits) : m_limits(limits) { }

    /// Appends received data and parses
    Status feed(QByteArrayView data);

    /// Parses the buffered data (the next pipelined request after reset)
    Status parse();

    /// Status of an error (431, 400, ...)
    int getErrorStatus() const { return m_errorStatus; }

    /// English text of the error
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// The parsed request (RequestComplete)
    const PDFMcpHttpRequest& getRequest() const { return m_request; }

    /// Takes the parsed request
    PDFMcpHttpRequest takeRequest();

    /// Bytes of the body of the current request, which have not been received yet
    /// (the head is complete)
    qsizetype getMissingBodySize() const;

    /// Takes the request after its head without its body (a request refused
    /// before its body is read); the parser stops - nothing more is parsed, the
    /// connection closes after the response
    PDFMcpHttpRequest takeHead();

    /// A byte of the current request was received (empty lines before the request
    /// line count too)
    bool hasStarted() const { return m_hasStarted; }

    /// The head of the current request is complete (the body is read)
    bool isHeadComplete() const { return m_state == State::Body || m_state == State::Complete; }

    /// Bytes buffered and not yet consumed
    qsizetype getBufferedSize() const { return m_buffer.size(); }

    /// Clears the state for the next request of the connection; the buffered
    /// bytes of a pipelined request are kept
    void reset();

    /// Clears everything
    void clear();

    const PDFMcpHttpParserLimits& getLimits() const { return m_limits; }

private:
    enum class State
    {
        Head,
        Body,
        Complete,
        Error
    };

    Status fail(int status, const char* message);
    Status parseRequestLine(QByteArrayView line);
    Status parseHeaderLine(QByteArrayView line);
    Status finishHead();

    static bool isTokenCharacter(char c);
    static bool isToken(QByteArrayView text);

    PDFMcpHttpParserLimits m_limits;
    QByteArray m_buffer;
    qsizetype m_position = 0;       ///< Parsed bytes of the buffer (head)
    qsizetype m_headSize = 0;       ///< Bytes of the head so far
    qsizetype m_bodySize = 0;       ///< Expected body size
    State m_state = State::Head;
    bool m_hasRequestLine = false;
    bool m_hasStarted = false;
    PDFMcpHttpRequest m_request;
    int m_errorStatus = 0;
    QString m_errorMessage;
};

}   // namespace pdf

#endif // PDFMCPHTTP_H
