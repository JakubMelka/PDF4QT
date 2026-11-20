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

#ifndef PDFAPPTOOLGUI_COMPARE_H
#define PDFAPPTOOLGUI_COMPARE_H

#include "pdfappglobal.h"
#include "pdfappcomparedifferencemodel.h"
#include "pdfappcompareview.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_compare.h"
#include "pdffluentwidgets.h"

#include <QAbstractButton>
#include <QFutureWatcher>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>

#include <array>
#include <map>
#include <memory>
#include <optional>

class QAbstractButton;
class QAction;
class QMimeData;
class QPushButton;
class QButtonGroup;
class QSlider;
class QStackedWidget;
class QToolButton;
class QTreeView;
class QVBoxLayout;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppCompareDifferenceDelegate;
class PDFAppCompareDifferenceModel;
class PDFAppMainWindow;
class PDFAppParagraph;
class PDFAppPasswordDialog;
class PDFAppToolCallout;
class PDFAppToolInputList;
class PDFAppToolPanelHeader;
class PDFAppToolPanelPopup;
class PDFAppToolTab;
class PDFColorPickerDialog;
class PDFColorSwatch;
class PDFComboBox;
class PDFDoubleSpinBox;
class PDFIconBadge;
class PDFLabel;
class PDFLineEdit;
class PDFTileRow;
class PDFAppToolInput;

/// Compare of one run of the GUI: the core tool, which gives the whole result of its run (every difference, not only
/// the MAXIMUM_DIFFERENCES of the data for MCP) to the collector of that run (PDFDocumentToolCompare::compare). The
/// GUI creates one for each run and gives it by the environment of the run (PDFAppToolRunEnvironment::runTool, as Ink
/// Coverage): the collector belongs to the run, no other run can reach it. Immutable; the collector is thread-safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareTool : public PDFDocumentToolCompare
{
public:
    /// \param collector The collector of the run, nullptr = none (PDFDocumentToolCompare::execute exactly)
    explicit PDFAppCompareTool(std::shared_ptr<PDFDiffResultCollector> collector);

    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    const std::shared_ptr<PDFDiffResultCollector>& getCollector() const { return m_collector; }

private:
    std::shared_ptr<PDFDiffResultCollector> m_collector;
};

/// A row of the panel Colors (color-removed 492:20127): the name Regular 12 text/secondary, five swatches 28 (the
/// annotation colors red, orange, green, blue and black - the default color of the kind in the place of its hue,
/// 492:20129) and the round "+" of a custom color (the color picker, new + QPointer). A choice of the user emits
/// colorChosen; setColor never emits.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareColorRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// \param name Name of the kind ("Removed")
    /// \param defaultColor The default color of the kind (the token diff/*)
    /// \param defaultSlot The swatch, which the default color takes (its hue)
    explicit PDFAppCompareColorRow(const QString& name, const QColor& defaultColor, int defaultSlot, QWidget* parent);
    virtual ~PDFAppCompareColorRow() override;

    /// The colors of the swatches (canonical values)
    static std::array<QColor, 5> getSwatchColors(const QColor& defaultColor, int defaultSlot);

    void setColor(const QColor& color);
    const QColor& getColor() const { return m_color; }

    PDFColorSwatch* getSwatch(int index) const;
    QAbstractButton* getCustomButton() const;
    PDFColorPickerDialog* getPickerDialog() const { return m_dialog.data(); }

Q_SIGNALS:
    void colorChosen(const QColor& color);

private:
    void onSwatchClicked();
    void onCustomClicked();
    void onDialogColorSelected(const QColor& color);

    QColor m_color;
    std::array<QPointer<PDFColorSwatch>, 5> m_swatches;
    QPointer<QAbstractButton> m_custom;
    QPointer<PDFColorPickerDialog> m_dialog;
};

