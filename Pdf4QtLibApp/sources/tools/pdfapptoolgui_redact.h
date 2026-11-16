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

#ifndef PDFAPPTOOLGUI_REDACT_H
#define PDFAPPTOOLGUI_REDACT_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolredactsearchpart.h"

#include "pdfcomponentdelegates.h"
#include "pdfdocument.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfobject.h"
#include "pdftextlayout.h"

#include <QAbstractListModel>
#include <QColor>
#include <QCursor>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QRectF>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class QPushButton;
class QToolButton;

namespace pdf
{

class PDFAppDocumentView;
class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolFlagsMenu;
class PDFAppToolPanelHeader;
class PDFColorSwatchRow;
class PDFDocument;
class PDFDrawWidgetProxy;
class PDFKeyValueList;
class PDFLabel;
class PDFListView;
class PDFMessageBox;
class PDFPageSelection;
class PDFSegmentedControl;
class PDFTileRow;
class PDFWidget;

/// A mark of Redact Content: a Redact annotation of the document (ISO 32000-2 12.5.6.23). The marks
/// of the tool are written with /Subj of their kind (SUBJECT_*); a Redact annotation of another
/// program is a mark too (a whole page, when it covers the crop box, otherwise an area).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactMark
{
public:
    enum class Kind
    {
        Area,   ///< A rectangle drawn on the page (Area)
        Text,   ///< The quadrilaterals of a text (Text, a match of Search…)
        Page    ///< The crop box of a page (Pages…)
    };

    /// /Subj of the marks of the tool (English: a value of the document, as Acrobat writes "Redact")
    static constexpr const char* SUBJECT_AREA = "Redact area";
    static constexpr const char* SUBJECT_TEXT = "Redact text";
    static constexpr const char* SUBJECT_PAGE = "Redact page";

    PDFObjectReference annotation;
    PDFInteger pageIndex = -1;
    Kind kind = Kind::Area;
    QRectF rectangle;           ///< /Rect in the user space of the page
    QPolygonF quadrilaterals;   ///< /QuadPoints (4 points of each quadrilateral, the order of PDF), empty = the rectangle
    QString description;        ///< The text under a Text mark (filled by the GUI), empty otherwise

    /// The area of the mark in the user space of the page: the quadrilaterals united with
    /// Qt::WindingFill (overlapping quadrilaterals of a text of more lines, R6-7), else the rectangle
    QPainterPath getPath() const;

    /// The Redact annotations of the pages of the document, in the order of the pages and of /Annots;
    /// at most \p maximumCount (\p isTruncated says, that the document has more)
    static std::vector<PDFAppToolRedactMark> readMarks(const PDFDocument* document, size_t maximumCount, bool* isTruncated);
};

/// Rows of the marks of the left panel (437:10393: the captions "Page 12 · 4 areas" and the rows of the
/// marks - Text / Area / Whole page with their descriptions and the × of the row), drawn by
/// PDFAppToolRedactMarkDelegate. The rows of the selected mark are the selection of the view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactMarkModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    explicit PDFAppToolRedactMarkModel(QObject* parent);
    virtual ~PDFAppToolRedactMarkModel() override;

    /// Sets the marks (sorted by their pages); the rows are made again
    void setMarks(std::vector<PDFAppToolRedactMark> marks);
    const std::vector<PDFAppToolRedactMark>& getMarks() const { return m_marks; }

    /// The mark of a row, nullptr for a caption or an invalid row
    const PDFAppToolRedactMark* getMark(int row) const;

    /// The row of a mark, -1 = none
    int getRow(PDFObjectReference annotation) const;

    /// The selected mark: its row has the accent tile (437:10559)
    void setSelectedMark(PDFObjectReference annotation);

    /// Title and description of a row ("Text" / “Revenue grew by 18 % year…”, "Area" / "52 × 80 pt",
    /// "Whole page" / "612 × 792 pt")
    static QString getTitle(const PDFAppToolRedactMark& mark);
    static QString getDescription(const PDFAppToolRedactMark& mark);
    static QString getIconName(PDFAppToolRedactMark::Kind kind);

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Row
    {
        PDFInteger pageIndex = -1;  ///< The page of a caption
        int markIndex = -1;         ///< The mark, -1 = the caption of a page
        int pageMarkCount = 0;      ///< The marks of the page of a caption
    };

