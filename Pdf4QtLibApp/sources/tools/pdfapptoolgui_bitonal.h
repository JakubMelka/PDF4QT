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

#ifndef PDFAPPTOOLGUI_BITONAL_H
#define PDFAPPTOOLGUI_BITONAL_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfbitonaldocumentcreator.h"
#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool_bitonal.h"
#include "pdfprogress.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QAction;
class QSpinBox;
class QToolButton;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolImageComparison;
class PDFAppToolImageList;
class PDFAppToolPageGrid;
class PDFAppToolPanelHeader;
class PDFAppToolSectionHeader;
class PDFKeyValueList;
class PDFLabel;
class PDFMessageBox;
class PDFTileRow;

/// A panel of a menu of the toolbar of Black & White with its own content below the rows (the
/// Resolution of Source and the Threshold of Method, 306:5353): PDFAppToolPanelPopup, which takes
/// a widget into its content. It belongs to its button (the toolbar of the tool).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolBitonalMenuPanel : public PDFAppToolPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolPanelPopup;

public:
    explicit PDFAppToolBitonalMenuPanel(PDFAppDocumentView* view, int panelWidth, QWidget* parent);
    virtual ~PDFAppToolBitonalMenuPanel() override;

    /// Adds a widget to the content below the rows (its parent becomes the panel)
    void addContentWidget(QWidget* widget);
};

/// What a background job of Black & White computes (the workers of PDFAppToolGUIBitonal). Static
/// functions of plain values: a worker gets a shared immutable document, copies of the requests
/// and a cancel flag shared with the GUI; it creates its own rasterization environment, which
/// lives and dies in the worker (never an object of the view). R4-22: the preview is the function
/// of the core PDFBitonalDocumentCreator::createPreview.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolBitonalJobs
{
public:
    PDFAppToolBitonalJobs() = delete;

    /// A preview of one item; the pages are rendered with \p cmsSettings, the color management of
    /// the run (PDFAppDocumentView::createToolRenderContext, RS4APP) - the preview is the result
    static PDFBitonalDocumentCreator::Preview createPreview(PDFDocumentPointer document, PDFCMSSettings cmsSettings, PDFBitonalDocumentCreator::PreviewRequest request, std::shared_ptr<PDFAppCancelFlag> cancel);

    /// The encoded sizes of the items of \p requests (-1 = not encoded); the items, which the
    /// cancellation has skipped, have -2
    static std::vector<qint64> estimateSizes(PDFDocumentPointer document, PDFCMSSettings cmsSettings, std::vector<PDFBitonalDocumentCreator::PreviewRequest> requests, std::shared_ptr<PDFAppCancelFlag> cancel);

    /// The list of the images of a change (PDFDocumentImageList::Options::createForChange)
    static std::shared_ptr<const PDFDocumentImageList> collectImages(PDFDocumentPointer document, std::shared_ptr<PDFAppCancelFlag> cancel);

    /// The detection of the blank pages (the progress is polled by the GUI)
    static std::vector<PDFBitonalDocumentCreator::BlankPageResult> detectBlankPages(PDFDocumentPointer document, PDFCMSSettings cmsSettings, std::vector<PDFInteger> pageIndices,
                                                                                  std::shared_ptr<PDFAppCancelFlag> cancel, std::shared_ptr<PDFProgressCounter> progress);
};

