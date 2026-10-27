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

#ifndef PDFMCPLIMITS_H
#define PDFMCPLIMITS_H

#include "pdf4qtlibmcp_export.h"

#include <QtGlobal>

namespace pdf
{

/// Limits of the MCP server (plan of step 7, chapters 1.5, 2.2 and 7). Every
/// limit of the network surface is a constant here, so the tests and the
/// documentation name one place. The values of a running server can be lowered
/// by PDFMcpServerConfig (tests), never raised beyond these constants.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpLimits
{
public:
    PDFMcpLimits() = delete;

    // JSON layer (2.2)

    /// Largest body of a request (decided from Content-Length before reading - 413)
    static constexpr qsizetype MAXIMUM_BODY_SIZE = qsizetype(8) * 1024 * 1024;

    /// Deepest nesting of arrays and objects
    static constexpr int MAXIMUM_JSON_DEPTH = 64;

    /// Most members of one object
    static constexpr int MAXIMUM_OBJECT_MEMBERS = 10000;

    /// Most items of one array
    static constexpr int MAXIMUM_ARRAY_ITEMS = 100000;

    /// Longest string (bytes of its UTF-8 encoding in the request)
    static constexpr qsizetype MAXIMUM_STRING_SIZE = qsizetype(4) * 1024 * 1024;

    /// Longest number token
    static constexpr int MAXIMUM_NUMBER_LENGTH = 64;

    /// Longest string id of a request (characters), echoed exactly
    static constexpr int MAXIMUM_ID_LENGTH = 256;

    // HTTP layer (1.5)

    /// Largest head of a request (request line and header fields) - 431
    static constexpr qsizetype MAXIMUM_HEADER_SIZE = qsizetype(16) * 1024;

    /// Most header fields - 431
    static constexpr int MAXIMUM_HEADER_COUNT = 64;

    /// The head of a request must be complete within this time (ms) - 408
    static constexpr int HEADER_TIMEOUT = 10000;

    /// The body of a request must be complete within this time after its head (ms) - 408
    static constexpr int BODY_TIMEOUT = 60000;

    /// Longest request target (bytes, part of the head)
    static constexpr int MAXIMUM_TARGET_LENGTH = 2048;

    /// Largest response (bytes)
    static constexpr qsizetype MAXIMUM_RESPONSE_SIZE = qsizetype(16) * 1024 * 1024;

    /// A connection, whose unsent data exceed this size, is closed (backpressure)
    static constexpr qsizetype MAXIMUM_PENDING_OUTPUT = qsizetype(32) * 1024 * 1024;

    /// A result ready within this time (ms) is sent as application/json, a later
    /// one switches the response to SSE
    static constexpr int JSON_RESPONSE_DELAY = 1000;

    /// Keep-alive comments of the SSE streams (ms)
    static constexpr int KEEP_ALIVE_INTERVAL = 15000;

    /// Longest value of traceparent / tracestate / baggage, which is accepted
    static constexpr int MAXIMUM_TRACE_HEADER_LENGTH = 512;

    /// Fixed pool of the I/O threads
    static constexpr int IO_THREAD_COUNT = 4;

    // Connections and calls (7)

    static constexpr int MAXIMUM_CONNECTIONS = 64;
    static constexpr int MAXIMUM_CONNECTIONS_PER_REMOTE_ADDRESS = 16;
    static constexpr int MAXIMUM_CALLS_PER_ASSISTANT = 16;
    static constexpr int MAXIMUM_CALLS = 64;
    static constexpr int MAXIMUM_HEAVY_JOBS = 2;
    static constexpr int RATE_BURST = 50;
    static constexpr int RATE_SUSTAINED = 20;
    static constexpr int MAXIMUM_LISTEN_STREAMS_PER_ASSISTANT = 8;
    static constexpr int MAXIMUM_LISTEN_STREAMS = 32;
    static constexpr int MAXIMUM_LEGACY_SESSIONS = 64;

    /// Legacy initialize of one address (a new session each): a burst of 16,
    /// then 2 per second - 429 beyond it (follow-up 36)
    static constexpr int INITIALIZE_BURST = 16;
    static constexpr int INITIALIZE_RATE = 2;

    /// Most connected assistants (modern keys and legacy sessions together); a
    /// modern request of a new key beyond it is refused (429), so a client cannot
    /// create unlimited identities by clientInfo.name - each with its own limits
    /// of the calls, streams, tasks and rates and a row of the page AI
    static constexpr int MAXIMUM_ASSISTANTS = 128;

    static constexpr int MAXIMUM_TASKS_PER_ASSISTANT = 64;
    static constexpr int MAXIMUM_TASKS = 256;
    static constexpr int MAXIMUM_SUBSCRIPTIONS_PER_STREAM = 256;
    static constexpr int MAXIMUM_COMPLETIONS_PER_SECOND = 20;

    /// Failed authorizations of one address within a minute, then 429 for a minute
    static constexpr int MAXIMUM_AUTHORIZATION_FAILURES = 20;

    /// Window of the failed authorizations and the time of the block (ms)
    static constexpr qint64 AUTHORIZATION_BLOCK_TIME = 60000;

    /// Most addresses and keys remembered by the rate limits (the oldest are forgotten)
    static constexpr int MAXIMUM_RATE_LIMIT_KEYS = 4096;

    /// Rounds of elicitation of one legacy call (then an execution error)
    static constexpr int MAXIMUM_ELICITATION_ROUNDS = 4;

    /// A call canceled by a disconnect or the stop is answered by an error after this time,
    /// when its host has not finished it (ms)
    static constexpr int CANCEL_GRACE_TIME = 5000;

    // Times (ms)

    /// An assistant without a request and without a stream is idle after this time [D2]
    static constexpr qint64 ASSISTANT_IDLE_TIME = qint64(30) * 60 * 1000;

    /// A legacy session without a request ends after this time
    static constexpr qint64 SESSION_IDLE_TIME = qint64(30) * 60 * 1000;

    /// A legacy session, which got no message after its initialize (neither
    /// notifications/initialized nor a request, a GET stream or an answer), ends
    /// after this time (follow-up 36). Together with the rate of initialize one
    /// address holds at most INITIALIZE_BURST + INITIALIZE_RATE * (10 + 1 s of
    /// the maintenance) = 38 such sessions - always fewer than
    /// MAXIMUM_LEGACY_SESSIONS, so a flood of unused sessions never takes the
    /// last slot of a client, which initializes properly (asserted below).
    static constexpr qint64 UNUSED_SESSION_TIME = 10000;

    /// A sealed requestState of MRTR expires after this time
    static constexpr qint64 REQUEST_STATE_LIFETIME = qint64(5) * 60 * 1000;

    /// Largest sealed requestState (characters of base64url)
    static constexpr int MAXIMUM_REQUEST_STATE_SIZE = 4096;

    /// Largest inputResponses of a retried request (bytes)
    static constexpr qsizetype MAXIMUM_INPUT_RESPONSES_SIZE = qsizetype(64) * 1024;

    /// Legacy elicitation waits at most this time for the answer of the client
    static constexpr int ELICITATION_TIMEOUT = 5 * 60 * 1000;

    /// A finished task is kept for this time
    static constexpr qint64 TASK_TTL = qint64(600) * 1000;

    /// Suggested poll interval of the tasks
    static constexpr int TASK_POLL_INTERVAL = 1000;

    /// A call, which has not finished within this time, becomes a task (tasks extension)
    static constexpr int TASK_CREATION_DELAY = 1000;

    /// Updates of one subscribed resource are coalesced to one per this time
    static constexpr int RESOURCE_UPDATE_INTERVAL = 250;

    /// Timeout of the probe of a port, which is in use (2.6)
    static constexpr int PORT_PROBE_TIMEOUT = 500;

    // Lists (1.4)

    /// Longest cursor of a list (bytes of its base64url text)
    static constexpr int MAXIMUM_CURSOR_LENGTH = 64;

    /// Page size of resources/list
    static constexpr int RESOURCE_PAGE_SIZE = 100;

    /// ttlMs of the lists (modern era)
    static constexpr qint64 DISCOVER_TTL = 3600000;
    static constexpr qint64 TOOLS_LIST_TTL = 60000;
    static constexpr qint64 STATIC_LIST_TTL = 3600000;
};

static_assert(PDFMcpLimits::INITIALIZE_BURST + PDFMcpLimits::INITIALIZE_RATE * (PDFMcpLimits::UNUSED_SESSION_TIME / 1000 + 1) < PDFMcpLimits::MAXIMUM_LEGACY_SESSIONS,
              "Unused legacy sessions of one address must not fill every session slot");

}   // namespace pdf

#endif // PDFMCPLIMITS_H
