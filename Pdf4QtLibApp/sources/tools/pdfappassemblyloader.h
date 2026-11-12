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

#ifndef PDFAPPASSEMBLYLOADER_H
#define PDFAPPASSEMBLYLOADER_H

#include "pdfappglobal.h"

#include "pdfdocumentassemblyfile.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QFutureWatcherBase;

namespace pdf
{

class PDFAppAssemblyLoadControl;
class PDFAppToolInputList;
class PDFOperationControl;

/// State of a source of an assembly file at its opening (387:16980)
enum class PDFAppAssemblySourceState
{
    Found,              ///< The file is there and unchanged (size, time, SHA-256)
    Changed,            ///< The file has changed ("Changed on 2 Oct 2026 – now 5 pages instead of 6"): Use Changed File, pages over the new count go to the bin
    Missing,            ///< The file is not there ("Not found in C:\Tender"): Locate…, or its pages go to the bin
    OnAnotherComputer   ///< A network or device path (RB8): never touched automatically - Locate… or Open From There
};

/// A source of an assembly file at its opening
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAssemblySourceStatus
{
public:
    QString sourceId;                                       ///< PDFDocumentAssembly::Source::id
    PDFAppAssemblySourceState state = PDFAppAssemblySourceState::Found;
    QString filePath;                                       ///< The path of the source (resolved against the folder of the assembly); OnAnotherComputer: the path, which was not touched; Missing: the path of the assembly ("Not found in C:\Tender")
    PDFInteger pageCount = 0;                               ///< Pages of the file now (Changed; -1 = not known - the file needs its password), else of the assembly
    qint64 size = -1;                                       ///< Found, Changed: the size of the file now (bytes)
    QDateTime modified;                                     ///< Found, Changed: the time of the last change of the file now (UTC; "Changed on 2 Oct 2026")
    QByteArray sha256;                                      ///< Found, Changed: SHA-256 of the file now (empty = not computed)
    bool isAccepted = false;                                ///< Changed: the user has chosen Use Changed File (the dialog sets it; createContent uses the file)

    /// The source can be used by createContent: Found, or Changed and accepted
    bool isUsable() const { return state == PDFAppAssemblySourceState::Found || (state == PDFAppAssemblySourceState::Changed && isAccepted); }
};

/// The result of the opening of an assembly file
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAssemblyLoadResult
{
public:
    QString filePath;                                       ///< The assembly file
    std::optional<PDFDocumentAssemblyFile::Content> content;    ///< The content, std::nullopt = refused or canceled
    std::vector<PDFDocumentAssemblyFile::Problem> problems; ///< Problems of the file (PDFDocumentAssemblyFile::read)
    std::vector<PDFAppAssemblySourceStatus> sources;        ///< The state of every source (the dialog 387:16980 when one is not Found)
    QString errorMessage;                                   ///< Translated reason of a refusal
    bool isCanceled = false;                                ///< cancel or the deletion of the loader
};

/// Opening of an assembly file of Manage Pages (plan of step 3b of phase 2, 4.3, 4.6, 9.3, RB8,
/// B24; package M4). The skeleton declares it, package M4 defines it; the GUI of the tool (M3)
/// only uses it.
///
/// Contract:
///  - load reads the file and hashes the local sources in one job (QtConcurrent::run, owner
///    2026-11-09); the job holds only copies of the paths and the limits, never a pointer to the
///    loader or to a GUI. A network or device path of a source is never touched (RB8). The job is
///    cancelable (PDFOperationControl after each source and each megabyte of a hash): the close
///    of the application does not wait for gigabytes.
///  - \p loaded comes from the event loop through a QFutureWatcher with the loader as its context:
///    a deleted loader delivers nothing (the job ends and its result is dropped). It is emitted
///    last; a slot may delete the loader.
///  - The result is a new value (B24): the GUI exchanges its assembly only after a successful
///    load; the passwords of the sources are asked later by PDFAppToolInputLoader.
///  - A .pagemaster file is not an assembly (A6).
///
/// The sources of the file (package M4, plan 4.3, 6, RB8): an image is in the file (Found, never
/// read from the disk). A PDF source is looked for at its absolute path, then at its relative
/// path resolved against the folder of the assembly file; a candidate on another computer or a
/// device (PDFAppToolPathRules::isDeviceOrNetworkPath, also a mapped network drive) is never
/// touched - not even whether it exists. The first local file, which exists, is checked: a
/// different size is Changed, else SHA-256 decides (the time, when the file has no hash); a
/// Changed file is read by PDFAppToolInputLoader::read for its page count. No local file: a
/// network candidate makes the source OnAnotherComputer, else it is Missing. The dialog 387:16980
/// (the GUI, M3) shows the sources, which are not Found: Locate… and Open From There check the
/// file the user has chosen (locate - an explicit choice, so also a file of another computer),
/// Use Changed File sets PDFAppAssemblySourceStatus::isAccepted, Open Without Them / Open Assembly
/// call createContent.
///
/// The file of a new own tab (PDFAppMainWindow::openAssembly, a double click, the command line):
/// the window sets it on the list of the inputs of the tab before the tab shows its route
/// (setRequestedFile), the GUI of the tool takes it at its start (takeRequestedFile) and loads it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAssemblyLoader : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppAssemblyLoader(QObject* parent);
    virtual ~PDFAppAssemblyLoader() override;

