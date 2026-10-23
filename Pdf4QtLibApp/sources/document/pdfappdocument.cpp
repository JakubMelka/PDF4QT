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

#include "pdfappdocument.h"
#include "pdfappcontext.h"
#include "pdfappundostack.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttextindex.h"
#include "pdfoptionalcontent.h"

#include <QFileInfo>
#include <QRandomGenerator>

namespace pdf
{

PDFAppDocumentOperation::PDFAppDocumentOperation(PDFAppDocumentOperationKind kind, QString text, bool blocksChanges, QObject* parent) :
    BaseClass(parent),
    m_kind(kind),
    m_text(std::move(text)),
    m_blocksChanges(blocksChanges)
{

}

PDFAppDocumentOperation::~PDFAppDocumentOperation() = default;

bool PDFAppDocumentOperation::isAskedBeforeClose() const
{
    switch (m_kind)
    {
        case PDFAppDocumentOperationKind::Save:
        case PDFAppDocumentOperationKind::Print:
        case PDFAppDocumentOperationKind::Mcp:
            return true;

        case PDFAppDocumentOperationKind::Load:
        case PDFAppDocumentOperationKind::Index:
        case PDFAppDocumentOperationKind::Signatures:
        case PDFAppDocumentOperationKind::Fonts:
            break;
    }
    return false;
}

void PDFAppDocumentOperation::setProgress(int progress)
{
    progress = qBound(-1, progress, 100);
    if (m_progress != progress)
    {
        m_progress = progress;
        Q_EMIT progressChanged(progress);
    }
}

void PDFAppDocumentOperation::requestCancel()
{
    if (!m_isCancelRequested && !m_isFinished)
    {
        m_isCancelRequested = true;
        Q_EMIT cancelRequested();
    }
}

void PDFAppDocumentOperation::finish()
{
    if (!m_isFinished)
    {
        m_isFinished = true;
        Q_EMIT finished();
    }
}

PDFAppDocument::PDFAppDocument(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context),
    m_id(createId()),
    m_undoStack(std::make_unique<PDFAppUndoStack>(this))
{
    setObjectName(QStringLiteral("document"));
}

PDFAppDocument::~PDFAppDocument()
{
    // The operations are children of the document: nothing is announced from here
    for (const QPointer<PDFAppDocumentOperation>& operation : m_operations)
    {
        if (operation)
        {
            operation->disconnect(this);
        }
    }
}

QString PDFAppDocument::createId()
{
    // RFC 4648 base32 in lowercase: 26 characters of 5 bits = 130 bits, the last
    // two bits are zero (128 bits of randomness)
    static constexpr char ALPHABET[] = "abcdefghijklmnopqrstuvwxyz234567";

    quint32 words[4] = { };
    QRandomGenerator::system()->fillRange(words, 4);

    QByteArray bits;
    bits.reserve(16);
    for (quint32 word : words)
    {
        for (int i = 0; i < 4; ++i)
        {
            bits.append(char((word >> (8 * i)) & 0xFF));
        }
    }

    QString id;
    id.reserve(26);
    for (int index = 0; index < 26; ++index)
    {
        // Bits [5 * index, 5 * index + 5) of the 128 bits (beyond 128 = 0)
        int value = 0;
        for (int bit = 0; bit < 5; ++bit)
        {
            const int position = 5 * index + bit;
            int bitValue = 0;
            if (position < 128)
            {
                bitValue = (uchar(bits[position / 8]) >> (7 - position % 8)) & 1;
            }
            value = (value << 1) | bitValue;
        }
        id.append(QLatin1Char(ALPHABET[value]));
    }
    return id;
}

QString PDFAppDocument::getTitle() const
{
    if (!m_filePath.isEmpty())
    {
        return QFileInfo(m_filePath).fileName();
    }
    if (!m_copyTitle.isEmpty())
    {
        return m_copyTitle;
    }
    return tr("Untitled");
}

PDFInteger PDFAppDocument::getPageCount() const
{
    return m_document ? PDFInteger(m_document->getCatalog()->getPageCount()) : 0;
}

int PDFAppDocument::getUnsavedChangeCount() const
{
    if (!isModified())
    {
        return 0;
    }
    if (m_isCopy)
    {
        return -1;
    }
    return m_undoStack->getUnsavedChangeCount();
}

void PDFAppDocument::setSaved()
{
    m_undoStack->setSavedPosition();
    if (m_savedVersion != m_version)
    {
        m_savedVersion = m_version;
        Q_EMIT modifiedChanged();
    }
}

void PDFAppDocument::setSavedAs(quint64 version, const QString& filePath, QByteArray data)
{
    // A version newer than the current one cannot have been saved
    if (version > m_version || filePath.isEmpty())
    {
        return;
    }

    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    const QString newPath = canonicalPath.isEmpty() ? filePath : canonicalPath;
    const bool isFileChanged = newPath != m_filePath || m_isCopy;
    const bool wasModified = isModified();

    m_filePath = newPath;
    m_originalData = std::move(data);
    m_isCopy = false;
    m_copyTitle.clear();
    m_savedVersion = version;

    // The saved position of the undo stack: the current version, or a version,
    // which the stack cannot tell (changed while the file was being written)
    if (version == m_version)
    {
        m_undoStack->setSavedPosition();
    }
    else
    {
        m_undoStack->setSavedPositionUnknown();
    }

    QPointer<PDFAppDocument> guard(this);
    if (isFileChanged)
    {
        Q_EMIT fileChanged();
    }
    if (guard && (isFileChanged || wasModified != isModified() || isModified()))
    {
        Q_EMIT modifiedChanged();
    }
}

void PDFAppDocument::setLocked(bool locked)
{
    if (m_isLocked != locked)
    {
        m_isLocked = locked;
        Q_EMIT lockChanged();
    }
}

void PDFAppDocument::setLoadingProgress(int progress)
{
    progress = qBound(-1, progress, 100);
    if (m_loadingProgress != progress)
    {
        m_loadingProgress = progress;
        Q_EMIT loadingProgressChanged();
    }
}

PDFAppDocumentSnapshot PDFAppDocument::getSnapshot() const
{
    PDFAppDocumentSnapshot snapshot;
    snapshot.document = m_document;
    snapshot.version = m_version;
    snapshot.originalData = m_originalData;
    snapshot.filePath = m_filePath;
    return snapshot;
}

bool PDFAppDocument::commit(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion, PDFAppChangeSource source)
{
    const PDFDocumentPointer newDocument = modified;
    if (!isReady() || newDocument.isNull() || expectedVersion != m_version || m_isLocked || isBlockedByOperation())
    {
        return false;
    }

    PDFAppUndoStep step;
    step.document = m_document;
    step.flags = modified.getFlags();
    step.description = description.left(1024);
    step.source = source;
    step.memory = PDFAppUndoStack::estimateMemory(m_document.data(), newDocument.data());

    // The limit is read at each change (no cache of the settings)
    if (m_context && m_context->getDatabase())
    {
        m_undoStack->setLimit(int(m_context->getSettings().getInteger(PDFSettingKeys::editingUndoLimit())));
    }
    m_undoStack->push(std::move(step));

    setDocument(newDocument, modified.getFlags(), false);
    return true;
}

void PDFAppDocument::restore(PDFDocumentPointer document, PDFModifiedDocument::ModificationFlags flags)
{
    if (!isReady() || document.isNull())
    {
        return;
    }

    // Back at the saved version the document is not modified (a copy without a
    // file has no saved version)
    const bool isSaved = !m_isCopy && m_undoStack->isAtSavedPosition();
    setDocument(std::move(document), flags, isSaved);
}

void PDFAppDocument::setDocument(PDFDocumentPointer document, PDFModifiedDocument::ModificationFlags flags, bool isSaved)
{
    const bool wasModified = isModified();
    const bool isReset = flags.testFlag(PDFModifiedDocument::Reset);

    // The old document and its optional content activity stay alive, until the
    // listeners of documentChanged have the new ones: the compilers of the view
    // read them in their worker threads by raw pointers (a reload and a cleared or
    // trimmed undo stack hold no other reference to the old document)
    const PDFDocumentPointer oldDocument = m_document;
    Q_UNUSED(oldDocument);
    QPointer<PDFOptionalContentActivity> oldActivity;

    m_document = std::move(document);
    ++m_version;
    if (isSaved)
    {
        m_savedVersion = m_version;
    }

    if (isReset || flags.testFlag(PDFModifiedDocument::PageContents))
    {
        m_pageContentsVersion = m_version;
        m_textIndex.reset();
    }

    // A new optional content activity for a reset (the old one refers to the old
    // document); otherwise the activity is kept and points to the new document
    if (isReset || !m_optionalContentActivity)
    {
        oldActivity = m_optionalContentActivity;
        m_optionalContentActivity = new PDFOptionalContentActivity(m_document.data(), OCUsage::View, this);
    }
    else
    {
        m_optionalContentActivity->setDocument(m_document.data());
    }

    // The old activity is deleted from the event loop, after the signals (a child of
    // the document: it goes with the document, if a slot deletes the document)
    if (oldActivity)
    {
        oldActivity->deleteLater();
    }

    // The signatures belong to one version
    QPointer<PDFAppDocument> guard(this);
    if (!m_signatureResults.empty())
    {
        m_signatureResults.clear();
        Q_EMIT signatureResultsChanged();
        if (!guard)
        {
            return;
        }
    }

    Q_EMIT documentChanged(PDFModifiedDocument(m_document, m_optionalContentActivity.data(), flags));

    // The count of the unsaved changes changes with every change
    if (guard && (wasModified != isModified() || isModified()))
    {
        Q_EMIT modifiedChanged();
    }
}

void PDFAppDocument::setState(PDFAppDocumentState state)
{
    if (m_state != state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

PDFAppDocumentOperation* PDFAppDocument::beginOperation(PDFAppDocumentOperationKind kind, const QString& text, bool blocksChanges)
{
    PDFAppDocumentOperation* operation = new PDFAppDocumentOperation(kind, text, blocksChanges, this);
    m_operations.emplace_back(operation);
    connect(operation, &PDFAppDocumentOperation::finished, this, &PDFAppDocument::onOperationFinished);
    connect(operation, &PDFAppDocumentOperation::progressChanged, this, &PDFAppDocument::operationProgressChanged);
    Q_EMIT operationsChanged();
    return operation;
}

std::vector<PDFAppDocumentOperation*> PDFAppDocument::getOperations() const
{
    std::vector<PDFAppDocumentOperation*> operations;
    for (const QPointer<PDFAppDocumentOperation>& operation : m_operations)
    {
        if (operation && !operation->isFinished())
        {
            operations.push_back(operation.data());
        }
    }
    return operations;
}

bool PDFAppDocument::isBlockedByOperation() const
{
    for (const QPointer<PDFAppDocumentOperation>& operation : m_operations)
    {
        if (operation && !operation->isFinished() && operation->isBlockingChanges())
        {
            return true;
        }
    }
    return false;
}

void PDFAppDocument::onOperationFinished()
{
    // The operation is the sender: it is deleted later
    PDFAppDocumentOperation* operation = qobject_cast<PDFAppDocumentOperation*>(sender());
    for (auto it = m_operations.begin(); it != m_operations.end();)
    {
        if (it->isNull() || it->data() == operation)
        {
            it = m_operations.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (operation)
    {
        operation->disconnect(this);
        operation->deleteLater();
    }
    Q_EMIT operationsChanged();
}

void PDFAppDocument::setTextIndex(std::shared_ptr<const PDFDocumentTextIndex> index, quint64 version)
{
    // An index of a version older than the last change of the page contents is not valid
    if (version < m_pageContentsVersion || version > m_version)
    {
        return;
    }
    m_textIndex = std::move(index);
}

void PDFAppDocument::setSignatureResults(std::vector<PDFSignatureVerificationResult> results, quint64 version)
{
    if (version != m_version)
    {
        return;
    }
    m_signatureResults = std::move(results);
    Q_EMIT signatureResultsChanged();
}

void PDFAppDocument::setLoading(const QString& filePath)
{
    m_errorMessage.clear();
    m_loadingProgress = -1;
    if (m_state != PDFAppDocumentState::Ready)
    {
        // A shown document keeps its file while it is read again (a reload)
        m_filePath = filePath;
        setState(PDFAppDocumentState::Loading);
    }
}

void PDFAppDocument::setLoaded(PDFDocumentPointer document, QByteArray originalData, const QString& filePath, bool isPasswordProtected)
{
    if (document.isNull())
    {
        setFailed(filePath, tr("The file is not a valid PDF document."));
        return;
    }

    m_filePath = filePath;
    m_originalData = std::move(originalData);
    m_isPasswordProtected = isPasswordProtected;
    m_isPasswordWrong = false;
    m_isCopy = false;
    m_copyTitle.clear();
    m_errorMessage.clear();
    m_loadingProgress = -1;
    m_undoStack->clear();
    m_state = PDFAppDocumentState::Ready;

    QPointer<PDFAppDocument> guard(this);
    setDocument(std::move(document), PDFModifiedDocument::Reset, true);
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocument::reload(PDFDocumentPointer document, QByteArray data, bool isPasswordProtected)
{
    if (!isReady() || document.isNull())
    {
        return;
    }

    // The reload replaces the document and its history (decision D3), the view
    // keeps its page and zoom
    m_originalData = std::move(data);
    m_isPasswordProtected = isPasswordProtected;
    m_undoStack->clear();
    setDocument(std::move(document), PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset | PDFModifiedDocument::PreserveView), true);
}

void PDFAppDocument::setPasswordRequired(const QString& filePath, bool isWrong)
{
    m_filePath = filePath;
    m_isPasswordProtected = true;
    m_loadingProgress = -1;
    m_isPasswordWrong = isWrong;

    // The state screen shows a repeated wrong password too (the state is the same)
    if (m_state != PDFAppDocumentState::PasswordRequired)
    {
        setState(PDFAppDocumentState::PasswordRequired);
    }
    else
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocument::setFailed(const QString& filePath, const QString& errorMessage)
{
    m_filePath = filePath;
    m_errorMessage = errorMessage;
    m_loadingProgress = -1;
    setState(PDFAppDocumentState::Failed);
}

bool PDFAppDocument::setCopyOf(const PDFAppDocument* source)
{
    if (!source || source == this || !source->isReady() || source->getDocument().isNull() || m_state == PDFAppDocumentState::Ready)
    {
        return false;
    }

    // The copy has no file: it is modified until it is saved (Save As of package E)
    m_filePath.clear();
    m_copyTitle = tr("Copy of %1").arg(source->getTitle());
    m_isCopy = true;
    m_originalData = source->getOriginalData();
    m_isPasswordProtected = false;
    m_isPasswordWrong = false;
    m_isLocked = false;
    m_errorMessage.clear();
    m_undoStack->clear();
    m_state = PDFAppDocumentState::Ready;

    QPointer<PDFAppDocument> guard(this);
    setDocument(source->getDocument(), PDFModifiedDocument::Reset, false);
    if (guard)
    {
        Q_EMIT stateChanged();
    }
    return true;
}

PDFAppContext* PDFAppDocument::getContext() const
{
    return m_context.data();
}

PDFOptionalContentActivity* PDFAppDocument::getOptionalContentActivity() const
{
    return m_optionalContentActivity.data();
}

}   // namespace pdf
