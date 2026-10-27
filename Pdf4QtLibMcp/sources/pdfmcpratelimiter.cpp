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

#include "pdfmcpratelimiter.h"

#include <cmath>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpRateLimiter
// ----------------------------------------------------------------------------

PDFMcpRateLimiter::PDFMcpRateLimiter(int burst, int rate) :
    m_burst(qMax(1, burst)),
    m_rate(qMax(1, rate))
{

}

void PDFMcpRateLimiter::refill(Bucket& bucket, qint64 now) const
{
    if (now > bucket.updatedAt)
    {
        bucket.tokens = qMin(double(m_burst), bucket.tokens + double(now - bucket.updatedAt) * double(m_rate) / 1000.0);
    }
    bucket.updatedAt = now;
}

void PDFMcpRateLimiter::purge(qint64 now)
{
    // Full buckets carry no information
    for (auto it = m_buckets.begin(); it != m_buckets.end();)
    {
        refill(it->second, now);
        if (it->second.tokens >= double(m_burst))
        {
            it = m_buckets.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Still too many keys: the oldest are forgotten
    while (int(m_buckets.size()) >= PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS)
    {
        auto oldest = m_buckets.begin();
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ++it)
        {
            if (it->second.updatedAt < oldest->second.updatedAt)
            {
                oldest = it;
            }
        }
        m_buckets.erase(oldest);
    }
}

bool PDFMcpRateLimiter::allow(const QString& key, qint64 now, qint64* retryAfter)
{
    auto it = m_buckets.find(key);
    if (it == m_buckets.end())
    {
        if (int(m_buckets.size()) >= PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS)
        {
            purge(now);
        }

        Bucket bucket;
        bucket.tokens = double(m_burst);
        bucket.updatedAt = now;
        it = m_buckets.emplace(key, bucket).first;
    }

    Bucket& bucket = it->second;
    refill(bucket, now);
    if (bucket.tokens >= 1.0)
    {
        bucket.tokens -= 1.0;
        if (retryAfter)
        {
            *retryAfter = 0;
        }
        return true;
    }

    if (retryAfter)
    {
        *retryAfter = qMax<qint64>(1, qint64(std::ceil((1.0 - bucket.tokens) * 1000.0 / double(m_rate))));
    }
    return false;
}

void PDFMcpRateLimiter::remove(const QString& key)
{
    m_buckets.erase(key);
}

// ----------------------------------------------------------------------------
// PDFMcpAuthorizationThrottle
// ----------------------------------------------------------------------------

qint64 PDFMcpAuthorizationThrottle::getActivityTime(const Address& address)
{
    const qint64 failureTime = address.failures.empty() ? 0 : address.failures.back();
    return qMax(failureTime, address.blockedUntil);
}

void PDFMcpAuthorizationThrottle::purge(qint64 now)
{
    for (auto it = m_addresses.begin(); it != m_addresses.end();)
    {
        Address& address = it->second;
        while (!address.failures.empty() && now - address.failures.front() >= PDFMcpLimits::AUTHORIZATION_BLOCK_TIME)
        {
            address.failures.pop_front();
        }

        if (address.failures.empty() && address.blockedUntil <= now)
        {
            it = m_addresses.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // An attack from many addresses: the oldest are forgotten (a block of one
    // of them ends sooner, nothing grows without a bound). A blocked address
    // has no failures (cleared at the block): its time is the end of its
    // block, so it is forgotten last, not first.
    while (int(m_addresses.size()) >= PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS)
    {
        auto oldest = m_addresses.begin();
        qint64 oldestTime = getActivityTime(oldest->second);
        for (auto it = m_addresses.begin(); it != m_addresses.end(); ++it)
        {
            const qint64 time = getActivityTime(it->second);
            if (time < oldestTime)
            {
                oldest = it;
                oldestTime = time;
            }
        }
        m_addresses.erase(oldest);
    }
}

bool PDFMcpAuthorizationThrottle::isBlocked(const QString& address, qint64 now, qint64* retryAfter)
{
    auto it = m_addresses.find(address);
    if (it == m_addresses.end() || it->second.blockedUntil <= now)
    {
        return false;
    }

    if (retryAfter)
    {
        *retryAfter = it->second.blockedUntil - now;
    }
    return true;
}

void PDFMcpAuthorizationThrottle::recordFailure(const QString& address, qint64 now)
{
    auto it = m_addresses.find(address);
    if (it == m_addresses.end())
    {
        if (int(m_addresses.size()) >= PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS)
        {
            purge(now);
        }
        it = m_addresses.emplace(address, Address()).first;
    }

    Address& entry = it->second;
    while (!entry.failures.empty() && now - entry.failures.front() >= PDFMcpLimits::AUTHORIZATION_BLOCK_TIME)
    {
        entry.failures.pop_front();
    }

    entry.failures.push_back(now);
    if (int(entry.failures.size()) >= PDFMcpLimits::MAXIMUM_AUTHORIZATION_FAILURES)
    {
        entry.blockedUntil = now + PDFMcpLimits::AUTHORIZATION_BLOCK_TIME;
        entry.failures.clear();
    }
}

}   // namespace pdf
