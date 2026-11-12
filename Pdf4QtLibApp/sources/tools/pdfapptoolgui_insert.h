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

#ifndef PDFAPPTOOLGUI_INSERT_H
#define PDFAPPTOOLGUI_INSERT_H

#include "pdfappglobal.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfappimagepagepreview.h"
#include "pdfapppagelayout.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgroupedpagemodel.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolinsertstrip.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfimagepagefactory.h"
#include "pdfwatchedpointer.h"

#include <QImage>
#include <QKeySequence>
#include <QPointer>
#include <QSizeF>
#include <QStringList>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class QAction;
class QButtonGroup;
class QGridLayout;
class QMimeData;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppImagePagePreview;
class PDFAppImageThumbnailCache;
class PDFAppInsertPositionStrip;
class PDFAppParagraph;
class PDFAppPasswordDialog;
class PDFAppSettingControl;
class PDFAppThumbnailScheduler;
class PDFAppToolAnchorField;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolInputFilesPanel;
class PDFAppToolMergePageView;
class PDFAppToolPanelHeader;
class PDFAppToolPanelPopup;
class PDFAppToolParameterModel;
class PDFAppToolSectionHeader;
class PDFAppToolTileSizeMenu;
class PDFComboBox;
class PDFKeyValueList;
class PDFLabel;
class PDFLineEdit;
class PDFPageTileDelegate;
class PDFSegmentedControl;
class PDFTag;
class PDFTileRow;

/// The tools pages.insert and pages.add-image (package C of step 3a of phase 2, plan 9.2); its
/// file defines the elements of the panel builder of package C (addChoiceSwitch,
/// addPageRangeField)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIInsert
{
public:
    PDFAppToolGUIInsert() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerInsertTools();
};

/// The switch of the options of a choice inside of a panel (the source of Insert Pages: All
/// pages / Page range, card 388:16076: the radio buttons 18 high, 8 apart). It is bound to an
/// Enum parameter of the model (PDFAppToolPanelBuilder::addChoiceSwitch: the options of the
/// parameter, a click writes it - one undo step - and the switch follows Undo and Redo), or the
/// tool gives its options and reacts to choiceChanged (Insert: the mode of the selected source
/// is a part of the parameter "sourcePages", no Enum parameter of the core).
///
/// Ownership: the radio buttons and their group are children of the switch. choiceChanged is
/// emitted by a click of the user, last (a slot may delete the switch); setChoice is silent.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolChoiceSwitch : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int ROW_HEIGHT = 18;
    static constexpr int GAP = 8;

    explicit PDFAppToolChoiceSwitch(QWidget* parent);
    virtual ~PDFAppToolChoiceSwitch() override;

    /// Adds an option at the end
    void addOption(const QString& id, const QString& title);

    /// The chosen option (no signal); an unknown id chooses nothing
    void setChoice(const QString& id);
    QString getChoice() const;

    QStringList getOptionIds() const;
    QRadioButton* getButton(const QString& id) const;

    /// Binds the switch to the Enum parameter: its options, its value, its changes
    void bindParameter(PDFAppToolParameterModel* model, const QString& parameter);

Q_SIGNALS:
    /// The user has chosen the option
    void choiceChanged(const QString& id);

private:
    void onButtonClicked();
    void onParameterChanged(const QString& name);

    QPointer<QButtonGroup> m_group;
    QPointer<QVBoxLayout> m_layout;
    std::vector<std::pair<QString, QPointer<QRadioButton>>> m_buttons;
    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
};

/// The field of an ordered page range (K23, 388:16076: the text field 261 x 34, the examples
/// "Examples: 1-5, 7, 10-, odd, even" 8 below it and the line of the result 8 below them -
/// "5 of 24 pages selected" in text/accent, or the reason of an error in text/danger). The text
/// is checked by PDFPageRangeList::parse of the core for the page count (the reason comes from
/// the core: "Page 60 does not exist – the document has 48 pages"); this is not the page
/// selection of tools.md 9.6.
///
/// Bound to a Text parameter (PDFAppToolPanelBuilder::addPageRangeField: the pages of the
/// document of the tool, a valid text is written to the model after the delay of the binder,
/// an error is an input error of the GUI and disables the Primary button), or the tool gives the
/// page count and takes rangeChosen (Insert: a part of "sourcePages" of the selected source).
///
/// Ownership: the field, the labels and the timer are children. errorChanged is emitted at an
/// edit, which changes the validity, rangeChosen with a valid text after the delay or at the end
/// of the editing; both last (a slot may delete the field). setText is silent.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageRangeField : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int FIELD_HEIGHT = 34;
    static constexpr int LINE_HEIGHT = 13;
    static constexpr int GAP = 8;
    static constexpr int WRITE_DELAY = 300;

    explicit PDFAppToolPageRangeField(QWidget* parent);
    virtual ~PDFAppToolPageRangeField() override;

    /// The pages of the document of the range (the text is checked again, no signal)
    void setPageCount(PDFInteger pageCount);
    PDFInteger getPageCount() const { return m_pageCount; }

    /// The text (checked, no signal)
    void setText(const QString& text);
    QString getText() const;

    /// An empty text is a valid range of no page (default false: the field asks for pages)
    void setEmptyAllowed(bool isEmptyAllowed);

    void setExamples(const QString& text);

    /// The reason of an error, empty for a valid text
    const QString& getError() const { return m_error; }

    /// The pages of a valid text in its order (indices from 0)
    const std::vector<PDFInteger>& getPages() const { return m_pages; }

    PDFLineEdit* getField() const { return m_field.data(); }
    PDFLabel* getExamplesLabel() const { return m_examples.data(); }
    PDFLabel* getStatusLabel() const { return m_status.data(); }

    /// Binds the field to the Text parameter of the GUI (the pages of its document)
    void bindParameter(PDFDocumentToolGUI* gui, const QString& parameter);

    /// Writes a valid text, which waits for the delay, now (tests, the end of the editing)
    void flush();

