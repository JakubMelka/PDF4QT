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

#ifndef PDFAPPTOOLGUI_AUDIOBOOK_H
#define PDFAPPTOOLGUI_AUDIOBOOK_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappaudiobookoverlay.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include "pdfdocumenttextflow.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumenttoolshell.h"
#include "pdfspeechsynthesizer.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>

#include <memory>
#include <set>
#include <vector>

class QAction;
class QCheckBox;
class QMenu;
class QPushButton;
class QScrollArea;
class QSlider;
class QTimer;

namespace pdf
{

class PDFAppParagraph;
class PDFAppSpeechEngine;
class PDFAppToolPagePicture;
class PDFComboBox;
class PDFKeyValueList;
class PDFLabel;
class PDFLineEdit;
class PDFListView;
class PDFMessageBox;
class PDFOCRCancelToken;
class PDFPlainTextEdit;
class PDFSegmentedControl;
class PDFTag;
class PDFTextLink;

/// The items of the text stream of Create Audio Book (Text stream 341:9229): the model owns the
/// editor of the stream (PDFDocumentTextFlowEditor, a value - its items with Read / Skip, their
/// texts and their order) and the selection of the panel (indices of the items, never in the
/// editor and never in an undo step). The rows are the items of the filter (All, Skipped,
/// Modified). The GUI changes the items only through the functions below; each resets or updates
/// the rows (the signals of the model are the last thing of each function).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAudioBookItemModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// Chips of 341:9229
    enum class Filter
    {
        All,
        Skipped,
        Modified
    };

    enum Role
    {
        PageTextRole = Qt::UserRole + 1,    ///< "Page 308"
        TypeTextRole,                       ///< "Text" / "Special"
        StateTextRole,                      ///< "Read" / "Skipped" / "Modified"
        StateRole,                          ///< PDFAppAudioBookOverlay::State as int (Selected is not a state of a row)
        IsSelectedRole,                     ///< The item is selected
        ItemIndexRole                       ///< Index of the item in the editor (qint64)
    };

    explicit PDFAppAudioBookItemModel(QObject* parent);
    virtual ~PDFAppAudioBookItemModel() override;

    virtual int rowCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    /// A new stream (Rebuild, Undo / Redo): the selection is cleared
    void setEditor(PDFDocumentTextFlowEditor editor);
    const PDFDocumentTextFlowEditor& getEditor() const { return m_editor; }

    /// The stream is being read (no rows until setEditor)
    void setBuilding(bool isBuilding);
    bool isBuilding() const { return m_isBuilding; }

    void setFilter(Filter filter);
    Filter getFilter() const { return m_filter; }

    /// The item of a row and the row of an item (-1 = not shown by the filter)
    size_t getItemIndex(int row) const;
    int getRow(size_t itemIndex) const;

    /// The selection (indices of the items)
    const std::set<size_t>& getSelection() const { return m_selection; }
    void setSelection(std::set<size_t> selection);

    /// Counts of the stream: all items, the read ones, the skipped and the modified ones
    size_t getItemCount() const { return m_editor.getItemCount(); }
    size_t getReadCount() const;
    size_t getSkippedCount() const;
    size_t getModifiedCount() const;

    /// Characters of the read items (the estimate of the length)
    qint64 getReadCharacterCount() const;

    /// The state of an item (Read, Modified, Skipped)
    PDFAppAudioBookOverlay::State getItemState(size_t itemIndex) const;

    /// The edits of the panel (each one step of Undo of the GUI): Read / Skip of the items, the
    /// text of an item, the original texts, the move of the selection
    void setItemsRead(const std::set<size_t>& items, bool isRead);
    void setItemText(size_t itemIndex, const QString& text);
    void restoreTexts(const std::set<size_t>& items);
    void moveSelection(bool isUp);

    /// The stream differs from the stream of its defaults: skipped items, which are read by default,
    /// read items, which are skipped by default, edited texts and a changed order (the question of
    /// Rebuild 356:14056)
    struct Changes
    {
        size_t skipped = 0;     ///< Items read by default, which are skipped
        size_t read = 0;        ///< Items skipped by default, which are read
        size_t edited = 0;      ///< Modified texts
        bool isReordered = false;
        bool hasChanges() const { return skipped > 0 || read > 0 || edited > 0 || isReordered; }
    };
    Changes getChanges(const QStringList& readSpecial) const;

private:
    void updateRows();

