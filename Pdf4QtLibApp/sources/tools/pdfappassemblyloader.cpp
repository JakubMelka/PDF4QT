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

#include "pdfappassemblyloader.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputloader.h"
#include "pdfapptoolpathrules.h"
#include "pdfrepositoryhelper.h"

#include "pdfoperationcontrol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimeZone>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <limits>

namespace pdf
{

/// The cancellation of a job of the loader: shared by the loader (GUI thread, cancel and the
/// destructor) and the job (a thread of the pool), an atomic flag only
class PDFAppAssemblyLoadControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(); }
    void cancel() { m_isCanceled.store(true); }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The job of the loader (QtConcurrent::run, owner 2026-11-09): plain values only - the paths,
/// a copy of the result of a locate and the shared cancellation; never the loader or a GUI
class PDFAppAssemblyLoadTask
{
public:
    PDFAppAssemblyLoadTask(PDFAppAssemblyLoadResult result, QString sourceId, QString filePath, std::shared_ptr<PDFAppAssemblyLoadControl> control) :
        m_result(std::move(result)),
        m_sourceId(std::move(sourceId)),
        m_filePath(std::move(filePath)),
        m_control(std::move(control))
    {

    }

    PDFAppAssemblyLoadResult operator()() const
    {
        if (m_sourceId.isEmpty())
        {
            return PDFAppAssemblyLoader::read(m_filePath, m_control.get());
        }
        return PDFAppAssemblyLoader::readLocated(m_result, m_sourceId, m_filePath, m_control.get());
    }

private:
    PDFAppAssemblyLoadResult m_result;
    QString m_sourceId;
    QString m_filePath;
    std::shared_ptr<PDFAppAssemblyLoadControl> m_control;
};

/// Helpers of the loader
class PDFAppAssemblyLoaderHelper
{
public:
    PDFAppAssemblyLoaderHelper() = delete;

    /// The dynamic property of the list of the inputs with the requested file
    static constexpr const char* REQUESTED_FILE_PROPERTY = "pdf4qtRequestedAssembly";

    /// A path of the file, which may be a candidate: absolute after cleaning, without NUL, not
    /// too long (an assembly file is untrusted). Empty, when it cannot be one.
    static QString cleanCandidate(const QString& path)
    {
        if (path.isEmpty() || path.size() > PDFRepositoryHelper::MAXIMUM_PATH_LENGTH || path.contains(QChar(0)))
        {
            return QString();
        }
        const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
        return QDir::isAbsolutePath(cleanPath) ? cleanPath : QString();
    }

    /// Sorts a candidate path into the local ones and the first one of another computer (RB8:
    /// the text of the path decides, nothing is touched)
    static void addCandidate(const QString& path, QStringList* localPaths, QString* remotePath)
    {
        const QString candidate = cleanCandidate(path);
        if (candidate.isEmpty())
        {
            return;
        }

        // The rule tests the original spelling too ("\\server" and "//server" are one path); the
        // path of another computer is shown with its two leading separators (cleaning may drop one)
        if (PDFAppToolPathRules::isDeviceOrNetworkPath(path) || PDFAppToolPathRules::isDeviceOrNetworkPath(candidate))
        {
            if (remotePath->isEmpty())
            {
                *remotePath = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
            }
            return;
        }

        if (!localPaths->contains(candidate))
        {
            localPaths->append(candidate);
        }
    }

