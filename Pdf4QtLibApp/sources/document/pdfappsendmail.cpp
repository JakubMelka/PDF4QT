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

#include "pdfappsendmail.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"

#include "pdfmessagebox.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <MAPI.h>
#endif

#include <array>
#include <memory>
#include <string>

namespace pdf
{

/// Helpers of the e-mail
class PDFAppSendMailHelper
{
public:
    PDFAppSendMailHelper() = delete;

    /// Guards the launcher (the tests set it, a worker reads it)
    static QMutex& getMutex()
    {
        static QMutex mutex;
        return mutex;
    }

    /// Pool of the handover. MAPISendMail with its dialog blocks its thread until
    /// the user closes the message, so it does not take a thread of the global
    /// pool; the pool is never destroyed (its destructor would wait for an open
    /// message at the exit of the application).
    static QThreadPool* getPool()
    {
#if defined(Q_OS_WIN)
        static QThreadPool* pool = new QThreadPool();
        return pool;
#else
        return QThreadPool::globalInstance();
#endif
    }

    /// Width of the message boxes (958:28033)
    static constexpr int MESSAGE_BOX_WIDTH = 440;

    /// Character, which xdg-email cannot pass to the e-mail program in a path
    static bool isUnsafeXdgCharacter(QChar character)
    {
        const QChar::Category category = character.category();
        return character == QLatin1Char('\'') || character == QLatin1Char(',') || category == QChar::Other_Control || category == QChar::Other_Format ||
               category == QChar::Separator_Line || category == QChar::Separator_Paragraph || character.isSurrogate() || character.isNonCharacter();
    }

