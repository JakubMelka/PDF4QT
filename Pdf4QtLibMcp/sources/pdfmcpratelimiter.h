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

#ifndef PDFMCPRATELIMITER_H
#define PDFMCPRATELIMITER_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcplimits.h"

#include <QString>

#include <deque>
#include <map>

namespace pdf
{

/// Token bucket of each key (plan 7): \p burst requests at once, refilled by
/// \p rate requests per second - 50 / 20 per assistant for the requests, 20 / 20
/// for completion/complete. A refused request gets the wait until the next
/// token (retryAfterMs of the error -31001). At most
/// PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS keys are remembered (full buckets are
/// forgotten first). Not thread safe (the control thread).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpRateLimiter
{
public:
    explicit PDFMcpRateLimiter(int burst = PDFMcpLimits::RATE_BURST, int rate = PDFMcpLimits::RATE_SUSTAINED);

    /// The request of the key may run now (\p retryAfter = the wait in ms otherwise)
    bool allow(const QString& key, qint64 now, qint64* retryAfter);

    /// Forgets a key
    void remove(const QString& key);

    void clear() { m_buckets.clear(); }

    int getKeyCount() const { return int(m_buckets.size()); }

private:
    class Bucket
    {
    public:
        double tokens = 0.0;
        qint64 updatedAt = 0;
    };

    void refill(Bucket& bucket, qint64 now) const;
    void purge(qint64 now);

    int m_burst = PDFMcpLimits::RATE_BURST;
    int m_rate = PDFMcpLimits::RATE_SUSTAINED;
    std::map<QString, Bucket> m_buckets;
};

/// Failed authorizations of the addresses (plan 7): 20 failures of one address
/// within a minute block it for a minute (429). Not thread safe (the control
/// thread).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpAuthorizationThrottle
{
public:
    PDFMcpAuthorizationThrottle() = default;

    /// The address is blocked (\p retryAfter = the remaining time in ms)
    bool isBlocked(const QString& address, qint64 now, qint64* retryAfter);

    /// A failed authorization of the address
    void recordFailure(const QString& address, qint64 now);

    void clear() { m_addresses.clear(); }

    int getAddressCount() const { return int(m_addresses.size()); }

private:
    class Address
    {
    public:
        std::deque<qint64> failures;
        qint64 blockedUntil = 0;
    };

    void purge(qint64 now);

    /// Time of the last failure or the end of the block (the order of forgetting)
    static qint64 getActivityTime(const Address& address);

    std::map<QString, Address> m_addresses;
};

}   // namespace pdf

#endif // PDFMCPRATELIMITER_H