    /// SHA-256 of a file, read by blocks of HASH_BLOCK_SIZE with the cancellation after each one;
    /// empty, when it cannot be read or the job is canceled (\p isCanceled)
    static QByteArray computeHash(const QString& filePath, const PDFOperationControl* control, bool* isCanceled)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return QByteArray();
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd())
        {
            if (PDFOperationControl::isOperationCancelled(control))
            {
                *isCanceled = true;
                return QByteArray();
            }

            const QByteArray block = file.read(PDFAppAssemblyLoader::HASH_BLOCK_SIZE);
            if (block.isEmpty())
            {
                // A read error before the end
                return file.atEnd() ? hash.result() : QByteArray();
            }
            hash.addData(block);
        }
        return hash.result();
    }

    /// The page count of a changed PDF file now (PDFAppToolInputLoader::read, the reader of the
    /// inputs): -1 for an encrypted file without its password, 0 for a file, which is no usable PDF
    static PDFInteger readPageCount(const QString& filePath)
    {
        PDFAppToolInputLoader::Request request;
        request.filePath = filePath;
        request.name = QFileInfo(filePath).fileName();
        const PDFAppToolInputLoader::Result read = PDFAppToolInputLoader::read(std::move(request));
        if (read.kind != PDFAppToolInputKind::File)
        {
            return 0;
        }

        switch (read.state)
        {
            case PDFAppToolInputState::PasswordRequired:
                return -1;

            case PDFAppToolInputState::Ready:
            case PDFAppToolInputState::Restricted:
            case PDFAppToolInputState::NoPages:
                return read.pageCount;

            case PDFAppToolInputState::Loading:
            case PDFAppToolInputState::Damaged:
            case PDFAppToolInputState::Unsupported:
                break;
        }
        return 0;
    }

    /// The times of a source and of a file are the same (the file keeps the seconds of ISO 8601)
    static bool isSameTime(const QDateTime& first, const QDateTime& second)
    {
        return first.isValid() && second.isValid() && first.toSecsSinceEpoch() == second.toSecsSinceEpoch();
    }

    /// The state of the local file \p filePath, which exists, for \p source (see the class)
    static PDFAppAssemblySourceStatus checkFile(const PDFDocumentAssembly::Source& source, const QString& filePath, const PDFOperationControl* control, bool* isCanceled)
    {
        PDFAppAssemblySourceStatus status;
        status.sourceId = source.id;
        status.filePath = filePath;
        status.pageCount = source.pageCount;

        const QFileInfo info(filePath);
        status.size = info.size();
        status.modified = info.lastModified(QTimeZone::UTC);

        bool isChanged = false;
        if (source.size >= 0 && status.size != source.size)
        {
            isChanged = true;
        }
        else if (!source.sha256.isEmpty())
        {
            status.sha256 = computeHash(filePath, control, isCanceled);
            if (*isCanceled)
            {
                return status;
            }
            isChanged = status.sha256 != source.sha256;
        }
        else
        {
            // A file without a hash (another writer): its time decides
            isChanged = source.modified.isValid() && !isSameTime(source.modified, status.modified);
        }

        if (!isChanged)
        {
            status.state = PDFAppAssemblySourceState::Found;
            if (status.sha256.isEmpty())
            {
                status.sha256 = source.sha256;
            }
            return status;
        }

        // Changed: its hash for the next saving of the assembly and its page count for the
        // dialog ("now 5 pages instead of 6") and the bin
        if (status.sha256.isEmpty())
        {
            status.sha256 = computeHash(filePath, control, isCanceled);
            if (*isCanceled)
            {
                return status;
            }
        }
        if (PDFOperationControl::isOperationCancelled(control))
        {
            *isCanceled = true;
            return status;
        }
        status.state = PDFAppAssemblySourceState::Changed;
        status.pageCount = readPageCount(filePath);
        return status;
    }

    /// The state of a source of the file of the folder \p folder (see the class)
    static PDFAppAssemblySourceStatus checkSource(const PDFDocumentAssembly::Source& source, const QString& folder, const PDFOperationControl* control, bool* isCanceled)
    {
        PDFAppAssemblySourceStatus status;
        status.sourceId = source.id;
        status.pageCount = source.pageCount;
        status.size = source.size;
        status.modified = source.modified;
        status.sha256 = source.sha256;
        status.filePath = source.path;

        // An image is in the assembly file (Q11): nothing is read from the disk
        if (source.kind == PDFDocumentAssembly::SourceKind::Image)
        {
            status.state = PDFAppAssemblySourceState::Found;
            return status;
        }

        QStringList localPaths;
        QString remotePath;
        addCandidate(source.path, &localPaths, &remotePath);
        if (!source.relativePath.isEmpty() && !folder.isEmpty())
        {
            // A relative path, which is absolute itself, stays as it is (QDir::filePath)
            addCandidate(QDir(folder).filePath(QDir::fromNativeSeparators(source.relativePath)), &localPaths, &remotePath);
        }

        for (const QString& localPath : std::as_const(localPaths))
        {
            if (QFileInfo(localPath).isFile())
            {
                return checkFile(source, localPath, control, isCanceled);
            }
        }

        if (!remotePath.isEmpty())
        {
            // RB8: never touched automatically - Locate… or Open From There
            status.state = PDFAppAssemblySourceState::OnAnotherComputer;
            status.filePath = remotePath;
            return status;
        }

        status.state = PDFAppAssemblySourceState::Missing;
        status.filePath = !localPaths.isEmpty() ? localPaths.front() : cleanCandidate(source.path);
        return status;
    }

    /// A refused or canceled result keeps no content
    static PDFAppAssemblyLoadResult cancelResult(PDFAppAssemblyLoadResult result)
    {
        result.isCanceled = true;
        result.content.reset();
        result.sources.clear();
        return result;
    }

    /// Index of the source of the id, -1 = none
    static int findSource(const std::vector<PDFDocumentAssembly::Source>& sources, const QString& sourceId)
    {
        for (size_t i = 0; i < sources.size(); ++i)
        {
            if (sources[i].id == sourceId)
            {
                return int(i);
            }
        }
        return -1;
    }

    /// Index of the status of the source, -1 = none
    static int findStatus(const std::vector<PDFAppAssemblySourceStatus>& statuses, const QString& sourceId)
    {
        for (size_t i = 0; i < statuses.size(); ++i)
        {
            if (statuses[i].sourceId == sourceId)
            {
                return int(i);
            }
        }
        return -1;
    }

};

