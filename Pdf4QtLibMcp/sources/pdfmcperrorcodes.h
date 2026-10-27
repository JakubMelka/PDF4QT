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

#ifndef PDFMCPERRORCODES_H
#define PDFMCPERRORCODES_H

#include "pdf4qtlibmcp_export.h"

#include <QtGlobal>

namespace pdf
{

/// Error codes of JSON-RPC and MCP (plan of step 7, 1.2 and 1.4; spec
/// 2026-07-28 basic/index "Error codes") and their HTTP statuses
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpErrorCodes
{
public:
    PDFMcpErrorCodes() = delete;

    // JSON-RPC 2.0
    static constexpr int PARSE_ERROR = -32700;          ///< Invalid JSON, invalid UTF-8
    static constexpr int INVALID_REQUEST = -32600;      ///< Not a request object, invalid id, duplicate key, batch
    static constexpr int METHOD_NOT_FOUND = -32601;     ///< Unknown method (modern: with HTTP 404)
    static constexpr int INVALID_PARAMS = -32602;       ///< Invalid parameters, unknown tool, resource not found (modern)
    static constexpr int INTERNAL_ERROR = -32603;

    // MCP
    static constexpr int RESOURCE_NOT_FOUND_LEGACY = -32002;    ///< Resource not found of 2025-11-25 and earlier
    static constexpr int HEADER_MISMATCH = -32020;              ///< HTTP headers do not match the body, or are missing / malformed
    static constexpr int MISSING_CLIENT_CAPABILITY = -32021;    ///< data.requiredCapabilities
    static constexpr int UNSUPPORTED_PROTOCOL_VERSION = -32022; ///< data { supported, requested }

    // PDF4QT (outside of the reserved range, plan 1.4)
    static constexpr int RATE_LIMITED = -31001;     ///< HTTP 429, data.retryAfterMs
    static constexpr int SERVER_STOPPING = -31002;  ///< HTTP 503

    /// HTTP status of a JSON-RPC error, which is not tied to a header, version,
    /// capability or method: 200 (plan 1.4). The tied ones: HEADER_MISMATCH,
    /// MISSING_CLIENT_CAPABILITY, UNSUPPORTED_PROTOCOL_VERSION, PARSE_ERROR and
    /// INVALID_REQUEST 400, METHOD_NOT_FOUND 404 (modern), RATE_LIMITED 429,
    /// SERVER_STOPPING 503.
    static int getHttpStatus(int errorCode)
    {
        switch (errorCode)
        {
            case PARSE_ERROR:
            case INVALID_REQUEST:
            case HEADER_MISMATCH:
            case MISSING_CLIENT_CAPABILITY:
            case UNSUPPORTED_PROTOCOL_VERSION:
                return 400;
            case METHOD_NOT_FOUND:
                return 404;
            case RATE_LIMITED:
                return 429;
            case SERVER_STOPPING:
                return 503;
            default:
                return 200;
        }
    }
};

}   // namespace pdf

#endif // PDFMCPERRORCODES_H
