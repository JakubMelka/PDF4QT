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

#ifndef PDFAPPFOLDERINFO_H
#define PDFAPPFOLDERINFO_H

#include "pdfappglobal.h"

#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

class QThreadPool;

namespace pdf
{

class PDFAppFolderInfoShared;

/// Information about a path of the file system: whether it exists and, for a
/// folder, the number of the PDF files in it (directly, not in the subfolders)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFolderInfo
{
public:
    QString path;               ///< The requested path (as requested)
    bool exists = false;
    bool isDirectory = false;
    bool isFile = false;
    int pdfCount = -1;          ///< PDF files of a folder (-1 = not a folder / not counted)
    bool isCountLimited = false;///< The enumeration stopped at the limit: pdfCount is a lower bound ("1000+")
};

/// Information about folders and files computed in worker threads (a network path
/// or a removed drive can block for seconds - never in the GUI thread): Recent
/// shows "6 PDF files" for a folder and "No longer exists" for a missing file or
/// folder. Requests of the same path are merged while one is running; the result
/// is delivered by the signal infoReady in the GUI thread (connect it with a
/// context object). Nothing is cached: a page asks again when it is shown.
///
/// The enumeration of a folder stops after PDFAppFolderInfoService::setMaximumEntries
/// entries (default 10000). The destructor drops the pending requests and waits
/// for the running ones at most getStopWaitTime(); a worker, which hangs in the
/// file system, is abandoned and its result is dropped. GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFolderInfoService : public QObject
{
    Q_OBJECT

public:
    /// Default limit of the entries of one folder, which are enumerated
    static constexpr int DEFAULT_MAXIMUM_ENTRIES = 10000;

    /// Most requests, which are pending at once (further ones are refused, isFull)
    static constexpr int MAXIMUM_PENDING = 2000;

    explicit PDFAppFolderInfoService(QObject* parent = nullptr);
    virtual ~PDFAppFolderInfoService() override;

    /// Requests the information about a folder (existence and the PDF count).
    /// \returns true, when the result will be delivered by infoReady (also for a
    /// request merged with a running one); false for an invalid path or when
    /// MAXIMUM_PENDING requests are pending (isFull - ask again later)
    bool requestFolderInfo(const QString& path);

    /// Requests only the existence of a path (a recent file), see requestFolderInfo
    bool requestPathInfo(const QString& path);

    /// Number of the requests, whose result has not been delivered yet
    int getPendingCount() const { return int(m_pending.size()); }

    /// No further request is accepted now (MAXIMUM_PENDING requests are pending)
    bool isFull() const { return m_pending.size() >= MAXIMUM_PENDING; }

    void setMaximumEntries(int maximumEntries);
    int getMaximumEntries() const;

    /// Time, for which the destructor waits for the running workers (ms)
    void setStopWaitTime(int stopWaitTime) { m_stopWaitTime = qMax(0, stopWaitTime); }
    int getStopWaitTime() const { return m_stopWaitTime; }

    /// Delay of each worker in milliseconds (tests: requests running at the destruction)
    void setTestDelay(int delay);

    /// Number of the workers, which have started a request and not finished it yet,
    /// the test delay included (tests: wait until a request really runs)
    int getRunningCount() const;

    /// Computes the information synchronously (any thread; used by the workers and the tests)
    static PDFAppFolderInfo computeInfo(const QString& path, bool countPdfFiles, int maximumEntries);

Q_SIGNALS:
    /// Result of a request (GUI thread)
    void infoReady(const pdf::PDFAppFolderInfo& info);

private:
    friend class PDFAppFolderInfoTask;

    bool request(const QString& path, bool countPdfFiles);
    void onResult(const pdf::PDFAppFolderInfo& info, bool countPdfFiles);

    std::shared_ptr<PDFAppFolderInfoShared> m_shared;
    QThreadPool* m_pool = nullptr;
    QSet<QString> m_pending;    ///< Keys of the running requests ("1:" / "0:" + path)
    int m_stopWaitTime = 2000;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppFolderInfo)

#endif // PDFAPPFOLDERINFO_H
