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

#include "pdfapptoolinputlist.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"

#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QFileInfo>
#include <QSet>

namespace pdf
{

/// Helpers of the list
class PDFAppToolInputListHelper
{
public:
    PDFAppToolInputListHelper() = delete;

    /// Id of a list unique in the process (GUI thread only)
    static quint64 nextListId()
    {
        static quint64 listId = 0;
        return ++listId;
    }

    /// The canonical path of an existing file, the absolute path otherwise
    static QString getCanonicalPath(const QString& filePath)
    {
        if (filePath.isEmpty())
        {
            return QString();
        }

        const QFileInfo fileInfo(filePath);
        const QString canonicalPath = fileInfo.canonicalFilePath();
        return canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath;
    }

    /// Paths are compared as the file system of the platform does
    static bool isSamePath(const QString& left, const QString& right)
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return left.compare(right, Qt::CaseInsensitive) == 0;
#else
        return left == right;
#endif
    }
};

bool PDFAppToolInput::needsAttention() const
{
    switch (state)
    {
        case PDFAppToolInputState::PasswordRequired:
        case PDFAppToolInputState::Restricted:
        case PDFAppToolInputState::Damaged:
        case PDFAppToolInputState::Unsupported:
            return true;

        case PDFAppToolInputState::Loading:
        case PDFAppToolInputState::Ready:
        case PDFAppToolInputState::NoPages:
            break;
    }
    return false;
}

PDFDocumentToolInputDocument PDFAppToolInput::createToolInputDocument() const
{
    PDFDocumentToolInputDocument input;
    input.document = document;
    input.originalData = originalData;
    input.fileName = name;
    input.inputId = id;
    input.isOwnerPasswordVerified = isOwnerPasswordVerified;

    // The bytes of a file are the bytes of its document; a snapshot of a tab knows it
    input.isModified = kind == PDFAppToolInputKind::OpenTab ? isModified : false;
    return input;
}

PDFDocumentToolInputImage PDFAppToolInput::createToolInputImage() const
{
    PDFDocumentToolInputImage image;
    image.data = imageData;
    image.format = imageFormat;
    image.fileName = name;
    image.inputId = id;
    return image;
}

PDFAppToolInputList::PDFAppToolInputList(QObject* parent) :
    BaseClass(parent),
    m_listId(QString::number(PDFAppToolInputListHelper::nextListId()))
{
    m_loader = new PDFAppToolInputLoader(this);
    connect(m_loader.data(), &PDFAppToolInputLoader::readingFinished, this, &PDFAppToolInputList::onReadingFinished);
}

PDFAppToolInputList::~PDFAppToolInputList()
{
    // The readings are dropped (the loader, a child, cuts its link); the passwords are not kept
    if (m_loader)
    {
        m_loader->disconnect(this);
        m_loader->cancelAll();
    }
}

const PDFAppToolInput* PDFAppToolInputList::getInput(int index) const
{
    return index >= 0 && index < getCount() ? &m_inputs[size_t(index)] : nullptr;
}

const PDFAppToolInput* PDFAppToolInputList::findInput(const QString& id) const
{
    return const_cast<PDFAppToolInputList*>(this)->findMutableInput(id);
}

PDFAppToolInput* PDFAppToolInputList::findMutableInput(const QString& id)
{
    for (PDFAppToolInput& input : m_inputs)
    {
        if (input.id == id)
        {
            return &input;
        }
    }

    auto it = m_parked.find(id);
    return it != m_parked.end() ? &it->second : nullptr;
}

int PDFAppToolInputList::indexOf(const QString& id) const
{
    for (size_t i = 0; i < m_inputs.size(); ++i)
    {
        if (m_inputs[i].id == id)
        {
            return int(i);
        }
    }
    return -1;
}

QStringList PDFAppToolInputList::getInputIds() const
{
    QStringList ids;
    ids.reserve(getCount());
    for (const PDFAppToolInput& input : m_inputs)
    {
        ids << input.id;
    }
    return ids;
}

bool PDFAppToolInputList::isParked(const QString& id) const
{
    return m_parked.count(id) > 0;
}

QStringList PDFAppToolInputList::getParkedIds() const
{
    QStringList ids;
    for (const auto& item : m_parked)
    {
        ids << item.first;
    }
    return ids;
}