PDFAppAssemblyLoader::PDFAppAssemblyLoader(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppAssemblyLoader::~PDFAppAssemblyLoader()
{
    // Nobody waits for the job: it sees the cancellation after its block, its result is dropped
    // (the watcher is a child and goes with the loader, no signal comes)
    if (m_control)
    {
        m_control->cancel();
    }
}

bool PDFAppAssemblyLoader::isAssemblyFile(const QString& filePath)
{
    return QFileInfo(filePath).suffix().compare(QLatin1String(PDFDocumentAssemblyFile::FILE_SUFFIX), Qt::CaseInsensitive) == 0;
}

bool PDFAppAssemblyLoader::load(const QString& filePath)
{
    return start(PDFAppAssemblyLoadResult(), QString(), filePath);
}

bool PDFAppAssemblyLoader::locate(const PDFAppAssemblyLoadResult& result, const QString& sourceId, const QString& filePath)
{
    if (!result.content || sourceId.isEmpty())
    {
        return false;
    }
    return start(result, sourceId, filePath);
}

bool PDFAppAssemblyLoader::start(const PDFAppAssemblyLoadResult& result, const QString& sourceId, const QString& filePath)
{
    if (isLoading())
    {
        return false;
    }

    // A check of a located file belongs to the assembly of its result
    m_loadingFilePath = QDir::cleanPath(QFileInfo(sourceId.isEmpty() ? filePath : result.filePath).absoluteFilePath());
    m_control = std::make_shared<PDFAppAssemblyLoadControl>();
    QFutureWatcher<PDFAppAssemblyLoadResult>* watcher = new QFutureWatcher<PDFAppAssemblyLoadResult>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppAssemblyLoader::onFinished);
    watcher->setFuture(QtConcurrent::run(PDFAppAssemblyLoadTask(result, sourceId, filePath, m_control)));
    return true;
}

void PDFAppAssemblyLoader::cancel()
{
    if (m_control)
    {
        m_control->cancel();
    }
}

bool PDFAppAssemblyLoader::isLoading() const
{
    return !m_watcher.isNull();
}

void PDFAppAssemblyLoader::onFinished()
{
    QFutureWatcher<PDFAppAssemblyLoadResult>* watcher = static_cast<QFutureWatcher<PDFAppAssemblyLoadResult>*>(m_watcher.data());
    if (!watcher || sender() != watcher)
    {
        return;
    }

    PDFAppAssemblyLoadResult result = watcher->result();
    const bool isCanceled = m_control && m_control->isOperationCancelled();

    // The state is cleared before the signal: a slot may start the next load or delete the loader
    m_watcher = nullptr;
    m_control.reset();
    m_loadingFilePath.clear();
    watcher->deleteLater();
    if (isCanceled && !result.isCanceled)
    {
        result = PDFAppAssemblyLoaderHelper::cancelResult(std::move(result));
    }

    Q_EMIT loaded(result);
}

