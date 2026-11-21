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

#ifndef PDFAPPTOOLDRAWINGPART_H
#define PDFAPPTOOLDRAWINGPART_H

#include "pdfappglobal.h"
#include "pdfapptoolundotarget.h"

#include "pdfpagedrawing.h"

#include <QPointer>
#include <QVariant>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QAbstractButton;
class QToolButton;

namespace pdf
{

class PDFAppContext;
class PDFAppFileDialogRequest;
class PDFAppParagraph;
class PDFAppToolCard;
class PDFAppToolPanelBuilder;
class PDFAppToolPanelPopup;
class PDFAppToolParameterModel;
class PDFAppToolSectionHeader;
class PDFColorSwatchRow;
class PDFComboBox;
class PDFCreatePCElementTool;
class PDFDocument;
class PDFDoubleSpinBox;
class PDFDrawWidgetProxy;
class PDFLabel;
class PDFPageContentElement;
class PDFPageContentScene;
class PDFSegmentedControl;
class PDFTileRow;
class PDFToolManager;
class PDFWidget;
class PDFAppToolDrawingPart;

/// The list of the elements of the drawing (Sign by Hand 435:8619 card-elements: the captions of the
/// pages and the rows; Sign 434:5210 the rows of the one page): a row per element, the selected
/// element of the scene is the selected row (the old dock Signature Toolbox, R6-24). A click selects
/// the element on the page, a double click on a text label edits its text. A child of its panel; it
/// holds the part by QPointer and is made again by the part, when the elements change.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDrawingElementList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolDrawingElementList(PDFAppToolDrawingPart* part, QWidget* parent);
    virtual ~PDFAppToolDrawingElementList() override;

    /// The rows of the elements in the order of the list (tests), the captions left out
    const std::vector<QPointer<PDFTileRow>>& getRows() const { return m_rows; }

    /// The captions of the pages ("Page 3"), Sign by Hand only
    const std::vector<QPointer<PDFTileRow>>& getCaptions() const { return m_captions; }

    /// The id of the element of a row, 0 = none
    static PDFInteger getElementId(const PDFTileRow* row);

    /// Makes the rows again of the elements of the part
    void rebuild();

    /// Marks the rows of the selected elements
    void updateSelection();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onRowClicked();

    QPointer<PDFAppToolDrawingPart> m_part;
    QPointer<PDFAppToolCard> m_card;
    std::vector<QPointer<PDFTileRow>> m_rows;
    std::vector<QPointer<PDFTileRow>> m_captions;
};

/// The style of the drawing (Sign 434:5697 "Style of the text label", Sign by Hand 435:8874 "Pen and
/// fill" and 435:10179 "Text"): the section header and its card - for a text label the font, its
/// size, the text color (the swatches of the panel, K6.4), the alignment and the angle; for the other
/// elements the pen color, the pen width and the fill. It shows the style of the selected elements
/// or, without a selection, the style of the next element; a change goes to the selected elements
/// (one undo step) and to the next element. A child of its panel, it holds the part by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDrawingStylePanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolDrawingStylePanel(PDFAppToolDrawingPart* part, QWidget* parent);
    virtual ~PDFAppToolDrawingStylePanel() override;

    /// Shows the style and the kind of the part
    void refresh();

    /// The text card is shown (else the pen card)
    bool isTextStyleShown() const;

    /// Parts (tests; nullptr = none)
    PDFAppToolSectionHeader* getHeader() const { return m_header.data(); }
    PDFComboBox* getFontCombo() const { return m_fontCombo.data(); }
    PDFDoubleSpinBox* getFontSizeSpin() const { return m_fontSizeSpin.data(); }
    PDFColorSwatchRow* getTextColorRow() const { return m_textColorRow.data(); }
    PDFLabel* getTextColorName() const { return m_textColorName.data(); }
    PDFSegmentedControl* getAlignmentControl() const { return m_alignmentControl.data(); }
    PDFDoubleSpinBox* getAngleSpin() const { return m_angleSpin.data(); }
    PDFColorSwatchRow* getPenColorRow() const { return m_penColorRow.data(); }
    PDFLabel* getPenColorName() const { return m_penColorName.data(); }
    PDFDoubleSpinBox* getPenWidthSpin() const { return m_penWidthSpin.data(); }
    PDFComboBox* getFillCombo() const { return m_fillCombo.data(); }
    QWidget* getTextCard() const { return m_textCard.data(); }
    QWidget* getPenCard() const { return m_penCard.data(); }

    /// The name of a color for the user ("Black", the names of the swatches; a custom color is its
    /// hexadecimal value)
    static QString getColorName(const QColor& color);