QString PDFAppToolInputList::findParkedFile(const QString& filePath) const
{
    const QString canonicalPath = PDFAppToolInputListHelper::getCanonicalPath(filePath);
    for (const auto& item : m_parked)
    {
        if (!canonicalPath.isEmpty() && item.second.kind != PDFAppToolInputKind::OpenTab && PDFAppToolInputListHelper::isSamePath(item.second.filePath, canonicalPath))
        {
            return item.first;
        }
    }
    return QString();
}

int PDFAppToolInputList::getUsableCount() const
{
    int count = 0;
    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.isUsable())
        {
            ++count;
        }
    }
    return count;
}

int PDFAppToolInputList::getLoadingCount() const
{
    int count = 0;
    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.state == PDFAppToolInputState::Loading)
        {
            ++count;
        }
    }
    return count;
}

int PDFAppToolInputList::getAttentionCount() const
{
    int count = 0;
    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.needsAttention())
        {
            ++count;
        }
    }
    return count;
}

bool PDFAppToolInputList::isRunnable(int minimumCount) const
{
    return getLoadingCount() == 0 && getAttentionCount() == 0 && getUsableCount() >= minimumCount;
}

PDFInteger PDFAppToolInputList::getTotalPageCount() const
{
    PDFInteger count = 0;
    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.isUsable())
        {
            count += input.pageCount;
        }
    }
    return count;
}

qint64 PDFAppToolInputList::getTotalSize() const
{
    qint64 size = 0;
    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.isUsable())
        {
            size += input.fileSize;
        }
    }
    return size;
}

bool PDFAppToolInputList::containsFile(const QString& filePath) const
{
    const QString canonicalPath = PDFAppToolInputListHelper::getCanonicalPath(filePath);
    if (canonicalPath.isEmpty())
    {
        return false;
    }

    for (const PDFAppToolInput& input : m_inputs)
    {
        if (PDFAppToolInputListHelper::isSamePath(input.filePath, canonicalPath))
        {
            return true;
        }
    }
    return false;
}

bool PDFAppToolInputList::containsDocument(const QString& documentId) const
{
    if (documentId.isEmpty())
    {
        return false;
    }

    for (const PDFAppToolInput& input : m_inputs)
    {
        if (input.documentId == documentId)
        {
            return true;
        }
    }
    return false;
}

int PDFAppToolInputList::getLoadingProgress(const QString& id) const
{
    return m_loader ? m_loader->getProgress(id) : -1;
}

QString PDFAppToolInputList::getLetter(int index)
{
    // Bijective base 26: A ... Z, AA, AB, ... (R20)
    if (index < 0)
    {
        return QString();
    }

    QString letter;
    qint64 value = qint64(index) + 1;
    while (value > 0)
    {
        const qint64 remainder = (value - 1) % 26;
        letter.prepend(QChar(char16_t(u'A' + remainder)));
        value = (value - 1) / 26;
    }
    return letter;
}

PDFAccentHue PDFAppToolInputList::getHue(int index, const PDFAppToolInput* input)
{
    // K15 as changed by K40: red and amber mean an error and a partial result
    if (input && (input->state == PDFAppToolInputState::Damaged || input->state == PDFAppToolInputState::Unsupported || input->state == PDFAppToolInputState::NoPages))
    {
        return PDFAccentHue::Neutral;
    }

    static constexpr PDFAccentHue hues[] = { PDFAccentHue::Blue, PDFAccentHue::Green, PDFAccentHue::Violet, PDFAccentHue::Teal };
    return index >= 0 ? hues[index % std::size(hues)] : PDFAccentHue::Neutral;
}

QString PDFAppToolInputList::getRefusalText(Refusal refusal, const QString& name)
{
    switch (refusal)
    {
        case Refusal::None:
            return QString();
        case Refusal::Duplicate:
            return tr("%1 is already in the list").arg(name);
        case Refusal::TooMany:
            return tr("The list has %Ln files, no more can be added", nullptr, MAXIMUM_INPUTS);
        case Refusal::NotReady:
            return tr("%1 is not open yet").arg(name);
        case Refusal::Invalid:
            return tr("%1 cannot be added").arg(name);
        case Refusal::ImageTooLarge:
            return tr("The pasted image is too large");
    }
    return QString();
}