Q_SIGNALS:
    void errorChanged(const QString& error);
    void rangeChosen(const QString& text);

private:
    void validate();
    void updateStatus();
    void onTextEdited(const QString& text);
    void onEditingFinished();
    void onWriteTimer();
    void onParameterChanged(const QString& name);

    QPointer<PDFLineEdit> m_field;
    QPointer<PDFLabel> m_examples;
    QPointer<PDFLabel> m_status;
    QPointer<QTimer> m_timer;
    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
    QString m_error;
    QString m_chosenText;
    std::vector<PDFInteger> m_pages;
    PDFInteger m_pageCount = 0;
    bool m_isEmptyAllowed = false;
};

/// The source cards of the canvas of Insert Pages (388:15487, 388:17789) for the grouped view of
/// package A (PDFAppToolMergePageView through PDFAppToolGroupedPageModel, R1 b): one group per
/// source in the order of the insertion - a PDF with all its pages (the inserted ones selected,
/// the others dimmed, 388:15756), an image with its frames, the blank pages, a source being read
/// or locked without pages and with the callout of the view. The GUI computes the groups and the
/// rows (values); the pictures are the pictures of the inputs of the list (the keys and the
/// sources of PDFAppToolInputList).
///
/// Ownership: a child of the canvas of Insert; the list is held by a QPointer (the GUI owns it).
/// groupsChanged is emitted last after a change of the groups.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertSourceModel : public PDFAppToolGroupedPageModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolGroupedPageModel;

public:
    /// A tile of a source
    struct Row
    {
        QString inputId;            ///< Input of the page, empty = a blank page
        int pageIndex = -1;         ///< Page of the input (a frame of an image)
        QString label;              ///< "1", "New page 13"
        QString caption;            ///< "blank · A4 portrait", empty = none
        QSizeF pageSize;            ///< Size of the page in points, invalid = unknown
        PDFComponentPainter::PageTileState state = PDFComponentPainter::PageTileState::Normal;
        bool isDimmed = false;      ///< The page is not inserted (388:15756: muted)

        bool operator==(const Row&) const = default;
    };

    explicit PDFAppInsertSourceModel(QObject* parent);
    virtual ~PDFAppInsertSourceModel() override;

    void setInputList(PDFAppToolInputList* list);

    /// The groups and the rows (a reset, when the rows differ; groupsChanged last)
    void setContent(std::vector<Group> groups, std::vector<Row> rows);

    /// The row (nullptr for an invalid row)
    const Row* getRow(int row) const;

    virtual const std::vector<Group>& getGroups() const override { return m_groups; }
    virtual int getGroupOfRow(int row) const override;
    virtual bool isRowSelected(int row) const override;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual QString getKey(int pageIndex) const override;
    virtual bool hasPictureSources() const override;
    virtual PDFAppThumbnailSource getPictureSource(int row) const override;

private:
    QPointer<PDFAppToolInputList> m_list;
    std::vector<Group> m_groups;
    std::vector<Row> m_rows;
};

