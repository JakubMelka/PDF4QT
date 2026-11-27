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
#include "pdfdeferreddeletion.h"
#include "pdfworkspacerepository.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QFileDialog;
class QMimeData;

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
/// files are taken, the rest is refused. A drop onto the window takes also the assembly files of
/// Manage Pages (by the suffix .pdf4qtassembly, PDFAppAssemblyLoader::isAssemblyFile; plan of
/// step 3b of phase 2, RB3 e): PDFAppMainWindow::openDocuments opens them by openAssembly.
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
        NotPdfOrImage,  ///< Neither a PDF nor an image of QImageReader (a drop of the inputs of a tool, K8 of step 2 of phase 2)
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

    /// Files of the drop (it reads the file system); \p isImageAccepted takes also the images
    /// of QImageReader (the inputs of a tool with more inputs: PDF files and images),
    /// \p isAssemblyAccepted the assembly files of Manage Pages (a drop onto the window)
    static Result getFiles(const QMimeData* mimeData, int maximumFiles = MAXIMUM_FILES, bool isImageAccepted = false, bool isAssemblyAccepted = false);

    /// The local file is a PDF (by its extension or its content)
    static bool isPdfFile(const QString& filePath);

    /// The local file is an image of QImageReader by its content (a network path is never read)
    static bool isImageFile(const QString& filePath);

    /// The bytes (the start of a file) contain the signature of a PDF "%PDF-" in their first
    /// HEADER_LENGTH bytes (ISO 32000-2, 7.5.2)
    static bool isPdfData(const QByteArray& data);

    /// The path lies on another computer (UNC path, a network share), its content is not read
    static bool isNetworkPath(const QString& filePath);

    /// Translated reason of the refusal
    static QString getRefusalText(Refusal refusal);
};