    PDFDocumentTextFlowEditor m_editor;
    std::vector<size_t> m_rows;
    std::set<size_t> m_selection;
    Filter m_filter = Filter::All;
    bool m_isBuilding = false;
};

/// A row of the text stream (item 341:9401): the bar of the state (3 wide, 6 from the edge), the
/// line "Page 307 [Text] Read" (Regular 11 text/tertiary, the tag, the state Medium 11 in its color)
/// and the text (Regular 12 text/primary, at most three lines, crossed out when skipped); the
/// selected row has the background bg/accent-subtle, radius 8
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAudioBookItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QStyledItemDelegate;

public:
    static constexpr int ROW_PADDING = 6;
    static constexpr int TEXT_LEFT = 17;
    static constexpr int TOP_HEIGHT = 21;
    static constexpr int TEXT_GAP = 2;
    static constexpr int TEXT_LINE = 15;
    static constexpr int MAXIMUM_LINES = 3;
    static constexpr int ROW_GAP = 2;

    explicit PDFAppAudioBookItemDelegate(QObject* parent);
    virtual ~PDFAppAudioBookItemDelegate() override;

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /// The lines of the text of a row of the width (at most MAXIMUM_LINES, the last one elided)
    static QStringList getTextLines(const QString& text, int width);
};

/// The canvas of Create Audio Book (document-canvas 341:9262): the bar "Page 308 · items of the
/// text stream" with Fit / 100 %, the legend Read / Selected / Modified / Skipped and the stage with
/// the picture of the page (PDFAppToolPagePicture of Extract Text, the compiled page of the view) and
/// PDFAppAudioBookOverlay over it; the hint of Select by rectangle at the bottom (360:13257).
///
/// Ownership: the canvas owns its parts (the scroll area, the picture, the overlay); the GUI holds
/// it and the overlay by QPointer. It emits no signals of its own.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAudioBookCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;

    explicit PDFAppAudioBookCanvas(QWidget* parent);
    virtual ~PDFAppAudioBookCanvas() override;

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFSegmentedControl* getZoomControl() const { return m_zoom; }
    PDFAppToolPagePicture* getPicture() const { return m_picture; }
    PDFAppAudioBookOverlay* getOverlay() const { return m_overlay; }
    QScrollArea* getPictureArea() const { return m_pictureArea; }
    PDFLabel* getRectangleHint() const { return m_rectangleHint; }

    void setTitle(const QString& text);

    /// The hint of Select by rectangle (360:13257) is shown
    void setRectangleHintShown(bool isShown);

    /// The content is shown (a document, which refuses the tool, shows an empty canvas)
    void setContentShown(bool isShown);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void onZoomChanged(int index);
    void updateTexts();
    void placeRectangleHint();

    QPointer<QWidget> m_content;
    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoom;
    std::vector<QPointer<PDFLabel>> m_legendLabels;
    QPointer<QScrollArea> m_pictureArea;
    QPointer<PDFAppToolPagePicture> m_picture;
    QPointer<PDFAppAudioBookOverlay> m_overlay;
    QPointer<PDFLabel> m_rectangleHint;
};

