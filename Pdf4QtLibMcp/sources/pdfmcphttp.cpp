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

#include "pdfmcphttp.h"
#include "pdfmcpjson.h"

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpHttpRequest
// ----------------------------------------------------------------------------

QByteArray PDFMcpHttpRequest::getHeader(QByteArrayView name) const
{
    for (const auto& header : headers)
    {
        if (header.first == name)
        {
            return header.second;
        }
    }
    return QByteArray();
}

int PDFMcpHttpRequest::getHeaderCount(QByteArrayView name) const
{
    int count = 0;
    for (const auto& header : headers)
    {
        if (header.first == name)
        {
            ++count;
        }
    }
    return count;
}

bool PDFMcpHttpRequest::isConnectionClose() const
{
    for (const auto& header : headers)
    {
        if (header.first == "connection")
        {
            const QList<QByteArray> options = header.second.split(',');
            for (const QByteArray& option : options)
            {
                if (option.trimmed().compare("close", Qt::CaseInsensitive) == 0)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// PDFMcpHttpResponse
// ----------------------------------------------------------------------------

PDFMcpHttpResponse PDFMcpHttpResponse::createJsonRpcError(int status, int code, const QString& message)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("jsonrpc"), QLatin1StringView("2.0"));
    writer.key(QLatin1StringView("error"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("code"), code);
    writer.writeMember(QLatin1StringView("message"), QStringView(message));
    writer.endObject();
    writer.endObject();

    PDFMcpHttpResponse response;
    response.status = status;
    response.headers.emplace_back(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
    response.body = writer.take();
    return response;
}

QByteArray PDFMcpHttpResponse::getReasonPhrase(int status)
{
    switch (status)
    {
        case 200: return QByteArrayLiteral("OK");
        case 202: return QByteArrayLiteral("Accepted");
        case 400: return QByteArrayLiteral("Bad Request");
        case 401: return QByteArrayLiteral("Unauthorized");
        case 403: return QByteArrayLiteral("Forbidden");
        case 404: return QByteArrayLiteral("Not Found");
        case 405: return QByteArrayLiteral("Method Not Allowed");
        case 406: return QByteArrayLiteral("Not Acceptable");
        case 408: return QByteArrayLiteral("Request Timeout");
        case 411: return QByteArrayLiteral("Length Required");
        case 413: return QByteArrayLiteral("Content Too Large");
        case 415: return QByteArrayLiteral("Unsupported Media Type");
        case 417: return QByteArrayLiteral("Expectation Failed");
        case 429: return QByteArrayLiteral("Too Many Requests");
        case 431: return QByteArrayLiteral("Request Header Fields Too Large");
        case 500: return QByteArrayLiteral("Internal Server Error");
        case 503: return QByteArrayLiteral("Service Unavailable");
        case 505: return QByteArrayLiteral("HTTP Version Not Supported");
        default: return QByteArrayLiteral("Unknown");
    }
}

QByteArray PDFMcpHttpResponse::getServerHeader(const QByteArray& serverVersion)
{
    return QByteArrayLiteral("PDF4QT/") + serverVersion;
}

QByteArray PDFMcpHttpResponse::serializeHead(const QByteArray& serverVersion) const
{
    QByteArray data;
    data.reserve(body.size() + 256);
    data += "HTTP/1.1 " + QByteArray::number(status) + ' ' + getReasonPhrase(status) + "\r\n";
    data += "Server: " + getServerHeader(serverVersion) + "\r\n";
    data += "Cache-Control: no-store\r\n";
    data += "X-Content-Type-Options: nosniff\r\n";
    for (const auto& header : headers)
    {
        // Defense in depth: no header of the server can split the response
        const QByteArray name = removeLineBreaks(header.first);
        if (!name.isEmpty())
        {
            data += name + ": " + removeLineBreaks(header.second) + "\r\n";
        }
    }
    return data;
}

QByteArray PDFMcpHttpResponse::removeLineBreaks(const QByteArray& text)
{
    QByteArray result;
    result.reserve(text.size());
    for (char c : text)
    {
        if (c != '\r' && c != '\n' && c != '\0')
        {
            result.append(c);
        }
    }
    return result;
}

QByteArray PDFMcpHttpResponse::serialize(const QByteArray& serverVersion) const
{
    QByteArray data = serializeHead(serverVersion);
    data += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    if (closeConnection)
    {
        data += "Connection: close\r\n";
    }
    data += "\r\n";
    data += body;
    return data;
}

QByteArray PDFMcpHttpResponse::serializeStreamHead(const QByteArray& serverVersion) const
{
    QByteArray data = serializeHead(serverVersion);
    data += "Content-Type: text/event-stream\r\n";
    data += "X-Accel-Buffering: no\r\n";
    data += "Transfer-Encoding: chunked\r\n";
    if (closeConnection)
    {
        data += "Connection: close\r\n";
    }
    data += "\r\n";
    return data;
}

// ----------------------------------------------------------------------------
// PDFMcpHttpParser
// ----------------------------------------------------------------------------

bool PDFMcpHttpParser::isTokenCharacter(char c)
{
    // tchar of RFC 9110 5.6.2
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    {
        return true;
    }

    switch (c)
    {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
        case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool PDFMcpHttpParser::isToken(QByteArrayView text)
{
    if (text.isEmpty())
    {
        return false;
    }

    for (char c : text)
    {
        if (!isTokenCharacter(c))
        {
            return false;
        }
    }
    return true;
}

PDFMcpHttpParser::Status PDFMcpHttpParser::fail(int status, const char* message)
{
    m_state = State::Error;
    m_errorStatus = status;
    m_errorMessage = QString::fromLatin1(message);
    return Status::Error;
}

PDFMcpHttpParser::Status PDFMcpHttpParser::feed(QByteArrayView data)
{
    if (m_state == State::Error)
    {
        return Status::Error;
    }

    if (!data.isEmpty())
    {
        m_buffer.append(data);
    }
    return parse();
}

PDFMcpHttpParser::Status PDFMcpHttpParser::parse()
{
    switch (m_state)
    {
        case State::Error:
            return Status::Error;
        case State::Complete:
            return Status::RequestComplete;
        case State::Head:
        case State::Body:
            break;
    }

    while (m_state == State::Head)
    {
        if (m_position < m_buffer.size())
        {
            m_hasStarted = true;
        }

        const qsizetype lineEnd = m_buffer.indexOf('\n', m_position);
        if (lineEnd < 0)
        {
            // The unfinished line counts into the head
            if (m_headSize + (m_buffer.size() - m_position) > m_limits.maximumHeaderSize)
            {
                return fail(431, "The head of the request is too large");
            }
            return Status::NeedMoreData;
        }

        const qsizetype lineSize = lineEnd + 1 - m_position;
        m_headSize += lineSize;
        if (m_headSize > m_limits.maximumHeaderSize)
        {
            return fail(431, "The head of the request is too large");
        }

        // Every line ends by CR LF (a bare LF is refused)
        if (lineEnd == m_position || m_buffer[lineEnd - 1] != '\r')
        {
            return fail(400, "A line of the head does not end by CR LF");
        }

        const QByteArrayView line(m_buffer.constData() + m_position, lineEnd - 1 - m_position);
        m_position = lineEnd + 1;

        for (char c : line)
        {
            if (c == '\r' || c == '\0')
            {
                return fail(400, "A line of the head contains CR or NUL");
            }
        }

        if (line.isEmpty())
        {
            if (!m_hasRequestLine)
            {
                // Empty lines before the request line are ignored (RFC 9112 2.2)
                continue;
            }

            Status status = finishHead();
            if (status == Status::Error)
            {
                return status;
            }
            break;
        }

        const Status status = m_hasRequestLine ? parseHeaderLine(line) : parseRequestLine(line);
        if (status == Status::Error)
        {
            return status;
        }
    }

    if (m_state == State::Body)
    {
        if (m_buffer.size() - m_position < m_bodySize)
        {
            return Status::NeedMoreData;
        }

        m_request.body = m_buffer.mid(m_position, m_bodySize);
        m_position += m_bodySize;
        m_state = State::Complete;
    }

    return m_state == State::Complete ? Status::RequestComplete : Status::NeedMoreData;
}

PDFMcpHttpParser::Status PDFMcpHttpParser::parseRequestLine(QByteArrayView line)
{
    const qsizetype first = line.indexOf(' ');
    if (first <= 0)
    {
        return fail(400, "Invalid request line");
    }

    const qsizetype second = line.indexOf(' ', first + 1);
    if (second < 0 || second == first + 1 || line.indexOf(' ', second + 1) >= 0)
    {
        return fail(400, "Invalid request line");
    }

    const QByteArrayView method = line.first(first);
    const QByteArrayView target = line.sliced(first + 1, second - first - 1);
    const QByteArrayView version = line.sliced(second + 1);

    if (!isToken(method) || method.size() > 32)
    {
        return fail(400, "Invalid method");
    }

    if (target.size() > m_limits.maximumTargetLength)
    {
        return fail(431, "The request target is too long");
    }

    for (char c : target)
    {
        const uchar byte = uchar(c);
        if (byte < 0x21 || byte > 0x7E)
        {
            return fail(400, "Invalid request target");
        }
    }

    // HTTP-version = "HTTP/" DIGIT "." DIGIT
    if (version.size() != 8 || !version.startsWith("HTTP/") || version[5] < '0' || version[5] > '9' || version[6] != '.' || version[7] < '0' || version[7] > '9')
    {
        return fail(400, "Invalid HTTP version");
    }

    if (version != "HTTP/1.1")
    {
        return fail(505, "Only HTTP/1.1 is supported");
    }

    m_request.method = method.toByteArray();
    m_request.target = target.toByteArray();
    m_request.version = version.toByteArray();
    m_hasRequestLine = true;
    return Status::NeedMoreData;
}

PDFMcpHttpParser::Status PDFMcpHttpParser::parseHeaderLine(QByteArrayView line)
{
    // obs-fold (RFC 9112 5.2) is refused
    if (line[0] == ' ' || line[0] == '\t')
    {
        return fail(400, "Folded header lines are not accepted");
    }

    const qsizetype colon = line.indexOf(':');
    if (colon <= 0)
    {
        return fail(400, "Invalid header field");
    }

    // No white space between the name and the colon (RFC 9112 5.1)
    const QByteArrayView name = line.first(colon);
    if (!isToken(name))
    {
        return fail(400, "Invalid header field name");
    }

    QByteArrayView value = line.sliced(colon + 1);
    for (char c : value)
    {
        const uchar byte = uchar(c);
        if ((byte < 0x20 && byte != '\t') || byte == 0x7F)
        {
            return fail(400, "Invalid character in a header value");
        }
    }

    if (int(m_request.headers.size()) >= m_limits.maximumHeaderCount)
    {
        return fail(431, "Too many header fields");
    }

    value = value.trimmed();
    m_request.headers.emplace_back(name.toByteArray().toLower(), value.toByteArray());
    return Status::NeedMoreData;
}

PDFMcpHttpParser::Status PDFMcpHttpParser::finishHead()
{
    if (m_request.getHeaderCount("host") != 1)
    {
        return fail(400, "Exactly one Host header is required");
    }

    if (m_request.hasHeader("transfer-encoding"))
    {
        return fail(411, "Transfer-Encoding is not accepted; send Content-Length");
    }

    if (m_request.hasHeader("expect"))
    {
        return fail(417, "Expect is not supported");
    }

    m_bodySize = 0;
    const int contentLengthCount = m_request.getHeaderCount("content-length");
    if (contentLengthCount > 1)
    {
        return fail(400, "Repeated Content-Length");
    }

    if (contentLengthCount == 1)
    {
        const QByteArray text = m_request.getHeader("content-length");
        if (text.isEmpty() || text.size() > 18)
        {
            return fail(400, "Invalid Content-Length");
        }

        qint64 length = 0;
        for (char c : text)
        {
            if (c < '0' || c > '9')
            {
                return fail(400, "Invalid Content-Length");
            }
            length = length * 10 + (c - '0');
        }

        if (length > m_limits.maximumBodySize)
        {
            return fail(413, "The body of the request is too large");
        }
        m_bodySize = qsizetype(length);
    }
    else if (m_request.method == "POST")
    {
        return fail(411, "Content-Length is required");
    }

    m_state = State::Body;
    return Status::NeedMoreData;
}

PDFMcpHttpRequest PDFMcpHttpParser::takeRequest()
{
    PDFMcpHttpRequest request = std::move(m_request);
    m_request = PDFMcpHttpRequest();
    return request;
}

qsizetype PDFMcpHttpParser::getMissingBodySize() const
{
    if (m_state != State::Body)
    {
        return 0;
    }
    return qMax<qsizetype>(0, m_bodySize - (m_buffer.size() - m_position));
}

PDFMcpHttpRequest PDFMcpHttpParser::takeHead()
{
    PDFMcpHttpRequest request = std::move(m_request);
    request.body.clear();
    request.headers.emplace_back(QByteArrayLiteral("connection"), QByteArrayLiteral("close"));
    m_request = PDFMcpHttpRequest();

    // The rest of the input is never parsed (the body is not read)
    m_buffer.clear();
    m_position = 0;
    m_bodySize = 0;
    m_state = State::Error;
    m_errorStatus = 0;
    m_errorMessage.clear();
    return request;
}

void PDFMcpHttpParser::reset()
{
    // The consumed bytes are removed, a pipelined request stays
    if (m_state == State::Error)
    {
        m_buffer.clear();
    }
    else
    {
        m_buffer.remove(0, m_position);
    }

    m_position = 0;
    m_headSize = 0;
    m_bodySize = 0;
    m_state = State::Head;
    m_hasRequestLine = false;
    m_hasStarted = false;
    m_request = PDFMcpHttpRequest();
    m_errorStatus = 0;
    m_errorMessage.clear();
}

void PDFMcpHttpParser::clear()
{
    m_buffer.clear();
    reset();
}

}   // namespace pdf
