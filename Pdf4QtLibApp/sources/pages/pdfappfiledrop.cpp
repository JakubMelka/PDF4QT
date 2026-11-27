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

#include "pdfappfiledrop.h"
#include "pdfappassemblyloader.h"
#include "pdfappleaveflow.h"
#include "pdfappmainwindow.h"
#include "pdfdocumentassemblyfile.h"
#include "pdfdatabase.h"
#include "pdfrepositoryhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QPointer>
#include <QUrl>
#include <QWidget>

#include <algorithm>

namespace pdf
{

/// Helpers of the drop of files
class PDFAppFileDropHelper
{
public:
    PDFAppFileDropHelper() = delete;

    /// Longest text of a refused item shown to the user
    static constexpr int MAXIMUM_TEXT_LENGTH = 260;

    static QString shorten(const QString& text)
    {
        return text.size() > MAXIMUM_TEXT_LENGTH ? text.left(MAXIMUM_TEXT_LENGTH - 1) + QChar(0x2026) : text;
    }

    static void refuse(PDFAppFileDrop::Result& result, const QString& text, PDFAppFileDrop::Refusal refusal)
    {
        // The list of the refused items is bounded as well (a drop of a whole disk)
        if (result.refused.size() < size_t(PDFAppFileDrop::MAXIMUM_ITEMS))
        {
            result.refused.push_back({ shorten(text), refusal });
        }
    }
};

bool PDFAppFileDrop::hasLocalFiles(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
    {
        return false;
    }

    const QList<QUrl> urls = mimeData->urls();
    int examined = 0;
    for (const QUrl& url : urls)
    {
        if (++examined > MAXIMUM_ITEMS)
        {
            break;
        }

        if (url.isLocalFile() && url.host().isEmpty() && !isNetworkPath(url.toLocalFile()))
        {
            return true;
        }
    }
    return false;
}

PDFAppFileDrop::Result PDFAppFileDrop::getFiles(const QMimeData* mimeData, int maximumFiles, bool isImageAccepted, bool isAssemblyAccepted)
{
    Result result;
    if (!mimeData || !mimeData->hasUrls())
    {
        return result;
    }

    const QList<QUrl> urls = mimeData->urls();
    int examined = 0;
    for (const QUrl& url : urls)
    {
        if (++examined > MAXIMUM_ITEMS)
        {
            break;
        }

        // A file of another computer is never touched (an access to a share sends
        // the credentials of the user to its server, as the navigator refuses it)
        const QString localFile = url.isLocalFile() && url.host().isEmpty() ? url.toLocalFile() : QString();
        if (localFile.isEmpty() || isNetworkPath(localFile) || !QDir::isAbsolutePath(localFile) || localFile.contains(QChar(0)) ||
            localFile.size() > PDFRepositoryHelper::MAXIMUM_PATH_LENGTH)
        {
            PDFAppFileDropHelper::refuse(result, url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString(QUrl::RemoveUserInfo), Refusal::Remote);
            continue;
        }

        const QFileInfo info(localFile);
        const QString displayPath = QDir::toNativeSeparators(info.absoluteFilePath());
        if (info.isDir())
        {
            PDFAppFileDropHelper::refuse(result, displayPath, Refusal::Folder);
            continue;
        }

        if (!info.isFile())
        {
            PDFAppFileDropHelper::refuse(result, displayPath, Refusal::Missing);
            continue;
        }

        // An assembly file is taken by its suffix (nothing is read; its loader checks the content)
        const bool isAssembly = isAssemblyAccepted && PDFAppAssemblyLoader::isAssemblyFile(info.absoluteFilePath());
        if (!isAssembly && !isPdfFile(info.absoluteFilePath()) && !(isImageAccepted && isImageFile(info.absoluteFilePath())))
        {
            PDFAppFileDropHelper::refuse(result, displayPath, isImageAccepted ? Refusal::NotPdfOrImage : Refusal::NotPdf);
            continue;
        }

        const QString path = info.absoluteFilePath();
        if (result.files.contains(path))
        {
            continue;
        }

        if (result.files.size() >= qMax(0, maximumFiles))
        {
            PDFAppFileDropHelper::refuse(result, displayPath, Refusal::TooMany);
            continue;
        }

        result.files << path;
    }

    return result;
}

bool PDFAppFileDrop::isPdfFile(const QString& filePath)
{
    if (filePath.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
    {
        return true;
    }

    if (isNetworkPath(filePath))
    {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    return isPdfData(file.read(HEADER_LENGTH));
}

bool PDFAppFileDrop::isImageFile(const QString& filePath)
{
    return !isNetworkPath(filePath) && !QImageReader::imageFormat(filePath).isEmpty();
}

bool PDFAppFileDrop::isPdfData(const QByteArray& data)
{
    return data.left(HEADER_LENGTH).contains("%PDF-");
}

bool PDFAppFileDrop::isNetworkPath(const QString& filePath)
{
    // Any two separators at the start: Windows takes also "/\server" or "\/server"
    // as a UNC path (and QFileInfo converts the separators, file:///%5Cserver%5Cshare
    // gives such a path); the device namespaces "\\.\", "\\?\" start the same way
    if (filePath.size() < 2)
    {
        return false;
    }

    const QChar first = filePath.at(0);
    const QChar second = filePath.at(1);
    const bool isFirstSeparator = first == QLatin1Char('/') || first == QLatin1Char('\\');
    const bool isSecondSeparator = second == QLatin1Char('/') || second == QLatin1Char('\\');
    return isFirstSeparator && isSecondSeparator;
}

QString PDFAppFileDrop::getRefusalText(Refusal refusal)
{
    switch (refusal)
    {
        case Refusal::Remote:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "Not a file on this computer");
        case Refusal::Folder:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "A folder");
        case Refusal::Missing:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "The file does not exist");
        case Refusal::NotPdf:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "Not a PDF file");
        case Refusal::NotPdfOrImage:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "Neither a PDF file nor an image");
        case Refusal::TooMany:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "Too many files at once");
    }
    return QString();
}