/// A popover of the menu Select (341:10217): Select by contained text, by regular expression, by
/// page list - the title with its icon, the field, the line of the check (the count of the matches
/// or the error) and Cancel / Select. The GUI checks the text (PDFAppToolGUIAudioBook::checkSelection)
/// and selects on Select (Enter).
///
/// Ownership: the GUI creates it (new, the parent is the canvas area) and keeps it by a QPointer;
/// it closes itself, the GUI deletes it later (deleteLater on closed).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAudioBookSelectPopover : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    /// The kinds of the popovers
    enum class Kind
    {
        ContainedText,
        RegularExpression,
        PageList
    };

    static constexpr int WIDTH = 330;

    explicit PDFAppAudioBookSelectPopover(Kind kind, PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppAudioBookSelectPopover() override;

    Kind getKind() const { return m_kind; }
    PDFLineEdit* getField() const { return m_field; }
    PDFLabel* getCheckLabel() const { return m_check; }
    QPushButton* getSelectButton() const { return m_selectButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

    /// The result of the check of the field: a valid text says what it selects, an invalid one why
    void setCheck(bool isValid, const QString& text);

Q_SIGNALS:
    /// The text of the field has changed
    void textChanged(const QString& text);

    /// Select (or Enter) with a valid text
    void selectRequested(const QString& text);

private:
    void onTextEdited(const QString& text);
    void onSelectClicked();

    Kind m_kind;
    bool m_isValid = false;
    QPointer<PDFLineEdit> m_field;
    QPointer<PDFLabel> m_checkIcon;
    QPointer<PDFLabel> m_check;
    QPointer<QPushButton> m_selectButton;
    QPointer<QPushButton> m_cancelButton;
};

/// The tool Create Audio Book (convert.audiobook, Figma 341:9229 ... 341:11665; plan of step 4 of
/// phase 2, 4.7, K4.7, R4-13 - R4-15; package P4): the list of the items of the text stream (its
/// own model over PDFDocumentTextFlowEditor, chips, Delete / Shift+Delete, Alt+arrows),
/// PDFAppAudioBookOverlay on the page, the menus Select (popovers 341:10217), Sync, Order (question
/// 356:14056), Voice; Selected item, Voice and format (Play Sample with the voice, speed and volume
/// of the tool - never the settings speech.* of Read aloud, R4-14 d), Summary. The result is a
/// temporary file (R4-1), saved by the result dialog; the state 341:11665 without an engine
/// (Open Speech Settings only on Windows).
///
/// The stream is the panel-only value editedStream (PDFDocumentToolAudioBookStream with the state
/// of the editor): every edit of the user is one step of Undo; Undo and Redo give the editor back.
/// The stream is read by one asynchronous job (QtConcurrent::run of createStream, a cancel token;
/// the result of an older job is dropped by its generation) at the start, by Rebuild and by a new
/// Order.
///
/// Ownership: the shell owns this GUI and its parts; the GUI keeps the parts in QPointer. The model
/// of the items, the watcher of the job and the sample engine are children of the GUI. The popovers
/// and the message boxes are new + QPointer, deleted later after they close.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIAudioBook : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUIAudioBook(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIAudioBook() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): the tool in
    /// PDFDocumentToolRegistry and its GUI in PDFDocumentToolGUIRegistry
    static void registerAudioBookTools();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getStatusText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual QString getRunningTimeText(int seconds) const override;
    virtual QString getBusyText() const override;
    virtual QString getPermissionTitle() const override;
    virtual QString getPermissionSubtitle() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;

    /// Parts (tests)
    PDFAppAudioBookItemModel* getItemModel() const { return m_itemModel; }
    PDFListView* getListView() const { return m_listView; }
    PDFLabel* getStreamTitle() const { return m_streamTitle; }
    PDFLabel* getStreamCount() const { return m_streamCount; }
    PDFTextLink* getRebuildLink() const { return m_rebuildLink; }
    QPushButton* getFilterChip(PDFAppAudioBookItemModel::Filter filter) const;
    PDFAppParagraph* getListHint() const { return m_listHint; }
    PDFLabel* getEmptyLabel() const { return m_emptyLabel; }
    PDFAppAudioBookCanvas* getCanvas() const { return m_canvas; }
    PDFAppAudioBookOverlay* getOverlay() const;
    PDFTag* getSelectedTag() const { return m_selectedTag; }
    PDFTextLink* getRestoreLink() const { return m_restoreLink; }
    PDFPlainTextEdit* getTextEditor() const { return m_textEditor; }
    QCheckBox* getReadCheckBox() const { return m_readCheckBox; }
    QSlider* getSpeedSlider() const { return m_speedSlider; }
    QSlider* getVolumeSlider() const { return m_volumeSlider; }
    PDFLabel* getSpeedLabel() const { return m_speedLabel; }
    PDFLabel* getVolumeLabel() const { return m_volumeLabel; }
    PDFComboBox* getFormatCombo() const { return m_formatCombo; }
    QPushButton* getPlaySampleButton() const { return m_playSampleButton; }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows; }
    QWidget* getSelectedSection() const { return m_selectedSection; }
    QToolButton* getReadButton() const { return m_readButton; }
    QToolButton* getSkipButton() const { return m_skipButton; }
    QToolButton* getMoveUpButton() const { return m_moveUpButton; }
    QToolButton* getMoveDownButton() const { return m_moveDownButton; }
    PDFAppToolPanelPopup* getSelectMenu() const { return m_selectMenu; }
    PDFAppToolPanelPopup* getSyncMenu() const { return m_syncMenu; }
    PDFAppToolEnumMenu* getOrderMenu() const { return m_orderMenu; }
    PDFAppToolPanelPopup* getVoiceMenu() const { return m_voiceMenu; }
    PDFAppAudioBookSelectPopover* getSelectPopover() const { return m_selectPopover; }
    PDFMessageBox* getRebuildQuestion() const { return m_rebuildQuestion; }
    PDFMessageBox* getNoVoiceMessage() const { return m_noVoiceMessage; }

    /// The context menu of the list (plan 2.7: Select, Read, Skip, Move), nullptr = closed
    QMenu* getContextMenu() const;

    /// The page shown by the canvas (-1 = none)
    PDFInteger getShownPageIndex() const { return m_shownPageIndex; }

    /// The voices of the engine (empty = no voice, 341:11665, or not known yet)
    const std::vector<PDFSpeechVoice>& getVoices() const { return m_voices; }

    /// The voices are being looked for: one job, when the tool opens (the engine of the system
    /// starts in seconds the first time); the voice states follow its end
    bool isProbingVoices() const { return m_voicesWatcher != nullptr; }

    /// Sync (341:9657): the list shows the page of its selected item, a click on the page shows
    /// the item in the list
    bool isListToPage() const { return m_isListToPage; }
    bool isPageToList() const { return m_isPageToList; }

    /// The check of a text of a popover of Select (341:10217): \p isValid and the text of the line
    /// ("134 items match · they replace the current selection", "Page 1700 is not in the document
    /// (1–1,592).", "Regular expression is not valid: …")
    struct SelectionCheck
    {
        bool isValid = false;
        QString text;
        std::set<size_t> items;
    };
    SelectionCheck checkSelection(PDFAppAudioBookSelectPopover::Kind kind, const QString& text) const;

    /// The stream is being read (Rebuild); the tests wait for it
    bool isBuildingStream() const;

    /// Starts reading the stream again (the link Rebuild asks first, when there are changes)
    void rebuildStream();

    /// The actions of the toolbar and of the keys
    void readSelected();
    void skipSelected();
    void moveSelectionUp();
    void moveSelectionDown();
    void selectByRectangleMode();

    /// Opens a popover of Select (341:10217)
    void openSelectPopover(PDFAppAudioBookSelectPopover::Kind kind);

    /// Selects the items (the list, the overlay, the panel follow)
    void selectItems(std::set<size_t> items);

    /// The text being edited goes into the stream (one step of Undo): at a pause of the typing,
    /// when the editor loses the focus and before anything else changes the stream
    void commitEditedText();

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// The stream of one job (a plain value of the thread)
    struct StreamJobResult
    {
        quint64 generation = 0;
        PDFDocumentTextFlow flow;
        QStringList readSpecial;
    };

    /// The job of the thread (QtConcurrent::run): the reading stream of the parameters
    static StreamJobResult createStreamJob(quint64 generation, PDFDocumentPointer document, QVariantMap parameters, std::shared_ptr<PDFOCRCancelToken> token);

    void startStreamJob(bool isUndoStep);
    void onStreamJobFinished();
    void cancelStreamJob();

    /// The edited stream into the parameter model: one step of Undo (or a following value)
    void storeStream(bool isUndoStep);

    /// The editor of the value of the model (Undo, Redo), when it is not the one just stored
    void loadStreamFromModel();

    /// Starts the job of the voices (once); onVoicesProbed takes its result
    void ensureVoices();

    /// The voices of \p factory in a worker (the job holds its own copy of the factory)
    static std::vector<PDFSpeechVoice> probeVoices(std::shared_ptr<PDFSpeechSynthesizerFactory> factory);
    void onVoicesProbed();

    void updateAll();
    void updateHeader();
    void updateChips();
    void updateCanvas();
    void updateSelectedItem();
    void updateVoiceAndFormat();
    void updateSummary();
    void updateActions();
    void updateVoiceMenu();
    void updateSyncMenu();

    /// The page of the canvas (Sync List to page, the view)
    void showPage(PDFInteger pageIndex);

    void onFilterChipClicked();
    void onListSelectionChanged();
    void applyListSelection();
    void onListActivated(const QModelIndex& index);
    void onListContextMenuRequested(const QPoint& position);
    void onContextMenuTriggered(QAction* action);
    void onOverlayItemClicked(qint64 index, bool isAdding);
    void onOverlayRectangleSelected(QRectF pageRectangle);
    void onOverlayRectangleCanceled();
    void onDisplayedPageChanged(pdf::PDFInteger pageIndex);
    void onTextEdited();
    void onReadCheckBoxToggled(bool isChecked);
    void onRestoreClicked();
    void onRebuildClicked();
    void onRebuildQuestionFinished(int result);
    void onSpeedChanged(int value);
    void onVolumeChanged(int value);
    void onSliderReleased();
    void onPlaySampleClicked();
    void onSampleStateChanged();
    void onSelectMenuRowClicked();
    void onSyncMenuRowClicked();
    void onVoiceMenuRowClicked();
    void onSelectPopoverTextChanged(const QString& text);
    void onSelectPopoverSelected(const QString& text);
    void onSelectPopoverClosed();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);
    void showNoVoiceMessage();
    void onNoVoiceMessageFinished();
    void onOpenSpeechSettings();
    void onDeleteShortcut();
    void onShiftDeleteShortcut();

    /// The kinds of readSpecial of the parameters (the defaults of the items of a new stream)
    QStringList getReadSpecial() const;

    /// "Zira" of the parameter voice "Microsoft Zira" (the short name), "none" without a voice
    QString getVoiceTitle() const;

    /// The estimated length of the read items at the speed of the parameters (ms)
    qint64 getEstimatedDuration() const;

    /// The page of the item, which a run speaks now (the step of its runner), -1 = not known
    PDFInteger getSpokenPageIndex() const;

    /// The text of the sample (the selected item, else the first read item)
    QString getSampleText() const;

    QPointer<PDFAppAudioBookItemModel> m_itemModel;
    QPointer<PDFListView> m_listView;
    QPointer<PDFLabel> m_streamTitle;
    QPointer<PDFLabel> m_streamCount;
    QPointer<PDFTextLink> m_rebuildLink;
    std::vector<QPointer<QPushButton>> m_filterChips;
    QPointer<PDFAppParagraph> m_listHint;
    QPointer<PDFLabel> m_emptyLabel;
    QPointer<PDFAppAudioBookCanvas> m_canvas;
    QPointer<QWidget> m_selectedSection;
    QPointer<PDFTag> m_selectedTag;
    QPointer<PDFTextLink> m_restoreLink;
    QPointer<PDFPlainTextEdit> m_textEditor;
    QPointer<QCheckBox> m_readCheckBox;
    QPointer<QSlider> m_speedSlider;
    QPointer<QSlider> m_volumeSlider;
    QPointer<PDFLabel> m_speedLabel;
    QPointer<PDFLabel> m_volumeLabel;
    QPointer<PDFComboBox> m_formatCombo;
    QPointer<QPushButton> m_playSampleButton;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<QToolButton> m_readButton;
    QPointer<QToolButton> m_skipButton;
    QPointer<QToolButton> m_moveUpButton;
    QPointer<QToolButton> m_moveDownButton;
    QPointer<PDFAppToolPanelPopup> m_selectMenu;
    QPointer<PDFAppToolPanelPopup> m_syncMenu;
    QPointer<PDFAppToolEnumMenu> m_orderMenu;
    QPointer<PDFAppToolPanelPopup> m_voiceMenu;
    QPointer<PDFAppAudioBookSelectPopover> m_selectPopover;
    QPointer<PDFMessageBox> m_rebuildQuestion;
    QPointer<PDFMessageBox> m_noVoiceMessage;
    QPointer<QMenu> m_contextMenu;          ///< A child of the list, deleted when it closes
    QPointer<PDFAppSpeechEngine> m_sampleEngine;
    QPointer<PDFAppDocumentView> m_connectedView;
    QPointer<QFutureWatcher<StreamJobResult>> m_streamWatcher;
    QPointer<QFutureWatcher<std::vector<PDFSpeechVoice>>> m_voicesWatcher;  ///< The job of the voices (a child; null = none running)
    QPointer<QTimer> m_commitTimer;
    QPointer<QTimer> m_selectionTimer;      ///< The selection of the view from the event loop (applyListSelection)
    bool m_hasPendingText = false;
    std::shared_ptr<PDFOCRCancelToken> m_streamToken;
    quint64 m_streamGeneration = 0;
    bool m_isStreamJobUndoStep = false;
    /// The value, which this GUI has stored or loaded last: a copy holds its data alive, so its
    /// identity (the address of the data) is never taken by a later value
    PDFDocumentToolAudioBookStream m_storedStream;
    std::vector<PDFSpeechVoice> m_voices;
    bool m_isVoicesKnown = false;
    bool m_isNoVoiceMessageShown = false;
    bool m_isListToPage = true;
    bool m_isPageToList = true;
    bool m_isUpdatingPanel = false;
    QString m_lastOrder;
    PDFInteger m_shownPageIndex = -1;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_AUDIOBOOK_H