    std::vector<PDFAppToolRedactMark> m_marks;
    std::vector<Row> m_rows;
    PDFObjectReference m_selectedMark;
};

/// The rows of the marks: PDFTileRowDelegate (ListItem) with the action Remove at the ending icon ×
/// of a row (437:10535) and Delete on the current row
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactMarkDelegate : public PDFTileRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFTileRowDelegate;

public:
    enum RedactActionId
    {
        RemoveAction = LinkAction + 1   ///< The × of the row
    };

    explicit PDFAppToolRedactMarkDelegate(QObject* parent);
    virtual ~PDFAppToolRedactMarkDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// The canvas of Redact Content in the view of the document (437:10393, 437:10982, 437:11317,
/// 437:13219): the bar over the canvas (the caption "Page 308" and the segmented control Marks |
/// Preview result, Preview Disabled without marks and while running), the input of the modes Area (a
/// rectangle dragged on one page) and Text (a selection of the text of the page as Redact Text of the
/// Editor, PDFTextLayout::createTextSelection) on the PDFWidget of the view before its tools
/// (IDrawWidgetInputInterface::ToolPriority - 1), and the drawing over the pages: the rectangle or the
/// selection being dragged, a light fill of the marks with the selected one framed (border/focus 2), the
/// matches of the search, and in Preview result the areas filled by the fill (No fill = white paper, plan
/// 4.5). The marks themselves are the Redact annotations drawn by the view.
///
/// Ownership: the widget is the segmented control, a child of the canvas area, deleted by the shell when
/// the tool closes; the caption is a second child of the canvas area owned by it (deleted in its
/// destructor). It holds the PDFWidget and its proxy by QPointer and unregisters itself from both in its
/// destructor. A finished pick is emitted from the event loop (never from inside of the dispatch of the
/// input of the PDFWidget), Q_EMIT is the last call.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactOverlay : public QWidget, public IDocumentDrawInterface, public IDrawWidgetInputInterface
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Input
    {
        None,
        Area,
        Text
    };

    /// The bar at 20, 20 of the canvas (437:10607), 36 high; the segments 104 wide (870:20145)
    static constexpr int BAR_OFFSET = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int SEGMENT_WIDTH = 104;

    /// The smallest area of a drag (points of the page): a click is no area
    static constexpr qreal MINIMUM_AREA_SIZE = 1.0;

    explicit PDFAppToolRedactOverlay(PDFWidget* pdfWidget, QWidget* canvasArea);
    virtual ~PDFAppToolRedactOverlay() override;

    void setInput(Input input);
    Input getInput() const { return m_input; }

    /// The marks drawn over the pages (their paths), the selected one framed
    void setMarks(const std::vector<PDFAppToolRedactMark>& marks, PDFObjectReference selectedMark);

    /// The matches of the search (the outlines of their quadrilaterals), empty = none
    void setMatches(const std::vector<PDFAppToolRedactSearchPart::Match>& matches);

    /// Preview result: the areas filled by \p fill (No fill = white)
    void setPreview(bool isPreview, const QColor& fill);
    bool isPreview() const { return m_isPreview; }

    /// The segment Preview result can be chosen
    void setPreviewEnabled(bool isEnabled);
    bool isPreviewEnabled() const;

    /// The caption of the shown page
    void setPageText(const QString& text);
    QString getPageText() const;

    PDFSegmentedControl* getModeControl() const { return m_modeControl.data(); }
    PDFLabel* getCaption() const { return m_caption.data(); }

    /// The rectangle being dragged in the mode Area (page space), nullopt = none
    std::optional<QRectF> getDraggedArea() const;

    // IDocumentDrawInterface
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    // IDrawWidgetInputInterface
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;
    virtual int getInputPriority() const override;

