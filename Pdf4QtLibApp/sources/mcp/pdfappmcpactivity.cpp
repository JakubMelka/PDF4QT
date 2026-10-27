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

#include "pdfappmcpactivity.h"
#include "pdfdatabase.h"

#include "pdfdocumenttoolcatalog.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>

namespace pdf
{

/// Helpers of the activity texts
class PDFAppMcpActivityHelper
{
public:
    PDFAppMcpActivityHelper() = delete;

    /// Titles of the actions without counts
    static const char* findFixedTitle(const QString& action)
    {
        static constexpr const char* TITLES[][2] =
        {
            { "document.get_info", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read document information") },
            { "document.get_pages", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read page information") },
            { "document.get_outline", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read the outline") },
            { "document.get_annotations", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read annotations") },
            { "document.get_form_fields", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read form fields") },
            { "document.get_attachments", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read the list of attachments") },
            { "document.read_attachment", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read an attachment") },
            { "document.get_signatures", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read signatures") },
            { "document.get_selection", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read the selection") },
            { "document.get_layers", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read layers") },
            { "document.get_bookmarks", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read bookmarks") },
            { "text.get_text", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read text") },
            { "text.search", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Search text") },
            { "text.get_text_in_area", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Read text of an area") },
            { "render.page", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Rendered page image") },
            { "render.overview", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Rendered page overview") },
            { "view.activate", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Switched to the document") },
            { "view.go_to", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Went to a page") },
            { "view.show_area", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Showed an area") },
            { "view.show_search", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Showed search results") },
            { "view.navigate", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Opened a page of PDF4QT") },
            { "view.close", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Closed tab") },
            { "annotations.reply", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Reply to a comment") },
            { "forms.reset", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Reset form") },
            { "files.open", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Open document") },
            { "files.save", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Save document") },
            { "files.save_copy", QT_TRANSLATE_NOOP("pdf::PDFAppMcpActivity", "Save a copy") },
        };

        for (const auto& title : TITLES)
        {
            if (action == QLatin1String(title[0]))
            {
                return title[1];
            }
        }
        return nullptr;
    }

    /// "Add %Ln highlights" of the kind of annotations.add
    static QString getAddTitle(const QString& kind, int count)
    {
        const bool one = count == 1;
        if (kind == QLatin1String("highlight"))
        {
            return one ? PDFAppMcpActivity::tr("Add %Ln highlight", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln highlights", nullptr, count);
        }
        if (kind == QLatin1String("note"))
        {
            return one ? PDFAppMcpActivity::tr("Add %Ln note", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln notes", nullptr, count);
        }
        if (kind == QLatin1String("text_box"))
        {
            return one ? PDFAppMcpActivity::tr("Add %Ln text box", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln text boxes", nullptr, count);
        }
        if (kind == QLatin1String("link"))
        {
            return one ? PDFAppMcpActivity::tr("Add %Ln link", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln links", nullptr, count);
        }
        if (kind == QLatin1String("stamp"))
        {
            return one ? PDFAppMcpActivity::tr("Add %Ln stamp", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln stamps", nullptr, count);
        }
        return one ? PDFAppMcpActivity::tr("Add %Ln annotation", nullptr, count) : PDFAppMcpActivity::tr("Add %Ln annotations", nullptr, count);
    }
};

QString PDFAppMcpActivity::getTitle(const QString& action, const QJsonObject& detail)
{
    const int count = qBound(0, detail.value(QStringLiteral("count")).toInt(1), 1000000);
    const bool one = count == 1;
    const QString kind = detail.value(QStringLiteral("kind")).toString();

    if (const char* title = PDFAppMcpActivityHelper::findFixedTitle(action))
    {
        return tr(title);
    }
    if (action == QLatin1String("annotations.add"))
    {
        return PDFAppMcpActivityHelper::getAddTitle(kind, count);
    }
    if (action == QLatin1String("annotations.update"))
    {
        return one ? tr("Change %Ln annotation", nullptr, count) : tr("Change %Ln annotations", nullptr, count);
    }
    if (action == QLatin1String("annotations.delete"))
    {
        return one ? tr("Delete %Ln annotation", nullptr, count) : tr("Delete %Ln annotations", nullptr, count);
    }
    if (action == QLatin1String("forms.set_values"))
    {
        return one ? tr("Fill %Ln form field", nullptr, count) : tr("Fill %Ln form fields", nullptr, count);
    }
    if (action.startsWith(QLatin1String("outline.")))
    {
        return tr("Edit the outline");
    }

    // A tool of the catalog (phase 2): "Run Document Statistics"
    if (const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(action))
    {
        return tr("Run %1").arg(entry->getTitle());
    }
    return tr("Request of the assistant");
}

QString PDFAppMcpActivity::getMetaText(const QString& client, const QString& document, const QJsonObject& detail)
{
    QStringList parts;
    parts << client;
    if (!document.isEmpty())
    {
        parts << document;
    }

    const std::set<int> pages = parsePages(detail.value(QStringLiteral("pages")).toString());
    if (pages.size() == 1)
    {
        parts << tr("page %1").arg(*pages.begin());
    }
    else if (pages.size() > 1)
    {
        parts << tr("pages %1").arg(formatPages(pages));
    }
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppMcpActivity::getStatusText(PDFMcpActivityStatus status, const QString& client, const QJsonObject& detail, bool isOtherInstance)
{
    switch (status)
    {
        case PDFMcpActivityStatus::Ok:
            return QString();
        case PDFMcpActivityStatus::Waiting:
            if (detail.value(QStringLiteral("reason")).toString() == QLatin1String("elicitation"))
            {
                return tr("Waiting for a choice in %1").arg(client);
            }
            return isOtherInstance ? tr("Waiting for confirmation in the other PDF4QT window") : tr("Waiting for your confirmation");
        case PDFMcpActivityStatus::Allowed:
            return tr("Allowed by you");
        case PDFMcpActivityStatus::DeniedUser:
            return tr("Denied by you");
        case PDFMcpActivityStatus::DeniedTimeout:
            return tr("Denied automatically · no answer in 2 min");
        case PDFMcpActivityStatus::DeniedClosed:
            return tr("Denied · the tab was closed");
        case PDFMcpActivityStatus::DeniedLocked:
            return tr("Denied · the document is locked");
        case PDFMcpActivityStatus::DeniedBusy:
            return tr("Denied · a tool is running on the document");
        case PDFMcpActivityStatus::DeniedDialog:
            return tr("Denied · a dialog was open in PDF4QT");
        case PDFMcpActivityStatus::DeniedDisconnected:
            return tr("Denied · the assistant disconnected");
        case PDFMcpActivityStatus::DeniedStopped:
            return tr("Denied · the server stopped");
        case PDFMcpActivityStatus::Canceled:
            return tr("Canceled by the assistant");
        case PDFMcpActivityStatus::DeniedPermission:
            return tr("Denied · the permission is set to Deny");
        case PDFMcpActivityStatus::Failed:
        {
            const QString reason = detail.value(QStringLiteral("reason")).toString();
            return reason.isEmpty() ? tr("Failed") : tr("Failed · %1").arg(reason);
        }
    }
    return QString();
}

QString PDFAppMcpActivity::formatPages(const std::set<int>& pages)
{
    QStringList parts;
    auto it = pages.cbegin();
    while (it != pages.cend())
    {
        const int first = *it;
        int last = first;
        ++it;
        while (it != pages.cend() && *it == last + 1)
        {
            last = *it;
            ++it;
        }
        parts << (first == last ? QString::number(first) : QStringLiteral("%1–%2").arg(first).arg(last));
    }
    return parts.join(QStringLiteral(", "));
}

std::set<int> PDFAppMcpActivity::parsePages(const QString& text)
{
    std::set<int> pages;
    const QStringList parts = text.left(4096).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QStringList range = part.trimmed().split(QChar(0x2013));
        bool okFirst = false;
        bool okLast = false;
        const int first = range.value(0).toInt(&okFirst);
        const int last = range.size() == 2 ? range.value(1).toInt(&okLast) : first;
        if (!okFirst || (range.size() == 2 && !okLast) || range.size() > 2 || first < 1 || last < first)
        {
            continue;
        }

        for (int page = first; page <= last && pages.size() < 10000; ++page)
        {
            pages.insert(page);
        }
    }
    return pages;
}

bool PDFAppMcpActivity::isLoggedAction(const QString& action)
{
    return action != QLatin1String("application.get_state") && PDFMcpRepository::isValidAction(action);
}

bool PDFAppMcpActivity::isReadAction(const QString& action)
{
    return action.startsWith(QLatin1String("document.")) || action.startsWith(QLatin1String("text.")) || action.startsWith(QLatin1String("render."));
}

// ----------------------------------------------------------------------------
// PDFAppMcpActivityLog
// ----------------------------------------------------------------------------

PDFAppMcpActivityLog::PDFAppMcpActivityLog(PDFDatabase* database, QObject* parent) :
    QObject(parent),
    m_database(database)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(FLUSH_INTERVAL);
    connect(m_timer, &QTimer::timeout, this, &PDFAppMcpActivityLog::flush);
}

PDFAppMcpActivityLog::~PDFAppMcpActivityLog()
{
    // The last rows are written synchronously (no signal from a dying object)
    blockSignals(true);
    flush();
}

qint64 PDFAppMcpActivityLog::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

void PDFAppMcpActivityLog::scheduleFlush()
{
    if (m_timer && !m_timer->isActive())
    {
        m_timer->start(FLUSH_INTERVAL);
    }
}

void PDFAppMcpActivityLog::limitUnwrittenRows()
{
    // Unwritten rows are kept for the next flush up to a bound: the oldest
    // finished reads go first, then the oldest other finished rows; a waiting row
    // is never dropped (its decision comes later and must be written)
    std::vector<std::pair<qint64, qint64>> reads;
    std::vector<std::pair<qint64, qint64>> others;
    for (const auto& item : m_rows)
    {
        const Row& row = item.second;
        if (row.databaseId > 0 || row.entry.status == PDFMcpActivityStatus::Waiting)
        {
            continue;
        }

        const bool isRead = row.entry.status == PDFMcpActivityStatus::Ok && PDFAppMcpActivity::isReadAction(row.entry.action);
        (isRead ? reads : others).emplace_back(row.time, item.first);
    }

    size_t unwritten = 0;
    for (const auto& item : m_rows)
    {
        unwritten += item.second.databaseId > 0 ? 0 : 1;
    }
    if (unwritten <= MAXIMUM_UNWRITTEN_ROWS)
    {
        return;
    }

    std::sort(reads.begin(), reads.end());
    std::sort(others.begin(), others.end());
    for (const std::vector<std::pair<qint64, qint64>>* candidates : { &reads, &others })
    {
        for (const auto& candidate : *candidates)
        {
            if (unwritten <= MAXIMUM_UNWRITTEN_ROWS)
            {
                return;
            }
            m_rows.erase(candidate.second);
            --unwritten;
        }
    }
}

void PDFAppMcpActivityLog::forgetWrittenRows(qint64 now)
{
    // A written row is kept while its status may still change: the decision of
    // a confirmation, which waited up to 2 minutes, and the final status of the
    // job of the change after it are written into the same row (the window of
    // the coalescing alone lost them - review RC7-H). A waiting row stays until
    // its status changes.
    std::vector<std::pair<qint64, qint64>> written;
    for (auto it = m_rows.begin(); it != m_rows.end();)
    {
        const Row& row = it->second;
        const bool isForgettable = !row.isDirty && row.databaseId > 0 && row.entry.status != PDFMcpActivityStatus::Waiting;
        if (isForgettable && row.lastChange < now - KEEP_TIME)
        {
            it = m_rows.erase(it);
            continue;
        }
        if (isForgettable)
        {
            written.emplace_back(row.lastChange, it->first);
        }
        ++it;
    }

    // A bounded memory: the rows changed longest ago go first
    if (m_rows.size() > MAXIMUM_KEPT_ROWS)
    {
        std::sort(written.begin(), written.end());
        for (const auto& item : written)
        {
            if (m_rows.size() <= MAXIMUM_KEPT_ROWS)
            {
                break;
            }
            m_rows.erase(item.second);
        }
    }
}

int PDFAppMcpActivityLog::getPendingCount() const
{
    int count = 0;
    for (const auto& item : m_rows)
    {
        count += item.second.isDirty ? 1 : 0;
    }
    return count;
}

qint64 PDFAppMcpActivityLog::record(const PDFAppMcpActivityEntry& entry)
{
    if (!PDFAppMcpActivity::isLoggedAction(entry.action))
    {
        return 0;
    }

    const qint64 now = getCurrentTime();

    forgetWrittenRows(now);

    // Reads of the same assistant / document / action within 60 s are one row,
    // so are the requests refused by a Deny permission (a client repeating a
    // denied call cannot flood the table; the row counts the attempts)
    const bool isRead = entry.status == PDFMcpActivityStatus::Ok && PDFAppMcpActivity::isReadAction(entry.action);
    const bool isDenied = entry.status == PDFMcpActivityStatus::DeniedPermission;
    if (isRead || isDenied)
    {
        for (auto& item : m_rows)
        {
            Row& row = item.second;
            if (row.entry.status == entry.status && row.entry.action == entry.action && row.entry.assistantKey == entry.assistantKey &&
                row.entry.documentId == entry.documentId && row.time >= now - COALESCE_TIME)
            {
                row.entry.pages.insert(entry.pages.cbegin(), entry.pages.cend());
                if (isDenied)
                {
                    const int attempts = qBound(1, row.entry.detail.value(QStringLiteral("attempts")).toInt(1), MAXIMUM_ATTEMPTS);
                    row.entry.detail.insert(QStringLiteral("attempts"), qMin(attempts + 1, MAXIMUM_ATTEMPTS));
                }
                row.time = now;
                row.lastChange = now;
                row.isDirty = true;
                scheduleFlush();
                return item.first;
            }
        }
    }

    const qint64 id = m_nextId++;
    Row row;
    row.entry = entry;
    row.time = now;
    row.lastChange = now;
    m_rows.emplace(id, std::move(row));
    scheduleFlush();
    return id;
}

void PDFAppMcpActivityLog::update(qint64 localId, PDFMcpActivityStatus status, const QJsonObject& detail)
{
    auto it = m_rows.find(localId);
    if (it == m_rows.end())
    {
        return;
    }

    Row& row = it->second;
    row.entry.status = status;
    for (auto detailIt = detail.constBegin(); detailIt != detail.constEnd(); ++detailIt)
    {
        row.entry.detail.insert(detailIt.key(), detailIt.value());
    }
    row.lastChange = getCurrentTime();
    row.isDirty = true;
    scheduleFlush();
}

void PDFAppMcpActivityLog::flush()
{
    if (m_timer)
    {
        m_timer->stop();
    }

    if (!m_database)
    {
        return;
    }

    PDFMcpRepository repository(m_database);
    repository.setCurrentTime(m_currentTime);

    std::vector<qint64> newIds;
    std::vector<PDFMcpActivityRow> newRows;
    bool isWritten = false;
    bool isRefused = false;

    for (auto& item : m_rows)
    {
        Row& row = item.second;
        if (!row.isDirty)
        {
            continue;
        }

        QJsonObject detail = row.entry.detail;
        if (!row.entry.pages.empty())
        {
            detail.insert(QStringLiteral("pages"), PDFAppMcpActivity::formatPages(row.entry.pages));
        }

        if (row.databaseId > 0)
        {
            bool isUpdated = false;
            if (!repository.updateActivity(row.databaseId, row.entry.status, detail, &isUpdated).isError())
            {
                isWritten = isWritten || isUpdated;
            }
            else
            {
                // A refused write: the change stays for the next flush
                isRefused = true;
                continue;
            }

            if (isUpdated)
            {
                row.isDirty = false;
                continue;
            }

            // The row was deleted meanwhile (Clear of any instance, the trim): a
            // read coalesced into it, or the decision of a confirmation, is a new
            // row - nothing of the audit is lost (review RW7-0)
            row.databaseId = 0;
        }

        PDFMcpActivityRow activity;
        activity.time = row.time;
        activity.client = row.entry.client;
        activity.action = row.entry.action;
        activity.document = row.entry.document;
        activity.detail = detail;
        activity.status = row.entry.status;
        newRows.push_back(activity);
        newIds.push_back(item.first);
    }

    if (!newRows.empty())
    {
        std::vector<qint64> databaseIds;
        if (!repository.addActivity(newRows, &databaseIds).isError() && databaseIds.size() == newIds.size())
        {
            for (size_t i = 0; i < newIds.size(); ++i)
            {
                Row& row = m_rows[newIds[i]];
                row.databaseId = databaseIds[i];
                row.isDirty = false;
            }
            isWritten = true;
        }
        else
        {
            // A refused write (a full disk, a lock of another process): the rows
            // stay for the next flush - a waiting row keeps its local id, so its
            // later decision is not lost (bounded by limitUnwrittenRows)
            isRefused = true;
        }
    }

    if (isRefused)
    {
        limitUnwrittenRows();

        // Tried again later, not at once (a refusing database is not hammered)
        if (m_timer)
        {
            m_timer->start(RETRY_INTERVAL);
        }
    }

    if (isWritten)
    {
        Q_EMIT written();
    }
}

}   // namespace pdf