private:
    void onFontChanged();
    void onFontSizeChanged();
    void onTextColorSelected(const QColor& color);
    void onAlignmentChanged(int index);
    void onAngleChanged();
    void onPenColorSelected(const QColor& color);
    void onPenWidthChanged();
    void onFillChanged();

    QPointer<PDFAppToolDrawingPart> m_part;
    QPointer<PDFAppToolSectionHeader> m_header;
    QPointer<QWidget> m_textCard;
    QPointer<QWidget> m_penCard;
    QPointer<PDFComboBox> m_fontCombo;
    QPointer<PDFDoubleSpinBox> m_fontSizeSpin;
    QPointer<PDFColorSwatchRow> m_textColorRow;
    QPointer<PDFLabel> m_textColorName;
    QPointer<PDFSegmentedControl> m_alignmentControl;
    QPointer<PDFDoubleSpinBox> m_angleSpin;
    QPointer<PDFColorSwatchRow> m_penColorRow;
    QPointer<PDFLabel> m_penColorName;
    QPointer<PDFDoubleSpinBox> m_penWidthSpin;
    QPointer<PDFComboBox> m_fillCombo;
    bool m_isRefreshing = false;
};

/// The hint over the canvas of a drawing (434:5596 canvas-hint, 435:8619: Regular 12 text/secondary,
/// centered 20 below the top of the canvas): a text only, it takes no mouse and no focus. A child of
/// the canvas area (the shell deletes it, the canvas area when it dies first); it emits no signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDrawingHint : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int TOP = 20;
    static constexpr int HEIGHT = 15;

    explicit PDFAppToolDrawingHint(QWidget* canvasArea);
    virtual ~PDFAppToolDrawingHint() override;

    void setText(const QString& text);
    QString getText() const;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePlacement();

    QPointer<PDFLabel> m_label;
};

/// The drawing of Sign with Certificate (the visible appearance, 434:4338 menu Draw) and of Sign by Hand
/// (435:8619) on the pages of the document (plan of step 6 of phase 2, 4.5, 4.8, R6-24; package P4
/// implements it): a scene of the page content elements of Pdf4QtLibWidgets (PDFPageContentScene, the
/// tools of pdfpagecontenteditortools as a drawing surface only - Edit Content is not designed, O32),
/// the toolbar of its layout (Sign: the menu Draw of 11 elements, Arrange, Clear All; Sign by Hand: the
/// buttons Text label, Freehand curve, SVG image…, Accept mark, Reject mark, Dot, the menus Shapes and
/// Arrange, Clear All - Arrange by PDFPageContentElementManipulator), the list of the elements by pages
/// in the left panel (in sync with the selection, a double click edits a text - the old dock Signature
/// Toolbox, R6-24) and the style of the right panel (Pen color, Pen width, Fill, the font, the alignment,
/// the angle; the color by the swatches of the panel, K6.4).
///
/// Undo and Redo: the drawing is the value of the parameter drawing of the tool (bindParameter): every
/// change of the scene is one step of the parameter model, which Undo and Redo of the open tool act on
/// (the model is the undo target of the mode); a step taken back gives the scene the drawing of the
/// step. The undo target of the part delegates to the model, so Undo is disabled without a step
/// (K6.10). The drawing is a sensitive value (R6-19): the model never stores it.
///
/// Ownership and lifetime (6.4 of the plan of step 5, 7.2): the part is a child of the GUI of its tool,
/// it owns the scene and the creation tools; the scene is registered in the proxy and the widget of the
/// view by attachToView and unregistered by detachFromView, which the destructor calls before the scene
/// dies; the tools are added to the tool manager of the view (it forgets a destroyed tool). The view,
/// the proxy, the widget, the tool manager and the model are held by QPointer. Every Q_EMIT is the last
/// call of its function. A worker gets only toDrawing (a value).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDrawingPart : public PDFAppToolUndoTarget
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolUndoTarget;