/// A file dialog of the system shown without an event loop of its own (A0, QR3.7, newapp_robustness.md 15.3): the
/// asynchronous variants of PDFAppFileDialogs and PDFAppPanelFileDialogs create it. The dialog is a QFileDialog
/// (the native dialog of the system: on Windows it runs in the thread QWindowsDialogThread of Qt, the event loop of
/// the application goes on) with the settings of today's static functions of QFileDialog (the folder of the proposed
/// file shown and its name selected, the filter, the mode, only local files), shown modal as their exec() shows it
/// (application modal), never by exec and never by QFileDialog::open(receiver, member).
///
/// Ownership (QR3.7, RR3-M2): the dialog is a child of the window of the parent widget and deletes itself when it is
/// closed (WA_DeleteOnClose); the request is a child of the requester (the receiver of its signal) and owns nothing
/// but its answer. A requester deleted while the dialog is open gets nothing: the request goes with it, the dialog
/// stays until the user closes it (its answer is dropped) - a native dialog is never closed from the program.
///
/// Delivery: exactly once, never inside the call, which has created the request (the caller may connect after it):
/// fileChosen (OpenFile, SaveFile, Directory: the file or the folder, empty = canceled) or filesChosen (OpenFiles:
/// the files, empty = canceled), the last call of the request (the slot may delete anything). The dialog deleted
/// without an answer (with its window) is canceled, from the event loop. A test handler of the helpers (set*Handler)
/// answers like the user of a real dialog, later: the call only remembers it, a queued member slot of the request calls
/// it from the event loop and delivers its answer right after it. The call runs no foreign code, so the requester may
/// keep the returned request; a requester deleted before (also by the handler itself) gets nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFileDialogRequest : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// What the dialog chooses
    enum class Mode
    {
        OpenFiles,  ///< Existing files (QFileDialog::getOpenFileNames)
        OpenFile,   ///< One existing file (getOpenFileName)
        SaveFile,   ///< A file to write (getSaveFileName: the dialog asks before an existing file is replaced)
        Directory   ///< An existing folder (getExistingDirectory with ShowDirsOnly)
    };

    /// The dialog (the arguments of the static functions of QFileDialog)
    class Settings
    {
    public:
        Mode mode = Mode::OpenFile;
        QString title;          ///< Title of the dialog
        QString path;           ///< The folder shown, or the proposed file (its folder is shown, its name selected)
        QString filter;         ///< Name filters separated by ";;" (empty = all files)
        QString defaultSuffix;  ///< Suffix added to a name typed without one (empty = none)
    };

    /// Test handlers, which replace the dialog: (the widget of the request, the path of the settings) -> the files /
    /// the file (empty = canceled); the handler of a panel gets (widget, title, path, filter)
    using FilesHandler = std::function<QStringList(QWidget*, const QString&)>;
    using FileHandler = std::function<QString(QWidget*, const QString&)>;
    using PanelFileHandler = std::function<QString(QWidget*, const QString&, const QString&, const QString&)>;

    virtual ~PDFAppFileDialogRequest() override;

    /// Creates the request of \p requester (its parent and the receiver of its signal). \p parent is the widget of
    /// the request: the dialog belongs to its window. Nothing is shown yet (show, answerByHandler). \returns nullptr
    /// without a requester.
    static PDFAppFileDialogRequest* create(QObject* requester, QWidget* parent, const Settings& settings);

    /// Shows the dialog (once) and returns at once
    void show();

    /// A test handler answers instead of the dialog (once): it is only remembered here; it is called from the event
    /// loop (a queued member slot: the widget of the request, nullptr when gone, and the arguments of the settings)
    /// and its answer is delivered right after it. A request deleted before (with its requester) calls nothing.
    void answerByHandler(FilesHandler handler);
    void answerByHandler(FileHandler handler);
    void answerByHandler(PanelFileHandler handler);

    /// Connects the signal of the mode to the member slot of the receiver (the requester). The request is a child of
    /// the requester; a receiver of another subtree is safe too: the connection is cut, when the receiver is retired
    /// (PDFDeferredDeletion::connect, step A)
    template<typename Receiver>
    void connectTo(Receiver* receiver, void (Receiver::*slot)(const QStringList&))
    {
        PDFDeferredDeletion::connect(this, &PDFAppFileDialogRequest::filesChosen, receiver, slot);
    }

    template<typename Receiver>
    void connectTo(Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        PDFDeferredDeletion::connect(this, &PDFAppFileDialogRequest::fileChosen, receiver, slot);
    }

    /// The requests, which wait for their answer, oldest first (tests: AppAsyncDialogs answers them)
    static std::vector<PDFAppFileDialogRequest*> getPendingRequests();

    const Settings& getSettings() const { return m_settings; }

    /// The widget of the request (nullptr after it has gone)
    QWidget* getParentWidget() const { return m_parent.data(); }

    /// The open dialog (nullptr: a test handler answers, the dialog has ended or gone)
    QFileDialog* getDialog() const;

    /// A test handler waits for the event loop to answer the request (it has not been called yet)
    bool isHandlerWaiting() const { return m_isPending && isHandlerSet(); }

    /// The answer has not been delivered yet
    bool isPending() const { return m_isPending; }

    /// Answers the request as the user would (tests): the dialog closes without its own answer and \p files are
    /// delivered now (empty = canceled; one file for the modes other than OpenFiles). Nothing after the answer.
    void answer(const QStringList& files);

    /// The chosen files of the dialog in the form of the static functions of QFileDialog: local paths (OpenFiles:
    /// QUrl::PreferLocalFile of every URL; the other modes: the first URL)
    static QStringList getSelectedFiles(const QFileDialog* dialog, Mode mode);

    /// The name of the file selected in the dialog for \p path (QFileDialogArgs: the name of a path, which is no
    /// existing folder; empty otherwise)
    static QString getInitialSelection(const QString& path);

Q_SIGNALS:
    /// The answer of OpenFiles (empty = canceled)
    void filesChosen(const QStringList& files);

    /// The answer of OpenFile, SaveFile, Directory (empty = canceled)
    void fileChosen(const QString& file);

