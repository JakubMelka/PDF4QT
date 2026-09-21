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

#ifndef PDFOCRDOCUMENTDIALOG_H
#define PDFOCRDOCUMENTDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"
#include "pdfocrmodel.h"
#include "pdfocrsession.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfmeshqualitysettings.h"

#include <QTimer>
#include <QImage>
#include <QDialog>
#include <QTransform>
#include <QElapsedTimer>
#include <QFuture>
#include <QMutex>

#include <set>
#include <map>
#include <memory>
#include <functional>

namespace Ui
{
class PDFOCRDocumentDialog;
}

class QSplitter;
class QListWidgetItem;
class QTreeWidgetItem;

namespace pdf
{
class PDFCMS;
class PDFProgress;
class PDFDrawWidgetProxy;
class PDFOptionalContentActivity;
}

namespace pdfviewer
{
class PDFOCRPageView;

/// Dialog "Recognize Text (OCR)" of the editor (chapter 4 of the OCR
/// specification). The workflow has four phases: set up, recognize, review
/// and correct, apply to PDF / export. Recognition itself never modifies
/// the document; all expensive operations run outside of the GUI thread.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    struct Context
    {
        const pdf::PDFDocument* document = nullptr;
        pdf::PDFDrawWidgetProxy* proxy = nullptr;
        const pdf::PDFCMS* cms = nullptr;
        pdf::PDFProgress* progress = nullptr;

        /// Pages currently visible in the editor (0-based), the first one is the current page
        std::vector<pdf::PDFInteger> visiblePages;

        /// Explicit page selection of the editor (0-based), may be empty
        std::vector<pdf::PDFInteger> selectedPages;

        QString fileName;
        bool canModify = true;
        bool canCopyContent = true;
        bool hasSignatures = false;
        bool isEncrypted = false;
    };

    explicit PDFOCRDocumentDialog(const Context& context, QWidget* parent);
    virtual ~PDFOCRDocumentDialog() override;

    /// Returns true, if the dialog produced a modified document, which
    /// should replace the current document of the editor (single undo step).
    bool hasModifiedDocument() const { return !m_modifiedDocument.isNull(); }
    pdf::PDFDocumentPointer takeModifiedDocument() { return std::move(m_modifiedDocument); }

    virtual void done(int result) override;

signals:
    void pageDataReady(int generation, qint64 pageIndex, QImage thumbnail, pdf::PDFOCRPageAnalysis analysis);
    void ownLayerLoaded(int generation, pdf::PDFOCRPageResult result);
    void previewReady(int generation, qint64 pageIndex, QImage original, QTransform pageToOriginal, QImage working, QTransform pageToWorking, QString message);
    void applyFinished(int generation);

private:
    enum class OutputMode
    {
        ModifyCurrent,
        CreateCopy,
        ExportOnly
    };

    enum class ViewMode
    {
        Original,
        Working,
        SideBySide
    };

    enum class RunMode
    {
        Pages,          ///< Standard recognition of the pages
        Line,           ///< Repeated recognition of a line
        Word            ///< Repeated recognition of a word
    };

    struct AsyncTask
    {
        int generation = 0;
        std::shared_ptr<pdf::PDFOCRCancelToken> token;
    };

    struct ApplyResult
    {
        pdf::PDFDocumentPointer document;
        pdf::PDFOCRTextLayerWriter::Report report;
        QString errorMessage;
        QString copyFileName;
        bool isRemoval = false;
        bool conformanceRemoved = false;
    };

    // Initialization
    void initializeUi();
    void initializePages();
    void loadSettings();
    void saveSettings() const;

    // Background tasks
    void startTask(AsyncTask& task, std::function<void(int, const pdf::PDFOperationControl*)> worker);
    void cancelTask(AsyncTask& task);
    void startPageDataTask();
    void schedulePreview();
    void startPreviewTask();
    void onPageDataReady(int generation, qint64 pageIndex, QImage thumbnail, pdf::PDFOCRPageAnalysis analysis);
    void onPreviewReady(int generation, qint64 pageIndex, QImage original, QTransform pageToOriginal, QImage working, QTransform pageToWorking, QString message);

    // Configuration
    pdf::PDFOCRConfiguration getConfigurationFromUi() const;
    void setConfigurationToUi(const pdf::PDFOCRConfiguration& configuration);
    void onConfigurationChanged();
    void updateLanguageList(const QStringList& selectedLanguages);
    QStringList getSelectedLanguages() const;
    void updateLayoutCombos();
    void updateMemoryEstimate();
    void updatePolicySummary();
    void updateProfiles();
    void onSaveProfile();
    void onLoadProfile();
    void onDeleteProfile();
    void onManageLanguages();
    void onPageOverrideChanged();
    void updatePageOverrideUi();

    // Pages
    std::vector<pdf::PDFInteger> getCheckedPages() const;
    void setCheckedPages(const std::vector<pdf::PDFInteger>& pages);
    void onApplyPageSelection();
    void onApplyHelperSelection();
    void updatePageItem(pdf::PDFInteger pageIndex);
    void updateSelectionInfo();
    void onCurrentPageChanged();
    const pdf::PDFOCRPageAnalysis* getAnalysis(pdf::PDFInteger pageIndex);
    static QString getPageStateName(pdf::PDFOCRPageState state);