/// The drop zone in the place of the stage without a source (Insert 398:29210: 540 wide,
/// bg/surface with the dashed border/strong, radius 16, padding 28 / 24; the icon tile 44, the
/// title Semi Bold 16, the text Regular 12, the buttons, "Open documents" with a row of each
/// ready document with its Tag "unsaved changes" and Add; Add Image 388:19253 without the
/// documents). Built again by the GUI when the open documents change; it only says what was
/// chosen (the signals are emitted last, a slot may delete it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertEmptyState : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int CARD_WIDTH = 540;
    static constexpr int RADIUS = 16;
    static constexpr int PADDING_TOP = 29;      ///< Padding 28 + the border
    static constexpr int PADDING_SIDE = 29;
    static constexpr int PADDING_BOTTOM = 25;   ///< Padding 24 + the border
    static constexpr int GAP = 12;
    static constexpr int ICON_TILE = 44;
    static constexpr int ROW_HEIGHT = PDFAppToolOpenDocumentRow::HEIGHT;

    /// An open document, which can be added ("3 pages · the current unsaved state is used")
    using Document = PDFAppToolOpenDocumentRow::Document;

    /// What the zone shows
    struct Content
    {
        QString iconName;
        QString title;
        QString text;
        QString primaryText;
        QString primaryIconName;
        QString secondaryText;      ///< Empty = no second button
        QString secondaryIconName;
        std::vector<Document> documents;
    };

    explicit PDFAppInsertEmptyState(const Content& content, QWidget* parent);
    virtual ~PDFAppInsertEmptyState() override;

    QWidget* getCard() const { return m_card.data(); }
    QPushButton* getPrimaryButton() const { return m_primaryButton.data(); }
    QPushButton* getSecondaryButton() const { return m_secondaryButton.data(); }
    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getTextLabel() const { return m_text.data(); }
    std::vector<QPushButton*> getAddButtons() const;

Q_SIGNALS:
    void primaryRequested();
    void secondaryRequested();
    void addDocumentRequested(const QString& documentId);

private:
    QPointer<QWidget> m_card;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_text;
    QPointer<QPushButton> m_primaryButton;
    QPointer<QPushButton> m_secondaryButton;
    std::vector<QPointer<QPushButton>> m_addButtons;
};

/// The canvas of Insert Pages (document-canvas 388:15520): bg/track, the padding 20, the bar of
/// the preview (preview-bar 388:15744: "Pages to insert · 6 pages from 2 files" Semi Bold 13, 36
/// high), 12 below it the source cards in the grouped view of package A (stage 388:15746) and the
/// hint 10 below them (388:16070, Regular 11 text/tertiary; the view hugs its cards). Without a
/// source the drop zone takes the stage (398:29209, no bar). A click on a tile or on a card of a
/// source emits pageClicked from the event loop (Qt uses the view after its mouse events); a
/// button of a card (Enter Password…, Remove File of 388:17789) emits groupActionTriggered from
/// the event loop.
///
/// Ownership: the canvas owns the view, the model, the delegate and the scheduler; the GUI holds
/// the canvas by a QPointer (the view of the tool owns it during the mode).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int HINT_GAP = 10;

    /// Slot of a page of the size Small (388:15758: the box 73 x 100)
    static constexpr QSize SMALL_SLOT = QSize(73, 100);

    explicit PDFAppInsertCanvas(QWidget* parent);
    virtual ~PDFAppInsertCanvas() override;

    PDFAppInsertSourceModel* getModel() const { return m_model.data(); }
    PDFAppToolMergePageView* getPageView() const { return m_pageView.data(); }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler.data(); }
    PDFPageTileDelegate* getDelegate() const { return m_delegate.data(); }
    PDFLabel* getHeaderLabel() const { return m_header.data(); }
    PDFAppParagraph* getHintLabel() const { return m_hint.data(); }

    void setHeaderText(const QString& text);
    void setHintText(const QString& text);

    /// The empty state in the place of the stage (nullptr: the sources); the canvas takes it over
    void setEmptyState(QWidget* emptyState);
    QWidget* getEmptyState() const { return m_emptyState.data(); }

    /// The size of the tiles: the slot of Small scaled (PDFAppToolPageGrid::getTileScale)
    void setTileSize(PDFAppToolPageGrid::TileSize size);

    /// The tiles can be clicked (false while the tool runs)
    void setEditable(bool isEditable);

Q_SIGNALS:
    /// A tile (\p row >= 0) or the card of the group \p group was clicked
    void pageClicked(int row, int group);

    /// A button of the card of the source was clicked ("password", "remove")
    void groupActionTriggered(const QString& inputId, const QString& actionId);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePictureSize();
    void emitQueuedClick();
    void onContentHeightChanged(int height);

    QPointer<PDFAppInsertSourceModel> m_model;
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<QWidget> m_bar;
    QPointer<PDFLabel> m_header;
    QPointer<QWidget> m_stage;
    QPointer<PDFAppToolMergePageView> m_pageView;
    PDFWatchedPointer<QWidget> m_pageViewport;      ///< The viewport of m_pageView (filtered): the event filter compares only its address
    QPointer<PDFAppParagraph> m_hint;
    QPointer<QWidget> m_emptyState;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Medium;
    QPoint m_pressPosition;
    int m_queuedRow = -1;
    int m_queuedGroup = -1;
    bool m_isClickQueued = false;
    bool m_isEditable = true;
};

