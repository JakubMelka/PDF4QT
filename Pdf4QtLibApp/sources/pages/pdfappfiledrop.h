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

#ifndef PDFAPPFILEDROP_H
#define PDFAPPFILEDROP_H

#include "pdfappglobal.h"
#include "pdfworkspacerepository.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QMimeData;
class QWidget;

namespace pdf
{

/// Files dropped onto the window (drag and drop of step 2, newapp_navigation.md 3):
/// only local files are taken, a remote address (http, a network share of
/// another computer) and a folder are refused. A file is a PDF by its extension
/// (".pdf", any case - nothing is read) or, without it, by its content: the first
/// HEADER_LENGTH bytes contain the signature "%PDF-" (ISO 32000-2, 7.5.2; readers
/// accept a signature after leading garbage). The content is read only from a
/// local disk, never from a network path (reading it could block the user
/// interface for seconds and would contact the server). At most \p maximumFiles
/// files are taken, the rest is refused.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFileDrop
{
public:
    PDFAppFileDrop() = delete;

    /// Bytes read from the start of a file without the extension .pdf
    static constexpr int HEADER_LENGTH = 1024;

    /// Most files of one drop
    static constexpr int MAXIMUM_FILES = 100;

    /// Most items of a drop, which are examined (a drop of a whole disk)
    static constexpr int MAXIMUM_ITEMS = 10000;

    /// Why a dropped item was not taken
    enum class Refusal
    {
        Remote,     ///< Not a local file (an address, a share of another computer)
        Folder,     ///< A folder
        Missing,    ///< The file does not exist
        NotPdf,     ///< Neither the extension nor the content is of a PDF
        TooMany     ///< More files than MAXIMUM_FILES
    };

    /// Item of a drop, which was refused
    struct RefusedItem
    {
        QString text;           ///< Path or address for the user (shortened)
        Refusal refusal = Refusal::NotPdf;
    };

    /// Result of a drop
    struct Result
    {
        QStringList files;                  ///< Absolute paths of the PDF files, in the order of the drop
        std::vector<RefusedItem> refused;   ///< Refused items
    };

    /// The data may contain files: a drag over the window is accepted (no access
    /// to the file system, it runs at each move of the pointer)
    static bool hasLocalFiles(const QMimeData* mimeData);

    /// Files of the drop (it reads the file system)
    static Result getFiles(const QMimeData* mimeData, int maximumFiles = MAXIMUM_FILES);

    /// The local file is a PDF (by its extension or its content)
    static bool isPdfFile(const QString& filePath);

    /// The path lies on another computer (UNC path, a network share), its content is not read
    static bool isNetworkPath(const QString& filePath);

    /// Translated reason of the refusal
    static QString getRefusalText(Refusal refusal);
};

/// Native dialogs of the system for files and folders (newapplication.md 3: the
/// non-native dialogs of Qt draw from the palette). Tests replace them by handlers.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFileDialogs
{
public:
    PDFAppFileDialogs() = delete;

    using OpenFilesHandler = std::function<QStringList(QWidget*, const QString&)>;
    using FolderHandler = std::function<QString(QWidget*, const QString&)>;
    using SaveFileHandler = std::function<QString(QWidget*, const QString&)>;

    /// Browse files…: PDF files of the folder (multi-select); an empty list = canceled
    static QStringList getOpenFileNames(QWidget* parent, const QString& folder);

    /// Add… of Pinned Locations: a folder; an empty text = canceled
    static QString getExistingDirectory(QWidget* parent, const QString& folder);

    /// Save as… of a document (step 3): a PDF file, \p filePath is the proposed
    /// file (folder and name); an empty text = canceled
    static QString getSaveFileName(QWidget* parent, const QString& filePath);

    /// Handlers replacing the dialogs (tests); an empty handler restores the dialog
    static void setOpenFilesHandler(OpenFilesHandler handler);
    static void setFolderHandler(FolderHandler handler);
    static void setSaveFileHandler(SaveFileHandler handler);

private:
    static OpenFilesHandler& getOpenFilesHandler();
    static FolderHandler& getFolderHandler();
    static SaveFileHandler& getSaveFileHandler();
};

/// Document open in a window, which a workspace can save (Save as workspace
/// 941:136915). Step 3 (document tabs) fills it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenDocument
{
public:
    QString path;           ///< File of the document (empty = not saved to a file yet, "Untitled 1")
    QString title;          ///< Title of the tab
    int page = 0;           ///< Current page (from 1, 0 = unknown)
    int pageCount = 0;      ///< Page count (0 = unknown)
    bool isModified = false;///< Unsaved changes (the saved file is used)
};

/// Request to open a document (PDFAppMainWindow::openDocuments): the command line,
/// another process, the pages Home / Open / Recent, a drop, a workspace (a page per
/// file)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenRequest
{
public:
    QString filePath;       ///< Absolute path of the file
    int page = 0;           ///< Physical page from 1 (0 = the last page of Recent)
};

/// Opening of documents from the pages Home, Open and Recent: the files go to
/// PDFAppMainWindow::openDocuments of the window of the page (step 3 opens them,
/// step 2 records them in Recent). The documents open in the window, which a
/// workspace saves, come from the window as well (step 3).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentActions
{
public:
    PDFAppDocumentActions() = delete;

    using OpenDocumentsProvider = std::function<std::vector<PDFAppOpenDocument>(const QWidget*)>;

    /// Opens the files in the window of the widget (absolute paths, \p page from 1
    /// or 0). \returns false, if the widget is not in a main window.
    static bool openFiles(QWidget* source, const QStringList& files, int page = 0);

    /// Documents open in the window of the widget (PDFAppMainWindow::getOpenDocuments:
    /// there are no document tabs in step 2, the list is empty unless a test sets a provider)
    static std::vector<PDFAppOpenDocument> getOpenDocuments(const QWidget* source);

    /// Provider of the open documents, which replaces the documents of the window
    /// (tests, until step 3 has document tabs; an empty provider restores the window)
    static void setOpenDocumentsProvider(OpenDocumentsProvider provider);

    /// Documents of a workspace as paths (the documents of an invalid path are skipped)
    static QStringList getPaths(const std::vector<PDFWorkspaceDocument>& documents);

    /// Documents of a workspace as requests with their pages (the documents of an
    /// invalid path and the second document of a path are skipped)
    static std::vector<PDFAppOpenRequest> getRequests(const std::vector<PDFWorkspaceDocument>& documents);

    /// Opens the requests (a page per file - workspaces, Restore last session) in
    /// the window of the widget. \returns false, if the widget is not in a main window.
    static bool openRequests(QWidget* source, const std::vector<PDFAppOpenRequest>& requests);

private:
    static OpenDocumentsProvider& getProvider();
};

}   // namespace pdf

#endif // PDFAPPFILEDROP_H