Q_SIGNALS:
    /// An area has been dragged on a page (page space, inside of its crop box)
    void areaPicked(pdf::PDFInteger pageIndex, QRectF rectangle);

    /// A text has been selected on a page: its quadrilaterals (page space)
    void textPicked(pdf::PDFInteger pageIndex, QPolygonF quadrilaterals);

    /// The mode Text has found no text on the page (a scanned page, 2.6)
    void textNotFound(pdf::PDFInteger pageIndex);

    /// The segment of the control has been changed by the user
    void previewToggled(bool isPreview);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    struct Pick
    {
        enum class Kind
        {
            None,
            Area,
            Text,
            NoText
        };

        Kind kind = Kind::None;
        PDFInteger pageIndex = -1;
        QRectF rectangle;
        QPolygonF quadrilaterals;
    };

    void retranslate();
    void updatePlacement();
    void updateCanvas();
    void updateCursor(const QPointF& position);
    void finishDrag(const QPointF& position);

    /// Esc during a drag (no modifier): it ends the drag only, nothing is marked
    bool isEscapeOfDrag(const QKeyEvent* event) const;
    void cancelDrag();

    void emitPick();
    void onModeChanged(int index);

    /// The point of the widget in the space of the page (also outside of the page, by its matrix)
    std::optional<QPointF> mapToPage(PDFInteger pageIndex, const QPointF& position) const;
    QRectF getCropBox(PDFInteger pageIndex) const;

    /// The end point of the selection of the text (never on the same line as the start: the rectangle of
    /// the points has an area)
    QPointF getSelectionEnd() const;

    /// The graphics of a page over everything of the view (drawPostRendering): the marks or the preview,
    /// the selected mark, the matches, the dragged area
    void drawPageGraphics(QPainter* painter, PDFInteger pageIndex, const QTransform& pagePointToDevicePointMatrix) const;

    QPointer<PDFWidget> m_pdfWidget;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QWidget> m_canvasArea;
    QPointer<QWidget> m_drawWidget;
    QPointer<PDFLabel> m_caption;               ///< Child of the canvas area, owned by the overlay
    QPointer<PDFSegmentedControl> m_modeControl;
    Input m_input = Input::None;
    std::optional<QCursor> m_cursor;
    std::map<PDFInteger, std::vector<QPainterPath>> m_markPaths;
    std::map<PDFInteger, QPainterPath> m_selectedPaths;
    std::map<PDFInteger, std::vector<QPolygonF>> m_matchQuadrilaterals;
    bool m_isPreview = false;
    QColor m_previewFill = Qt::black;

    // The drag of the modes Area and Text
    PDFInteger m_dragPage = -1;
    QPointF m_dragStart;
    QPointF m_dragEnd;
    PDFTextSelection m_textSelection;
    Pick m_pick;
    bool m_isPickQueued = false;
    bool m_isUpdatingMode = false;
};

/// The panel of the toolbar menu Fill (437:12794): the rows Black, White, Custom color…, No fill and
/// the group Colors with the swatches of the annotation colors and "+" (K6.5), which Custom color… opens
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRedactFillPanel : public PDFAppToolPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolPanelPopup;

public:
    explicit PDFAppToolRedactFillPanel(PDFAppDocumentView* view, int panelWidth, QWidget* parent);
    virtual ~PDFAppToolRedactFillPanel() override;

    /// Adds the group Colors (a caption and the row of the swatches) at the end of the panel
    PDFColorSwatchRow* addColorGroup(const QString& caption);

    /// Shows or hides the group Colors
    void setColorGroupVisible(bool isVisible);
    bool isColorGroupVisible() const;

    PDFColorSwatchRow* getColorRow() const { return m_colorRow.data(); }

private:
    QPointer<QWidget> m_colorDivider;
    QPointer<PDFLabel> m_colorCaption;
    QPointer<PDFColorSwatchRow> m_colorRow;
};