/// A new page of Add Image as Page in its canvas (388:18837: the page with its image laid out by
/// PDFAppImagePagePreview of package D, "New page 13" Medium 11 and "site_photo.jpg · A4
/// landscape" Regular 10 below it; the card of the selected image bg/accent-subtle with its
/// border). A click emits clicked (from the release, last).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAddImagePageCard : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 6;
    static constexpr int CAPTION_GAP = 6;
    static constexpr int RADIUS = 8;

    explicit PDFAppAddImagePageCard(const QString& inputId, QWidget* parent);
    virtual ~PDFAppAddImagePageCard() override;

    const QString& getInputId() const { return m_inputId; }
    PDFAppImagePagePreview* getPreview() const { return m_preview.data(); }
    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getCaptionLabel() const { return m_caption.data(); }

    void setTexts(const QString& title, const QString& caption);
    void setSelected(bool isSelected);
    bool isSelected() const { return m_isSelected; }

    /// The size of the preview of the page (Fit: \p pageHeight high by the ratio of the page)
    void setPageHeight(int pageHeight);

Q_SIGNALS:
    void clicked(const QString& inputId);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString m_inputId;
    QPointer<PDFAppImagePagePreview> m_preview;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_caption;
    bool m_isSelected = false;
};

/// The canvas of Add Image as Page (document-canvas 388:18837): bg/track, the padding 20, the
/// bar (preview-bar: "New pages · 2" Semi Bold 13, the segments Fit / 100 % 120 x 32), the new
/// pages in a flow 20 apart (PDFAppAddImagePageCard), the hint below them ("Orientation follows
/// each image…", Regular 11 text/secondary); without an image the drop zone of 388:19253.
///
/// Ownership: the canvas owns the cards (made again when the images change); the GUI holds the
/// canvas by a QPointer. zoomChanged and cardClicked are emitted last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAddImageCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int CARD_GAP = 20;
    static constexpr int HINT_GAP = 10;
    static constexpr int SEGMENT_WIDTH = 60;
    static constexpr int SEGMENT_HEIGHT = 32;

    /// Height of a page in the view Fit (388:18837: the landscape page 330 high)
    static constexpr int FIT_PAGE_HEIGHT = 330;

    explicit PDFAppAddImageCanvas(QWidget* parent);
    virtual ~PDFAppAddImageCanvas() override;

    PDFLabel* getHeaderLabel() const { return m_header.data(); }
    PDFSegmentedControl* getZoomSwitch() const { return m_zoomSwitch.data(); }
    PDFAppParagraph* getHintLabel() const { return m_hint.data(); }
    QScrollArea* getScrollArea() const { return m_scrollArea.data(); }

    void setHeaderText(const QString& text);
    void setHintText(const QString& text);

    /// The cards of the new pages (the canvas takes them over; the old ones are deleted later)
    void setCards(const std::vector<PDFAppAddImagePageCard*>& cards);
    std::vector<PDFAppAddImagePageCard*> getCards() const;

    /// Fit (true) or 100 % (false); no signal
    void setFit(bool isFit);
    bool isFit() const { return m_isFit; }

    void setEmptyState(QWidget* emptyState);
    QWidget* getEmptyState() const { return m_emptyState.data(); }

    void retranslate();

Q_SIGNALS:
    void zoomChanged(bool isFit);
    void cardClicked(const QString& inputId);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onZoomSwitchChanged(int index);
    void onCardClicked(const QString& inputId);
    void layoutCards();

    QPointer<QWidget> m_bar;
    QPointer<PDFLabel> m_header;
    QPointer<PDFSegmentedControl> m_zoomSwitch;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_flow;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<QWidget> m_emptyState;
    std::vector<QPointer<PDFAppAddImagePageCard>> m_cards;
    bool m_isFit = true;
};