    // Recognition
    void onRecognizeClicked();
    void onStopClicked();
    bool startRecognition(const std::vector<pdf::PDFInteger>& pages, RunMode runMode);
    void onJobPageStateChanged(int generation, qint64 pageIndex, int state, QString phase);
    void onJobPageProgress(int generation, qint64 pageIndex, int percent);
    void onJobPageFinished(int generation, pdf::PDFOCRPageResult result);
    void onJobProgress(int generation, int finished, int total);
    void onJobFinished(int generation, pdf::PDFOCRJobSummary summary);
    void processCandidates();
    void onRerecognizeClicked();

    // Review
    void updateResultsTree();
    void updateInspector();
    void updateStatistics();
    void selectWord(int wordId, bool fromTree);
    void onTreeSelectionChanged();
    void onWordTextEdited();
    void onLineTextEdited();
    void onReviewNavigation(bool forward, bool confirm);
    void onFindNext();
    void onReplaceAll();
    void onRectangleDrawn(int mode, QRectF pageRectangle, pdf::PDFOCRQuad pageQuad);
    void onRegionProperties();
    void onRemoveRegion();
    void onMoveItem(bool up);
    void onInsertWord();
    void onConfirmAll();
    void onSessionPageChanged(qint64 pageIndex);
    void showTreeContextMenu(const QPoint& point);
    std::vector<pdf::PDFInteger> getFindScopePages() const;
    bool isPageEditable(pdf::PDFInteger pageIndex) const;

    /// Returns true, if the page is processed (or waits for the processing) by the running job,
    /// so its result will replace the content of the page in the session
    bool isPageInRunningJob(pdf::PDFInteger pageIndex) const;

    /// Own OCR layer of the document is offered for further corrections without a new recognition (PDF-10)
    void onOwnLayerLoaded(int generation, pdf::PDFOCRPageResult result);

    // Output
    void onApplyClicked();
    void onRemoveLayerClicked();
    void onApplyFinished(int generation);
    void onExportClicked();
    void onSaveProject();
    void onOpenProject();
    bool confirmReadableTextOutput(const QString& title);
    bool saveProject();

    // Common
    void updateUi();
    void updateViews();
    void setViewMode(ViewMode mode);
    void updateWorkflowLabel();
    void showReviewPanel(bool show);
    bool isBusy() const;
    pdf::PDFOCRDocumentIdentity createIdentity() const;

    Ui::PDFOCRDocumentDialog* ui;
    Context m_context;
    pdf::PDFOCRSession* m_session;
    pdf::PDFOCRJobController* m_jobController;
    pdf::PDFOCRModelManager* m_modelManager;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFMeshQualitySettings m_meshQualitySettings;
    QSplitter* m_viewSplitter = nullptr;
    PDFOCRPageView* m_originalView = nullptr;
    PDFOCRPageView* m_workingView = nullptr;

    pdf::PDFInteger m_pageCount = 0;
    pdf::PDFInteger m_currentPage = -1;
    int m_selectedWordId = 0;
    int m_selectedLineId = 0;
    int m_selectedBlockId = 0;
    bool m_updatingUi = false;
    bool m_closeRequested = false;
    bool m_hasConformanceDeclaration = false;
    bool m_isTagged = false;
    QStringList m_conformanceDeclarations;

    std::map<pdf::PDFInteger, pdf::PDFOCRPageAnalysis> m_analysis;
    std::map<pdf::PDFInteger, QByteArray> m_fingerprints;
    std::set<pdf::PDFInteger> m_reviewOnlyPages;
    std::map<pdf::PDFInteger, pdf::PDFOCRPageResult> m_candidates;
    std::set<pdf::PDFInteger> m_candidatePages;

    /// States of the pages before the running job. A repeated recognition, which was
    /// stopped or failed, must not destroy the existing result of the page (JOB-05).
    std::map<pdf::PDFInteger, pdf::PDFOCRPageState> m_previousPageStates;
    int m_keptResultsCount = 0;

    AsyncTask m_pageDataTask;
    AsyncTask m_previewTask;
    AsyncTask m_applyTask;
    std::vector<QFuture<void>> m_futures;
    QTimer m_previewTimer;

    int m_jobGeneration = 0;
    RunMode m_runMode = RunMode::Pages;
    int m_rerecognizeLineId = 0;
    int m_rerecognizeWordId = 0;
    pdf::PDFInteger m_rerecognizePage = -1;
    QElapsedTimer m_jobTimer;
    int m_jobFinishedPages = 0;
    int m_jobTotalPages = 0;

    QMutex m_applyMutex;
    ApplyResult m_applyResult;
    bool m_applyInProgress = false;
    pdf::PDFDocumentPointer m_modifiedDocument;
    QString m_projectFileName;
    size_t m_findIndex = 0;
};

}   // namespace pdfviewer

#endif // PDFOCRDOCUMENTDIALOG_H