/// The GUI of Redact Content (plan of step 6 of phase 2, 2.5, 4.5, K6.5, K6.7, K6.9, K6.21, K6.23,
/// R6-15, R6-19, R6-24; package P5; Figma 437:10393 ... 448:17473 with the corrections of P2S6FIGMA).
///
/// The marks are the Redact annotations of the document. While the tool is open, the document is in the
/// mode of the tool (the controller of the annotations and the creation tools of the Editor refuse the
/// changes of the user), so the GUI writes the marks itself: a finished pick of Area or Text, Pages… and
/// Mark N Selected Matches make the annotations by PDFDocumentModifier and commit them as the change of
/// the tool (PDFAppDocument::commit, PDFAppChangeSource::Tool) - one Undo step of the document each, the
/// tab becomes unsaved (tools.md 5.4). The × of a row, Delete, Clear All and Remove Marks of the close
/// question remove them the same way (Clear All and Remove Marks in one step). Undo / Redo of the toolbar
/// act on the parameters of the tool (Fill, Keep) and on these changes of the marks in one order
/// (hasDocumentUndoSteps, PDFAppToolCompositeUndoTarget of the shell), never on other changes.
///
/// The left panel is the list of the marks by pages (PDFAppToolRedactMarkModel) or the search
/// (PDFAppToolRedactSearchPart); the right panel says what the copy will contain; the canvas has the bar
/// Marks | Preview result and the input of the modes (PDFAppToolRedactOverlay). The run takes the marks
/// of the document (useMarks), the fill and the Keep options of the model; search, areas and pages of the
/// core stay empty (the GUI makes marks of them). The result is the FileOnly dialog (_REDACTED, Then
/// continue with, Q6.16).
///
/// R6-19: the search term, the results and the areas are never stored (the parameters search and areas
/// are sensitive and the GUI never writes them; the search lives in the part only and is cleared at the
/// close of the tool).
///
/// Ownership: the shell owns this GUI; the model of the marks and the search part are its children; the
/// panels and their parts are children of the widgets of the shell (QPointer here, made again after a
/// change of the language); the overlay is the shell's (QPointer); the dialogs are new + QPointer with
/// WA_DeleteOnClose, closed by the destructor. Every commit, every dialog and every emission is followed by
/// a check of this GUI (a slot of the document may close the tab).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIRedact : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// The mode of the toolbar (Area, Text, Search… - one at a time)
    enum class Mode
    {
        None,
        Area,
        Text,
        Search
    };

    /// The choice of the menu Fill (437:12794)
    enum class Fill
    {
        Black,
        White,
        Custom,
        None
    };

    /// Widths of the panels Fill (437:12794: 298) and Keep (328), the card of the marks (437:10529: at
    /// most 520 high, padding 6 + the border)
    static constexpr int FILL_PANEL_WIDTH = 300;
    static constexpr int KEEP_PANEL_WIDTH = 330;

    /// Width of the card of the question at the close (448:17473): Back to Redacting | Keep Marks and
    /// Close, Remove Marks keep their gap
    static constexpr int CLOSE_PROMPT_WIDTH = 600;
    static constexpr int LIST_MAXIMUM_HEIGHT = 520;
    static constexpr int LIST_PADDING = 7;

    explicit PDFAppToolGUIRedact(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIRedact() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): PDFDocumentToolRedact
    /// in PDFDocumentToolRegistry and this GUI in PDFDocumentToolGUIRegistry
    static void registerRedactTools();

    // ------------------------------------------------------------- parts (nullptr = none)

    QToolButton* getAreaButton() const { return m_areaButton.data(); }
    QToolButton* getTextButton() const { return m_textButton.data(); }
    QToolButton* getPagesButton() const { return m_pagesButton.data(); }
    QToolButton* getSearchButton() const { return m_searchButton.data(); }
    QToolButton* getFillButton() const;
    QToolButton* getKeepButton() const;
    QToolButton* getClearButton() const { return m_clearButton.data(); }
    PDFAppToolRedactFillPanel* getFillPanel() const { return m_fillPanel.data(); }
    PDFTileRow* getFillRow(Fill fill) const;
    PDFAppToolFlagsMenu* getKeepMenu() const { return m_keepMenu.data(); }
    PDFAppToolPanelHeader* getLeftHeader() const { return m_leftHeader.data(); }
    PDFAppToolRedactMarkModel* getMarkModel() const { return m_markModel.data(); }
    PDFListView* getMarksView() const { return m_marksView.data(); }
    QWidget* getMarksCard() const { return m_marksCard.data(); }
    QWidget* getEmptyCard() const { return m_emptyCard.data(); }
    PDFAppToolCallout* getNoTextCallout() const { return m_noTextCallout.data(); }
    QWidget* getMarksContent() const { return m_marksContent.data(); }
    QWidget* getSearchContent() const { return m_searchContent.data(); }
    PDFAppToolRedactSearchPart* getSearchPart() const { return m_searchPart.data(); }
    PDFAppToolPanelHeader* getRightHeader() const { return m_rightHeader.data(); }
    PDFAppToolCallout* getMarksCallout() const { return m_marksCallout.data(); }
    PDFAppToolCallout* getPagesOnlyCallout() const { return m_pagesOnlyCallout.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    PDFAppToolRedactOverlay* getOverlay() const { return m_overlay.data(); }

    // ------------------------------------------------------------- state

    Mode getMode() const { return m_mode; }

    /// Sets the mode (the checked button of the toolbar; the same mode again = None)
    void setMode(Mode mode);

    /// The marks of the document (the Redact annotations, at most PDFDocumentToolRedact::MAXIMUM_MARK_COUNT)
    const std::vector<PDFAppToolRedactMark>& getMarks() const;
    int getMarkPageCount() const;

    /// The selected mark (the row clicked), invalid = none
    PDFObjectReference getSelectedMark() const { return m_selectedMark; }

    /// Selects the mark: its row, its frame on the page; the page is shown
    void selectMark(PDFObjectReference annotation);

    /// The fill of the model
    Fill getFill() const;
    QColor getFillColor() const;

    /// Writes the fill into the model (one undo step of the parameters): Black, White, a custom color
    /// (\p color), No fill
    void setFill(Fill fill, const QColor& color = QColor());

    /// The canvas shows the preview of the result (437:13219)
    bool isPreview() const { return m_isPreview; }
    void setPreview(bool isPreview);

    /// Preview result can be chosen: some mark, no run
    bool isPreviewAvailable() const;

    // ------------------------------------------------------------- marks (each one commit = one undo step)

    /// A mark of the area of the page (the mode Area); the area is cut to the crop box. \returns the mark was made
    bool addAreaMark(PDFInteger pageIndex, const QRectF& rectangle);

    /// A mark of a text (the mode Text): its quadrilaterals. \returns the mark was made
    bool addTextMark(PDFInteger pageIndex, const QPolygonF& quadrilaterals);

    /// Marks of whole pages (Pages…): the crop box of each page, one commit. A page, which has a mark of the
    /// whole page already, is skipped. \returns some mark was made
    bool markPages(const std::vector<PDFInteger>& pageIndices);

    /// Marks of the matches of the search (one mark of each match, one commit). \returns some mark was made
    bool markMatches(const std::vector<PDFAppToolRedactSearchPart::Match>& matches);

    /// Removes the mark (the × of its row, Delete). A locked mark (ISO 32000-2 Table 167: Locked or
    /// ReadOnly) stays; the status bar says so. \returns the mark was removed
    bool removeMark(PDFObjectReference annotation);

    /// Removes every mark of the list in one commit (Clear All, Remove Marks); the locked marks stay and
    /// the status bar names their count. \returns some mark was removed
    bool removeAllMarks();

    // ------------------------------------------------------------- dialogs

    /// Pages… (437:11653): "Redact whole pages" with the Page selection, Cancel and Mark N Pages
    PDFMessageBox* openPagesDialog();
    PDFMessageBox* getPagesDialog() const { return m_pagesDialog.data(); }
    PDFPageSelection* getPagesSelection() const { return m_pagesSelection.data(); }
    QPushButton* getPagesMarkButton() const { return m_pagesMarkButton.data(); }

    /// The question before the tool closes with marks (448:17473): "Keep the 12 marks in the document?"
    /// with Back to Redacting | Keep Marks and Close (Primary) | Remove Marks. Keep closes the tool, Remove
    /// removes the marks in one step and closes it (PDFDocumentToolShell::closeAfterQuestion) - unless some
    /// marks remain (locked ones, those beyond the listed ones): the tool stays with them, Back returns.
    /// nullptr = no question (no mark: the tool may close). The shell asks it by askClose at Cancel, Esc and
    /// Discard result of the result dialog.
    PDFMessageBox* openClosePrompt();
    PDFMessageBox* getClosePrompt() const { return m_closePrompt.data(); }

    /// Clear All: the question "Remove all 12 marks?" (no frame), then removeAllMarks
    PDFMessageBox* clearAll();
    PDFMessageBox* getClearDialog() const { return m_clearDialog.data(); }

    // ------------------------------------------------------------- PDFDocumentToolGUI

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningTitle(int percentage) const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onDocumentChanged() override;

    /// openClosePrompt (448:17473)
    virtual bool askClose() override;

    /// The marks are changes of the document: Undo of the tool takes them back in its order (P5 open
    /// point 3)
    virtual bool hasDocumentUndoSteps() const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Reads the marks of the document again (a new version)
    void refreshMarks();

    void updateParts();
    void updateToolbar();
    void updateFillParts();
    void updateLeftPanel();
    void updateRightPanel();
    void updateOverlay();
    void updateListHeight();
    void updateDisplayedPage();

    /// The text flows of a page (the text index of the document, when it is built, otherwise the layout
    /// of the view)
    PDFTextFlows getPageTextFlows(PDFInteger pageIndex) const;

    /// The text under the quadrilaterals of a Text mark, of the text flows of its page (getPageTextFlows,
    /// made once per page for all marks of a reading - review RS6APP: 10,000 marks)
    static QString getMarkText(const PDFAppToolRedactMark& mark, const PDFTextFlows& flows);

    /// The marks, which can be added: the limit of 7.2 (MAXIMUM_MARK_COUNT); a refusal is said in the status bar
    bool canAddMarks(size_t count);

    /// The status bar says, that \p count marks are locked and stay in the document
    void showLockedMarks(int count);

    /// Commits a change of the marks as the change of the tool. \returns the commit was accepted; this GUI
    /// may be deleted meanwhile (the caller checks its guard)
    bool commitMarks(const std::optional<PDFModifiedDocument>& modified, quint64 version, const QString& description);

    /// The page has text (the text layout of the view)
    bool hasPageText(PDFInteger pageIndex) const;

    void onAreaClicked();
    void onTextClicked();
    void onPagesClicked();
    void onSearchClicked();
    void onClearClicked();
    void onFillRowClicked();
    void onFillColorSelected(const QColor& color);
    void onAreaPicked(pdf::PDFInteger pageIndex, QRectF rectangle);
    void onTextPicked(pdf::PDFInteger pageIndex, QPolygonF quadrilaterals);
    void onTextNotFound(pdf::PDFInteger pageIndex);
    void onPreviewToggled(bool isPreview);
    void onDisplayedPageChanged();
    void onMarkRowChanged(const QModelIndex& current);
    void onMarkActionTriggered(const QModelIndex& index, int action);
    void onSearchMarkRequested();
    void onSearchMatchesChanged();
    void onSearchBackRequested();
    void onPagesResultChanged();
    void onPagesMarkClicked();
    void onCloseBackClicked();
    void onCloseKeepClicked();
    void onCloseRemoveClicked();
    void onClearConfirmClicked();

    Mode m_mode = Mode::None;
    bool m_isPreview = false;
    bool m_isRunning = false;
    bool m_isMarkListTruncated = false;
    bool m_isMarksRead = false;
    PDFObjectReference m_selectedMark;
    PDFInteger m_noTextPage = -1;               ///< The page of the callout "This page has no text", -1 = none
    std::map<PDFObjectReference, std::pair<QPolygonF, QString>> m_textCache;   ///< Texts of the Text marks by their quadrilaterals

    QPointer<PDFAppToolRedactMarkModel> m_markModel;    ///< Child of the GUI
    QPointer<PDFAppToolRedactSearchPart> m_searchPart;  ///< Child of the GUI
    QPointer<PDFAppToolRedactOverlay> m_overlay;        ///< The shell's
    bool m_isViewConnected = false;

    QPointer<QToolButton> m_areaButton;
    QPointer<QToolButton> m_textButton;
    QPointer<QToolButton> m_pagesButton;
    QPointer<QToolButton> m_searchButton;
    QPointer<QToolButton> m_clearButton;
    QPointer<PDFAppToolRedactFillPanel> m_fillPanel;
    std::map<Fill, QPointer<PDFTileRow>> m_fillRows;
    QPointer<PDFAppToolFlagsMenu> m_keepMenu;

    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<QWidget> m_marksContent;
    QPointer<QWidget> m_searchContent;
    QPointer<PDFAppToolCard> m_marksCard;
    QPointer<PDFListView> m_marksView;
    QPointer<QObject> m_marksViewport;          ///< The viewport of the list, compared by its address in the event filter
    QPointer<QWidget> m_emptyCard;
    QPointer<PDFLabel> m_emptyTitle;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<PDFAppToolCallout> m_noTextCallout;

    QPointer<PDFAppToolPanelHeader> m_rightHeader;
    QPointer<PDFAppToolCallout> m_marksCallout;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<PDFAppToolCallout> m_pagesOnlyCallout;

    QPointer<PDFMessageBox> m_pagesDialog;
    QPointer<PDFPageSelection> m_pagesSelection;
    QPointer<QPushButton> m_pagesMarkButton;
    QPointer<PDFMessageBox> m_closePrompt;
    QPointer<PDFMessageBox> m_clearDialog;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_REDACT_H
