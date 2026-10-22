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

#include "pdfappautorefresh.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Helpers of the automatic refresh
class PDFAppAutoRefreshHelper
{
public:
    PDFAppAutoRefreshHelper() = delete;

    /// Password callback of the reader: no password is given (an encrypted file
    /// is read by the loader of the tab, which asks for it)
    static QString refusePassword(bool* ok)
    {
        if (ok)
        {
            *ok = false;
        }
        return QString();
    }

    /// Text of the dialog refresh-unsaved with the number of the changes, when it is known
    static QString getUnsavedText(int changeCount)
    {
        if (changeCount == 1)
        {
            return PDFAppAutoRefresh::tr("Another program has saved a new version of the file. Your document has an unsaved change. Reloading shows the new version and your change is lost.");
        }
        if (changeCount > 1)
        {
            return PDFAppAutoRefresh::tr("Another program has saved a new version of the file. Your document has unsaved changes (%Ln changes). Reloading shows the new version and your changes are lost.", nullptr, changeCount);
        }
        return PDFAppAutoRefresh::tr("Another program has saved a new version of the file. Your document has unsaved changes. Reloading shows the new version and your changes are lost.");
    }

    /// Width of the message box (958:28033)
    static constexpr int MESSAGE_BOX_WIDTH = 440;
};

PDFAppAutoRefreshReader::Result PDFAppAutoRefreshReader::read(QString filePath, QByteArray knownData)
{
    Result result;
    result.filePath = filePath;

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists())
    {
        result.outcome = Outcome::Missing;
        return result;
    }

    if (!fileInfo.isFile() || fileInfo.size() > MAXIMUM_FILE_SIZE)
    {
        result.outcome = Outcome::Failed;
        result.errorMessage = tr("The file cannot be read.");
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.outcome = Outcome::Failed;
        result.errorMessage = file.errorString();
        return result;
    }

    try
    {
        QByteArray data = file.read(MAXIMUM_FILE_SIZE);
        file.close();

        if (data == knownData)
        {
            result.outcome = Outcome::Unchanged;
            return result;
        }

        PDFDocumentReader reader(nullptr, &PDFAppAutoRefreshHelper::refusePassword, true, false);
        PDFDocument document = reader.readFromBuffer(data);
        switch (reader.getReadingResult())
        {
            case PDFDocumentReader::Result::OK:
            {
                const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
                result.isPasswordProtected = securityHandler && securityHandler->getMode() != EncryptionMode::None;
                result.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
                result.data = std::move(data);
                result.outcome = Outcome::Changed;
                break;
            }

            case PDFDocumentReader::Result::Cancelled:
                // The reader has asked for a password
                result.data = std::move(data);
                result.outcome = Outcome::PasswordRequired;
                break;

            default:
                result.outcome = Outcome::Failed;
                result.errorMessage = reader.getErrorMessage();
                break;
        }
    }
    catch (const PDFException& exception)
    {
        result.outcome = Outcome::Failed;
        result.errorMessage = exception.getMessage();
    }
    catch (const std::bad_alloc&)
    {
        result.outcome = Outcome::Failed;
        result.errorMessage = tr("There is not enough memory to read the file.");
    }

    return result;
}

PDFAppAutoRefresh::PDFAppAutoRefresh(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_document(tab ? tab->getDocument() : nullptr)
{
    // The default of a new document (the switch of the document follows it)
    PDFAppContext* context = tab ? tab->getContext() : nullptr;
    if (context && context->getDatabase())
    {
        m_isEnabled = context->getSettings().getBool(PDFSettingKeys::documentAutoRefresh());
    }

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &PDFAppAutoRefresh::onFileChanged);

    m_checkTimer = new QTimer(this);
    m_checkTimer->setSingleShot(true);
    connect(m_checkTimer, &QTimer::timeout, this, &PDFAppAutoRefresh::startCheck);

    m_rewatchTimer = new QTimer(this);
    m_rewatchTimer->setInterval(REWATCH_INTERVAL);
    connect(m_rewatchTimer, &QTimer::timeout, this, &PDFAppAutoRefresh::onRewatchTimeout);

    if (m_document)
    {
        connect(m_document, &PDFAppDocument::stateChanged, this, &PDFAppAutoRefresh::onDocumentStateChanged);
        connect(m_document, &PDFAppDocument::fileChanged, this, &PDFAppAutoRefresh::onDocumentStateChanged);
    }

    updateWatchedPath();
}

PDFAppAutoRefresh::~PDFAppAutoRefresh()
{
    // A running worker keeps its own data; its result is not delivered
    if (m_watcher)
    {
        m_watcher->disconnect(this);
    }
    closeDialog();
}