public:
    /// The tool, whose drawing it is (its toolbar and its rules)
    enum class Layout
    {
        SignatureAppearance,    ///< Sign with Certificate: the menu Draw, the elements of one page with graphics (434:7784)
        SignByHand              ///< Sign by Hand: the buttons and Shapes, the elements of any pages
    };

    /// The elements, which can be drawn (the menu Draw of 434:4951, the toolbar and Shapes of 435:9472)
    enum class ElementKind
    {
        Text,
        FreehandCurve,
        SvgImage,
        AcceptMark,
        RejectMark,
        Dot,
        Rectangle,
        RoundedRectangle,
        Line,
        HorizontalLine,
        VerticalLine
    };

    /// The operations of Arrange (434:5421; PDFPageContentElementManipulator::Operation, its order)
    enum class ArrangeOperation
    {
        AlignTop,
        AlignCenterVertically,
        AlignBottom,
        AlignLeft,
        AlignCenterHorizontally,
        AlignRight,
        SameHeight,
        SameWidth,
        SameSize,
        CenterHorizontally,
        CenterVertically,
        CenterBoth,
        InAColumn,
        InARow,
        AsAForm,
        InAGrid
    };

    /// Widths of the menus: Draw 250 (434:5346), Arrange 230 (434:5772), Shapes 250 (435:9472)
    static constexpr int DRAW_PANEL_WIDTH = 250;
    static constexpr int ARRANGE_PANEL_WIDTH = 230;
    static constexpr int SHAPES_PANEL_WIDTH = 250;

    /// Largest SVG image (PDFPageDrawing::MAXIMUM_SVG_SIZE, plan 7.2)
    static constexpr qint64 MAXIMUM_SVG_SIZE = PDFPageDrawing::MAXIMUM_SVG_SIZE;

    explicit PDFAppToolDrawingPart(PDFAppContext* context, Layout layout, QObject* parent);
    virtual ~PDFAppToolDrawingPart() override;

    Layout getLayout() const { return m_layout; }

    /// The toolbar of the layout (see the class)
    void describeToolbar(PDFAppToolPanelBuilder& builder);

    /// The list of the elements by pages (the left panel), a new widget of the parent
    QWidget* createElementList(QWidget* parent);

    /// The style of the selected elements or of the next one (the right panel), a new widget of the parent
    QWidget* createStylePanel(QWidget* parent);

    /// The hint over the canvas (434:5596), a new child of the canvas area; the part keeps its text
    QWidget* createCanvasHint(QWidget* canvasArea);

    /// Shows the scene over the pages of the view (its proxy); nothing for null
    void attachToView(PDFDrawWidgetProxy* proxy);

    /// Takes the scene away from the view (the close of the tool, the destructor)
    void detachFromView();

    /// The drawing is the value of the parameter of the model (see the class): the scene shows the
    /// value of the model now, a change of the scene writes the model, a change of the model (Undo,
    /// Redo) gives the scene its value. Null = none.
    void bindParameter(PDFAppToolParameterModel* model, const QString& parameter);

    /// The drawing as a value of the core (the parameter drawing; plan 10.3)
    PDFPageDrawing toDrawing() const;

    /// Replaces the elements by a drawing (one Undo step)
    void setDrawing(const PDFPageDrawing& drawing);

    /// The scene has no element
    bool isEmpty() const;

    /// The number of the elements
    int getElementCount() const;

    /// The pages with an element (indices from 0, sorted)
    std::vector<PDFInteger> getPageIndices() const;

    /// Removes every element (one Undo step; the GUI asks before, 435:10500)
    void clearAll();

    /// Activates the creation tool of the kind (the next click or drag on a page creates it). The SVG
    /// image asks for its file first (chooseSvgImage).
    void startElement(ElementKind kind);

    /// The kind, whose creation tool is active, std::nullopt = none
    std::optional<ElementKind> getActiveElementKind() const;

    /// Esc of the tool (PDFDocumentToolGUI::handleEscape): the active creation tool ends (an element
    /// being drawn is dropped), the drawing stays. \returns false, when no creation tool was active.
    /// The tool manager tells the view (its slots run): the caller checks its guard.
    bool endActiveElement();

    /// Esc of the tool after endActiveElement (owner 2026-11-18, as the annotations): the selected
    /// elements are deselected (in the scene and in the list), the drawing stays. \returns false, when
    /// nothing was selected. The selection signal of the part is queued.
    bool clearSelection();

    /// What Esc of the tool does in the part now, by the order of the tool (endActiveElement, then
    /// clearSelection): "Esc ends Freehand curve", "Esc clears the selection", empty = nothing
    QString getEscapeHint() const;

    /// The file dialog of an SVG image (A0: the file comes later from the event loop, one dialog at a
    /// time) and its check (the size, the external references by PDFPageDrawing::isValid); a valid
    /// image activates the tool placing it, a file, which is not used, says why (a message box), a
    /// canceled dialog says nothing
    void chooseSvgImage();

    /// Uses the bytes of an SVG image (the check of chooseSvgImage); the tool placing it is active
    bool startSvgImage(const QByteArray& data, QString* errorMessage);

    /// An operation of Arrange over the selected elements (one Undo step)
    void arrange(ArrangeOperation operation);

    /// The elements are selected (in the scene and in the list)
    void selectElements(const std::set<PDFInteger>& ids);
    std::set<PDFInteger> getSelectedElements() const;

    /// The ids of the elements in the order of painting
    std::vector<PDFInteger> getElementIds() const;

    /// The kind, the page, the title ("Freehand curve") and the description (the text of a label in
    /// quotes) of an element, empty for an unknown id
    std::optional<ElementKind> getElementKind(PDFInteger id) const;
    PDFInteger getElementPage(PDFInteger id) const;
    QString getElementTitle(PDFInteger id) const;
    QString getElementDescription(PDFInteger id) const;

    /// The title and the icon of a kind (the menus, the rows)
    static QString getKindTitle(ElementKind kind);
    static QString getKindDescription(ElementKind kind);
    static QString getKindIconName(ElementKind kind);

    /// The text of a text label (the double click, R6-24): a dialog asks for it; one Undo step
    void editText(PDFInteger id);

    /// Sets the text of a text label (one Undo step); false for another element
    bool setElementText(PDFInteger id, const QString& text);

    /// The style shown by the style panel: of the first selected element, else of the next one
    PDFPageDrawing::Style getShownStyle() const;

    /// The style panel shows the text style (a selected text label, the active text tool, Sign without
    /// a selection: its first element is a name)
    bool isTextStyleShown() const;

    /// Changes the style of the selected elements (one Undo step) and of the next element
    void setPenColor(const QColor& color);
    void setPenWidth(double width);
    void setFillColor(const QColor& color);
    void setFontFamily(const QString& family);
    void setFontSize(double size);
    void setTextColor(const QColor& color);
    void setAlignment(PDFPageDrawing::Alignment alignment);
    void setAngle(double angle);

    /// The style of the next element
    const PDFPageDrawing::Style& getNextStyle() const { return m_nextStyle; }

    /// Parts (tests; nullptr = none)
    PDFPageContentScene* getScene() const { return m_scene; }
    PDFAppToolPanelPopup* getDrawPanel() const { return m_drawPanel.data(); }
    PDFAppToolPanelPopup* getShapesPanel() const { return m_shapesPanel.data(); }
    PDFAppToolPanelPopup* getArrangePanel() const { return m_arrangePanel.data(); }
    QToolButton* getClearButton() const { return m_clearButton.data(); }
    QToolButton* getElementButton(ElementKind kind) const;
    PDFTileRow* getDrawRow(ElementKind kind) const;
    PDFTileRow* getArrangeRow(ArrangeOperation operation) const;
    PDFAppToolDrawingElementList* getElementList() const { return m_elementList.data(); }
    PDFAppToolDrawingStylePanel* getStylePanel() const { return m_stylePanel.data(); }
    PDFAppToolDrawingHint* getCanvasHint() const { return m_canvasHint.data(); }
    QWidget* getEditDialog() const { return m_editDialog.data(); }
    bool isAttached() const;

    /// The text of the canvas hint (the GUI gives it, 434:5596, 435:8619)
    void setCanvasHintText(const QString& text);

    /// The toolbar actions are enabled (Sign invisible: Draw, Arrange, Clear All Disabled, 434:6626)
    void setActionsEnabled(bool isEnabled);

    /// The drawing takes the input (the GUI by PDFDocumentToolGUI::setContentEnabled; K6.8, review
    /// RS6APP): while the tool runs, the creation tool ends, the actions are Disabled and the scene is no
    /// input interface of the widget (it keeps drawing the elements) - the run has taken the drawing
    void setInputEnabled(bool isEnabled);
    bool isInputEnabled() const { return m_isInputEnabled; }

    /// The bytes of the accept and reject marks (the scene shows them as images; the core writes its
    /// own marks, PDFPageDrawing::Kind)
    static QByteArray getMarkSvg(bool isAccept);

    virtual bool canUndo() const override;
    virtual bool canRedo() const override;
    virtual void undo() override;
    virtual void redo() override;