/// The tool Convert to Black & White (optimize.bitonal, Figma 304:5102 ... 307:6564; plan of step 4
/// of phase 2, 4.6, K4.2 - K4.4, K4.13, R4-22; package P3): the grid of the pages with the labels of
/// their modes (PDFAppToolPageGrid) or the list of the images (PDFAppToolImageList) for Source
/// Images, PDFAppToolImageComparison (ORIGINAL / BITONAL, Sync zoom), Selected items, Summary, the
/// callout of the source, the menus Source / Method / Compression / Mode, the detection of blank
/// pages with Stop (n/N), the questions 307:6250 and 307:6388; the confirm button "Convert" (owner
/// 13. 11.).
///
/// The modes of the items are the parameter "items" of the model (Undo / Redo), the other values
/// its parameters. The workers (the list of the images, the preview after 150 ms, the estimated
/// sizes of the Summary, the detection) are single asynchronous jobs (QtConcurrent::run of
/// PDFAppToolBitonalJobs) with a cancel flag and a generation: a late result of an older job is
/// dropped; the GUI keeps only values of them.
///
/// Ownership: the parts belong to the panels and the canvas of the shell, the GUI keeps them in
/// QPointer; the watchers of the jobs are children of the GUI (a result of a job, which ends after
/// the GUI, goes nowhere); the questions are new + QPointer, deleted on close.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIBitonal : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    using ItemMode = PDFBitonalDocumentCreator::ItemMode;

    explicit PDFAppToolGUIBitonal(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIBitonal() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): the tool in
    /// PDFDocumentToolRegistry and its GUI in PDFDocumentToolGUIRegistry
    static void registerBitonalTools();

    /// Delay of the preview after a change (plan 4.6)
    static constexpr int PREVIEW_DELAY = 150;

    /// Items of one job of the estimate of the size
    static constexpr int ESTIMATE_BATCH_SIZE = 8;

    /// Interval of the polling of the progress of the detection (ms)
    static constexpr int DETECTION_POLL_INTERVAL = 100;

    // ------------------------------------------------------------- state (also the tests)

    /// The source of the panel is the pages
    bool isPageSource() const;

    /// Number of the items of the source (pages, or the convertible images once they are known)
    int getItemCount() const;

    /// The mode of an item (index of the item: a page index, or an index of the convertible images)
    ItemMode getItemMode(int itemIndex) const;

    /// The selected items in their order, and the current one (-1 = none)
    const std::vector<int>& getSelectedItems() const { return m_selectedItems; }
    int getCurrentItem() const { return m_currentItem; }

    /// Selects the items (the click of the grid or of the list does it); \p current is the item of
    /// the preview (-1 = the last one)
    void selectItems(std::vector<int> items, int current = -1);

    /// Sets the mode to the selected items, to all items with nothing selected (one undo step)
    void setModeToItems(ItemMode mode);

    /// The list of the images is being collected (Source Images)
    bool isCollectingImages() const;

    /// The blank page detection runs; its examined pages and all pages
    bool isDetecting() const { return m_isDetecting; }
    int getExaminedPageCount() const { return m_examinedPageCount; }
    int getPagesToExamineCount() const { return m_pagesToExamineCount; }

    /// Starts the detection (the button Blank pages) or stops it (Stop (n/N))
    void toggleBlankPageDetection();

    /// The question of the found blank pages or the message of the detection, nullptr = none
    PDFMessageBox* getQuestion() const;

    /// The preview job runs or waits for its delay
    bool isPreviewPending() const;

    /// The watcher of the preview job (a test watches its start)
    const QFutureWatcherBase* getPreviewWatcher() const { return &m_previewWatcher; }

    /// The estimated size of the result is known for every item (Summary)
    bool isEstimateComplete() const;

    /// Text of the line below the cards (304:5102 "Automatic threshold (Otsu): 141"), the value and
    /// the note; the comparison draws the value (getFooterValueText) and the muted note
    /// (getFooterNoteText) as two labels
    QString getFooterText() const;
    QString getFooterValueText() const;
    static QString getFooterNoteText();

    // ------------------------------------------------------------- parts (nullptr = none)

    QToolButton* getSourceButton() const;
    QToolButton* getMethodButton() const;
    QToolButton* getCompressionButton() const;
    QToolButton* getModeButton() const;
    QToolButton* getBlankPagesButton() const;
    PDFAppToolBitonalMenuPanel* getSourcePanel() const;
    PDFAppToolBitonalMenuPanel* getMethodPanel() const;
    PDFAppToolBitonalMenuPanel* getCompressionPanel() const;
    PDFAppToolBitonalMenuPanel* getModePanel() const;
    QSpinBox* getResolutionSpinBox() const;
    QSpinBox* getThresholdSpinBox() const;
    PDFAppToolPanelHeader* getLeftHeader() const;
    PDFAppToolPageGrid* getPageGrid() const;
    PDFAppToolImageList* getImageList() const;
    PDFAppToolImageComparison* getComparison() const;
    PDFLabel* getCanvasTitle() const;
    PDFAppToolSectionHeader* getSelectedHeader() const;
    PDFKeyValueList* getSelectedRows() const;
    PDFKeyValueList* getSummaryRows() const;
    PDFAppToolCallout* getCallout() const;

    // ------------------------------------------------------------- PDFDocumentToolGUI

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getStatusDetailText() const override;
    virtual bool isFileSizeInStatusText() const override { return false; }  ///< 304:5102, 306:5615
    virtual void setContentEnabled(bool isEnabled) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onDocumentChanged() override;
    virtual void retranslate() override;

    /// 307:6388: a result with items, which were not converted, asks "No, discard" / "Use partial
    /// document" (the question m_question, new + QPointer, deleted with the GUI); the answer goes
    /// to PDFDocumentToolShell::answerResult. Every item converted: the dialog opens at once.
    virtual bool confirmResult(const PDFDocumentToolResult& result) override;

    /// The question of the partial result: its text and its detail (307:6388) - "3 of 120 items
    /// could not be converted …", "Not converted: pages 17, 43, 44 (rendering errors)"; empty
    /// texts = every item converted
    static std::pair<QString, QString> getPartialResultTexts(const PDFDocumentToolResult& result);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The document shown (nullptr = none)
    PDFDocumentPointer getShownDocument() const;

    /// A value of the model
    QVariant getParameter(const char* name) const;

    /// The resolution of the pages (the parameter, the estimate of the document for 0)
    int getEffectiveDpi() const;

    /// The modes of the parameter "items" (only the entries of this source); a reference to the
    /// cache, valid until the next call (it is asked for every item of thousands of pages)
    const std::map<int, ItemMode>& getModesOfItems() const;

    /// Key of the image item (convertible image index)
    std::optional<PDFDocumentImageList::Key> getImageKey(int itemIndex) const;

    /// Index of the item of the list of the images of a convertible image, -1 = none
    int getImageListIndex(int itemIndex) const;

    /// The text of a mode for the toolbar, the rows, the tiles and the captions
    static QString getModeTitle(ItemMode mode);
    static QString getModeDescription(ItemMode mode);
    static QString getModeLabel(ItemMode mode);
    static QString getModeCaption(ItemMode mode);
    static QString getModeIconName(ItemMode mode);

    /// Number text of the items ("3, 5, 6", "4, 9-12, 57")
    static QString createNumbersText(std::vector<int> numbers);

    QToolButton* addMenuButton(PDFAppToolPanelBuilder& builder, const QString& objectName, const QString& iconName, QPointer<PDFAppToolBitonalMenuPanel>* panel, int panelWidth);
    void addEnumRows(PDFAppToolBitonalMenuPanel* panel, const char* parameter, const QStringList& iconNames, std::map<QString, QPointer<PDFTileRow>>* rows);
    QSpinBox* createSpinRow(PDFAppToolBitonalMenuPanel* panel, const QString& label, const QString& suffix, int minimum, int maximum, int step, const QString& help);

    // ------------------------------------------------------------- updates (no signal)

    void updateAll();
    void updateToolbar();
    void updateLeftPanel();
    void updateTiles();
    void updateCanvas();
    void updateRightPanel();
    void updateSummary();

    // ------------------------------------------------------------- jobs

    void startImageCollection();
    void cancelImageCollection();
    void schedulePreview();
    void startPreview();
    void cancelPreview();
    void startEstimate();
    void cancelEstimate();
    void cancelDetection();
    void cancelJobs();

    /// The preview request of the current item, std::nullopt = no item
    std::optional<PDFBitonalDocumentCreator::PreviewRequest> createPreviewRequest(int itemIndex, bool isEstimate) const;

    /// Key of the cache of the estimate of an item with the settings of now
    QString getEstimateKey(int itemIndex) const;

    /// Bytes of the original content, which the item replaces (the image streams, Summary)
    qint64 getOriginalItemBytes(int itemIndex) const;

    /// The color management of a run of the tool (PDFAppDocumentView::createToolRenderContext):
    /// the preview, the estimate and the detection render as the run does
    PDFCMSSettings getRunCmsSettings() const;

    // ------------------------------------------------------------- slots

    void onSourceRowClicked();
    void onMethodRowClicked();
    void onCompressionRowClicked();
    void onModeRowClicked();
    void onResolutionChanged(int value);
    void onThresholdChanged(int value);
    void onBlankPagesClicked();
    void onGridPageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);
    void onImageListSelectionChanged();

    /// Space of the list of the images (markToggled, from the event loop): the selected images
    /// are left unchanged (mode Original, as Exclude of Optimize Images), or converted again,
    /// when all of them are unchanged
    void onImageMarksToggled();
    void onItemContextMenuRequested(const QPoint& position);
    void onContextMenuTriggered(QAction* action);
    void onImagesCollected();
    void onPreviewTimeout();
    void onPreviewFinished();
    void onEstimateFinished();
    void onDetectionPoll();
    void onDetectionFinished();
    void onBlankPagesQuestionFinished(int result);
    void onPartialResultQuestionFinished(int result);
    void onComparisonZoomChanged();
    void onShowOnPage();

    // ------------------------------------------------------------- state

    std::vector<int> m_selectedItems;
    int m_currentItem = -1;
    int m_anchorItem = -1;                  ///< The item of Shift
    bool m_isContentEnabled = true;
    bool m_isUpdatingControls = false;       ///< The spin boxes are set by the model, not the user

    std::shared_ptr<const PDFDocumentImageList> m_imageList;   ///< The images of the shown document (Source Images), null = not collected yet
    quint64 m_imageListGeneration = 0;      ///< Counted up by every new (or reset) m_imageList: its identity (never its address, which a new list may reuse)
    PDFDocumentPointer m_imageListDocument;                     ///< The version, whose images the list has
    std::vector<int> m_convertibleImages;                       ///< The items: indices of the convertible images in m_imageList
    mutable int m_estimatedDpi = 0;                             ///< The estimate of the document (dpi 0), a cache of getEffectiveDpi
    mutable PDFDocumentPointer m_estimatedDpiDocument;          ///< The version of the estimate

    /// A cache of getModesOfItems: the text of "items", the source and the list of the images, which it was parsed for
    mutable QString m_modesCacheText;
    mutable bool m_modesCacheIsPages = false;
    mutable bool m_isModesCacheValid = false;
    mutable quint64 m_modesCacheListGeneration = 0;     ///< m_imageListGeneration of the cache
    mutable std::map<int, ItemMode> m_modesCache;

    /// Jobs (a generation per kind: a result of an older one is dropped)
    QFutureWatcher<std::shared_ptr<const PDFDocumentImageList>> m_imagesWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_imagesCancel;
    QFutureWatcher<PDFBitonalDocumentCreator::Preview> m_previewWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_previewCancel;
    QTimer m_previewTimer;
    QString m_previewKey;                                       ///< Item and settings of the running preview
    std::optional<PDFBitonalDocumentCreator::Preview> m_preview;    ///< The preview shown
    QString m_shownPreviewKey;
    QImage m_cachedOriginal;                                    ///< The rendered page / decoded image of an earlier preview
    QString m_cachedOriginalKey;                                ///< Item and resolution of m_cachedOriginal (set with the image, never before)
    PDFDocumentPointer m_cachedOriginalDocument;                ///< The version, which m_cachedOriginal was made of
    QString m_previewOriginalKey;                               ///< Item and resolution of the running preview
    PDFDocumentPointer m_previewDocument;                       ///< The version of the running preview
    QFutureWatcher<std::vector<qint64>> m_estimateWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_estimateCancel;
    std::vector<QString> m_estimateKeys;                        ///< Keys of the running estimate
    std::map<QString, qint64> m_estimates;                      ///< Encoded size of an item with its settings (R4-22 cache)

    /// Detection of the blank pages
    QFutureWatcher<std::vector<PDFBitonalDocumentCreator::BlankPageResult>> m_detectionWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_detectionCancel;
    std::shared_ptr<PDFProgressCounter> m_detectionProgress;
    QTimer m_detectionTimer;
    bool m_isDetecting = false;
    int m_examinedPageCount = 0;
    int m_pagesToExamineCount = 0;
    std::vector<PDFInteger> m_foundBlankPages;                  ///< The pages of the question 307:6250
    QPointer<PDFMessageBox> m_question;

    // Parts
    QPointer<QToolButton> m_sourceButton;
    QPointer<QToolButton> m_methodButton;
    QPointer<QToolButton> m_compressionButton;
    QPointer<QToolButton> m_modeButton;
    QPointer<QToolButton> m_blankPagesButton;
    QPointer<PDFAppToolBitonalMenuPanel> m_sourcePanel;
    QPointer<PDFAppToolBitonalMenuPanel> m_methodPanel;
    QPointer<PDFAppToolBitonalMenuPanel> m_compressionPanel;
    QPointer<PDFAppToolBitonalMenuPanel> m_modePanel;
    std::map<QString, QPointer<PDFTileRow>> m_sourceRows;
    std::map<QString, QPointer<PDFTileRow>> m_methodRows;
    std::map<QString, QPointer<PDFTileRow>> m_compressionRows;
    std::map<int, QPointer<PDFTileRow>> m_modeRows;
    QPointer<PDFLabel> m_modeCaption;
    QPointer<QSpinBox> m_resolutionSpinBox;
    QPointer<QSpinBox> m_thresholdSpinBox;
    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<PDFAppToolPageGrid> m_pageGrid;
    QPointer<PDFAppToolImageList> m_imageListWidget;
    QPointer<PDFAppParagraph> m_leftHint;
    QPointer<QWidget> m_canvas;             ///< The canvas page: the comparison
    QPointer<PDFLabel> m_canvasTitle;       ///< The title of the comparison (a part of it)
    QPointer<PDFAppToolImageComparison> m_comparison;
    QPointer<PDFAppToolSectionHeader> m_selectedHeader;
    QPointer<PDFKeyValueList> m_selectedRows;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<PDFAppToolCallout> m_callout;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_BITONAL_H