    /// Private folder of the copies of the attachments: it lives until the exit
    /// of the application (the e-mail program reads the file after the handover);
    /// guarded by getMutex
    static std::unique_ptr<QTemporaryDir>& getCopyFolder()
    {
        static std::unique_ptr<QTemporaryDir> folder;
        return folder;
    }

#if defined(Q_OS_WIN)
    /// Single-threaded apartment of COM of the current thread for its lifetime
    /// (a thread of another apartment keeps it - it is not released then)
    class ComApartment
    {
    public:
        ComApartment() :
            m_isInitialized(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        {

        }

        ~ComApartment()
        {
            if (m_isInitialized)
            {
                ::CoUninitialize();
            }
        }

        ComApartment(const ComApartment&) = delete;
        ComApartment& operator=(const ComApartment&) = delete;

    private:
        bool m_isInitialized;
    };
#endif
};

PDFAppSendMail::PDFAppSendMail(PDFAppDocumentTab* tab, PDFAppDocumentSaver* saver, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_document(tab ? tab->getDocument() : nullptr),
    m_saver(saver)
{
    if (m_saver)
    {
        connect(m_saver, &PDFAppDocumentSaver::finished, this, &PDFAppSendMail::onSaverFinished);
    }
}

PDFAppSendMail::~PDFAppSendMail()
{
    // The handover in the worker continues (the e-mail program has the file)
    if (m_watcher)
    {
        m_watcher->disconnect(this);
    }
    closeDialog();
}

PDFMessageBox* PDFAppSendMail::getDialog() const
{
    return m_dialog.data();
}

PDFAppSendMail::Launcher& PDFAppSendMail::getLauncher()
{
    static Launcher launcher;
    return launcher;
}

void PDFAppSendMail::setLauncher(Launcher launcher)
{
    QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
    getLauncher() = std::move(launcher);
}

std::optional<bool>& PDFAppSendMail::getAvailableOverride()
{
    static std::optional<bool> isAvailable;
    return isAvailable;
}

void PDFAppSendMail::setAvailableOverride(std::optional<bool> isAvailable)
{
    QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
    getAvailableOverride() = isAvailable;
}

bool PDFAppSendMail::isAvailable()
{
    {
        QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
        if (getAvailableOverride().has_value())
        {
            return *getAvailableOverride();
        }
        if (getLauncher())
        {
            return true;
        }
    }

#if defined(Q_OS_WIN) || (defined(Q_OS_UNIX) && !defined(Q_OS_MACOS))
    return true;
#else
    return false;
#endif
}

QStringList PDFAppSendMail::getXdgEmailArguments(const QString& filePath, const QString& subject)
{
    // Each value is one argument (no shell); "--" is not used, xdg-email takes the
    // options first and the recipients last - there is no recipient.
    //
    // xdg-email passes the subject to Thunderbird inside of single quotes of its
    // -compose text (to='',subject='…',attachment='…'): an apostrophe of the name of
    // the file would end the value and add another field (",attachment='…'" would
    // attach a file of the user). The apostrophe becomes U+2019, control and format
    // characters a space.
    QString safeSubject = subject.left(MAXIMUM_SUBJECT_LENGTH);
    for (QChar& character : safeSubject)
    {
        const QChar::Category category = character.category();
        if (character == QLatin1Char('\''))
        {
            character = QChar(0x2019);
        }
        else if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
        {
            character = QLatin1Char(' ');
        }
    }
    return { QStringLiteral("--subject"), safeSubject, QStringLiteral("--attach"), QFileInfo(filePath).absoluteFilePath() };
}

std::optional<bool>& PDFAppSendMail::getXdgOverride()
{
    static std::optional<bool> isXdg;
    return isXdg;
}

void PDFAppSendMail::setXdgOverride(std::optional<bool> isXdg)
{
    QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
    getXdgOverride() = isXdg;
}

bool PDFAppSendMail::isXdg()
{
    {
        QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
        if (getXdgOverride().has_value())
        {
            return *getXdgOverride();
        }
    }

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool PDFAppSendMail::isSafeXdgAttachmentPath(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }
    for (const QChar character : filePath)
    {
        if (PDFAppSendMailHelper::isUnsafeXdgCharacter(character))
        {
            return false;
        }
    }
    return true;
}

QString PDFAppSendMail::getXdgAttachmentPath(const QString& filePath)
{
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (isSafeXdgAttachmentPath(absolutePath))
    {
        return absolutePath;
    }

    // The name of the copy: the unsafe characters replaced, the extension kept
    QString name = QFileInfo(absolutePath).fileName();
    for (QChar& character : name)
    {
        if (PDFAppSendMailHelper::isUnsafeXdgCharacter(character))
        {
            character = QLatin1Char('_');
        }
    }
    if (name.isEmpty())
    {
        name = QStringLiteral("document.pdf");
    }

    QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
    std::unique_ptr<QTemporaryDir>& folder = PDFAppSendMailHelper::getCopyFolder();
    if (!folder)
    {
        // Readable only by the owner; a temporary folder, whose own path is not
        // safe, cannot help
        folder = std::make_unique<QTemporaryDir>(QDir(QDir::tempPath()).filePath(QStringLiteral("pdf4qt-mail-XXXXXX")));
        if (!folder->isValid() || !isSafeXdgAttachmentPath(folder->path()))
        {
            folder.reset();
            return QString();
        }
        QFile::setPermissions(folder->path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    // Each e-mail has its own subfolder (the same name may be sent twice); the
    // subfolder is reserved under the lock, the copy is made without it (a large
    // document would block isAvailable of the thread of the user interface)
    const QDir root(folder->path());
    QString copyPath;
    for (int i = 1; i < 10000 && copyPath.isEmpty(); ++i)
    {
        const QString subfolder = QString::number(i);
        if (!root.exists(subfolder) && root.mkdir(subfolder))
        {
            copyPath = root.filePath(subfolder + QLatin1Char('/') + name);
        }
    }
    locker.unlock();

    if (copyPath.isEmpty() || !QFile::copy(absolutePath, copyPath))
    {
        return QString();
    }
    QFile::setPermissions(copyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return copyPath;
}

bool PDFAppSendMail::canSend() const
{
    const PDFAppDocument* document = m_document.data();
    return document && document->isReady() && isAvailable() && !isSending() && !m_isWaitingForSave;
}

void PDFAppSendMail::send()
{
    PDFAppDocument* document = m_document.data();
    if (!canSend() || !document)
    {
        return;
    }

    // An open editor of a form field is a change, which is sent too (step 4,
    // package D): it is committed first, so the document asks to be saved
    QPointer<PDFAppSendMail> guard(this);
    document->commitPendingEdits();
    if (!guard)
    {
        return;
    }
    document = m_document.data();
    if (!document || !document->isReady())
    {
        return;
    }

    if (document->isModified() || document->getFilePath().isEmpty())
    {
        showSaveFirstDialog();
        return;
    }

    sendFile();
}

void PDFAppSendMail::sendFile()
{
    PDFAppDocument* document = m_document.data();
    if (!document || document->getFilePath().isEmpty() || isSending())
    {
        return;
    }

    QFutureWatcher<PDFAppMailResult>* watcher = new QFutureWatcher<PDFAppMailResult>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<PDFAppMailResult>::finished, this, &PDFAppSendMail::onLaunchFinished);
    watcher->setFuture(QtConcurrent::run(PDFAppSendMailHelper::getPool(), &PDFAppSendMail::launch, document->getFilePath(), document->getTitle()));
    Q_EMIT stateChanged();
}

PDFAppMailResult PDFAppSendMail::launch(QString filePath, QString subject)
{
    Launcher launcher;
    {
        QMutexLocker locker(&PDFAppSendMailHelper::getMutex());
        launcher = getLauncher();
    }

    // xdg-email: a path, which would inject fields into the -compose text of
    // Thunderbird, is replaced by a safe copy (or the e-mail is not created)
    if (isXdg())
    {
        filePath = getXdgAttachmentPath(filePath);
        if (filePath.isEmpty())
        {
            return PDFAppMailResult::AttachmentFailed;
        }
    }

    if (launcher)
    {
        return launcher(filePath, subject);
    }
    return launchPlatform(filePath, subject);
}

PDFAppMailResult PDFAppSendMail::launchPlatform(const QString& filePath, const QString& subject)
{
#if defined(Q_OS_WIN)
    // Simple MAPI of the system: MAPI32.dll of the system folder only (never a
    // library found by the search path)
    std::array<wchar_t, MAX_PATH> systemDirectory = { };
    const UINT length = ::GetSystemDirectoryW(systemDirectory.data(), UINT(systemDirectory.size()));
    if (length == 0 || length >= systemDirectory.size())
    {
        return PDFAppMailResult::NoMailProgram;
    }

    // The e-mail program shows its message window from this thread of the pool:
    // a single-threaded apartment of COM, as the Send To of the shell has (Outlook
    // needs it for its window), released at the end of the handover
    const PDFAppSendMailHelper::ComApartment apartment;

    const std::wstring libraryPath = std::wstring(systemDirectory.data(), length) + L"\\MAPI32.dll";
    HMODULE library = ::LoadLibraryExW(libraryPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
    {
        return PDFAppMailResult::NoMailProgram;
    }

    LPMAPISENDMAILW sendMail = reinterpret_cast<LPMAPISENDMAILW>(reinterpret_cast<void*>(::GetProcAddress(library, "MAPISendMailW")));
    if (!sendMail)
    {
        ::FreeLibrary(library);
        return PDFAppMailResult::NoMailProgram;
    }

    const QFileInfo fileInfo(filePath);
    std::wstring subjectString = subject.toStdWString();
    std::wstring fileNameString = fileInfo.fileName().toStdWString();
    std::wstring filePathString = QDir::toNativeSeparators(fileInfo.absoluteFilePath()).toStdWString();

    MapiFileDescW fileDescription = { };
    fileDescription.nPosition = ULONG(-1);
    fileDescription.lpszFileName = fileNameString.data();
    fileDescription.lpszPathName = filePathString.data();

    MapiMessageW message = { };
    message.lpszSubject = subjectString.data();
    message.nFileCount = 1;
    message.lpFiles = &fileDescription;

    // No parent window: the dialog of the e-mail program does not block the window
    const ULONG result = sendMail(0, 0, &message, MAPI_LOGON_UI | MAPI_DIALOG, 0);
    ::FreeLibrary(library);

    switch (result)
    {
        case SUCCESS_SUCCESS:
        case MAPI_USER_ABORT:
            return PDFAppMailResult::Created;

        case MAPI_E_LOGIN_FAILURE:
        case MAPI_E_NOT_SUPPORTED:
            return PDFAppMailResult::NoMailProgram;

        default:
            return PDFAppMailResult::Failed;
    }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // xdg-email of the desktop: an argument list, no shell
    return QProcess::startDetached(QStringLiteral("xdg-email"), getXdgEmailArguments(filePath, subject)) ? PDFAppMailResult::Created : PDFAppMailResult::NoMailProgram;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(subject);
    return PDFAppMailResult::NoMailProgram;
#endif
}

void PDFAppSendMail::onLaunchFinished()
{
    QFutureWatcher<PDFAppMailResult>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppMailResult result = watcher->future().result();
    m_watcher = nullptr;
    watcher->deleteLater();

    QPointer<PDFAppSendMail> guard(this);
    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }

    if (result != PDFAppMailResult::Created)
    {
        showErrorDialog(result);
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT finished(result);
}

void PDFAppSendMail::showSaveFirstDialog()
{
    PDFAppDocument* document = m_document.data();
    if (!document)
    {
        return;
    }

    closeDialog();

    const QString text = document->getFilePath().isEmpty() ? tr("The document has not been saved to a file yet. The e-mail takes the file from the disk, so save the document first.")
                                                           : tr("%1 has unsaved changes. The e-mail takes the file from the disk, so save the changes first.").arg(document->getTitle());
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Information, tr("Save the document first?"), text, QDialogButtonBox::NoButton, m_tab ? m_tab->window() : nullptr);
    dialog->setObjectName(QStringLiteral("saveFirstDialog"));
    dialog->setCardWidth(PDFAppSendMailHelper::MESSAGE_BOX_WIDTH);
    PDFAppMainWindow::prepareDialog(dialog, m_tab);

    QPushButton* cancelButton = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    QPushButton* saveButton = dialog->addActionButton(tr("Save and Send"), PDFMessageBox::ActionPosition::Trailing, true);
    saveButton->setObjectName(QStringLiteral("saveButton"));
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(dialog, &QDialog::finished, this, &PDFAppSendMail::onSaveFirstFinished);

    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void PDFAppSendMail::onSaveFirstFinished(int result)
{
    if (sender() != m_dialog.data())
    {
        return;
    }
    m_dialog = nullptr;

    if (result != QDialog::Accepted || !m_saver)
    {
        return;
    }

    // The e-mail follows the end of the save (onSaverFinished)
    m_isWaitingForSave = true;
    Q_EMIT stateChanged();

    QPointer<PDFAppSendMail> guard(this);
    if (!m_saver->save() && guard)
    {
        m_isWaitingForSave = false;
        Q_EMIT stateChanged();
    }
}

void PDFAppSendMail::onSaverFinished(bool isSaved)
{
    if (!m_isWaitingForSave)
    {
        return;
    }

    m_isWaitingForSave = false;
    QPointer<PDFAppSendMail> guard(this);
    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }

    const PDFAppDocument* document = m_document.data();
    if (isSaved && document && !document->isModified() && !document->getFilePath().isEmpty())
    {
        sendFile();
    }
}

void PDFAppSendMail::showErrorDialog(PDFAppMailResult result)
{
    closeDialog();

    QString text;
    switch (result)
    {
        case PDFAppMailResult::NoMailProgram:
            text = tr("No e-mail program is set up on this computer. Set up an e-mail program, or attach the file to a message yourself.");
            break;

        case PDFAppMailResult::AttachmentFailed:
            text = tr("The file cannot be handed over to the e-mail program. Attach the file to a message yourself.");
            break;

        case PDFAppMailResult::Created:
        case PDFAppMailResult::Failed:
            text = tr("The e-mail program has refused the message. Attach the file to a message yourself.");
            break;
    }
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("The e-mail could not be created"), text, QDialogButtonBox::Ok, m_tab ? m_tab->window() : nullptr);
    dialog->setObjectName(QStringLiteral("mailErrorDialog"));
    dialog->setCardWidth(PDFAppSendMailHelper::MESSAGE_BOX_WIDTH);
    if (m_document && !m_document->getFilePath().isEmpty())
    {
        dialog->setDetailedText(QDir::toNativeSeparators(m_document->getFilePath()));
    }
    PDFAppMainWindow::prepareDialog(dialog, m_tab);
    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void PDFAppSendMail::closeDialog()
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