QString PDFAppToolInputList::getStateText(PDFAppToolInputState state)
{
    switch (state)
    {
        case PDFAppToolInputState::Loading:
            return tr("Loading…");
        case PDFAppToolInputState::Ready:
            return QString();
        case PDFAppToolInputState::PasswordRequired:
            return tr("Protected by a password");
        case PDFAppToolInputState::Restricted:
            return tr("Restricted – enter the owner password");
        case PDFAppToolInputState::Damaged:
            return tr("Damaged – the file cannot be read");
        case PDFAppToolInputState::Unsupported:
            return tr("Not supported");
        case PDFAppToolInputState::NoPages:
            return tr("No pages");
    }
    return QString();
}

PDFAppThumbnailSource PDFAppToolInputList::getPictureSource(const QString& id, int pageIndex) const
{
    PDFAppThumbnailSource source;
    const PDFAppToolInput* input = findInput(id);
    if (!input || !input->isUsable() || pageIndex < 0 || pageIndex >= input->pageCount)
    {
        return source;
    }

    source.pageIndex = pageIndex;
    if (input->isImage())
    {
        source.imageData = input->imageData;
    }
    else
    {
        source.document = input->document;
    }
    return source;
}

QString PDFAppToolInputList::getPictureKey(const QString& id, int pageIndex, const QSize& size, qreal devicePixelRatio) const
{
    const PDFAppToolInput* input = findInput(id);
    const quint64 generation = input ? input->contentGeneration : 0;
    return QStringLiteral("pdf4qt-input/%1/%2/%3/%4/%5x%6@%7").arg(m_listId, id).arg(generation).arg(pageIndex).arg(size.width()).arg(size.height()).arg(devicePixelRatio);
}

QString PDFAppToolInputList::createId()
{
    return QStringLiteral("i%1").arg(m_nextId++);
}

void PDFAppToolInputList::advanceGeneration()
{
    ++m_generation;
}

PDFAppToolInputList::AddResult PDFAppToolInputList::addInput(PDFAppToolInput input, bool isReadingNeeded)
{
    AddResult result;
    result.name = input.name;
    if (getCount() >= MAXIMUM_INPUTS)
    {
        result.refusal = Refusal::TooMany;
        return result;
    }

    input.id = createId();
    advanceGeneration();
    input.contentGeneration = m_generation;
    result.id = input.id;
    m_inputs.push_back(input);

    if (isReadingNeeded)
    {
        startReading(input, false, QString(), false);
    }

    Q_EMIT inputsChanged();
    return result;
}

PDFAppToolInputList::AddResult PDFAppToolInputList::addFile(const QString& filePath)
{
    const QString canonicalPath = PDFAppToolInputListHelper::getCanonicalPath(filePath);
    AddResult refused;
    refused.name = QFileInfo(filePath).fileName();
    if (canonicalPath.isEmpty())
    {
        refused.refusal = Refusal::Invalid;
        return refused;
    }
    if (containsFile(canonicalPath))
    {
        refused.refusal = Refusal::Duplicate;
        return refused;
    }

    PDFAppToolInput input;
    input.kind = PDFAppToolInputKind::File;
    input.state = PDFAppToolInputState::Loading;
    input.name = QFileInfo(canonicalPath).fileName();
    input.filePath = canonicalPath;
    return addInput(std::move(input), true);
}

std::vector<PDFAppToolInputList::AddResult> PDFAppToolInputList::addFiles(const QStringList& filePaths)
{
    // Each addition emits: the list may be deleted by a slot
    QPointer<PDFAppToolInputList> guard(this);
    std::vector<AddResult> results;
    for (const QString& filePath : filePaths)
    {
        results.push_back(addFile(filePath));
        if (!guard)
        {
            break;
        }
    }
    return results;
}