PDFMessageBox* PDFAppAutoRefresh::getDialog() const
{
    return m_dialog.data();
}

void PDFAppAutoRefresh::setEnabled(bool enabled)
{
    if (m_isEnabled != enabled)
    {
        m_isEnabled = enabled;
        if (!enabled)
        {
            m_checkTimer->stop();
            m_isCheckPending = false;
        }
        updateWatchedPath();
        Q_EMIT stateChanged();
    }
}

QString PDFAppAutoRefresh::getWatchedPath() const
{
    return m_watchedPath;
}

void PDFAppAutoRefresh::checkNow()
{
    m_checkTimer->stop();
    startCheck();
}

void PDFAppAutoRefresh::ignoreOwnWrite(const QString& filePath)
{
    // The written file replaced the watched one (QSaveFile renames a temporary
    // file): the path is watched again, the notifications of the write are only
    // checked against the new bytes of the document
    Q_UNUSED(filePath);
    m_keptData.clear();
    updateWatchedPath();
}

void PDFAppAutoRefresh::onDocumentStateChanged()
{
    updateWatchedPath();
}

void PDFAppAutoRefresh::updateWatchedPath()
{
    const PDFAppDocument* document = m_document.data();
    const QString path = (m_isEnabled && document && document->isReady()) ? document->getFilePath() : QString();

    if (path != m_watchedPath)
    {
        m_keptData.clear();
    }

    // The path is removed and added again: after an atomic replace the watcher
    // has dropped it, a new file is a new inode
    if (!m_fileWatcher->files().isEmpty())
    {
        m_fileWatcher->removePaths(m_fileWatcher->files());
    }
    m_rewatchTimer->stop();
    m_rewatchAttempts = 0;

    const bool isChanged = path != m_watchedPath;
    m_watchedPath = path;
    if (!path.isEmpty())
    {
        if (!m_fileWatcher->addPath(path))
        {
            // The file is missing now (it is being replaced): try it again later
            m_rewatchTimer->start();
        }
    }

    if (isChanged)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppAutoRefresh::onFileChanged(const QString& path)
{
    if (path != m_watchedPath || m_watchedPath.isEmpty())
    {
        return;
    }

    // An atomic replace removes the path from the watcher
    if (!m_fileWatcher->files().contains(path) && !m_fileWatcher->addPath(path))
    {
        m_rewatchAttempts = 0;
        m_rewatchTimer->start();
    }

    scheduleCheck(CHECK_DELAY);
}

void PDFAppAutoRefresh::onRewatchTimeout()
{
    if (m_watchedPath.isEmpty() || m_fileWatcher->files().contains(m_watchedPath))
    {
        m_rewatchTimer->stop();
        return;
    }

    if (m_fileWatcher->addPath(m_watchedPath))
    {
        // The file exists again: it may have new content
        m_rewatchTimer->stop();
        scheduleCheck(CHECK_DELAY);
        return;
    }

    if (++m_rewatchAttempts >= REWATCH_ATTEMPTS)
    {
        // The file has been deleted for good: nothing to refresh
        m_rewatchTimer->stop();
    }
}

void PDFAppAutoRefresh::scheduleCheck(int delay)
{
    if (!m_isEnabled || m_watchedPath.isEmpty())
    {
        return;
    }
    m_checkTimer->start(delay);
}

void PDFAppAutoRefresh::startCheck()
{
    PDFAppDocument* document = m_document.data();
    if (!m_isEnabled || !document || !document->isReady() || m_watchedPath.isEmpty())
    {
        return;
    }

    if (m_watcher || m_dialog)
    {
        // A check or the question runs: the newest file is checked after it
        m_isCheckPending = true;
        return;
    }

    // An own save in progress writes the file: it is checked after the save
    for (const PDFAppDocumentOperation* operation : document->getOperations())
    {
        if (operation && operation->getKind() == PDFAppDocumentOperationKind::Save)
        {
            scheduleCheck(CHECK_DELAY);
            return;
        }
    }

    m_isCheckPending = false;
    QFutureWatcher<PDFAppAutoRefreshReader::Result>* watcher = new QFutureWatcher<PDFAppAutoRefreshReader::Result>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<PDFAppAutoRefreshReader::Result>::finished, this, &PDFAppAutoRefresh::onReaderFinished);
    watcher->setFuture(QtConcurrent::run(&PDFAppAutoRefreshReader::read, m_watchedPath, document->getOriginalData()));
}

void PDFAppAutoRefresh::onReaderFinished()
{
    QFutureWatcher<PDFAppAutoRefreshReader::Result>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppAutoRefreshReader::Result result = watcher->future().result();
    m_watcher = nullptr;
    watcher->deleteLater();

    QPointer<PDFAppAutoRefresh> guard(this);
    applyResult(result);
    if (!guard)
    {
        return;
    }

    Q_EMIT checked(result.outcome);
    if (guard && m_isCheckPending && !m_dialog)
    {
        scheduleCheck(0);
    }
}