Q_SIGNALS:
    /// The elements or their style have changed (the last call of the change)
    void drawingChanged();

    /// The selection or the active creation tool has changed (the style panel follows it)
    void selectionChanged();

    /// A request of Clear All of the toolbar: the GUI asks (435:10500 style) and calls clearAll
    void clearRequested();

private:
    /// Creates the creation tools for the view (attachToView)
    void createTools();
    void deleteTools();

    /// The scene of the drawing, without an Undo step (setDrawing, a step taken back)
    void applyDrawing(const PDFPageDrawing& drawing);

    /// The elements of the scene made of a drawing and back
    static std::vector<std::unique_ptr<PDFPageContentElement>> createElements(const PDFPageDrawing& drawing);
    static std::optional<PDFPageDrawing::Element> createDrawingElement(const PDFPageContentElement* element);
    static std::optional<ElementKind> getKindOfElement(const PDFPageContentElement* element);

    /// Applies the style of the next element to the creation tools
    void updateToolStyles();

    /// The reason, why the chosen SVG image is not used (a message box; the last call of its caller)
    void showSvgError(const QString& errorMessage);

    /// The answer of the file dialog of chooseSvgImage (empty = canceled)
    void onSvgFileChosen(const QString& filePath);

    /// Reads the SVG file (at most MAXIMUM_SVG_SIZE) and uses it by startSvgImage. \returns false and the reason, when
    /// the file is not used
    bool useSvgFile(const QString& filePath, QString* errorMessage);

    /// Changes the style of the selected elements by \p change (one Undo step)
    void changeSelectedStyle(int property, const QVariant& value);

    void updateActions();
    void updateParts();

    /// The scene has changed: the model gets the drawing (not by applyDrawing), the parts follow
    void onSceneChanged(bool graphicsOnly);
    void onSceneChangeQueued();
    void onSceneSelectionChanged();
    void queueSelectionUpdate();
    void onSelectionUpdateQueued();
    void onSceneEditRequest(const std::set<PDFInteger>& elements);
    void onToolActivityChanged();
    void onModelChanged(const QString& name);
    void onModelUndoStateChanged();
    void onDrawRowClicked();
    void onArrangeRowClicked();
    void onElementButtonClicked();
    void onClearClicked();
    void onProxyDestroyed();
    void onEditTextAccepted();

    QPointer<PDFAppContext> m_context;
    Layout m_layout;
    PDFPageContentScene* m_scene = nullptr;     ///< Child object; the destructor detaches it and deletes it later (it may be inside of its own call)
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<PDFWidget> m_widget;
    QPointer<PDFToolManager> m_toolManager;
    QPointer<PDFAppToolParameterModel> m_model;
    QString m_parameter;
    PDFPageDrawing m_lastWrittenDrawing;        ///< The value the part wrote into the model last (its identity)
    PDFPageDrawing::Style m_nextStyle;
    std::map<ElementKind, QPointer<PDFCreatePCElementTool>> m_tools;
    QByteArray m_svgData;                       ///< The image of the SVG tool
    QPointer<PDFAppToolPanelPopup> m_drawPanel;
    QPointer<PDFAppToolPanelPopup> m_shapesPanel;
    QPointer<PDFAppToolPanelPopup> m_arrangePanel;
    QPointer<QToolButton> m_clearButton;
    std::map<ElementKind, QPointer<QToolButton>> m_elementButtons;
    std::map<ElementKind, QPointer<PDFTileRow>> m_drawRows;
    std::map<ArrangeOperation, QPointer<PDFTileRow>> m_arrangeRows;
    QPointer<PDFAppToolDrawingElementList> m_elementList;
    QPointer<PDFAppToolDrawingStylePanel> m_stylePanel;
    QPointer<PDFAppToolDrawingHint> m_canvasHint;
    QPointer<QWidget> m_editDialog;             ///< The form of editText (new, deletes itself on close)
    QPointer<PDFAppFileDialogRequest> m_fileRequest;    ///< The file dialog of an SVG image waiting for its answer (a child of the part, A0)
    QString m_canvasHintText;
    bool m_isApplying = false;      ///< applyDrawing changes the scene: no step of the model
    bool m_isDetaching = false;     ///< detachFromView: the scene reports nothing
    bool m_isActionsEnabled = true;
    bool m_isInputEnabled = true;               ///< setInputEnabled
    bool m_isSceneChangePending = false;        ///< onSceneChangeQueued is queued
    bool m_isSelectionUpdatePending = false;    ///< onSelectionUpdateQueued is queued
};

}   // namespace pdf

#endif // PDFAPPTOOLDRAWINGPART_H