/// The shared GUI of Insert Pages and Add Image as Page (K57: Add Image is a preset of Insert -
/// one list of the sources, one strip of the position, one layout; plan of step 3, 2.4, 2.5, 4.1,
/// 4.3, K7, K8, K9, K24, K35, K36, K37, K53):
///  - the sources: a PDFAppToolInputList owned by this GUI (it lives for the whole session of the
///    tool, the parts made again after a change of the language hold it by a QPointer); the
///    parameter "inputs" holds its ids in their order (with "blank" for the blank pages of
///    Insert), one more parameter holds a value of each source in the same order ("sourcePages"
///    of Insert, "turns" of Add Image). A change of the user is one undo step of both; Undo and
///    Redo set the inputs of the list (a removed input is parked, R12 of step 2);
///  - the run gets the usable inputs only (prepareEnvironment, getRunParameters);
///  - the left panel: "Insert position" with the strip (PDFAppInsertPositionStrip) and its hint;
///    a click, a key or a drop of files on the strip sets the place (position, page);
///  - the toolbar: Add (the menu of the tool), Position (Before page N, After page N - N is the
///    current page -, At the beginning, At the end, Replace selected pages of Insert, K36),
///    the keys of the frames on the key widget of the host, a drop of files anywhere in the tab;
///  - a source, which needs a password: the password dialog of Merge (new + QPointer).
///
/// Ownership (checklist point 1): the shell owns this GUI; this GUI owns the list (a child QObject)
/// and the parts are owned by the shell (held here by QPointer, made again after a change of the
/// language). The password dialog is new + QPointer (WA_DeleteOnClose). The actions and the drop
/// filter of the key widget live while this GUI does (the destructor takes them back).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertToolGUIBase : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Widths of the panels of the toolbar (388:16725, 388:16769)
    static constexpr int ADD_PANEL_WIDTH = 290;
    static constexpr int POSITION_PANEL_WIDTH = 300;

    /// Interval of the polling of the reading of the sources (ms)
    static constexpr int PROGRESS_INTERVAL = 100;

    explicit PDFAppInsertToolGUIBase(QObject* parent = nullptr);
    virtual ~PDFAppInsertToolGUIBase() override;

    // ------------------------------------------------------------ parts and flow

    virtual void setContentEnabled(bool isEnabled) override;
    virtual PDFAppToolInputRules getInputRules() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;
    virtual void retranslate() override;
    virtual bool isConfirmEnabled() const override;

    // ------------------------------------------------------------ the place

    /// The place of the insertion: the gap of the strip (-1 for Replace)
    int getGap() const;

    /// The replaced pages (indices from 0, sorted; empty unless the position is Replace)
    std::vector<PDFInteger> getReplacedPages() const;

    /// Moves the place to the gap (one undo step): 0 = At the beginning, the page count = At the
    /// end, else After page \p gap
    void setGap(int gap);

    /// The rows of the menu Position: before, after (the current page), start, end, replace
    void choosePosition(const QString& positionId);

    /// The text of the place: "After page 12", "At the beginning", "Replace 13–14" (Summary)
    QString getPositionValueText() const;

    /// Pages of the document of the tab, pages of the document after the run
    PDFInteger getDocumentPageCount() const;
    PDFInteger getResultPageCount() const;

    /// Index (from 0) of the first new page in the changed document, as the core places it
    PDFInteger getInsertionIndex() const;

    /// The page, whose size "Same as page N" takes (index from 0, PDFDocumentToolInsertBase::Placement)
    PDFInteger getNeighborPage() const;

    /// Pages, which the run inserts (the usable sources)
    virtual PDFInteger getInsertedPageCount() const = 0;

    // ------------------------------------------------------------ the sources

    PDFAppToolInputList* getSourceList() const { return m_list.data(); }

    /// The source shown in the right panel ("blank" for the blank pages)
    const QString& getCurrentSource() const { return m_currentInput; }
    void setCurrentSource(const QString& inputId);

    /// Adds the files at the end (\p gap >= 0: and moves the place there), one undo step;
    /// refused ones are said in the status bar
    void addFiles(const QStringList& filePaths, int gap = -1, const QStringList& refusedTexts = QStringList());
    void addFilesFromDialog();
    void pasteImage();
    void addOpenDocument(const QString& documentId);
    void removeInput(const QString& inputId);

    /// Moves the source in front of the source at \p targetIndex of "inputs" (the count = to
    /// the end), one undo step
    void moveInput(const QString& inputId, int targetIndex);

    /// Removes the current source (the button of the toolbar)
    void removeCurrentSource();

    /// The password dialog of the input (388:17789, the dialog of Merge)
    void openPasswordDialog(const QString& inputId);

    /// A drop of files anywhere in the tab; \returns true for files
    bool dropFiles(const QMimeData* mimeData);

    // ------------------------------------------------------------ parts (tests)

    PDFAppInsertPositionStrip* getStrip() const { return m_strip.data(); }
    PDFAppToolPanelHeader* getPositionHeader() const { return m_positionHeader.data(); }
    PDFAppParagraph* getStripHint() const { return m_stripHint.data(); }
    QToolButton* getAction(const QString& name) const;
    PDFAppToolPanelPopup* getPanel(const QString& name) const;
    PDFTileRow* getPanelRow(const QString& name) const;
    PDFAppPasswordDialog* getPasswordDialog() const { return m_passwordDialog.data(); }
    PDFAppToolCallout* getAttentionCallout() const { return m_attentionCallout.data(); }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

    /// The rows of the menu Add (the panel exists); the actions of the toolbar after Position
    virtual void describeAddMenu(PDFAppToolPanelPopup* panel) = 0;
    virtual void describeActionsBeforePosition(PDFAppToolPanelBuilder& builder);
    virtual void describeActionsAfterPosition(PDFAppToolPanelBuilder& builder);

    /// The parts above the strip in the left panel (Add Image: Images)
    virtual void describeLeftPanelTop(PDFAppToolPanelBuilder& builder);

    /// Texts of the toolbar and of the menus
    virtual QString getAddButtonText() const = 0;
    virtual QString getAddButtonToolTip() const = 0;
    virtual bool hasReplace() const = 0;
    virtual bool hasPositionDescriptions() const = 0;

    /// The parameter, which holds a value of each source in the order of "inputs" ("sourcePages"
    /// separated by ";", "turns" separated by ","), and its separator
    virtual QString getSourceValuesParameter() const = 0;
    virtual QChar getSourceValuesSeparator() const = 0;

    /// The parts follow the sources and the values (after a change of the model or of the list)
    virtual void updateParts() = 0;

    /// The ids of "inputs" in their order (with "blank")
    QStringList getModelInputIds() const;

    /// The values of the sources of "inputs" by their ids (blank has none)
    std::map<QString, QString> getSourceValues() const;

    /// Writes the inputs and the values of the sources by their ids as one undo step (\p extra:
    /// further values of the same step)
    void applySources(const QStringList& inputIds, const std::map<QString, QString>& values, const QVariantMap& extra = QVariantMap());

    /// The usable inputs of the run in their order (with "blank", when it has pages)
    virtual QStringList getRunInputIds() const;

    /// The input is usable (the list), "blank" with pages is usable for Insert
    bool isUsableInput(const QString& inputId) const;

    /// The tool can be changed (no run, no result dialog)
    bool isEditable() const { return !m_isRunning && !m_isDeciding; }

    /// The ready documents of the tabs of this window
    std::vector<PDFAppDocument*> getOpenAppDocuments() const;
    std::vector<PDFAppInsertEmptyState::Document> getOpenDocuments() const;

    /// The rows of the open documents in the panel Add ("Open documents")
    void updateOpenDocumentRows(PDFAppToolPanelPopup* panel);

    /// Text of the place for the status bar ("after page 12", "replace pages 13–14")
    QString getPlaceClause() const;

    /// The panel closes first (it may run code of the application), then the slot runs
    void closePanel(const QString& name);

    QString getParameter(const char* name) const;
    QString getDocumentTitle() const;

    /// Updates the strip, the toolbar and the texts of the shell (the last call)
    void updateCommon();

    /// The parts of the toolbar (updateCommon)
    virtual void updateToolbar();

    /// The shell says the Primary button and the status, the runner gets the generation of the
    /// inputs and computes the preview again (the list is no list of the host)
    void announceSources();

    std::map<QString, QPointer<QToolButton>> m_actions;
    std::map<QString, QPointer<PDFAppToolPanelPopup>> m_panels;
    std::map<QString, QPointer<PDFTileRow>> m_panelRows;
    std::vector<QPointer<PDFTileRow>> m_openDocumentRows;
    QPointer<PDFLabel> m_openDocumentsCaption;
    QPointer<PDFAppToolCallout> m_attentionCallout;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<QWidget> m_keyWidget;
    std::vector<QPointer<QAction>> m_keyActions;
    QString m_currentInput;
    bool m_isRunning = false;
    bool m_isDeciding = false;

    /// A key of the frames on the key widget: the sequence and the slot
    struct KeyAction
    {
        QString name;
        QKeySequence keys;
        void (PDFAppInsertToolGUIBase::*slot)() = nullptr;
    };
    virtual std::vector<KeyAction> getKeyActions() const = 0;

    /// Slots of the keys and of the rows of the menus
    void onAddFilesRowClicked();
    void onPasteRowClicked();
    void onOpenDocumentRowClicked();
    void onEmptyStateAddFiles();
    void onEmptyStateAddDocument(const QString& documentId);

    /// The list, the shell, the host and the registry are connected once; the first values
    void connectList();