// ---------------------------------------------------------------- file dialog request

PDFAppFileDialogRequest::PDFAppFileDialogRequest(QObject* requester, QWidget* parent, const Settings& settings) :
    BaseClass(requester),
    m_parent(parent),
    m_settings(settings)
{
    setObjectName(QStringLiteral("fileDialogRequest"));
    getRegistry().push_back(this);
}

PDFAppFileDialogRequest::~PDFAppFileDialogRequest()
{
    // An open dialog stays (QR3.7, RR3-M2: a native dialog is never closed from the program); its answer is dropped
    unregister();
}

PDFAppFileDialogRequest* PDFAppFileDialogRequest::create(QObject* requester, QWidget* parent, const Settings& settings)
{
    return requester ? new PDFAppFileDialogRequest(requester, parent, settings) : nullptr;
}

void PDFAppFileDialogRequest::show()
{
    if (!m_isPending || m_dialog || m_dialogAnswer || isHandlerSet())
    {
        return;
    }

    // The arguments of the static functions of QFileDialog: the constructor takes the folder of the path and selects
    // its name (QFileDialogArgs), then the mode, the options and the selection as QFileDialog(const QFileDialogArgs&)
    QWidget* window = m_parent ? m_parent->window() : nullptr;
    QFileDialog* dialog = new QFileDialog(window, m_settings.title, m_settings.path, m_settings.filter);
    dialog->setObjectName(QStringLiteral("fileDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setSupportedSchemes(QStringList(QStringLiteral("file")));
    switch (m_settings.mode)
    {
        case Mode::OpenFiles:
            dialog->setFileMode(QFileDialog::ExistingFiles);
            break;

        case Mode::OpenFile:
            dialog->setFileMode(QFileDialog::ExistingFile);
            break;

        case Mode::SaveFile:
            dialog->setFileMode(QFileDialog::AnyFile);
            dialog->setAcceptMode(QFileDialog::AcceptSave);
            break;

        case Mode::Directory:
            dialog->setFileMode(QFileDialog::Directory);
            dialog->setOptions(QFileDialog::ShowDirsOnly);
            break;
    }
    if (!m_settings.defaultSuffix.isEmpty())
    {
        dialog->setDefaultSuffix(m_settings.defaultSuffix);
    }
    dialog->selectFile(getInitialSelection(m_settings.path));

    m_dialog = dialog;
    m_dialogAnswer = PDFAppDialogAnswer::watch(dialog, this, &PDFAppFileDialogRequest::onDialogAnswered);
    PDFAppDialogAnswer::showModal(dialog);
}

void PDFAppFileDialogRequest::answerByHandler(FilesHandler handler)
{
    if (!handler || !canAnswerByHandler())
    {
        return;
    }

    m_filesHandler = std::move(handler);
    queueHandlerAnswer();
}

void PDFAppFileDialogRequest::answerByHandler(FileHandler handler)
{
    if (!handler || !canAnswerByHandler())
    {
        return;
    }

    m_fileHandler = std::move(handler);
    queueHandlerAnswer();
}

void PDFAppFileDialogRequest::answerByHandler(PanelFileHandler handler)
{
    if (!handler || !canAnswerByHandler())
    {
        return;
    }

    m_panelFileHandler = std::move(handler);
    queueHandlerAnswer();
}

void PDFAppFileDialogRequest::queueHandlerAnswer()
{
    // The handler answers later, as the user of a real dialog: the slot of this request (a child of the requester) -
    // a request deleted before it calls nothing
    QMetaObject::invokeMethod(this, &PDFAppFileDialogRequest::onHandlerAnswer, Qt::QueuedConnection);
}

std::vector<PDFAppFileDialogRequest*> PDFAppFileDialogRequest::getPendingRequests()
{
    return getRegistry();
}

QFileDialog* PDFAppFileDialogRequest::getDialog() const
{
    return m_isPending ? m_dialog.data() : nullptr;
}

void PDFAppFileDialogRequest::answer(const QStringList& files)
{
    if (!m_isPending)
    {
        return;
    }

    // The dialog closes without its own answer: its watcher goes first; its close runs code of Qt
    delete m_dialogAnswer.data();
    QPointer<PDFAppFileDialogRequest> guard(this);
    if (QFileDialog* dialog = m_dialog.data())
    {
        dialog->reject();
    }
    if (guard)
    {
        deliver(files);
    }
}

QStringList PDFAppFileDialogRequest::getSelectedFiles(const QFileDialog* dialog, Mode mode)
{
    QStringList files;
    if (!dialog)
    {
        return files;
    }

    const QList<QUrl> urls = dialog->selectedUrls();
    if (mode == Mode::OpenFiles)
    {
        for (const QUrl& url : urls)
        {
            files << url.toString(QUrl::PreferLocalFile);
        }
        return files;
    }

    const QUrl url = urls.value(0);
    const QString file = url.isLocalFile() || url.isEmpty() ? url.toLocalFile() : url.toString();
    if (!file.isEmpty())
    {
        files << file;
    }
    return files;
}

QString PDFAppFileDialogRequest::getInitialSelection(const QString& path)
{
    if (path.isEmpty())
    {
        return QString();
    }

    const QFileInfo info(path);
    return info.isDir() ? QString() : info.fileName();
}

void PDFAppFileDialogRequest::deliver(const QStringList& files)
{
    if (!m_isPending)
    {
        return;
    }

    m_isPending = false;
    m_dialog = nullptr;
    unregister();
    deleteLater();
    if (m_settings.mode == Mode::OpenFiles)
    {
        Q_EMIT filesChosen(files);
    }
    else
    {
        Q_EMIT fileChosen(files.value(0));
    }
}

void PDFAppFileDialogRequest::onDialogAnswered(int code)
{
    // finished: the dialog is alive (it deletes itself later); gone without finished: canceled
    QStringList files;
    if (code == QDialog::Accepted && m_dialog)
    {
        files = getSelectedFiles(m_dialog.data(), m_settings.mode);
    }
    deliver(files);
}

void PDFAppFileDialogRequest::onHandlerAnswer()
{
    if (!m_isPending)
    {
        // Answered meanwhile (answer)
        return;
    }

    // The handler is foreign code (a test): it may delete the requester and this request with it, so it and its
    // arguments are local copies and nothing of this request is used after it unless the request is alive
    FilesHandler filesHandler = std::move(m_filesHandler);
    FileHandler fileHandler = std::move(m_fileHandler);
    PanelFileHandler panelFileHandler = std::move(m_panelFileHandler);
    m_filesHandler = nullptr;
    m_fileHandler = nullptr;
    m_panelFileHandler = nullptr;
    const Settings settings = m_settings;
    QWidget* parent = m_parent.data();
    QPointer<PDFAppFileDialogRequest> guard(this);

    QStringList files;
    if (filesHandler)
    {
        files = filesHandler(parent, settings.path);
    }
    else
    {
        // A single file: an empty text is canceled
        QString file;
        if (fileHandler)
        {
            file = fileHandler(parent, settings.path);
        }
        else if (panelFileHandler)
        {
            file = panelFileHandler(parent, settings.title, settings.path, settings.filter);
        }
        if (!file.isEmpty())
        {
            files << file;
        }
    }

    if (guard)
    {
        guard->deliver(files);
    }
}

std::vector<PDFAppFileDialogRequest*>& PDFAppFileDialogRequest::getRegistry()
{
    // GUI thread only
    static std::vector<PDFAppFileDialogRequest*> requests;
    return requests;
}

void PDFAppFileDialogRequest::unregister()
{
    std::vector<PDFAppFileDialogRequest*>& requests = getRegistry();
    requests.erase(std::remove(requests.begin(), requests.end(), this), requests.end());
}

// ---------------------------------------------------------------- file dialogs

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getOpenFileNamesSettings(const QString& folder)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::OpenFiles;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Open PDF Files");
    settings.path = folder;
    settings.filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files (*.pdf);;All files (*)");
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getOpenDocumentFileNamesSettings(const QString& folder)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::OpenFiles;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Open");
    settings.path = folder;
    settings.filter = getOpenDocumentFilter();
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getOpenInputFileNamesSettings(const QString& folder)
{
    // The formats QImageReader reads, next to the PDF files
    QStringList patterns{ QStringLiteral("*.pdf") };
    for (const QByteArray& format : QImageReader::supportedImageFormats())
    {
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    }

    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::OpenFiles;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Add Files");
    settings.path = folder;
    settings.filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files and images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
    return settings;
}

QString PDFAppFileDialogs::getImageFilter()
{
    // The formats QImageReader reads now (its plugins), in its order
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
    {
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    }
    return QCoreApplication::translate("pdf::PDFAppFileDialogs", "Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getOpenImageFileNamesSettings(const QString& folder, bool isMultiple)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = isMultiple ? PDFAppFileDialogRequest::Mode::OpenFiles : PDFAppFileDialogRequest::Mode::OpenFile;
    settings.title = isMultiple ? QCoreApplication::translate("pdf::PDFAppFileDialogs", "Open Images") : QCoreApplication::translate("pdf::PDFAppFileDialogs", "Open Image");
    settings.path = folder;
    settings.filter = getImageFilter();
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getExistingDirectorySettings(const QString& folder, const QString& title)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::Directory;
    settings.title = title.isEmpty() ? QCoreApplication::translate("pdf::PDFAppFileDialogs", "Pin a Location") : title;
    settings.path = folder;
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getSaveFileNameSettings(const QString& filePath)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::SaveFile;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Save As");
    settings.path = filePath;
    settings.filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files (*.pdf)");
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getSaveBackupFileNameSettings(const QString& filePath)
{
    // The dialog adds the suffix itself (a name typed without it), so it asks before an existing backup is replaced;
    // the static getSaveFileName has no default suffix (the native dialog of Windows adds none)
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::SaveFile;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Export Settings");
    settings.path = filePath;
    settings.filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF4QT settings backups (*.pdf4qt-backup)");
    settings.defaultSuffix = QLatin1String(PDFDatabaseBackup::FILE_SUFFIX);
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getOpenBackupFileNameSettings(const QString& folder)
{
    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::OpenFile;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Import Settings");
    settings.path = folder;
    settings.filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF4QT settings backups (*.pdf4qt-backup);;All files (*)");
    return settings;
}

PDFAppFileDialogRequest::Settings PDFAppFileDialogs::getPrintFileNameSettings(const QString& filePath, const QString& suffix)
{
    QString filter;
    if (suffix == QLatin1String("pdf"))
    {
        filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files (*.pdf)");
    }
    else if (suffix == QLatin1String("oxps") || suffix == QLatin1String("xps"))
    {
        filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "XPS files (*.oxps *.xps)");
    }
    else
    {
        filter = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Printer files (*.prn)");
    }
    filter += QStringLiteral(";;") + QCoreApplication::translate("pdf::PDFAppFileDialogs", "All files (*)");

    PDFAppFileDialogRequest::Settings settings;
    settings.mode = PDFAppFileDialogRequest::Mode::SaveFile;
    settings.title = QCoreApplication::translate("pdf::PDFAppFileDialogs", "Print to File");
    settings.path = filePath;
    settings.filter = filter;
    return settings;
}

PDFAppFileDialogRequest* PDFAppFileDialogs::begin(QObject* requester, QWidget* parent, const PDFAppFileDialogRequest::Settings& settings, Handler handler)
{
    PDFAppFileDialogRequest* request = PDFAppFileDialogRequest::create(requester, parent, settings);
    if (!request)
    {
        return nullptr;
    }

    // The handler of the tests (the one set now) answers as the user of a real dialog, later: the request calls it
    // from the event loop (the widget and the path of the settings, as the dialog). This call runs no foreign code.
    const PDFAppFileDialogRequest::FileHandler* fileHandler = nullptr;
    switch (handler)
    {
        case Handler::OpenFiles:
            if (const OpenFilesHandler& openFilesHandler = getOpenFilesHandler())
            {
                request->answerByHandler(openFilesHandler);
                return request;
            }
            break;

        case Handler::Folder:
            fileHandler = &getFolderHandler();
            break;

        case Handler::SaveFile:
            fileHandler = &getSaveFileHandler();
            break;

        case Handler::SaveBackup:
            fileHandler = &getSaveBackupHandler();
            break;

        case Handler::OpenBackup:
            fileHandler = &getOpenBackupHandler();
            break;

        case Handler::PrintFile:
            fileHandler = &getPrintFileHandler();
            break;
    }

    if (fileHandler && *fileHandler)
    {
        request->answerByHandler(*fileHandler);
        return request;
    }

    request->show();
    return request;
}

QString PDFAppFileDialogs::getOpenDocumentFilter()
{
    const QString assemblies = QStringLiteral("*.") + QString::fromLatin1(PDFDocumentAssemblyFile::FILE_SUFFIX);
    return QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files and assemblies (*.pdf %1);;PDF files (*.pdf);;PDF4QT assemblies (%1);;All files (*)").arg(assemblies);
}

void PDFAppFileDialogs::setSaveFileHandler(SaveFileHandler handler)
{
    getSaveFileHandler() = std::move(handler);
}

void PDFAppFileDialogs::setPrintFileHandler(SaveFileHandler handler)
{
    getPrintFileHandler() = std::move(handler);
}

PDFAppFileDialogs::SaveFileHandler& PDFAppFileDialogs::getPrintFileHandler()
{
    static SaveFileHandler handler;
    return handler;
}

void PDFAppFileDialogs::setSaveBackupHandler(SaveFileHandler handler)
{
    getSaveBackupHandler() = std::move(handler);
}

void PDFAppFileDialogs::setOpenBackupHandler(FolderHandler handler)
{
    getOpenBackupHandler() = std::move(handler);
}

PDFAppFileDialogs::SaveFileHandler& PDFAppFileDialogs::getSaveBackupHandler()
{
    static SaveFileHandler handler;
    return handler;
}

PDFAppFileDialogs::FolderHandler& PDFAppFileDialogs::getOpenBackupHandler()
{
    static FolderHandler handler;
    return handler;
}

PDFAppFileDialogs::SaveFileHandler& PDFAppFileDialogs::getSaveFileHandler()
{
    static SaveFileHandler handler;
    return handler;
}

void PDFAppFileDialogs::setOpenFilesHandler(OpenFilesHandler handler)
{
    getOpenFilesHandler() = std::move(handler);
}

void PDFAppFileDialogs::setFolderHandler(FolderHandler handler)
{
    getFolderHandler() = std::move(handler);
}

PDFAppFileDialogs::OpenFilesHandler& PDFAppFileDialogs::getOpenFilesHandler()
{
    static OpenFilesHandler handler;
    return handler;
}

PDFAppFileDialogs::FolderHandler& PDFAppFileDialogs::getFolderHandler()
{
    static FolderHandler handler;
    return handler;
}

bool PDFAppDocumentActions::openFiles(QWidget* source, const QStringList& files, int page)
{
    PDFAppMainWindow* window = source ? qobject_cast<PDFAppMainWindow*>(source->window()) : nullptr;
    if (!window || files.isEmpty())
    {
        return false;
    }

    window->openDocuments(files, page);
    return true;
}

std::vector<PDFAppOpenDocument> PDFAppDocumentActions::getOpenDocuments(const QWidget* source)
{
    // The documents of the window (PDFAppMainWindow::getOpenDocuments, the document
    // tabs of step 3); a test provider replaces them until step 3 has documents
    if (const OpenDocumentsProvider& provider = getProvider())
    {
        return provider(source);
    }

    const PDFAppMainWindow* window = source ? qobject_cast<const PDFAppMainWindow*>(source->window()) : nullptr;
    return window ? window->getOpenDocuments() : std::vector<PDFAppOpenDocument>();
}

void PDFAppDocumentActions::setOpenDocumentsProvider(OpenDocumentsProvider provider)
{
    getProvider() = std::move(provider);
}

QStringList PDFAppDocumentActions::getPaths(const std::vector<PDFWorkspaceDocument>& documents)
{
    QStringList paths;
    for (const PDFWorkspaceDocument& document : documents)
    {
        if (PDFRepositoryHelper::isValidStoredPath(document.path) && !paths.contains(document.path))
        {
            paths << document.path;
        }
    }
    return paths;
}

std::vector<PDFAppOpenRequest> PDFAppDocumentActions::getRequests(const std::vector<PDFWorkspaceDocument>& documents)
{
    std::vector<PDFAppOpenRequest> requests;
    QStringList paths;
    for (const PDFWorkspaceDocument& document : documents)
    {
        if (PDFRepositoryHelper::isValidStoredPath(document.path) && !paths.contains(document.path))
        {
            paths << document.path;
            PDFAppOpenRequest request;
            request.filePath = document.path;
            request.page = qMax(0, document.page);
            requests.push_back(std::move(request));
        }
    }
    return requests;
}

bool PDFAppDocumentActions::openRequests(QWidget* source, const std::vector<PDFAppOpenRequest>& requests)
{
    PDFAppMainWindow* window = source ? qobject_cast<PDFAppMainWindow*>(source->window()) : nullptr;
    if (!window || requests.empty())
    {
        return false;
    }

    window->openDocuments(requests);
    return true;
}

PDFAppDocumentActions::OpenDocumentsProvider& PDFAppDocumentActions::getProvider()
{
    static OpenDocumentsProvider provider;
    return provider;
}

}   // namespace pdf

#include "moc_pdfappfiledrop.cpp"