/// The card of a slot of the choice of the documents (492:17871, 492:18158, 492:106266): the title ("Original") and
/// its line ("The older version"); the chosen document as a selected row with Change… (its pages, "open tab"), or a
/// red callout of an input, which cannot be read (its reason, "Choose another file."), the dashed place "Drop a PDF
/// here" with Choose File…, and the group OPEN TABS (the ready documents of the window; an unsaved one "unsaved
/// changes, the working version is used" - its snapshot of now; the document of the other slot Disabled "Already
/// used as the Original", FC-3). The card only says what was chosen: the signals may rebuild it (the old rows go
/// later).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareSlotCard : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// A document of the group OPEN TABS
    struct OpenDocument
    {
        QString documentId;
        QString name;
        QString details;            ///< "24 pages", "25 pages · unsaved changes, the working version is used"
        bool isWarning = false;     ///< The details in the warning color (an unsaved document)
        bool isDisabled = false;    ///< Already used in the other slot (FC-3)
    };

    /// What the card shows
    struct Content
    {
        QString inputName;          ///< The chosen document, empty = none
        QString inputDetails;       ///< "24 pages · open tab"
        QString errorTitle;         ///< "Scan_2019.pdf cannot be read", empty = no error
        QString errorText;
        QString passwordText;       ///< "Contract.pdf needs a password", empty = no password needed
        std::vector<OpenDocument> openDocuments;
    };

    explicit PDFAppCompareSlotCard(const QString& title, const QString& subtitle, QWidget* parent);
    virtual ~PDFAppCompareSlotCard() override;

    void setContent(const Content& content);

    /// The texts of the card in the language of now (the card stays over a change of the language)
    void retranslate(const QString& title, const QString& subtitle);

    PDFTileRow* getInputRow() const { return m_inputRow.data(); }
    PDFAppToolCallout* getErrorCallout() const { return m_error.data(); }
    QWidget* getDropArea() const { return m_dropArea.data(); }
    QPushButton* getChooseButton() const { return m_chooseButton.data(); }
    QPushButton* getPasswordButton() const { return m_passwordButton.data(); }
    std::vector<PDFTileRow*> getDocumentRows() const;

Q_SIGNALS:
    void chooseFileRequested();
    void changeRequested();
    void passwordRequested();
    void documentChosen(const QString& documentId);

private:
    void onDocumentRowClicked();

    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_subtitle;
    QPointer<PDFLabel> m_dropText;
    QPointer<PDFTileRow> m_inputRow;
    QPointer<PDFAppToolCallout> m_error;
    QPointer<QWidget> m_passwordRow;
    QPointer<PDFLabel> m_passwordLabel;
    QPointer<QPushButton> m_passwordButton;
    QPointer<QWidget> m_dropArea;
    QPointer<QPushButton> m_chooseButton;
    QPointer<PDFLabel> m_documentsCaption;
    QPointer<QVBoxLayout> m_documentsLayout;
    std::vector<QPointer<PDFTileRow>> m_documentRows;
};

/// The canvas page of Compare: the choice of the two documents (492:17871 "Choose the two documents to compare" with
/// the cards Original and Revised side by side) until both are chosen, then the view of the pages
/// (PDFAppCompareView). Ownership: the page owns the cards and the view (children); no signal of its own.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppCompareCanvas(QWidget* parent);
    virtual ~PDFAppCompareCanvas() override;

    void showChoice();
    void showView();
    bool isChoiceShown() const;

    /// The texts of the choice in the language of now (the canvas stays over a change of the language)
    void retranslate();

    PDFAppCompareSlotCard* getSlotCard(int slot) const;
    PDFAppCompareView* getView() const { return m_view.data(); }
    PDFLabel* getChoiceTitle() const { return m_choiceTitle.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget> m_choicePage;
    QPointer<PDFLabel> m_choiceTitle;
    QPointer<PDFLabel> m_choiceHint;
    std::array<QPointer<PDFAppCompareSlotCard>, 2> m_slotCards;
    QPointer<PDFAppCompareView> m_view;
};