PDFAppToolInputList::AddResult PDFAppToolInputList::addOpenDocument(const PDFAppDocument* document)
{
    AddResult refused;
    if (!document)
    {
        refused.refusal = Refusal::Invalid;
        return refused;
    }

    refused.name = document->getTitle();
    if (!document->isReady())
    {
        refused.refusal = Refusal::NotReady;
        return refused;
    }

    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    const QString canonicalPath = PDFAppToolInputListHelper::getCanonicalPath(snapshot.filePath);
    if (!snapshot.isValid())
    {
        refused.refusal = Refusal::NotReady;
        return refused;
    }
    if (containsDocument(document->getId()) || (!canonicalPath.isEmpty() && containsFile(canonicalPath)))
    {
        refused.refusal = Refusal::Duplicate;
        return refused;
    }

    // R11: the snapshot of now; the tab is never followed (no pointer to the document)
    PDFAppToolInput input;
    input.kind = PDFAppToolInputKind::OpenTab;
    input.name = document->getTitle();
    input.filePath = canonicalPath;
    input.documentId = document->getId();
    input.isModified = snapshot.isModified;
    input.document = snapshot.document;
    input.originalData = snapshot.originalData;
    input.fileSize = snapshot.originalData.size();
    input.isRepaired = snapshot.restoration != PDFAppDocumentRestoration::None;
    const PDFSecurityHandler* securityHandler = snapshot.document->getStorage().getSecurityHandler();
    input.isEncrypted = securityHandler && securityHandler->getMode() != EncryptionMode::None;

    if (snapshot.isPagedXFA)
    {
        input.state = PDFAppToolInputState::Unsupported;
        input.reason = PDFAppToolInputLoader::tr("The file is a dynamic XFA form; its pages cannot be merged.");
    }
    else
    {
        PDFAppToolInputLoader::Result result;
        PDFAppToolInputLoader::fillPdfResult(snapshot.document.data(), &result, input.name);
        input.state = result.state;
        input.reason = result.reason;
        input.pageCount = result.pageCount;
        input.firstPageSize = result.firstPageSize;
    }
    return addInput(std::move(input), false);
}

PDFAppToolInputList::AddResult PDFAppToolInputList::addImage(const QImage& image, const QString& name)
{
    AddResult refused;
    refused.name = name;
    if (image.isNull())
    {
        refused.refusal = Refusal::Invalid;
        return refused;
    }
    if (qint64(image.width()) * qint64(image.height()) > MAXIMUM_CLIPBOARD_PIXELS)
    {
        refused.refusal = Refusal::ImageTooLarge;
        return refused;
    }

    // Plan 5.3: the image of the clipboard is stored as PNG bytes (no loss), its resolution
    // is unknown as the one of a file without it
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || data.isEmpty())
    {
        refused.refusal = Refusal::Invalid;
        return refused;
    }
    buffer.close();

    PDFAppToolInput input;
    input.kind = PDFAppToolInputKind::Clipboard;
    input.state = PDFAppToolInputState::Loading;
    input.name = name.isEmpty() ? tr("Pasted image %1").arg(++m_clipboardCount) : name;
    input.imageData = data;
    input.fileSize = data.size();
    return addInput(std::move(input), true);
}

bool PDFAppToolInputList::moveInput(const QString& id, int index)
{
    const int from = indexOf(id);
    if (from < 0 || m_inputs.empty())
    {
        return false;
    }

    const int to = qBound(0, index, getCount() - 1);
    if (from == to)
    {
        return false;
    }

    PDFAppToolInput input = std::move(m_inputs[size_t(from)]);
    m_inputs.erase(m_inputs.begin() + from);
    m_inputs.insert(m_inputs.begin() + to, std::move(input));
    advanceGeneration();
    Q_EMIT inputsChanged();
    return true;
}

bool PDFAppToolInputList::removeInput(const QString& id)
{
    const int index = indexOf(id);
    if (index < 0)
    {
        return false;
    }

    // R12: parked, a running reading goes on (Undo takes the input back read)
    PDFAppToolInput input = std::move(m_inputs[size_t(index)]);
    m_inputs.erase(m_inputs.begin() + index);
    m_parked[input.id] = std::move(input);
    advanceGeneration();
    Q_EMIT inputsChanged();
    return true;
}

bool PDFAppToolInputList::setInputIds(const QStringList& ids)
{
    if (ids == getInputIds())
    {
        return false;
    }

    // Everything is parked first, then the ids take their inputs back in their order
    for (PDFAppToolInput& input : m_inputs)
    {
        const QString id = input.id;
        m_parked[id] = std::move(input);
    }
    m_inputs.clear();

    for (const QString& id : ids)
    {
        auto it = m_parked.find(id);
        if (it == m_parked.end() || getCount() >= MAXIMUM_INPUTS)
        {
            // An unknown id, an id given twice, or more than the limit
            continue;
        }
        m_inputs.push_back(std::move(it->second));
        m_parked.erase(it);
    }

    advanceGeneration();
    Q_EMIT inputsChanged();
    return true;
}