private:
    void initializeValues();
    void readValues();
    void installKeys();
    void addResults(const std::vector<PDFAppToolInputList::AddResult>& results, int gap, const QStringList& refusedTexts);
    QVariantMap createGapValues(int gap) const;

    void onListInputsChanged();
    void onListInputChanged(const QString& inputId);
    void onReadQueued();
    void onProgressTimer();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);
    void onHostPagesChanged();
    void onRegistryChanged();
    void onStripGapChosen(int gap);
    void onStripFilesDropped(int gap, const QStringList& filePaths, const QStringList& refusedTexts);
    void onPositionRowClicked();
    void onPasswordRemoveRequested(const QString& inputId);
    void updatePositionRows();
    void updateStrip();

    QPointer<PDFAppToolInputList> m_list;
    QPointer<PDFAppInsertPositionStrip> m_strip;
    QPointer<PDFAppToolPanelHeader> m_positionHeader;
    QPointer<PDFAppParagraph> m_stripHint;
    QPointer<PDFAppPasswordDialog> m_passwordDialog;
    QPointer<QTimer> m_progressTimer;
    bool m_wasDropAccepted = false;
    bool m_isValuesInitialized = false;
    bool m_isChangingList = false;
    bool m_isReadQueued = false;
    bool m_isShellConnected = false;
    bool m_isHostConnected = false;
    bool m_isRegistryConnected = false;
};

