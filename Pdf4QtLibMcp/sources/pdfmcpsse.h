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


#ifndef PDFMCPSSE_H
#define PDFMCPSSE_H

#include "pdf4qtlibmcp_export.h"

#include <QByteArray>
#include <QByteArrayView>

namespace pdf
{

/// Server-sent events of the responses and streams (plan 1.5): every event and
/// comment is one HTTP chunk (Transfer-Encoding: chunked keeps the connection
/// reusable). The output of the streams is queued by PDFMcpExchange, the
/// keep-alives and the backpressure are kept by the dispatcher and the connection.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSseWriter
{
public:
    PDFMcpSseWriter() = delete;

    /// An event "data: <json>" and an empty line as one chunk (the JSON is compact)
    static QByteArray createEvent(QByteArrayView json);

    /// A comment ": <text>" and an empty line as one chunk (keep-alive)
    static QByteArray createComment(QByteArrayView text);

    /// One chunk of the data (the size in hex, CR LF, the data, CR LF)
    static QByteArray createChunk(QByteArrayView data);

    /// The last chunk of a stream
    static QByteArray createLastChunk();
};

}   // namespace pdf

#endif // PDFMCPSSE_H