PDFAppAssemblyLoadResult PDFAppAssemblyLoader::read(const QString& filePath, const PDFOperationControl* control)
{
    PDFAppAssemblyLoadResult result;
    const QFileInfo info(filePath);
    result.filePath = QDir::cleanPath(info.absoluteFilePath());

    const PDFDocumentAssemblyFile::Limits limits;
    if (!info.isFile())
    {
        result.errorMessage = tr("The assembly file does not exist.");
        return result;
    }
    if (info.size() > limits.maximumFileSize)
    {
        result.errorMessage = tr("The assembly file is larger than %1 MB.").arg(limits.maximumFileSize / (1024 * 1024));
        return result;
    }

    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = tr("The assembly file cannot be read.");
        return result;
    }

    // The file may grow meanwhile: never more than the limit is read
    const QByteArray data = file.read(limits.maximumFileSize + 1);
    file.close();
    if (data.size() > limits.maximumFileSize)
    {
        result.errorMessage = tr("The assembly file is larger than %1 MB.").arg(limits.maximumFileSize / (1024 * 1024));
        return result;
    }
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFAppAssemblyLoaderHelper::cancelResult(std::move(result));
    }

    PDFDocumentAssemblyFile::ReadResult read = PDFDocumentAssemblyFile::read(data, limits);
    result.problems = std::move(read.problems);
    if (!read.content)
    {
        result.errorMessage = read.errorMessage.isEmpty() ? tr("This is not an assembly file.") : read.errorMessage;
        return result;
    }
    result.content = std::move(read.content);

    // The sources in their order; the cancellation after each one (and each block of a hash)
    const QString folder = info.absolutePath();
    for (const PDFDocumentAssembly::Source& source : result.content->assembly.getSources())
    {
        if (PDFOperationControl::isOperationCancelled(control))
        {
            return PDFAppAssemblyLoaderHelper::cancelResult(std::move(result));
        }

        bool isCanceled = false;
        PDFAppAssemblySourceStatus status = PDFAppAssemblyLoaderHelper::checkSource(source, folder, control, &isCanceled);
        if (isCanceled)
        {
            return PDFAppAssemblyLoaderHelper::cancelResult(std::move(result));
        }
        result.sources.push_back(std::move(status));
    }
    return result;
}

PDFAppAssemblyLoadResult PDFAppAssemblyLoader::readLocated(PDFAppAssemblyLoadResult result, const QString& sourceId, const QString& filePath, const PDFOperationControl* control)
{
    if (!result.content)
    {
        return result;
    }

    const std::vector<PDFDocumentAssembly::Source>& sources = result.content->assembly.getSources();
    const int sourceIndex = PDFAppAssemblyLoaderHelper::findSource(sources, sourceId);
    if (sourceIndex < 0 || sources[sourceIndex].kind != PDFDocumentAssembly::SourceKind::Pdf)
    {
        return result;
    }
    const PDFDocumentAssembly::Source& source = sources[sourceIndex];

    // The file the user has chosen (an explicit choice: also a file of another computer)
    PDFAppAssemblySourceStatus status;
    const QString candidate = PDFAppAssemblyLoaderHelper::cleanCandidate(filePath);
    bool isCanceled = false;
    if (!candidate.isEmpty() && QFileInfo(candidate).isFile())
    {
        status = PDFAppAssemblyLoaderHelper::checkFile(source, candidate, control, &isCanceled);
    }
    else
    {
        status.sourceId = sourceId;
        status.state = PDFAppAssemblySourceState::Missing;
        status.filePath = candidate;
        status.pageCount = source.pageCount;
    }
    if (isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFAppAssemblyLoaderHelper::cancelResult(std::move(result));
    }

    const int statusIndex = PDFAppAssemblyLoaderHelper::findStatus(result.sources, sourceId);
    if (statusIndex >= 0)
    {
        result.sources[statusIndex] = std::move(status);
    }
    else
    {
        result.sources.push_back(std::move(status));
    }
    return result;
}