/// Insert Pages (pages.insert; Figma 388:15487, 388:16103, 388:16811, 388:17167, 388:17789,
/// 388:18134, 398:28602): PDF files, open documents (their snapshot, K24), images (K35: the
/// layout of Add Image) and blank pages at a place of the document of the tab.
///  - the toolbar: Add (PDF files… Ctrl+I, Images… Ctrl+Alt+I, Blank pages Ctrl+Shift+I, Paste
///    image Ctrl+V, Open documents), Position, the removal of the selected source, Thumbnails:
///    Small (the size of the tiles of the source cards);
///  - the canvas: the source cards (PDFAppInsertCanvas, the grouped view); a click on a page of a
///    PDF chooses it (Page range), a click on a card selects its source; a source, which cannot
///    be read, has Enter Password… / Remove File in its card (388:17789);
///  - the right panel: the callout of the sources, which need attention, the section of the
///    selected source (All pages / Page range with the field of the range, the blank pages:
///    Count, Size "As page N" and its presets, Orientation) and Summary (Insert, Position,
///    Document "48 → 54 pages").
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertToolGUI : public PDFAppInsertToolGUIBase
{
    Q_OBJECT

private:
    using BaseClass = PDFAppInsertToolGUIBase;

public:
    explicit PDFAppInsertToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppInsertToolGUI() override;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual PDFInteger getInsertedPageCount() const override;

    /// Blank pages (Ctrl+Shift+I): one blank page more, the source of the blank pages selected
    void addBlankPages();

    /// The mode of the current PDF source: All pages (true) or Page range
    void setAllPages(bool isAllPages);

    /// The range of the current PDF source (one undo step; an invalid one is not written)
    void setSourceRange(const QString& text);

    /// A click on a page of a source in the canvas: the source is selected, a page of a PDF is
    /// added to its range or taken away (All pages becomes the range of the page)
    void clickSourcePage(int row, int group);

    /// The size of the tiles of the source cards (the menu Thumbnails)
    void setTileSize(PDFAppToolPageGrid::TileSize size);

    /// The pages of a source, which the run inserts (a usable PDF: its range; an image: its
    /// frames; "blank": the count); std::nullopt for an invalid range
    std::optional<std::vector<PDFInteger>> getSourcePages(const QString& inputId) const;

    // Parts (tests)
    PDFAppInsertCanvas* getCanvas() const { return m_canvas.data(); }
    QWidget* getSourceSection() const { return m_sourceSection.data(); }
    PDFLabel* getSourceTitle() const { return m_sourceTitle.data(); }
    PDFTag* getSourceTag() const { return m_sourceTag.data(); }
    PDFAppToolChoiceSwitch* getChoiceSwitch() const { return m_choiceSwitch.data(); }
    PDFAppToolPageRangeField* getRangeField() const { return m_rangeField.data(); }
    QWidget* getBlankSection() const { return m_blankSection.data(); }
    PDFComboBox* getBlankSizeCombo() const { return m_blankSizeCombo.data(); }
    PDFLabel* getBlankNote() const { return m_blankNote.data(); }
    PDFAppToolTileSizeMenu* getTileSizeMenu() const { return m_tileSizeMenu.data(); }
    PDFAppInsertEmptyState* getEmptyState() const;

protected:
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeAddMenu(PDFAppToolPanelPopup* panel) override;
    virtual void describeActionsAfterPosition(PDFAppToolPanelBuilder& builder) override;
    virtual QString getAddButtonText() const override;
    virtual QString getAddButtonToolTip() const override;
    virtual bool hasReplace() const override { return true; }
    virtual bool hasPositionDescriptions() const override { return true; }
    virtual QString getSourceValuesParameter() const override;
    virtual QChar getSourceValuesSeparator() const override;
    virtual void updateParts() override;
    virtual void updateToolbar() override;
    virtual QStringList getRunInputIds() const override;
    virtual std::vector<KeyAction> getKeyActions() const override;

private:
    void updateCanvas();
    void updateRightPanel();
    void updateEmptyState();
    QString getHeaderText() const;
    PDFInteger getBlankCount() const;
    QSizeF getBlankPageSize() const;

    /// The orientation of the neighboring page as it is shown (ORIENTATION_*): "As page N" starts
    /// in it, when the blank pages are added or the size is chosen (388:16811)
    QString getNeighborOrientation() const;
    void onChoiceChanged(const QString& id);
    void onRangeChosen(const QString& text);
    void onRangeErrorChanged(const QString& error);
    void onCanvasPageClicked(int row, int group);
    void onBlankRowClicked();
    void onBlankSizeActivated(int index);
    void onCanvasActionTriggered(const QString& inputId, const QString& actionId);
    void onTileSizeChosen(pdf::PDFAppToolPageGrid::TileSize size);
    void onEmptyStateBlankPages();

    QPointer<PDFAppInsertCanvas> m_canvas;
    QPointer<QWidget> m_sourceSection;
    QPointer<PDFLabel> m_sourceTitle;
    QPointer<PDFTag> m_sourceTag;
    QPointer<PDFAppToolChoiceSwitch> m_choiceSwitch;
    QPointer<PDFAppToolPageRangeField> m_rangeField;
    QPointer<QWidget> m_blankSection;
    QPointer<PDFComboBox> m_blankSizeCombo;
    QPointer<QWidget> m_blankOrientationControl;
    QPointer<PDFLabel> m_blankNote;
    QString m_rangeSource;      ///< The source, whose range the field shows
    QPointer<PDFAppToolTileSizeMenu> m_tileSizeMenu;
    std::map<QString, QString> m_typedRanges;   ///< The last range typed per source (All pages keeps it for Page range)
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Medium;
    bool m_isTileSizeRead = false;
};