void PDFAppToolInputList::dropParkedInputs(const QStringList& keptIds)
{
    const QSet<QString> kept(keptIds.cbegin(), keptIds.cend());
    for (auto it = m_parked.begin(); it != m_parked.end();)
    {
        if (!kept.contains(it->first))
        {
            if (m_loader)
            {
                m_loader->cancel(it->first);
            }
            it = m_parked.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool PDFAppToolInputList::setPassword(const QString& id, const QString& password)
{
    PDFAppToolInput* input = findMutableInput(id);
    if (!input || (m_loader && m_loader->isRunning(id)))
    {
        return false;
    }

    const bool isOwnerPassword = input->state == PDFAppToolInputState::Restricted;
    if (input->state != PDFAppToolInputState::PasswordRequired && !isOwnerPassword)
    {
        return false;
    }

    // A snapshot of an open tab is verified (Q15), it needs no bytes, when its handler is there
    const bool isSnapshot = input->kind == PDFAppToolInputKind::OpenTab && isOwnerPassword && input->document;
    if (!isSnapshot && input->originalData.isEmpty())
    {
        return false;
    }

    PDFAppToolInput copy = *input;
    input->state = PDFAppToolInputState::Loading;
    input->isPasswordWrong = false;
    startReading(copy, true, password, isOwnerPassword);
    Q_EMIT inputChanged(id);
    return true;
}

void PDFAppToolInputList::startReading(const PDFAppToolInput& input, bool hasPassword, const QString& password, bool isOwnerPassword)
{
    if (!m_loader)
    {
        return;
    }

    PDFAppToolInputLoader::Request request;
    request.id = input.id;
    request.name = input.name;
    request.filePath = input.filePath;
    request.isImageOnly = input.kind == PDFAppToolInputKind::Clipboard;
    request.data = request.isImageOnly ? input.imageData : input.originalData;
    request.hasPassword = hasPassword;
    request.password = password;
    request.isOwnerPassword = isOwnerPassword;
    if (input.kind == PDFAppToolInputKind::OpenTab && isOwnerPassword)
    {
        request.snapshotDocument = input.document;
    }
    ++m_startedReadingCount;
    m_loader->start(std::move(request));
}

void PDFAppToolInputList::applyResult(PDFAppToolInput* input, const PDFAppToolInputLoader::Result& result)
{
    // An open tab stays an open tab (a snapshot read again with its owner password)
    if (input->kind != PDFAppToolInputKind::OpenTab)
    {
        input->kind = result.kind;
    }

    input->state = result.state;
    input->reason = result.reason;
    input->isEncrypted = input->isEncrypted || result.isEncrypted;
    input->isPasswordWrong = result.isPasswordWrong;
    if (input->kind != PDFAppToolInputKind::OpenTab)
    {
        // A snapshot keeps what its tab said of its file (a verification reads no file)
        input->isRepaired = result.isRepaired;
    }
    input->isOwnerPasswordVerified = result.isOwnerPasswordVerified;
    input->pageCount = result.pageCount;
    input->firstPageSize = result.firstPageSize;
    if (result.fileSize > 0)
    {
        input->fileSize = result.fileSize;
    }

    if (input->isImage())
    {
        input->document.reset();
        input->originalData.clear();
        input->imageData = result.data;
        input->imageFormat = result.imageFormat;
        input->imageSize = result.imageSize;
        input->imageDpiX = result.imageDpiX;
        input->imageDpiY = result.imageDpiY;
    }
    else
    {
        // A failed reading of a password keeps the bytes (the next password reads them)
        if (!result.data.isEmpty())
        {
            input->originalData = result.data;
        }
        if (result.document || result.state != PDFAppToolInputState::Restricted)
        {
            input->document = result.document;
        }
        input->imageData.clear();
    }
}

void PDFAppToolInputList::onReadingFinished(const QString& id, const PDFAppToolInputLoader::Result& result)
{
    PDFAppToolInput* input = findMutableInput(id);
    if (!input)
    {
        return;
    }

    applyResult(input, result);
    advanceGeneration();
    input->contentGeneration = m_generation;
    Q_EMIT inputChanged(id);
}

}   // namespace pdf

#include "moc_pdfapptoolinputlist.cpp"