std::optional<PDFDocumentAssemblyFile::Content> PDFAppAssemblyLoader::createContent(const PDFAppAssemblyLoadResult& result, QString* errorMessage)
{
    if (!result.content)
    {
        if (errorMessage)
        {
            *errorMessage = result.errorMessage.isEmpty() ? tr("The assembly file has not been read.") : result.errorMessage;
        }
        return std::nullopt;
    }

    // The sources by the operations of the model (one rule of the bin, M1): a usable one takes
    // its file now (updateSource: the pages over the page count of a changed file go to the bin),
    // the pages of the others go to the bin (removeSource: the source is parked, the checkpoints
    // still know it)
    PDFDocumentAssembly assembly = result.content->assembly;
    const std::vector<PDFDocumentAssembly::Source> sources = assembly.getSources();
    std::vector<PDFInteger> pageLimit(sources.size(), std::numeric_limits<PDFInteger>::max());
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const PDFDocumentAssembly::Source& source = sources[i];
        if (source.kind == PDFDocumentAssembly::SourceKind::Image)
        {
            continue;
        }

        PDFDocumentAssembly::Edit edit;
        const int statusIndex = PDFAppAssemblyLoaderHelper::findStatus(result.sources, source.id);
        if (statusIndex < 0 || !result.sources[statusIndex].isUsable())
        {
            assembly = PDFDocumentAssembly::removeSource(assembly, source.id, &edit);
        }
        else
        {
            const PDFAppAssemblySourceStatus& status = result.sources[statusIndex];
            PDFDocumentAssembly::Source newSource = source;
            newSource.path = status.filePath;
            newSource.size = status.size;
            newSource.modified = status.modified;
            if (!status.sha256.isEmpty())
            {
                newSource.sha256 = status.sha256;
            }
            if (status.state == PDFAppAssemblySourceState::Changed && status.pageCount >= 0)
            {
                newSource.pageCount = status.pageCount;
                pageLimit[i] = status.pageCount;
            }
            if (newSource != source)
            {
                assembly = PDFDocumentAssembly::updateSource(assembly, std::move(newSource), &edit);
            }
        }

        if (!edit.errorMessage.isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = edit.errorMessage;
            }
            return std::nullopt;
        }
    }

    // The checkpoints lose the pages over the page count of a changed file (the other pages
    // stay: a missing source is parked, its pages are known)
    std::vector<PDFDocumentAssembly::Checkpoint> checkpoints = result.content->checkpoints;
    for (PDFDocumentAssembly::Checkpoint& checkpoint : checkpoints)
    {
        std::vector<PDFDocumentAssembly::Item> checkpointItems;
        for (PDFDocumentAssembly::Item& item : checkpoint.items)
        {
            std::vector<PDFDocumentAssembly::Page> pages;
            for (const PDFDocumentAssembly::Page& page : item.pages)
            {
                const int input = page.plan.input;
                if (input < 0 || size_t(input) >= sources.size() || page.plan.pageIndex < pageLimit[input])
                {
                    pages.push_back(page);
                }
            }
            if (!pages.empty())
            {
                item.pages = std::move(pages);
                checkpointItems.push_back(std::move(item));
            }
        }
        checkpoint.items = std::move(checkpointItems);
    }

    PDFDocumentAssemblyFile::Content content;
    content.assembly = std::move(assembly);
    content.checkpoints = std::move(checkpoints);
    content.outputParameters = result.content->outputParameters;
    return content;
}

void PDFAppAssemblyLoader::setRequestedFile(PDFAppToolInputList* inputList, const QString& filePath)
{
    if (inputList)
    {
        inputList->setProperty(PDFAppAssemblyLoaderHelper::REQUESTED_FILE_PROPERTY, filePath);
    }
}

QString PDFAppAssemblyLoader::takeRequestedFile(PDFAppToolInputList* inputList)
{
    if (!inputList)
    {
        return QString();
    }

    const QString filePath = inputList->property(PDFAppAssemblyLoaderHelper::REQUESTED_FILE_PROPERTY).toString();
    inputList->setProperty(PDFAppAssemblyLoaderHelper::REQUESTED_FILE_PROPERTY, QVariant());
    return filePath;
}

}   // namespace pdf

#include "moc_pdfappassemblyloader.cpp"