/// The GUI of Compare (review.compare, Figma 492:17871 … 492:109399; plan of step 7 of phase 2, 2.1, 4.9, 4.10 with
/// K7.2 - K7.4, K7.25, R7-9, owner answers Q7.13, Q7.17): its own tab (PDFAppOwnTabToolHost, isSingleOwnTab false -
/// more comparisons at once), the slots Original and Revised (the input list of the tab: a dropped file, Choose
/// File…, the open tabs - an unsaved tab is its working version, a snapshot at the choice; the password dialog of step
/// 2, PDFAppPasswordDialog), the panel Documents (Change…, Swap, the page ranges "1-24 of 24" - K7.3: the parser and the
/// texts of PDFClosedIntervalSet as Page selection), the canvas PDFAppCompareCanvas, the list of the differences
/// (PDFAppCompareDifferenceModel), the menus View, Filter (with the counts of the kinds, a kind without a difference
/// Disabled), Show (Difference Highlights, Markers at the Edge, Only Pages with Differences, Follow the Selected
/// Difference), Options ("takes effect after Compare Again"), Export (Report as PDF…, Differences as XML… - Disabled
/// without a shown difference, R7-9), F5 (the comparison), F6 / Shift+F6 (the next / previous difference), the stale
/// state (FC-4, R7-2: Compare Again Disabled after a result, Primary only when stale - Q7.17), the overlay (the blend,
/// the page sizes, the scales, the shift in mm, Reset) and the colors of the four kinds with a swap, when a color is
/// taken by another kind.
///
/// It runs PDFDocumentToolCompare by its run tool (PDFAppCompareTool with a collector of the run) and keeps the whole
/// PDFDiffResult and the snapshots of the inputs of the run (plan 6.3: a closed source tab never invalidates them);
/// FC-5: the close of the source tab of a slot stops a running comparison and empties the slot. The document of the
/// view is built by PDFDiffReportBuilder::createCombinedDocument in one asynchronous job (QtConcurrent::run on shared
/// immutable snapshots, the result taken only from the last request while this GUI lives); the report PDF of Export
/// the same way (createReport + the writer into bytes) - then the dialog of the export (openExport), which never runs
/// the comparison again. The XML is the shown differences (PDFDiffResult::saveToXML). The texts of the differences
/// are an extraction: XML needs bit 5 and the report the rule of Merge of every input - a restricted input asks for
/// its owner password (FC-6, the dialog of step 2).
///
/// The view settings (the colors, the items of Show) are the last values of the tool (PDFToolParameterRepository with
/// the definitions of this GUI - the old Diff kept them in QSettings); the overlay resets with the inputs (the old
/// Diff).
///
/// The command line PDF4QT --compare a.pdf b.pdf (Q7.13): openCompareTab opens this tab with both inputs and the
/// comparison starts, when both are read.
///
/// Ownership (checklist point 1): the shell owns this GUI and its parts (QPointer); the list of the inputs belongs to
/// the tab (QPointer); the jobs hold copies only (the snapshots by their shared pointers, the result, a cancellation
/// flag by a shared pointer) - their watchers are children of this GUI, a late result of a deleted GUI is dropped
/// with them; the popups (the password dialog, the color picker) are new + QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUICompare : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// The slots of the inputs
    enum Slot
    {
        OriginalSlot = 0,
        RevisedSlot = 1,
        SlotCount = 2
    };

    /// What the tool shows
    enum class Content
    {
        Choice,         ///< A slot is empty or its input cannot be used (492:17871, 492:18158, 492:106266)
        Ready,          ///< Both inputs, not compared yet (492:18424); also after Stop (FC-1)
        Running,        ///< The comparison runs (492:18869)
        Result,         ///< The differences (492:19314), stale after a change (492:109399)
        NoDifferences,  ///< No difference (492:105498)
        Failed          ///< The comparison failed (FC-2)
    };

    /// The dynamic property of the input list of a tab, which starts the comparison, when both inputs are read
    /// (PDF4QT --compare): the ids of the two inputs of the command line (QStringList). Another pair (a file refused
    /// as the same one, an input changed by the user) or an input, which cannot be read, ends the request unfulfilled
    static constexpr const char* START_PROPERTY = "pdf4qtCompareStart";

    /// Names of the view settings stored as the last values of the tool (not parameters of the core)
    static constexpr const char* SETTING_COLOR_REMOVED = "viewColorRemoved";
    static constexpr const char* SETTING_COLOR_ADDED = "viewColorAdded";
    static constexpr const char* SETTING_COLOR_REPLACED = "viewColorReplaced";
    static constexpr const char* SETTING_COLOR_MOVED = "viewColorMoved";
    static constexpr const char* SETTING_HIGHLIGHTS = "viewHighlights";
    static constexpr const char* SETTING_MARKERS = "viewMarkers";
    static constexpr const char* SETTING_ONLY_CHANGED = "viewOnlyChangedPages";
    static constexpr const char* SETTING_FOLLOW = "viewFollowSelection";

    /// The kinds of the colors (the rows of the panel Colors)
    enum ColorKind
    {
        RemovedColor = 0,
        AddedColor,
        ReplacedColor,
        MovedColor,
        ColorKindCount
    };

    explicit PDFAppToolGUICompare(QObject* parent = nullptr);
    virtual ~PDFAppToolGUICompare() override;

    /// Registers the tool review.compare and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerCompareTools();

    /// Opens a new tab of Compare in the window with the two files (PDF4QT --compare original revised, Q7.13): the
    /// comparison starts, when both are read. \returns the tab, nullptr = not opened (a wrong count of files, no tab)
    static PDFAppToolTab* openCompareTab(PDFAppMainWindow* window, const QStringList& files);

    /// The definitions of the view settings (SETTING_*)
    static PDFDocumentToolParameterDefinitions getViewSettingDefinitions();

    /// A choice of the color of a kind with the swap (the old Diff had none): when another kind has the color, it
    /// takes the old color of this kind - two kinds never share a color
    static PDFDiffReportColors chooseColor(const PDFDiffReportColors& colors, ColorKind kind, const QColor& color);
    static QColor getColor(const PDFDiffReportColors& colors, ColorKind kind);

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual QString getCancelText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QString getOwnTabIconName() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual PDFAppToolInputRules getInputRules() const override;
    virtual bool isResultKeptInTool() const override;
    virtual void onResultKept(const PDFDocumentToolResult& result) override;
    virtual bool isResultStaleBy(const QString& parameterName) const override;
    virtual QString getStaleTitle() const override;
    virtual QString getStaleText() const override;
    virtual PDFAppToolCalloutPlace getStaleCalloutPlace() const override;
    virtual void onExportDecided(const PDFAppToolResultOutcome& outcome) override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onRunRefused() override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void setContentEnabled(bool isEnabled) override;
    virtual void retranslate() override;

    // ------------------------------------------------------------- state (tests and the parts)

    Content getContent() const { return m_content; }

    /// The whole result and the shown one (filtered by the menu Filter)
    const PDFDiffResult& getResult() const { return m_result; }
    const PDFDiffResult& getShownResult() const { return m_shownResult; }

    /// Shows a result of the comparison of the snapshots (onResultKept with the collector; tests): the inputs of the
    /// report are the documents of the run
    void showResult(const PDFDiffResult& result, const PDFDocumentToolInputDocument& original, const PDFDocumentToolInputDocument& revised);

    /// The view of now (the parameter view)
    PDFDiffReportView getView() const;
    void setView(PDFDiffReportView view);

    /// The selected difference (an index of the shown result), std::nullopt = none
    std::optional<size_t> getSelectedDifference() const { return m_selected; }
    void selectDifference(std::optional<size_t> index);

    /// F6 / Shift+F6 (the old Next / Previous Difference)
    void goToNextDifference();
    void goToPreviousDifference();
    bool canGoToNextDifference() const;
    bool canGoToPreviousDifference() const;

    /// The counts of the menu Filter of the whole result (CATEGORY_* of PDFDocumentToolCompare)
    int getCategoryCount(const QString& categoryId) const;

    /// The items of the menu Show
    bool isHighlightsShown() const { return m_isHighlightsShown; }
    bool isMarkersShown() const { return m_isMarkersShown; }
    bool isOnlyChangedPagesShown() const { return m_isOnlyChangedPages; }
    bool isSelectionFollowed() const { return m_isSelectionFollowed; }
    void setShowOption(const QString& settingName, bool isOn);

    const PDFDiffReportColors& getColors() const { return m_colors; }
    void setColor(ColorKind kind, const QColor& color);

    const PDFAppCompareOverlaySettings& getOverlaySettings() const { return m_overlay; }
    void setOverlaySettings(const PDFAppCompareOverlaySettings& settings);
    void resetOverlay();

    /// Swap (one action of the user, not an undo step): the inputs, their page ranges
    void swapSlots();

    /// A file (Choose File…, a drop) or a snapshot of an open document in the slot instead of its input
    void setSlotFile(Slot slot, const QString& filePath);
    void setSlotDocument(Slot slot, const QString& documentId);

    /// The text of the page range of the slot and its error (empty = valid)
    void setPageRangeText(Slot slot, const QString& text);
    QString getPageRangeText(Slot slot) const;
    QString getPageRangeError(Slot slot) const;

    /// The input of the slot (nullptr = none)
    const PDFAppToolInput* getSlotInput(Slot slot) const;

    /// The slot can be compared: a PDF with pages (a restricted one too - viewing is reading, Q7.2)
    bool isSlotComparable(Slot slot) const;

    /// Export (the dialog of the export opens when the file is made; the report in its job)
    bool exportXml();
    bool exportReport();
    bool isExportEnabled() const;

    /// The document of the view is being built
    bool isViewDocumentPending() const;

    /// A change of the input list by this GUI is in progress (its signals wait for the end of the change)
    bool isChangingList() const { return m_isChangingList; }

    /// Parts (tests; nullptr = none)
    PDFAppCompareCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppCompareView* getCompareView() const;
    PDFAppCompareDifferenceModel* getDifferenceModel() const { return m_differenceModel.data(); }
    QTreeView* getDifferenceView() const { return m_differenceView.data(); }
    PDFAppToolPanelHeader* getDifferencesHeader() const { return m_differencesHeader.data(); }
    PDFAppParagraph* getDifferencesEmptyText() const { return m_emptyText.data(); }
    PDFAppToolPanelPopup* getMenuPanel(const QString& menuId) const;
    PDFTileRow* getMenuRow(const QString& rowId) const;
    QToolButton* getPreviousButton() const { return m_previousButton.data(); }
    QToolButton* getNextButton() const { return m_nextButton.data(); }
    PDFTileRow* getSlotRow(Slot slot) const { return m_slotRows[slot].data(); }
    PDFLineEdit* getPageRangeField(Slot slot) const { return m_rangeFields[slot].data(); }
    PDFLabel* getPageCountLabel(Slot slot) const { return m_pageCountLabels[slot].data(); }
    PDFLabel* getPageRangeErrorLabel(Slot slot) const { return m_rangeErrorLabels[slot].data(); }
    QPushButton* getSwapButton() const { return m_swapButton.data(); }
    PDFAppToolCallout* getInfoCallout() const { return m_infoCallout.data(); }
    QWidget* getDocumentsSection() const { return m_documentsSection.data(); }
    QWidget* getOverlaySection() const { return m_overlaySection.data(); }
    QSlider* getBlendSlider() const { return m_blendSlider.data(); }
    PDFLabel* getBlendLabel() const { return m_blendLabel.data(); }
    PDFComboBox* getPageSizesCombo() const { return m_pageSizesCombo.data(); }
    PDFDoubleSpinBox* getScaleSpinBox(Slot slot) const { return m_scaleSpinBoxes[slot].data(); }
    PDFDoubleSpinBox* getOffsetSpinBox(int axis) const { return m_offsetSpinBoxes[axis].data(); }
    QAbstractButton* getOverlayResetLink() const { return m_overlayReset.data(); }
    PDFAppCompareColorRow* getColorRow(ColorKind kind) const { return m_colorRows[kind].data(); }
    PDFAppPasswordDialog* getPasswordDialog() const { return m_passwordDialog.data(); }
    PDFAppToolPanelPopup* getChangePanel() const { return m_changePanel.data(); }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class StaleReason
    {
        Options,
        Documents
    };

    /// A change of the input list by this GUI (m_isChangingList: onListChanged ignores the signals of its steps) from
    /// the construction to end() or the destruction - on every return. A slot of the list may delete the GUI: a
    /// deleted GUI is not touched.
    class ChangingListGuard
    {
    public:
        explicit ChangingListGuard(PDFAppToolGUICompare* gui);
        ~ChangingListGuard();

        ChangingListGuard(const ChangingListGuard&) = delete;
        ChangingListGuard& operator=(const ChangingListGuard&) = delete;

        /// The change has ended (before the GUI reacts to it once)
        void end();

    private:
        QPointer<PDFAppToolGUICompare> m_gui;
    };

    /// The facts of an exported file (the dialog of the export, 492:107143, 492:108839): of the result and the filter,
    /// from which the file is made - taken when its job starts, never from the view of the time the dialog opens
    struct ExportFacts
    {
        PDFDiffReportView view = PDFDiffReportView::SideBySide;
        int differenceCount = 0;
        int pairCount = 0;
        int originalPageCount = 0;
        int revisedPageCount = 0;
    };

    static ExportFacts createExportFacts(const PDFDiffResult& shownResult, PDFDiffReportView view);

    // The list of the inputs
    void connectList();
    void onListChanged();
    void onListInputChanged(const QString& inputId);
    void onListDestroyed();
    void onDocumentRemoved(const QString& documentId);
    void onRegistryChanged();
    void setInputIds(const QStringList& ids);
    void onInputsReplaced();
    void startWhenRequested();
    void onStartQueued();

    // The parts
    void createMenus(PDFAppToolPanelBuilder& builder);
    void createKeyActions();
    void readViewSettings();
    void storeViewSetting(const QString& name, const QVariant& value);
    void onViewRowClicked();
    void onFilterRowClicked();
    void onShowRowClicked();
    void onOptionRowClicked();
    void onExportReportClicked();
    void onExportXmlClicked();
    void onPreviousClicked();
    void onNextClicked();
    void onConfirmKey();
    void openChangePanel(Slot slot, QWidget* anchor);
    void onChangeRowClicked();
    void onChooseOriginal();
    void onChooseRevised();
    void onChangeOriginal();
    void onChangeRevised();
    void onPasswordOriginal();
    void onPasswordRevised();
    void onDocumentChosenOriginal(const QString& documentId);
    void onDocumentChosenRevised(const QString& documentId);
    void onRangeEditedOriginal();
    void onRangeEditedRevised();
    void onDifferenceSelectionChanged();
    void onQueuedDifferenceSelectionChanged();
    void onColorChosen(const QColor& color);
    void onBlendChanged(int value);
    void onPageSizesChanged(int index);
    void onOverlayValueChanged();
    void onViewRepaint();
    void onViewRepaintQueued();
    void onMenuToggled();
    void onMenuToggledQueued();
    QString getOpenMenuStatusText() const;
    void chooseSlotFile(Slot slot);
    void addSlotInput(Slot slot, const QString& filePath, const QString& documentId);
    void openPassword(Slot slot);
    void onPasswordRemoveRequested(const QString& inputId);
    bool dropFiles(const QMimeData* mimeData, const QPoint& globalPosition);

    // The result and the view
    void applyResult(const PDFDiffResult& result);
    void updateShownResult();
    void requestViewDocument();
    void onViewDocumentReady();
    void onReportReady();
    PDFDiffResult createPreviewSequence() const;
    std::vector<PDFInteger> getRangePages(Slot slot) const;
    void clearResult();
    void cancelReport();

    // Updates (the texts first, the states and the visibility last with guards)
    void updateAll();
    void updateSlots();
    void updateChoice();
    void updateDifferences();
    void updateCanvas();
    void updateMenus();
    void updateOverlayControls();
    void updateViewTexts();

    QString getSlotTitle(Slot slot) const;
    QString getInputName(Slot slot) const;
    QString getPagesText() const;
    QString getDifferencesSummary() const;
    QString getFilteredSummary() const;
    QWidget* getDialogParent() const;
    bool checkExportPermission(bool isReport);

    QPointer<PDFAppToolInputList> m_list;
    QStringList m_lastInputIds;
    Content m_content = Content::Choice;
    PDFDiffResult m_result;
    PDFDiffResult m_shownResult;
    bool m_hasResult = false;
    bool m_isStopped = false;
    QString m_failureText;
    std::vector<PDFDocumentToolInputDocument> m_resultInputs;     ///< The snapshots of the shown result (original, revised)
    mutable std::shared_ptr<PDFDiffResultCollector> m_runCollector;
    mutable std::vector<PDFDocumentToolInputDocument> m_runInputs;
    mutable StaleReason m_staleReason = StaleReason::Options;
    std::optional<size_t> m_selected;
    bool m_isRunning = false;
    bool m_isStartQueued = false;
    std::array<QString, SlotCount> m_rangeTexts;
    std::array<QString, SlotCount> m_rangeErrors;

    PDFDiffReportColors m_colors;
    bool m_isHighlightsShown = true;
    bool m_isMarkersShown = true;
    bool m_isOnlyChangedPages = false;
    bool m_isSelectionFollowed = true;
    bool m_areViewSettingsRead = false;
    PDFAppCompareOverlaySettings m_overlay;
    PDFInteger m_shownPage = -1;
    QString m_shownZoomText;        ///< The zoom item of the status bar (its text of the zoom of the canvas)

    // The jobs (one each): the document of the view and the report
    quint64 m_viewGeneration = 0;
    std::shared_ptr<PDFAppCancelFlag> m_viewCancel;
    QPointer<QFutureWatcher<PDFDiffCombinedDocument>> m_viewWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_reportCancel;
    QPointer<QFutureWatcher<PDFDocumentToolResult>> m_reportWatcher;
    PDFDiffReportView m_viewDocumentView = PDFDiffReportView::SideBySide;
    bool m_isViewDocumentPending = false;
    enum class ExportKind
    {
        None,
        Xml,
        Report
    };
    ExportKind m_exportKind = ExportKind::None;
    ExportFacts m_exportFacts;      ///< The facts of the dialog of the export
    ExportFacts m_reportFacts;      ///< The facts of the report of the running job of the report

    // The parts
    QPointer<PDFAppCompareCanvas> m_canvas;
    QPointer<PDFAppToolPanelHeader> m_differencesHeader;
    QPointer<QStackedWidget> m_differencesStack;
    QPointer<QTreeView> m_differenceView;
    QPointer<PDFAppCompareDifferenceModel> m_differenceModel;
    QPointer<PDFAppCompareDifferenceDelegate> m_differenceDelegate;
    QPointer<PDFIconBadge> m_emptyBadge;
    QPointer<PDFAppParagraph> m_emptyText;
    std::map<QString, QPointer<PDFAppToolPanelPopup>> m_menus;
    std::map<QString, QPointer<PDFTileRow>> m_menuRows;
    QPointer<QToolButton> m_previousButton;
    QPointer<QToolButton> m_nextButton;
    QPointer<PDFAppToolCallout> m_infoCallout;
    QPointer<QWidget> m_rightPanel;
    QPointer<QWidget> m_documentsSection;
    std::array<QPointer<PDFTileRow>, SlotCount> m_slotRows;
    std::array<QPointer<PDFLineEdit>, SlotCount> m_rangeFields;
    std::array<QPointer<PDFLabel>, SlotCount> m_pageCountLabels;
    std::array<QPointer<PDFLabel>, SlotCount> m_rangeErrorLabels;
    QPointer<QPushButton> m_swapButton;
    QPointer<QVBoxLayout> m_rightLayout;
    QPointer<QWidget> m_overlaySection;
    QPointer<QSlider> m_blendSlider;
    QPointer<PDFLabel> m_blendLabel;
    QPointer<PDFComboBox> m_pageSizesCombo;
    std::array<QPointer<QWidget>, SlotCount> m_scaleRows;
    std::array<QPointer<PDFDoubleSpinBox>, SlotCount> m_scaleSpinBoxes;
    std::array<QPointer<PDFDoubleSpinBox>, 2> m_offsetSpinBoxes;
    QPointer<QAbstractButton> m_overlayReset;
    QPointer<QWidget> m_colorsSection;
    std::array<QPointer<PDFAppCompareColorRow>, ColorKindCount> m_colorRows;
    QPointer<PDFAppPasswordDialog> m_passwordDialog;
    QPointer<PDFAppToolPanelPopup> m_changePanel;
    Slot m_changeSlot = OriginalSlot;
    QPointer<QWidget> m_keyWidget;
    std::vector<QPointer<QAction>> m_keyActions;
    bool m_wasDropAccepted = false;
    bool m_isUpdatingControls = false;
    bool m_isChangingList = false;
    bool m_isViewRepaintQueued = false;
    bool m_isMenuStatusQueued = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_COMPARE_H
