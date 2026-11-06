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

#ifndef PDFDOCUMENTTOOLRUNNER_H
#define PDFDOCUMENTTOOLRUNNER_H

#include "pdfappdocument.h"

#include "pdfdocumenttool.h"

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace pdf
{

class PDFDocumentToolRunnerJob;
class PDFDocumentToolRunnerLink;

/// What a run gets besides the snapshot of the document. A value filled in the GUI
/// thread: the color management is a shared immutable copy, never a pointer to an
/// object of a view (F12 of the plan of the first batch).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRunEnvironment
{
public:
    std::vector<PDFDocumentToolInputDocument> additionalDocuments;  ///< The other inputs of a tool of more documents (Merge)
    std::vector<PDFInteger> selectedPages;                          ///< Pages selected in the thumbnails of the document
    PDFInteger currentPage = -1;                                    ///< Current page of the view
    PDFCMSPointer cms;                                              ///< Color management of the application, null = the default settings
    QString authorName;                                             ///< Author of the new content
    std::shared_ptr<const PDFCertificateStore> certificateStore;    ///< Trusted certificates, may be null
    QString temporaryFolder;                                        ///< Folder of the temporary files of the run
};

/// One tool on one document of a tab (plan of the first batch, chapter 2.1 and 2.4). A
/// run works in the pool on a snapshot of the document, nobody waits for it: the
/// deletion of the runner only sets the cancellation, a late result is dropped.
///
/// The computation of a tool (\p run):
///  - Holds a blocking PDFAppDocumentOperation of the kind Tool from its start
///    (changes are refused, the tab shows the progress, the close of the tab asks).
///  - A tool, whose preview is the result (PDFDocumentTool::isPreviewFinal): a
///    finished preview of the same parameters and of the same version is the result,
///    a running one continues as the run; nothing is computed twice.
///  - A successful result keeps the operation while the user decides (the result
///    dialog, F2): nothing changes the document under the facts the user reads.
///    The operation ends just before the commit (\p commitResult), by \p endDecision
///    or by the deletion of the runner - never by the return of the worker (F12).
///
/// The preview (\p requestPreview): debounced by PREVIEW_DELAY, an older computation
/// is canceled, a cache {version, parameters} -> result. No operation: the document
/// stays usable. A new version of the document drops the preview and computes it
/// again. Only one cached result may hold documents or files (the memory).
///
/// Every slot of a signal may delete the runner.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolRunner : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Rest after a change of the parameters before a preview starts (ms)
    static constexpr int PREVIEW_DELAY = 300;

    /// Runs of one confirmation, when the commit is refused for an old version
    static constexpr int MAXIMUM_ATTEMPTS = 3;

    /// Results kept by the cache of the previews
    static constexpr int MAXIMUM_CACHED_PREVIEWS = 8;

    /// Interval of the reading of the progress of a computation (ms): the computation steps
    /// a PDFProgressCounter, the runner reads it in the GUI thread (the first read at once)
    static constexpr int PROGRESS_INTERVAL = 100;

    /// State of the run
    enum class State
    {
        Idle,       ///< No run, no operation
        Running,    ///< The tool computes its result
        Waiting     ///< The result waits for the decision of the user, the operation is held
    };

    /// Result of \p commitResult
    enum class CommitStatus
    {
        Committed,  ///< The document has the result, one undo step
        Rerun,      ///< The document has another version: the tool runs again on it, \p runFinished comes again (the user decides over the new facts)
        Refused     ///< Nothing has changed, \p errorMessage says why
    };

    /// \param document Document of the tab, null for a tool without a document (the inputs come from the environment)
    /// \param tool The tool, never null
    /// \param parent Parent object
    explicit PDFDocumentToolRunner(PDFAppDocument* document, std::shared_ptr<const PDFDocumentTool> tool, QObject* parent);

    /// Cancels the computations and ends the operation; nothing waits for the workers
    virtual ~PDFDocumentToolRunner() override;

    /// Context of a run from a snapshot of a document (the shared part of the GUI and
    /// of MCP, F17): a valid snapshot is the first input document
    static PDFDocumentToolContext createContext(const PDFAppDocumentSnapshot& snapshot, const PDFAppToolRunEnvironment& environment = PDFAppToolRunEnvironment());

    /// Text of the step of a progress in the language of the user interface: the text of
    /// the phase, with its count when the phase shows it ("Recompressing images · 152 of 412")
    static QString getStepText(const PDFProgressCounter::Snapshot& snapshot);

    /// The changed document of the first input of a result, nullptr = none
    static const PDFDocumentToolOutputDocument* findChangedDocument(const PDFDocumentToolResult& result);

    PDFAppDocument* getDocument() const { return m_document.data(); }
    const std::shared_ptr<const PDFDocumentTool>& getTool() const { return m_tool; }

    /// Inputs of the following computations (selected pages, color management, ...)
    void setEnvironment(const PDFAppToolRunEnvironment& environment);

    /// Starts the computation of the tool on the current version. The parameters are
    /// normalized and validated by the tool. \returns false, when it cannot start
    /// (a run or a decision is in progress, the document is not shown, has gone or
    /// another blocking operation holds it - its commit would be refused); then no
    /// signal comes. \p runFinished may come before the call returns (a finished
    /// preview is the result).
    bool run(const QVariantMap& parameters);

    /// Ends a run now: its worker gets the cancellation, its late result is dropped, the
    /// document is free and \p runFinished comes with Stopped. Nothing is kept: the
    /// next \p run computes again.
    void stop();

    State getState() const { return m_state; }

    /// Progress of the run or of the preview, 0 - 100, and the text of the step (the last
    /// value read from the computation; runProgress and previewProgress tell its changes)
    int getProgress() const { return m_progress; }
    const QString& getStepText() const { return m_stepText; }

    /// Number of the started computations of the run of the last confirmation
    /// (1 + the runs after a refused commit)
    int getAttempt() const { return m_attempt; }

    /// Version of the document, from which the last result comes
    quint64 getResultVersion() const { return m_resultSnapshot.version; }

    /// Snapshot, from which the last result comes (the result writer)
    const PDFAppDocumentSnapshot& getResultSnapshot() const { return m_resultSnapshot; }

    /// Why \p commitResult would refuse the result now, without changing anything (not
    /// the state Waiting, no document, a locked document, a display version, another
    /// blocking operation). Empty: the commit passes, another version only runs again.
    /// What writes the result over the file asks before it writes (F6).
    QString getCommitRefusal() const;

    /// Commits the changed document of the result (a result of the state Waiting)
    /// as one undo step: the operation ends just before the commit. For another
    /// version of the document the tool runs again on it, at most MAXIMUM_ATTEMPTS
    /// computations - never silently: \p runFinished asks for a new decision. A refusal
    /// by \p getCommitRefusal keeps the state and the operation.
    CommitStatus commitResult(const PDFDocumentToolResult& result, const QString& description, QString* errorMessage);

    /// The user has decided (a copy has been written, the result has been discarded):
    /// the operation ends, the state is Idle
    void endDecision();

    /// Asks for a preview of the parameters: the result of the cache at once, else a
    /// computation after PREVIEW_DELAY (an older one is canceled). The latest
    /// parameters stay requested: a new version of the document computes them again.
    /// A request during a run or a decision is served after its end. Nothing else
    /// computes after a run: Stop means no computation until the next request.
    void requestPreview(const QVariantMap& parameters);

    /// No preview is wanted any more (the cache stays until a new version)
    void cancelPreview();

    /// A preview computes in a worker or waits for its delay
    bool isPreviewPending() const;

    /// The latest finished preview of the requested parameters and of the current
    /// version, nullptr = none. Valid until the next finished computation or change
    /// of the document (never kept over a return to the event loop).
    const PDFDocumentToolResult* getPreview() const;

Q_SIGNALS:
    /// A preview has started in a worker
    void previewStarted();

    /// Progress of the preview in a worker
    void previewProgress(int percentage, const QString& stepText);

    /// The preview of the requested parameters is ready (also Failed / NothingToDo)
    void previewFinished(const pdf::PDFDocumentToolResult& result);

    /// The shown preview is not valid any more (a new version of the document)
    void previewDropped();

    /// A run has started (also a preview, which continues as the run)
    void runStarted();

    /// Progress of the run
    void runProgress(int percentage, const QString& stepText);

    /// The run has ended. Success: the state is Waiting, the user decides. Other
    /// statuses (Stopped, Failed, NothingToDo): the state is Idle.
    void runFinished(const pdf::PDFDocumentToolResult& result);

    /// The document has left the state Ready or it is being destroyed: everything is
    /// canceled, the state is Idle
    void documentLost();

private:
    friend class PDFDocumentToolRunnerLink;

    class CachedPreview
    {
    public:
        quint64 version = 0;
        QVariantMap parameters;
        PDFDocumentToolResult result;
    };

    void onProgressTimeout();
    void onJobFinished(const std::shared_ptr<pdf::PDFDocumentToolRunnerJob>& job);
    void onPreviewTimeout();
    void onDocumentChanged();
    void onDocumentStateChanged();
    void onDocumentAboutToBeDestroyed();
    void onOperationCancelRequested();

    /// Starts a computation in the pool
    std::shared_ptr<PDFDocumentToolRunnerJob> startJob(const PDFAppDocumentSnapshot& snapshot, const QVariantMap& parameters, bool isPreview);

    /// Starts the computation of a confirmation (also after a refused commit)
    /// \returns false, when the runner has been deleted by a slot
    bool startRun();

    /// Begins the operation of the run. \returns false, when the runner has been
    /// deleted by a slot of the document
    bool beginOperation();

    /// Ends the operation. \returns false, when the runner has been deleted by a slot
    bool endOperation();

    void cancelPreviewJob();
    void finishRun(const PDFDocumentToolResult& result);
    void addToCache(quint64 version, const QVariantMap& parameters, const PDFDocumentToolResult& result);
    const CachedPreview* findCached(quint64 version, const QVariantMap& parameters) const;
    void setProgress(int percentage, const QString& stepText);

    /// Reads the progress of the computation of \p job and tells its change (runProgress,
    /// previewProgress). \returns false, when the runner has been deleted by a slot
    bool readProgress(const std::shared_ptr<PDFDocumentToolRunnerJob>& job);
    void loseDocument();
    PDFAppDocumentSnapshot createSnapshot() const;

    /// Serves a preview requested during a run or a decision. \returns false, when the
    /// runner has been deleted by a slot
    bool resumeDeferredPreview();

    QPointer<PDFAppDocument> m_document;
    bool m_hasDocument = false;     ///< Created with a document: a lost document is no tool without a document
    std::shared_ptr<const PDFDocumentTool> m_tool;
    std::shared_ptr<PDFDocumentToolRunnerLink> m_link;
    PDFAppToolRunEnvironment m_environment;
    QPointer<PDFAppDocumentOperation> m_operation;
    State m_state = State::Idle;
    int m_progress = 0;
    QString m_stepText;
    int m_attempt = 0;
    QVariantMap m_runParameters;
    std::shared_ptr<PDFDocumentToolRunnerJob> m_runJob;
    PDFAppDocumentSnapshot m_resultSnapshot;

    QTimer m_previewTimer;
    QTimer m_progressTimer;
    bool m_isPreviewRequested = false;
    bool m_isPreviewDeferred = false;
    QVariantMap m_previewParameters;
    std::shared_ptr<PDFDocumentToolRunnerJob> m_previewJob;
    std::vector<CachedPreview> m_previewCache;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFDocumentToolResult)

#endif // PDFDOCUMENTTOOLRUNNER_H
