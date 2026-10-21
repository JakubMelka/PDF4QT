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
#include "pdfappmainwindow.h"
#include "pdfrepositoryhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QWidget>

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

PDFAppFileDrop::Result PDFAppFileDrop::getFiles(const QMimeData* mimeData, int maximumFiles)
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

        if (!isPdfFile(info.absoluteFilePath()))
        {
            PDFAppFileDropHelper::refuse(result, displayPath, Refusal::NotPdf);
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

    const QByteArray header = file.read(HEADER_LENGTH);
    return header.contains("%PDF-");
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
        case Refusal::TooMany:
            return QCoreApplication::translate("pdf::PDFAppFileDrop", "Too many files at once");
    }
    return QString();
}

QStringList PDFAppFileDialogs::getOpenFileNames(QWidget* parent, const QString& folder)
{
    if (const OpenFilesHandler& handler = getOpenFilesHandler())
    {
        return handler(parent, folder);
    }

    return QFileDialog::getOpenFileNames(parent, QCoreApplication::translate("pdf::PDFAppFileDialogs", "Open PDF Files"), folder,
                                         QCoreApplication::translate("pdf::PDFAppFileDialogs", "PDF files (*.pdf);;All files (*)"));
}

QString PDFAppFileDialogs::getExistingDirectory(QWidget* parent, const QString& folder)
{
    if (const FolderHandler& handler = getFolderHandler())
    {
        return handler(parent, folder);
    }

    return QFileDialog::getExistingDirectory(parent, QCoreApplication::translate("pdf::PDFAppFileDialogs", "Pin a Location"), folder);
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

PDFAppDocumentActions::OpenDocumentsProvider& PDFAppDocumentActions::getProvider()
{
    static OpenDocumentsProvider provider;
    return provider;
}

}   // namespace pdf
