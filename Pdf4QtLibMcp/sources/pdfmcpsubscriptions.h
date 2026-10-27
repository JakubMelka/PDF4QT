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

#ifndef PDFMCPSUBSCRIPTIONS_H
#define PDFMCPSUBSCRIPTIONS_H

#include "pdf4qtlibmcp_export.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <set>

namespace pdf
{

class PDFMcpJsonWriter;

/// Filter of a subscriptions/listen request (spec 2026-07-28, Subscriptions;
/// taskIds of the tasks extension): the notification types the client opts
/// in to. Parsing keeps only what the server agrees to honor - the
/// acknowledged subset (invalid or unknown URIs and task ids are omitted).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSubscriptionFilter
{
public:
    bool toolsListChanged = false;
    bool promptsListChanged = false;
    bool resourcesListChanged = false;
    QStringList resourceSubscriptions;  ///< Valid URIs only, without duplicates, at most 256
    QStringList taskIds;                ///< Task ids (checked by the dispatcher)
    bool hasTaskIds = false;            ///< The client asked for task notifications

    /// Parses the "notifications" member; std::nullopt (with \p error) when it is
    /// not an object or a member has a wrong type
    static std::optional<PDFMcpSubscriptionFilter> parse(const QJsonValue& value, QString* error);

    /// Writes the filter object (only the honored types)
    void write(PDFMcpJsonWriter& writer) const;
};

/// Resource URIs of one listen stream or one legacy session (plan 4.2): at
/// most 256 valid URIs; an update of a URI (a document prefix such as
/// "pdf4qt://documents/<id>" updates every subscribed URI below it) is sent
/// at most once per interval for each URI - the rest waits and is sent by
/// takeDue (coalescing).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSubscriptions
{
public:
    PDFMcpSubscriptions() = default;

    /// A URI, which can be subscribed: pdf4qt:// and at most 2048 characters
    /// without control characters
    static bool isValidUri(const QString& uri);

    /// The update of \p updatedUri concerns the subscribed URI (equal, or a
    /// prefix ending at a path segment)
    static bool isAffected(const QString& subscribedUri, const QString& updatedUri);

    /// Adds a URI (false: invalid, or the limit of 256 reached; true also for a
    /// URI, which is subscribed already)
    bool add(const QString& uri);

    /// Removes a URI (false = not subscribed)
    bool remove(const QString& uri);

    bool contains(const QString& uri) const { return m_uris.contains(uri); }
    const QStringList& getUris() const { return m_uris; }
    int getSubscriptionCount() const { return int(m_uris.size()); }

    /// An update: the subscribed URIs, which are sent now (the others wait)
    QStringList update(const QString& updatedUri, qint64 now, qint64 interval);

    /// Waiting URIs, whose interval has passed
    QStringList takeDue(qint64 now, qint64 interval);

    bool hasPending() const { return !m_pending.empty(); }

private:
    QStringList m_uris;
    std::map<QString, qint64> m_lastSent;
    std::set<QString> m_pending;
};

}   // namespace pdf

#endif // PDFMCPSUBSCRIPTIONS_H