private:
    explicit PDFAppFileDialogRequest(QObject* requester, QWidget* parent, const Settings& settings);

    void deliver(const QStringList& files);
    void onDialogAnswered(int code);
    void onHandlerAnswer();

    bool isHandlerSet() const { return m_filesHandler || m_fileHandler || m_panelFileHandler; }
    bool canAnswerByHandler() const { return m_isPending && !m_dialog && !m_dialogAnswer && !isHandlerSet(); }
    void queueHandlerAnswer();

    static std::vector<PDFAppFileDialogRequest*>& getRegistry();
    void unregister();

    QPointer<QWidget> m_parent;
    Settings m_settings;
    QPointer<QFileDialog> m_dialog;
    QPointer<QObject> m_dialogAnswer;
    FilesHandler m_filesHandler;
    FileHandler m_fileHandler;
    PanelFileHandler m_panelFileHandler;
    bool m_isPending = true;
};

/// Native dialogs of the system for files and folders (newapplication.md 3: the
/// non-native dialogs of Qt draw from the palette). Tests replace them by handlers.
///
/// A0 (no nested event loop): the request* functions show the dialogs without an event loop of their own
/// (PDFAppFileDialogRequest). They return at once; the answer comes to the member slot of the receiver (the requester,
/// which owns the request), never inside the call. A test handler is never called inside the call either: it answers
/// from the event loop as the user of the dialog would (a test settles before it checks the answer).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFileDialogs
{
public:
    PDFAppFileDialogs() = delete;

    using OpenFilesHandler = PDFAppFileDialogRequest::FilesHandler;
    using FolderHandler = PDFAppFileDialogRequest::FileHandler;
    using SaveFileHandler = PDFAppFileDialogRequest::FileHandler;

    /// Browse files… of the start page of a tool: PDF files of the folder (multi-select; the files to the slot,
    /// empty = canceled)
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenFileNames(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QStringList&))
    {
        return connectRequest(begin(receiver, parent, getOpenFileNamesSettings(folder), Handler::OpenFiles), receiver, slot);
    }

    /// Open… of the window (Ctrl+O, Browse files… of Home and Open): PDF files and the assembly files of Manage Pages
    /// (getOpenDocumentFilter, multi-select); the handler of the open files answers it in the tests
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenDocumentFileNames(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QStringList&))
    {
        return connectRequest(begin(receiver, parent, getOpenDocumentFileNamesSettings(folder), Handler::OpenFiles), receiver, slot);
    }

    /// Add Files… of a tool with more inputs (Merge): PDF files and images (multi-select); the handler of the open
    /// files answers it in the tests
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenInputFileNames(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QStringList&))
    {
        return connectRequest(begin(receiver, parent, getOpenInputFileNamesSettings(folder), Handler::OpenFiles), receiver, slot);
    }

    /// One image of the folder (the tool Image and Replace… of Edit Content): the files of getImageFilter; the file
    /// to the slot, empty = canceled. The handler of the open files answers it in the tests (its first file).
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenImageFileName(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getOpenImageFileNamesSettings(folder, false), Handler::OpenFiles), receiver, slot);
    }

    /// Images of the folder (multi-select; the files to the slot, empty = canceled); the handler of the open files
    /// answers it in the tests
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenImageFileNames(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QStringList&))
    {
        return connectRequest(begin(receiver, parent, getOpenImageFileNamesSettings(folder, true), Handler::OpenFiles), receiver, slot);
    }

    /// Add… of Pinned Locations: a folder (the folder to the slot, empty = canceled)
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestExistingDirectory(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getExistingDirectorySettings(folder, QString()), Handler::Folder), receiver, slot);
    }

    /// A folder chosen by the dialog of the title \p title (Browse… of a folder of Settings)
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestExistingDirectory(QWidget* parent, const QString& folder, const QString& title, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getExistingDirectorySettings(folder, title), Handler::Folder), receiver, slot);
    }

    /// Save as… of a document: a PDF file, \p filePath is the proposed file (folder and name); the file to the slot,
    /// empty = canceled
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestSaveFileName(QWidget* parent, const QString& filePath, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getSaveFileNameSettings(filePath), Handler::SaveFile), receiver, slot);
    }

    /// Export settings… of Settings › Storage: a backup file, \p filePath is the proposed file; the dialog adds the
    /// suffix of a backup
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestSaveBackupFileName(QWidget* parent, const QString& filePath, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getSaveBackupFileNameSettings(filePath), Handler::SaveBackup), receiver, slot);
    }

    /// Import settings… of Settings › Storage: a backup file of the folder
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestOpenBackupFileName(QWidget* parent, const QString& folder, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getOpenBackupFileNameSettings(folder), Handler::OpenBackup), receiver, slot);
    }

    /// The file of a print to a printer, whose port asks for a file (Print to File, Microsoft Print to PDF):
    /// \p filePath is the proposed file, \p suffix the suffix of the file of the driver
    template<typename Receiver>
    static PDFAppFileDialogRequest* requestPrintFileName(QWidget* parent, const QString& filePath, const QString& suffix, Receiver* receiver, void (Receiver::*slot)(const QString&))
    {
        return connectRequest(begin(receiver, parent, getPrintFileNameSettings(filePath, suffix), Handler::PrintFile), receiver, slot);
    }

    /// The dialogs of the request* functions
    static PDFAppFileDialogRequest::Settings getOpenFileNamesSettings(const QString& folder);
    static PDFAppFileDialogRequest::Settings getOpenDocumentFileNamesSettings(const QString& folder);
    static PDFAppFileDialogRequest::Settings getOpenInputFileNamesSettings(const QString& folder);

    /// \p isMultiple - more files (the mode OpenFiles, requestOpenImageFileNames), else one file (OpenFile)
    static PDFAppFileDialogRequest::Settings getOpenImageFileNamesSettings(const QString& folder, bool isMultiple);
    static PDFAppFileDialogRequest::Settings getExistingDirectorySettings(const QString& folder, const QString& title);
    static PDFAppFileDialogRequest::Settings getSaveFileNameSettings(const QString& filePath);
    static PDFAppFileDialogRequest::Settings getSaveBackupFileNameSettings(const QString& filePath);
    static PDFAppFileDialogRequest::Settings getOpenBackupFileNameSettings(const QString& folder);
    static PDFAppFileDialogRequest::Settings getPrintFileNameSettings(const QString& filePath, const QString& suffix);

    /// The filter of requestOpenDocumentFileNames: PDF files and assemblies together first, then each
    /// of them alone, then all files
    static QString getOpenDocumentFilter();

    /// The filter of the dialogs of images: one entry with a pattern "*.<format>" for every format QImageReader
    /// reads (QImageReader::supportedImageFormats, in its order), then all files
    static QString getImageFilter();

    /// Handlers replacing the dialogs (tests); an empty handler restores the dialog. The handler set at the request
    /// answers it, from the event loop (never inside the request call).
    static void setOpenFilesHandler(OpenFilesHandler handler);
    static void setFolderHandler(FolderHandler handler);
    static void setSaveFileHandler(SaveFileHandler handler);
    static void setSaveBackupHandler(SaveFileHandler handler);
    static void setOpenBackupHandler(FolderHandler handler);
    static void setPrintFileHandler(SaveFileHandler handler);

private:
    /// The handler of the tests, which replaces a dialog
    enum class Handler
    {
        OpenFiles,
        Folder,
        SaveFile,
        SaveBackup,
        OpenBackup,
        PrintFile
    };

    /// Creates the request: the handler of the tests (called from the event loop), or the dialog
    static PDFAppFileDialogRequest* begin(QObject* requester, QWidget* parent, const PDFAppFileDialogRequest::Settings& settings, Handler handler);

    template<typename Receiver, typename Slot>
    static PDFAppFileDialogRequest* connectRequest(PDFAppFileDialogRequest* request, Receiver* receiver, Slot slot)
    {
        if (request)
        {
            request->connectTo(receiver, slot);
        }
        return request;
    }

    static OpenFilesHandler& getOpenFilesHandler();
    static FolderHandler& getFolderHandler();
    static SaveFileHandler& getSaveFileHandler();
    static SaveFileHandler& getSaveBackupHandler();
    static FolderHandler& getOpenBackupHandler();
    static SaveFileHandler& getPrintFileHandler();
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
    QString toolId;         ///< The tool, which opens on the document (the start page of a tool), empty = none
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