void PDFAppAutoRefresh::applyResult(const PDFAppAutoRefreshReader::Result& result)
{
    PDFAppDocument* document = m_document.data();
    if (!m_isEnabled || !document || !document->isReady() || result.filePath != m_watchedPath)
    {
        return;
    }

    switch (result.outcome)
    {
        case PDFAppAutoRefreshReader::Outcome::Unchanged:
        case PDFAppAutoRefreshReader::Outcome::Failed:
        case PDFAppAutoRefreshReader::Outcome::Missing:
            // Nothing new (an own save, a file being written - its next change is checked)
            return;

        case PDFAppAutoRefreshReader::Outcome::Changed:
        case PDFAppAutoRefreshReader::Outcome::PasswordRequired:
            break;
    }

    if (!m_keptData.isEmpty() && result.data == m_keptData)
    {
        // The user has kept the version of the document against this file
        return;
    }

    if (document->isModified())
    {
        showUnsavedDialog(result);
        return;
    }

    if (result.outcome == PDFAppAutoRefreshReader::Outcome::PasswordRequired)
    {
        // The new file needs a password: the loader of the tab asks for it
        if (PDFAppDocumentLoader* loader = m_tab ? m_tab->getLoader() : nullptr)
        {
            ++m_reloadCount;
            loader->start(result.filePath);
        }
        return;
    }

    ++m_reloadCount;
    QPointer<PDFAppAutoRefresh> guard(this);
    document->reload(result.document, result.data, result.isPasswordProtected);
    if (guard)
    {
        Q_EMIT reloaded();
    }
}

void PDFAppAutoRefresh::showUnsavedDialog(const PDFAppAutoRefreshReader::Result& result)
{
    PDFAppDocument* document = m_document.data();
    if (!document)
    {
        return;
    }

    closeDialog();
    m_pendingResult = result;

    QWidget* parent = m_tab ? m_tab->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("%1 was changed on the disk").arg(document->getTitle()),
                                              PDFAppAutoRefreshHelper::getUnsavedText(document->getUnsavedChangeCount()),
                                              QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("refreshUnsavedDialog"));
    dialog->setCardWidth(PDFAppAutoRefreshHelper::MESSAGE_BOX_WIDTH);
    dialog->setDetailedText(tr("Keep My Version leaves the file on the disk as it is. When you save, your version replaces it."));
    PDFAppMainWindow::prepareDialog(dialog, m_tab);

    // Destructive action Secondary at the leading edge, the safe one Primary (958:28033)
    QPushButton* reloadButton = dialog->addActionButton(tr("Reload and Lose Changes"), PDFMessageBox::ActionPosition::Leading, false);
    reloadButton->setObjectName(QStringLiteral("reloadButton"));
    QPushButton* keepButton = dialog->addActionButton(tr("Keep My Version"), PDFMessageBox::ActionPosition::Trailing, true);
    keepButton->setObjectName(QStringLiteral("keepButton"));
    connect(reloadButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(keepButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, &PDFAppAutoRefresh::onDialogFinished);

    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void PDFAppAutoRefresh::onDialogFinished(int result)
{
    if (sender() != m_dialog.data())
    {
        return;
    }

    m_dialog = nullptr;
    PDFAppAutoRefreshReader::Result pending = std::move(m_pendingResult);
    m_pendingResult = PDFAppAutoRefreshReader::Result();

    PDFAppDocument* document = m_document.data();
    if (!document || !document->isReady() || pending.filePath != m_watchedPath)
    {
        return;
    }

    QPointer<PDFAppAutoRefresh> guard(this);
    if (result == QDialog::Accepted)
    {
        ++m_reloadCount;
        m_keptData.clear();
        if (pending.outcome == PDFAppAutoRefreshReader::Outcome::PasswordRequired)
        {
            if (PDFAppDocumentLoader* loader = m_tab ? m_tab->getLoader() : nullptr)
            {
                loader->start(pending.filePath);
            }
        }
        else
        {
            document->reload(pending.document, pending.data, pending.isPasswordProtected);
            if (guard)
            {
                Q_EMIT reloaded();
            }
        }
    }
    else
    {
        // Keep My Version: this file is not offered again
        m_keptData = pending.data;
    }

    if (guard && m_isCheckPending)
    {
        scheduleCheck(0);
    }
}

void PDFAppAutoRefresh::closeDialog()
{
    if (PDFMessageBox* dialog = m_dialog.data())
    {
        m_dialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

}   // namespace pdf