/// Add Image as Page (pages.add-image; Figma 388:18837, 388:19253, 388:19607, 388:20121): images
/// (each frame of a multi-page TIFF) as new pages at a place of the document of the tab, laid out
/// by PDFImagePageLayout (5.8).
///  - the toolbar: Add Images (Image files… Ctrl+Alt+I, Paste from clipboard Ctrl+V), Rotate Left /
///    Right and Remove of the selected image, Position (no Replace, no descriptions, K36), Page
///    (Same as page N, Fit to image, A4, A3, Letter, Custom);
///  - the left panel: Images (the rows of the list: pixels, size, "New page 13 · A4 landscape"),
///    then the strip of the position;
///  - the canvas: the new pages with their images (PDFAppImagePagePreview of package D), Fit /
///    100 %;
///  - the right panel: Page layout (Width and Height of Custom, Margins, Image, Anchor - the
///    anchor field of package B), Summary (New pages, Position, Page size, Document), the note.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAddImageToolGUI : public PDFAppInsertToolGUIBase
{
    Q_OBJECT

private:
    using BaseClass = PDFAppInsertToolGUIBase;

public:
    /// Longest side of a decoded preview of an image (pixels)
    static constexpr int MAXIMUM_PREVIEW_SIDE = 800;

    /// Width of the panel Page (388:19607)
    static constexpr int PAGE_PANEL_WIDTH = 280;

    explicit PDFAppAddImageToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppAddImageToolGUI() override;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual PDFAppToolInputRules getInputRules() const override;
    virtual PDFInteger getInsertedPageCount() const override;

    /// Turns the selected image (one undo step)
    void rotateLeft();
    void rotateRight();

    /// Removes the selected image
    void removeImage();

    /// Fit / 100 % of the canvas
    void setFit(bool isFit);

    /// The layout of the image of the input as the core lays it out
    PDFImagePageLayout getImageLayout(const QString& inputId) const;

    /// The size of the new page of the input in points
    QSizeF getImagePageSize(const QString& inputId) const;

    /// "A4, as page 12", "Fit to image", "A3", "120 × 80 mm"
    QString getPageSizeValueText() const;

    // Parts (tests)
    PDFAppAddImageCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppToolInputFilesPanel* getImagesPanel() const { return m_imagesPanel.data(); }
    PDFAppImageThumbnailCache* getThumbnailCache() const;
    QWidget* getSizeFields() const { return m_sizeFields.data(); }
    PDFAppToolAnchorField* getAnchorField() const;
    PDFAppParagraph* getNote() const { return m_note.data(); }
    PDFAppInsertEmptyState* getEmptyState() const;

protected:
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanelTop(PDFAppToolPanelBuilder& builder) override;
    virtual void describeAddMenu(PDFAppToolPanelPopup* panel) override;
    virtual void describeActionsBeforePosition(PDFAppToolPanelBuilder& builder) override;
    virtual void describeActionsAfterPosition(PDFAppToolPanelBuilder& builder) override;
    virtual QString getAddButtonText() const override;
    virtual QString getAddButtonToolTip() const override;
    virtual bool hasReplace() const override { return false; }
    virtual bool hasPositionDescriptions() const override { return false; }
    virtual QString getSourceValuesParameter() const override;
    virtual QChar getSourceValuesSeparator() const override;
    virtual void updateParts() override;
    virtual void updateToolbar() override;
    virtual std::vector<KeyAction> getKeyActions() const override;

private:
    void turnCurrent(bool isClockwise);
    PageRotation getTurn(const QString& inputId) const;
    QSizeF getNeighborPageSize() const;
    QImage getPreviewImage(const PDFAppToolInput& input);
    void updateImages();
    void updateCanvas();
    void updateRightPanel();
    void updateEmptyState();
    void updatePageRows();
    void onImageRowClicked(const QString& inputId);
    void onImageLinkActivated(const QString& inputId);
    void onCardClicked(const QString& inputId);
    void onZoomChanged(bool isFit);
    void onPageRowClicked();
    void onThumbnailDecoded();

    QPointer<PDFAppAddImageCanvas> m_canvas;
    QPointer<PDFAppToolInputFilesPanel> m_imagesPanel;
    QPointer<PDFAppImageThumbnailCache> m_thumbnailCache;      ///< The thumbnails of the rows (M10, of Images to PDF; a child)
    QPointer<QWidget> m_sizeFields;
    QPointer<PDFAppToolAnchorField> m_anchorField;
    QPointer<PDFAppParagraph> m_note;
    std::map<QString, std::pair<quint64, QImage>> m_previews;   ///< Decoded previews by the input id with its content generation
    bool m_isFit = true;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_INSERT_H