    /// Size of a block of a hash, after which the job checks the cancellation (RB8)
    static constexpr qint64 HASH_BLOCK_SIZE = qint64(1024) * 1024;

    /// The file is an assembly file by its suffix (PDFDocumentAssemblyFile::FILE_SUFFIX, any case)
    static bool isAssemblyFile(const QString& filePath);

    /// Starts the loading of the file. \returns false (no signal), when a load runs already
    bool load(const QString& filePath);

    /// Starts the check of a file chosen for a source (Locate…, Open From There): \p loaded
    /// comes with \p result, whose status of \p sourceId is the state of the file \p filePath
    /// (Found or Changed; Missing, when it is no file). \returns false (no signal), when a load
    /// runs already or \p result has no content.
    bool locate(const PDFAppAssemblyLoadResult& result, const QString& sourceId, const QString& filePath);

    /// Cancels a running load: \p loaded comes with isCanceled
    void cancel();

    /// A load runs
    bool isLoading() const;

    /// The assembly file of the running load or check of a located file (empty = none): a tab,
    /// which is loading the file, has it open already (PDFAppManageToolGUI::findAssemblyTab)
    const QString& getLoadingFilePath() const { return m_loadingFilePath; }

    /// The job of load (any thread; the tests call it directly): reads the file under the limits
    /// of PDFDocumentAssemblyFile and checks its sources (see the class)
    static PDFAppAssemblyLoadResult read(const QString& filePath, const PDFOperationControl* control);

    /// The job of locate (any thread): \p result with the state of the file \p filePath for the
    /// source \p sourceId (an unknown source leaves \p result as it is)
    static PDFAppAssemblyLoadResult readLocated(PDFAppAssemblyLoadResult result, const QString& sourceId, const QString& filePath, const PDFOperationControl* control);

    /// The content of the assembly for the GUI (Open Assembly, Open Without Them, B24: a new
    /// value): the usable sources (PDFAppAssemblySourceStatus::isUsable) take the file of their
    /// status - its path, size, time, hash and the page count of a changed file
    /// (PDFDocumentAssembly::updateSource: the pages over the new count go to the bin); the other
    /// PDF sources are removed (PDFDocumentAssembly::removeSource: parked, their pages go to the
    /// bin - 387:16980 "Pages of files that stay missing are left out and listed under Removed
    /// pages"). The checkpoints lose the pages over the page count of a changed file.
    /// std::nullopt and \p errorMessage, when the result has no content or an operation refuses.
    static std::optional<PDFDocumentAssemblyFile::Content> createContent(const PDFAppAssemblyLoadResult& result, QString* errorMessage);

    /// The assembly file, which a new own tab opens (see the class); a dynamic property of
    /// \p inputList, so it dies with the list
    static void setRequestedFile(PDFAppToolInputList* inputList, const QString& filePath);

    /// The requested file of \p inputList, removed from it (empty = none)
    static QString takeRequestedFile(PDFAppToolInputList* inputList);

Q_SIGNALS:
    /// The load has ended (also refused or canceled)
    void loaded(const pdf::PDFAppAssemblyLoadResult& result);

private:
    bool start(const PDFAppAssemblyLoadResult& result, const QString& sourceId, const QString& filePath);
    void onFinished();

    QPointer<QFutureWatcherBase> m_watcher;                 ///< The watcher of the job (a child), nullptr = no job
    std::shared_ptr<PDFAppAssemblyLoadControl> m_control;   ///< The cancellation of the job, shared with it
    QString m_loadingFilePath;                              ///< The assembly file of the job (empty = no job)
};

}   // namespace pdf

#endif // PDFAPPASSEMBLYLOADER_H
